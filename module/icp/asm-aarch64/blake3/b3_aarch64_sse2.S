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
 * Based on BLAKE3 v1.3.1, https://github.com/BLAKE3-team/BLAKE3
 * Copyright (c) 2019-2022 Samuel Neves and Matthew Krupcale
 * Copyright (c) 2022 Tino Reichardt <milky-zfs@mcmilk.de>
 *
 * This is converted assembly: SSE2 -> ARMv8-A
 * Used tools: SIMDe https://github.com/simd-everywhere/simde
 */

#if defined(__aarch64__)
	.text
	.section	.rodata.cst16,"aM",@progbits,16
	.p2align	4
.LCPI0_0:
	.word	1779033703
	.word	3144134277
	.word	1013904242
	.word	2773480762
.LCPI0_1:
	.xword	0
	.xword	-4294967296
.LCPI0_2:
	.xword	-1
	.xword	4294967295
	.text
	.globl	zfs_blake3_compress_in_place_sse2
	.p2align	2
	.type	zfs_blake3_compress_in_place_sse2,@function
zfs_blake3_compress_in_place_sse2:
	.cfi_startproc
	ldp	q3, q2, [x0]
	ldp	q5, q6, [x1]
	add	x10, x1, #32
	lsr	x11, x3, #32
	fmov	s4, w3
	ld2	{ v17.4s, v18.4s }, [x10]
	adrp	x10, .LCPI0_2
	and	w8, w2, #0xff
	mov	v4.s[1], w11
	ldr	q1, [x10, :lo12:.LCPI0_2]
	and	w9, w4, #0xff
	adrp	x12, .LCPI0_0
	mov	v4.s[2], w8
	uzp1	v19.4s, v5.4s, v6.4s
	add	v3.4s, v2.4s, v3.4s
	ldr	q7, [x12, :lo12:.LCPI0_0]
	mov	v4.s[3], w9
	add	v3.4s, v3.4s, v19.4s
	uzp2	v5.4s, v5.4s, v6.4s
	ext	v21.16b, v18.16b, v18.16b, #12
	uzp1	v6.4s, v19.4s, v19.4s
	ext	v22.16b, v19.16b, v19.16b, #12
	eor	v4.16b, v3.16b, v4.16b
	ext	v20.16b, v17.16b, v17.16b, #12
	ext	v6.16b, v6.16b, v19.16b, #8
	ext	v19.16b, v19.16b, v22.16b, #12
	zip1	v22.2d, v21.2d, v5.2d
	rev32	v24.8h, v4.8h
	mov	v4.16b, v1.16b
	zip2	v23.4s, v5.4s, v21.4s
	uzp2	v6.4s, v6.4s, v5.4s
	bsl	v4.16b, v22.16b, v20.16b
	add	v3.4s, v3.4s, v5.4s
	zip1	v5.4s, v23.4s, v20.4s
	zip1	v22.4s, v20.4s, v23.4s
	add	v23.4s, v24.4s, v7.4s
	ext	v7.16b, v6.16b, v6.16b, #4
	ext	v25.16b, v4.16b, v4.16b, #12
	ext	v5.16b, v22.16b, v5.16b, #8
	eor	v2.16b, v23.16b, v2.16b
	uzp1	v4.4s, v4.4s, v25.4s
	uzp1	v22.4s, v7.4s, v7.4s
	ext	v25.16b, v7.16b, v7.16b, #12
	ext	v22.16b, v22.16b, v7.16b, #8
	ext	v7.16b, v7.16b, v25.16b, #12
	ushr	v25.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	orr	v2.16b, v2.16b, v25.16b
	add	v3.4s, v3.4s, v2.4s
	eor	v24.16b, v3.16b, v24.16b
	add	v3.4s, v3.4s, v17.4s
	ushr	v17.4s, v24.4s, #8
	shl	v18.4s, v24.4s, #24
	orr	v17.16b, v18.16b, v17.16b
	add	v18.4s, v17.4s, v23.4s
	eor	v2.16b, v18.16b, v2.16b
	ushr	v23.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	ext	v3.16b, v3.16b, v3.16b, #12
	orr	v2.16b, v2.16b, v23.16b
	ext	v17.16b, v17.16b, v17.16b, #8
	add	v3.4s, v2.4s, v3.4s
	adrp	x11, .LCPI0_1
	eor	v17.16b, v3.16b, v17.16b
	ldr	q16, [x11, :lo12:.LCPI0_1]
	ext	v18.16b, v18.16b, v18.16b, #4
	rev32	v24.8h, v17.8h
	movi	v0.2d, #0xffffffff00000000
	add	v23.4s, v3.4s, v21.4s
	mov	v21.s[1], v20.s[2]
	add	v20.4s, v18.4s, v24.4s
	bit	v19.16b, v21.16b, v0.16b
	eor	v3.16b, v20.16b, v2.16b
	uzp2	v2.4s, v22.4s, v19.4s
	zip1	v17.2d, v5.2d, v19.2d
	zip2	v18.4s, v19.4s, v5.4s
	ushr	v21.4s, v3.4s, #12
	shl	v3.4s, v3.4s, #20
	ext	v22.16b, v2.16b, v2.16b, #4
	bsl	v16.16b, v4.16b, v17.16b
	zip1	v17.4s, v18.4s, v4.4s
	zip1	v18.4s, v4.4s, v18.4s
	orr	v21.16b, v3.16b, v21.16b
	ext	v25.16b, v16.16b, v16.16b, #12
	ext	v3.16b, v18.16b, v17.16b, #8
	uzp1	v18.4s, v22.4s, v22.4s
	ext	v26.16b, v22.16b, v22.16b, #12
	add	v23.4s, v23.4s, v21.4s
	uzp1	v17.4s, v16.4s, v25.4s
	ext	v16.16b, v18.16b, v22.16b, #8
	ext	v18.16b, v22.16b, v26.16b, #12
	eor	v22.16b, v23.16b, v24.16b
	add	v6.4s, v23.4s, v6.4s
	ushr	v23.4s, v22.4s, #8
	shl	v22.4s, v22.4s, #24
	orr	v22.16b, v22.16b, v23.16b
	add	v20.4s, v22.4s, v20.4s
	eor	v21.16b, v20.16b, v21.16b
	ushr	v23.4s, v21.4s, #7
	shl	v21.4s, v21.4s, #25
	ext	v6.16b, v6.16b, v6.16b, #4
	orr	v21.16b, v21.16b, v23.16b
	ext	v22.16b, v22.16b, v22.16b, #8
	add	v6.4s, v21.4s, v6.4s
	eor	v22.16b, v6.16b, v22.16b
	ext	v20.16b, v20.16b, v20.16b, #12
	add	v6.4s, v6.4s, v19.4s
	rev32	v19.8h, v22.8h
	add	v20.4s, v20.4s, v19.4s
	eor	v21.16b, v20.16b, v21.16b
	ushr	v22.4s, v21.4s, #12
	shl	v21.4s, v21.4s, #20
	orr	v21.16b, v21.16b, v22.16b
	add	v6.4s, v6.4s, v21.4s
	eor	v19.16b, v6.16b, v19.16b
	ushr	v22.4s, v19.4s, #8
	shl	v19.4s, v19.4s, #24
	orr	v19.16b, v19.16b, v22.16b
	add	v20.4s, v19.4s, v20.4s
	eor	v21.16b, v20.16b, v21.16b
	ext	v6.16b, v6.16b, v6.16b, #12
	ushr	v22.4s, v21.4s, #7
	shl	v21.4s, v21.4s, #25
	add	v6.4s, v6.4s, v4.4s
	orr	v21.16b, v21.16b, v22.16b
	ext	v19.16b, v19.16b, v19.16b, #8
	add	v6.4s, v6.4s, v21.4s
	eor	v19.16b, v6.16b, v19.16b
	ext	v20.16b, v20.16b, v20.16b, #4
	rev32	v19.8h, v19.8h
	add	v20.4s, v20.4s, v19.4s
	add	v6.4s, v6.4s, v5.4s
	mov	v5.s[1], v4.s[2]
	eor	v4.16b, v20.16b, v21.16b
	ushr	v21.4s, v4.4s, #12
	shl	v4.4s, v4.4s, #20
	orr	v21.16b, v4.16b, v21.16b
	add	v6.4s, v6.4s, v21.4s
	eor	v19.16b, v6.16b, v19.16b
	add	v2.4s, v6.4s, v2.4s
	ushr	v6.4s, v19.4s, #8
	shl	v19.4s, v19.4s, #24
	orr	v6.16b, v19.16b, v6.16b
	add	v19.4s, v6.4s, v20.4s
	eor	v20.16b, v19.16b, v21.16b
	ushr	v21.4s, v20.4s, #7
	shl	v20.4s, v20.4s, #25
	ext	v2.16b, v2.16b, v2.16b, #4
	orr	v20.16b, v20.16b, v21.16b
	ext	v6.16b, v6.16b, v6.16b, #8
	add	v2.4s, v20.4s, v2.4s
	eor	v6.16b, v2.16b, v6.16b
	ext	v19.16b, v19.16b, v19.16b, #12
	rev32	v6.8h, v6.8h
	add	v19.4s, v19.4s, v6.4s
	mov	v22.16b, v0.16b
	eor	v20.16b, v19.16b, v20.16b
	bsl	v22.16b, v5.16b, v7.16b
	ushr	v21.4s, v20.4s, #12
	shl	v20.4s, v20.4s, #20
	add	v2.4s, v2.4s, v22.4s
	orr	v20.16b, v20.16b, v21.16b
	add	v2.4s, v2.4s, v20.4s
	eor	v6.16b, v2.16b, v6.16b
	ushr	v21.4s, v6.4s, #8
	shl	v6.4s, v6.4s, #24
	orr	v6.16b, v6.16b, v21.16b
	add	v19.4s, v6.4s, v19.4s
	eor	v20.16b, v19.16b, v20.16b
	ext	v2.16b, v2.16b, v2.16b, #12
	ushr	v21.4s, v20.4s, #7
	shl	v20.4s, v20.4s, #25
	add	v2.4s, v2.4s, v17.4s
	orr	v20.16b, v20.16b, v21.16b
	ext	v6.16b, v6.16b, v6.16b, #8
	add	v2.4s, v2.4s, v20.4s
	eor	v6.16b, v2.16b, v6.16b
	uzp2	v5.4s, v16.4s, v22.4s
	zip1	v7.2d, v3.2d, v22.2d
	zip2	v16.4s, v22.4s, v3.4s
	ext	v19.16b, v19.16b, v19.16b, #4
	rev32	v22.8h, v6.8h
	ext	v23.16b, v5.16b, v5.16b, #4
	bif	v7.16b, v17.16b, v1.16b
	zip1	v24.4s, v16.4s, v17.4s
	zip1	v16.4s, v17.4s, v16.4s
	add	v21.4s, v2.4s, v3.4s
	mov	v3.s[1], v17.s[2]
	add	v17.4s, v19.4s, v22.4s
	mov	v19.16b, v0.16b
	ext	v25.16b, v7.16b, v7.16b, #12
	ext	v4.16b, v16.16b, v24.16b, #8
	uzp1	v16.4s, v23.4s, v23.4s
	bsl	v19.16b, v3.16b, v18.16b
	eor	v2.16b, v17.16b, v20.16b
	uzp1	v7.4s, v7.4s, v25.4s
	ext	v25.16b, v16.16b, v23.16b, #8
	zip1	v3.2d, v4.2d, v19.2d
	ushr	v20.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	ext	v24.16b, v23.16b, v23.16b, #12
	uzp2	v6.4s, v25.4s, v19.4s
	zip2	v18.4s, v19.4s, v4.4s
	bif	v3.16b, v7.16b, v1.16b
	orr	v20.16b, v2.16b, v20.16b
	ext	v16.16b, v23.16b, v24.16b, #12
	ext	v23.16b, v6.16b, v6.16b, #4
	zip1	v24.4s, v18.4s, v7.4s
	zip1	v18.4s, v7.4s, v18.4s
	ext	v25.16b, v3.16b, v3.16b, #12
	add	v21.4s, v21.4s, v20.4s
	ext	v2.16b, v18.16b, v24.16b, #8
	uzp1	v18.4s, v23.4s, v23.4s
	ext	v24.16b, v23.16b, v23.16b, #12
	uzp1	v3.4s, v3.4s, v25.4s
	eor	v22.16b, v21.16b, v22.16b
	ext	v25.16b, v18.16b, v23.16b, #8
	dup	v18.4s, v2.s[3]
	ext	v23.16b, v23.16b, v24.16b, #12
	add	v5.4s, v21.4s, v5.4s
	trn1	v21.4s, v3.4s, v3.4s
	ushr	v24.4s, v22.4s, #8
	shl	v22.4s, v22.4s, #24
	ext	v18.16b, v21.16b, v18.16b, #8
	orr	v21.16b, v22.16b, v24.16b
	add	v17.4s, v21.4s, v17.4s
	eor	v20.16b, v17.16b, v20.16b
	ushr	v22.4s, v20.4s, #7
	shl	v20.4s, v20.4s, #25
	ext	v5.16b, v5.16b, v5.16b, #4
	orr	v20.16b, v20.16b, v22.16b
	ext	v21.16b, v21.16b, v21.16b, #8
	add	v5.4s, v20.4s, v5.4s
	eor	v21.16b, v5.16b, v21.16b
	ext	v17.16b, v17.16b, v17.16b, #12
	add	v5.4s, v5.4s, v19.4s
	rev32	v19.8h, v21.8h
	add	v17.4s, v17.4s, v19.4s
	eor	v20.16b, v17.16b, v20.16b
	ushr	v21.4s, v20.4s, #12
	shl	v20.4s, v20.4s, #20
	orr	v20.16b, v20.16b, v21.16b
	add	v5.4s, v5.4s, v20.4s
	eor	v19.16b, v5.16b, v19.16b
	ushr	v21.4s, v19.4s, #8
	shl	v19.4s, v19.4s, #24
	orr	v19.16b, v19.16b, v21.16b
	add	v17.4s, v19.4s, v17.4s
	eor	v20.16b, v17.16b, v20.16b
	ext	v5.16b, v5.16b, v5.16b, #12
	ushr	v21.4s, v20.4s, #7
	shl	v20.4s, v20.4s, #25
	add	v5.4s, v5.4s, v7.4s
	orr	v20.16b, v20.16b, v21.16b
	ext	v19.16b, v19.16b, v19.16b, #8
	add	v5.4s, v5.4s, v20.4s
	eor	v19.16b, v5.16b, v19.16b
	ext	v17.16b, v17.16b, v17.16b, #4
	rev32	v22.8h, v19.8h
	add	v21.4s, v5.4s, v4.4s
	mov	v4.s[1], v7.s[2]
	add	v19.4s, v17.4s, v22.4s
	bit	v16.16b, v4.16b, v0.16b
	eor	v5.16b, v19.16b, v20.16b
	uzp2	v4.4s, v25.4s, v16.4s
	zip1	v7.2d, v2.2d, v16.2d
	zip2	v17.4s, v16.4s, v2.4s
	ushr	v20.4s, v5.4s, #12
	shl	v5.4s, v5.4s, #20
	ext	v24.16b, v4.16b, v4.16b, #4
	bif	v7.16b, v3.16b, v1.16b
	zip1	v25.4s, v17.4s, v3.4s
	zip1	v17.4s, v3.4s, v17.4s
	orr	v20.16b, v5.16b, v20.16b
	ext	v26.16b, v7.16b, v7.16b, #12
	ext	v5.16b, v17.16b, v25.16b, #8
	uzp1	v17.4s, v24.4s, v24.4s
	ext	v25.16b, v24.16b, v24.16b, #12
	bit	v23.16b, v18.16b, v0.16b
	add	v21.4s, v21.4s, v20.4s
	uzp1	v7.4s, v7.4s, v26.4s
	ext	v26.16b, v17.16b, v24.16b, #8
	ext	v17.16b, v24.16b, v25.16b, #12
	eor	v22.16b, v21.16b, v22.16b
	add	v6.4s, v21.4s, v6.4s
	zip1	v21.2d, v5.2d, v23.2d
	zip2	v24.4s, v23.4s, v5.4s
	bif	v21.16b, v7.16b, v1.16b
	zip1	v1.4s, v24.4s, v7.4s
	zip1	v24.4s, v7.4s, v24.4s
	ext	v1.16b, v24.16b, v1.16b, #8
	ushr	v24.4s, v22.4s, #8
	shl	v22.4s, v22.4s, #24
	orr	v22.16b, v22.16b, v24.16b
	add	v19.4s, v22.4s, v19.4s
	ext	v24.16b, v21.16b, v21.16b, #12
	eor	v20.16b, v19.16b, v20.16b
	uzp1	v21.4s, v21.4s, v24.4s
	ushr	v24.4s, v20.4s, #7
	shl	v20.4s, v20.4s, #25
	orr	v20.16b, v20.16b, v24.16b
	ext	v6.16b, v6.16b, v6.16b, #4
	ext	v22.16b, v22.16b, v22.16b, #8
	add	v6.4s, v20.4s, v6.4s
	eor	v22.16b, v6.16b, v22.16b
	ext	v19.16b, v19.16b, v19.16b, #12
	add	v6.4s, v6.4s, v16.4s
	rev32	v16.8h, v22.8h
	add	v19.4s, v19.4s, v16.4s
	eor	v20.16b, v19.16b, v20.16b
	ushr	v22.4s, v20.4s, #12
	shl	v20.4s, v20.4s, #20
	orr	v20.16b, v20.16b, v22.16b
	add	v6.4s, v6.4s, v20.4s
	eor	v16.16b, v6.16b, v16.16b
	ext	v6.16b, v6.16b, v6.16b, #12
	add	v3.4s, v6.4s, v3.4s
	ushr	v6.4s, v16.4s, #8
	shl	v16.4s, v16.4s, #24
	orr	v6.16b, v16.16b, v6.16b
	add	v16.4s, v6.4s, v19.4s
	eor	v19.16b, v16.16b, v20.16b
	ushr	v20.4s, v19.4s, #7
	shl	v19.4s, v19.4s, #25
	orr	v19.16b, v19.16b, v20.16b
	ext	v6.16b, v6.16b, v6.16b, #8
	add	v3.4s, v3.4s, v19.4s
	eor	v6.16b, v3.16b, v6.16b
	ext	v16.16b, v16.16b, v16.16b, #4
	add	v2.4s, v3.4s, v2.4s
	rev32	v3.8h, v6.8h
	add	v6.4s, v16.4s, v3.4s
	eor	v16.16b, v6.16b, v19.16b
	ushr	v19.4s, v16.4s, #12
	shl	v16.4s, v16.4s, #20
	orr	v16.16b, v16.16b, v19.16b
	add	v2.4s, v2.4s, v16.4s
	eor	v3.16b, v2.16b, v3.16b
	add	v2.4s, v2.4s, v4.4s
	ushr	v4.4s, v3.4s, #8
	shl	v3.4s, v3.4s, #24
	orr	v3.16b, v3.16b, v4.16b
	add	v4.4s, v3.4s, v6.4s
	eor	v6.16b, v4.16b, v16.16b
	ushr	v16.4s, v6.4s, #7
	shl	v6.4s, v6.4s, #25
	ext	v2.16b, v2.16b, v2.16b, #4
	orr	v6.16b, v6.16b, v16.16b
	ext	v3.16b, v3.16b, v3.16b, #8
	add	v2.4s, v6.4s, v2.4s
	eor	v3.16b, v2.16b, v3.16b
	ext	v4.16b, v4.16b, v4.16b, #12
	rev32	v3.8h, v3.8h
	add	v4.4s, v4.4s, v3.4s
	eor	v6.16b, v4.16b, v6.16b
	ushr	v16.4s, v6.4s, #12
	shl	v6.4s, v6.4s, #20
	add	v2.4s, v2.4s, v23.4s
	orr	v6.16b, v6.16b, v16.16b
	add	v2.4s, v2.4s, v6.4s
	eor	v3.16b, v2.16b, v3.16b
	ushr	v16.4s, v3.4s, #8
	shl	v3.4s, v3.4s, #24
	orr	v3.16b, v3.16b, v16.16b
	add	v4.4s, v3.4s, v4.4s
	eor	v6.16b, v4.16b, v6.16b
	ext	v2.16b, v2.16b, v2.16b, #12
	ushr	v16.4s, v6.4s, #7
	shl	v6.4s, v6.4s, #25
	add	v2.4s, v2.4s, v7.4s
	orr	v6.16b, v6.16b, v16.16b
	ext	v3.16b, v3.16b, v3.16b, #8
	add	v2.4s, v2.4s, v6.4s
	eor	v3.16b, v2.16b, v3.16b
	ext	v4.16b, v4.16b, v4.16b, #4
	rev32	v3.8h, v3.8h
	add	v2.4s, v2.4s, v5.4s
	mov	v5.s[1], v7.s[2]
	add	v4.4s, v4.4s, v3.4s
	bsl	v0.16b, v5.16b, v17.16b
	eor	v5.16b, v4.16b, v6.16b
	ushr	v6.4s, v5.4s, #12
	shl	v5.4s, v5.4s, #20
	orr	v5.16b, v5.16b, v6.16b
	add	v2.4s, v2.4s, v5.4s
	eor	v3.16b, v2.16b, v3.16b
	ushr	v6.4s, v3.4s, #8
	shl	v3.4s, v3.4s, #24
	orr	v3.16b, v3.16b, v6.16b
	add	v4.4s, v3.4s, v4.4s
	uzp2	v18.4s, v26.4s, v18.4s
	eor	v5.16b, v4.16b, v5.16b
	add	v2.4s, v2.4s, v18.4s
	ushr	v6.4s, v5.4s, #7
	shl	v5.4s, v5.4s, #25
	ext	v2.16b, v2.16b, v2.16b, #4
	orr	v5.16b, v5.16b, v6.16b
	ext	v3.16b, v3.16b, v3.16b, #8
	add	v2.4s, v5.4s, v2.4s
	eor	v3.16b, v2.16b, v3.16b
	ext	v4.16b, v4.16b, v4.16b, #12
	add	v0.4s, v2.4s, v0.4s
	rev32	v2.8h, v3.8h
	add	v3.4s, v4.4s, v2.4s
	eor	v4.16b, v3.16b, v5.16b
	ushr	v5.4s, v4.4s, #12
	shl	v4.4s, v4.4s, #20
	orr	v4.16b, v4.16b, v5.16b
	add	v0.4s, v0.4s, v4.4s
	eor	v2.16b, v0.16b, v2.16b
	ushr	v5.4s, v2.4s, #8
	shl	v2.4s, v2.4s, #24
	orr	v2.16b, v2.16b, v5.16b
	add	v3.4s, v2.4s, v3.4s
	eor	v4.16b, v3.16b, v4.16b
	ext	v0.16b, v0.16b, v0.16b, #12
	ushr	v5.4s, v4.4s, #7
	shl	v4.4s, v4.4s, #25
	add	v0.4s, v0.4s, v21.4s
	orr	v4.16b, v4.16b, v5.16b
	ext	v2.16b, v2.16b, v2.16b, #8
	add	v0.4s, v0.4s, v4.4s
	eor	v2.16b, v0.16b, v2.16b
	ext	v3.16b, v3.16b, v3.16b, #4
	add	v0.4s, v0.4s, v1.4s
	rev32	v1.8h, v2.8h
	add	v2.4s, v3.4s, v1.4s
	eor	v3.16b, v2.16b, v4.16b
	ushr	v4.4s, v3.4s, #12
	shl	v3.4s, v3.4s, #20
	orr	v3.16b, v3.16b, v4.16b
	add	v0.4s, v0.4s, v3.4s
	eor	v1.16b, v0.16b, v1.16b
	ushr	v4.4s, v1.4s, #8
	shl	v1.4s, v1.4s, #24
	orr	v1.16b, v1.16b, v4.16b
	add	v2.4s, v1.4s, v2.4s
	eor	v3.16b, v2.16b, v3.16b
	ext	v0.16b, v0.16b, v0.16b, #4
	ext	v2.16b, v2.16b, v2.16b, #12
	ushr	v4.4s, v3.4s, #7
	shl	v3.4s, v3.4s, #25
	ext	v1.16b, v1.16b, v1.16b, #8
	eor	v0.16b, v2.16b, v0.16b
	orr	v2.16b, v3.16b, v4.16b
	eor	v1.16b, v2.16b, v1.16b
	stp	q0, q1, [x0]
	ret
