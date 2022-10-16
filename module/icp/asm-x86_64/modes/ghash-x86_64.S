# Copyright 2010-2016 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the Apache License 2.0 (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html

#
# ====================================================================
# Written by Andy Polyakov <appro@openssl.org> for the OpenSSL
# project. The module is, however, dual licensed under OpenSSL and
# CRYPTOGAMS licenses depending on where you obtain it. For further
# details see http://www.openssl.org/~appro/cryptogams/.
# ====================================================================
#
# March, June 2010
#
# The module implements "4-bit" GCM GHASH function and underlying
# single multiplication operation in GF(2^128). "4-bit" means that
# it uses 256 bytes per-key table [+128 bytes shared table]. GHASH
# function features so called "528B" variant utilizing additional
# 256+16 bytes of per-key storage [+512 bytes shared table].
# Performance results are for this streamed GHASH subroutine and are
# expressed in cycles per processed byte, less is better:
#
#		gcc 3.4.x(*)	assembler
#
# P4		28.6		14.0		+100%
# Opteron	19.3		7.7		+150%
# Core2		17.8		8.1(**)		+120%
# Atom		31.6		16.8		+88%
# VIA Nano	21.8		10.1		+115%
#
# (*)	comparison is not completely fair, because C results are
#	for vanilla "256B" implementation, while assembler results
#	are for "528B";-)
# (**)	it's mystery [to me] why Core2 result is not same as for
#	Opteron;

# May 2010
#
# Add PCLMULQDQ version performing at 2.02 cycles per processed byte.
# See ghash-x86.pl for background information and details about coding
# techniques.
#
# Special thanks to David Woodhouse for providing access to a
# Westmere-based system on behalf of Intel Open Source Technology Centre.

# December 2012
#
# Overhaul: aggregate Karatsuba post-processing, improve ILP in
# reduction_alg9, increase reduction aggregate factor to 4x. As for
# the latter. ghash-x86.pl discusses that it makes lesser sense to
# increase aggregate factor. Then why increase here? Critical path
# consists of 3 independent pclmulqdq instructions, Karatsuba post-
# processing and reduction. "On top" of this we lay down aggregated
# multiplication operations, triplets of independent pclmulqdq's. As
# issue rate for pclmulqdq is limited, it makes lesser sense to
# aggregate more multiplications than it takes to perform remaining
# non-multiplication operations. 2x is near-optimal coefficient for
# contemporary Intel CPUs (therefore modest improvement coefficient),
# but not for Bulldozer. Latter is because logical SIMD operations
# are twice as slow in comparison to Intel, so that critical path is
# longer. A CPU with higher pclmulqdq issue rate would also benefit
# from higher aggregate factor...
#
# Westmere	1.78(+13%)
# Sandy Bridge	1.80(+8%)
# Ivy Bridge	1.80(+7%)
# Haswell	0.55(+93%) (if system doesn't support AVX)
# Broadwell	0.45(+110%)(if system doesn't support AVX)
# Skylake	0.44(+110%)(if system doesn't support AVX)
# Bulldozer	1.49(+27%)
# Silvermont	2.88(+13%)
# Knights L	2.12(-)    (if system doesn't support AVX)
# Goldmont	1.08(+24%)

# March 2013
#
# ... 8x aggregate factor AVX code path is using reduction algorithm
# suggested by Shay Gueron[1]. Even though contemporary AVX-capable
# CPUs such as Sandy and Ivy Bridge can execute it, the code performs
# sub-optimally in comparison to above mentioned version. But thanks
# to Ilya Albrekht and Max Locktyukhin of Intel Corp. we knew that
# it performs in 0.41 cycles per byte on Haswell processor, in
# 0.29 on Broadwell, and in 0.36 on Skylake.
#
# Knights Landing achieves 1.09 cpb.
#
# [1] http://rt.openssl.org/Ticket/Display.html?id=2900&user=guest&pass=guest

# Generated once from
# https://github.com/openssl/openssl/blob/5ffc3324/crypto/modes/asm/ghash-x86_64.pl
# and modified for ICP. Modification are kept at a bare minimum to ease later
# upstream merges.

#if defined(__x86_64__) && defined(HAVE_AVX) && \
    defined(HAVE_AES) && defined(HAVE_PCLMULQDQ)

#define _ASM
#include <sys/asm_linkage.h>

.text

.globl	gcm_gmult_clmul
.type	gcm_gmult_clmul,@function
.align	16
gcm_gmult_clmul:
.cfi_startproc
	ENDBR
.L_gmult_clmul:
	movdqu	(%rdi),%xmm0
	movdqa	.Lbswap_mask(%rip),%xmm5
	movdqu	(%rsi),%xmm2
	movdqu	32(%rsi),%xmm4
