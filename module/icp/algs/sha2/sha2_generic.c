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
 * Based on public domain code in cppcrypto 0.10.
 * Copyright (c) 2022 Tino Reichardt <milky-zfs@mcmilk.de>
 */

#include <sys/zfs_context.h>
#include <sys/zfs_impl.h>
#include <sys/sha2.h>

#include <sha2/sha2_impl.h>

/*
 * On i386, gcc brings this for sha512_generic():
 * error: the frame size of 1040 bytes is larger than 1024
 */
#if defined(__GNUC__) && defined(_ILP32)
#pragma GCC diagnostic ignored "-Wframe-larger-than="
#endif

/* SHA256 */
static const uint32_t SHA256_K[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
	0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
	0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
	0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
	0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
	0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
	0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
	0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define	Ch(x, y, z)	((z) ^ ((x) & ((y) ^ (z))))
#define	Maj(x, y, z)	(((y) & (z)) | (((y) | (z)) & (x)))

#define	rotr32(x, n)	(((x) >> n) | ((x) << (32 - n)))
#define	sum0(x)		(rotr32((x),  2) ^ rotr32((x), 13) ^ rotr32((x), 22))
#define	sum1(x)		(rotr32((x),  6) ^ rotr32((x), 11) ^ rotr32((x), 25))
#define	sigma0(x)	(rotr32((x),  7) ^ rotr32((x), 18) ^ ((x) >> 3))
#define	sigma1(x)	(rotr32((x), 17) ^ rotr32((x), 19) ^ ((x) >> 10))

#define	WU(j) (W[j & 15] += sigma1(W[(j + 14) & 15]) \
	+ W[(j + 9) & 15] + sigma0(W[(j + 1) & 15]))

#define	COMPRESS(i, j, K) \
	T1 = h + sum1(e) + Ch(e, f, g) + K[i + j] + (i? WU(j): W[j]); \
	T2 = sum0(a) + Maj(a, b, c); \
	h = g, g = f, f = e, e = d + T1; \
	d = c, c = b, b = a, a = T1 + T2;

static void sha256_generic(uint32_t state[8], const void *data, size_t num_blks)
{
	uint64_t blk;

	for (blk = 0; blk < num_blks; blk++) {
		uint32_t W[16];
		uint32_t a, b, c, d, e, f, g, h;
		uint32_t T1, T2;
		int i;

		for (i = 0; i < 16; i++) {
			W[i] = BE_32( \
			    (((const uint32_t *)(data))[blk * 16 + i]));
		}

		a = state[0];
		b = state[1];
		c = state[2];
		d = state[3];
		e = state[4];
		f = state[5];
		g = state[6];
		h = state[7];

		for (i = 0; i <= 63; i += 16) {
			COMPRESS(i, 0, SHA256_K);
			COMPRESS(i, 1, SHA256_K);
			COMPRESS(i, 2, SHA256_K);
			COMPRESS(i, 3, SHA256_K);
			COMPRESS(i, 4, SHA256_K);
			COMPRESS(i, 5, SHA256_K);
			COMPRESS(i, 6, SHA256_K);
			COMPRESS(i, 7, SHA256_K);
			COMPRESS(i, 8, SHA256_K);
			COMPRESS(i, 9, SHA256_K);
			COMPRESS(i, 10, SHA256_K);
			COMPRESS(i, 11, SHA256_K);
			COMPRESS(i, 12, SHA256_K);
			COMPRESS(i, 13, SHA256_K);
			COMPRESS(i, 14, SHA256_K);
			COMPRESS(i, 15, SHA256_K);
		}

		state[0] += a;
		state[1] += b;
		state[2] += c;
		state[3] += d;
		state[4] += e;
		state[5] += f;
		state[6] += g;
		state[7] += h;
	}
}

#undef sum0
#undef sum1
#undef sigma0
#undef sigma1

