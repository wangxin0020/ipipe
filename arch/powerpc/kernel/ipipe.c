/* -*- linux-c -*-
 * linux/arch/powerpc/kernel/ipipe.c
 *
 * Copyright (C) 2005 Heikki Lindholm (PPC64 port).
 * Copyright (C) 2004 Wolfgang Grandegger (Adeos/ppc port over 2.4).
 * Copyright (C) 2002-2007 Philippe Gerum.
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
#include <linux/module.h>
#include <linux/kernel_stat.h>
#include <asm/reg.h>
#include <asm/system.h>
#include <asm/mmu_context.h>
#include <asm/unistd.h>
#include <asm/machdep.h>
#include <asm/atomic.h>
#include <asm/hardirq.h>
#include <asm/io.h>
#include <asm/time.h>

#ifdef CONFIG_PPC_BOOK3E_64
#error "BOOK3E/64bit architecture not supported, yet"
#endif

static void __ipipe_do_IRQ(unsigned irq, void *cookie);

static void __ipipe_do_timer(unsigned irq, void *cookie);

DEFINE_PER_CPU(struct pt_regs, __ipipe_tick_regs);
#ifdef CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH
DEFINE_PER_CPU(struct mm_struct *, ipipe_active_mm);
EXPORT_PER_CPU_SYMBOL(ipipe_active_mm);
#endif

#define DECREMENTER_MAX	0x7fffffff

#ifdef CONFIG_SMP

static cpumask_t __ipipe_cpu_sync_map;

static cpumask_t __ipipe_cpu_lock_map;

static IPIPE_DEFINE_SPINLOCK(__ipipe_cpu_barrier);

static atomic_t __ipipe_critical_count = ATOMIC_INIT(0);

static void (*__ipipe_cpu_sync) (void);

static DEFINE_PER_CPU(struct ipipe_ipi_struct, ipipe_ipi_message);

unsigned int __ipipe_ipi_irq = NR_IRQS + 1; /* dummy value */

#ifdef CONFIG_DEBUGGER
cpumask_t __ipipe_dbrk_pending;	/* pending debugger break IPIs */
#endif

/* Always called with hw interrupts off. */

void __ipipe_do_critical_sync(unsigned irq, void *cookie)
{
	cpu_set(ipipe_processor_id(), __ipipe_cpu_sync_map);

	/*
	 * Now we are in sync with the lock requestor running on another
	 * CPU. Enter a spinning wait until he releases the global
	 * lock.
	 */
	spin_lock(&__ipipe_cpu_barrier);

	/* Got it. Now get out. */

	if (__ipipe_cpu_sync)
		/* Call the sync routine if any. */
		__ipipe_cpu_sync();

	spin_unlock(&__ipipe_cpu_barrier);

	cpu_clear(ipipe_processor_id(), __ipipe_cpu_sync_map);
}

void __ipipe_hook_critical_ipi(struct ipipe_domain *ipd)
{
	ipd->irqs[IPIPE_CRITICAL_IPI].acknowledge = NULL;
	ipd->irqs[IPIPE_CRITICAL_IPI].handler = &__ipipe_do_critical_sync;
	ipd->irqs[IPIPE_CRITICAL_IPI].cookie = NULL;
	/* Immediately handle in the current domain but *never* pass */
	ipd->irqs[IPIPE_CRITICAL_IPI].control =
		IPIPE_HANDLE_MASK|IPIPE_STICKY_MASK|IPIPE_SYSTEM_MASK;
}

void __ipipe_register_ipi(unsigned int irq)
{
	__ipipe_ipi_irq = irq;
	mb();
#ifndef CONFIG_DEBUGGER
	irq_to_desc(irq)->chip->startup(irq);
#endif
} 

