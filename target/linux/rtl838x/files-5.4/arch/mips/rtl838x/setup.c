// SPDX-License-Identifier: GPL-2.0-only
/*
 * Setup for the Realtek RTL838X SoC:
 *	Memory, Timer and Serial
 *
 * Copyright (C) 2020 B. Koblitz
 * based on the original BSP by
 * Copyright (C) 2006-2012 Tony Wu (tonywu@realtek.com)
 *
 */

#include <linux/console.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>

#include <asm/addrspace.h>
#include <asm/io.h>

#include <asm/bootinfo.h>
#include <linux/of_fdt.h>
#include <asm/reboot.h>
#include <asm/time.h>		/* for mips_hpt_frequency */
#include <asm/prom.h>
#include <asm/smp-ops.h>
#include <linux/delay.h>

#include <asm/cevt-r4k.h>
#include <linux/irq.h>

#include "mach-rtl838x.h"

extern int rtl838x_serial_init(void);
extern void rtl9300_timer_init(void);
extern struct rtl838x_soc_info soc_info;

struct clk {
	struct clk_lookup cl;
	unsigned long rate;
};

struct clk cpu_clk;

u32 pll_reset_value;

static void rtl838x_restart(char *command)
{
	u32 pll = sw_r32(RTL838X_PLL_CML_CTRL);
	 /* SoC reset vector (in flash memory): on RTL839x platform preferred way to reset */
	void (*f)(void) = (void *) 0xbfc00000;
	u32 v;

	pr_info("System restart.\n");
	if (soc_info.family == RTL8390_FAMILY_ID) {
		f();
		/* If calling reset vector fails, reset entire chip */
		sw_w32(0xFFFFFFFF, RTL839X_RST_GLB_CTRL);
		/* If this fails, halt the CPU */
		while
			(1);
	} else if (soc_info.family == RTL9300_FAMILY_ID) {
		sw_w32(0x1, RTL930X_RST_GLB_CTRL_0);
		while
			(1);
	} else if (soc_info.family == RTL9310_FAMILY_ID) {
		sw_w32(1, RTL931X_RST_GLB_CTRL);
		v = sw_r32(RTL931X_RST_GLB_CTRL);
		sw_w32(0x101, RTL931X_RST_GLB_CTRL);
		msleep(15);
		sw_w32(v, RTL931X_RST_GLB_CTRL);
		msleep(15);
		sw_w32(0x101, RTL931X_RST_GLB_CTRL);
	}
	pr_info("PLL control register: %x, applying reset value %x\n",
		pll, pll_reset_value);
	sw_w32(3, RTL838X_INT_RW_CTRL);
	sw_w32(pll_reset_value, RTL838X_PLL_CML_CTRL);
	sw_w32(0, RTL838X_INT_RW_CTRL);

	pr_info("Resetting RTL838X SoC\n");
	/* Reset Global Control1 Register */
	sw_w32(1, RTL838X_RST_GLB_CTRL_1);
}

static void rtl838x_halt(void)
{
	pr_info("System halted.\n");
	while
		(1);
}

static void __init rtl838x_setup(void)
{
	unsigned int val;

	pr_info("Registering _machine_restart\n");
	_machine_restart = rtl838x_restart;
	_machine_halt = rtl838x_halt;

	val = rtl838x_r32((volatile void *)0xBB0040000);
	if (val == 3)
		pr_info("PCI device found\n");
	else
		pr_info("NO PCI device found\n");

	/* Setup System LED. Bit 15 (14 for RTL8390) then allows to toggle it */
	switch (soc_info.family) {
	case RTL8380_FAMILY_ID:
		sw_w32_mask(0, 3 << 16, RTL838X_LED_GLB_CTRL);
		break;
	case RTL8390_FAMILY_ID:
		sw_w32_mask(0, 3 << 15, RTL839X_LED_GLB_CTRL);
		break;
	case RTL9300_FAMILY_ID:
		if (soc_info.id == 0x9302)
			sw_w32_mask(0, 3 << 13, RTL9302_LED_GLB_CTRL);
		else
			sw_w32_mask(0, 3 << 13, RTL930X_LED_GLB_CTRL);
		break;
	case RTL9310_FAMILY_ID:
		sw_w32_mask(0, 3 << 12, RTL931X_LED_GLB_CTRL);
		break;
	}
}

void __init plat_mem_setup(void)
{
	void *dtb;

	pr_info("%s called\n", __func__);

	set_io_port_base(KSEG1);

	if (fw_passed_dtb) /* UHI interface */
		dtb = (void *)fw_passed_dtb;
	else if (__dtb_start != __dtb_end)
		dtb = (void *)__dtb_start;
	else
		panic("no dtb found");

	/*
	 * Load the devicetree. This causes the chosen node to be
	 * parsed resulting in our memory appearing
	 */
	__dt_setup_arch(dtb);

	rtl838x_setup();
}


/*
 * Linux clock API
 */
int clk_enable(struct clk *clk)
{
	return 0;
}
EXPORT_SYMBOL_GPL(clk_enable);

void clk_disable(struct clk *clk)
{

}
EXPORT_SYMBOL_GPL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	if (!clk)
		return 0;

	return clk->rate;
}
EXPORT_SYMBOL_GPL(clk_get_rate);

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	return -1;
}
EXPORT_SYMBOL_GPL(clk_set_rate);

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	return -1;
}
EXPORT_SYMBOL_GPL(clk_round_rate);

void __init plat_time_init(void)
{
	u32 freq = 500000000;
	struct device_node *np;
	struct clk *clk = &cpu_clk;

	np = of_find_node_by_name(NULL, "cpus");
	if (!np) {
		pr_err("Missing 'cpus' DT node, using default frequency.");
	} else {
		if (of_property_read_u32(np, "frequency", &freq) < 0)
			pr_err("No 'frequency' property in DT, using default.");
		else
			pr_info("CPU frequency from device tree: %d Hz", freq);
		of_node_put(np);
	}

	clk->rate = freq;

	if (IS_ERR(clk))
		panic("unable to get CPU clock, err=%ld", PTR_ERR(clk));

	pr_info("CPU Clock: %ld MHz\n", clk->rate / 1000000);
	mips_hpt_frequency = freq / 2;
	cp0_compare_irq = 7;
	if (soc_info.family == RTL9300_FAMILY_ID)
		rtl9300_timer_init();

	if (soc_info.family == RTL8380_FAMILY_ID) {
		pll_reset_value = sw_r32(RTL838X_PLL_CML_CTRL);
		pr_info("PLL control register: %x\n", pll_reset_value);
	}

	/* With the info from the command line and cpu-freq we can setup the console */
	rtl838x_serial_init();
}
