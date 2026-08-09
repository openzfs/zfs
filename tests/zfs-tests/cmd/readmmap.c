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
 * --------------------------------------------------------------
 *	BugId 5047993 : Getting bad read data.
 *
 *	Usage: readmmap <filename>
 *
 *	where:
 *		filename is an absolute path to the file name.
 *
 *	Return values:
 *		1 : error
 *		0 : no errors
 * --------------------------------------------------------------
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <time.h>

int
main(int argc, char **argv)
{
	const char *filename = "badfile";
	size_t size = 4395;
	size_t idx = 0;
	char *buf = NULL;
	char *map = NULL;
	int fd = -1, bytes, retval = 0;
	uint_t seed;

	if (argc < 2 || optind == argc) {
		(void) fprintf(stderr,
		    "usage: %s <file name>\n", argv[0]);
		exit(1);
	}

	if ((buf = calloc(1, size)) == NULL) {
		perror("calloc");
		exit(1);
	}

	filename = argv[optind];

	(void) remove(filename);

	fd = open(filename, O_RDWR|O_CREAT|O_TRUNC, 0666);
	if (fd == -1) {
		perror("open to create");
		retval = 1;
		goto end;
	}

	bytes = write(fd, buf, size);
	if (bytes != size) {
		(void) printf("short write: %d != %zd\n", bytes, size);
		retval = 1;
		goto end;
	}

	map = mmap(0, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED) {
		perror("mmap");
		retval = 1;
		goto end;
	}
	seed = (uint_t)time(NULL);
	srandom(seed);

	idx = random() % size;
	map[idx] = 1;

	if (msync(map, size, MS_SYNC) != 0) {
		perror("msync");
		retval = 1;
		goto end;
	}

	if (munmap(map, size) != 0) {
		perror("munmap");
		retval = 1;
		goto end;
	}

	bytes = pread(fd, buf, size, 0);
	if (bytes != size) {
		(void) printf("short read: %d != %zd\n", bytes, size);
		retval = 1;
		goto end;
	}

	if (buf[idx] != 1) {
		(void) printf(
		    "bad data from read!  got buf[%zd]=%d, expected 1\n",
		    idx, buf[idx]);
		retval = 1;
		goto end;
	}

	(void) printf("good data from read: buf[%zd]=1\n", idx);
end:
	if (fd != -1) {
		(void) close(fd);
	}
	if (buf != NULL) {
		free(buf);
	}

	return (retval);
}
