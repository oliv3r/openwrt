// SPDX-License-Identifier: GPL-2.0-only
/* Realtek RTL838X Ethernet MDIO interface driver
 *
 * Copyright (C) 2020 B. Koblitz
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/phy.h>
#include <linux/netdevice.h>
#include <linux/firmware.h>
#include <linux/crc32.h>
#include <linux/mdio.h>

#include <asm/mach-rtl838x/mach-rtl83xx.h>
#include "rtl83xx-phy.h"

extern struct rtl83xx_soc_info soc_info;
extern struct mutex smi_lock;

#define PHY_CTRL_REG	0
#define PHY_POWER_BIT	11

#define PHY_PAGE_2	2
#define PHY_PAGE_4	4

/* all Clause-22 RealTek MDIO PHYs use register 0x1f for page select */
#define RTL8XXX_PAGE_SELECT		0x1f

#define RTL8XXX_PAGE_MAIN		0x0000
#define RTL821X_PAGE_PORT		0x0266
#define RTL821X_PAGE_POWER		0x0a40
#define RTL821X_PAGE_GPHY		0x0a42
#define RTL821X_PAGE_MAC		0x0a43
#define RTL821X_PAGE_STATE		0x0b80
#define RTL821X_PAGE_PATCH		0x0b82

/* The RTL8214QF is a variant of the RTL8295 and RTL8295R (the internal SerDes
 * for the 10GBit ports of the RTL8396) with the same internal register
 * layout */
#define RTL8295_PAGE_SDS_CTRL_S0	0x0005
#define RTL8295_SDS_CTRL_CTRL_REG_S0	17

int rtl8295_sds_ctrl_regs[] = {17, 18, 19, 0, 20, 21, 22, 23};
#define PHY_8295_PAGE_BASE_OFFSET_S0	256

static u32  rtl8295_sds_page_offset[] = {
	PHY_8295_PAGE_BASE_OFFSET_S0,	// Serdes S0
	768,	// Serdes S1
	512,	// S0_SLV
	2304,	// S1_SLV
	1024,	// Serdes S4
	1280,	// Serdes S5
	1536,	// Serdes S6
	1792,	// Serdes S7
	2048,	// Broadcast
};

#define RTL8295_SDS0_ANA_SPD_5G_REG21_PAGE	(426 - PHY_8295_PAGE_BASE_OFFSET_S0)
#define RTL8295_SDS0_ANA_SPD_5G_REG21_REG	21
#define RTL8295_SDS0_ANA_MISC_REG02_PAGE	(384 - PHY_8295_PAGE_BASE_OFFSET_S0)
#define	RTL8295_SDS0_ANA_MISC_REG02_REG		18
#define RTL8295_SDS0_ANA_SPD_1P25G_REG08_PAGE	(401 - PHY_8295_PAGE_BASE_OFFSET_S0)
#define RTL8295_SDS0_ANA_SPD_1P25G_REG08_REG 	16
#define RTL8295_SDS0_SDS_EXT_REG00_PAGE		(260 - PHY_8295_PAGE_BASE_OFFSET_S0)
#define RTL8295_SDS0_SDS_EXT_REG00_REG		16
#define RTL8295_SDS0_SDS_REG14_PAGE		(257 - PHY_8295_PAGE_BASE_OFFSET_S0)
#define RTL8295_SDS0_SDS_REG14_REG		22

#define RTL8295_SDS_MODE_SGMII		0x2
#define RTL8295_SDS_MODE_FIB1G		0x4
#define RTL8295_SDS_MODE_FIB100M	0x5
#define RTL8295_SDS_MODE_QSGMII		0x6
#define RTL8295_SDS_MODE_OFF		0x1f

/*
 * Using the special page 0xfff with the MDIO controller found in
 * RealTek SoCs allows to access the PHY in RAW mode, ie. bypassing
 * the cache and paging engine of the MDIO controller.
 */
#define RTL83XX_PAGE_RAW		0x0fff

/* internal RTL821X PHY uses register 0x1d to select media page */
#define RTL821XINT_MEDIA_PAGE_SELECT	0x1d
/* external RTL821X PHY uses register 0x1e to select media page */
#define RTL821XEXT_MEDIA_PAGE_SELECT	0x1e

#define RTL821X_MEDIA_PAGE_AUTO		0
#define RTL821X_MEDIA_PAGE_COPPER	1
#define RTL821X_MEDIA_PAGE_FIBRE	3
#define RTL821X_MEDIA_PAGE_INTERNAL	8

#define RTL9300_PHY_ID_MASK 0xf0ffffff

/*
 * This lock protects the state of the SoC automatically polling the PHYs over the SMI
 * bus to detect e.g. link and media changes. For operations on the PHYs such as
 * patching or other configuration changes such as EEE, polling needs to be disabled
 * since otherwise these operations may fails or lead to unpredictable results.
 */
DEFINE_MUTEX(poll_lock);

static const struct firmware rtl838x_8380_fw;
static const struct firmware rtl838x_8214fc_fw;
static const struct firmware rtl838x_8218b_fw;

static u64 disable_polling(int port)
{
	u64 saved_state;

	mutex_lock(&poll_lock);

	switch (soc_info.family) {
	case RTL8380_FAMILY_ID:
		saved_state = sw_r32(RTL838X_SMI_POLL_CTRL);
		sw_w32_mask(BIT(port), 0, RTL838X_SMI_POLL_CTRL);
		break;
	case RTL8390_FAMILY_ID:
		saved_state = sw_r32(RTL839X_SMI_PORT_POLLING_CTRL + 4);
		saved_state <<= 32;
		saved_state |= sw_r32(RTL839X_SMI_PORT_POLLING_CTRL);
		sw_w32_mask(BIT(port % 32), 0,
			    RTL839X_SMI_PORT_POLLING_CTRL + ((port >> 5) << 2));
		break;
	case RTL9300_FAMILY_ID:
		saved_state = sw_r32(RTL930X_SMI_POLL_CTRL);
		sw_w32_mask(BIT(port), 0, RTL930X_SMI_POLL_CTRL);
		break;
	case RTL9310_FAMILY_ID:
		pr_warn("%s not implemented for RTL931X\n", __func__);
		break;
	}

	mutex_unlock(&poll_lock);

	return saved_state;
}

static int resume_polling(u64 saved_state)
{
	mutex_lock(&poll_lock);

	switch (soc_info.family) {
	case RTL8380_FAMILY_ID:
		sw_w32(saved_state, RTL838X_SMI_POLL_CTRL);
		break;
	case RTL8390_FAMILY_ID:
		sw_w32(saved_state >> 32, RTL839X_SMI_PORT_POLLING_CTRL + 4);
		sw_w32(saved_state, RTL839X_SMI_PORT_POLLING_CTRL);
		break;
	case RTL9300_FAMILY_ID:
		sw_w32(saved_state, RTL930X_SMI_POLL_CTRL);
		break;
	case RTL9310_FAMILY_ID:
		pr_warn("%s not implemented for RTL931X\n", __func__);
		break;
	}

	mutex_unlock(&poll_lock);

	return 0;
}

static void rtl8380_int_phy_on_off(struct phy_device *phydev, bool on)
{
	phy_modify(phydev, 0, BIT(11), on?0:BIT(11));
}

static void rtl8380_rtl8214fc_on_off(struct phy_device *phydev, bool on)
{
	/* fiber ports */
	phy_write_paged(phydev, RTL83XX_PAGE_RAW, RTL821XEXT_MEDIA_PAGE_SELECT, RTL821X_MEDIA_PAGE_FIBRE);
	phy_modify(phydev, 0x10, BIT(11), on?0:BIT(11));

	/* copper ports */
	phy_write_paged(phydev, RTL83XX_PAGE_RAW, RTL821XEXT_MEDIA_PAGE_SELECT, RTL821X_MEDIA_PAGE_COPPER);
	phy_modify_paged(phydev, RTL821X_PAGE_POWER, 0x10, BIT(11), on?0:BIT(11));
}

static void rtl8380_phy_reset(struct phy_device *phydev)
{
	phy_modify(phydev, 0, BIT(15), BIT(15));
}

// The access registers for SDS_MODE_SEL and the LSB for each SDS within
u16 rtl9300_sds_regs[] = { 0x0194, 0x0194, 0x0194, 0x0194, 0x02a0, 0x02a0, 0x02a0, 0x02a0,
			   0x02A4, 0x02A4, 0x0198, 0x0198 };
u8  rtl9300_sds_lsb[]  = { 0, 6, 12, 18, 0, 6, 12, 18, 0, 6, 0, 6};
u16 rtl9300_sds_sub_reg[] = {0x1CC, 0x1CC, 0x2D8, 0x2D8, 0x2D8, 0x2D8, 0x2D8, 0x2D8, 0x2D8};
u8  rtl9300_sds_sub_lsb[] = {0, 5, 0, 5, 10, 15, 20, 25};

void rtl9300_sds_set(int sds_num, u32 mode)
{
	pr_info("%s set serdes %d to mode 0x%x\n", __func__, sds_num, mode);

	sw_w32_mask(0x1f << rtl9300_sds_lsb[sds_num], mode << rtl9300_sds_lsb[sds_num],
		    rtl9300_sds_regs[sds_num]);

	// For USXGMII we need to set the 10GSXGMII sub-mode 0
	if (mode == 0x0d) {
		sw_w32_mask(0x1f << rtl9300_sds_sub_lsb[sds_num -2], 0,
			    rtl9300_sds_sub_reg[sds_num -2]);
		sw_w32(0x00840000, 0x2a8 + (sds_num - 4) * 4);
		sw_w32(0x0003ff00, 0x1c4);
	}
	mdelay(10);
}

/*
 * Reset the SerDes by powering it off and set a new operations mode
 * of the SerDes. 0x1f is off. Other modes are
 * 0x02: SGMII		0x04: 1000BX_FIBER	0x05: FIBER100
 * 0x06: QSGMII		0x09: RSGMII		0x0d: USXGMII
 * 0x10: XSGMII		0x12: HISGMII		0x16: 2500Base_X
 * 0x17: RXAUI_LITE	0x19: RXAUI_PLUS	0x1a: 10G Base-R
 * 0x1b: 10GR1000BX_AUTO			0x1f: OFF
 * Note that this function is not used with SerDes which are wired
 * to be used with 10GR, 1000BX_FIBER, HISGMII or 2500Base_X. For them
 * the SDS mode is set to "off" and the actual mode is set via the
 * rtl9300_force_sds_mode
 */
void rtl9300_sds_rst(int sds_num, u32 mode)
{
	pr_info("%s SDS %d to mode 0x%x\n", __func__, sds_num, mode);
	if (sds_num < 0 || sds_num > 11) {
		pr_err("Wrong SerDes number: %d\n", sds_num);
		return;
	}
	rtl9300_sds_set(sds_num, 0x1f);
	rtl9300_sds_set(sds_num, mode);

	pr_info("%s: 194:%08x 198:%08x 2a0:%08x 2a4:%08x\n", __func__,
		sw_r32(0x194), sw_r32(0x198), sw_r32(0x2a0), sw_r32(0x2a4));
}

u32 rtl9300_sds_mode_get(int sds_num)
{
	u32 v;

	if (sds_num < 0 || sds_num > 11) {
		pr_err("Wrong SerDes number: %d\n", sds_num);
		return 0;
	}

	v = sw_r32(rtl9300_sds_regs[sds_num]);
	v >>= rtl9300_sds_lsb[sds_num];

	return v & 0x1f;
}

/*
 * On the RTL839x family of SoCs with inbuilt SerDes, these SerDes are accessed through
 * a 2048 bit register that holds the contents of the PHY being simulated by the SoC.
 */
int rtl839x_read_sds_phy(int phy_addr, int phy_reg)
{
	int offset = 0;
	int reg;
	u32 val;

	if (phy_addr == 49)
		offset = 0x100;

	/*
	 * For the RTL8393 internal SerDes, we simulate a PHY ID in registers 2/3
	 * which would otherwise read as 0.
	 */
	if (soc_info.id == 0x8393) {
		if (phy_reg == 2)
			return 0x1c;
		if (phy_reg == 3)
			return 0x8393;
	}

	/*
	 * Register RTL839X_SDS12_13_XSG0 is 2048 bit broad, the MSB (bit 15) of the
	 * 0th PHY register is bit 1023 (in byte 0x80). Because PHY-registers are 16
	 * bit broad, we offset by reg << 1. In the SoC 2 registers are stored in
	 * one 32 bit register.
	 */
	reg = (phy_reg << 1) & 0xfc;
	val = sw_r32(RTL839X_SDS12_13_XSG0 + offset + 0x80 + reg);

	if (phy_reg & 1)
		val = (val >> 16) & 0xffff;
	else
		val &= 0xffff;

	pr_info("%s: phy_addr %d phy_reg: %d, read %x\n", __func__, phy_addr, phy_reg, val);
	return val;
}

/*
 * On the RTL930x family of SoCs, the internal SerDes are accessed through an IO
 * register which simulates commands to an internal MDIO bus.
 */
int rtl930x_read_sds_phy(int phy_addr, int page, int phy_reg)
{
	int i;
	u32 cmd = phy_addr << 2 | page << 7 | phy_reg << 13 | 1;

	sw_w32(cmd, RTL930X_SDS_INDACS_CMD);

	for (i = 0; i < 100; i++) {
		if (!(sw_r32(RTL930X_SDS_INDACS_CMD) & 0x1))
			break;
		mdelay(1);
	}

	if (i >= 100)
		return -EIO;

	return sw_r32(RTL930X_SDS_INDACS_DATA) & 0xffff;
}

int rtl930x_write_sds_phy(int phy_addr, int page, int phy_reg, u16 v)
{
	int i;
	u32 cmd;

	sw_w32(v, RTL930X_SDS_INDACS_DATA);
	cmd = phy_addr << 2 | page << 7 | phy_reg << 13 | 0x3;
	sw_w32(cmd, RTL930X_SDS_INDACS_CMD);

	for (i = 0; i < 100; i++) {
		if (!(sw_r32(RTL930X_SDS_INDACS_CMD) & 0x1))
			break;
		mdelay(1);
	}


	if (i >= 100) {
		pr_info("%s ERROR !!!!!!!!!!!!!!!!!!!!\n", __func__);
		return -EIO;
	}

	return 0;
}

int rtl931x_read_sds_phy(int phy_addr, int page, int phy_reg)
{
	int i;
	u32 cmd = phy_addr << 2 | page << 7 | phy_reg << 13 | 1;

	pr_debug("%s: phy_addr(SDS-ID) %d, phy_reg: %d\n", __func__, phy_addr, phy_reg);
	sw_w32(cmd, RTL931X_SERDES_INDRT_ACCESS_CTRL);

	for (i = 0; i < 100; i++) {
		if (!(sw_r32(RTL931X_SERDES_INDRT_ACCESS_CTRL) & 0x1))
			break;
		mdelay(1);
	}

	if (i >= 100)
		return -EIO;

	pr_debug("%s: returning %04x\n", __func__, sw_r32(RTL931X_SERDES_INDRT_DATA_CTRL) & 0xffff);
	return sw_r32(RTL931X_SERDES_INDRT_DATA_CTRL) & 0xffff;
}

int rtl931x_write_sds_phy(int phy_addr, int page, int phy_reg, u16 v)
{
	int i;
	u32 cmd;

	cmd = phy_addr << 2 | page << 7 | phy_reg << 13;
	sw_w32(cmd, RTL931X_SERDES_INDRT_ACCESS_CTRL);

	sw_w32(v, RTL931X_SERDES_INDRT_DATA_CTRL);
		
	cmd =  sw_r32(RTL931X_SERDES_INDRT_ACCESS_CTRL) | 0x3;
	sw_w32(cmd, RTL931X_SERDES_INDRT_ACCESS_CTRL);

	for (i = 0; i < 100; i++) {
		if (!(sw_r32(RTL931X_SERDES_INDRT_ACCESS_CTRL) & 0x1))
			break;
		mdelay(1);
	}

	if (i >= 100)
		return -EIO;

	return 0;
}

/*
 * On the RTL838x SoCs, the internal SerDes is accessed through direct access to
 * standard PHY registers, where a 32 bit register holds a 16 bit word as found
 * in a standard page 0 of a PHY
 */
int rtl838x_read_sds_phy(int phy_addr, int phy_reg)
{
	int offset = 0;
	u32 val;

	if (phy_addr == 26)
		offset = 0x100;
	val = sw_r32(RTL838X_SDS4_FIB_REG0 + offset + (phy_reg << 2)) & 0xffff;

	return val;
}

int rtl839x_write_sds_phy(int phy_addr, int phy_reg, u16 v)
{
	int offset = 0;
	int reg;
	u32 val;

	if (phy_addr == 49)
		offset = 0x100;

	reg = (phy_reg << 1) & 0xfc;
	val = v;
	if (phy_reg & 1) {
		val = val << 16;
		sw_w32_mask(0xffff0000, val,
			    RTL839X_SDS12_13_XSG0 + offset + 0x80 + reg);
	} else {
		sw_w32_mask(0xffff, val,
			    RTL839X_SDS12_13_XSG0 + offset + 0x80 + reg);
	}

	return 0;
}

/* Read the link and speed status of the 2 internal SGMII/1000Base-X
 * ports of the RTL838x SoCs
 */
static int rtl8380_read_status(struct phy_device *phydev)
{
	int err;

	err = genphy_read_status(phydev);

	if (phydev->link) {
		phydev->speed = SPEED_1000;
		phydev->duplex = DUPLEX_FULL;
	}

	return err;
}

/* Read the link and speed status of the 2 internal SGMII/1000Base-X
 * ports of the RTL8393 SoC
 */
static int rtl8393_read_status(struct phy_device *phydev)
{
	int offset = 0;
	int err;
	int phy_addr = phydev->mdio.addr;
	u32 v;

	err = genphy_read_status(phydev);
	if (phy_addr == 49)
		offset = 0x100;

	if (phydev->link) {
		phydev->speed = SPEED_100;
		/* Read SPD_RD_00 (bit 13) and SPD_RD_01 (bit 6) out of the internal
		 * PHY registers
		 */
		v = sw_r32(RTL839X_SDS12_13_XSG0 + offset + 0x80);
		if (!(v & (1 << 13)) && (v & (1 << 6)))
			phydev->speed = SPEED_1000;
		phydev->duplex = DUPLEX_FULL;
	}

	return err;
}

static int rtl8226_read_page(struct phy_device *phydev)
{
	return __phy_read(phydev, RTL8XXX_PAGE_SELECT);
}

static int rtl8226_write_page(struct phy_device *phydev, int page)
{
	return __phy_write(phydev, RTL8XXX_PAGE_SELECT, page);
}

static int rtl8226_read_status(struct phy_device *phydev)
{
	int ret = 0, i;
	u32 val;

// TODO: ret = genphy_read_status(phydev);
// 	if (ret < 0) {
// 		pr_info("%s: genphy_read_status failed\n", __func__);
// 		return ret;
// 	}

	// Link status must be read twice
	for (i = 0; i < 2; i++) {
		val = phy_read_mmd(phydev, MDIO_MMD_VEND2, 0xA402);
	}
	phydev->link = val & BIT(2) ? 1 : 0;
	if (!phydev->link)
		goto out;

	// Read duplex status
	val = phy_read_mmd(phydev, MDIO_MMD_VEND2, 0xA434);
	if (val < 0)
		goto out;
	phydev->duplex = !!(val & BIT(3));

	// Read speed
	val = phy_read_mmd(phydev, MDIO_MMD_VEND2, 0xA434);
	switch (val & 0x0630) {
	case 0x0000:
		phydev->speed = SPEED_10;
		break;
	case 0x0010:
		phydev->speed = SPEED_100;
		break;
	case 0x0020:
		phydev->speed = SPEED_1000;
		break;
	case 0x0200:
		phydev->speed = SPEED_10000;
		break;
	case 0x0210:
		phydev->speed = SPEED_2500;
		break;
	case 0x0220:
		phydev->speed = SPEED_5000;
		break;
	default:
		break;
	}
out:
	return ret;
}

static int rtl8226_advertise_aneg(struct phy_device *phydev)
{
	int ret = 0;
	u32 v;

	pr_info("In %s\n", __func__);

	v = phy_read_mmd(phydev, MDIO_MMD_AN, 16);
	if (v < 0)
		goto out;

	v |= BIT(5); // HD 10M
	v |= BIT(6); // FD 10M
	v |= BIT(7); // HD 100M
	v |= BIT(8); // FD 100M

	ret = phy_write_mmd(phydev, MDIO_MMD_AN, 16, v);

	// Allow 1GBit
	v = phy_read_mmd(phydev, MDIO_MMD_VEND2, 0xA412);
	if (v < 0)
		goto out;
	v |= BIT(9); // FD 1000M

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, 0xA412, v);
	if (ret < 0)
		goto out;

	// Allow 2.5G
	v = phy_read_mmd(phydev, MDIO_MMD_AN, 32);
	if (v < 0)
		goto out;

	v |= BIT(7);
	ret = phy_write_mmd(phydev, MDIO_MMD_AN, 32, v);

out:
	return ret;
}

static int rtl8226_config_aneg(struct phy_device *phydev)
{
	int ret = 0;
	u32 v;

	pr_debug("In %s\n", __func__);
	if (phydev->autoneg == AUTONEG_ENABLE) {
		ret = rtl8226_advertise_aneg(phydev);
		if (ret)
			goto out;
		// AutoNegotiationEnable
		v = phy_read_mmd(phydev, MDIO_MMD_AN, 0);
		if (v < 0)
			goto out;

		v |= BIT(12); // Enable AN
		ret = phy_write_mmd(phydev, MDIO_MMD_AN, 0, v);
		if (ret < 0)
			goto out;

		// RestartAutoNegotiation
		v = phy_read_mmd(phydev, MDIO_MMD_VEND2, 0xA400);
		if (v < 0)
			goto out;
		v |= BIT(9);

		ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, 0xA400, v);
	}

//	TODO: ret = __genphy_config_aneg(phydev, ret);

out:
	return ret;
}

static int rtl8226_get_eee(struct phy_device *phydev,
				     struct ethtool_eee *e)
{
	u32 val;
	int addr = phydev->mdio.addr;

	pr_debug("In %s, port %d, was enabled: %d\n", __func__, addr, e->eee_enabled);

	val = phy_read_mmd(phydev, MDIO_MMD_AN, 60);
	if (e->eee_enabled) {
		e->eee_enabled = !!(val & BIT(1));
		if (!e->eee_enabled) {
			val = phy_read_mmd(phydev, MDIO_MMD_AN, 62);
			e->eee_enabled = !!(val & BIT(0));
		}
	}
	pr_debug("%s: enabled: %d\n", __func__, e->eee_enabled);

	return 0;
}

static int rtl8226_set_eee(struct phy_device *phydev, struct ethtool_eee *e)
{
	int port = phydev->mdio.addr;
	u64 poll_state;
	bool an_enabled;
	u32 val;

	pr_info("In %s, port %d, enabled %d\n", __func__, port, e->eee_enabled);

	poll_state = disable_polling(port);

	// Remember aneg state
	val = phy_read_mmd(phydev, MDIO_MMD_AN, 0);
	an_enabled = !!(val & BIT(12));

	// Setup 100/1000MBit
	val = phy_read_mmd(phydev, MDIO_MMD_AN, 60);
	if (e->eee_enabled)
		val |= 0x6;
	else
		val &= 0x6;
	phy_write_mmd(phydev, MDIO_MMD_AN, 60, val);

	// Setup 2.5GBit
	val = phy_read_mmd(phydev, MDIO_MMD_AN, 62);
	if (e->eee_enabled)
		val |= 0x1;
	else
		val &= 0x1;
	phy_write_mmd(phydev, MDIO_MMD_AN, 62, val);

	// RestartAutoNegotiation
	val = phy_read_mmd(phydev, MDIO_MMD_VEND2, 0xA400);
	val |= BIT(9);
	phy_write_mmd(phydev, MDIO_MMD_VEND2, 0xA400, val);

	resume_polling(poll_state);

	return 0;
}

static struct fw_header *rtl838x_request_fw(struct phy_device *phydev,
					    const struct firmware *fw,
					    const char *name)
{
	struct device *dev = &phydev->mdio.dev;
	int err;
	struct fw_header *h;
	uint32_t checksum, my_checksum;

	err = request_firmware(&fw, name, dev);
	if (err < 0)
		goto out;

	if (fw->size < sizeof(struct fw_header)) {
		pr_err("Firmware size too small.\n");
		err = -EINVAL;
		goto out;
	}

	h = (struct fw_header *) fw->data;
	pr_info("Firmware loaded. Size %d, magic: %08x\n", fw->size, h->magic);

	if (h->magic != 0x83808380) {
		pr_err("Wrong firmware file: MAGIC mismatch.\n");
		goto out;
	}

	checksum = h->checksum;
	h->checksum = 0;
	my_checksum = ~crc32(0xFFFFFFFFU, fw->data, fw->size);
	if (checksum != my_checksum) {
		pr_err("Firmware checksum mismatch.\n");
		err = -EINVAL;
		goto out;
	}
	h->checksum = checksum;

	return h;
out:
	dev_err(dev, "Unable to load firmware %s (%d)\n", name, err);
	return NULL;
}

static void rtl821x_phy_setup_package_broadcast(struct phy_device *phydev, bool enable)
{
	int mac = phydev->mdio.addr;

	/* select main page 0 */
	phy_write_paged(phydev, RTL83XX_PAGE_RAW, RTL8XXX_PAGE_SELECT, RTL8XXX_PAGE_MAIN);
	/* write to 0x8 to register 0x1d on main page 0 */
	phy_write_paged(phydev, RTL83XX_PAGE_RAW, RTL821XINT_MEDIA_PAGE_SELECT, RTL821X_MEDIA_PAGE_INTERNAL);
	/* select page 0x266 */
	phy_write_paged(phydev, RTL83XX_PAGE_RAW, RTL8XXX_PAGE_SELECT, RTL821X_PAGE_PORT);
	/* set phy id and target broadcast bitmap in register 0x16 on page 0x266 */
	phy_write_paged(phydev, RTL83XX_PAGE_RAW, 0x16, (enable?0xff00:0x00) | mac);
	/* return to main page 0 */
	phy_write_paged(phydev, RTL83XX_PAGE_RAW, RTL8XXX_PAGE_SELECT, RTL8XXX_PAGE_MAIN);
	/* write to 0x0 to register 0x1d on main page 0 */
	phy_write_paged(phydev, RTL83XX_PAGE_RAW, RTL821XINT_MEDIA_PAGE_SELECT, RTL821X_MEDIA_PAGE_AUTO);
	mdelay(1);
}

