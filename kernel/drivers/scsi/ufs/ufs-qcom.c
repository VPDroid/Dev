/*
 * Copyright (c) 2013-2015, Linux Foundation. All rights reserved.
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

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/iopoll.h>
#include <linux/platform_device.h>

#include <linux/msm-bus.h>
#include <soc/qcom/scm.h>
#include <linux/phy/phy.h>

#include <linux/scsi/ufs/ufshcd.h>
#include <linux/scsi/ufs/ufs-qcom.h>
#include <linux/phy/phy-qcom-ufs.h>
#include "ufshci.h"
#include "ufs-qcom-ice.h"
#include "qcom-debugfs.h"

#define UFS_QCOM_DEFAULT_DBG_PRINT_EN	\
	(UFS_QCOM_DBG_PRINT_REGS_EN | UFS_QCOM_DBG_PRINT_TEST_BUS_EN)

enum {
	TSTBUS_UAWM,
	TSTBUS_UARM,
	TSTBUS_TXUC,
	TSTBUS_RXUC,
	TSTBUS_DFC,
	TSTBUS_TRLUT,
	TSTBUS_TMRLUT,
	TSTBUS_OCSC,
	TSTBUS_UTP_HCI,
	TSTBUS_COMBINED,
	TSTBUS_WRAPPER,
	TSTBUS_UNIPRO,
	TSTBUS_MAX,
};

static struct ufs_qcom_host *ufs_qcom_hosts[MAX_UFS_QCOM_HOSTS];

static void ufs_qcom_get_speed_mode(struct ufs_pa_layer_attr *p, char *result);
static int ufs_qcom_get_bus_vote(struct ufs_qcom_host *host,
		const char *speed_mode);
static int ufs_qcom_set_bus_vote(struct ufs_qcom_host *host, int vote);
static int ufs_qcom_update_sec_cfg(struct ufs_hba *hba, bool restore_sec_cfg);
static void ufs_qcom_get_default_testbus_cfg(struct ufs_qcom_host *host);

static void ufs_qcom_dump_regs(struct ufs_hba *hba, int offset, int len,
		char *prefix)
{
	print_hex_dump(KERN_ERR, prefix,
			len > 4 ? DUMP_PREFIX_OFFSET : DUMP_PREFIX_NONE,
			16, 4, hba->mmio_base + offset, len * 4, false);
}

static void ufs_qcom_dump_regs_wrapper(struct ufs_hba *hba, int offset, int len,
		char *prefix, void *priv)
{
	ufs_qcom_dump_regs(hba, offset, len, prefix);
}

static int ufs_qcom_get_connected_tx_lanes(struct ufs_hba *hba, u32 *tx_lanes)
{
	int err = 0;

	err = ufshcd_dme_get(hba,
			UIC_ARG_MIB(PA_CONNECTEDTXDATALANES), tx_lanes);
	if (err)
		dev_err(hba->dev, "%s: couldn't read PA_CONNECTEDTXDATALANES %d\n",
				__func__, err);

	return err;
}

static int ufs_qcom_host_clk_get(struct device *dev,
		const char *name, struct clk **clk_out)
{
	struct clk *clk;
	int err = 0;

	clk = devm_clk_get(dev, name);
	if (IS_ERR(clk)) {
		err = PTR_ERR(clk);
		dev_err(dev, "%s: failed to get %s err %d",
				__func__, name, err);
	} else {
		*clk_out = clk;
	}

	return err;
}

static int ufs_qcom_host_clk_enable(struct device *dev,
		const char *name, struct clk *clk)
{
	int err = 0;

	err = clk_prepare_enable(clk);
	if (err)
		dev_err(dev, "%s: %s enable failed %d\n", __func__, name, err);

	return err;
}

static void ufs_qcom_disable_lane_clks(struct ufs_qcom_host *host)
{
	if (!host->is_lane_clks_enabled)
		return;

	clk_disable_unprepare(host->tx_l1_sync_clk);
	clk_disable_unprepare(host->tx_l0_sync_clk);
	clk_disable_unprepare(host->rx_l1_sync_clk);
	clk_disable_unprepare(host->rx_l0_sync_clk);

	host->is_lane_clks_enabled = false;
}

static int ufs_qcom_enable_lane_clks(struct ufs_qcom_host *host)
{
	int err = 0;
	struct device *dev = host->hba->dev;

	if (host->is_lane_clks_enabled)
		return 0;

	err = ufs_qcom_host_clk_enable(dev,
			"rx_lane0_sync_clk", host->rx_l0_sync_clk);
	if (err)
		goto out;

	err = ufs_qcom_host_clk_enable(dev,
			"rx_lane1_sync_clk", host->rx_l1_sync_clk);
	if (err)
		goto disable_rx_l0;

	err = ufs_qcom_host_clk_enable(dev,
			"tx_lane0_sync_clk", host->tx_l0_sync_clk);
	if (err)
		goto disable_rx_l1;

	err = ufs_qcom_host_clk_enable(dev,
			"tx_lane1_sync_clk", host->tx_l1_sync_clk);
	if (err)
		goto disable_tx_l0;

	host->is_lane_clks_enabled = true;
	goto out;

disable_tx_l0:
	clk_disable_unprepare(host->tx_l0_sync_clk);
disable_rx_l1:
	clk_disable_unprepare(host->rx_l1_sync_clk);
disable_rx_l0:
	clk_disable_unprepare(host->rx_l0_sync_clk);
out:
	return err;
}

static int ufs_qcom_init_lane_clks(struct ufs_qcom_host *host)
{
	int err = 0;
	struct device *dev = host->hba->dev;

	err = ufs_qcom_host_clk_get(dev,
			"rx_lane0_sync_clk", &host->rx_l0_sync_clk);
	if (err)
		goto out;

	err = ufs_qcom_host_clk_get(dev,
			"rx_lane1_sync_clk", &host->rx_l1_sync_clk);
	if (err)
		goto out;

	err = ufs_qcom_host_clk_get(dev,
			"tx_lane0_sync_clk", &host->tx_l0_sync_clk);
	if (err)
		goto out;

	err = ufs_qcom_host_clk_get(dev,
			"tx_lane1_sync_clk", &host->tx_l1_sync_clk);
out:
	return err;
}

static int ufs_qcom_link_startup_post_change(struct ufs_hba *hba)
{
	struct ufs_qcom_host *host = hba->priv;
	struct phy *phy = host->generic_phy;
	u32 tx_lanes;
	int err = 0;

	err = ufs_qcom_get_connected_tx_lanes(hba, &tx_lanes);
	if (err)
		goto out;

	err = ufs_qcom_phy_set_tx_lane_enable(phy, tx_lanes);
	if (err)
		dev_err(hba->dev, "%s: ufs_qcom_phy_set_tx_lane_enable failed\n",
			__func__);

out:
	return err;
}

static int ufs_qcom_check_hibern8(struct ufs_hba *hba)
{
	int err;
	u32 tx_fsm_val = 0;
	unsigned long timeout = jiffies + msecs_to_jiffies(HBRN8_POLL_TOUT_MS);

	do {
		err = ufshcd_dme_get(hba,
			UIC_ARG_MIB(MPHY_TX_FSM_STATE), &tx_fsm_val);
		if (err || tx_fsm_val == TX_FSM_HIBERN8)
			break;

		/* sleep for max. 200us */
		usleep_range(100, 200);
	} while (time_before(jiffies, timeout));

	/*
	 * we might have scheduled out for long during polling so
	 * check the state again.
	 */
	if (time_after(jiffies, timeout))
		err = ufshcd_dme_get(hba,
				UIC_ARG_MIB(MPHY_TX_FSM_STATE), &tx_fsm_val);

	if (err) {
		dev_err(hba->dev, "%s: unable to get TX_FSM_STATE, err %d\n",
				__func__, err);
	} else if (tx_fsm_val != TX_FSM_HIBERN8) {
		err = tx_fsm_val;
		dev_err(hba->dev, "%s: invalid TX_FSM_STATE = %d\n",
				__func__, err);
	}

	return err;
}

