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
 * Copyright (c) 2012, 2015 by Delphix. All rights reserved.
 * Copyright 2016 Nexenta Systems, Inc. All rights reserved.
 */

/* Portions Copyright 2010 Robert Milkowski */
/* Portions Copyright 2013,2020 Jorgen Lundman */

#include <sys/types.h>
#include <sys/zfs_dir.h>
#include <sys/policy.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_dataset.h>
#include <sys/zap.h>
#include <sys/sa.h>
#include <sys/sa_impl.h>
#include <sys/zfs_ctldir.h>
#include <sys/dmu_objset.h>
#include <sys/zfs_quota.h>

#include "zfs_comutil.h"

#include <sys/zfs_vnops.h>
#include <sys/systeminfo.h>
#include <sys/zfs_mount.h>
#include <sys/dsl_dir.h>
#include <sys/dataset_kstats.h>
#include <sys/zfs_vfsops_os.h>

unsigned int zfs_vnop_skip_unlinked_drain = 0;

extern int getzfsvfs(const char *dsname, zfsvfs_t **zfvp);

void arc_os_init(void);
void arc_os_fini(void);

/*
 * AVL tree of hardlink entries, which we need to map for Finder. The va_linkid
 * needs to be unique for each hardlink target, as well as, return the znode
 * in vget(va_linkid). Unfortunately, the va_linkid is 32bit (lost in the
 * syscall translation to userland struct). We sort the AVL tree by
 * -> directory id
 *       -> z_id
 *              -> name
 *
 */
static int hardlinks_compare(const void *arg1, const void *arg2)
{
	const hardlinks_t *node1 = arg1;
	const hardlinks_t *node2 = arg2;
	int value;
	if (node1->hl_parent > node2->hl_parent)
		return (1);
	if (node1->hl_parent < node2->hl_parent)
		return (-1);
	if (node1->hl_fileid > node2->hl_fileid)
		return (1);
	if (node1->hl_fileid < node2->hl_fileid)
		return (-1);

	value = strncmp(node1->hl_name, node2->hl_name, PATH_MAX);
	if (value < 0)
		return (-1);
	if (value > 0)
		return (1);
	return (0);
}

/*
 * Lookup same information from linkid, to get at parentid, objid and name
 */
static int hardlinks_compare_linkid(const void *arg1, const void *arg2)
{
	const hardlinks_t *node1 = arg1;
	const hardlinks_t *node2 = arg2;
	if (node1->hl_linkid > node2->hl_linkid)
		return (1);
	if (node1->hl_linkid < node2->hl_linkid)
		return (-1);
	return (0);
}

extern int
zfs_obtain_xattr(znode_t *, const char *, mode_t, cred_t *, vnode_t **, int);


/*
 * We need to keep a count of active fs's.
 * This is necessary to prevent our kext
 * from being unloaded after a umount -f
 */
uint32_t	zfs_active_fs_count = 0;

extern void zfs_ioctl_init(void);
extern void zfs_ioctl_fini(void);

boolean_t
zfs_is_readonly(zfsvfs_t *zfsvfs)
{
	return (!!(vfs_isrdonly(zfsvfs->z_vfs)));
}

/*
 * The OS sync ignored by default, as ZFS handles internal periodic
 * syncs. (As per illumos) Unfortunately, we can not tell the difference
 * of when users run "sync" by hand. Sync is called on umount though.
 */
uint64_t zfs_vfs_sync_paranoia = 0;

int
zfs_vfs_sync(struct mount *vfsp, __unused int waitfor,
    __unused vfs_context_t context)
{
	/*
	 * Data integrity is job one. We don't want a compromised kernel
	 * writing to the storage pool, so we never sync during panic.
	 */
	if (spl_panicstr())
		return (0);

	/* Check if sysctl setting wants sync - and we are not unmounting */
	if (zfs_vfs_sync_paranoia == 0 &&
	    !vfs_isunmount(vfsp))
		return (0);

	if (vfsp != NULL) {
		/*
		 * Sync a specific filesystem.
		 */
		zfsvfs_t *zfsvfs = vfs_fsprivate(vfsp);
		dsl_pool_t *dp;

		ZFS_ENTER(zfsvfs);
		dp = dmu_objset_pool(zfsvfs->z_os);

		/*
		 * If the system is shutting down, then skip any
		 * filesystems which may exist on a suspended pool.
		 */
		if (spl_system_inshutdown() && spa_suspended(dp->dp_spa)) {
			ZFS_EXIT(zfsvfs);
			return (0);
		}

		if (zfsvfs->z_log != NULL)
			zil_commit(zfsvfs->z_log, 0);

		ZFS_EXIT(zfsvfs);

	} else {
		/*
		 * Sync all ZFS filesystems. This is what happens when you
		 * run sync(1M). Unlike other filesystems, ZFS honors the
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

	if (newval == B_TRUE) {
		zfsvfs->z_atime = B_TRUE;
		vfs_clearflags(zfsvfs->z_vfs, (uint64_t)MNT_NOATIME);
	} else {
		zfsvfs->z_atime = B_FALSE;
		vfs_setflags(zfsvfs->z_vfs, (uint64_t)MNT_NOATIME);
	}
}

static void
xattr_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	/*
	 * Apple does have MNT_NOUSERXATTR mount option, but unfortunately
	 * the VFS layer returns EACCESS if xattr access is attempted.
	 * Finder etc, will do so, even if filesystem capabilities is set
	 * without xattr, rendering the mount option useless. We no longer
	 * set it, and handle xattrs being disabled internally.
	 */

	if (newval == ZFS_XATTR_OFF) {
		zfsvfs->z_xattr = B_FALSE;
		// vfs_setflags(zfsvfs->z_vfs, (uint64_t)MNT_NOUSERXATTR);
	} else {
		zfsvfs->z_xattr = B_TRUE;
		// vfs_clearflags(zfsvfs->z_vfs, (uint64_t)MNT_NOUSERXATTR);

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
	// zfsvfs->z_vfs->mnt_stat.f_iosize = newval;
}

static void
readonly_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;
	if (newval == B_TRUE) {
		vfs_setflags(zfsvfs->z_vfs, (uint64_t)MNT_RDONLY);
	} else {
		vfs_clearflags(zfsvfs->z_vfs, (uint64_t)MNT_RDONLY);
	}
}

static void
devices_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;
	if (newval == B_FALSE) {
		vfs_setflags(zfsvfs->z_vfs, (uint64_t)MNT_NODEV);
	} else {
		vfs_clearflags(zfsvfs->z_vfs, (uint64_t)MNT_NODEV);
	}
}

static void
setuid_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;
	if (newval == B_FALSE) {
		vfs_setflags(zfsvfs->z_vfs, (uint64_t)MNT_NOSUID);
	} else {
		vfs_clearflags(zfsvfs->z_vfs, (uint64_t)MNT_NOSUID);
	}
}

static void
exec_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;
	if (newval == B_FALSE) {
		vfs_setflags(zfsvfs->z_vfs, (uint64_t)MNT_NOEXEC);
	} else {
		vfs_clearflags(zfsvfs->z_vfs, (uint64_t)MNT_NOEXEC);
	}
}

static void
snapdir_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;
	zfsvfs->z_show_ctldir = newval;
	cache_purgevfs(zfsvfs->z_vfs);
}

static void
vscan_changed_cb(void *arg, uint64_t newval)
{
	// zfsvfs_t *zfsvfs = arg;
	// zfsvfs->z_vscan = newval;
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

static void
finderbrowse_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;
	if (newval == B_FALSE) {
		vfs_setflags(zfsvfs->z_vfs, (uint64_t)MNT_DONTBROWSE);
	} else {
		vfs_clearflags(zfsvfs->z_vfs, (uint64_t)MNT_DONTBROWSE);
	}
}
static void
ignoreowner_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;
	if (newval == B_FALSE) {
		vfs_clearflags(zfsvfs->z_vfs, (uint64_t)MNT_IGNORE_OWNERSHIP);
	} else {
		vfs_setflags(zfsvfs->z_vfs, (uint64_t)MNT_IGNORE_OWNERSHIP);
	}
}