.byte	102,15,56,0,197
	movdqa	%xmm0,%xmm1
	pshufd	$78,%xmm0,%xmm3
	pxor	%xmm0,%xmm3
.byte	102,15,58,68,194,0
.byte	102,15,58,68,202,17
.byte	102,15,58,68,220,0
	pxor	%xmm0,%xmm3
	pxor	%xmm1,%xmm3

	movdqa	%xmm3,%xmm4
	psrldq	$8,%xmm3
	pslldq	$8,%xmm4
	pxor	%xmm3,%xmm1
	pxor	%xmm4,%xmm0

	movdqa	%xmm0,%xmm4
	movdqa	%xmm0,%xmm3
	psllq	$5,%xmm0
	pxor	%xmm0,%xmm3
	psllq	$1,%xmm0
	pxor	%xmm3,%xmm0
	psllq	$57,%xmm0
	movdqa	%xmm0,%xmm3
	pslldq	$8,%xmm0
	psrldq	$8,%xmm3
	pxor	%xmm4,%xmm0
	pxor	%xmm3,%xmm1


	movdqa	%xmm0,%xmm4
	psrlq	$1,%xmm0
	pxor	%xmm4,%xmm1
	pxor	%xmm0,%xmm4
	psrlq	$5,%xmm0
	pxor	%xmm4,%xmm0
	psrlq	$1,%xmm0
	pxor	%xmm1,%xmm0
.byte	102,15,56,0,197
	movdqu	%xmm0,(%rdi)
	RET
.cfi_endproc
.size	gcm_gmult_clmul,.-gcm_gmult_clmul

.globl	gcm_init_htab_avx
.type	gcm_init_htab_avx,@function
.align	32
gcm_init_htab_avx:
.cfi_startproc
	ENDBR
	vzeroupper

	vmovdqu	(%rsi),%xmm2
	// KCF/ICP stores H in network byte order with the hi qword first
	// so we need to swap all bytes, not the 2 qwords.
	vmovdqu	.Lbswap_mask(%rip),%xmm4
	vpshufb	%xmm4,%xmm2,%xmm2


	vpshufd	$255,%xmm2,%xmm4
	vpsrlq	$63,%xmm2,%xmm3
	vpsllq	$1,%xmm2,%xmm2
	vpxor	%xmm5,%xmm5,%xmm5
	vpcmpgtd	%xmm4,%xmm5,%xmm5
	vpslldq	$8,%xmm3,%xmm3
	vpor	%xmm3,%xmm2,%xmm2


	vpand	.L0x1c2_polynomial(%rip),%xmm5,%xmm5
	vpxor	%xmm5,%xmm2,%xmm2

	vpunpckhqdq	%xmm2,%xmm2,%xmm6
	vmovdqa	%xmm2,%xmm0
	vpxor	%xmm2,%xmm6,%xmm6
	movq	$4,%r10
	jmp	.Linit_start_avx
.align	32
.Linit_loop_avx:
	vpalignr	$8,%xmm3,%xmm4,%xmm5
	vmovdqu	%xmm5,-16(%rdi)
	vpunpckhqdq	%xmm0,%xmm0,%xmm3
	vpxor	%xmm0,%xmm3,%xmm3
	vpclmulqdq	$0x11,%xmm2,%xmm0,%xmm1
	vpclmulqdq	$0x00,%xmm2,%xmm0,%xmm0
	vpclmulqdq	$0x00,%xmm6,%xmm3,%xmm3
	vpxor	%xmm0,%xmm1,%xmm4
	vpxor	%xmm4,%xmm3,%xmm3

	vpslldq	$8,%xmm3,%xmm4
	vpsrldq	$8,%xmm3,%xmm3
	vpxor	%xmm4,%xmm0,%xmm0
	vpxor	%xmm3,%xmm1,%xmm1
	vpsllq	$57,%xmm0,%xmm3
	vpsllq	$62,%xmm0,%xmm4
	vpxor	%xmm3,%xmm4,%xmm4
	vpsllq	$63,%xmm0,%xmm3
	vpxor	%xmm3,%xmm4,%xmm4
	vpslldq	$8,%xmm4,%xmm3
	vpsrldq	$8,%xmm4,%xmm4
	vpxor	%xmm3,%xmm0,%xmm0
	vpxor	%xmm4,%xmm1,%xmm1

	vpsrlq	$1,%xmm0,%xmm4
	vpxor	%xmm0,%xmm1,%xmm1
	vpxor	%xmm4,%xmm0,%xmm0
	vpsrlq	$5,%xmm4,%xmm4
	vpxor	%xmm4,%xmm0,%xmm0
	vpsrlq	$1,%xmm0,%xmm0
	vpxor	%xmm1,%xmm0,%xmm0