static void __ipipe_ipi_demux(int irq, struct pt_regs *regs)
{
	struct irq_desc *desc = irq_to_desc(irq);
	int ipi, cpu = ipipe_processor_id();

	desc->ipipe_ack(irq, desc);

	kstat_incr_irqs_this_cpu(irq, desc);

	while (per_cpu(ipipe_ipi_message, cpu).value & IPIPE_MSG_IPI_MASK) {
		for (ipi = IPIPE_MSG_CRITICAL_IPI; ipi <= IPIPE_MSG_SERVICE_IPI4; ++ipi) {
			if (test_and_clear_bit(ipi, &per_cpu(ipipe_ipi_message, cpu).value)) {
				mb();
				__ipipe_handle_irq(ipi + IPIPE_MSG_IPI_OFFSET, NULL);
			}
		}
	}

#ifdef CONFIG_DEBUGGER
	/*
	 * The debugger IPI handler should be NMI-safe, so let's call
	 * it immediately in case the IPI is pending.
	 */
	if (cpu_isset(cpu, __ipipe_dbrk_pending)) {
		cpu_clear(cpu, __ipipe_dbrk_pending);
		debugger_ipi(regs);
	}
#endif /* CONFIG_DEBUGGER */

	__ipipe_end_irq(irq);
}

cpumask_t __ipipe_set_irq_affinity(unsigned irq, cpumask_t cpumask)
{
	cpumask_t oldmask;

	if (irq_to_desc(irq)->chip->set_affinity == NULL)
		return CPU_MASK_NONE;

	if (cpus_empty(cpumask))
		return CPU_MASK_NONE; /* Return mask value -- no change. */

	cpus_and(cpumask, cpumask, cpu_online_map);
	if (cpus_empty(cpumask))
		return CPU_MASK_NONE;	/* Error -- bad mask value or non-routable IRQ. */

	cpumask_copy(&oldmask, irq_to_desc(irq)->affinity);
	irq_to_desc(irq)->chip->set_affinity(irq, &cpumask);

	return oldmask;
}

int __ipipe_send_ipi(unsigned ipi, cpumask_t cpumask)
{
	unsigned long flags;
	cpumask_t testmask;
	int cpu;

	local_irq_save_hw(flags);

	ipi -= IPIPE_MSG_IPI_OFFSET;
	for_each_online_cpu(cpu) {
		if (cpu_isset(cpu, cpumask))
			set_bit(ipi, &per_cpu(ipipe_ipi_message, cpu).value);
	}
	mb();	 
	
	if (unlikely(cpus_empty(cpumask)))
		goto out;

	cpus_setall(testmask);
	cpu_clear(ipipe_processor_id(), testmask);
	if (likely(cpus_equal(cpumask, testmask)))
		smp_ops->message_pass(MSG_ALL_BUT_SELF, PPC_MSG_IPIPE_DEMUX);
	else {
		/* Long path. */
		for_each_cpu_mask_nr(cpu, cpumask)
			smp_ops->message_pass(cpu, PPC_MSG_IPIPE_DEMUX);
	}
out:
	local_irq_restore_hw(flags);

	return 0;
}

void __ipipe_stall_root(void)
{
	unsigned long flags;

	local_irq_save_hw(flags);
	set_bit(IPIPE_STALL_FLAG, &ipipe_root_cpudom_var(status));
	local_irq_restore_hw(flags);
}

unsigned long __ipipe_test_and_stall_root(void)
{
	unsigned long flags;
	int x;

	local_irq_save_hw(flags);
	x = test_and_set_bit(IPIPE_STALL_FLAG, &ipipe_root_cpudom_var(status));
	local_irq_restore_hw(flags);

	return x;
}

unsigned long __ipipe_test_root(void)
{
	unsigned long flags;
	int x;

	local_irq_save_hw(flags);
	x = test_bit(IPIPE_STALL_FLAG, &ipipe_root_cpudom_var(status));
	local_irq_restore_hw(flags);

	return x;
}

#endif	/* CONFIG_SMP */

/*
 * ipipe_critical_enter() -- Grab the superlock excluding all CPUs
 * but the current one from a critical section. This lock is used when
 * we must enforce a global critical section for a single CPU in a
 * possibly SMP system whichever context the CPUs are running.
 */