static int ufs_qcom_power_up_sequence(struct ufs_hba *hba)
{
	struct ufs_qcom_host *host = hba->priv;
	struct phy *phy = host->generic_phy;
	int ret = 0;
	u8 major;
	u16 minor, step;

	/* Assert PHY reset and apply PHY calibration values */
	ufs_qcom_assert_reset(hba);
	/* provide 1ms delay to let the reset pulse propagate */
	usleep_range(1000, 1100);

	ufs_qcom_get_controller_revision(hba, &major, &minor, &step);
	ufs_qcom_phy_save_controller_version(phy, major, minor, step);
	ret = ufs_qcom_phy_calibrate_phy(phy);
	if (ret) {
		dev_err(hba->dev, "%s: ufs_qcom_phy_calibrate_phy() failed, ret = %d\n",
			__func__, ret);
		goto out;
	}

	/* De-assert PHY reset and start serdes */
	ufs_qcom_deassert_reset(hba);

	/*
	 * after reset deassertion, phy will need all ref clocks,
	 * voltage, current to settle down before starting serdes.
	 */
	usleep_range(1000, 1100);
	ret = ufs_qcom_phy_start_serdes(phy);
	if (ret) {
		dev_err(hba->dev, "%s: ufs_qcom_phy_start_serdes() failed, ret = %d\n",
			__func__, ret);
		goto out;
	}

	ret = ufs_qcom_phy_is_pcs_ready(phy);
	if (ret)
		dev_err(hba->dev, "%s: is_physical_coding_sublayer_ready() failed, ret = %d\n",
			__func__, ret);

out:
	return ret;
}

/*
 * The UTP controller has a number of internal clock gating cells (CGCs).
 * Internal hardware sub-modules within the UTP controller control the CGCs.
 * Hardware CGCs disable the clock to inactivate UTP sub-modules not involved
 * in a specific operation, UTP controller CGCs are by default disabled and
 * this function enables them (after every UFS link startup) to save some power
 * leakage.
 */
static void ufs_qcom_enable_hw_clk_gating(struct ufs_hba *hba)
{
	ufshcd_writel(hba,
		ufshcd_readl(hba, REG_UFS_CFG2) | REG_UFS_CFG2_CGC_EN_ALL,
		REG_UFS_CFG2);

	/* Ensure that HW clock gating is enabled before next operations */
	mb();
}

static int ufs_qcom_hce_enable_notify(struct ufs_hba *hba, bool status)
{
	struct ufs_qcom_host *host = hba->priv;
	int err = 0;

	switch (status) {
	case PRE_CHANGE:
		ufs_qcom_power_up_sequence(hba);
		/*
		 * The PHY PLL output is the source of tx/rx lane symbol
		 * clocks, hence, enable the lane clocks only after PHY
		 * is initialized.
		 */
		err = ufs_qcom_enable_lane_clks(host);
		if (!err && host->ice.pdev) {
			err = ufs_qcom_ice_init(host);
			if (err) {
				dev_err(hba->dev, "%s: ICE init failed (%d)\n",
					__func__, err);
				err = -EINVAL;
			}
		}

		break;
	case POST_CHANGE:
		/* check if UFS PHY moved from DISABLED to HIBERN8 */
		err = ufs_qcom_check_hibern8(hba);
		ufs_qcom_enable_hw_clk_gating(hba);

		break;
	default:
		dev_err(hba->dev, "%s: invalid status %d\n", __func__, status);
		err = -EINVAL;
		break;
	}
	return err;
}

/**
 * Returns non-zero for success (which rate of core_clk) and 0
 * in case of a failure
 */
static unsigned long
ufs_qcom_cfg_timers(struct ufs_hba *hba, u32 gear, u32 hs, u32 rate)
{
	struct ufs_clk_info *clki;
	u32 core_clk_period_in_ns;
	u32 tx_clk_cycles_per_us = 0;
	unsigned long core_clk_rate = 0;
	u32 core_clk_cycles_per_us = 0;

	static u32 pwm_fr_table[][2] = {
		{UFS_PWM_G1, 0x1},
		{UFS_PWM_G2, 0x1},
		{UFS_PWM_G3, 0x1},
		{UFS_PWM_G4, 0x1},
	};

	static u32 hs_fr_table_rA[][2] = {
		{UFS_HS_G1, 0x1F},
		{UFS_HS_G2, 0x3e},
	};

	static u32 hs_fr_table_rB[][2] = {
		{UFS_HS_G1, 0x24},
		{UFS_HS_G2, 0x49},
	};

	if (gear == 0) {
		dev_err(hba->dev, "%s: invalid gear = %d\n", __func__, gear);
		goto out_error;
	}

	list_for_each_entry(clki, &hba->clk_list_head, list) {
		if (!strcmp(clki->name, "core_clk"))
			core_clk_rate = clk_get_rate(clki->clk);
	}

	/* If frequency is smaller than 1MHz, set to 1MHz */
	if (core_clk_rate < DEFAULT_CLK_RATE_HZ)
		core_clk_rate = DEFAULT_CLK_RATE_HZ;

	core_clk_cycles_per_us = core_clk_rate / USEC_PER_SEC;
	if (ufshcd_readl(hba, REG_UFS_SYS1CLK_1US) != core_clk_cycles_per_us) {
		ufshcd_writel(hba, core_clk_cycles_per_us, REG_UFS_SYS1CLK_1US);
		/*
		 * make sure above write gets applied before we return from
		 * this function.
		 */
		mb();
	}

	core_clk_period_in_ns = NSEC_PER_SEC / core_clk_rate;
	core_clk_period_in_ns <<= OFFSET_CLK_NS_REG;
	core_clk_period_in_ns &= MASK_CLK_NS_REG;

	switch (hs) {
	case FASTAUTO_MODE:
	case FAST_MODE:
		if (rate == PA_HS_MODE_A) {
			if (gear > ARRAY_SIZE(hs_fr_table_rA)) {
				dev_err(hba->dev,
					"%s: index %d exceeds table size %zu\n",
					__func__, gear,
					ARRAY_SIZE(hs_fr_table_rA));
				goto out_error;
			}
			tx_clk_cycles_per_us = hs_fr_table_rA[gear-1][1];
		} else if (rate == PA_HS_MODE_B) {
			if (gear > ARRAY_SIZE(hs_fr_table_rB)) {
				dev_err(hba->dev,
					"%s: index %d exceeds table size %zu\n",
					__func__, gear,
					ARRAY_SIZE(hs_fr_table_rB));
				goto out_error;
			}
			tx_clk_cycles_per_us = hs_fr_table_rB[gear-1][1];
		} else {
			dev_err(hba->dev, "%s: invalid rate = %d\n",
				__func__, rate);
			goto out_error;
		}
		break;
	case SLOWAUTO_MODE:
	case SLOW_MODE:
		if (gear > ARRAY_SIZE(pwm_fr_table)) {
			dev_err(hba->dev,
					"%s: index %d exceeds table size %zu\n",
					__func__, gear,
					ARRAY_SIZE(pwm_fr_table));
			goto out_error;
		}
		tx_clk_cycles_per_us = pwm_fr_table[gear-1][1];
		break;
	case UNCHANGED:
	default:
		dev_err(hba->dev, "%s: invalid mode = %d\n", __func__, hs);
		goto out_error;
	}

	if (ufshcd_readl(hba, REG_UFS_TX_SYMBOL_CLK_NS_US) !=
	    (core_clk_period_in_ns | tx_clk_cycles_per_us)) {
		/* this register 2 fields shall be written at once */
		ufshcd_writel(hba, core_clk_period_in_ns | tx_clk_cycles_per_us,
			      REG_UFS_TX_SYMBOL_CLK_NS_US);
		/*
		 * make sure above write gets applied before we return from
		 * this function.
		 */
		mb();
	}
	goto out;

out_error:
	core_clk_rate = 0;
out:
	return core_clk_rate;
}

