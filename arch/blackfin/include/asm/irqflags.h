/*
 * interface to Blackfin CEC
 *
 * Copyright 2009 Analog Devices Inc.
 * Licensed under the GPL-2 or later.
 */

#ifndef __ASM_BFIN_IRQFLAGS_H__
#define __ASM_BFIN_IRQFLAGS_H__

#include <mach/blackfin.h>

#ifdef CONFIG_SMP
# include <asm/pda.h>
# include <asm/processor.h>
# define bfin_irq_flags cpu_pda[blackfin_core_id()].imask
#else
extern unsigned long bfin_irq_flags;
#endif

static inline notrace void bfin_sti(unsigned long flags)
{
	asm volatile("sti %0;" : : "d" (flags));
}

static inline notrace unsigned long bfin_cli(void)
{
	unsigned long flags;
	asm volatile("cli %0;" : "=d" (flags));
	return flags;
}

#ifdef CONFIG_DEBUG_HWERR
# define bfin_no_irqs 0x3f
#else
# define bfin_no_irqs 0x1f
#endif

/*****************************************************************************/
/*
 * Hard, untraced CPU interrupt flag manipulation and access.
 */
static inline notrace void __hard_local_irq_disable(void)
{
	bfin_cli();
}

static inline notrace void __hard_local_irq_enable(void)
{
	bfin_sti(bfin_irq_flags);
}

static inline notrace unsigned long hard_local_save_flags(void)
{
	return bfin_read_IMASK();
}

static inline notrace unsigned long __hard_local_irq_save(void)
{
	unsigned long flags;
	flags = bfin_cli();
#ifdef CONFIG_DEBUG_HWERR
	bfin_sti(0x3f);
#endif
	return flags;
}

static inline notrace int hard_irqs_disabled_flags(unsigned long flags)
{
#ifdef CONFIG_BF60x
	return (flags & IMASK_IVG11) == 0;
#else
	return (flags & ~0x3f) == 0;
#endif
}

static inline notrace int hard_irqs_disabled(void)
{
	unsigned long flags = hard_local_save_flags();
	return hard_irqs_disabled_flags(flags);
}

static inline notrace void __hard_local_irq_restore(unsigned long flags)
{
	if (!hard_irqs_disabled_flags(flags))
		__hard_local_irq_enable();
}

/*****************************************************************************/
/*
 * Interrupt pipe handling.
 */
#ifdef CONFIG_IPIPE

#include <linux/compiler.h>
#include <asm/irq_pipeline.h>

/*
 * Interrupt pipe interface to linux/irqflags.h.
 */
static inline notrace void arch_local_irq_disable(void)
{
	root_irq_disable();
	barrier();
}

static inline notrace void arch_local_irq_enable(void)
{
	barrier();
	root_irq_enable();
}

static inline notrace unsigned long arch_local_save_flags(void)
{
	return root_irqs_disabled() ? bfin_no_irqs : bfin_irq_flags;
}

static inline notrace int arch_irqs_disabled_flags(unsigned long flags)
{
	return flags == bfin_no_irqs;
}

static inline notrace unsigned long arch_local_irq_save(void)
{
	unsigned long flags;

	flags = root_irq_save() ? bfin_no_irqs : bfin_irq_flags;
	barrier();

	return flags;
}

static inline notrace void arch_local_irq_restore(unsigned long flags)
{
	root_irq_restore(flags == bfin_no_irqs);
}

static inline notrace unsigned long arch_mangle_irq_bits(int virt, unsigned long real)
{
	/*
	 * Merge virtual and real interrupt mask bits into a single
	 * 32bit word.
	 */
	return (real & ~(1 << 31)) | ((virt != 0) << 31);
}

static inline notrace int arch_demangle_irq_bits(unsigned long *x)
{
	int virt = (*x & (1 << 31)) != 0;
	*x &= ~(1L << 31);
	return virt;
}

/*
 * Interface to various arch routines that may be traced.
 */
# define hard_local_irq_disable()	__hard_local_irq_disable()
# define hard_local_irq_enable()	__hard_local_irq_enable()
# define hard_local_irq_save()		__hard_local_irq_save()
# define hard_local_irq_restore(flags)	__hard_local_irq_restore(flags)

static inline notrace unsigned long hard_local_irq_save_notrace(void)
{
	return __hard_local_irq_save();
}

static inline notrace void hard_local_irq_restore_notrace(unsigned long flags)
{
	return __hard_local_irq_restore(flags);
}

static inline notrace void hard_local_irq_disable_notrace(void)
{
	return __hard_local_irq_disable();
}

static inline notrace void hard_local_irq_enable_notrace(void)
{
	return __hard_local_irq_enable();
}

static inline
void arch_save_timer_regs(struct pt_regs *dst,
			  struct pt_regs *src, bool head_context)
{
	dst->ipend = src->ipend;
	dst->pc = src->pc;
	if (head_context)
		dst->ipend &= ~0x10;
	else
		dst->ipend |= 0x10;
}

#else /* !CONFIG_IPIPE */

/*
 * Direct interface to linux/irqflags.h.
 */
#define arch_local_save_flags()		hard_local_save_flags()
#define arch_local_irq_save()		__hard_local_irq_save()
#define arch_local_irq_restore(flags)	__hard_local_irq_restore(flags)
#define arch_local_irq_enable()		__hard_local_irq_enable()
#define arch_local_irq_disable()	__hard_local_irq_disable()
#define arch_irqs_disabled_flags(flags)	hard_irqs_disabled_flags(flags)
#define arch_irqs_disabled()		hard_irqs_disabled()

/*
 * Interface to various arch routines that may be traced.
 */
#define hard_local_irq_save()		__hard_local_irq_save()
#define hard_local_irq_restore(flags)	__hard_local_irq_restore(flags)
#define hard_local_irq_enable()		__hard_local_irq_enable()
#define hard_local_irq_disable()	__hard_local_irq_disable()

#endif /* !CONFIG_IPIPE */

#if defined(CONFIG_SMP) && defined(CONFIG_IPIPE)
#define hard_smp_local_irq_save()		hard_local_irq_save()
#define hard_smp_local_irq_restore(flags)	hard_local_irq_restore(flags)
#else
#define hard_smp_local_irq_save()		hard_local_save_flags()
#define hard_smp_local_irq_restore(flags)	do { (void)(flags); } while (0)
#endif

#endif
