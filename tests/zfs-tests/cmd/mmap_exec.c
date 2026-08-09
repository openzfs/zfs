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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright (c) 2013 by Delphix. All rights reserved.
 */

#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <errno.h>

int
main(int argc, char *argv[])
{
	int error, fd;
	struct stat statbuf;

	if (argc != 2) {
		(void) printf("Error: missing binary name.\n");
		(void) printf("Usage:\n\t%s <binary name>\n",
		    argv[0]);
		return (1);
	}

	errno = 0;

	if ((fd = open(argv[1], O_RDONLY)) < 0) {
		error = errno;
		perror("open");
		return (error);
	}
	if (fstat(fd, &statbuf) < 0) {
		error = errno;
		perror("fstat");
		return (error);
	}

	if (mmap(0, statbuf.st_size,
	    PROT_EXEC, MAP_SHARED, fd, 0) == MAP_FAILED) {
		error = errno;
		perror("mmap");
		return (error);
	}

	return (0);
}
