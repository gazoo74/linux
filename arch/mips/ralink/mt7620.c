/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * Parts of this file are based on Ralink's 2.6.21 BSP
 *
 * Copyright (C) 2008-2011 Gabor Juhos <juhosg@openwrt.org>
 * Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 * Copyright (C) 2013 John Crispin <blogic@openwrt.org>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>

#include <asm/mipsregs.h>
#include <asm/mach-ralink/ralink_regs.h>
#include <asm/mach-ralink/mt7620.h>

#include "common.h"

/* analog */
#define PMU0_CFG		0x88
#define PMU_SW_SET		BIT(28)
#define A_DCDC_EN		BIT(24)
#define A_SSC_PERI		BIT(19)
#define A_SSC_GEN		BIT(18)
#define A_SSC_M			0x3
#define A_SSC_S			16
#define A_DLY_M			0x7
#define A_DLY_S			8
#define A_VTUNE_M		0xff

/* digital */
#define PMU1_CFG		0x8C
#define DIG_SW_SEL		BIT(25)

/* clock scaling */
#define CLKCFG_FDIV_MASK	0x1f00
#define CLKCFG_FDIV_USB_VAL	0x0300
#define CLKCFG_FFRAC_MASK	0x001f
#define CLKCFG_FFRAC_USB_VAL	0x0003

enum mt762x_soc_type mt762x_soc;

/* does the board have sdram or ddram */
static int dram_type;

static struct ralink_pinmux_grp mode_mux[] = {
	{
		.name = "i2c",
		.mask = MT7620_GPIO_MODE_I2C,
		.gpio_first = 1,
		.gpio_last = 2,
	}, {
		.name = "spi",
		.mask = MT7620_GPIO_MODE_SPI,
		.gpio_first = 3,
		.gpio_last = 6,
	}, {
		.name = "uartlite",
		.mask = MT7620_GPIO_MODE_UART1,
		.gpio_first = 15,
		.gpio_last = 16,
	}, {
		.name = "wdt",
		.mask = MT7620_GPIO_MODE_WDT,
		.gpio_first = 17,
		.gpio_last = 17,
	}, {
		.name = "mdio",
		.mask = MT7620_GPIO_MODE_MDIO,
		.gpio_first = 22,
		.gpio_last = 23,
	}, {
		.name = "rgmii1",
		.mask = MT7620_GPIO_MODE_RGMII1,
		.gpio_first = 24,
		.gpio_last = 35,
	}, {
		.name = "spi refclk",
		.mask = MT7620_GPIO_MODE_SPI_REF_CLK,
		.gpio_first = 37,
		.gpio_last = 39,
	}, {
		.name = "jtag",
		.mask = MT7620_GPIO_MODE_JTAG,
		.gpio_first = 40,
		.gpio_last = 44,
	}, {
		/* shared lines with jtag */
		.name = "ephy",
		.mask = MT7620_GPIO_MODE_EPHY,
		.gpio_first = 40,
		.gpio_last = 44,
	}, {
		.name = "nand",
		.mask = MT7620_GPIO_MODE_JTAG,
		.gpio_first = 45,
		.gpio_last = 59,
	}, {
		.name = "rgmii2",
		.mask = MT7620_GPIO_MODE_RGMII2,
		.gpio_first = 60,
		.gpio_last = 71,
	}, {
		.name = "wled",
		.mask = MT7620_GPIO_MODE_WLED,
		.gpio_first = 72,
		.gpio_last = 72,
	}, {0}
};

static struct ralink_pinmux_grp uart_mux[] = {
	{
		.name = "uartf",
		.mask = MT7620_GPIO_MODE_UARTF,
		.gpio_first = 7,
		.gpio_last = 14,
	}, {
		.name = "pcm uartf",
		.mask = MT7620_GPIO_MODE_PCM_UARTF,
		.gpio_first = 7,
		.gpio_last = 14,
	}, {
		.name = "pcm i2s",
		.mask = MT7620_GPIO_MODE_PCM_I2S,
		.gpio_first = 7,
		.gpio_last = 14,
	}, {
		.name = "i2s uartf",
		.mask = MT7620_GPIO_MODE_I2S_UARTF,
		.gpio_first = 7,
		.gpio_last = 14,
	}, {
		.name = "pcm gpio",
		.mask = MT7620_GPIO_MODE_PCM_GPIO,
		.gpio_first = 11,
		.gpio_last = 14,
	}, {
		.name = "gpio uartf",
		.mask = MT7620_GPIO_MODE_GPIO_UARTF,
		.gpio_first = 7,
		.gpio_last = 10,
	}, {
		.name = "gpio i2s",
		.mask = MT7620_GPIO_MODE_GPIO_I2S,
		.gpio_first = 7,
		.gpio_last = 10,
	}, {
		.name = "gpio",
		.mask = MT7620_GPIO_MODE_GPIO,
	}, {0}
};

