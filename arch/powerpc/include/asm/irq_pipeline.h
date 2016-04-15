/*
 * arch/powerpc/include/asm/irq_pipeline.h
 *
 * Copyright (C) 2009 Philippe Gerum.
 */
#ifndef _ASM_POWERPC_IRQ_PIPELINE_H
#define _ASM_POWERPC_IRQ_PIPELINE_H

#define IPIPE_NR_XIRQS		CONFIG_NR_IRQS

#include <asm-generic/irq_pipeline.h>

#ifdef CONFIG_IRQ_PIPELINE

#define IPIPE_CORE_RELEASE	1

struct ipipe_arch_sysinfo {
};

#define __ipipe_cpu_freq	ppc_proc_freq

#define DECREMENTER_IRQ		ipipe_get_platform_sirq(0) /* from the virtual domain. */

#ifdef CONFIG_SMP
#define IPIPE_MSG_CRITICAL_IPI		0
#define IPIPE_MSG_HRTIMER_IPI		1
#define IPIPE_MSG_RESCHEDULE_IPI	2
#define IPIPE_MSG_IPI_MASK	(BIT(IPIPE_MSG_CRITICAL_IPI) |	\
				 BIT(IPIPE_MSG_HRTIMER_IPI)  |	\
				 BIT(IPIPE_MSG_RESCHEDULE_IPI))
#define IPIPE_CRITICAL_IPI	ipipe_get_platform_sirq(1)
#define IPIPE_HRTIMER_IPI	ipipe_get_platform_sirq(2)
#define IPIPE_RESCHEDULE_IPI	ipipe_get_platform_sirq(3)
#define IPIPE_BASE_IPI_OFFSET	IPIPE_CRITICAL_IPI

void smp_ipi_pipeline_demux(int irq);
#endif /* CONFIG_SMP */

#ifdef CONFIG_PPC64
/*
 * The built-in soft disabling mechanism is diverted to the pipeline
 * when interrupt pipelining is enabled, therefore we won't hard
 * disable waiting for soft enabling.
 */
static inline bool lazy_irq_pending(void)
{
	return false;
}

static inline void may_hard_irq_enable(void) { }

static inline void hard_irq_disable(void)
{
	__hard_irq_disable();
}

#endif /* CONFIG_PPC64 */

static inline unsigned long arch_local_irq_disable(void)
{
	unsigned long flags;

	flags = (!root_irq_save()) << MSR_EE_LG;
	barrier();

	return flags;
}

static inline void arch_local_irq_enable(void)
{
	barrier();
	root_irq_enable();
}

static inline unsigned long arch_local_irq_save(void)
{
	return arch_local_irq_disable();
}

static inline unsigned long arch_local_save_flags(void)
{
	return (!root_irqs_disabled()) << MSR_EE_LG;
}

static inline int arch_irqs_disabled_flags(unsigned long flags)
{
	return native_irqs_disabled_flags(flags);
}

static inline bool arch_irq_disabled_regs(struct pt_regs *regs)
{
	return arch_irqs_disabled_flags(regs->msr);
}

static inline void arch_local_irq_restore(unsigned long flags)
{
	barrier();
	if (!arch_irqs_disabled_flags(flags))
		root_irq_enable();
}

static inline int arch_irqs_disabled(void)
{
	unsigned long flags = arch_local_save_flags();

	return arch_irqs_disabled_flags(flags);
}

static inline unsigned long arch_mangle_irq_bits(int stalled, unsigned long msr)
{
	/* Merge virtual and real interrupt mask bits. */
	return (msr & ~MSR_VIRTEE) | ((long)(stalled == 0) << MSR_VIRTEE_LG);
}

static inline int arch_demangle_irq_bits(unsigned long *flags)
{
	int stalled = (*flags & MSR_VIRTEE) == 0;

	*flags &= ~MSR_VIRTEE;

	return stalled;
}

static inline
void arch_save_timer_regs(struct pt_regs *dst,
			  struct pt_regs *src, bool head_context)
{
	dst->msr = src->msr;
	dst->nip = src->nip;
	if (head_context)
		dst->msr &= ~MSR_EE;
}

static inline bool arch_is_root_tick(struct pt_regs *regs)
{
	return ((regs)->msr & MSR_EE) != 0;
}

#ifdef CONFIG_PPC64
#define ipipe_read_tsc(t)	(t = mftb())
#define ipipe_tsc2ns(t)		(((t) * 1000UL) / (ppc_tb_freq / 1000000UL))
#define ipipe_tsc2us(t)		((t) / (ppc_tb_freq / 1000000UL))
#else /* CONFIG_PPC32 */
#define ipipe_read_tsc(t)					\
	({							\
		unsigned long __tbu;				\
		__asm__ __volatile__ ("1: mftbu %0\n"		\
				      "mftb %1\n"		\
				      "mftbu %2\n"		\
				      "cmpw %2,%0\n"		\
				      "bne- 1b\n"		\
				      :"=r" (((unsigned long *)&t)[0]),	\
				       "=r" (((unsigned long *)&t)[1]),	\
				       "=r" (__tbu));			\
		t;							\
	})

#define ipipe_tsc2ns(t)	\
	((((unsigned long)(t)) * 1000) / (ppc_tb_freq / 1000000))

#define ipipe_tsc2us(t)						\
	({							\
		unsigned long long delta = (t);			\
		do_div(delta, ppc_tb_freq/1000000+1);		\
		(unsigned long)delta;				\
	})
#endif /* CONFIG_PPC32 */

extern int ipipe_platform_sirqs[];

static inline int ipipe_get_platform_sirq(int nth)
{
	return ipipe_platform_sirqs[nth];
}

int enter_irq_pipeline(struct pt_regs *regs);

int timer_enter_pipeline(struct pt_regs *regs);

#else /* !CONFIG_IRQ_PIPELINE */

#define DECREMENTER_IRQ		0	/* None. */

static inline void smp_ipi_pipeline_demux(int irq) { }
static inline int enter_irq_pipeline(struct pt_regs *regs) { return 1; }
static inline int timer_enter_pipeline(struct pt_regs *regs) { return 1; }

#endif /* !CONFIG_IRQ_PIPELINE */

#endif /* _ASM_POWERPC_IRQ_PIPELINE_H */
