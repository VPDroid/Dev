/* ehci-msm2.c - HSUSB Host Controller Driver Implementation
 *
 * Copyright (c) 2008-2014, The Linux Foundation. All rights reserved.
 *
 * Partly derived from ehci-fsl.c and ehci-hcd.c
 * Copyright (c) 2000-2004 by David Brownell
 * Copyright (c) 2005 MontaVista Software
 *
 * All source code in this file is licensed under the following license except
 * where indicated.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can find it at http://www.fsf.org
 */

#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/pm_wakeup.h>
#include <linux/pm_runtime.h>
#include <linux/dma-mapping.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/irq.h>
#include <linux/clk/msm-clk.h>
#include <linux/msm-bus.h>

#include <linux/usb/ulpi.h>
#include <linux/usb/msm_hsusb_hw.h>
#include <linux/usb/msm_hsusb.h>
#include <linux/usb/phy.h>
#include <linux/of.h>

#include <linux/debugfs.h>

#include "ehci.h"

#define DRIVER_DESC "Qualcomm EHCI Host Controller"

static const char hcd_name[] = "ehci-msm2";

#define MSM_USB_BASE (hcd->regs)

#define PDEV_NAME_LEN 20

static bool uicc_card_present;
module_param(uicc_card_present, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(uicc_card_present, "UICC card inserted");

struct msm_hcd {
	struct ehci_hcd				ehci;
	spinlock_t				wakeup_lock;
	struct device				*dev;
	struct clk				*xo_clk;
	struct clk				*iface_clk;
	struct clk				*core_clk;
	long                                    core_clk_rate;
	struct clk				*alt_core_clk;
	struct clk				*phy_sleep_clk;
	struct regulator			*hsusb_vddcx;
	struct regulator			*hsusb_3p3;
	struct regulator			*hsusb_1p8;
	struct regulator			*vbus;
	bool					vbus_on;
	atomic_t				in_lpm;
	int					pmic_gpio_dp_irq;
	bool					pmic_gpio_dp_irq_enabled;
	uint32_t				pmic_gpio_int_cnt;
	atomic_t				pm_usage_cnt;
	struct wakeup_source			ws;
	struct work_struct			phy_susp_fail_work;
	int					async_irq;
	bool					async_irq_enabled;
	uint32_t				async_int_cnt;
	int					resume_gpio;

	int					wakeup_int_cnt;
	bool					wakeup_irq_enabled;
	int					wakeup_irq;
	void __iomem				*usb_phy_ctrl_reg;
	struct pinctrl				*hsusb_pinctrl;
	struct pm_qos_request			pm_qos_req_dma;
	struct msm_bus_scale_pdata		*bus_scale_table;
	u32					bus_perf_client;
};

static inline struct msm_hcd *hcd_to_mhcd(struct usb_hcd *hcd)
{
	return (struct msm_hcd *) (hcd->hcd_priv);
}

static inline struct usb_hcd *mhcd_to_hcd(struct msm_hcd *mhcd)
{
	return container_of((void *) mhcd, struct usb_hcd, hcd_priv);
}

#define HSUSB_PHY_3P3_VOL_MIN		3050000 /* uV */
#define HSUSB_PHY_3P3_VOL_MAX		3300000 /* uV */
#define HSUSB_PHY_3P3_HPM_LOAD		50000	/* uA */

#define HSUSB_PHY_1P8_VOL_MIN		1800000 /* uV */
#define HSUSB_PHY_1P8_VOL_MAX		1800000 /* uV */
#define HSUSB_PHY_1P8_HPM_LOAD		50000	/* uA */

#define HSUSB_PHY_VDD_DIG_VOL_NONE	0	/* uV */
#define HSUSB_PHY_VDD_DIG_VOL_MIN	1045000	/* uV */
#define HSUSB_PHY_VDD_DIG_VOL_MAX	1320000	/* uV */
#define HSUSB_PHY_VDD_DIG_LOAD		49360	/* uA */

#define HSUSB_PHY_SUSP_DIG_VOL_P50  500000
#define HSUSB_PHY_SUSP_DIG_VOL_P75  750000
enum hsusb_vdd_value {
	VDD_MIN_NONE = 0,
	VDD_MIN_P50,
	VDD_MIN_P75,
	VDD_MIN_OP,
	VDD_MAX_OP,
	VDD_VAL_MAX_OP,
};

static int hsusb_vdd_val[VDD_VAL_MAX_OP];

static int msm_ehci_init_vddcx(struct msm_hcd *mhcd, int init)
{
	int ret = 0;
	int none_vol, min_vol, max_vol;
	u32 tmp[5];
	int len = 0;

	if (!init) {
		none_vol = hsusb_vdd_val[VDD_MIN_NONE];
		max_vol = hsusb_vdd_val[VDD_MAX_OP];
		goto disable_reg;
	}

	mhcd->hsusb_vddcx = devm_regulator_get(mhcd->dev, "hsusb_vdd_dig");
	if (IS_ERR(mhcd->hsusb_vddcx)) {
		mhcd->hsusb_vddcx = devm_regulator_get(mhcd->dev,
							"HSUSB_VDDCX");
		if (IS_ERR(mhcd->hsusb_vddcx)) {
			dev_err(mhcd->dev, "unable to get ehci vddcx\n");
			return PTR_ERR(mhcd->hsusb_vddcx);
		}
	}

	if (of_get_property(mhcd->dev->of_node,
			"qcom,vdd-voltage-level",
			&len)) {
		if (len == sizeof(tmp)) {
			of_property_read_u32_array(mhcd->dev->of_node,
				"qcom,vdd-voltage-level",
				tmp, len/sizeof(*tmp));
			hsusb_vdd_val[VDD_MIN_NONE] = tmp[0];
			hsusb_vdd_val[VDD_MIN_P50] = tmp[1];
			hsusb_vdd_val[VDD_MIN_P75] = tmp[2];
			hsusb_vdd_val[VDD_MIN_OP] = tmp[3];
			hsusb_vdd_val[VDD_MAX_OP] = tmp[4];
		} else {
			dev_dbg(mhcd->dev, "Use default vdd config\n");
			return -ENODEV;
		}
	} else {
		return -ENODEV;
	}

	none_vol = hsusb_vdd_val[VDD_MIN_NONE];
	min_vol = hsusb_vdd_val[VDD_MIN_OP];
	max_vol = hsusb_vdd_val[VDD_MAX_OP];

	ret = regulator_set_voltage(mhcd->hsusb_vddcx, min_vol, max_vol);
	if (ret) {
		dev_err(mhcd->dev, "unable to set the voltage"
				"for ehci vddcx\n");
		return ret;
	}

	ret = regulator_set_optimum_mode(mhcd->hsusb_vddcx,
				HSUSB_PHY_VDD_DIG_LOAD);
	if (ret < 0) {
		dev_err(mhcd->dev, "%s: Unable to set optimum mode of the"
				" regulator: VDDCX\n", __func__);
		goto reg_optimum_mode_err;
	}

	ret = regulator_enable(mhcd->hsusb_vddcx);
	if (ret) {
		dev_err(mhcd->dev, "unable to enable ehci vddcx\n");
		goto reg_enable_err;
	}

	return 0;

disable_reg:
	regulator_disable(mhcd->hsusb_vddcx);
reg_enable_err:
	regulator_set_optimum_mode(mhcd->hsusb_vddcx, 0);
reg_optimum_mode_err:
	regulator_set_voltage(mhcd->hsusb_vddcx, none_vol, max_vol);
	return ret;

}

static int msm_ehci_ldo_init(struct msm_hcd *mhcd, int init)
{
	int rc = 0;

	if (!init)
		goto put_1p8;

	mhcd->hsusb_3p3 = devm_regulator_get(mhcd->dev, "HSUSB_3p3");
	if (IS_ERR(mhcd->hsusb_3p3)) {
		dev_err(mhcd->dev, "unable to get hsusb 3p3\n");
		return PTR_ERR(mhcd->hsusb_3p3);
	}

	rc = regulator_set_voltage(mhcd->hsusb_3p3,
			HSUSB_PHY_3P3_VOL_MIN, HSUSB_PHY_3P3_VOL_MAX);
	if (rc) {
		dev_err(mhcd->dev, "unable to set voltage level for"
				"hsusb 3p3\n");
		return rc;
	}
	mhcd->hsusb_1p8 = devm_regulator_get(mhcd->dev, "HSUSB_1p8");
	if (IS_ERR(mhcd->hsusb_1p8)) {
		dev_err(mhcd->dev, "unable to get hsusb 1p8\n");
		rc = PTR_ERR(mhcd->hsusb_1p8);
		goto put_3p3_lpm;
	}
	rc = regulator_set_voltage(mhcd->hsusb_1p8,
			HSUSB_PHY_1P8_VOL_MIN, HSUSB_PHY_1P8_VOL_MAX);
	if (rc) {
		dev_err(mhcd->dev, "unable to set voltage level for"
				"hsusb 1p8\n");
		goto put_1p8;
	}

	return 0;

put_1p8:
	regulator_set_voltage(mhcd->hsusb_1p8, 0, HSUSB_PHY_1P8_VOL_MAX);
put_3p3_lpm:
	regulator_set_voltage(mhcd->hsusb_3p3, 0, HSUSB_PHY_3P3_VOL_MAX);

	return rc;
}

#ifdef CONFIG_PM_SLEEP
static int msm_ehci_config_vddcx(struct msm_hcd *mhcd, int high)
{
	struct msm_usb_host_platform_data *pdata;
	int max_vol = hsusb_vdd_val[VDD_MAX_OP];
	int min_vol;
	int ret;

	pdata = mhcd->dev->platform_data;

	if (high)
		min_vol = hsusb_vdd_val[VDD_MIN_OP];
	else if (pdata && pdata->dock_connect_irq &&
			!irq_read_line(pdata->dock_connect_irq))
		min_vol = hsusb_vdd_val[VDD_MIN_P75];
	else
		min_vol = hsusb_vdd_val[VDD_MIN_P50];

	ret = regulator_set_voltage(mhcd->hsusb_vddcx, min_vol, max_vol);
	if (ret) {
		dev_err(mhcd->dev, "%s: unable to set the voltage of regulator"
			" HSUSB_VDDCX\n", __func__);
		return ret;
	}

	dev_dbg(mhcd->dev, "%s: min_vol:%d max_vol:%d\n", __func__, min_vol,
								max_vol);

	return ret;
}
#else
static int msm_ehci_config_vddcx(struct msm_hcd *mhcd, int high)
{
	return 0;
}
#endif

static void msm_ehci_vbus_power(struct msm_hcd *mhcd, bool on)
{
	int ret;

	if (!mhcd->vbus)
		return;

	if (mhcd->vbus_on == on)
		return;

	if (on) {
		ret = regulator_enable(mhcd->vbus);
		if (ret) {
			pr_err("unable to enable vbus\n");
			return;
		}
		mhcd->vbus_on = true;
	} else {
		ret = regulator_disable(mhcd->vbus);
		if (ret) {
			pr_err("unable to disable vbus\n");
			return;
		}
		mhcd->vbus_on = false;
	}
}

static irqreturn_t msm_ehci_dock_connect_irq(int irq, void *data)
{
	const struct msm_usb_host_platform_data *pdata;
	struct msm_hcd *mhcd = data;
	struct usb_hcd *hcd = mhcd_to_hcd(mhcd);

	pdata = mhcd->dev->platform_data;

	if (atomic_read(&mhcd->in_lpm))
		usb_hcd_resume_root_hub(hcd);

	if (irq_read_line(pdata->dock_connect_irq)) {
		dev_dbg(mhcd->dev, "%s:Dock removed disable vbus\n", __func__);
		msm_ehci_vbus_power(mhcd, 0);
	} else {
		dev_dbg(mhcd->dev, "%s:Dock connected enable vbus\n", __func__);
		msm_ehci_vbus_power(mhcd, 1);
	}

	return IRQ_HANDLED;
}

static int msm_ehci_init_vbus(struct msm_hcd *mhcd, int init)
{
	int rc = 0;
	struct usb_hcd *hcd = mhcd_to_hcd(mhcd);
	const struct msm_usb_host_platform_data *pdata;

	pdata = mhcd->dev->platform_data;

	if (!init) {
		if (pdata && pdata->dock_connect_irq)
			free_irq(pdata->dock_connect_irq, mhcd);
		return rc;
	}

	mhcd->vbus = devm_regulator_get(mhcd->dev, "vbus");
	if (PTR_ERR(mhcd->vbus) == -EPROBE_DEFER) {
		dev_dbg(mhcd->dev, "failed to get vbus handle, defer probe\n");
		return -EPROBE_DEFER;
	} else {
		dev_dbg(mhcd->dev, "vbus-supply not specified\n");
		mhcd->vbus = NULL;
	}

	if (pdata) {
		hcd->power_budget = pdata->power_budget;

		if (pdata->dock_connect_irq) {
			rc = request_threaded_irq(pdata->dock_connect_irq, NULL,
					msm_ehci_dock_connect_irq,
					IRQF_TRIGGER_FALLING |
					IRQF_TRIGGER_RISING |
					IRQF_ONESHOT, "msm_ehci_host", mhcd);
			if (!rc)
				enable_irq_wake(pdata->dock_connect_irq);
		}
	}
	return rc;
}

static int msm_ehci_ldo_enable(struct msm_hcd *mhcd, int on)
{
	int ret = 0;

	if (IS_ERR(mhcd->hsusb_1p8)) {
		dev_err(mhcd->dev, "%s: HSUSB_1p8 is not initialized\n",
								__func__);
		return -ENODEV;
	}

	if (IS_ERR(mhcd->hsusb_3p3)) {
		dev_err(mhcd->dev, "%s: HSUSB_3p3 is not initialized\n",
								__func__);
		return -ENODEV;
	}

	if (on) {
		ret = regulator_set_optimum_mode(mhcd->hsusb_1p8,
						HSUSB_PHY_1P8_HPM_LOAD);
		if (ret < 0) {
			dev_err(mhcd->dev, "%s: Unable to set HPM of the"
				" regulator: HSUSB_1p8\n", __func__);
			return ret;
		}

		ret = regulator_enable(mhcd->hsusb_1p8);
		if (ret) {
			dev_err(mhcd->dev, "%s: unable to enable the hsusb"
						" 1p8\n", __func__);
			regulator_set_optimum_mode(mhcd->hsusb_1p8, 0);
			return ret;
		}

		ret = regulator_set_optimum_mode(mhcd->hsusb_3p3,
						HSUSB_PHY_3P3_HPM_LOAD);
		if (ret < 0) {
			dev_err(mhcd->dev, "%s: Unable to set HPM of the "
				"regulator: HSUSB_3p3\n", __func__);
			regulator_set_optimum_mode(mhcd->hsusb_1p8, 0);
			regulator_disable(mhcd->hsusb_1p8);
			return ret;
		}

		ret = regulator_enable(mhcd->hsusb_3p3);
		if (ret) {
			dev_err(mhcd->dev, "%s: unable to enable the "
					"hsusb 3p3\n", __func__);
			regulator_set_optimum_mode(mhcd->hsusb_3p3, 0);
			regulator_set_optimum_mode(mhcd->hsusb_1p8, 0);
			regulator_disable(mhcd->hsusb_1p8);
			return ret;
		}

	} else {
		ret = regulator_disable(mhcd->hsusb_1p8);
		if (ret) {
			dev_err(mhcd->dev, "%s: unable to disable the "
					"hsusb 1p8\n", __func__);
			return ret;
		}

		ret = regulator_set_optimum_mode(mhcd->hsusb_1p8, 0);
		if (ret < 0)
			dev_err(mhcd->dev, "%s: Unable to set LPM of the "
				"regulator: HSUSB_1p8\n", __func__);

		ret = regulator_disable(mhcd->hsusb_3p3);
		if (ret) {
			dev_err(mhcd->dev, "%s: unable to disable the "
					"hsusb 3p3\n", __func__);
			return ret;
		}
		ret = regulator_set_optimum_mode(mhcd->hsusb_3p3, 0);
		if (ret < 0)
			dev_err(mhcd->dev, "%s: Unable to set LPM of the "
					"regulator: HSUSB_3p3\n", __func__);
	}

	dev_dbg(mhcd->dev, "reg (%s)\n", on ? "HPM" : "LPM");

	return ret < 0 ? ret : 0;
}


#define ULPI_IO_TIMEOUT_USECS	(10 * 1000)
static int msm_ulpi_read(struct msm_hcd *mhcd, u32 reg)
{
	struct usb_hcd *hcd = mhcd_to_hcd(mhcd);
	unsigned long timeout;

	/* initiate read operation */
	writel_relaxed(ULPI_RUN | ULPI_READ | ULPI_ADDR(reg),
	       USB_ULPI_VIEWPORT);

	/* wait for completion */
	timeout = jiffies + usecs_to_jiffies(ULPI_IO_TIMEOUT_USECS);
	while (readl_relaxed(USB_ULPI_VIEWPORT) & ULPI_RUN) {
		if (time_after(jiffies, timeout)) {
			dev_err(mhcd->dev, "msm_ulpi_read: timeout %08x\n",
				readl_relaxed(USB_ULPI_VIEWPORT));
			dev_err(mhcd->dev, "PORTSC: %08x USBCMD: %08x\n",
				readl_relaxed(USB_PORTSC),
				readl_relaxed(USB_USBCMD));
			return -ETIMEDOUT;
		}
		udelay(1);
	}

	return ULPI_DATA_READ(readl_relaxed(USB_ULPI_VIEWPORT));
}


static int msm_ulpi_write(struct msm_hcd *mhcd, u32 val, u32 reg)
{
	struct usb_hcd *hcd = mhcd_to_hcd(mhcd);
	unsigned long timeout;

	/* initiate write operation */
	writel_relaxed(ULPI_RUN | ULPI_WRITE |
	       ULPI_ADDR(reg) | ULPI_DATA(val),
	       USB_ULPI_VIEWPORT);

	/* wait for completion */
	timeout = jiffies + usecs_to_jiffies(ULPI_IO_TIMEOUT_USECS);
	while (readl_relaxed(USB_ULPI_VIEWPORT) & ULPI_RUN) {
		if (time_after(jiffies, timeout)) {
			dev_err(mhcd->dev, "msm_ulpi_write: timeout\n");
			dev_err(mhcd->dev, "PORTSC: %08x USBCMD: %08x\n",
				readl_relaxed(USB_PORTSC),
				readl_relaxed(USB_USBCMD));
			return -ETIMEDOUT;
		}
		udelay(1);
	}

	return 0;
}

/**
 * Do hard reset to USB hardware block using one of reset methodology based
 * on availablity of alt_core_clk. There are two kinds of hardware resets.
 * 1. Conventional synchronous reset where clocks to blocks to be ON while
 * issuing the reset. 2. Asynchronous reset which requires clocks to be OFF.
 */
static int msm_ehci_link_clk_reset(struct msm_hcd *mhcd, bool assert)
{
	int ret;

	if (assert) {
		if (mhcd->alt_core_clk) {
			ret = clk_reset(mhcd->alt_core_clk, CLK_RESET_ASSERT);
		} else {
			/* Using asynchronous block reset to the hardware */
			clk_disable_unprepare(mhcd->iface_clk);
			clk_disable_unprepare(mhcd->core_clk);
			ret = clk_reset(mhcd->core_clk, CLK_RESET_ASSERT);
		}
		if (ret)
			dev_err(mhcd->dev, "usb clk assert failed\n");
	} else {
		if (mhcd->alt_core_clk) {
			ret = clk_reset(mhcd->alt_core_clk, CLK_RESET_DEASSERT);
		} else {
			ret = clk_reset(mhcd->core_clk, CLK_RESET_DEASSERT);
			ndelay(200);
			clk_prepare_enable(mhcd->core_clk);
			clk_prepare_enable(mhcd->iface_clk);
		}
		if (ret)
			dev_err(mhcd->dev, "usb clk deassert failed\n");
	}

	return ret;
}

static int msm_ehci_phy_reset(struct msm_hcd *mhcd)
{
	struct usb_hcd *hcd = mhcd_to_hcd(mhcd);
	struct msm_usb_host_platform_data *pdata;
	u32 val;
	int ret;

	ret = msm_ehci_link_clk_reset(mhcd, 1);
	if (ret)
		return ret;

	/* Minimum 10msec delay for block reset as per hardware spec */
	usleep_range(10000, 12000);

	ret = msm_ehci_link_clk_reset(mhcd, 0);
	if (ret)
		return ret;

	pdata = mhcd->dev->platform_data;
	if (pdata && pdata->use_sec_phy)
		/* select secondary phy if offset is set for USB operation */
		writel_relaxed(readl_relaxed(USB_PHY_CTRL2) | (1<<16),
								USB_PHY_CTRL2);
	val = readl_relaxed(USB_PORTSC) & ~PORTSC_PTS_MASK;
	writel_relaxed(val | PORTSC_PTS_ULPI, USB_PORTSC);

	dev_info(mhcd->dev, "phy_reset: success\n");

	return 0;
}

static void msm_usb_phy_reset(struct msm_hcd *mhcd)
{
	u32 val;

	/* Assert USB PHY_PON */
	val =  readl_relaxed(mhcd->usb_phy_ctrl_reg);
	val &= ~PHY_POR_BIT_MASK;
	val |= PHY_POR_ASSERT;
	writel_relaxed(val, mhcd->usb_phy_ctrl_reg);

	/* wait for minimum 10 microseconds as suggested in hardware spec */
	usleep_range(10, 15);

	/* Deassert USB PHY_PON */
	val =  readl_relaxed(mhcd->usb_phy_ctrl_reg);
	val &= ~PHY_POR_BIT_MASK;
	val |= PHY_POR_DEASSERT;
	writel_relaxed(val, mhcd->usb_phy_ctrl_reg);

	/* Ensure that RESET operation is completed. */
	mb();
}

#define LINK_RESET_TIMEOUT_USEC		(250 * 1000)
static int msm_hsusb_reset(struct msm_hcd *mhcd)
{
	struct usb_hcd *hcd = mhcd_to_hcd(mhcd);
	struct msm_usb_host_platform_data *pdata;
	unsigned long timeout;
	int ret;

	if (mhcd->alt_core_clk)
		clk_prepare_enable(mhcd->alt_core_clk);

	ret = msm_ehci_phy_reset(mhcd);
	if (ret) {
		dev_err(mhcd->dev, "phy_reset failed\n");
		return ret;
	}

	writel_relaxed(USBCMD_RESET, USB_USBCMD);

	timeout = jiffies + usecs_to_jiffies(LINK_RESET_TIMEOUT_USEC);
	while (readl_relaxed(USB_USBCMD) & USBCMD_RESET) {
		if (time_after(jiffies, timeout))
			return -ETIMEDOUT;
		udelay(1);
	}

	/* select ULPI phy */
	writel_relaxed(0x80000000, USB_PORTSC);

	pdata = mhcd->dev->platform_data;
	if (pdata && pdata->use_sec_phy)
		writel_relaxed(readl_relaxed(USB_PHY_CTRL2) | (1<<16),
								USB_PHY_CTRL2);

	/* Reset USB PHY after performing USB Link RESET */
	if (hcd->phy) {
		usb_phy_reset(hcd->phy);
		usb_phy_init(hcd->phy);
	} else {
		msm_usb_phy_reset(mhcd);
	}

	msleep(100);

	writel_relaxed(0x0, USB_AHBBURST);
	writel_relaxed(0x08, USB_AHBMODE);

	/* Ensure that RESET operation is completed before turning off clock */
	mb();

	if (mhcd->alt_core_clk)
		clk_disable_unprepare(mhcd->alt_core_clk);

	/*rising edge interrupts with Dp rise and fall enabled*/
	msm_ulpi_write(mhcd, ULPI_INT_DP, ULPI_USB_INT_EN_RISE);
	msm_ulpi_write(mhcd, ULPI_INT_DP, ULPI_USB_INT_EN_FALL);

	/*Clear the PHY interrupts by reading the PHY interrupt latch register*/
	msm_ulpi_read(mhcd, ULPI_USB_INT_LATCH);

	return 0;
}

static void msm_ehci_phy_susp_fail_work(struct work_struct *w)
{
	struct msm_hcd *mhcd = container_of(w, struct msm_hcd,
					phy_susp_fail_work);
	struct usb_hcd *hcd = mhcd_to_hcd(mhcd);

	pm_runtime_disable(mhcd->dev);

	msm_ehci_vbus_power(mhcd, 0);
	usb_remove_hcd(hcd);
	msm_hsusb_reset(mhcd);
	usb_add_hcd(hcd, hcd->irq, IRQF_SHARED);
	msm_ehci_vbus_power(mhcd, 1);

	pm_runtime_set_active(mhcd->dev);
	pm_runtime_enable(mhcd->dev);
}

#define PHY_SUSP_TIMEOUT_MSEC	500
#define PHY_RESUME_TIMEOUT_USEC	(100 * 1000)

#ifdef CONFIG_PM_SLEEP
static int msm_ehci_suspend(struct msm_hcd *mhcd)
{
	struct usb_hcd *hcd = mhcd_to_hcd(mhcd);
	unsigned long timeout;
	u32 portsc;
	const struct msm_usb_host_platform_data *pdata;
	u32 func_ctrl;

	if (atomic_read(&mhcd->in_lpm)) {
		dev_dbg(mhcd->dev, "%s called in lpm\n", __func__);
		return 0;
	}

	disable_irq(hcd->irq);

	/* make sure we don't race against a remote wakeup */
	if (test_bit(HCD_FLAG_WAKEUP_PENDING, &hcd->flags) ||
	    readl_relaxed(USB_PORTSC) & PORT_RESUME) {
		dev_dbg(mhcd->dev, "wakeup pending, aborting suspend\n");
		enable_irq(hcd->irq);
		return -EBUSY;
	}

	pdata = mhcd->dev->platform_data;
	if (pdata && pdata->is_uicc) {
		/* put the controller in non-driving mode */
		func_ctrl = msm_ulpi_read(mhcd, ULPI_FUNC_CTRL);
		func_ctrl &= ~ULPI_FUNC_CTRL_OPMODE_MASK;
		func_ctrl |= ULPI_FUNC_CTRL_OPMODE_NONDRIVING;
		msm_ulpi_write(mhcd, func_ctrl, ULPI_FUNC_CTRL);
	}

	/*
	 * Set the PHCD bit, only if it is not set by the controller.
	 * PHY may take some time or even fail to enter into low power
	 * mode (LPM). Hence poll for 500 msec and reset the PHY and link
	 * in failure case.
	 */
	portsc = readl_relaxed(USB_PORTSC);
	if (!(portsc & PORTSC_PHCD)) {
		writel_relaxed(portsc | PORTSC_PHCD, USB_PORTSC);

		timeout = jiffies + msecs_to_jiffies(PHY_SUSP_TIMEOUT_MSEC);
		while (!(readl_relaxed(USB_PORTSC) & PORTSC_PHCD)) {
			if (time_after(jiffies, timeout)) {
				dev_err(mhcd->dev,
					"Unable to suspend PHY. portsc: %8x\n",
					readl_relaxed(USB_PORTSC));
				goto reset_phy_and_link;
			}
			usleep_range(10000, 10000);
		}
	}

	/* Suspend QUSB2 PHY */
	usb_phy_set_suspend(hcd->phy, 1);

	/*
	 * PHY has capability to generate interrupt asynchronously in low
	 * power mode (LPM). This interrupt is level triggered. So USB IRQ
	 * line must be disabled till async interrupt enable bit is cleared
	 * in USBCMD register. Assert STP (ULPI interface STOP signal) to
	 * block data communication from PHY.  Enable asynchronous interrupt
	 * only when wakeup gpio IRQ is not present.
	 */
	if (mhcd->wakeup_irq)
		writel_relaxed(readl_relaxed(USB_USBCMD) | ULPI_STP_CTRL,
				USB_USBCMD);
	else
		writel_relaxed(readl_relaxed(USB_USBCMD) | ASYNC_INTR_CTRL |
				ULPI_STP_CTRL, USB_USBCMD);

	/*
	 * Ensure that hardware is put in low power mode before
	 * clocks are turned OFF and VDD is allowed to minimize.
	 */
	mb();

	clk_disable_unprepare(mhcd->iface_clk);
	clk_disable_unprepare(mhcd->core_clk);

	/* usb phy does not require TCXO clock, hence vote for TCXO disable */
	if (mhcd->xo_clk)
		clk_disable_unprepare(mhcd->xo_clk);
	clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);

	msm_ehci_config_vddcx(mhcd, 0);

	atomic_set(&mhcd->in_lpm, 1);
	enable_irq(hcd->irq);

	if (mhcd->wakeup_irq) {
		mhcd->wakeup_irq_enabled = 1;
		enable_irq_wake(mhcd->wakeup_irq);
		enable_irq(mhcd->wakeup_irq);
	}

	if (mhcd->pmic_gpio_dp_irq) {
		mhcd->pmic_gpio_dp_irq_enabled = 1;
		enable_irq_wake(mhcd->pmic_gpio_dp_irq);
		enable_irq(mhcd->pmic_gpio_dp_irq);
	}
	if (mhcd->async_irq) {
		mhcd->async_irq_enabled = 1;
		enable_irq_wake(mhcd->async_irq);
		enable_irq(mhcd->async_irq);
	}

	if (mhcd->bus_perf_client) {
		int ret = msm_bus_scale_client_update_request(
						mhcd->bus_perf_client, 0);
		if (ret)
			dev_err(mhcd->dev, "Failed to vote for bus scaling\n");
	}

	pm_qos_update_request(&mhcd->pm_qos_req_dma, PM_QOS_DEFAULT_VALUE);

	pm_relax(mhcd->dev);

	dev_info(mhcd->dev, "EHCI USB in low power mode\n");

	return 0;

reset_phy_and_link:
	schedule_work(&mhcd->phy_susp_fail_work);
	return -ETIMEDOUT;
}

