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
 * Copyright (c) 2011 Pawel Jakub Dawidek <pawel@dawidek.net>.
 * All rights reserved.
 * Copyright (c) 2012, 2015 by Delphix. All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 * Copyright 2016 Nexenta Systems, Inc. All rights reserved.
 */

/* Portions Copyright 2010 Robert Milkowski */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysmacros.h>
#include <sys/kmem.h>
#include <sys/atomic.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/acl.h>
#include <sys/vnode.h>
#include <sys/vfs.h>
#include <sys/mntent.h>
#include <sys/mount.h>
#include <sys/fstrans.h>
#include <sys/cmn_err.h>
#include <sys/cpu.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vnops.h>
#include <sys/proc.h>
#include <sys/filedesc.h>
#include <sys/zfs_dir.h>
#include <sys/zil.h>
#include <sys/fs/zfs.h>
#include <sys/dmu.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_deleg.h>
#include <sys/spa_impl.h>
#include <sys/zap.h>
#include <sys/sa.h>
#include <sys/sa_impl.h>
#include <sys/policy.h>
#include <sys/atomic.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_ioctl_impl.h>
#include <sys/zfs_ctldir.h>
#include <sys/zfs_fuid.h>
#include <sys/sunddi.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_dir.h>
#include <sys/proc.h>
#include <sys/lwp.h>
#include <ufs/ufs/quota.h>
#include <sys/quota.h>
#include <sys/quotactl.h>
#include <sys/zfs_quota.h>
#include <miscfs/genfs/genfs.h>
#if !defined(_MODULE) && (defined(__amd64__) || defined(__i386__))
#include <machine/bootinfo.h>
#endif

#include "zfs_comutil.h"
#include "zfs_crrd.h"

kmutex_t zfs_debug_mtx;

int zfs_super_owner;
int zfs_debug_level;

static int zfs_mount(vfs_t *vfsp, const char *path, void *data,
    size_t *data_len);
static int zfs_umount(vfs_t *vfsp, int fflag);
static int zfs_root(vfs_t *vfsp, int flags, vnode_t **vpp);
static int zfs_statvfs(vfs_t *vfsp, struct statvfs *statp);
static int zfs_vget(vfs_t *vfsp, ino_t ino, int flags, vnode_t **vpp);
static int zfs_sync(vfs_t *vfsp, int waitfor);
static int zfs_netbsd_sync(vfs_t *vfsp, int waitfor, cred_t *cr);
static int zfs_netbsd_vptofh(vnode_t *vp, fid_t *fidp, size_t *fh_size);
static int zfs_netbsd_fhtovp(vfs_t *vfsp, fid_t *fidp, int flags,
    vnode_t **vpp);
static int zfs_netbsd_suspendctl(struct mount *mp, int cmd);
static void zfs_freevfs(vfs_t *vfsp);
#ifndef _MODULE
static int zfs_mountroot(void);
#endif
static void zfs_vfs_init(void);
static void zfs_vfs_done(void);
static void zfs_nb_reap_znodes(zfsvfs_t *);
int zfs_domount(vfs_t *vfsp, char *osname);

extern const struct vnodeopv_desc zfs_vnodeop_opv_desc;
extern const struct vnodeopv_desc zfs_specop_opv_desc;
extern const struct vnodeopv_desc zfs_fifoop_opv_desc;
extern const struct vnodeopv_desc zfsctl_vnodeop_opv_desc;
extern const struct vnodeopv_desc zfs_sfsop_opv_desc;
extern int zfs_loadvnode(struct mount *, struct vnode *,
    const void *, size_t, const void **);
extern int zfs_newvnode(struct mount *, struct vnode *, struct vnode *,
    struct vattr *, kauth_cred_t, void *, size_t *, const void **);

static const struct vnodeopv_desc * const zfs_vnodeop_descs[] = {
	&zfs_vnodeop_opv_desc,
	&zfs_specop_opv_desc,
	&zfs_fifoop_opv_desc,
	&zfsctl_vnodeop_opv_desc,
	&zfs_sfsop_opv_desc,	/* loadvnode sets v_op = zfs_sfsop_p */
	NULL,
};

extern int zfs_netbsd_quotactl(vfs_t *, struct quotactl_args *);

struct vfsops zfs_vfsops = {
	.vfs_name = MOUNT_ZFS,
	.vfs_min_mount_data = sizeof (struct zfs_args),
	.vfs_opv_descs = zfs_vnodeop_descs,
	.vfs_mount = zfs_mount,
	.vfs_unmount = zfs_umount,
	.vfs_root = zfs_root,
	.vfs_statvfs = zfs_statvfs,
	.vfs_sync = zfs_netbsd_sync,
	.vfs_vget = zfs_vget,
	.vfs_loadvnode = zfs_loadvnode,
	.vfs_newvnode = zfs_newvnode,
	.vfs_init = zfs_vfs_init,
	.vfs_done = zfs_vfs_done,
#ifndef _MODULE
	/*
	 * Root-on-ZFS is only reachable when this file is built
	 * into the kernel (GENERIC + ZFS).  An external kmod is
	 * loaded after root is already mounted, so vfs_mountroot
	 * can never run there.  _MODULE is defined by
	 * bsd.kmodule.mk; it is not defined for an in-kernel
	 * build, so the hook stays for that future.
	 */
	.vfs_mountroot = zfs_mountroot,
#endif
	.vfs_start = (void *)nullop,
	.vfs_renamelock_enter = genfs_renamelock_enter,
	.vfs_renamelock_exit = genfs_renamelock_exit,
	.vfs_reinit = (void *)nullop,
	.vfs_vptofh = zfs_netbsd_vptofh,
	.vfs_fhtovp = zfs_netbsd_fhtovp,
	.vfs_quotactl = zfs_netbsd_quotactl,
	.vfs_extattrctl = (void *)eopnotsupp,
	/*
	 * Do not genfs_suspendctl: it drains fstrans on this mp while
	 * holding the global vfs_suspend_lock, then mount_domount
	 * namei's the cover.  zfs_domount already ran zfsctl_create ->
	 * VFS_ROOT and may have zio/txg holders, so that drain can
	 * wait forever.  Returning 0 (not EOPNOTSUPP) lets
	 * mount_domount skip the drain and still lets dounmount
	 * proceed -- EOPNOTSUPP from vfs_suspend is returned to
	 * userland as "Operation not supported".  ZFS teardown is via
	 * z_teardown_lock, not fstrans suspend.
	 */
	.vfs_suspendctl = zfs_netbsd_suspendctl,
	.vfs_snapshot = (void *)eopnotsupp,
	.vfs_fsync = (void *)eopnotsupp,
};

/*
 * We need to keep a count of active fs's.
 * This is necessary to prevent our module
 * from being unloaded after a umount -f
 */
uint32_t zfs_active_fs_count = 0;

int
zfs_get_temporary_prop(dsl_dataset_t *ds, zfs_prop_t zfs_prop, uint64_t *val,
    char *setpoint)
{
	int error;
	zfsvfs_t *zfvp;
	vfs_t *vfsp;
	objset_t *os;
	uint64_t tmp = *val;

	error = dmu_objset_from_ds(ds, &os);
	if (error != 0)
		return (error);

	error = getzfsvfs_impl(os, &zfvp);
	if (error != 0)
		return (error);
	if (zfvp == NULL)
		return (ENOENT);
	vfsp = zfvp->z_vfs;
	switch (zfs_prop) {
	case ZFS_PROP_ATIME:
		if (vfs_optionisset(vfsp, MNTOPT_NOATIME, NULL))
			tmp = 0;
		if (vfs_optionisset(vfsp, MNTOPT_ATIME, NULL))
			tmp = 1;
		break;
	case ZFS_PROP_DEVICES:
		if (vfs_optionisset(vfsp, MNTOPT_NODEVICES, NULL))
			tmp = 0;
		if (vfs_optionisset(vfsp, MNTOPT_DEVICES, NULL))
			tmp = 1;
		break;
	case ZFS_PROP_EXEC:
		if (vfs_optionisset(vfsp, MNTOPT_NOEXEC, NULL))
			tmp = 0;
		if (vfs_optionisset(vfsp, MNTOPT_EXEC, NULL))
			tmp = 1;
		break;
	case ZFS_PROP_SETUID:
		if (vfs_optionisset(vfsp, MNTOPT_NOSETUID, NULL))
			tmp = 0;
		if (vfs_optionisset(vfsp, MNTOPT_SETUID, NULL))
			tmp = 1;
		break;
	case ZFS_PROP_READONLY:
		if (vfs_optionisset(vfsp, MNTOPT_RW, NULL))
			tmp = 0;
		if (vfs_optionisset(vfsp, MNTOPT_RO, NULL))
			tmp = 1;
		break;
	case ZFS_PROP_XATTR:
		if (zfvp->z_flags & ZSB_XATTR)
			tmp = zfvp->z_xattr;
		break;
	case ZFS_PROP_NBMAND:
		if (vfs_optionisset(vfsp, MNTOPT_NONBMAND, NULL))
			tmp = 0;
		if (vfs_optionisset(vfsp, MNTOPT_NBMAND, NULL))
			tmp = 1;
		break;
	default:
		zfs_vfs_rele(zfvp);
		return (ENOENT);
	}

	zfs_vfs_rele(zfvp);
	if (tmp != *val) {
		if (setpoint)
			(void) strlcpy(setpoint, "temporary",
			    ZFS_MAX_DATASET_NAME_LEN);
		*val = tmp;
	}
	return (0);
}

boolean_t
zfs_is_readonly(zfsvfs_t *zfsvfs)
{
	return (!!(zfsvfs->z_vfs->vfs_flag & VFS_RDONLY));
}

static int
zfs_sync(vfs_t *vfsp, int waitfor)
{

	/*
	 * Data integrity is job one.  We don't want a compromised kernel
	 * writing to the storage pool, so we never sync during panic.
	 */
	if (panicstr)
		return (0);

	/*
	 * Ignore the system syncher.  ZFS already commits async data
	 * at zfs_txg_timeout intervals.
	 */
	if (waitfor == MNT_LAZY)
		return (0);

	if (vfsp != NULL) {
		/*
		 * Sync a specific filesystem.
		 */
		zfsvfs_t *zfsvfs = vfsp->vfs_data;
		dsl_pool_t *dp;
		int error;

		if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
			return (error);
		dp = dmu_objset_pool(zfsvfs->z_os);

		/*
		 * If the system is shutting down, then skip any
		 * filesystems which may exist on a suspended pool.
		 */
		if (shutting_down && spa_suspended(dp->dp_spa)) {
			zfs_exit(zfsvfs, FTAG);
			return (0);
		}

		if (zfsvfs->z_log != NULL) {
			error = zil_commit(zfsvfs->z_log, 0);
			if (error != 0) {
				zfs_exit(zfsvfs, FTAG);
				return (error);
			}
		}

		zfs_exit(zfsvfs, FTAG);
	} else {
		/*
		 * Sync all ZFS filesystems.  This is what happens when you
		 * run sync(8).  Unlike other filesystems, ZFS honors the
		 * request by waiting for all pools to commit all dirty data.
		 */
		spa_sync_allpools();
	}

	return (0);
}

static void
atime_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	if (newval == TRUE) {
		zfsvfs->z_atime = TRUE;
		zfsvfs->z_vfs->vfs_flag &= ~MNT_NOATIME;
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_NOATIME);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_ATIME, NULL, 0);
	} else {
		zfsvfs->z_atime = FALSE;
		zfsvfs->z_vfs->vfs_flag |= MNT_NOATIME;
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_ATIME);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_NOATIME, NULL, 0);
	}
}

static void
relatime_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	zfsvfs->z_relatime = (newval != 0);
#ifdef MNT_RELATIME
	if (newval != 0) {
		zfsvfs->z_vfs->vfs_flag |= MNT_RELATIME;
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_NORELATIME);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_RELATIME, NULL, 0);
	} else {
		zfsvfs->z_vfs->vfs_flag &= ~MNT_RELATIME;
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_RELATIME);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_NORELATIME, NULL, 0);
	}
#endif
}

static void
xattr_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	if (newval == ZFS_XATTR_OFF) {
		zfsvfs->z_flags &= ~ZSB_XATTR;
	} else {
		zfsvfs->z_flags |= ZSB_XATTR;

		if (newval == ZFS_XATTR_SA)
			zfsvfs->z_xattr_sa = B_TRUE;
		else
			zfsvfs->z_xattr_sa = B_FALSE;
	}
}

static void
blksz_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;
	ASSERT3U(newval, <=, spa_maxblocksize(dmu_objset_spa(zfsvfs->z_os)));
	ASSERT3U(newval, >=, SPA_MINBLOCKSIZE);
	ASSERT(ISP2(newval));

	zfsvfs->z_max_blksz = newval;
	zfsvfs->z_vfs->mnt_stat.f_iosize = newval;
}

static void
readonly_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	if (newval) {
		/* XXX locking on vfs_flag? */
		zfsvfs->z_vfs->vfs_flag |= VFS_RDONLY;
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_RW);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_RO, NULL, 0);
	} else {
		/* XXX locking on vfs_flag? */
		zfsvfs->z_vfs->vfs_flag &= ~VFS_RDONLY;
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_RO);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_RW, NULL, 0);
	}
}

