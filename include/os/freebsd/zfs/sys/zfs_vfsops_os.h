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
 * Copyright (c) 2011 Pawel Jakub Dawidek <pawel@dawidek.net>.
 * All rights reserved.
 */

#ifndef	_SYS_FS_ZFS_VFSOPS_H
#define	_SYS_FS_ZFS_VFSOPS_H

#if __FreeBSD_version >= 1300125
#define	TEARDOWN_RMS
#endif

#if __FreeBSD_version >= 1300109
#define	TEARDOWN_INACTIVE_RMS
#endif

#include <sys/dataset_kstats.h>
#include <sys/list.h>
#include <sys/vfs.h>
#include <sys/zil.h>
#include <sys/sa.h>
#include <sys/rrwlock.h>
#ifdef TEARDOWN_INACTIVE_RMS
#include <sys/rmlock.h>
#endif
#include <sys/zfs_ioctl.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef TEARDOWN_RMS
typedef struct rmslock zfs_teardown_lock_t;
#else
#define	zfs_teardown_lock_t		rrmlock_t
#endif

#ifdef TEARDOWN_INACTIVE_RMS
typedef struct rmslock zfs_teardown_inactive_lock_t;
#else
#define	zfs_teardown_inactive_lock_t krwlock_t
#endif

typedef struct zfsvfs zfsvfs_t;
struct znode;

struct zfsvfs {
	vfs_t		*z_vfs;		/* generic fs struct */
	zfsvfs_t	*z_parent;	/* parent fs */
	objset_t	*z_os;		/* objset reference */
	uint64_t	z_flags;	/* super_block flags */
	uint64_t	z_root;		/* id of root znode */
	uint64_t	z_unlinkedobj;	/* id of unlinked zapobj */
	uint64_t	z_max_blksz;	/* maximum block size for files */
	uint64_t	z_fuid_obj;	/* fuid table object number */
	uint64_t	z_fuid_size;	/* fuid table size */
	avl_tree_t	z_fuid_idx;	/* fuid tree keyed by index */
	avl_tree_t	z_fuid_domain;	/* fuid tree keyed by domain */
	krwlock_t	z_fuid_lock;	/* fuid lock */
	boolean_t	z_fuid_loaded;	/* fuid tables are loaded */
	boolean_t	z_fuid_dirty;   /* need to sync fuid table ? */
	struct zfs_fuid_info	*z_fuid_replay; /* fuid info for replay */
	zilog_t		*z_log;		/* intent log pointer */
	uint_t		z_acl_type;	/* type of acl usable on this fs */
	uint_t		z_acl_mode;	/* acl chmod/mode behavior */
	uint_t		z_acl_inherit;	/* acl inheritance behavior */
	zfs_case_t	z_case;		/* case-sense */
	boolean_t	z_utf8;		/* utf8-only */
	int		z_norm;		/* normalization flags */
	boolean_t	z_atime;	/* enable atimes mount option */
	boolean_t	z_unmounted;	/* unmounted */
	zfs_teardown_lock_t z_teardown_lock;
	zfs_teardown_inactive_lock_t z_teardown_inactive_lock;
	list_t		z_all_znodes;	/* all vnodes in the fs */
	uint64_t	z_nr_znodes;	/* number of znodes in the fs */
	kmutex_t	z_znodes_lock;	/* lock for z_all_znodes */
	struct zfsctl_root	*z_ctldir;	/* .zfs directory pointer */
	boolean_t	z_show_ctldir;	/* expose .zfs in the root dir */
	boolean_t	z_issnap;	/* true if this is a snapshot */
	boolean_t	z_use_fuids;	/* version allows fuids */
	boolean_t	z_replay;	/* set during ZIL replay */
	boolean_t	z_use_sa;	/* version allow system attributes */
	boolean_t	z_xattr_sa;	/* allow xattrs to be stores as SA */
	boolean_t	z_use_namecache; /* make use of FreeBSD name cache */
	uint8_t		z_xattr;	/* xattr type in use */
	uint64_t	z_version;	/* ZPL version */
	uint64_t	z_shares_dir;	/* hidden shares dir */
	dataset_kstats_t	z_kstat;	/* fs kstats */
	kmutex_t	z_lock;
	uint64_t	z_userquota_obj;
	uint64_t	z_groupquota_obj;
	uint64_t	z_userobjquota_obj;
	uint64_t	z_groupobjquota_obj;
	uint64_t	z_projectquota_obj;
	uint64_t	z_projectobjquota_obj;
	uint64_t	z_replay_eof;	/* New end of file - replay only */
	sa_attr_type_t	*z_attr_table;	/* SA attr mapping->id */
#define	ZFS_OBJ_MTX_SZ	64
	kmutex_t	z_hold_mtx[ZFS_OBJ_MTX_SZ];	/* znode hold locks */
	struct task	z_unlinked_drain_task;
};

#ifdef TEARDOWN_RMS
#define	ZFS_TEARDOWN_INIT(zfsvfs)		\
	rms_init(&(zfsvfs)->z_teardown_lock, "zfs teardown")

