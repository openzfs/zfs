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
 */

#ifndef	_SYS_FS_ZFS_VFSOPS_H
#define	_SYS_FS_ZFS_VFSOPS_H

#include <sys/dataset_kstats.h>
#include <sys/isa_defs.h>
#include <sys/types32.h>
#include <sys/list.h>
#include <sys/vfs.h>
#include <sys/zil.h>
#include <sys/sa.h>
#include <sys/rrwlock.h>
#include <sys/dsl_dataset.h>
#include <sys/zfs_ioctl.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct zfs_sb;
struct znode;

#ifdef __APPLE__
#define	APPLE_SA_RECOVER
/* #define	WITH_SEARCHFS */
/* #define	WITH_READDIRATTR */
#define	HAVE_PAGEOUT_V2 1
#define	HIDE_TRIVIAL_ACL 1
#ifndef __arm64__
#define	HAVE_NAMED_STREAMS 1
#endif
#endif

/*
 * Status of the zfs_unlinked_drain thread.
 */
typedef enum drain_state {
	ZFS_DRAIN_SHUTDOWN = 0,
	ZFS_DRAIN_RUNNING,
    ZFS_DRAIN_SHUTDOWN_REQ
} drain_state_t;


typedef struct zfsvfs zfsvfs_t;

struct zfsvfs {
	vfs_t	*z_vfs;	/* generic fs struct */
	zfsvfs_t	*z_parent;	/* parent fs */
	objset_t	*z_os;	/* objset reference */
	uint64_t	z_root;	/* id of root znode */
	uint64_t	z_unlinkedobj;	/* id of unlinked zapobj */
	uint64_t	z_max_blksz;	/* maximum block size for files */
	uint64_t	z_fuid_obj;	/* fuid table object number */
	uint64_t	z_fuid_size;	/* fuid table size */
	avl_tree_t	z_fuid_idx;	/* fuid tree keyed by index */
	avl_tree_t	z_fuid_domain;	/* fuid tree keyed by domain */
	krwlock_t	z_fuid_lock;	/* fuid lock */
	boolean_t	z_fuid_loaded;	/* fuid tables are loaded */
	boolean_t	z_fuid_dirty;	/* need to sync fuid table ? */
	struct zfs_fuid_info *z_fuid_replay; /* fuid info for replay */
	uint64_t	z_assign;	/* TXG_NOWAIT or set by zil_replay() */
	zilog_t	*z_log;	/* intent log pointer */
	uint_t	z_acl_mode;	/* acl chmod/mode behavior */
	uint_t	z_acl_inherit;	/* acl inheritance behavior */
	zfs_case_t	z_case;	/* case-sense */
	boolean_t	z_utf8;	/* utf8-only */
	int	z_norm;	/* normalization flags */
	boolean_t	z_atime;	/* enable atimes mount option */
	boolean_t	z_unmounted;	/* unmounted */
	rrmlock_t	z_teardown_lock;
	krwlock_t	z_teardown_inactive_lock;
	list_t	z_all_znodes;	/* all vnodes in the fs */
	kmutex_t	z_znodes_lock;	/* lock for z_all_znodes */
	struct vnode	*z_ctldir;	/* .zfs directory pointer */
	uint64_t	z_ctldir_startid;	/* Start of snapdir range */
	boolean_t	z_show_ctldir; 	/* expose .zfs in the root dir */
	boolean_t	z_issnap;	/* true if this is a snapshot */
	boolean_t	z_vscan;	/* virus scan on/off */
	boolean_t	z_use_fuids;	/* version allows fuids */
	boolean_t	z_replay;	/* set during ZIL replay */
	boolean_t	z_use_sa;	/* version allow system attributes */
	boolean_t	z_xattr_sa;	/* allow xattrs to be stores as SA */
	uint64_t	z_version;
	uint64_t	z_shares_dir;	/* hidden shares dir */
	dataset_kstats_t	z_kstat;	/* fs kstats */
	kmutex_t	z_lock;