static void
setuid_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	if (newval == FALSE) {
		zfsvfs->z_vfs->vfs_flag |= VFS_NOSETUID;
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_SETUID);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_NOSETUID, NULL, 0);
	} else if (!zfsvfs->z_policy_nosuid) {
		zfsvfs->z_vfs->vfs_flag &= ~VFS_NOSETUID;
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_NOSETUID);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_SETUID, NULL, 0);
	}
}

static void
devices_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	if (newval == FALSE) {
		zfsvfs->z_vfs->vfs_flag |= VFS_NODEVICES;
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_DEVICES);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_NODEVICES, NULL, 0);
	} else if (!zfsvfs->z_policy_nodev) {
		zfsvfs->z_vfs->vfs_flag &= ~VFS_NODEVICES;
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_NODEVICES);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_DEVICES, NULL, 0);
	}
}

static void
exec_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	if (newval == FALSE) {
		zfsvfs->z_vfs->vfs_flag |= VFS_NOEXEC;
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_EXEC);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_NOEXEC, NULL, 0);
	} else {
		zfsvfs->z_vfs->vfs_flag &= ~VFS_NOEXEC;
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_NOEXEC);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_EXEC, NULL, 0);
	}
}

/*
 * The nbmand mount option can be changed at mount time.
 * We can't allow it to be toggled on live file systems or incorrect
 * behavior may be seen from cifs clients
 *
 * This property isn't registered via dsl_prop_register(), but this callback
 * will be called when a file system is first mounted
 */
static void
nbmand_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;
	if (newval == FALSE) {
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_NBMAND);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_NONBMAND, NULL, 0);
	} else {
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_NONBMAND);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_NBMAND, NULL, 0);
	}
}

static void
snapdir_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	zfsvfs->z_show_ctldir = newval;
}

static void
acl_mode_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	zfsvfs->z_acl_mode = newval;
}

static void
acl_inherit_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	zfsvfs->z_acl_inherit = newval;
}

/*
 * Invalidate IMNT_NCLOOKUP identity (vi_nc_*) on every live directory.
 * cache_enter_id takes vi_nc_lock only — no vnode lock.  This runs
 * from acl_type_changed_cb, which after mount is a spa_sync synctask
 * (dsl_prop_set_sync_impl).  vn_lock here inverts with VOP →
 * dmu_tx_assign waiting for this txg.
 */
static void
zfs_nclookup_invalidate_dirs(zfsvfs_t *zfsvfs)
{
	vfs_t *vfsp = zfsvfs->z_vfs;
	struct vnode_iterator *marker = NULL;
	vnode_t *vp;

	if (vfsp == NULL)
		return;

	vfs_vnode_iterator_init(vfsp, &marker);
	while ((vp = vfs_vnode_iterator_next(marker, NULL, NULL)) != NULL) {
		if (vp->v_type == VDIR)
			cache_enter_id(vp, 0, 0, 0, B_FALSE);
		vrele(vp);
	}
	vfs_vnode_iterator_destroy(marker);
}

static void
zfs_acltype_apply_mnt(zfsvfs_t *zfsvfs)
{
	vfs_t *vfsp = zfsvfs->z_vfs;

	if (vfsp == NULL)
		return;
	/*
	 * NetBSD setfacl/getfacl brand via pathconf(_PC_ACL_NFS4),
	 * and mount(8) / statvfs print these MNT_* bits (UFS does
	 * the same from MNT_NFS4ACLS / MNT_POSIX1EACLS).
	 */
	vfsp->mnt_flag &= ~(MNT_NFS4ACLS | MNT_POSIX1EACLS);
	switch (zfsvfs->z_acl_type) {
	case ZFS_ACLTYPE_NFSV4:
		vfsp->mnt_flag |= MNT_NFS4ACLS;
		break;
	case ZFS_ACLTYPE_POSIX:
		vfsp->mnt_flag |= MNT_POSIX1EACLS;
		break;
	default:
		break;
	}

	/*
	 * IMNT_NCLOOKUP treats vi_nc_mode != VNOVAL as VEXEC from
	 * the mode bits, with no VOP_ACCESS.  cache_purgevfs drops
	 * nchash names only, not vi_nc_*.
	 *
	 * nfsv4 trivial → posix is the unsafe direction: leftover
	 * valid=true skips a POSIX ACL.  Invalidate dirs with
	 * cache_enter_id(..., false).  No vn_lock — this callback
	 * runs in spa_sync (dsl_prop_set_sync_impl).
	 *
	 * posix → nfsv4/off: leftover valid=false is extra
	 * VOP_ACCESS, which is safe.  setattr/setacl already
	 * republish via zfs_znode_cache_id.  Do not walk from
	 * the synctask.
	 */
	cache_purgevfs(vfsp);
	if (zfsvfs->z_acl_type == ZFS_ACLTYPE_POSIX)
		zfs_nclookup_invalidate_dirs(zfsvfs);
}

static void
acl_type_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	zfsvfs->z_acl_type = newval;
	zfs_acltype_apply_mnt(zfsvfs);
}

static void
longname_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	zfsvfs->z_longname = newval;
}

static int
zfs_register_callbacks(vfs_t *vfsp)
{
	struct dsl_dataset *ds = NULL;
	objset_t *os = NULL;
	zfsvfs_t *zfsvfs = NULL;
	uint64_t nbmand;
	boolean_t readonly = B_FALSE;
	boolean_t do_readonly = B_FALSE;
	boolean_t setuid = B_FALSE;
	boolean_t do_setuid = B_FALSE;
	boolean_t devices = B_FALSE;
	boolean_t do_devices = B_FALSE;
	boolean_t exec = B_FALSE;
	boolean_t do_exec = B_FALSE;
	boolean_t xattr = B_FALSE;
	boolean_t atime = B_FALSE;
	boolean_t do_atime = B_FALSE;
	boolean_t do_xattr = B_FALSE;
	int error = 0;

	ASSERT3P(vfsp, !=, NULL);
	zfsvfs = vfsp->vfs_data;
	ASSERT3P(zfsvfs, !=, NULL);
	os = zfsvfs->z_os;

	/*
	 * This function can be called for a snapshot when we update snapshot's
	 * mount point, which isn't really supported.
	 */
	if (dmu_objset_is_snapshot(os))
		return (EOPNOTSUPP);

	/*
	 * The act of registering our callbacks will destroy any mount
	 * options we may have.  In order to enable temporary overrides
	 * of mount options, we stash away the current values and
	 * restore them after we register the callbacks.
	 */
	if (vfs_optionisset(vfsp, MNTOPT_RO, NULL) ||
	    !spa_writeable(dmu_objset_spa(os))) {
		readonly = B_TRUE;
		do_readonly = B_TRUE;
	} else if (vfs_optionisset(vfsp, MNTOPT_RW, NULL)) {
		readonly = B_FALSE;
		do_readonly = B_TRUE;
	}
	if (vfs_optionisset(vfsp, MNTOPT_NOSETUID, NULL)) {
		setuid = B_FALSE;
		do_setuid = B_TRUE;
	} else if (vfs_optionisset(vfsp, MNTOPT_SETUID, NULL)) {
		setuid = B_TRUE;
		do_setuid = B_TRUE;
	}
	if (vfs_optionisset(vfsp, MNTOPT_NODEVICES, NULL)) {
		devices = B_FALSE;
		do_devices = B_TRUE;
	} else if (vfs_optionisset(vfsp, MNTOPT_DEVICES, NULL)) {
		devices = B_TRUE;
		do_devices = B_TRUE;
	}
	if (vfs_optionisset(vfsp, MNTOPT_NOEXEC, NULL)) {
		exec = B_FALSE;
		do_exec = B_TRUE;
	} else if (vfs_optionisset(vfsp, MNTOPT_EXEC, NULL)) {
		exec = B_TRUE;
		do_exec = B_TRUE;
	}
	if (vfs_optionisset(vfsp, MNTOPT_NOXATTR, NULL)) {
		zfsvfs->z_xattr = xattr = ZFS_XATTR_OFF;
		do_xattr = B_TRUE;
	} else if (vfs_optionisset(vfsp, MNTOPT_XATTR, NULL)) {
		zfsvfs->z_xattr = xattr = ZFS_XATTR_DIR;
		do_xattr = B_TRUE;
	} else if (vfs_optionisset(vfsp, MNTOPT_DIRXATTR, NULL)) {
		zfsvfs->z_xattr = xattr = ZFS_XATTR_DIR;
		do_xattr = B_TRUE;
	} else if (vfs_optionisset(vfsp, MNTOPT_SAXATTR, NULL)) {
		zfsvfs->z_xattr = xattr = ZFS_XATTR_SA;
		do_xattr = B_TRUE;
	}
	if (vfs_optionisset(vfsp, MNTOPT_NOATIME, NULL)) {
		atime = B_FALSE;
		do_atime = B_TRUE;
	} else if (vfs_optionisset(vfsp, MNTOPT_ATIME, NULL)) {
		atime = B_TRUE;
		do_atime = B_TRUE;
	}

	/*
	 * We need to enter pool configuration here, so that we can use
	 * dsl_prop_get_int_ds() to handle the special nbmand property below.
	 * dsl_prop_get_integer() can not be used, because it has to acquire
	 * spa_namespace_lock and we can not do that because we already hold
	 * z_teardown_lock.  The problem is that spa_write_cachefile() is called
	 * with spa_namespace_lock held and the function calls ZFS vnode
	 * operations to write the cache file and thus z_teardown_lock is
	 * acquired after spa_namespace_lock.
	 */
	ds = dmu_objset_ds(os);
	dsl_pool_config_enter(dmu_objset_pool(os), FTAG);

	/*
	 * nbmand is a special property.  It can only be changed at
	 * mount time.
	 *
	 * This is weird, but it is documented to only be changeable
	 * at mount time.
	 */
	if (vfs_optionisset(vfsp, MNTOPT_NONBMAND, NULL)) {
		nbmand = B_FALSE;
	} else if (vfs_optionisset(vfsp, MNTOPT_NBMAND, NULL)) {
		nbmand = B_TRUE;
	} else if ((error = dsl_prop_get_int_ds(ds, "nbmand", &nbmand)) != 0) {
		dsl_pool_config_exit(dmu_objset_pool(os), FTAG);
		return (error);
	}

	/*
	 * Register property callbacks.
	 *
	 * It would probably be fine to just check for i/o error from
	 * the first prop_register(), but I guess I like to go
	 * overboard...
	 */
	error = dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_ATIME), atime_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_RELATIME), relatime_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_XATTR), xattr_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_RECORDSIZE), blksz_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_READONLY), readonly_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_SETUID), setuid_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_DEVICES), devices_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_EXEC), exec_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_SNAPDIR), snapdir_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_ACLTYPE), acl_type_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_ACLMODE), acl_mode_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_ACLINHERIT), acl_inherit_changed_cb,
	    zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_LONGNAME), longname_changed_cb, zfsvfs);
	dsl_pool_config_exit(dmu_objset_pool(os), FTAG);
	if (error)
		goto unregister;

	/*
	 * Invoke our callbacks to restore temporary mount options.
	 */
	if (do_readonly)
		readonly_changed_cb(zfsvfs, readonly);
	if (do_setuid)
		setuid_changed_cb(zfsvfs, setuid);
	if (do_devices)
		devices_changed_cb(zfsvfs, devices);
	if (do_exec)
		exec_changed_cb(zfsvfs, exec);
	if (do_xattr)
		xattr_changed_cb(zfsvfs, xattr);
	if (do_atime)
		atime_changed_cb(zfsvfs, atime);

	nbmand_changed_cb(zfsvfs, nbmand);

	return (0);

unregister:
	dsl_prop_unregister_all(ds, zfsvfs);
	return (error);
}

/*
 * Associate this zfsvfs with the given objset, which must be owned.
 * This will cache a bunch of on-disk state from the objset in the
 * zfsvfs.
 */
