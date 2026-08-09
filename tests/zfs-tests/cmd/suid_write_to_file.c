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