.Linit_start_avx:
	vmovdqa	%xmm0,%xmm5
	vpunpckhqdq	%xmm0,%xmm0,%xmm3
	vpxor	%xmm0,%xmm3,%xmm3
	vpclmulqdq	$0x11,%xmm2,%xmm0,%xmm1
	vpclmulqdq	$0x00,%xmm2,%xmm0,%xmm0
	vpclmulqdq	$0x00,%xmm6,%xmm3,%xmm3
	vpxor	%xmm0,%xmm1,%xmm4
	vpxor	%xmm4,%xmm3,%xmm3

	vpslldq	$8,%xmm3,%xmm4
	vpsrldq	$8,%xmm3,%xmm3
	vpxor	%xmm4,%xmm0,%xmm0
	vpxor	%xmm3,%xmm1,%xmm1
	vpsllq	$57,%xmm0,%xmm3
	vpsllq	$62,%xmm0,%xmm4
	vpxor	%xmm3,%xmm4,%xmm4
	vpsllq	$63,%xmm0,%xmm3
	vpxor	%xmm3,%xmm4,%xmm4
	vpslldq	$8,%xmm4,%xmm3
	vpsrldq	$8,%xmm4,%xmm4
	vpxor	%xmm3,%xmm0,%xmm0
	vpxor	%xmm4,%xmm1,%xmm1

	vpsrlq	$1,%xmm0,%xmm4
	vpxor	%xmm0,%xmm1,%xmm1
	vpxor	%xmm4,%xmm0,%xmm0
	vpsrlq	$5,%xmm4,%xmm4
	vpxor	%xmm4,%xmm0,%xmm0
	vpsrlq	$1,%xmm0,%xmm0
	vpxor	%xmm1,%xmm0,%xmm0
	vpshufd	$78,%xmm5,%xmm3
	vpshufd	$78,%xmm0,%xmm4
	vpxor	%xmm5,%xmm3,%xmm3
	vmovdqu	%xmm5,0(%rdi)
	vpxor	%xmm0,%xmm4,%xmm4
	vmovdqu	%xmm0,16(%rdi)
	leaq	48(%rdi),%rdi
	subq	$1,%r10
	jnz	.Linit_loop_avx

	vpalignr	$8,%xmm4,%xmm3,%xmm5
	vmovdqu	%xmm5,-16(%rdi)

	vzeroupper
	RET
.cfi_endproc
.size	gcm_init_htab_avx,.-gcm_init_htab_avx

.globl	gcm_gmult_avx
.type	gcm_gmult_avx,@function
.align	32
gcm_gmult_avx:
.cfi_startproc
	ENDBR
	jmp	.L_gmult_clmul
