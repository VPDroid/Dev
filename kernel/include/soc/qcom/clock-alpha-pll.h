/*
 * Copyright (c) 2012-2014, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __ARCH_ARM_MACH_MSM_CLOCK_ALPHA_PLL_H
#define __ARCH_ARM_MACH_MSM_CLOCK_ALPHA_PLL_H

#include <linux/spinlock.h>
#include <linux/clk/msm-clk-provider.h>

struct alpha_pll_masks {
	u32 lock_mask;		/* lock_det bit */
	u32 active_mask;	/* active_flag in FSM mode */
	u32 update_mask;	/* update bit for dynamic update */
	u32 vco_mask;		/* vco_sel bits */
	u32 vco_shift;
	u32 alpha_en_mask;	/* alpha_en bit */
	u32 output_mask;	/* pllout_* bits */
	u32 post_div_mask;
};

struct alpha_pll_vco_tbl {
	u32 vco_val;
	unsigned long min_freq;
	unsigned long max_freq;
};

#define VCO(a, b, c) { \
	.vco_val = a,\
	.min_freq = b,\
	.max_freq = c,\
}

struct alpha_pll_clk {
	struct alpha_pll_masks *masks;
	void *const __iomem *base;
	u32 offset;

	/* if fsm_en_mask is set, config PLL to FSM mode */
	u32 fsm_reg_offset;
	u32 fsm_en_mask;

	u32 enable_config;	/* bitmask of outputs to be enabled */
	u32 post_div_config;	/* masked post divider setting */

	struct alpha_pll_vco_tbl *vco_tbl;
	u32 num_vco;

	bool inited;

	struct clk c;
};

static inline struct alpha_pll_clk *to_alpha_pll_clk(struct clk *c)
{
	return container_of(c, struct alpha_pll_clk, c);
}
#endif

extern struct clk_ops clk_ops_alpha_pll;
extern struct clk_ops clk_ops_fixed_alpha_pll;
