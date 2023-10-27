/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
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

#ifndef _SYS_ZFS_MOUNT_H_
#define	_SYS_ZFS_MOUNT_H_

#include <sys/mount.h>

struct zfs_mount_args {
	const char	*fspec;
	int			mflag;
	const char		*optptr;
	int			optlen;
	int			struct_size;
};


/*
 * Maximum option string length accepted or returned by mount(2).
 */
#define	MAX_MNTOPT_STR	1024	/* max length of mount options string */

#ifdef _KERNEL
#define	MS_RDONLY MNT_RDONLY
#define	MS_NOEXEC MNT_NOEXEC
#define	MS_NOSUID MNT_NOSUID
#define	MS_NODEV  MNT_NODEV
#define	MS_BIND   0
#define	MS_REMOUNT MNT_UPDATE
#define	MS_SYNCHRONOUS MNT_SYNCHRONOUS
#define	MS_USERS (MS_NOEXEC|MS_NOSUID|MS_NODEV)
#define	MS_OWNER (MS_NOSUID|MS_NODEV)
#define	MS_GROUP (MS_NOSUID|MS_NODEV)
#define	MS_COMMENT 0
#define	MS_FORCE MNT_FORCE
#define	MS_DETACH MNT_DETACH
#define	MS_OVERLAY MNT_UNION
#define	MS_CRYPT MNT_CPROTECT
#endif

#endif	/* _SYS_ZFS_IOCTL_H */
