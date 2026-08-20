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

#include <sys/mount.h>
#include <err.h>
#include <errno.h>
#include <stdlib.h>

int
main(int argc, char **argv)
{
	if (argc != 2)
		errx(EXIT_FAILURE, "usage: %s mountpoint", argv[0]);

	if (mount(NULL, argv[1], "zfs", 0, NULL) == 0)
		errx(EXIT_FAILURE, "mount unexpectedly succeeded");

	if (errno != EINVAL)
		err(EXIT_FAILURE, "mount");

	return (EXIT_SUCCESS);
}
