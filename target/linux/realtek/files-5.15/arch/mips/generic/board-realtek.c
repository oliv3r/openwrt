// SPDX-License-Identifier: GPL-2.0-only
/*
 * based on the original BSP by
 * Copyright (C) 2006-2012 Tony Wu (tonywu@realtek.com)
 */

#include <asm/cpu-features.h>
#include <asm/mach-realtek/otto.h>
#include <asm/machine.h>
#include <asm/mips-cps.h>
#include <asm/mips-gic.h>
#include <asm/mipsregs.h>
#include <asm/prom.h>
#include <asm/setup.h>
#include <asm/smp-ops.h>
#include <linux/init.h>
#include <linux/mod_devicetable.h>
#include <linux/printk.h>

struct rtl83xx_soc_info soc_info;

#ifdef CONFIG_MIPS_MT_SMP
/*
 * This is needed by the VPE loader code, just set it to 0 and assume
 * that the firmware hardcodes this value to something useful.
 */
unsigned long physical_memsize = 0L;

extern const struct plat_smp_ops vsmp_smp_ops;

static void __init rtl_init_secondary(void)
{
#ifndef CONFIG_CEVT_R4K
/*
 * These devices are low on resources. There might be the chance that CEVT_R4K
 * is not enabled in kernel build. Nevertheless the timer and interrupt 7 might
 * be active by default after startup of secondary VPE. With no registered
 * handler that leads to continuous unhandeled interrupts. In this case disable
 * counting (DC) in the core and confirm a pending interrupt.
 */
	write_c0_cause(read_c0_cause() | CAUSEF_DC);
	write_c0_compare(0);
#endif /* CONFIG_CEVT_R4K */
/*
 * Enable all CPU interrupts, as everything is managed by the external
 * controller. TODO: Standard vsmp_init_secondary() has special treatment for
 * Malta if external GIC is available. Maybe we need this too.
 */
	if (mips_gic_present())
		pr_warn("%s: GIC present. Maybe interrupt enabling required.\n", __func__);
	else
		set_c0_status(ST0_IM);
}
#endif /* CONFIG_MIPS_MT_SMP */

static void __init identify_rtl9302(void)
{
	switch (sw_r32(RTL93XX_MODEL_NAME_INFO) & 0xfffffff0) {
	case 0x93020810:
		soc_info.name = "RTL9302A 12x2.5G";
		break;
	case 0x93021010:
		soc_info.name = "RTL9302B 8x2.5G";
		break;
	case 0x93021810:
		soc_info.name = "RTL9302C 16x2.5G";
		break;
	case 0x93022010:
		soc_info.name = "RTL9302D 24x2.5G";
		break;
	case 0x93020800:
		soc_info.name = "RTL9302A";
		break;
	case 0x93021000:
		soc_info.name = "RTL9302B";
		break;
	case 0x93021800:
		soc_info.name = "RTL9302C";
		break;
	case 0x93022000:
		soc_info.name = "RTL9302D";
		break;
	case 0x93023001:
		soc_info.name = "RTL9302F";
		break;
	default:
		soc_info.name = "RTL9302";
	}
}

