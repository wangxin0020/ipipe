/* -*- linux-c -*-
 * arch/powerpc/kernel/irq_pipeline.c
 *
 * Copyright (C) 2005 Heikki Lindholm (PPC64 port).
 * Copyright (C) 2004 Wolfgang Grandegger (Adeos/ppc port over 2.4).
 * Copyright (C) 2002-2012 Philippe Gerum.
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
 * Architecture-dependent I-PIPE core support for PowerPC 32/64bit.
 */

#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/clockchips.h>
#include <linux/export.h>
#include <linux/kernel_stat.h>
#include <asm/reg.h>
#include <asm/switch_to.h>
#include <asm/mmu_context.h>
#include <asm/unistd.h>
#include <asm/machdep.h>
#include <asm/atomic.h>
#include <asm/hardirq.h>
#include <asm/io.h>
#include <asm/time.h>
#include <asm/runlatch.h>
#include <asm/debug.h>

#ifdef CONFIG_SMP

struct remote_message_map {
	unsigned long pending;
} ____cacheline_aligned;

static DEFINE_PER_CPU(struct remote_message_map, pending_remote_messages);

int ipipe_platform_sirqs[4];
EXPORT_SYMBOL_GPL(ipipe_platform_sirqs);

void smp_ipi_pipeline_demux(int irq)
{
	struct remote_message_map *m;
	unsigned long flags;
	int msg;

	flags = hard_local_irq_save();

	m = this_cpu_ptr(&pending_remote_messages);
	while (m->pending & IPIPE_MSG_IPI_MASK) {
		for (msg = IPIPE_MSG_CRITICAL_IPI;
		     msg <= IPIPE_MSG_RESCHEDULE_IPI; ++msg)
			if (test_and_clear_bit(msg, &m->pending))
				__irq_pipeline_enter(msg + IPIPE_BASE_IPI_OFFSET, NULL);
	}

	/*
	 * Schedule a call to call_function_action() over the root
	 * domain next, to run the regular IPI callbacks if any.
	 */
	irq_stage_post_root(irq);

	hard_local_irq_restore(flags);
}

void irq_pipeline_send_remote(unsigned int ipi,
			      const struct cpumask *cpumask)
{
	unsigned long flags;
	int msg, cpu, me;

	if (unlikely(cpumask_empty(cpumask)))
		return;

	flags = hard_local_irq_save();

	me = raw_smp_processor_id();
	msg = ipi - IPIPE_BASE_IPI_OFFSET;
	for_each_cpu(cpu, cpumask) {
		if (cpu == me)
			continue;
		set_bit(msg, &per_cpu(pending_remote_messages, cpu).pending);
		mb();
		smp_ops->message_pass(cpu, PPC_MSG_CALL_FUNCTION);
	}

	hard_local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(irq_pipeline_send_remote);

#else  /* !CONFIG_SMP */

int ipipe_platform_sirqs[1];
EXPORT_SYMBOL_GPL(ipipe_platform_sirqs);

#endif	/* CONFIG_SMP */

void __init arch_irq_pipeline_init(void)
{
	unsigned int irq;
	int n;

	/*
	 * Allocate the synthetic interrupts we need. On powerpc, we
	 * map the decrementer trap onto a synthetic IRQ for injecting
	 * tick events into the pipeline.
	 */
	for (n = 0; n < ARRAY_SIZE(ipipe_platform_sirqs); n++) {
		irq = irq_create_direct_mapping(synthetic_irq_domain);
		BUG_ON(irq == 0);
		ipipe_platform_sirqs[n] = irq;
	}
}

void __init irq_pipeline_init_late(void)
{
	__ipipe_hrclock_freq = ppc_tb_freq;
}

void arch_irq_push_stage(struct irq_stage *stage,
			 struct irq_pipeline_clocking *clocking)
{
	clocking->sys_hrclock_freq = __ipipe_hrclock_freq;
	clocking->hrclock_name = "timebase";
}

int enter_irq_pipeline(struct pt_regs *regs)
{
	int irq;

	irq = ppc_md.get_irq();
	if (unlikely(irq == NO_IRQ))
		__this_cpu_add(irq_stat.spurious_irqs, 1);
	else
		irq_pipeline_enter(irq, regs);

	return __on_root_stage() && !test_bit(IPIPE_STALL_FLAG, &irq_root_status);
}

int timer_enter_pipeline(struct pt_regs *regs)
{
	__irq_pipeline_enter(DECREMENTER_IRQ, regs);

	return __on_root_stage() && !test_bit(IPIPE_STALL_FLAG, &irq_root_status);
}

void do_IRQ_pipelined(unsigned int irq, struct irq_desc *desc)
{
	struct pt_regs *regs, *old_regs;

	/* Any sensible register frame will do for non-timer IRQs. */
	regs = raw_cpu_ptr(&irq_pipeline.tick_regs);
	old_regs = set_irq_regs(regs);

	if (irq == DECREMENTER_IRQ) {
		irq_enter();
		__timer_interrupt();
		irq_exit();
	} else
		___do_irq(irq, regs);

	set_irq_regs(old_regs);
}

EXPORT_SYMBOL_GPL(show_stack);
EXPORT_SYMBOL_GPL(_switch);
#ifndef CONFIG_SMP
EXPORT_SYMBOL_GPL(last_task_used_math);
#endif