static void
mimic_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;
	struct vfsstatfs *vfsstatfs;
	vfsstatfs = vfs_statfs(zfsvfs->z_vfs);

	if (newval == 0) {
		strlcpy(vfsstatfs->f_fstypename, "zfs", MFSTYPENAMELEN);
	} else {
		strlcpy(vfsstatfs->f_fstypename, "hfs", MFSTYPENAMELEN);
	}
}

static int
zfs_register_callbacks(struct mount *vfsp)
{
	struct dsl_dataset *ds = NULL;

	objset_t *os = NULL;
	zfsvfs_t *zfsvfs = NULL;
	boolean_t readonly = B_FALSE;
	boolean_t do_readonly = B_FALSE;
	boolean_t setuid = B_FALSE;
	boolean_t do_setuid = B_FALSE;
	boolean_t exec = B_FALSE;
	boolean_t do_exec = B_FALSE;
	boolean_t devices = B_FALSE;
	boolean_t do_devices = B_FALSE;
	boolean_t xattr = B_FALSE;
	boolean_t do_xattr = B_FALSE;
	boolean_t atime = B_FALSE;
	boolean_t do_atime = B_FALSE;
	boolean_t finderbrowse = B_FALSE;
	boolean_t do_finderbrowse = B_FALSE;
	boolean_t ignoreowner = B_FALSE;
	boolean_t do_ignoreowner = B_FALSE;
	int error = 0;

	ASSERT(vfsp);
	zfsvfs = vfs_fsprivate(vfsp);
	ASSERT(zfsvfs);
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
#define	vfs_optionisset(X, Y, Z) (vfs_flags(X)&(Y))

	if (vfs_optionisset(vfsp, MNT_RDONLY, NULL) ||
	    !spa_writeable(dmu_objset_spa(os))) {
		readonly = B_TRUE;
		do_readonly = B_TRUE;
	}
	if (vfs_optionisset(vfsp, MNT_NODEV, NULL)) {
		devices = B_FALSE;
		do_devices = B_TRUE;
	}
	/* xnu SETUID, not IllumOS SUID */
	if (vfs_optionisset(vfsp, MNT_NOSUID, NULL)) {
		setuid = B_FALSE;
		do_setuid = B_TRUE;
	}
	if (vfs_optionisset(vfsp, MNT_NOEXEC, NULL)) {
		exec = B_FALSE;
		do_exec = B_TRUE;
	}
	if (vfs_optionisset(vfsp, MNT_NOUSERXATTR, NULL)) {
		xattr = B_FALSE;
		do_xattr = B_TRUE;
	}
	if (vfs_optionisset(vfsp, MNT_NOATIME, NULL)) {
		atime = B_FALSE;
		do_atime = B_TRUE;
	}
	if (vfs_optionisset(vfsp, MNT_DONTBROWSE, NULL)) {
		finderbrowse = B_FALSE;
		do_finderbrowse = B_TRUE;
	}
	if (vfs_optionisset(vfsp, MNT_IGNORE_OWNERSHIP, NULL)) {
		ignoreowner = B_TRUE;
		do_ignoreowner = B_TRUE;
	}

	/*
	 * nbmand is a special property.  It can only be changed at
	 * mount time.
	 *
	 * This is weird, but it is documented to only be changeable
	 * at mount time.
	 */

	/*
	 * Register property callbacks.
	 *
	 * It would probably be fine to just check for i/o error from
	 * the first prop_register(), but I guess I like to go
	 * overboard...
	 */
	ds = dmu_objset_ds(os);
	dsl_pool_config_enter(dmu_objset_pool(os), FTAG);
	error = dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_ATIME), atime_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_XATTR), xattr_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_RECORDSIZE), blksz_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_READONLY), readonly_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_DEVICES), devices_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_SETUID), setuid_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_EXEC), exec_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_SNAPDIR), snapdir_changed_cb, zfsvfs);
	// This appears to be PROP_PRIVATE, investigate if we want this
	// ZOL calls this ACLTYPE
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_ACLMODE), acl_mode_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_ACLINHERIT), acl_inherit_changed_cb,
	    zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_VSCAN), vscan_changed_cb, zfsvfs);

	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_MIMIC), mimic_changed_cb, zfsvfs);

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
	if (do_exec)
		exec_changed_cb(zfsvfs, exec);
	if (do_devices)
		devices_changed_cb(zfsvfs, devices);
	if (do_xattr)
		xattr_changed_cb(zfsvfs, xattr);
	if (do_atime)
		atime_changed_cb(zfsvfs, atime);

	if (do_finderbrowse)
		finderbrowse_changed_cb(zfsvfs, finderbrowse);
	if (do_ignoreowner)
		ignoreowner_changed_cb(zfsvfs, ignoreowner);

	return (0);

unregister:
	dsl_prop_unregister_all(ds, zfsvfs);
	return (error);
}

/*
 * Takes a dataset, a property, a value and that value's setpoint as
 * found in the ZAP. Checks if the property has been changed in the vfs.
 * If so, val and setpoint will be overwritten with updated content.
 * Otherwise, they are left unchanged.
 */
