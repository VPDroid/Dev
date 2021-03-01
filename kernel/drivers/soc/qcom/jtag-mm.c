/* Copyright (c) 2013-2014, The Linux Foundation. All rights reserved.
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
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/export.h>
#include <linux/printk.h>
#include <linux/ratelimit.h>
#include <linux/coresight.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/bitops.h>
#include <linux/of.h>
#include <linux/cpu.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <soc/qcom/scm.h>
#include <soc/qcom/jtag.h>
#include <asm/smp_plat.h>

/* Coresight management registers */
#define CORESIGHT_ITCTRL	(0xF00)
#define CORESIGHT_CLAIMSET	(0xFA0)
#define CORESIGHT_CLAIMCLR	(0xFA4)
#define CORESIGHT_LAR		(0xFB0)
#define CORESIGHT_LSR		(0xFB4)
#define CORESIGHT_AUTHSTATUS	(0xFB8)
#define CORESIGHT_DEVID		(0xFC8)
#define CORESIGHT_DEVTYPE	(0xFCC)

#define CORESIGHT_UNLOCK	(0xC5ACCE55)

#define TIMEOUT_US		(100)

#define BM(lsb, msb)		((BIT(msb) - BIT(lsb)) + BIT(msb))
#define BMVAL(val, lsb, msb)	((val & BM(lsb, msb)) >> lsb)
#define BVAL(val, n)		((val & BIT(n)) >> n)

/* Trace registers */
#define ETMCR			(0x000)
#define ETMCCR			(0x004)
#define ETMTRIGGER		(0x008)
#define ETMASICCTLR		(0x00C)
#define ETMSR			(0x010)
#define ETMSCR			(0x014)
#define ETMTSSCR		(0x018)
#define ETMTECR2		(0x01C)
#define ETMTEEVR		(0x020)
#define ETMTECR1		(0x024)
#define ETMFFLR			(0x02C)
#define ETMVDEVR		(0x030)
#define ETMVDCR1		(0x034)
#define ETMVDCR3		(0x03C)
#define ETMACVRn(n)		(0x040 + (n * 4))
#define ETMACTRn(n)		(0x080 + (n * 4))
#define ETMDCVRn(n)		(0x0C0 + (n * 8))
#define ETMDCMRn(n)		(0x100 + (n * 8))
#define ETMCNTRLDVRn(n)		(0x140 + (n * 4))
#define ETMCNTENRn(n)		(0x150 + (n * 4))
#define ETMCNTRLDEVRn(n)	(0x160 + (n * 4))
#define ETMCNTVRn(n)		(0x170 + (n * 4))
#define ETMSQ12EVR		(0x180)
#define ETMSQ21EVR		(0x184)
#define ETMSQ23EVR		(0x188)
#define ETMSQ31EVR		(0x18C)
#define ETMSQ32EVR		(0x190)
#define ETMSQ13EVR		(0x194)
#define ETMSQR			(0x19C)
#define ETMEXTOUTEVRn(n)	(0x1A0 + (n * 4))
#define ETMCIDCVRn(n)		(0x1B0 + (n * 4))
#define ETMCIDCMR		(0x1BC)
#define ETMIMPSPEC0		(0x1C0)
#define ETMIMPSPEC1		(0x1C4)
#define ETMIMPSPEC2		(0x1C8)
#define ETMIMPSPEC3		(0x1CC)
#define ETMIMPSPEC4		(0x1D0)
#define ETMIMPSPEC5		(0x1D4)
#define ETMIMPSPEC6		(0x1D8)
#define ETMIMPSPEC7		(0x1DC)
#define ETMSYNCFR		(0x1E0)
#define ETMIDR			(0x1E4)
#define ETMCCER			(0x1E8)
#define ETMEXTINSELR		(0x1EC)
#define ETMTESSEICR		(0x1F0)
#define ETMEIBCR		(0x1F4)
#define ETMTSEVR		(0x1F8)
#define ETMAUXCR		(0x1FC)
#define ETMTRACEIDR		(0x200)
#define ETMIDR2			(0x208)
#define ETMVMIDCVR		(0x240)
#define ETMCLAIMSET		(0xFA0)
#define ETMCLAIMCLR		(0xFA4)
/* ETM Management registers */
#define ETMOSLAR		(0x300)
#define ETMOSLSR		(0x304)
#define ETMOSSRR		(0x308)
#define ETMPDCR			(0x310)
#define ETMPDSR			(0x314)

#define ETM_MAX_ADDR_CMP	(16)
#define ETM_MAX_CNTR		(4)
#define ETM_MAX_CTXID_CMP	(3)

