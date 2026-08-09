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
