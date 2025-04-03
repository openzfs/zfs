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
 * Copyright (c) 2022 Tino Reichardt <milky-zfs@mcmilk.de>
 *
 * This is converted assembly: SSE4.1 -> POWER8 PPC64 Little Endian
 * Used tools: SIMDe https://github.com/simd-everywhere/simde
 */

#if (defined(__PPC64__) && defined(__LITTLE_ENDIAN__))
	.text
	.abiversion 2
	.section	.rodata.cst16,"aM",@progbits,16
	.p2align	4
.LCPI0_0:
	.byte	31
	.byte	14
	.byte	13
	.byte	12
	.byte	30
	.byte	10
	.byte	9
	.byte	8
	.byte	29
	.byte	6
	.byte	5
	.byte	4
	.byte	28
	.byte	2
	.byte	1
	.byte	0
.LCPI0_1:
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
.LCPI0_2:
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
.LCPI0_3:
	.long	1779033703
	.long	3144134277
	.long	1013904242
	.long	2773480762
.LCPI0_4:
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
.LCPI0_5:
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
.LCPI0_6:
	.byte	30
	.byte	29
	.byte	28
	.byte	31
	.byte	26
	.byte	25
	.byte	24
	.byte	27
	.byte	22
	.byte	21
	.byte	20
	.byte	23
	.byte	18
	.byte	17
	.byte	16
	.byte	19
.LCPI0_7:
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
.LCPI0_8:
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
.LCPI0_9:
	.byte	31
	.byte	31
	.byte	31
	.byte	31
	.byte	23
	.byte	22
	.byte	21
	.byte	20
	.byte	31
	.byte	31
	.byte	31
	.byte	31
	.byte	3
	.byte	2
	.byte	1
	.byte	0
.LCPI0_10:
	.byte	19
	.byte	18
	.byte	17
	.byte	16
	.byte	31
	.byte	31
	.byte	31
	.byte	31
	.byte	31
	.byte	30
	.byte	29
	.byte	28
	.byte	31
	.byte	31
	.byte	31
	.byte	31
.LCPI0_11:
	.byte	31
	.byte	30
	.byte	29
	.byte	28
	.byte	11
	.byte	10
	.byte	9
	.byte	8
	.byte	23
	.byte	22
	.byte	21
	.byte	20
	.byte	3
	.byte	2
	.byte	1
	.byte	0
.LCPI0_12:
	.byte	31
	.byte	30
	.byte	29
	.byte	28
	.byte	23
	.byte	22
	.byte	21
	.byte	20
	.byte	3
	.byte	2
	.byte	1
	.byte	0
	.byte	27
	.byte	26
	.byte	25
	.byte	24
.LCPI0_13:
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
.LCPI0_14:
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
	.text
	.globl	zfs_blake3_compress_in_place_sse41
	.p2align	2
	.type	zfs_blake3_compress_in_place_sse41,@function
zfs_blake3_compress_in_place_sse41:
.Lfunc_begin0:
	.cfi_startproc
.Lfunc_gep0:
	addis 2, 12, .TOC.-.Lfunc_gep0@ha
	addi 2, 2, .TOC.-.Lfunc_gep0@l
