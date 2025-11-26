#include <linux/module.h>
#include <linux/genalloc.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <net/netmem_pool.h>
#include <linux/jsw.h>

#define NETMEM_POOL_SIZE	(512 * 1024 * 1024)
#define NETMEM_CHUNK_SIZE	(2 * 1024 * 1024)
#define NETMEM_NUM_CHUNKS	(NETMEM_POOL_SIZE / NETMEM_CHUNK_SIZE)
#define NETMEM_MIN_ALLOC_ORDER	6			// 2^6 = 64 bytes minimum
#define NETMEM_ALIGNMENT	64			// Cache line alignment

static struct gen_pool *netmem_pool = NULL;
static void **netmem_pool_chunks = NULL;
static size_t netmem_num_chunks = NETMEM_NUM_CHUNKS;

static atomic64_t pool_alloc_count = ATOMIC64_INIT(0);
static atomic64_t pool_free_count = ATOMIC64_INIT(0);
static atomic64_t pool_fallback_alloc_count = ATOMIC64_INIT(0);
static atomic64_t pool_fallback_free_count = ATOMIC64_INIT(0);
static atomic64_t pool_bytes_alloc_total = ATOMIC64_INIT(0);
static atomic64_t pool_bytes_free_total = ATOMIC64_INIT(0);
static atomic64_t pool_bytes_peak_usage = ATOMIC64_INIT(0);

int __init netmem_pool_init(void)
{
	int ret;
	size_t i;

	// Create the generic pool
	netmem_pool = gen_pool_create(NETMEM_MIN_ALLOC_ORDER, NUMA_NO_NODE);
	if (!netmem_pool)
		return -ENOMEM;

	// Allocate array to track chunk pointers
	netmem_pool_chunks = kzalloc(netmem_num_chunks * sizeof(void *), GFP_KERNEL);
	if (!netmem_pool_chunks) {
		gen_pool_destroy(netmem_pool);
		netmem_pool = NULL;
		return -ENOMEM;
	}

	// Allocate multiple chunks
	for (i = 0; i < netmem_num_chunks; i++) {
#ifdef USE_JSW
		netmem_pool_chunks[i] = alloc_jsw(NETMEM_CHUNK_SIZE);
#else
		netmem_pool_chunks[i] = kmalloc(NETMEM_CHUNK_SIZE, GFP_KERNEL | __GFP_ZERO);
#endif
		if (!netmem_pool_chunks[i])
			goto cleanup_chunks;

		// Add this chunk to the pool
		ret = gen_pool_add(netmem_pool,
				   (unsigned long)netmem_pool_chunks[i],
				   NETMEM_CHUNK_SIZE, NUMA_NO_NODE);
		if (ret < 0) {
#ifdef USE_JSW
			free_jsw(netmem_pool_chunks[i]);
#else
			kfree(netmem_pool_chunks[i]);
#endif
			netmem_pool_chunks[i] = NULL;
			goto cleanup_chunks;
		}
	}

	return 0;

cleanup_chunks:

	for (i = 0; i < netmem_num_chunks; i++) {
		if (netmem_pool_chunks[i]) {
#ifdef USE_JSW
			free_jsw(netmem_pool_chunks[i]);
#else
			kfree(netmem_pool_chunks[i]);
#endif
			netmem_pool_chunks[i] = NULL;
		}
	}
	kfree(netmem_pool_chunks);
	netmem_pool_chunks = NULL;
	gen_pool_destroy(netmem_pool);
	netmem_pool = NULL;
	return -ENOMEM;
}

void netmem_pool_cleanup(void)
{
	size_t i;

	if (netmem_pool_chunks) {
		for (i = 0; i < netmem_num_chunks; i++) {
			if (netmem_pool_chunks[i]) {
#ifdef USE_JSW
				free_jsw(netmem_pool_chunks[i]);
#else
				kfree(netmem_pool_chunks[i]);
#endif
				netmem_pool_chunks[i] = NULL;
			}
		}
		kfree(netmem_pool_chunks);
		netmem_pool_chunks = NULL;
	}

	if (netmem_pool) {
		gen_pool_destroy(netmem_pool);
		netmem_pool = NULL;
	}
}

