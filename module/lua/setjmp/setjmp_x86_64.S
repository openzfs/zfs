// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
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