static int msm_ehci_resume(struct msm_hcd *mhcd)
{
	struct usb_hcd *hcd = mhcd_to_hcd(mhcd);
	unsigned long timeout;
	unsigned temp;
	unsigned long flags;
	u32 func_ctrl;
	const struct msm_usb_host_platform_data *pdata;

	if (!atomic_read(&mhcd->in_lpm)) {
		dev_dbg(mhcd->dev, "%s called in !in_lpm\n", __func__);
		return 0;
	}

	/* Handles race with Async interrupt */
	disable_irq(hcd->irq);

	if (mhcd->pmic_gpio_dp_irq_enabled) {
		disable_irq_wake(mhcd->pmic_gpio_dp_irq);
		disable_irq_nosync(mhcd->pmic_gpio_dp_irq);
		mhcd->pmic_gpio_dp_irq_enabled = 0;
	}

	spin_lock_irqsave(&mhcd->wakeup_lock, flags);
	if (mhcd->async_irq_enabled) {
		disable_irq_wake(mhcd->async_irq);
		disable_irq_nosync(mhcd->async_irq);
		mhcd->async_irq_enabled = 0;
	}

	if (mhcd->wakeup_irq) {
		if (mhcd->wakeup_irq_enabled) {
			disable_irq_wake(mhcd->wakeup_irq);
			disable_irq_nosync(mhcd->wakeup_irq);
			mhcd->wakeup_irq_enabled = 0;
		}
	}
	spin_unlock_irqrestore(&mhcd->wakeup_lock, flags);

	pm_stay_awake(mhcd->dev);

	pdata = mhcd->dev->platform_data;
	if (pdata)
		pm_qos_update_request(&mhcd->pm_qos_req_dma,
			pdata->pm_qos_latency + 1);

	if (mhcd->bus_perf_client) {
		int ret = msm_bus_scale_client_update_request(
						mhcd->bus_perf_client, 1);
		if (ret)
			dev_err(mhcd->dev, "Failed to vote for bus scaling\n");
	}

	/* Vote for TCXO when waking up the phy */
	if (mhcd->xo_clk)
		clk_prepare_enable(mhcd->xo_clk);

	clk_prepare_enable(mhcd->core_clk);
	clk_prepare_enable(mhcd->iface_clk);

	msm_ehci_config_vddcx(mhcd, 1);

	temp = readl_relaxed(USB_USBCMD);
	temp &= ~ASYNC_INTR_CTRL;
	temp &= ~ULPI_STP_CTRL;
	writel_relaxed(temp, USB_USBCMD);

	/* Resume QUSB2 PHY */
	usb_phy_set_suspend(hcd->phy, 0);

	if (!(readl_relaxed(USB_PORTSC) & PORTSC_PHCD))
		goto skip_phy_resume;

	temp = readl_relaxed(USB_PORTSC) & ~PORTSC_PHCD;
	temp &= ~PORT_RWC_BITS;
	temp |= PORT_RESUME;
	writel_relaxed(temp, USB_PORTSC);

	timeout = jiffies + usecs_to_jiffies(PHY_RESUME_TIMEOUT_USEC);
	while ((readl_relaxed(USB_PORTSC) & PORTSC_PHCD) ||
			!(readl_relaxed(USB_ULPI_VIEWPORT) & ULPI_SYNC_STATE)) {
		if (time_after(jiffies, timeout)) {
			/*This is a fatal error. Reset the link and PHY*/
			dev_err(mhcd->dev, "Unable to resume USB. Resetting the h/w\n");
			msm_hsusb_reset(mhcd);
			break;
		}
		udelay(1);
	}

skip_phy_resume:
	if (pdata->is_uicc) {
		/* put the controller in normal mode */
		func_ctrl = msm_ulpi_read(mhcd, ULPI_FUNC_CTRL);
		func_ctrl &= ~ULPI_FUNC_CTRL_OPMODE_MASK;
		func_ctrl |= ULPI_FUNC_CTRL_OPMODE_NORMAL;
		msm_ulpi_write(mhcd, func_ctrl, ULPI_FUNC_CTRL);
	}

	set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
	usb_hcd_resume_root_hub(hcd);
	atomic_set(&mhcd->in_lpm, 0);

	if (atomic_read(&mhcd->pm_usage_cnt)) {
		atomic_set(&mhcd->pm_usage_cnt, 0);
		pm_runtime_put_noidle(mhcd->dev);
	}

	enable_irq(hcd->irq);
	dev_info(mhcd->dev, "EHCI USB exited from low power mode\n");

	return 0;
}
#endif

