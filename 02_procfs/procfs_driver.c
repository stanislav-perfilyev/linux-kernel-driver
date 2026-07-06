// SPDX-License-Identifier: GPL-2.0
/*
 * procfs_driver.c — /proc/mydriver/{stats,config} driver
 *
 * Creates:
 *   /proc/mydriver/stats   — read-only: uptime, msg count, last_pid
 *   /proc/mydriver/config  — read/write: log_level, max_msgs
 *
 * Uses seq_file API for efficient multi-page reads.
 *
 * Build:  make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
 * Load:   sudo insmod procfs_driver.ko
 * Test:   cat /proc/mydriver/stats
 *         echo "log_level=2" | sudo tee /proc/mydriver/config
 * Unload: sudo rmmod procfs_driver
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/ktime.h>
#include <linux/jiffies.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Stanislav Perfilyev");
MODULE_DESCRIPTION("Portfolio LKM: /proc/mydriver procfs driver (seq_file)");
MODULE_VERSION("1.0");

/* ------------------------------------------------------------------ */
/*  Driver state                                                        */
/* ------------------------------------------------------------------ */
static DEFINE_MUTEX(drv_lock);

static unsigned long drv_msg_count;
static unsigned long drv_load_jiffies;
static pid_t         drv_last_pid;
static int           cfg_log_level = 1;     /* 0=off 1=info 2=debug */
static unsigned int  cfg_max_msgs  = 1000;

/* ------------------------------------------------------------------ */
/*  /proc/mydriver/stats — seq_file (read-only)                        */
/* ------------------------------------------------------------------ */
static int stats_show(struct seq_file *m, void *v)
{
	unsigned long uptime_sec = (jiffies - drv_load_jiffies) / HZ;

	mutex_lock(&drv_lock);
	seq_printf(m, "uptime_sec    : %lu\n",   uptime_sec);
	seq_printf(m, "msg_count     : %lu\n",   drv_msg_count);
	seq_printf(m, "last_pid      : %d\n",    drv_last_pid);
	seq_printf(m, "log_level     : %d\n",    cfg_log_level);
	seq_printf(m, "max_msgs      : %u\n",    cfg_max_msgs);
	mutex_unlock(&drv_lock);
	return 0;
}

static int stats_open(struct inode *inode, struct file *file)
{
	mutex_lock(&drv_lock);
	drv_msg_count++;
	drv_last_pid = current->pid;
	mutex_unlock(&drv_lock);
	return single_open(file, stats_show, NULL);
}

static const struct proc_ops stats_fops = {
	.proc_open    = stats_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};

/* ------------------------------------------------------------------ */
/*  /proc/mydriver/config — seq_file read + custom write               */
/* ------------------------------------------------------------------ */
static int config_show(struct seq_file *m, void *v)
{
	mutex_lock(&drv_lock);
	seq_printf(m, "log_level=%d\n",  cfg_log_level);
	seq_printf(m, "max_msgs=%u\n",   cfg_max_msgs);
	mutex_unlock(&drv_lock);
	return 0;
}

static int config_open(struct inode *inode, struct file *file)
{
	return single_open(file, config_show, NULL);
}

/*
 * Write format: "key=value\n"
 * Supported keys: log_level, max_msgs
 */
static ssize_t config_write(struct file *file, const char __user *ubuf,
			    size_t count, loff_t *ppos)
{
	char *buf;
	int   ret = 0;
	long  val;
	char *eq;

	if (count == 0 || count > 128)
		return -EINVAL;

	buf = kmalloc(count + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, ubuf, count)) {
		kfree(buf);
		return -EFAULT;
	}
	buf[count] = '\0';

	/* Strip trailing newline */
	if (count > 0 && buf[count - 1] == '\n')
		buf[count - 1] = '\0';

	eq = strchr(buf, '=');
	if (!eq) {
		ret = -EINVAL;
		goto out;
	}
	*eq = '\0';
	eq++;

	ret = kstrtol(eq, 10, &val);
	if (ret)
		goto out;

	mutex_lock(&drv_lock);
	if (strcmp(buf, "log_level") == 0) {
		if (val < 0 || val > 2) { ret = -ERANGE; }
		else { cfg_log_level = (int)val; }
	} else if (strcmp(buf, "max_msgs") == 0) {
		if (val <= 0 || val > 1000000) { ret = -ERANGE; }
		else { cfg_max_msgs = (unsigned int)val; }
	} else {
		ret = -EINVAL;
	}
	mutex_unlock(&drv_lock);

	if (!ret)
		ret = (ssize_t)count;
out:
	kfree(buf);
	return ret;
}

static const struct proc_ops config_fops = {
	.proc_open    = config_open,
	.proc_read    = seq_read,
	.proc_write   = config_write,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};

/* ------------------------------------------------------------------ */
/*  Module init / exit                                                  */
/* ------------------------------------------------------------------ */
static struct proc_dir_entry *proc_dir;
static struct proc_dir_entry *proc_stats;
static struct proc_dir_entry *proc_config;

static int __init procfs_driver_init(void)
{
	drv_load_jiffies = jiffies;

	proc_dir = proc_mkdir("mydriver", NULL);
	if (!proc_dir) {
		pr_err("mydriver: failed to create /proc/mydriver\n");
		return -ENOMEM;
	}

	proc_stats = proc_create("stats", 0444, proc_dir, &stats_fops);
	if (!proc_stats) {
		pr_err("mydriver: failed to create /proc/mydriver/stats\n");
		goto err_dir;
	}

	proc_config = proc_create("config", 0644, proc_dir, &config_fops);
	if (!proc_config) {
		pr_err("mydriver: failed to create /proc/mydriver/config\n");
		goto err_stats;
	}

	pr_info("mydriver: loaded — /proc/mydriver/{stats,config} created\n");
	return 0;

err_stats:
	proc_remove(proc_stats);
err_dir:
	proc_remove(proc_dir);
	return -ENOMEM;
}

static void __exit procfs_driver_exit(void)
{
	proc_remove(proc_config);
	proc_remove(proc_stats);
	proc_remove(proc_dir);
	pr_info("mydriver: unloaded\n");
}

module_init(procfs_driver_init);
module_exit(procfs_driver_exit);
