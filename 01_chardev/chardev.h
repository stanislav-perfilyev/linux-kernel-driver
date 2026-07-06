/* SPDX-License-Identifier: GPL-2.0 */
/*
 * chardev.h — shared definitions for /dev/mymonitor character device
 *
 * Kernel module + userspace must include this header for ioctl commands.
 */
#ifndef CHARDEV_H
#define CHARDEV_H

#include <linux/ioctl.h>

#define MYMONITOR_MAGIC  'M'
#define MYMONITOR_MAXMSG 256

/* Stats exposed via GET_STATS ioctl */
struct mymonitor_stats {
	unsigned long reads;        /* total read() calls */
	unsigned long writes;       /* total write() calls */
	unsigned long bytes_written;
	unsigned long bytes_read;
	unsigned long ring_used;    /* currently occupied bytes */
	unsigned long ring_capacity;
};

/* ioctl commands */
#define IOCTL_GET_STATS   _IOR(MYMONITOR_MAGIC, 1, struct mymonitor_stats)
#define IOCTL_RESET       _IO(MYMONITOR_MAGIC,  2)
#define IOCTL_SET_FILTER_PID _IOW(MYMONITOR_MAGIC, 3, pid_t)

#endif /* CHARDEV_H */
