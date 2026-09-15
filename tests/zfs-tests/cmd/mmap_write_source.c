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
 * Copyright (c) 2026 by George Melikov.
 */

/*
 * Write to a file from a source which is a private mapping of another,
 * not yet resident, file.  The write has to transfer everything it was
 * given: a source page which is not resident is only a reason to fault
 * it in, not to cut the write short.
 *
 * usage: mmap_write_source <src> <dst> <size>
 */

#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
main(int argc, char *argv[])
{
	if (argc != 4) {
		(void) fprintf(stderr,
		    "usage: %s <src> <dst> <size>\n", argv[0]);
		return (2);
	}

	size_t size = strtoull(argv[3], NULL, 0);
	if (size == 0) {
		(void) fprintf(stderr, "invalid size: %s\n", argv[3]);
		return (2);
	}

	/*
	 * The source is a hole, so none of its pages are resident and
	 * reading them faults.
	 */
	int sfd = open(argv[1], O_RDWR | O_CREAT | O_TRUNC, 0666);
	if (sfd < 0 || ftruncate(sfd, size) != 0) {
		perror(argv[1]);
		return (1);
	}

	void *src = mmap(NULL, size, PROT_READ, MAP_PRIVATE, sfd, 0);
	if (src == MAP_FAILED) {
		perror("mmap");
		return (1);
	}

	int dfd = open(argv[2], O_RDWR | O_CREAT | O_TRUNC, 0666);
	if (dfd < 0) {
		perror(argv[2]);
		return (1);
	}

	ssize_t written = write(dfd, src, size);
	if (written < 0) {
		(void) fprintf(stderr, "write of %zu bytes failed: %s\n",
		    size, strerror(errno));
		return (1);
	}

	if ((size_t)written != size) {
		(void) fprintf(stderr,
		    "short write: %zd of %zu bytes\n", written, size);
		return (1);
	}

	if (close(dfd) != 0) {
		perror("close");
		return (1);
	}

	return (0);
}