static int rtl8214qf_sds_mode_set(struct phy_device *phydev, int mode)
{
	int port = phydev->mdio.addr;
	int sds = 4 + (port % 4);
	int base_port = port - (port % 4);
	int reg = rtl8295_sds_ctrl_regs[sds]; // RTL8295_SDS_CTRL_SDS_CTRL_Sx_REG of SDS
	int m, p = 0;
	u32 v, w;

	switch (mode) {
	case PHY_INTERFACE_MODE_SGMII:
		m = RTL8295_SDS_MODE_SGMII;
		break;
	case PHY_INTERFACE_MODE_1000BASEX:
		m = RTL8295_SDS_MODE_FIB1G;
		break;
//	case PHY_INTERFACE_MODE_100:
//		m = PHY_8295_SDS_MODE_FIB100M;
//		break;
	default:
		return -ENOTSUPP;
	}

	pr_debug("%s port %d, sds %d, base port %d, reg %d\n",
		__func__, port, sds, base_port, reg);

	v = phy_package_port_read_paged(phydev, p, RTL8295_PAGE_SDS_CTRL_S0, reg);
	pr_debug("%s port %d, ctrl reg is %x, current mode is %x\n",
		 __func__, port, v, v & 0x1f);

	v |= RTL8295_SDS_MODE_OFF;

	phy_package_port_write_paged(phydev, p, RTL8295_PAGE_SDS_CTRL_S0, reg, v);
	v = (v & (~RTL8295_SDS_MODE_OFF)) | m;

	msleep(1);

	phy_package_port_write_paged(phydev, p, RTL8295_PAGE_SDS_CTRL_S0, reg, v);

	// Enable SerDes
	v = phy_package_port_read_paged(phydev, p,
			     rtl8295_sds_page_offset[sds] + RTL8295_SDS0_ANA_MISC_REG02_PAGE,
			     RTL8295_SDS0_ANA_MISC_REG02_REG);

	v &= ~BIT(12);  // Clear FRC_BER_NOTIFY_ON
	phy_package_port_write_paged(phydev, p,
			rtl8295_sds_page_offset[sds] + RTL8295_SDS0_ANA_MISC_REG02_PAGE,
			RTL8295_SDS0_ANA_MISC_REG02_REG, v);

	// Setup 1.25G mode
	v = phy_package_port_read_paged(phydev, p,
			   rtl8295_sds_page_offset[sds] + RTL8295_SDS0_SDS_REG14_PAGE,
			   RTL8295_SDS0_SDS_REG14_REG);
	v &= ~BIT(12);  // Clear SP_SEL_CALIBOK
	phy_package_port_write_paged(phydev, p,
			rtl8295_sds_page_offset[sds] + RTL8295_SDS0_SDS_REG14_PAGE,
			RTL8295_SDS0_SDS_REG14_REG, v);

	v = phy_package_port_read_paged(phydev, p,
			   rtl8295_sds_page_offset[sds] +RTL8295_SDS0_ANA_MISC_REG02_PAGE,
			   RTL8295_SDS0_ANA_MISC_REG02_REG);
	v &= ~BIT(12);  // Clear FRC_BER_NOTIFY_ON
	phy_package_port_write_paged(phydev, p,
			rtl8295_sds_page_offset[sds] + RTL8295_SDS0_ANA_MISC_REG02_PAGE,
			RTL8295_SDS0_ANA_MISC_REG02_REG, v);

	// Reset the port-side SerDes
	v = phy_package_port_read_paged(phydev, p,
			     rtl8295_sds_page_offset[sds] + RTL8295_SDS0_ANA_MISC_REG02_PAGE,
			     RTL8295_SDS0_ANA_MISC_REG02_REG);
	v |= BIT(10);  // Set FRC_CKRDY_ON
	phy_package_port_write_paged(phydev, p,
			rtl8295_sds_page_offset[sds] + RTL8295_SDS0_ANA_MISC_REG02_PAGE,
			RTL8295_SDS0_ANA_MISC_REG02_REG, v);
	msleep(1);

	v &= ~BIT(10);
	phy_package_port_write_paged(phydev, p,
			rtl8295_sds_page_offset[sds] + RTL8295_SDS0_ANA_MISC_REG02_PAGE,
			RTL8295_SDS0_ANA_MISC_REG02_REG, v);

	// Reset RX (fiber): RX_DISABLE: 1 >> RX_ENSELF: 1 >> RX_ENSELF: 0 >> RX_DISABLE: 0
	v = phy_package_port_read_paged(phydev, p, RTL8295_PAGE_SDS_CTRL_S0, reg);
	w = phy_package_port_read_paged(phydev, p,
			   rtl8295_sds_page_offset[sds] + RTL8295_SDS0_ANA_SPD_1P25G_REG08_PAGE,
			   RTL8295_SDS0_ANA_SPD_1P25G_REG08_REG);

	pr_debug("%s port %d, RTL8295_SDS0_ANA_SPD_1P25G_REG08_REG is %x\n", __func__, port, w);

	// Set RX_DISABLE
	phy_package_port_write_paged(phydev, p, RTL8295_PAGE_SDS_CTRL_S0, reg, v | BIT(14));
	// Set RX_ENSELF
	phy_package_port_write_paged(phydev, p,
			rtl8295_sds_page_offset[sds] + RTL8295_SDS0_ANA_SPD_1P25G_REG08_PAGE,
			RTL8295_SDS0_ANA_SPD_1P25G_REG08_REG, w | BIT(9));
	// Clear RX_ENSELF
	phy_package_port_write_paged(phydev, p,
			rtl8295_sds_page_offset[sds] + RTL8295_SDS0_ANA_SPD_1P25G_REG08_PAGE,
			RTL8295_SDS0_ANA_SPD_1P25G_REG08_REG, w & (~BIT(9)));
	// Clear RX_DISABLE
	phy_package_port_write_paged(phydev, p, RTL8295_PAGE_SDS_CTRL_S0, reg, v);

	// Clear Counter
	phy_package_port_write_paged(phydev, p,
			rtl8295_sds_page_offset[sds] + RTL8295_SDS0_SDS_EXT_REG00_PAGE,
			RTL8295_SDS0_SDS_EXT_REG00_REG, 0);

	// Restart PHY
	phy_modify(phydev, 0, BIT(15), BIT(15));
	msleep(1);
	phy_modify(phydev, 0, BIT(15), 0);

	return 0;
}

/*
 * The RTL8214QF is a quad 1000BaseX/100FX PHY
 * It is connected via 5GBit/s QSGMII link to the MAC and provides
 * up to 4 SGMII links to Ethernet SFP modules and
 * up to 4 1GBit 100FX/1000Base-X links
 * It provides 6 SerDes, SerDes 0 being the one facing the MAC,
 * and SerDes 4 to 7 being the ones facing the PHY
 */
static int rtl8214qf_configure(struct phy_device *phydev)
{
	int port = phydev->mdio.addr;
	u32 val;

	// We only need to configure the package for the base port
	if (port % 4)
		return rtl8214qf_sds_mode_set(phydev, PHY_INTERFACE_MODE_1000BASEX);

	// Get S0 interface mode (MAC <-> PHY)
	val = phy_read_paged(phydev, RTL8295_PAGE_SDS_CTRL_S0, RTL8295_SDS_CTRL_CTRL_REG_S0);
	pr_info("%s port %d read control register %x\n", __func__, port, val);
	val &= 0x1f;
	pr_info("%s port %d serdes mode: %x\n", __func__, port, val);

	if (val != RTL8295_SDS_MODE_QSGMII) // We only support QSGMII
		return -1;

	// Reset the 5G serdes
	val = phy_read_paged(phydev,
			     rtl8295_sds_page_offset[0] + RTL8295_SDS0_ANA_SPD_5G_REG21_PAGE,
			     RTL8295_SDS0_ANA_SPD_5G_REG21_REG);
	pr_info("%s port %d RTL8295_SDS0_ANA_SPD_5G_REG21_REG: %x\n", __func__, port, val);
	val |= BIT(4);  // RxEnSelf
	phy_write_paged(phydev,
			rtl8295_sds_page_offset[0] + RTL8295_SDS0_ANA_SPD_5G_REG21_PAGE,
			RTL8295_SDS0_ANA_SPD_5G_REG21_REG, val);
	msleep(1);
	val &= ~BIT(4);
	val = phy_write_paged(phydev,
			rtl8295_sds_page_offset[0] + RTL8295_SDS0_ANA_SPD_5G_REG21_PAGE,
			RTL8295_SDS0_ANA_SPD_5G_REG21_REG, val);

	rtl8214qf_sds_mode_set(phydev, PHY_INTERFACE_MODE_1000BASEX);

	return 0;
}

static int rtl8214qf_read_status(struct phy_device *phydev)
{
	int ret;
	u32 val;

	ret = genphy_read_status(phydev);
	if (ret < 0) {
		pr_info("%s: genphy_read_status failed\n", __func__);
		return ret;
	}

	// Read speed
	val = phy_read(phydev, 0);

	// the RTL8214QF uses reserved bit 13 for designating the speed
	val = ((val & BIT(6)) >> 5) | ((val & BIT(13)) >> 13);
	switch (val) {
	case 0:
		phydev->speed = SPEED_10;
		break;
	case 1:
		phydev->speed = SPEED_100;
		break;
	case 2:
		phydev->speed = SPEED_1000;
		break;
	default:
		break;
	}

	return ret;
}

static int rtl8380_configure_int_rtl8218b(struct phy_device *phydev)
{
	u32 val, phy_id;
	int i, p, ipd_flag;
	int mac = phydev->mdio.addr;
	struct fw_header *h;
	u32 *rtl838x_6275B_intPhy_perport;
	u32 *rtl8218b_6276B_hwEsd_perport;

	val = phy_read(phydev, 2);
	phy_id = val << 16;
	val = phy_read(phydev, 3);
	phy_id |= val;
	pr_debug("Phy on MAC %d: %x\n", mac, phy_id);

	/* Read internal PHY ID */
	phy_write_paged(phydev, 31, 27, 0x0002);
	val = phy_read_paged(phydev, 31, 28);
	if (val != 0x6275) {
		phydev_err(phydev, "Expected internal RTL8218B, found PHY-ID %x\n", val);
		return -1;
	}

	/* Internal RTL8218B, version 2 */
	phydev_info(phydev, "Detected internal RTL8218B\n");

	h = rtl838x_request_fw(phydev, &rtl838x_8380_fw, FIRMWARE_838X_8380_1);
	if (!h)
		return -1;

	if (h->phy != 0x83800000) {
		phydev_err(phydev, "Wrong firmware file: PHY mismatch.\n");
		return -1;
	}

	rtl838x_6275B_intPhy_perport = (void *)h + sizeof(struct fw_header)
			+ h->parts[8].start;

	rtl8218b_6276B_hwEsd_perport = (void *)h + sizeof(struct fw_header)
			+ h->parts[9].start;

	if (sw_r32(RTL838X_DMY_REG31) == 0x1)
		ipd_flag = 1;

	val = phy_read(phydev, 0);
	if (val & BIT(11))
		rtl8380_int_phy_on_off(phydev, true);
	else
		rtl8380_phy_reset(phydev);
	msleep(100);

	/* Ready PHY for patch */
	for (p = 0; p < 8; p++) {
		phy_package_port_write_paged(phydev, p, RTL83XX_PAGE_RAW, RTL8XXX_PAGE_SELECT, RTL821X_PAGE_PATCH);
		phy_package_port_write_paged(phydev, p, RTL83XX_PAGE_RAW, 0x10, 0x0010);
	}
	msleep(500);
	for (p = 0; p < 8; p++) {
		for (i = 0; i < 100 ; i++) {
			val = phy_package_port_read_paged(phydev, p, RTL821X_PAGE_STATE, 0x10);
			if (val & 0x40)
				break;
		}
		if (i >= 100) {
			phydev_err(phydev,
				   "ERROR: Port %d not ready for patch.\n",
				   mac + p);
			return -1;
		}
	}
	for (p = 0; p < 8; p++) {
		i = 0;
		while (rtl838x_6275B_intPhy_perport[i * 2]) {
			phy_package_port_write_paged(phydev, p, RTL83XX_PAGE_RAW,
				rtl838x_6275B_intPhy_perport[i * 2],
				rtl838x_6275B_intPhy_perport[i * 2 + 1]);
			i++;
		}
		i = 0;
		while (rtl8218b_6276B_hwEsd_perport[i * 2]) {
			phy_package_port_write_paged(phydev, p, RTL83XX_PAGE_RAW,
				rtl8218b_6276B_hwEsd_perport[i * 2],
				rtl8218b_6276B_hwEsd_perport[i * 2 + 1]);
			i++;
		}
	}
	return 0;
}

static int rtl8380_configure_ext_rtl8218b(struct phy_device *phydev)
{
	u32 val, ipd, phy_id;
	int i, l;
	int mac = phydev->mdio.addr;
	struct fw_header *h;
	u32 *rtl8380_rtl8218b_perchip;
	u32 *rtl8218B_6276B_rtl8380_perport;
	u32 *rtl8380_rtl8218b_perport;

	if (soc_info.family == RTL8380_FAMILY_ID && mac != 0 && mac != 16) {
		phydev_err(phydev, "External RTL8218B must have PHY-IDs 0 or 16!\n");
		return -1;
	}
	val = phy_read(phydev, 2);
	phy_id = val << 16;
	val = phy_read(phydev, 3);
	phy_id |= val;
	pr_info("Phy on MAC %d: %x\n", mac, phy_id);

	/* Read internal PHY ID */
	phy_write_paged(phydev, 31, 27, 0x0002);
	val = phy_read_paged(phydev, 31, 28);
	if (val != 0x6276) {
		phydev_err(phydev, "Expected external RTL8218B, found PHY-ID %x\n", val);
		return -1;
	}
	phydev_info(phydev, "Detected external RTL8218B\n");

	h = rtl838x_request_fw(phydev, &rtl838x_8218b_fw, FIRMWARE_838X_8218b_1);
	if (!h)
		return -1;

	if (h->phy != 0x8218b000) {
		phydev_err(phydev, "Wrong firmware file: PHY mismatch.\n");
		return -1;
	}

	rtl8380_rtl8218b_perchip = (void *)h + sizeof(struct fw_header)
			+ h->parts[0].start;

	rtl8218B_6276B_rtl8380_perport = (void *)h + sizeof(struct fw_header)
			+ h->parts[1].start;

	rtl8380_rtl8218b_perport = (void *)h + sizeof(struct fw_header)
			+ h->parts[2].start;

	val = phy_read(phydev, 0);
	if (val & (1 << 11))
		rtl8380_int_phy_on_off(phydev, true);
	else
		rtl8380_phy_reset(phydev);

	msleep(100);

	/* Get Chip revision */
	phy_write_paged(phydev, RTL83XX_PAGE_RAW, RTL8XXX_PAGE_SELECT, RTL8XXX_PAGE_MAIN);
	phy_write_paged(phydev, RTL83XX_PAGE_RAW, 0x1b, 0x4);
	val = phy_read_paged(phydev, RTL83XX_PAGE_RAW, 0x1c);

	phydev_info(phydev, "Detected chip revision %04x\n", val);

	i = 0;
	while (rtl8380_rtl8218b_perchip[i * 3]
		&& rtl8380_rtl8218b_perchip[i * 3 + 1]) {
			phy_package_port_write_paged(phydev, rtl8380_rtl8218b_perchip[i * 3],
					  RTL83XX_PAGE_RAW, rtl8380_rtl8218b_perchip[i * 3 + 1],
					  rtl8380_rtl8218b_perchip[i * 3 + 2]);
		i++;
	}

	/* Enable PHY */
	for (i = 0; i < 8; i++) {
		phy_package_port_write_paged(phydev, i, RTL83XX_PAGE_RAW, RTL8XXX_PAGE_SELECT, RTL8XXX_PAGE_MAIN);
		phy_package_port_write_paged(phydev, i, RTL83XX_PAGE_RAW, 0x00, 0x1140);
	}
	mdelay(100);

	/* Request patch */
	for (i = 0; i < 8; i++) {
		phy_package_port_write_paged(phydev, i, RTL83XX_PAGE_RAW, RTL8XXX_PAGE_SELECT, RTL821X_PAGE_PATCH);
		phy_package_port_write_paged(phydev, i, RTL83XX_PAGE_RAW, 0x10, 0x0010);
	}

	mdelay(300);

	/* Verify patch readiness */
	for (i = 0; i < 8; i++) {
		for (l = 0; l < 100; l++) {
			val = phy_package_port_read_paged(phydev, i, RTL821X_PAGE_STATE, 0x10);
			if (val & 0x40)
				break;
		}
		if (l >= 100) {
			phydev_err(phydev, "Could not patch PHY\n");
			return -1;
		}
	}

	/* Use Broadcast ID method for patching */
	rtl821x_phy_setup_package_broadcast(phydev, true);

	phy_write_paged(phydev, RTL83XX_PAGE_RAW, 30, 8);
	phy_write_paged(phydev, 0x26e, 17, 0xb);
	phy_write_paged(phydev, 0x26e, 16, 0x2);
	mdelay(1);
	ipd = phy_read_paged(phydev, 0x26e, 19);
	phy_write_paged(phydev, 0, 30, 0);
	ipd = (ipd >> 4) & 0xf; /* unused ? */

	i = 0;
	while (rtl8218B_6276B_rtl8380_perport[i * 2]) {
		phy_write_paged(phydev, RTL83XX_PAGE_RAW, rtl8218B_6276B_rtl8380_perport[i * 2],
				  rtl8218B_6276B_rtl8380_perport[i * 2 + 1]);
		i++;
	}

	/*Disable broadcast ID*/
	rtl821x_phy_setup_package_broadcast(phydev, false);

	return 0;
}

static int rtl8218b_ext_match_phy_device(struct phy_device *phydev)
{
	int addr = phydev->mdio.addr;

	/* Both the RTL8214FC and the external RTL8218B have the same
	 * PHY ID. On the RTL838x, the RTL8218B can only be attached_dev
	 * at PHY IDs 0-7, while the RTL8214FC must be attached via
	 * the pair of SGMII/1000Base-X with higher PHY-IDs
	 */
	if (soc_info.family == RTL8380_FAMILY_ID)
		return phydev->phy_id == PHY_ID_RTL8218B_E && addr < 8;
	else
		return phydev->phy_id == PHY_ID_RTL8218B_E;
}

static void rtl8380_rtl8214fc_media_set(struct phy_device *phydev, bool set_fibre)
{
	int mac = phydev->mdio.addr;

	static int reg[] = {16, 19, 20, 21};
	int val, media, power;

	pr_info("%s: port %d, set_fibre: %d\n", __func__, mac, set_fibre);
	phy_package_write_paged(phydev, RTL83XX_PAGE_RAW, RTL821XINT_MEDIA_PAGE_SELECT, RTL821X_MEDIA_PAGE_INTERNAL);
	val = phy_package_read_paged(phydev, RTL821X_PAGE_PORT, reg[mac % 4]);

	media = (val >> 10) & 0x3;
	pr_info("Current media %x\n", media);
	if (media & 0x2) {
		pr_info("Powering off COPPER\n");
		phy_package_write_paged(phydev, RTL83XX_PAGE_RAW, RTL821XINT_MEDIA_PAGE_SELECT, RTL821X_MEDIA_PAGE_COPPER);
		/* Ensure power is off */
		power = phy_package_read_paged(phydev, RTL821X_PAGE_POWER, 0x10);
		if (!(power & (1 << 11)))
			phy_package_write_paged(phydev, RTL821X_PAGE_POWER, 0x10, power | (1 << 11));
	} else {
		pr_info("Powering off FIBRE");
		phy_package_write_paged(phydev, RTL83XX_PAGE_RAW, RTL821XINT_MEDIA_PAGE_SELECT, RTL821X_MEDIA_PAGE_FIBRE);
		/* Ensure power is off */
		power = phy_package_read_paged(phydev, RTL821X_PAGE_POWER, 0x10);
		if (!(power & (1 << 11)))
			phy_package_write_paged(phydev, RTL821X_PAGE_POWER, 0x10, power | (1 << 11));
	}

	if (set_fibre) {
		val |= 1 << 10;
		val &= ~(1 << 11);
	} else {
		val |= 1 << 10;
		val |= 1 << 11;
	}
	phy_package_write_paged(phydev, RTL83XX_PAGE_RAW, RTL821XINT_MEDIA_PAGE_SELECT, RTL821X_MEDIA_PAGE_INTERNAL);
	phy_package_write_paged(phydev, RTL821X_PAGE_PORT, reg[mac % 4], val);
	phy_package_write_paged(phydev, RTL83XX_PAGE_RAW, RTL821XINT_MEDIA_PAGE_SELECT, RTL821X_MEDIA_PAGE_AUTO);

	if (set_fibre) {
		pr_info("Powering on FIBRE");
		phy_package_write_paged(phydev, RTL83XX_PAGE_RAW, RTL821XINT_MEDIA_PAGE_SELECT, RTL821X_MEDIA_PAGE_FIBRE);
		/* Ensure power is off */
		power = phy_package_read_paged(phydev, RTL821X_PAGE_POWER, 0x10);
		if (power & (1 << 11))
			phy_package_write_paged(phydev, RTL821X_PAGE_POWER, 0x10, power & ~(1 << 11));
	} else {
		pr_info("Powering on COPPER\n");
		phy_package_write_paged(phydev, RTL83XX_PAGE_RAW, RTL821XINT_MEDIA_PAGE_SELECT, RTL821X_MEDIA_PAGE_COPPER);
		/* Ensure power is off */
		power = phy_package_read_paged(phydev, RTL821X_PAGE_POWER, 0x10);
		if (power & (1 << 11))
			phy_package_write_paged(phydev, RTL821X_PAGE_POWER, 0x10, power & ~(1 << 11));
	}

	phy_package_write_paged(phydev, RTL83XX_PAGE_RAW, RTL821XINT_MEDIA_PAGE_SELECT, RTL821X_MEDIA_PAGE_AUTO);
}

static bool rtl8380_rtl8214fc_media_is_fibre(struct phy_device *phydev)
{
	int mac = phydev->mdio.addr;

	static int reg[] = {16, 19, 20, 21};
	u32 val;

	phy_package_write_paged(phydev, RTL83XX_PAGE_RAW, RTL821XINT_MEDIA_PAGE_SELECT, RTL821X_MEDIA_PAGE_INTERNAL);
	val = phy_package_read_paged(phydev, RTL821X_PAGE_PORT, reg[mac % 4]);
	phy_package_write_paged(phydev, RTL83XX_PAGE_RAW, RTL821XINT_MEDIA_PAGE_SELECT, RTL821X_MEDIA_PAGE_AUTO);
	if (val & (1 << 11))
		return false;
	return true;
}

static int rtl8214fc_set_port(struct phy_device *phydev, int port)
{
	bool is_fibre = (port == PORT_FIBRE ? true : false);
	int addr = phydev->mdio.addr;

	pr_debug("%s port %d to %d\n", __func__, addr, port);

	rtl8380_rtl8214fc_media_set(phydev, is_fibre);
	return 0;
}

static int rtl8214fc_get_port(struct phy_device *phydev)
{
	int addr = phydev->mdio.addr;

	pr_debug("%s: port %d\n", __func__, addr);
	if (rtl8380_rtl8214fc_media_is_fibre(phydev))
		return PORT_FIBRE;
	return PORT_MII;
}

/*
 * Enable EEE on the RTL8218B PHYs
 * The method used is not the preferred way (which would be based on the MAC-EEE state,
 * but the only way that works since the kernel first enables EEE in the MAC
 * and then sets up the PHY. The MAC-based approach would require the oppsite.
 */
void rtl8218d_eee_set(struct phy_device *phydev, bool enable)
{
	u32 val;
	bool an_enabled;

	pr_debug("In %s %d, enable %d\n", __func__, phydev->mdio.addr, enable);
	/* Set GPHY page to copper */
	phy_write_paged(phydev, RTL821X_PAGE_GPHY, RTL821XEXT_MEDIA_PAGE_SELECT, RTL821X_MEDIA_PAGE_COPPER);

	val = phy_read(phydev, 0);
	an_enabled = val & BIT(12);

	/* Enable 100M (bit 1) / 1000M (bit 2) EEE */
	val = phy_read_mmd(phydev, 7, 60);
	val |= BIT(2) | BIT(1);
	phy_write_mmd(phydev, 7, 60, enable ? 0x6 : 0);

	/* 500M EEE ability */
	val = phy_read_paged(phydev, RTL821X_PAGE_GPHY, 20);
	if (enable)
		val |= BIT(7);
	else
		val &= ~BIT(7);
	phy_write_paged(phydev, RTL821X_PAGE_GPHY, 20, val);

	/* Restart AN if enabled */
	if (an_enabled) {
		val = phy_read(phydev, 0);
		val |= BIT(9);
		phy_write(phydev, 0, val);
	}

	/* GPHY page back to auto*/
	phy_write_paged(phydev, RTL821X_PAGE_GPHY, RTL821XEXT_MEDIA_PAGE_SELECT, RTL821X_MEDIA_PAGE_AUTO);
}

static int rtl8218b_get_eee(struct phy_device *phydev,
				     struct ethtool_eee *e)
{
	u32 val;
	int addr = phydev->mdio.addr;

	pr_debug("In %s, port %d, was enabled: %d\n", __func__, addr, e->eee_enabled);

	/* Set GPHY page to copper */
	phy_write_paged(phydev, RTL821X_PAGE_GPHY, RTL821XINT_MEDIA_PAGE_SELECT, RTL821X_MEDIA_PAGE_COPPER);

	val = phy_read_paged(phydev, 7, 60);
	if (e->eee_enabled) {
		// Verify vs MAC-based EEE
		e->eee_enabled = !!(val & BIT(7));
		if (!e->eee_enabled) {
			val = phy_read_paged(phydev, RTL821X_PAGE_MAC, 25);
			e->eee_enabled = !!(val & BIT(4));
		}
	}
	pr_debug("%s: enabled: %d\n", __func__, e->eee_enabled);

	/* GPHY page to auto */
	phy_write_paged(phydev, RTL821X_PAGE_GPHY, RTL821XINT_MEDIA_PAGE_SELECT, RTL821X_MEDIA_PAGE_AUTO);

	return 0;
}

static int rtl8218d_get_eee(struct phy_device *phydev,
				     struct ethtool_eee *e)
{
	u32 val;
	int addr = phydev->mdio.addr;

	pr_debug("In %s, port %d, was enabled: %d\n", __func__, addr, e->eee_enabled);

	/* Set GPHY page to copper */
	phy_write_paged(phydev, RTL821X_PAGE_GPHY, RTL821XEXT_MEDIA_PAGE_SELECT, RTL821X_MEDIA_PAGE_COPPER);

	val = phy_read_paged(phydev, 7, 60);
	if (e->eee_enabled)
		e->eee_enabled = !!(val & BIT(7));
	pr_debug("%s: enabled: %d\n", __func__, e->eee_enabled);

	/* GPHY page to auto */
	phy_write_paged(phydev, RTL821X_PAGE_GPHY, RTL821XEXT_MEDIA_PAGE_SELECT, RTL821X_MEDIA_PAGE_AUTO);

	return 0;
}

static int rtl8214fc_set_eee(struct phy_device *phydev,
				     struct ethtool_eee *e)
{
	u32 poll_state;
	int port = phydev->mdio.addr;
	bool an_enabled;
	u32 val;

	pr_debug("In %s port %d, enabled %d\n", __func__, port, e->eee_enabled);

	if (rtl8380_rtl8214fc_media_is_fibre(phydev)) {
		netdev_err(phydev->attached_dev, "Port %d configured for FIBRE", port);
		return -ENOTSUPP;
	}

	poll_state = disable_polling(port);

	/* Set GPHY page to copper */
	phy_write_paged(phydev, RTL821X_PAGE_GPHY, RTL821XINT_MEDIA_PAGE_SELECT, RTL821X_MEDIA_PAGE_COPPER);

	// Get auto-negotiation status
	val = phy_read(phydev, 0);
	an_enabled = val & BIT(12);

	pr_info("%s: aneg: %d\n", __func__, an_enabled);
	val = phy_read_paged(phydev, RTL821X_PAGE_MAC, 25);
	val &= ~BIT(5);  // Use MAC-based EEE
	phy_write_paged(phydev, RTL821X_PAGE_MAC, 25, val);

	/* Enable 100M (bit 1) / 1000M (bit 2) EEE */
	phy_write_paged(phydev, 7, 60, e->eee_enabled ? 0x6 : 0);

	/* 500M EEE ability */
	val = phy_read_paged(phydev, RTL821X_PAGE_GPHY, 20);
	if (e->eee_enabled)
		val |= BIT(7);
	else
		val &= ~BIT(7);

	phy_write_paged(phydev, RTL821X_PAGE_GPHY, 20, val);

	/* Restart AN if enabled */
	if (an_enabled) {
		pr_info("%s: doing aneg\n", __func__);
		val = phy_read(phydev, 0);
		val |= BIT(9);
		phy_write(phydev, 0, val);
	}

	/* GPHY page back to auto*/
	phy_write_paged(phydev, RTL821X_PAGE_GPHY, RTL821XINT_MEDIA_PAGE_SELECT, RTL821X_MEDIA_PAGE_AUTO);

	resume_polling(poll_state);

	return 0;
}

static int rtl8214fc_get_eee(struct phy_device *phydev,
				      struct ethtool_eee *e)
{
	int addr = phydev->mdio.addr;

	pr_debug("In %s port %d, enabled %d\n", __func__, addr, e->eee_enabled);
	if (rtl8380_rtl8214fc_media_is_fibre(phydev)) {
		netdev_err(phydev->attached_dev, "Port %d configured for FIBRE", addr);
		return -ENOTSUPP;
	}

	return rtl8218b_get_eee(phydev, e);
}

