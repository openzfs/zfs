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

/* Portions Copyright 2010 Robert Milkowski */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/kmem.h>
#include <sys/pathname.h>
#include <sys/vnode.h>
#include <sys/vfs.h>
#include <sys/vfs_opreg.h>
#include <sys/mntent.h>
#include <sys/mount.h>
#include <sys/cmn_err.h>
#include "fs/fs_subr.h"
#include <sys/zfs_znode.h>
#include <sys/zfs_dir.h>
#include <sys/zil.h>
#include <sys/fs/zfs.h>
#include <sys/dmu.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_deleg.h>
#include <sys/spa.h>
#include <sys/zap.h>
#include <sys/sa.h>
#include <sys/varargs.h>
#include <sys/policy.h>
#include <sys/atomic.h>
#include <sys/mkdev.h>
#include <sys/modctl.h>
#include <sys/refstr.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_ctldir.h>
#include <sys/zfs_fuid.h>
#include <sys/bootconf.h>
#include <sys/sunddi.h>
#include <sys/dnlc.h>
#include <sys/dmu_objset.h>
#include <sys/spa_boot.h>
#include <sys/sa.h>
#include "zfs_comutil.h"

#ifdef HAVE_ZPL

/*ARGSUSED*/
int
zfs_sync(vfs_t *vfsp, short flag, cred_t *cr)
{
	/*
	 * Data integrity is job one.  We don't want a compromised kernel
	 * writing to the storage pool, so we never sync during panic.
	 */
	if (panicstr)
		return (0);

	if (vfsp != NULL) {
		/*
		 * Sync a specific filesystem.
		 */
		zfsvfs_t *zfsvfs = vfsp->vfs_data;
		dsl_pool_t *dp;

		ZFS_ENTER(zfsvfs);
		dp = dmu_objset_pool(zfsvfs->z_os);

#ifdef HAVE_SHUTDOWN
		/*
		 * If the system is shutting down, then skip any
		 * filesystems which may exist on a suspended pool.
		 *
		 * XXX: This can be implemented using the Linux reboot
		 *      notifiers: {un}register_reboot_notifier().
		 */
		if (sys_shutdown && spa_suspended(dp->dp_spa)) {
			ZFS_EXIT(zfsvfs);
			return (0);
		}
#endif /* HAVE_SHUTDOWN */

		if (zfsvfs->z_log != NULL)
			zil_commit(zfsvfs->z_log, 0);

		ZFS_EXIT(zfsvfs);
	} else {
		/*
		 * Sync all ZFS filesystems.  This is what happens when you
		 * run sync(1M).  Unlike other filesystems, ZFS honors the
		 * request by waiting for all pools to commit all dirty data.
		 */
		spa_sync_allpools();
	}

	return (0);
}
EXPORT_SYMBOL(zfs_sync);

static void
atime_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	if (newval == TRUE) {
		zfsvfs->z_atime = TRUE;
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_NOATIME);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_ATIME, NULL, 0);
	} else {
		zfsvfs->z_atime = FALSE;
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_ATIME);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_NOATIME, NULL, 0);
	}
}

static void
xattr_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	if (newval == TRUE) {
		/* XXX locking on vfs_flag? */
		zfsvfs->z_vfs->vfs_flag |= VFS_XATTR;
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_NOXATTR);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_XATTR, NULL, 0);
	} else {
		/* XXX locking on vfs_flag? */
		zfsvfs->z_vfs->vfs_flag &= ~VFS_XATTR;
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_XATTR);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_NOXATTR, NULL, 0);
	}
}

static void
blksz_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	if (newval < SPA_MINBLOCKSIZE ||
	    newval > SPA_MAXBLOCKSIZE || !ISP2(newval))
		newval = SPA_MAXBLOCKSIZE;

	zfsvfs->z_max_blksz = newval;
	zfsvfs->z_vfs->vfs_bsize = newval;
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
devices_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	if (newval == FALSE) {
		zfsvfs->z_vfs->vfs_flag |= VFS_NODEVICES;
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_DEVICES);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_NODEVICES, NULL, 0);
	} else {
		zfsvfs->z_vfs->vfs_flag &= ~VFS_NODEVICES;
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_NODEVICES);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_DEVICES, NULL, 0);
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
	} else {
		zfsvfs->z_vfs->vfs_flag &= ~VFS_NOSETUID;
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_NOSETUID);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_SETUID, NULL, 0);
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
vscan_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	zfsvfs->z_vscan = newval;
}

static void
acl_inherit_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	zfsvfs->z_acl_inherit = newval;
}

