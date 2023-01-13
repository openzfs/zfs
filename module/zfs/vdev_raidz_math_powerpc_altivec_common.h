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
 * Copyright (C) 2019 Romain Dolbeau. All rights reserved.
 *           <romain.dolbeau@european-processor-initiative.eu>
 */

#include <sys/types.h>
#include <sys/simd.h>

#define	_REG_CNT(_0, _1, _2, _3, _4, _5, _6, _7, N, ...) N
#define	REG_CNT(r...) _REG_CNT(r, 8, 7, 6, 5, 4, 3, 2, 1)

#define	VR0_(REG, ...) "%[w"#REG"]"
#define	VR1_(_1, REG, ...) "%[w"#REG"]"
#define	VR2_(_1, _2, REG, ...) "%[w"#REG"]"
#define	VR3_(_1, _2, _3, REG, ...) "%[w"#REG"]"
#define	VR4_(_1, _2, _3, _4, REG, ...) "%[w"#REG"]"
#define	VR5_(_1, _2, _3, _4, _5, REG, ...) "%[w"#REG"]"
#define	VR6_(_1, _2, _3, _4, _5, _6, REG, ...) "%[w"#REG"]"
#define	VR7_(_1, _2, _3, _4, _5, _6, _7, REG, ...) "%[w"#REG"]"

/*
 * Here we need registers not used otherwise.
 * They will be used in unused ASM for the case
 * with more registers than required... but GCC
 * will still need to make sure the constraints
 * are correct, and duplicate constraints are illegal
 * ... and we use the "register" number as a name
 */

#define	VR0(r...) VR0_(r)
#define	VR1(r...) VR1_(r)
#define	VR2(r...) VR2_(r, 36)
#define	VR3(r...) VR3_(r, 36, 35)
#define	VR4(r...) VR4_(r, 36, 35, 34, 33)
#define	VR5(r...) VR5_(r, 36, 35, 34, 33, 32)
#define	VR6(r...) VR6_(r, 36, 35, 34, 33, 32, 31)
#define	VR7(r...) VR7_(r, 36, 35, 34, 33, 32, 31, 30)

#define	VR(X) "%[w"#X"]"

#define	RVR0_(REG, ...) [w##REG] "v" (w##REG)
#define	RVR1_(_1, REG, ...) [w##REG] "v" (w##REG)
#define	RVR2_(_1, _2, REG, ...) [w##REG] "v" (w##REG)
#define	RVR3_(_1, _2, _3, REG, ...) [w##REG] "v" (w##REG)
#define	RVR4_(_1, _2, _3, _4, REG, ...) [w##REG] "v" (w##REG)
#define	RVR5_(_1, _2, _3, _4, _5, REG, ...) [w##REG] "v" (w##REG)
#define	RVR6_(_1, _2, _3, _4, _5, _6, REG, ...) [w##REG] "v" (w##REG)
#define	RVR7_(_1, _2, _3, _4, _5, _6, _7, REG, ...) [w##REG] "v" (w##REG)

#define	RVR0(r...) RVR0_(r)
#define	RVR1(r...) RVR1_(r)
#define	RVR2(r...) RVR2_(r, 36)
#define	RVR3(r...) RVR3_(r, 36, 35)
#define	RVR4(r...) RVR4_(r, 36, 35, 34, 33)
#define	RVR5(r...) RVR5_(r, 36, 35, 34, 33, 32)
#define	RVR6(r...) RVR6_(r, 36, 35, 34, 33, 32, 31)
#define	RVR7(r...) RVR7_(r, 36, 35, 34, 33, 32, 31, 30)

#define	RVR(X) [w##X] "v" (w##X)

#define	WVR0_(REG, ...) [w##REG] "=v" (w##REG)
#define	WVR1_(_1, REG, ...) [w##REG] "=v" (w##REG)
#define	WVR2_(_1, _2, REG, ...) [w##REG] "=v" (w##REG)
#define	WVR3_(_1, _2, _3, REG, ...) [w##REG] "=v" (w##REG)
#define	WVR4_(_1, _2, _3, _4, REG, ...) [w##REG] "=v" (w##REG)
#define	WVR5_(_1, _2, _3, _4, _5, REG, ...) [w##REG] "=v" (w##REG)
#define	WVR6_(_1, _2, _3, _4, _5, _6, REG, ...) [w##REG] "=v" (w##REG)
#define	WVR7_(_1, _2, _3, _4, _5, _6, _7, REG, ...) [w##REG] "=v" (w##REG)

