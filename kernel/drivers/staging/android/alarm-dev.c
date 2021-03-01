/* drivers/rtc/alarm-dev.c
 *
 * Copyright (C) 2007-2009 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/time.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/alarmtimer.h>
#include "android_alarm.h"

#include <linux/slab.h>
#include <linux/drv_namespace.h>

#define ANDROID_ALARM_PRINT_INFO (1U << 0)
#define ANDROID_ALARM_PRINT_IO (1U << 1)
#define ANDROID_ALARM_PRINT_INT (1U << 2)

static int debug_mask = ANDROID_ALARM_PRINT_INFO;
module_param_named(debug_mask, debug_mask, int, S_IRUGO | S_IWUSR | S_IWGRP);

#define alarm_dbg(debug_level_mask, fmt, ...)				\
do {									\
	if (debug_mask & ANDROID_ALARM_PRINT_##debug_level_mask)	\
		pr_info(fmt, ##__VA_ARGS__);				\
} while (0)

#define ANDROID_ALARM_WAKEUP_MASK ( \
	ANDROID_ALARM_RTC_WAKEUP_MASK | \
	ANDROID_ALARM_ELAPSED_REALTIME_WAKEUP_MASK | \
	ANDROID_ALARM_RTC_POWEROFF_WAKEUP_MASK)

struct alarm_drv_ns;

struct devalarm {
	union {
		struct hrtimer hrt;
		struct alarm alrm;
	} u;
	struct alarm_drv_ns *alarm_ns;
	enum android_alarm_type type;
};

struct alarm_drv_ns {
	spinlock_t		     alarm_slock;
	struct mutex         alarm_mutex;
	struct wakeup_source alarm_wake_lock;
	wait_queue_head_t	 alarm_wait_queue;
	uint32_t		     alarm_pending;
	uint32_t		     alarm_enabled;
	uint32_t		     wait_pending;
	struct devalarm		 alarms[ANDROID_ALARM_TYPE_COUNT];
	struct drv_ns_info	 drv_ns_info;
};

struct alarm_private_data{
	struct alarm_drv_ns* alarm_ns;
	int private_data_flag;
    int alarm_opened;
};

static enum hrtimer_restart devalarm_hrthandler(struct hrtimer *hrt);
static enum alarmtimer_restart devalarm_alarmhandler(struct alarm *alrm,
								ktime_t now);

static int is_wakeup(enum android_alarm_type type)
{
	return (type == ANDROID_ALARM_RTC_WAKEUP ||
		type == ANDROID_ALARM_ELAPSED_REALTIME_WAKEUP ||
		type == ANDROID_ALARM_RTC_POWEROFF_WAKEUP);
}

static inline void devalarms_init(struct alarm_drv_ns *alarm_ns)
{
	int i;
	alarm_init(&alarm_ns->alarms[ANDROID_ALARM_RTC_WAKEUP].u.alrm,
			ALARM_REALTIME, devalarm_alarmhandler);
	hrtimer_init(&alarm_ns->alarms[ANDROID_ALARM_RTC].u.hrt,
			CLOCK_REALTIME, HRTIMER_MODE_ABS);
	alarm_init(&alarm_ns->alarms[ANDROID_ALARM_ELAPSED_REALTIME_WAKEUP].u.alrm,
			ALARM_BOOTTIME, devalarm_alarmhandler);
	hrtimer_init(&alarm_ns->alarms[ANDROID_ALARM_ELAPSED_REALTIME].u.hrt,
			CLOCK_BOOTTIME, HRTIMER_MODE_ABS);
	hrtimer_init(&alarm_ns->alarms[ANDROID_ALARM_SYSTEMTIME].u.hrt,
			CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	alarm_init(&alarm_ns->alarms[ANDROID_ALARM_RTC_POWEROFF_WAKEUP].u.alrm,
			ALARM_REALTIME, devalarm_alarmhandler);

	for (i = 0; i < ANDROID_ALARM_TYPE_COUNT; i++) {
		alarm_ns->alarms[i].alarm_ns = alarm_ns;
		alarm_ns->alarms[i].type = i;
		if (!is_wakeup(i))
			alarm_ns->alarms[i].u.hrt.function = devalarm_hrthandler;
	}
}

static void alarm_ns_initialize(struct alarm_drv_ns *alarm_ns)
{
	char buf[32];

	alarm_ns->alarm_slock = __SPIN_LOCK_UNLOCKED(alarm_ns->alarm_slock);
	mutex_init(&alarm_ns->alarm_mutex);
	init_waitqueue_head(&alarm_ns->alarm_wait_queue);

	alarm_ns->alarm_pending = 0;
	alarm_ns->alarm_enabled = 0;
	alarm_ns->wait_pending = 0;

	devalarms_init(alarm_ns);

#ifdef CONFIG_DRV_NS
	/* encode device-namespace into wakelock to ensure uniqueness */
	snprintf(buf, sizeof(buf)-1, "alarm[ns:%d]",
		drv_ns_init_pid(current_drv_ns()));
