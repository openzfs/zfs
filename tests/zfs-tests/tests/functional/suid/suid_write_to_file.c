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
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

static void
test_stat_mode(mode_t extra)
{
	struct stat st;
	int i, fd;
	char fpath[1024];
	char *penv[] = {"TESTDIR", "TESTFILE0"};
	char buf[] = "test";
	mode_t res;
	mode_t mode = 0777 | extra;

	/*
	 * Get the environment variable values.
	 */
	for (i = 0; i < sizeof (penv) / sizeof (char *); i++) {
		if ((penv[i] = getenv(penv[i])) == NULL) {
			fprintf(stderr, "getenv(penv[%d])\n", i);
			exit(1);
		}
	}

	umask(0);
	if (stat(penv[0], &st) == -1 && mkdir(penv[0], mode) == -1) {
		perror("mkdir");
		exit(2);
	}

	snprintf(fpath, sizeof (fpath), "%s/%s", penv[0], penv[1]);
	unlink(fpath);
	if (stat(fpath, &st) == 0) {
		fprintf(stderr, "%s exists\n", fpath);
		exit(3);
	}

	fd = creat(fpath, mode);
	if (fd == -1) {
		perror("creat");
		exit(4);
	}
	close(fd);

	if (setuid(65534) == -1) {
		perror("setuid");
		exit(5);
	}

	fd = open(fpath, O_RDWR);
	if (fd == -1) {
		perror("open");
		exit(6);
	}

	if (write(fd, buf, sizeof (buf)) == -1) {
		perror("write");
		exit(7);
	}
	close(fd);

	if (stat(fpath, &st) == -1) {
		perror("stat");
		exit(8);
	}
	unlink(fpath);

	/* Verify SUID/SGID are dropped */
	res = st.st_mode & (0777 | S_ISUID | S_ISGID);
	if (res != (mode & 0777)) {
		fprintf(stderr, "stat(2) %o\n", res);
		exit(9);
	}
}

int
main(int argc, char *argv[])
{
	const char *name;
	mode_t extra;

	if (argc < 2) {
		fprintf(stderr, "Invalid argc\n");
		exit(1);
	}

	name = argv[1];
	if (strcmp(name, "SUID") == 0) {
		extra = S_ISUID;
	} else if (strcmp(name, "SGID") == 0) {
		extra = S_ISGID;
	} else if (strcmp(name, "SUID_SGID") == 0) {
		extra = S_ISUID | S_ISGID;
	} else if (strcmp(name, "NONE") == 0) {
		extra = 0;
	} else {
		fprintf(stderr, "Invalid name %s\n", name);
		exit(1);
	}

	test_stat_mode(extra);

	return (0);
}
