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
 * Copyright (c) 2022 Tino Reichardt <milky-zfs@mcmilk.de>
 *
 * This is converted assembly: SSE2 -> POWER8 PPC64 Little Endian
 * Used tools: SIMDe https://github.com/simd-everywhere/simde
 */

#if (defined(__PPC64__) && defined(__LITTLE_ENDIAN__))
	.text
	.abiversion 2
	.section	.rodata.cst16,"aM",@progbits,16
	.p2align	4
.LCPI0_0:
	.byte	29
	.byte	28
	.byte	31
	.byte	30
	.byte	25
	.byte	24
	.byte	27
	.byte	26
	.byte	21
	.byte	20
	.byte	23
	.byte	22
	.byte	17
	.byte	16
	.byte	19
	.byte	18
.LCPI0_1:
	.long	1779033703
	.long	3144134277
	.long	1013904242
	.long	2773480762
.LCPI0_2:
	.byte	27
	.byte	26
	.byte	25
	.byte	24
	.byte	19
	.byte	18
	.byte	17
	.byte	16
	.byte	11
	.byte	10
	.byte	9
	.byte	8
	.byte	3
	.byte	2
	.byte	1
	.byte	0
.LCPI0_3:
	.byte	19
	.byte	18
	.byte	17
	.byte	16
	.byte	11
	.byte	10
	.byte	9
	.byte	8
	.byte	3
	.byte	2
	.byte	1
	.byte	0
	.byte	27
	.byte	26
	.byte	25
	.byte	24
.LCPI0_4:
	.byte	23
	.byte	22
	.byte	21
	.byte	20
	.byte	27
	.byte	26
	.byte	25
	.byte	24
	.byte	11
	.byte	10
	.byte	9
	.byte	8
	.byte	3
	.byte	2
	.byte	1
	.byte	0
.LCPI0_5:
	.byte	23
	.byte	22
	.byte	21
	.byte	20
	.byte	23
	.byte	22
	.byte	21
	.byte	20
	.byte	3
	.byte	2
	.byte	1
	.byte	0
	.byte	3
	.byte	2
	.byte	1
	.byte	0
.LCPI0_6:
	.short	1
	.short	2
	.short	4
	.short	8
	.short	16
	.short	32
	.short	64
	.short	128
.LCPI0_7:
	.short	0
	.short	0
	.short	4
	.short	8
	.short	0
	.short	0
	.short	64
	.short	128
.LCPI0_8:
	.byte	19
	.byte	18
	.byte	17
	.byte	16
	.byte	19
	.byte	18
	.byte	17
	.byte	16
	.byte	31
	.byte	30
	.byte	29
	.byte	28
	.byte	31
	.byte	30
	.byte	29
	.byte	28
.LCPI0_9:
	.short	0
	.short	0
	.short	0
	.short	0
	.short	0
	.short	0
	.short	64
	.short	128
.LCPI0_10:
	.byte	31
	.byte	30
	.byte	29
	.byte	28
	.byte	7
	.byte	6
	.byte	5
	.byte	4
	.byte	3
	.byte	2
	.byte	1
	.byte	0
	.byte	27
	.byte	26
	.byte	25
	.byte	24
.LCPI0_11:
	.byte	31
	.byte	30
	.byte	29
	.byte	28
	.byte	23
	.byte	22
	.byte	21
	.byte	20
	.byte	19
	.byte	18
	.byte	17
	.byte	16
	.byte	27
	.byte	26
	.byte	25
	.byte	24
.LCPI0_12:
	.byte	27
	.byte	26
	.byte	25
	.byte	24
	.byte	11
	.byte	10
	.byte	9
	.byte	8
	.byte	15
	.byte	14
	.byte	13
	.byte	12
	.byte	31
	.byte	30
	.byte	29
	.byte	28
.LCPI0_13:
	.byte	31
	.byte	30
	.byte	29
	.byte	28
	.byte	15
	.byte	14
	.byte	13
	.byte	12
	.byte	11
	.byte	10
	.byte	9
	.byte	8
	.byte	27
	.byte	26
	.byte	25
	.byte	24
.LCPI0_14:
	.byte	27
	.byte	26
	.byte	25
	.byte	24
	.byte	11
	.byte	10
	.byte	9
	.byte	8
	.byte	3
	.byte	2
	.byte	1
	.byte	0
	.byte	23
	.byte	22
	.byte	21
	.byte	20
	.text
	.globl	zfs_blake3_compress_in_place_sse2
	.p2align	2
	.type	zfs_blake3_compress_in_place_sse2,@function
zfs_blake3_compress_in_place_sse2:
.Lfunc_begin0:
	.cfi_startproc
.Lfunc_gep0:
	addis 2, 12, .TOC.-.Lfunc_gep0@ha
	addi 2, 2, .TOC.-.Lfunc_gep0@l
