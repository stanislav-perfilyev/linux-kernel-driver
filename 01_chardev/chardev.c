// SPDX-License-Identifier: GPL-2.0
/*
 * chardev.c — /dev/mymonitor character device driver
 *
 * Features:
 *  - Kernel-space ring buffer (RING_SIZE bytes)
 *  - file_operations: open / release / read / write / ioctl
 *  - ioctl: GET_STATS, RESET, SET_FILTER_PID
 *  - Per-open-instance private state
 *
 * Build:  make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
 * Load:   sudo insmod chardev.ko
 * Device: sudo mknod /dev/mymonitor c $(cat /proc/devices | grep mymonitor | awk '{print $1}') 0
 *         sudo chmod 666 /dev/mymonitor
 * Unload: sudo rmmod chardev
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/device.h>
#include <linux/pid.h>
#include "chardev.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Stanislav Perfilyev");
MODULE_DESCRIPTION("Portfolio LKM: /dev/mymonitor character device with ring buffer");
MODULE_VERSION("1.0");

/* ------------------------------------------------------------------ */
/*  Module parameters                                                   */
/* ------------------------------------------------------------------ */
static unsigned int ring_size = 4096;
module_param(ring_size, uint, 0444);
MODULE_PARM_DESC(ring_size, "Ring buffer size in bytes (default 4096)");

static int filter_pid = 0;  /* 0 = accept all */
module_param(filter_pid, int, 0644);
MODULE_PARM_DESC(filter_pid, "Only accept writes from this PID (0 = no filter)");

/* ------------------------------------------------------------------ */
/*  Internal ring buffer                                                */
/* ------------------------------------------------------------------ */
struct ring_buf {
	u8            *buf;
	unsigned int   size;    /* capacity (bytes), always power-of-2 */
	unsigned int   head;    /* next read position  */
	unsigned int   tail;    /* next write position */
	spinlock_t     lock;
	wait_queue_head_t wq;
};

static inline unsigned int ring_used(const struct ring_buf *r)
{
	return (r->tail - r->head) & (r->size - 1);
}

static inline unsigned int ring_free(const struct ring_buf *r)
{
	return r->size - ring_used(r) - 1;
}

static inline bool ring_empty(const struct ring_buf *r)
{
	return r->head == r->tail;
}

/* Write up to len bytes; returns bytes actually written */
static unsigned int ring_write(struct ring_buf *r, const u8 *src, unsigned int len)
{
	unsigned int avail = ring_free(r);
	unsigned int n, i;

	n = min(len, avail);
	for (i = 0; i < n; i++) {
		r->buf[r->tail & (r->size - 1)] = src[i];
		r->tail++;
	}
	return n;
}

/* Read up to len bytes; returns bytes actually read */
static unsigned int ring_read(struct ring_buf *r, u8 *dst, unsigned int len)
{
	unsigned int avail = ring_used(r);
	unsigned int n, i;

	n = min(len, avail);
	for (i = 0; i < n; i++) {
		dst[i] = r->buf[r->head & (r->size - 1)];
		r->head++;
	}
	return n;
}

/* ------------------------------------------------------------------ */
/*  Device globals                                                      */
/* ------------------------------------------------------------------ */
#define DEVICE_NAME "mymonitor"
#define CLASS_NAME  "mymonitor_class"

static int            major;
static struct class  *mymonitor_class;
static struct cdev    mymonitor_cdev;

static struct ring_buf  g_ring;
static struct mutex     g_stats_lock;

static unsigned long stat_reads;
static unsigned long stat_writes;
static unsigned long stat_bytes_written;
static unsigned long stat_bytes_read;

/* ------------------------------------------------------------------ */
/*  file_operations                                                     */
/* ------------------------------------------------------------------ */
static int chardev_open(struct inode *inode, struct file *filp)
{
	/* Nothing per-file for now; could allocate private state here */
	pr_debug("mymonitor: open() pid=%d\n", current->pid);
	return 0;
}

static int chardev_release(struct inode *inode, struct file *filp)
{
	pr_debug("mymonitor: release() pid=%d\n", current->pid);
	return 0;
}

