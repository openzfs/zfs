/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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

/* straight out of clang -S to blake3_neon.c, then modified to taste */

/*
 * Based on BLAKE3 v1.2.0, https://github.com/BLAKE3-team/BLAKE3
 * Copyright (c) 2019-2021 Samuel Neves
 */

#if defined(__aarch64__)

	.p2align	2                               // -- Begin function _blake3_hash_many_neon
	.globl  _blake3_hash_many_neon
	.type	_blake3_hash_many_neon,@function
_blake3_hash_many_neon:                  // @_blake3_hash_many_neon
// %bb.0:
	str	d12, [sp, #-144]!               // 8-byte Folded Spill
	stp	d11, d10, [sp, #16]             // 16-byte Folded Spill
	stp	d9, d8, [sp, #32]               // 16-byte Folded Spill
	stp	x29, x30, [sp, #48]             // 16-byte Folded Spill
	stp	x28, x27, [sp, #64]             // 16-byte Folded Spill
	stp	x26, x25, [sp, #80]             // 16-byte Folded Spill
	stp	x24, x23, [sp, #96]             // 16-byte Folded Spill
	stp	x22, x21, [sp, #112]            // 16-byte Folded Spill
	stp	x20, x19, [sp, #128]            // 16-byte Folded Spill
	mov	x29, sp
	sub	sp, sp, #672                    // =672
	ldr	x27, [x29, #152]
	ldrb	w28, [x29, #144]
	mov	w19, w6
	mov	w25, w5
	mov	x20, x4
	mov	x21, x3
	mov	x22, x2
	mov	x23, x1
	cmp	x1, #4                          // =4
	mov	x24, x0
	str	x2, [x29, #8]                   // 8-byte Folded Spill
	str	w7, [sp, #12]                   // 4-byte Folded Spill
	b.lo	.LBB2_6
// %bb.1:
	orr	w8, w7, w19
	cmp	w25, #0                         // =0
	str	w8, [sp, #44]                   // 4-byte Folded Spill
	mov	w8, #58983
	mov	w9, #44677
	movk	w8, #27145, lsl #16
	movk	w9, #47975, lsl #16
	mov	w10, #62322
	mov	w11, #62778
	csetm	x12, ne
	movk	w10, #15470, lsl #16
	movk	w11, #42319, lsl #16
	dup	v8.4s, w8
	dup	v9.4s, w9
	and	x9, x12, #0x1
	and	x8, x12, #0x2
	dup	v10.4s, w10
	dup	v11.4s, w11
	stp	x8, x9, [sp, #24]               // 16-byte Folded Spill
	and	x8, x12, #0x3
	movi	v12.4s, #64
	str	x8, [sp, #16]                   // 8-byte Folded Spill
	stp	q9, q8, [sp, #80]               // 32-byte Folded Spill
	stp	q11, q10, [sp, #48]             // 32-byte Folded Spill
	b	.LBB2_3
.LBB2_2:                                //   in Loop: Header=BB2_3 Depth=1
	trn1	v16.4s, v6.4s, v7.4s
	trn2	v6.4s, v6.4s, v7.4s
	trn1	v7.4s, v5.4s, v4.4s
	trn2	v4.4s, v5.4s, v4.4s
	trn1	v5.4s, v3.4s, v2.4s
	trn2	v2.4s, v3.4s, v2.4s
	trn1	v3.4s, v1.4s, v0.4s
	trn2	v0.4s, v1.4s, v0.4s
	ext	v1.16b, v7.16b, v16.16b, #8
	ext	v17.16b, v4.16b, v6.16b, #8
	ext	v1.16b, v16.16b, v1.16b, #8
	mov	v16.d[1], v7.d[0]
	ext	v7.16b, v3.16b, v5.16b, #8
	ext	v17.16b, v6.16b, v17.16b, #8
	mov	v6.d[1], v4.d[0]
	ext	v4.16b, v0.16b, v2.16b, #8
	ext	v7.16b, v5.16b, v7.16b, #8
	ext	v4.16b, v2.16b, v4.16b, #8
	mov	v5.d[1], v3.d[0]
	mov	v2.d[1], v0.d[0]
	stp	q1, q7, [x27, #64]
	stp	q17, q4, [x27, #96]
	stp	q16, q5, [x27]
	stp	q6, q2, [x27, #32]
	ldr	x22, [x29, #8]                  // 8-byte Folded Reload
	add	x8, x20, #4                     // =4
	cmp	w25, #0                         // =0
	sub	x23, x23, #4                    // =4
	add	x24, x24, #32                   // =32
	csel	x20, x20, x8, eq
	cmp	x23, #3                         // =3
	add	x27, x27, #128                  // =128
	b.ls	.LBB2_6
.LBB2_3:                                // =>This Loop Header: Depth=1
                                        //     Child Loop BB2_5 Depth 2
	mov	x8, x21
	ld1r	{ v6.4s }, [x8], #4
	add	x9, x21, #8                     // =8
	add	x10, x21, #12                   // =12
	add	x11, x21, #16                   // =16
	add	x12, x21, #20                   // =20
	add	x13, x21, #24                   // =24
	add	x14, x21, #28                   // =28
	ld1r	{ v5.4s }, [x9]
	ld1r	{ v4.4s }, [x10]
	ld1r	{ v3.4s }, [x11]
	ld1r	{ v2.4s }, [x12]
	ld1r	{ v1.4s }, [x13]
	ld1r	{ v7.4s }, [x8]
	ld1r	{ v0.4s }, [x14]
	cbz	x22, .LBB2_2
// %bb.4:                               //   in Loop: Header=BB2_3 Depth=1
	ldp	x9, x8, [sp, #24]               // 16-byte Folded Reload
	ldr	x11, [sp, #16]                  // 8-byte Folded Reload
	lsr	x10, x20, #32
	fmov	s16, w20
	add	x8, x20, x8
	add	x9, x20, x9
	fmov	s17, w10
	mov	v16.s[1], w8
	lsr	x8, x8, #32
	add	x11, x20, x11
	mov	v16.s[2], w9
	lsr	x9, x9, #32
	mov	v17.s[1], w8
	lsr	x8, x11, #32
	mov	v17.s[2], w9
	mov	v17.s[3], w8
	ldr	w8, [sp, #44]                   // 4-byte Folded Reload
	mov	v16.s[3], w11
	mov	w26, #48
	stp	q17, q16, [sp, #112]            // 32-byte Folded Spill
.LBB2_5:                                //   Parent Loop BB2_3 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	ldp	x9, x10, [x24]
	ldp	x11, x12, [x24, #16]
	subs	x22, x22, #1                    // =1
	add	x0, sp, #144                    // =144
	add	x9, x9, x26
	ldp	q16, q17, [x9, #-48]
	ldp	q18, q19, [x9, #-16]
	add	x9, x10, x26
	add	x10, x11, x26
	add	x11, x12, x26
	ldp	q29, q21, [x9, #-48]
	ldp	q30, q22, [x10, #-48]
	ldp	q31, q28, [x11, #-48]
	ldp	q26, q24, [x9, #-16]
	csel	w9, w28, wzr, eq
	orr	w8, w9, w8
	and	w8, w8, #0xff
	ldp	q23, q20, [x10, #-16]
	ldp	q27, q25, [x11, #-16]
	stp	q1, q0, [sp, #240]
	dup	v0.4s, w8
	stp	q3, q2, [sp, #208]
	str	q0, [sp, #384]
	trn1	v0.4s, v16.4s, v29.4s
	trn1	v2.4s, v30.4s, v31.4s
	stp	q6, q7, [sp, #144]
	trn2	v1.4s, v16.4s, v29.4s
	trn2	v3.4s, v30.4s, v31.4s
	trn1	v6.4s, v22.4s, v28.4s
	trn2	v7.4s, v22.4s, v28.4s
	trn1	v22.4s, v19.4s, v24.4s
	trn2	v19.4s, v19.4s, v24.4s
	ext	v24.16b, v2.16b, v0.16b, #8
	ext	v24.16b, v0.16b, v24.16b, #8
	mov	v0.d[1], v2.d[0]
	ext	v2.16b, v3.16b, v1.16b, #8
	ext	v2.16b, v1.16b, v2.16b, #8
	mov	v1.d[1], v3.d[0]
	stp	q0, q1, [sp, #400]
	ldr	q0, [sp, #128]                  // 16-byte Folded Reload
	stp	q5, q4, [sp, #176]
	trn1	v4.4s, v17.4s, v21.4s
	trn2	v5.4s, v17.4s, v21.4s
	stp	q24, q2, [sp, #432]
	ext	v2.16b, v6.16b, v4.16b, #8
	ext	v3.16b, v7.16b, v5.16b, #8
	trn1	v16.4s, v18.4s, v26.4s
	trn2	v17.4s, v18.4s, v26.4s
	trn1	v18.4s, v23.4s, v27.4s
	trn2	v21.4s, v23.4s, v27.4s
	ext	v2.16b, v4.16b, v2.16b, #8
	ext	v3.16b, v5.16b, v3.16b, #8
	stp	q2, q3, [sp, #496]
	ext	v2.16b, v18.16b, v16.16b, #8
	ext	v3.16b, v21.16b, v17.16b, #8
	str	q0, [sp, #336]
	ldr	q0, [sp, #112]                  // 16-byte Folded Reload
	trn1	v23.4s, v20.4s, v25.4s
	trn2	v20.4s, v20.4s, v25.4s
	ext	v2.16b, v16.16b, v2.16b, #8
	ext	v3.16b, v17.16b, v3.16b, #8
	stp	q2, q3, [sp, #560]
	ext	v2.16b, v23.16b, v22.16b, #8
	ext	v3.16b, v20.16b, v19.16b, #8
	mov	v4.d[1], v6.d[0]
	mov	v5.d[1], v7.d[0]
	mov	v16.d[1], v18.d[0]
	mov	v17.d[1], v21.d[0]
	ext	v2.16b, v22.16b, v2.16b, #8
	mov	v22.d[1], v23.d[0]
	ext	v3.16b, v19.16b, v3.16b, #8
	mov	v19.d[1], v20.d[0]
	add	x1, sp, #400                    // =400
	mov	x2, xzr
	stp	q2, q3, [sp, #624]
	stp	q4, q5, [sp, #464]
	stp	q16, q17, [sp, #528]
	stp	q22, q19, [sp, #592]
	stp	q8, q9, [sp, #272]
	stp	q10, q11, [sp, #304]
	stp	q0, q12, [sp, #352]
	bl	round_fn4
	add	x0, sp, #144                    // =144
	add	x1, sp, #400                    // =400
	mov	w2, #1
	bl	round_fn4
	add	x0, sp, #144                    // =144
	add	x1, sp, #400                    // =400
	mov	w2, #2
	bl	round_fn4
	add	x0, sp, #144                    // =144
	add	x1, sp, #400                    // =400
	mov	w2, #3
	bl	round_fn4
	add	x0, sp, #144                    // =144
	add	x1, sp, #400                    // =400
	mov	w2, #4
	bl	round_fn4
	add	x0, sp, #144                    // =144
	add	x1, sp, #400                    // =400
	mov	w2, #5
	bl	round_fn4
	add	x0, sp, #144                    // =144
	add	x1, sp, #400                    // =400
	mov	w2, #6
	bl	round_fn4
	ldp	q0, q1, [sp, #144]
	ldp	q2, q3, [sp, #272]
	ldp	q4, q16, [sp, #176]
	ldp	q5, q17, [sp, #304]
	ldp	q18, q19, [sp, #208]
	ldp	q20, q21, [sp, #336]
	eor	v6.16b, v2.16b, v0.16b
	eor	v7.16b, v3.16b, v1.16b
	ldp	q0, q22, [sp, #240]
	eor	v5.16b, v5.16b, v4.16b
	eor	v4.16b, v17.16b, v16.16b
	ldp	q1, q16, [sp, #368]
	ldp	q11, q10, [sp, #48]             // 32-byte Folded Reload
	ldp	q9, q8, [sp, #80]               // 32-byte Folded Reload
	movi	v12.4s, #64
	eor	v3.16b, v20.16b, v18.16b
	eor	v2.16b, v21.16b, v19.16b
	eor	v1.16b, v1.16b, v0.16b
	eor	v0.16b, v16.16b, v22.16b
	add	x26, x26, #64                   // =64
	mov	w8, w19
	cbnz	x22, .LBB2_5
	b	.LBB2_2
.LBB2_6:
	cbz	x23, .LBB2_14
// %bb.7:
	ldr	w8, [sp, #12]                   // 4-byte Folded Reload
	cmp	w25, #0                         // =0
	orr	w8, w8, w19
	str	w8, [sp, #128]                  // 4-byte Folded Spill
	cset	w8, ne
	str	x8, [sp, #112]                  // 8-byte Folded Spill
.LBB2_8:                                // =>This Loop Header: Depth=1
                                        //     Child Loop BB2_11 Depth 2
	ldp	q1, q0, [x21]
	ldr	x25, [x24]
	ldr	w26, [sp, #128]                 // 4-byte Folded Reload
	mov	x8, x22
	stp	q1, q0, [sp, #400]
	b	.LBB2_11
.LBB2_9:                                //   in Loop: Header=BB2_11 Depth=2
	orr	w26, w26, w28
.LBB2_10:                               //   in Loop: Header=BB2_11 Depth=2
	bl	blake3_impl_get_generic_ops
	ldr	x8, [x0]
	add	x0, sp, #400                    // =400
	mov	w2, #64
	mov	x1, x25
	mov	x3, x20
	mov	w4, w26
	blr	x8
	add	x25, x25, #64                   // =64
	mov	x8, x22
	mov	w26, w19
.LBB2_11:                               //   Parent Loop BB2_8 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	subs	x22, x8, #1                     // =1
	b.eq	.LBB2_9
// %bb.12:                              //   in Loop: Header=BB2_11 Depth=2
	cbnz	x8, .LBB2_10
// %bb.13:                              //   in Loop: Header=BB2_8 Depth=1
	ldp	q0, q1, [sp, #400]
	ldr	x8, [sp, #112]                  // 8-byte Folded Reload
	add	x24, x24, #8                    // =8
	subs	x23, x23, #1                    // =1
	stp	q0, q1, [x27], #32
	ldr	x22, [x29, #8]                  // 8-byte Folded Reload
	add	x20, x20, x8
	b.ne	.LBB2_8
.LBB2_14:
	add	sp, sp, #672                    // =672
	ldp	x20, x19, [sp, #128]            // 16-byte Folded Reload
	ldp	x22, x21, [sp, #112]            // 16-byte Folded Reload
	ldp	x24, x23, [sp, #96]             // 16-byte Folded Reload
	ldp	x26, x25, [sp, #80]             // 16-byte Folded Reload
	ldp	x28, x27, [sp, #64]             // 16-byte Folded Reload
	ldp	x29, x30, [sp, #48]             // 16-byte Folded Reload
	ldp	d9, d8, [sp, #32]               // 16-byte Folded Reload
	ldp	d11, d10, [sp, #16]             // 16-byte Folded Reload
	ldr	d12, [sp], #144                 // 8-byte Folded Reload
	ret
.Lfunc_end2:
	.size	_blake3_hash_many_neon, .Lfunc_end2-_blake3_hash_many_neon
                                        // -- End function
	.p2align	2                               // -- Begin function round_fn4
	.type	round_fn4,@function
round_fn4:                              // @round_fn4
// %bb.0:
	adrp	x8, MSG_SCHEDULE
	add	x8, x8, :lo12:MSG_SCHEDULE
	add	x8, x8, x2, lsl #4
	ldrb	w9, [x8]
	ldp	q3, q6, [x0]
	ldp	q4, q5, [x0, #64]
	ldp	q17, q18, [x0, #192]
	ldr	q19, [x1, x9, lsl #4]
	ldrb	w9, [x8, #2]
	ldp	q16, q0, [x0, #32]
	ldp	q20, q22, [x0, #128]
	add	v3.4s, v19.4s, v3.4s
	str	q3, [x0]
	ldr	q19, [x1, x9, lsl #4]
	ldrb	w9, [x8, #4]
	add	v28.4s, v4.4s, v3.4s
	eor	v3.16b, v17.16b, v28.16b
	add	v6.4s, v19.4s, v6.4s
	str	q6, [x0, #16]
	ldr	q19, [x1, x9, lsl #4]
	add	v29.4s, v5.4s, v6.4s
	ldp	q2, q1, [x0, #96]
	ldrb	w9, [x8, #6]
	add	v19.4s, v19.4s, v16.4s
	ushr	v16.4s, v3.4s, #16
	shl	v3.4s, v3.4s, #16
	orr	v16.16b, v3.16b, v16.16b
	add	v3.4s, v20.4s, v16.4s
	eor	v4.16b, v3.16b, v4.16b
	ushr	v6.4s, v4.4s, #12
	shl	v4.4s, v4.4s, #20
	eor	v17.16b, v18.16b, v29.16b
	ldp	q21, q7, [x0, #224]
	orr	v20.16b, v4.16b, v6.16b
	ushr	v4.4s, v17.4s, #16
	shl	v6.4s, v17.4s, #16
	orr	v18.16b, v6.16b, v4.16b
	str	q19, [x0, #32]
	add	v4.4s, v22.4s, v18.4s
	ldp	q25, q26, [x0, #160]
	ldr	q27, [x1, x9, lsl #4]
	eor	v5.16b, v4.16b, v5.16b
	add	v30.4s, v2.4s, v19.4s
	ushr	v6.4s, v5.4s, #12
	shl	v5.4s, v5.4s, #20
	eor	v17.16b, v21.16b, v30.16b
	orr	v24.16b, v5.16b, v6.16b
	ushr	v5.4s, v17.4s, #16
	shl	v6.4s, v17.4s, #16
	orr	v23.16b, v6.16b, v5.16b
	add	v6.4s, v25.4s, v23.4s
	add	v0.4s, v27.4s, v0.4s
	eor	v2.16b, v6.16b, v2.16b
	add	v17.4s, v1.4s, v0.4s
	ushr	v0.4s, v2.4s, #12
	shl	v2.4s, v2.4s, #20
	eor	v5.16b, v7.16b, v17.16b
	orr	v22.16b, v2.16b, v0.16b
	ushr	v0.4s, v5.4s, #16
	shl	v2.4s, v5.4s, #16
	orr	v21.16b, v2.16b, v0.16b
	add	v7.4s, v26.4s, v21.4s
	ldrb	w9, [x8, #1]
	eor	v0.16b, v7.16b, v1.16b
	ushr	v1.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	orr	v19.16b, v0.16b, v1.16b
	stp	q28, q29, [x0]
	stp	q16, q18, [x0, #192]
	stp	q3, q4, [x0, #128]
	stp	q20, q24, [x0, #64]
	stp	q30, q17, [x0, #32]
	stp	q23, q21, [x0, #224]
	stp	q6, q7, [x0, #160]
	stp	q22, q19, [x0, #96]
	ldr	q0, [x1, x9, lsl #4]
	ldrb	w9, [x8, #3]
	add	v0.4s, v0.4s, v28.4s
	str	q0, [x0]
	ldr	q1, [x1, x9, lsl #4]
	ldrb	w9, [x8, #5]
	add	v27.4s, v0.4s, v20.4s
	eor	v0.16b, v27.16b, v16.16b
	add	v1.4s, v1.4s, v29.4s
	str	q1, [x0, #16]
	ldr	q2, [x1, x9, lsl #4]
	add	v16.4s, v1.4s, v24.4s
	ldrb	w9, [x8, #7]
	add	v25.4s, v2.4s, v30.4s
	ushr	v2.4s, v0.4s, #8
	shl	v0.4s, v0.4s, #24
	orr	v5.16b, v0.16b, v2.16b
	add	v2.4s, v5.4s, v3.4s
	eor	v0.16b, v2.16b, v20.16b
	ushr	v1.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	eor	v3.16b, v16.16b, v18.16b
	orr	v0.16b, v0.16b, v1.16b
	ushr	v1.4s, v3.4s, #8
	shl	v3.4s, v3.4s, #24
	orr	v3.16b, v3.16b, v1.16b
	str	q25, [x0, #32]
	add	v1.4s, v3.4s, v4.4s
	ldr	q26, [x1, x9, lsl #4]
	eor	v4.16b, v1.16b, v24.16b
	add	v18.4s, v25.4s, v22.4s
	ushr	v20.4s, v4.4s, #7
	shl	v4.4s, v4.4s, #25
	eor	v23.16b, v18.16b, v23.16b
	orr	v20.16b, v4.16b, v20.16b
	ushr	v4.4s, v23.4s, #8
	shl	v23.4s, v23.4s, #24
	orr	v4.16b, v23.16b, v4.16b
	add	v23.4s, v4.4s, v6.4s
	add	v6.4s, v26.4s, v17.4s
	eor	v17.16b, v23.16b, v22.16b
	add	v6.4s, v6.4s, v19.4s
	ushr	v22.4s, v17.4s, #7
	shl	v17.4s, v17.4s, #25
	eor	v21.16b, v6.16b, v21.16b
	orr	v17.16b, v17.16b, v22.16b
	ushr	v22.4s, v21.4s, #8
	shl	v21.4s, v21.4s, #24
	orr	v21.16b, v21.16b, v22.16b
	add	v7.4s, v21.4s, v7.4s
	ldrb	w9, [x8, #8]
	eor	v19.16b, v7.16b, v19.16b
	ushr	v22.4s, v19.4s, #7
	shl	v19.4s, v19.4s, #25
	orr	v19.16b, v19.16b, v22.16b
	stp	q27, q16, [x0]
	stp	q5, q3, [x0, #192]
	stp	q2, q1, [x0, #128]
	stp	q0, q20, [x0, #64]
	stp	q18, q6, [x0, #32]
	stp	q4, q21, [x0, #224]
	stp	q23, q7, [x0, #160]
	stp	q17, q19, [x0, #96]
	ldr	q22, [x1, x9, lsl #4]
	ldrb	w9, [x8, #10]
	add	v22.4s, v22.4s, v27.4s
	str	q22, [x0]
	ldr	q24, [x1, x9, lsl #4]
	ldrb	w9, [x8, #12]
	add	v22.4s, v22.4s, v20.4s
	eor	v21.16b, v22.16b, v21.16b
	add	v16.4s, v24.4s, v16.4s
	ushr	v25.4s, v21.4s, #16
	shl	v21.4s, v21.4s, #16
	str	q16, [x0, #16]
	orr	v21.16b, v21.16b, v25.16b
	ldr	q24, [x1, x9, lsl #4]
	add	v23.4s, v21.4s, v23.4s
	eor	v20.16b, v23.16b, v20.16b
	add	v16.4s, v16.4s, v17.4s
	ldrb	w9, [x8, #14]
	ushr	v25.4s, v20.4s, #12
	shl	v20.4s, v20.4s, #20
	eor	v5.16b, v16.16b, v5.16b
	orr	v20.16b, v20.16b, v25.16b
	ushr	v25.4s, v5.4s, #16
	shl	v5.4s, v5.4s, #16
	add	v18.4s, v24.4s, v18.4s
	orr	v5.16b, v5.16b, v25.16b
	str	q18, [x0, #32]
	add	v7.4s, v5.4s, v7.4s
	ldr	q24, [x1, x9, lsl #4]
	eor	v17.16b, v7.16b, v17.16b
	add	v18.4s, v18.4s, v19.4s
	ushr	v25.4s, v17.4s, #12
	shl	v17.4s, v17.4s, #20
	eor	v3.16b, v18.16b, v3.16b
	orr	v17.16b, v17.16b, v25.16b
	ushr	v25.4s, v3.4s, #16
	shl	v3.4s, v3.4s, #16
	orr	v3.16b, v3.16b, v25.16b
	add	v2.4s, v3.4s, v2.4s
	add	v6.4s, v24.4s, v6.4s
	eor	v19.16b, v2.16b, v19.16b
	add	v6.4s, v6.4s, v0.4s
	ushr	v24.4s, v19.4s, #12
	shl	v19.4s, v19.4s, #20
	eor	v4.16b, v6.16b, v4.16b
	orr	v19.16b, v19.16b, v24.16b
	ushr	v24.4s, v4.4s, #16
	shl	v4.4s, v4.4s, #16
	orr	v4.16b, v4.16b, v24.16b
	add	v1.4s, v4.4s, v1.4s
	ldrb	w9, [x8, #9]
	eor	v0.16b, v1.16b, v0.16b
	ushr	v24.4s, v0.4s, #12
	shl	v0.4s, v0.4s, #20
	orr	v0.16b, v0.16b, v24.16b
	stp	q22, q16, [x0]
	stp	q23, q7, [x0, #160]
	stp	q5, q3, [x0, #192]
	stp	q18, q6, [x0, #32]
	stp	q17, q19, [x0, #96]
	stp	q4, q21, [x0, #224]
	stp	q2, q1, [x0, #128]
	stp	q0, q20, [x0, #64]
	ldr	q24, [x1, x9, lsl #4]
	ldrb	w9, [x8, #11]
	add	v22.4s, v24.4s, v22.4s
	str	q22, [x0]
	ldr	q24, [x1, x9, lsl #4]
	ldrb	w9, [x8, #13]
	add	v22.4s, v22.4s, v20.4s
	eor	v21.16b, v22.16b, v21.16b
	add	v16.4s, v24.4s, v16.4s
	ushr	v25.4s, v21.4s, #8
	shl	v21.4s, v21.4s, #24
	str	q16, [x0, #16]
	orr	v21.16b, v21.16b, v25.16b
	ldr	q25, [x1, x9, lsl #4]
	ldrb	w8, [x8, #15]
	add	v23.4s, v21.4s, v23.4s
	eor	v20.16b, v23.16b, v20.16b
	add	v18.4s, v25.4s, v18.4s
	str	q18, [x0, #32]
	add	v16.4s, v16.4s, v17.4s
	add	v18.4s, v18.4s, v19.4s
	ldr	q25, [x1, x8, lsl #4]
	ushr	v24.4s, v20.4s, #7
	shl	v20.4s, v20.4s, #25
	eor	v5.16b, v16.16b, v5.16b
	eor	v3.16b, v18.16b, v3.16b
	orr	v20.16b, v20.16b, v24.16b
	ushr	v24.4s, v5.4s, #8
	shl	v5.4s, v5.4s, #24
	stp	q22, q16, [x0]
	ushr	v16.4s, v3.4s, #8
	shl	v3.4s, v3.4s, #24
	orr	v5.16b, v5.16b, v24.16b
	orr	v3.16b, v3.16b, v16.16b
	stp	q5, q3, [x0, #192]
	add	v2.4s, v3.4s, v2.4s
	add	v3.4s, v25.4s, v6.4s
	add	v7.4s, v5.4s, v7.4s
	eor	v5.16b, v2.16b, v19.16b
	add	v3.4s, v3.4s, v0.4s
	ushr	v6.4s, v5.4s, #7
	shl	v5.4s, v5.4s, #25
	stp	q18, q3, [x0, #32]
	eor	v3.16b, v3.16b, v4.16b
	orr	v4.16b, v5.16b, v6.16b
	ushr	v5.4s, v3.4s, #8
	shl	v3.4s, v3.4s, #24
	orr	v3.16b, v3.16b, v5.16b
	add	v1.4s, v3.4s, v1.4s
	eor	v17.16b, v7.16b, v17.16b
	eor	v0.16b, v1.16b, v0.16b
	ushr	v24.4s, v17.4s, #7
	shl	v17.4s, v17.4s, #25
	stp	q2, q1, [x0, #128]
	ushr	v1.4s, v0.4s, #7
	shl	v0.4s, v0.4s, #25
	stp	q23, q7, [x0, #160]
	orr	v7.16b, v17.16b, v24.16b
	orr	v0.16b, v0.16b, v1.16b
	stp	q7, q4, [x0, #96]
	stp	q3, q21, [x0, #224]
	stp	q0, q20, [x0, #64]
	ret
.Lfunc_end4:
	.size	round_fn4, .Lfunc_end4-round_fn4
                                        // -- End function

	.type	MSG_SCHEDULE,@object            // @MSG_SCHEDULE
MSG_SCHEDULE:
	.ascii	"\000\001\002\003\004\005\006\007\b\t\n\013\f\r\016\017"
	.ascii	"\002\006\003\n\007\000\004\r\001\013\f\005\t\016\017\b"
	.ascii	"\003\004\n\f\r\002\007\016\006\005\t\000\013\017\b\001"
	.ascii	"\n\007\f\t\016\003\r\017\004\000\013\002\005\b\001\006"
	.ascii	"\f\r\t\013\017\n\016\b\007\002\005\003\000\001\006\004"
	.ascii	"\t\016\013\005\b\f\017\001\r\003\000\n\002\006\004\007"
	.ascii	"\013\017\005\000\001\t\b\006\016\n\002\f\003\004\007\r"
	.size	MSG_SCHEDULE, 112

	.ident	"Debian clang version 11.0.1-2"
	.section	".note.GNU-stack","",@progbits
#endif /* __AARCH64__ */
