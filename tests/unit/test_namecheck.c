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

#include <string.h>

#include <sys/fs/zfs.h>
#include "zfs_namecheck.h"

#include "unit.h"

/* ========== */

/*
 * The namecheck routines validate a name and report, via namecheck_err_t,
 * exactly why it failed.  We test them in two directions:
 *
 *   - Validity path: randomly generated names from unit_rand_str(), which
 *     give only 'a'-'z' characters.
 *
 *   - Invalidity path: explicit names, each tested against their specific
 *     error code since the rejection reason depends on the exact characters
 *     used.
 */
typedef int (*namecheck_f)(const char *, namecheck_err_t *, char *);

/* Confirm 'name' is accepted by 'fn'. */
static void
check_valid(namecheck_f fn, const char *name)
{
	namecheck_err_t why = (namecheck_err_t)-1;
	char what = '\0';
	unit_ok(fn(name, &why, &what));
}

/* Confirm 'name' is rejected by 'fn' with the 'why' we expected. */
static void
check_invalid(namecheck_f fn, const char *name, namecheck_err_t expected)
{
	namecheck_err_t why = (namecheck_err_t)-1;
	char what = '\0';
	unit_err(fn(name, &why, &what), -1);
	unit_eq(why, expected);
}

/* Confirm 'fn' rejects a lengthy name and returns NAME_ERR_TOOLONG. */
static void
check_longname_invalid(namecheck_f fn)
{
	char buf[ZFS_MAX_DATASET_NAME_LEN + 16];
	check_invalid(fn, unit_rand_str(buf, sizeof (buf)), NAME_ERR_TOOLONG);
}

/* ========== */

/* pool_namecheck: dataset character set that must begin with a letter. */
static MunitResult
test_pool_namecheck(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	/* A random array of letters is always a valid pool name. */
	char pool[16];
	check_valid(pool_namecheck, unit_rand_str(pool, sizeof (pool)));

	/* Fixed names cover the rest of the allowed character set. */
	check_valid(pool_namecheck, "tank_01");
	check_valid(pool_namecheck, "Pool-2.0:label");

	/* A pool name has to start with a letter. */
	check_invalid(pool_namecheck, "0tank", NAME_ERR_NOLETTER);
	check_invalid(pool_namecheck, "_tank", NAME_ERR_NOLETTER);

	/* These pool names are reserved. */
	check_invalid(pool_namecheck, "mirror", NAME_ERR_RESERVED);
	check_invalid(pool_namecheck, "raidz", NAME_ERR_RESERVED);
	check_invalid(pool_namecheck, "draid", NAME_ERR_RESERVED);

	/* A pool name carries no hierarchy or snapshot delimiter. */
	check_invalid(pool_namecheck, "tank/fs", NAME_ERR_INVALCHAR);
	check_invalid(pool_namecheck, "tank@snap", NAME_ERR_INVALCHAR);

	check_longname_invalid(pool_namecheck);

	return (MUNIT_OK);
}

/* dataset_namecheck: any entity except a bookmark. */
static MunitResult
test_dataset_namecheck(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	/* A path of random, independently-valid components is accepted. */
	char path[64];
	unit_rand_str(path, sizeof (path));
	path[20] = path[40] = '/';
	check_valid(dataset_namecheck, path);

	/* A trailing snapshot is still a valid dataset name. */
	check_valid(dataset_namecheck, "tank/home@snap");

	/* '%' is allowed, for temporary clone names (online recv). */
	check_valid(dataset_namecheck, "tank/%recv");

	check_invalid(dataset_namecheck, "/tank", NAME_ERR_LEADING_SLASH);
	check_invalid(dataset_namecheck, "tank/", NAME_ERR_TRAILING_SLASH);
	check_invalid(dataset_namecheck, "tank//home",
	    NAME_ERR_EMPTY_COMPONENT);
	check_invalid(dataset_namecheck, "", NAME_ERR_EMPTY_COMPONENT);
	check_invalid(dataset_namecheck, "tank/./home", NAME_ERR_SELF_REF);
	check_invalid(dataset_namecheck, "tank/../home", NAME_ERR_PARENT_REF);
	check_invalid(dataset_namecheck, "tank/fs!", NAME_ERR_INVALCHAR);

	/* A bookmark delimiter does not belong in a dataset name. */
	check_invalid(dataset_namecheck, "tank/fs#bm", NAME_ERR_INVALCHAR);

	check_longname_invalid(dataset_namecheck);

	return (MUNIT_OK);
}

/* snapshot_namecheck: a valid snapshot name (an entity with '@'). */
static MunitResult
test_snapshot_namecheck(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	/* A random "filesystem@snapshot" pair is valid. */
	char path[64];
	unit_rand_str(path, sizeof (path));
	path[40] = '@';
	check_valid(snapshot_namecheck, path);

	/* Without an '@' it is not a snapshot. */
	check_invalid(snapshot_namecheck, "tank/home", NAME_ERR_NO_AT);

	/* Only one delimiter is allowed. */
	check_invalid(snapshot_namecheck, "tank@a@b",
	    NAME_ERR_MULTIPLE_DELIMITERS);

	/* Nothing may follow the snapshot name with a '/'. */
	check_invalid(snapshot_namecheck, "tank@snap/x",
	    NAME_ERR_TRAILING_SLASH);

	return (MUNIT_OK);
}