/* DBG Registers */
#define DBGDIDR			(0x0)
#define DBGWFAR			(0x18)
#define DBGVCR			(0x1C)
#define DBGDTRRXext		(0x80)
#define DBGDSCRext		(0x88)
#define DBGDTRTXext		(0x8C)
#define DBGDRCR			(0x90)
#define DBGBVRn(n)		(0x100 + (n * 4))
#define DBGBCRn(n)		(0x140 + (n * 4))
#define DBGWVRn(n)		(0x180 + (n * 4))
#define DBGWCRn(n)		(0x1C0 + (n * 4))
#define DBGOSLAR		(0x300)
#define DBGOSLSR		(0x304)
#define DBGPRCR			(0x310)
#define DBGITMISCOUT		(0xEF8)
#define DBGITMISCIN		(0xEFC)
#define DBGCLAIMSET		(0xFA0)
#define DBGCLAIMCLR		(0xFA4)

#define DBGDSCR_MASK		(0x6C30FC3C)
#define OSLOCK_MAGIC		(0xC5ACCE55)

#define MAX_DBG_STATE_SIZE	(90)
#define MAX_ETM_STATE_SIZE	(78)

#define TZ_DBG_ETM_FEAT_ID	(0x8)
#define TZ_DBG_ETM_VER		(0x400000)

#define ARCH_V3_5		(0x25)
#define ARM_DEBUG_ARCH_V7B	(0x3)
#define ARM_DEBUG_ARCH_V7p1	(0x5)

#define etm_write(etm, val, off)	\
			__raw_writel(val, etm->base + off)
#define etm_read(etm, off)	\
			__raw_readl(etm->base + off)

#define dbg_write(dbg, val, off)	\
			__raw_writel(val, dbg->base + off)
#define dbg_read(dbg, off)	\
			__raw_readl(dbg->base + off)

#define ETM_LOCK(base)						\
do {									\
	/* recommended by spec to ensure ETM writes are committed prior
	 * to resuming execution
	 */								\
	mb();								\
	etm_write(base, 0x0, CORESIGHT_LAR);			\
} while (0)

#define ETM_UNLOCK(base)						\
do {									\
	etm_write(base, CORESIGHT_UNLOCK, CORESIGHT_LAR);	\
	/* ensure unlock and any pending writes are committed prior to
	 * programming ETM registers
	 */								\
	mb();								\
} while (0)

#define DBG_LOCK(base)						\
do {									\
	/* recommended by spec to ensure ETM writes are committed prior
	 * to resuming execution
	 */								\
	mb();								\
	dbg_write(base, 0x0, CORESIGHT_LAR);			\
} while (0)

#define DBG_UNLOCK(base)						\
do {									\
	dbg_write(base, CORESIGHT_UNLOCK, CORESIGHT_LAR);	\
	/* ensure unlock and any pending writes are committed prior to
	 * programming ETM registers
	 */								\
	mb();								\
} while (0)

uint32_t msm_jtag_save_cntr[NR_CPUS];
uint32_t msm_jtag_restore_cntr[NR_CPUS];

struct dbg_ctx {
	uint8_t			arch;
	uint8_t			nr_wp;
	uint8_t			nr_bp;
	uint8_t			nr_ctx_cmp;
	bool			save_restore_enabled;
	bool			init;
	bool			enable;
	void __iomem		*base;
	uint32_t		*state;
	spinlock_t		spinlock;
	struct mutex		mutex;
};
static struct dbg_ctx *dbg[NR_CPUS];

struct etm_ctx {
	uint8_t			arch;
	uint8_t			nr_addr_cmp;
	uint8_t			nr_data_cmp;
	uint8_t			nr_cntr;
	uint8_t			nr_ext_inp;
	uint8_t			nr_ext_out;
	uint8_t			nr_ctxid_cmp;
	bool			save_restore_enabled;
	bool			os_lock_present;
	bool			init;
	bool			enable;
	void __iomem		*base;
	struct device		*dev;
	uint32_t		*state;
	spinlock_t		spinlock;
	struct mutex		mutex;
};

static struct etm_ctx *etm[NR_CPUS];
static int cnt;

static struct clk *clock[NR_CPUS];

static void etm_os_lock(struct etm_ctx *etmdata)
{
	uint32_t count;

	if (etmdata->os_lock_present) {
		etm_write(etmdata, OSLOCK_MAGIC, ETMOSLAR);
		/* Ensure OS lock is set before proceeding */
		mb();
		for (count = TIMEOUT_US; BVAL(etm_read(etmdata, ETMSR), 1) != 1
							&& count > 0; count--)
			udelay(1);
		if (count == 0)
			pr_err_ratelimited(
				"timeout while setting prog bit, ETMSR: %#x\n",
						etm_read(etmdata, ETMSR));
	}
}

static void etm_os_unlock(struct etm_ctx *etmdata)
{
	if (etmdata->os_lock_present) {
		/* Ensure all writes are complete before clearing OS lock */
		mb();
		etm_write(etmdata, 0x0, ETMOSLAR);
	}
}

