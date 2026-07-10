// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright (c) 2026, Christos Longros.
 */

#include <string.h>

#include <sys/types.h>
#include <sys/sha2.h>
#include <sys/zfs_impl.h>

#include "unit.h"

/* ========== */

#define	DATA_SIZE	8192

static uint8_t databuf[DATA_SIZE] __attribute__((aligned(64)));

static void
fill_data(void)
{
	for (size_t i = 0; i < DATA_SIZE; i++)
		databuf[i] = (uint8_t)i;
}

static void
hash_buf(int algotype, const void *buf, size_t len, uint8_t *digest)
{
	SHA2_CTX ctx;

	SHA2Init(algotype, &ctx);
	SHA2Update(&ctx, buf, len);
	SHA2Final(digest, &ctx);
}

/* ========== */

/*
 * Test messages and digests from:
 * http://csrc.nist.gov/groups/ST/toolkit/documents/Examples/SHA_All.pdf
 */

static const char *test_msg0 = "abc";
static const char *test_msg1 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklm"
	"nlmnomnopnopq";
static const char *test_msg2 = "abcdefghbcdefghicdefghijdefghijkefghijklfgh"
	"ijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";

static const uint8_t	sha256_test_digests[][32] = {
	{
		/* for test_msg0 */
		0xBA, 0x78, 0x16, 0xBF, 0x8F, 0x01, 0xCF, 0xEA,
		0x41, 0x41, 0x40, 0xDE, 0x5D, 0xAE, 0x22, 0x23,
		0xB0, 0x03, 0x61, 0xA3, 0x96, 0x17, 0x7A, 0x9C,
		0xB4, 0x10, 0xFF, 0x61, 0xF2, 0x00, 0x15, 0xAD
	},
	{
		/* for test_msg1 */
		0x24, 0x8D, 0x6A, 0x61, 0xD2, 0x06, 0x38, 0xB8,
		0xE5, 0xC0, 0x26, 0x93, 0x0C, 0x3E, 0x60, 0x39,
		0xA3, 0x3C, 0xE4, 0x59, 0x64, 0xFF, 0x21, 0x67,
		0xF6, 0xEC, 0xED, 0xD4, 0x19, 0xDB, 0x06, 0xC1
	}
	/* no test vector for test_msg2 */
};

static const uint8_t	sha512_test_digests[][64] = {
	{
		/* for test_msg0 */
		0xDD, 0xAF, 0x35, 0xA1, 0x93, 0x61, 0x7A, 0xBA,
		0xCC, 0x41, 0x73, 0x49, 0xAE, 0x20, 0x41, 0x31,
		0x12, 0xE6, 0xFA, 0x4E, 0x89, 0xA9, 0x7E, 0xA2,
		0x0A, 0x9E, 0xEE, 0xE6, 0x4B, 0x55, 0xD3, 0x9A,
		0x21, 0x92, 0x99, 0x2A, 0x27, 0x4F, 0xC1, 0xA8,
		0x36, 0xBA, 0x3C, 0x23, 0xA3, 0xFE, 0xEB, 0xBD,
		0x45, 0x4D, 0x44, 0x23, 0x64, 0x3C, 0xE8, 0x0E,
		0x2A, 0x9A, 0xC9, 0x4F, 0xA5, 0x4C, 0xA4, 0x9F
	},
	{
		/* no test vector for test_msg1 */
		0
	},
	{
		/* for test_msg2 */
		0x8E, 0x95, 0x9B, 0x75, 0xDA, 0xE3, 0x13, 0xDA,
		0x8C, 0xF4, 0xF7, 0x28, 0x14, 0xFC, 0x14, 0x3F,
		0x8F, 0x77, 0x79, 0xC6, 0xEB, 0x9F, 0x7F, 0xA1,
		0x72, 0x99, 0xAE, 0xAD, 0xB6, 0x88, 0x90, 0x18,
		0x50, 0x1D, 0x28, 0x9E, 0x49, 0x00, 0xF7, 0xE4,
		0x33, 0x1B, 0x99, 0xDE, 0xC4, 0xB5, 0x43, 0x3A,
		0xC7, 0xD3, 0x29, 0xEE, 0xB6, 0xDD, 0x26, 0x54,
		0x5E, 0x96, 0xE5, 0x5B, 0x87, 0x4B, 0xE9, 0x09
	}
};

static const uint8_t	sha512_256_test_digests[][32] = {
	{
		/* for test_msg0 */
		0x53, 0x04, 0x8E, 0x26, 0x81, 0x94, 0x1E, 0xF9,
		0x9B, 0x2E, 0x29, 0xB7, 0x6B, 0x4C, 0x7D, 0xAB,
		0xE4, 0xC2, 0xD0, 0xC6, 0x34, 0xFC, 0x6D, 0x46,
		0xE0, 0xE2, 0xF1, 0x31, 0x07, 0xE7, 0xAF, 0x23
	},
	{
		/* no test vector for test_msg1 */
		0
	},
	{
		/* for test_msg2 */
		0x39, 0x28, 0xE1, 0x84, 0xFB, 0x86, 0x90, 0xF8,
		0x40, 0xDA, 0x39, 0x88, 0x12, 0x1D, 0x31, 0xBE,
		0x65, 0xCB, 0x9D, 0x3E, 0xF8, 0x3E, 0xE6, 0x14,
		0x6F, 0xEA, 0xC8, 0x61, 0xE1, 0x9B, 0x56, 0x3A
	}
};