.Lfunc_lep0:
	.localentry	zfs_blake3_compress_in_place_sse41, .Lfunc_lep0-.Lfunc_gep0
	li 8, -64
	mtvsrd 34, 5
	li 5, 16
	lfdx 0, 0, 4
	vspltisw 13, -16
	stxvd2x 60, 1, 8
	li 8, -48
	mtvsrd 35, 7
	lfd 2, 16(4)
	lfd 3, 24(4)
	addis 7, 2, .LCPI0_0@toc@ha
	stxvd2x 61, 1, 8
	li 8, -32
	mtvsrwz 36, 6
	rldicl 6, 6, 32, 32
	stxvd2x 62, 1, 8
	li 8, -16
	vmrghb 2, 3, 2
	stxvd2x 63, 1, 8
	mtvsrwz 35, 6
	addi 6, 7, .LCPI0_0@toc@l
	addis 7, 2, .LCPI0_2@toc@ha
	lfd 1, 8(4)
	xxmrghd	32, 3, 2
	lvx 6, 0, 6
	xxlxor 33, 33, 33
	addis 6, 2, .LCPI0_1@toc@ha
	addi 7, 7, .LCPI0_2@toc@l
	vmrghw 3, 3, 4
	addi 6, 6, .LCPI0_1@toc@l
	vspltisw 14, 9
	xxmrghd	37, 1, 0
	lxvd2x 0, 0, 3
	lxvd2x 1, 3, 5
	vperm 2, 1, 2, 6
	vpkudum 9, 0, 5
	xxswapd	36, 0
	xxswapd	38, 1
	xxmrgld	34, 34, 35
	lvx 3, 0, 7
	addis 7, 2, .LCPI0_4@toc@ha
	addi 7, 7, .LCPI0_4@toc@l
	vadduwm 4, 9, 4
	lvx 11, 0, 7
	addis 7, 2, .LCPI0_6@toc@ha
	addi 7, 7, .LCPI0_6@toc@l
	vadduwm 7, 4, 6
	lvx 4, 0, 6
	addis 6, 2, .LCPI0_3@toc@ha
	addi 6, 6, .LCPI0_3@toc@l
	vperm 11, 0, 5, 11
	lvx 0, 0, 7
	li 7, 48
	xxlxor 40, 39, 34
	lvx 10, 0, 6
	addis 6, 2, .LCPI0_5@toc@ha
	lxvd2x 1, 4, 7
	vcmpgtsb 2, 1, 4
	addi 6, 6, .LCPI0_5@toc@l
	vperm 4, 8, 8, 3
	vspltisw 8, 10
	xxlandc 44, 36, 34
	vadduwm 4, 8, 8
	vadduwm 8, 12, 10
	xxlxor 37, 40, 38
	vrlw 6, 5, 4
	vadduwm 5, 7, 11
	vadduwm 7, 6, 5
	lvx 5, 0, 6
	li 6, 32
	lxvd2x 0, 4, 6
	addis 4, 2, .LCPI0_7@toc@ha
	addis 6, 2, .LCPI0_9@toc@ha
	xxlxor 42, 39, 44
	xxswapd	44, 1
	addi 4, 4, .LCPI0_7@toc@l
	addi 6, 6, .LCPI0_9@toc@l
	vcmpgtsb 5, 1, 5
	vperm 1, 10, 10, 0
	xxswapd	42, 0
	vpkudum 16, 12, 10
	xxlandc 47, 33, 37
	vsubuwm 1, 14, 13
	lvx 14, 0, 4
	addis 4, 2, .LCPI0_8@toc@ha
	vadduwm 8, 15, 8
	xxswapd	45, 47
	addi 4, 4, .LCPI0_8@toc@l
	vadduwm 7, 7, 16
	xxsldwi 48, 48, 48, 1
	xxlxor 38, 40, 38
	xxsldwi 40, 40, 40, 3
	xxsldwi 39, 39, 39, 1
	vperm 14, 10, 12, 14
	vrlw 6, 6, 1
	vadduwm 7, 6, 7
	xxlxor 45, 39, 45
	vperm 13, 13, 13, 3
	xxlandc 45, 45, 34
	vadduwm 8, 13, 8
	xxlxor 38, 40, 38
	vrlw 10, 6, 4
	vadduwm 6, 7, 14
	vadduwm 7, 10, 6
	xxlxor 38, 39, 45
	vperm 12, 6, 6, 0
	lvx 6, 0, 4
	addis 4, 2, .LCPI0_10@toc@ha
	addi 4, 4, .LCPI0_10@toc@l
	vperm 13, 11, 9, 6
	xxlandc 44, 44, 37
	vadduwm 15, 12, 8
	vadduwm 7, 7, 13
	xxsldwi 45, 45, 45, 3
	xxlxor 40, 47, 42
	xxsldwi 47, 47, 47, 1
	xxsldwi 39, 39, 39, 3
	vrlw 10, 8, 1
	xxswapd	40, 44
	vadduwm 17, 10, 7
	lvx 7, 0, 4
	addis 4, 2, .LCPI0_11@toc@ha
	addi 4, 4, .LCPI0_11@toc@l
	xxlxor 44, 49, 40
	lvx 8, 0, 6
	vperm 18, 9, 9, 7
	lvx 9, 0, 4
	addis 4, 2, .LCPI0_12@toc@ha
	vperm 12, 12, 12, 3
	addi 4, 4, .LCPI0_12@toc@l
	vperm 19, 14, 16, 8
	xxlandc 63, 44, 34
	vperm 12, 19, 18, 9
	vadduwm 15, 31, 15
	xxlxor 42, 47, 42
	vrlw 18, 10, 4
	vadduwm 10, 17, 12
	vadduwm 17, 18, 10
	xxlxor 42, 49, 63
	xxmrgld	63, 43, 46
	xxsldwi 49, 49, 49, 1
	vmrghw 14, 14, 11
	vperm 19, 10, 10, 0
	lvx 10, 0, 4
	addis 4, 2, .LCPI0_13@toc@ha
	addi 4, 4, .LCPI0_13@toc@l
	lvx 11, 0, 4
	addis 4, 2, .LCPI0_14@toc@ha
	vperm 31, 16, 31, 10
	addi 4, 4, .LCPI0_14@toc@l
	vperm 14, 14, 16, 11
	xxlandc 51, 51, 37
	vadduwm 15, 19, 15
	xxswapd	51, 51
	vadduwm 17, 17, 31
	xxlxor 50, 47, 50
	xxsldwi 47, 47, 47, 3
	vperm 30, 14, 31, 8
	vrlw 18, 18, 1
	vadduwm 17, 18, 17
	xxlxor 51, 49, 51
	vadduwm 17, 17, 14
	vperm 19, 19, 19, 3
	xxlandc 51, 51, 34
	vadduwm 15, 19, 15
	xxlxor 48, 47, 50
	vrlw 16, 16, 4
	vadduwm 17, 16, 17
	xxlxor 50, 49, 51
	vperm 19, 12, 13, 6
	vperm 18, 18, 18, 0
	vperm 13, 13, 13, 7
	vadduwm 17, 17, 19
	xxlandc 50, 50, 37
	xxsldwi 49, 49, 49, 3
	vperm 13, 30, 13, 9
	vadduwm 15, 18, 15
	xxswapd	50, 50
	xxmrgld	62, 44, 46
	vmrghw 12, 14, 12
	xxlxor 48, 47, 48
	xxsldwi 47, 47, 47, 1
	vrlw 16, 16, 1
	vperm 30, 31, 30, 10
	vperm 12, 12, 31, 11
	vadduwm 17, 16, 17
	xxlxor 50, 49, 50
	vadduwm 17, 17, 13
	vperm 18, 18, 18, 3
	vperm 31, 12, 30, 8
	xxlandc 50, 50, 34
	vadduwm 15, 18, 15
	xxlxor 48, 47, 48
	vrlw 16, 16, 4
	vadduwm 17, 16, 17
	xxlxor 50, 49, 50
	xxsldwi 49, 49, 49, 1
	vperm 18, 18, 18, 0
	vadduwm 17, 17, 30
	xxlandc 50, 50, 37
	vadduwm 15, 18, 15
	xxswapd	50, 50
	xxlxor 48, 47, 48
	xxsldwi 46, 47, 47, 3
	vrlw 16, 16, 1
	vadduwm 17, 16, 17
	xxlxor 50, 49, 50
	vadduwm 17, 17, 12
	vperm 18, 18, 18, 3
	xxlandc 47, 50, 34
	xxsldwi 50, 51, 51, 3
	vadduwm 14, 15, 14
	vperm 19, 13, 18, 6
	xxlxor 48, 46, 48
	vperm 18, 18, 18, 7
	vrlw 16, 16, 4
	vadduwm 17, 16, 17
	xxlxor 47, 49, 47
	vadduwm 17, 17, 19
	vperm 15, 15, 15, 0
	xxsldwi 49, 49, 49, 3
	xxlandc 47, 47, 37
	vadduwm 14, 15, 14
	xxswapd	47, 47
	xxlxor 48, 46, 48
	xxsldwi 46, 46, 46, 1
	vrlw 16, 16, 1
	vadduwm 17, 16, 17
	xxlxor 47, 49, 47
	vperm 15, 15, 15, 3
	xxlandc 47, 47, 34
	vadduwm 29, 15, 14
	vperm 14, 31, 18, 9
	xxmrgld	50, 45, 44
	xxlxor 48, 61, 48
	vmrghw 12, 12, 13
	vrlw 16, 16, 4
	vperm 18, 30, 18, 10
	vadduwm 17, 17, 14
	vadduwm 17, 16, 17
	xxlxor 47, 49, 47
	xxsldwi 49, 49, 49, 1
	vperm 15, 15, 15, 0
	vadduwm 17, 17, 18
	xxlandc 47, 47, 37
	vadduwm 31, 15, 29
	xxswapd	47, 47
	xxlxor 48, 63, 48
	xxsldwi 45, 63, 63, 3
	vperm 31, 12, 30, 11
	vrlw 16, 16, 1
	vadduwm 17, 16, 17
	xxlxor 47, 49, 47
	vperm 15, 15, 15, 3
	xxlandc 47, 47, 34
	vadduwm 13, 15, 13
	xxlxor 44, 45, 48
	vadduwm 16, 17, 31
	xxsldwi 49, 51, 51, 3
	vrlw 12, 12, 4
	vperm 19, 14, 17, 6
	vadduwm 16, 12, 16
	xxlxor 47, 48, 47
	vperm 15, 15, 15, 0
	xxlandc 47, 47, 37
	vadduwm 13, 15, 13
	xxswapd	47, 47
	xxlxor 44, 45, 44
	xxsldwi 45, 45, 45, 1
	vrlw 30, 12, 1
	vadduwm 12, 16, 19
	xxsldwi 44, 44, 44, 3
	vadduwm 16, 30, 12
	xxlxor 44, 48, 47
	vperm 15, 17, 17, 7
	vperm 12, 12, 12, 3
	vperm 17, 31, 18, 8
	xxlandc 61, 44, 34
	vperm 12, 17, 15, 9
	vadduwm 13, 29, 13
	xxlxor 47, 45, 62
	xxmrgld	62, 46, 63
	vmrghw 14, 31, 14
	vrlw 15, 15, 4
	vadduwm 16, 16, 12
	vperm 30, 18, 30, 10
	vperm 14, 14, 18, 11
	xxsldwi 50, 51, 51, 3
	vadduwm 16, 15, 16
	xxlxor 49, 48, 61
	xxsldwi 48, 48, 48, 1
	vperm 19, 12, 18, 6
	vperm 17, 17, 17, 0
	vadduwm 16, 16, 30
	xxmrgld	60, 44, 46
	vmrghw 12, 14, 12
	vperm 28, 30, 28, 10
	xxlandc 49, 49, 37
	vadduwm 13, 17, 13
	xxswapd	49, 49
	vperm 12, 12, 30, 11
	xxlxor 47, 45, 47
	xxsldwi 45, 45, 45, 3
	vrlw 15, 15, 1
	vperm 8, 12, 28, 8
	vadduwm 16, 15, 16
	xxlxor 49, 48, 49
	vadduwm 16, 16, 14
	vperm 17, 17, 17, 3
	xxlandc 49, 49, 34
	vadduwm 13, 17, 13
	xxlxor 47, 45, 47
	vrlw 15, 15, 4
	vadduwm 16, 15, 16
	xxlxor 49, 48, 49
	vperm 17, 17, 17, 0
	xxlandc 49, 49, 37
	vadduwm 31, 17, 13
	xxlxor 45, 63, 47
	vrlw 15, 13, 1
	vadduwm 13, 16, 19
	xxswapd	48, 49
	xxsldwi 51, 51, 51, 3
	xxsldwi 45, 45, 45, 3
	vadduwm 17, 15, 13
	xxlxor 45, 49, 48
	lvx 16, 0, 4
	vperm 29, 13, 13, 3
	vperm 13, 18, 18, 7
	xxsldwi 50, 63, 63, 1
	vperm 16, 14, 30, 16
	vperm 7, 19, 19, 7
	xxlandc 63, 61, 34
	vadduwm 18, 31, 18
	vperm 29, 16, 13, 9
	xxlxor 47, 50, 47
	vperm 6, 16, 19, 6
	vrlw 15, 15, 4
	vperm 7, 8, 7, 9
	vadduwm 17, 17, 29
	xxmrgld	41, 61, 44
	vadduwm 17, 15, 17
	vperm 9, 28, 9, 10
	xxlxor 63, 49, 63
	xxsldwi 49, 49, 49, 1
	vperm 31, 31, 31, 0
	vadduwm 17, 17, 28
	xxlandc 63, 63, 37
	vadduwm 18, 31, 18
	xxswapd	63, 63
	xxlxor 47, 50, 47
	xxsldwi 46, 50, 50, 3
	vrlw 15, 15, 1
	vadduwm 17, 15, 17
	xxlxor 63, 49, 63
	vadduwm 17, 17, 12
	vperm 31, 31, 31, 3
	xxlandc 50, 63, 34
	vadduwm 14, 18, 14
	xxlxor 47, 46, 47
	vrlw 15, 15, 4
	vadduwm 17, 15, 17
	xxlxor 50, 49, 50
	vadduwm 6, 17, 6
	vperm 18, 18, 18, 0
	xxsldwi 38, 38, 38, 3
	xxlandc 50, 50, 37
	vadduwm 14, 18, 14
	xxswapd	48, 50
	xxlxor 47, 46, 47
	xxsldwi 46, 46, 46, 1
	vrlw 15, 15, 1
	vadduwm 6, 15, 6
	xxlxor 48, 38, 48
	vadduwm 6, 6, 7
	vperm 16, 16, 16, 3
	xxlandc 48, 48, 34
	vadduwm 14, 16, 14
	xxlxor 40, 46, 47
	vrlw 8, 8, 4
	vadduwm 6, 8, 6
	xxlxor 39, 38, 48
	xxsldwi 38, 38, 38, 1
	vperm 7, 7, 7, 0
	vadduwm 6, 6, 9
	xxlandc 39, 39, 37
	vadduwm 14, 7, 14
	xxswapd	39, 39
	xxlxor 40, 46, 40
	xxsldwi 41, 46, 46, 3
	vrlw 8, 8, 1
	vadduwm 6, 8, 6
	xxlxor 39, 38, 39
	vperm 3, 7, 7, 3
	vmrghw 7, 12, 13
	xxlandc 34, 35, 34
	vperm 7, 7, 28, 11
	vadduwm 3, 2, 9
	xxlxor 40, 35, 40
	vrlw 4, 8, 4
	vadduwm 6, 6, 7
	vadduwm 6, 4, 6
	xxlxor 34, 38, 34
	xxsldwi 0, 38, 38, 3
	vperm 2, 2, 2, 0
	xxlandc 34, 34, 37
	vadduwm 3, 2, 3
	xxswapd	34, 34
	xxlxor 36, 35, 36
	xxsldwi 1, 35, 35, 1
	vrlw 4, 4, 1
	xxlxor 0, 1, 0
	xxswapd	0, 0
	xxlxor 1, 36, 34
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
	.size	zfs_blake3_compress_in_place_sse41, .Lfunc_end0-.Lfunc_begin0
	.cfi_endproc

	.section	.rodata.cst16,"aM",@progbits,16
	.p2align	4
.LCPI1_0:
	.byte	31
	.byte	14
	.byte	13
	.byte	12
	.byte	30
	.byte	10
	.byte	9
	.byte	8
	.byte	29
	.byte	6
	.byte	5
	.byte	4
	.byte	28
	.byte	2
	.byte	1
	.byte	0
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
.LCPI1_3:
	.long	1779033703
	.long	3144134277
	.long	1013904242
	.long	2773480762
.LCPI1_4:
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
.LCPI1_5:
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
.LCPI1_6:
	.byte	30
	.byte	29
	.byte	28
	.byte	31
	.byte	26
	.byte	25
	.byte	24
	.byte	27
	.byte	22
	.byte	21
	.byte	20
	.byte	23
	.byte	18
	.byte	17
	.byte	16
	.byte	19
.LCPI1_7:
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
.LCPI1_8:
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
.LCPI1_9:
	.byte	31
	.byte	31
	.byte	31
	.byte	31
	.byte	23
	.byte	22
	.byte	21
	.byte	20
	.byte	31
	.byte	31
	.byte	31
	.byte	31
	.byte	3
	.byte	2
	.byte	1
	.byte	0
.LCPI1_10:
	.byte	19
	.byte	18
	.byte	17
	.byte	16
	.byte	31
	.byte	31
	.byte	31
	.byte	31
	.byte	31
	.byte	30
	.byte	29
	.byte	28
	.byte	31
	.byte	31
	.byte	31
	.byte	31
.LCPI1_11:
	.byte	31
	.byte	30
	.byte	29
	.byte	28
	.byte	11
	.byte	10
	.byte	9
	.byte	8
	.byte	23
	.byte	22
	.byte	21
	.byte	20
	.byte	3
	.byte	2
	.byte	1
	.byte	0
.LCPI1_12:
	.byte	31
	.byte	30
	.byte	29
	.byte	28
	.byte	23
	.byte	22
	.byte	21
	.byte	20
	.byte	3
	.byte	2
	.byte	1
	.byte	0
	.byte	27
	.byte	26
	.byte	25
	.byte	24
.LCPI1_13:
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
.LCPI1_14:
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
	.text
	.globl	zfs_blake3_compress_xof_sse41
	.p2align	2
	.type	zfs_blake3_compress_xof_sse41,@function
zfs_blake3_compress_xof_sse41:
.Lfunc_begin1:
	.cfi_startproc
.Lfunc_gep1:
	addis 2, 12, .TOC.-.Lfunc_gep1@ha
	addi 2, 2, .TOC.-.Lfunc_gep1@l
