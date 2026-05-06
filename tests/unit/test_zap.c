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
 * Copyright (c) 2026, TrueNAS.
 */

#include <stdbool.h>

#include <sys/zap.h>
#include <sys/btree.h>
typedef struct spa spa_t;	/* forward decl for zap_impl.h */
#include <sys/zap_impl.h>

#include "mock_dmu.h"
#include "unit.h"

/* ========== */

/*
 * Normally defined and initialised in arc.c.  We define and initialise it
 * ourselves here so this mock can be linked without arc.c.
 */
uint64_t zfs_crc64_table[256];

static void
mock_crc64_init(void)
{
	for (int i = 0; i < 256; i++) {
		uint64_t ct = i;
		for (int j = 8; j > 0; j--)
			ct = (ct >> 1) ^ (-(ct & 1) & ZFS_CRC64_POLY);
		zfs_crc64_table[i] = ct;
	}
}

/* Misc utility functions. */

#define	rd64(ptr, off)	(*(uint64_t *)((const char *)(ptr) + (off)))

/* ========== */

/* ZAP-specific mocks and other test helpers. */

/* Create a microzap backed by a mock dnode. */
static dnode_t *
mock_zap_create_microzap(void) {
	/*
	 * We use DMU_OTN_ZAP_DATA so that DMU_OT_BYTESWAP() returns
	 * DMU_BSWAP_ZAP without consulting dmu_ot[], which is not currently
	 * provided in the mock.
	 */
	mock_dnode_t *mdn = mock_dnode_create(512, DMU_OTN_ZAP_DATA);
	dnode_t *dn = (dnode_t *)mdn;
	dmu_tx_t *tx = (dmu_tx_t *)mock_tx_create();
	mzap_create_impl(dn, 0, 0, tx);
	mock_tx_destroy((mock_dmu_tx_t *)tx);
	return (dn);
}

/* Create a fatzap backed by a mock dnode. */
static dnode_t *
mock_zap_create_fatzap(void)
{
	/*
	 * We can only create microzaps directly. They only take u64s as a
	 * value, so we add a u16 to trigger an upgrade to fatzap.
	 */
	dnode_t *dn = mock_zap_create_microzap();
	dmu_tx_t *tx = (dmu_tx_t *)mock_tx_create();
	uint16_t upgrade = 0;
	zap_add_by_dnode(dn, "_upgrade", sizeof (uint16_t), 1, &upgrade, tx);
	zap_remove_by_dnode(dn, "_upgrade", tx);
	mock_tx_destroy((mock_dmu_tx_t *)tx);
	return (dn);
}

static bool
mock_zap_is_microzap(dnode_t *dn)
{
	/* check block 0 has a microzap header */
	const void *blk = mock_dnode_block_data((mock_dnode_t *)dn, 0);
	return (rd64(blk, 0) == ZBT_MICRO);
}

static bool
mock_zap_is_fatzap(dnode_t *dn)
{
	/* check block 0 has a fatzap header */
	const void *blk = mock_dnode_block_data((mock_dnode_t *)dn, 0);
	return (rd64(blk, 0) == ZBT_HEADER && rd64(blk, 8) == ZAP_MAGIC);
}

static void
mock_zap_destroy(dnode_t *dn)
{
	mock_dnode_t *mdn = (mock_dnode_t *)dn;
	unit_eq(mock_dnode_refcount(mdn), 1);
	mock_dnode_destroy(mdn);
}

/* Create a ZAP of the type named in the given test params. */
static dnode_t *
mock_zap_create_params(const MunitParameter params[], const char *key) {
	const char *type = munit_parameters_get(params, key);
	if (type == NULL)
		munit_error("mock_zap_create_params: missing type param");
	else if (strcmp(type, "micro") == 0)
		return (mock_zap_create_microzap());
	else if (strcmp(type, "fat") == 0)
		return (mock_zap_create_fatzap());
	else
		munit_errorf("mock_zap_create_params: invalid type '%s'", type);
	__builtin_unreachable();
}

/*
 * Confirm the stored ZAP is of the type named in the given test params. This
 * is useful for sanity checks within tests that a ZAP wasn't unexpectedly
 * upgraded during the test.
 */
static bool
mock_zap_is_params(dnode_t *dn, const MunitParameter params[],
    const char *key)
{
	const char *type = munit_parameters_get(params, key);
	if (type == NULL)
		munit_error("mock_zap_is_params: missing type param");
	else if (strcmp(type, "micro") == 0)
		return (mock_zap_is_microzap(dn));
	else if (strcmp(type, "fat") == 0)
		return (mock_zap_is_fatzap(dn));
	else
		munit_errorf("mock_zap_is_params: invalid type '%s'", type);
	__builtin_unreachable();
}

/* ========== */

/*
 * Sanity checks for mock ZAPs. Ensures that the mock_zap_create_* functions
 * really do create the right kind of ZAPs, since many of the tests need to
 * run against both kinds to confirm that they all work the same way.
 */