int
zfs_register_callbacks(vfs_t *vfsp)
{
	struct dsl_dataset *ds = NULL;
	objset_t *os = NULL;
	zfsvfs_t *zfsvfs = NULL;
	uint64_t nbmand;
	int readonly, do_readonly = B_FALSE;
	int setuid, do_setuid = B_FALSE;
	int exec, do_exec = B_FALSE;
	int devices, do_devices = B_FALSE;
	int xattr, do_xattr = B_FALSE;
	int atime, do_atime = B_FALSE;
	int error = 0;

	ASSERT(vfsp);
	zfsvfs = vfsp->vfs_data;
	ASSERT(zfsvfs);
	os = zfsvfs->z_os;

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
	if (vfs_optionisset(vfsp, MNTOPT_NOSUID, NULL)) {
		devices = B_FALSE;
		setuid = B_FALSE;
		do_devices = B_TRUE;
		do_setuid = B_TRUE;
	} else {
		if (vfs_optionisset(vfsp, MNTOPT_NODEVICES, NULL)) {
			devices = B_FALSE;
			do_devices = B_TRUE;
		} else if (vfs_optionisset(vfsp, MNTOPT_DEVICES, NULL)) {
			devices = B_TRUE;
			do_devices = B_TRUE;
		}

		if (vfs_optionisset(vfsp, MNTOPT_NOSETUID, NULL)) {
			setuid = B_FALSE;
			do_setuid = B_TRUE;
		} else if (vfs_optionisset(vfsp, MNTOPT_SETUID, NULL)) {
			setuid = B_TRUE;
			do_setuid = B_TRUE;
		}
	}
	if (vfs_optionisset(vfsp, MNTOPT_NOEXEC, NULL)) {
		exec = B_FALSE;
		do_exec = B_TRUE;
	} else if (vfs_optionisset(vfsp, MNTOPT_EXEC, NULL)) {
		exec = B_TRUE;
		do_exec = B_TRUE;
	}
	if (vfs_optionisset(vfsp, MNTOPT_NOXATTR, NULL)) {
		xattr = B_FALSE;
		do_xattr = B_TRUE;
	} else if (vfs_optionisset(vfsp, MNTOPT_XATTR, NULL)) {
		xattr = B_TRUE;
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
	} else {
		char osname[MAXNAMELEN];

		dmu_objset_name(os, osname);
		if ((error = dsl_prop_get_integer(osname, "nbmand", &nbmand,
		    NULL))) {
			return (error);
		}
	}

	/*
	 * Register property callbacks.
	 *
	 * It would probably be fine to just check for i/o error from
	 * the first prop_register(), but I guess I like to go
	 * overboard...
	 */
	ds = dmu_objset_ds(os);
	error = dsl_prop_register(ds, "atime", atime_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    "xattr", xattr_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    "recordsize", blksz_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    "readonly", readonly_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    "devices", devices_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    "setuid", setuid_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    "exec", exec_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    "snapdir", snapdir_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    "aclinherit", acl_inherit_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    "vscan", vscan_changed_cb, zfsvfs);
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

	nbmand_changed_cb(zfsvfs, nbmand);

	return (0);

unregister:
	/*
	 * We may attempt to unregister some callbacks that are not
	 * registered, but this is OK; it will simply return ENOMSG,
	 * which we will ignore.
	 */
	(void) dsl_prop_unregister(ds, "atime", atime_changed_cb, zfsvfs);
	(void) dsl_prop_unregister(ds, "xattr", xattr_changed_cb, zfsvfs);
	(void) dsl_prop_unregister(ds, "recordsize", blksz_changed_cb, zfsvfs);
	(void) dsl_prop_unregister(ds, "readonly", readonly_changed_cb, zfsvfs);
	(void) dsl_prop_unregister(ds, "devices", devices_changed_cb, zfsvfs);
	(void) dsl_prop_unregister(ds, "setuid", setuid_changed_cb, zfsvfs);
	(void) dsl_prop_unregister(ds, "exec", exec_changed_cb, zfsvfs);
	(void) dsl_prop_unregister(ds, "snapdir", snapdir_changed_cb, zfsvfs);
	(void) dsl_prop_unregister(ds, "aclinherit", acl_inherit_changed_cb,
	    zfsvfs);
	(void) dsl_prop_unregister(ds, "vscan", vscan_changed_cb, zfsvfs);
	return (error);

}
EXPORT_SYMBOL(zfs_register_callbacks);
#endif /* HAVE_ZPL */

static int
zfs_space_delta_cb(dmu_object_type_t bonustype, void *data,
    uint64_t *userp, uint64_t *groupp)
{
	znode_phys_t *znp = data;
	int error = 0;

	/*
	 * Is it a valid type of object to track?
	 */
	if (bonustype != DMU_OT_ZNODE && bonustype != DMU_OT_SA)
		return (ENOENT);

	/*
	 * If we have a NULL data pointer
	 * then assume the id's aren't changing and
	 * return EEXIST to the dmu to let it know to
	 * use the same ids
	 */
	if (data == NULL)
		return (EEXIST);

	if (bonustype == DMU_OT_ZNODE) {
		*userp = znp->zp_uid;
		*groupp = znp->zp_gid;
	} else {
		int hdrsize;

		ASSERT(bonustype == DMU_OT_SA);
		hdrsize = sa_hdrsize(data);

		if (hdrsize != 0) {
			*userp = *((uint64_t *)((uintptr_t)data + hdrsize +
			    SA_UID_OFFSET));
			*groupp = *((uint64_t *)((uintptr_t)data + hdrsize +
			    SA_GID_OFFSET));
		} else {
			/*
			 * This should only happen for newly created
			 * files that haven't had the znode data filled
			 * in yet.
			 */
			*userp = 0;
			*groupp = 0;
		}
	}
	return (error);
}

#ifdef HAVE_ZPL
static void
fuidstr_to_sid(zfsvfs_t *zfsvfs, const char *fuidstr,
    char *domainbuf, int buflen, uid_t *ridp)
{
	uint64_t fuid;
	const char *domain;

	fuid = strtonum(fuidstr, NULL);

	domain = zfs_fuid_find_by_idx(zfsvfs, FUID_INDEX(fuid));
	if (domain)
		(void) strlcpy(domainbuf, domain, buflen);
	else
		domainbuf[0] = '\0';
	*ridp = FUID_RID(fuid);
}

static uint64_t
zfs_userquota_prop_to_obj(zfsvfs_t *zfsvfs, zfs_userquota_prop_t type)
{
	switch (type) {
	case ZFS_PROP_USERUSED:
		return (DMU_USERUSED_OBJECT);
	case ZFS_PROP_GROUPUSED:
		return (DMU_GROUPUSED_OBJECT);
	case ZFS_PROP_USERQUOTA:
		return (zfsvfs->z_userquota_obj);
	case ZFS_PROP_GROUPQUOTA:
		return (zfsvfs->z_groupquota_obj);
	default:
		return (ENOTSUP);
	}
	return (0);
}

int
zfs_userspace_many(zfsvfs_t *zfsvfs, zfs_userquota_prop_t type,
    uint64_t *cookiep, void *vbuf, uint64_t *bufsizep)
{
	int error;
	zap_cursor_t zc;
	zap_attribute_t za;
	zfs_useracct_t *buf = vbuf;
	uint64_t obj;

	if (!dmu_objset_userspace_present(zfsvfs->z_os))
		return (ENOTSUP);

	obj = zfs_userquota_prop_to_obj(zfsvfs, type);
	if (obj == 0) {
		*bufsizep = 0;
		return (0);
	}

	for (zap_cursor_init_serialized(&zc, zfsvfs->z_os, obj, *cookiep);
	    (error = zap_cursor_retrieve(&zc, &za)) == 0;
	    zap_cursor_advance(&zc)) {
		if ((uintptr_t)buf - (uintptr_t)vbuf + sizeof (zfs_useracct_t) >
		    *bufsizep)
			break;

		fuidstr_to_sid(zfsvfs, za.za_name,
		    buf->zu_domain, sizeof (buf->zu_domain), &buf->zu_rid);

		buf->zu_space = za.za_first_integer;
		buf++;
	}
	if (error == ENOENT)
		error = 0;

	ASSERT3U((uintptr_t)buf - (uintptr_t)vbuf, <=, *bufsizep);
	*bufsizep = (uintptr_t)buf - (uintptr_t)vbuf;
	*cookiep = zap_cursor_serialize(&zc);
	zap_cursor_fini(&zc);
	return (error);
}
EXPORT_SYMBOL(zfs_userspace_many);

