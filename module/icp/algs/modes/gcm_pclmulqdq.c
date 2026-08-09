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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#if defined(__x86_64) && HAVE_SIMD(PCLMULQDQ)

#include <sys/types.h>
#include <sys/simd.h>
#include <sys/asm_linkage.h>

/* These functions are used to execute pclmulqdq based assembly methods */
extern void ASMABI gcm_mul_pclmulqdq(uint64_t *, uint64_t *, uint64_t *);

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

#endif /* defined(__x86_64) && HAVE_SIMD(PCLMULQDQ) */
