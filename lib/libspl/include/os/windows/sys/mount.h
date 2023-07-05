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

#ifndef _LIBSPL_SYS_MOUNT_H
#define	_LIBSPL_SYS_MOUNT_H

#include <sys/vnode.h>
#include <sys/mntent.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>

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

#define	MS_USERS	(MS_NOEXEC|MS_NOSUID|MS_NODEV)
#define	MS_OWNER	(MS_NOSUID|MS_NODEV)
#define	MS_GROUP	(MS_NOSUID|MS_NODEV)
#define	MS_COMMENT	0

#include <sys/zfs_mount.h>


#ifdef __LINUX__
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

#define	MS_RDONLY	0x0001	/* Read-only */
#define	MS_OPTIONSTR	0x0100	/* Data is an in/out option string */
#define	MS_NOMNTTAB	0x0800	/* Don't show mount in mnttab */
#endif /* __LINUX__ */
/*
 * MS_CRYPT indicates that encryption keys should be loaded if they are not
 * already available. This is not defined in glibc, but it is never seen by
 * the kernel so it will not cause any problems.
 */
#define	MS_CRYPT	0x00000008

#define	MFSTYPENAMELEN	16

struct statfs {
	uint32_t	f_bsize;	/* fundamental file system block size */
	uint64_t	f_blocks;	/* total data blocks in file system */
	uint64_t	f_bfree;	/* free blocks in fs */
	uint64_t	f_bavail;	/* free blocks avail to non-superuser */
	uint64_t	f_files;	/* total file nodes in file system */
	uint64_t	f_ffree;	/* free file nodes in fs */
	uint32_t	f_type;		/* type of filesystem */
	uint32_t	f_flags;	/* copy of mount exported flags */
	uint32_t	f_fssubtype;	/* fs sub-type (flavor) */
	char		f_fstypename[MFSTYPENAMELEN];	/* fs type name */
	char		f_mntonname[MAXPATHLEN]; /* dir on which mounted */
	char		f_mntfromname[MAXPATHLEN];	/* mounted filesystem */
};

int statfs(const char *path, struct statfs *buf);

#endif /* _LIBSPL_SYS_MOUNT_H */
