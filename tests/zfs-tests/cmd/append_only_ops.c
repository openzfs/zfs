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
 * Copyright (c) 2026, Kyle Evans <kevans@FreeBSD.org>
 */

/*
 * Attempt to modify existing data in a file through a descriptor opened with
 * O_APPEND, which is the only way an append-only file can be opened for
 * writing.
 *
 *   append_only_ops ftruncate <file> <length>
 *   append_only_ops punch <file> <offset> <length>
 *   append_only_ops clone <src> <dst> <offset> <length>   (FreeBSD only)
 *
 * "clone" clears O_APPEND on the destination descriptor with F_SETFL before
 * calling copy_file_range(2), since an O_APPEND destination is refused
 * outright.
 *
 * Exits 0 if the operation succeeded and 1 if it failed.
 */

#if defined(__linux__) && !defined(_GNU_SOURCE)
#define	_GNU_SOURCE
#endif

#include <sys/types.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/falloc.h>
#endif

static void
usage(void)
{
	(void) fprintf(stderr,
	    "usage: append_only_ops ftruncate <file> <length>\n"
	    "       append_only_ops punch <file> <offset> <length>\n"
	    "       append_only_ops clone <src> <dst> <offset> <length>\n");
	exit(1);
}

static off_t
parse_off(const char *s)
{
	char *end;
	long long v;

	errno = 0;
	v = strtoll(s, &end, 0);
	if (errno != 0 || *end != '\0' || v < 0)
		errx(1, "invalid offset or length: %s", s);
	return ((off_t)v);
}

static int
open_append(const char *path)
{
	int fd;

	fd = open(path, O_WRONLY | O_APPEND);
	if (fd == -1)
		err(1, "open %s", path);
	return (fd);
}

static int
do_ftruncate(const char *path, off_t len)
{
	int fd;

	fd = open_append(path);
	if (ftruncate(fd, len) != 0) {
		warn("ftruncate");
		return (1);
	}

	(void) close(fd);
	return (0);
}

static int
do_punch(const char *path, off_t off, off_t len)
{
#if defined(__FreeBSD__)
	struct spacectl_range rqsr;
#endif
	int fd;

	fd = open_append(path);
#if defined(__FreeBSD__)
	rqsr.r_offset = off;
	rqsr.r_len = len;
	if (fspacectl(fd, SPACECTL_DEALLOC, &rqsr, 0, NULL) != 0) {
		warn("fspacectl");
		return (1);
	}
#elif defined(__linux__)
	if (fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
	    off, len) != 0) {
		warn("fallocate");
		return (1);
	}
#else
	(void) off;
	(void) len;
	errx(1, "punch is not supported on this platform");
#endif
	(void) close(fd);
	return (0);
}

static int
do_clone(const char *src, const char *dst, off_t off, off_t len)
{
#if defined(__FreeBSD__)
	off_t inoff;
	off_t outoff;
	ssize_t n;
	int sfd;
	int dfd;
	int fl;

	sfd = open(src, O_RDONLY);
	if (sfd == -1)
		err(1, "open %s", src);
	dfd = open_append(dst);
	if ((fl = fcntl(dfd, F_GETFL)) == -1)
		err(1, "F_GETFL");
	if (fcntl(dfd, F_SETFL, fl & ~O_APPEND) != 0) {
		warn("F_SETFL");
		return (1);
	}
	inoff = outoff = off;
	n = copy_file_range(sfd, &inoff, dfd, &outoff, len, 0);
	if (n == -1) {
		warn("copy_file_range");
		return (1);
	}
	(void) close(sfd);
	(void) close(dfd);
	return (n == 0);
#else
	(void) src;
	(void) dst;
	(void) off;
	(void) len;
	errx(1, "clone is not supported on this platform");
#endif
}

int
main(int argc, char *argv[])
{
	if (argc < 2)
		usage();

	if (strcmp(argv[1], "ftruncate") == 0 && argc == 4)
		return (do_ftruncate(argv[2], parse_off(argv[3])));
	if (strcmp(argv[1], "punch") == 0 && argc == 5)
		return (do_punch(argv[2], parse_off(argv[3]),
		    parse_off(argv[4])));
	if (strcmp(argv[1], "clone") == 0 && argc == 6)
		return (do_clone(argv[2], argv[3], parse_off(argv[4]),
		    parse_off(argv[5])));
	usage();
	return (1);
}
