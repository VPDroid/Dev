/*
 * Atmel maXTouch Touchscreen driver
 *
 * Copyright (c) 2014, The Linux Foundation.  All rights reserved.
 *
 * Linux foundation chooses to take subject only to the GPLv2 license terms,
 * and distributes only under these terms.
 *
 * Copyright (C) 2010 Samsung Electronics Co.Ltd
 * Copyright (C) 2011-2012 Atmel Corporation
 * Copyright (C) 2012 Google, Inc.
 *
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/input/atmel_maxtouch_ts.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>

#if defined(CONFIG_FB)
#include <linux/notifier.h>
#include <linux/fb.h>

#elif defined(CONFIG_HAS_EARLYSUSPEND)
#include <linux/earlysuspend.h>
/* Early-suspend level */
#define MXT_SUSPEND_LEVEL 1
#endif

#if defined(CONFIG_SECURE_TOUCH)
#include <linux/completion.h>
#include <linux/pm_runtime.h>
#include <linux/errno.h>
#include <linux/atomic.h>
#include <linux/clk.h>
#endif

/* Configuration file */
#define MXT_CFG_MAGIC		"OBP_RAW V1"

/* Registers */
#define MXT_OBJECT_START	0x07
#define MXT_OBJECT_SIZE		6
#define MXT_INFO_CHECKSUM_SIZE	3
#define MXT_MAX_BLOCK_WRITE	256

/* Object types */
#define MXT_DEBUG_DIAGNOSTIC_T37	37
#define MXT_GEN_MESSAGE_T5		5
#define MXT_GEN_COMMAND_T6		6
#define MXT_GEN_POWER_T7		7
#define MXT_GEN_ACQUIRE_T8		8
#define MXT_GEN_DATASOURCE_T53		53
#define MXT_TOUCH_MULTI_T9		9
#define MXT_TOUCH_KEYARRAY_T15		15
#define MXT_TOUCH_PROXIMITY_T23		23
#define MXT_TOUCH_PROXKEY_T52		52
#define MXT_PROCI_GRIPFACE_T20		20
#define MXT_PROCG_NOISE_T22		22
#define MXT_PROCI_ONETOUCH_T24		24
#define MXT_PROCI_TWOTOUCH_T27		27
#define MXT_PROCI_GRIP_T40		40
#define MXT_PROCI_PALM_T41		41
#define MXT_PROCI_TOUCHSUPPRESSION_T42	42
#define MXT_PROCI_STYLUS_T47		47
#define MXT_PROCG_NOISESUPPRESSION_T48	48
#define MXT_SPT_COMMSCONFIG_T18		18
#define MXT_SPT_GPIOPWM_T19		19
#define MXT_SPT_SELFTEST_T25		25
#define MXT_SPT_CTECONFIG_T28		28
#define MXT_SPT_USERDATA_T38		38
#define MXT_SPT_DIGITIZER_T43		43
#define MXT_SPT_MESSAGECOUNT_T44	44
#define MXT_SPT_CTECONFIG_T46		46
#define MXT_PROCI_ACTIVE_STYLUS_T63	63
#define MXT_TOUCH_MULTITOUCHSCREEN_T100 100

/* MXT_GEN_MESSAGE_T5 object */
#define MXT_RPTID_NOMSG		0xff

/* MXT_GEN_COMMAND_T6 field */
#define MXT_COMMAND_RESET	0
#define MXT_COMMAND_BACKUPNV	1
#define MXT_COMMAND_CALIBRATE	2
#define MXT_COMMAND_REPORTALL	3
#define MXT_COMMAND_DIAGNOSTIC	5

/* Define for T6 status byte */
#define MXT_T6_STATUS_RESET	(1 << 7)
#define MXT_T6_STATUS_OFL	(1 << 6)
#define MXT_T6_STATUS_SIGERR	(1 << 5)
#define MXT_T6_STATUS_CAL	(1 << 4)
#define MXT_T6_STATUS_CFGERR	(1 << 3)
#define MXT_T6_STATUS_COMSERR	(1 << 2)

/* MXT_GEN_POWER_T7 field */
struct t7_config {
	u8 idle;
	u8 active;
} __packed;

#define MXT_POWER_CFG_RUN		0
#define MXT_POWER_CFG_DEEPSLEEP		1

/* MXT_TOUCH_MULTI_T9 field */
#define MXT_T9_ORIENT		9
#define MXT_T9_RANGE		18

/* MXT_TOUCH_MULTI_T9 status */
#define MXT_T9_UNGRIP		(1 << 0)
#define MXT_T9_SUPPRESS		(1 << 1)
#define MXT_T9_AMP		(1 << 2)
#define MXT_T9_VECTOR		(1 << 3)
#define MXT_T9_MOVE		(1 << 4)
#define MXT_T9_RELEASE		(1 << 5)
#define MXT_T9_PRESS		(1 << 6)
#define MXT_T9_DETECT		(1 << 7)

struct t9_range {
	u16 x;
	u16 y;
} __packed;

/* MXT_TOUCH_MULTI_T9 orient */
#define MXT_T9_ORIENT_SWITCH	(1 << 0)

/* MXT_SPT_COMMSCONFIG_T18 */
#define MXT_COMMS_CTRL		0
#define MXT_COMMS_CMD		1
#define MXT_COMMS_RETRIGEN      (1 << 6)

/* Define for MXT_GEN_COMMAND_T6 */
#define MXT_BOOT_VALUE		0xa5
#define MXT_RESET_VALUE		0x01
#define MXT_BACKUP_VALUE	0x55

/* Define for MXT_PROCI_TOUCHSUPPRESSION_T42 */
#define MXT_T42_MSG_TCHSUP	(1 << 0)

/* T47 Stylus */
#define MXT_TOUCH_MAJOR_T47_STYLUS	1

/* T63 Stylus */
#define MXT_T63_STYLUS_PRESS	(1 << 0)
#define MXT_T63_STYLUS_RELEASE	(1 << 1)
#define MXT_T63_STYLUS_MOVE		(1 << 2)
#define MXT_T63_STYLUS_SUPPRESS	(1 << 3)

#define MXT_T63_STYLUS_DETECT	(1 << 4)
#define MXT_T63_STYLUS_TIP		(1 << 5)
#define MXT_T63_STYLUS_ERASER	(1 << 6)
#define MXT_T63_STYLUS_BARREL	(1 << 7)

#define MXT_T63_STYLUS_PRESSURE_MASK	0x3F

/* T100 Multiple Touch Touchscreen */
#define MXT_T100_CTRL		0
#define MXT_T100_CFG1		1
#define MXT_T100_TCHAUX		3
#define MXT_T100_XRANGE		13
#define MXT_T100_YRANGE		24

#define MXT_T100_CFG_SWITCHXY	(1 << 5)

#define MXT_T100_TCHAUX_VECT	(1 << 0)
#define MXT_T100_TCHAUX_AMPL	(1 << 1)
#define MXT_T100_TCHAUX_AREA	(1 << 2)

#define MXT_T100_DETECT		(1 << 7)
#define MXT_T100_TYPE_MASK	0x70
#define MXT_T100_TYPE_STYLUS	0x20

/* Delay times */
#define MXT_BACKUP_TIME		50	/* msec */
#define MXT_RESET_TIME		200	/* msec */
#define MXT_RESET_TIMEOUT	3000	/* msec */
#define MXT_CRC_TIMEOUT		1000	/* msec */
#define MXT_FW_RESET_TIME	3000	/* msec */
#define MXT_FW_CHG_TIMEOUT	300	/* msec */
#define MXT_WAKEUP_TIME		25	/* msec */
#define MXT_REGULATOR_DELAY	150	/* msec */
#define MXT_POWERON_DELAY	150	/* msec */

/* Command to unlock bootloader */
#define MXT_UNLOCK_CMD_MSB	0xaa
#define MXT_UNLOCK_CMD_LSB	0xdc

/* Bootloader mode status */
#define MXT_WAITING_BOOTLOAD_CMD	0xc0	/* valid 7 6 bit only */
#define MXT_WAITING_FRAME_DATA	0x80	/* valid 7 6 bit only */
#define MXT_FRAME_CRC_CHECK	0x02
#define MXT_FRAME_CRC_FAIL	0x03
#define MXT_FRAME_CRC_PASS	0x04
#define MXT_APP_CRC_FAIL	0x40	/* valid 7 8 bit only */
#define MXT_BOOT_STATUS_MASK	0x3f
#define MXT_BOOT_EXTENDED_ID	(1 << 5)
#define MXT_BOOT_ID_MASK	0x1f

/* Touchscreen absolute values */
#define MXT_MAX_AREA		0xff

#define MXT_PIXELS_PER_MM	20

#define DEBUG_MSG_MAX		200

#define MXT_COORDS_ARR_SIZE	4

/* Orient */
#define MXT_NORMAL		0x0
#define MXT_DIAGONAL		0x1
#define MXT_HORIZONTAL_FLIP	0x2
#define MXT_ROTATED_90_COUNTER	0x3
#define MXT_VERTICAL_FLIP	0x4
#define MXT_ROTATED_90		0x5
#define MXT_ROTATED_180		0x6
#define MXT_DIAGONAL_COUNTER	0x7

/* MXT_TOUCH_KEYARRAY_T15 */
#define MXT_KEYARRAY_MAX_KEYS	32

/* Bootoader IDs */
#define MXT_BOOTLOADER_ID_224		0x0A
#define MXT_BOOTLOADER_ID_224E		0x06
#define MXT_BOOTLOADER_ID_336S		0x1A
#define MXT_BOOTLOADER_ID_1386		0x01
#define MXT_BOOTLOADER_ID_1386E		0x10
#define MXT_BOOTLOADER_ID_1664S		0x14

/* recommended voltage specifications */
#define MXT_VDD_VTG_MIN_UV	1800000
#define MXT_VDD_VTG_MAX_UV	1800000
#define MXT_AVDD_VTG_MIN_UV	2700000
#define MXT_AVDD_VTG_MAX_UV	3300000
#define MXT_XVDD_VTG_MIN_UV	2700000
#define MXT_XVDD_VTG_MAX_UV	10000000

#define MXT_GEN_CFG	"maxtouch_generic_cfg.raw"
#define MXT_NAME_MAX_LEN 100

struct mxt_info {
	u8 family_id;
	u8 variant_id;
	u8 version;
	u8 build;
	u8 matrix_xsize;
	u8 matrix_ysize;
	u8 object_num;
};

struct mxt_object {
	u8 type;
	u16 start_address;
	u8 size_minus_one;
	u8 instances_minus_one;
	u8 num_report_ids;
} __packed;

/* Each client has this additional data */
struct mxt_data {
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct pinctrl *ts_pinctrl;
	struct pinctrl_state *gpio_state_active;
	struct pinctrl_state *gpio_state_suspend;
	char phys[64];		/* device physical location */
	struct mxt_platform_data *pdata;
	struct mxt_object *object_table;
	struct mxt_info *info;
	void *raw_info_block;
	unsigned int irq;
	unsigned int max_x;
	unsigned int max_y;
	bool in_bootloader;
	u16 mem_size;
	u8 t100_aux_ampl;
	u8 t100_aux_area;
	u8 t100_aux_vect;
	struct bin_attribute mem_access_attr;
	bool debug_enabled;
	bool debug_v2_enabled;
	u8 *debug_msg_data;
	u16 debug_msg_count;
	struct bin_attribute debug_msg_attr;
	struct mutex debug_msg_lock;
	u8 max_reportid;
	u32 config_crc;
	u32 info_crc;
	u8 bootloader_addr;
	struct t7_config t7_cfg;
	u8 *msg_buf;
	u8 t6_status;
	bool update_input;
	u8 last_message_count;
	u8 num_touchids;
	u8 num_stylusids;
	unsigned long t15_keystatus;
	bool use_retrigen_workaround;
	bool use_regulator;
	struct regulator *reg_vdd;
	struct regulator *reg_avdd;
	struct regulator *reg_xvdd;
	char fw_name[MXT_NAME_MAX_LEN];
	char cfg_name[MXT_NAME_MAX_LEN];
	u8 cfg_version[3];
	bool fw_w_no_cfg_update;

#if defined(CONFIG_FB)
	struct notifier_block fb_notif;
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	struct early_suspend early_suspend;
#endif
	/* Cached parameters from object table */
	u16 T5_address;
	u8 T5_msg_size;
	u8 T6_reportid;
	u16 T6_address;
	u16 T7_address;
	u8 T9_reportid_min;
	u8 T9_reportid_max;
	u8 T15_reportid_min;
	u8 T15_reportid_max;
	u16 T18_address;
	u8 T19_reportid;
	u8 T42_reportid_min;
	u8 T42_reportid_max;
	u16 T44_address;
	u8 T48_reportid;
	u8 T63_reportid_min;
	u8 T63_reportid_max;
	u8 T100_reportid_min;
	u8 T100_reportid_max;

	/* for fw update in bootloader */
	struct completion bl_completion;

	/* for reset handling */
	struct completion reset_completion;

	/* for reset handling */
	struct completion crc_completion;

	/* Enable reporting of input events */
	bool enable_reporting;

	/* Indicates whether device is in suspend */
	bool suspended;

#if defined(CONFIG_SECURE_TOUCH)
	atomic_t st_enabled;
	atomic_t st_pending_irqs;
	bool st_initialized;
	struct completion st_powerdown;
	struct clk *core_clk;
	struct clk *iface_clk;
#endif
};

static inline unsigned int mxt_obj_size(const struct mxt_object *obj)
{
	return obj->size_minus_one + 1;
}

static inline unsigned int mxt_obj_instances(const struct mxt_object *obj)
{
	return obj->instances_minus_one + 1;
}

static bool mxt_object_readable(unsigned int type)
{
	switch (type) {
	case MXT_GEN_COMMAND_T6:
	case MXT_GEN_POWER_T7:
	case MXT_GEN_ACQUIRE_T8:
	case MXT_GEN_DATASOURCE_T53:
	case MXT_TOUCH_MULTI_T9:
	case MXT_TOUCH_KEYARRAY_T15:
	case MXT_TOUCH_PROXIMITY_T23:
	case MXT_TOUCH_PROXKEY_T52:
	case MXT_PROCI_GRIPFACE_T20:
	case MXT_PROCG_NOISE_T22:
	case MXT_PROCI_ONETOUCH_T24:
	case MXT_PROCI_TWOTOUCH_T27:
	case MXT_PROCI_GRIP_T40:
	case MXT_PROCI_PALM_T41:
	case MXT_PROCI_TOUCHSUPPRESSION_T42:
	case MXT_PROCI_STYLUS_T47:
	case MXT_PROCG_NOISESUPPRESSION_T48:
	case MXT_SPT_COMMSCONFIG_T18:
	case MXT_SPT_GPIOPWM_T19:
	case MXT_SPT_SELFTEST_T25:
	case MXT_SPT_CTECONFIG_T28:
	case MXT_SPT_USERDATA_T38:
	case MXT_SPT_DIGITIZER_T43:
	case MXT_SPT_CTECONFIG_T46:
		return true;
	default:
		return false;
	}
}

static void mxt_dump_message(struct mxt_data *data, u8 *message)
{
	print_hex_dump(KERN_DEBUG, "MXT MSG:", DUMP_PREFIX_NONE, 16, 1,
		       message, data->T5_msg_size, false);
}

static void mxt_debug_msg_enable(struct mxt_data *data)
{
	struct device *dev = &data->client->dev;

	if (data->debug_v2_enabled)
		return;

	mutex_lock(&data->debug_msg_lock);

	data->debug_msg_data = kcalloc(DEBUG_MSG_MAX,
				data->T5_msg_size, GFP_KERNEL);
	if (!data->debug_msg_data) {
		dev_err(&data->client->dev, "Failed to allocate buffer\n");
		return;
	}

	data->debug_v2_enabled = true;
	mutex_unlock(&data->debug_msg_lock);

	dev_info(dev, "Enabled message output\n");
}