	/* for controlling async zfs_unlinked_drain */
	kmutex_t	z_drain_lock;
	kcondvar_t	z_drain_cv;
	drain_state_t	z_drain_state;

	uint64_t	z_userquota_obj;
	uint64_t	z_groupquota_obj;
	uint64_t	z_userobjquota_obj;
	uint64_t	z_groupobjquota_obj;
	uint64_t	z_projectquota_obj;
	uint64_t	z_projectobjquota_obj;

#ifdef __APPLE__
	dev_t	z_rdev;	/* proxy device for mount */
	boolean_t	z_rdonly;	/* is mount read-only? */
	time_t	z_mount_time;	/* mount timestamp (for Spotlight) */
	time_t	z_last_unmount_time;	/* unmount timestamp (for Spotlight) */
	boolean_t	z_xattr;	/* enable atimes mount option */

	avl_tree_t	z_hardlinks;	/* linkid hash avl tree for vget */
	avl_tree_t	z_hardlinks_linkid;	/* sorted on linkid */
	krwlock_t	z_hardlinks_lock;	/* lock to access z_hardlinks */

	uint64_t	z_notification_conditions; /* HFSIOC_VOLUME_STATUS */
	uint64_t	z_freespace_notify_warninglimit;
	uint64_t	z_freespace_notify_dangerlimit;
	uint64_t	z_freespace_notify_desiredlevel;

	void	*z_devdisk; /* Hold fake disk if prop devdisk is on */

	uint64_t	z_findernotify_space;

#endif
	uint64_t	z_replay_eof;	/* New end of file - replay only */
	sa_attr_type_t	*z_attr_table;	/* SA attr mapping->id */

	uint64_t	z_hold_size;	/* znode hold array size */
	avl_tree_t	*z_hold_trees;	/* znode hold trees */
	kmutex_t	*z_hold_locks;	/* znode hold locks */
	taskqid_t	z_drain_task;	/* task id for the unlink drain task */
};
#define	ZFS_OBJ_MTX_SZ	64

#ifdef __APPLE__
struct hardlinks_struct {
	avl_node_t hl_node;
	avl_node_t hl_node_linkid;
	uint64_t hl_parent;	// parentid of entry
	uint64_t hl_fileid;	// the fileid (z_id) for vget
	uint32_t hl_linkid;	// the linkid, persistent over renames
	char hl_name[PATH_MAX]; // cached name for vget
};
typedef struct hardlinks_struct hardlinks_t;

int zfs_vfs_uuid_unparse(uuid_t uuid, char *dst);
int zfs_vfs_uuid_gen(const char *osname, uuid_t uuid);
#endif

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
 * Filesystems under .zfs/snapshot have a total file ID size of 22 bytes
 * (including the length field).  This makes files under .zfs/snapshot
 * accessible by NFSv3 and NFSv4, but not NFSv2.
 *
 * For files under .zfs/snapshot, we partition up the available space
 * as follows:
 *	2 bytes		fid length (required)
 *	6 bytes		object number (48 bits)
 *	4 bytes		generation number (32 bits)
 *	6 bytes		objset id (48 bits)
 *	4 bytes		currently just zero (32 bits)
 *
 * We reserve only 48 bits for the object number and objset id, as these are
 * the limits currently defined and imposed by the DMU.
 */
typedef struct zfid_long {
	zfid_short_t	z_fid;
	uint8_t		zf_setid[6];		/* obj[i] = obj >> (8 * i) */
	uint8_t		zf_setgen[4];		/* gen[i] = gen >> (8 * i) */
} zfid_long_t;

#define	SHORT_FID_LEN	(sizeof (zfid_short_t) - sizeof (uint16_t))
#define	LONG_FID_LEN	(sizeof (zfid_long_t) - sizeof (uint16_t))

extern uint_t zfs_fsyncer_key;

extern int zfs_suspend_fs(zfsvfs_t *zfsvfs);
extern int zfs_resume_fs(zfsvfs_t *zfsvfs, struct dsl_dataset *ds);
extern int zfs_userspace_one(zfsvfs_t *zfsvfs, zfs_userquota_prop_t type,
    const char *domain, uint64_t rid, uint64_t *valuep);
