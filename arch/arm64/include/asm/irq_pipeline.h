/*
 * arch/arm64/include/asm/irq_pipeline.h
 *
 * Copyright (C) 2002-2016 Philippe Gerum.
 * Copyright (C) 2005 Stelian Pop.
 * Copyright (C) 2006-2008 Gilles Chanteperdrix.
 * Copyright (C) 2010 Philippe Gerum (SMP port).
 */
#ifndef _ASM_ARM64_IRQ_PIPELINE_H
#define _ASM_ARM64_IRQ_PIPELINE_H

#define IPIPE_NR_XIRQS		3072

#include <asm-generic/irq_pipeline.h>

#ifdef CONFIG_IRQ_PIPELINE

#define IPIPE_CORE_RELEASE	1

#include <asm/irq.h>

#define IPIPE_IPI_BASE		NR_IRQS
#define IPIPE_CRITICAL_IPI	(IPIPE_IPI_BASE + IPI_PIPELINE_CRITICAL)
#define IPIPE_HRTIMER_IPI	(IPIPE_IPI_BASE + IPI_PIPELINE_HRTIMER)
#define IPIPE_RESCHEDULE_IPI	(IPIPE_IPI_BASE + IPI_PIPELINE_RESCHEDULE)

#ifdef CONFIG_SMP_ON_UP
#define ipipe_smp_p 	static_key_true(&__ipipe_smp_key)
extern struct static_key __ipipe_smp_key;
#endif

#define IPIPE_TSC_TYPE_NONE			0
#define IPIPE_TSC_TYPE_FREERUNNING		1
#define IPIPE_TSC_TYPE_DECREMENTER		2
#define IPIPE_TSC_TYPE_FREERUNNING_COUNTDOWN	3
#define IPIPE_TSC_TYPE_FREERUNNING_TWICE	4
#define IPIPE_TSC_TYPE_FREERUNNING_ARCH		5

/* tscinfo, exported to user-space */
struct __ipipe_tscinfo {
	unsigned type;
	unsigned freq;
	unsigned long counter_vaddr;
	union {
		struct {
			unsigned long counter_paddr;
			unsigned long long mask;
		};
		struct {
			unsigned *counter; /* Hw counter physical address */
			unsigned long long mask; /* Significant bits in the hw counter. */
			unsigned long long *tsc; /* 64 bits tsc value. */
		} fr;
		struct {
			unsigned *counter; /* Hw counter physical address */
			unsigned long long mask; /* Significant bits in the hw counter. */
			unsigned *last_cnt; /* Counter value when updating
						tsc value. */
			unsigned long long *tsc; /* 64 bits tsc value. */
		} dec;
	} u;
};

struct ipipe_arch_sysinfo {
	struct __ipipe_tscinfo tsc;
};

void __ipipe_mach_get_tscinfo(struct __ipipe_tscinfo *info);

static inline void __ipipe_mach_update_tsc(void) {}

unsigned long long __ipipe_tsc_get(void);
void __ipipe_tsc_register(struct __ipipe_tscinfo *info);

static inline bool arch_is_root_tick(struct pt_regs *regs)
{
	return !arch_irqs_disabled_flags(regs->pstate);
}

struct task_struct *ipipe_switch_to(struct task_struct *prev,
				    struct task_struct *next);

static inline notrace unsigned long arch_local_irq_save(void)
{
	unsigned long flags = root_irq_save() << 7; /* PSR_I_BIT */
	barrier();
	return flags;
}

static inline notrace void arch_local_irq_enable(void)
{
	barrier();
	root_irq_enable();
}

static inline notrace void arch_local_irq_disable(void)
{
	root_irq_disable();
	barrier();
}

static inline notrace unsigned long arch_local_save_flags(void)
{
	unsigned long flags = root_irqs_disabled() << 7; /* PSR_I_BIT */
	barrier();
	return flags;
}

static inline int arch_irqs_disabled_flags(unsigned long flags)
{
	return native_irqs_disabled_flags(flags);
}

static inline notrace void arch_local_irq_restore(unsigned long flags)
{
	if (!arch_irqs_disabled_flags(flags))
		arch_local_irq_enable();
}

static inline unsigned long arch_mangle_irq_bits(int virt, unsigned long real)
{
	/* Merge virtual and real interrupt mask bits into a single
	   32bit word. */
	return (real & ~(1L << 8)) | ((virt != 0) << 8);
}

static inline int arch_demangle_irq_bits(unsigned long *x)
{
	int virt = (*x & (1 << 8)) != 0;
	*x &= ~(1L << 8);
	return virt;
}

static inline
void arch_save_timer_regs(struct pt_regs *dst,
			  struct pt_regs *src, bool head_context)
{
	dst->pstate = src->pstate;
	dst->pc = src->pc;
	if (head_context)
		dst->pstate |= PSR_I_BIT;
}

#define __ipipe_tsc_update()	do { } while(0)

#define ipipe_read_tsc(t)	do { t = __ipipe_tsc_get(); } while(0)

#define ipipe_tsc2ns(t) \
({ \
	unsigned long long delta = (t)*1000; \
	do_div(delta, __ipipe_hrclock_freq / 1000000 + 1); \
	(unsigned long)delta; \
})
#define ipipe_tsc2us(t) \
({ \
	unsigned long long delta = (t); \
	do_div(delta, __ipipe_hrclock_freq / 1000000 + 1); \
	(unsigned long)delta; \
})

#endif /* CONFIG_IRQ_PIPELINE */

#endif /* _ASM_ARM64_IRQ_PIPELINE_H */
