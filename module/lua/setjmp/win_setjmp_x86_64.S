/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
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
 * Jorgen Lundman <lundman@lundman.net>
 */

 // Windows x64: 
 // Calling: rcx, rdx, r8, and r9 (float: xmm0-xmm3)
 // Return: rax (float: xmm0)
 // Volatile: rax, rcx, rdx, r8-r11
 // Nonvolatile: rbx, rbp, rsp, rdi, rsi, r12-r15

 // Unix x64:
 // Calling: rdi, rsi, rdx, rcx, r8, r9 (float: xmm0-xmm7)
 // Return: rax (float: xmm0)
 // Volatile:
 // Nonvolatile: rbx, rbp, rsp, r12-r15

 // outcome:
 // rdi -> rcx
 // save rdi, rsi.

 #define	ENTRY(x) \
	.text; \
	.align	8; \
	.globl	x; \
	.def x; .scl 2; .type 32; .endef ; \
x:

#define	SET_SIZE(x) 

/*
 * Setjmp and longjmp implement non-local gotos using state vectors
 * type label_t.
 */
#ifdef __x86_64__

	ENTRY(setjmp)
	nop
	movq	%rsp, 0(%rcx)
	movq	%rbp, 8(%rcx)
	movq	%rbx, 16(%rcx)
	movq	%r12, 24(%rcx)
	movq	%r13, 32(%rcx)
	movq	%r14, 40(%rcx)
	movq	%r15, 48(%rcx)
	movq	%rdi, 56(%rcx)
	movq	%rsi, 64(%rcx)
	movq	0(%rsp), %rdx		/* return address */
	movq	%rdx, 72(%rcx)		/* rip */
	xorl	%eax, %eax		/* return 0 */
	ret
	SET_SIZE(setjmp)

	ENTRY(longjmp)
	movq	0(%rdi), %rsp
	movq	8(%rdi), %rbp
	movq	16(%rdi), %rbx
	movq	24(%rdi), %r12
	movq	32(%rdi), %r13
	movq	40(%rdi), %r14
	movq	48(%rdi), %r15
	movq	56(%rdi), %rdi
	movq	64(%rdi), %rsi
	movq	72(%rdi), %rdx		/* return address */
	movq	%rdx, 0(%rsp)
	xorl	%eax, %eax
	incl	%eax			/* return 1 */
	ret
	SET_SIZE(longjmp)

#ifdef __ELF__
.section .note.GNU-stack,"",%progbits
#endif

#endif /* __x86_64__ */
