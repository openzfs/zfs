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
 * Copyright (c) 2026, Will Rouesnel.
 */

#include <sys/types.h>
#include <sys/zfs_impl.h>
#include <sys/blake3.h>
#include "blake3_impl.h"

#include "unit.h"

/* ========== */

/*
 * Every supported implementation passes the self-test run at init, and one
 * that computes wrong answers fails it, whether it goes wrong on every
 * compression or only in hash_many(), the multi-chunk path that the SIMD
 * implementations speed up.
 */
static const blake3_ops_t *real_blake3;

static void
broken_compress_in_place(uint32_t cv[8], const uint8_t block[BLAKE3_BLOCK_LEN],
    uint8_t block_len, uint64_t counter, uint8_t flags)
{
	real_blake3->compress_in_place(cv, block, block_len, counter, flags);
	cv[0] ^= 1;
}

static void
broken_hash_many(const uint8_t * const *inputs, size_t num_inputs,
    size_t blocks, const uint32_t key[8], uint64_t counter,
    boolean_t increment_counter, uint8_t flags, uint8_t flags_start,
    uint8_t flags_end, uint8_t *out)
{
	real_blake3->hash_many(inputs, num_inputs, blocks, key, counter,
	    increment_counter, flags, flags_start, flags_end, out);
	if (num_inputs > 1)
		out[BLAKE3_OUT_LEN] ^= 0x80;
}

static MunitResult
test_selftest(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	const zfs_impl_t *ops = zfs_impl_get_ops("blake3");
	uint32_t saved = ops->getid();

	for (uint32_t id = 0; id < ops->getcnt(); id++) {
		ops->setid(id);
		unit_true(blake3_impl_selftest(blake3_get_ops()));
	}

	ops->setid(0);
	real_blake3 = blake3_get_ops();

	blake3_ops_t bad = *real_blake3;
	bad.compress_in_place = broken_compress_in_place;
	unit_false(blake3_impl_selftest(&bad));

	bad = *real_blake3;
	bad.hash_many = broken_hash_many;
	unit_false(blake3_impl_selftest(&bad));

	ops->setid(saved);

	return (MUNIT_OK);
}

/* ========== */

static const MunitTest blake3_tests[] = {
	UNIT_TEST("selftest",		test_selftest),
	{ 0 },
};

static const MunitSuite blake3_test_suite = {
	"blake3.",
	blake3_tests,
	NULL,
	1,
	MUNIT_SUITE_OPTION_NONE,
};

int
main(int argc, char **argv)
{
	return (munit_suite_main(&blake3_test_suite, NULL, argc, argv));
}
