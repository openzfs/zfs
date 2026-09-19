// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source. A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */

#include <sys/zfs_context.h>
#include <sys/vdev_impl.h>

#include "munit.h"

static MunitResult
test_resilver_bounds(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	const struct {
		uint64_t start;
		uint64_t size;
		uint64_t min;
		uint64_t max;
	} cases[] = {
		{ 0, 16, 0, 16 },
		{ 1, 15, 0, 16 },
		{ 4, 12, 3, 16 },
		{ 4, 1, 3, 5 },
	};
	vdev_t *vd = kmem_zalloc(sizeof (*vd), KM_SLEEP);
	vd->vdev_ops = &vdev_file_ops;
	vd->vdev_state = VDEV_STATE_HEALTHY;
	mutex_init(&vd->vdev_dtl_lock, NULL, MUTEX_DEFAULT, NULL);
	vd->vdev_dtl[DTL_MISSING] = zfs_range_tree_create(NULL,
	    ZFS_RANGE_SEG64, NULL, 0, 0);

	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		uint64_t min = UINT64_MAX, max = UINT64_MAX;
		zfs_range_tree_add(vd->vdev_dtl[DTL_MISSING],
		    cases[i].start, cases[i].size);
		munit_assert_true(vdev_resilver_needed(vd, &min, &max));
		munit_assert_uint64(min, ==, cases[i].min);
		munit_assert_uint64(max, ==, cases[i].max);
		munit_assert_true(vdev_resilver_needed(vd, NULL, NULL));
		zfs_range_tree_vacate(vd->vdev_dtl[DTL_MISSING], NULL, NULL);
	}

	zfs_range_tree_destroy(vd->vdev_dtl[DTL_MISSING]);
	mutex_destroy(&vd->vdev_dtl_lock);
	kmem_free(vd, sizeof (*vd));
	return (MUNIT_OK);
}

static MunitTest vdev_tests[] = {
	{ "resilver_bounds", test_resilver_bounds, NULL, NULL,
	    MUNIT_TEST_OPTION_NONE, NULL },
	{ 0 },
};

static const MunitSuite vdev_test_suite = {
	"vdev.", vdev_tests, NULL, 1, MUNIT_SUITE_OPTION_NONE,
};

int
main(int argc, char **argv)
{
	zfs_btree_init();
	int ret = munit_suite_main(&vdev_test_suite, NULL, argc, argv);
	zfs_btree_fini();
	return (ret);
}
