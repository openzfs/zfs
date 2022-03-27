/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
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
