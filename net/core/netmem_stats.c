#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/percpu.h>
#include <linux/slab.h>
#include <linux/hashtable.h>
#include <linux/jhash.h>
#include <net/netmem_stats.h>
#include <net/netmem_pool.h>

struct netmem_stats netmem_global_stats;

#define NETMEM_SITE_HASH_BITS 7
static DEFINE_HASHTABLE(netmem_site_hash, NETMEM_SITE_HASH_BITS);

static DEFINE_SPINLOCK(netmem_site_lock);

void netmem_stats_init_counters(void)
{
	struct netmem_stats *stats = &netmem_global_stats;

	atomic64_set(&stats->total_allocations, 0);
	atomic64_set(&stats->total_deallocations, 0);
	atomic64_set(&stats->total_bytes_allocated, 0);
	atomic64_set(&stats->total_bytes_deallocated, 0);
	atomic64_set(&stats->active_allocations, 0);
	atomic64_set(&stats->active_bytes, 0);

	spin_lock_init(&stats->stats_lock);

	hash_init(netmem_site_hash);
}

void netmem_stats_cleanup_counters(void)
{
	struct netmem_stats *stats = &netmem_global_stats;
	struct netmem_site_stats *site_stats;

	struct hlist_node *tmp;
	int bkt;

	unsigned long flags;

	spin_lock_irqsave(&netmem_site_lock, flags);

	hash_for_each_safe(netmem_site_hash, bkt, tmp, site_stats, hash_node) {
		hash_del(&site_stats->hash_node);
		kfree(site_stats);
	}

	spin_unlock_irqrestore(&netmem_site_lock, flags);

	atomic64_set(&stats->total_allocations, 0);
	atomic64_set(&stats->total_deallocations, 0);
	atomic64_set(&stats->total_bytes_allocated, 0);
	atomic64_set(&stats->total_bytes_deallocated, 0);
	atomic64_set(&stats->active_allocations, 0);
	atomic64_set(&stats->active_bytes, 0);

	netmem_pool_reset_stats();
}

void netmem_stats_alloc_per_site(size_t size, const char *site)
{
	struct netmem_site_stats *site_stats;

	u32 hash;
	bool found = false;

	unsigned long flags;

	netmem_stats_alloc(size);

	if (!site)
		return;

	hash = jhash(site, strlen(site), 0);

	spin_lock_irqsave(&netmem_site_lock, flags);

	hash_for_each_possible(netmem_site_hash, site_stats, hash_node, hash) {
		if (strcmp(site_stats->site_name, site) == 0) {
			found = true;
			break;
		}
	}

	if (found) {
		atomic64_inc(&site_stats->allocations);
		atomic64_add(size, &site_stats->bytes_allocated);
	} else {
		site_stats = kmalloc(sizeof(*site_stats), GFP_ATOMIC);
		if (site_stats) {
			strncpy(site_stats->site_name, site, 63);
			site_stats->site_name[63] = '\0';
			atomic64_set(&site_stats->allocations, 1);
			atomic64_set(&site_stats->bytes_allocated, size);
			atomic64_set(&site_stats->deallocations, 0);
			atomic64_set(&site_stats->bytes_deallocated, 0);
			hash_add(netmem_site_hash, &site_stats->hash_node, hash);
		}
	}

	spin_unlock_irqrestore(&netmem_site_lock, flags);
}

void netmem_stats_free_per_site(size_t size, const char *site)
{
	struct netmem_site_stats *site_stats;

	u32 hash;
	bool found = false;

	unsigned long flags;

	netmem_stats_free(size);

	if (!site)
		return;

	hash = jhash(site, strlen(site), 0);

	spin_lock_irqsave(&netmem_site_lock, flags);

	hash_for_each_possible(netmem_site_hash, site_stats, hash_node, hash) {
		if (strcmp(site_stats->site_name, site) == 0) {
			found = true;
			break;
		}
	}

	if (found) {
		atomic64_inc(&site_stats->deallocations);
		atomic64_add(size, &site_stats->bytes_deallocated);
	} else {
		site_stats = kmalloc(sizeof(*site_stats), GFP_ATOMIC);
		if (site_stats) {
			strncpy(site_stats->site_name, site, 63);
			site_stats->site_name[63] = '\0';
			atomic64_set(&site_stats->allocations, 0);
			atomic64_set(&site_stats->bytes_allocated, 0);
			atomic64_set(&site_stats->deallocations, 1);
			atomic64_set(&site_stats->bytes_deallocated, size);
			hash_add(netmem_site_hash, &site_stats->hash_node, hash);
		}
	}

	spin_unlock_irqrestore(&netmem_site_lock, flags);
}

void netmem_stats_alloc(size_t size)
{
	struct netmem_stats *stats = &netmem_global_stats;

	unsigned long flags;

	spin_lock_irqsave(&stats->stats_lock, flags);

	atomic64_inc(&stats->total_allocations);
	atomic64_add(size, &stats->total_bytes_allocated);
	atomic64_inc(&stats->active_allocations);
	atomic64_add(size, &stats->active_bytes);

	spin_unlock_irqrestore(&stats->stats_lock, flags);
}

void netmem_stats_free(size_t size)
{
	struct netmem_stats *stats = &netmem_global_stats;

	unsigned long flags;

	spin_lock_irqsave(&stats->stats_lock, flags);

	atomic64_inc(&stats->total_deallocations);
	atomic64_add(size, &stats->total_bytes_deallocated);
	atomic64_dec(&stats->active_allocations);
	atomic64_sub(size, &stats->active_bytes);

	spin_unlock_irqrestore(&stats->stats_lock, flags);
}

