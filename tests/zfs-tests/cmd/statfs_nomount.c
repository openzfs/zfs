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
 * Print the f_fsid of a path as 16 hex digits.  The path is opened with
 * O_PATH so that an automount point, such as a '.zfs/snapshot/<name>'
 * entry, is probed without being mounted.
 */

#ifndef _GNU_SOURCE
#define	_GNU_SOURCE
#endif
#include <fcntl.h>
#include <stdio.h>
#include <sys/vfs.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	struct statfs st;
	int fd;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <path>\n", argv[0]);
		return (2);
	}

	fd = open(argv[1], O_PATH | O_CLOEXEC);
	if (fd < 0) {
		perror("open");
		return (1);
	}
	if (fstatfs(fd, &st) != 0) {
		perror("fstatfs");
		return (1);
	}
	/* Same word order as nfs-utils' uuid_by_path() and stat -f %i. */
	printf("%08x%08x\n", (unsigned int)st.f_fsid.__val[0],
	    (unsigned int)st.f_fsid.__val[1]);
	return (0);
}
