// SPDX-License-Identifier: GPL-2.0
/*
 * Hardware breakpoint/watchpoint for tracking sk_buff truesize modifications
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/hw_breakpoint.h>
#include <linux/perf_event.h>
#include <linux/kallsyms.h>
#include <linux/percpu.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/cred.h>
#include <linux/init_task.h>
#include <net/netmem_stats.h>

/* Watchpoint state */
static struct perf_event * __percpu *truesize_bp;
static struct sk_buff *watched_skb;
static unsigned int last_truesize;
static atomic_t wp_hits = ATOMIC_INIT(0);
static DEFINE_SPINLOCK(wp_lock);

/* Forward declaration */
static void skb_uninstall_truesize_watchpoint_internal(void);

/* Watchpoint callback - called when truesize is modified */
static void truesize_watchpoint_handler(struct perf_event *bp,
					struct perf_sample_data *data,
					struct pt_regs *regs)
{
	unsigned int new_truesize;
	unsigned long flags;
	void *caller = (void *)instruction_pointer(regs);

	if (!watched_skb)
		return;

	spin_lock_irqsave(&wp_lock, flags);

	new_truesize = watched_skb->truesize;

	/* Log the modification with caller information */
	pr_info("NETMEM_WP: [HIT %d] skb=%p truesize changed: %u -> %u (caller: %pS at IP=%lx)\n",
		atomic_inc_return(&wp_hits),
		watched_skb,
		last_truesize,
		new_truesize,
		caller,
		instruction_pointer(regs));

	last_truesize = new_truesize;

	spin_unlock_irqrestore(&wp_lock, flags);
}

/* Install watchpoint on a specific skb's truesize field */
int skb_install_truesize_watchpoint(struct sk_buff *skb)
{
	struct perf_event_attr attr;
	unsigned long flags;

	if (!skb)
		return -EINVAL;

	/* Uninstall existing watchpoint if any */
	if (truesize_bp) {
		skb_uninstall_truesize_watchpoint_internal();
	}

	spin_lock_irqsave(&wp_lock, flags);
	watched_skb = skb;
	last_truesize = skb->truesize;
	atomic_set(&wp_hits, 0);
	spin_unlock_irqrestore(&wp_lock, flags);

	/* Setup hardware breakpoint attributes */
	hw_breakpoint_init(&attr);
	attr.bp_addr = (unsigned long)&skb->truesize;
	attr.bp_len = HW_BREAKPOINT_LEN_4;  /* sizeof(unsigned int) */
	attr.bp_type = HW_BREAKPOINT_W;      /* Write watchpoint */
	attr.disabled = 0;

	pr_info("NETMEM_WP: Attempting to register watchpoint at addr=%llx len=%llu type=%llu (in_interrupt=%d)\n",
		(unsigned long long)attr.bp_addr, (unsigned long long)attr.bp_len,
		(unsigned long long)attr.bp_type, in_interrupt());

	/* Ensure we're not in interrupt context and have necessary context */
	if (in_interrupt()) {
		pr_err("NETMEM_WP: Cannot register watchpoint from interrupt context\n");
		watched_skb = NULL;
		return -EINVAL;
	}

	/* Register the watchpoint on all CPUs
	 * Use init_cred to ensure we have CAP_SYS_ADMIN capability,
	 * which is required for kernel-space breakpoints.
	 */
	{
		const struct cred *old_cred;

		old_cred = override_creds(&init_cred);
		truesize_bp = register_wide_hw_breakpoint(&attr,
							  truesize_watchpoint_handler,
							  NULL);
		revert_creds(old_cred);
	}

	if (IS_ERR(truesize_bp)) {
		long err = PTR_ERR(truesize_bp);
		pr_err("NETMEM_WP: Failed to register watchpoint: %ld (%s) at addr=%llx len=%llu type=%llu\n",
		       err,
		       err == -EPERM ? "EPERM" :
		       err == -ENOSPC ? "ENOSPC/slots_full" :
		       err == -EINVAL ? "EINVAL" :
		       err == -EBUSY ? "EBUSY" : "unknown",
		       (unsigned long long)attr.bp_addr, (unsigned long long)attr.bp_len,
		       (unsigned long long)attr.bp_type);
		truesize_bp = NULL;
		watched_skb = NULL;
		return err;
	}

	pr_info("NETMEM_WP: Installed watchpoint on skb=%p truesize field at %p (initial value: %u)\n",
		skb, &skb->truesize, skb->truesize);

	return 0;
}
EXPORT_SYMBOL(skb_install_truesize_watchpoint);

/* Internal helper to remove watchpoint */
static void skb_uninstall_truesize_watchpoint_internal(void)
{
	unsigned long flags;

	if (truesize_bp && !IS_ERR(truesize_bp)) {
		pr_info("NETMEM_WP: Unregistering watchpoint at %p...\n", truesize_bp);
		unregister_wide_hw_breakpoint(truesize_bp);
		truesize_bp = NULL;

		/* Wait for RCU callbacks to complete */
		pr_info("NETMEM_WP: Waiting for RCU synchronization...\n");
		synchronize_rcu();
		pr_info("NETMEM_WP: RCU synchronization complete\n");
	}

	spin_lock_irqsave(&wp_lock, flags);

	if (watched_skb) {
		pr_info("NETMEM_WP: Removed watchpoint from skb=%p (final truesize: %u, total hits: %d)\n",
			watched_skb, watched_skb->truesize, atomic_read(&wp_hits));
		watched_skb = NULL;
	}

	spin_unlock_irqrestore(&wp_lock, flags);
}

/* Remove the watchpoint - public API */
void skb_uninstall_truesize_watchpoint(void)
{
	skb_uninstall_truesize_watchpoint_internal();
}
EXPORT_SYMBOL(skb_uninstall_truesize_watchpoint);

/* Check if a watchpoint is active */
bool skb_has_truesize_watchpoint(void)
{
	return watched_skb != NULL;
}
EXPORT_SYMBOL(skb_has_truesize_watchpoint);

/* Get the currently watched skb */
struct sk_buff *skb_get_watched_skb(void)
{
	return watched_skb;
}
EXPORT_SYMBOL(skb_get_watched_skb);