int
zfs_get_temporary_prop(dsl_dataset_t *ds, zfs_prop_t zfs_prop, uint64_t *val,
    char *setpoint)
{
	int error;
	zfsvfs_t *zfvp;
	mount_t *vfsp;
	objset_t *os;
	uint64_t tmp = *val;

	error = dmu_objset_from_ds(ds, &os);
	if (error != 0)
		return (error);

	if (dmu_objset_type(os) != DMU_OST_ZFS)
		return (EINVAL);

	mutex_enter(&os->os_user_ptr_lock);
	zfvp = dmu_objset_get_user(os);
	mutex_exit(&os->os_user_ptr_lock);
	if (zfvp == NULL)
		return (ESRCH);

	vfsp = zfvp->z_vfs;

	switch (zfs_prop) {
		case ZFS_PROP_ATIME:
//			if (vfsp->vfs_do_atime)
//				tmp = vfsp->vfs_atime;
			break;
		case ZFS_PROP_RELATIME:
//			if (vfsp->vfs_do_relatime)
//				tmp = vfsp->vfs_relatime;
			break;
		case ZFS_PROP_DEVICES:
//			if (vfsp->vfs_do_devices)
//				tmp = vfsp->vfs_devices;
			break;
		case ZFS_PROP_EXEC:
//			if (vfsp->vfs_do_exec)
//				tmp = vfsp->vfs_exec;
			break;
		case ZFS_PROP_SETUID:
//			if (vfsp->vfs_do_setuid)
//				tmp = vfsp->vfs_setuid;
			break;
		case ZFS_PROP_READONLY:
//			if (vfsp->vfs_do_readonly)
//				tmp = vfsp->vfs_readonly;
			break;
		case ZFS_PROP_XATTR:
//			if (vfsp->vfs_do_xattr)
//				tmp = vfsp->vfs_xattr;
			break;
		case ZFS_PROP_NBMAND:
//			if (vfsp->vfs_do_nbmand)
//				tmp = vfsp->vfs_nbmand;
			break;
		default:
			return (ENOENT);
	}

	if (tmp != *val) {
		(void) strlcpy(setpoint, "temporary", ZFS_MAX_DATASET_NAME_LEN);
		*val = tmp;
	}
	return (0);
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

	/* Volume status "all ok" */
	zfsvfs->z_notification_conditions = 0;
	zfsvfs->z_freespace_notify_warninglimit = 0;
	zfsvfs->z_freespace_notify_dangerlimit = 0;
	zfsvfs->z_freespace_notify_desiredlevel = 0;

	error = zfs_get_zplprop(os, ZFS_PROP_VERSION, &zfsvfs->z_version);
	if (error != 0)
		return (error);
	if (zfsvfs->z_version >
	    zfs_zpl_version_map(spa_version(dmu_objset_spa(os)))) {
		dprintf("Can't mount a version %lld file system "
		    "on a version %lld pool\n. Pool must be upgraded to mount "
		    "this file system.\n", (u_longlong_t)zfsvfs->z_version,
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

	error = zfs_get_zplprop(os, ZFS_PROP_ACLMODE, &val);
	if (error != 0)
		return (error);
	zfsvfs->z_acl_mode = (uint_t)val;

	// zfs_get_zplprop(os, ZFS_PROP_LASTUNMOUNT, &val);
	// zfsvfs->z_last_unmount_time = val;

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
		if ((error == 0) && (val == ZFS_XATTR_SA))
			zfsvfs->z_xattr_sa = B_TRUE;
	}

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
	ASSERT(zfsvfs->z_root != 0);

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

	return (0);
}

int
zfsvfs_create(const char *osname, boolean_t readonly, zfsvfs_t **zfvp)
{
	objset_t *os;
	zfsvfs_t *zfsvfs;
	int error;

	zfsvfs = kmem_zalloc(sizeof (zfsvfs_t), KM_SLEEP);

	/*
	 * We claim to always be readonly so we can open snapshots;
	 * other ZPL code will prevent us from writing to snapshots.
	 */
	error = dmu_objset_own(osname, DMU_OST_ZFS, B_TRUE, B_TRUE,
	    zfsvfs, &os);
	if (error != 0) {
		kmem_free(zfsvfs, sizeof (zfsvfs_t));
		return (error);
	}

	error = zfsvfs_create_impl(zfvp, zfsvfs, os);
	if (error != 0) {
		dmu_objset_disown(os, B_TRUE, zfsvfs);
	}
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

	zfsvfs->z_ctldir_startid = ZFSCTL_INO_SNAPDIRS;

	rrm_init(&zfsvfs->z_teardown_lock, B_FALSE);

	rw_init(&zfsvfs->z_teardown_inactive_lock, NULL, RW_DEFAULT, NULL);
	rw_init(&zfsvfs->z_fuid_lock, NULL, RW_DEFAULT, NULL);

	int size = MIN(1 << (highbit64(zfs_object_mutex_size) - 1),
	    ZFS_OBJ_MTX_MAX);
	zfsvfs->z_hold_size = size;
	zfsvfs->z_hold_trees = vmem_zalloc(sizeof (avl_tree_t) * size,
	    KM_SLEEP);
	zfsvfs->z_hold_locks = vmem_zalloc(sizeof (kmutex_t) * size, KM_SLEEP);
	for (int i = 0; i != size; i++) {
		avl_create(&zfsvfs->z_hold_trees[i], zfs_znode_hold_compare,
		    sizeof (znode_hold_t), offsetof(znode_hold_t, zh_node));
		mutex_init(&zfsvfs->z_hold_locks[i], NULL, MUTEX_DEFAULT, NULL);
	}

	rw_init(&zfsvfs->z_hardlinks_lock, NULL, RW_DEFAULT, NULL);
	avl_create(&zfsvfs->z_hardlinks, hardlinks_compare,
	    sizeof (hardlinks_t), offsetof(hardlinks_t, hl_node));
	avl_create(&zfsvfs->z_hardlinks_linkid, hardlinks_compare_linkid,
	    sizeof (hardlinks_t), offsetof(hardlinks_t, hl_node_linkid));
	zfsvfs->z_rdonly = 0;

	mutex_init(&zfsvfs->z_drain_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&zfsvfs->z_drain_cv, NULL, CV_DEFAULT, NULL);

	error = zfsvfs_init(zfsvfs, os);
	if (error != 0) {
		*zfvp = NULL;
		kmem_free(zfsvfs, sizeof (zfsvfs_t));
		return (error);
	}

	*zfvp = zfsvfs;
	return (0);
}

static int
zfsvfs_setup(zfsvfs_t *zfsvfs, boolean_t mounting)
{
	int error;
	boolean_t readonly = vfs_isrdonly(zfsvfs->z_vfs);

	error = zfs_register_callbacks(zfsvfs->z_vfs);
	if (error)
		return (error);

	zfsvfs->z_log = zil_open(zfsvfs->z_os, zfs_get_data);

	/*
	 * If we are not mounting (ie: online recv), then we don't
	 * have to worry about replaying the log as we blocked all
	 * operations out since we closed the ZIL.
	 */
	if (mounting) {

		/*
		 * During replay we remove the read only flag to
		 * allow replays to succeed.
		 */

		if (readonly != 0)
			readonly_changed_cb(zfsvfs, B_FALSE);
		else
			if (!zfs_vnop_skip_unlinked_drain)
				zfs_unlinked_drain(zfsvfs);

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
				zfsvfs->z_replay = B_TRUE;
				zil_replay(zfsvfs->z_os, zfsvfs,
				    zfs_replay_vector);
				zfsvfs->z_replay = B_FALSE;
			}
		}

		/* restore readonly bit */
		if (readonly != 0)
			readonly_changed_cb(zfsvfs, B_TRUE);
	}

	/*
	 * Set the objset user_ptr to track its zfsvfs.
	 */
	mutex_enter(&zfsvfs->z_os->os_user_ptr_lock);
	dmu_objset_set_user(zfsvfs->z_os, zfsvfs);
	mutex_exit(&zfsvfs->z_os->os_user_ptr_lock);

	return (0);
}

extern krwlock_t zfsvfs_lock; /* in zfs_znode.c */

void
zfsvfs_free(zfsvfs_t *zfsvfs)
{
	int i, size = zfsvfs->z_hold_size;

	dprintf("+zfsvfs_free\n");

	zfs_fuid_destroy(zfsvfs);

	cv_destroy(&zfsvfs->z_drain_cv);
	mutex_destroy(&zfsvfs->z_drain_lock);
	mutex_destroy(&zfsvfs->z_znodes_lock);
	mutex_destroy(&zfsvfs->z_lock);
	list_destroy(&zfsvfs->z_all_znodes);
	rrm_destroy(&zfsvfs->z_teardown_lock);
	rw_destroy(&zfsvfs->z_teardown_inactive_lock);
	rw_destroy(&zfsvfs->z_fuid_lock);

	for (i = 0; i != size; i++) {
		avl_destroy(&zfsvfs->z_hold_trees[i]);
		mutex_destroy(&zfsvfs->z_hold_locks[i]);
	}
	kmem_free(zfsvfs->z_hold_trees, sizeof (avl_tree_t) * size);
	kmem_free(zfsvfs->z_hold_locks, sizeof (kmutex_t) * size);

	dprintf("ZFS: Unloading hardlink AVLtree: %lu\n",
	    avl_numnodes(&zfsvfs->z_hardlinks));
	void *cookie = NULL;
	hardlinks_t *hardlink;
	rw_destroy(&zfsvfs->z_hardlinks_lock);
	while ((hardlink = avl_destroy_nodes(&zfsvfs->z_hardlinks_linkid,
	    &cookie))) {
	}
	cookie = NULL;
	while ((hardlink = avl_destroy_nodes(&zfsvfs->z_hardlinks, &cookie))) {
		kmem_free(hardlink, sizeof (*hardlink));
	}
	avl_destroy(&zfsvfs->z_hardlinks);
	avl_destroy(&zfsvfs->z_hardlinks_linkid);

	kmem_free(zfsvfs, sizeof (zfsvfs_t));
	dprintf("-zfsvfs_free\n");
}

static void
zfs_set_fuid_feature(zfsvfs_t *zfsvfs)
{
	zfsvfs->z_use_fuids = USE_FUIDS(zfsvfs->z_version, zfsvfs->z_os);
	if (zfsvfs->z_vfs) {
#if 0
		if (zfsvfs->z_use_fuids) {
			vfs_set_feature(zfsvfs->z_vfs, VFSFT_XVATTR);
			vfs_set_feature(zfsvfs->z_vfs, VFSFT_SYSATTR_VIEWS);
			vfs_set_feature(zfsvfs->z_vfs, VFSFT_ACEMASKONACCESS);
			vfs_set_feature(zfsvfs->z_vfs, VFSFT_ACLONCREATE);
			vfs_set_feature(zfsvfs->z_vfs, VFSFT_ACCESS_FILTER);
			vfs_set_feature(zfsvfs->z_vfs, VFSFT_REPARSE);
		} else {
			vfs_clear_feature(zfsvfs->z_vfs, VFSFT_XVATTR);
			vfs_clear_feature(zfsvfs->z_vfs, VFSFT_SYSATTR_VIEWS);
			vfs_clear_feature(zfsvfs->z_vfs, VFSFT_ACEMASKONACCESS);
			vfs_clear_feature(zfsvfs->z_vfs, VFSFT_ACLONCREATE);
			vfs_clear_feature(zfsvfs->z_vfs, VFSFT_ACCESS_FILTER);
			vfs_clear_feature(zfsvfs->z_vfs, VFSFT_REPARSE);
		}
#endif
	}
	zfsvfs->z_use_sa = USE_SA(zfsvfs->z_version, zfsvfs->z_os);
}


