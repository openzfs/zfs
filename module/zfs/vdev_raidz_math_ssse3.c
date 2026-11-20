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
 * Copyright (C) 2016 Gvozden Nešković. All rights reserved.
 */

#include <sys/isa_defs.h>

#if defined(__x86_64) && HAVE_SIMD(SSSE3)

#include <sys/types.h>
#include <sys/simd.h>

#ifdef __linux__
#define	__asm __asm__ __volatile__
#endif

#define	_REG_CNT(_0, _1, _2, _3, _4, _5, _6, _7, N, ...) N
#define	REG_CNT(r...) _REG_CNT(r, 8, 7, 6, 5, 4, 3, 2, 1)

#define	VR0_(REG, ...) "xmm"#REG
#define	VR1_(_1, REG, ...) "xmm"#REG
#define	VR2_(_1, _2, REG, ...) "xmm"#REG
#define	VR3_(_1, _2, _3, REG, ...) "xmm"#REG
#define	VR4_(_1, _2, _3, _4, REG, ...) "xmm"#REG
#define	VR5_(_1, _2, _3, _4, _5, REG, ...) "xmm"#REG
#define	VR6_(_1, _2, _3, _4, _5, _6, REG, ...) "xmm"#REG
#define	VR7_(_1, _2, _3, _4, _5, _6, _7, REG, ...) "xmm"#REG

#define	VR0(r...) VR0_(r)
#define	VR1(r...) VR1_(r)
#define	VR2(r...) VR2_(r, 1)
#define	VR3(r...) VR3_(r, 1, 2)
#define	VR4(r...) VR4_(r, 1, 2)
#define	VR5(r...) VR5_(r, 1, 2, 3)
#define	VR6(r...) VR6_(r, 1, 2, 3, 4)
#define	VR7(r...) VR7_(r, 1, 2, 3, 4, 5)

#define	R_01(REG1, REG2, ...) REG1, REG2
#define	_R_23(_0, _1, REG2, REG3, ...) REG2, REG3
#define	R_23(REG...) _R_23(REG, 1, 2, 3)

#define	ZFS_ASM_BUG()	ASSERT(0)

extern uint8_t gf_mul_nibble_lt[256][2][16];
extern void gf_mul_nibble_lt_init(void);

#define	ELEM_SIZE 16

typedef struct v {
	uint8_t b[ELEM_SIZE] __attribute__((aligned(ELEM_SIZE)));
} v_t;


#define	XOR_ACC(src, r...) 						\
{									\
	switch (REG_CNT(r)) {						\
	case 4:								\
		__asm(							\
		    "pxor 0x00(%[SRC]), %%" VR0(r) "\n"			\
		    "pxor 0x10(%[SRC]), %%" VR1(r) "\n"			\
		    "pxor 0x20(%[SRC]), %%" VR2(r) "\n"			\
		    "pxor 0x30(%[SRC]), %%" VR3(r) "\n"			\
		    : : [SRC] "r" (src));				\
		break;							\
	case 2:								\
		__asm(							\
		    "pxor 0x00(%[SRC]), %%" VR0(r) "\n"			\
		    "pxor 0x10(%[SRC]), %%" VR1(r) "\n"			\
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
		    "pxor %" VR0(r) ", %" VR4(r) "\n"			\
		    "pxor %" VR1(r) ", %" VR5(r) "\n"			\
		    "pxor %" VR2(r) ", %" VR6(r) "\n"			\
		    "pxor %" VR3(r) ", %" VR7(r));			\
		break;							\
	case 4:								\
		__asm(							\
		    "pxor %" VR0(r) ", %" VR2(r) "\n"			\
		    "pxor %" VR1(r) ", %" VR3(r));			\
		break;							\
	default:							\
		ZFS_ASM_BUG();						\
	}								\
}

#define	ZERO(r...)	XOR(r, r)

#define	COPY(r...) 							\
{									\
	switch (REG_CNT(r)) {						\
	case 8:								\
		__asm(							\
		    "movdqa %" VR0(r) ", %" VR4(r) "\n"			\
		    "movdqa %" VR1(r) ", %" VR5(r) "\n"			\
		    "movdqa %" VR2(r) ", %" VR6(r) "\n"			\
		    "movdqa %" VR3(r) ", %" VR7(r));			\
		break;							\
	case 4:								\
		__asm(							\
		    "movdqa %" VR0(r) ", %" VR2(r) "\n"			\
		    "movdqa %" VR1(r) ", %" VR3(r));			\
		break;							\
	default:							\
		ZFS_ASM_BUG();						\
	}								\
}