.Lfunc_lep1:
	.localentry	zfs_blake3_compress_xof_sse41, .Lfunc_lep1-.Lfunc_gep1
	li 9, -64
	mtvsrd 34, 5
	li 5, 16
	lfdx 0, 0, 4
	vspltisw 13, -16
	addis 11, 2, .LCPI1_9@toc@ha
	stxvd2x 60, 1, 9
	li 9, -48
	mtvsrd 35, 7
	lfd 1, 8(4)
	lfd 2, 16(4)
	addis 7, 2, .LCPI1_0@toc@ha
	stxvd2x 61, 1, 9
	li 9, -32
	mtvsrwz 36, 6
	rldicl 6, 6, 32, 32
	stxvd2x 62, 1, 9
	li 9, -16
	vmrghb 2, 3, 2
	stxvd2x 63, 1, 9
	mtvsrwz 35, 6
	addi 6, 7, .LCPI1_0@toc@l
	addis 7, 2, .LCPI1_2@toc@ha
	lfd 3, 24(4)
	xxmrghd	37, 1, 0
	lvx 6, 0, 6
	xxlxor 33, 33, 33
	lxvd2x 0, 0, 3
	addis 6, 2, .LCPI1_1@toc@ha
	addi 7, 7, .LCPI1_2@toc@l
	vmrghw 3, 3, 4
	lxvd2x 1, 3, 5
	addi 6, 6, .LCPI1_1@toc@l
	vspltisw 14, 9
	xxmrghd	32, 3, 2
	xxswapd	36, 0
	vperm 2, 1, 2, 6
	xxswapd	38, 1
	vpkudum 9, 0, 5
	xxmrgld	34, 34, 35
	lvx 3, 0, 7
	addis 7, 2, .LCPI1_4@toc@ha
	addi 7, 7, .LCPI1_4@toc@l
	vadduwm 4, 9, 4
	lvx 11, 0, 7
	addis 7, 2, .LCPI1_6@toc@ha
	addi 7, 7, .LCPI1_6@toc@l
	vadduwm 7, 4, 6
	lvx 4, 0, 6
	addis 6, 2, .LCPI1_3@toc@ha
	addi 6, 6, .LCPI1_3@toc@l
	vperm 11, 0, 5, 11
	lvx 0, 0, 7
	li 7, 32
	xxlxor 40, 39, 34
	lvx 10, 0, 6
	addis 6, 2, .LCPI1_5@toc@ha
	lxvd2x 0, 4, 7
	vcmpgtsb 2, 1, 4
	addi 6, 6, .LCPI1_5@toc@l
	vperm 4, 8, 8, 3
	vspltisw 8, 10
	xxlandc 44, 36, 34
	vadduwm 4, 8, 8
	vadduwm 8, 12, 10
	xxlxor 37, 40, 38
	vrlw 6, 5, 4
	vadduwm 5, 7, 11
	vadduwm 7, 6, 5
	lvx 5, 0, 6
	li 6, 48
	lxvd2x 1, 4, 6
	addis 4, 2, .LCPI1_7@toc@ha
	xxlxor 42, 39, 44
	addi 4, 4, .LCPI1_7@toc@l
	vcmpgtsb 5, 1, 5
	vperm 1, 10, 10, 0
	xxswapd	42, 0
	xxswapd	44, 1
	vpkudum 16, 12, 10
	xxlandc 47, 33, 37
	vsubuwm 1, 14, 13
	lvx 14, 0, 4
	addis 4, 2, .LCPI1_8@toc@ha
	vadduwm 8, 15, 8
	xxswapd	45, 47
	addi 4, 4, .LCPI1_8@toc@l
	xxlxor 38, 40, 38
	xxsldwi 40, 40, 40, 3
	vadduwm 7, 7, 16
	xxsldwi 48, 48, 48, 1
	vrlw 6, 6, 1
	xxsldwi 39, 39, 39, 1
	vperm 14, 10, 12, 14
	vadduwm 7, 6, 7
	xxlxor 45, 39, 45
	vperm 13, 13, 13, 3
	xxlandc 45, 45, 34
	vadduwm 8, 13, 8
	xxlxor 38, 40, 38
	vrlw 10, 6, 4
	vadduwm 6, 7, 14
	vadduwm 7, 10, 6
	xxlxor 38, 39, 45
	vperm 12, 6, 6, 0
	lvx 6, 0, 4
	addis 4, 2, .LCPI1_10@toc@ha
	addi 4, 4, .LCPI1_10@toc@l
	vperm 13, 11, 9, 6
	xxlandc 44, 44, 37
	vadduwm 15, 12, 8
	vadduwm 7, 7, 13
	xxsldwi 45, 45, 45, 3
	xxlxor 40, 47, 42
	xxsldwi 47, 47, 47, 1
	xxsldwi 39, 39, 39, 3
	vrlw 10, 8, 1
	xxswapd	40, 44
	vadduwm 17, 10, 7
	lvx 7, 0, 4
	addi 4, 11, .LCPI1_9@toc@l
	xxlxor 44, 49, 40
	lvx 8, 0, 4
	addis 4, 2, .LCPI1_11@toc@ha
	vperm 18, 9, 9, 7
	addi 4, 4, .LCPI1_11@toc@l
	vperm 12, 12, 12, 3
	lvx 9, 0, 4
	addis 4, 2, .LCPI1_12@toc@ha
	vperm 19, 14, 16, 8
	addi 4, 4, .LCPI1_12@toc@l
	xxlandc 63, 44, 34
	vperm 12, 19, 18, 9
	vadduwm 15, 31, 15
	xxlxor 42, 47, 42
	vrlw 18, 10, 4
	vadduwm 10, 17, 12
	vadduwm 17, 18, 10
	xxlxor 42, 49, 63
	xxmrgld	63, 43, 46
	xxsldwi 49, 49, 49, 1
	vmrghw 14, 14, 11
	vperm 19, 10, 10, 0
	lvx 10, 0, 4
	addis 4, 2, .LCPI1_13@toc@ha
	addi 4, 4, .LCPI1_13@toc@l
	lvx 11, 0, 4
	addis 4, 2, .LCPI1_14@toc@ha
	vperm 31, 16, 31, 10
	addi 4, 4, .LCPI1_14@toc@l
	vperm 14, 14, 16, 11
	xxlandc 51, 51, 37
	vadduwm 15, 19, 15
	xxswapd	51, 51
	vadduwm 17, 17, 31
	xxlxor 50, 47, 50
	xxsldwi 47, 47, 47, 3
	vperm 30, 14, 31, 8
	vrlw 18, 18, 1
	vadduwm 17, 18, 17
	xxlxor 51, 49, 51
	vadduwm 17, 17, 14
	vperm 19, 19, 19, 3
	xxlandc 51, 51, 34
	vadduwm 15, 19, 15
	xxlxor 48, 47, 50
	vrlw 16, 16, 4
	vadduwm 17, 16, 17
	xxlxor 50, 49, 51
	vperm 19, 12, 13, 6
	vperm 18, 18, 18, 0
	vperm 13, 13, 13, 7
	vadduwm 17, 17, 19
	xxlandc 50, 50, 37
	xxsldwi 49, 49, 49, 3
	vperm 13, 30, 13, 9
	vadduwm 15, 18, 15
	xxswapd	50, 50
	xxmrgld	62, 44, 46
	vmrghw 12, 14, 12
	xxlxor 48, 47, 48
	xxsldwi 47, 47, 47, 1
	vrlw 16, 16, 1
	vperm 30, 31, 30, 10
	vperm 12, 12, 31, 11
	vadduwm 17, 16, 17
	xxlxor 50, 49, 50
	vadduwm 17, 17, 13
	vperm 18, 18, 18, 3
	vperm 31, 12, 30, 8
	xxlandc 50, 50, 34
	vadduwm 15, 18, 15
	xxlxor 48, 47, 48
	vrlw 16, 16, 4
	vadduwm 17, 16, 17
	xxlxor 50, 49, 50
	xxsldwi 49, 49, 49, 1
	vperm 18, 18, 18, 0
	vadduwm 17, 17, 30
	xxlandc 50, 50, 37
	vadduwm 15, 18, 15
	xxswapd	50, 50
	xxlxor 48, 47, 48
	xxsldwi 46, 47, 47, 3
	vrlw 16, 16, 1
	vadduwm 17, 16, 17
	xxlxor 50, 49, 50
	vadduwm 17, 17, 12
	vperm 18, 18, 18, 3
	xxlandc 47, 50, 34
	xxsldwi 50, 51, 51, 3
	vadduwm 14, 15, 14
	vperm 19, 13, 18, 6
	xxlxor 48, 46, 48
	vperm 18, 18, 18, 7
	vrlw 16, 16, 4
	vadduwm 17, 16, 17
	xxlxor 47, 49, 47
	vadduwm 17, 17, 19
	vperm 15, 15, 15, 0
	xxsldwi 49, 49, 49, 3
	xxlandc 47, 47, 37
	vadduwm 14, 15, 14
	xxswapd	47, 47
	xxlxor 48, 46, 48
	xxsldwi 46, 46, 46, 1
	vrlw 16, 16, 1
	vadduwm 17, 16, 17
	xxlxor 47, 49, 47
	vperm 15, 15, 15, 3
	xxlandc 47, 47, 34
	vadduwm 29, 15, 14
	vperm 14, 31, 18, 9
	xxmrgld	50, 45, 44
	xxlxor 48, 61, 48
	vmrghw 12, 12, 13
	vrlw 16, 16, 4
	vperm 18, 30, 18, 10
	vadduwm 17, 17, 14
	vadduwm 17, 16, 17
	xxlxor 47, 49, 47
	xxsldwi 49, 49, 49, 1
	vperm 15, 15, 15, 0
	vadduwm 17, 17, 18
	xxlandc 47, 47, 37
	vadduwm 31, 15, 29
	xxswapd	47, 47
	xxlxor 48, 63, 48
	xxsldwi 45, 63, 63, 3
	vperm 31, 12, 30, 11
	vrlw 16, 16, 1
	vadduwm 17, 16, 17
	xxlxor 47, 49, 47
	vperm 15, 15, 15, 3
	xxlandc 47, 47, 34
	vadduwm 13, 15, 13
	xxlxor 44, 45, 48
	vadduwm 16, 17, 31
	xxsldwi 49, 51, 51, 3
	vrlw 12, 12, 4
	vperm 19, 14, 17, 6
	vadduwm 16, 12, 16
	xxlxor 47, 48, 47
	vperm 15, 15, 15, 0
	xxlandc 47, 47, 37
	vadduwm 13, 15, 13
	xxswapd	47, 47
	xxlxor 44, 45, 44
	xxsldwi 45, 45, 45, 1
	vrlw 30, 12, 1
	vadduwm 12, 16, 19
	xxsldwi 44, 44, 44, 3
	vadduwm 16, 30, 12
	xxlxor 44, 48, 47
	vperm 15, 17, 17, 7
	vperm 12, 12, 12, 3
	vperm 17, 31, 18, 8
	xxlandc 61, 44, 34
	vperm 12, 17, 15, 9
	vadduwm 13, 29, 13
	xxlxor 47, 45, 62
	xxmrgld	62, 46, 63
	vmrghw 14, 31, 14
	vrlw 15, 15, 4
	vadduwm 16, 16, 12
	vperm 30, 18, 30, 10
	vperm 14, 14, 18, 11
	xxsldwi 50, 51, 51, 3
	vadduwm 16, 15, 16
	xxlxor 49, 48, 61
	xxsldwi 48, 48, 48, 1
	vperm 19, 12, 18, 6
	vperm 17, 17, 17, 0
	vadduwm 16, 16, 30
	xxmrgld	60, 44, 46
	vmrghw 12, 14, 12
	vperm 28, 30, 28, 10
	xxlandc 49, 49, 37
	vadduwm 13, 17, 13
	xxswapd	49, 49
	vperm 12, 12, 30, 11
	xxlxor 47, 45, 47
	xxsldwi 45, 45, 45, 3
	vrlw 15, 15, 1
	vperm 8, 12, 28, 8
	vadduwm 16, 15, 16
	xxlxor 49, 48, 49
	vadduwm 16, 16, 14
	vperm 17, 17, 17, 3
	xxlandc 49, 49, 34
	vadduwm 13, 17, 13
	xxlxor 47, 45, 47
	vrlw 15, 15, 4
	vadduwm 16, 15, 16
	xxlxor 49, 48, 49
	vperm 17, 17, 17, 0
	xxlandc 49, 49, 37
	vadduwm 31, 17, 13
	xxlxor 45, 63, 47
	vrlw 15, 13, 1
	vadduwm 13, 16, 19
	xxswapd	48, 49
	xxsldwi 51, 51, 51, 3
	xxsldwi 45, 45, 45, 3
	vadduwm 17, 15, 13
	xxlxor 45, 49, 48
	lvx 16, 0, 4
	vperm 29, 13, 13, 3
	vperm 13, 18, 18, 7
	xxsldwi 50, 63, 63, 1
	vperm 16, 14, 30, 16
	vperm 7, 19, 19, 7
	xxlandc 63, 61, 34
	vadduwm 18, 31, 18
	vperm 29, 16, 13, 9
	xxlxor 47, 50, 47
	vperm 6, 16, 19, 6
	vrlw 15, 15, 4
	vperm 7, 8, 7, 9
	vadduwm 17, 17, 29
	xxmrgld	41, 61, 44
	vadduwm 17, 15, 17
	vperm 9, 28, 9, 10
	xxlxor 63, 49, 63
	xxsldwi 49, 49, 49, 1
	vperm 31, 31, 31, 0
	vadduwm 17, 17, 28
	xxlandc 63, 63, 37
	vadduwm 18, 31, 18
	xxswapd	63, 63
	xxlxor 47, 50, 47
	xxsldwi 46, 50, 50, 3
	vrlw 15, 15, 1
	vadduwm 17, 15, 17
	xxlxor 63, 49, 63
	vadduwm 17, 17, 12
	vperm 31, 31, 31, 3
	xxlandc 50, 63, 34
	vadduwm 14, 18, 14
	xxlxor 47, 46, 47
	vrlw 15, 15, 4
	vadduwm 17, 15, 17
	xxlxor 50, 49, 50
	vadduwm 6, 17, 6
	vperm 18, 18, 18, 0
	xxsldwi 38, 38, 38, 3
	xxlandc 50, 50, 37
	vadduwm 14, 18, 14
	xxswapd	48, 50
	xxlxor 47, 46, 47
	xxsldwi 46, 46, 46, 1
	vrlw 15, 15, 1
	vadduwm 6, 15, 6
	xxlxor 48, 38, 48
	vadduwm 6, 6, 7
	vperm 16, 16, 16, 3
	xxlandc 48, 48, 34
	vadduwm 14, 16, 14
	xxlxor 40, 46, 47
	vrlw 8, 8, 4
	vadduwm 6, 8, 6
	xxlxor 39, 38, 48
	xxsldwi 38, 38, 38, 1
	vperm 7, 7, 7, 0
	vadduwm 6, 6, 9
	xxlandc 39, 39, 37
	vadduwm 14, 7, 14
	xxswapd	39, 39
	xxlxor 40, 46, 40
	xxsldwi 41, 46, 46, 3
	vrlw 8, 8, 1
	vadduwm 6, 8, 6
	xxlxor 39, 38, 39
	vperm 3, 7, 7, 3
	vmrghw 7, 12, 13
	xxlandc 34, 35, 34
	vperm 7, 7, 28, 11
	vadduwm 3, 2, 9
	xxlxor 40, 35, 40
	vrlw 4, 8, 4
	vadduwm 6, 6, 7
	vadduwm 6, 4, 6
	xxlxor 34, 38, 34
	xxsldwi 0, 38, 38, 3
	vperm 2, 2, 2, 0
	xxlandc 34, 34, 37
	vadduwm 3, 2, 3
	xxswapd	34, 34
	xxlxor 36, 35, 36
	xxsldwi 1, 35, 35, 1
	vrlw 4, 4, 1
	xxlxor 0, 1, 0
	xxswapd	0, 0
	xxlxor 2, 36, 34
	stxvd2x 0, 0, 8
	xxswapd	2, 2
	stxvd2x 2, 8, 5
	lfdx 0, 0, 3
	lfd 2, 8(3)
	xxmrghd	35, 2, 0
	xxlxor 0, 1, 35
	xxswapd	0, 0
	stxvd2x 0, 8, 7
	lfd 0, 16(3)
	lfd 1, 24(3)
	li 3, -16
	xxmrghd	35, 1, 0
	xxlxor 0, 34, 35
	xxswapd	0, 0
	stxvd2x 0, 8, 6
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
.Lfunc_end1:
	.size	zfs_blake3_compress_xof_sse41, .Lfunc_end1-.Lfunc_begin1
	.cfi_endproc

	.globl	zfs_blake3_hash_many_sse41
	.p2align	2
	.type	zfs_blake3_hash_many_sse41,@function
