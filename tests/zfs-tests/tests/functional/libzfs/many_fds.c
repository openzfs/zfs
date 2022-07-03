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
 * Copyright (C) 2015 STRATO AG.
 */
#include <err.h>
#include <fcntl.h>
#include <libzfs.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/*
 * Check if libzfs works with more than 255 held file handles.
 */
int
main(void)
{
	struct rlimit limit = {
		.rlim_cur = 64 * 1024,
		.rlim_max = 64 * 1024,
	};
	if (setrlimit(RLIMIT_NOFILE, &limit) != 0)
		err(1, "setrlimit()");

	int fd = open("/dev/null", O_RDONLY);
	if (fd == -1)
			err(1, "open()");
	for (int i = 0; i < limit.rlim_cur / 2; ++i)
		if (dup(fd) == -1)
			err(1, "dup()");

	libzfs_handle_t *h = libzfs_init();
	if (h == NULL)
		err(1, "libzfs_init()");

	libzfs_fini(h);
}
