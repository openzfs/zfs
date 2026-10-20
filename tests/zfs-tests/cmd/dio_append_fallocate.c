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
 * Copyright (c) 2026 by the OpenZFS project.  All rights reserved.
 */

/*
 * Race fallocate() against a 128 KiB O_DIRECT append and check that
 * the file keeps the appended block.
 *
 * Exit 0 if every round kept it, 1 if any round lost it, 4 if
 * fallocate() fails with EOPNOTSUPP, and 2 on any other error.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/falloc.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	BLKSZ		(128 * 1024)
#define	FALLOC_LEN	4096

typedef struct falloc_arg {
	int	fa_fd;
	int	fa_mode;
	off_t	fa_off;
	off_t	fa_len;
	int	fa_ret;
	int	fa_errno;
} falloc_arg_t;

static __attribute__((noreturn)) void
usage(void)
{
	(void) fprintf(stderr, "usage: dio_append_fallocate "
	    "<extend|punch|zero> <file> <rounds>\n");
	exit(2);
}

static void *
falloc_thread(void *arg)
{
	falloc_arg_t *fa = arg;

	fa->fa_ret = fallocate(fa->fa_fd, fa->fa_mode, fa->fa_off,
	    fa->fa_len);
	fa->fa_errno = errno;
	return (NULL);
}

int
main(int argc, char **argv)
{
	falloc_arg_t fa;
	pthread_t tid;
	struct stat st;
	const char *path;
	char *end;
	void *buf;
	long rounds, lost = 0, first = 0;
	off_t sz, first_size = 0, first_want = 0;
	ssize_t n;
	int fd, dfd, mode, rc;

	if (argc != 4)
		usage();
	if (strcmp(argv[1], "extend") == 0)
		mode = 0;
	else if (strcmp(argv[1], "punch") == 0)
		mode = FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE;
	else if (strcmp(argv[1], "zero") == 0)
		mode = FALLOC_FL_ZERO_RANGE | FALLOC_FL_KEEP_SIZE;
	else
		usage();
	path = argv[2];
	errno = 0;
	rounds = strtol(argv[3], &end, 10);
	if (errno != 0 || end == argv[3] || *end != '\0' ||
	    rounds < 1)
		usage();

	rc = posix_memalign(&buf, getpagesize(), BLKSZ);
	if (rc != 0) {
		errno = rc;
		err(2, "posix_memalign");
	}
	(void) memset(buf, 0x5a, BLKSZ);

	for (long i = 1; i <= rounds; i++) {
		fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
		if (fd == -1)
			err(2, "open %s", path);
		n = write(fd, buf, BLKSZ);
		if (n == -1)
			err(2, "write %s", path);
		if (n != BLKSZ)
			errx(2, "short write to %s", path);
		if (fsync(fd) == -1)
			err(2, "fsync %s", path);
		if (fstat(fd, &st) == -1)
			err(2, "fstat %s", path);
		sz = st.st_size;

		dfd = open(path, O_RDWR | O_DIRECT);
		if (dfd == -1)
			err(2, "open %s O_DIRECT", path);

		fa.fa_fd = fd;
		fa.fa_mode = mode;
		if (mode == 0) {
			fa.fa_off = 0;
			fa.fa_len = sz + FALLOC_LEN;
		} else {
			fa.fa_off = sz;
			fa.fa_len = FALLOC_LEN;
		}

		/* Another thread runs fallocate() during pwrite(). */
		rc = pthread_create(&tid, NULL, falloc_thread, &fa);
		if (rc != 0) {
			errno = rc;
			err(2, "pthread_create");
		}
		n = pwrite(dfd, buf, BLKSZ, sz);
		if (n == -1)
			err(2, "O_DIRECT write %s", path);
		if (n != BLKSZ)
			errx(2, "short O_DIRECT write to %s", path);
		rc = pthread_join(tid, NULL);
		if (rc != 0) {
			errno = rc;
			err(2, "pthread_join");
		}

		if (fa.fa_ret == -1) {
			errno = fa.fa_errno;
			if (errno == EOPNOTSUPP) {
				warn("fallocate %s", argv[1]);
				exit(4);
			}
			err(2, "fallocate %s", argv[1]);
		}

		if (fstat(fd, &st) == -1)
			err(2, "fstat %s", path);
		if (st.st_size < sz + BLKSZ) {
			if (lost == 0) {
				first = i;
				first_size = st.st_size;
				first_want = sz + BLKSZ;
			}
			lost++;
		}

		if (close(dfd) == -1 || close(fd) == -1)
			err(2, "close %s", path);
	}
	free(buf);

	(void) printf("%s: %ld of %ld rounds lost the appended block",
	    argv[1], lost, rounds);
	if (lost == 0) {
		(void) printf("\n");
		return (0);
	}
	(void) printf(", round %ld left %lld bytes, expected "
	    "at least %lld\n", first, (long long)first_size,
	    (long long)first_want);
	return (1);
}