static void mxt_debug_msg_disable(struct mxt_data *data)
{
	struct device *dev = &data->client->dev;

	if (!data->debug_v2_enabled)
		return;

	dev_info(dev, "disabling message output\n");
	data->debug_v2_enabled = false;

	mutex_lock(&data->debug_msg_lock);
	kfree(data->debug_msg_data);
	data->debug_msg_data = NULL;
	data->debug_msg_count = 0;
	mutex_unlock(&data->debug_msg_lock);
	dev_info(dev, "Disabled message output\n");
}

static void mxt_debug_msg_add(struct mxt_data *data, u8 *msg)
{
	struct device *dev = &data->client->dev;

	mutex_lock(&data->debug_msg_lock);

	if (!data->debug_msg_data) {
		dev_err(dev, "No buffer!\n");
		return;
	}

	if (data->debug_msg_count < DEBUG_MSG_MAX) {
		memcpy(data->debug_msg_data + data->debug_msg_count * data->T5_msg_size,
		       msg,
		       data->T5_msg_size);
		data->debug_msg_count++;
	} else {
		dev_dbg(dev, "Discarding %u messages\n", data->debug_msg_count);
		data->debug_msg_count = 0;
	}

	mutex_unlock(&data->debug_msg_lock);

	sysfs_notify(&data->client->dev.kobj, NULL, "debug_notify");
}

static ssize_t mxt_debug_msg_write(struct file *filp, struct kobject *kobj,
	struct bin_attribute *bin_attr, char *buf, loff_t off,
	size_t count)
{
	return -EIO;
}

static ssize_t mxt_debug_msg_read(struct file *filp, struct kobject *kobj,
	struct bin_attribute *bin_attr, char *buf, loff_t off, size_t bytes)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct mxt_data *data = dev_get_drvdata(dev);
	int count;
	size_t bytes_read;

	if (!data->debug_msg_data) {
		dev_err(dev, "No buffer!\n");
		return 0;
	}

	count = bytes / data->T5_msg_size;

	if (count > DEBUG_MSG_MAX)
		count = DEBUG_MSG_MAX;

	mutex_lock(&data->debug_msg_lock);

	if (count > data->debug_msg_count)
		count = data->debug_msg_count;

	bytes_read = count * data->T5_msg_size;

	memcpy(buf, data->debug_msg_data, bytes_read);
	data->debug_msg_count = 0;

	mutex_unlock(&data->debug_msg_lock);

	return bytes_read;
}

static int mxt_debug_msg_init(struct mxt_data *data)
{
	sysfs_bin_attr_init(&data->debug_msg_attr);
	data->debug_msg_attr.attr.name = "debug_msg";
	data->debug_msg_attr.attr.mode = 0666;
	data->debug_msg_attr.read = mxt_debug_msg_read;
	data->debug_msg_attr.write = mxt_debug_msg_write;
	data->debug_msg_attr.size = data->T5_msg_size * DEBUG_MSG_MAX;

	if (sysfs_create_bin_file(&data->client->dev.kobj,
				  &data->debug_msg_attr) < 0)
		dev_info(&data->client->dev, "Debugfs already exists\n");

	return 0;
}

static void mxt_debug_msg_remove(struct mxt_data *data)
{
	if (data->debug_msg_attr.attr.name)
		sysfs_remove_bin_file(&data->client->dev.kobj,
				      &data->debug_msg_attr);
}

static int mxt_wait_for_completion(struct mxt_data *data,
			struct completion *comp, unsigned int timeout_ms)
{
	struct device *dev = &data->client->dev;
	unsigned long timeout = msecs_to_jiffies(timeout_ms);
	long ret;

	ret = wait_for_completion_interruptible_timeout(comp, timeout);
	if (ret < 0) {
		dev_err(dev, "Wait for completion interrupted.\n");
		return -EINTR;
	} else if (ret == 0) {
		dev_err(dev, "Wait for completion timed out.\n");
		return -ETIMEDOUT;
	}
	return 0;
}

static int mxt_bootloader_read(struct mxt_data *data,
			       u8 *val, unsigned int count)
{
	int ret;
	struct i2c_msg msg;

	msg.addr = data->bootloader_addr;
	msg.flags = data->client->flags & I2C_M_TEN;
	msg.flags |= I2C_M_RD;
	msg.len = count;
	msg.buf = val;

	ret = i2c_transfer(data->client->adapter, &msg, 1);

	if (ret == 1) {
		ret = 0;
	} else {
		ret = (ret < 0) ? ret : -EIO;
		dev_err(&data->client->dev, "%s: i2c recv failed (%d)\n",
			__func__, ret);
	}

	return ret;
}

static int mxt_bootloader_write(struct mxt_data *data,
				const u8 * const val, unsigned int count)
{
	int ret;
	struct i2c_msg msg;

	msg.addr = data->bootloader_addr;
	msg.flags = data->client->flags & I2C_M_TEN;
	msg.len = count;
	msg.buf = (u8 *)val;

	ret = i2c_transfer(data->client->adapter, &msg, 1);
	if (ret == 1) {
		ret = 0;
	} else {
		ret = (ret < 0) ? ret : -EIO;
		dev_err(&data->client->dev, "%s: i2c send failed (%d)\n",
			__func__, ret);
	}

	return ret;
}

static int mxt_lookup_bootloader_address(struct mxt_data *data, bool retry)
{
	u8 appmode = data->client->addr;
	u8 bootloader;
	u8 family_id = 0;

	if (data->pdata->bl_addr) {
		data->bootloader_addr = data->pdata->bl_addr;
		return 0;
	}

	if (data->info)
		family_id = data->info->family_id;

	switch (appmode) {
	case 0x4a:
	case 0x4b:
		/* Chips after 1664S use different scheme */
		if (retry || family_id >= 0xa2) {
			bootloader = appmode - 0x24;
			break;
		}
		/* Fall through for normal case */
	case 0x4c:
	case 0x4d:
	case 0x5a:
	case 0x5b:
		bootloader = appmode - 0x26;
		break;
	default:
		dev_err(&data->client->dev,
			"Appmode i2c address 0x%02x not found\n",
			appmode);
		return -EINVAL;
	}

	data->bootloader_addr = bootloader;
	return 0;
}

static int mxt_probe_bootloader(struct mxt_data *data, bool retry)
{
	struct device *dev = &data->client->dev;
	int ret;
	u8 val;
	bool crc_failure;

	ret = mxt_lookup_bootloader_address(data, retry);
	if (ret)
		return ret;

	ret = mxt_bootloader_read(data, &val, 1);
	if (ret)
		return ret;

	/* Check app crc fail mode */
	crc_failure = (val & ~MXT_BOOT_STATUS_MASK) == MXT_APP_CRC_FAIL;

	dev_err(dev, "Detected bootloader, status:%02X%s\n",
			val, crc_failure ? ", APP_CRC_FAIL" : "");

	return 0;
}

static u8 mxt_get_bootloader_version(struct mxt_data *data, u8 val)
{
	struct device *dev = &data->client->dev;
	u8 buf[3];

	if (val & MXT_BOOT_EXTENDED_ID) {
		if (mxt_bootloader_read(data, &buf[0], 3) != 0) {
			dev_err(dev, "%s: i2c failure\n", __func__);
			return -EIO;
		}

		dev_info(dev, "Bootloader ID:%d Version:%d\n", buf[1], buf[2]);

		return buf[0];
	} else {
		dev_info(dev, "Bootloader ID:%d\n", val & MXT_BOOT_ID_MASK);

		return val;
	}
}

