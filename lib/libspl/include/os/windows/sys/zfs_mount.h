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

struct zfs_mount_args {
	const char	*fspec; /* block special device to mount */
	int		mflag;
	char	*optptr;
	int		optlen;
	int struct_size;
};

/*
 * Flag bits passed to mount(2).
 */
#define	MS_RDONLY	0x0001	/* Read-only */
#define	MS_FSS		0x0002	/* Old (4-argument) mount (compatibility) */
#define	MS_DATA		0x0004	/* 6-argument mount */
#define	MS_NOSUID	0x0010	/* Setuid programs disallowed */
#define	MS_REMOUNT	0x0020	/* Remount */
#define	MS_NOTRUNC	0x0040	/* Return ENAMETOOLONG for long filenames */
#define	MS_OVERLAY	0x0080	/* Allow overlay mounts */
#define	MS_OPTIONSTR	0x0100	/* Data is a an in/out option string */
#define	MS_GLOBAL	0x0200	/* Clustering: Mount into global name space */
#define	MS_FORCE	0x0400	/* Forced unmount */
#define	MS_NOMNTTAB	0x0800	/* Don't show mount in mnttab */
/*
 * Additional flag bits that domount() is prepared to interpret, but that
 * can't be passed through mount(2).
 */
#define	MS_SYSSPACE	0x0008	/* Mounta already in kernel space */
#define	MS_NOSPLICE	0x1000	/* Don't splice fs instance into name space */
#define	MS_NOCHECK	0x2000	/* Clustering: suppress mount busy checks */
/*
 * Mask to sift out flag bits allowable from mount(2).
 */
#define	MS_MASK	\
	(MS_RDONLY|MS_FSS|MS_DATA|MS_NOSUID|MS_REMOUNT|MS_NOTRUNC|MS_OVERLAY|\
	    MS_OPTIONSTR|MS_GLOBAL|MS_NOMNTTAB)

/*
 * Mask to sift out flag bits allowable from umount2(2).
 */

#define	MS_UMOUNT_MASK	(MS_FORCE)

/*
 * Maximum option string length accepted or returned by mount(2).
 */
#define	MAX_MNTOPT_STR	1024	/* max length of mount options string */


#endif	/* _SYS_ZFS_IOCTL_H */