struct ralink_pinmux rt_gpio_pinmux = {
	.mode = mode_mux,
	.uart = uart_mux,
	.uart_shift = MT7620_GPIO_MODE_UART0_SHIFT,
	.uart_mask = MT7620_GPIO_MODE_UART0_MASK,
};

static struct rt2880_pmx_func pwm1_grp_mt7628[] = {
	FUNC("sdxc", 3, 19, 1),
	FUNC("utif", 2, 19, 1),
	FUNC("gpio", 1, 19, 1),
	FUNC("pwm", 0, 19, 1),
};

static struct rt2880_pmx_func pwm0_grp_mt7628[] = {
	FUNC("sdxc", 3, 18, 1),
	FUNC("utif", 2, 18, 1),
	FUNC("gpio", 1, 18, 1),
	FUNC("pwm", 0, 18, 1),
};

static struct rt2880_pmx_func uart2_grp_mt7628[] = {
	FUNC("sdxc", 3, 20, 2),
	FUNC("pwm", 2, 20, 2),
	FUNC("gpio", 1, 20, 2),
	FUNC("uart2", 0, 20, 2),
};

static struct rt2880_pmx_func uart1_grp_mt7628[] = {
	FUNC("sdxc", 3, 45, 2),
	FUNC("pwm", 2, 45, 2),
	FUNC("gpio", 1, 45, 2),
	FUNC("uart1", 0, 45, 2),
};

static struct rt2880_pmx_func i2c_grp_mt7628[] = {
	FUNC("-", 3, 4, 2),
	FUNC("debug", 2, 4, 2),
	FUNC("gpio", 1, 4, 2),
	FUNC("i2c", 0, 4, 2),
};

static struct rt2880_pmx_func refclk_grp_mt7628[] = { FUNC("reclk", 0, 36, 1) };
static struct rt2880_pmx_func perst_grp_mt7628[] = { FUNC("perst", 0, 37, 1) };
static struct rt2880_pmx_func wdt_grp_mt7628[] = { FUNC("wdt", 0, 38, 1) };
static struct rt2880_pmx_func spi_grp_mt7628[] = { FUNC("spi", 0, 7, 4) };

static struct rt2880_pmx_func sd_mode_grp_mt7628[] = {
	FUNC("jtag", 3, 22, 8),
	FUNC("utif", 2, 22, 8),
	FUNC("gpio", 1, 22, 8),
	FUNC("sdxc", 0, 22, 8),
};

static struct rt2880_pmx_func uart0_grp_mt7628[] = {
	FUNC("-", 3, 12, 2),
	FUNC("-", 2, 12, 2),
	FUNC("gpio", 1, 12, 2),
	FUNC("uart0", 0, 12, 2),
};

static struct rt2880_pmx_func i2s_grp_mt7628[] = {
	FUNC("antenna", 3, 0, 4),
	FUNC("pcm", 2, 0, 4),
	FUNC("gpio", 1, 0, 4),
	FUNC("i2s", 0, 0, 4),
};

static struct rt2880_pmx_func spi_cs1_grp_mt7628[] = {
	FUNC("-", 3, 6, 1),
	FUNC("refclk", 2, 6, 1),
	FUNC("gpio", 1, 6, 1),
	FUNC("spi", 0, 6, 1),
};

static struct rt2880_pmx_func spis_grp_mt7628[] = {
	FUNC("pwm", 3, 14, 4),
	FUNC("util", 2, 14, 4),
	FUNC("gpio", 1, 14, 4),
	FUNC("spis", 0, 14, 4),
};