static int ufs_qcom_link_startup_notify(struct ufs_hba *hba, bool status)
{
	unsigned long core_clk_rate = 0;
	u32 core_clk_cycles_per_100ms;

	switch (status) {
	case PRE_CHANGE:
		core_clk_rate = ufs_qcom_cfg_timers(hba, UFS_PWM_G1,
						    SLOWAUTO_MODE, 0);
		if (!core_clk_rate) {
			dev_err(hba->dev, "%s: ufs_qcom_cfg_timers() failed\n",
				__func__);
			return -EINVAL;
		}
		core_clk_cycles_per_100ms =
			(core_clk_rate / MSEC_PER_SEC) * 100;
		ufshcd_writel(hba, core_clk_cycles_per_100ms,
					REG_UFS_PA_LINK_STARTUP_TIMER);
		break;
	case POST_CHANGE:
		ufs_qcom_link_startup_post_change(hba);
		break;
	default:
		break;
	}

	return 0;
}

static int ufs_qcom_suspend(struct ufs_hba *hba, enum ufs_pm_op pm_op)
{
	struct ufs_qcom_host *host = hba->priv;
	struct phy *phy = host->generic_phy;
	int ret = 0;

	if (ufs_qcom_is_link_off(hba)) {
		/*
		 * Disable the tx/rx lane symbol clocks before PHY is
		 * powered down as the PLL source should be disabled
		 * after downstream clocks are disabled.
		 */
		ufs_qcom_disable_lane_clks(host);
		phy_power_off(phy);
		ret = ufs_qcom_ice_suspend(host);
		if (ret)
			dev_err(hba->dev, "%s: failed ufs_qcom_ice_suspend %d\n",
					__func__, ret);

		/* Assert PHY soft reset */
		ufs_qcom_assert_reset(hba);
		goto out;
	}

	/*
	 * If UniPro link is not active, PHY ref_clk, main PHY analog power
	 * rail and low noise analog power rail for PLL can be switched off.
	 */
	if (!ufs_qcom_is_link_active(hba)) {
		if (ufs_qcom_phy_is_cfg_restore_quirk_enabled(phy) &&
		    ufs_qcom_is_link_hibern8(hba)) {
			ret = ufs_qcom_phy_save_configuration(phy);
			if (ret)
				dev_err(hba->dev, "%s: failed ufs_qcom_phy_save_configuration %d\n",
					__func__, ret);
		}
		phy_power_off(phy);
		ufs_qcom_ice_suspend(host);
	}

out:
	return ret;
}

static bool ufs_qcom_is_phy_config_restore_required(struct ufs_hba *hba)
{
	struct ufs_qcom_host *host = hba->priv;
	struct phy *phy = host->generic_phy;

	return ufs_qcom_phy_is_cfg_restore_quirk_enabled(phy)
		&& ufshcd_is_link_hibern8(hba)
		&& hba->is_sys_suspended;
}

static int ufs_qcom_resume(struct ufs_hba *hba, enum ufs_pm_op pm_op)
{
	struct ufs_qcom_host *host = hba->priv;
	struct phy *phy = host->generic_phy;
	int err;

	if (ufs_qcom_is_phy_config_restore_required(hba)) {
		ufs_qcom_assert_reset(hba);
		/* provide 1ms delay to let the reset pulse propagate */
		usleep_range(1000, 1100);
	}

	err = phy_power_on(phy);
	if (err) {
		dev_err(hba->dev, "%s: failed enabling regs, err = %d\n",
			__func__, err);
		goto out;
	}

	if (ufs_qcom_is_phy_config_restore_required(hba)) {
		ufs_qcom_phy_restore_swi_regs(phy);

		/* De-assert PHY reset and start serdes */
		ufs_qcom_deassert_reset(hba);

		/*
		 * after reset deassertion, phy will need all ref clocks,
		 * voltage, current to settle down before starting serdes.
		 */
		usleep_range(1000, 1100);

		err = ufs_qcom_phy_start_serdes(phy);
		if (err) {
			dev_err(hba->dev, "%s: ufs_qcom_phy_start_serdes() failed, err = %d\n",
				__func__, err);
			goto out;
		}

		err = ufs_qcom_phy_restore_configuration(phy);
		if (err) {
			dev_err(hba->dev, "%s: ufs_qcom_phy_restore_configuration() failed, err = %d\n",
				__func__, err);
			goto out;
		}

		err = ufs_qcom_phy_is_pcs_ready(phy);
		if (err) {
			dev_err(hba->dev, "%s: is_physical_coding_sublayer_ready() failed, err = %d\n",
				__func__, err);
			goto out;
		}
	}

	err = ufs_qcom_ice_resume(host);
	if (err) {
		dev_err(hba->dev, "%s: ufs_qcom_ice_resume failed, err = %d\n",
			__func__, err);
		goto out;
	}

	hba->is_sys_suspended = false;

out:
	return err;
}

static
int ufs_qcom_crytpo_engine_cfg(struct ufs_hba *hba, unsigned int task_tag)
{
	struct ufs_qcom_host *host = hba->priv;
	struct ufshcd_lrb *lrbp = &hba->lrb[task_tag];
	int err = 0;

	if (!host->ice.pdev ||
	    !lrbp->cmd || lrbp->command_type != UTP_CMD_TYPE_SCSI)
		goto out;

	err = ufs_qcom_ice_cfg(host, lrbp->cmd);
out:
	return err;
}

static
int ufs_qcom_crytpo_engine_reset(struct ufs_hba *hba)
{
	struct ufs_qcom_host *host = hba->priv;
	int err = 0;

	if (!host->ice.pdev)
		goto out;

	err = ufs_qcom_ice_reset(host);
out:
	return err;
}

