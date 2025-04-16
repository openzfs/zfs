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
 * Copyright (c) 2019-2022 Samuel Neves
 * Copyright (c) 2022-2023 Tino Reichardt <milky-zfs@mcmilk.de>
 *
 * This is converted assembly: SSE4.1 -> ARMv8-A
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
	.globl	zfs_blake3_compress_in_place_sse41
	.p2align	2
	.type	zfs_blake3_compress_in_place_sse41,@function
zfs_blake3_compress_in_place_sse41:
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
	.size	zfs_blake3_compress_in_place_sse41, .Lfunc_end0-zfs_blake3_compress_in_place_sse41
	.cfi_endproc

	.section	.rodata.cst16,"aM",@progbits,16
	.p2align	4
.LCPI1_0:
	.xword	-4942790177982912921
	.xword	-6534734903820487822
.LCPI1_1:
	.byte	2
	.byte	3
	.byte	0
	.byte	1
	.byte	6
	.byte	7
	.byte	4
	.byte	5
	.byte	10
	.byte	11
	.byte	8
	.byte	9
	.byte	14
	.byte	15
	.byte	12
	.byte	13
.LCPI1_2:
	.byte	1
	.byte	2
	.byte	3
	.byte	0
	.byte	5
	.byte	6
	.byte	7
	.byte	4
	.byte	9
	.byte	10
	.byte	11
	.byte	8
	.byte	13
	.byte	14
	.byte	15
	.byte	12
	.text
	.p2align	2
	.type	compress_pre,@function
