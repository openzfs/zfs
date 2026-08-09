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
#include_next <sys/mount.h>

#include <assert.h>
#include <string.h>
#include <stdlib.h>

#if !defined(BLKGETSIZE64)
#define	BLKGETSIZE64		DIOCGMEDIASIZE
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
#define	MS_NODEV	0
#define	S_WRITE		0
#define	MS_BIND		0
#define	MS_REMOUNT	0
#define	MS_SYNCHRONOUS	MNT_SYNCHRONOUS

#define	MS_USERS	(MS_NOEXEC|MS_NOSUID|MS_NODEV)
#define	MS_OWNER	(MS_NOSUID|MS_NODEV)
#define	MS_GROUP	(MS_NOSUID|MS_NODEV)
#define	MS_COMMENT	0

/*
 * Older glibc <sys/mount.h> headers did not define all the available
 * umount2(2) flags.  Both MNT_FORCE and MNT_DETACH are supported in the
 * kernel back to 2.4.11 so we define them correctly if they are missing.
 */
#ifdef MNT_FORCE
#define	MS_FORCE	MNT_FORCE
#else
#define	MS_FORCE	0x00000001
#endif /* MNT_FORCE */

#ifdef MNT_DETACH
#define	MS_DETACH	MNT_DETACH
#else
#define	MS_DETACH	0x00000002
#endif /* MNT_DETACH */

/*
 * MS_OVERLAY (-O: allow mounting over a non-empty directory) and MS_CRYPT
 * (load/unload encryption keys with the (un)mount) are libzfs-internal flags,
 * never passed to the kernel.  They use high bits that do_unmount() masks
 * off before unmount(2); do_mount() builds its iovec from named options.
 */
#define	MS_OVERLAY	0x20000000
#define	MS_CRYPT	0x40000000

#endif /* _LIBSPL_SYS_MOUNT_H */
