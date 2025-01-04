// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://opensource.org/licenses/CDDL-1.0.
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
 * Based on Edon-R implementation for SUPERCOP, based on NIST API.
 * Copyright (c) 2009, 2010, Jørn Amundsen <jorn.amundsen@ntnu.no>
 * Copyright (c) 2013 Saso Kiselkov, All rights reserved
 * Copyright (c) 2023 Tino Reichardt <milky-zfs@mcmilk.de>
 */

#include <sys/zfs_context.h>
#include <sys/string.h>
#include <sys/edonr.h>

/*
 * We need 1196 byte stack for Q512() on i386
 * - we define this pragma to make gcc happy
 */
#if defined(__GNUC__) && defined(_ILP32)
#pragma GCC diagnostic ignored "-Wframe-larger-than="
#endif

/*
 * Insert compiler memory barriers to reduce stack frame size.
 */
#define	MEMORY_BARRIER   asm volatile("" ::: "memory");

#if defined(_ZFS_BIG_ENDIAN)
#define	ld_swap64(s, d) (d = __builtin_bswap64(*(s)))
#define	st_swap64(s, d) (*(d) = __builtin_bswap64(s))
#else
#define	ld_swap64(s, d) (d = *(s))
#define	st_swap64(s, d) (*(d) = s)
#endif

#define	hashState512(x)	((x)->pipe->p512)

/* rotate shortcuts */
#define	rotl64(x, n)	(((x) << (n)) | ((x) >> (64 - (n))))

/* EdonR512 initial double chaining pipe */
static const uint64_t i512p2[16] = {
	0x8081828384858687ull, 0x88898a8b8c8d8e8full,
	0x9091929394959697ull, 0x98999a9b9c9d9e9full,
	0xa0a1a2a3a4a5a6a7ull, 0xa8a9aaabacadaeafull,
	0xb0b1b2b3b4b5b6b7ull, 0xb8b9babbbcbdbebfull,
	0xc0c1c2c3c4c5c6c7ull, 0xc8c9cacbcccdcecfull,
	0xd0d1d2d3d4d5d6d7ull, 0xd8d9dadbdcdddedfull,
	0xe0e1e2e3e4e5e6e7ull, 0xe8e9eaebecedeeefull,
	0xf0f1f2f3f4f5f6f7ull, 0xf8f9fafbfcfdfeffull
};

#define	LS1_512(x0, x1, x2, x3, x4, x5, x6, x7)		\
{							\
	MEMORY_BARRIER					\
	z1 = x0 + x4, z2 = x1 + x7; z5 = z1 + z2;	\
	s0 = 0xaaaaaaaaaaaaaaaaull + z5 + x2;		\
	s1 = rotl64(z5 + x3, 5);			\
	s2 = rotl64(z5 + x6, 15); z3 = x2 + x3;		\
	s5 = rotl64(z1 + z3 + x5, 40); z4 = x5 + x6;	\
	s6 = rotl64(z2 + z4 + x0, 50); z6 = z3 + z4;	\
	s3 = rotl64(z6 + x7, 22);			\
	s4 = rotl64(z6 + x1, 31);			\
	s7 = rotl64(z6 + x4, 59);			\
}

#define	LS2_512(y0, y1, y2, y3, y4, y5, y6, y7)		\
{							\
	z1 = y0 + y1, z2 = y2 + y5; z6 = z1 + z2;	\
	t0  = ~0xaaaaaaaaaaaaaaaaull + z6 + y7;		\
	t2 = rotl64(z6 + y3, 19);			\
	z3 = y3 + y4, z5 = z1 + z3;			\
	t1 = rotl64(z5 + y6, 10);			\
	t4 = rotl64(z5 + y5, 36);			\
	z4 = y6 + y7, z8 = z3 + z4;			\
	t3 = rotl64(z8 + y2, 29);			\
	t7 = rotl64(z8 + y0, 55); z7 = z2 + z4;		\
	t5 = rotl64(z7 + y4, 44);			\
	t6 = rotl64(z7 + y1, 48);			\
}

