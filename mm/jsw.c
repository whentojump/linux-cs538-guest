// SPDX-License-Identifier: GPL-2.0
/*
 * Memory Migration functionality - linux/mm/migrate.c
 *
 * Copyright (C) 2006 Silicon Graphics, Inc., Christoph Lameter
 *
 * Page migration was first developed in the context of the memory hotplug
 * project. The main authors of the migration code are:
 *
 * IWAMOTO Toshihiro <iwamoto@valinux.co.jp>
 * Hirokazu Takahashi <taka@valinux.co.jp>
 * Dave Hansen <haveblue@us.ibm.com>
 * Christoph Lameter
 */

#define pr_fmt(fmt) "JSW: " fmt

#include <linux/proc_fs.h>
#include <linux/migrate.h>
#include <linux/export.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/pagemap.h>
#include <linux/buffer_head.h>
#include <linux/mm_inline.h>
#include <linux/ksm.h>
#include <linux/rmap.h>
#include <linux/topology.h>
#include <linux/cpu.h>
#include <linux/cpuset.h>
#include <linux/writeback.h>
#include <linux/mempolicy.h>
#include <linux/vmalloc.h>
#include <linux/security.h>
#include <linux/backing-dev.h>
#include <linux/compaction.h>
#include <linux/syscalls.h>
#include <linux/compat.h>
#include <linux/hugetlb.h>
#include <linux/gfp.h>
#include <linux/pfn_t.h>
#include <linux/page_idle.h>
#include <linux/page_owner.h>
#include <linux/sched/mm.h>
#include <linux/ptrace.h>
#include <linux/memory.h>
#include <linux/sched/sysctl.h>
#include <linux/memory-tiers.h>
#include <linux/pagewalk.h>
#include <linux/kvm_para.h>
#include <linux/jsw.h>

#include <asm/tlbflush.h>

#include <trace/events/migrate.h>

//
// Guest functions
//

static void *__alloc_jsw(unsigned long size, bool do_hc)
{
	const unsigned long ALIGNMENT = HPAGE_PMD_NR;

	unsigned long needed = ALIGN(size, HPAGE_PMD_SIZE) / PAGE_SIZE;
	unsigned long pgcnt = needed + ALIGNMENT;

	// allocate contiguous pages
	struct page *pages = alloc_contig_pages(pgcnt, GFP_KERNEL, first_online_node, NULL);
	if (!pages) {
		pr_warn("Attempt to allocate %ld contig pages failed\n", pgcnt);
		return NULL;
	}

	// align to pmd boundary
	unsigned long pfn = page_to_pfn(pages);
	unsigned long skipped = 0;

	for (skipped = 0; skipped < ALIGNMENT; skipped++) {
		if (IS_ALIGNED(pfn + skipped, ALIGNMENT))
			break;
	}

	// since we ask ALIGN more pages, we can shift everything by ALIGN
	if (skipped == 0)
		skipped = ALIGNMENT;

	//
	// safe checks
	//

	// underskipping
	if (skipped < 1) {
		pr_warn("JSW pool underskipping: pfn 0x%lx, skipped %ld, needed %ld, pgcnt %ld, align %ld\n", pfn, skipped, needed, pgcnt, ALIGNMENT);
		goto cleanup;
	}

	// overskipping
	if (skipped > ALIGNMENT) {
		pr_warn("JSW pool overskipping: pfn 0x%lx, skipped %ld, needed %ld, pgcnt %ld, align %ld\n", pfn, skipped, needed, pgcnt, ALIGNMENT);
		goto cleanup;
	}

	// head misalignment
	if (!IS_ALIGNED(pfn + skipped, ALIGNMENT)) {
		pr_warn("JSW pool head misalignment: pfn 0x%lx, skipped %ld, needed %ld, pgcnt %ld, align %ld\n", pfn, skipped, needed, pgcnt, ALIGNMENT);
		goto cleanup;
	}

	// length misalignment
	if (!IS_ALIGNED(needed, ALIGNMENT)) {
		pr_warn("JSW pool length misalignment: pfn 0x%lx, skipped %ld, needed %ld, pgcnt %ld, align %ld\n", pfn, skipped, needed, pgcnt, ALIGNMENT);
		goto cleanup;
	}

	// out-of-bound
	if (skipped + needed > pgcnt) {
		pr_warn("JSW pool out-of-bound: pfn 0x%lx, skipped %ld, needed %ld, pgcnt %ld, align %ld\n", pfn, skipped, needed, pgcnt, ALIGNMENT);
		goto cleanup;
	}

	//
	// host side work
	//

	if (do_hc) {
		// invoke host promotion
		long ret = kvm_hypercall2(KVM_HC_THP_GPA_RANGE, PFN_PHYS(pfn), needed * PAGE_SIZE);
		if (ret) {
			pr_warn("JSW pool promotion failed %ld\n", ret);
			goto cleanup;
		}
	}

	//
	// cleanup
	//

	unsigned long freed = 0;

	// head, we leave one page as metadata
	for (unsigned long i = 0; i < skipped - 1; i++) {
		__free_page(&pages[i]);
		freed++;
	}

	// tail
	for (unsigned long i = skipped + needed; i < pgcnt; i++) {
		__free_page(&pages[i]);
		freed++;
	}

	// record metadata
	unsigned long *meta = page_to_virt(&pages[skipped - 1]);
	meta[0] = 0x1145141919810UL;
	meta[1] = needed + 1; // +1 for our meta page
	meta[2] = 0x1145141919811UL;

	pr_warn("JSW pool allocated %ld pages\n", pgcnt - freed);

	return page_to_virt(&pages[skipped]);

cleanup:
	free_contig_range(pfn, pgcnt);
	return NULL;
}

void free_jsw(void *ptr)
{
	struct page *pages = virt_to_page(ptr) - 1;

	unsigned long *meta = page_to_virt(pages);
	if ((meta[0] != 0x1145141919810UL) || (meta[2] != 0x1145141919811UL)) {
		pr_warn("Pool metadata corrupted: meta[0] 0x%lx, meta[1] 0x%lx, meta[2] 0x%lx\n", meta[0], meta[1], meta[2]);
		return;
	}

	unsigned long pgcnt = meta[1];

	free_contig_range(page_to_pfn(pages), pgcnt);

	pr_warn("JSW pool freed %ld pages\n", pgcnt);
}

void *alloc_jsw(unsigned long size)
{
	return __alloc_jsw(size, true);
}
