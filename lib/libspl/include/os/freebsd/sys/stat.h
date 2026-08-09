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
