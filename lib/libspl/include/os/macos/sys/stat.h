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

#include <sys/stdtypes.h>
#include <sys/disk.h>
#include <sys/mount.h> /* for BLKGETSIZE64 */

#define	MAXOFFSET_T	OFF_MAX

#ifndef _KERNEL
#include <sys/disk.h>
#endif

static inline int
fstat_blk(int fd, struct stat *st)
{
	if (fstat(fd, st) == -1)
		return (-1);

	/* In OS X we need to use ioctl to get the size of a block dev */
	if (st->st_mode & (S_IFBLK | S_IFCHR)) {
		uint32_t blksize;
		uint64_t blkcnt;

		if (ioctl(fd, DKIOCGETBLOCKSIZE, &blksize) < 0) {
			return (-1);
		}
		if (ioctl(fd, DKIOCGETBLOCKCOUNT, &blkcnt) < 0) {
			return (-1);
		}

		st->st_size = (off_t)((uint64_t)blksize * blkcnt);
	}

	return (0);
}


/*
 * Deal with Linux use of 64 for everything.
 * OsX has moved past it, dropped all 32 versions, and
 * standard form is 64 bit.
 */

#define	stat64		stat
#define	lstat64		lstat
#define	fstat64		fstat
#define	fstat64_blk	fstat_blk
#define	statfs64	statfs

#endif /* _LIBSPL_SYS_STAT_H */
