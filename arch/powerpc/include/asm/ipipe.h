/*
 *   include/asm-powerpc/ipipe.h
 *
 *   I-pipe 32/64bit merge - Copyright (C) 2007 Philippe Gerum.
 *   I-pipe PA6T support - Copyright (C) 2007 Philippe Gerum.
 *   I-pipe 64-bit PowerPC port - Copyright (C) 2005 Heikki Lindholm.
 *   I-pipe PowerPC support - Copyright (C) 2002-2005 Philippe Gerum.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 *   USA; either version 2 of the License, or (at your option) any later
 *   version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __ASM_POWERPC_IPIPE_H
#define __ASM_POWERPC_IPIPE_H

#ifdef CONFIG_IPIPE

#include <asm/ptrace.h>
#include <asm/hw_irq.h>
#include <asm/irq.h>
#include <asm/bitops.h>
#include <asm/time.h>
#include <linux/ipipe_percpu.h>
#include <linux/list.h>
#include <linux/cpumask.h>
#include <linux/cache.h>
#include <linux/threads.h>

#ifdef CONFIG_PPC64
#ifdef CONFIG_PPC_ISERIES
#error "I-pipe: IBM I-series not supported, sorry"
#endif
#include <asm/paca.h>
#endif

#define IPIPE_ARCH_STRING	"2.9-01"
#define IPIPE_MAJOR_NUMBER	2
#define IPIPE_MINOR_NUMBER	9
#define IPIPE_PATCH_NUMBER	1

#ifdef CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH

#define prepare_arch_switch(next)			\
	do {						\
		local_irq_enable_hw();			\
		ipipe_schedule_notify(current ,next);	\
	} while(0)

#define task_hijacked(p)						\
	({								\
		unsigned long __flags__;				\
		int __x__;						\
		local_irq_save_hw_smp(__flags__);			\
		__x__ = __ipipe_root_domain_p;				\
		__clear_bit(IPIPE_SYNC_FLAG, &ipipe_root_cpudom_var(status)); \
		local_irq_restore_hw_smp(__flags__);			\
		!__x__;							\
	})

#else /* !CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH */

#define prepare_arch_switch(next)			\
	do {						\
		ipipe_schedule_notify(current ,next);	\
		local_irq_disable_hw();			\
	} while(0)

#define task_hijacked(p)						\
	({								\
		int __x__ = __ipipe_root_domain_p;			\
		__clear_bit(IPIPE_SYNC_FLAG, &ipipe_root_cpudom_var(status)); \
		if (__x__) local_irq_enable_hw(); !__x__;		\
	})

#endif /* !CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH */

struct ipipe_domain;

struct ipipe_sysinfo {
	int sys_nr_cpus;	/* Number of CPUs on board */
	int sys_hrtimer_irq;	/* hrtimer device IRQ */
	u64 sys_hrtimer_freq;	/* hrtimer device frequency */
	u64 sys_hrclock_freq;	/* hrclock device frequency */
	u64 sys_cpu_freq;	/* CPU frequency (Hz) */
};

#ifdef CONFIG_DEBUGGER
extern cpumask_t __ipipe_dbrk_pending;
#endif

#ifdef CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH
struct mm;
DECLARE_PER_CPU(struct mm_struct *, ipipe_active_mm);
#define ipipe_mm_switch_protect(flags)					\
	do {								\
		preempt_disable();					\
		per_cpu(ipipe_active_mm, smp_processor_id()) = NULL;	\
		barrier();						\
		(void)(flags);						\
	} while(0)
#define ipipe_mm_switch_unprotect(flags)				\
	do {								\
		preempt_enable();					\
		(void)(flags);						\
	} while(0)
#else
#define ipipe_mm_switch_protect(flags)		local_irq_save_hw_cond(flags)
#define ipipe_mm_switch_unprotect(flags)	local_irq_restore_hw_cond(flags)
#endif

#define __ipipe_hrtimer_irq	IPIPE_TIMER_VIRQ
#define __ipipe_hrtimer_freq	ppc_tb_freq
#define __ipipe_hrclock_freq	__ipipe_hrtimer_freq
#define __ipipe_cpu_freq	ppc_proc_freq

#ifdef CONFIG_PPC64
#define ipipe_read_tsc(t)	(t = mftb())
#define ipipe_tsc2ns(t)		(((t) * 1000UL) / (__ipipe_cpu_freq / 1000000UL))
#define ipipe_tsc2us(t)		((t) / (__ipipe_cpu_freq / 1000000UL))
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
	((((unsigned long)(t)) * 1000) / (__ipipe_cpu_freq / 1000000))