static int
zfs_domount(struct mount *vfsp, dev_t mount_dev, char *osname,
    vfs_context_t ctx)
{
	int error = 0;
	zfsvfs_t *zfsvfs;
	uint64_t mimic = 0;
	// struct timeval tv;

	ASSERT(vfsp);
	ASSERT(osname);

	error = zfsvfs_create(osname, B_FALSE, &zfsvfs);
	if (error)
		return (error);
	zfsvfs->z_vfs = vfsp;

	zfsvfs->z_rdev = mount_dev;

	/* HFS sets this prior to mounting */
	vfs_setflags(vfsp, (uint64_t)((unsigned int)MNT_DOVOLFS));
	/* Advisory locking should be handled at the VFS layer */
	vfs_setlocklocal(vfsp);

	/*
	 * Record the mount time (for Spotlight)
	 */

	vfs_setfsprivate(vfsp, zfsvfs);

	/*
	 * The fsid is 64 bits, composed of an 8-bit fs type, which
	 * separates our fsid from any other filesystem types, and a
	 * 56-bit objset unique ID.  The objset unique ID is unique to
	 * all objsets open on this system, provided by unique_create().
	 * The 8-bit fs type must be put in the low bits of fsid[1]
	 * because that's where other Solaris filesystems put it.
	 */

	error = dsl_prop_get_integer(osname, "com.apple.mimic", &mimic, NULL);
	if (zfsvfs->z_rdev) {
		struct vfsstatfs *vfsstatfs;
		vfsstatfs = vfs_statfs(vfsp);
		vfsstatfs->f_fsid.val[0] = zfsvfs->z_rdev;
		vfsstatfs->f_fsid.val[1] = vfs_typenum(vfsp);
	} else {
		// Otherwise, ask VFS to give us a random unique one.
		vfs_getnewfsid(vfsp);
		struct vfsstatfs *vfsstatfs;
		vfsstatfs = vfs_statfs(vfsp);
		zfsvfs->z_rdev = vfsstatfs->f_fsid.val[0];
	}

	/*
	 * If we are readonly (ie, waiting for rootmount) we need to reply
	 * honestly, so launchd runs fsck_zfs and mount_zfs
	 */
	if (mimic) {
		struct vfsstatfs *vfsstatfs;
		vfsstatfs = vfs_statfs(vfsp);
		strlcpy(vfsstatfs->f_fstypename, "ntfs", MFSTYPENAMELEN);
	}

	/*
	 * Set features for file system.
	 */
	zfs_set_fuid_feature(zfsvfs);

	if (dmu_objset_is_snapshot(zfsvfs->z_os)) {
		uint64_t pval;
		char fsname[ZFS_MAX_DATASET_NAME_LEN];
		zfsvfs_t *fs_zfsvfs;

		dmu_fsname(osname, fsname);
		error = getzfsvfs(fsname, &fs_zfsvfs);
		if (error == 0) {
			if (fs_zfsvfs->z_unmounted)
				error = SET_ERROR(EINVAL);
			vfs_unbusy(fs_zfsvfs->z_vfs);
		}
		if (error) {
			dprintf("file system '%s' is unmounted : error %d\n",
			    fsname,
			    error);
			goto out;
		}

		atime_changed_cb(zfsvfs, B_FALSE);
		readonly_changed_cb(zfsvfs, B_TRUE);
		if ((error = dsl_prop_get_integer(osname, "xattr", &pval,
		    NULL)))
			goto out;
		xattr_changed_cb(zfsvfs, pval);
		zfsvfs->z_issnap = B_TRUE;
		zfsvfs->z_os->os_sync = ZFS_SYNC_DISABLED;

		mutex_enter(&zfsvfs->z_os->os_user_ptr_lock);
		dmu_objset_set_user(zfsvfs->z_os, zfsvfs);
		mutex_exit(&zfsvfs->z_os->os_user_ptr_lock);

		zfsctl_mount_signal(osname, B_TRUE);

	} else {
		if ((error = zfsvfs_setup(zfsvfs, B_TRUE)))
			goto out;
	}

	vfs_setflags(vfsp, (uint64_t)((unsigned int)MNT_JOURNALED));

	if ((vfs_flags(vfsp) & MNT_ROOTFS) != 0) {
		/* Root FS */
		vfs_clearflags(vfsp,
		    (uint64_t)((unsigned int)MNT_UNKNOWNPERMISSIONS));
		vfs_clearflags(vfsp,
		    (uint64_t)((unsigned int)MNT_IGNORE_OWNERSHIP));
	}

#if 1 // Want .zfs or not
	if (!zfsvfs->z_issnap) {
		zfsctl_create(zfsvfs);
	}
#endif

out:
	if (error) {
		vfs_setfsprivate(vfsp, NULL);
		dmu_objset_disown(zfsvfs->z_os, B_TRUE, zfsvfs);
		zfsvfs_free(zfsvfs);
	} else {
		atomic_inc_32(&zfs_active_fs_count);
	}

	return (error);
}

void
zfs_unregister_callbacks(zfsvfs_t *zfsvfs)
{
	objset_t *os = zfsvfs->z_os;

	/*
	 * Unregister properties.
	 */
	if (!dmu_objset_is_snapshot(os)) {
		dsl_prop_unregister_all(dmu_objset_ds(os), zfsvfs);
	}
}

/*
 * zfs_vfs_mountroot
 * Given a device vnode created by vfs_mountroot bdevvp,
 * and with the root pool already imported, root mount the
 * dataset specified in the pool's bootfs property.
 *
 * Inputs:
 * mp: VFS mount struct
 * devvp: device vnode, currently only used to retrieve the
 *  dev_t for the fsid. Could vnode_get, vnode_ref, vnode_put,
 *  with matching get/rele/put in zfs_vfs_umount, but this is
 *  already done by XNU as well.
 * ctx: VFS context, unused.
 *
 * Return:
 * 0 on success, positive int on failure.
 */
int
zfs_vfs_mountroot(struct mount *mp, struct vnode *devvp, vfs_context_t ctx)
{
	/*
	 * static int zfsrootdone = 0;
	 */
	zfsvfs_t *zfsvfs = NULL;
	spa_t *spa = 0;
	char *zfs_bootfs = 0;
	dev_t dev = 0;
	int error = EINVAL;

	dprintf("ZFS: %s\n", __func__);
	ASSERT(mp);
	ASSERT(devvp);
	ASSERT(ctx);
	if (!mp || !devvp | !ctx) {
		cmn_err(CE_NOTE, "%s: missing one of mp %p devvp %p"
		    " or ctx %p", __func__, mp, devvp, ctx);
		return (EINVAL);
	}

	/* Look up bootfs variable from pool here */
	zfs_bootfs = kmem_alloc(MAXPATHLEN, KM_SLEEP);
	if (!zfs_bootfs) {
		cmn_err(CE_NOTE, "%s: bootfs alloc failed",
		    __func__);
		return (ENOMEM);
	}

	mutex_enter(&spa_namespace_lock);
	spa = spa_next(NULL);
	if (!spa) {
		mutex_exit(&spa_namespace_lock);
		cmn_err(CE_NOTE, "%s: no pool available",
		    __func__);
		goto out;
	}

	error = dsl_dsobj_to_dsname(spa_name(spa),
	    spa_bootfs(spa), zfs_bootfs);
	if (error != 0) {
		mutex_exit(&spa_namespace_lock);
		cmn_err(CE_NOTE, "%s: bootfs to name error %d",
		    __func__, error);
		goto out;
	}
	mutex_exit(&spa_namespace_lock);

	/*
	 * By setting the dev_t value in the mount vfsp,
	 * mount_zfs will be called with the /dev/diskN
	 * proxy, but we can leave the dataset name in
	 * the mountedfrom field
	 */
	dev = vnode_specrdev(devvp);

	dprintf("Setting readonly\n");

	if ((error = zfs_domount(mp, dev, zfs_bootfs, ctx)) != 0) {
		dprintf("zfs_domount: error %d", error);
		goto out;
	}

	zfsvfs = (zfsvfs_t *)vfs_fsprivate(mp);
	ASSERT(zfsvfs);
	if (!zfsvfs) {
		cmn_err(CE_NOTE, "missing zfsvfs");
		goto out;
	}

	/* Set this mount to read-only */
	zfsvfs->z_rdonly = 1;

	/*
	 * Due to XNU mount flags, readonly gets set off for a short
	 * while, which means mimic will kick in if enabled. But we need
	 * to reply with true "zfs" until root has been remounted RW, so
	 * that launchd tries to run mount_zfs instead of mount_hfs
	 */
	mimic_changed_cb(zfsvfs, B_FALSE);

	/*
	 * Leave rootvp held.  The root file system is never unmounted.
	 *
	 * XXX APPLE
	 * xnu will in fact call vfs_unmount on the root filesystem
	 * during shutdown/reboot.
	 */

out:

	if (zfs_bootfs) {
		kmem_free(zfs_bootfs, MAXPATHLEN);
	}
	return (error);

}

