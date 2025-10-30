/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Network Memory Statistics Collection Implementation
 *
 * This module provides custom memory allocation tracking for the network subsystem
 * to collect exclusive memory statistics.
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/percpu.h>
#include <linux/slab.h>
#include <linux/hashtable.h>
#include <linux/jhash.h>
#include <net/netmem_stats.h>

/* Global network memory statistics */
struct netmem_stats netmem_global_stats;

/* Size thresholds for allocation categorization */
// #define NETMEM_SMALL_THRESHOLD   (1024)      /* 1KB */
// #define NETMEM_MEDIUM_THRESHOLD  (4096)      /* 4KB */

/**
 * netmem_stats_init - Initialize network memory statistics
 */
void netmem_stats_init(void)
{
	struct netmem_stats *stats = &netmem_global_stats;

	/* Initialize atomic counters */
	atomic64_set(&stats->total_allocations, 0);
	atomic64_set(&stats->total_deallocations, 0);
	atomic64_set(&stats->total_bytes_allocated, 0);
	atomic64_set(&stats->total_bytes_deallocated, 0);
	atomic64_set(&stats->active_allocations, 0);
	atomic64_set(&stats->active_bytes, 0);
	// atomic64_set(&stats->small_allocs, 0);
	// atomic64_set(&stats->medium_allocs, 0);
	// atomic64_set(&stats->large_allocs, 0);
	// atomic64_set(&stats->interrupt_allocs, 0);
	// atomic64_set(&stats->process_allocs, 0);
	// atomic64_set(&stats->napi_allocs, 0);
	// atomic64_set(&stats->pressure_allocs, 0);
	// atomic64_set(&stats->failed_allocs, 0);

	/* Initialize per-CPU statistics */
	// stats->per_cpu_stats = alloc_percpu(struct netmem_per_cpu_stats);

	// if (!stats->per_cpu_stats) {
	// 	pr_err("Failed to allocate per-CPU network memory statistics\n");
	// 	return;
	// }

	/* Initialize spinlock */
	spin_lock_init(&stats->stats_lock);

	/* Initialize per-site hash table */
	hash_init(netmem_site_hash);

	pr_info("Network memory statistics initialized\n");
}

/**
 * netmem_stats_cleanup - Cleanup network memory statistics
 */
void netmem_stats_cleanup(void)
{
	struct netmem_stats *stats = &netmem_global_stats;
	struct netmem_site_stats *site_stats;
	struct hlist_node *tmp;
	int bkt;
	unsigned long flags;

	// if (stats->per_cpu_stats) {
	// 	free_percpu(stats->per_cpu_stats);
	// 	stats->per_cpu_stats = NULL;
	// }

	/* Clean up per-site hash table */
	spin_lock_irqsave(&netmem_site_lock, flags);
	hash_for_each_safe(netmem_site_hash, bkt, tmp, site_stats, hash_node) {
		hash_del(&site_stats->hash_node);
		kfree(site_stats);
	}
	spin_unlock_irqrestore(&netmem_site_lock, flags);

	/* Reset atomic counters */
	atomic64_set(&stats->total_allocations, 0);
	atomic64_set(&stats->total_deallocations, 0);
	atomic64_set(&stats->total_bytes_allocated, 0);
	atomic64_set(&stats->total_bytes_deallocated, 0);
	atomic64_set(&stats->active_allocations, 0);
	atomic64_set(&stats->active_bytes, 0);
	// atomic64_set(&stats->small_allocs, 0);
	// atomic64_set(&stats->medium_allocs, 0);
	// atomic64_set(&stats->large_allocs, 0);
	// atomic64_set(&stats->interrupt_allocs, 0);
	// atomic64_set(&stats->process_allocs, 0);
	// atomic64_set(&stats->napi_allocs, 0);
	// atomic64_set(&stats->pressure_allocs, 0);
	// atomic64_set(&stats->failed_allocs, 0);

	/* Re-initialize per-CPU statistics */
	// stats->per_cpu_stats = alloc_percpu(struct netmem_per_cpu_stats);

	// if (!stats->per_cpu_stats) {
	// 	pr_err("Failed to allocate per-CPU network memory statistics\n");
	// 	return;
	// }

	pr_info("Network memory statistics cleaned up\n");
}