#define	WVR0(r...) WVR0_(r)
#define	WVR1(r...) WVR1_(r)
#define	WVR2(r...) WVR2_(r, 36)
#define	WVR3(r...) WVR3_(r, 36, 35)
#define	WVR4(r...) WVR4_(r, 36, 35, 34, 33)
#define	WVR5(r...) WVR5_(r, 36, 35, 34, 33, 32)
#define	WVR6(r...) WVR6_(r, 36, 35, 34, 33, 32, 31)
#define	WVR7(r...) WVR7_(r, 36, 35, 34, 33, 32, 31, 30)

#define	WVR(X) [w##X] "=v" (w##X)

#define	UVR0_(REG, ...) [w##REG] "+&v" (w##REG)
#define	UVR1_(_1, REG, ...) [w##REG] "+&v" (w##REG)
#define	UVR2_(_1, _2, REG, ...) [w##REG] "+&v" (w##REG)
#define	UVR3_(_1, _2, _3, REG, ...) [w##REG] "+&v" (w##REG)
#define	UVR4_(_1, _2, _3, _4, REG, ...) [w##REG] "+&v" (w##REG)
#define	UVR5_(_1, _2, _3, _4, _5, REG, ...) [w##REG] "+&v" (w##REG)
#define	UVR6_(_1, _2, _3, _4, _5, _6, REG, ...) [w##REG] "+&v" (w##REG)
#define	UVR7_(_1, _2, _3, _4, _5, _6, _7, REG, ...) [w##REG] "+&v" (w##REG)

#define	UVR0(r...) UVR0_(r)
#define	UVR1(r...) UVR1_(r)
#define	UVR2(r...) UVR2_(r, 36)
#define	UVR3(r...) UVR3_(r, 36, 35)
#define	UVR4(r...) UVR4_(r, 36, 35, 34, 33)
#define	UVR5(r...) UVR5_(r, 36, 35, 34, 33, 32)
#define	UVR6(r...) UVR6_(r, 36, 35, 34, 33, 32, 31)
#define	UVR7(r...) UVR7_(r, 36, 35, 34, 33, 32, 31, 30)

#define	UVR(X) [w##X] "+&v" (w##X)

#define	R_01(REG1, REG2, ...) REG1, REG2
#define	_R_23(_0, _1, REG2, REG3, ...) REG2, REG3
#define	R_23(REG...) _R_23(REG, 1, 2, 3)

#define	ZFS_ASM_BUG()	ASSERT(0)

#define	OFFSET(ptr, val)	(((unsigned char *)(ptr))+val)

extern const uint8_t gf_clmul_mod_lt[4*256][16];

#define	ELEM_SIZE 16

typedef struct v {
	uint8_t b[ELEM_SIZE] __attribute__((aligned(ELEM_SIZE)));
} v_t;

