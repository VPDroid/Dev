/* Copyright (c) 2012-2015, The Linux Foundation. All rights reserved.
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
#include <asm/barrier.h>
#include <linux/delay.h>
#include <linux/device.h>
#include "ipa_i.h"

/*
 * These values were determined empirically and shows good E2E bi-
 * directional throughputs
 */
#define IPA_HOLB_TMR_EN 0x1
#define IPA_HOLB_TMR_DIS 0x0
#define IPA_HOLB_TMR_DEFAULT_VAL 0x1ff

#define IPA_PKT_FLUSH_TO_US 100

int ipa_enable_data_path(u32 clnt_hdl)
{
	struct ipa_ep_context *ep = &ipa_ctx->ep[clnt_hdl];
	struct ipa_ep_cfg_holb holb_cfg;
	struct ipa_ep_cfg_ctrl ep_cfg_ctrl;
	int res = 0;

	IPADBG("Enabling data path\n");
	/* From IPA 2.0, disable HOLB */
	if ((ipa_ctx->ipa_hw_type == IPA_HW_v2_0 ||
		ipa_ctx->ipa_hw_type == IPA_HW_v2_5) &&
		IPA_CLIENT_IS_CONS(ep->client)) {
		memset(&holb_cfg, 0 , sizeof(holb_cfg));
		holb_cfg.en = IPA_HOLB_TMR_DIS;
		holb_cfg.tmr_val = 0;
		res = ipa_cfg_ep_holb(clnt_hdl, &holb_cfg);
	}

	/* Enable the pipe */
	if (IPA_CLIENT_IS_CONS(ep->client) &&
	    (ep->keep_ipa_awake ||
	     ipa_ctx->resume_on_connect[ep->client] ||
	     !ipa_should_pipe_be_suspended(ep->client))) {
		memset(&ep_cfg_ctrl, 0 , sizeof(ep_cfg_ctrl));
		ep_cfg_ctrl.ipa_ep_suspend = false;
		ipa_cfg_ep_ctrl(clnt_hdl, &ep_cfg_ctrl);
	}

	return res;
}

int ipa_disable_data_path(u32 clnt_hdl)
{
	struct ipa_ep_context *ep = &ipa_ctx->ep[clnt_hdl];
	struct ipa_ep_cfg_holb holb_cfg;
	struct ipa_ep_cfg_ctrl ep_cfg_ctrl;
	u32 aggr_init;
	int res = 0;

	IPADBG("Disabling data path\n");
	/* On IPA 2.0, enable HOLB in order to prevent IPA from stalling */
	if ((ipa_ctx->ipa_hw_type == IPA_HW_v2_0 ||
		ipa_ctx->ipa_hw_type == IPA_HW_v2_5) &&
		IPA_CLIENT_IS_CONS(ep->client)) {
		memset(&holb_cfg, 0, sizeof(holb_cfg));
		holb_cfg.en = IPA_HOLB_TMR_EN;
		holb_cfg.tmr_val = 0;
		res = ipa_cfg_ep_holb(clnt_hdl, &holb_cfg);
	}

	/* Suspend the pipe */
	if (IPA_CLIENT_IS_CONS(ep->client)) {
		memset(&ep_cfg_ctrl, 0 , sizeof(struct ipa_ep_cfg_ctrl));
		ep_cfg_ctrl.ipa_ep_suspend = true;
		ipa_cfg_ep_ctrl(clnt_hdl, &ep_cfg_ctrl);
	}

	udelay(IPA_PKT_FLUSH_TO_US);
	aggr_init = ipa_read_reg(ipa_ctx->mmio,
			IPA_ENDP_INIT_AGGR_N_OFST_v2_0(clnt_hdl));
	if (((aggr_init & IPA_ENDP_INIT_AGGR_N_AGGR_EN_BMSK) >>
	    IPA_ENDP_INIT_AGGR_N_AGGR_EN_SHFT) == IPA_ENABLE_AGGR)
		ipa_tag_aggr_force_close(clnt_hdl);

	return res;
}

static int ipa_connect_configure_sps(const struct ipa_connect_params *in,
				     struct ipa_ep_context *ep, int ipa_ep_idx)
{
	int result = -EFAULT;

	/* Default Config */
	ep->ep_hdl = sps_alloc_endpoint();

	if (ep->ep_hdl == NULL) {
		IPAERR("SPS EP alloc failed EP.\n");
		return -EFAULT;
	}

	result = sps_get_config(ep->ep_hdl,
		&ep->connect);
	if (result) {
		IPAERR("fail to get config.\n");
		return -EFAULT;
	}

