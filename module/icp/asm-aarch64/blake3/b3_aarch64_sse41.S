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
 * Copyright (c) 2022 Tino Reichardt <milky-zfs@mcmilk.de>
 *
 * This is converted assembly: SSE4.1 -> ARMv8-A
 * Used tools: SIMDe https://github.com/simd-everywhere/simde
 */

#if defined(__aarch64__)
	.text
	.section	.rodata.cst16,"aM",@progbits,16
	.p2align	4
.LCPI0_0:
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
.LCPI0_1:
	.word	1779033703
	.word	3144134277
	.word	1013904242
	.word	2773480762
.LCPI0_2:
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
.LCPI0_3:
	.byte	0
	.byte	1
	.byte	2
	.byte	3
	.byte	20
	.byte	21
	.byte	22
	.byte	23
	.byte	8
	.byte	9
	.byte	10
	.byte	11
	.byte	28
	.byte	29
	.byte	30
	.byte	31
.LCPI0_4:
	.byte	0
	.byte	1
	.byte	2
	.byte	3
	.byte	4
	.byte	5
	.byte	6
	.byte	7
	.byte	8
	.byte	9
	.byte	10
	.byte	11
	.byte	28
	.byte	29
	.byte	30
	.byte	31
	.text
	.globl	zfs_blake3_compress_in_place_sse41
	.p2align	2
	.type	zfs_blake3_compress_in_place_sse41,@function
zfs_blake3_compress_in_place_sse41:
	.cfi_startproc
	ldp	q7, q6, [x0]
	ldp	q17, q18, [x1]
	add	x12, x1, #32
	ld2	{ v4.4s, v5.4s }, [x12]
	lsr	x10, x3, #32
	fmov	s16, w3
	adrp	x13, .LCPI0_0
	adrp	x11, .LCPI0_1
	and	w8, w2, #0xff
	mov	v16.s[1], w10
	ldr	q0, [x13, :lo12:.LCPI0_0]
	ldr	q20, [x11, :lo12:.LCPI0_1]
	adrp	x11, .LCPI0_4
	and	w9, w4, #0xff
	ldr	q2, [x11, :lo12:.LCPI0_4]
	mov	v16.s[2], w8
	uzp1	v21.4s, v17.4s, v18.4s
	add	v7.4s, v6.4s, v7.4s
	adrp	x12, .LCPI0_3
	mov	v16.s[3], w9
	uzp2	v18.4s, v17.4s, v18.4s
	add	v7.4s, v7.4s, v21.4s
	ext	v17.16b, v5.16b, v5.16b, #12
	ldr	q3, [x12, :lo12:.LCPI0_3]
	ext	v24.16b, v4.16b, v4.16b, #12
	eor	v16.16b, v7.16b, v16.16b
	mov	v27.16b, v17.16b
	uzp1	v19.4s, v21.4s, v21.4s
	ext	v25.16b, v21.16b, v21.16b, #12
	zip2	v28.4s, v18.4s, v17.4s
	tbl	v29.16b, { v16.16b }, v0.16b
	mov	v27.s[1], v24.s[2]
	zip1	v23.2d, v17.2d, v18.2d
	ext	v19.16b, v19.16b, v21.16b, #8
	add	v22.4s, v29.4s, v20.4s
	ext	v26.16b, v21.16b, v25.16b, #12
	tbl	v20.16b, { v23.16b, v24.16b }, v2.16b
	zip1	v21.4s, v28.4s, v24.4s
	zip1	v23.4s, v24.4s, v28.4s
	uzp2	v19.4s, v19.4s, v18.4s
	eor	v24.16b, v22.16b, v6.16b
	ext	v25.16b, v20.16b, v20.16b, #12
	ext	v6.16b, v23.16b, v21.16b, #8
	add	v7.4s, v7.4s, v18.4s
	ext	v18.16b, v19.16b, v19.16b, #4
	tbl	v16.16b, { v26.16b, v27.16b }, v3.16b
	uzp1	v21.4s, v20.4s, v25.4s
	mov	v26.16b, v6.16b
	ext	v23.16b, v18.16b, v18.16b, #12
	mov	v26.s[1], v21.s[2]
	adrp	x10, .LCPI0_2
	ext	v25.16b, v18.16b, v23.16b, #12
	uzp1	v23.4s, v18.4s, v18.4s
	ldr	q1, [x10, :lo12:.LCPI0_2]
	ext	v18.16b, v23.16b, v18.16b, #8
	ushr	v23.4s, v24.4s, #12
	shl	v24.4s, v24.4s, #20
	orr	v23.16b, v24.16b, v23.16b
	add	v7.4s, v7.4s, v23.4s
	eor	v27.16b, v29.16b, v7.16b
	add	v4.4s, v7.4s, v4.4s
	tbl	v7.16b, { v25.16b, v26.16b }, v3.16b
	tbl	v26.16b, { v27.16b }, v1.16b
	add	v22.4s, v22.4s, v26.4s
	uzp2	v18.4s, v18.4s, v16.4s
	eor	v23.16b, v23.16b, v22.16b
	ext	v5.16b, v18.16b, v18.16b, #4
	ushr	v27.4s, v23.4s, #7
	shl	v23.4s, v23.4s, #25
	uzp1	v25.4s, v5.4s, v5.4s
	orr	v23.16b, v23.16b, v27.16b
	ext	v28.16b, v4.16b, v4.16b, #12
	ext	v4.16b, v25.16b, v5.16b, #8
	ext	v25.16b, v26.16b, v26.16b, #8
	add	v26.4s, v28.4s, v23.4s
	eor	v25.16b, v26.16b, v25.16b
	ext	v22.16b, v22.16b, v22.16b, #4
	tbl	v25.16b, { v25.16b }, v0.16b
	add	v22.4s, v22.4s, v25.4s
	eor	v23.16b, v23.16b, v22.16b
	add	v17.4s, v26.4s, v17.4s
	ushr	v26.4s, v23.4s, #12
	shl	v23.4s, v23.4s, #20
	orr	v23.16b, v23.16b, v26.16b
	add	v17.4s, v17.4s, v23.4s
	eor	v25.16b, v25.16b, v17.16b
	add	v17.4s, v17.4s, v19.4s
	tbl	v19.16b, { v25.16b }, v1.16b
	add	v22.4s, v22.4s, v19.4s
	eor	v23.16b, v23.16b, v22.16b
	ushr	v25.4s, v23.4s, #7
	shl	v23.4s, v23.4s, #25
	ext	v17.16b, v17.16b, v17.16b, #4
	orr	v23.16b, v23.16b, v25.16b
	ext	v19.16b, v19.16b, v19.16b, #8
	add	v17.4s, v17.4s, v23.4s
	eor	v19.16b, v17.16b, v19.16b
	ext	v22.16b, v22.16b, v22.16b, #12
	tbl	v19.16b, { v19.16b }, v0.16b
	add	v22.4s, v22.4s, v19.4s
	eor	v23.16b, v23.16b, v22.16b
	ushr	v25.4s, v23.4s, #12
	shl	v23.4s, v23.4s, #20
	add	v17.4s, v17.4s, v16.4s
	orr	v23.16b, v23.16b, v25.16b
	add	v17.4s, v17.4s, v23.4s
	ext	v25.16b, v17.16b, v17.16b, #12
	eor	v17.16b, v19.16b, v17.16b
	tbl	v17.16b, { v17.16b }, v1.16b
	add	v19.4s, v22.4s, v17.4s
	eor	v22.16b, v23.16b, v19.16b
	add	v25.4s, v25.4s, v21.4s
	zip1	v20.2d, v6.2d, v16.2d
	ushr	v23.4s, v22.4s, #7
	shl	v22.4s, v22.4s, #25
	zip2	v24.4s, v16.4s, v6.4s
	tbl	v26.16b, { v20.16b, v21.16b }, v2.16b
	orr	v22.16b, v22.16b, v23.16b
	zip1	v16.4s, v24.4s, v21.4s
	zip1	v20.4s, v21.4s, v24.4s
	ext	v21.16b, v26.16b, v26.16b, #12
	ext	v17.16b, v17.16b, v17.16b, #8
	add	v25.4s, v25.4s, v22.4s
	ext	v16.16b, v20.16b, v16.16b, #8
	uzp1	v21.4s, v26.4s, v21.4s
	eor	v26.16b, v25.16b, v17.16b
	ext	v19.16b, v19.16b, v19.16b, #4
	tbl	v26.16b, { v26.16b }, v0.16b
	mov	v29.16b, v16.16b
	add	v19.4s, v19.4s, v26.4s
	ext	v27.16b, v5.16b, v5.16b, #12
	mov	v29.s[1], v21.s[2]
	eor	v22.16b, v22.16b, v19.16b
	ext	v28.16b, v5.16b, v27.16b, #12
	ushr	v27.4s, v22.4s, #12
	shl	v22.4s, v22.4s, #20
	add	v6.4s, v25.4s, v6.4s
	orr	v22.16b, v22.16b, v27.16b
	add	v6.4s, v6.4s, v22.4s
	eor	v26.16b, v26.16b, v6.16b
	add	v6.4s, v6.4s, v18.4s
	tbl	v18.16b, { v26.16b }, v1.16b
	add	v19.4s, v19.4s, v18.4s
	eor	v22.16b, v22.16b, v19.16b
	ushr	v26.4s, v22.4s, #7
	shl	v22.4s, v22.4s, #25
	ext	v6.16b, v6.16b, v6.16b, #4
	orr	v22.16b, v22.16b, v26.16b
	ext	v18.16b, v18.16b, v18.16b, #8
	add	v6.4s, v6.4s, v22.4s
	eor	v18.16b, v6.16b, v18.16b
	ext	v19.16b, v19.16b, v19.16b, #12
	tbl	v18.16b, { v18.16b }, v0.16b
	add	v19.4s, v19.4s, v18.4s
	eor	v22.16b, v22.16b, v19.16b
	ushr	v26.4s, v22.4s, #12
	shl	v22.4s, v22.4s, #20
	add	v6.4s, v6.4s, v7.4s
	orr	v22.16b, v22.16b, v26.16b
	add	v6.4s, v6.4s, v22.4s
	ext	v26.16b, v6.16b, v6.16b, #12
	eor	v6.16b, v18.16b, v6.16b
	uzp2	v4.4s, v4.4s, v7.4s
	zip2	v25.4s, v7.4s, v16.4s
	add	v26.4s, v26.4s, v21.4s
	zip1	v20.2d, v16.2d, v7.2d
	tbl	v6.16b, { v6.16b }, v1.16b
	ext	v24.16b, v4.16b, v4.16b, #4
	tbl	v27.16b, { v20.16b, v21.16b }, v2.16b
	zip1	v7.4s, v25.4s, v21.4s
	zip1	v20.4s, v21.4s, v25.4s
	add	v18.4s, v19.4s, v6.4s
	uzp1	v5.4s, v24.4s, v24.4s
	ext	v21.16b, v27.16b, v27.16b, #12
	ext	v7.16b, v20.16b, v7.16b, #8
	eor	v19.16b, v22.16b, v18.16b
	ext	v5.16b, v5.16b, v24.16b, #8
	tbl	v17.16b, { v28.16b, v29.16b }, v3.16b
	uzp1	v21.4s, v27.4s, v21.4s
	mov	v28.16b, v7.16b
	ushr	v22.4s, v19.4s, #7
	shl	v19.4s, v19.4s, #25
	ext	v23.16b, v24.16b, v24.16b, #12
	uzp2	v5.4s, v5.4s, v17.4s
	mov	v28.s[1], v21.s[2]
	orr	v19.16b, v19.16b, v22.16b
	ext	v27.16b, v24.16b, v23.16b, #12
	ext	v23.16b, v5.16b, v5.16b, #4
	ext	v6.16b, v6.16b, v6.16b, #8
	ext	v25.16b, v18.16b, v18.16b, #4
	add	v18.4s, v26.4s, v19.4s
	uzp1	v24.4s, v23.4s, v23.4s
	eor	v6.16b, v18.16b, v6.16b
	ext	v24.16b, v24.16b, v23.16b, #8
	add	v16.4s, v18.4s, v16.4s
	tbl	v18.16b, { v27.16b, v28.16b }, v3.16b
	tbl	v27.16b, { v6.16b }, v0.16b
	uzp2	v6.4s, v24.4s, v18.4s
	add	v24.4s, v25.4s, v27.4s
	eor	v19.16b, v19.16b, v24.16b
	ushr	v25.4s, v19.4s, #12
	shl	v19.4s, v19.4s, #20
	orr	v19.16b, v19.16b, v25.16b
	add	v16.4s, v16.4s, v19.4s
	eor	v25.16b, v27.16b, v16.16b
	add	v4.4s, v16.4s, v4.4s
	tbl	v16.16b, { v25.16b }, v1.16b
	add	v24.4s, v24.4s, v16.4s
	eor	v19.16b, v19.16b, v24.16b
	ushr	v25.4s, v19.4s, #7
	shl	v19.4s, v19.4s, #25
	ext	v4.16b, v4.16b, v4.16b, #4
	orr	v19.16b, v19.16b, v25.16b
	ext	v16.16b, v16.16b, v16.16b, #8
	add	v4.4s, v4.4s, v19.4s
	eor	v16.16b, v4.16b, v16.16b
	ext	v24.16b, v24.16b, v24.16b, #12
	tbl	v25.16b, { v16.16b }, v0.16b
	add	v24.4s, v24.4s, v25.4s
	eor	v16.16b, v19.16b, v24.16b
	ushr	v19.4s, v16.4s, #12
	shl	v16.4s, v16.4s, #20
	add	v4.4s, v4.4s, v17.4s
	orr	v19.16b, v16.16b, v19.16b
	add	v27.4s, v4.4s, v19.4s
	eor	v25.16b, v25.16b, v27.16b
	tbl	v25.16b, { v25.16b }, v1.16b
	add	v24.4s, v24.4s, v25.4s
	zip2	v26.4s, v17.4s, v7.4s
	ext	v4.16b, v27.16b, v27.16b, #12
	eor	v19.16b, v19.16b, v24.16b
	add	v28.4s, v4.4s, v21.4s
	zip1	v20.2d, v7.2d, v17.2d
	zip1	v4.4s, v26.4s, v21.4s
	zip1	v17.4s, v21.4s, v26.4s
	ushr	v26.4s, v19.4s, #7
	shl	v19.4s, v19.4s, #25
	orr	v19.16b, v19.16b, v26.16b
	ext	v25.16b, v25.16b, v25.16b, #8
	add	v27.4s, v28.4s, v19.4s
	eor	v25.16b, v27.16b, v25.16b
	ext	v24.16b, v24.16b, v24.16b, #4
	tbl	v25.16b, { v25.16b }, v0.16b
	add	v24.4s, v24.4s, v25.4s
	eor	v19.16b, v19.16b, v24.16b
	add	v7.4s, v27.4s, v7.4s
	ushr	v27.4s, v19.4s, #12
	shl	v19.4s, v19.4s, #20
	orr	v19.16b, v19.16b, v27.16b
	add	v7.4s, v7.4s, v19.4s
	eor	v25.16b, v25.16b, v7.16b
	add	v5.4s, v7.4s, v5.4s
	tbl	v7.16b, { v25.16b }, v1.16b
	add	v24.4s, v24.4s, v7.4s
	eor	v19.16b, v19.16b, v24.16b
	ushr	v25.4s, v19.4s, #7
	shl	v19.4s, v19.4s, #25
	ext	v5.16b, v5.16b, v5.16b, #4
	orr	v19.16b, v19.16b, v25.16b
	ext	v7.16b, v7.16b, v7.16b, #8
	add	v5.4s, v5.4s, v19.4s
	eor	v7.16b, v5.16b, v7.16b
	ext	v24.16b, v24.16b, v24.16b, #12
	tbl	v7.16b, { v7.16b }, v0.16b
	add	v24.4s, v24.4s, v7.4s
	eor	v19.16b, v19.16b, v24.16b
	ushr	v25.4s, v19.4s, #12
	shl	v19.4s, v19.4s, #20
	tbl	v16.16b, { v20.16b, v21.16b }, v2.16b
	add	v5.4s, v5.4s, v18.4s
	orr	v19.16b, v19.16b, v25.16b
	ext	v20.16b, v16.16b, v16.16b, #12
	ext	v4.16b, v17.16b, v4.16b, #8
	add	v5.4s, v5.4s, v19.4s
	uzp1	v21.4s, v16.4s, v20.4s
	mov	v17.16b, v4.16b
	ext	v25.16b, v5.16b, v5.16b, #12
	mov	v17.s[1], v21.s[2]
	add	v25.4s, v25.4s, v21.4s
	zip1	v20.2d, v4.2d, v18.2d
	ext	v22.16b, v23.16b, v23.16b, #12
	zip2	v26.4s, v18.4s, v4.4s
	tbl	v18.16b, { v20.16b, v21.16b }, v2.16b
	eor	v5.16b, v7.16b, v5.16b
	ext	v16.16b, v23.16b, v22.16b, #12
	ext	v22.16b, v6.16b, v6.16b, #4
	zip1	v27.4s, v26.4s, v21.4s
	zip1	v20.4s, v21.4s, v26.4s
	ext	v21.16b, v18.16b, v18.16b, #12
	tbl	v5.16b, { v5.16b }, v1.16b
	ext	v20.16b, v20.16b, v27.16b, #8
	uzp1	v27.4s, v18.4s, v21.4s
	uzp1	v18.4s, v22.4s, v22.4s
	add	v21.4s, v24.4s, v5.4s
	ext	v18.16b, v18.16b, v22.16b, #8
	eor	v19.16b, v19.16b, v21.16b
	tbl	v7.16b, { v16.16b, v17.16b }, v3.16b
	uzp2	v18.4s, v18.4s, v17.4s
	zip2	v16.4s, v16.4s, v20.4s
	ushr	v17.4s, v19.4s, #7
	shl	v19.4s, v19.4s, #25
	orr	v17.16b, v19.16b, v17.16b
	ext	v5.16b, v5.16b, v5.16b, #8
	add	v19.4s, v25.4s, v17.4s
	eor	v5.16b, v19.16b, v5.16b
	ext	v21.16b, v21.16b, v21.16b, #4
	tbl	v5.16b, { v5.16b }, v0.16b
	add	v4.4s, v19.4s, v4.4s
	add	v19.4s, v21.4s, v5.4s
	eor	v17.16b, v17.16b, v19.16b
	ushr	v21.4s, v17.4s, #12
	shl	v17.4s, v17.4s, #20
	orr	v17.16b, v17.16b, v21.16b
	add	v4.4s, v4.4s, v17.4s
	eor	v5.16b, v5.16b, v4.16b
	tbl	v5.16b, { v5.16b }, v1.16b
	add	v4.4s, v4.4s, v6.4s
	add	v6.4s, v19.4s, v5.4s
	eor	v17.16b, v17.16b, v6.16b
	ushr	v19.4s, v17.4s, #7
	shl	v17.4s, v17.4s, #25
	ext	v4.16b, v4.16b, v4.16b, #4
	orr	v17.16b, v17.16b, v19.16b
	ext	v5.16b, v5.16b, v5.16b, #8
	add	v4.4s, v4.4s, v17.4s
	eor	v5.16b, v4.16b, v5.16b
	ext	v6.16b, v6.16b, v6.16b, #12
	tbl	v5.16b, { v5.16b }, v0.16b
	add	v6.4s, v6.4s, v5.4s
	eor	v17.16b, v17.16b, v6.16b
	ushr	v19.4s, v17.4s, #12
	shl	v17.4s, v17.4s, #20
	add	v4.4s, v4.4s, v7.4s
	orr	v17.16b, v17.16b, v19.16b
	add	v4.4s, v4.4s, v17.4s
	eor	v5.16b, v5.16b, v4.16b
	tbl	v5.16b, { v5.16b }, v1.16b
	mov	v29.16b, v20.16b
	ext	v4.16b, v4.16b, v4.16b, #12
	add	v6.4s, v6.4s, v5.4s
	mov	v29.s[1], v27.s[2]
	add	v4.4s, v4.4s, v27.4s
	zip1	v26.2d, v20.2d, v7.2d
	zip1	v7.4s, v16.4s, v27.4s
	zip1	v16.4s, v27.4s, v16.4s
	eor	v17.16b, v17.16b, v6.16b
	ext	v7.16b, v16.16b, v7.16b, #8
	ushr	v16.4s, v17.4s, #7
	shl	v17.4s, v17.4s, #25
	orr	v16.16b, v17.16b, v16.16b
	ext	v5.16b, v5.16b, v5.16b, #8
	add	v4.4s, v4.4s, v16.4s
	eor	v5.16b, v4.16b, v5.16b
	ext	v6.16b, v6.16b, v6.16b, #4
	tbl	v5.16b, { v5.16b }, v0.16b
	add	v6.4s, v6.4s, v5.4s
	eor	v16.16b, v16.16b, v6.16b
	ushr	v17.4s, v16.4s, #12
	shl	v16.4s, v16.4s, #20
	add	v4.4s, v4.4s, v20.4s
	orr	v16.16b, v16.16b, v17.16b
	add	v4.4s, v4.4s, v16.4s
	eor	v5.16b, v5.16b, v4.16b
	tbl	v5.16b, { v5.16b }, v1.16b
	add	v6.4s, v6.4s, v5.4s
	eor	v16.16b, v16.16b, v6.16b
	add	v4.4s, v4.4s, v18.4s
	ushr	v17.4s, v16.4s, #7
	shl	v16.4s, v16.4s, #25
	ext	v23.16b, v22.16b, v22.16b, #12
	ext	v4.16b, v4.16b, v4.16b, #4
	orr	v16.16b, v16.16b, v17.16b
	ext	v28.16b, v22.16b, v23.16b, #12
	ext	v5.16b, v5.16b, v5.16b, #8
	add	v4.4s, v16.4s, v4.4s
	tbl	v3.16b, { v28.16b, v29.16b }, v3.16b
	eor	v5.16b, v4.16b, v5.16b
	ext	v6.16b, v6.16b, v6.16b, #12
	add	v3.4s, v4.4s, v3.4s
	tbl	v4.16b, { v5.16b }, v0.16b
	add	v5.4s, v6.4s, v4.4s
	eor	v6.16b, v16.16b, v5.16b
	ushr	v16.4s, v6.4s, #12
	shl	v6.4s, v6.4s, #20
	orr	v6.16b, v6.16b, v16.16b
	tbl	v2.16b, { v26.16b, v27.16b }, v2.16b
	add	v3.4s, v3.4s, v6.4s
	ext	v19.16b, v2.16b, v2.16b, #12
	eor	v4.16b, v4.16b, v3.16b
	uzp1	v2.4s, v2.4s, v19.4s
	ext	v3.16b, v3.16b, v3.16b, #12
	tbl	v4.16b, { v4.16b }, v1.16b
	add	v2.4s, v3.4s, v2.4s
	add	v3.4s, v5.4s, v4.4s
	eor	v5.16b, v6.16b, v3.16b
	ushr	v6.4s, v5.4s, #7
	shl	v5.4s, v5.4s, #25
	orr	v5.16b, v5.16b, v6.16b
	ext	v4.16b, v4.16b, v4.16b, #8
	add	v2.4s, v2.4s, v5.4s
	eor	v4.16b, v2.16b, v4.16b
	ext	v3.16b, v3.16b, v3.16b, #4
	tbl	v0.16b, { v4.16b }, v0.16b
	add	v3.4s, v3.4s, v0.4s
	eor	v4.16b, v5.16b, v3.16b
	ushr	v5.4s, v4.4s, #12
	shl	v4.4s, v4.4s, #20
	add	v2.4s, v2.4s, v7.4s
	orr	v4.16b, v4.16b, v5.16b
	add	v2.4s, v2.4s, v4.4s
	eor	v0.16b, v0.16b, v2.16b
	tbl	v0.16b, { v0.16b }, v1.16b
	add	v1.4s, v3.4s, v0.4s
	eor	v3.16b, v4.16b, v1.16b
	ext	v2.16b, v2.16b, v2.16b, #4
	ext	v1.16b, v1.16b, v1.16b, #12
	ushr	v4.4s, v3.4s, #7
	shl	v3.4s, v3.4s, #25
	ext	v0.16b, v0.16b, v0.16b, #8
	eor	v1.16b, v2.16b, v1.16b
	orr	v2.16b, v3.16b, v4.16b
	eor	v0.16b, v2.16b, v0.16b
	stp	q1, q0, [x0]
	ret