static int
zfsvfs_init(zfsvfs_t *zfsvfs, objset_t *os)
{
	int error;
	uint64_t val;

	zfsvfs->z_max_blksz = SPA_OLD_MAXBLOCKSIZE;
	zfsvfs->z_show_ctldir = ZFS_SNAPDIR_VISIBLE;
	zfsvfs->z_os = os;

	error = zfs_get_zplprop(os, ZFS_PROP_VERSION, &zfsvfs->z_version);
	if (error != 0)
		return (error);
	if (zfsvfs->z_version >
	    zfs_zpl_version_map(spa_version(dmu_objset_spa(os)))) {
		(void) printf("Can't mount a version %lld file system "
		    "on a version %lld pool\n. Pool must be upgraded to mount "
		    "this file system.", (u_longlong_t)zfsvfs->z_version,
		    (u_longlong_t)spa_version(dmu_objset_spa(os)));
		return (SET_ERROR(ENOTSUP));
	}
	error = zfs_get_zplprop(os, ZFS_PROP_NORMALIZE, &val);
	if (error != 0)
		return (error);
	zfsvfs->z_norm = (int)val;

	error = zfs_get_zplprop(os, ZFS_PROP_UTF8ONLY, &val);
	if (error != 0)
		return (error);
	zfsvfs->z_utf8 = (val != 0);

	error = zfs_get_zplprop(os, ZFS_PROP_CASE, &val);
	if (error != 0)
		return (error);
	zfsvfs->z_case = (uint_t)val;

	error = zfs_get_zplprop(os, ZFS_PROP_ACLTYPE, &val);
	if (error != 0)
		return (error);
	zfsvfs->z_acl_type = (uint_t)val;

	/*
	 * Fold case on file systems that are always or sometimes case
	 * insensitive.
	 */
	if (zfsvfs->z_case == ZFS_CASE_INSENSITIVE ||
	    zfsvfs->z_case == ZFS_CASE_MIXED)
		zfsvfs->z_norm |= U8_TEXTPREP_TOUPPER;

	zfsvfs->z_use_fuids = USE_FUIDS(zfsvfs->z_version, zfsvfs->z_os);
	zfsvfs->z_use_sa = USE_SA(zfsvfs->z_version, zfsvfs->z_os);

	uint64_t sa_obj = 0;
	if (zfsvfs->z_use_sa) {
		/* should either have both of these objects or none */
		error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_SA_ATTRS, 8, 1,
		    &sa_obj);
		if (error != 0)
			return (error);

		error = zfs_get_zplprop(os, ZFS_PROP_XATTR, &val);
		if (error == 0 && val == ZFS_XATTR_SA)
			zfsvfs->z_xattr_sa = B_TRUE;
	}

	error = zfs_get_zplprop(os, ZFS_PROP_DEFAULTUSERQUOTA,
	    &zfsvfs->z_defaultuserquota);
	if (error != 0)
		return (error);

	error = zfs_get_zplprop(os, ZFS_PROP_DEFAULTGROUPQUOTA,
	    &zfsvfs->z_defaultgroupquota);
	if (error != 0)
		return (error);

	error = zfs_get_zplprop(os, ZFS_PROP_DEFAULTPROJECTQUOTA,
	    &zfsvfs->z_defaultprojectquota);
	if (error != 0)
		return (error);

	error = zfs_get_zplprop(os, ZFS_PROP_DEFAULTUSEROBJQUOTA,
	    &zfsvfs->z_defaultuserobjquota);
	if (error != 0)
		return (error);

	error = zfs_get_zplprop(os, ZFS_PROP_DEFAULTGROUPOBJQUOTA,
	    &zfsvfs->z_defaultgroupobjquota);
	if (error != 0)
		return (error);

	error = zfs_get_zplprop(os, ZFS_PROP_DEFAULTPROJECTOBJQUOTA,
	    &zfsvfs->z_defaultprojectobjquota);
	if (error != 0)
		return (error);

	error = sa_setup(os, sa_obj, zfs_attr_table, ZPL_END,
	    &zfsvfs->z_attr_table);
	if (error != 0)
		return (error);

	if (zfsvfs->z_version >= ZPL_VERSION_SA)
		sa_register_update_callback(os, zfs_sa_upgrade);

	error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_ROOT_OBJ, 8, 1,
	    &zfsvfs->z_root);
	if (error != 0)
		return (error);
	ASSERT3U(zfsvfs->z_root, !=, 0);

	error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_UNLINKED_SET, 8, 1,
	    &zfsvfs->z_unlinkedobj);
	if (error != 0)
		return (error);

	error = zap_lookup(os, MASTER_NODE_OBJ,
	    zfs_userquota_prop_prefixes[ZFS_PROP_USERQUOTA],
	    8, 1, &zfsvfs->z_userquota_obj);
	if (error == ENOENT)
		zfsvfs->z_userquota_obj = 0;
	else if (error != 0)
		return (error);

	error = zap_lookup(os, MASTER_NODE_OBJ,
	    zfs_userquota_prop_prefixes[ZFS_PROP_GROUPQUOTA],
	    8, 1, &zfsvfs->z_groupquota_obj);
	if (error == ENOENT)
		zfsvfs->z_groupquota_obj = 0;
	else if (error != 0)
		return (error);

	error = zap_lookup(os, MASTER_NODE_OBJ,
	    zfs_userquota_prop_prefixes[ZFS_PROP_PROJECTQUOTA],
	    8, 1, &zfsvfs->z_projectquota_obj);
	if (error == ENOENT)
		zfsvfs->z_projectquota_obj = 0;
	else if (error != 0)
		return (error);

	error = zap_lookup(os, MASTER_NODE_OBJ,
	    zfs_userquota_prop_prefixes[ZFS_PROP_USEROBJQUOTA],
	    8, 1, &zfsvfs->z_userobjquota_obj);
	if (error == ENOENT)
		zfsvfs->z_userobjquota_obj = 0;
	else if (error != 0)
		return (error);

	error = zap_lookup(os, MASTER_NODE_OBJ,
	    zfs_userquota_prop_prefixes[ZFS_PROP_GROUPOBJQUOTA],
	    8, 1, &zfsvfs->z_groupobjquota_obj);
	if (error == ENOENT)
		zfsvfs->z_groupobjquota_obj = 0;
	else if (error != 0)
		return (error);

	error = zap_lookup(os, MASTER_NODE_OBJ,
	    zfs_userquota_prop_prefixes[ZFS_PROP_PROJECTOBJQUOTA],
	    8, 1, &zfsvfs->z_projectobjquota_obj);
	if (error == ENOENT)
		zfsvfs->z_projectobjquota_obj = 0;
	else if (error != 0)
		return (error);

	error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_FUID_TABLES, 8, 1,
	    &zfsvfs->z_fuid_obj);
	if (error == ENOENT)
		zfsvfs->z_fuid_obj = 0;
	else if (error != 0)
		return (error);

	error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_SHARES_DIR, 8, 1,
	    &zfsvfs->z_shares_dir);
	if (error == ENOENT)
		zfsvfs->z_shares_dir = 0;
	else if (error != 0)
		return (error);

	/*
	 * Only use the name cache if we are looking for a
	 * name on a file system that does not require normalization
	 * or case folding.
	 */
	zfsvfs->z_use_namecache = !zfsvfs->z_norm &&
	    zfsvfs->z_case == ZFS_CASE_SENSITIVE;

	return (0);
}

taskq_t *zfsvfs_taskq;

static void
zfsvfs_task_unlinked_drain(void *context, int pending __unused)
{
	zfsvfs_t *zfsvfs = context;

	if (zfsvfs->z_unmounted)
		return;
	zfs_unlinked_drain(zfsvfs);
}

int
zfsvfs_create(const char *osname, boolean_t readonly, zfsvfs_t **zfvp)
{
	objset_t *os;
	zfsvfs_t *zfsvfs;
	int error;
	boolean_t ro = (readonly || (strchr(osname, '@') != NULL));

	/*
	 * XXX: Fix struct statfs so this isn't necessary!
	 *
	 * The 'osname' is used as the filesystem's special node, which means
	 * it must fit in statfs.f_mntfromname, or else it can't be
	 * enumerated, so libzfs_mnttab_find() returns NULL, which causes
	 * 'zfs unmount' to think it's not mounted when it is.
	 */
	if (strlen(osname) >= ZFS_MNAMELEN)
		return (SET_ERROR(ENAMETOOLONG));

	zfsvfs = kmem_zalloc(sizeof (zfsvfs_t), KM_SLEEP);

	error = dmu_objset_own(osname, DMU_OST_ZFS, ro, B_TRUE, zfsvfs,
	    &os);
	if (error != 0) {
		kmem_free(zfsvfs, sizeof (zfsvfs_t));
		return (error);
	}

	error = zfsvfs_create_impl(zfvp, zfsvfs, os);

	return (error);
}

int
zfsvfs_create_hold(const char *osname, zfsvfs_t **zfvp)
{
	objset_t *os;
	zfsvfs_t *zfsvfs;
	int error;

	zfsvfs = kmem_zalloc(sizeof (zfsvfs_t), KM_SLEEP);

	error = dmu_objset_hold(osname, zfsvfs, &os);
	if (error != 0) {
		kmem_free(zfsvfs, sizeof (zfsvfs_t));
		return (error);
	}

	if (dmu_objset_type(os) != DMU_OST_ZFS) {
		dmu_objset_rele(os, zfsvfs);
		kmem_free(zfsvfs, sizeof (zfsvfs_t));
		return (EINVAL);
	}

	zfsvfs->z_use_hold = B_TRUE;
	error = zfsvfs_create_impl(zfvp, zfsvfs, os);

	return (error);
}

int
zfsvfs_create_impl(zfsvfs_t **zfvp, zfsvfs_t *zfsvfs, objset_t *os)
{
	int error;

	zfsvfs->z_vfs = NULL;
	zfsvfs->z_parent = zfsvfs;

	mutex_init(&zfsvfs->z_znodes_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&zfsvfs->z_lock, NULL, MUTEX_DEFAULT, NULL);
	list_create(&zfsvfs->z_all_znodes, sizeof (znode_t),
	    offsetof(znode_t, z_link_node));
	TASK_INIT(&zfsvfs->z_unlinked_drain_task, 0,
	    zfsvfs_task_unlinked_drain, zfsvfs);
	ZFS_TEARDOWN_INIT(zfsvfs);
	ZFS_TEARDOWN_INACTIVE_INIT(zfsvfs);
	rw_init(&zfsvfs->z_fuid_lock, NULL, RW_DEFAULT, NULL);
	for (int i = 0; i != ZFS_OBJ_MTX_SZ; i++)
		mutex_init(&zfsvfs->z_hold_mtx[i], NULL, MUTEX_DEFAULT, NULL);

	error = zfsvfs_init(zfsvfs, os);
	if (error != 0) {
		if (zfsvfs->z_use_hold)
			dmu_objset_rele(os, zfsvfs);
		else
			dmu_objset_disown(os, B_TRUE, zfsvfs);
		*zfvp = NULL;
		zfsvfs_free(zfsvfs);
		return (error);
	}

	*zfvp = zfsvfs;
	return (0);
}

static int
zfsvfs_setup(zfsvfs_t *zfsvfs, boolean_t mounting)
{
	int error;

	/*
	 * Check for a bad on-disk format version now since we
	 * lied about owning the dataset readonly before.
	 */
	if (!(zfsvfs->z_vfs->vfs_flag & VFS_RDONLY) &&
	    dmu_objset_incompatible_encryption_version(zfsvfs->z_os))
		return (SET_ERROR(EROFS));

	error = zfs_register_callbacks(zfsvfs->z_vfs);
	if (error)
		return (error);

	/*
	 * If we are not mounting (ie: online recv), then we don't
	 * have to worry about replaying the log as we blocked all
	 * operations out since we closed the ZIL.
	 */
	if (mounting) {
		boolean_t readonly;

		ASSERT0P(zfsvfs->z_kstat.dk_kstats);
		error = dataset_kstats_create(&zfsvfs->z_kstat, zfsvfs->z_os);
		if (error)
			return (error);
		zfsvfs->z_log = zil_open(zfsvfs->z_os, zfs_get_data,
		    &zfsvfs->z_kstat.dk_zil_sums);

		/*
		 * During replay we remove the read only flag to
		 * allow replays to succeed.
		 */
		readonly = zfsvfs->z_vfs->vfs_flag & VFS_RDONLY;
		if (readonly != 0) {
			zfsvfs->z_vfs->vfs_flag &= ~VFS_RDONLY;
		} else {
			dsl_dir_t *dd;
			zap_stats_t zs;

			if (zap_get_stats(zfsvfs->z_os, zfsvfs->z_unlinkedobj,
			    &zs) == 0) {
				dataset_kstats_update_nunlinks_kstat(
				    &zfsvfs->z_kstat, zs.zs_num_entries);
				dprintf_ds(zfsvfs->z_os->os_dsl_dataset,
				    "num_entries in unlinked set: %llu",
				    (u_longlong_t)zs.zs_num_entries);
			}

			zfs_unlinked_drain(zfsvfs);
			dd = zfsvfs->z_os->os_dsl_dataset->ds_dir;
			dd->dd_activity_cancelled = B_FALSE;
		}

		/*
		 * Parse and replay the intent log.
		 *
		 * Because of ziltest, this must be done after
		 * zfs_unlinked_drain().  (Further note: ziltest
		 * doesn't use readonly mounts, where
		 * zfs_unlinked_drain() isn't called.)  This is because
		 * ziltest causes spa_sync() to think it's committed,
		 * but actually it is not, so the intent log contains
		 * many txg's worth of changes.
		 *
		 * In particular, if object N is in the unlinked set in
		 * the last txg to actually sync, then it could be
		 * actually freed in a later txg and then reallocated
		 * in a yet later txg.  This would write a "create
		 * object N" record to the intent log.  Normally, this
		 * would be fine because the spa_sync() would have
		 * written out the fact that object N is free, before
		 * we could write the "create object N" intent log
		 * record.
		 *
		 * But when we are in ziltest mode, we advance the "open
		 * txg" without actually spa_sync()-ing the changes to
		 * disk.  So we would see that object N is still
		 * allocated and in the unlinked set, and there is an
		 * intent log record saying to allocate it.
		 */
		if (spa_writeable(dmu_objset_spa(zfsvfs->z_os))) {
			if (zil_replay_disable) {
				zil_destroy(zfsvfs->z_log, B_FALSE);
			} else {
				boolean_t use_nc = zfsvfs->z_use_namecache;
				zfsvfs->z_use_namecache = B_FALSE;
				zfsvfs->z_replay = B_TRUE;
				zil_replay(zfsvfs->z_os, zfsvfs,
				    zfs_replay_vector);
				zfsvfs->z_replay = B_FALSE;
				zfsvfs->z_use_namecache = use_nc;
			}
		}

		/* restore readonly bit */
		if (readonly != 0)
			zfsvfs->z_vfs->vfs_flag |= VFS_RDONLY;
	} else {
		ASSERT3P(zfsvfs->z_kstat.dk_kstats, !=, NULL);
		zfsvfs->z_log = zil_open(zfsvfs->z_os, zfs_get_data,
		    &zfsvfs->z_kstat.dk_zil_sums);
	}

	/*
	 * Set the objset user_ptr to track its zfsvfs.
	 */
	mutex_enter(&zfsvfs->z_os->os_user_ptr_lock);
	dmu_objset_set_user(zfsvfs->z_os, zfsvfs);
	mutex_exit(&zfsvfs->z_os->os_user_ptr_lock);

	return (0);
}

