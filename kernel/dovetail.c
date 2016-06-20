/* -*- linux-c -*-
 * kernel/dovetail.c
 *
 * Copyright (C) 2002-2016 Philippe Gerum.
 *
 * Dual kernel interface.
 */
#include <linux/timekeeper_internal.h>
#include <linux/dovetail.h>

void __weak arch_dovetail_enable(int flags)
{
}

void dovetail_enable(int flags)	/* flags unused yet. */
{
	ipipe_root_only();
	arch_dovetail_enable(flags);
	set_thread_local_flags(_TLF_DOVETAIL);
}
EXPORT_SYMBOL_GPL(dovetail_enable);

void dovetail_disable(void)
{
	clear_thread_local_flags(_TLF_DOVETAIL);
	clear_thread_flag(TIF_MAYDAY);
}
EXPORT_SYMBOL_GPL(dovetail_disable);

int __weak dovetail_fastcall_hook(struct pt_regs *regs)
{
	return -1;	/* i.e. fall back to slow path. */
}

int __weak dovetail_syscall_hook(struct irq_stage *stage, struct pt_regs *regs)
{
	return 0;
}

void dovetail_root_sync(void)
{
	struct irq_stage_data *p;
	unsigned long flags;

	flags = hard_local_irq_save();

	p = irq_root_this_context();
	if (irq_staged_waiting(p))
		irq_stage_sync_current();

	hard_local_irq_restore(flags);
}

int dovetail_handle_syscall_slow(struct pt_regs *regs)
{
	struct irq_stage *caller_stage, *this_stage, *stage;
	struct irq_stage_data *p;
	unsigned long flags;
	int ret = 0;

	/*
	 * We should definitely not pipeline a syscall through the
	 * slow path with IRQs off.
	 */
	WARN_ON_ONCE(dovetail_debug() && hard_irqs_disabled());

	flags = hard_local_irq_save();
	caller_stage = this_stage = __current_irq_stage;
	stage = head_irq_stage;
next:
	p = irq_stage_this_context(stage);
	if (likely(p->coflags & _DOVETAIL_SYSCALL_E)) {
		irq_set_current_context(p);
		p->coflags |= _DOVETAIL_SYSCALL_R;
		hard_local_irq_restore(flags);
		ret = dovetail_syscall_hook(caller_stage, regs);
		flags = hard_local_irq_save();
		p->coflags &= ~_DOVETAIL_SYSCALL_R;
		if (__current_irq_stage != stage)
			/* Account for stage migration. */
			this_stage = __current_irq_stage;
		else
			__set_current_irq_stage(this_stage);
	}

	if (this_stage == &root_irq_stage) {
		if (stage != &root_irq_stage && ret == 0) {
			stage = &root_irq_stage;
			goto next;
		}
		/*
		 * Careful: we may have migrated from head->root, so p
		 * would be irq_stage_this_context(head).
		 */
		p = irq_root_this_context();
		if (irq_staged_waiting(p))
			irq_stage_sync_current();
 	} else if (test_thread_flag(TIF_MAYDAY)) {
		clear_thread_flag(TIF_MAYDAY);
		dovetail_handle_trap(IPIPE_TRAP_MAYDAY, regs);
	}

	hard_local_irq_restore(flags);

	return ret;
}

int __weak dovetail_trap_hook(struct dovetail_trap_data *data)
{
	return 0;
}

int dovetail_handle_trap(int exception, struct pt_regs *regs)
{
	struct dovetail_trap_data data;
	struct irq_stage_data *p;
	unsigned long flags;
	int ret = 0;

	flags = hard_local_irq_save();

	/*
	 * We send a notification about all traps raised over a
	 * registered head stage only.
	 */
	if (__on_root_stage())
		goto out;

	p = irq_head_this_context();
	if (likely(p->coflags & _DOVETAIL_TRAP_E)) {
		p->coflags |= _DOVETAIL_TRAP_R;
		hard_local_irq_restore(flags);
		data.exception = exception;
		data.regs = regs;
		ret = dovetail_trap_hook(&data);
		flags = hard_local_irq_save();
		p->coflags &= ~_DOVETAIL_TRAP_R;
	}
out:
	hard_local_irq_restore(flags);

	return ret;
}

