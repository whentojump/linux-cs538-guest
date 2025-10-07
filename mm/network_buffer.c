/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Dedicated Network Buffer Implementation
 *
 * This module allocates a dedicated memory buffer for network communication
 * and notifies the hypervisor about its location.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/memblock.h>
#include <linux/network_buffer.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/stddef.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/workqueue.h>
#include <asm/page.h>
#include <linux/io.h>

/* Global network buffer instance */
struct network_buffer_info network_buffer = {
	.initialized = false,
};

/* Chunked allocation for large buffers */
struct network_buffer_chunk {
	phys_addr_t start;
	phys_addr_t end;
	void *virt_addr;
	size_t size;
	struct list_head list;
};

static LIST_HEAD(network_buffer_chunks);
static DEFINE_SPINLOCK(network_buffer_lock);

/* Hypervisor notification function - for now just prints to kernel log */
void notify_network_buffer_addr(phys_addr_t start, phys_addr_t end)
{
	pr_info("Network Buffer: Notifying hypervisor of dedicated buffer "
		"at physical address range [%pa, %pa)\n", &start, &end);
	pr_info("Network Buffer: Size = %llu bytes\n", (unsigned long long)(end - start));

	/* TODO: Implement actual hypervisor communication here */
	/* This could be a hypercall, MMIO write, or other mechanism */
}

/* Early boot initialization when memblock is available */
static int __init network_buffer_early_init(void)
{
	phys_addr_t start, end;
	void *virt_addr;
	phys_addr_t max_addr;
	size_t chunk_size = NETWORK_BUFFER_CHUNK_SIZE;
	int chunks_allocated = 0;

	pr_info("Network Buffer: Starting early boot initialization\n");

	/* Get the maximum available memory address */
	// -m 4096 0x0000000140000000
	// -m 8192 0x0000000240000000
	max_addr = memblock_end_of_DRAM();
	pr_info("Network Buffer: Available memory up to %pa\n", &max_addr);

	/* Try to allocate a few chunks during early boot */
	while (chunks_allocated < 128 && chunk_size >= PAGE_SIZE) {
		/* Try to allocate physical memory using memblock */
		start = memblock_phys_alloc_range(chunk_size,
						  NETWORK_BUFFER_ALIGN,
						  0, max_addr);
		if (!start) {
			/* Try smaller chunk size */
			chunk_size /= 2;
			pr_info("Network Buffer: network_buffer_early_init: try smaller chunk size: %zu", chunk_size);
			continue;
		}

		end = start + chunk_size;

		/* Map the physical memory to virtual address space */
		virt_addr = memremap(start, chunk_size, MEMREMAP_WB);
		if (!virt_addr) {
			pr_err("Network Buffer: Failed to map chunk at %pa\n", &start);
			memblock_phys_free(start, chunk_size);
			chunk_size /= 2;
			pr_info("Network Buffer: network_buffer_early_init: try smaller chunk size: %zu", chunk_size);
			continue;
		}

		/* Create chunk structure */
		{
			struct network_buffer_chunk *chunk;
			chunk = kzalloc(sizeof(*chunk), GFP_KERNEL);
			if (chunk) {
				chunk->start = start;
				chunk->end = end;
				chunk->virt_addr = virt_addr;
				chunk->size = chunk_size;

				spin_lock(&network_buffer_lock);
				list_add_tail(&chunk->list, &network_buffer_chunks);
				spin_unlock(&network_buffer_lock);

				chunks_allocated++;
				pr_info("Network Buffer: Early allocated chunk [%pa, %pa) size=%zu\n",
					&start, &end, chunk_size);
			}
		}
	}

	if (chunks_allocated > 0) {
		/* Initialize the network buffer structure */
		network_buffer.initialized = true;
		network_buffer.size = chunks_allocated * chunk_size;

		/* Get first and last chunk for notification */
		if (!list_empty(&network_buffer_chunks)) {
			struct network_buffer_chunk *first_chunk = list_first_entry(&network_buffer_chunks,
									       struct network_buffer_chunk, list);
			struct network_buffer_chunk *last_chunk = list_last_entry(&network_buffer_chunks,
									      struct network_buffer_chunk, list);

			network_buffer.start = first_chunk->start;
			network_buffer.end = last_chunk->end;

			notify_network_buffer_addr(network_buffer.start, network_buffer.end);
		}

		pr_info("Network Buffer: Early initialization completed with %d chunks (%llu MB)\n",
			chunks_allocated, (unsigned long long)network_buffer.size / (1024 * 1024));
	} else {
		pr_warn("Network Buffer: Early initialization failed, will try workqueue approach\n");
	}

	return 0;
}
early_initcall(network_buffer_early_init);
