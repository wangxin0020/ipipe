/*   -*- linux-c -*-
 *   include/asm-blackfin/ipipe.h
 *
 *   Copyright (C) 2002-2007 Philippe Gerum.
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

#ifndef __ASM_BLACKFIN_IPIPE_H
#define __ASM_BLACKFIN_IPIPE_H

#ifdef CONFIG_IPIPE

#include <linux/cpumask.h>
#include <linux/list.h>
#include <linux/threads.h>
#include <linux/irq.h>
#include <asm/ptrace.h>
#include <asm/irq.h>
#include <asm/bitops.h>
#include <linux/atomic.h>
#include <asm/traps.h>
#include <asm/bitsperlong.h>

#define IPIPE_CORE_RELEASE	1

#ifdef CONFIG_SMP
#error "I-pipe/blackfin: SMP not implemented"
#endif	/* CONFIG_SMP */

struct ipipe_arch_sysinfo {
};

#define ipipe_read_tsc(t)					\
	({							\
	unsigned long __cy2;					\
	__asm__ __volatile__ ("1: %0 = CYCLES2\n"		\
				"%1 = CYCLES\n"			\
				"%2 = CYCLES2\n"		\
				"CC = %2 == %0\n"		\
				"if ! CC jump 1b\n"		\
				: "=d,a" (((unsigned long *)&t)[1]),	\
				  "=d,a" (((unsigned long *)&t)[0]),	\
				  "=d,a" (__cy2)				\
				: /*no input*/ : "CC");			\
	t;								\
	})

#define ipipe_cpu_freq()	__ipipe_hrclock_freq
#define ipipe_tsc2ns(_t)	(((unsigned long)(_t)) * __ipipe_freq_scale)
#define ipipe_tsc2us(_t)	(ipipe_tsc2ns(_t) / 1000 + 1)

/* Private interface -- Internal use only */

void __ipipe_call_irqtail(unsigned long addr);

extern unsigned long __ipipe_freq_scale;

extern unsigned long __ipipe_irq_tail_hook;

DECLARE_PER_CPU(int, __ipipe_defer_root_sync);

#ifdef CONFIG_BF561
#define bfin_write_TIMER_DISABLE(val)	bfin_write_TMRS8_DISABLE(val)
#define bfin_write_TIMER_ENABLE(val)	bfin_write_TMRS8_ENABLE(val)
#define bfin_write_TIMER_STATUS(val)	bfin_write_TMRS8_STATUS(val)
#define bfin_read_TIMER_STATUS()	bfin_read_TMRS8_STATUS()
#elif defined(CONFIG_BF54x)
#define bfin_write_TIMER_DISABLE(val)	bfin_write_TIMER_DISABLE0(val)
#define bfin_write_TIMER_ENABLE(val)	bfin_write_TIMER_ENABLE0(val)
#define bfin_write_TIMER_STATUS(val)	bfin_write_TIMER_STATUS0(val)
#define bfin_read_TIMER_STATUS(val)	bfin_read_TIMER_STATUS0(val)
#endif

static inline bool arch_is_root_tick(struct pt_regs *regs)
{
	return (regs->ipend & 0x10) != 0;
}

#endif /* CONFIG_IPIPE */

#ifdef CONFIG_TICKSOURCE_CORETMR
#define IRQ_SYSTMR		IRQ_CORETMR
#define IRQ_PRIOTMR		IRQ_CORETMR
#else
#define IRQ_SYSTMR		IRQ_TIMER0
#define IRQ_PRIOTMR		CONFIG_IRQ_TIMER0
#endif

#endif	/* !__ASM_BLACKFIN_IPIPE_H */