int __weak dovetail_kevent_hook(int kevent, void *data)
{
	return 0;
}

int dovetail_handle_kevent(int kevent, void *data)
{
	struct irq_stage_data *p;
	unsigned long flags;
	int ret = 0;

	ipipe_root_only();

	flags = hard_local_irq_save();

	p = irq_root_this_context();
	if (likely(p->coflags & _DOVETAIL_KEVENT_E)) {
		p->coflags |= _DOVETAIL_KEVENT_R;
		hard_local_irq_restore(flags);
		ret = dovetail_kevent_hook(kevent, data);
		flags = hard_local_irq_save();
		p->coflags &= ~_DOVETAIL_KEVENT_R;
	}

	hard_local_irq_restore(flags);

	return ret;
}

void __weak dovetail_migration_hook(struct task_struct *p)
{
}

static void complete_domain_migration(void) /* hw IRQs off */
{
	struct irq_stage_data *p;
	struct irq_pipeline_data *pd;
	struct task_struct *t;

	ipipe_root_only();
	pd = raw_cpu_ptr(&irq_pipeline);
	t = pd->task_hijacked;
	if (t == NULL)
		return;

	pd->task_hijacked = NULL;
	t->state &= ~TASK_HARDENING;
	if (t->state != TASK_INTERRUPTIBLE)
		/* Migration aborted (by signal). */
		return;

	/*
	 * hw IRQs are disabled, but the completion hook assumes the
	 * head stage is logically stalled: fix it up.
	 */
	p = irq_head_this_context();
	__set_bit(IPIPE_STALL_FLAG, &p->status);
	dovetail_migration_hook(t);
	__clear_bit(IPIPE_STALL_FLAG, &p->status);
	if (irq_staged_waiting(p))
		irq_stage_sync(p->stage);
}

void dovetail_complete_domain_migration(void)
{
	unsigned long flags;

	flags = hard_local_irq_save();
	complete_domain_migration();
	hard_local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(dovetail_complete_domain_migration);

int dovetail_context_switch_tail(void)
{
	int x;

#ifdef CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH
	hard_local_irq_disable();
#endif
	x = __on_root_stage();
	if (x)
		complete_domain_migration();
	else
		set_thread_local_flags(_TLF_HEAD);

#ifndef CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH
	if (x)
#endif
		hard_local_irq_enable();

	return !x;
}

#ifdef CONFIG_DOVETAIL_TRACK_VM_GUEST
void dovetail_hypervisor_stall(void)
{
	struct hypervisor_stall *nfy;
	struct irq_pipeline_data *p;

	ipipe_check_irqoff();
	p = raw_cpu_ptr(&irq_pipeline);
	nfy = p->vm_notifier;
	if (unlikely(nfy))
		nfy->handler(nfy);
}
EXPORT_SYMBOL_GPL(dovetail_hypervisor_stall);
#endif

void dovetail_call_mayday(struct pt_regs *regs)
{
	unsigned long flags;

	flags = hard_local_irq_save();
	clear_thread_flag(TIF_MAYDAY);
	dovetail_handle_trap(IPIPE_TRAP_MAYDAY, regs);
	hard_local_irq_restore(flags);
}

void dovetail_host_events(struct irq_stage *stage, int event_mask)
{
	struct irq_stage_data *p;
	unsigned long flags;
	int cpu, wait;

	if (stage == &root_irq_stage) {
		WARN_ON(dovetail_debug() && (event_mask & _DOVETAIL_TRAP_E));
		event_mask &= ~_DOVETAIL_TRAP_E;
	} else {
		WARN_ON(dovetail_debug() && (event_mask & _DOVETAIL_KEVENT_E));
		event_mask &= ~_DOVETAIL_KEVENT_E;
	}

	flags = irq_pipeline_lock(NULL, NULL);

	for_each_online_cpu(cpu) {
		p = irq_stage_context(stage, cpu);
		p->coflags &= ~_DOVETAIL_ALL_E;
		p->coflags |= event_mask;
	}

	wait = (event_mask ^ _DOVETAIL_ALL_E) << _DOVETAIL_SHIFT_R;
	if (wait == 0 || !__on_root_stage()) {
		irq_pipeline_unlock(flags);
		return;
	}

	irq_stage_this_context(stage)->coflags &= ~wait;

	irq_pipeline_unlock(flags);

	/*
	 * In case we cleared some hooks over the root stage, we have
	 * to wait for any ongoing execution to finish, since our
	 * caller might subsequently unmap the target stage code.
	 *
	 * We synchronize with the relevant __ipipe_notify_*()
	 * helpers, disabling all hooks before we start waiting for
	 * completion on all CPUs.
	 */
	for_each_online_cpu(cpu) {
		while (irq_stage_context(stage, cpu)->coflags & wait)
			schedule_timeout_interruptible(HZ / 50);
	}
}
EXPORT_SYMBOL_GPL(dovetail_host_events);

#ifdef CONFIG_KGDB
bool __ipipe_probe_access;

long ipipe_probe_kernel_read(void *dst, void *src, size_t size)
{
	long ret;
	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);
	__ipipe_probe_access = true;
	barrier();
	ret = __copy_from_user_inatomic(dst,
			(__force const void __user *)src, size);
	barrier();
	__ipipe_probe_access = false;
	set_fs(old_fs);

	return ret ? -EFAULT : 0;
}

