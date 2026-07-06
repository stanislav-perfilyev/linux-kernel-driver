// SPDX-License-Identifier: GPL-2.0
/*
 * netfilter_hook.c — Netfilter NF_INET_PRE_ROUTING packet filter
 *
 * Registers a hook at NF_INET_PRE_ROUTING (IPv4).
 * Drops incoming TCP/UDP packets matching blocked IP+port rules.
 * Rules configured via module params and /proc/netfilter_hook/rules.
 *
 * Exposes via /proc/netfilter_hook/:
 *   stats  — packets accepted/dropped totals
 *   rules  — list blocked "ip:port" pairs (write to add, "flush" to clear)
 *
 * Build:  make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
 * Load:   sudo insmod netfilter_hook.ko
 * Add rule: echo "192.168.1.100:8080" | sudo tee /proc/netfilter_hook/rules
 * Flush:    echo "flush" | sudo tee /proc/netfilter_hook/rules
 * Unload: sudo rmmod netfilter_hook
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/inet.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Stanislav Perfilyev");
MODULE_DESCRIPTION("Portfolio LKM: Netfilter hook with dynamic IP:port blocklist");
MODULE_VERSION("1.0");

/* ------------------------------------------------------------------ */
/*  Blocklist                                                           */
/* ------------------------------------------------------------------ */
#define MAX_RULES 64

struct block_rule {
	__be32           ip;    /* network byte order */
	__be16           port;  /* network byte order, 0 = any port */
	struct list_head node;
};

static LIST_HEAD(block_list);
static DEFINE_SPINLOCK(block_lock);
static unsigned int rule_count;

/* Stats */
static atomic_long_t stat_accepted;
static atomic_long_t stat_dropped;

/* ------------------------------------------------------------------ */
/*  Netfilter hook                                                      */
/* ------------------------------------------------------------------ */
static unsigned int nf_hook_fn(void *priv,
			       struct sk_buff *skb,
			       const struct nf_hook_state *state)
{
	struct iphdr  *iph;
	struct tcphdr *tcph;
	struct udphdr *udph;
	__be32 src_ip;
	__be16 src_port = 0;
	struct block_rule *rule;
	unsigned long flags;
	bool drop = false;

	if (!skb)
		return NF_ACCEPT;

	iph = ip_hdr(skb);
	if (!iph || iph->version != 4) {
		atomic_long_inc(&stat_accepted);
		return NF_ACCEPT;
	}

	src_ip = iph->saddr;

	if (iph->protocol == IPPROTO_TCP) {
		tcph = (struct tcphdr *)(skb_network_header(skb) +
					 (iph->ihl * 4));
		src_port = tcph->source;
	} else if (iph->protocol == IPPROTO_UDP) {
		udph = (struct udphdr *)(skb_network_header(skb) +
					 (iph->ihl * 4));
		src_port = udph->source;
	}

	spin_lock_irqsave(&block_lock, flags);
	list_for_each_entry(rule, &block_list, node) {
		if (rule->ip == src_ip &&
		    (rule->port == 0 || rule->port == src_port)) {
			drop = true;
			break;
		}
	}
	spin_unlock_irqrestore(&block_lock, flags);

	if (drop) {
		atomic_long_inc(&stat_dropped);
		return NF_DROP;
	}

	atomic_long_inc(&stat_accepted);
	return NF_ACCEPT;
}

static struct nf_hook_ops nf_ops = {
	.hook     = nf_hook_fn,
	.pf       = PF_INET,
	.hooknum  = NF_INET_PRE_ROUTING,
	.priority = NF_IP_PRI_FIRST,
};

/* ------------------------------------------------------------------ */
/*  /proc/netfilter_hook/stats                                         */
/* ------------------------------------------------------------------ */
static int stats_show(struct seq_file *m, void *v)
{
	seq_printf(m, "accepted : %ld\n", atomic_long_read(&stat_accepted));
	seq_printf(m, "dropped  : %ld\n", atomic_long_read(&stat_dropped));
	seq_printf(m, "rules    : %u\n",  rule_count);
	return 0;
}
static int stats_open(struct inode *i, struct file *f)
{ return single_open(f, stats_show, NULL); }

static const struct proc_ops nf_stats_fops = {
	.proc_open    = stats_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};

/* ------------------------------------------------------------------ */
/*  /proc/netfilter_hook/rules                                         */
/* ------------------------------------------------------------------ */
static int rules_show(struct seq_file *m, void *v)
{
	struct block_rule *rule;
	unsigned long flags;
	u8 *p;

	spin_lock_irqsave(&block_lock, flags);
	list_for_each_entry(rule, &block_list, node) {
		p = (u8 *)&rule->ip;
		seq_printf(m, "%u.%u.%u.%u:%u\n",
			   p[0], p[1], p[2], p[3],
			   ntohs(rule->port));
	}
	spin_unlock_irqrestore(&block_lock, flags);
	return 0;
}
static int rules_open(struct inode *i, struct file *f)
{ return single_open(f, rules_show, NULL); }