/**
 * netmem_stats_alloc_per_site - Record allocation with caller site information
 * @size: Size of the allocation
 * @site: String identifying the allocation site (caller)
 */
void netmem_stats_alloc_per_site(size_t size, const char *site)
{
	struct netmem_site_stats *site_stats;
	unsigned long flags;
	u32 hash;
	bool found = false;

	/* Also update global stats */
	netmem_stats_alloc(size);

	if (!site)
		return;

	/* Calculate hash for this site */
	hash = jhash(site, strlen(site), 0);

	/* Look for existing entry */
	spin_lock_irqsave(&netmem_site_lock, flags);
	hash_for_each_possible(netmem_site_hash, site_stats, hash_node, hash) {
		if (strcmp(site_stats->site_name, site) == 0) {
			found = true;
			break;
		}
	}

	if (found) {
		/* Update existing entry */
		atomic64_inc(&site_stats->allocations);
		atomic64_add(size, &site_stats->bytes_allocated);
	} else {
		/* Create new entry */
		site_stats = kmalloc(sizeof(*site_stats), GFP_ATOMIC);
		if (site_stats) {
			strncpy(site_stats->site_name, site, 63);
			site_stats->site_name[63] = '\0';
			atomic64_set(&site_stats->allocations, 1);
			atomic64_set(&site_stats->bytes_allocated, size);
			hash_add(netmem_site_hash, &site_stats->hash_node, hash);
		}
	}
	spin_unlock_irqrestore(&netmem_site_lock, flags);
}

/**
 * netmem_stats_alloc - Record a network memory allocation
 * @size: Size of the allocation
 * @gfp_flags: GFP flags used for allocation
 * @success: Whether the allocation was successful
 */
void netmem_stats_alloc(size_t size/*, gfp_t gfp_flags, bool success*/)
{
	struct netmem_stats *stats = &netmem_global_stats;
	unsigned long flags;

	spin_lock_irqsave(&stats->stats_lock, flags);

	// if (success) {
		/* Update total counters */
		atomic64_inc(&stats->total_allocations);
		atomic64_add(size, &stats->total_bytes_allocated);
		atomic64_inc(&stats->active_allocations);
		atomic64_add(size, &stats->active_bytes);

		// /* Update per-CPU counters */
		// if (stats->per_cpu_stats) {
		// 	struct netmem_per_cpu_stats *cpu_stats = this_cpu_ptr(stats->per_cpu_stats);
		// 	atomic64_inc(&cpu_stats->local_allocs);
		// 	atomic64_add(size, &cpu_stats->local_bytes);
		// }

		// /* Categorize by size */
		// if (size <= NETMEM_SMALL_THRESHOLD) {
		// 	atomic64_inc(&stats->small_allocs);
		// } else if (size <= NETMEM_MEDIUM_THRESHOLD) {
		// 	atomic64_inc(&stats->medium_allocs);
		// } else {
		// 	atomic64_inc(&stats->large_allocs);
		// }

		// /* Categorize by context */
		// if (gfp_flags & GFP_ATOMIC) {
		// 	atomic64_inc(&stats->interrupt_allocs);
		// } else {
		// 	atomic64_inc(&stats->process_allocs);
		// }

		// /* Check for memory pressure */
		// if (gfp_flags & __GFP_MEMALLOC) {
		// 	atomic64_inc(&stats->pressure_allocs);
		// }
	// } else {
	// 	atomic64_inc(&stats->failed_allocs);
	// }

	spin_unlock_irqrestore(&stats->stats_lock, flags);
}

/**
 * netmem_stats_free - Record a network memory deallocation
 * @size: Size of the deallocation
 */