.Lfunc_lep0:
	.localentry	zfs_blake3_compress_in_place_sse2, .Lfunc_lep0-.Lfunc_gep0
	li 8, -64
	mtvsrd 35, 5
	li 5, 16
	lfdx 0, 0, 4
	vspltisw 12, 9
	stxvd2x 60, 1, 8
	li 8, -48
	mtvsrd 36, 7
	lfd 2, 16(4)
	stxvd2x 61, 1, 8
	li 8, -32
	lfd 1, 8(4)
	mtvsrwz 37, 6
	rldicl 6, 6, 32, 32
	addis 7, 2, .LCPI0_2@toc@ha
	stxvd2x 62, 1, 8
	li 8, -16
	addi 7, 7, .LCPI0_2@toc@l
	stxvd2x 63, 1, 8
	li 8, 0
	lvx 9, 0, 7
	li 7, 48
	mtvsrd 34, 8
	xxmrghd	32, 1, 0
	lxvd2x 0, 0, 3
	lxvd2x 1, 3, 5
	lfd 3, 24(4)
	addis 8, 2, .LCPI0_5@toc@ha
	vmrghb 3, 2, 3
	addi 8, 8, .LCPI0_5@toc@l
	vmrghb 4, 2, 4
	vspltb 2, 2, 7
	xxmrghd	33, 3, 2
	vpkudum 7, 1, 0
	vmrglh 3, 2, 3
	vmrglh 2, 2, 4
	mtvsrwz 36, 6
	addis 6, 2, .LCPI0_0@toc@ha
	addi 6, 6, .LCPI0_0@toc@l
	vperm 10, 1, 0, 9
	vmrghw 4, 4, 5
	xxswapd	37, 1
	lxvd2x 1, 4, 7
	addis 7, 2, .LCPI0_8@toc@ha
	addi 7, 7, .LCPI0_8@toc@l
	vmrglw 2, 2, 3
	xxswapd	35, 0
	xxswapd	41, 1
	xxspltd	62, 42, 1
	vadduwm 3, 7, 3
	vadduwm 6, 3, 5
	xxmrgld	36, 34, 36
	lvx 2, 0, 6
	addis 6, 2, .LCPI0_1@toc@ha
	addi 6, 6, .LCPI0_1@toc@l
	xxlxor 35, 38, 36
	lvx 4, 0, 6
	li 6, 32
	lxvd2x 0, 4, 6
	addis 4, 2, .LCPI0_3@toc@ha
	addis 6, 2, .LCPI0_7@toc@ha
	vperm 8, 3, 3, 2
	vspltisw 3, 10
	addi 4, 4, .LCPI0_3@toc@l
	addi 6, 6, .LCPI0_7@toc@l
	vadduwm 3, 3, 3
	vadduwm 11, 8, 4
	xxlxor 36, 43, 37
	vadduwm 5, 6, 10
	vrlw 0, 4, 3
	vspltisw 4, 12
	vadduwm 4, 4, 4
	vadduwm 1, 0, 5
	xxlxor 37, 33, 40
	xxswapd	40, 0
	vrlw 6, 5, 4
	vspltisw 5, -16
	vpkudum 13, 9, 8
	vsubuwm 5, 12, 5
	lvx 12, 0, 4
	addis 4, 2, .LCPI0_4@toc@ha
	addi 4, 4, .LCPI0_4@toc@l
	vadduwm 11, 6, 11
	xxswapd	0, 38
	vadduwm 1, 1, 13
	xxsldwi 50, 45, 45, 1
	xxlxor 32, 43, 32
	xxsldwi 43, 43, 43, 3
	xxsldwi 33, 33, 33, 1
	vperm 12, 8, 9, 12
	vrlw 0, 0, 5
	vadduwm 1, 0, 1
	xxlxor 38, 33, 0
	vadduwm 1, 1, 12
	vperm 6, 6, 6, 2
	vadduwm 15, 6, 11
	lvx 11, 0, 4
	addis 4, 2, .LCPI0_6@toc@ha
	addi 4, 4, .LCPI0_6@toc@l
	xxlxor 32, 47, 32
	lvx 17, 0, 4
	addis 4, 2, .LCPI0_9@toc@ha
	vperm 14, 10, 7, 11
	addi 4, 4, .LCPI0_9@toc@l
	vrlw 0, 0, 3
	vadduwm 1, 0, 1
	xxlxor 38, 33, 38
	vrlw 6, 6, 4
	vadduwm 8, 6, 15
	xxswapd	0, 38
	lvx 6, 0, 8
	xxlxor 32, 40, 32
	xxsldwi 40, 40, 40, 1
	vperm 13, 12, 18, 6
	vrlw 9, 0, 5
	vadduwm 0, 1, 14
	lvx 1, 0, 7
	xxsldwi 46, 46, 46, 3
	xxsldwi 32, 32, 32, 3
	vperm 7, 7, 7, 1
	vadduwm 15, 9, 0
	xxlxor 32, 47, 0
	vperm 16, 0, 0, 2
	lvx 0, 0, 6
	addis 6, 2, .LCPI0_10@toc@ha
	vcmpequh 0, 0, 17
	vadduwm 19, 16, 8
	xxlxor 40, 51, 41
	xxsel 45, 39, 45, 32
	vrlw 31, 8, 3
	lvx 8, 0, 4
	addis 4, 2, .LCPI0_11@toc@ha
	addi 4, 4, .LCPI0_11@toc@l
	vcmpequh 7, 8, 17
	vadduwm 8, 15, 13
	vadduwm 15, 31, 8
	lvx 8, 0, 4
	addi 4, 6, .LCPI0_10@toc@l
	lvx 17, 0, 4
	addis 4, 2, .LCPI0_12@toc@ha
	xxlxor 41, 47, 48
	xxsldwi 47, 47, 47, 1
	addi 4, 4, .LCPI0_12@toc@l
	xxlnor 48, 39, 39
	vrlw 29, 9, 4
	vperm 9, 16, 16, 8
	xxland 48, 50, 39
	vperm 17, 30, 12, 17
	vperm 16, 16, 16, 8
	vmrghw 12, 12, 10
	lvx 10, 0, 4
	addis 4, 2, .LCPI0_13@toc@ha
	vadduwm 19, 29, 19
	addi 4, 4, .LCPI0_13@toc@l
	xxlxor 63, 51, 63
	xxsldwi 51, 51, 51, 3
	xxland 0, 49, 41
	vrlw 17, 31, 5
	xxlor 48, 0, 48
	xxswapd	0, 61
	vperm 18, 12, 18, 10
	vadduwm 15, 15, 16
	xxland 60, 48, 39
	vadduwm 15, 17, 15
	vperm 28, 28, 28, 8
	xxlxor 63, 47, 0
	vadduwm 15, 15, 18
	vperm 31, 31, 31, 2
	vperm 30, 18, 16, 6
	vadduwm 19, 31, 19
	xxlxor 44, 51, 49
	vrlw 12, 12, 3
	vadduwm 15, 12, 15
	xxlxor 49, 47, 63
	vperm 31, 13, 14, 11
	vrlw 17, 17, 4
	vperm 14, 14, 14, 1
	vadduwm 15, 15, 31
	vadduwm 19, 17, 19
	xxswapd	0, 49
	xxsldwi 47, 47, 47, 3
	xxsel 46, 46, 62, 32
	xxlxor 44, 51, 44
	xxsldwi 51, 51, 51, 1
	vrlw 12, 12, 5
	vadduwm 15, 12, 15
	xxlxor 49, 47, 0
	vperm 17, 17, 17, 2
	vadduwm 19, 17, 19
	xxlxor 44, 51, 44
	vrlw 29, 12, 3
	vadduwm 12, 15, 14
	vadduwm 15, 29, 12
	lvx 12, 0, 4
	addis 4, 2, .LCPI0_14@toc@ha
	addi 4, 4, .LCPI0_14@toc@l
	xxlxor 49, 47, 49
	xxsldwi 47, 47, 47, 1
	vperm 30, 13, 18, 12
	vrlw 17, 17, 4
	vmrghw 13, 18, 13
	xxland 0, 62, 41
	vadduwm 19, 17, 19
	vperm 16, 13, 16, 10
	xxlxor 61, 51, 61
	xxsldwi 50, 51, 51, 3
	xxsldwi 51, 63, 63, 3
	vrlw 30, 29, 5
	xxlor 61, 60, 0
	xxswapd	0, 49
	vperm 31, 14, 19, 11
	vadduwm 15, 15, 29
	vperm 19, 19, 19, 1
	vadduwm 15, 30, 15
	xxlxor 49, 47, 0
	vadduwm 15, 15, 16
	vperm 17, 17, 17, 2
	vadduwm 18, 17, 18
	xxlxor 45, 50, 62
	vperm 30, 16, 29, 6
	vrlw 13, 13, 3
	vadduwm 15, 13, 15
	xxlxor 49, 47, 49
	vadduwm 15, 15, 31
	xxsldwi 63, 63, 63, 3
	vrlw 17, 17, 4
	xxsldwi 47, 47, 47, 3
	vadduwm 18, 17, 18
	xxswapd	0, 49
	xxlxor 45, 50, 45
	xxsldwi 50, 50, 50, 1
	vrlw 13, 13, 5
	vadduwm 15, 13, 15
	xxlxor 49, 47, 0
	vperm 17, 17, 17, 2
	vadduwm 18, 17, 18
	xxlxor 45, 50, 45
	vrlw 28, 13, 3
	xxsel 45, 51, 62, 32
	xxland 51, 61, 39
	vperm 30, 14, 16, 12
	vadduwm 15, 15, 13
	vperm 19, 19, 19, 8
	vmrghw 14, 16, 14
	vadduwm 15, 28, 15
	xxlxor 49, 47, 49
	xxsldwi 47, 47, 47, 1
	xxland 0, 62, 41
	vrlw 17, 17, 4
	xxlor 51, 51, 0
	vadduwm 15, 15, 19
	vadduwm 18, 17, 18
	xxswapd	0, 49
	xxlxor 60, 50, 60
	xxsldwi 48, 50, 50, 3
	vperm 18, 14, 29, 10
	vrlw 30, 28, 5
	vperm 29, 18, 19, 6
	vadduwm 15, 30, 15
	xxlxor 49, 47, 0
	vadduwm 15, 15, 18
	vperm 17, 17, 17, 2
	vadduwm 16, 17, 16
	xxlxor 46, 48, 62
	vperm 30, 13, 31, 11
	vrlw 14, 14, 3
	vperm 31, 31, 31, 1
	vadduwm 15, 14, 15
	xxlxor 49, 47, 49
	vadduwm 15, 15, 30
	vrlw 17, 17, 4
	xxsldwi 47, 47, 47, 3
	vadduwm 16, 17, 16
	xxswapd	0, 49
	xxlxor 46, 48, 46
	xxsldwi 48, 48, 48, 1
	vrlw 14, 14, 5
	vadduwm 15, 14, 15
	xxlxor 49, 47, 0
	vperm 17, 17, 17, 2
	vadduwm 16, 17, 16
	xxlxor 46, 48, 46
	vrlw 28, 14, 3
	xxsel 46, 63, 61, 32
	xxland 63, 51, 39
	vperm 29, 13, 18, 12
	vadduwm 15, 15, 14
	vperm 31, 31, 31, 8
	vmrghw 13, 18, 13
	vadduwm 15, 28, 15
	xxlxor 49, 47, 49
	xxsldwi 47, 47, 47, 1
	xxland 0, 61, 41
	vrlw 17, 17, 4
	xxlor 63, 63, 0
	vperm 13, 13, 19, 10
	xxsldwi 51, 62, 62, 3
	vadduwm 15, 15, 31
	vperm 30, 14, 19, 11
	vadduwm 16, 17, 16
	xxswapd	0, 49
	xxlxor 60, 48, 60
	xxsldwi 48, 48, 48, 3
	vrlw 29, 28, 5
	vadduwm 15, 29, 15
	xxlxor 49, 47, 0
	vadduwm 15, 15, 13
	vperm 17, 17, 17, 2
	vadduwm 16, 17, 16
	xxlxor 50, 48, 61
	vrlw 18, 18, 3
	vadduwm 15, 18, 15
	xxlxor 49, 47, 49
	vadduwm 15, 15, 30
	vrlw 17, 17, 4
	xxsldwi 47, 47, 47, 3
	vadduwm 11, 17, 16
	xxswapd	0, 49
	xxlxor 48, 43, 50
	xxsldwi 43, 43, 43, 1
	vperm 18, 19, 19, 1
	vrlw 16, 16, 5
	vperm 19, 13, 31, 6
	vadduwm 15, 16, 15
	xxlxor 49, 47, 0
	vperm 17, 17, 17, 2
	vadduwm 29, 17, 11
	xxlxor 43, 61, 48
	vrlw 16, 11, 3
	xxsel 43, 50, 51, 32
	xxland 50, 63, 39
	vperm 19, 14, 13, 12
	vadduwm 15, 15, 11
	vperm 18, 18, 18, 8
	vmrghw 13, 13, 14
	vadduwm 15, 16, 15
	xxlxor 49, 47, 49
	xxsldwi 47, 47, 47, 1
	xxland 0, 51, 41
	lvx 19, 0, 4
	vrlw 17, 17, 4
	xxlor 50, 50, 0
	vperm 13, 13, 31, 10
	xxsldwi 63, 62, 62, 3
	vadduwm 15, 15, 18
	vperm 19, 11, 31, 19
	vadduwm 29, 17, 29
	xxswapd	0, 49
	vperm 1, 31, 31, 1
	xxlxor 48, 61, 48
	xxsldwi 46, 61, 61, 3
	vperm 6, 13, 18, 6
	vrlw 16, 16, 5
	xxsel 32, 33, 38, 32
	xxland 38, 50, 39
	vadduwm 15, 16, 15
	vperm 7, 11, 13, 12
	xxlxor 49, 47, 0
	vadduwm 15, 15, 13
	vperm 17, 17, 17, 2
	vperm 6, 6, 6, 8
	vadduwm 14, 17, 14
	xxlxor 48, 46, 48
	vrlw 16, 16, 3
	vadduwm 15, 16, 15
	xxlxor 49, 47, 49
	xxsldwi 47, 47, 47, 3
	vrlw 17, 17, 4
	vadduwm 15, 15, 19
	vadduwm 14, 17, 14
	xxswapd	0, 49
	xxlxor 48, 46, 48
	xxsldwi 46, 46, 46, 1
	vrlw 16, 16, 5
	vadduwm 15, 16, 15
	xxlxor 49, 47, 0
	vadduwm 0, 15, 0
	vperm 17, 17, 17, 2
	xxland 0, 39, 41
	xxlor 38, 38, 0
	vadduwm 14, 17, 14
	xxlxor 48, 46, 48
	vrlw 16, 16, 3
	vadduwm 0, 16, 0
	xxlxor 33, 32, 49
	xxsldwi 32, 32, 32, 1
	vrlw 1, 1, 4
	vadduwm 0, 0, 6
	vadduwm 8, 1, 14
	xxswapd	0, 33
	xxlxor 44, 40, 48
	xxsldwi 38, 40, 40, 3
	vrlw 7, 12, 5
	vadduwm 0, 7, 0
	xxlxor 33, 32, 0
	vperm 2, 1, 1, 2
	vmrghw 1, 13, 11
	vadduwm 6, 2, 6
	vperm 1, 1, 18, 10
	xxlxor 39, 38, 39
	vrlw 3, 7, 3
	vadduwm 0, 0, 1
	vadduwm 0, 3, 0
	xxlxor 34, 32, 34
	xxsldwi 0, 32, 32, 3
	vrlw 2, 2, 4
	vadduwm 4, 2, 6
	xxswapd	2, 34
	xxlxor 35, 36, 35
	xxsldwi 1, 36, 36, 1
	vrlw 3, 3, 5
	xxlxor 0, 1, 0
	xxswapd	0, 0
	xxlxor 1, 35, 2
	stxvd2x 0, 0, 3
	xxswapd	1, 1
	stxvd2x 1, 3, 5
	li 3, -16
	lxvd2x 63, 1, 3
	li 3, -32
	lxvd2x 62, 1, 3
	li 3, -48
	lxvd2x 61, 1, 3
	li 3, -64
	lxvd2x 60, 1, 3
	blr
	.long	0
	.quad	0
.Lfunc_end0:
	.size	zfs_blake3_compress_in_place_sse2, .Lfunc_end0-.Lfunc_begin0
	.cfi_endproc

	.section	.rodata.cst16,"aM",@progbits,16
	.p2align	4
.LCPI1_0:
	.byte	29
	.byte	28
	.byte	31
	.byte	30
	.byte	25
	.byte	24
	.byte	27
	.byte	26
	.byte	21
	.byte	20
	.byte	23
	.byte	22
	.byte	17
	.byte	16
	.byte	19
	.byte	18
.LCPI1_1:
	.long	1779033703
	.long	3144134277
	.long	1013904242
	.long	2773480762
.LCPI1_2:
	.byte	27
	.byte	26
	.byte	25
	.byte	24
	.byte	19
	.byte	18
	.byte	17
	.byte	16
	.byte	11
	.byte	10
	.byte	9
	.byte	8
	.byte	3
	.byte	2
	.byte	1
	.byte	0
.LCPI1_3:
	.byte	19
	.byte	18
	.byte	17
	.byte	16
	.byte	11
	.byte	10
	.byte	9
	.byte	8
	.byte	3
	.byte	2
	.byte	1
	.byte	0
	.byte	27
	.byte	26
	.byte	25
	.byte	24
.LCPI1_4:
	.byte	23
	.byte	22
	.byte	21
	.byte	20
	.byte	27
	.byte	26
	.byte	25
	.byte	24
	.byte	11
	.byte	10
	.byte	9
	.byte	8
	.byte	3
	.byte	2
	.byte	1
	.byte	0
.LCPI1_5:
	.byte	23
	.byte	22
	.byte	21
	.byte	20
	.byte	23
	.byte	22
	.byte	21
	.byte	20
	.byte	3
	.byte	2
	.byte	1
	.byte	0
	.byte	3
	.byte	2
	.byte	1
	.byte	0
.LCPI1_6:
	.short	1
	.short	2
	.short	4
	.short	8
	.short	16
	.short	32
	.short	64
	.short	128
