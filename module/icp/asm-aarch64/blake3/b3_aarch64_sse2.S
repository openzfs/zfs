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
 * Based on BLAKE3 v1.3.1, https://github.com/BLAKE3-team/BLAKE3
 * Copyright (c) 2019-2022 Samuel Neves and Matthew Krupcale
 * Copyright (c) 2022-2023 Tino Reichardt <milky-zfs@mcmilk.de>
 *
 * This is converted assembly: SSE2 -> ARMv8-A
 * Used tools: SIMDe https://github.com/simd-everywhere/simde
 *
 * Should work on FreeBSD, Linux and macOS
 * see: https://github.com/mcmilk/BLAKE3-tests/blob/master/contrib/simde.sh
 */

#if defined(__aarch64__)

/* make gcc <= 9 happy */
#if !defined(LD_VERSION) || LD_VERSION >= 233010000
#define CFI_NEGATE_RA_STATE .cfi_negate_ra_state
#else
#define CFI_NEGATE_RA_STATE
#endif

	.text
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
.Lsec_end0:
	.text
	.globl	zfs_blake3_compress_in_place_sse2
	.p2align	2
	.type	zfs_blake3_compress_in_place_sse2,@function
zfs_blake3_compress_in_place_sse2:
	.cfi_startproc
	hint	#25
	CFI_NEGATE_RA_STATE
	sub	sp, sp, #96
	stp	x29, x30, [sp, #64]
	add	x29, sp, #64
	str	x19, [sp, #80]
	.cfi_def_cfa w29, 32
	.cfi_offset w19, -16
	.cfi_offset w30, -24
	.cfi_offset w29, -32
	mov	x19, x0
	mov	w5, w4
	mov	x4, x3
	mov	w3, w2
	mov	x2, x1
	mov	x0, sp
	mov	x1, x19
	bl	compress_pre
	ldp	q0, q1, [sp]
	ldp	q2, q3, [sp, #32]
	eor	v0.16b, v2.16b, v0.16b
	eor	v1.16b, v3.16b, v1.16b
	ldp	x29, x30, [sp, #64]
	stp	q0, q1, [x19]
	ldr	x19, [sp, #80]
	add	sp, sp, #96
	hint	#29
	ret
.Lfunc_end0:
	.size	zfs_blake3_compress_in_place_sse2, .Lfunc_end0-zfs_blake3_compress_in_place_sse2
	.cfi_endproc

	.section	.rodata.cst16,"aM",@progbits,16
	.p2align	4
.LCPI1_0:
	.xword	-4942790177982912921
	.xword	-6534734903820487822
	.text
	.p2align	2
	.type	compress_pre,@function
compress_pre:
	.cfi_startproc
	hint	#34
	fmov	s1, w3
	movi	d0, #0x0000ff000000ff
	ldr	q2, [x1]
	fmov	d3, x4
	adrp	x8, .LCPI1_0
	mov	v1.s[1], w5
	str	q2, [x0]
	ldr	q4, [x8, :lo12:.LCPI1_0]
	add	x8, x2, #32
	ldr	q5, [x1, #16]
	and	v0.8b, v1.8b, v0.8b
	stp	q5, q4, [x0, #16]
	mov	v3.d[1], v0.d[0]
	str	q3, [x0, #48]
	ldp	q0, q6, [x2]
	uzp1	v1.4s, v0.4s, v6.4s
	uzp2	v0.4s, v0.4s, v6.4s
	add	v2.4s, v2.4s, v1.4s
	uzp1	v18.4s, v1.4s, v1.4s
	add	v2.4s, v2.4s, v5.4s
	eor	v3.16b, v2.16b, v3.16b
	add	v2.4s, v2.4s, v0.4s
	rev32	v3.8h, v3.8h
	add	v4.4s, v3.4s, v4.4s
	eor	v5.16b, v4.16b, v5.16b
	ushr	v6.4s, v5.4s, #12
	shl	v5.4s, v5.4s, #20
	orr	v5.16b, v5.16b, v6.16b
	add	v2.4s, v2.4s, v5.4s
	eor	v3.16b, v2.16b, v3.16b
	ushr	v6.4s, v3.4s, #8
	shl	v3.4s, v3.4s, #24
	orr	v3.16b, v3.16b, v6.16b
	ld2	{ v6.4s, v7.4s }, [x8]
	add	v4.4s, v3.4s, v4.4s
	ext	v3.16b, v3.16b, v3.16b, #8
	add	v2.4s, v2.4s, v6.4s
	eor	v5.16b, v4.16b, v5.16b
	ext	v4.16b, v4.16b, v4.16b, #4
	ext	v6.16b, v6.16b, v6.16b, #12
	ext	v2.16b, v2.16b, v2.16b, #12
	ushr	v16.4s, v5.4s, #7
	shl	v5.4s, v5.4s, #25
	orr	v5.16b, v5.16b, v16.16b
	ext	v16.16b, v7.16b, v7.16b, #12
	add	v2.4s, v2.4s, v5.4s
	mov	v7.16b, v16.16b
	eor	v3.16b, v3.16b, v2.16b
	add	v2.4s, v2.4s, v16.4s
	mov	v7.s[1], v6.s[2]
	rev32	v3.8h, v3.8h
	add	v4.4s, v4.4s, v3.4s
	eor	v5.16b, v4.16b, v5.16b
	ushr	v17.4s, v5.4s, #12
	shl	v5.4s, v5.4s, #20
	orr	v5.16b, v5.16b, v17.16b
	add	v2.4s, v2.4s, v5.4s
	eor	v3.16b, v2.16b, v3.16b
	ushr	v17.4s, v3.4s, #8
	shl	v3.4s, v3.4s, #24
	orr	v3.16b, v3.16b, v17.16b
	ext	v17.16b, v18.16b, v1.16b, #8
	add	v4.4s, v3.4s, v4.4s
	uzp2	v17.4s, v17.4s, v0.4s
	ext	v3.16b, v3.16b, v3.16b, #8
	eor	v5.16b, v4.16b, v5.16b
	add	v2.4s, v2.4s, v17.4s
	ext	v4.16b, v4.16b, v4.16b, #12
	ushr	v18.4s, v5.4s, #7
	shl	v5.4s, v5.4s, #25
	ext	v2.16b, v2.16b, v2.16b, #4
	orr	v5.16b, v5.16b, v18.16b
	ext	v18.16b, v1.16b, v1.16b, #12
	add	v2.4s, v2.4s, v5.4s
	ext	v1.16b, v1.16b, v18.16b, #12
	zip1	v18.2d, v16.2d, v0.2d
	zip2	v0.4s, v0.4s, v16.4s
	eor	v3.16b, v3.16b, v2.16b
	rev64	v1.4s, v1.4s
	mov	v18.s[3], v6.s[3]
	zip1	v16.4s, v0.4s, v6.4s
	rev32	v3.8h, v3.8h
	trn2	v1.4s, v1.4s, v7.4s
	zip1	v0.4s, v6.4s, v0.4s
	add	v4.4s, v4.4s, v3.4s
	add	v2.4s, v2.4s, v1.4s
	ext	v6.16b, v0.16b, v16.16b, #8
	eor	v5.16b, v4.16b, v5.16b
	ushr	v7.4s, v5.4s, #12
	shl	v5.4s, v5.4s, #20
	orr	v5.16b, v5.16b, v7.16b
	add	v7.4s, v2.4s, v5.4s
	eor	v2.16b, v7.16b, v3.16b
	ext	v7.16b, v7.16b, v7.16b, #12
	ushr	v3.4s, v2.4s, #8
	shl	v2.4s, v2.4s, #24
	orr	v3.16b, v2.16b, v3.16b
	ext	v2.16b, v18.16b, v18.16b, #12
	add	v4.4s, v3.4s, v4.4s
	uzp1	v2.4s, v18.4s, v2.4s
	ext	v3.16b, v3.16b, v3.16b, #8
	eor	v5.16b, v4.16b, v5.16b
	add	v7.4s, v7.4s, v2.4s
	ext	v4.16b, v4.16b, v4.16b, #4
	ushr	v18.4s, v5.4s, #7
	shl	v5.4s, v5.4s, #25
	orr	v5.16b, v5.16b, v18.16b
	add	v7.4s, v7.4s, v5.4s
	eor	v3.16b, v3.16b, v7.16b
	add	v7.4s, v7.4s, v6.4s
	rev32	v3.8h, v3.8h
	add	v4.4s, v4.4s, v3.4s
	eor	v5.16b, v4.16b, v5.16b
	ushr	v0.4s, v5.4s, #12
	shl	v5.4s, v5.4s, #20
	orr	v0.16b, v5.16b, v0.16b
	add	v5.4s, v7.4s, v0.4s
	ext	v7.16b, v17.16b, v17.16b, #4
	eor	v3.16b, v5.16b, v3.16b
	uzp1	v17.4s, v7.4s, v7.4s
	ushr	v16.4s, v3.4s, #8
	shl	v3.4s, v3.4s, #24
	orr	v3.16b, v3.16b, v16.16b
	ext	v16.16b, v17.16b, v7.16b, #8
	add	v4.4s, v3.4s, v4.4s
	uzp2	v16.4s, v16.4s, v1.4s
	ext	v3.16b, v3.16b, v3.16b, #8
	eor	v0.16b, v4.16b, v0.16b
	add	v5.4s, v5.4s, v16.4s
	ext	v4.16b, v4.16b, v4.16b, #12
	ushr	v17.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	ext	v5.16b, v5.16b, v5.16b, #4
	orr	v0.16b, v0.16b, v17.16b
	ext	v17.16b, v7.16b, v7.16b, #12
	add	v5.4s, v5.4s, v0.4s
	ext	v7.16b, v7.16b, v17.16b, #12
	mov	v17.16b, v6.16b
	eor	v3.16b, v3.16b, v5.16b
	rev64	v7.4s, v7.4s
	mov	v17.s[1], v2.s[2]
	rev32	v3.8h, v3.8h
	add	v4.4s, v4.4s, v3.4s
	eor	v18.16b, v4.16b, v0.16b
	trn2	v0.4s, v7.4s, v17.4s
	ushr	v7.4s, v18.4s, #12
	shl	v17.4s, v18.4s, #20
	add	v5.4s, v5.4s, v0.4s
	zip1	v18.2d, v6.2d, v1.2d
	zip2	v1.4s, v1.4s, v6.4s
	orr	v7.16b, v17.16b, v7.16b
	mov	v18.s[3], v2.s[3]
	zip1	v6.4s, v1.4s, v2.4s
	add	v5.4s, v5.4s, v7.4s
	zip1	v1.4s, v2.4s, v1.4s
	eor	v3.16b, v5.16b, v3.16b
	ext	v5.16b, v5.16b, v5.16b, #12
	ext	v6.16b, v1.16b, v6.16b, #8
	ushr	v17.4s, v3.4s, #8
	shl	v3.4s, v3.4s, #24
	orr	v17.16b, v3.16b, v17.16b
	ext	v3.16b, v18.16b, v18.16b, #12
	add	v4.4s, v17.4s, v4.4s
	uzp1	v3.4s, v18.4s, v3.4s
	ext	v17.16b, v17.16b, v17.16b, #8
	eor	v7.16b, v4.16b, v7.16b
	add	v5.4s, v5.4s, v3.4s
	ext	v4.16b, v4.16b, v4.16b, #4
	ushr	v18.4s, v7.4s, #7
	shl	v7.4s, v7.4s, #25
	orr	v7.16b, v7.16b, v18.16b
	add	v5.4s, v5.4s, v7.4s
	eor	v17.16b, v17.16b, v5.16b
	add	v5.4s, v5.4s, v6.4s
	rev32	v17.8h, v17.8h
	add	v4.4s, v4.4s, v17.4s
	eor	v2.16b, v4.16b, v7.16b
	ext	v7.16b, v16.16b, v16.16b, #4
	ushr	v1.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	orr	v1.16b, v2.16b, v1.16b
	add	v2.4s, v5.4s, v1.4s
	eor	v5.16b, v2.16b, v17.16b
	uzp1	v17.4s, v7.4s, v7.4s
	ushr	v16.4s, v5.4s, #8
	shl	v5.4s, v5.4s, #24
	orr	v5.16b, v5.16b, v16.16b
	ext	v16.16b, v17.16b, v7.16b, #8
	add	v4.4s, v5.4s, v4.4s
	uzp2	v16.4s, v16.4s, v0.4s
	ext	v5.16b, v5.16b, v5.16b, #8
	eor	v1.16b, v4.16b, v1.16b
	add	v2.4s, v2.4s, v16.4s
	ext	v4.16b, v4.16b, v4.16b, #12
	ushr	v17.4s, v1.4s, #7
	shl	v1.4s, v1.4s, #25
	ext	v2.16b, v2.16b, v2.16b, #4
	orr	v1.16b, v1.16b, v17.16b
	ext	v17.16b, v7.16b, v7.16b, #12
	add	v2.4s, v2.4s, v1.4s
	ext	v7.16b, v7.16b, v17.16b, #12
	mov	v17.16b, v6.16b
	eor	v5.16b, v5.16b, v2.16b
	rev64	v7.4s, v7.4s
	mov	v17.s[1], v3.s[2]
	rev32	v5.8h, v5.8h
	add	v4.4s, v4.4s, v5.4s
	eor	v18.16b, v4.16b, v1.16b
	trn2	v1.4s, v7.4s, v17.4s
	ushr	v7.4s, v18.4s, #12
	shl	v17.4s, v18.4s, #20
	add	v2.4s, v2.4s, v1.4s
	zip1	v18.2d, v6.2d, v0.2d
	zip2	v0.4s, v0.4s, v6.4s
	orr	v7.16b, v17.16b, v7.16b
	mov	v18.s[3], v3.s[3]
	add	v2.4s, v2.4s, v7.4s
	eor	v5.16b, v2.16b, v5.16b
	ext	v2.16b, v2.16b, v2.16b, #12
	ushr	v17.4s, v5.4s, #8
	shl	v5.4s, v5.4s, #24
	orr	v5.16b, v5.16b, v17.16b
	add	v17.4s, v5.4s, v4.4s
	ext	v4.16b, v18.16b, v18.16b, #12
	ext	v5.16b, v5.16b, v5.16b, #8
	eor	v7.16b, v17.16b, v7.16b
	uzp1	v4.4s, v18.4s, v4.4s
	ext	v17.16b, v17.16b, v17.16b, #4
	ushr	v18.4s, v7.4s, #7
	shl	v7.4s, v7.4s, #25
	add	v2.4s, v2.4s, v4.4s
	orr	v7.16b, v7.16b, v18.16b
	add	v2.4s, v2.4s, v7.4s
	eor	v5.16b, v5.16b, v2.16b
	rev32	v5.8h, v5.8h
	add	v6.4s, v17.4s, v5.4s
	zip1	v17.4s, v0.4s, v3.4s
	zip1	v0.4s, v3.4s, v0.4s
	eor	v3.16b, v6.16b, v7.16b
	ext	v0.16b, v0.16b, v17.16b, #8
	ushr	v7.4s, v3.4s, #12
	shl	v3.4s, v3.4s, #20
	add	v2.4s, v2.4s, v0.4s
	orr	v3.16b, v3.16b, v7.16b
	ext	v7.16b, v16.16b, v16.16b, #4
	add	v2.4s, v2.4s, v3.4s
	uzp1	v17.4s, v7.4s, v7.4s
	eor	v5.16b, v2.16b, v5.16b
	ushr	v16.4s, v5.4s, #8
	shl	v5.4s, v5.4s, #24
	orr	v5.16b, v5.16b, v16.16b
	ext	v16.16b, v17.16b, v7.16b, #8
	add	v6.4s, v5.4s, v6.4s
	uzp2	v16.4s, v16.4s, v1.4s
	ext	v5.16b, v5.16b, v5.16b, #8
	eor	v3.16b, v6.16b, v3.16b
	add	v2.4s, v2.4s, v16.4s
	ext	v6.16b, v6.16b, v6.16b, #12
	ushr	v17.4s, v3.4s, #7
	shl	v3.4s, v3.4s, #25
	ext	v2.16b, v2.16b, v2.16b, #4
	orr	v3.16b, v3.16b, v17.16b
	add	v17.4s, v2.4s, v3.4s
	eor	v2.16b, v5.16b, v17.16b
	ext	v5.16b, v7.16b, v7.16b, #12
	rev32	v18.8h, v2.8h
	ext	v2.16b, v7.16b, v5.16b, #12
	mov	v5.16b, v0.16b
	add	v6.4s, v6.4s, v18.4s
	rev64	v2.4s, v2.4s
	mov	v5.s[1], v4.s[2]
	eor	v3.16b, v6.16b, v3.16b
	trn2	v2.4s, v2.4s, v5.4s
	ushr	v5.4s, v3.4s, #12
	shl	v3.4s, v3.4s, #20
	add	v7.4s, v17.4s, v2.4s
	orr	v3.16b, v3.16b, v5.16b
	add	v5.4s, v7.4s, v3.4s
	eor	v7.16b, v5.16b, v18.16b
	zip1	v18.2d, v0.2d, v1.2d
	ext	v5.16b, v5.16b, v5.16b, #12
	zip2	v0.4s, v1.4s, v0.4s
	ushr	v17.4s, v7.4s, #8
	shl	v7.4s, v7.4s, #24
	mov	v18.s[3], v4.s[3]
	orr	v7.16b, v7.16b, v17.16b
	ext	v17.16b, v18.16b, v18.16b, #12
	add	v6.4s, v7.4s, v6.4s
	ext	v7.16b, v7.16b, v7.16b, #8
	eor	v19.16b, v6.16b, v3.16b
	uzp1	v3.4s, v18.4s, v17.4s
	ext	v6.16b, v6.16b, v6.16b, #4
	ushr	v17.4s, v19.4s, #7
	shl	v18.4s, v19.4s, #25
	add	v5.4s, v5.4s, v3.4s
	orr	v17.16b, v18.16b, v17.16b
	add	v5.4s, v5.4s, v17.4s
	eor	v7.16b, v7.16b, v5.16b
	rev32	v7.8h, v7.8h
	add	v1.4s, v6.4s, v7.4s
	zip1	v6.4s, v0.4s, v4.4s
	zip1	v0.4s, v4.4s, v0.4s
	eor	v4.16b, v1.16b, v17.16b
	ext	v6.16b, v0.16b, v6.16b, #8
	ushr	v0.4s, v4.4s, #12
	shl	v4.4s, v4.4s, #20
	add	v5.4s, v5.4s, v6.4s
	zip1	v20.2d, v6.2d, v2.2d
	orr	v0.16b, v4.16b, v0.16b
	mov	v20.s[3], v3.s[3]
	add	v4.4s, v5.4s, v0.4s
	eor	v5.16b, v4.16b, v7.16b
	ext	v7.16b, v16.16b, v16.16b, #4
	ushr	v16.4s, v5.4s, #8
	shl	v5.4s, v5.4s, #24
	uzp1	v17.4s, v7.4s, v7.4s
	orr	v5.16b, v5.16b, v16.16b
	ext	v16.16b, v17.16b, v7.16b, #8
	add	v1.4s, v5.4s, v1.4s
	uzp2	v16.4s, v16.4s, v2.4s
	zip2	v2.4s, v2.4s, v6.4s
	eor	v0.16b, v1.16b, v0.16b
	add	v4.4s, v4.4s, v16.4s
	ext	v1.16b, v1.16b, v1.16b, #12
	ext	v16.16b, v16.16b, v16.16b, #4
	ushr	v17.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	ext	v4.16b, v4.16b, v4.16b, #4
	orr	v17.16b, v0.16b, v17.16b
	ext	v0.16b, v5.16b, v5.16b, #8
	ext	v5.16b, v7.16b, v7.16b, #12
	add	v4.4s, v4.4s, v17.4s
	eor	v0.16b, v0.16b, v4.16b
	rev32	v18.8h, v0.8h
	ext	v0.16b, v7.16b, v5.16b, #12
	mov	v5.16b, v6.16b
	add	v7.4s, v1.4s, v18.4s
	rev64	v1.4s, v0.4s
	mov	v5.s[1], v3.s[2]
	eor	v17.16b, v7.16b, v17.16b
	trn2	v1.4s, v1.4s, v5.4s
	ushr	v19.4s, v17.4s, #12
	shl	v17.4s, v17.4s, #20
	add	v4.4s, v4.4s, v1.4s
	orr	v17.16b, v17.16b, v19.16b
	add	v19.4s, v4.4s, v17.4s
	eor	v4.16b, v19.16b, v18.16b
	ext	v19.16b, v19.16b, v19.16b, #12
	ushr	v18.4s, v4.4s, #8
	shl	v4.4s, v4.4s, #24
	orr	v18.16b, v4.16b, v18.16b
	ext	v4.16b, v20.16b, v20.16b, #12
	add	v7.4s, v18.4s, v7.4s
	uzp1	v4.4s, v20.4s, v4.4s
	ext	v18.16b, v18.16b, v18.16b, #8
	eor	v17.16b, v7.16b, v17.16b
	add	v19.4s, v19.4s, v4.4s
	ext	v7.16b, v7.16b, v7.16b, #4
	ushr	v20.4s, v17.4s, #7
	shl	v17.4s, v17.4s, #25
	orr	v17.16b, v17.16b, v20.16b
	add	v19.4s, v19.4s, v17.4s
	eor	v18.16b, v18.16b, v19.16b
	rev32	v18.8h, v18.8h
	add	v6.4s, v7.4s, v18.4s
	zip1	v7.4s, v2.4s, v3.4s
	zip1	v2.4s, v3.4s, v2.4s
	eor	v3.16b, v6.16b, v17.16b
	ext	v2.16b, v2.16b, v7.16b, #8
	ushr	v7.4s, v3.4s, #12
	shl	v3.4s, v3.4s, #20
	add	v17.4s, v19.4s, v2.4s
	zip1	v1.2d, v2.2d, v1.2d
	zip2	v0.4s, v0.4s, v2.4s
	orr	v3.16b, v3.16b, v7.16b
	mov	v1.s[3], v4.s[3]
	add	v7.4s, v17.4s, v3.4s
	eor	v17.16b, v7.16b, v18.16b
	ext	v7.16b, v7.16b, v7.16b, #4
	ushr	v18.4s, v17.4s, #8
	shl	v17.4s, v17.4s, #24
	orr	v17.16b, v17.16b, v18.16b
	ext	v18.16b, v16.16b, v16.16b, #8
	add	v6.4s, v17.4s, v6.4s
	uzp2	v5.4s, v18.4s, v5.4s
	eor	v3.16b, v6.16b, v3.16b
	ext	v5.16b, v5.16b, v18.16b, #4
	ext	v6.16b, v6.16b, v6.16b, #12
	ushr	v18.4s, v3.4s, #7
	shl	v3.4s, v3.4s, #25
	add	v5.4s, v7.4s, v5.4s
	ext	v7.16b, v17.16b, v17.16b, #8
	ext	v17.16b, v16.16b, v16.16b, #12
	orr	v3.16b, v3.16b, v18.16b
	ext	v16.16b, v16.16b, v17.16b, #12
	add	v5.4s, v3.4s, v5.4s
	mov	v17.16b, v2.16b
	rev64	v16.4s, v16.4s
	eor	v7.16b, v7.16b, v5.16b
	mov	v17.s[1], v4.s[2]
	rev32	v7.8h, v7.8h
	trn2	v16.4s, v16.4s, v17.4s
	add	v6.4s, v6.4s, v7.4s
	add	v5.4s, v5.4s, v16.4s
	eor	v3.16b, v6.16b, v3.16b
	ushr	v17.4s, v3.4s, #12
	shl	v3.4s, v3.4s, #20
	orr	v3.16b, v3.16b, v17.16b
	add	v5.4s, v5.4s, v3.4s
	eor	v7.16b, v5.16b, v7.16b
	ext	v5.16b, v5.16b, v5.16b, #12
	ushr	v16.4s, v7.4s, #8
	shl	v7.4s, v7.4s, #24
	orr	v7.16b, v7.16b, v16.16b
	ext	v16.16b, v1.16b, v1.16b, #12
	add	v6.4s, v7.4s, v6.4s
	uzp1	v1.4s, v1.4s, v16.4s
	eor	v3.16b, v6.16b, v3.16b
	add	v1.4s, v5.4s, v1.4s
	ext	v5.16b, v7.16b, v7.16b, #8
	ext	v6.16b, v6.16b, v6.16b, #4
	ushr	v16.4s, v3.4s, #7
	shl	v3.4s, v3.4s, #25
	orr	v3.16b, v3.16b, v16.16b
	add	v1.4s, v1.4s, v3.4s
	eor	v5.16b, v5.16b, v1.16b
	rev32	v5.8h, v5.8h
	add	v2.4s, v6.4s, v5.4s
	zip1	v6.4s, v0.4s, v4.4s
	zip1	v0.4s, v4.4s, v0.4s
	eor	v3.16b, v2.16b, v3.16b
	ext	v0.16b, v0.16b, v6.16b, #8
	ushr	v4.4s, v3.4s, #12
	shl	v3.4s, v3.4s, #20
	add	v0.4s, v1.4s, v0.4s
	orr	v1.16b, v3.16b, v4.16b
	add	v0.4s, v0.4s, v1.4s
	eor	v3.16b, v0.16b, v5.16b
	ext	v0.16b, v0.16b, v0.16b, #4
	ushr	v4.4s, v3.4s, #8
	shl	v3.4s, v3.4s, #24
	orr	v3.16b, v3.16b, v4.16b
	add	v2.4s, v3.4s, v2.4s
	ext	v3.16b, v3.16b, v3.16b, #8
	eor	v1.16b, v2.16b, v1.16b
	ext	v2.16b, v2.16b, v2.16b, #12
	ushr	v4.4s, v1.4s, #7
	shl	v1.4s, v1.4s, #25
	stp	q2, q3, [x0, #32]
	orr	v1.16b, v1.16b, v4.16b
	stp	q0, q1, [x0]
	ret
.Lfunc_end1:
	.size	compress_pre, .Lfunc_end1-compress_pre
	.cfi_endproc

	.globl	zfs_blake3_compress_xof_sse2
	.p2align	2
	.type	zfs_blake3_compress_xof_sse2,@function
zfs_blake3_compress_xof_sse2:
	.cfi_startproc
	hint	#25
	CFI_NEGATE_RA_STATE
	sub	sp, sp, #96
	stp	x29, x30, [sp, #64]
	add	x29, sp, #64
	stp	x20, x19, [sp, #80]
	.cfi_def_cfa w29, 32
	.cfi_offset w19, -8
	.cfi_offset w20, -16
	.cfi_offset w30, -24
	.cfi_offset w29, -32
	mov	x20, x0
	mov	x19, x5
	mov	w5, w4
	mov	x4, x3
	mov	w3, w2
	mov	x2, x1
	mov	x0, sp
	mov	x1, x20
	bl	compress_pre
	ldp	q0, q1, [sp]
	ldp	q2, q3, [sp, #32]
	eor	v0.16b, v2.16b, v0.16b
	eor	v1.16b, v3.16b, v1.16b
	ldp	x29, x30, [sp, #64]
	stp	q0, q1, [x19]
	ldr	q0, [x20]
	eor	v0.16b, v0.16b, v2.16b
	str	q0, [x19, #32]
	ldr	q0, [x20, #16]
	eor	v0.16b, v0.16b, v3.16b
	str	q0, [x19, #48]
	ldp	x20, x19, [sp, #80]
	add	sp, sp, #96
	hint	#29
	ret
.Lfunc_end2:
	.size	zfs_blake3_compress_xof_sse2, .Lfunc_end2-zfs_blake3_compress_xof_sse2
	.cfi_endproc

	.section	.rodata.cst16,"aM",@progbits,16
	.p2align	4
.LCPI3_0:
	.word	0
	.word	1
	.word	2
	.word	3
	.text
	.globl	zfs_blake3_hash_many_sse2
	.p2align	2
	.type	zfs_blake3_hash_many_sse2,@function
zfs_blake3_hash_many_sse2:
	.cfi_startproc
	hint	#25
	CFI_NEGATE_RA_STATE
	stp	d15, d14, [sp, #-160]!
	stp	d13, d12, [sp, #16]
	stp	d11, d10, [sp, #32]
	stp	d9, d8, [sp, #48]
	stp	x29, x30, [sp, #64]
	add	x29, sp, #64
	stp	x28, x27, [sp, #80]
	stp	x26, x25, [sp, #96]
	stp	x24, x23, [sp, #112]
	stp	x22, x21, [sp, #128]
	stp	x20, x19, [sp, #144]
	sub	sp, sp, #464
	.cfi_def_cfa w29, 96
	.cfi_offset w19, -8
	.cfi_offset w20, -16
	.cfi_offset w21, -24
	.cfi_offset w22, -32
	.cfi_offset w23, -40
	.cfi_offset w24, -48
	.cfi_offset w25, -56
	.cfi_offset w26, -64
	.cfi_offset w27, -72
	.cfi_offset w28, -80
	.cfi_offset w30, -88
	.cfi_offset w29, -96
	.cfi_offset b8, -104
	.cfi_offset b9, -112
	.cfi_offset b10, -120
	.cfi_offset b11, -128
	.cfi_offset b12, -136
	.cfi_offset b13, -144
	.cfi_offset b14, -152
	.cfi_offset b15, -160
	mov	w19, w6
	mov	x20, x4
	mov	x24, x1
	ldr	x26, [x29, #104]
	ldrb	w27, [x29, #96]
	cmp	x1, #4
	str	x3, [sp, #40]
	b.lo	.LBB3_6
	adrp	x8, .LCPI3_0
	sbfx	w9, w5, #0, #1
	mov	w10, #44677
	mov	w11, #62322
	movk	w10, #47975, lsl #16
	movk	w11, #15470, lsl #16
	ldr	q0, [x8, :lo12:.LCPI3_0]
	dup	v1.4s, w9
	mov	w9, #58983
	orr	w8, w7, w19
	movk	w9, #27145, lsl #16
	and	v0.16b, v1.16b, v0.16b
	dup	v1.4s, w11
	movi	v24.4s, #64
	dup	v2.4s, w9
	mov	w9, #62778
	movk	w9, #42319, lsl #16
	str	q0, [sp, #16]
	orr	v0.4s, #128, lsl #24
	stp	q2, q1, [sp, #48]
	str	q0, [sp]
	dup	v0.4s, w10
	str	q0, [sp, #80]
	b	.LBB3_3
.LBB3_2:
	zip1	v0.4s, v12.4s, v31.4s
	add	x10, x20, #4
	zip1	v1.4s, v29.4s, v30.4s
	tst	w5, #0x1
	zip1	v2.4s, v28.4s, v23.4s
	csel	x20, x10, x20, ne
	zip1	v3.4s, v13.4s, v25.4s
	add	x0, x0, #32
	zip2	v6.4s, v12.4s, v31.4s
	sub	x24, x24, #4
	zip1	v4.2d, v0.2d, v1.2d
	cmp	x24, #3
	zip2	v7.4s, v29.4s, v30.4s
	zip1	v5.2d, v2.2d, v3.2d
	zip2	v0.2d, v0.2d, v1.2d
	zip2	v1.2d, v2.2d, v3.2d
	zip2	v2.4s, v28.4s, v23.4s
	zip2	v3.4s, v13.4s, v25.4s
	stp	q4, q5, [x26]
	zip2	v4.2d, v6.2d, v7.2d
	stp	q0, q1, [x26, #32]
	zip1	v0.2d, v6.2d, v7.2d
	zip1	v1.2d, v2.2d, v3.2d
	zip2	v2.2d, v2.2d, v3.2d
	stp	q0, q1, [x26, #64]
	stp	q4, q2, [x26, #96]
	add	x26, x26, #128
	b.ls	.LBB3_6
.LBB3_3:
	ldr	x14, [sp, #40]
	mov	x10, x14
	add	x11, x14, #8
	add	x12, x14, #12
	add	x13, x14, #16
	ld1r	{ v12.4s }, [x10], #4
	ld1r	{ v29.4s }, [x11]
	add	x11, x14, #20
	ld1r	{ v30.4s }, [x12]
	add	x12, x14, #24
	ld1r	{ v28.4s }, [x13]
	ld1r	{ v23.4s }, [x11]
	add	x11, x14, #28
	ld1r	{ v13.4s }, [x12]
	ld1r	{ v31.4s }, [x10]
	ld1r	{ v25.4s }, [x11]
	cbz	x2, .LBB3_2
	ldr	q1, [sp, #16]
	dup	v0.4s, w20
	lsr	x12, x20, #32
	mov	x10, xzr
	ldp	x13, x14, [x0, #16]
	add	v1.4s, v0.4s, v1.4s
	mov	x15, x2
	movi	v0.4s, #128, lsl #24
	mov	w4, w8
	str	q1, [sp, #112]
	eor	v0.16b, v1.16b, v0.16b
	ldr	q1, [sp]
	cmgt	v0.4s, v1.4s, v0.4s
	dup	v1.4s, w12
	ldp	x11, x12, [x0]
	sub	v0.4s, v1.4s, v0.4s
	str	q0, [sp, #96]
.LBB3_5:
	add	x17, x11, x10
	add	x21, x12, x10
	add	x16, x13, x10
	add	x6, x14, x10
	subs	x15, x15, #1
	add	x10, x10, #64
	ldp	q0, q1, [x17]
	csel	w3, w27, wzr, eq
	orr	w3, w3, w4
	mov	w4, w19
	and	w3, w3, #0xff
	ldp	q3, q6, [x21]
	dup	v2.4s, w3
	zip1	v21.4s, v0.4s, v3.4s
	zip2	v19.4s, v0.4s, v3.4s
	ldp	q5, q7, [x16]
	zip1	v17.4s, v1.4s, v6.4s
	zip2	v22.4s, v1.4s, v6.4s
	ldp	q16, q18, [x6]
	zip1	v4.4s, v5.4s, v16.4s
	zip2	v0.4s, v5.4s, v16.4s
	ldp	q26, q27, [x17, #32]
	zip1	v1.4s, v7.4s, v18.4s
	zip2	v3.4s, v7.4s, v18.4s
	zip2	v20.2d, v19.2d, v0.2d
	mov	v19.d[1], v0.d[0]
	dup	v18.4s, w9
	ldp	q8, q9, [x21, #32]
	stur	q19, [x29, #-208]
	zip2	v7.4s, v26.4s, v8.4s
	zip1	v10.4s, v26.4s, v8.4s
	ldp	q11, q5, [x16, #32]
	zip2	v26.2d, v17.2d, v1.2d
	stp	q7, q26, [sp, #192]
	mov	v17.d[1], v1.d[0]
	add	v1.4s, v23.4s, v31.4s
	ldp	q16, q6, [x6, #32]
	stur	q17, [x29, #-256]
	add	v1.4s, v1.4s, v19.4s
	zip1	v8.4s, v11.4s, v16.4s
	zip2	v7.4s, v11.4s, v16.4s
	zip1	v11.4s, v27.4s, v9.4s
	zip2	v9.4s, v27.4s, v9.4s
	zip2	v27.2d, v21.2d, v4.2d
	mov	v21.d[1], v4.d[0]
	str	q7, [sp, #224]
	add	v4.4s, v28.4s, v12.4s
	zip1	v15.4s, v5.4s, v6.4s
	zip2	v14.4s, v5.4s, v6.4s
	stur	q27, [x29, #-192]
	zip2	v16.2d, v22.2d, v3.2d
	stp	q20, q21, [x29, #-240]
	add	v0.4s, v4.4s, v21.4s
	ldp	q6, q4, [sp, #96]
	mov	v22.d[1], v3.d[0]
	add	v5.4s, v25.4s, v30.4s
	add	v3.4s, v13.4s, v29.4s
	eor	v6.16b, v1.16b, v6.16b
	add	v1.4s, v1.4s, v20.4s
	str	q22, [sp, #256]
	eor	v4.16b, v0.16b, v4.16b
	add	v5.4s, v5.4s, v22.4s
	add	v3.4s, v3.4s, v17.4s
	ldr	q17, [sp, #48]
	rev32	v6.8h, v6.8h
	rev32	v4.8h, v4.8h
	eor	v2.16b, v5.16b, v2.16b
	eor	v7.16b, v3.16b, v24.16b
	add	v0.4s, v0.4s, v27.4s
	add	v21.4s, v4.4s, v17.4s
	rev32	v31.8h, v2.8h
	ldr	q2, [sp, #80]
	rev32	v7.8h, v7.8h
	mov	v27.16b, v16.16b
	eor	v17.16b, v21.16b, v28.16b
	add	v29.4s, v6.4s, v2.4s
	ldr	q2, [sp, #64]
	add	v24.4s, v31.4s, v18.4s
	str	q27, [sp, #176]
	ushr	v19.4s, v17.4s, #12
	shl	v17.4s, v17.4s, #20
	add	v30.4s, v7.4s, v2.4s
	eor	v18.16b, v29.16b, v23.16b
	orr	v12.16b, v17.16b, v19.16b
	eor	v17.16b, v30.16b, v13.16b
	eor	v19.16b, v24.16b, v25.16b
	ushr	v23.4s, v18.4s, #12
	shl	v18.4s, v18.4s, #20
	ushr	v25.4s, v17.4s, #12
	shl	v17.4s, v17.4s, #20
	ushr	v28.4s, v19.4s, #12
	shl	v19.4s, v19.4s, #20
	orr	v13.16b, v18.16b, v23.16b
	orr	v25.16b, v17.16b, v25.16b
	orr	v2.16b, v19.16b, v28.16b
	add	v28.4s, v0.4s, v12.4s
	add	v0.4s, v3.4s, v26.4s
	add	v18.4s, v1.4s, v13.4s
	add	v3.4s, v5.4s, v16.4s
	eor	v1.16b, v28.16b, v4.16b
	add	v17.4s, v0.4s, v25.4s
	eor	v0.16b, v18.16b, v6.16b
	add	v19.4s, v3.4s, v2.4s
	ushr	v16.4s, v1.4s, #8
	shl	v3.4s, v1.4s, #24
	eor	v4.16b, v17.16b, v7.16b
	ushr	v6.4s, v0.4s, #8
	shl	v1.4s, v0.4s, #24
	eor	v5.16b, v19.16b, v31.16b
	ushr	v23.4s, v4.4s, #8
	shl	v4.4s, v4.4s, #24
	orr	v7.16b, v3.16b, v16.16b
	orr	v6.16b, v1.16b, v6.16b
	ushr	v31.4s, v5.4s, #8
	shl	v0.4s, v5.4s, #24
	orr	v5.16b, v4.16b, v23.16b
	add	v4.4s, v7.4s, v21.4s
	ldr	q21, [sp, #192]
	add	v3.4s, v6.4s, v29.4s
	orr	v31.16b, v0.16b, v31.16b
	add	v23.4s, v5.4s, v30.4s
	eor	v0.16b, v4.16b, v12.16b
	eor	v1.16b, v3.16b, v13.16b
	add	v16.4s, v31.4s, v24.4s
	eor	v20.16b, v23.16b, v25.16b
	ushr	v24.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	ushr	v29.4s, v1.4s, #7
	shl	v1.4s, v1.4s, #25
	ushr	v30.4s, v20.4s, #7
	shl	v20.4s, v20.4s, #25
	orr	v25.16b, v0.16b, v24.16b
	orr	v0.16b, v1.16b, v29.16b
	mov	v29.16b, v10.16b
	orr	v1.16b, v20.16b, v30.16b
	mov	v20.16b, v10.16b
	mov	v24.16b, v21.16b
	ldr	q20, [sp, #224]
	mov	v29.d[1], v8.d[0]
	mov	v13.16b, v9.16b
	zip2	v30.2d, v10.2d, v8.2d
	zip2	v8.2d, v21.2d, v20.2d
	mov	v26.16b, v11.16b
	mov	v24.d[1], v20.d[0]
	add	v20.4s, v28.4s, v29.4s
	mov	v13.d[1], v14.d[0]
	str	q8, [sp, #128]
	eor	v2.16b, v16.16b, v2.16b
	mov	v26.d[1], v15.d[0]
	str	q24, [sp, #192]
	add	v20.4s, v20.4s, v0.4s
	add	v19.4s, v19.4s, v13.4s
	ushr	v12.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	zip2	v10.2d, v9.2d, v14.2d
	add	v18.4s, v18.4s, v24.4s
	add	v17.4s, v17.4s, v26.4s
	mov	v14.16b, v26.16b
	eor	v26.16b, v20.16b, v31.16b
	stp	q10, q30, [sp, #224]
	add	v19.4s, v19.4s, v25.4s
	orr	v2.16b, v2.16b, v12.16b
	add	v18.4s, v18.4s, v1.4s
	rev32	v26.8h, v26.8h
	eor	v5.16b, v19.16b, v5.16b
	add	v17.4s, v17.4s, v2.4s
	eor	v7.16b, v18.16b, v7.16b
	add	v23.4s, v23.4s, v26.4s
	rev32	v5.8h, v5.8h
	eor	v6.16b, v17.16b, v6.16b
	rev32	v7.8h, v7.8h
	eor	v0.16b, v23.16b, v0.16b
	add	v3.4s, v3.4s, v5.4s
	rev32	v6.8h, v6.8h
	add	v16.4s, v16.4s, v7.4s
	ushr	v31.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	eor	v25.16b, v3.16b, v25.16b
	add	v4.4s, v4.4s, v6.4s
	eor	v1.16b, v16.16b, v1.16b
	orr	v0.16b, v0.16b, v31.16b
	ushr	v31.4s, v25.4s, #12
	shl	v25.4s, v25.4s, #20
	add	v20.4s, v20.4s, v30.4s
	zip2	v21.2d, v11.2d, v15.2d
	ushr	v11.4s, v1.4s, #12
	shl	v1.4s, v1.4s, #20
	eor	v2.16b, v4.16b, v2.16b
	orr	v25.16b, v25.16b, v31.16b
	add	v19.4s, v19.4s, v10.4s
	add	v20.4s, v20.4s, v0.4s
	orr	v1.16b, v1.16b, v11.16b
	ushr	v11.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	add	v18.4s, v18.4s, v8.4s
	add	v19.4s, v19.4s, v25.4s
	eor	v26.16b, v20.16b, v26.16b
	orr	v2.16b, v2.16b, v11.16b
	add	v17.4s, v17.4s, v21.4s
	add	v18.4s, v18.4s, v1.4s
	eor	v5.16b, v19.16b, v5.16b
	ushr	v31.4s, v26.4s, #8
	shl	v26.4s, v26.4s, #24
	add	v17.4s, v17.4s, v2.4s
	ushr	v11.4s, v5.4s, #8
	shl	v5.4s, v5.4s, #24
	eor	v7.16b, v18.16b, v7.16b
	orr	v26.16b, v26.16b, v31.16b
	eor	v6.16b, v17.16b, v6.16b
	orr	v5.16b, v5.16b, v11.16b
	ushr	v31.4s, v7.4s, #8
	shl	v7.4s, v7.4s, #24
	add	v23.4s, v26.4s, v23.4s
	ushr	v11.4s, v6.4s, #8
	shl	v6.4s, v6.4s, #24
	orr	v7.16b, v7.16b, v31.16b
	add	v3.4s, v5.4s, v3.4s
	eor	v0.16b, v23.16b, v0.16b
	ldp	q28, q12, [x29, #-256]
	orr	v6.16b, v6.16b, v11.16b
	add	v16.4s, v7.4s, v16.4s
	eor	v25.16b, v3.16b, v25.16b
	ushr	v31.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	add	v4.4s, v6.4s, v4.4s
	ushr	v11.4s, v25.4s, #7
	shl	v25.4s, v25.4s, #25
	eor	v1.16b, v16.16b, v1.16b
	orr	v0.16b, v0.16b, v31.16b
	add	v18.4s, v18.4s, v12.4s
	mov	v15.16b, v29.16b
	ldur	q29, [x29, #-208]
	eor	v2.16b, v4.16b, v2.16b
	orr	v25.16b, v25.16b, v11.16b
	ushr	v31.4s, v1.4s, #7
	shl	v1.4s, v1.4s, #25
	str	q15, [sp, #160]
	add	v20.4s, v20.4s, v29.4s
	add	v18.4s, v18.4s, v0.4s
	ushr	v11.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	orr	v1.16b, v1.16b, v31.16b
	add	v20.4s, v20.4s, v25.4s
	add	v17.4s, v17.4s, v27.4s
	eor	v6.16b, v6.16b, v18.16b
	orr	v2.16b, v2.16b, v11.16b
	add	v19.4s, v19.4s, v28.4s
	eor	v7.16b, v7.16b, v20.16b
	add	v17.4s, v17.4s, v1.4s
	rev32	v6.8h, v6.8h
	add	v19.4s, v19.4s, v2.4s
	rev32	v7.8h, v7.8h
	eor	v5.16b, v17.16b, v5.16b
	add	v3.4s, v3.4s, v6.4s
	eor	v26.16b, v19.16b, v26.16b
	add	v4.4s, v4.4s, v7.4s
	rev32	v5.8h, v5.8h
	eor	v0.16b, v3.16b, v0.16b
	rev32	v26.8h, v26.8h
	eor	v25.16b, v4.16b, v25.16b
	add	v23.4s, v23.4s, v5.4s
	ushr	v11.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	add	v16.4s, v16.4s, v26.4s
	ushr	v31.4s, v25.4s, #12
	shl	v25.4s, v25.4s, #20
	eor	v1.16b, v23.16b, v1.16b
	orr	v0.16b, v0.16b, v11.16b
	add	v18.4s, v18.4s, v24.4s
	orr	v25.16b, v25.16b, v31.16b
	eor	v2.16b, v16.16b, v2.16b
	ushr	v31.4s, v1.4s, #12
	shl	v1.4s, v1.4s, #20
	add	v20.4s, v20.4s, v22.4s
	add	v18.4s, v18.4s, v0.4s
	mov	v9.16b, v30.16b
	mov	v30.16b, v21.16b
	ldur	q21, [x29, #-224]
	ushr	v11.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	orr	v1.16b, v1.16b, v31.16b
	add	v20.4s, v20.4s, v25.4s
	str	q30, [sp, #144]
	add	v17.4s, v17.4s, v21.4s
	ldur	q21, [x29, #-192]
	eor	v6.16b, v18.16b, v6.16b
	orr	v2.16b, v2.16b, v11.16b
	add	v19.4s, v19.4s, v30.4s
	eor	v7.16b, v20.16b, v7.16b
	add	v17.4s, v17.4s, v1.4s
	ushr	v11.4s, v6.4s, #8
	shl	v6.4s, v6.4s, #24
	add	v19.4s, v19.4s, v2.4s
	ushr	v31.4s, v7.4s, #8
	shl	v7.4s, v7.4s, #24
	eor	v5.16b, v17.16b, v5.16b
	orr	v6.16b, v6.16b, v11.16b
	eor	v26.16b, v19.16b, v26.16b
	orr	v7.16b, v7.16b, v31.16b
	ushr	v31.4s, v5.4s, #8
	shl	v5.4s, v5.4s, #24
	add	v3.4s, v6.4s, v3.4s
	ushr	v11.4s, v26.4s, #8
	shl	v26.4s, v26.4s, #24
	add	v4.4s, v7.4s, v4.4s
	orr	v5.16b, v5.16b, v31.16b
	eor	v0.16b, v3.16b, v0.16b
	orr	v26.16b, v26.16b, v11.16b
	eor	v25.16b, v4.16b, v25.16b
	add	v23.4s, v5.4s, v23.4s
	ushr	v11.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	add	v16.4s, v26.4s, v16.4s
	ushr	v31.4s, v25.4s, #7
	shl	v25.4s, v25.4s, #25
	eor	v1.16b, v23.16b, v1.16b
	orr	v0.16b, v0.16b, v11.16b
	add	v20.4s, v20.4s, v21.4s
	orr	v25.16b, v25.16b, v31.16b
	eor	v2.16b, v16.16b, v2.16b
	ushr	v31.4s, v1.4s, #7
	shl	v1.4s, v1.4s, #25
	add	v20.4s, v20.4s, v0.4s
	add	v19.4s, v19.4s, v10.4s
	ushr	v11.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	orr	v1.16b, v1.16b, v31.16b
	add	v18.4s, v18.4s, v14.4s
	eor	v26.16b, v20.16b, v26.16b
	add	v19.4s, v19.4s, v25.4s
	orr	v2.16b, v2.16b, v11.16b
	add	v17.4s, v17.4s, v9.4s
	ldr	q9, [sp, #208]
	add	v18.4s, v18.4s, v1.4s
	rev32	v26.8h, v26.8h
	eor	v5.16b, v19.16b, v5.16b
	add	v17.4s, v17.4s, v2.4s
	eor	v7.16b, v18.16b, v7.16b
	add	v23.4s, v23.4s, v26.4s
	rev32	v5.8h, v5.8h
	eor	v6.16b, v17.16b, v6.16b
	rev32	v7.8h, v7.8h
	eor	v0.16b, v23.16b, v0.16b
	add	v3.4s, v3.4s, v5.4s
	rev32	v6.8h, v6.8h
	add	v16.4s, v16.4s, v7.4s
	ushr	v31.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	eor	v25.16b, v3.16b, v25.16b
	add	v4.4s, v4.4s, v6.4s
	eor	v1.16b, v16.16b, v1.16b
	orr	v0.16b, v0.16b, v31.16b
	ushr	v31.4s, v25.4s, #12
	shl	v25.4s, v25.4s, #20
	add	v20.4s, v20.4s, v8.4s
	ushr	v11.4s, v1.4s, #12
	shl	v1.4s, v1.4s, #20
	eor	v2.16b, v4.16b, v2.16b
	orr	v25.16b, v25.16b, v31.16b
	add	v19.4s, v19.4s, v15.4s
	add	v20.4s, v20.4s, v0.4s
	orr	v1.16b, v1.16b, v11.16b
	ushr	v11.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	add	v18.4s, v18.4s, v9.4s
	add	v19.4s, v19.4s, v25.4s
	eor	v26.16b, v20.16b, v26.16b
	orr	v2.16b, v2.16b, v11.16b
	add	v17.4s, v17.4s, v13.4s
	add	v18.4s, v18.4s, v1.4s
	eor	v5.16b, v19.16b, v5.16b
	ushr	v31.4s, v26.4s, #8
	shl	v26.4s, v26.4s, #24
	add	v17.4s, v17.4s, v2.4s
	ushr	v11.4s, v5.4s, #8
	shl	v5.4s, v5.4s, #24
	eor	v7.16b, v18.16b, v7.16b
	orr	v26.16b, v26.16b, v31.16b
	eor	v6.16b, v17.16b, v6.16b
	orr	v5.16b, v5.16b, v11.16b
	ushr	v31.4s, v7.4s, #8
	shl	v7.4s, v7.4s, #24
	add	v23.4s, v26.4s, v23.4s
	ushr	v11.4s, v6.4s, #8
	shl	v6.4s, v6.4s, #24
	orr	v7.16b, v7.16b, v31.16b
	add	v3.4s, v5.4s, v3.4s
	eor	v0.16b, v23.16b, v0.16b
	orr	v6.16b, v6.16b, v11.16b
	add	v16.4s, v7.4s, v16.4s
	eor	v25.16b, v3.16b, v25.16b
	ushr	v31.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	add	v4.4s, v6.4s, v4.4s
	ushr	v11.4s, v25.4s, #7
	shl	v25.4s, v25.4s, #25
	eor	v1.16b, v16.16b, v1.16b
	orr	v0.16b, v0.16b, v31.16b
	add	v18.4s, v18.4s, v24.4s
	eor	v2.16b, v4.16b, v2.16b
	orr	v25.16b, v25.16b, v11.16b
	ushr	v31.4s, v1.4s, #7
	shl	v1.4s, v1.4s, #25
	add	v20.4s, v20.4s, v12.4s
	add	v18.4s, v18.4s, v0.4s
	ushr	v11.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	orr	v1.16b, v1.16b, v31.16b
	add	v20.4s, v20.4s, v25.4s
	add	v17.4s, v17.4s, v30.4s
	eor	v6.16b, v6.16b, v18.16b
	orr	v2.16b, v2.16b, v11.16b
	add	v19.4s, v19.4s, v27.4s
	eor	v7.16b, v7.16b, v20.16b
	add	v17.4s, v17.4s, v1.4s
	rev32	v6.8h, v6.8h
	add	v19.4s, v19.4s, v2.4s
	rev32	v7.8h, v7.8h
	eor	v5.16b, v17.16b, v5.16b
	add	v3.4s, v3.4s, v6.4s
	eor	v26.16b, v19.16b, v26.16b
	add	v4.4s, v4.4s, v7.4s
	rev32	v5.8h, v5.8h
	eor	v0.16b, v3.16b, v0.16b
	rev32	v26.8h, v26.8h
	eor	v25.16b, v4.16b, v25.16b
	add	v23.4s, v23.4s, v5.4s
	ushr	v11.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	add	v16.4s, v16.4s, v26.4s
	ushr	v31.4s, v25.4s, #12
	shl	v25.4s, v25.4s, #20
	eor	v1.16b, v23.16b, v1.16b
	orr	v0.16b, v0.16b, v11.16b
	add	v18.4s, v18.4s, v14.4s
	orr	v25.16b, v25.16b, v31.16b
	eor	v2.16b, v16.16b, v2.16b
	ushr	v31.4s, v1.4s, #12
	shl	v1.4s, v1.4s, #20
	add	v20.4s, v20.4s, v28.4s
	add	v18.4s, v18.4s, v0.4s
	mov	v10.16b, v13.16b
	ushr	v11.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	orr	v1.16b, v1.16b, v31.16b
	add	v20.4s, v20.4s, v25.4s
	add	v17.4s, v17.4s, v29.4s
	eor	v6.16b, v18.16b, v6.16b
	orr	v2.16b, v2.16b, v11.16b
	add	v19.4s, v19.4s, v10.4s
	eor	v7.16b, v20.16b, v7.16b
	add	v17.4s, v17.4s, v1.4s
	ushr	v11.4s, v6.4s, #8
	shl	v6.4s, v6.4s, #24
	add	v19.4s, v19.4s, v2.4s
	ushr	v31.4s, v7.4s, #8
	shl	v7.4s, v7.4s, #24
	eor	v5.16b, v17.16b, v5.16b
	orr	v6.16b, v6.16b, v11.16b
	eor	v26.16b, v19.16b, v26.16b
	orr	v7.16b, v7.16b, v31.16b
	ushr	v31.4s, v5.4s, #8
	shl	v5.4s, v5.4s, #24
	add	v3.4s, v6.4s, v3.4s
	ushr	v11.4s, v26.4s, #8
	shl	v26.4s, v26.4s, #24
	add	v4.4s, v7.4s, v4.4s
	orr	v5.16b, v5.16b, v31.16b
	eor	v0.16b, v3.16b, v0.16b
	mov	v22.16b, v8.16b
	ldp	q8, q28, [sp, #240]
	orr	v26.16b, v26.16b, v11.16b
	eor	v25.16b, v4.16b, v25.16b
	add	v23.4s, v5.4s, v23.4s
	ushr	v11.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	add	v16.4s, v26.4s, v16.4s
	ushr	v31.4s, v25.4s, #7
	shl	v25.4s, v25.4s, #25
	eor	v1.16b, v23.16b, v1.16b
	orr	v0.16b, v0.16b, v11.16b
	add	v20.4s, v20.4s, v28.4s
	orr	v25.16b, v25.16b, v31.16b
	eor	v2.16b, v16.16b, v2.16b
	ushr	v31.4s, v1.4s, #7
	shl	v1.4s, v1.4s, #25
	add	v20.4s, v20.4s, v0.4s
	add	v19.4s, v19.4s, v15.4s
	ushr	v11.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	orr	v1.16b, v1.16b, v31.16b
	add	v18.4s, v18.4s, v8.4s
	eor	v26.16b, v20.16b, v26.16b
	add	v19.4s, v19.4s, v25.4s
	orr	v2.16b, v2.16b, v11.16b
	add	v17.4s, v17.4s, v22.4s
	ldur	q22, [x29, #-256]
	add	v18.4s, v18.4s, v1.4s
	rev32	v26.8h, v26.8h
	eor	v5.16b, v19.16b, v5.16b
	add	v17.4s, v17.4s, v2.4s
	eor	v7.16b, v18.16b, v7.16b
	add	v23.4s, v23.4s, v26.4s
	rev32	v5.8h, v5.8h
	eor	v6.16b, v17.16b, v6.16b
	rev32	v7.8h, v7.8h
	eor	v0.16b, v23.16b, v0.16b
	add	v3.4s, v3.4s, v5.4s
	rev32	v6.8h, v6.8h
	add	v16.4s, v16.4s, v7.4s
	ushr	v31.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	eor	v25.16b, v3.16b, v25.16b
	add	v4.4s, v4.4s, v6.4s
	eor	v1.16b, v16.16b, v1.16b
	orr	v0.16b, v0.16b, v31.16b
	ushr	v31.4s, v25.4s, #12
	shl	v25.4s, v25.4s, #20
	add	v20.4s, v20.4s, v9.4s
	mov	v13.16b, v12.16b
	mov	v12.16b, v27.16b
	mov	v27.16b, v9.16b
	ldur	q9, [x29, #-192]
	mov	v21.16b, v15.16b
	ldr	q15, [sp, #224]
	ushr	v11.4s, v1.4s, #12
	ldur	q21, [x29, #-224]
	shl	v1.4s, v1.4s, #20
	eor	v2.16b, v4.16b, v2.16b
	orr	v25.16b, v25.16b, v31.16b
	add	v19.4s, v19.4s, v9.4s
	add	v20.4s, v20.4s, v0.4s
	orr	v1.16b, v1.16b, v11.16b
	ushr	v11.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	add	v18.4s, v18.4s, v21.4s
	add	v19.4s, v19.4s, v25.4s
	eor	v26.16b, v20.16b, v26.16b
	orr	v2.16b, v2.16b, v11.16b
	add	v17.4s, v17.4s, v15.4s
	add	v18.4s, v18.4s, v1.4s
	eor	v5.16b, v19.16b, v5.16b
	ushr	v31.4s, v26.4s, #8
	shl	v26.4s, v26.4s, #24
	add	v17.4s, v17.4s, v2.4s
	ushr	v11.4s, v5.4s, #8
	shl	v5.4s, v5.4s, #24
	eor	v7.16b, v18.16b, v7.16b
	orr	v26.16b, v26.16b, v31.16b
	eor	v6.16b, v17.16b, v6.16b
	orr	v5.16b, v5.16b, v11.16b
	ushr	v31.4s, v7.4s, #8
	shl	v7.4s, v7.4s, #24
	add	v23.4s, v26.4s, v23.4s
	ushr	v11.4s, v6.4s, #8
	shl	v6.4s, v6.4s, #24
	orr	v7.16b, v7.16b, v31.16b
	add	v3.4s, v5.4s, v3.4s
	eor	v0.16b, v23.16b, v0.16b
	orr	v6.16b, v6.16b, v11.16b
	add	v16.4s, v7.4s, v16.4s
	eor	v25.16b, v3.16b, v25.16b
	ushr	v31.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	add	v4.4s, v6.4s, v4.4s
	ushr	v11.4s, v25.4s, #7
	shl	v25.4s, v25.4s, #25
	eor	v1.16b, v16.16b, v1.16b
	orr	v0.16b, v0.16b, v31.16b
	add	v18.4s, v18.4s, v14.4s
	eor	v2.16b, v4.16b, v2.16b
	orr	v25.16b, v25.16b, v11.16b
	ushr	v31.4s, v1.4s, #7
	shl	v1.4s, v1.4s, #25
	add	v20.4s, v20.4s, v24.4s
	add	v18.4s, v18.4s, v0.4s
	ushr	v11.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	orr	v1.16b, v1.16b, v31.16b
	add	v20.4s, v20.4s, v25.4s
	add	v17.4s, v17.4s, v10.4s
	eor	v6.16b, v6.16b, v18.16b
	orr	v2.16b, v2.16b, v11.16b
	add	v19.4s, v19.4s, v30.4s
	eor	v7.16b, v7.16b, v20.16b
	add	v17.4s, v17.4s, v1.4s
	rev32	v6.8h, v6.8h
	add	v19.4s, v19.4s, v2.4s
	rev32	v7.8h, v7.8h
	eor	v5.16b, v17.16b, v5.16b
	add	v3.4s, v3.4s, v6.4s
	eor	v26.16b, v19.16b, v26.16b
	add	v4.4s, v4.4s, v7.4s
	rev32	v5.8h, v5.8h
	eor	v0.16b, v3.16b, v0.16b
	rev32	v26.8h, v26.8h
	eor	v25.16b, v4.16b, v25.16b
	add	v23.4s, v23.4s, v5.4s
	ushr	v11.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	add	v16.4s, v16.4s, v26.4s
	ushr	v31.4s, v25.4s, #12
	shl	v25.4s, v25.4s, #20
	eor	v1.16b, v23.16b, v1.16b
	orr	v0.16b, v0.16b, v11.16b
	add	v18.4s, v18.4s, v8.4s
	orr	v25.16b, v25.16b, v31.16b
	eor	v2.16b, v16.16b, v2.16b
	ushr	v31.4s, v1.4s, #12
	shl	v1.4s, v1.4s, #20
	add	v20.4s, v20.4s, v12.4s
	add	v18.4s, v18.4s, v0.4s
	ushr	v11.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	orr	v1.16b, v1.16b, v31.16b
	add	v20.4s, v20.4s, v25.4s
	add	v17.4s, v17.4s, v13.4s
	ldr	q13, [sp, #160]
	eor	v6.16b, v18.16b, v6.16b
	orr	v2.16b, v2.16b, v11.16b
	add	v19.4s, v19.4s, v15.4s
	eor	v7.16b, v20.16b, v7.16b
	add	v17.4s, v17.4s, v1.4s
	ushr	v11.4s, v6.4s, #8
	shl	v6.4s, v6.4s, #24
	add	v19.4s, v19.4s, v2.4s
	ushr	v31.4s, v7.4s, #8
	shl	v7.4s, v7.4s, #24
	eor	v5.16b, v17.16b, v5.16b
	orr	v6.16b, v6.16b, v11.16b
	eor	v26.16b, v19.16b, v26.16b
	orr	v7.16b, v7.16b, v31.16b
	ushr	v31.4s, v5.4s, #8
	shl	v5.4s, v5.4s, #24
	add	v3.4s, v6.4s, v3.4s
	ushr	v11.4s, v26.4s, #8
	shl	v26.4s, v26.4s, #24
	add	v4.4s, v7.4s, v4.4s
	orr	v5.16b, v5.16b, v31.16b
	eor	v0.16b, v3.16b, v0.16b
	orr	v26.16b, v26.16b, v11.16b
	eor	v25.16b, v4.16b, v25.16b
	add	v23.4s, v5.4s, v23.4s
	ushr	v11.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	add	v16.4s, v26.4s, v16.4s
	ushr	v31.4s, v25.4s, #7
	shl	v25.4s, v25.4s, #25
	eor	v1.16b, v23.16b, v1.16b
	orr	v0.16b, v0.16b, v11.16b
	add	v20.4s, v20.4s, v22.4s
	orr	v25.16b, v25.16b, v31.16b
	eor	v2.16b, v16.16b, v2.16b
	ushr	v31.4s, v1.4s, #7
	shl	v1.4s, v1.4s, #25
	add	v20.4s, v20.4s, v0.4s
	add	v19.4s, v19.4s, v9.4s
	mov	v29.16b, v14.16b
	ldr	q14, [sp, #128]
	ushr	v11.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	orr	v1.16b, v1.16b, v31.16b
	add	v18.4s, v18.4s, v14.4s
	eor	v26.16b, v20.16b, v26.16b
	add	v19.4s, v19.4s, v25.4s
	orr	v2.16b, v2.16b, v11.16b
	add	v17.4s, v17.4s, v27.4s
	add	v18.4s, v18.4s, v1.4s
	rev32	v26.8h, v26.8h
	eor	v5.16b, v19.16b, v5.16b
	add	v17.4s, v17.4s, v2.4s
	eor	v7.16b, v18.16b, v7.16b
	add	v23.4s, v23.4s, v26.4s
	rev32	v5.8h, v5.8h
	eor	v6.16b, v17.16b, v6.16b
	rev32	v7.8h, v7.8h
	eor	v0.16b, v23.16b, v0.16b
	add	v3.4s, v3.4s, v5.4s
	rev32	v6.8h, v6.8h
	add	v16.4s, v16.4s, v7.4s
	ushr	v31.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	eor	v25.16b, v3.16b, v25.16b
	add	v4.4s, v4.4s, v6.4s
	eor	v1.16b, v16.16b, v1.16b
	orr	v0.16b, v0.16b, v31.16b
	ushr	v31.4s, v25.4s, #12
	shl	v25.4s, v25.4s, #20
	add	v20.4s, v20.4s, v21.4s
	ushr	v11.4s, v1.4s, #12
	shl	v1.4s, v1.4s, #20
	eor	v2.16b, v4.16b, v2.16b
	orr	v25.16b, v25.16b, v31.16b
	add	v19.4s, v19.4s, v28.4s
	add	v20.4s, v20.4s, v0.4s
	mov	v12.16b, v27.16b
	ldur	q27, [x29, #-208]
	orr	v1.16b, v1.16b, v11.16b
	ushr	v11.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	add	v18.4s, v18.4s, v27.4s
	add	v19.4s, v19.4s, v25.4s
	eor	v26.16b, v20.16b, v26.16b
	orr	v2.16b, v2.16b, v11.16b
	add	v17.4s, v17.4s, v13.4s
	add	v18.4s, v18.4s, v1.4s
	eor	v5.16b, v19.16b, v5.16b
	ushr	v31.4s, v26.4s, #8
	shl	v26.4s, v26.4s, #24
	add	v17.4s, v17.4s, v2.4s
	ushr	v11.4s, v5.4s, #8
	shl	v5.4s, v5.4s, #24
	eor	v7.16b, v18.16b, v7.16b
	orr	v26.16b, v26.16b, v31.16b
	eor	v6.16b, v17.16b, v6.16b
	orr	v5.16b, v5.16b, v11.16b
	ushr	v31.4s, v7.4s, #8
	shl	v7.4s, v7.4s, #24
	add	v23.4s, v26.4s, v23.4s
	ushr	v11.4s, v6.4s, #8
	shl	v6.4s, v6.4s, #24
	orr	v7.16b, v7.16b, v31.16b
	add	v3.4s, v5.4s, v3.4s
	eor	v0.16b, v23.16b, v0.16b
	orr	v6.16b, v6.16b, v11.16b
	add	v16.4s, v7.4s, v16.4s
	eor	v25.16b, v3.16b, v25.16b
	ushr	v31.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	add	v4.4s, v6.4s, v4.4s
	ushr	v11.4s, v25.4s, #7
	shl	v25.4s, v25.4s, #25
	eor	v1.16b, v16.16b, v1.16b
	orr	v0.16b, v0.16b, v31.16b
	add	v18.4s, v18.4s, v8.4s
	eor	v2.16b, v4.16b, v2.16b
	orr	v25.16b, v25.16b, v11.16b
	ushr	v31.4s, v1.4s, #7
	shl	v1.4s, v1.4s, #25
	add	v20.4s, v20.4s, v29.4s
	add	v18.4s, v18.4s, v0.4s
	ushr	v11.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	orr	v1.16b, v1.16b, v31.16b
	add	v20.4s, v20.4s, v25.4s
	add	v17.4s, v17.4s, v15.4s
	eor	v6.16b, v6.16b, v18.16b
	orr	v2.16b, v2.16b, v11.16b
	add	v19.4s, v19.4s, v10.4s
	eor	v7.16b, v7.16b, v20.16b
	add	v17.4s, v17.4s, v1.4s
	rev32	v6.8h, v6.8h
	add	v19.4s, v19.4s, v2.4s
	rev32	v7.8h, v7.8h
	eor	v5.16b, v17.16b, v5.16b
	add	v3.4s, v3.4s, v6.4s
	eor	v26.16b, v19.16b, v26.16b
	add	v4.4s, v4.4s, v7.4s
	rev32	v5.8h, v5.8h
	eor	v0.16b, v3.16b, v0.16b
	rev32	v26.8h, v26.8h
	eor	v25.16b, v4.16b, v25.16b
	add	v23.4s, v23.4s, v5.4s
	ushr	v11.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	add	v16.4s, v16.4s, v26.4s
	ushr	v31.4s, v25.4s, #12
	shl	v25.4s, v25.4s, #20
	eor	v1.16b, v23.16b, v1.16b
	orr	v0.16b, v0.16b, v11.16b
	add	v18.4s, v18.4s, v14.4s
	mov	v30.16b, v29.16b
	mov	v29.16b, v15.16b
	ldr	q15, [sp, #144]
	orr	v25.16b, v25.16b, v31.16b
	eor	v2.16b, v16.16b, v2.16b
	ushr	v31.4s, v1.4s, #12
	shl	v1.4s, v1.4s, #20
	add	v20.4s, v20.4s, v15.4s
	add	v18.4s, v18.4s, v0.4s
	ushr	v11.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	orr	v1.16b, v1.16b, v31.16b
	add	v20.4s, v20.4s, v25.4s
	add	v17.4s, v17.4s, v24.4s
	eor	v6.16b, v18.16b, v6.16b
	orr	v2.16b, v2.16b, v11.16b
	add	v19.4s, v19.4s, v13.4s
	eor	v7.16b, v20.16b, v7.16b
	add	v17.4s, v17.4s, v1.4s
	ushr	v11.4s, v6.4s, #8
	shl	v6.4s, v6.4s, #24
	add	v19.4s, v19.4s, v2.4s
	ushr	v31.4s, v7.4s, #8
	shl	v7.4s, v7.4s, #24
	eor	v5.16b, v17.16b, v5.16b
	orr	v6.16b, v6.16b, v11.16b
	eor	v26.16b, v19.16b, v26.16b
	orr	v7.16b, v7.16b, v31.16b
	ushr	v31.4s, v5.4s, #8
	shl	v5.4s, v5.4s, #24
	add	v3.4s, v6.4s, v3.4s
	ushr	v11.4s, v26.4s, #8
	shl	v26.4s, v26.4s, #24
	add	v4.4s, v7.4s, v4.4s
	orr	v5.16b, v5.16b, v31.16b
	eor	v0.16b, v3.16b, v0.16b
	orr	v26.16b, v26.16b, v11.16b
	eor	v25.16b, v4.16b, v25.16b
	add	v23.4s, v5.4s, v23.4s
	ushr	v11.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	mov	v9.16b, v28.16b
	mov	v28.16b, v10.16b
	ldr	q10, [sp, #176]
	add	v16.4s, v26.4s, v16.4s
	ushr	v31.4s, v25.4s, #7
	shl	v25.4s, v25.4s, #25
	eor	v1.16b, v23.16b, v1.16b
	orr	v0.16b, v0.16b, v11.16b
	add	v20.4s, v20.4s, v10.4s
	orr	v25.16b, v25.16b, v31.16b
	eor	v2.16b, v16.16b, v2.16b
	ushr	v31.4s, v1.4s, #7
	shl	v1.4s, v1.4s, #25
	add	v20.4s, v20.4s, v0.4s
	add	v19.4s, v19.4s, v9.4s
	ushr	v11.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	orr	v1.16b, v1.16b, v31.16b
	add	v18.4s, v18.4s, v12.4s
	eor	v26.16b, v20.16b, v26.16b
	add	v19.4s, v19.4s, v25.4s
	orr	v2.16b, v2.16b, v11.16b
	add	v17.4s, v17.4s, v21.4s
	add	v18.4s, v18.4s, v1.4s
	rev32	v26.8h, v26.8h
	eor	v5.16b, v19.16b, v5.16b
	add	v17.4s, v17.4s, v2.4s
	eor	v7.16b, v18.16b, v7.16b
	add	v23.4s, v23.4s, v26.4s
	rev32	v5.8h, v5.8h
	eor	v6.16b, v17.16b, v6.16b
	rev32	v7.8h, v7.8h
	eor	v0.16b, v23.16b, v0.16b
	add	v3.4s, v3.4s, v5.4s
	rev32	v6.8h, v6.8h
	add	v16.4s, v16.4s, v7.4s
	ushr	v31.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	eor	v25.16b, v3.16b, v25.16b
	add	v4.4s, v4.4s, v6.4s
	eor	v1.16b, v16.16b, v1.16b
	orr	v0.16b, v0.16b, v31.16b
	ushr	v31.4s, v25.4s, #12
	shl	v25.4s, v25.4s, #20
	ushr	v11.4s, v1.4s, #12
	shl	v1.4s, v1.4s, #20
	eor	v2.16b, v4.16b, v2.16b
	add	v20.4s, v20.4s, v27.4s
	orr	v25.16b, v25.16b, v31.16b
	add	v19.4s, v19.4s, v22.4s
	mov	v9.16b, v22.16b
	ldur	q22, [x29, #-240]
	orr	v1.16b, v1.16b, v11.16b
	ushr	v11.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	add	v20.4s, v20.4s, v0.4s
	add	v18.4s, v18.4s, v22.4s
	add	v19.4s, v19.4s, v25.4s
	mov	v24.16b, v21.16b
	ldur	q21, [x29, #-192]
	orr	v2.16b, v2.16b, v11.16b
	eor	v26.16b, v20.16b, v26.16b
	add	v17.4s, v17.4s, v21.4s
	add	v18.4s, v18.4s, v1.4s
	eor	v5.16b, v19.16b, v5.16b
	ushr	v31.4s, v26.4s, #8
	add	v17.4s, v17.4s, v2.4s
	shl	v26.4s, v26.4s, #24
	ushr	v11.4s, v5.4s, #8
	shl	v5.4s, v5.4s, #24
	eor	v7.16b, v18.16b, v7.16b
	orr	v26.16b, v26.16b, v31.16b
	eor	v6.16b, v17.16b, v6.16b
	orr	v5.16b, v5.16b, v11.16b
	ushr	v31.4s, v7.4s, #8
	shl	v7.4s, v7.4s, #24
	ushr	v11.4s, v6.4s, #8
	shl	v6.4s, v6.4s, #24
	add	v23.4s, v26.4s, v23.4s
	orr	v7.16b, v7.16b, v31.16b
	add	v3.4s, v5.4s, v3.4s
	orr	v6.16b, v6.16b, v11.16b
	eor	v0.16b, v23.16b, v0.16b
	add	v16.4s, v7.4s, v16.4s
	eor	v25.16b, v3.16b, v25.16b
	add	v4.4s, v6.4s, v4.4s
	ushr	v31.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	ushr	v11.4s, v25.4s, #7
	shl	v25.4s, v25.4s, #25
	eor	v1.16b, v16.16b, v1.16b
	orr	v0.16b, v0.16b, v31.16b
	eor	v2.16b, v4.16b, v2.16b
	orr	v25.16b, v25.16b, v11.16b
	ushr	v31.4s, v1.4s, #7
	shl	v1.4s, v1.4s, #25
	add	v20.4s, v20.4s, v8.4s
	add	v18.4s, v18.4s, v14.4s
	ushr	v11.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	orr	v1.16b, v1.16b, v31.16b
	add	v20.4s, v20.4s, v25.4s
	add	v17.4s, v17.4s, v13.4s
	add	v18.4s, v18.4s, v0.4s
	orr	v2.16b, v2.16b, v11.16b
	add	v19.4s, v19.4s, v29.4s
	eor	v7.16b, v7.16b, v20.16b
	add	v17.4s, v17.4s, v1.4s
	eor	v6.16b, v6.16b, v18.16b
	add	v19.4s, v19.4s, v2.4s
	rev32	v7.8h, v7.8h
	eor	v5.16b, v17.16b, v5.16b
	rev32	v6.8h, v6.8h
	eor	v26.16b, v19.16b, v26.16b
	add	v4.4s, v4.4s, v7.4s
	rev32	v5.8h, v5.8h
	add	v3.4s, v3.4s, v6.4s
	rev32	v26.8h, v26.8h
	eor	v25.16b, v4.16b, v25.16b
	add	v23.4s, v23.4s, v5.4s
	eor	v0.16b, v3.16b, v0.16b
	add	v16.4s, v16.4s, v26.4s
	ushr	v31.4s, v25.4s, #12
	shl	v25.4s, v25.4s, #20
	ushr	v11.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	eor	v1.16b, v23.16b, v1.16b
	orr	v25.16b, v25.16b, v31.16b
	eor	v2.16b, v16.16b, v2.16b
	orr	v0.16b, v0.16b, v11.16b
	ushr	v31.4s, v1.4s, #12
	shl	v1.4s, v1.4s, #20
	add	v20.4s, v20.4s, v28.4s
	add	v18.4s, v18.4s, v12.4s
	ushr	v11.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	orr	v1.16b, v1.16b, v31.16b
	add	v20.4s, v20.4s, v25.4s
	add	v17.4s, v17.4s, v30.4s
	add	v18.4s, v18.4s, v0.4s
	orr	v2.16b, v2.16b, v11.16b
	add	v19.4s, v19.4s, v21.4s
	eor	v7.16b, v20.16b, v7.16b
	add	v17.4s, v17.4s, v1.4s
	eor	v6.16b, v18.16b, v6.16b
	add	v19.4s, v19.4s, v2.4s
	ushr	v31.4s, v7.4s, #8
	shl	v7.4s, v7.4s, #24
	ushr	v11.4s, v6.4s, #8
	shl	v6.4s, v6.4s, #24
	eor	v5.16b, v17.16b, v5.16b
	orr	v7.16b, v7.16b, v31.16b
	eor	v26.16b, v19.16b, v26.16b
	orr	v6.16b, v6.16b, v11.16b
	ushr	v31.4s, v5.4s, #8
	shl	v5.4s, v5.4s, #24
	ushr	v11.4s, v26.4s, #8
	shl	v26.4s, v26.4s, #24
	add	v4.4s, v7.4s, v4.4s
	orr	v5.16b, v5.16b, v31.16b
	add	v3.4s, v6.4s, v3.4s
	orr	v26.16b, v26.16b, v11.16b
	eor	v25.16b, v4.16b, v25.16b
	add	v23.4s, v5.4s, v23.4s
	eor	v0.16b, v3.16b, v0.16b
	add	v16.4s, v26.4s, v16.4s
	ushr	v31.4s, v25.4s, #7
	shl	v25.4s, v25.4s, #25
	ushr	v11.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	eor	v1.16b, v23.16b, v1.16b
	orr	v25.16b, v25.16b, v31.16b
	eor	v2.16b, v16.16b, v2.16b
	orr	v0.16b, v0.16b, v11.16b
	ushr	v31.4s, v1.4s, #7
	shl	v1.4s, v1.4s, #25
	add	v20.4s, v20.4s, v15.4s
	ushr	v11.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	orr	v1.16b, v1.16b, v31.16b
	add	v18.4s, v18.4s, v24.4s
	add	v20.4s, v20.4s, v0.4s
	add	v19.4s, v19.4s, v9.4s
	mov	v8.16b, v13.16b
	ldur	q13, [x29, #-208]
	orr	v2.16b, v2.16b, v11.16b
	add	v18.4s, v18.4s, v1.4s
	add	v17.4s, v17.4s, v13.4s
	eor	v26.16b, v20.16b, v26.16b
	add	v19.4s, v19.4s, v25.4s
	eor	v7.16b, v18.16b, v7.16b
	add	v17.4s, v17.4s, v2.4s
	rev32	v26.8h, v26.8h
	eor	v5.16b, v19.16b, v5.16b
	rev32	v7.8h, v7.8h
	eor	v6.16b, v17.16b, v6.16b
	add	v23.4s, v23.4s, v26.4s
	rev32	v5.8h, v5.8h
	add	v16.4s, v16.4s, v7.4s
	rev32	v6.8h, v6.8h
	eor	v0.16b, v23.16b, v0.16b
	add	v3.4s, v3.4s, v5.4s
	eor	v1.16b, v16.16b, v1.16b
	add	v4.4s, v4.4s, v6.4s
	ushr	v31.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	eor	v25.16b, v3.16b, v25.16b
	ushr	v11.4s, v1.4s, #12
	shl	v1.4s, v1.4s, #20
	orr	v0.16b, v0.16b, v31.16b
	eor	v2.16b, v4.16b, v2.16b
	ushr	v31.4s, v25.4s, #12
	shl	v25.4s, v25.4s, #20
	orr	v1.16b, v1.16b, v11.16b
	ushr	v11.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	add	v20.4s, v20.4s, v22.4s
	orr	v25.16b, v25.16b, v31.16b
	add	v19.4s, v19.4s, v10.4s
	mov	v27.16b, v12.16b
	mov	v12.16b, v30.16b
	mov	v29.16b, v21.16b
	mov	v21.16b, v24.16b
	ldr	q24, [sp, #192]
	mov	v30.16b, v22.16b
	ldr	q22, [sp, #256]
	orr	v2.16b, v2.16b, v11.16b
	add	v20.4s, v20.4s, v0.4s
	add	v18.4s, v18.4s, v24.4s
	add	v19.4s, v19.4s, v25.4s
	add	v17.4s, v17.4s, v22.4s
	eor	v26.16b, v20.16b, v26.16b
	add	v18.4s, v18.4s, v1.4s
	eor	v5.16b, v19.16b, v5.16b
	add	v17.4s, v17.4s, v2.4s
	ushr	v31.4s, v26.4s, #8
	shl	v26.4s, v26.4s, #24
	ushr	v11.4s, v5.4s, #8
	shl	v5.4s, v5.4s, #24
	eor	v7.16b, v18.16b, v7.16b
	eor	v6.16b, v17.16b, v6.16b
	orr	v26.16b, v26.16b, v31.16b
	orr	v5.16b, v5.16b, v11.16b
	ushr	v31.4s, v7.4s, #8
	shl	v7.4s, v7.4s, #24
	ushr	v11.4s, v6.4s, #8
	shl	v6.4s, v6.4s, #24
	add	v23.4s, v26.4s, v23.4s
	orr	v7.16b, v7.16b, v31.16b
	add	v3.4s, v5.4s, v3.4s
	orr	v6.16b, v6.16b, v11.16b
	eor	v0.16b, v23.16b, v0.16b
	add	v16.4s, v7.4s, v16.4s
	eor	v25.16b, v3.16b, v25.16b
	add	v4.4s, v6.4s, v4.4s
	ushr	v31.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	ushr	v11.4s, v25.4s, #7
	shl	v25.4s, v25.4s, #25
	eor	v1.16b, v16.16b, v1.16b
	eor	v2.16b, v4.16b, v2.16b
	orr	v0.16b, v0.16b, v31.16b
	orr	v25.16b, v25.16b, v11.16b
	ushr	v31.4s, v1.4s, #7
	shl	v1.4s, v1.4s, #25
	ushr	v11.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	add	v20.4s, v20.4s, v14.4s
	add	v18.4s, v18.4s, v27.4s
	ldr	q27, [sp, #224]
	orr	v1.16b, v1.16b, v31.16b
	orr	v2.16b, v2.16b, v11.16b
	add	v20.4s, v20.4s, v25.4s
	add	v17.4s, v17.4s, v29.4s
	add	v18.4s, v18.4s, v0.4s
	add	v19.4s, v19.4s, v8.4s
	eor	v7.16b, v7.16b, v20.16b
	add	v17.4s, v17.4s, v1.4s
	eor	v6.16b, v6.16b, v18.16b
	add	v19.4s, v19.4s, v2.4s
	rev32	v7.8h, v7.8h
	eor	v5.16b, v17.16b, v5.16b
	rev32	v6.8h, v6.8h
	eor	v26.16b, v19.16b, v26.16b
	add	v4.4s, v4.4s, v7.4s
	rev32	v5.8h, v5.8h
	add	v3.4s, v3.4s, v6.4s
	rev32	v26.8h, v26.8h
	eor	v25.16b, v4.16b, v25.16b
	add	v23.4s, v23.4s, v5.4s
	eor	v0.16b, v3.16b, v0.16b
	add	v16.4s, v16.4s, v26.4s
	ushr	v29.4s, v25.4s, #12
	shl	v25.4s, v25.4s, #20
	ushr	v31.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	eor	v1.16b, v23.16b, v1.16b
	eor	v2.16b, v16.16b, v2.16b
	orr	v25.16b, v25.16b, v29.16b
	orr	v0.16b, v0.16b, v31.16b
	ushr	v29.4s, v1.4s, #12
	shl	v1.4s, v1.4s, #20
	ushr	v31.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	add	v18.4s, v18.4s, v21.4s
	ldr	q21, [sp, #240]
	add	v20.4s, v20.4s, v27.4s
	prfm	pldl1keep, [x17, #256]
	orr	v1.16b, v1.16b, v29.16b
	prfm	pldl1keep, [x21, #256]
	orr	v2.16b, v2.16b, v31.16b
	prfm	pldl1keep, [x16, #256]
	add	v18.4s, v18.4s, v0.4s
	prfm	pldl1keep, [x6, #256]
	add	v17.4s, v17.4s, v21.4s
	add	v19.4s, v19.4s, v22.4s
	add	v20.4s, v20.4s, v25.4s
	eor	v6.16b, v18.16b, v6.16b
	add	v17.4s, v17.4s, v1.4s
	add	v19.4s, v19.4s, v2.4s
	eor	v7.16b, v20.16b, v7.16b
	ushr	v22.4s, v6.4s, #8
	shl	v6.4s, v6.4s, #24
	eor	v5.16b, v17.16b, v5.16b
	eor	v26.16b, v19.16b, v26.16b
	ushr	v21.4s, v7.4s, #8
	shl	v7.4s, v7.4s, #24
	orr	v6.16b, v6.16b, v22.16b
	ushr	v22.4s, v5.4s, #8
	shl	v5.4s, v5.4s, #24
	ushr	v29.4s, v26.4s, #8
	shl	v26.4s, v26.4s, #24
	orr	v7.16b, v7.16b, v21.16b
	orr	v5.16b, v5.16b, v22.16b
	add	v3.4s, v6.4s, v3.4s
	orr	v21.16b, v26.16b, v29.16b
	add	v4.4s, v7.4s, v4.4s
	add	v22.4s, v5.4s, v23.4s
	eor	v0.16b, v3.16b, v0.16b
	add	v16.4s, v21.4s, v16.4s
	eor	v23.16b, v4.16b, v25.16b
	eor	v1.16b, v22.16b, v1.16b
	ushr	v25.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	eor	v2.16b, v16.16b, v2.16b
	ushr	v26.4s, v23.4s, #7
	shl	v23.4s, v23.4s, #25
	orr	v0.16b, v0.16b, v25.16b
	ushr	v25.4s, v1.4s, #7
	shl	v1.4s, v1.4s, #25
	ushr	v29.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	add	v20.4s, v20.4s, v28.4s
	orr	v23.16b, v23.16b, v26.16b
	orr	v1.16b, v1.16b, v25.16b
	orr	v2.16b, v2.16b, v29.16b
	add	v20.4s, v20.4s, v0.4s
	add	v18.4s, v18.4s, v13.4s
	add	v17.4s, v17.4s, v30.4s
	add	v19.4s, v19.4s, v10.4s
	eor	v21.16b, v20.16b, v21.16b
	add	v18.4s, v18.4s, v1.4s
	add	v17.4s, v17.4s, v2.4s
	add	v19.4s, v19.4s, v23.4s
	rev32	v21.8h, v21.8h
	eor	v7.16b, v18.16b, v7.16b
	eor	v6.16b, v17.16b, v6.16b
	eor	v5.16b, v19.16b, v5.16b
	add	v22.4s, v22.4s, v21.4s
	rev32	v7.8h, v7.8h
	rev32	v6.8h, v6.8h
	rev32	v5.8h, v5.8h
	eor	v0.16b, v22.16b, v0.16b
	add	v16.4s, v16.4s, v7.4s
	add	v4.4s, v4.4s, v6.4s
	add	v3.4s, v3.4s, v5.4s
	ushr	v25.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	eor	v1.16b, v16.16b, v1.16b
	eor	v2.16b, v4.16b, v2.16b
	eor	v23.16b, v3.16b, v23.16b
	orr	v0.16b, v0.16b, v25.16b
	ushr	v25.4s, v1.4s, #12
	shl	v1.4s, v1.4s, #20
	ushr	v26.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	ushr	v27.4s, v23.4s, #12
	shl	v23.4s, v23.4s, #20
	orr	v1.16b, v1.16b, v25.16b
	add	v20.4s, v20.4s, v24.4s
	orr	v2.16b, v2.16b, v26.16b
	orr	v23.16b, v23.16b, v27.16b
	add	v18.4s, v18.4s, v12.4s
	add	v17.4s, v17.4s, v9.4s
	add	v19.4s, v19.4s, v15.4s
	add	v20.4s, v20.4s, v0.4s
	add	v18.4s, v18.4s, v1.4s
	add	v17.4s, v17.4s, v2.4s
	add	v19.4s, v19.4s, v23.4s
	eor	v21.16b, v20.16b, v21.16b
	eor	v7.16b, v18.16b, v7.16b
	eor	v6.16b, v17.16b, v6.16b
	eor	v5.16b, v19.16b, v5.16b
	ushr	v24.4s, v21.4s, #8
	shl	v21.4s, v21.4s, #24
	ushr	v25.4s, v7.4s, #8
	shl	v7.4s, v7.4s, #24
	ushr	v26.4s, v6.4s, #8
	shl	v6.4s, v6.4s, #24
	ushr	v27.4s, v5.4s, #8
	shl	v5.4s, v5.4s, #24
	orr	v21.16b, v21.16b, v24.16b
	orr	v7.16b, v7.16b, v25.16b
	orr	v6.16b, v6.16b, v26.16b
	orr	v5.16b, v5.16b, v27.16b
	add	v22.4s, v21.4s, v22.4s
	add	v16.4s, v7.4s, v16.4s
	add	v4.4s, v6.4s, v4.4s
	add	v3.4s, v5.4s, v3.4s
	eor	v0.16b, v22.16b, v0.16b
	eor	v1.16b, v16.16b, v1.16b
	eor	v2.16b, v4.16b, v2.16b
	eor	v23.16b, v3.16b, v23.16b
	ushr	v24.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	ushr	v25.4s, v1.4s, #7
	shl	v1.4s, v1.4s, #25
	ushr	v26.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	ushr	v27.4s, v23.4s, #7
	shl	v23.4s, v23.4s, #25
	orr	v0.16b, v0.16b, v24.16b
	orr	v1.16b, v1.16b, v25.16b
	orr	v2.16b, v2.16b, v26.16b
	orr	v23.16b, v23.16b, v27.16b
	movi	v24.4s, #64
	eor	v12.16b, v4.16b, v20.16b
	eor	v31.16b, v18.16b, v3.16b
	eor	v29.16b, v17.16b, v22.16b
	eor	v30.16b, v16.16b, v19.16b
	eor	v28.16b, v7.16b, v23.16b
	eor	v23.16b, v6.16b, v0.16b
	eor	v13.16b, v1.16b, v5.16b
	eor	v25.16b, v2.16b, v21.16b
	cbnz	x15, .LBB3_5
	b	.LBB3_2
.LBB3_6:
	cbz	x24, .LBB3_14
	orr	w8, w7, w19
	and	x22, x5, #0x1
	stur	w8, [x29, #-192]
.LBB3_8:
	ldr	x8, [sp, #40]
	mov	x28, x0
	ldr	x25, [x0]
	mov	x23, x2
	ldur	w5, [x29, #-192]
	ldp	q0, q1, [x8]
	mov	x8, x2
	b	.LBB3_11
.LBB3_9:
	orr	w5, w5, w27
.LBB3_10:
	sub	x0, x29, #144
	sub	x1, x29, #176
	mov	x2, x25
	mov	w3, #64
	mov	x4, x20
	bl	compress_pre
	ldp	q0, q1, [x29, #-144]
	add	x25, x25, #64
	mov	x8, x21
	mov	w5, w19
	ldp	q2, q3, [x29, #-112]
	eor	v0.16b, v2.16b, v0.16b
	eor	v1.16b, v3.16b, v1.16b
.LBB3_11:
	subs	x21, x8, #1
	stp	q0, q1, [x29, #-176]
	b.eq	.LBB3_9
	cbnz	x8, .LBB3_10
	ldp	q1, q0, [x29, #-176]
	mov	x0, x28
	add	x20, x20, x22
	add	x0, x28, #8
	subs	x24, x24, #1
	mov	x2, x23
	stp	q1, q0, [x26], #32
	b.ne	.LBB3_8
.LBB3_14:
	add	sp, sp, #464
	ldp	x20, x19, [sp, #144]
	ldp	x22, x21, [sp, #128]
	ldp	x24, x23, [sp, #112]
	ldp	x26, x25, [sp, #96]
	ldp	x28, x27, [sp, #80]
	ldp	x29, x30, [sp, #64]
	ldp	d9, d8, [sp, #48]
	ldp	d11, d10, [sp, #32]
	ldp	d13, d12, [sp, #16]
	ldp	d15, d14, [sp], #160
	hint	#29
	ret
.Lfunc_end3:
	.size	zfs_blake3_hash_many_sse2, .Lfunc_end3-zfs_blake3_hash_many_sse2
	.cfi_endproc
	.section	".note.GNU-stack","",@progbits
#endif