long ipipe_probe_kernel_write(void *dst, void *src, size_t size)
{
	long ret;
	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);
	__ipipe_probe_access = true;
	barrier();
	ret = __copy_to_user_inatomic((__force void __user *)dst, src, size);
	barrier();
	__ipipe_probe_access = false;
	set_fs(old_fs);

	return ret ? -EFAULT : 0;
}
#endif /* CONFIG_KGDB */

#if defined(CONFIG_DEBUG_ATOMIC_SLEEP) || defined(CONFIG_PROVE_LOCKING) || \
	defined(CONFIG_PREEMPT_VOLUNTARY) || defined(CONFIG_DOVETAIL_DEBUG)
void __ipipe_uaccess_might_fault(void)
{
	struct irq_stage *stage;
	unsigned long flags;

	flags = hard_local_irq_save();
	stage = __current_irq_stage;
	if (stage == &root_irq_stage) {
		hard_local_irq_restore(flags);
		might_fault();
		return;
	}

	WARN_ON_ONCE(dovetail_debug() &&
		     (hard_irqs_disabled_flags(flags) 
		      || test_bit(IPIPE_STALL_FLAG,
				  &irq_stage_this_context(stage)->status)));
	hard_local_irq_restore(flags);
	
}
EXPORT_SYMBOL_GPL(__ipipe_uaccess_might_fault);
#endif

#ifdef CONFIG_DOVETAIL_HAVE_HOSTRT
/*
 * NOTE: The architecture specific code must only call this function
 * when a clocksource suitable for CLOCK_HOST_REALTIME is enabled.
 * The event receiver is responsible for providing proper locking.
 */
void dovetail_update_hostrt(struct timekeeper *tk)
{
	struct tk_read_base *tkr = &tk->tkr_mono;
	struct clocksource *clock = tkr->clock;
	struct ipipe_hostrt_data data;
	struct timespec xt;

	xt.tv_sec = tk->xtime_sec;
	xt.tv_nsec = (long)(tkr->xtime_nsec >> tkr->shift);
	ipipe_root_only();
	data.live = 1;
	data.cycle_last = tkr->cycle_last;
	data.mask = clock->mask;
	data.mult = tkr->mult;
	data.shift = tkr->shift;
	data.wall_time_sec = xt.tv_sec;
	data.wall_time_nsec = xt.tv_nsec;
	data.wall_to_monotonic.tv_sec = tk->wall_to_monotonic.tv_sec;
	data.wall_to_monotonic.tv_nsec = tk->wall_to_monotonic.tv_nsec;
	dovetail_handle_kevent(KEVENT_HOSTRT, &data);
}

#endif /* CONFIG_DOVETAIL_HAVE_HOSTRT */
