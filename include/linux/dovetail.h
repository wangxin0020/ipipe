/*
 * include/linux/dovetail.h
 *
 * Copyright (C) 2016 Philippe Gerum.
 */
#ifndef _LINUX_DOVETAIL_H
#define _LINUX_DOVETAIL_H

#include <linux/sched.h>
#include <linux/irq_pipeline.h>
#include <linux/irq.h>
#include <asm/dovetail.h>

#ifdef CONFIG_DOVETAIL

#define _DOVETAIL_SYSCALL_P	0
#define _DOVETAIL_TRAP_P	1
#define _DOVETAIL_KEVENT_P	2
#define _DOVETAIL_SYSCALL_E	(1 << _DOVETAIL_SYSCALL_P)
#define _DOVETAIL_TRAP_E	(1 << _DOVETAIL_TRAP_P)
#define _DOVETAIL_KEVENT_E	(1 << _DOVETAIL_KEVENT_P)
#define _DOVETAIL_ALL_E		0x7
#define _DOVETAIL_SYSCALL_R	(8 << _DOVETAIL_SYSCALL_P)
#define _DOVETAIL_TRAP_R	(8 << _DOVETAIL_TRAP_P)
#define _DOVETAIL_KEVENT_R	(8 << _DOVETAIL_KEVENT_P)
#define _DOVETAIL_SHIFT_R	3
#define _DOVETAIL_ALL_R		(_DOVETAIL_ALL_E << _DOVETAIL_SHIFT_R)

#define DOVETAIL_SYSCALLS	_DOVETAIL_SYSCALL_E
#define DOVETAIL_TRAPS		_DOVETAIL_TRAP_E
#define DOVETAIL_KEVENTS	_DOVETAIL_KEVENT_E

#define KEVENT_SCHEDULE		0
#define KEVENT_SIGWAKE		1
#define KEVENT_SETSCHED		2
#define KEVENT_SETAFFINITY	3
#define KEVENT_EXIT		4
#define KEVENT_CLEANUP		5
#define KEVENT_HOSTRT		6

struct ipipe_hostrt_data {
	short live;
	seqcount_t seqcount;
	time_t wall_time_sec;
	u32 wall_time_nsec;
	struct timespec wall_to_monotonic;
	cycle_t cycle_last;
	cycle_t mask;
	u32 mult;
	u32 shift;
};

struct dovetail_trap_data {
	int exception;
	struct pt_regs *regs;
};

struct dovetail_migration_data {
	struct task_struct *task;
	int dest_cpu;
};

struct hypervisor_stall {
	void (*handler)(struct hypervisor_stall *nfy);
};

int dovetail_handle_syscall_slow(struct pt_regs *regs);

int dovetail_handle_trap(int trapnr, struct pt_regs *regs);

int dovetail_handle_kevent(int event, void *data);

#ifdef CONFIG_DOVETAIL_TRACK_VM_GUEST
void dovetail_hypervisor_stall(void);
#else
static inline void dovetail_hypervisor_stall(void) { }
#endif

static inline void dovetail_leave_root(void)
{
	dovetail_hypervisor_stall();
	irq_mute_all();
}

static inline int dovetail_trap(int trapnr, struct pt_regs *regs)
{
	return dovetail_handle_trap(trapnr, regs);
}

static inline void dovetail_signal_task(struct task_struct *p)
{
	if (test_ti_local_flags(task_thread_info(p), _TLF_DOVETAIL))
		dovetail_handle_kevent(KEVENT_SIGWAKE, p);
}

static inline void dovetail_change_task_affinity(struct task_struct *p, int cpu)
{
	if (test_ti_local_flags(task_thread_info(p), _TLF_DOVETAIL)) {
		struct dovetail_migration_data d = {
			.task = p,
			.dest_cpu = cpu,
		};
		dovetail_handle_kevent(KEVENT_SETAFFINITY, &d);
	}
}

static inline void dovetail_task_exit(void)
{
	if (test_thread_local_flags(_TLF_DOVETAIL))
		dovetail_handle_kevent(KEVENT_EXIT, NULL);
}

static inline void dovetail_change_task_scheduler(struct task_struct *p)
{
	if (test_ti_local_flags(task_thread_info(p), _TLF_DOVETAIL))
		dovetail_handle_kevent(KEVENT_SETSCHED, p);
}

static inline
void dovetail_context_switch(struct task_struct *next)
{
	struct task_struct *prev = current;

	if (test_ti_local_flags(task_thread_info(next), _TLF_DOVETAIL) ||
	    test_ti_local_flags(task_thread_info(prev), _TLF_DOVETAIL)) {
		__this_cpu_write(irq_pipeline.rqlock_owner, prev);
		dovetail_handle_kevent(KEVENT_SCHEDULE, next);
	}
}

int dovetail_context_switch_tail(void);

static inline void dovetail_mm_cleanup(struct mm_struct *mm)
{
	dovetail_handle_kevent(KEVENT_CLEANUP, mm);
}

/* Hypervisor-side calls, hw IRQs off. */
static inline void dovetail_enter_vm_guest(struct hypervisor_stall *nfy)
{
	struct irq_pipeline_data *p = raw_cpu_ptr(&irq_pipeline);
	p->vm_notifier = nfy;
	barrier();
}

