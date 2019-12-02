/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
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

void
usage(int err)
{
	fprintf(stderr, "Usage: ./os_id_to_name <pool> <objset id> "
	    "<object id>\n");
	exit(err);
}

int
main(int argc, char **argv)
{
	if (argc != 4) {
		(void) fprintf(stderr, "Incorrect number of arguments: %d\n",
		    argc);
		usage(1);
	}

	uint64_t objset, object;
	if (sscanf(argv[2], "%lu", &objset) != 1) {
		(void) fprintf(stderr, "Invalid objset id: %s\n", argv[2]);
		usage(2);
	}
	if (sscanf(argv[3], "%lu", &object) != 1) {
		(void) fprintf(stderr, "Invalid object id: %s\n", argv[3]);
		usage(3);
	}
	if ((g_zfs = libzfs_init()) == NULL) {
		(void) fprintf(stderr, "%s\n", libzfs_error_init(errno));
		return (4);
	}
	zpool_handle_t *pool = zpool_open(g_zfs, argv[1]);
	if (pool == NULL) {
		fprintf(stderr, "Could not open pool %s\n", argv[1]);
		libzfs_fini(g_zfs);
		return (5);
	}

	char pathname[PATH_MAX * 2];
	zpool_obj_to_path(pool, objset, object, pathname, PATH_MAX * 2);
	printf("%s\n", pathname);
	zpool_close(pool);
	libzfs_fini(g_zfs);
	return (0);
}