.LCPI1_7:
	.short	0
	.short	0
	.short	4
	.short	8
	.short	0
	.short	0
	.short	64
	.short	128
.LCPI1_8:
	.byte	19
	.byte	18
	.byte	17
	.byte	16
	.byte	19
	.byte	18
	.byte	17
	.byte	16
	.byte	31
	.byte	30
	.byte	29
	.byte	28
	.byte	31
	.byte	30
	.byte	29
	.byte	28
.LCPI1_9:
	.short	0
	.short	0
	.short	0
	.short	0
	.short	0
	.short	0
	.short	64
	.short	128
.LCPI1_10:
	.byte	31
	.byte	30
	.byte	29
	.byte	28
	.byte	7
	.byte	6
	.byte	5
	.byte	4
	.byte	3
	.byte	2
	.byte	1
	.byte	0
	.byte	27
	.byte	26
	.byte	25
	.byte	24
.LCPI1_11:
	.byte	31
	.byte	30
	.byte	29
	.byte	28
	.byte	23
	.byte	22
	.byte	21
	.byte	20
	.byte	19
	.byte	18
	.byte	17
	.byte	16
	.byte	27
	.byte	26
	.byte	25
	.byte	24
.LCPI1_12:
	.byte	27
	.byte	26
	.byte	25
	.byte	24
	.byte	11
	.byte	10
	.byte	9
	.byte	8
	.byte	15
	.byte	14
	.byte	13
	.byte	12
	.byte	31
	.byte	30
	.byte	29
	.byte	28
.LCPI1_13:
	.byte	31
	.byte	30
	.byte	29
	.byte	28
	.byte	15
	.byte	14
	.byte	13
	.byte	12
	.byte	11
	.byte	10
	.byte	9
	.byte	8
	.byte	27
	.byte	26
	.byte	25
	.byte	24
.LCPI1_14:
	.byte	27
	.byte	26
	.byte	25
	.byte	24
	.byte	11
	.byte	10
	.byte	9
	.byte	8
	.byte	3
	.byte	2
	.byte	1
	.byte	0
	.byte	23
	.byte	22
	.byte	21
	.byte	20
	.text
	.globl	zfs_blake3_compress_xof_sse2
	.p2align	2
	.type	zfs_blake3_compress_xof_sse2,@function
zfs_blake3_compress_xof_sse2:
.Lfunc_begin1:
	.cfi_startproc
.Lfunc_gep1:
	addis 2, 12, .TOC.-.Lfunc_gep1@ha
	addi 2, 2, .TOC.-.Lfunc_gep1@l
.Lfunc_lep1:
	.localentry	zfs_blake3_compress_xof_sse2, .Lfunc_lep1-.Lfunc_gep1
	li 9, -80
	mtvsrd 35, 5
	li 5, 16
	lfdx 0, 0, 4
	addis 10, 2, .LCPI1_2@toc@ha
	vspltisw 12, 9
	std 30, -16(1)
	addis 12, 2, .LCPI1_8@toc@ha
	addis 30, 2, .LCPI1_5@toc@ha
	addis 11, 2, .LCPI1_7@toc@ha
	stxvd2x 60, 1, 9
	li 9, -64
	mtvsrd 36, 7
	lfd 2, 16(4)
	addi 10, 10, .LCPI1_2@toc@l
	addi 12, 12, .LCPI1_8@toc@l
	addi 11, 11, .LCPI1_7@toc@l
	stxvd2x 61, 1, 9
	li 9, -48
	lfd 3, 24(4)
	mtvsrwz 37, 6
	rldicl 6, 6, 32, 32
	lvx 9, 0, 10
	stxvd2x 62, 1, 9
	li 9, -32
	li 10, 32
	stxvd2x 63, 1, 9
	li 9, 0
	mtvsrd 34, 9
	xxmrghd	33, 3, 2
	lfd 1, 8(4)
	vmrghb 3, 2, 3
	vmrghb 4, 2, 4
	vspltb 2, 2, 7
	xxmrghd	32, 1, 0
	lxvd2x 0, 0, 3
	lxvd2x 1, 3, 5
	vpkudum 7, 1, 0
	vmrglh 3, 2, 3
	vmrglh 2, 2, 4
	mtvsrwz 36, 6
	addis 6, 2, .LCPI1_0@toc@ha
	addi 6, 6, .LCPI1_0@toc@l
	vperm 10, 1, 0, 9
	vmrghw 4, 4, 5
	xxswapd	37, 1
	vmrglw 2, 2, 3
	xxswapd	35, 0
	lxvd2x 0, 4, 10
	xxspltd	62, 42, 1
	vadduwm 3, 7, 3
	vadduwm 6, 3, 5
	xxmrgld	36, 34, 36
	lvx 2, 0, 6
	addis 6, 2, .LCPI1_1@toc@ha
	addi 6, 6, .LCPI1_1@toc@l
	xxlxor 35, 38, 36
	lvx 4, 0, 6
	li 6, 48
	lxvd2x 1, 4, 6
	addis 4, 2, .LCPI1_3@toc@ha
	vperm 8, 3, 3, 2
	vspltisw 3, 10
	addi 4, 4, .LCPI1_3@toc@l
	xxswapd	41, 1
	vadduwm 3, 3, 3
	vadduwm 11, 8, 4
	xxlxor 36, 43, 37
	vadduwm 5, 6, 10
	vrlw 0, 4, 3
	vspltisw 4, 12
	vadduwm 4, 4, 4
	vadduwm 1, 0, 5
	xxlxor 37, 33, 40
	xxswapd	40, 0
	vrlw 6, 5, 4
	vspltisw 5, -16
	vpkudum 13, 9, 8
	vsubuwm 5, 12, 5
	lvx 12, 0, 4
	addis 4, 2, .LCPI1_4@toc@ha
	addi 4, 4, .LCPI1_4@toc@l
	vadduwm 11, 6, 11
	xxswapd	0, 38
	vadduwm 1, 1, 13
	xxsldwi 50, 45, 45, 1
	xxlxor 32, 43, 32
	xxsldwi 43, 43, 43, 3
	xxsldwi 33, 33, 33, 1
	vperm 12, 8, 9, 12
	vrlw 0, 0, 5
	vadduwm 1, 0, 1
	xxlxor 38, 33, 0
	vadduwm 1, 1, 12
	vperm 6, 6, 6, 2
	vadduwm 15, 6, 11
	lvx 11, 0, 4
	addis 4, 2, .LCPI1_6@toc@ha
	addi 4, 4, .LCPI1_6@toc@l
	xxlxor 32, 47, 32
	lvx 17, 0, 4
	addi 4, 30, .LCPI1_5@toc@l
	vperm 14, 10, 7, 11
	vrlw 0, 0, 3
	vadduwm 1, 0, 1
	xxlxor 38, 33, 38
	vrlw 6, 6, 4
	vadduwm 8, 6, 15
	xxswapd	0, 38
	lvx 6, 0, 4
	addis 4, 2, .LCPI1_9@toc@ha
	addi 4, 4, .LCPI1_9@toc@l
	xxlxor 32, 40, 32
	xxsldwi 40, 40, 40, 1
	vperm 13, 12, 18, 6
	vrlw 9, 0, 5
	vadduwm 0, 1, 14
	lvx 1, 0, 12
	xxsldwi 46, 46, 46, 3
	xxsldwi 32, 32, 32, 3
	vperm 7, 7, 7, 1
	vadduwm 15, 9, 0
	xxlxor 32, 47, 0
	vperm 16, 0, 0, 2
	lvx 0, 0, 11
	addis 11, 2, .LCPI1_10@toc@ha
	vcmpequh 0, 0, 17
	vadduwm 19, 16, 8
	xxlxor 40, 51, 41
	xxsel 45, 39, 45, 32
	vrlw 31, 8, 3
	lvx 8, 0, 4
	addis 4, 2, .LCPI1_11@toc@ha
	addi 4, 4, .LCPI1_11@toc@l
	vcmpequh 7, 8, 17
	vadduwm 8, 15, 13
	vadduwm 15, 31, 8
	lvx 8, 0, 4
	addi 4, 11, .LCPI1_10@toc@l
	lvx 17, 0, 4
	addis 4, 2, .LCPI1_12@toc@ha
	xxlxor 41, 47, 48
	xxsldwi 47, 47, 47, 1
	addi 4, 4, .LCPI1_12@toc@l
	xxlnor 48, 39, 39
	vrlw 29, 9, 4
	vperm 9, 16, 16, 8
	xxland 48, 50, 39
	vperm 17, 30, 12, 17
	vperm 16, 16, 16, 8
	vmrghw 12, 12, 10
	lvx 10, 0, 4
	addis 4, 2, .LCPI1_13@toc@ha
	vadduwm 19, 29, 19
	addi 4, 4, .LCPI1_13@toc@l
	xxlxor 63, 51, 63
	xxsldwi 51, 51, 51, 3
	xxland 0, 49, 41
	vrlw 17, 31, 5
	xxlor 48, 0, 48
	xxswapd	0, 61
	vperm 18, 12, 18, 10
	vadduwm 15, 15, 16
	xxland 60, 48, 39
	vadduwm 15, 17, 15
	vperm 28, 28, 28, 8
	xxlxor 63, 47, 0
	vadduwm 15, 15, 18
	vperm 31, 31, 31, 2
	vperm 30, 18, 16, 6
	vadduwm 19, 31, 19
	xxlxor 44, 51, 49
	vrlw 12, 12, 3
	vadduwm 15, 12, 15
	xxlxor 49, 47, 63
	vperm 31, 13, 14, 11
	vrlw 17, 17, 4
	vperm 14, 14, 14, 1
	vadduwm 15, 15, 31
	vadduwm 19, 17, 19
	xxswapd	0, 49
	xxsldwi 47, 47, 47, 3
	xxsel 46, 46, 62, 32
	xxlxor 44, 51, 44
	xxsldwi 51, 51, 51, 1
	vrlw 12, 12, 5
	vadduwm 15, 12, 15
	xxlxor 49, 47, 0
	vperm 17, 17, 17, 2
	vadduwm 19, 17, 19
	xxlxor 44, 51, 44
	vrlw 29, 12, 3
	vadduwm 12, 15, 14
	vadduwm 15, 29, 12
	lvx 12, 0, 4
	addis 4, 2, .LCPI1_14@toc@ha
	addi 4, 4, .LCPI1_14@toc@l
	xxlxor 49, 47, 49
	xxsldwi 47, 47, 47, 1
	vperm 30, 13, 18, 12
	vrlw 17, 17, 4
	vmrghw 13, 18, 13
	xxland 0, 62, 41
	vadduwm 19, 17, 19
	vperm 16, 13, 16, 10
	xxlxor 61, 51, 61
	xxsldwi 50, 51, 51, 3
	xxsldwi 51, 63, 63, 3
	vrlw 30, 29, 5
	xxlor 61, 60, 0
	xxswapd	0, 49
	vperm 31, 14, 19, 11
	vadduwm 15, 15, 29
	vperm 19, 19, 19, 1
	vadduwm 15, 30, 15
	xxlxor 49, 47, 0
	vadduwm 15, 15, 16
	vperm 17, 17, 17, 2
	vadduwm 18, 17, 18
	xxlxor 45, 50, 62
	vperm 30, 16, 29, 6
	vrlw 13, 13, 3
	vadduwm 15, 13, 15
	xxlxor 49, 47, 49
	vadduwm 15, 15, 31
	xxsldwi 63, 63, 63, 3
	vrlw 17, 17, 4
	xxsldwi 47, 47, 47, 3
	vadduwm 18, 17, 18
	xxswapd	0, 49
	xxlxor 45, 50, 45
	xxsldwi 50, 50, 50, 1
	vrlw 13, 13, 5
	vadduwm 15, 13, 15
	xxlxor 49, 47, 0
	vperm 17, 17, 17, 2
	vadduwm 18, 17, 18
	xxlxor 45, 50, 45
	vrlw 28, 13, 3
	xxsel 45, 51, 62, 32
	xxland 51, 61, 39
	vperm 30, 14, 16, 12
	vadduwm 15, 15, 13
	vperm 19, 19, 19, 8
	vmrghw 14, 16, 14
	vadduwm 15, 28, 15
	xxlxor 49, 47, 49
	xxsldwi 47, 47, 47, 1
	xxland 0, 62, 41
	vrlw 17, 17, 4
	xxlor 51, 51, 0
	vadduwm 15, 15, 19
	vadduwm 18, 17, 18
	xxswapd	0, 49
	xxlxor 60, 50, 60
	xxsldwi 48, 50, 50, 3
	vperm 18, 14, 29, 10
	vrlw 30, 28, 5
	vperm 29, 18, 19, 6
	vadduwm 15, 30, 15
	xxlxor 49, 47, 0
	vadduwm 15, 15, 18
	vperm 17, 17, 17, 2
	vadduwm 16, 17, 16
	xxlxor 46, 48, 62
	vperm 30, 13, 31, 11
	vrlw 14, 14, 3
	vperm 31, 31, 31, 1
	vadduwm 15, 14, 15
	xxlxor 49, 47, 49
	vadduwm 15, 15, 30
	vrlw 17, 17, 4
	xxsldwi 47, 47, 47, 3
	vadduwm 16, 17, 16
	xxswapd	0, 49
	xxlxor 46, 48, 46
	xxsldwi 48, 48, 48, 1
	vrlw 14, 14, 5
	vadduwm 15, 14, 15
	xxlxor 49, 47, 0
	vperm 17, 17, 17, 2
	vadduwm 16, 17, 16
	xxlxor 46, 48, 46
	vrlw 28, 14, 3
	xxsel 46, 63, 61, 32
	xxland 63, 51, 39
	vperm 29, 13, 18, 12
	vadduwm 15, 15, 14
	vperm 31, 31, 31, 8
	vmrghw 13, 18, 13
	vadduwm 15, 28, 15
	xxlxor 49, 47, 49
	xxsldwi 47, 47, 47, 1
	xxland 0, 61, 41
	vrlw 17, 17, 4
	xxlor 63, 63, 0
	vperm 13, 13, 19, 10
	xxsldwi 51, 62, 62, 3
	vadduwm 15, 15, 31
	vperm 30, 14, 19, 11
	vadduwm 16, 17, 16
	xxswapd	0, 49
	xxlxor 60, 48, 60
	xxsldwi 48, 48, 48, 3
	vrlw 29, 28, 5
	vadduwm 15, 29, 15
	xxlxor 49, 47, 0
	vadduwm 15, 15, 13
	vperm 17, 17, 17, 2
	vadduwm 16, 17, 16
	xxlxor 50, 48, 61
	vrlw 18, 18, 3
	vadduwm 15, 18, 15
	xxlxor 49, 47, 49
	vadduwm 15, 15, 30
	vrlw 17, 17, 4
	xxsldwi 47, 47, 47, 3
	vadduwm 11, 17, 16
	xxswapd	0, 49
	xxlxor 48, 43, 50
	xxsldwi 43, 43, 43, 1
	vperm 18, 19, 19, 1
	vrlw 16, 16, 5
	vperm 19, 13, 31, 6
	vadduwm 15, 16, 15
	xxlxor 49, 47, 0
	vperm 17, 17, 17, 2
	vadduwm 29, 17, 11
	xxlxor 43, 61, 48
	vrlw 16, 11, 3
	xxsel 43, 50, 51, 32
	xxland 50, 63, 39
	vperm 19, 14, 13, 12
	vadduwm 15, 15, 11
	vperm 18, 18, 18, 8
	vmrghw 13, 13, 14
	vadduwm 15, 16, 15
	xxlxor 49, 47, 49
	xxsldwi 47, 47, 47, 1
	xxland 0, 51, 41
	lvx 19, 0, 4
	vrlw 17, 17, 4
	xxlor 50, 50, 0
	vperm 13, 13, 31, 10
	xxsldwi 63, 62, 62, 3
	vadduwm 15, 15, 18
	vperm 19, 11, 31, 19
	vadduwm 29, 17, 29
	xxswapd	0, 49
	vperm 1, 31, 31, 1
	xxlxor 48, 61, 48
	xxsldwi 46, 61, 61, 3
	vperm 6, 13, 18, 6
	vrlw 16, 16, 5
	xxsel 32, 33, 38, 32
	xxland 38, 50, 39
	vadduwm 15, 16, 15
	vperm 7, 11, 13, 12
	xxlxor 49, 47, 0
	vadduwm 15, 15, 13
	vperm 17, 17, 17, 2
	vperm 6, 6, 6, 8
	vadduwm 14, 17, 14
	xxlxor 48, 46, 48
	vrlw 16, 16, 3
	vadduwm 15, 16, 15
	xxlxor 49, 47, 49
	xxsldwi 47, 47, 47, 3
	vrlw 17, 17, 4
	vadduwm 15, 15, 19
	vadduwm 14, 17, 14
	xxswapd	0, 49
	xxlxor 48, 46, 48
	xxsldwi 46, 46, 46, 1
	vrlw 16, 16, 5
	vadduwm 15, 16, 15
	xxlxor 49, 47, 0
	vadduwm 0, 15, 0
	vperm 17, 17, 17, 2
	xxland 0, 39, 41
	xxlor 38, 38, 0
	vadduwm 14, 17, 14
	xxlxor 48, 46, 48
	vrlw 16, 16, 3
	vadduwm 0, 16, 0
	xxlxor 33, 32, 49
	xxsldwi 32, 32, 32, 1
	vrlw 1, 1, 4
	vadduwm 0, 0, 6
	vadduwm 8, 1, 14
	xxswapd	0, 33
	xxlxor 44, 40, 48
	xxsldwi 38, 40, 40, 3
	vrlw 7, 12, 5
	vadduwm 0, 7, 0
	xxlxor 33, 32, 0
	vperm 2, 1, 1, 2
	vmrghw 1, 13, 11
	vadduwm 6, 2, 6
	vperm 1, 1, 18, 10
	xxlxor 39, 38, 39
	vrlw 3, 7, 3
	vadduwm 0, 0, 1
	vadduwm 0, 3, 0
	xxlxor 34, 32, 34
	xxsldwi 0, 32, 32, 3
	vrlw 2, 2, 4
	vadduwm 4, 2, 6
	xxswapd	2, 34
	xxlxor 35, 36, 35
	xxsldwi 1, 36, 36, 1
	vrlw 3, 3, 5
	xxlxor 0, 1, 0
	xxswapd	0, 0
	xxlxor 3, 35, 2
	stxvd2x 0, 0, 8
	xxswapd	3, 3
	stxvd2x 3, 8, 5
	lfdx 0, 0, 3
	lfd 3, 8(3)
	xxmrghd	34, 3, 0
	xxlxor 0, 1, 34
	xxswapd	0, 0
	stxvd2x 0, 8, 10
	lfd 0, 16(3)
	lfd 1, 24(3)
	li 3, -32
	xxmrghd	34, 1, 0
	xxlxor 0, 2, 34
	xxswapd	0, 0
	stxvd2x 0, 8, 6
	lxvd2x 63, 1, 3
	li 3, -48
	ld 30, -16(1)
	lxvd2x 62, 1, 3
	li 3, -64
	lxvd2x 61, 1, 3
	li 3, -80
	lxvd2x 60, 1, 3
	blr
	.long	0
	.quad	0