static struct rt2880_pmx_func gpio_grp_mt7628[] = {
	FUNC("pcie", 3, 11, 1),
	FUNC("refclk", 2, 11, 1),
	FUNC("gpio", 1, 11, 1),
	FUNC("gpio", 0, 11, 1),
};

#define MT7628_GPIO_MODE_MASK	0x3

#define MT7628_GPIO_MODE_PWM1	30
#define MT7628_GPIO_MODE_PWM0	28
#define MT7628_GPIO_MODE_UART2	26
#define MT7628_GPIO_MODE_UART1	24
#define MT7628_GPIO_MODE_I2C	20
#define MT7628_GPIO_MODE_REFCLK	18
#define MT7628_GPIO_MODE_PERST	16
#define MT7628_GPIO_MODE_WDT	14
#define MT7628_GPIO_MODE_SPI	12
#define MT7628_GPIO_MODE_SDMODE	10
#define MT7628_GPIO_MODE_UART0	8
#define MT7628_GPIO_MODE_I2S	6
#define MT7628_GPIO_MODE_CS1	4
#define MT7628_GPIO_MODE_SPIS	2
#define MT7628_GPIO_MODE_GPIO	0

static struct rt2880_pmx_group mt7628an_pinmux_data[] = {
	GRP_G("pmw1", pwm1_grp_mt7628, MT7628_GPIO_MODE_MASK, 1, MT7628_GPIO_MODE_PWM1),
	GRP_G("pmw0", pwm0_grp_mt7628, MT7628_GPIO_MODE_MASK, 1, MT7628_GPIO_MODE_PWM0),
	GRP_G("uart2", uart2_grp_mt7628, MT7628_GPIO_MODE_MASK, 1, MT7628_GPIO_MODE_UART2),
	GRP_G("uart1", uart1_grp_mt7628, MT7628_GPIO_MODE_MASK, 1, MT7628_GPIO_MODE_UART1),
	GRP_G("i2c", i2c_grp_mt7628, MT7628_GPIO_MODE_MASK, 1, MT7628_GPIO_MODE_I2C),
	GRP("refclk", refclk_grp_mt7628, 1, MT7628_GPIO_MODE_REFCLK),
	GRP("perst", perst_grp_mt7628, 1, MT7628_GPIO_MODE_PERST),
	GRP("wdt", wdt_grp_mt7628, 1, MT7628_GPIO_MODE_WDT),
	GRP("spi", spi_grp_mt7628, 1, MT7628_GPIO_MODE_SPI),
	GRP_G("sdmode", sd_mode_grp_mt7628, MT7628_GPIO_MODE_MASK, 1, MT7628_GPIO_MODE_SDMODE),
	GRP_G("uart0", uart0_grp_mt7628, MT7628_GPIO_MODE_MASK, 1, MT7628_GPIO_MODE_UART0),
	GRP_G("i2s", i2s_grp_mt7628, MT7628_GPIO_MODE_MASK, 1, MT7628_GPIO_MODE_I2S),
	GRP_G("spi cs1", spi_cs1_grp_mt7628, MT7628_GPIO_MODE_MASK, 1, MT7628_GPIO_MODE_CS1),
	GRP_G("spis", spis_grp_mt7628, MT7628_GPIO_MODE_MASK, 1, MT7628_GPIO_MODE_SPIS),
	GRP_G("gpio", gpio_grp_mt7628, MT7628_GPIO_MODE_MASK, 1, MT7628_GPIO_MODE_GPIO),
	{ 0 }
};

static __init u32
mt7620_calc_rate(u32 ref_rate, u32 mul, u32 div)
{
	u64 t;

	t = ref_rate;
	t *= mul;
	do_div(t, div);

	return t;
}

#define MHZ(x)		((x) * 1000 * 1000)

static __init unsigned long
mt7620_get_xtal_rate(void)
{
	u32 reg;

	reg = rt_sysc_r32(SYSC_REG_SYSTEM_CONFIG0);
	if (reg & SYSCFG0_XTAL_FREQ_SEL)
		return MHZ(40);

	return MHZ(20);
}

