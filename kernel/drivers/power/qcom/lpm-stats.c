/* Copyright (c) 2012-2014, The Linux Foundation. All rights reserved.
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <soc/qcom/spm.h>
#include <soc/qcom/pm.h>

#define MAX_STR_LEN 256
const char *lpm_stats_reset = "reset";
const char *lpm_stats_suspend = "suspend";

struct level_stats {
	const char *name;
	struct lpm_stats *owner;
	int64_t first_bucket_time;
	int bucket[CONFIG_MSM_IDLE_STATS_BUCKET_COUNT];
	int64_t min_time[CONFIG_MSM_IDLE_STATS_BUCKET_COUNT];
	int64_t max_time[CONFIG_MSM_IDLE_STATS_BUCKET_COUNT];
	int success_count;
	int failed_count;
	int64_t total_time;
	uint64_t enter_time;
};

struct lifo_stats {
	uint32_t last_in;
	uint32_t first_out;
};

struct lpm_stats {
	char name[MAX_STR_LEN];
	struct level_stats *time_stats;
	uint32_t num_levels;
	struct lifo_stats lifo;
	struct lpm_stats *parent;
	struct list_head sibling;
	struct list_head child;
	struct cpumask mask;
	struct dentry *directory;
	bool is_cpu;
};

static struct level_stats suspend_time_stats;

static DEFINE_PER_CPU_SHARED_ALIGNED(struct lpm_stats, cpu_stats);

static void update_level_stats(struct level_stats *stats, uint64_t t,
				bool success)
{
	uint64_t bt;
	int i;

	if (!success) {
		stats->failed_count++;
		return;
	}

	stats->success_count++;
	stats->total_time += t;
	bt = t;
	do_div(bt, stats->first_bucket_time);

	if (bt < 1ULL << (CONFIG_MSM_IDLE_STATS_BUCKET_SHIFT *
			(CONFIG_MSM_IDLE_STATS_BUCKET_COUNT - 1)))
		i = DIV_ROUND_UP(fls((uint32_t)bt),
			CONFIG_MSM_IDLE_STATS_BUCKET_SHIFT);
	else
		i = CONFIG_MSM_IDLE_STATS_BUCKET_COUNT - 1;

	if (i >= CONFIG_MSM_IDLE_STATS_BUCKET_COUNT)
		i = CONFIG_MSM_IDLE_STATS_BUCKET_COUNT - 1;

	stats->bucket[i]++;

	if (t < stats->min_time[i] || !stats->max_time[i])
		stats->min_time[i] = t;
	if (t > stats->max_time[i])
		stats->max_time[i] = t;
	return;
}

static void level_stats_print(struct seq_file *m, struct level_stats *stats)
{
	int i = 0;
	int64_t bucket_time = 0;
	char seqs[MAX_STR_LEN] = {0};
	int64_t s = stats->total_time;
	uint32_t ns = do_div(s, NSEC_PER_SEC);

	snprintf(seqs, MAX_STR_LEN,
		"[%s] %s:\n"
		"  success count: %7d\n"
		"  total success time: %lld.%09u\n",
		stats->owner->name,
		stats->name,
		stats->success_count,
		s, ns);
	seq_puts(m, seqs);

	if (stats->failed_count) {
		snprintf(seqs, MAX_STR_LEN, "  failed count: %7d\n",
			stats->failed_count);
		seq_puts(m, seqs);
	}

	bucket_time = stats->first_bucket_time;
	for (i = 0;
		i < CONFIG_MSM_IDLE_STATS_BUCKET_COUNT - 1;
		i++) {
		s = bucket_time;
		ns = do_div(s, NSEC_PER_SEC);
		snprintf(seqs, MAX_STR_LEN,
			"\t<%6lld.%09u: %7d (%lld-%lld)\n",
			s, ns, stats->bucket[i],
				stats->min_time[i],
				stats->max_time[i]);
		seq_puts(m, seqs);
		bucket_time <<= CONFIG_MSM_IDLE_STATS_BUCKET_SHIFT;
	}
	snprintf(seqs, MAX_STR_LEN,
		"\t>=%5lld.%09u:%8d (%lld-%lld)\n",
		s, ns, stats->bucket[i],
		stats->min_time[i],
		stats->max_time[i]);
	seq_puts(m, seqs);
}

static int level_stats_file_show(struct seq_file *m, void *v)
{
	struct level_stats *stats = NULL;

	if (!m->private)
		return -EINVAL;

	stats = (struct level_stats *) m->private;

	level_stats_print(m, stats);

	return 0;
}

static int level_stats_file_open(struct inode *inode, struct file *file)
{
	return single_open(file, level_stats_file_show, inode->i_private);
}

static void level_stats_print_all(struct seq_file *m, struct lpm_stats *stats)
{
	struct list_head *centry = NULL;
	struct lpm_stats *pos = NULL;
	int i = 0;

	for (i = 0; i < stats->num_levels; i++)
		level_stats_print(m, &stats->time_stats[i]);

	if (list_empty(&stats->child))
		return;

	centry = &stats->child;
	list_for_each_entry(pos, centry, sibling) {
		level_stats_print_all(m, pos);
	}
}

static void level_stats_reset(struct level_stats *stats)
{
	memset(stats->bucket, 0, sizeof(stats->bucket));
	memset(stats->min_time, 0, sizeof(stats->min_time));
	memset(stats->max_time, 0, sizeof(stats->max_time));
	stats->success_count = 0;
	stats->failed_count = 0;
	stats->total_time = 0;
}

static void level_stats_reset_all(struct lpm_stats *stats)
{
	struct list_head *centry = NULL;
	struct lpm_stats *pos = NULL;
	int i = 0;

	for (i = 0; i < stats->num_levels; i++)
		level_stats_reset(&stats->time_stats[i]);

	if (list_empty(&stats->child))
		return;

	centry = &stats->child;
	list_for_each_entry(pos, centry, sibling) {
		level_stats_reset_all(pos);
	}
}

static int lpm_stats_file_show(struct seq_file *m, void *v)
{
	struct lpm_stats *stats = (struct lpm_stats *)m->private;

	if (!m->private) {
		pr_err("%s: Invalid pdata, Cannot print stats\n", __func__);
		return -EINVAL;
	}

	level_stats_print_all(m, stats);
	level_stats_print(m, &suspend_time_stats);

	return 0;
}

static int lpm_stats_file_open(struct inode *inode, struct file *file)
{
	return single_open(file, lpm_stats_file_show, inode->i_private);
}

static ssize_t level_stats_file_write(struct file *file,
	const char __user *buffer, size_t count, loff_t *off)
{
	char buf[MAX_STR_LEN] = {0};
	struct inode *in = file->f_inode;
	struct level_stats *stats = (struct level_stats *)in->i_private;
	size_t len = strnlen(lpm_stats_reset, MAX_STR_LEN);

	if (!stats)
		return -EINVAL;

	if (count != len+1)
		return -EINVAL;

	if (copy_from_user(buf, buffer, len))
		return -EFAULT;

	if (strcmp(buf, lpm_stats_reset))
		return -EINVAL;

	level_stats_reset(stats);

	return count;
}

static ssize_t lpm_stats_file_write(struct file *file,
	const char __user *buffer, size_t count, loff_t *off)
{
	char buf[MAX_STR_LEN] = {0};
	struct inode *in = file->f_inode;
	struct lpm_stats *stats = (struct lpm_stats *)in->i_private;
	size_t len = strnlen(lpm_stats_reset, MAX_STR_LEN);

	if (!stats)
		return -EINVAL;

	if (count != len+1)
		return -EINVAL;

	if (copy_from_user(buf, buffer, len))
		return -EFAULT;

	if (strcmp(buf, lpm_stats_reset))
		return -EINVAL;

	level_stats_reset_all(stats);

	return count;
}

int lifo_stats_file_show(struct seq_file *m, void *v)
{
	struct lpm_stats *stats = NULL;
	struct list_head *centry = NULL;
	struct lpm_stats *pos = NULL;
	char seqs[MAX_STR_LEN] = {0};

	if (!m->private)
		return -EINVAL;

	stats = (struct lpm_stats *)m->private;

	if (list_empty(&stats->child)) {
		pr_err("%s: ERROR: Lifo level with no children.\n",
			__func__);
		return -EINVAL;
	}

	centry = &stats->child;
	list_for_each_entry(pos, centry, sibling) {
		snprintf(seqs, MAX_STR_LEN,
			"%s:\n"
			"\tLast-In:%u\n"
			"\tFirst-Out:%u\n",
			pos->name,
			pos->lifo.last_in,
			pos->lifo.first_out);
		seq_puts(m, seqs);
	}
	return 0;
}

static int lifo_stats_file_open(struct inode *inode, struct file *file)
{
	return single_open(file, lifo_stats_file_show, inode->i_private);
}

static void lifo_stats_reset_all(struct lpm_stats *stats)
{
	struct list_head *centry = NULL;
	struct lpm_stats *pos = NULL;

	centry = &stats->child;
	list_for_each_entry(pos, centry, sibling) {
		pos->lifo.last_in = 0;
		pos->lifo.first_out = 0;
		if (!list_empty(&pos->child))
			lifo_stats_reset_all(pos);
	}
}

static ssize_t lifo_stats_file_write(struct file *file,
	const char __user *buffer, size_t count, loff_t *off)
{
	char buf[MAX_STR_LEN] = {0};
	struct inode *in = file->f_inode;
	struct lpm_stats *stats = (struct lpm_stats *)in->i_private;
	size_t len = strnlen(lpm_stats_reset, MAX_STR_LEN);

	if (!stats)
		return -EINVAL;

	if (count != len+1)
		return -EINVAL;

	if (copy_from_user(buf, buffer, len))
		return -EFAULT;

	if (strcmp(buf, lpm_stats_reset))
		return -EINVAL;

	lifo_stats_reset_all(stats);

	return count;
}

static const struct file_operations level_stats_fops = {
	.owner	  = THIS_MODULE,
	.open	  = level_stats_file_open,
	.read	  = seq_read,
	.release  = single_release,
	.llseek   = no_llseek,
	.write	  = level_stats_file_write,
};

static const struct file_operations lpm_stats_fops = {
	.owner	  = THIS_MODULE,
	.open	  = lpm_stats_file_open,
	.read	  = seq_read,
	.release  = single_release,
	.llseek   = no_llseek,
	.write	  = lpm_stats_file_write,
};

static const struct file_operations lifo_stats_fops = {
	.owner	  = THIS_MODULE,
	.open	  = lifo_stats_file_open,
	.read	  = seq_read,
	.release  = single_release,
	.llseek   = no_llseek,
	.write	  = lifo_stats_file_write,
};

static void update_last_in_stats(struct lpm_stats *stats)
{
	struct list_head *centry = NULL;
	struct lpm_stats *pos = NULL;

	if (list_empty(&stats->child))
		return;

	centry = &stats->child;
	list_for_each_entry(pos, centry, sibling) {
		if (cpumask_test_cpu(smp_processor_id(), &pos->mask)) {
			pos->lifo.last_in++;
			return;
		}
	}
	WARN(1, "Should not reach here\n");
}

static void update_first_out_stats(struct lpm_stats *stats)
{
	struct list_head *centry = NULL;
	struct lpm_stats *pos = NULL;

	if (list_empty(&stats->child))
		return;

	centry = &stats->child;
	list_for_each_entry(pos, centry, sibling) {
		if (cpumask_test_cpu(smp_processor_id(), &pos->mask)) {
			pos->lifo.first_out++;
			return;
		}
	}
	WARN(1, "Should not reach here\n");
}

static inline void update_exit_stats(struct lpm_stats *stats, uint32_t index,
					bool success)
{
	uint64_t exit_time = 0;

	/* Update time stats only when exit is preceded by enter */
	if (stats->time_stats[index].enter_time) {
		exit_time = sched_clock() -
				stats->time_stats[index].enter_time;
		update_level_stats(&stats->time_stats[index], exit_time,
					success);
		stats->time_stats[index].enter_time = 0;
	}
}