/*
 * buf must be big enough (eg, 32 bytes)
 */
static int
id_to_fuidstr(zfsvfs_t *zfsvfs, const char *domain, uid_t rid,
    char *buf, boolean_t addok)
{
	uint64_t fuid;
	int domainid = 0;

	if (domain && domain[0]) {
		domainid = zfs_fuid_find_by_domain(zfsvfs, domain, NULL, addok);
		if (domainid == -1)
			return (ENOENT);
	}
	fuid = FUID_ENCODE(domainid, rid);
	(void) sprintf(buf, "%llx", (longlong_t)fuid);
	return (0);
}

int
zfs_userspace_one(zfsvfs_t *zfsvfs, zfs_userquota_prop_t type,
    const char *domain, uint64_t rid, uint64_t *valp)
{
	char buf[32];
	int err;
	uint64_t obj;

	*valp = 0;

	if (!dmu_objset_userspace_present(zfsvfs->z_os))
		return (ENOTSUP);

	obj = zfs_userquota_prop_to_obj(zfsvfs, type);
	if (obj == 0)
		return (0);

	err = id_to_fuidstr(zfsvfs, domain, rid, buf, B_FALSE);
	if (err)
		return (err);

	err = zap_lookup(zfsvfs->z_os, obj, buf, 8, 1, valp);
	if (err == ENOENT)
		err = 0;
	return (err);
}
EXPORT_SYMBOL(zfs_userspace_one);

int
zfs_set_userquota(zfsvfs_t *zfsvfs, zfs_userquota_prop_t type,
    const char *domain, uint64_t rid, uint64_t quota)
{
	char buf[32];
	int err;
	dmu_tx_t *tx;
	uint64_t *objp;
	boolean_t fuid_dirtied;

	if (type != ZFS_PROP_USERQUOTA && type != ZFS_PROP_GROUPQUOTA)
		return (EINVAL);

	if (zfsvfs->z_version < ZPL_VERSION_USERSPACE)
		return (ENOTSUP);

	objp = (type == ZFS_PROP_USERQUOTA) ? &zfsvfs->z_userquota_obj :
	    &zfsvfs->z_groupquota_obj;

	err = id_to_fuidstr(zfsvfs, domain, rid, buf, B_TRUE);
	if (err)
		return (err);
	fuid_dirtied = zfsvfs->z_fuid_dirty;

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_zap(tx, *objp ? *objp : DMU_NEW_OBJECT, B_TRUE, NULL);
	if (*objp == 0) {
		dmu_tx_hold_zap(tx, MASTER_NODE_OBJ, B_TRUE,
		    zfs_userquota_prop_prefixes[type]);
	}
	if (fuid_dirtied)
		zfs_fuid_txhold(zfsvfs, tx);
	err = dmu_tx_assign(tx, TXG_WAIT);
	if (err) {
		dmu_tx_abort(tx);
		return (err);
	}

	mutex_enter(&zfsvfs->z_lock);
	if (*objp == 0) {
		*objp = zap_create(zfsvfs->z_os, DMU_OT_USERGROUP_QUOTA,
		    DMU_OT_NONE, 0, tx);
		VERIFY(0 == zap_add(zfsvfs->z_os, MASTER_NODE_OBJ,
		    zfs_userquota_prop_prefixes[type], 8, 1, objp, tx));
	}
	mutex_exit(&zfsvfs->z_lock);

	if (quota == 0) {
		err = zap_remove(zfsvfs->z_os, *objp, buf, tx);
		if (err == ENOENT)
			err = 0;
	} else {
		err = zap_update(zfsvfs->z_os, *objp, buf, 8, 1, &quota, tx);
	}
	ASSERT(err == 0);
	if (fuid_dirtied)
		zfs_fuid_sync(zfsvfs, tx);
	dmu_tx_commit(tx);
	return (err);
}
EXPORT_SYMBOL(zfs_set_userquota);

boolean_t
zfs_fuid_overquota(zfsvfs_t *zfsvfs, boolean_t isgroup, uint64_t fuid)
{
	char buf[32];
	uint64_t used, quota, usedobj, quotaobj;
	int err;

	usedobj = isgroup ? DMU_GROUPUSED_OBJECT : DMU_USERUSED_OBJECT;
	quotaobj = isgroup ? zfsvfs->z_groupquota_obj : zfsvfs->z_userquota_obj;

	if (quotaobj == 0 || zfsvfs->z_replay)
		return (B_FALSE);

	(void) sprintf(buf, "%llx", (longlong_t)fuid);
	err = zap_lookup(zfsvfs->z_os, quotaobj, buf, 8, 1, &quota);
	if (err != 0)
		return (B_FALSE);

	err = zap_lookup(zfsvfs->z_os, usedobj, buf, 8, 1, &used);
	if (err != 0)
		return (B_FALSE);
	return (used >= quota);
}
EXPORT_SYMBOL(zfs_fuid_overquota);

boolean_t
zfs_owner_overquota(zfsvfs_t *zfsvfs, znode_t *zp, boolean_t isgroup)
{
	uint64_t fuid;
	uint64_t quotaobj;

	quotaobj = isgroup ? zfsvfs->z_groupquota_obj : zfsvfs->z_userquota_obj;

	fuid = isgroup ? zp->z_gid : zp->z_uid;

	if (quotaobj == 0 || zfsvfs->z_replay)
		return (B_FALSE);

	return (zfs_fuid_overquota(zfsvfs, isgroup, fuid));
}
EXPORT_SYMBOL(zfs_owner_overquota);

