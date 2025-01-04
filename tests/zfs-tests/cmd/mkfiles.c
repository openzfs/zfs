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
 * Copyright (c) 2016 by Delphix. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/param.h>

static void
usage(const char *msg, int exit_value)
{
	(void) fprintf(stderr, "usage: mkfiles basename max_file [min_file]\n"
	    "%s\n", msg);
	exit(exit_value);
}

int
main(int argc, char **argv)
{
	unsigned int numfiles = 0;
	unsigned int first_file = 0;
	unsigned int i;
	char buf[MAXPATHLEN];

	if (argc < 3 || argc > 4)
		usage("Invalid number of arguments", 1);

	if (sscanf(argv[2], "%u", &numfiles) != 1)
		usage("Invalid maximum file", 2);

	if (argc == 4 && sscanf(argv[3], "%u", &first_file) != 1)
		usage("Invalid first file", 3);

	for (i = first_file; i < first_file + numfiles; i++) {
		int fd;
		(void) snprintf(buf, MAXPATHLEN, "%s%u", argv[1], i);
		if ((fd = open(buf, O_CREAT | O_EXCL, O_RDWR)) == -1) {
			(void) fprintf(stderr, "Failed to create %s %s\n", buf,
			    strerror(errno));
			return (4);
		} else if (fchown(fd, getuid(), getgid()) < 0) {
			(void) fprintf(stderr, "Failed to chown %s %s\n", buf,
			    strerror(errno));
			return (5);
		}
		(void) close(fd);
	}
	return (0);
}
