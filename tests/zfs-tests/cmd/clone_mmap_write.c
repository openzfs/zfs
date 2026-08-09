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

#if defined(_GNU_SOURCE) && defined(__linux__)
_Static_assert(sizeof (loff_t) == sizeof (off_t),
	"loff_t and off_t must be the same size");
#endif

ssize_t
copy_file_range(int, off_t *, int, off_t *, size_t, unsigned int)
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