#define	XOR_ACC(src, r...)					\
{								\
	switch (REG_CNT(r)) {					\
	case 8:							\
		__asm__ __volatile__(				\
		"lvx 21,0,%[SRC0]\n"				\
		"lvx 20,0,%[SRC1]\n"				\
		"lvx 19,0,%[SRC2]\n"				\
		"lvx 18,0,%[SRC3]\n"				\
		"vxor " VR0(r) "," VR0(r) ",21\n"		\
		"vxor " VR1(r) "," VR1(r) ",20\n"		\
		"vxor " VR2(r) "," VR2(r) ",19\n"		\
		"vxor " VR3(r) "," VR3(r) ",18\n"		\
		"lvx 21,0,%[SRC4]\n"				\
		"lvx 20,0,%[SRC5]\n"				\
		"lvx 19,0,%[SRC6]\n"				\
		"lvx 18,0,%[SRC7]\n"				\
		"vxor " VR4(r) "," VR4(r) ",21\n"		\
		"vxor " VR5(r) "," VR5(r) ",20\n"		\
		"vxor " VR6(r) "," VR6(r) ",19\n"		\
		"vxor " VR7(r) "," VR7(r) ",18\n"		\
		:	UVR0(r), UVR1(r), UVR2(r), UVR3(r),	\
			UVR4(r), UVR5(r), UVR6(r), UVR7(r)	\
		:	[SRC0] "r" ((OFFSET(src, 0))),		\
		[SRC1] "r" ((OFFSET(src, 16))),			\
		[SRC2] "r" ((OFFSET(src, 32))),			\
		[SRC3] "r" ((OFFSET(src, 48))),			\
		[SRC4] "r" ((OFFSET(src, 64))),			\
		[SRC5] "r" ((OFFSET(src, 80))),			\
		[SRC6] "r" ((OFFSET(src, 96))),			\
		[SRC7] "r" ((OFFSET(src, 112)))			\
		:	"v18", "v19", "v20", "v21");		\
		break;						\
	case 4:							\
		__asm__ __volatile__(				\
		"lvx 21,0,%[SRC0]\n"				\
		"lvx 20,0,%[SRC1]\n"				\
		"lvx 19,0,%[SRC2]\n"				\
		"lvx 18,0,%[SRC3]\n"				\
		"vxor " VR0(r) "," VR0(r) ",21\n"		\
		"vxor " VR1(r) "," VR1(r) ",20\n"		\
		"vxor " VR2(r) "," VR2(r) ",19\n"		\
		"vxor " VR3(r) "," VR3(r) ",18\n"		\
		:	UVR0(r), UVR1(r), UVR2(r), UVR3(r)	\
		:	[SRC0] "r" ((OFFSET(src, 0))),		\
		[SRC1] "r" ((OFFSET(src, 16))),			\
		[SRC2] "r" ((OFFSET(src, 32))),			\
		[SRC3] "r" ((OFFSET(src, 48)))			\
		:	"v18", "v19", "v20", "v21");		\
		break;						\
	case 2:							\
		__asm__ __volatile__(				\
		"lvx 21,0,%[SRC0]\n"				\
		"lvx 20,0,%[SRC1]\n"				\
		"vxor " VR0(r) "," VR0(r) ",21\n"		\
		"vxor " VR1(r) "," VR1(r) ",20\n"		\
		:	UVR0(r), UVR1(r)			\
		:	[SRC0] "r" ((OFFSET(src, 0))),		\
		[SRC1] "r" ((OFFSET(src, 16)))			\
		:	"v20", "v21");				\
		break;						\
	default:						\
		ZFS_ASM_BUG();					\
	}							\
}

#define	XOR(r...)						\
{								\
	switch (REG_CNT(r)) {					\
	case 8:							\
		__asm__ __volatile__(				\
		"vxor " VR4(r) "," VR4(r) "," VR0(r) "\n"	\
		"vxor " VR5(r) "," VR5(r) "," VR1(r) "\n"	\
		"vxor " VR6(r) "," VR6(r) "," VR2(r) "\n"	\
		"vxor " VR7(r) "," VR7(r) "," VR3(r) "\n"	\
		:	UVR4(r), UVR5(r), UVR6(r), UVR7(r)	\
		:	RVR0(r), RVR1(r), RVR2(r), RVR3(r));	\
		break;						\
	case 4:							\
		__asm__ __volatile__(				\
		"vxor " VR2(r) "," VR2(r) "," VR0(r) "\n"	\
		"vxor " VR3(r) "," VR3(r) "," VR1(r) "\n"	\
		:	UVR2(r), UVR3(r)			\
		:	RVR0(r), RVR1(r));			\
		break;						\
	default:						\
		ZFS_ASM_BUG();					\
	}							\
}