.Lfunc_end1:
	.size	zfs_blake3_compress_xof_sse2, .Lfunc_end1-.Lfunc_begin1
	.cfi_endproc

	.globl	zfs_blake3_hash_many_sse2
	.p2align	2
	.type	zfs_blake3_hash_many_sse2,@function
zfs_blake3_hash_many_sse2:
.Lfunc_begin2:
	.cfi_startproc
.Lfunc_gep2:
	addis 2, 12, .TOC.-.Lfunc_gep2@ha
	addi 2, 2, .TOC.-.Lfunc_gep2@l
.Lfunc_lep2:
	.localentry	zfs_blake3_hash_many_sse2, .Lfunc_lep2-.Lfunc_gep2
	mfocrf 12, 32
	mflr 0
	std 0, 16(1)
	stw 12, 8(1)
	stdu 1, -256(1)
	.cfi_def_cfa_offset 256
	.cfi_offset lr, 16
	.cfi_offset r17, -120
	.cfi_offset r18, -112
	.cfi_offset r19, -104
	.cfi_offset r20, -96
	.cfi_offset r21, -88
	.cfi_offset r22, -80
	.cfi_offset r23, -72
	.cfi_offset r24, -64
	.cfi_offset r25, -56
	.cfi_offset r26, -48
	.cfi_offset r27, -40
	.cfi_offset r28, -32
	.cfi_offset r29, -24
	.cfi_offset r30, -16
	.cfi_offset cr2, 8
	std 26, 208(1)
	mr	26, 4
	cmpldi 1, 4, 4
	andi. 4, 8, 1
	std 18, 144(1)
	std 19, 152(1)
	crmove	8, 1
	ld 19, 360(1)
	lwz 18, 352(1)
	std 24, 192(1)
	std 25, 200(1)
	std 27, 216(1)
	std 28, 224(1)
	mr	24, 10
	mr	28, 6
	mr	27, 5
	mr	25, 3
	std 29, 232(1)
	std 30, 240(1)
	mr	30, 9
	mr	29, 7
	std 17, 136(1)
	std 20, 160(1)
	std 21, 168(1)
	std 22, 176(1)
	std 23, 184(1)
	blt	1, .LBB2_3
	li 3, 0
	li 4, 1
	clrldi	23, 30, 32
	isel 22, 4, 3, 8
	clrldi	21, 24, 32
	clrldi	20, 18, 32
.LBB2_2:
	mr	3, 25
	mr	4, 27
	mr	5, 28
	mr	6, 29
	mr	7, 22
	mr	8, 23
	mr	9, 21
	mr	10, 20
	std 19, 32(1)
	bl blake3_hash4_sse2
	addi 26, 26, -4
	addi 3, 29, 4
	addi 25, 25, 32
	addi 19, 19, 128
	cmpldi	26, 3
	isel 29, 3, 29, 8
	bgt	0, .LBB2_2
.LBB2_3:
	cmpldi	26, 0
	beq	0, .LBB2_11
	li 3, 0
	li 4, 1
	or 21, 24, 30
	li 20, 16
	addi 24, 1, 96
	isel 22, 4, 3, 8
.LBB2_5:
	lxvd2x 0, 28, 20
	ld 23, 0(25)
	mr	17, 27
	mr	3, 21
	stxvd2x 0, 24, 20
	lxvd2x 0, 0, 28
	stxvd2x 0, 0, 24
.LBB2_6:
	cmpldi	17, 1
	beq	0, .LBB2_8
	cmpldi	17, 0
	bne	0, .LBB2_9
	b .LBB2_10
.LBB2_8:
	or 3, 3, 18
.LBB2_9:
	clrldi	7, 3, 56
	mr	3, 24
	mr	4, 23
	li 5, 64
	mr	6, 29
	bl zfs_blake3_compress_in_place_sse2
	addi 23, 23, 64
	addi 17, 17, -1
	mr	3, 30
	b .LBB2_6
