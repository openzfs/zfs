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
 * Copyright (c) 2026, Tigerblue77.
 */

/*
 * metaslab_class_balance() gives each metaslab group of a class a quota of
 * allocated bytes per rotation of the class rotor, mg_aliquot, and sets the
 * allocation throttle threshold of the class, mc_alloc_max, to 4 rotations.
 * These tests create pools of file vdevs with libzpool and check that each
 * child of a RAID-Z group gets metaslab_aliquot per rotation, parity
 * included, and that mc_alloc_max counts logical bytes, as the throttle
 * reservations it limits do.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/metaslab_impl.h>
#include <sys/vdev_impl.h>
#include <sys/fs/zfs.h>
#include <libzpool.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "unit.h"

/* ========== */

#define	POOL		"unit_metaslab_balance"
#define	ALIQUOT		(2ULL << 20)
#define	FILE_SIZE	(256ULL << 20)
#define	ASHIFT		12
#define	MAX_GROUPS	2
#define	MAX_FILES	12

typedef struct {
	const char	*g_type;	/* raidz or mirror */
	uint64_t	g_nparity;	/* RAID-Z only */
	uint_t		g_children;
} group_t;

static char tmpdir[MAXPATHLEN];
static char cachefile[MAXPATHLEN];
static char path[MAXPATHLEN];
static uint_t nfiles;

static const char *
file_path(uint_t f)
{
	(void) snprintf(path, sizeof (path), "%s/f%u", tmpdir, f);
	return (path);
}

static nvlist_t *
make_file(void)
{
	nvlist_t *nv;
	int fd;

	munit_assert_uint(nfiles, <, MAX_FILES);
	fd = open(file_path(nfiles++), O_RDWR | O_CREAT | O_TRUNC, 0600);
	munit_assert_int(fd, >=, 0);
	unit_ok(ftruncate(fd, FILE_SIZE));
	(void) close(fd);

	nv = fnvlist_alloc();
	fnvlist_add_string(nv, ZPOOL_CONFIG_TYPE, VDEV_TYPE_FILE);
	fnvlist_add_string(nv, ZPOOL_CONFIG_PATH, file_path(nfiles - 1));
	fnvlist_add_uint64(nv, ZPOOL_CONFIG_ASHIFT, ASHIFT);

	return (nv);
}

/*
 * Create the pool with a fixed metaslab_aliquot, and without the free space
 * bias, which the free space of each group would make vary.
 */
static spa_t *
pool_create(const group_t *groups, uint_t ngroups)
{
	nvlist_t *top[MAX_GROUPS];
	nvlist_t *leaf[MAX_FILES];
	nvlist_t *root;
	spa_t *spa;

	const char *t = getenv("TMPDIR");
	(void) snprintf(tmpdir, sizeof (tmpdir), "%s/metaslab_balance.XXXXXX",
	    t != NULL ? t : "/tmp");
	unit_notnull(mkdtemp(tmpdir));
	(void) snprintf(cachefile, sizeof (cachefile), "%s/zpool.cache",
	    tmpdir);
	spa_config_path = cachefile;
	nfiles = 0;

	unit_ok(handle_tunable_option("metaslab_aliquot=2097152", B_TRUE));
	unit_ok(handle_tunable_option("metaslab_bias_enabled=0", B_TRUE));
	kernel_init(SPA_MODE_READ | SPA_MODE_WRITE);

	munit_assert_uint(ngroups, <=, MAX_GROUPS);
	for (uint_t g = 0; g < ngroups; g++) {
		const group_t *gp = &groups[g];

		munit_assert_uint(gp->g_children, <=, MAX_FILES);
		for (uint_t c = 0; c < gp->g_children; c++)
			leaf[c] = make_file();

		top[g] = fnvlist_alloc();
		fnvlist_add_string(top[g], ZPOOL_CONFIG_TYPE, gp->g_type);
		if (strcmp(gp->g_type, VDEV_TYPE_RAIDZ) == 0) {
			fnvlist_add_uint64(top[g], ZPOOL_CONFIG_NPARITY,
			    gp->g_nparity);
		}
		fnvlist_add_uint64(top[g], ZPOOL_CONFIG_IS_LOG, 0);
		fnvlist_add_nvlist_array(top[g], ZPOOL_CONFIG_CHILDREN,
		    (const nvlist_t **)leaf, gp->g_children);
		for (uint_t c = 0; c < gp->g_children; c++)
			fnvlist_free(leaf[c]);
	}

	root = fnvlist_alloc();
	fnvlist_add_string(root, ZPOOL_CONFIG_TYPE, VDEV_TYPE_ROOT);
	fnvlist_add_nvlist_array(root, ZPOOL_CONFIG_CHILDREN,
	    (const nvlist_t **)top, ngroups);
	for (uint_t g = 0; g < ngroups; g++)
		fnvlist_free(top[g]);

	unit_ok(spa_create(POOL, root, NULL, NULL, NULL, NULL));
	fnvlist_free(root);
	unit_ok(spa_open(POOL, &spa, FTAG));

	return (spa);
}

