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

#include <linux/irqchip/qpnp-int.h>
#include <linux/irqchip/msm-gpio-irq.h>
#include <linux/irqchip/msm-mpm-irq.h>
#include <linux/mfd/wcd9xxx/core.h>
#include "irqchip.h"

static int __init irq_msm_gpio_init(struct device_node *node,
			struct device_node *parent)
{
	int rc;

#ifdef CONFIG_USE_PINCTRL_IRQ
	rc = msm_tlmm_of_irq_init(node, parent);
#else
	rc = msm_gpio_of_init(node, parent);
#endif
	if (rc) {
		pr_err("Couldn't initlialize gpio irq rc = %d\n", rc);
		return rc;
	}

	/*
	 * Initialize the mpm after gpio (and gic) are initialized. Note that
	 * gpio irq controller is the child of gic irq controller, hence gic's
	 * init function will be called prior to gpio.
	 */
	of_mpm_init();

	return 0;
}

#ifdef CONFIG_USE_PINCTRL_IRQ
IRQCHIP_DECLARE(tlmmv3_irq, "qcom,msm-tlmm-gp", irq_msm_gpio_init);
#else
IRQCHIP_DECLARE(tlmm_irq, "qcom,msm-gpio", irq_msm_gpio_init);
#endif
IRQCHIP_DECLARE(qpnp_irq, "qcom,spmi-pmic-arb", qpnpint_of_init);
IRQCHIP_DECLARE(wcd9xxx_irq, "qcom,wcd9xxx-irq", wcd9xxx_irq_of_init);
