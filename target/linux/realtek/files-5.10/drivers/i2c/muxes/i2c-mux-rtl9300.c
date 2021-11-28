// SPDX-License-Identifier: GPL-2.0-only
/*
 * I2C multiplexer for the 2 I2C Masters of the RTL9300
 * with up to 8 channels each, but which are not entirely
 * independent of each other
 */
#include <linux/i2c-mux.h>
#include <linux/module.h>
#include <linux/mux/consumer.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>

#include "../busses/i2c-rtl9300.h"

#define NUM_MASTERS		2
#define NUM_BUSSES		8

#define REG(mst, x)	(mux->base + x + (mst ? 0x1c : 0))
#define REG_MASK(mst, clear, set, reg)	\
			writel((readl(REG((mst),(reg))) & ~(clear)) | (set), REG((mst),(reg)))

struct channel {
	u8 sda_num;
	u8 scl_num;
};

static struct channel channels[NUM_MASTERS * NUM_BUSSES];

struct rtl9300_mux {
	void __iomem *base;
	struct device *dev;
	struct i2c_adapter *parent;
	struct rtl9300_i2c * i2c;
};

static int rtl9300_i2c_mux_select(struct i2c_mux_core *muxc, u32 chan)
{
	struct rtl9300_mux *mux = i2c_mux_priv(muxc);

	// Set SCL pin
	REG_MASK(channels[chan].scl_num, 0, BIT(I2C_CTRL1_GPIO8_SCL_SEL), I2C_CTRL1);

	// Set SDA pin
	REG_MASK(channels[chan].scl_num, 0x7 << I2C_CTRL1_SDA_OUT_SEL,
		 channels[chan].sda_num << I2C_CTRL1_SDA_OUT_SEL, I2C_CTRL1);

	mux->i2c->sda_num = channels[chan].sda_num;
	mux->i2c->scl_num = channels[chan].scl_num;

	return 0;
}

static int rtl9300_i2c_mux_deselect(struct i2c_mux_core *muxc, u32 chan)
{
	return 0;
}

static struct device_node *mux_parent_adapter(struct device *dev, struct rtl9300_mux *mux)
{
	struct device_node *node = dev->of_node;
	struct device_node *parent_np;
	struct i2c_adapter *parent;

	parent_np = of_parse_phandle(node, "i2c-parent", 0);
	if (!parent_np) {
		dev_err(dev, "Cannot parse i2c-parent\n");
		return ERR_PTR(-ENODEV);
	}
	parent = of_find_i2c_adapter_by_node(parent_np);
	of_node_put(parent_np);
	if (!parent)
		return ERR_PTR(-EPROBE_DEFER);

	if (!of_device_is_compatible(parent_np, "realtek,rtl9300-i2c")){
		dev_err(dev, "I2C parent not an RTL9300 I2C controller\n");
		return ERR_PTR(-ENODEV);
	}

	mux->parent = parent;
	mux->i2c = (struct rtl9300_i2c *)i2c_get_adapdata(parent);
	mux->base = mux->i2c->base;

	return parent_np;
}

static const struct of_device_id rtl9300_i2c_mux_of_match[] = {
	{ .compatible = "realtek,i2c-mux-rtl9300", },
	{},
};

MODULE_DEVICE_TABLE(of, rtl9300_i2c_mux_of_match);

static int rtl9300_i2c_mux_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct device_node *parent_np;
	struct device_node *child;
	struct i2c_mux_core *muxc;
	struct rtl9300_mux *mux;
	int children;
	int ret;

	pr_info("%s probing I2C adapter\n", __func__);
	
	if (!node) {
		dev_err(dev, "No DT found\n");
		return -EINVAL;
	}

	mux = devm_kzalloc(dev, sizeof(*mux), GFP_KERNEL);
	if (!mux)
		return -ENOMEM;

	mux->dev = dev;

	parent_np = mux_parent_adapter(dev, mux);
	if (IS_ERR(parent_np))
		return dev_err_probe(dev, PTR_ERR(parent_np), "i2c-parent adapter not found\n");

	pr_info("%s base memory %08x\n", __func__, (u32)mux->base);

	children = of_get_child_count(node);

	muxc = i2c_mux_alloc(mux->parent, dev, children, 0, 0,
			     rtl9300_i2c_mux_select, rtl9300_i2c_mux_deselect);
	if (!muxc) {
		ret = -ENOMEM;
		goto err_parent;
	}
	muxc->priv = mux;

	platform_set_drvdata(pdev, muxc);

	for_each_child_of_node(node, child) {
		u32 chan;
		u32 pin;
		u32 v;

		ret = of_property_read_u32(child, "reg", &chan);
		if (ret < 0) {
			dev_err(dev, "no reg property for node '%pOFn'\n",
				child);
			goto err_children;
		}

		if (chan >= NUM_MASTERS * NUM_BUSSES) {
			dev_err(dev, "invalid reg %u\n", chan);
			ret = -EINVAL;
			goto err_children;
		}

		if (of_property_read_u32(child, "scl-pin", &pin)) {
			dev_warn(dev, "SCL pin not found in DT, using default\n");
			pin = 8;
		}
		if (!(pin == 8 || pin == 17)) {
			dev_warn(dev, "SCL pin %d not supported\n", pin);
			ret = -EINVAL;
			goto err_children;
		}
		channels[chan].scl_num = pin == 8 ? 0 : 1;
		pr_info("%s channel %d scl_num %d\n", __func__, chan, channels[chan].scl_num);

		if (of_property_read_u32(child, "sda-pin", &pin)) {
			dev_warn(dev, "SDA pin not found in DT, using default \n");
			pin = 9;
		}
		channels[chan].sda_num = pin - 9;
		if (channels[chan].sda_num < 0 || channels[chan].sda_num > 7) {
			dev_warn(dev, "SDA pin %d not supported\n", pin);
			return -EINVAL;
		}
		pr_info("%s channel %d sda_num %d\n", __func__, chan, channels[chan].sda_num);

		// Set SDA pin to I2C functionality
		v = readl(REG(0, I2C_MST_GLB_CTRL));
		v |= BIT(channels[chan].sda_num);
		writel(v, REG(0, I2C_MST_GLB_CTRL));

		ret = i2c_mux_add_adapter(muxc, 0, chan, 0);
		if (ret)
			goto err_children;
	}

	dev_info(dev, "%d-port mux on %s adapter\n", children, mux->parent->name);

	return 0;

err_children:
	i2c_mux_del_adapters(muxc);
err_parent:
	i2c_put_adapter(mux->parent);

	return ret;
}

static int rtl9300_i2c_mux_remove(struct platform_device *pdev)
{
	struct i2c_mux_core *muxc = platform_get_drvdata(pdev);

	i2c_mux_del_adapters(muxc);
	i2c_put_adapter(muxc->parent);

	return 0;
}

static struct platform_driver i2c_mux_driver = {
	.probe	= rtl9300_i2c_mux_probe,
	.remove	= rtl9300_i2c_mux_remove,
	.driver	= {
		.name	= "i2c-mux-rtl9300",
		.of_match_table = rtl9300_i2c_mux_of_match,
	},
};
module_platform_driver(i2c_mux_driver);

MODULE_DESCRIPTION("RTL9300 I2C multiplexer driver");
MODULE_AUTHOR("Birger Koblitz");
MODULE_LICENSE("GPL v2");