static void __init realtek_soc_identify(void)
{
	uint32_t model;

	model = sw_r32(RTL838X_MODEL_NAME_INFO);
	pr_info("RTL838X model is %x\n", model);
	model = model >> 16 & 0xFFFF;

	if ((model != 0x8328) && (model != 0x8330) && (model != 0x8332)
	    && (model != 0x8380) && (model != 0x8382)) {
		model = sw_r32(RTL839X_MODEL_NAME_INFO);
		pr_info("RTL839X model is %x\n", model);
		model = model >> 16 & 0xFFFF;
	}

	if ((model & 0x8390) != 0x8380 && (model & 0x8390) != 0x8390) {
		model = sw_r32(RTL93XX_MODEL_NAME_INFO);
		pr_info("RTL93XX model is %x\n", model);
		model = model >> 16 & 0xFFFF;
	}

	soc_info.id = model;

	switch (model) {
	case 0x8328:
		soc_info.name = "RTL8328";
		soc_info.family = RTL8328_FAMILY_ID;
		break;
	case 0x8332:
		soc_info.name = "RTL8332";
		soc_info.family = RTL8380_FAMILY_ID;
		break;
	case 0x8380:
		soc_info.name = "RTL8380";
		soc_info.family = RTL8380_FAMILY_ID;
		break;
	case 0x8382:
		soc_info.name = "RTL8382";
		soc_info.family = RTL8380_FAMILY_ID;
		break;
	case 0x8390:
		soc_info.name = "RTL8390";
		soc_info.family = RTL8390_FAMILY_ID;
		break;
	case 0x8391:
		soc_info.name = "RTL8391";
		soc_info.family = RTL8390_FAMILY_ID;
		break;
	case 0x8392:
		soc_info.name = "RTL8392";
		soc_info.family = RTL8390_FAMILY_ID;
		break;
	case 0x8393:
		soc_info.name = "RTL8393";
		soc_info.family = RTL8390_FAMILY_ID;
		break;
	case 0x9301:
		soc_info.name = "RTL9301";
		soc_info.family = RTL9300_FAMILY_ID;
		break;
	case 0x9302:
		identify_rtl9302();
		soc_info.family = RTL9300_FAMILY_ID;
		break;
	case 0x9303:
		soc_info.name = "RTL9303";
		soc_info.family = RTL9300_FAMILY_ID;
		break;
	case 0x9313:
		soc_info.name = "RTL9313";
		soc_info.family = RTL9310_FAMILY_ID;
		break;
	default:
		soc_info.name = "DEFAULT";
		soc_info.family = 0;
	}

	pr_info("SoC Type: %s\n", soc_info.name);
}

static const void __init *realtek_fixup_fdt(const void *fdt, const void *data)
{
	realtek_soc_identify();
	system_type = soc_info.name;

	return fdt;
}

static void __init realtek_register_smp_ops(void)
{
	if (!register_cps_smp_ops())
		return;

#ifdef CONFIG_MIPS_MT_SMP
	if (cpu_has_mipsmt) {
		extern const struct plat_smp_ops vsmp_smp_ops;
		struct plat_smp_ops rtl_smp_ops;

		rtl_smp_ops = vsmp_smp_ops;
		rtl_smp_ops.init_secondary = rtl_init_secondary;
		register_smp_ops(&rtl_smp_ops);

		return;
	}
#endif

	register_up_smp_ops();
}

static const struct of_device_id realtek_board_ids[] __initconst = {
	{ .compatible = "realtek,otto-soc" },
	/* RTL838x */
	{ .compatible = "realtek,maple-soc" },
	{ .compatible = "realtek,rtl838x-soc" },
	{ .compatible = "realtek,rtl8380-soc" },
	{ .compatible = "realtek,rtl8381-soc" },
	{ .compatible = "realtek,rtl8382-soc" },
	/* RTL839x */
	{ .compatible = "realtek,cypress-soc" },
	{ .compatible = "realtek,rtl839x-soc" },
	{ .compatible = "realtek,rtl8390-soc" },
	{ .compatible = "realtek,rtl8391-soc" },
	{ .compatible = "realtek,rtl8392-soc" },
	{ .compatible = "realtek,rtl8393-soc" },
	/* RTL930x */
	{ .compatible = "realtek,longan-soc" },
	{ .compatible = "realtek,rtl930x-soc" },
	{ .compatible = "realtek,rtl9300-soc" },
	{ .compatible = "realtek,rtl9301-soc" },
	{ .compatible = "realtek,rtl9302-soc" },
	{ .compatible = "realtek,rtl9302a-soc" },
	{ .compatible = "realtek,rtl9302b-soc" },
	{ .compatible = "realtek,rtl9302c-soc" },
	{ .compatible = "realtek,rtl9302d-soc" },
	{ .compatible = "realtek,rtl9302e-soc" },
	{ .compatible = "realtek,rtl9302f-soc" },
	{ .compatible = "realtek,rtl9303-soc" },
	/* RTL931x */
	{ .compatible = "realtek,mango-soc" },
	{ .compatible = "realtek,rtl931x-soc" },
	{ .compatible = "realtek,rtl9310-soc" },
	{ .compatible = "realtek,rtl9311-soc" },
	{ .compatible = "realtek,rtl9313-soc" },
	{ /* sentinel */ }
};

MIPS_MACHINE(realtek) = {
	.matches          = realtek_board_ids,
	.fixup_fdt        = realtek_fixup_fdt,
	.register_smp_ops = realtek_register_smp_ops,
};
