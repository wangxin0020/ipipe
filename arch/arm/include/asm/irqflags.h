#ifndef __ASM_ARM_IRQFLAGS_H
#define __ASM_ARM_IRQFLAGS_H

#ifdef __KERNEL__

#include <asm/ptrace.h>

/*
 * CPU interrupt mask handling.
 */
#ifdef CONFIG_CPU_V7M
#define IRQMASK_REG_NAME_R "primask"
#define IRQMASK_REG_NAME_W "primask"
#define IRQMASK_I_BIT	1
#else
#define IRQMASK_REG_NAME_R "cpsr"
#define IRQMASK_REG_NAME_W "cpsr_c"
#define IRQMASK_I_BIT	PSR_I_BIT
#endif

#if __LINUX_ARM_ARCH__ >= 6

static inline unsigned long native_irq_save(void)
{
	unsigned long flags;

	asm volatile(
		"	mrs	%0, " IRQMASK_REG_NAME_R "	@ native_save_irq\n"
		"	cpsid	i"
		: "=r" (flags) : : "memory", "cc");
	return flags;
}

static inline void native_irq_enable(void)
{
	asm volatile(
		"	cpsie i			@ native_irq_enable"
		:
		:
		: "memory", "cc");
}

static inline void native_irq_disable(void)
{
	asm volatile(
		"	cpsid i			@ native_irq_disable"
		:
		:
		: "memory", "cc");
}

#define local_fiq_enable()  __asm__("cpsie f	@ __stf" : : : "memory", "cc")
#define local_fiq_disable() __asm__("cpsid f	@ __clf" : : : "memory", "cc")
#else

/*
 * Save the current interrupt enable state & disable IRQs
 */
static inline unsigned long native_irq_save(void)
{
	unsigned long flags, temp;

	asm volatile(
		"	mrs	%0, cpsr	@ native_save_irq\n"
		"	orr	%1, %0, #128\n"
		"	msr	cpsr_c, %1"
		: "=r" (flags), "=r" (temp)
		:
		: "memory", "cc");
	return flags;
}

/*
 * Enable IRQs
 */
static inline void native_irq_enable(void)
{
	unsigned long temp;
	asm volatile(
		"	mrs	%0, cpsr	@ native_irq_enable\n"
		"	bic	%0, %0, #128\n"
		"	msr	cpsr_c, %0"
		: "=r" (temp)
		:
		: "memory", "cc");
}

/*
 * Disable IRQs
 */
static inline void native_irq_disable(void)
{
	unsigned long temp;
	asm volatile(
		"	mrs	%0, cpsr	@ native_irq_disable\n"
		"	orr	%0, %0, #128\n"
		"	msr	cpsr_c, %0"
		: "=r" (temp)
		:
		: "memory", "cc");
}

/*
 * Enable FIQs
 */
#define local_fiq_enable()					\
	({							\
		unsigned long temp;				\
	__asm__ __volatile__(					\
	"mrs	%0, cpsr		@ stf\n"		\
"	bic	%0, %0, #64\n"					\
"	msr	cpsr_c, %0"					\
	: "=r" (temp)						\
	:							\
	: "memory", "cc");					\
	})

/*
 * Disable FIQs
 */
#define local_fiq_disable()					\
	({							\
		unsigned long temp;				\
	__asm__ __volatile__(					\
	"mrs	%0, cpsr		@ clf\n"		\
"	orr	%0, %0, #64\n"					\
"	msr	cpsr_c, %0"					\
	: "=r" (temp)						\
	:							\
	: "memory", "cc");					\
	})

#endif

/*
 * Save the current interrupt enable state.
 */
static inline unsigned long native_save_flags(void)
{
	unsigned long flags;
	asm volatile(
		"	mrs	%0, " IRQMASK_REG_NAME_R "	@ native_save_flags"
		: "=r" (flags) : : "memory", "cc");
	return flags;
}

/*
 * restore saved IRQ & FIQ state
 */
static inline void native_irq_restore(unsigned long flags)
{
	asm volatile(
		"	msr	" IRQMASK_REG_NAME_W ", %0	@ native_irq_restore"
		:
		: "r" (flags)
		: "memory", "cc");
}

static inline bool native_irqs_disabled_flags(unsigned long flags)
{
	return (flags & IRQMASK_I_BIT) != 0;
}

static inline bool native_irqs_disabled(void)
{
	unsigned long flags = native_save_flags();
	return native_irqs_disabled_flags(flags);
}

#include <asm/irq_pipeline.h>

#ifndef CONFIG_IRQ_PIPELINE

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

static inline unsigned long arch_local_save_flags(void)
{
	return native_save_flags();
}

static inline void arch_local_irq_restore(unsigned long flags)
{
	native_irq_restore(flags);
}

static inline int arch_irqs_disabled_flags(unsigned long flags)
{
	return native_irqs_disabled_flags(flags);
}

#endif /* !CONFIG_IRQ_PIPELINE */

static inline int arch_irqs_disabled(void)
{
	unsigned long flags = arch_local_save_flags();

	return arch_irqs_disabled_flags(flags);
}

#endif /* ifdef __KERNEL__ */
#endif /* ifndef __ASM_ARM_IRQFLAGS_H */