int
zfsvfs_create(const char *osname, zfsvfs_t **zfvp)
{
	objset_t *os;
	zfsvfs_t *zfsvfs;
	uint64_t zval;
	int i, error;
	uint64_t sa_obj;

	zfsvfs = kmem_zalloc(sizeof (zfsvfs_t), KM_SLEEP);

	/*
	 * We claim to always be readonly so we can open snapshots;
	 * other ZPL code will prevent us from writing to snapshots.
	 */
	error = dmu_objset_own(osname, DMU_OST_ZFS, B_TRUE, zfsvfs, &os);
	if (error) {
		kmem_free(zfsvfs, sizeof (zfsvfs_t));
		return (error);
	}

	/*
	 * Initialize the zfs-specific filesystem structure.
	 * Should probably make this a kmem cache, shuffle fields,
	 * and just bzero up to z_hold_mtx[].
	 */
	zfsvfs->z_vfs = NULL;
	zfsvfs->z_parent = zfsvfs;
	zfsvfs->z_max_blksz = SPA_MAXBLOCKSIZE;
	zfsvfs->z_show_ctldir = ZFS_SNAPDIR_VISIBLE;
	zfsvfs->z_os = os;

	error = zfs_get_zplprop(os, ZFS_PROP_VERSION, &zfsvfs->z_version);
	if (error) {
		goto out;
	} else if (zfsvfs->z_version >
	    zfs_zpl_version_map(spa_version(dmu_objset_spa(os)))) {
		(void) printk("Can't mount a version %lld file system "
		    "on a version %lld pool\n. Pool must be upgraded to mount "
		    "this file system.", (u_longlong_t)zfsvfs->z_version,
		    (u_longlong_t)spa_version(dmu_objset_spa(os)));
		error = ENOTSUP;
		goto out;
	}
	if ((error = zfs_get_zplprop(os, ZFS_PROP_NORMALIZE, &zval)) != 0)
		goto out;
	zfsvfs->z_norm = (int)zval;

	if ((error = zfs_get_zplprop(os, ZFS_PROP_UTF8ONLY, &zval)) != 0)
		goto out;
	zfsvfs->z_utf8 = (zval != 0);

	if ((error = zfs_get_zplprop(os, ZFS_PROP_CASE, &zval)) != 0)
		goto out;
	zfsvfs->z_case = (uint_t)zval;

	/*
	 * Fold case on file systems that are always or sometimes case
	 * insensitive.
	 */
	if (zfsvfs->z_case == ZFS_CASE_INSENSITIVE ||
	    zfsvfs->z_case == ZFS_CASE_MIXED)
		zfsvfs->z_norm |= U8_TEXTPREP_TOUPPER;

	zfsvfs->z_use_fuids = USE_FUIDS(zfsvfs->z_version, zfsvfs->z_os);
	zfsvfs->z_use_sa = USE_SA(zfsvfs->z_version, zfsvfs->z_os);

	if (zfsvfs->z_use_sa) {
		/* should either have both of these objects or none */
		error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_SA_ATTRS, 8, 1,
		    &sa_obj);
		if (error)
			return (error);
	} else {
		/*
		 * Pre SA versions file systems should never touch
		 * either the attribute registration or layout objects.
		 */
		sa_obj = 0;
	}

	error = sa_setup(os, sa_obj, zfs_attr_table, ZPL_END,
	    &zfsvfs->z_attr_table);
	if (error)
		goto out;

	if (zfsvfs->z_version >= ZPL_VERSION_SA)
		sa_register_update_callback(os, zfs_sa_upgrade);

	error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_ROOT_OBJ, 8, 1,
	    &zfsvfs->z_root);
	if (error)
		goto out;
	ASSERT(zfsvfs->z_root != 0);

	error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_UNLINKED_SET, 8, 1,
	    &zfsvfs->z_unlinkedobj);
	if (error)
		goto out;

	error = zap_lookup(os, MASTER_NODE_OBJ,
	    zfs_userquota_prop_prefixes[ZFS_PROP_USERQUOTA],
	    8, 1, &zfsvfs->z_userquota_obj);
	if (error && error != ENOENT)
		goto out;

	error = zap_lookup(os, MASTER_NODE_OBJ,
	    zfs_userquota_prop_prefixes[ZFS_PROP_GROUPQUOTA],
	    8, 1, &zfsvfs->z_groupquota_obj);
	if (error && error != ENOENT)
		goto out;

	error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_FUID_TABLES, 8, 1,
	    &zfsvfs->z_fuid_obj);
	if (error && error != ENOENT)
		goto out;

	error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_SHARES_DIR, 8, 1,
	    &zfsvfs->z_shares_dir);
	if (error && error != ENOENT)
		goto out;

	mutex_init(&zfsvfs->z_znodes_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&zfsvfs->z_lock, NULL, MUTEX_DEFAULT, NULL);
	list_create(&zfsvfs->z_all_znodes, sizeof (znode_t),
	    offsetof(znode_t, z_link_node));
	rrw_init(&zfsvfs->z_teardown_lock);
	rw_init(&zfsvfs->z_teardown_inactive_lock, NULL, RW_DEFAULT, NULL);
	rw_init(&zfsvfs->z_fuid_lock, NULL, RW_DEFAULT, NULL);
	for (i = 0; i != ZFS_OBJ_MTX_SZ; i++)
		mutex_init(&zfsvfs->z_hold_mtx[i], NULL, MUTEX_DEFAULT, NULL);

	*zfvp = zfsvfs;
	return (0);

out:
	dmu_objset_disown(os, zfsvfs);
	*zfvp = NULL;
	kmem_free(zfsvfs, sizeof (zfsvfs_t));
	return (error);
}

static int
zfsvfs_setup(zfsvfs_t *zfsvfs, boolean_t mounting)
{
	int error;

	error = zfs_register_callbacks(zfsvfs->z_vfs);
	if (error)
		return (error);

	/*
	 * Set the objset user_ptr to track its zfsvfs.
	 */
	mutex_enter(&zfsvfs->z_os->os_user_ptr_lock);
	dmu_objset_set_user(zfsvfs->z_os, zfsvfs);
	mutex_exit(&zfsvfs->z_os->os_user_ptr_lock);

	zfsvfs->z_log = zil_open(zfsvfs->z_os, zfs_get_data);

	/*
	 * If we are not mounting (ie: online recv), then we don't
	 * have to worry about replaying the log as we blocked all
	 * operations out since we closed the ZIL.
	 */
	if (mounting) {
		boolean_t readonly;

		/*
		 * During replay we remove the read only flag to
		 * allow replays to succeed.
		 */
		readonly = zfsvfs->z_vfs->vfs_flag & VFS_RDONLY;
		if (readonly != 0)
			zfsvfs->z_vfs->vfs_flag &= ~VFS_RDONLY;
		else
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
		zfsvfs->z_vfs->vfs_flag |= readonly; /* restore readonly bit */
	}

	return (0);
}

