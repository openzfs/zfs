/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
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
 * Copyright (c) 2019 by Tomohiro Kusumi. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

/* backward compat in case it's not defined */
#ifndef O_TMPFILE
#define	O_TMPFILE	(020000000|O_DIRECTORY)
#endif

/*
 * DESCRIPTION:
 *	Verify stat(2) for O_TMPFILE file considers umask.
 *
 * STRATEGY:
 *	1. open(2) with O_TMPFILE.
 *	2. linkat(2).
 *	3. fstat(2)/stat(2) and verify .st_mode value.
 */

static void
test_stat_mode(mode_t mask)
{
	struct stat st, fst;
	int i, fd;
	char spath[1024], dpath[1024];
	char *penv[] = {"TESTDIR", "TESTFILE0"};
	mode_t masked = 0777 & ~mask;
	mode_t mode;

	/*
	 * Get the environment variable values.
	 */
	for (i = 0; i < sizeof (penv) / sizeof (char *); i++) {
		if ((penv[i] = getenv(penv[i])) == NULL) {
			fprintf(stderr, "getenv(penv[%d])\n", i);
			exit(1);
		}
	}

	umask(mask);
	fd = open(penv[0], O_RDWR|O_TMPFILE, 0777);
	if (fd == -1) {
		perror("open");
		exit(2);
	}

	if (fstat(fd, &fst) == -1) {
		perror("fstat");
		close(fd);
		exit(3);
	}

	snprintf(spath, sizeof (spath), "/proc/self/fd/%d", fd);
	snprintf(dpath, sizeof (dpath), "%s/%s", penv[0], penv[1]);

	unlink(dpath);
	if (linkat(AT_FDCWD, spath, AT_FDCWD, dpath, AT_SYMLINK_FOLLOW) == -1) {
		perror("linkat");
		close(fd);
		exit(4);
	}
	close(fd);

	if (stat(dpath, &st) == -1) {
		perror("stat");
		exit(5);
	}
	unlink(dpath);

	/* Verify fstat(2) result */
	mode = fst.st_mode & 0777;
	if (mode != masked) {
		fprintf(stderr, "fstat(2) %o != %o\n", mode, masked);
		exit(6);
	}

	/* Verify stat(2) result */
	mode = st.st_mode & 0777;
	if (mode != masked) {
		fprintf(stderr, "stat(2) %o != %o\n", mode, masked);
		exit(7);
	}
}

int
main(void)
{
	fprintf(stdout, "Verify stat(2) for O_TMPFILE file considers umask.\n");

	test_stat_mode(0022);
	test_stat_mode(0077);

	return (0);
}
