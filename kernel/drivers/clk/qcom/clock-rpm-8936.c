/* Copyright (c) 2014, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/ctype.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <soc/qcom/clock-local2.h>
#include <soc/qcom/clock-rpm.h>
#include <soc/qcom/clock-voter.h>
#include <soc/qcom/rpm-smd.h>

#include <linux/clk/msm-clock-generic.h>

#include <dt-bindings/clock/msm-clocks-8936.h>

#include "clock.h"

#define GCC_DEBUG_CLK_CTL	0x74000
#define RPM_MISC_CLK_TYPE	0x306b6c63
#define RPM_BUS_CLK_TYPE	0x316b6c63
#define RPM_MEM_CLK_TYPE	0x326b6c63
#define RPM_SMD_KEY_ENABLE	0x62616E45

#define CXO_ID			0x0
#define QDSS_ID			0x1
#define BUS_SCALING		0x2

#define PCNOC_ID		0x0
#define SNOC_ID			0x1
#define SYSMMNOC_ID		0x2
#define BIMC_ID			0x0

/* XO clock */
#define BB_CLK1_ID		1
#define BB_CLK2_ID		2
#define RF_CLK2_ID		5

static void __iomem *virt_base;

/* SMD clocks */
DEFINE_CLK_RPM_SMD(pcnoc_clk, pcnoc_a_clk, RPM_BUS_CLK_TYPE, PCNOC_ID, NULL);
DEFINE_CLK_RPM_SMD(snoc_clk, snoc_a_clk, RPM_BUS_CLK_TYPE, SNOC_ID, NULL);
DEFINE_CLK_RPM_SMD(sysmmnoc_clk, sysmmnoc_a_clk, RPM_BUS_CLK_TYPE,
							SYSMMNOC_ID, NULL);
DEFINE_CLK_RPM_SMD(bimc_clk, bimc_a_clk, RPM_MEM_CLK_TYPE, BIMC_ID, NULL);

DEFINE_CLK_RPM_SMD_BRANCH(xo_clk_src, xo_a_clk_src,
				RPM_MISC_CLK_TYPE, CXO_ID, 19200000);

DEFINE_CLK_RPM_SMD_QDSS(qdss_clk, qdss_a_clk, RPM_MISC_CLK_TYPE, QDSS_ID);

/* SMD_XO_BUFFER */
DEFINE_CLK_RPM_SMD_XO_BUFFER(bb_clk1, bb_clk1_a, BB_CLK1_ID);
DEFINE_CLK_RPM_SMD_XO_BUFFER(bb_clk2, bb_clk2_a, BB_CLK2_ID);
DEFINE_CLK_RPM_SMD_XO_BUFFER(rf_clk2, rf_clk2_a, RF_CLK2_ID);

DEFINE_CLK_RPM_SMD_XO_BUFFER_PINCTRL(bb_clk1_pin, bb_clk1_a_pin, BB_CLK1_ID);
DEFINE_CLK_RPM_SMD_XO_BUFFER_PINCTRL(bb_clk2_pin, bb_clk2_a_pin, BB_CLK2_ID);
DEFINE_CLK_RPM_SMD_XO_BUFFER_PINCTRL(rf_clk2_pin, rf_clk2_a_pin, RF_CLK2_ID);

/* Voter clocks */
static DEFINE_CLK_VOTER(pcnoc_msmbus_clk, &pcnoc_clk.c, LONG_MAX);
static DEFINE_CLK_VOTER(snoc_msmbus_clk,  &snoc_clk.c,  LONG_MAX);
static DEFINE_CLK_VOTER(sysmmnoc_msmbus_clk,  &sysmmnoc_clk.c,  LONG_MAX);
static DEFINE_CLK_VOTER(bimc_msmbus_clk,  &bimc_clk.c,  LONG_MAX);

static DEFINE_CLK_VOTER(pcnoc_msmbus_a_clk, &pcnoc_a_clk.c, LONG_MAX);
static DEFINE_CLK_VOTER(snoc_msmbus_a_clk,  &snoc_a_clk.c,  LONG_MAX);
static DEFINE_CLK_VOTER(sysmmnoc_msmbus_a_clk,  &sysmmnoc_a_clk.c,  LONG_MAX);
static DEFINE_CLK_VOTER(bimc_msmbus_a_clk,  &bimc_a_clk.c,  LONG_MAX);
static DEFINE_CLK_VOTER(pcnoc_keepalive_a_clk, &pcnoc_a_clk.c, LONG_MAX);

static DEFINE_CLK_VOTER(pcnoc_usb_a_clk, &pcnoc_a_clk.c,  LONG_MAX);
static DEFINE_CLK_VOTER(snoc_usb_a_clk,  &snoc_a_clk.c,  LONG_MAX);
static DEFINE_CLK_VOTER(bimc_usb_a_clk,  &bimc_a_clk.c,  LONG_MAX);

/* Branch Voter clocks */
static DEFINE_CLK_BRANCH_VOTER(xo_gcc, &xo_clk_src.c);
static DEFINE_CLK_BRANCH_VOTER(xo_otg_clk, &xo_clk_src.c);
static DEFINE_CLK_BRANCH_VOTER(xo_lpm_clk, &xo_clk_src.c);
static DEFINE_CLK_BRANCH_VOTER(xo_pil_pronto_clk, &xo_clk_src.c);
static DEFINE_CLK_BRANCH_VOTER(xo_pil_mss_clk, &xo_clk_src.c);
static DEFINE_CLK_BRANCH_VOTER(xo_wlan_clk, &xo_clk_src.c);