static irqreturn_t msm_async_irq(int irq, void *data)
{
	struct msm_hcd *mhcd = data;
	int ret;

	mhcd->async_int_cnt++;
	dev_dbg(mhcd->dev, "%s: hsusb host remote wakeup interrupt cnt: %u\n",
			__func__, mhcd->async_int_cnt);

	pm_stay_awake(mhcd->dev);

	spin_lock(&mhcd->wakeup_lock);
	if (mhcd->async_irq_enabled) {
		mhcd->async_irq_enabled = 0;
		disable_irq_wake(irq);
		disable_irq_nosync(irq);
	}
	spin_unlock(&mhcd->wakeup_lock);

	if (!atomic_read(&mhcd->pm_usage_cnt)) {
		ret = pm_runtime_get(mhcd->dev);
		if ((ret == 1) || (ret == -EINPROGRESS))
			pm_runtime_put_noidle(mhcd->dev);
		else
			atomic_set(&mhcd->pm_usage_cnt, 1);
	}

	return IRQ_HANDLED;
}

static irqreturn_t msm_ehci_host_wakeup_irq(int irq, void *data)
{

	struct msm_hcd *mhcd = data;

	mhcd->pmic_gpio_int_cnt++;
	dev_dbg(mhcd->dev, "%s: hsusb host remote wakeup interrupt cnt: %u\n",
			__func__, mhcd->pmic_gpio_int_cnt);


	pm_stay_awake(mhcd->dev);

	if (mhcd->pmic_gpio_dp_irq_enabled) {
		mhcd->pmic_gpio_dp_irq_enabled = 0;
		disable_irq_wake(irq);
		disable_irq_nosync(irq);
	}

	if (!atomic_read(&mhcd->pm_usage_cnt)) {
		atomic_set(&mhcd->pm_usage_cnt, 1);
		pm_runtime_get(mhcd->dev);
	}

	return IRQ_HANDLED;
}

