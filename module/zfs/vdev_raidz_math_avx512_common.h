// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */
/*
 * Copyright (C) 2016 Romain Dolbeau. All rights reserved.
 * Copyright (C) 2016 Gvozden Nešković. All rights reserved.
 */

/*
 * Shared by the avx512bw and avx512_gfni implementations, which differ only in
 * their multiplication macros.
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/simd.h>


#ifdef __linux__
#define	__asm __asm__ __volatile__
#endif

#define	_REG_CNT(_0, _1, _2, _3, _4, _5, _6, _7, N, ...) N
#define	REG_CNT(r...) _REG_CNT(r, 8, 7, 6, 5, 4, 3, 2, 1)

#define	VR0_(REG, ...) "zmm"#REG
#define	VR1_(_1, REG, ...) "zmm"#REG
#define	VR2_(_1, _2, REG, ...) "zmm"#REG
#define	VR3_(_1, _2, _3, REG, ...) "zmm"#REG
#define	VR4_(_1, _2, _3, _4, REG, ...) "zmm"#REG
#define	VR5_(_1, _2, _3, _4, _5, REG, ...) "zmm"#REG
#define	VR6_(_1, _2, _3, _4, _5, _6, REG, ...) "zmm"#REG
#define	VR7_(_1, _2, _3, _4, _5, _6, _7, REG, ...) "zmm"#REG

#define	VR0(r...) VR0_(r)
#define	VR1(r...) VR1_(r)
#define	VR2(r...) VR2_(r, 1)
#define	VR3(r...) VR3_(r, 1, 2)
#define	VR4(r...) VR4_(r, 1, 2)
#define	VR5(r...) VR5_(r, 1, 2, 3)
#define	VR6(r...) VR6_(r, 1, 2, 3, 4)
#define	VR7(r...) VR7_(r, 1, 2, 3, 4, 5)

#define	ZFS_ASM_BUG()	ASSERT(0)

#define	ELEM_SIZE 64

typedef struct v {
	uint8_t b[ELEM_SIZE] __attribute__((aligned(ELEM_SIZE)));
} v_t;

#define	XOR_ACC(src, r...)						\
{									\
	switch (REG_CNT(r)) {						\
	case 4:								\
		__asm(							\
		    "vpxorq 0x00(%[SRC]), %%" VR0(r)", %%" VR0(r) "\n"	\
		    "vpxorq 0x40(%[SRC]), %%" VR1(r)", %%" VR1(r) "\n"	\
		    "vpxorq 0x80(%[SRC]), %%" VR2(r)", %%" VR2(r) "\n"	\
		    "vpxorq 0xc0(%[SRC]), %%" VR3(r)", %%" VR3(r) "\n"	\
		    : : [SRC] "r" (src));				\
		break;							\
	case 2:								\
		__asm(							\
		    "vpxorq 0x00(%[SRC]), %%" VR0(r)", %%" VR0(r) "\n"	\
		    "vpxorq 0x40(%[SRC]), %%" VR1(r)", %%" VR1(r) "\n"	\
		    : : [SRC] "r" (src));				\
		break;							\
	default:							\
		ZFS_ASM_BUG();						\
	}								\
}

#define	XOR(r...)							\
{									\
	switch (REG_CNT(r)) {						\
	case 8:								\
		__asm(							\
		    "vpxorq %" VR0(r) ", %" VR4(r)", %" VR4(r) "\n"	\
		    "vpxorq %" VR1(r) ", %" VR5(r)", %" VR5(r) "\n"	\
		    "vpxorq %" VR2(r) ", %" VR6(r)", %" VR6(r) "\n"	\
		    "vpxorq %" VR3(r) ", %" VR7(r)", %" VR7(r));	\
		break;							\
	case 4:								\
		__asm(							\
		    "vpxorq %" VR0(r) ", %" VR2(r)", %" VR2(r) "\n"	\
		    "vpxorq %" VR1(r) ", %" VR3(r)", %" VR3(r));	\
		break;							\
	default:							\
		ZFS_ASM_BUG();						\
	}								\
}

#define	ZERO(r...)	XOR(r, r)

#define	COPY(r...)							\
{									\
	switch (REG_CNT(r)) {						\
	case 8:								\
		__asm(							\
		    "vmovdqa64 %" VR0(r) ", %" VR4(r) "\n"		\
		    "vmovdqa64 %" VR1(r) ", %" VR5(r) "\n"		\
		    "vmovdqa64 %" VR2(r) ", %" VR6(r) "\n"		\
		    "vmovdqa64 %" VR3(r) ", %" VR7(r));			\
		break;							\
	case 4:								\
		__asm(							\
		    "vmovdqa64 %" VR0(r) ", %" VR2(r) "\n"		\
		    "vmovdqa64 %" VR1(r) ", %" VR3(r));			\
		break;							\
	default:							\
		ZFS_ASM_BUG();						\
	}								\
}

#define	LOAD(src, r...)							\
{									\
	switch (REG_CNT(r)) {						\
	case 4:								\
		__asm(							\
		    "vmovdqa64 0x00(%[SRC]), %%" VR0(r) "\n"		\
		    "vmovdqa64 0x40(%[SRC]), %%" VR1(r) "\n"		\
		    "vmovdqa64 0x80(%[SRC]), %%" VR2(r) "\n"		\
		    "vmovdqa64 0xc0(%[SRC]), %%" VR3(r) "\n"		\
		    : : [SRC] "r" (src));				\
		break;							\
	case 2:								\
		__asm(							\
		    "vmovdqa64 0x00(%[SRC]), %%" VR0(r) "\n"		\
		    "vmovdqa64 0x40(%[SRC]), %%" VR1(r) "\n"		\
		    : : [SRC] "r" (src));				\
		break;							\
	default:							\
		ZFS_ASM_BUG();						\
	}								\
}

#define	STORE(dst, r...)						\
{									\
	switch (REG_CNT(r)) {						\
	case 4:								\
		__asm(							\
		    "vmovdqa64 %%" VR0(r) ", 0x00(%[DST])\n"		\
		    "vmovdqa64 %%" VR1(r) ", 0x40(%[DST])\n"		\
		    "vmovdqa64 %%" VR2(r) ", 0x80(%[DST])\n"		\
		    "vmovdqa64 %%" VR3(r) ", 0xc0(%[DST])\n"		\
		    : : [DST] "r" (dst));				\
		break;							\
	case 2:								\
		__asm(							\
		    "vmovdqa64 %%" VR0(r) ", 0x00(%[DST])\n"		\
		    "vmovdqa64 %%" VR1(r) ", 0x40(%[DST])\n"		\
		    : : [DST] "r" (dst));				\
		break;							\
	default:							\
		ZFS_ASM_BUG();						\
	}								\
}

#define	raidz_math_begin()	kfpu_begin()
#define	raidz_math_end()	kfpu_end()

/*
 * ZERO, COPY, and MUL operations are already 2x unrolled, which means that
 * the stride of these operations for avx512 must not exceed 4. Otherwise, a
 * single step would exceed 512B block size.
 */