static int rtl8218b_set_eee(struct phy_device *phydev, struct ethtool_eee *e)
{
	int port = phydev->mdio.addr;
	u64 poll_state;
	u32 val;
	bool an_enabled;

	pr_info("In %s, port %d, enabled %d\n", __func__, port, e->eee_enabled);

	poll_state = disable_polling(port);

	/* Set GPHY page to copper */
	phy_write(phydev, RTL821XEXT_MEDIA_PAGE_SELECT, RTL821X_MEDIA_PAGE_COPPER);
	val = phy_read(phydev, 0);
	an_enabled = val & BIT(12);

	if (e->eee_enabled) {
		/* 100/1000M EEE Capability */
		phy_write(phydev, 13, 0x0007);
		phy_write(phydev, 14, 0x003C);
		phy_write(phydev, 13, 0x4007);
		phy_write(phydev, 14, 0x0006);

		val = phy_read_paged(phydev, RTL821X_PAGE_MAC, 25);
		val |= BIT(4);
		phy_write_paged(phydev, RTL821X_PAGE_MAC, 25, val);
	} else {
		/* 100/1000M EEE Capability */
		phy_write(phydev, 13, 0x0007);
		phy_write(phydev, 14, 0x003C);
		phy_write(phydev, 13, 0x0007);
		phy_write(phydev, 14, 0x0000);

		val = phy_read_paged(phydev, RTL821X_PAGE_MAC, 25);
		val &= ~BIT(4);
		phy_write_paged(phydev, RTL821X_PAGE_MAC, 25, val);
	}

	/* Restart AN if enabled */
	if (an_enabled) {
		val = phy_read(phydev, 0);
		val |= BIT(9);
		phy_write(phydev, 0, val);
	}

	/* GPHY page back to auto*/
	phy_write_paged(phydev, RTL821X_PAGE_GPHY, RTL821XEXT_MEDIA_PAGE_SELECT, RTL821X_MEDIA_PAGE_AUTO);

	pr_info("%s done\n", __func__);
	resume_polling(poll_state);

	return 0;
}

static int rtl8218d_set_eee(struct phy_device *phydev, struct ethtool_eee *e)
{
	int addr = phydev->mdio.addr;
	u64 poll_state;

	pr_info("In %s, port %d, enabled %d\n", __func__, addr, e->eee_enabled);

	poll_state = disable_polling(addr);

	rtl8218d_eee_set(phydev, (bool) e->eee_enabled);

	resume_polling(poll_state);

	return 0;
}

static int rtl8214c_match_phy_device(struct phy_device *phydev)
{
	return phydev->phy_id == PHY_ID_RTL8214C;
}

static int rtl8380_configure_rtl8214c(struct phy_device *phydev)
{
	u32 phy_id, val;
	int mac = phydev->mdio.addr;

	val = phy_read(phydev, 2);
	phy_id = val << 16;
	val = phy_read(phydev, 3);
	phy_id |= val;
	pr_debug("Phy on MAC %d: %x\n", mac, phy_id);

	phydev_info(phydev, "Detected external RTL8214C\n");

	/* GPHY auto conf */
	phy_write_paged(phydev, RTL821X_PAGE_GPHY, RTL821XINT_MEDIA_PAGE_SELECT, RTL821X_MEDIA_PAGE_AUTO);
	return 0;
}

static int rtl8380_configure_rtl8214fc(struct phy_device *phydev)
{
	u32 phy_id, val, page = 0;
	int i, l;
	int mac = phydev->mdio.addr;
	struct fw_header *h;
	u32 *rtl8380_rtl8214fc_perchip;
	u32 *rtl8380_rtl8214fc_perport;

	val = phy_read(phydev, 2);
	phy_id = val << 16;
	val = phy_read(phydev, 3);
	phy_id |= val;
	pr_debug("Phy on MAC %d: %x\n", mac, phy_id);

	/* Read internal PHY id */
	phy_write_paged(phydev, 0, RTL821XEXT_MEDIA_PAGE_SELECT, RTL821X_MEDIA_PAGE_COPPER);
	phy_write_paged(phydev, 0x1f, 0x1b, 0x0002);
	val = phy_read_paged(phydev, 0x1f, 0x1c);
	if (val != 0x6276) {
		phydev_err(phydev, "Expected external RTL8214FC, found PHY-ID %x\n", val);
		return -1;
	}
	phydev_info(phydev, "Detected external RTL8214FC\n");

	h = rtl838x_request_fw(phydev, &rtl838x_8214fc_fw, FIRMWARE_838X_8214FC_1);
	if (!h)
		return -1;

	if (h->phy != 0x8214fc00) {
		phydev_err(phydev, "Wrong firmware file: PHY mismatch.\n");
		return -1;
	}

	rtl8380_rtl8214fc_perchip = (void *)h + sizeof(struct fw_header)
		   + h->parts[0].start;

	rtl8380_rtl8214fc_perport = (void *)h + sizeof(struct fw_header)
		   + h->parts[1].start;

	/* detect phy version */
	phy_write_paged(phydev, RTL83XX_PAGE_RAW, 27, 0x0004);
	val = phy_read_paged(phydev, RTL83XX_PAGE_RAW, 28);

	val = phy_read(phydev, 16);
	if (val & (1 << 11))
		rtl8380_rtl8214fc_on_off(phydev, true);
	else
		rtl8380_phy_reset(phydev);

	msleep(100);
	phy_write_paged(phydev, 0, RTL821XEXT_MEDIA_PAGE_SELECT, RTL821X_MEDIA_PAGE_COPPER);

	i = 0;
	while (rtl8380_rtl8214fc_perchip[i * 3]
	       && rtl8380_rtl8214fc_perchip[i * 3 + 1]) {
		if (rtl8380_rtl8214fc_perchip[i * 3 + 1] == 0x1f)
			page = rtl8380_rtl8214fc_perchip[i * 3 + 2];
		if (rtl8380_rtl8214fc_perchip[i * 3 + 1] == 0x13 && page == 0x260) {
			val = phy_read_paged(phydev, 0x260, 13);
			val = (val & 0x1f00) | (rtl8380_rtl8214fc_perchip[i * 3 + 2]
				& 0xe0ff);
			phy_write_paged(phydev, RTL83XX_PAGE_RAW,
					rtl8380_rtl8214fc_perchip[i * 3 + 1], val);
		} else {
			phy_write_paged(phydev, RTL83XX_PAGE_RAW,
					rtl8380_rtl8214fc_perchip[i * 3 + 1],
					rtl8380_rtl8214fc_perchip[i * 3 + 2]);
		}
		i++;
	}

	/* Force copper medium */
	for (i = 0; i < 4; i++) {
		phy_package_port_write_paged(phydev, i, RTL83XX_PAGE_RAW, RTL8XXX_PAGE_SELECT, RTL8XXX_PAGE_MAIN);
		phy_package_port_write_paged(phydev, i, RTL83XX_PAGE_RAW, RTL821XEXT_MEDIA_PAGE_SELECT, RTL821X_MEDIA_PAGE_COPPER);
	}

	/* Enable PHY */
	for (i = 0; i < 4; i++) {
		phy_package_port_write_paged(phydev, i, RTL83XX_PAGE_RAW, RTL8XXX_PAGE_SELECT, RTL8XXX_PAGE_MAIN);
		phy_package_port_write_paged(phydev, i, RTL83XX_PAGE_RAW, 0x00, 0x1140);
	}
	mdelay(100);

	/* Disable Autosensing */
	for (i = 0; i < 4; i++) {
		for (l = 0; l < 100; l++) {
			val = phy_package_port_read_paged(phydev, i, RTL821X_PAGE_GPHY, 0x10);
			if ((val & 0x7) >= 3)
				break;
		}
		if (l >= 100) {
			phydev_err(phydev, "Could not disable autosensing\n");
			return -1;
		}
	}

	/* Request patch */
	for (i = 0; i < 4; i++) {
		phy_package_port_write_paged(phydev, i, RTL83XX_PAGE_RAW, RTL8XXX_PAGE_SELECT, RTL821X_PAGE_PATCH);
		phy_package_port_write_paged(phydev, i, RTL83XX_PAGE_RAW, 0x10, 0x0010);
	}
	mdelay(300);

	/* Verify patch readiness */
	for (i = 0; i < 4; i++) {
		for (l = 0; l < 100; l++) {
			val = phy_package_port_read_paged(phydev, i, RTL821X_PAGE_STATE, 0x10);
			if (val & 0x40)
				break;
		}
		if (l >= 100) {
			phydev_err(phydev, "Could not patch PHY\n");
			return -1;
		}
	}
	/* Use Broadcast ID method for patching */
	rtl821x_phy_setup_package_broadcast(phydev, true);

	i = 0;
	while (rtl8380_rtl8214fc_perport[i * 2]) {
		phy_write_paged(phydev, RTL83XX_PAGE_RAW, rtl8380_rtl8214fc_perport[i * 2],
				  rtl8380_rtl8214fc_perport[i * 2 + 1]);
		i++;
	}

	/*Disable broadcast ID*/
	rtl821x_phy_setup_package_broadcast(phydev, false);

	/* Auto medium selection */
	for (i = 0; i < 4; i++) {
		phy_write_paged(phydev, RTL83XX_PAGE_RAW, RTL8XXX_PAGE_SELECT, RTL8XXX_PAGE_MAIN);
		phy_write_paged(phydev, RTL83XX_PAGE_RAW, RTL821XEXT_MEDIA_PAGE_SELECT, RTL821X_MEDIA_PAGE_AUTO);
	}

	return 0;
}

static int rtl8214fc_match_phy_device(struct phy_device *phydev)
{
	int addr = phydev->mdio.addr;

	return phydev->phy_id == PHY_ID_RTL8214FC && addr >= 24;
}

static int rtl8380_configure_serdes(struct phy_device *phydev)
{
	u32 v;
	u32 sds_conf_value;
	int i;
	struct fw_header *h;
	u32 *rtl8380_sds_take_reset;
	u32 *rtl8380_sds_common;
	u32 *rtl8380_sds01_qsgmii_6275b;
	u32 *rtl8380_sds23_qsgmii_6275b;
	u32 *rtl8380_sds4_fiber_6275b;
	u32 *rtl8380_sds5_fiber_6275b;
	u32 *rtl8380_sds_reset;
	u32 *rtl8380_sds_release_reset;

	phydev_info(phydev, "Detected internal RTL8380 SERDES\n");

	h = rtl838x_request_fw(phydev, &rtl838x_8218b_fw, FIRMWARE_838X_8380_1);
	if (!h)
		return -1;

	if (h->magic != 0x83808380) {
		phydev_err(phydev, "Wrong firmware file: magic number mismatch.\n");
		return -1;
	}

	rtl8380_sds_take_reset = (void *)h + sizeof(struct fw_header)
		   + h->parts[0].start;

	rtl8380_sds_common = (void *)h + sizeof(struct fw_header)
		   + h->parts[1].start;

	rtl8380_sds01_qsgmii_6275b = (void *)h + sizeof(struct fw_header)
		   + h->parts[2].start;

	rtl8380_sds23_qsgmii_6275b = (void *)h + sizeof(struct fw_header)
		   + h->parts[3].start;

	rtl8380_sds4_fiber_6275b = (void *)h + sizeof(struct fw_header)
		   + h->parts[4].start;

	rtl8380_sds5_fiber_6275b = (void *)h + sizeof(struct fw_header)
		   + h->parts[5].start;

	rtl8380_sds_reset = (void *)h + sizeof(struct fw_header)
		   + h->parts[6].start;

	rtl8380_sds_release_reset = (void *)h + sizeof(struct fw_header)
		   + h->parts[7].start;

	/* Back up serdes power off value */
	sds_conf_value = sw_r32(RTL838X_SDS_CFG_REG);
	pr_info("SDS power down value: %x\n", sds_conf_value);

	/* take serdes into reset */
	i = 0;
	while (rtl8380_sds_take_reset[2 * i]) {
		sw_w32(rtl8380_sds_take_reset[2 * i + 1], rtl8380_sds_take_reset[2 * i]);
		i++;
		udelay(1000);
	}

	/* apply common serdes patch */
	i = 0;
	while (rtl8380_sds_common[2 * i]) {
		sw_w32(rtl8380_sds_common[2 * i + 1], rtl8380_sds_common[2 * i]);
		i++;
		udelay(1000);
	}

	/* internal R/W enable */
	sw_w32(3, RTL838X_INT_RW_CTRL);

	/* SerDes ports 4 and 5 are FIBRE ports */
	sw_w32_mask(0x7 | 0x38, 1 | (1 << 3), RTL838X_INT_MODE_CTRL);

	/* SerDes module settings, SerDes 0-3 are QSGMII */
	v = 0x6 << 25 | 0x6 << 20 | 0x6 << 15 | 0x6 << 10;
	/* SerDes 4 and 5 are 1000BX FIBRE */
	v |= 0x4 << 5 | 0x4;
	sw_w32(v, RTL838X_SDS_MODE_SEL);

	pr_info("PLL control register: %x\n", sw_r32(RTL838X_PLL_CML_CTRL));
	sw_w32_mask(0xfffffff0, 0xaaaaaaaf & 0xf, RTL838X_PLL_CML_CTRL);
	i = 0;
	while (rtl8380_sds01_qsgmii_6275b[2 * i]) {
		sw_w32(rtl8380_sds01_qsgmii_6275b[2 * i + 1],
			rtl8380_sds01_qsgmii_6275b[2 * i]);
		i++;
	}

	i = 0;
	while (rtl8380_sds23_qsgmii_6275b[2 * i]) {
		sw_w32(rtl8380_sds23_qsgmii_6275b[2 * i + 1], rtl8380_sds23_qsgmii_6275b[2 * i]);
		i++;
	}

	i = 0;
	while (rtl8380_sds4_fiber_6275b[2 * i]) {
		sw_w32(rtl8380_sds4_fiber_6275b[2 * i + 1], rtl8380_sds4_fiber_6275b[2 * i]);
		i++;
	}

	i = 0;
	while (rtl8380_sds5_fiber_6275b[2 * i]) {
		sw_w32(rtl8380_sds5_fiber_6275b[2 * i + 1], rtl8380_sds5_fiber_6275b[2 * i]);
		i++;
	}

	i = 0;
	while (rtl8380_sds_reset[2 * i]) {
		sw_w32(rtl8380_sds_reset[2 * i + 1], rtl8380_sds_reset[2 * i]);
		i++;
	}

	i = 0;
	while (rtl8380_sds_release_reset[2 * i]) {
		sw_w32(rtl8380_sds_release_reset[2 * i + 1], rtl8380_sds_release_reset[2 * i]);
		i++;
	}

	pr_info("SDS power down value now: %x\n", sw_r32(RTL838X_SDS_CFG_REG));
	sw_w32(sds_conf_value, RTL838X_SDS_CFG_REG);

	pr_info("Configuration of SERDES done\n");
	return 0;
}

static int rtl8390_configure_serdes(struct phy_device *phydev)
{
	phydev_info(phydev, "Detected internal RTL8390 SERDES\n");

	/* In autoneg state, force link, set SR4_CFG_EN_LINK_FIB1G */
	sw_w32_mask(0, 1 << 18, RTL839X_SDS12_13_XSG0 + 0x0a);

	/* Disable EEE: Clear FRE16_EEE_RSG_FIB1G, FRE16_EEE_STD_FIB1G,
	 * FRE16_C1_PWRSAV_EN_FIB1G, FRE16_C2_PWRSAV_EN_FIB1G
	 * and FRE16_EEE_QUIET_FIB1G
	 */
	sw_w32_mask(0x1f << 10, 0, RTL839X_SDS12_13_XSG0 + 0xe0);

	return 0;
}

void rtl9300_sds_field_w(int sds, u32 page, u32 reg, int end_bit, int start_bit, u32 v)
{
	int l = end_bit - start_bit + 1;
	u32 data = v;

	if (l < 32) {
		u32 mask = BIT(l) - 1;

		data = rtl930x_read_sds_phy(sds, page, reg);
		data &= ~(mask << start_bit);
		data |= (v & mask) << start_bit;
	}

	rtl930x_write_sds_phy(sds, page, reg, data);
}

u32 rtl9300_sds_field_r(int sds, u32 page, u32 reg, int end_bit, int start_bit)
{
	int l = end_bit - start_bit + 1;
	u32 v = rtl930x_read_sds_phy(sds, page, reg);

	if (l >= 32)
		return v;

	return (v >> start_bit) & (BIT(l) - 1);
}

/* Read the link and speed status of the internal SerDes of the RTL9300
 */
static int rtl9300_read_status(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	int phy_addr = phydev->mdio.addr;
	struct device_node *dn;
	u32 sds_num = 0, status, latch_status, mode;

	phydev->link = 0;
	phydev->speed = SPEED_UNKNOWN;
	phydev->duplex = DUPLEX_UNKNOWN;

	if (dev->of_node) {
		dn = dev->of_node;

		if (of_property_read_u32(dn, "sds", &sds_num))
			sds_num = -1;
		pr_debug("%s: Port %d, SerDes is %d\n", __func__, phy_addr, sds_num);
	} else {
		dev_err(dev, "No DT node.\n");
		return -EINVAL;
	}

	if (sds_num < 0)
		return 0;

	mode = rtl9300_sds_mode_get(sds_num);
	if (mode == 0x1f)  // SerDes "off", look at forced mode instead
		 mode = rtl9300_sds_field_r(sds_num, 0x1f, 9, 11, 7);
	if (mode == 0x1a) {		// 10GR mode
		status = rtl9300_sds_field_r(sds_num, 0x5, 0, 12, 12);
		latch_status = rtl9300_sds_field_r(sds_num, 0x4, 1, 2, 2);
		status |= rtl9300_sds_field_r(sds_num, 0x5, 0, 12, 12);
		latch_status |= rtl9300_sds_field_r(sds_num, 0x4, 1, 2, 2);
	} else {
		status = rtl9300_sds_field_r(sds_num, 0x1, 29, 8, 0);
		latch_status = rtl9300_sds_field_r(sds_num, 0x1, 30, 8, 0);
		status |= rtl9300_sds_field_r(sds_num, 0x1, 29, 8, 0);
		latch_status |= rtl9300_sds_field_r(sds_num, 0x1, 30, 8, 0);
	}

	pr_debug("%s link status: status: %d, latch %d\n", __func__, status, latch_status);

	if (latch_status) {
		phydev->link = true;
		if (mode == 0x1a) {
			phydev->speed = SPEED_10000;
			phydev->interface = PHY_INTERFACE_MODE_10GBASER;
		} else {
			phydev->speed = SPEED_1000;
			phydev->interface = PHY_INTERFACE_MODE_1000BASEX;
		}
		phydev->duplex = DUPLEX_FULL;
	}

	return 0;
}

void rtl930x_sds_rx_rst(int sds_num, phy_interface_t phy_if)
{
	int page = 0x2e; // 10GR and USXGMII

	if (phy_if == PHY_INTERFACE_MODE_1000BASEX)
		page = 0x24;
	else if (phy_if != PHY_INTERFACE_MODE_10GBASER
		  && phy_if != PHY_INTERFACE_MODE_USXGMII)
		return;

	rtl9300_sds_field_w(sds_num, page, 0x15, 4, 4, 0x1);
	mdelay(5);
	rtl9300_sds_field_w(sds_num, page, 0x15, 4, 4, 0x0);
}

/*
 * Force PHY modes on 10GBit Serdes
 * Ports which are wired to be used with 10GR, 1000BX fibre, HISGMII or 2500Base_X
 * use this function to set the SerDes mode, not rtl9300_sds_set
 * For them the SerDes mode register RTL9300_SDS_MODE_SEL must be configured ot be off (0x1f)
 */
void rtl9300_force_sds_mode(int sds, phy_interface_t phy_if)
{
	int sds_mode = 0x1f;	// Default is SDS is off
	bool lc_on;		// Use LC circuit for PLL?
	int i, lc_value;
	int lane_0 = (sds % 2) ? sds - 1 : sds;
	u32 v, cr_0, cr_1, cr_2;
	u32 m_bit, l_bit;

	pr_info("%s: SDS: %d, PHY mode %d\n", __func__, sds, phy_if);
	switch (phy_if) {
	case PHY_INTERFACE_MODE_SGMII:
		sds_mode = 0x2;
		lc_on = false;
		lc_value = 0x1;
		break;

	case PHY_INTERFACE_MODE_HSGMII:
		sds_mode = 0x12;
		lc_value = 0x3;
		// Configure LC on only if port HW-type is also PHY_INTERFACE_MODE_HSGMII
		lc_on = false;
		break;

	case PHY_INTERFACE_MODE_1000BASEX:
		sds_mode = 0x04;
		lc_on = false;
		break;

	case PHY_INTERFACE_MODE_2500BASEX:
		sds_mode = 0x16;
		lc_value = 0x3;
		// Configure LC on only if port HW-type is also PHY_INTERFACE_MODE_2500BASEX
		lc_on = true;
		break;

	case PHY_INTERFACE_MODE_10GBASER:
		sds_mode = 0x1a;
		lc_on = true;
		lc_value = 0x5;
		break;

	case PHY_INTERFACE_MODE_NA:
		// This will disable SerDes
		sds_mode = 0x1f;
		break;

	default:
		pr_err("%s: unknown serdes mode: %s\n",
		       __func__, phy_modes(phy_if));
		return;
	}

	pr_info("%s: forcing SDS mode %x\n", __func__, sds_mode);

	// Power down SerDes
	rtl9300_sds_field_w(sds, 0x20, 0, 7, 6, 0x3);

	// Force mode enable
	rtl9300_sds_field_w(sds, 0x1f, 9, 6, 6, 0x1);

	/* SerDes off */
	rtl9300_sds_field_w(sds, 0x1f, 9, 11, 7, 0x1f);

	if (phy_if == PHY_INTERFACE_MODE_NA)
		return;

	// Enable LC and ring
	rtl9300_sds_field_w(lane_0, 0x20, 18, 3, 0, 0xf);

	if (sds == lane_0)
		rtl9300_sds_field_w(lane_0, 0x20, 18, 5, 4, 0x1);
	else
		rtl9300_sds_field_w(lane_0, 0x20, 18, 7, 6, 0x1);

	rtl9300_sds_field_w(sds, 0x20, 0, 5, 4, 0x3);

	if (lc_on)
		rtl9300_sds_field_w(lane_0, 0x20, 18, 11, 8, lc_value);
	else
		rtl9300_sds_field_w(lane_0, 0x20, 18, 15, 12, lc_value);

	// Force analog LC & ring on
	rtl9300_sds_field_w(lane_0, 0x21, 11, 3, 0, 0xf);

	v = lc_on ? 0x3 : 0x1;

	if (sds == lane_0)
		rtl9300_sds_field_w(lane_0, 0x20, 18, 5, 4, v);
	else
		rtl9300_sds_field_w(lane_0, 0x20, 18, 7, 6, v);

	// Force SerDes mode
	rtl9300_sds_field_w(sds, 0x1f, 9, 6, 6, 1);
	rtl9300_sds_field_w(sds, 0x1f, 9, 11, 7, sds_mode);

	// Toggle LC or Ring
	for (i = 0; i < 20; i++) {
		mdelay(200);

		pr_info("%s toggling LC or Ring for 10gr, round %d\n", __func__, i);
		rtl930x_write_sds_phy(lane_0, 0x1f, 2, 53);

		m_bit = (lane_0 == sds) ? (4) : (5);
		l_bit = (lane_0 == sds) ? (4) : (5);

		cr_0 = rtl9300_sds_field_r(lane_0, 0x1f, 20, m_bit, l_bit);
		mdelay(10);
		cr_1 = rtl9300_sds_field_r(lane_0, 0x1f, 20, m_bit, l_bit);
		mdelay(10);
		cr_2 = rtl9300_sds_field_r(lane_0, 0x1f, 20, m_bit, l_bit);

		if (cr_0 && cr_1 && cr_2) {
			u32 t;
			if (phy_if != PHY_INTERFACE_MODE_10GBASER)
				break;

			t = rtl9300_sds_field_r(sds, 0x6, 0x1, 2, 2);
			rtl9300_sds_field_w(sds, 0x6, 0x1, 2, 2, 0x1);

			// Reset FSM
			rtl9300_sds_field_w(sds, 0x6, 0x2, 12, 12, 0x1);
			mdelay(10);
			rtl9300_sds_field_w(sds, 0x6, 0x2, 12, 12, 0x0);
			mdelay(10);

			// Need to read this twice
			v = rtl9300_sds_field_r(sds, 0x5, 0, 12, 12);
			v = rtl9300_sds_field_r(sds, 0x5, 0, 12, 12);

			rtl9300_sds_field_w(sds, 0x6, 0x1, 2, 2, t);

			// Reset FSM again
			rtl9300_sds_field_w(sds, 0x6, 0x2, 12, 12, 0x1);
			mdelay(10);
			rtl9300_sds_field_w(sds, 0x6, 0x2, 12, 12, 0x0);
			mdelay(10);

			if (v == 1)
				break;
		}

		m_bit = (phy_if == PHY_INTERFACE_MODE_10GBASER) ? 3 : 1;
		l_bit = (phy_if == PHY_INTERFACE_MODE_10GBASER) ? 2 : 0;

		rtl9300_sds_field_w(lane_0, 0x21, 11, m_bit, l_bit, 0x2);
		mdelay(10);
		rtl9300_sds_field_w(lane_0, 0x21, 11, m_bit, l_bit, 0x3);
	}

	rtl930x_sds_rx_rst(sds, phy_if);

	// Re-enable power
	rtl9300_sds_field_w(sds, 0x20, 0, 7, 6, 0);
	pr_info("%s end power 0x20 0 %x\n", __func__, rtl930x_read_sds_phy(sds, 0x20, 0));

	pr_info("%s -------------------- serdes %d forced to %x DONE\n", __func__, sds, sds_mode);
}

/*
 * Configure the transmitter of the SerDes, in particular the pre- main- and post-amplifiers
 * phy_if is the physical interface to the phy, not the protocol currently in use
 */
void rtl9300_sds_tx_config(int sds, phy_interface_t phy_if)
{
	// parameters: rtl9303_80G_txParam_s2
	int impedance = 0x8;
	int pre_amp = 0x2;
	int main_amp = 0x9;
	int post_amp = 0x2;
	int pre_en = 0x1;
	int post_en = 0x1;
	int page;

	switch(phy_if) {
	case PHY_INTERFACE_MODE_1000BASEX:
		pre_amp = 0x1;
		main_amp = 0x9;
		post_amp = 0x1;
		pre_en = 1;
		post_en = 1;
		page = 0x25;
		break;

	case PHY_INTERFACE_MODE_HSGMII:
	case PHY_INTERFACE_MODE_2500BASEX:
		pre_amp = 0;
		post_amp = 0x8;
		pre_en = 0;
		page = 0x29;
		break;

	case PHY_INTERFACE_MODE_XGMII:
		pre_en = 0;
		pre_amp = 0;
		main_amp = 16;
		post_amp = 0;
		post_en	= 0;
		/* fallthrough */
	case PHY_INTERFACE_MODE_10GBASER:
	case PHY_INTERFACE_MODE_USXGMII:
		page = 0x2f;
		break;
	default:
		pr_err("%s: unsupported PHY mode\n", __func__);
		return;
	}

	pr_info("%s SerDes %d, pre-amp enable %d, pre-amp val %d, main-amp %d, post-amp enable %d, post-amp val %d, impedance %d\n",
		__func__, sds, pre_en, pre_amp, main_amp, post_en, post_amp, impedance);
	rtl9300_sds_field_w(sds, page, 0x1, 15, 11, pre_amp);
	rtl9300_sds_field_w(sds, page, 0x7, 0, 0, pre_en);
	rtl9300_sds_field_w(sds, page, 0x7, 8, 4, main_amp);
	rtl9300_sds_field_w(sds, page, 0x6, 4, 0, post_amp);
	rtl9300_sds_field_w(sds, page, 0x7, 3, 3, post_en);
	rtl9300_sds_field_w(sds, page, 0x18, 15, 12, impedance);
}

/*
 * Wait for clock ready, this assumes the SerDes is in XGMII mode
 * timeout is in ms
 */
int rtl9300_sds_clock_wait(int timeout)
{
	u32 v;
	unsigned long start = jiffies;

	do {
		rtl9300_sds_field_w(2, 0x1f, 0x2, 15, 0, 53);
		v = rtl9300_sds_field_r(2, 0x1f, 20, 5, 4);
		if (v == 3)
			return 0;
	} while (jiffies < start + (HZ / 1000) * timeout);

	return 1;
}