#define	ZERO(r...)						\
{								\
	switch (REG_CNT(r)) {					\
	case 8:							\
		__asm__ __volatile__(				\
		"vxor " VR0(r) "," VR0(r) "," VR0(r) "\n"	\
		"vxor " VR1(r) "," VR1(r) "," VR1(r) "\n"	\
		"vxor " VR2(r) "," VR2(r) "," VR2(r) "\n"	\
		"vxor " VR3(r) "," VR3(r) "," VR3(r) "\n"	\
		"vxor " VR4(r) "," VR4(r) "," VR4(r) "\n"	\
		"vxor " VR5(r) "," VR5(r) "," VR5(r) "\n"	\
		"vxor " VR6(r) "," VR6(r) "," VR6(r) "\n"	\
		"vxor " VR7(r) "," VR7(r) "," VR7(r) "\n"	\
		:	WVR0(r), WVR1(r), WVR2(r), WVR3(r),	\
			WVR4(r), WVR5(r), WVR6(r), WVR7(r));	\
		break;						\
	case 4:							\
		__asm__ __volatile__(				\
		"vxor " VR0(r) "," VR0(r) "," VR0(r) "\n"	\
		"vxor " VR1(r) "," VR1(r) "," VR1(r) "\n"	\
		"vxor " VR2(r) "," VR2(r) "," VR2(r) "\n"	\
		"vxor " VR3(r) "," VR3(r) "," VR3(r) "\n"	\
		:	WVR0(r), WVR1(r), WVR2(r), WVR3(r));	\
		break;						\
	case 2:							\
		__asm__ __volatile__(				\
		"vxor " VR0(r) "," VR0(r) "," VR0(r) "\n"	\
		"vxor " VR1(r) "," VR1(r) "," VR1(r) "\n"	\
		:	WVR0(r), WVR1(r));			\
		break;						\
	default:						\
		ZFS_ASM_BUG();					\
	}							\
}

#define	COPY(r...)						\
{								\
	switch (REG_CNT(r)) {					\
	case 8:							\
		__asm__ __volatile__(				\
		"vor " VR4(r) "," VR0(r) "," VR0(r) "\n"	\
		"vor " VR5(r) "," VR1(r) "," VR1(r) "\n"	\
		"vor " VR6(r) "," VR2(r) "," VR2(r) "\n"	\
		"vor " VR7(r) "," VR3(r) "," VR3(r) "\n"	\
		:	WVR4(r), WVR5(r), WVR6(r), WVR7(r)	\
		:	RVR0(r), RVR1(r), RVR2(r), RVR3(r));	\
		break;						\
	case 4:							\
		__asm__ __volatile__(				\
		"vor " VR2(r) "," VR0(r) "," VR0(r) "\n"	\
		"vor " VR3(r) "," VR1(r) "," VR1(r) "\n"	\
		:	WVR2(r), WVR3(r)			\
		:	RVR0(r), RVR1(r));			\
		break;						\
	default:						\
		ZFS_ASM_BUG();					\
	}							\
}

#define	LOAD(src, r...)						\
{								\
	switch (REG_CNT(r)) {					\
	case 8:							\
		__asm__ __volatile__(				\
		"lvx " VR0(r) " ,0,%[SRC0]\n"			\
		"lvx " VR1(r) " ,0,%[SRC1]\n"			\
		"lvx " VR2(r) " ,0,%[SRC2]\n"			\
		"lvx " VR3(r) " ,0,%[SRC3]\n"			\
		"lvx " VR4(r) " ,0,%[SRC4]\n"			\
		"lvx " VR5(r) " ,0,%[SRC5]\n"			\
		"lvx " VR6(r) " ,0,%[SRC6]\n"			\
		"lvx " VR7(r) " ,0,%[SRC7]\n"			\
		:	WVR0(r), WVR1(r), WVR2(r), WVR3(r),	\
			WVR4(r), WVR5(r), WVR6(r), WVR7(r)	\
		:	[SRC0] "r" ((OFFSET(src, 0))),		\
		[SRC1] "r" ((OFFSET(src, 16))),			\
		[SRC2] "r" ((OFFSET(src, 32))),			\
		[SRC3] "r" ((OFFSET(src, 48))),			\
		[SRC4] "r" ((OFFSET(src, 64))),			\
		[SRC5] "r" ((OFFSET(src, 80))),			\
		[SRC6] "r" ((OFFSET(src, 96))),			\
		[SRC7] "r" ((OFFSET(src, 112))));		\
		break;						\
	case 4:							\
		__asm__ __volatile__(				\
		"lvx " VR0(r) " ,0,%[SRC0]\n"			\
		"lvx " VR1(r) " ,0,%[SRC1]\n"			\
		"lvx " VR2(r) " ,0,%[SRC2]\n"			\
		"lvx " VR3(r) " ,0,%[SRC3]\n"			\
		:	WVR0(r), WVR1(r), WVR2(r), WVR3(r)	\
		:	[SRC0] "r" ((OFFSET(src, 0))),		\
		[SRC1] "r" ((OFFSET(src, 16))),			\
		[SRC2] "r" ((OFFSET(src, 32))),			\
		[SRC3] "r" ((OFFSET(src, 48))));		\
		break;						\
	case 2:							\
		__asm__ __volatile__(				\
		"lvx " VR0(r) " ,0,%[SRC0]\n"			\
		"lvx " VR1(r) " ,0,%[SRC1]\n"			\
		:	WVR0(r), WVR1(r)			\
		:	[SRC0] "r" ((OFFSET(src, 0))),		\
		[SRC1] "r" ((OFFSET(src, 16))));		\
		break;						\
	default:						\
		ZFS_ASM_BUG();					\
	}							\
}

