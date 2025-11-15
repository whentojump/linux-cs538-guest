#ifndef _NET_NETMEM_STATS_H
#define _NET_NETMEM_STATS_H

#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/spinlock.h>
#include <linux/seq_file.h>

#define NETMEM_COUNT_BY_ADDRESS

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

void netmem_track_skb_reset(void);
void netmem_track_skb_operation(struct sk_buff *skb, const char *operation, size_t size);

/* Hardware watchpoint for truesize field */
#ifdef CONFIG_HAVE_HW_BREAKPOINT
int skb_install_truesize_watchpoint(struct sk_buff *skb);
void skb_uninstall_truesize_watchpoint(void);
bool skb_has_truesize_watchpoint(void);
struct sk_buff *skb_get_watched_skb(void);
#else
static inline int skb_install_truesize_watchpoint(struct sk_buff *skb) { return -ENOSYS; }
static inline void skb_uninstall_truesize_watchpoint(void) { }
static inline bool skb_has_truesize_watchpoint(void) { return false; }
static inline struct sk_buff *skb_get_watched_skb(void) { return NULL; }
#endif

#endif /* _NET_NETMEM_STATS_H */