static void etm_set_pwrdwn(struct etm_ctx *etmdata)
{
	uint32_t etmcr;

	/* ensure all writes are complete before setting pwrdwn */
	mb();
	etmcr = etm_read(etmdata, ETMCR);
	etmcr |= BIT(0);
	etm_write(etmdata, etmcr, ETMCR);
}

static void etm_clr_pwrdwn(struct etm_ctx *etmdata)
{
	uint32_t etmcr;

	etmcr = etm_read(etmdata, ETMCR);
	etmcr &= ~BIT(0);
	etm_write(etmdata, etmcr, ETMCR);
	/* ensure pwrup completes before subsequent register accesses */
	mb();
}

static void etm_set_prog(struct etm_ctx *etmdata)
{
	uint32_t etmcr;
	int count;

	etmcr = etm_read(etmdata, ETMCR);
	etmcr |= BIT(10);
	etm_write(etmdata, etmcr, ETMCR);
	for (count = TIMEOUT_US; BVAL(etm_read(etmdata, ETMSR), 1) != 1
				&& count > 0; count--)
		udelay(1);
	WARN(count == 0, "timeout while setting prog bit, ETMSR: %#x\n",
	     etm_read(etmdata, ETMSR));
}

static inline void etm_save_state(struct etm_ctx *etmdata)
{
	int i, j;

	i = 0;
	ETM_UNLOCK(etmdata);

	switch (etmdata->arch) {
	case ETM_ARCH_V3_5:
		etm_os_lock(etmdata);

		etmdata->state[i++] = etm_read(etmdata, ETMTRIGGER);
		etmdata->state[i++] = etm_read(etmdata, ETMASICCTLR);
		etmdata->state[i++] = etm_read(etmdata, ETMSR);
		etmdata->state[i++] = etm_read(etmdata, ETMTSSCR);
		etmdata->state[i++] = etm_read(etmdata, ETMTECR2);
		etmdata->state[i++] = etm_read(etmdata, ETMTEEVR);
		etmdata->state[i++] = etm_read(etmdata, ETMTECR1);
		etmdata->state[i++] = etm_read(etmdata, ETMFFLR);
		etmdata->state[i++] = etm_read(etmdata, ETMVDEVR);
		etmdata->state[i++] = etm_read(etmdata, ETMVDCR1);
		etmdata->state[i++] = etm_read(etmdata, ETMVDCR3);
		for (j = 0; j < etmdata->nr_addr_cmp; j++) {
			etmdata->state[i++] = etm_read(etmdata,
								ETMACVRn(j));
			etmdata->state[i++] = etm_read(etmdata,
								ETMACTRn(j));
		}
		for (j = 0; j < etmdata->nr_data_cmp; j++) {
			etmdata->state[i++] = etm_read(etmdata,
								ETMDCVRn(j));
			etmdata->state[i++] = etm_read(etmdata,
								ETMDCMRn(j));
		}
		for (j = 0; j < etmdata->nr_cntr; j++) {
			etmdata->state[i++] = etm_read(etmdata,
							ETMCNTRLDVRn(j));
			etmdata->state[i++] = etm_read(etmdata,
							ETMCNTENRn(j));
			etmdata->state[i++] = etm_read(etmdata,
							ETMCNTRLDEVRn(j));
			etmdata->state[i++] = etm_read(etmdata,
							ETMCNTVRn(j));
		}
		etmdata->state[i++] = etm_read(etmdata, ETMSQ12EVR);
		etmdata->state[i++] = etm_read(etmdata, ETMSQ21EVR);
		etmdata->state[i++] = etm_read(etmdata, ETMSQ23EVR);
		etmdata->state[i++] = etm_read(etmdata, ETMSQ31EVR);
		etmdata->state[i++] = etm_read(etmdata, ETMSQ32EVR);
		etmdata->state[i++] = etm_read(etmdata, ETMSQ13EVR);
		etmdata->state[i++] = etm_read(etmdata, ETMSQR);
		for (j = 0; j < etmdata->nr_ext_out; j++)
			etmdata->state[i++] = etm_read(etmdata,
							ETMEXTOUTEVRn(j));
		for (j = 0; j < etmdata->nr_ctxid_cmp; j++)
			etmdata->state[i++] = etm_read(etmdata,
							ETMCIDCVRn(j));
		etmdata->state[i++] = etm_read(etmdata, ETMCIDCMR);
		etmdata->state[i++] = etm_read(etmdata, ETMSYNCFR);
		etmdata->state[i++] = etm_read(etmdata, ETMEXTINSELR);
		etmdata->state[i++] = etm_read(etmdata, ETMTSEVR);
		etmdata->state[i++] = etm_read(etmdata, ETMAUXCR);
		etmdata->state[i++] = etm_read(etmdata, ETMTRACEIDR);
		etmdata->state[i++] = etm_read(etmdata, ETMVMIDCVR);
		etmdata->state[i++] = etm_read(etmdata, ETMCLAIMCLR);
		etmdata->state[i++] = etm_read(etmdata, ETMCR);
		break;
	default:
		pr_err_ratelimited("unsupported etm arch %d in %s\n",
				   etmdata->arch, __func__);
	}

	ETM_LOCK(etmdata);
}