#define	STORE(dst, r...)					\
{								\
	switch (REG_CNT(r)) {					\
	case 8:							\
		__asm__ __volatile__(				\
		"stvx " VR0(r) " ,0,%[DST0]\n"			\
		"stvx " VR1(r) " ,0,%[DST1]\n"			\
		"stvx " VR2(r) " ,0,%[DST2]\n"			\
		"stvx " VR3(r) " ,0,%[DST3]\n"			\
		"stvx " VR4(r) " ,0,%[DST4]\n"			\
		"stvx " VR5(r) " ,0,%[DST5]\n"			\
		"stvx " VR6(r) " ,0,%[DST6]\n"			\
		"stvx " VR7(r) " ,0,%[DST7]\n"			\
		: :	[DST0] "r" ((OFFSET(dst, 0))),		\
		[DST1] "r" ((OFFSET(dst, 16))),			\
		[DST2] "r" ((OFFSET(dst, 32))),			\
		[DST3] "r" ((OFFSET(dst, 48))),			\
		[DST4] "r" ((OFFSET(dst, 64))),			\
		[DST5] "r" ((OFFSET(dst, 80))),			\
		[DST6] "r" ((OFFSET(dst, 96))),			\
		[DST7] "r" ((OFFSET(dst, 112))),		\
		RVR0(r), RVR1(r), RVR2(r), RVR3(r),		\
		RVR4(r), RVR5(r), RVR6(r), RVR7(r)		\
		:	"memory");				\
		break;						\
	case 4:							\
		__asm__ __volatile__(				\
		"stvx " VR0(r) " ,0,%[DST0]\n"			\
		"stvx " VR1(r) " ,0,%[DST1]\n"			\
		"stvx " VR2(r) " ,0,%[DST2]\n"			\
		"stvx " VR3(r) " ,0,%[DST3]\n"			\
		: :	[DST0] "r" ((OFFSET(dst, 0))),		\
		[DST1] "r" ((OFFSET(dst, 16))),			\
		[DST2] "r" ((OFFSET(dst, 32))),			\
		[DST3] "r" ((OFFSET(dst, 48))),			\
		RVR0(r), RVR1(r), RVR2(r), RVR3(r)		\
		: "memory");					\
		break;						\
	case 2:							\
		__asm__ __volatile__(				\
		"stvx " VR0(r) " ,0,%[DST0]\n"			\
		"stvx " VR1(r) " ,0,%[DST1]\n"			\
		: :	[DST0] "r" ((OFFSET(dst, 0))),		\
		[DST1] "r" ((OFFSET(dst, 16))),			\
		RVR0(r), RVR1(r) : "memory");			\
		break;						\
	default:						\
		ZFS_ASM_BUG();					\
	}							\
}

/*
 * Unfortunately cannot use the macro, because GCC
 * will try to use the macro name and not value
 * later on...
 * Kept as a reference to what a numbered variable is
 */
#define	_00	"17"
#define	_1d	"16"
#define	_temp0	"19"
#define	_temp1	"18"

#define	MUL2_SETUP()						\
{								\
	__asm__ __volatile__(					\
		"vspltisb " VR(16) ",14\n"			\
		"vspltisb " VR(17) ",15\n"			\
		"vaddubm " VR(16) "," VR(17) "," VR(16) "\n"	\
		"vxor " VR(17) "," VR(17) "," VR(17) "\n"	\
		:	WVR(16), WVR(17));			\
}

