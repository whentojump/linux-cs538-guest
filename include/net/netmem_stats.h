#ifndef _NET_NETMEM_STATS_H
#define _NET_NETMEM_STATS_H

#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/spinlock.h>
#include <linux/seq_file.h>

// #define NM_PRINT(...) pr_info(__VA_ARGS__)
#define NM_PRINT(...) (void) 0

#define CHANGE_KERNEL_CACHE_BEHAVIOR 1

struct netmem_site_stats {
	char site_name[64];
	atomic64_t allocations;
	atomic64_t bytes_allocated;
	atomic64_t deallocations;
	atomic64_t bytes_deallocated;
	struct hlist_node hash_node;
};

struct netmem_stats {
	atomic64_t total_allocations;
	atomic64_t total_deallocations;
	atomic64_t total_bytes_allocated;
	atomic64_t total_bytes_deallocated;
	atomic64_t active_allocations;
	atomic64_t active_bytes;

	spinlock_t stats_lock;
};

extern struct netmem_stats netmem_global_stats;

void netmem_stats_init_counters(void);
void netmem_stats_cleanup_counters(void);

void netmem_stats_alloc(size_t size);
void netmem_stats_free(size_t size);

void netmem_stats_alloc_per_site(size_t size, const char *site);
void netmem_stats_free_per_site(size_t size, const char *site);

void netmem_stats_show(struct seq_file *seq);
void netmem_stats_show_per_site(struct seq_file *seq);

#endif /* _NET_NETMEM_STATS_H */