#define	ZFS_TEARDOWN_DESTROY(zfsvfs)		\
	rms_destroy(&(zfsvfs)->z_teardown_lock)

#define	ZFS_TEARDOWN_TRY_ENTER_READ(zfsvfs)	\
	rms_try_rlock(&(zfsvfs)->z_teardown_lock)

#define	ZFS_TEARDOWN_ENTER_READ(zfsvfs, tag)	\
	rms_rlock(&(zfsvfs)->z_teardown_lock);

#define	ZFS_TEARDOWN_EXIT_READ(zfsvfs, tag)	\
	rms_runlock(&(zfsvfs)->z_teardown_lock)

#define	ZFS_TEARDOWN_ENTER_WRITE(zfsvfs, tag)	\
	rms_wlock(&(zfsvfs)->z_teardown_lock)

#define	ZFS_TEARDOWN_EXIT_WRITE(zfsvfs)		\
	rms_wunlock(&(zfsvfs)->z_teardown_lock)

#define	ZFS_TEARDOWN_EXIT(zfsvfs, tag)		\
	rms_unlock(&(zfsvfs)->z_teardown_lock)

#define	ZFS_TEARDOWN_READ_HELD(zfsvfs)		\
	rms_rowned(&(zfsvfs)->z_teardown_lock)

#define	ZFS_TEARDOWN_WRITE_HELD(zfsvfs)		\
	rms_wowned(&(zfsvfs)->z_teardown_lock)

#define	ZFS_TEARDOWN_HELD(zfsvfs)		\
	rms_owned_any(&(zfsvfs)->z_teardown_lock)
#else
#define	ZFS_TEARDOWN_INIT(zfsvfs)		\
	rrm_init(&(zfsvfs)->z_teardown_lock, B_FALSE)

#define	ZFS_TEARDOWN_DESTROY(zfsvfs)		\
	rrm_destroy(&(zfsvfs)->z_teardown_lock)

#define	ZFS_TEARDOWN_TRY_ENTER_READ(zfsvfs)	\
	rw_tryenter(&(zfsvfs)->z_teardown_lock, RW_READER)

#define	ZFS_TEARDOWN_ENTER_READ(zfsvfs, tag)	\
	rrm_enter_read(&(zfsvfs)->z_teardown_lock, tag);

#define	ZFS_TEARDOWN_EXIT_READ(zfsvfs, tag)	\
	rrm_exit(&(zfsvfs)->z_teardown_lock, tag)

#define	ZFS_TEARDOWN_ENTER_WRITE(zfsvfs, tag)	\
	rrm_enter(&(zfsvfs)->z_teardown_lock, RW_WRITER, tag)

#define	ZFS_TEARDOWN_EXIT_WRITE(zfsvfs)		\
	rrm_exit(&(zfsvfs)->z_teardown_lock, tag)

#define	ZFS_TEARDOWN_EXIT(zfsvfs, tag)		\
	rrm_exit(&(zfsvfs)->z_teardown_lock, tag)

#define	ZFS_TEARDOWN_READ_HELD(zfsvfs)		\
	RRM_READ_HELD(&(zfsvfs)->z_teardown_lock)

#define	ZFS_TEARDOWN_WRITE_HELD(zfsvfs)		\
	RRM_WRITE_HELD(&(zfsvfs)->z_teardown_lock)

#define	ZFS_TEARDOWN_HELD(zfsvfs)		\
	RRM_LOCK_HELD(&(zfsvfs)->z_teardown_lock)
#endif

#ifdef TEARDOWN_INACTIVE_RMS
#define	ZFS_TEARDOWN_INACTIVE_INIT(zfsvfs)		\
	rms_init(&(zfsvfs)->z_teardown_inactive_lock, "zfs teardown inactive")

#define	ZFS_TEARDOWN_INACTIVE_DESTROY(zfsvfs)		\
	rms_destroy(&(zfsvfs)->z_teardown_inactive_lock)

#define	ZFS_TEARDOWN_INACTIVE_TRY_ENTER_READ(zfsvfs)	\
	rms_try_rlock(&(zfsvfs)->z_teardown_inactive_lock)

#define	ZFS_TEARDOWN_INACTIVE_ENTER_READ(zfsvfs)	\
	rms_rlock(&(zfsvfs)->z_teardown_inactive_lock)

#define	ZFS_TEARDOWN_INACTIVE_EXIT_READ(zfsvfs)		\
	rms_runlock(&(zfsvfs)->z_teardown_inactive_lock)

#define	ZFS_TEARDOWN_INACTIVE_ENTER_WRITE(zfsvfs)	\
	rms_wlock(&(zfsvfs)->z_teardown_inactive_lock)

#define	ZFS_TEARDOWN_INACTIVE_EXIT_WRITE(zfsvfs)	\
	rms_wunlock(&(zfsvfs)->z_teardown_inactive_lock)

#define	ZFS_TEARDOWN_INACTIVE_WRITE_HELD(zfsvfs)	\
	rms_wowned(&(zfsvfs)->z_teardown_inactive_lock)
#else
#define	ZFS_TEARDOWN_INACTIVE_INIT(zfsvfs)		\
	rw_init(&(zfsvfs)->z_teardown_inactive_lock, NULL, RW_DEFAULT, NULL)