void
zfsvfs_free(zfsvfs_t *zfsvfs)
{
	int i;

	zfs_fuid_destroy(zfsvfs);

	mutex_destroy(&zfsvfs->z_znodes_lock);
	mutex_destroy(&zfsvfs->z_lock);
	list_destroy(&zfsvfs->z_all_znodes);
	ZFS_TEARDOWN_DESTROY(zfsvfs);
	ZFS_TEARDOWN_INACTIVE_DESTROY(zfsvfs);
	rw_destroy(&zfsvfs->z_fuid_lock);
	for (i = 0; i != ZFS_OBJ_MTX_SZ; i++)
		mutex_destroy(&zfsvfs->z_hold_mtx[i]);
	dataset_kstats_destroy(&zfsvfs->z_kstat);
	kmem_free(zfsvfs, sizeof (zfsvfs_t));
}

static void
zfs_set_fuid_feature(zfsvfs_t *zfsvfs)
{
	zfsvfs->z_use_fuids = USE_FUIDS(zfsvfs->z_version, zfsvfs->z_os);
	zfsvfs->z_use_sa = USE_SA(zfsvfs->z_version, zfsvfs->z_os);
}

extern int zfs_xattr_compat;

int
zfs_domount(vfs_t *vfsp, char *osname)
{
	uint64_t recordsize, fsid_guid;
	int error = 0;
	zfsvfs_t *zfsvfs;

	ASSERT3P(vfsp, !=, NULL);
	ASSERT3P(osname, !=, NULL);

	error = zfsvfs_create(osname, vfsp->mnt_flag & MNT_RDONLY, &zfsvfs);
	if (error)
		return (error);

	zfsvfs->z_vfs = vfsp;
	zfsvfs->z_mount_uid = crgetuid(CRED());
	zfs_acltype_apply_mnt(zfsvfs);
	if (vfsp->vfs_flag & VFS_NOSETUID)
		zfsvfs->z_policy_nosuid = B_TRUE;
	if (vfsp->vfs_flag & VFS_NODEVICES)
		zfsvfs->z_policy_nodev = B_TRUE;

	if ((error = dsl_prop_get_integer(osname,
	    "recordsize", &recordsize, NULL)))
		goto out;
	zfsvfs->z_vfs->vfs_bsize = SPA_MINBLOCKSIZE;
	zfsvfs->z_vfs->mnt_stat.f_iosize = recordsize;

	vfsp->vfs_data = zfsvfs;
	vfsp->mnt_flag |= MNT_LOCAL;
	/*
	 * mount_update() refuses an rw→ro downgrade unless this is
	 * set, and returns EOPNOTSUPP ("Operation not supported").
	 * That is mount -ur / remount_ro in zfs_mount_remount.
	 */
	vfsp->mnt_iflag |= IMNT_MPSAFE | IMNT_NCLOOKUP | IMNT_DTYPE;
	vfsp->mnt_iflag |= IMNT_SHRLOOKUP | IMNT_CAN_RWTORO;

	/*
	 * The fsid is 64 bits, composed of an 8-bit fs type, which
	 * separates our fsid from any other filesystem types, and a
	 * 56-bit objset unique ID.  The objset unique ID is unique to
	 * all objsets open on this system, provided by unique_create().
	 * The 8-bit fs type must be put in the low bits of fsid[1]
	 * because that's where other Solaris filesystems put it.
	 */
	fsid_guid = dmu_objset_fsid_guid(zfsvfs->z_os);
	ASSERT3U((fsid_guid & ~((1ULL << 56) - 1)), ==, 0);
	/* NetBSD fsid is in mnt_stat.f_fsidx.  Low 8 of [1] is fstype. */
	vfsp->mnt_stat.f_fsidx.__fsid_val[0] = (long)fsid_guid;
	vfsp->mnt_stat.f_fsidx.__fsid_val[1] =
	    (long)(((fsid_guid >> 32) << 8) | (makefstype(MOUNT_ZFS) & 0xff));

	/*
	 * Set features for file system.
	 */
	zfs_set_fuid_feature(zfsvfs);

	if (dmu_objset_is_snapshot(zfsvfs->z_os)) {
		uint64_t pval;

		atime_changed_cb(zfsvfs, B_FALSE);
		readonly_changed_cb(zfsvfs, B_TRUE);
		if ((error = dsl_prop_get_integer(osname,
		    "xattr", &pval, NULL)))
			goto out;
		xattr_changed_cb(zfsvfs, pval);
		if ((error = dsl_prop_get_integer(osname,
		    "acltype", &pval, NULL)))
			goto out;
		acl_type_changed_cb(zfsvfs, pval);
		zfsvfs->z_issnap = B_TRUE;
		zfsvfs->z_os->os_sync = ZFS_SYNC_DISABLED;

		mutex_enter(&zfsvfs->z_os->os_user_ptr_lock);
		dmu_objset_set_user(zfsvfs->z_os, zfsvfs);
		mutex_exit(&zfsvfs->z_os->os_user_ptr_lock);
	} else {
		if ((error = zfsvfs_setup(zfsvfs, B_TRUE)))
			goto out;
	}

	(void) strlcpy(vfsp->mnt_stat.f_mntfromname, osname,
	    sizeof (vfsp->mnt_stat.f_mntfromname));

	if (!zfsvfs->z_issnap)
		zfsctl_create(zfsvfs);
out:
	if (error) {
		dmu_objset_disown(zfsvfs->z_os, B_TRUE, zfsvfs);
		zfsvfs_free(zfsvfs);
	} else {
		spa_t *spa = zfsvfs->z_os->os_spa;

		atomic_inc_32(&zfs_active_fs_count);

		(void) spa;
	}

	return (error);
}

static void
zfs_unregister_callbacks(zfsvfs_t *zfsvfs)
{
	objset_t *os = zfsvfs->z_os;

	if (!dmu_objset_is_snapshot(os))
		dsl_prop_unregister_all(dmu_objset_ds(os), zfsvfs);
}

static void
fetch_osname_options(char *name, bool *checkpointrewind)
{

	if (name[0] == '!') {
		*checkpointrewind = true;
		memmove(name, name + 1, strlen(name));
	} else {
		*checkpointrewind = false;
	}
}

/*
 * Apply zfs_args.optptr tokens onto mnt_flag so vfs_optionisset()
 * sees temporary -o overrides (noatime, ro, nosuid, …) the same
 * way FreeBSD nmount iovecs do.  mount(2) only forwards MNT_* bits
 * that do_mount put in mntflags; the rest live in optptr.
 */
static void
zfs_apply_mntopts(vfs_t *vfsp, const char *opts)
{
	char *buf, *p, *tok;

	if (vfsp == NULL || opts == NULL || opts[0] == '\0')
		return;
	/*
	 * The option string is the whole temporary set, not a delta.
	 * Drop a previous noxattr/xattr/nbmand bag first.
	 */
	vfs_strmntopt_purge(vfsp);
	buf = kmem_alloc(MAXPATHLEN, KM_SLEEP);
	strlcpy(buf, opts, MAXPATHLEN);
	for (p = buf; (tok = strsep(&p, ",")) != NULL; ) {
		const char *opt = tok;

		if (*tok == '\0' || strcmp(tok, "defaults") == 0)
			continue;
		if (strcmp(tok, MNTOPT_REMOUNT) == 0 ||
		    strcmp(tok, "update") == 0)
			continue;
		if (strcmp(tok, "nosuid") == 0)
			opt = MNTOPT_NOSETUID;
		else if (strcmp(tok, "suid") == 0)
			opt = MNTOPT_SETUID;
		else if (strcmp(tok, "nodev") == 0 ||
		    strcmp(tok, "nodevices") == 0)
			opt = MNTOPT_NODEVICES;
		else if (strcmp(tok, "dev") == 0 ||
		    strcmp(tok, "devices") == 0)
			opt = MNTOPT_DEVICES;
		vfs_setmntopt(vfsp, opt, NULL, 0);
	}
	kmem_free(buf, MAXPATHLEN);
}

static int
zfs_mount(vfs_t *vfsp, const char *path, void *data, size_t *data_len)
{
	vnode_t		*mvp = vfsp->mnt_vnodecovered;
	cred_t		*cr = CRED();
	struct zfs_args	*uap = data;
	char		*osname;
	int		error = 0;
	bool		checkpointrewind;

	if (uap == NULL || data_len == NULL ||
	    *data_len < offsetof(struct zfs_args, mntonname)) {
		return (SET_ERROR(EINVAL));
	}

	osname = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);
	{
		size_t n = sizeof (uap->fspec);

		if (n > ZFS_MAX_DATASET_NAME_LEN - 1)
			n = ZFS_MAX_DATASET_NAME_LEN - 1;
		memcpy(osname, uap->fspec, n);
		osname[n] = '\0';
	}
	fetch_osname_options(osname, &checkpointrewind);

	error = secpolicy_fs_mount(cr, mvp, vfsp);
	if (error) {
		if (dsl_deleg_access(osname, ZFS_DELEG_PERM_MOUNT, cr) != 0)
			goto out;
		secpolicy_fs_mount_clearopts(cr, vfsp);
	}

	vfsp->mnt_iflag |= IMNT_MPSAFE | IMNT_NCLOOKUP;

	if ((vfsp->vfs_flag & MS_REMOUNT) == 0)
		vfs_strmntopt_purge(vfsp);

	if (vfsp->vfs_flag & MS_REMOUNT) {
		zfsvfs_t *zfsvfs = vfsp->vfs_data;
		boolean_t want_rw = ((vfsp->mnt_flag & MNT_RDONLY) == 0);

		/*
		 * Match Linux remount,rw: a snapshot, or a pool imported
		 * readonly, must fail the upgrade rather than silently
		 * stay ro (zfs_mount_remount log_mustnot remount_rw).
		 *
		 * Reject before zfs_apply_mntopts().  NetBSD
		 * mount_update() restores mnt_flag if VFS_MOUNT fails,
		 * but not mnt_stat.f_flag, which vfs_setmntopt writes.
		 */
		if (want_rw && (zfsvfs->z_issnap ||
		    !spa_writeable(dmu_objset_spa(zfsvfs->z_os)))) {
			error = SET_ERROR(EROFS);
			goto out;
		}

		if (*data_len >= offsetof(struct zfs_args, optlen) +
		    sizeof (uap->optlen) && uap->optptr[0] != '\0')
			zfs_apply_mntopts(vfsp, uap->optptr);

		/*
		 * Snapshots never register property callbacks
		 * (zfs_domount skips zfsvfs_setup).  remount,ro must
		 * not call zfs_register_callbacks — that returns
		 * EOPNOTSUPP.  Linux zfs_remount does the same.
		 */
		if (zfsvfs->z_issnap)
			goto out;

		ZFS_TEARDOWN_ENTER_WRITE(zfsvfs, FTAG);
		zfs_unregister_callbacks(zfsvfs);
		error = zfs_register_callbacks(vfsp);
		if (error != 0) {
			int rerr = zfs_register_callbacks(vfsp);

			/*
			 * Unregister already ran.  Retry so a
			 * transient dsl_prop_register failure does
			 * not leave the live mount with no property
			 * callbacks.  A second failure is logged;
			 * VFS still restores mnt_flag.
			 */
			if (rerr != 0) {
				cmn_err(CE_WARN,
				    "zfs_mount: remount of %s dropped "
				    "property callbacks (%d, retry %d)",
				    osname, error, rerr);
			} else {
				error = 0;
			}
		}
		ZFS_TEARDOWN_EXIT_WRITE(zfsvfs);
		goto out;
	}

	if (*data_len >= offsetof(struct zfs_args, optlen) +
	    sizeof (uap->optlen) && uap->optptr[0] != '\0')
		zfs_apply_mntopts(vfsp, uap->optptr);

	/*
	 * VFS_MOUNT() samples IMNT_MPSAFE before calling us.  A first
	 * mount has it clear, so KERNEL_LOCK is held across this
	 * function even after we set the flag.  zfs_domount may
	 * txg_wait, replay the ZIL and issue vdev I/O; those waiters
	 * need the lock.  Drop it for the duration.
	 */
	{
		int nlocks;

		KERNEL_UNLOCK_ALL(curlwp, &nlocks);
		error = zfs_domount(vfsp, osname);
		KERNEL_LOCK(nlocks, curlwp);
	}
	if (error == 0) {
		/*
		 * Record the mount(2) path, not zfs_args.mntonname.
		 * set_statvfs_info prepends cwdi_rdir when the
		 * caller is chrooted.  A failed copy unwinds the
		 * domount so the vfs is not left half-published.
		 */
		(void) strlcpy(vfsp->mnt_stat.f_mntfromname, osname,
		    sizeof (vfsp->mnt_stat.f_mntfromname));
		error = set_statvfs_info(path, UIO_USERSPACE, osname,
		    UIO_SYSSPACE, vfsp->mnt_op->vfs_name, vfsp, curlwp);
		if (error != 0)
			(void) zfs_umount(vfsp, MS_FORCE);
	}