static int mxt_check_bootloader(struct mxt_data *data, unsigned int state,
				bool wait)
{
	struct device *dev = &data->client->dev;
	u8 val;
	int ret;

recheck:
	if (wait) {
		/*
		 * In application update mode, the interrupt
		 * line signals state transitions. We must wait for the
		 * CHG assertion before reading the status byte.
		 * Once the status byte has been read, the line is deasserted.
		 */
		ret = mxt_wait_for_completion(data, &data->bl_completion,
					      MXT_FW_CHG_TIMEOUT);
		if (ret) {
			/*
			 * TODO: handle -EINTR better by terminating fw update
			 * process before returning to userspace by writing
			 * length 0x000 to device (iff we are in
			 * WAITING_FRAME_DATA state).
			 */
			dev_err(dev, "Update wait error %d\n", ret);
			return ret;
		}
	}

	ret = mxt_bootloader_read(data, &val, 1);
	if (ret)
		return ret;

	if (state == MXT_WAITING_BOOTLOAD_CMD)
		val = mxt_get_bootloader_version(data, val);

	switch (state) {
	case MXT_WAITING_BOOTLOAD_CMD:
	case MXT_WAITING_FRAME_DATA:
	case MXT_APP_CRC_FAIL:
		val &= ~MXT_BOOT_STATUS_MASK;
		break;
	case MXT_FRAME_CRC_PASS:
		if (val == MXT_FRAME_CRC_CHECK) {
			goto recheck;
		} else if (val == MXT_FRAME_CRC_FAIL) {
			dev_err(dev, "Bootloader CRC fail\n");
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	if (val != state) {
		dev_err(dev, "Invalid bootloader state %02X != %02X\n",
			val, state);
		return -EINVAL;
	}

	return 0;
}

static int mxt_send_bootloader_cmd(struct mxt_data *data, bool unlock)
{
	int ret;
	u8 buf[2];

	if (unlock) {
		buf[0] = MXT_UNLOCK_CMD_LSB;
		buf[1] = MXT_UNLOCK_CMD_MSB;
	} else {
		buf[0] = 0x01;
		buf[1] = 0x01;
	}

	ret = mxt_bootloader_write(data, buf, 2);
	if (ret)
		return ret;

	return 0;
}

static int __mxt_read_reg(struct i2c_client *client,
			       u16 reg, u16 len, void *val)
{
	struct i2c_msg xfer[2];
	u8 buf[2];
	int ret;
	bool retry = false;

	buf[0] = reg & 0xff;
	buf[1] = (reg >> 8) & 0xff;

	/* Write register */
	xfer[0].addr = client->addr;
	xfer[0].flags = 0;
	xfer[0].len = 2;
	xfer[0].buf = buf;

	/* Read data */
	xfer[1].addr = client->addr;
	xfer[1].flags = I2C_M_RD;
	xfer[1].len = len;
	xfer[1].buf = val;

retry_read:
	ret = i2c_transfer(client->adapter, xfer, ARRAY_SIZE(xfer));
	if (ret != ARRAY_SIZE(xfer)) {
		if (!retry) {
			dev_dbg(&client->dev, "%s: i2c retry\n", __func__);
			msleep(MXT_WAKEUP_TIME);
			retry = true;
			goto retry_read;
		} else {
			dev_err(&client->dev, "%s: i2c transfer failed (%d)\n",
				__func__, ret);
			return -EIO;
		}
	}

	return 0;
}

static int __mxt_write_reg(struct i2c_client *client, u16 reg, u16 len,
			   const void *val)
{
	u8 *buf;
	int count;
	int ret;
	bool retry = false;

	count = len + 2;
	buf = kmalloc(count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	buf[0] = reg & 0xff;
	buf[1] = (reg >> 8) & 0xff;
	memcpy(&buf[2], val, len);

retry_write:
	ret = i2c_master_send(client, buf, count);
	if (ret != count) {
		if (!retry) {
			dev_dbg(&client->dev, "%s: i2c retry\n", __func__);
			msleep(MXT_WAKEUP_TIME);
			retry = true;
			goto retry_write;
		} else {
			dev_err(&client->dev, "%s: i2c send failed (%d)\n",
				__func__, ret);
			ret = -EIO;
		}
	} else {
		ret = 0;
	}

	kfree(buf);
	return ret;
}

static int mxt_write_reg(struct i2c_client *client, u16 reg, u8 val)
{
	return __mxt_write_reg(client, reg, 1, &val);
}

static struct mxt_object *
mxt_get_object(struct mxt_data *data, u8 type)
{
	struct mxt_object *object;
	int i;

	for (i = 0; i < data->info->object_num; i++) {
		object = data->object_table + i;
		if (object->type == type)
			return object;
	}

	dev_warn(&data->client->dev, "Invalid object type T%u\n", type);
	return NULL;
}

static void mxt_proc_t6_messages(struct mxt_data *data, u8 *msg)
{
	struct device *dev = &data->client->dev;
	u8 status = msg[1];
	u32 crc = msg[2] | (msg[3] << 8) | (msg[4] << 16);

	if (crc != data->config_crc) {
		data->config_crc = crc;
		dev_dbg(dev, "T6 Config Checksum: 0x%06X\n", crc);
		complete(&data->crc_completion);
	}

	/* Detect transition out of reset */
	if ((data->t6_status & MXT_T6_STATUS_RESET) &&
	    !(status & MXT_T6_STATUS_RESET))
		complete(&data->reset_completion);

	/* Output debug if status has changed */
	if (status != data->t6_status)
		dev_dbg(dev, "T6 Status 0x%02X%s%s%s%s%s%s%s\n",
			status,
			(status == 0) ? " OK" : "",
			(status & MXT_T6_STATUS_RESET) ? " RESET" : "",
			(status & MXT_T6_STATUS_OFL) ? " OFL" : "",
			(status & MXT_T6_STATUS_SIGERR) ? " SIGERR" : "",
			(status & MXT_T6_STATUS_CAL) ? " CAL" : "",
			(status & MXT_T6_STATUS_CFGERR) ? " CFGERR" : "",
			(status & MXT_T6_STATUS_COMSERR) ? " COMSERR" : "");

	/* Save current status */
	data->t6_status = status;
}

static void mxt_input_button(struct mxt_data *data, u8 *message)
{
	struct input_dev *input = data->input_dev;
	const struct mxt_platform_data *pdata = data->pdata;
	bool button;
	int i;

	/* do not report events if input device not yet registered */
	if (!data->enable_reporting)
		return;

	/* Active-low switch */
	for (i = 0; i < pdata->t19_num_keys; i++) {
		if (pdata->t19_keymap[i] == KEY_RESERVED)
			continue;
		button = !(message[1] & (1 << i));
		input_report_key(input, pdata->t19_keymap[i], button);
	}
}

static void mxt_input_sync(struct input_dev *input_dev)
{
	input_mt_report_pointer_emulation(input_dev, false);
	input_sync(input_dev);
}

static void mxt_proc_t9_message(struct mxt_data *data, u8 *message)
{
	struct device *dev = &data->client->dev;
	struct input_dev *input_dev = data->input_dev;
	int id;
	u8 status;
	int x;
	int y;
	int area;
	int amplitude;
	u8 vector;
	int tool;

	/* do not report events if input device not yet registered */
	if (!data->enable_reporting)
		return;

	id = message[0] - data->T9_reportid_min;
	status = message[1];
	x = (message[2] << 4) | ((message[4] >> 4) & 0xf);
	y = (message[3] << 4) | ((message[4] & 0xf));

	/* Handle 10/12 bit switching */
	if (data->max_x < 1024)
		x >>= 2;
	if (data->max_y < 1024)
		y >>= 2;

	area = message[5];

	amplitude = message[6];
	vector = message[7];

	dev_dbg(dev,
		"[%u] %c%c%c%c%c%c%c%c x: %5u y: %5u area: %3u amp: %3u vector: %02X\n",
		id,
		(status & MXT_T9_DETECT) ? 'D' : '.',
		(status & MXT_T9_PRESS) ? 'P' : '.',
		(status & MXT_T9_RELEASE) ? 'R' : '.',
		(status & MXT_T9_MOVE) ? 'M' : '.',
		(status & MXT_T9_VECTOR) ? 'V' : '.',
		(status & MXT_T9_AMP) ? 'A' : '.',
		(status & MXT_T9_SUPPRESS) ? 'S' : '.',
		(status & MXT_T9_UNGRIP) ? 'U' : '.',
		x, y, area, amplitude, vector);

	input_mt_slot(input_dev, id);

	if (status & MXT_T9_DETECT) {
		/* Multiple bits may be set if the host is slow to read the
		 * status messages, indicating all the events that have
		 * happened */
		if (status & MXT_T9_RELEASE) {
			input_mt_report_slot_state(input_dev,
						   MT_TOOL_FINGER, 0);
			mxt_input_sync(input_dev);
		}

		/* A reported size of zero indicates that the reported touch
		 * is a stylus from a linked Stylus T47 object. */
		if (area == 0) {
			area = MXT_TOUCH_MAJOR_T47_STYLUS;
			tool = MT_TOOL_PEN;
		} else {
			tool = MT_TOOL_FINGER;
		}

		/* Touch active */
		input_mt_report_slot_state(input_dev, tool, 1);
		input_report_abs(input_dev, ABS_MT_POSITION_X, x);
		input_report_abs(input_dev, ABS_MT_POSITION_Y, y);
		input_report_abs(input_dev, ABS_MT_PRESSURE, amplitude);
		input_report_abs(input_dev, ABS_MT_TOUCH_MAJOR, area);
		input_report_abs(input_dev, ABS_MT_ORIENTATION, vector);
	} else {
		/* Touch no longer active, close out slot */
		input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, 0);
	}

	data->update_input = true;
}

static void mxt_proc_t100_message(struct mxt_data *data, u8 *message)
{
	struct device *dev = &data->client->dev;
	struct input_dev *input_dev = data->input_dev;
	int id;
	u8 status;
	int x;
	int y;
	int tool;

	/* do not report events if input device not yet registered */
	if (!data->enable_reporting)
		return;

	id = message[0] - data->T100_reportid_min - 2;

	/* ignore SCRSTATUS events */
	if (id < 0)
		return;

	status = message[1];
	x = (message[3] << 8) | message[2];
	y = (message[5] << 8) | message[4];

	dev_dbg(dev,
		"[%u] status:%02X x:%u y:%u area:%02X amp:%02X vec:%02X\n",
		id,
		status,
		x, y,
		(data->t100_aux_area) ? message[data->t100_aux_area] : 0,
		(data->t100_aux_ampl) ? message[data->t100_aux_ampl] : 0,
		(data->t100_aux_vect) ? message[data->t100_aux_vect] : 0);

	input_mt_slot(input_dev, id);

	if (status & MXT_T100_DETECT) {
		/* A reported size of zero indicates that the reported touch
		 * is a stylus from a linked Stylus T47 object. */
		if ((status & MXT_T100_TYPE_MASK) == MXT_T100_TYPE_STYLUS)
			tool = MT_TOOL_PEN;
		else
			tool = MT_TOOL_FINGER;

		/* Touch active */
		input_mt_report_slot_state(input_dev, tool, 1);
		input_report_abs(input_dev, ABS_MT_POSITION_X, x);
		input_report_abs(input_dev, ABS_MT_POSITION_Y, y);

		if (data->t100_aux_ampl)
			input_report_abs(input_dev, ABS_MT_PRESSURE,
					 message[data->t100_aux_ampl]);
		else
			input_report_abs(input_dev, ABS_MT_PRESSURE, 255);

		if (data->t100_aux_area) {
			if (tool == MT_TOOL_PEN)
				input_report_abs(input_dev, ABS_MT_TOUCH_MAJOR,
						 MXT_TOUCH_MAJOR_T47_STYLUS);
			else
				input_report_abs(input_dev, ABS_MT_TOUCH_MAJOR,
						 message[data->t100_aux_area]);
		}

		if (data->t100_aux_vect)
			input_report_abs(input_dev, ABS_MT_ORIENTATION,
					 message[data->t100_aux_vect]);
	} else {
		/* Touch no longer active, close out slot */
		input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, 0);
	}

	data->update_input = true;
}


static void mxt_proc_t15_messages(struct mxt_data *data, u8 *msg)
{
	struct input_dev *input_dev = data->input_dev;
	struct device *dev = &data->client->dev;
	int key;
	bool curr_state, new_state;
	bool sync = false;
	unsigned long keystates = le32_to_cpu(msg[2]);

	/* do not report events if input device not yet registered */
	if (!data->enable_reporting)
		return;

	for (key = 0; key < data->pdata->t15_num_keys; key++) {
		curr_state = test_bit(key, &data->t15_keystatus);
		new_state = test_bit(key, &keystates);

		if (!curr_state && new_state) {
			dev_dbg(dev, "T15 key press: %u\n", key);
			__set_bit(key, &data->t15_keystatus);
			input_event(input_dev, EV_KEY,
				    data->pdata->t15_keymap[key], 1);
			sync = true;
		} else if (curr_state && !new_state) {
			dev_dbg(dev, "T15 key release: %u\n", key);
			__clear_bit(key, &data->t15_keystatus);
			input_event(input_dev, EV_KEY,
				    data->pdata->t15_keymap[key], 0);
			sync = true;
		}
	}

	if (sync)
		input_sync(input_dev);
}

static void mxt_proc_t42_messages(struct mxt_data *data, u8 *msg)
{
	struct device *dev = &data->client->dev;
	u8 status = msg[1];

	if (status & MXT_T42_MSG_TCHSUP)
		dev_info(dev, "T42 suppress\n");
	else
		dev_info(dev, "T42 normal\n");
}

static int mxt_proc_t48_messages(struct mxt_data *data, u8 *msg)
{
	struct device *dev = &data->client->dev;
	u8 status, state;

	status = msg[1];
	state  = msg[4];

	dev_dbg(dev, "T48 state %d status %02X %s%s%s%s%s\n",
			state,
			status,
			(status & 0x01) ? "FREQCHG " : "",
			(status & 0x02) ? "APXCHG " : "",
			(status & 0x04) ? "ALGOERR " : "",
			(status & 0x10) ? "STATCHG " : "",
			(status & 0x20) ? "NLVLCHG " : "");

	return 0;
}

static void mxt_proc_t63_messages(struct mxt_data *data, u8 *msg)
{
	struct device *dev = &data->client->dev;
	struct input_dev *input_dev = data->input_dev;
	u8 id;
	u16 x, y;
	u8 pressure;

	/* do not report events if input device not yet registered */
	if (!data->enable_reporting)
		return;

	/* stylus slots come after touch slots */
	id = data->num_touchids + (msg[0] - data->T63_reportid_min);

	if (id < 0 || id > (data->num_touchids + data->num_stylusids)) {
		dev_err(dev, "invalid stylus id %d, max slot is %d\n",
			id, data->num_stylusids);
		return;
	}

	x = msg[3] | (msg[4] << 8);
	y = msg[5] | (msg[6] << 8);
	pressure = msg[7] & MXT_T63_STYLUS_PRESSURE_MASK;

	dev_dbg(dev,
		"[%d] %c%c%c%c x: %d y: %d pressure: %d stylus:%c%c%c%c\n",
		id,
		(msg[1] & MXT_T63_STYLUS_SUPPRESS) ? 'S' : '.',
		(msg[1] & MXT_T63_STYLUS_MOVE)     ? 'M' : '.',
		(msg[1] & MXT_T63_STYLUS_RELEASE)  ? 'R' : '.',
		(msg[1] & MXT_T63_STYLUS_PRESS)    ? 'P' : '.',
		x, y, pressure,
		(msg[2] & MXT_T63_STYLUS_BARREL) ? 'B' : '.',
		(msg[2] & MXT_T63_STYLUS_ERASER) ? 'E' : '.',
		(msg[2] & MXT_T63_STYLUS_TIP)    ? 'T' : '.',
		(msg[2] & MXT_T63_STYLUS_DETECT) ? 'D' : '.');

	input_mt_slot(input_dev, id);

	if (msg[2] & MXT_T63_STYLUS_DETECT) {
		input_mt_report_slot_state(input_dev, MT_TOOL_PEN, 1);
		input_report_abs(input_dev, ABS_MT_POSITION_X, x);
		input_report_abs(input_dev, ABS_MT_POSITION_Y, y);
		input_report_abs(input_dev, ABS_MT_PRESSURE, pressure);
	} else {
		input_mt_report_slot_state(input_dev, MT_TOOL_PEN, 0);
	}

	input_report_key(input_dev, BTN_STYLUS,
			 (msg[2] & MXT_T63_STYLUS_ERASER));
	input_report_key(input_dev, BTN_STYLUS2,
			 (msg[2] & MXT_T63_STYLUS_BARREL));

	mxt_input_sync(input_dev);
}

static int mxt_proc_message(struct mxt_data *data, u8 *message)
{
	u8 report_id = message[0];
	bool dump = data->debug_enabled;

	if (report_id == MXT_RPTID_NOMSG)
		return 0;

	if (report_id == data->T6_reportid) {
		mxt_proc_t6_messages(data, message);
	} else if (report_id >= data->T9_reportid_min
	    && report_id <= data->T9_reportid_max) {
		mxt_proc_t9_message(data, message);
	} else if (report_id >= data->T100_reportid_min
	    && report_id <= data->T100_reportid_max) {
		mxt_proc_t100_message(data, message);
	} else if (report_id == data->T19_reportid) {
		mxt_input_button(data, message);
		data->update_input = true;
	} else if (report_id >= data->T63_reportid_min
		   && report_id <= data->T63_reportid_max) {
		mxt_proc_t63_messages(data, message);
	} else if (report_id >= data->T42_reportid_min
		   && report_id <= data->T42_reportid_max) {
		mxt_proc_t42_messages(data, message);
	} else if (report_id == data->T48_reportid) {
		mxt_proc_t48_messages(data, message);
	} else if (report_id >= data->T15_reportid_min
		   && report_id <= data->T15_reportid_max) {
		mxt_proc_t15_messages(data, message);
	} else {
		dump = true;
	}

	if (dump)
		mxt_dump_message(data, message);

	if (data->debug_v2_enabled)
		mxt_debug_msg_add(data, message);

	return 1;
}

static int mxt_read_and_process_messages(struct mxt_data *data, u8 count)
{
	struct device *dev = &data->client->dev;
	int ret;
	int i;
	u8 num_valid = 0;

	/* Safety check for msg_buf */
	if (count > data->max_reportid)
		return -EINVAL;

	/* Process remaining messages if necessary */
	ret = __mxt_read_reg(data->client, data->T5_address,
				data->T5_msg_size * count, data->msg_buf);
	if (ret) {
		dev_err(dev, "Failed to read %u messages (%d)\n", count, ret);
		return ret;
	}

	for (i = 0;  i < count; i++) {
		ret = mxt_proc_message(data,
			data->msg_buf + data->T5_msg_size * i);

		if (ret == 1)
			num_valid++;
	}

	/* return number of messages read */
	return num_valid;
}

static irqreturn_t mxt_process_messages_t44(struct mxt_data *data)
{
	struct device *dev = &data->client->dev;
	int ret;
	u8 count, num_left;

	/* Read T44 and T5 together */
	ret = __mxt_read_reg(data->client, data->T44_address,
		data->T5_msg_size + 1, data->msg_buf);
	if (ret) {
		dev_err(dev, "Failed to read T44 and T5 (%d)\n", ret);
		return IRQ_NONE;
	}

	count = data->msg_buf[0];

	if (count == 0) {
		dev_warn(dev, "Interrupt triggered but zero messages\n");
		return IRQ_NONE;
	} else if (count > data->max_reportid) {
		dev_err(dev, "T44 count %d exceeded max report id\n", count);
		count = data->max_reportid;
	}

	/* Process first message */
	ret = mxt_proc_message(data, data->msg_buf + 1);
	if (ret < 0) {
		dev_warn(dev, "Unexpected invalid message\n");
		return IRQ_NONE;
	}

	num_left = count - 1;

	/* Process remaining messages if necessary */
	if (num_left) {
		ret = mxt_read_and_process_messages(data, num_left);
		if (ret < 0)
			goto end;
		else if (ret != num_left)
			dev_warn(dev, "Unexpected invalid message\n");
	}

end:
	if (data->update_input) {
		mxt_input_sync(data->input_dev);
		data->update_input = false;
	}

	return IRQ_HANDLED;
}

static int mxt_process_messages_until_invalid(struct mxt_data *data)
{
	struct device *dev = &data->client->dev;
	int count, read;
	u8 tries = 2;

	count = data->max_reportid;

	/* Read messages until we force an invalid */
	do {
		read = mxt_read_and_process_messages(data, count);
		if (read < count)
			return 0;
	} while (--tries);

	if (data->update_input) {
		mxt_input_sync(data->input_dev);
		data->update_input = false;
	}

	dev_err(dev, "CHG pin isn't cleared\n");
	return -EBUSY;
}

static irqreturn_t mxt_process_messages(struct mxt_data *data)
{
	int total_handled, num_handled;
	u8 count = data->last_message_count;

	if (count < 1 || count > data->max_reportid)
		count = 1;

	/* include final invalid message */
	total_handled = mxt_read_and_process_messages(data, count + 1);
	if (total_handled < 0)
		return IRQ_NONE;
	/* if there were invalid messages, then we are done */
	else if (total_handled <= count)
		goto update_count;

	/* read two at a time until an invalid message or else we reach
	 * reportid limit */
	do {
		num_handled = mxt_read_and_process_messages(data, 2);
		if (num_handled < 0)
			return IRQ_NONE;

		total_handled += num_handled;

		if (num_handled < 2)
			break;
	} while (total_handled < data->num_touchids);

update_count:
	data->last_message_count = total_handled;

	if (data->enable_reporting && data->update_input) {
		mxt_input_sync(data->input_dev);
		data->update_input = false;
	}

	return IRQ_HANDLED;
}

#if defined(CONFIG_SECURE_TOUCH)
static void mxt_secure_touch_notify(struct mxt_data *data)
{
	sysfs_notify(&data->client->dev.kobj, NULL, "secure_touch");
}

static irqreturn_t mxt_filter_interrupt(struct mxt_data *data)
{
	if (atomic_read(&data->st_enabled)) {
		if (atomic_cmpxchg(&data->st_pending_irqs, 0, 1) == 0)
			mxt_secure_touch_notify(data);
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}
#else
static irqreturn_t mxt_filter_interrupt(struct mxt_data *data)
{
	return IRQ_NONE;
}
#endif

static irqreturn_t mxt_interrupt(int irq, void *dev_id)
{
	struct mxt_data *data = dev_id;

	if (data->in_bootloader) {
		/* bootloader state transition completion */
		complete(&data->bl_completion);
		return IRQ_HANDLED;
	}

	if (IRQ_HANDLED == mxt_filter_interrupt(data))
		return IRQ_HANDLED;

	if (!data->object_table)
		return IRQ_NONE;

	if (data->T44_address) {
		return mxt_process_messages_t44(data);
	} else {
		return mxt_process_messages(data);
	}
}

static int mxt_t6_command(struct mxt_data *data, u16 cmd_offset,
			  u8 value, bool wait)
{
	u16 reg;
	u8 command_register;
	int timeout_counter = 0;
	int ret;

	reg = data->T6_address + cmd_offset;

	ret = mxt_write_reg(data->client, reg, value);
	if (ret)
		return ret;

	if (!wait)
		return 0;

	do {
		msleep(20);
		ret = __mxt_read_reg(data->client, reg, 1, &command_register);
		if (ret)
			return ret;
	} while ((command_register != 0) && (timeout_counter++ <= 100));

	if (timeout_counter > 100) {
		dev_err(&data->client->dev, "Command failed!\n");
		return -EIO;
	}

	return 0;
}

static int mxt_soft_reset(struct mxt_data *data)
{
	struct device *dev = &data->client->dev;
	int ret = 0;

	dev_info(dev, "Resetting chip\n");

	INIT_COMPLETION(data->reset_completion);

	ret = mxt_t6_command(data, MXT_COMMAND_RESET, MXT_RESET_VALUE, false);
	if (ret)
		return ret;

	ret = mxt_wait_for_completion(data, &data->reset_completion,
				      MXT_RESET_TIMEOUT);
	if (ret)
		return ret;

	return 0;
}

static void mxt_update_crc(struct mxt_data *data, u8 cmd, u8 value)
{
	/* on failure, CRC is set to 0 and config will always be downloaded */
	data->config_crc = 0;
	INIT_COMPLETION(data->crc_completion);

	mxt_t6_command(data, cmd, value, true);

	/* Wait for crc message. On failure, CRC is set to 0 and config will
	 * always be downloaded */
	mxt_wait_for_completion(data, &data->crc_completion, MXT_CRC_TIMEOUT);
}

static void mxt_calc_crc24(u32 *crc, u8 firstbyte, u8 secondbyte)
{
	static const unsigned int crcpoly = 0x80001B;
	u32 result;
	u32 data_word;

	data_word = (secondbyte << 8) | firstbyte;
	result = ((*crc << 1) ^ data_word);

	if (result & 0x1000000)
		result ^= crcpoly;

	*crc = result;
}

static u32 mxt_calculate_crc(u8 *base, off_t start_off, off_t end_off)
{
	u32 crc = 0;
	u8 *ptr = base + start_off;
	u8 *last_val = base + end_off - 1;

	if (end_off < start_off)
		return -EINVAL;

	while (ptr < last_val) {
		mxt_calc_crc24(&crc, *ptr, *(ptr + 1));
		ptr += 2;
	}

	/* if len is odd, fill the last byte with 0 */
	if (ptr == last_val)
		mxt_calc_crc24(&crc, *ptr, 0);

	/* Mask to 24-bit */
	crc &= 0x00FFFFFF;

	return crc;
}

static int mxt_check_retrigen(struct mxt_data *data)
{
	struct i2c_client *client = data->client;
	int error;
	int val;

	if (data->pdata->irqflags & IRQF_TRIGGER_LOW)
		return 0;

	if (data->T18_address) {
		error = __mxt_read_reg(client,
				       data->T18_address + MXT_COMMS_CTRL,
				       1, &val);
		if (error)
			return error;

		if (val & MXT_COMMS_RETRIGEN)
			return 0;
	}

	dev_warn(&client->dev, "Enabling RETRIGEN workaround\n");
	data->use_retrigen_workaround = true;
	return 0;
}

static int mxt_init_t7_power_cfg(struct mxt_data *data);

static int mxt_update_cfg_version(struct mxt_data *data)
{
	struct mxt_object *object;
	int error;

	object = mxt_get_object(data, MXT_SPT_USERDATA_T38);
	if (!object)
		return -EINVAL;

	error = __mxt_read_reg(data->client, object->start_address,
			       sizeof(data->cfg_version), &data->cfg_version);
	if (error)
		return error;

	return 0;
}

static int mxt_update_t100_resolution(struct mxt_data *data)
{
	struct i2c_client *client = data->client;
	int error;
	struct mxt_object *object;
	u16 range_x, range_y, temp;
	u8 cfg, tchaux;
	u8 aux;
	bool update = false;

	object = mxt_get_object(data, MXT_TOUCH_MULTITOUCHSCREEN_T100);
	if (!object)
		return -EINVAL;

	error = __mxt_read_reg(client,
			       object->start_address + MXT_T100_XRANGE,
			       sizeof(range_x), &range_x);
	if (error)
		return error;

	le16_to_cpus(range_x);

	error = __mxt_read_reg(client,
			       object->start_address + MXT_T100_YRANGE,
			       sizeof(range_y), &range_y);
	if (error)
		return error;

	le16_to_cpus(range_y);

	error =  __mxt_read_reg(client,
				object->start_address + MXT_T100_CFG1,
				1, &cfg);
	if (error)
		return error;

	error =  __mxt_read_reg(client,
				object->start_address + MXT_T100_TCHAUX,
				1, &tchaux);
	if (error)
		return error;

	/* Handle default values */
	if (range_x == 0)
		range_x = 1023;

	/* Handle default values */
	if (range_x == 0)
		range_x = 1023;

	if (range_y == 0)
		range_y = 1023;

	dev_dbg(&client->dev, "initial x=%d y=%d\n", range_x, range_y);

	if (cfg & MXT_T100_CFG_SWITCHXY) {
		dev_dbg(&client->dev, "flip x and y\n");
		temp = range_y;
		range_y = range_x;
		range_x = temp;
	}

	if (data->pdata->panel_maxx != range_x) {
		if (cfg & MXT_T100_CFG_SWITCHXY)
			range_x = data->pdata->panel_maxy;
		else
			range_x = data->pdata->panel_maxx;
		cpu_to_le16s(range_x);
		error =  __mxt_write_reg(client,
				object->start_address + MXT_T100_XRANGE,
				sizeof(range_x), &range_x);
		if (error)
			return error;
		dev_dbg(&client->dev, "panel maxx mismatch. update\n");
		update = true;
	}

	if (data->pdata->panel_maxy != range_y) {
		if (cfg & MXT_T100_CFG_SWITCHXY)
			range_y = data->pdata->panel_maxx;
		else
			range_y = data->pdata->panel_maxy;

		cpu_to_le16s(range_y);
		error =  __mxt_write_reg(client,
				object->start_address + MXT_T100_YRANGE,
				sizeof(range_y), &range_y);
		if (error)
			return error;
		dev_dbg(&client->dev, "panel maxy mismatch. update\n");
		update = true;
	}

	if (update) {
		mxt_update_crc(data, MXT_COMMAND_BACKUPNV, MXT_BACKUP_VALUE);

		mxt_soft_reset(data);
	}

	/* allocate aux bytes */
	aux = 6;

	if (tchaux & MXT_T100_TCHAUX_VECT)
		data->t100_aux_vect = aux++;

	if (tchaux & MXT_T100_TCHAUX_AMPL)
		data->t100_aux_ampl = aux++;

	if (tchaux & MXT_T100_TCHAUX_AREA)
		data->t100_aux_area = aux++;

	dev_info(&client->dev,
		 "T100 Touchscreen size X%uY%u\n", range_x, range_y);

	return 0;
}

static int mxt_update_t9_resolution(struct mxt_data *data)
{
	struct i2c_client *client = data->client;
	int error;
	struct t9_range range;
	unsigned char orient;
	struct mxt_object *object;
	u16 temp;
	bool update = false;

	object = mxt_get_object(data, MXT_TOUCH_MULTI_T9);
	if (!object)
		return -EINVAL;

	error = __mxt_read_reg(client,
			       object->start_address + MXT_T9_RANGE,
			       sizeof(range), &range);
	if (error)
		return error;

	le16_to_cpus(range.x);
	le16_to_cpus(range.y);

	error =  __mxt_read_reg(client,
				object->start_address + MXT_T9_ORIENT,
				1, &orient);
	if (error)
		return error;

	/* Handle default values */
	if (range.x == 0)
		range.x = 1023;

	if (range.y == 0)
		range.y = 1023;

	dev_dbg(&client->dev, "initial x=%d y=%d\n", range.x, range.y);

	if (orient & MXT_T9_ORIENT_SWITCH) {
		dev_dbg(&client->dev, "flip x and y\n");
		temp = range.y;
		range.y = range.x;
		range.x = temp;
	}

	if (data->pdata->panel_maxx != range.x) {
		if (orient & MXT_T9_ORIENT_SWITCH)
			range.x = data->pdata->panel_maxy;
		else
			range.x = data->pdata->panel_maxx;
		cpu_to_le16s(range.x);
		error =  __mxt_write_reg(client,
				object->start_address + MXT_T100_XRANGE,
				sizeof(range.x), &range.x);
		if (error)
			return error;
		dev_dbg(&client->dev, "panel maxx mismatch. update\n");
		update = true;
	}

	if (data->pdata->panel_maxy != range.y) {
		if (orient & MXT_T9_ORIENT_SWITCH)
			range.y = data->pdata->panel_maxx;
		else
			range.y = data->pdata->panel_maxy;

		cpu_to_le16s(range.y);
		error =  __mxt_write_reg(client,
				object->start_address + MXT_T100_YRANGE,
				sizeof(range.y), &range.y);
		if (error)
			return error;
		dev_dbg(&client->dev, "panel maxy mismatch. update\n");
		update = true;
	}

	if (update) {
		mxt_update_crc(data, MXT_COMMAND_BACKUPNV, MXT_BACKUP_VALUE);

		mxt_soft_reset(data);
	}

	dev_info(&client->dev,
		 "Touchscreen size X%uY%u\n", range.x, range.y);

	return 0;
}

/*
 * mxt_load_cfg - download configuration to chip
 *
 * Atmel Raw Config File Format
 *
 * The first four lines of the raw config file contain:
 *  1) Version
 *  2) Chip ID Information (first 7 bytes of device memory)
 *  3) Chip Information Block 24-bit CRC Checksum
 *  4) Chip Configuration 24-bit CRC Checksum
 *
 * The rest of the file consists of one line per object instance:
 *   <TYPE> <INSTANCE> <SIZE> <CONTENTS>
 *
 *   <TYPE> - 2-byte object type as hex
 *   <INSTANCE> - 2-byte object instance number as hex
 *   <SIZE> - 2-byte object size as hex
 *   <CONTENTS> - array of <SIZE> 1-byte hex values
 */
static int mxt_load_cfg(struct mxt_data *data, bool force)
{
	struct device *dev = &data->client->dev;
	struct mxt_info cfg_info;
	struct mxt_object *object;
	const struct firmware *cfg = NULL;
	int ret = 0;
	int offset;
	int data_pos;
	int byte_offset;
	int i;
	int cfg_start_ofs;
	u32 info_crc, config_crc, calculated_crc;
	u8 *config_mem;
	unsigned int config_mem_size;
	unsigned int type, instance, size;
	u8 val;
	int ver[3];
	u16 reg;

	if (!data->cfg_name) {
		dev_dbg(dev, "Skipping cfg download\n");
		goto report_enable;
	}

	ret = request_firmware(&cfg, data->cfg_name, dev);
	if (ret < 0) {
		dev_err(dev, "Failure to request config file %s\n",
			data->cfg_name);
		goto report_enable;
	}

	mxt_update_crc(data, MXT_COMMAND_REPORTALL, 1);

	if (strncmp(cfg->data, MXT_CFG_MAGIC, strlen(MXT_CFG_MAGIC))) {
		dev_err(dev, "Unrecognised config file\n");
		ret = -EINVAL;
		goto release;
	}

	data_pos = strlen(MXT_CFG_MAGIC);

	/* Load information block and check */
	for (i = 0; i < sizeof(struct mxt_info); i++) {
		ret = sscanf(cfg->data + data_pos, "%hhx%n",
			     (unsigned char *)&cfg_info + i,
			     &offset);
		if (ret != 1) {
			dev_err(dev, "Bad format\n");
			ret = -EINVAL;
			goto release;
		}

		data_pos += offset;
	}

	if (cfg_info.family_id != data->info->family_id) {
		dev_err(dev, "Family ID mismatch!\n");
		ret = -EINVAL;
		goto release;
	}

	if (cfg_info.variant_id != data->info->variant_id) {
		dev_err(dev, "Variant ID mismatch!\n");
		ret = -EINVAL;
		goto release;
	}

	/* Read CRCs */
	ret = sscanf(cfg->data + data_pos, "%x%n", &info_crc, &offset);
	if (ret != 1) {
		dev_err(dev, "Bad format: failed to parse Info CRC\n");
		ret = -EINVAL;
		goto release;
	}
	data_pos += offset;

	ret = sscanf(cfg->data + data_pos, "%x%n", &config_crc, &offset);
	if (ret != 1) {
		dev_err(dev, "Bad format: failed to parse Config CRC\n");
		ret = -EINVAL;
		goto release;
	}
	data_pos += offset;

	/* Malloc memory to store configuration */
	cfg_start_ofs = MXT_OBJECT_START
		+ data->info->object_num * sizeof(struct mxt_object)
		+ MXT_INFO_CHECKSUM_SIZE;
	config_mem_size = data->mem_size - cfg_start_ofs;
	config_mem = kzalloc(config_mem_size, GFP_KERNEL);
	if (!config_mem) {
		dev_err(dev, "Failed to allocate memory\n");
		ret = -ENOMEM;
		goto release;
	}

	while (data_pos < cfg->size) {
		/* Read type, instance, length */
		ret = sscanf(cfg->data + data_pos, "%x %x %x%n",
			     &type, &instance, &size, &offset);
		if (ret == 0) {
			/* EOF */
			break;
		} else if (ret != 3) {
			dev_err(dev, "Bad format: failed to parse object\n");
			ret = -EINVAL;
			goto release_mem;
		}
		data_pos += offset;

		if (type == MXT_SPT_USERDATA_T38) {
			ret = sscanf(cfg->data + data_pos, "%x %x %x",
			     &ver[0], &ver[1], &ver[2]);
			dev_info(dev, "controller version:%d.%d.%d file version:%d.%d.%d",
				data->cfg_version[0], data->cfg_version[1],
				data->cfg_version[2], ver[0], ver[1], ver[2]);

			if (force || data->fw_w_no_cfg_update) {
				dev_info(dev, "starting force cfg update\n");
			} else if (data->cfg_version[0] != ver[0]) {
				dev_info(dev, "cfg major versions do not match\n");
				ret = -EINVAL;
				goto release_mem;
			} else if (data->cfg_version[1] > ver[1]) {
				dev_info(dev, "configuration is up-to-date\n");
				ret = -EINVAL;
				goto release_mem;
			} else if (data->cfg_version[1] == ver[1]) {
				if (data->cfg_version[2] >= ver[2]) {
					dev_info(dev, "configuration is up-to-date\n");
					ret = -EINVAL;
					goto release_mem;
				}
			} else {
				dev_info(dev, "starting cfg update\n");
			}
		}

		object = mxt_get_object(data, type);
		if (!object) {
			/* Skip object */
			for (i = 0; i < size; i++) {
				ret = sscanf(cfg->data + data_pos, "%hhx%n",
					     &val,
					     &offset);
				data_pos += offset;
			}
			continue;
		}

		if (size > mxt_obj_size(object)) {
			/* Either we are in fallback mode due to wrong
			 * config or config from a later fw version,
			 * or the file is corrupt or hand-edited */
			dev_warn(dev, "Discarding %u byte(s) in T%u\n",
				 size - mxt_obj_size(object), type);
		} else if (mxt_obj_size(object) > size) {
			/* If firmware is upgraded, new bytes may be added to
			 * end of objects. It is generally forward compatible
			 * to zero these bytes - previous behaviour will be
			 * retained. However this does invalidate the CRC and
			 * will force fallback mode until the configuration is
			 * updated. We warn here but do nothing else - the
			 * malloc has zeroed the entire configuration. */
			dev_warn(dev, "Zeroing %u byte(s) in T%d\n",
				 mxt_obj_size(object) - size, type);
		}

		if (instance >= mxt_obj_instances(object)) {
			dev_err(dev, "Object instances exceeded!\n");
			ret = -EINVAL;
			goto release_mem;
		}

		reg = object->start_address + mxt_obj_size(object) * instance;

		for (i = 0; i < size; i++) {
			ret = sscanf(cfg->data + data_pos, "%hhx%n",
				     &val,
				     &offset);
			if (ret != 1) {
				dev_err(dev, "Bad format in T%d\n", type);
				ret = -EINVAL;
				goto release_mem;
			}
			data_pos += offset;

			if (i > mxt_obj_size(object))
				continue;

			byte_offset = reg + i - cfg_start_ofs;

			if ((byte_offset >= 0)
			    && (byte_offset <= config_mem_size)) {
				*(config_mem + byte_offset) = val;
			} else {
				dev_err(dev, "Bad object: reg:%d, T%d, ofs=%d\n",
					reg, object->type, byte_offset);
				ret = -EINVAL;
				goto release_mem;
			}
		}
	}

	/* calculate crc of the received configs (not the raw config file) */
	if (data->T7_address < cfg_start_ofs) {
		dev_err(dev, "Bad T7 address, T7addr = %x, config offset %x\n",
			data->T7_address, cfg_start_ofs);
		ret = 0;
		goto release_mem;
	}

	calculated_crc = mxt_calculate_crc(config_mem,
					   data->T7_address - cfg_start_ofs,
					   config_mem_size);

	if (config_crc > 0 && (config_crc != calculated_crc))
		dev_warn(dev, "Config CRC error, calculated=%06X, file=%06X\n",
			 calculated_crc, config_crc);

	/* Write configuration as blocks */
	byte_offset = 0;
	while (byte_offset < config_mem_size) {
		size = config_mem_size - byte_offset;

		if (size > MXT_MAX_BLOCK_WRITE)
			size = MXT_MAX_BLOCK_WRITE;

		ret = __mxt_write_reg(data->client,
				      cfg_start_ofs + byte_offset,
				      size, config_mem + byte_offset);
		if (ret != 0) {
			dev_err(dev, "Config write error, ret=%d\n", ret);
			goto release_mem;
		}

		byte_offset += size;
	}

	mxt_update_crc(data, MXT_COMMAND_BACKUPNV, MXT_BACKUP_VALUE);

	ret = mxt_check_retrigen(data);
	if (ret)
		goto release_mem;

	ret = mxt_soft_reset(data);
	if (ret)
		goto release_mem;

	dev_info(dev, "Config written\n");

	/* T7 config may have changed */
	mxt_init_t7_power_cfg(data);

	mxt_update_cfg_version(data);

	/* update resolution if needed */
	if (data->T9_reportid_min) {
		ret = mxt_update_t9_resolution(data);
		if (ret)
			goto release_mem;
	} else if (data->T100_reportid_min) {
		ret = mxt_update_t100_resolution(data);
		if (ret)
			goto release_mem;
	} else {
		dev_warn(dev, "No touch object detected\n");
	}

release_mem:
	kfree(config_mem);
release:
	release_firmware(cfg);
report_enable:
	data->enable_reporting = true;

	return ret;
}

static int mxt_set_t7_power_cfg(struct mxt_data *data, u8 sleep)
{
	struct device *dev = &data->client->dev;
	int error;
	struct t7_config *new_config;
	struct t7_config deepsleep = { .active = 0, .idle = 0 };

	if (sleep == MXT_POWER_CFG_DEEPSLEEP)
		new_config = &deepsleep;
	else
		new_config = &data->t7_cfg;

	error = __mxt_write_reg(data->client, data->T7_address,
			sizeof(data->t7_cfg),
			new_config);
	if (error)
		return error;

	dev_dbg(dev, "Set T7 ACTV:%d IDLE:%d\n",
		new_config->active, new_config->idle);

	return 0;
}

static int mxt_init_t7_power_cfg(struct mxt_data *data)
{
	struct device *dev = &data->client->dev;
	int error;
	bool retry = false;

recheck:
	error = __mxt_read_reg(data->client, data->T7_address,
				sizeof(data->t7_cfg), &data->t7_cfg);
	if (error)
		return error;

	if (data->t7_cfg.active == 0 || data->t7_cfg.idle == 0) {
		if (!retry) {
			dev_info(dev, "T7 cfg zero, resetting\n");
			mxt_soft_reset(data);
			retry = true;
			goto recheck;
		} else {
		    dev_dbg(dev, "T7 cfg zero after reset, overriding\n");
		    data->t7_cfg.active = 20;
		    data->t7_cfg.idle = 100;
		    return mxt_set_t7_power_cfg(data, MXT_POWER_CFG_RUN);
		}
	} else {
		dev_info(dev, "Initialised power cfg: ACTV %d, IDLE %d\n",
				data->t7_cfg.active, data->t7_cfg.idle);
		return 0;
	}
}

static int mxt_acquire_irq(struct mxt_data *data)
{
	int error;

	enable_irq(data->irq);

	if (data->use_retrigen_workaround) {
		error = mxt_process_messages_until_invalid(data);
		if (error)
			return error;
	}

	return 0;
}

static void mxt_free_input_device(struct mxt_data *data)
{
	if (data->input_dev) {
		input_unregister_device(data->input_dev);
		data->input_dev = NULL;
	}
}

static void mxt_free_object_table(struct mxt_data *data)
{
	mxt_debug_msg_remove(data);

	kfree(data->raw_info_block);
	data->object_table = NULL;
	data->info = NULL;
	data->raw_info_block = NULL;
	kfree(data->msg_buf);
	data->msg_buf = NULL;

	mxt_free_input_device(data);

	data->enable_reporting = false;
	data->T5_address = 0;
	data->T5_msg_size = 0;
	data->T6_reportid = 0;
	data->T7_address = 0;
	data->T9_reportid_min = 0;
	data->T9_reportid_max = 0;
	data->T15_reportid_min = 0;
	data->T15_reportid_max = 0;
	data->T18_address = 0;
	data->T19_reportid = 0;
	data->T42_reportid_min = 0;
	data->T42_reportid_max = 0;
	data->T44_address = 0;
	data->T48_reportid = 0;
	data->T63_reportid_min = 0;
	data->T63_reportid_max = 0;
	data->T100_reportid_min = 0;
	data->T100_reportid_max = 0;
	data->max_reportid = 0;
}

static int mxt_parse_object_table(struct mxt_data *data)
{
	struct i2c_client *client = data->client;
	int i;
	u8 reportid;
	u16 end_address;

	/* Valid Report IDs start counting from 1 */
	reportid = 1;
	data->mem_size = 0;
	for (i = 0; i < data->info->object_num; i++) {
		struct mxt_object *object = data->object_table + i;
		u8 min_id, max_id;

		le16_to_cpus(&object->start_address);

		if (object->num_report_ids) {
			min_id = reportid;
			reportid += object->num_report_ids *
					mxt_obj_instances(object);
			max_id = reportid - 1;
		} else {
			min_id = 0;
			max_id = 0;
		}

		dev_dbg(&data->client->dev,
			"T%u Start:%u Size:%u Instances:%u Report IDs:%u-%u\n",
			object->type, object->start_address,
			mxt_obj_size(object), mxt_obj_instances(object),
			min_id, max_id);

		switch (object->type) {
		case MXT_GEN_MESSAGE_T5:
			if (data->info->family_id == 0x80) {
				/* On mXT224 read and discard unused CRC byte
				 * otherwise DMA reads are misaligned */
				data->T5_msg_size = mxt_obj_size(object);
			} else {
				/* CRC not enabled, so skip last byte */
				data->T5_msg_size = mxt_obj_size(object) - 1;
			}
			data->T5_address = object->start_address;
		case MXT_GEN_COMMAND_T6:
			data->T6_reportid = min_id;
			data->T6_address = object->start_address;
			break;
		case MXT_GEN_POWER_T7:
			data->T7_address = object->start_address;
			break;
		case MXT_TOUCH_MULTI_T9:
			/* Only handle messages from first T9 instance */
			data->T9_reportid_min = min_id;
			data->T9_reportid_max = min_id +
						object->num_report_ids - 1;
			data->num_touchids = object->num_report_ids;
			break;
		case MXT_TOUCH_KEYARRAY_T15:
			data->T15_reportid_min = min_id;
			data->T15_reportid_max = max_id;
			break;
		case MXT_SPT_COMMSCONFIG_T18:
			data->T18_address = object->start_address;
			break;
		case MXT_PROCI_TOUCHSUPPRESSION_T42:
			data->T42_reportid_min = min_id;
			data->T42_reportid_max = max_id;
			break;
		case MXT_SPT_MESSAGECOUNT_T44:
			data->T44_address = object->start_address;
			break;
		case MXT_SPT_GPIOPWM_T19:
			data->T19_reportid = min_id;
			break;
		case MXT_PROCG_NOISESUPPRESSION_T48:
			data->T48_reportid = min_id;
			break;
		case MXT_PROCI_ACTIVE_STYLUS_T63:
			/* Only handle messages from first T63 instance */
			data->T63_reportid_min = min_id;
			data->T63_reportid_max = min_id;
			data->num_stylusids = 1;
			break;
		case MXT_TOUCH_MULTITOUCHSCREEN_T100:
			data->T100_reportid_min = min_id;
			data->T100_reportid_max = max_id;
			/* first two report IDs reserved */
			data->num_touchids = object->num_report_ids - 2;
			break;
		}

		end_address = object->start_address
			+ mxt_obj_size(object) * mxt_obj_instances(object) - 1;

		if (end_address >= data->mem_size)
			data->mem_size = end_address + 1;
	}

	/* Store maximum reportid */
	data->max_reportid = reportid;

	/* If T44 exists, T5 position has to be directly after */
	if (data->T44_address && (data->T5_address != data->T44_address + 1)) {
		dev_err(&client->dev, "Invalid T44 position\n");
		return -EINVAL;
	}

	data->msg_buf = kcalloc(data->max_reportid,
				data->T5_msg_size, GFP_KERNEL);
	if (!data->msg_buf) {
		dev_err(&client->dev, "Failed to allocate message buffer\n");
		return -ENOMEM;
	}

	return 0;
}

static int mxt_read_info_block(struct mxt_data *data)
{
	struct i2c_client *client = data->client;
	int error;
	u16 size;
	void *buf;
	uint8_t num_objects;
	u32 calculated_crc;
	u8 *crc_ptr;

	/* If info block already allocated, free it */
	if (data->raw_info_block != NULL)
		mxt_free_object_table(data);

	/* Read 7-byte ID information block starting at address 0 */
	size = sizeof(struct mxt_info);
	buf = kzalloc(size, GFP_KERNEL);
	if (!buf) {
		dev_err(&client->dev, "Failed to allocate memory\n");
		return -ENOMEM;
	}

	error = __mxt_read_reg(client, 0, size, buf);
	if (error)
		goto err_free_mem;

	/* Resize buffer to give space for rest of info block */
	num_objects = ((struct mxt_info *)buf)->object_num;
	size += (num_objects * sizeof(struct mxt_object))
		+ MXT_INFO_CHECKSUM_SIZE;

	buf = krealloc(buf, size, GFP_KERNEL);
	if (!buf) {
		dev_err(&client->dev, "Failed to allocate memory\n");
		error = -ENOMEM;
		goto err_free_mem;
	}

	/* Read rest of info block */
	error = __mxt_read_reg(client, MXT_OBJECT_START,
			       size - MXT_OBJECT_START,
			       buf + MXT_OBJECT_START);
	if (error)
		goto err_free_mem;

	/* Extract & calculate checksum */
	crc_ptr = buf + size - MXT_INFO_CHECKSUM_SIZE;
	data->info_crc = crc_ptr[0] | (crc_ptr[1] << 8) | (crc_ptr[2] << 16);

	calculated_crc = mxt_calculate_crc(buf, 0,
					   size - MXT_INFO_CHECKSUM_SIZE);

	/* CRC mismatch can be caused by data corruption due to I2C comms
	 * issue or else device is not using Object Based Protocol */
	if ((data->info_crc == 0) || (data->info_crc != calculated_crc)) {
		dev_err(&client->dev,
			"Info Block CRC error calculated=0x%06X read=0x%06X\n",
			data->info_crc, calculated_crc);
		if (!data->pdata->ignore_crc)
			goto err_free_mem;
	}

	/* Save pointers in device data structure */
	data->raw_info_block = buf;
	data->info = (struct mxt_info *)buf;
	data->object_table = (struct mxt_object *)(buf + MXT_OBJECT_START);

	/* Parse object table information */
	error = mxt_parse_object_table(data);
	if (error) {
		dev_err(&client->dev, "Error %d reading object table\n", error);
		goto err_free_obj_table;
	}

	error = mxt_update_cfg_version(data);
	if (error)
		goto err_free_obj_table;

	dev_info(&client->dev,
		 "Family: %u Variant: %u Firmware V%u.%u.%02X Objects: %u cfg version: %d.%d.%d\n",
		 data->info->family_id, data->info->variant_id,
		 data->info->version >> 4, data->info->version & 0xf,
		 data->info->build, data->info->object_num,
		 data->cfg_version[0], data->cfg_version[1],
		data->cfg_version[2]);


	return 0;

err_free_obj_table:
	mxt_free_object_table(data);
err_free_mem:
	kfree(buf);
	return error;
}

static int mxt_pinctrl_init(struct mxt_data *data)
{
	int error;

	/* Get pinctrl if target uses pinctrl */
	data->ts_pinctrl = devm_pinctrl_get((&data->client->dev));
	if (IS_ERR_OR_NULL(data->ts_pinctrl)) {
		dev_dbg(&data->client->dev,
			"Device does not use pinctrl\n");
		error = PTR_ERR(data->ts_pinctrl);
		data->ts_pinctrl = NULL;
		return error;
	}

	data->gpio_state_active
		= pinctrl_lookup_state(data->ts_pinctrl, "pmx_ts_active");
	if (IS_ERR_OR_NULL(data->gpio_state_active)) {
		dev_dbg(&data->client->dev,
			"Can not get ts default pinstate\n");
		error = PTR_ERR(data->gpio_state_active);
		data->ts_pinctrl = NULL;
		return error;
	}

	data->gpio_state_suspend
		= pinctrl_lookup_state(data->ts_pinctrl, "pmx_ts_suspend");
	if (IS_ERR_OR_NULL(data->gpio_state_suspend)) {
		dev_dbg(&data->client->dev,
			"Can not get ts sleep pinstate\n");
		error = PTR_ERR(data->gpio_state_suspend);
		data->ts_pinctrl = NULL;
		return error;
	}

	return 0;
}

static int mxt_pinctrl_select(struct mxt_data *data, bool on)
{
	struct pinctrl_state *pins_state;
	int error;

	pins_state = on ? data->gpio_state_active
		: data->gpio_state_suspend;
	if (!IS_ERR_OR_NULL(pins_state)) {
		error = pinctrl_select_state(data->ts_pinctrl, pins_state);
		if (error) {
			dev_err(&data->client->dev,
				"can not set %s pins\n",
				on ? "pmx_ts_active" : "pmx_ts_suspend");
			return error;
		}
	} else {
		dev_err(&data->client->dev,
			"not a valid '%s' pinstate\n",
				on ? "pmx_ts_active" : "pmx_ts_suspend");
	}

	return 0;
}

static int mxt_gpio_enable(struct mxt_data *data, bool enable)
{
	const struct mxt_platform_data *pdata = data->pdata;
	int error;

	if (data->ts_pinctrl) {
		error = mxt_pinctrl_select(data, enable);
		if (error < 0)
			return error;
	}

	if (enable) {
		if (gpio_is_valid(pdata->gpio_irq)) {
			error = gpio_request(pdata->gpio_irq,
				"maxtouch_gpio_irq");
			if (error) {
				dev_err(&data->client->dev,
					"unable to request %d gpio(%d)\n",
					pdata->gpio_irq, error);
				return error;
			}
			error = gpio_direction_input(pdata->gpio_irq);
			if (error) {
				dev_err(&data->client->dev,
					"unable to set dir for %d gpio(%d)\n",
					data->pdata->gpio_irq, error);
				goto err_free_irq;
			}

		} else {
			dev_err(&data->client->dev, "irq gpio not provided\n");
			return -EINVAL;
		}

		if (gpio_is_valid(pdata->gpio_reset)) {
			error = gpio_request(pdata->gpio_reset,
				"maxtouch_gpio_reset");
			if (error) {
				dev_err(&data->client->dev,
					"unable to request %d gpio(%d)\n",
					pdata->gpio_reset, error);
				goto err_free_irq;
			}
			error = gpio_direction_output(pdata->gpio_reset, 0);
			if (error) {
				dev_err(&data->client->dev,
					"unable to set dir for %d gpio(%d)\n",
					pdata->gpio_reset, error);
				goto err_free_reset;
			}
		} else {
			dev_err(&data->client->dev,
				"reset gpio not provided\n");
			goto err_free_irq;
		}

		if (gpio_is_valid(pdata->gpio_i2cmode)) {
			error = gpio_request(pdata->gpio_i2cmode,
				"maxtouch_gpio_i2cmode");
			if (error) {
				dev_err(&data->client->dev,
					"unable to request %d gpio (%d)\n",
					pdata->gpio_i2cmode, error);
				goto err_free_reset;
			}
			error = gpio_direction_output(pdata->gpio_i2cmode, 1);
			if (error) {
				dev_err(&data->client->dev,
					"unable to set dir for %d gpio (%d)\n",
					pdata->gpio_i2cmode, error);
				goto err_free_i2cmode;
			}
		} else {
			dev_info(&data->client->dev,
				"i2cmode gpio is not used\n");
		}
	} else {
		if (gpio_is_valid(pdata->gpio_irq))
			gpio_free(pdata->gpio_irq);

		if (gpio_is_valid(pdata->gpio_reset)) {
			gpio_free(pdata->gpio_reset);
		}

		if (gpio_is_valid(pdata->gpio_i2cmode)) {
			gpio_set_value(pdata->gpio_i2cmode, 0);
			gpio_free(pdata->gpio_i2cmode);
		}
	}

	return 0;

err_free_i2cmode:
	if (gpio_is_valid(pdata->gpio_i2cmode)) {
		gpio_set_value(pdata->gpio_i2cmode, 0);
		gpio_free(pdata->gpio_i2cmode);
	}
err_free_reset:
	if (gpio_is_valid(pdata->gpio_reset)) {
		gpio_free(pdata->gpio_reset);
	}
err_free_irq:
	if (gpio_is_valid(pdata->gpio_irq))
		gpio_free(pdata->gpio_irq);
	return error;
}

static int mxt_regulator_enable(struct mxt_data *data)
{
	int error;

	if (!data->use_regulator)
		return 0;

	gpio_set_value(data->pdata->gpio_reset, 0);

	error = regulator_enable(data->reg_vdd);
	if (error) {
		dev_err(&data->client->dev,
			"vdd enable failed, error=%d\n", error);
		return error;
	}
	error = regulator_enable(data->reg_avdd);
	if (error) {
		dev_err(&data->client->dev,
			"avdd enable failed, error=%d\n", error);
		goto err_dis_vdd;
	}

	if (!IS_ERR(data->reg_xvdd)) {
		error = regulator_enable(data->reg_xvdd);
		if (error) {
			dev_err(&data->client->dev,
				"xvdd enable failed, error=%d\n", error);
			goto err_dis_avdd;
		}
	}
	msleep(MXT_REGULATOR_DELAY);

	INIT_COMPLETION(data->bl_completion);
	gpio_set_value(data->pdata->gpio_reset, 1);
	mxt_wait_for_completion(data, &data->bl_completion, MXT_POWERON_DELAY);

	return 0;

err_dis_avdd:
	regulator_disable(data->reg_avdd);
err_dis_vdd:
	regulator_disable(data->reg_vdd);
	return error;
}

static void mxt_regulator_disable(struct mxt_data *data)
{
	regulator_disable(data->reg_vdd);
	regulator_disable(data->reg_avdd);
	if (!IS_ERR(data->reg_xvdd))
		regulator_disable(data->reg_xvdd);
}

static int mxt_regulator_configure(struct mxt_data *data, bool state)
{
	struct device *dev = &data->client->dev;
	int error = 0;

	/* According to maXTouch power sequencing specification, RESET line
	 * must be kept low until some time after regulators come up to
	 * voltage */
	if (!data->pdata->gpio_reset) {
		dev_warn(dev, "Must have reset GPIO to use regulator support\n");
		return 0;
	}

	if (!state)
		goto deconfig;

	data->reg_vdd = regulator_get(dev, "vdd");
	if (IS_ERR(data->reg_vdd)) {
		error = PTR_ERR(data->reg_vdd);
		dev_err(dev, "Error %d getting vdd regulator\n", error);
		return error;
	}

	if (regulator_count_voltages(data->reg_vdd) > 0) {
		error = regulator_set_voltage(data->reg_vdd,
				MXT_VDD_VTG_MIN_UV, MXT_VDD_VTG_MAX_UV);
		if (error) {
			dev_err(&data->client->dev,
				"vdd set_vtg failed err=%d\n", error);
			goto fail_put_vdd;
		}
	}

	data->reg_avdd = regulator_get(dev, "avdd");
	if (IS_ERR(data->reg_avdd)) {
		error = PTR_ERR(data->reg_avdd);
		dev_err(dev, "Error %d getting avdd regulator\n", error);
		goto fail_put_vdd;
	}

	if (regulator_count_voltages(data->reg_avdd) > 0) {
		error = regulator_set_voltage(data->reg_avdd,
				MXT_AVDD_VTG_MIN_UV, MXT_AVDD_VTG_MAX_UV);
		if (error) {
			dev_err(&data->client->dev,
				"avdd set_vtg failed err=%d\n", error);
			goto fail_put_avdd;
		}
	}

	data->reg_xvdd = regulator_get(dev, "xvdd");
	if (IS_ERR(data->reg_xvdd)) {
		dev_info(dev, "xvdd regulator is not used\n");
	} else {
		if (regulator_count_voltages(data->reg_xvdd) > 0) {
			error = regulator_set_voltage(data->reg_xvdd,
				MXT_XVDD_VTG_MIN_UV, MXT_XVDD_VTG_MAX_UV);
			if (error)
				dev_err(&data->client->dev,
					"xvdd set_vtg failed err=%d\n", error);
		}
	}

	data->use_regulator = true;

	dev_dbg(dev, "Initialised regulators\n");
	return 0;

deconfig:
	if (!IS_ERR(data->reg_xvdd))
		regulator_put(data->reg_xvdd);
fail_put_avdd:
	regulator_put(data->reg_avdd);
fail_put_vdd:
	regulator_put(data->reg_vdd);
	return error;
}


#if defined(CONFIG_SECURE_TOUCH)
static void mxt_secure_touch_stop(struct mxt_data *data, int blocking)
{
	if (atomic_read(&data->st_enabled)) {
		atomic_set(&data->st_pending_irqs, -1);
		mxt_secure_touch_notify(data);
		if (blocking)
			wait_for_completion_interruptible(&data->st_powerdown);
	}
}
#else
static void mxt_secure_touch_stop(struct mxt_data *data, int blocking)
{
}
#endif

static void mxt_start(struct mxt_data *data)
{
	if (!data->suspended || data->in_bootloader)
		return;

	mxt_secure_touch_stop(data, 1);

	/* enable gpios */
	mxt_gpio_enable(data, true);

	/* enable regulators */
	mxt_regulator_enable(data);

	/* Discard any messages still in message buffer from before
	 * chip went to sleep */
	mxt_process_messages_until_invalid(data);

	mxt_set_t7_power_cfg(data, MXT_POWER_CFG_RUN);

	/* Recalibrate since chip has been in deep sleep */
	mxt_t6_command(data, MXT_COMMAND_CALIBRATE, 1, false);

	mxt_acquire_irq(data);
	data->enable_reporting = true;
	data->suspended = false;
}

static void mxt_reset_slots(struct mxt_data *data)
{
	struct input_dev *input_dev = data->input_dev;
	unsigned int num_mt_slots;
	int id;

	num_mt_slots = data->num_touchids + data->num_stylusids;

	for (id = 0; id < num_mt_slots; id++) {
		input_mt_slot(input_dev, id);
		input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, 0);
	}

	mxt_input_sync(input_dev);
}

static void mxt_stop(struct mxt_data *data)
{
	if (data->suspended || data->in_bootloader)
		return;

	mxt_secure_touch_stop(data, 1);

	data->enable_reporting = false;
	disable_irq(data->irq);

	/* put in deep sleep */
	mxt_set_t7_power_cfg(data, MXT_POWER_CFG_DEEPSLEEP);

	/* disable regulators */
	mxt_regulator_disable(data);

	/* disable gpios */
	mxt_gpio_enable(data, false);

	mxt_reset_slots(data);
	data->suspended = true;
}


static int mxt_input_open(struct input_dev *dev)
{
	struct mxt_data *data = input_get_drvdata(dev);

	mxt_start(data);

	return 0;
}

static void mxt_input_close(struct input_dev *dev)
{
	struct mxt_data *data = input_get_drvdata(dev);

	mxt_stop(data);
}

static int mxt_create_input_dev(struct mxt_data *data)
{
	struct device *dev = &data->client->dev;
	struct input_dev *input_dev;
	int error;
	unsigned int num_mt_slots;
	int i;

	if (data->T9_reportid_min) {
		error = mxt_update_t9_resolution(data);
		if (error) {
			dev_err(dev, "update resolution failed\n");
			return error;
		}
	} else if (data->T100_reportid_min) {
		error = mxt_update_t100_resolution(data);
		if (error) {
			dev_err(dev, "update resolution failed\n");
			return error;
		}
	} else {
		dev_warn(dev, "No touch object detected\n");
	}

	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(dev, "Failed to allocate memory\n");
		return -ENOMEM;
	}

	input_dev->name = "Atmel maXTouch Touchscreen";
	input_dev->phys = data->phys;
	input_dev->id.bustype = BUS_I2C;
	input_dev->dev.parent = dev;
	input_dev->open = mxt_input_open;
	input_dev->close = mxt_input_close;

	__set_bit(EV_ABS, input_dev->evbit);
	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(BTN_TOUCH, input_dev->keybit);
	__set_bit(INPUT_PROP_DIRECT, input_dev->propbit);

	num_mt_slots = data->num_touchids + data->num_stylusids;
	error = input_mt_init_slots(input_dev, num_mt_slots, 0);
	if (error) {
		dev_err(dev, "Error %d initialising slots\n", error);
		goto err_free_mem;
	}

	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR,
			     0, MXT_MAX_AREA, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_X,
			     data->pdata->disp_minx, data->pdata->disp_maxx,
				0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y,
			     data->pdata->disp_miny, data->pdata->disp_maxy,
				0, 0);
	input_set_abs_params(input_dev, ABS_MT_PRESSURE,
			     0, 255, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_ORIENTATION,
			     0, 255, 0, 0);

	/* For T63 active stylus */
	if (data->T63_reportid_min) {
		input_set_capability(input_dev, EV_KEY, BTN_STYLUS);
		input_set_capability(input_dev, EV_KEY, BTN_STYLUS2);
		input_set_abs_params(input_dev, ABS_MT_TOOL_TYPE,
			0, MT_TOOL_MAX, 0, 0);
	}

	/* For T15 key array */
	if (data->pdata->key_codes && data->T15_reportid_min) {
		data->t15_keystatus = 0;

		for (i = 0; i < data->pdata->t15_num_keys; i++)
			input_set_capability(input_dev, EV_KEY,
				     data->pdata->t15_keymap[i]);
	}

	input_set_drvdata(input_dev, data);

	error = input_register_device(input_dev);
	if (error) {
		dev_err(dev, "Error %d registering input device\n", error);
		goto err_free_mem;
	}

	data->input_dev = input_dev;

	return 0;

