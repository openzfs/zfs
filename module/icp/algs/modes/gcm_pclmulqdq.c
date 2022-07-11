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

#if defined(__x86_64) && defined(HAVE_PCLMULQDQ)

#include <sys/types.h>
#include <sys/simd.h>

/* These functions are used to execute pclmulqdq based assembly methods */
extern void gcm_mul_pclmulqdq(uint64_t *, uint64_t *, uint64_t *);

#include <modes/gcm_impl.h>

/*
 * Perform a carry-less multiplication (that is, use XOR instead of the
 * multiply operator) on *x_in and *y and place the result in *res.
 *
 * Byte swap the input (*x_in and *y) and the output (*res).
 *
 * Note: x_in, y, and res all point to 16-byte numbers (an array of two
 * 64-bit integers).
 */
static void
gcm_pclmulqdq_mul(uint64_t *x_in, uint64_t *y, uint64_t *res)
{
	kfpu_begin();
	gcm_mul_pclmulqdq(x_in, y, res);
	kfpu_end();
}

static boolean_t
gcm_pclmulqdq_will_work(void)
{
	return (kfpu_allowed() && zfs_pclmulqdq_available());
}

const gcm_impl_ops_t gcm_pclmulqdq_impl = {
	.mul = &gcm_pclmulqdq_mul,
	.is_supported = &gcm_pclmulqdq_will_work,
	.name = "pclmulqdq"
};

#endif /* defined(__x86_64) && defined(HAVE_PCLMULQDQ) */
