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
 * This program clones the file, mmap it, and writes from the map into
 * file. This scenario triggers a panic on Linux in dbuf_redirty(),
 * which is fixed under PR#15656. On FreeBSD, the same test causes data
 * corruption, which is fixed by PR#15665.
 *
 * It would be good to test for this scenario in ZTS. This program and
 * issue was initially produced by @robn.
 */
#ifndef _GNU_SOURCE
#define	_GNU_SOURCE
#endif

#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>

#ifdef __FreeBSD__
#define	loff_t	off_t
#endif

ssize_t
copy_file_range(int, loff_t *, int, loff_t *, size_t, unsigned int)
    __attribute__((weak));

static int
open_file(const char *source)
{
	int fd;
	if ((fd = open(source, O_RDWR | O_APPEND)) < 0) {
		(void) fprintf(stderr, "Error opening %s\n", source);
		exit(1);
	}
	sync();
	return (fd);
}

static int
clone_file(int sfd, long long size, const char *dest)
{
	int dfd;

	if ((dfd = open(dest, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR)) < 0) {
		(void) fprintf(stderr, "Error opening %s\n", dest);
		exit(1);
	}

	if (copy_file_range(sfd, 0, dfd, 0, size, 0) < 0) {
		(void) fprintf(stderr, "copy_file_range failed\n");
		exit(1);
	}

	return (dfd);
}

static void *
map_file(int fd, long long size)
{
	void *p = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED) {
		(void) fprintf(stderr, "mmap failed\n");
		exit(1);
	}

	return (p);
}

static void
map_write(void *p, int fd)
{
	if (pwrite(fd, p, 1024*128, 0) < 0) {
		(void) fprintf(stderr, "write failed\n");
		exit(1);
	}
}

int
main(int argc, char **argv)
{
	int sfd, dfd;
	void *p;
	struct stat sb;
	if (argc != 3) {
		(void) printf("usage: %s <input source file> "
		    "<clone destination file>\n", argv[0]);
		exit(1);
	}
	sfd = open_file(argv[1]);
	if (fstat(sfd, &sb) == -1) {
		(void) fprintf(stderr, "fstat failed\n");
		exit(1);
	}
	dfd = clone_file(sfd, sb.st_size, argv[2]);
	p = map_file(dfd, sb.st_size);
	map_write(p, dfd);
	return (0);
}