extern int zfs_userspace_many(zfsvfs_t *zfsvfs, zfs_userquota_prop_t type,
    uint64_t *cookiep, void *vbuf, uint64_t *bufsizep);
extern int zfs_set_userquota(zfsvfs_t *zfsvfs, zfs_userquota_prop_t type,
    const char *domain, uint64_t rid, uint64_t quota);
extern boolean_t zfs_owner_overquota(zfsvfs_t *zfsvfs, struct znode *,
    boolean_t isgroup);
extern boolean_t zfs_fuid_overquota(zfsvfs_t *zfsvfs, boolean_t isgroup,
    uint64_t fuid);
extern int zfs_set_version(zfsvfs_t *zfsvfs, uint64_t newvers);
extern int zfsvfs_create_impl(zfsvfs_t **zfvp, zfsvfs_t *zfsvfs, objset_t *os);

extern int zfs_get_zplprop(objset_t *os, zfs_prop_t prop,
    uint64_t *value);

extern int zfs_sb_create(const char *name, zfsvfs_t **zfsvfsp);
extern int zfs_sb_setup(zfsvfs_t *zfsvfs, boolean_t mounting);
extern void zfs_sb_free(zfsvfs_t *zfsvfs);
extern int zfs_check_global_label(const char *dsname, const char *hexsl);
extern boolean_t zfs_is_readonly(zfsvfs_t *zfsvfs);




extern int  zfs_vfs_init(struct vfsconf *vfsp);
extern int  zfs_vfs_start(struct mount *mp, int flags, vfs_context_t context);
extern int  zfs_vfs_mount(struct mount *mp, vnode_t *devvp,
    user_addr_t data, vfs_context_t context);
extern int  zfs_vfs_unmount(struct mount *mp, int mntflags,
    vfs_context_t context);
extern int  zfs_vfs_root(struct mount *mp, vnode_t **vpp,
    vfs_context_t context);
extern int  zfs_vfs_vget(struct mount *mp, ino64_t ino, vnode_t **vpp,
    vfs_context_t context);
extern int  zfs_vfs_getattr(struct mount *mp, struct vfs_attr *fsap,
    vfs_context_t context);
extern int  zfs_vfs_setattr(struct mount *mp, struct vfs_attr *fsap,
    vfs_context_t context);
extern int  zfs_vfs_sync(struct mount *mp, int waitfor, vfs_context_t context);
extern int  zfs_vfs_fhtovp(struct mount *mp, int fhlen, unsigned char *fhp,
    vnode_t **vpp, vfs_context_t context);
extern int  zfs_vfs_vptofh(vnode_t *vp, int *fhlenp, unsigned char *fhp,
    vfs_context_t context);
extern int  zfs_vfs_sysctl(int *name, uint_t namelen, user_addr_t oldp,
    size_t *oldlenp,  user_addr_t newp, size_t newlen, vfs_context_t context);
extern int  zfs_vfs_quotactl(struct mount *mp, int cmds, uid_t uid,
    caddr_t datap, vfs_context_t context);
extern int  zfs_vfs_mountroot(struct mount *mp, struct vnode *vp,
    vfs_context_t context);

extern void zfs_init(void);
extern void zfs_fini(void);

extern int  zfs_vnode_lock(vnode_t *vp, int flags);
extern void zfs_freevfs(struct mount *vfsp);

extern int  zfsvfs_create(const char *name, boolean_t rd, zfsvfs_t **zfvp);
extern void zfsvfs_free(zfsvfs_t *zfsvfs);

extern int zfs_get_temporary_prop(dsl_dataset_t *ds, zfs_prop_t zfs_prop,
    uint64_t *val, char *setpoint);

extern int zfs_end_fs(zfsvfs_t *zfsvfs, struct dsl_dataset *ds);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FS_ZFS_VFSOPS_H */
