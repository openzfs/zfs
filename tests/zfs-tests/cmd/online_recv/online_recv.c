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
 * Copyright (c) 2019 by Datto, Inc. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libzfs_core.h>

int zfs_fd;

/*
 * Use libzfs_core to do a "zfs receive".  This allows us to
 * bypass certain checks in the zfs command utility and
 * perform an online receive into an existing filesystem for
 * testing purposes.
 */
int
main(int argc, const char *argv[])
{
	int err = 0;

	if (argc < 2) {
		fprintf(stderr, "usage: online_recv <destination>\n");
		exit(2);
	}

	(void) libzfs_core_init();

	err = lzc_receive(argv[1], NULL, NULL, B_TRUE, B_FALSE, 0);

	libzfs_core_fini();

	return (err);
}
