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
 * Copyright (c) 2021 by Lawrence Livermore National Security, LLC.
 */

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sysmacros.h>
#include <errno.h>
#ifdef __linux__
#include <linux/fs.h>
#endif

/* some older uClibc's lack the defines, so we'll manually define them */
#ifdef	__UCLIBC__
#ifndef	SEEK_DATA
#define	SEEK_DATA 3
#endif
#ifndef	SEEK_HOLE
#define	SEEK_HOLE 4
#endif
#endif

static void
seek_data(int fd, off_t offset, off_t expected)
{
	off_t data_offset = lseek(fd, offset, SEEK_DATA);
	if (data_offset != expected) {
		fprintf(stderr, "lseek(fd, %d, SEEK_DATA) = %d (expected %d)\n",
		    (int)offset, (int)data_offset, (int)expected);
		exit(2);
	}
}

static void
seek_hole(int fd, off_t offset, off_t expected)
{
	off_t hole_offset = lseek(fd, offset, SEEK_HOLE);
	if (hole_offset != expected) {
		fprintf(stderr, "lseek(fd, %d, SEEK_HOLE) = %d (expected %d)\n",
		    (int)offset, (int)hole_offset, (int)expected);
		exit(2);
	}
}

int
main(int argc, char **argv)
{
	char *execname = argv[0];
	char *file_path = argv[1];
	char *buf = NULL;
	int err;

	if (argc != 4) {
		(void) printf("usage: %s <file name> <file size> "
		    "<block size>\n", argv[0]);
		exit(1);
	}

	int fd = open(file_path, O_RDWR | O_CREAT, 0666);
	if (fd == -1) {
		(void) fprintf(stderr, "%s: %s: ", execname, file_path);
		perror("open");
		exit(2);
	}

	off_t file_size = atoi(argv[2]);
	off_t block_size = atoi(argv[3]);

	if (block_size * 2 > file_size) {
		(void) fprintf(stderr, "file size must be at least "
		    "double the block size\n");
		exit(2);
	}

	err = ftruncate(fd, file_size);
	if (err == -1) {
		perror("ftruncate");
		exit(2);
	}

	if ((buf = mmap(NULL, file_size, PROT_READ | PROT_WRITE,
	    MAP_SHARED, fd, 0)) == MAP_FAILED) {
		perror("mmap");
		exit(2);
	}

	/* Verify the file is sparse and reports no data. */
	seek_data(fd, 0, -1);

	/* Verify the file is reported as a hole. */
	seek_hole(fd, 0, 0);

	/* Verify search beyond end of file is an error. */
	seek_data(fd, 2 * file_size, -1);
	seek_hole(fd, 2 * file_size, -1);

	/* Dirty the first byte. */
	memset(buf, 'a', 1);
	seek_data(fd, 0, 0);
	seek_data(fd, block_size, -1);
	seek_hole(fd, 0, block_size);
	seek_hole(fd, block_size, block_size);

	/* Dirty the first half of the file. */
	memset(buf, 'b', file_size / 2);
	seek_data(fd, 0, 0);
	seek_data(fd, block_size, block_size);
	seek_hole(fd, 0, P2ROUNDUP(file_size / 2, block_size));
	seek_hole(fd, block_size, P2ROUNDUP(file_size / 2, block_size));

	/* Dirty the whole file. */
	memset(buf, 'c', file_size);
	seek_data(fd, 0, 0);
	seek_data(fd, file_size * 3 / 4,
	    P2ROUNDUP(file_size * 3 / 4, block_size));
	seek_hole(fd, 0, file_size);
	seek_hole(fd, file_size / 2, file_size);

	/* Punch a hole (required compression be enabled). */
	memset(buf + block_size, 0, block_size);
	seek_data(fd, 0, 0);
	seek_data(fd, block_size, 2 * block_size);
	seek_hole(fd, 0, block_size);
	seek_hole(fd, block_size, block_size);
	seek_hole(fd, 2 * block_size, file_size);

	err = munmap(buf, file_size);
	if (err == -1) {
		perror("munmap");
		exit(2);
	}

	close(fd);

	return (0);
}