static void flush_rules(void)
{
	struct block_rule *rule, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&block_lock, flags);
	list_for_each_entry_safe(rule, tmp, &block_list, node) {
		list_del(&rule->node);
		kfree(rule);
	}
	rule_count = 0;
	spin_unlock_irqrestore(&block_lock, flags);
}

/* Parse "A.B.C.D:PORT" */
static int parse_rule(const char *s, __be32 *ip_out, __be16 *port_out)
{
	u8   a, b, c, d;
	unsigned int port = 0;
	int  ret;

	ret = sscanf(s, "%hhu.%hhu.%hhu.%hhu:%u", &a, &b, &c, &d, &port);
	if (ret < 4)
		return -EINVAL;
	*ip_out   = htonl(((u32)a << 24) | ((u32)b << 16) |
			  ((u32)c << 8) | d);
	*port_out = (ret == 5) ? htons((u16)port) : 0;
	return 0;
}

static ssize_t rules_write(struct file *file, const char __user *ubuf,
			   size_t count, loff_t *ppos)
{
	char *buf;
	struct block_rule *rule;
	unsigned long flags;
	int ret = 0;

	if (count == 0 || count > 64)
		return -EINVAL;

	buf = kmalloc(count + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	if (copy_from_user(buf, ubuf, count)) {
		kfree(buf);
		return -EFAULT;
	}
	buf[count] = '\0';
	if (count > 0 && buf[count - 1] == '\n')
		buf[count - 1] = '\0';

	if (strcmp(buf, "flush") == 0) {
		flush_rules();
		kfree(buf);
		return (ssize_t)count;
	}

	if (rule_count >= MAX_RULES) {
		kfree(buf);
		return -ENOSPC;
	}

	rule = kmalloc(sizeof(*rule), GFP_KERNEL);
	if (!rule) {
		kfree(buf);
		return -ENOMEM;
	}

	ret = parse_rule(buf, &rule->ip, &rule->port);
	kfree(buf);
	if (ret) {
		kfree(rule);
		return ret;
	}

	spin_lock_irqsave(&block_lock, flags);
	list_add_tail(&rule->node, &block_list);
	rule_count++;
	spin_unlock_irqrestore(&block_lock, flags);

	pr_info("netfilter_hook: rule added — total %u\n", rule_count);
	return (ssize_t)count;
}

static const struct proc_ops nf_rules_fops = {
	.proc_open    = rules_open,
	.proc_read    = seq_read,
	.proc_write   = rules_write,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};

/* ------------------------------------------------------------------ */
/*  Module init / exit                                                  */
/* ------------------------------------------------------------------ */
static struct proc_dir_entry *nf_proc_dir;
static struct proc_dir_entry *nf_proc_stats;
static struct proc_dir_entry *nf_proc_rules;

static int __init nf_hook_init(void)
{
	int ret;

	atomic_long_set(&stat_accepted, 0);
	atomic_long_set(&stat_dropped, 0);

	nf_proc_dir = proc_mkdir("netfilter_hook", NULL);
	if (!nf_proc_dir)
		return -ENOMEM;

	nf_proc_stats = proc_create("stats", 0444, nf_proc_dir, &nf_stats_fops);
	if (!nf_proc_stats) {
		ret = -ENOMEM;
		goto err_dir;
	}

	nf_proc_rules = proc_create("rules", 0644, nf_proc_dir, &nf_rules_fops);
	if (!nf_proc_rules) {
		ret = -ENOMEM;
		goto err_stats;
	}

	ret = nf_register_net_hook(&init_net, &nf_ops);
	if (ret) {
		pr_err("netfilter_hook: nf_register_net_hook failed: %d\n", ret);
		goto err_rules;
	}

	pr_info("netfilter_hook: loaded — hook at NF_INET_PRE_ROUTING\n");
	return 0;

err_rules:
	proc_remove(nf_proc_rules);
err_stats:
	proc_remove(nf_proc_stats);
err_dir:
	proc_remove(nf_proc_dir);
	return ret;
}

static void __exit nf_hook_exit(void)
{
	nf_unregister_net_hook(&init_net, &nf_ops);
	flush_rules();
	proc_remove(nf_proc_rules);
	proc_remove(nf_proc_stats);
	proc_remove(nf_proc_dir);
	pr_info("netfilter_hook: unloaded\n");
}

module_init(nf_hook_init);
module_exit(nf_hook_exit);