/*ARGSUSED*/
int
zfs_vfs_mount(struct mount *vfsp, vnode_t *mvp /* devvp */,
    user_addr_t data, vfs_context_t context)
{
	int		error = 0;
	cred_t		*cr = NULL;//(cred_t *)vfs_context_ucred(context);
	char		*osname = NULL;
	char		*options = NULL;
	uint64_t	flags = vfs_flags(vfsp);
	int		canwrite;
	int		rdonly = 0;
	int		mflag = 0;

	struct zfs_mount_args *mnt_args = (struct zfs_mount_args *)data;
	size_t		osnamelen = 0;
	uint32_t	cmdflags = 0;

dprintf("%s\n", __func__);
	cmdflags = (uint32_t) vfs_flags(vfsp) & MNT_CMDFLAGS;
	rdonly = vfs_isrdonly(vfsp);
dprintf("%s cmdflags %u rdonly %d\n", __func__, cmdflags, rdonly);

	/*
	* Get the objset name (the "special" mount argument).
	*/
	if (data) {

		// Allocate string area
		osname = kmem_alloc(MAXPATHLEN, KM_SLEEP);

		strlcpy(osname, mnt_args->fspec, MAXPATHLEN);

	}

	if (mnt_args->struct_size == sizeof(*mnt_args)) {

		mflag = mnt_args->mflag;

		if (mnt_args->optlen) {
			options = kmem_alloc(mnt_args->optlen, KM_SLEEP);
			strlcpy(options, mnt_args->optptr, mnt_args->optlen);
		}
			//dprintf("vfs_mount: fspec '%s' : mflag %04llx : optptr %p : optlen %d :"
		dprintf("%s: fspec '%s' : mflag %04x : optptr %p : optlen %d :"
		    " options %s\n", __func__,
			osname,
			mnt_args->mflag,
			mnt_args->optptr,
			mnt_args->optlen,
			options);
	}

	if (mflag & MS_RDONLY) {
		dprintf("%s: adding MNT_RDONLY\n", __func__);
		cmdflags |= MNT_RDONLY;
	}

	if (mflag & MS_OVERLAY) {
		dprintf("%s: adding MNT_UNION\n", __func__);
		cmdflags |= MNT_UNION;
	}

	if (mflag & MS_FORCE) {
		dprintf("%s: adding MNT_FORCE\n", __func__);
		cmdflags |= MNT_FORCE;
	}

	if (mflag & MS_REMOUNT) {
		dprintf("%s: adding MNT_UPDATE on MS_REMOUNT\n", __func__);
		cmdflags |= MNT_UPDATE;
	}

	vfs_setflags(vfsp, (uint64_t)cmdflags);

	/*
	 * When doing a remount, we simply refresh our temporary properties
	 * according to those options set in the current VFS options.
	 */
	if (cmdflags & MNT_UPDATE) {

		error = 0;
		// Used after fsck
		if (cmdflags & MNT_RELOAD) {
			goto out;
		}

		/* refresh mount options */
		zfsvfs_t *zfsvfs = vfs_fsprivate(vfsp);

		if (zfsvfs != NULL) {
			if (zfsvfs->z_rdonly == 0 &&
			    (cmdflags & MNT_RDONLY ||
			    vfs_isrdonly(vfsp))) {
				/* downgrade */
				dprintf("%s: downgrade requested\n", __func__);
				zfsvfs->z_rdonly = 1;
				readonly_changed_cb(zfsvfs, B_TRUE);
				zfs_unregister_callbacks(zfsvfs);
				error = zfs_register_callbacks(vfsp);
				if (error) {
					dprintf("%s: remount returned %d",
					    __func__, error);
				}
			}

			if (vfs_iswriteupgrade(vfsp)) {
				/* upgrade */
				dprintf("%s: upgrade requested\n", __func__);
				zfsvfs->z_rdonly = 0;
				readonly_changed_cb(zfsvfs, B_FALSE);
				zfs_unregister_callbacks(zfsvfs);
				error = zfs_register_callbacks(vfsp);
				if (error) {
					dprintf("%s: remount returned %d",
					    __func__, error);
				}
			}
		}
		goto out;
	}

	if (vfs_fsprivate(vfsp) != NULL) {
		dprintf("already mounted\n");
		error = 0;
		goto out;
	}

	error = zfs_domount(vfsp, 0, osname, context);
	if (error) {
		dprintf("%s: zfs_domount returned %d\n",
		    __func__, error);
		goto out;
	}

out:

	if (error == 0) {

		/* Indicate to VFS that we support ACLs. */
		vfs_setextendedsecurity(vfsp);

	}

	if (error)
		dprintf("zfs_vfs_mount: error %d\n", error);

	if (osname)
		kmem_free(osname, MAXPATHLEN);

	if (options)
		kmem_free(options, mnt_args->optlen);

	return (error);
}

int
zfs_vnode_lock(vnode_t *vp, int flags)
{
	int error;

	ASSERT(vp != NULL);

	error = vn_lock(vp, flags);
	return (error);
}

/*
 * The ARC has requested that the filesystem drop entries from the dentry
 * and inode caches.  This can occur when the ARC needs to free meta data
 * blocks but can't because they are all pinned by entries in these caches.
 */