.LBB2_10:
	lxvd2x 0, 24, 20
	addi 26, 26, -1
	add 29, 29, 22
	addi 25, 25, 8
	cmpldi	26, 0
	stxvd2x 0, 19, 20
	lxvd2x 0, 0, 24
	stxvd2x 0, 0, 19
	addi 19, 19, 32
	bne	0, .LBB2_5
.LBB2_11:
	ld 30, 240(1)
	ld 29, 232(1)
	ld 28, 224(1)
	ld 27, 216(1)
	ld 26, 208(1)
	ld 25, 200(1)
	ld 24, 192(1)
	ld 23, 184(1)
	ld 22, 176(1)
	ld 21, 168(1)
	ld 20, 160(1)
	ld 19, 152(1)
	ld 18, 144(1)
	ld 17, 136(1)
	addi 1, 1, 256
	ld 0, 16(1)
	lwz 12, 8(1)
	mtocrf 32, 12
	mtlr 0
	blr
	.long	0
	.quad	0
.Lfunc_end2:
	.size	zfs_blake3_hash_many_sse2, .Lfunc_end2-.Lfunc_begin2
	.cfi_endproc

	.section	.rodata.cst16,"aM",@progbits,16
	.p2align	4
.LCPI3_0:
	.quad	4294967296
	.quad	12884901890
.LCPI3_1:
	.byte	29
	.byte	28
	.byte	31
	.byte	30
	.byte	25
	.byte	24
	.byte	27
	.byte	26
	.byte	21
	.byte	20
	.byte	23
	.byte	22
	.byte	17
	.byte	16
	.byte	19
	.byte	18
.LCPI3_2:
	.long	1779033703
	.long	1779033703
	.long	1779033703
	.long	1779033703
.LCPI3_3:
	.long	3144134277
	.long	3144134277
	.long	3144134277
	.long	3144134277
.LCPI3_4:
	.long	1013904242
	.long	1013904242
	.long	1013904242
	.long	1013904242
.LCPI3_5:
	.long	2773480762
	.long	2773480762
	.long	2773480762
	.long	2773480762
	.text
	.p2align	2
	.type	blake3_hash4_sse2,@function
blake3_hash4_sse2:
.Lfunc_begin3:
	.cfi_startproc
.Lfunc_gep3:
	addis 2, 12, .TOC.-.Lfunc_gep3@ha
	addi 2, 2, .TOC.-.Lfunc_gep3@l
.Lfunc_lep3:
	.localentry	blake3_hash4_sse2, .Lfunc_lep3-.Lfunc_gep3
	stdu 1, -400(1)
	.cfi_def_cfa_offset 400
	.cfi_offset r22, -152
	.cfi_offset r23, -144
	.cfi_offset r24, -136
	.cfi_offset r25, -128
	.cfi_offset r26, -120
	.cfi_offset r27, -112
	.cfi_offset r28, -104
	.cfi_offset r29, -96
	.cfi_offset r30, -88
	.cfi_offset f23, -72
	.cfi_offset f24, -64
	.cfi_offset f25, -56
	.cfi_offset f26, -48
	.cfi_offset f27, -40
	.cfi_offset f28, -32
	.cfi_offset f29, -24
	.cfi_offset f30, -16
	.cfi_offset f31, -8
	.cfi_offset v20, -352
	.cfi_offset v21, -336
	.cfi_offset v22, -320
	.cfi_offset v23, -304
	.cfi_offset v24, -288
	.cfi_offset v25, -272
	.cfi_offset v26, -256
	.cfi_offset v27, -240
	.cfi_offset v28, -224
	.cfi_offset v29, -208
	.cfi_offset v30, -192
	.cfi_offset v31, -176
	li 11, 48
	li 0, 8
	std 30, 312(1)
	li 30, 12
	li 12, 4
	lfiwzx 0, 0, 5
	stxvd2x 52, 1, 11
	li 11, 64
	lfiwzx 2, 5, 0
	li 0, 20
	lfiwzx 3, 5, 30
	stxvd2x 53, 1, 11
	li 11, 80
	li 30, 24
	lfiwzx 4, 5, 0
	li 0, 28
	stxvd2x 54, 1, 11
	li 11, 96
	lfiwzx 1, 5, 12
	lfiwzx 6, 5, 30
	xxspltw 45, 0, 1
	cmpldi	4, 0
	std 22, 248(1)
	stxvd2x 55, 1, 11
	li 11, 112
	lfiwzx 7, 5, 0
	xxspltw 40, 2, 1
	std 23, 256(1)
	xxspltw 38, 3, 1
	xxspltw 50, 4, 1
	std 24, 264(1)
	std 25, 272(1)
	std 26, 280(1)
	xxspltw 54, 7, 1
	std 27, 288(1)
	std 28, 296(1)
	std 29, 304(1)
	stxvd2x 56, 1, 11
	li 11, 128
	stfd 23, 328(1)
	stxvd2x 57, 1, 11
	li 11, 144
	stfd 24, 336(1)
	stxvd2x 58, 1, 11
	li 11, 160
	stfd 25, 344(1)
	stxvd2x 59, 1, 11
	li 11, 176
	xxspltw 59, 1, 1
	stxvd2x 60, 1, 11
	li 11, 192
	stfd 26, 352(1)
	stxvd2x 61, 1, 11
	li 11, 208
	stfd 27, 360(1)
	stxvd2x 62, 1, 11
	li 11, 224
	xxspltw 62, 6, 1
	stxvd2x 63, 1, 11
	li 11, 16
	stfd 28, 368(1)
	lfiwzx 5, 5, 11
	ld 5, 432(1)
	stfd 29, 376(1)
	stfd 30, 384(1)
	stfd 31, 392(1)
	xxspltw 61, 5, 1
	beq	0, .LBB3_5
	addis 30, 2, .LCPI3_0@toc@ha
	neg 7, 7
	xxleqv 34, 34, 34
	addis 28, 2, .LCPI3_2@toc@ha
	addis 27, 2, .LCPI3_3@toc@ha
	addis 26, 2, .LCPI3_4@toc@ha
	addis 25, 2, .LCPI3_5@toc@ha
	ld 29, 24(3)
	addi 0, 30, .LCPI3_0@toc@l
	mtfprwz	1, 7
	addis 7, 2, .LCPI3_1@toc@ha
	ld 30, 16(3)
	lxvd2x 0, 0, 0
	mtfprwz	2, 6
	rldicl 6, 6, 32, 32
	addi 0, 7, .LCPI3_1@toc@l
	ld 7, 8(3)
	vslw 2, 2, 2
	lvx 5, 0, 0
	addi 0, 28, .LCPI3_2@toc@l
	addi 28, 27, .LCPI3_3@toc@l
	addi 27, 26, .LCPI3_4@toc@l
	addi 26, 25, .LCPI3_5@toc@l
	or 25, 9, 8
	li 9, 0
	xxspltw 36, 2, 1
	xxswapd	35, 0
	xxspltw 0, 1, 1
	xxland 35, 0, 35
	mtfprwz	0, 6
	ld 6, 0(3)
	addi 3, 3, -8
	vadduwm 4, 3, 4
	xxlor 35, 35, 34
	xxlxor 34, 36, 34
	xxlor 9, 36, 36
	vspltisw 4, 4
	vcmpgtsw 2, 3, 2
	xxspltw 35, 0, 1
	xxlor 10, 36, 36
	vsubuwm 2, 3, 2
	xxlor 11, 34, 34
	lvx 2, 0, 0
	li 0, 32
	xxlor 12, 34, 34
	lvx 2, 0, 28
	li 28, 48
	xxlor 13, 34, 34
	lvx 2, 0, 27
	li 27, 0
	xxlor 31, 34, 34
	lvx 2, 0, 26
	xxlor 30, 34, 34
.LBB3_2:
	mr	26, 27
	addi 27, 27, 1
	xxlor 28, 40, 40
	cmpld	27, 4
	sldi 26, 26, 6
	xxlor 24, 45, 45
	iseleq	24, 10, 9
	add 23, 6, 26
	add 22, 30, 26
	lxvd2x 0, 6, 26
	lxvd2x 1, 7, 26
	or 25, 24, 25
	add 24, 7, 26
	lxvd2x 2, 30, 26
	lxvd2x 3, 29, 26
	xxlor 29, 38, 38
	lxvd2x 4, 23, 11
	lxvd2x 6, 24, 11
	clrlwi	25, 25, 24
	lxvd2x 7, 22, 11
	lxvd2x 8, 23, 0
	mtfprd	5, 25
	add 25, 29, 26
	xxswapd	34, 0
	lxvd2x 0, 25, 11
	xxswapd	36, 1
	xxswapd	33, 2
	lxvd2x 1, 24, 0
	lxvd2x 2, 22, 0
	xxswapd	39, 3
	xxswapd	32, 4
	lxvd2x 3, 25, 0
	lxvd2x 4, 23, 28
	xxswapd	49, 6
	xxswapd	51, 7
	lxvd2x 6, 24, 28
	xxswapd	58, 8
	lxvd2x 7, 22, 28
	lxvd2x 8, 25, 28
	xxswapd	60, 0
	mr	25, 3
	xxswapd	57, 1
	xxswapd	53, 2
	xxswapd	52, 3
	xxswapd	56, 4
	xxswapd	55, 6
	xxswapd	0, 5
	xxswapd	40, 7
	xxswapd	41, 8
	mtctr 12