void
zfsvfs_free(zfsvfs_t *zfsvfs)
{
	int i;
	extern krwlock_t zfsvfs_lock; /* in zfs_znode.c */

	/*
	 * This is a barrier to prevent the filesystem from going away in
	 * zfs_znode_move() until we can safely ensure that the filesystem is
	 * not unmounted. We consider the filesystem valid before the barrier
	 * and invalid after the barrier.
	 */
	rw_enter(&zfsvfs_lock, RW_READER);
	rw_exit(&zfsvfs_lock);

	zfs_fuid_destroy(zfsvfs);

	mutex_destroy(&zfsvfs->z_znodes_lock);
	mutex_destroy(&zfsvfs->z_lock);
	list_destroy(&zfsvfs->z_all_znodes);
	rrw_destroy(&zfsvfs->z_teardown_lock);
	rw_destroy(&zfsvfs->z_teardown_inactive_lock);
	rw_destroy(&zfsvfs->z_fuid_lock);
	for (i = 0; i != ZFS_OBJ_MTX_SZ; i++)
		mutex_destroy(&zfsvfs->z_hold_mtx[i]);
	kmem_free(zfsvfs, sizeof (zfsvfs_t));
}

static void
zfs_set_fuid_feature(zfsvfs_t *zfsvfs)
{
	zfsvfs->z_use_fuids = USE_FUIDS(zfsvfs->z_version, zfsvfs->z_os);
	if (zfsvfs->z_use_fuids && zfsvfs->z_vfs) {
		vfs_set_feature(zfsvfs->z_vfs, VFSFT_XVATTR);
		vfs_set_feature(zfsvfs->z_vfs, VFSFT_SYSATTR_VIEWS);
		vfs_set_feature(zfsvfs->z_vfs, VFSFT_ACEMASKONACCESS);
		vfs_set_feature(zfsvfs->z_vfs, VFSFT_ACLONCREATE);
		vfs_set_feature(zfsvfs->z_vfs, VFSFT_ACCESS_FILTER);
		vfs_set_feature(zfsvfs->z_vfs, VFSFT_REPARSE);
	}
	zfsvfs->z_use_sa = USE_SA(zfsvfs->z_version, zfsvfs->z_os);
}

int
zfs_domount(vfs_t *vfsp, char *osname)
{
	uint64_t recordsize, fsid_guid;
	int error = 0;
	zfsvfs_t *zfsvfs;

	ASSERT(vfsp);
	ASSERT(osname);

	error = zfsvfs_create(osname, &zfsvfs);
	if (error)
		return (error);
	zfsvfs->z_vfs = vfsp;

	/* Initialize the generic filesystem structure. */
	vfsp->vfs_bcount = 0;
	vfsp->vfs_data = NULL;

	if ((error = dsl_prop_get_integer(osname, "recordsize",
	    &recordsize, NULL)))
		goto out;

	vfsp->vfs_bsize = recordsize;
	vfsp->vfs_flag |= VFS_NOTRUNC;
	vfsp->vfs_data = zfsvfs;

	/*
	 * The fsid is 64 bits, composed of an 8-bit fs type, which
	 * separates our fsid from any other filesystem types, and a
	 * 56-bit objset unique ID.  The objset unique ID is unique to
	 * all objsets open on this system, provided by unique_create().
	 * The 8-bit fs type must be put in the low bits of fsid[1]
	 * because that's where other Solaris filesystems put it.
	 */
	fsid_guid = dmu_objset_fsid_guid(zfsvfs->z_os);
	ASSERT((fsid_guid & ~((1ULL<<56)-1)) == 0);
	vfsp->vfs_fsid.val[0] = fsid_guid;
	vfsp->vfs_fsid.val[1] = ((fsid_guid>>32) << 8);

	/*
	 * Set features for file system.
	 */
	zfs_set_fuid_feature(zfsvfs);
	if (zfsvfs->z_case == ZFS_CASE_INSENSITIVE) {
		vfs_set_feature(vfsp, VFSFT_DIRENTFLAGS);
		vfs_set_feature(vfsp, VFSFT_CASEINSENSITIVE);
		vfs_set_feature(vfsp, VFSFT_NOCASESENSITIVE);
	} else if (zfsvfs->z_case == ZFS_CASE_MIXED) {
		vfs_set_feature(vfsp, VFSFT_DIRENTFLAGS);
		vfs_set_feature(vfsp, VFSFT_CASEINSENSITIVE);
	}
	vfs_set_feature(vfsp, VFSFT_ZEROCOPY_SUPPORTED);

	if (dmu_objset_is_snapshot(zfsvfs->z_os)) {
		uint64_t pval;

		atime_changed_cb(zfsvfs, B_FALSE);
		readonly_changed_cb(zfsvfs, B_TRUE);
		if ((error = dsl_prop_get_integer(osname,"xattr",&pval,NULL)))
			goto out;
		xattr_changed_cb(zfsvfs, pval);
		zfsvfs->z_issnap = B_TRUE;
		zfsvfs->z_os->os_sync = ZFS_SYNC_DISABLED;

		mutex_enter(&zfsvfs->z_os->os_user_ptr_lock);
		dmu_objset_set_user(zfsvfs->z_os, zfsvfs);
		mutex_exit(&zfsvfs->z_os->os_user_ptr_lock);
	} else {
		error = zfsvfs_setup(zfsvfs, B_TRUE);
	}

	if (!zfsvfs->z_issnap)
		zfsctl_create(zfsvfs);
out:
	if (error) {
		dmu_objset_disown(zfsvfs->z_os, zfsvfs);
		zfsvfs_free(zfsvfs);
	}

	return (error);
}
EXPORT_SYMBOL(zfs_domount);

