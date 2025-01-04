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

#ifdef __FreeBSD__
#define	loff_t	off_t
#endif

ssize_t
copy_file_range(int, loff_t *, int, loff_t *, size_t, unsigned int)
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
