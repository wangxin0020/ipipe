/* -*- linux-c -*-
 * arch/arm64/kernel/dovetail.c
 *
 * Copyright (C) 2006-2008 Gilles Chanteperdrix.
 * Copyright (C) 2016      Philippe Gerum <rpm@xenomai.org>.
 */
#include <linux/export.h>
#include <asm/mmu_context.h>

void __switch_mm_inner(struct mm_struct *prev, struct mm_struct *next,
		       struct task_struct *tsk)
{
	struct mm_struct ** const active_mm =
		raw_cpu_ptr(&irq_pipeline.active_mm);
	int ret;
#ifdef CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH
	struct thread_info *const tip = current_thread_info();
	unsigned long flags;

	prev = *active_mm;
	clear_bit(TIF_MMSWITCH_INT, &tip->flags);
	barrier();
	*active_mm = NULL;
	barrier();

	for (;;) {
		ret = __do_switch_mm(prev, next, tsk, true);
		/*
		 * Reading thread_info flags and setting active_mm
		 * must be done atomically.
		 */
		flags = hard_local_irq_save();
		if (__test_and_clear_bit(TIF_MMSWITCH_INT, &tip->flags) == 0) {
			*active_mm = ret < 0 ? prev : next;
			hard_local_irq_restore(flags);
			return;
		}
		hard_local_irq_restore(flags);

		if (ret < 0)
			/*
			 * We were interrupted by head domain, which
			 * may have changed the mm context, mm context
			 * is now unknown, but will be switched in
			 * deferred_switch_mm
			 */
			return;

		prev = NULL;
	}
#else
	ret = __do_switch_mm(prev, next, tsk, true);
	*active_mm = ret < 0 ? prev : next;
#endif	/* CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH */
}

#ifdef finish_arch_post_lock_switch
void deferred_switch_mm(struct mm_struct *next)
{
	struct mm_struct ** const active_mm =
		raw_cpu_ptr(&irq_pipeline.active_mm);
	struct mm_struct *prev = *active_mm;
#ifdef CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH
	struct thread_info *const tip = current_thread_info();
	unsigned long flags;

	clear_bit(TIF_MMSWITCH_INT, &tip->flags);
	barrier();
	*active_mm = NULL;
	barrier();

	for (;;) {
		__do_switch_mm(prev, next, NULL, false);
		/*
		 * Reading thread_info flags and setting active_mm
		 * must be done atomically.
		 */
		flags = hard_local_irq_save();
		if (__test_and_clear_bit(TIF_MMSWITCH_INT, &tip->flags) == 0) {
			*active_mm = next;
			hard_local_irq_restore(flags);
			return;
		}
		hard_local_irq_restore(flags);
		prev = NULL;
	}
#else
	__do_switch_mm(prev, next, NULL, false);
	*active_mm = next;
#endif	/* CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH */
}
#endif	/* finish_arch_post_lock_switch */

#ifndef MULTI_CPU
EXPORT_SYMBOL_GPL(cpu_do_switch_mm);
#endif
EXPORT_SYMBOL_GPL(init_mm);