#define ipipe_tsc2us(t)						\
	({							\
		unsigned long long delta = (t);			\
		do_div(delta, __ipipe_cpu_freq/1000000+1);	\
		(unsigned long)delta;				\
	})
#endif /* CONFIG_PPC32 */

/* Private interface -- Internal use only */

#define __ipipe_check_platform()		do { } while(0)
#define __ipipe_enable_irq(irq)			enable_irq(irq)
#define __ipipe_disable_irq(irq)		disable_irq(irq)
#define __ipipe_disable_irqdesc(ipd, irq)	do { } while(0)

void __ipipe_enable_irqdesc(struct ipipe_domain *ipd, unsigned irq);

void __ipipe_init_platform(void);

void __ipipe_enable_pipeline(void);

void __ipipe_end_irq(unsigned irq);

static inline int __ipipe_check_tickdev(const char *devname)
{
	return 1;
}

#ifdef CONFIG_SMP
struct ipipe_ipi_struct {
	volatile unsigned long value;
} ____cacheline_aligned;

void __ipipe_hook_critical_ipi(struct ipipe_domain *ipd);

void __ipipe_register_ipi(unsigned int irq);
#else
#define __ipipe_hook_critical_ipi(ipd)	do { } while(0)
#endif /* CONFIG_SMP */

DECLARE_PER_CPU(struct pt_regs, __ipipe_tick_regs);

void __ipipe_handle_irq(int irq, struct pt_regs *regs);

static inline void ipipe_handle_chained_irq(unsigned int irq)
{
	struct pt_regs regs;	/* dummy */

	ipipe_trace_irq_entry(irq);
	__ipipe_handle_irq(irq, &regs);
	ipipe_trace_irq_exit(irq);
}

struct irq_desc;
void __ipipe_ack_level_irq(unsigned irq, struct irq_desc *desc);
void __ipipe_end_level_irq(unsigned irq, struct irq_desc *desc);
void __ipipe_ack_edge_irq(unsigned irq, struct irq_desc *desc);
void __ipipe_end_edge_irq(unsigned irq, struct irq_desc *desc);

void __ipipe_serial_debug(const char *fmt, ...);

#define __ipipe_tick_irq	IPIPE_TIMER_VIRQ

static inline unsigned long __ipipe_ffnz(unsigned long ul)
{
#ifdef CONFIG_PPC64
	__asm__ __volatile__("cntlzd %0, %1":"=r"(ul):"r"(ul & (-ul)));
	return 63 - ul;
#else
	__asm__ __volatile__("cntlzw %0, %1":"=r"(ul):"r"(ul & (-ul)));
	return 31 - ul;
#endif
}

/*
 * When running handlers, enable hw interrupts for all domains but the
 * one heading the pipeline, so that IRQs can never be significantly
 * deferred for the latter.
 */
#define __ipipe_run_isr(ipd, irq)					\
do {									\
	if (!__ipipe_pipeline_head_p(ipd))				\
		local_irq_enable_hw();					\
	if (ipd == ipipe_root_domain)					\
		if (likely(!ipipe_virtual_irq_p(irq)))			\
			ipd->irqs[irq].handler(irq, NULL);		\
		else {							\
			irq_enter();					\
			ipd->irqs[irq].handler(irq, ipd->irqs[irq].cookie);\
			irq_exit();					\
		}							\
	else {								\
		__clear_bit(IPIPE_SYNC_FLAG, &ipipe_cpudom_var(ipd, status)); \
		ipd->irqs[irq].handler(irq, ipd->irqs[irq].cookie);	\
		__set_bit(IPIPE_SYNC_FLAG, &ipipe_cpudom_var(ipd, status)); \
	}								\
	local_irq_disable_hw();						\
} while(0)

#define __ipipe_syscall_watched_p(p, sc)	\
	(((p)->flags & PF_EVNOTIFY) || (unsigned long)sc >= NR_syscalls)

#define __ipipe_root_tick_p(regs)	((regs)->msr & MSR_EE)

void handle_one_irq(unsigned int irq);

void check_stack_overflow(void);

#else /* !CONFIG_IPIPE */

#define task_hijacked(p)	0

#define ipipe_handle_chained_irq(irq)	generic_handle_irq(irq)

#define ipipe_mm_switch_protect(flags)		do { (void)(flags); } while(0)
#define ipipe_mm_switch_unprotect(flags)	do { (void)(flags); } while(0)

#endif /* CONFIG_IPIPE */

#define ipipe_update_tick_evtdev(evtdev)	do { } while (0)

#endif /* !__ASM_POWERPC_IPIPE_H */
