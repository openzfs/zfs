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
#include <sys/crypto/icp.h>
#include <aes/aes_impl.h>
#include <modes/gcm_impl.h>

#include "unit.h"

/* ========== */

/*
 * Every supported implementation passes the self-test run at init, and one
 * that computes wrong answers in key expansion, encryption, decryption or
 * GHASH multiplication fails it.
 */
static const aes_impl_ops_t *real_aes;
static const gcm_impl_ops_t *real_gcm;

static void
broken_generate(aes_key_t *key, const uint32_t *keyarr32, int keybits)
{
	real_aes->generate(key, keyarr32, keybits);
	key->encr_ks.ks32[key->nr * 4] ^= 1;
}

static void
broken_encrypt(const uint32_t rk[], int nr, const uint32_t pt[4],
    uint32_t ct[4])
{
	real_aes->encrypt(rk, nr, pt, ct);
	ct[3] ^= 1;
}

static void
broken_decrypt(const uint32_t rk[], int nr, const uint32_t ct[4],
    uint32_t pt[4])
{
	real_aes->decrypt(rk, nr, ct, pt);
	pt[0] ^= 1;
}

static void
broken_mul(uint64_t *x, uint64_t *y, uint64_t *res)
{
	real_gcm->mul(x, y, res);
	res[1] ^= 1;
}

static MunitResult
test_aes_selftest(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	static const char *const names[] = { "generic", "x86_64", "aesni" };

	for (int i = 0; i < ARRAY_SIZE(names); i++) {
		if (aes_impl_set(names[i]) == 0)
			unit_true(aes_impl_selftest(aes_impl_get_ops()));
	}

	unit_eq(aes_impl_set("generic"), 0);
	real_aes = aes_impl_get_ops();

	aes_impl_ops_t bad = *real_aes;
	bad.generate = broken_generate;
	unit_false(aes_impl_selftest(&bad));

	bad = *real_aes;
	bad.encrypt = broken_encrypt;
	unit_false(aes_impl_selftest(&bad));

	bad = *real_aes;
	bad.decrypt = broken_decrypt;
	unit_false(aes_impl_selftest(&bad));

	unit_eq(aes_impl_set("fastest"), 0);

	return (MUNIT_OK);
}

static MunitResult
test_gcm_selftest(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	static const char *const names[] = { "generic", "pclmulqdq" };

	for (int i = 0; i < ARRAY_SIZE(names); i++) {
		if (gcm_impl_set(names[i]) == 0)
			unit_true(gcm_impl_selftest(gcm_impl_get_ops()));
	}

	unit_eq(gcm_impl_set("generic"), 0);
	real_gcm = gcm_impl_get_ops();

	gcm_impl_ops_t bad = *real_gcm;
	bad.mul = broken_mul;
	unit_false(gcm_impl_selftest(&bad));

	unit_eq(gcm_impl_set("fastest"), 0);

	return (MUNIT_OK);
}

/* ========== */

static const MunitTest aes_tests[] = {
	UNIT_TEST("aes_selftest",	test_aes_selftest),
	UNIT_TEST("gcm_selftest",	test_gcm_selftest),
	{ 0 },
};

static const MunitSuite aes_test_suite = {
	"aes.",
	aes_tests,
	NULL,
	1,
	MUNIT_SUITE_OPTION_NONE,
};

int
main(int argc, char **argv)
{
	aes_impl_init();
	gcm_impl_init();
	return (munit_suite_main(&aes_test_suite, NULL, argc, argv));
}