static int msm_ehci_reset(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	struct msm_hcd *mhcd = hcd_to_mhcd(hcd);
	struct msm_usb_host_platform_data *pdata;
	int retval;

	ehci->caps = USB_CAPLENGTH;
	hcd->has_tt = 1;
	ehci->no_testmode_suspend = true;

	retval = ehci_setup(hcd);
	if (retval)
		return retval;

	/* bursts of unspecified length. */
	writel_relaxed(0, USB_AHBBURST);
	/* Use the AHB transactor */
	writel_relaxed(0x08, USB_AHBMODE);
	/* Disable streaming mode and select host mode */
	writel_relaxed(0x13, USB_USBMODE);

	pdata = mhcd->dev->platform_data;
	if (pdata && pdata->use_sec_phy)
		writel_relaxed(readl_relaxed(USB_PHY_CTRL2) | (1<<16),
								USB_PHY_CTRL2);

	/* Disable ULPI_TX_PKT_EN_CLR_FIX which is valid only for HSIC */
	writel_relaxed(readl_relaxed(USB_GENCONFIG2) & ~(1<<19),
					USB_GENCONFIG2);

	return 0;
}

static int (*ehci_bus_resume_func)(struct usb_hcd *hcd);

static int msm_ehci_bus_resume_with_gpio(struct usb_hcd *hcd)
{
	struct msm_hcd *mhcd = hcd_to_mhcd(hcd);
	int ret;

	gpio_direction_output(mhcd->resume_gpio, 1);

	/* call ehci_bus_resume from ehci-hcd library */
	ret = ehci_bus_resume_func(hcd);

	gpio_direction_output(mhcd->resume_gpio, 0);

	return ret;
}

