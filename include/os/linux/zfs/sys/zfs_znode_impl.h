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
/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright 2016 Nexenta Systems, Inc. All rights reserved.
 */

#ifndef	_SYS_ZFS_ZNODE_IMPL_H
#define	_SYS_ZFS_ZNODE_IMPL_H

#ifndef _KERNEL
#error "no user serviceable parts within"
#endif

#include <sys/isa_defs.h>
#include <sys/types32.h>
#include <sys/list.h>
#include <sys/dmu.h>
#include <sys/sa.h>
#include <sys/zfs_vfsops.h>
#include <sys/rrwlock.h>
#include <sys/zfs_sa.h>
#include <sys/zfs_stat.h>
#include <sys/zfs_rlock.h>


#ifdef	__cplusplus
extern "C" {
#endif

#define	ZNODE_OS_FIELDS			\
	struct inode	z_inode;


/*
 * Convert between znode pointers and inode pointers
 */
#define	ZTOI(znode)	(&((znode)->z_inode))
#define	ITOZ(inode)	(container_of((inode), znode_t, z_inode))
#define	ZTOZSB(znode)	((zfsvfs_t *)(ZTOI(znode)->i_sb->s_fs_info))
#define	ITOZSB(inode)	((zfsvfs_t *)((inode)->i_sb->s_fs_info))

#define	ZTOTYPE(zp)	(ZTOI(zp)->i_mode)
#define	ZTOGID(zp) (ZTOI(zp)->i_gid)
#define	ZTOUID(zp) (ZTOI(zp)->i_uid)
#define	ZTONLNK(zp) (ZTOI(zp)->i_nlink)

#define	Z_ISBLK(type) S_ISBLK(type)
#define	Z_ISCHR(type) S_ISCHR(type)
#define	Z_ISLNK(type) S_ISLNK(type)
#define	Z_ISDEV(type)	(S_ISCHR(type) || S_ISBLK(type) || S_ISFIFO(type))

#define	zhold(zp)	igrab(ZTOI((zp)))
#define	zrele(zp)	iput(ZTOI((zp)))

/* Called on entry to each ZFS inode and vfs operation. */
#define	ZFS_ENTER_ERROR(zfsvfs, error)				\
do {								\
	rrm_enter_read(&(zfsvfs)->z_teardown_lock, FTAG);	\
	if ((zfsvfs)->z_unmounted) {				\
		ZFS_EXIT(zfsvfs);				\
		return (error);					\
	}							\
} while (0)
#define	ZFS_ENTER(zfsvfs)	ZFS_ENTER_ERROR(zfsvfs, EIO)
#define	ZPL_ENTER(zfsvfs)	ZFS_ENTER_ERROR(zfsvfs, -EIO)

/* Must be called before exiting the operation. */
#define	ZFS_EXIT(zfsvfs)					\
do {								\
	zfs_exit_fs(zfsvfs);					\
	rrm_exit(&(zfsvfs)->z_teardown_lock, FTAG);		\
} while (0)

#define	ZPL_EXIT(zfsvfs)					\
do {								\
	rrm_exit(&(zfsvfs)->z_teardown_lock, FTAG);		\
} while (0)

/* Verifies the znode is valid. */
#define	ZFS_VERIFY_ZP_ERROR(zp, error)				\
do {								\
	if ((zp)->z_sa_hdl == NULL) {				\
		ZFS_EXIT(ZTOZSB(zp));				\
		return (error);					\
	}							\
} while (0)
#define	ZFS_VERIFY_ZP(zp)	ZFS_VERIFY_ZP_ERROR(zp, EIO)
#define	ZPL_VERIFY_ZP(zp)	ZFS_VERIFY_ZP_ERROR(zp, -EIO)

/*
 * Macros for dealing with dmu_buf_hold
 */
#define	ZFS_OBJ_MTX_SZ		64
#define	ZFS_OBJ_MTX_MAX		(1024 * 1024)
#define	ZFS_OBJ_HASH(zfsvfs, obj)	((obj) & ((zfsvfs->z_hold_size) - 1))

extern unsigned int zfs_object_mutex_size;

/*
 * Encode ZFS stored time values from a struct timespec / struct timespec64.
 */
#define	ZFS_TIME_ENCODE(tp, stmp)		\
do {						\
	(stmp)[0] = (uint64_t)(tp)->tv_sec;	\
	(stmp)[1] = (uint64_t)(tp)->tv_nsec;	\
} while (0)

#if defined(HAVE_INODE_TIMESPEC64_TIMES)
/*
 * Decode ZFS stored time values to a struct timespec64
 * 4.18 and newer kernels.
 */
#define	ZFS_TIME_DECODE(tp, stmp)		\
do {						\
	(tp)->tv_sec = (time64_t)(stmp)[0];	\
	(tp)->tv_nsec = (long)(stmp)[1];	\
} while (0)
#else
/*
 * Decode ZFS stored time values to a struct timespec
 * 4.17 and older kernels.
 */
#define	ZFS_TIME_DECODE(tp, stmp)		\
do {						\
	(tp)->tv_sec = (time_t)(stmp)[0];	\
	(tp)->tv_nsec = (long)(stmp)[1];	\
} while (0)
#endif /* HAVE_INODE_TIMESPEC64_TIMES */

struct znode;

extern int	zfs_sync(struct super_block *, int, cred_t *);
extern int	zfs_inode_alloc(struct super_block *, struct inode **ip);
extern void	zfs_inode_destroy(struct inode *);
extern void	zfs_inode_update(struct znode *);
extern void	zfs_mark_inode_dirty(struct inode *);
extern boolean_t zfs_relatime_need_update(const struct inode *);

#if defined(HAVE_UIO_RW)
extern caddr_t zfs_map_page(page_t *, enum seg_rw);
extern void zfs_unmap_page(page_t *, caddr_t);
#endif /* HAVE_UIO_RW */

extern zil_get_data_t zfs_get_data;
extern zil_replay_func_t *zfs_replay_vector[TX_MAX_TYPE];
extern int zfsfstype;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ZFS_ZNODE_IMPL_H */