static int ufs_qcom_crypto_engine_eh(struct ufs_hba *hba)
{
	struct ufs_qcom_host *host = hba->priv;
	int ice_status = 0;
	int err = 0;

	host->ice.crypto_engine_err = 0;

	if (host->ice.quirks &
	    UFS_QCOM_ICE_QUIRK_HANDLE_CRYPTO_ENGINE_ERRORS) {
		err = ufs_qcom_ice_get_status(host, &ice_status);
		if (!err)
			host->ice.crypto_engine_err = ice_status;

		if (host->ice.crypto_engine_err) {
			dev_err(hba->dev, "%s handling crypto engine error\n",
					__func__);
			/*
			 * block commands from scsi mid-layer.
			 * As crypto error is a fatal error and will result in
			 * a host reset we should leave scsi mid layer blocked
			 * until host reset is completed.
			 * Host reset will be handled in a seperate workqueue
			 * and will be triggered from ufshcd_check_errors.
			 */
			scsi_block_requests(hba->host);

			ufshcd_abort_outstanding_transfer_requests(hba,
					DID_TARGET_FAILURE);
		}
	}

	return host->ice.crypto_engine_err;
}

static int ufs_qcom_crypto_engine_get_err(struct ufs_hba *hba)
{
	struct ufs_qcom_host *host = hba->priv;

	return host->ice.crypto_engine_err;
}

static void ufs_qcom_crypto_engine_reset_err(struct ufs_hba *hba)
{
	struct ufs_qcom_host *host = hba->priv;

	host->ice.crypto_engine_err = 0;
}

struct ufs_qcom_dev_params {
	u32 pwm_rx_gear;	/* pwm rx gear to work in */
	u32 pwm_tx_gear;	/* pwm tx gear to work in */
	u32 hs_rx_gear;		/* hs rx gear to work in */
	u32 hs_tx_gear;		/* hs tx gear to work in */
	u32 rx_lanes;		/* number of rx lanes */
	u32 tx_lanes;		/* number of tx lanes */
	u32 rx_pwr_pwm;		/* rx pwm working pwr */
	u32 tx_pwr_pwm;		/* tx pwm working pwr */
	u32 rx_pwr_hs;		/* rx hs working pwr */
	u32 tx_pwr_hs;		/* tx hs working pwr */
	u32 hs_rate;		/* rate A/B to work in HS */
	u32 desired_working_mode;
};

static int ufs_qcom_get_pwr_dev_param(struct ufs_qcom_dev_params *qcom_param,
				      struct ufs_pa_layer_attr *dev_max,
				      struct ufs_pa_layer_attr *agreed_pwr)
{
	int min_qcom_gear;
	int min_dev_gear;
	bool is_dev_sup_hs = false;
	bool is_qcom_max_hs = false;

	if (dev_max->pwr_rx == FAST_MODE)
		is_dev_sup_hs = true;

	if (qcom_param->desired_working_mode == FAST) {
		is_qcom_max_hs = true;
		min_qcom_gear = min_t(u32, qcom_param->hs_rx_gear,
				      qcom_param->hs_tx_gear);
	} else {
		min_qcom_gear = min_t(u32, qcom_param->pwm_rx_gear,
				      qcom_param->pwm_tx_gear);
	}

	/*
	 * device doesn't support HS but qcom_param->desired_working_mode is
	 * HS, thus device and qcom_param don't agree
	 */
	if (!is_dev_sup_hs && is_qcom_max_hs) {
		pr_err("%s: failed to agree on power mode (device doesn't support HS but requested power is HS)\n",
			__func__);
		return -ENOTSUPP;
	} else if (is_dev_sup_hs && is_qcom_max_hs) {
		/*
		 * since device supports HS, it supports FAST_MODE.
		 * since qcom_param->desired_working_mode is also HS
		 * then final decision (FAST/FASTAUTO) is done according
		 * to qcom_params as it is the restricting factor
		 */
		agreed_pwr->pwr_rx = agreed_pwr->pwr_tx =
						qcom_param->rx_pwr_hs;
	} else {
		/*
		 * here qcom_param->desired_working_mode is PWM.
		 * it doesn't matter whether device supports HS or PWM,
		 * in both cases qcom_param->desired_working_mode will
		 * determine the mode
		 */
		 agreed_pwr->pwr_rx = agreed_pwr->pwr_tx =
						qcom_param->rx_pwr_pwm;
	}

	/*
	 * we would like tx to work in the minimum number of lanes
	 * between device capability and vendor preferences.
	 * the same decision will be made for rx
	 */
	agreed_pwr->lane_tx = min_t(u32, dev_max->lane_tx,
						qcom_param->tx_lanes);
	agreed_pwr->lane_rx = min_t(u32, dev_max->lane_rx,
						qcom_param->rx_lanes);

	/* device maximum gear is the minimum between device rx and tx gears */
	min_dev_gear = min_t(u32, dev_max->gear_rx, dev_max->gear_tx);

	/*
	 * if both device capabilities and vendor pre-defined preferences are
	 * both HS or both PWM then set the minimum gear to be the chosen
	 * working gear.
	 * if one is PWM and one is HS then the one that is PWM get to decide
	 * what is the gear, as it is the one that also decided previously what
	 * pwr the device will be configured to.
	 */
	if ((is_dev_sup_hs && is_qcom_max_hs) ||
	    (!is_dev_sup_hs && !is_qcom_max_hs))
		agreed_pwr->gear_rx = agreed_pwr->gear_tx =
			min_t(u32, min_dev_gear, min_qcom_gear);
	else if (!is_dev_sup_hs)
		agreed_pwr->gear_rx = agreed_pwr->gear_tx = min_dev_gear;
	else
		agreed_pwr->gear_rx = agreed_pwr->gear_tx = min_qcom_gear;

	agreed_pwr->hs_rate = qcom_param->hs_rate;
	return 0;
}

static int ufs_qcom_update_bus_bw_vote(struct ufs_qcom_host *host)
{
	int vote;
	int err = 0;
	char mode[BUS_VECTOR_NAME_LEN];

	ufs_qcom_get_speed_mode(&host->dev_req_params, mode);

	vote = ufs_qcom_get_bus_vote(host, mode);
	if (vote >= 0)
		err = ufs_qcom_set_bus_vote(host, vote);
	else
		err = vote;

	if (err)
		dev_err(host->hba->dev, "%s: failed %d\n", __func__, err);
	else
		host->bus_vote.saved_vote = vote;
	return err;
}

#define UFS_REF_CLK_EN	(1 << 5)
static void ufs_qcom_enable_dev_ref_clk(struct ufs_qcom_host *host, bool enable)
{
	if (host->dev_ref_clk_ctrl_mmio &&
	    (enable ^ host->is_dev_ref_clk_enabled)) {
		u32 temp = readl_relaxed(host->dev_ref_clk_ctrl_mmio);

		if (enable)
			temp |= UFS_REF_CLK_EN;
		else
			temp &= ~UFS_REF_CLK_EN;

		/*
		 * If we are here to disable this clock it might be immediately
		 * after entering into hibern8 in which case we need to make
		 * sure that device ref_clk is active at least 1us after the
		 * hibern8 enter.
		 */
		if (!enable)
			udelay(1);

		writel_relaxed(temp, host->dev_ref_clk_ctrl_mmio);

		/* ensure that ref_clk is enabled/disabled before we return */
		wmb();

		/*
		 * If we call hibern8 exit after this, we need to make sure that
		 * device ref_clk is stable for at least 1us before the hibern8
		 * exit command.
		 */
		if (enable)
			udelay(1);

		host->is_dev_ref_clk_enabled = enable;
	}
}