zfs_blake3_hash_many_sse41:
.Lfunc_begin2:
	.cfi_startproc
.Lfunc_gep2:
	addis 2, 12, .TOC.-.Lfunc_gep2@ha
	addi 2, 2, .TOC.-.Lfunc_gep2@l
.Lfunc_lep2:
	.localentry	zfs_blake3_hash_many_sse41, .Lfunc_lep2-.Lfunc_gep2
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
	bl blake3_hash4_sse41
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
	bl zfs_blake3_compress_in_place_sse41
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
	.size	zfs_blake3_hash_many_sse41, .Lfunc_end2-.Lfunc_begin2
	.cfi_endproc

	.section	.rodata.cst16,"aM",@progbits,16
	.p2align	4
.LCPI3_0:
	.quad	4294967296
	.quad	12884901890
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
.LCPI3_4:
	.long	1779033703
	.long	1779033703
	.long	1779033703
	.long	1779033703
.LCPI3_5:
	.long	3144134277
	.long	3144134277
	.long	3144134277
	.long	3144134277
.LCPI3_6:
	.long	1013904242
	.long	1013904242
	.long	1013904242
	.long	1013904242
.LCPI3_7:
	.long	2773480762
	.long	2773480762
	.long	2773480762
	.long	2773480762
.LCPI3_8:
	.byte	30
	.byte	29
	.byte	28
	.byte	31
	.byte	26
	.byte	25
	.byte	24
	.byte	27
	.byte	22
	.byte	21
	.byte	20
	.byte	23
	.byte	18
	.byte	17
	.byte	16
	.byte	19
	.text
	.p2align	2
	.type	blake3_hash4_sse41,@function
blake3_hash4_sse41:
.Lfunc_begin3:
	.cfi_startproc
.Lfunc_gep3:
	addis 2, 12, .TOC.-.Lfunc_gep3@ha
	addi 2, 2, .TOC.-.Lfunc_gep3@l
.Lfunc_lep3:
	.localentry	blake3_hash4_sse41, .Lfunc_lep3-.Lfunc_gep3
	stdu 1, -416(1)
	.cfi_def_cfa_offset 416
	.cfi_offset r22, -176
	.cfi_offset r23, -168
	.cfi_offset r24, -160
	.cfi_offset r25, -152
	.cfi_offset r26, -144
	.cfi_offset r27, -136
	.cfi_offset r28, -128
	.cfi_offset r29, -120
	.cfi_offset r30, -112
	.cfi_offset f20, -96
	.cfi_offset f21, -88
	.cfi_offset f22, -80
	.cfi_offset f23, -72
	.cfi_offset f24, -64
	.cfi_offset f25, -56
	.cfi_offset f26, -48
	.cfi_offset f27, -40
	.cfi_offset f28, -32
	.cfi_offset f29, -24
	.cfi_offset f30, -16
	.cfi_offset f31, -8
	.cfi_offset v20, -368
	.cfi_offset v21, -352
	.cfi_offset v22, -336
	.cfi_offset v23, -320
	.cfi_offset v24, -304
	.cfi_offset v25, -288
	.cfi_offset v26, -272
	.cfi_offset v27, -256
	.cfi_offset v28, -240
	.cfi_offset v29, -224
	.cfi_offset v30, -208
	.cfi_offset v31, -192
	li 11, 48
	li 0, 8
	std 30, 304(1)
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
	xxspltw 47, 0, 1
	cmpldi	4, 0
	std 22, 240(1)
	stxvd2x 55, 1, 11
	li 11, 112
	lfiwzx 7, 5, 0
	xxspltw 40, 2, 1
	std 23, 248(1)
	xxspltw 39, 3, 1
	std 24, 256(1)
	std 25, 264(1)
	xxspltw 51, 1, 1
	xxspltw 43, 6, 1
	std 26, 272(1)
	xxspltw 41, 7, 1
	std 27, 280(1)
	std 28, 288(1)
	std 29, 296(1)
	stxvd2x 56, 1, 11
	li 11, 128
	stfd 20, 320(1)
	stxvd2x 57, 1, 11
	li 11, 144
	stfd 21, 328(1)
	stxvd2x 58, 1, 11
	li 11, 160
	stfd 22, 336(1)
	stxvd2x 59, 1, 11
	li 11, 176
	stfd 23, 344(1)
	stxvd2x 60, 1, 11
	li 11, 192
	stfd 24, 352(1)
	stxvd2x 61, 1, 11
	li 11, 208
	stfd 25, 360(1)
	stxvd2x 62, 1, 11
	li 11, 224
	stfd 26, 368(1)
	stxvd2x 63, 1, 11
	li 11, 16
	xxspltw 63, 4, 1
	lfiwzx 5, 5, 11
	ld 5, 448(1)
	stfd 27, 376(1)
	stfd 28, 384(1)
	stfd 29, 392(1)
	stfd 30, 400(1)
	stfd 31, 408(1)
	xxspltw 50, 5, 1
	beq	0, .LBB3_5
	addis 30, 2, .LCPI3_0@toc@ha
	neg 7, 7
	xxleqv 34, 34, 34
	addis 28, 2, .LCPI3_5@toc@ha
	addis 27, 2, .LCPI3_6@toc@ha
	addis 26, 2, .LCPI3_7@toc@ha
	addis 29, 2, .LCPI3_4@toc@ha
	addis 25, 2, .LCPI3_8@toc@ha
	addi 0, 30, .LCPI3_0@toc@l
	mtfprwz	2, 7
	addis 7, 2, .LCPI3_1@toc@ha
	addis 30, 2, .LCPI3_3@toc@ha
	addi 24, 29, .LCPI3_4@toc@l
	ld 29, 24(3)
	lxvd2x 1, 0, 0
	mtfprwz	0, 6
	rldicl 6, 6, 32, 32
	addi 0, 30, .LCPI3_3@toc@l
	ld 30, 16(3)
	xxspltw 2, 2, 1
	vslw 2, 2, 2
	xxspltw 37, 0, 1
	mtfprwz	0, 6
	addi 6, 7, .LCPI3_1@toc@l
	addis 7, 2, .LCPI3_2@toc@ha
	xxswapd	35, 1
	xxlxor 36, 36, 36
	xxspltw 33, 0, 1
	xxland 35, 2, 35
	vadduwm 0, 3, 5
	lvx 5, 0, 6
	addi 6, 7, .LCPI3_2@toc@l
	ld 7, 8(3)
	xxlor 35, 35, 34
	xxlxor 34, 32, 34
	xxlor 9, 32, 32
	lvx 0, 0, 6
	ld 6, 0(3)
	addi 3, 3, -8
	vcmpgtsw 2, 3, 2
	lvx 3, 0, 0
	addi 0, 28, .LCPI3_5@toc@l
	addi 28, 27, .LCPI3_6@toc@l
	addi 27, 26, .LCPI3_7@toc@l
	addi 26, 25, .LCPI3_8@toc@l
	or 25, 9, 8
	li 9, 0
	vcmpgtsb 5, 4, 5
	vcmpgtsb 0, 4, 0
	xxlor 11, 35, 35
	lvx 3, 0, 24
	xxlor 12, 35, 35
	vsubuwm 2, 1, 2
	xxlnor 10, 37, 37
	xxlor 13, 34, 34
	lvx 2, 0, 0
	li 0, 32
	xxlnor 31, 32, 32
	xxlor 30, 34, 34
	lvx 2, 0, 28
	li 28, 48
	xxlor 29, 34, 34
	lvx 2, 0, 27
	li 27, 0
	xxlor 28, 34, 34
	lvx 2, 0, 26
	xxlor 27, 34, 34
