#ifndef __ASM_POWERPC_MMU_CONTEXT_H
#define __ASM_POWERPC_MMU_CONTEXT_H
#ifdef __KERNEL__

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <asm/mmu.h>	
#include <asm/cputable.h>
#include <asm-generic/mm_hooks.h>
#include <asm/cputhreads.h>

/*
 * Most if the context management is out of line
 */
extern int init_new_context(struct task_struct *tsk, struct mm_struct *mm);
extern void destroy_context(struct mm_struct *mm);

extern void switch_mmu_context(struct mm_struct *prev, struct mm_struct *next);
extern void switch_slb(struct task_struct *tsk, struct mm_struct *mm);
extern void set_context(unsigned long id, pgd_t *pgd);

#ifdef CONFIG_PPC_BOOK3S_64
extern int __init_new_context(void);
extern void __destroy_context(int context_id);
static inline void mmu_context_init(void) { }
#else
extern unsigned long __init_new_context(void);
extern void __destroy_context(unsigned long context_id);
extern void mmu_context_init(void);
#endif

extern void switch_cop(struct mm_struct *next);
extern int use_cop(unsigned long acop, struct mm_struct *mm);
extern void drop_cop(unsigned long acop, struct mm_struct *mm);

/*
 * switch_mm is the entry point called from the architecture independent
 * code in kernel/sched/core.c
 */
static inline void __do_switch_mm(struct mm_struct *prev, struct mm_struct *next,
				  struct task_struct *tsk, bool irq_sync_p)
{
	/* Mark this context has been used on the new CPU */
	cpumask_set_cpu(raw_smp_processor_id(), mm_cpumask(next));

	/* 32-bit keeps track of the current PGDIR in the thread struct */
#ifdef CONFIG_PPC32
	tsk->thread.pgdir = next->pgd;
#endif /* CONFIG_PPC32 */

	/* 64-bit Book3E keeps track of current PGD in the PACA */
#ifdef CONFIG_PPC_BOOK3E_64
	get_paca()->pgd = next->pgd;
#endif
	/* Nothing else to do if we aren't actually switching */
	if (prev == next)
		return;

	if (irq_sync_p)
		hard_local_irq_enable();
	
#ifdef CONFIG_PPC_ICSWX
	/* Switch coprocessor context only if prev or next uses a coprocessor */
	if (prev->context.acop || next->context.acop)
		switch_cop(next);
#endif /* CONFIG_PPC_ICSWX */

	/* We must stop all altivec streams before changing the HW
	 * context
	 */
#ifdef CONFIG_ALTIVEC
	if (cpu_has_feature(CPU_FTR_ALTIVEC))
		asm volatile ("dssall");
#endif /* CONFIG_ALTIVEC */

	/* The actual HW switching method differs between the various
	 * sub architectures.
	 */
#ifdef CONFIG_PPC_STD_MMU_64
	switch_slb(tsk, next);
#else
	/* Out of line for now */
	switch_mmu_context(prev, next);
#endif
	if (irq_sync_p)
		hard_local_irq_disable();
}

static inline void __switch_mm(struct mm_struct *prev, struct mm_struct *next,
			       struct task_struct *tsk)
{
#ifdef CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH
#if defined(CONFIG_PPC_MMU_NOHASH) && defined(CONFIG_SMP)
 /*
  * mmu_context_nohash in SMP mode is tracking an activity counter
  * into the mm struct. Therefore, we make sure the kernel always sees
  * the irq_pipeline.active_mm update and the actual switch as a
  * single atomic operation. Since the related code already requires
  * to hard disable irqs all through the switch, there is no
  * additional penalty anyway.
  */
#define mmswitch_irq_sync false
#else
#define mmswitch_irq_sync true
#endif
	WARN_ON_ONCE(dovetail_debug() && hard_irqs_disabled());
	for (;;) {
		hard_local_irq_disable();
		__this_cpu_write(irq_pipeline.active_mm, NULL);
		barrier();
		__do_switch_mm(prev, next, tsk, mmswitch_irq_sync);
		if (!test_and_clear_thread_flag(TIF_MMSWITCH_INT)) {
			__this_cpu_write(irq_pipeline.active_mm, next);
			hard_local_irq_enable();
			return;
		}
	}
#else /* !CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH */
	WARN_ON_ONCE(dovetail_debug() && !hard_irqs_disabled());
	__do_switch_mm(prev, next, tsk, false);
#endif /* !CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH */
}

#ifdef CONFIG_IPIPE
/*
 * ipipe_switch_mm_head() is reserved to the head domain for switching
 * mmu context.
 */
static inline
void ipipe_switch_mm_head(struct mm_struct *prev, struct mm_struct *next,
			  struct task_struct *tsk)
{
	unsigned long flags;

	flags = hard_local_irq_save();
	__do_switch_mm(prev, next, tsk, false);
	hard_local_irq_restore(flags);
}

#endif /* CONFIG_IPIPE */

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
			     struct task_struct *tsk)
{
	unsigned long flags;

	dovetail_switch_mm_enter(flags);
	__switch_mm(prev, next, tsk);
	dovetail_switch_mm_exit(flags);
}

#define deactivate_mm(tsk,mm)	do { } while (0)

/*
 * After we have set current->mm to a new value, this activates
 * the context for the new mm so we see the new mappings.
 */
static inline void activate_mm(struct mm_struct *prev, struct mm_struct *next)
{
#ifndef CONFIG_IPIPE
	unsigned long flags;

	local_irq_save(flags);
#endif
	switch_mm(prev, next, current);
#ifndef CONFIG_IPIPE
	local_irq_restore(flags);
#endif
}

/* We don't currently use enter_lazy_tlb() for anything */
static inline void enter_lazy_tlb(struct mm_struct *mm,
				  struct task_struct *tsk)
{
	/* 64-bit Book3E keeps track of current PGD in the PACA */
#ifdef CONFIG_PPC_BOOK3E_64
	get_paca()->pgd = NULL;
#endif
}

#endif /* __KERNEL__ */
#endif /* __ASM_POWERPC_MMU_CONTEXT_H */