static int ufs_qcom_pwr_change_notify(struct ufs_hba *hba,
				bool status,
				struct ufs_pa_layer_attr *dev_max_params,
				struct ufs_pa_layer_attr *dev_req_params)
{
	u32 val;
	struct ufs_qcom_host *host = hba->priv;
	struct phy *phy = host->generic_phy;
	struct ufs_qcom_dev_params ufs_qcom_cap;
	int ret = 0;
	int res = 0;

	if (!dev_req_params) {
		pr_err("%s: incoming dev_req_params is NULL\n", __func__);
		ret = -EINVAL;
		goto out;
	}

	switch (status) {
	case PRE_CHANGE:
		ufs_qcom_cap.tx_lanes = UFS_QCOM_LIMIT_NUM_LANES_TX;
		ufs_qcom_cap.rx_lanes = UFS_QCOM_LIMIT_NUM_LANES_RX;
		ufs_qcom_cap.hs_rx_gear = UFS_QCOM_LIMIT_HSGEAR_RX;
		ufs_qcom_cap.hs_tx_gear = UFS_QCOM_LIMIT_HSGEAR_TX;
		ufs_qcom_cap.pwm_rx_gear = UFS_QCOM_LIMIT_PWMGEAR_RX;
		ufs_qcom_cap.pwm_tx_gear = UFS_QCOM_LIMIT_PWMGEAR_TX;
		ufs_qcom_cap.rx_pwr_pwm = UFS_QCOM_LIMIT_RX_PWR_PWM;
		ufs_qcom_cap.tx_pwr_pwm = UFS_QCOM_LIMIT_TX_PWR_PWM;
		ufs_qcom_cap.rx_pwr_hs = UFS_QCOM_LIMIT_RX_PWR_HS;
		ufs_qcom_cap.tx_pwr_hs = UFS_QCOM_LIMIT_TX_PWR_HS;
		ufs_qcom_cap.hs_rate = UFS_QCOM_LIMIT_HS_RATE;
		ufs_qcom_cap.desired_working_mode =
					UFS_QCOM_LIMIT_DESIRED_MODE;

		ret = ufs_qcom_get_pwr_dev_param(&ufs_qcom_cap,
						 dev_max_params,
						 dev_req_params);
		if (ret) {
			pr_err("%s: failed to determine capabilities\n",
					__func__);
			goto out;
		}

		/* enable the device ref clock before changing to HS mode */
		if (!ufshcd_is_hs_mode(&hba->pwr_info) &&
			ufshcd_is_hs_mode(dev_req_params))
			ufs_qcom_enable_dev_ref_clk(host, true);
		break;
	case POST_CHANGE:
		if (!ufs_qcom_cfg_timers(hba, dev_req_params->gear_rx,
					dev_req_params->pwr_rx,
					dev_req_params->hs_rate)) {
			dev_err(hba->dev, "%s: ufs_qcom_cfg_timers() failed\n",
				__func__);
			/*
			 * we return error code at the end of the routine,
			 * but continue to configure UFS_PHY_TX_LANE_ENABLE
			 * and bus voting as usual
			 */
			ret = -EINVAL;
		}

		val = ~(MAX_U32 << dev_req_params->lane_tx);
		res = ufs_qcom_phy_set_tx_lane_enable(phy, val);
		if (res) {
			dev_err(hba->dev, "%s: ufs_qcom_phy_set_tx_lane_enable() failed res = %d\n",
				__func__, res);
			ret = res;
		}

		/* cache the power mode parameters to use internally */
		memcpy(&host->dev_req_params,
				dev_req_params, sizeof(*dev_req_params));
		ufs_qcom_update_bus_bw_vote(host);

		/* disable the device ref clock if entered PWM mode */
		if (ufshcd_is_hs_mode(&hba->pwr_info) &&
			!ufshcd_is_hs_mode(dev_req_params))
			ufs_qcom_enable_dev_ref_clk(host, false);
		break;
	default:
		ret = -EINVAL;
		break;
	}
out:
	return ret;
}

/**
 * ufs_qcom_advertise_quirks - advertise the known QCOM UFS controller quirks
 * @hba: host controller instance
 *
 * QCOM UFS host controller might have some non standard behaviours (quirks)
 * than what is specified by UFSHCI specification. Advertise all such
 * quirks to standard UFS host controller driver so standard takes them into
 * account.
 */
static void ufs_qcom_advertise_quirks(struct ufs_hba *hba)
{
	struct ufs_qcom_host *host = hba->priv;

	if (host->hw_ver.major == 0x1) {
		hba->quirks |= (UFSHCD_QUIRK_DELAY_BEFORE_DME_CMDS
			      | UFSHCD_QUIRK_BROKEN_PA_RXHSUNTERMCAP
			      | UFSHCD_QUIRK_BROKEN_LCC
			      | UFSHCD_QUIRK_DME_PEER_ACCESS_AUTO_MODE);

		if (host->hw_ver.minor == 0x001 && host->hw_ver.step == 0x0001)
			hba->quirks |= UFSHCD_QUIRK_BROKEN_INTR_AGGR;
	}
}

static int ufs_qcom_get_bus_vote(struct ufs_qcom_host *host,
		const char *speed_mode)
{
	struct device *dev = host->hba->dev;
	struct device_node *np = dev->of_node;
	int err;
	const char *key = "qcom,bus-vector-names";

	if (!speed_mode) {
		err = -EINVAL;
		goto out;
	}

	if (host->bus_vote.is_max_bw_needed && !!strcmp(speed_mode, "MIN"))
		err = of_property_match_string(np, key, "MAX");
	else
		err = of_property_match_string(np, key, speed_mode);

out:
	if (err < 0)
		dev_err(dev, "%s: Invalid %s mode %d\n",
				__func__, speed_mode, err);
	return err;
}

static int ufs_qcom_set_bus_vote(struct ufs_qcom_host *host, int vote)
{
	int err = 0;

	if (vote != host->bus_vote.curr_vote) {
		err = msm_bus_scale_client_update_request(
				host->bus_vote.client_handle, vote);
		if (err) {
			dev_err(host->hba->dev,
				"%s: msm_bus_scale_client_update_request() failed: bus_client_handle=0x%x, vote=%d, err=%d\n",
				__func__, host->bus_vote.client_handle,
				vote, err);
			goto out;
		}

		host->bus_vote.curr_vote = vote;
	}
out:
	return err;
}