static inline void dovetail_exit_vm_guest(void)
{
	struct irq_pipeline_data *p = raw_cpu_ptr(&irq_pipeline);
	p->vm_notifier = NULL;
	barrier();
}

static inline void dovetail_clear_callouts(struct irq_stage_data *p)
{
	p->coflags &= ~_DOVETAIL_ALL_R;
}

#ifdef CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH

static inline void __prepare_arch_switch(struct task_struct *next)
{
	hard_local_irq_enable();
	dovetail_context_switch(next);
}

#ifndef dovetail_get_active_mm
static inline struct mm_struct *dovetail_get_active_mm(void)
{
	return __this_cpu_read(irq_pipeline.active_mm);
}
#define dovetail_get_active_mm dovetail_get_active_mm
#endif

#else /* !CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH */

static inline void __prepare_arch_switch(struct task_struct *next)
{
	dovetail_context_switch(next);
	hard_local_irq_disable();
}

#ifndef dovetail_get_active_mm
#define dovetail_get_active_mm()	(current->active_mm)
#endif

#endif /* !CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH */

#define prepare_arch_switch __prepare_arch_switch

void dovetail_enable(int flags);

void dovetail_disable(void);

void dovetail_complete_domain_migration(void);

int dovetail_enter_head(void);

void dovetail_leave_head(void);

void dovetail_host_events(struct irq_stage *stage,
			  int event_mask);

extern bool __ipipe_probe_access;

long ipipe_probe_kernel_read(void *dst, void *src, size_t size);
long ipipe_probe_kernel_write(void *dst, void *src, size_t size);

#if defined(CONFIG_DEBUG_ATOMIC_SLEEP) || defined(CONFIG_PROVE_LOCKING) || \
	defined(CONFIG_PREEMPT_VOLUNTARY) || defined(CONFIG_DOVETAIL_DEBUG)
extern void __ipipe_uaccess_might_fault(void);
#else
#define __ipipe_uaccess_might_fault() might_fault()
#endif

static inline
struct dovetail_thread_state *dovetail_current_state(void)
{
	return &current_thread_info()->dovetail_state;
}

static inline
struct dovetail_thread_state *dovetail_task_state(struct task_struct *p)
{
	return &task_thread_info(p)->dovetail_state;
}

static inline void dovetail_send_mayday(struct task_struct *castaway)
{
	struct thread_info *ti = task_thread_info(castaway);

	ipipe_check_irqoff();
	if (test_ti_local_flags(ti, _TLF_DOVETAIL))
		set_ti_thread_flag(ti, TIF_MAYDAY);
}

#else	/* !CONFIG_DOVETAIL */

struct irq_stage_data;

static inline int dovetail_trap(int trapnr, struct pt_regs *regs)
{
	return 0;
}

static inline void dovetail_signal_task(struct task_struct *p) { }

static inline
void dovetail_change_task_affinity(struct task_struct *p, int cpu) { }

static inline void dovetail_task_exit(void) { }

static inline void dovetail_change_task_scheduler(struct task_struct *p) { }

static inline void dovetail_mm_cleanup(struct mm_struct *mm) { }

#define dovetail_enter_vm_guest(__nfy) do { } while (0)

#define dovetail_exit_vm_guest(__nfy) do { } while (0)

static inline void dovetail_complete_domain_migration(void) { }

static inline int dovetail_context_switch_tail(void)
{
	return 0;
}

static inline void dovetail_clear_callouts(struct irq_stage_data *p) { }

#define ipipe_probe_kernel_read(d, s, sz)	probe_kernel_read(d, s, sz)
#define ipipe_probe_kernel_write(d, s, sz)	probe_kernel_write(d, s, sz)
#define __ipipe_uaccess_might_fault()		might_fault()

#endif	/* !CONFIG_DOVETAIL */

#if !defined(CONFIG_DOVETAIL) || defined(CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH)

#define dovetail_switch_mm_enter(flags)		\
  do { (void)(flags); } while (0)

#define dovetail_switch_mm_exit(flags)	\
  do { (void)(flags); } while (0)

#else /* CONFIG_DOVETAIL && !CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH */

#define dovetail_switch_mm_enter(flags)		\
  do {						\
    	(flags) = hard_cond_local_irq_save();	\
	barrier();				\
  } while (0)					\

#define dovetail_switch_mm_exit(flags)	\
  do {						\
	barrier();				\
    	hard_cond_local_irq_restore(flags);	\
  } while (0)					\

#endif /* CONFIG_DOVETAIL && !CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH */

struct timekeeper;

#ifdef CONFIG_DOVETAIL_HAVE_HOSTRT
void dovetail_update_hostrt(struct timekeeper *tk);
#else
static inline void
dovetail_update_hostrt(struct timekeeper *tk) {}
#endif

static inline bool dovetailing(void)
{
	return IS_ENABLED(CONFIG_DOVETAIL);
}

static inline bool dovetail_debug(void)
{
	return IS_ENABLED(CONFIG_DOVETAIL_DEBUG);
}

#endif /* _LINUX_DOVETAIL_H */