#define	LOAD(src, r...) 						\
{									\
	switch (REG_CNT(r)) {						\
	case 4:								\
		__asm(							\
		    "movdqa 0x00(%[SRC]), %%" VR0(r) "\n"		\
		    "movdqa 0x10(%[SRC]), %%" VR1(r) "\n"		\
		    "movdqa 0x20(%[SRC]), %%" VR2(r) "\n"		\
		    "movdqa 0x30(%[SRC]), %%" VR3(r) "\n"		\
		    : : [SRC] "r" (src));				\
		break;							\
	case 2:								\
		__asm(							\
		    "movdqa 0x00(%[SRC]), %%" VR0(r) "\n"		\
		    "movdqa 0x10(%[SRC]), %%" VR1(r) "\n"		\
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
		    "movdqa %%" VR0(r)", 0x00(%[DST])\n"		\
		    "movdqa %%" VR1(r)", 0x10(%[DST])\n"		\
		    "movdqa %%" VR2(r)", 0x20(%[DST])\n"		\
		    "movdqa %%" VR3(r)", 0x30(%[DST])\n"		\
		    : : [DST] "r" (dst));				\
		break;							\
	case 2:								\
		__asm(							\
		    "movdqa %%" VR0(r)", 0x00(%[DST])\n"		\
		    "movdqa %%" VR1(r)", 0x10(%[DST])\n"		\
		    : : [DST] "r" (dst));				\
		break;							\
	default:							\
		ZFS_ASM_BUG();						\
	}								\
}

#define	MUL2_SETUP()							\
{   									\
	__asm(								\
	    "movd %[mask], %%xmm15\n"					\
	    "pshufd $0x0, %%xmm15, %%xmm15\n"				\
	    : : [mask] "r" (0x1d1d1d1d));				\
}

#define	_MUL2_x2(r...) 							\
{									\
	switch (REG_CNT(r)) {						\
	case 2:								\
		__asm(							\
		    "pxor    %xmm14,      %xmm14\n"			\
		    "pxor    %xmm13,      %xmm13\n"			\
		    "pcmpgtb %" VR0(r)",  %xmm14\n"			\
		    "pcmpgtb %" VR1(r)",  %xmm13\n"			\
		    "pand    %xmm15,      %xmm14\n"			\
		    "pand    %xmm15,      %xmm13\n"			\
		    "paddb   %" VR0(r)",  %" VR0(r) "\n"		\
		    "paddb   %" VR1(r)",  %" VR1(r) "\n"		\
		    "pxor    %xmm14,      %" VR0(r) "\n"		\
		    "pxor    %xmm13,      %" VR1(r));			\
		break;							\
	default:							\
		ZFS_ASM_BUG();						\
	}								\
}

#define	MUL2(r...)							\
{									\
	switch (REG_CNT(r)) {						\
	case 4:								\
		_MUL2_x2(R_01(r));					\
		_MUL2_x2(R_23(r));					\
		break;							\
	case 2:								\
		_MUL2_x2(r);						\
		break;							\
	default:							\
		ZFS_ASM_BUG();						\
	}								\
}

#define	MUL4(r...)							\
{									\
	MUL2(r);							\
	MUL2(r);							\
}

#define	_0f		"xmm15"
#define	_a_lo		"xmm14"
#define	_b_lo		"xmm13"
#define	_a_hi		"xmm12"
#define	_b_hi		"xmm11"

#define	_MULx2(c, r...)							\
{									\
	switch (REG_CNT(r)) {						\
	case 2:								\
		__asm(							\
		    "movd %[mask], %%" _0f "\n"				\
		    "pshufd $0x0, %%" _0f ", %%" _0f "\n"		\
		    "movdqa %%" VR0(r) ", %%" _a_lo "\n"		\
		    "movdqa %%" VR1(r) ", %%" _b_lo "\n"		\
		    "psraw $0x4, %%" VR0(r) "\n"			\
		    "psraw $0x4, %%" VR1(r) "\n"			\
		    "pand %%" _0f ", %%" _a_lo "\n"			\
		    "pand %%" _0f ", %%" _b_lo "\n"			\
		    "pand %%" _0f ", %%" VR0(r) "\n"			\
		    "pand %%" _0f ", %%" VR1(r) "\n"			\
		    /* high nibbles */					\
		    "movdqa 0x10(%[lt]), %%" _a_hi "\n"			\
		    "movdqa %%" _a_hi ", %%" _b_hi "\n"			\
		    "pshufb %%" VR0(r) ", %%" _a_hi "\n"		\
		    "pshufb %%" VR1(r) ", %%" _b_hi "\n"		\
		    /* low nibbles */					\
		    "movdqa 0x00(%[lt]), %%" VR0(r) "\n"		\
		    "movdqa %%" VR0(r) ", %%" VR1(r) "\n"		\
		    "pshufb %%" _a_lo ", %%" VR0(r) "\n"		\
		    "pshufb %%" _b_lo ", %%" VR1(r) "\n"		\
									\
		    "pxor %%" _a_hi ", %%" VR0(r) "\n"			\
		    "pxor %%" _b_hi ", %%" VR1(r) "\n"			\
		    : : [mask] "r" (0x0f0f0f0f),			\
		    [lt] "r" (gf_mul_nibble_lt[c]));			\
		break;							\
	default:							\
		ZFS_ASM_BUG();						\
	}								\
}

