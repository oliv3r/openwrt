// SPDX-License-Identifier: GPL-2.0-only

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_platform.h>

#define I2C_CTRL1			0x00
#define I2C_CTRL1_MEM_ADDR		8
#define I2C_CTRL1_SDA_OUT_SEL		4
#define I2C_CTRL1_GPIO8_SCL_SEL		3
#define I2C_CTRL1_RWOP			2
#define I2C_CTRL1_I2C_FAIL		1
#define I2C_CTRL1_I2C_TRIG		0

#define I2C_CTRL2			0x04
#define I2C_CTRL2_DRIVE_ACK_DELAY	20
#define I2C_CTRL2_CHECK_ACK_DELAY	16
#define I2C_CTRL2_READ_MODE		15
#define I2C_CTRL2_DEV_ADDR		8
#define I2C_CTRL2_DATA_WIDTH		4
#define I2C_CTRL2_MADDR_WIDTH		2
#define I2C_CTRL2_SCL_FREQ		0

#define I2C_DATA_WORD0		0x08

#define I2C_MST_GLB_CTRL	0x18

#define RTL9300_I2C_STD_FREQ	0
#define RTL9300_I2C_FAST_FREQ	1

#define REG(x)		(i2c->base + x + (i2c->scl_num ? 0x1c : 0))
#define REG_MASK(clear, set, reg)	\
			writel((readl(REG(reg)) & ~(clear)) | (set), REG(reg))

DEFINE_MUTEX(i2c_lock);

struct rtl9300_i2c {
	void __iomem *base;		// Must be first for i2c-mux-rtl9300 to pick up
	struct device *dev;
	struct i2c_adapter adap;
	u8 bus_freq;
	u8 sda_num;			// SDA channel number
	u8 scl_num;			// SCL channel, mapping to master 1 or 2
};


static void rtl9300_i2c_reg_addr_set(struct rtl9300_i2c *i2c, u32 reg, u16 len)
{
	// Set register address width
	REG_MASK(0x3 << I2C_CTRL2_MADDR_WIDTH, len << I2C_CTRL2_MADDR_WIDTH, I2C_CTRL2);

	// Set register address
	REG_MASK(0xffffff << I2C_CTRL1_MEM_ADDR, reg << I2C_CTRL1_MEM_ADDR, I2C_CTRL1);
}

static void rtl9300_i2c_config_io(struct rtl9300_i2c *i2c, int scl_num, int sda_num)
{
	u32 v;

	// Set SCL pin
	REG_MASK(0, BIT(I2C_CTRL1_GPIO8_SCL_SEL), I2C_CTRL1);

	// Set SDA pin
	REG_MASK(0x7 << I2C_CTRL1_SDA_OUT_SEL, i2c->sda_num << I2C_CTRL1_SDA_OUT_SEL, I2C_CTRL1);

	// Set SDA pin to I2C functionality
	v = readl(i2c->base + I2C_MST_GLB_CTRL);
	v |= BIT(i2c->sda_num);
	writel(v, i2c->base + I2C_MST_GLB_CTRL);
}

static int rtl9300_i2c_config_xfer(struct rtl9300_i2c *i2c, u16 addr, u16 len)
{
//	rtl9300_i2c_config_io(i2c, i2c->scl_num, i2c->sda_num);

	// Set bus frequency
	REG_MASK(0x3 << I2C_CTRL2_SCL_FREQ, i2c->bus_freq << I2C_CTRL2_SCL_FREQ, I2C_CTRL2);

	// Set slave device address
	REG_MASK(0x7f << I2C_CTRL2_DEV_ADDR, addr << I2C_CTRL2_DEV_ADDR, I2C_CTRL2);

	// Set data length
	REG_MASK(0xf << I2C_CTRL2_DATA_WIDTH, (len - 1) << I2C_CTRL2_DATA_WIDTH, I2C_CTRL2);

	// Set register address width to 0, register address, too
	rtl9300_i2c_reg_addr_set(i2c, 0, 0);

	// Set read mode to random
	REG_MASK(0x1 << I2C_CTRL2_READ_MODE, 0, I2C_CTRL2);

	// Check the ACK delay to cause transfer fails
	REG_MASK(0xf << I2C_CTRL2_CHECK_ACK_DELAY, 2 << I2C_CTRL2_CHECK_ACK_DELAY, I2C_CTRL2);
	REG_MASK(0xf << I2C_CTRL2_DRIVE_ACK_DELAY, 4 << I2C_CTRL2_DRIVE_ACK_DELAY, I2C_CTRL2);

	pr_debug("%s CTRL1: %08x, CTRL2: %08x\n", __func__,
		 *(u32 *)REG(I2C_CTRL1), *(u32 *)REG(I2C_CTRL2));
	return 0;
}

