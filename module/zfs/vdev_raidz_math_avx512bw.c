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

#include <sys/isa_defs.h>

#if defined(__x86_64) && HAVE_SIMD(AVX512BW)

#include "vdev_raidz_math_avx512_common.h"

#define	R_01(REG1, REG2, ...) REG1, REG2
#define	_R_23(_0, _1, REG2, REG3, ...) REG2, REG3
#define	R_23(REG...) _R_23(REG, 1, 2, 3)

extern const uint8_t gf_clmul_mod_lt[4*256][16];

#define	MUL2_SETUP() 							\
{   									\
	__asm("vmovq %0,    %%xmm22" :: "r"(0x1d1d1d1d1d1d1d1d));	\
	__asm("vpbroadcastq  %xmm22, %zmm22");				\
	__asm("vpxord        %zmm23, %zmm23 ,%zmm23");			\
}

#define	_MUL2(r...)							\
{									\
	switch	(REG_CNT(r)) {						\
	case 2:								\
		__asm(							\
		    "vpcmpb $1, %zmm23,     %" VR0(r)", %k1\n"		\
		    "vpcmpb $1, %zmm23,     %" VR1(r)", %k2\n"		\
		    "vpaddb     %" VR0(r)", %" VR0(r)", %" VR0(r) "\n"	\
		    "vpaddb     %" VR1(r)", %" VR1(r)", %" VR1(r) "\n"	\
		    "vpxord     %zmm22,     %" VR0(r)", %zmm12\n"	\
		    "vpxord     %zmm22,     %" VR1(r)", %zmm13\n"	\
		    "vmovdqu8   %zmm12,     %" VR0(r) "{%k1}\n"		\
		    "vmovdqu8   %zmm13,     %" VR1(r) "{%k2}");		\
		break;							\
	default:							\
		ZFS_ASM_BUG();						\
	}								\
}

#define	MUL2(r...)							\
{									\
	switch (REG_CNT(r)) {						\
	case 4:								\
	    _MUL2(R_01(r));						\
	    _MUL2(R_23(r));						\
	    break;							\
	case 2:								\
	    _MUL2(r);							\
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

#define	_0f		"zmm15"
#define	_as		"zmm14"
#define	_bs		"zmm13"
#define	_ltmod		"zmm12"
#define	_ltmul		"zmm11"
#define	_ta		"zmm10"
#define	_tb		"zmm15"

static const uint8_t __attribute__((aligned(64))) _mul_mask = 0x0F;

#define	_MULx2(c, r...)							\
{									\
	switch (REG_CNT(r)) {						\
	case 2:								\
		__asm(							\
		    "vpbroadcastb (%[mask]), %%" _0f "\n"		\
		    /* upper bits */					\
		    "vbroadcasti32x4 0x00(%[lt]), %%" _ltmod "\n"	\
		    "vbroadcasti32x4 0x10(%[lt]), %%" _ltmul "\n"	\
									\
		    "vpsraw $0x4, %%" VR0(r) ", %%"_as "\n"		\
		    "vpsraw $0x4, %%" VR1(r) ", %%"_bs "\n"		\
		    "vpandq %%" _0f ", %%" VR0(r) ", %%" VR0(r) "\n"	\
		    "vpandq %%" _0f ", %%" VR1(r) ", %%" VR1(r) "\n"	\
		    "vpandq %%" _0f ", %%" _as ", %%" _as "\n"		\
		    "vpandq %%" _0f ", %%" _bs ", %%" _bs "\n"		\
									\
		    "vpshufb %%" _as ", %%" _ltmod ", %%" _ta "\n"	\
		    "vpshufb %%" _bs ", %%" _ltmod ", %%" _tb "\n"	\
		    "vpshufb %%" _as ", %%" _ltmul ", %%" _as "\n"	\
		    "vpshufb %%" _bs ", %%" _ltmul ", %%" _bs "\n"	\
		    /* lower bits */					\
		    "vbroadcasti32x4 0x20(%[lt]), %%" _ltmod "\n"	\
		    "vbroadcasti32x4 0x30(%[lt]), %%" _ltmul "\n"	\
									\
		    "vpxorq %%" _ta ", %%" _as ", %%" _as "\n"		\
		    "vpxorq %%" _tb ", %%" _bs ", %%" _bs "\n"		\
									\
		    "vpshufb %%" VR0(r) ", %%" _ltmod ", %%" _ta "\n"	\
		    "vpshufb %%" VR1(r) ", %%" _ltmod ", %%" _tb "\n"	\
		    "vpshufb %%" VR0(r) ", %%" _ltmul ", %%" VR0(r) "\n"\
		    "vpshufb %%" VR1(r) ", %%" _ltmul ", %%" VR1(r) "\n"\
									\
		    "vpxorq %%" _ta ", %%" VR0(r) ", %%" VR0(r) "\n"	\
		    "vpxorq %%" _as ", %%" VR0(r) ", %%" VR0(r) "\n"	\
		    "vpxorq %%" _tb ", %%" VR1(r) ", %%" VR1(r) "\n"	\
		    "vpxorq %%" _bs ", %%" VR1(r) ", %%" VR1(r) "\n"	\
		    : : [mask] "r" (&_mul_mask),			\
		    [lt] "r" (gf_clmul_mod_lt[4*(c)]));			\
		break;							\
	default:							\
		ZFS_ASM_BUG();						\
	}								\
}

#define	MUL(c, r...)							\
{									\
	switch (REG_CNT(r)) {						\
	case 4:								\
		_MULx2(c, R_01(r));					\
		_MULx2(c, R_23(r));					\
		break;							\
	case 2:								\
		_MULx2(c, R_01(r));					\
		break;							\
	default:							\
		ZFS_ASM_BUG();						\
	}								\
}

#include <sys/vdev_raidz_impl.h>
#include "vdev_raidz_math_impl.h"

DEFINE_GEN_METHODS(avx512bw);
DEFINE_REC_METHODS(avx512bw);

static boolean_t
raidz_will_avx512bw_work(void)
{
	return (kfpu_allowed() && zfs_avx_available() &&
	    zfs_avx512f_available() && zfs_avx512bw_available());
}

const raidz_impl_ops_t vdev_raidz_avx512bw_impl = {
	.init = NULL,
	.fini = NULL,
	.gen = RAIDZ_GEN_METHODS(avx512bw),
	.rec = RAIDZ_REC_METHODS(avx512bw),
	.is_supported = &raidz_will_avx512bw_work,
	.name = "avx512bw"
};

#endif /* defined(__x86_64) && HAVE_SIMD(AVX512BW) */