#if defined(CONFIG_DEBUG_FS)
static u32 addr;
#define BUF_SIZE	32
static ssize_t debug_read_phy_data(struct file *file, char __user *ubuf,
				 size_t count, loff_t *ppos)
{
	struct msm_hcd *mhcd = file->private_data;
	char *kbuf;
	size_t c = 0;
	u32 data = 0;
	int ret = 0;

	kbuf = kzalloc(sizeof(char) * BUF_SIZE, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;
	pm_runtime_get(mhcd->dev);
	data = msm_ulpi_read(mhcd, addr);
	pm_runtime_put(mhcd->dev);
	if (data < 0) {
		dev_err(mhcd->dev,
				"%s(): ulpi read timeout\n", __func__);
		return -ETIMEDOUT;
	}

	c = scnprintf(kbuf, BUF_SIZE, "addr: 0x%x: data: 0x%x\n", addr, data);

	ret = simple_read_from_buffer(ubuf, count, ppos, kbuf, c);

	kfree(kbuf);

	return ret;
}

static ssize_t debug_write_phy_data(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	struct msm_hcd *mhcd = file->private_data;
	char kbuf[10];
	u32 data = 0;

	memset(kbuf, 0, 10);

	if (copy_from_user(kbuf, buf, min_t(size_t, sizeof(kbuf) - 1, count)))
		return -EFAULT;

	if (sscanf(kbuf, "%x", &data) != 1)
		return -EINVAL;

	pm_runtime_get(mhcd->dev);
	if (msm_ulpi_write(mhcd, data, addr) < 0) {
		dev_err(mhcd->dev,
				"%s(): ulpi write timeout\n", __func__);
		return -ETIMEDOUT;
	}
	pm_runtime_put(mhcd->dev);

	return count;
}

static ssize_t debug_phy_write_addr(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	char kbuf[10];
	u32 temp;

	memset(kbuf, 0, 10);

	if (copy_from_user(kbuf, buf, min_t(size_t, sizeof(kbuf) - 1, count)))
		return -EFAULT;

	if (sscanf(kbuf, "%x", &temp) != 1)
		return -EINVAL;

	if (temp > 0x3F)
		return -EINVAL;

	addr = temp;

	return count;
}

static int debug_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

const struct file_operations debug_rw_phy_ops = {
	.open = debug_open,
	.read = debug_read_phy_data,
	.write = debug_write_phy_data,
};

const struct file_operations debug_write_phy_ops = {
	.open = debug_open,
	.write = debug_phy_write_addr,
};

static struct dentry *dent_ehci;

static int ehci_debugfs_init(struct msm_hcd *mhcd)
{
	struct dentry *debug_phy_data;
	struct dentry *debug_phy_addr;

	dent_ehci = debugfs_create_dir(dev_name(mhcd->dev), 0);
	if (IS_ERR(dent_ehci))
		return -ENOENT;

	debug_phy_data = debugfs_create_file("phy_reg_data", 0666,
					dent_ehci, mhcd, &debug_rw_phy_ops);
	if (!debug_phy_data) {
		debugfs_remove(dent_ehci);
		return -ENOENT;
	}

	debug_phy_addr = debugfs_create_file("phy_reg_addr", 0666,
					dent_ehci, mhcd, &debug_write_phy_ops);
	if (!debug_phy_addr) {
		debugfs_remove_recursive(dent_ehci);
		return -ENOENT;
	}
	return 0;
}
#else
static int ehci_debugfs_init(struct msm_hcd *mhcd)
{
	return 0;
}
#endif

static const struct ehci_driver_overrides ehci_msm2_overrides __initdata = {
	.reset			= msm_ehci_reset,
	.extra_priv_size	= sizeof(struct msm_hcd),
};

static struct hc_driver __read_mostly ehci_msm2_hc_driver;

static irqreturn_t msm_hsusb_wakeup_irq(int irq, void *data)
{
	struct msm_hcd *mhcd = data;
	int ret;

	mhcd->wakeup_int_cnt++;
	dev_dbg(mhcd->dev, "%s: hsic remote wakeup interrupt cnt: %u\n",
			__func__, mhcd->wakeup_int_cnt);

	pm_stay_awake(mhcd->dev);

	spin_lock(&mhcd->wakeup_lock);
	if (mhcd->wakeup_irq_enabled) {
		mhcd->wakeup_irq_enabled = 0;
		disable_irq_wake(irq);
		disable_irq_nosync(irq);
	}
	spin_unlock(&mhcd->wakeup_lock);

	if (!atomic_read(&mhcd->pm_usage_cnt)) {
		ret = pm_runtime_get(mhcd->dev);
		/*
		 * controller runtime resume can race with us.
		 * if we are active (ret == 1) or resuming
		 * (ret == -EINPROGRESS), decrement the
		 * PM usage counter before returning.
		 */
		if ((ret == 1) || (ret == -EINPROGRESS)) {
			pm_runtime_put_noidle(mhcd->dev);
		} else {
			/* Let khubd know of hub port status change */
			if (mhcd->ehci.no_selective_suspend)
				mhcd->ehci.suspended_ports = 1;
			atomic_set(&mhcd->pm_usage_cnt, 1);
		}
	}

	return IRQ_HANDLED;
}

static int msm_ehci_init_clocks(struct msm_hcd *mhcd, u32 init)
{
	int ret = 0;

	if (!init)
		goto put_clocks;

	/* iface_clk is required for data transfers */
	mhcd->iface_clk = devm_clk_get(mhcd->dev, "iface_clk");
	if (IS_ERR(mhcd->iface_clk)) {
		ret = PTR_ERR(mhcd->iface_clk);
		mhcd->iface_clk = NULL;
		if (ret != -EPROBE_DEFER)
			dev_err(mhcd->dev, "failed to get iface_clk\n");
		return ret;
	}

	/* Link's protocol engine is based on pclk which must
	 * be running >55Mhz and frequency should also not change.
	 * Hence, vote for maximum clk frequency on its source
	 */
	mhcd->core_clk = devm_clk_get(mhcd->dev, "core_clk");
	if (IS_ERR(mhcd->core_clk)) {
		ret = PTR_ERR(mhcd->core_clk);
		mhcd->core_clk = NULL;
		if (ret != -EPROBE_DEFER)
			dev_err(mhcd->dev, "failed to get core_clk\n");
		return ret;
	}

	/*
	 * Get Max supported clk frequency for USB Core CLK and request
	 * to set the same.
	 */
	mhcd->core_clk_rate = clk_round_rate(mhcd->core_clk, LONG_MAX);
	if (IS_ERR_VALUE(mhcd->core_clk_rate)) {
		ret = mhcd->core_clk_rate;
		dev_err(mhcd->dev, "fail to get core clk max freq\n");
		return ret;
	}

	ret = clk_set_rate(mhcd->core_clk, mhcd->core_clk_rate);
	if (ret) {
		dev_err(mhcd->dev, "fail to set core_clk: %d\n", ret);
		return ret;
	}

	clk_prepare_enable(mhcd->core_clk);
	clk_prepare_enable(mhcd->iface_clk);

	mhcd->phy_sleep_clk = devm_clk_get(mhcd->dev, "sleep_clk");
	if (IS_ERR(mhcd->phy_sleep_clk)) {
		mhcd->phy_sleep_clk = NULL;
		dev_dbg(mhcd->dev, "failed to get sleep_clk\n");
	} else {
		clk_prepare_enable(mhcd->phy_sleep_clk);
	}

	/* 60MHz alt_core_clk is for LINK to be used during PHY RESET  */
	mhcd->alt_core_clk = devm_clk_get(mhcd->dev, "alt_core_clk");
	if (IS_ERR(mhcd->alt_core_clk)) {
		mhcd->alt_core_clk = NULL;
		dev_dbg(mhcd->dev, "failed to get alt_core_clk\n");
	} else {
		clk_set_rate(mhcd->alt_core_clk, 60000000);
	}

	return 0;

put_clocks:
	if (!atomic_read(&mhcd->in_lpm)) {
		clk_disable_unprepare(mhcd->iface_clk);
		clk_disable_unprepare(mhcd->core_clk);
	}
	if (mhcd->phy_sleep_clk)
		clk_disable_unprepare(mhcd->phy_sleep_clk);

	return 0;
}

struct msm_usb_host_platform_data *ehci_msm2_dt_to_pdata(
				struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct msm_usb_host_platform_data *pdata;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		dev_err(&pdev->dev, "unable to allocate platform data\n");
		return NULL;
	}

	pdata->use_sec_phy = of_property_read_bool(node,
					"qcom,usb2-enable-hsphy2");
	of_property_read_u32(node, "qcom,usb2-power-budget",
					&pdata->power_budget);
	pdata->no_selective_suspend = of_property_read_bool(node,
					"qcom,no-selective-suspend");
	pdata->resume_gpio = of_get_named_gpio(node, "qcom,resume-gpio", 0);

	pdata->ext_hub_reset_gpio = of_get_named_gpio(node,
					"qcom,ext-hub-reset-gpio", 0);
	pdata->is_uicc = of_property_read_bool(node,
					"qcom,usb2-enable-uicc");
	of_property_read_u32(node, "qcom,pm-qos-latency",
				&pdata->pm_qos_latency);

	return pdata;
}

