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
 * Portions Copyright 2020 iXsystems, Inc.
 */

/*
 * Test a corner case : a "doall" send without children datasets.
 */

#include <libzfs.h>
#include <libzfs_core.h>

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sysexits.h>
#include <err.h>

static void
usage(const char *name)
{
	fprintf(stderr, "usage: %s snap\n", name);
	exit(EX_USAGE);
}

int
main(int argc, char const * const argv[])
{
	sendflags_t flags = { 0 };
	libzfs_handle_t *zhdl;
	zfs_handle_t *zhp;
	const char *tofull, *fsname, *tosnap, *p;
	int error;

	if (argc != 2)
		usage(argv[0]);

	tofull = argv[1];

	p = strchr(tofull, '@');
	if (p == NULL)
		usage(argv[0]);
	tosnap = p + 1;

	fsname = strndup(tofull, p - tofull);

	zhdl = libzfs_init();
	if (zhdl == NULL)
		errx(EX_OSERR, "libzfs_init(): %s", libzfs_error_init(errno));

	zhp = zfs_open(zhdl, fsname, ZFS_TYPE_FILESYSTEM);
	if (zhp == NULL)
		err(EX_OSERR, "zfs_open(\"%s\")", fsname);

	flags.doall = B_TRUE;

	error = zfs_send(zhp, NULL, tosnap, &flags,
	    STDOUT_FILENO, NULL, NULL, NULL);

	zfs_close(zhp);

	libzfs_fini(zhdl);
	free((void *)fsname);

	return (error);
}