unsigned long ipipe_critical_enter(void (*syncfn) (void))
{
	unsigned long flags;

	local_irq_save_hw(flags);

#ifdef CONFIG_SMP
	if (likely(num_online_cpus() > 1)) {
		/* We might be running a SMP-kernel on a UP box... */
		int cpu = ipipe_processor_id();
		cpumask_t lock_map;
		cpumask_t others;

		if (!cpu_test_and_set(cpu, __ipipe_cpu_lock_map)) {
			while (cpu_test_and_set(BITS_PER_LONG - 1, __ipipe_cpu_lock_map)) {
				int n = 0;
				do {
					cpu_relax();
				} while (++n < cpu);
			}

			spin_lock(&__ipipe_cpu_barrier);

			__ipipe_cpu_sync = syncfn;

			/* Send the sync IPI to all processors but the current one. */
			cpus_setall(others);
			cpu_clear(ipipe_processor_id(), others);
			__ipipe_send_ipi(IPIPE_CRITICAL_IPI, others);

			cpus_andnot(lock_map, cpu_online_map,
				    __ipipe_cpu_lock_map);

			while (!cpus_equal(__ipipe_cpu_sync_map, lock_map))
				cpu_relax();
		}

		atomic_inc(&__ipipe_critical_count);
	}
#endif	/* CONFIG_SMP */

	return flags;
}

/* ipipe_critical_exit() -- Release the superlock. */

void ipipe_critical_exit(unsigned long flags)
{
#ifdef CONFIG_SMP
	if (likely(num_online_cpus() > 1)) {
		/* We might be running a SMP-kernel on a UP box... */
		if (atomic_dec_and_test(&__ipipe_critical_count)) {
			spin_unlock(&__ipipe_cpu_barrier);

			while (!cpus_empty(__ipipe_cpu_sync_map))
				cpu_relax();

			cpu_clear(ipipe_processor_id(), __ipipe_cpu_lock_map);
			cpu_clear(BITS_PER_LONG - 1, __ipipe_cpu_lock_map);
		}
	}
#endif	/* CONFIG_SMP */

	local_irq_restore_hw(flags);
}

void __ipipe_init_platform(void)
{
	unsigned int virq;

	/*
	 * Allocate a virtual IRQ for the decrementer trap early to
	 * get it mapped to IPIPE_VIRQ_BASE
	 */

	virq = ipipe_alloc_virq();

	if (virq != IPIPE_TIMER_VIRQ)
		panic("I-pipe: cannot reserve timer virq #%d (got #%d)",
		      IPIPE_TIMER_VIRQ, virq);

#ifdef CONFIG_SMP
	virq = ipipe_alloc_virq();
	if (virq != IPIPE_CRITICAL_IPI)
		panic("I-pipe: cannot reserve critical IPI virq #%d (got #%d)",
		      IPIPE_CRITICAL_IPI, virq);
	virq = ipipe_alloc_virq();
	if (virq != IPIPE_SERVICE_IPI0)
		panic("I-pipe: cannot reserve service IPI 0 virq #%d (got #%d)",
		      IPIPE_SERVICE_IPI0, virq);
	virq = ipipe_alloc_virq();
	if (virq != IPIPE_SERVICE_IPI1)
		panic("I-pipe: cannot reserve service IPI 1 virq #%d (got #%d)",
		      IPIPE_SERVICE_IPI1, virq);
	virq = ipipe_alloc_virq();
	if (virq != IPIPE_SERVICE_IPI2)
		panic("I-pipe: cannot reserve service IPI 2 virq #%d (got #%d)",
		      IPIPE_SERVICE_IPI2, virq);
	virq = ipipe_alloc_virq();
	if (virq != IPIPE_SERVICE_IPI3)
		panic("I-pipe: cannot reserve service IPI 3 virq #%d (got #%d)",
		      IPIPE_SERVICE_IPI3, virq);
	virq = ipipe_alloc_virq();
	if (virq != IPIPE_SERVICE_IPI4)
		panic("I-pipe: cannot reserve service IPI 4 virq #%d (got #%d)",
		      IPIPE_SERVICE_IPI4, virq);
#endif
}

void __ipipe_end_irq(unsigned irq)
{
	struct irq_desc *desc = irq_to_desc(irq);
	desc->ipipe_end(irq, desc);
}

void __ipipe_enable_irqdesc(struct ipipe_domain *ipd, unsigned irq)
{
	irq_to_desc(irq)->status &= ~IRQ_DISABLED;
}

static void __ipipe_ack_irq(unsigned irq, struct irq_desc *desc)
{
	desc->ipipe_ack(irq, desc);
}

/*
 * __ipipe_enable_pipeline() -- We are running on the boot CPU, hw
 * interrupts are off, and secondary CPUs are still lost in space.
 */