#define	rotr64(x, n)	(((x) >> n) | ((x) << (64 - n)))
#define	sum0(x)		(rotr64((x), 28) ^ rotr64((x), 34) ^ rotr64((x), 39))
#define	sum1(x)		(rotr64((x), 14) ^ rotr64((x), 18) ^ rotr64((x), 41))
#define	sigma0(x)	(rotr64((x),  1) ^ rotr64((x),  8) ^ ((x) >> 7))
#define	sigma1(x)	(rotr64((x), 19) ^ rotr64((x), 61) ^ ((x) >> 6))

/* SHA512 */
static const uint64_t SHA512_K[80] = {
	0x428a2f98d728ae22, 0x7137449123ef65cd, 0xb5c0fbcfec4d3b2f,
	0xe9b5dba58189dbbc, 0x3956c25bf348b538, 0x59f111f1b605d019,
	0x923f82a4af194f9b, 0xab1c5ed5da6d8118, 0xd807aa98a3030242,
	0x12835b0145706fbe, 0x243185be4ee4b28c, 0x550c7dc3d5ffb4e2,
	0x72be5d74f27b896f, 0x80deb1fe3b1696b1, 0x9bdc06a725c71235,
	0xc19bf174cf692694, 0xe49b69c19ef14ad2, 0xefbe4786384f25e3,
	0x0fc19dc68b8cd5b5, 0x240ca1cc77ac9c65, 0x2de92c6f592b0275,
	0x4a7484aa6ea6e483, 0x5cb0a9dcbd41fbd4, 0x76f988da831153b5,
	0x983e5152ee66dfab, 0xa831c66d2db43210, 0xb00327c898fb213f,
	0xbf597fc7beef0ee4, 0xc6e00bf33da88fc2, 0xd5a79147930aa725,
	0x06ca6351e003826f, 0x142929670a0e6e70, 0x27b70a8546d22ffc,
	0x2e1b21385c26c926, 0x4d2c6dfc5ac42aed, 0x53380d139d95b3df,
	0x650a73548baf63de, 0x766a0abb3c77b2a8, 0x81c2c92e47edaee6,
	0x92722c851482353b, 0xa2bfe8a14cf10364, 0xa81a664bbc423001,
	0xc24b8b70d0f89791, 0xc76c51a30654be30, 0xd192e819d6ef5218,
	0xd69906245565a910, 0xf40e35855771202a, 0x106aa07032bbd1b8,
	0x19a4c116b8d2d0c8, 0x1e376c085141ab53, 0x2748774cdf8eeb99,
	0x34b0bcb5e19b48a8, 0x391c0cb3c5c95a63, 0x4ed8aa4ae3418acb,
	0x5b9cca4f7763e373, 0x682e6ff3d6b2b8a3, 0x748f82ee5defb2fc,
	0x78a5636f43172f60, 0x84c87814a1f0ab72, 0x8cc702081a6439ec,
	0x90befffa23631e28, 0xa4506cebde82bde9, 0xbef9a3f7b2c67915,
	0xc67178f2e372532b, 0xca273eceea26619c, 0xd186b8c721c0c207,
	0xeada7dd6cde0eb1e, 0xf57d4f7fee6ed178, 0x06f067aa72176fba,
	0x0a637dc5a2c898a6, 0x113f9804bef90dae, 0x1b710b35131c471b,
	0x28db77f523047d84, 0x32caab7b40c72493, 0x3c9ebe0a15c9bebc,
	0x431d67c49c100d4c, 0x4cc5d4becb3e42b6, 0x597f299cfc657e2a,
	0x5fcb6fab3ad6faec, 0x6c44198c4a475817
};

