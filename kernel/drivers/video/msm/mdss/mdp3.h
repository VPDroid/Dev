/* Copyright (c) 2013-2015, The Linux Foundation. All rights reserved.
 * Copyright (C) 2007 Google Incorporated
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
#ifndef MDP3_H
#define MDP3_H

#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include <linux/msm_iommu_domains.h>

#include "mdp3_dma.h"
#include "mdss_fb.h"
#include "mdss.h"

#define MDP_VSYNC_CLK_RATE	19200000
#define MDP_CORE_CLK_RATE_SVS	150000000
#define MDP_CORE_CLK_RATE_MAX	307200000

/* PPP cant work at SVS for panel res above qHD */
#define SVS_MAX_PIXEL		(540 * 960)

#define KOFF_TIMEOUT msecs_to_jiffies(84)

enum  {
	MDP3_CLK_AHB,
	MDP3_CLK_AXI,
	MDP3_CLK_MDP_SRC,
	MDP3_CLK_MDP_CORE,
	MDP3_CLK_VSYNC,
	MDP3_CLK_DSI,
	MDP3_MAX_CLK
};

enum {
	MDP3_BUS_HANDLE,
	MDP3_BUS_HANDLE_MAX,
};

enum {
	MDP3_IOMMU_DOMAIN_SECURE,
	MDP3_IOMMU_DOMAIN_UNSECURE,
	MDP3_IOMMU_DOMAIN_MAX,
};

enum {
	MDP3_IOMMU_CTX_MDP_0,
	MDP3_IOMMU_CTX_MDP_1,
	MDP3_IOMMU_CTX_MAX
};

/* Keep DSI entry in sync with mdss
 which is being used by DSI 6G */
enum {
	MDP3_CLIENT_DMA_P,
	MDP3_CLIENT_DSI = 1,
	MDP3_CLIENT_PPP,
	MDP3_CLIENT_MAX,
};

enum {
	DI_PARTITION_NUM = 0,
	DI_DOMAIN_NUM = 1,
	DI_MAX,
};

struct mdp3_bus_handle_map {
	struct msm_bus_vectors *bus_vector;
	struct msm_bus_paths *usecases;
	struct msm_bus_scale_pdata *scale_pdata;
	int current_bus_idx;
	int ref_cnt;
	u64 restore_ab[MDP3_CLIENT_MAX];
	u64 restore_ib[MDP3_CLIENT_MAX];
	u64 ab[MDP3_CLIENT_MAX];
	u64 ib[MDP3_CLIENT_MAX];
	u32 handle;
};

struct mdp3_iommu_domain_map {
	u32 domain_type;
	char *client_name;
	struct msm_iova_partition partitions[1];
	int npartitions;
	int domain_idx;
	struct iommu_domain *domain;
};

struct mdp3_iommu_ctx_map {
	u32 ctx_type;
	struct mdp3_iommu_domain_map *domain;
	char *ctx_name;
	struct device *ctx;
	int attached;
};

struct mdp3_iommu_meta {
	struct rb_node node;
	struct ion_handle *handle;
	struct rb_root iommu_maps;
	struct kref ref;
	struct sg_table *table;
	struct dma_buf *dbuf;
	int mapped_size;
	unsigned long size;
	dma_addr_t iova_addr;
	unsigned long flags;
};

#define MDP3_MAX_INTR 28

struct mdp3_intr_cb {
	void (*cb)(int type, void *);
	void *data;
};

struct mdp3_hw_resource {
	struct platform_device *pdev;
	u32 mdp_rev;

	struct mutex res_mutex;

	struct clk *clocks[MDP3_MAX_CLK];
	int clock_ref_count[MDP3_MAX_CLK];
	unsigned long dma_core_clk_request;
	unsigned long ppp_core_clk_request;
	struct mdss_hw mdp3_hw;
	struct mdss_util_intf *mdss_util;

	char __iomem *mdp_base;
	size_t mdp_reg_size;

