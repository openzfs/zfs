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

static MunitResult
test_try_add_gap(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	zfs_range_tree_t *rt = zfs_range_tree_create_gap(NULL,
	    ZFS_RANGE_SEG_GAP, NULL, 0, 0, 0x1000);

	unit_false(zfs_range_tree_try_add(rt, 0x1000, 0x1000));
	unit_true(zfs_range_tree_is_empty(rt));
	zfs_range_tree_add(rt, 0x1000, 0x1000);
	zfs_range_tree_add(rt, 0x3000, 0x1000);

	unit_false(zfs_range_tree_try_add(rt, 0x2000, 0x1000));
	unit_false(zfs_range_tree_try_add(rt, 0x4000, 0x1000));
	unit_false(zfs_range_tree_try_add(rt, 0x6000, 0x1000));
	unit_eq(zfs_range_tree_numsegs(rt), 1);
	unit_eq(zfs_range_tree_space(rt), 0x3000);
	zfs_range_seg_t *rs = zfs_range_tree_find(rt, 0x1000, 0x3000);
	unit_notnull(rs);
	unit_eq(zfs_rs_get_start(rs, rt), 0x1000);
	unit_eq(zfs_rs_get_end(rs, rt), 0x4000);
	unit_eq(zfs_rs_get_fill(rs, rt), 0x2000);
	zfs_range_tree_stat_verify(rt);

	range_tree_destroy(rt);
	return (MUNIT_OK);
}

static MunitResult
test_try_remove_gap(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	zfs_range_tree_t *rt = zfs_range_tree_create_gap(NULL,
	    ZFS_RANGE_SEG_GAP, NULL, 0, 0, 0x1000);

	unit_false(zfs_range_tree_try_remove(rt, 0x1000, 0x1000));
	zfs_range_tree_add(rt, 0x1000, 0x1000);
	zfs_range_tree_add(rt, 0x3000, 0x1000);

	unit_false(zfs_range_tree_try_remove(rt, 0x1000, 0x1000));
	unit_false(zfs_range_tree_try_remove(rt, 0x1000, 0x3000));
	unit_false(zfs_range_tree_try_remove(rt, 0x6000, 0x1000));
	unit_eq(zfs_range_tree_numsegs(rt), 1);
	unit_eq(zfs_range_tree_space(rt), 0x3000);
	zfs_range_seg_t *rs = zfs_range_tree_find(rt, 0x1000, 0x3000);
	unit_notnull(rs);
	unit_eq(zfs_rs_get_start(rs, rt), 0x1000);
	unit_eq(zfs_rs_get_end(rs, rt), 0x4000);
	unit_eq(zfs_rs_get_fill(rs, rt), 0x2000);
	zfs_range_tree_stat_verify(rt);

	range_tree_destroy(rt);
	return (MUNIT_OK);
}

static void
check_invalid_ranges(boolean_t (*try_range)(zfs_range_tree_t *, uint64_t,
    uint64_t))
{
	const struct {
		zfs_range_seg_type_t type;
		uint64_t start;
		uint64_t shift;
	} layouts[] = {
		{ ZFS_RANGE_SEG32, 0, 0 },
		{ ZFS_RANGE_SEG64, 0, 0 },
		{ ZFS_RANGE_SEG32, 0x1000, 9 },
		{ ZFS_RANGE_SEG64, 0x1000, 9 }
	};

	for (size_t i = 0; i < ARRAY_SIZE(layouts); i++) {
		zfs_range_tree_t *rt = zfs_range_tree_create(NULL,
		    layouts[i].type, NULL, layouts[i].start, layouts[i].shift);
		uint64_t unit = 1ULL << rt->rt_shift;
		uint64_t end = rt->rt_type == ZFS_RANGE_SEG32 ?
		    rt->rt_start + ((uint64_t)UINT32_MAX << rt->rt_shift) :
		    P2ALIGN_TYPED(UINT64_MAX, unit, uint64_t);

		zfs_range_tree_add(rt, 0x2000, 0x2000);
		unit_false(try_range(rt, 0x2000, 0));
		unit_false(try_range(rt, UINT64_MAX, 1));
		unit_false(try_range(rt, UINT64_MAX - 0x1ff, 0x200));
		if (rt->rt_start != 0)
			unit_false(try_range(rt, 0, unit));
		if (rt->rt_shift != 0) {
			unit_false(try_range(rt, 0x2001, unit));
			unit_false(try_range(rt, 0x2000, unit + 1));
		}
		if (rt->rt_type == ZFS_RANGE_SEG32) {
			unit_false(try_range(rt, end, unit));
			unit_false(try_range(rt, end + unit, unit));
		}
		unit_eq(zfs_range_tree_numsegs(rt), 1);
		unit_eq(zfs_range_tree_space(rt), 0x2000);
		unit_true(zfs_range_tree_contains(rt, 0x2000, 0x2000));
		zfs_range_tree_stat_verify(rt);

		/* The largest representable end is still valid. */
		unit_true(zfs_range_tree_try_add(rt, end - unit, unit));
		unit_true(zfs_range_tree_try_remove(rt, end - unit, unit));
		unit_eq(zfs_range_tree_numsegs(rt), 1);
		unit_eq(zfs_range_tree_space(rt), 0x2000);
		zfs_range_tree_stat_verify(rt);
		range_tree_destroy(rt);
	}
}

static MunitResult
test_try_add_invalid(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	check_invalid_ranges(zfs_range_tree_try_add);
	return (MUNIT_OK);
}

static MunitResult
test_try_remove_invalid(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	check_invalid_ranges(zfs_range_tree_try_remove);
	return (MUNIT_OK);
}

static const MunitTest range_tree_tests[] = {
	UNIT_TEST("try_add",		test_try_add),
	UNIT_TEST("try_add_overlap",	test_try_add_overlap),
	UNIT_TEST("try_add_gap",	test_try_add_gap),
	UNIT_TEST("try_add_invalid",	test_try_add_invalid),
	UNIT_TEST("try_remove",		test_try_remove),
	UNIT_TEST("try_remove_missing",	test_try_remove_missing),
	UNIT_TEST("try_remove_gap",	test_try_remove_gap),
	UNIT_TEST("try_remove_invalid",	test_try_remove_invalid),
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