#define	MUL2(r...)						\
{								\
	switch (REG_CNT(r)) {					\
	case 4:							\
		__asm__ __volatile__(				\
		"vcmpgtsb 19," VR(17) "," VR0(r) "\n"		\
		"vcmpgtsb 18," VR(17) "," VR1(r) "\n"		\
		"vcmpgtsb 21," VR(17) "," VR2(r) "\n"		\
		"vcmpgtsb 20," VR(17) "," VR3(r) "\n"		\
		"vand 19,19," VR(16) "\n"			\
		"vand 18,18," VR(16) "\n"			\
		"vand 21,21," VR(16) "\n"			\
		"vand 20,20," VR(16) "\n"			\
		"vaddubm " VR0(r) "," VR0(r) "," VR0(r) "\n"	\
		"vaddubm " VR1(r) "," VR1(r) "," VR1(r) "\n"	\
		"vaddubm " VR2(r) "," VR2(r) "," VR2(r) "\n"	\
		"vaddubm " VR3(r) "," VR3(r) "," VR3(r) "\n"	\
		"vxor " VR0(r) ",19," VR0(r) "\n"		\
		"vxor " VR1(r) ",18," VR1(r) "\n"		\
		"vxor " VR2(r) ",21," VR2(r) "\n"		\
		"vxor " VR3(r) ",20," VR3(r) "\n"		\
		:	UVR0(r), UVR1(r), UVR2(r), UVR3(r)	\
		:	RVR(17), RVR(16)			\
		:	"v18", "v19", "v20", "v21");		\
		break;						\
	case 2:							\
		__asm__ __volatile__(				\
		"vcmpgtsb 19," VR(17) "," VR0(r) "\n"		\
		"vcmpgtsb 18," VR(17) "," VR1(r) "\n"		\
		"vand 19,19," VR(16) "\n"			\
		"vand 18,18," VR(16) "\n"			\
		"vaddubm " VR0(r) "," VR0(r) "," VR0(r) "\n"	\
		"vaddubm " VR1(r) "," VR1(r) "," VR1(r) "\n"	\
		"vxor " VR0(r) ",19," VR0(r) "\n"		\
		"vxor " VR1(r) ",18," VR1(r) "\n"		\
		:	UVR0(r), UVR1(r)			\
		:	RVR(17), RVR(16)			\
		:	"v18", "v19");				\
		break;						\
	default:						\
		ZFS_ASM_BUG();					\
	}							\
}

#define	MUL4(r...)						\
{								\
	MUL2(r);						\
	MUL2(r);						\
}

/*
 * Unfortunately cannot use the macro, because GCC
 * will try to use the macro name and not value
 * later on...
 * Kept as a reference to what a register is
 * (here we're using actual registers for the
 * clobbered ones)
 */
#define	_0f		"15"
#define	_a_save		"14"
#define	_b_save		"13"
#define	_lt_mod_a	"12"
#define	_lt_clmul_a	"11"
#define	_lt_mod_b	"10"
#define	_lt_clmul_b	"15"