.cfi_endproc
.size	gcm_gmult_avx,.-gcm_gmult_avx
.globl	gcm_ghash_avx
.type	gcm_ghash_avx,@function
.align	32
gcm_ghash_avx:
.cfi_startproc
	ENDBR
	vzeroupper

	vmovdqu	(%rdi),%xmm10
	leaq	.L0x1c2_polynomial(%rip),%r10
	leaq	64(%rsi),%rsi
	vmovdqu	.Lbswap_mask(%rip),%xmm13
	vpshufb	%xmm13,%xmm10,%xmm10
	cmpq	$0x80,%rcx
	jb	.Lshort_avx
	subq	$0x80,%rcx

	vmovdqu	112(%rdx),%xmm14
	vmovdqu	0-64(%rsi),%xmm6
	vpshufb	%xmm13,%xmm14,%xmm14
	vmovdqu	32-64(%rsi),%xmm7

	vpunpckhqdq	%xmm14,%xmm14,%xmm9
	vmovdqu	96(%rdx),%xmm15
	vpclmulqdq	$0x00,%xmm6,%xmm14,%xmm0
	vpxor	%xmm14,%xmm9,%xmm9
	vpshufb	%xmm13,%xmm15,%xmm15
	vpclmulqdq	$0x11,%xmm6,%xmm14,%xmm1
	vmovdqu	16-64(%rsi),%xmm6
	vpunpckhqdq	%xmm15,%xmm15,%xmm8
	vmovdqu	80(%rdx),%xmm14
	vpclmulqdq	$0x00,%xmm7,%xmm9,%xmm2
	vpxor	%xmm15,%xmm8,%xmm8

	vpshufb	%xmm13,%xmm14,%xmm14
	vpclmulqdq	$0x00,%xmm6,%xmm15,%xmm3
	vpunpckhqdq	%xmm14,%xmm14,%xmm9
	vpclmulqdq	$0x11,%xmm6,%xmm15,%xmm4
	vmovdqu	48-64(%rsi),%xmm6
	vpxor	%xmm14,%xmm9,%xmm9
	vmovdqu	64(%rdx),%xmm15
	vpclmulqdq	$0x10,%xmm7,%xmm8,%xmm5
	vmovdqu	80-64(%rsi),%xmm7

	vpshufb	%xmm13,%xmm15,%xmm15
	vpxor	%xmm0,%xmm3,%xmm3
	vpclmulqdq	$0x00,%xmm6,%xmm14,%xmm0
	vpxor	%xmm1,%xmm4,%xmm4
	vpunpckhqdq	%xmm15,%xmm15,%xmm8
	vpclmulqdq	$0x11,%xmm6,%xmm14,%xmm1
	vmovdqu	64-64(%rsi),%xmm6
	vpxor	%xmm2,%xmm5,%xmm5
	vpclmulqdq	$0x00,%xmm7,%xmm9,%xmm2
	vpxor	%xmm15,%xmm8,%xmm8

	vmovdqu	48(%rdx),%xmm14
	vpxor	%xmm3,%xmm0,%xmm0
	vpclmulqdq	$0x00,%xmm6,%xmm15,%xmm3
	vpxor	%xmm4,%xmm1,%xmm1
	vpshufb	%xmm13,%xmm14,%xmm14
	vpclmulqdq	$0x11,%xmm6,%xmm15,%xmm4
	vmovdqu	96-64(%rsi),%xmm6
	vpxor	%xmm5,%xmm2,%xmm2
	vpunpckhqdq	%xmm14,%xmm14,%xmm9
	vpclmulqdq	$0x10,%xmm7,%xmm8,%xmm5
	vmovdqu	128-64(%rsi),%xmm7
	vpxor	%xmm14,%xmm9,%xmm9

	vmovdqu	32(%rdx),%xmm15
	vpxor	%xmm0,%xmm3,%xmm3
	vpclmulqdq	$0x00,%xmm6,%xmm14,%xmm0
	vpxor	%xmm1,%xmm4,%xmm4
	vpshufb	%xmm13,%xmm15,%xmm15
	vpclmulqdq	$0x11,%xmm6,%xmm14,%xmm1
	vmovdqu	112-64(%rsi),%xmm6
	vpxor	%xmm2,%xmm5,%xmm5
	vpunpckhqdq	%xmm15,%xmm15,%xmm8
	vpclmulqdq	$0x00,%xmm7,%xmm9,%xmm2
	vpxor	%xmm15,%xmm8,%xmm8

	vmovdqu	16(%rdx),%xmm14
	vpxor	%xmm3,%xmm0,%xmm0
	vpclmulqdq	$0x00,%xmm6,%xmm15,%xmm3
	vpxor	%xmm4,%xmm1,%xmm1
	vpshufb	%xmm13,%xmm14,%xmm14
	vpclmulqdq	$0x11,%xmm6,%xmm15,%xmm4
	vmovdqu	144-64(%rsi),%xmm6
	vpxor	%xmm5,%xmm2,%xmm2
	vpunpckhqdq	%xmm14,%xmm14,%xmm9
	vpclmulqdq	$0x10,%xmm7,%xmm8,%xmm5
	vmovdqu	176-64(%rsi),%xmm7
	vpxor	%xmm14,%xmm9,%xmm9

	vmovdqu	(%rdx),%xmm15
	vpxor	%xmm0,%xmm3,%xmm3
	vpclmulqdq	$0x00,%xmm6,%xmm14,%xmm0
	vpxor	%xmm1,%xmm4,%xmm4
	vpshufb	%xmm13,%xmm15,%xmm15
	vpclmulqdq	$0x11,%xmm6,%xmm14,%xmm1
	vmovdqu	160-64(%rsi),%xmm6
	vpxor	%xmm2,%xmm5,%xmm5
	vpclmulqdq	$0x10,%xmm7,%xmm9,%xmm2

	leaq	128(%rdx),%rdx
	cmpq	$0x80,%rcx
	jb	.Ltail_avx

	vpxor	%xmm10,%xmm15,%xmm15
	subq	$0x80,%rcx
	jmp	.Loop8x_avx

