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
 * Copyright (c) 2012, 2015 by Delphix. All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 * Copyright 2016 Nexenta Systems, Inc. All rights reserved.
 */

#ifndef	_FREEBSD_ZFS_SYS_ZNODE_IMPL_H
#define	_FREEBSD_ZFS_SYS_ZNODE_IMPL_H

#include <sys/list.h>
#include <sys/dmu.h>
#include <sys/sa.h>
#include <sys/zfs_vfsops.h>
#include <sys/rrwlock.h>
#include <sys/zfs_sa.h>
#include <sys/zfs_stat.h>
#include <sys/zfs_rlock.h>
#include <sys/zfs_acl.h>
#include <sys/zil.h>
#include <sys/zfs_project.h>
#include <vm/vm_object.h>
#include <sys/uio.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Directory entry locks control access to directory entries.
 * They are used to protect creates, deletes, and renames.
 * Each directory znode has a mutex and a list of locked names.
 */
#define	ZNODE_OS_FIELDS                 \
	struct zfsvfs	*z_zfsvfs;      \
	vnode_t		*z_vnode;       \
	char		*z_cached_symlink;	\
	uint64_t		z_uid;          \
	uint64_t		z_gid;          \
	uint64_t		z_gen;          \
	uint64_t		z_atime[2];     \
	uint64_t		z_links;

#define	ZFS_LINK_MAX	UINT64_MAX

/*
 * ZFS minor numbers can refer to either a control device instance or
 * a zvol. Depending on the value of zss_type, zss_data points to either
 * a zvol_state_t or a zfs_onexit_t.
 */
enum zfs_soft_state_type {
	ZSST_ZVOL,
	ZSST_CTLDEV
};

typedef struct zfs_soft_state {
	enum zfs_soft_state_type zss_type;
	void *zss_data;
} zfs_soft_state_t;

extern minor_t zfsdev_minor_alloc(void);

/*
 * Range locking rules
 * --------------------
 * 1. When truncating a file (zfs_create, zfs_setattr, zfs_space) the whole
 *    file range needs to be locked as RL_WRITER. Only then can the pages be
 *    freed etc and zp_size reset. zp_size must be set within range lock.
 * 2. For writes and punching holes (zfs_write & zfs_space) just the range
 *    being written or freed needs to be locked as RL_WRITER.
 *    Multiple writes at the end of the file must coordinate zp_size updates
 *    to ensure data isn't lost. A compare and swap loop is currently used
 *    to ensure the file size is at least the offset last written.
 * 3. For reads (zfs_read, zfs_get_data & zfs_putapage) just the range being
 *    read needs to be locked as RL_READER. A check against zp_size can then
 *    be made for reading beyond end of file.
 */

/*
 * Convert between znode pointers and vnode pointers
 */
#define	ZTOV(ZP)	((ZP)->z_vnode)
#define	ZTOI(ZP)	((ZP)->z_vnode)
#define	VTOZ(VP)	((struct znode *)(VP)->v_data)
#define	VTOZ_SMR(VP)	((znode_t *)vn_load_v_data_smr(VP))
#define	ITOZ(VP)	((struct znode *)(VP)->v_data)
#define	zhold(zp)	vhold(ZTOV((zp)))
#define	zrele(zp)	vrele(ZTOV((zp)))

#define	ZTOZSB(zp) ((zp)->z_zfsvfs)
#define	ITOZSB(vp) (VTOZ(vp)->z_zfsvfs)
#define	ZTOTYPE(zp)	(ZTOV(zp)->v_type)
#define	ZTOGID(zp) ((zp)->z_gid)
#define	ZTOUID(zp) ((zp)->z_uid)
#define	ZTONLNK(zp) ((zp)->z_links)
#define	Z_ISBLK(type) ((type) == VBLK)
#define	Z_ISCHR(type) ((type) == VCHR)
#define	Z_ISLNK(type) ((type) == VLNK)
#define	Z_ISDIR(type) ((type) == VDIR)