out:
	kmem_free(osname, ZFS_MAX_DATASET_NAME_LEN);
	return (error);
}

static int
zfs_statvfs(vfs_t *vfsp, struct statvfs *statp)
{
	zfsvfs_t *zfsvfs = vfsp->vfs_data;
	uint64_t refdbytes, availbytes, usedobjs, availobjs;
	int error;

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	dmu_objset_space(zfsvfs->z_os,
	    &refdbytes, &availbytes, &usedobjs, &availobjs);

	/*
	 * The underlying storage pool actually uses multiple block sizes.
	 * We report the fragsize as the smallest block size we support,
	 * and we report our blocksize as the filesystem's maximum blocksize.
	 */
	statp->f_bsize = SPA_MINBLOCKSIZE;
	statp->f_frsize = SPA_MINBLOCKSIZE;
	statp->f_iosize = zfsvfs->z_vfs->mnt_stat.f_iosize;

	/*
	 * The following report "total" blocks of various kinds in the
	 * file system, but reported in terms of f_frsize - the
	 * "fragment" size.
	 */

	statp->f_blocks = (refdbytes + availbytes) >> SPA_MINBLOCKSHIFT;
	statp->f_bfree = availbytes / statp->f_bsize;
	statp->f_bavail = statp->f_bfree; /* no root reservation */

	/*
	 * statvfs() should really be called statufs(), because it assumes
	 * static metadata.  ZFS doesn't preallocate files, so the best
	 * we can do is report the max that could possibly fit in f_files,
	 * and that minus the number actually used in f_ffree.
	 * For f_ffree, report the smaller of the number of object available
	 * and the number of blocks (each object will take at least a block).
	 */
	statp->f_ffree = MIN(availobjs, statp->f_bfree);
	statp->f_files = statp->f_ffree + usedobjs;

	/*
	 * We're a zfs filesystem.
	 */
	strlcpy(statp->f_fstypename, "zfs",
	    sizeof (statp->f_fstypename));

	strlcpy(statp->f_mntfromname, vfsp->mnt_stat.f_mntfromname,
	    sizeof (statp->f_mntfromname));
	strlcpy(statp->f_mntonname, vfsp->mnt_stat.f_mntonname,
	    sizeof (statp->f_mntonname));
	/*
	 * mount(8) prints f_flag.  Keep it in sync with the MNT_*
	 * bits vfs_setmntopt() (and the property callbacks) maintain.
	 */
	statp->f_flag = vfsp->mnt_flag;

	statp->f_namemax =
	    zfsvfs->z_longname ? (ZAP_MAXNAMELEN_NEW - 1) : (MAXNAMELEN - 1);

	zfs_exit(zfsvfs, FTAG);
	return (0);
}

static int
zfs_root(vfs_t *vfsp, int flags, vnode_t **vpp)
{
	zfsvfs_t *zfsvfs = vfsp->vfs_data;
	znode_t *rootzp;
	int error;

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	error = zfs_zget(zfsvfs, zfsvfs->z_root, &rootzp);
	if (error == 0) {
		*vpp = ZTOV(rootzp);
		(*vpp)->v_vflag |= VV_ROOT | VV_MPSAFE;
	}

	zfs_exit(zfsvfs, FTAG);

	if (error == 0) {
		error = vn_lock(*vpp, flags);
		if (error != 0) {
			VN_RELE(*vpp);
			*vpp = NULL;
		}
	}
	return (error);
}

/*
 * Teardown the zfsvfs::z_os.
 *
 * Note, if 'unmounting' is FALSE, we return with the 'z_teardown_lock'
 * and 'z_teardown_inactive_lock' held.
 */
static int
zfsvfs_teardown(zfsvfs_t *zfsvfs, boolean_t unmounting)
{
	znode_t	*zp;
	dsl_dir_t *dd;

	/*
	 * If someone has not already unmounted this file system,
	 * drain the zrele_taskq to ensure all active references to the
	 * zfsvfs_t have been handled only then can it be safely destroyed.
	 */
	if (zfsvfs->z_os) {
		/*
		 * If we're unmounting we have to wait for the list to
		 * drain completely.
		 *
		 * If we're not unmounting there's no guarantee the list
		 * will drain completely, but zreles run from the taskq
		 * may add the parents of dir-based xattrs to the taskq
		 * so we want to wait for these.
		 *
		 * We can safely check z_all_znodes for being empty because the
		 * VFS has already blocked operations which add to it.
		 */
		int round = 0;
		/*
		 * Same wait as Linux/FreeBSD.  VN_RELE_ASYNC dispatches
		 * onto this taskq; VOP_INACTIVE recycles during unmount;
		 * leftover vnodes are vgone'd before this wait.  The
		 * wait is for xattr-parent zreles those vgones queued.
		 *
		 * vp==NULL orphans are not on the zrele taskq.  Reap
		 * them before and inside the wait or the list stays
		 * nonempty forever and zfs_nb_reap_znodes after
		 * teardown never runs.
		 */
		if (unmounting)
			zfs_nb_reap_znodes(zfsvfs);
		while (!list_is_empty(&zfsvfs->z_all_znodes)) {
			taskq_wait_outstanding(dsl_pool_zrele_taskq(
			    dmu_objset_pool(zfsvfs->z_os)), 0);
			if (unmounting)
				zfs_nb_reap_znodes(zfsvfs);
			/*
			 * Live znodes (ZTOV != NULL) are not on the
			 * zrele taskq.  Waiting again cannot drain
			 * them; leftover vgones before this wait
			 * already queued their xattr-parent zreles.
			 * Bound both paths so one leftover vnode cannot
			 * spin forever.
			 */
			if (++round > 1)
				break;
		}
	}
	/*
	 * Child mounts whose covered directory this rollback or receive
	 * deletes are detached in zfs_resume_fs(), after rezget.  SA
	 * handles are still valid here, so every directory looks live.
	 */
	ZFS_TEARDOWN_ENTER_WRITE(zfsvfs, FTAG);

	if (!unmounting) {
		/*
		 * We purge the parent filesystem's vfsp as the parent
		 * filesystem and all of its snapshots have their vnode's
		 * v_vfsp set to the parent's filesystem's vfsp.  Note,
		 * 'z_parent' is self referential for non-snapshots.
		 */
		cache_purgevfs(zfsvfs->z_parent->z_vfs);
	}

	/*
	 * Close the zil. NB: Can't close the zil while zfs_inactive
	 * threads are blocked as zil_close can call zfs_inactive.
	 */
	if (zfsvfs->z_log) {
		zil_close(zfsvfs->z_log);
		zfsvfs->z_log = NULL;
	}

	ZFS_TEARDOWN_INACTIVE_ENTER_WRITE(zfsvfs);

	/*
	 * If we are not unmounting (ie: online recv) and someone already
	 * unmounted this file system while we were doing the switcheroo,
	 * or a reopen of z_os failed then just bail out now.
	 */
	if (!unmounting && (zfsvfs->z_unmounted || zfsvfs->z_os == NULL)) {
		ZFS_TEARDOWN_INACTIVE_EXIT_WRITE(zfsvfs);
		ZFS_TEARDOWN_EXIT(zfsvfs, FTAG);
		return (SET_ERROR(EIO));
	}

	/*
	 * At this point there are no vops active, and any new vops will
	 * fail with EIO since we have z_teardown_lock for writer (only
	 * relevant for forced unmount).
	 *
	 * Release all holds on dbufs.
	 */
	mutex_enter(&zfsvfs->z_znodes_lock);
	for (zp = list_head(&zfsvfs->z_all_znodes); zp != NULL;
	    zp = list_next(&zfsvfs->z_all_znodes, zp)) {
		if (zp->z_sa_hdl != NULL) {
			zfs_znode_dmu_fini(zp);
		}
	}
	mutex_exit(&zfsvfs->z_znodes_lock);

	/*
	 * If we are unmounting, set the unmounted flag and let new vops
	 * unblock.  zfs_inactive will have the unmounted behavior, and all
	 * other vops will fail with EIO.
	 */
	if (unmounting) {
		zfsvfs->z_unmounted = B_TRUE;
		ZFS_TEARDOWN_INACTIVE_EXIT_WRITE(zfsvfs);
		ZFS_TEARDOWN_EXIT(zfsvfs, FTAG);
	}

	/*
	 * z_os will be NULL if there was an error in attempting to reopen
	 * zfsvfs, so just return as the properties had already been
	 * unregistered and cached data had been evicted before.
	 */
	if (zfsvfs->z_os == NULL)
		return (0);

	/*
	 * Unregister properties.
	 */
	zfs_unregister_callbacks(zfsvfs);

	/*
	 * Evict cached data. We must write out any dirty data before
	 * disowning the dataset.
	 */
	objset_t *os = zfsvfs->z_os;
	boolean_t os_dirty = B_FALSE;
	for (int t = 0; t < TXG_SIZE; t++) {
		if (dmu_objset_is_dirty(os, t)) {
			os_dirty = B_TRUE;
			break;
		}
	}
	if (!zfs_is_readonly(zfsvfs) && os_dirty)
		txg_wait_synced(dmu_objset_pool(zfsvfs->z_os), 0);
	dmu_objset_evict_dbufs(zfsvfs->z_os);
	dd = zfsvfs->z_os->os_dsl_dataset->ds_dir;
	dsl_dir_cancel_waiters(dd);

	return (0);
}

/*
 * NetBSD vflush can leave cached vnodes on the mount.  Reclaim them
 * so z_all_znodes can drain before teardown waits on the zrele taskq.
 * zil_close may vcache_get() new files, so this is also run after
 * teardown.  Do not call this if you still intend to abort the umount.
 */
static int
zfs_nb_vgone_leftovers(vfs_t *vfsp)
{
	struct vnode_iterator *vi = NULL;
	vnode_t *vp;
	int left = 0;

	cache_purgevfs(vfsp);
	vfs_vnode_iterator_init(vfsp, &vi);
	while ((vp = vfs_vnode_iterator_next(vi, NULL, NULL)) != NULL) {
		/*
		 * Cover vnodes still have a snapshot mounted.
		 * vgone would yank v_mountedhere out from under
		 * dounmount.  iterator_next vgets; vgone consumes
		 * that hold.
		 */
		if (vp->v_mountedhere != NULL) {
			vrele(vp);
			continue;
		}
		vgone(vp);
	}
	vfs_vnode_iterator_destroy(vi);

	vfs_vnode_iterator_init(vfsp, &vi);
	while ((vp = vfs_vnode_iterator_next(vi, NULL, NULL)) != NULL) {
		left++;
		vrele(vp);
	}
	vfs_vnode_iterator_destroy(vi);
	return (left);
}

/*
 * After leftover vgones, only vp==NULL orphans can remain on
 * z_all_znodes without sitting on the zrele taskq.  Reap those
 * before (and inside) the teardown wait.  Do not vref a live
 * leftover -- NetBSD vref() KASSERTs usecount > 0 on DIAGNOSTIC,
 * and reap-then-wait of live vnodes races the xattr-parent zrele
 * the leftover vgones just queued.
 */
static void
zfs_nb_reap_znodes(zfsvfs_t *zfsvfs)
{
	znode_t *zp;

restart:
	mutex_enter(&zfsvfs->z_znodes_lock);
	for (zp = list_head(&zfsvfs->z_all_znodes); zp != NULL;
	    zp = list_next(&zfsvfs->z_all_znodes, zp)) {
		if (ZTOV(zp) != NULL)
			continue;
		/* zfs_znode_reap_orphan drops z_znodes_lock. */
		zfs_znode_reap_orphan(zp);
		goto restart;
	}
	mutex_exit(&zfsvfs->z_znodes_lock);
}