static __init unsigned long
mt7620_get_periph_rate(unsigned long xtal_rate)
{
	u32 reg;

	reg = rt_sysc_r32(SYSC_REG_CLKCFG0);
	if (reg & CLKCFG0_PERI_CLK_SEL)
		return xtal_rate;

	return MHZ(40);
}

static const u32 mt7620_clk_divider[] __initconst = { 2, 3, 4, 8 };

static __init unsigned long
mt7620_get_cpu_pll_rate(unsigned long xtal_rate)
{
	u32 reg;
	u32 mul;
	u32 div;

	reg = rt_sysc_r32(SYSC_REG_CPLL_CONFIG0);
	if (reg & CPLL_CFG0_BYPASS_REF_CLK)
		return xtal_rate;

	if ((reg & CPLL_CFG0_SW_CFG) == 0)
		return MHZ(600);

	mul = (reg >> CPLL_CFG0_PLL_MULT_RATIO_SHIFT) &
	      CPLL_CFG0_PLL_MULT_RATIO_MASK;
	mul += 24;
	if (reg & CPLL_CFG0_LC_CURFCK)
		mul *= 2;

	div = (reg >> CPLL_CFG0_PLL_DIV_RATIO_SHIFT) &
	      CPLL_CFG0_PLL_DIV_RATIO_MASK;

	WARN_ON(div >= ARRAY_SIZE(mt7620_clk_divider));

	return mt7620_calc_rate(xtal_rate, mul, mt7620_clk_divider[div]);
}

static __init unsigned long
mt7620_get_pll_rate(unsigned long xtal_rate, unsigned long cpu_pll_rate)
{
	u32 reg;

	reg = rt_sysc_r32(SYSC_REG_CPLL_CONFIG1);
	if (reg & CPLL_CFG1_CPU_AUX1)
		return xtal_rate;

	if (reg & CPLL_CFG1_CPU_AUX0)
		return MHZ(480);

	return cpu_pll_rate;
}

static __init unsigned long
mt7620_get_cpu_rate(unsigned long pll_rate)
{
	u32 reg;
	u32 mul;
	u32 div;

	reg = rt_sysc_r32(SYSC_REG_CPU_SYS_CLKCFG);

	mul = reg & CPU_SYS_CLKCFG_CPU_FFRAC_MASK;
	div = (reg >> CPU_SYS_CLKCFG_CPU_FDIV_SHIFT) &
	      CPU_SYS_CLKCFG_CPU_FDIV_MASK;

	return mt7620_calc_rate(pll_rate, mul, div);
}

static const u32 mt7620_ocp_dividers[16] __initconst = {
	[CPU_SYS_CLKCFG_OCP_RATIO_2] = 2,
	[CPU_SYS_CLKCFG_OCP_RATIO_3] = 3,
	[CPU_SYS_CLKCFG_OCP_RATIO_4] = 4,
	[CPU_SYS_CLKCFG_OCP_RATIO_5] = 5,
	[CPU_SYS_CLKCFG_OCP_RATIO_10] = 10,
};

static __init unsigned long
mt7620_get_dram_rate(unsigned long pll_rate)
{
	if (dram_type == SYSCFG0_DRAM_TYPE_SDRAM)
		return pll_rate / 4;

	return pll_rate / 3;
}

static __init unsigned long
mt7620_get_sys_rate(unsigned long cpu_rate)
{
	u32 reg;
	u32 ocp_ratio;
	u32 div;

	reg = rt_sysc_r32(SYSC_REG_CPU_SYS_CLKCFG);

	ocp_ratio = (reg >> CPU_SYS_CLKCFG_OCP_RATIO_SHIFT) &
		    CPU_SYS_CLKCFG_OCP_RATIO_MASK;

	if (WARN_ON(ocp_ratio >= ARRAY_SIZE(mt7620_ocp_dividers)))
		return cpu_rate;

	div = mt7620_ocp_dividers[ocp_ratio];
	if (WARN(!div, "invalid divider for OCP ratio %u", ocp_ratio))
		return cpu_rate;

	return cpu_rate / div;
}

