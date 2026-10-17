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
 * Copyright (c) 2026, TrueNAS.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/kmem.h>
#include <sys/nvpair.h>
#include <sys/sysmacros.h>

#include "unit.h"

/*
 * Stress/profiling tests for nvpair internals. They're not expected to fail;
 * they're designed to be run under a profiler, with judicious use of
 * --no-fork, --iterations, --param and --seed.
 *
 * Loosely based on Jackson Allan's hashtable benchmark
 *   https://jacksonallan.github.io/c_cpp_hash_tables_benchmark/
 *
 * - each test runs against a fixture: an nvlist pre-populated with n keys
 *   ("hit" keys), plus a second set of n keys never inserted ("miss" keys)
 *
 * - building the fixture happens in setup, outside the test function, so
 *   the test body is (mostly) just the operation under test
 *
 * - every test performs roughly NVS_ROUNDS operations regardless of n, so
 *   per-op cost can be compared across list sizes
 *
 * - parameters:
 *     unique: none, name, name_type (like the main nvpair test suite)
 *     keys: rand  (random strings of mixed length; best case for a weak hash)
 *           seq   (zero-padded decimal; shared prefix, varied suffix)
 *           zfs   (ZFS-inspired names with a few shared suffixes;
 *                  worst case for a hash that's weak in the low bits)
 *     n: 8, 64, 512, 4096 (most real nvlists are quite small!)
 */

/* max length + terminator of random keys */
#define	NVS_KEYLEN	32

/* number of rounds (ops) per test */
#define	NVS_ROUNDS	4096

typedef struct {
	nvlist_t	*nvl;
	uint_t		nvflag;
	uint_t		n;
	char		(*hit)[NVS_KEYLEN];	/* n keys, in the list */
	char		(*miss)[NVS_KEYLEN];	/* n keys, never in the list */
	uint_t		order[NVS_ROUNDS];	/* random key for each round */
} nvs_fixture_t;

/* ========== */

static uint_t
nvs_param_nvflag(const MunitParameter params[])
{
	const char *unique = munit_parameters_get(params, "unique");
	if (unique == NULL)
		munit_error("missing unique param");
	if (strcmp(unique, "none") == 0)
		return (0);
	if (strcmp(unique, "name") == 0)
		return (NV_UNIQUE_NAME);
	if (strcmp(unique, "name_type") == 0)
		return (NV_UNIQUE_NAME_TYPE);
	munit_errorf("invalid unique '%s'", unique);
}

static const char *const nvs_zfs_prefix[] = {
	"vdev_", "pool_", "top_", "feature_", "com.truenas:", "org.openzfs:",
};
static const char *const nvs_zfs_suffix[] = {
	"guid", "txg", "size", "count", "state",
};

typedef enum { KEYS_RAND, KEYS_SEQ, KEYS_ZFS } nvs_keystyle_t;

static nvs_keystyle_t
nvs_param_keystyle(const MunitParameter params[])
{
	const char *keyparam = munit_parameters_get(params, "keys");
	if (keyparam == NULL)
		munit_error("missing keys param");
	if (strcmp(keyparam, "rand") == 0)
		return (KEYS_RAND);
	if (strcmp(keyparam, "seq") == 0)
		return (KEYS_SEQ);
	if (strcmp(keyparam, "zfs") == 0)
		return (KEYS_ZFS);
	munit_errorf("invalid keys '%s'", keyparam);
}

static void
nvs_key_gen(nvs_keystyle_t keystyle, uint_t i, char *buf)
{
	switch (keystyle) {
	case KEYS_RAND:
		unit_rand_str(buf, munit_rand_int_range(8, NVS_KEYLEN));
		break;
	case KEYS_SEQ:
		snprintf(buf, NVS_KEYLEN, "%016u", i);
		break;
	case KEYS_ZFS:
		/* i keeps them unique; suffix is one of a handful */
		snprintf(buf, NVS_KEYLEN, "%s%u_%s",
		    nvs_zfs_prefix[i % ARRAY_SIZE(nvs_zfs_prefix)], i,
		    nvs_zfs_suffix[(i / ARRAY_SIZE(nvs_zfs_prefix)) %
		    ARRAY_SIZE(nvs_zfs_suffix)]);
		break;
	default:
		unit_unreachable();
	}
}