static inline void etm_restore_state(struct etm_ctx *etmdata)
{
	int i, j;

	i = 0;
	ETM_UNLOCK(etmdata);

	switch (etmdata->arch) {
	case ETM_ARCH_V3_5:
		etm_os_lock(etmdata);

		etm_clr_pwrdwn(etmdata);
		etm_write(etmdata, etmdata->state[i++], ETMTRIGGER);
		etm_write(etmdata, etmdata->state[i++], ETMASICCTLR);
		etm_write(etmdata, etmdata->state[i++], ETMSR);
		etm_write(etmdata, etmdata->state[i++], ETMTSSCR);
		etm_write(etmdata, etmdata->state[i++], ETMTECR2);
		etm_write(etmdata, etmdata->state[i++], ETMTEEVR);
		etm_write(etmdata, etmdata->state[i++], ETMTECR1);
		etm_write(etmdata, etmdata->state[i++], ETMFFLR);
		etm_write(etmdata, etmdata->state[i++], ETMVDEVR);
		etm_write(etmdata, etmdata->state[i++], ETMVDCR1);
		etm_write(etmdata, etmdata->state[i++], ETMVDCR3);
		for (j = 0; j < etmdata->nr_addr_cmp; j++) {
			etm_write(etmdata, etmdata->state[i++],
								ETMACVRn(j));
			etm_write(etmdata, etmdata->state[i++],
								ETMACTRn(j));
		}
		for (j = 0; j < etmdata->nr_data_cmp; j++) {
			etm_write(etmdata, etmdata->state[i++],
								ETMDCVRn(j));
			etm_write(etmdata, etmdata->state[i++],
								ETMDCMRn(j));
		}
		for (j = 0; j < etmdata->nr_cntr; j++) {
			etm_write(etmdata, etmdata->state[i++],
							ETMCNTRLDVRn(j));
			etm_write(etmdata, etmdata->state[i++],
							ETMCNTENRn(j));
			etm_write(etmdata, etmdata->state[i++],
							ETMCNTRLDEVRn(j));
			etm_write(etmdata, etmdata->state[i++],
							ETMCNTVRn(j));
		}
		etm_write(etmdata, etmdata->state[i++], ETMSQ12EVR);
		etm_write(etmdata, etmdata->state[i++], ETMSQ21EVR);
		etm_write(etmdata, etmdata->state[i++], ETMSQ23EVR);
		etm_write(etmdata, etmdata->state[i++], ETMSQ31EVR);
		etm_write(etmdata, etmdata->state[i++], ETMSQ32EVR);
		etm_write(etmdata, etmdata->state[i++], ETMSQ13EVR);
		etm_write(etmdata, etmdata->state[i++], ETMSQR);
		for (j = 0; j < etmdata->nr_ext_out; j++)
			etm_write(etmdata, etmdata->state[i++],
							ETMEXTOUTEVRn(j));
		for (j = 0; j < etmdata->nr_ctxid_cmp; j++)
			etm_write(etmdata, etmdata->state[i++],
							ETMCIDCVRn(j));
		etm_write(etmdata, etmdata->state[i++], ETMCIDCMR);
		etm_write(etmdata, etmdata->state[i++], ETMSYNCFR);
		etm_write(etmdata, etmdata->state[i++], ETMEXTINSELR);
		etm_write(etmdata, etmdata->state[i++], ETMTSEVR);
		etm_write(etmdata, etmdata->state[i++], ETMAUXCR);
		etm_write(etmdata, etmdata->state[i++], ETMTRACEIDR);
		etm_write(etmdata, etmdata->state[i++], ETMVMIDCVR);
		etm_write(etmdata, etmdata->state[i++], ETMCLAIMSET);
		/*
		 * Set ETMCR at last as we dont know the saved status of pwrdwn
		 * bit
		 */
		etm_write(etmdata, etmdata->state[i++], ETMCR);

		etm_os_unlock(etmdata);
		break;
	default:
		pr_err_ratelimited("unsupported etm arch %d in %s\n",
				   etmdata->arch, __func__);
	}

	ETM_LOCK(etmdata);
}

