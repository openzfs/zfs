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
 * Copyright (c) 1988, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/* Copyright (c) 1983, 1984, 1985, 1986, 1987, 1988, 1989 AT&T */
/* All Rights Reserved */

/*
 * Portions of this source code were derived from Berkeley 4.3 BSD
 * under license from the Regents of the University of California.
 */

#ifndef _SPL_ZFS_H
#define	_SPL_ZFS_H

#include <sys/attr.h>
#include <sys/mount.h>

#define	MAXFIDSZ	64

typedef struct mount vfs_t;

#define	vn_vfswlock(vp) (0)
#define	vn_vfsunlock(vp)
#define	VFS_HOLD(vfsp)
#define	VFS_RELE(vfsp)



/*
 * File identifier.  Should be unique per filesystem on a single
 * machine.  This is typically called by a stateless file server
 * in order to generate "file handles".
 *
 * Do not change the definition of struct fid ... fid_t without
 * letting the CacheFS group know about it!  They will have to do at
 * least two things, in the same change that changes this structure:
 *   1. change CFSVERSION in usr/src/uts/common/sys/fs/cachefs_fs.h
 *   2. put the old version # in the canupgrade array
 *      in cachfs_upgrade() in usr/src/cmd/fs.d/cachefs/fsck/fsck.c
 * This is necessary because CacheFS stores FIDs on disk.
 *
 * Many underlying file systems cast a struct fid into other
 * file system dependent structures which may require 4 byte alignment.
 * Because a fid starts with a short it may not be 4 byte aligned, the
 * fid_pad will force the alignment.
 */
#define	MAXFIDSZ		64
#define	OLD_MAXFIDSZ	16

typedef struct fid {
	union {
		long fid_pad;
		struct {
			ushort_t len;   /* length of data in bytes */
			char    data[MAXFIDSZ]; /* data (variable len) */
		} _fid;
	} un;
} fid_t;


extern void (*mountroot_post_hook)(void);

#endif /* SPL_ZFS_H */