static int zfs_cwd_on_mount(struct mount *, boolean_t *);

static int
zfs_umount(vfs_t *vfsp, int fflag)
{
	zfsvfs_t *zfsvfs = vfsp->vfs_data;
	objset_t *os;
	cred_t *cr = CRED();
	int ret, left;

	ret = secpolicy_fs_unmount(cr, vfsp);
	if (ret) {
		const char *from = vfsp->mnt_stat.f_mntfromname;

		if (dsl_deleg_access(from, ZFS_DELEG_PERM_MOUNT, cr))
			return (ret);
	}

	/*
	 * Do not force-unmount the filesystem this thread is standing
	 * in when that would vflush a .zfs/snapshot cover mount.  A
	 * plain cwd on this dataset is not a reason to ignore
	 * MNT_FORCE: destroy -f, umount -f, and unmount -fa stand
	 * inside the mount and expect it to detach (zfs_destroy_004,
	 * zfs_unmount_005, zfs_unmount_all_001).  A cwd inside
	 * .zfs/snapshot must stay EBUSY so the directory remains
	 * usable (zfs_unmount_009_pos).
	 */
	{
		boolean_t snapcover = B_FALSE;

		if (zfs_cwd_on_mount(vfsp, &snapcover) &&
		    (snapcover || (fflag & MS_FORCE) == 0))
			return (SET_ERROR(EBUSY));
	}

	/*
	 * Unmount any snapshots mounted under .zfs before unmounting the
	 * dataset itself.
	 */
	if (zfsvfs->z_ctldir != NULL) {
		if ((ret = zfsctl_umount_snapshots(vfsp, fflag, cr)) != 0)
			return (ret);
	}
	cache_purgevfs(vfsp);

	/*
	 * POSIX ACL xattrs must be on disk before vflush reclaims
	 * the xattr dir (posix_003 remount + getfacl).
	 */
	if (!zfs_is_readonly(zfsvfs) && zfsvfs->z_os != NULL)
		txg_wait_synced(dmu_objset_pool(zfsvfs->z_os), 0);

	if (fflag & MS_FORCE) {
		/*
		 * Mark file system as unmounted before calling
		 * vflush(FORCECLOSE). This way we ensure no future vnops
		 * will be called and risk operating on DOOMED vnodes.
		 */
		ZFS_TEARDOWN_ENTER_WRITE(zfsvfs, FTAG);
		zfsvfs->z_unmounted = B_TRUE;
		ZFS_TEARDOWN_EXIT(zfsvfs, FTAG);
	}

	/*
	 * Finish in-flight unlinked-drain before vflush vgones
	 * those vnodes.  POSIX ACL xattr deletes enqueue this
	 * task; vn_lock(LK_RETRY) vs vflush was a tqdrain deadlock.
	 */
	(void) taskqueue_drain((struct taskqueue *)zfsvfs_taskq,
	    &zfsvfs->z_unlinked_drain_task);

	/*
	 * Flush all the files. Honor MS_FORCE -- a plain umount of a
	 * busy mount must return EBUSY so destroy is not a silent
	 * revoke of open files.  Retry EBUSY briefly: NetBSD
	 * VOP_INACTIVE is deferred, so a just-unlinked file can
	 * still hold a usecount for a tick (channel-program
	 * rollback tests unmount immediately after rm).
	 */
	{
		int tries;

		for (tries = 0; tries < 3; tries++) {
			ret = vflush(vfsp, NULL,
			    (fflag & MS_FORCE) ? FORCECLOSE : 0);
			if (ret != EBUSY || (fflag & MS_FORCE))
				break;
			(void) kpause("zfsvfl", false, hz / 10, NULL);
		}
	}
	if (ret != 0)
		return (ret);
	(void) taskqueue_drain((struct taskqueue *)zfsvfs_taskq,
	    &zfsvfs->z_unlinked_drain_task);
	/*
	 * Reclaim anything vflush left behind *before* the teardown
	 * wait (iterator already holds; vgone consumes that hold).
	 * Orphans (vp == NULL) are reaped inside teardown -- they
	 * are not on the zrele taskq.  After a successful vflush
	 * these are idle cached vnodes, not busy opens.
	 */
	(void) zfs_nb_vgone_leftovers(vfsp);

	VERIFY0(zfsvfs_teardown(zfsvfs, B_TRUE));
	zfs_nb_reap_znodes(zfsvfs);

	/*
	 * zil_close / teardown can vcache_get() files after the first
	 * vflush.  The first pass already succeeded; we cannot abort
	 * after teardown.  FORCECLOSE leftovers; leftover vgone is
	 * the recovery if vflush still returns an error.
	 */
	(void) vflush(vfsp, NULL, FORCECLOSE);
	left = zfs_nb_vgone_leftovers(vfsp);
	if (left != 0)
		(void) vflush(vfsp, NULL, FORCECLOSE);

	os = zfsvfs->z_os;

	/*
	 * z_os will be NULL if there was an error in
	 * attempting to reopen zfsvfs.
	 */
	if (os != NULL) {
		/*
		 * Unset the objset user_ptr.
		 */
		mutex_enter(&os->os_user_ptr_lock);
		dmu_objset_set_user(os, NULL);
		mutex_exit(&os->os_user_ptr_lock);

		/*
		 * Finally release the objset
		 */
		dmu_objset_disown(os, B_TRUE, zfsvfs);
	}

	/*
	 * We can now safely destroy the '.zfs' directory node.
	 */
	if (zfsvfs->z_ctldir != NULL)
		zfsctl_destroy(zfsvfs);
	zfs_freevfs(vfsp);
	vfs_strmntopt_purge(vfsp);

	return (0);
}

static int
zfs_vget(vfs_t *vfsp, ino_t ino, int flags, vnode_t **vpp)
{
	zfsvfs_t	*zfsvfs = vfsp->vfs_data;
	znode_t		*zp;
	int 		err;

	/*
	 * zfs_zget() can't operate on virtual entries like .zfs/ or
	 * .zfs/snapshot/ directories, that's why we return EOPNOTSUPP.
	 * This will make NFS to switch to LOOKUP instead of using VGET.
	 */
	if (ino == ZFSCTL_INO_ROOT || ino == ZFSCTL_INO_SNAPDIR ||
	    (zfsvfs->z_shares_dir != 0 && ino == zfsvfs->z_shares_dir))
		return (EOPNOTSUPP);

	if ((err = zfs_enter(zfsvfs, FTAG)) != 0)
		return (err);
	err = zfs_zget(zfsvfs, ino, &zp);
	if (err == 0)
		*vpp = ZTOV(zp);
	zfs_exit(zfsvfs, FTAG);
	if (err == 0) {
		err = vn_lock(*vpp, flags);
		if (err != 0)
			vrele(*vpp);
	}
	if (err != 0)
		*vpp = NULL;
	return (err);
}

/*
 * Block out VOPs and close zfsvfs_t::z_os
 *
 * Note, if successful, then we return with the 'z_teardown_lock' and
 * 'z_teardown_inactive_lock' write held.  We leave ownership of the underlying
 * dataset and objset intact so that they can be atomically handed off during
 * a subsequent rollback or recv operation and the resume thereafter.
 */
int
zfs_suspend_fs(zfsvfs_t *zfsvfs)
{
	int error;

	/*
	 * vfs_suspend takes the global vfs_suspend_lock and holds it
	 * until vfs_resume.  Teardown already excludes new zfs_enter
	 * via z_teardown_lock.  Drop the VFS interlock as soon as
	 * teardown finishes so a later mount_domount vfs_suspend
	 * (nested child mount) is not blocked behind rollback/recv
	 * and zfs_resume_fs rezget/vgone.
	 */
	if ((error = vfs_suspend(zfsvfs->z_vfs, 0)) != 0)
		return (error);
	if ((error = zfsvfs_teardown(zfsvfs, B_FALSE)) != 0) {
		vfs_resume(zfsvfs->z_vfs);
		return (error);
	}
	vfs_resume(zfsvfs->z_vfs);

	return (0);
}

/*
 * NetBSD 11 unmount(2) finds a mount only by namei of its path.
 * A child mount whose covered directory this rollback or receive
 * removed has no path left.  Unmount it here, not forced: a cwd
 * or an open file keeps it, as before.  Deepest first.
 *
 * vfs_ref, dounmount, vfs_rele is what sys_unmount() does.  The
 * rollback and receive ioctls hold vfs_busy on this mount.  A
 * child dounmount only vrele's a vnode of that mount, on this
 * thread.  Mounts are appended, so a parent usually precedes
 * its children.  The walk repeats so a child that was inserted
 * earlier is still found.  trynext does not wait: a suspended
 * mount is skipped, not a reason to stall resume.
 */
static void
zfs_nb_unmount_uncovered(zfsvfs_t *zfsvfs)
{
	mount_iterator_t *iter;
	struct mount *mp, **mps = NULL;
	int n = 0, max = 0, i, j;
	boolean_t grew;

	if (zfsvfs == NULL || zfsvfs->z_vfs == NULL)
		return;

	grew = B_TRUE;
	while (grew) {
		grew = B_FALSE;
		mountlist_iterator_init(&iter);
		while ((mp = mountlist_iterator_trynext(iter)) != NULL) {
			vnode_t *cvp = mp->mnt_vnodecovered;
			boolean_t take = B_FALSE;

			if (mp == zfsvfs->z_vfs || cvp == NULL)
				continue;
			for (j = 0; j < n; j++) {
				if (mps[j] == mp) {
					take = B_TRUE;
					break;
				}
			}
			if (take)
				continue;
			take = B_FALSE;
			if (cvp->v_op == zfs_vnodeop_p &&
			    cvp->v_mount == zfsvfs->z_vfs) {
				znode_t *zp;

				/*
				 * DIAGNOSTIC VTOZ() asserts v_data.
				 * A covered vnode with no znode is
				 * already detached; the mount has
				 * no path.
				 */
				if (cvp->v_data == NULL) {
					take = B_TRUE;
				} else {
					zp = VTOZ(cvp);
					take = (zp->z_nb_orphaned ||
					    zp->z_sa_hdl == NULL);
				}
			} else {
				for (j = 0; j < n && !take; j++) {
					if (cvp->v_mount == mps[j])
						take = B_TRUE;
				}
			}
			if (!take)
				continue;
			if (n == max) {
				struct mount **nm = kmem_alloc(
				    (max + 8) * sizeof (*nm), KM_SLEEP);

				if (n > 0) {
					memcpy(nm, mps, n * sizeof (*nm));
					kmem_free(mps, max * sizeof (*mps));
				}
				mps = nm;
				max += 8;
			}
			vfs_ref(mp);
			mps[n++] = mp;
			grew = B_TRUE;
		}
		mountlist_iterator_destroy(iter);
	}

	for (i = n - 1; i >= 0; i--) {
		int error = dounmount(mps[i], 0, curlwp);

		if (error != 0)
			printf("ZFS: %s: covered directory removed, "
			    "unmount failed (%d)\n",
			    mps[i]->mnt_stat.f_mntonname, error);
		vfs_rele(mps[i]);
	}
	if (max > 0)
		kmem_free(mps, max * sizeof (*mps));
}

#ifdef __NetBSD__
static bool
zfs_resume_selector(void *cl, struct vnode *vp)
{

	(void) cl;
	if (zfsctl_is_node(vp))
		return (false);
	/*
	 * Still hashed by object id, but rezget failed.  Orphan that
	 * znode.  A vnode with no znode is reclaiming; vcache_new
	 * waits.  Do not vgone.
	 */
	if (vp->v_data == NULL)
		return (false);
	return (VTOZ(vp)->z_sa_hdl == NULL);
}
#endif

/*
 * Rebuild SA and release VOPs.  Note that ownership of the underlying dataset
 * is an invariant across any of the operations that can be performed while the
 * filesystem was suspended.  Whether it succeeded or failed, the preconditions
 * are the same: the relevant objset and associated dataset are owned by
 * zfsvfs, held, and long held on entry.
 */
