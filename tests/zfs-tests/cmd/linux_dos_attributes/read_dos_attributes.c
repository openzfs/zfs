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
 * Copyright 2022 iXsystems, Inc.
 */

/*
 * FreeBSD exposes additional file attributes via ls -o and chflags.
 * Under Linux, we provide ZFS_IOC_[GS]ETDOSFLAGS ioctl()s.
 *
 * This application is the equivalent to FreeBSD ls -lo $1 | awk '{print $5}'.
 */

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/fs/zfs.h>
#include "dos_attributes.h"

int
main(int argc, const char *const *argv)
{
	if (argc != 2)
		errx(EXIT_FAILURE, "usage: %s file", argv[0]);

	int fd = open(argv[1], O_RDONLY | O_CLOEXEC);
	if (fd == -1)
		err(EXIT_FAILURE, "%s", argv[1]);

	uint64_t flags;
	if (ioctl(fd, ZFS_IOC_GETDOSFLAGS, &flags) == -1)
		err(EXIT_FAILURE, "ZFS_IOC_GETDOSFLAGS");

	bool any = false;
	for (size_t i = 0; i < ARRAY_SIZE(all_dos_attributes); ++i)
		if (flags & all_dos_attributes[i]) {
			if (any)
				putchar(',');
			(void) fputs(*all_dos_attribute_names[i], stdout);
			any = true;
		}
	if (any)
		(void) putchar('\n');
	else
		(void) puts("-");
}