err_free_mem:
	input_free_device(input_dev);
	return error;
}

static int mxt_configure_objects(struct mxt_data *data)
{
	struct i2c_client *client = data->client;
	int error;

	error = mxt_debug_msg_init(data);
	if (error)
		return error;

	error = mxt_init_t7_power_cfg(data);
	if (error)
		dev_dbg(&client->dev, "Failed to initialize power cfg\n");

	return 0;
}

#ifdef CONFIG_OF
static int mxt_get_dt_coords(struct device *dev, char *name,
				struct mxt_platform_data *pdata)
{
	u32 coords[MXT_COORDS_ARR_SIZE];
	struct property *prop;
	struct device_node *np = dev->of_node;
	size_t coords_size;
	int error;

	prop = of_find_property(np, name, NULL);
	if (!prop)
		return -EINVAL;
	if (!prop->value)
		return -ENODATA;

	coords_size = prop->length / sizeof(u32);
	if (coords_size != MXT_COORDS_ARR_SIZE) {
		dev_err(dev, "invalid %s\n", name);
		return -EINVAL;
	}

	error = of_property_read_u32_array(np, name, coords, coords_size);
	if (error && (error != -EINVAL)) {
		dev_err(dev, "Unable to read %s\n", name);
		return error;
	}

	if (strcmp(name, "atmel,panel-coords") == 0) {
		pdata->panel_minx = coords[0];
		pdata->panel_miny = coords[1];
		pdata->panel_maxx = coords[2];
		pdata->panel_maxy = coords[3];
	} else if (strcmp(name, "atmel,display-coords") == 0) {
		pdata->disp_minx = coords[0];
		pdata->disp_miny = coords[1];
		pdata->disp_maxx = coords[2];
		pdata->disp_maxy = coords[3];
	} else {
		dev_err(dev, "unsupported property %s\n", name);
		return -EINVAL;
	}