void __ipipe_enable_pipeline(void)
{
	unsigned long flags;
	unsigned irq;

	flags = ipipe_critical_enter(NULL);

	/* First, virtualize all interrupts from the root domain. */

	for (irq = 0; irq < NR_IRQS; irq++)
		/*
		 * May fail with -EINVAL for unallocated descriptors;
		 * that's ok.
		 */
		ipipe_virtualize_irq(ipipe_root_domain,
				     irq,
				     &__ipipe_do_IRQ, NULL,
				     &__ipipe_ack_irq,
				     IPIPE_HANDLE_MASK | IPIPE_PASS_MASK);
	/*
	 * We use a virtual IRQ to handle the timer irq (decrementer trap)
	 * which has been allocated early in __ipipe_init_platform().
	 */

	ipipe_virtualize_irq(ipipe_root_domain,
			     IPIPE_TIMER_VIRQ,
			     &__ipipe_do_timer, NULL,
			     NULL, IPIPE_HANDLE_MASK | IPIPE_PASS_MASK);

	ipipe_critical_exit(flags);
}

int ipipe_get_sysinfo(struct ipipe_sysinfo *info)
{
	info->sys_nr_cpus = num_online_cpus();
	info->sys_cpu_freq = __ipipe_cpu_freq;
	info->sys_hrtimer_irq = __ipipe_hrtimer_irq;
	info->sys_hrtimer_freq = __ipipe_hrtimer_freq;
	info->sys_hrclock_freq = __ipipe_hrclock_freq;

	return 0;
}

/*
 * ipipe_trigger_irq() -- Push the interrupt at front of the pipeline
 * just like if it has been actually received from a hw source. Also
 * works for virtual interrupts.
 */
int ipipe_trigger_irq(unsigned irq)
{
	unsigned long flags;

#ifdef CONFIG_IPIPE_DEBUG
	if (irq >= IPIPE_NR_IRQS)
		return -EINVAL;
	if (ipipe_virtual_irq_p(irq)) {
		if (!test_bit(irq - IPIPE_VIRQ_BASE,
			      &__ipipe_virtual_irq_map))
			return -EINVAL;
	} else if (irq_to_desc(irq) == NULL)
		return -EINVAL;
#endif
	local_irq_save_hw(flags);
	__ipipe_handle_irq(irq, NULL);
	local_irq_restore_hw(flags);

	return 1;
}

/*
 * __ipipe_handle_irq() -- IPIPE's generic IRQ handler. An optimistic
 * interrupt protection log is maintained here for each domain. Hw
 * interrupts are off on entry.
 */
void __ipipe_handle_irq(int irq, struct pt_regs *regs)
{
	struct ipipe_domain *this_domain, *next_domain;
	struct list_head *head, *pos;
	int m_ack;

	/* Software-triggered IRQs do not need any ack. */
	m_ack = (regs == NULL);

#ifdef CONFIG_IPIPE_DEBUG
	if (unlikely(irq >= IPIPE_NR_IRQS) ||
	    (!m_ack && irq_to_desc(irq) == NULL)) {
		printk(KERN_ERR "I-pipe: spurious interrupt %d\n", irq);
		return;
	}
#endif
	this_domain = __ipipe_current_domain;

	if (unlikely(test_bit(IPIPE_STICKY_FLAG, &this_domain->irqs[irq].control)))
		head = &this_domain->p_link;
	else {
		head = __ipipe_pipeline.next;
		next_domain = list_entry(head, struct ipipe_domain, p_link);
		if (likely(test_bit(IPIPE_WIRED_FLAG, &next_domain->irqs[irq].control))) {
			if (!m_ack && next_domain->irqs[irq].acknowledge)
				next_domain->irqs[irq].acknowledge(irq, irq_to_desc(irq));
			__ipipe_dispatch_wired(next_domain, irq);
			return;
		}
	}

	/* Ack the interrupt. */

	pos = head;

	while (pos != &__ipipe_pipeline) {
		next_domain = list_entry(pos, struct ipipe_domain, p_link);
		prefetch(next_domain);
		/*
		 * For each domain handling the incoming IRQ, mark it as
		 * pending in its log.
		 */
		if (test_bit(IPIPE_HANDLE_FLAG, &next_domain->irqs[irq].control)) {
			/*
			 * Domains that handle this IRQ are polled for
			 * acknowledging it by decreasing priority order. The
			 * interrupt must be made pending _first_ in the
			 * domain's status flags before the PIC is unlocked.
			 */
			__ipipe_set_irq_pending(next_domain, irq);

			if (!m_ack && next_domain->irqs[irq].acknowledge) {
				next_domain->irqs[irq].acknowledge(irq, irq_to_desc(irq));
				m_ack = 1;
			}
		}

		/*
		 * If the domain does not want the IRQ to be passed down the
		 * interrupt pipe, exit the loop now.
		 */
		if (!test_bit(IPIPE_PASS_FLAG, &next_domain->irqs[irq].control))
			break;

		pos = next_domain->p_link.next;
	}

	/*
	 * If the interrupt preempted the head domain, then do not
	 * even try to walk the pipeline, unless an interrupt is
	 * pending for it.
	 */
	if (test_bit(IPIPE_AHEAD_FLAG, &this_domain->flags) &&
	    !__ipipe_ipending_p(ipipe_head_cpudom_ptr()))
		return;

	/*
	 * Now walk the pipeline, yielding control to the highest
	 * priority domain that has pending interrupt(s) or
	 * immediately to the current domain if the interrupt has been
	 * marked as 'sticky'. This search does not go beyond the
	 * current domain in the pipeline.
	 */

	__ipipe_walk_pipeline(head);
}