static u64 ehci_msm_dma_mask = DMA_BIT_MASK(32);
static int ehci_msm2_probe(struct platform_device *pdev)
{
	struct usb_hcd *hcd;
	struct resource *res;
	struct msm_hcd *mhcd;
	struct pinctrl_state *set_state;
	const struct msm_usb_host_platform_data *pdata;
	char pdev_name[PDEV_NAME_LEN];
	int ret;

	dev_dbg(&pdev->dev, "ehci_msm2 probe\n");

	/*
	 * Fail probe in case of uicc till userspace activates driver through
	 * sysfs entry.
	 */
	if (!uicc_card_present && pdev->dev.of_node && of_property_read_bool(
				pdev->dev.of_node, "qcom,usb2-enable-uicc"))
		return -ENODEV;

	hcd = usb_create_hcd(&ehci_msm2_hc_driver, &pdev->dev,
				dev_name(&pdev->dev));
	if (!hcd) {
		dev_err(&pdev->dev, "Unable to create HCD\n");
		return  -ENOMEM;
	}

	mhcd = hcd_to_mhcd(hcd);
	mhcd->dev = &pdev->dev;

	mhcd->xo_clk = clk_get(&pdev->dev, "xo");
	if (IS_ERR(mhcd->xo_clk)) {
		ret = PTR_ERR(mhcd->xo_clk);
		mhcd->xo_clk = NULL;
		if (ret == -EPROBE_DEFER)
			goto put_hcd;
	}

	ret = msm_ehci_init_clocks(mhcd, 1);
	if (ret)
		goto xo_put;

	if (pdev->dev.of_node) {
		dev_dbg(&pdev->dev, "device tree enabled\n");
		pdev->dev.platform_data = ehci_msm2_dt_to_pdata(pdev);
	}

	if (!pdev->dev.platform_data)
		dev_dbg(&pdev->dev, "No platform data given\n");

	pdata = pdev->dev.platform_data;

	if (!pdev->dev.dma_mask)
		pdev->dev.dma_mask = &ehci_msm_dma_mask;
	if (!pdev->dev.coherent_dma_mask)
		pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);

	hcd_to_bus(hcd)->skip_resume = true;

	hcd->irq = platform_get_irq(pdev, 0);
	if (hcd->irq < 0) {
		dev_err(&pdev->dev, "Unable to get IRQ resource\n");
		ret = hcd->irq;
		goto deinit_clocks;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Unable to get memory resource\n");
		ret = -ENODEV;
		goto deinit_clocks;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);
	hcd->regs = ioremap(hcd->rsrc_start, hcd->rsrc_len);
	if (!hcd->regs) {
		dev_err(&pdev->dev, "ioremap failed\n");
		ret = -ENOMEM;
		goto deinit_clocks;
	}


	spin_lock_init(&mhcd->wakeup_lock);

	mhcd->async_irq = platform_get_irq_byname(pdev, "async_irq");
	if (mhcd->async_irq < 0) {
		dev_dbg(&pdev->dev, "platform_get_irq for async_int failed\n");
		mhcd->async_irq = 0;
	} else {
		ret = request_irq(mhcd->async_irq, msm_async_irq,
				IRQF_TRIGGER_RISING, "msm_ehci_host", mhcd);
		if (ret) {
			dev_err(&pdev->dev, "request irq failed (ASYNC INT)\n");
			goto unmap;
		}
		disable_irq(mhcd->async_irq);
	}

	snprintf(pdev_name, PDEV_NAME_LEN, "%s.%d", pdev->name, pdev->id);
	if (mhcd->xo_clk)
		ret = clk_prepare_enable(mhcd->xo_clk);
	if (ret) {
		dev_err(&pdev->dev, "%s failed to vote for TCXO %d\n",
								__func__, ret);
		goto free_xo_handle;
	}

	/* Get pinctrl if target uses pinctrl */
	mhcd->hsusb_pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(mhcd->hsusb_pinctrl)) {
		if (of_property_read_bool(pdev->dev.of_node, "pinctrl-names")) {
			dev_err(&pdev->dev, "Error encountered while getting pinctrl");
			ret = PTR_ERR(mhcd->hsusb_pinctrl);
			goto devote_xo_handle;
		}
		pr_debug("Target does not use pinctrl\n");
		mhcd->hsusb_pinctrl = NULL;
	}

	if (mhcd->hsusb_pinctrl) {
		set_state = pinctrl_lookup_state(mhcd->hsusb_pinctrl,
				"ehci_active");
		if (IS_ERR(set_state)) {
			pr_err("cannot get hsusb pinctrl active state\n");
			ret = PTR_ERR(set_state);
			goto devote_xo_handle;
		}
		ret = pinctrl_select_state(mhcd->hsusb_pinctrl, set_state);
		if (ret) {
			pr_err("cannot set hsusb pinctrl active state\n");
			goto devote_xo_handle;
		}
	}

	if (pdata && gpio_is_valid(pdata->resume_gpio)) {
		mhcd->resume_gpio = pdata->resume_gpio;
		ret = devm_gpio_request(&pdev->dev, mhcd->resume_gpio,
							"hsusb_resume");
		if (ret) {
			dev_err(&pdev->dev,
				"resume gpio(%d) request failed:%d\n",
				mhcd->resume_gpio, ret);
			mhcd->resume_gpio = -EINVAL;
		} else {
			/* to override ehci_bus_resume from ehci-hcd library */
			ehci_bus_resume_func = ehci_msm2_hc_driver.bus_resume;
			ehci_msm2_hc_driver.bus_resume =
				msm_ehci_bus_resume_with_gpio;
		}
	}

	if (pdata && gpio_is_valid(pdata->ext_hub_reset_gpio)) {
		ret = devm_gpio_request(&pdev->dev, pdata->ext_hub_reset_gpio,
							"hsusb_reset");
		if (ret) {
			dev_err(&pdev->dev,
				"reset gpio(%d) request failed:%d\n",
				pdata->ext_hub_reset_gpio, ret);
			goto pinctrl_sleep;
		} else {
			/* reset external hub */
			gpio_direction_output(pdata->ext_hub_reset_gpio, 0);
			/*
			 * Hub reset should be asserted for minimum 5microsec
			 * before deasserting.
			 */
			usleep_range(5, 1000);
			gpio_direction_output(pdata->ext_hub_reset_gpio, 1);
		}
	}

	spin_lock_init(&mhcd->wakeup_lock);

	ret = msm_ehci_init_vddcx(mhcd, 1);
	if (ret) {
		dev_err(&pdev->dev, "unable to initialize VDDCX\n");
		ret = -ENODEV;
		goto pinctrl_sleep;
	}

	ret = msm_ehci_config_vddcx(mhcd, 1);
	if (ret) {
		dev_err(&pdev->dev, "hsusb vddcx configuration failed\n");
		goto deinit_vddcx;
	}

	ret = msm_ehci_ldo_init(mhcd, 1);
	if (ret) {
		dev_err(&pdev->dev, "hsusb vreg configuration failed\n");
		goto deinit_vddcx;
	}

	ret = msm_ehci_ldo_enable(mhcd, 1);
	if (ret) {
		dev_err(&pdev->dev, "hsusb vreg enable failed\n");
		goto deinit_ldo;
	}

	ret = msm_ehci_init_vbus(mhcd, 1);
	if (ret)
		goto disable_ldo;

	hcd->phy = devm_usb_get_phy_by_phandle(&pdev->dev, "usb-phy", 0);
	if (IS_ERR(hcd->phy)) {
		if (PTR_ERR(hcd->phy) == -EPROBE_DEFER) {
			dev_dbg(&pdev->dev, "usb-phy not probed yet\n");
			ret = -EPROBE_DEFER;
			goto vbus_deinit;
		}
		hcd->phy = NULL;
	}

	if (hcd->phy) {
		usb_phy_init(hcd->phy);
		/* Set Host mode flag */
		hcd->phy->flags |= PHY_HOST_MODE;
	} else if (pdata && pdata->use_sec_phy) {
		mhcd->usb_phy_ctrl_reg = USB_PHY_CTRL2;
	} else {
		mhcd->usb_phy_ctrl_reg = USB_PHY_CTRL;
	}

	ret = msm_hsusb_reset(mhcd);
	if (ret) {
		dev_err(&pdev->dev, "hsusb PHY initialization failed\n");
		goto vbus_deinit;
	}

	ret = usb_add_hcd(hcd, hcd->irq, IRQF_SHARED);
	if (ret) {
		dev_err(&pdev->dev, "unable to register HCD\n");
		goto vbus_deinit;
	}

	mhcd->bus_scale_table = msm_bus_cl_get_pdata(pdev);
	if (!mhcd->bus_scale_table) {
		dev_dbg(&pdev->dev, "bus scaling is disabled\n");
	} else {
		mhcd->bus_perf_client =
			msm_bus_scale_register_client(mhcd->bus_scale_table);
		ret = msm_bus_scale_client_update_request(
						mhcd->bus_perf_client, 1);
		if (ret)
			dev_err(&pdev->dev, "Failed to vote for bus scaling\n");
	}

	pdata = mhcd->dev->platform_data;
	if (pdata && (!pdata->dock_connect_irq ||
				!irq_read_line(pdata->dock_connect_irq)))
		msm_ehci_vbus_power(mhcd, 1);

	/* For peripherals directly conneted to downstream port of root hub
	 * and require to drive suspend and resume by controller driver instead
	 * of root hub.
	 */
	if (pdata)
		mhcd->ehci.no_selective_suspend = pdata->no_selective_suspend;

	mhcd->wakeup_irq = platform_get_irq_byname(pdev, "wakeup_irq");
	if (mhcd->wakeup_irq > 0) {
		dev_dbg(&pdev->dev, "wakeup irq:%d\n", mhcd->wakeup_irq);

		irq_set_status_flags(mhcd->wakeup_irq, IRQ_NOAUTOEN);
		ret = request_irq(mhcd->wakeup_irq, msm_hsusb_wakeup_irq,
				IRQF_TRIGGER_HIGH,
				"msm_hsusb_wakeup", mhcd);
		if (ret) {
			dev_err(&pdev->dev, "request_irq(%d) failed:%d\n",
					mhcd->wakeup_irq, ret);
			mhcd->wakeup_irq = 0;
		}
	} else {
		mhcd->wakeup_irq = 0;
	}

	device_init_wakeup(&pdev->dev, 1);
	wakeup_source_init(&mhcd->ws, dev_name(&pdev->dev));
	pm_stay_awake(mhcd->dev);
	INIT_WORK(&mhcd->phy_susp_fail_work, msm_ehci_phy_susp_fail_work);
	/*
	 * This pdev->dev is assigned parent of root-hub by USB core,
	 * hence, runtime framework automatically calls this driver's
	 * runtime APIs based on root-hub's state.
	 */
	/* configure pmic_gpio_irq for D+ change */
	if (pdata && pdata->pmic_gpio_dp_irq)
		mhcd->pmic_gpio_dp_irq = pdata->pmic_gpio_dp_irq;
	if (mhcd->pmic_gpio_dp_irq) {
		ret = request_threaded_irq(mhcd->pmic_gpio_dp_irq, NULL,
				msm_ehci_host_wakeup_irq,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"msm_ehci_host_wakeup", mhcd);
		if (!ret) {
			disable_irq_nosync(mhcd->pmic_gpio_dp_irq);
		} else {
			dev_err(&pdev->dev, "request_irq(%d) failed: %d\n",
					mhcd->pmic_gpio_dp_irq, ret);
			mhcd->pmic_gpio_dp_irq = 0;
		}
	}

	if (pdata && pdata->pm_qos_latency)
		pm_qos_add_request(&mhcd->pm_qos_req_dma,
			PM_QOS_CPU_DMA_LATENCY, pdata->pm_qos_latency + 1);

	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	if (ehci_debugfs_init(mhcd) < 0)
		dev_err(mhcd->dev, "%s: debugfs init failed\n", __func__);

	return 0;