.align	32
.Loop8x_avx:
	vpunpckhqdq	%xmm15,%xmm15,%xmm8
	vmovdqu	112(%rdx),%xmm14
	vpxor	%xmm0,%xmm3,%xmm3
	vpxor	%xmm15,%xmm8,%xmm8
	vpclmulqdq	$0x00,%xmm6,%xmm15,%xmm10
	vpshufb	%xmm13,%xmm14,%xmm14
	vpxor	%xmm1,%xmm4,%xmm4
	vpclmulqdq	$0x11,%xmm6,%xmm15,%xmm11
	vmovdqu	0-64(%rsi),%xmm6
	vpunpckhqdq	%xmm14,%xmm14,%xmm9
	vpxor	%xmm2,%xmm5,%xmm5
	vpclmulqdq	$0x00,%xmm7,%xmm8,%xmm12
	vmovdqu	32-64(%rsi),%xmm7
	vpxor	%xmm14,%xmm9,%xmm9

	vmovdqu	96(%rdx),%xmm15
	vpclmulqdq	$0x00,%xmm6,%xmm14,%xmm0
	vpxor	%xmm3,%xmm10,%xmm10
	vpshufb	%xmm13,%xmm15,%xmm15
	vpclmulqdq	$0x11,%xmm6,%xmm14,%xmm1
	vxorps	%xmm4,%xmm11,%xmm11
	vmovdqu	16-64(%rsi),%xmm6
	vpunpckhqdq	%xmm15,%xmm15,%xmm8
	vpclmulqdq	$0x00,%xmm7,%xmm9,%xmm2
	vpxor	%xmm5,%xmm12,%xmm12
	vxorps	%xmm15,%xmm8,%xmm8

	vmovdqu	80(%rdx),%xmm14
	vpxor	%xmm10,%xmm12,%xmm12
	vpclmulqdq	$0x00,%xmm6,%xmm15,%xmm3
	vpxor	%xmm11,%xmm12,%xmm12
	vpslldq	$8,%xmm12,%xmm9
	vpxor	%xmm0,%xmm3,%xmm3
	vpclmulqdq	$0x11,%xmm6,%xmm15,%xmm4
	vpsrldq	$8,%xmm12,%xmm12
	vpxor	%xmm9,%xmm10,%xmm10
	vmovdqu	48-64(%rsi),%xmm6
	vpshufb	%xmm13,%xmm14,%xmm14
	vxorps	%xmm12,%xmm11,%xmm11
	vpxor	%xmm1,%xmm4,%xmm4
	vpunpckhqdq	%xmm14,%xmm14,%xmm9
	vpclmulqdq	$0x10,%xmm7,%xmm8,%xmm5
	vmovdqu	80-64(%rsi),%xmm7
	vpxor	%xmm14,%xmm9,%xmm9
	vpxor	%xmm2,%xmm5,%xmm5

	vmovdqu	64(%rdx),%xmm15
	vpalignr	$8,%xmm10,%xmm10,%xmm12
	vpclmulqdq	$0x00,%xmm6,%xmm14,%xmm0
	vpshufb	%xmm13,%xmm15,%xmm15
	vpxor	%xmm3,%xmm0,%xmm0
	vpclmulqdq	$0x11,%xmm6,%xmm14,%xmm1
	vmovdqu	64-64(%rsi),%xmm6
	vpunpckhqdq	%xmm15,%xmm15,%xmm8
	vpxor	%xmm4,%xmm1,%xmm1
	vpclmulqdq	$0x00,%xmm7,%xmm9,%xmm2
	vxorps	%xmm15,%xmm8,%xmm8
	vpxor	%xmm5,%xmm2,%xmm2

	vmovdqu	48(%rdx),%xmm14
	vpclmulqdq	$0x10,(%r10),%xmm10,%xmm10
	vpclmulqdq	$0x00,%xmm6,%xmm15,%xmm3
	vpshufb	%xmm13,%xmm14,%xmm14
	vpxor	%xmm0,%xmm3,%xmm3
	vpclmulqdq	$0x11,%xmm6,%xmm15,%xmm4
	vmovdqu	96-64(%rsi),%xmm6
	vpunpckhqdq	%xmm14,%xmm14,%xmm9
	vpxor	%xmm1,%xmm4,%xmm4
	vpclmulqdq	$0x10,%xmm7,%xmm8,%xmm5
	vmovdqu	128-64(%rsi),%xmm7
	vpxor	%xmm14,%xmm9,%xmm9
	vpxor	%xmm2,%xmm5,%xmm5

	vmovdqu	32(%rdx),%xmm15
	vpclmulqdq	$0x00,%xmm6,%xmm14,%xmm0
	vpshufb	%xmm13,%xmm15,%xmm15
	vpxor	%xmm3,%xmm0,%xmm0
	vpclmulqdq	$0x11,%xmm6,%xmm14,%xmm1
	vmovdqu	112-64(%rsi),%xmm6
	vpunpckhqdq	%xmm15,%xmm15,%xmm8
	vpxor	%xmm4,%xmm1,%xmm1
	vpclmulqdq	$0x00,%xmm7,%xmm9,%xmm2
	vpxor	%xmm15,%xmm8,%xmm8
	vpxor	%xmm5,%xmm2,%xmm2
	vxorps	%xmm12,%xmm10,%xmm10

	vmovdqu	16(%rdx),%xmm14
	vpalignr	$8,%xmm10,%xmm10,%xmm12
	vpclmulqdq	$0x00,%xmm6,%xmm15,%xmm3
	vpshufb	%xmm13,%xmm14,%xmm14
	vpxor	%xmm0,%xmm3,%xmm3
	vpclmulqdq	$0x11,%xmm6,%xmm15,%xmm4
	vmovdqu	144-64(%rsi),%xmm6
	vpclmulqdq	$0x10,(%r10),%xmm10,%xmm10
	vxorps	%xmm11,%xmm12,%xmm12
	vpunpckhqdq	%xmm14,%xmm14,%xmm9
	vpxor	%xmm1,%xmm4,%xmm4
	vpclmulqdq	$0x10,%xmm7,%xmm8,%xmm5
	vmovdqu	176-64(%rsi),%xmm7
	vpxor	%xmm14,%xmm9,%xmm9
	vpxor	%xmm2,%xmm5,%xmm5

	vmovdqu	(%rdx),%xmm15
	vpclmulqdq	$0x00,%xmm6,%xmm14,%xmm0
	vpshufb	%xmm13,%xmm15,%xmm15
	vpclmulqdq	$0x11,%xmm6,%xmm14,%xmm1
	vmovdqu	160-64(%rsi),%xmm6
	vpxor	%xmm12,%xmm15,%xmm15
	vpclmulqdq	$0x10,%xmm7,%xmm9,%xmm2
	vpxor	%xmm10,%xmm15,%xmm15

	leaq	128(%rdx),%rdx
	subq	$0x80,%rcx
	jnc	.Loop8x_avx

	addq	$0x80,%rcx
	jmp	.Ltail_no_xor_avx

