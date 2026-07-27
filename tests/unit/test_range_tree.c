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
 * Copyright (c) 2026, James Hilliard.
 */

#include <sys/btree.h>
#include <sys/range_tree.h>

#include "unit.h"

static zfs_range_tree_t *
range_tree_create(void)
{
	return (zfs_range_tree_create(NULL, ZFS_RANGE_SEG64, NULL, 0, 0));
}

static void
range_tree_destroy(zfs_range_tree_t *rt)
{
	zfs_range_tree_vacate(rt, NULL, NULL);
	zfs_range_tree_destroy(rt);
}

static MunitResult
test_try_add(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	zfs_range_tree_t *rt = range_tree_create();

	unit_true(zfs_range_tree_try_add(rt, 0x1000, 0x1000));
	unit_true(zfs_range_tree_try_add(rt, 0x3000, 0x1000));
	unit_true(zfs_range_tree_try_add(rt, 0x2000, 0x1000));
	unit_eq(zfs_range_tree_numsegs(rt), 1);
	unit_eq(zfs_range_tree_space(rt), 0x3000);
	unit_true(zfs_range_tree_contains(rt, 0x1000, 0x3000));

	range_tree_destroy(rt);
	return (MUNIT_OK);
}

static MunitResult
test_try_add_overlap(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	zfs_range_tree_t *rt = range_tree_create();

	zfs_range_tree_add(rt, 0x1000, 0x2000);
	zfs_range_tree_add(rt, 0x5000, 0x1000);

	unit_false(zfs_range_tree_try_add(rt, 0x1000, 0x1000));
	unit_false(zfs_range_tree_try_add(rt, 0x2800, 0x1000));
	unit_eq(zfs_range_tree_numsegs(rt), 2);
	unit_eq(zfs_range_tree_space(rt), 0x3000);
	unit_true(zfs_range_tree_contains(rt, 0x1000, 0x2000));
	unit_true(zfs_range_tree_contains(rt, 0x5000, 0x1000));
	unit_false(zfs_range_tree_contains(rt, 0x3000, 0x2000));

	range_tree_destroy(rt);
	return (MUNIT_OK);
}

static MunitResult
test_try_remove(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	zfs_range_tree_t *rt = range_tree_create();

	zfs_range_tree_add(rt, 0x1000, 0x3000);
	unit_true(zfs_range_tree_try_remove(rt, 0x2000, 0x1000));
	unit_eq(zfs_range_tree_numsegs(rt), 2);
	unit_eq(zfs_range_tree_space(rt), 0x2000);
	unit_true(zfs_range_tree_contains(rt, 0x1000, 0x1000));
	unit_true(zfs_range_tree_contains(rt, 0x3000, 0x1000));
	unit_false(zfs_range_tree_contains(rt, 0x2000, 0x1000));

	unit_true(zfs_range_tree_try_remove(rt, 0x1000, 0x1000));
	unit_true(zfs_range_tree_try_remove(rt, 0x3000, 0x1000));
	unit_true(zfs_range_tree_is_empty(rt));

	range_tree_destroy(rt);
	return (MUNIT_OK);
}

static MunitResult
test_try_remove_missing(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	zfs_range_tree_t *rt = range_tree_create();

	zfs_range_tree_add(rt, 0x1000, 0x2000);
	zfs_range_tree_add(rt, 0x5000, 0x1000);

	unit_false(zfs_range_tree_try_remove(rt, 0x3000, 0x1000));
	unit_false(zfs_range_tree_try_remove(rt, 0x2800, 0x3000));
	unit_false(zfs_range_tree_try_remove(rt, 0x1000, 0x4000));
	unit_eq(zfs_range_tree_numsegs(rt), 2);
	unit_eq(zfs_range_tree_space(rt), 0x3000);
	unit_true(zfs_range_tree_contains(rt, 0x1000, 0x2000));
	unit_true(zfs_range_tree_contains(rt, 0x5000, 0x1000));
	unit_false(zfs_range_tree_contains(rt, 0x3000, 0x2000));

	range_tree_destroy(rt);
	return (MUNIT_OK);
}

static const MunitTest range_tree_tests[] = {
	UNIT_TEST("try_add",		test_try_add),
	UNIT_TEST("try_add_overlap",	test_try_add_overlap),
	UNIT_TEST("try_remove",		test_try_remove),
	UNIT_TEST("try_remove_missing",	test_try_remove_missing),
	{ 0 },
};

static const MunitSuite range_tree_test_suite = {
	"range_tree.",
	range_tree_tests,
	NULL,
	1,
	MUNIT_SUITE_OPTION_NONE,
};

int
main(int argc, char **argv)
{
	int ret;

	zfs_btree_init();
	ret = munit_suite_main(&range_tree_test_suite, NULL, argc, argv);
	zfs_btree_fini();
	return (ret);
}