void __init ralink_clk_init(void)
{
	unsigned long xtal_rate;
	unsigned long cpu_pll_rate;
	unsigned long pll_rate;
	unsigned long cpu_rate;
	unsigned long sys_rate;
	unsigned long dram_rate;
	unsigned long periph_rate;

	xtal_rate = mt7620_get_xtal_rate();

#define RFMT(label)	label ":%lu.%03luMHz "
#define RINT(x)		((x) / 1000000)
#define RFRAC(x)	(((x) / 1000) % 1000)

	if (mt762x_soc == MT762X_SOC_MT7628AN) {
		if (xtal_rate == MHZ(40))
			cpu_rate = MHZ(580);
		else
			cpu_rate = MHZ(575);
		dram_rate = sys_rate = cpu_rate / 3;
		periph_rate = MHZ(40);

		ralink_clk_add("10000d00.uartlite", periph_rate);
		ralink_clk_add("10000e00.uartlite", periph_rate);
	} else {
		cpu_pll_rate = mt7620_get_cpu_pll_rate(xtal_rate);
		pll_rate = mt7620_get_pll_rate(xtal_rate, cpu_pll_rate);

		cpu_rate = mt7620_get_cpu_rate(pll_rate);
		dram_rate = mt7620_get_dram_rate(pll_rate);
		sys_rate = mt7620_get_sys_rate(cpu_rate);
		periph_rate = mt7620_get_periph_rate(xtal_rate);

		pr_debug(RFMT("XTAL") RFMT("CPU_PLL") RFMT("PLL"),
			 RINT(xtal_rate), RFRAC(xtal_rate),
			 RINT(cpu_pll_rate), RFRAC(cpu_pll_rate),
			 RINT(pll_rate), RFRAC(pll_rate));

		ralink_clk_add("10000500.uart", periph_rate);
	}

	pr_debug(RFMT("CPU") RFMT("DRAM") RFMT("SYS") RFMT("PERIPH"),
		 RINT(cpu_rate), RFRAC(cpu_rate),
		 RINT(dram_rate), RFRAC(dram_rate),
		 RINT(sys_rate), RFRAC(sys_rate),
		 RINT(periph_rate), RFRAC(periph_rate));
#undef RFRAC
#undef RINT
#undef RFMT

	ralink_clk_add("cpu", cpu_rate);
	ralink_clk_add("10000100.timer", periph_rate);
	ralink_clk_add("10000120.watchdog", periph_rate);
	ralink_clk_add("10000b00.spi", sys_rate);
	ralink_clk_add("10000c00.uartlite", periph_rate);
 	ralink_clk_add("10000d00.uart1", periph_rate);
 	ralink_clk_add("10000e00.uart2", periph_rate);
	ralink_clk_add("10180000.wmac", xtal_rate);

	if (IS_ENABLED(CONFIG_USB) && mt762x_soc != MT762X_SOC_MT7628AN) {
		/*
		 * When the CPU goes into sleep mode, the BUS clock will be too low for
		 * USB to function properly
		 */
		u32 val = rt_sysc_r32(SYSC_REG_CPU_SYS_CLKCFG);

		val &= ~(CLKCFG_FDIV_MASK | CLKCFG_FFRAC_MASK);
		val |= CLKCFG_FDIV_USB_VAL | CLKCFG_FFRAC_USB_VAL;

		rt_sysc_w32(val, SYSC_REG_CPU_SYS_CLKCFG);
	}
}

void __init ralink_of_remap(void)
{
	rt_sysc_membase = plat_of_remap_node("ralink,mt7620a-sysc");
	rt_memc_membase = plat_of_remap_node("ralink,mt7620a-memc");

	if (!rt_sysc_membase || !rt_memc_membase)
		panic("Failed to remap core resources");
}

static __init void
mt7620_dram_init(struct ralink_soc_info *soc_info)
{
	switch (dram_type) {
	case SYSCFG0_DRAM_TYPE_SDRAM:
		pr_info("Board has SDRAM\n");
		soc_info->mem_size_min = MT7620_SDRAM_SIZE_MIN;
		soc_info->mem_size_max = MT7620_SDRAM_SIZE_MAX;
		break;

	case SYSCFG0_DRAM_TYPE_DDR1:
		pr_info("Board has DDR1\n");
		soc_info->mem_size_min = MT7620_DDR1_SIZE_MIN;
		soc_info->mem_size_max = MT7620_DDR1_SIZE_MAX;
		break;

	case SYSCFG0_DRAM_TYPE_DDR2:
		pr_info("Board has DDR2\n");
		soc_info->mem_size_min = MT7620_DDR2_SIZE_MIN;
		soc_info->mem_size_max = MT7620_DDR2_SIZE_MAX;
		break;
	default:
		BUG();
	}
}