void netmem_stats_show(struct seq_file *seq)
{
	struct netmem_stats *stats = &netmem_global_stats;

	u64 total_alloc, total_dealloc;
	u64 total_bytes_allocated, total_bytes_deallocated;
	u64 active_alloc, active_bytes;

	unsigned long flags;

	spin_lock_irqsave(&stats->stats_lock, flags);

	total_alloc = atomic64_read(&stats->total_allocations);
	total_dealloc = atomic64_read(&stats->total_deallocations);
	total_bytes_allocated = atomic64_read(&stats->total_bytes_allocated);
	total_bytes_deallocated = atomic64_read(&stats->total_bytes_deallocated);
	active_alloc = atomic64_read(&stats->active_allocations);
	active_bytes = atomic64_read(&stats->active_bytes);

	spin_unlock_irqrestore(&stats->stats_lock, flags);

	seq_printf(seq, "Total Allocations: %lld\n", total_alloc);
	seq_printf(seq, "Total Bytes Allocated: %lld\n", total_bytes_allocated);
	seq_printf(seq, "Total Deallocations: %lld\n", total_dealloc);
	seq_printf(seq, "Total Bytes Deallocated: %lld\n", total_bytes_deallocated);
	seq_printf(seq, "Active Allocations: %lld\n", active_alloc);
	seq_printf(seq, "Active Bytes: %lld\n", active_bytes);
}

#define COL_WIDTH1 65
#define COL_WIDTH2 5
#define COL_WIDTH3 10
#define COL_WIDTH4 5
#define COL_WIDTH5 10

void netmem_stats_show_per_site(struct seq_file *seq)
{
	struct netmem_site_stats *site_stats;

	int bkt;

	unsigned long flags;

	u64 total_alloc = 0;
	u64 total_bytes_allocated = 0;
	u64 total_dealloc = 0;
	u64 total_bytes_deallocated = 0;

	seq_printf(seq, "%-*s %*s %*s %*s %*s\n",
		   COL_WIDTH1, "caller site",
		   COL_WIDTH2, "+",
		   COL_WIDTH3, "+B",
		   COL_WIDTH4, "-",
		   COL_WIDTH5, "-B");
	for (int i = 0; i < COL_WIDTH1 + 1 + COL_WIDTH2 + 1 +
			    COL_WIDTH3 + 1 + COL_WIDTH4 + 1 +
			    COL_WIDTH5; i++)
		seq_printf(seq, "-");
	seq_printf(seq, "\n");

	spin_lock_irqsave(&netmem_site_lock, flags);

	hash_for_each(netmem_site_hash, bkt, site_stats, hash_node) {
		u64 allocs = atomic64_read(&site_stats->allocations);
		u64 bytes_alloc = atomic64_read(&site_stats->bytes_allocated);
		u64 deallocs = atomic64_read(&site_stats->deallocations);
		u64 bytes_dealloc = atomic64_read(&site_stats->bytes_deallocated);
		seq_printf(seq, "%-*s %*llu %*llu %*llu %*llu\n",
			   COL_WIDTH1, site_stats->site_name,
			   COL_WIDTH2, allocs,
			   COL_WIDTH3, bytes_alloc,
			   COL_WIDTH4, deallocs,
			   COL_WIDTH5, bytes_dealloc);
		total_alloc += allocs;
		total_bytes_allocated += bytes_alloc;
		total_dealloc += deallocs;
		total_bytes_deallocated += bytes_dealloc;
	}

	seq_printf(seq, "%-*s %*llu %*llu %*llu %*llu\n",
		COL_WIDTH1, "Total",
		COL_WIDTH2, total_alloc,
		COL_WIDTH3, total_bytes_allocated,
		COL_WIDTH4, total_dealloc,
		COL_WIDTH5, total_bytes_deallocated);

	spin_unlock_irqrestore(&netmem_site_lock, flags);
}

static int __netmem_stats_dump_proc_open(struct seq_file *seq, void *v)
{
	netmem_stats_show(seq);
	return 0;
}

static int netmem_stats_dump_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, __netmem_stats_dump_proc_open, NULL);
}

static const struct proc_ops netmem_stats_dump_proc_ops = {
	.proc_open = netmem_stats_dump_proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static ssize_t netmem_stats_reset_proc_write(struct file * file, const char __user * ubuf, size_t cnt, loff_t * ppos)
{
	netmem_stats_cleanup_counters();
	return cnt;
}

static const struct proc_ops netmem_stats_reset_proc_ops = {
    .proc_write = netmem_stats_reset_proc_write,
};

static int __netmem_stats_per_site_proc_open(struct seq_file *seq, void *v)
{
	netmem_stats_show_per_site(seq);
	return 0;
}

static int netmem_stats_per_site_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, __netmem_stats_per_site_proc_open, NULL);
}

static const struct proc_ops netmem_stats_per_site_proc_ops = {
	.proc_open = netmem_stats_per_site_proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int __init netmem_stats_init(void)
{
	netmem_stats_init_counters();

	proc_mkdir("netmem_stats", NULL);
	proc_create("netmem_stats/dump", 0444, NULL, &netmem_stats_dump_proc_ops);
	proc_create("netmem_stats/reset", 0200, NULL, &netmem_stats_reset_proc_ops);
	proc_create("netmem_stats/per_site", 0444, NULL, &netmem_stats_per_site_proc_ops);

	return 0;
}

subsys_initcall(netmem_stats_init);