#define	QEF_512(r0, r1, r2, r3, r4, r5, r6, r7)		\
{							\
	z1 = s0 ^ s4, z5 = t0 ^ t1;			\
	r0 = (z1 ^ s1) + (z5 ^ t5); z8 = t6 ^ t7;	\
	r1 = (z1 ^ s7) + (t2 ^ z8); z3 = s2 ^ s3;	\
	r7 = (z3 ^ s5) + (t4 ^ z8); z7 = t3 ^ t4;	\
	r3 = (z3 ^ s4) + (t0 ^ z7); z4 = s5 ^ s6;	\
	r5 = (s3 ^ z4) + (z7 ^ t6); z6 = t2 ^ t5;	\
	r6 = (s2 ^ z4) + (z6 ^ t7); z2 = s1 ^ s7;	\
	r4 = (s0 ^ z2) + (t1 ^ z6);			\
	r2 = (z2 ^ s6) + (z5 ^ t3);			\
}

static inline size_t
Q512(size_t bitlen, const uint64_t *data, uint64_t *p)
{
	size_t bl;

	for (bl = bitlen; bl >= EdonR512_BLOCK_BITSIZE;
	    bl -= EdonR512_BLOCK_BITSIZE, data += 16) {
		uint64_t q0, q1, q2, q3, q4, q5, q6, q7;
		uint64_t p0, p1, p2, p3, p4, p5, p6, p7;
		uint64_t s0, s1, s2, s3, s4, s5, s6, s7;
		uint64_t t0, t1, t2, t3, t4, t5, t6, t7;
		uint64_t z1, z2, z3, z4, z5, z6, z7, z8;

#if defined(_ZFS_BIG_ENDIAN)
		uint64_t swp0, swp1, swp2, swp3, swp4, swp5, swp6, swp7,
		    swp8, swp9, swp10, swp11, swp12, swp13, swp14, swp15;
#define	d(j)	swp##j
#define	s64(j)	ld_swap64((uint64_t *)data+j, swp##j)
		s64(0);
		s64(1);
		s64(2);
		s64(3);
		s64(4);
		s64(5);
		s64(6);
		s64(7);
		s64(8);
		s64(9);
		s64(10);
		s64(11);
		s64(12);
		s64(13);
		s64(14);
		s64(15);
#else
#define	d(j)	data[j]
#endif

		/* First row of quasigroup e-transformations */
		LS1_512(d(15), d(14), d(13), d(12), d(11), d(10), d(9), d(8));
		LS2_512(d(0), d(1), d(2), d(3), d(4), d(5), d(6), d(7));
		QEF_512(p0, p1, p2, p3, p4, p5, p6, p7);

		LS1_512(p0, p1, p2, p3, p4, p5, p6, p7);
		LS2_512(d(8), d(9), d(10), d(11), d(12), d(13), d(14), d(15));
		QEF_512(q0, q1, q2, q3, q4, q5, q6, q7);

		/* Second row of quasigroup e-transformations */
		LS1_512(p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
		LS2_512(p0, p1, p2, p3, p4, p5, p6, p7);
		QEF_512(p0, p1, p2, p3, p4, p5, p6, p7);

		LS1_512(p0, p1, p2, p3, p4, p5, p6, p7);
		LS2_512(q0, q1, q2, q3, q4, q5, q6, q7);
		QEF_512(q0, q1, q2, q3, q4, q5, q6, q7);

		/* Third row of quasigroup e-transformations */
		LS1_512(p0, p1, p2, p3, p4, p5, p6, p7);
		LS2_512(p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
		QEF_512(p0, p1, p2, p3, p4, p5, p6, p7);

		LS1_512(q0, q1, q2, q3, q4, q5, q6, q7);
		LS2_512(p0, p1, p2, p3, p4, p5, p6, p7);
		QEF_512(q0, q1, q2, q3, q4, q5, q6, q7);

		/* Fourth row of quasigroup e-transformations */
		LS1_512(d(7), d(6), d(5), d(4), d(3), d(2), d(1), d(0));
		LS2_512(p0, p1, p2, p3, p4, p5, p6, p7);
		QEF_512(p0, p1, p2, p3, p4, p5, p6, p7);

		LS1_512(p0, p1, p2, p3, p4, p5, p6, p7);
		LS2_512(q0, q1, q2, q3, q4, q5, q6, q7);
		QEF_512(q0, q1, q2, q3, q4, q5, q6, q7);

		/* Edon-R tweak on the original SHA-3 Edon-R submission. */
		p[0] ^= d(8) ^ p0;
		p[1] ^= d(9) ^ p1;
		p[2] ^= d(10) ^ p2;
		p[3] ^= d(11) ^ p3;
		p[4] ^= d(12) ^ p4;
		p[5] ^= d(13) ^ p5;
		p[6] ^= d(14) ^ p6;
		p[7] ^= d(15) ^ p7;
		p[8] ^= d(0) ^ q0;
		p[9] ^= d(1) ^ q1;
		p[10] ^= d(2) ^ q2;
		p[11] ^= d(3) ^ q3;
		p[12] ^= d(4) ^ q4;
		p[13] ^= d(5) ^ q5;
		p[14] ^= d(6) ^ q6;
		p[15] ^= d(7) ^ q7;
	}

#undef s64
#undef d
	return (bitlen - bl);
}

void
EdonRInit(EdonRState *state)
{
	state->bits_processed = 0;
	state->unprocessed_bits = 0;
	memcpy(hashState512(state)->DoublePipe, i512p2, sizeof (i512p2));
}

void
EdonRUpdate(EdonRState *state, const uint8_t *data, size_t databitlen)
{
	uint64_t *data64;
	size_t bits_processed;

	if (state->unprocessed_bits > 0) {
		/* LastBytes = databitlen / 8 */
		int LastBytes = (int)databitlen >> 3;

		ASSERT(state->unprocessed_bits + databitlen <=
		    EdonR512_BLOCK_SIZE * 8);

		memcpy(hashState512(state)->LastPart
		    + (state->unprocessed_bits >> 3), data, LastBytes);
		state->unprocessed_bits += (int)databitlen;
		databitlen = state->unprocessed_bits;
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		data64 = (uint64_t *)hashState512(state)->LastPart;
	} else
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		data64 = (uint64_t *)data;

	bits_processed = Q512(databitlen, data64,
	    hashState512(state)->DoublePipe);
	state->bits_processed += bits_processed;
	databitlen -= bits_processed;
	state->unprocessed_bits = (int)databitlen;
	if (databitlen > 0) {
		/* LastBytes = Ceil(databitlen / 8) */
		int LastBytes = ((~(((-(int)databitlen) >> 3) & 0x03ff)) + 1) \
		    & 0x03ff;

		data64 += bits_processed >> 6;	/* byte size update */
		memmove(hashState512(state)->LastPart, data64, LastBytes);
	}
}

void
EdonRFinal(EdonRState *state, uint8_t *hashval)
{
	uint64_t *data64, num_bits;
	size_t databitlen;
	int LastByte, PadOnePosition;

	num_bits = state->bits_processed + state->unprocessed_bits;
	LastByte = (int)state->unprocessed_bits >> 3;
	PadOnePosition = 7 - (state->unprocessed_bits & 0x07);
	hashState512(state)->LastPart[LastByte] =
	    (hashState512(state)->LastPart[LastByte] \
	    & (0xff << (PadOnePosition + 1))) ^ (0x01 << PadOnePosition);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	data64 = (uint64_t *)hashState512(state)->LastPart;

	if (state->unprocessed_bits < 960) {
		memset((hashState512(state)->LastPart) +
		    LastByte + 1, 0x00, EdonR512_BLOCK_SIZE - LastByte - 9);
		databitlen = EdonR512_BLOCK_SIZE * 8;
#if defined(_ZFS_BIG_ENDIAN)
		st_swap64(num_bits, data64 + 15);
#else
		data64[15] = num_bits;
#endif
	} else {
		memset((hashState512(state)->LastPart) + LastByte + 1,
		    0x00, EdonR512_BLOCK_SIZE * 2 - LastByte - 9);
		databitlen = EdonR512_BLOCK_SIZE * 16;
#if defined(_ZFS_BIG_ENDIAN)
		st_swap64(num_bits, data64 + 31);
#else
		data64[31] = num_bits;
#endif
	}

	state->bits_processed += Q512(databitlen, data64,
	    hashState512(state)->DoublePipe);

#if defined(_ZFS_BIG_ENDIAN)
	data64 = (uint64_t *)hashval;
	uint64_t *s64 = hashState512(state)->DoublePipe + 8;
	int j;

	for (j = 0; j < EdonR512_DIGEST_SIZE >> 3; j++)
		st_swap64(s64[j], data64 + j);
#else
	memcpy(hashval, hashState512(state)->DoublePipe + 8,
	    EdonR512_DIGEST_SIZE);
#endif
}

void
EdonRHash(const uint8_t *data, size_t databitlen, uint8_t *hashval)
{
	EdonRState state;

	EdonRInit(&state);
	EdonRUpdate(&state, data, databitlen);
	EdonRFinal(&state, hashval);
}

#ifdef _KERNEL
EXPORT_SYMBOL(EdonRInit);
EXPORT_SYMBOL(EdonRUpdate);
EXPORT_SYMBOL(EdonRHash);
EXPORT_SYMBOL(EdonRFinal);
#endif
