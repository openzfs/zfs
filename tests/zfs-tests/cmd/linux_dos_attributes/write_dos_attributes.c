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
 * This application is equivalent to FreeBSD chflags.
 */

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/fs/zfs.h>
#include "dos_attributes.h"

int
main(int argc, const char *const *argv)
{
	if (argc != 3)
		errx(EXIT_FAILURE, "usage: %s flag file", argv[0]);

	bool unset = false;
	uint64_t attr = 0;
	const char *flag = argv[1];
	if (strcmp(flag, "0") == 0)
		;
	else if (strcmp(flag, SU_NODUMP) == 0)
		attr = ZFS_NODUMP;
	else if (strcmp(flag, UNSET_NODUMP) == 0) {
		attr = ZFS_NODUMP;
		unset = true;
	} else {
		if (strncmp(flag, "no", 2) == 0) {
			unset = true;
			flag += 2;
		}
		for (size_t i = 0; i < ARRAY_SIZE(all_dos_attribute_names); ++i)
			for (const char *const *nm = all_dos_attribute_names[i];
			    *nm; ++nm)
				if (strcmp(flag, *nm) == 0) {
					attr = all_dos_attributes[i];
					goto found;
				}

		errx(EXIT_FAILURE, "%s: unknown flag", argv[1]);
found:;
	}

	int fd = open(argv[2], O_RDWR | O_APPEND | O_CLOEXEC);
	if (fd == -1)
		err(EXIT_FAILURE, "%s", argv[2]);

	uint64_t flags;
	if (ioctl(fd, ZFS_IOC_GETDOSFLAGS, &flags) == -1)
		err(EXIT_FAILURE, "ZFS_IOC_GETDOSFLAGS");

	if (attr == 0)
		flags = 0;
	else if (unset)
		flags &= ~attr;
	else
		flags |= attr;

	if (ioctl(fd, ZFS_IOC_SETDOSFLAGS, &flags) == -1)
		err(EXIT_FAILURE, "ZFS_IOC_SETDOSFLAGS");

	uint64_t newflags;
	if (ioctl(fd, ZFS_IOC_GETDOSFLAGS, &newflags) == -1)
		err(EXIT_FAILURE, "second ZFS_IOC_GETDOSFLAGS");

	if (newflags != flags)
		errx(EXIT_FAILURE, "expecting %#" PRIx64 ", got %#" PRIx64
		    "; %ssetting %#" PRIx64 "",
		    flags, newflags, unset ? "un" : "", attr);

	(void) printf("%#" PRIx64 "\n", flags);
}
