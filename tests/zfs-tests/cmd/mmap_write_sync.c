// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>

#define	PAGES	(8)

int
main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: %s <filename>\n", argv[0]);
		exit(1);
	}

	long page_size = sysconf(_SC_PAGESIZE);
	if (page_size < 0) {
		perror("sysconf");
		exit(2);
	}
	size_t map_size = page_size * PAGES;

	int fd = open(argv[1], O_CREAT|O_RDWR, S_IRWXU|S_IRWXG|S_IRWXO);
	if (fd < 0) {
		perror("open");
		exit(2);
	}

	if (ftruncate(fd, map_size) < 0) {
		perror("ftruncate");
		close(fd);
		exit(2);
	}

	uint64_t *p =
	    mmap(NULL, map_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED) {
		perror("mmap");
		close(fd);
		exit(2);
	}

	for (int i = 0; i < (map_size / sizeof (uint64_t)); i++)
		p[i] = 0x0123456789abcdef;

	if (msync(p, map_size, MS_SYNC) < 0) {
		perror("msync");
		munmap(p, map_size);
		close(fd);
		exit(3);
	}

	munmap(p, map_size);
	close(fd);
	exit(0);
}
