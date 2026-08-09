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
 * Copyright (c) 2011, Fajar A. Nugraha.  All rights reserved.
 * Use is subject to license terms.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/fs/zfs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#if defined(ZFS_ASAN_ENABLED)
/*
 * zvol_id is invoked by udev with the help of ptrace()
 * making sanitized binary with leak detection croak
 * because of tracing mechanisms collision
 */
extern const char *__asan_default_options(void);

const char *__asan_default_options(void) {
	return ("abort_on_error=true:halt_on_error=true:"
		"allocator_may_return_null=true:disable_coredump=false:"
		"detect_stack_use_after_return=true:detect_leaks=false");
}
#endif

int
main(int argc, const char *const *argv)
{
	if (argc != 2 || strncmp(argv[1], "/dev/zd", 7) != 0) {
		fprintf(stderr, "usage: %s /dev/zdX\n", argv[0]);
		return (1);
	}
	const char *dev_name = argv[1];
	size_t i, len;

	int fd;
	struct stat sb;
	if ((fd = open(dev_name, O_RDONLY|O_CLOEXEC)) == -1 ||
	    fstat(fd, &sb) != 0) {
		fprintf(stderr, "%s: %s\n", dev_name, strerror(errno));
		return (1);
	}

	char zvol_name[MAXNAMELEN+15];
	if (ioctl(fd, BLKZNAME, zvol_name) == -1) {
		fprintf(stderr, "%s: BLKZNAME: %s\n",
		    dev_name, strerror(errno));
		return (1);
	}

	const char *dev_part = strrchr(dev_name, 'p');
	len = strlen(zvol_name);
	if (dev_part != NULL) {
		sprintf(zvol_name + len, "-part%s", dev_part + 1);
		len = strlen(zvol_name);
	}

	for (i = 0; i < len; ++i)
		if (isblank(zvol_name[i]))
			zvol_name[i] = '+';

	puts(zvol_name);

	return (0);
}