	char __iomem *vbif_base;
	size_t vbif_reg_size;

	struct mdp3_bus_handle_map *bus_handle;

	struct ion_client *ion_client;
	struct mdp3_iommu_domain_map *domains;
	struct mdp3_iommu_ctx_map *iommu_contexts;
	unsigned int iommu_ref_cnt;
	bool allow_iommu_update;
	struct ion_handle *ion_handle;
	struct mutex iommu_lock;

	struct mdp3_dma dma[MDP3_DMA_MAX];
	struct mdp3_intf intf[MDP3_DMA_OUTPUT_SEL_MAX];

	struct rb_root iommu_root;
	spinlock_t irq_lock;
	u32 irq_ref_count[MDP3_MAX_INTR];
	u32 irq_mask;
	int irq_ref_cnt;
	struct mdp3_intr_cb callbacks[MDP3_MAX_INTR];
	u32 underrun_cnt;

	int irq_registered;

	unsigned long splash_mem_addr;
	u32 splash_mem_size;
	struct mdss_panel_cfg pan_cfg;

	int clk_prepare_count;
	int cont_splash_en;

	bool batfet_required;
	struct regulator *batfet;
	struct regulator *vdd_cx;
	struct regulator *fs;
	bool fs_ena;
	bool smart_blit_en;
};

struct mdp3_img_data {
	dma_addr_t addr;
	u32 len;
	u32 flags;
	u32 padding;
	int p_need;
	struct file *srcp_file;
	struct ion_handle *srcp_ihdl;
};

extern struct mdp3_hw_resource *mdp3_res;

struct mdp3_dma *mdp3_get_dma_pipe(int capability);
struct mdp3_intf *mdp3_get_display_intf(int type);
void mdp3_irq_enable(int type);
void mdp3_irq_disable(int type);
void mdp3_irq_disable_nosync(int type);
int mdp3_set_intr_callback(u32 type, struct mdp3_intr_cb *cb);
void mdp3_irq_register(void);
void mdp3_irq_deregister(void);
int mdp3_clk_set_rate(int clk_type, unsigned long clk_rate, int client);
int mdp3_clk_enable(int enable, int dsi_clk);
int mdp3_res_update(int enable, int dsi_clk, int client);
int mdp3_bus_scale_set_quota(int client, u64 ab_quota, u64 ib_quota);
int mdp3_put_img(struct mdp3_img_data *data, int client);
int mdp3_get_img(struct msmfb_data *img, struct mdp3_img_data *data, int client);
int mdp3_iommu_enable(void);
int mdp3_iommu_disable(void);
int mdp3_iommu_is_attached(void);
void mdp3_free(struct msm_fb_data_type *mfd);
int mdp3_parse_dt_splash(struct msm_fb_data_type *mfd);
void mdp3_release_splash_memory(struct msm_fb_data_type *mfd);
int mdp3_create_sysfs_link(struct device *dev);
int mdp3_get_cont_spash_en(void);
int mdp3_get_mdp_dsi_clk(void);
int mdp3_put_mdp_dsi_clk(void);

int mdp3_misr_set(struct mdp_misr *misr_req);
int mdp3_misr_get(struct mdp_misr *misr_resp);
void mdp3_enable_regulator(int enable);
void mdp3_check_dsi_ctrl_status(struct work_struct *work,
				uint32_t interval);
int mdp3_dynamic_clock_gating_ctrl(int enable);
int mdp3_footswitch_ctrl(int enable);
int mdp3_qos_remapper_setup(struct mdss_panel_data *panel);

#define MDP3_REG_WRITE(addr, val) writel_relaxed(val, mdp3_res->mdp_base + addr)
#define MDP3_REG_READ(addr) readl_relaxed(mdp3_res->mdp_base + addr)
#define VBIF_REG_WRITE(off, val) writel_relaxed(val, mdp3_res->vbif_base + off)
#define VBIF_REG_READ(off) readl_relaxed(mdp3_res->vbif_base + off)

#endif /* MDP3_H */