static void sha512_generic(uint64_t state[8], const void *data, size_t num_blks)
{
	uint64_t blk;

	for (blk = 0; blk < num_blks; blk++) {
		uint64_t W[16];
		uint64_t a, b, c, d, e, f, g, h;
		uint64_t T1, T2;
		int i;

		for (i = 0; i < 16; i++) {
			W[i] = BE_64( \
			    (((const uint64_t *)(data))[blk * 16 + i]));
		}

		a = state[0];
		b = state[1];
		c = state[2];
		d = state[3];
		e = state[4];
		f = state[5];
		g = state[6];
		h = state[7];

		for (i = 0; i <= 79; i += 16) {
			COMPRESS(i, 0, SHA512_K);
			COMPRESS(i, 1, SHA512_K);
			COMPRESS(i, 2, SHA512_K);
			COMPRESS(i, 3, SHA512_K);
			COMPRESS(i, 4, SHA512_K);
			COMPRESS(i, 5, SHA512_K);
			COMPRESS(i, 6, SHA512_K);
			COMPRESS(i, 7, SHA512_K);
			COMPRESS(i, 8, SHA512_K);
			COMPRESS(i, 9, SHA512_K);
			COMPRESS(i, 10, SHA512_K);
			COMPRESS(i, 11, SHA512_K);
			COMPRESS(i, 12, SHA512_K);
			COMPRESS(i, 13, SHA512_K);
			COMPRESS(i, 14, SHA512_K);
			COMPRESS(i, 15, SHA512_K);
		}
		state[0] += a;
		state[1] += b;
		state[2] += c;
		state[3] += d;
		state[4] += e;
		state[5] += f;
		state[6] += g;
		state[7] += h;
	}
}

static void sha256_update(sha256_ctx *ctx, const uint8_t *data, size_t len)
{
	uint64_t pos = ctx->count[0];
	uint64_t total = ctx->count[1];
	uint8_t *m = ctx->wbuf;
	const sha256_ops_t *ops = ctx->ops;

	if (pos && pos + len >= 64) {
		memcpy(m + pos, data, 64 - pos);
		ops->transform(ctx->state, m, 1);
		len -= 64 - pos;
		total += (64 - pos) * 8;
		data += 64 - pos;
		pos = 0;
	}

	if (len >= 64) {
		uint32_t blocks = len / 64;
		uint32_t bytes = blocks * 64;
		ops->transform(ctx->state, data, blocks);
		len -= bytes;
		total += (bytes) * 8;
		data += bytes;
	}
	memcpy(m + pos, data, len);

	pos += len;
	total += len * 8;
	ctx->count[0] = pos;
	ctx->count[1] = total;
}

static void sha512_update(sha512_ctx *ctx, const uint8_t *data, size_t len)
{
	uint64_t pos = ctx->count[0];
	uint64_t total = ctx->count[1];
	uint8_t *m = ctx->wbuf;
	const sha512_ops_t *ops = ctx->ops;

	if (pos && pos + len >= 128) {
		memcpy(m + pos, data, 128 - pos);
		ops->transform(ctx->state, m, 1);
		len -= 128 - pos;
		total += (128 - pos) * 8;
		data += 128 - pos;
		pos = 0;
	}

	if (len >= 128) {
		uint64_t blocks = len / 128;
		uint64_t bytes = blocks * 128;
		ops->transform(ctx->state, data, blocks);
		len -= bytes;
		total += (bytes) * 8;
		data += bytes;
	}
	memcpy(m + pos, data, len);

	pos += len;
	total += len * 8;
	ctx->count[0] = pos;
	ctx->count[1] = total;
}