static int __ipipe_exit_irq(struct pt_regs *regs)
{
	int root = __ipipe_root_domain_p;

	if (root) {
#ifdef CONFIG_PPC_970_NAP
		struct thread_info *ti = current_thread_info();
		/* Emulate the napping check when 100% sure we do run
		 * over the root context. */
		if (test_and_clear_bit(TLF_NAPPING, &ti->local_flags))
			regs->nip = regs->link;
#endif
#ifdef CONFIG_PPC64
		ppc64_runlatch_on();
#endif
	}

	if (user_mode(regs) &&
	    (current->ipipe_flags & PF_EVTRET) != 0) {
		/*
		 * Testing for user_regs() eliminates foreign stack
		 * contexts, including from careless domains which did
		 * not set the foreign stack bit (foreign stacks are
		 * always kernel-based).
		 */
		current->ipipe_flags &= ~PF_EVTRET;
		__ipipe_dispatch_event(IPIPE_EVENT_RETURN, regs);
	}

	if (root &&
	    !test_bit(IPIPE_STALL_FLAG, &ipipe_root_cpudom_var(status)))
		return 1;

	return 0;
}

asmlinkage int __ipipe_grab_irq(struct pt_regs *regs)
{
	extern int ppc_spurious_interrupts;
	int irq;

	irq = ppc_md.get_irq();
	if (unlikely(irq == NO_IRQ)) {
		ppc_spurious_interrupts++;
		return __ipipe_exit_irq(regs);
	}

	if (likely(irq != NO_IRQ_IGNORE)) {
		ipipe_trace_irq_entry(irq);
#ifdef CONFIG_SMP
		/* Check for cascaded I-pipe IPIs */
		if (irq == __ipipe_ipi_irq)
			__ipipe_ipi_demux(irq, regs);
		else
#endif /* CONFIG_SMP */
			__ipipe_handle_irq(irq, regs);
	}

	ipipe_trace_irq_exit(irq);

	return __ipipe_exit_irq(regs);
}

static void __ipipe_do_IRQ(unsigned irq, void *cookie)
{
	struct pt_regs *old_regs;

	/* Provide a valid register frame, even if not the exact one. */
	old_regs = set_irq_regs(&__raw_get_cpu_var(__ipipe_tick_regs));
	irq_enter();
	check_stack_overflow();
	handle_one_irq(irq);
	irq_exit();
	set_irq_regs(old_regs);
}

static void __ipipe_do_timer(unsigned irq, void *cookie)
{
	check_stack_overflow();
	timer_interrupt(&__raw_get_cpu_var(__ipipe_tick_regs));
}

