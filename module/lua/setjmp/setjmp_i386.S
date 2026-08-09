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
