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
 * Copyright (c) 2019 by Delphix. All rights reserved.
 */
#include <libintl.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdint.h>
#include <libzfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

libzfs_handle_t *g_zfs;

static void
usage(int err)
{
	fprintf(stderr, "Usage: zfs_ids_to_path [-v] <pool> <objset id> "
	    "<object id>\n");
	exit(err);
}

int
main(int argc, char **argv)
{
	boolean_t verbose = B_FALSE;
	int c;
	while ((c = getopt(argc, argv, "v")) != -1) {
		switch (c) {
		case 'v':
			verbose = B_TRUE;
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 3) {
		(void) fprintf(stderr, "Incorrect number of arguments: %d\n",
		    argc);
		usage(1);
	}

	uint64_t objset, object;
	if (sscanf(argv[1], "%llu", (u_longlong_t *)&objset) != 1) {
		(void) fprintf(stderr, "Invalid objset id: %s\n", argv[1]);
		usage(2);
	}
	if (sscanf(argv[2], "%llu", (u_longlong_t *)&object) != 1) {
		(void) fprintf(stderr, "Invalid object id: %s\n", argv[2]);
		usage(3);
	}
	if ((g_zfs = libzfs_init()) == NULL) {
		(void) fprintf(stderr, "%s\n", libzfs_error_init(errno));
		return (4);
	}
	zpool_handle_t *pool = zpool_open(g_zfs, argv[0]);
	if (pool == NULL) {
		fprintf(stderr, "Could not open pool %s\n", argv[0]);
		libzfs_fini(g_zfs);
		return (5);
	}

	char pathname[PATH_MAX * 2];
	if (verbose) {
		zpool_obj_to_path_ds(pool, objset, object, pathname,
		    sizeof (pathname));
	} else {
		zpool_obj_to_path(pool, objset, object, pathname,
		    sizeof (pathname));
	}
	printf("%s\n", pathname);
	zpool_close(pool);
	libzfs_fini(g_zfs);
	return (0);
}