static MunitResult
test_mock_microzap_sanity(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	dnode_t *dn = mock_zap_create_microzap();
	unit_true(mock_zap_is_microzap(dn));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

static MunitResult
test_mock_fatzap_sanity(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	dnode_t *dn = mock_zap_create_fatzap();
	unit_true(mock_zap_is_fatzap(dn));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

/* ========== */

/*
 * A simple add, lookup and remove test. Confirms basic operation. These are
 * tested together simply because all other tests rely on these primitives.
 */
static MunitResult
test_zap_basic(const MunitParameter params[], void *data)
{
	(void) data;

	dnode_t *dn = mock_zap_create_params(params, "type");
	dmu_tx_t *tx = (dmu_tx_t *)mock_tx_create();

	/* Insert a few entries. */
	uint64_t val42 = 42;
	uint64_t val99 = 99;
	uint64_t val0  = 0;

	unit_ok(zap_add_by_dnode(dn, "hello",
	    sizeof (uint64_t), 1, &val42, tx));
	unit_ok(zap_add_by_dnode(dn, "world",
	    sizeof (uint64_t), 1, &val99, tx));
	unit_ok(zap_add_by_dnode(dn, "zero",
	    sizeof (uint64_t), 1, &val0, tx));

	/* Lookup each entry. */
	uint64_t result = 0;
	unit_ok(zap_lookup_by_dnode(dn, "hello",
	    sizeof (uint64_t), 1, &result));
	unit_eq(result, 42);

	unit_ok(zap_lookup_by_dnode(dn, "world",
	    sizeof (uint64_t), 1, &result));
	unit_eq(result, 99);

	unit_ok(zap_lookup_by_dnode(dn, "zero",
	    sizeof (uint64_t), 1, &result));
	unit_eq(result, 0);

	/* Non-existent key should return ENOENT. */
	unit_err(zap_lookup_by_dnode(dn, "nope",
	    sizeof (uint64_t), 1, &result), ENOENT);

	/* Removing an entry should make it impossible to look up. */
	unit_ok(zap_remove_by_dnode(dn, "world", tx));
	unit_err(zap_lookup_by_dnode(dn, "world",
	    sizeof (uint64_t), 1, &result), ENOENT);

	mock_tx_destroy((mock_dmu_tx_t *)tx);
	unit_true(mock_zap_is_params(dn, params, "type"));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

/* ========== */

/*
 * "Core" ZAP API tests. Covers the most basic functionality upon which which
 * everything else is built.
 *
 * Note that to avoid microzap upgrade here, we only short keys and
 * single-uint64 values.
 */

/* zap_add: add new items. */
static MunitResult
test_zap_add(const MunitParameter params[], void *data)
{
	(void) data;

	dnode_t *dn = mock_zap_create_params(params, "type");
	dmu_tx_t *tx = (dmu_tx_t *)mock_tx_create();

	/* A key added can be found by that name. */
	uint64_t va = 1, var = 0;
	unit_ok(zap_add_by_dnode(dn, "a", sizeof (uint64_t), 1, &va, tx));
	unit_ok(zap_lookup_by_dnode(dn, "a", sizeof (uint64_t), 1, &var));
	unit_eq(var, 1);

	/* Another key added can be found by that name. */
	uint64_t vb = 2, vbr = 0;
	unit_ok(zap_add_by_dnode(dn, "b", sizeof (uint64_t), 1, &vb, tx));
	unit_ok(zap_lookup_by_dnode(dn, "b", sizeof (uint64_t), 1, &vbr));
	unit_eq(vbr, 2);

	/* The first key is still findable with the right value. */
	var = 0;
	unit_ok(zap_lookup_by_dnode(dn, "a", sizeof (uint64_t), 1, &var));
	unit_eq(var, 1);

	/* Adding the key again fails. */
	unit_err(zap_add_by_dnode(dn, "a",
	    sizeof (uint64_t), 1, &va, tx), EEXIST);

	/* Adding the key with a different value still fails. */
	va = 2;
	unit_err(zap_add_by_dnode(dn, "a",
	    sizeof (uint64_t), 1, &va, tx), EEXIST);

	/* And is still findable with the original value. */
	var = 0;
	unit_ok(zap_lookup_by_dnode(dn, "a", sizeof (uint64_t), 1, &var));
	unit_eq(var, 1);

	mock_tx_destroy((mock_dmu_tx_t *)tx);
	unit_true(mock_zap_is_params(dn, params, "type"));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

/* zap_update: add new or replace existing items. */
static MunitResult
test_zap_update(const MunitParameter params[], void *data)
{
	(void) data;

	dnode_t *dn = mock_zap_create_params(params, "type");
	dmu_tx_t *tx = (dmu_tx_t *)mock_tx_create();

	/* Update on a non-existent key inserts it. */
	uint64_t va = 1, var = 0;
	unit_ok(zap_update_by_dnode(dn, "a", sizeof (uint64_t), 1, &va, tx));
	unit_ok(zap_lookup_by_dnode(dn, "a", sizeof (uint64_t), 1, &var));
	unit_eq(var, 1);

	/* Update on an existing key replaces it without error. */
	va = 2;
	unit_ok(zap_update_by_dnode(dn, "a", sizeof (uint64_t), 1, &va, tx));
	unit_ok(zap_lookup_by_dnode(dn, "a", sizeof (uint64_t), 1, &var));
	unit_eq(var, 2);

	/* Count should still be 1 (no duplicate was created). */
	uint64_t count = 0;
	unit_ok(zap_count_by_dnode(dn, &count));
	unit_eq(count, 1);

	mock_tx_destroy((mock_dmu_tx_t *)tx);
	unit_true(mock_zap_is_params(dn, params, "type"));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

/* zap_remove: remove existing items. */
static MunitResult
test_zap_remove(const MunitParameter params[], void *data)
{
	(void) data;

	dnode_t *dn = mock_zap_create_params(params, "type");
	dmu_tx_t *tx = (dmu_tx_t *)mock_tx_create();

	/* Removing a non-existing key fails. */
	unit_err(zap_remove_by_dnode(dn, "a", tx), ENOENT);

	/* Adding two keys. */
	uint64_t va = 1, vb = 2;
	unit_ok(zap_add_by_dnode(dn, "a", sizeof (uint64_t), 1, &va, tx));
	unit_ok(zap_add_by_dnode(dn, "b", sizeof (uint64_t), 1, &vb, tx));

	/* Remove an existing key succeeds. */
	unit_ok(zap_remove_by_dnode(dn, "a", tx));

	/* After removing, looking up removed key fails. */
	uint64_t var = 0;
	unit_err(
	    zap_lookup_by_dnode(dn, "a", sizeof (uint64_t), 1, &var), ENOENT);

	/* Looking up the other key succeeds, and has the correct value. */
	uint64_t vbr = 0;
	unit_ok(zap_lookup_by_dnode(dn, "b", sizeof (uint64_t), 1, &vbr));
	unit_eq(vbr, 2);

	mock_tx_destroy((mock_dmu_tx_t *)tx);
	unit_true(mock_zap_is_params(dn, params, "type"));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

/* zap_count: number of entries, typically without lookup or traversal. */
static MunitResult
test_zap_count(const MunitParameter params[], void *data)
{
	(void) data;

	dnode_t *dn = mock_zap_create_params(params, "type");
	dmu_tx_t *tx = (dmu_tx_t *)mock_tx_create();

	/* A new ZAP has zero entries. */
	uint64_t count = 0;
	unit_ok(zap_count_by_dnode(dn, &count));
	unit_eq(count, 0);

	/* Adding two keys bumps the count to 2. */
	uint64_t v = 1;
	unit_ok(zap_add_by_dnode(dn, "a", sizeof (uint64_t), 1, &v, tx));
	unit_ok(zap_add_by_dnode(dn, "b", sizeof (uint64_t), 1, &v, tx));
	unit_ok(zap_count_by_dnode(dn, &count));
	unit_eq(count, 2);

	/* Removing a key reduces the count. */
	unit_ok(zap_remove_by_dnode(dn, "a", tx));
	unit_ok(zap_count_by_dnode(dn, &count));
	unit_eq(count, 1);

	mock_tx_destroy((mock_dmu_tx_t *)tx);
	unit_true(mock_zap_is_params(dn, params, "type"));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

/* zap_contains: existence check without reading the value. */
static MunitResult
test_zap_contains(const MunitParameter params[], void *data)
{
	(void) data;

	dnode_t *dn = mock_zap_create_params(params, "type");
	dmu_tx_t *tx = (dmu_tx_t *)mock_tx_create();

	uint64_t v = 1;
	unit_ok(zap_add_by_dnode(dn, "a", sizeof (uint64_t), 1, &v, tx));
	unit_ok(zap_contains_by_dnode(dn, "a"));
	unit_err(zap_contains_by_dnode(dn, "b"), ENOENT);

	mock_tx_destroy((mock_dmu_tx_t *)tx);
	unit_true(mock_zap_is_params(dn, params, "type"));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

/* zap_length: item metadata without reading the value. */
static MunitResult
test_zap_length(const MunitParameter params[], void *data)
{
	(void) data;

	dnode_t *dn = mock_zap_create_params(params, "type");
	dmu_tx_t *tx = (dmu_tx_t *)mock_tx_create();

	/* uint64: integer_size=8, num_integers=1. */
	uint64_t v = 42;
	unit_ok(zap_add_by_dnode(dn, "u64",
	    sizeof (uint64_t), 1, &v, tx));

	uint64_t isz = 0, nint = 0;
	unit_ok(zap_length_by_dnode(dn, "u64", &isz, &nint));
	unit_eq(isz, 8);
	unit_eq(nint, 1);

	/* Missing key returns ENOENT. */
	unit_err(zap_length_by_dnode(dn, "nope", &isz, &nint), ENOENT);

	/* Either output pointer may be NULL. */
	isz = 0; nint = 0;
	unit_ok(zap_length_by_dnode(dn, "u64", NULL, &nint));
	unit_ok(zap_length_by_dnode(dn, "u64", &isz, NULL));
	unit_eq(isz, 8);
	unit_eq(nint, 1);

	mock_tx_destroy((mock_dmu_tx_t *)tx);
	unit_true(mock_zap_is_params(dn, params, "type"));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

/* zap_increment: add integer value to existing integer */
static MunitResult
test_zap_increment(const MunitParameter params[], void *data)
{
	(void) data;

	dnode_t *dn = mock_zap_create_params(params, "type");
	dmu_tx_t *tx = (dmu_tx_t *)mock_tx_create();

	uint64_t r = 0;

	/* Increment a missing key creates it with that value. */
	unit_ok(zap_increment_by_dnode(dn, "a", 5, tx));
	unit_ok(zap_lookup_by_dnode(dn, "a", sizeof (uint64_t), 1, &r));
	unit_eq(r, 5);

	/* Further increments accumulate. */
	unit_ok(zap_increment_by_dnode(dn, "a", 3, tx));
	unit_ok(zap_lookup_by_dnode(dn, "a", sizeof (uint64_t), 1, &r));
	unit_eq(r, 8);

	/* Decrement works. */
	unit_ok(zap_increment_by_dnode(dn, "a", -2, tx));
	unit_ok(zap_lookup_by_dnode(dn, "a", sizeof (uint64_t), 1, &r));
	unit_eq(r, 6);

	/* Zero delta leaves it unchanged. */
	r = 0;
	unit_ok(zap_increment_by_dnode(dn, "a", 0, tx));
	unit_ok(zap_lookup_by_dnode(dn, "a", sizeof (uint64_t), 1, &r));
	unit_eq(r, 6);

	/* Decrementing to zero removes the entry. */
	unit_ok(zap_increment_by_dnode(dn, "a", -6, tx));
	unit_err(zap_lookup_by_dnode(dn, "a",
	    sizeof (uint64_t), 1, &r), ENOENT);

	/* Delta of zero is a no-op even for a missing key. */
	unit_ok(zap_increment_by_dnode(dn, "a", 0, tx));
	unit_err(zap_lookup_by_dnode(dn, "a",
	    sizeof (uint64_t), 1, &r), ENOENT);

	mock_tx_destroy((mock_dmu_tx_t *)tx);
	unit_true(mock_zap_is_params(dn, params, "type"));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

/* ========== */

/*
 * zap_add_int/zap_remove_int/zap_lookup_int: single uint64_t value,
 * stringified to form the key.
 */
static MunitResult
test_zap_int(const MunitParameter params[], void *data)
{
	(void) data;

	dnode_t *dn = mock_zap_create_params(params, "type");
	dmu_tx_t *tx = (dmu_tx_t *)mock_tx_create();

	/* Add some ints. */
	unit_ok(zap_add_int_by_dnode(dn, 5, tx));
	unit_ok(zap_add_int_by_dnode(dn, 17, tx));

	/* Confirm they're there. */
	unit_ok(zap_lookup_int_by_dnode(dn, 17));
	unit_ok(zap_lookup_int_by_dnode(dn, 5));

	/* But not something we didn't add. */
	unit_err(zap_lookup_int_by_dnode(dn, 23), ENOENT);

	/* Adding something that already exists fails. */
	unit_err(zap_add_int_by_dnode(dn, 17, tx), EEXIST);

	/* Removing it works, and then it can't be found. */
	unit_ok(zap_remove_int_by_dnode(dn, 17, tx));
	unit_err(zap_lookup_int_by_dnode(dn, 17), ENOENT);

	/* Add it can be added back. */
	unit_ok(zap_add_int_by_dnode(dn, 17, tx));
	unit_ok(zap_lookup_int_by_dnode(dn, 17));

	mock_tx_destroy((mock_dmu_tx_t *)tx);
	unit_true(mock_zap_is_params(dn, params, "type"));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

/* zap_*_int_key: like zap_*_int, but with separate value. */
static MunitResult
test_zap_int_keys(const MunitParameter params[], void *data)
{
	(void) data;

	dnode_t *dn = mock_zap_create_params(params, "type");
	dmu_tx_t *tx = (dmu_tx_t *)mock_tx_create();

	/* Add some ints. */
	unit_ok(zap_add_int_key_by_dnode(dn, 5, 17, tx));
	unit_ok(zap_add_int_key_by_dnode(dn, 23, 35, tx));

	/* Confirm they're there. */
	uint64_t r = 0;
	unit_ok(zap_lookup_int_key_by_dnode(dn, 5, &r));
	unit_eq(r, 17);
	unit_ok(zap_lookup_int_key_by_dnode(dn, 23, &r));
	unit_eq(r, 35);

	/* But not something we didn't add. */
	unit_err(zap_lookup_int_key_by_dnode(dn, 79, &r), ENOENT);

	/* Adding something that already exists fails. */
	unit_err(zap_add_int_key_by_dnode(dn, 23, 51, tx), EEXIST);

	/* Updating it works though. */
	unit_ok(zap_update_int_key_by_dnode(dn, 23, 51, tx));

	/* Removing it works, and then it can't be found. */
	unit_ok(zap_remove_int_by_dnode(dn, 23, tx));
	unit_err(zap_lookup_int_key_by_dnode(dn, 23, &r), ENOENT);

	/* Add it can be added back. */
	unit_ok(zap_add_int_key_by_dnode(dn, 23, 11, tx));
	unit_ok(zap_lookup_int_key_by_dnode(dn, 23, &r));
	unit_eq(r, 11);

	mock_tx_destroy((mock_dmu_tx_t *)tx);
	unit_true(mock_zap_is_params(dn, params, "type"));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

/* ========== */

/*
 * Separate stats tests for each ZAP type, since they are about internals and
 * so can and will produce different results.
 */

static MunitResult
test_microzap_stats(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	dnode_t *dn = mock_zap_create_microzap();
	dmu_tx_t *tx = (dmu_tx_t *)mock_tx_create();

	zap_stats_t zs;
	uint64_t v = 1;
	unit_ok(zap_add_by_dnode(dn, "a", sizeof (uint64_t), 1, &v, tx));
	unit_ok(zap_add_by_dnode(dn, "b", sizeof (uint64_t), 1, &v, tx));
	unit_ok(zap_get_stats_by_dnode(dn, &zs));

	/* We added two entries. */
	unit_eq(zs.zs_num_entries, 2);

	/* MicroZAP is always a single block. */
	unit_eq(zs.zs_num_blocks, 1);

	/* Blocksize matches what we passed to mock_dnode_create(). */
	unit_eq(zs.zs_blocksize, 512);

	mock_tx_destroy((mock_dmu_tx_t *)tx);
	unit_true(mock_zap_is_microzap(dn));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

static MunitResult
test_fatzap_stats(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	dnode_t *dn = mock_zap_create_fatzap();
	dmu_tx_t *tx = (dmu_tx_t *)mock_tx_create();

	zap_stats_t zs;
	uint64_t v = 1;
	unit_ok(zap_add_by_dnode(dn, "a", sizeof (uint64_t), 1, &v, tx));
	unit_ok(zap_add_by_dnode(dn, "b", sizeof (uint64_t), 1, &v, tx));
	unit_ok(zap_get_stats_by_dnode(dn, &zs));

	/* We added two entries. */
	unit_eq(zs.zs_num_entries, 2);

	/* One header block, one leaf block. */
	unit_eq(zs.zs_num_blocks, 2);

	/* FatZAP block size set by tuneable. */
	unit_eq(zs.zs_blocksize, 1 << fzap_default_block_shift);

	mock_tx_destroy((mock_dmu_tx_t *)tx);
	unit_true(mock_zap_is_fatzap(dn));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

/* ========== */

/* Cursor tests. */

/*
 * Basic cursor test. Add a bunch of keys+values to a ZAP, read them back
 * via cursor, confirm they're all there and nothing else is.
 */
static MunitResult
test_cursor(const MunitParameter params[], void *data)
{
	(void) data;

	dnode_t *dn = mock_zap_create_params(params, "type");
	dmu_tx_t *tx = (dmu_tx_t *)mock_tx_create();

	/* For each ASCII letter as key, add a unique value to the ZAP. */
	for (int i = 0; i < 26; i++) {
		char c = (char)i + 'a';
		char k[2] = { c, '\0' };
		uint64_t v = (uint64_t)c * 11;
		unit_ok(zap_add_by_dnode(dn, k, sizeof (uint64_t), 1, &v, tx));
	}

	/* Sanity check; confirm they're all there by count. */
	uint64_t count = 0;
	unit_ok(zap_count_by_dnode(dn, &count));
	unit_eq(count, 26);

	zap_cursor_t zc;
	zap_attribute_t *za = zap_attribute_alloc();

	unit_ok(zap_cursor_init_by_dnode(&zc, dn));

	/*
	 * Cursors don't guarantee an order, so we run over them them all,
	 * confirm the key matches the value, and then set a bit for each
	 * one we've seen. By the end, we should have seen them all.
	 */
	uint64_t seen = 0;
	for (int i = 0; i < 26; i++) {
		unit_ok(zap_cursor_retrieve(&zc, za));

		/* Confirm attribute has the right details for the value. */
		unit_eq(za->za_integer_length, sizeof (uint64_t));
		unit_eq(za->za_num_integers, 1);

		/*
		 * And the right key in za_name. Note that we don't check
		 * za_name_len, which is the length of a buffer that can
		 * definitely hold the key, not the key length itself.
		 */
		char c = za->za_name[0];
		unit_true(c >= 'a' && c <= 'z');
		unit_zero(za->za_name[1]);

		/* Check the value in the attribute. */
		uint64_t v = (uint64_t)c * 11;
		unit_eq(za->za_first_integer, v);

		/*
		 * Also do a direct lookup and confirm the value matches
		 * the value from the attribute.
		 */
		char k[2] = { c, '\0' };
		uint64_t result = 0;
		unit_ok(zap_lookup_by_dnode(dn, k,
		    sizeof (uint64_t), 1, &result));
		unit_eq(result, v);

		/* This one is good, set the bit to remember this fact. */
		seen |= 1 << (c-'a');

		zap_cursor_advance(&zc);
	}

	/* There should be no more keys in the ZAP. */
	unit_err(zap_cursor_retrieve(&zc, za), ENOENT);

	/* Bits 0-25 should be set if we've seen them all. */
	unit_eq(seen, (1 << 26) - 1);

	zap_attribute_free(za);
	zap_cursor_fini(&zc);

	mock_tx_destroy((mock_dmu_tx_t *)tx);
	unit_true(mock_zap_is_params(dn, params, "type"));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

/*
 * Cursor serialize test. Add a bunch of items, use the cursor to read half of
 * them back, then serialize the cursor. Reload the cursor from the serialized
 * state and confirm that we pick up where we left off. Then do it again to
 * ensure it doesn't rely on any internal state.
 */
static MunitResult
test_cursor_serialize(const MunitParameter params[], void *data)
{
	(void) data;

	dnode_t *dn = mock_zap_create_params(params, "type");
	dmu_tx_t *tx = (dmu_tx_t *)mock_tx_create();

	/* For each ASCII letter as key, add a unique value to the ZAP. */
	for (int i = 0; i < 26; i++) {
		char c = (char)i + 'a';
		char k[2] = { c, '\0' };
		uint64_t v = (uint64_t)c * 11;
		unit_ok(zap_add_by_dnode(dn, k, sizeof (uint64_t), 1, &v, tx));
	}

	/* Sanity check; confirm they're all there by count. */
	uint64_t count = 0;
	unit_ok(zap_count_by_dnode(dn, &count));
	unit_eq(count, 26);

	/*
	 * Like test_cursor above, we'll walk over the ZAP and set bits
	 * for each key we see.
	 */
	zap_cursor_t zc;
	zap_attribute_t *za = zap_attribute_alloc();
	uint64_t seen = 0;

	unit_ok(zap_cursor_init_by_dnode(&zc, dn));
	for (int i = 0; i < 13; i++) {
		unit_ok(zap_cursor_retrieve(&zc, za));

		char c = za->za_name[0];
		unit_true(c >= 'a' && c <= 'z');

		/* This one is good, set the bit to remember this fact. */
		seen |= 1 << (c-'a');

		zap_cursor_advance(&zc);
	}

	/* Serialise the and terminate the cursor. */
	uint64_t cookie = zap_cursor_serialize(&zc);
	zap_cursor_fini(&zc);

	/*
	 * Record the bits we saw in the first iteration; we'll use this
	 * when we reload the cursor a second time below.
	 */
	uint64_t orig_seen = seen;

	/* Reinitialise the cursor from the cookie. */
	unit_ok(zap_cursor_init_serialized_by_dnode(&zc, dn, cookie));

	/* Loop over the remaining entries and track them. */
	for (int i = 0; i < 13; i++) {
		unit_ok(zap_cursor_retrieve(&zc, za));

		char c = za->za_name[0];
		unit_true(c >= 'a' && c <= 'z');

		/* This one is good, set the bit to remember this fact. */
		seen |= 1 << (c-'a');

		zap_cursor_advance(&zc);
	}

	/* There should be no more keys in the ZAP. */
	unit_err(zap_cursor_retrieve(&zc, za), ENOENT);

	/* Bits 0-25 should be set if we've seen them all. */
	unit_eq(seen, (1 << 26) - 1);

	/* Cursor done. */
	zap_cursor_fini(&zc);

	/*
	 * Restore the seen state to before when we reinitialised the saved
	 * cursor.
	 */
	seen = orig_seen;

	/*
	 * Do it all again a second time. This is making sure that the saved
	 * cursor is usable even after the its been "used".
	 */
	unit_ok(zap_cursor_init_serialized_by_dnode(&zc, dn, cookie));
	for (int i = 0; i < 13; i++) {
		unit_ok(zap_cursor_retrieve(&zc, za));

		char c = za->za_name[0];
		unit_true(c >= 'a' && c <= 'z');

		seen |= 1 << (c-'a');

		zap_cursor_advance(&zc);
	}

	unit_err(zap_cursor_retrieve(&zc, za), ENOENT);
	unit_eq(seen, (1 << 26) - 1);

	zap_attribute_free(za);
	zap_cursor_fini(&zc);

	mock_tx_destroy((mock_dmu_tx_t *)tx);
	unit_true(mock_zap_is_params(dn, params, "type"));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

/*
 * The following tests confirm that the cursor is properly cleaning up dnode
 * holds taken (or not) across the lifetime of the cursor. The test is not
 * about how or when it takes holds, only that the dnode refcount is the
 * same before zap_cursor_init() as after zap_cursor_fini().
 */
static MunitResult
test_cursor_release_unused(const MunitParameter params[], void *data)
{
	(void) data;

	dnode_t *dn = mock_zap_create_params(params, "type");

	uint64_t refcount = mock_dnode_refcount((mock_dnode_t *)dn);

	zap_cursor_t zc;
	unit_ok(zap_cursor_init_by_dnode(&zc, dn));
	zap_cursor_fini(&zc);

	unit_eq(refcount, mock_dnode_refcount((mock_dnode_t *)dn));

	unit_true(mock_zap_is_params(dn, params, "type"));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

static MunitResult
test_cursor_release_advance(const MunitParameter params[], void *data)
{
	(void) data;

	dnode_t *dn = mock_zap_create_params(params, "type");

	uint64_t refcount = mock_dnode_refcount((mock_dnode_t *)dn);

	zap_cursor_t zc;
	unit_ok(zap_cursor_init_by_dnode(&zc, dn));
	zap_cursor_advance(&zc);
	zap_cursor_fini(&zc);

	unit_eq(refcount, mock_dnode_refcount((mock_dnode_t *)dn));

	unit_true(mock_zap_is_params(dn, params, "type"));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

static MunitResult
test_cursor_release_empty(const MunitParameter params[], void *data)
{
	(void) data;

	dnode_t *dn = mock_zap_create_params(params, "type");

	uint64_t refcount = mock_dnode_refcount((mock_dnode_t *)dn);

	zap_cursor_t zc;
	zap_attribute_t *za = zap_attribute_alloc();

	unit_ok(zap_cursor_init_by_dnode(&zc, dn));
	unit_err(zap_cursor_retrieve(&zc, za), ENOENT);

	zap_attribute_free(za);
	zap_cursor_fini(&zc);

	unit_eq(refcount, mock_dnode_refcount((mock_dnode_t *)dn));

	unit_true(mock_zap_is_params(dn, params, "type"));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

static MunitResult
test_cursor_release_one(const MunitParameter params[], void *data)
{
	(void) data;

	dnode_t *dn = mock_zap_create_params(params, "type");
	dmu_tx_t *tx = (dmu_tx_t *)mock_tx_create();

	uint64_t v = 1;
	unit_ok(zap_add_by_dnode(dn, "a", sizeof (uint64_t), 1, &v, tx));
	unit_ok(zap_add_by_dnode(dn, "b", sizeof (uint64_t), 1, &v, tx));

	uint64_t refcount = mock_dnode_refcount((mock_dnode_t *)dn);

	zap_cursor_t zc;
	zap_attribute_t *za = zap_attribute_alloc();

	unit_ok(zap_cursor_init_by_dnode(&zc, dn));
	unit_ok(zap_cursor_retrieve(&zc, za));

	zap_attribute_free(za);
	zap_cursor_fini(&zc);

	unit_eq(refcount, mock_dnode_refcount((mock_dnode_t *)dn));

	mock_tx_destroy((mock_dmu_tx_t *)tx);
	unit_true(mock_zap_is_params(dn, params, "type"));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

/* ========== */

/* zap_value_search: find key with given uint64 value. */
static MunitResult
test_zap_value_search(const MunitParameter params[], void *data)
{
	(void) data;

	dnode_t *dn = mock_zap_create_params(params, "type");
	dmu_tx_t *tx = (dmu_tx_t *)mock_tx_create();

	/* Add some items. */
	uint64_t v1 = 1, v2 = 2, v3 = 3;
	unit_ok(zap_add_by_dnode(dn, "one", sizeof (uint64_t), 1, &v1, tx));
	unit_ok(zap_add_by_dnode(dn, "two", sizeof (uint64_t), 1, &v2, tx));
	unit_ok(zap_add_by_dnode(dn, "three", sizeof (uint64_t), 1, &v3, tx));

	char name[ZAP_MAXNAMELEN];

	/* Find one of them. */
	unit_ok(zap_value_search_by_dnode(dn, 2, 0, name, sizeof (name)));
	unit_str_eq(name, "two");

	/* Nonexistent value. */
	unit_err(zap_value_search_by_dnode(dn, 10, 0,
	    name, sizeof (name)), ENOENT);

	/* Buffer too small for the key. */
	unit_err(zap_value_search_by_dnode(dn, 3, 0, name, 2), ENAMETOOLONG);

	mock_tx_destroy((mock_dmu_tx_t *)tx);
	unit_true(mock_zap_is_params(dn, params, "type"));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

/* zap_value_search: value masks */
static MunitResult
test_zap_value_search_mask(const MunitParameter params[], void *data)
{
	(void) data;

	dnode_t *dn = mock_zap_create_params(params, "type");
	dmu_tx_t *tx = (dmu_tx_t *)mock_tx_create();

	/*
	 * Add a set of values. These all have the same bottom 16 bits, with
	 * different upper 48 bits, segmented so we can mask them in different
	 * and interesting ways.
	 */
	uint64_t v1 = 0x000000000000f0f0ull;
	uint64_t v2 = 0x00000000fffff0f0ull;
	uint64_t v3 = 0x0000ffff0000f0f0ull;
	uint64_t v4 = 0xffff00000000f0f0ull;

	/*
	 * Generate four random keys. We do this because zap_value_search() is
	 * implemented with a simple cursor walk, so will always return the
	 * first match in hash order, which with fixed keys will always give
	 * exactly the same results. Using random keys ensures the test values
	 * are encountered in different orders between test runs, giving us
	 * better coverage when there are multiple matches.
	 */

	char k1[9], k2[9], k3[9], k4[9];
	unit_rand_str(k1, sizeof (k1));
	unit_rand_str(k2, sizeof (k2));
	unit_rand_str(k3, sizeof (k3));
	unit_rand_str(k4, sizeof (k4));

	unit_ok(zap_add_by_dnode(dn, k1, sizeof (uint64_t), 1, &v1, tx));
	unit_ok(zap_add_by_dnode(dn, k2, sizeof (uint64_t), 1, &v2, tx));
	unit_ok(zap_add_by_dnode(dn, k3, sizeof (uint64_t), 1, &v3, tx));
	unit_ok(zap_add_by_dnode(dn, k4, sizeof (uint64_t), 1, &v4, tx));

	char name[ZAP_MAXNAMELEN];

	/* 0 mask is equivalent to all bits set in mask ie exact match. */
	unit_ok(zap_value_search_by_dnode(dn,
	    0xf0f0, 0, name, sizeof (name)));
	unit_str_eq(name, k1);
	unit_ok(zap_value_search_by_dnode(dn,
	    0xf0f0, 0xffffffffffffffffull, name, sizeof (name)));
	unit_str_eq(name, k1);

	/* Low 16 bits could match any. */
	unit_ok(zap_value_search_by_dnode(dn,
	    0xf0f0, 0xffff, name, sizeof (name)));

	/* Low 32 bits, 3/1 matches. */
	unit_ok(zap_value_search_by_dnode(dn,
	    0x0000f0f0, 0xffffffff, name, sizeof (name)));
	unit_true(strcmp(name, k1) == 0 || strcmp(name, k3) == 0 ||
	    strcmp(name, k4) == 0);
	unit_ok(zap_value_search_by_dnode(dn,
	    0xfffff0f0, 0xffffffff, name, sizeof (name)));
	unit_str_eq(name, k2);

	/* Low 48 bits, 2/1/1 matches */
	unit_ok(zap_value_search_by_dnode(dn,
	    0x00000000f0f0ull, 0xffffffffffffull, name, sizeof (name)));
	unit_true(strcmp(name, k1) == 0 || strcmp(name, k4) == 0);
	unit_ok(zap_value_search_by_dnode(dn,
	    0x0000fffff0f0ull, 0xffffffffffffull, name, sizeof (name)));
	unit_str_eq(name, k2);
	unit_ok(zap_value_search_by_dnode(dn,
	    0xffff0000f0f0ull, 0xffffffffffffull, name, sizeof (name)));
	unit_str_eq(name, k3);

	/* Value doesn't exist directly, but matches when mask applied. */
	unit_ok(zap_value_search_by_dnode(dn,
	    0xffffffff, 0xffff0000, name, sizeof (name)));
	unit_str_eq(name, k2);

	mock_tx_destroy((mock_dmu_tx_t *)tx);
	unit_true(mock_zap_is_params(dn, params, "type"));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

/* ========== */

/* Test suite definition and boilerplate. */

#define	UNIT_PARAM_ZAP_TYPES(p)	\
	UNIT_PARAM((p), "micro", "fat")

static const MunitParameterEnum zap_type_params[] = {
	UNIT_PARAM_ZAP_TYPES("type"),
	{ 0 },
};

#define	UNIT_TEST_ZAP_TYPES(name, func)	\
	UNIT_TEST(name, func, zap_type_params)

static const MunitTest zap_tests[] = {
	UNIT_TEST("mock_microzap_sanity",	test_mock_microzap_sanity),
	UNIT_TEST("mock_fatzap_sanity",		test_mock_fatzap_sanity),

	UNIT_TEST_ZAP_TYPES("zap_basic",	test_zap_basic),

	UNIT_TEST_ZAP_TYPES("zap_add",		test_zap_add),
	UNIT_TEST_ZAP_TYPES("zap_update",	test_zap_update),
	UNIT_TEST_ZAP_TYPES("zap_remove",	test_zap_remove),
	UNIT_TEST_ZAP_TYPES("zap_count",	test_zap_count),
	UNIT_TEST_ZAP_TYPES("zap_contains",	test_zap_contains),
	UNIT_TEST_ZAP_TYPES("zap_length",	test_zap_length),

	UNIT_TEST_ZAP_TYPES("zap_increment",	test_zap_increment),

	UNIT_TEST_ZAP_TYPES("zap_int",		test_zap_int),
	UNIT_TEST_ZAP_TYPES("zap_int_keys",	test_zap_int_keys),

	UNIT_TEST("microzap_stats",		test_microzap_stats),
	UNIT_TEST("fatzap_stats",		test_fatzap_stats),

	UNIT_TEST_ZAP_TYPES("cursor",		test_cursor),
	UNIT_TEST_ZAP_TYPES("cursor_serialize",	test_cursor_serialize),

	UNIT_TEST_ZAP_TYPES(
	    "cursor_release_unused",	test_cursor_release_unused),
	UNIT_TEST_ZAP_TYPES(
	    "cursor_release_advance",	test_cursor_release_advance),
	UNIT_TEST_ZAP_TYPES(
	    "cursor_release_empty",	test_cursor_release_empty),
	UNIT_TEST_ZAP_TYPES(
	    "cursor_release_one",	test_cursor_release_one),

	UNIT_TEST_ZAP_TYPES(
	    "zap_value_search",		test_zap_value_search),
	UNIT_TEST_ZAP_TYPES(
	    "zap_value_search_mask",	test_zap_value_search_mask),

	{ 0 },
};

static const MunitSuite zap_test_suite = {
	"zap.",
	zap_tests,
	NULL,
	1,
	MUNIT_SUITE_OPTION_NONE,
};

int
main(int argc, char **argv)
{
	mock_crc64_init();

	zap_init();

	int rc = munit_suite_main(&zap_test_suite, NULL, argc, argv);

	zap_fini();

	return (rc);
}
