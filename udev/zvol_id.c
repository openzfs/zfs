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

	char zvol_name[MAXNAMELEN + strlen("-part") + 10];
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
