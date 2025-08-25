// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://opensource.org/licenses/CDDL-1.0.
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
