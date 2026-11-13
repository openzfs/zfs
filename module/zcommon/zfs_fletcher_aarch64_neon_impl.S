// SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
/*
 * Four-stream NEON Fletcher-4 inner loops for aarch64.
 *
 * GCC releases before 12 generate poor code for the vector C version of
 * these loops, so the output of Clang (or GCC 12+) is kept here instead.
 * The C source lives in contrib/zcommon/zfs_fletcher_aarch64_neon_impl.c,
 * which documents how to regenerate this file.
 *
 * Copyright (C) 2022 Kevin Zhao.
 *
 * Authors:
 *	Kevin Zhao <kevin.zhao@linaro.org>
 * Co-authored-by:
 *	Richard Yao <richard.yao@alumni.stonybrook.edu>
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *	- Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *	- Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#if defined(__aarch64__)

/* Mark the object BTI and PAC compatible, as the icp aarch64 code does. */
	.section	.note.gnu.property,"a",@note
	.p2align	3
	.word	4
	.word	16
	.word	5
	.asciz	"GNU"
	.word	3221225472
	.word	4
	.word	3
	.word	0

	.text
	.arch	armv8-a

/*
 * void zfs_fletcher_4_aarch64_neon_native(fletcher_4_ctx_t *ctx,
 *     const void *buf, uint64_t size)
 *
 * size must be a non-zero multiple of 16.  ctx->aarch64_neon[i].v[j]
 * holds the i-th running sum of the words at index j modulo 4.
 */
	.globl	zfs_fletcher_4_aarch64_neon_native
	.hidden	zfs_fletcher_4_aarch64_neon_native
	.p2align	2
	.type	zfs_fletcher_4_aarch64_neon_native,@function
zfs_fletcher_4_aarch64_neon_native:
	.cfi_startproc
	hint	#34
	ldp	q4, q0, [x0]
	add	x8, x1, x2
	ldp	q5, q1, [x0, #32]
	ldp	q6, q2, [x0, #64]
	ldp	q7, q3, [x0, #96]
.Lnative_loop:
	ldr	q16, [x1], #16
	cmp	x1, x8
	uaddw2	v0.2d, v0.2d, v16.4s
	uaddw	v4.2d, v4.2d, v16.2s
	add	v1.2d, v0.2d, v1.2d
	add	v5.2d, v4.2d, v5.2d
	add	v2.2d, v1.2d, v2.2d
	add	v6.2d, v5.2d, v6.2d
	add	v3.2d, v2.2d, v3.2d
	add	v7.2d, v6.2d, v7.2d
	b.lo	.Lnative_loop
	stp	q4, q0, [x0]
	stp	q5, q1, [x0, #32]
	stp	q6, q2, [x0, #64]
	stp	q7, q3, [x0, #96]
	ret
.Lnative_end:
	.size	zfs_fletcher_4_aarch64_neon_native, \
	    .Lnative_end-zfs_fletcher_4_aarch64_neon_native
	.cfi_endproc

/* As above, with each 32-bit word byte-swapped before it is summed. */
	.globl	zfs_fletcher_4_aarch64_neon_byteswap
	.hidden	zfs_fletcher_4_aarch64_neon_byteswap
	.p2align	2
	.type	zfs_fletcher_4_aarch64_neon_byteswap,@function
zfs_fletcher_4_aarch64_neon_byteswap:
	.cfi_startproc
	hint	#34
	ldp	q4, q0, [x0]
	add	x8, x1, x2
	ldp	q5, q1, [x0, #32]
	ldp	q6, q2, [x0, #64]
	ldp	q7, q3, [x0, #96]
.Lbyteswap_loop:
	ldr	q16, [x1], #16
	cmp	x1, x8
	rev32	v16.16b, v16.16b
	uaddw2	v0.2d, v0.2d, v16.4s
	uaddw	v4.2d, v4.2d, v16.2s
	add	v1.2d, v0.2d, v1.2d
	add	v5.2d, v4.2d, v5.2d
	add	v2.2d, v1.2d, v2.2d
	add	v6.2d, v5.2d, v6.2d
	add	v3.2d, v2.2d, v3.2d
	add	v7.2d, v6.2d, v7.2d
	b.lo	.Lbyteswap_loop
	stp	q4, q0, [x0]
	stp	q5, q1, [x0, #32]
	stp	q6, q2, [x0, #64]
	stp	q7, q3, [x0, #96]
	ret
.Lbyteswap_end:
	.size	zfs_fletcher_4_aarch64_neon_byteswap, \
	    .Lbyteswap_end-zfs_fletcher_4_aarch64_neon_byteswap
	.cfi_endproc

	.section	.note.GNU-stack,"",%progbits

#endif /* defined(__aarch64__) */