vbus_deinit:
	msm_ehci_init_vbus(mhcd, 0);
disable_ldo:
	msm_ehci_ldo_enable(mhcd, 0);
deinit_ldo:
	msm_ehci_ldo_init(mhcd, 0);
deinit_vddcx:
	msm_ehci_init_vddcx(mhcd, 0);
pinctrl_sleep:
	if (mhcd->hsusb_pinctrl) {
		set_state = pinctrl_lookup_state(mhcd->hsusb_pinctrl,
				"ehci_sleep");
		if (IS_ERR(set_state))
			pr_err("cannot get hsusb pinctrl sleep state\n");
		else
			pinctrl_select_state(mhcd->hsusb_pinctrl, set_state);
	}
devote_xo_handle:
	if (mhcd->xo_clk)
		clk_disable_unprepare(mhcd->xo_clk);
free_xo_handle:
	if (mhcd->xo_clk) {
		clk_put(mhcd->xo_clk);
		mhcd->xo_clk = NULL;
	}
	if (mhcd->async_irq)
		free_irq(mhcd->async_irq, mhcd);
unmap:
	iounmap(hcd->regs);
deinit_clocks:
	msm_ehci_init_clocks(mhcd, 0);
xo_put:
	if (mhcd->xo_clk)
		clk_put(mhcd->xo_clk);