static __init void
mt7628_dram_init(struct ralink_soc_info *soc_info)
{
	switch (dram_type) {
	case SYSCFG0_DRAM_TYPE_DDR1_MT7628:
		pr_info("Board has DDR1\n");
		soc_info->mem_size_min = MT7620_DDR1_SIZE_MIN;
		soc_info->mem_size_max = MT7620_DDR1_SIZE_MAX;
		break;

	case SYSCFG0_DRAM_TYPE_DDR2_MT7628:
		pr_info("Board has DDR2\n");
		soc_info->mem_size_min = MT7620_DDR2_SIZE_MIN;
		soc_info->mem_size_max = MT7620_DDR2_SIZE_MAX;
		break;
	default:
		BUG();
	}
}

void prom_soc_init(struct ralink_soc_info *soc_info)
{
	void __iomem *sysc = (void __iomem *) KSEG1ADDR(MT7620_SYSC_BASE);
	unsigned char *name = NULL;
	u32 n0;
	u32 n1;
	u32 rev;
	u32 cfg0;
	u32 pmu0;
	u32 pmu1;
	u32 bga;

	n0 = __raw_readl(sysc + SYSC_REG_CHIP_NAME0);
	n1 = __raw_readl(sysc + SYSC_REG_CHIP_NAME1);
	rev = __raw_readl(sysc + SYSC_REG_CHIP_REV);
	bga = (rev >> CHIP_REV_PKG_SHIFT) & CHIP_REV_PKG_MASK;

	if (n0 == MT7620_CHIP_NAME0 && n1 == MT7620_CHIP_NAME1) {
		if (bga) {
			mt762x_soc = MT762X_SOC_MT7620A;
			name = "MT7620A";
			soc_info->compatible = "ralink,mt7620a-soc";
		} else {
			mt762x_soc = MT762X_SOC_MT7620N;
			name = "MT7620N";
			soc_info->compatible = "ralink,mt7620n-soc";
#ifdef CONFIG_PCI
			panic("mt7620n is only supported for non pci kernels");
#endif
		}
	} else if (n0 == MT7620_CHIP_NAME0 && n1 == MT7628_CHIP_NAME1) {
		mt762x_soc = MT762X_SOC_MT7628AN;
		name = "MT7628AN";
		soc_info->compatible = "ralink,mt7628an-soc";
	} else {
		panic("mt762x: unknown SoC, n0:%08x n1:%08x\n", n0, n1);
	}

	snprintf(soc_info->sys_type, RAMIPS_SYS_TYPE_LEN,
		"Ralink %s ver:%u eco:%u",
		name,
		(rev >> CHIP_REV_VER_SHIFT) & CHIP_REV_VER_MASK,
		(rev & CHIP_REV_ECO_MASK));

	cfg0 = __raw_readl(sysc + SYSC_REG_SYSTEM_CONFIG0);
	dram_type = (cfg0 >> SYSCFG0_DRAM_TYPE_SHIFT) & SYSCFG0_DRAM_TYPE_MASK;

	soc_info->mem_base = MT7620_DRAM_BASE;
	if (mt762x_soc == MT762X_SOC_MT7628AN)
		mt7628_dram_init(soc_info);
	else
		mt7620_dram_init(soc_info);

	pmu0 = __raw_readl(sysc + PMU0_CFG);
	pmu1 = __raw_readl(sysc + PMU1_CFG);

	pr_info("Analog PMU set to %s control\n",
		(pmu0 & PMU_SW_SET) ? ("sw") : ("hw"));
	pr_info("Digital PMU set to %s control\n",
		(pmu1 & DIG_SW_SEL) ? ("sw") : ("hw"));

	if (mt762x_soc == MT762X_SOC_MT7628AN)
		rt2880_pinmux_data = mt7628an_pinmux_data;
	else
		rt2880_pinmux_data = mt7620a_pinmux_data;
}
