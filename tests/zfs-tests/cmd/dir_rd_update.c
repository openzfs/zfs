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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Assertion:
 *
 *	A read operation and directory update operation performed
 *      concurrently on the same directory can lead to deadlock
 *	on a UFS logging file system, but not on a ZFS file system.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#define	TMP_DIR /tmp

static char dirpath[256];

int
main(int argc, char **argv)
{
	const char *cp1 = "";
	int i = 0;
	int ret = 0;
	int testdd = 0;
	pid_t pid;
	static const int op_num = 5;

	if (argc == 1) {
		(void) printf("Usage: %s <mount point>\n", argv[0]);
		exit(-1);
	}
	for (i = 0; i < 256; i++) {
		dirpath[i] = 0;
	}

	cp1 = argv[1];
	if (strlen(cp1) >= (sizeof (dirpath) - strlen("/TMP_DIR"))) {
		(void) printf("The string length of mount point is "
		    "too large\n");
		exit(-1);
	}
	(void) snprintf(dirpath, sizeof (dirpath), "%s/TMP_DIR", cp1);

	ret = mkdir(dirpath, 0777);
	if (ret != 0) {
		if (errno != EEXIST) {
			(void) printf("%s: mkdir(<%s>, 0777) failed: errno "
			    "(decimal)=%d\n", argv[0], dirpath, errno);
			exit(-1);
		}
	}
	testdd = open(dirpath, O_RDONLY|O_RSYNC|O_SYNC|O_DSYNC);
	if (testdd < 0) {
		(void) printf("%s: open(<%s>, O_RDONLY|O_RSYNC|O_SYNC|O_DSYNC)"
		    " failed: errno (decimal)=%d\n", argv[0], dirpath, errno);
		exit(-1);
	} else {
		(void) close(testdd);
	}
	pid = fork();
	if (pid > 0) {
		int fd = open(dirpath, O_RDONLY|O_RSYNC|O_SYNC|O_DSYNC);
		char buf[16];
		int rdret;
		int j = 0;

		if (fd < 0) {
			(void) printf("%s: open <%s> again failed:"
			    " errno = %d\n", argv[0], dirpath, errno);
			exit(-1);
		}

		while (j < op_num) {
			(void) sleep(1);
			rdret = read(fd, buf, 16);
			if (rdret == -1) {
				(void) printf("readdir failed");
			}
			j++;
		}
		(void) close(fd);
	} else if (pid == 0) {
		int fd = open(dirpath, O_RDONLY);
		int chownret;
		int k = 0;

		if (fd < 0) {
			(void) printf("%s: open(<%s>, O_RDONLY) again failed:"
			    " errno (decimal)=%d\n", argv[0], dirpath, errno);
			exit(-1);
		}

		while (k < op_num) {
			(void) sleep(1);
			chownret = fchown(fd, 0, 0);
			if (chownret == -1) {
				(void) printf("chown failed");
			}

			k++;
		}
		(void) close(fd);
	}

	return (0);
}