#else
	sprintf(buf, "alarm");
#endif

	wakeup_source_init(&alarm_ns->alarm_wake_lock, buf);
}

static void alarm_ns_destroy(struct alarm_drv_ns *alarm_ns)
{
	wakeup_source_trash(&alarm_ns->alarm_wake_lock);
}


#ifdef CONFIG_DRV_NS
/* alarm_ns_id, get_alarm_ns(), get_alarm_ns_cur(), put_alarm_ns() */
DEFINE_DRV_NS_INFO(alarm)

static struct drv_ns_info *alarm_ns_create(struct drv_namespace *drv_ns)
{
	struct alarm_drv_ns *alarm_ns;

	alarm_ns = kzalloc(sizeof(*alarm_ns), GFP_KERNEL);
	if (!alarm_ns)
		return ERR_PTR(-ENOMEM);

	alarm_ns_initialize(alarm_ns);

	return &alarm_ns->drv_ns_info;
}

static void alarm_ns_release(struct drv_ns_info *drv_ns_info)
{
	struct alarm_drv_ns *alarm_ns;

	alarm_ns = container_of(drv_ns_info, struct alarm_drv_ns, drv_ns_info);
	alarm_ns_destroy(alarm_ns);
	kfree(alarm_ns);
	
}

/*
 * If the RTC is set in any namespace (via alarm_set_rtc), make sure
 * the remaining namespaces are udpates suitably.
 */
static void update_alarm_time_change(struct alarm_drv_ns *alarm_ns);
static void propagate_alarm_time_change_func(struct drv_ns_info *drv_ns_info,
												void *unused)
{
	struct alarm_drv_ns *alarm_ns;

	alarm_ns = container_of(drv_ns_info, struct alarm_drv_ns, drv_ns_info);
	update_alarm_time_change(alarm_ns);
}

static void propagate_alarm_time_change(void)
{
	loop_drv_ns_info(alarm_ns_id, NULL, propagate_alarm_time_change_func);
}

static struct drv_ns_ops alarm_ns_ops = {
	.create = alarm_ns_create,
	.release = alarm_ns_release,
};
#else
/* init_alarm_ns, get_alarm_ns(), get_alarm_ns_cur(), put_alarm_ns() */
DEFINE_DRV_NS_INIT(alarm)
#endif

static void update_alarm_time_change(struct alarm_drv_ns *alarm_ns)
{
	unsigned long flags;

	spin_lock_irqsave(&alarm_ns->alarm_slock, flags);
	alarm_ns->alarm_pending |= ANDROID_ALARM_TIME_CHANGE_MASK;
	wake_up(&alarm_ns->alarm_wait_queue);
	spin_unlock_irqrestore(&alarm_ns->alarm_slock, flags);
}

static void devalarm_start(struct devalarm *alrm, ktime_t exp)
{
	if (is_wakeup(alrm->type))
		alarm_start(&alrm->u.alrm, exp);
	else
		hrtimer_start(&alrm->u.hrt, exp, HRTIMER_MODE_ABS);
}


static int devalarm_try_to_cancel(struct devalarm *alrm)
{
	if (is_wakeup(alrm->type))
		return alarm_try_to_cancel(&alrm->u.alrm);
	return hrtimer_try_to_cancel(&alrm->u.hrt);
}

static void devalarm_cancel(struct devalarm *alrm)
{
	if (is_wakeup(alrm->type))
		alarm_cancel(&alrm->u.alrm);
	else
		hrtimer_cancel(&alrm->u.hrt);
}

static void alarm_clear(enum android_alarm_type alarm_type, 
                              struct timespec *ts,struct alarm_drv_ns *alarm_ns)
{
	uint32_t alarm_type_mask = 1U << alarm_type;
	unsigned long flags;