static int rtl9300_i2c_read(struct rtl9300_i2c *i2c, u8 *buf, int len)
{
	int i;
	u32 v;

	pr_debug("%s data: %08x %08x %08x %08x\n", __func__,
		*(u32 *)REG(I2C_DATA_WORD0), *(u32 *)REG(I2C_DATA_WORD0 + 4),
		*(u32 *)REG(I2C_DATA_WORD0 + 8), *(u32 *)REG(I2C_DATA_WORD0 + 12));

	if (len > 16)
		return -EIO;

	for (i = 0; i < len; i++) {
		if (i % 4 == 0)
			v = readl(REG(I2C_DATA_WORD0 + i));
		buf[i] = v;
		v >>= 8;
	}

	return len;
}

static int rtl9300_i2c_write(struct rtl9300_i2c *i2c, u8 *buf, int len)
{
	u32 v;
	int i;

	if (len > 16)
		return -EIO;

	for (i = 0; i < len; i++) {
		if (! (i % 4))
			v = 0;
		v <<= 8;
		v |= buf[i];
		if (i % 4 == 3 || i == len - 1)
			writel(v, REG(I2C_DATA_WORD0 + (i / 4) * 4));
	}

	pr_debug("%s data: %08x %08x %08x %08x\n", __func__,
		*(u32 *)REG(I2C_DATA_WORD0), *(u32 *)REG(I2C_DATA_WORD0 + 4),
		*(u32 *)REG(I2C_DATA_WORD0 + 8), *(u32 *)REG(I2C_DATA_WORD0 + 12));

	return len;
}

static int rtl9300_execute_xfer(struct rtl9300_i2c *i2c)
{
	u32 v;

	REG_MASK(0, BIT(I2C_CTRL1_I2C_TRIG), I2C_CTRL1);
	do {
		v = readl(REG(I2C_CTRL1));
	} while (v & BIT(I2C_CTRL1_I2C_TRIG));

	if (v & BIT(I2C_CTRL1_I2C_FAIL))
		return -EIO;

	return 0;
}

static int rtl9300_i2c_master_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	struct rtl9300_i2c *i2c = i2c_get_adapdata(adap);
	struct i2c_msg *pmsg;
	int ret = 0, i;

	mutex_lock(&i2c_lock);
	for (i = 0; i < num; i++) {
		pmsg = &msgs[i];

		pr_debug("%s flags: %04x len %d addr %08x\n", __func__,
			pmsg->flags, pmsg->len, pmsg->addr);

		rtl9300_i2c_config_xfer(i2c, pmsg->addr, pmsg->len);

		if (pmsg->flags & I2C_M_RD) {
			REG_MASK(BIT(I2C_CTRL1_RWOP), 0, I2C_CTRL1);

			ret = rtl9300_execute_xfer(i2c);
			if (ret) {
				pr_debug("Read failed\n");
				break;
			}
			rtl9300_i2c_read(i2c, pmsg->buf, pmsg->len);
		} else {
			REG_MASK(0, BIT(I2C_CTRL1_RWOP), I2C_CTRL1);

			rtl9300_i2c_write(i2c, pmsg->buf, pmsg->len);
			ret = rtl9300_execute_xfer(i2c);
			if (ret) {
				pr_debug("Write failed\n");
				break;
			}
		}
	}
	mutex_unlock(&i2c_lock);

	if (ret)
		return ret;
	return i;
}


static u32 rtl9300_i2c_func(struct i2c_adapter *a)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm rtl9300_i2c_algo = {
	.master_xfer	= rtl9300_i2c_master_xfer,
	.functionality	= rtl9300_i2c_func,
};

