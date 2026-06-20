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

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/btree.h>

#include "unit.h"

/* ========== */

/*
 * The B-Tree stores arbitrary sortable data; these tests use plain uint64_t
 * values.  Elements are kept in sorted order, so a comparison function must
 * return -1, 0, or +1 for less-than, equal, and greater-than.
 */
static int
u64_compare(const void *a, const void *b)
{
	const uint64_t x = *(const uint64_t *)a;
	const uint64_t y = *(const uint64_t *)b;

	if (x < y)
		return (-1);
	if (x > y)
		return (1);
	return (0);
}

/*
 * Create a tree of uint64_t values.
 */
static void
btree_create_u64(zfs_btree_t *bt)
{
	zfs_btree_create(bt, u64_compare, NULL, sizeof (uint64_t));
}

/* ========== */

/* A new tree is empty: no nodes, and nothing to walk. */
static MunitResult
test_btree_empty(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	zfs_btree_t bt;
	zfs_btree_index_t idx;

	btree_create_u64(&bt);
	unit_zero(zfs_btree_numnodes(&bt));
	unit_true(zfs_btree_first(&bt, &idx) == NULL);
	zfs_btree_destroy(&bt);

	return (MUNIT_OK);
}

/* Added values can be found; the count tracks them; absent values are not. */
static MunitResult
test_btree_add_find(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	zfs_btree_t bt;
	zfs_btree_index_t idx;
	btree_create_u64(&bt);

	uint64_t vals[] = { 50, 10, 30 };
	for (size_t i = 0; i < ARRAY_SIZE(vals); i++)
		zfs_btree_add(&bt, &vals[i]);

	/* The tree now holds exactly the values we added. */
	unit_eq(zfs_btree_numnodes(&bt), ARRAY_SIZE(vals));

	/* Each value is found; find() returns a pointer to the stored copy. */
	for (size_t i = 0; i < ARRAY_SIZE(vals); i++) {
		uint64_t *found = zfs_btree_find(&bt, &vals[i], &idx);
		unit_true(found != NULL);
		unit_eq(*found, vals[i]);
	}

	/* A value that was never added is not in the tree. */
	uint64_t absent = 99;
	unit_true(zfs_btree_find(&bt, &absent, &idx) == NULL);

	zfs_btree_destroy(&bt);
	return (MUNIT_OK);
}

/* Removing a value drops it from the tree and lowers the count. */
static MunitResult
test_btree_remove(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	zfs_btree_t bt;
	zfs_btree_index_t idx;
	btree_create_u64(&bt);

	uint64_t vals[] = { 10, 20, 30 };
	for (size_t i = 0; i < ARRAY_SIZE(vals); i++)
		zfs_btree_add(&bt, &vals[i]);

	uint64_t gone = 20;
	zfs_btree_remove(&bt, &gone);

	unit_eq(zfs_btree_numnodes(&bt), 2);
	unit_true(zfs_btree_find(&bt, &gone, &idx) == NULL);

	/* The values we kept are still present. */
	uint64_t keep1 = 10, keep2 = 30;
	unit_true(zfs_btree_find(&bt, &keep1, &idx) != NULL);
	unit_true(zfs_btree_find(&bt, &keep2, &idx) != NULL);

	zfs_btree_destroy(&bt);
	return (MUNIT_OK);
}

/* Values inserted out of order are walked back in ascending order. */
static MunitResult
test_btree_walk(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	zfs_btree_t bt;
	zfs_btree_index_t idx;
	btree_create_u64(&bt);

	uint64_t vals[] = { 50, 10, 40, 20, 30 };
	for (size_t i = 0; i < ARRAY_SIZE(vals); i++)
		zfs_btree_add(&bt, &vals[i]);

	/* first()/next() yield the elements smallest-to-largest. */
	uint64_t prev = 0;
	uint64_t count = 0;
	for (uint64_t *p = zfs_btree_first(&bt, &idx); p != NULL;
	    p = zfs_btree_next(&bt, &idx, &idx)) {
		unit_gt(*p, prev);	/* strictly increasing */
		prev = *p;
		count++;
	}
	unit_eq(count, ARRAY_SIZE(vals));

	zfs_btree_destroy(&bt);
	return (MUNIT_OK);
}

/* ========== */

static const MunitTest btree_tests[] = {
	UNIT_TEST("empty",	test_btree_empty),
	UNIT_TEST("add_find",	test_btree_add_find),
	UNIT_TEST("remove",	test_btree_remove),
	UNIT_TEST("walk",	test_btree_walk),
	{ 0 },
};

static const MunitSuite btree_test_suite = {
	"btree.",
	btree_tests,
	NULL,
	1,
	MUNIT_SUITE_OPTION_NONE,
};

int
main(int argc, char **argv)
{
	zfs_btree_init();
	int ret = munit_suite_main(&btree_test_suite, NULL, argc, argv);
	zfs_btree_fini();
	return (ret);
}