	mutex_lock(&alarm_ns->alarm_mutex);
	spin_lock_irqsave(&alarm_ns->alarm_slock, flags);
	alarm_dbg(IO, "alarm %d clear\n", alarm_type);
	devalarm_try_to_cancel(&alarm_ns->alarms[alarm_type]);
	if (alarm_ns->alarm_pending) {
		alarm_ns->alarm_pending &= ~alarm_type_mask;
		if (!alarm_ns->alarm_pending && !alarm_ns->wait_pending)
			__pm_relax(&alarm_ns->alarm_wake_lock);
	}
	alarm_ns->alarm_enabled &= ~alarm_type_mask;
	spin_unlock_irqrestore(&alarm_ns->alarm_slock, flags);

	if (alarm_type == ANDROID_ALARM_RTC_POWEROFF_WAKEUP)
		set_power_on_alarm(ts->tv_sec, 0);
	mutex_unlock(&alarm_ns->alarm_mutex);
}

static void alarm_set(enum android_alarm_type alarm_type,
							struct timespec *ts,struct alarm_drv_ns *alarm_ns)
{
	uint32_t alarm_type_mask = 1U << alarm_type;
	unsigned long flags;

	mutex_lock(&alarm_ns->alarm_mutex);
	spin_lock_irqsave(&alarm_ns->alarm_slock, flags);
	alarm_dbg(IO, "alarm %d set %ld.%09ld\n",
			alarm_type, ts->tv_sec, ts->tv_nsec);
	alarm_ns->alarm_enabled |= alarm_type_mask;
	devalarm_start(&alarm_ns->alarms[alarm_type], timespec_to_ktime(*ts));
	spin_unlock_irqrestore(&alarm_ns->alarm_slock, flags);

	if (alarm_type == ANDROID_ALARM_RTC_POWEROFF_WAKEUP)
		set_power_on_alarm(ts->tv_sec, 1);
	mutex_unlock(&alarm_ns->alarm_mutex);
}

static int alarm_wait(struct alarm_drv_ns *alarm_ns)
{
	unsigned long flags;
	int rv = 0;

	spin_lock_irqsave(&alarm_ns->alarm_slock, flags);
	alarm_dbg(IO, "alarm wait\n");
	if (!alarm_ns->alarm_pending && alarm_ns->wait_pending) {
		__pm_relax(&alarm_ns->alarm_wake_lock);
		alarm_ns->wait_pending = 0;
	}
	spin_unlock_irqrestore(&alarm_ns->alarm_slock, flags);

	rv = wait_event_interruptible(alarm_ns->alarm_wait_queue, alarm_ns->alarm_pending);
	if (rv)
		return rv;

	spin_lock_irqsave(&alarm_ns->alarm_slock, flags);
	rv = alarm_ns->alarm_pending;
	alarm_ns->wait_pending = 1;
	alarm_ns->alarm_pending = 0;
	spin_unlock_irqrestore(&alarm_ns->alarm_slock, flags);

	return rv;
}

static int alarm_set_rtc(struct timespec *ts,struct alarm_drv_ns *alarm_ns)
{
	struct rtc_time new_rtc_tm;
	struct rtc_device *rtc_dev;
	int rv = 0;

	rtc_time_to_tm(ts->tv_sec, &new_rtc_tm);
	rtc_dev = alarmtimer_get_rtcdev();
	rv = do_settimeofday(ts);
	if (rv < 0)
		return rv;
	if (rtc_dev)
		rv = rtc_set_time(rtc_dev, &new_rtc_tm);

#ifdef CONFIG_DRV_NS
	propagate_alarm_time_change();
#else
    update_alarm_time_change(alarm_ns);
#endif

	return rv;
}

static int alarm_get_time(enum android_alarm_type alarm_type,
							struct timespec *ts,struct alarm_drv_ns *alarm_ns)
{
	int rv = 0;

	switch (alarm_type) {
	case ANDROID_ALARM_RTC_WAKEUP:
	case ANDROID_ALARM_RTC:
	case ANDROID_ALARM_RTC_POWEROFF_WAKEUP:
		getnstimeofday(ts);
		break;
	case ANDROID_ALARM_ELAPSED_REALTIME_WAKEUP:
	case ANDROID_ALARM_ELAPSED_REALTIME:
		get_monotonic_boottime(ts);
		break;
	case ANDROID_ALARM_SYSTEMTIME:
		ktime_get_ts(ts);
		break;
	default:
		rv = -EINVAL;
	}
	return rv;
}

static long alarm_do_ioctl(struct file *file, unsigned int cmd,
							struct timespec *ts)
{
	int rv = 0;
	unsigned long flags;
	enum android_alarm_type alarm_type = ANDROID_ALARM_IOCTL_TO_TYPE(cmd);
	struct alarm_drv_ns *alarm_ns = NULL;
	struct alarm_private_data* alarm_data = (struct alarm_private_data*)file->private_data;
	if(alarm_data){
		alarm_ns = alarm_data->alarm_ns;
	}