static void
nvs_shuffle(char (*keys)[NVS_KEYLEN], uint_t nk)
{
	char tmp[NVS_KEYLEN];
	for (uint_t i = nk - 1; i > 0; i--) {
		uint_t j = munit_rand_int_range(0, i);
		memcpy(tmp, keys[i], NVS_KEYLEN);
		memcpy(keys[i], keys[j], NVS_KEYLEN);
		memcpy(keys[j], tmp, NVS_KEYLEN);
	}
}

static void
nvs_fill(nvlist_t *nvl, const nvs_fixture_t *fx)
{
	for (uint_t i = 0; i < fx->n; i++)
		unit_ok(nvlist_add_boolean(nvl, fx->hit[i]));
}

static void *
nvs_setup(const MunitParameter params[], void *data)
{
	(void) data;

	nvs_fixture_t *fx = calloc(1, sizeof (nvs_fixture_t));
	fx->nvflag = nvs_param_nvflag(params);
	fx->n = atoi(munit_parameters_get(params, "n"));

	nvs_keystyle_t keystyle = nvs_param_keystyle(params);

	/*
	 * Generate 2*n keys of the given style, shuffle them, then take the
	 * first half as hits and the second half as misses. The shuffle is so
	 * that insertion order is different to generation order.
	 */
	uint_t nk = fx->n * 2;
	char (*keys)[NVS_KEYLEN] = calloc(nk, NVS_KEYLEN);
	for (uint_t i = 0; i < nk; i++)
		nvs_key_gen(keystyle, i, keys[i]);
	nvs_shuffle(keys, nk);
	fx->hit = keys;
	fx->miss = &keys[fx->n];

	/*
	 * We also precompute a random access order, so that we aren't using
	 * the same keys in the same order over and over, which makes it much
	 * harder for the CPU prefetcher and branch predictor to get clever
	 * and mess up our numbers.
	 */
	for (uint_t i = 0; i < NVS_ROUNDS; i++)
		fx->order[i] = munit_rand_int_range(0, fx->n - 1);

	unit_ok(nvlist_alloc(&fx->nvl, fx->nvflag, KM_SLEEP));
	nvs_fill(fx->nvl, fx);

	return (fx);
}

static void
nvs_teardown(void *data)
{
	nvs_fixture_t *fx = data;
	nvlist_free(fx->nvl);
	free(fx->hit);		/* base of the whole key array */
	free(fx);
}

/* ========== */

/* build and free a fresh list, repeatedly. includes alloc/free cost */
static MunitResult
test_nvs_insert(const MunitParameter params[], void *data)
{
	(void) params;
	nvs_fixture_t *fx = data;

	for (uint_t done = 0; done < NVS_ROUNDS; done += fx->n) {
		nvlist_t *nvl;
		unit_ok(nvlist_alloc(&nvl, fx->nvflag, KM_SLEEP));
		nvs_fill(nvl, fx);
		nvlist_free(nvl);
	}
	return (MUNIT_OK);
}

/* re-add existing keys. unique lists only (otherwise it's just insert) */
static MunitResult
test_nvs_replace(const MunitParameter params[], void *data)
{
	(void) params;
	nvs_fixture_t *fx = data;

	for (uint_t i = 0; i < NVS_ROUNDS; i++)
		unit_ok(nvlist_add_boolean(fx->nvl, fx->hit[fx->order[i]]));
	return (MUNIT_OK);
}

/* remove an existing key, then put it back (article's "erase existing") */
static MunitResult
test_nvs_remove_hit(const MunitParameter params[], void *data)
{
	(void) params;
	nvs_fixture_t *fx = data;

	for (uint_t i = 0; i < NVS_ROUNDS; i++) {
		const char *k = fx->hit[fx->order[i]];
		unit_ok(nvlist_remove_all(fx->nvl, k));
		unit_ok(nvlist_add_boolean(fx->nvl, k));
	}
	return (MUNIT_OK);
}

static MunitResult
test_nvs_remove_miss(const MunitParameter params[], void *data)
{
	(void) params;
	nvs_fixture_t *fx = data;

	for (uint_t i = 0; i < NVS_ROUNDS; i++)
		unit_eq(nvlist_remove_all(fx->nvl, fx->miss[fx->order[i]]),
		    ENOENT);
	return (MUNIT_OK);
}

