/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
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

#include_next <sys/mount.h>

#ifndef _LIBSPL_SYS_MOUNT_H
#define _LIBSPL_SYS_MOUNT_H

#include <sys/mntent.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

/*
 * Some old glibc headers don't define BLKGETSIZE64
 * and we don't want to require the kernel headers
 */
#if !defined(BLKGETSIZE64)
#define BLKGETSIZE64		_IOR(0x12, 114, size_t)
#endif

/*
 * Some old glibc headers don't correctly define MS_DIRSYNC and
 * instead use the enum name S_WRITE.  When using these older
 * headers define MS_DIRSYNC to be S_WRITE.
 */
#if !defined(MS_DIRSYNC)
#define MS_DIRSYNC		S_WRITE
#endif

#define	MS_USERS	0x40000000
#define	MS_OWNER	0x10000000
#define	MS_GROUP	0x08000000
#define	MS_COMMENT	0x02000000

/*
 * Older glibc <sys/mount.h> headers did not define all the available
 * umount2(2) flags.  Both MNT_FORCE and MNT_DETACH are supported in the
 * kernel back to 2.4.11 so we define them correctly if they are missing.
 */
#ifdef MNT_FORCE
# define MS_FORCE	MNT_FORCE
#else
# define MS_FORCE	0x00000001
#endif /* MNT_FORCE */

#ifdef MNT_DETACH
# define MS_DETACH	MNT_DETACH
#else
# define MS_DETACH	0x00000002
#endif /* MNT_DETACH */

/*
 * Overlay mount is default in Linux, but for solaris/zfs
 * compatibility, MS_OVERLAY is defined to explicitly have the user
 * provide a flag (-O) to mount over a non empty directory.
 */
#define MS_OVERLAY      0x00000004

#endif /* _LIBSPL_SYS_MOUNT_H */