static int config_level(const char *name, const char **levels,
	int num_levels, struct lpm_stats *parent, struct lpm_stats *stats)
{
	int i = 0;
	struct dentry *directory = NULL;
	const char *rootname = "lpm_stats";
	const char *dirname = rootname;

	strlcpy(stats->name, name, MAX_STR_LEN);
	stats->num_levels = num_levels;
	stats->parent = parent;
	INIT_LIST_HEAD(&stats->sibling);
	INIT_LIST_HEAD(&stats->child);

	stats->time_stats = kzalloc(sizeof(struct level_stats) *
				num_levels, GFP_KERNEL);
	if (!stats->time_stats) {
		pr_err("%s: Insufficient memory for %s level time stats\n",
			__func__, name);
		return -ENOMEM;
	}

	if (parent) {
		list_add_tail(&stats->sibling, &parent->child);
		directory = parent->directory;
		dirname = name;
	}

	stats->directory = debugfs_create_dir(dirname, directory);
	if (!stats->directory) {
		pr_err("%s: Unable to create %s debugfs directory\n",
			__func__, dirname);
		kfree(stats->time_stats);
		return -EPERM;
	}

	for (i = 0; i < num_levels; i++) {
		stats->time_stats[i].name = levels[i];
		stats->time_stats[i].owner = stats;
		stats->time_stats[i].first_bucket_time =
			CONFIG_MSM_IDLE_STATS_FIRST_BUCKET;
		stats->time_stats[i].enter_time = 0;

		if (!debugfs_create_file(stats->time_stats[i].name, S_IRUGO,
			stats->directory, (void *)&stats->time_stats[i],
			&level_stats_fops)) {
			pr_err("%s: Unable to create %s %s level-stats file\n",
				__func__, stats->name,
				stats->time_stats[i].name);
			kfree(stats->time_stats);
			return -EPERM;
		}
	}

	if (!debugfs_create_file("stats", S_IRUGO, stats->directory,
		(void *)stats, &lpm_stats_fops)) {
		pr_err("%s: Unable to create %s's overall 'stats' file\n",
			__func__, stats->name);
		kfree(stats->time_stats);
		return -EPERM;
	}

	return 0;
}