/* lookup_* return ENOTSUP on non-unique lists, so unique only */
static MunitResult
test_nvs_lookup_hit(const MunitParameter params[], void *data)
{
	(void) params;
	nvs_fixture_t *fx = data;

	for (uint_t i = 0; i < NVS_ROUNDS; i++)
		unit_ok(nvlist_lookup_boolean(fx->nvl, fx->hit[fx->order[i]]));
	return (MUNIT_OK);
}

static MunitResult
test_nvs_lookup_miss(const MunitParameter params[], void *data)
{
	(void) params;
	nvs_fixture_t *fx = data;

	for (uint_t i = 0; i < NVS_ROUNDS; i++)
		unit_eq(nvlist_lookup_boolean(fx->nvl,
		    fx->miss[fx->order[i]]), ENOENT);
	return (MUNIT_OK);
}

static MunitResult
test_nvs_exists_hit(const MunitParameter params[], void *data)
{
	(void) params;
	nvs_fixture_t *fx = data;

	for (uint_t i = 0; i < NVS_ROUNDS; i++)
		unit_true(nvlist_exists(fx->nvl, fx->hit[fx->order[i]]));
	return (MUNIT_OK);
}

static MunitResult
test_nvs_exists_miss(const MunitParameter params[], void *data)
{
	(void) params;
	nvs_fixture_t *fx = data;

	for (uint_t i = 0; i < NVS_ROUNDS; i++)
		unit_false(nvlist_exists(fx->nvl, fx->miss[fx->order[i]]));
	return (MUNIT_OK);
}

/* full forward walk, repeated */
static MunitResult
test_nvs_iter(const MunitParameter params[], void *data)
{
	(void) params;
	nvs_fixture_t *fx = data;

	for (uint_t done = 0; done < NVS_ROUNDS; done += fx->n) {
		uint_t npairs = 0;
		nvpair_t *nvp = NULL;
		while ((nvp = nvlist_next_nvpair(fx->nvl, nvp)) != NULL)
			npairs++;
		unit_eq(npairs, fx->n);
	}
	return (MUNIT_OK);
}

/*
 * Two cursors walking the same list, half a list apart. Every call misses
 * the nvp_curr hint, so nvlist_contains_nvp() walks the list each step:
 * O(n^2) per pass. Expect this to fall off a cliff as n grows.
 */
static MunitResult
test_nvs_iter_two(const MunitParameter params[], void *data)
{
	(void) params;
	nvs_fixture_t *fx = data;

	for (uint_t done = 0; done < NVS_ROUNDS; done += fx->n) {
		nvpair_t *a = nvlist_next_nvpair(fx->nvl, NULL);
		nvpair_t *b = a;
		for (uint_t i = 0; i < fx->n / 2; i++)
			b = nvlist_next_nvpair(fx->nvl, b);

		uint_t npairs = 0;
		while (a != NULL || b != NULL) {
			if (a != NULL) {
				a = nvlist_next_nvpair(fx->nvl, a);
				npairs++;
			}
			if (b != NULL) {
				b = nvlist_next_nvpair(fx->nvl, b);
				npairs++;
			}
		}

		/* sanity; a makes n calls, b makes n-n/2 calls */
		unit_eq(npairs, (fx->n) + (fx->n - fx->n / 2));
	}
	return (MUNIT_OK);
}

/* drain the list while iterating, then refill. includes refill cost */
static MunitResult
test_nvs_iter_remove(const MunitParameter params[], void *data)
{
	(void) params;
	nvs_fixture_t *fx = data;

	for (uint_t done = 0; done < NVS_ROUNDS; done += fx->n) {
		nvpair_t *nvp = nvlist_next_nvpair(fx->nvl, NULL);
		while (nvp != NULL) {
			nvpair_t *next = nvlist_next_nvpair(fx->nvl, nvp);
			unit_ok(nvlist_remove_nvpair(fx->nvl, nvp));
			nvp = next;
		}
		unit_true(nvlist_empty(fx->nvl));
		nvs_fill(fx->nvl, fx);
	}
	return (MUNIT_OK);
}

static MunitResult
test_nvs_dup(const MunitParameter params[], void *data)
{
	(void) params;
	nvs_fixture_t *fx = data;

	for (uint_t done = 0; done < NVS_ROUNDS; done += fx->n) {
		nvlist_t *dup;
		unit_ok(nvlist_dup(fx->nvl, &dup, KM_SLEEP));
		nvlist_free(dup);
	}
	return (MUNIT_OK);
}