void rtl9300_serdes_mac_link_config(int sds, bool tx_normal, bool rx_normal)
{
	u32 v10, v1;

	v10 = rtl930x_read_sds_phy(sds, 6, 2); // 10GBit, page 6, reg 2
	v1 = rtl930x_read_sds_phy(sds, 0, 0); // 1GBit, page 0, reg 0
	pr_info("%s: registers before %08x %08x\n", __func__, v10, v1);

	v10 &= ~(BIT(13) | BIT(14));
	v1 &= ~(BIT(8) | BIT(9));

	v10 |= rx_normal ? 0 : BIT(13);
	v1 |= rx_normal ? 0 : BIT(9);

	v10 |= tx_normal ? 0 : BIT(14);
	v1 |= tx_normal ? 0 : BIT(8);

	rtl930x_write_sds_phy(sds, 6, 2, v10);
	rtl930x_write_sds_phy(sds, 0, 0, v1);

	v10 = rtl930x_read_sds_phy(sds, 6, 2);
	v1 = rtl930x_read_sds_phy(sds, 0, 0);
	pr_info("%s: registers after %08x %08x\n", __func__, v10, v1);
}

void rtl9300_sds_rxcal_dcvs_manual(u32 sds_num, u32 dcvs_id, bool manual, u32 dvcs_list[])
{
	if (manual) {
		switch(dcvs_id) {
		case 0:
			rtl9300_sds_field_w(sds_num, 0x2e, 0x1e, 14, 14, 0x1);
			rtl9300_sds_field_w(sds_num, 0x2f, 0x03, 5, 5, dvcs_list[0]);
			rtl9300_sds_field_w(sds_num, 0x2f, 0x03, 4, 0, dvcs_list[1]);
			break;
		case 1:
			rtl9300_sds_field_w(sds_num, 0x2e, 0x1e, 13, 13, 0x1);
			rtl9300_sds_field_w(sds_num, 0x2e, 0x1d, 15, 15, dvcs_list[0]);
			rtl9300_sds_field_w(sds_num, 0x2e, 0x1d, 14, 11, dvcs_list[1]);
			break;
		case 2:
			rtl9300_sds_field_w(sds_num, 0x2e, 0x1e, 12, 12, 0x1);
			rtl9300_sds_field_w(sds_num, 0x2e, 0x1d, 10, 10, dvcs_list[0]);
			rtl9300_sds_field_w(sds_num, 0x2e, 0x1d, 9, 6, dvcs_list[1]);
			break;
		case 3:
			rtl9300_sds_field_w(sds_num, 0x2e, 0x1e, 11, 11, 0x1);
			rtl9300_sds_field_w(sds_num, 0x2e, 0x1d, 5, 5, dvcs_list[0]);
			rtl9300_sds_field_w(sds_num, 0x2e, 0x1d, 4, 1, dvcs_list[1]);
			break;
		case 4:
			rtl9300_sds_field_w(sds_num, 0x2e, 0x01, 15, 15, 0x1);
			rtl9300_sds_field_w(sds_num, 0x2e, 0x11, 10, 10, dvcs_list[0]);
			rtl9300_sds_field_w(sds_num, 0x2e, 0x11, 9, 6, dvcs_list[1]);
			break;
		case 5:
			rtl9300_sds_field_w(sds_num, 0x2e, 0x02, 11, 11, 0x1);
			rtl9300_sds_field_w(sds_num, 0x2e, 0x11, 4, 4, dvcs_list[0]);
			rtl9300_sds_field_w(sds_num, 0x2e, 0x11, 3, 0, dvcs_list[1]);
			break;
		default:
			break;
		}
	} else {
		switch(dcvs_id) {
		case 0:
			rtl9300_sds_field_w(sds_num, 0x2e, 0x1e, 14, 14, 0x0);
			break;
		case 1:
			rtl9300_sds_field_w(sds_num, 0x2e, 0x1e, 13, 13, 0x0);
			break;
		case 2:
			rtl9300_sds_field_w(sds_num, 0x2e, 0x1e, 12, 12, 0x0);
			break;
		case 3:
			rtl9300_sds_field_w(sds_num, 0x2e, 0x1e, 11, 11, 0x0);
			break;
		case 4:
			rtl9300_sds_field_w(sds_num, 0x2e, 0x01, 15, 15, 0x0);
			break;
		case 5:
			rtl9300_sds_field_w(sds_num, 0x2e, 0x02, 11, 11, 0x0);
			break;
		default:
			break;
		}
		mdelay(1);
	}
}

void rtl9300_sds_rxcal_dcvs_get(u32 sds_num, u32 dcvs_id, u32 dcvs_list[])
{
	u32 dcvs_sign_out = 0, dcvs_coef_bin = 0;
	bool dcvs_manual;

	if (!(sds_num % 2))
		rtl930x_write_sds_phy(sds_num, 0x1f, 0x2, 0x2f);
	else
		rtl930x_write_sds_phy(sds_num - 1, 0x1f, 0x2, 0x31);

	// ##Page0x2E, Reg0x15[9], REG0_RX_EN_TEST=[1]
	rtl9300_sds_field_w(sds_num, 0x2e, 0x15, 9, 9, 0x1);

	// ##Page0x21, Reg0x06[11 6], REG0_RX_DEBUG_SEL=[1 0 x x x x]
	rtl9300_sds_field_w(sds_num, 0x21, 0x06, 11, 6, 0x20);

	switch(dcvs_id) {
	case 0:
		rtl9300_sds_field_w(sds_num, 0x2f, 0x0c, 5, 0, 0x22);
		mdelay(1);

		// ##DCVS0 Read Out
		dcvs_sign_out = rtl9300_sds_field_r(sds_num, 0x1f, 0x14, 4, 4);
		dcvs_coef_bin = rtl9300_sds_field_r(sds_num, 0x1f, 0x14, 3, 0);
		dcvs_manual = !!rtl9300_sds_field_r(sds_num, 0x2e, 0x1e, 14, 14);
		break;

	case 1:
		rtl9300_sds_field_w(sds_num, 0x2f, 0x0c, 5, 0, 0x23);
		mdelay(1);

		// ##DCVS0 Read Out
		dcvs_coef_bin = rtl9300_sds_field_r(sds_num, 0x1f, 0x14, 4, 4);
		dcvs_coef_bin = rtl9300_sds_field_r(sds_num, 0x1f, 0x14, 3, 0);
		dcvs_manual = !!rtl9300_sds_field_r(sds_num, 0x2e, 0x1e, 13, 13);
		break;

	case 2:
		rtl9300_sds_field_w(sds_num, 0x2f, 0x0c, 5, 0, 0x24);
		mdelay(1);

		// ##DCVS0 Read Out
		dcvs_sign_out = rtl9300_sds_field_r(sds_num, 0x1f, 0x14, 4, 4);
		dcvs_coef_bin = rtl9300_sds_field_r(sds_num, 0x1f, 0x14, 3, 0);
		dcvs_manual = !!rtl9300_sds_field_r(sds_num, 0x2e, 0x1e, 12, 12);
		break;
	case 3:
		rtl9300_sds_field_w(sds_num, 0x2f, 0x0c, 5, 0, 0x25);
		mdelay(1);

		// ##DCVS0 Read Out
		dcvs_sign_out = rtl9300_sds_field_r(sds_num, 0x1f, 0x14, 4, 4);
		dcvs_coef_bin = rtl9300_sds_field_r(sds_num, 0x1f, 0x14, 3, 0);
		dcvs_manual = rtl9300_sds_field_r(sds_num, 0x2e, 0x1e, 11, 11);
		break;

	case 4:
		rtl9300_sds_field_w(sds_num, 0x2f, 0x0c, 5, 0, 0x2c);
		mdelay(1);

		// ##DCVS0 Read Out
		dcvs_sign_out = rtl9300_sds_field_r(sds_num, 0x1f, 0x14, 4, 4);
		dcvs_coef_bin = rtl9300_sds_field_r(sds_num, 0x1f, 0x14, 3, 0);
		dcvs_manual = !!rtl9300_sds_field_r(sds_num, 0x2e, 0x01, 15, 15);
		break;

	case 5:
		rtl9300_sds_field_w(sds_num, 0x2f, 0x0c, 5, 0, 0x2d);
		mdelay(1);

		// ##DCVS0 Read Out
		dcvs_sign_out = rtl9300_sds_field_r(sds_num, 0x1f, 0x14, 4, 4);
		dcvs_coef_bin = rtl9300_sds_field_r(sds_num, 0x1f, 0x14, 3, 0);
		dcvs_manual = rtl9300_sds_field_r(sds_num, 0x2e, 0x02, 11, 11);
		break;

	default:
		break;
	}

	if (dcvs_sign_out)
		pr_info("%s DCVS %u Sign: -", __func__, dcvs_id);
	else
		pr_info("%s DCVS %u Sign: +", __func__, dcvs_id);

	pr_info("DCVS %u even coefficient = %u", dcvs_id, dcvs_coef_bin);
	pr_info("DCVS %u manual = %u", dcvs_id, dcvs_manual);

	dcvs_list[0] = dcvs_sign_out;
	dcvs_list[1] = dcvs_coef_bin;
}

void rtl9300_sds_rxcal_leq_manual(u32 sds_num, bool manual, u32 leq_gray)
{
	if (manual) {
		rtl9300_sds_field_w(sds_num, 0x2e, 0x18, 15, 15, 0x1);
		rtl9300_sds_field_w(sds_num, 0x2e, 0x16, 14, 10, leq_gray);
	} else {
		rtl9300_sds_field_w(sds_num, 0x2e, 0x18, 15, 15, 0x0);
		mdelay(100);
	}
}

void rtl9300_sds_rxcal_leq_offset_manual(u32 sds_num, bool manual, u32 offset)
{
	if (manual) {
		rtl9300_sds_field_w(sds_num, 0x2e, 0x17, 6, 2, offset);
	} else {
		rtl9300_sds_field_w(sds_num, 0x2e, 0x17, 6, 2, offset);
		mdelay(1);
	}
}

#define GRAY_BITS 5
u32 rtl9300_sds_rxcal_gray_to_binary(u32 gray_code)
{
	int i, j, m;
	u32 g[GRAY_BITS];
	u32 c[GRAY_BITS];
	u32 leq_binary = 0;

	for(i = 0; i < GRAY_BITS; i++)
		g[i] = (gray_code & BIT(i)) >> i;

	m = GRAY_BITS - 1;

	c[m] = g[m];

	for(i = 0; i < m; i++) {
		c[i] = g[i];
		for(j  = i + 1; j < GRAY_BITS; j++)
			c[i] = c[i] ^ g[j];
	}

	for(i = 0; i < GRAY_BITS; i++)
		leq_binary += c[i] << i;

	return leq_binary;
}

u32 rtl9300_sds_rxcal_leq_read(int sds_num)
{
	u32 leq_gray, leq_bin;
	bool leq_manual;

	if (!(sds_num % 2))
		rtl930x_write_sds_phy(sds_num, 0x1f, 0x2, 0x2f);
	else
		rtl930x_write_sds_phy(sds_num - 1, 0x1f, 0x2, 0x31);

	// ##Page0x2E, Reg0x15[9], REG0_RX_EN_TEST=[1]
	rtl9300_sds_field_w(sds_num, 0x2e, 0x15, 9, 9, 0x1);

	// ##Page0x21, Reg0x06[11 6], REG0_RX_DEBUG_SEL=[0 1 x x x x]
	rtl9300_sds_field_w(sds_num, 0x21, 0x06, 11, 6, 0x10);
	mdelay(1);

	// ##LEQ Read Out
	leq_gray = rtl9300_sds_field_r(sds_num, 0x1f, 0x14, 7, 3);
	leq_manual = !!rtl9300_sds_field_r(sds_num, 0x2e, 0x18, 15, 15);
	leq_bin = rtl9300_sds_rxcal_gray_to_binary(leq_gray);

	pr_info("LEQ_gray: %u, LEQ_bin: %u", leq_gray, leq_bin);
	pr_info("LEQ manual: %u", leq_manual);

	return leq_bin;
}

void rtl9300_sds_rxcal_vth_manual(u32 sds_num, bool manual, u32 vth_list[])
{
	if (manual) {
		rtl9300_sds_field_w(sds_num, 0x2e, 0x0f, 13, 13, 0x1);
		rtl9300_sds_field_w(sds_num, 0x2e, 0x13, 5, 3, vth_list[0]);
		rtl9300_sds_field_w(sds_num, 0x2e, 0x13, 2, 0, vth_list[1]);
	} else {
		rtl9300_sds_field_w(sds_num, 0x2e, 0x0f, 13, 13, 0x0);
		mdelay(10);
	}
}

void rtl9300_sds_rxcal_vth_get(u32  sds_num, u32 vth_list[])
{
	u32 vth_manual;

	//##Page0x1F, Reg0x02[15 0], REG_DBGO_SEL=[0x002F]; //Lane0
	//##Page0x1F, Reg0x02[15 0], REG_DBGO_SEL=[0x0031]; //Lane1
	if (!(sds_num % 2))
		rtl930x_write_sds_phy(sds_num, 0x1f, 0x2, 0x2f);
	else
		rtl930x_write_sds_phy(sds_num - 1, 0x1f, 0x2, 0x31);

	//##Page0x2E, Reg0x15[9], REG0_RX_EN_TEST=[1]
	rtl9300_sds_field_w(sds_num, 0x2e, 0x15, 9, 9, 0x1);
	//##Page0x21, Reg0x06[11 6], REG0_RX_DEBUG_SEL=[1 0 x x x x]
	rtl9300_sds_field_w(sds_num, 0x21, 0x06, 11, 6, 0x20);
	//##Page0x2F, Reg0x0C[5 0], REG0_COEF_SEL=[0 0 1 1 0 0]
	rtl9300_sds_field_w(sds_num, 0x2f, 0x0c, 5, 0, 0xc);

	mdelay(1);

	//##VthP & VthN Read Out
	vth_list[0] = rtl9300_sds_field_r(sds_num, 0x1f, 0x14, 2, 0); // v_thp set bin
	vth_list[1] = rtl9300_sds_field_r(sds_num, 0x1f, 0x14, 5, 3); // v_thn set bin

	pr_info("vth_set_bin = %d", vth_list[0]);
	pr_info("vth_set_bin = %d", vth_list[1]);

	vth_manual = !!rtl9300_sds_field_r(sds_num, 0x2e, 0x0f, 13, 13);
	pr_info("Vth Maunal = %d", vth_manual);
}

void rtl9300_sds_rxcal_tap_manual(u32 sds_num, int tap_id, bool manual, u32 tap_list[])
{
	if (manual) {
		switch(tap_id) {
		case 0:
			//##REG0_LOAD_IN_INIT[0]=1; REG0_TAP0_INIT[5:0]=Tap0_Value
			rtl9300_sds_field_w(sds_num, 0x2e, 0x0f, tap_id + 7, tap_id + 7, 0x1);
			rtl9300_sds_field_w(sds_num, 0x2f, 0x03, 5, 5, tap_list[0]);
			rtl9300_sds_field_w(sds_num, 0x2f, 0x03, 4, 0, tap_list[1]);
			break;
		case 1:
			rtl9300_sds_field_w(sds_num, 0x2e, 0x0f, tap_id + 7, tap_id + 7, 0x1);
			rtl9300_sds_field_w(sds_num, 0x21, 0x07, 6, 6, tap_list[0]);
			rtl9300_sds_field_w(sds_num, 0x2e, 0x09, 11, 6, tap_list[1]);
			rtl9300_sds_field_w(sds_num, 0x21, 0x07, 5, 5, tap_list[2]);
			rtl9300_sds_field_w(sds_num, 0x2f, 0x12, 5, 0, tap_list[3]);
			break;
		case 2:
			rtl9300_sds_field_w(sds_num, 0x2e, 0x0f, tap_id + 7, tap_id + 7, 0x1);
			rtl9300_sds_field_w(sds_num, 0x2e, 0x09, 5, 5, tap_list[0]);
			rtl9300_sds_field_w(sds_num, 0x2e, 0x09, 4, 0, tap_list[1]);
			rtl9300_sds_field_w(sds_num, 0x2e, 0x0a, 11, 11, tap_list[2]);
			rtl9300_sds_field_w(sds_num, 0x2e, 0x0a, 10, 6, tap_list[3]);
			break;
		case 3:
			rtl9300_sds_field_w(sds_num, 0x2e, 0x0f, tap_id + 7, tap_id + 7, 0x1);
			rtl9300_sds_field_w(sds_num, 0x2e, 0x0a, 5, 5, tap_list[0]);
			rtl9300_sds_field_w(sds_num, 0x2e, 0x0a, 4, 0, tap_list[1]);
			rtl9300_sds_field_w(sds_num, 0x2e, 0x06, 5, 5, tap_list[2]);
			rtl9300_sds_field_w(sds_num, 0x2e, 0x06, 4, 0, tap_list[3]);
			break;
		case 4:
			rtl9300_sds_field_w(sds_num, 0x2e, 0x0f, tap_id + 7, tap_id + 7, 0x1);
			rtl9300_sds_field_w(sds_num, 0x2f, 0x01, 5, 5, tap_list[0]);
			rtl9300_sds_field_w(sds_num, 0x2f, 0x01, 4, 0, tap_list[1]);
			rtl9300_sds_field_w(sds_num, 0x2e, 0x06, 11, 11, tap_list[2]);
			rtl9300_sds_field_w(sds_num, 0x2e, 0x06, 10, 6, tap_list[3]);
			break;
		default:
			break;
		}
	} else {
		rtl9300_sds_field_w(sds_num, 0x2e, 0x0f, tap_id + 7, tap_id + 7, 0x0);
		mdelay(10);
	}
}

void rtl9300_sds_rxcal_tap_get(u32 sds_num, u32 tap_id, u32 tap_list[])
{
	u32 tap0_sign_out;
	u32 tap0_coef_bin;
	u32 tap_sign_out_even;
	u32 tap_coef_bin_even;
	u32 tap_sign_out_odd;
	u32 tap_coef_bin_odd;
	bool tap_manual;

	if (!(sds_num % 2))
		rtl930x_write_sds_phy(sds_num, 0x1f, 0x2, 0x2f);
	else
		rtl930x_write_sds_phy(sds_num - 1, 0x1f, 0x2, 0x31);

	//##Page0x2E, Reg0x15[9], REG0_RX_EN_TEST=[1]
	rtl9300_sds_field_w(sds_num, 0x2e, 0x15, 9, 9, 0x1);
	//##Page0x21, Reg0x06[11 6], REG0_RX_DEBUG_SEL=[1 0 x x x x]
	rtl9300_sds_field_w(sds_num, 0x21, 0x06, 11, 6, 0x20);

	if (!tap_id) {
		//##Page0x2F, Reg0x0C[5 0], REG0_COEF_SEL=[0 0 0 0 0 1]
		rtl9300_sds_field_w(sds_num, 0x2f, 0x0c, 5, 0, 0);
		//##Tap1 Even Read Out
		mdelay(1);
		tap0_sign_out = rtl9300_sds_field_r(sds_num, 0x1f, 0x14, 5, 5);
		tap0_coef_bin = rtl9300_sds_field_r(sds_num, 0x1f, 0x14, 4, 0);

		if (tap0_sign_out == 1)
			pr_info("Tap0 Sign : -");
		else
			pr_info("Tap0 Sign : +");

		pr_info("tap0_coef_bin = %d", tap0_coef_bin);

		tap_list[0] = tap0_sign_out;
		tap_list[1] = tap0_coef_bin;

		tap_manual = !!rtl9300_sds_field_r(sds_num, 0x2e, 0x0f, 7, 7);
		pr_info("tap0 manual = %u",tap_manual);
	} else {
		//##Page0x2F, Reg0x0C[5 0], REG0_COEF_SEL=[0 0 0 0 0 1]
		rtl9300_sds_field_w(sds_num, 0x2f, 0x0c, 5, 0, tap_id);
		mdelay(1);
		//##Tap1 Even Read Out
		tap_sign_out_even = rtl9300_sds_field_r(sds_num, 0x1f, 0x14, 5, 5);
		tap_coef_bin_even = rtl9300_sds_field_r(sds_num, 0x1f, 0x14, 4, 0);

		//##Page0x2F, Reg0x0C[5 0], REG0_COEF_SEL=[0 0 0 1 1 0]
		rtl9300_sds_field_w(sds_num, 0x2f, 0x0c, 5, 0, (tap_id + 5));
		//##Tap1 Odd Read Out
		tap_sign_out_odd = rtl9300_sds_field_r(sds_num, 0x1f, 0x14, 5, 5);
		tap_coef_bin_odd = rtl9300_sds_field_r(sds_num, 0x1f, 0x14, 4, 0);

		if (tap_sign_out_even == 1)
			pr_info("Tap %u even sign: -", tap_id);
		else
			pr_info("Tap %u even sign: +", tap_id);

		pr_info("Tap %u even coefficient = %u", tap_id, tap_coef_bin_even);

		if (tap_sign_out_odd == 1)
			pr_info("Tap %u odd sign: -", tap_id);
		else
			pr_info("Tap %u odd sign: +", tap_id);

		pr_info("Tap %u odd coefficient = %u", tap_id,tap_coef_bin_odd);

		tap_list[0] = tap_sign_out_even;
		tap_list[1] = tap_coef_bin_even;
		tap_list[2] = tap_sign_out_odd;
		tap_list[3] = tap_coef_bin_odd;

		tap_manual = rtl9300_sds_field_r(sds_num, 0x2e, 0x0f, tap_id + 7, tap_id + 7);
		pr_info("tap %u manual = %d",tap_id, tap_manual);
	}
}

void rtl9300_do_rx_calibration_1(int sds, phy_interface_t phy_mode)
{
	// From both rtl9300_rxCaliConf_serdes_myParam and rtl9300_rxCaliConf_phy_myParam
	int tap0_init_val       = 0x1f; // Initial Decision Fed Equalizer 0 tap
	int vth_min             = 0x0;

	pr_debug("Doing calibration step 1.1.x for sds %d\n", sds);
	rtl930x_write_sds_phy(sds, 6,  0, 0);

	// FGCAL
	rtl9300_sds_field_w(sds, 0x2e, 0x01, 14, 14, 0x0);
	rtl9300_sds_field_w(sds, 0x2e, 0x1c, 10, 5, 0x20);
	rtl9300_sds_field_w(sds, 0x2f, 0x02, 0, 0, 0x1);

	// DCVS
	rtl9300_sds_field_w(sds, 0x2e, 0x1e, 14, 11, 0x0);
	rtl9300_sds_field_w(sds, 0x2e, 0x01, 15, 15, 0x0);
	rtl9300_sds_field_w(sds, 0x2e, 0x02, 11, 11, 0x0);
	rtl9300_sds_field_w(sds, 0x2e, 0x1c, 4, 0, 0x0);
	rtl9300_sds_field_w(sds, 0x2e, 0x1d, 15, 11, 0x0);
	rtl9300_sds_field_w(sds, 0x2e, 0x1d, 10, 6, 0x0);
	rtl9300_sds_field_w(sds, 0x2e, 0x1d, 5, 1, 0x0);
	rtl9300_sds_field_w(sds, 0x2e, 0x02, 10, 6, 0x0);
	rtl9300_sds_field_w(sds, 0x2e, 0x11, 4, 0, 0x0);
	rtl9300_sds_field_w(sds, 0x2f, 0x00, 3, 0, 0xf);
	rtl9300_sds_field_w(sds, 0x2e, 0x04, 6, 6, 0x1);
	rtl9300_sds_field_w(sds, 0x2e, 0x04, 7, 7, 0x1);

	// LEQ (Long Term Equivalent signal level)
	rtl9300_sds_field_w(sds, 0x2e, 0x16, 14, 8, 0x0);

	// DFE (Decision Fed Equalizer)
	rtl9300_sds_field_w(sds, 0x2f, 0x03, 5, 0, tap0_init_val);
	rtl9300_sds_field_w(sds, 0x2e, 0x09, 11, 6, 0x0);
	rtl9300_sds_field_w(sds, 0x2e, 0x09, 5, 0, 0x0);
	rtl9300_sds_field_w(sds, 0x2e, 0x0a, 5, 0, 0x0);
	rtl9300_sds_field_w(sds, 0x2f, 0x01, 5, 0, 0x0);
	rtl9300_sds_field_w(sds, 0x2f, 0x12, 5, 0, 0x0);
	rtl9300_sds_field_w(sds, 0x2e, 0x0a, 11, 6, 0x0);
	rtl9300_sds_field_w(sds, 0x2e, 0x06, 5, 0, 0x0);
	rtl9300_sds_field_w(sds, 0x2f, 0x01, 5, 0, 0x0);

	// Vth
	rtl9300_sds_field_w(sds, 0x2e, 0x13, 5, 3, 0x7);
	rtl9300_sds_field_w(sds, 0x2e, 0x13, 2, 0, 0x7);
	rtl9300_sds_field_w(sds, 0x2f, 0x0b, 5, 3, vth_min);

	rtl9300_sds_field_w(sds, 0x2e, 0x0f, 13, 7, 0x7f);

	rtl9300_sds_field_w(sds, 0x2e, 0x17, 7, 7, 0x0);
	rtl9300_sds_field_w(sds, 0x2e, 0x17, 6, 2, 0x0);
	rtl9300_sds_field_w(sds, 0x2e, 0x0c, 8, 8, 0x0);
	rtl9300_sds_field_w(sds, 0x2e, 0x0b, 4, 4, 0x1);
	rtl9300_sds_field_w(sds, 0x2e, 0x12, 14, 14, 0x0);
	rtl9300_sds_field_w(sds, 0x2f, 0x02, 15, 15, 0x0);

	rtl9300_sds_field_w(sds, 0x2e, 0x0f, 15, 14, 0x3);

	// TODO: make this work for DAC cables of different lengths
	// For a 10GBit serdes wit Fibre, SDS 8 or 9
	if (phy_mode == PHY_INTERFACE_MODE_10GBASER || PHY_INTERFACE_MODE_1000BASEX)
		rtl9300_sds_field_w(sds, 0x2e, 0x16, 3, 2, 0x2);
	else
		pr_err("%s not PHY-based or SerDes, implement DAC!\n", __func__);

	// No serdes, check for Aquantia PHYs
	rtl9300_sds_field_w(sds, 0x2e, 0x16, 3, 2, 0x2);

	rtl9300_sds_field_w(sds, 0x2e, 0x0f, 6, 0, 0x5f);
	rtl9300_sds_field_w(sds, 0x2f, 0x05, 7, 2, 0x1f);
	rtl9300_sds_field_w(sds, 0x2e, 0x19, 9, 5, 0x1f);
	rtl9300_sds_field_w(sds, 0x2f, 0x0b, 15, 9, 0x3c);
	rtl9300_sds_field_w(sds, 0x2e, 0x0b, 1, 0, 0x3);
}

void rtl9300_do_rx_calibration_2_1(u32 sds_num)
{
	pr_info("start_1.2.1 ForegroundOffsetCal_Manual\n");

	// Gray config endis to 1
	rtl9300_sds_field_w(sds_num, 0x2f, 0x02, 2, 2, 0x1);

	// ForegroundOffsetCal_Manual(auto mode)
	rtl9300_sds_field_w(sds_num, 0x2e, 0x01, 14, 14, 0x0);

	pr_info("end_1.2.1");
}

void rtl9300_do_rx_calibration_2_2(int sds_num)
{
	//Force Rx-Run = 0
	rtl9300_sds_field_w(sds_num, 0x2e, 0x15, 8, 8, 0x0);

	rtl930x_sds_rx_rst(sds_num, PHY_INTERFACE_MODE_10GBASER);
}