static void sha256_final(sha256_ctx *ctx, uint8_t *result, int bits)
{
	uint64_t mlen, pos = ctx->count[0];
	uint8_t *m = ctx->wbuf;
	uint32_t *R = (uint32_t *)result;
	const sha256_ops_t *ops = ctx->ops;

	m[pos++] = 0x80;
	if (pos > 56) {
		memset(m + pos, 0, 64 - pos);
		ops->transform(ctx->state, m, 1);
		pos = 0;
	}

	memset(m + pos, 0, 64 - pos);
	mlen = BE_64(ctx->count[1]);
	memcpy(m + (64 - 8), &mlen, 64 / 8);
	ops->transform(ctx->state, m, 1);

	switch (bits) {
	case 224: /* 28 - unused currently /TR */
		R[0] = BE_32(ctx->state[0]);
		R[1] = BE_32(ctx->state[1]);
		R[2] = BE_32(ctx->state[2]);
		R[3] = BE_32(ctx->state[3]);
		R[4] = BE_32(ctx->state[4]);
		R[5] = BE_32(ctx->state[5]);
		R[6] = BE_32(ctx->state[6]);
		break;
	case 256: /* 32 */
		R[0] = BE_32(ctx->state[0]);
		R[1] = BE_32(ctx->state[1]);
		R[2] = BE_32(ctx->state[2]);
		R[3] = BE_32(ctx->state[3]);
		R[4] = BE_32(ctx->state[4]);
		R[5] = BE_32(ctx->state[5]);
		R[6] = BE_32(ctx->state[6]);
		R[7] = BE_32(ctx->state[7]);
		break;
	}

	memset(ctx, 0, sizeof (*ctx));
}

static void sha512_final(sha512_ctx *ctx, uint8_t *result, int bits)
{
	uint64_t mlen, pos = ctx->count[0];
	uint8_t *m = ctx->wbuf, *r;
	uint64_t *R = (uint64_t *)result;
	const sha512_ops_t *ops = ctx->ops;

	m[pos++] = 0x80;
	if (pos > 112) {
		memset(m + pos, 0, 128 - pos);
		ops->transform(ctx->state, m, 1);
		pos = 0;
	}

	memset(m + pos, 0, 128 - pos);
	mlen = BE_64(ctx->count[1]);
	memcpy(m + (128 - 8), &mlen, 64 / 8);
	ops->transform(ctx->state, m, 1);

	switch (bits) {
	case 224: /* 28 => 3,5 x 8 */
		r = result + 24;
		R[0] = BE_64(ctx->state[0]);
		R[1] = BE_64(ctx->state[1]);
		R[2] = BE_64(ctx->state[2]);
		/* last 4 bytes are special here */
		*r++ = (uint8_t)(ctx->state[3] >> 56);
		*r++ = (uint8_t)(ctx->state[3] >> 48);
		*r++ = (uint8_t)(ctx->state[3] >> 40);
		*r++ = (uint8_t)(ctx->state[3] >> 32);
		break;
	case 256: /* 32 */
		R[0] = BE_64(ctx->state[0]);
		R[1] = BE_64(ctx->state[1]);
		R[2] = BE_64(ctx->state[2]);
		R[3] = BE_64(ctx->state[3]);
		break;
	case 384: /* 48 */
		R[0] = BE_64(ctx->state[0]);
		R[1] = BE_64(ctx->state[1]);
		R[2] = BE_64(ctx->state[2]);
		R[3] = BE_64(ctx->state[3]);
		R[4] = BE_64(ctx->state[4]);
		R[5] = BE_64(ctx->state[5]);
		break;
	case 512: /* 64 */
		R[0] = BE_64(ctx->state[0]);
		R[1] = BE_64(ctx->state[1]);
		R[2] = BE_64(ctx->state[2]);
		R[3] = BE_64(ctx->state[3]);
		R[4] = BE_64(ctx->state[4]);
		R[5] = BE_64(ctx->state[5]);
		R[6] = BE_64(ctx->state[6]);
		R[7] = BE_64(ctx->state[7]);
		break;
	}

	memset(ctx, 0, sizeof (*ctx));
}