static void ufs_qcom_get_speed_mode(struct ufs_pa_layer_attr *p, char *result)
{
	int gear = max_t(u32, p->gear_rx, p->gear_tx);
	int lanes = max_t(u32, p->lane_rx, p->lane_tx);
	int pwr;

	/* default to PWM Gear 1, Lane 1 if power mode is not initialized */
	if (!gear)
		gear = 1;

	if (!lanes)
		lanes = 1;

	if (!p->pwr_rx && !p->pwr_tx) {
		pwr = SLOWAUTO_MODE;
		snprintf(result, BUS_VECTOR_NAME_LEN, "MIN");
	} else if (p->pwr_rx == FAST_MODE || p->pwr_rx == FASTAUTO_MODE ||
		 p->pwr_tx == FAST_MODE || p->pwr_tx == FASTAUTO_MODE) {
		pwr = FAST_MODE;
		snprintf(result, BUS_VECTOR_NAME_LEN, "%s_R%s_G%d_L%d", "HS",
			 p->hs_rate == PA_HS_MODE_B ? "B" : "A", gear, lanes);
	} else {
		pwr = SLOW_MODE;
		snprintf(result, BUS_VECTOR_NAME_LEN, "%s_G%d_L%d",
			 "PWM", gear, lanes);
	}
}

static int ufs_qcom_setup_clocks(struct ufs_hba *hba, bool on)
{
	struct ufs_qcom_host *host = hba->priv;
	int err;
	int vote = 0;

	/*
	 * In case ufs_qcom_init() is not yet done, simply ignore.
	 * This ufs_qcom_setup_clocks() shall be called from
	 * ufs_qcom_init() after init is done.
	 */
	if (!host)
		return 0;

	if (on) {
		err = ufs_qcom_phy_enable_iface_clk(host->generic_phy);
		if (err)
			goto out;

		err = ufs_qcom_phy_enable_ref_clk(host->generic_phy);
		if (err) {
			dev_err(hba->dev, "%s enable phy ref clock failed, err=%d\n",
				__func__, err);
			ufs_qcom_phy_disable_iface_clk(host->generic_phy);
			goto out;
		}
		/* enable the device ref clock for HS mode*/
		if (ufshcd_is_hs_mode(&hba->pwr_info))
			ufs_qcom_enable_dev_ref_clk(host, true);
		vote = host->bus_vote.saved_vote;
		if (vote == host->bus_vote.min_bw_vote)
			ufs_qcom_update_bus_bw_vote(host);
	} else {
		/* M-PHY RMMI interface clocks can be turned off */
		ufs_qcom_phy_disable_iface_clk(host->generic_phy);
		if (!ufs_qcom_is_link_active(hba)) {
			/* turn off UFS local PHY ref_clk */
			ufs_qcom_phy_disable_ref_clk(host->generic_phy);
			/* disable device ref_clk */
			ufs_qcom_enable_dev_ref_clk(host, false);
		}
		vote = host->bus_vote.min_bw_vote;
	}

	err = ufs_qcom_set_bus_vote(host, vote);
	if (err)
		dev_err(hba->dev, "%s: set bus vote failed %d\n",
				__func__, err);

out:
	return err;
}

static ssize_t
show_ufs_to_mem_max_bus_bw(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);
	struct ufs_qcom_host *host = hba->priv;

	return snprintf(buf, PAGE_SIZE, "%u\n",
			host->bus_vote.is_max_bw_needed);
}

static ssize_t
store_ufs_to_mem_max_bus_bw(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);
	struct ufs_qcom_host *host = hba->priv;
	uint32_t value;

	if (!kstrtou32(buf, 0, &value)) {
		host->bus_vote.is_max_bw_needed = !!value;
		ufs_qcom_update_bus_bw_vote(host);
	}

	return count;
}

static int ufs_qcom_bus_register(struct ufs_qcom_host *host)
{
	int err;
	struct msm_bus_scale_pdata *bus_pdata;
	struct device *dev = host->hba->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct device_node *np = dev->of_node;

	bus_pdata = msm_bus_cl_get_pdata(pdev);
	if (!bus_pdata) {
		dev_err(dev, "%s: failed to get bus vectors\n", __func__);
		err = -ENODATA;
		goto out;
	}

	err = of_property_count_strings(np, "qcom,bus-vector-names");
	if (err < 0 || err != bus_pdata->num_usecases) {
		dev_err(dev, "%s: qcom,bus-vector-names not specified correctly %d\n",
				__func__, err);
		goto out;
	}

	host->bus_vote.client_handle = msm_bus_scale_register_client(bus_pdata);
	if (!host->bus_vote.client_handle) {
		dev_err(dev, "%s: msm_bus_scale_register_client failed\n",
				__func__);
		err = -EFAULT;
		goto out;
	}

	/* cache the vote index for minimum and maximum bandwidth */
	host->bus_vote.min_bw_vote = ufs_qcom_get_bus_vote(host, "MIN");
	host->bus_vote.max_bw_vote = ufs_qcom_get_bus_vote(host, "MAX");

	host->bus_vote.max_bus_bw.show = show_ufs_to_mem_max_bus_bw;
	host->bus_vote.max_bus_bw.store = store_ufs_to_mem_max_bus_bw;
	sysfs_attr_init(&host->bus_vote.max_bus_bw.attr);
	host->bus_vote.max_bus_bw.attr.name = "max_bus_bw";
	host->bus_vote.max_bus_bw.attr.mode = S_IRUGO | S_IWUSR;
	err = device_create_file(dev, &host->bus_vote.max_bus_bw);
out:
	return err;
}

#define	ANDROID_BOOT_DEV_MAX	30
static char android_boot_dev[ANDROID_BOOT_DEV_MAX];
static int get_android_boot_dev(char *str)
{
	strlcpy(android_boot_dev, str, ANDROID_BOOT_DEV_MAX);
	return 1;
}
__setup("androidboot.bootdevice=", get_android_boot_dev);

/**
 * ufs_qcom_init - bind phy with controller
 * @hba: host controller instance
 *
 * Binds PHY with controller and powers up PHY enabling clocks
 * and regulators.
 *
 * Returns -EPROBE_DEFER if binding fails, returns negative error
 * on phy power up failure and returns zero on success.
 */