.LBB3_2:
	mr	26, 27
	addi 27, 27, 1
	xxlor 23, 39, 39
	cmpld	27, 4
	sldi 26, 26, 6
	xxlor 24, 40, 40
	iseleq	24, 10, 9
	add 23, 6, 26
	add 22, 30, 26
	lxvd2x 0, 6, 26
	lxvd2x 1, 7, 26
	or 25, 24, 25
	add 24, 7, 26
	lxvd2x 2, 30, 26
	lxvd2x 3, 29, 26
	xxlor 26, 47, 47
	lxvd2x 4, 23, 11
	lxvd2x 6, 24, 11
	clrlwi	25, 25, 24
	xxlor 25, 51, 51
	lxvd2x 7, 22, 11
	lxvd2x 8, 23, 0
	mtfprd	5, 25
	add 25, 29, 26
	xxswapd	34, 0
	lxvd2x 0, 25, 11
	xxswapd	38, 1
	xxswapd	32, 2
	lxvd2x 1, 24, 0
	lxvd2x 2, 22, 0
	xxswapd	40, 3
	xxswapd	39, 4
	lxvd2x 3, 25, 0
	lxvd2x 4, 23, 28
	xxswapd	60, 6
	xxswapd	47, 7
	lxvd2x 6, 24, 28
	xxswapd	57, 8
	lxvd2x 7, 22, 28
	lxvd2x 8, 25, 28
	xxswapd	58, 0
	mr	25, 3
	xxswapd	53, 1
	xxswapd	56, 2
	xxswapd	52, 3
	xxswapd	55, 4
	xxswapd	54, 6
	xxswapd	0, 5
	xxswapd	42, 7
	xxswapd	48, 8
	mtctr 12