.LBB3_3:
	ldu 24, 8(25)
	add 24, 24, 26
	addi 24, 24, 256
	dcbt 0, 24
	bdnz .LBB3_3
	vmrgew 3, 4, 2
	vspltisw 31, 9
	mr	25, 8
	vmrglw 10, 4, 2
	vspltisw 14, 10
	vmrghw 6, 4, 2
	xxspltw 0, 0, 3
	vmrgew 4, 17, 0
	vmrglw 11, 17, 0
	vmrghw 16, 17, 0
	vmrgew 0, 25, 26
	vmrgew 13, 7, 1
	vmrglw 2, 7, 1
	vmrghw 7, 7, 1
	xxlor 25, 36, 36
	vmrgew 4, 28, 19
	xxlor 26, 32, 32
	vmrglw 0, 25, 26
	vmrglw 1, 28, 19
	xxmrgld	47, 34, 42
	xxlor 44, 28, 28
	vmrghw 25, 25, 26
	xxlor 23, 36, 36
	vmrghw 4, 28, 19
	vspltisw 19, -16
	xxlor 5, 32, 32
	vmrgew 0, 20, 21
	xxmrgld	34, 33, 43
	vmrglw 28, 20, 21
	vmrghw 21, 20, 21
	vmrglw 20, 23, 24
	vmrghw 26, 23, 24
	vmrglw 17, 9, 8
	xxlor 8, 32, 32
	vmrgew 0, 23, 24
	xxmrgld	56, 39, 38
	vmrgew 23, 9, 8
	xxlor 33, 24, 24
	xxlor 2, 34, 34
	vadduwm 11, 15, 1
	xxmrgld	33, 36, 48
	xxlor 6, 47, 47
	xxlor 27, 32, 32
	vmrghw 0, 9, 8
	vspltisw 9, 12
	vsubuwm 8, 31, 19
	xxmrgld	51, 23, 25
	vadduwm 31, 2, 12
	xxlor 34, 10, 10
	vadduwm 10, 14, 14
	vslw 15, 2, 2
	xxlor 34, 29, 29
	vadduwm 14, 24, 27
	xxlor 24, 48, 48
	vadduwm 16, 1, 2
	xxmrgld	34, 45, 35
	vadduwm 31, 31, 30
	xxmrghd	36, 36, 24
	vadduwm 11, 11, 29
	vadduwm 14, 14, 18
	vadduwm 13, 16, 22
	xxlxor 47, 63, 47
	xxlor 1, 9, 9
	xxlor 1, 11, 11
	xxlxor 48, 43, 9
	vadduwm 11, 11, 2
	xxlor 7, 34, 34
	xxmrghd	34, 39, 38
	xxlxor 39, 46, 11
	xxlor 1, 50, 50
	xxlxor 50, 45, 0
	vperm 15, 15, 15, 5
	vperm 16, 16, 16, 5
	vperm 7, 7, 7, 5
	vperm 18, 18, 18, 5
	xxlor 4, 33, 33
	xxlor 33, 31, 31
	vadduwm 14, 14, 2
	xxlor 3, 34, 34
	xxlor 34, 12, 12
	xxlor 35, 13, 13
	vadduwm 6, 15, 1
	xxlor 33, 30, 30
	vadduwm 2, 16, 2
	vadduwm 3, 7, 3
	vadduwm 12, 18, 1
	xxlxor 59, 34, 61
	xxlxor 61, 35, 1
	xxlxor 33, 38, 62
	xxlxor 62, 44, 54
	vrlw 22, 27, 10
	vrlw 29, 29, 10
	vrlw 1, 1, 10
	vrlw 30, 30, 10
	vadduwm 31, 31, 19
	vadduwm 13, 13, 4
	vadduwm 11, 22, 11
	vadduwm 14, 29, 14
	vadduwm 31, 1, 31
	vadduwm 13, 30, 13
	vadduwm 9, 9, 9
	xxlor 1, 36, 36
	xxlxor 48, 43, 48
	xxlxor 36, 46, 39
	xxmrgld	39, 60, 5
	xxlxor 47, 63, 47
	xxlxor 50, 45, 50
	vrlw 16, 16, 9
	vrlw 28, 4, 9
	xxmrgld	36, 53, 57
	vrlw 15, 15, 9
	xxmrghd	57, 53, 57
	vrlw 18, 18, 9
	vadduwm 14, 14, 4
	xxlor 0, 36, 36
	xxmrgld	36, 49, 52
	vadduwm 2, 16, 2
	xxmrgld	49, 8, 26
	vadduwm 3, 28, 3
	vadduwm 6, 15, 6
	vadduwm 12, 18, 12
	xxlxor 54, 34, 54
	xxlxor 61, 35, 61
	xxlxor 33, 38, 33
	xxlxor 62, 44, 62
	vrlw 29, 29, 8
	vrlw 20, 1, 8
	xxmrgld	33, 55, 27
	vrlw 30, 30, 8
	vrlw 22, 22, 8
	vadduwm 11, 11, 7
	xxlor 5, 39, 39
	xxmrgld	39, 32, 58
	vadduwm 31, 31, 4
	vadduwm 11, 29, 11
	vadduwm 13, 13, 7
	vadduwm 14, 20, 14
	vadduwm 31, 30, 31
	vadduwm 13, 22, 13
	xxlor 28, 36, 36
	xxlxor 50, 43, 50
	xxlxor 48, 46, 48
	xxlxor 36, 63, 60
	xxlxor 47, 45, 47
	vperm 18, 18, 18, 5
	vperm 16, 16, 16, 5
	vperm 4, 4, 4, 5
	vperm 15, 15, 15, 5
	vadduwm 11, 11, 17
	vmr	28, 17
	xxmrghd	49, 32, 58
	vadduwm 6, 18, 6
	vadduwm 12, 16, 12
	vadduwm 21, 4, 2
	vadduwm 3, 15, 3
	xxlxor 34, 38, 61
	xxlxor 61, 44, 52
	xxlxor 62, 53, 62
	xxlxor 54, 35, 54
	vrlw 20, 2, 10
	vrlw 29, 29, 10
	vrlw 0, 30, 10
	vrlw 30, 22, 10
	vadduwm 14, 14, 25
	vadduwm 31, 31, 1
	vadduwm 13, 13, 17
	vadduwm 11, 20, 11
	vadduwm 14, 29, 14
	vadduwm 31, 0, 31
	vadduwm 13, 30, 13
	xxlxor 50, 43, 50
	xxlxor 48, 46, 48
	xxlxor 36, 63, 36
	xxlxor 47, 45, 47
	vrlw 18, 18, 9
	vrlw 16, 16, 9
	vrlw 4, 4, 9
	vrlw 15, 15, 9
	vadduwm 11, 11, 24
	xxlor 8, 56, 56
	vadduwm 6, 18, 6
	vadduwm 12, 16, 12
	vadduwm 24, 4, 21
	vadduwm 3, 15, 3
	xxlxor 55, 38, 52
	xxlxor 61, 44, 61
	xxlxor 62, 35, 62
	xxlxor 32, 56, 32
	vrlw 30, 30, 8
	vrlw 23, 23, 8
	vrlw 29, 29, 8
	vrlw 0, 0, 8
	xxlor 25, 51, 51
	vmr	26, 17
	xxlor 49, 3, 3
	xxlor 52, 1, 1
	xxlor 51, 2, 2
	vadduwm 14, 14, 17
	vadduwm 31, 31, 20
	vadduwm 13, 13, 19
	vadduwm 11, 30, 11
	vadduwm 14, 23, 14
	vadduwm 31, 29, 31
	vadduwm 13, 0, 13
	xxlxor 48, 43, 48
	xxlxor 36, 46, 36
	xxlxor 47, 63, 47
	xxlxor 50, 45, 50
	vperm 16, 16, 16, 5
	vperm 4, 4, 4, 5
	vperm 15, 15, 15, 5
	vperm 18, 18, 18, 5
	xxlor 29, 39, 39
	xxlor 59, 4, 4
	vadduwm 24, 16, 24
	vadduwm 3, 4, 3
	vadduwm 6, 15, 6
	vadduwm 12, 18, 12
	xxlxor 62, 56, 62
	xxlxor 55, 35, 55
	xxlxor 61, 38, 61
	xxlxor 32, 44, 32
	vrlw 30, 30, 10
	vrlw 23, 23, 10
	vrlw 29, 29, 10
	vrlw 0, 0, 10
	xxlor 53, 0, 0
	xxlor 39, 6, 6
	vadduwm 11, 11, 27
	vadduwm 14, 14, 21
	vadduwm 31, 31, 7
	vadduwm 13, 13, 1
	vadduwm 11, 30, 11
	vadduwm 14, 23, 14
	vadduwm 31, 29, 31
	vadduwm 13, 0, 13
	xxlxor 48, 43, 48
	xxlxor 36, 46, 36
	xxlxor 47, 63, 47
	xxlxor 50, 45, 50
	vrlw 16, 16, 9
	vrlw 4, 4, 9
	vrlw 15, 15, 9
	vrlw 18, 18, 9
	xxlor 34, 7, 7
	vadduwm 31, 31, 28
	vadduwm 24, 16, 24
	vadduwm 3, 4, 3
	vadduwm 6, 15, 6
	vadduwm 12, 18, 12
	xxlxor 62, 56, 62
	xxlxor 55, 35, 55
	xxlxor 61, 38, 61
	xxlxor 32, 44, 32
	vrlw 23, 23, 8
	vrlw 29, 29, 8
	vrlw 0, 0, 8
	vrlw 30, 30, 8
	vadduwm 11, 11, 2
	xxlor 34, 28, 28
	vadduwm 13, 13, 26
	vadduwm 14, 14, 2
	vadduwm 11, 23, 11
	vadduwm 14, 29, 14
	vadduwm 31, 0, 31
	vadduwm 13, 30, 13
	xxlxor 50, 43, 50
	xxlxor 48, 46, 48
	xxlxor 36, 63, 36
	xxlxor 47, 45, 47
	vperm 18, 18, 18, 5
	vperm 16, 16, 16, 5
	vperm 4, 4, 4, 5
	vperm 15, 15, 15, 5
	xxlor 2, 58, 58
	xxlor 39, 25, 25
	vadduwm 6, 18, 6
	vadduwm 12, 16, 12
	vadduwm 24, 4, 24
	vadduwm 3, 15, 3
	xxlxor 55, 38, 55
	xxlxor 61, 44, 61
	xxlxor 32, 56, 32
	xxlxor 62, 35, 62
	vrlw 23, 23, 10
	vrlw 29, 29, 10
	vrlw 0, 0, 10
	vrlw 30, 30, 10
	xxlor 54, 29, 29
	xxlor 58, 5, 5
	vadduwm 11, 11, 25
	vadduwm 14, 14, 7
	vadduwm 31, 31, 22
	vadduwm 13, 13, 26
	vadduwm 11, 23, 11
	vadduwm 14, 29, 14
	vadduwm 31, 0, 31
	vadduwm 13, 30, 13
	xxlxor 50, 43, 50
	xxlxor 48, 46, 48
	xxlxor 36, 63, 36
	xxlxor 47, 45, 47
	vrlw 18, 18, 9
	vrlw 16, 16, 9
	vrlw 4, 4, 9
	vrlw 15, 15, 9
	vadduwm 11, 11, 17
	vadduwm 14, 14, 21
	vadduwm 6, 18, 6
	vadduwm 12, 16, 12
	vadduwm 24, 4, 24
	vadduwm 3, 15, 3
	xxlxor 55, 38, 55
	xxlxor 61, 44, 61
	xxlxor 62, 35, 62
	xxlxor 32, 56, 32
	vrlw 30, 30, 8
	vrlw 23, 23, 8
	vrlw 29, 29, 8
	vrlw 0, 0, 8
	vadduwm 31, 31, 1
	vadduwm 13, 13, 20
	vadduwm 11, 30, 11
	vadduwm 14, 23, 14
	vadduwm 31, 29, 31
	vadduwm 13, 0, 13
	xxlxor 48, 43, 48
	xxlxor 36, 46, 36
	xxlxor 47, 63, 47
	xxlxor 50, 45, 50
	vperm 16, 16, 16, 5
	vperm 4, 4, 4, 5
	vperm 15, 15, 15, 5
	vperm 18, 18, 18, 5
	xxlor 0, 33, 33
	xxlor 33, 8, 8
	vadduwm 24, 16, 24
	vadduwm 3, 4, 3
	vadduwm 6, 15, 6
	vadduwm 12, 18, 12
	xxlxor 62, 56, 62
	xxlxor 55, 35, 55
	xxlxor 61, 38, 61
	xxlxor 32, 44, 32
	vrlw 30, 30, 10
	vrlw 23, 23, 10
	vrlw 29, 29, 10
	vrlw 0, 0, 10
	vadduwm 11, 11, 19
	vadduwm 14, 14, 2
	vadduwm 31, 31, 1
	vadduwm 13, 13, 22
	vadduwm 11, 30, 11
	vadduwm 14, 23, 14
	vadduwm 31, 29, 31
	vadduwm 13, 0, 13
	xxlxor 48, 43, 48
	xxlxor 36, 46, 36
	xxlxor 47, 63, 47
	xxlxor 50, 45, 50
	vrlw 16, 16, 9
	vrlw 4, 4, 9
	vrlw 15, 15, 9
	vrlw 18, 18, 9
	vadduwm 11, 11, 27
	vadduwm 14, 14, 28
	vadduwm 24, 16, 24
	vadduwm 3, 4, 3
	vadduwm 6, 15, 6
	vadduwm 12, 18, 12
	xxlxor 62, 56, 62
	xxlxor 55, 35, 55
	xxlxor 61, 38, 61
	xxlxor 32, 44, 32
	vrlw 23, 23, 8
	vrlw 29, 29, 8
	vrlw 0, 0, 8
	vrlw 30, 30, 8
	vadduwm 31, 31, 25
	vadduwm 13, 13, 26
	vadduwm 11, 23, 11
	vadduwm 14, 29, 14
	vadduwm 31, 0, 31
	vadduwm 13, 30, 13
	xxlxor 50, 43, 50
	xxlxor 48, 46, 48
	xxlxor 36, 63, 36
	xxlxor 47, 45, 47
	vperm 18, 18, 18, 5
	vperm 16, 16, 16, 5
	vperm 4, 4, 4, 5
	vperm 15, 15, 15, 5
	xxlor 3, 7, 7
	vadduwm 11, 11, 7
	vadduwm 6, 18, 6
	vadduwm 12, 16, 12
	vadduwm 24, 4, 24
	vadduwm 3, 15, 3
	xxlxor 55, 38, 55
	xxlxor 61, 44, 61
	xxlxor 32, 56, 32
	xxlxor 62, 35, 62
	vrlw 23, 23, 10
	vrlw 29, 29, 10
	vrlw 0, 0, 10
	vrlw 30, 30, 10
	xxlor 33, 6, 6
	xxlor 58, 2, 2
	xxlor 39, 3, 3
	vadduwm 14, 14, 1
	vadduwm 31, 31, 26
	vadduwm 13, 13, 7
	vadduwm 11, 23, 11
	vadduwm 14, 29, 14
	vadduwm 31, 0, 31
	vadduwm 13, 30, 13
	xxlxor 50, 43, 50
	xxlxor 48, 46, 48
	xxlxor 36, 63, 36
	xxlxor 47, 45, 47
	vrlw 18, 18, 9
	vrlw 16, 16, 9
	vrlw 4, 4, 9
	vrlw 15, 15, 9
	xxlor 52, 0, 0
	vadduwm 11, 11, 21
	vadduwm 6, 18, 6
	vadduwm 12, 16, 12
	vadduwm 24, 4, 24
	vadduwm 3, 15, 3
	xxlxor 55, 38, 55
	xxlxor 61, 44, 61
	xxlxor 62, 35, 62
	xxlxor 32, 56, 32
	vrlw 30, 30, 8
	vrlw 23, 23, 8
	vrlw 29, 29, 8
	vrlw 0, 0, 8
	vadduwm 14, 14, 2
	vadduwm 31, 31, 22
	vadduwm 13, 13, 20
	vadduwm 11, 30, 11
	vadduwm 14, 23, 14
	vadduwm 31, 29, 31
	vadduwm 13, 0, 13
	xxlxor 48, 43, 48
	xxlxor 36, 46, 36
	xxlxor 47, 63, 47
	xxlxor 50, 45, 50
	vperm 16, 16, 16, 5
	vperm 4, 4, 4, 5
	vperm 15, 15, 15, 5
	vperm 18, 18, 18, 5
	xxlor 7, 49, 49
	vmr	17, 2
	vadduwm 24, 16, 24
	vadduwm 3, 4, 3
	vadduwm 6, 15, 6
	vadduwm 12, 18, 12
	xxlxor 62, 56, 62
	xxlxor 55, 35, 55
	xxlxor 61, 38, 61
	xxlxor 32, 44, 32
	vrlw 30, 30, 10
	vrlw 23, 23, 10
	vrlw 29, 29, 10
	vrlw 0, 0, 10
	xxlor 54, 1, 1
	xxlor 34, 7, 7
	vadduwm 11, 11, 22
	vadduwm 14, 14, 28
	vadduwm 31, 31, 2
	vadduwm 13, 13, 26
	vadduwm 11, 30, 11
	vadduwm 14, 23, 14
	vadduwm 31, 29, 31
	vadduwm 13, 0, 13
	xxlxor 48, 43, 48
	xxlxor 36, 46, 36
	xxlxor 47, 63, 47
	xxlxor 50, 45, 50
	vrlw 16, 16, 9
	vrlw 4, 4, 9
	vrlw 15, 15, 9
	vrlw 18, 18, 9
	xxlor 59, 25, 25
	vadduwm 11, 11, 19
	vadduwm 24, 16, 24
	vadduwm 3, 4, 3
	vadduwm 6, 15, 6
	vadduwm 12, 18, 12
	xxlxor 62, 56, 62
	xxlxor 55, 35, 55
	xxlxor 61, 38, 61
	xxlxor 32, 44, 32
	vrlw 23, 23, 8
	vrlw 29, 29, 8
	vrlw 0, 0, 8
	vrlw 30, 30, 8
	vadduwm 14, 14, 25
	vadduwm 31, 31, 27
	vadduwm 13, 13, 7
	vadduwm 11, 23, 11
	vadduwm 14, 29, 14
	vadduwm 31, 0, 31
	vadduwm 13, 30, 13
	xxlxor 50, 43, 50
	xxlxor 48, 46, 48
	xxlxor 36, 63, 36
	xxlxor 47, 45, 47
	vperm 18, 18, 18, 5
	vperm 16, 16, 16, 5
	vperm 4, 4, 4, 5
	vperm 15, 15, 15, 5
	vmr	2, 19
	xxlor 0, 7, 7
	vadduwm 6, 18, 6
	vadduwm 12, 16, 12
	vadduwm 24, 4, 24
	vadduwm 3, 15, 3
	xxlxor 55, 38, 55
	xxlxor 61, 44, 61
	xxlxor 32, 56, 32
	xxlxor 62, 35, 62
	vrlw 23, 23, 10
	vrlw 29, 29, 10
	vrlw 0, 0, 10
	vrlw 30, 30, 10
	xxlor 1, 51, 51
	xxlor 7, 39, 39
	xxlor 51, 8, 8
	xxlor 39, 5, 5
	xxlor 34, 4, 4
	vadduwm 11, 11, 1
	vadduwm 14, 14, 19
	vadduwm 31, 31, 7
	vadduwm 13, 13, 2
	vadduwm 11, 23, 11
	vadduwm 14, 29, 14
	vadduwm 31, 0, 31
	vadduwm 13, 30, 13
	xxlxor 50, 43, 50
	xxlxor 48, 46, 48
	xxlxor 36, 63, 36
	xxlxor 47, 45, 47
	vrlw 18, 18, 9
	vrlw 16, 16, 9
	vrlw 4, 4, 9
	vrlw 15, 15, 9
	xxlor 2, 53, 53
	vmr	21, 28
	vadduwm 6, 18, 6
	vadduwm 12, 16, 12
	vadduwm 24, 4, 24
	vadduwm 3, 15, 3
	xxlxor 55, 38, 55
	xxlxor 61, 44, 61
	xxlxor 62, 35, 62
	xxlxor 32, 56, 32
	vrlw 30, 30, 8
	vrlw 23, 23, 8
	vrlw 29, 29, 8
	vrlw 0, 0, 8
	xxlor 53, 29, 29
	vadduwm 11, 11, 17
	vadduwm 14, 14, 28
	vadduwm 31, 31, 26
	vadduwm 13, 13, 21
	vadduwm 11, 30, 11
	vadduwm 14, 23, 14
	vadduwm 31, 29, 31
	vadduwm 13, 0, 13
	xxlxor 48, 43, 48
	xxlxor 36, 46, 36
	xxlxor 47, 63, 47
	xxlxor 50, 45, 50
	vperm 16, 16, 16, 5
	vperm 4, 4, 4, 5
	vperm 15, 15, 15, 5
	vperm 18, 18, 18, 5
	vadduwm 11, 11, 20
	xxlor 5, 52, 52
	vadduwm 24, 16, 24
	vadduwm 3, 4, 3
	vadduwm 6, 15, 6
	vadduwm 12, 18, 12
	xxlxor 62, 56, 62
	xxlxor 55, 35, 55
	xxlxor 61, 38, 61
	xxlxor 32, 44, 32
	vrlw 30, 30, 10
	vrlw 23, 23, 10
	vrlw 29, 29, 10
	vrlw 0, 0, 10
	xxlor 52, 2, 2
	vadduwm 14, 14, 25
	vadduwm 31, 31, 20
	vadduwm 13, 13, 7
	vadduwm 11, 30, 11
	vadduwm 14, 23, 14
	vadduwm 31, 29, 31
	vadduwm 13, 0, 13
	xxlxor 48, 43, 48
	xxlxor 36, 46, 36
	xxlxor 47, 63, 47
	xxlxor 50, 45, 50
	vrlw 16, 16, 9
	vrlw 4, 4, 9
	vrlw 15, 15, 9
	vrlw 18, 18, 9
	vadduwm 11, 11, 22
	vadduwm 14, 14, 27
	vadduwm 24, 16, 24
	vadduwm 3, 4, 3
	vadduwm 6, 15, 6
	vadduwm 12, 18, 12
	xxlxor 62, 56, 62
	xxlxor 55, 35, 55
	xxlxor 61, 38, 61
	xxlxor 32, 44, 32
	vrlw 23, 23, 8
	vrlw 29, 29, 8
	vrlw 0, 0, 8
	vrlw 30, 30, 8
	vadduwm 31, 31, 1
	vadduwm 13, 13, 2
	vadduwm 11, 23, 11
	vadduwm 14, 29, 14
	vadduwm 31, 0, 31
	vadduwm 13, 30, 13
	xxlxor 50, 43, 50
	xxlxor 48, 46, 48
	xxlxor 36, 63, 36
	xxlxor 47, 45, 47
	vperm 18, 18, 18, 5
	vperm 16, 16, 16, 5
	vperm 4, 4, 4, 5
	vperm 15, 15, 15, 5
	xxlor 3, 29, 29
	xxlor 4, 49, 49
	vadduwm 6, 18, 6
	vadduwm 12, 16, 12
	vadduwm 24, 4, 24
	vadduwm 3, 15, 3
	xxlxor 55, 38, 55
	xxlxor 61, 44, 61
	xxlxor 32, 56, 32
	xxlxor 62, 35, 62
	vrlw 23, 23, 10
	vrlw 29, 29, 10
	vrlw 0, 0, 10
	vrlw 30, 30, 10
	vmr	17, 28
	xxlor 2, 54, 54
	xxlor 3, 34, 34
	xxlor 34, 8, 8
	xxlor 51, 0, 0
	xxlor 60, 7, 7
	xxlor 54, 1, 1
	vadduwm 11, 11, 2
	vadduwm 14, 14, 19
	vadduwm 31, 31, 28
	vadduwm 13, 13, 22
	vadduwm 11, 23, 11
	vadduwm 14, 29, 14
	vadduwm 31, 0, 31
	vadduwm 13, 30, 13
	xxlxor 50, 43, 50
	xxlxor 48, 46, 48
	xxlxor 36, 63, 36
	xxlxor 47, 45, 47
	vrlw 18, 18, 9
	vrlw 16, 16, 9
	vrlw 4, 4, 9
	vrlw 15, 15, 9
	vadduwm 11, 11, 17
	vadduwm 14, 14, 25
	vadduwm 6, 18, 6
	vadduwm 12, 16, 12
	vadduwm 24, 4, 24
	vadduwm 3, 15, 3
	xxlxor 55, 38, 55
	xxlxor 61, 44, 61
	xxlxor 62, 35, 62
	xxlxor 32, 56, 32
	vrlw 30, 30, 8
	vrlw 23, 23, 8
	vrlw 29, 29, 8
	vrlw 0, 0, 8
	vadduwm 31, 31, 7
	vadduwm 13, 13, 26
	vadduwm 11, 30, 11
	vadduwm 14, 23, 14
	vadduwm 31, 29, 31
	vadduwm 13, 0, 13
	xxlxor 48, 43, 48
	xxlxor 36, 46, 36
	xxlxor 47, 63, 47
	xxlxor 50, 45, 50
	vperm 16, 16, 16, 5
	vperm 4, 4, 4, 5
	vperm 15, 15, 15, 5
	vperm 18, 18, 18, 5
	xxlor 6, 39, 39
	xxlor 39, 4, 4
	vadduwm 24, 16, 24
	vadduwm 3, 4, 3
	vadduwm 6, 15, 6
	vadduwm 12, 18, 12
	xxlxor 62, 56, 62
	xxlxor 55, 35, 55
	xxlxor 61, 38, 61
	xxlxor 32, 44, 32
	vrlw 30, 30, 10
	vrlw 23, 23, 10
	vrlw 29, 29, 10
	vrlw 0, 0, 10
	vadduwm 11, 11, 21
	vadduwm 14, 14, 27
	vadduwm 31, 31, 7
	vadduwm 13, 13, 28
	vadduwm 11, 30, 11
	vadduwm 14, 23, 14
	vadduwm 31, 29, 31
	vadduwm 13, 0, 13
	xxlxor 48, 43, 48
	xxlxor 36, 46, 36
	xxlxor 47, 63, 47
	xxlxor 50, 45, 50
	vrlw 16, 16, 9
	vrlw 4, 4, 9
	vrlw 15, 15, 9
	vrlw 18, 18, 9
	xxlor 0, 49, 49
	xxlor 49, 5, 5
	vadduwm 24, 16, 24
	vadduwm 3, 4, 3
	vadduwm 6, 15, 6
	vadduwm 12, 18, 12
	xxlxor 62, 56, 62
	xxlxor 55, 35, 55
	xxlxor 61, 38, 61
	xxlxor 32, 44, 32
	vrlw 23, 23, 8
	vrlw 29, 29, 8
	vrlw 0, 0, 8
	vrlw 30, 30, 8
	vadduwm 11, 11, 17
	vadduwm 14, 14, 1
	vadduwm 31, 31, 2
	vadduwm 13, 13, 22
	vadduwm 11, 23, 11
	vadduwm 14, 29, 14
	vadduwm 31, 0, 31
	vadduwm 13, 30, 13
	xxlxor 50, 43, 50
	xxlxor 48, 46, 48
	xxlxor 36, 63, 36
	xxlxor 47, 45, 47
	vperm 18, 18, 18, 5
	vperm 16, 16, 16, 5
	vperm 4, 4, 4, 5
	vperm 15, 15, 15, 5
	xxlor 34, 3, 3
	xxlor 49, 2, 2
	vadduwm 6, 18, 6
	vadduwm 12, 16, 12
	vadduwm 24, 4, 24
	vadduwm 3, 15, 3
	xxlxor 55, 38, 55
	xxlxor 61, 44, 61
	xxlxor 32, 56, 32
	xxlxor 62, 35, 62
	vrlw 23, 23, 10
	vrlw 29, 29, 10
	vrlw 0, 0, 10
	vrlw 30, 30, 10
	vadduwm 11, 11, 19
	vadduwm 14, 14, 20
	vadduwm 31, 31, 2
	vadduwm 13, 13, 17
	vadduwm 11, 23, 11
	vadduwm 14, 29, 14
	vadduwm 31, 0, 31
	vadduwm 13, 30, 13
	xxlxor 50, 43, 50
	xxlxor 48, 46, 48
	xxlxor 36, 63, 36
	xxlxor 47, 45, 47
	vrlw 18, 18, 9
	vrlw 16, 16, 9
	vrlw 4, 4, 9
	vrlw 15, 15, 9
	vadduwm 14, 14, 27
	vadduwm 11, 11, 25
	vadduwm 6, 18, 6
	vadduwm 12, 16, 12
	vadduwm 27, 4, 24
	vadduwm 3, 15, 3
	xxlxor 57, 38, 55
	xxlxor 61, 44, 61
	xxlxor 62, 35, 62
	xxlxor 32, 59, 32
	xxlor 39, 7, 7
	vrlw 30, 30, 8
	vrlw 25, 25, 8
	vrlw 29, 29, 8
	vrlw 0, 0, 8
	xxlor 1, 58, 58
	vmr	26, 19
	vadduwm 19, 31, 7
	xxlor 39, 6, 6
	vadduwm 11, 30, 11
	vadduwm 7, 13, 7
	vadduwm 13, 25, 14
	vadduwm 14, 29, 19
	vadduwm 7, 0, 7
	xxlxor 48, 43, 48
	xxlxor 36, 45, 36
	xxlxor 47, 46, 47
	xxlxor 50, 39, 50
	vperm 16, 16, 16, 5
	vperm 4, 4, 4, 5
	vperm 15, 15, 15, 5
	vperm 18, 18, 18, 5
	xxlor 51, 1, 1
	vadduwm 13, 13, 1
	vadduwm 11, 11, 19
	vadduwm 19, 16, 27
	vadduwm 3, 4, 3
	vadduwm 6, 15, 6
	vadduwm 12, 18, 12
	xxlxor 63, 51, 62
	xxlxor 62, 35, 57
	xxlxor 61, 38, 61
	xxlxor 32, 44, 32
	vrlw 31, 31, 10
	vrlw 30, 30, 10
	vrlw 29, 29, 10
	vrlw 0, 0, 10
	xxlor 33, 0, 0
	vadduwm 7, 7, 2
	vadduwm 14, 14, 1
	vadduwm 11, 31, 11
	vadduwm 13, 30, 13
	vadduwm 14, 29, 14
	vadduwm 7, 0, 7
	xxlxor 48, 43, 48
	xxlxor 36, 45, 36
	xxlxor 47, 46, 47
	xxlxor 50, 39, 50
	vrlw 16, 16, 9
	vrlw 4, 4, 9
	vrlw 15, 15, 9
	vrlw 18, 18, 9
	xxlor 60, 8, 8
	vadduwm 1, 11, 21
	vadduwm 11, 13, 28
	vadduwm 13, 16, 19
	vadduwm 3, 4, 3
	vadduwm 6, 15, 6
	vadduwm 12, 18, 12
	xxlxor 51, 45, 63
	xxlxor 63, 35, 62
	xxlxor 62, 38, 61
	xxlxor 32, 44, 32
	vrlw 31, 31, 8
	vrlw 30, 30, 8
	vrlw 0, 0, 8
	vrlw 19, 19, 8
	vadduwm 14, 14, 26
	vadduwm 7, 7, 17
	vadduwm 1, 31, 1
	vadduwm 11, 30, 11
	vadduwm 14, 0, 14
	vadduwm 7, 19, 7
	xxlxor 50, 33, 50
	xxlxor 48, 43, 48
	xxlxor 36, 46, 36
	xxlxor 47, 39, 47
	vperm 18, 18, 18, 5
	vperm 16, 16, 16, 5
	vperm 4, 4, 4, 5
	vperm 15, 15, 15, 5
	xxlor 34, 4, 4
	vadduwm 14, 14, 22
	vadduwm 6, 18, 6
	vadduwm 12, 16, 12
	vadduwm 13, 4, 13
	vadduwm 3, 15, 3
	xxlxor 49, 38, 63
	xxlxor 63, 44, 62
	xxlxor 32, 45, 32
	xxlxor 51, 35, 51
	vrlw 17, 17, 10
	vrlw 31, 31, 10
	vrlw 0, 0, 10
	vrlw 10, 19, 10
	vadduwm 11, 11, 2
	xxlor 34, 5, 5
	vadduwm 1, 1, 20
	vadduwm 2, 7, 2
	vadduwm 7, 31, 11
	vadduwm 11, 0, 14
	vadduwm 2, 10, 2
	vadduwm 1, 17, 1
	xxlxor 36, 43, 36
	xxlxor 46, 34, 47
	vrlw 4, 4, 9
	vrlw 14, 14, 9
	xxlxor 47, 33, 50
	xxlxor 48, 39, 48
	vrlw 15, 15, 9
	vrlw 9, 16, 9
	vadduwm 13, 4, 13
	vadduwm 3, 14, 3
	xxlxor 32, 45, 32
	xxlxor 45, 45, 33
	xxlxor 33, 35, 42
	xxlxor 59, 35, 39
	vadduwm 3, 15, 6
	vadduwm 6, 9, 12
	xxlxor 39, 35, 49
	xxlxor 42, 38, 63
	vrlw 1, 1, 8
	vrlw 7, 7, 8
	vrlw 10, 10, 8
	vrlw 0, 0, 8
	xxlxor 40, 35, 43
	xxlxor 38, 38, 34
	xxlxor 61, 33, 41
	xxlxor 50, 39, 36
	xxlxor 62, 42, 46
	xxlxor 54, 32, 47
	bne	0, .LBB3_2
