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
 * Copyright (c) 2025, Klara, Inc.
 */

/*
 * Tests async writeback behaviour. Creates a file, maps it into memory, and
 * dirties every page within it. Then, calls ftruncate() to collapse the file
 * back down to 0. This causes the kernel to begin writeback on the dirty
 * pages so they can be freed, before it can complete the ftruncate() call.
 * None of these are sync operations, so they should avoid the various "force
 * flush" codepaths.
 */

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>

#define	_pdfail(f, l, s)	\
	do { perror("[" f "#" #l "] " s); exit(2); } while (0)
#define	pdfail(str) _pdfail(__FILE__, __LINE__, str)

int
main(int argc, char **argv) {
	if (argc != 3) {
		printf("usage: mmap_ftruncate <file> <size>\n");
		exit(2);
	}

	const char *file = argv[1];

	char *end;
	off_t sz = strtoull(argv[2], &end, 0);
	if (end == argv[2] || *end != '\0' || sz == 0) {
		fprintf(stderr, "E: invalid size");
		exit(2);
	}

	int fd = open(file, O_CREAT|O_TRUNC|O_RDWR, S_IRUSR|S_IWUSR);
	if (fd < 0)
		pdfail("open");

	if (ftruncate(fd, sz) < 0)
		pdfail("ftruncate");

	char *p = mmap(NULL, sz, PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED)
		pdfail("mmap");

	for (off_t off = 0; off < sz; off += 4096)
		p[off] = 1;

	if (ftruncate(fd, 0) < 0)
		pdfail("ftruncate");

	if (munmap(p, sz) < 0)
		pdfail("munmap");

	close(fd);
	return (0);
}