#define	zn_has_cached_data(zp)		vn_has_cached_data(ZTOV(zp))
#define	zn_flush_cached_data(zp, sync)	vn_flush_cached_data(ZTOV(zp), sync)
#define	zn_rlimit_fsize(zp, uio) \
    vn_rlimit_fsize(ZTOV(zp), GET_UIO_STRUCT(uio), zfs_uio_td(uio))

#define	ZFS_ENTER_ERROR(zfsvfs, error) do {			\
	ZFS_TEARDOWN_ENTER_READ((zfsvfs), FTAG);		\
	if (__predict_false((zfsvfs)->z_unmounted)) {		\
		ZFS_TEARDOWN_EXIT_READ(zfsvfs, FTAG);		\
		return (error);					\
	}							\
} while (0)

/* Called on entry to each ZFS vnode and vfs operation  */
#define	ZFS_ENTER(zfsvfs)	ZFS_ENTER_ERROR(zfsvfs, EIO)

/* Must be called before exiting the vop */
#define	ZFS_EXIT(zfsvfs)	ZFS_TEARDOWN_EXIT_READ(zfsvfs, FTAG)

#define	ZFS_VERIFY_ZP_ERROR(zp, error) do {			\
	if (__predict_false((zp)->z_sa_hdl == NULL)) {		\
		ZFS_EXIT((zp)->z_zfsvfs);			\
		return (error);					\
	}							\
} while (0)

/* Verifies the znode is valid */
#define	ZFS_VERIFY_ZP(zp)	ZFS_VERIFY_ZP_ERROR(zp, EIO)

/*
 * Macros for dealing with dmu_buf_hold
 */
#define	ZFS_OBJ_HASH(obj_num)	((obj_num) & (ZFS_OBJ_MTX_SZ - 1))
#define	ZFS_OBJ_MUTEX(zfsvfs, obj_num)	\
	(&(zfsvfs)->z_hold_mtx[ZFS_OBJ_HASH(obj_num)])
#define	ZFS_OBJ_HOLD_ENTER(zfsvfs, obj_num) \
	mutex_enter(ZFS_OBJ_MUTEX((zfsvfs), (obj_num)))
#define	ZFS_OBJ_HOLD_TRYENTER(zfsvfs, obj_num) \
	mutex_tryenter(ZFS_OBJ_MUTEX((zfsvfs), (obj_num)))
#define	ZFS_OBJ_HOLD_EXIT(zfsvfs, obj_num) \
	mutex_exit(ZFS_OBJ_MUTEX((zfsvfs), (obj_num)))

/* Encode ZFS stored time values from a struct timespec */
#define	ZFS_TIME_ENCODE(tp, stmp)		\
{						\
	(stmp)[0] = (uint64_t)(tp)->tv_sec;	\
	(stmp)[1] = (uint64_t)(tp)->tv_nsec;	\
}

/* Decode ZFS stored time values to a struct timespec */
#define	ZFS_TIME_DECODE(tp, stmp)		\
{						\
	(tp)->tv_sec = (time_t)(stmp)[0];		\
	(tp)->tv_nsec = (long)(stmp)[1];		\
}
#define	ZFS_ACCESSTIME_STAMP(zfsvfs, zp) \
	if ((zfsvfs)->z_atime && !((zfsvfs)->z_vfs->vfs_flag & VFS_RDONLY)) \
		zfs_tstamp_update_setup_ext(zp, ACCESSED, NULL, NULL, B_FALSE);

extern void	zfs_tstamp_update_setup_ext(struct znode *,
    uint_t, uint64_t [2], uint64_t [2], boolean_t have_tx);
extern void zfs_znode_free(struct znode *);

extern zil_replay_func_t *zfs_replay_vector[TX_MAX_TYPE];
extern int zfsfstype;

extern int zfs_znode_parent_and_name(struct znode *zp, struct znode **dzpp,
    char *buf);
#ifdef	__cplusplus
}
#endif

#endif	/* _FREEBSD_SYS_FS_ZFS_ZNODE_H */
