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
 *
 */

#ifndef _UFS_QUIRKS_H_
#define _UFS_QUIRKS_H_

/* return true if s1 is a prefix of s2 */
#define STR_PRFX_EQUAL(s1, s2) !strncmp(s1, s2, strlen(s1))

#define UFS_ANY_VENDOR -1
#define UFS_ANY_MODEL  "ANY_MODEL"

#define MAX_MODEL_LEN 16

#define UFS_VENDOR_TOSHIBA     0x198
#define UFS_VENDOR_SAMSUNG     0x1CE

/* UFS TOSHIBA MODELS */
#define UFS_MODEL_TOSHIBA_32GB "THGLF2G8D4KBADR"
#define UFS_MODEL_TOSHIBA_64GB "THGLF2G9D8KBADG"

/**
 * ufs_card_info - ufs device details
 * @wmanufacturerid: card details
 * @model: card model
 */
struct ufs_card_info {
	u16 wmanufacturerid;
	char *model;
};

/**
 * ufs_card_fix - ufs device quirk info
 * @card: ufs card details
 * @quirk: device quirk
 */
struct ufs_card_fix {
	struct ufs_card_info card;
	unsigned int quirk;
};

#define END_FIX { { 0 } , 0 }

/* add specific device quirk */
#define UFS_FIX(_vendor, _model, _quirk) \
		{						  \
				.card.wmanufacturerid = (_vendor),\
				.card.model = (_model),		  \
				.quirk = (_quirk),		  \
		}

/*
 * If UFS device is having issue in processing LCC (Line Control
 * Command) coming from UFS host controller then enable this quirk.
 * When this quirk is enabled, host controller driver should disable
 * the LCC transmission on UFS host controller (by clearing
 * TX_LCC_ENABLE attribute of host to 0).
 */
#define UFS_DEVICE_QUIRK_BROKEN_LCC (1 << 0)

/*
 * Some UFS devices don't need VCCQ rail for device operations. Enabling this
 * quirk for such devices will make sure that VCCQ rail is not voted.
 */
#define UFS_DEVICE_NO_VCCQ (1 << 1)

/*
 * Some vendor's UFS device sends back to back NACs for the DL data frames
 * causing the host controller to raise the DFES error status. Sometimes
 * such UFS devices send back to back NAC without waiting for new
 * retransmitted DL frame from the host and in such cases it might be possible
 * the Host UniPro goes into bad state without raising the DFES error
 * interrupt. If this happens then all the pending commands would timeout
 * only after respective SW command (which is generally too large).
 *
 * We can workaround such device behaviour like this:
 * - As soon as SW sees the DL NAC error, it should schedule the error handler
 * - Error handler would sleep for 50ms to see if there are any fatal errors
 *   raised by UFS controller.
 *    - If there are fatal errors then SW does normal error recovery.
 *    - If there are no fatal errors then SW sends the NOP command to device
 *      to check if link is alive.
 *        - If NOP command times out, SW does normal error recovery
 *        - If NOP command succeed, skip the error handling.
 *
 * If DL NAC error is seen multiple times with some vendor's UFS devices then
 * enable this quirk to initiate quick error recovery and also silence related
 * error logs to reduce spamming of kernel logs.
 */
#define UFS_DEVICE_QUIRK_RECOVERY_FROM_DL_NAC_ERRORS (1 << 2)

/*
 * Some UFS devices may not work properly after resume if the link was kept
 * in off state during suspend. Enabling this quirk will not allow the
 * link to be kept in off state during suspend.
 */
#define UFS_DEVICE_QUIRK_NO_LINK_OFF	(1 << 3)

struct ufs_hba;
void ufs_advertise_fixup_device(struct ufs_hba *hba);
#endif /* UFS_QUIRKS_H_ */