#define	_MULx2(c, r...)						\
{								\
	switch (REG_CNT(r)) {					\
	case 2:							\
		__asm__ __volatile__(				\
		/* lts for upper part */			\
		"vspltisb 15,15\n"				\
		"lvx 10,0,%[lt0]\n"				\
		"lvx 11,0,%[lt1]\n"				\
		/* upper part */				\
		"vand 14," VR0(r) ",15\n"			\
		"vand 13," VR1(r) ",15\n"			\
		"vspltisb 15,4\n"				\
		"vsrab " VR0(r) "," VR0(r) ",15\n"		\
		"vsrab " VR1(r) "," VR1(r) ",15\n"		\
								\
		"vperm 12,10,10," VR0(r) "\n"			\
		"vperm 10,10,10," VR1(r) "\n"			\
		"vperm 15,11,11," VR0(r) "\n"			\
		"vperm 11,11,11," VR1(r) "\n"			\
								\
		"vxor " VR0(r) ",15,12\n"			\
		"vxor " VR1(r) ",11,10\n"			\
		/* lts for lower part */			\
		"lvx 10,0,%[lt2]\n"				\
		"lvx 15,0,%[lt3]\n"				\
		/* lower part */				\
		"vperm 12,10,10,14\n"				\
		"vperm 10,10,10,13\n"				\
		"vperm 11,15,15,14\n"				\
		"vperm 15,15,15,13\n"				\
								\
		"vxor " VR0(r) "," VR0(r) ",12\n"		\
		"vxor " VR1(r) "," VR1(r) ",10\n"		\
		"vxor " VR0(r) "," VR0(r) ",11\n"		\
		"vxor " VR1(r) "," VR1(r) ",15\n"		\
		: UVR0(r), UVR1(r)				\
		: [lt0] "r" (&(gf_clmul_mod_lt[4*(c)+0][0])),	\
		[lt1] "r" (&(gf_clmul_mod_lt[4*(c)+1][0])),	\
		[lt2] "r" (&(gf_clmul_mod_lt[4*(c)+2][0])),	\
		[lt3] "r" (&(gf_clmul_mod_lt[4*(c)+3][0]))	\
		: "v10", "v11", "v12", "v13", "v14", "v15");	\
		break;						\
	default:						\
		ZFS_ASM_BUG();					\
	}							\
}

#define	MUL(c, r...)						\
{								\
	switch (REG_CNT(r)) {					\
	case 4:							\
		_MULx2(c, R_23(r));				\
		_MULx2(c, R_01(r));				\
		break;						\
	case 2:							\
		_MULx2(c, R_01(r));				\
		break;						\
	default:						\
		ZFS_ASM_BUG();					\
	}							\
}

#define	raidz_math_begin()	kfpu_begin()
#define	raidz_math_end()	kfpu_end()

/* Overkill... */
#if 0 // defined(_KERNEL)
#define	GEN_X_DEFINE_0_3()	\
register unsigned char w0 asm("0") __attribute__((vector_size(16)));	\
register unsigned char w1 asm("1") __attribute__((vector_size(16)));	\
register unsigned char w2 asm("2") __attribute__((vector_size(16)));	\
register unsigned char w3 asm("3") __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_4_5()	\
register unsigned char w4 asm("4") __attribute__((vector_size(16)));	\
register unsigned char w5 asm("5") __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_6_7()	\
register unsigned char w6 asm("6") __attribute__((vector_size(16)));	\
register unsigned char w7 asm("7") __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_8_9()	\
register unsigned char w8 asm("8") __attribute__((vector_size(16)));	\
register unsigned char w9 asm("9") __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_10_11()	\
register unsigned char w10 asm("10") __attribute__((vector_size(16)));	\
register unsigned char w11 asm("11") __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_12_15()	\
register unsigned char w12 asm("12") __attribute__((vector_size(16)));	\
register unsigned char w13 asm("13") __attribute__((vector_size(16)));	\
register unsigned char w14 asm("14") __attribute__((vector_size(16)));	\
register unsigned char w15 asm("15") __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_16()	\
register unsigned char w16 asm("16") __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_17()	\
register unsigned char w17 asm("17") __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_18_21()	\
register unsigned char w18 asm("18") __attribute__((vector_size(16)));	\
register unsigned char w19 asm("19") __attribute__((vector_size(16)));	\
register unsigned char w20 asm("20") __attribute__((vector_size(16)));	\
register unsigned char w21 asm("21") __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_22_23()	\
register unsigned char w22 asm("22") __attribute__((vector_size(16)));	\
register unsigned char w23 asm("23") __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_24_27()	\
register unsigned char w24 asm("24") __attribute__((vector_size(16)));	\
register unsigned char w25 asm("25") __attribute__((vector_size(16)));	\
register unsigned char w26 asm("26") __attribute__((vector_size(16)));	\
register unsigned char w27 asm("27") __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_28_30()	\
register unsigned char w28 asm("28") __attribute__((vector_size(16)));	\
register unsigned char w29 asm("29") __attribute__((vector_size(16)));	\
register unsigned char w30 asm("30") __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_31()	\
register unsigned char w31 asm("31") __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_32()	\
register unsigned char w32 asm("31") __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_33_36()	\
register unsigned char w33 asm("31") __attribute__((vector_size(16)));	\
register unsigned char w34 asm("31") __attribute__((vector_size(16)));	\
register unsigned char w35 asm("31") __attribute__((vector_size(16)));	\
register unsigned char w36 asm("31") __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_37_38()	\
register unsigned char w37 asm("31") __attribute__((vector_size(16)));	\
register unsigned char w38 asm("31") __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_ALL()	\
	GEN_X_DEFINE_0_3()	\
	GEN_X_DEFINE_4_5()	\
	GEN_X_DEFINE_6_7()	\
	GEN_X_DEFINE_8_9()	\
	GEN_X_DEFINE_10_11()	\
	GEN_X_DEFINE_12_15()	\
	GEN_X_DEFINE_16()	\
	GEN_X_DEFINE_17()	\
	GEN_X_DEFINE_18_21()	\
	GEN_X_DEFINE_22_23()	\
	GEN_X_DEFINE_24_27()	\
	GEN_X_DEFINE_28_30()	\
	GEN_X_DEFINE_31()	\
	GEN_X_DEFINE_32()	\
	GEN_X_DEFINE_33_36() 	\
	GEN_X_DEFINE_37_38()
