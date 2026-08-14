// SPDX-License-Identifier: CDDL-1.0
#ifndef _LIBSPL_SYS_MOUNT_H
#define	_LIBSPL_SYS_MOUNT_H

#undef _SYS_MOUNT_H_
#include_next <sys/mount.h>

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <sys/param.h>

#ifndef MS_NOSUID
#define	MS_NOSUID	MNT_NOSUID
#endif
#ifndef MS_NOEXEC
#define	MS_NOEXEC	MNT_NOEXEC
#endif
#define	MS_NODEV	0
#define	S_WRITE		0
#define	MS_BIND		0
#ifndef MS_REMOUNT
#define	MS_REMOUNT	MNT_UPDATE
#endif
#ifndef MS_RDONLY
#define	MS_RDONLY	MNT_RDONLY
#endif
#ifndef MS_SYNCHRONOUS
#define	MS_SYNCHRONOUS	MNT_SYNCHRONOUS
#endif

#define	MS_USERS	(MS_NOEXEC|MS_NOSUID|MS_NODEV)
#define	MS_OWNER	(MS_NOSUID|MS_NODEV)
#define	MS_GROUP	(MS_NOSUID|MS_NODEV)
#define	MS_COMMENT	0

#ifdef MNT_FORCE
#define	MS_FORCE	MNT_FORCE
#else
#define	MS_FORCE	0x00000001
#endif

#ifdef MNT_DETACH
#define	MS_DETACH	MNT_DETACH
#else
#define	MS_DETACH	0
#endif

#ifndef MNAMELEN
#define	MNAMELEN	90
#endif

#ifndef	ZFS_MNAMELEN
#ifdef	_VFS_MNAMELEN
#define	ZFS_MNAMELEN	_VFS_MNAMELEN
#else
#define	ZFS_MNAMELEN	1024
#endif
#endif

/*
 * libzfs-internal flags.  These collide with NetBSD MNT_SYMPERM
 * (0x20000000) and MNT_NODEVMTIME (0x40000000).  do_mount() builds
 * mntflags from scratch; do_unmount() forwards only MS_FORCE and
 * MS_DETACH and must not pass these bits to unmount(2).
 *
 * There is no MS_ENOENTOK.  unmount(2) namei's the mountpoint, so a
 * directory removed by receive -F or rename returns ENOENT while the
 * mount is still on the kernel list.  Linux umount2(2) still reaches
 * that mount.  The NetBSD kernel needs the same lookup.  Do not add
 * an unmount ioctl, and do not treat ENOENT as success: destroy must
 * not proceed against a filesystem that is still mounted.
 */
#define	MS_OVERLAY	0x20000000
#define	MS_CRYPT	0x40000000

/*
 * Argument block passed to mount(2) for fstype "zfs".
 * Must match the kernel-side zfsvfs mount parser.
 * Mount paths are _VFS_MNAMELEN (1024), not MNAMELEN/90 or _VFS_NAMELEN/32.
 */
struct zfs_args {
	char	fspec[MAXPATHLEN];
	int	mflag;
	char	optptr[MAXPATHLEN];
	int	optlen;
	char	mntonname[ZFS_MNAMELEN];
};
typedef struct zfs_args zfs_args_t;

#endif /* _LIBSPL_SYS_MOUNT_H */