asmlinkage int __ipipe_grab_timer(struct pt_regs *regs)
{
	struct ipipe_domain *ipd, *head;

	ipd = __ipipe_current_domain;
	head = __ipipe_pipeline_head();

	set_dec(DECREMENTER_MAX);

	ipipe_trace_irq_entry(IPIPE_TIMER_VIRQ);

	__raw_get_cpu_var(__ipipe_tick_regs).msr = regs->msr; /* for timer_interrupt() */
	__raw_get_cpu_var(__ipipe_tick_regs).nip = regs->nip;

	if (ipd != &ipipe_root)
		__raw_get_cpu_var(__ipipe_tick_regs).msr &= ~MSR_EE;
	else if (unlikely(list_empty(&__ipipe_pipeline)))
		head = ipd;

	if (test_bit(IPIPE_WIRED_FLAG, &head->irqs[IPIPE_TIMER_VIRQ].control))
		/*
		 * Finding a wired IRQ means that we do have a
		 * registered head domain as well. The decrementer
		 * interrupt requires no acknowledge, so we may branch
		 * to the wired IRQ dispatcher directly. Additionally,
		 * we may bypass checks for locked interrupts or
		 * stalled stage (the decrementer cannot be locked and
		 * the head domain is obviously not stalled since we
		 * got there).
		 */
		__ipipe_dispatch_wired_nocheck(head, IPIPE_TIMER_VIRQ);
	else
		__ipipe_handle_irq(IPIPE_TIMER_VIRQ, NULL);

	ipipe_trace_irq_exit(IPIPE_TIMER_VIRQ);

	return __ipipe_exit_irq(regs);
}

asmlinkage notrace int __ipipe_check_root(void) /* hw IRQs off */
{
	return __ipipe_root_domain_p;
}

#ifdef CONFIG_PPC64

#include <asm/firmware.h>
#include <asm/lv1call.h>

asmlinkage notrace void __ipipe_restore_if_root(unsigned long x) /* hw IRQs on */
{
	struct ipipe_percpu_domain_data *p;
	unsigned long flags;

	local_irq_save_hw(flags);

	if (likely(!__ipipe_root_domain_p))
		goto done;

	p = ipipe_root_cpudom_ptr();

	if (x)
		__set_bit(IPIPE_STALL_FLAG, &p->status);
	else
		__clear_bit(IPIPE_STALL_FLAG, &p->status);

	if ((int)mfspr(SPRN_DEC) < 0)
		mtspr(SPRN_DEC, 1);

	/*
	 * Force the delivery of pending soft-disabled interrupts on
	 * PS3.  Any HV call will have this side effect.
	 */
	if (firmware_has_feature(FW_FEATURE_PS3_LV1)) {
		u64 tmp;
		lv1_get_version_info(&tmp);
	}
done:
	local_irq_restore_hw(flags);
}

#endif /* CONFIG_PPC64 */

#ifdef CONFIG_PREEMPT

asmlinkage void __sched preempt_schedule_irq(void);

void __sched  __ipipe_preempt_schedule_irq(void)
{
	struct ipipe_percpu_domain_data *p; 
	unsigned long flags;  
	/*
	 * We have no IRQ state fixup on entry to exceptions, so we
	 * have to stall the root stage before rescheduling.
	 */  
#ifdef CONFIG_IPIPE_DEBUG
	BUG_ON(!irqs_disabled_hw());
#endif
	local_irq_save(flags);	
	local_irq_enable_hw();	
	preempt_schedule_irq(); /* Ok, may reschedule now. */  
	local_irq_disable_hw(); 
	/*
	 * Flush any pending interrupt that may have been logged after
	 * preempt_schedule_irq() stalled the root stage before
	 * returning to us, and now.
	 */
	p = ipipe_root_cpudom_ptr(); 
	if (unlikely(__ipipe_ipending_p(p))) {
		add_preempt_count(PREEMPT_ACTIVE);
		clear_bit(IPIPE_STALL_FLAG, &p->status); 
		__ipipe_sync_pipeline(IPIPE_IRQ_DOALL); 
		sub_preempt_count(PREEMPT_ACTIVE);
	} 

	__local_irq_restore_nosync(flags);  
}

#endif /* CONFIG_PREEMPT */

#ifdef CONFIG_IPIPE_TRACE_IRQSOFF

asmlinkage notrace void __ipipe_trace_irqsoff(void)
{
	ipipe_trace_irqsoff();
}

asmlinkage notrace void __ipipe_trace_irqson(void)
{
	ipipe_trace_irqson();
}