void rtl9300_do_rx_calibration_2_3(int sds_num)
{
	u32 fgcal_binary, fgcal_gray;
	u32 offset_range;

	pr_info("start_1.2.3 Foreground Calibration\n");

	while(1) {
		if (!(sds_num % 2))
			rtl930x_write_sds_phy(sds_num, 0x1f, 0x2, 0x2f);
		else
			rtl930x_write_sds_phy(sds_num -1 , 0x1f, 0x2, 0x31);

		// ##Page0x2E, Reg0x15[9], REG0_RX_EN_TEST=[1]
		rtl9300_sds_field_w(sds_num, 0x2e, 0x15, 9, 9, 0x1);
		// ##Page0x21, Reg0x06[11 6], REG0_RX_DEBUG_SEL=[1 0 x x x x]
		rtl9300_sds_field_w(sds_num, 0x21, 0x06, 11, 6, 0x20);
		// ##Page0x2F, Reg0x0C[5 0], REG0_COEF_SEL=[0 0 1 1 1 1]
		rtl9300_sds_field_w(sds_num, 0x2f, 0x0c, 5, 0, 0xf);
		// ##FGCAL read gray
		fgcal_gray = rtl9300_sds_field_r(sds_num, 0x1f, 0x14, 5, 0);
		// ##Page0x2F, Reg0x0C[5 0], REG0_COEF_SEL=[0 0 1 1 1 0]
		rtl9300_sds_field_w(sds_num, 0x2f, 0x0c, 5, 0, 0xe);
		// ##FGCAL read binary
		fgcal_binary = rtl9300_sds_field_r(sds_num, 0x1f, 0x14, 5, 0);

		pr_info("%s: fgcal_gray: %d, fgcal_binary %d\n",
			__func__, fgcal_gray, fgcal_binary);

		offset_range = rtl9300_sds_field_r(sds_num, 0x2e, 0x15, 15, 14);

		if (fgcal_binary > 60 || fgcal_binary < 3) {
			if (offset_range == 3) {
				pr_info("%s: Foreground Calibration result marginal!", __func__);
				break;
			} else {
				offset_range++;
				rtl9300_sds_field_w(sds_num, 0x2e, 0x15, 15, 14, offset_range);
				rtl9300_do_rx_calibration_2_2(sds_num);
			}
		} else {
			break;
		}
	}
	pr_info("%s: end_1.2.3\n", __func__);
}

void rtl9300_do_rx_calibration_2(int sds)
{
	rtl930x_sds_rx_rst(sds, PHY_INTERFACE_MODE_10GBASER);
	rtl9300_do_rx_calibration_2_1(sds);
	rtl9300_do_rx_calibration_2_2(sds);
	rtl9300_do_rx_calibration_2_3(sds);
}

void rtl9300_sds_rxcal_3_1(int sds_num, phy_interface_t phy_mode)
{
	pr_info("start_1.3.1");

	// ##1.3.1
	if (phy_mode != PHY_INTERFACE_MODE_10GBASER && phy_mode != PHY_INTERFACE_MODE_1000BASEX)
		rtl9300_sds_field_w(sds_num, 0x2e, 0xc, 8, 8, 0);

	rtl9300_sds_field_w(sds_num, 0x2e, 0x17, 7, 7, 0x0);
	rtl9300_sds_rxcal_leq_manual(sds_num, false, 0);

	pr_info("end_1.3.1");
}

void rtl9300_sds_rxcal_3_2(int sds_num, phy_interface_t phy_mode)
{
	u32 sum10 = 0, avg10, int10;
	int dac_long_cable_offset;
	bool eq_hold_enabled;
	int i;

	if (phy_mode == PHY_INTERFACE_MODE_10GBASER || phy_mode == PHY_INTERFACE_MODE_1000BASEX) {
		// rtl9300_rxCaliConf_serdes_myParam
		dac_long_cable_offset = 3;
		eq_hold_enabled = true;
	} else {
		// rtl9300_rxCaliConf_phy_myParam
		dac_long_cable_offset = 0;
		eq_hold_enabled = false;
	}

	if (phy_mode == PHY_INTERFACE_MODE_1000BASEX)
		pr_warn("%s: LEQ only valid for 10GR!\n", __func__);

	pr_info("start_1.3.2");

	for(i = 0; i < 10; i++) {
		sum10 += rtl9300_sds_rxcal_leq_read(sds_num);
		mdelay(10);
	}

	avg10 = (sum10 / 10) + (((sum10 % 10) >= 5) ? 1 : 0);
	int10 = sum10 / 10;

	pr_info("sum10:%u, avg10:%u, int10:%u", sum10, avg10, int10);

	if (phy_mode == PHY_INTERFACE_MODE_10GBASER || phy_mode == PHY_INTERFACE_MODE_1000BASEX) {
		if (dac_long_cable_offset) {
			rtl9300_sds_rxcal_leq_offset_manual(sds_num, 1, dac_long_cable_offset);
			rtl9300_sds_field_w(sds_num, 0x2e, 0x17, 7, 7, eq_hold_enabled);
			if (phy_mode == PHY_INTERFACE_MODE_10GBASER)
				rtl9300_sds_rxcal_leq_manual(sds_num, true, avg10);
		} else {
			if (sum10 >= 5) {
				rtl9300_sds_rxcal_leq_offset_manual(sds_num, 1, 3);
				rtl9300_sds_field_w(sds_num, 0x2e, 0x17, 7, 7, 0x1);
				if (phy_mode == PHY_INTERFACE_MODE_10GBASER)
					rtl9300_sds_rxcal_leq_manual(sds_num, true, avg10);
			} else {
				rtl9300_sds_rxcal_leq_offset_manual(sds_num, 1, 0);
				rtl9300_sds_field_w(sds_num, 0x2e, 0x17, 7, 7, 0x1);
				if (phy_mode == PHY_INTERFACE_MODE_10GBASER)
					rtl9300_sds_rxcal_leq_manual(sds_num, true, avg10);
			}
		}
	}

	pr_info("Sds:%u LEQ = %u",sds_num, rtl9300_sds_rxcal_leq_read(sds_num));

	pr_info("end_1.3.2");
}

void rtl9300_do_rx_calibration_3(int sds_num, phy_interface_t phy_mode)
{
	rtl9300_sds_rxcal_3_1(sds_num, phy_mode);

	if (phy_mode == PHY_INTERFACE_MODE_10GBASER || phy_mode == PHY_INTERFACE_MODE_1000BASEX)
		rtl9300_sds_rxcal_3_2(sds_num, phy_mode);
}

void rtl9300_do_rx_calibration_4_1(int sds_num)
{
	u32 vth_list[2] = {0, 0};
	u32 tap0_list[4] = {0, 0, 0, 0};

	pr_info("start_1.4.1");

	// ##1.4.1
	rtl9300_sds_rxcal_vth_manual(sds_num, false, vth_list);
	rtl9300_sds_rxcal_tap_manual(sds_num, 0, false, tap0_list);
	mdelay(200);

	pr_info("end_1.4.1");
}

void rtl9300_do_rx_calibration_4_2(u32 sds_num)
{
	u32 vth_list[2];
	u32 tap_list[4];

	pr_info("start_1.4.2");

	rtl9300_sds_rxcal_vth_get(sds_num, vth_list);
	rtl9300_sds_rxcal_vth_manual(sds_num, true, vth_list);

	mdelay(100);

	rtl9300_sds_rxcal_tap_get(sds_num, 0, tap_list);
	rtl9300_sds_rxcal_tap_manual(sds_num, 0, true, tap_list);

	pr_info("end_1.4.2");
}

void rtl9300_do_rx_calibration_4(u32 sds_num)
{
	rtl9300_do_rx_calibration_4_1(sds_num);
	rtl9300_do_rx_calibration_4_2(sds_num);
}

void rtl9300_do_rx_calibration_5_2(u32 sds_num)
{
	u32 tap1_list[4] = {0};
	u32 tap2_list[4] = {0};
	u32 tap3_list[4] = {0};
	u32 tap4_list[4] = {0};

	pr_info("start_1.5.2");

	rtl9300_sds_rxcal_tap_manual(sds_num, 1, false, tap1_list);
	rtl9300_sds_rxcal_tap_manual(sds_num, 2, false, tap2_list);
	rtl9300_sds_rxcal_tap_manual(sds_num, 3, false, tap3_list);
	rtl9300_sds_rxcal_tap_manual(sds_num, 4, false, tap4_list);

	mdelay(30);

	pr_info("end_1.5.2");
}

void rtl9300_do_rx_calibration_5(u32 sds_num, phy_interface_t phy_mode)
{
	if (phy_mode == PHY_INTERFACE_MODE_10GBASER) //   true
		rtl9300_do_rx_calibration_5_2(sds_num);
}


void rtl9300_do_rx_calibration_dfe_disable(u32 sds_num)
{
	u32 tap1_list[4] = {0};
	u32 tap2_list[4] = {0};
	u32 tap3_list[4] = {0};
	u32 tap4_list[4] = {0};

	rtl9300_sds_rxcal_tap_manual(sds_num, 1, true, tap1_list);
	rtl9300_sds_rxcal_tap_manual(sds_num, 2, true, tap2_list);
	rtl9300_sds_rxcal_tap_manual(sds_num, 3, true, tap3_list);
	rtl9300_sds_rxcal_tap_manual(sds_num, 4, true, tap4_list);

	mdelay(10);
}

void rtl9300_do_rx_calibration(int sds, phy_interface_t phy_mode)
{
	u32 latch_sts;

	rtl9300_do_rx_calibration_1(sds, phy_mode);
	rtl9300_do_rx_calibration_2(sds);
	rtl9300_do_rx_calibration_4(sds);
	rtl9300_do_rx_calibration_5(sds, phy_mode);
	mdelay(20);

	// Do this only for 10GR mode, SDS active in mode 0x1a
	if (rtl9300_sds_field_r(sds, 0x1f, 9, 11, 7) == 0x1a) {
		pr_info("%s: SDS enabled\n", __func__);
		latch_sts = rtl9300_sds_field_r(sds, 0x4, 1, 2, 2);
		mdelay(1);
		latch_sts = rtl9300_sds_field_r(sds, 0x4, 1, 2, 2);
		if (latch_sts) {
			rtl9300_do_rx_calibration_dfe_disable(sds);
			rtl9300_do_rx_calibration_4(sds);
			rtl9300_do_rx_calibration_5(sds, phy_mode);
		}
	}
}

int rtl9300_sds_sym_err_reset(int sds_num, phy_interface_t phy_mode)
{
	switch (phy_mode) {
	case PHY_INTERFACE_MODE_XGMII:
		break;

	case PHY_INTERFACE_MODE_10GBASER:
		// Read twice to clear
		rtl930x_read_sds_phy(sds_num, 5, 1);
		rtl930x_read_sds_phy(sds_num, 5, 1);
		break;

	case PHY_INTERFACE_MODE_1000BASEX:
		rtl9300_sds_field_w(sds_num, 0x1, 24, 2, 0, 0);
		rtl9300_sds_field_w(sds_num, 0x1, 3, 15, 8, 0);
		rtl9300_sds_field_w(sds_num, 0x1, 2, 15, 0, 0);
		break;

	default:
		pr_info("%s unsupported phy mode\n", __func__);
		return -1;
	}

	return 0;
}

u32 rtl9300_sds_sym_err_get(int sds_num, phy_interface_t phy_mode)
{
	u32 v = 0;

	switch (phy_mode) {
	case PHY_INTERFACE_MODE_XGMII:
		break;

	case PHY_INTERFACE_MODE_10GBASER:
		v = rtl930x_read_sds_phy(sds_num, 5, 1);
		return v & 0xff;

	default:
		pr_info("%s unsupported PHY-mode\n", __func__);
	}

	return v;
}

int rtl9300_sds_check_calibration(int sds_num, phy_interface_t phy_mode)
{
	u32 errors1, errors2;

	rtl9300_sds_sym_err_reset(sds_num, phy_mode);
	rtl9300_sds_sym_err_reset(sds_num, phy_mode);

	// Count errors during 1ms
	errors1 = rtl9300_sds_sym_err_get(sds_num, phy_mode);
	mdelay(1);
	errors2 = rtl9300_sds_sym_err_get(sds_num, phy_mode);

	switch (phy_mode) {
		case PHY_INTERFACE_MODE_XGMII:

			if ((errors2 - errors1 > 100)
			    || (errors1 >= 0xffff00) || (errors2 >= 0xffff00)) {
				pr_info("%s XSGMII error rate too high\n", __func__);
				return 1;
			}
			break;
		case PHY_INTERFACE_MODE_10GBASER:
			if (errors2 > 0) {
				pr_info("%s 10GBASER error rate too high\n", __func__);
				return 1;
			}
			break;
		default:
			return 1;
	}
	return 0;
}

void rtl9300_phy_enable_10g_1g(int sds_num)
{
	u32 v;

	// Enable 1GBit PHY
	v = rtl930x_read_sds_phy(sds_num, PHY_PAGE_2, PHY_CTRL_REG);
	pr_info("%s 1gbit phy: %08x\n", __func__, v);
	v &= ~BIT(PHY_POWER_BIT);
	rtl930x_write_sds_phy(sds_num, PHY_PAGE_2, PHY_CTRL_REG, v);
	pr_info("%s 1gbit phy enabled: %08x\n", __func__, v);

	// Enable 10GBit PHY
	v = rtl930x_read_sds_phy(sds_num, PHY_PAGE_4, PHY_CTRL_REG);
	pr_info("%s 10gbit phy: %08x\n", __func__, v);
	v &= ~BIT(PHY_POWER_BIT);
	rtl930x_write_sds_phy(sds_num, PHY_PAGE_4, PHY_CTRL_REG, v);
	pr_info("%s 10gbit phy after: %08x\n", __func__, v);

	// dal_longan_construct_mac_default_10gmedia_fiber
	v = rtl930x_read_sds_phy(sds_num, 0x1f, 11);
	pr_info("%s set medium: %08x\n", __func__, v);
	v |= BIT(1);
	rtl930x_write_sds_phy(sds_num, 0x1f, 11, v);
	pr_info("%s set medium after: %08x\n", __func__, v);
}

int rtl9300_sds_10g_idle(int sds_num)
{
	bool busy;
	int i = 0;

	do {
		if (sds_num % 2) {
			rtl9300_sds_field_w(sds_num - 1, 0x1f, 0x2, 15, 0, 53);
			busy = !!rtl9300_sds_field_r(sds_num - 1, 0x1f, 0x14, 1, 1);
		} else {
			rtl9300_sds_field_w(sds_num, 0x1f, 0x2, 15, 0, 53);
			busy = !!rtl9300_sds_field_r(sds_num, 0x1f, 0x14, 0, 0);
		}
		i++;
	} while (busy && i < 100);

	if (i < 100)
		return 0;

	pr_warn("%s WARNING: Waiting for RX idle timed out, SDS %d\n", __func__, sds_num);
	return -EIO;
}


#define RTL930X_MAC_FORCE_MODE_CTRL		(0xCA1C)
// phy_mode = PHY_INTERFACE_MODE_10GBASER, sds_mode = 0x1a
int rtl9300_serdes_setup(int sds_num, phy_interface_t phy_mode)
{
	int sds_mode;
	int calib_tries = 0;

	switch (phy_mode) {
	case PHY_INTERFACE_MODE_HSGMII:
		sds_mode = 0x12;
		break;
	case PHY_INTERFACE_MODE_1000BASEX:
		sds_mode = 0x04;
		break;
	case PHY_INTERFACE_MODE_XGMII:
		sds_mode = 0x10;
		break;
	case PHY_INTERFACE_MODE_10GBASER:
		sds_mode = 0x1a;
		break;
	case PHY_INTERFACE_MODE_USXGMII:
		sds_mode = 0x0d;
		break;
	default:
		pr_err("%s: unknown serdes mode: %s\n", __func__, phy_modes(phy_mode));
		return -EINVAL;
	}

	// Maybe use dal_longan_sds_init

	// dal_longan_construct_serdesConfig_init		// Serdes Construct
	rtl9300_phy_enable_10g_1g(sds_num);

	// Set Serdes Mode
	rtl9300_sds_set(sds_num, 0x1a);	 // 0x1b: RTK_MII_10GR1000BX_AUTO

	// Do RX calibration
	rtl9300_sds_10g_idle(sds_num);
	do {
		rtl9300_do_rx_calibration(sds_num, phy_mode);
		calib_tries++;
		mdelay(50);
	} while (rtl9300_sds_check_calibration(sds_num, phy_mode) && calib_tries < 3);


	return 0;
}

typedef struct {
	u8 page;
	u8 reg;
	u16 data;
} sds_config;


sds_config rtl9300_a_sds_xsgmii_lane0[] =
{
	{0x00, 0x0E, 0x3053}, {0x00, 0x02, 0x70d0}, {0x21, 0x03, 0x8206},
	{0x21, 0x05, 0x40B0}, {0x21, 0x06, 0x0010}, {0x21, 0x07, 0xF09F},
	{0x21, 0x0C, 0x0007}, {0x21, 0x0D, 0x6009}, {0x21, 0x0E, 0x0000},
	{0x21, 0x0F, 0x0008}, {0x2E, 0x00, 0xA668}, {0x2E, 0x02, 0xD020},
	{0x2E, 0x06, 0xC000}, {0x2E, 0x0B, 0x1892}, {0x2E, 0x0F, 0xFFDF},
	{0x2E, 0x11, 0x8280}, {0x2E, 0x12, 0x0484}, {0x2E, 0x13, 0x027F},
	{0x2E, 0x14, 0x1311}, {0x2E, 0x17, 0xA100}, {0x2E, 0x1A, 0x0001},
	{0x2E, 0x1C, 0x0400}, {0x2F, 0x01, 0x0300}, {0x2F, 0x02, 0x1017},
	{0x2F, 0x03, 0xFFDF}, {0x2F, 0x05, 0x7F7C}, {0x2F, 0x07, 0x8104},
	{0x2F, 0x08, 0x0001}, {0x2F, 0x09, 0xFFD4}, {0x2F, 0x0A, 0x7C2F},
	{0x2F, 0x0E, 0x003F}, {0x2F, 0x0F, 0x0121}, {0x2F, 0x10, 0x0020},
	{0x2F, 0x11, 0x8840}, {0x2B, 0x13, 0x0050}, {0x2B, 0x18, 0x8E88},
	{0x2B, 0x19, 0x4902}, {0x2B, 0x1D, 0x2501}, {0x2D, 0x13, 0x0050},
	{0x2D, 0x18, 0x8E88}, {0x2D, 0x19, 0x4902}, {0x2D, 0x1D, 0x2641},
	{0x2F, 0x13, 0x0050}, {0x2F, 0x18, 0x8E88}, {0x2F, 0x19, 0x4902},
	{0x2F, 0x1D, 0x66E1},
};

sds_config rtl9300_a_sds_xsgmii_lane1[] =
{
	{0x00, 0x0E, 0x3053}, {0x00, 0x02, 0x70d0}, {0x21, 0x03, 0x8206},
	{0x21, 0x05, 0x40B0}, {0x21, 0x06, 0x0010}, {0x21, 0x07, 0xF09F},
	{0x21, 0x0A, 0x0003}, {0x21, 0x0B, 0x0005}, {0x21, 0x0C, 0x0007},
	{0x21, 0x0D, 0x6009}, {0x21, 0x0E, 0x0000}, {0x21, 0x0F, 0x0008},
	{0x2E, 0x00, 0xA668}, {0x2E, 0x02, 0xD020}, {0x2E, 0x06, 0xC000},
	{0x2E, 0x0B, 0x1892}, {0x2E, 0x0F, 0xFFDF}, {0x2E, 0x11, 0x8280},
	{0x2E, 0x12, 0x0484}, {0x2E, 0x13, 0x027F}, {0x2E, 0x14, 0x1311},
	{0x2E, 0x17, 0xA100}, {0x2E, 0x1A, 0x0001}, {0x2E, 0x1C, 0x0400},
	{0x2F, 0x00, 0x820F}, {0x2F, 0x01, 0x0300}, {0x2F, 0x02, 0x1017},
	{0x2F, 0x03, 0xFFDF}, {0x2F, 0x05, 0x7F7C}, {0x2F, 0x07, 0x8104},
	{0x2F, 0x08, 0x0001}, {0x2F, 0x09, 0xFFD4}, {0x2F, 0x0A, 0x7C2F},
	{0x2F, 0x0E, 0x003F}, {0x2F, 0x0F, 0x0121}, {0x2F, 0x10, 0x0020},
	{0x2F, 0x11, 0x8840}, {0x2B, 0x13, 0x3D87}, {0x2B, 0x14, 0x3108},
	{0x2D, 0x13, 0x3C87}, {0x2D, 0x14, 0x1808},
};

/*
 * Configuration parameters for Even Serdes in XSGMII mode in combination with an RTL8218D
 * PHY as found in a Zyxel XGS1210/XGS1250
 */
sds_config rtl9300_a_sds_xsgmii_rtl8218d_lane0[] =
{
	{0x00, 0x0E, 0x3053}, {0x00, 0x02, 0x71d0}, {0x21, 0x03, 0x8206},
	{0x21, 0x05, 0x40B0}, {0x21, 0x06, 0x0010}, {0x21, 0x07, 0xF09F},
	{0x21, 0x0C, 0x0007}, {0x21, 0x0D, 0x6009}, {0x21, 0x0E, 0x0000},
	{0x21, 0x0F, 0x0008}, {0x2E, 0x00, 0xA668}, {0x2E, 0x02, 0xD020},
	{0x2E, 0x06, 0xC000}, {0x2E, 0x0B, 0x1892}, {0x2E, 0x0F, 0xFFDF},
	{0x2E, 0x11, 0x8280}, {0x2E, 0x12, 0x0484}, {0x2E, 0x13, 0x027F},
	{0x2E, 0x14, 0x1311}, {0x2E, 0x17, 0xA100}, {0x2E, 0x1A, 0x0001},
	{0x2E, 0x1C, 0x0400}, {0x2F, 0x01, 0x0300}, {0x2F, 0x02, 0x1017},
	{0x2F, 0x03, 0xFFDF}, {0x2F, 0x05, 0x7F7C}, {0x2F, 0x07, 0x8104},
	{0x2F, 0x08, 0x0001}, {0x2F, 0x09, 0xFFD4}, {0x2F, 0x0A, 0x7C2F},
	{0x2F, 0x0E, 0x003F}, {0x2F, 0x0F, 0x0121}, {0x2F, 0x10, 0x0020},
	{0x2F, 0x11, 0x8840}, {0x2B, 0x13, 0x0050}, {0x2B, 0x18, 0x8E88},
	{0x2B, 0x19, 0x4902}, {0x2B, 0x1D, 0x2501}, {0x2D, 0x13, 0x0050},
	{0x2D, 0x18, 0x8E88}, {0x2D, 0x19, 0x4902}, {0x2D, 0x1D, 0x2641},
	{0x2F, 0x13, 0x0050}, {0x2F, 0x18, 0x8E88}, {0x2F, 0x19, 0x4902},
	{0x2F, 0x1D, 0x66E1},
};

/*
 * Configuration parameters for odd Serdes in XSGMII mode in combination with an RTL8218D
 * PHY as found in a Zyxel XGS1210/XGS1250
 */
sds_config rtl9300_a_sds_xsgmii_rtl8218d_lane1[] =
{
	{0x00, 0x0E, 0x3053}, {0x00, 0x02, 0x71d0}, {0x21, 0x03, 0x8206},
	{0x21, 0x05, 0x40B0}, {0x21, 0x06, 0x0010}, {0x21, 0x07, 0xF09F},
	{0x21, 0x0A, 0x0003}, {0x21, 0x0B, 0x0005}, {0x21, 0x0C, 0x0007},
	{0x21, 0x0D, 0x6009}, {0x21, 0x0E, 0x0000}, {0x21, 0x0F, 0x0008},
	{0x2E, 0x00, 0xA668}, {0x2E, 0x02, 0xD020}, {0x2E, 0x06, 0xC000},
	{0x2E, 0x0B, 0x1892}, {0x2E, 0x0F, 0xFFDF}, {0x2E, 0x11, 0x8280},
	{0x2E, 0x12, 0x0484}, {0x2E, 0x13, 0x027F}, {0x2E, 0x14, 0x1311},
	{0x2E, 0x17, 0xA100}, {0x2E, 0x1A, 0x0001}, {0x2E, 0x1C, 0x0400},
	{0x2F, 0x00, 0x820F}, {0x2F, 0x01, 0x0300}, {0x2F, 0x02, 0x1017},
	{0x2F, 0x03, 0xFFDF}, {0x2F, 0x05, 0x7F7C}, {0x2F, 0x07, 0x8104},
	{0x2F, 0x08, 0x0001}, {0x2F, 0x09, 0xFFD4}, {0x2F, 0x0A, 0x7C2F},
	{0x2F, 0x0E, 0x003F}, {0x2F, 0x0F, 0x0121}, {0x2F, 0x10, 0x0020},
	{0x2F, 0x11, 0x8840}, {0x2B, 0x13, 0x3D87}, {0x2B, 0x14, 0x3108},
	{0x2D, 0x13, 0x3C87}, {0x2D, 0x14, 0x1808},
};

/*
 * Configuration parameters for Even Serdes in 10GR mode without additional PHY
 */
sds_config rtl9300_a_sds_10gr_lane0[] =
{
	/*1G*/
	{0x00, 0x0E, 0x3053}, {0x01, 0x14, 0x0100}, {0x21, 0x03, 0x8206},
	{0x21, 0x05, 0x40B0}, {0x21, 0x06, 0x0010}, {0x21, 0x07, 0xF09F},
	{0x21, 0x0C, 0x0007}, {0x21, 0x0D, 0x6009}, {0x21, 0x0E, 0x0000},
	{0x21, 0x0F, 0x0008}, {0x24, 0x00, 0x0668}, {0x24, 0x02, 0xD020},
	{0x24, 0x06, 0xC000}, {0x24, 0x0B, 0x1892}, {0x24, 0x0F, 0xFFDF},
	{0x24, 0x12, 0x03C4}, {0x24, 0x13, 0x027F}, {0x24, 0x14, 0x1311},
	{0x24, 0x16, 0x00C9}, {0x24, 0x17, 0xA100}, {0x24, 0x1A, 0x0001},
	{0x24, 0x1C, 0x0400}, {0x25, 0x01, 0x0300}, {0x25, 0x02, 0x1017},
	{0x25, 0x03, 0xFFDF}, {0x25, 0x05, 0x7F7C}, {0x25, 0x07, 0x8100},
	{0x25, 0x08, 0x0001}, {0x25, 0x09, 0xFFD4}, {0x25, 0x0A, 0x7C2F},
	{0x25, 0x0E, 0x003F}, {0x25, 0x0F, 0x0121}, {0x25, 0x10, 0x0020},
	{0x25, 0x11, 0x8840}, {0x2B, 0x13, 0x0050}, {0x2B, 0x18, 0x8E88},
	{0x2B, 0x19, 0x4902}, {0x2B, 0x1D, 0x2501}, {0x2D, 0x13, 0x0050},
	{0x2D, 0x18, 0x8E88}, {0x2D, 0x19, 0x4902}, {0x2D, 0x1D, 0x2641},
	{0x2F, 0x13, 0x0050}, {0x2F, 0x18, 0x8E88}, {0x2F, 0x19, 0x4902},
	{0x2F, 0x1D, 0x66E1},
	/*3.125G*/
	{0x28, 0x00, 0x0668}, {0x28, 0x02, 0xD020}, {0x28, 0x06, 0xC000},
	{0x28, 0x0B, 0x1892}, {0x28, 0x0F, 0xFFDF}, {0x28, 0x12, 0x01C4},
	{0x28, 0x13, 0x027F}, {0x28, 0x14, 0x1311}, {0x28, 0x16, 0x00C9},
	{0x28, 0x17, 0xA100}, {0x28, 0x1A, 0x0001}, {0x28, 0x1C, 0x0400},
	{0x29, 0x01, 0x0300}, {0x29, 0x02, 0x1017}, {0x29, 0x03, 0xFFDF},
	{0x29, 0x05, 0x7F7C}, {0x29, 0x07, 0x8100}, {0x29, 0x08, 0x0001},
	{0x29, 0x09, 0xFFD4}, {0x29, 0x0A, 0x7C2F}, {0x29, 0x0E, 0x003F},
	{0x29, 0x0F, 0x0121}, {0x29, 0x10, 0x0020}, {0x29, 0x11, 0x8840},
	/*10G*/
	{0x06, 0x0D, 0x0F00}, {0x06, 0x00, 0x0000}, {0x06, 0x01, 0xC800},
	{0x21, 0x03, 0x8206}, {0x21, 0x05, 0x40B0}, {0x21, 0x06, 0x0010},
	{0x21, 0x07, 0xF09F}, {0x21, 0x0C, 0x0007}, {0x21, 0x0D, 0x6009},
	{0x21, 0x0E, 0x0000}, {0x21, 0x0F, 0x0008}, {0x2E, 0x00, 0xA668},
	{0x2E, 0x02, 0xD020}, {0x2E, 0x06, 0xC000}, {0x2E, 0x0B, 0x1892},
	{0x2E, 0x0F, 0xFFDF}, {0x2E, 0x11, 0x8280}, {0x2E, 0x12, 0x0044},
	{0x2E, 0x13, 0x027F}, {0x2E, 0x14, 0x1311}, {0x2E, 0x17, 0xA100},
	{0x2E, 0x1A, 0x0001}, {0x2E, 0x1C, 0x0400}, {0x2F, 0x01, 0x0300},
	{0x2F, 0x02, 0x1217}, {0x2F, 0x03, 0xFFDF}, {0x2F, 0x05, 0x7F7C},
	{0x2F, 0x07, 0x80C4}, {0x2F, 0x08, 0x0001}, {0x2F, 0x09, 0xFFD4},
	{0x2F, 0x0A, 0x7C2F}, {0x2F, 0x0E, 0x003F}, {0x2F, 0x0F, 0x0121},
	{0x2F, 0x10, 0x0020}, {0x2F, 0x11, 0x8840}, {0x2F, 0x14, 0xE008},
	{0x2B, 0x13, 0x0050}, {0x2B, 0x18, 0x8E88}, {0x2B, 0x19, 0x4902},
	{0x2B, 0x1D, 0x2501}, {0x2D, 0x13, 0x0050}, {0x2D, 0x17, 0x4109},
	{0x2D, 0x18, 0x8E88}, {0x2D, 0x19, 0x4902}, {0x2D, 0x1C, 0x1109},
	{0x2D, 0x1D, 0x2641}, {0x2F, 0x13, 0x0050}, {0x2F, 0x18, 0x8E88},
	{0x2F, 0x19, 0x4902}, {0x2F, 0x1D, 0x76E1},
};

