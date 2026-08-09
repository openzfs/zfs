// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */

/*
 * Copyright (c) 1992, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#if defined(_KERNEL) && defined(__linux__)
#include <linux/linkage.h>
#endif

/*
 * Setjmp and longjmp implement non-local gotos using state vectors
 * type label_t.
 */
#ifdef __x86_64__

#define _ASM
#include <sys/asm_linkage.h>

ENTRY_ALIGN(setjmp, 8)
	movq	%rsp, 0(%rdi)
	movq	%rbp, 8(%rdi)
	movq	%rbx, 16(%rdi)
	movq	%r12, 24(%rdi)
	movq	%r13, 32(%rdi)
	movq	%r14, 40(%rdi)
	movq	%r15, 48(%rdi)
	movq	0(%rsp), %rdx		/* return address */
	movq	%rdx, 56(%rdi)		/* rip */
	xorl	%eax, %eax		/* return 0 */
	RET
	SET_SIZE(setjmp)

ENTRY_ALIGN(longjmp, 8)
	movq	0(%rdi), %rsp
	movq	8(%rdi), %rbp
	movq	16(%rdi), %rbx
	movq	24(%rdi), %r12
	movq	32(%rdi), %r13
	movq	40(%rdi), %r14
	movq	48(%rdi), %r15
	movq	56(%rdi), %rdx		/* return address */
	movq	%rdx, 0(%rsp)
	xorl	%eax, %eax
	incl	%eax			/* return 1 */
	RET
	SET_SIZE(longjmp)

#ifdef __ELF__
.section .note.GNU-stack,"",%progbits
#endif

#endif /* __x86_64__ */