	/* Specific Config */
	if (IPA_CLIENT_IS_CONS(in->client)) {
		ep->connect.mode = SPS_MODE_SRC;
		ep->connect.destination =
			in->client_bam_hdl;
		ep->connect.source = ipa_ctx->bam_handle;
		ep->connect.dest_pipe_index =
			in->client_ep_idx;
		ep->connect.src_pipe_index = ipa_ep_idx;
	} else {
		ep->connect.mode = SPS_MODE_DEST;
		ep->connect.source = in->client_bam_hdl;
		ep->connect.destination = ipa_ctx->bam_handle;
		ep->connect.src_pipe_index = in->client_ep_idx;
		ep->connect.dest_pipe_index = ipa_ep_idx;
	}

	return 0;
}

static int ipa_connect_allocate_fifo(const struct ipa_connect_params *in,
				     struct sps_mem_buffer *mem_buff_ptr,
				     bool *fifo_in_pipe_mem_ptr,
				     u32 *fifo_pipe_mem_ofst_ptr,
				     u32 fifo_size, int ipa_ep_idx)
{
	dma_addr_t dma_addr;
	u32 ofst;
	int result = -EFAULT;

	mem_buff_ptr->size = fifo_size;
	if (in->pipe_mem_preferred) {
		if (ipa_pipe_mem_alloc(&ofst, fifo_size)) {
			IPAERR("FIFO pipe mem alloc fail ep %u\n",
				ipa_ep_idx);
			mem_buff_ptr->base =
				dma_alloc_coherent(ipa_ctx->pdev,
				mem_buff_ptr->size,
				&dma_addr, GFP_KERNEL);
		} else {
			memset(mem_buff_ptr, 0, sizeof(struct sps_mem_buffer));
			result = sps_setup_bam2bam_fifo(mem_buff_ptr, ofst,
				fifo_size, 1);
			WARN_ON(result);
			*fifo_in_pipe_mem_ptr = 1;
			dma_addr = mem_buff_ptr->phys_base;
			*fifo_pipe_mem_ofst_ptr = ofst;
		}
	} else {
		mem_buff_ptr->base =
			dma_alloc_coherent(ipa_ctx->pdev, mem_buff_ptr->size,
			&dma_addr, GFP_KERNEL);
	}
	mem_buff_ptr->phys_base = dma_addr;
	if (mem_buff_ptr->base == NULL) {
		IPAERR("fail to get DMA memory.\n");
		return -EFAULT;
	}

	return 0;
}

/**
 * ipa_connect() - low-level IPA client connect
 * @in:	[in] input parameters from client
 * @sps:	[out] sps output from IPA needed by client for sps_connect
 * @clnt_hdl:	[out] opaque client handle assigned by IPA to client
 *
 * Should be called by the driver of the peripheral that wants to connect to
 * IPA in BAM-BAM mode. these peripherals are USB and HSIC. this api
 * expects caller to take responsibility to add any needed headers, routing
 * and filtering tables and rules as needed.
 *
 * Returns:	0 on success, negative on failure
 *
 * Note:	Should not be called from atomic context
 */