	return 0;
}

static int mxt_search_fw_name(struct mxt_data *data)
{
	struct device *dev = &data->client->dev;
	struct device_node *np = dev->of_node, *temp;
	u32 temp_val; size_t len;
	int rc;

	data->fw_name[0] = '\0';

	if (data->in_bootloader)
		return 0;

	for_each_child_of_node(np, temp) {
		rc = of_property_read_u32(temp, "atmel,version", &temp_val);
		if (rc) {
			dev_err(dev, "Unable to read controller version\n");
			return rc;
		}

		if (temp_val != data->info->version)
			continue;

		rc = of_property_read_u32(temp, "atmel,build", &temp_val);
		if (rc) {
			dev_err(dev, "Unable to read build id\n");
			return rc;
		}

		if (temp_val != data->info->build)
			continue;

		rc = of_property_read_string(temp, "atmel,fw-name",
					&data->pdata->fw_name);
		if (rc && (rc != -EINVAL)) {
			dev_err(dev, "Unable to read fw name\n");
			return rc;
		}

		dev_dbg(dev, "fw name found(%s)\n", data->pdata->fw_name);

		if (data->pdata->fw_name) {
			len = strlen(data->pdata->fw_name);
			if (len > MXT_NAME_MAX_LEN - 1) {
				dev_err(dev, "Invalid firmware name\n");
				return -EINVAL;
			}
			strlcpy(data->fw_name, data->pdata->fw_name, len + 1);
		}
	}

	return 0;
}

