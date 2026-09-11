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
