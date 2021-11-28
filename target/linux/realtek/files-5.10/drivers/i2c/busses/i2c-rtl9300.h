#ifndef I2C_RTL9300_H
#define I2C_RTL9300_H

#include <linux/i2c.h>

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

#define I2C_DATA_WORD0			0x08

#define I2C_MST_GLB_CTRL		0x18

#define RTL9300_I2C_STD_FREQ		0
#define RTL9300_I2C_FAST_FREQ		1


struct rtl9300_i2c {
	void __iomem *base;		// Must be first for i2c-mux-rtl9300 to pick up
	struct device *dev;
	struct i2c_adapter adap;
	u8 bus_freq;
	u8 sda_num;			// SDA channel number
	u8 scl_num;			// SCL channel, mapping to master 1 or 2
};

#endif