static int mxt_parse_dt(struct device *dev, struct mxt_platform_data *pdata)
{
	int error;
	u32 temp_val;
	struct device_node *np = dev->of_node;
	struct property *prop;

	error = mxt_get_dt_coords(dev, "atmel,panel-coords", pdata);
	if (error)
		return error;

	error = mxt_get_dt_coords(dev, "atmel,display-coords", pdata);
	if (error)
		return error;

	pdata->cfg_name = MXT_GEN_CFG;
	error = of_property_read_string(np, "atmel,cfg-name", &pdata->cfg_name);
	if (error && (error != -EINVAL)) {
		dev_err(dev, "Unable to read cfg name\n");
		return error;
	}

	/* reset, irq gpio info */
	pdata->gpio_reset = of_get_named_gpio_flags(np, "atmel,reset-gpio",
				0, &temp_val);
	pdata->resetflags = temp_val;
	pdata->gpio_irq = of_get_named_gpio_flags(np, "atmel,irq-gpio",
				0, &temp_val);
	pdata->irqflags = temp_val;
	pdata->gpio_i2cmode = of_get_named_gpio_flags(np, "atmel,i2cmode-gpio",
				0, &temp_val);

	pdata->ignore_crc = of_property_read_bool(np, "atmel,ignore-crc");

	error = of_property_read_u32(np, "atmel,bl-addr", &temp_val);
	if (error && (error != -EINVAL))
		dev_err(dev, "Unable to read bootloader address\n");
	else if (error != -EINVAL)
		pdata->bl_addr = (u8) temp_val;

	/* keycodes for keyarray object */
	prop = of_find_property(np, "atmel,key-codes", NULL);
	if (prop) {
		pdata->key_codes = devm_kzalloc(dev,
				sizeof(int) * MXT_KEYARRAY_MAX_KEYS,
				GFP_KERNEL);
		if (!pdata->key_codes)
			return -ENOMEM;
		if ((prop->length/sizeof(u32)) == MXT_KEYARRAY_MAX_KEYS) {
			error = of_property_read_u32_array(np,
				"atmel,key-codes", pdata->key_codes,
				MXT_KEYARRAY_MAX_KEYS);
			if (error) {
				dev_err(dev, "Unable to read key codes\n");
				return error;
			}
		} else
			return -EINVAL;
	}

	return 0;
}
#else
static int mxt_parse_dt(struct device *dev, struct mxt_platform_data *pdata)
{
	return -ENODEV;
}

