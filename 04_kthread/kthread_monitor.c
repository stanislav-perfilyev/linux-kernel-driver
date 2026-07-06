// SPDX-License-Identifier: GPL-2.0
/*
 * kthread_monitor.c — Kernel thread + workqueue periodic monitor
 *
 * Starts a kthread that wakes every `poll_interval_ms` milliseconds.
 * Each wake:
 *   1. Reads /proc/meminfo-equivalent (si_meminfo) & uptime.
 *   2. Queues a work item that formats a snapshot and writes it to
 *      a kernel ring buffer readable from /proc/kmon/snapshot.
 *   3. Triggers a notification via /proc/kmon/notify (poll()-able).
 *
 * Exposes:
 *   /proc/kmon/snapshot  — latest memory/CPU snapshot (seq_file)
 *   /proc/kmon/notify    — poll()-able: readable when new snapshot ready
 *   /proc/kmon/control   — write "start"/"stop"/"interval=<ms>"
 *
 * Build:  make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
 * Load:   sudo insmod kthread_monitor.ko poll_interval_ms=2000
 * Watch:  watch -n2 cat /proc/kmon/snapshot
 * Unload: sudo rmmod kthread_monitor
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/atomic.h>
#include <linux/jiffies.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Stanislav Perfilyev");
MODULE_DESCRIPTION("Portfolio LKM: kthread + workqueue memory/uptime monitor");
MODULE_VERSION("1.0");

/* ------------------------------------------------------------------ */
/*  Module parameters                                                   */
/* ------------------------------------------------------------------ */
static unsigned int poll_interval_ms = 1000;
module_param(poll_interval_ms, uint, 0644);
MODULE_PARM_DESC(poll_interval_ms, "Polling interval in milliseconds (default 1000)");

/* ------------------------------------------------------------------ */
/*  Snapshot storage                                                    */
/* ------------------------------------------------------------------ */
#define SNAP_BUF_SIZE 512

struct snapshot {
	ktime_t        timestamp;
	unsigned long  uptime_sec;
	unsigned long  total_ram_kb;
	unsigned long  free_ram_kb;
	unsigned long  available_ram_kb;
	unsigned long  total_swap_kb;
	unsigned long  free_swap_kb;
	unsigned long  snap_count;
};

static struct snapshot  g_snap;
static DEFINE_MUTEX(snap_lock);
static DECLARE_WAIT_QUEUE_HEAD(snap_wq);
static atomic_t new_snap_available = ATOMIC_INIT(0);

/* ------------------------------------------------------------------ */
/*  Work item: format snapshot                                          */
/* ------------------------------------------------------------------ */
struct snap_work {
	struct work_struct work;
	struct snapshot    data;
};

static void snap_work_fn(struct work_struct *work)
{
	struct snap_work *sw = container_of(work, struct snap_work, work);

	mutex_lock(&snap_lock);
	g_snap = sw->data;
	mutex_unlock(&snap_lock);

	atomic_set(&new_snap_available, 1);
	wake_up_interruptible(&snap_wq);

	kfree(sw);
}

/* ------------------------------------------------------------------ */
/*  kthread                                                             */
/* ------------------------------------------------------------------ */
static struct task_struct *kmon_thread;
static struct workqueue_struct *kmon_wq;
static atomic_t kmon_running = ATOMIC_INIT(1);
static unsigned long snap_count;

static int kmon_thread_fn(void *data)
{
	struct sysinfo si;
	struct snap_work *sw;

	pr_info("kmon: thread started (interval=%u ms)\n", poll_interval_ms);

	while (!kthread_should_stop() && atomic_read(&kmon_running)) {
		si_meminfo(&si);

		sw = kmalloc(sizeof(*sw), GFP_KERNEL);
		if (sw) {
			INIT_WORK(&sw->work, snap_work_fn);
			sw->data.timestamp       = ktime_get_real();
			sw->data.uptime_sec      = jiffies_to_msecs(jiffies) / 1000;
			sw->data.total_ram_kb    = si.totalram * (PAGE_SIZE / 1024);
			sw->data.free_ram_kb     = si.freeram  * (PAGE_SIZE / 1024);
			sw->data.available_ram_kb= (si.freeram + si.bufferram)
						   * (PAGE_SIZE / 1024);
			sw->data.total_swap_kb   = si.totalswap * (PAGE_SIZE / 1024);
			sw->data.free_swap_kb    = si.freeswap  * (PAGE_SIZE / 1024);
			sw->data.snap_count      = ++snap_count;
			queue_work(kmon_wq, &sw->work);
		}

		msleep_interruptible(poll_interval_ms);
	}

	pr_info("kmon: thread exiting\n");
	return 0;
}