int ipa_connect(const struct ipa_connect_params *in, struct ipa_sps_params *sps,
		u32 *clnt_hdl)
{
	int ipa_ep_idx;
	int result = -EFAULT;
	struct ipa_ep_context *ep;
	struct ipa_ep_cfg_status ep_status;

	IPADBG("connecting client\n");

	if (in == NULL || sps == NULL || clnt_hdl == NULL ||
	    in->client >= IPA_CLIENT_MAX ||
	    in->desc_fifo_sz == 0 || in->data_fifo_sz == 0) {
		IPAERR("bad parm.\n");
		return -EINVAL;
	}

	ipa_ep_idx = ipa_get_ep_mapping(in->client);
	if (ipa_ep_idx == -1) {
		IPAERR("fail to alloc EP.\n");
		goto fail;
	}

	ep = &ipa_ctx->ep[ipa_ep_idx];

	if (ep->valid) {
		IPAERR("EP already allocated.\n");
		goto fail;
	}

	memset(&ipa_ctx->ep[ipa_ep_idx], 0, sizeof(struct ipa_ep_context));
	ipa_inc_client_enable_clks();

	ep->skip_ep_cfg = in->skip_ep_cfg;
	ep->valid = 1;
	ep->client = in->client;
	ep->client_notify = in->notify;
	ep->priv = in->priv;
	ep->keep_ipa_awake = in->keep_ipa_awake;

	result = ipa_enable_data_path(ipa_ep_idx);
	if (result) {
		IPAERR("enable data path failed res=%d clnt=%d.\n", result,
				ipa_ep_idx);
		goto ipa_cfg_ep_fail;
	}

	if (!ep->skip_ep_cfg) {
		if (ipa_cfg_ep(ipa_ep_idx, &in->ipa_ep_cfg)) {
			IPAERR("fail to configure EP.\n");
			goto ipa_cfg_ep_fail;
		}
		/* Setting EP status 0 */
		memset(&ep_status, 0, sizeof(ep_status));
		if (ipa_cfg_ep_status(ipa_ep_idx, &ep_status)) {
			IPAERR("fail to configure status of EP.\n");
			goto ipa_cfg_ep_fail;
		}
		IPADBG("ep configuration successful\n");
	} else {
		IPADBG("Skipping endpoint configuration.\n");
	}

	result = ipa_connect_configure_sps(in, ep, ipa_ep_idx);
	if (result) {
		IPAERR("fail to configure SPS.\n");
		goto ipa_cfg_ep_fail;
	}

	if (in->desc.base == NULL) {
		result = ipa_connect_allocate_fifo(in, &ep->connect.desc,
						  &ep->desc_fifo_in_pipe_mem,
						  &ep->desc_fifo_pipe_mem_ofst,
						  in->desc_fifo_sz, ipa_ep_idx);
		if (result) {
			IPAERR("fail to allocate DESC FIFO.\n");
			goto desc_mem_alloc_fail;
		}
	} else {
		IPADBG("client allocated DESC FIFO\n");
		ep->connect.desc = in->desc;
		ep->desc_fifo_client_allocated = 1;
	}
	IPADBG("Descriptor FIFO pa=%pa, size=%d\n", &ep->connect.desc.phys_base,
	       ep->connect.desc.size);

	if (in->data.base == NULL) {
		result = ipa_connect_allocate_fifo(in, &ep->connect.data,
						&ep->data_fifo_in_pipe_mem,
						&ep->data_fifo_pipe_mem_ofst,
						in->data_fifo_sz, ipa_ep_idx);
		if (result) {
			IPAERR("fail to allocate DATA FIFO.\n");
			goto data_mem_alloc_fail;
		}
	} else {
		IPADBG("client allocated DATA FIFO\n");
		ep->connect.data = in->data;
		ep->data_fifo_client_allocated = 1;
	}
	IPADBG("Data FIFO pa=%pa, size=%d\n", &ep->connect.data.phys_base,
	       ep->connect.data.size);

	if ((ipa_ctx->ipa_hw_type == IPA_HW_v2_0 ||
		ipa_ctx->ipa_hw_type == IPA_HW_v2_5) &&
		IPA_CLIENT_IS_USB_CONS(in->client))
		ep->connect.event_thresh = IPA_USB_EVENT_THRESHOLD;
	else
		ep->connect.event_thresh = IPA_EVENT_THRESHOLD;
	ep->connect.options = SPS_O_AUTO_ENABLE;    /* BAM-to-BAM */

	result = ipa_sps_connect_safe(ep->ep_hdl, &ep->connect, in->client);
	if (result) {
		IPAERR("sps_connect fails.\n");
		goto sps_connect_fail;
	}

	sps->ipa_bam_hdl = ipa_ctx->bam_handle;
	sps->ipa_ep_idx = ipa_ep_idx;
	*clnt_hdl = ipa_ep_idx;
	memcpy(&sps->desc, &ep->connect.desc, sizeof(struct sps_mem_buffer));
	memcpy(&sps->data, &ep->connect.data, sizeof(struct sps_mem_buffer));

	ipa_ctx->skip_ep_cfg_shadow[ipa_ep_idx] = ep->skip_ep_cfg;
	if (!ep->skip_ep_cfg && IPA_CLIENT_IS_PROD(in->client))
		ipa_install_dflt_flt_rules(ipa_ep_idx);

	if (!ep->keep_ipa_awake)
		ipa_dec_client_disable_clks();

	IPADBG("client %d (ep: %d) connected\n", in->client, ipa_ep_idx);

	return 0;

sps_connect_fail:
	if (!ep->data_fifo_in_pipe_mem)
		dma_free_coherent(ipa_ctx->pdev,
				  ep->connect.data.size,
				  ep->connect.data.base,
				  ep->connect.data.phys_base);
	else
		ipa_pipe_mem_free(ep->data_fifo_pipe_mem_ofst,
				  ep->connect.data.size);

data_mem_alloc_fail:
	if (!ep->desc_fifo_in_pipe_mem)
		dma_free_coherent(ipa_ctx->pdev,
				  ep->connect.desc.size,
				  ep->connect.desc.base,
				  ep->connect.desc.phys_base);
	else
		ipa_pipe_mem_free(ep->desc_fifo_pipe_mem_ofst,
				  ep->connect.desc.size);

desc_mem_alloc_fail:
	sps_free_endpoint(ep->ep_hdl);
ipa_cfg_ep_fail:
	memset(&ipa_ctx->ep[ipa_ep_idx], 0, sizeof(struct ipa_ep_context));
	ipa_dec_client_disable_clks();
fail:
	return result;
}
EXPORT_SYMBOL(ipa_connect);