void netmem_pool_reset_stats(void)
{
	atomic64_set(&pool_alloc_count, 0);
	atomic64_set(&pool_free_count, 0);
	atomic64_set(&pool_fallback_alloc_count, 0);
	atomic64_set(&pool_fallback_free_count, 0);
	atomic64_set(&pool_bytes_alloc_total, 0);
	atomic64_set(&pool_bytes_free_total, 0);
	atomic64_set(&pool_bytes_peak_usage, 0);
}
EXPORT_SYMBOL(netmem_pool_reset_stats);

static inline void netmem_pool_update_peak_usage(void)
{
	s64 current_usage;
	s64 peak;

	current_usage = atomic64_read(&pool_bytes_alloc_total) -
			atomic64_read(&pool_bytes_free_total);
	peak = atomic64_read(&pool_bytes_peak_usage);

	if (current_usage > peak)
		atomic64_set(&pool_bytes_peak_usage, current_usage);
}

static inline bool netmem_pool_is_from_pool(const void *addr)
{
	unsigned long ptr = (unsigned long)addr;
	size_t i;

	if (!netmem_pool_chunks)
		return false;

	for (i = 0; i < netmem_num_chunks; i++) {
		if (netmem_pool_chunks[i]) {
			unsigned long start = (unsigned long)netmem_pool_chunks[i];
			unsigned long end = start + NETMEM_CHUNK_SIZE;
			if (ptr >= start && ptr < end)
				return true;
		}
	}

	return false;
}

void *netmem_pool_alloc(size_t payload_size, gfp_t gfp)
{
	struct genpool_data_align align_data = { .align = NETMEM_ALIGNMENT };
	unsigned long addr;
	void *ptr;
	struct netmem_alloc_header *header;
	size_t total_size;

	total_size = payload_size + sizeof(struct netmem_alloc_header);

	if (netmem_pool) {
		addr = gen_pool_alloc_algo(netmem_pool, total_size,
					   gen_pool_first_fit_align, &align_data);
		if (addr) {
			ptr = (void *)addr;
			header = (struct netmem_alloc_header *)ptr;
			header->magic = NETMEM_FROM_POOL;
			header->payload_size = payload_size;
			header->total_size = total_size;

			atomic64_inc(&pool_alloc_count);
			atomic64_add(payload_size, &pool_bytes_alloc_total);
			netmem_pool_update_peak_usage();

			return ptr + sizeof(struct netmem_alloc_header);
		}
	}

	// Fallback
	ptr = kmalloc(total_size, gfp);
	if (ptr) {
		size_t actual_total_size = ksize(ptr);
		header = (struct netmem_alloc_header *)ptr;
		header->magic = NETMEM_FROM_KMALLOC;
		header->payload_size = actual_total_size - sizeof(struct netmem_alloc_header);
		header->total_size = actual_total_size;

		atomic64_inc(&pool_fallback_alloc_count);

		return ptr + sizeof(struct netmem_alloc_header);
	}

	return NULL;
}
EXPORT_SYMBOL(netmem_pool_alloc);

void netmem_pool_free(void *ptr)
{
	struct netmem_alloc_header *header;
	void *real_ptr;

	if (!ptr)
		return;

	real_ptr = ptr - sizeof(struct netmem_alloc_header);
	header = (struct netmem_alloc_header *)real_ptr;

	if (header->magic == NETMEM_FROM_POOL) {
		if (netmem_pool) {
			atomic64_inc(&pool_free_count);
			atomic64_add(header->payload_size, &pool_bytes_free_total);
			netmem_pool_update_peak_usage();
			gen_pool_free(netmem_pool, (unsigned long)real_ptr,
				      header->total_size);
		} else {
			pr_err("Trying to free pool memory but pool is destroyed!\n");
		}
	} else if (header->magic == NETMEM_FROM_KMALLOC) {
		kfree(real_ptr);
		atomic64_inc(&pool_fallback_free_count);
	} else {
		pr_err("Invalid magic in netmem_pool_free: %x at %p\n",
		       header->magic, ptr);
	}
}
EXPORT_SYMBOL(netmem_pool_free);