	if(!alarm_ns)
		return 0;
		
	if (alarm_type >= ANDROID_ALARM_TYPE_COUNT) {
		rv = -EINVAL;
		goto out;
	}

	if (ANDROID_ALARM_BASE_CMD(cmd) != ANDROID_ALARM_GET_TIME(0)) {
		if ((file->f_flags & O_ACCMODE) == O_RDONLY) {
			rv = -EPERM;
			goto out;
		}
		
		spin_lock_irqsave(&alarm_ns->alarm_slock, flags);
		if (!alarm_data->private_data_flag && cmd != ANDROID_ALARM_SET_RTC) {
			
			if(alarm_data->alarm_opened){
				spin_unlock_irqrestore(&alarm_ns->alarm_slock, flags);
				rv = -EBUSY;
				goto out;	
			}

			alarm_data->private_data_flag = 1;
			alarm_data->alarm_opened = 1;
		}
		spin_unlock_irqrestore(&alarm_ns->alarm_slock, flags);
	}

	switch (ANDROID_ALARM_BASE_CMD(cmd)) {
	case ANDROID_ALARM_CLEAR(0):
		alarm_clear(alarm_type, ts,alarm_ns);
		break;
	case ANDROID_ALARM_SET(0):
		alarm_set(alarm_type, ts,alarm_ns);
		break;
	case ANDROID_ALARM_SET_AND_WAIT(0):
		alarm_set(alarm_type, ts,alarm_ns);
		/* fall though */
	case ANDROID_ALARM_WAIT:
		rv = alarm_wait(alarm_ns);
		break;
	case ANDROID_ALARM_SET_RTC:
		rv = alarm_set_rtc(ts,alarm_ns);
		break;
	case ANDROID_ALARM_GET_TIME(0):
		rv = alarm_get_time(alarm_type, ts,alarm_ns);
		break;

	default:
		rv = -EINVAL;
	}

out:
	return rv;
}

static long alarm_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{

	struct timespec ts;
	int rv;

	switch (ANDROID_ALARM_BASE_CMD(cmd)) {
	case ANDROID_ALARM_SET_AND_WAIT(0):
	case ANDROID_ALARM_SET(0):
	case ANDROID_ALARM_SET_RTC:
	case ANDROID_ALARM_CLEAR(0):
		if (copy_from_user(&ts, (void __user *)arg, sizeof(ts)))
			return -EFAULT;
		break;
	}

	rv = alarm_do_ioctl(file, cmd, &ts);
	if (rv)
		return rv;

	switch (ANDROID_ALARM_BASE_CMD(cmd)) {
	case ANDROID_ALARM_GET_TIME(0):
		if (copy_to_user((void __user *)arg, &ts, sizeof(ts)))
			return -EFAULT;
		break;
	}

	return 0;
}
#ifdef CONFIG_COMPAT
static long alarm_compat_ioctl(struct file *file, unsigned int cmd,
							unsigned long arg)
{

	struct timespec ts;
	int rv;

	switch (ANDROID_ALARM_BASE_CMD(cmd)) {
	case ANDROID_ALARM_SET_AND_WAIT_COMPAT(0):
	case ANDROID_ALARM_SET_COMPAT(0):
	case ANDROID_ALARM_SET_RTC_COMPAT:
		if (compat_get_timespec(&ts, (void __user *)arg))
			return -EFAULT;
		/* fall through */
	case ANDROID_ALARM_GET_TIME_COMPAT(0):
		cmd = ANDROID_ALARM_COMPAT_TO_NORM(cmd);
		break;
	}

	rv = alarm_do_ioctl(file, cmd, &ts);
	if (rv)
		return rv;

	switch (ANDROID_ALARM_BASE_CMD(cmd)) {
	case ANDROID_ALARM_GET_TIME(0): /* NOTE: we modified cmd above */
		if (compat_put_timespec(&ts, (void __user *)arg))
			return -EFAULT;
		break;
	}

	return 0;
}
#endif

static int alarm_open(struct inode *inode, struct file *file)
{
	struct alarm_private_data *alarm_data;
	alarm_data = kzalloc(sizeof(*alarm_data), GFP_KERNEL);
	if(alarm_data){
		alarm_data->alarm_ns = get_alarm_ns_cur();
		alarm_data->private_data_flag = 0;
		//alarm_data->alarm_opened = 0;
	}
	file->private_data = alarm_data;
	return 0;
}