static struct lpm_stats *config_cpu_level(const char *name,
	const char **levels, int num_levels, struct lpm_stats *parent,
	struct cpumask *mask)
{
	int cpu = 0;
	struct lpm_stats *pstats = NULL;
	struct lpm_stats *stats = NULL;

	for (pstats = parent; pstats; pstats = pstats->parent)
		cpumask_or(&pstats->mask, &pstats->mask, mask);

	for_each_cpu(cpu, mask) {
		int ret = 0;
		char cpu_name[MAX_STR_LEN] = {0};

		stats = &per_cpu(cpu_stats, cpu);
		snprintf(cpu_name, MAX_STR_LEN, "%s%d", name, cpu);
		cpumask_set_cpu(cpu, &stats->mask);

		stats->is_cpu = true;

		ret = config_level(cpu_name, levels, num_levels, parent,
					stats);
		if (ret) {
			pr_err("%s: Unable to create %s stats\n",
				__func__, cpu_name);
			return ERR_PTR(ret);
		}
	}

	return stats;
}

static void config_suspend_level(struct lpm_stats *stats)
{
	suspend_time_stats.name = lpm_stats_suspend;
	suspend_time_stats.owner = stats;
	suspend_time_stats.first_bucket_time =
			CONFIG_MSM_SUSPEND_STATS_FIRST_BUCKET;
	suspend_time_stats.enter_time = 0;
	suspend_time_stats.success_count = 0;
	suspend_time_stats.failed_count = 0;

	if (!debugfs_create_file(suspend_time_stats.name, S_IRUGO,
		stats->directory, (void *)&suspend_time_stats,
		&level_stats_fops))
		pr_err("%s: Unable to create %s Suspend stats file\n",
			__func__, stats->name);
}

