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

#include <sys/types.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zone.h>
#include <libnvpair.h>
#include <zfs_prop.h>
#include <lib/libzfs/libzfs_impl.h>

#include "unit.h"

/* ========== */

/*
 * zfs_foreach_mountpoint() hands each handle to the callback in a worker
 * thread, and the callback may replace the handle's properties at any time:
 * zfs_mount() does, through zfs_refresh_properties(), for an encrypted
 * dataset. So once the callbacks may run, no other thread may read a
 * handle's properties.
 *
 * Each callback here frees its handle's properties and leaves them
 * unreadable, so that a later read by zfs_foreach_mountpoint() fails a
 * VERIFY in libzfs and kills the test, instead of passing by luck.
 */

#define	FE_MAX_DS	16
#define	FE_NTHR		4
#define	FE_WAIT_SEC	10

typedef struct fe_ds {
	const char	*fd_name;
	const char	*fd_mountpoint;
	boolean_t	fd_zoned;
	int		fd_parent;	/* parent mountpoint, or -1 */
} fe_ds_t;

typedef struct fe_state {
	pthread_mutex_t	fs_lock;
	pthread_cond_t	fs_cv;
	zfs_handle_t	*fs_handles[FE_MAX_DS];	/* in fe_ds_t order */
	int		fs_nds;
	int		fs_seq;
	int		fs_calls[FE_MAX_DS];
	int		fs_start[FE_MAX_DS];
	int		fs_end[FE_MAX_DS];
	int		fs_waiter;	/* this callback waits... */
	int		fs_waitfor;	/* ...until this one has started */
	boolean_t	fs_timedout;
} fe_state_t;

/* An all-B_FALSE property table: any property read fails a VERIFY. */
static uint8_t fe_no_props[ZFS_NUM_PROPS];

static nvlist_t *
fe_prop(const char *source, const char *strval, uint64_t intval)
{
	nvlist_t *nv = fnvlist_alloc();

	if (strval != NULL)
		fnvlist_add_string(nv, ZPROP_VALUE, strval);
	else
		fnvlist_add_uint64(nv, ZPROP_VALUE, intval);
	fnvlist_add_string(nv, ZPROP_SOURCE, source);
	return (nv);
}

static zfs_handle_t *
fe_handle(libzfs_handle_t *hdl, zpool_handle_t *zph, const fe_ds_t *ds)
{
	zfs_handle_t *zhp = calloc(1, sizeof (*zhp));
	nvlist_t *nv;

	unit_notnull(zhp);
	zhp->zfs_hdl = hdl;
	zhp->zpool_hdl = zph;
	(void) strlcpy(zhp->zfs_name, ds->fd_name, sizeof (zhp->zfs_name));
	zhp->zfs_type = ZFS_TYPE_FILESYSTEM;
	zhp->zfs_head_type = ZFS_TYPE_FILESYSTEM;

	zhp->zfs_props = fnvlist_alloc();
	nv = fe_prop(ds->fd_name, ds->fd_mountpoint, 0);
	fnvlist_add_nvlist(zhp->zfs_props,
	    zfs_prop_to_name(ZFS_PROP_MOUNTPOINT), nv);
	fnvlist_free(nv);
	nv = fe_prop(ds->fd_name, NULL, ds->fd_zoned);
	fnvlist_add_nvlist(zhp->zfs_props, zfs_prop_to_name(ZFS_PROP_ZONED),
	    nv);
	fnvlist_free(nv);

	return (zhp);
}

static int
fe_index(fe_state_t *st, zfs_handle_t *zhp)
{
	for (int i = 0; i < st->fs_nds; i++) {
		if (st->fs_handles[i] == zhp)
			return (i);
	}
	unit_unreachable();
	return (-1);
}

static int
fe_callback(zfs_handle_t *zhp, void *arg)
{
	fe_state_t *st = arg;
	int i = fe_index(st, zhp);

	/* Drop the properties before anyone can see that we started. */
	nvlist_free(zhp->zfs_props);
	zhp->zfs_props = NULL;
	zhp->zfs_props_table = fe_no_props;

	pthread_mutex_lock(&st->fs_lock);
	st->fs_calls[i]++;
	st->fs_start[i] = ++st->fs_seq;
	(void) pthread_cond_broadcast(&st->fs_cv);

	if (i == st->fs_waiter) {
		struct timespec ts;

		(void) clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec += FE_WAIT_SEC;
		while (st->fs_start[st->fs_waitfor] == 0 && !st->fs_timedout) {
			if (pthread_cond_timedwait(&st->fs_cv, &st->fs_lock,
			    &ts) == ETIMEDOUT)
				st->fs_timedout = B_TRUE;
		}
	}

	st->fs_end[i] = ++st->fs_seq;
	pthread_mutex_unlock(&st->fs_lock);

	return (0);
}

/*
 * Run zfs_foreach_mountpoint() over the given datasets, passed in reverse
 * order so that it has to sort them, with fe_callback() as the callback.
 */
