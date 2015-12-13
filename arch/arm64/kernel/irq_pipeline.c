/* -*- linux-c -*-
 * arch/arm64/kernel/irq_pipeline.c
 *
 * Copyright (C) 2002-2005 Philippe Gerum.
 * Copyright (C) 2004 Wolfgang Grandegger (Adeos/arm port over 2.4).
 * Copyright (C) 2005 Heikki Lindholm (PowerPC 970 fixes).
 * Copyright (C) 2005 Stelian Pop.
 * Copyright (C) 2006-2008 Gilles Chanteperdrix.
 * Copyright (C) 2010 Philippe Gerum (SMP port).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 * USA; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Architecture-dependent I-PIPE support for ARM.
 */

#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kallsyms.h>
#include <linux/kprobes.h>
#include <linux/irq.h>
#include <linux/irqnr.h>
#include <linux/prefetch.h>
#include <linux/cpu.h>
#include <linux/ipipe_tickdev.h>
#include <asm/atomic.h>
#include <asm/hardirq.h>
#include <asm/io.h>
#include <asm/unistd.h>
#include <asm/mmu_context.h>
#include <asm/exception.h>
#include <asm/arch_timer.h>

#ifdef CONFIG_SMP

void irq_pipeline_send_remote(unsigned int ipi,
			      const struct cpumask *cpumask)
{
	enum ipi_msg_type msg = ipi - IPIPE_IPI_BASE;
	smp_cross_call(cpumask, msg);
}
EXPORT_SYMBOL_GPL(irq_pipeline_send_remote);

#endif	/* CONFIG_SMP */

#ifdef CONFIG_SMP_ON_UP
struct static_key __ipipe_smp_key = STATIC_KEY_INIT_TRUE;

static int disable_smp(void)
{
	if (num_online_cpus() == 1) {
		unsigned long flags;

		printk("I-pipe: disabling SMP code\n");

		flags = hard_local_irq_save();
		static_key_slow_dec(&__ipipe_smp_key);
		hard_local_irq_restore(flags);
	}
	return 0;
}
arch_initcall(disable_smp);
#endif /* SMP_ON_UP */

void arch_irq_push_stage(struct irq_stage *stage,
			 struct irq_pipeline_clocking *clocking)
{
	clocking->sys_hrclock_freq = __ipipe_hrclock_freq;
	clocking->hrclock_name = "ipipe_tsc";
	__ipipe_mach_get_tscinfo(&clocking->arch.tsc);
}

void __init arch_irq_pipeline_init(void)
{
#ifdef CONFIG_SMP
	smp_create_ipi_domain();
#endif
}

void __init irq_pipeline_init_late(void)
{
	__ipipe_hrclock_freq = arch_timer_get_rate();
}

#ifdef CONFIG_IRQ_PIPELINE_DEBUG
unsigned asmlinkage __ipipe_bugon_irqs_enabled(unsigned long x)
{
	BUG_ON(!hard_irqs_disabled());
	return x;		/* Preserve r0 */
}
#endif

asmlinkage int __ipipe_check_root_interruptible(void)
{
	return __on_root_stage() && !irqs_disabled();
}

void do_IRQ_pipelined(unsigned int irq, struct irq_desc *desc)
{
	struct pt_regs *regs = raw_cpu_ptr(&irq_pipeline.tick_regs);

#ifdef CONFIG_SMP
	/*
	 * Check for regular IPIs, handing them over to the regular
	 * dispatch code. Pipeline-specific IPIs and other IRQs can go
	 * through the domain IRQ handler.
	 */
	if (irq >= IPIPE_IPI_BASE && irq < IPIPE_CRITICAL_IPI) {
		__handle_IPI(irq - IPIPE_IPI_BASE, regs);
		return;
	}
#endif
		
	do_domain_irq(irq, regs);
}

static struct __ipipe_tscinfo tsc_info;

void __init __ipipe_tsc_register(struct __ipipe_tscinfo *info)
{
	tsc_info = *info;
	__ipipe_hrclock_freq = info->freq;
}

void __ipipe_mach_get_tscinfo(struct __ipipe_tscinfo *info)
{
	*info = tsc_info;
}

notrace unsigned long long __ipipe_tsc_get(void)
{
	return arch_counter_get_cntvct();
}

EXPORT_SYMBOL_GPL(do_munmap);
EXPORT_SYMBOL_GPL(show_stack);