.LBB3_3:
	ldu 24, 8(25)
	add 24, 24, 26
	addi 24, 24, 256
	dcbt 0, 24
	bdnz .LBB3_3
	vmrgew 4, 28, 7
	vspltisw 14, 9
	mr	25, 8
	vmrgew 27, 6, 2
	vspltisw 17, 4
	vmrglw 12, 6, 2
	vspltisw 19, 10
	vmrghw 30, 6, 2
	xxspltw 0, 0, 3
	vmrglw 2, 8, 0
	vmrghw 13, 8, 0
	xxlor 7, 36, 36
	vmrgew 4, 21, 25
	vmrglw 29, 28, 7
	vmrghw 1, 28, 7
	vmrglw 28, 26, 15
	xxmrgld	37, 34, 44
	vmrgew 7, 26, 15
	vmrghw 15, 26, 15
	xxlor 21, 36, 36
	vmrglw 4, 21, 25
	vmrghw 21, 21, 25
	vmrglw 25, 20, 24
	xxmrgld	34, 60, 61
	vmrghw 26, 20, 24
	xxlor 38, 26, 26
	vmrgew 3, 8, 0
	xxlor 5, 36, 36
	vmrgew 4, 20, 24
	vspltisw 24, -16
	vmrglw 20, 22, 23
	xxmrgld	57, 57, 5
	vmrglw 8, 16, 10
	vmrghw 0, 16, 10
	vadduwm 12, 19, 19
	xxlor 8, 37, 37
	xxlor 20, 36, 36
	vmrgew 4, 22, 23
	vmrghw 23, 22, 23
	xxmrgld	40, 40, 52
	vmrgew 22, 16, 10
	vsubuwm 10, 14, 24
	vslw 14, 17, 17
	vadduwm 17, 5, 6
	xxmrgld	37, 47, 33
	xxlor 22, 36, 36
	xxmrgld	36, 45, 62
	xxlor 38, 25, 25
	xxlor 2, 34, 34
	vadduwm 19, 4, 6
	xxmrgld	38, 39, 7
	xxlor 3, 36, 36
	xxmrghd	39, 47, 33
	xxlor 36, 24, 24
	xxmrgld	33, 58, 53
	vadduwm 17, 17, 18
	vadduwm 29, 2, 4
	xxmrgld	36, 35, 59
	xxlor 34, 23, 23
	xxmrghd	35, 45, 62
	xxlor 1, 9, 9
	vadduwm 28, 5, 2
	xxlor 1, 13, 13
	vadduwm 19, 19, 31
	vadduwm 24, 29, 11
	vadduwm 28, 28, 9
	xxlxor 61, 49, 9
	xxlor 1, 41, 41
	xxlor 41, 11, 11
	xxlxor 34, 51, 13
	vperm 29, 29, 29, 9
	xxlxor 46, 56, 46
	vperm 2, 2, 2, 9
	xxlxor 59, 60, 0
	vperm 14, 14, 14, 9
	vperm 30, 27, 27, 9
	vadduwm 19, 19, 3
	xxlor 4, 35, 35
	xxland 61, 61, 10
	xxlor 35, 12, 12
	xxland 34, 34, 10
	vadduwm 27, 29, 3
	xxlor 35, 30, 30
	vadduwm 17, 17, 4
	xxlor 26, 36, 36
	xxland 46, 46, 10
	vadduwm 3, 2, 3
	xxlor 36, 29, 29
	xxland 62, 62, 10
	xxlxor 45, 59, 50
	xxlxor 50, 35, 63
	vadduwm 31, 14, 4
	xxlor 36, 28, 28
	xxlor 6, 37, 37
	vadduwm 16, 30, 4
	xxlxor 43, 63, 43
	xxlxor 37, 48, 1
	vrlw 4, 13, 12
	vrlw 18, 18, 12
	vrlw 11, 11, 12
	vrlw 5, 5, 12
	vadduwm 15, 24, 6
	vadduwm 28, 28, 7
	vadduwm 17, 4, 17
	vadduwm 19, 18, 19
	vadduwm 15, 11, 15
	vadduwm 28, 5, 28
	xxlor 25, 38, 38
	xxlxor 61, 49, 61
	xxlxor 34, 51, 34
	xxlxor 46, 47, 46
	xxlxor 62, 60, 62
	xxlor 38, 27, 27
	vadduwm 19, 19, 1
	vperm 29, 29, 29, 6
	vperm 2, 2, 2, 6
	vperm 24, 14, 14, 6
	vperm 30, 30, 30, 6
	xxlor 5, 33, 33
	vadduwm 17, 17, 25
	xxland 61, 61, 31
	xxland 34, 34, 31
	xxland 56, 56, 31
	xxland 62, 62, 31
	vadduwm 27, 29, 27
	vadduwm 3, 2, 3
	vadduwm 31, 24, 31
	vadduwm 16, 30, 16
	xxlxor 36, 59, 36
	xxlxor 50, 35, 50
	xxlxor 43, 63, 43
	xxlxor 37, 48, 37
	vrlw 1, 18, 10
	xxmrgld	50, 32, 55
	vrlw 11, 11, 10
	xxmrghd	55, 32, 55
	vrlw 5, 5, 10
	vrlw 4, 4, 10
	vadduwm 15, 15, 8
	vadduwm 28, 28, 18
	vadduwm 17, 1, 17
	vadduwm 19, 11, 19
	vadduwm 15, 5, 15
	vadduwm 28, 4, 28
	xxlor 7, 57, 57
	xxlxor 62, 49, 62
	xxlxor 61, 51, 61
	xxlxor 57, 47, 34
	xxlxor 34, 60, 56
	vperm 24, 30, 30, 9
	xxmrgld	62, 20, 21
	vperm 29, 29, 29, 9
	vperm 25, 25, 25, 9
	vperm 2, 2, 2, 9
	vmr	14, 8
	xxmrghd	40, 58, 53
	xxmrgld	58, 54, 22
	vadduwm 17, 17, 30
	xxland 56, 56, 10
	vadduwm 21, 19, 8
	xxland 61, 61, 10
	xxland 51, 57, 10
	xxland 34, 34, 10
	vadduwm 31, 24, 31
	vadduwm 16, 29, 16
	vadduwm 27, 19, 27
	vadduwm 3, 2, 3
	xxlxor 33, 63, 33
	xxlxor 43, 48, 43
	xxlxor 37, 59, 37
	xxlxor 36, 35, 36
	vrlw 1, 1, 12
	vrlw 11, 11, 12
	vrlw 5, 5, 12
	vrlw 4, 4, 12
	vadduwm 0, 15, 26
	vadduwm 15, 28, 23
	vadduwm 17, 1, 17
	vadduwm 28, 11, 21
	vadduwm 0, 5, 0
	vadduwm 15, 4, 15
	xxlxor 56, 49, 56
	xxlxor 61, 60, 61
	xxlxor 51, 32, 51
	xxlxor 34, 47, 34
	vperm 24, 24, 24, 6
	vperm 29, 29, 29, 6
	vperm 19, 19, 19, 6
	vperm 2, 2, 2, 6
	vmr	13, 8
	xxlor 53, 3, 3
	xxland 56, 56, 31
	xxland 61, 61, 31
	xxland 51, 51, 31
	xxland 34, 34, 31
	vadduwm 31, 24, 31
	vadduwm 16, 29, 16
	vadduwm 27, 19, 27
	vadduwm 3, 2, 3
	xxlxor 33, 63, 33
	xxlxor 43, 48, 43
	xxlxor 36, 35, 36
	xxlxor 37, 59, 37
	vrlw 4, 4, 10
	vrlw 1, 1, 10
	vrlw 11, 11, 10
	vrlw 5, 5, 10
	xxlor 52, 4, 4
	xxlor 40, 2, 2
	vadduwm 17, 17, 21
	vadduwm 28, 28, 20
	vadduwm 0, 0, 7
	vadduwm 15, 15, 8
	vadduwm 17, 4, 17
	vadduwm 28, 1, 28
	vadduwm 0, 11, 0
	vadduwm 15, 5, 15
	xxlxor 61, 49, 61
	xxlxor 51, 60, 51
	xxlxor 34, 32, 34
	xxlxor 56, 47, 56
	vperm 29, 29, 29, 9
	vperm 19, 19, 19, 9
	vperm 2, 2, 2, 9
	vperm 24, 24, 24, 9
	vmr	25, 26
	xxlor 3, 39, 39
	xxland 61, 61, 10
	xxland 51, 51, 10
	xxland 34, 34, 10
	xxland 56, 56, 10
	vadduwm 27, 29, 27
	vadduwm 3, 19, 3
	vadduwm 31, 2, 31
	vadduwm 16, 24, 16
	xxlxor 36, 59, 36
	xxlxor 33, 35, 33
	xxlxor 43, 63, 43
	xxlxor 37, 48, 37
	vrlw 4, 4, 12
	vrlw 1, 1, 12
	vrlw 11, 11, 12
	vrlw 5, 5, 12
	xxlor 54, 6, 6
	xxlor 58, 5, 5
	xxlor 39, 8, 8
	vadduwm 17, 17, 22
	vadduwm 28, 28, 26
	vadduwm 0, 0, 7
	vadduwm 15, 15, 25
	vadduwm 17, 4, 17
	vadduwm 28, 1, 28
	vadduwm 0, 11, 0
	vadduwm 15, 5, 15
	xxlxor 61, 49, 61
	xxlxor 51, 60, 51
	xxlxor 34, 32, 34
	xxlxor 56, 47, 56
	vperm 29, 29, 29, 6
	vperm 19, 19, 19, 6
	vperm 2, 2, 2, 6
	vperm 24, 24, 24, 6
	xxlor 39, 26, 26
	vadduwm 28, 28, 14
	xxland 61, 61, 31
	xxland 51, 51, 31
	xxland 34, 34, 31
	xxland 56, 56, 31
	vadduwm 27, 29, 27
	vadduwm 3, 19, 3
	vadduwm 31, 2, 31
	vadduwm 16, 24, 16
	xxlxor 36, 59, 36
	xxlxor 33, 35, 33
	xxlxor 43, 63, 43
	xxlxor 37, 48, 37
	vrlw 1, 1, 10
	vrlw 11, 11, 10
	vrlw 5, 5, 10
	vrlw 4, 4, 10
	vadduwm 17, 17, 7
	vadduwm 0, 0, 30
	vadduwm 15, 15, 23
	vadduwm 17, 1, 17
	vadduwm 28, 11, 28
	vadduwm 0, 5, 0
	vadduwm 15, 4, 15
	xxlxor 56, 49, 56
	xxlxor 61, 60, 61
	xxlxor 51, 32, 51
	xxlxor 34, 47, 34
	vperm 24, 24, 24, 9
	vperm 29, 29, 29, 9
	vperm 19, 19, 19, 9
	vperm 2, 2, 2, 9
	xxlor 24, 55, 55
	vadduwm 17, 17, 13
	xxland 56, 56, 10
	xxland 61, 61, 10
	xxland 51, 51, 10
	xxland 34, 34, 10
	vadduwm 31, 24, 31
	vadduwm 16, 29, 16
	vadduwm 27, 19, 27
	vadduwm 3, 2, 3
	xxlxor 33, 63, 33
	xxlxor 43, 48, 43
	xxlxor 37, 59, 37
	xxlxor 36, 35, 36
	vrlw 1, 1, 12
	vrlw 11, 11, 12
	vrlw 5, 5, 12
	vrlw 4, 4, 12
	vmr	23, 13
	xxlor 45, 25, 25
	xxlor 39, 7, 7
	vadduwm 28, 28, 13
	vadduwm 0, 0, 18
	vadduwm 15, 15, 7
	vadduwm 17, 1, 17
	vadduwm 28, 11, 28
	vadduwm 0, 5, 0
	vadduwm 15, 4, 15
	xxlxor 56, 49, 56
	xxlxor 61, 60, 61
	xxlxor 51, 32, 51
	xxlxor 34, 47, 34
	vperm 24, 24, 24, 6
	vperm 29, 29, 29, 6
	vperm 19, 19, 19, 6
	vperm 2, 2, 2, 6
	xxlor 2, 46, 46
	xxlor 46, 3, 3
	xxland 56, 56, 31
	xxland 61, 61, 31
	xxland 51, 51, 31
	xxland 34, 34, 31
	vadduwm 31, 24, 31
	vadduwm 16, 29, 16
	vadduwm 27, 19, 27
	vadduwm 3, 2, 3
	xxlxor 33, 63, 33
	xxlxor 43, 48, 43
	xxlxor 36, 35, 36
	xxlxor 37, 59, 37
	vrlw 4, 4, 10
	vrlw 1, 1, 10
	vrlw 11, 11, 10
	vrlw 5, 5, 10
	vadduwm 17, 17, 20
	vadduwm 28, 28, 26
	vadduwm 0, 0, 25
	vadduwm 15, 15, 14
	vadduwm 17, 4, 17
	vadduwm 28, 1, 28
	vadduwm 0, 11, 0
	vadduwm 15, 5, 15
	xxlxor 61, 49, 61
	xxlxor 51, 60, 51
	xxlxor 34, 32, 34
	xxlxor 56, 47, 56
	vperm 29, 29, 29, 9
	vperm 19, 19, 19, 9
	vperm 2, 2, 2, 9
	vperm 24, 24, 24, 9
	xxlor 52, 2, 2
	vadduwm 17, 17, 8
	xxland 61, 61, 10
	xxland 51, 51, 10
	xxland 34, 34, 10
	xxland 56, 56, 10
	vadduwm 27, 29, 27
	vadduwm 3, 19, 3
	vadduwm 31, 2, 31
	vadduwm 16, 24, 16
	xxlxor 36, 59, 36
	xxlxor 33, 35, 33
	xxlxor 43, 63, 43
	xxlxor 37, 48, 37
	vrlw 4, 4, 12
	vrlw 1, 1, 12
	vrlw 11, 11, 12
	vrlw 5, 5, 12
	vadduwm 28, 28, 20
	vadduwm 0, 0, 21
	vadduwm 15, 15, 18
	vadduwm 17, 4, 17
	vadduwm 28, 1, 28
	vadduwm 0, 11, 0
	vadduwm 15, 5, 15
	xxlxor 61, 49, 61
	xxlxor 51, 60, 51
	xxlxor 34, 32, 34
	xxlxor 56, 47, 56
	vperm 29, 29, 29, 6
	vperm 19, 19, 19, 6
	vperm 2, 2, 2, 6
	vperm 24, 24, 24, 6
	vadduwm 17, 17, 22
	vadduwm 28, 28, 30
	xxland 61, 61, 31
	xxland 51, 51, 31
	xxland 34, 34, 31
	xxland 56, 56, 31
	vadduwm 27, 29, 27
	vadduwm 3, 19, 3
	vadduwm 31, 2, 31
	vadduwm 16, 24, 16
	xxlxor 36, 59, 36
	xxlxor 33, 35, 33
	xxlxor 43, 63, 43
	xxlxor 37, 48, 37
	vrlw 1, 1, 10
	vrlw 11, 11, 10
	vrlw 5, 5, 10
	vrlw 4, 4, 10
	vadduwm 0, 0, 23
	vadduwm 15, 15, 7
	vadduwm 17, 1, 17
	vadduwm 28, 11, 28
	vadduwm 0, 5, 0
	vadduwm 15, 4, 15
	xxlxor 56, 49, 56
	xxlxor 61, 60, 61
	xxlxor 51, 32, 51
	xxlxor 34, 47, 34
	vperm 24, 24, 24, 9
	vperm 29, 29, 29, 9
	vperm 19, 19, 19, 9
	vperm 2, 2, 2, 9
	xxlor 5, 4, 4
	xxlor 4, 58, 58
	xxland 56, 56, 10
	xxland 61, 61, 10
	xxland 51, 51, 10
	xxland 34, 34, 10
	vadduwm 31, 24, 31
	vadduwm 16, 29, 16
	vadduwm 27, 19, 27
	vadduwm 3, 2, 3
	xxlxor 33, 63, 33
	xxlxor 43, 48, 43
	xxlxor 37, 59, 37
	xxlxor 36, 35, 36
	vrlw 1, 1, 12
	vrlw 11, 11, 12
	vrlw 5, 5, 12
	vrlw 4, 4, 12
	xxlor 39, 8, 8
	xxlor 54, 24, 24
	xxlor 58, 26, 26
	vadduwm 17, 17, 13
	vadduwm 28, 28, 7
	vadduwm 0, 0, 22
	vadduwm 15, 15, 26
	vadduwm 17, 1, 17
	vadduwm 28, 11, 28
	vadduwm 0, 5, 0
	vadduwm 15, 4, 15
	xxlxor 56, 49, 56
	xxlxor 61, 60, 61
	xxlxor 51, 32, 51
	xxlxor 34, 47, 34
	vperm 24, 24, 24, 6
	vperm 29, 29, 29, 6
	vperm 19, 19, 19, 6
	vperm 2, 2, 2, 6
	xxlor 3, 53, 53
	xxlor 53, 4, 4
	xxland 56, 56, 31
	xxland 61, 61, 31
	xxland 51, 51, 31
	xxland 34, 34, 31
	vadduwm 31, 24, 31
	vadduwm 16, 29, 16
	vadduwm 27, 19, 27
	vadduwm 3, 2, 3
	xxlxor 33, 63, 33
	xxlxor 43, 48, 43
	xxlxor 36, 35, 36
	xxlxor 37, 59, 37
	vrlw 4, 4, 10
	vrlw 1, 1, 10
	vrlw 11, 11, 10
	vrlw 5, 5, 10
	vadduwm 17, 17, 21
	vadduwm 28, 28, 20
	vadduwm 0, 0, 18
	vadduwm 15, 15, 25
	vadduwm 17, 4, 17
	vadduwm 28, 1, 28
	vadduwm 0, 11, 0
	vadduwm 15, 5, 15
	xxlxor 61, 49, 61
	xxlxor 51, 60, 51
	xxlxor 34, 32, 34
	xxlxor 56, 47, 56
	vperm 29, 29, 29, 9
	vperm 19, 19, 19, 9
	vperm 2, 2, 2, 9
	vperm 24, 24, 24, 9
	xxlor 2, 55, 55
	vmr	23, 18
	xxland 61, 61, 10
	xxland 51, 51, 10
	xxland 34, 34, 10
	xxland 56, 56, 10
	vadduwm 27, 29, 27
	vadduwm 3, 19, 3
	vadduwm 31, 2, 31
	vadduwm 16, 24, 16
	xxlxor 36, 59, 36
	xxlxor 33, 35, 33
	xxlxor 43, 63, 43
	xxlxor 37, 48, 37
	vrlw 4, 4, 12
	vrlw 1, 1, 12
	vrlw 11, 11, 12
	vrlw 5, 5, 12
	xxlor 50, 5, 5
	vadduwm 17, 17, 14
	vadduwm 28, 28, 30
	vadduwm 0, 0, 18
	vadduwm 15, 15, 22
	vadduwm 17, 4, 17
	vadduwm 28, 1, 28
	vadduwm 0, 11, 0
	vadduwm 15, 5, 15
	xxlxor 61, 49, 61
	xxlxor 51, 60, 51
	xxlxor 34, 32, 34
	xxlxor 56, 47, 56
	vperm 29, 29, 29, 6
	vperm 19, 19, 19, 6
	vperm 2, 2, 2, 6
	vperm 24, 24, 24, 6
	xxlor 25, 40, 40
	vmr	8, 13
	xxland 61, 61, 31
	xxland 51, 51, 31
	xxland 34, 34, 31
	xxland 56, 56, 31
	vadduwm 27, 29, 27
	vadduwm 3, 19, 3
	vadduwm 31, 2, 31
	vadduwm 16, 24, 16
	xxlxor 36, 59, 36
	xxlxor 33, 35, 33
	xxlxor 43, 63, 43
	xxlxor 37, 48, 37
	xxlor 45, 25, 25
	vrlw 1, 1, 10
	vrlw 11, 11, 10
	vrlw 5, 5, 10
	vrlw 4, 4, 10
	vadduwm 17, 17, 13
	xxlor 45, 2, 2
	vadduwm 0, 0, 8
	vadduwm 28, 28, 13
	vadduwm 15, 15, 26
	vadduwm 17, 1, 17
	vadduwm 28, 11, 28
	vadduwm 0, 5, 0
	vadduwm 15, 4, 15
	xxlxor 56, 49, 56
	xxlxor 61, 60, 61
	xxlxor 51, 32, 51
	xxlxor 34, 47, 34
	vperm 24, 24, 24, 9
	vperm 29, 29, 29, 9
	vperm 19, 19, 19, 9
	vperm 2, 2, 2, 9
	xxlor 4, 57, 57
	xxlor 26, 46, 46
	xxland 56, 56, 10
	xxland 61, 61, 10
	xxland 51, 51, 10
	xxland 34, 34, 10
	vadduwm 31, 24, 31
	vadduwm 16, 29, 16
	vadduwm 27, 19, 27
	vadduwm 3, 2, 3
	xxlxor 33, 63, 33
	xxlxor 43, 48, 43
	xxlxor 37, 59, 37
	xxlxor 36, 35, 36
	vrlw 1, 1, 12
	vrlw 11, 11, 12
	vrlw 5, 5, 12
	vrlw 4, 4, 12
	xxlor 8, 62, 62
	xxlor 57, 3, 3
	xxlor 46, 7, 7
	xxlor 62, 6, 6
	vadduwm 17, 17, 7
	vadduwm 28, 28, 25
	vadduwm 0, 0, 14
	vadduwm 15, 15, 30
	vadduwm 17, 1, 17
	vadduwm 28, 11, 28
	vadduwm 0, 5, 0
	vadduwm 15, 4, 15
	xxlxor 56, 49, 56
	xxlxor 61, 60, 61
	xxlxor 51, 32, 51
	xxlxor 34, 47, 34
	vperm 24, 24, 24, 6
	vperm 29, 29, 29, 6
	vperm 19, 19, 19, 6
	vperm 2, 2, 2, 6
	vadduwm 17, 17, 20
	xxlor 3, 52, 52
	xxland 56, 56, 31
	xxland 61, 61, 31
	xxland 51, 51, 31
	xxland 34, 34, 31
	vadduwm 31, 24, 31
	vadduwm 16, 29, 16
	vadduwm 27, 19, 27
	vadduwm 3, 2, 3
	xxlxor 33, 63, 33
	xxlxor 43, 48, 43
	xxlxor 36, 35, 36
	xxlxor 37, 59, 37
	vrlw 4, 4, 10
	vrlw 1, 1, 10
	vrlw 11, 11, 10
	vrlw 5, 5, 10
	xxlor 52, 8, 8
	vadduwm 0, 0, 22
	vadduwm 28, 28, 20
	vadduwm 15, 15, 23
	vadduwm 17, 4, 17
	vadduwm 28, 1, 28
	vadduwm 0, 11, 0
	vadduwm 15, 5, 15
	xxlxor 61, 49, 61
	xxlxor 51, 60, 51
	xxlxor 34, 32, 34
	xxlxor 56, 47, 56
	vperm 29, 29, 29, 9
	vperm 19, 19, 19, 9
	vperm 2, 2, 2, 9
	vperm 24, 24, 24, 9
	xxlor 6, 55, 55
	xxlor 55, 4, 4
	xxland 61, 61, 10
	xxland 51, 51, 10
	xxland 34, 34, 10
	xxland 56, 56, 10
	vadduwm 27, 29, 27
	vadduwm 3, 19, 3
	vadduwm 31, 2, 31
	vadduwm 16, 24, 16
	xxlxor 36, 59, 36
	xxlxor 33, 35, 33
	xxlxor 43, 63, 43
	xxlxor 37, 48, 37
	vrlw 4, 4, 12
	vrlw 1, 1, 12
	vrlw 11, 11, 12
	vrlw 5, 5, 12
	vadduwm 17, 17, 23
	vadduwm 28, 28, 13
	vadduwm 0, 0, 21
	vadduwm 15, 15, 14
	vadduwm 17, 4, 17
	vadduwm 28, 1, 28
	vadduwm 0, 11, 0
	vadduwm 15, 5, 15
	xxlxor 61, 49, 61
	xxlxor 51, 60, 51
	xxlxor 34, 32, 34
	xxlxor 56, 47, 56
	vperm 29, 29, 29, 6
	vperm 19, 19, 19, 6
	vperm 2, 2, 2, 6
	vperm 24, 24, 24, 6
	xxlor 4, 53, 53
	xxlor 53, 26, 26
	xxland 61, 61, 31
	xxland 51, 51, 31
	xxland 34, 34, 31
	xxland 56, 56, 31
	vadduwm 27, 29, 27
	vadduwm 3, 19, 3
	vadduwm 31, 2, 31
	vadduwm 16, 24, 16
	xxlxor 36, 59, 36
	xxlxor 33, 35, 33
	xxlxor 43, 63, 43
	xxlxor 37, 48, 37
	vrlw 1, 1, 10
	vrlw 11, 11, 10
	vrlw 5, 5, 10
	vrlw 4, 4, 10
	vadduwm 17, 17, 21
	vadduwm 28, 28, 8
	vadduwm 0, 0, 7
	vadduwm 15, 15, 30
	vadduwm 17, 1, 17
	vadduwm 28, 11, 28
	vadduwm 0, 5, 0
	vadduwm 15, 4, 15
	xxlxor 56, 49, 56
	xxlxor 61, 60, 61
	xxlxor 51, 32, 51
	xxlxor 34, 47, 34
	vperm 24, 24, 24, 9
	vperm 29, 29, 29, 9
	vperm 19, 19, 19, 9
	vperm 2, 2, 2, 9
	xxlor 5, 25, 25
	xxlor 2, 58, 58
	xxland 56, 56, 10
	xxland 61, 61, 10
	xxland 51, 51, 10
	xxland 34, 34, 10
	vadduwm 31, 24, 31
	vadduwm 16, 29, 16
	vadduwm 27, 19, 27
	vadduwm 3, 2, 3
	xxlxor 33, 63, 33
	xxlxor 43, 48, 43
	xxlxor 37, 59, 37
	xxlxor 36, 35, 36
	vrlw 1, 1, 12
	vrlw 11, 11, 12
	vrlw 5, 5, 12
	vrlw 4, 4, 12
	vmr	22, 26
	vadduwm 0, 0, 26
	xxlor 58, 5, 5
	vadduwm 17, 17, 25
	vadduwm 28, 28, 18
	vadduwm 15, 15, 26
	vadduwm 17, 1, 17
	vadduwm 28, 11, 28
	vadduwm 0, 5, 0
	vadduwm 15, 4, 15
	xxlxor 56, 49, 56
	xxlxor 61, 60, 61
	xxlxor 51, 32, 51
	xxlxor 34, 47, 34
	vperm 24, 24, 24, 6
	vperm 29, 29, 29, 6
	vperm 19, 19, 19, 6
	vperm 2, 2, 2, 6
	xxlor 7, 24, 24
	xxlor 8, 57, 57
	xxland 56, 56, 31
	xxland 61, 61, 31
	xxland 51, 51, 31
	xxland 34, 34, 31
	vadduwm 31, 24, 31
	vadduwm 16, 29, 16
	vadduwm 27, 19, 27
	vadduwm 3, 2, 3
	xxlxor 33, 63, 33
	xxlxor 43, 48, 43
	xxlxor 36, 35, 36
	xxlxor 37, 59, 37
	vrlw 4, 4, 10
	vrlw 1, 1, 10
	vrlw 11, 11, 10
	vrlw 5, 5, 10
	xxlor 57, 7, 7
	vadduwm 17, 17, 20
	vadduwm 28, 28, 13
	vadduwm 0, 0, 14
	vadduwm 15, 15, 25
	vadduwm 17, 4, 17
	vadduwm 28, 1, 28
	vadduwm 0, 11, 0
	vadduwm 15, 5, 15
	xxlxor 61, 49, 61
	xxlxor 51, 60, 51
	xxlxor 34, 32, 34
	xxlxor 56, 47, 56
	vperm 29, 29, 29, 9
	vperm 19, 19, 19, 9
	vperm 2, 2, 2, 9
	vperm 24, 24, 24, 9
	xxlor 5, 52, 52
	xxlor 23, 45, 45
	xxland 61, 61, 10
	xxland 51, 51, 10
	xxland 34, 34, 10
	xxland 56, 56, 10
	vadduwm 27, 29, 27
	vadduwm 3, 19, 3
	vadduwm 31, 2, 31
	vadduwm 16, 24, 16
	xxlxor 36, 59, 36
	xxlxor 33, 35, 33
	xxlxor 43, 63, 43
	xxlxor 37, 48, 37
	vrlw 4, 4, 12
	vrlw 1, 1, 12
	vrlw 11, 11, 12
	vrlw 5, 5, 12
	xxlor 52, 6, 6
	vadduwm 28, 28, 8
	vmr	13, 8
	xxlor 40, 3, 3
	vadduwm 17, 17, 20
	vadduwm 0, 0, 8
	vadduwm 15, 15, 22
	vadduwm 17, 4, 17
	vadduwm 28, 1, 28
	vadduwm 0, 11, 0
	vadduwm 15, 5, 15
	xxlxor 61, 49, 61
	xxlxor 51, 60, 51
	xxlxor 34, 32, 34
	xxlxor 56, 47, 56
	vperm 29, 29, 29, 6
	vperm 19, 19, 19, 6
	vperm 2, 2, 2, 6
	vperm 24, 24, 24, 6
	xxlor 25, 39, 39
	vmr	7, 30
	xxland 61, 61, 31
	xxland 51, 51, 31
	xxland 34, 34, 31
	xxland 56, 56, 31
	vadduwm 27, 29, 27
	vadduwm 3, 19, 3
	vadduwm 31, 2, 31
	vadduwm 16, 24, 16
	xxlxor 36, 59, 36
	xxlxor 33, 35, 33
	xxlxor 43, 63, 43
	xxlxor 37, 48, 37
	vrlw 1, 1, 10
	vrlw 11, 11, 10
	vrlw 5, 5, 10
	vrlw 4, 4, 10
	vmr	30, 18
	xxlor 24, 46, 46
	xxlor 46, 25, 25
	xxlor 50, 8, 8
	vadduwm 17, 17, 23
	vadduwm 28, 28, 14
	vadduwm 0, 0, 18
	vadduwm 15, 15, 26
	vadduwm 17, 1, 17
	vadduwm 28, 11, 28
	vadduwm 0, 5, 0
	vadduwm 15, 4, 15
	xxlxor 56, 49, 56
	xxlxor 61, 60, 61
	xxlxor 51, 32, 51
	xxlxor 34, 47, 34
	vperm 24, 24, 24, 9
	vperm 29, 29, 29, 9
	vperm 19, 19, 19, 9
	vperm 2, 2, 2, 9
	xxlor 6, 58, 58
	xxlor 58, 4, 4
	xxland 56, 56, 10
	xxland 61, 61, 10
	xxland 51, 51, 10
	xxland 34, 34, 10
	vadduwm 31, 24, 31
	vadduwm 16, 29, 16
	vadduwm 27, 19, 27
	vadduwm 3, 2, 3
	xxlxor 33, 63, 33
	xxlxor 43, 48, 43
	xxlxor 37, 59, 37
	xxlxor 36, 35, 36
	vrlw 1, 1, 12
	vrlw 11, 11, 12
	vrlw 5, 5, 12
	vrlw 4, 4, 12
	vadduwm 17, 17, 30
	vadduwm 28, 28, 26
	vadduwm 0, 0, 7
	vadduwm 15, 15, 21
	vadduwm 17, 1, 17
	vadduwm 28, 11, 28
	vadduwm 0, 5, 0
	vadduwm 15, 4, 15
	xxlxor 56, 49, 56
	xxlxor 61, 60, 61
	xxlxor 51, 32, 51
	xxlxor 34, 47, 34
	vperm 24, 24, 24, 6
	vperm 29, 29, 29, 6
	vperm 19, 19, 19, 6
	vperm 2, 2, 2, 6
	xxlor 40, 23, 23
	vadduwm 13, 28, 13
	vadduwm 8, 17, 8
	xxland 49, 56, 31
	xxland 61, 61, 31
	xxland 51, 51, 31
	xxland 34, 34, 31
	vadduwm 31, 17, 31
	vadduwm 16, 29, 16
	vadduwm 28, 19, 27
	vadduwm 3, 2, 3
	xxlxor 33, 63, 33
	xxlxor 43, 48, 43
	xxlxor 36, 35, 36
	xxlxor 37, 60, 37
	vrlw 4, 4, 10
	vrlw 1, 1, 10
	vrlw 11, 11, 10
	vrlw 5, 5, 10
	xxlor 2, 55, 55
	vmr	23, 30
	xxlor 62, 24, 24
	vadduwm 0, 0, 22
	vadduwm 15, 15, 30
	vadduwm 8, 4, 8
	vadduwm 13, 1, 13
	vadduwm 0, 11, 0
	vadduwm 15, 5, 15
	xxlxor 61, 40, 61
	xxlxor 51, 45, 51
	xxlxor 34, 32, 34
	xxlxor 49, 47, 49
	vperm 29, 29, 29, 9
	vperm 19, 19, 19, 9
	vperm 2, 2, 2, 9
	vperm 17, 17, 17, 9
	vadduwm 13, 13, 14
	xxlor 46, 5, 5
	xxland 61, 61, 10
	xxland 51, 51, 10
	xxland 34, 34, 10
	xxland 49, 49, 10
	vadduwm 28, 29, 28
	vadduwm 3, 19, 3
	vadduwm 31, 2, 31
	vadduwm 16, 17, 16
	xxlxor 36, 60, 36
	xxlxor 33, 35, 33
	xxlxor 43, 63, 43
	xxlxor 37, 48, 37
	vrlw 4, 4, 12
	vrlw 1, 1, 12
	vrlw 11, 11, 12
	vrlw 5, 5, 12
	vadduwm 8, 8, 25
	vadduwm 0, 0, 14
	vadduwm 15, 15, 7
	vadduwm 8, 4, 8
	vadduwm 13, 1, 13
	vadduwm 0, 11, 0
	vadduwm 15, 5, 15
	xxlxor 62, 40, 61
	xxlxor 51, 45, 51
	xxlxor 34, 32, 34
	xxlxor 49, 47, 49
	vperm 30, 30, 30, 6
	vperm 19, 19, 19, 6
	vperm 2, 2, 2, 6
	vperm 17, 17, 17, 6
	vadduwm 29, 8, 20
	vadduwm 8, 13, 18
	xxland 45, 62, 31
	xxland 51, 51, 31
	xxland 34, 34, 31
	xxland 49, 49, 31
	vadduwm 30, 13, 28
	vadduwm 3, 19, 3
	vadduwm 31, 2, 31
	vadduwm 16, 17, 16
	xxlxor 36, 62, 36
	xxlxor 33, 35, 33
	xxlxor 43, 63, 43
	xxlxor 37, 48, 37
	vrlw 1, 1, 10
	vrlw 11, 11, 10
	vrlw 5, 5, 10
	vrlw 4, 4, 10
	vadduwm 0, 0, 23
	vadduwm 7, 15, 21
	vadduwm 29, 1, 29
	vadduwm 8, 11, 8
	vadduwm 0, 5, 0
	vadduwm 7, 4, 7
	xxlxor 47, 61, 49
	xxlxor 45, 40, 45
	xxlxor 49, 32, 51
	xxlxor 34, 39, 34
	vperm 15, 15, 15, 9
	vperm 13, 13, 13, 9
	vperm 17, 17, 17, 9
	vperm 2, 2, 2, 9
	xxlor 46, 3, 3
	vadduwm 9, 29, 26
	vadduwm 8, 8, 14
	xxland 46, 47, 10
	xxland 45, 45, 10
	xxland 47, 49, 10
	xxland 34, 34, 10
	vadduwm 17, 14, 31
	vadduwm 16, 13, 16
	vadduwm 18, 15, 30
	vadduwm 3, 2, 3
	xxlxor 33, 49, 33
	xxlxor 43, 48, 43
	xxlxor 37, 50, 37
	xxlxor 36, 35, 36
	vrlw 1, 1, 12
	vrlw 11, 11, 12
	vrlw 5, 5, 12
	vrlw 4, 4, 12
	xxlor 44, 6, 6
	xxlor 0, 10, 10
	vadduwm 0, 0, 12
	xxlor 44, 2, 2
	vadduwm 9, 1, 9
	vadduwm 7, 7, 12
	vadduwm 8, 11, 8
	vadduwm 7, 4, 7
	vadduwm 0, 5, 0
	xxlxor 34, 39, 34
	xxlxor 44, 32, 47
	vperm 2, 2, 2, 6
	xxlxor 46, 41, 46
	xxlxor 45, 40, 45
	vperm 12, 12, 12, 6
	vperm 14, 14, 14, 6
	vperm 13, 13, 13, 6
	xxland 34, 34, 31
	xxlor 1, 31, 31
	vadduwm 3, 2, 3
	xxland 44, 44, 31
	xxlxor 36, 35, 36
	xxlxor 51, 35, 40
	xxland 35, 46, 31
	xxland 38, 45, 31
	vadduwm 15, 12, 18
	vadduwm 8, 3, 17
	vadduwm 13, 6, 16
	xxlxor 37, 47, 37
	xxlxor 33, 40, 33
	xxlxor 43, 45, 43
	vrlw 4, 4, 10
	vrlw 1, 1, 10
	vrlw 11, 11, 10
	vrlw 5, 5, 10
	xxlxor 47, 47, 41
	xxlxor 40, 40, 32
	xxlxor 39, 45, 39
	xxlxor 50, 36, 38
	xxlxor 63, 33, 44
	xxlxor 43, 43, 34
	xxlxor 41, 37, 35
	bne	0, .LBB3_2