#else
#define	GEN_X_DEFINE_0_3()	\
	unsigned char w0 __attribute__((vector_size(16)));	\
	unsigned char w1 __attribute__((vector_size(16)));	\
	unsigned char w2 __attribute__((vector_size(16)));	\
	unsigned char w3 __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_4_5()	\
	unsigned char w4 __attribute__((vector_size(16)));	\
	unsigned char w5 __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_6_7()	\
	unsigned char w6 __attribute__((vector_size(16)));	\
	unsigned char w7 __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_8_9()	\
	unsigned char w8 __attribute__((vector_size(16)));	\
	unsigned char w9 __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_10_11()	\
	unsigned char w10 __attribute__((vector_size(16)));	\
	unsigned char w11 __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_12_15()	\
	unsigned char w12 __attribute__((vector_size(16)));	\
	unsigned char w13 __attribute__((vector_size(16)));	\
	unsigned char w14 __attribute__((vector_size(16)));	\
	unsigned char w15 __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_16()	\
	unsigned char w16 __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_17()	\
	unsigned char w17 __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_18_21()	\
	unsigned char w18 __attribute__((vector_size(16)));	\
	unsigned char w19 __attribute__((vector_size(16)));	\
	unsigned char w20 __attribute__((vector_size(16)));	\
	unsigned char w21 __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_22_23()	\
	unsigned char w22 __attribute__((vector_size(16)));	\
	unsigned char w23 __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_24_27()	\
	unsigned char w24 __attribute__((vector_size(16)));	\
	unsigned char w25 __attribute__((vector_size(16)));	\
	unsigned char w26 __attribute__((vector_size(16)));	\
	unsigned char w27 __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_28_30()	\
	unsigned char w28 __attribute__((vector_size(16)));	\
	unsigned char w29 __attribute__((vector_size(16)));	\
	unsigned char w30 __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_31()	\
	unsigned char w31 __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_32()	\
	unsigned char w32 __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_33_36()	\
	unsigned char w33 __attribute__((vector_size(16)));	\
	unsigned char w34 __attribute__((vector_size(16)));	\
	unsigned char w35 __attribute__((vector_size(16)));	\
	unsigned char w36 __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_37_38()	\
	unsigned char w37 __attribute__((vector_size(16)));	\
	unsigned char w38 __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_ALL()	\
	GEN_X_DEFINE_0_3()	\
	GEN_X_DEFINE_4_5()	\
	GEN_X_DEFINE_6_7()	\
	GEN_X_DEFINE_8_9()	\
	GEN_X_DEFINE_10_11()	\
	GEN_X_DEFINE_12_15()	\
	GEN_X_DEFINE_16()	\
	GEN_X_DEFINE_17()	\
	GEN_X_DEFINE_18_21()	\
	GEN_X_DEFINE_22_23()	\
	GEN_X_DEFINE_24_27()	\
	GEN_X_DEFINE_28_30()	\
	GEN_X_DEFINE_31()	\
	GEN_X_DEFINE_32()	\
	GEN_X_DEFINE_33_36()	\
	GEN_X_DEFINE_37_38()
#endif