compress_pre:
	.cfi_startproc
	hint	#34
	fmov	s1, w3
	movi	d0, #0x0000ff000000ff
	ldr	q2, [x1]
	adrp	x8, .LCPI1_0
	mov	v1.s[1], w5
	str	q2, [x0]
	ldr	q4, [x8, :lo12:.LCPI1_0]
	ldr	q5, [x1, #16]
	adrp	x8, .LCPI1_1
	and	v0.8b, v1.8b, v0.8b
	fmov	d1, x4
	stp	q5, q4, [x0, #16]
	mov	v1.d[1], v0.d[0]
	str	q1, [x0, #48]
	ldp	q6, q7, [x2]
	uzp1	v3.4s, v6.4s, v7.4s
	add	v0.4s, v2.4s, v3.4s
	uzp2	v2.4s, v6.4s, v7.4s
	add	v16.4s, v0.4s, v5.4s
	ldr	q0, [x8, :lo12:.LCPI1_1]
	adrp	x8, .LCPI1_2
	eor	v1.16b, v16.16b, v1.16b
	add	v7.4s, v16.4s, v2.4s
	tbl	v1.16b, { v1.16b }, v0.16b
	add	v4.4s, v1.4s, v4.4s
	eor	v5.16b, v4.16b, v5.16b
	ushr	v6.4s, v5.4s, #12
	shl	v5.4s, v5.4s, #20
	orr	v5.16b, v5.16b, v6.16b
	add	v6.4s, v7.4s, v5.4s
	eor	v7.16b, v1.16b, v6.16b
	ldr	q1, [x8, :lo12:.LCPI1_2]
	add	x8, x2, #32
	tbl	v7.16b, { v7.16b }, v1.16b
	ld2	{ v16.4s, v17.4s }, [x8]
	add	v4.4s, v4.4s, v7.4s
	ext	v7.16b, v7.16b, v7.16b, #8
	add	v6.4s, v6.4s, v16.4s
	eor	v5.16b, v4.16b, v5.16b
	ext	v4.16b, v4.16b, v4.16b, #4
	ext	v16.16b, v16.16b, v16.16b, #12
	ext	v6.16b, v6.16b, v6.16b, #12
	ushr	v18.4s, v5.4s, #7
	shl	v5.4s, v5.4s, #25
	orr	v5.16b, v5.16b, v18.16b
	ext	v18.16b, v17.16b, v17.16b, #12
	add	v6.4s, v6.4s, v5.4s
	mov	v17.16b, v18.16b
	eor	v7.16b, v7.16b, v6.16b
	add	v6.4s, v6.4s, v18.4s
	mov	v17.s[1], v16.s[2]
	tbl	v7.16b, { v7.16b }, v0.16b
	add	v4.4s, v4.4s, v7.4s
	eor	v5.16b, v4.16b, v5.16b
	ushr	v19.4s, v5.4s, #12
	shl	v5.4s, v5.4s, #20
	orr	v5.16b, v5.16b, v19.16b
	uzp1	v19.4s, v3.4s, v3.4s
	add	v6.4s, v6.4s, v5.4s
	ext	v19.16b, v19.16b, v3.16b, #8
	eor	v7.16b, v7.16b, v6.16b
	uzp2	v19.4s, v19.4s, v2.4s
	tbl	v7.16b, { v7.16b }, v1.16b
	add	v6.4s, v6.4s, v19.4s
	add	v4.4s, v4.4s, v7.4s
	ext	v6.16b, v6.16b, v6.16b, #4
	ext	v7.16b, v7.16b, v7.16b, #8
	eor	v5.16b, v4.16b, v5.16b
	ext	v4.16b, v4.16b, v4.16b, #12
	ushr	v20.4s, v5.4s, #7
	shl	v5.4s, v5.4s, #25
	orr	v5.16b, v5.16b, v20.16b
	ext	v20.16b, v3.16b, v3.16b, #12
	add	v6.4s, v6.4s, v5.4s
	ext	v3.16b, v3.16b, v20.16b, #12
	eor	v7.16b, v7.16b, v6.16b
	rev64	v3.4s, v3.4s
	tbl	v7.16b, { v7.16b }, v0.16b
	trn2	v3.4s, v3.4s, v17.4s
	add	v4.4s, v4.4s, v7.4s
	add	v6.4s, v6.4s, v3.4s
	eor	v5.16b, v4.16b, v5.16b
	ushr	v17.4s, v5.4s, #12
	shl	v5.4s, v5.4s, #20
	orr	v5.16b, v5.16b, v17.16b
	zip1	v17.2d, v18.2d, v2.2d
	zip2	v2.4s, v2.4s, v18.4s
	add	v6.4s, v6.4s, v5.4s
	mov	v17.s[3], v16.s[3]
	zip1	v18.4s, v2.4s, v16.4s
	zip1	v2.4s, v16.4s, v2.4s
	eor	v7.16b, v7.16b, v6.16b
	ext	v6.16b, v6.16b, v6.16b, #12
	ext	v16.16b, v2.16b, v18.16b, #8
	tbl	v7.16b, { v7.16b }, v1.16b
	add	v20.4s, v4.4s, v7.4s
	ext	v4.16b, v17.16b, v17.16b, #12
	ext	v7.16b, v7.16b, v7.16b, #8
	eor	v5.16b, v20.16b, v5.16b
	uzp1	v4.4s, v17.4s, v4.4s
	ushr	v17.4s, v5.4s, #7
	shl	v5.4s, v5.4s, #25
	add	v6.4s, v6.4s, v4.4s
	orr	v5.16b, v5.16b, v17.16b
	ext	v17.16b, v20.16b, v20.16b, #4
	add	v6.4s, v6.4s, v5.4s
	eor	v7.16b, v7.16b, v6.16b
	add	v6.4s, v6.4s, v16.4s
	tbl	v7.16b, { v7.16b }, v0.16b
	add	v17.4s, v17.4s, v7.4s
	eor	v5.16b, v17.16b, v5.16b
	ushr	v2.4s, v5.4s, #12
	shl	v5.4s, v5.4s, #20
	orr	v2.16b, v5.16b, v2.16b
	add	v5.4s, v6.4s, v2.4s
	ext	v6.16b, v19.16b, v19.16b, #4
	eor	v7.16b, v7.16b, v5.16b
	uzp1	v18.4s, v6.4s, v6.4s
	tbl	v7.16b, { v7.16b }, v1.16b
	ext	v18.16b, v18.16b, v6.16b, #8
	add	v17.4s, v17.4s, v7.4s
	uzp2	v18.4s, v18.4s, v3.4s
	ext	v7.16b, v7.16b, v7.16b, #8
	eor	v2.16b, v17.16b, v2.16b
	add	v5.4s, v5.4s, v18.4s
	ext	v17.16b, v17.16b, v17.16b, #12
	ushr	v19.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	ext	v5.16b, v5.16b, v5.16b, #4
	orr	v2.16b, v2.16b, v19.16b
	ext	v19.16b, v6.16b, v6.16b, #12
	add	v5.4s, v5.4s, v2.4s
	ext	v6.16b, v6.16b, v19.16b, #12
	mov	v19.16b, v16.16b
	eor	v7.16b, v7.16b, v5.16b
	rev64	v6.4s, v6.4s
	mov	v19.s[1], v4.s[2]
	tbl	v7.16b, { v7.16b }, v0.16b
	add	v17.4s, v17.4s, v7.4s
	eor	v20.16b, v17.16b, v2.16b
	trn2	v2.4s, v6.4s, v19.4s
	ushr	v6.4s, v20.4s, #12
	shl	v19.4s, v20.4s, #20
	add	v5.4s, v5.4s, v2.4s
	orr	v6.16b, v19.16b, v6.16b
	add	v19.4s, v5.4s, v6.4s
	eor	v5.16b, v7.16b, v19.16b
	zip1	v7.2d, v16.2d, v3.2d
	zip2	v3.4s, v3.4s, v16.4s
	tbl	v20.16b, { v5.16b }, v1.16b
	mov	v7.s[3], v4.s[3]
	add	v17.4s, v17.4s, v20.4s
	ext	v5.16b, v7.16b, v7.16b, #12
	eor	v6.16b, v17.16b, v6.16b
	uzp1	v5.4s, v7.4s, v5.4s
	ext	v7.16b, v19.16b, v19.16b, #12
	ext	v17.16b, v17.16b, v17.16b, #4
	ushr	v19.4s, v6.4s, #7
	shl	v6.4s, v6.4s, #25
	add	v7.4s, v7.4s, v5.4s
	orr	v6.16b, v6.16b, v19.16b
	ext	v19.16b, v20.16b, v20.16b, #8
	add	v7.4s, v7.4s, v6.4s
	eor	v19.16b, v19.16b, v7.16b
	tbl	v19.16b, { v19.16b }, v0.16b
	add	v16.4s, v17.4s, v19.4s
	zip1	v17.4s, v3.4s, v4.4s
	zip1	v3.4s, v4.4s, v3.4s
	eor	v4.16b, v16.16b, v6.16b
	ext	v17.16b, v3.16b, v17.16b, #8
	ushr	v3.4s, v4.4s, #12
	shl	v4.4s, v4.4s, #20
	add	v6.4s, v7.4s, v17.4s
	orr	v3.16b, v4.16b, v3.16b
	add	v4.4s, v6.4s, v3.4s
	ext	v6.16b, v18.16b, v18.16b, #4
	eor	v7.16b, v19.16b, v4.16b
	uzp1	v18.4s, v6.4s, v6.4s
	tbl	v7.16b, { v7.16b }, v1.16b
	ext	v18.16b, v18.16b, v6.16b, #8
	add	v16.4s, v16.4s, v7.4s
	uzp2	v18.4s, v18.4s, v2.4s
	ext	v7.16b, v7.16b, v7.16b, #8
	eor	v3.16b, v16.16b, v3.16b
	add	v4.4s, v4.4s, v18.4s
	ext	v16.16b, v16.16b, v16.16b, #12
	ushr	v19.4s, v3.4s, #7
	shl	v3.4s, v3.4s, #25
	ext	v4.16b, v4.16b, v4.16b, #4
	orr	v3.16b, v3.16b, v19.16b
	ext	v19.16b, v6.16b, v6.16b, #12
	add	v4.4s, v4.4s, v3.4s
	ext	v6.16b, v6.16b, v19.16b, #12
	mov	v19.16b, v17.16b
	eor	v7.16b, v7.16b, v4.16b
	rev64	v6.4s, v6.4s
	mov	v19.s[1], v5.s[2]
	tbl	v7.16b, { v7.16b }, v0.16b
	add	v16.4s, v16.4s, v7.4s
	eor	v20.16b, v16.16b, v3.16b
	trn2	v3.4s, v6.4s, v19.4s
	ushr	v6.4s, v20.4s, #12
	shl	v19.4s, v20.4s, #20
	add	v4.4s, v4.4s, v3.4s
	orr	v6.16b, v19.16b, v6.16b
	zip1	v19.2d, v17.2d, v2.2d
	zip2	v2.4s, v2.4s, v17.4s
	add	v4.4s, v4.4s, v6.4s
	mov	v19.s[3], v5.s[3]
	zip1	v17.4s, v2.4s, v5.4s
	zip1	v2.4s, v5.4s, v2.4s
	eor	v7.16b, v7.16b, v4.16b
	ext	v20.16b, v19.16b, v19.16b, #12
	ext	v4.16b, v4.16b, v4.16b, #12
	ext	v2.16b, v2.16b, v17.16b, #8
	tbl	v7.16b, { v7.16b }, v1.16b
	add	v16.4s, v16.4s, v7.4s
	ext	v7.16b, v7.16b, v7.16b, #8
	eor	v21.16b, v16.16b, v6.16b
	uzp1	v6.4s, v19.4s, v20.4s
	ext	v16.16b, v16.16b, v16.16b, #4
	ushr	v19.4s, v21.4s, #7
	shl	v20.4s, v21.4s, #25
	add	v4.4s, v4.4s, v6.4s
	orr	v19.16b, v20.16b, v19.16b
	add	v4.4s, v4.4s, v19.4s
	eor	v7.16b, v7.16b, v4.16b
	add	v4.4s, v4.4s, v2.4s
	tbl	v7.16b, { v7.16b }, v0.16b
	add	v16.4s, v16.4s, v7.4s
	eor	v5.16b, v16.16b, v19.16b
	ushr	v17.4s, v5.4s, #12
	shl	v5.4s, v5.4s, #20
	orr	v5.16b, v5.16b, v17.16b
	ext	v17.16b, v18.16b, v18.16b, #4
	add	v4.4s, v4.4s, v5.4s
	uzp1	v18.4s, v17.4s, v17.4s
	eor	v7.16b, v7.16b, v4.16b
	ext	v18.16b, v18.16b, v17.16b, #8
	tbl	v7.16b, { v7.16b }, v1.16b
	uzp2	v18.4s, v18.4s, v3.4s
	add	v16.4s, v16.4s, v7.4s
	add	v4.4s, v4.4s, v18.4s
	ext	v7.16b, v7.16b, v7.16b, #8
	eor	v5.16b, v16.16b, v5.16b
	ext	v4.16b, v4.16b, v4.16b, #4
	ext	v16.16b, v16.16b, v16.16b, #12
	ushr	v19.4s, v5.4s, #7
	shl	v5.4s, v5.4s, #25
	orr	v5.16b, v5.16b, v19.16b
	add	v19.4s, v4.4s, v5.4s
	eor	v4.16b, v7.16b, v19.16b
	ext	v7.16b, v17.16b, v17.16b, #12
	tbl	v20.16b, { v4.16b }, v0.16b
	ext	v4.16b, v17.16b, v7.16b, #12
	mov	v7.16b, v2.16b
	add	v16.4s, v16.4s, v20.4s
	rev64	v4.4s, v4.4s
	mov	v7.s[1], v6.s[2]
	eor	v5.16b, v16.16b, v5.16b
	trn2	v4.4s, v4.4s, v7.4s
	ushr	v7.4s, v5.4s, #12
	shl	v5.4s, v5.4s, #20
	add	v17.4s, v19.4s, v4.4s
	zip1	v19.2d, v2.2d, v3.2d
	zip2	v2.4s, v3.4s, v2.4s
	orr	v5.16b, v5.16b, v7.16b
	mov	v19.s[3], v6.s[3]
	add	v7.4s, v17.4s, v5.4s
	eor	v17.16b, v20.16b, v7.16b
	ext	v20.16b, v19.16b, v19.16b, #12
	ext	v7.16b, v7.16b, v7.16b, #12
	tbl	v17.16b, { v17.16b }, v1.16b
	add	v16.4s, v16.4s, v17.4s
	ext	v17.16b, v17.16b, v17.16b, #8
	eor	v21.16b, v16.16b, v5.16b
	uzp1	v5.4s, v19.4s, v20.4s
	ext	v16.16b, v16.16b, v16.16b, #4
	ushr	v19.4s, v21.4s, #7
	shl	v20.4s, v21.4s, #25
	add	v7.4s, v7.4s, v5.4s
	orr	v19.16b, v20.16b, v19.16b
	add	v7.4s, v7.4s, v19.4s
	eor	v17.16b, v17.16b, v7.16b
	tbl	v17.16b, { v17.16b }, v0.16b
	add	v3.4s, v16.4s, v17.4s
	zip1	v16.4s, v2.4s, v6.4s
	zip1	v2.4s, v6.4s, v2.4s
	eor	v6.16b, v3.16b, v19.16b
	ext	v16.16b, v2.16b, v16.16b, #8
	ushr	v2.4s, v6.4s, #12
	shl	v6.4s, v6.4s, #20
	add	v7.4s, v7.4s, v16.4s
	orr	v2.16b, v6.16b, v2.16b
	add	v6.4s, v7.4s, v2.4s
	ext	v7.16b, v18.16b, v18.16b, #4
	eor	v17.16b, v17.16b, v6.16b
	uzp1	v18.4s, v7.4s, v7.4s
	tbl	v17.16b, { v17.16b }, v1.16b
	ext	v18.16b, v18.16b, v7.16b, #8
	add	v3.4s, v3.4s, v17.4s
	uzp2	v18.4s, v18.4s, v4.4s
	eor	v2.16b, v3.16b, v2.16b
	add	v6.4s, v6.4s, v18.4s
	ext	v3.16b, v3.16b, v3.16b, #12
	ext	v18.16b, v18.16b, v18.16b, #4
	ushr	v19.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	ext	v6.16b, v6.16b, v6.16b, #4
	orr	v19.16b, v2.16b, v19.16b
	ext	v2.16b, v17.16b, v17.16b, #8
	ext	v17.16b, v7.16b, v7.16b, #12
	add	v6.4s, v6.4s, v19.4s
	eor	v2.16b, v2.16b, v6.16b
	tbl	v20.16b, { v2.16b }, v0.16b
	ext	v2.16b, v7.16b, v17.16b, #12
	mov	v7.16b, v16.16b
	add	v17.4s, v3.4s, v20.4s
	rev64	v3.4s, v2.4s
	mov	v7.s[1], v5.s[2]
	eor	v19.16b, v17.16b, v19.16b
	trn2	v3.4s, v3.4s, v7.4s
	ushr	v21.4s, v19.4s, #12
	shl	v19.4s, v19.4s, #20
	add	v6.4s, v6.4s, v3.4s
	orr	v19.16b, v19.16b, v21.16b
	add	v21.4s, v6.4s, v19.4s
	eor	v6.16b, v20.16b, v21.16b
	zip1	v20.2d, v16.2d, v4.2d
	zip2	v4.4s, v4.4s, v16.4s
	tbl	v22.16b, { v6.16b }, v1.16b
	mov	v20.s[3], v5.s[3]
	add	v17.4s, v17.4s, v22.4s
	ext	v6.16b, v20.16b, v20.16b, #12
	eor	v19.16b, v17.16b, v19.16b
	uzp1	v6.4s, v20.4s, v6.4s
	ext	v20.16b, v21.16b, v21.16b, #12
	ext	v17.16b, v17.16b, v17.16b, #4
	ushr	v21.4s, v19.4s, #7
	shl	v19.4s, v19.4s, #25
	add	v20.4s, v20.4s, v6.4s
	orr	v19.16b, v19.16b, v21.16b
	ext	v21.16b, v22.16b, v22.16b, #8
	add	v20.4s, v20.4s, v19.4s
	eor	v21.16b, v21.16b, v20.16b
	tbl	v21.16b, { v21.16b }, v0.16b
	add	v16.4s, v17.4s, v21.4s
	zip1	v17.4s, v4.4s, v5.4s
	zip1	v4.4s, v5.4s, v4.4s
	eor	v5.16b, v16.16b, v19.16b
	ext	v4.16b, v4.16b, v17.16b, #8
	ushr	v17.4s, v5.4s, #12
	shl	v5.4s, v5.4s, #20
	add	v19.4s, v20.4s, v4.4s
	ext	v20.16b, v18.16b, v18.16b, #8
	zip1	v3.2d, v4.2d, v3.2d
	orr	v5.16b, v5.16b, v17.16b
	zip2	v2.4s, v2.4s, v4.4s
	uzp2	v7.4s, v20.4s, v7.4s
	mov	v3.s[3], v6.s[3]
	add	v17.4s, v19.4s, v5.4s
	ext	v7.16b, v7.16b, v20.16b, #4
	eor	v19.16b, v21.16b, v17.16b
	ext	v17.16b, v17.16b, v17.16b, #4
	tbl	v19.16b, { v19.16b }, v1.16b
	add	v7.4s, v17.4s, v7.4s
	add	v16.4s, v16.4s, v19.4s
	ext	v17.16b, v19.16b, v19.16b, #8
	ext	v19.16b, v18.16b, v18.16b, #12
	eor	v5.16b, v16.16b, v5.16b
	ext	v16.16b, v16.16b, v16.16b, #12
	ext	v18.16b, v18.16b, v19.16b, #12
	mov	v19.16b, v4.16b
	ushr	v20.4s, v5.4s, #7
	shl	v5.4s, v5.4s, #25
	rev64	v18.4s, v18.4s
	mov	v19.s[1], v6.s[2]
	orr	v5.16b, v5.16b, v20.16b
	trn2	v18.4s, v18.4s, v19.4s
	add	v7.4s, v5.4s, v7.4s
	eor	v17.16b, v17.16b, v7.16b
	add	v7.4s, v7.4s, v18.4s
	ext	v18.16b, v3.16b, v3.16b, #12
	tbl	v17.16b, { v17.16b }, v0.16b
	uzp1	v3.4s, v3.4s, v18.4s
	add	v16.4s, v16.4s, v17.4s
	eor	v5.16b, v16.16b, v5.16b
	ushr	v19.4s, v5.4s, #12
	shl	v5.4s, v5.4s, #20
	orr	v5.16b, v5.16b, v19.16b
	add	v7.4s, v7.4s, v5.4s
	eor	v17.16b, v17.16b, v7.16b
	ext	v7.16b, v7.16b, v7.16b, #12
	tbl	v17.16b, { v17.16b }, v1.16b
	add	v3.4s, v7.4s, v3.4s
	add	v16.4s, v16.4s, v17.4s
	ext	v7.16b, v17.16b, v17.16b, #8
	eor	v5.16b, v16.16b, v5.16b
	ext	v16.16b, v16.16b, v16.16b, #4
	ushr	v18.4s, v5.4s, #7
	shl	v5.4s, v5.4s, #25
	orr	v5.16b, v5.16b, v18.16b
	add	v3.4s, v3.4s, v5.4s
	eor	v7.16b, v7.16b, v3.16b
	tbl	v0.16b, { v7.16b }, v0.16b
	zip1	v7.4s, v2.4s, v6.4s
	zip1	v2.4s, v6.4s, v2.4s
	add	v4.4s, v16.4s, v0.4s
	ext	v2.16b, v2.16b, v7.16b, #8
	eor	v5.16b, v4.16b, v5.16b
	add	v2.4s, v3.4s, v2.4s
	ushr	v6.4s, v5.4s, #12
	shl	v5.4s, v5.4s, #20
	orr	v3.16b, v5.16b, v6.16b
	add	v2.4s, v2.4s, v3.4s
	eor	v0.16b, v0.16b, v2.16b
	ext	v2.16b, v2.16b, v2.16b, #4
	tbl	v0.16b, { v0.16b }, v1.16b
	add	v1.4s, v4.4s, v0.4s
	ext	v0.16b, v0.16b, v0.16b, #8
	eor	v3.16b, v1.16b, v3.16b
	ext	v1.16b, v1.16b, v1.16b, #12
	ushr	v4.4s, v3.4s, #7
	shl	v3.4s, v3.4s, #25
	stp	q1, q0, [x0, #32]
	orr	v3.16b, v3.16b, v4.16b
	stp	q2, q3, [x0]
	ret
.Lfunc_end1:
	.size	compress_pre, .Lfunc_end1-compress_pre
	.cfi_endproc

	.globl	zfs_blake3_compress_xof_sse41
	.p2align	2
	.type	zfs_blake3_compress_xof_sse41,@function
zfs_blake3_compress_xof_sse41:
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
	.size	zfs_blake3_compress_xof_sse41, .Lfunc_end2-zfs_blake3_compress_xof_sse41
	.cfi_endproc

	.section	.rodata.cst16,"aM",@progbits,16
	.p2align	4
.LCPI3_0:
	.word	0
	.word	1
	.word	2
	.word	3
.LCPI3_1:
	.byte	2
	.byte	3
	.byte	0
	.byte	1
	.byte	6
	.byte	7
	.byte	4
	.byte	5
	.byte	10
	.byte	11
	.byte	8
	.byte	9
	.byte	14
	.byte	15
	.byte	12
	.byte	13
.LCPI3_2:
	.byte	1
	.byte	2
	.byte	3
	.byte	0
	.byte	5
	.byte	6
	.byte	7
	.byte	4
	.byte	9
	.byte	10
	.byte	11
	.byte	8
	.byte	13
	.byte	14
	.byte	15
	.byte	12
.LCPI3_3:
	.word	1779033703
	.word	3144134277
	.word	1013904242
	.word	2773480762
	.text
	.globl	zfs_blake3_hash_many_sse41
	.p2align	2
	.type	zfs_blake3_hash_many_sse41,@function
zfs_blake3_hash_many_sse41:
	.cfi_startproc
	hint	#34
	stp	d15, d14, [sp, #-144]!
	stp	d13, d12, [sp, #16]
	stp	d11, d10, [sp, #32]
	stp	d9, d8, [sp, #48]
	stp	x29, x27, [sp, #64]
	stp	x26, x25, [sp, #80]
	stp	x24, x23, [sp, #96]
	stp	x22, x21, [sp, #112]
	stp	x20, x19, [sp, #128]
	sub	sp, sp, #368
	.cfi_def_cfa_offset 512
	.cfi_offset w19, -8
	.cfi_offset w20, -16
	.cfi_offset w21, -24
	.cfi_offset w22, -32
	.cfi_offset w23, -40
	.cfi_offset w24, -48
	.cfi_offset w25, -56
	.cfi_offset w26, -64
	.cfi_offset w27, -72
	.cfi_offset w29, -80
	.cfi_offset b8, -88
	.cfi_offset b9, -96
	.cfi_offset b10, -104
	.cfi_offset b11, -112
	.cfi_offset b12, -120
	.cfi_offset b13, -128
	.cfi_offset b14, -136
	.cfi_offset b15, -144
	ldr	x8, [sp, #520]
	adrp	x11, .LCPI3_1
	ldrb	w9, [sp, #512]
	adrp	x10, .LCPI3_2
	cmp	x1, #4
	b.lo	.LBB3_6
	adrp	x12, .LCPI3_0
	sbfx	w13, w5, #0, #1
	mov	w15, #58983
	mov	w16, #44677
	movk	w15, #27145, lsl #16
	movk	w16, #47975, lsl #16
	ldr	q0, [x12, :lo12:.LCPI3_0]
	dup	v1.4s, w13
	movi	v13.4s, #64
	mov	w13, #62322
	mov	w14, #62778
	orr	w12, w7, w6
	and	v0.16b, v1.16b, v0.16b
	ldr	q1, [x11, :lo12:.LCPI3_1]
	movk	w13, #15470, lsl #16
	movk	w14, #42319, lsl #16
	dup	v14.4s, w15
	stp	q0, q1, [sp, #16]
	orr	v0.4s, #128, lsl #24
	str	q0, [sp]
	dup	v0.4s, w16
	stp	q0, q14, [sp, #48]
	b	.LBB3_3
.LBB3_2:
	zip1	v0.4s, v29.4s, v8.4s
	add	x15, x4, #4
	zip1	v1.4s, v30.4s, v31.4s
	tst	w5, #0x1
	zip1	v2.4s, v24.4s, v18.4s
	csel	x4, x15, x4, ne
	zip1	v3.4s, v25.4s, v26.4s
	add	x0, x0, #32
	zip2	v6.4s, v29.4s, v8.4s
	sub	x1, x1, #4
	zip1	v4.2d, v0.2d, v1.2d
	cmp	x1, #3
	zip2	v7.4s, v30.4s, v31.4s
	zip1	v5.2d, v2.2d, v3.2d
	zip2	v0.2d, v0.2d, v1.2d
	zip2	v1.2d, v2.2d, v3.2d
	zip2	v2.4s, v24.4s, v18.4s
	zip2	v3.4s, v25.4s, v26.4s
	stp	q4, q5, [x8]
	zip2	v4.2d, v6.2d, v7.2d
	stp	q0, q1, [x8, #32]
	zip1	v0.2d, v6.2d, v7.2d
	zip1	v1.2d, v2.2d, v3.2d
	zip2	v2.2d, v2.2d, v3.2d
	stp	q0, q1, [x8, #64]
	stp	q4, q2, [x8, #96]
	add	x8, x8, #128
	b.ls	.LBB3_6
.LBB3_3:
	mov	x15, x3
	add	x16, x3, #8
	add	x17, x3, #12
	add	x19, x3, #16
	add	x20, x3, #20
	ld1r	{ v29.4s }, [x15], #4
	ld1r	{ v30.4s }, [x16]
	add	x16, x3, #24
	ld1r	{ v31.4s }, [x17]
	add	x17, x3, #28
	ld1r	{ v24.4s }, [x19]
	ld1r	{ v18.4s }, [x20]
	ld1r	{ v25.4s }, [x16]
	ld1r	{ v8.4s }, [x15]
	ld1r	{ v26.4s }, [x17]
	cbz	x2, .LBB3_2
	ldr	q1, [sp, #16]
	dup	v0.4s, w4
	lsr	x17, x4, #32
	mov	x15, xzr
	ldp	x19, x20, [x0, #16]
	add	v1.4s, v0.4s, v1.4s
	mov	x21, x2
	movi	v0.4s, #128, lsl #24
	mov	w26, w12
	str	q1, [sp, #96]
	eor	v0.16b, v1.16b, v0.16b
	ldr	q1, [sp]
	cmgt	v0.4s, v1.4s, v0.4s
	dup	v1.4s, w17
	ldp	x16, x17, [x0]
	sub	v0.4s, v1.4s, v0.4s
	str	q0, [sp, #80]
.LBB3_5:
	add	x23, x16, x15
	add	x24, x17, x15
	add	x22, x19, x15
	add	x25, x20, x15
	subs	x21, x21, #1
	add	x15, x15, #64
	ldp	q1, q2, [x23]
	csel	w27, w9, wzr, eq
	orr	w26, w27, w26
	and	w26, w26, #0xff
	ldp	q4, q5, [x24]
	dup	v0.4s, w26
	mov	w26, w6
	zip1	v22.4s, v1.4s, v4.4s
	zip2	v20.4s, v1.4s, v4.4s
	ldp	q6, q7, [x22]
	zip1	v17.4s, v2.4s, v5.4s
	zip2	v23.4s, v2.4s, v5.4s
	ldp	q16, q21, [x25]
	zip1	v19.4s, v6.4s, v16.4s
	zip2	v1.4s, v6.4s, v16.4s
	ldp	q27, q28, [x23, #32]
	zip1	v4.4s, v7.4s, v21.4s
	zip2	v5.4s, v7.4s, v21.4s
	zip2	v15.2d, v17.2d, v4.2d
	ldp	q9, q10, [x24, #32]
	mov	v17.d[1], v4.d[0]
	add	v4.4s, v30.4s, v25.4s
	zip2	v11.2d, v23.2d, v5.2d
	zip2	v3.4s, v27.4s, v9.4s
	zip1	v7.4s, v27.4s, v9.4s
	ldp	q12, q6, [x22, #32]
	mov	v23.d[1], v5.d[0]
	stp	q11, q3, [sp, #256]
	add	v5.4s, v31.4s, v26.4s
	add	v4.4s, v4.4s, v17.4s
	str	q23, [sp, #352]
	ldp	q16, q2, [x25, #32]
	add	v5.4s, v5.4s, v23.4s
	zip1	v3.4s, v12.4s, v16.4s
	eor	v0.16b, v5.16b, v0.16b
	zip1	v9.4s, v6.4s, v2.4s
	zip2	v2.4s, v6.4s, v2.4s
	stp	q7, q3, [sp, #208]
	zip2	v3.4s, v12.4s, v16.4s
	zip1	v12.4s, v28.4s, v10.4s
	zip2	v10.4s, v28.4s, v10.4s
	stp	q17, q2, [sp, #160]
	zip2	v28.2d, v22.2d, v19.2d
	mov	v22.d[1], v19.d[0]
	str	q3, [sp, #240]
	add	v2.4s, v8.4s, v18.4s
	eor	v16.16b, v4.16b, v13.16b
	dup	v17.4s, w13
	mov	v3.16b, v22.16b
	stp	q22, q28, [sp, #320]
	zip2	v22.2d, v20.2d, v1.2d
	mov	v20.d[1], v1.d[0]
	add	v1.4s, v29.4s, v24.4s
	add	v4.4s, v4.4s, v15.4s
	add	v5.4s, v5.4s, v11.4s
	add	v2.4s, v2.4s, v20.4s
	stp	q15, q20, [sp, #288]
	add	v1.4s, v1.4s, v3.4s
	ldr	q3, [sp, #96]
	dup	v20.4s, w14
	mov	v23.16b, v22.16b
	mov	v15.16b, v10.16b
	eor	v6.16b, v1.16b, v3.16b
	ldr	q3, [sp, #80]
	add	v1.4s, v1.4s, v28.4s
	ldr	q28, [sp, #272]
	str	q23, [sp, #128]
	eor	v7.16b, v2.16b, v3.16b
	ldp	q27, q3, [sp, #32]
	add	v2.4s, v2.4s, v22.4s
	tbl	v6.16b, { v6.16b }, v27.16b
	tbl	v7.16b, { v7.16b }, v27.16b
	tbl	v16.16b, { v16.16b }, v27.16b
	tbl	v0.16b, { v0.16b }, v27.16b
	add	v19.4s, v6.4s, v14.4s
	add	v21.4s, v7.4s, v3.4s
	add	v30.4s, v16.4s, v17.4s
	add	v31.4s, v0.4s, v20.4s
	eor	v24.16b, v19.16b, v24.16b
	eor	v17.16b, v21.16b, v18.16b
	ushr	v18.4s, v24.4s, #12
	shl	v20.4s, v24.4s, #20
	eor	v24.16b, v30.16b, v25.16b
	eor	v25.16b, v31.16b, v26.16b
	ushr	v26.4s, v17.4s, #12
	shl	v17.4s, v17.4s, #20
	ushr	v29.4s, v24.4s, #12
	shl	v24.4s, v24.4s, #20
	ushr	v8.4s, v25.4s, #12
	shl	v25.4s, v25.4s, #20
	orr	v3.16b, v20.16b, v18.16b
	ldr	q18, [x10, :lo12:.LCPI3_2]
	orr	v13.16b, v17.16b, v26.16b
	orr	v24.16b, v24.16b, v29.16b
	orr	v14.16b, v25.16b, v8.16b
	add	v8.4s, v1.4s, v3.4s
	add	v29.4s, v2.4s, v13.4s
	add	v17.4s, v4.4s, v24.4s
	add	v20.4s, v5.4s, v14.4s
	eor	v1.16b, v6.16b, v8.16b
	eor	v2.16b, v7.16b, v29.16b
	eor	v4.16b, v16.16b, v17.16b
	eor	v0.16b, v0.16b, v20.16b
	tbl	v25.16b, { v1.16b }, v18.16b
	tbl	v16.16b, { v2.16b }, v18.16b
	tbl	v6.16b, { v4.16b }, v18.16b
	tbl	v4.16b, { v0.16b }, v18.16b
	add	v19.4s, v19.4s, v25.4s
	add	v21.4s, v21.4s, v16.4s
	add	v26.4s, v30.4s, v6.4s
	add	v7.4s, v31.4s, v4.4s
	eor	v0.16b, v19.16b, v3.16b
	eor	v1.16b, v21.16b, v13.16b
	eor	v2.16b, v26.16b, v24.16b
	eor	v3.16b, v7.16b, v14.16b
	ushr	v5.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	ushr	v24.4s, v1.4s, #7
	shl	v1.4s, v1.4s, #25
	ushr	v30.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	orr	v5.16b, v0.16b, v5.16b
	orr	v0.16b, v1.16b, v24.16b
	ushr	v31.4s, v3.4s, #7
	orr	v2.16b, v2.16b, v30.16b
	ldp	q24, q30, [sp, #208]
	shl	v3.4s, v3.4s, #25
	zip2	v14.2d, v12.2d, v9.2d
	mov	v22.16b, v24.16b
	orr	v1.16b, v3.16b, v31.16b
	zip2	v3.2d, v24.2d, v30.2d
	mov	v24.16b, v28.16b
	mov	v22.d[1], v30.d[0]
	ldr	q30, [sp, #240]
	mov	v31.16b, v12.16b
	stp	q22, q14, [sp, #224]
	mov	v24.d[1], v30.d[0]
	add	v12.4s, v8.4s, v22.4s
	mov	v31.d[1], v9.d[0]
	add	v22.4s, v29.4s, v24.4s
	ldr	q29, [sp, #176]
	zip2	v28.2d, v28.2d, v30.2d
	mov	v9.16b, v24.16b
	mov	v15.d[1], v29.d[0]
	zip2	v8.2d, v10.2d, v29.2d
	add	v10.4s, v12.4s, v0.4s
	add	v22.4s, v22.4s, v2.4s
	str	q9, [sp, #144]
	add	v20.4s, v20.4s, v15.4s
	add	v17.4s, v17.4s, v31.4s
	stp	q3, q8, [sp, #192]
	eor	v4.16b, v4.16b, v10.16b
	eor	v25.16b, v25.16b, v22.16b
	add	v20.4s, v20.4s, v5.4s
	add	v17.4s, v17.4s, v1.4s
	tbl	v4.16b, { v4.16b }, v27.16b
	tbl	v25.16b, { v25.16b }, v27.16b
	eor	v6.16b, v6.16b, v20.16b
	eor	v16.16b, v16.16b, v17.16b
	add	v26.4s, v26.4s, v4.4s
	add	v7.4s, v7.4s, v25.4s
	tbl	v6.16b, { v6.16b }, v27.16b
	tbl	v16.16b, { v16.16b }, v27.16b
	eor	v0.16b, v26.16b, v0.16b
	eor	v2.16b, v7.16b, v2.16b
	add	v21.4s, v21.4s, v6.4s
	add	v19.4s, v19.4s, v16.4s
	ushr	v12.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	ushr	v13.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	eor	v5.16b, v21.16b, v5.16b
	eor	v1.16b, v19.16b, v1.16b
	orr	v0.16b, v0.16b, v12.16b
	add	v10.4s, v10.4s, v3.4s
	orr	v2.16b, v2.16b, v13.16b
	ushr	v13.4s, v5.4s, #12
	shl	v5.4s, v5.4s, #20
	add	v22.4s, v22.4s, v28.4s
	ushr	v12.4s, v1.4s, #12
	shl	v1.4s, v1.4s, #20
	add	v10.4s, v10.4s, v0.4s
	orr	v5.16b, v5.16b, v13.16b
	add	v22.4s, v22.4s, v2.4s
	add	v20.4s, v20.4s, v8.4s
	orr	v1.16b, v1.16b, v12.16b
	add	v17.4s, v17.4s, v14.4s
	eor	v4.16b, v4.16b, v10.16b
	eor	v25.16b, v25.16b, v22.16b
	add	v20.4s, v20.4s, v5.4s
	add	v17.4s, v17.4s, v1.4s
	tbl	v4.16b, { v4.16b }, v18.16b
	tbl	v25.16b, { v25.16b }, v18.16b
	eor	v6.16b, v6.16b, v20.16b
	eor	v16.16b, v16.16b, v17.16b
	add	v26.4s, v26.4s, v4.4s
	add	v7.4s, v7.4s, v25.4s
	tbl	v6.16b, { v6.16b }, v18.16b
	tbl	v16.16b, { v16.16b }, v18.16b
	eor	v0.16b, v26.16b, v0.16b
	eor	v2.16b, v7.16b, v2.16b
	add	v21.4s, v21.4s, v6.4s
	add	v19.4s, v19.4s, v16.4s
	ushr	v12.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	ushr	v13.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	eor	v5.16b, v21.16b, v5.16b
	eor	v1.16b, v19.16b, v1.16b
	orr	v0.16b, v0.16b, v12.16b
	add	v22.4s, v22.4s, v23.4s
	orr	v2.16b, v2.16b, v13.16b
	ushr	v13.4s, v5.4s, #7
	shl	v5.4s, v5.4s, #25
	add	v17.4s, v17.4s, v11.4s
	mov	v30.16b, v28.16b
	mov	v28.16b, v23.16b
	ldr	q23, [sp, #304]
	ushr	v12.4s, v1.4s, #7
	shl	v1.4s, v1.4s, #25
	add	v22.4s, v22.4s, v0.4s
	mov	v29.16b, v31.16b
	ldr	q31, [sp, #160]
	orr	v5.16b, v5.16b, v13.16b
	add	v17.4s, v17.4s, v2.4s
	add	v10.4s, v10.4s, v23.4s
	orr	v1.16b, v1.16b, v12.16b
	str	q29, [sp, #272]
	eor	v16.16b, v16.16b, v22.16b
	add	v20.4s, v20.4s, v31.4s
	eor	v6.16b, v6.16b, v17.16b
	add	v10.4s, v10.4s, v5.4s
	tbl	v16.16b, { v16.16b }, v27.16b
	add	v20.4s, v20.4s, v1.4s
	tbl	v6.16b, { v6.16b }, v27.16b
	eor	v25.16b, v25.16b, v10.16b
	add	v21.4s, v21.4s, v16.4s
	eor	v4.16b, v4.16b, v20.16b
	add	v26.4s, v26.4s, v6.4s
	tbl	v25.16b, { v25.16b }, v27.16b
	eor	v0.16b, v21.16b, v0.16b
	tbl	v4.16b, { v4.16b }, v27.16b
	eor	v2.16b, v26.16b, v2.16b
	add	v19.4s, v19.4s, v25.4s
	ushr	v12.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	add	v7.4s, v7.4s, v4.4s
	ushr	v13.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	eor	v5.16b, v5.16b, v19.16b
	add	v22.4s, v22.4s, v24.4s
	ldr	q24, [sp, #320]
	orr	v0.16b, v0.16b, v12.16b
	eor	v1.16b, v7.16b, v1.16b
	orr	v2.16b, v2.16b, v13.16b
	ushr	v12.4s, v5.4s, #12
	shl	v5.4s, v5.4s, #20
	add	v17.4s, v17.4s, v24.4s
	ldr	q24, [sp, #352]
	ushr	v13.4s, v1.4s, #12
	shl	v1.4s, v1.4s, #20
	add	v22.4s, v22.4s, v0.4s
	orr	v5.16b, v5.16b, v12.16b
	add	v17.4s, v17.4s, v2.4s
	add	v10.4s, v10.4s, v24.4s
	ldr	q24, [sp, #336]
	orr	v1.16b, v1.16b, v13.16b
	eor	v16.16b, v16.16b, v22.16b
	add	v20.4s, v20.4s, v14.4s
	eor	v6.16b, v6.16b, v17.16b
	add	v10.4s, v10.4s, v5.4s
	tbl	v16.16b, { v16.16b }, v18.16b
	add	v20.4s, v20.4s, v1.4s
	tbl	v6.16b, { v6.16b }, v18.16b
	eor	v25.16b, v25.16b, v10.16b
	add	v21.4s, v21.4s, v16.4s
	eor	v4.16b, v4.16b, v20.16b
	add	v26.4s, v26.4s, v6.4s
	tbl	v25.16b, { v25.16b }, v18.16b
	eor	v0.16b, v21.16b, v0.16b
	tbl	v4.16b, { v4.16b }, v18.16b
	eor	v2.16b, v26.16b, v2.16b
	add	v19.4s, v19.4s, v25.4s
	ushr	v12.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	add	v7.4s, v7.4s, v4.4s
	ushr	v13.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	eor	v5.16b, v19.16b, v5.16b
	orr	v0.16b, v0.16b, v12.16b
	eor	v1.16b, v7.16b, v1.16b
	add	v10.4s, v10.4s, v24.4s
	orr	v2.16b, v2.16b, v13.16b
	ushr	v12.4s, v5.4s, #7
	shl	v5.4s, v5.4s, #25
	add	v22.4s, v22.4s, v29.4s
	ushr	v13.4s, v1.4s, #7
	shl	v1.4s, v1.4s, #25
	add	v10.4s, v10.4s, v0.4s
	orr	v5.16b, v5.16b, v12.16b
	add	v22.4s, v22.4s, v2.4s
	add	v20.4s, v20.4s, v8.4s
	ldr	q8, [sp, #288]
	orr	v1.16b, v1.16b, v13.16b
	add	v17.4s, v17.4s, v3.4s
	ldr	q3, [sp, #352]
	eor	v4.16b, v4.16b, v10.16b
	eor	v25.16b, v25.16b, v22.16b
	add	v20.4s, v20.4s, v5.4s
	add	v17.4s, v17.4s, v1.4s
	tbl	v4.16b, { v4.16b }, v27.16b
	tbl	v25.16b, { v25.16b }, v27.16b
	eor	v6.16b, v6.16b, v20.16b
	eor	v16.16b, v16.16b, v17.16b
	add	v26.4s, v26.4s, v4.4s
	add	v7.4s, v7.4s, v25.4s
	tbl	v6.16b, { v6.16b }, v27.16b
	tbl	v16.16b, { v16.16b }, v27.16b
	eor	v0.16b, v26.16b, v0.16b
	eor	v2.16b, v7.16b, v2.16b
	add	v21.4s, v21.4s, v6.4s
	add	v19.4s, v19.4s, v16.4s
	ushr	v12.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	ushr	v13.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	eor	v5.16b, v21.16b, v5.16b
	eor	v1.16b, v19.16b, v1.16b
	orr	v0.16b, v0.16b, v12.16b
	add	v10.4s, v10.4s, v30.4s
	orr	v2.16b, v2.16b, v13.16b
	ushr	v13.4s, v5.4s, #12
	shl	v5.4s, v5.4s, #20
	add	v22.4s, v22.4s, v8.4s
	mov	v24.16b, v30.16b
	mov	v30.16b, v15.16b
	add	v17.4s, v17.4s, v15.4s
	ldr	q15, [sp, #224]
	ushr	v12.4s, v1.4s, #12
	shl	v1.4s, v1.4s, #20
	add	v10.4s, v10.4s, v0.4s
	str	q30, [sp, #176]
	orr	v5.16b, v5.16b, v13.16b
	add	v22.4s, v22.4s, v2.4s
	add	v20.4s, v20.4s, v15.4s
	orr	v1.16b, v1.16b, v12.16b
	eor	v4.16b, v4.16b, v10.16b
	eor	v25.16b, v25.16b, v22.16b
	add	v20.4s, v20.4s, v5.4s
	add	v17.4s, v17.4s, v1.4s
	tbl	v4.16b, { v4.16b }, v18.16b
	tbl	v25.16b, { v25.16b }, v18.16b
	eor	v6.16b, v6.16b, v20.16b
	eor	v16.16b, v16.16b, v17.16b
	add	v26.4s, v26.4s, v4.4s
	add	v7.4s, v7.4s, v25.4s
	tbl	v6.16b, { v6.16b }, v18.16b
	tbl	v16.16b, { v16.16b }, v18.16b
	eor	v0.16b, v26.16b, v0.16b
	eor	v2.16b, v7.16b, v2.16b
	add	v21.4s, v21.4s, v6.4s
	add	v19.4s, v19.4s, v16.4s
	ushr	v12.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	ushr	v13.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	eor	v5.16b, v21.16b, v5.16b
	eor	v1.16b, v19.16b, v1.16b
	orr	v0.16b, v0.16b, v12.16b
	add	v22.4s, v22.4s, v9.4s
	orr	v2.16b, v2.16b, v13.16b
	ushr	v13.4s, v5.4s, #7
	shl	v5.4s, v5.4s, #25
	add	v17.4s, v17.4s, v14.4s
	ushr	v12.4s, v1.4s, #7
	shl	v1.4s, v1.4s, #25
	add	v22.4s, v22.4s, v0.4s
	orr	v5.16b, v5.16b, v13.16b
	add	v17.4s, v17.4s, v2.4s
	add	v10.4s, v10.4s, v28.4s
	orr	v1.16b, v1.16b, v12.16b
	eor	v16.16b, v16.16b, v22.16b
	add	v20.4s, v20.4s, v11.4s
	eor	v6.16b, v6.16b, v17.16b
	add	v10.4s, v10.4s, v5.4s
	tbl	v16.16b, { v16.16b }, v27.16b
	add	v20.4s, v20.4s, v1.4s
	tbl	v6.16b, { v6.16b }, v27.16b
	eor	v25.16b, v25.16b, v10.16b
	add	v21.4s, v21.4s, v16.4s
	eor	v4.16b, v4.16b, v20.16b
	add	v26.4s, v26.4s, v6.4s
	tbl	v25.16b, { v25.16b }, v27.16b
	eor	v0.16b, v21.16b, v0.16b
	tbl	v4.16b, { v4.16b }, v27.16b
	eor	v2.16b, v26.16b, v2.16b
	add	v19.4s, v19.4s, v25.4s
	ushr	v12.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	add	v7.4s, v7.4s, v4.4s
	ushr	v13.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	eor	v5.16b, v5.16b, v19.16b
	orr	v0.16b, v0.16b, v12.16b
	eor	v1.16b, v7.16b, v1.16b
	add	v22.4s, v22.4s, v29.4s
	orr	v2.16b, v2.16b, v13.16b
	ushr	v12.4s, v5.4s, #12
	shl	v5.4s, v5.4s, #20
	add	v17.4s, v17.4s, v23.4s
	ushr	v13.4s, v1.4s, #12
	shl	v1.4s, v1.4s, #20
	add	v22.4s, v22.4s, v0.4s
	orr	v5.16b, v5.16b, v12.16b
	add	v17.4s, v17.4s, v2.4s
	add	v10.4s, v10.4s, v31.4s
	orr	v1.16b, v1.16b, v13.16b
	eor	v16.16b, v16.16b, v22.16b
	add	v20.4s, v20.4s, v30.4s
	eor	v6.16b, v6.16b, v17.16b
	add	v10.4s, v10.4s, v5.4s
	tbl	v16.16b, { v16.16b }, v18.16b
	add	v20.4s, v20.4s, v1.4s
	tbl	v6.16b, { v6.16b }, v18.16b
	eor	v25.16b, v25.16b, v10.16b
	add	v21.4s, v21.4s, v16.4s
	eor	v4.16b, v4.16b, v20.16b
	add	v26.4s, v26.4s, v6.4s
	tbl	v25.16b, { v25.16b }, v18.16b
	eor	v0.16b, v21.16b, v0.16b
	tbl	v4.16b, { v4.16b }, v18.16b
	eor	v2.16b, v26.16b, v2.16b
	add	v19.4s, v19.4s, v25.4s
	ushr	v12.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	add	v7.4s, v7.4s, v4.4s
	ushr	v13.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	eor	v5.16b, v19.16b, v5.16b
	add	v10.4s, v10.4s, v3.4s
	ldr	q3, [sp, #192]
	orr	v0.16b, v0.16b, v12.16b
	eor	v1.16b, v7.16b, v1.16b
	orr	v2.16b, v2.16b, v13.16b
	ushr	v12.4s, v5.4s, #7
	shl	v5.4s, v5.4s, #25
	add	v22.4s, v22.4s, v3.4s
	ushr	v13.4s, v1.4s, #7
	shl	v1.4s, v1.4s, #25
	add	v10.4s, v10.4s, v0.4s
	orr	v5.16b, v5.16b, v12.16b
	add	v22.4s, v22.4s, v2.4s
	add	v20.4s, v20.4s, v15.4s
	ldr	q15, [sp, #128]
	orr	v1.16b, v1.16b, v13.16b
	add	v17.4s, v17.4s, v24.4s
	eor	v4.16b, v4.16b, v10.16b
	eor	v25.16b, v25.16b, v22.16b
	add	v20.4s, v20.4s, v5.4s
	add	v17.4s, v17.4s, v1.4s
	tbl	v4.16b, { v4.16b }, v27.16b
	tbl	v25.16b, { v25.16b }, v27.16b
	eor	v6.16b, v6.16b, v20.16b
	eor	v16.16b, v16.16b, v17.16b
	add	v26.4s, v26.4s, v4.4s
	add	v7.4s, v7.4s, v25.4s
	tbl	v6.16b, { v6.16b }, v27.16b
	tbl	v16.16b, { v16.16b }, v27.16b
	eor	v0.16b, v26.16b, v0.16b
	eor	v2.16b, v7.16b, v2.16b
	add	v21.4s, v21.4s, v6.4s
	add	v19.4s, v19.4s, v16.4s
	ushr	v12.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	ushr	v13.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	eor	v5.16b, v21.16b, v5.16b
	ldp	q23, q11, [sp, #320]
	eor	v1.16b, v19.16b, v1.16b
	orr	v0.16b, v0.16b, v12.16b
	add	v10.4s, v10.4s, v8.4s
	orr	v2.16b, v2.16b, v13.16b
	ushr	v13.4s, v5.4s, #12
	shl	v5.4s, v5.4s, #20
	add	v22.4s, v22.4s, v23.4s
	ushr	v12.4s, v1.4s, #12
	shl	v1.4s, v1.4s, #20
	add	v10.4s, v10.4s, v0.4s
	mov	v28.16b, v31.16b
	mov	v31.16b, v8.16b
	ldr	q8, [sp, #208]
	orr	v5.16b, v5.16b, v13.16b
	add	v22.4s, v22.4s, v2.4s
	add	v20.4s, v20.4s, v11.4s
	orr	v1.16b, v1.16b, v12.16b
	add	v17.4s, v17.4s, v8.4s
	eor	v4.16b, v4.16b, v10.16b
	eor	v25.16b, v25.16b, v22.16b
	add	v20.4s, v20.4s, v5.4s
	add	v17.4s, v17.4s, v1.4s
	tbl	v4.16b, { v4.16b }, v18.16b
	tbl	v25.16b, { v25.16b }, v18.16b
	eor	v6.16b, v6.16b, v20.16b
	eor	v16.16b, v16.16b, v17.16b
	add	v26.4s, v26.4s, v4.4s
	add	v7.4s, v7.4s, v25.4s
	tbl	v6.16b, { v6.16b }, v18.16b
	tbl	v16.16b, { v16.16b }, v18.16b
	eor	v0.16b, v26.16b, v0.16b
	eor	v2.16b, v7.16b, v2.16b
	add	v21.4s, v21.4s, v6.4s
	add	v19.4s, v19.4s, v16.4s
	ushr	v12.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	ushr	v13.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	eor	v5.16b, v21.16b, v5.16b
	eor	v1.16b, v19.16b, v1.16b
	orr	v0.16b, v0.16b, v12.16b
	add	v22.4s, v22.4s, v29.4s
	orr	v2.16b, v2.16b, v13.16b
	ushr	v13.4s, v5.4s, #7
	shl	v5.4s, v5.4s, #25
	add	v17.4s, v17.4s, v30.4s
	ushr	v12.4s, v1.4s, #7
	shl	v1.4s, v1.4s, #25
	add	v22.4s, v22.4s, v0.4s
	orr	v5.16b, v5.16b, v13.16b
	add	v17.4s, v17.4s, v2.4s
	add	v10.4s, v10.4s, v9.4s
	orr	v1.16b, v1.16b, v12.16b
	eor	v16.16b, v16.16b, v22.16b
	add	v20.4s, v20.4s, v14.4s
	ldr	q14, [sp, #256]
	eor	v6.16b, v6.16b, v17.16b
	add	v10.4s, v10.4s, v5.4s
	tbl	v16.16b, { v16.16b }, v27.16b
	add	v20.4s, v20.4s, v1.4s
	tbl	v6.16b, { v6.16b }, v27.16b
	eor	v25.16b, v25.16b, v10.16b
	add	v21.4s, v21.4s, v16.4s
	eor	v4.16b, v4.16b, v20.16b
	add	v26.4s, v26.4s, v6.4s
	tbl	v25.16b, { v25.16b }, v27.16b
	eor	v0.16b, v21.16b, v0.16b
	tbl	v4.16b, { v4.16b }, v27.16b
	eor	v2.16b, v26.16b, v2.16b
	add	v19.4s, v19.4s, v25.4s
	ushr	v12.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	add	v7.4s, v7.4s, v4.4s
	ushr	v13.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	eor	v5.16b, v5.16b, v19.16b
	orr	v0.16b, v0.16b, v12.16b
	eor	v1.16b, v7.16b, v1.16b
	add	v22.4s, v22.4s, v3.4s
	orr	v2.16b, v2.16b, v13.16b
	ushr	v12.4s, v5.4s, #12
	shl	v5.4s, v5.4s, #20
	add	v17.4s, v17.4s, v15.4s
	ushr	v13.4s, v1.4s, #12
	shl	v1.4s, v1.4s, #20
	add	v22.4s, v22.4s, v0.4s
	orr	v5.16b, v5.16b, v12.16b
	add	v17.4s, v17.4s, v2.4s
	add	v10.4s, v10.4s, v14.4s
	orr	v1.16b, v1.16b, v13.16b
	eor	v16.16b, v16.16b, v22.16b
	add	v20.4s, v20.4s, v8.4s
	eor	v6.16b, v6.16b, v17.16b
	add	v10.4s, v10.4s, v5.4s
	tbl	v16.16b, { v16.16b }, v18.16b
	add	v20.4s, v20.4s, v1.4s
	tbl	v6.16b, { v6.16b }, v18.16b
	eor	v25.16b, v25.16b, v10.16b
	add	v21.4s, v21.4s, v16.4s
	eor	v4.16b, v4.16b, v20.16b
	add	v26.4s, v26.4s, v6.4s
	tbl	v25.16b, { v25.16b }, v18.16b
	eor	v0.16b, v21.16b, v0.16b
	tbl	v4.16b, { v4.16b }, v18.16b
	eor	v2.16b, v26.16b, v2.16b
	add	v19.4s, v19.4s, v25.4s
	ushr	v12.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	add	v7.4s, v7.4s, v4.4s
	ushr	v13.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	eor	v5.16b, v19.16b, v5.16b
	orr	v0.16b, v0.16b, v12.16b
	eor	v1.16b, v7.16b, v1.16b
	add	v10.4s, v10.4s, v28.4s
	orr	v2.16b, v2.16b, v13.16b
	ushr	v12.4s, v5.4s, #7
	shl	v5.4s, v5.4s, #25
	add	v22.4s, v22.4s, v24.4s
	ushr	v13.4s, v1.4s, #7
	shl	v1.4s, v1.4s, #25
	add	v10.4s, v10.4s, v0.4s
	orr	v5.16b, v5.16b, v12.16b
	add	v22.4s, v22.4s, v2.4s
	add	v20.4s, v20.4s, v11.4s
	ldr	q11, [sp, #304]
	orr	v1.16b, v1.16b, v13.16b
	add	v17.4s, v17.4s, v31.4s
	ldr	q31, [sp, #224]
	eor	v4.16b, v4.16b, v10.16b
	eor	v25.16b, v25.16b, v22.16b
	add	v20.4s, v20.4s, v5.4s
	add	v17.4s, v17.4s, v1.4s
	tbl	v4.16b, { v4.16b }, v27.16b
	tbl	v25.16b, { v25.16b }, v27.16b
	eor	v6.16b, v6.16b, v20.16b
	eor	v16.16b, v16.16b, v17.16b
	add	v26.4s, v26.4s, v4.4s
	add	v7.4s, v7.4s, v25.4s
	tbl	v6.16b, { v6.16b }, v27.16b
	tbl	v16.16b, { v16.16b }, v27.16b
	eor	v0.16b, v26.16b, v0.16b
	eor	v2.16b, v7.16b, v2.16b
	add	v21.4s, v21.4s, v6.4s
	add	v19.4s, v19.4s, v16.4s
	ushr	v12.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	ushr	v13.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	eor	v5.16b, v21.16b, v5.16b
	eor	v1.16b, v19.16b, v1.16b
	orr	v0.16b, v0.16b, v12.16b
	add	v10.4s, v10.4s, v23.4s
	ldr	q23, [sp, #240]
	orr	v2.16b, v2.16b, v13.16b
	ushr	v13.4s, v5.4s, #12
	shl	v5.4s, v5.4s, #20
	add	v22.4s, v22.4s, v11.4s
	mov	v30.16b, v8.16b
	mov	v8.16b, v24.16b
	ldr	q24, [sp, #352]
	ushr	v12.4s, v1.4s, #12
	shl	v1.4s, v1.4s, #20
	add	v10.4s, v10.4s, v0.4s
	orr	v5.16b, v5.16b, v13.16b
	str	q8, [sp, #112]
	add	v22.4s, v22.4s, v2.4s
	add	v20.4s, v20.4s, v24.4s
	orr	v1.16b, v1.16b, v12.16b
	add	v17.4s, v17.4s, v31.4s
	eor	v4.16b, v4.16b, v10.16b
	eor	v25.16b, v25.16b, v22.16b
	add	v20.4s, v20.4s, v5.4s
	add	v17.4s, v17.4s, v1.4s
	tbl	v4.16b, { v4.16b }, v18.16b
	tbl	v25.16b, { v25.16b }, v18.16b
	eor	v6.16b, v6.16b, v20.16b
	eor	v16.16b, v16.16b, v17.16b
	add	v26.4s, v26.4s, v4.4s
	add	v7.4s, v7.4s, v25.4s
	tbl	v6.16b, { v6.16b }, v18.16b
	tbl	v16.16b, { v16.16b }, v18.16b
	eor	v0.16b, v26.16b, v0.16b
	eor	v2.16b, v7.16b, v2.16b
	add	v21.4s, v21.4s, v6.4s
	mov	v29.16b, v3.16b
	add	v19.4s, v19.4s, v16.4s
	ushr	v12.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	ushr	v13.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	eor	v5.16b, v21.16b, v5.16b
	eor	v1.16b, v19.16b, v1.16b
	orr	v0.16b, v0.16b, v12.16b
	add	v22.4s, v22.4s, v29.4s
	orr	v2.16b, v2.16b, v13.16b
	ushr	v13.4s, v5.4s, #7
	shl	v5.4s, v5.4s, #25
	add	v17.4s, v17.4s, v30.4s
	ldr	q30, [sp, #272]
	ushr	v12.4s, v1.4s, #7
	shl	v1.4s, v1.4s, #25
	add	v22.4s, v22.4s, v0.4s
	mov	v3.16b, v28.16b
	ldr	q28, [sp, #176]
	orr	v5.16b, v5.16b, v13.16b
	add	v17.4s, v17.4s, v2.4s
	add	v10.4s, v10.4s, v30.4s
	orr	v1.16b, v1.16b, v12.16b
	eor	v16.16b, v16.16b, v22.16b
	add	v20.4s, v20.4s, v28.4s
	eor	v6.16b, v6.16b, v17.16b
	add	v10.4s, v10.4s, v5.4s
	tbl	v16.16b, { v16.16b }, v27.16b
	add	v20.4s, v20.4s, v1.4s
	tbl	v6.16b, { v6.16b }, v27.16b
	eor	v25.16b, v25.16b, v10.16b
	add	v21.4s, v21.4s, v16.4s
	eor	v4.16b, v4.16b, v20.16b
	add	v26.4s, v26.4s, v6.4s
	tbl	v25.16b, { v25.16b }, v27.16b
	eor	v0.16b, v21.16b, v0.16b
	tbl	v4.16b, { v4.16b }, v27.16b
	eor	v2.16b, v26.16b, v2.16b
	add	v19.4s, v19.4s, v25.4s
	ushr	v12.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	add	v7.4s, v7.4s, v4.4s
	ushr	v13.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	eor	v5.16b, v5.16b, v19.16b
	orr	v0.16b, v0.16b, v12.16b
	eor	v1.16b, v7.16b, v1.16b
	add	v22.4s, v22.4s, v8.4s
	orr	v2.16b, v2.16b, v13.16b
	ushr	v12.4s, v5.4s, #12
	shl	v5.4s, v5.4s, #20
	add	v17.4s, v17.4s, v9.4s
	ldr	q9, [sp, #320]
	ushr	v13.4s, v1.4s, #12
	shl	v1.4s, v1.4s, #20
	add	v22.4s, v22.4s, v0.4s
	orr	v5.16b, v5.16b, v12.16b
	add	v17.4s, v17.4s, v2.4s
	add	v10.4s, v10.4s, v23.4s
	orr	v1.16b, v1.16b, v13.16b
	eor	v16.16b, v16.16b, v22.16b
	add	v20.4s, v20.4s, v31.4s
	eor	v6.16b, v6.16b, v17.16b
	add	v10.4s, v10.4s, v5.4s
	tbl	v16.16b, { v16.16b }, v18.16b
	add	v20.4s, v20.4s, v1.4s
	tbl	v6.16b, { v6.16b }, v18.16b
	eor	v25.16b, v25.16b, v10.16b
	add	v21.4s, v21.4s, v16.4s
	eor	v4.16b, v4.16b, v20.16b
	add	v26.4s, v26.4s, v6.4s
	tbl	v25.16b, { v25.16b }, v18.16b
	eor	v0.16b, v21.16b, v0.16b
	tbl	v4.16b, { v4.16b }, v18.16b
	eor	v2.16b, v26.16b, v2.16b
	add	v19.4s, v19.4s, v25.4s
	ushr	v12.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	add	v7.4s, v7.4s, v4.4s
	ushr	v13.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	eor	v5.16b, v19.16b, v5.16b
	add	v10.4s, v10.4s, v14.4s
	ldr	q14, [sp, #288]
	orr	v0.16b, v0.16b, v12.16b
	eor	v1.16b, v7.16b, v1.16b
	orr	v2.16b, v2.16b, v13.16b
	ushr	v12.4s, v5.4s, #7
	shl	v5.4s, v5.4s, #25
	add	v22.4s, v22.4s, v14.4s
	ushr	v13.4s, v1.4s, #7
	shl	v1.4s, v1.4s, #25
	add	v10.4s, v10.4s, v0.4s
	orr	v5.16b, v5.16b, v12.16b
	add	v22.4s, v22.4s, v2.4s
	add	v20.4s, v20.4s, v24.4s
	orr	v1.16b, v1.16b, v13.16b
	eor	v4.16b, v4.16b, v10.16b
	add	v17.4s, v17.4s, v9.4s
	eor	v25.16b, v25.16b, v22.16b
	add	v20.4s, v20.4s, v5.4s
	tbl	v4.16b, { v4.16b }, v27.16b
	add	v17.4s, v17.4s, v1.4s
	tbl	v25.16b, { v25.16b }, v27.16b
	eor	v6.16b, v6.16b, v20.16b
	add	v26.4s, v26.4s, v4.4s
	eor	v16.16b, v16.16b, v17.16b
	add	v7.4s, v7.4s, v25.4s
	tbl	v6.16b, { v6.16b }, v27.16b
	eor	v0.16b, v26.16b, v0.16b
	tbl	v16.16b, { v16.16b }, v27.16b
	eor	v2.16b, v7.16b, v2.16b
	add	v21.4s, v21.4s, v6.4s
	ushr	v12.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	add	v19.4s, v19.4s, v16.4s
	ushr	v13.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	eor	v5.16b, v21.16b, v5.16b
	orr	v0.16b, v0.16b, v12.16b
	eor	v1.16b, v19.16b, v1.16b
	add	v10.4s, v10.4s, v11.4s
	orr	v2.16b, v2.16b, v13.16b
	ushr	v13.4s, v5.4s, #12
	shl	v5.4s, v5.4s, #20
	ushr	v12.4s, v1.4s, #12
	shl	v1.4s, v1.4s, #20
	add	v10.4s, v10.4s, v0.4s
	add	v22.4s, v22.4s, v15.4s
	orr	v5.16b, v5.16b, v13.16b
	add	v20.4s, v20.4s, v3.4s
	mov	v24.16b, v3.16b
	ldr	q3, [sp, #336]
	orr	v1.16b, v1.16b, v12.16b
	eor	v4.16b, v4.16b, v10.16b
	add	v22.4s, v22.4s, v2.4s
	add	v17.4s, v17.4s, v3.4s
	add	v20.4s, v20.4s, v5.4s
	tbl	v4.16b, { v4.16b }, v18.16b
	eor	v25.16b, v25.16b, v22.16b
	add	v17.4s, v17.4s, v1.4s
	eor	v6.16b, v6.16b, v20.16b
	add	v26.4s, v26.4s, v4.4s
	tbl	v25.16b, { v25.16b }, v18.16b
	eor	v16.16b, v16.16b, v17.16b
	tbl	v6.16b, { v6.16b }, v18.16b
	eor	v0.16b, v26.16b, v0.16b
	add	v7.4s, v7.4s, v25.4s
	tbl	v16.16b, { v16.16b }, v18.16b
	add	v21.4s, v21.4s, v6.4s
	ushr	v12.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	eor	v2.16b, v7.16b, v2.16b
	add	v19.4s, v19.4s, v16.4s
	eor	v5.16b, v21.16b, v5.16b
	orr	v0.16b, v0.16b, v12.16b
	ushr	v12.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	eor	v1.16b, v19.16b, v1.16b
	ushr	v13.4s, v5.4s, #7
	shl	v5.4s, v5.4s, #25
	add	v22.4s, v22.4s, v8.4s
	orr	v2.16b, v2.16b, v12.16b
	ushr	v12.4s, v1.4s, #7
	shl	v1.4s, v1.4s, #25
	orr	v5.16b, v5.16b, v13.16b
	add	v22.4s, v22.4s, v0.4s
	add	v10.4s, v10.4s, v29.4s
	ldr	q29, [sp, #208]
	add	v17.4s, v17.4s, v31.4s
	orr	v1.16b, v1.16b, v12.16b
	add	v20.4s, v20.4s, v29.4s
	eor	v16.16b, v16.16b, v22.16b
	add	v10.4s, v10.4s, v5.4s
	add	v17.4s, v17.4s, v2.4s
	add	v20.4s, v20.4s, v1.4s
	tbl	v16.16b, { v16.16b }, v27.16b
	eor	v25.16b, v25.16b, v10.16b
	eor	v6.16b, v6.16b, v17.16b
	eor	v4.16b, v4.16b, v20.16b
	add	v21.4s, v21.4s, v16.4s
	tbl	v25.16b, { v25.16b }, v27.16b
	tbl	v6.16b, { v6.16b }, v27.16b
	tbl	v4.16b, { v4.16b }, v27.16b
	eor	v0.16b, v21.16b, v0.16b
	add	v19.4s, v19.4s, v25.4s
	add	v26.4s, v26.4s, v6.4s
	add	v7.4s, v7.4s, v4.4s
	ushr	v12.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	eor	v5.16b, v5.16b, v19.16b
	eor	v2.16b, v26.16b, v2.16b
	eor	v1.16b, v7.16b, v1.16b
	orr	v0.16b, v0.16b, v12.16b
	ushr	v12.4s, v5.4s, #12
	shl	v5.4s, v5.4s, #20
	add	v22.4s, v22.4s, v14.4s
	mov	v8.16b, v31.16b
	ushr	v13.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	mov	v31.16b, v14.16b
	ushr	v14.4s, v1.4s, #12
	shl	v1.4s, v1.4s, #20
	orr	v5.16b, v5.16b, v12.16b
	add	v22.4s, v22.4s, v0.4s
	add	v10.4s, v10.4s, v28.4s
	ldr	q28, [sp, #352]
	orr	v2.16b, v2.16b, v13.16b
	orr	v1.16b, v1.16b, v14.16b
	add	v17.4s, v17.4s, v30.4s
	add	v20.4s, v20.4s, v3.4s
	eor	v16.16b, v16.16b, v22.16b
	add	v10.4s, v10.4s, v5.4s
	add	v17.4s, v17.4s, v2.4s
	add	v20.4s, v20.4s, v1.4s
	tbl	v16.16b, { v16.16b }, v18.16b
	eor	v25.16b, v25.16b, v10.16b
	eor	v6.16b, v6.16b, v17.16b
	eor	v4.16b, v4.16b, v20.16b
	add	v21.4s, v21.4s, v16.4s
	tbl	v25.16b, { v25.16b }, v18.16b
	tbl	v6.16b, { v6.16b }, v18.16b
	tbl	v4.16b, { v4.16b }, v18.16b
	eor	v0.16b, v21.16b, v0.16b
	add	v19.4s, v19.4s, v25.4s
	add	v26.4s, v26.4s, v6.4s
	add	v7.4s, v7.4s, v4.4s
	ushr	v12.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	eor	v5.16b, v19.16b, v5.16b
	eor	v2.16b, v26.16b, v2.16b
	eor	v1.16b, v7.16b, v1.16b
	orr	v0.16b, v0.16b, v12.16b
	ushr	v12.4s, v5.4s, #7
	shl	v5.4s, v5.4s, #25
	add	v10.4s, v10.4s, v23.4s
	ushr	v13.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	ushr	v14.4s, v1.4s, #7
	shl	v1.4s, v1.4s, #25
	orr	v5.16b, v5.16b, v12.16b
	add	v10.4s, v10.4s, v0.4s
	add	v20.4s, v20.4s, v24.4s
	ldr	q24, [sp, #144]
	orr	v2.16b, v2.16b, v13.16b
	orr	v1.16b, v1.16b, v14.16b
	add	v22.4s, v22.4s, v9.4s
	add	v17.4s, v17.4s, v11.4s
	eor	v4.16b, v4.16b, v10.16b
	add	v20.4s, v20.4s, v5.4s
	add	v22.4s, v22.4s, v2.4s
	add	v17.4s, v17.4s, v1.4s
	tbl	v4.16b, { v4.16b }, v27.16b
	eor	v6.16b, v6.16b, v20.16b
	eor	v25.16b, v25.16b, v22.16b
	eor	v16.16b, v16.16b, v17.16b
	add	v26.4s, v26.4s, v4.4s
	tbl	v6.16b, { v6.16b }, v27.16b
	tbl	v25.16b, { v25.16b }, v27.16b
	tbl	v16.16b, { v16.16b }, v27.16b
	eor	v0.16b, v26.16b, v0.16b
	add	v21.4s, v21.4s, v6.4s
	add	v7.4s, v7.4s, v25.4s
	add	v19.4s, v19.4s, v16.4s
	ushr	v12.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	eor	v5.16b, v21.16b, v5.16b
	eor	v2.16b, v7.16b, v2.16b
	eor	v1.16b, v19.16b, v1.16b
	orr	v0.16b, v0.16b, v12.16b
	add	v10.4s, v10.4s, v15.4s
	ushr	v14.4s, v5.4s, #12
	shl	v5.4s, v5.4s, #20
	mov	v30.16b, v3.16b
	ldr	q3, [sp, #256]
	ushr	v12.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	ushr	v13.4s, v1.4s, #12
	shl	v1.4s, v1.4s, #20
	add	v10.4s, v10.4s, v0.4s
	orr	v5.16b, v5.16b, v14.16b
	add	v20.4s, v20.4s, v3.4s
	orr	v2.16b, v2.16b, v12.16b
	orr	v1.16b, v1.16b, v13.16b
	add	v22.4s, v22.4s, v24.4s
	add	v17.4s, v17.4s, v28.4s
	eor	v4.16b, v4.16b, v10.16b
	add	v20.4s, v20.4s, v5.4s
	add	v22.4s, v22.4s, v2.4s
	add	v17.4s, v17.4s, v1.4s
	tbl	v4.16b, { v4.16b }, v18.16b
	eor	v6.16b, v6.16b, v20.16b
	eor	v25.16b, v25.16b, v22.16b
	eor	v16.16b, v16.16b, v17.16b
	add	v26.4s, v26.4s, v4.4s
	tbl	v6.16b, { v6.16b }, v18.16b
	tbl	v25.16b, { v25.16b }, v18.16b
	tbl	v16.16b, { v16.16b }, v18.16b
	eor	v0.16b, v26.16b, v0.16b
	add	v21.4s, v21.4s, v6.4s
	add	v7.4s, v7.4s, v25.4s
	add	v19.4s, v19.4s, v16.4s
	ushr	v12.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	eor	v5.16b, v21.16b, v5.16b
	eor	v2.16b, v7.16b, v2.16b
	eor	v1.16b, v19.16b, v1.16b
	orr	v0.16b, v0.16b, v12.16b
	ushr	v12.4s, v5.4s, #7
	shl	v5.4s, v5.4s, #25
	mov	v23.16b, v9.16b
	ldr	q9, [sp, #112]
	ushr	v13.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	ushr	v14.4s, v1.4s, #7
	shl	v1.4s, v1.4s, #25
	orr	v5.16b, v5.16b, v12.16b
	add	v9.4s, v10.4s, v9.4s
	orr	v2.16b, v2.16b, v13.16b
	orr	v1.16b, v1.16b, v14.16b
	ldr	q14, [sp, #64]
	add	v22.4s, v22.4s, v31.4s
	add	v17.4s, v17.4s, v30.4s
	add	v20.4s, v20.4s, v8.4s
	add	v9.4s, v9.4s, v5.4s
	add	v22.4s, v22.4s, v0.4s
	add	v17.4s, v17.4s, v2.4s
	add	v20.4s, v20.4s, v1.4s
	eor	v25.16b, v25.16b, v9.16b
	eor	v16.16b, v16.16b, v22.16b
	eor	v6.16b, v6.16b, v17.16b
	eor	v4.16b, v4.16b, v20.16b
	tbl	v25.16b, { v25.16b }, v27.16b
	tbl	v16.16b, { v16.16b }, v27.16b
	tbl	v6.16b, { v6.16b }, v27.16b
	tbl	v4.16b, { v4.16b }, v27.16b
	add	v19.4s, v19.4s, v25.4s
	add	v21.4s, v21.4s, v16.4s
	add	v26.4s, v26.4s, v6.4s
	add	v7.4s, v7.4s, v4.4s
	eor	v5.16b, v5.16b, v19.16b
	eor	v0.16b, v21.16b, v0.16b
	eor	v2.16b, v26.16b, v2.16b
	eor	v1.16b, v7.16b, v1.16b
	ushr	v30.4s, v5.4s, #12
	shl	v5.4s, v5.4s, #20
	ushr	v10.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	ushr	v12.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	ushr	v13.4s, v1.4s, #12
	shl	v1.4s, v1.4s, #20
	orr	v5.16b, v5.16b, v30.16b
	add	v30.4s, v9.4s, v29.4s
	add	v22.4s, v22.4s, v23.4s
	ldr	q23, [sp, #192]
	orr	v0.16b, v0.16b, v10.16b
	orr	v2.16b, v2.16b, v12.16b
	orr	v1.16b, v1.16b, v13.16b
	add	v17.4s, v17.4s, v23.4s
	add	v20.4s, v20.4s, v28.4s
	add	v23.4s, v30.4s, v5.4s
	add	v22.4s, v22.4s, v0.4s
	add	v17.4s, v17.4s, v2.4s
	add	v20.4s, v20.4s, v1.4s
	eor	v25.16b, v25.16b, v23.16b
	eor	v16.16b, v16.16b, v22.16b
	eor	v6.16b, v6.16b, v17.16b
	eor	v4.16b, v4.16b, v20.16b
	tbl	v25.16b, { v25.16b }, v18.16b
	tbl	v16.16b, { v16.16b }, v18.16b
	tbl	v6.16b, { v6.16b }, v18.16b
	tbl	v4.16b, { v4.16b }, v18.16b
	add	v19.4s, v19.4s, v25.4s
	add	v21.4s, v21.4s, v16.4s
	add	v26.4s, v26.4s, v6.4s
	add	v7.4s, v7.4s, v4.4s
	eor	v5.16b, v19.16b, v5.16b
	eor	v0.16b, v21.16b, v0.16b
	eor	v2.16b, v26.16b, v2.16b
	eor	v1.16b, v7.16b, v1.16b
	ushr	v28.4s, v5.4s, #7
	shl	v5.4s, v5.4s, #25
	ushr	v30.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	ushr	v31.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	ushr	v8.4s, v1.4s, #7
	shl	v1.4s, v1.4s, #25
	orr	v5.16b, v5.16b, v28.16b
	ldr	q28, [sp, #176]
	orr	v0.16b, v0.16b, v30.16b
	orr	v2.16b, v2.16b, v31.16b
	orr	v1.16b, v1.16b, v8.16b
	add	v23.4s, v23.4s, v28.4s
	add	v22.4s, v22.4s, v11.4s
	add	v17.4s, v17.4s, v15.4s
	add	v20.4s, v20.4s, v3.4s
	ldr	q3, [sp, #272]
	add	v23.4s, v23.4s, v0.4s
	add	v22.4s, v22.4s, v2.4s
	add	v17.4s, v17.4s, v1.4s
	add	v20.4s, v20.4s, v5.4s
	eor	v4.16b, v4.16b, v23.16b
	eor	v25.16b, v25.16b, v22.16b
	eor	v16.16b, v16.16b, v17.16b
	eor	v6.16b, v6.16b, v20.16b
	tbl	v4.16b, { v4.16b }, v27.16b
	tbl	v25.16b, { v25.16b }, v27.16b
	tbl	v16.16b, { v16.16b }, v27.16b
	tbl	v6.16b, { v6.16b }, v27.16b
	add	v26.4s, v26.4s, v4.4s
	add	v7.4s, v7.4s, v25.4s
	add	v19.4s, v19.4s, v16.4s
	add	v21.4s, v21.4s, v6.4s
	eor	v0.16b, v26.16b, v0.16b
	eor	v2.16b, v7.16b, v2.16b
	eor	v1.16b, v19.16b, v1.16b
	eor	v5.16b, v21.16b, v5.16b
	add	v3.4s, v22.4s, v3.4s
	ldr	q22, [sp, #160]
	ushr	v28.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	ushr	v29.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	ushr	v30.4s, v1.4s, #12
	shl	v1.4s, v1.4s, #20
	ushr	v31.4s, v5.4s, #12
	shl	v5.4s, v5.4s, #20
	add	v17.4s, v17.4s, v22.4s
	ldr	q22, [sp, #240]
	orr	v0.16b, v0.16b, v28.16b
	prfm	pldl1keep, [x23, #256]
	orr	v2.16b, v2.16b, v29.16b
	prfm	pldl1keep, [x24, #256]
	orr	v1.16b, v1.16b, v30.16b
	prfm	pldl1keep, [x22, #256]
	orr	v5.16b, v5.16b, v31.16b
	prfm	pldl1keep, [x25, #256]
	add	v23.4s, v23.4s, v24.4s
	add	v20.4s, v20.4s, v22.4s
	add	v3.4s, v3.4s, v2.4s
	add	v17.4s, v17.4s, v1.4s
	add	v22.4s, v23.4s, v0.4s
	add	v20.4s, v20.4s, v5.4s
	eor	v23.16b, v25.16b, v3.16b
	eor	v16.16b, v16.16b, v17.16b
	eor	v4.16b, v4.16b, v22.16b
	eor	v6.16b, v6.16b, v20.16b
	tbl	v23.16b, { v23.16b }, v18.16b
	tbl	v16.16b, { v16.16b }, v18.16b
	tbl	v4.16b, { v4.16b }, v18.16b
	tbl	v6.16b, { v6.16b }, v18.16b
	add	v7.4s, v7.4s, v23.4s
	add	v19.4s, v19.4s, v16.4s
	add	v18.4s, v26.4s, v4.4s
	add	v21.4s, v21.4s, v6.4s
	eor	v2.16b, v7.16b, v2.16b
	eor	v1.16b, v19.16b, v1.16b
	eor	v0.16b, v18.16b, v0.16b
	eor	v5.16b, v21.16b, v5.16b
	ushr	v25.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	ushr	v24.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	ushr	v26.4s, v1.4s, #7
	shl	v1.4s, v1.4s, #25
	ushr	v27.4s, v5.4s, #7
	shl	v5.4s, v5.4s, #25
	orr	v0.16b, v0.16b, v24.16b
	orr	v2.16b, v2.16b, v25.16b
	orr	v1.16b, v1.16b, v26.16b
	orr	v5.16b, v5.16b, v27.16b
	movi	v13.4s, #64
	eor	v29.16b, v19.16b, v22.16b
	eor	v8.16b, v21.16b, v3.16b
	eor	v30.16b, v17.16b, v18.16b
	eor	v31.16b, v20.16b, v7.16b
	eor	v24.16b, v5.16b, v23.16b
	eor	v18.16b, v0.16b, v16.16b
	eor	v25.16b, v2.16b, v6.16b
	eor	v26.16b, v1.16b, v4.16b
	cbnz	x21, .LBB3_5
	b	.LBB3_2
.LBB3_6:
	cbz	x1, .LBB3_14
	adrp	x12, .LCPI3_3
	ldr	q0, [x11, :lo12:.LCPI3_1]
	orr	w11, w7, w6
	ldr	q2, [x10, :lo12:.LCPI3_2]
	ldr	q1, [x12, :lo12:.LCPI3_3]
	and	x12, x5, #0x1
.LBB3_8:
	movi	v3.4s, #64
	lsr	x13, x4, #32
	ldp	q5, q4, [x3]
	mov	x15, x2
	mov	w14, w11
	mov	v3.s[0], w4
	ldr	x10, [x0]
	mov	v3.s[1], w13
	b	.LBB3_11
.LBB3_9:
	orr	w14, w14, w9
.LBB3_10:
	ldp	q6, q7, [x10]
	mov	v16.16b, v3.16b
	and	w14, w14, #0xff
	add	v5.4s, v5.4s, v4.4s
	mov	x15, x13
	mov	v16.s[3], w14
	add	x14, x10, #32
	uzp1	v17.4s, v6.4s, v7.4s
	add	x10, x10, #64
	add	v5.4s, v5.4s, v17.4s
	eor	v16.16b, v5.16b, v16.16b
	tbl	v16.16b, { v16.16b }, v0.16b
	add	v18.4s, v16.4s, v1.4s
	eor	v19.16b, v18.16b, v4.16b
	uzp2	v4.4s, v6.4s, v7.4s
	ushr	v6.4s, v19.4s, #12
	shl	v7.4s, v19.4s, #20
	ld2	{ v19.4s, v20.4s }, [x14]
	add	v5.4s, v5.4s, v4.4s
	mov	w14, w6
	orr	v6.16b, v7.16b, v6.16b
	add	v5.4s, v5.4s, v6.4s
	eor	v7.16b, v16.16b, v5.16b
	add	v5.4s, v5.4s, v19.4s
	tbl	v7.16b, { v7.16b }, v2.16b
	ext	v5.16b, v5.16b, v5.16b, #12
	add	v16.4s, v18.4s, v7.4s
	ext	v7.16b, v7.16b, v7.16b, #8
	eor	v6.16b, v6.16b, v16.16b
	ext	v16.16b, v16.16b, v16.16b, #4
	ushr	v18.4s, v6.4s, #7
	shl	v6.4s, v6.4s, #25
	orr	v6.16b, v6.16b, v18.16b
	ext	v18.16b, v20.16b, v20.16b, #12
	add	v5.4s, v5.4s, v6.4s
	eor	v7.16b, v5.16b, v7.16b
	add	v5.4s, v5.4s, v18.4s
	tbl	v7.16b, { v7.16b }, v0.16b
	add	v16.4s, v16.4s, v7.4s
	eor	v6.16b, v6.16b, v16.16b
	ushr	v21.4s, v6.4s, #12
	shl	v6.4s, v6.4s, #20
	orr	v6.16b, v6.16b, v21.16b
	uzp1	v21.4s, v17.4s, v17.4s
	add	v5.4s, v5.4s, v6.4s
	ext	v21.16b, v21.16b, v17.16b, #8
	eor	v7.16b, v7.16b, v5.16b
	uzp2	v21.4s, v21.4s, v4.4s
	tbl	v7.16b, { v7.16b }, v2.16b
	add	v5.4s, v5.4s, v21.4s
	add	v16.4s, v16.4s, v7.4s
	ext	v5.16b, v5.16b, v5.16b, #4
	ext	v7.16b, v7.16b, v7.16b, #8
	eor	v6.16b, v6.16b, v16.16b
	ushr	v22.4s, v6.4s, #7
	shl	v6.4s, v6.4s, #25
	orr	v6.16b, v6.16b, v22.16b
	add	v22.4s, v5.4s, v6.4s
	eor	v5.16b, v22.16b, v7.16b
	ext	v7.16b, v16.16b, v16.16b, #12
	tbl	v16.16b, { v5.16b }, v0.16b
	ext	v5.16b, v17.16b, v17.16b, #12
	add	v7.4s, v7.4s, v16.4s
	ext	v5.16b, v17.16b, v5.16b, #12
	ext	v17.16b, v19.16b, v19.16b, #12
	mov	v19.16b, v18.16b
	eor	v6.16b, v6.16b, v7.16b
	rev64	v5.4s, v5.4s
	mov	v19.s[1], v17.s[2]
	ushr	v20.4s, v6.4s, #12
	shl	v6.4s, v6.4s, #20
	trn2	v5.4s, v5.4s, v19.4s
	orr	v6.16b, v6.16b, v20.16b
	zip1	v20.2d, v18.2d, v4.2d
	zip2	v4.4s, v4.4s, v18.4s
	add	v19.4s, v6.4s, v5.4s
	mov	v20.s[3], v17.s[3]
	add	v19.4s, v19.4s, v22.4s
	ext	v22.16b, v20.16b, v20.16b, #12
	eor	v16.16b, v16.16b, v19.16b
	ext	v19.16b, v19.16b, v19.16b, #12
	tbl	v16.16b, { v16.16b }, v2.16b
	add	v7.4s, v7.4s, v16.4s
	ext	v16.16b, v16.16b, v16.16b, #8
	eor	v6.16b, v6.16b, v7.16b
	ext	v7.16b, v7.16b, v7.16b, #4
	ushr	v23.4s, v6.4s, #7
	shl	v24.4s, v6.4s, #25
	uzp1	v6.4s, v20.4s, v22.4s
	orr	v20.16b, v24.16b, v23.16b
	add	v22.4s, v20.4s, v6.4s
	add	v19.4s, v22.4s, v19.4s
	eor	v16.16b, v19.16b, v16.16b
	tbl	v16.16b, { v16.16b }, v0.16b
	add	v7.4s, v7.4s, v16.4s
	eor	v18.16b, v20.16b, v7.16b
	zip1	v20.4s, v4.4s, v17.4s
	zip1	v4.4s, v17.4s, v4.4s
	ushr	v17.4s, v18.4s, #12
	shl	v18.4s, v18.4s, #20
	ext	v20.16b, v4.16b, v20.16b, #8
	orr	v4.16b, v18.16b, v17.16b
	ext	v18.16b, v21.16b, v21.16b, #4
	add	v17.4s, v4.4s, v20.4s
	add	v17.4s, v17.4s, v19.4s
	uzp1	v19.4s, v18.4s, v18.4s
	eor	v16.16b, v16.16b, v17.16b
	ext	v19.16b, v19.16b, v18.16b, #8
	tbl	v16.16b, { v16.16b }, v2.16b
	uzp2	v19.4s, v19.4s, v5.4s
	add	v7.4s, v7.4s, v16.4s
	add	v17.4s, v17.4s, v19.4s
	ext	v16.16b, v16.16b, v16.16b, #8
	eor	v4.16b, v4.16b, v7.16b
	ext	v17.16b, v17.16b, v17.16b, #4
	ext	v7.16b, v7.16b, v7.16b, #12
	ushr	v21.4s, v4.4s, #7
	shl	v4.4s, v4.4s, #25
	orr	v4.16b, v4.16b, v21.16b
	ext	v21.16b, v18.16b, v18.16b, #12
	add	v17.4s, v17.4s, v4.4s
	ext	v18.16b, v18.16b, v21.16b, #12
	mov	v21.16b, v20.16b
	eor	v16.16b, v17.16b, v16.16b
	rev64	v18.4s, v18.4s
	mov	v21.s[1], v6.s[2]
	tbl	v16.16b, { v16.16b }, v0.16b
	add	v7.4s, v7.4s, v16.4s
	eor	v4.16b, v4.16b, v7.16b
	ushr	v22.4s, v4.4s, #12
	shl	v23.4s, v4.4s, #20
	trn2	v4.4s, v18.4s, v21.4s
	orr	v18.16b, v23.16b, v22.16b
	add	v21.4s, v18.4s, v4.4s
	add	v17.4s, v21.4s, v17.4s
	zip1	v21.2d, v20.2d, v5.2d
	zip2	v5.4s, v5.4s, v20.4s
	eor	v16.16b, v16.16b, v17.16b
	mov	v21.s[3], v6.s[3]
	ext	v17.16b, v17.16b, v17.16b, #12
	zip1	v20.4s, v5.4s, v6.4s
	tbl	v16.16b, { v16.16b }, v2.16b
	zip1	v5.4s, v6.4s, v5.4s
	add	v22.4s, v7.4s, v16.4s
	ext	v16.16b, v16.16b, v16.16b, #8
	ext	v20.16b, v5.16b, v20.16b, #8
	eor	v7.16b, v18.16b, v22.16b
	ext	v18.16b, v21.16b, v21.16b, #12
	ushr	v23.4s, v7.4s, #7
	shl	v24.4s, v7.4s, #25
	uzp1	v7.4s, v21.4s, v18.4s
	orr	v18.16b, v24.16b, v23.16b
	add	v21.4s, v18.4s, v7.4s
	add	v17.4s, v21.4s, v17.4s
	ext	v21.16b, v22.16b, v22.16b, #4
	eor	v16.16b, v17.16b, v16.16b
	tbl	v16.16b, { v16.16b }, v0.16b
	add	v21.4s, v21.4s, v16.4s
	eor	v18.16b, v18.16b, v21.16b
	ushr	v6.4s, v18.4s, #12
	shl	v18.4s, v18.4s, #20
	orr	v5.16b, v18.16b, v6.16b
	add	v6.4s, v5.4s, v20.4s
	add	v6.4s, v6.4s, v17.4s
	ext	v17.16b, v19.16b, v19.16b, #4
	eor	v16.16b, v16.16b, v6.16b
	uzp1	v18.4s, v17.4s, v17.4s
	tbl	v16.16b, { v16.16b }, v2.16b
	ext	v18.16b, v18.16b, v17.16b, #8
	add	v19.4s, v21.4s, v16.4s
	uzp2	v18.4s, v18.4s, v4.4s
	ext	v16.16b, v16.16b, v16.16b, #8
	eor	v5.16b, v5.16b, v19.16b
	add	v6.4s, v6.4s, v18.4s
	ext	v19.16b, v19.16b, v19.16b, #12
	ushr	v21.4s, v5.4s, #7
	shl	v5.4s, v5.4s, #25
	ext	v6.16b, v6.16b, v6.16b, #4
	orr	v5.16b, v5.16b, v21.16b
	ext	v21.16b, v17.16b, v17.16b, #12
	add	v6.4s, v6.4s, v5.4s
	ext	v17.16b, v17.16b, v21.16b, #12
	mov	v21.16b, v20.16b
	eor	v16.16b, v6.16b, v16.16b
	rev64	v17.4s, v17.4s
	mov	v21.s[1], v7.s[2]
	tbl	v16.16b, { v16.16b }, v0.16b
	add	v19.4s, v19.4s, v16.4s
	eor	v5.16b, v5.16b, v19.16b
	ushr	v22.4s, v5.4s, #12
	shl	v23.4s, v5.4s, #20
	trn2	v5.4s, v17.4s, v21.4s
	orr	v17.16b, v23.16b, v22.16b
	add	v21.4s, v17.4s, v5.4s
	add	v6.4s, v21.4s, v6.4s
	eor	v16.16b, v16.16b, v6.16b
	ext	v6.16b, v6.16b, v6.16b, #12
	tbl	v21.16b, { v16.16b }, v2.16b
	zip1	v16.2d, v20.2d, v4.2d
	zip2	v4.4s, v4.4s, v20.4s
	add	v19.4s, v19.4s, v21.4s
	mov	v16.s[3], v7.s[3]
	ext	v21.16b, v21.16b, v21.16b, #8
	zip1	v20.4s, v4.4s, v7.4s
	eor	v17.16b, v17.16b, v19.16b
	ext	v22.16b, v16.16b, v16.16b, #12
	ext	v19.16b, v19.16b, v19.16b, #4
	zip1	v4.4s, v7.4s, v4.4s
	ushr	v23.4s, v17.4s, #7
	shl	v17.4s, v17.4s, #25
	uzp1	v16.4s, v16.4s, v22.4s
	ext	v4.16b, v4.16b, v20.16b, #8
	orr	v17.16b, v17.16b, v23.16b
	add	v22.4s, v17.4s, v16.4s
	add	v6.4s, v22.4s, v6.4s
	eor	v21.16b, v6.16b, v21.16b
	tbl	v21.16b, { v21.16b }, v0.16b
	add	v19.4s, v19.4s, v21.4s
	eor	v17.16b, v17.16b, v19.16b
	ushr	v7.4s, v17.4s, #12
	shl	v17.4s, v17.4s, #20
	orr	v7.16b, v17.16b, v7.16b
	add	v17.4s, v7.4s, v4.4s
	add	v6.4s, v17.4s, v6.4s
	ext	v17.16b, v18.16b, v18.16b, #4
	eor	v18.16b, v21.16b, v6.16b
	uzp1	v20.4s, v17.4s, v17.4s
	tbl	v18.16b, { v18.16b }, v2.16b
	ext	v20.16b, v20.16b, v17.16b, #8
	add	v19.4s, v19.4s, v18.4s
	uzp2	v20.4s, v20.4s, v5.4s
	ext	v18.16b, v18.16b, v18.16b, #8
	eor	v7.16b, v7.16b, v19.16b
	add	v6.4s, v6.4s, v20.4s
	ushr	v21.4s, v7.4s, #7
	shl	v7.4s, v7.4s, #25
	ext	v6.16b, v6.16b, v6.16b, #4
	orr	v7.16b, v7.16b, v21.16b
	add	v21.4s, v6.4s, v7.4s
	eor	v6.16b, v21.16b, v18.16b
	ext	v18.16b, v19.16b, v19.16b, #12
	tbl	v19.16b, { v6.16b }, v0.16b
	ext	v6.16b, v17.16b, v17.16b, #12
	add	v18.4s, v18.4s, v19.4s
	ext	v6.16b, v17.16b, v6.16b, #12
	mov	v17.16b, v4.16b
	eor	v7.16b, v7.16b, v18.16b
	rev64	v6.4s, v6.4s
	mov	v17.s[1], v16.s[2]
	ushr	v22.4s, v7.4s, #12
	shl	v7.4s, v7.4s, #20
	trn2	v6.4s, v6.4s, v17.4s
	orr	v7.16b, v7.16b, v22.16b
	add	v17.4s, v7.4s, v6.4s
	add	v17.4s, v17.4s, v21.4s
	zip1	v21.2d, v4.2d, v5.2d
	zip2	v4.4s, v5.4s, v4.4s
	eor	v19.16b, v19.16b, v17.16b
	mov	v21.s[3], v16.s[3]
	ext	v17.16b, v17.16b, v17.16b, #12
	tbl	v19.16b, { v19.16b }, v2.16b
	ext	v22.16b, v21.16b, v21.16b, #12
	add	v18.4s, v18.4s, v19.4s
	ext	v19.16b, v19.16b, v19.16b, #8
	eor	v7.16b, v7.16b, v18.16b
	ext	v18.16b, v18.16b, v18.16b, #4
	ushr	v23.4s, v7.4s, #7
	shl	v24.4s, v7.4s, #25
	uzp1	v7.4s, v21.4s, v22.4s
	orr	v21.16b, v24.16b, v23.16b
	add	v22.4s, v21.4s, v7.4s
	add	v17.4s, v22.4s, v17.4s
	eor	v19.16b, v17.16b, v19.16b
	tbl	v19.16b, { v19.16b }, v0.16b
	add	v18.4s, v18.4s, v19.4s
	eor	v5.16b, v21.16b, v18.16b
	zip1	v21.4s, v4.4s, v16.4s
	zip1	v4.4s, v16.4s, v4.4s
	ushr	v16.4s, v5.4s, #12
	shl	v5.4s, v5.4s, #20
	ext	v21.16b, v4.16b, v21.16b, #8
	orr	v4.16b, v5.16b, v16.16b
	ext	v16.16b, v20.16b, v20.16b, #4
	mov	v23.16b, v21.16b
	add	v5.4s, v4.4s, v21.4s
	mov	v23.s[1], v7.s[2]
	add	v5.4s, v5.4s, v17.4s
	eor	v17.16b, v19.16b, v5.16b
	uzp1	v19.4s, v16.4s, v16.4s
	tbl	v17.16b, { v17.16b }, v2.16b
	ext	v19.16b, v19.16b, v16.16b, #8
	add	v18.4s, v18.4s, v17.4s
	uzp2	v19.4s, v19.4s, v6.4s
	eor	v4.16b, v4.16b, v18.16b
	add	v5.4s, v5.4s, v19.4s
	ext	v19.16b, v19.16b, v19.16b, #4
	ushr	v20.4s, v4.4s, #7
	shl	v4.4s, v4.4s, #25
	ext	v5.16b, v5.16b, v5.16b, #4
	orr	v20.16b, v4.16b, v20.16b
	ext	v4.16b, v17.16b, v17.16b, #8
	add	v17.4s, v5.4s, v20.4s
	ext	v5.16b, v18.16b, v18.16b, #12
	eor	v4.16b, v17.16b, v4.16b
	tbl	v18.16b, { v4.16b }, v0.16b
	ext	v4.16b, v16.16b, v16.16b, #12
	add	v22.4s, v5.4s, v18.4s
	ext	v4.16b, v16.16b, v4.16b, #12
	eor	v5.16b, v20.16b, v22.16b
	rev64	v16.4s, v4.4s
	ushr	v20.4s, v5.4s, #12
	shl	v24.4s, v5.4s, #20
	trn2	v5.4s, v16.4s, v23.4s
	orr	v16.16b, v24.16b, v20.16b
	add	v20.4s, v16.4s, v5.4s
	add	v17.4s, v20.4s, v17.4s
	zip1	v20.2d, v21.2d, v6.2d
	zip2	v6.4s, v6.4s, v21.4s
	eor	v18.16b, v18.16b, v17.16b
	mov	v20.s[3], v7.s[3]
	ext	v17.16b, v17.16b, v17.16b, #12
	zip1	v21.4s, v6.4s, v7.4s
	tbl	v18.16b, { v18.16b }, v2.16b
	ext	v24.16b, v20.16b, v20.16b, #12
	zip1	v6.4s, v7.4s, v6.4s
	add	v22.4s, v22.4s, v18.4s
	ext	v18.16b, v18.16b, v18.16b, #8
	ext	v6.16b, v6.16b, v21.16b, #8
	eor	v16.16b, v16.16b, v22.16b
	ext	v22.16b, v22.16b, v22.16b, #4
	zip1	v5.2d, v6.2d, v5.2d
	zip2	v4.4s, v4.4s, v6.4s
	ushr	v25.4s, v16.4s, #7
	shl	v26.4s, v16.4s, #25
	uzp1	v16.4s, v20.4s, v24.4s
	orr	v20.16b, v26.16b, v25.16b
	mov	v5.s[3], v16.s[3]
	add	v24.4s, v20.4s, v16.4s
	add	v17.4s, v24.4s, v17.4s
	eor	v18.16b, v17.16b, v18.16b
	tbl	v18.16b, { v18.16b }, v0.16b
	add	v22.4s, v22.4s, v18.4s
	eor	v20.16b, v20.16b, v22.16b
	ushr	v7.4s, v20.4s, #12
	shl	v20.4s, v20.4s, #20
	orr	v7.16b, v20.16b, v7.16b
	add	v20.4s, v7.4s, v6.4s
	add	v17.4s, v20.4s, v17.4s
	ext	v20.16b, v19.16b, v19.16b, #8
	eor	v18.16b, v18.16b, v17.16b
	ext	v17.16b, v17.16b, v17.16b, #4
	tbl	v18.16b, { v18.16b }, v2.16b
	add	v21.4s, v22.4s, v18.4s
	uzp2	v22.4s, v20.4s, v23.4s
	ext	v18.16b, v18.16b, v18.16b, #8
	eor	v7.16b, v7.16b, v21.16b
	ext	v20.16b, v22.16b, v20.16b, #4
	ushr	v22.4s, v7.4s, #7
	shl	v7.4s, v7.4s, #25
	add	v17.4s, v17.4s, v20.4s
	ext	v20.16b, v21.16b, v21.16b, #12
	ext	v21.16b, v19.16b, v19.16b, #12
	orr	v7.16b, v7.16b, v22.16b
	ext	v19.16b, v19.16b, v21.16b, #12
	add	v17.4s, v17.4s, v7.4s
	mov	v21.16b, v6.16b
	rev64	v19.4s, v19.4s
	eor	v18.16b, v17.16b, v18.16b
	mov	v21.s[1], v16.s[2]
	tbl	v18.16b, { v18.16b }, v0.16b
	trn2	v19.4s, v19.4s, v21.4s
	add	v20.4s, v20.4s, v18.4s
	eor	v7.16b, v7.16b, v20.16b
	ushr	v22.4s, v7.4s, #12
	shl	v7.4s, v7.4s, #20
	orr	v7.16b, v7.16b, v22.16b
	add	v19.4s, v7.4s, v19.4s
	add	v17.4s, v19.4s, v17.4s
	eor	v18.16b, v18.16b, v17.16b
	ext	v17.16b, v17.16b, v17.16b, #12
	tbl	v18.16b, { v18.16b }, v2.16b
	add	v19.4s, v20.4s, v18.4s
	ext	v20.16b, v5.16b, v5.16b, #12
	ext	v18.16b, v18.16b, v18.16b, #8
	eor	v7.16b, v7.16b, v19.16b
	uzp1	v5.4s, v5.4s, v20.4s
	ushr	v21.4s, v7.4s, #7
	shl	v7.4s, v7.4s, #25
	orr	v7.16b, v7.16b, v21.16b
	add	v5.4s, v7.4s, v5.4s
	add	v5.4s, v5.4s, v17.4s
	eor	v17.16b, v5.16b, v18.16b
	ext	v18.16b, v19.16b, v19.16b, #4
	tbl	v17.16b, { v17.16b }, v0.16b
	add	v18.4s, v18.4s, v17.4s
	eor	v6.16b, v7.16b, v18.16b
	zip1	v7.4s, v4.4s, v16.4s
	zip1	v4.4s, v16.4s, v4.4s
	ushr	v16.4s, v6.4s, #12
	shl	v6.4s, v6.4s, #20
	ext	v4.16b, v4.16b, v7.16b, #8
	orr	v6.16b, v6.16b, v16.16b
	add	v4.4s, v6.4s, v4.4s
	add	v4.4s, v4.4s, v5.4s
	eor	v5.16b, v17.16b, v4.16b
	ext	v4.16b, v4.16b, v4.16b, #4
	tbl	v5.16b, { v5.16b }, v2.16b
	add	v7.4s, v18.4s, v5.4s
	eor	v6.16b, v6.16b, v7.16b
	ext	v7.16b, v7.16b, v7.16b, #12
	ushr	v16.4s, v6.4s, #7
	shl	v6.4s, v6.4s, #25
	orr	v6.16b, v6.16b, v16.16b
	ext	v16.16b, v5.16b, v5.16b, #8
	eor	v5.16b, v4.16b, v7.16b
	eor	v4.16b, v6.16b, v16.16b
.LBB3_11:
	subs	x13, x15, #1
	b.eq	.LBB3_9
	cbnz	x15, .LBB3_10
	add	x4, x4, x12
	add	x0, x0, #8
	subs	x1, x1, #1
	stp	q5, q4, [x8], #32
	b.ne	.LBB3_8
.LBB3_14:
	add	sp, sp, #368
	ldp	x20, x19, [sp, #128]
	ldp	x22, x21, [sp, #112]
	ldp	x24, x23, [sp, #96]
	ldp	x26, x25, [sp, #80]
	ldp	x29, x27, [sp, #64]
	ldp	d9, d8, [sp, #48]
	ldp	d11, d10, [sp, #32]
	ldp	d13, d12, [sp, #16]
	ldp	d15, d14, [sp], #144
	ret
.Lfunc_end3:
	.size	zfs_blake3_hash_many_sse41, .Lfunc_end3-zfs_blake3_hash_many_sse41
	.cfi_endproc
	.section	".note.GNU-stack","",@progbits
#endif