static int ufs_qcom_init(struct ufs_hba *hba)
{
	int err;
	struct device *dev = hba->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct ufs_qcom_host *host;
	struct resource *res;

	if (strlen(android_boot_dev) && strcmp(android_boot_dev, dev_name(dev)))
		return -ENODEV;

	host = devm_kzalloc(dev, sizeof(*host), GFP_KERNEL);
	if (!host) {
		err = -ENOMEM;
		dev_err(dev, "%s: no memory for qcom ufs host\n", __func__);
		goto out;
	}

	host->hba = hba;
	hba->priv = (void *)host;

	err = ufs_qcom_ice_get_dev(host);
	if (err == -EPROBE_DEFER) {
		/*
		 * UFS driver might be probed before ICE driver does.
		 * In that case we would like to return EPROBE_DEFER code
		 * in order to delay its probing.
		 */
		dev_err(dev, "%s: required ICE device not probed yet err = %d\n",
			__func__, err);
		goto out_host_free;

	} else if (err == -ENODEV) {
		/*
		 * ICE device is not enabled in DTS file. No need for further
		 * initialization of ICE driver.
		 */
		dev_warn(dev, "%s: ICE device is not enabled",
			__func__);
	} else if (err) {
		dev_err(dev, "%s: ufs_qcom_ice_get_dev failed %d\n",
			__func__, err);
		goto out_host_free;
	}

	host->generic_phy = devm_phy_get(dev, "ufsphy");

	if (IS_ERR(host->generic_phy)) {
		err = PTR_ERR(host->generic_phy);
		dev_err(dev, "%s: PHY get failed %d\n", __func__, err);
		goto out;
	}

	/* restore the secure configuration */
	ufs_qcom_update_sec_cfg(hba, true);

	err = ufs_qcom_bus_register(host);
	if (err)
		goto out_host_free;

	phy_init(host->generic_phy);
	err = phy_power_on(host->generic_phy);
	if (err)
		goto out_unregister_bus;

	err = ufs_qcom_init_lane_clks(host);
	if (err)
		goto out_disable_phy;

	ufs_qcom_get_controller_revision(hba, &host->hw_ver.major,
		&host->hw_ver.minor, &host->hw_ver.step);
	ufs_qcom_advertise_quirks(hba);

	hba->caps |= UFSHCD_CAP_CLK_GATING | UFSHCD_CAP_CLK_SCALING;
	hba->caps |= UFSHCD_CAP_AUTO_BKOPS_SUSPEND;
	hba->caps |= UFSHCD_CAP_HIBERN8_ENTER_ON_IDLE;
	ufs_qcom_setup_clocks(hba, true);

	/* "dev_ref_clk_ctrl_mem" is optional resource */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res) {
		dev_dbg(dev, "%s: dev_ref_clk_ctrl_mem resource not found\n",
			__func__);
	} else {
		host->dev_ref_clk_ctrl_mmio = devm_ioremap_resource(dev, res);
		if (IS_ERR(host->dev_ref_clk_ctrl_mmio)) {
			dev_warn(dev,
				"%s: could not map dev_ref_clk_ctrl_mmio, err %ld\n",
				__func__, PTR_ERR(host->dev_ref_clk_ctrl_mmio));
			host->dev_ref_clk_ctrl_mmio = NULL;
		}
	}

	if (hba->dev->id < MAX_UFS_QCOM_HOSTS)
		ufs_qcom_hosts[hba->dev->id] = host;

	host->dbg_print_en |= UFS_QCOM_DEFAULT_DBG_PRINT_EN;
	ufs_qcom_get_default_testbus_cfg(host);
	err = ufs_qcom_testbus_config(host);
	if (err) {
		dev_warn(dev, "%s: failed to configure the testbus %d\n",
				__func__, err);
		err = 0;
	}

	goto out;

out_disable_phy:
	phy_power_off(host->generic_phy);
out_unregister_bus:
	phy_exit(host->generic_phy);
	msm_bus_scale_unregister_client(host->bus_vote.client_handle);
out_host_free:
	devm_kfree(dev, host);
	hba->priv = NULL;
out:
	return err;
}

static void ufs_qcom_exit(struct ufs_hba *hba)
{
	struct ufs_qcom_host *host = hba->priv;

	msm_bus_scale_unregister_client(host->bus_vote.client_handle);
	ufs_qcom_disable_lane_clks(host);
	phy_power_off(host->generic_phy);
}


void ufs_qcom_clk_scale_notify(struct ufs_hba *hba)
{
	struct ufs_qcom_host *host = hba->priv;
	struct ufs_pa_layer_attr *dev_req_params = &host->dev_req_params;

	if (!dev_req_params)
		return;

	ufs_qcom_cfg_timers(hba, dev_req_params->gear_rx,
				dev_req_params->pwr_rx,
				dev_req_params->hs_rate);
	ufs_qcom_update_bus_bw_vote(host);
}

/*
 * This function should be called to restore the security configuration of UFS
 * register space after coming out of UFS host core power collapse.
 *
 * @hba: host controller instance
 * @restore_sec_cfg: Set "true" if secure configuration needs to be restored
 * and set "false" when secure configuration is lost.
 */
static int ufs_qcom_update_sec_cfg(struct ufs_hba *hba, bool restore_sec_cfg)
{
	int ret = 0, scm_ret = 0;
	struct ufs_qcom_host *host = hba->priv;

	/* scm command buffer structrue */
	struct msm_scm_cmd_buf {
		unsigned int device_id;
		unsigned int spare;
	} cbuf = {0};
	#define RESTORE_SEC_CFG_CMD	0x2
	#define UFS_TZ_DEV_ID		19

	if (!host || !hba->vreg_info.vdd_hba ||
	    !(host->sec_cfg_updated ^ restore_sec_cfg)) {
		return 0;
	} else if (!restore_sec_cfg) {
		/*
		 * Clear the flag so next time when this function is called
		 * with restore_sec_cfg set to true, we can restore the secure
		 * configuration.
		 */
		host->sec_cfg_updated = false;
		goto out;
	} else if (hba->clk_gating.state != CLKS_ON) {
		/*
		 * Clocks should be ON to restore the host controller secure
		 * configuration.
		 */
		goto out;
	}

	/*
	 * If we are here, Host controller clocks are running, Host controller
	 * power collapse feature is supported and Host controller has just came
	 * out of power collapse.
	 */
	cbuf.device_id = UFS_TZ_DEV_ID;
	ret = scm_restore_sec_cfg(cbuf.device_id, cbuf.spare, &scm_ret);
	if (ret || scm_ret) {
		dev_err(hba->dev, "%s: failed, ret %d scm_ret %d\n",
			__func__, ret, scm_ret);
		if (!ret)
			ret = scm_ret;
	} else {
		host->sec_cfg_updated = true;
	}

out:
	dev_dbg(hba->dev, "%s: ip: restore_sec_cfg %d, op: restore_sec_cfg %d, ret %d scm_ret %d\n",
		__func__, restore_sec_cfg, host->sec_cfg_updated, ret, scm_ret);
	return ret;
}

void ufs_qcom_print_hw_debug_reg_all(struct ufs_hba *hba, void *priv,
		void (*print_fn)(struct ufs_hba *hba, int offset, int num_regs,
				char *str, void *priv))
{
	u32 reg;
	struct ufs_qcom_host *host;

	if (unlikely(!hba)) {
		pr_err("%s: hba is NULL\n", __func__);
		return;
	}
	if (unlikely(!print_fn)) {
		dev_err(hba->dev, "%s: print_fn is NULL\n", __func__);
		return;
	}

	host = hba->priv;
	if (!(host->dbg_print_en & UFS_QCOM_DBG_PRINT_REGS_EN))
		return;

	print_fn(hba, UFS_UFS_DBG_RD_REG_OCSC, 44,
			"UFS_UFS_DBG_RD_REG_OCSC ", priv);

	reg = ufshcd_readl(hba, REG_UFS_CFG1);
	reg |= UFS_BIT(17);
	ufshcd_writel(hba, reg, REG_UFS_CFG1);

	print_fn(hba, UFS_UFS_DBG_RD_EDTL_RAM, 32,
			"UFS_UFS_DBG_RD_EDTL_RAM ", priv);
	print_fn(hba, UFS_UFS_DBG_RD_DESC_RAM, 128,
			"UFS_UFS_DBG_RD_DESC_RAM ", priv);
	print_fn(hba, UFS_UFS_DBG_RD_PRDT_RAM, 64,
			"UFS_UFS_DBG_RD_PRDT_RAM ", priv);

	ufshcd_writel(hba, (reg & ~UFS_BIT(17)), REG_UFS_CFG1);

	print_fn(hba, UFS_DBG_RD_REG_UAWM, 4,
			"UFS_DBG_RD_REG_UAWM ", priv);
	print_fn(hba, UFS_DBG_RD_REG_UARM, 4,
			"UFS_DBG_RD_REG_UARM ", priv);
	print_fn(hba, UFS_DBG_RD_REG_TXUC, 48,
			"UFS_DBG_RD_REG_TXUC ", priv);
	print_fn(hba, UFS_DBG_RD_REG_RXUC, 27,
			"UFS_DBG_RD_REG_RXUC ", priv);
	print_fn(hba, UFS_DBG_RD_REG_DFC, 19,
			"UFS_DBG_RD_REG_DFC ", priv);
	print_fn(hba, UFS_DBG_RD_REG_TRLUT, 34,
			"UFS_DBG_RD_REG_TRLUT ", priv);
	print_fn(hba, UFS_DBG_RD_REG_TMRLUT, 9,
			"UFS_DBG_RD_REG_TMRLUT ", priv);
}