.Lfunc_end0:
	.size	zfs_blake3_compress_in_place_sse41, .Lfunc_end0-zfs_blake3_compress_in_place_sse41
	.cfi_endproc

	.section	.rodata.cst16,"aM",@progbits,16
	.p2align	4
.LCPI1_0:
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
.LCPI1_1:
	.word	1779033703
	.word	3144134277
	.word	1013904242
	.word	2773480762
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
.LCPI1_3:
	.byte	0
	.byte	1
	.byte	2
	.byte	3
	.byte	20
	.byte	21
	.byte	22
	.byte	23
	.byte	8
	.byte	9
	.byte	10
	.byte	11
	.byte	28
	.byte	29
	.byte	30
	.byte	31
.LCPI1_4:
	.byte	0
	.byte	1
	.byte	2
	.byte	3
	.byte	4
	.byte	5
	.byte	6
	.byte	7
	.byte	8
	.byte	9
	.byte	10
	.byte	11
	.byte	28
	.byte	29
	.byte	30
	.byte	31
	.text
	.globl	zfs_blake3_compress_xof_sse41
	.p2align	2
	.type	zfs_blake3_compress_xof_sse41,@function
zfs_blake3_compress_xof_sse41:
	.cfi_startproc
	ldp	q7, q6, [x0]
	ldp	q17, q18, [x1]
	add	x12, x1, #32
	ld2	{ v4.4s, v5.4s }, [x12]
	lsr	x10, x3, #32
	fmov	s16, w3
	adrp	x13, .LCPI1_0
	adrp	x11, .LCPI1_1
	and	w8, w2, #0xff
	mov	v16.s[1], w10
	ldr	q0, [x13, :lo12:.LCPI1_0]
	ldr	q20, [x11, :lo12:.LCPI1_1]
	adrp	x11, .LCPI1_4
	and	w9, w4, #0xff
	ldr	q2, [x11, :lo12:.LCPI1_4]
	mov	v16.s[2], w8
	uzp1	v21.4s, v17.4s, v18.4s
	add	v7.4s, v6.4s, v7.4s
	adrp	x12, .LCPI1_3
	mov	v16.s[3], w9
	uzp2	v18.4s, v17.4s, v18.4s
	add	v7.4s, v7.4s, v21.4s
	ext	v17.16b, v5.16b, v5.16b, #12
	ldr	q3, [x12, :lo12:.LCPI1_3]
	ext	v24.16b, v4.16b, v4.16b, #12
	eor	v16.16b, v7.16b, v16.16b
	mov	v27.16b, v17.16b
	uzp1	v19.4s, v21.4s, v21.4s
	ext	v25.16b, v21.16b, v21.16b, #12
	zip2	v28.4s, v18.4s, v17.4s
	tbl	v29.16b, { v16.16b }, v0.16b
	mov	v27.s[1], v24.s[2]
	zip1	v23.2d, v17.2d, v18.2d
	ext	v19.16b, v19.16b, v21.16b, #8
	add	v22.4s, v29.4s, v20.4s
	ext	v26.16b, v21.16b, v25.16b, #12
	tbl	v20.16b, { v23.16b, v24.16b }, v2.16b
	zip1	v21.4s, v28.4s, v24.4s
	zip1	v23.4s, v24.4s, v28.4s
	uzp2	v19.4s, v19.4s, v18.4s
	eor	v24.16b, v22.16b, v6.16b
	ext	v25.16b, v20.16b, v20.16b, #12
	ext	v6.16b, v23.16b, v21.16b, #8
	add	v7.4s, v7.4s, v18.4s
	ext	v18.16b, v19.16b, v19.16b, #4
	tbl	v16.16b, { v26.16b, v27.16b }, v3.16b
	uzp1	v21.4s, v20.4s, v25.4s
	mov	v26.16b, v6.16b
	ext	v23.16b, v18.16b, v18.16b, #12
	mov	v26.s[1], v21.s[2]
	adrp	x10, .LCPI1_2
	ext	v25.16b, v18.16b, v23.16b, #12
	uzp1	v23.4s, v18.4s, v18.4s
	ldr	q1, [x10, :lo12:.LCPI1_2]
	ext	v18.16b, v23.16b, v18.16b, #8
	ushr	v23.4s, v24.4s, #12
	shl	v24.4s, v24.4s, #20
	orr	v23.16b, v24.16b, v23.16b
	add	v7.4s, v7.4s, v23.4s
	eor	v27.16b, v29.16b, v7.16b
	add	v4.4s, v7.4s, v4.4s
	tbl	v7.16b, { v25.16b, v26.16b }, v3.16b
	tbl	v26.16b, { v27.16b }, v1.16b
	add	v22.4s, v22.4s, v26.4s
	uzp2	v18.4s, v18.4s, v16.4s
	eor	v23.16b, v23.16b, v22.16b
	ext	v5.16b, v18.16b, v18.16b, #4
	ushr	v27.4s, v23.4s, #7
	shl	v23.4s, v23.4s, #25
	uzp1	v25.4s, v5.4s, v5.4s
	orr	v23.16b, v23.16b, v27.16b
	ext	v28.16b, v4.16b, v4.16b, #12
	ext	v4.16b, v25.16b, v5.16b, #8
	ext	v25.16b, v26.16b, v26.16b, #8
	add	v26.4s, v28.4s, v23.4s
	eor	v25.16b, v26.16b, v25.16b
	ext	v22.16b, v22.16b, v22.16b, #4
	tbl	v25.16b, { v25.16b }, v0.16b
	add	v22.4s, v22.4s, v25.4s
	eor	v23.16b, v23.16b, v22.16b
	add	v17.4s, v26.4s, v17.4s
	ushr	v26.4s, v23.4s, #12
	shl	v23.4s, v23.4s, #20
	orr	v23.16b, v23.16b, v26.16b
	add	v17.4s, v17.4s, v23.4s
	eor	v25.16b, v25.16b, v17.16b
	add	v17.4s, v17.4s, v19.4s
	tbl	v19.16b, { v25.16b }, v1.16b
	add	v22.4s, v22.4s, v19.4s
	eor	v23.16b, v23.16b, v22.16b
	ushr	v25.4s, v23.4s, #7
	shl	v23.4s, v23.4s, #25
	ext	v17.16b, v17.16b, v17.16b, #4
	orr	v23.16b, v23.16b, v25.16b
	ext	v19.16b, v19.16b, v19.16b, #8
	add	v17.4s, v17.4s, v23.4s
	eor	v19.16b, v17.16b, v19.16b
	ext	v22.16b, v22.16b, v22.16b, #12
	tbl	v19.16b, { v19.16b }, v0.16b
	add	v22.4s, v22.4s, v19.4s
	eor	v23.16b, v23.16b, v22.16b
	ushr	v25.4s, v23.4s, #12
	shl	v23.4s, v23.4s, #20
	add	v17.4s, v17.4s, v16.4s
	orr	v23.16b, v23.16b, v25.16b
	add	v17.4s, v17.4s, v23.4s
	ext	v25.16b, v17.16b, v17.16b, #12
	eor	v17.16b, v19.16b, v17.16b
	tbl	v17.16b, { v17.16b }, v1.16b
	add	v19.4s, v22.4s, v17.4s
	eor	v22.16b, v23.16b, v19.16b
	add	v25.4s, v25.4s, v21.4s
	zip1	v20.2d, v6.2d, v16.2d
	ushr	v23.4s, v22.4s, #7
	shl	v22.4s, v22.4s, #25
	zip2	v24.4s, v16.4s, v6.4s
	tbl	v26.16b, { v20.16b, v21.16b }, v2.16b
	orr	v22.16b, v22.16b, v23.16b
	zip1	v16.4s, v24.4s, v21.4s
	zip1	v20.4s, v21.4s, v24.4s
	ext	v21.16b, v26.16b, v26.16b, #12
	ext	v17.16b, v17.16b, v17.16b, #8
	add	v25.4s, v25.4s, v22.4s
	ext	v16.16b, v20.16b, v16.16b, #8
	uzp1	v21.4s, v26.4s, v21.4s
	eor	v26.16b, v25.16b, v17.16b
	ext	v19.16b, v19.16b, v19.16b, #4
	tbl	v26.16b, { v26.16b }, v0.16b
	mov	v29.16b, v16.16b
	add	v19.4s, v19.4s, v26.4s
	ext	v27.16b, v5.16b, v5.16b, #12
	mov	v29.s[1], v21.s[2]
	eor	v22.16b, v22.16b, v19.16b
	ext	v28.16b, v5.16b, v27.16b, #12
	ushr	v27.4s, v22.4s, #12
	shl	v22.4s, v22.4s, #20
	add	v6.4s, v25.4s, v6.4s
	orr	v22.16b, v22.16b, v27.16b
	add	v6.4s, v6.4s, v22.4s
	eor	v26.16b, v26.16b, v6.16b
	add	v6.4s, v6.4s, v18.4s
	tbl	v18.16b, { v26.16b }, v1.16b
	add	v19.4s, v19.4s, v18.4s
	eor	v22.16b, v22.16b, v19.16b
	ushr	v26.4s, v22.4s, #7
	shl	v22.4s, v22.4s, #25
	ext	v6.16b, v6.16b, v6.16b, #4
	orr	v22.16b, v22.16b, v26.16b
	ext	v18.16b, v18.16b, v18.16b, #8
	add	v6.4s, v6.4s, v22.4s
	eor	v18.16b, v6.16b, v18.16b
	ext	v19.16b, v19.16b, v19.16b, #12
	tbl	v18.16b, { v18.16b }, v0.16b
	add	v19.4s, v19.4s, v18.4s
	eor	v22.16b, v22.16b, v19.16b
	ushr	v26.4s, v22.4s, #12
	shl	v22.4s, v22.4s, #20
	add	v6.4s, v6.4s, v7.4s
	orr	v22.16b, v22.16b, v26.16b
	add	v6.4s, v6.4s, v22.4s
	ext	v26.16b, v6.16b, v6.16b, #12
	eor	v6.16b, v18.16b, v6.16b
	uzp2	v4.4s, v4.4s, v7.4s
	zip2	v25.4s, v7.4s, v16.4s
	add	v26.4s, v26.4s, v21.4s
	zip1	v20.2d, v16.2d, v7.2d
	tbl	v6.16b, { v6.16b }, v1.16b
	ext	v24.16b, v4.16b, v4.16b, #4
	tbl	v27.16b, { v20.16b, v21.16b }, v2.16b
	zip1	v7.4s, v25.4s, v21.4s
	zip1	v20.4s, v21.4s, v25.4s
	add	v18.4s, v19.4s, v6.4s
	uzp1	v5.4s, v24.4s, v24.4s
	ext	v21.16b, v27.16b, v27.16b, #12
	ext	v7.16b, v20.16b, v7.16b, #8
	eor	v19.16b, v22.16b, v18.16b
	ext	v5.16b, v5.16b, v24.16b, #8
	tbl	v17.16b, { v28.16b, v29.16b }, v3.16b
	uzp1	v21.4s, v27.4s, v21.4s
	mov	v28.16b, v7.16b
	ushr	v22.4s, v19.4s, #7
	shl	v19.4s, v19.4s, #25
	ext	v23.16b, v24.16b, v24.16b, #12
	uzp2	v5.4s, v5.4s, v17.4s
	mov	v28.s[1], v21.s[2]
	orr	v19.16b, v19.16b, v22.16b
	ext	v27.16b, v24.16b, v23.16b, #12
	ext	v23.16b, v5.16b, v5.16b, #4
	ext	v6.16b, v6.16b, v6.16b, #8
	ext	v25.16b, v18.16b, v18.16b, #4
	add	v18.4s, v26.4s, v19.4s
	uzp1	v24.4s, v23.4s, v23.4s
	eor	v6.16b, v18.16b, v6.16b
	ext	v24.16b, v24.16b, v23.16b, #8
	add	v16.4s, v18.4s, v16.4s
	tbl	v18.16b, { v27.16b, v28.16b }, v3.16b
	tbl	v27.16b, { v6.16b }, v0.16b
	uzp2	v6.4s, v24.4s, v18.4s
	add	v24.4s, v25.4s, v27.4s
	eor	v19.16b, v19.16b, v24.16b
	ushr	v25.4s, v19.4s, #12
	shl	v19.4s, v19.4s, #20
	orr	v19.16b, v19.16b, v25.16b
	add	v16.4s, v16.4s, v19.4s
	eor	v25.16b, v27.16b, v16.16b
	add	v4.4s, v16.4s, v4.4s
	tbl	v16.16b, { v25.16b }, v1.16b
	add	v24.4s, v24.4s, v16.4s
	eor	v19.16b, v19.16b, v24.16b
	ushr	v25.4s, v19.4s, #7
	shl	v19.4s, v19.4s, #25
	ext	v4.16b, v4.16b, v4.16b, #4
	orr	v19.16b, v19.16b, v25.16b
	ext	v16.16b, v16.16b, v16.16b, #8
	add	v4.4s, v4.4s, v19.4s
	eor	v16.16b, v4.16b, v16.16b
	ext	v24.16b, v24.16b, v24.16b, #12
	tbl	v25.16b, { v16.16b }, v0.16b
	add	v24.4s, v24.4s, v25.4s
	eor	v16.16b, v19.16b, v24.16b
	ushr	v19.4s, v16.4s, #12
	shl	v16.4s, v16.4s, #20
	add	v4.4s, v4.4s, v17.4s
	orr	v19.16b, v16.16b, v19.16b
	add	v27.4s, v4.4s, v19.4s
	eor	v25.16b, v25.16b, v27.16b
	tbl	v25.16b, { v25.16b }, v1.16b
	add	v24.4s, v24.4s, v25.4s
	zip2	v26.4s, v17.4s, v7.4s
	ext	v4.16b, v27.16b, v27.16b, #12
	eor	v19.16b, v19.16b, v24.16b
	add	v28.4s, v4.4s, v21.4s
	zip1	v20.2d, v7.2d, v17.2d
	zip1	v4.4s, v26.4s, v21.4s
	zip1	v17.4s, v21.4s, v26.4s
	ushr	v26.4s, v19.4s, #7
	shl	v19.4s, v19.4s, #25
	orr	v19.16b, v19.16b, v26.16b
	ext	v25.16b, v25.16b, v25.16b, #8
	add	v27.4s, v28.4s, v19.4s
	eor	v25.16b, v27.16b, v25.16b
	ext	v24.16b, v24.16b, v24.16b, #4
	tbl	v25.16b, { v25.16b }, v0.16b
	add	v24.4s, v24.4s, v25.4s
	eor	v19.16b, v19.16b, v24.16b
	add	v7.4s, v27.4s, v7.4s
	ushr	v27.4s, v19.4s, #12
	shl	v19.4s, v19.4s, #20
	orr	v19.16b, v19.16b, v27.16b
	add	v7.4s, v7.4s, v19.4s
	eor	v25.16b, v25.16b, v7.16b
	add	v5.4s, v7.4s, v5.4s
	tbl	v7.16b, { v25.16b }, v1.16b
	add	v24.4s, v24.4s, v7.4s
	eor	v19.16b, v19.16b, v24.16b
	ushr	v25.4s, v19.4s, #7
	shl	v19.4s, v19.4s, #25
	ext	v5.16b, v5.16b, v5.16b, #4
	orr	v19.16b, v19.16b, v25.16b
	ext	v7.16b, v7.16b, v7.16b, #8
	add	v5.4s, v5.4s, v19.4s
	eor	v7.16b, v5.16b, v7.16b
	ext	v24.16b, v24.16b, v24.16b, #12
	tbl	v7.16b, { v7.16b }, v0.16b
	add	v24.4s, v24.4s, v7.4s
	eor	v19.16b, v19.16b, v24.16b
	ushr	v25.4s, v19.4s, #12
	shl	v19.4s, v19.4s, #20
	tbl	v16.16b, { v20.16b, v21.16b }, v2.16b
	add	v5.4s, v5.4s, v18.4s
	orr	v19.16b, v19.16b, v25.16b
	ext	v20.16b, v16.16b, v16.16b, #12
	ext	v4.16b, v17.16b, v4.16b, #8
	add	v5.4s, v5.4s, v19.4s
	uzp1	v21.4s, v16.4s, v20.4s
	mov	v17.16b, v4.16b
	ext	v25.16b, v5.16b, v5.16b, #12
	mov	v17.s[1], v21.s[2]
	add	v25.4s, v25.4s, v21.4s
	zip1	v20.2d, v4.2d, v18.2d
	ext	v22.16b, v23.16b, v23.16b, #12
	zip2	v26.4s, v18.4s, v4.4s
	tbl	v18.16b, { v20.16b, v21.16b }, v2.16b
	eor	v5.16b, v7.16b, v5.16b
	ext	v16.16b, v23.16b, v22.16b, #12
	ext	v22.16b, v6.16b, v6.16b, #4
	zip1	v27.4s, v26.4s, v21.4s
	zip1	v20.4s, v21.4s, v26.4s
	ext	v21.16b, v18.16b, v18.16b, #12
	tbl	v5.16b, { v5.16b }, v1.16b
	ext	v20.16b, v20.16b, v27.16b, #8
	uzp1	v27.4s, v18.4s, v21.4s
	uzp1	v18.4s, v22.4s, v22.4s
	add	v21.4s, v24.4s, v5.4s
	ext	v18.16b, v18.16b, v22.16b, #8
	eor	v19.16b, v19.16b, v21.16b
	tbl	v7.16b, { v16.16b, v17.16b }, v3.16b
	uzp2	v18.4s, v18.4s, v17.4s
	zip2	v16.4s, v16.4s, v20.4s
	ushr	v17.4s, v19.4s, #7
	shl	v19.4s, v19.4s, #25
	orr	v17.16b, v19.16b, v17.16b
	ext	v5.16b, v5.16b, v5.16b, #8
	add	v19.4s, v25.4s, v17.4s
	eor	v5.16b, v19.16b, v5.16b
	ext	v21.16b, v21.16b, v21.16b, #4
	tbl	v5.16b, { v5.16b }, v0.16b
	add	v4.4s, v19.4s, v4.4s
	add	v19.4s, v21.4s, v5.4s
	eor	v17.16b, v17.16b, v19.16b
	ushr	v21.4s, v17.4s, #12
	shl	v17.4s, v17.4s, #20
	orr	v17.16b, v17.16b, v21.16b
	add	v4.4s, v4.4s, v17.4s
	eor	v5.16b, v5.16b, v4.16b
	tbl	v5.16b, { v5.16b }, v1.16b
	add	v4.4s, v4.4s, v6.4s
	add	v6.4s, v19.4s, v5.4s
	eor	v17.16b, v17.16b, v6.16b
	ushr	v19.4s, v17.4s, #7
	shl	v17.4s, v17.4s, #25
	ext	v4.16b, v4.16b, v4.16b, #4
	orr	v17.16b, v17.16b, v19.16b
	ext	v5.16b, v5.16b, v5.16b, #8
	add	v4.4s, v4.4s, v17.4s
	eor	v5.16b, v4.16b, v5.16b
	ext	v6.16b, v6.16b, v6.16b, #12
	tbl	v5.16b, { v5.16b }, v0.16b
	add	v6.4s, v6.4s, v5.4s
	eor	v17.16b, v17.16b, v6.16b
	ushr	v19.4s, v17.4s, #12
	shl	v17.4s, v17.4s, #20
	add	v4.4s, v4.4s, v7.4s
	orr	v17.16b, v17.16b, v19.16b
	add	v4.4s, v4.4s, v17.4s
	eor	v5.16b, v5.16b, v4.16b
	tbl	v5.16b, { v5.16b }, v1.16b
	mov	v29.16b, v20.16b
	ext	v4.16b, v4.16b, v4.16b, #12
	add	v6.4s, v6.4s, v5.4s
	mov	v29.s[1], v27.s[2]
	add	v4.4s, v4.4s, v27.4s
	zip1	v26.2d, v20.2d, v7.2d
	zip1	v7.4s, v16.4s, v27.4s
	zip1	v16.4s, v27.4s, v16.4s
	eor	v17.16b, v17.16b, v6.16b
	ext	v7.16b, v16.16b, v7.16b, #8
	ushr	v16.4s, v17.4s, #7
	shl	v17.4s, v17.4s, #25
	orr	v16.16b, v17.16b, v16.16b
	ext	v5.16b, v5.16b, v5.16b, #8
	add	v4.4s, v4.4s, v16.4s
	eor	v5.16b, v4.16b, v5.16b
	ext	v6.16b, v6.16b, v6.16b, #4
	tbl	v5.16b, { v5.16b }, v0.16b
	add	v6.4s, v6.4s, v5.4s
	eor	v16.16b, v16.16b, v6.16b
	ushr	v17.4s, v16.4s, #12
	shl	v16.4s, v16.4s, #20
	add	v4.4s, v4.4s, v20.4s
	orr	v16.16b, v16.16b, v17.16b
	add	v4.4s, v4.4s, v16.4s
	eor	v5.16b, v5.16b, v4.16b
	tbl	v5.16b, { v5.16b }, v1.16b
	add	v6.4s, v6.4s, v5.4s
	eor	v16.16b, v16.16b, v6.16b
	add	v4.4s, v4.4s, v18.4s
	ushr	v17.4s, v16.4s, #7
	shl	v16.4s, v16.4s, #25
	ext	v23.16b, v22.16b, v22.16b, #12
	ext	v4.16b, v4.16b, v4.16b, #4
	orr	v16.16b, v16.16b, v17.16b
	ext	v28.16b, v22.16b, v23.16b, #12
	ext	v5.16b, v5.16b, v5.16b, #8
	add	v4.4s, v16.4s, v4.4s
	tbl	v3.16b, { v28.16b, v29.16b }, v3.16b
	eor	v5.16b, v4.16b, v5.16b
	ext	v6.16b, v6.16b, v6.16b, #12
	add	v3.4s, v4.4s, v3.4s
	tbl	v4.16b, { v5.16b }, v0.16b
	add	v5.4s, v6.4s, v4.4s
	eor	v6.16b, v16.16b, v5.16b
	ushr	v16.4s, v6.4s, #12
	shl	v6.4s, v6.4s, #20
	orr	v6.16b, v6.16b, v16.16b
	tbl	v2.16b, { v26.16b, v27.16b }, v2.16b
	add	v3.4s, v3.4s, v6.4s
	ext	v19.16b, v2.16b, v2.16b, #12
	eor	v4.16b, v4.16b, v3.16b
	uzp1	v2.4s, v2.4s, v19.4s
	ext	v3.16b, v3.16b, v3.16b, #12
	tbl	v4.16b, { v4.16b }, v1.16b
	add	v2.4s, v3.4s, v2.4s
	add	v3.4s, v5.4s, v4.4s
	eor	v5.16b, v6.16b, v3.16b
	ushr	v6.4s, v5.4s, #7
	shl	v5.4s, v5.4s, #25
	orr	v5.16b, v5.16b, v6.16b
	ext	v4.16b, v4.16b, v4.16b, #8
	add	v2.4s, v2.4s, v5.4s
	eor	v4.16b, v2.16b, v4.16b
	ext	v3.16b, v3.16b, v3.16b, #4
	tbl	v0.16b, { v4.16b }, v0.16b
	add	v3.4s, v3.4s, v0.4s
	eor	v4.16b, v5.16b, v3.16b
	ushr	v5.4s, v4.4s, #12
	shl	v4.4s, v4.4s, #20
	add	v2.4s, v2.4s, v7.4s
	orr	v4.16b, v4.16b, v5.16b
	add	v2.4s, v2.4s, v4.4s
	eor	v0.16b, v0.16b, v2.16b
	tbl	v0.16b, { v0.16b }, v1.16b
	add	v1.4s, v3.4s, v0.4s
	eor	v3.16b, v4.16b, v1.16b
	ushr	v4.4s, v3.4s, #7
	shl	v3.4s, v3.4s, #25
	ext	v2.16b, v2.16b, v2.16b, #4
	ext	v0.16b, v0.16b, v0.16b, #8
	ext	v1.16b, v1.16b, v1.16b, #12
	orr	v3.16b, v3.16b, v4.16b
	eor	v2.16b, v2.16b, v1.16b
	eor	v3.16b, v3.16b, v0.16b
	stp	q2, q3, [x5]
	ldr	q2, [x0]
	eor	v1.16b, v2.16b, v1.16b
	str	q1, [x5, #32]
	ldr	q1, [x0, #16]
	eor	v0.16b, v1.16b, v0.16b
	str	q0, [x5, #48]
	ret
.Lfunc_end1:
	.size	zfs_blake3_compress_xof_sse41, .Lfunc_end1-zfs_blake3_compress_xof_sse41
	.cfi_endproc

	.section	.rodata.cst16,"aM",@progbits,16
	.p2align	4
.LCPI2_0:
	.word	0
	.word	1
	.word	2
	.word	3
.LCPI2_1:
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
.LCPI2_2:
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
	.globl	zfs_blake3_hash_many_sse41
	.p2align	2
	.type	zfs_blake3_hash_many_sse41,@function
zfs_blake3_hash_many_sse41:
	.cfi_startproc
	stp	d15, d14, [sp, #-160]!
	stp	d13, d12, [sp, #16]
	stp	d11, d10, [sp, #32]
	stp	d9, d8, [sp, #48]
	stp	x29, x30, [sp, #64]
	stp	x28, x27, [sp, #80]
	stp	x26, x25, [sp, #96]
	stp	x24, x23, [sp, #112]
	stp	x22, x21, [sp, #128]
	stp	x20, x19, [sp, #144]
	mov	x29, sp
	sub	sp, sp, #448
	.cfi_def_cfa w29, 160
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
	ldr	x26, [x29, #168]
	ldrb	w27, [x29, #160]
	mov	w19, w6
	mov	x20, x4
	mov	x22, x2
	mov	x28, x1
	cmp	x1, #4
	mov	x24, x0
	str	x3, [sp, #40]
	b.lo	.LBB2_8
	adrp	x11, .LCPI2_0
	ldr	q0, [x11, :lo12:.LCPI2_0]
	sbfx	w13, w5, #0, #1
	dup	v1.4s, w13
	mov	w10, #58983
	mov	w11, #44677
	mov	w12, #62322
	and	v0.16b, v1.16b, v0.16b
	mov	w13, #62778
	orr	w8, w7, w19
	adrp	x9, .LCPI2_1
	movk	w10, #27145, lsl #16
	movk	w11, #47975, lsl #16
	movk	w12, #15470, lsl #16
	movk	w13, #42319, lsl #16
	str	q0, [sp, #16]
	orr	v0.4s, #128, lsl #24
	adrp	x14, .LCPI2_2
	str	q0, [sp]
.LBB2_2:
	ldr	x2, [sp, #40]
	mov	x15, x2
	ld1r	{ v7.4s }, [x15], #4
	add	x16, x2, #8
	add	x17, x2, #12
	add	x18, x2, #16
	add	x0, x2, #20
	add	x3, x2, #24
	add	x2, x2, #28
	ld1r	{ v6.4s }, [x16]
	ld1r	{ v17.4s }, [x17]
	ld1r	{ v10.4s }, [x18]
	ld1r	{ v11.4s }, [x0]
	ld1r	{ v19.4s }, [x3]
	ld1r	{ v18.4s }, [x15]
	ld1r	{ v16.4s }, [x2]
	cbz	x22, .LBB2_7
	ldr	q1, [sp, #16]
	dup	v0.4s, w20
	ldp	x15, x16, [x24]
	ldp	x17, x18, [x24, #16]
	add	v1.4s, v0.4s, v1.4s
	movi	v0.4s, #128, lsl #24
	str	q1, [sp, #64]
	eor	v0.16b, v1.16b, v0.16b
	ldr	q1, [sp]
	lsr	x2, x20, #32
	mov	x0, xzr
	mov	w6, w8
	cmgt	v0.4s, v1.4s, v0.4s
	dup	v1.4s, w2
	sub	v0.4s, v1.4s, v0.4s
	str	q0, [sp, #48]
.LBB2_4:
	mov	w4, #16
	stp	q16, q17, [sp, #192]
	bfi	x4, x0, #6, #58
	ldr	q1, [x15, x4]
	ldr	q3, [x16, x4]
	ldr	q2, [x17, x4]
	ldr	q4, [x18, x4]
	mov	w4, #32
	bfi	x4, x0, #6, #58
	ldr	q5, [x15, x4]
	ldr	q20, [x16, x4]
	ldr	q21, [x17, x4]
	ldr	q22, [x18, x4]
	mov	w4, #48
	lsl	x3, x0, #6
	bfi	x4, x0, #6, #58
	add	x0, x0, #1
	ldr	q0, [x15, x3]
	ldr	q23, [x16, x3]
	ldr	q16, [x17, x3]
	ldr	q17, [x18, x3]
	cmp	x0, x22
	ldr	q25, [x15, x4]
	ldr	q14, [x16, x4]
	ldr	q28, [x17, x4]
	ldr	q31, [x18, x4]
	csel	w4, w27, wzr, eq
	orr	w4, w4, w6
	mov	x2, xzr
	and	w6, w4, #0xff
	add	x3, x3, #256
.LBB2_5:
	ldr	x4, [x24, x2]
	add	x2, x2, #8
	cmp	x2, #32
	add	x4, x4, x3
	prfm	pldl1keep, [x4]
	b.ne	.LBB2_5
	zip1	v29.4s, v0.4s, v23.4s
	zip2	v23.4s, v0.4s, v23.4s
	zip1	v0.4s, v16.4s, v17.4s
	zip2	v24.4s, v16.4s, v17.4s
	zip1	v9.4s, v1.4s, v3.4s
	zip2	v26.4s, v1.4s, v3.4s
	zip1	v27.4s, v2.4s, v4.4s
	zip2	v17.4s, v2.4s, v4.4s
	zip1	v12.4s, v21.4s, v22.4s
	zip2	v13.4s, v21.4s, v22.4s
	add	v2.4s, v7.4s, v10.4s
	add	v1.4s, v18.4s, v11.4s
	ext	v7.16b, v0.16b, v29.16b, #8
	ext	v22.16b, v24.16b, v23.16b, #8
	zip1	v30.4s, v5.4s, v20.4s
	zip2	v20.4s, v5.4s, v20.4s
	stp	q1, q2, [sp, #112]
	ext	v2.16b, v29.16b, v7.16b, #8
	mov	v29.d[1], v0.d[0]
	ext	v18.16b, v23.16b, v22.16b, #8
	mov	v23.d[1], v24.d[0]
	zip1	v21.4s, v25.4s, v14.4s
	zip2	v4.4s, v25.4s, v14.4s
	zip1	v14.4s, v28.4s, v31.4s
	zip2	v15.4s, v28.4s, v31.4s
	add	v8.4s, v6.4s, v19.4s
	ext	v28.16b, v27.16b, v9.16b, #8
	ext	v31.16b, v17.16b, v26.16b, #8
	stur	q2, [x29, #-208]
	mov	v7.16b, v29.16b
	ext	v0.16b, v12.16b, v30.16b, #8
	stp	q23, q29, [x29, #-80]
	mov	v2.16b, v19.16b
	ext	v19.16b, v13.16b, v20.16b, #8
	mov	v29.16b, v9.16b
	ext	v25.16b, v9.16b, v28.16b, #8
	mov	v29.d[1], v27.d[0]
	ext	v24.16b, v26.16b, v31.16b, #8
	mov	v26.d[1], v17.d[0]
	ext	v17.16b, v15.16b, v4.16b, #8
	ext	v27.16b, v30.16b, v0.16b, #8
	ext	v0.16b, v20.16b, v19.16b, #8
	stp	q0, q25, [sp, #80]
	ext	v0.16b, v4.16b, v17.16b, #8
	str	q0, [sp, #224]
	ldr	q0, [sp, #128]
	mov	v6.16b, v23.16b
	mov	v22.16b, v4.16b
	ldr	q16, [x9, :lo12:.LCPI2_1]
	add	v17.4s, v0.4s, v7.4s
	ldr	q0, [sp, #112]
	mov	v30.d[1], v12.d[0]
	add	v7.4s, v8.4s, v29.4s
	mov	v20.d[1], v13.d[0]
	add	v4.4s, v0.4s, v6.4s
	ldr	q0, [sp, #64]
	dup	v3.4s, w12
	ext	v28.16b, v14.16b, v21.16b, #8
	dup	v1.4s, w10
	eor	v19.16b, v17.16b, v0.16b
	ldr	q0, [sp, #48]
	ext	v23.16b, v21.16b, v28.16b, #8
	mov	v21.d[1], v14.d[0]
	tbl	v14.16b, { v19.16b }, v16.16b
	eor	v12.16b, v4.16b, v0.16b
	movi	v0.4s, #64
	eor	v13.16b, v7.16b, v0.16b
	tbl	v13.16b, { v13.16b }, v16.16b
	add	v6.4s, v13.4s, v3.4s
	dup	v5.4s, w11
	tbl	v12.16b, { v12.16b }, v16.16b
	add	v1.4s, v14.4s, v1.4s
	eor	v9.16b, v6.16b, v2.16b
	ldp	q2, q0, [sp, #192]
	add	v5.4s, v12.4s, v5.4s
	eor	v19.16b, v1.16b, v10.16b
	eor	v10.16b, v5.16b, v11.16b
	ushr	v11.4s, v19.4s, #12
	shl	v19.4s, v19.4s, #20
	orr	v11.16b, v19.16b, v11.16b
	ushr	v19.4s, v10.4s, #12
	shl	v10.4s, v10.4s, #20
	mov	v22.d[1], v15.d[0]
	orr	v10.16b, v10.16b, v19.16b
	ushr	v19.4s, v9.4s, #12
	shl	v9.4s, v9.4s, #20
	add	v15.4s, v0.4s, v2.4s
	orr	v9.16b, v9.16b, v19.16b
	dup	v19.4s, w6
	add	v15.4s, v15.4s, v26.4s
	eor	v19.16b, v15.16b, v19.16b
	tbl	v3.16b, { v19.16b }, v16.16b
	dup	v19.4s, w13
	add	v8.4s, v3.4s, v19.4s
	ldur	q31, [x29, #-208]
	eor	v19.16b, v8.16b, v2.16b
	ushr	v0.4s, v19.4s, #12
	shl	v19.4s, v19.4s, #20
	orr	v2.16b, v19.16b, v0.16b
	ldr	q19, [x14, :lo12:.LCPI2_2]
	add	v17.4s, v17.4s, v31.4s
	add	v17.4s, v17.4s, v11.4s
	eor	v14.16b, v14.16b, v17.16b
	tbl	v14.16b, { v14.16b }, v19.16b
	add	v1.4s, v1.4s, v14.4s
	eor	v11.16b, v1.16b, v11.16b
	add	v4.4s, v4.4s, v18.4s
	ushr	v0.4s, v11.4s, #7
	shl	v11.4s, v11.4s, #25
	add	v4.4s, v4.4s, v10.4s
	orr	v0.16b, v11.16b, v0.16b
	eor	v11.16b, v12.16b, v4.16b
	tbl	v11.16b, { v11.16b }, v19.16b
	add	v5.4s, v5.4s, v11.4s
	eor	v10.16b, v5.16b, v10.16b
	add	v7.4s, v7.4s, v25.4s
	ushr	v12.4s, v10.4s, #7
	shl	v10.4s, v10.4s, #25
	add	v7.4s, v7.4s, v9.4s
	orr	v10.16b, v10.16b, v12.16b
	eor	v12.16b, v13.16b, v7.16b
	tbl	v12.16b, { v12.16b }, v19.16b
	add	v6.4s, v6.4s, v12.4s
	eor	v9.16b, v6.16b, v9.16b
	ushr	v13.4s, v9.4s, #7
	shl	v9.4s, v9.4s, #25
	orr	v9.16b, v9.16b, v13.16b
	add	v13.4s, v15.4s, v24.4s
	add	v13.4s, v13.4s, v2.4s
	eor	v3.16b, v3.16b, v13.16b
	tbl	v3.16b, { v3.16b }, v19.16b
	add	v8.4s, v8.4s, v3.4s
	eor	v2.16b, v8.16b, v2.16b
	add	v17.4s, v17.4s, v30.4s
	ushr	v15.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	add	v17.4s, v17.4s, v10.4s
	add	v4.4s, v4.4s, v20.4s
	orr	v2.16b, v2.16b, v15.16b
	eor	v3.16b, v3.16b, v17.16b
	add	v4.4s, v4.4s, v9.4s
	add	v7.4s, v7.4s, v21.4s
	tbl	v3.16b, { v3.16b }, v16.16b
	eor	v14.16b, v14.16b, v4.16b
	add	v7.4s, v7.4s, v2.4s
	add	v13.4s, v13.4s, v22.4s
	mov	v28.16b, v26.16b
	stur	q26, [x29, #-112]
	mov	v26.16b, v18.16b
	mov	v18.16b, v24.16b
	stur	q24, [x29, #-160]
	add	v6.4s, v6.4s, v3.4s
	mov	v24.16b, v20.16b
	tbl	v14.16b, { v14.16b }, v16.16b
	eor	v11.16b, v11.16b, v7.16b
	add	v13.4s, v13.4s, v0.4s
	ldr	q20, [sp, #80]
	eor	v10.16b, v6.16b, v10.16b
	add	v8.4s, v8.4s, v14.4s
	tbl	v11.16b, { v11.16b }, v16.16b
	eor	v12.16b, v12.16b, v13.16b
	stp	q30, q22, [x29, #-192]
	ushr	v15.4s, v10.4s, #12
	shl	v10.4s, v10.4s, #20
	eor	v9.16b, v8.16b, v9.16b
	add	v1.4s, v1.4s, v11.4s
	tbl	v12.16b, { v12.16b }, v16.16b
	mov	v30.16b, v27.16b
	add	v17.4s, v17.4s, v27.4s
	ldr	q27, [sp, #224]
	orr	v10.16b, v10.16b, v15.16b
	ushr	v15.4s, v9.4s, #12
	shl	v9.4s, v9.4s, #20
	eor	v2.16b, v1.16b, v2.16b
	add	v5.4s, v5.4s, v12.4s
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	eor	v0.16b, v5.16b, v0.16b
	add	v17.4s, v17.4s, v10.4s
	add	v4.4s, v4.4s, v20.4s
	orr	v2.16b, v2.16b, v15.16b
	ushr	v15.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	eor	v3.16b, v3.16b, v17.16b
	add	v4.4s, v4.4s, v9.4s
	add	v7.4s, v7.4s, v23.4s
	orr	v0.16b, v0.16b, v15.16b
	tbl	v3.16b, { v3.16b }, v19.16b
	eor	v14.16b, v14.16b, v4.16b
	add	v7.4s, v7.4s, v2.4s
	add	v13.4s, v13.4s, v27.4s
	add	v6.4s, v6.4s, v3.4s
	tbl	v14.16b, { v14.16b }, v19.16b
	eor	v11.16b, v11.16b, v7.16b
	add	v13.4s, v13.4s, v0.4s
	eor	v10.16b, v6.16b, v10.16b
	add	v8.4s, v8.4s, v14.4s
	tbl	v11.16b, { v11.16b }, v19.16b
	eor	v12.16b, v12.16b, v13.16b
	stur	q21, [x29, #-144]
	ushr	v15.4s, v10.4s, #7
	shl	v10.4s, v10.4s, #25
	eor	v9.16b, v8.16b, v9.16b
	add	v1.4s, v1.4s, v11.4s
	tbl	v12.16b, { v12.16b }, v19.16b
	ldur	q21, [x29, #-80]
	orr	v10.16b, v10.16b, v15.16b
	ushr	v15.4s, v9.4s, #7
	shl	v9.4s, v9.4s, #25
	eor	v2.16b, v1.16b, v2.16b
	add	v5.4s, v5.4s, v12.4s
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	eor	v0.16b, v5.16b, v0.16b
	orr	v2.16b, v2.16b, v15.16b
	ushr	v15.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	orr	v0.16b, v0.16b, v15.16b
	add	v17.4s, v17.4s, v21.4s
	add	v17.4s, v17.4s, v0.4s
	add	v4.4s, v4.4s, v26.4s
	eor	v14.16b, v14.16b, v17.16b
	add	v4.4s, v4.4s, v10.4s
	add	v7.4s, v7.4s, v18.4s
	tbl	v14.16b, { v14.16b }, v16.16b
	eor	v11.16b, v11.16b, v4.16b
	add	v7.4s, v7.4s, v9.4s
	add	v13.4s, v13.4s, v29.4s
	add	v1.4s, v1.4s, v14.4s
	tbl	v11.16b, { v11.16b }, v16.16b
	eor	v12.16b, v12.16b, v7.16b
	add	v13.4s, v13.4s, v2.4s
	eor	v0.16b, v0.16b, v1.16b
	add	v5.4s, v5.4s, v11.4s
	tbl	v12.16b, { v12.16b }, v16.16b
	eor	v3.16b, v3.16b, v13.16b
	ldur	q22, [x29, #-64]
	ushr	v15.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	eor	v10.16b, v5.16b, v10.16b
	add	v6.4s, v6.4s, v12.4s
	tbl	v3.16b, { v3.16b }, v16.16b
	orr	v0.16b, v0.16b, v15.16b
	ushr	v15.4s, v10.4s, #12
	shl	v10.4s, v10.4s, #20
	eor	v9.16b, v6.16b, v9.16b
	add	v8.4s, v8.4s, v3.4s
	add	v17.4s, v17.4s, v28.4s
	orr	v10.16b, v10.16b, v15.16b
	ushr	v15.4s, v9.4s, #12
	shl	v9.4s, v9.4s, #20
	eor	v2.16b, v8.16b, v2.16b
	add	v17.4s, v17.4s, v0.4s
	add	v4.4s, v4.4s, v24.4s
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	eor	v14.16b, v14.16b, v17.16b
	add	v4.4s, v4.4s, v10.4s
	add	v7.4s, v7.4s, v22.4s
	orr	v2.16b, v2.16b, v15.16b
	tbl	v14.16b, { v14.16b }, v19.16b
	eor	v11.16b, v11.16b, v4.16b
	add	v7.4s, v7.4s, v9.4s
	add	v13.4s, v13.4s, v23.4s
	add	v1.4s, v1.4s, v14.4s
	tbl	v11.16b, { v11.16b }, v19.16b
	eor	v12.16b, v12.16b, v7.16b
	add	v13.4s, v13.4s, v2.4s
	eor	v0.16b, v0.16b, v1.16b
	add	v5.4s, v5.4s, v11.4s
	tbl	v12.16b, { v12.16b }, v19.16b
	eor	v3.16b, v3.16b, v13.16b
	ldur	q22, [x29, #-144]
	ushr	v15.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	eor	v10.16b, v5.16b, v10.16b
	add	v6.4s, v6.4s, v12.4s
	tbl	v3.16b, { v3.16b }, v19.16b
	orr	v0.16b, v0.16b, v15.16b
	ushr	v15.4s, v10.4s, #7
	shl	v10.4s, v10.4s, #25
	eor	v9.16b, v6.16b, v9.16b
	add	v8.4s, v8.4s, v3.4s
	orr	v10.16b, v10.16b, v15.16b
	ushr	v15.4s, v9.4s, #7
	shl	v9.4s, v9.4s, #25
	eor	v2.16b, v8.16b, v2.16b
	add	v17.4s, v17.4s, v31.4s
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	add	v17.4s, v17.4s, v10.4s
	add	v4.4s, v4.4s, v22.4s
	orr	v2.16b, v2.16b, v15.16b
	eor	v3.16b, v3.16b, v17.16b
	add	v4.4s, v4.4s, v9.4s
	add	v7.4s, v7.4s, v30.4s
	tbl	v3.16b, { v3.16b }, v16.16b
	eor	v14.16b, v14.16b, v4.16b
	add	v7.4s, v7.4s, v2.4s
	add	v13.4s, v13.4s, v27.4s
	add	v6.4s, v6.4s, v3.4s
	tbl	v14.16b, { v14.16b }, v16.16b
	eor	v11.16b, v11.16b, v7.16b
	add	v13.4s, v13.4s, v0.4s
	ldr	q27, [sp, #96]
	mov	v21.16b, v26.16b
	stur	q26, [x29, #-96]
	mov	v28.16b, v31.16b
	eor	v10.16b, v6.16b, v10.16b
	add	v8.4s, v8.4s, v14.4s
	tbl	v11.16b, { v11.16b }, v16.16b
	eor	v12.16b, v12.16b, v13.16b
	ldp	q31, q26, [x29, #-192]
	ushr	v15.4s, v10.4s, #12
	shl	v10.4s, v10.4s, #20
	eor	v9.16b, v8.16b, v9.16b
	add	v1.4s, v1.4s, v11.4s
	tbl	v12.16b, { v12.16b }, v16.16b
	orr	v10.16b, v10.16b, v15.16b
	ushr	v15.4s, v9.4s, #12
	shl	v9.4s, v9.4s, #20
	eor	v2.16b, v1.16b, v2.16b
	add	v5.4s, v5.4s, v12.4s
	add	v17.4s, v17.4s, v20.4s
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	eor	v0.16b, v5.16b, v0.16b
	add	v17.4s, v17.4s, v10.4s
	add	v4.4s, v4.4s, v27.4s
	orr	v2.16b, v2.16b, v15.16b
	ushr	v15.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	eor	v3.16b, v3.16b, v17.16b
	add	v4.4s, v4.4s, v9.4s
	add	v7.4s, v7.4s, v26.4s
	orr	v0.16b, v0.16b, v15.16b
	tbl	v3.16b, { v3.16b }, v19.16b
	eor	v14.16b, v14.16b, v4.16b
	add	v7.4s, v7.4s, v2.4s
	add	v13.4s, v13.4s, v31.4s
	add	v6.4s, v6.4s, v3.4s
	tbl	v14.16b, { v14.16b }, v19.16b
	eor	v11.16b, v11.16b, v7.16b
	add	v13.4s, v13.4s, v0.4s
	eor	v10.16b, v6.16b, v10.16b
	add	v8.4s, v8.4s, v14.4s
	tbl	v11.16b, { v11.16b }, v19.16b
	eor	v12.16b, v12.16b, v13.16b
	ushr	v15.4s, v10.4s, #7
	shl	v10.4s, v10.4s, #25
	eor	v9.16b, v8.16b, v9.16b
	add	v1.4s, v1.4s, v11.4s
	tbl	v12.16b, { v12.16b }, v19.16b
	orr	v10.16b, v10.16b, v15.16b
	ushr	v15.4s, v9.4s, #7
	shl	v9.4s, v9.4s, #25
	eor	v2.16b, v1.16b, v2.16b
	add	v5.4s, v5.4s, v12.4s
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	eor	v0.16b, v5.16b, v0.16b
	mov	v18.16b, v24.16b
	mov	v24.16b, v20.16b
	orr	v2.16b, v2.16b, v15.16b
	ushr	v15.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	ldur	q20, [x29, #-160]
	orr	v0.16b, v0.16b, v15.16b
	add	v17.4s, v17.4s, v21.4s
	add	v17.4s, v17.4s, v0.4s
	add	v4.4s, v4.4s, v18.4s
	eor	v14.16b, v14.16b, v17.16b
	add	v4.4s, v4.4s, v10.4s
	add	v7.4s, v7.4s, v23.4s
	tbl	v14.16b, { v14.16b }, v16.16b
	eor	v11.16b, v11.16b, v4.16b
	add	v7.4s, v7.4s, v9.4s
	add	v13.4s, v13.4s, v20.4s
	add	v1.4s, v1.4s, v14.4s
	tbl	v11.16b, { v11.16b }, v16.16b
	eor	v12.16b, v12.16b, v7.16b
	add	v13.4s, v13.4s, v2.4s
	eor	v0.16b, v0.16b, v1.16b
	add	v5.4s, v5.4s, v11.4s
	tbl	v12.16b, { v12.16b }, v16.16b
	eor	v3.16b, v3.16b, v13.16b
	ldur	q25, [x29, #-80]
	ushr	v15.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	eor	v10.16b, v5.16b, v10.16b
	add	v6.4s, v6.4s, v12.4s
	tbl	v3.16b, { v3.16b }, v16.16b
	orr	v0.16b, v0.16b, v15.16b
	ushr	v15.4s, v10.4s, #12
	shl	v10.4s, v10.4s, #20
	eor	v9.16b, v6.16b, v9.16b
	add	v8.4s, v8.4s, v3.4s
	add	v17.4s, v17.4s, v29.4s
	orr	v10.16b, v10.16b, v15.16b
	ushr	v15.4s, v9.4s, #12
	shl	v9.4s, v9.4s, #20
	eor	v2.16b, v8.16b, v2.16b
	add	v17.4s, v17.4s, v0.4s
	add	v4.4s, v4.4s, v22.4s
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	eor	v14.16b, v14.16b, v17.16b
	add	v4.4s, v4.4s, v10.4s
	add	v7.4s, v7.4s, v25.4s
	orr	v2.16b, v2.16b, v15.16b
	tbl	v14.16b, { v14.16b }, v19.16b
	eor	v11.16b, v11.16b, v4.16b
	add	v7.4s, v7.4s, v9.4s
	add	v13.4s, v13.4s, v26.4s
	add	v1.4s, v1.4s, v14.4s
	tbl	v11.16b, { v11.16b }, v19.16b
	eor	v12.16b, v12.16b, v7.16b
	add	v13.4s, v13.4s, v2.4s
	ldur	q25, [x29, #-112]
	eor	v0.16b, v0.16b, v1.16b
	add	v5.4s, v5.4s, v11.4s
	tbl	v12.16b, { v12.16b }, v19.16b
	eor	v3.16b, v3.16b, v13.16b
	ushr	v15.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	eor	v10.16b, v5.16b, v10.16b
	add	v6.4s, v6.4s, v12.4s
	tbl	v3.16b, { v3.16b }, v19.16b
	orr	v0.16b, v0.16b, v15.16b
	ushr	v15.4s, v10.4s, #7
	shl	v10.4s, v10.4s, #25
	eor	v9.16b, v6.16b, v9.16b
	add	v8.4s, v8.4s, v3.4s
	orr	v10.16b, v10.16b, v15.16b
	ushr	v15.4s, v9.4s, #7
	shl	v9.4s, v9.4s, #25
	eor	v2.16b, v8.16b, v2.16b
	add	v17.4s, v17.4s, v25.4s
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	add	v17.4s, v17.4s, v10.4s
	add	v4.4s, v4.4s, v30.4s
	orr	v2.16b, v2.16b, v15.16b
	eor	v3.16b, v3.16b, v17.16b
	add	v4.4s, v4.4s, v9.4s
	add	v7.4s, v7.4s, v24.4s
	tbl	v3.16b, { v3.16b }, v16.16b
	eor	v14.16b, v14.16b, v4.16b
	add	v7.4s, v7.4s, v2.4s
	add	v13.4s, v13.4s, v31.4s
	add	v6.4s, v6.4s, v3.4s
	tbl	v14.16b, { v14.16b }, v16.16b
	eor	v11.16b, v11.16b, v7.16b
	add	v13.4s, v13.4s, v0.4s
	ldur	q25, [x29, #-64]
	eor	v10.16b, v6.16b, v10.16b
	add	v8.4s, v8.4s, v14.4s
	tbl	v11.16b, { v11.16b }, v16.16b
	eor	v12.16b, v12.16b, v13.16b
	ldr	q31, [sp, #224]
	ushr	v15.4s, v10.4s, #12
	shl	v10.4s, v10.4s, #20
	eor	v9.16b, v8.16b, v9.16b
	add	v1.4s, v1.4s, v11.4s
	tbl	v12.16b, { v12.16b }, v16.16b
	orr	v10.16b, v10.16b, v15.16b
	ushr	v15.4s, v9.4s, #12
	shl	v9.4s, v9.4s, #20
	eor	v2.16b, v1.16b, v2.16b
	add	v5.4s, v5.4s, v12.4s
	add	v17.4s, v17.4s, v27.4s
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	eor	v0.16b, v5.16b, v0.16b
	add	v17.4s, v17.4s, v10.4s
	add	v4.4s, v4.4s, v25.4s
	orr	v2.16b, v2.16b, v15.16b
	ushr	v15.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	eor	v3.16b, v3.16b, v17.16b
	add	v4.4s, v4.4s, v9.4s
	add	v7.4s, v7.4s, v31.4s
	orr	v0.16b, v0.16b, v15.16b
	tbl	v3.16b, { v3.16b }, v19.16b
	eor	v14.16b, v14.16b, v4.16b
	add	v7.4s, v7.4s, v2.4s
	add	v13.4s, v13.4s, v28.4s
	add	v6.4s, v6.4s, v3.4s
	tbl	v14.16b, { v14.16b }, v19.16b
	eor	v11.16b, v11.16b, v7.16b
	add	v13.4s, v13.4s, v0.4s
	eor	v10.16b, v6.16b, v10.16b
	add	v8.4s, v8.4s, v14.4s
	tbl	v11.16b, { v11.16b }, v19.16b
	eor	v12.16b, v12.16b, v13.16b
	ushr	v15.4s, v10.4s, #7
	shl	v10.4s, v10.4s, #25
	eor	v9.16b, v8.16b, v9.16b
	add	v1.4s, v1.4s, v11.4s
	tbl	v12.16b, { v12.16b }, v19.16b
	orr	v10.16b, v10.16b, v15.16b
	ushr	v15.4s, v9.4s, #7
	shl	v9.4s, v9.4s, #25
	eor	v2.16b, v1.16b, v2.16b
	add	v5.4s, v5.4s, v12.4s
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	eor	v0.16b, v5.16b, v0.16b
	orr	v2.16b, v2.16b, v15.16b
	ushr	v15.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	orr	v0.16b, v0.16b, v15.16b
	add	v17.4s, v17.4s, v18.4s
	add	v17.4s, v17.4s, v0.4s
	add	v4.4s, v4.4s, v22.4s
	eor	v14.16b, v14.16b, v17.16b
	add	v4.4s, v4.4s, v10.4s
	add	v7.4s, v7.4s, v26.4s
	tbl	v14.16b, { v14.16b }, v16.16b
	eor	v11.16b, v11.16b, v4.16b
	add	v7.4s, v7.4s, v9.4s
	add	v13.4s, v13.4s, v23.4s
	add	v1.4s, v1.4s, v14.4s
	tbl	v11.16b, { v11.16b }, v16.16b
	eor	v12.16b, v12.16b, v7.16b
	add	v13.4s, v13.4s, v2.4s
	mov	v21.16b, v29.16b
	stur	q29, [x29, #-128]
	mov	v29.16b, v30.16b
	mov	v30.16b, v27.16b
	mov	v27.16b, v18.16b
	str	q18, [sp, #176]
	eor	v0.16b, v0.16b, v1.16b
	mov	v18.16b, v22.16b
	add	v5.4s, v5.4s, v11.4s
	tbl	v12.16b, { v12.16b }, v16.16b
	eor	v3.16b, v3.16b, v13.16b
	ldur	q22, [x29, #-96]
	ushr	v15.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	eor	v10.16b, v5.16b, v10.16b
	add	v6.4s, v6.4s, v12.4s
	tbl	v3.16b, { v3.16b }, v16.16b
	orr	v0.16b, v0.16b, v15.16b
	ushr	v15.4s, v10.4s, #12
	shl	v10.4s, v10.4s, #20
	eor	v9.16b, v6.16b, v9.16b
	add	v8.4s, v8.4s, v3.4s
	add	v17.4s, v17.4s, v20.4s
	orr	v10.16b, v10.16b, v15.16b
	ushr	v15.4s, v9.4s, #12
	shl	v9.4s, v9.4s, #20
	eor	v2.16b, v8.16b, v2.16b
	add	v17.4s, v17.4s, v0.4s
	add	v4.4s, v4.4s, v29.4s
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	eor	v14.16b, v14.16b, v17.16b
	add	v4.4s, v4.4s, v10.4s
	add	v7.4s, v7.4s, v22.4s
	orr	v2.16b, v2.16b, v15.16b
	tbl	v14.16b, { v14.16b }, v19.16b
	eor	v11.16b, v11.16b, v4.16b
	add	v7.4s, v7.4s, v9.4s
	add	v13.4s, v13.4s, v31.4s
	add	v1.4s, v1.4s, v14.4s
	tbl	v11.16b, { v11.16b }, v19.16b
	eor	v12.16b, v12.16b, v7.16b
	add	v13.4s, v13.4s, v2.4s
	eor	v0.16b, v0.16b, v1.16b
	add	v5.4s, v5.4s, v11.4s
	tbl	v12.16b, { v12.16b }, v19.16b
	eor	v3.16b, v3.16b, v13.16b
	ushr	v15.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	eor	v10.16b, v5.16b, v10.16b
	add	v6.4s, v6.4s, v12.4s
	tbl	v3.16b, { v3.16b }, v19.16b
	orr	v0.16b, v0.16b, v15.16b
	ushr	v15.4s, v10.4s, #7
	shl	v10.4s, v10.4s, #25
	eor	v9.16b, v6.16b, v9.16b
	add	v8.4s, v8.4s, v3.4s
	orr	v10.16b, v10.16b, v15.16b
	ushr	v15.4s, v9.4s, #7
	shl	v9.4s, v9.4s, #25
	eor	v2.16b, v8.16b, v2.16b
	add	v17.4s, v17.4s, v21.4s
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	add	v17.4s, v17.4s, v10.4s
	add	v4.4s, v4.4s, v24.4s
	orr	v2.16b, v2.16b, v15.16b
	eor	v3.16b, v3.16b, v17.16b
	add	v4.4s, v4.4s, v9.4s
	add	v7.4s, v7.4s, v30.4s
	tbl	v3.16b, { v3.16b }, v16.16b
	eor	v14.16b, v14.16b, v4.16b
	add	v7.4s, v7.4s, v2.4s
	add	v13.4s, v13.4s, v28.4s
	add	v6.4s, v6.4s, v3.4s
	mov	v22.16b, v24.16b
	tbl	v14.16b, { v14.16b }, v16.16b
	eor	v11.16b, v11.16b, v7.16b
	add	v13.4s, v13.4s, v0.4s
	ldur	q24, [x29, #-80]
	eor	v10.16b, v6.16b, v10.16b
	add	v8.4s, v8.4s, v14.4s
	mov	v21.16b, v30.16b
	tbl	v11.16b, { v11.16b }, v16.16b
	eor	v12.16b, v12.16b, v13.16b
	ldur	q30, [x29, #-192]
	mov	v20.16b, v29.16b
	ushr	v15.4s, v10.4s, #12
	shl	v10.4s, v10.4s, #20
	eor	v9.16b, v8.16b, v9.16b
	add	v1.4s, v1.4s, v11.4s
	tbl	v12.16b, { v12.16b }, v16.16b
	ldur	q29, [x29, #-112]
	orr	v10.16b, v10.16b, v15.16b
	ushr	v15.4s, v9.4s, #12
	shl	v9.4s, v9.4s, #20
	eor	v2.16b, v1.16b, v2.16b
	add	v5.4s, v5.4s, v12.4s
	add	v17.4s, v17.4s, v25.4s
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	eor	v0.16b, v5.16b, v0.16b
	add	v17.4s, v17.4s, v10.4s
	add	v4.4s, v4.4s, v24.4s
	orr	v2.16b, v2.16b, v15.16b
	ushr	v15.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	eor	v3.16b, v3.16b, v17.16b
	add	v4.4s, v4.4s, v9.4s
	add	v7.4s, v7.4s, v30.4s
	orr	v0.16b, v0.16b, v15.16b
	tbl	v3.16b, { v3.16b }, v19.16b
	eor	v14.16b, v14.16b, v4.16b
	add	v7.4s, v7.4s, v2.4s
	add	v13.4s, v13.4s, v29.4s
	add	v6.4s, v6.4s, v3.4s
	tbl	v14.16b, { v14.16b }, v19.16b
	eor	v11.16b, v11.16b, v7.16b
	add	v13.4s, v13.4s, v0.4s
	eor	v10.16b, v6.16b, v10.16b
	add	v8.4s, v8.4s, v14.4s
	tbl	v11.16b, { v11.16b }, v19.16b
	eor	v12.16b, v12.16b, v13.16b
	ushr	v15.4s, v10.4s, #7
	shl	v10.4s, v10.4s, #25
	eor	v9.16b, v8.16b, v9.16b
	add	v1.4s, v1.4s, v11.4s
	tbl	v12.16b, { v12.16b }, v19.16b
	orr	v10.16b, v10.16b, v15.16b
	ushr	v15.4s, v9.4s, #7
	shl	v9.4s, v9.4s, #25
	eor	v2.16b, v1.16b, v2.16b
	add	v5.4s, v5.4s, v12.4s
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	eor	v0.16b, v5.16b, v0.16b
	orr	v2.16b, v2.16b, v15.16b
	ushr	v15.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	orr	v0.16b, v0.16b, v15.16b
	add	v17.4s, v17.4s, v18.4s
	add	v17.4s, v17.4s, v0.4s
	add	v4.4s, v4.4s, v20.4s
	eor	v14.16b, v14.16b, v17.16b
	add	v4.4s, v4.4s, v10.4s
	add	v7.4s, v7.4s, v31.4s
	tbl	v14.16b, { v14.16b }, v16.16b
	eor	v11.16b, v11.16b, v4.16b
	add	v7.4s, v7.4s, v9.4s
	add	v13.4s, v13.4s, v26.4s
	add	v1.4s, v1.4s, v14.4s
	tbl	v11.16b, { v11.16b }, v16.16b
	eor	v12.16b, v12.16b, v7.16b
	add	v13.4s, v13.4s, v2.4s
	eor	v0.16b, v0.16b, v1.16b
	add	v5.4s, v5.4s, v11.4s
	tbl	v12.16b, { v12.16b }, v16.16b
	eor	v3.16b, v3.16b, v13.16b
	ushr	v15.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	eor	v10.16b, v5.16b, v10.16b
	add	v6.4s, v6.4s, v12.4s
	tbl	v3.16b, { v3.16b }, v16.16b
	orr	v0.16b, v0.16b, v15.16b
	ushr	v15.4s, v10.4s, #12
	shl	v10.4s, v10.4s, #20
	eor	v9.16b, v6.16b, v9.16b
	add	v8.4s, v8.4s, v3.4s
	add	v17.4s, v17.4s, v23.4s
	orr	v10.16b, v10.16b, v15.16b
	ushr	v15.4s, v9.4s, #12
	shl	v9.4s, v9.4s, #20
	eor	v2.16b, v8.16b, v2.16b
	add	v17.4s, v17.4s, v0.4s
	add	v4.4s, v4.4s, v22.4s
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	eor	v14.16b, v14.16b, v17.16b
	add	v4.4s, v4.4s, v10.4s
	add	v7.4s, v7.4s, v27.4s
	orr	v2.16b, v2.16b, v15.16b
	tbl	v14.16b, { v14.16b }, v19.16b
	eor	v11.16b, v11.16b, v4.16b
	add	v7.4s, v7.4s, v9.4s
	add	v13.4s, v13.4s, v30.4s
	add	v1.4s, v1.4s, v14.4s
	tbl	v11.16b, { v11.16b }, v19.16b
	eor	v12.16b, v12.16b, v7.16b
	add	v13.4s, v13.4s, v2.4s
	ldur	q27, [x29, #-160]
	eor	v0.16b, v0.16b, v1.16b
	add	v5.4s, v5.4s, v11.4s
	tbl	v12.16b, { v12.16b }, v19.16b
	eor	v3.16b, v3.16b, v13.16b
	ushr	v15.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	eor	v10.16b, v5.16b, v10.16b
	add	v6.4s, v6.4s, v12.4s
	tbl	v3.16b, { v3.16b }, v19.16b
	orr	v0.16b, v0.16b, v15.16b
	ushr	v15.4s, v10.4s, #7
	shl	v10.4s, v10.4s, #25
	eor	v9.16b, v6.16b, v9.16b
	add	v8.4s, v8.4s, v3.4s
	orr	v10.16b, v10.16b, v15.16b
	ushr	v15.4s, v9.4s, #7
	shl	v9.4s, v9.4s, #25
	eor	v2.16b, v8.16b, v2.16b
	add	v17.4s, v17.4s, v27.4s
	mov	v28.16b, v25.16b
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	add	v17.4s, v17.4s, v10.4s
	add	v4.4s, v4.4s, v21.4s
	orr	v2.16b, v2.16b, v15.16b
	eor	v3.16b, v3.16b, v17.16b
	add	v4.4s, v4.4s, v9.4s
	add	v7.4s, v7.4s, v28.4s
	tbl	v3.16b, { v3.16b }, v16.16b
	eor	v14.16b, v14.16b, v4.16b
	add	v7.4s, v7.4s, v2.4s
	add	v13.4s, v13.4s, v29.4s
	mov	v25.16b, v31.16b
	add	v6.4s, v6.4s, v3.4s
	tbl	v14.16b, { v14.16b }, v16.16b
	eor	v11.16b, v11.16b, v7.16b
	add	v13.4s, v13.4s, v0.4s
	ldur	q31, [x29, #-96]
	eor	v10.16b, v6.16b, v10.16b
	add	v8.4s, v8.4s, v14.4s
	tbl	v11.16b, { v11.16b }, v16.16b
	eor	v12.16b, v12.16b, v13.16b
	ldur	q28, [x29, #-208]
	mov	v18.16b, v20.16b
	str	q20, [sp, #144]
	ushr	v15.4s, v10.4s, #12
	shl	v10.4s, v10.4s, #20
	eor	v9.16b, v8.16b, v9.16b
	add	v1.4s, v1.4s, v11.4s
	tbl	v12.16b, { v12.16b }, v16.16b
	ldur	q20, [x29, #-128]
	orr	v10.16b, v10.16b, v15.16b
	ushr	v15.4s, v9.4s, #12
	shl	v9.4s, v9.4s, #20
	eor	v2.16b, v1.16b, v2.16b
	add	v5.4s, v5.4s, v12.4s
	add	v17.4s, v17.4s, v24.4s
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	eor	v0.16b, v5.16b, v0.16b
	add	v17.4s, v17.4s, v10.4s
	add	v4.4s, v4.4s, v31.4s
	orr	v2.16b, v2.16b, v15.16b
	ushr	v15.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	eor	v3.16b, v3.16b, v17.16b
	add	v4.4s, v4.4s, v9.4s
	add	v7.4s, v7.4s, v28.4s
	orr	v0.16b, v0.16b, v15.16b
	tbl	v3.16b, { v3.16b }, v19.16b
	eor	v14.16b, v14.16b, v4.16b
	add	v7.4s, v7.4s, v2.4s
	add	v13.4s, v13.4s, v20.4s
	add	v6.4s, v6.4s, v3.4s
	tbl	v14.16b, { v14.16b }, v19.16b
	eor	v11.16b, v11.16b, v7.16b
	add	v13.4s, v13.4s, v0.4s
	eor	v10.16b, v6.16b, v10.16b
	add	v8.4s, v8.4s, v14.4s
	tbl	v11.16b, { v11.16b }, v19.16b
	eor	v12.16b, v12.16b, v13.16b
	ushr	v15.4s, v10.4s, #7
	shl	v10.4s, v10.4s, #25
	eor	v9.16b, v8.16b, v9.16b
	add	v1.4s, v1.4s, v11.4s
	tbl	v12.16b, { v12.16b }, v19.16b
	orr	v10.16b, v10.16b, v15.16b
	ushr	v15.4s, v9.4s, #7
	shl	v9.4s, v9.4s, #25
	eor	v2.16b, v1.16b, v2.16b
	add	v5.4s, v5.4s, v12.4s
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	eor	v0.16b, v5.16b, v0.16b
	orr	v2.16b, v2.16b, v15.16b
	ushr	v15.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	orr	v0.16b, v0.16b, v15.16b
	add	v17.4s, v17.4s, v18.4s
	add	v17.4s, v17.4s, v0.4s
	add	v4.4s, v4.4s, v22.4s
	eor	v14.16b, v14.16b, v17.16b
	add	v4.4s, v4.4s, v10.4s
	add	v7.4s, v7.4s, v30.4s
	tbl	v14.16b, { v14.16b }, v16.16b
	eor	v11.16b, v11.16b, v4.16b
	add	v7.4s, v7.4s, v9.4s
	add	v13.4s, v13.4s, v25.4s
	add	v1.4s, v1.4s, v14.4s
	tbl	v11.16b, { v11.16b }, v16.16b
	eor	v12.16b, v12.16b, v7.16b
	add	v13.4s, v13.4s, v2.4s
	eor	v0.16b, v0.16b, v1.16b
	add	v5.4s, v5.4s, v11.4s
	tbl	v12.16b, { v12.16b }, v16.16b
	eor	v3.16b, v3.16b, v13.16b
	add	v17.4s, v17.4s, v26.4s
	mov	v26.16b, v21.16b
	add	v4.4s, v4.4s, v21.4s
	ldur	q21, [x29, #-144]
	ushr	v15.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	eor	v10.16b, v5.16b, v10.16b
	add	v6.4s, v6.4s, v12.4s
	tbl	v3.16b, { v3.16b }, v16.16b
	orr	v0.16b, v0.16b, v15.16b
	ushr	v15.4s, v10.4s, #12
	shl	v10.4s, v10.4s, #20
	eor	v9.16b, v6.16b, v9.16b
	add	v8.4s, v8.4s, v3.4s
	orr	v10.16b, v10.16b, v15.16b
	ushr	v15.4s, v9.4s, #12
	shl	v9.4s, v9.4s, #20
	eor	v2.16b, v8.16b, v2.16b
	add	v17.4s, v17.4s, v0.4s
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	eor	v14.16b, v14.16b, v17.16b
	add	v4.4s, v4.4s, v10.4s
	add	v7.4s, v7.4s, v21.4s
	orr	v2.16b, v2.16b, v15.16b
	tbl	v14.16b, { v14.16b }, v19.16b
	eor	v11.16b, v11.16b, v4.16b
	add	v7.4s, v7.4s, v9.4s
	add	v13.4s, v13.4s, v28.4s
	add	v1.4s, v1.4s, v14.4s
	tbl	v11.16b, { v11.16b }, v19.16b
	eor	v12.16b, v12.16b, v7.16b
	add	v13.4s, v13.4s, v2.4s
	str	q23, [sp, #160]
	eor	v0.16b, v0.16b, v1.16b
	add	v5.4s, v5.4s, v11.4s
	tbl	v12.16b, { v12.16b }, v19.16b
	eor	v3.16b, v3.16b, v13.16b
	add	v17.4s, v17.4s, v23.4s
	ldur	q23, [x29, #-64]
	ushr	v15.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	eor	v10.16b, v5.16b, v10.16b
	add	v6.4s, v6.4s, v12.4s
	tbl	v3.16b, { v3.16b }, v19.16b
	orr	v0.16b, v0.16b, v15.16b
	ushr	v15.4s, v10.4s, #7
	shl	v10.4s, v10.4s, #25
	eor	v9.16b, v6.16b, v9.16b
	add	v8.4s, v8.4s, v3.4s
	orr	v10.16b, v10.16b, v15.16b
	ushr	v15.4s, v9.4s, #7
	shl	v9.4s, v9.4s, #25
	eor	v2.16b, v8.16b, v2.16b
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	add	v17.4s, v17.4s, v10.4s
	add	v4.4s, v4.4s, v23.4s
	orr	v2.16b, v2.16b, v15.16b
	eor	v3.16b, v3.16b, v17.16b
	add	v4.4s, v4.4s, v9.4s
	add	v7.4s, v7.4s, v24.4s
	tbl	v3.16b, { v3.16b }, v16.16b
	eor	v14.16b, v14.16b, v4.16b
	add	v7.4s, v7.4s, v2.4s
	add	v6.4s, v6.4s, v3.4s
	tbl	v14.16b, { v14.16b }, v16.16b
	eor	v11.16b, v11.16b, v7.16b
	add	v13.4s, v13.4s, v20.4s
	eor	v10.16b, v6.16b, v10.16b
	add	v8.4s, v8.4s, v14.4s
	tbl	v11.16b, { v11.16b }, v16.16b
	add	v13.4s, v13.4s, v0.4s
	ldr	q20, [sp, #176]
	ushr	v15.4s, v10.4s, #12
	shl	v10.4s, v10.4s, #20
	eor	v9.16b, v8.16b, v9.16b
	add	v1.4s, v1.4s, v11.4s
	eor	v12.16b, v12.16b, v13.16b
	orr	v10.16b, v10.16b, v15.16b
	ushr	v15.4s, v9.4s, #12
	shl	v9.4s, v9.4s, #20
	eor	v2.16b, v1.16b, v2.16b
	tbl	v12.16b, { v12.16b }, v16.16b
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	add	v5.4s, v5.4s, v12.4s
	add	v17.4s, v17.4s, v31.4s
	orr	v2.16b, v2.16b, v15.16b
	eor	v0.16b, v5.16b, v0.16b
	add	v17.4s, v17.4s, v10.4s
	add	v4.4s, v4.4s, v20.4s
	add	v7.4s, v7.4s, v29.4s
	ushr	v15.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	eor	v3.16b, v3.16b, v17.16b
	add	v4.4s, v4.4s, v9.4s
	add	v7.4s, v7.4s, v2.4s
	orr	v0.16b, v0.16b, v15.16b
	mov	v15.16b, v31.16b
	add	v17.4s, v17.4s, v22.4s
	eor	v31.16b, v14.16b, v4.16b
	eor	v22.16b, v11.16b, v7.16b
	add	v11.4s, v13.4s, v27.4s
	tbl	v3.16b, { v3.16b }, v19.16b
	add	v11.4s, v11.4s, v0.4s
	tbl	v31.16b, { v31.16b }, v19.16b
	add	v6.4s, v6.4s, v3.4s
	eor	v12.16b, v12.16b, v11.16b
	tbl	v22.16b, { v22.16b }, v19.16b
	add	v8.4s, v8.4s, v31.4s
	eor	v10.16b, v6.16b, v10.16b
	add	v30.4s, v11.4s, v30.4s
	tbl	v11.16b, { v12.16b }, v19.16b
	add	v1.4s, v1.4s, v22.4s
	eor	v9.16b, v8.16b, v9.16b
	ushr	v12.4s, v10.4s, #7
	shl	v10.4s, v10.4s, #25
	add	v5.4s, v5.4s, v11.4s
	eor	v2.16b, v1.16b, v2.16b
	orr	v10.16b, v10.16b, v12.16b
	ushr	v12.4s, v9.4s, #7
	shl	v9.4s, v9.4s, #25
	eor	v0.16b, v5.16b, v0.16b
	orr	v9.16b, v9.16b, v12.16b
	ushr	v12.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	orr	v2.16b, v2.16b, v12.16b
	ushr	v12.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	orr	v0.16b, v0.16b, v12.16b
	add	v4.4s, v4.4s, v26.4s
	add	v17.4s, v17.4s, v0.4s
	add	v7.4s, v7.4s, v28.4s
	mov	v18.16b, v27.16b
	eor	v31.16b, v31.16b, v17.16b
	add	v4.4s, v4.4s, v10.4s
	add	v27.4s, v30.4s, v2.4s
	eor	v22.16b, v22.16b, v4.16b
	add	v7.4s, v7.4s, v9.4s
	eor	v3.16b, v3.16b, v27.16b
	add	v26.4s, v27.4s, v29.4s
	tbl	v27.16b, { v31.16b }, v16.16b
	eor	v28.16b, v11.16b, v7.16b
	tbl	v22.16b, { v22.16b }, v16.16b
	add	v1.4s, v1.4s, v27.4s
	add	v4.4s, v4.4s, v23.4s
	ldr	q23, [sp, #144]
	tbl	v28.16b, { v28.16b }, v16.16b
	tbl	v3.16b, { v3.16b }, v16.16b
	add	v5.4s, v5.4s, v22.4s
	eor	v0.16b, v0.16b, v1.16b
	add	v6.4s, v6.4s, v28.4s
	add	v29.4s, v8.4s, v3.4s
	eor	v30.16b, v5.16b, v10.16b
	ushr	v8.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	eor	v31.16b, v6.16b, v9.16b
	orr	v0.16b, v0.16b, v8.16b
	ushr	v8.4s, v30.4s, #12
	shl	v30.4s, v30.4s, #20
	eor	v2.16b, v29.16b, v2.16b
	orr	v30.16b, v30.16b, v8.16b
	ushr	v8.4s, v31.4s, #12
	shl	v31.4s, v31.4s, #20
	add	v17.4s, v17.4s, v25.4s
	add	v7.4s, v7.4s, v23.4s
	orr	v31.16b, v31.16b, v8.16b
	ushr	v8.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	ldur	q23, [x29, #-176]
	orr	v2.16b, v2.16b, v8.16b
	add	v17.4s, v17.4s, v0.4s
	eor	v27.16b, v27.16b, v17.16b
	add	v4.4s, v4.4s, v30.4s
	add	v25.4s, v26.4s, v2.4s
	eor	v22.16b, v22.16b, v4.16b
	add	v4.4s, v4.4s, v24.4s
	add	v7.4s, v7.4s, v31.4s
	eor	v3.16b, v3.16b, v25.16b
	add	v24.4s, v25.4s, v18.4s
	tbl	v25.16b, { v27.16b }, v19.16b
	add	v17.4s, v17.4s, v23.4s
	eor	v23.16b, v28.16b, v7.16b
	tbl	v22.16b, { v22.16b }, v19.16b
	add	v1.4s, v1.4s, v25.4s
	tbl	v23.16b, { v23.16b }, v19.16b
	tbl	v3.16b, { v3.16b }, v19.16b
	add	v5.4s, v5.4s, v22.4s
	eor	v0.16b, v0.16b, v1.16b
	add	v6.4s, v6.4s, v23.4s
	add	v26.4s, v29.4s, v3.4s
	eor	v27.16b, v5.16b, v30.16b
	ushr	v29.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	eor	v28.16b, v6.16b, v31.16b
	orr	v0.16b, v0.16b, v29.16b
	ushr	v29.4s, v27.4s, #7
	shl	v27.4s, v27.4s, #25
	eor	v2.16b, v26.16b, v2.16b
	orr	v27.16b, v27.16b, v29.16b
	ushr	v29.4s, v28.4s, #7
	shl	v28.4s, v28.4s, #25
	ldur	q18, [x29, #-128]
	orr	v28.16b, v28.16b, v29.16b
	ushr	v29.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	add	v7.4s, v7.4s, v15.4s
	orr	v2.16b, v2.16b, v29.16b
	add	v17.4s, v17.4s, v27.4s
	add	v4.4s, v4.4s, v28.4s
	add	v7.4s, v7.4s, v2.4s
	eor	v3.16b, v3.16b, v17.16b
	add	v17.4s, v17.4s, v20.4s
	eor	v20.16b, v25.16b, v4.16b
	add	v4.4s, v4.4s, v21.4s
	eor	v21.16b, v22.16b, v7.16b
	add	v7.4s, v7.4s, v18.4s
	add	v18.4s, v24.4s, v0.4s
	eor	v22.16b, v23.16b, v18.16b
	ldr	q23, [sp, #160]
	tbl	v3.16b, { v3.16b }, v16.16b
	tbl	v20.16b, { v20.16b }, v16.16b
	add	v6.4s, v6.4s, v3.4s
	add	v18.4s, v18.4s, v23.4s
	tbl	v21.16b, { v21.16b }, v16.16b
	tbl	v16.16b, { v22.16b }, v16.16b
	add	v22.4s, v26.4s, v20.4s
	eor	v23.16b, v6.16b, v27.16b
	add	v1.4s, v1.4s, v21.4s
	eor	v24.16b, v22.16b, v28.16b
	ushr	v25.4s, v23.4s, #12
	shl	v23.4s, v23.4s, #20
	add	v5.4s, v5.4s, v16.4s
	eor	v2.16b, v1.16b, v2.16b
	orr	v23.16b, v23.16b, v25.16b
	ushr	v25.4s, v24.4s, #12
	shl	v24.4s, v24.4s, #20
	eor	v0.16b, v5.16b, v0.16b
	orr	v24.16b, v24.16b, v25.16b
	ushr	v25.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	orr	v2.16b, v2.16b, v25.16b
	ushr	v25.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	orr	v0.16b, v0.16b, v25.16b
	add	v25.4s, v7.4s, v2.4s
	add	v26.4s, v18.4s, v0.4s
	eor	v18.16b, v21.16b, v25.16b
	add	v17.4s, v17.4s, v23.4s
	add	v4.4s, v4.4s, v24.4s
	eor	v16.16b, v16.16b, v26.16b
	tbl	v21.16b, { v18.16b }, v19.16b
	eor	v3.16b, v3.16b, v17.16b
	eor	v7.16b, v20.16b, v4.16b
	tbl	v16.16b, { v16.16b }, v19.16b
	add	v1.4s, v1.4s, v21.4s
	tbl	v3.16b, { v3.16b }, v19.16b
	tbl	v20.16b, { v7.16b }, v19.16b
	eor	v2.16b, v1.16b, v2.16b
	eor	v7.16b, v1.16b, v17.16b
	add	v1.4s, v5.4s, v16.4s
	eor	v0.16b, v1.16b, v0.16b
	eor	v18.16b, v1.16b, v4.16b
	add	v1.4s, v6.4s, v3.4s
	eor	v4.16b, v1.16b, v23.16b
	eor	v6.16b, v25.16b, v1.16b
	add	v1.4s, v22.4s, v20.4s
	eor	v5.16b, v1.16b, v24.16b
	eor	v17.16b, v26.16b, v1.16b
	ushr	v1.4s, v4.4s, #7
	shl	v4.4s, v4.4s, #25
	orr	v1.16b, v4.16b, v1.16b
	ushr	v4.4s, v5.4s, #7
	shl	v5.4s, v5.4s, #25
	orr	v4.16b, v5.16b, v4.16b
	ushr	v5.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	orr	v2.16b, v2.16b, v5.16b
	ushr	v5.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	orr	v0.16b, v0.16b, v5.16b
	eor	v10.16b, v0.16b, v20.16b
	eor	v11.16b, v1.16b, v21.16b
	eor	v19.16b, v4.16b, v16.16b
	cmp	x0, x22
	eor	v16.16b, v2.16b, v3.16b
	mov	w6, w19
	b.ne	.LBB2_4
.LBB2_7:
	zip1	v0.4s, v7.4s, v18.4s
	zip2	v1.4s, v7.4s, v18.4s
	zip1	v2.4s, v6.4s, v17.4s
	zip2	v3.4s, v6.4s, v17.4s
	zip1	v4.4s, v10.4s, v11.4s
	zip2	v5.4s, v10.4s, v11.4s
	zip1	v6.4s, v19.4s, v16.4s
	zip2	v7.4s, v19.4s, v16.4s
	add	x15, x20, #4
	tst	w5, #0x1
	sub	x28, x28, #4
	zip1	v16.2d, v0.2d, v2.2d
	zip2	v0.2d, v0.2d, v2.2d
	zip1	v2.2d, v1.2d, v3.2d
	zip2	v1.2d, v1.2d, v3.2d
	zip1	v3.2d, v4.2d, v6.2d
	zip2	v4.2d, v4.2d, v6.2d
	zip1	v6.2d, v5.2d, v7.2d
	zip2	v5.2d, v5.2d, v7.2d
	add	x24, x24, #32
	csel	x20, x15, x20, ne
	cmp	x28, #3
	stp	q16, q3, [x26]
	stp	q0, q4, [x26, #32]
	stp	q2, q6, [x26, #64]
	stp	q1, q5, [x26, #96]
	add	x26, x26, #128
	b.hi	.LBB2_2
.LBB2_8:
	cbz	x28, .LBB2_16
	orr	w8, w7, w19
	and	x21, x5, #0x1
	stur	w8, [x29, #-64]
.LBB2_10:
	ldr	x8, [sp, #40]
	ldr	x25, [x24]
	ldur	w4, [x29, #-64]
	ldp	q1, q0, [x8]
	mov	x8, x22
	stp	q1, q0, [x29, #-48]
.LBB2_11:
	subs	x23, x8, #1
	b.eq	.LBB2_13
	cbnz	x8, .LBB2_14
	b	.LBB2_15
.LBB2_13:
	orr	w4, w4, w27
.LBB2_14:
	sub	x0, x29, #48
	mov	w2, #64
	mov	x1, x25
	mov	x3, x20
	bl	zfs_blake3_compress_in_place_sse41
	add	x25, x25, #64
	mov	x8, x23
	mov	w4, w19
	b	.LBB2_11
.LBB2_15:
	ldp	q0, q1, [x29, #-48]
	add	x20, x20, x21
	add	x24, x24, #8
	subs	x28, x28, #1
	stp	q0, q1, [x26], #32
	b.ne	.LBB2_10
.LBB2_16:
	add	sp, sp, #448
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
	ret
.Lfunc_end2:
	.size	zfs_blake3_hash_many_sse41, .Lfunc_end2-zfs_blake3_hash_many_sse41
	.cfi_endproc
	.section	".note.GNU-stack","",@progbits
#endif
