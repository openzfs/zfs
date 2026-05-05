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
	mock_dnode_destroy((mock_dnode_t *)dn);
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

/* Test suite definition and boilerplate. */

#define	UNIT_PARAM_ZAP_TYPES(p)	\
	UNIT_PARAM((p), "micro", "fat")

static const MunitParameterEnum zap_type_params[] = {
	UNIT_PARAM_ZAP_TYPES("type"),
	{ 0 },
};

static const MunitTest zap_tests[] = {
	UNIT_TEST("mock_microzap_sanity",	test_mock_microzap_sanity),
	UNIT_TEST("mock_fatzap_sanity",		test_mock_fatzap_sanity),

	UNIT_TEST("zap_basic",	test_zap_basic,	zap_type_params),

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