static inline void dbg_save_state(struct dbg_ctx *dbgdata)
{
	int i, j;

	i = 0;
	DBG_UNLOCK(dbgdata);

	switch (dbgdata->arch) {
	case ARM_DEBUG_ARCH_V7B:
		dbgdata->state[i++] =  dbg_read(dbgdata, DBGWFAR);
		dbgdata->state[i++] =  dbg_read(dbgdata, DBGVCR);
		for (j = 0; j < dbgdata->nr_bp; j++) {
			dbgdata->state[i++] =  dbg_read(dbgdata, DBGBVRn(j));
			dbgdata->state[i++] =  dbg_read(dbgdata, DBGBCRn(j));
		}
		for (j = 0; j < dbgdata->nr_wp; j++) {
			dbgdata->state[i++] =  dbg_read(dbgdata, DBGWVRn(j));
			dbgdata->state[i++] =  dbg_read(dbgdata, DBGWCRn(j));
		}
		dbgdata->state[i++] =  dbg_read(dbgdata, DBGPRCR);
		dbgdata->state[i++] =  dbg_read(dbgdata, DBGDTRTXext);
		dbgdata->state[i++] =  dbg_read(dbgdata, DBGDTRRXext);
		dbgdata->state[i++] =  dbg_read(dbgdata, DBGDSCRext);
		break;
	case ARM_DEBUG_ARCH_V7p1:
		/* Set OS Lock */
		dbg_write(dbgdata, OSLOCK_MAGIC, DBGOSLAR);
		/* Ensure OS lock is set before proceeding */
		mb();

		dbgdata->state[i++] =  dbg_read(dbgdata, DBGWFAR);
		dbgdata->state[i++] =  dbg_read(dbgdata, DBGVCR);
		for (j = 0; j < dbgdata->nr_bp; j++) {
			dbgdata->state[i++] =  dbg_read(dbgdata, DBGBVRn(j));
			dbgdata->state[i++] =  dbg_read(dbgdata, DBGBCRn(j));
		}
		for (j = 0; j < dbgdata->nr_wp; j++) {
			dbgdata->state[i++] =  dbg_read(dbgdata, DBGWVRn(j));
			dbgdata->state[i++] =  dbg_read(dbgdata, DBGWCRn(j));
		}
		dbgdata->state[i++] =  dbg_read(dbgdata, DBGPRCR);
		dbgdata->state[i++] =  dbg_read(dbgdata, DBGCLAIMCLR);
		dbgdata->state[i++] =  dbg_read(dbgdata, DBGDTRTXext);
		dbgdata->state[i++] =  dbg_read(dbgdata, DBGDTRRXext);
		dbgdata->state[i++] =  dbg_read(dbgdata, DBGDSCRext);
		break;
	}

	DBG_LOCK(dbgdata);
}

static inline void dbg_restore_state(struct dbg_ctx *dbgdata)
{
	int i, j;

	i = 0;
	DBG_UNLOCK(dbgdata);

	switch (dbgdata->arch) {
	case ARM_DEBUG_ARCH_V7B:
		dbg_write(dbgdata, dbgdata->state[i++], DBGWFAR);
		dbg_write(dbgdata, dbgdata->state[i++], DBGVCR);
		for (j = 0; j < dbgdata->nr_bp; j++) {
			dbg_write(dbgdata, dbgdata->state[i++], DBGBVRn(j));
			dbg_write(dbgdata, dbgdata->state[i++], DBGBCRn(j));
		}
		for (j = 0; j < dbgdata->nr_wp; j++) {
			dbg_write(dbgdata, dbgdata->state[i++], DBGWVRn(j));
			dbg_write(dbgdata, dbgdata->state[i++], DBGWCRn(j));
		}
		dbg_write(dbgdata, dbgdata->state[i++], DBGPRCR);
		dbg_write(dbgdata, dbgdata->state[i++], DBGDTRTXext);
		dbg_write(dbgdata, dbgdata->state[i++], DBGDTRRXext);
		dbg_write(dbgdata, dbgdata->state[i++] & DBGDSCR_MASK,
								DBGDSCRext);
		break;
	case ARM_DEBUG_ARCH_V7p1:
		/* Set OS lock. Lock will already be set after power collapse
		 * but this write is included to ensure it is set.
		 */
		dbg_write(dbgdata, OSLOCK_MAGIC, DBGOSLAR);
		/* Ensure OS lock is set before proceeding */
		mb();

		dbg_write(dbgdata, dbgdata->state[i++], DBGWFAR);
		dbg_write(dbgdata, dbgdata->state[i++], DBGVCR);
		for (j = 0; j < dbgdata->nr_bp; j++) {
			dbg_write(dbgdata, dbgdata->state[i++], DBGBVRn(j));
			dbg_write(dbgdata, dbgdata->state[i++], DBGBCRn(j));
		}
		for (j = 0; j < dbgdata->nr_wp; j++) {
			dbg_write(dbgdata, dbgdata->state[i++], DBGWVRn(j));
			dbg_write(dbgdata, dbgdata->state[i++], DBGWCRn(j));
		}
		dbg_write(dbgdata, dbgdata->state[i++], DBGPRCR);
		dbg_write(dbgdata, dbgdata->state[i++], DBGCLAIMSET);
		dbg_write(dbgdata, dbgdata->state[i++], DBGDTRTXext);
		dbg_write(dbgdata, dbgdata->state[i++], DBGDTRRXext);
		dbg_write(dbgdata, dbgdata->state[i++] & DBGDSCR_MASK,
								DBGDSCRext);
		/* Ensure all writes are completing before clearing OS lock */
		mb();
		dbg_write(dbgdata, 0x0, DBGOSLAR);
		/* Ensure OS lock is cleared before proceeding */
		mb();
		break;
	}

	DBG_LOCK(dbgdata);
}