static struct lpm_stats *config_cluster_level(const char *name,
	const char **levels, int num_levels, struct lpm_stats *parent)
{
	struct lpm_stats *stats = NULL;
	int ret = 0;

	stats = kzalloc(sizeof(struct lpm_stats), GFP_KERNEL);
	if (!stats) {
		pr_err("%s: Insufficient memory for %s stats\n",
			__func__, name);
		return ERR_PTR(-ENOMEM);
	}

	stats->is_cpu = false;

	ret = config_level(name, levels, num_levels, parent, stats);
	if (ret) {
		pr_err("%s: Unable to create %s stats\n", __func__,
			name);
		kfree(stats);
		return ERR_PTR(ret);
	}

	if (!debugfs_create_file("lifo", S_IRUGO, stats->directory,
		(void *)stats, &lifo_stats_fops)) {
		pr_err("%s: Unable to create %s lifo stats file\n",
			__func__, stats->name);
		kfree(stats);
		return ERR_PTR(-EPERM);
	}

	if (!parent)
		config_suspend_level(stats);

	return stats;
}

static void cleanup_stats(struct lpm_stats *stats)
{
	struct list_head *centry = NULL;
	struct lpm_stats *pos = NULL;

	centry = &stats->child;
	list_for_each_entry_reverse(pos, centry, sibling) {
		if (!list_empty(&pos->child))
			cleanup_stats(pos);

		list_del_init(&pos->child);

		kfree(pos->time_stats);
		if (!pos->is_cpu)
			kfree(pos);
	}
	kfree(stats->time_stats);
	kfree(stats);
}

static void lpm_stats_cleanup(struct lpm_stats *stats)
{
	struct lpm_stats *pstats = stats;

	if (!pstats)
		return;

	while (pstats->parent)
		pstats = pstats->parent;

	debugfs_remove_recursive(pstats->directory);

	cleanup_stats(pstats);
}

/**
 * lpm_stats_config_level() - API to configure levels stats.
 *
 * @name:	Name of the cluster/cpu.
 * @levels:	Low power mode level names.
 * @num_levels:	Number of leves supported.
 * @parent:	Pointer to the parent's lpm_stats object.
 * @mask:	cpumask, if configuring cpu stats, else NULL.
 *
 * Function to communicate the low power mode levels supported by
 * cpus or a cluster.
 *
 * Return: Pointer to the lpm_stats object or ERR_PTR(-ERRNO)
 */