/*
 * Configuration parameters for odd Serdes in 10GR mode without additional PHY
 */
sds_config rtl9300_a_sds_10gr_lane1[] =
{
	/*1G*/
	{0x00, 0x0E, 0x3053}, {0x01, 0x14, 0x0100}, {0x21, 0x03, 0x8206},
	{0x21, 0x06, 0x0010}, {0x21, 0x07, 0xF09F}, {0x21, 0x0A, 0x0003},
	{0x21, 0x0B, 0x0005}, {0x21, 0x0C, 0x0007}, {0x21, 0x0D, 0x6009},
	{0x21, 0x0E, 0x0000}, {0x21, 0x0F, 0x0008}, {0x24, 0x00, 0x0668},
	{0x24, 0x02, 0xD020}, {0x24, 0x06, 0xC000}, {0x24, 0x0B, 0x1892},
	{0x24, 0x0F, 0xFFDF}, {0x24, 0x12, 0x03C4}, {0x24, 0x13, 0x027F},
	{0x24, 0x14, 0x1311}, {0x24, 0x16, 0x00C9}, {0x24, 0x17, 0xA100},
	{0x24, 0x1A, 0x0001}, {0x24, 0x1C, 0x0400}, {0x25, 0x00, 0x820F},
	{0x25, 0x01, 0x0300}, {0x25, 0x02, 0x1017}, {0x25, 0x03, 0xFFDF},
	{0x25, 0x05, 0x7F7C}, {0x25, 0x07, 0x8100}, {0x25, 0x08, 0x0001},
	{0x25, 0x09, 0xFFD4}, {0x25, 0x0A, 0x7C2F}, {0x25, 0x0E, 0x003F},
	{0x25, 0x0F, 0x0121}, {0x25, 0x10, 0x0020}, {0x25, 0x11, 0x8840},
	{0x2B, 0x13, 0x3D87}, {0x2B, 0x14, 0x3108}, {0x2D, 0x13, 0x3C87},
	{0x2D, 0x14, 0x1808},
	/*3.125G*/
	{0x28, 0x00, 0x0668}, {0x28, 0x02, 0xD020}, {0x28, 0x06, 0xC000},
	{0x28, 0x0B, 0x1892}, {0x28, 0x0F, 0xFFDF}, {0x28, 0x12, 0x01C4},
	{0x28, 0x13, 0x027F}, {0x28, 0x14, 0x1311}, {0x28, 0x16, 0x00C9},
	{0x28, 0x17, 0xA100}, {0x28, 0x1A, 0x0001}, {0x28, 0x1C, 0x0400},
	{0x29, 0x00, 0x820F}, {0x29, 0x01, 0x0300}, {0x29, 0x02, 0x1017},
	{0x29, 0x03, 0xFFDF}, {0x29, 0x05, 0x7F7C}, {0x29, 0x07, 0x8100},
	{0x29, 0x08, 0x0001}, {0x29, 0x0A, 0x7C2F}, {0x29, 0x0E, 0x003F},
	{0x29, 0x0F, 0x0121}, {0x29, 0x10, 0x0020}, {0x29, 0x11, 0x8840},
	/*10G*/
	{0x06, 0x0D, 0x0F00}, {0x06, 0x00, 0x0000}, {0x06, 0x01, 0xC800},
	{0x21, 0x03, 0x8206}, {0x21, 0x05, 0x40B0}, {0x21, 0x06, 0x0010},
	{0x21, 0x07, 0xF09F}, {0x21, 0x0A, 0x0003}, {0x21, 0x0B, 0x0005},
	{0x21, 0x0C, 0x0007}, {0x21, 0x0D, 0x6009}, {0x21, 0x0E, 0x0000},
	{0x21, 0x0F, 0x0008}, {0x2E, 0x00, 0xA668}, {0x2E, 0x02, 0xD020},
	{0x2E, 0x06, 0xC000}, {0x2E, 0x0B, 0x1892}, {0x2E, 0x0F, 0xFFDF},
	{0x2E, 0x11, 0x8280}, {0x2E, 0x12, 0x0044}, {0x2E, 0x13, 0x027F},
	{0x2E, 0x14, 0x1311}, {0x2E, 0x17, 0xA100}, {0x2E, 0x1A, 0x0001},
	{0x2E, 0x1C, 0x0400}, {0x2F, 0x00, 0x820F}, {0x2F, 0x01, 0x0300},
	{0x2F, 0x02, 0x1217}, {0x2F, 0x03, 0xFFDF}, {0x2F, 0x05, 0x7F7C},
	{0x2F, 0x07, 0x80C4}, {0x2F, 0x08, 0x0001}, {0x2F, 0x09, 0xFFD4},
	{0x2F, 0x0A, 0x7C2F}, {0x2F, 0x0E, 0x003F}, {0x2F, 0x0F, 0x0121},
	{0x2F, 0x10, 0x0020}, {0x2F, 0x11, 0x8840}, {0x2B, 0x13, 0x3D87},
	{0x2B, 0x14, 0x3108}, {0x2D, 0x13, 0x3C87}, {0x2D, 0x14, 0x1808},
};

// TODO: Add patch for USXGMII on SDS 6-8 for AQR113C

void rtl9300_sds_patch(int sds_num, sds_config c[], int entries)
{	int i;

	for (i = 0; i < entries; ++i)
		rtl930x_write_sds_phy(sds_num, c[i].page, c[i].reg, c[i].data);
}

int rtl9300_sds_cmu_band_get(int sds)
{
	u32 page;
	u32 en;
	u32 cmu_band;

//	page = rtl9300_sds_cmu_page_get(sds);
	page = 0x25; // 10GR and 1000BX
	sds = (sds % 2) ? (sds - 1) : (sds);

	rtl9300_sds_field_w(sds, page, 0x1c, 15, 15, 1);
	rtl9300_sds_field_w(sds + 1, page, 0x1c, 15, 15, 1);

	en = rtl9300_sds_field_r(sds, page, 27, 1, 1);
	if(!en) { // Auto mode
		rtl930x_write_sds_phy(sds, 0x1f, 0x02, 31);

		cmu_band = rtl9300_sds_field_r(sds, 0x1f, 0x15, 5, 1);
	} else {
		cmu_band = rtl9300_sds_field_r(sds, page, 30, 4, 0);
	}

	return cmu_band;
}

static int rtl8218d_serdes_mode_get(struct phy_device *phydev)
{
	int mode;
	u32 block, data;

	block = phy_read_paged(phydev, RTL8XXX_PAGE_MAIN, 30);
	phy_write_paged(phydev, RTL8XXX_PAGE_MAIN, 30, 8);
	data = phy_read_paged(phydev, 0x260, 18);
	phy_write_paged(phydev, RTL8XXX_PAGE_MAIN, 30, block);
	pr_info("%s, reading phy %d got me block %04x and data %04x\n",
		__func__, phydev->mdio.addr, block, data);

	switch (data & 0xf0) {
	case 0xd0:
		mode = PHY_INTERFACE_MODE_QSGMII;
		break;
	case 0xb0:
		mode = PHY_INTERFACE_MODE_XGMII;
		break;
	default:
		pr_err("%s: unknown SDS mode: %x\n", __func__, data & 0xf0);
		return PHY_INTERFACE_MODE_NA;
	}

	pr_info("%s: SDS mode: %x\n", __func__, data & 0xf0);

	return mode;
}

static u32 rtl8218d_serdes_model_get(struct phy_device *phydev)
{
	u32 block, data;

	block = phy_read_paged(phydev, RTL8XXX_PAGE_MAIN, 30);
	phy_write_paged(phydev, RTL8XXX_PAGE_MAIN, 30, 8);
	data = phy_read_paged(phydev, 0x327, 0x15);
	phy_write_paged(phydev, RTL8XXX_PAGE_MAIN, 30, block);

	pr_info("%s: 8281D model: %x\n", __func__, data);
	return data;
}

#define REALTEK_PATCH_PAGE 0xFFFF
typedef struct {
	u8 phy;
	u8 reg;
	u16 data;
} phy_config;

/*
 * RTL9300-specific patches for the RTL828D_NMP for QSGMII
 */
phy_config rtl8218d_bT_qsgmii_rtl9300[] = {
	{0, 0x1f, 0x0000}, {0, 0x1e, 0x0008}, {0, 0x1f, 0x047C}, {0, 0x10, 0x1980},
	{0, 0x1f, 0x0484}, {0, 0x11, 0x0000}, {0, 0x12, 0x7C9F}, {0, 0x1f, 0x0485},
	{0, 0x12, 0x001F}, {0, 0x13, 0x0808}, {0, 0x1f, 0x0486}, {0, 0x10, 0x0010},
	{0, 0x11, 0x07C0}, {0, 0x16, 0x005F}, {0, 0x17, 0x3FBE}, {0, 0x1f, 0x0487},
	{0, 0x10, 0x0000}, {0, 0x1f, 0x04A8}, {0, 0x10, 0x0080}, {0, 0x11, 0x2C27},
	{0, 0x12, 0x0000}, {0, 0x13, 0xE015}, {0, 0x14, 0x0430}, {0, 0x16, 0x0100},
	{0, 0x17, 0x0009}, {0, 0x1f, 0x04A9}, {0, 0x10, 0x7F04}, {0, 0x11, 0xE9E2},
	{0, 0x12, 0xFFFF}, {0, 0x13, 0xA854}, {0, 0x14, 0x3202}, {0, 0x15, 0xFFFD},
	{0, 0x17, 0x8400}, {0, 0x1f, 0x04AA}, {0, 0x10, 0x032C}, {0, 0x11, 0x0120},
	{0, 0x12, 0x0580}, {0, 0x13, 0x2000}, {0, 0x14, 0x228A}, {0, 0x15, 0x7F52},
	{0, 0x16, 0x01C7}, {0, 0x17, 0x808F}, {0, 0x1f, 0x04AB}, {0, 0x10, 0x8813},
	{0, 0x11, 0x8888}, {0, 0x12, 0x8F18}, {0, 0x14, 0x0040}, {0, 0x16, 0x01E0},
	{0, 0x1f, 0x04AC}, {0, 0x11, 0x9F12}, {0, 0x12, 0x00EE}, {0, 0x1f, 0x04AD},
	{0, 0x11, 0x010F}, {0, 0x12, 0x88FF}, {0, 0x13, 0x4208}, {0, 0x1f, 0x04AF},
	{0, 0x14, 0xF0F3}, {0, 0x1f, 0x0400}, {0, 0x10, 0x1700}, {0, 0x10, 0x1703},
	{0, 0x1f, 0x0584}, {0, 0x11, 0x0000}, {0, 0x12, 0x7C9F}, {0, 0x1f, 0x0585},
	{0, 0x12, 0x001F}, {0, 0x13, 0x0808}, {0, 0x1f, 0x0586}, {0, 0x10, 0x0010},
	{0, 0x11, 0x07C0}, {0, 0x16, 0x005F}, {0, 0x17, 0x3FBE}, {0, 0x1f, 0x0587},
	{0, 0x10, 0x0000}, {0, 0x1f, 0x05A8}, {0, 0x10, 0x0080}, {0, 0x11, 0x2C27},
	{0, 0x12, 0x0000}, {0, 0x13, 0xE015}, {0, 0x14, 0x0430}, {0, 0x16, 0x0100},
	{0, 0x17, 0x0009}, {0, 0x1f, 0x05A9}, {0, 0x10, 0x7F04}, {0, 0x11, 0xE9E2},
	{0, 0x12, 0xFFFF}, {0, 0x13, 0xA854}, {0, 0x14, 0x3202}, {0, 0x15, 0xFFFD},
	{0, 0x17, 0x8400}, {0, 0x1f, 0x05AA}, {0, 0x10, 0x032C}, {0, 0x11, 0x0120},
	{0, 0x12, 0x0580}, {0, 0x13, 0x2000}, {0, 0x14, 0x228A}, {0, 0x15, 0x7F52},
	{0, 0x16, 0x01C7}, {0, 0x17, 0x808F}, {0, 0x1f, 0x05AB}, {0, 0x10, 0x8813},
	{0, 0x11, 0x8888}, {0, 0x12, 0x8F18}, {0, 0x14, 0x0040}, {0, 0x16, 0x01E0},
	{0, 0x1f, 0x05AC}, {0, 0x11, 0x9F12}, {0, 0x12, 0x00EE}, {0, 0x1f, 0x05AD},
	{0, 0x11, 0x010F}, {0, 0x12, 0x88FF}, {0, 0x13, 0x4208}, {0, 0x1f, 0x05AF},
	{0, 0x14, 0xF0F3}, {0, 0x1f, 0x0500}, {0, 0x10, 0x1400}, {0, 0x10, 0x1403},
	{0, 0x1f, 0x0000}, {0, 0x1e, 0x0001},
};

/*
 * RTL9300-specific patches for the RTL828D_NMP for XSGMII
 */
phy_config rtl8218d_bT_xsgmii_rtl9300[] = {
	{0, 0x1f, 0x0000}, {0, 0x1e, 0x0008}, {0, 0x1f, 0x047C}, {0, 0x10, 0x1980},
	{0, 0x1f, 0x0484}, {0, 0x11, 0x0000}, {0, 0x12, 0x7C9F}, {0, 0x1f, 0x0485},
	{0, 0x12, 0x001F}, {0, 0x13, 0x0804}, {0, 0x1f, 0x0486}, {0, 0x10, 0x0010},
	{0, 0x11, 0x07C0}, {0, 0x16, 0x005F}, {0, 0x17, 0x3FBE}, {0, 0x1f, 0x0487},
	{0, 0x10, 0x0000}, {0, 0x1f, 0x04B8}, {0, 0x10, 0x0080}, {0, 0x11, 0x2C27},
	{0, 0x12, 0x0100}, {0, 0x13, 0xE015}, {0, 0x14, 0x0430}, {0, 0x16, 0x0100},
	{0, 0x17, 0x0009}, {0, 0x1f, 0x04B9}, {0, 0x10, 0x7F04}, {0, 0x11, 0xE9E2},
	{0, 0x12, 0xFFFF}, {0, 0x13, 0xA854}, {0, 0x14, 0x3A02}, {0, 0x15, 0xFFFD},
	{0, 0x17, 0x8400}, {0, 0x1f, 0x04BA}, {0, 0x10, 0x032C}, {0, 0x11, 0x0120},
	{0, 0x12, 0x0580}, {0, 0x13, 0x2000}, {0, 0x14, 0x228A}, {0, 0x15, 0x7F52},
	{0, 0x16, 0x01C7}, {0, 0x17, 0x808F}, {0, 0x1f, 0x04BB}, {0, 0x10, 0x8813},
	{0, 0x11, 0x8888}, {0, 0x12, 0x8F18}, {0, 0x14, 0x0040}, {0, 0x16, 0x01E0},
	{0, 0x1f, 0x04BC}, {0, 0x10, 0x02C4}, {0, 0x1f, 0x04BD}, {0, 0x11, 0x010F},
	{0, 0x12, 0x88FF}, {0, 0x13, 0x4208}, {0, 0x1f, 0x04BF}, {0, 0x14, 0xF0F3},
	{0, 0x1f, 0x0400}, {0, 0x10, 0x1700}, {0, 0x10, 0x1703}, {0, 0x1f, 0x0000},
	{0, 0x1e, 0x0001},
};

/*
 * RTL9300-specific patches for the RTL828D for QSGMII
 */
phy_config rtl8218d_a_qsgmii_rtl9300[] = {
	{0, 0x1f, 0x0000}, {0, 0x1e, 0x0008}, {0, 0x1f, 0x0401}, {0, 0x16, 0x3053},
	{0, 0x1f, 0x0480}, {0, 0x13, 0x0200}, {0, 0x1f, 0x0485}, {0, 0x13, 0x0808},
	{0, 0x1f, 0x04A8}, {0, 0x11, 0x2C27}, {0, 0x12, 0x0100}, {0, 0x13, 0xE015},
	{0, 0x17, 0x0009}, {0, 0x1f, 0x04A9}, {0, 0x10, 0x7F04}, {0, 0x13, 0xA854},
	{0, 0x14, 0x3202}, {0, 0x1f, 0x04AA}, {0, 0x10, 0x032F}, {0, 0x15, 0x7F52},
	{0, 0x1f, 0x04AB}, {0, 0x16, 0x01E0}, {0, 0x1f, 0x04AC}, {0, 0x15, 0x4380},
	{0, 0x1f, 0x04AD}, {0, 0x10, 0x4321}, {0, 0x11, 0x010F}, {0, 0x12, 0x88FF},
	{0, 0x13, 0x4208}, {0, 0x1f, 0x04AF}, {0, 0x14, 0xF0F3}, {0, 0x15, 0xF2F0},
	{0, 0x1f, 0x0404}, {0, 0x11, 0x000F}, {0, 0x1f, 0x0400}, {0, 0x10, 0x1700},
	{0, 0x10, 0x1703}, {0, 0x1f, 0x0501},
	{0, 0x16, 0x3053}, {0, 0x1f, 0x0580},
	{0, 0x13, 0x0200}, {0, 0x1f, 0x0585}, {0, 0x13, 0x0808}, {0, 0x1f, 0x05A8},
	{0, 0x11, 0x2C27}, {0, 0x12, 0x0100}, {0, 0x13, 0xE015}, {0, 0x17, 0x0009},
	{0, 0x1f, 0x05A9}, {0, 0x10, 0x7F04}, {0, 0x13, 0xA854}, {0, 0x14, 0x3202},
	{0, 0x1f, 0x05AA}, {0, 0x10, 0x032F}, {0, 0x15, 0x7F52}, {0, 0x1f, 0x05AB},
	{0, 0x16, 0x01E0}, {0, 0x1f, 0x05AC}, {0, 0x15, 0x4380}, {0, 0x1f, 0x05AD},
	{0, 0x10, 0x4321}, {0, 0x11, 0x010F}, {0, 0x12, 0x88FF}, {0, 0x13, 0x4208},
	{0, 0x1f, 0x05AF}, {0, 0x14, 0xF0F3}, {0, 0x15, 0xF2F0}, {0, 0x1f, 0x0504},
	{0, 0x11, 0x000F}, {0, 0x1f, 0x0500}, {0, 0x10, 0x1400}, {0, 0x10, 0x1403},
	{0, 0x1f, 0x0000}, {0, 0x1e, 0x0001},
};

/*
 * RTL9300-specific patches for the RTL828D for XSGMII
 */
phy_config rtl8218d_a_xsgmii_rtl9300[] = {
	{0, 0x1f, 0x0000}, {0, 0x1e, 0x0008}, {0, 0x1f, 0x0400}, {0, 0x12, 0x71D0},
	{0, 0x1f, 0x0500}, {0 ,0x12, 0x71D0}, {0, 0x1f, 0x0401}, {0, 0x16, 0x3053},
	{0, 0x1f, 0x0480}, {0, 0x13, 0x0200}, {0, 0x1f, 0x0485}, {0, 0x13, 0x0804},
	{0, 0x1f, 0x04B8}, {0, 0x11, 0x2C27}, {0, 0x12, 0x0100}, {0, 0x13, 0xE015},
	{0, 0x17, 0x000A}, {0, 0x1f, 0x04B9}, {0, 0x10, 0x7F04}, {0, 0x13, 0xA854},
	{0, 0x14, 0x3A02}, {0, 0x1f, 0x04BA}, {0, 0x10, 0x032F}, {0, 0x11, 0x0121},
	{0, 0x15, 0x7E12}, {0, 0x17, 0x808F},
	{0, 0x1f, 0x04BB}, {0, 0x16, 0x01E0}, {0, 0x1f, 0x04BC}, {0, 0x10, 0x02C4},
	{0, 0x1f, 0x04BD}, {0, 0x10, 0x4321}, {0, 0x11, 0x010F}, {0, 0x12, 0x88FF},
	{0, 0x13, 0x4208}, {0, 0x1f, 0x04BF}, {0, 0x14, 0xF0F3}, {0, 0x15, 0xF2F0},
	{0, 0x1f, 0x0404}, {0, 0x11, 0x000F}, {0, 0x1f, 0x0486}, {0, 0x10, 0x001F},
	{0, 0x1f, 0x0400}, {0, 0x10, 0x1700}, {0, 0x10, 0x1703}, {0, 0x1f, 0x0000},
	{0, 0x1e, 0x0000},
};

static void rtl9300_phy_patch(struct phy_device *phydev, phy_config c[], int entries)
{
	int i;

	for (i = 0; i < entries; ++i)
		phy_write_paged(phydev + c[i].phy, REALTEK_PATCH_PAGE, c[i].reg, c[i].data);
}

static int rtl9300_rtl821d_phy_setup(struct phy_device *phydev, int phy_mode)
{
	u32 model;

	rtl8218d_serdes_mode_get(phydev);

	model = rtl8218d_serdes_model_get(phydev);

	if (model & BIT(7)) {  // Is RTL821D_NMP?
		switch(phy_mode) {
		case PHY_INTERFACE_MODE_QSGMII:
			rtl9300_phy_patch(phydev, rtl8218d_bT_qsgmii_rtl9300,
					  sizeof (rtl8218d_bT_qsgmii_rtl9300) / sizeof(phy_config));
			break;
		case PHY_INTERFACE_MODE_XGMII:
			rtl9300_phy_patch(phydev, rtl8218d_bT_xsgmii_rtl9300,
					  sizeof (rtl8218d_bT_xsgmii_rtl9300) / sizeof(phy_config));
			break;
		default:
			pr_err("%s: Unsupported PHY mode\n", __func__);
			return -EINVAL;
		}
	} else { // Normal RTL821D
		switch(phy_mode) {
		case PHY_INTERFACE_MODE_QSGMII:
			rtl9300_phy_patch(phydev, rtl8218d_a_qsgmii_rtl9300,
					  sizeof (rtl8218d_a_qsgmii_rtl9300) / sizeof(phy_config));
			break;
		case PHY_INTERFACE_MODE_XGMII:
			rtl9300_phy_patch(phydev, rtl8218d_a_xsgmii_rtl9300,
					  sizeof (rtl8218d_a_xsgmii_rtl9300) / sizeof(phy_config));
			break;
		default:
			pr_err("%s Unsupported PHY mode\n", __func__);
			return -EINVAL;
		}
	}

	return 0;
}

int rtl9300_configure_8218d(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	int phy_addr = phydev->mdio.addr;
	struct device_node *dn;
	u32 sds_num = 0;
	int sds_mode, phy_mode = PHY_INTERFACE_MODE_XGMII, i;
	u64 saved_state;

	if (dev->of_node) {
		dn = dev->of_node;

		if (of_property_read_u32(dn, "sds", &sds_num))
			return 0;  // Not the base address
	} else {
		dev_err(dev, "No DT node.\n");
		return -EINVAL;
	}

	pr_info("%s: A Port %d, SerDes is %d\n", __func__, phy_addr, sds_num);
	sds_mode = 0x10;
	pr_info("%s CMU BAND is %d\n", __func__, rtl9300_sds_cmu_band_get(sds_num));

	// Disable polling for all 8 ports
	saved_state  = disable_polling(phy_addr);
	for (i = 1; i < 8; i++)
		disable_polling(phy_addr + i);

	// Disable MAC
	sw_w32_mask(0, 1, RTL930X_MAC_FORCE_MODE_CTRL + 4 * phy_addr);
	mdelay(20);

	// On the RTL8218D we do not need to change the polarity
	// See dal_longan_construct_macConfig_init

	// Turn Off Serdes
	rtl9300_sds_rst(sds_num, 0x1f);

	pr_info("%s PATCHING SerDes %d\n", __func__, sds_num);

	if (sds_num % 2) {
		rtl9300_sds_patch(sds_num, rtl9300_a_sds_xsgmii_rtl8218d_lane1,
				  sizeof(rtl9300_a_sds_xsgmii_rtl8218d_lane1) / sizeof(sds_config));

		rtl9300_sds_patch(sds_num, rtl9300_a_sds_xsgmii_lane1,
				  sizeof(rtl9300_a_sds_xsgmii_lane1) / sizeof(sds_config));
	} else {
		rtl9300_sds_patch(sds_num, rtl9300_a_sds_xsgmii_rtl8218d_lane0,
				  sizeof(rtl9300_a_sds_xsgmii_rtl8218d_lane0) / sizeof(sds_config));
	/*	rtl9300_sds_patch(sds_num, rtl9300_a_sds_xsgmii_lane0,
				  sizeof(rtl9300_a_sds_xsgmii_lane0) / sizeof(sds_config)); */
	}

	// On the RTL8218D we do not need to call
	// dal_longan_construct_mac_default_10gmedia_fiber because the port is HWP_GE,
	// not HWP_XGE. But do that for the Aquantia and RTL8226 PHYs.
	// ----> dal_longan_sds_mode_set
	pr_info("%s: Configuring RTL9300 SERDES %d, mode %02x\n", __func__, sds_num, sds_mode);

	// Configure PHY from phy_construct_config_init
	rtl9300_rtl821d_phy_setup(phydev, phy_mode);

	// Configure link to MAC
	rtl9300_serdes_mac_link_config(sds_num, true, true);	// MAC Construct

	// Re-enable SDS with new mode
	rtl9300_sds_set(sds_num, sds_mode);

	// Re-Enable MAC
	sw_w32_mask(1, 0, RTL930X_MAC_FORCE_MODE_CTRL + 4 * phy_addr);

	rtl9300_sds_tx_config(sds_num, phy_mode);

	// Re-enable polling
	resume_polling(saved_state);

	// The clock needs only to be configured on the FPGA implementation

	return 0;
}

int rtl8266_wait_ready(struct phy_device *phydev)
{
	int timeout = 100;
	u32 val;

	do {
		val = phy_read_mmd(phydev, MDIO_MMD_VEND2, 0xA420);
		if ((val & 0x3) == 0x3)
			break;
		mdelay(1);
	} while (--timeout);

	if (!timeout) {
		pr_warn("%s PHY at port %d not ready\n", __func__, phydev->mdio.addr);
		return -EIO;
	}

	return 0;
}

/* Configure the RTL8266, note that this is specific for the RTL93xx SoCs
 * We also always enable swapping the meaning of the MDI pins, since
 * this is the configuration found on the only known device, XGS1210
 */
