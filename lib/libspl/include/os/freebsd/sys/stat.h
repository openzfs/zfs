// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _LIBSPL_SYS_STAT_H
#define	_LIBSPL_SYS_STAT_H

#include_next <sys/stat.h>

/* Note: this file can be used on linux/macOS when bootstrapping tools. */

#if defined(__FreeBSD__)
#include <sys/mount.h> /* for BLKGETSIZE64 */

#define	stat64	stat

#define	MAXOFFSET_T	OFF_MAX

#ifndef _KERNEL
#include <sys/disk.h>

static __inline int
fstat64(int fd, struct stat *sb)
{
	int ret;

	ret = fstat(fd, sb);
	if (ret == 0) {
		if (S_ISCHR(sb->st_mode))
			(void) ioctl(fd, DIOCGMEDIASIZE, &sb->st_size);
	}
	return (ret);
}
#endif

/*
 * Emulate Solaris' behavior of returning the block device size in fstat64().
 */
static inline int
fstat64_blk(int fd, struct stat64 *st)
{
	if (fstat64(fd, st) == -1)
		return (-1);

	/* In Linux we need to use an ioctl to get the size of a block device */
	if (S_ISBLK(st->st_mode)) {
		if (ioctl(fd, BLKGETSIZE64, &st->st_size) != 0)
			return (-1);
	}

	return (0);
}
#endif /* defined(__FreeBSD__) */

/*
 * Only Intel-based Macs have a separate stat64; Arm-based Macs are like
 * FreeBSD and have a full 64-bit stat from the start.
 *
 * On Linux, musl libc is full 64-bit too and has deprecated its own version
 * of these defines since version 1.2.4.
 */
#if (defined(__APPLE__) && !(defined(__i386__) || defined(__x86_64__))) || \
	(defined(__linux__) && !defined(__GLIBC__))
#define	stat64	stat
#define	fstat64	fstat
#endif

#endif /* _LIBSPL_SYS_STAT_H */