/* native pack + unpack round trip; exercises decode's add/link path */
static MunitResult
test_nvs_pack_unpack(const MunitParameter params[], void *data)
{
	(void) params;
	nvs_fixture_t *fx = data;

	for (uint_t done = 0; done < NVS_ROUNDS; done += fx->n) {
		char *buf = NULL;
		size_t len = 0;
		nvlist_t *out;
		unit_ok(nvlist_pack(fx->nvl, &buf, &len, NV_ENCODE_NATIVE,
		    KM_SLEEP));
		unit_ok(nvlist_unpack(buf, len, &out, KM_SLEEP));
		nvlist_free(out);
		fnvlist_pack_free(buf, len);
	}
	return (MUNIT_OK);
}

/* embedded copies: add the fixture list as an array of 8 sub-lists */
static MunitResult
test_nvs_embedded(const MunitParameter params[], void *data)
{
	(void) params;
	nvs_fixture_t *fx = data;

	const nvlist_t *subs[8];
	for (uint_t i = 0; i < ARRAY_SIZE(subs); i++)
		subs[i] = fx->nvl;

	for (uint_t done = 0; done < NVS_ROUNDS;
	    done += fx->n * ARRAY_SIZE(subs)) {
		nvlist_t *top;
		unit_ok(nvlist_alloc(&top, fx->nvflag, KM_SLEEP));
		unit_ok(nvlist_add_nvlist_array(top, "subs", subs,
		    ARRAY_SIZE(subs)));
		nvlist_free(top);
	}
	return (MUNIT_OK);
}

/* ========== */

static const MunitParameterEnum nvs_all_params[] = {
	UNIT_PARAM("unique", "none", "name", "name_type"),
	UNIT_PARAM("keys", "rand", "seq", "zfs"),
	UNIT_PARAM("n", "8", "64", "512", "4096"),
	{ 0 },
};
static const MunitParameterEnum nvs_unique_params[] = {
	UNIT_PARAM("unique", "name", "name_type"),
	UNIT_PARAM("keys", "rand", "seq", "zfs"),
	UNIT_PARAM("n", "8", "64", "512", "4096"),
	{ 0 },
};

/* not using UNIT_TEST here because it has no way to set setup/teardown funcs */
#define	NVS_TEST(name, fn, params)					\
	{ (char *)(name), (fn), nvs_setup, nvs_teardown,		\
	    MUNIT_TEST_OPTION_NONE, (MunitParameterEnum *)(params) }

static const MunitTest nvpair_stress_tests[] = {
	NVS_TEST("insert",	test_nvs_insert,	nvs_all_params),
	NVS_TEST("replace",	test_nvs_replace,	nvs_unique_params),
	NVS_TEST("remove_hit",	test_nvs_remove_hit,	nvs_all_params),
	NVS_TEST("remove_miss",	test_nvs_remove_miss,	nvs_all_params),
	NVS_TEST("lookup_hit",	test_nvs_lookup_hit,	nvs_unique_params),
	NVS_TEST("lookup_miss",	test_nvs_lookup_miss,	nvs_unique_params),
	NVS_TEST("exists_hit",	test_nvs_exists_hit,	nvs_all_params),
	NVS_TEST("exists_miss",	test_nvs_exists_miss,	nvs_all_params),
	NVS_TEST("iter",	test_nvs_iter,		nvs_all_params),
	NVS_TEST("iter_two",	test_nvs_iter_two,	nvs_all_params),
	NVS_TEST("iter_remove",	test_nvs_iter_remove,	nvs_all_params),
	NVS_TEST("dup",		test_nvs_dup,		nvs_all_params),
	NVS_TEST("pack_unpack",	test_nvs_pack_unpack,	nvs_all_params),
	NVS_TEST("embedded",	test_nvs_embedded,	nvs_all_params),
	{ 0 },
};

static const MunitSuite nvpair_stress_test_suite = {
	"nvs.",
	(MunitTest *)nvpair_stress_tests,
	NULL,
	1,
	MUNIT_SUITE_OPTION_NONE,
};

int
main(int argc, char **argv)
{
	return (munit_suite_main(&nvpair_stress_test_suite, NULL, argc, argv));
}