void
zfs_unregister_callbacks(zfsvfs_t *zfsvfs)
{
	objset_t *os = zfsvfs->z_os;
	struct dsl_dataset *ds;

	/*
	 * Unregister properties.
	 */
	if (!dmu_objset_is_snapshot(os)) {
		ds = dmu_objset_ds(os);
		VERIFY(dsl_prop_unregister(ds, "atime", atime_changed_cb,
		    zfsvfs) == 0);

		VERIFY(dsl_prop_unregister(ds, "xattr", xattr_changed_cb,
		    zfsvfs) == 0);

		VERIFY(dsl_prop_unregister(ds, "recordsize", blksz_changed_cb,
		    zfsvfs) == 0);

		VERIFY(dsl_prop_unregister(ds, "readonly", readonly_changed_cb,
		    zfsvfs) == 0);

		VERIFY(dsl_prop_unregister(ds, "devices", devices_changed_cb,
		    zfsvfs) == 0);

		VERIFY(dsl_prop_unregister(ds, "setuid", setuid_changed_cb,
		    zfsvfs) == 0);

		VERIFY(dsl_prop_unregister(ds, "exec", exec_changed_cb,
		    zfsvfs) == 0);

		VERIFY(dsl_prop_unregister(ds, "snapdir", snapdir_changed_cb,
		    zfsvfs) == 0);

		VERIFY(dsl_prop_unregister(ds, "aclinherit",
		    acl_inherit_changed_cb, zfsvfs) == 0);

		VERIFY(dsl_prop_unregister(ds, "vscan",
		    vscan_changed_cb, zfsvfs) == 0);
	}
}
EXPORT_SYMBOL(zfs_unregister_callbacks);

#ifdef HAVE_MLSLABEL
/*
 * zfs_check_global_label:
 *	Check that the hex label string is appropriate for the dataset
 *	being mounted into the global_zone proper.
 *
 *	Return an error if the hex label string is not default or
 *	admin_low/admin_high.  For admin_low labels, the corresponding
 *	dataset must be readonly.
 */
int
zfs_check_global_label(const char *dsname, const char *hexsl)
{
	if (strcasecmp(hexsl, ZFS_MLSLABEL_DEFAULT) == 0)
		return (0);
	if (strcasecmp(hexsl, ADMIN_HIGH) == 0)
		return (0);
	if (strcasecmp(hexsl, ADMIN_LOW) == 0) {
		/* must be readonly */
		uint64_t rdonly;

		if (dsl_prop_get_integer(dsname,
		    zfs_prop_to_name(ZFS_PROP_READONLY), &rdonly, NULL))
			return (EACCES);
		return (rdonly ? 0 : EACCES);
	}
	return (EACCES);
}
#endif /* HAVE_MLSLABEL */

int
zfs_statvfs(vfs_t *vfsp, struct statvfs64 *statp)
{
	zfsvfs_t *zfsvfs = vfsp->vfs_data;
	dev32_t d32;
	uint64_t refdbytes, availbytes, usedobjs, availobjs;

	ZFS_ENTER(zfsvfs);

	dmu_objset_space(zfsvfs->z_os,
	    &refdbytes, &availbytes, &usedobjs, &availobjs);

	/*
	 * The underlying storage pool actually uses multiple block sizes.
	 * We report the fragsize as the smallest block size we support,
	 * and we report our blocksize as the filesystem's maximum blocksize.
	 */
	statp->f_frsize = 1UL << SPA_MINBLOCKSHIFT;
	statp->f_bsize = zfsvfs->z_max_blksz;

	/*
	 * The following report "total" blocks of various kinds in the
	 * file system, but reported in terms of f_frsize - the
	 * "fragment" size.
	 */

	statp->f_blocks = (refdbytes + availbytes) >> SPA_MINBLOCKSHIFT;
	statp->f_bfree = availbytes >> SPA_MINBLOCKSHIFT;
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
	statp->f_favail = statp->f_ffree;	/* no "root reservation" */
	statp->f_files = statp->f_ffree + usedobjs;

	(void) cmpldev(&d32, vfsp->vfs_dev);
	statp->f_fsid = d32;

	/*
	 * We're a zfs filesystem.
	 */
	(void) strcpy(statp->f_basetype, MNTTYPE_ZFS);

	statp->f_flag = vf_to_stf(vfsp->vfs_flag);

	statp->f_namemax = ZFS_MAXNAMELEN;

	/*
	 * We have all of 32 characters to stuff a string here.
	 * Is there anything useful we could/should provide?
	 */
	bzero(statp->f_fstr, sizeof (statp->f_fstr));

	ZFS_EXIT(zfsvfs);
	return (0);
}
EXPORT_SYMBOL(zfs_statvfs);

int
zfs_root(vfs_t *vfsp, vnode_t **vpp)
{
	zfsvfs_t *zfsvfs = vfsp->vfs_data;
	znode_t *rootzp;
	int error;

	ZFS_ENTER(zfsvfs);

	error = zfs_zget(zfsvfs, zfsvfs->z_root, &rootzp);
	if (error == 0)
		*vpp = ZTOV(rootzp);

	ZFS_EXIT(zfsvfs);
	return (error);
}
EXPORT_SYMBOL(zfs_root);

/*
 * Teardown the zfsvfs::z_os.
 *
 * Note, if 'unmounting' if FALSE, we return with the 'z_teardown_lock'
 * and 'z_teardown_inactive_lock' held.
 */
