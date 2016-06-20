/* -*- linux-c -*-
 * arch/arm/kernel/irq_pipeline.c
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
#include <linux/irq_pipeline.h>
#include <asm/system_info.h>
#include <asm/atomic.h>
#include <asm/hardirq.h>
#include <asm/io.h>
#include <asm/unistd.h>
#include <asm/mach/irq.h>
#include <asm/mmu_context.h>
#include <asm/exception.h>

#ifdef CONFIG_SMP

static struct irq_domain *ipim_domain;

static void ipim_irq_noop(struct irq_data *data) { }

static unsigned int ipim_irq_noop_ret(struct irq_data *data)
{
	return 0;
}

static struct irq_chip ipim_chip = {
	.name		= "IPI mapper",
	.irq_startup	= ipim_irq_noop_ret,
	.irq_shutdown	= ipim_irq_noop,
	.irq_enable	= ipim_irq_noop,
	.irq_disable	= ipim_irq_noop,
	.irq_ack	= ipim_irq_noop,
	.irq_mask	= ipim_irq_noop,
	.irq_unmask	= ipim_irq_noop,
	.flags		= IRQCHIP_PIPELINE_SAFE | IRQCHIP_SKIP_SET_WAKE,
};

static int ipim_irq_map(struct irq_domain *d, unsigned int irq,
			 irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &ipim_chip, handle_synthetic_irq);

	return 0;
}

static struct irq_domain_ops ipim_domain_ops = {
	.map	= ipim_irq_map,
};

static void create_ipi_domain(void)
{
	/*
	 * Create an IRQ domain for mapping all IPIs, with fixed sirq
	 * numbers starting from IPIPE_IPI_BASE onward. The sirqs
	 * obtained can be injected into the pipeline upon IPI receipt
	 * like other interrupts.
	 */
	ipim_domain = irq_domain_add_simple(NULL, NR_IPI, IPIPE_IPI_BASE,
					    &ipim_domain_ops, NULL);
}

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
EXPORT_SYMBOL_GPL(__ipipe_smp_key);

static int disable_smp(void)
{
	unsigned long flags;

	if (num_online_cpus() == 1) {
		printk("IRQ pipeline: disabling SMP code\n");
		flags = hard_local_irq_save();
		static_key_slow_dec(&__ipipe_smp_key);
		hard_local_irq_restore(flags);
	}

	return 0;
}
arch_initcall(disable_smp);

extern unsigned int smp_on_up;
EXPORT_SYMBOL_GPL(smp_on_up);
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
#ifdef CONFIG_CPU_ARM926T
	/*
	 * We do not want "wfi" to be called in arm926ejs based
	 * processor, as this causes Linux to disable the I-cache
	 * when idle.
	 */
	extern void cpu_arm926_proc_init(void);
	if (likely(cpu_proc_init == &cpu_arm926_proc_init)) {
		printk("I-pipe: ARM926EJ-S detected, disabling wfi instruction"
		       " in idle loop\n");
		cpu_idle_poll_ctrl(true);
	}
#endif
#ifdef CONFIG_SMP
	create_ipi_domain();
#endif
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

__kprobes int
__ipipe_switch_to_notifier_call_chain(struct atomic_notifier_head *nh,
				      unsigned long val, void *v)
{
	unsigned long flags;
	int ret;

	local_irq_save(flags);
	ret = atomic_notifier_call_chain(nh, val, v);
	__root_irq_restore_nosync(flags);

	return ret;
}

void do_IRQ_pipelined(unsigned int irq, struct irq_desc *desc)
{
	struct pt_regs *regs = raw_cpu_ptr(&irq_pipeline.tick_regs);

#ifdef CONFIG_SMP
	/*
	 * Check for IPIs, handing them over to the specific dispatch
	 * code.
	 */
	if (irq >= IPIPE_IPI_BASE && irq < IPIPE_IPI_BASE + NR_IPI) {
		__handle_IPI(irq - IPIPE_IPI_BASE, regs);
		return;
	}
#endif
		
	do_domain_irq(irq, regs);
}

EXPORT_SYMBOL_GPL(do_munmap);
EXPORT_SYMBOL_GPL(show_stack);
EXPORT_SYMBOL_GPL(cpu_architecture);
