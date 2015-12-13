/*
 * arch/x86/include/asm/dovetail.h
 *
 * Copyright (C) 2016 Philippe Gerum.
 */
#ifndef _ASM_X86_DOVETAIL_H
#define _ASM_X86_DOVETAIL_H

#ifdef CONFIG_X86_32

/* 32 from IDT + iret_error + mayday trap */
#define IPIPE_TRAP_MAYDAY	33	/* Internal recovery trap */
#define IPIPE_NR_FAULTS		34

#else   /* CONFIG_X86_64 */

/* 32 from IDT + mayday trap */
#define IPIPE_TRAP_MAYDAY	32	/* Internal recovery trap */
#define IPIPE_NR_FAULTS		33

#endif

#endif /* _ASM_X86_DOVETAIL_H */