#define	ZFS_TEARDOWN_INACTIVE_DESTROY(zfsvfs)		\
	rw_destroy(&(zfsvfs)->z_teardown_inactive_lock)

#define	ZFS_TEARDOWN_INACTIVE_TRY_ENTER_READ(zfsvfs)	\
	rw_tryenter(&(zfsvfs)->z_teardown_inactive_lock, RW_READER)

#define	ZFS_TEARDOWN_INACTIVE_ENTER_READ(zfsvfs)	\
	rw_enter(&(zfsvfs)->z_teardown_inactive_lock, RW_READER)

#define	ZFS_TEARDOWN_INACTIVE_EXIT_READ(zfsvfs)		\
	rw_exit(&(zfsvfs)->z_teardown_inactive_lock)

#define	ZFS_TEARDOWN_INACTIVE_ENTER_WRITE(zfsvfs)	\
	rw_enter(&(zfsvfs)->z_teardown_inactive_lock, RW_WRITER)

#define	ZFS_TEARDOWN_INACTIVE_EXIT_WRITE(zfsvfs)	\
	rw_exit(&(zfsvfs)->z_teardown_inactive_lock)

#define	ZFS_TEARDOWN_INACTIVE_WRITE_HELD(zfsvfs)	\
	RW_WRITE_HELD(&(zfsvfs)->z_teardown_inactive_lock)
#endif

#define	ZSB_XATTR	0x0001		/* Enable user xattrs */
/*
 * Normal filesystems (those not under .zfs/snapshot) have a total
 * file ID size limited to 12 bytes (including the length field) due to
 * NFSv2 protocol's limitation of 32 bytes for a filehandle.  For historical
 * reasons, this same limit is being imposed by the Solaris NFSv3 implementation
 * (although the NFSv3 protocol actually permits a maximum of 64 bytes).  It
 * is not possible to expand beyond 12 bytes without abandoning support
 * of NFSv2.
 *
 * For normal filesystems, we partition up the available space as follows:
 *	2 bytes		fid length (required)
 *	6 bytes		object number (48 bits)
 *	4 bytes		generation number (32 bits)
 *
 * We reserve only 48 bits for the object number, as this is the limit
 * currently defined and imposed by the DMU.
 */
typedef struct zfid_short {
	uint16_t	zf_len;
	uint8_t		zf_object[6];		/* obj[i] = obj >> (8 * i) */
	uint8_t		zf_gen[4];		/* gen[i] = gen >> (8 * i) */
} zfid_short_t;

/*
 * Filesystems under .zfs/snapshot have a total file ID size of 22[*] bytes
 * (including the length field).  This makes files under .zfs/snapshot
 * accessible by NFSv3 and NFSv4, but not NFSv2.
 *
 * For files under .zfs/snapshot, we partition up the available space
 * as follows:
 *	2 bytes		fid length (required)
 *	6 bytes		object number (48 bits)
 *	4 bytes		generation number (32 bits)
 *	6 bytes		objset id (48 bits)
 *	4 bytes[**]	currently just zero (32 bits)
 *
 * We reserve only 48 bits for the object number and objset id, as these are
 * the limits currently defined and imposed by the DMU.
 *
 * [*] 20 bytes on FreeBSD to fit into the size of struct fid.
 * [**] 2 bytes on FreeBSD for the above reason.
 */
typedef struct zfid_long {
	zfid_short_t	z_fid;
	uint8_t		zf_setid[6];		/* obj[i] = obj >> (8 * i) */
	uint8_t		zf_setgen[2];		/* gen[i] = gen >> (8 * i) */
} zfid_long_t;

#define	SHORT_FID_LEN	(sizeof (zfid_short_t) - sizeof (uint16_t))
#define	LONG_FID_LEN	(sizeof (zfid_long_t) - sizeof (uint16_t))

extern uint_t zfs_fsyncer_key;
extern int zfs_super_owner;

extern void zfs_init(void);
extern void zfs_fini(void);

extern int zfs_suspend_fs(zfsvfs_t *zfsvfs);
extern int zfs_resume_fs(zfsvfs_t *zfsvfs, struct dsl_dataset *ds);
extern int zfs_end_fs(zfsvfs_t *zfsvfs, struct dsl_dataset *ds);
extern int zfs_set_version(zfsvfs_t *zfsvfs, uint64_t newvers);
extern int zfsvfs_create(const char *name, boolean_t readonly, zfsvfs_t **zfvp);
extern int zfsvfs_create_impl(zfsvfs_t **zfvp, zfsvfs_t *zfsvfs, objset_t *os);
extern void zfsvfs_free(zfsvfs_t *zfsvfs);
extern int zfs_check_global_label(const char *dsname, const char *hexsl);
extern boolean_t zfs_is_readonly(zfsvfs_t *zfsvfs);
extern int zfs_get_temporary_prop(struct dsl_dataset *ds, zfs_prop_t zfs_prop,
    uint64_t *val, char *setpoint);
extern int zfs_busy(void);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FS_ZFS_VFSOPS_H */