/* SHA2 Init function */
void
SHA2Init(int algotype, SHA2_CTX *ctx)
{
	sha256_ctx *ctx256 = &ctx->sha256;
	sha512_ctx *ctx512 = &ctx->sha512;

	ASSERT3S(algotype, >=, SHA512_HMAC_MECH_INFO_TYPE);
	ASSERT3S(algotype, <=, SHA512_256);

	memset(ctx, 0, sizeof (*ctx));
	ctx->algotype = algotype;
	switch (ctx->algotype) {
		case SHA256:
			ctx256->state[0] = 0x6a09e667;
			ctx256->state[1] = 0xbb67ae85;
			ctx256->state[2] = 0x3c6ef372;
			ctx256->state[3] = 0xa54ff53a;
			ctx256->state[4] = 0x510e527f;
			ctx256->state[5] = 0x9b05688c;
			ctx256->state[6] = 0x1f83d9ab;
			ctx256->state[7] = 0x5be0cd19;
			ctx256->count[0] = 0;
			ctx256->ops = sha256_get_ops();
			break;
		case SHA512:
		case SHA512_HMAC_MECH_INFO_TYPE:
			ctx512->state[0] = 0x6a09e667f3bcc908ULL;
			ctx512->state[1] = 0xbb67ae8584caa73bULL;
			ctx512->state[2] = 0x3c6ef372fe94f82bULL;
			ctx512->state[3] = 0xa54ff53a5f1d36f1ULL;
			ctx512->state[4] = 0x510e527fade682d1ULL;
			ctx512->state[5] = 0x9b05688c2b3e6c1fULL;
			ctx512->state[6] = 0x1f83d9abfb41bd6bULL;
			ctx512->state[7] = 0x5be0cd19137e2179ULL;
			ctx512->count[0] = 0;
			ctx512->count[1] = 0;
			ctx512->ops = sha512_get_ops();
			break;
		case SHA512_256:
			ctx512->state[0] = 0x22312194fc2bf72cULL;
			ctx512->state[1] = 0x9f555fa3c84c64c2ULL;
			ctx512->state[2] = 0x2393b86b6f53b151ULL;
			ctx512->state[3] = 0x963877195940eabdULL;
			ctx512->state[4] = 0x96283ee2a88effe3ULL;
			ctx512->state[5] = 0xbe5e1e2553863992ULL;
			ctx512->state[6] = 0x2b0199fc2c85b8aaULL;
			ctx512->state[7] = 0x0eb72ddc81c52ca2ULL;
			ctx512->count[0] = 0;
			ctx512->count[1] = 0;
			ctx512->ops = sha512_get_ops();
			break;
	}
}

/* SHA2 Update function */
void
SHA2Update(SHA2_CTX *ctx, const void *data, size_t len)
{
	/* check for zero input length */
	if (len == 0)
		return;

	ASSERT3P(data, !=, NULL);

	switch (ctx->algotype) {
		case SHA256:
			sha256_update(&ctx->sha256, data, len);
			break;
		case SHA512:
		case SHA512_HMAC_MECH_INFO_TYPE:
			sha512_update(&ctx->sha512, data, len);
			break;
		case SHA512_256:
			sha512_update(&ctx->sha512, data, len);
			break;
	}
}

/* SHA2Final function */
void
SHA2Final(void *digest, SHA2_CTX *ctx)
{
	switch (ctx->algotype) {
		case SHA256:
			sha256_final(&ctx->sha256, digest, 256);
			break;
		case SHA512:
		case SHA512_HMAC_MECH_INFO_TYPE:
			sha512_final(&ctx->sha512, digest, 512);
			break;
		case SHA512_256:
			sha512_final(&ctx->sha512, digest, 256);
			break;
	}
}

/* the generic implementation is always okay */
static boolean_t sha2_is_supported(void)
{
	return (B_TRUE);
}

const sha256_ops_t sha256_generic_impl = {
	.name = "generic",
	.transform = sha256_generic,
	.is_supported = sha2_is_supported
};

const sha512_ops_t sha512_generic_impl = {
	.name = "generic",
	.transform = sha512_generic,
	.is_supported = sha2_is_supported
};
