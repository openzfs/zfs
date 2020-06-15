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
 * Copyright (c) 2009 Intel Corporation
 * All Rights Reserved.
 */
/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Accelerated GHASH implementation with Intel PCLMULQDQ-NI
 * instructions.  This file contains an accelerated
 * Galois Field Multiplication implementation.
 *
 * PCLMULQDQ is used to accelerate the most time-consuming part of GHASH,
 * carry-less multiplication. More information about PCLMULQDQ can be
 * found at:
 * http://software.intel.com/en-us/articles/
 * carry-less-multiplication-and-its-usage-for-computing-the-gcm-mode/
 *
 */

/*
 * ====================================================================
 * OpenSolaris OS modifications
 *
 * This source originates as file galois_hash_asm.c from
 * Intel Corporation dated September 21, 2009.
 *
 * This OpenSolaris version has these major changes from the original source:
 *
 * 1. Added OpenSolaris ENTRY_NP/SET_SIZE macros from
 * /usr/include/sys/asm_linkage.h, lint(1B) guards, and a dummy C function
 * definition for lint.
 *
 * 2. Formatted code, added comments, and added #includes and #defines.
 *
 * 3. If bit CR0.TS is set, clear and set the TS bit, after and before
 * calling kpreempt_disable() and kpreempt_enable().
 * If the TS bit is not set, Save and restore %xmm registers at the beginning
 * and end of function calls (%xmm* registers are not saved and restored by
 * during kernel thread preemption).
 *
 * 4. Removed code to perform hashing.  This is already done with C macro
 * GHASH in gcm.c.  For better performance, this removed code should be
 * reintegrated in the future to replace the C GHASH macro.
 *
 * 5. Added code to byte swap 16-byte input and output.
 *
 * 6. Folded in comments from the original C source with embedded assembly
 * (SB_w_shift_xor.c)
 *
 * 7. Renamed function and reordered parameters to match OpenSolaris:
 * Intel interface:
 *	void galois_hash_asm(unsigned char *hk, unsigned char *s,
 *		unsigned char *d, int length)
 * OpenSolaris OS interface:
 *	void gcm_mul_pclmulqdq(uint64_t *x_in, uint64_t *y, uint64_t *res);
 * ====================================================================
 */


#if defined(lint) || defined(__lint)

#include <sys/types.h>

/* ARGSUSED */
void
gcm_mul_pclmulqdq(uint64_t *x_in, uint64_t *y, uint64_t *res) {
}

#else	/* lint */

#define _ASM
#include <sys/asm_linkage.h>

#if defined(_KERNEL) && !defined(__APPLE__)
	/*
	 * Note: the CLTS macro clobbers P2 (%rsi) under i86xpv.  That is,
	 * it calls HYPERVISOR_fpu_taskswitch() which modifies %rsi when it
	 * uses it to pass P2 to syscall.
	 * This also occurs with the STTS macro, but we dont care if
	 * P2 (%rsi) is modified just before function exit.
	 * The CLTS and STTS macros push and pop P1 (%rdi) already.
	 */
#ifdef __xpv
#define	PROTECTED_CLTS \
	push	%rsi; \
	CLTS; \
	pop	%rsi
#else
#define	PROTECTED_CLTS \
	CLTS
#endif	/* __xpv */

	/*
	 * If CR0_TS is not set, align stack (with push %rbp) and push
	 * %xmm0 - %xmm10 on stack, otherwise clear CR0_TS
	 */
#define	CLEAR_TS_OR_PUSH_XMM_REGISTERS(tmpreg) \
	push	%rbp; \
	mov	%rsp, %rbp; \
	movq	%cr0, tmpreg; \
	testq	$CR0_TS, tmpreg; \
	jnz	1f; \
	and	$-XMM_ALIGN, %rsp; \
	sub	$(XMM_SIZE * 11), %rsp; \
	movaps	%xmm0, 160(%rsp); \
	movaps	%xmm1, 144(%rsp); \
	movaps	%xmm2, 128(%rsp); \
	movaps	%xmm3, 112(%rsp); \
	movaps	%xmm4, 96(%rsp); \
	movaps	%xmm5, 80(%rsp); \
	movaps	%xmm6, 64(%rsp); \
	movaps	%xmm7, 48(%rsp); \
	movaps	%xmm8, 32(%rsp); \
	movaps	%xmm9, 16(%rsp); \
	movaps	%xmm10, (%rsp); \
	jmp	2f; \
