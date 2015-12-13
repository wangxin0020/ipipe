/*
 * arch/powerpc/include/asm/dovetail.h
 *
 * Copyright (C) 2016 Philippe Gerum.
 */
#ifndef _ASM_POWERPC_DOVETAIL_H
#define _ASM_POWERPC_DOVETAIL_H

#define IPIPE_TRAP_ACCESS	 0	/* Data or instruction access exception */
#define IPIPE_TRAP_ALIGNMENT	 1	/* Alignment exception */
#define IPIPE_TRAP_ALTUNAVAIL	 2	/* Altivec unavailable */
#define IPIPE_TRAP_PCE		 3	/* Program check exception */
#define IPIPE_TRAP_MCE		 4	/* Machine check exception */
#define IPIPE_TRAP_UNKNOWN	 5	/* Unknown exception */
#define IPIPE_TRAP_IABR		 6	/* Instruction breakpoint */
#define IPIPE_TRAP_RM		 7	/* Run mode exception */
#define IPIPE_TRAP_SSTEP	 8	/* Single-step exception */
#define IPIPE_TRAP_NREC		 9	/* Non-recoverable exception */
#define IPIPE_TRAP_SOFTEMU	10	/* Software emulation */
#define IPIPE_TRAP_DEBUG	11	/* Debug exception */
#define IPIPE_TRAP_SPE		12	/* SPE exception */
#define IPIPE_TRAP_ALTASSIST	13	/* Altivec assist exception */
#define IPIPE_TRAP_CACHE	14	/* Cache-locking exception (FSL) */
#define IPIPE_TRAP_KFPUNAVAIL	15	/* FP unavailable exception */
#define IPIPE_TRAP_MAYDAY	16	/* Internal recovery trap */
#define IPIPE_NR_FAULTS		17

#endif /* _ASM_POWERPC_DOVETAIL_H */
