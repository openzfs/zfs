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

#ifndef	_MACOS_ZFS_SYS_ZNODE_IMPL_H
#define	_MACOS_ZFS_SYS_ZNODE_IMPL_H

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

#ifdef	__cplusplus
extern "C" {
#endif

#define	ZFS_UIMMUTABLE	0x0000001000000000ull // OSX
#define	ZFS_UAPPENDONLY	0x0000004000000000ull // OSX

// #define	ZFS_IMMUTABLE  (ZFS_UIMMUTABLE  | ZFS_SIMMUTABLE)
// #define	ZFS_APPENDONLY (ZFS_UAPPENDONLY | ZFS_SAPPENDONLY)

#define	ZFS_TRACKED	0x0010000000000000ull
#define	ZFS_COMPRESSED	0x0020000000000000ull

#define	ZFS_SIMMUTABLE	0x0040000000000000ull
#define	ZFS_SAPPENDONLY	0x0080000000000000ull

#define	SA_ZPL_ADDTIME(z)	z->z_attr_table[ZPL_ADDTIME]
#define	SA_ZPL_DOCUMENTID(z)	z->z_attr_table[ZPL_DOCUMENTID]

#define	ZGET_FLAG_UNLINKED	(1<<0) /* Also lookup unlinked */
#define	ZGET_FLAG_ASYNC		(1<<3) /* taskq the vnode_create call */

extern int zfs_zget_ext(zfsvfs_t *zfsvfs, uint64_t obj_num,
	struct znode **zpp,	int flags);


/*
 * Directory entry locks control access to directory entries.
 * They are used to protect creates, deletes, and renames.
 * Each directory znode has a mutex and a list of locked names.
 */
#define	ZNODE_OS_FIELDS		\
	struct zfsvfs	*z_zfsvfs;	\
	struct vnode	*z_vnode;	\
	uint64_t		z_uid;	\
	uint64_t		z_gid;	\
	uint64_t		z_gen;	\
	uint64_t		z_atime[2];	\
	uint64_t		z_links;	\
	uint32_t		z_vid;	\
	uint32_t		z_document_id;	\
	uint64_t		z_finder_parentid;	\
	boolean_t		z_finder_hardlink;	\
	uint64_t		z_write_gencount;	\
	char			z_name_cache[MAXPATHLEN];	\
	boolean_t		z_skip_truncate_undo_decmpfs;	\
	taskq_ent_t		z_attach_taskq;	\
	kcondvar_t		z_attach_cv;	\
	kmutex_t		z_attach_lock;	\
	hrtime_t		z_snap_mount_time;	\
	krwlock_t		z_map_lock;

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
 * Convert between znode pointers and vnode pointers
 */
#define	ZTOV(ZP)		((ZP)->z_vnode)
#define	ZTOI(ZP)		((ZP)->z_vnode)
#define	VTOZ(VP)		((znode_t *)vnode_fsnode((VP)))
#define	ITOZ(VP)		((znode_t *)vnode_fsnode((VP)))

#define	VTOM(VP)		((mount_t *)vnode_mount((VP)))

/* These are not used so far, VN_HOLD returncode must be checked. */
#define	zhold(zp)		VN_HOLD(ZTOV(zp))
#define	zrele(zp)		VN_RELE(ZTOV(zp))

#define	ZTOZSB(zp)		((zp)->z_zfsvfs)
#define	ITOZSB(vp)		((zfsvfs_t *)vfs_fsprivate(vnode_mount(vp)))
#define	ZTOTYPE(zp)		(vnode_vtype(ZTOV(zp)))
#define	ZTOGID(zp)		((zp)->z_gid)
#define	ZTOUID(zp)		((zp)->z_uid)
#define	ZTONLNK(zp)		((zp)->z_links)
#define	Z_ISBLK(type)	((type) == VBLK)
#define	Z_ISCHR(type)	((type) == VCHR)
#define	Z_ISLNK(type)	((type) == VLNK)
#define	Z_ISDIR(type)	((type) == VDIR)

#define	zn_has_cached_data(zp)	((zp)->z_is_mapped)
#define	zn_flush_cached_data(zp, sync) \
	(void) ubc_msync(ZTOV(zp), 0, \
	ubc_getsize(ZTOV(zp)), NULL, UBC_PUSHALL | UBC_SYNC);
#define	zn_rlimit_fsize(zp, uio)	(0)

/* Called on entry to each ZFS vnode and vfs operation  */
static inline int
zfs_enter(zfsvfs_t *zfsvfs, const char *tag)
{
	ZFS_TEARDOWN_ENTER_READ(zfsvfs, tag);
	if (unlikely((zfsvfs)->z_unmounted)) {
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
#define	ZFS_OBJ_MTX_SZ	64
#define	ZFS_OBJ_MTX_MAX	(1024 * 1024)
#define	ZFS_OBJ_HASH(zfsvfs, obj)	((obj) & ((zfsvfs->z_hold_size) - 1))

extern unsigned int zfs_object_mutex_size;

/* Encode ZFS stored time values from a struct timespec */
#define	ZFS_TIME_ENCODE(tp, stmp)	\
	{	\
		(stmp)[0] = (uint64_t)(tp)->tv_sec;	\
		(stmp)[1] = (uint64_t)(tp)->tv_nsec;	\
	}

/* Decode ZFS stored time values to a struct timespec */
#define	ZFS_TIME_DECODE(tp, stmp)	\
	{	\
	(tp)->tv_sec = (time_t)(stmp)[0];	\
	(tp)->tv_nsec = (long)(stmp)[1];	\
}

#define	ZFS_ACCESSTIME_STAMP(zfsvfs, zp)	\
    if ((zfsvfs)->z_atime && !vfs_isrdonly(zfsvfs->z_vfs))	\
		zfs_tstamp_update_setup_ext(zp, ACCESSED, NULL, NULL, B_FALSE);

extern void	zfs_tstamp_update_setup_ext(struct znode *,
    uint_t, uint64_t [2], uint64_t [2], boolean_t);
extern void	zfs_tstamp_update_setup(struct znode *,
    uint_t, uint64_t [2], uint64_t [2]);
extern void zfs_znode_free(struct znode *);

extern zil_get_data_t zfs_get_data;
extern zil_replay_func_t *const zfs_replay_vector[TX_MAX_TYPE];
extern int zfsfstype;

extern int zfs_znode_parent_and_name(struct znode *zp, struct znode **dzpp,
    char *buf);
extern uint32_t zfs_getbsdflags(struct znode *zp);
extern void zfs_setattr_generate_id(struct znode *, uint64_t, char *name);

extern int zfs_setattr_set_documentid(struct znode *zp,
    boolean_t update_flags);

/* Legacy macOS uses fnv_32a hash for hostid. */
#define	FNV1_32A_INIT ((uint32_t)0x811c9dc5)
uint32_t fnv_32a_str(const char *str, uint32_t hval);

void zfs_setbsdflags(struct znode *, uint32_t bsdflags);
uint32_t zfs_getbsdflags(struct znode *zp);

#ifdef	__cplusplus
}
#endif

#endif	/* _MACOS_SYS_FS_ZFS_ZNODE_H */