int
zfs_resume_fs(zfsvfs_t *zfsvfs, dsl_dataset_t *ds)
{
	int err;
	znode_t *zp;

	ASSERT(ZFS_TEARDOWN_WRITE_HELD(zfsvfs));
	ASSERT(ZFS_TEARDOWN_INACTIVE_WRITE_HELD(zfsvfs));

	/*
	 * We already own this, so just update the objset_t, as the one we
	 * had before may have been evicted.
	 */
	objset_t *os;
	VERIFY3P(ds->ds_owner, ==, zfsvfs);
	VERIFY(dsl_dataset_long_held(ds));
	dsl_pool_t *dp = spa_get_dsl(dsl_dataset_get_spa(ds));
	dsl_pool_config_enter(dp, FTAG);
	VERIFY0(dmu_objset_from_ds(ds, &os));
	dsl_pool_config_exit(dp, FTAG);

	err = zfsvfs_init(zfsvfs, os);
	if (err != 0)
		goto bail;

	ds->ds_dir->dd_activity_cancelled = B_FALSE;
	VERIFY0(zfsvfs_setup(zfsvfs, B_FALSE));

	zfs_set_fuid_feature(zfsvfs);

	/*
	 * Attempt to re-establish all the active znodes with
	 * their dbufs.  If a zfs_rezget() fails, then we'll let
	 * any potential callers discover that via zfs_enter_verify_zp
	 * when they try to use their znode.
	 */
	mutex_enter(&zfsvfs->z_znodes_lock);
	for (zp = list_head(&zfsvfs->z_all_znodes); zp;
	    zp = list_next(&zfsvfs->z_all_znodes, zp)) {
		(void) zfs_rezget(zp);
	}
	mutex_exit(&zfsvfs->z_znodes_lock);

bail:
	/* release the VOPs */
	ZFS_TEARDOWN_INACTIVE_EXIT_WRITE(zfsvfs);
	ZFS_TEARDOWN_EXIT(zfsvfs, FTAG);

#ifdef __NetBSD__
	/*
	 * Re-key vnodes whose object id rollback or recv is about to
	 * reuse.  zfs_rezget already orphans the ones it detached,
	 * including z_unlinked.  This pass catches a vnode that was
	 * already detached.  A vnode with no znode is in reclaim;
	 * vcache_new waits for that, and there is no znode to store
	 * a non-colliding key in.  Do not re-key it onto &v_data:
	 * that is 8 zero bytes, object id 0.
	 *
	 * vgone needs fstrans_is_owner.  Re-key does not take fstrans
	 * and does not change LWPs.  A mounted rollback (vfs_trybusy
	 * in getzfsvfs, then this resume) has been smoked.  A
	 * rezget-failure orphan — object gone, key moved off the
	 * object id — has not been forced.
	 */
	{
		struct vnode_iterator *marker;
		vnode_t *vp;
		struct mount *mp = zfsvfs->z_vfs;

		vfs_vnode_iterator_init(mp, &marker);
		while ((vp = vfs_vnode_iterator_next(marker,
		    zfs_resume_selector, NULL)) != NULL) {
			if (vp->v_data != NULL)
				zfs_nb_orphan(VTOZ(vp));
			vrele(vp);
		}
		vfs_vnode_iterator_destroy(marker);
	}
	/*
	 * Only after a successful rezget.  Teardown has already
	 * dropped every SA handle; on the bail path they are all
	 * NULL and every child would look uncovered.
	 */
	if (err == 0)
		zfs_nb_unmount_uncovered(zfsvfs);
	else {
		vfs_t *mp = zfsvfs->z_vfs;
		int uerr;

		/*
		 * zfsvfs_init failed.  Suspend already dropped every
		 * SA handle and closed the ZIL, and the teardown
		 * locks are released, so the mount is still listed.
		 * FreeBSD force-unmounts through dounmount.
		 * z_nb_holds keeps the zfsvfs until the last
		 * zfs_vfs_rele, including this ioctl.  vfs_busy does
		 * not delay dounmount: suspendctl does not drain
		 * fstrans.
		 *
		 * A mounted snapshot makes this return EBUSY.  The
		 * suspend lock is global, so the parent unmount
		 * cannot dounmount that snapshot.  Leave the mount.
		 */
		vfs_ref(mp);
		uerr = dounmount(mp, MNT_FORCE, curlwp);
		vfs_rele(mp);
		if (uerr != 0) {
			printf("ZFS: zfs_resume_fs failed (%d); "
			    "force-unmount failed (%d)\n", err, uerr);
		}
	}
#endif

	return (err);
}

static void
zfs_freevfs(vfs_t *vfsp)
{
	zfsvfs_t *zfsvfs = vfsp->vfs_data;
	uint32_t oldv, newv;

	/*
	 * The objset user pointer is already clear, so getzfsvfs
	 * cannot add a pin.  Set the gone bit.  If the count was
	 * already zero, free here.  Otherwise the last zfs_vfs_rele
	 * frees after vfs_unbusy.  That is resume failure and a
	 * concurrent unmount -f.  Do not take z_lock: getzfsvfs_impl
	 * pins under the pool config lock.
	 */
	for (;;) {
		oldv = atomic_load_relaxed(&zfsvfs->z_nb_holds);
		if ((oldv & ZFS_NB_HOLD_GONE) != 0)
			return;
		newv = oldv | ZFS_NB_HOLD_GONE;
		if (atomic_cas_32(&zfsvfs->z_nb_holds, oldv, newv) == oldv)
			break;
	}
	if (oldv != 0)
		return;
	vfsp->vfs_data = NULL;
	zfsvfs_free(zfsvfs);
	atomic_dec_32(&zfs_active_fs_count);
}

static arc_prune_t *zfs_prune;

/*
 * NetBSD calls vfs_init/vfs_done from vfs_attach/vfs_detach.  OpenZFS
 * zfs_init/zfs_fini are driven from zfs_kmod_init() instead -- do not
 * hook them here or they run twice and the error path double-frees.
 */
char zfs_bootfs[MNAMELEN];

#ifndef _MODULE
/*
 * Fill zfs_bootfs from the boot loader if the sysctl is still empty.
 * NetBSD passes the root spec as BTINFO_ROOTDEVICE "zfs:pool/ds".
 * Compiled only for in-kernel builds -- see vfs_mountroot below.
 */
static void
zfs_bootfs_from_bootinfo(void)
{
#ifdef BTINFO_ROOTDEVICE
	struct btinfo_rootdevice *bi;

	if (zfs_bootfs[0] != '\0')
		return;
	bi = lookup_bootinfo(BTINFO_ROOTDEVICE);
	if (bi != NULL && strncmp(bi->devname, "zfs:", 4) == 0)
		strlcpy(zfs_bootfs, bi->devname + 4, sizeof (zfs_bootfs));
#endif
}

/*
 * Kernel root-on-ZFS.  Not used by the external kmod: modules load
 * after the root mount, so this pointer is omitted from vfsops
 * when _MODULE is defined.  Kept for a future GENERIC+ZFS kernel.
 *
 * Set vfs.zfs.bootfs or pass root=zfs:pool/ROOT/default from the
 * loader.  The pool must already be importable (cachefile or a
 * label on rootvp).
 */
static int
zfs_mountroot(void)
{
	struct mount *mp;
	char osname[MNAMELEN];
	int error;

	zfs_bootfs_from_bootinfo();
	if (zfs_bootfs[0] == '\0')
		return (ENODEV);
	strlcpy(osname, zfs_bootfs, sizeof (osname));
	if ((error = vfs_rootmountalloc(MOUNT_ZFS, osname, &mp)) != 0)
		return (error);
	error = zfs_domount(mp, osname);
	if (error != 0) {
		vfs_unbusy(mp);
		vfs_rele(mp);
		return (error);
	}
	strlcpy(mp->mnt_stat.f_mntfromname, osname,
	    sizeof (mp->mnt_stat.f_mntfromname));
	strlcpy(mp->mnt_stat.f_mntonname, "/",
	    sizeof (mp->mnt_stat.f_mntonname));
	mountlist_append(mp);
	vfs_unbusy(mp);
	return (0);
}
#endif /* !_MODULE */

static void
zfs_vfs_init(void)
{
	extern void zfs_netbsd_pager_init(void);

	zfs_netbsd_pager_init();
}

static void
zfs_vfs_done(void)
{
}

void
zfs_init(void)
{
	extern void zfs_netbsd_vnops_taskq_init(void);
	extern void zfs_netbsd_zcp_taskq_init(void);

	printf("ZFS filesystem version: " ZPL_VERSION_STRING "\n");

	/*
	 * Initialize .zfs directory structures
	 */
	zfsctl_init();

	/*
	 * Initialize znode cache, vnode ops, etc...
	 */
	zfs_znode_init();

	dmu_objset_register_type(DMU_OST_ZFS, zpl_get_file_info);

	zfsvfs_taskq = taskq_create("zfsvfs", 1, minclsyspri, 0, 0, 0);
	zfs_netbsd_vnops_taskq_init();
	zfs_netbsd_zcp_taskq_init();

	zfs_prune = NULL;
	vfs_strmntopt_init();
}

void
zfs_fini(void)
{
	extern void zfs_netbsd_vnops_taskq_fini(void);
	extern void zfs_netbsd_zcp_taskq_fini(void);

	if (zfs_prune != NULL) {
		arc_remove_prune_callback(zfs_prune);
		zfs_prune = NULL;
	}

	zfs_netbsd_zcp_taskq_fini();
	zfs_netbsd_vnops_taskq_fini();
	if (zfsvfs_taskq != NULL) {
		taskq_destroy(zfsvfs_taskq);
		zfsvfs_taskq = NULL;
	}
	zfsctl_fini();
	zfs_znode_fini();
	vfs_strmntopt_fini();
}

int
zfs_busy(void)
{
	return (zfs_active_fs_count != 0);
}

/*
 * Release VOPs and unmount a suspended filesystem.
 */
int
zfs_end_fs(zfsvfs_t *zfsvfs, dsl_dataset_t *ds)
{
	ASSERT(ZFS_TEARDOWN_WRITE_HELD(zfsvfs));
	ASSERT(ZFS_TEARDOWN_INACTIVE_WRITE_HELD(zfsvfs));

	/*
	 * We already own this, so just hold and rele it to update the
	 * objset_t, as the one we had before may have been evicted.
	 */
	objset_t *os;
	VERIFY3P(ds->ds_owner, ==, zfsvfs);
	VERIFY(dsl_dataset_long_held(ds));
	dsl_pool_t *dp = spa_get_dsl(dsl_dataset_get_spa(ds));
	dsl_pool_config_enter(dp, FTAG);
	VERIFY0(dmu_objset_from_ds(ds, &os));
	dsl_pool_config_exit(dp, FTAG);
	zfsvfs->z_os = os;

	/* release the VOPs */
	ZFS_TEARDOWN_INACTIVE_EXIT_WRITE(zfsvfs);
	ZFS_TEARDOWN_EXIT(zfsvfs, FTAG);

	/*
	 * The dataset is going away.  Not forced: a cwd or an open
	 * file keeps the mount, and destroy then returns EBUSY.
	 */
	(void) zfs_umount(zfsvfs->z_vfs, 0);
	zfsvfs->z_unmounted = B_TRUE;
	return (0);
}

int
zfs_set_version(zfsvfs_t *zfsvfs, uint64_t newvers)
{
	int error;
	objset_t *os = zfsvfs->z_os;
	dmu_tx_t *tx;

	if (newvers < ZPL_VERSION_INITIAL || newvers > ZPL_VERSION)
		return (SET_ERROR(EINVAL));

	if (newvers < zfsvfs->z_version)
		return (SET_ERROR(EINVAL));

	if (zfs_spa_version_map(newvers) >
	    spa_version(dmu_objset_spa(zfsvfs->z_os)))
		return (SET_ERROR(ENOTSUP));

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, MASTER_NODE_OBJ, B_FALSE, ZPL_VERSION_STR);
	if (newvers >= ZPL_VERSION_SA && !zfsvfs->z_use_sa) {
		dmu_tx_hold_zap(tx, MASTER_NODE_OBJ, B_TRUE,
		    ZFS_SA_ATTRS);
		dmu_tx_hold_zap(tx, DMU_NEW_OBJECT, FALSE, NULL);
	}
	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		return (error);
	}

	error = zap_update(os, MASTER_NODE_OBJ, ZPL_VERSION_STR,
	    8, 1, &newvers, tx);

	if (error) {
		dmu_tx_commit(tx);
		return (error);
	}

	if (newvers >= ZPL_VERSION_SA && !zfsvfs->z_use_sa) {
		uint64_t sa_obj;

		ASSERT3U(spa_version(dmu_objset_spa(zfsvfs->z_os)), >=,
		    SPA_VERSION_SA);
		sa_obj = zap_create(os, DMU_OT_SA_MASTER_NODE,
		    DMU_OT_NONE, 0, tx);

		error = zap_add(os, MASTER_NODE_OBJ,
		    ZFS_SA_ATTRS, 8, 1, &sa_obj, tx);
		if (error != 0) {
			dmu_tx_commit(tx);
			return (error);
		}

		VERIFY0(sa_set_sa_object(os, sa_obj));
		sa_register_update_callback(os, zfs_sa_upgrade);
	}

	spa_history_log_internal_ds(dmu_objset_ds(os), "upgrade", tx,
	    "from %ju to %ju", (uintmax_t)zfsvfs->z_version,
	    (uintmax_t)newvers);
	dmu_tx_commit(tx);

	zfsvfs->z_version = newvers;
	os->os_version = newvers;

	zfs_set_fuid_feature(zfsvfs);

	return (0);
}

