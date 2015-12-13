/*   -*- linux-c -*-
 *   arch/x86/kernel/irq_pipeline.c
 *
 *   Copyright (C) 2002-2012 Philippe Gerum.
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
 *
 *   Architecture-dependent I-PIPE support for x86.
 */

#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/clockchips.h>
#include <linux/kprobes.h>
#include <linux/mm.h>
#include <linux/irq_pipeline.h>
#include <asm/asm-offsets.h>
#include <asm/unistd.h>
#include <asm/processor.h>
#include <asm/atomic.h>
#include <asm/hw_irq.h>
#include <asm/irq.h>
#include <asm/desc.h>
#include <asm/io.h>
#ifdef CONFIG_X86_LOCAL_APIC
#include <asm/tlbflush.h>
#include <asm/fixmap.h>
#include <asm/bitops.h>
#include <asm/mpspec.h>
#ifdef CONFIG_X86_IO_APIC
#include <asm/io_apic.h>
#endif	/* CONFIG_X86_IO_APIC */
#include <asm/apic.h>
#endif	/* CONFIG_X86_LOCAL_APIC */
#include <asm/traps.h>
#include <asm/tsc.h>
#include <asm/mce.h>

DEFINE_PER_CPU(unsigned long, __ipipe_cr2);
EXPORT_PER_CPU_SYMBOL_GPL(__ipipe_cr2);

void arch_irq_push_stage(struct irq_stage *stage,
			 struct irq_pipeline_clocking *clocking)
{
	clocking->sys_hrclock_freq = __ipipe_hrclock_freq;
	clocking->hrclock_name = "tsc";
}

#ifdef CONFIG_X86_LOCAL_APIC

static struct irq_domain *apic_irq_domain;

static void apicm_irq_noop(struct irq_data *data) { }

static unsigned int apicm_irq_noop_ret(struct irq_data *data)
{
	return 0;
}

void handle_apic_irq(unsigned int irq, struct irq_desc *desc)
{
	if (apicm_irq_vector(irq) != SPURIOUS_APIC_VECTOR)
		__ack_APIC_irq();
}

static struct irq_chip apicm_chip = {
	.name		= "APIC mapper",
	.irq_startup	= apicm_irq_noop_ret,
	.irq_shutdown	= apicm_irq_noop,
	.irq_enable	= apicm_irq_noop,
	.irq_disable	= apicm_irq_noop,
	.irq_ack	= apicm_irq_noop,
	.irq_mask	= apicm_irq_noop,
	.irq_unmask	= apicm_irq_noop,
	.flags		= IRQCHIP_PIPELINE_SAFE | IRQCHIP_SKIP_SET_WAKE,
};

static int apicm_irq_map(struct irq_domain *d, unsigned int irq,
			 irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &apicm_chip, handle_apic_irq);

	return 0;
}

static struct irq_domain_ops apicm_domain_ops = {
	.map	= apicm_irq_map,
};

#endif	/* CONFIG_X86_LOCAL_APIC */

void do_root_irq(struct pt_regs *regs,
		 void (*handler)(struct pt_regs *regs));

#define __P(__x)	((void (*)(struct pt_regs *))__x)

static inline unsigned get_irq_vector(int irq)
{
#ifdef CONFIG_X86_IO_APIC
	struct irq_cfg *cfg;

	if (irq == IRQ_MOVE_CLEANUP_VECTOR)
		return irq;

	if (irq >= IPIPE_FIRST_APIC_IRQ && irq < IPIPE_NR_XIRQS)
		return apicm_irq_vector(irq);

	cfg = irq_get_chip_data(irq);

	return cfg->vector;
#elif defined(CONFIG_X86_LOCAL_APIC)
	return is_apic_irqnr(irq) ?
		apicm_irq_vector(irq) : irq + IRQ0_VECTOR;
#else
	return irq + IRQ0_VECTOR;
#endif
}

static inline void do_root_sirq(unsigned int irq, struct irq_desc *desc,
				struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	irq_enter();
	generic_handle_irq_desc(irq, desc);
	irq_exit();
	set_irq_regs(old_regs);
#ifdef CONFIG_X86_64
	if (raw_cpu_read(irq_count) < 0)
#endif
		preempt_check_resched();
}