static int
zfsvfs_teardown(zfsvfs_t *zfsvfs, boolean_t unmounting)
{
	znode_t	*zp;

	rrw_enter(&zfsvfs->z_teardown_lock, RW_WRITER, FTAG);

	if (!unmounting) {
		/*
		 * We purge the parent filesystem's vfsp as the parent
		 * filesystem and all of its snapshots have their vnode's
		 * v_vfsp set to the parent's filesystem's vfsp.  Note,
		 * 'z_parent' is self referential for non-snapshots.
		 */
		(void) dnlc_purge_vfsp(zfsvfs->z_parent->z_vfs, 0);
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
		rrw_exit(&zfsvfs->z_teardown_lock, FTAG);
		return (EIO);
	}

	/*
	 * At this point there are no vops active, and any new vops will
	 * fail with EIO since we have z_teardown_lock for writer (only
	 * relavent for forced unmount).
	 *
	 * Release all holds on dbufs.
	 */
	mutex_enter(&zfsvfs->z_znodes_lock);
	for (zp = list_head(&zfsvfs->z_all_znodes); zp != NULL;
	    zp = list_next(&zfsvfs->z_all_znodes, zp))
		if (zp->z_sa_hdl) {
			ASSERT(ZTOV(zp)->v_count > 0);
			zfs_znode_dmu_fini(zp);
		}
	mutex_exit(&zfsvfs->z_znodes_lock);

	/*
	 * If we are unmounting, set the unmounted flag and let new vops
	 * unblock.  zfs_inactive will have the unmounted behavior, and all
	 * other vops will fail with EIO.
	 */
	if (unmounting) {
		zfsvfs->z_unmounted = B_TRUE;
		rrw_exit(&zfsvfs->z_teardown_lock, FTAG);
		rw_exit(&zfsvfs->z_teardown_inactive_lock);
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
	if (dmu_objset_is_dirty_anywhere(zfsvfs->z_os))
		if (!(zfsvfs->z_vfs->vfs_flag & VFS_RDONLY))
			txg_wait_synced(dmu_objset_pool(zfsvfs->z_os), 0);
	(void) dmu_objset_evict_dbufs(zfsvfs->z_os);

	return (0);
}

/*ARGSUSED*/
int
zfs_umount(vfs_t *vfsp, int fflag, cred_t *cr)
{
	zfsvfs_t *zfsvfs = vfsp->vfs_data;
	objset_t *os;
	int ret;

	ret = secpolicy_fs_unmount(cr, vfsp);
	if (ret) {
		if (dsl_deleg_access((char *)refstr_value(vfsp->vfs_resource),
		    ZFS_DELEG_PERM_MOUNT, cr))
			return (ret);
	}

	/*
	 * We purge the parent filesystem's vfsp as the parent filesystem
	 * and all of its snapshots have their vnode's v_vfsp set to the
	 * parent's filesystem's vfsp.  Note, 'z_parent' is self
	 * referential for non-snapshots.
	 */
	(void) dnlc_purge_vfsp(zfsvfs->z_parent->z_vfs, 0);

	/*
	 * Unmount any snapshots mounted under .zfs before unmounting the
	 * dataset itself.
	 */
	if (zfsvfs->z_ctldir != NULL &&
	    (ret = zfsctl_umount_snapshots(vfsp, fflag, cr)) != 0) {
		return (ret);
	}

	if (!(fflag & MS_FORCE)) {
		/*
		 * Check the number of active vnodes in the file system.
		 * Our count is maintained in the vfs structure, but the
		 * number is off by 1 to indicate a hold on the vfs
		 * structure itself.
		 *
		 * The '.zfs' directory maintains a reference of its
		 * own, and any active references underneath are
		 * reflected in the vnode count.
		 */
		if (zfsvfs->z_ctldir == NULL) {
			if (vfsp->vfs_count > 1)
				return (EBUSY);
		} else {
			if (vfsp->vfs_count > 2 ||
			    zfsvfs->z_ctldir->v_count > 1)
				return (EBUSY);
		}
	}

	vfsp->vfs_flag |= VFS_UNMOUNTED;

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
		dmu_objset_disown(os, zfsvfs);
	}

	/*
	 * We can now safely destroy the '.zfs' directory node.
	 */
	if (zfsvfs->z_ctldir != NULL)
		zfsctl_destroy(zfsvfs);

	return (0);
}
EXPORT_SYMBOL(zfs_umount);

int
zfs_vget(vfs_t *vfsp, vnode_t **vpp, fid_t *fidp)
{
	zfsvfs_t	*zfsvfs = vfsp->vfs_data;
	znode_t		*zp;
	uint64_t	object = 0;
	uint64_t	fid_gen = 0;
	uint64_t	gen_mask;
	uint64_t	zp_gen;
	int 		i, err;

	*vpp = NULL;

	ZFS_ENTER(zfsvfs);

	if (fidp->fid_len == LONG_FID_LEN) {
		zfid_long_t	*zlfid = (zfid_long_t *)fidp;
		uint64_t	objsetid = 0;
		uint64_t	setgen = 0;

		for (i = 0; i < sizeof (zlfid->zf_setid); i++)
			objsetid |= ((uint64_t)zlfid->zf_setid[i]) << (8 * i);

		for (i = 0; i < sizeof (zlfid->zf_setgen); i++)
			setgen |= ((uint64_t)zlfid->zf_setgen[i]) << (8 * i);

		ZFS_EXIT(zfsvfs);

		err = zfsctl_lookup_objset(vfsp, objsetid, &zfsvfs);
		if (err)
			return (EINVAL);
		ZFS_ENTER(zfsvfs);
	}

	if (fidp->fid_len == SHORT_FID_LEN || fidp->fid_len == LONG_FID_LEN) {
		zfid_short_t	*zfid = (zfid_short_t *)fidp;

		for (i = 0; i < sizeof (zfid->zf_object); i++)
			object |= ((uint64_t)zfid->zf_object[i]) << (8 * i);

		for (i = 0; i < sizeof (zfid->zf_gen); i++)
			fid_gen |= ((uint64_t)zfid->zf_gen[i]) << (8 * i);
	} else {
		ZFS_EXIT(zfsvfs);
		return (EINVAL);
	}

	/* A zero fid_gen means we are in the .zfs control directories */
	if (fid_gen == 0 &&
	    (object == ZFSCTL_INO_ROOT || object == ZFSCTL_INO_SNAPDIR)) {
		*vpp = zfsvfs->z_ctldir;
		ASSERT(*vpp != NULL);
		if (object == ZFSCTL_INO_SNAPDIR) {
			VERIFY(zfsctl_root_lookup(*vpp, "snapshot", vpp, NULL,
			    0, NULL, NULL, NULL, NULL, NULL) == 0);
		} else {
			VN_HOLD(*vpp);
		}
		ZFS_EXIT(zfsvfs);
		return (0);
	}

	gen_mask = -1ULL >> (64 - 8 * i);

	dprintf("getting %llu [%u mask %llx]\n", object, fid_gen, gen_mask);
	if ((err = zfs_zget(zfsvfs, object, &zp))) {
		ZFS_EXIT(zfsvfs);
		return (err);
	}
	(void) sa_lookup(zp->z_sa_hdl, SA_ZPL_GEN(zfsvfs), &zp_gen,
	    sizeof (uint64_t));
	zp_gen = zp_gen & gen_mask;
	if (zp_gen == 0)
		zp_gen = 1;
	if (zp->z_unlinked || zp_gen != fid_gen) {
		dprintf("znode gen (%u) != fid gen (%u)\n", zp_gen, fid_gen);
		VN_RELE(ZTOV(zp));
		ZFS_EXIT(zfsvfs);
		return (EINVAL);
	}

	*vpp = ZTOV(zp);
	if (*vpp)
		zfs_inode_update(VTOZ(*vpp));

	ZFS_EXIT(zfsvfs);
	return (0);
}
EXPORT_SYMBOL(zfs_vget);

