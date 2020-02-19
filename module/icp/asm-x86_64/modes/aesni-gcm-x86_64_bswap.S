# Copyright 2013-2016 The OpenSSL Project Authors. All Rights Reserved.
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
#
# AES-NI-CTR+GHASH stitch.
#
# February 2013
#
# OpenSSL GCM implementation is organized in such way that its
# performance is rather close to the sum of its streamed components,
# in the context parallelized AES-NI CTR and modulo-scheduled
# PCLMULQDQ-enabled GHASH. Unfortunately, as no stitch implementation
# was observed to perform significantly better than the sum of the
# components on contemporary CPUs, the effort was deemed impossible to
# justify. This module is based on combination of Intel submissions,
# [1] and [2], with MOVBE twist suggested by Ilya Albrekht and Max
# Locktyukhin of Intel Corp. who verified that it reduces shuffles
# pressure with notable relative improvement, achieving 1.0 cycle per
# byte processed with 128-bit key on Haswell processor, 0.74 - on
# Broadwell, 0.63 - on Skylake... [Mentioned results are raw profiled
# measurements for favourable packet size, one divisible by 96.
# Applications using the EVP interface will observe a few percent
# worse performance.]
#
# Knights Landing processes 1 byte in 1.25 cycles (measured with EVP).
#
# [1] http://rt.openssl.org/Ticket/Display.html?id=2900&user=guest&pass=guest
# [2] http://www.intel.com/content/dam/www/public/us/en/documents/software-support/enabling-high-performance-gcm.pdf

# Generated once from
# https://github.com/openssl/openssl/blob/5ffc3324/crypto/modes/asm/aesni-gcm-x86_64.pl
# and modified for ICP. Modification are kept at a bare minimum to ease later
# upstream merges.

/* aes-without-movbe (bswap) x86_64 GCM implementation
   and shared global utility functions */

#if defined(__x86_64__) && defined(HAVE_AVX) && \
    defined(HAVE_AES) && defined(HAVE_PCLMULQDQ)

/* Some utility routines */

/*
 * clear all fpu registers
 * void clear_fpu_regs_avx(void);
 */
.globl	clear_fpu_regs_avx
.type	clear_fpu_regs_avx,@function
.align	32
clear_fpu_regs_avx:
	vzeroall
	ret
.size	clear_fpu_regs_avx,.-clear_fpu_regs_avx

/*
 * void gcm_xor_avx(const uint8_t *src, uint8_t *dst);
 *
 * XORs one pair of unaligned 128-bit blocks from `src' and `dst' and
 * stores the result at `dst'. The XOR is performed using FPU registers,
 * so make sure FPU state is saved when running this in the kernel.
 */
.globl  gcm_xor_avx
.type	gcm_xor_avx,@function
.align	32
gcm_xor_avx:
	movdqu  (%rdi), %xmm0
	movdqu  (%rsi), %xmm1
	pxor    %xmm1, %xmm0
	movdqu  %xmm0, (%rsi)
	ret
.size	gcm_xor_avx,.-gcm_xor_avx

/*
 * Toggle a boolean_t value atomically and return the new value.
 * boolean_t atomic_toggle_boolean_nv(volatile boolean_t *);
 */
.globl	atomic_toggle_boolean_nv
.type	atomic_toggle_boolean_nv,@function
.align	32
atomic_toggle_boolean_nv:
	xorl	%eax, %eax
	lock
	xorl	$1, (%rdi)
	jz	1f
	movl	$1, %eax
1:
	ret
.size	atomic_toggle_boolean_nv,.-atomic_toggle_boolean_nv

/* defined in two parts because it NEEDS to expand into two separate lines */
# define MOVQ_AND_BYTESWAP_PART1(SRC,DEST) \
    movq SRC,DEST
# define MOVQ_AND_BYTESWAP_PART2(SRC,DEST) \
    bswapq DEST
# define ZFS_ENTRY_SYMBOL_RENAME(NAME) NAME##_bswap
# include "aesni-gcm-x86_64.inc.S"

#endif /* defined(__x86_64__) ... */
