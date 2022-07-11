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

#include <modes/gcm_impl.h>

struct aes_block {
	uint64_t a;
	uint64_t b;
};

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
gcm_generic_mul(uint64_t *x_in, uint64_t *y, uint64_t *res)
{
	static const uint64_t R = 0xe100000000000000ULL;
	struct aes_block z = {0, 0};
	struct aes_block v;
	uint64_t x;
	int i, j;

	v.a = ntohll(y[0]);
	v.b = ntohll(y[1]);

	for (j = 0; j < 2; j++) {
		x = ntohll(x_in[j]);
		for (i = 0; i < 64; i++, x <<= 1) {
			if (x & 0x8000000000000000ULL) {
				z.a ^= v.a;
				z.b ^= v.b;
			}
			if (v.b & 1ULL) {
				v.b = (v.a << 63)|(v.b >> 1);
				v.a = (v.a >> 1) ^ R;
			} else {
				v.b = (v.a << 63)|(v.b >> 1);
				v.a = v.a >> 1;
			}
		}
	}
	res[0] = htonll(z.a);
	res[1] = htonll(z.b);
}

static boolean_t
gcm_generic_will_work(void)
{
	return (B_TRUE);
}

const gcm_impl_ops_t gcm_generic_impl = {
	.mul = &gcm_generic_mul,
	.is_supported = &gcm_generic_will_work,
	.name = "generic"
};
