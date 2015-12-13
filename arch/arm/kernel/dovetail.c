/* -*- linux-c -*-
 * arch/arm/kernel/dovetail.c
 *
 * Copyright (C) 2006-2008 Gilles Chanteperdrix.
 */
#include <linux/export.h>
#include <asm/mmu_context.h>

void __switch_mm_inner(struct mm_struct *prev, struct mm_struct *next,
		       struct task_struct *tsk)
{
	struct mm_struct ** const active_mm =
		raw_cpu_ptr(&irq_pipeline.active_mm);
#ifdef CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH
	struct thread_info *const tip = current_thread_info();
	prev = *active_mm;
	clear_bit(TIF_MMSWITCH_INT, &tip->flags);
	barrier();
	*active_mm = NULL;
	barrier();
	for (;;) {
		unsigned long flags;
#endif /* CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH */

		int rc __maybe_unused = __do_switch_mm(prev, next, tsk, true);

#ifdef CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH
		/*
		 * Reading thread_info flags and setting active_mm
		 * must be done atomically.
		 */
		flags = hard_local_irq_save();
		if (__test_and_clear_bit(TIF_MMSWITCH_INT, &tip->flags) == 0) {
			if (rc < 0)
				*active_mm = prev;
			else {
				*active_mm = next;
				fcse_switch_mm_end(next);
			}
			hard_local_irq_restore(flags);
			return;
		}
		hard_local_irq_restore(flags);

		if (rc < 0)
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
	if (rc < 0)
		*active_mm = prev;
	else {
		*active_mm = next;
		fcse_switch_mm_end(next);
	}
#endif /* !IPIPE_WANT_PREEMPTIBLE_SWITCH */
}

#ifdef finish_arch_post_lock_switch
void deferred_switch_mm(struct mm_struct *next)
{
	struct mm_struct ** const active_mm =
		raw_cpu_ptr(&irq_pipeline.active_mm);
	struct mm_struct *prev = *active_mm;
#ifdef CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH
	struct thread_info *const tip = current_thread_info();
	clear_bit(TIF_MMSWITCH_INT, &tip->flags);
	barrier();
	*active_mm = NULL;
	barrier();
	for (;;) {
		unsigned long flags;
#endif /* CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH */

		__do_switch_mm(prev, next, NULL, false);

#ifdef CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH
		/*
		 * Reading thread_info flags and setting active_mm
		 * must be done atomically.
		 */
		flags = hard_local_irq_save();
		if (__test_and_clear_bit(TIF_MMSWITCH_INT, &tip->flags) == 0) {
			*active_mm = next;
			fcse_switch_mm_end(next);
			hard_local_irq_restore(flags);
			return;
		}
		hard_local_irq_restore(flags);
		prev = NULL;
	}
#else
	*active_mm = next;
	fcse_switch_mm_end(next);
#endif /* CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH */
}
#endif

#ifndef MULTI_CPU
EXPORT_SYMBOL_GPL(cpu_do_switch_mm);
#endif
#ifdef CONFIG_CPU_HAS_ASID
EXPORT_SYMBOL_GPL(check_and_switch_context);
#endif /* CONFIG_CPU_HAS_ASID */
EXPORT_SYMBOL_GPL(init_mm);
EXPORT_SYMBOL_GPL(__check_vmalloc_seq);