1: \
	PROTECTED_CLTS; \
2:


	/*
	 * If CR0_TS was not set above, pop %xmm0 - %xmm10 off stack,
	 * otherwise set CR0_TS.
	 */
#define	SET_TS_OR_POP_XMM_REGISTERS(tmpreg) \
	testq	$CR0_TS, tmpreg; \
	jnz	1f; \
	movaps	(%rsp), %xmm10; \
	movaps	16(%rsp), %xmm9; \
	movaps	32(%rsp), %xmm8; \
	movaps	48(%rsp), %xmm7; \
	movaps	64(%rsp), %xmm6; \
	movaps	80(%rsp), %xmm5; \
	movaps	96(%rsp), %xmm4; \
	movaps	112(%rsp), %xmm3; \
	movaps	128(%rsp), %xmm2; \
	movaps	144(%rsp), %xmm1; \
	movaps	160(%rsp), %xmm0; \
	jmp	2f; \
1: \
	STTS(tmpreg); \
2: \
	mov	%rbp, %rsp; \
	pop	%rbp


#else
#define	PROTECTED_CLTS
#define	CLEAR_TS_OR_PUSH_XMM_REGISTERS(tmpreg)
#define	SET_TS_OR_POP_XMM_REGISTERS(tmpreg)
#endif	/* _KERNEL */

/*
 * Use this mask to byte-swap a 16-byte integer with the pshufb instruction
 */

// static uint8_t byte_swap16_mask[] = {
//	 15, 14, 13, 12, 11, 10, 9, 8, 7, 6 ,5, 4, 3, 2, 1, 0 };
.text
.align XMM_ALIGN_LOG
.Lbyte_swap16_mask:
	.byte	15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0



/*
 * void gcm_mul_pclmulqdq(uint64_t *x_in, uint64_t *y, uint64_t *res);
 *
 * Perform a carry-less multiplication (that is, use XOR instead of the
 * multiply operator) on P1 and P2 and place the result in P3.
 *
 * Byte swap the input and the output.
 *
 * Note: x_in, y, and res all point to a block of 20-byte numbers
 * (an array of two 64-bit integers).
 *
 * Note2: For kernel code, caller is responsible for ensuring
 * kpreempt_disable() has been called.  This is because %xmm registers are
 * not saved/restored.  Clear and set the CR0.TS bit on entry and exit,
 * respectively, if TS is set on entry.  Otherwise, if TS is not set,
 * save and restore %xmm registers on the stack.
 *
 * Note3: Original Intel definition:
 * void galois_hash_asm(unsigned char *hk, unsigned char *s,
 *	unsigned char *d, int length)
 *
 * Note4: Register/parameter mapping:
 * Intel:
 *	Parameter 1: %rcx (copied to %xmm0)	hk or x_in
 *	Parameter 2: %rdx (copied to %xmm1)	s or y
 *	Parameter 3: %rdi (result)		d or res
 * OpenSolaris:
 *	Parameter 1: %rdi (copied to %xmm0)	x_in
 *	Parameter 2: %rsi (copied to %xmm1)	y
 *	Parameter 3: %rdx (result)		res
 */

