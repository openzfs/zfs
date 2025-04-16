// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