.Lfunc_end0:
	.size	zfs_blake3_compress_in_place_sse2, .Lfunc_end0-zfs_blake3_compress_in_place_sse2
	.cfi_endproc

	.section	.rodata.cst16,"aM",@progbits,16
	.p2align	4
.LCPI1_0:
	.word	1779033703
	.word	3144134277
	.word	1013904242
	.word	2773480762
.LCPI1_1:
	.xword	0
	.xword	-4294967296
.LCPI1_2:
	.xword	-1
	.xword	4294967295
	.text
	.globl	zfs_blake3_compress_xof_sse2
	.p2align	2
	.type	zfs_blake3_compress_xof_sse2,@function
zfs_blake3_compress_xof_sse2:
	.cfi_startproc
	ldp	q3, q2, [x0]
	ldp	q5, q6, [x1]
	add	x10, x1, #32
	lsr	x11, x3, #32
	fmov	s4, w3
	ld2	{ v17.4s, v18.4s }, [x10]
	adrp	x10, .LCPI1_2
	and	w8, w2, #0xff
	mov	v4.s[1], w11
	ldr	q1, [x10, :lo12:.LCPI1_2]
	and	w9, w4, #0xff
	adrp	x12, .LCPI1_0
	mov	v4.s[2], w8
	uzp1	v19.4s, v5.4s, v6.4s
	add	v3.4s, v2.4s, v3.4s
	ldr	q7, [x12, :lo12:.LCPI1_0]
	mov	v4.s[3], w9
	add	v3.4s, v3.4s, v19.4s
	uzp2	v5.4s, v5.4s, v6.4s
	ext	v21.16b, v18.16b, v18.16b, #12
	uzp1	v6.4s, v19.4s, v19.4s
	ext	v22.16b, v19.16b, v19.16b, #12
	eor	v4.16b, v3.16b, v4.16b
	ext	v20.16b, v17.16b, v17.16b, #12
	ext	v6.16b, v6.16b, v19.16b, #8
	ext	v19.16b, v19.16b, v22.16b, #12
	zip1	v22.2d, v21.2d, v5.2d
	rev32	v24.8h, v4.8h
	mov	v4.16b, v1.16b
	zip2	v23.4s, v5.4s, v21.4s
	uzp2	v6.4s, v6.4s, v5.4s
	bsl	v4.16b, v22.16b, v20.16b
	add	v3.4s, v3.4s, v5.4s
	zip1	v5.4s, v23.4s, v20.4s
	zip1	v22.4s, v20.4s, v23.4s
	add	v23.4s, v24.4s, v7.4s
	ext	v7.16b, v6.16b, v6.16b, #4
	ext	v25.16b, v4.16b, v4.16b, #12
	ext	v5.16b, v22.16b, v5.16b, #8
	eor	v2.16b, v23.16b, v2.16b
	uzp1	v4.4s, v4.4s, v25.4s
	uzp1	v22.4s, v7.4s, v7.4s
	ext	v25.16b, v7.16b, v7.16b, #12
	ext	v22.16b, v22.16b, v7.16b, #8
	ext	v7.16b, v7.16b, v25.16b, #12
	ushr	v25.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	orr	v2.16b, v2.16b, v25.16b
	add	v3.4s, v3.4s, v2.4s
	eor	v24.16b, v3.16b, v24.16b
	add	v3.4s, v3.4s, v17.4s
	ushr	v17.4s, v24.4s, #8
	shl	v18.4s, v24.4s, #24
	orr	v17.16b, v18.16b, v17.16b
	add	v18.4s, v17.4s, v23.4s
	eor	v2.16b, v18.16b, v2.16b
	ushr	v23.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	ext	v3.16b, v3.16b, v3.16b, #12
	orr	v2.16b, v2.16b, v23.16b
	ext	v17.16b, v17.16b, v17.16b, #8
	add	v3.4s, v2.4s, v3.4s
	adrp	x11, .LCPI1_1
	eor	v17.16b, v3.16b, v17.16b
	ldr	q16, [x11, :lo12:.LCPI1_1]
	ext	v18.16b, v18.16b, v18.16b, #4
	rev32	v24.8h, v17.8h
	movi	v0.2d, #0xffffffff00000000
	add	v23.4s, v3.4s, v21.4s
	mov	v21.s[1], v20.s[2]
	add	v20.4s, v18.4s, v24.4s
	bit	v19.16b, v21.16b, v0.16b
	eor	v3.16b, v20.16b, v2.16b
	uzp2	v2.4s, v22.4s, v19.4s
	zip1	v17.2d, v5.2d, v19.2d
	zip2	v18.4s, v19.4s, v5.4s
	ushr	v21.4s, v3.4s, #12
	shl	v3.4s, v3.4s, #20
	ext	v22.16b, v2.16b, v2.16b, #4
	bsl	v16.16b, v4.16b, v17.16b
	zip1	v17.4s, v18.4s, v4.4s
	zip1	v18.4s, v4.4s, v18.4s
	orr	v21.16b, v3.16b, v21.16b
	ext	v25.16b, v16.16b, v16.16b, #12
	ext	v3.16b, v18.16b, v17.16b, #8
	uzp1	v18.4s, v22.4s, v22.4s
	ext	v26.16b, v22.16b, v22.16b, #12
	add	v23.4s, v23.4s, v21.4s
	uzp1	v17.4s, v16.4s, v25.4s
	ext	v16.16b, v18.16b, v22.16b, #8
	ext	v18.16b, v22.16b, v26.16b, #12
	eor	v22.16b, v23.16b, v24.16b
	add	v6.4s, v23.4s, v6.4s
	ushr	v23.4s, v22.4s, #8
	shl	v22.4s, v22.4s, #24
	orr	v22.16b, v22.16b, v23.16b
	add	v20.4s, v22.4s, v20.4s
	eor	v21.16b, v20.16b, v21.16b
	ushr	v23.4s, v21.4s, #7
	shl	v21.4s, v21.4s, #25
	ext	v6.16b, v6.16b, v6.16b, #4
	orr	v21.16b, v21.16b, v23.16b
	ext	v22.16b, v22.16b, v22.16b, #8
	add	v6.4s, v21.4s, v6.4s
	eor	v22.16b, v6.16b, v22.16b
	ext	v20.16b, v20.16b, v20.16b, #12
	add	v6.4s, v6.4s, v19.4s
	rev32	v19.8h, v22.8h
	add	v20.4s, v20.4s, v19.4s
	eor	v21.16b, v20.16b, v21.16b
	ushr	v22.4s, v21.4s, #12
	shl	v21.4s, v21.4s, #20
	orr	v21.16b, v21.16b, v22.16b
	add	v6.4s, v6.4s, v21.4s
	eor	v19.16b, v6.16b, v19.16b
	ushr	v22.4s, v19.4s, #8
	shl	v19.4s, v19.4s, #24
	orr	v19.16b, v19.16b, v22.16b
	add	v20.4s, v19.4s, v20.4s
	eor	v21.16b, v20.16b, v21.16b
	ext	v6.16b, v6.16b, v6.16b, #12
	ushr	v22.4s, v21.4s, #7
	shl	v21.4s, v21.4s, #25
	add	v6.4s, v6.4s, v4.4s
	orr	v21.16b, v21.16b, v22.16b
	ext	v19.16b, v19.16b, v19.16b, #8
	add	v6.4s, v6.4s, v21.4s
	eor	v19.16b, v6.16b, v19.16b
	ext	v20.16b, v20.16b, v20.16b, #4
	rev32	v19.8h, v19.8h
	add	v20.4s, v20.4s, v19.4s
	add	v6.4s, v6.4s, v5.4s
	mov	v5.s[1], v4.s[2]
	eor	v4.16b, v20.16b, v21.16b
	ushr	v21.4s, v4.4s, #12
	shl	v4.4s, v4.4s, #20
	orr	v21.16b, v4.16b, v21.16b
	add	v6.4s, v6.4s, v21.4s
	eor	v19.16b, v6.16b, v19.16b
	add	v2.4s, v6.4s, v2.4s
	ushr	v6.4s, v19.4s, #8
	shl	v19.4s, v19.4s, #24
	orr	v6.16b, v19.16b, v6.16b
	add	v19.4s, v6.4s, v20.4s
	eor	v20.16b, v19.16b, v21.16b
	ushr	v21.4s, v20.4s, #7
	shl	v20.4s, v20.4s, #25
	ext	v2.16b, v2.16b, v2.16b, #4
	orr	v20.16b, v20.16b, v21.16b
	ext	v6.16b, v6.16b, v6.16b, #8
	add	v2.4s, v20.4s, v2.4s
	eor	v6.16b, v2.16b, v6.16b
	ext	v19.16b, v19.16b, v19.16b, #12
	rev32	v6.8h, v6.8h
	add	v19.4s, v19.4s, v6.4s
	mov	v22.16b, v0.16b
	eor	v20.16b, v19.16b, v20.16b
	bsl	v22.16b, v5.16b, v7.16b
	ushr	v21.4s, v20.4s, #12
	shl	v20.4s, v20.4s, #20
	add	v2.4s, v2.4s, v22.4s
	orr	v20.16b, v20.16b, v21.16b
	add	v2.4s, v2.4s, v20.4s
	eor	v6.16b, v2.16b, v6.16b
	ushr	v21.4s, v6.4s, #8
	shl	v6.4s, v6.4s, #24
	orr	v6.16b, v6.16b, v21.16b
	add	v19.4s, v6.4s, v19.4s
	eor	v20.16b, v19.16b, v20.16b
	ext	v2.16b, v2.16b, v2.16b, #12
	ushr	v21.4s, v20.4s, #7
	shl	v20.4s, v20.4s, #25
	add	v2.4s, v2.4s, v17.4s
	orr	v20.16b, v20.16b, v21.16b
	ext	v6.16b, v6.16b, v6.16b, #8
	add	v2.4s, v2.4s, v20.4s
	eor	v6.16b, v2.16b, v6.16b
	uzp2	v5.4s, v16.4s, v22.4s
	zip1	v7.2d, v3.2d, v22.2d
	zip2	v16.4s, v22.4s, v3.4s
	ext	v19.16b, v19.16b, v19.16b, #4
	rev32	v22.8h, v6.8h
	ext	v23.16b, v5.16b, v5.16b, #4
	bif	v7.16b, v17.16b, v1.16b
	zip1	v24.4s, v16.4s, v17.4s
	zip1	v16.4s, v17.4s, v16.4s
	add	v21.4s, v2.4s, v3.4s
	mov	v3.s[1], v17.s[2]
	add	v17.4s, v19.4s, v22.4s
	mov	v19.16b, v0.16b
	ext	v25.16b, v7.16b, v7.16b, #12
	ext	v4.16b, v16.16b, v24.16b, #8
	uzp1	v16.4s, v23.4s, v23.4s
	bsl	v19.16b, v3.16b, v18.16b
	eor	v2.16b, v17.16b, v20.16b
	uzp1	v7.4s, v7.4s, v25.4s
	ext	v25.16b, v16.16b, v23.16b, #8
	zip1	v3.2d, v4.2d, v19.2d
	ushr	v20.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	ext	v24.16b, v23.16b, v23.16b, #12
	uzp2	v6.4s, v25.4s, v19.4s
	zip2	v18.4s, v19.4s, v4.4s
	bif	v3.16b, v7.16b, v1.16b
	orr	v20.16b, v2.16b, v20.16b
	ext	v16.16b, v23.16b, v24.16b, #12
	ext	v23.16b, v6.16b, v6.16b, #4
	zip1	v24.4s, v18.4s, v7.4s
	zip1	v18.4s, v7.4s, v18.4s
	ext	v25.16b, v3.16b, v3.16b, #12
	add	v21.4s, v21.4s, v20.4s
	ext	v2.16b, v18.16b, v24.16b, #8
	uzp1	v18.4s, v23.4s, v23.4s
	ext	v24.16b, v23.16b, v23.16b, #12
	uzp1	v3.4s, v3.4s, v25.4s
	eor	v22.16b, v21.16b, v22.16b
	ext	v25.16b, v18.16b, v23.16b, #8
	dup	v18.4s, v2.s[3]
	ext	v23.16b, v23.16b, v24.16b, #12
	add	v5.4s, v21.4s, v5.4s
	trn1	v21.4s, v3.4s, v3.4s
	ushr	v24.4s, v22.4s, #8
	shl	v22.4s, v22.4s, #24
	ext	v18.16b, v21.16b, v18.16b, #8
	orr	v21.16b, v22.16b, v24.16b
	add	v17.4s, v21.4s, v17.4s
	eor	v20.16b, v17.16b, v20.16b
	ushr	v22.4s, v20.4s, #7
	shl	v20.4s, v20.4s, #25
	ext	v5.16b, v5.16b, v5.16b, #4
	orr	v20.16b, v20.16b, v22.16b
	ext	v21.16b, v21.16b, v21.16b, #8
	add	v5.4s, v20.4s, v5.4s
	eor	v21.16b, v5.16b, v21.16b
	ext	v17.16b, v17.16b, v17.16b, #12
	add	v5.4s, v5.4s, v19.4s
	rev32	v19.8h, v21.8h
	add	v17.4s, v17.4s, v19.4s
	eor	v20.16b, v17.16b, v20.16b
	ushr	v21.4s, v20.4s, #12
	shl	v20.4s, v20.4s, #20
	orr	v20.16b, v20.16b, v21.16b
	add	v5.4s, v5.4s, v20.4s
	eor	v19.16b, v5.16b, v19.16b
	ushr	v21.4s, v19.4s, #8
	shl	v19.4s, v19.4s, #24
	orr	v19.16b, v19.16b, v21.16b
	add	v17.4s, v19.4s, v17.4s
	eor	v20.16b, v17.16b, v20.16b
	ext	v5.16b, v5.16b, v5.16b, #12
	ushr	v21.4s, v20.4s, #7
	shl	v20.4s, v20.4s, #25
	add	v5.4s, v5.4s, v7.4s
	orr	v20.16b, v20.16b, v21.16b
	ext	v19.16b, v19.16b, v19.16b, #8
	add	v5.4s, v5.4s, v20.4s
	eor	v19.16b, v5.16b, v19.16b
	ext	v17.16b, v17.16b, v17.16b, #4
	rev32	v22.8h, v19.8h
	add	v21.4s, v5.4s, v4.4s
	mov	v4.s[1], v7.s[2]
	add	v19.4s, v17.4s, v22.4s
	bit	v16.16b, v4.16b, v0.16b
	eor	v5.16b, v19.16b, v20.16b
	uzp2	v4.4s, v25.4s, v16.4s
	zip1	v7.2d, v2.2d, v16.2d
	zip2	v17.4s, v16.4s, v2.4s
	ushr	v20.4s, v5.4s, #12
	shl	v5.4s, v5.4s, #20
	ext	v24.16b, v4.16b, v4.16b, #4
	bif	v7.16b, v3.16b, v1.16b
	zip1	v25.4s, v17.4s, v3.4s
	zip1	v17.4s, v3.4s, v17.4s
	orr	v20.16b, v5.16b, v20.16b
	ext	v26.16b, v7.16b, v7.16b, #12
	ext	v5.16b, v17.16b, v25.16b, #8
	uzp1	v17.4s, v24.4s, v24.4s
	ext	v25.16b, v24.16b, v24.16b, #12
	bit	v23.16b, v18.16b, v0.16b
	add	v21.4s, v21.4s, v20.4s
	uzp1	v7.4s, v7.4s, v26.4s
	ext	v26.16b, v17.16b, v24.16b, #8
	ext	v17.16b, v24.16b, v25.16b, #12
	eor	v22.16b, v21.16b, v22.16b
	add	v6.4s, v21.4s, v6.4s
	zip1	v21.2d, v5.2d, v23.2d
	zip2	v24.4s, v23.4s, v5.4s
	bif	v21.16b, v7.16b, v1.16b
	zip1	v1.4s, v24.4s, v7.4s
	zip1	v24.4s, v7.4s, v24.4s
	ext	v1.16b, v24.16b, v1.16b, #8
	ushr	v24.4s, v22.4s, #8
	shl	v22.4s, v22.4s, #24
	orr	v22.16b, v22.16b, v24.16b
	add	v19.4s, v22.4s, v19.4s
	ext	v24.16b, v21.16b, v21.16b, #12
	eor	v20.16b, v19.16b, v20.16b
	uzp1	v21.4s, v21.4s, v24.4s
	ushr	v24.4s, v20.4s, #7
	shl	v20.4s, v20.4s, #25
	orr	v20.16b, v20.16b, v24.16b
	ext	v6.16b, v6.16b, v6.16b, #4
	ext	v22.16b, v22.16b, v22.16b, #8
	add	v6.4s, v20.4s, v6.4s
	eor	v22.16b, v6.16b, v22.16b
	ext	v19.16b, v19.16b, v19.16b, #12
	add	v6.4s, v6.4s, v16.4s
	rev32	v16.8h, v22.8h
	add	v19.4s, v19.4s, v16.4s
	eor	v20.16b, v19.16b, v20.16b
	ushr	v22.4s, v20.4s, #12
	shl	v20.4s, v20.4s, #20
	orr	v20.16b, v20.16b, v22.16b
	add	v6.4s, v6.4s, v20.4s
	eor	v16.16b, v6.16b, v16.16b
	ext	v6.16b, v6.16b, v6.16b, #12
	add	v3.4s, v6.4s, v3.4s
	ushr	v6.4s, v16.4s, #8
	shl	v16.4s, v16.4s, #24
	orr	v6.16b, v16.16b, v6.16b
	add	v16.4s, v6.4s, v19.4s
	eor	v19.16b, v16.16b, v20.16b
	ushr	v20.4s, v19.4s, #7
	shl	v19.4s, v19.4s, #25
	orr	v19.16b, v19.16b, v20.16b
	ext	v6.16b, v6.16b, v6.16b, #8
	add	v3.4s, v3.4s, v19.4s
	eor	v6.16b, v3.16b, v6.16b
	ext	v16.16b, v16.16b, v16.16b, #4
	add	v2.4s, v3.4s, v2.4s
	rev32	v3.8h, v6.8h
	add	v6.4s, v16.4s, v3.4s
	eor	v16.16b, v6.16b, v19.16b
	ushr	v19.4s, v16.4s, #12
	shl	v16.4s, v16.4s, #20
	orr	v16.16b, v16.16b, v19.16b
	add	v2.4s, v2.4s, v16.4s
	eor	v3.16b, v2.16b, v3.16b
	add	v2.4s, v2.4s, v4.4s
	ushr	v4.4s, v3.4s, #8
	shl	v3.4s, v3.4s, #24
	orr	v3.16b, v3.16b, v4.16b
	add	v4.4s, v3.4s, v6.4s
	eor	v6.16b, v4.16b, v16.16b
	ushr	v16.4s, v6.4s, #7
	shl	v6.4s, v6.4s, #25
	ext	v2.16b, v2.16b, v2.16b, #4
	orr	v6.16b, v6.16b, v16.16b
	ext	v3.16b, v3.16b, v3.16b, #8
	add	v2.4s, v6.4s, v2.4s
	eor	v3.16b, v2.16b, v3.16b
	ext	v4.16b, v4.16b, v4.16b, #12
	rev32	v3.8h, v3.8h
	add	v4.4s, v4.4s, v3.4s
	eor	v6.16b, v4.16b, v6.16b
	ushr	v16.4s, v6.4s, #12
	shl	v6.4s, v6.4s, #20
	add	v2.4s, v2.4s, v23.4s
	orr	v6.16b, v6.16b, v16.16b
	add	v2.4s, v2.4s, v6.4s
	eor	v3.16b, v2.16b, v3.16b
	ushr	v16.4s, v3.4s, #8
	shl	v3.4s, v3.4s, #24
	orr	v3.16b, v3.16b, v16.16b
	add	v4.4s, v3.4s, v4.4s
	eor	v6.16b, v4.16b, v6.16b
	ext	v2.16b, v2.16b, v2.16b, #12
	ushr	v16.4s, v6.4s, #7
	shl	v6.4s, v6.4s, #25
	add	v2.4s, v2.4s, v7.4s
	orr	v6.16b, v6.16b, v16.16b
	ext	v3.16b, v3.16b, v3.16b, #8
	add	v2.4s, v2.4s, v6.4s
	eor	v3.16b, v2.16b, v3.16b
	ext	v4.16b, v4.16b, v4.16b, #4
	rev32	v3.8h, v3.8h
	add	v2.4s, v2.4s, v5.4s
	mov	v5.s[1], v7.s[2]
	add	v4.4s, v4.4s, v3.4s
	bsl	v0.16b, v5.16b, v17.16b
	eor	v5.16b, v4.16b, v6.16b
	ushr	v6.4s, v5.4s, #12
	shl	v5.4s, v5.4s, #20
	orr	v5.16b, v5.16b, v6.16b
	add	v2.4s, v2.4s, v5.4s
	eor	v3.16b, v2.16b, v3.16b
	ushr	v6.4s, v3.4s, #8
	shl	v3.4s, v3.4s, #24
	orr	v3.16b, v3.16b, v6.16b
	add	v4.4s, v3.4s, v4.4s
	uzp2	v18.4s, v26.4s, v18.4s
	eor	v5.16b, v4.16b, v5.16b
	add	v2.4s, v2.4s, v18.4s
	ushr	v6.4s, v5.4s, #7
	shl	v5.4s, v5.4s, #25
	ext	v2.16b, v2.16b, v2.16b, #4
	orr	v5.16b, v5.16b, v6.16b
	ext	v3.16b, v3.16b, v3.16b, #8
	add	v2.4s, v5.4s, v2.4s
	eor	v3.16b, v2.16b, v3.16b
	ext	v4.16b, v4.16b, v4.16b, #12
	add	v0.4s, v2.4s, v0.4s
	rev32	v2.8h, v3.8h
	add	v3.4s, v4.4s, v2.4s
	eor	v4.16b, v3.16b, v5.16b
	ushr	v5.4s, v4.4s, #12
	shl	v4.4s, v4.4s, #20
	orr	v4.16b, v4.16b, v5.16b
	add	v0.4s, v0.4s, v4.4s
	eor	v2.16b, v0.16b, v2.16b
	ushr	v5.4s, v2.4s, #8
	shl	v2.4s, v2.4s, #24
	orr	v2.16b, v2.16b, v5.16b
	add	v3.4s, v2.4s, v3.4s
	eor	v4.16b, v3.16b, v4.16b
	ext	v0.16b, v0.16b, v0.16b, #12
	ushr	v5.4s, v4.4s, #7
	shl	v4.4s, v4.4s, #25
	add	v0.4s, v0.4s, v21.4s
	orr	v4.16b, v4.16b, v5.16b
	ext	v2.16b, v2.16b, v2.16b, #8
	add	v0.4s, v0.4s, v4.4s
	eor	v2.16b, v0.16b, v2.16b
	ext	v3.16b, v3.16b, v3.16b, #4
	add	v0.4s, v0.4s, v1.4s
	rev32	v1.8h, v2.8h
	add	v2.4s, v3.4s, v1.4s
	eor	v3.16b, v2.16b, v4.16b
	ushr	v4.4s, v3.4s, #12
	shl	v3.4s, v3.4s, #20
	orr	v3.16b, v3.16b, v4.16b
	add	v0.4s, v0.4s, v3.4s
	eor	v1.16b, v0.16b, v1.16b
	ushr	v4.4s, v1.4s, #8
	shl	v1.4s, v1.4s, #24
	orr	v1.16b, v1.16b, v4.16b
	add	v2.4s, v1.4s, v2.4s
	eor	v3.16b, v2.16b, v3.16b
	ushr	v4.4s, v3.4s, #7
	shl	v3.4s, v3.4s, #25
	ext	v0.16b, v0.16b, v0.16b, #4
	ext	v1.16b, v1.16b, v1.16b, #8
	ext	v2.16b, v2.16b, v2.16b, #12
	orr	v3.16b, v3.16b, v4.16b
	eor	v0.16b, v2.16b, v0.16b
	eor	v3.16b, v3.16b, v1.16b
	stp	q0, q3, [x5]
	ldr	q0, [x0]
	eor	v0.16b, v0.16b, v2.16b
	str	q0, [x5, #32]
	ldr	q0, [x0, #16]
	eor	v0.16b, v0.16b, v1.16b
	str	q0, [x5, #48]
	ret
.Lfunc_end1:
	.size	zfs_blake3_compress_xof_sse2, .Lfunc_end1-zfs_blake3_compress_xof_sse2
	.cfi_endproc

	.section	.rodata.cst16,"aM",@progbits,16
	.p2align	4
.LCPI2_0:
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
	sub	sp, sp, #384
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
	adrp	x9, .LCPI2_0
	ldr	q0, [x9, :lo12:.LCPI2_0]
	sbfx	w11, w5, #0, #1
	dup	v1.4s, w11
	mov	w9, #58983
	mov	w10, #44677
	and	v0.16b, v1.16b, v0.16b
	mov	w11, #62322
	mov	w12, #62778
	orr	w8, w7, w19
	movk	w9, #27145, lsl #16
	movk	w10, #47975, lsl #16
	movk	w11, #15470, lsl #16
	str	q0, [sp, #16]
	orr	v0.4s, #128, lsl #24
	movk	w12, #42319, lsl #16
	str	q0, [sp]
.LBB2_2:
	ldr	x0, [sp, #40]
	mov	x13, x0
	ld1r	{ v20.4s }, [x13], #4
	add	x14, x0, #8
	add	x15, x0, #12
	add	x16, x0, #16
	add	x17, x0, #20
	add	x18, x0, #24
	add	x0, x0, #28
	ld1r	{ v17.4s }, [x14]
	ld1r	{ v6.4s }, [x15]
	ld1r	{ v8.4s }, [x16]
	ld1r	{ v9.4s }, [x17]
	ld1r	{ v31.4s }, [x18]
	ld1r	{ v26.4s }, [x13]
	ld1r	{ v15.4s }, [x0]
	cbz	x22, .LBB2_7
	ldr	q1, [sp, #16]
	dup	v0.4s, w20
	ldp	x13, x14, [x24]
	ldp	x15, x16, [x24, #16]
	add	v1.4s, v0.4s, v1.4s
	movi	v0.4s, #128, lsl #24
	str	q1, [sp, #64]
	eor	v0.16b, v1.16b, v0.16b
	ldr	q1, [sp]
	lsr	x18, x20, #32
	mov	x17, xzr
	cmgt	v0.4s, v1.4s, v0.4s
	dup	v1.4s, w18
	sub	v0.4s, v1.4s, v0.4s
	mov	w18, w8
	str	q0, [sp, #48]
.LBB2_4:
	mov	w2, #16
	bfi	x2, x17, #6, #58
	ldr	q1, [x13, x2]
	ldr	q3, [x14, x2]
	ldr	q2, [x15, x2]
	ldr	q4, [x16, x2]
	mov	w2, #32
	bfi	x2, x17, #6, #58
	ldr	q5, [x13, x2]
	ldr	q18, [x14, x2]
	ldr	q19, [x15, x2]
	ldr	q23, [x16, x2]
	mov	w2, #48
	lsl	x3, x17, #6
	bfi	x2, x17, #6, #58
	add	x17, x17, #1
	ldr	q0, [x13, x3]
	ldr	q21, [x14, x3]
	ldr	q7, [x15, x3]
	ldr	q16, [x16, x3]
	cmp	x17, x22
	ldr	q13, [x13, x2]
	ldr	q14, [x14, x2]
	ldr	q29, [x15, x2]
	ldr	q10, [x16, x2]
	csel	w2, w27, wzr, eq
	orr	w18, w2, w18
	mov	x0, xzr
	and	w18, w18, #0xff
	add	x3, x3, #256
.LBB2_5:
	ldr	x2, [x24, x0]
	add	x0, x0, #8
	cmp	x0, #32
	add	x2, x2, x3
	prfm	pldl1keep, [x2]
	b.ne	.LBB2_5
	dup	v22.4s, w18
	str	q22, [sp, #192]
	zip1	v27.4s, v0.4s, v21.4s
	zip2	v21.4s, v0.4s, v21.4s
	zip1	v0.4s, v7.4s, v16.4s
	zip2	v22.4s, v7.4s, v16.4s
	zip1	v7.4s, v1.4s, v3.4s
	zip1	v25.4s, v2.4s, v4.4s
	zip2	v16.4s, v2.4s, v4.4s
	zip1	v11.4s, v19.4s, v23.4s
	zip2	v12.4s, v19.4s, v23.4s
	zip1	v19.4s, v13.4s, v14.4s
	zip2	v23.4s, v13.4s, v14.4s
	zip1	v13.4s, v29.4s, v10.4s
	zip2	v14.4s, v29.4s, v10.4s
	add	v10.4s, v20.4s, v8.4s
	add	v2.4s, v26.4s, v9.4s
	ext	v20.16b, v22.16b, v21.16b, #8
	ext	v26.16b, v25.16b, v7.16b, #8
	zip2	v24.4s, v1.4s, v3.4s
	add	v1.4s, v6.4s, v15.4s
	ext	v6.16b, v0.16b, v27.16b, #8
	ext	v20.16b, v21.16b, v20.16b, #8
	mov	v21.d[1], v22.d[0]
	ext	v22.16b, v7.16b, v26.16b, #8
	mov	v7.d[1], v25.d[0]
	add	v3.4s, v17.4s, v31.4s
	str	q1, [sp, #144]
	ext	v1.16b, v27.16b, v6.16b, #8
	mov	v6.16b, v7.16b
	zip1	v28.4s, v5.4s, v18.4s
	stur	q1, [x29, #-80]
	mov	v1.16b, v27.16b
	mov	v27.16b, v24.16b
	add	v3.4s, v3.4s, v6.4s
	ldr	q6, [sp, #64]
	ext	v29.16b, v16.16b, v24.16b, #8
	mov	v1.d[1], v0.d[0]
	ext	v0.16b, v11.16b, v28.16b, #8
	mov	v27.d[1], v16.d[0]
	ext	v16.16b, v14.16b, v23.16b, #8
	stur	q7, [x29, #-144]
	ext	v7.16b, v24.16b, v29.16b, #8
	ext	v29.16b, v28.16b, v0.16b, #8
	ext	v0.16b, v23.16b, v16.16b, #8
	mov	v23.d[1], v14.d[0]
	stp	q0, q23, [sp, #80]
	add	v0.4s, v10.4s, v1.4s
	eor	v16.16b, v0.16b, v6.16b
	ldr	q6, [sp, #48]
	add	v2.4s, v2.4s, v21.4s
	mov	v28.d[1], v11.d[0]
	zip2	v18.4s, v5.4s, v18.4s
	eor	v10.16b, v2.16b, v6.16b
	movi	v6.4s, #64
	eor	v11.16b, v3.16b, v6.16b
	ldr	q6, [sp, #144]
	dup	v17.4s, w9
	ext	v30.16b, v12.16b, v18.16b, #8
	rev32	v16.8h, v16.8h
	dup	v5.4s, w10
	ext	v25.16b, v18.16b, v30.16b, #8
	mov	v30.16b, v23.16b
	mov	v23.16b, v1.16b
	str	q1, [sp, #160]
	rev32	v10.8h, v10.8h
	add	v1.4s, v16.4s, v17.4s
	add	v17.4s, v6.4s, v27.4s
	ldr	q6, [sp, #192]
	dup	v4.4s, w11
	rev32	v11.8h, v11.8h
	add	v5.4s, v10.4s, v5.4s
	eor	v8.16b, v1.16b, v8.16b
	stur	q21, [x29, #-128]
	mov	v18.d[1], v12.d[0]
	add	v4.4s, v11.4s, v4.4s
	eor	v9.16b, v5.16b, v9.16b
	ushr	v12.4s, v8.4s, #12
	shl	v8.4s, v8.4s, #20
	ldur	q21, [x29, #-80]
	ext	v26.16b, v13.16b, v19.16b, #8
	eor	v31.16b, v4.16b, v31.16b
	orr	v8.16b, v8.16b, v12.16b
	ushr	v12.4s, v9.4s, #12
	shl	v9.4s, v9.4s, #20
	ext	v26.16b, v19.16b, v26.16b, #8
	mov	v19.d[1], v13.d[0]
	orr	v9.16b, v9.16b, v12.16b
	ushr	v12.4s, v31.4s, #12
	shl	v31.4s, v31.4s, #20
	eor	v13.16b, v17.16b, v6.16b
	orr	v31.16b, v31.16b, v12.16b
	dup	v12.4s, w12
	rev32	v13.8h, v13.8h
	add	v12.4s, v13.4s, v12.4s
	add	v0.4s, v0.4s, v21.4s
	eor	v14.16b, v12.16b, v15.16b
	add	v0.4s, v0.4s, v8.4s
	add	v2.4s, v2.4s, v20.4s
	ushr	v15.4s, v14.4s, #12
	shl	v14.4s, v14.4s, #20
	eor	v16.16b, v0.16b, v16.16b
	add	v2.4s, v2.4s, v9.4s
	add	v3.4s, v3.4s, v22.4s
	orr	v14.16b, v14.16b, v15.16b
	ushr	v15.4s, v16.4s, #8
	shl	v16.4s, v16.4s, #24
	eor	v10.16b, v2.16b, v10.16b
	add	v3.4s, v3.4s, v31.4s
	add	v17.4s, v17.4s, v7.4s
	orr	v16.16b, v16.16b, v15.16b
	ushr	v15.4s, v10.4s, #8
	shl	v10.4s, v10.4s, #24
	eor	v11.16b, v3.16b, v11.16b
	add	v17.4s, v17.4s, v14.4s
	orr	v10.16b, v10.16b, v15.16b
	ushr	v15.4s, v11.4s, #8
	shl	v11.4s, v11.4s, #24
	eor	v13.16b, v17.16b, v13.16b
	add	v1.4s, v16.4s, v1.4s
	orr	v11.16b, v11.16b, v15.16b
	ushr	v15.4s, v13.4s, #8
	shl	v13.4s, v13.4s, #24
	eor	v8.16b, v1.16b, v8.16b
	add	v5.4s, v10.4s, v5.4s
	orr	v13.16b, v13.16b, v15.16b
	ushr	v15.4s, v8.4s, #7
	shl	v8.4s, v8.4s, #25
	eor	v9.16b, v5.16b, v9.16b
	add	v4.4s, v11.4s, v4.4s
	orr	v8.16b, v8.16b, v15.16b
	ushr	v15.4s, v9.4s, #7
	shl	v9.4s, v9.4s, #25
	eor	v31.16b, v4.16b, v31.16b
	add	v12.4s, v13.4s, v12.4s
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v31.4s, #7
	shl	v31.4s, v31.4s, #25
	eor	v14.16b, v12.16b, v14.16b
	add	v0.4s, v0.4s, v28.4s
	orr	v31.16b, v31.16b, v15.16b
	ushr	v15.4s, v14.4s, #7
	shl	v14.4s, v14.4s, #25
	add	v0.4s, v0.4s, v9.4s
	add	v2.4s, v2.4s, v18.4s
	orr	v14.16b, v14.16b, v15.16b
	eor	v13.16b, v0.16b, v13.16b
	add	v2.4s, v2.4s, v31.4s
	add	v3.4s, v3.4s, v19.4s
	rev32	v13.8h, v13.8h
	eor	v16.16b, v2.16b, v16.16b
	add	v3.4s, v3.4s, v14.4s
	add	v17.4s, v17.4s, v30.4s
	add	v4.4s, v4.4s, v13.4s
	rev32	v16.8h, v16.8h
	eor	v10.16b, v3.16b, v10.16b
	add	v17.4s, v17.4s, v8.4s
	eor	v9.16b, v4.16b, v9.16b
	add	v12.4s, v12.4s, v16.4s
	rev32	v10.8h, v10.8h
	eor	v11.16b, v17.16b, v11.16b
	mov	v24.16b, v7.16b
	stur	q7, [x29, #-112]
	ushr	v15.4s, v9.4s, #12
	shl	v9.4s, v9.4s, #20
	eor	v31.16b, v12.16b, v31.16b
	add	v1.4s, v1.4s, v10.4s
	rev32	v11.8h, v11.8h
	mov	v7.16b, v26.16b
	add	v3.4s, v3.4s, v26.4s
	ldr	q26, [sp, #80]
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v31.4s, #12
	shl	v31.4s, v31.4s, #20
	eor	v14.16b, v1.16b, v14.16b
	add	v5.4s, v5.4s, v11.4s
	add	v0.4s, v0.4s, v29.4s
	orr	v31.16b, v31.16b, v15.16b
	ushr	v15.4s, v14.4s, #12
	shl	v14.4s, v14.4s, #20
	eor	v8.16b, v5.16b, v8.16b
	add	v0.4s, v0.4s, v9.4s
	add	v2.4s, v2.4s, v25.4s
	orr	v14.16b, v14.16b, v15.16b
	ushr	v15.4s, v8.4s, #12
	shl	v8.4s, v8.4s, #20
	eor	v13.16b, v0.16b, v13.16b
	add	v2.4s, v2.4s, v31.4s
	orr	v8.16b, v8.16b, v15.16b
	ushr	v15.4s, v13.4s, #8
	shl	v13.4s, v13.4s, #24
	eor	v16.16b, v2.16b, v16.16b
	add	v3.4s, v3.4s, v14.4s
	add	v17.4s, v17.4s, v26.4s
	orr	v13.16b, v13.16b, v15.16b
	ushr	v15.4s, v16.4s, #8
	shl	v16.4s, v16.4s, #24
	eor	v10.16b, v3.16b, v10.16b
	add	v17.4s, v17.4s, v8.4s
	orr	v16.16b, v16.16b, v15.16b
	ushr	v15.4s, v10.4s, #8
	shl	v10.4s, v10.4s, #24
	eor	v11.16b, v17.16b, v11.16b
	add	v4.4s, v13.4s, v4.4s
	orr	v10.16b, v10.16b, v15.16b
	ushr	v15.4s, v11.4s, #8
	shl	v11.4s, v11.4s, #24
	eor	v9.16b, v4.16b, v9.16b
	add	v12.4s, v16.4s, v12.4s
	str	q22, [sp, #128]
	orr	v11.16b, v11.16b, v15.16b
	ushr	v15.4s, v9.4s, #7
	shl	v9.4s, v9.4s, #25
	eor	v31.16b, v12.16b, v31.16b
	add	v1.4s, v10.4s, v1.4s
	ldur	q22, [x29, #-128]
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v31.4s, #7
	shl	v31.4s, v31.4s, #25
	eor	v14.16b, v1.16b, v14.16b
	add	v5.4s, v11.4s, v5.4s
	orr	v31.16b, v31.16b, v15.16b
	ushr	v15.4s, v14.4s, #7
	shl	v14.4s, v14.4s, #25
	eor	v8.16b, v5.16b, v8.16b
	mov	v6.16b, v18.16b
	orr	v14.16b, v14.16b, v15.16b
	ushr	v15.4s, v8.4s, #7
	shl	v8.4s, v8.4s, #25
	ldur	q18, [x29, #-144]
	orr	v8.16b, v8.16b, v15.16b
	add	v0.4s, v0.4s, v22.4s
	add	v0.4s, v0.4s, v8.4s
	add	v2.4s, v2.4s, v20.4s
	eor	v16.16b, v0.16b, v16.16b
	add	v2.4s, v2.4s, v9.4s
	add	v3.4s, v3.4s, v24.4s
	rev32	v16.8h, v16.8h
	eor	v10.16b, v2.16b, v10.16b
	add	v3.4s, v3.4s, v31.4s
	add	v17.4s, v17.4s, v18.4s
	add	v1.4s, v1.4s, v16.4s
	rev32	v10.8h, v10.8h
	eor	v11.16b, v3.16b, v11.16b
	add	v17.4s, v17.4s, v14.4s
	eor	v8.16b, v1.16b, v8.16b
	add	v5.4s, v5.4s, v10.4s
	rev32	v11.8h, v11.8h
	eor	v13.16b, v17.16b, v13.16b
	ushr	v15.4s, v8.4s, #12
	shl	v8.4s, v8.4s, #20
	eor	v9.16b, v5.16b, v9.16b
	add	v4.4s, v4.4s, v11.4s
	rev32	v13.8h, v13.8h
	orr	v8.16b, v8.16b, v15.16b
	ushr	v15.4s, v9.4s, #12
	shl	v9.4s, v9.4s, #20
	eor	v31.16b, v4.16b, v31.16b
	add	v12.4s, v12.4s, v13.4s
	add	v0.4s, v0.4s, v27.4s
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v31.4s, #12
	shl	v31.4s, v31.4s, #20
	eor	v14.16b, v12.16b, v14.16b
	add	v0.4s, v0.4s, v8.4s
	add	v2.4s, v2.4s, v6.4s
	orr	v31.16b, v31.16b, v15.16b
	ushr	v15.4s, v14.4s, #12
	shl	v14.4s, v14.4s, #20
	eor	v16.16b, v0.16b, v16.16b
	add	v2.4s, v2.4s, v9.4s
	add	v3.4s, v3.4s, v23.4s
	orr	v14.16b, v14.16b, v15.16b
	ushr	v15.4s, v16.4s, #8
	shl	v16.4s, v16.4s, #24
	eor	v10.16b, v2.16b, v10.16b
	add	v3.4s, v3.4s, v31.4s
	add	v17.4s, v17.4s, v7.4s
	orr	v16.16b, v16.16b, v15.16b
	ushr	v15.4s, v10.4s, #8
	shl	v10.4s, v10.4s, #24
	eor	v11.16b, v3.16b, v11.16b
	add	v17.4s, v17.4s, v14.4s
	orr	v10.16b, v10.16b, v15.16b
	ushr	v15.4s, v11.4s, #8
	shl	v11.4s, v11.4s, #24
	eor	v13.16b, v17.16b, v13.16b
	add	v1.4s, v16.4s, v1.4s
	orr	v11.16b, v11.16b, v15.16b
	ushr	v15.4s, v13.4s, #8
	shl	v13.4s, v13.4s, #24
	eor	v8.16b, v1.16b, v8.16b
	add	v5.4s, v10.4s, v5.4s
	orr	v13.16b, v13.16b, v15.16b
	ushr	v15.4s, v8.4s, #7
	shl	v8.4s, v8.4s, #25
	eor	v9.16b, v5.16b, v9.16b
	add	v4.4s, v11.4s, v4.4s
	orr	v8.16b, v8.16b, v15.16b
	ushr	v15.4s, v9.4s, #7
	shl	v9.4s, v9.4s, #25
	eor	v31.16b, v4.16b, v31.16b
	add	v12.4s, v13.4s, v12.4s
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v31.4s, #7
	shl	v31.4s, v31.4s, #25
	eor	v14.16b, v12.16b, v14.16b
	add	v0.4s, v0.4s, v21.4s
	orr	v31.16b, v31.16b, v15.16b
	ushr	v15.4s, v14.4s, #7
	shl	v14.4s, v14.4s, #25
	add	v0.4s, v0.4s, v9.4s
	add	v2.4s, v2.4s, v19.4s
	orr	v14.16b, v14.16b, v15.16b
	eor	v13.16b, v0.16b, v13.16b
	add	v2.4s, v2.4s, v31.4s
	add	v3.4s, v3.4s, v29.4s
	str	q28, [sp, #112]
	rev32	v13.8h, v13.8h
	eor	v16.16b, v2.16b, v16.16b
	add	v3.4s, v3.4s, v14.4s
	add	v17.4s, v17.4s, v26.4s
	add	v4.4s, v4.4s, v13.4s
	rev32	v16.8h, v16.8h
	eor	v10.16b, v3.16b, v10.16b
	add	v17.4s, v17.4s, v8.4s
	ldp	q28, q23, [sp, #112]
	eor	v9.16b, v4.16b, v9.16b
	add	v12.4s, v12.4s, v16.4s
	rev32	v10.8h, v10.8h
	eor	v11.16b, v17.16b, v11.16b
	ldr	q21, [sp, #96]
	ushr	v15.4s, v9.4s, #12
	shl	v9.4s, v9.4s, #20
	eor	v31.16b, v12.16b, v31.16b
	add	v1.4s, v1.4s, v10.4s
	rev32	v11.8h, v11.8h
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v31.4s, #12
	shl	v31.4s, v31.4s, #20
	eor	v14.16b, v1.16b, v14.16b
	add	v5.4s, v5.4s, v11.4s
	add	v0.4s, v0.4s, v25.4s
	orr	v31.16b, v31.16b, v15.16b
	ushr	v15.4s, v14.4s, #12
	shl	v14.4s, v14.4s, #20
	eor	v8.16b, v5.16b, v8.16b
	add	v0.4s, v0.4s, v9.4s
	add	v2.4s, v2.4s, v23.4s
	orr	v14.16b, v14.16b, v15.16b
	ushr	v15.4s, v8.4s, #12
	shl	v8.4s, v8.4s, #20
	eor	v13.16b, v0.16b, v13.16b
	add	v2.4s, v2.4s, v31.4s
	add	v3.4s, v3.4s, v21.4s
	orr	v8.16b, v8.16b, v15.16b
	ushr	v15.4s, v13.4s, #8
	shl	v13.4s, v13.4s, #24
	eor	v16.16b, v2.16b, v16.16b
	add	v3.4s, v3.4s, v14.4s
	add	v17.4s, v17.4s, v28.4s
	orr	v13.16b, v13.16b, v15.16b
	ushr	v15.4s, v16.4s, #8
	shl	v16.4s, v16.4s, #24
	eor	v10.16b, v3.16b, v10.16b
	add	v17.4s, v17.4s, v8.4s
	orr	v16.16b, v16.16b, v15.16b
	ushr	v15.4s, v10.4s, #8
	shl	v10.4s, v10.4s, #24
	eor	v11.16b, v17.16b, v11.16b
	add	v4.4s, v13.4s, v4.4s
	orr	v10.16b, v10.16b, v15.16b
	ushr	v15.4s, v11.4s, #8
	shl	v11.4s, v11.4s, #24
	eor	v9.16b, v4.16b, v9.16b
	add	v12.4s, v16.4s, v12.4s
	orr	v11.16b, v11.16b, v15.16b
	ushr	v15.4s, v9.4s, #7
	shl	v9.4s, v9.4s, #25
	eor	v31.16b, v12.16b, v31.16b
	add	v1.4s, v10.4s, v1.4s
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v31.4s, #7
	shl	v31.4s, v31.4s, #25
	eor	v14.16b, v1.16b, v14.16b
	add	v5.4s, v11.4s, v5.4s
	orr	v31.16b, v31.16b, v15.16b
	ushr	v15.4s, v14.4s, #7
	shl	v14.4s, v14.4s, #25
	eor	v8.16b, v5.16b, v8.16b
	mov	v30.16b, v29.16b
	mov	v29.16b, v25.16b
	orr	v14.16b, v14.16b, v15.16b
	ushr	v15.4s, v8.4s, #7
	shl	v8.4s, v8.4s, #25
	ldur	q25, [x29, #-112]
	orr	v8.16b, v8.16b, v15.16b
	add	v0.4s, v0.4s, v20.4s
	add	v0.4s, v0.4s, v8.4s
	add	v2.4s, v2.4s, v6.4s
	eor	v16.16b, v0.16b, v16.16b
	add	v2.4s, v2.4s, v9.4s
	add	v3.4s, v3.4s, v7.4s
	rev32	v16.8h, v16.8h
	eor	v10.16b, v2.16b, v10.16b
	add	v3.4s, v3.4s, v31.4s
	add	v17.4s, v17.4s, v25.4s
	add	v1.4s, v1.4s, v16.4s
	rev32	v10.8h, v10.8h
	eor	v11.16b, v3.16b, v11.16b
	add	v17.4s, v17.4s, v14.4s
	eor	v8.16b, v1.16b, v8.16b
	add	v5.4s, v5.4s, v10.4s
	rev32	v11.8h, v11.8h
	eor	v13.16b, v17.16b, v13.16b
	ushr	v15.4s, v8.4s, #12
	shl	v8.4s, v8.4s, #20
	eor	v9.16b, v5.16b, v9.16b
	add	v4.4s, v4.4s, v11.4s
	rev32	v13.8h, v13.8h
	orr	v8.16b, v8.16b, v15.16b
	ushr	v15.4s, v9.4s, #12
	shl	v9.4s, v9.4s, #20
	eor	v31.16b, v4.16b, v31.16b
	add	v12.4s, v12.4s, v13.4s
	add	v0.4s, v0.4s, v18.4s
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v31.4s, #12
	shl	v31.4s, v31.4s, #20
	eor	v14.16b, v12.16b, v14.16b
	add	v0.4s, v0.4s, v8.4s
	add	v2.4s, v2.4s, v19.4s
	orr	v31.16b, v31.16b, v15.16b
	ushr	v15.4s, v14.4s, #12
	shl	v14.4s, v14.4s, #20
	eor	v16.16b, v0.16b, v16.16b
	add	v2.4s, v2.4s, v9.4s
	add	v3.4s, v3.4s, v22.4s
	orr	v14.16b, v14.16b, v15.16b
	ushr	v15.4s, v16.4s, #8
	shl	v16.4s, v16.4s, #24
	eor	v10.16b, v2.16b, v10.16b
	add	v3.4s, v3.4s, v31.4s
	add	v17.4s, v17.4s, v21.4s
	orr	v16.16b, v16.16b, v15.16b
	ushr	v15.4s, v10.4s, #8
	shl	v10.4s, v10.4s, #24
	eor	v11.16b, v3.16b, v11.16b
	add	v17.4s, v17.4s, v14.4s
	orr	v10.16b, v10.16b, v15.16b
	ushr	v15.4s, v11.4s, #8
	shl	v11.4s, v11.4s, #24
	eor	v13.16b, v17.16b, v13.16b
	add	v1.4s, v16.4s, v1.4s
	orr	v11.16b, v11.16b, v15.16b
	ushr	v15.4s, v13.4s, #8
	shl	v13.4s, v13.4s, #24
	eor	v8.16b, v1.16b, v8.16b
	add	v5.4s, v10.4s, v5.4s
	orr	v13.16b, v13.16b, v15.16b
	ushr	v15.4s, v8.4s, #7
	shl	v8.4s, v8.4s, #25
	eor	v9.16b, v5.16b, v9.16b
	add	v4.4s, v11.4s, v4.4s
	orr	v8.16b, v8.16b, v15.16b
	ushr	v15.4s, v9.4s, #7
	shl	v9.4s, v9.4s, #25
	eor	v31.16b, v4.16b, v31.16b
	add	v12.4s, v13.4s, v12.4s
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v31.4s, #7
	shl	v31.4s, v31.4s, #25
	eor	v14.16b, v12.16b, v14.16b
	add	v0.4s, v0.4s, v27.4s
	orr	v31.16b, v31.16b, v15.16b
	ushr	v15.4s, v14.4s, #7
	shl	v14.4s, v14.4s, #25
	add	v0.4s, v0.4s, v9.4s
	add	v2.4s, v2.4s, v30.4s
	orr	v14.16b, v14.16b, v15.16b
	eor	v13.16b, v0.16b, v13.16b
	add	v2.4s, v2.4s, v31.4s
	add	v3.4s, v3.4s, v29.4s
	rev32	v13.8h, v13.8h
	eor	v16.16b, v2.16b, v16.16b
	add	v3.4s, v3.4s, v14.4s
	add	v17.4s, v17.4s, v28.4s
	add	v4.4s, v4.4s, v13.4s
	rev32	v16.8h, v16.8h
	eor	v10.16b, v3.16b, v10.16b
	add	v17.4s, v17.4s, v8.4s
	eor	v9.16b, v4.16b, v9.16b
	add	v12.4s, v12.4s, v16.4s
	rev32	v10.8h, v10.8h
	eor	v11.16b, v17.16b, v11.16b
	ushr	v15.4s, v9.4s, #12
	shl	v9.4s, v9.4s, #20
	eor	v31.16b, v12.16b, v31.16b
	add	v1.4s, v1.4s, v10.4s
	rev32	v11.8h, v11.8h
	ldr	q24, [sp, #160]
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v31.4s, #12
	shl	v31.4s, v31.4s, #20
	eor	v14.16b, v1.16b, v14.16b
	add	v5.4s, v5.4s, v11.4s
	stur	q7, [x29, #-64]
	orr	v31.16b, v31.16b, v15.16b
	ushr	v15.4s, v14.4s, #12
	shl	v14.4s, v14.4s, #20
	eor	v8.16b, v5.16b, v8.16b
	mov	v7.16b, v26.16b
	add	v3.4s, v3.4s, v26.4s
	ldur	q26, [x29, #-80]
	orr	v14.16b, v14.16b, v15.16b
	ushr	v15.4s, v8.4s, #12
	shl	v8.4s, v8.4s, #20
	add	v0.4s, v0.4s, v23.4s
	orr	v8.16b, v8.16b, v15.16b
	add	v15.4s, v0.4s, v9.4s
	add	v2.4s, v2.4s, v24.4s
	eor	v0.16b, v15.16b, v13.16b
	add	v2.4s, v2.4s, v31.4s
	ushr	v13.4s, v0.4s, #8
	shl	v0.4s, v0.4s, #24
	eor	v16.16b, v2.16b, v16.16b
	add	v3.4s, v3.4s, v14.4s
	add	v17.4s, v17.4s, v26.4s
	orr	v0.16b, v0.16b, v13.16b
	ushr	v13.4s, v16.4s, #8
	shl	v16.4s, v16.4s, #24
	eor	v10.16b, v3.16b, v10.16b
	add	v17.4s, v17.4s, v8.4s
	orr	v16.16b, v16.16b, v13.16b
	ushr	v13.4s, v10.4s, #8
	shl	v10.4s, v10.4s, #24
	eor	v11.16b, v17.16b, v11.16b
	add	v4.4s, v0.4s, v4.4s
	orr	v10.16b, v10.16b, v13.16b
	ushr	v13.4s, v11.4s, #8
	shl	v11.4s, v11.4s, #24
	eor	v9.16b, v4.16b, v9.16b
	add	v12.4s, v16.4s, v12.4s
	orr	v11.16b, v11.16b, v13.16b
	ushr	v13.4s, v9.4s, #7
	shl	v9.4s, v9.4s, #25
	eor	v31.16b, v12.16b, v31.16b
	orr	v9.16b, v9.16b, v13.16b
	ushr	v13.4s, v31.4s, #7
	shl	v31.4s, v31.4s, #25
	add	v1.4s, v10.4s, v1.4s
	orr	v31.16b, v31.16b, v13.16b
	eor	v13.16b, v1.16b, v14.16b
	add	v5.4s, v11.4s, v5.4s
	ushr	v14.4s, v13.4s, #7
	shl	v13.4s, v13.4s, #25
	eor	v8.16b, v5.16b, v8.16b
	orr	v13.16b, v13.16b, v14.16b
	ushr	v14.4s, v8.4s, #7
	shl	v8.4s, v8.4s, #25
	stur	q6, [x29, #-96]
	orr	v8.16b, v8.16b, v14.16b
	add	v14.4s, v15.4s, v6.4s
	ldur	q6, [x29, #-64]
	mov	v18.16b, v19.16b
	add	v14.4s, v14.4s, v8.4s
	add	v2.4s, v2.4s, v18.4s
	eor	v16.16b, v14.16b, v16.16b
	add	v2.4s, v2.4s, v9.4s
	add	v3.4s, v3.4s, v21.4s
	rev32	v16.8h, v16.8h
	eor	v10.16b, v2.16b, v10.16b
	add	v3.4s, v3.4s, v31.4s
	add	v17.4s, v17.4s, v6.4s
	add	v1.4s, v1.4s, v16.4s
	rev32	v10.8h, v10.8h
	eor	v11.16b, v3.16b, v11.16b
	add	v17.4s, v17.4s, v13.4s
	eor	v8.16b, v1.16b, v8.16b
	add	v5.4s, v5.4s, v10.4s
	rev32	v11.8h, v11.8h
	eor	v0.16b, v17.16b, v0.16b
	ushr	v15.4s, v8.4s, #12
	shl	v8.4s, v8.4s, #20
	eor	v9.16b, v5.16b, v9.16b
	add	v4.4s, v4.4s, v11.4s
	rev32	v0.8h, v0.8h
	str	q27, [sp, #176]
	mov	v27.16b, v30.16b
	orr	v8.16b, v8.16b, v15.16b
	ushr	v15.4s, v9.4s, #12
	shl	v9.4s, v9.4s, #20
	eor	v31.16b, v4.16b, v31.16b
	add	v12.4s, v12.4s, v0.4s
	add	v14.4s, v14.4s, v25.4s
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v31.4s, #12
	shl	v31.4s, v31.4s, #20
	eor	v13.16b, v12.16b, v13.16b
	add	v14.4s, v14.4s, v8.4s
	add	v2.4s, v2.4s, v27.4s
	orr	v31.16b, v31.16b, v15.16b
	ushr	v15.4s, v13.4s, #12
	shl	v13.4s, v13.4s, #20
	eor	v16.16b, v14.16b, v16.16b
	add	v2.4s, v2.4s, v9.4s
	add	v3.4s, v3.4s, v20.4s
	orr	v13.16b, v13.16b, v15.16b
	ushr	v15.4s, v16.4s, #8
	shl	v16.4s, v16.4s, #24
	eor	v10.16b, v2.16b, v10.16b
	add	v3.4s, v3.4s, v31.4s
	add	v17.4s, v17.4s, v7.4s
	orr	v16.16b, v16.16b, v15.16b
	ushr	v15.4s, v10.4s, #8
	shl	v10.4s, v10.4s, #24
	eor	v11.16b, v3.16b, v11.16b
	add	v17.4s, v17.4s, v13.4s
	mov	v30.16b, v23.16b
	orr	v10.16b, v10.16b, v15.16b
	ushr	v15.4s, v11.4s, #8
	shl	v11.4s, v11.4s, #24
	eor	v0.16b, v17.16b, v0.16b
	add	v1.4s, v16.4s, v1.4s
	ldur	q23, [x29, #-144]
	orr	v11.16b, v11.16b, v15.16b
	ushr	v15.4s, v0.4s, #8
	shl	v0.4s, v0.4s, #24
	eor	v8.16b, v1.16b, v8.16b
	add	v5.4s, v10.4s, v5.4s
	orr	v0.16b, v0.16b, v15.16b
	ushr	v15.4s, v8.4s, #7
	shl	v8.4s, v8.4s, #25
	eor	v9.16b, v5.16b, v9.16b
	add	v4.4s, v11.4s, v4.4s
	orr	v8.16b, v8.16b, v15.16b
	ushr	v15.4s, v9.4s, #7
	shl	v9.4s, v9.4s, #25
	eor	v31.16b, v4.16b, v31.16b
	add	v12.4s, v0.4s, v12.4s
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v31.4s, #7
	shl	v31.4s, v31.4s, #25
	eor	v13.16b, v12.16b, v13.16b
	add	v14.4s, v14.4s, v23.4s
	orr	v31.16b, v31.16b, v15.16b
	ushr	v15.4s, v13.4s, #7
	shl	v13.4s, v13.4s, #25
	add	v14.4s, v14.4s, v9.4s
	add	v2.4s, v2.4s, v29.4s
	orr	v13.16b, v13.16b, v15.16b
	eor	v0.16b, v14.16b, v0.16b
	add	v2.4s, v2.4s, v31.4s
	add	v3.4s, v3.4s, v30.4s
	rev32	v0.8h, v0.8h
	eor	v16.16b, v2.16b, v16.16b
	add	v3.4s, v3.4s, v13.4s
	add	v17.4s, v17.4s, v26.4s
	add	v4.4s, v4.4s, v0.4s
	rev32	v16.8h, v16.8h
	eor	v10.16b, v3.16b, v10.16b
	add	v17.4s, v17.4s, v8.4s
	ldur	q22, [x29, #-128]
	eor	v9.16b, v4.16b, v9.16b
	add	v12.4s, v12.4s, v16.4s
	rev32	v10.8h, v10.8h
	eor	v11.16b, v17.16b, v11.16b
	ushr	v15.4s, v9.4s, #12
	shl	v9.4s, v9.4s, #20
	eor	v31.16b, v12.16b, v31.16b
	add	v1.4s, v1.4s, v10.4s
	rev32	v11.8h, v11.8h
	ldr	q26, [sp, #176]
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v31.4s, #12
	shl	v31.4s, v31.4s, #20
	eor	v13.16b, v1.16b, v13.16b
	add	v5.4s, v5.4s, v11.4s
	add	v14.4s, v14.4s, v24.4s
	orr	v31.16b, v31.16b, v15.16b
	ushr	v15.4s, v13.4s, #12
	shl	v13.4s, v13.4s, #20
	eor	v8.16b, v5.16b, v8.16b
	add	v14.4s, v14.4s, v9.4s
	add	v2.4s, v2.4s, v22.4s
	orr	v13.16b, v13.16b, v15.16b
	ushr	v15.4s, v8.4s, #12
	shl	v8.4s, v8.4s, #20
	eor	v0.16b, v14.16b, v0.16b
	add	v2.4s, v2.4s, v31.4s
	add	v3.4s, v3.4s, v28.4s
	orr	v8.16b, v8.16b, v15.16b
	ushr	v15.4s, v0.4s, #8
	shl	v0.4s, v0.4s, #24
	eor	v16.16b, v2.16b, v16.16b
	add	v3.4s, v3.4s, v13.4s
	add	v17.4s, v17.4s, v26.4s
	orr	v0.16b, v0.16b, v15.16b
	ushr	v15.4s, v16.4s, #8
	shl	v16.4s, v16.4s, #24
	eor	v10.16b, v3.16b, v10.16b
	add	v17.4s, v17.4s, v8.4s
	orr	v16.16b, v16.16b, v15.16b
	ushr	v15.4s, v10.4s, #8
	shl	v10.4s, v10.4s, #24
	eor	v11.16b, v17.16b, v11.16b
	add	v4.4s, v0.4s, v4.4s
	orr	v10.16b, v10.16b, v15.16b
	ushr	v15.4s, v11.4s, #8
	shl	v11.4s, v11.4s, #24
	eor	v9.16b, v4.16b, v9.16b
	add	v12.4s, v16.4s, v12.4s
	orr	v11.16b, v11.16b, v15.16b
	ushr	v15.4s, v9.4s, #7
	shl	v9.4s, v9.4s, #25
	eor	v31.16b, v12.16b, v31.16b
	add	v1.4s, v10.4s, v1.4s
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v31.4s, #7
	shl	v31.4s, v31.4s, #25
	eor	v13.16b, v1.16b, v13.16b
	add	v5.4s, v11.4s, v5.4s
	orr	v31.16b, v31.16b, v15.16b
	ushr	v15.4s, v13.4s, #7
	shl	v13.4s, v13.4s, #25
	eor	v8.16b, v5.16b, v8.16b
	orr	v13.16b, v13.16b, v15.16b
	ushr	v15.4s, v8.4s, #7
	shl	v8.4s, v8.4s, #25
	orr	v8.16b, v8.16b, v15.16b
	add	v14.4s, v14.4s, v18.4s
	add	v14.4s, v14.4s, v8.4s
	add	v2.4s, v2.4s, v27.4s
	eor	v16.16b, v14.16b, v16.16b
	add	v2.4s, v2.4s, v9.4s
	add	v3.4s, v3.4s, v7.4s
	rev32	v16.8h, v16.8h
	eor	v10.16b, v2.16b, v10.16b
	add	v3.4s, v3.4s, v31.4s
	add	v17.4s, v17.4s, v21.4s
	add	v1.4s, v1.4s, v16.4s
	rev32	v10.8h, v10.8h
	eor	v11.16b, v3.16b, v11.16b
	add	v17.4s, v17.4s, v13.4s
	eor	v8.16b, v1.16b, v8.16b
	add	v5.4s, v5.4s, v10.4s
	rev32	v11.8h, v11.8h
	eor	v0.16b, v17.16b, v0.16b
	add	v14.4s, v14.4s, v6.4s
	ldur	q6, [x29, #-96]
	ushr	v15.4s, v8.4s, #12
	shl	v8.4s, v8.4s, #20
	eor	v9.16b, v5.16b, v9.16b
	add	v4.4s, v4.4s, v11.4s
	rev32	v0.8h, v0.8h
	stur	q20, [x29, #-160]
	mov	v20.16b, v29.16b
	orr	v8.16b, v8.16b, v15.16b
	ushr	v15.4s, v9.4s, #12
	shl	v9.4s, v9.4s, #20
	eor	v31.16b, v4.16b, v31.16b
	add	v12.4s, v12.4s, v0.4s
	mov	v19.16b, v29.16b
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v31.4s, #12
	shl	v31.4s, v31.4s, #20
	eor	v13.16b, v12.16b, v13.16b
	add	v14.4s, v14.4s, v8.4s
	add	v2.4s, v2.4s, v20.4s
	mov	v19.16b, v28.16b
	orr	v31.16b, v31.16b, v15.16b
	ushr	v15.4s, v13.4s, #12
	shl	v13.4s, v13.4s, #20
	eor	v16.16b, v14.16b, v16.16b
	add	v2.4s, v2.4s, v9.4s
	add	v3.4s, v3.4s, v6.4s
	orr	v13.16b, v13.16b, v15.16b
	ushr	v15.4s, v16.4s, #8
	shl	v16.4s, v16.4s, #24
	eor	v10.16b, v2.16b, v10.16b
	add	v3.4s, v3.4s, v31.4s
	add	v17.4s, v17.4s, v19.4s
	orr	v16.16b, v16.16b, v15.16b
	ushr	v15.4s, v10.4s, #8
	shl	v10.4s, v10.4s, #24
	eor	v11.16b, v3.16b, v11.16b
	add	v17.4s, v17.4s, v13.4s
	orr	v10.16b, v10.16b, v15.16b
	ushr	v15.4s, v11.4s, #8
	shl	v11.4s, v11.4s, #24
	eor	v0.16b, v17.16b, v0.16b
	add	v1.4s, v16.4s, v1.4s
	orr	v11.16b, v11.16b, v15.16b
	ushr	v15.4s, v0.4s, #8
	shl	v0.4s, v0.4s, #24
	eor	v8.16b, v1.16b, v8.16b
	add	v5.4s, v10.4s, v5.4s
	orr	v0.16b, v0.16b, v15.16b
	ushr	v15.4s, v8.4s, #7
	shl	v8.4s, v8.4s, #25
	eor	v9.16b, v5.16b, v9.16b
	add	v4.4s, v11.4s, v4.4s
	orr	v8.16b, v8.16b, v15.16b
	ushr	v15.4s, v9.4s, #7
	shl	v9.4s, v9.4s, #25
	eor	v31.16b, v4.16b, v31.16b
	add	v12.4s, v0.4s, v12.4s
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v31.4s, #7
	shl	v31.4s, v31.4s, #25
	eor	v13.16b, v12.16b, v13.16b
	add	v14.4s, v14.4s, v25.4s
	orr	v31.16b, v31.16b, v15.16b
	ushr	v15.4s, v13.4s, #7
	shl	v13.4s, v13.4s, #25
	add	v14.4s, v14.4s, v9.4s
	add	v2.4s, v2.4s, v30.4s
	orr	v13.16b, v13.16b, v15.16b
	eor	v0.16b, v14.16b, v0.16b
	add	v2.4s, v2.4s, v31.4s
	add	v3.4s, v3.4s, v24.4s
	rev32	v0.8h, v0.8h
	eor	v16.16b, v2.16b, v16.16b
	add	v3.4s, v3.4s, v13.4s
	add	v17.4s, v17.4s, v26.4s
	mov	v29.16b, v27.16b
	add	v4.4s, v4.4s, v0.4s
	rev32	v16.8h, v16.8h
	eor	v10.16b, v3.16b, v10.16b
	add	v17.4s, v17.4s, v8.4s
	ldur	q27, [x29, #-160]
	eor	v9.16b, v4.16b, v9.16b
	add	v12.4s, v12.4s, v16.4s
	rev32	v10.8h, v10.8h
	eor	v11.16b, v17.16b, v11.16b
	ldur	q6, [x29, #-80]
	ushr	v15.4s, v9.4s, #12
	shl	v9.4s, v9.4s, #20
	eor	v31.16b, v12.16b, v31.16b
	add	v1.4s, v1.4s, v10.4s
	rev32	v11.8h, v11.8h
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v31.4s, #12
	shl	v31.4s, v31.4s, #20
	eor	v13.16b, v1.16b, v13.16b
	add	v5.4s, v5.4s, v11.4s
	add	v14.4s, v14.4s, v22.4s
	orr	v31.16b, v31.16b, v15.16b
	ushr	v15.4s, v13.4s, #12
	shl	v13.4s, v13.4s, #20
	eor	v8.16b, v5.16b, v8.16b
	add	v14.4s, v14.4s, v9.4s
	add	v2.4s, v2.4s, v27.4s
	orr	v13.16b, v13.16b, v15.16b
	ushr	v15.4s, v8.4s, #12
	shl	v8.4s, v8.4s, #20
	eor	v0.16b, v14.16b, v0.16b
	add	v2.4s, v2.4s, v31.4s
	add	v3.4s, v3.4s, v6.4s
	orr	v8.16b, v8.16b, v15.16b
	ushr	v15.4s, v0.4s, #8
	shl	v0.4s, v0.4s, #24
	eor	v16.16b, v2.16b, v16.16b
	add	v3.4s, v3.4s, v13.4s
	add	v17.4s, v17.4s, v23.4s
	orr	v0.16b, v0.16b, v15.16b
	ushr	v15.4s, v16.4s, #8
	shl	v16.4s, v16.4s, #24
	eor	v10.16b, v3.16b, v10.16b
	add	v17.4s, v17.4s, v8.4s
	orr	v16.16b, v16.16b, v15.16b
	ushr	v15.4s, v10.4s, #8
	shl	v10.4s, v10.4s, #24
	eor	v11.16b, v17.16b, v11.16b
	add	v4.4s, v0.4s, v4.4s
	orr	v10.16b, v10.16b, v15.16b
	ushr	v15.4s, v11.4s, #8
	shl	v11.4s, v11.4s, #24
	eor	v9.16b, v4.16b, v9.16b
	add	v12.4s, v16.4s, v12.4s
	orr	v11.16b, v11.16b, v15.16b
	ushr	v15.4s, v9.4s, #7
	shl	v9.4s, v9.4s, #25
	eor	v31.16b, v12.16b, v31.16b
	add	v1.4s, v10.4s, v1.4s
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v31.4s, #7
	shl	v31.4s, v31.4s, #25
	eor	v13.16b, v1.16b, v13.16b
	add	v5.4s, v11.4s, v5.4s
	orr	v31.16b, v31.16b, v15.16b
	ushr	v15.4s, v13.4s, #7
	shl	v13.4s, v13.4s, #25
	eor	v8.16b, v5.16b, v8.16b
	orr	v13.16b, v13.16b, v15.16b
	ushr	v15.4s, v8.4s, #7
	shl	v8.4s, v8.4s, #25
	orr	v8.16b, v8.16b, v15.16b
	add	v14.4s, v14.4s, v29.4s
	add	v14.4s, v14.4s, v8.4s
	add	v2.4s, v2.4s, v20.4s
	mov	v28.16b, v7.16b
	eor	v16.16b, v14.16b, v16.16b
	add	v2.4s, v2.4s, v9.4s
	add	v3.4s, v3.4s, v19.4s
	rev32	v16.8h, v16.8h
	eor	v10.16b, v2.16b, v10.16b
	add	v3.4s, v3.4s, v31.4s
	add	v17.4s, v17.4s, v28.4s
	add	v1.4s, v1.4s, v16.4s
	rev32	v10.8h, v10.8h
	eor	v11.16b, v3.16b, v11.16b
	add	v17.4s, v17.4s, v13.4s
	eor	v8.16b, v1.16b, v8.16b
	add	v5.4s, v5.4s, v10.4s
	rev32	v11.8h, v11.8h
	eor	v0.16b, v17.16b, v0.16b
	ushr	v15.4s, v8.4s, #12
	shl	v8.4s, v8.4s, #20
	eor	v9.16b, v5.16b, v9.16b
	add	v4.4s, v4.4s, v11.4s
	rev32	v0.8h, v0.8h
	orr	v8.16b, v8.16b, v15.16b
	ushr	v15.4s, v9.4s, #12
	shl	v9.4s, v9.4s, #20
	eor	v31.16b, v4.16b, v31.16b
	add	v12.4s, v12.4s, v0.4s
	add	v14.4s, v14.4s, v21.4s
	orr	v9.16b, v9.16b, v15.16b
	ushr	v15.4s, v31.4s, #12
	shl	v31.4s, v31.4s, #20
	eor	v13.16b, v12.16b, v13.16b
	add	v14.4s, v14.4s, v8.4s
	add	v2.4s, v2.4s, v30.4s
	orr	v31.16b, v31.16b, v15.16b
	ushr	v15.4s, v13.4s, #12
	shl	v13.4s, v13.4s, #20
	eor	v16.16b, v14.16b, v16.16b
	add	v2.4s, v2.4s, v9.4s
	orr	v13.16b, v13.16b, v15.16b
	ushr	v15.4s, v16.4s, #8
	shl	v16.4s, v16.4s, #24
	eor	v10.16b, v2.16b, v10.16b
	orr	v16.16b, v16.16b, v15.16b
	ushr	v15.4s, v10.4s, #8
	shl	v10.4s, v10.4s, #24
	add	v3.4s, v3.4s, v18.4s
	orr	v10.16b, v10.16b, v15.16b
	add	v15.4s, v3.4s, v31.4s
	eor	v3.16b, v15.16b, v11.16b
	ushr	v11.4s, v3.4s, #8
	shl	v3.4s, v3.4s, #24
	orr	v11.16b, v3.16b, v11.16b
	add	v3.4s, v17.4s, v6.4s
	add	v17.4s, v3.4s, v13.4s
	eor	v0.16b, v17.16b, v0.16b
	ushr	v3.4s, v0.4s, #8
	shl	v0.4s, v0.4s, #24
	add	v1.4s, v16.4s, v1.4s
	orr	v0.16b, v0.16b, v3.16b
	eor	v3.16b, v1.16b, v8.16b
	ushr	v8.4s, v3.4s, #7
	shl	v3.4s, v3.4s, #25
	add	v5.4s, v10.4s, v5.4s
	orr	v8.16b, v3.16b, v8.16b
	eor	v3.16b, v5.16b, v9.16b
	add	v4.4s, v11.4s, v4.4s
	ushr	v9.4s, v3.4s, #7
	shl	v3.4s, v3.4s, #25
	eor	v31.16b, v4.16b, v31.16b
	mov	v7.16b, v23.16b
	mov	v23.16b, v28.16b
	mov	v28.16b, v6.16b
	orr	v3.16b, v3.16b, v9.16b
	ushr	v9.4s, v31.4s, #7
	shl	v31.4s, v31.4s, #25
	ldur	q6, [x29, #-64]
	orr	v31.16b, v31.16b, v9.16b
	add	v9.4s, v0.4s, v12.4s
	eor	v12.16b, v9.16b, v13.16b
	ushr	v13.4s, v12.4s, #7
	shl	v12.4s, v12.4s, #25
	orr	v12.16b, v12.16b, v13.16b
	add	v13.4s, v14.4s, v6.4s
	add	v13.4s, v13.4s, v3.4s
	eor	v0.16b, v13.16b, v0.16b
	add	v2.4s, v2.4s, v24.4s
	rev32	v14.8h, v0.8h
	add	v0.4s, v2.4s, v31.4s
	add	v6.4s, v4.4s, v14.4s
	eor	v2.16b, v0.16b, v16.16b
	eor	v3.16b, v6.16b, v3.16b
	rev32	v16.8h, v2.8h
	ushr	v4.4s, v3.4s, #12
	shl	v3.4s, v3.4s, #20
	add	v2.4s, v9.4s, v16.4s
	orr	v4.16b, v3.16b, v4.16b
	eor	v3.16b, v2.16b, v31.16b
	ushr	v31.4s, v3.4s, #12
	shl	v3.4s, v3.4s, #20
	orr	v3.16b, v3.16b, v31.16b
	add	v31.4s, v15.4s, v22.4s
	add	v31.4s, v31.4s, v12.4s
	add	v17.4s, v17.4s, v7.4s
	eor	v9.16b, v31.16b, v10.16b
	add	v17.4s, v17.4s, v8.4s
	rev32	v9.8h, v9.8h
	eor	v11.16b, v17.16b, v11.16b
	add	v1.4s, v1.4s, v9.4s
	rev32	v11.8h, v11.8h
	eor	v10.16b, v1.16b, v12.16b
	add	v5.4s, v5.4s, v11.4s
	ushr	v12.4s, v10.4s, #12
	shl	v10.4s, v10.4s, #20
	eor	v8.16b, v5.16b, v8.16b
	orr	v10.16b, v10.16b, v12.16b
	ushr	v12.4s, v8.4s, #12
	shl	v8.4s, v8.4s, #20
	orr	v8.16b, v8.16b, v12.16b
	add	v12.4s, v13.4s, v27.4s
	add	v12.4s, v12.4s, v4.4s
	eor	v13.16b, v12.16b, v14.16b
	ldur	q14, [x29, #-96]
	mov	v25.16b, v29.16b
	add	v29.4s, v12.4s, v20.4s
	add	v20.4s, v31.4s, v26.4s
	add	v0.4s, v0.4s, v14.4s
	add	v0.4s, v0.4s, v3.4s
	eor	v16.16b, v0.16b, v16.16b
	add	v0.4s, v0.4s, v30.4s
	ldur	q30, [x29, #-112]
	add	v20.4s, v20.4s, v10.4s
	eor	v31.16b, v20.16b, v9.16b
	add	v20.4s, v20.4s, v28.4s
	add	v17.4s, v17.4s, v30.4s
	add	v17.4s, v17.4s, v8.4s
	eor	v9.16b, v17.16b, v11.16b
	ushr	v28.4s, v13.4s, #8
	shl	v11.4s, v13.4s, #24
	orr	v28.16b, v11.16b, v28.16b
	ushr	v11.4s, v16.4s, #8
	shl	v16.4s, v16.4s, #24
	orr	v16.16b, v16.16b, v11.16b
	ushr	v11.4s, v31.4s, #8
	shl	v31.4s, v31.4s, #24
	add	v6.4s, v28.4s, v6.4s
	orr	v31.16b, v31.16b, v11.16b
	ushr	v11.4s, v9.4s, #8
	shl	v9.4s, v9.4s, #24
	add	v2.4s, v16.4s, v2.4s
	eor	v4.16b, v6.16b, v4.16b
	orr	v9.16b, v9.16b, v11.16b
	add	v1.4s, v31.4s, v1.4s
	eor	v3.16b, v2.16b, v3.16b
	ushr	v11.4s, v4.4s, #7
	shl	v4.4s, v4.4s, #25
	add	v5.4s, v9.4s, v5.4s
	eor	v10.16b, v1.16b, v10.16b
	orr	v4.16b, v4.16b, v11.16b
	ushr	v11.4s, v3.4s, #7
	shl	v3.4s, v3.4s, #25
	eor	v8.16b, v5.16b, v8.16b
	orr	v3.16b, v3.16b, v11.16b
	ushr	v11.4s, v10.4s, #7
	shl	v10.4s, v10.4s, #25
	orr	v10.16b, v10.16b, v11.16b
	ushr	v11.4s, v8.4s, #7
	shl	v8.4s, v8.4s, #25
	orr	v8.16b, v8.16b, v11.16b
	add	v29.4s, v29.4s, v8.4s
	eor	v16.16b, v29.16b, v16.16b
	add	v0.4s, v0.4s, v4.4s
	mov	v12.16b, v26.16b
	add	v17.4s, v17.4s, v19.4s
	add	v26.4s, v29.4s, v23.4s
	eor	v29.16b, v0.16b, v31.16b
	add	v20.4s, v20.4s, v3.4s
	rev32	v16.8h, v16.8h
	stur	q18, [x29, #-176]
	mov	v18.16b, v27.16b
	add	v0.4s, v0.4s, v24.4s
	eor	v27.16b, v20.16b, v9.16b
	add	v17.4s, v17.4s, v10.4s
	rev32	v24.8h, v29.8h
	add	v1.4s, v1.4s, v16.4s
	add	v20.4s, v20.4s, v25.4s
	eor	v25.16b, v17.16b, v28.16b
	rev32	v27.8h, v27.8h
	add	v5.4s, v5.4s, v24.4s
	eor	v28.16b, v1.16b, v8.16b
	rev32	v25.8h, v25.8h
	add	v6.4s, v6.4s, v27.4s
	eor	v4.16b, v5.16b, v4.16b
	ushr	v31.4s, v28.4s, #12
	shl	v28.4s, v28.4s, #20
	add	v2.4s, v2.4s, v25.4s
	eor	v3.16b, v6.16b, v3.16b
	orr	v28.16b, v28.16b, v31.16b
	ushr	v31.4s, v4.4s, #12
	shl	v4.4s, v4.4s, #20
	eor	v29.16b, v2.16b, v10.16b
	orr	v4.16b, v4.16b, v31.16b
	ushr	v31.4s, v3.4s, #12
	shl	v3.4s, v3.4s, #20
	add	v26.4s, v26.4s, v28.4s
	orr	v3.16b, v3.16b, v31.16b
	ushr	v31.4s, v29.4s, #12
	shl	v29.4s, v29.4s, #20
	eor	v16.16b, v26.16b, v16.16b
	add	v0.4s, v0.4s, v4.4s
	add	v17.4s, v17.4s, v12.4s
	orr	v29.16b, v29.16b, v31.16b
	eor	v24.16b, v0.16b, v24.16b
	add	v0.4s, v0.4s, v22.4s
	add	v20.4s, v20.4s, v3.4s
	ushr	v22.4s, v16.4s, #8
	shl	v16.4s, v16.4s, #24
	add	v23.4s, v26.4s, v21.4s
	eor	v21.16b, v20.16b, v27.16b
	add	v17.4s, v17.4s, v29.4s
	orr	v16.16b, v16.16b, v22.16b
	ushr	v22.4s, v24.4s, #8
	shl	v24.4s, v24.4s, #24
	eor	v25.16b, v17.16b, v25.16b
	orr	v22.16b, v24.16b, v22.16b
	ushr	v24.4s, v21.4s, #8
	shl	v21.4s, v21.4s, #24
	orr	v21.16b, v21.16b, v24.16b
	ushr	v24.4s, v25.4s, #8
	shl	v25.4s, v25.4s, #24
	add	v1.4s, v16.4s, v1.4s
	orr	v24.16b, v25.16b, v24.16b
	add	v5.4s, v22.4s, v5.4s
	eor	v25.16b, v1.16b, v28.16b
	add	v6.4s, v21.4s, v6.4s
	eor	v4.16b, v5.16b, v4.16b
	ushr	v27.4s, v25.4s, #7
	shl	v25.4s, v25.4s, #25
	add	v2.4s, v24.4s, v2.4s
	eor	v3.16b, v6.16b, v3.16b
	orr	v25.16b, v25.16b, v27.16b
	ushr	v27.4s, v4.4s, #7
	shl	v4.4s, v4.4s, #25
	ldur	q19, [x29, #-176]
	eor	v26.16b, v2.16b, v29.16b
	orr	v4.16b, v4.16b, v27.16b
	ushr	v27.4s, v3.4s, #7
	shl	v3.4s, v3.4s, #25
	orr	v3.16b, v3.16b, v27.16b
	ushr	v27.4s, v26.4s, #7
	shl	v26.4s, v26.4s, #25
	add	v20.4s, v20.4s, v18.4s
	add	v17.4s, v17.4s, v30.4s
	orr	v26.16b, v26.16b, v27.16b
	add	v0.4s, v0.4s, v3.4s
	eor	v16.16b, v0.16b, v16.16b
	add	v0.4s, v0.4s, v19.4s
	add	v19.4s, v20.4s, v26.4s
	add	v17.4s, v17.4s, v25.4s
	eor	v20.16b, v19.16b, v22.16b
	add	v7.4s, v19.4s, v7.4s
	eor	v19.16b, v17.16b, v21.16b
	ldur	q21, [x29, #-64]
	add	v23.4s, v23.4s, v4.4s
	eor	v24.16b, v23.16b, v24.16b
	rev32	v16.8h, v16.8h
	add	v17.4s, v17.4s, v21.4s
	rev32	v21.8h, v24.8h
	add	v6.4s, v6.4s, v21.4s
	rev32	v20.8h, v20.8h
	add	v2.4s, v2.4s, v16.4s
	eor	v4.16b, v6.16b, v4.16b
	rev32	v19.8h, v19.8h
	add	v1.4s, v1.4s, v20.4s
	eor	v3.16b, v2.16b, v3.16b
	ushr	v24.4s, v4.4s, #12
	shl	v4.4s, v4.4s, #20
	add	v5.4s, v5.4s, v19.4s
	eor	v22.16b, v1.16b, v26.16b
	orr	v4.16b, v4.16b, v24.16b
	ushr	v24.4s, v3.4s, #12
	shl	v3.4s, v3.4s, #20
	add	v18.4s, v23.4s, v14.4s
	eor	v23.16b, v5.16b, v25.16b
	orr	v3.16b, v3.16b, v24.16b
	ushr	v24.4s, v22.4s, #12
	shl	v22.4s, v22.4s, #20
	orr	v22.16b, v22.16b, v24.16b
	ushr	v24.4s, v23.4s, #12
	shl	v23.4s, v23.4s, #20
	orr	v23.16b, v23.16b, v24.16b
	add	v18.4s, v18.4s, v4.4s
	add	v0.4s, v0.4s, v3.4s
	add	v24.4s, v17.4s, v23.4s
	eor	v17.16b, v18.16b, v21.16b
	add	v7.4s, v7.4s, v22.4s
	eor	v16.16b, v0.16b, v16.16b
	ushr	v21.4s, v17.4s, #8
	shl	v17.4s, v17.4s, #24
	eor	v20.16b, v7.16b, v20.16b
	orr	v21.16b, v17.16b, v21.16b
	ushr	v17.4s, v16.4s, #8
	shl	v16.4s, v16.4s, #24
	eor	v19.16b, v24.16b, v19.16b
	orr	v16.16b, v16.16b, v17.16b
	ushr	v17.4s, v20.4s, #8
	shl	v20.4s, v20.4s, #24
	orr	v25.16b, v20.16b, v17.16b
	ushr	v17.4s, v19.4s, #8
	shl	v19.4s, v19.4s, #24
	orr	v19.16b, v19.16b, v17.16b
	add	v1.4s, v25.4s, v1.4s
	eor	v22.16b, v1.16b, v22.16b
	eor	v20.16b, v1.16b, v18.16b
	add	v1.4s, v19.4s, v5.4s
	eor	v26.16b, v1.16b, v0.16b
	add	v0.4s, v21.4s, v6.4s
	eor	v5.16b, v1.16b, v23.16b
	eor	v1.16b, v0.16b, v4.16b
	eor	v17.16b, v0.16b, v7.16b
	add	v0.4s, v16.4s, v2.4s
	eor	v2.16b, v0.16b, v3.16b
	eor	v6.16b, v0.16b, v24.16b
	ushr	v0.4s, v1.4s, #7
	shl	v1.4s, v1.4s, #25
	orr	v0.16b, v1.16b, v0.16b
	ushr	v1.4s, v2.4s, #7
	shl	v2.4s, v2.4s, #25
	orr	v1.16b, v2.16b, v1.16b
	ushr	v2.4s, v22.4s, #7
	shl	v3.4s, v22.4s, #25
	orr	v2.16b, v3.16b, v2.16b
	ushr	v3.4s, v5.4s, #7
	shl	v4.4s, v5.4s, #25
	orr	v3.16b, v4.16b, v3.16b
	eor	v8.16b, v16.16b, v3.16b
	eor	v9.16b, v25.16b, v0.16b
	eor	v31.16b, v1.16b, v19.16b
	cmp	x17, x22
	eor	v15.16b, v2.16b, v21.16b
	mov	w18, w19
	b.ne	.LBB2_4
.LBB2_7:
	zip1	v0.4s, v20.4s, v26.4s
	zip2	v1.4s, v20.4s, v26.4s
	zip1	v2.4s, v17.4s, v6.4s
	zip2	v3.4s, v17.4s, v6.4s
	zip1	v4.4s, v8.4s, v9.4s
	zip2	v5.4s, v8.4s, v9.4s
	zip1	v6.4s, v31.4s, v15.4s
	zip2	v7.4s, v31.4s, v15.4s
	add	x13, x20, #4
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
	csel	x20, x13, x20, ne
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
	bl	zfs_blake3_compress_in_place_sse2
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
	add	sp, sp, #384
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
	.size	zfs_blake3_hash_many_sse2, .Lfunc_end2-zfs_blake3_hash_many_sse2
	.cfi_endproc
	.section	".note.GNU-stack","",@progbits
#endif