size_t netmem_pool_available(void)
{
	if (!netmem_pool)
		return 0;

	return gen_pool_avail(netmem_pool);
}
EXPORT_SYMBOL(netmem_pool_available);

size_t netmem_pool_size_get(void)
{
	if (!netmem_pool)
		return 0;

	return gen_pool_size(netmem_pool);
}
EXPORT_SYMBOL(netmem_pool_size_get);

static int netmem_pool_proc_show(struct seq_file *seq, void *v)
{
	size_t pool_size = netmem_pool_size_get();
	size_t pool_avail = netmem_pool_available();
	size_t pool_used = pool_size - pool_avail;
	u64 pool_allocs = atomic64_read(&pool_alloc_count);
	u64 pool_frees = atomic64_read(&pool_free_count);
	u64 fallback_allocs = atomic64_read(&pool_fallback_alloc_count);
	u64 fallback_frees = atomic64_read(&pool_fallback_free_count);
	u64 bytes_allocated = atomic64_read(&pool_bytes_alloc_total);
	u64 bytes_freed = atomic64_read(&pool_bytes_free_total);
	u64 bytes_peak = atomic64_read(&pool_bytes_peak_usage);

	seq_printf(seq, "Network Memory Pool Status\n");
	seq_printf(seq, "===========================\n\n");

	seq_printf(seq, "Pool Configuration:\n");
	seq_printf(seq, "  Total Size: %zu bytes (%zu MB)\n",
		   pool_size, pool_size / (1024 * 1024));
	seq_printf(seq, "  Number of Chunks: %zu\n", netmem_num_chunks);
	seq_printf(seq, "  Chunk Size: %zu bytes (%zu KB)\n",
		   (size_t)NETMEM_CHUNK_SIZE, (size_t)(NETMEM_CHUNK_SIZE / 1024));
	seq_printf(seq, "  Min Allocation: %zu bytes\n",
		   1UL << NETMEM_MIN_ALLOC_ORDER);
	seq_printf(seq, "  Alignment: %u bytes\n\n", NETMEM_ALIGNMENT);

	seq_printf(seq, "Pool Usage:\n");
	seq_printf(seq, "  Used: %zu bytes (%zu KB, %llu%%)\n",
		   pool_used, pool_used / 1024,
		   pool_size ? ((u64)pool_used * 100 / pool_size) : 0);
	seq_printf(seq, "  Available: %zu bytes (%zu KB, %llu%%)\n\n",
		   pool_avail, pool_avail / 1024,
		   pool_size ? ((u64)pool_avail * 100 / pool_size) : 0);

	seq_printf(seq, "Pool Allocations:\n");
	seq_printf(seq, "  Successful: %llu\n", pool_allocs);
	seq_printf(seq, "  Freed: %llu\n", pool_frees);
	seq_printf(seq, "  Active: %lld\n", (s64)(pool_allocs - pool_frees));
	seq_printf(seq, "  Total Bytes Allocated: %llu\n", bytes_allocated);
	seq_printf(seq, "  Total Bytes Freed: %llu\n", bytes_freed);
	seq_printf(seq, "  Peak Usage: %llu bytes (%llu KB)\n\n", bytes_peak, bytes_peak / 1024);

	seq_printf(seq, "Fallback Allocations (kmalloc):\n");
	seq_printf(seq, "  Total: %llu\n", fallback_allocs);
	seq_printf(seq, "  Freed: %llu\n", fallback_frees);
	seq_printf(seq, "  Active: %lld\n", (s64)(fallback_allocs - fallback_frees));

	return 0;
}

static int netmem_pool_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, netmem_pool_proc_show, NULL);
}

static const struct proc_ops netmem_pool_proc_ops = {
	.proc_open = netmem_pool_proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int __init netmem_pool_init_module(void)
{
	int ret;

	ret = netmem_pool_init();
	if (ret < 0)
		return ret;

	proc_create("netmem_stats/pool", 0444, NULL, &netmem_pool_proc_ops);

	return 0;
}

subsys_initcall(netmem_pool_init_module);