void msm_jtag_save_state(void)
{
	int cpu;

	cpu = raw_smp_processor_id();

	msm_jtag_save_cntr[cpu]++;
	/* ensure counter is updated before moving forward */
	mb();

	if (dbg[cpu] && dbg[cpu]->save_restore_enabled)
		dbg_save_state(dbg[cpu]);
	if (etm[cpu] && etm[cpu]->save_restore_enabled)
		etm_save_state(etm[cpu]);
}
EXPORT_SYMBOL(msm_jtag_save_state);

void msm_jtag_restore_state(void)
{
	int cpu;

	cpu = raw_smp_processor_id();

	/* Attempt restore only if save has been done. If power collapse
	 * is disabled, hotplug off of non-boot core will result in WFI
	 * and hence msm_jtag_save_state will not occur. Subsequently,
	 * during hotplug on of non-boot core when msm_jtag_restore_state
	 * is called via msm_platform_secondary_init, this check will help
	 * bail us out without restoring.
	 */
	if (msm_jtag_save_cntr[cpu] == msm_jtag_restore_cntr[cpu])
		return;
	else if (msm_jtag_save_cntr[cpu] != msm_jtag_restore_cntr[cpu] + 1)
		pr_err_ratelimited("jtag imbalance, save:%lu, restore:%lu\n",
				   (unsigned long)msm_jtag_save_cntr[cpu],
				   (unsigned long)msm_jtag_restore_cntr[cpu]);

	msm_jtag_restore_cntr[cpu]++;
	/* ensure counter is updated before moving forward */
	mb();

	if (dbg[cpu] && dbg[cpu]->save_restore_enabled)
		dbg_restore_state(dbg[cpu]);
	if (etm[cpu] && etm[cpu]->save_restore_enabled)
		etm_restore_state(etm[cpu]);
}
EXPORT_SYMBOL(msm_jtag_restore_state);

static inline bool etm_arch_supported(uint8_t arch)
{
	switch (arch) {
	case ETM_ARCH_V3_5:
		break;
	default:
		return false;
	}
	return true;
}

static void etm_os_lock_init(struct etm_ctx *etmdata)
{
	uint32_t etmoslsr;

	etmoslsr = etm_read(etmdata, ETMOSLSR);
	if (!BVAL(etmoslsr, 0) && !BVAL(etmoslsr, 3))
		etmdata->os_lock_present = false;
	else
		etmdata->os_lock_present = true;
}

static void etm_init_arch_data(void *info)
{
	uint32_t etmidr;
	uint32_t etmccr;
	struct etm_ctx  *etmdata = info;

	/*
	 * Clear power down bit since when this bit is set writes to
	 * certain registers might be ignored.
	 */
	ETM_UNLOCK(etmdata);

	etm_os_lock_init(etmdata);

	etm_clr_pwrdwn(etmdata);
	/* Set prog bit. It will be set from reset but this is included to
	 * ensure it is set
	 */
	etm_set_prog(etmdata);

	/* find all capabilities */
	etmidr = etm_read(etmdata, ETMIDR);
	etmdata->arch = BMVAL(etmidr, 4, 11);

	etmccr = etm_read(etmdata, ETMCCR);
	etmdata->nr_addr_cmp = BMVAL(etmccr, 0, 3) * 2;
	etmdata->nr_data_cmp = BMVAL(etmccr, 4, 7);
	etmdata->nr_cntr = BMVAL(etmccr, 13, 15);
	etmdata->nr_ext_inp = BMVAL(etmccr, 17, 19);
	etmdata->nr_ext_out = BMVAL(etmccr, 20, 22);
	etmdata->nr_ctxid_cmp = BMVAL(etmccr, 24, 25);

	etm_set_pwrdwn(etmdata);

	ETM_LOCK(etmdata);
}

static int jtag_mm_etm_callback(struct notifier_block *nfb,
				unsigned long action,
				void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;

	if (!etm[cpu])
		goto out;

	switch (action & (~CPU_TASKS_FROZEN)) {
	case CPU_STARTING:
		spin_lock(&etm[cpu]->spinlock);
		if (!etm[cpu]->init) {
			etm_init_arch_data(etm[cpu]);
			etm[cpu]->init = true;
		}
		spin_unlock(&etm[cpu]->spinlock);
		break;

	case CPU_ONLINE:
		mutex_lock(&etm[cpu]->mutex);
		if (etm[cpu]->enable) {
			mutex_unlock(&etm[cpu]->mutex);
			goto out;
		}
		if (etm_arch_supported(etm[cpu]->arch)) {
			if (scm_get_feat_version(TZ_DBG_ETM_FEAT_ID) <
						 TZ_DBG_ETM_VER)
				etm[cpu]->save_restore_enabled = true;
			else
				pr_info("etm save-restore supported by TZ\n");
		} else
			pr_info("etm arch %u not supported\n", etm[cpu]->arch);
		etm[cpu]->enable = true;
		mutex_unlock(&etm[cpu]->mutex);
		break;

	default:
		break;
	}
out:
	return NOTIFY_OK;
}

