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
 * Copyright (c) 2024 by Pawel Jakub Dawidek
 */

#include <sys/mman.h>
#include <sys/stat.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(_GNU_SOURCE) && defined(__linux__)
_Static_assert(sizeof (loff_t) == sizeof (off_t),
	"loff_t and off_t must be the same size");
#endif

ssize_t
copy_file_range(int, off_t *, int, off_t *, size_t, unsigned int)
    __attribute__((weak));

static void *
mmap_file(int fd, size_t size)
{
	void *p;

	p = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED) {
		(void) fprintf(stderr, "mmap failed: %s\n", strerror(errno));
		exit(2);
	}

	return (p);
}

static void
usage(const char *progname)
{

	/*
	 * -i cache input before copy_file_range(2).
	 * -o cache input before copy_file_range(2).
	 */
	(void) fprintf(stderr, "usage: %s [-io] <input> <output>\n", progname);
	exit(3);
}

int
main(int argc, char *argv[])
{
	int dfd, sfd;
	size_t dsize, ssize;
	void *dmem, *smem, *ptr;
	off_t doff, soff;
	struct stat sb;
	bool cache_input, cache_output;
	const char *progname;
	int c;

	progname = argv[0];
	cache_input = cache_output = false;

	while ((c = getopt(argc, argv, "io")) != -1) {
		switch (c) {
		case 'i':
			cache_input = true;
			break;
		case 'o':
			cache_output = true;
			break;
		default:
			usage(progname);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 2) {
		usage(progname);
	}

	sfd = open(argv[0], O_RDONLY);
	if (fstat(sfd, &sb) == -1) {
		(void) fprintf(stderr, "fstat failed: %s\n", strerror(errno));
		exit(2);
	}
	ssize = sb.st_size;
	smem = mmap_file(sfd, ssize);

	dfd = open(argv[1], O_RDWR);
	if (fstat(dfd, &sb) == -1) {
		(void) fprintf(stderr, "fstat failed: %s\n", strerror(errno));
		exit(2);
	}
	dsize = sb.st_size;
	dmem = mmap_file(dfd, dsize);

	/*
	 * Hopefully it won't be compiled out.
	 */
	if (cache_input) {
		ptr = malloc(ssize);
		assert(ptr != NULL);
		memcpy(ptr, smem, ssize);
		free(ptr);
	}
	if (cache_output) {
		ptr = malloc(ssize);
		assert(ptr != NULL);
		memcpy(ptr, dmem, dsize);
		free(ptr);
	}

	soff = doff = 0;
	if (copy_file_range(sfd, &soff, dfd, &doff, ssize, 0) < 0) {
		(void) fprintf(stderr, "copy_file_range failed: %s\n",
		    strerror(errno));
		exit(2);
	}

	exit(memcmp(smem, dmem, ssize) == 0 ? 0 : 1);
}
