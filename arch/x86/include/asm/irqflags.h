#ifndef _X86_IRQFLAGS_H_
#define _X86_IRQFLAGS_H_

#include <asm/processor-flags.h>

#ifndef __ASSEMBLY__

#include <linux/compiler.h>
#include <linux/types.h>

/*
 * Interrupt control:
 */

static inline unsigned long native_save_fl(void)
{
	unsigned long flags;

	/*
	 * "=rm" is safe here, because "pop" adjusts the stack before
	 * it evaluates its effective address -- this is part of the
	 * documented behavior of the "pop" instruction.
	 */
	asm volatile("# __raw_save_flags\n\t"
		     "pushf ; pop %0"
		     : "=rm" (flags)
		     : /* no input */
		     : "memory");

	return flags;
}

static inline unsigned long native_save_flags(void)
{
	return native_save_fl();
}

static inline void native_restore_fl(unsigned long flags)
{
	asm volatile("push %0 ; popf"
		     : /* no output */
		     :"g" (flags)
		     :"memory", "cc");
}

static inline void native_irq_restore(unsigned long flags)
{
	return native_restore_fl(flags);
}

static inline void native_irq_disable(void)
{
	asm volatile("cli": : :"memory");
}

static inline unsigned long native_irq_save(void)
{
	unsigned long flags = native_save_flags();
	native_irq_disable();
	return flags;
}

static inline void native_irq_enable(void)
{
	asm volatile("sti": : :"memory");
}

static inline void native_safe_halt(void)
{
	asm volatile("sti; hlt": : :"memory");
}

static inline void native_halt(void)
{
	asm volatile("hlt": : :"memory");
}

static inline bool native_irqs_disabled_flags(unsigned long flags)
{
	return !(flags & X86_EFLAGS_IF);
}

static inline bool native_irqs_disabled(void)
{
	unsigned long flags = native_save_flags();

	return native_irqs_disabled_flags(flags);
}

#endif

#ifdef CONFIG_PARAVIRT
#include <asm/paravirt.h>
#else
#ifndef __ASSEMBLY__
#include <asm/irq_pipeline.h>

#ifndef CONFIG_IRQ_PIPELINE

static inline notrace unsigned long arch_local_save_flags(void)
{
	return native_save_fl();
}

static inline notrace void arch_local_irq_restore(unsigned long flags)
{
	native_restore_fl(flags);
}

static inline notrace void arch_local_irq_disable(void)
{
	native_irq_disable();
}

static inline notrace void arch_local_irq_enable(void)
{
	native_irq_enable();
}

/*
 * Used in the idle loop; sti takes one instruction cycle
 * to complete:
 */
static inline void arch_safe_halt(void)
{
	native_safe_halt();
}

#endif /* !CONFIG_IRQ_PIPELINE */

/*
 * Used when interrupts are already enabled or to
 * shutdown the processor:
 */
static inline void halt(void)
{
	native_halt();
}

/*
 * For spinlocks, etc:
 */
static inline notrace unsigned long arch_local_irq_save(void)
{
	unsigned long flags = arch_local_save_flags();
	arch_local_irq_disable();
	return flags;
}

static inline int arch_irqs_disabled_flags(unsigned long flags)
{
	return native_irqs_disabled_flags(flags);
}

#else  /* __ASSEMBLY__ */

#define ENABLE_INTERRUPTS(x)	sti
#define DISABLE_INTERRUPTS(x)	cli
#ifdef CONFIG_IRQ_PIPELINE
#define ENABLE_INTERRUPTS_IF_PIPELINED(x)	ENABLE_INTERRUPTS(x)
#define DISABLE_INTERRUPTS_IF_PIPELINED(x)	DISABLE_INTERRUPTS(x)
#else
#define ENABLE_INTERRUPTS_IF_PIPELINED(x)
#define DISABLE_INTERRUPTS_IF_PIPELINED(x)
#endif

#ifdef CONFIG_X86_64
#define SWAPGS	swapgs
/*
 * Currently paravirt can't handle swapgs nicely when we
 * don't have a stack we can rely on (such as a user space
 * stack).  So we either find a way around these or just fault
 * and emulate if a guest tries to call swapgs directly.
 *
 * Either way, this is a good way to document that we don't
 * have a reliable stack. x86_64 only.
 */
#define SWAPGS_UNSAFE_STACK	swapgs

#define PARAVIRT_ADJUST_EXCEPTION_FRAME	/*  */

#define INTERRUPT_RETURN	jmp native_iret
#define USERGS_SYSRET64				\
	swapgs;					\
	sysretq;
#define USERGS_SYSRET32				\
	swapgs;					\
	sysretl

#else
#define INTERRUPT_RETURN		iret
#define ENABLE_INTERRUPTS_SYSEXIT	sti; sysexit
#define GET_CR0_INTO_EAX		movl %cr0, %eax
#endif


#endif /* __ASSEMBLY__ */
#endif /* CONFIG_PARAVIRT */

#ifdef __ASSEMBLY__
#ifdef CONFIG_TRACE_IRQFLAGS
#  define TRACE_IRQS_ON		call trace_hardirqs_on_thunk;
#  define TRACE_IRQS_OFF	call trace_hardirqs_off_thunk;
#else
#  define TRACE_IRQS_ON
#  define TRACE_IRQS_OFF
#endif
#ifdef CONFIG_DEBUG_LOCK_ALLOC
#  ifdef CONFIG_X86_64
#    define LOCKDEP_SYS_EXIT		call lockdep_sys_exit_thunk
#    define LOCKDEP_SYS_EXIT_IRQ \
	TRACE_IRQS_ON; \
	sti; \
	call lockdep_sys_exit_thunk; \
	cli; \
	TRACE_IRQS_OFF;
#  else
#    define LOCKDEP_SYS_EXIT \
	pushl %eax;				\
	pushl %ecx;				\
	pushl %edx;				\
	pushfl;					\
	sti;					\
	call lockdep_sys_exit;			\
	popfl;					\
	popl %edx;				\
	popl %ecx;				\
	popl %eax;
#    define LOCKDEP_SYS_EXIT_IRQ
#  endif
#else
#  define LOCKDEP_SYS_EXIT
#  define LOCKDEP_SYS_EXIT_IRQ
#endif
#else
static inline int arch_irqs_disabled(void)
{
	unsigned long flags = arch_local_save_flags();

	return arch_irqs_disabled_flags(flags);
}
#endif /* __ASSEMBLY__ */

#endif