struct lpm_stats *lpm_stats_config_level(const char *name,
	const char **levels, int num_levels, struct lpm_stats *parent,
	struct cpumask *mask)
{
	struct lpm_stats *stats = NULL;

	if (!levels || num_levels <= 0 || IS_ERR(parent)) {
		pr_err("%s: Invalid input\n\t\tlevels = %p\n\t\t"
			"num_levels = %d\n\t\tparent = %ld\n",
			__func__, levels, num_levels, PTR_ERR(parent));
		return ERR_PTR(-EINVAL);
	}

	if (mask)
		stats = config_cpu_level(name, levels, num_levels, parent,
						mask);
	else
		stats = config_cluster_level(name, levels, num_levels,
						parent);

	if (IS_ERR(stats)) {
		lpm_stats_cleanup(parent);
		return stats;
	}

	return stats;
}
EXPORT_SYMBOL(lpm_stats_config_level);

/**
 * lpm_stats_cluster_enter() - API to communicate the lpm level a cluster
 * is prepared to enter.
 *
 * @stats:	Pointer to the cluster's lpm_stats object.
 * @index:	Index of the lpm level that the cluster is going to enter.
 *
 * Function to communicate the low power mode level that the cluster is
 * prepared to enter.
 */
void lpm_stats_cluster_enter(struct lpm_stats *stats, uint32_t index)
{
	if (IS_ERR_OR_NULL(stats))
		return;

	stats->time_stats[index].enter_time = sched_clock();

	update_last_in_stats(stats);
}
EXPORT_SYMBOL(lpm_stats_cluster_enter);

/**
 * lpm_stats_cluster_exit() - API to communicate the lpm level a cluster
 * exited.
 *
 * @stats:	Pointer to the cluster's lpm_stats object.
 * @index:	Index of the cluster lpm level.
 * @success:	Success/Failure of the low power mode execution.
 *
 * Function to communicate the low power mode level that the cluster
 * exited.
 */
void lpm_stats_cluster_exit(struct lpm_stats *stats, uint32_t index,
				bool success)
{
	if (IS_ERR_OR_NULL(stats))
		return;

	update_exit_stats(stats, index, success);

	update_first_out_stats(stats);
}
EXPORT_SYMBOL(lpm_stats_cluster_exit);

/**
 * lpm_stats_cpu_enter() - API to communicate the lpm level a cpu
 * is prepared to enter.
 *
 * @index:	cpu's lpm level index.
 *
 * Function to communicate the low power mode level that the cpu is
 * prepared to enter.
 */
void lpm_stats_cpu_enter(uint32_t index)
{
	struct lpm_stats *stats = &__get_cpu_var(cpu_stats);

	if (!stats->time_stats)
		return;

	stats->time_stats[index].enter_time = sched_clock();
}
EXPORT_SYMBOL(lpm_stats_cpu_enter);

/**
 * lpm_stats_cpu_exit() - API to communicate the lpm level that the cpu exited.
 *
 * @index:	cpu's lpm level index.
 * @success:	Success/Failure of the low power mode execution.
 *
 * Function to communicate the low power mode level that the cpu exited.
 */
void lpm_stats_cpu_exit(uint32_t index, bool success)
{
	struct lpm_stats *stats = &__get_cpu_var(cpu_stats);

	if (!stats->time_stats)
		return;

	update_exit_stats(stats, index, success);
}
EXPORT_SYMBOL(lpm_stats_cpu_exit);

/**
 * lpm_stats_suspend_enter() - API to communicate system entering suspend.
 *
 * Function to communicate that the system is ready to enter suspend.
 */
void lpm_stats_suspend_enter(void)
{
	struct timespec ts;

	getnstimeofday(&ts);
	suspend_time_stats.enter_time = timespec_to_ns(&ts);
}
EXPORT_SYMBOL(lpm_stats_suspend_enter);

/**
 * lpm_stats_suspend_exit() - API to communicate system exiting suspend.
 *
 * Function to communicate that the system exited suspend.
 */
void lpm_stats_suspend_exit(void)
{
	struct timespec ts;
	uint64_t exit_time = 0;

	getnstimeofday(&ts);
	exit_time = timespec_to_ns(&ts) - suspend_time_stats.enter_time;
	update_level_stats(&suspend_time_stats, exit_time, true);
}
EXPORT_SYMBOL(lpm_stats_suspend_exit);