.align	32
.Lshort_avx:
	vmovdqu	-16(%rdx,%rcx,1),%xmm14
	leaq	(%rdx,%rcx,1),%rdx
	vmovdqu	0-64(%rsi),%xmm6
	vmovdqu	32-64(%rsi),%xmm7
	vpshufb	%xmm13,%xmm14,%xmm15

	vmovdqa	%xmm0,%xmm3
	vmovdqa	%xmm1,%xmm4
	vmovdqa	%xmm2,%xmm5
	subq	$0x10,%rcx
	jz	.Ltail_avx

	vpunpckhqdq	%xmm15,%xmm15,%xmm8
	vpxor	%xmm0,%xmm3,%xmm3
	vpclmulqdq	$0x00,%xmm6,%xmm15,%xmm0
	vpxor	%xmm15,%xmm8,%xmm8
	vmovdqu	-32(%rdx),%xmm14
	vpxor	%xmm1,%xmm4,%xmm4
	vpclmulqdq	$0x11,%xmm6,%xmm15,%xmm1
	vmovdqu	16-64(%rsi),%xmm6
	vpshufb	%xmm13,%xmm14,%xmm15
	vpxor	%xmm2,%xmm5,%xmm5
	vpclmulqdq	$0x00,%xmm7,%xmm8,%xmm2
	vpsrldq	$8,%xmm7,%xmm7
	subq	$0x10,%rcx
	jz	.Ltail_avx

	vpunpckhqdq	%xmm15,%xmm15,%xmm8
	vpxor	%xmm0,%xmm3,%xmm3
	vpclmulqdq	$0x00,%xmm6,%xmm15,%xmm0
	vpxor	%xmm15,%xmm8,%xmm8
	vmovdqu	-48(%rdx),%xmm14
	vpxor	%xmm1,%xmm4,%xmm4
	vpclmulqdq	$0x11,%xmm6,%xmm15,%xmm1
	vmovdqu	48-64(%rsi),%xmm6
	vpshufb	%xmm13,%xmm14,%xmm15
	vpxor	%xmm2,%xmm5,%xmm5
	vpclmulqdq	$0x00,%xmm7,%xmm8,%xmm2
	vmovdqu	80-64(%rsi),%xmm7
	subq	$0x10,%rcx
	jz	.Ltail_avx

	vpunpckhqdq	%xmm15,%xmm15,%xmm8
	vpxor	%xmm0,%xmm3,%xmm3
	vpclmulqdq	$0x00,%xmm6,%xmm15,%xmm0
	vpxor	%xmm15,%xmm8,%xmm8
	vmovdqu	-64(%rdx),%xmm14
	vpxor	%xmm1,%xmm4,%xmm4
	vpclmulqdq	$0x11,%xmm6,%xmm15,%xmm1
	vmovdqu	64-64(%rsi),%xmm6
	vpshufb	%xmm13,%xmm14,%xmm15
	vpxor	%xmm2,%xmm5,%xmm5
	vpclmulqdq	$0x00,%xmm7,%xmm8,%xmm2
	vpsrldq	$8,%xmm7,%xmm7
	subq	$0x10,%rcx
	jz	.Ltail_avx

	vpunpckhqdq	%xmm15,%xmm15,%xmm8
	vpxor	%xmm0,%xmm3,%xmm3
	vpclmulqdq	$0x00,%xmm6,%xmm15,%xmm0
	vpxor	%xmm15,%xmm8,%xmm8
	vmovdqu	-80(%rdx),%xmm14
	vpxor	%xmm1,%xmm4,%xmm4
	vpclmulqdq	$0x11,%xmm6,%xmm15,%xmm1
	vmovdqu	96-64(%rsi),%xmm6
	vpshufb	%xmm13,%xmm14,%xmm15
	vpxor	%xmm2,%xmm5,%xmm5
	vpclmulqdq	$0x00,%xmm7,%xmm8,%xmm2
	vmovdqu	128-64(%rsi),%xmm7
	subq	$0x10,%rcx
	jz	.Ltail_avx

	vpunpckhqdq	%xmm15,%xmm15,%xmm8
	vpxor	%xmm0,%xmm3,%xmm3
	vpclmulqdq	$0x00,%xmm6,%xmm15,%xmm0
	vpxor	%xmm15,%xmm8,%xmm8
	vmovdqu	-96(%rdx),%xmm14
	vpxor	%xmm1,%xmm4,%xmm4
	vpclmulqdq	$0x11,%xmm6,%xmm15,%xmm1
	vmovdqu	112-64(%rsi),%xmm6
	vpshufb	%xmm13,%xmm14,%xmm15
	vpxor	%xmm2,%xmm5,%xmm5
	vpclmulqdq	$0x00,%xmm7,%xmm8,%xmm2
	vpsrldq	$8,%xmm7,%xmm7
	subq	$0x10,%rcx
	jz	.Ltail_avx

	vpunpckhqdq	%xmm15,%xmm15,%xmm8
	vpxor	%xmm0,%xmm3,%xmm3
	vpclmulqdq	$0x00,%xmm6,%xmm15,%xmm0
	vpxor	%xmm15,%xmm8,%xmm8
	vmovdqu	-112(%rdx),%xmm14
	vpxor	%xmm1,%xmm4,%xmm4
	vpclmulqdq	$0x11,%xmm6,%xmm15,%xmm1
	vmovdqu	144-64(%rsi),%xmm6
	vpshufb	%xmm13,%xmm14,%xmm15
	vpxor	%xmm2,%xmm5,%xmm5
	vpclmulqdq	$0x00,%xmm7,%xmm8,%xmm2
	vmovq	184-64(%rsi),%xmm7
	subq	$0x10,%rcx
	jmp	.Ltail_avx

