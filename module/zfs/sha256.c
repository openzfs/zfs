/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/zfs_context.h>
#include <sys/zio.h>
#include <sys/zio_checksum.h>
#include <sys/sha256.h>

/*
 * SHA-256 checksum, as specified in FIPS 180-3, available at:
 * http://csrc.nist.gov/publications/PubsFIPS.html
 *
 * This is a very compact implementation of SHA-256.
 * It is designed to be simple and portable, not to be fast.
 */

/*
 * The literal definitions of Ch() and Maj() according to FIPS 180-3 are:
 *
 * 	Ch(x, y, z)     (x & y) ^ (~x & z)
 * 	Maj(x, y, z)    (x & y) ^ (x & z) ^ (y & z)
 *
 * We use equivalent logical reductions here that require one less op.
 */
#define	Ch(x, y, z)	((z) ^ ((x) & ((y) ^ (z))))
#define	Maj(x, y, z)	(((x) & (y)) ^ ((z) & ((x) ^ (y))))
#define	Rot32(x, s)	(((x) >> s) | ((x) << (32 - s)))
#define	SIGMA0(x)	(Rot32(x, 2) ^ Rot32(x, 13) ^ Rot32(x, 22))
#define	SIGMA1(x)	(Rot32(x, 6) ^ Rot32(x, 11) ^ Rot32(x, 25))
#define	sigma0(x)	(Rot32(x, 7) ^ Rot32(x, 18) ^ ((x) >> 3))
#define	sigma1(x)	(Rot32(x, 17) ^ Rot32(x, 19) ^ ((x) >> 10))

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

static void
sha256_transform_block(uint32_t *H, const uint8_t *cp)
{
	uint32_t a, b, c, d, e, f, g, h, t, T1, T2, W[64];

	for (t = 0; t < 16; t++, cp += 4)
		W[t] = (cp[0] << 24) | (cp[1] << 16) | (cp[2] << 8) | cp[3];

	for (t = 16; t < 64; t++)
		W[t] = sigma1(W[t - 2]) + W[t - 7] +
		    sigma0(W[t - 15]) + W[t - 16];

	a = H[0]; b = H[1]; c = H[2]; d = H[3];
	e = H[4]; f = H[5]; g = H[6]; h = H[7];

	for (t = 0; t < 64; t++) {
		T1 = h + SIGMA1(e) + Ch(e, f, g) + SHA256_K[t] + W[t];
		T2 = SIGMA0(a) + Maj(a, b, c);
		h = g; g = f; f = e; e = d + T1;
		d = c; c = b; b = a; a = T1 + T2;
	}

	H[0] += a; H[1] += b; H[2] += c; H[3] += d;
	H[4] += e; H[5] += f; H[6] += g; H[7] += h;
}

void
sha256_transform_generic(const void *buf, uint32_t *H, uint64_t blks)
{
	int i;
	for (i = 0; i < blks; i++)
		sha256_transform_block(H, buf + (i << SHA256_SHIFT));
}

void
zio_checksum_SHA256(const void *buf, uint64_t size, zio_cksum_t *zcp)
{
	uint32_t H[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
	    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
	uint8_t pad[128];
	int i, padsize;

	sha256_transform(buf, H, size >> SHA256_SHIFT);

	for (padsize = 0, i = size & ~63ULL; i < size; i++)
		pad[padsize++] = *((uint8_t *)buf + i);

	for (pad[padsize++] = 0x80; (padsize & 63) != 56; padsize++)
		pad[padsize] = 0;

	for (i = 56; i >= 0; i -= 8)
		pad[padsize++] = (size << 3) >> i;

	sha256_transform(pad, H, padsize >> SHA256_SHIFT);

	ZIO_SET_CHECKSUM(zcp,
	    (uint64_t)H[0] << 32 | H[1],
	    (uint64_t)H[2] << 32 | H[3],
	    (uint64_t)H[4] << 32 | H[5],
	    (uint64_t)H[6] << 32 | H[7]);
}

void (*sha256_transform)(const void *, uint32_t *, uint64_t);

#ifdef ZFS_DEBUG
#define	SHA256_NR_TEST 4

static const char *test_str[SHA256_NR_TEST] = {
	"abcdefghijklmnopqrstuvwxyz012345678",
	"abcdefghijklmnopqrstuvwxyz0123456789",
	"the quick brown fox jumps over the lazy dog",
	"The quick brown fox jumps over the lazy dog",
};

static const zio_cksum_t test_zc[SHA256_NR_TEST] = {
	{{ 0xab297465f02553de, 0xf36470e31b10aa37,
	0x5df65ca60bd6b9cf, 0xd47fdb0020c78e07 }},

	{{ 0x011fc2994e39d251, 0x141540f87a69092b,
	0x3f22a86767f7283d, 0xe7eeedb3897bedf6 }},

	{{ 0x05c6e08f1d9fdafa, 0x03147fcb8f82f124,
	0xc76d2f70e3d989dc, 0x8aadb5e7d7450bec }},

	{{ 0xd7a8fbb307d78094, 0x69ca9abcb0082e4f,
	0x8d5651e46d3cdb76, 0x2d02d0bf37c9e592 }},
};

static void sha256_test(void)
{
	int i;
	zio_cksum_t zc;

	for (i = 0; i < SHA256_NR_TEST; i++) {
		zio_checksum_SHA256(test_str[i], strlen(test_str[i]), &zc);
		VERIFY0(memcmp(&zc, &test_zc[i], sizeof (zio_cksum_t)));
	}
}
#endif /* ZFS_DEBUG */

void
zio_checksum_SHA256_init(void)
{
	sha256_transform = sha256_transform_generic;
	arch_sha256_init();
#ifdef ZFS_DEBUG
	sha256_test();
#endif
}