static int alarm_release(struct inode *inode, struct file *file)
{
	int i;
	unsigned long flags;
	struct alarm_drv_ns *alarm_ns = NULL;
	struct alarm_private_data* alarm_data = (struct alarm_private_data*)file->private_data;
	if(alarm_data){
		alarm_ns = alarm_data->alarm_ns;
	}

	if(!alarm_ns)
		goto out;

	spin_lock_irqsave(&alarm_ns->alarm_slock, flags);
	if(alarm_data->private_data_flag){
		for (i = 0; i < ANDROID_ALARM_TYPE_COUNT; i++) {
			uint32_t alarm_type_mask = 1U << i;
			if (alarm_ns->alarm_enabled & alarm_type_mask) {
				alarm_dbg(INFO,
					"%s: clear alarm, pending %d\n",
					__func__,
					!!(alarm_ns->alarm_pending & alarm_type_mask));
				alarm_ns->alarm_enabled &= ~alarm_type_mask;
			}
			spin_unlock_irqrestore(&alarm_ns->alarm_slock, flags);
			devalarm_cancel(&alarm_ns->alarms[i]);
			spin_lock_irqsave(&alarm_ns->alarm_slock, flags);
		}
		if (alarm_ns->alarm_pending | alarm_ns->wait_pending) {
			if (alarm_ns->alarm_pending)
				alarm_dbg(INFO, "%s: clear pending alarms %x\n",
					__func__, alarm_ns->alarm_pending);
			__pm_relax(&alarm_ns->alarm_wake_lock);
			alarm_ns->wait_pending = 0;
			alarm_ns->alarm_pending = 0;
		}
		
		alarm_data->alarm_opened = 0;
	}
	spin_unlock_irqrestore(&alarm_ns->alarm_slock, flags);

out:
	if(alarm_ns)
		put_alarm_ns(alarm_ns);
	if(alarm_data){
		alarm_data->private_data_flag = 0;
		alarm_data->alarm_ns = NULL;
		kfree(file->private_data);
	}
	return 0;
}

static void devalarm_triggered(struct devalarm *alarm)
{
	unsigned long flags;
	uint32_t alarm_type_mask = 1U << alarm->type;
	struct alarm_drv_ns *alarm_ns = alarm->alarm_ns;

	alarm_dbg(INT, "%s: type %d\n", __func__, alarm->type);
	spin_lock_irqsave(&alarm_ns->alarm_slock, flags);
	if (alarm_ns->alarm_enabled & alarm_type_mask) {
		__pm_wakeup_event(&alarm_ns->alarm_wake_lock, 5000); /* 5secs */
		alarm_ns->alarm_enabled &= ~alarm_type_mask;
		alarm_ns->alarm_pending |= alarm_type_mask;
		wake_up(&alarm_ns->alarm_wait_queue);
	}
	spin_unlock_irqrestore(&alarm_ns->alarm_slock, flags);
}


static enum hrtimer_restart devalarm_hrthandler(struct hrtimer *hrt)
{
	struct devalarm *devalrm = container_of(hrt, struct devalarm, u.hrt);

	devalarm_triggered(devalrm);
	return HRTIMER_NORESTART;
}

static enum alarmtimer_restart devalarm_alarmhandler(struct alarm *alrm,
							ktime_t now)
{
	struct devalarm *devalrm = container_of(alrm, struct devalarm, u.alrm);

	devalarm_triggered(devalrm);
	return ALARMTIMER_NORESTART;
}


static const struct file_operations alarm_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = alarm_ioctl,
	.open = alarm_open,
	.release = alarm_release,
#ifdef CONFIG_COMPAT
	.compat_ioctl = alarm_compat_ioctl,
#endif
};

static struct miscdevice alarm_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "alarm",
	.fops = &alarm_fops,
};

static int __init alarm_dev_init(void)
{
	int err;

	err = misc_register(&alarm_device);
	if (err)
		return err;

#ifdef CONFIG_DRV_NS
	err = DRV_NS_REGISTER(alarm, "alarm");
	if (err < 0) {
		misc_deregister(&alarm_device);
		return err;
	}
#else
	alarm_ns_initialize(&init_alarm_ns);
#endif

	return 0;
}

static void  __exit alarm_dev_exit(void)
{
	misc_deregister(&alarm_device);
#ifdef CONFIG_DRV_NS
	DRV_NS_UNREGISTER(alarm);
#else
	alarm_ns_destroy(&init_alarm_ns);
#endif
}

module_init(alarm_dev_init);
module_exit(alarm_dev_exit);