/* ------------------------------------------------------------------ */
/*  /proc/kmon/snapshot                                                */
/* ------------------------------------------------------------------ */
static int snapshot_show(struct seq_file *m, void *v)
{
	struct snapshot s;
	s64 sec;

	mutex_lock(&snap_lock);
	s = g_snap;
	mutex_unlock(&snap_lock);

	sec = ktime_to_ns(s.timestamp);
	do_div(sec, NSEC_PER_SEC);

	seq_printf(m, "snap_count     : %lu\n",    s.snap_count);
	seq_printf(m, "uptime_sec     : %lu\n",    s.uptime_sec);
	seq_printf(m, "total_ram_kb   : %lu\n",    s.total_ram_kb);
	seq_printf(m, "free_ram_kb    : %lu\n",    s.free_ram_kb);
	seq_printf(m, "available_ram_kb: %lu\n",   s.available_ram_kb);
	seq_printf(m, "used_ram_kb    : %lu\n",
		   s.total_ram_kb > s.free_ram_kb ?
		   s.total_ram_kb - s.free_ram_kb : 0);
	seq_printf(m, "total_swap_kb  : %lu\n",    s.total_swap_kb);
	seq_printf(m, "free_swap_kb   : %lu\n",    s.free_swap_kb);
	seq_printf(m, "interval_ms    : %u\n",     poll_interval_ms);
	return 0;
}

static int snapshot_open(struct inode *i, struct file *f)
{
	atomic_set(&new_snap_available, 0);
	return single_open(f, snapshot_show, NULL);
}

static __poll_t snapshot_poll(struct file *filp, poll_table *wait)
{
	poll_wait(filp, &snap_wq, wait);
	if (atomic_read(&new_snap_available))
		return EPOLLIN | EPOLLRDNORM;
	return 0;
}

static const struct proc_ops snap_fops = {
	.proc_open    = snapshot_open,
	.proc_read    = seq_read,
	.proc_poll    = snapshot_poll,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};

/* ------------------------------------------------------------------ */
/*  /proc/kmon/control — write "start" | "stop" | "interval=<ms>"    */
/* ------------------------------------------------------------------ */
static int ctrl_show(struct seq_file *m, void *v)
{
	seq_printf(m, "running  : %d\n", atomic_read(&kmon_running));
	seq_printf(m, "interval : %u ms\n", poll_interval_ms);
	return 0;
}
static int ctrl_open(struct inode *i, struct file *f)
{ return single_open(f, ctrl_show, NULL); }

static ssize_t ctrl_write(struct file *file, const char __user *ubuf,
			  size_t count, loff_t *ppos)
{
	char buf[64];
	unsigned int ms;

	if (count == 0 || count >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, ubuf, count))
		return -EFAULT;
	buf[count] = '\0';
	if (count > 0 && buf[count - 1] == '\n')
		buf[count - 1] = '\0';

	if (strcmp(buf, "stop") == 0) {
		atomic_set(&kmon_running, 0);
		pr_info("kmon: stopped via /proc\n");
	} else if (strcmp(buf, "start") == 0) {
		atomic_set(&kmon_running, 1);
		pr_info("kmon: started via /proc\n");
	} else if (sscanf(buf, "interval=%u", &ms) == 1) {
		if (ms < 100 || ms > 60000)
			return -ERANGE;
		poll_interval_ms = ms;
		pr_info("kmon: interval set to %u ms\n", poll_interval_ms);
	} else {
		return -EINVAL;
	}

	return (ssize_t)count;
}

static const struct proc_ops ctrl_fops = {
	.proc_open    = ctrl_open,
	.proc_read    = seq_read,
	.proc_write   = ctrl_write,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};

/* ------------------------------------------------------------------ */
/*  Module init / exit                                                  */
/* ------------------------------------------------------------------ */
static struct proc_dir_entry *kmon_proc_dir;
static struct proc_dir_entry *kmon_proc_snap;
static struct proc_dir_entry *kmon_proc_ctrl;

static int __init kmon_init(void)
{
	int ret;

	kmon_wq = alloc_ordered_workqueue("kmon_wq", 0);
	if (!kmon_wq)
		return -ENOMEM;

	kmon_proc_dir = proc_mkdir("kmon", NULL);
	if (!kmon_proc_dir) {
		ret = -ENOMEM;
		goto err_wq;
	}

	kmon_proc_snap = proc_create("snapshot", 0444, kmon_proc_dir, &snap_fops);
	if (!kmon_proc_snap) {
		ret = -ENOMEM;
		goto err_dir;
	}

	kmon_proc_ctrl = proc_create("control", 0644, kmon_proc_dir, &ctrl_fops);
	if (!kmon_proc_ctrl) {
		ret = -ENOMEM;
		goto err_snap;
	}

	kmon_thread = kthread_run(kmon_thread_fn, NULL, "kmon_thread");
	if (IS_ERR(kmon_thread)) {
		ret = PTR_ERR(kmon_thread);
		pr_err("kmon: kthread_run failed: %d\n", ret);
		goto err_ctrl;
	}

	pr_info("kmon: loaded — polling every %u ms\n", poll_interval_ms);
	return 0;

err_ctrl:
	proc_remove(kmon_proc_ctrl);
err_snap:
	proc_remove(kmon_proc_snap);
err_dir:
	proc_remove(kmon_proc_dir);
err_wq:
	destroy_workqueue(kmon_wq);
	return ret;
}

static void __exit kmon_exit(void)
{
	atomic_set(&kmon_running, 0);
	kthread_stop(kmon_thread);
	flush_workqueue(kmon_wq);
	destroy_workqueue(kmon_wq);
	proc_remove(kmon_proc_ctrl);
	proc_remove(kmon_proc_snap);
	proc_remove(kmon_proc_dir);
	pr_info("kmon: unloaded\n");
}

module_init(kmon_init);
module_exit(kmon_exit);