void netmem_stats_free(size_t size)
{
	struct netmem_stats *stats = &netmem_global_stats;
	unsigned long flags;

	spin_lock_irqsave(&stats->stats_lock, flags);

	atomic64_inc(&stats->total_deallocations);
	atomic64_add(size, &stats->total_bytes_deallocated);
	atomic64_dec(&stats->active_allocations);
	atomic64_sub(size, &stats->active_bytes);

	// /* Update per-CPU counters */
	// if (stats->per_cpu_stats) {
	// 	struct netmem_per_cpu_stats *cpu_stats = this_cpu_ptr(stats->per_cpu_stats);
	// 	atomic64_dec(&cpu_stats->local_allocs);
	// 	atomic64_sub(size, &cpu_stats->local_bytes);
	// }

	spin_unlock_irqrestore(&stats->stats_lock, flags);
}

/**
 * netmem_stats_show - Display network memory statistics
 * @seq: Sequence file for output
 */
void netmem_stats_show(struct seq_file *seq)
{
	struct netmem_stats *stats = &netmem_global_stats;
	unsigned long flags;
	u64 total_alloc, total_dealloc, active_alloc, active_bytes;
	u64 total_bytes_allocated, total_bytes_deallocated;
	// u64 small_alloc, medium_alloc, large_alloc;
	// u64 interrupt_alloc, process_alloc, pressure_alloc, failed_alloc;

	spin_lock_irqsave(&stats->stats_lock, flags);

	total_alloc = atomic64_read(&stats->total_allocations);
	total_dealloc = atomic64_read(&stats->total_deallocations);
	total_bytes_allocated = atomic64_read(&stats->total_bytes_allocated);
	total_bytes_deallocated = atomic64_read(&stats->total_bytes_deallocated);
	active_alloc = atomic64_read(&stats->active_allocations);
	active_bytes = atomic64_read(&stats->active_bytes);
	// small_alloc = atomic64_read(&stats->small_allocs);
	// medium_alloc = atomic64_read(&stats->medium_allocs);
	// large_alloc = atomic64_read(&stats->large_allocs);
	// interrupt_alloc = atomic64_read(&stats->interrupt_allocs);
	// process_alloc = atomic64_read(&stats->process_allocs);
	// pressure_alloc = atomic64_read(&stats->pressure_allocs);
	// failed_alloc = atomic64_read(&stats->failed_allocs);

	spin_unlock_irqrestore(&stats->stats_lock, flags);

	seq_printf(seq, "Total Allocations: %lld\n", total_alloc);
	seq_printf(seq, "Total Bytes Allocated: %lld\n", total_bytes_allocated);
	seq_printf(seq, "Total Deallocations: %lld\n", total_dealloc);
	seq_printf(seq, "Total Bytes Deallocated: %lld\n", total_bytes_deallocated);
	seq_printf(seq, "Active Allocations: %lld\n", active_alloc);
	seq_printf(seq, "Active Bytes: %lld\n", active_bytes);

	// seq_printf(seq, "Size Distribution:\n");
	// seq_printf(seq, "  Small (<=1KB): %llu\n", small_alloc);
	// seq_printf(seq, "  Medium (1KB-4KB): %llu\n", medium_alloc);
	// seq_printf(seq, "  Large (>4KB): %llu\n", large_alloc);
	// seq_printf(seq, "\n");

	// seq_printf(seq, "Context Distribution:\n");
	// seq_printf(seq, "  Interrupt Context: %llu\n", interrupt_alloc);
	// seq_printf(seq, "  Process Context: %llu\n", process_alloc);
	// seq_printf(seq, "  Under Memory Pressure: %llu\n", pressure_alloc);
	// seq_printf(seq, "  Failed Allocations: %llu\n", failed_alloc);
	// seq_printf(seq, "\n");

	/* Calculate percentages using integer arithmetic */
	// if (total_alloc > 0) {
	// 	seq_printf(seq, "Percentages:\n");
	// 	seq_printf(seq, "  Small: %llu%%\n",
	// 		   (small_alloc * 100) / total_alloc);
	// 	seq_printf(seq, "  Medium: %llu%%\n",
	// 		   (medium_alloc * 100) / total_alloc);
	// 	seq_printf(seq, "  Large: %llu%%\n",
	// 		   (large_alloc * 100) / total_alloc);
	// 	seq_printf(seq, "  Interrupt: %llu%%\n",
	// 		   (interrupt_alloc * 100) / total_alloc);
	// 	seq_printf(seq, "  Process: %llu%%\n",
	// 		   (process_alloc * 100) / total_alloc);
	// 	seq_printf(seq, "  Failed: %llu%%\n",
	// 		   (failed_alloc * 100) / total_alloc);
	// }
}

