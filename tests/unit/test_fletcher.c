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
#include <sys/spa_checksum.h>
#include "zfs_fletcher.h"

#include "unit.h"

/* ========== */

/*
 * Fletcher checksums require the buffer size to be 4-byte aligned, and the
 * SIMD implementations process the data in wide strides.  We use a buffer that
 * is a multiple of every implementation's stride so a single call never has a
 * scalar remainder, which keeps the cross-implementation comparison exact.
 */
#define	DATA_SIZE	8192

static uint8_t databuf[DATA_SIZE] __attribute__((aligned(64)));

/* Deterministic; distinct bytes per word so native and byteswap differ. */
static void
fill_data(void)
{
	for (size_t i = 0; i < DATA_SIZE; i++)
		databuf[i] = (uint8_t)i;
}

/* ========== */

/* Known answers, verifiable by hand; small buffers take the scalar path. */
static MunitResult
test_fletcher4_known(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	zio_cksum_t zc;

	/* A single word w yields {w, w, w, w}. */
	const uint32_t one[1] = { 0x01020304u };
	fletcher_4_native(one, sizeof (one), NULL, &zc);
	unit_eq(zc.zc_word[0], 0x01020304ULL);
	unit_eq(zc.zc_word[1], 0x01020304ULL);
	unit_eq(zc.zc_word[2], 0x01020304ULL);
	unit_eq(zc.zc_word[3], 0x01020304ULL);

	/* Words {1, 2} yield {1+2, 2*1+2, 3*1+2, 4*1+2}. */
	const uint32_t two[2] = { 1u, 2u };
	fletcher_4_native(two, sizeof (two), NULL, &zc);
	unit_eq(zc.zc_word[0], 3);
	unit_eq(zc.zc_word[1], 4);
	unit_eq(zc.zc_word[2], 5);
	unit_eq(zc.zc_word[3], 6);

	/* An empty buffer is the zero checksum. */
	fletcher_4_native(databuf, 0, NULL, &zc);
	uint64_t bits = zc.zc_word[0] | zc.zc_word[1] |
	    zc.zc_word[2] | zc.zc_word[3];
	unit_zero(bits);

	return (MUNIT_OK);
}

/* Accumulating 4-byte-aligned chunks must match the single-call checksum. */
static MunitResult
test_fletcher4_incremental(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	fill_data();

	zio_cksum_t once, inc;
	fletcher_4_native(databuf, DATA_SIZE, NULL, &once);

	fletcher_init(&inc);
	(void) fletcher_4_incremental_native(databuf, 2048, &inc);
	(void) fletcher_4_incremental_native(databuf + 2048, 2048, &inc);
	(void) fletcher_4_incremental_native(databuf + 4096, DATA_SIZE - 4096,
	    &inc);
	unit_true(ZIO_CHECKSUM_EQUAL(once, inc));

	return (MUNIT_OK);
}

/* varsize equals native when aligned; drops the trailing size % 4 bytes. */
static MunitResult
test_fletcher4_varsize(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	fill_data();

	zio_cksum_t native, var;
	fletcher_4_native(databuf, 4096, NULL, &native);
	fletcher_4_native_varsize(databuf, 4096, &var);
	unit_true(ZIO_CHECKSUM_EQUAL(native, var));

	zio_cksum_t var_unaligned, var_trunc;
	fletcher_4_native_varsize(databuf, 4098, &var_unaligned);
	fletcher_4_native_varsize(databuf, 4096, &var_trunc);
	unit_true(ZIO_CHECKSUM_EQUAL(var_unaligned, var_trunc));

	return (MUNIT_OK);
}

/* Native and byteswapped checksums differ on byte-asymmetric data. */
static MunitResult
test_fletcher4_byteswap(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	fill_data();

	zio_cksum_t native, swap;
	fletcher_4_native(databuf, DATA_SIZE, NULL, &native);
	fletcher_4_byteswap(databuf, DATA_SIZE, NULL, &swap);
	unit_false(ZIO_CHECKSUM_EQUAL(native, swap));

	return (MUNIT_OK);
}