static int mxt_search_fw_name(struct mxt_data *data)
{

	return -ENODEV;
}
#endif

static int mxt_initialize(struct mxt_data *data)
{
	struct i2c_client *client = data->client;
	int error;
	bool alt_bootloader_addr = false;
	bool retry = false;

retry_info:
	error = mxt_read_info_block(data);
	if (error) {
retry_bootloader:
		error = mxt_probe_bootloader(data, alt_bootloader_addr);
		if (error) {
			if (alt_bootloader_addr) {
				/* Chip is not in appmode or bootloader mode */
				return error;
			}

			dev_info(&client->dev, "Trying alternate bootloader address\n");
			alt_bootloader_addr = true;
			goto retry_bootloader;
		} else {
			if (retry) {
				dev_err(&client->dev,
						"Could not recover device from "
						"bootloader mode\n");
				/* this is not an error state, we can reflash
				 * from here */
				data->in_bootloader = true;
				goto recover_bootloader;
			}

			/* Attempt to exit bootloader into app mode */
			mxt_send_bootloader_cmd(data, false);
			msleep(MXT_FW_RESET_TIME);
			retry = true;
			goto retry_info;
		}
	}

	error = mxt_check_retrigen(data);
	if (error)
		return error;

	error = mxt_acquire_irq(data);
	if (error)
		return error;

	error = mxt_configure_objects(data);
	if (error)
		return error;

recover_bootloader:
	error = mxt_create_input_dev(data);
	if (error) {
		dev_err(&client->dev, "Failed to create input dev\n");
		return error;
	}

	data->enable_reporting = true;

	error = mxt_search_fw_name(data);
	if (error)
		dev_dbg(&client->dev, "firmware name search fail\n");

	return 0;
}

/* Firmware Version is returned as Major.Minor.Build */
static ssize_t mxt_fw_version_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	return scnprintf(buf, PAGE_SIZE, "%u.%u.%02X\n",
			 data->info->version >> 4, data->info->version & 0xf,
			 data->info->build);
}

/* Hardware Version is returned as FamilyID.VariantID */
static ssize_t mxt_hw_version_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	return scnprintf(buf, PAGE_SIZE, "%u.%u\n",
			data->info->family_id, data->info->variant_id);
}

/* Hardware Version is returned as FamilyID.VariantID */
static ssize_t mxt_cfg_version_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	return scnprintf(buf, PAGE_SIZE, "%u.%u.%u\n",
			data->cfg_version[0],  data->cfg_version[1],
			data->cfg_version[2]);
}

static ssize_t mxt_show_instance(char *buf, int count,
				 struct mxt_object *object, int instance,
				 const u8 *val)
{
	int i;

	if (mxt_obj_instances(object) > 1)
		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "Instance %u\n", instance);

	for (i = 0; i < mxt_obj_size(object); i++)
		count += scnprintf(buf + count, PAGE_SIZE - count,
				"\t[%2u]: %02x (%d)\n", i, val[i], val[i]);
	count += scnprintf(buf + count, PAGE_SIZE - count, "\n");

	return count;
}

static ssize_t mxt_object_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	struct mxt_object *object;
	int count = 0;
	int i, j;
	int error;
	u8 *obuf;

	/* Pre-allocate buffer large enough to hold max sized object. */
	obuf = kmalloc(256, GFP_KERNEL);
	if (!obuf)
		return -ENOMEM;

	error = 0;
	for (i = 0; i < data->info->object_num; i++) {
		object = data->object_table + i;

		if (!mxt_object_readable(object->type))
			continue;

		count += scnprintf(buf + count, PAGE_SIZE - count,
				"T%u:\n", object->type);

		for (j = 0; j < mxt_obj_instances(object); j++) {
			u16 size = mxt_obj_size(object);
			u16 addr = object->start_address + j * size;

			error = __mxt_read_reg(data->client, addr, size, obuf);
			if (error)
				goto done;

			count = mxt_show_instance(buf, count, object, j, obuf);
		}
	}

done:
	kfree(obuf);
	return error ?: count;
}

static int mxt_check_firmware_format(struct device *dev,
				     const struct firmware *fw)
{
	unsigned int pos = 0;
	char c;

	while (pos < fw->size) {
		c = *(fw->data + pos);

		if (c < '0' || (c > '9' && c < 'A') || c > 'F')
			return 0;

		pos++;
	}

	/* To convert file try
	 * xxd -r -p mXTXXX__APP_VX-X-XX.enc > maxtouch.fw */
	dev_err(dev, "Aborting: firmware file must be in binary format\n");

	return -1;
}

static int mxt_load_fw(struct device *dev)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	const struct firmware *fw = NULL;
	unsigned int frame_size;
	unsigned int pos = 0;
	unsigned int retry = 0;
	unsigned int frame = 0;
	int ret;

	ret = request_firmware(&fw, data->fw_name, dev);
	if (ret) {
		dev_err(dev, "Unable to open firmware %s\n", data->fw_name);
		return ret;
	}

	/* Check for incorrect enc file */
	ret = mxt_check_firmware_format(dev, fw);
	if (ret)
		goto release_firmware;

	if (data->suspended) {
		if (data->use_regulator)
			mxt_regulator_enable(data);

		enable_irq(data->irq);
		data->suspended = false;
	}

	if (!data->in_bootloader) {
		/* Change to the bootloader mode */
		data->in_bootloader = true;

		ret = mxt_t6_command(data, MXT_COMMAND_RESET,
				     MXT_BOOT_VALUE, false);
		if (ret)
			goto release_firmware;

		msleep(MXT_RESET_TIME);

		/* At this stage, do not need to scan since we know
		 * family ID */
		ret = mxt_lookup_bootloader_address(data, 0);
		if (ret)
			goto release_firmware;
	} else {
		enable_irq(data->irq);
	}

	mxt_free_object_table(data);
	INIT_COMPLETION(data->bl_completion);

	ret = mxt_check_bootloader(data, MXT_WAITING_BOOTLOAD_CMD, false);
	if (ret) {
		/* Bootloader may still be unlocked from previous update
		 * attempt */
		ret = mxt_check_bootloader(data, MXT_WAITING_FRAME_DATA, false);
		if (ret)
			goto disable_irq;
	} else {
		dev_info(dev, "Unlocking bootloader\n");

		/* Unlock bootloader */
		ret = mxt_send_bootloader_cmd(data, true);
		if (ret)
			goto disable_irq;
	}

	while (pos < fw->size) {
		ret = mxt_check_bootloader(data, MXT_WAITING_FRAME_DATA, true);
		if (ret)
			goto disable_irq;

		frame_size = ((*(fw->data + pos) << 8) | *(fw->data + pos + 1));

		/* Take account of CRC bytes */
		frame_size += 2;

		/* Write one frame to device */
		ret = mxt_bootloader_write(data, fw->data + pos, frame_size);
		if (ret)
			goto disable_irq;

		ret = mxt_check_bootloader(data, MXT_FRAME_CRC_PASS, true);
		if (ret) {
			retry++;

			/* Back off by 20ms per retry */
			msleep(retry * 20);

			if (retry > 20) {
				dev_err(dev, "Retry count exceeded\n");
				goto disable_irq;
			}
		} else {
			retry = 0;
			pos += frame_size;
			frame++;
		}

		if (frame % 50 == 0)
			dev_info(dev, "Sent %d frames, %d/%zd bytes\n",
				 frame, pos, fw->size);
	}

	/* Wait for flash. */
	ret = mxt_wait_for_completion(data, &data->bl_completion,
				MXT_FW_RESET_TIME);
	if (ret)
		goto disable_irq;

	dev_info(dev, "Sent %d frames, %u bytes\n", frame, pos);

	/* Wait for device to reset. Some bootloader versions do not assert
	 * the CHG line after bootloading has finished, so ignore error */
	mxt_wait_for_completion(data, &data->bl_completion,
				MXT_FW_RESET_TIME);

	data->in_bootloader = false;

disable_irq:
	disable_irq(data->irq);
release_firmware:
	release_firmware(fw);
	return ret;
}

static ssize_t mxt_update_fw_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	int error;

	if (data->fw_name[0] == '\0') {
		if (data->in_bootloader)
			dev_info(dev, "Manual update needed\n");
		else
			dev_info(dev, "firmware is up-to-date\n");
		return count;
	}

	error = mxt_load_fw(dev);
	if (error) {
		dev_err(dev, "The firmware update failed(%d)\n", error);
		count = error;
	} else {
		dev_info(dev, "The firmware update succeeded\n");

		msleep(MXT_FW_RESET_TIME);
		data->suspended = false;

		error = mxt_initialize(data);
		if (error)
			return error;

		data->fw_w_no_cfg_update = true;
	}

	return count;
}

static int mxt_update_cfg(struct mxt_data *data, bool force)
{
	int ret;

	if (data->in_bootloader) {
		dev_err(&data->client->dev, "Not in appmode\n");
		return -EINVAL;
	}

	data->enable_reporting = false;

	if (data->suspended) {
		if (data->use_regulator)
			mxt_regulator_enable(data);

		mxt_set_t7_power_cfg(data, MXT_POWER_CFG_RUN);

		mxt_acquire_irq(data);

		data->suspended = false;
	}

	/* load config */
	ret = mxt_load_cfg(data, force);
	if (ret)
		return ret;

	return 0;
}

static ssize_t mxt_update_cfg_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	int ret;

	/* update config */
	ret = mxt_update_cfg(data, false);
	if (ret)
		return ret;

	return count;
}

static ssize_t mxt_force_update_cfg_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	int ret;

	/* update force config */
	ret = mxt_update_cfg(data, true);
	if (ret)
		return ret;

	return count;
}

static ssize_t mxt_debug_enable_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	char c;

	c = data->debug_enabled ? '1' : '0';
	return scnprintf(buf, PAGE_SIZE, "%c\n", c);
}

static ssize_t mxt_debug_notify_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "0\n");
}

static ssize_t mxt_debug_v2_enable_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	int i;

	if (sscanf(buf, "%u", &i) == 1 && i < 2) {
		if (i == 1)
			mxt_debug_msg_enable(data);
		else
			mxt_debug_msg_disable(data);

		return count;
	} else {
		dev_dbg(dev, "debug_enabled write error\n");
		return -EINVAL;
	}
}

static ssize_t mxt_debug_enable_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	int i;

	if (sscanf(buf, "%u", &i) == 1 && i < 2) {
		data->debug_enabled = (i == 1);

		dev_dbg(dev, "%s\n", i ? "debug enabled" : "debug disabled");
		return count;
	} else {
		dev_dbg(dev, "debug_enabled write error\n");
		return -EINVAL;
	}
}

static int mxt_check_mem_access_params(struct mxt_data *data, loff_t off,
				       size_t *count)
{
	if (off >= data->mem_size)
		return -EIO;

	if (off + *count > data->mem_size)
		*count = data->mem_size - off;

	if (*count > MXT_MAX_BLOCK_WRITE)
		*count = MXT_MAX_BLOCK_WRITE;

	return 0;
}

static ssize_t mxt_mem_access_read(struct file *filp, struct kobject *kobj,
	struct bin_attribute *bin_attr, char *buf, loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct mxt_data *data = dev_get_drvdata(dev);
	int ret = 0;

	ret = mxt_check_mem_access_params(data, off, &count);
	if (ret < 0)
		return ret;

	if (count > 0)
		ret = __mxt_read_reg(data->client, off, count, buf);

	return ret == 0 ? count : ret;
}

static ssize_t mxt_mem_access_write(struct file *filp, struct kobject *kobj,
	struct bin_attribute *bin_attr, char *buf, loff_t off,
	size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct mxt_data *data = dev_get_drvdata(dev);
	int ret = 0;

	ret = mxt_check_mem_access_params(data, off, &count);
	if (ret < 0)
		return ret;

	if (count > 0)
		ret = __mxt_write_reg(data->client, off, count, buf);

	return ret == 0 ? count : 0;
}

#if defined(CONFIG_SECURE_TOUCH)

static int mxt_secure_touch_clk_prepare_enable(
		struct mxt_data *data)
{
	int ret;
	ret = clk_prepare_enable(data->iface_clk);
	if (ret) {
		dev_err(&data->client->dev,
			"error on clk_prepare_enable(iface_clk):%d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(data->core_clk);
	if (ret) {
		clk_disable_unprepare(data->iface_clk);
		dev_err(&data->client->dev,
			"error clk_prepare_enable(core_clk):%d\n", ret);
	}
	return ret;
}

static void mxt_secure_touch_clk_disable_unprepare(
		struct mxt_data *data)
{
	clk_disable_unprepare(data->core_clk);
	clk_disable_unprepare(data->iface_clk);
}

static ssize_t mxt_secure_touch_enable_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	return scnprintf(buf, PAGE_SIZE, "%d", atomic_read(&data->st_enabled));
}
/*
 * Accept only "0" and "1" valid values.
 * "0" will reset the st_enabled flag, then wake up the reading process.
 * The bus driver is notified via pm_runtime that it is not required to stay
 * awake anymore.
 * It will also make sure the queue of events is emptied in the controller,
 * in case a touch happened in between the secure touch being disabled and
 * the local ISR being ungated.
 * "1" will set the st_enabled flag and clear the st_pending_irqs flag.
 * The bus driver is requested via pm_runtime to stay awake.
 */
static ssize_t mxt_secure_touch_enable_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	struct device *adapter = data->client->adapter->dev.parent;
	unsigned long value;
	int err = 0;