ENTRY_NP(gcm_mul_pclmulqdq)
	CLEAR_TS_OR_PUSH_XMM_REGISTERS(%r10)

	//
	// Copy Parameters
	//
	movdqu	(%rdi), %xmm0	// P1
	movdqu	(%rsi), %xmm1	// P2

	//
	// Byte swap 16-byte input
	//
	lea	.Lbyte_swap16_mask(%rip), %rax
	movups	(%rax), %xmm10
	pshufb	%xmm10, %xmm0
	pshufb	%xmm10, %xmm1


	//
	// Multiply with the hash key
	//
	movdqu	%xmm0, %xmm3
	pclmulqdq $0, %xmm1, %xmm3	// xmm3 holds a0*b0

	movdqu	%xmm0, %xmm4
	pclmulqdq $16, %xmm1, %xmm4	// xmm4 holds a0*b1

	movdqu	%xmm0, %xmm5
	pclmulqdq $1, %xmm1, %xmm5	// xmm5 holds a1*b0
	movdqu	%xmm0, %xmm6
	pclmulqdq $17, %xmm1, %xmm6	// xmm6 holds a1*b1

	pxor	%xmm5, %xmm4	// xmm4 holds a0*b1 + a1*b0

	movdqu	%xmm4, %xmm5	// move the contents of xmm4 to xmm5
	psrldq	$8, %xmm4	// shift by xmm4 64 bits to the right
	pslldq	$8, %xmm5	// shift by xmm5 64 bits to the left
	pxor	%xmm5, %xmm3
	pxor	%xmm4, %xmm6	// Register pair <xmm6:xmm3> holds the result
				// of the carry-less multiplication of
				// xmm0 by xmm1.

	// We shift the result of the multiplication by one bit position
	// to the left to cope for the fact that the bits are reversed.
	movdqu	%xmm3, %xmm7
	movdqu	%xmm6, %xmm8
	pslld	$1, %xmm3
	pslld	$1, %xmm6
	psrld	$31, %xmm7
	psrld	$31, %xmm8
	movdqu	%xmm7, %xmm9
	pslldq	$4, %xmm8
	pslldq	$4, %xmm7
	psrldq	$12, %xmm9
	por	%xmm7, %xmm3
	por	%xmm8, %xmm6
	por	%xmm9, %xmm6

	//
	// First phase of the reduction
	//
	// Move xmm3 into xmm7, xmm8, xmm9 in order to perform the shifts
	// independently.
	movdqu	%xmm3, %xmm7
	movdqu	%xmm3, %xmm8
	movdqu	%xmm3, %xmm9
	pslld	$31, %xmm7	// packed right shift shifting << 31
	pslld	$30, %xmm8	// packed right shift shifting << 30
	pslld	$25, %xmm9	// packed right shift shifting << 25
	pxor	%xmm8, %xmm7	// xor the shifted versions
	pxor	%xmm9, %xmm7
	movdqu	%xmm7, %xmm8
	pslldq	$12, %xmm7
	psrldq	$4, %xmm8
	pxor	%xmm7, %xmm3	// first phase of the reduction complete

	//
	// Second phase of the reduction
	//
	// Make 3 copies of xmm3 in xmm2, xmm4, xmm5 for doing these
	// shift operations.
	movdqu	%xmm3, %xmm2
	movdqu	%xmm3, %xmm4	// packed left shifting >> 1
	movdqu	%xmm3, %xmm5
	psrld	$1, %xmm2
	psrld	$2, %xmm4	// packed left shifting >> 2
	psrld	$7, %xmm5	// packed left shifting >> 7
	pxor	%xmm4, %xmm2	// xor the shifted versions
	pxor	%xmm5, %xmm2
	pxor	%xmm8, %xmm2
	pxor	%xmm2, %xmm3
	pxor	%xmm3, %xmm6	// the result is in xmm6

	//
	// Byte swap 16-byte result
	//
	pshufb	%xmm10, %xmm6	// %xmm10 has the swap mask

	//
	// Store the result
	//
	movdqu	%xmm6, (%rdx)	// P3


	//
	// Cleanup and Return
	//
	SET_TS_OR_POP_XMM_REGISTERS(%r10)
	ret
	SET_SIZE(gcm_mul_pclmulqdq)

#endif	/* lint || __lint */
