/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Realtek RTL83XX clock headers
 * Copyright (C) 2022 Markus Stockhausen <markus.stockhausen@gmx.de>
 */

/*
 * Switch registers (e.g. switch configuration, CPU PLL, ...)
 */

#define RTL_SW_CORE_BASE			(0xbb000000)

#define RTL838X_INT_RW_CTRL			(0x0058)
#define RTL838X_PLL_GLB_CTRL			(0x0fc0)
#define RTL838X_PLL_CPU_CTRL0			(0x0fc4)
#define RTL838X_PLL_CPU_CTRL1			(0x0fc8)
#define RTL838X_PLL_CPU_MISC_CTRL		(0x0fcc)
#define RTL838X_PLL_LXB_CTRL0			(0x0fd0)
#define RTL838X_PLL_LXB_CTRL1			(0x0fd4)
#define RTL838X_PLL_LXB_MISC_CTRL		(0x0fd8)
#define RTL838X_PLL_MEM_CTRL0			(0x0fdc)
#define RTL838X_PLL_MEM_CTRL1			(0x0fe0)
#define RTL838X_PLL_MEM_MISC_CTRL		(0x0fe4)
#define RTL838X_PLL_SW_CTRL0			(0x0fe8)
#define RTL838X_PLL_SW_CTRL1			(0x0fec)
#define RTL838X_PLL_SW_MISC_CTRL		(0x0ff0)

#define RTL839X_PLL_GLB_CTRL			(0x0024)
#define RTL839X_PLL_CPU_CTRL0			(0x0028)
#define RTL839X_PLL_CPU_CTRL1			(0x002c)
#define RTL839X_PLL_CPU_MISC_CTRL		(0x0034)
#define RTL839X_PLL_LXB_CTRL0			(0x0038)
#define RTL839X_PLL_LXB_CTRL1			(0x003c)
#define RTL839X_PLL_LXB_MISC_CTRL		(0x0044)
#define RTL839X_PLL_MEM_CTRL0			(0x0048)
#define RTL839X_PLL_MEM_CTRL1			(0x004c)
#define RTL839X_PLL_MEM_MISC_CTRL		(0x0054)
#define RTL839X_PLL_SW_CTRL			(0x0058)
#define RTL839X_PLL_SW_MISC_CTRL		(0x005c)

#define RTL930X_MODEL_NAME_INFO			(0x0004)
#define RTL930X_PLL_GLB_CTRL0			(0xe200)
#define RTL930X_PLL_GLB_CTRL1			(0xe200)
#define RTL930X_PLL_CPU_CTRL0			(0xe208)
#define RTL930X_PLL_CPU_CTRL1			(0xe20c)
#define RTL930X_PLL_CPU_MISC_CTRL		(0xe210)
#define RTL930X_PLL_SW_CTRL0			(0xe214)
#define RTL930X_PLL_SW_CTRL1			(0xe218)
#define RTL930X_PLL_SW_MISC_CTRL		(0xe21c)
#define RTL930X_PLL_SW_DIV_CTRL			(0xe220)

#define RTL_PLL_CTRL0_CMU_SEL_PREDIV(v)		(((v) >> 0) & 0x3)
#define RTL_PLL_CTRL0_CMU_SEL_DIV4(v)		(((v) >> 2) & 0x1)
#define RTL_PLL_CTRL0_CMU_NCODE_IN(v)		(((v) >> 4) & 0xff)

#define RTL83XX_PLL_CTRL0_CMU_DIVN2(v)		(((v) >> 12) & 0xff)

#define RTL838X_INT_RW_CTRL_READ_EN		(1 << 0)
#define RTL838X_INT_RW_CTRL_WRITE_EN		(1 << 1)
#define RTL838X_GLB_CTRL_EN_CPU_PLL_MASK	(1 << 0)
#define RTL838X_GLB_CTRL_EN_LXB_PLL_MASK	(1 << 1)
#define RTL838X_GLB_CTRL_EN_MEM_PLL_MASK	(1 << 2)
#define RTL838X_GLB_CTRL_CPU_PLL_READY_MASK	(1 << 8)
#define RTL838X_GLB_CTRL_LXB_PLL_READY_MASK	(1 << 9)
#define RTL838X_GLB_CTRL_MEM_PLL_READY_MASK	(1 << 10)
#define RTL838X_GLB_CTRL_CPU_PLL_SC_MUX_MASK	(1 << 12)

#define RTL838X_PLL_CTRL1_CMU_DIVN2_SELB(v)	(((v) >> 26) & 0x1)
#define RTL838X_PLL_CTRL1_CMU_DIVN3_SEL(v)	(((v) >> 27) & 0x3)

#define RTL839X_GLB_CTRL_CPU_CLKSEL_MASK	(1 << 11)
#define RTL839X_GLB_CTRL_MEM_CLKSEL_MASK	(1 << 12)
#define RTL839X_GLB_CTRL_LXB_CLKSEL_MASK	(1 << 13)

#define RTL839X_PLL_CTRL1_CMU_DIVN2_SELB(v)	(((v) >> 2) & 0x1)
#define RTL839X_PLL_CTRL1_CMU_DIVN3_SEL(v)	(((v) >> 0) & 0x3)

#define RTL930X_PLL_CTRL0_CMU_DIVN3_CPU(v)	(((v) >> 25) & 0x3)
#define RTL930X_PLL_MISC_CMU_DIVN2(v)		(((v) >> 4) & 0x3)
#define RTL930X_PLL_SW_DIV_CTRL_DIVN2_LXB(v)	(((v) >> 9) & 0xf)

/*
 * Core registers (e.g. memory controller, ...)
 */

#define RTL_SOC_BASE				(0xB8000000)

#define RTL_MC_MCR				(0x1000)
#define RTL_MC_DCR				(0x1004)
#define RTL_MC_DTR0				(0x1008)
#define RTL_MC_DTR1				(0x100c)
#define RTL_MC_DTR2				(0x1010)
#define RTL_MC_DMCR				(0x101c)
#define RTL_MC_DACCR				(0x1500)
#define RTL_MC_DCDR				(0x1060)

#define RTL930X_SYS_STATUS			(0x0044)
#define RTL930X_PLL_MEM_CTRL0			(0x0234)
#define RTL930X_PLL_MEM_CTRL1			(0x0238)
#define RTL930X_PLL_MEM_CTRL2			(0x023c)
#define RTL930X_PLL_MEM_CTRL3			(0x0240)
#define RTL930X_PLL_MEM_CTRL4			(0x0244)
#define RTL930X_PLL_MEM_CTRL5			(0x0248)
#define RTL930X_PLL_MEM_CTRL6			(0x024c)

#define RTL_MC_MCR_DRAMTYPE(v)			((((v) >> 28) & 0xf) + 1)
#define RTL_MC_DCR_BUSWIDTH(v)			(8 << (((v) >> 24) & 0xf))

#define RTL930X_PLL_MEM_CTRL2_PREDIV(v)		(((v) >> 14) & 0x3)
#define RTL930X_PLL_MEM_CTRL3_CMU_NCODE_IN(v)	(((v) >> 24) & 0xff)

/*
 * SRAM stuff
 */

#define RTL_SRAM_MARKER				(0x5eaf00d5)
#define RTL_SRAM_BASE_CACHED			(0x9f000000)
#define RTL_SRAM_BASE_UNCACHED			(0xbf000000)
