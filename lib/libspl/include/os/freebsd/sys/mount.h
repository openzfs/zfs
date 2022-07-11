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
 * Overlay mount is default in Linux, but for solaris/zfs
 * compatibility, MS_OVERLAY is defined to explicitly have the user
 * provide a flag (-O) to mount over a non empty directory.
 */
#define	MS_OVERLAY	0x00000004

/*
 * MS_CRYPT indicates that encryption keys should be loaded if they are not
 * already available. This is not defined in glibc, but it is never seen by
 * the kernel so it will not cause any problems.
 */
#define	MS_CRYPT	0x00000008

#endif /* _LIBSPL_SYS_MOUNT_H */