.align	32
.Ltail_avx:
	vpxor	%xmm10,%xmm15,%xmm15
.Ltail_no_xor_avx:
	vpunpckhqdq	%xmm15,%xmm15,%xmm8
	vpxor	%xmm0,%xmm3,%xmm3
	vpclmulqdq	$0x00,%xmm6,%xmm15,%xmm0
	vpxor	%xmm15,%xmm8,%xmm8
	vpxor	%xmm1,%xmm4,%xmm4
	vpclmulqdq	$0x11,%xmm6,%xmm15,%xmm1
	vpxor	%xmm2,%xmm5,%xmm5
	vpclmulqdq	$0x00,%xmm7,%xmm8,%xmm2

	vmovdqu	(%r10),%xmm12

	vpxor	%xmm0,%xmm3,%xmm10
	vpxor	%xmm1,%xmm4,%xmm11
	vpxor	%xmm2,%xmm5,%xmm5

	vpxor	%xmm10,%xmm5,%xmm5
	vpxor	%xmm11,%xmm5,%xmm5
	vpslldq	$8,%xmm5,%xmm9
	vpsrldq	$8,%xmm5,%xmm5
	vpxor	%xmm9,%xmm10,%xmm10
	vpxor	%xmm5,%xmm11,%xmm11

	vpclmulqdq	$0x10,%xmm12,%xmm10,%xmm9
	vpalignr	$8,%xmm10,%xmm10,%xmm10
	vpxor	%xmm9,%xmm10,%xmm10

	vpclmulqdq	$0x10,%xmm12,%xmm10,%xmm9
	vpalignr	$8,%xmm10,%xmm10,%xmm10
	vpxor	%xmm11,%xmm10,%xmm10
	vpxor	%xmm9,%xmm10,%xmm10

	cmpq	$0,%rcx
	jne	.Lshort_avx

	vpshufb	%xmm13,%xmm10,%xmm10
	vmovdqu	%xmm10,(%rdi)
	vzeroupper
	RET
.cfi_endproc
.size	gcm_ghash_avx,.-gcm_ghash_avx

