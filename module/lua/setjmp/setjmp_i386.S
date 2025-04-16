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

#define	ENTRY(x) \
	.text; \
	.balign	8; \
	.globl	x; \
	.type	x, @function; \
x:

#define	SET_SIZE(x) \
	.size	x, [.-x]

/*
 * Setjmp and longjmp implement non-local gotos using state vectors
 * type label_t.
 */
#ifdef __i386__

	ENTRY(setjmp)			/* save area is passed in eax */
	movl	%ebp, 0(%eax)		/* save ebp */
	movl	%ebx, 4(%eax)		/* save ebx */
	movl	%esi, 8(%eax)		/* save esi */
	movl	%edi, 12(%eax)		/* save edi */
	movl	%esp, 16(%eax)		/* save esp */
	movl	(%esp), %ecx		/* %eip (return address) */
	movl	%ecx, 20(%eax)		/* save eip */
	subl	%eax, %eax		/* return 0 */
	ret
	SET_SIZE(setjmp)

	ENTRY(longjmp)			/* save area is passed in eax */
	movl	0(%eax), %ebp		/* restore ebp */
	movl	4(%eax), %ebx		/* restore ebx */
	movl	8(%eax), %esi		/* restore esi */
	movl	12(%eax), %edi		/* restore edi */
	movl	16(%eax), %esp		/* restore esp */
	movl	20(%eax), %ecx		/* %eip (return address) */
	addl	$4, %esp		/* pop ret adr */
	jmp	*%ecx			/* indirect jump */
	SET_SIZE(longjmp)

#ifdef __ELF__
.section .note.GNU-stack,"",%progbits
#endif

#endif /* __i386__ */
