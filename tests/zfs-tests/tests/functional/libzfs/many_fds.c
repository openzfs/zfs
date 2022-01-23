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
 * Copyright (C) 2015 STRATO AG.
 */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libzfs.h>
#include <sys/resource.h>
#include <errno.h>

/*
 * Check if libzfs works with more than 255 held file handles.
 */
int
main(void)
{
	int i;
	struct rlimit limit;
	libzfs_handle_t *h;

	limit.rlim_cur = 65535;
	limit.rlim_max = 65535;

	if (setrlimit(RLIMIT_NOFILE, &limit) != 0) {
		(void) printf("many_fds: setrlimit() failed with errno=%d\n",
		    errno);
		exit(1);
	}

	for (i = 0; i < 255; ++i) {
		int fd = open("/dev/null", O_RDONLY);
		if (fd == -1) {
			(void) printf("open failed with errno=%d\n", errno);
			return (1);
		}
	}

	h = libzfs_init();

	if (h != NULL) {
		libzfs_fini(h);
		return (0);
	} else {
		(void) printf("many_fds: libzfs_init() failed with errno=%d\n",
		    errno);
		return (1);
	}
}
