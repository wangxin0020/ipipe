/*
 * arch/arm64/include/asm/dovetail.h
 *
 * Copyright (C) 2016 Philippe Gerum.
 */
#ifndef _ASM_ARM64_DOVETAIL_H
#define _ASM_ARM64_DOVETAIL_H

#define IPIPE_TRAP_ACCESS	 0	/* Data or instruction access exception */
#define IPIPE_TRAP_SECTION	 1	/* Section fault */
#define IPIPE_TRAP_DABT		 2	/* Generic data abort */
#define IPIPE_TRAP_UNKNOWN	 3	/* Unknown exception */
#define IPIPE_TRAP_BREAK	 4	/* Instruction breakpoint */
#define IPIPE_TRAP_FPU_ACC	 5	/* Floating point access */
#define IPIPE_TRAP_FPU_EXC	 6	/* Floating point exception */
#define IPIPE_TRAP_UNDEFINSTR	 7	/* Undefined instruction */
#define IPIPE_TRAP_ALIGNMENT	 8	/* Unaligned access exception */
#define IPIPE_TRAP_MAYDAY        9	/* Internal recovery trap */
#define IPIPE_NR_FAULTS         10

#define dovetail_get_active_mm()	__this_cpu_read(irq_pipeline.active_mm)

#endif /* _ASM_ARM64_DOVETAIL_H */