	if (count > 2)
		return -EINVAL;

	err = kstrtoul(buf, 10, &value);
	if (err != 0)
		return err;

	if (!data->st_initialized)
		return -EIO;

	err = count;

	switch (value) {
	case 0:
		if (atomic_read(&data->st_enabled) == 0)
			break;

		mxt_secure_touch_clk_disable_unprepare(data);
		pm_runtime_put_sync(adapter);
		atomic_set(&data->st_enabled, 0);
		mxt_secure_touch_notify(data);
		mxt_interrupt(data->client->irq, data);
		complete(&data->st_powerdown);
		break;
	case 1:
		if (atomic_read(&data->st_enabled)) {
			err = -EBUSY;
			break;
		}

		if (pm_runtime_get_sync(adapter) < 0) {
			dev_err(&data->client->dev, "pm_runtime_get failed\n");
			err = -EIO;
			break;
		}

		if (mxt_secure_touch_clk_prepare_enable(data) < 0) {
			pm_runtime_put_sync(adapter);
			err = -EIO;
			break;
		}
		INIT_COMPLETION(data->st_powerdown);
		atomic_set(&data->st_enabled, 1);
		synchronize_irq(data->client->irq);
		atomic_set(&data->st_pending_irqs, 0);
		break;
	default:
		dev_err(&data->client->dev, "unsupported value: %lu\n", value);
		err = -EINVAL;
		break;
	}

	return err;
}

static ssize_t mxt_secure_touch_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	int val = 0;

	if (atomic_read(&data->st_enabled) == 0)
		return -EBADF;

	if (atomic_cmpxchg(&data->st_pending_irqs, -1, 0) == -1)
		return -EINVAL;

	if (atomic_cmpxchg(&data->st_pending_irqs, 1, 0) == 1)
		val = 1;

	return scnprintf(buf, PAGE_SIZE, "%u", val);
}

static DEVICE_ATTR(secure_touch_enable, S_IRUGO | S_IWUSR | S_IWGRP ,
			 mxt_secure_touch_enable_show,
			 mxt_secure_touch_enable_store);
static DEVICE_ATTR(secure_touch, S_IRUGO, mxt_secure_touch_show, NULL);
#endif

static ssize_t mxt_fw_name_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	return snprintf(buf, MXT_NAME_MAX_LEN - 1, "%s\n", data->fw_name);
}

static ssize_t mxt_fw_name_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct mxt_data *data = dev_get_drvdata(dev);

	if (size > MXT_NAME_MAX_LEN - 1)
		return -EINVAL;

	strlcpy(data->fw_name, buf, size);
	if (data->fw_name[size-1] == '\n')
		data->fw_name[size-1] = 0;

	return size;
}

static ssize_t mxt_cfg_name_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	return snprintf(buf, MXT_NAME_MAX_LEN - 1, "%s\n", data->cfg_name);
}

static ssize_t mxt_cfg_name_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct mxt_data *data = dev_get_drvdata(dev);

	if (size > MXT_NAME_MAX_LEN - 1)
		return -EINVAL;

	strlcpy(data->cfg_name, buf, size);
	if (data->cfg_name[size-1] == '\n')
		data->cfg_name[size-1] = 0;

	return size;
}

static DEVICE_ATTR(fw_name, S_IWUSR | S_IRUSR,
			mxt_fw_name_show, mxt_fw_name_store);
static DEVICE_ATTR(cfg_name, S_IWUSR | S_IRUSR,
			mxt_cfg_name_show, mxt_cfg_name_store);
static DEVICE_ATTR(fw_version, S_IRUGO, mxt_fw_version_show, NULL);
static DEVICE_ATTR(hw_version, S_IRUGO, mxt_hw_version_show, NULL);
static DEVICE_ATTR(cfg_version, S_IRUGO, mxt_cfg_version_show, NULL);
static DEVICE_ATTR(object, S_IRUGO, mxt_object_show, NULL);
static DEVICE_ATTR(update_fw, S_IWUSR, NULL, mxt_update_fw_store);
static DEVICE_ATTR(update_cfg, S_IWUSR, NULL, mxt_update_cfg_store);
static DEVICE_ATTR(force_update_cfg, S_IWUSR, NULL, mxt_force_update_cfg_store);
static DEVICE_ATTR(debug_v2_enable, S_IWUSR | S_IRUSR, NULL, mxt_debug_v2_enable_store);
static DEVICE_ATTR(debug_notify, S_IRUGO, mxt_debug_notify_show, NULL);
static DEVICE_ATTR(debug_enable, S_IWUSR | S_IRUSR, mxt_debug_enable_show,
		   mxt_debug_enable_store);

static struct attribute *mxt_attrs[] = {
	&dev_attr_fw_name.attr,
	&dev_attr_cfg_name.attr,
	&dev_attr_fw_version.attr,
	&dev_attr_hw_version.attr,
	&dev_attr_cfg_version.attr,
	&dev_attr_object.attr,
	&dev_attr_update_fw.attr,
	&dev_attr_update_cfg.attr,
	&dev_attr_force_update_cfg.attr,
	&dev_attr_debug_enable.attr,
	&dev_attr_debug_v2_enable.attr,
	&dev_attr_debug_notify.attr,
#if defined(CONFIG_SECURE_TOUCH)
	&dev_attr_secure_touch_enable.attr,
	&dev_attr_secure_touch.attr,
#endif
	NULL
};

static const struct attribute_group mxt_attr_group = {
	.attrs = mxt_attrs,
};

#ifdef CONFIG_PM_SLEEP
static int mxt_suspend(struct device *dev)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	struct input_dev *input_dev = data->input_dev;

	mutex_lock(&input_dev->mutex);

	if (input_dev->users)
		mxt_stop(data);

	mutex_unlock(&input_dev->mutex);

	return 0;
}

static int mxt_resume(struct device *dev)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	struct input_dev *input_dev = data->input_dev;

	mxt_secure_touch_stop(data, 1);

	mutex_lock(&input_dev->mutex);

	if (input_dev->users)
		mxt_start(data);

	mutex_unlock(&input_dev->mutex);

	return 0;
}

#if defined(CONFIG_FB)
static int fb_notifier_callback(struct notifier_block *self,
				 unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	int *blank;
	struct mxt_data *mxt_dev_data =
		container_of(self, struct mxt_data, fb_notif);

	if (evdata && evdata->data && mxt_dev_data && mxt_dev_data->client) {
		if (event == FB_EARLY_EVENT_BLANK)
			mxt_secure_touch_stop(mxt_dev_data, 0);
		else if (event == FB_EVENT_BLANK) {
			blank = evdata->data;
			if (*blank == FB_BLANK_UNBLANK)
				mxt_resume(&mxt_dev_data->client->dev);
			else if (*blank == FB_BLANK_POWERDOWN)
				mxt_suspend(&mxt_dev_data->client->dev);
		}
	}

	return 0;
}
#elif defined(CONFIG_HAS_EARLYSUSPEND)
static void mxt_early_suspend(struct early_suspend *h)
{
	struct mxt_data *data = container_of(h, struct mxt_data,
						early_suspend);
	mxt_suspend(&data->client->dev);
}

static void mxt_late_resume(struct early_suspend *h)
{
	struct mxt_data *data = container_of(h, struct mxt_data,
						early_suspend);
	mxt_resume(&data->client->dev);
}
#endif

static const struct dev_pm_ops mxt_pm_ops = {
#if (!defined(CONFIG_FB) && !defined(CONFIG_HAS_EARLYSUSPEND))
	.suspend	= mxt_suspend,
	.resume		= mxt_resume,
#endif
};
#endif

#if defined(CONFIG_SECURE_TOUCH)
static void mxt_secure_touch_init(struct mxt_data *data)
{
	int ret = 0;
	data->st_initialized = 0;
	init_completion(&data->st_powerdown);
	/* Get clocks */
	data->core_clk = clk_get(&data->client->dev, "core_clk");
	if (IS_ERR(data->core_clk)) {
		ret = PTR_ERR(data->core_clk);
		dev_err(&data->client->dev,
			"%s: error on clk_get(core_clk):%d\n", __func__, ret);
		return;
	}

	data->iface_clk = clk_get(&data->client->dev, "iface_clk");
	if (IS_ERR(data->iface_clk)) {
		ret = PTR_ERR(data->iface_clk);
		dev_err(&data->client->dev,
			"%s: error on clk_get(iface_clk):%d\n", __func__, ret);
		goto err_iface_clk;
	}

	data->st_initialized = 1;
	return;

err_iface_clk:
	clk_put(data->core_clk);
	data->core_clk = NULL;
}
#else
static void mxt_secure_touch_init(struct mxt_data *data)
{
}
#endif

static int mxt_probe(struct i2c_client *client,
			       const struct i2c_device_id *id)
{
	struct mxt_data *data;
	struct mxt_platform_data *pdata;
	int error, len;

	if (client->dev.of_node) {
		pdata = devm_kzalloc(&client->dev,
			sizeof(struct mxt_platform_data), GFP_KERNEL);
		if (!pdata) {
			dev_err(&client->dev, "Failed to allocate memory\n");
			return -ENOMEM;
		}

		error = mxt_parse_dt(&client->dev, pdata);
		if (error)
			return error;
	} else
		pdata = client->dev.platform_data;

	if (!pdata)
		return -EINVAL;

	data = devm_kzalloc(&client->dev, sizeof(struct mxt_data), GFP_KERNEL);
	if (!data) {
		dev_err(&client->dev, "Failed to allocate memory\n");
		return -ENOMEM;
	}

	snprintf(data->phys, sizeof(data->phys), "i2c-%u-%04x/input0",
		 client->adapter->nr, client->addr);

	data->client = client;
	data->irq = client->irq;
	data->pdata = pdata;
	i2c_set_clientdata(client, data);

	init_completion(&data->bl_completion);
	init_completion(&data->reset_completion);
	init_completion(&data->crc_completion);
	mutex_init(&data->debug_msg_lock);

	if (data->pdata->cfg_name) {
		len = strlen(data->pdata->cfg_name);
		if (len > MXT_NAME_MAX_LEN - 1) {
			dev_err(&client->dev, "Invalid config name\n");
			goto err_destroy_mutex;
		}

		strlcpy(data->cfg_name, data->pdata->cfg_name, len + 1);
	}

	error = mxt_pinctrl_init(data);
	if (error)
		dev_info(&client->dev, "No pinctrl support\n");

	error = mxt_gpio_enable(data, true);
	if (error) {
		dev_err(&client->dev, "Failed to configure gpios\n");
		goto err_destroy_mutex;
	}
	data->irq = data->client->irq =
				gpio_to_irq(data->pdata->gpio_irq);

	error = mxt_regulator_configure(data, true);
	if (error) {
		dev_err(&client->dev, "Failed to probe regulators\n");
		goto err_free_gpios;
	}

	error = mxt_regulator_enable(data);
	if (error) {
		dev_err(&client->dev, "Error %d enabling regulators\n", error);
		goto err_put_regs;
	}

	error = mxt_initialize(data);
	if (error)
		goto err_free_regs;

	error = sysfs_create_group(&client->dev.kobj, &mxt_attr_group);
	if (error) {
		dev_err(&client->dev, "Failure %d creating sysfs group\n",
			error);
		goto err_free_object;
	}

	sysfs_bin_attr_init(&data->mem_access_attr);
	data->mem_access_attr.attr.name = "mem_access";
	data->mem_access_attr.attr.mode = S_IRUGO | S_IWUSR;
	data->mem_access_attr.read = mxt_mem_access_read;
	data->mem_access_attr.write = mxt_mem_access_write;
	data->mem_access_attr.size = data->mem_size;

	if (sysfs_create_bin_file(&client->dev.kobj,
				  &data->mem_access_attr) < 0) {
		dev_err(&client->dev, "Failed to create %s\n",
			data->mem_access_attr.attr.name);
		goto err_remove_sysfs_group;
	}

	error = request_threaded_irq(data->irq, NULL, mxt_interrupt,
				     data->pdata->irqflags | IRQF_ONESHOT,
				     client->name, data);
	if (error) {
		dev_err(&client->dev, "Failed to register interrupt\n");
		goto err_remove_sysfs_group;
	}

#if defined(CONFIG_FB)
	data->fb_notif.notifier_call = fb_notifier_callback;

	error = fb_register_client(&data->fb_notif);

	if (error) {
		dev_err(&client->dev, "Unable to register fb_notifier: %d\n",
			error);
		goto err_free_irq;
	}
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	data->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN +
						MXT_SUSPEND_LEVEL;
	data->early_suspend.suspend = mxt_early_suspend;
	data->early_suspend.resume = mxt_late_resume;
	register_early_suspend(&data->early_suspend);
#endif

	mxt_secure_touch_init(data);

	return 0;

err_free_irq:
	free_irq(data->irq, data);
err_remove_sysfs_group:
	sysfs_remove_group(&client->dev.kobj, &mxt_attr_group);
err_free_object:
	mxt_free_object_table(data);
err_put_regs:
	mxt_regulator_configure(data, false);
err_free_regs:
	mxt_regulator_disable(data);
err_free_gpios:
	mxt_gpio_enable(data, false);
err_destroy_mutex:
	mutex_destroy(&data->debug_msg_lock);
	return error;
}

static int mxt_remove(struct i2c_client *client)
{
	struct mxt_data *data = i2c_get_clientdata(client);

	if (data->mem_access_attr.attr.name)
		sysfs_remove_bin_file(&client->dev.kobj,
				      &data->mem_access_attr);

#if defined(CONFIG_FB)
	fb_unregister_client(&data->fb_notif);
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	unregister_early_suspend(&data->early_suspend);
#endif
	sysfs_remove_group(&client->dev.kobj, &mxt_attr_group);
	free_irq(data->irq, data);
	mxt_regulator_configure(data, false);
	mxt_regulator_disable(data);
	if (!IS_ERR(data->reg_xvdd))
		regulator_put(data->reg_xvdd);
	regulator_put(data->reg_avdd);
	regulator_put(data->reg_vdd);
	mxt_free_object_table(data);
	if (!dev_get_platdata(&data->client->dev))
		kfree(data->pdata);
	kfree(data);

	return 0;
}

static void mxt_shutdown(struct i2c_client *client)
{
	struct mxt_data *data = i2c_get_clientdata(client);

	disable_irq(data->irq);
}

static const struct i2c_device_id mxt_id[] = {
	{ "qt602240_ts", 0 },
	{ "atmel_mxt_ts", 0 },
	{ "atmel_maxtouch_ts", 0 },
	{ "atmel_mxt_tp", 0 },
	{ "mXT224", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mxt_id);
#ifdef CONFIG_OF
static struct of_device_id mxt_match_table[] = {
	{ .compatible = "atmel,maxtouch-ts",},
	{ },
};
#else
#define mxt_match_table NULL
#endif

static struct i2c_driver mxt_driver = {
	.driver = {
		.name	= "atmel_maxtouch_ts",
		.owner	= THIS_MODULE,
#ifdef CONFIG_PM_SLEEP
		.pm	= &mxt_pm_ops,
#endif
		.of_match_table = mxt_match_table,
	},
	.probe		= mxt_probe,
	.remove		= mxt_remove,
	.shutdown	= mxt_shutdown,
	.id_table	= mxt_id,
};

module_i2c_driver(mxt_driver);

/* Module information */
MODULE_AUTHOR("Joonyoung Shim <jy0922.shim@samsung.com>");
MODULE_DESCRIPTION("Atmel maXTouch Touchscreen driver");
MODULE_LICENSE("GPL");
