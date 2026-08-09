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
 * Based on BLAKE3 v1.3.1, https://github.com/BLAKE3-team/BLAKE3
 * Copyright (c) 2019-2020 Samuel Neves and Jack O'Connor
 * Copyright (c) 2021-2022 Tino Reichardt <milky-zfs@mcmilk.de>
 */

#include <sys/simd.h>
#include <sys/zfs_context.h>
#include "blake3_impl.h"

#define	rotr32(x, n)	(((x) >> (n)) | ((x) << (32 - (n))))
static inline void g(uint32_t *state, size_t a, size_t b, size_t c, size_t d,
    uint32_t x, uint32_t y)
{
	state[a] = state[a] + state[b] + x;
	state[d] = rotr32(state[d] ^ state[a], 16);
	state[c] = state[c] + state[d];
	state[b] = rotr32(state[b] ^ state[c], 12);
	state[a] = state[a] + state[b] + y;
	state[d] = rotr32(state[d] ^ state[a], 8);
	state[c] = state[c] + state[d];
	state[b] = rotr32(state[b] ^ state[c], 7);
}

static inline void round_fn(uint32_t state[16], const uint32_t *msg,
    size_t round)
{
	/* Select the message schedule based on the round. */
	const uint8_t *schedule = BLAKE3_MSG_SCHEDULE[round];

	/* Mix the columns. */
	g(state, 0, 4, 8, 12, msg[schedule[0]], msg[schedule[1]]);
	g(state, 1, 5, 9, 13, msg[schedule[2]], msg[schedule[3]]);
	g(state, 2, 6, 10, 14, msg[schedule[4]], msg[schedule[5]]);
	g(state, 3, 7, 11, 15, msg[schedule[6]], msg[schedule[7]]);

	/* Mix the rows. */
	g(state, 0, 5, 10, 15, msg[schedule[8]], msg[schedule[9]]);
	g(state, 1, 6, 11, 12, msg[schedule[10]], msg[schedule[11]]);
	g(state, 2, 7, 8, 13, msg[schedule[12]], msg[schedule[13]]);
	g(state, 3, 4, 9, 14, msg[schedule[14]], msg[schedule[15]]);
}

static inline void compress_pre(uint32_t state[16], const uint32_t cv[8],
    const uint8_t block[BLAKE3_BLOCK_LEN], uint8_t block_len,
    uint64_t counter, uint8_t flags)
{
	uint32_t block_words[16];
	block_words[0] = load32(block + 4 * 0);
	block_words[1] = load32(block + 4 * 1);
	block_words[2] = load32(block + 4 * 2);
	block_words[3] = load32(block + 4 * 3);
	block_words[4] = load32(block + 4 * 4);
	block_words[5] = load32(block + 4 * 5);
	block_words[6] = load32(block + 4 * 6);
	block_words[7] = load32(block + 4 * 7);
	block_words[8] = load32(block + 4 * 8);
	block_words[9] = load32(block + 4 * 9);
	block_words[10] = load32(block + 4 * 10);
	block_words[11] = load32(block + 4 * 11);
	block_words[12] = load32(block + 4 * 12);
	block_words[13] = load32(block + 4 * 13);
	block_words[14] = load32(block + 4 * 14);
	block_words[15] = load32(block + 4 * 15);

	state[0] = cv[0];
	state[1] = cv[1];
	state[2] = cv[2];
	state[3] = cv[3];
	state[4] = cv[4];
	state[5] = cv[5];
	state[6] = cv[6];
	state[7] = cv[7];
	state[8] = BLAKE3_IV[0];
	state[9] = BLAKE3_IV[1];
	state[10] = BLAKE3_IV[2];
	state[11] = BLAKE3_IV[3];
	state[12] = counter_low(counter);
	state[13] = counter_high(counter);
	state[14] = (uint32_t)block_len;
	state[15] = (uint32_t)flags;

	round_fn(state, &block_words[0], 0);
	round_fn(state, &block_words[0], 1);
	round_fn(state, &block_words[0], 2);
	round_fn(state, &block_words[0], 3);
	round_fn(state, &block_words[0], 4);
	round_fn(state, &block_words[0], 5);
	round_fn(state, &block_words[0], 6);
}

static inline void blake3_compress_in_place_generic(uint32_t cv[8],
    const uint8_t block[BLAKE3_BLOCK_LEN], uint8_t block_len,
    uint64_t counter, uint8_t flags)
{
	uint32_t state[16];
	compress_pre(state, cv, block, block_len, counter, flags);
	cv[0] = state[0] ^ state[8];
	cv[1] = state[1] ^ state[9];
	cv[2] = state[2] ^ state[10];
	cv[3] = state[3] ^ state[11];
	cv[4] = state[4] ^ state[12];
	cv[5] = state[5] ^ state[13];
	cv[6] = state[6] ^ state[14];
	cv[7] = state[7] ^ state[15];
}

static inline void hash_one_generic(const uint8_t *input, size_t blocks,
    const uint32_t key[8], uint64_t counter, uint8_t flags,
    uint8_t flags_start, uint8_t flags_end, uint8_t out[BLAKE3_OUT_LEN])
{
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
	store_cv_words(out, cv);
}

static inline void blake3_compress_xof_generic(const uint32_t cv[8],
    const uint8_t block[BLAKE3_BLOCK_LEN], uint8_t block_len,
    uint64_t counter, uint8_t flags, uint8_t out[64])
{
	uint32_t state[16];
	compress_pre(state, cv, block, block_len, counter, flags);

	store32(&out[0 * 4], state[0] ^ state[8]);
	store32(&out[1 * 4], state[1] ^ state[9]);
	store32(&out[2 * 4], state[2] ^ state[10]);
	store32(&out[3 * 4], state[3] ^ state[11]);
	store32(&out[4 * 4], state[4] ^ state[12]);
	store32(&out[5 * 4], state[5] ^ state[13]);
	store32(&out[6 * 4], state[6] ^ state[14]);
	store32(&out[7 * 4], state[7] ^ state[15]);
	store32(&out[8 * 4], state[8] ^ cv[0]);
	store32(&out[9 * 4], state[9] ^ cv[1]);
	store32(&out[10 * 4], state[10] ^ cv[2]);
	store32(&out[11 * 4], state[11] ^ cv[3]);
	store32(&out[12 * 4], state[12] ^ cv[4]);
	store32(&out[13 * 4], state[13] ^ cv[5]);
	store32(&out[14 * 4], state[14] ^ cv[6]);
	store32(&out[15 * 4], state[15] ^ cv[7]);
}

static inline void blake3_hash_many_generic(const uint8_t * const *inputs,
    size_t num_inputs, size_t blocks, const uint32_t key[8], uint64_t counter,
    boolean_t increment_counter, uint8_t flags, uint8_t flags_start,
    uint8_t flags_end, uint8_t *out)
{
	while (num_inputs > 0) {
		hash_one_generic(inputs[0], blocks, key, counter, flags,
		    flags_start, flags_end, out);
		if (increment_counter) {
			counter += 1;
		}
		inputs += 1;
		num_inputs -= 1;
		out = &out[BLAKE3_OUT_LEN];
	}
}

/* the generic implementation is always okay */
static boolean_t blake3_is_supported(void)
{
	return (B_TRUE);
}

const blake3_ops_t blake3_generic_impl = {
	.compress_in_place = blake3_compress_in_place_generic,
	.compress_xof = blake3_compress_xof_generic,
	.hash_many = blake3_hash_many_generic,
	.is_supported = blake3_is_supported,
	.degree = 4,
	.name = "generic"
};
