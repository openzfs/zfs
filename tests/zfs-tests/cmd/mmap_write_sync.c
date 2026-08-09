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
