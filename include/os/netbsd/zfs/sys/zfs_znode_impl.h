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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2015 by Delphix. All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 * Copyright 2016 Nexenta Systems, Inc. All rights reserved.
 */

#ifndef	_NETBSD_ZFS_SYS_ZNODE_IMPL_H
#define	_NETBSD_ZFS_SYS_ZNODE_IMPL_H

#include <sys/list.h>
#include <sys/dmu.h>
#include <sys/sa.h>
#include <sys/zfs_vfsops.h>
#include <sys/rrwlock.h>
#include <sys/zfs_sa.h>
#include <sys/zfs_stat.h>
#include <sys/zfs_rlock.h>
#include <sys/zfs_acl_impl.h>
#include <sys/zil.h>
#include <sys/zfs_project.h>
#include <miscfs/genfs/genfs_node.h>
#include <sys/lockf.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * vcache stores the key pointer, not a copy, until reclaim memcpy's
 * it.  An orphaned vnode must keep a key that cannot match an
 * 8-byte object id or a 16-byte ctldir key.  The storage lives in
 * the znode, which outlives every lookup.
 */
struct zfs_nb_orphan_key {
	uint64_t	ok_obj;
	uint64_t	ok_gen;
	uintptr_t	ok_zp;
};

/*
 * Directory entry locks control access to directory entries.
 * They are used to protect creates, deletes, and renames.
 * Each directory znode has a mutex and a list of locked names.
 */
#define	ZNODE_OS_FIELDS                 \
	struct genfs_node	z_gnode;	\
	struct zfsvfs	*z_zfsvfs;      \
	vnode_t		*z_vnode;       \
	char		*z_cached_symlink;	\
	uint64_t		z_uid;          \
	uint64_t		z_gid;          \
	uint64_t		z_gen;          \
	uint64_t		z_atime[2];     \
	uint64_t		z_links;	\
	struct lockf	*z_lockf;	\
	void		*z_posix_acl;	/* cached POSIX access ACL */ \
	uint64_t	z_posix_acl_gen; /* bumped on purge */ \
	struct zfs_nb_orphan_key z_nb_orphan_key; \
	boolean_t	z_nb_orphaned;

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
 * Convert between znode pointers and vnode pointers.
 * v_data is &zp->z_gnode (a genfs_node), not the znode itself.
 * A ctldir sfs_node in v_data would make the offsetof walk look
 * plausible; DIAGNOSTIC checks the vnodeop vector so that is loud.
 *
 * Must be a macro: this header is included from zfs_znode.h
 * before znode_t is complete.  A static inline would need the
 * type now.
 */
extern int (**zfs_vnodeop_p)(void *);
extern int (**zfs_specop_p)(void *);
extern int (**zfs_fifoop_p)(void *);

#ifdef DIAGNOSTIC
/* BEGIN CSTYLED */
#define	VTOZ(VP) ({							\
	vnode_t *_zfs_vp = (VP);					\
	KASSERT(_zfs_vp != NULL && _zfs_vp->v_data != NULL);		\
	KASSERT(_zfs_vp->v_op == zfs_vnodeop_p ||			\
	    _zfs_vp->v_op == zfs_specop_p ||				\
	    _zfs_vp->v_op == zfs_fifoop_p);				\
	(znode_t *)((char *)(_zfs_vp)->v_data -				\
	    offsetof(znode_t, z_gnode));				\
})
/* END CSTYLED */
#else
#define	VTOZ(VP)							\
	((znode_t *)((char *)(VP)->v_data - offsetof(znode_t, z_gnode)))
#endif
#define	ZTOV(ZP)	((ZP)->z_vnode)
#define	ZTOI(ZP)	((ZP)->z_vnode)
#define	VTOZ_SMR(VP)	VTOZ(VP)
#define	ITOZ(VP)	VTOZ(VP)
#define	ZFS_VP_ATTACH(vp, zp)	((vp)->v_data = &(zp)->z_gnode)
#define	zhold(zp)	vref(ZTOV((zp)))
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

#define	zn_has_cached_data(zp, start, end)			\
	(ZTOV(zp) != NULL && ZTOV(zp)->v_uobj.uo_npages != 0)
#define	zn_flush_cached_data(zp, sync)	vn_flush_cached_data(ZTOV(zp), sync)
#define	zn_rlimit_fsize(size)		(0)
#define	zn_rlimit_fsize_uio(zp, uio)	(0)

/* Called on entry to each ZFS vnode and vfs operation  */
static inline int
zfs_enter(zfsvfs_t *zfsvfs, const char *tag)
{
	ZFS_TEARDOWN_ENTER_READ(zfsvfs, tag);
	if (__predict_false((zfsvfs)->z_unmounted)) {
		ZFS_TEARDOWN_EXIT_READ(zfsvfs, tag);
		return (SET_ERROR(EIO));
	}
	return (0);
}

/* Non-blocking enter for the pagedaemon. */
static inline int
zfs_tryenter(zfsvfs_t *zfsvfs, const char *tag)
{
	if (!ZFS_TEARDOWN_TRY_ENTER_READ(zfsvfs, tag))
		return (SET_ERROR(EAGAIN));
	if (__predict_false((zfsvfs)->z_unmounted)) {
		ZFS_TEARDOWN_EXIT_READ(zfsvfs, tag);
		return (SET_ERROR(EIO));
	}
	return (0);
}

/* Must be called before exiting the vop */
static inline void
zfs_exit(zfsvfs_t *zfsvfs, const char *tag)
{
	ZFS_TEARDOWN_EXIT_READ(zfsvfs, tag);
}

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
	if ((zfsvfs)->z_atime && !((zfsvfs)->z_vfs->vfs_flag & VFS_RDONLY) && \
	    (!(zfsvfs)->z_relatime || zfs_relatime_need_update(zp))) \
		zfs_tstamp_update_setup_ext(zp, ACCESSED, NULL, NULL, B_FALSE);

extern void	zfs_tstamp_update_setup_ext(struct znode *,
    uint_t, uint64_t [2], uint64_t [2], boolean_t have_tx);
extern boolean_t zfs_relatime_need_update(const struct znode *);
extern void zfs_znode_free(struct znode *);
extern void zfs_znode_cache_id(struct znode *);
extern void zfs_netbsd_posix_acl_purge(struct znode *);
/*
 * Drops z_znodes_lock.  Caller holds it; this removes zp from
 * z_all_znodes and mutex_exit's before freeing.
 */
extern void zfs_znode_reap_orphan(struct znode *);
extern void zfs_nb_orphan(struct znode *);

extern zil_replay_func_t *const zfs_replay_vector[TX_MAX_TYPE];

extern int zfs_znode_parent_and_name(struct znode *zp, struct znode **dzpp,
    char *buf, uint64_t buflen);

#ifdef	__cplusplus
}
#endif

#endif	/* _FREEBSD_SYS_FS_ZFS_ZNODE_H */
