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

#include <sys/isa_defs.h>

#if defined(__x86_64) && HAVE_SIMD(AVX2) && HAVE_SIMD(GFNI)

#include "vdev_raidz_math_avx2_common.h"

/*
 * GF2P8AFFINEQB multiplies every byte by an 8x8 bit matrix, so it can
 * multiply by a constant in the RAID-Z field (polynomial 0x11d), which
 * GF2P8MULB cannot: that one is fixed to the AES polynomial 0x11b.
 * gf_gfni_mul_lt[c] is the matrix of multiplication by c.
 */
extern const uint64_t gf_gfni_mul_lt[256];

#define	_mulc		"ymm13"
#define	_mul2		"ymm14"
#define	_mul4		"ymm15"

#define	_GF_AFFINE(m, r...)						\
{									\
	switch (REG_CNT(r)) {						\
	case 4:								\
		__asm(							\
		    "vgf2p8affineqb $0, %" m ", %" VR0(r) ", %" VR0(r) "\n"\
		    "vgf2p8affineqb $0, %" m ", %" VR1(r) ", %" VR1(r) "\n"\
		    "vgf2p8affineqb $0, %" m ", %" VR2(r) ", %" VR2(r) "\n"\
		    "vgf2p8affineqb $0, %" m ", %" VR3(r) ", %" VR3(r));\
		break;							\
	case 2:								\
		__asm(							\
		    "vgf2p8affineqb $0, %" m ", %" VR0(r) ", %" VR0(r) "\n"\
		    "vgf2p8affineqb $0, %" m ", %" VR1(r) ", %" VR1(r));\
		break;							\
	default:							\
		ZFS_ASM_BUG();						\
	}								\
}

#define	MUL2_SETUP()							\
{									\
	__asm("vpbroadcastq %0, %%" _mul2 :: "m" (gf_gfni_mul_lt[2]));	\
	__asm("vpbroadcastq %0, %%" _mul4 :: "m" (gf_gfni_mul_lt[4]));	\
}

#define	MUL2(r...)	_GF_AFFINE(_mul2, r)
#define	MUL4(r...)	_GF_AFFINE(_mul4, r)

#define	MUL(c, r...)							\
{									\
	__asm("vpbroadcastq %0, %%" _mulc :: "m" (gf_gfni_mul_lt[c]));	\
	_GF_AFFINE(_mulc, r);						\
}

#include <sys/vdev_raidz_impl.h>
#include "vdev_raidz_math_impl.h"

DEFINE_GEN_METHODS(avx2_gfni);
DEFINE_REC_METHODS(avx2_gfni);

static boolean_t
raidz_will_avx2_gfni_work(void)
{
	return (kfpu_allowed() && zfs_avx_available() &&
	    zfs_avx2_available() && zfs_gfni_available());
}

const raidz_impl_ops_t vdev_raidz_avx2_gfni_impl = {
	.init = NULL,
	.fini = NULL,
	.gen = RAIDZ_GEN_METHODS(avx2_gfni),
	.rec = RAIDZ_REC_METHODS(avx2_gfni),
	.is_supported = &raidz_will_avx2_gfni_work,
	.name = "avx2_gfni"
};

#endif /* defined(__x86_64) && HAVE_SIMD(AVX2) && HAVE_SIMD(GFNI) */
