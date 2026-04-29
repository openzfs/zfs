// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2026 by Delphix. All rights reserved.
 */

/*
 * libzfs_mnttab_cache_check.c
 *
 * Tests that libzfs_mnttab_cache(hdl, B_FALSE) does indeed disable the
 * per-handle mnttab cache. It does this by adding a fake entry to it, then
 * trying to read the status of a known-mounted dataset from it.
 *
 * As currently implemented, when enabled, libzfs_mnttab_find() assumes the
 * cache is correct and up to date if it has any entries in it at all. So by
 * putting something in it before searching, the initial load from /etc/mtab
 * never happens, and the real mounted datasets are never seen.
 *
 * When disabled, the entire cache is discarded and reloaded on every lookup,
 * so the fake entry will disappear and the real state will be found correctly.
 * to date if it has any entries in it at all.
 *
 * Run (as a user that can read /etc/mtab):
 *   ./libzfs_mnttab_cache_check <name-of-any-currently-mounted-zfs-dataset>
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/mnttab.h>
#include <libzfs.h>

int
main(int argc, char *argv[])
{
	if (argc != 2) {
		fprintf(stderr,
		    "usage: %s <currently-mounted-zfs-dataset>\n", argv[0]);
		return (2);
	}
	const char *real_ds = argv[1];

	libzfs_handle_t *hdl = libzfs_init();
	if (hdl == NULL) {
		fprintf(stderr, "libzfs_init failed\n");
		return (1);
	}

	/* Ask libzfs to disable the per-handle mnttab cache. */
	libzfs_mnttab_cache(hdl, B_FALSE);

	/*
	 * Stand-in for what zfs_mount() does internally on every successful
	 * mount: zfs_mount_at() calls libzfs_mnttab_add(hdl, ...) after
	 * do_mount(). In a real consumer, this happens implicitly; we call it
	 * directly here so the reproducer doesn't need root or a mountable
	 * dataset.
	 */
	libzfs_mnttab_add(hdl, "fake/dataset", "/fake/mountpoint", "rw");

	/*
	 * Now query ZFS_PROP_MOUNTED on a real, currently-mounted dataset.
	 * This is the standard libzfs API a consumer uses to check mount
	 * state. Internally it calls libzfs_mnttab_find().
	 */
	zfs_handle_t *zhp = zfs_open(hdl, real_ds, ZFS_TYPE_FILESYSTEM);
	if (zhp == NULL) {
		fprintf(stderr, "zfs_open(%s) failed\n", real_ds);
		libzfs_fini(hdl);
		return (1);
	}

	uint64_t mounted = zfs_prop_get_int(zhp, ZFS_PROP_MOUNTED);
	zfs_close(zhp);

	int rc;
	if (mounted) {
		printf("OK: ZFS_PROP_MOUNTED reports %s as mounted\n", real_ds);
		rc = 0;
	} else {
		printf("BUG: ZFS_PROP_MOUNTED reports %s as NOT mounted\n",
		    real_ds);
		printf("     but %s IS mounted (see /etc/mtab and "
		    "`zfs get mounted`).\n", real_ds);
		printf("     libzfs_mnttab_cache(hdl, B_FALSE) did not "
		    "actually disable the cache.\n");
		rc = 1;
	}

	libzfs_fini(hdl);
	return (rc);
}
