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
 */

#include <sys/isa_defs.h>
#include <sys/types.h>

#if defined(__aarch64__)

#include "vdev_raidz_math_aarch64_neon_common.h"

#define	COPY_STRIDE		4
#define	COPY_DEFINE()	\
	GEN_X_DEFINE_0_3() \
	GEN_X_DEFINE_33_36()
#define	COPY_D			0, 1, 2, 3

#define	GEN_P_DEFINE() \
	GEN_X_DEFINE_0_3() \
	GEN_X_DEFINE_33_36()
#define	GEN_P_STRIDE		4
#define	GEN_P_P			0, 1, 2, 3

#define	GEN_PQ_DEFINE()	\
	GEN_X_DEFINE_0_3()	\
	GEN_X_DEFINE_4_5()	\
	GEN_X_DEFINE_6_7()	\
	GEN_X_DEFINE_16()	\
	GEN_X_DEFINE_17()	\
	GEN_X_DEFINE_33_36()
#define	GEN_PQ_STRIDE		4
#define	GEN_PQ_P		0, 1, 2, 3
#define	GEN_PQ_Q		4, 5, 6, 7

#define	GEN_PQR_DEFINE() \
	GEN_X_DEFINE_0_3()	\
	GEN_X_DEFINE_4_5()	\
	GEN_X_DEFINE_6_7()	\
	GEN_X_DEFINE_8_9()	\
	GEN_X_DEFINE_10_11()	\
	GEN_X_DEFINE_16()	\
	GEN_X_DEFINE_17()	\
	GEN_X_DEFINE_33_36()
#define	GEN_PQR_STRIDE		4
#define	GEN_PQR_P		0, 1, 2, 3
#define	GEN_PQR_Q		4, 5, 6, 7
#define	GEN_PQR_R		8, 9, 10, 11

#define	REC_Q_STRIDE		4
#define	REC_Q_DEFINE() \
	GEN_X_DEFINE_0_3()	\
	GEN_X_DEFINE_16()	\
	GEN_X_DEFINE_17()	\
	GEN_X_DEFINE_33_36()
#define	REC_Q_X			0, 1, 2, 3

#define	REC_R_STRIDE		4
#define	REC_R_DEFINE() \
	GEN_X_DEFINE_0_3()	\
	GEN_X_DEFINE_16()	\
	GEN_X_DEFINE_17()	\
	GEN_X_DEFINE_33_36()
#define	REC_R_X			0, 1, 2, 3

#define	REC_PQ_DEFINE() \
	GEN_X_DEFINE_0_3()	\
	GEN_X_DEFINE_4_5()	\
	GEN_X_DEFINE_16()	\
	GEN_X_DEFINE_17()	\
	GEN_X_DEFINE_31()	\
	GEN_X_DEFINE_32()	\
	GEN_X_DEFINE_33_36()
#define	REC_PQ_STRIDE		2
#define	REC_PQ_X		0, 1
#define	REC_PQ_Y		2, 3
#define	REC_PQ_T		4, 5

#define	REC_PR_DEFINE()	\
	GEN_X_DEFINE_0_3()	\
	GEN_X_DEFINE_4_5()	\
	GEN_X_DEFINE_16()	\
	GEN_X_DEFINE_17()	\
	GEN_X_DEFINE_31()	\
	GEN_X_DEFINE_32()	\
	GEN_X_DEFINE_33_36()
#define	REC_PR_STRIDE		2
#define	REC_PR_X		0, 1
#define	REC_PR_Y		2, 3
#define	REC_PR_T		4, 5

#define	REC_QR_DEFINE()	\
	GEN_X_DEFINE_0_3()	\
	GEN_X_DEFINE_4_5()	\
	GEN_X_DEFINE_16()	\
	GEN_X_DEFINE_17()	\
	GEN_X_DEFINE_31()	\
	GEN_X_DEFINE_32()	\
	GEN_X_DEFINE_33_36()
#define	REC_QR_STRIDE		2
#define	REC_QR_X		0, 1
#define	REC_QR_Y		2, 3
#define	REC_QR_T		4, 5

#define	REC_PQR_DEFINE() \
	GEN_X_DEFINE_0_3()	\
	GEN_X_DEFINE_4_5()	\
	GEN_X_DEFINE_6_7()	\
	GEN_X_DEFINE_8_9()	\
	GEN_X_DEFINE_16()	\
	GEN_X_DEFINE_17()	\
	GEN_X_DEFINE_31()	\
	GEN_X_DEFINE_32()	\
	GEN_X_DEFINE_33_36()
#define	REC_PQR_STRIDE		2
#define	REC_PQR_X		0, 1
#define	REC_PQR_Y		2, 3
#define	REC_PQR_Z		4, 5
#define	REC_PQR_XS		6, 7
#define	REC_PQR_YS		8, 9


#include <sys/vdev_raidz_impl.h>
#include "vdev_raidz_math_impl.h"

DEFINE_GEN_METHODS(aarch64_neon);
DEFINE_REC_METHODS(aarch64_neon);

static boolean_t
raidz_will_aarch64_neon_work(void)
{
	return (kfpu_allowed());
}

const raidz_impl_ops_t vdev_raidz_aarch64_neon_impl = {
	.init = gf_mul_nibble_lt_init,
	.fini = NULL,
	.gen = RAIDZ_GEN_METHODS(aarch64_neon),
	.rec = RAIDZ_REC_METHODS(aarch64_neon),
	.is_supported = &raidz_will_aarch64_neon_work,
	.name = "aarch64_neon"
};

#endif /* defined(__aarch64__) */


#if defined(__aarch64__)
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
#endif /* defined(__aarch64__) */