int
zfs_set_default_quota(zfsvfs_t *zfsvfs, zfs_prop_t prop, uint64_t quota)
{
	int error;
	objset_t *os = zfsvfs->z_os;
	const char *propstr = zfs_prop_to_name(prop);
	dmu_tx_t *tx;

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, MASTER_NODE_OBJ, B_FALSE, propstr);
	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		return (error);
	}

	if (quota == 0) {
		error = zap_remove(os, MASTER_NODE_OBJ, propstr, tx);
		if (error == ENOENT)
			error = 0;
	} else {
		error = zap_update(os, MASTER_NODE_OBJ, propstr, 8, 1,
		    &quota, tx);
	}

	if (error)
		goto out;

	switch (prop) {
	case ZFS_PROP_DEFAULTUSERQUOTA:
		zfsvfs->z_defaultuserquota = quota;
		break;
	case ZFS_PROP_DEFAULTGROUPQUOTA:
		zfsvfs->z_defaultgroupquota = quota;
		break;
	case ZFS_PROP_DEFAULTPROJECTQUOTA:
		zfsvfs->z_defaultprojectquota = quota;
		break;
	case ZFS_PROP_DEFAULTUSEROBJQUOTA:
		zfsvfs->z_defaultuserobjquota = quota;
		break;
	case ZFS_PROP_DEFAULTGROUPOBJQUOTA:
		zfsvfs->z_defaultgroupobjquota = quota;
		break;
	case ZFS_PROP_DEFAULTPROJECTOBJQUOTA:
		zfsvfs->z_defaultprojectobjquota = quota;
		break;
	default:
		break;
	}

out:
	dmu_tx_commit(tx);
	return (error);
}

/*
 * Return true if the corresponding vfs's unmounted flag is set.
 * Otherwise return false.
 * If this function returns true we know VFS unmount has been initiated.
 */
boolean_t
zfs_get_vfs_flag_unmounted(objset_t *os)
{
	zfsvfs_t *zfvp;
	boolean_t unmounted = B_FALSE;

	ASSERT3U(dmu_objset_type(os), ==, DMU_OST_ZFS);

	mutex_enter(&os->os_user_ptr_lock);
	zfvp = dmu_objset_get_user(os);

	if (zfvp != NULL && zfvp->z_vfs != NULL &&
	    (zfvp->z_vfs->mnt_iflag & (IMNT_UNMOUNT | IMNT_GONE)) != 0)
		unmounted = B_TRUE;
	mutex_exit(&os->os_user_ptr_lock);

	return (unmounted);
}

#ifdef _KERNEL
/*
 * True when this thread's cwd is on mp, or on a snapshot mount covered
 * by mp.  *snapcover is set only for the covered-snapshot case.
 * zfs unmount -fa from inside .zfs/snapshot must fail (EBUSY) and leave
 * the directory usable.  MNT_FORCE would otherwise vflush the cwd vnode
 * (zfs_unmount_009_pos).  A cwd on mp itself is reported with
 * *snapcover false so MNT_FORCE can still detach it.
 */
static int
zfs_cwd_on_mount(struct mount *mp, boolean_t *snapcover)
{
	struct cwdinfo *cwdi;
	vnode_t *cdir;
	struct mount *cmp;
	int on = 0;

	if (snapcover != NULL)
		*snapcover = B_FALSE;
	if (mp == NULL || curlwp == NULL || curlwp->l_proc == NULL)
		return (0);
	cwdi = curlwp->l_proc->p_cwdi;
	if (cwdi == NULL)
		return (0);
	rw_enter(&cwdi->cwdi_lock, RW_READER);
	cdir = cwdi->cwdi_cdir;
	if (cdir != NULL)
		vref(cdir);
	rw_exit(&cwdi->cwdi_lock);
	if (cdir == NULL)
		return (0);
	cmp = cdir->v_mount;
	if (cmp == mp)
		on = 1;
	else if (cmp != NULL && cmp->mnt_vnodecovered != NULL &&
	    cmp->mnt_vnodecovered->v_mount == mp) {
		on = 1;
		if (snapcover != NULL)
			*snapcover = B_TRUE;
	}
	vrele(cdir);
	return (on);
}

void
zfsvfs_update_fromname(const char *oldname, const char *newname)
{
	mount_iterator_t *iter;
	struct mount *mp;
	char *tmpbuf;
	size_t oldlen;

	/*
	 * Rename does not remount when -u is set, but `zfs mount`
	 * matches mntfromname.  A no-op here makes zfs_rename_nounmount
	 * report the datasets as gone.  Same walk as FreeBSD.
	 *
	 * This runs from dsl_dir_rename_sync, in the txg sync thread.
	 * mountlist_iterator_next() vfs_busy()s every mount and waits
	 * out a suspend, including a non-ZFS one (an ffs snapshot via
	 * fss).  That would stop this txg.  trynext skips a mount it
	 * cannot busy; that fromname stays stale until the next rename.
	 */
	oldlen = strlen(oldname);
	tmpbuf = kmem_alloc(MAXPATHLEN, KM_SLEEP);
	mountlist_iterator_init(&iter);
	while ((mp = mountlist_iterator_trynext(iter)) != NULL) {
		char *fromname = mp->mnt_stat.f_mntfromname;

		if (strcmp(fromname, oldname) == 0) {
			(void) strlcpy(fromname, newname,
			    sizeof (mp->mnt_stat.f_mntfromname));
			continue;
		}
		if (strncmp(fromname, oldname, oldlen) == 0 &&
		    (fromname[oldlen] == '/' || fromname[oldlen] == '@')) {
			(void) snprintf(tmpbuf, MAXPATHLEN, "%s%s",
			    newname, fromname + oldlen);
			(void) strlcpy(fromname, tmpbuf,
			    sizeof (mp->mnt_stat.f_mntfromname));
		}
	}
	mountlist_iterator_destroy(iter);
	kmem_free(tmpbuf, MAXPATHLEN);
}
#endif

static int
zfs_netbsd_sync(vfs_t *vfsp, int waitfor, cred_t *cr)
{
	/*
	 * Same contract as Linux zpl_sync_fs / FreeBSD vfs_sync /
	 * native osnet: zfs_sync() already ignores MNT_LAZY (the
	 * periodic sched_sync).  MNT_WAIT is dounmount after
	 * vfs_suspend -- that must zil_commit.  A SHARED fstrans
	 * during zil_commit only delays umount, it does not
	 * deadlock.  Run zfs_sync on this LWP.  fstrans is per LWP,
	 * so handing the commit to another thread would leak the
	 * caller's SHARED hold and wedge fstcnt.
	 */
	(void) cr;
	return (zfs_sync(vfsp, waitfor));
}

static int
zfs_netbsd_vptofh(vnode_t *vp, fid_t *fidp, size_t *fh_size)
{
	znode_t *zp;
	zfsvfs_t *zfsvfs;
	zfid_short_t *zfid;
	uint64_t gen64 = 0, object;
	uint32_t gen;
	int size, i, error;

	if (zfsctl_is_node(vp))
		return (zfsctl_vptofh(vp, fidp, fh_size));

	zp = VTOZ(vp);
	if (zp == NULL || zp->z_zfsvfs == NULL)
		return (SET_ERROR(ESTALE));
	zfsvfs = zp->z_zfsvfs;
	if ((error = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (error);

	object = zp->z_id;
	error = sa_lookup(zp->z_sa_hdl, SA_ZPL_GEN(zfsvfs),
	    &gen64, sizeof (uint64_t));
	if (error != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}
	gen = (uint32_t)gen64;
	/* Non-zero so NFS does not treat this as a .zfs ctl node. */
	if (gen == 0)
		gen = 1;

	size = (zfsvfs->z_parent != zfsvfs) ? LONG_FID_LEN : SHORT_FID_LEN;
	if (*fh_size < (size_t)size) {
		*fh_size = size;
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(E2BIG));
	}
	*fh_size = size;

	zfid = (zfid_short_t *)fidp;
	zfid->zf_len = (uint16_t)size;
	for (i = 0; i < (int)sizeof (zfid->zf_object); i++)
		zfid->zf_object[i] = (uint8_t)(object >> (8 * i));
	for (i = 0; i < (int)sizeof (zfid->zf_gen); i++)
		zfid->zf_gen[i] = (uint8_t)(gen >> (8 * i));

	if (size == LONG_FID_LEN) {
		uint64_t objsetid = dmu_objset_id(zfsvfs->z_os);
		zfid_long_t *zlfid = (zfid_long_t *)fidp;

		for (i = 0; i < (int)sizeof (zlfid->zf_setid); i++)
			zlfid->zf_setid[i] = (uint8_t)(objsetid >> (8 * i));
		for (i = 0; i < (int)sizeof (zlfid->zf_setgen); i++)
			zlfid->zf_setgen[i] = 0;
	}

	zfs_exit(zfsvfs, FTAG);
	return (0);
}

/*
 * See vfsops.vfs_suspendctl.  Must not return EOPNOTSUPP: dounmount
 * propagates that to unmount(2).  Returning 0 skips the fstrans drain
 * (genfs_suspendctl).
 */
static int
zfs_netbsd_suspendctl(struct mount *mp, int cmd)
{
	(void) mp;
	switch (cmd) {
	case SUSPEND_SUSPEND:
	case SUSPEND_RESUME:
		return (0);
	default:
		return (SET_ERROR(EOPNOTSUPP));
	}
}

static int
zfs_netbsd_fhtovp(vfs_t *vfsp, fid_t *fidp, int flags, vnode_t **vpp)
{
	zfsvfs_t *zfsvfs = vfsp->vfs_data;
	znode_t *zp;
	uint64_t object = 0, fid_gen = 0, setgen = 0;
	uint64_t gen_mask, zp_gen = 0;
	int i, err;

	*vpp = NULL;
	if ((err = zfs_enter(zfsvfs, FTAG)) != 0)
		return (err);

	if (fidp->fid_len != SHORT_FID_LEN && fidp->fid_len != LONG_FID_LEN) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EINVAL));
	}

	if (zfsvfs->z_parent == zfsvfs && fidp->fid_len == LONG_FID_LEN) {
		zfid_long_t *zlfid = (zfid_long_t *)fidp;
		uint64_t objsetid = 0;

		for (i = 0; i < (int)sizeof (zlfid->zf_setid); i++)
			objsetid |= ((uint64_t)zlfid->zf_setid[i]) << (8 * i);
		for (i = 0; i < (int)sizeof (zlfid->zf_setgen); i++)
			setgen |= ((uint64_t)zlfid->zf_setgen[i]) << (8 * i);

		zfs_exit(zfsvfs, FTAG);
		err = zfsctl_lookup_objset(vfsp, objsetid, &zfsvfs);
		if (err != 0)
			return (SET_ERROR(ESTALE));
		err = zfs_enter(zfsvfs, FTAG);
		zfs_vfs_rele(zfsvfs);
		if (err != 0)
			return (err);
	}

	{
		zfid_short_t *zfid = (zfid_short_t *)fidp;

		for (i = 0; i < (int)sizeof (zfid->zf_object); i++)
			object |= ((uint64_t)zfid->zf_object[i]) << (8 * i);
		for (i = 0; i < (int)sizeof (zfid->zf_gen); i++)
			fid_gen |= ((uint64_t)zfid->zf_gen[i]) << (8 * i);
	}

	if (fidp->fid_len == LONG_FID_LEN && setgen != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EINVAL));
	}

	if (fid_gen == 0 &&
	    (object == ZFSCTL_INO_ROOT || object == ZFSCTL_INO_SNAPDIR)) {
		zfs_exit(zfsvfs, FTAG);
		if (object == ZFSCTL_INO_ROOT)
			err = zfsctl_root(zfsvfs, 0, vpp);
		else
			err = zfsctl_snapshot(zfsvfs, vpp);
		if (err != 0)
			return (err);
		/*
		 * LK_NONE is 0.  Do not promote it to LK_EXCLUSIVE  --
		 * that takes fstrans via VOP_LOCK and the caller
		 * never VOP_UNLOCK's.
		 */
		if ((flags & (LK_SHARED | LK_EXCLUSIVE)) != 0) {
			err = vn_lock(*vpp, flags);
			if (err != 0) {
				vrele(*vpp);
				*vpp = NULL;
			}
		}
		return (err);
	}

	gen_mask = ~((uint64_t)0) >>
	    (64 - 8 * (int)sizeof (((zfid_short_t *)0)->zf_gen));
	err = zfs_zget(zfsvfs, object, &zp);
	if (err != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(ESTALE));
	}
	err = sa_lookup(zp->z_sa_hdl, SA_ZPL_GEN(zfsvfs), &zp_gen,
	    sizeof (uint64_t));
	if (err != 0) {
		vrele(ZTOV(zp));
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(ESTALE));
	}
	zp_gen &= gen_mask;
	if (zp_gen == 0)
		zp_gen = 1;
	/*
	 * Match FreeBSD and Linux: generation is the check.  An
	 * open-unlinked file is still reachable by file handle
	 * (unlinked_fh_accessible).  z_unlinked is not ESTALE.
	 */
	if (zp_gen != fid_gen) {
		vrele(ZTOV(zp));
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(ESTALE));
	}

	*vpp = ZTOV(zp);
	zfs_exit(zfsvfs, FTAG);
	if ((flags & (LK_SHARED | LK_EXCLUSIVE)) != 0) {
		err = vn_lock(*vpp, flags);
		if (err != 0) {
			vrele(*vpp);
			*vpp = NULL;
		}
	}
	return (err);
}