#define	MUL(c, r...) 							\
{									\
	switch (REG_CNT(r)) {						\
	case 4:								\
		_MULx2(c, R_23(r));					\
		_MULx2(c, R_01(r));					\
		break;							\
	case 2:								\
		_MULx2(c, R_01(r));					\
		break;							\
	default:							\
		ZFS_ASM_BUG();						\
	}								\
}

#define	raidz_math_begin(ctx)	kfpu_begin(ctx)
#define	raidz_math_end(ctx)	kfpu_end(ctx)


#define	COPY_STRIDE		4
#define	COPY_DEFINE()		{}
#define	COPY_D			0, 1, 2, 3

#define	GEN_P_STRIDE		4
#define	GEN_P_DEFINE()		{}
#define	GEN_P_P			0, 1, 2, 3

#define	GEN_PQ_STRIDE		4
#define	GEN_PQ_DEFINE() 	{}
#define	GEN_PQ_P		0, 1, 2, 3
#define	GEN_PQ_Q		4, 5, 6, 7

#define	GEN_PQR_STRIDE		4
#define	GEN_PQR_DEFINE() 	{}
#define	GEN_PQR_P		0, 1, 2, 3
#define	GEN_PQR_Q		4, 5, 6, 7
#define	GEN_PQR_R		8, 9, 10, 11

#define	REC_Q_STRIDE		4
#define	REC_Q_DEFINE()		{}
#define	REC_Q_X			0, 1, 2, 3

#define	REC_R_STRIDE		4
#define	REC_R_DEFINE()		{}
#define	REC_R_X			0, 1, 2, 3

#define	REC_PQ_STRIDE		2
#define	REC_PQ_DEFINE() 	{}
#define	REC_PQ_X		0, 1
#define	REC_PQ_Y		2, 3
#define	REC_PQ_T		4, 5

#define	REC_PR_STRIDE		2
#define	REC_PR_DEFINE() 	{}
#define	REC_PR_X		0, 1
#define	REC_PR_Y		2, 3
#define	REC_PR_T		4, 5

#define	REC_QR_STRIDE		2
#define	REC_QR_DEFINE() 	{}
#define	REC_QR_X		0, 1
#define	REC_QR_Y		2, 3
#define	REC_QR_T		4, 5

#define	REC_PQR_STRIDE		2
#define	REC_PQR_DEFINE() 	{}
#define	REC_PQR_X		0, 1
#define	REC_PQR_Y		2, 3
#define	REC_PQR_Z		4, 5
#define	REC_PQR_XS		6, 7
#define	REC_PQR_YS		8, 9


#include <sys/vdev_raidz_impl.h>
#include "vdev_raidz_math_impl.h"

DEFINE_GEN_METHODS(ssse3);
DEFINE_REC_METHODS(ssse3);

static boolean_t
raidz_will_ssse3_work(void)
{
	return (kfpu_allowed() && zfs_sse_available() &&
	    zfs_sse2_available() && zfs_ssse3_available());
}

const raidz_impl_ops_t vdev_raidz_ssse3_impl = {
	.init = gf_mul_nibble_lt_init,
	.fini = NULL,
	.gen = RAIDZ_GEN_METHODS(ssse3),
	.rec = RAIDZ_REC_METHODS(ssse3),
	.is_supported = &raidz_will_ssse3_work,
	.name = "ssse3"
};

#endif /* defined(__x86_64) && HAVE_SIMD(SSSE3) */


#if defined(__x86_64)
#if HAVE_SIMD(SSSE3) || HAVE_SIMD(AVX2) || HAVE_SIMD(AVX512BW)
#include <sys/vdev_raidz_impl.h>

/*
 * gf_mul_nibble_lt[c][0][i] is c * i and gf_mul_nibble_lt[c][1][i] is
 * c * (i << 4), for every nibble i: the MUL macros multiply each byte x of
 * a vector by c with one table lookup per nibble of x, and XOR the two.
 */
uint8_t __attribute__((aligned(256))) gf_mul_nibble_lt[256][2][16];

void
gf_mul_nibble_lt_init(void)
{
	for (int c = 0; c < 256; c++) {
		for (int i = 0; i < 16; i++) {
			gf_mul_nibble_lt[c][0][i] = gf_mul(c, i);
			gf_mul_nibble_lt[c][1][i] = gf_mul(c, i << 4);
		}
	}
}
#endif /* HAVE_SIMD(SSSE3) || HAVE_SIMD(AVX2) || HAVE_SIMD(AVX512BW) */
#endif /* defined(__x86_64) */