static void
fe_run(const fe_ds_t *ds, int nds, int waiter, int waitfor, fe_state_t *st)
{
	libzfs_handle_t *hdl;
	zpool_handle_t *zph;
	zfs_handle_t *handles[FE_MAX_DS];

	unit_le(nds, FE_MAX_DS);
	unit_zero(unsetenv("ZFS_SERIAL_MOUNT"));

	memset(st, 0, sizeof (*st));
	unit_zero(pthread_mutex_init(&st->fs_lock, NULL));
	unit_zero(pthread_cond_init(&st->fs_cv, NULL));
	st->fs_nds = nds;
	st->fs_waiter = waiter;
	st->fs_waitfor = waitfor;

	hdl = calloc(1, sizeof (*hdl));
	unit_notnull(hdl);
	zph = calloc(1, sizeof (*zph));
	unit_notnull(zph);
	zph->zpool_hdl = hdl;
	(void) strlcpy(zph->zpool_name, "p", sizeof (zph->zpool_name));
	zph->zpool_state = POOL_STATE_ACTIVE;
	zph->zpool_props = fnvlist_alloc();

	for (int i = 0; i < nds; i++) {
		st->fs_handles[i] = fe_handle(hdl, zph, &ds[i]);
		handles[nds - 1 - i] = st->fs_handles[i];
	}

	zfs_foreach_mountpoint(hdl, handles, nds, fe_callback, st, FE_NTHR);

	for (int i = 0; i < nds; i++)
		zfs_close(st->fs_handles[i]);
	zpool_close(zph);
	free(hdl);
	(void) pthread_cond_destroy(&st->fs_cv);
	(void) pthread_mutex_destroy(&st->fs_lock);
}

/* Every parent's callback must have returned before its child's started. */
static void
fe_check_order(const fe_ds_t *ds, int nds, const fe_state_t *st)
{
	for (int i = 0; i < nds; i++) {
		int p = ds[i].fd_parent;

		if (p < 0 || st->fs_calls[i] == 0)
			continue;
		unit_eq(st->fs_calls[p], 1);
		unit_lt(st->fs_end[p], st->fs_start[i]);
	}
}

/*
 * /p/a's callback does not return until /p/b's has started. With /p/b
 * dispatched, the task for /p/a must not read /p/b's mountpoint to find
 * out that it is not a child of /p/a.
 */
static MunitResult
test_foreach_mountpoint_siblings(const MunitParameter params[], void *data)
{
	static const fe_ds_t ds[] = {
		{ "p",		"/p",		B_FALSE,	-1 },
		{ "p/a",	"/p/a",		B_FALSE,	0 },
		{ "p/b",	"/p/b",		B_FALSE,	0 },
	};
	const int nds = ARRAY_SIZE(ds);
	fe_state_t st;

	(void) params, (void) data;

	fe_run(ds, nds, 1, 2, &st);

	unit_false(st.fs_timedout);
	for (int i = 0; i < nds; i++)
		unit_eq(st.fs_calls[i], 1);
	fe_check_order(ds, nds, &st);

	return (MUNIT_OK);
}

/*
 * A tree with deeper levels, mountpoints sharing a prefix without being
 * nested, a second top-level mountpoint, legacy and none, and a zoned
 * dataset, which the parallel path skips in the global zone.
 */
static MunitResult
test_foreach_mountpoint_tree(const MunitParameter params[], void *data)
{
	static const fe_ds_t ds[] = {
		{ "p",		"/p",		B_FALSE,	-1 },
		{ "p/a",	"/p/a",		B_FALSE,	0 },
		{ "p/a/x",	"/p/a/x",	B_FALSE,	1 },
		{ "p/a/x/y",	"/p/a/x/y",	B_FALSE,	2 },
		{ "p/a/z",	"/p/a/z",	B_FALSE,	1 },
		{ "p/ab",	"/p/ab",	B_FALSE,	0 },
		{ "p/a.b",	"/p/a.b",	B_FALSE,	0 },
		{ "p/b",	"/p/b",		B_FALSE,	0 },
		{ "p/q",	"/q",		B_FALSE,	-1 },
		{ "p/q/r",	"/q/r",		B_FALSE,	8 },
		{ "p/legacy",	"legacy",	B_FALSE,	-1 },
		{ "p/none",	"none",		B_FALSE,	-1 },
		{ "p/zoned",	"/zoned",	B_TRUE,		-1 },
	};
	const int nds = ARRAY_SIZE(ds);
	fe_state_t st;

	(void) params, (void) data;

	fe_run(ds, nds, -1, -1, &st);

	for (int i = 0; i < nds; i++) {
		if (ds[i].fd_zoned && getzoneid() == GLOBAL_ZONEID)
			unit_eq(st.fs_calls[i], 0);
		else
			unit_eq(st.fs_calls[i], 1);
	}
	fe_check_order(ds, nds, &st);

	return (MUNIT_OK);
}

/* ========== */

static const MunitTest libzfs_mount_tests[] = {
	UNIT_TEST("foreach_mountpoint_siblings",
	    test_foreach_mountpoint_siblings),
	UNIT_TEST("foreach_mountpoint_tree",
	    test_foreach_mountpoint_tree),
	{ 0 },
};

static const MunitSuite libzfs_mount_test_suite = {
	"libzfs_mount.",
	libzfs_mount_tests,
	NULL,
	1,
	MUNIT_SUITE_OPTION_NONE,
};

int
main(int argc, char **argv)
{
	zfs_prop_init();
	zpool_prop_init();

	return (munit_suite_main(&libzfs_mount_test_suite, NULL, argc, argv));
}
