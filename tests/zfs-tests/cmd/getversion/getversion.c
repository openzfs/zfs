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
 * Copyright 2021 iXsystems, Inc.
 */

/*
 * FreeBSD and macOS expose file generation number through stat(2) and stat(1).
 * Linux exposes it instead through an ioctl.
 */

#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <linux/fs.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int
main(int argc, const char * const argv[])
{
	if (argc != 2)
		errx(EXIT_FAILURE, "usage: %s filename", argv[0]);

	int fd = open(argv[1], O_RDONLY);
	if (fd == -1)
		err(EXIT_FAILURE, "failed to open %s", argv[1]);

	int gen = 0;
	if (ioctl(fd, FS_IOC_GETVERSION, &gen) == -1)
		err(EXIT_FAILURE, "FS_IOC_GETVERSION failed");

	(void) close(fd);

	(void) printf("%d\n", gen);

	return (EXIT_SUCCESS);
}