.LBB3_5:
	vmrglw 2, 27, 13
	li 3, 32
	li 4, 48
	vmrglw 4, 6, 8
	vmrglw 0, 18, 29
	vmrglw 1, 22, 30
	vmrghw 3, 27, 13
	vmrghw 5, 6, 8
	vmrghw 6, 18, 29
	vmrghw 7, 22, 30
	xxmrgld	40, 36, 34
	xxmrghd	34, 36, 34
	xxmrgld	41, 33, 32
	xxswapd	0, 40
	xxmrgld	36, 37, 35
	xxmrghd	35, 37, 35
	xxmrghd	37, 33, 32
	xxswapd	1, 41
	xxmrgld	32, 39, 38
	xxmrghd	33, 39, 38
	xxswapd	2, 34
	xxswapd	4, 36
	xxswapd	3, 37
	stxvd2x 0, 0, 5
	xxswapd	5, 32
	stxvd2x 1, 5, 11
	xxswapd	0, 35
	xxswapd	1, 33
	stxvd2x 2, 5, 3
	li 3, 64
	stxvd2x 3, 5, 4
	li 4, 80
	stxvd2x 4, 5, 3
	li 3, 96
	stxvd2x 5, 5, 4
	li 4, 112
	stxvd2x 0, 5, 3
	stxvd2x 1, 5, 4
	li 3, 224
	lxvd2x 63, 1, 3
	li 3, 208
	lfd 31, 392(1)
	ld 30, 312(1)
	ld 29, 304(1)
	lxvd2x 62, 1, 3
	li 3, 192
	lfd 30, 384(1)
	ld 28, 296(1)
	ld 27, 288(1)
	lxvd2x 61, 1, 3
	li 3, 176
	lfd 29, 376(1)
	ld 26, 280(1)
	ld 25, 272(1)
	lxvd2x 60, 1, 3
	li 3, 160
	lfd 28, 368(1)
	ld 24, 264(1)
	ld 23, 256(1)
	lxvd2x 59, 1, 3
	li 3, 144
	lfd 27, 360(1)
	ld 22, 248(1)
	lxvd2x 58, 1, 3
	li 3, 128
	lfd 26, 352(1)
	lxvd2x 57, 1, 3
	li 3, 112
	lfd 25, 344(1)
	lxvd2x 56, 1, 3
	li 3, 96
	lfd 24, 336(1)
	lxvd2x 55, 1, 3
	li 3, 80
	lfd 23, 328(1)
	lxvd2x 54, 1, 3
	li 3, 64
	lxvd2x 53, 1, 3
	li 3, 48
	lxvd2x 52, 1, 3
	addi 1, 1, 400
	blr
	.long	0
	.quad	0
.Lfunc_end3:
	.size	blake3_hash4_sse2, .Lfunc_end3-.Lfunc_begin3
	.cfi_endproc
	.section	".note.GNU-stack","",@progbits
#endif
