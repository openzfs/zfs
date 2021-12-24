
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Based on BLAKE3 v1.2.0, https://github.com/BLAKE3-team/BLAKE3
 * Copyright (c) 2019-2020 Samuel Neves and Jack O'Connor
 * Copyright (c) 2021 Tino Reichardt <milky-zfs@mcmilk.de>
 */

#if defined(__aarch64__)

#include <sys/types.h>
#include <sys/strings.h>
#include <sys/simd.h>

#include "blake3_impl.h"

static void blake3_compress_in_place_generic(uint32_t cv[8],
    const uint8_t block[BLAKE3_BLOCK_LEN], uint8_t block_len,
    uint64_t counter, uint8_t flags)
{
	const blake3_impl_ops_t *ops = blake3_impl_get_ops();
	ops->compress_in_place(cv, block, block_len, counter, flags);
}

static void blake3_compress_xof_generic(const uint32_t cv[8],
    const uint8_t block[BLAKE3_BLOCK_LEN], uint8_t block_len,
    uint64_t counter, uint8_t flags, uint8_t out[64])
{
	const blake3_impl_ops_t *ops = blake3_impl_get_ops();
	ops->compress_xof(cv, block, block_len, counter, flags, out);
}


static inline void hash_one_neon(const uint8_t *input, size_t blocks,
    const uint32_t key[8], uint64_t counter, uint8_t flags,
    uint8_t flags_start, uint8_t flags_end, uint8_t out[BLAKE3_OUT_LEN]) {
	uint32_t cv[8];
	memcpy(cv, key, BLAKE3_KEY_LEN);
	uint8_t block_flags = flags | flags_start;
	while (blocks > 0) {
		if (blocks == 1) {
			block_flags |= flags_end;
		}
		blake3_compress_in_place_generic(cv, input, BLAKE3_BLOCK_LEN,
		    counter, block_flags);
		input = &input[BLAKE3_BLOCK_LEN];
		blocks -= 1;
		block_flags = flags;
	}
	memcpy(out, cv, BLAKE3_OUT_LEN);
}


extern void _blake3_hash_many_neon(const uint8_t *const *inputs,
    size_t num_inputs, size_t blocks, const uint32_t key[8], uint64_t counter,
    boolean_t increment_counter, uint8_t flags, uint8_t flags_start, uint8_t
    flags_end, uint8_t *out);

static void blake3_hash_many_neon(const uint8_t *const *inputs,
    size_t num_inputs, size_t blocks, const uint32_t key[8], uint64_t counter,
    boolean_t increment_counter, uint8_t flags, uint8_t flags_start, uint8_t
    flags_end, uint8_t *out) {
	kfpu_begin();
	_blake3_hash_many_neon(inputs, num_inputs, blocks, key, counter,
	increment_counter, flags, flags_start, flags_end, out);
	kfpu_end();
}

static boolean_t blake3_is_neon_supported(void)
{
		/* NEON isn't optional in AArch64 */
		return (kfpu_allowed());
}

const blake3_impl_ops_t blake3_neon_impl = {
	.compress_in_place = blake3_compress_in_place_generic,
	.compress_xof = blake3_compress_xof_generic,
	.hash_many = blake3_hash_many_neon,
	.is_supported = blake3_is_neon_supported,
	.degree = 4,
	.name = "neon"
};

#endif		/* defined(__aarch64__) */
