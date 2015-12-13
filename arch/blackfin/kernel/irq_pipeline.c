/* -*- linux-c -*-
 * arch/blackfin/kernel/irq_pipeline.c
 *
 * Copyright (C) 2005-2007 Philippe Gerum.
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
 * Architecture-dependent I-pipe support for the Blackfin.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/percpu.h>
#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/kthread.h>
#include <linux/unistd.h>
#include <linux/io.h>
#include <linux/atomic.h>
#include <linux/ipipe_tickdev.h>
#include <asm/irq_handler.h>
#include <asm/blackfin.h>
#include <asm/time.h>

asmlinkage void asm_do_IRQ(unsigned int irq, struct pt_regs *regs);

static void __ipipe_no_irqtail(void);

unsigned long __ipipe_irq_tail_hook = (unsigned long)__ipipe_no_irqtail;
EXPORT_SYMBOL_GPL(__ipipe_irq_tail_hook);

unsigned long __ipipe_freq_scale;
EXPORT_SYMBOL_GPL(__ipipe_freq_scale);

DEFINE_PER_CPU(int, __ipipe_defer_root_sync);

void __init arch_irq_pipeline_init(void)
{
	__ipipe_hrclock_freq = get_cclk();
	__ipipe_freq_scale = 1000000000UL / __ipipe_hrclock_freq;
}

static void do_IRQ_pipelined(unsigned int irq, void *cookie)
{
	struct pt_regs *regs = raw_cpu_ptr(&irq_pipeline.tick_regs);
	asm_do_IRQ(irq, regs);
}

static void __ipipe_no_irqtail(void)
{
}

void irq_stage_sync_current(void)
{
	if (!__on_root_stage() || raw_cpu_read(__ipipe_defer_root_sync) == 0)
		__irq_stage_sync_current();
}

void arch_irq_push_stage(struct irq_stage *stage,
			 struct irq_pipeline_clocking *clocking)
{
	clocking->sys_hrclock_freq = __ipipe_hrclock_freq;
	clocking->hrclock_name = "cyclectr";
}

void __ipipe_sync_root(void)
{
	void (*irq_tail_hook)(void) = (void (*)(void))__ipipe_irq_tail_hook;
	struct irq_stage_data *p;
	unsigned long flags;

	BUG_ON(irqs_disabled());

	flags = hard_local_irq_save();

	if (irq_tail_hook)
		irq_tail_hook();

	clear_thread_flag(TIF_IRQ_SYNC);

	p = irq_root_this_context();
	if (irq_staged_waiting(p))
		irq_stage_sync_current();

	hard_local_irq_restore(flags);
}

void __ipipe_lock_root(void)
{
	unsigned long flags;

	flags = hard_smp_local_irq_save();
	raw_cpu_write(__ipipe_defer_root_sync, 1);
	hard_smp_local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(__ipipe_lock_root);

void __ipipe_unlock_root(void)
{
	unsigned long flags;

	flags = hard_smp_local_irq_save();
	raw_cpu_write(__ipipe_defer_root_sync, 0);
	hard_smp_local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(__ipipe_unlock_root);

/*
 * We have two main options on the Blackfin for dealing with the clock
 * event sources for both domains:
 *
 * - If CONFIG_GENERIC_CLOCKEVENTS is disabled (old cranky stuff), we
 * force the system timer to GPT0.  This gives the head domain
 * exclusive control over the Blackfin core timer.  Therefore, we have
 * to flesh out the core timer request and release handlers since the
 * regular kernel won't have set it up at boot.
 *
 * - If CONFIG_GENERIC_CLOCKEVENTS is enabled, then Linux may pick
 * either GPT0 (CONFIG_TICKSOURCE_GPTMR0), or the core timer
 * (CONFIG_TICKSOURCE_CORETMR) as its own tick source. Depending on
 * what Kconfig says regarding this setting, we may have in turn:
 *
 * - CONFIG_TICKSOURCE_CORETMR is set, which means that both root
 * (linux) and the head domain will have to share the core timer for
 * timing duties. In this case, we don't register the core timer with
 * the pipeline, we only connect the regular linux clock event
 * structure to our ipipe_time timer structure via the ipipe_timer
 * field in struct clock_event_device.
 *
 * - CONFIG_TICKSOURCE_GPTMR0 is set, in which case we reserve the
 * core timer to the head domain, just like in the
 * CONFIG_GENERIC_CLOCKEVENTS disabled case. We have to register the
 * core timer with the pipeline, so that ipipe_select_timers() may
 * find it.
 */
#if defined(CONFIG_GENERIC_CLOCKEVENTS) && defined(CONFIG_TICKSOURCE_CORETMR)

static inline void icoretmr_request(struct ipipe_timer *timer, int steal)
{
}

static inline void icoretmr_release(struct ipipe_timer *timer)
{
}

#else /* !(CONFIG_GENERIC_CLOCKEVENTS && CONFIG_TICKSOURCE_CORETMR) */

static void icoretmr_request(struct ipipe_timer *timer, int steal)
{
	bfin_write_TCNTL(TMPWR);
	CSYNC();
	bfin_write_TSCALE(TIME_SCALE - 1);
	bfin_write_TPERIOD(0);
	bfin_write_TCOUNT(0);
	CSYNC();
}

static void icoretmr_release(struct ipipe_timer *timer)
{
	/* Power down the core timer */
	bfin_write_TCNTL(0);
}

#endif /* !(CONFIG_GENERIC_CLOCKEVENTS && CONFIG_TICKSOURCE_CORETMR) */

static int icoretmr_set(unsigned long evt, void *timer)
{
	bfin_write_TCNTL(TMPWR);
	CSYNC();
	bfin_write_TCOUNT(evt);
	CSYNC();
	bfin_write_TCNTL(TMPWR | TMREN);

	return 0;
}

struct ipipe_timer bfin_coretmr_itimer = {
	.irq			= IRQ_CORETMR,
	.request		= icoretmr_request,
	.set			= icoretmr_set,
	.ack			= NULL,
	.release		= icoretmr_release,
	.name			= "bfin_coretmr",
	.rating			= 500,
	.min_delay_ticks	= 2,
};

void bfin_ipipe_coretmr_register(void)
{
	bfin_coretmr_itimer.freq = get_cclk() / TIME_SCALE;
#if !(defined(CONFIG_GENERIC_CLOCKEVENTS) && defined(CONFIG_TICKSOURCE_CORETMR))
	ipipe_timer_register(&bfin_coretmr_itimer);
#endif
}