/* bookmark_namecheck: a valid bookmark name (an entity with '#'). */
static MunitResult
test_bookmark_namecheck(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	/* A random "filesystem#bookmark" pair is valid. */
	char path[64];
	unit_rand_str(path, sizeof (path));
	path[40] = '#';
	check_valid(bookmark_namecheck, path);

	/* Without a '#' it is not a bookmark. */
	check_invalid(bookmark_namecheck, "tank/home", NAME_ERR_NO_POUND);

	return (MUNIT_OK);
}

/* zfs_component_namecheck: one component; alphanumeric plus [-_.: ]. */
static MunitResult
test_component_namecheck(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	/* A bare random component is valid. */
	char comp[16];
	unit_rand_str(comp, sizeof (comp));
	check_valid(zfs_component_namecheck, comp);

	/* An empty component is not valid. */
	check_invalid(zfs_component_namecheck, "", NAME_ERR_EMPTY_COMPONENT);

	/* A single component cannot contain a path separator. */
	check_invalid(zfs_component_namecheck, "a/b", NAME_ERR_INVALCHAR);

	check_longname_invalid(zfs_component_namecheck);

	return (MUNIT_OK);
}

/* permset_namecheck: a permission set name, starting with '@'. */
static MunitResult
test_permset_namecheck(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	/* A random name behind a leading '@' is a valid permission set. */
	char set[16];
	set[0] = '@';
	unit_rand_str(set + 1, sizeof (set) - 1);
	check_valid(permset_namecheck, set);

	/* It has to start with '@'. */
	check_invalid(permset_namecheck, "backup", NAME_ERR_NO_AT);

	/* The text after '@' follows the component rules. */
	check_invalid(permset_namecheck, "@bad/name", NAME_ERR_INVALCHAR);

	/* The length upper limit is checked ahead of everything else. */
	check_longname_invalid(permset_namecheck);

	return (MUNIT_OK);
}

/* mountpoint_namecheck: a mountpoint path, /[component][/]*. */
static MunitResult
test_mountpoint_namecheck(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	namecheck_err_t why = (namecheck_err_t)-1;

	/* An absolute path with a random component is accepted. */
	char path[64];
	unit_rand_str(path, sizeof (path));
	path[0] = '/';
	unit_ok(mountpoint_namecheck(path, &why));

	/* The root mountpoint is valid. */
	unit_ok(mountpoint_namecheck("/", &why));

	/* A mountpoint must be absolute. */
	unit_err(mountpoint_namecheck("relative/path", &why), -1);
	unit_eq(why, NAME_ERR_LEADING_SLASH);

	/* A NULL path counts as missing the leading slash. */
	unit_err(mountpoint_namecheck(NULL, &why), -1);
	unit_eq(why, NAME_ERR_LEADING_SLASH);

	/* A long path component is rejected. */
	char buf[ZFS_MAX_DATASET_NAME_LEN + 4];
	buf[0] = '/';
	(void) memset(buf + 1, 'a', sizeof (buf) - 2);
	buf[sizeof (buf) - 1] = '\0';
	unit_err(mountpoint_namecheck(buf, &why), -1);
	unit_eq(why, NAME_ERR_TOOLONG);

	return (MUNIT_OK);
}

/* get_dataset_depth: a path's level of nesting (depth). */
static MunitResult
test_dataset_depth(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	/* Depth is the number of '/' separators in the path. */
	unit_eq(get_dataset_depth("tank"), 0);
	unit_eq(get_dataset_depth("tank/home"), 1);
	unit_eq(get_dataset_depth("tank/home/user"), 2);

	/* Counting stops at the snapshot or bookmark delimiter. */
	unit_eq(get_dataset_depth("tank/home@snap"), 1);
	unit_eq(get_dataset_depth("tank/home#bm"), 1);

	/*
	 * dataset_nestcheck() passes while the depth is under the limit and
	 * fails once it reaches it.  zfs_max_dataset_nesting is a tunable that
	 * can be adjusted to the desired nesting.
	 */
	zfs_max_dataset_nesting = 2;
	unit_ok(dataset_nestcheck("a/b"));		/* depth 1, under 2 */
	unit_err(dataset_nestcheck("a/b/c"), -1);	/* depth 2, at 2   */

	return (MUNIT_OK);
}

/* ========== */

static const MunitTest namecheck_tests[] = {
	UNIT_TEST("pool",	test_pool_namecheck),
	UNIT_TEST("dataset",	test_dataset_namecheck),
	UNIT_TEST("snapshot",	test_snapshot_namecheck),
	UNIT_TEST("bookmark",	test_bookmark_namecheck),
	UNIT_TEST("component",	test_component_namecheck),
	UNIT_TEST("permset",	test_permset_namecheck),
	UNIT_TEST("mountpoint",	test_mountpoint_namecheck),
	UNIT_TEST("depth",	test_dataset_depth),
	{ 0 },
};

static const MunitSuite namecheck_test_suite = {
	"namecheck.",
	namecheck_tests,
	NULL,
	1,
	MUNIT_SUITE_OPTION_NONE,
};

int
main(int argc, char **argv)
{
	return (munit_suite_main(&namecheck_test_suite, NULL, argc, argv));
}