/*
 * Block out VOPs and close zfsvfs_t::z_os
 *
 * Note, if successful, then we return with the 'z_teardown_lock' and
 * 'z_teardown_inactive_lock' write held.
 */
int
zfs_suspend_fs(zfsvfs_t *zfsvfs)
{
	int error;

	if ((error = zfsvfs_teardown(zfsvfs, B_FALSE)) != 0)
		return (error);
	dmu_objset_disown(zfsvfs->z_os, zfsvfs);

	return (0);
}
EXPORT_SYMBOL(zfs_suspend_fs);

/*
 * Reopen zfsvfs_t::z_os and release VOPs.
 */
int
zfs_resume_fs(zfsvfs_t *zfsvfs, const char *osname)
{
	int err, err2;

	ASSERT(RRW_WRITE_HELD(&zfsvfs->z_teardown_lock));
	ASSERT(RW_WRITE_HELD(&zfsvfs->z_teardown_inactive_lock));

	err = dmu_objset_own(osname, DMU_OST_ZFS, B_FALSE, zfsvfs,
	    &zfsvfs->z_os);
	if (err) {
		zfsvfs->z_os = NULL;
	} else {
		znode_t *zp;
		uint64_t sa_obj = 0;

		err2 = zap_lookup(zfsvfs->z_os, MASTER_NODE_OBJ,
		    ZFS_SA_ATTRS, 8, 1, &sa_obj);

		if ((err || err2) && zfsvfs->z_version >= ZPL_VERSION_SA)
			goto bail;


		if ((err = sa_setup(zfsvfs->z_os, sa_obj,
		    zfs_attr_table,  ZPL_END, &zfsvfs->z_attr_table)) != 0)
			goto bail;

		VERIFY(zfsvfs_setup(zfsvfs, B_FALSE) == 0);

		/*
		 * Attempt to re-establish all the active znodes with
		 * their dbufs.  If a zfs_rezget() fails, then we'll let
		 * any potential callers discover that via ZFS_ENTER_VERIFY_VP
		 * when they try to use their znode.
		 */
		mutex_enter(&zfsvfs->z_znodes_lock);
		for (zp = list_head(&zfsvfs->z_all_znodes); zp;
		    zp = list_next(&zfsvfs->z_all_znodes, zp)) {
			(void) zfs_rezget(zp);
		}
		mutex_exit(&zfsvfs->z_znodes_lock);

	}

bail:
	/* release the VOPs */
	rw_exit(&zfsvfs->z_teardown_inactive_lock);
	rrw_exit(&zfsvfs->z_teardown_lock, FTAG);

	if (err) {
		/*
		 * Since we couldn't reopen zfsvfs::z_os, force
		 * unmount this file system.
		 */
		if (vn_vfswlock(zfsvfs->z_vfs->vfs_vnodecovered) == 0)
			(void) dounmount(zfsvfs->z_vfs, MS_FORCE, CRED());
	}
	return (err);
}
EXPORT_SYMBOL(zfs_resume_fs);

static void
zfs_freevfs(vfs_t *vfsp)
{
	zfsvfs_t *zfsvfs = vfsp->vfs_data;

	zfsvfs_free(zfsvfs);
}
#endif /* HAVE_ZPL */

void
zfs_init(void)
{
	zfsctl_init();
	zfs_znode_init();

	dmu_objset_register_type(DMU_OST_ZFS, zfs_space_delta_cb);
}

void
zfs_fini(void)
{
	zfsctl_fini();
	zfs_znode_fini();
}

#ifdef HAVE_ZPL
int
zfs_set_version(zfsvfs_t *zfsvfs, uint64_t newvers)
{
	int error;
	objset_t *os = zfsvfs->z_os;
	dmu_tx_t *tx;

	if (newvers < ZPL_VERSION_INITIAL || newvers > ZPL_VERSION)
		return (EINVAL);

	if (newvers < zfsvfs->z_version)
		return (EINVAL);

	if (zfs_spa_version_map(newvers) >
	    spa_version(dmu_objset_spa(zfsvfs->z_os)))
		return (ENOTSUP);

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
		ASSERT3U(error, ==, 0);

		VERIFY(0 == sa_set_sa_object(os, sa_obj));
		sa_register_update_callback(os, zfs_sa_upgrade);
	}

	spa_history_log_internal(LOG_DS_UPGRADE,
	    dmu_objset_spa(os), tx, "oldver=%llu newver=%llu dataset = %llu",
	    zfsvfs->z_version, newvers, dmu_objset_id(os));

	dmu_tx_commit(tx);

	zfsvfs->z_version = newvers;

	if (zfsvfs->z_version >= ZPL_VERSION_FUID)
		zfs_set_fuid_feature(zfsvfs);

	return (0);
}
EXPORT_SYMBOL(zfs_set_version);
#endif /* HAVE_ZPL */

/*
 * Read a property stored within the master node.
 */
int
zfs_get_zplprop(objset_t *os, zfs_prop_t prop, uint64_t *value)
{
	const char *pname;
	int error = ENOENT;

	/*
	 * Look up the file system's value for the property.  For the
	 * version property, we look up a slightly different string.
	 */
	if (prop == ZFS_PROP_VERSION)
		pname = ZPL_VERSION_STR;
	else
		pname = zfs_prop_to_name(prop);

	if (os != NULL)
		error = zap_lookup(os, MASTER_NODE_OBJ, pname, 8, 1, value);

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
		default:
			return (error);
		}
		error = 0;
	}
	return (error);
}