void do_IRQ_pipelined(unsigned int irq, struct irq_desc *desc)
{
	void (*handler)(struct pt_regs *regs);
	struct pt_regs *regs;
	unsigned int vector;

	regs = raw_cpu_ptr(&irq_pipeline.tick_regs);

	if (desc->irq_data.domain == synthetic_irq_domain) {
		do_root_sirq(irq, desc, regs);
		return;
	}

	vector = get_irq_vector(irq);
	regs->orig_ax = ~vector;

	if (!is_apic_irqnr(irq)) {
		do_root_irq(regs, __P(do_IRQ));
		return;
	}

#ifdef CONFIG_X86_LOCAL_APIC
	switch (vector) {
	case LOCAL_TIMER_VECTOR:
		handler = __P(smp_apic_timer_interrupt);
		break;
	case ERROR_APIC_VECTOR:
		handler = __P(smp_error_interrupt);
		break;
#ifdef CONFIG_X86_THERMAL_VECTOR
	case THERMAL_APIC_VECTOR:
		handler = __P(smp_thermal_interrupt);
		break;
#endif
#ifdef CONFIG_X86_MCE_THRESHOLD
	case THRESHOLD_APIC_VECTOR:
		handler = __P(smp_threshold_interrupt);
		break;
#endif
#ifdef CONFIG_X86_UV
	case UV_BAU_MESSAGE:
		handler = __P(uv_bau_message_interrupt);
		break;
#endif
#ifdef CONFIG_IRQ_WORK
	case IRQ_WORK_VECTOR:
		handler = __P(smp_irq_work_interrupt);
		break;
#endif
	case X86_PLATFORM_IPI_VECTOR:
		handler = __P(smp_x86_platform_ipi);
#ifdef CONFIG_SMP
	case RESCHEDULE_VECTOR:
		handler = __P(smp_reschedule_interrupt);
		break;
	case CALL_FUNCTION_VECTOR:
		handler = __P(smp_call_function_interrupt);
		break;
	case CALL_FUNCTION_SINGLE_VECTOR:
		handler = __P(smp_call_function_single_interrupt);
		break;
	case IRQ_MOVE_CLEANUP_VECTOR:
		handler = __P(smp_irq_move_cleanup_interrupt);
		break;
	case REBOOT_VECTOR:
		handler = __P(smp_reboot_interrupt);
		break;
#endif	/* CONFIG_SMP */
	case SPURIOUS_APIC_VECTOR:
	default:
		handler = __P(smp_spurious_interrupt);
	}

	do_root_irq(regs, handler);
#endif	/* CONFIG_X86_LOCAL_APIC */
}

#undef __P

#ifdef CONFIG_X86_LOCAL_APIC

void __init arch_irq_pipeline_init(void)
{
	apic_irq_domain = irq_domain_add_simple(NULL,
			NR_VECTORS - FIRST_SYSTEM_VECTOR,
			apicm_vector_irq(FIRST_SYSTEM_VECTOR),
			&apicm_domain_ops, NULL);
}

#else
void __init arch_irq_pipeline_init(void) { }
#endif

void __init irq_pipeline_init_late(void)
{
	__ipipe_hrclock_freq = 1000ULL * cpu_khz;
}

#ifdef CONFIG_SMP