static int rtl9300_rtl8226_phy_setup(struct phy_device *phydev)
{
	u32 v, v0, v1, v2, v3, reg_6A21_5, adccal_offset_p0, adccal_offset_p1, adccal_offset_p2;
	u32 adccal_offset_p3, rg_lpf_cap_xg_p0, rg_lpf_cap_xg_p1, rg_lpf_cap_xg_p2;
	u32 rg_lpf_cap_xg_p3, rg_lpf_cap_p0, rg_lpf_cap_p1, rg_lpf_cap_p2, rg_lpf_cap_p3;

	// Check polling is turned off
	rtl8266_wait_ready(phydev);

	phy_write_mmd(phydev, MDIO_MMD_VEND2, 0xA436, 0x801E);
	v = phy_read_mmd(phydev, MDIO_MMD_VEND2, 0xA438);
	pr_info("%s, port %d patch version %x\n", __func__, phydev->mdio.addr, v);

	reg_6A21_5 = phy_read_paged(phydev, MDIO_MMD_VEND1, 0x6A21);
	/*
	 * Swap MDI pins
	 */
	v = phy_read_paged(phydev, MDIO_MMD_VEND2, 0xD068);

	if (!(v & BIT(1))) {
		pr_info("%s: MDI pins already swapped\n", __func__);
		return 0;
	}
	v0 = (v & 0xFFE0) | 0x1;
	phy_write_paged(phydev, MDIO_MMD_VEND2, 0xD068, v0);
	adccal_offset_p0 = phy_read_paged(phydev, MDIO_MMD_VEND2, 0xD06A);

	v1 = (v & 0xFFE0) | 0x9;
	phy_write_paged(phydev, MDIO_MMD_VEND2, 0xD068, v1);
	adccal_offset_p1 = phy_read_paged(phydev, MDIO_MMD_VEND2, 0xD06A);

	v2 = (v & 0xFFE0) | 0x11;
	phy_write_paged(phydev, MDIO_MMD_VEND2, 0xD068, v2);
	adccal_offset_p2 = phy_read_paged(phydev, MDIO_MMD_VEND2, 0xD06A);

	v3 = (v & 0xFFE0) | 0x19;
	phy_write_paged(phydev, MDIO_MMD_VEND2, 0xD068, v3);
	adccal_offset_p3 = phy_read_paged(phydev, MDIO_MMD_VEND2, 0xD06A);

	v = phy_read_paged(phydev, MDIO_MMD_VEND2, 0xBD5A);
	rg_lpf_cap_xg_p0 = v & 0x001F;
	rg_lpf_cap_xg_p1 = v & 0x1F00;

	v = phy_read_paged(phydev, MDIO_MMD_VEND2, 0xBD5C);
	rg_lpf_cap_xg_p2 = v & 0x001F;
	rg_lpf_cap_xg_p3 = v & 0x1F00;

	v = phy_read_paged(phydev, MDIO_MMD_VEND2, 0xBC18);
	rg_lpf_cap_p0 = v & 0x001F;
	rg_lpf_cap_p1 = v & 0x1F00;

	v = phy_read_paged(phydev, MDIO_MMD_VEND2, 0xBC1A);
	rg_lpf_cap_p2 = v & 0x001F;
	rg_lpf_cap_p3 = v & 0x1F00;

	// Actually enable PIN swapping
	reg_6A21_5 |= BIT(5);
	phy_write_paged(phydev, MDIO_MMD_VEND1, 0x6A21, reg_6A21_5);

	phy_write_paged(phydev, MDIO_MMD_VEND2, 0xD068, v0);
	phy_write_paged(phydev, MDIO_MMD_VEND2, 0xD06A, adccal_offset_p3);
	phy_write_paged(phydev, MDIO_MMD_VEND2, 0xD068, v1);
	phy_write_paged(phydev, MDIO_MMD_VEND2, 0xD06A, adccal_offset_p2);
	phy_write_paged(phydev, MDIO_MMD_VEND2, 0xD068, v2);

	phy_write_paged(phydev, MDIO_MMD_VEND2, 0xD06A, adccal_offset_p1);
	phy_write_paged(phydev, MDIO_MMD_VEND2, 0xD068, v3);
	phy_write_paged(phydev, MDIO_MMD_VEND2, 0xD06A, adccal_offset_p0);

	v = phy_read_paged(phydev, MDIO_MMD_VEND2, 0xBD5A);
	v = ( rg_lpf_cap_xg_p3 >> 8 ) | (rg_lpf_cap_xg_p2 << 8) | (v & 0xe0e0);
	phy_write_paged(phydev, MDIO_MMD_VEND2, 0xBD5A, v);

	v = phy_read_paged(phydev, MDIO_MMD_VEND2, 0xBD5C);
	v = (rg_lpf_cap_xg_p1 >> 8) | (rg_lpf_cap_xg_p0 << 8) | (v & 0xe0e0);
	phy_write_paged(phydev, MDIO_MMD_VEND2, 0xBD5C, v);

	v = phy_read_paged(phydev, MDIO_MMD_VEND2, 0xBC18);
	v = (rg_lpf_cap_p3 >> 8) | (rg_lpf_cap_p2 << 8) | (v & 0xe0e0);
	phy_write_paged(phydev, MDIO_MMD_VEND2, 0xBC18, v);

	v = phy_read_paged(phydev, MDIO_MMD_VEND2, 0xBC1A);
	v = (rg_lpf_cap_p1 >>8) | (rg_lpf_cap_p0 << 8) | (v & 0xe0e0);
	phy_write_paged(phydev, MDIO_MMD_VEND2, 0xBC1A, v);

	/*
	 * Enable SGMII or HISGMII
	 */

	v = phy_read_paged(phydev, MDIO_MMD_VEND1, 0x697A);
	v &= ~0x3f;
	v |= 0x1; // Various functions 0x1 to 0x5. Ox1 enables SGMII/HISGMII

	phy_write_paged(phydev, MDIO_MMD_VEND1, 0x697A, v);
	return 0;
}

/*
 * Configuration parameters for even Serdes in HISGMII mode for e.g. the RTL8226
 * PHY as found in a Zyxel XGS1210
 */
sds_config rtl9300_a_sds_10g_hisgmii_lane0[] =
{
	{0x00, 0x0E, 0x3053}, {0x01, 0x14, 0x0100}, {0x21, 0x03, 0x8206}, {0x21, 0x05, 0x40B0},
	{0x21, 0x06, 0x0010}, {0x21, 0x07, 0xF09F}, {0x21, 0x0C, 0x0007}, {0x21, 0x0D, 0x6009},
	{0x21, 0x0E, 0x0000}, {0x21, 0x0F, 0x0008}, {0x24, 0x00, 0x0668}, {0x24, 0x02, 0xD020},
	{0x24, 0x06, 0xC000}, {0x24, 0x0B, 0x1892}, {0x24, 0x0F, 0xFFDF}, {0x24, 0x12, 0x03C4},
	{0x24, 0x13, 0x027F}, {0x24, 0x14, 0x1311}, {0x24, 0x16, 0x00C9}, {0x24, 0x17, 0xA100},
	{0x24, 0x1A, 0x0001}, {0x24, 0x1C, 0x0400}, {0x25, 0x01, 0x0300}, {0x25, 0x02, 0x1017},
	{0x25, 0x03, 0xFFDF}, {0x25, 0x05, 0x7F7C}, {0x25, 0x07, 0x8100}, {0x25, 0x08, 0x0001},
	{0x25, 0x09, 0xFFD4}, {0x25, 0x0A, 0x7C2F}, {0x25, 0x0E, 0x003F}, {0x25, 0x0F, 0x0121},
	{0x25, 0x10, 0x0020}, {0x25, 0x11, 0x8840}, {0x28, 0x00, 0x0668}, {0x28, 0x02, 0xD020},
	{0x28, 0x06, 0xC000}, {0x28, 0x0B, 0x1892}, {0x28, 0x0F, 0xFFDF}, {0x28, 0x12, 0x03C4},
	{0x28, 0x13, 0x027F}, {0x28, 0x14, 0x1311}, {0x28, 0x16, 0x00C9}, {0x28, 0x17, 0xA100},
	{0x28, 0x1A, 0x0001}, {0x28, 0x1C, 0x0400}, {0x29, 0x01, 0x0300}, {0x29, 0x02, 0x1017},
	{0x29, 0x03, 0xFFDF}, {0x29, 0x05, 0x7F7C}, {0x29, 0x07, 0x8100}, {0x29, 0x08, 0x0001},
	{0x29, 0x09, 0xFFD4}, {0x29, 0x0A, 0x7C2F}, {0x29, 0x0E, 0x003F}, {0x29, 0x0F, 0x0121},
	{0x29, 0x10, 0x0020}, {0x29, 0x11, 0x8840}, {0x2B, 0x13, 0x0050}, {0x2B, 0x18, 0x8E88},
	{0x2B, 0x19, 0x4902}, {0x2B, 0x1D, 0x2501}, {0x2D, 0x13, 0x0050}, {0x2D, 0x17, 0x4109},
	{0x2D, 0x18, 0x8E88}, {0x2D, 0x19, 0x4902}, {0x2D, 0x1C, 0x1109}, {0x2D, 0x1D, 0x2641},
	{0x2F, 0x13, 0x0050}, {0x2F, 0x18, 0x8E88}, {0x2F, 0x19, 0x4902}, {0x2F, 0x1D, 0x66E1},
};

/*
 * Configuration parameters for odd Serdes in HISGMII mode for e.g. the RTL8226
 * PHY as found in a Zyxel XGS1210
 */
sds_config rtl9300_a_sds_10g_hisgmii_lane1[] =
{
	{0x00, 0x0E, 0x3053}, {0x01, 0x14, 0x0100}, {0x21, 0x03, 0x8206}, {0x21, 0x06, 0x0010},
	{0x21, 0x07, 0xF09F}, {0x21, 0x0A, 0x0003}, {0x21, 0x0B, 0x0005}, {0x21, 0x0C, 0x0007},
	{0x21, 0x0D, 0x6009}, {0x21, 0x0E, 0x0000}, {0x21, 0x0F, 0x0008}, {0x24, 0x00, 0x0668},
	{0x24, 0x02, 0xD020}, {0x24, 0x06, 0xC000}, {0x24, 0x0B, 0x1892}, {0x24, 0x0F, 0xFFDF},
	{0x24, 0x12, 0x03C4}, {0x24, 0x13, 0x027F}, {0x24, 0x14, 0x1311}, {0x24, 0x16, 0x00C9},
	{0x24, 0x17, 0xA100}, {0x24, 0x1A, 0x0001}, {0x24, 0x1C, 0x0400}, {0x25, 0x00, 0x820F},
	{0x25, 0x01, 0x0300}, {0x25, 0x02, 0x1017}, {0x25, 0x03, 0xFFDF}, {0x25, 0x05, 0x7F7C},
	{0x25, 0x07, 0x8100}, {0x25, 0x08, 0x0001}, {0x25, 0x09, 0xFFD4}, {0x25, 0x0A, 0x7C2F},
	{0x25, 0x0E, 0x003F}, {0x25, 0x0F, 0x0121}, {0x25, 0x10, 0x0020}, {0x25, 0x11, 0x8840},
	{0x28, 0x00, 0x0668}, {0x28, 0x02, 0xD020}, {0x28, 0x06, 0xC000}, {0x28, 0x0B, 0x1892},
	{0x28, 0x0F, 0xFFDF}, {0x28, 0x12, 0x03C4}, {0x28, 0x13, 0x027F}, {0x28, 0x14, 0x1311},
	{0x28, 0x16, 0x00C9}, {0x28, 0x17, 0xA100}, {0x28, 0x1A, 0x0001}, {0x28, 0x1C, 0x0400},
	{0x29, 0x00, 0x820F}, {0x29, 0x01, 0x0300}, {0x29, 0x02, 0x1017}, {0x29, 0x03, 0xFFDF},
	{0x29, 0x05, 0x7F7C}, {0x29, 0x07, 0x8100}, {0x29, 0x08, 0x0001}, {0x29, 0x0A, 0x7C2F},
	{0x29, 0x0E, 0x003F}, {0x29, 0x0F, 0x0121}, {0x29, 0x10, 0x0020}, {0x29, 0x11, 0x8840},
	{0x2B, 0x13, 0x3D87}, {0x2B, 0x14, 0x3108}, {0x2D, 0x13, 0x3C87}, {0x2D, 0x14, 0x1808},
};

/*
 * Performs the initial configuration of the RTL8226 PHY and configures
 * the SerDes accordingly. Note that this function depends on the use with an
 * RTL9300 SoC.
 * We enable HSGMII as default mode so that a later switch to SGMII does
 * not need to do a complete recalibration
*/
int rtl9300_configure_rtl8226(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	int phy_addr = phydev->mdio.addr;
	struct device_node *dn;
	u32 sds_num = 0;
	int phy_mode = PHY_INTERFACE_MODE_HSGMII;
	u64 saved_state;

	pr_info("%s configuring RTL8226 on port %d\n", __func__, phy_addr);
	if (dev->of_node) {
		dn = dev->of_node;

		if (of_property_read_u32(dn, "sds", &sds_num))
			return 0;  // Not the base address
	} else {
		dev_err(dev, "No DT node.\n");
		return -EINVAL;
	}

	pr_info("%s: port %d, SerDes is %d\n", __func__, phy_addr, sds_num);
	pr_info("%s CMU BAND is %d\n", __func__, rtl9300_sds_cmu_band_get(sds_num));

	saved_state  = disable_polling(phy_addr);

	// Disable MAC
	sw_w32_mask(0, 1, RTL930X_MAC_FORCE_MODE_CTRL + 4 * phy_addr);
	mdelay(20);

	// On the RTL8226 no need to change the polarity, see dal_longan_construct_macConfig_init

	// Turn Off Serdes
	rtl9300_force_sds_mode(sds_num, PHY_INTERFACE_MODE_NA);

	pr_info("%s PATCHING SerDes %d\n", __func__, sds_num);

	if (sds_num % 2) {
		rtl9300_sds_patch(sds_num, rtl9300_a_sds_10g_hisgmii_lane1,
				  sizeof(rtl9300_a_sds_10g_hisgmii_lane1) / sizeof(sds_config));
	} else {
		rtl9300_sds_patch(sds_num, rtl9300_a_sds_10g_hisgmii_lane0,
				  sizeof(rtl9300_a_sds_10g_hisgmii_lane0) / sizeof(sds_config));
	}

	// Configure PHY from phy_construct_config_init -> rtl8226_config
	rtl9300_rtl8226_phy_setup(phydev);

	// Configure link to MAC
	rtl9300_serdes_mac_link_config(sds_num, true, true);	// MAC Construct

	// Re-Enable MAC
	sw_w32_mask(1, 0, RTL930X_MAC_FORCE_MODE_CTRL + 4 * phy_addr);

	// Set initial RX calibration parameter, but do not perform actual calibration
	rtl9300_do_rx_calibration_1(sds_num, phy_mode);

	// Re-enable SDS with new mode
	rtl9300_force_sds_mode(sds_num, phy_mode);

	rtl9300_sds_tx_config(sds_num, phy_mode);

	// Re-enable polling
	resume_polling(saved_state);

	return 0;
}

int rtl9300_rtl8226_mode_set(int port, int sds_num, phy_interface_t phy_mode)
{
	pr_info("%s setting serdes %d to mode %s +++++\n", __func__, sds_num, phy_modes(phy_mode));
	// Disable MAC
	sw_w32_mask(0, 1, RTL930X_MAC_FORCE_MODE_CTRL + 4 * port);
	mdelay(20);

	// Turn Off Serdes
	rtl9300_force_sds_mode(sds_num, PHY_INTERFACE_MODE_NA);

	// Configure link to MAC
	rtl9300_serdes_mac_link_config(sds_num, true, true);	// MAC Construct

	// Re-Enable MAC
	sw_w32_mask(1, 0, RTL930X_MAC_FORCE_MODE_CTRL + 4 * port);

	// Set initial RX calibration parameter, but do not perform actual calibration
	rtl9300_do_rx_calibration_1(sds_num, PHY_INTERFACE_MODE_HSGMII);

	// Re-enable SDS with new mode
	rtl9300_force_sds_mode(sds_num, phy_mode);

	rtl9300_sds_tx_config(sds_num, PHY_INTERFACE_MODE_HSGMII);

	return 0;
}

int rtl9300_configure_serdes(int port, int sds_num, phy_interface_t phy_mode)
{
	pr_info("%s CMU BAND is %d\n", __func__, rtl9300_sds_cmu_band_get(sds_num));

	// Turn Off Serdes
	rtl9300_sds_rst(sds_num, 0x1f);

	// TODO: Apply SerDes patches

	if (phy_mode == PHY_INTERFACE_MODE_10GBASER)
		rtl9300_phy_enable_10g_1g(sds_num);

	// Disable MAC
	sw_w32_mask(0, 1, RTL930X_MAC_FORCE_MODE_CTRL + 4 * port);
	mdelay(20);

	// ----> dal_longan_sds_mode_set
	pr_info("%s: Configuring RTL9300 SERDES %d\n", __func__, sds_num);

	// Configure link to MAC
	rtl9300_serdes_mac_link_config(sds_num, true, true);	// MAC Construct

	rtl9300_force_sds_mode(sds_num, PHY_INTERFACE_MODE_NA);

	// Re-Enable MAC
	sw_w32_mask(1, 0, RTL930X_MAC_FORCE_MODE_CTRL + 4 * port);

	rtl9300_force_sds_mode(sds_num, phy_mode);

	// Enable Fiber RX
	rtl9300_sds_field_w(sds_num, 0x20, 2, 12, 12, 0);

	// Do RX calibration
	rtl9300_do_rx_calibration_1(sds_num, phy_mode);

	rtl9300_sds_tx_config(sds_num, phy_mode);

	return 0;
}

void rtl9310_sds_field_w(int sds, u32 page, u32 reg, int end_bit, int start_bit, u32 v)
{
	int l = end_bit - start_bit + 1;
	u32 data = v;

	if (l < 32) {
		u32 mask = BIT(l) - 1;

		data = rtl930x_read_sds_phy(sds, page, reg);
		data &= ~(mask << start_bit);
		data |= (v & mask) << start_bit;
	}

	rtl931x_write_sds_phy(sds, page, reg, data);
}


u32 rtl9310_sds_field_r(int sds, u32 page, u32 reg, int end_bit, int start_bit)
{
	int l = end_bit - start_bit + 1;
	u32 v = rtl931x_read_sds_phy(sds, page, reg);

	if (l >= 32)
		return v;

	return (v >> start_bit) & (BIT(l) - 1);
}

static void rtl931x_sds_rst(u32 sds)
{
	u32 o, v, o_mode;
	int shift = ((sds & 0x3) << 3);

	// TODO: We need to lock this!
	
	o = sw_r32(RTL931X_PS_SERDES_OFF_MODE_CTRL_ADDR);
	v = o | BIT(sds);
	sw_w32(v, RTL931X_PS_SERDES_OFF_MODE_CTRL_ADDR);

	o_mode = sw_r32(RTL931X_SERDES_MODE_CTRL + 4 * (sds >> 2));
	v = BIT(7) | 0x1F;
	sw_w32_mask(0xff << shift, v << shift, RTL931X_SERDES_MODE_CTRL + 4 * (sds >> 2));
	sw_w32(o_mode, RTL931X_SERDES_MODE_CTRL + 4 * (sds >> 2));

	sw_w32(o, RTL931X_PS_SERDES_OFF_MODE_CTRL_ADDR);
}

static void rtl931x_symerr_clear(u32 sds, phy_interface_t mode)
{
	u32 i;
	u32 xsg_sdsid_0, xsg_sdsid_1;

	switch (mode) {
	case PHY_INTERFACE_MODE_NA:
		break;
	case PHY_INTERFACE_MODE_XGMII:
		if (sds < 2)
			xsg_sdsid_0 = sds;
		else
			xsg_sdsid_0 = (sds - 1) * 2;
		xsg_sdsid_1 = xsg_sdsid_0 + 1;

		for (i = 0; i < 4; ++i) {
			rtl9310_sds_field_w(xsg_sdsid_0, 0x1, 24, 2, 0, i);
			rtl9310_sds_field_w(xsg_sdsid_0, 0x1, 3, 15, 8, 0x0);
			rtl9310_sds_field_w(xsg_sdsid_0, 0x1, 2, 15, 0, 0x0);
		}

		for (i = 0; i < 4; ++i) {
			rtl9310_sds_field_w(xsg_sdsid_1, 0x1, 24, 2, 0, i);
			rtl9310_sds_field_w(xsg_sdsid_1, 0x1, 3, 15, 8, 0x0);
			rtl9310_sds_field_w(xsg_sdsid_1, 0x1, 2, 15, 0, 0x0);
		}

		rtl9310_sds_field_w(xsg_sdsid_0, 0x1, 0, 15, 0, 0x0);
		rtl9310_sds_field_w(xsg_sdsid_0, 0x1, 1, 15, 8, 0x0);
		rtl9310_sds_field_w(xsg_sdsid_1, 0x1, 0, 15, 0, 0x0);
		rtl9310_sds_field_w(xsg_sdsid_1, 0x1, 1, 15, 8, 0x0);
		break;
	default:
		break;
	}

	return;
}

static u32 rtl931x_get_analog_sds(u32 sds)
{
	u32 sds_map[] = { 0, 1, 2, 3, 6, 7, 10, 11, 14, 15, 18, 19, 22, 23 };

	if (sds < 14)
		return sds_map[sds];
	return sds;
}

void rtl931x_sds_fiber_disable(u32 sds)
{
	u32 v = 0x3F;
	u32 asds = rtl931x_get_analog_sds(sds);

	rtl9310_sds_field_w(asds, 0x1F, 0x9, 11, 6, v);
}

static void rtl931x_sds_fiber_mode_set(u32 sds, phy_interface_t mode)
{
	u32 val, asds = rtl931x_get_analog_sds(sds);

	/* clear symbol error count before changing mode */
	rtl931x_symerr_clear(sds, mode);

	val = 0x9F;
	sw_w32(val, RTL931X_SERDES_MODE_CTRL + 4 * (sds >> 2));

	switch (mode) {
	case PHY_INTERFACE_MODE_SGMII:
		val = 0x5;
		break;

	case PHY_INTERFACE_MODE_1000BASEX:
		/* serdes mode FIBER1G */
		val = 0x9;
		break;

	case PHY_INTERFACE_MODE_10GBASER:
	case PHY_INTERFACE_MODE_10GKR:
		val = 0x35;
		break;
/*	case MII_10GR1000BX_AUTO:
		val = 0x39;
		break; */


	case PHY_INTERFACE_MODE_USXGMII:
		val = 0x1B;
		break;
	default:
		val = 0x25;
	}

	pr_info("%s writing analog SerDes Mode value %02x\n", __func__, val);
	rtl9310_sds_field_w(asds, 0x1F, 0x9, 11, 6, val);

	return;
}

static int rtl931x_sds_cmu_page_get(phy_interface_t mode)
{
	switch (mode) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_1000BASEX:	// MII_1000BX_FIBER / 100BX_FIBER / 1000BX100BX_AUTO
		return 0x24;
	case PHY_INTERFACE_MODE_HSGMII:
	case PHY_INTERFACE_MODE_2500BASEX:	// MII_2500Base_X:
		return 0x28;
//	case MII_HISGMII_5G:
//		return 0x2a;
	case PHY_INTERFACE_MODE_QSGMII:
		return 0x2a;			// Code also has 0x34
	case PHY_INTERFACE_MODE_XAUI:		// MII_RXAUI_LITE:
		return 0x2c;
	case PHY_INTERFACE_MODE_XGMII:		// MII_XSGMII
	case PHY_INTERFACE_MODE_10GKR:
	case PHY_INTERFACE_MODE_10GBASER:	// MII_10GR
		return 0x2e;
	default:
		return -1;
	}
	return -1;
}

static void rtl931x_cmu_type_set(u32 asds, phy_interface_t mode, int chiptype)
{
	int cmu_type = 0; // Clock Management Unit
	u32 cmu_page = 0;
	u32 frc_cmu_spd;
	u32 evenSds;
	u32 lane, frc_lc_mode_bitnum, frc_lc_mode_val_bitnum;

	switch (mode) {
	case PHY_INTERFACE_MODE_NA:
	case PHY_INTERFACE_MODE_10GKR:
	case PHY_INTERFACE_MODE_XGMII:
	case PHY_INTERFACE_MODE_10GBASER:
	case PHY_INTERFACE_MODE_USXGMII:
		return;

/*	case MII_10GR1000BX_AUTO:
		if (chiptype)
			rtl9310_sds_field_w(asds, 0x24, 0xd, 14, 14, 0);
		return; */

	case PHY_INTERFACE_MODE_QSGMII:
		cmu_type = 1;
		frc_cmu_spd = 0;
		break;

	case PHY_INTERFACE_MODE_HSGMII:
		cmu_type = 1;
		frc_cmu_spd = 1;
		break;

	case PHY_INTERFACE_MODE_1000BASEX:
		cmu_type = 1;
		frc_cmu_spd = 0;
		break;

/*	case MII_1000BX100BX_AUTO:
		cmu_type = 1;
		frc_cmu_spd = 0;
		break; */

	case PHY_INTERFACE_MODE_SGMII:
		cmu_type = 1;
		frc_cmu_spd = 0;
		break;

	case PHY_INTERFACE_MODE_2500BASEX:
		cmu_type = 1;
		frc_cmu_spd = 1;
		break;

	default:
		pr_info("SerDes %d mode is invalid\n", asds);
		return;
	}

	if (cmu_type == 1)
		cmu_page = rtl931x_sds_cmu_page_get(mode);

	lane = asds % 2;

	if (!lane) {
		frc_lc_mode_bitnum = 4;
		frc_lc_mode_val_bitnum = 5;
	} else {
		frc_lc_mode_bitnum = 6;
		frc_lc_mode_val_bitnum = 7;
	}

	evenSds = asds - lane;

	pr_info("%s: cmu_type %0d cmu_page %x frc_cmu_spd %d lane %d asds %d\n",
		__func__, cmu_type, cmu_page, frc_cmu_spd, lane, asds);

	if (cmu_type == 1) {
		pr_info("%s A CMU page 0x28 0x7 %08x\n", __func__, rtl931x_read_sds_phy(asds, 0x28, 0x7));
		rtl9310_sds_field_w(asds, cmu_page, 0x7, 15, 15, 0);
		pr_info("%s B CMU page 0x28 0x7 %08x\n", __func__, rtl931x_read_sds_phy(asds, 0x28, 0x7));
		if (chiptype) {
			rtl9310_sds_field_w(asds, cmu_page, 0xd, 14, 14, 0);
		}

		rtl9310_sds_field_w(evenSds, 0x20, 0x12, 3, 2, 0x3);
		rtl9310_sds_field_w(evenSds, 0x20, 0x12, frc_lc_mode_bitnum, frc_lc_mode_bitnum, 1);
		rtl9310_sds_field_w(evenSds, 0x20, 0x12, frc_lc_mode_val_bitnum, frc_lc_mode_val_bitnum, 0);
		rtl9310_sds_field_w(evenSds, 0x20, 0x12, 12, 12, 1);
		rtl9310_sds_field_w(evenSds, 0x20, 0x12, 15, 13, frc_cmu_spd);
	}

	pr_info("%s CMU page 0x28 0x7 %08x\n", __func__, rtl931x_read_sds_phy(asds, 0x28, 0x7));
	return;
}

static void rtl931x_sds_rx_rst(u32 sds)
{
	u32 asds = rtl931x_get_analog_sds(sds);

	if (sds < 2)
		return;

	rtl931x_write_sds_phy(asds, 0x2e, 0x12, 0x2740);
	rtl931x_write_sds_phy(asds, 0x2f, 0x0, 0x0);
	rtl931x_write_sds_phy(asds, 0x2f, 0x2, 0x2010);
	rtl931x_write_sds_phy(asds, 0x20, 0x0, 0xc10);

	rtl931x_write_sds_phy(asds, 0x2e, 0x12, 0x27c0);
	rtl931x_write_sds_phy(asds, 0x2f, 0x0, 0xc000);
	rtl931x_write_sds_phy(asds, 0x2f, 0x2, 0x6010);
	rtl931x_write_sds_phy(asds, 0x20, 0x0, 0xc30);

	mdelay(50);
}

static void rtl931x_sds_disable(u32 sds)
{
	u32 v = 0x1f;

	v |= BIT(7);
	sw_w32(v, RTL931X_SERDES_MODE_CTRL + (sds >> 2) * 4);
}