/**
 * netmem_stats_show_per_site - Display per-site memory statistics
 * @seq: Sequence file for output
 */
void netmem_stats_show_per_site(struct seq_file *seq)
{
	struct netmem_site_stats *site_stats;
	unsigned long flags;
	int bkt;

	u64 total_alloc = 0;
	u64 total_bytes_allocated = 0;

	seq_printf(seq, "Per-Site Network Memory Statistics:\n");
	seq_printf(seq, "%-50s %15s %20s\n", "Caller Site", "Allocations", "Bytes Allocated");
	seq_printf(seq, "%s\n", "--------------------------------------------------------------------------------");

	spin_lock_irqsave(&netmem_site_lock, flags);
	hash_for_each(netmem_site_hash, bkt, site_stats, hash_node) {
		u64 allocs = atomic64_read(&site_stats->allocations);
		u64 bytes = atomic64_read(&site_stats->bytes_allocated);
		seq_printf(seq, "%-50s %15llu %20llu\n",
			   site_stats->site_name, allocs, bytes);
		total_alloc += allocs;
		total_bytes_allocated += bytes;
	}

	seq_printf(seq, "%-50s %15llu %20llu\n",
		"Total", total_alloc, total_bytes_allocated);

	spin_unlock_irqrestore(&netmem_site_lock, flags);
}

// /**
//  * __alloc_skb_profile - Custom network buffer allocation function with statistics
//  * @size: Size to allocate
//  * @priority: Allocation mask
//  *
//  * This function replaces __alloc_skb() and provides network-specific
//  * memory statistics collection.
//  */
// struct sk_buff *__alloc_skb_profile(unsigned int size, gfp_t priority)
// {
// 	struct sk_buff *skb;
//
// 	/* Call the original allocation function */
// 	skb = __alloc_skb(size, priority, 0, NUMA_NO_NODE);
//
// 	if (skb) {
// 		/* Record successful allocation */
// 		netmem_stats_alloc(size, priority, true);
// 	} else {
// 		/* Record failed allocation */
// 		netmem_stats_alloc(size, priority, false);
// 	}
//
// 	return skb;
// }
// // Human (read "Wentao"): we may need this because its caller is an inline function?
// EXPORT_SYMBOL(__alloc_skb_profile);

// void kfree_skb_reason_profile(struct sk_buff *skb)
// {
// 	size_t size = 0;
//
// 	if (skb) {
// 		size = skb_end_offset(skb) + skb->data_len;
// 		netmem_stats_free(size);
// 	}
// 	kfree_skb_reason(skb, SKB_DROP_REASON_NOT_SPECIFIED);
// }
// // Human (read "Wentao"): we may need this because its caller is an inline function?
// EXPORT_SYMBOL(kfree_skb_reason_profile);

/* Proc filesystem interface */
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
	netmem_stats_cleanup();
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

/* Module initialization and cleanup */
static int __init netmem_stats_init_module(void)
{
	netmem_stats_init();

	/* Create proc entry */
	proc_mkdir("netmem_stats", NULL);
	proc_create("netmem_stats/dump", 0444, NULL, &netmem_stats_dump_proc_ops);
	proc_create("netmem_stats/reset", 0200, NULL, &netmem_stats_reset_proc_ops);
	proc_create("netmem_stats/per_site", 0444, NULL, &netmem_stats_per_site_proc_ops);

	pr_info("Network memory statistics module loaded\n");
	return 0;
}

static void __exit netmem_stats_cleanup_module(void)
{
	remove_proc_subtree("netmem_stats", NULL);
	netmem_stats_cleanup();

	pr_info("Network memory statistics module unloaded\n");
}

module_init(netmem_stats_init_module);
module_exit(netmem_stats_cleanup_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Network Memory Statistics Collector");
MODULE_DESCRIPTION("Custom network memory allocation statistics collection");
MODULE_VERSION("1.0");