asmlinkage notrace void __ipipe_trace_irqsx(unsigned long msr_ee)
{
	if (msr_ee)
		ipipe_trace_irqson();
	else
		ipipe_trace_irqsoff();
}

#endif

asmlinkage int __ipipe_syscall_root(struct pt_regs *regs)
{
	struct ipipe_percpu_domain_data *p;
        int ret;

#ifdef CONFIG_PPC64
	WARN_ON_ONCE(!irqs_disabled_hw());
	/*
	 * Unlike ppc32, hw interrupts are off on entry here.  We did
	 * not copy the stall state on entry yet, so do it now.
	 */
	p = ipipe_root_cpudom_ptr();
	regs->softe = !test_bit(IPIPE_STALL_FLAG, &p->status);

	/* We ran DISABLE_INTS before being sent to the syscall
	 * dispatcher, so we need to unstall the root stage, unless
	 * the root domain is not current. */
	if (__ipipe_root_domain_p)
		__clear_bit(IPIPE_STALL_FLAG, &p->status);
#else
	WARN_ON_ONCE(irqs_disabled_hw());
#endif
        /*
         * This routine either returns:
         * 0 -- if the syscall is to be passed to Linux;
         * >0 -- if the syscall should not be passed to Linux, and no
         * tail work should be performed;
         * <0 -- if the syscall should not be passed to Linux but the
         * tail work has to be performed (for handling signals etc).
         */

        if (!__ipipe_syscall_watched_p(current, regs->gpr[0]) ||
            !__ipipe_event_monitored_p(IPIPE_EVENT_SYSCALL))
                return 0;

#ifdef CONFIG_PPC64
	local_irq_enable_hw();
#endif
        ret = __ipipe_dispatch_event(IPIPE_EVENT_SYSCALL, regs);

	local_irq_disable_hw();

	/*
	 * This is the end of the syscall path, so we may
	 * safely assume a valid Linux task stack here.
	 */
	if (current->ipipe_flags & PF_EVTRET) {
		current->ipipe_flags &= ~PF_EVTRET;
		__ipipe_dispatch_event(IPIPE_EVENT_RETURN, regs);
	}

        if (!__ipipe_root_domain_p) {
#ifdef CONFIG_PPC32
		local_irq_enable_hw();
#endif
		return 1;
	}

	p = ipipe_root_cpudom_ptr();
	if (__ipipe_ipending_p(p))
		__ipipe_sync_pipeline(IPIPE_IRQ_DOVIRT);

#ifdef CONFIG_PPC32
	local_irq_enable_hw();
#endif

	return -ret;
}

void __ipipe_pin_range_globally(unsigned long start, unsigned long end)
{
	/* We don't support this. */
}

#ifdef CONFIG_SMP
EXPORT_SYMBOL(__ipipe_stall_root);
EXPORT_SYMBOL(__ipipe_test_root);
EXPORT_SYMBOL(__ipipe_test_and_stall_root);
#else
EXPORT_SYMBOL_GPL(last_task_used_math);
#endif

EXPORT_SYMBOL_GPL(__switch_to);
EXPORT_SYMBOL_GPL(show_stack);
EXPORT_SYMBOL_GPL(_switch);
EXPORT_SYMBOL_GPL(tasklist_lock);
#ifdef CONFIG_PPC64
EXPORT_PER_CPU_SYMBOL(ppc64_tlb_batch);
EXPORT_SYMBOL_GPL(switch_slb);
EXPORT_SYMBOL_GPL(switch_stab);
EXPORT_SYMBOL_GPL(__flush_tlb_pending);
EXPORT_SYMBOL_GPL(mmu_linear_psize);
EXPORT_SYMBOL_GPL(mmu_psize_defs);
#else  /* !CONFIG_PPC64 */
void atomic_set_mask(unsigned long mask, unsigned long *ptr);
void atomic_clear_mask(unsigned long mask, unsigned long *ptr);
#ifdef FEW_CONTEXTS
EXPORT_SYMBOL_GPL(nr_free_contexts);
EXPORT_SYMBOL_GPL(context_mm);
EXPORT_SYMBOL_GPL(steal_context);
#endif	/* !FEW_CONTEXTS */
EXPORT_SYMBOL_GPL(atomic_set_mask);
EXPORT_SYMBOL_GPL(atomic_clear_mask);
#endif	/* !CONFIG_PPC64 */