static void rtl931x_sds_mii_mode_set(u32 sds, phy_interface_t mode)
{
	u32 val;

	switch (mode) {
	case PHY_INTERFACE_MODE_QSGMII:
		val = 0x6;
		break;
	case PHY_INTERFACE_MODE_XGMII:
		val = 0x10; // serdes mode XSGMII
		break;
	case PHY_INTERFACE_MODE_USXGMII:
	case PHY_INTERFACE_MODE_2500BASEX:
		val = 0xD;
		break;
	case PHY_INTERFACE_MODE_HSGMII:
		val = 0x12;
		break;
	case PHY_INTERFACE_MODE_SGMII:
		val = 0x2;
		break;
	default:
		return;
	}

	val |= (1 << 7);

	sw_w32(val, RTL931X_SERDES_MODE_CTRL + 4 * (sds >> 2));
}

static sds_config sds_config_10p3125g_type1[] = {
	{ 0x2E, 0x00, 0x0107 }, { 0x2E, 0x01, 0x01A3 }, { 0x2E, 0x02, 0x6A24 },
	{ 0x2E, 0x03, 0xD10D }, { 0x2E, 0x04, 0x8000 }, { 0x2E, 0x05, 0xA17E },
	{ 0x2E, 0x06, 0xE31D }, { 0x2E, 0x07, 0x800E }, { 0x2E, 0x08, 0x0294 },
	{ 0x2E, 0x09, 0x0CE4 }, { 0x2E, 0x0A, 0x7FC8 }, { 0x2E, 0x0B, 0xE0E7 },
	{ 0x2E, 0x0C, 0x0200 }, { 0x2E, 0x0D, 0xDF80 }, { 0x2E, 0x0E, 0x0000 },
	{ 0x2E, 0x0F, 0x1FC2 }, { 0x2E, 0x10, 0x0C3F }, { 0x2E, 0x11, 0x0000 },
	{ 0x2E, 0x12, 0x27C0 }, { 0x2E, 0x13, 0x7E1D }, { 0x2E, 0x14, 0x1300 },
	{ 0x2E, 0x15, 0x003F }, { 0x2E, 0x16, 0xBE7F }, { 0x2E, 0x17, 0x0090 },
	{ 0x2E, 0x18, 0x0000 }, { 0x2E, 0x19, 0x4000 }, { 0x2E, 0x1A, 0x0000 },
	{ 0x2E, 0x1B, 0x8000 }, { 0x2E, 0x1C, 0x011F }, { 0x2E, 0x1D, 0x0000 },
	{ 0x2E, 0x1E, 0xC8FF }, { 0x2E, 0x1F, 0x0000 }, { 0x2F, 0x00, 0xC000 },
	{ 0x2F, 0x01, 0xF000 }, { 0x2F, 0x02, 0x6010 }, { 0x2F, 0x12, 0x0EE7 },
	{ 0x2F, 0x13, 0x0000 }
};

static sds_config sds_config_10p3125g_cmu_type1[] = {
	{ 0x2F, 0x03, 0x4210 }, { 0x2F, 0x04, 0x0000 }, { 0x2F, 0x05, 0x0019 },
	{ 0x2F, 0x06, 0x18A6 }, { 0x2F, 0x07, 0x2990 }, { 0x2F, 0x08, 0xFFF4 },
	{ 0x2F, 0x09, 0x1F08 }, { 0x2F, 0x0A, 0x0000 }, { 0x2F, 0x0B, 0x8000 },
	{ 0x2F, 0x0C, 0x4224 }, { 0x2F, 0x0D, 0x0000 }, { 0x2F, 0x0E, 0x0000 },
	{ 0x2F, 0x0F, 0xA470 }, { 0x2F, 0x10, 0x8000 }, { 0x2F, 0x11, 0x037B }
};

void rtl931x_sds_init(u32 sds, phy_interface_t mode)
{

	u32 board_sds_tx_type1[] = { 0x1C3, 0x1C3, 0x1C3, 0x1A3, 0x1A3,
		0x1A3, 0x143, 0x143, 0x143, 0x143, 0x163, 0x163
	};

	u32 board_sds_tx[] = { 0x1A00, 0x1A00, 0x200, 0x200, 0x200,
		0x200, 0x1A3, 0x1A3, 0x1A3, 0x1A3, 0x1E3, 0x1E3
	};

	u32 board_sds_tx2[] = { 0xDC0, 0x1C0, 0x200, 0x180, 0x160,
		0x123, 0x123, 0x163, 0x1A3, 0x1A0, 0x1C3, 0x9C3
	};

	u32 asds, dSds, ori, model_info, val;
	int chiptype = 0;

	asds = rtl931x_get_analog_sds(sds);

	if (sds > 13)
		return;

	pr_info("%s: set sds %d to mode %d\n", __func__, sds, mode);
	val = rtl9310_sds_field_r(asds, 0x1F, 0x9, 11, 6);

	pr_info("%s: fibermode %08X stored mode 0x%x analog SDS %d", __func__,
			rtl931x_read_sds_phy(asds, 0x1f, 0x9), val, asds);
	pr_info("%s: SGMII mode %08X in 0x24 0x9 analog SDS %d", __func__,
			rtl931x_read_sds_phy(asds, 0x24, 0x9), asds);
	pr_info("%s: CMU mode %08X stored even SDS %d", __func__,
			rtl931x_read_sds_phy(asds & ~1, 0x20, 0x12), asds & ~1);
	pr_info("%s: serdes_mode_ctrl %08X", __func__,  RTL931X_SERDES_MODE_CTRL + 4 * (sds >> 2));
	pr_info("%s CMU page 0x24 0x7 %08x\n", __func__, rtl931x_read_sds_phy(asds, 0x24, 0x7));
	pr_info("%s CMU page 0x26 0x7 %08x\n", __func__, rtl931x_read_sds_phy(asds, 0x26, 0x7));
	pr_info("%s CMU page 0x28 0x7 %08x\n", __func__, rtl931x_read_sds_phy(asds, 0x28, 0x7));
	pr_info("%s XSG page 0x0 0xe %08x\n", __func__, rtl931x_read_sds_phy(dSds, 0x0, 0xe));
	pr_info("%s XSG2 page 0x0 0xe %08x\n", __func__, rtl931x_read_sds_phy(dSds + 1, 0x0, 0xe));

	model_info = sw_r32(RTL93XX_MODEL_NAME_INFO);
	if ((model_info >> 4) & 0x1) {
		pr_info("detected chiptype 1\n");
		chiptype = 1;
	} else {
		pr_info("detected chiptype 0\n");
	}

	if (sds < 2)
		dSds = sds;
	else
		dSds = (sds - 1) * 2;

	pr_info("%s: 2.5gbit %08X dsds %d", __func__,
		rtl931x_read_sds_phy(dSds, 0x1, 0x14), dSds);

	pr_info("%s: RTL931X_PS_SERDES_OFF_MODE_CTRL_ADDR 0x%08X\n", __func__, sw_r32(RTL931X_PS_SERDES_OFF_MODE_CTRL_ADDR));
	ori = sw_r32(RTL931X_PS_SERDES_OFF_MODE_CTRL_ADDR);
	val = ori | (1 << sds);
	sw_w32(val, RTL931X_PS_SERDES_OFF_MODE_CTRL_ADDR);

	switch (mode) {
	case PHY_INTERFACE_MODE_NA:
		break;

	case PHY_INTERFACE_MODE_XGMII: // MII_XSGMII

		if (chiptype) {
			u32 xsg_sdsid_1;
			xsg_sdsid_1 = dSds + 1;
			//fifo inv clk
			rtl9310_sds_field_w(dSds, 0x1, 0x1, 7, 4, 0xf);
			rtl9310_sds_field_w(dSds, 0x1, 0x1, 3, 0, 0xf);

			rtl9310_sds_field_w(xsg_sdsid_1, 0x1, 0x1, 7, 4, 0xf);
			rtl9310_sds_field_w(xsg_sdsid_1, 0x1, 0x1, 3, 0, 0xf);

		}

		rtl9310_sds_field_w(dSds, 0x0, 0xE, 12, 12, 1);
		rtl9310_sds_field_w(dSds + 1, 0x0, 0xE, 12, 12, 1);
		break;

	case PHY_INTERFACE_MODE_USXGMII: // MII_USXGMII_10GSXGMII/10GDXGMII/10GQXGMII:
		u32 i, evenSds;
		u32 op_code = 0x6003;

		if (chiptype) {
			rtl9310_sds_field_w(asds, 0x6, 0x2, 12, 12, 1);

			for (i = 0; i < sizeof(sds_config_10p3125g_type1) / sizeof(sds_config); ++i) {
				rtl931x_write_sds_phy(asds, sds_config_10p3125g_type1[i].page - 0x4, sds_config_10p3125g_type1[i].reg, sds_config_10p3125g_type1[i].data);
			}

			evenSds = asds - (asds % 2);

			for (i = 0; i < sizeof(sds_config_10p3125g_cmu_type1) / sizeof(sds_config); ++i) {
				rtl931x_write_sds_phy(evenSds,
						      sds_config_10p3125g_cmu_type1[i].page - 0x4, sds_config_10p3125g_cmu_type1[i].reg, sds_config_10p3125g_cmu_type1[i].data);
			}

			rtl9310_sds_field_w(asds, 0x6, 0x2, 12, 12, 0);
		} else {

			rtl9310_sds_field_w(asds, 0x2e, 0xd, 6, 0, 0x0);
			rtl9310_sds_field_w(asds, 0x2e, 0xd, 7, 7, 0x1);

			rtl9310_sds_field_w(asds, 0x2e, 0x1c, 5, 0, 0x1E);
			rtl9310_sds_field_w(asds, 0x2e, 0x1d, 11, 0, 0x00);
			rtl9310_sds_field_w(asds, 0x2e, 0x1f, 11, 0, 0x00);
			rtl9310_sds_field_w(asds, 0x2f, 0x0, 11, 0, 0x00);
			rtl9310_sds_field_w(asds, 0x2f, 0x1, 11, 0, 0x00);

			rtl9310_sds_field_w(asds, 0x2e, 0xf, 12, 6, 0x7F);
			rtl931x_write_sds_phy(asds, 0x2f, 0x12, 0xaaa);

			rtl931x_sds_rx_rst(sds);

			rtl931x_write_sds_phy(asds, 0x7, 0x10, op_code);
			rtl931x_write_sds_phy(asds, 0x6, 0x1d, 0x0480);
			rtl931x_write_sds_phy(asds, 0x6, 0xe, 0x0400);
		}
		break;

	case PHY_INTERFACE_MODE_10GBASER: // MII_10GR / MII_10GR1000BX_AUTO:
		// configure 10GR fiber mode=1
		rtl9310_sds_field_w(asds, 0x1f, 0xb, 1, 1, 1);

		// init fiber_1g
		rtl9310_sds_field_w(dSds, 0x3, 0x13, 15, 14, 0);

		rtl9310_sds_field_w(dSds, 0x2, 0x0, 12, 12, 1);
		rtl9310_sds_field_w(dSds, 0x2, 0x0, 6, 6, 1);
		rtl9310_sds_field_w(dSds, 0x2, 0x0, 13, 13, 0);

		// init auto
		rtl9310_sds_field_w(asds, 0x1f, 13, 15, 0, 0x109e);
		rtl9310_sds_field_w(asds, 0x1f, 0x6, 14, 10, 0x8);
		rtl9310_sds_field_w(asds, 0x1f, 0x7, 10, 4, 0x7f);
		break;

	case PHY_INTERFACE_MODE_HSGMII:
		rtl9310_sds_field_w(dSds, 0x1, 0x14, 8, 8, 1);
		break;

	case PHY_INTERFACE_MODE_1000BASEX: // MII_1000BX_FIBER
		rtl9310_sds_field_w(dSds, 0x3, 0x13, 15, 14, 0);

		rtl9310_sds_field_w(dSds, 0x2, 0x0, 12, 12, 1);
		rtl9310_sds_field_w(dSds, 0x2, 0x0, 6, 6, 1);
		rtl9310_sds_field_w(dSds, 0x2, 0x0, 13, 13, 0);
		break;

	case PHY_INTERFACE_MODE_SGMII:
		rtl9310_sds_field_w(asds, 0x24, 0x9, 15, 15, 0);
		break;

	case PHY_INTERFACE_MODE_2500BASEX:
		rtl9310_sds_field_w(dSds, 0x1, 0x14, 8, 8, 1);
		break;

	case PHY_INTERFACE_MODE_QSGMII:
	default:
		pr_info("%s: PHY mode %s not supported by SerDes %d\n",
			__func__, phy_modes(mode), sds);
		return;
	}

	rtl931x_cmu_type_set(asds, mode, chiptype);

	if (sds >= 2 && sds <= 13) {
		if (chiptype)
			rtl931x_write_sds_phy(asds, 0x2E, 0x1, board_sds_tx_type1[sds - 2]);
		else {
			val = 0xa0000;
			sw_w32(val, RTL931X_CHIP_INFO_ADDR);
			val = sw_r32(RTL931X_CHIP_INFO_ADDR);
			if (val & BIT(28)) // consider 9311 etc. RTL9313_CHIP_ID == HWP_CHIP_ID(unit))
			{
				rtl931x_write_sds_phy(asds, 0x2E, 0x1, board_sds_tx2[sds - 2]);
			} else {
				rtl931x_write_sds_phy(asds, 0x2E, 0x1, board_sds_tx[sds - 2]);
			}
			val = 0;
			sw_w32(val, RTL931X_CHIP_INFO_ADDR);
		}
	}

	val = ori & ~BIT(sds);
	sw_w32(val, RTL931X_PS_SERDES_OFF_MODE_CTRL_ADDR);
	pr_debug("%s: RTL931X_PS_SERDES_OFF_MODE_CTRL_ADDR 0x%08X\n", __func__, sw_r32(RTL931X_PS_SERDES_OFF_MODE_CTRL_ADDR));

	if (mode == PHY_INTERFACE_MODE_XGMII || mode == PHY_INTERFACE_MODE_QSGMII
	    || mode == PHY_INTERFACE_MODE_HSGMII || mode == PHY_INTERFACE_MODE_SGMII
	    || mode == PHY_INTERFACE_MODE_USXGMII) {
		if (mode == PHY_INTERFACE_MODE_XGMII)
			rtl931x_sds_mii_mode_set(sds, mode);
		else
			rtl931x_sds_fiber_mode_set(sds, mode);
	}
}

int rtl931x_sds_cmu_band_set(int sds, bool enable, u32 band, phy_interface_t mode)
{
	u32 asds;
	int page = rtl931x_sds_cmu_page_get(mode);

	sds -= (sds % 2);
	sds = sds & ~1;
	asds = rtl931x_get_analog_sds(sds);
	page += 1;

	if (enable) {
		rtl9310_sds_field_w(asds, page, 0x7, 13, 13, 0);
		rtl9310_sds_field_w(asds, page, 0x7, 11, 11, 0);
	} else {
		rtl9310_sds_field_w(asds, page, 0x7, 13, 13, 0);
		rtl9310_sds_field_w(asds, page, 0x7, 11, 11, 0);
	}
		
	rtl9310_sds_field_w(asds, page, 0x7, 4, 0, band);

	rtl931x_sds_rst(sds);

	return 0;
}

int rtl931x_sds_cmu_band_get(int sds, phy_interface_t mode)
{
	int page = rtl931x_sds_cmu_page_get(mode);
	u32 asds, band;

	sds -= (sds % 2);
	asds = rtl931x_get_analog_sds(sds);
	page += 1;
	rtl931x_write_sds_phy(asds, 0x1f, 0x02, 73);

	rtl9310_sds_field_w(asds, page, 0x5, 15, 15, 1);
	band = rtl9310_sds_field_r(asds, 0x1f, 0x15, 8, 3);
	pr_info("%s band is: %d\n", __func__, band);

	return band;
}


int rtl931x_link_sts_get(u32 sds)
{
	u32 sts, sts1, latch_sts, latch_sts1;
	if (0){
		u32 xsg_sdsid_0, xsg_sdsid_1;

		xsg_sdsid_0 = sds < 2 ? sds : (sds - 1) * 2;
		xsg_sdsid_1 = xsg_sdsid_0 + 1;

		sts = rtl9310_sds_field_r(xsg_sdsid_0, 0x1, 29, 8, 0);
		sts1 = rtl9310_sds_field_r(xsg_sdsid_1, 0x1, 29, 8, 0);
		latch_sts = rtl9310_sds_field_r(xsg_sdsid_0, 0x1, 30, 8, 0);
		latch_sts1 = rtl9310_sds_field_r(xsg_sdsid_1, 0x1, 30, 8, 0);
	} else {
		u32  asds, dsds;

		asds = rtl931x_get_analog_sds(sds);
		sts = rtl9310_sds_field_r(asds, 0x5, 0, 12, 12);
		latch_sts = rtl9310_sds_field_r(asds, 0x4, 1, 2, 2);

		dsds = sds < 2 ? sds : (sds - 1) * 2;
		latch_sts1 = rtl9310_sds_field_r(dsds, 0x2, 1, 2, 2);
		sts1 = rtl9310_sds_field_r(dsds, 0x2, 1, 2, 2);
	}

	pr_info("%s: serdes %d sts %d, sts1 %d, latch_sts %d, latch_sts1 %d\n", __func__,
		sds, sts, sts1, latch_sts, latch_sts1);
	return sts1;
}

static int rtl8214fc_phy_probe(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	int addr = phydev->mdio.addr;
	int ret = 0;

	/* 839x has internal SerDes */
	if (soc_info.id == 0x8393)
		return -ENODEV;

	/* All base addresses of the PHYs start at multiples of 8 */
	devm_phy_package_join(dev, phydev, addr & (~7),
				sizeof(struct rtl83xx_shared_private));

	if (!(addr % 8)) {
		struct rtl83xx_shared_private *shared = phydev->shared->priv;
		shared->name = "RTL8214FC";
		/* Configuration must be done while patching still possible */
		ret = rtl8380_configure_rtl8214fc(phydev);
		if (ret)
			return ret;
	}

	return 0;
}

static int rtl8214c_phy_probe(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	int addr = phydev->mdio.addr;
	int ret = 0;

	/* All base addresses of the PHYs start at multiples of 8 */
	devm_phy_package_join(dev, phydev, addr & (~7),
				sizeof(struct rtl83xx_shared_private));

	if (!(addr % 8)) {
		struct rtl83xx_shared_private *shared = phydev->shared->priv;
		shared->name = "RTL8214C";
		/* Configuration must be done whil patching still possible */
		ret = rtl8380_configure_rtl8214c(phydev);
		if (ret)
			return ret;
	}
	return 0;
}

static int rtl8218b_ext_phy_probe(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	int addr = phydev->mdio.addr;
	int ret = 0;

	/* All base addresses of the PHYs start at multiples of 8 */
	devm_phy_package_join(dev, phydev, addr & (~7),
				sizeof(struct rtl83xx_shared_private));

	if (!(addr % 8) && soc_info.family == RTL8380_FAMILY_ID) {
		struct rtl83xx_shared_private *shared = phydev->shared->priv;
		shared->name = "RTL8218B (external)";
		/* Configuration must be done while patching still possible */
		ret = rtl8380_configure_ext_rtl8218b(phydev);
		if (ret)
			return ret;
	}

	return 0;
}

static int rtl8218b_int_phy_probe(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	int addr = phydev->mdio.addr;
	int ret = 0;

	if (soc_info.family != RTL8380_FAMILY_ID)
		return -ENODEV;
	if (addr >= 24)
		return -ENODEV;

	pr_debug("%s: id: %d\n", __func__, addr);
	/* All base addresses of the PHYs start at multiples of 8 */
	devm_phy_package_join(dev, phydev, addr & (~7),
			      sizeof(struct rtl83xx_shared_private));

	if (!(addr % 8)) {
		struct rtl83xx_shared_private *shared = phydev->shared->priv;
		shared->name = "RTL8218B (internal)";
		/* Configuration must be done while patching still possible */
		ret = rtl8380_configure_int_rtl8218b(phydev);
	}

	return 0;
}

static int rtl8218d_phy_probe(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	int addr = phydev->mdio.addr;

	/* All base addresses of the PHYs start at multiples of 8 */
	devm_phy_package_join(dev, phydev, addr & (~7),
				sizeof(struct rtl83xx_shared_private));
	if (!(addr % 8)) {
		struct rtl83xx_shared_private *shared = phydev->shared->priv;
		shared->name = "RTL8218D";
	}
	return 0;
}

static int rtl838x_serdes_probe(struct phy_device *phydev)
{
	int addr = phydev->mdio.addr;

	if (soc_info.family != RTL8380_FAMILY_ID)
		return -ENODEV;
	if (addr < 24)
		return -ENODEV;

	/* On the RTL8380M, PHYs 24-27 connect to the internal SerDes */
	if (soc_info.id == 0x8380) {
		if (addr == 24)
			return rtl8380_configure_serdes(phydev);
		return 0;
	}
	return -ENODEV;
}

static int rtl8393_serdes_probe(struct phy_device *phydev)
{
	int addr = phydev->mdio.addr;

	pr_info("%s: id: %d\n", __func__, addr);
	if (soc_info.family != RTL8390_FAMILY_ID)
		return -ENODEV;

	if (addr < 24)
		return -ENODEV;

	return rtl8390_configure_serdes(phydev);
}

static int rtl8214qf_phy_probe(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	int addr = phydev->mdio.addr;
	u32 val;

	/* All base addresses of the PHYs start at multiples of 4 */
	devm_phy_package_join(dev, phydev, addr & (~7),
				sizeof(struct rtl83xx_shared_private));

	if (addr % 4)
		return 0;

	/* Read internal PHY ID */
	phy_write_paged(phydev, 0, 30, 8);
	val = phy_read_paged(phydev, 0x279, 16);

	/* Is 8214? */
	phydev_info(phydev, "Detected internal version %x\n", val);
	if (val != 0x8214)
		return -ENODEV;

	// Check minor version:
	val = phy_read_paged(phydev, 0x278, 22);
	phydev_info(phydev, "Detected minor %x\n", val);
	if ((val & 0xffc0) != 0x8980)
		return -ENODEV;

	phydev_info(phydev, "Identified RTL8214QF PHY\n");
	struct rtl83xx_shared_private *shared = phydev->shared->priv;
	shared->name = "RTL8214QF";

	return 0;
}

static int rtl9300_serdes_probe(struct phy_device *phydev)
{
	if (soc_info.family != RTL9300_FAMILY_ID)
		return -ENODEV;

	phydev_info(phydev, "Detected internal RTL9300 Serdes\n");

	return 0;
}

static struct phy_driver rtl83xx_phy_driver[] = {
	{
		PHY_ID_MATCH_MODEL(PHY_ID_RTL8214C),
		.name		= "Realtek RTL8214C",
		.features	= PHY_GBIT_FEATURES,
		.flags		= PHY_HAS_REALTEK_PAGES,
		.match_phy_device = rtl8214c_match_phy_device,
		.probe		= rtl8214c_phy_probe,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.set_loopback	= genphy_loopback,
	},
	{
		PHY_ID_MATCH_MODEL(PHY_ID_RTL8214FC),
		.name		= "Realtek RTL8214FC",
		.features	= PHY_GBIT_FIBRE_FEATURES,
		.flags		= PHY_HAS_REALTEK_PAGES,
		.match_phy_device = rtl8214fc_match_phy_device,
		.probe		= rtl8214fc_phy_probe,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.set_loopback	= genphy_loopback,
		.set_port	= rtl8214fc_set_port,
		.get_port	= rtl8214fc_get_port,
		.set_eee	= rtl8214fc_set_eee,
		.get_eee	= rtl8214fc_get_eee,
	},
	{
		PHY_ID_MATCH_MODEL(PHY_ID_RTL8218B_E),
		.name		= "Realtek RTL8218B (external)",
		.features	= PHY_GBIT_FEATURES,
		.flags		= PHY_HAS_REALTEK_PAGES,
		.match_phy_device = rtl8218b_ext_match_phy_device,
		.probe		= rtl8218b_ext_phy_probe,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.set_loopback	= genphy_loopback,
		.set_eee	= rtl8218b_set_eee,
		.get_eee	= rtl8218b_get_eee,
	},
	{
		PHY_ID_MATCH_MODEL(PHY_ID_RTL8218D),
		.name		= "REALTEK RTL8218D",
		.features	= PHY_GBIT_FEATURES,
		.flags		= PHY_HAS_REALTEK_PAGES,
		.probe		= rtl8218d_phy_probe,
		.config_init	= rtl9300_configure_8218d,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.set_loopback	= genphy_loopback,
		.set_eee	= rtl8218d_set_eee,
		.get_eee	= rtl8218d_get_eee,
	},
	{
		PHY_ID_MATCH_MODEL(PHY_ID_RTL8221B),
		.name           = "REALTEK RTL8221B",
		.features       = PHY_GBIT_FEATURES,
		.flags		= PHY_HAS_REALTEK_PAGES,
		.suspend        = genphy_suspend,
		.resume         = genphy_resume,
		.set_loopback   = genphy_loopback,
		.read_page      = rtl8226_read_page,
		.write_page     = rtl8226_write_page,
		.read_status    = rtl8226_read_status,
		.config_aneg    = rtl8226_config_aneg,
		.set_eee        = rtl8226_set_eee,
		.get_eee        = rtl8226_get_eee,
	},
	{
		PHY_ID_MATCH_MODEL(PHY_ID_RTL8226),
		.name		= "REALTEK RTL8226",
		.features	= PHY_GBIT_FEATURES,
		.flags		= PHY_HAS_REALTEK_PAGES,
		.config_init	= rtl9300_configure_rtl8226,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.set_loopback	= genphy_loopback,
		.read_page	= rtl8226_read_page,
		.write_page	= rtl8226_write_page,
		.read_status	= rtl8226_read_status,
		.config_aneg	= rtl8226_config_aneg,
		.set_eee	= rtl8226_set_eee,
		.get_eee	= rtl8226_get_eee,
	},
	{
		PHY_ID_MATCH_MODEL(PHY_ID_RTL8218B_I),
		.name		= "Realtek RTL8218B (internal)",
		.features	= PHY_GBIT_FEATURES,
		.flags		= PHY_HAS_REALTEK_PAGES,
		.probe		= rtl8218b_int_phy_probe,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.set_loopback	= genphy_loopback,
		.set_eee	= rtl8218b_set_eee,
		.get_eee	= rtl8218b_get_eee,
	},
	{
		PHY_ID_MATCH_MODEL(PHY_ID_RTL8218B_I),
		.name		= "Realtek RTL8380 SERDES",
		.features	= PHY_GBIT_FIBRE_FEATURES,
		.flags		= PHY_HAS_REALTEK_PAGES,
		.probe		= rtl838x_serdes_probe,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.set_loopback	= genphy_loopback,
		.read_status	= rtl8380_read_status,
	},
	{
		PHY_ID_MATCH_MODEL(PHY_ID_RTL8393_I),
		.name		= "Realtek RTL8393 SERDES",
		.features	= PHY_GBIT_FIBRE_FEATURES,
		.flags		= PHY_HAS_REALTEK_PAGES,
		.probe		= rtl8393_serdes_probe,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.set_loopback	= genphy_loopback,
		.read_status	= rtl8393_read_status,
	},
	{
		PHY_ID_MATCH_MODEL(PHY_ID_RTL8214QF),
		.name		= "REALTEK RTL8214QF",
		.features	= PHY_GBIT_FIBRE_FEATURES,
		.flags		= PHY_HAS_REALTEK_PAGES,
		.probe		= rtl8214qf_phy_probe,
		.config_init	= rtl8214qf_configure,
		.read_status	= rtl8214qf_read_status,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.set_loopback	= genphy_loopback,
	},
	{
		PHY_ID_MATCH_MODEL(PHY_ID_RTL9300_I),
		.name		= "REALTEK RTL9300 SERDES",
		.features	= PHY_GBIT_FIBRE_FEATURES,
		.flags		= PHY_HAS_REALTEK_PAGES,
		.probe		= rtl9300_serdes_probe,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.set_loopback	= genphy_loopback,
		.read_status	= rtl9300_read_status,
	},
};

module_phy_driver(rtl83xx_phy_driver);

static struct mdio_device_id __maybe_unused rtl83xx_tbl[] = {
	{ PHY_ID_MATCH_MODEL(PHY_ID_RTL8214FC) },
	{ }
};

MODULE_DEVICE_TABLE(mdio, rtl83xx_tbl);

MODULE_AUTHOR("B. Koblitz");
MODULE_DESCRIPTION("RTL83xx PHY driver");
MODULE_LICENSE("GPL");