/**
 * ipa_disconnect() - low-level IPA client disconnect
 * @clnt_hdl:	[in] opaque client handle assigned by IPA to client
 *
 * Should be called by the driver of the peripheral that wants to disconnect
 * from IPA in BAM-BAM mode. this api expects caller to take responsibility to
 * free any needed headers, routing and filtering tables and rules as needed.
 *
 * Returns:	0 on success, negative on failure
 *
 * Note:	Should not be called from atomic context
 */
int ipa_disconnect(u32 clnt_hdl)
{
	int result;
	struct ipa_ep_context *ep;

	if (clnt_hdl >= IPA_NUM_PIPES || ipa_ctx->ep[clnt_hdl].valid == 0) {
		IPAERR("bad parm.\n");
		return -EINVAL;
	}

	ep = &ipa_ctx->ep[clnt_hdl];

	if (!ep->keep_ipa_awake)
		ipa_inc_client_enable_clks();

	result = ipa_disable_data_path(clnt_hdl);
	if (result) {
		IPAERR("disable data path failed res=%d clnt=%d.\n", result,
				clnt_hdl);
		return -EPERM;
	}

	result = sps_disconnect(ep->ep_hdl);
	if (result) {
		IPAERR("SPS disconnect failed.\n");
		return -EPERM;
	}

	if (!ep->desc_fifo_client_allocated &&
	     ep->connect.desc.base) {
		if (!ep->desc_fifo_in_pipe_mem)
			dma_free_coherent(ipa_ctx->pdev,
					  ep->connect.desc.size,
					  ep->connect.desc.base,
					  ep->connect.desc.phys_base);
		else
			ipa_pipe_mem_free(ep->desc_fifo_pipe_mem_ofst,
					  ep->connect.desc.size);
	}

	if (!ep->data_fifo_client_allocated &&
	     ep->connect.data.base) {
		if (!ep->data_fifo_in_pipe_mem)
			dma_free_coherent(ipa_ctx->pdev,
					  ep->connect.data.size,
					  ep->connect.data.base,
					  ep->connect.data.phys_base);
		else
			ipa_pipe_mem_free(ep->data_fifo_pipe_mem_ofst,
					  ep->connect.data.size);
	}

	result = sps_free_endpoint(ep->ep_hdl);
	if (result) {
		IPAERR("SPS de-alloc EP failed.\n");
		return -EPERM;
	}

	ipa_delete_dflt_flt_rules(clnt_hdl);

	memset(&ipa_ctx->ep[clnt_hdl], 0, sizeof(struct ipa_ep_context));

	ipa_dec_client_disable_clks();

	IPADBG("client (ep: %d) disconnected\n", clnt_hdl);

	return 0;
}
EXPORT_SYMBOL(ipa_disconnect);

/**
* ipa_reset_endpoint() - reset an endpoint from BAM perspective
* @clnt_hdl: [in] IPA client handle
*
* Returns:	0 on success, negative on failure
*
* Note:	Should not be called from atomic context
*/
int ipa_reset_endpoint(u32 clnt_hdl)
{
	int res;
	struct ipa_ep_context *ep;

	if (clnt_hdl >= IPA_NUM_PIPES) {
		IPAERR("Bad parameters.\n");
		return -EFAULT;
	}
	ep = &ipa_ctx->ep[clnt_hdl];

	ipa_inc_client_enable_clks();
	res = sps_disconnect(ep->ep_hdl);
	if (res) {
		IPAERR("sps_disconnect() failed, res=%d.\n", res);
		goto bail;
	} else {
		res = ipa_sps_connect_safe(ep->ep_hdl, &ep->connect,
			ep->client);
		if (res) {
			IPAERR("sps_connect() failed, res=%d.\n", res);
			goto bail;
		}
	}

bail:
	ipa_dec_client_disable_clks();

	return res;
}
EXPORT_SYMBOL(ipa_reset_endpoint);

/**
 * ipa_sps_connect_safe() - connect endpoint from BAM prespective
 * @h: [in] sps pipe handle
 * @connect: [in] sps connect parameters
 * @ipa_client: [in] ipa client handle representing the pipe
 *
 * This function connects a BAM pipe using SPS driver sps_connect() API
 * and by requesting uC interface to reset the pipe, avoids an IPA HW
 * limitation that does not allow reseting a BAM pipe during traffic in
 * IPA TX command queue.
 *
 * Returns:	0 on success, negative on failure
 */
int ipa_sps_connect_safe(struct sps_pipe *h, struct sps_connect *connect,
			 enum ipa_client_type ipa_client)
{
	int res;

	res = ipa_uc_reset_pipe(ipa_client);
	if (res)
		return res;

	return sps_connect(h, connect);
}
EXPORT_SYMBOL(ipa_sps_connect_safe);