static void
pool_destroy(spa_t *spa)
{
	spa_close(spa, FTAG);
	unit_ok(spa_destroy(POOL));
	kernel_fini();

	for (uint_t f = 0; f < nfiles; f++)
		(void) unlink(file_path(f));
	(void) unlink(cachefile);
	(void) rmdir(tmpdir);
}

/*
 * Balance the normal class again.  With check_children, check that each
 * child of each group gets metaslab_aliquot per rotation, less the rounding
 * of the capacity scale, as the groups have the same size.  Then check that
 * mc_alloc_max is 4 rotations in logical bytes: the share of each quota
 * that the data children of its group store.
 */
static void
pool_balance(spa_t *spa, boolean_t check_children)
{
	metaslab_class_t *mc = spa_normal_class(spa);
	metaslab_group_t *mg, *first;
	uint64_t logical = 0;

	spa_config_enter(spa, SCL_ALLOC, FTAG, RW_READER);
	metaslab_class_balance(mc, B_FALSE);

	mg = first = mc->mc_allocator[0].mca_rotor;
	do {
		vdev_t *vd = mg->mg_vd;
		uint64_t ndisks = vdev_get_ndisks(vd);
		uint64_t ndata = ndisks - vdev_get_nparity(vd);

		if (check_children) {
			unit_le(mg->mg_aliquot / ndisks, ALIQUOT);
			unit_ge(mg->mg_aliquot / ndisks, ALIQUOT * 99 / 100);
		}
		logical += mg->mg_aliquot / ndisks * ndata;
	} while ((mg = mg->mg_next) != first);

	uint64_t alloc_max = mc->mc_alloc_max;
	spa_config_exit(spa, SCL_ALLOC, FTAG);

	unit_ge(alloc_max, logical * 4 * 99 / 100);
	unit_le(alloc_max, logical * 4 * 101 / 100);
}

/* ========== */

/*
 * Two 6-wide RAID-Z2 groups: each of their 6 children, parity included,
 * gets metaslab_aliquot per rotation, and the throttle threshold is 4
 * rotations of the 4 data children.
 */
static MunitResult
test_raidz_children(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	const group_t groups[] = {
		{ VDEV_TYPE_RAIDZ, 2, 6 },
		{ VDEV_TYPE_RAIDZ, 2, 6 },
	};
	spa_t *spa = pool_create(groups, ARRAY_SIZE(groups));

	pool_balance(spa, B_TRUE);

	pool_destroy(spa);

	return (MUNIT_OK);
}

/*
 * A 6-wide RAID-Z2 group beside a mirror, which stores what it allocates:
 * the throttle threshold counts the RAID-Z2 quota in logical bytes and the
 * mirror quota as it is.
 */
static MunitResult
test_mixed_alloc_max(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	const group_t groups[] = {
		{ VDEV_TYPE_RAIDZ, 2, 6 },
		{ VDEV_TYPE_MIRROR, 0, 2 },
	};
	spa_t *spa = pool_create(groups, ARRAY_SIZE(groups));

	pool_balance(spa, B_FALSE);

	pool_destroy(spa);

	return (MUNIT_OK);
}

/* ========== */

static const MunitTest metaslab_balance_tests[] = {
	UNIT_TEST("raidz_children",	test_raidz_children),
	UNIT_TEST("mixed_alloc_max",	test_mixed_alloc_max),
	{ 0 },
};

static const MunitSuite metaslab_balance_test_suite = {
	"metaslab_balance.",
	metaslab_balance_tests,
	NULL,
	1,
	MUNIT_SUITE_OPTION_NONE,
};

int
main(int argc, char **argv)
{
	return (munit_suite_main(&metaslab_balance_test_suite, NULL, argc,
	    argv));
}