/* Get vnode for the root object of this mount */
int
zfs_vfs_root(struct mount *mp, vnode_t **vpp, __unused vfs_context_t context)
{
	zfsvfs_t *zfsvfs = vfs_fsprivate(mp);
	znode_t *rootzp = NULL;
	int error;

	if (!zfsvfs) {
		struct vfsstatfs *stat = 0;
		if (mp) stat = vfs_statfs(mp);
		if (stat)
			dprintf("%s mp on %s from %s\n", __func__,
			    stat->f_mntonname, stat->f_mntfromname);
		dprintf("%s no zfsvfs yet for mp\n", __func__);
		return (EINVAL);
	}

	ZFS_ENTER(zfsvfs);

	error = zfs_zget(zfsvfs, zfsvfs->z_root, &rootzp);
	if (error == 0)
		*vpp = ZTOV(rootzp);
	else
		*vpp = NULL;

	ZFS_EXIT(zfsvfs);

	if (error == 0 && *vpp != NULL)
		if (vnode_vtype(*vpp) != VDIR) {
			panic("%s: not a directory\n", __func__);
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
	/*
	 * We have experienced deadlocks with dmu_recv_end happening between
	 * suspend_fs() and resume_fs(). Clearly something is not quite ready
	 * so we will wait for pools to be synced first.
	 * This is considered a temporary solution until we can work out
	 * the full issue.
	 */

	zfs_unlinked_drain_stop_wait(zfsvfs);

	/*
	 * If someone has not already unmounted this file system,
	 * drain the iput_taskq to ensure all active references to the
	 * zfs_sb_t have been handled only then can it be safely destroyed.
	 */
	if (zfsvfs->z_os) {
		/*
		 * If we're unmounting we have to wait for the list to
		 * drain completely.
		 *
		 * If we're not unmounting there's no guarantee the list
		 * will drain completely, but iputs run from the taskq
		 * may add the parents of dir-based xattrs to the taskq
		 * so we want to wait for these.
		 *
		 * We can safely read z_nr_znodes without locking because the
		 * VFS has already blocked operations which add to the
		 * z_all_znodes list and thus increment z_nr_znodes.
		 */
		int round = 0;
		while (!list_empty(&zfsvfs->z_all_znodes)) {
			taskq_wait_outstanding(dsl_pool_zrele_taskq(
			    dmu_objset_pool(zfsvfs->z_os)), 0);
			if (++round > 1 && !unmounting)
				break;
			break; /* Only loop once - osx can get stuck */
		}
	}

	rrm_enter(&zfsvfs->z_teardown_lock, RW_WRITER, FTAG);

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

	rw_enter(&zfsvfs->z_teardown_inactive_lock, RW_WRITER);

	/*
	 * If we are not unmounting (ie: online recv) and someone already
	 * unmounted this file system while we were doing the switcheroo,
	 * or a reopen of z_os failed then just bail out now.
	 */
	if (!unmounting && (zfsvfs->z_unmounted || zfsvfs->z_os == NULL)) {
		rw_exit(&zfsvfs->z_teardown_inactive_lock);
		rrm_exit(&zfsvfs->z_teardown_lock, FTAG);
		return (SET_ERROR(EIO));
	}
	/*
	 * At this point there are no VFS ops active, and any new VFS ops
	 * will fail with EIO since we have z_teardown_lock for writer (only
	 * relevant for forced unmount).
	 *
	 * Release all holds on dbufs. We also grab an extra reference to all
	 * the remaining inodes so that the kernel does not attempt to free
	 * any inodes of a suspended fs. This can cause deadlocks since the
	 * zfs_resume_fs() process may involve starting threads, which might
	 * attempt to free unreferenced inodes to free up memory for the new
	 * thread.
	 */
	if (!unmounting) {
		mutex_enter(&zfsvfs->z_znodes_lock);
		for (zp = list_head(&zfsvfs->z_all_znodes); zp != NULL;
		    zp = list_next(&zfsvfs->z_all_znodes, zp)) {
			if (zp->z_sa_hdl)
				zfs_znode_dmu_fini(zp);
			if (VN_HOLD(ZTOV(zp)) == 0) {
				vnode_ref(ZTOV(zp));
				zp->z_suspended = B_TRUE;
				VN_RELE(ZTOV(zp));
			}
		}
		mutex_exit(&zfsvfs->z_znodes_lock);
	}

	/*
	 * If we are unmounting, set the unmounted flag and let new VFS ops
	 * unblock.  zfs_inactive will have the unmounted behavior, and all
	 * other VFS ops will fail with EIO.
	 */
	if (unmounting) {
		zfsvfs->z_unmounted = B_TRUE;
		rw_exit(&zfsvfs->z_teardown_inactive_lock);
		rrm_exit(&zfsvfs->z_teardown_lock, FTAG);
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
	 * Evict cached data
	 */
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
	if (!zfs_is_readonly(zfsvfs) && os_dirty) {
		txg_wait_synced(dmu_objset_pool(zfsvfs->z_os), 0);
	}
	dmu_objset_evict_dbufs(zfsvfs->z_os);
	dsl_dir_t *dd = os->os_dsl_dataset->ds_dir;
	dsl_dir_cancel_waiters(dd);

	return (0);
}

int
zfs_vfs_unmount(struct mount *mp, int mntflags, vfs_context_t context)
{
	zfsvfs_t *zfsvfs = vfs_fsprivate(mp);
	objset_t *os;
	char osname[MAXNAMELEN];
	int ret;
	/* cred_t *cr = (cred_t *)vfs_context_ucred(context); */
	int destroyed_zfsctl = 0;

	dprintf("%s\n", __func__);

	zfs_unlinked_drain_stop_wait(zfsvfs);

	/* Save osname for later */
	dmu_objset_name(zfsvfs->z_os, osname);

	/*
	 * We might skip the sync called in the unmount path, since
	 * zfs_vfs_sync() is generally ignoring xnu's calls, and alas,
	 * mount_isforce() is set AFTER that sync call, so we can not
	 * detect unmount is inflight. But why not just sync now, it
	 * is safe. Optionally, sync if (mount_isforce());
	 */
	spa_sync_allpools();

	/*
	 * We purge the parent filesystem's vfsp as the parent filesystem
	 * and all of its snapshots have their vnode's v_vfsp set to the
	 * parent's filesystem's vfsp.  Note, 'z_parent' is self
	 * referential for non-snapshots.
	 */
	cache_purgevfs(zfsvfs->z_parent->z_vfs);

	/*
	 * Unmount any snapshots mounted under .zfs before unmounting the
	 * dataset itself.
	 *
	 * Unfortunately, XNU will check for mounts in preflight, and
	 * simply not call us at all if snapshots are mounted.
	 * We expect userland to unmount snapshots now.
	 */

	ret = vflush(mp, NULLVP, SKIPSYSTEM);

	if (mntflags & MNT_FORCE) {
		/*
		 * Mark file system as unmounted before calling
		 * vflush(FORCECLOSE). This way we ensure no future vnops
		 * will be called and risk operating on DOOMED vnodes.
		 */
		rrm_enter(&zfsvfs->z_teardown_lock, RW_WRITER, FTAG);
		zfsvfs->z_unmounted = B_TRUE;
		rrm_exit(&zfsvfs->z_teardown_lock, FTAG);
	}

	/*
	 * We must release ctldir before vflush on osx.
	 */
	if (zfsvfs->z_ctldir != NULL) {
		destroyed_zfsctl = 1;
		zfsctl_destroy(zfsvfs);
	}

	/*
	 * Flush all the files.
	 */
	ret = vflush(mp, NULLVP,
	    (mntflags & MNT_FORCE) ? FORCECLOSE|SKIPSYSTEM : SKIPSYSTEM);

	if ((ret != 0) && !(mntflags & MNT_FORCE)) {
		if (destroyed_zfsctl)
			zfsctl_create(zfsvfs);
		return (ret);
	}

	/* If we are ourselves a snapshot */
	if (dmu_objset_is_snapshot(zfsvfs->z_os)) {
		/* Wake up anyone waiting for unmount */
		zfsctl_mount_signal(osname, B_FALSE);
	}


	/*
	 * Last chance to dump unreferenced system files.
	 */
	(void) vflush(mp, NULLVP, FORCECLOSE);

	VERIFY(zfsvfs_teardown(zfsvfs, B_TRUE) == 0);
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

	zfs_freevfs(zfsvfs->z_vfs);

	return (0);
}

static int
zfs_vget_internal(zfsvfs_t *zfsvfs, ino64_t ino, vnode_t **vpp)
{
	znode_t		*zp;
	int 		err;

	dprintf("vget get %llu\n", ino);

	/*
	 * Check to see if we expect to find this in the hardlink avl tree of
	 * hashes. Use the MSB set high as indicator.
	 */
	hardlinks_t *findnode = NULL;
	if ((1ULL<<31) & ino) {
		hardlinks_t *searchnode;
		avl_index_t loc;

		searchnode = kmem_alloc(sizeof (hardlinks_t), KM_SLEEP);

		dprintf("ZFS: vget looking for (%llx,%llu)\n", ino, ino);

		searchnode->hl_linkid = ino;

		rw_enter(&zfsvfs->z_hardlinks_lock, RW_READER);
		findnode = avl_find(&zfsvfs->z_hardlinks_linkid, searchnode,
		    &loc);
		rw_exit(&zfsvfs->z_hardlinks_lock);

		kmem_free(searchnode, sizeof (hardlinks_t));

		if (findnode) {
			dprintf("ZFS: vget found (%llu, %llu, %u): '%s'\n",
			    findnode->hl_parent,
			    findnode->hl_fileid, findnode->hl_linkid,
			    findnode->hl_name);
			// Lookup the actual zp instead
			ino = findnode->hl_fileid;
		} // findnode
	} // MSB set


	/* We can not be locked during zget. */
	if (!ino) {
		dprintf("%s: setting ino from %lld to 2\n", __func__, ino);
		ino = 2;
	}

	err = zfs_zget(zfsvfs, ino, &zp);

	if (err) {
		dprintf("zget failed %d\n", err);
		return (err);
	}

	/* Don't expose EA objects! */
	if (zp->z_pflags & ZFS_XATTR) {
		err = ENOENT;
		goto out;
	}
	if (zp->z_unlinked) {
		err = EINVAL;
		goto out;
	}

	*vpp = ZTOV(zp);

	err = zfs_vnode_lock(*vpp, 0 /* flags */);

	/*
	 * Spotlight requires that vap->va_name() is set when returning
	 * from vfs_vget, so that vfs_getrealpath() can succeed in returning
	 * a path to mds.
	 */
	char *name = kmem_alloc(MAXPATHLEN + 2, KM_SLEEP);

	/* Root can't lookup in ZAP */
	if (zp->z_id == zfsvfs->z_root) {

		dmu_objset_name(zfsvfs->z_os, name);
		dprintf("vget: set root '%s'\n", name);
		// vnode_update_identity(*vpp, NULL, name,
		//    strlen(name), 0, VNODE_UPDATE_NAME);

	} else {
		uint64_t parent;

		// if its a hardlink cache
		if (findnode) {

			dprintf("vget: updating vnode to '%s' parent %llu\n",
			    findnode->hl_name, findnode->hl_parent);

			// vnode_update_identity(*vpp,
			//    NULL, findnode->hl_name,
			//    strlen(findnode->hl_name), 0,
			//    VNODE_UPDATE_NAME|VNODE_UPDATE_PARENT);
			mutex_enter(&zp->z_lock);
			strlcpy(zp->z_name_cache, findnode->hl_name, PATH_MAX);
			// zp->z_finder_parentid = findnode->hl_parent;
			mutex_exit(&zp->z_lock);


		// If we already have the name, cached in zfs_vnop_lookup
		} else if (zp->z_name_cache[0]) {
			dprintf("vget: cached name '%s'\n", zp->z_name_cache);
			// vnode_update_identity(*vpp, NULL, zp->z_name_cache,
			//    strlen(zp->z_name_cache), 0,
			//    VNODE_UPDATE_NAME);

			/* If needed, if findnode is set, update the parentid */

		} else {

			/* Lookup name from ID, grab parent */
			VERIFY(sa_lookup(zp->z_sa_hdl, SA_ZPL_PARENT(zfsvfs),
			    &parent, sizeof (parent)) == 0);

			if (zap_value_search(zfsvfs->z_os, parent, zp->z_id,
			    ZFS_DIRENT_OBJ(-1ULL), name) == 0) {

				dprintf("vget: set name '%s'\n", name);
				// vnode_update_identity(*vpp, NULL, name,
				//    strlen(name), 0,
				//    VNODE_UPDATE_NAME);
			} else {
				dprintf("vget: unable to get name for %llu\n",
				    zp->z_id);
			} // !zap_search
		}
	} // rootid

	kmem_free(name, MAXPATHLEN + 2);

out:

	if (err != 0) {
		VN_RELE(ZTOV(zp));
		*vpp = NULL;
	}

	dprintf("vget return %d\n", err);
	return (err);
}

/*
 * Get a vnode from a file id (ignoring the generation)
 *
 * Use by NFS Server (readdirplus) and VFS (build_path)
 */
int
zfs_vfs_vget(struct mount *mp, ino64_t ino, vnode_t **vpp,
    __unused vfs_context_t context)
{
	zfsvfs_t *zfsvfs = vfs_fsprivate(mp);
	int error;

	dprintf("%s: %llu\n", __func__, ino);

	ZFS_ENTER(zfsvfs);

	/* We also need to handle (.zfs) and (.zfs/snapshot). */
	if ((ino == ZFSCTL_INO_ROOT) && (zfsvfs->z_ctldir != NULL)) {
		if (VN_HOLD(zfsvfs->z_ctldir) == 0) {
			*vpp = zfsvfs->z_ctldir;
			error = 0;
		} else {
			error = ENOENT;
		}
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	/*
	 * This one is trickier, we have no reference to it, but it is
	 * in the all list. A little expensive to search list, but at
	 * least "snapshot" is infrequently accessed
	 * We also need to check if it is a ".zfs/snapshot/$name" entry -
	 * luckily we keep the "lowest" ID seen, so we only need to check
	 * when it is in the range.
	 */
	if (zfsvfs->z_ctldir != NULL) {

		/*
		 * Either it is the snapdir itself, or one of the snapshot
		 * directories inside it
		 */
		if ((ino == ZFSCTL_INO_SNAPDIR) ||
		    ((ino >= zfsvfs->z_ctldir_startid) &&
		    (ino <= ZFSCTL_INO_SNAPDIRS))) {
			znode_t *zp;

			mutex_enter(&zfsvfs->z_znodes_lock);
			for (zp = list_head(&zfsvfs->z_all_znodes); zp;
			    zp = list_next(&zfsvfs->z_all_znodes, zp)) {
				if (zp->z_id == ino)
					break;
				if (zp->z_id == ZFSCTL_INO_SHARES - ino)
					break;
			}
			mutex_exit(&zfsvfs->z_znodes_lock);

			error = ENOENT;
			if (zp != NULL) {
				if (VN_HOLD(ZTOV(zp)) == 0) {
					*vpp = ZTOV(zp);
					error = 0;
				}
			}

			ZFS_EXIT(zfsvfs);
			return (error);
		}
	}

	error = zfs_vget_internal(zfsvfs, ino, vpp);

	ZFS_EXIT(zfsvfs);
	return (error);
}

/*
 * NFS Server File Handle File ID
 */
typedef struct zfs_zfid {
	uint8_t   zf_object[8];		/* obj[i] = obj >> (8 * i) */
	uint8_t   zf_gen[8];		/* gen[i] = gen >> (8 * i) */
} zfs_zfid_t;

/*
 * File handle to vnode pointer
 */
int
zfs_vfs_fhtovp(struct mount *mp, int fhlen, unsigned char *fhp,
    vnode_t **vpp, __unused vfs_context_t context)
{
	dprintf("%s\n", __func__);
	zfsvfs_t *zfsvfs = vfs_fsprivate(mp);
	zfs_zfid_t	*zfid = (zfs_zfid_t *)fhp;
	znode_t		*zp;
	uint64_t	obj_num = 0;
	uint64_t	fid_gen = 0;
	uint64_t	zp_gen;
	int 		i;
	int		error;

	*vpp = NULL;

	ZFS_ENTER(zfsvfs);

	if (fhlen < sizeof (zfs_zfid_t)) {
		error = EINVAL;
		goto out;
	}

	/*
	 * Grab the object and gen numbers in an endian neutral manner
	 */
	for (i = 0; i < sizeof (zfid->zf_object); i++)
		obj_num |= ((uint64_t)zfid->zf_object[i]) << (8 * i);

	for (i = 0; i < sizeof (zfid->zf_gen); i++)
		fid_gen |= ((uint64_t)zfid->zf_gen[i]) << (8 * i);

	if ((error = zfs_zget(zfsvfs, obj_num, &zp))) {
		goto out;
	}

	zp_gen = zp->z_gen;
	if (zp_gen == 0)
		zp_gen = 1;

	if (zp->z_unlinked || zp_gen != fid_gen) {
		vnode_put(ZTOV(zp));
		error = EINVAL;
		goto out;
	}
	*vpp = ZTOV(zp);
out:
	ZFS_EXIT(zfsvfs);
	return (error);
}

/*
 * Vnode pointer to File handle
 *
 * XXX Do we want to check the DSL sharenfs property?
 */
int
zfs_vfs_vptofh(vnode_t *vp, int *fhlenp, unsigned char *fhp,
    __unused vfs_context_t context)
{
	dprintf("%s\n", __func__);
	zfsvfs_t	*zfsvfs = vfs_fsprivate(vnode_mount(vp));
	zfs_zfid_t	*zfid = (zfs_zfid_t *)fhp;
	znode_t		*zp = VTOZ(vp);
	uint64_t	obj_num;
	uint64_t	zp_gen;
	int		i;

	if (*fhlenp < sizeof (zfs_zfid_t)) {
		return (EOVERFLOW);
	}

	ZFS_ENTER(zfsvfs);

	obj_num = zp->z_id;
	zp_gen = zp->z_gen;
	if (zp_gen == 0)
		zp_gen = 1;

	/*
	 * Store the object and gen numbers in an endian neutral manner
	 */
	for (i = 0; i < sizeof (zfid->zf_object); i++)
		zfid->zf_object[i] = (uint8_t)(obj_num >> (8 * i));

	for (i = 0; i < sizeof (zfid->zf_gen); i++)
		zfid->zf_gen[i] = (uint8_t)(zp_gen >> (8 * i));

	*fhlenp = sizeof (zfs_zfid_t);

	ZFS_EXIT(zfsvfs);
	return (0);
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

	if ((error = zfsvfs_teardown(zfsvfs, B_FALSE)) != 0)
		return (error);

	return (0);
}

/*
 * Reopen zfsvfs_t::z_os and release VOPs.
 */
int
zfs_resume_fs(zfsvfs_t *zfsvfs, dsl_dataset_t *ds)
{
	int err, err2;
	znode_t *zp;

	ASSERT(RRM_WRITE_HELD(&zfsvfs->z_teardown_lock));
	ASSERT(RW_WRITE_HELD(&zfsvfs->z_teardown_inactive_lock));

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
	VERIFY(zfsvfs_setup(zfsvfs, B_FALSE) == 0);

	zfs_set_fuid_feature(zfsvfs);

	/*
	 * Attempt to re-establish all the active inodes with their
	 * dbufs.  If a zfs_rezget() fails, then we unhash the inode
	 * and mark it stale.  This prevents a collision if a new
	 * inode/object is created which must use the same inode
	 * number.  The stale inode will be be released when the
	 * VFS prunes the dentry holding the remaining references
	 * on the stale inode.
	 */
	mutex_enter(&zfsvfs->z_znodes_lock);
	for (zp = list_head(&zfsvfs->z_all_znodes); zp;
	    zp = list_next(&zfsvfs->z_all_znodes, zp)) {
		err2 = zfs_rezget(zp);
		if (err2) {
			zp->z_is_stale = B_TRUE;
		}

		/* see comment in zfs_suspend_fs() */
		if (zp->z_suspended) {
			if (vnode_getwithref(ZTOV(zp)) == 0) {
				vnode_rele(ZTOV(zp));
				zfs_zrele_async(zp);
				zp->z_suspended = B_FALSE;
			}
		}
	}
	mutex_exit(&zfsvfs->z_znodes_lock);

	if (!vfs_isrdonly(zfsvfs->z_vfs) && !zfsvfs->z_unmounted) {
		/*
		 * zfs_suspend_fs() could have interrupted freeing
		 * of dnodes. We need to restart this freeing so
		 * that we don't "leak" the space.
		 */
		zfs_unlinked_drain(zfsvfs);
	}

	cache_purgevfs(zfsvfs->z_parent->z_vfs);

bail:
	/* release the VFS ops */
	rw_exit(&zfsvfs->z_teardown_inactive_lock);
	rrm_exit(&zfsvfs->z_teardown_lock, FTAG);

	if (err) {
		/*
		 * Since we couldn't setup the sa framework, try to force
		 * unmount this file system.
		 */
		if (zfsvfs->z_os)
			zfs_vfs_unmount(zfsvfs->z_vfs, 0, NULL);
	}
	return (err);
}


void
zfs_freevfs(struct mount *vfsp)
{
	zfsvfs_t *zfsvfs = vfs_fsprivate(vfsp);

	dprintf("+freevfs\n");

	vfs_setfsprivate(vfsp, NULL);

	zfsvfs_free(zfsvfs);

	atomic_dec_32(&zfs_active_fs_count);
	dprintf("-freevfs\n");
}

struct fromname_struct {
	char *oldname;
	char *newname;
};
typedef struct fromname_struct fromname_t;

void
zfsvfs_update_fromname(const char *oldname, const char *newname)
{
	fromname_t frna;

	// find oldname's vfsp
	// vfs_mountedfrom(vfsp, newname);
	frna.oldname = oldname;
	frna.newname = newname;
}

void
zfs_init(void)
{

	dprintf("ZFS filesystem version: " ZPL_VERSION_STRING "\n");

	/*
	 * Initialize .zfs directory structures
	 */
	zfsctl_init();

	/*
	 * Initialize znode cache, vnode ops, etc...
	 */
	zfs_znode_init();

	dmu_objset_register_type(DMU_OST_ZFS, zpl_get_file_info);

	/* Start arc_os - reclaim thread */
	arc_os_init();

}

void
zfs_fini(void)
{
	arc_os_fini();
	zfsctl_fini();
	zfs_znode_fini();
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
	ASSERT(RRM_WRITE_HELD(&zfsvfs->z_teardown_lock));
	ASSERT(RW_WRITE_HELD(&zfsvfs->z_teardown_inactive_lock));

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
	rw_exit(&zfsvfs->z_teardown_inactive_lock);
	rrm_exit(&zfsvfs->z_teardown_lock, FTAG);

	/*
	 * Try to force unmount this file system.
	 */
	zfs_vfs_unmount(zfsvfs->z_vfs, 0, NULL);
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
	error = dmu_tx_assign(tx, TXG_WAIT);
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
		ASSERT(error == 0);

		VERIFY(0 == sa_set_sa_object(os, sa_obj));
		sa_register_update_callback(os, zfs_sa_upgrade);
	}

	spa_history_log_internal(dmu_objset_spa(os), "upgrade", tx,
	    "oldver=%llu newver=%llu dataset = %llu", zfsvfs->z_version,
	    newvers, dmu_objset_id(os));

	dmu_tx_commit(tx);

	zfsvfs->z_version = newvers;
	os->os_version = newvers;

	zfs_set_fuid_feature(zfsvfs);

	return (0);
}

/*
 * Read a property stored within the master node.
 */
int
zfs_get_zplprop(objset_t *os, zfs_prop_t prop, uint64_t *value)
{
	uint64_t *cached_copy = NULL;

	/*
	 * Figure out where in the objset_t the cached copy would live, if it
	 * is available for the requested property.
	 */
	if (os != NULL) {
		switch (prop) {
			case ZFS_PROP_VERSION:
				cached_copy = &os->os_version;
				break;
			case ZFS_PROP_NORMALIZE:
				cached_copy = &os->os_normalization;
				break;
			case ZFS_PROP_UTF8ONLY:
				cached_copy = &os->os_utf8only;
				break;
			case ZFS_PROP_CASE:
				cached_copy = &os->os_casesensitivity;
				break;
			default:
				break;
		}
	}
	if (cached_copy != NULL && *cached_copy != OBJSET_PROP_UNINITIALIZED) {
		*value = *cached_copy;
		return (0);
	}

	/*
	 * If the property wasn't cached, look up the file system's value for
	 * the property. For the version property, we look up a slightly
	 * different string.
	 */
	const char *pname;
	int error = ENOENT;
	if (prop == ZFS_PROP_VERSION) {
		pname = ZPL_VERSION_STR;
	} else {
		pname = zfs_prop_to_name(prop);
	}

	if (os != NULL) {
		ASSERT3U(os->os_phys->os_type, ==, DMU_OST_ZFS);
		error = zap_lookup(os, MASTER_NODE_OBJ, pname, 8, 1, value);
	}

	if (error == ENOENT) {
		/* No value set, use the default value */
		switch (prop) {
		case ZFS_PROP_VERSION:
			*value = ZPL_VERSION;
			break;
		case ZFS_PROP_NORMALIZE:
		case ZFS_PROP_UTF8ONLY:
			*value = 0;
			break;
		case ZFS_PROP_CASE:
			*value = ZFS_CASE_SENSITIVE;
			break;
		case ZFS_PROP_ACLMODE:
			*value = ZFS_ACLTYPE_OFF;
			break;
		default:
			return (error);
		}
		error = 0;
	}

	/*
	 * If one of the methods for getting the property value above worked,
	 * copy it into the objset_t's cache.
	 */
	if (error == 0 && cached_copy != NULL) {
		*cached_copy = *value;
	}

	return (error);
}

/*
 * Return true if the coresponding vfs's unmounted flag is set.
 * Otherwise return false.
 * If this function returns true we know VFS unmount has been initiated.
 */
boolean_t
zfs_get_vfs_flag_unmounted(objset_t *os)
{
	zfsvfs_t *zfvp;
	boolean_t unmounted = B_FALSE;

	ASSERT(dmu_objset_type(os) == DMU_OST_ZFS);

	mutex_enter(&os->os_user_ptr_lock);
	zfvp = dmu_objset_get_user(os);
	if (zfvp != NULL && zfvp->z_vfs != NULL &&
	    (vfs_isunmount(zfvp->z_vfs)))
		unmounted = B_TRUE;
	mutex_exit(&os->os_user_ptr_lock);

	return (unmounted);
}