put_hcd:
	usb_put_hcd(hcd);

	return ret;
}

static int ehci_msm2_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct msm_hcd *mhcd = hcd_to_mhcd(hcd);
	struct pinctrl_state *set_state;
	struct msm_usb_host_platform_data *pdata;

	if (mhcd->pmic_gpio_dp_irq) {
		if (mhcd->pmic_gpio_dp_irq_enabled)
			disable_irq_wake(mhcd->pmic_gpio_dp_irq);
		free_irq(mhcd->pmic_gpio_dp_irq, mhcd);
	}
	if (mhcd->async_irq) {
		if (mhcd->async_irq_enabled)
			disable_irq_wake(mhcd->async_irq);
		free_irq(mhcd->async_irq, mhcd);
	}

	if (mhcd->wakeup_irq) {
		if (mhcd->wakeup_irq_enabled)
			disable_irq_wake(mhcd->wakeup_irq);
		free_irq(mhcd->wakeup_irq, mhcd);
	}

	/* If the device was removed no need to call pm_runtime_disable */
	if (pdev->dev.power.power_state.event != PM_EVENT_INVALID)
		pm_runtime_disable(&pdev->dev);

	device_init_wakeup(&pdev->dev, 0);
	pm_runtime_set_suspended(&pdev->dev);

	usb_remove_hcd(hcd);

	if (mhcd->bus_perf_client)
		msm_bus_scale_unregister_client(mhcd->bus_perf_client);

	pdata = pdev->dev.platform_data;
	if (pdata && pdata->pm_qos_latency)
		pm_qos_remove_request(&mhcd->pm_qos_req_dma);

	if (hcd->phy) {
		/* Clear host mode flag */
		hcd->phy->flags &= ~PHY_HOST_MODE;
		usb_phy_shutdown(hcd->phy);
	}

	if (mhcd->xo_clk) {
		clk_disable_unprepare(mhcd->xo_clk);
		clk_put(mhcd->xo_clk);
	}
	msm_ehci_vbus_power(mhcd, 0);
	msm_ehci_init_vbus(mhcd, 0);
	msm_ehci_ldo_enable(mhcd, 0);
	msm_ehci_ldo_init(mhcd, 0);
	msm_ehci_init_vddcx(mhcd, 0);

	if (mhcd->hsusb_pinctrl) {
		set_state = pinctrl_lookup_state(mhcd->hsusb_pinctrl,
				"ehci_sleep");
		if (IS_ERR(set_state))
			pr_err("cannot get hsusb pinctrl sleep state\n");
		else
			pinctrl_select_state(mhcd->hsusb_pinctrl, set_state);
	}

	msm_ehci_init_clocks(mhcd, 0);
	wakeup_source_trash(&mhcd->ws);
	iounmap(hcd->regs);
	usb_put_hcd(hcd);

#if defined(CONFIG_DEBUG_FS)
	debugfs_remove_recursive(dent_ehci);
#endif
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int ehci_msm2_pm_suspend(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct msm_hcd *mhcd = hcd_to_mhcd(hcd);

	dev_dbg(dev, "ehci-msm2 PM suspend\n");

	if (device_may_wakeup(dev) && !mhcd->async_irq_enabled &&
		!mhcd->wakeup_irq_enabled && !mhcd->pmic_gpio_dp_irq_enabled)
		enable_irq_wake(hcd->irq);

	return msm_ehci_suspend(mhcd);

}

static int ehci_msm2_pm_resume(struct device *dev)
{
	int ret;
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct msm_hcd *mhcd = hcd_to_mhcd(hcd);

	dev_dbg(dev, "ehci-msm2 PM resume\n");

	if (device_may_wakeup(dev) && !mhcd->async_irq_enabled &&
		!mhcd->wakeup_irq_enabled && !mhcd->pmic_gpio_dp_irq_enabled)
		disable_irq_wake(hcd->irq);

	ret = msm_ehci_resume(mhcd);
	if (ret)
		return ret;

	/* Bring the device to full powered state upon system resume */
	pm_runtime_disable(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	return 0;
}
#endif

#ifdef CONFIG_PM_RUNTIME
static int ehci_msm2_runtime_idle(struct device *dev)
{
	dev_dbg(dev, "EHCI runtime idle\n");

	return 0;
}

static int ehci_msm2_runtime_suspend(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct msm_hcd *mhcd = hcd_to_mhcd(hcd);

	dev_dbg(dev, "EHCI runtime suspend\n");
	return msm_ehci_suspend(mhcd);
}

static int ehci_msm2_runtime_resume(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct msm_hcd *mhcd = hcd_to_mhcd(hcd);

	dev_dbg(dev, "EHCI runtime resume\n");
	return msm_ehci_resume(mhcd);
}
#endif

#ifdef CONFIG_PM
static const struct dev_pm_ops ehci_msm2_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ehci_msm2_pm_suspend, ehci_msm2_pm_resume)
	SET_RUNTIME_PM_OPS(ehci_msm2_runtime_suspend, ehci_msm2_runtime_resume,
				ehci_msm2_runtime_idle)
};
#endif

static const struct of_device_id ehci_msm2_dt_match[] = {
	{ .compatible = "qcom,ehci-host",
	},
	{}
};

static struct platform_driver ehci_msm2_driver = {
	.probe	= ehci_msm2_probe,
	.remove	= ehci_msm2_remove,
	.driver = {
		.name = "msm_ehci_host",
#ifdef CONFIG_PM
		.pm = &ehci_msm2_dev_pm_ops,
#endif
		.of_match_table = ehci_msm2_dt_match,
	},
};

static int __init ehci_msm2_init(void)
{
	if (usb_disabled())
		return -ENODEV;

	pr_info("%s: " DRIVER_DESC "\n", hcd_name);

	ehci_init_driver(&ehci_msm2_hc_driver, &ehci_msm2_overrides);
	return platform_driver_register(&ehci_msm2_driver);
}
module_init(ehci_msm2_init);

static void __exit ehci_msm2_cleanup(void)
{
	platform_driver_unregister(&ehci_msm2_driver);
}
module_exit(ehci_msm2_cleanup);