static struct mux_clk rpm_debug_mux = {
	.ops = &mux_reg_ops,
	.en_mask = BIT(16),
	.mask = 0x1FF,
	.base = &virt_base,
	MUX_SRC_LIST(
	{&snoc_clk.c,  0x0000},
	{&sysmmnoc_clk.c, 0x0001},
	{&pcnoc_clk.c, 0x0008},
	/* BIMC_CLK is 2x clock to the BIMC Core as well as DDR.
	 * The BIMC Clock gcc_bimc_clk is 1/2 of BIMC_CLK, and
	 * gcc_bimc_apss_axi_clk s not in-sync with gcc_bimc_clk,
	 * so measure gcc_bimc_clk.
	 */
	{&bimc_clk.c,  0x0154},
	),
	.c = {
		.dbg_name = "rpm_debug_mux",
		.ops = &clk_ops_gen_mux,
		.flags = CLKFLAG_NO_RATE_CACHE,
		CLK_INIT(rpm_debug_mux.c),
	},
};

/* Lookup Table */
static struct clk_lookup msm_clocks_rpm[] = {
	CLK_LIST(xo_clk_src),
	CLK_LIST(xo_a_clk_src),
	CLK_LIST(xo_otg_clk),
	CLK_LIST(xo_lpm_clk),
	CLK_LIST(xo_pil_mss_clk),
	CLK_LIST(xo_pil_pronto_clk),
	CLK_LIST(xo_wlan_clk),

	CLK_LIST(snoc_msmbus_clk),
	CLK_LIST(snoc_msmbus_a_clk),
	CLK_LIST(sysmmnoc_msmbus_clk),
	CLK_LIST(sysmmnoc_msmbus_a_clk),
	CLK_LIST(pcnoc_msmbus_clk),
	CLK_LIST(pcnoc_msmbus_a_clk),
	CLK_LIST(bimc_msmbus_clk),
	CLK_LIST(bimc_msmbus_a_clk),
	CLK_LIST(pcnoc_keepalive_a_clk),

	CLK_LIST(pcnoc_usb_a_clk),
	CLK_LIST(snoc_usb_a_clk),
	CLK_LIST(bimc_usb_a_clk),

	/* CoreSight clocks */
	CLK_LIST(qdss_clk),
	CLK_LIST(qdss_a_clk),

	CLK_LIST(snoc_clk),
	CLK_LIST(sysmmnoc_clk),
	CLK_LIST(pcnoc_clk),
	CLK_LIST(bimc_clk),
	CLK_LIST(snoc_a_clk),
	CLK_LIST(sysmmnoc_a_clk),
	CLK_LIST(pcnoc_a_clk),
	CLK_LIST(bimc_a_clk),

	CLK_LIST(bb_clk1),
	CLK_LIST(bb_clk1_a),
	CLK_LIST(bb_clk2),
	CLK_LIST(bb_clk2_a),
	CLK_LIST(rf_clk2),
	CLK_LIST(rf_clk2_a),

	CLK_LIST(bb_clk1_pin),
	CLK_LIST(bb_clk1_a_pin),
	CLK_LIST(bb_clk2_pin),
	CLK_LIST(bb_clk2_a_pin),
	CLK_LIST(rf_clk2_pin),
	CLK_LIST(rf_clk2_a_pin),

	/* RPM debug Mux*/
	CLK_LIST(rpm_debug_mux),
};

static int msm_rpmcc_8936_probe(struct platform_device *pdev)
{
	struct resource *res;
	int ret;

	ret = enable_rpm_scaling();
	if (ret < 0)
		return ret;

	res =  platform_get_resource_byname(pdev, IORESOURCE_MEM, "cc_base");
	if (!res) {
		dev_err(&pdev->dev, "Unable to get register base\n");
		return -ENOMEM;
	}

	virt_base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!virt_base) {
		dev_err(&pdev->dev, "Failed to map CC registers\n");
		return -ENOMEM;
	}

	ret = of_msm_clock_register(pdev->dev.of_node, msm_clocks_rpm,
				ARRAY_SIZE(msm_clocks_rpm));
	if (ret)
		return ret;

	/*
	 *  Hold an active set vote for PCNOC AHB source. Sleep set vote is 0.
	 */
	clk_set_rate(&pcnoc_keepalive_a_clk.c, 19200000);
	clk_prepare_enable(&pcnoc_keepalive_a_clk.c);

	clk_prepare_enable(&xo_a_clk_src.c);

	dev_info(&pdev->dev, "Registered RPM clocks.\n");

	return 0;
}

static struct of_device_id msm_clk_rpm_match_table[] = {
	{ .compatible = "qcom,rpmcc-8936" },
	{}
};

static struct platform_driver msm_clock_rpm_driver = {
	.probe = msm_rpmcc_8936_probe,
	.driver = {
		.name = "qcom,rpmcc-8936",
		.of_match_table = msm_clk_rpm_match_table,
		.owner = THIS_MODULE,
	},
};

int __init msm_rpmcc_8936_init(void)
{
	return platform_driver_register(&msm_clock_rpm_driver);
}
arch_initcall(msm_rpmcc_8936_init);
