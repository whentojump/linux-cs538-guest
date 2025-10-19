/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Network Memory Statistics Collection
 *
 * This header provides custom memory allocation tracking for the network subsystem
 * to collect exclusive memory statistics.
 */

#ifndef _NET_NETMEM_STATS_H
#define _NET_NETMEM_STATS_H

#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/spinlock.h>
#include <linux/seq_file.h>

/* Network memory allocation statistics */
struct netmem_stats {
	/* Total allocations and deallocations */
	atomic64_t total_allocations;
	atomic64_t total_deallocations;
	atomic64_t total_bytes_allocated;
	atomic64_t total_bytes_deallocated;

	/* Current active allocations */
	atomic64_t active_allocations;
	atomic64_t active_bytes;

	/* Size distribution counters */
	atomic64_t small_allocs;    /* <= 1KB */
	atomic64_t medium_allocs;   /* 1KB - 4KB */
	atomic64_t large_allocs;    /* > 4KB */

	/* Allocation context tracking */
	atomic64_t interrupt_allocs;    /* GFP_ATOMIC allocations */
	atomic64_t process_allocs;      /* GFP_KERNEL allocations */
	atomic64_t napi_allocs;         /* NAPI context allocations */

	/* Memory pressure tracking */
	atomic64_t pressure_allocs;     /* Allocations under memory pressure */
	atomic64_t failed_allocs;       /* Failed allocation attempts */

	/* Per-CPU statistics for better performance */
	struct netmem_per_cpu_stats {
		atomic64_t local_allocs;
		atomic64_t local_bytes;
	} __percpu *per_cpu_stats;

	/* Protection for statistics updates */
	spinlock_t stats_lock;
};

/* Global network memory statistics instance */
extern struct netmem_stats netmem_global_stats;

/* Function declarations */
void netmem_stats_init(void);
void netmem_stats_cleanup(void);
void netmem_stats_alloc(size_t size/*, gfp_t gfp_flags, bool success*/);
void netmem_stats_free(size_t size);
void netmem_stats_show(struct seq_file *seq);

/* Custom deallocation function that replaces kfree_skb() */
void kfree_skb_reason_profile(struct sk_buff *skb);

/* Custom allocation function that replaces alloc_skb() */
struct sk_buff *__alloc_skb_profile(unsigned int size, gfp_t priority);

#endif /* _NET_NETMEM_STATS_H */
