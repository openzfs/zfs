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
 * Copyright (c) 2019 by Tomohiro Kusumi. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>

int
main(int argc, char *argv[])
{
	const char *name, *phase;
	mode_t extra;
	struct stat st;

	if (argc < 3) {
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

	const char *testdir = getenv("TESTDIR");
	if (!testdir) {
		fprintf(stderr, "getenv(TESTDIR)\n");
		exit(1);
	}

	umask(0);
	if (stat(testdir, &st) == -1 && mkdir(testdir, 0777) == -1) {
		perror("mkdir");
		exit(2);
	}

	char fpath[1024];
	snprintf(fpath, sizeof (fpath), "%s/%s", testdir, name);


	phase = argv[2];
	if (strcmp(phase, "PRECRASH") == 0) {

		/* clean up last run */
		unlink(fpath);
		if (stat(fpath, &st) == 0) {
			fprintf(stderr, "%s exists\n", fpath);
			exit(3);
		}

		int fd;

		fd = creat(fpath, 0777 | extra);
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

		const char buf[] = "test";
		if (write(fd, buf, sizeof (buf)) == -1) {
			perror("write");
			exit(7);
		}
		close(fd);

	} else if (strcmp(phase, "REPLAY") == 0) {
		/* created in PRECRASH run */
	} else {
		fprintf(stderr, "Invalid phase %s\n", phase);
		exit(1);
	}

	if (stat(fpath, &st) == -1) {
			perror("stat");
			exit(8);
		}

	/* Verify SUID/SGID are dropped */
	mode_t res = st.st_mode & (0777 | S_ISUID | S_ISGID);
	if (res != 0777) {
		fprintf(stderr, "stat(2) %o\n", res);
		exit(9);
	}

	return (0);
}
