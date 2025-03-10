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
 * Based on BLAKE3 v1.3.1, https://github.com/BLAKE3-team/BLAKE3
 * Copyright (c) 2019-2020 Samuel Neves and Jack O'Connor
 * Copyright (c) 2021-2022 Tino Reichardt <milky-zfs@mcmilk.de>
 */

#ifndef	BLAKE3_IMPL_H
#define	BLAKE3_IMPL_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/blake3.h>
#include <sys/simd.h>
#include <sys/asm_linkage.h>

/*
 * Methods used to define BLAKE3 assembler implementations
 */
typedef void (*blake3_compress_in_place_f)(uint32_t cv[8],
    const uint8_t block[BLAKE3_BLOCK_LEN],
    uint8_t block_len, uint64_t counter,
    uint8_t flags);

typedef void (*blake3_compress_xof_f)(const uint32_t cv[8],
    const uint8_t block[BLAKE3_BLOCK_LEN], uint8_t block_len,
    uint64_t counter, uint8_t flags, uint8_t out[64]);

typedef void (*blake3_hash_many_f)(const uint8_t * const *inputs,
    size_t num_inputs, size_t blocks, const uint32_t key[8],
    uint64_t counter, boolean_t increment_counter, uint8_t flags,
    uint8_t flags_start, uint8_t flags_end, uint8_t *out);

typedef boolean_t (*blake3_is_supported_f)(void);

typedef struct {
	blake3_compress_in_place_f compress_in_place;
	blake3_compress_xof_f compress_xof;
	blake3_hash_many_f hash_many;
	blake3_is_supported_f is_supported;
	int degree;
	const char *name;
} blake3_ops_t;

/* return selected BLAKE3 implementation ops */
extern const blake3_ops_t *blake3_get_ops(void);

#if defined(__x86_64)
#define	MAX_SIMD_DEGREE 16
#else
#define	MAX_SIMD_DEGREE 4
#endif

#define	MAX_SIMD_DEGREE_OR_2	(MAX_SIMD_DEGREE > 2 ? MAX_SIMD_DEGREE : 2)

static const uint32_t BLAKE3_IV[8] = {
	0x6A09E667UL, 0xBB67AE85UL, 0x3C6EF372UL, 0xA54FF53AUL,
	0x510E527FUL, 0x9B05688CUL, 0x1F83D9ABUL, 0x5BE0CD19UL};

static const uint8_t BLAKE3_MSG_SCHEDULE[7][16] = {
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
	{2, 6, 3, 10, 7, 0, 4, 13, 1, 11, 12, 5, 9, 14, 15, 8},
	{3, 4, 10, 12, 13, 2, 7, 14, 6, 5, 9, 0, 11, 15, 8, 1},
	{10, 7, 12, 9, 14, 3, 13, 15, 4, 0, 11, 2, 5, 8, 1, 6},
	{12, 13, 9, 11, 15, 10, 14, 8, 7, 2, 5, 3, 0, 1, 6, 4},
	{9, 14, 11, 5, 8, 12, 15, 1, 13, 3, 0, 10, 2, 6, 4, 7},
	{11, 15, 5, 0, 1, 9, 8, 6, 14, 10, 2, 12, 3, 4, 7, 13},
};

/* Find index of the highest set bit */
static inline unsigned int highest_one(uint64_t x) {
#if defined(__GNUC__) || defined(__clang__)
	return (63 ^ __builtin_clzll(x));
#elif defined(_MSC_VER) && defined(IS_X86_64)
	unsigned long index;
	_BitScanReverse64(&index, x);
	return (index);
#elif defined(_MSC_VER) && defined(IS_X86_32)
	if (x >> 32) {
		unsigned long index;
		_BitScanReverse(&index, x >> 32);
		return (32 + index);
	} else {
		unsigned long index;
		_BitScanReverse(&index, x);
		return (index);
	}
#else
	unsigned int c = 0;
	if (x & 0xffffffff00000000ULL) { x >>= 32; c += 32; }
	if (x & 0x00000000ffff0000ULL) { x >>= 16; c += 16; }
	if (x & 0x000000000000ff00ULL) { x >>=  8; c +=  8; }
	if (x & 0x00000000000000f0ULL) { x >>=  4; c +=  4; }
	if (x & 0x000000000000000cULL) { x >>=  2; c +=  2; }
	if (x & 0x0000000000000002ULL) { c +=  1; }
	return (c);
#endif
}

/* Count the number of 1 bits. */
static inline unsigned int popcnt(uint64_t x) {
	unsigned int count = 0;

	while (x != 0) {
		count += 1;
		x &= x - 1;
	}

	return (count);
}

/*
 * Largest power of two less than or equal to x.
 * As a special case, returns 1 when x is 0.
 */
static inline uint64_t round_down_to_power_of_2(uint64_t x) {
	return (1ULL << highest_one(x | 1));
}

static inline uint32_t counter_low(uint64_t counter) {
	return ((uint32_t)counter);
}

static inline uint32_t counter_high(uint64_t counter) {
	return ((uint32_t)(counter >> 32));
}

static inline uint32_t load32(const void *src) {
	const uint8_t *p = (const uint8_t *)src;
	return ((uint32_t)(p[0]) << 0) | ((uint32_t)(p[1]) << 8) |
	    ((uint32_t)(p[2]) << 16) | ((uint32_t)(p[3]) << 24);
}

static inline void load_key_words(const uint8_t key[BLAKE3_KEY_LEN],
    uint32_t key_words[8]) {
	key_words[0] = load32(&key[0 * 4]);
	key_words[1] = load32(&key[1 * 4]);
	key_words[2] = load32(&key[2 * 4]);
	key_words[3] = load32(&key[3 * 4]);
	key_words[4] = load32(&key[4 * 4]);
	key_words[5] = load32(&key[5 * 4]);
	key_words[6] = load32(&key[6 * 4]);
	key_words[7] = load32(&key[7 * 4]);
}

static inline void store32(void *dst, uint32_t w) {
	uint8_t *p = (uint8_t *)dst;
	p[0] = (uint8_t)(w >> 0);
	p[1] = (uint8_t)(w >> 8);
	p[2] = (uint8_t)(w >> 16);
	p[3] = (uint8_t)(w >> 24);
}

static inline void store_cv_words(uint8_t bytes_out[32], uint32_t cv_words[8]) {
	store32(&bytes_out[0 * 4], cv_words[0]);
	store32(&bytes_out[1 * 4], cv_words[1]);
	store32(&bytes_out[2 * 4], cv_words[2]);
	store32(&bytes_out[3 * 4], cv_words[3]);
	store32(&bytes_out[4 * 4], cv_words[4]);
	store32(&bytes_out[5 * 4], cv_words[5]);
	store32(&bytes_out[6 * 4], cv_words[6]);
	store32(&bytes_out[7 * 4], cv_words[7]);
}

#ifdef	__cplusplus
}
#endif

#endif				/* BLAKE3_IMPL_H */
