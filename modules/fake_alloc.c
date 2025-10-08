/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Network Memory Statistics Demo
 *
 * This is a simple demonstration of how the custom __alloc_skb_profile() function
 * collects network-specific memory statistics.
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <net/netmem_stats.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Network Memory Statistics Demo");
MODULE_DESCRIPTION("Demonstration of custom network memory allocation tracking");

/* Demo function to show bar() in action */
static void demo_network_allocation(void)
{
	struct sk_buff *skb;

	pr_info("=== Network Memory Allocation Demo ===\n");

	/* Allocate different sizes to demonstrate statistics collection */
	pr_info("Allocating small buffer (512 bytes)...\n");
	skb = alloc_skb(512, GFP_KERNEL);
	if (skb) {
		pr_info("Small buffer allocated successfully\n");
		kfree_skb(skb);
		pr_info("Small buffer freed\n");
	}

	pr_info("Allocating medium buffer (2048 bytes)...\n");
	skb = alloc_skb(2048, GFP_KERNEL);
	if (skb) {
		pr_info("Medium buffer allocated successfully\n");
		kfree_skb(skb);
		pr_info("Medium buffer freed\n");
	}

	pr_info("Allocating large buffer (8192 bytes)...\n");
	skb = alloc_skb(8192, GFP_KERNEL);
	if (skb) {
		pr_info("Large buffer allocated successfully\n");
		kfree_skb(skb);
		pr_info("Large buffer freed\n");
	}

	pr_info("Allocating buffer in interrupt context (GFP_ATOMIC)...\n");
	skb = alloc_skb(1024, GFP_ATOMIC);
	if (skb) {
		pr_info("Interrupt context buffer allocated successfully\n");
		kfree_skb(skb);
		pr_info("Interrupt context buffer freed\n");
	}

	pr_info("=== Demo completed ===\n");
	pr_info("Check /proc/netmem_stats for detailed statistics\n");
}

static int __init netmem_demo_init(void)
{
	pr_info("Network memory statistics demo loaded\n");

	/* Run the demo */
	demo_network_allocation();

	return 0;
}

static void __exit netmem_demo_exit(void)
{
	pr_info("Network memory statistics demo unloaded\n");
}

module_init(netmem_demo_init);
module_exit(netmem_demo_exit);