.pushsection .rodata
.align	64
.Lbswap_mask:
.byte	15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0
.L0x1c2_polynomial:
.byte	1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0xc2
.L7_mask:
.long	7,0,7,0
.L7_mask_poly:
.long	7,0,450,0
.align	64
.type	.Lrem_4bit,@object
.Lrem_4bit:
.long	0,0,0,471859200,0,943718400,0,610271232
.long	0,1887436800,0,1822425088,0,1220542464,0,1423966208
.long	0,3774873600,0,4246732800,0,3644850176,0,3311403008
.long	0,2441084928,0,2376073216,0,2847932416,0,3051356160
.type	.Lrem_8bit,@object
.Lrem_8bit:
.value	0x0000,0x01C2,0x0384,0x0246,0x0708,0x06CA,0x048C,0x054E
.value	0x0E10,0x0FD2,0x0D94,0x0C56,0x0918,0x08DA,0x0A9C,0x0B5E
.value	0x1C20,0x1DE2,0x1FA4,0x1E66,0x1B28,0x1AEA,0x18AC,0x196E
.value	0x1230,0x13F2,0x11B4,0x1076,0x1538,0x14FA,0x16BC,0x177E
.value	0x3840,0x3982,0x3BC4,0x3A06,0x3F48,0x3E8A,0x3CCC,0x3D0E
.value	0x3650,0x3792,0x35D4,0x3416,0x3158,0x309A,0x32DC,0x331E
.value	0x2460,0x25A2,0x27E4,0x2626,0x2368,0x22AA,0x20EC,0x212E
.value	0x2A70,0x2BB2,0x29F4,0x2836,0x2D78,0x2CBA,0x2EFC,0x2F3E
.value	0x7080,0x7142,0x7304,0x72C6,0x7788,0x764A,0x740C,0x75CE
.value	0x7E90,0x7F52,0x7D14,0x7CD6,0x7998,0x785A,0x7A1C,0x7BDE
.value	0x6CA0,0x6D62,0x6F24,0x6EE6,0x6BA8,0x6A6A,0x682C,0x69EE
.value	0x62B0,0x6372,0x6134,0x60F6,0x65B8,0x647A,0x663C,0x67FE
.value	0x48C0,0x4902,0x4B44,0x4A86,0x4FC8,0x4E0A,0x4C4C,0x4D8E
.value	0x46D0,0x4712,0x4554,0x4496,0x41D8,0x401A,0x425C,0x439E
.value	0x54E0,0x5522,0x5764,0x56A6,0x53E8,0x522A,0x506C,0x51AE
.value	0x5AF0,0x5B32,0x5974,0x58B6,0x5DF8,0x5C3A,0x5E7C,0x5FBE
.value	0xE100,0xE0C2,0xE284,0xE346,0xE608,0xE7CA,0xE58C,0xE44E
.value	0xEF10,0xEED2,0xEC94,0xED56,0xE818,0xE9DA,0xEB9C,0xEA5E
.value	0xFD20,0xFCE2,0xFEA4,0xFF66,0xFA28,0xFBEA,0xF9AC,0xF86E
.value	0xF330,0xF2F2,0xF0B4,0xF176,0xF438,0xF5FA,0xF7BC,0xF67E
.value	0xD940,0xD882,0xDAC4,0xDB06,0xDE48,0xDF8A,0xDDCC,0xDC0E
.value	0xD750,0xD692,0xD4D4,0xD516,0xD058,0xD19A,0xD3DC,0xD21E
.value	0xC560,0xC4A2,0xC6E4,0xC726,0xC268,0xC3AA,0xC1EC,0xC02E
.value	0xCB70,0xCAB2,0xC8F4,0xC936,0xCC78,0xCDBA,0xCFFC,0xCE3E
.value	0x9180,0x9042,0x9204,0x93C6,0x9688,0x974A,0x950C,0x94CE
.value	0x9F90,0x9E52,0x9C14,0x9DD6,0x9898,0x995A,0x9B1C,0x9ADE
.value	0x8DA0,0x8C62,0x8E24,0x8FE6,0x8AA8,0x8B6A,0x892C,0x88EE
.value	0x83B0,0x8272,0x8034,0x81F6,0x84B8,0x857A,0x873C,0x86FE
.value	0xA9C0,0xA802,0xAA44,0xAB86,0xAEC8,0xAF0A,0xAD4C,0xAC8E
.value	0xA7D0,0xA612,0xA454,0xA596,0xA0D8,0xA11A,0xA35C,0xA29E
.value	0xB5E0,0xB422,0xB664,0xB7A6,0xB2E8,0xB32A,0xB16C,0xB0AE
.value	0xBBF0,0xBA32,0xB874,0xB9B6,0xBCF8,0xBD3A,0xBF7C,0xBEBE

.byte	71,72,65,83,72,32,102,111,114,32,120,56,54,95,54,52,44,32,67,82,89,80,84,79,71,65,77,83,32,98,121,32,60,97,112,112,114,111,64,111,112,101,110,115,115,108,46,111,114,103,62,0
.align	64
.popsection

/* Mark the stack non-executable. */
#if defined(__linux__) && defined(__ELF__)
.section .note.GNU-stack,"",%progbits
#endif

#endif /* defined(__x86_64__) && defined(HAVE_AVX) && defined(HAVE_AES) ... */