static ssize_t chardev_read(struct file *filp, char __user *ubuf,
			    size_t count, loff_t *ppos)
{
	u8      *tmp;
	ssize_t  n;
	unsigned long flags;

	if (count == 0)
		return 0;

	/* Block until data available (non-blocking: return EAGAIN) */
	if (filp->f_flags & O_NONBLOCK) {
		spin_lock_irqsave(&g_ring.lock, flags);
		if (ring_empty(&g_ring)) {
			spin_unlock_irqrestore(&g_ring.lock, flags);
			return -EAGAIN;
		}
		spin_unlock_irqrestore(&g_ring.lock, flags);
	} else {
		if (wait_event_interruptible(g_ring.wq,
					     !ring_empty(&g_ring)))
			return -ERESTARTSYS;
	}

	tmp = kmalloc(count, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	spin_lock_irqsave(&g_ring.lock, flags);
	n = ring_read(&g_ring, tmp, (unsigned int)count);
	spin_unlock_irqrestore(&g_ring.lock, flags);

	if (n > 0 && copy_to_user(ubuf, tmp, n)) {
		kfree(tmp);
		return -EFAULT;
	}

	kfree(tmp);

	mutex_lock(&g_stats_lock);
	stat_reads++;
	stat_bytes_read += (unsigned long)n;
	mutex_unlock(&g_stats_lock);

	return (ssize_t)n;
}

static ssize_t chardev_write(struct file *filp, const char __user *ubuf,
			     size_t count, loff_t *ppos)
{
	u8      *tmp;
	ssize_t  n;
	unsigned long flags;

	/* PID filter */
	if (filter_pid != 0 && current->pid != filter_pid)
		return -EPERM;

	if (count == 0)
		return 0;
	if (count > ring_size)
		count = ring_size;

	tmp = kmalloc(count, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	if (copy_from_user(tmp, ubuf, count)) {
		kfree(tmp);
		return -EFAULT;
	}

	spin_lock_irqsave(&g_ring.lock, flags);
	n = ring_write(&g_ring, tmp, (unsigned int)count);
	spin_unlock_irqrestore(&g_ring.lock, flags);

	kfree(tmp);

	if (n > 0) {
		wake_up_interruptible(&g_ring.wq);
		mutex_lock(&g_stats_lock);
		stat_writes++;
		stat_bytes_written += (unsigned long)n;
		mutex_unlock(&g_stats_lock);
	}

	return (n == 0) ? -ENOBUFS : (ssize_t)n;
}

static __poll_t chardev_poll(struct file *filp, poll_table *wait)
{
	__poll_t mask = 0;
	unsigned long flags;

	poll_wait(filp, &g_ring.wq, wait);

	spin_lock_irqsave(&g_ring.lock, flags);
	if (!ring_empty(&g_ring))
		mask |= EPOLLIN | EPOLLRDNORM;
	if (ring_free(&g_ring) > 0)
		mask |= EPOLLOUT | EPOLLWRNORM;
	spin_unlock_irqrestore(&g_ring.lock, flags);

	return mask;
}

static long chardev_ioctl(struct file *filp, unsigned int cmd,
			  unsigned long arg)
{
	struct mymonitor_stats s;
	pid_t pid;
	unsigned long flags;

	switch (cmd) {
	case IOCTL_GET_STATS:
		mutex_lock(&g_stats_lock);
		s.reads        = stat_reads;
		s.writes       = stat_writes;
		s.bytes_written = stat_bytes_written;
		s.bytes_read   = stat_bytes_read;
		mutex_unlock(&g_stats_lock);
		spin_lock_irqsave(&g_ring.lock, flags);
		s.ring_used     = ring_used(&g_ring);
		s.ring_capacity = g_ring.size;
		spin_unlock_irqrestore(&g_ring.lock, flags);
		if (copy_to_user((void __user *)arg, &s, sizeof(s)))
			return -EFAULT;
		return 0;

	case IOCTL_RESET:
		spin_lock_irqsave(&g_ring.lock, flags);
		g_ring.head = 0;
		g_ring.tail = 0;
		spin_unlock_irqrestore(&g_ring.lock, flags);
		mutex_lock(&g_stats_lock);
		stat_reads = stat_writes = 0;
		stat_bytes_read = stat_bytes_written = 0;
		mutex_unlock(&g_stats_lock);
		pr_info("mymonitor: stats reset by pid %d\n", current->pid);
		return 0;

	case IOCTL_SET_FILTER_PID:
		if (copy_from_user(&pid, (void __user *)arg, sizeof(pid)))
			return -EFAULT;
		if (pid < 0)
			return -EINVAL;
		filter_pid = (int)pid;
		pr_info("mymonitor: filter_pid set to %d\n", filter_pid);
		return 0;

	default:
		return -ENOTTY;
	}
}

static const struct file_operations chardev_fops = {
	.owner          = THIS_MODULE,
	.open           = chardev_open,
	.release        = chardev_release,
	.read           = chardev_read,
	.write          = chardev_write,
	.poll           = chardev_poll,
	.unlocked_ioctl = chardev_ioctl,
};

/* ------------------------------------------------------------------ */
/*  Module init / exit                                                  */
/* ------------------------------------------------------------------ */

/* Round up to next power-of-2 (for ring buffer masking) */
static unsigned int next_pow2(unsigned int v)
{
	if (v == 0)
		return 1;
	v--;
	v |= v >> 1; v |= v >> 2; v |= v >> 4;
	v |= v >> 8; v |= v >> 16;
	return v + 1;
}

static int __init chardev_init(void)
{
	int ret;
	dev_t dev;
	struct device *mymonitor_dev;

	/* Allocate ring buffer */
	g_ring.size = next_pow2(ring_size);
	g_ring.buf  = kmalloc(g_ring.size, GFP_KERNEL);
	if (!g_ring.buf)
		return -ENOMEM;
	g_ring.head = 0;
	g_ring.tail = 0;
	spin_lock_init(&g_ring.lock);
	init_waitqueue_head(&g_ring.wq);
	mutex_init(&g_stats_lock);

	/* Register char device */
	ret = alloc_chrdev_region(&dev, 0, 1, DEVICE_NAME);
	if (ret < 0) {
		pr_err("mymonitor: alloc_chrdev_region failed: %d\n", ret);
		goto err_free_buf;
	}
	major = MAJOR(dev);

	cdev_init(&mymonitor_cdev, &chardev_fops);
	mymonitor_cdev.owner = THIS_MODULE;
	ret = cdev_add(&mymonitor_cdev, dev, 1);
	if (ret < 0) {
		pr_err("mymonitor: cdev_add failed: %d\n", ret);
		goto err_unreg;
	}

	/* Create /dev/mymonitor automatically via udev */
	mymonitor_class = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(mymonitor_class)) {
		ret = PTR_ERR(mymonitor_class);
		pr_err("mymonitor: class_create failed: %d\n", ret);
		goto err_del_cdev;
	}
	mymonitor_dev = device_create(mymonitor_class, NULL, dev, NULL, DEVICE_NAME);
	if (IS_ERR(mymonitor_dev)) {
		ret = PTR_ERR(mymonitor_dev);
		pr_err("mymonitor: device_create failed\n");
		goto err_class;
	}

	pr_info("mymonitor: loaded — major=%d ring_size=%u filter_pid=%d\n",
		major, g_ring.size, filter_pid);
	return 0;

err_class:
	class_destroy(mymonitor_class);
err_del_cdev:
	cdev_del(&mymonitor_cdev);
err_unreg:
	unregister_chrdev_region(dev, 1);
err_free_buf:
	kfree(g_ring.buf);
	return ret;
}

static void __exit chardev_exit(void)
{
	dev_t dev = MKDEV(major, 0);

	device_destroy(mymonitor_class, dev);
	class_destroy(mymonitor_class);
	cdev_del(&mymonitor_cdev);
	unregister_chrdev_region(dev, 1);
	kfree(g_ring.buf);
	pr_info("mymonitor: unloaded\n");
}

module_init(chardev_init);
module_exit(chardev_exit);