static void ufs_qcom_enable_test_bus(struct ufs_qcom_host *host)
{
	if (host->dbg_print_en & UFS_QCOM_DBG_PRINT_TEST_BUS_EN)
		ufshcd_rmwl(host->hba, TEST_BUS_EN, TEST_BUS_EN, REG_UFS_CFG1);
	else
		ufshcd_rmwl(host->hba, TEST_BUS_EN, 0, REG_UFS_CFG1);
}

static void ufs_qcom_get_default_testbus_cfg(struct ufs_qcom_host *host)
{
	/* provide a legal default configuration */
	host->testbus.select_major = TSTBUS_UAWM;
	host->testbus.select_minor = 1;
}

static bool ufs_qcom_testbus_cfg_is_ok(struct ufs_qcom_host *host)
{
	if (host->testbus.select_major >= TSTBUS_MAX) {
		dev_err(host->hba->dev,
			"%s: UFS_CFG1[TEST_BUS_SEL} may not equal 0x%05X\n",
			__func__, host->testbus.select_major);
		return false;
	}

	/*
	 * Not performing check for each individual select_major
	 * mappings of select_minor, since there is no harm in
	 * configuring a non-existent select_minor
	 */
	if (host->testbus.select_major > 0x1F) {
		dev_err(host->hba->dev,
			"%s: 0x%05X is not a legal testbus option\n",
			__func__, host->testbus.select_minor);
		return false;
	}

	return true;
}

int ufs_qcom_testbus_config(struct ufs_qcom_host *host)
{
	int reg;
	int offset;
	u32 mask = TEST_BUS_SUB_SEL_MASK;

	if (!host)
		return -EINVAL;

	if (!ufs_qcom_testbus_cfg_is_ok(host))
		return -EPERM;

	switch (host->testbus.select_major) {
	case TSTBUS_UAWM:
		reg = UFS_TEST_BUS_CTRL_0;
		offset = 24;
		break;
	case TSTBUS_UARM:
		reg = UFS_TEST_BUS_CTRL_0;
		offset = 16;
		break;
	case TSTBUS_TXUC:
		reg = UFS_TEST_BUS_CTRL_0;
		offset = 8;
		break;
	case TSTBUS_RXUC:
		reg = UFS_TEST_BUS_CTRL_0;
		offset = 0;
		break;
	case TSTBUS_DFC:
		reg = UFS_TEST_BUS_CTRL_1;
		offset = 24;
		break;
	case TSTBUS_TRLUT:
		reg = UFS_TEST_BUS_CTRL_1;
		offset = 16;
		break;
	case TSTBUS_TMRLUT:
		reg = UFS_TEST_BUS_CTRL_1;
		offset = 8;
		break;
	case TSTBUS_OCSC:
		reg = UFS_TEST_BUS_CTRL_1;
		offset = 0;
		break;
	case TSTBUS_WRAPPER:
		reg = UFS_TEST_BUS_CTRL_2;
		offset = 16;
		break;
	case TSTBUS_COMBINED:
		reg = UFS_TEST_BUS_CTRL_2;
		offset = 8;
		break;
	case TSTBUS_UTP_HCI:
		reg = UFS_TEST_BUS_CTRL_2;
		offset = 0;
		break;
	case TSTBUS_UNIPRO:
		reg = UFS_UNIPRO_CFG;
		offset = 1;
		break;
	/*
	 * No need for a default case, since
	 * ufs_qcom_testbus_cfg_is_ok() checks that the configuration
	 * is legal
	 */
	}
	mask <<= offset;
	ufshcd_hold(host->hba, false);

	pm_runtime_get_sync(host->hba->dev);
	ufshcd_rmwl(host->hba, TEST_BUS_SEL,
		    (u32)host->testbus.select_major << 19,
		    REG_UFS_CFG1);
	ufshcd_rmwl(host->hba, mask,
		    (u32)host->testbus.select_minor << offset,
		    reg);
	ufs_qcom_enable_test_bus(host);

	pm_runtime_put_sync(host->hba->dev);
	ufshcd_release(host->hba, false);

	return 0;
}

static void ufs_qcom_testbus_read(struct ufs_hba *hba)
{
	ufs_qcom_dump_regs(hba, UFS_TEST_BUS, 1, "UFS_TEST_BUS ");
}

static void ufs_qcom_dump_dbg_regs(struct ufs_hba *hba)
{
	struct ufs_qcom_host *host = hba->priv;

	ufs_qcom_dump_regs(hba, REG_UFS_SYS1CLK_1US, 16,
			"HCI Vendor Specific Registers ");

	ufs_qcom_print_hw_debug_reg_all(hba, NULL, ufs_qcom_dump_regs_wrapper);
	ufs_qcom_testbus_read(hba);
	ufs_qcom_ice_print_regs(host);
}

/**
 * struct ufs_hba_qcom_vops - UFS QCOM specific variant operations
 *
 * The variant operations configure the necessary controller and PHY
 * handshake during initialization.
 */
const struct ufs_hba_variant_ops ufs_hba_qcom_vops = {
	.name                   = "qcom",
	.init                   = ufs_qcom_init,
	.exit                   = ufs_qcom_exit,
	.clk_scale_notify	= ufs_qcom_clk_scale_notify,
	.setup_clocks           = ufs_qcom_setup_clocks,
	.hce_enable_notify      = ufs_qcom_hce_enable_notify,
	.link_startup_notify    = ufs_qcom_link_startup_notify,
	.pwr_change_notify	= ufs_qcom_pwr_change_notify,
	.suspend		= ufs_qcom_suspend,
	.resume			= ufs_qcom_resume,
	.update_sec_cfg		= ufs_qcom_update_sec_cfg,
	.crypto_engine_cfg	= ufs_qcom_crytpo_engine_cfg,
	.crypto_engine_reset	= ufs_qcom_crytpo_engine_reset,
	.crypto_engine_eh	= ufs_qcom_crypto_engine_eh,
	.crypto_engine_get_err	= ufs_qcom_crypto_engine_get_err,
	.crypto_engine_reset_err = ufs_qcom_crypto_engine_reset_err,
	.dbg_register_dump	= ufs_qcom_dump_dbg_regs,
#ifdef CONFIG_DEBUG_FS
	.add_debugfs		= ufs_qcom_dbg_add_debugfs,
#endif
};
EXPORT_SYMBOL(ufs_hba_qcom_vops);