#define	SYN_STRIDE		4

#define	ZERO_STRIDE		4
#define	ZERO_DEFINE()		{}
#define	ZERO_D			0, 1, 2, 3

#define	COPY_STRIDE		4
#define	COPY_DEFINE()		{}
#define	COPY_D			0, 1, 2, 3

#define	ADD_STRIDE		4
#define	ADD_DEFINE()		{}
#define	ADD_D			0, 1, 2, 3

#define	MUL_STRIDE		4
#define	MUL_DEFINE()		{}
#define	MUL_D			0, 1, 2, 3

#define	GEN_P_STRIDE		4
#define	GEN_P_DEFINE()		{}
#define	GEN_P_P			0, 1, 2, 3

#define	GEN_PQ_STRIDE		4
#define	GEN_PQ_DEFINE()		{}
#define	GEN_PQ_D		0, 1, 2, 3
#define	GEN_PQ_C		4, 5, 6, 7

#define	GEN_PQR_STRIDE		4
#define	GEN_PQR_DEFINE()	{}
#define	GEN_PQR_D		0, 1, 2, 3
#define	GEN_PQR_C		4, 5, 6, 7

#define	SYN_Q_DEFINE()		{}
#define	SYN_Q_D			0, 1, 2, 3
#define	SYN_Q_X			4, 5, 6, 7

#define	SYN_R_DEFINE()		{}
#define	SYN_R_D			0, 1, 2, 3
#define	SYN_R_X			4, 5, 6, 7

#define	SYN_PQ_DEFINE()		{}
#define	SYN_PQ_D		0, 1, 2, 3
#define	SYN_PQ_X		4, 5, 6, 7

#define	REC_PQ_STRIDE		2
#define	REC_PQ_DEFINE()		{}
#define	REC_PQ_X		0, 1
#define	REC_PQ_Y		2, 3
#define	REC_PQ_T		4, 5

#define	SYN_PR_DEFINE()		{}
#define	SYN_PR_D		0, 1, 2, 3
#define	SYN_PR_X		4, 5, 6, 7

#define	REC_PR_STRIDE		2
#define	REC_PR_DEFINE()		{}
#define	REC_PR_X		0, 1
#define	REC_PR_Y		2, 3
#define	REC_PR_T		4, 5

#define	SYN_QR_DEFINE()		{}
#define	SYN_QR_D		0, 1, 2, 3
#define	SYN_QR_X		4, 5, 6, 7

#define	REC_QR_STRIDE		2
#define	REC_QR_DEFINE()		{}
#define	REC_QR_X		0, 1
#define	REC_QR_Y		2, 3
#define	REC_QR_T		4, 5

#define	SYN_PQR_DEFINE()	{}
#define	SYN_PQR_D		0, 1, 2, 3
#define	SYN_PQR_X		4, 5, 6, 7

#define	REC_PQR_STRIDE		2
#define	REC_PQR_DEFINE()	{}
#define	REC_PQR_X		0, 1
#define	REC_PQR_Y		2, 3
#define	REC_PQR_Z		4, 5
#define	REC_PQR_XS		6, 7
#define	REC_PQR_YS		8, 9
