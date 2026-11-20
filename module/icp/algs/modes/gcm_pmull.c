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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#if defined(__aarch64__) && HAVE_SIMD(ARMV8_PMULL)

#include <sys/byteorder.h>
#include <sys/simd.h>
#include <sys/types.h>
#include <sys/asm_linkage.h>

/*
 * This function is used to execute the ARMv8 PMULL based assembly
 * method, see asm-aarch64/modes/ghashv8-armx.S.
 */
extern void ASMABI gcm_gmult_v8(uint64_t Xi[2], const uint64_t *Htable);

#include <modes/gcm_impl.h>

/*
 * gcm_gmult_v8() reads two 128-bit Htable entries: the "twisted" hash
 * key H and the Karatsuba pre-processed mid-term (H.lo ^ H.hi).
 */
#define	GCM_V8_HTABLE_SIZE	(2 * 2)

/*
 * Perform a carry-less multiplication (that is, use XOR instead of the
 * multiply operator) on *x_in and *y and place the result in *res.
 *
 * Byte swap the input (*x_in and *y) and the output (*res).
 *
 * Note: x_in, y, and res all point to 16-byte numbers (an array of two
 * 64-bit integers).
 *
 * The mul operation of gcm_impl_ops_t is stateless, while the OpenSSL
 * assembly works on a table of pre-computed powers of the hash key H.
 * Of that table, gcm_gmult_v8() only reads the two entries derived in
 * the prologue of OpenSSL's gcm_init_v8(): H shifted left by one bit
 * modulo the GHASH polynomial ("twisted H") and the xor of its halves.
 * Both are cheap scalar bit operations, so compute them here on every
 * call instead of deriving the full table of powers of H.
 */
static void
gcm_pmull_mul(uint64_t *x_in, uint64_t *y, uint64_t *res)
{
	uint64_t Htable[GCM_V8_HTABLE_SIZE];
	uint64_t hi = ntohll(y[0]);
	uint64_t lo = ntohll(y[1]);
	uint64_t carry = hi >> 63;

	Htable[0] = (lo << 1) ^ carry;
	Htable[1] = ((hi << 1) | (lo >> 63)) ^
	    ((0ULL - carry) & 0xc200000000000000ULL);
	Htable[2] = Htable[0] ^ Htable[1];
	Htable[3] = 0;

	res[0] = x_in[0];
	res[1] = x_in[1];

	kfpu_begin();
	gcm_gmult_v8(res, Htable);
	kfpu_end();
}

static boolean_t
gcm_pmull_will_work(void)
{
	return (kfpu_allowed() && zfs_pmull_available());
}

const gcm_impl_ops_t gcm_pmull_impl = {
	.mul = &gcm_pmull_mul,
	.is_supported = &gcm_pmull_will_work,
	.name = "pmull"
};

#endif /* defined(__aarch64__) && HAVE_SIMD(ARMV8_PMULL) */
