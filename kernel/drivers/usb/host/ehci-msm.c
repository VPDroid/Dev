/* ehci-msm.c - HSUSB Host Controller Driver Implementation
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

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/dma-mapping.h>

#include <linux/usb/otg.h>
#include <linux/usb/msm_hsusb.h>
#include <linux/usb/msm_hsusb_hw.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>

#include "ehci.h"

#define MSM_USB_BASE (hcd->regs)

#define DRIVER_DESC "Qualcomm On-Chip EHCI Host Controller"

static const char hcd_name[] = "ehci-msm";
static struct hc_driver __read_mostly ehci_msm_hc_driver;
static struct usb_phy *phy;

static int ehci_msm_reset(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
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

	if (hcd->phy->flags & ENABLE_SECONDARY_PHY) {
		ehci_dbg(ehci, "using secondary hsphy\n");
		writel_relaxed(readl_relaxed(USB_PHY_CTRL2) | (1<<16),
							USB_PHY_CTRL2);
	}

	/* Disable ULPI_TX_PKT_EN_CLR_FIX which is valid only for HSIC */
	writel_relaxed(readl_relaxed(USB_GENCONFIG2) & ~(1<<19),
					USB_GENCONFIG2);
	return 0;
}

static const struct ehci_driver_overrides ehci_msm_overrides __initdata = {
	.reset = ehci_msm_reset,
};

static u64 msm_ehci_dma_mask = DMA_BIT_MASK(32);
static int ehci_msm_probe(struct platform_device *pdev)
{
	struct usb_hcd *hcd;
	struct resource *res;
	int ret;

	dev_dbg(&pdev->dev, "ehci_msm proble\n");

	if (!pdev->dev.dma_mask)
		pdev->dev.dma_mask = &msm_ehci_dma_mask;
	if (!pdev->dev.coherent_dma_mask)
		pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);

	hcd = usb_create_hcd(&ehci_msm_hc_driver, &pdev->dev,
			     dev_name(&pdev->dev));
	if (!hcd) {
		dev_err(&pdev->dev, "Unable to create HCD\n");
		return  -ENOMEM;
	}

	hcd_to_bus(hcd)->skip_resume = true;

	hcd->irq = platform_get_irq(pdev, 0);
	if (hcd->irq < 0) {
		dev_err(&pdev->dev, "Unable to get IRQ resource\n");
		ret = hcd->irq;
		goto put_hcd;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Unable to get memory resource\n");
		ret = -ENODEV;
		goto put_hcd;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);
	hcd->regs = devm_ioremap(&pdev->dev, hcd->rsrc_start, hcd->rsrc_len);
	if (!hcd->regs) {
		dev_err(&pdev->dev, "ioremap failed\n");
		ret = -ENOMEM;
		goto put_hcd;
	}

	/*
	 * OTG driver takes care of PHY initialization, clock management,
	 * powering up VBUS, mapping of registers address space and power
	 * management.
	 */
	phy = devm_usb_get_phy(&pdev->dev, USB_PHY_TYPE_USB2);
	if (IS_ERR_OR_NULL(phy)) {
		dev_err(&pdev->dev, "unable to find transceiver\n");
		ret = -ENODEV;
		goto put_hcd;
	}

	ret = otg_set_host(phy->otg, &hcd->self);
	if (ret < 0) {
		dev_err(&pdev->dev, "unable to register with transceiver\n");
		goto put_hcd;
	}

	hcd->phy = phy;
	device_init_wakeup(&pdev->dev, 1);
	pm_runtime_enable(&pdev->dev);

	msm_bam_set_usb_host_dev(&pdev->dev);

	return 0;

put_hcd:
	usb_put_hcd(hcd);

	return ret;
}

static int ehci_msm_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	device_init_wakeup(&pdev->dev, 0);
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);

	hcd->phy = NULL;
	otg_set_host(phy->otg, NULL);

	/* FIXME: need to call usb_remove_hcd() here? */

	usb_put_hcd(hcd);

	return 0;
}

#ifdef CONFIG_PM_RUNTIME
static int ehci_msm_runtime_idle(struct device *dev)
{
	dev_dbg(dev, "ehci runtime idle\n");
	return 0;
}

static int ehci_msm_runtime_suspend(struct device *dev)
{
	dev_dbg(dev, "ehci runtime suspend\n");
	/*
	 * Notify OTG about suspend.  It takes care of
	 * putting the hardware in LPM.
	 */
	return usb_phy_set_suspend(phy, 1);
}

static int ehci_msm_runtime_resume(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	int ret;
	u32 portsc;

	dev_dbg(dev, "ehci runtime resume\n");
	ret = usb_phy_set_suspend(phy, 0);
	if (ret)
		return ret;

	portsc = readl_relaxed(USB_PORTSC);
	portsc &= ~PORT_RWC_BITS;
	portsc |= PORT_RESUME;
	writel_relaxed(portsc, USB_PORTSC);

	return ret;
}
#endif

#ifdef CONFIG_PM_SLEEP
static int ehci_msm_pm_suspend(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);

	dev_dbg(dev, "ehci-msm PM suspend\n");

	if (!hcd->rh_registered)
		return 0;

	return usb_phy_set_suspend(phy, 1);
}

static int ehci_msm_pm_resume(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	int ret;
	u32 portsc;

	dev_dbg(dev, "ehci-msm PM resume\n");
	if (!hcd->rh_registered)
		return 0;

	/* Notify OTG to bring hw out of LPM before restoring wakeup flags */
	ret = usb_phy_set_suspend(phy, 0);
	if (ret)
		return ret;

	ehci_resume(hcd, false);
	portsc = readl_relaxed(USB_PORTSC);
	portsc &= ~PORT_RWC_BITS;
	portsc |= PORT_RESUME;
	writel_relaxed(portsc, USB_PORTSC);
	/* Resume root-hub to handle USB event if any else initiate LPM again */
	usb_hcd_resume_root_hub(hcd);

	return ret;
}
#endif

static const struct dev_pm_ops ehci_msm_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ehci_msm_pm_suspend, ehci_msm_pm_resume)
	SET_RUNTIME_PM_OPS(ehci_msm_runtime_suspend, ehci_msm_runtime_resume,
				ehci_msm_runtime_idle)
};

static struct platform_driver ehci_msm_driver = {
	.probe	= ehci_msm_probe,
	.remove	= ehci_msm_remove,
	.driver = {
		   .name = "msm_hsusb_host",
		   .pm = &ehci_msm_dev_pm_ops,
	},
};

static int __init ehci_msm_init(void)
{
	if (usb_disabled())
		return -ENODEV;

	pr_info("%s: " DRIVER_DESC "\n", hcd_name);
	ehci_init_driver(&ehci_msm_hc_driver, &ehci_msm_overrides);
	return platform_driver_register(&ehci_msm_driver);
}
module_init(ehci_msm_init);

static void __exit ehci_msm_cleanup(void)
{
	platform_driver_unregister(&ehci_msm_driver);
}
module_exit(ehci_msm_cleanup);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_ALIAS("platform:msm-ehci");
MODULE_LICENSE("GPL");