struct i2c_adapter_quirks rtl9300_i2c_quirks = {
	.flags		= I2C_AQ_NO_CLK_STRETCH,
	.max_read_len	= 16,
	.max_write_len	= 16,
};

static int rtl9300_i2c_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct rtl9300_i2c *i2c;
	struct i2c_adapter *adap;
	struct device_node *node = pdev->dev.of_node;
	u32 clock_freq, pin;
	int ret = 0;

	pr_info("%s probing I2C adapter\n", __func__);
	
	if (!node) {
		dev_err(i2c->dev, "No DT found\n");
		return -EINVAL;
	}

	pr_info("%s: DT found\n", __func__);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	i2c = devm_kzalloc(&pdev->dev, sizeof(struct rtl9300_i2c), GFP_KERNEL);
	if (!i2c)
		return -ENOMEM;

	i2c->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(i2c->base))
		return PTR_ERR(i2c->base);

	pr_info("%s base memory %08x\n", __func__, (u32)i2c->base);
	i2c->dev = &pdev->dev;

	if (of_property_read_u32(node, "clock-frequency", &clock_freq)) {
		clock_freq = I2C_MAX_STANDARD_MODE_FREQ;
	}
	switch(clock_freq) {
	case I2C_MAX_STANDARD_MODE_FREQ:
		i2c->bus_freq = RTL9300_I2C_STD_FREQ;
		break;
	
	case I2C_MAX_FAST_MODE_FREQ:
		i2c->bus_freq = RTL9300_I2C_FAST_FREQ;
		break;
	default:
		dev_warn(i2c->dev, "clock-frequency %d not supported\n", clock_freq);
		return -EINVAL;
	}

	dev_info(&pdev->dev, "SCL speed %d, mode is %d\n", clock_freq, i2c->bus_freq);

	if (of_property_read_u32(node, "scl-pin", &pin)) {
		dev_warn(i2c->dev, "SCL pin not found in DT, using default\n");
		pin = 8;
	}
	if (!(pin == 8 || pin == 17)) {
		dev_warn(i2c->dev, "SCL pin %d not supported\n", pin);
		return -EINVAL;
	}
	i2c->scl_num = pin == 8 ? 0 : 1;
	pr_info("%s scl_num %d\n", __func__, i2c->scl_num);

	if (of_property_read_u32(node, "sda-pin", &pin)) {
		dev_warn(i2c->dev, "SDA pin not found in DT, using default \n");
		pin = 9;
	}
	i2c->sda_num = pin - 9;
	if (i2c->sda_num < 0 || i2c->sda_num > 7) {
		dev_warn(i2c->dev, "SDA pin %d not supported\n", pin);
		return -EINVAL;
	}
	pr_info("%s sda_num %d\n", __func__, i2c->sda_num);

	adap = &i2c->adap;
	adap->owner = THIS_MODULE;
	adap->algo = &rtl9300_i2c_algo;
	adap->retries = 3;
	adap->dev.parent = &pdev->dev;
	i2c_set_adapdata(adap, i2c);
	adap->dev.of_node = node;
	strlcpy(adap->name, dev_name(&pdev->dev), sizeof(adap->name));

	platform_set_drvdata(pdev, i2c);

	rtl9300_i2c_config_io(i2c, i2c->scl_num, i2c->sda_num);

	ret = i2c_add_adapter(adap);

	return ret;
}

static int rtl9300_i2c_remove(struct platform_device *pdev)
{
	struct rtl9300_i2c *i2c = platform_get_drvdata(pdev);

	i2c_del_adapter(&i2c->adap);

	return 0;
}

static const struct of_device_id i2c_rtl9300_dt_ids[] = {
	{ .compatible = "realtek,rtl9300-i2c" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rtl838x_eth_of_ids);

static struct platform_driver rtl9300_i2c_driver = {
	.probe		= rtl9300_i2c_probe,
	.remove		= rtl9300_i2c_remove,
	.driver		= {
		.name	= "i2c-rtl9300",
		.pm 	= NULL,
		.of_match_table = i2c_rtl9300_dt_ids,
	},
};

module_platform_driver(rtl9300_i2c_driver);

MODULE_AUTHOR("Birger Koblitz");
MODULE_DESCRIPTION("RTL9300 I2C host driver");
MODULE_LICENSE("GPL v2");