.LBB3_5:
	vmrglw 2, 19, 15
	li 3, 32
	li 4, 48
	vmrglw 4, 7, 8
	vmrglw 0, 31, 18
	vmrglw 1, 9, 11
	vmrghw 3, 19, 15
	vmrghw 5, 7, 8
	vmrghw 6, 31, 18
	vmrghw 7, 9, 11
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
	lfd 31, 408(1)
	ld 30, 304(1)
	ld 29, 296(1)
	lxvd2x 62, 1, 3
	li 3, 192
	lfd 30, 400(1)
	ld 28, 288(1)
	ld 27, 280(1)
	lxvd2x 61, 1, 3
	li 3, 176
	lfd 29, 392(1)
	ld 26, 272(1)
	ld 25, 264(1)
	lxvd2x 60, 1, 3
	li 3, 160
	lfd 28, 384(1)
	ld 24, 256(1)
	ld 23, 248(1)
	lxvd2x 59, 1, 3
	li 3, 144
	lfd 27, 376(1)
	ld 22, 240(1)
	lxvd2x 58, 1, 3
	li 3, 128
	lfd 26, 368(1)
	lxvd2x 57, 1, 3
	li 3, 112
	lfd 25, 360(1)
	lxvd2x 56, 1, 3
	li 3, 96
	lfd 24, 352(1)
	lxvd2x 55, 1, 3
	li 3, 80
	lfd 23, 344(1)
	lxvd2x 54, 1, 3
	li 3, 64
	lfd 22, 336(1)
	lxvd2x 53, 1, 3
	li 3, 48
	lfd 21, 328(1)
	lxvd2x 52, 1, 3
	lfd 20, 320(1)
	addi 1, 1, 416
	blr
	.long	0
	.quad	0
.Lfunc_end3:
	.size	blake3_hash4_sse41, .Lfunc_end3-.Lfunc_begin3
	.cfi_endproc
	.section	".note.GNU-stack","",@progbits
#endif