/* Every supported implementation must agree; unsupported ones are skipped. */
static MunitResult
test_fletcher4_impls(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	fill_data();

	static const char *const impls[] = {
		"scalar", "superscalar", "superscalar4",
		"sse2", "ssse3", "avx2", "avx512f", "avx512bw",
		"aarch64_neon", NULL,
	};

	/* The generic implementations are always available. */
	unit_eq(fletcher_4_impl_set("scalar"), 0);
	unit_eq(fletcher_4_impl_set("superscalar"), 0);
	unit_eq(fletcher_4_impl_set("superscalar4"), 0);

	zio_cksum_t ref = { { 0 } };
	boolean_t have_ref = B_FALSE;

	for (int i = 0; impls[i] != NULL; i++) {
		if (fletcher_4_impl_set(impls[i]) != 0)
			continue;	/* not supported on this host */

		zio_cksum_t zc;
		fletcher_4_native(databuf, DATA_SIZE, NULL, &zc);

		if (!have_ref) {
			ref = zc;
			have_ref = B_TRUE;
		} else {
			unit_true(ZIO_CHECKSUM_EQUAL(ref, zc));
		}
	}

	(void) fletcher_4_impl_set("fastest");
	return (MUNIT_OK);
}

/* Fletcher-2 known answers, verifiable by hand.  It folds 64-bit words. */
static MunitResult
test_fletcher2_known(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	zio_cksum_t zc;

	/* One pair {x, y} yields {x, y, x, y}. */
	const uint64_t pair[2] = { 1, 2 };
	fletcher_2_native(pair, sizeof (pair), NULL, &zc);
	unit_eq(zc.zc_word[0], 1);
	unit_eq(zc.zc_word[1], 2);
	unit_eq(zc.zc_word[2], 1);
	unit_eq(zc.zc_word[3], 2);

	/* Two pairs {w, x, y, z} yield {w+y, x+z, 2w+y, 2x+z}. */
	const uint64_t pairs[4] = { 1, 2, 3, 4 };
	fletcher_2_native(pairs, sizeof (pairs), NULL, &zc);
	unit_eq(zc.zc_word[0], 4);
	unit_eq(zc.zc_word[1], 6);
	unit_eq(zc.zc_word[2], 5);
	unit_eq(zc.zc_word[3], 8);

	/* An empty buffer is the zero checksum. */
	fletcher_2_native(databuf, 0, NULL, &zc);
	uint64_t bits = zc.zc_word[0] | zc.zc_word[1] |
	    zc.zc_word[2] | zc.zc_word[3];
	unit_zero(bits);

	return (MUNIT_OK);
}

/* Fletcher-2: the incremental path must match the single call. */
static MunitResult
test_fletcher2_incremental(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	fill_data();

	zio_cksum_t once, inc;
	fletcher_2_native(databuf, DATA_SIZE, NULL, &once);

	fletcher_init(&inc);
	(void) fletcher_2_incremental_native(databuf, 4096, &inc);
	(void) fletcher_2_incremental_native(databuf + 4096, DATA_SIZE - 4096,
	    &inc);
	unit_true(ZIO_CHECKSUM_EQUAL(once, inc));

	return (MUNIT_OK);
}

/* ========== */

static const MunitTest fletcher_tests[] = {
	UNIT_TEST("fletcher4_known",		test_fletcher4_known),
	UNIT_TEST("fletcher4_incremental",	test_fletcher4_incremental),
	UNIT_TEST("fletcher4_varsize",		test_fletcher4_varsize),
	UNIT_TEST("fletcher4_byteswap",		test_fletcher4_byteswap),
	UNIT_TEST("fletcher4_impls",		test_fletcher4_impls),
	UNIT_TEST("fletcher2_known",		test_fletcher2_known),
	UNIT_TEST("fletcher2_incremental",	test_fletcher2_incremental),
	{ 0 },
};

static const MunitSuite fletcher_test_suite = {
	"fletcher.",
	fletcher_tests,
	NULL,
	1,
	MUNIT_SUITE_OPTION_NONE,
};

int
main(int argc, char **argv)
{
	fletcher_4_init();
	int ret = munit_suite_main(&fletcher_test_suite, NULL, argc, argv);
	fletcher_4_fini();
	return (ret);
}