static struct notifier_block jtag_mm_etm_notifier = {
	.notifier_call = jtag_mm_etm_callback,
};

static int jtag_mm_etm_probe(struct platform_device *pdev, uint32_t cpu)
{
	struct etm_ctx *etmdata;
	struct resource *res;
	struct device *dev = &pdev->dev;

	/* Allocate memory per cpu */
	etmdata = devm_kzalloc(dev, sizeof(struct etm_ctx), GFP_KERNEL);
	if (!etmdata)
		return -ENOMEM;

	etm[cpu] = etmdata;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "etm-base");
	if (!res)
		return -ENODEV;

	etmdata->base = devm_ioremap(dev, res->start, resource_size(res));
	if (!etmdata->base)
		return -EINVAL;

	/* Allocate etm state save space per core */
	etmdata->state = devm_kzalloc(dev,
			(MAX_ETM_STATE_SIZE * sizeof(uint32_t)), GFP_KERNEL);
	if (!etmdata->state)
		return -ENOMEM;

	spin_lock_init(&etmdata->spinlock);
	mutex_init(&etmdata->mutex);

	if (cnt == 0)
		register_hotcpu_notifier(&jtag_mm_etm_notifier);

	if (!smp_call_function_single(cpu, etm_init_arch_data, etmdata, 1))
		etmdata->init = true;

	if (etmdata->init) {
		mutex_lock(&etmdata->mutex);
		if (etm_arch_supported(etmdata->arch)) {
			if (scm_get_feat_version(TZ_DBG_ETM_FEAT_ID) <
						 TZ_DBG_ETM_VER)
				etmdata->save_restore_enabled = true;
			else
				pr_info("etm save-restore supported by TZ\n");
		} else
			pr_info("etm arch %u not supported\n", etmdata->arch);
		etmdata->enable = true;
		mutex_unlock(&etmdata->mutex);
	}
	return 0;
}

static inline bool dbg_arch_supported(uint8_t arch)
{
	switch (arch) {
	case ARM_DEBUG_ARCH_V7B:
	case ARM_DEBUG_ARCH_V7p1:
		break;
	default:
		return false;
	}
	return true;
}

static void dbg_init_arch_data(void *info)
{
	uint32_t dbgdidr;
	struct dbg_ctx *dbgdata = info;

	/* This will run on core0 so use it to populate parameters */
	dbgdidr = dbg_read(dbgdata, DBGDIDR);
	dbgdata->arch = BMVAL(dbgdidr, 16, 19);
	dbgdata->nr_ctx_cmp = BMVAL(dbgdidr, 20, 23) + 1;
	dbgdata->nr_bp = BMVAL(dbgdidr, 24, 27) + 1;
	dbgdata->nr_wp = BMVAL(dbgdidr, 28, 31) + 1;
}

static int jtag_mm_dbg_callback(struct notifier_block *nfb,
				unsigned long action,
				void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;
	if (!dbg[cpu])
		goto out;

	switch (action & (~CPU_TASKS_FROZEN)) {
	case CPU_STARTING:
		spin_lock(&dbg[cpu]->spinlock);
		if (!dbg[cpu]->init) {
			dbg_init_arch_data(dbg[cpu]);
			dbg[cpu]->init = true;
		}
		spin_unlock(&dbg[cpu]->spinlock);
		break;

	case CPU_ONLINE:
		mutex_lock(&dbg[cpu]->mutex);
		if (dbg[cpu]->enable) {
			mutex_unlock(&dbg[cpu]->mutex);
			goto out;
		}
		if (dbg_arch_supported(dbg[cpu]->arch)) {
			if (scm_get_feat_version(TZ_DBG_ETM_FEAT_ID) <
						 TZ_DBG_ETM_VER)
				dbg[cpu]->save_restore_enabled = true;
			else
				pr_info("dbg save-restore supported by TZ\n");
		} else
			pr_info("dbg arch %u not supported\n", dbg[cpu]->arch);
		mutex_unlock(&dbg[cpu]->mutex);
		break;

	default:
		break;
	}
out:
	return NOTIFY_OK;
}

static struct notifier_block jtag_mm_dbg_notifier = {
	.notifier_call = jtag_mm_dbg_callback,
};

