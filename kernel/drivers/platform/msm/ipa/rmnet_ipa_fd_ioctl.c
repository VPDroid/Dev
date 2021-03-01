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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/rmnet_ipa_fd_ioctl.h>
#include "ipa_qmi_service.h"

#define DRIVER_NAME "wwan_ioctl"

#ifdef CONFIG_COMPAT
#define WAN_IOC_ADD_FLT_RULE32 _IOWR(WAN_IOC_MAGIC, \
		WAN_IOCTL_ADD_FLT_RULE, \
		compat_uptr_t)
#define WAN_IOC_ADD_FLT_RULE_INDEX32 _IOWR(WAN_IOC_MAGIC, \
		WAN_IOCTL_ADD_FLT_INDEX, \
		compat_uptr_t)
#endif

static unsigned int dev_num = 1;
static struct cdev wan_ioctl_cdev;
static unsigned int process_ioctl = 1;
static struct class *class;
static dev_t device;

static long wan_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int retval = 0;
	u32 pyld_sz;
	u8 *param = NULL;

	IPAWANDBG("device %s got ioctl events :>>>\n",
		DRIVER_NAME);

	if (!process_ioctl) {
		IPAWANDBG("modem is in SSR, ignoring ioctl\n");
		return -EAGAIN;
	}

	switch (cmd) {
	case WAN_IOC_ADD_FLT_RULE:
		IPAWANDBG("device %s got WAN_IOC_ADD_FLT_RULE :>>>\n",
		DRIVER_NAME);
		pyld_sz = sizeof(struct ipa_install_fltr_rule_req_msg_v01);
		param = kzalloc(pyld_sz, GFP_KERNEL);
		if (!param) {
			retval = -ENOMEM;
			break;
		}
		if (copy_from_user(param, (u8 *)arg, pyld_sz)) {
			retval = -EFAULT;
			break;
		}
		if (qmi_filter_request_send(
			(struct ipa_install_fltr_rule_req_msg_v01 *)param)) {
			IPAWANDBG("IPACM->Q6 add filter rule failed\n");
			retval = -EFAULT;
			break;
		}
		if (copy_to_user((u8 *)arg, param, pyld_sz)) {
			retval = -EFAULT;
			break;
		}
		break;

	case WAN_IOC_ADD_FLT_RULE_INDEX:
	    IPAWANDBG("device %s got WAN_IOC_ADD_FLT_RULE_INDEX :>>>\n",
		DRIVER_NAME);
		pyld_sz = sizeof(struct ipa_fltr_installed_notif_req_msg_v01);
		param = kzalloc(pyld_sz, GFP_KERNEL);
		if (!param) {
			retval = -ENOMEM;
			break;
		}
		if (copy_from_user(param, (u8 *)arg, pyld_sz)) {
			retval = -EFAULT;
			break;
		}
		if (qmi_filter_notify_send(
		(struct ipa_fltr_installed_notif_req_msg_v01 *)param)) {
			IPAWANDBG("IPACM->Q6 rule index fail\n");
			retval = -EFAULT;
			break;
		}
		if (copy_to_user((u8 *)arg, param, pyld_sz)) {
			retval = -EFAULT;
			break;
		}
		break;
	default:
		retval = -ENOTTY;
	}
	kfree(param);
	return retval;
}

#ifdef CONFIG_COMPAT
long compat_wan_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case WAN_IOC_ADD_FLT_RULE32:
		cmd = WAN_IOC_ADD_FLT_RULE;
		break;
	case WAN_IOC_ADD_FLT_RULE_INDEX32:
		cmd = WAN_IOC_ADD_FLT_RULE_INDEX;
		break;
	default:
		return -ENOIOCTLCMD;
	}
	return wan_ioctl(file, cmd, (unsigned long) compat_ptr(arg));
}
#endif

static int wan_ioctl_open(struct inode *inode, struct file *filp)
{
	IPAWANDBG("\n IPA A7 wan_ioctl open OK :>>>> ");
	return 0;
}

const struct file_operations fops = {
	.owner = THIS_MODULE,
	.open = wan_ioctl_open,
	.read = NULL,
	.unlocked_ioctl = wan_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = compat_wan_ioctl,
#endif
};

int wan_ioctl_init(void)
{
	unsigned int wan_ioctl_major = 0;
	int ret;
	struct device *dev;

	device = MKDEV(wan_ioctl_major, 0);

	ret = alloc_chrdev_region(&device, 0, dev_num, DRIVER_NAME);
	if (ret) {
		IPAWANERR(":device_alloc err.\n");
		goto dev_alloc_err;
	}
	wan_ioctl_major = MAJOR(device);

	class = class_create(THIS_MODULE, DRIVER_NAME);
	if (IS_ERR(class)) {
		IPAWANERR(":class_create err.\n");
		goto class_err;
	}

	dev = device_create(class, NULL, device,
		NULL, DRIVER_NAME);
	if (IS_ERR(dev)) {
		IPAWANERR(":device_create err.\n");
		goto device_err;
	}

	cdev_init(&wan_ioctl_cdev, &fops);
	ret = cdev_add(&wan_ioctl_cdev, device, dev_num);
	if (ret) {
		IPAWANERR(":cdev_add err.\n");
		goto cdev_add_err;
	}

	process_ioctl = 1;

	IPAWANDBG("IPA %s major(%d) initial ok :>>>>\n",
	DRIVER_NAME, wan_ioctl_major);
	return 0;

cdev_add_err:
	device_destroy(class, device);
device_err:
	class_destroy(class);
class_err:
	unregister_chrdev_region(device, dev_num);
dev_alloc_err:
	return -ENODEV;
}

void wan_ioctl_stop_qmi_messages(void)
{
	process_ioctl = 0;
}

void wan_ioctl_enable_qmi_messages(void)
{
	process_ioctl = 1;
}

void wan_ioctl_deinit(void)
{
	cdev_del(&wan_ioctl_cdev);
	device_destroy(class, device);
	class_destroy(class);
	unregister_chrdev_region(device, dev_num);
}