void irq_pipeline_send_remote(unsigned int ipi,
			      const struct cpumask *cpumask)
{
	unsigned long flags;

	flags = hard_local_irq_save();

	if (likely(!cpumask_empty(cpumask)))
		apic->send_IPI_mask_allbutself(cpumask,
					       apicm_irq_vector(ipi));

	hard_local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(irq_pipeline_send_remote);

#endif	/* CONFIG_SMP */

int enter_irq_pipeline(struct pt_regs *regs)
{
	int irq, vector = ~regs->orig_ax;

	if (vector >= FIRST_SYSTEM_VECTOR)
		irq = apicm_vector_irq(vector);
	else {
		irq = __this_cpu_read(vector_irq[vector]);
		BUG_ON(irq < 0);
	}

	irq_pipeline_enter(irq, regs);

	if (!__on_root_stage() ||
	    test_bit(IPIPE_STALL_FLAG, &irq_root_status))
		return 0;

	return 1;
}

static inline void __fixup_if(int s, struct pt_regs *regs)
{
	/*
	 * Have the saved hw state look like the domain stall bit, so
	 * that __ipipe_unstall_iret_root() restores the proper
	 * pipeline state for the root stage upon exit.
	 */
	if (s)
		regs->flags &= ~X86_EFLAGS_IF;
	else
		regs->flags |= X86_EFLAGS_IF;
}

int do_trap_prologue(struct pt_regs *regs, int trapnr,
		     unsigned long *flags)
{
	bool root_entry = false;
	struct irq_stage *stage;
	unsigned long cr2;

#ifdef CONFIG_KGDB
	/* Fixup kgdb-own faults immediately. */
	if (__ipipe_probe_access) {
		const struct exception_table_entry *fixup =
			search_exception_tables(regs->ip);
		BUG_ON(!fixup);
		regs->ip = (unsigned long)&fixup->fixup + fixup->fixup;
		return 1;
	}
#endif /* CONFIG_KGDB */

	if (trapnr == X86_TRAP_PF)
		cr2 = native_read_cr2();

	/*
	 * If we fault over the root domain, we need to replicate the
	 * hw interrupt state into the virtual mask before calling the
	 * I-pipe event handler. This is also required later before
	 * branching to the regular exception handler.
	 */
	if (on_root_stage()) {
		root_entry = true;
		local_save_flags(*flags);
		if (hard_irqs_disabled())
			local_irq_disable();
	}

#ifdef CONFIG_KGDB
	/*
	 * Catch int1 and int3 for kgdb here. They may trigger over
	 * inconsistent states even when the root domain is active.
	 */
	if (kgdb_io_module_registered &&
	    (trapnr == X86_TRAP_DB || trapnr == X86_TRAP_BP)) {
		unsigned int condition = 0;

		if (trapnr == X86_TRAP_DB) {
			if (!atomic_read(&kgdb_cpu_doing_single_step) != -1 &&
			    test_thread_flag(TIF_SINGLESTEP))
				goto skip_kgdb;
			get_debugreg(condition, 6);
		}
		if (!user_mode(regs) &&
		    !kgdb_handle_exception(trapnr, SIGTRAP, condition, regs)) {
			if (root_entry) {
				root_irq_restore_nosync(*flags);
				__fixup_if(root_entry ?
					   raw_irqs_disabled_flags(*flags) :
					   raw_irqs_disabled(), regs);
			}
			return 1;
		}
	}
skip_kgdb:
#endif /* CONFIG_KGDB */

	if (unlikely(dovetail_trap(trapnr, regs))) {
		if (root_entry)
			root_irq_restore_nosync(*flags);
		return 1;
	}

	/*
	 * If no head domain is installed, or in case we faulted in
	 * the iret path of x86-32, regs.flags does not match the root
	 * domain state. The fault handler or the low-level return
	 * code may evaluate it. So fix this up, either by the root
	 * state sampled on entry or, if we migrated to root, with the
	 * current state.
	 */
	if (likely(on_root_stage()))
		__fixup_if(root_entry ? raw_irqs_disabled_flags(*flags) :
					raw_irqs_disabled(), regs);
	else {
		/*
		 * Detect unhandled faults over the head domain,
		 * switching to root so that it can handle the fault
		 * cleanly.
		 */
		hard_local_irq_disable();
		stage = __current_irq_stage;
		__set_current_irq_stage(&root_irq_stage);

		/* Always warn about user land and unfixable faults. */
		if (user_mode(regs) ||
		    !search_exception_tables(instruction_pointer(regs))) {
			printk(KERN_ERR "BUG: Unhandled exception over domain"
			       " %s at 0x%lx - switching to ROOT\n",
			       stage->name, instruction_pointer(regs));
			dump_stack();
		} else if (irq_pipeline_debug()) {
			/* Also report fixable ones when debugging is enabled. */
			printk(KERN_WARNING "WARNING: Fixable exception over "
			       "domain %s at 0x%lx - switching to ROOT\n",
			       stage->name, instruction_pointer(regs));
			dump_stack();
		}
	}

	if (trapnr == X86_TRAP_PF)
		write_cr2(cr2);

	return root_entry ? 0 : -1;
}