static int jtag_mm_dbg_probe(struct platform_device *pdev,
								uint32_t cpu)
{
	struct dbg_ctx *dbgdata;
	struct resource *res;
	struct device *dev = &pdev->dev;

	/* Allocate memory per cpu */
	dbgdata = devm_kzalloc(dev, sizeof(struct dbg_ctx), GFP_KERNEL);
	if (!dbgdata)
		return -ENOMEM;

	dbg[cpu] = dbgdata;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "debug-base");
	if (!res)
		return -ENODEV;

	dbgdata->base = devm_ioremap(dev, res->start, resource_size(res));
	if (!dbgdata->base)
		return -EINVAL;

	/* Allocate etm state save space per core */
	dbgdata->state = devm_kzalloc(dev,
			(MAX_DBG_STATE_SIZE * sizeof(uint32_t)), GFP_KERNEL);
	if (!dbgdata->state)
		return -ENOMEM;

	spin_lock_init(&dbgdata->spinlock);
	mutex_init(&dbgdata->mutex);

	if (cnt == 0)
		register_hotcpu_notifier(&jtag_mm_dbg_notifier);

	if (!smp_call_function_single(cpu, dbg_init_arch_data,
								dbgdata, 1))
		dbgdata->init = true;

	if (!dbgdata->init) {
		mutex_lock(&dbgdata->mutex);
		if (dbg_arch_supported(dbgdata->arch)) {
			if (scm_get_feat_version(TZ_DBG_ETM_FEAT_ID) <
						 TZ_DBG_ETM_VER)
				dbgdata->save_restore_enabled = true;
			else
				pr_info("dbg save-restore supported by TZ\n");
		} else
			pr_info("dbg arch %u not supported\n", dbgdata->arch);
		mutex_unlock(&dbgdata->mutex);
	}

	return 0;
}

static int jtag_mm_probe(struct platform_device *pdev)
{
	int etm_ret, dbg_ret, ret, i, cpu = -1;
	struct device *dev = &pdev->dev;
	struct device_node *cpu_node;

	if (msm_jtag_fuse_apps_access_disabled())
		return -EPERM;

	cpu_node = of_parse_phandle(pdev->dev.of_node,
				    "qcom,coresight-jtagmm-cpu", 0);
	if (!cpu_node) {
		dev_err(dev, "Jtag-mm cpu handle not specified\n");
		return -ENODEV;
	}
	for_each_possible_cpu(i) {
		if (cpu_node == of_get_cpu_node(i, NULL)) {
			cpu = i;
			break;
		}
	}
	if (cpu == -1) {
		dev_err(dev, "invalid Jtag-mm cpu handle\n");
		return -EINVAL;
	}

	clock[cpu] = devm_clk_get(dev, "core_clk");
	if (IS_ERR(clock[cpu])) {
		ret = PTR_ERR(clock[cpu]);
		return ret;
	}

	ret = clk_set_rate(clock[cpu], CORESIGHT_CLK_RATE_TRACE);
	if (ret)
		return ret;

	ret = clk_prepare_enable(clock[cpu]);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, clock[cpu]);

	etm_ret  = jtag_mm_etm_probe(pdev, cpu);

	dbg_ret = jtag_mm_dbg_probe(pdev, cpu);

	/* The probe succeeds even when only one of the etm and dbg probes
	 * succeeds. This allows us to save-restore etm and dbg registers
	 * independently.
	 */
	if (etm_ret && dbg_ret) {
		clk_disable_unprepare(clock[cpu]);
		ret = etm_ret;
	} else {
		cnt++;
		ret = 0;
	}
	return ret;
}

static void jtag_mm_etm_remove(void)
{
		unregister_hotcpu_notifier(&jtag_mm_etm_notifier);
}

static void jtag_mm_dbg_remove(void)
{
		unregister_hotcpu_notifier(&jtag_mm_dbg_notifier);
}

static int jtag_mm_remove(struct platform_device *pdev)
{
	struct clk *clock = platform_get_drvdata(pdev);

	if (--cnt == 0) {
		jtag_mm_dbg_remove();
		jtag_mm_etm_remove();
	}
	clk_disable_unprepare(clock);
	return 0;
}

static struct of_device_id msm_qdss_mm_match[] = {
	{ .compatible = "qcom,jtag-mm"},
	{}
};

static struct platform_driver jtag_mm_driver = {
	.probe          = jtag_mm_probe,
	.remove         = jtag_mm_remove,
	.driver         = {
		.name   = "msm-jtag-mm",
		.owner	= THIS_MODULE,
		.of_match_table	= msm_qdss_mm_match,
		},
};

static int __init jtag_mm_init(void)
{
	return platform_driver_register(&jtag_mm_driver);
}
module_init(jtag_mm_init);

static void __exit jtag_mm_exit(void)
{
	platform_driver_unregister(&jtag_mm_driver);
}
module_exit(jtag_mm_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Coresight debug and ETM save-restore driver");
