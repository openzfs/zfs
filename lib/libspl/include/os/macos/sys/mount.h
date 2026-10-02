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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */


#ifndef _LIBSPL_SYS_MOUNT_H
#define	_LIBSPL_SYS_MOUNT_H

#undef _SYS_MOUNT_H_

#include <sys/vnode.h>
#include <sys/mntent.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

/* Unfortunately, XNU has a different meaning for "vnode_t". */
#undef vnode_t
#include_next <sys/mount.h>
#define	vnode_t struct vnode

/*
 * Some old glibc headers don't define BLKGETSIZE64
 * and we don't want to require the kernel headers
 */
#if !defined(BLKGETSIZE64)
#define	BLKGETSIZE64		_IOR(0x12, 114, size_t)
#endif

/*
 * Some old glibc headers don't correctly define MS_DIRSYNC and
 * instead use the enum name S_WRITE.  When using these older
 * headers define MS_DIRSYNC to be S_WRITE.
 */
#if !defined(MS_DIRSYNC)
#define	MS_DIRSYNC		S_WRITE
#endif

/*
 * Some old glibc headers don't correctly define MS_POSIXACL and
 * instead leave it undefined.  When using these older headers define
 * MS_POSIXACL to the reserved value of (1<<16).
 */
#if !defined(MS_POSIXACL)
#define	MS_POSIXACL		(1<<16)
#endif

#define	MS_NOSUID	MNT_NOSUID
#define	MS_NOEXEC	MNT_NOEXEC
#define	MS_NODEV	MNT_NODEV
#define	S_WRITE		0
#define	MS_BIND		0
#define	MS_REMOUNT	MNT_UPDATE
#define	MS_SYNCHRONOUS	MNT_SYNCHRONOUS

#define	MS_USERS	(MS_NOEXEC|MS_NOSUID|MS_NODEV)
#define	MS_OWNER	(MS_NOSUID|MS_NODEV)
#define	MS_GROUP	(MS_NOSUID|MS_NODEV)
#define	MS_COMMENT	0
#define	MS_FORCE	MNT_FORCE
#define	MS_DETACH	MNT_DETACH
#define	MS_OVERLAY	MNT_UNION
#define	MS_CRYPT	MNT_CPROTECT

#endif /* _LIBSPL_SYS_MOUNT_H */