static MunitResult
test_sha256_known(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	uint8_t digest[SHA256_DIGEST_LENGTH];

	hash_buf(SHA256, test_msg0, strlen(test_msg0), digest);
	munit_assert_memory_equal(sizeof (digest), digest,
	    sha256_test_digests[0]);

	hash_buf(SHA256, test_msg1, strlen(test_msg1), digest);
	munit_assert_memory_equal(sizeof (digest), digest,
	    sha256_test_digests[1]);

	return (MUNIT_OK);
}

static MunitResult
test_sha512_known(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	uint8_t digest[SHA512_DIGEST_LENGTH];

	hash_buf(SHA512, test_msg0, strlen(test_msg0), digest);
	munit_assert_memory_equal(sizeof (digest), digest,
	    sha512_test_digests[0]);

	hash_buf(SHA512, test_msg2, strlen(test_msg2), digest);
	munit_assert_memory_equal(sizeof (digest), digest,
	    sha512_test_digests[2]);

	return (MUNIT_OK);
}

static MunitResult
test_sha512_256_known(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	uint8_t digest[SHA512_256_DIGEST_LENGTH];

	hash_buf(SHA512_256, test_msg0, strlen(test_msg0), digest);
	munit_assert_memory_equal(sizeof (digest), digest,
	    sha512_256_test_digests[0]);

	hash_buf(SHA512_256, test_msg2, strlen(test_msg2), digest);
	munit_assert_memory_equal(sizeof (digest), digest,
	    sha512_256_test_digests[2]);

	return (MUNIT_OK);
}

/*
 * Split updates across the 64- and 128-byte message block boundaries
 * must produce the same digest as a single update.
 */
static MunitResult
test_incremental(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	fill_data();

	static const int algos[] = { SHA256, SHA512, SHA512_256 };
	static const size_t diglens[] = { SHA256_DIGEST_LENGTH,
	    SHA512_DIGEST_LENGTH, SHA512_256_DIGEST_LENGTH };
	static const size_t chunks[] = { 1, 63, 64, 65, 127, 128, 129, 1000 };

	for (size_t a = 0; a < ARRAY_SIZE(algos); a++) {
		uint8_t once[SHA512_DIGEST_LENGTH];
		uint8_t split[SHA512_DIGEST_LENGTH];
		SHA2_CTX ctx;
		size_t off = 0;

		hash_buf(algos[a], databuf, DATA_SIZE, once);

		SHA2Init(algos[a], &ctx);
		for (size_t c = 0; c < ARRAY_SIZE(chunks); c++) {
			SHA2Update(&ctx, databuf + off, chunks[c]);
			off += chunks[c];
		}
		SHA2Update(&ctx, databuf + off, DATA_SIZE - off);
		SHA2Final(split, &ctx);

		munit_assert_memory_equal(diglens[a], once, split);
	}

	return (MUNIT_OK);
}

/*
 * Every supported implementation must reproduce the known answer and
 * compute the same digest for a multi-block message.
 */
static MunitResult
test_impls(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	fill_data();

	static const char *const algos[] = { "sha256", "sha512" };
	static const int algotypes[] = { SHA256, SHA512 };
	static const uint8_t *const known[] = { sha256_test_digests[0],
	    sha512_test_digests[0] };
	static const size_t diglens[] = { SHA256_DIGEST_LENGTH,
	    SHA512_DIGEST_LENGTH };

	for (size_t a = 0; a < ARRAY_SIZE(algos); a++) {
		const zfs_impl_t *ops = zfs_impl_get_ops(algos[a]);
		uint8_t ref[SHA512_DIGEST_LENGTH];
		uint8_t digest[SHA512_DIGEST_LENGTH];
		uint32_t saved;

		unit_true(ops != NULL);
		saved = ops->getid();

		for (uint32_t id = 0; id < ops->getcnt(); id++) {
			ops->setid(id);

			hash_buf(algotypes[a], test_msg0, strlen(test_msg0),
			    digest);
			munit_assert_memory_equal(diglens[a], digest,
			    known[a]);

			hash_buf(algotypes[a], databuf, DATA_SIZE, digest);
			if (id == 0)
				memcpy(ref, digest, diglens[a]);
			else
				munit_assert_memory_equal(diglens[a],
				    ref, digest);
		}

		ops->setid(saved);
	}

	return (MUNIT_OK);
}

/* ========== */

static const MunitTest sha2_tests[] = {
	UNIT_TEST("sha256_known",	test_sha256_known),
	UNIT_TEST("sha512_known",	test_sha512_known),
	UNIT_TEST("sha512_256_known",	test_sha512_256_known),
	UNIT_TEST("incremental",	test_incremental),
	UNIT_TEST("impls",		test_impls),
	{ 0 },
};

static const MunitSuite sha2_test_suite = {
	"sha2.",
	sha2_tests,
	NULL,
	1,
	MUNIT_SUITE_OPTION_NONE,
};

int
main(int argc, char **argv)
{
	return (munit_suite_main(&sha2_test_suite, NULL, argc, argv));
}
