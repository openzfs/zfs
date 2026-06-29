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

#include <stddef.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/avl.h>
#include <sys/btree.h>

#include "unit.h"

#define	DRAIN_COUNT	(64 * 1024)

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

	return (TREE_CMP(x, y));
}

/*
 * Create a tree of uint64_t values.
 */
static void
btree_create_u64(zfs_btree_t *bt)
{
	zfs_btree_create(bt, u64_compare, NULL, sizeof (uint64_t));
}

/*
 * Cross-check the B-Tree against an AVL tree holding the same
 * values, used as reference.
 */
typedef struct int_node {
	avl_node_t node;
	uint64_t data;
} int_node_t;

static int
avl_u64_compare(const void *v1, const void *v2)
{
	const int_node_t *n1 = v1;
	const int_node_t *n2 = v2;

	return (TREE_CMP(n1->data, n2->data));
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
	zfs_btree_clear(&bt);
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

	zfs_btree_clear(&bt);
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

	zfs_btree_clear(&bt);
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

	zfs_btree_clear(&bt);
	zfs_btree_destroy(&bt);
	return (MUNIT_OK);
}

/* Verify that zfs_btree_find() works correctly when passed a NULL index. */
static MunitResult
test_btree_find_without_index(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	zfs_btree_t bt;
	btree_create_u64(&bt);

	uint64_t i = 12345;
	zfs_btree_add(&bt, &i);

	uint64_t *p = zfs_btree_find(&bt, &i, NULL);
	unit_true(p != NULL);
	unit_eq(*p, i);

	uint64_t absent = i + 1;
	unit_true(zfs_btree_find(&bt, &absent, NULL) == NULL);

	zfs_btree_clear(&bt);
	zfs_btree_destroy(&bt);
	return (MUNIT_OK);
}

/*
 * Fill a B-Tree and an AVL tree with the same random values, then drain
 * both from alternating ends, checking they stay identical at every step.
 */
static MunitResult
test_btree_drain(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	zfs_btree_t bt;
	avl_tree_t avl;
	zfs_btree_index_t bt_idx = {0};
	avl_index_t avl_idx = {0};
	int_node_t *node;

	btree_create_u64(&bt);
	avl_create(&avl, avl_u64_compare, sizeof (int_node_t),
	    offsetof(int_node_t, node));

	/* Fill both trees with the same data. */
	for (int i = 0; i < DRAIN_COUNT; i++) {
		uint64_t randval = unit_rand_uint64();
		if (zfs_btree_find(&bt, &randval, &bt_idx) != NULL)
			continue;
		zfs_btree_add_idx(&bt, &randval, &bt_idx);

		node = malloc(sizeof (int_node_t));
		unit_true(node != NULL);
		node->data = randval;

		/* New to the btree, so the avl must not have it either. */
		unit_true(avl_find(&avl, node, &avl_idx) == NULL);
		avl_insert(&avl, node, avl_idx);
	}

	/* Remove from alternating ends, comparing the trees as we go. */
	while (avl_numnodes(&avl) != 0) {
		uint64_t *bt_data;

		unit_eq(zfs_btree_numnodes(&bt), avl_numnodes(&avl));
		if (avl_numnodes(&avl) % 2 == 0) {
			node = avl_first(&avl);
			bt_data = zfs_btree_first(&bt, &bt_idx);
		} else {
			node = avl_last(&avl);
			bt_data = zfs_btree_last(&bt, &bt_idx);
		}
		unit_eq(*bt_data, node->data);
		zfs_btree_remove_idx(&bt, &bt_idx);
		avl_remove(&avl, node);
		free(node);

		if (avl_numnodes(&avl) == 0)
			break;

		/* Both ends still agree after the removal. */
		uint64_t *bt_lo = zfs_btree_first(&bt, NULL);
		uint64_t *bt_hi = zfs_btree_last(&bt, NULL);
		int_node_t *avl_lo = avl_first(&avl);
		int_node_t *avl_hi = avl_last(&avl);
		unit_eq(*bt_lo, avl_lo->data);
		unit_eq(*bt_hi, avl_hi->data);
	}
	unit_zero(zfs_btree_numnodes(&bt));

	avl_destroy(&avl);
	zfs_btree_clear(&bt);
	zfs_btree_destroy(&bt);
	return (MUNIT_OK);
}

/* ========== */

static const MunitTest btree_tests[] = {
	UNIT_TEST("empty",		test_btree_empty),
	UNIT_TEST("add_find",		test_btree_add_find),
	UNIT_TEST("remove",		test_btree_remove),
	UNIT_TEST("walk",		test_btree_walk),
	UNIT_TEST("find_without_index",	test_btree_find_without_index),
	UNIT_TEST("drain",		test_btree_drain),
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
