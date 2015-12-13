/*
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_IRQFLAGS_H
#define __ASM_IRQFLAGS_H

#ifdef __KERNEL__

#include <linux/types.h>
#include <asm/ptrace.h>

static inline unsigned long native_irq_save(void)
{
	unsigned long flags;
	asm volatile(
		"mrs	%0, daif		// native_irq_save\n"
		"msr	daifset, #2"
		: "=r" (flags)
		:
		: "memory");
	return flags;
}

static inline void native_irq_enable(void)
{
	asm volatile(
		"msr	daifclr, #2		// native_irq_enable"
		:
		:
		: "memory");
}

static inline void native_irq_disable(void)
{
	asm volatile(
		"msr	daifset, #2		// native_irq_disable"
		:
		:
		: "memory");
}

static inline unsigned long native_save_flags(void)
{
	unsigned long flags;
	asm volatile(
		"mrs	%0, daif		// native_save_flags"
		: "=r" (flags)
		:
		: "memory");
	return flags;
}

static inline void native_irq_restore(unsigned long flags)
{
	asm volatile(
		"msr	daif, %0		// arch_local_irq_restore"
	:
	: "r" (flags)
	: "memory");
}

static inline bool native_irqs_disabled_flags(unsigned long flags)
{
	return flags & PSR_I_BIT;
}

static inline bool native_irqs_disabled(void)
{
	unsigned long flags = native_save_flags();
	return native_irqs_disabled_flags(flags);
}

#include <asm/irq_pipeline.h>

#ifndef CONFIG_IRQ_PIPELINE
/*
 * CPU interrupt mask handling.
 */
static inline unsigned long arch_local_irq_save(void)
{
	return native_irq_save();
}

static inline void arch_local_irq_enable(void)
{
	native_irq_enable();
}

static inline void arch_local_irq_disable(void)
{
	native_irq_disable();
}

/*
 * Save the current interrupt enable state.
 */
static inline unsigned long arch_local_save_flags(void)
{
	return native_save_flags();
}

/*
 * restore saved IRQ state
 */
static inline void arch_local_irq_restore(unsigned long flags)
{
	native_irq_restore(flags);
}

static inline int arch_irqs_disabled_flags(unsigned long flags)
{
	return native_irqs_disabled_flags(flags);
}

#endif /* !CONFIG_IRQ_PIPELINE */

/*
 * save and restore debug state
 */
#define local_dbg_save(flags)						\
	do {								\
		typecheck(unsigned long, flags);			\
		asm volatile(						\
		"mrs    %0, daif		// local_dbg_save\n"	\
		"msr    daifset, #8"					\
		: "=r" (flags) : : "memory");				\
	} while (0)

#define local_dbg_restore(flags)					\
	do {								\
		typecheck(unsigned long, flags);			\
		asm volatile(						\
		"msr    daif, %0		// local_dbg_restore\n"	\
		: : "r" (flags) : "memory");				\
	} while (0)

#define local_dbg_enable()	asm("msr	daifclr, #8" : : : "memory")
#define local_dbg_disable()	asm("msr	daifset, #8" : : : "memory")

#define local_fiq_enable()	asm("msr	daifclr, #1" : : : "memory")
#define local_fiq_disable()	asm("msr	daifset, #1" : : : "memory")

#define local_async_enable()	asm("msr	daifclr, #4" : : : "memory")
#define local_async_disable()	asm("msr	daifset, #4" : : : "memory")

#endif
#endif
