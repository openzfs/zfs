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

#include <sys/types.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/uio.h>
#include <sys/buf.h>
#include <sys/modctl.h>
#include <sys/open.h>
#include <sys/file.h>
#include <sys/kmem.h>
#include <sys/conf.h>
#include <sys/cmn_err.h>
#include <sys/stat.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_znode.h>
#include <sys/zap.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/vdev.h>
#include <sys/priv_impl.h>
#include <sys/dmu.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_deleg.h>
#include <sys/dmu_objset.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/sunldi.h>
#include <sys/policy.h>
#include <sys/zone.h>
#include <sys/nvpair.h>
#include <sys/pathname.h>
#include <sys/mount.h>
#include <sys/sdt.h>
#include <sys/fs/zfs.h>
#include <sys/zfs_ctldir.h>
#include <sys/zfs_dir.h>
#include <sys/zvol.h>
#include <sys/dsl_scan.h>
#include <sharefs/share.h>
#include <sys/dmu_objset.h>

#include "zfs_namecheck.h"
#include "zfs_prop.h"
#include "zfs_deleg.h"
#include "zfs_comutil.h"

extern struct modlfs zfs_modlfs;

extern void zfs_init(void);
extern void zfs_fini(void);

ldi_ident_t zfs_li = NULL;
dev_info_t *zfs_dip;

typedef int zfs_ioc_func_t(zfs_cmd_t *);
typedef int zfs_secpolicy_func_t(zfs_cmd_t *, cred_t *);

typedef enum {
	NO_NAME,
	POOL_NAME,
	DATASET_NAME
} zfs_ioc_namecheck_t;

typedef struct zfs_ioc_vec {
	zfs_ioc_func_t		*zvec_func;
	zfs_secpolicy_func_t	*zvec_secpolicy;
	zfs_ioc_namecheck_t	zvec_namecheck;
	boolean_t		zvec_his_log;
	boolean_t		zvec_pool_check;
} zfs_ioc_vec_t;

/* This array is indexed by zfs_userquota_prop_t */
static const char *userquota_perms[] = {
	ZFS_DELEG_PERM_USERUSED,
	ZFS_DELEG_PERM_USERQUOTA,
	ZFS_DELEG_PERM_GROUPUSED,
	ZFS_DELEG_PERM_GROUPQUOTA,
};

static int zfs_ioc_userspace_upgrade(zfs_cmd_t *zc);
static int zfs_check_settable(const char *name, nvpair_t *property,
    cred_t *cr);
static int zfs_check_clearable(char *dataset, nvlist_t *props,
    nvlist_t **errors);
static int zfs_fill_zplprops_root(uint64_t, nvlist_t *, nvlist_t *,
    boolean_t *);
int zfs_set_prop_nvlist(const char *, zprop_source_t, nvlist_t *, nvlist_t **);

/* _NOTE(PRINTFLIKE(4)) - this is printf-like, but lint is too whiney */
void
__dprintf(const char *file, const char *func, int line, const char *fmt, ...)
{
	const char *newfile;
	char buf[512];
	va_list adx;

	/*
	 * Get rid of annoying "../common/" prefix to filename.
	 */
	newfile = strrchr(file, '/');
	if (newfile != NULL) {
		newfile = newfile + 1; /* Get rid of leading / */
	} else {
		newfile = file;
	}

	va_start(adx, fmt);
	(void) vsnprintf(buf, sizeof (buf), fmt, adx);
	va_end(adx);

	/*
	 * To get this data, use the zfs-dprintf probe as so:
	 * dtrace -q -n 'zfs-dprintf \
	 *	/stringof(arg0) == "dbuf.c"/ \
	 *	{printf("%s: %s", stringof(arg1), stringof(arg3))}'
	 * arg0 = file name
	 * arg1 = function name
	 * arg2 = line number
	 * arg3 = message
	 */
	DTRACE_PROBE4(zfs__dprintf,
	    char *, newfile, char *, func, int, line, char *, buf);
}

static void
history_str_free(char *buf)
{
	kmem_free(buf, HIS_MAX_RECORD_LEN);
}

static char *
history_str_get(zfs_cmd_t *zc)
{
	char *buf;

	if (zc->zc_history == NULL)
		return (NULL);

	buf = kmem_alloc(HIS_MAX_RECORD_LEN, KM_SLEEP);
	if (copyinstr((void *)(uintptr_t)zc->zc_history,
	    buf, HIS_MAX_RECORD_LEN, NULL) != 0) {
		history_str_free(buf);
		return (NULL);
	}

	buf[HIS_MAX_RECORD_LEN -1] = '\0';

	return (buf);
}

/*
 * Check to see if the named dataset is currently defined as bootable
 */
static boolean_t
zfs_is_bootfs(const char *name)
{
	objset_t *os;

	if (dmu_objset_hold(name, FTAG, &os) == 0) {
		boolean_t ret;
		ret = (dmu_objset_id(os) == spa_bootfs(dmu_objset_spa(os)));
		dmu_objset_rele(os, FTAG);
		return (ret);
	}
	return (B_FALSE);
}

/*
 * zfs_earlier_version
 *
 *	Return non-zero if the spa version is less than requested version.
 */
static int
zfs_earlier_version(const char *name, int version)
{
	spa_t *spa;

	if (spa_open(name, &spa, FTAG) == 0) {
		if (spa_version(spa) < version) {
			spa_close(spa, FTAG);
			return (1);
		}
		spa_close(spa, FTAG);
	}
	return (0);
}

/*
 * zpl_earlier_version
 *
 * Return TRUE if the ZPL version is less than requested version.
 */
static boolean_t
zpl_earlier_version(const char *name, int version)
{
	objset_t *os;
	boolean_t rc = B_TRUE;

	if (dmu_objset_hold(name, FTAG, &os) == 0) {
		uint64_t zplversion;

		if (dmu_objset_type(os) != DMU_OST_ZFS) {
			dmu_objset_rele(os, FTAG);
			return (B_TRUE);
		}
		/* XXX reading from non-owned objset */
		if (zfs_get_zplprop(os, ZFS_PROP_VERSION, &zplversion) == 0)
			rc = zplversion < version;
		dmu_objset_rele(os, FTAG);
	}
	return (rc);
}

static void
zfs_log_history(zfs_cmd_t *zc)
{
	spa_t *spa;
	char *buf;

	if ((buf = history_str_get(zc)) == NULL)
		return;

	if (spa_open(zc->zc_name, &spa, FTAG) == 0) {
		if (spa_version(spa) >= SPA_VERSION_ZPOOL_HISTORY)
			(void) spa_history_log(spa, buf, LOG_CMD_NORMAL);
		spa_close(spa, FTAG);
	}
	history_str_free(buf);
}

/*
 * Policy for top-level read operations (list pools).  Requires no privileges,
 * and can be used in the local zone, as there is no associated dataset.
 */
/* ARGSUSED */
static int
zfs_secpolicy_none(zfs_cmd_t *zc, cred_t *cr)
{
	return (0);
}

/*
 * Policy for dataset read operations (list children, get statistics).  Requires
 * no privileges, but must be visible in the local zone.
 */
/* ARGSUSED */
static int
zfs_secpolicy_read(zfs_cmd_t *zc, cred_t *cr)
{
	if (INGLOBALZONE(curproc) ||
	    zone_dataset_visible(zc->zc_name, NULL))
		return (0);

	return (ENOENT);
}

static int
zfs_dozonecheck(const char *dataset, cred_t *cr)
{
	uint64_t zoned;
	int writable = 1;

	/*
	 * The dataset must be visible by this zone -- check this first
	 * so they don't see EPERM on something they shouldn't know about.
	 */
	if (!INGLOBALZONE(curproc) &&
	    !zone_dataset_visible(dataset, &writable))
		return (ENOENT);

	if (dsl_prop_get_integer(dataset, "zoned", &zoned, NULL))
		return (ENOENT);

	if (INGLOBALZONE(curproc)) {
		/*
		 * If the fs is zoned, only root can access it from the
		 * global zone.
		 */
		if (secpolicy_zfs(cr) && zoned)
			return (EPERM);
	} else {
		/*
		 * If we are in a local zone, the 'zoned' property must be set.
		 */
		if (!zoned)
			return (EPERM);

		/* must be writable by this zone */
		if (!writable)
			return (EPERM);
	}
	return (0);
}

int
zfs_secpolicy_write_perms(const char *name, const char *perm, cred_t *cr)
{
	int error;

	error = zfs_dozonecheck(name, cr);
	if (error == 0) {
		error = secpolicy_zfs(cr);
		if (error)
			error = dsl_deleg_access(name, perm, cr);
	}
	return (error);
}

/*
 * Policy for setting the security label property.
 *
 * Returns 0 for success, non-zero for access and other errors.
 */
static int
zfs_set_slabel_policy(const char *name, char *strval, cred_t *cr)
{
	char		ds_hexsl[MAXNAMELEN];
	bslabel_t	ds_sl, new_sl;
	boolean_t	new_default = FALSE;
	uint64_t	zoned;
	int		needed_priv = -1;
	int		error;

	/* First get the existing dataset label. */
	error = dsl_prop_get(name, zfs_prop_to_name(ZFS_PROP_MLSLABEL),
	    1, sizeof (ds_hexsl), &ds_hexsl, NULL);
	if (error)
		return (EPERM);

	if (strcasecmp(strval, ZFS_MLSLABEL_DEFAULT) == 0)
		new_default = TRUE;

	/* The label must be translatable */
	if (!new_default && (hexstr_to_label(strval, &new_sl) != 0))
		return (EINVAL);

	/*
	 * In a non-global zone, disallow attempts to set a label that
	 * doesn't match that of the zone; otherwise no other checks
	 * are needed.
	 */
	if (!INGLOBALZONE(curproc)) {
		if (new_default || !blequal(&new_sl, CR_SL(CRED())))
			return (EPERM);
		return (0);
	}

	/*
	 * For global-zone datasets (i.e., those whose zoned property is
	 * "off", verify that the specified new label is valid for the
	 * global zone.
	 */
	if (dsl_prop_get_integer(name,
	    zfs_prop_to_name(ZFS_PROP_ZONED), &zoned, NULL))
		return (EPERM);
	if (!zoned) {
		if (zfs_check_global_label(name, strval) != 0)
			return (EPERM);
	}

	/*
	 * If the existing dataset label is nondefault, check if the
	 * dataset is mounted (label cannot be changed while mounted).
	 * Get the zfsvfs; if there isn't one, then the dataset isn't
	 * mounted (or isn't a dataset, doesn't exist, ...).
	 */
	if (strcasecmp(ds_hexsl, ZFS_MLSLABEL_DEFAULT) != 0) {
		objset_t *os;
		static char *setsl_tag = "setsl_tag";

		/*
		 * Try to own the dataset; abort if there is any error,
		 * (e.g., already mounted, in use, or other error).
		 */
		error = dmu_objset_own(name, DMU_OST_ZFS, B_TRUE,
		    setsl_tag, &os);
		if (error)
			return (EPERM);

		dmu_objset_disown(os, setsl_tag);

		if (new_default) {
			needed_priv = PRIV_FILE_DOWNGRADE_SL;
			goto out_check;
		}

		if (hexstr_to_label(strval, &new_sl) != 0)
			return (EPERM);

		if (blstrictdom(&ds_sl, &new_sl))
			needed_priv = PRIV_FILE_DOWNGRADE_SL;
		else if (blstrictdom(&new_sl, &ds_sl))
			needed_priv = PRIV_FILE_UPGRADE_SL;
	} else {
		/* dataset currently has a default label */
		if (!new_default)
			needed_priv = PRIV_FILE_UPGRADE_SL;
	}

out_check:
	if (needed_priv != -1)
		return (PRIV_POLICY(cr, needed_priv, B_FALSE, EPERM, NULL));
	return (0);
}

static int
zfs_secpolicy_setprop(const char *dsname, zfs_prop_t prop, nvpair_t *propval,
    cred_t *cr)
{
	char *strval;

	/*
	 * Check permissions for special properties.
	 */
	switch (prop) {
	case ZFS_PROP_ZONED:
		/*
		 * Disallow setting of 'zoned' from within a local zone.
		 */
		if (!INGLOBALZONE(curproc))
			return (EPERM);
		break;

	case ZFS_PROP_QUOTA:
		if (!INGLOBALZONE(curproc)) {
			uint64_t zoned;
			char setpoint[MAXNAMELEN];
			/*
			 * Unprivileged users are allowed to modify the
			 * quota on things *under* (ie. contained by)
			 * the thing they own.
			 */
			if (dsl_prop_get_integer(dsname, "zoned", &zoned,
			    setpoint))
				return (EPERM);
			if (!zoned || strlen(dsname) <= strlen(setpoint))
				return (EPERM);
		}
		break;

	case ZFS_PROP_MLSLABEL:
		if (!is_system_labeled())
			return (EPERM);

		if (nvpair_value_string(propval, &strval) == 0) {
			int err;

			err = zfs_set_slabel_policy(dsname, strval, CRED());
			if (err != 0)
				return (err);
		}
		break;
	}

	return (zfs_secpolicy_write_perms(dsname, zfs_prop_to_name(prop), cr));
}

int
zfs_secpolicy_fsacl(zfs_cmd_t *zc, cred_t *cr)
{
	int error;

	error = zfs_dozonecheck(zc->zc_name, cr);
	if (error)
		return (error);

	/*
	 * permission to set permissions will be evaluated later in
	 * dsl_deleg_can_allow()
	 */
	return (0);
}

int
zfs_secpolicy_rollback(zfs_cmd_t *zc, cred_t *cr)
{
	return (zfs_secpolicy_write_perms(zc->zc_name,
	    ZFS_DELEG_PERM_ROLLBACK, cr));
}

int
zfs_secpolicy_send(zfs_cmd_t *zc, cred_t *cr)
{
	return (zfs_secpolicy_write_perms(zc->zc_name,
	    ZFS_DELEG_PERM_SEND, cr));
}

static int
zfs_secpolicy_deleg_share(zfs_cmd_t *zc, cred_t *cr)
{
	vnode_t *vp;
	int error;

	if ((error = lookupname(zc->zc_value, UIO_SYSSPACE,
	    NO_FOLLOW, NULL, &vp)) != 0)
		return (error);

	/* Now make sure mntpnt and dataset are ZFS */

	if (vp->v_vfsp->vfs_fstype != zfsfstype ||
	    (strcmp((char *)refstr_value(vp->v_vfsp->vfs_resource),
	    zc->zc_name) != 0)) {
		VN_RELE(vp);
		return (EPERM);
	}

	VN_RELE(vp);
	return (dsl_deleg_access(zc->zc_name,
	    ZFS_DELEG_PERM_SHARE, cr));
}

int
zfs_secpolicy_share(zfs_cmd_t *zc, cred_t *cr)
{
	if (!INGLOBALZONE(curproc))
		return (EPERM);

	if (secpolicy_nfs(cr) == 0) {
		return (0);
	} else {
		return (zfs_secpolicy_deleg_share(zc, cr));
	}
}

int
zfs_secpolicy_smb_acl(zfs_cmd_t *zc, cred_t *cr)
{
	if (!INGLOBALZONE(curproc))
		return (EPERM);

	if (secpolicy_smb(cr) == 0) {
		return (0);
	} else {
		return (zfs_secpolicy_deleg_share(zc, cr));
	}
}

static int
zfs_get_parent(const char *datasetname, char *parent, int parentsize)
{
	char *cp;

	/*
	 * Remove the @bla or /bla from the end of the name to get the parent.
	 */
	(void) strncpy(parent, datasetname, parentsize);
	cp = strrchr(parent, '@');
	if (cp != NULL) {
		cp[0] = '\0';
	} else {
		cp = strrchr(parent, '/');
		if (cp == NULL)
			return (ENOENT);
		cp[0] = '\0';
	}

	return (0);
}

int
zfs_secpolicy_destroy_perms(const char *name, cred_t *cr)
{
	int error;

	if ((error = zfs_secpolicy_write_perms(name,
	    ZFS_DELEG_PERM_MOUNT, cr)) != 0)
		return (error);

	return (zfs_secpolicy_write_perms(name, ZFS_DELEG_PERM_DESTROY, cr));
}

static int
zfs_secpolicy_destroy(zfs_cmd_t *zc, cred_t *cr)
{
	return (zfs_secpolicy_destroy_perms(zc->zc_name, cr));
}

/*
 * Destroying snapshots with delegated permissions requires
 * descendent mount and destroy permissions.
 * Reassemble the full filesystem@snap name so dsl_deleg_access()
 * can do the correct permission check.
 *
 * Since this routine is used when doing a recursive destroy of snapshots
 * and destroying snapshots requires descendent permissions, a successfull
 * check of the top level snapshot applies to snapshots of all descendent
 * datasets as well.
 */
static int
zfs_secpolicy_destroy_snaps(zfs_cmd_t *zc, cred_t *cr)
{
	int error;
	char *dsname;

	dsname = kmem_asprintf("%s@%s", zc->zc_name, zc->zc_value);

	error = zfs_secpolicy_destroy_perms(dsname, cr);

	strfree(dsname);
	return (error);
}

int
zfs_secpolicy_rename_perms(const char *from, const char *to, cred_t *cr)
{
	char	parentname[MAXNAMELEN];
	int	error;

	if ((error = zfs_secpolicy_write_perms(from,
	    ZFS_DELEG_PERM_RENAME, cr)) != 0)
		return (error);

	if ((error = zfs_secpolicy_write_perms(from,
	    ZFS_DELEG_PERM_MOUNT, cr)) != 0)
		return (error);

	if ((error = zfs_get_parent(to, parentname,
	    sizeof (parentname))) != 0)
		return (error);

	if ((error = zfs_secpolicy_write_perms(parentname,
	    ZFS_DELEG_PERM_CREATE, cr)) != 0)
		return (error);

	if ((error = zfs_secpolicy_write_perms(parentname,
	    ZFS_DELEG_PERM_MOUNT, cr)) != 0)
		return (error);

	return (error);
}

static int
zfs_secpolicy_rename(zfs_cmd_t *zc, cred_t *cr)
{
	return (zfs_secpolicy_rename_perms(zc->zc_name, zc->zc_value, cr));
}

static int
zfs_secpolicy_promote(zfs_cmd_t *zc, cred_t *cr)
{
	char	parentname[MAXNAMELEN];
	objset_t *clone;
	int error;

	error = zfs_secpolicy_write_perms(zc->zc_name,
	    ZFS_DELEG_PERM_PROMOTE, cr);
	if (error)
		return (error);

	error = dmu_objset_hold(zc->zc_name, FTAG, &clone);

	if (error == 0) {
		dsl_dataset_t *pclone = NULL;
		dsl_dir_t *dd;
		dd = clone->os_dsl_dataset->ds_dir;

		rw_enter(&dd->dd_pool->dp_config_rwlock, RW_READER);
		error = dsl_dataset_hold_obj(dd->dd_pool,
		    dd->dd_phys->dd_origin_obj, FTAG, &pclone);
		rw_exit(&dd->dd_pool->dp_config_rwlock);
		if (error) {
			dmu_objset_rele(clone, FTAG);
			return (error);
		}

		error = zfs_secpolicy_write_perms(zc->zc_name,
		    ZFS_DELEG_PERM_MOUNT, cr);

		dsl_dataset_name(pclone, parentname);
		dmu_objset_rele(clone, FTAG);
		dsl_dataset_rele(pclone, FTAG);
		if (error == 0)
			error = zfs_secpolicy_write_perms(parentname,
			    ZFS_DELEG_PERM_PROMOTE, cr);
	}
	return (error);
}

static int
zfs_secpolicy_receive(zfs_cmd_t *zc, cred_t *cr)
{
	int error;

	if ((error = zfs_secpolicy_write_perms(zc->zc_name,
	    ZFS_DELEG_PERM_RECEIVE, cr)) != 0)
		return (error);

	if ((error = zfs_secpolicy_write_perms(zc->zc_name,
	    ZFS_DELEG_PERM_MOUNT, cr)) != 0)
		return (error);

	return (zfs_secpolicy_write_perms(zc->zc_name,
	    ZFS_DELEG_PERM_CREATE, cr));
}

int
zfs_secpolicy_snapshot_perms(const char *name, cred_t *cr)
{
	return (zfs_secpolicy_write_perms(name,
	    ZFS_DELEG_PERM_SNAPSHOT, cr));
}

static int
zfs_secpolicy_snapshot(zfs_cmd_t *zc, cred_t *cr)
{

	return (zfs_secpolicy_snapshot_perms(zc->zc_name, cr));
}

static int
zfs_secpolicy_create(zfs_cmd_t *zc, cred_t *cr)
{
	char	parentname[MAXNAMELEN];
	int	error;

	if ((error = zfs_get_parent(zc->zc_name, parentname,
	    sizeof (parentname))) != 0)
		return (error);

	if (zc->zc_value[0] != '\0') {
		if ((error = zfs_secpolicy_write_perms(zc->zc_value,
		    ZFS_DELEG_PERM_CLONE, cr)) != 0)
			return (error);
	}

	if ((error = zfs_secpolicy_write_perms(parentname,
	    ZFS_DELEG_PERM_CREATE, cr)) != 0)
		return (error);

	error = zfs_secpolicy_write_perms(parentname,
	    ZFS_DELEG_PERM_MOUNT, cr);

	return (error);
}

static int
zfs_secpolicy_umount(zfs_cmd_t *zc, cred_t *cr)
{
	int error;

	error = secpolicy_fs_unmount(cr, NULL);
	if (error) {
		error = dsl_deleg_access(zc->zc_name, ZFS_DELEG_PERM_MOUNT, cr);
	}
	return (error);
}

/*
 * Policy for pool operations - create/destroy pools, add vdevs, etc.  Requires
 * SYS_CONFIG privilege, which is not available in a local zone.
 */
/* ARGSUSED */
static int
zfs_secpolicy_config(zfs_cmd_t *zc, cred_t *cr)
{
	if (secpolicy_sys_config(cr, B_FALSE) != 0)
		return (EPERM);

	return (0);
}

/*
 * Policy for fault injection.  Requires all privileges.
 */
/* ARGSUSED */
static int
zfs_secpolicy_inject(zfs_cmd_t *zc, cred_t *cr)
{
	return (secpolicy_zinject(cr));
}

static int
zfs_secpolicy_inherit(zfs_cmd_t *zc, cred_t *cr)
{
	zfs_prop_t prop = zfs_name_to_prop(zc->zc_value);

	if (prop == ZPROP_INVAL) {
		if (!zfs_prop_user(zc->zc_value))
			return (EINVAL);
		return (zfs_secpolicy_write_perms(zc->zc_name,
		    ZFS_DELEG_PERM_USERPROP, cr));
	} else {
		return (zfs_secpolicy_setprop(zc->zc_name, prop,
		    NULL, cr));
	}
}

static int
zfs_secpolicy_userspace_one(zfs_cmd_t *zc, cred_t *cr)
{
	int err = zfs_secpolicy_read(zc, cr);
	if (err)
		return (err);

	if (zc->zc_objset_type >= ZFS_NUM_USERQUOTA_PROPS)
		return (EINVAL);

	if (zc->zc_value[0] == 0) {
		/*
		 * They are asking about a posix uid/gid.  If it's
		 * themself, allow it.
		 */
		if (zc->zc_objset_type == ZFS_PROP_USERUSED ||
		    zc->zc_objset_type == ZFS_PROP_USERQUOTA) {
			if (zc->zc_guid == crgetuid(cr))
				return (0);
		} else {
			if (groupmember(zc->zc_guid, cr))
				return (0);
		}
	}

	return (zfs_secpolicy_write_perms(zc->zc_name,
	    userquota_perms[zc->zc_objset_type], cr));
}

static int
zfs_secpolicy_userspace_many(zfs_cmd_t *zc, cred_t *cr)
{
	int err = zfs_secpolicy_read(zc, cr);
	if (err)
		return (err);

	if (zc->zc_objset_type >= ZFS_NUM_USERQUOTA_PROPS)
		return (EINVAL);

	return (zfs_secpolicy_write_perms(zc->zc_name,
	    userquota_perms[zc->zc_objset_type], cr));
}

static int
zfs_secpolicy_userspace_upgrade(zfs_cmd_t *zc, cred_t *cr)
{
	return (zfs_secpolicy_setprop(zc->zc_name, ZFS_PROP_VERSION,
	    NULL, cr));
}

static int
zfs_secpolicy_hold(zfs_cmd_t *zc, cred_t *cr)
{
	return (zfs_secpolicy_write_perms(zc->zc_name,
	    ZFS_DELEG_PERM_HOLD, cr));
}

static int
zfs_secpolicy_release(zfs_cmd_t *zc, cred_t *cr)
{
	return (zfs_secpolicy_write_perms(zc->zc_name,
	    ZFS_DELEG_PERM_RELEASE, cr));
}

/*
 * Returns the nvlist as specified by the user in the zfs_cmd_t.
 */
static int
get_nvlist(uint64_t nvl, uint64_t size, int iflag, nvlist_t **nvp)
{
	char *packed;
	int error;
	nvlist_t *list = NULL;

	/*
	 * Read in and unpack the user-supplied nvlist.
	 */
	if (size == 0)
		return (EINVAL);

	packed = kmem_alloc(size, KM_SLEEP);

	if ((error = ddi_copyin((void *)(uintptr_t)nvl, packed, size,
	    iflag)) != 0) {
		kmem_free(packed, size);
		return (error);
	}

	if ((error = nvlist_unpack(packed, size, &list, 0)) != 0) {
		kmem_free(packed, size);
		return (error);
	}

	kmem_free(packed, size);

	*nvp = list;
	return (0);
}

static int
fit_error_list(zfs_cmd_t *zc, nvlist_t **errors)
{
	size_t size;

	VERIFY(nvlist_size(*errors, &size, NV_ENCODE_NATIVE) == 0);

	if (size > zc->zc_nvlist_dst_size) {
		nvpair_t *more_errors;
		int n = 0;

		if (zc->zc_nvlist_dst_size < 1024)
			return (ENOMEM);

		VERIFY(nvlist_add_int32(*errors, ZPROP_N_MORE_ERRORS, 0) == 0);
		more_errors = nvlist_prev_nvpair(*errors, NULL);

		do {
			nvpair_t *pair = nvlist_prev_nvpair(*errors,
			    more_errors);
			VERIFY(nvlist_remove_nvpair(*errors, pair) == 0);
			n++;
			VERIFY(nvlist_size(*errors, &size,
			    NV_ENCODE_NATIVE) == 0);
		} while (size > zc->zc_nvlist_dst_size);

		VERIFY(nvlist_remove_nvpair(*errors, more_errors) == 0);
		VERIFY(nvlist_add_int32(*errors, ZPROP_N_MORE_ERRORS, n) == 0);
		ASSERT(nvlist_size(*errors, &size, NV_ENCODE_NATIVE) == 0);
		ASSERT(size <= zc->zc_nvlist_dst_size);
	}

	return (0);
}

static int
put_nvlist(zfs_cmd_t *zc, nvlist_t *nvl)
{
	char *packed = NULL;
	int error = 0;
	size_t size;

	VERIFY(nvlist_size(nvl, &size, NV_ENCODE_NATIVE) == 0);

	if (size > zc->zc_nvlist_dst_size) {
		error = ENOMEM;
	} else {
		packed = kmem_alloc(size, KM_SLEEP);
		VERIFY(nvlist_pack(nvl, &packed, &size, NV_ENCODE_NATIVE,
		    KM_SLEEP) == 0);
		if (ddi_copyout(packed, (void *)(uintptr_t)zc->zc_nvlist_dst,
		    size, zc->zc_iflags) != 0)
			error = EFAULT;
		kmem_free(packed, size);
	}

	zc->zc_nvlist_dst_size = size;
	return (error);
}

static int
getzfsvfs(const char *dsname, zfsvfs_t **zfvp)
{
	objset_t *os;
	int error;

	error = dmu_objset_hold(dsname, FTAG, &os);
	if (error)
		return (error);
	if (dmu_objset_type(os) != DMU_OST_ZFS) {
		dmu_objset_rele(os, FTAG);
		return (EINVAL);
	}

	mutex_enter(&os->os_user_ptr_lock);
	*zfvp = dmu_objset_get_user(os);
	if (*zfvp) {
		VFS_HOLD((*zfvp)->z_vfs);
	} else {
		error = ESRCH;
	}
	mutex_exit(&os->os_user_ptr_lock);
	dmu_objset_rele(os, FTAG);
	return (error);
}

/*
 * Find a zfsvfs_t for a mounted filesystem, or create our own, in which
 * case its z_vfs will be NULL, and it will be opened as the owner.
 */
static int
zfsvfs_hold(const char *name, void *tag, zfsvfs_t **zfvp)
{
	int error = 0;

	if (getzfsvfs(name, zfvp) != 0)
		error = zfsvfs_create(name, zfvp);
	if (error == 0) {
		rrw_enter(&(*zfvp)->z_teardown_lock, RW_READER, tag);
		if ((*zfvp)->z_unmounted) {
			/*
			 * XXX we could probably try again, since the unmounting
			 * thread should be just about to disassociate the
			 * objset from the zfsvfs.
			 */
			rrw_exit(&(*zfvp)->z_teardown_lock, tag);
			return (EBUSY);
		}
	}
	return (error);
}

static void
zfsvfs_rele(zfsvfs_t *zfsvfs, void *tag)
{
	rrw_exit(&zfsvfs->z_teardown_lock, tag);

	if (zfsvfs->z_vfs) {
		VFS_RELE(zfsvfs->z_vfs);
	} else {
		dmu_objset_disown(zfsvfs->z_os, zfsvfs);
		zfsvfs_free(zfsvfs);
	}
}

static int
zfs_ioc_pool_create(zfs_cmd_t *zc)
{
	int error;
	nvlist_t *config, *props = NULL;
	nvlist_t *rootprops = NULL;
	nvlist_t *zplprops = NULL;
	char *buf;

	if (error = get_nvlist(zc->zc_nvlist_conf, zc->zc_nvlist_conf_size,
	    zc->zc_iflags, &config))
		return (error);

	if (zc->zc_nvlist_src_size != 0 && (error =
	    get_nvlist(zc->zc_nvlist_src, zc->zc_nvlist_src_size,
	    zc->zc_iflags, &props))) {
		nvlist_free(config);
		return (error);
	}

	if (props) {
		nvlist_t *nvl = NULL;
		uint64_t version = SPA_VERSION;

		(void) nvlist_lookup_uint64(props,
		    zpool_prop_to_name(ZPOOL_PROP_VERSION), &version);
		if (version < SPA_VERSION_INITIAL || version > SPA_VERSION) {
			error = EINVAL;
			goto pool_props_bad;
		}
		(void) nvlist_lookup_nvlist(props, ZPOOL_ROOTFS_PROPS, &nvl);
		if (nvl) {
			error = nvlist_dup(nvl, &rootprops, KM_SLEEP);
			if (error != 0) {
				nvlist_free(config);
				nvlist_free(props);
				return (error);
			}
			(void) nvlist_remove_all(props, ZPOOL_ROOTFS_PROPS);
		}
		VERIFY(nvlist_alloc(&zplprops, NV_UNIQUE_NAME, KM_SLEEP) == 0);
		error = zfs_fill_zplprops_root(version, rootprops,
		    zplprops, NULL);
		if (error)
			goto pool_props_bad;
	}

	buf = history_str_get(zc);

	error = spa_create(zc->zc_name, config, props, buf, zplprops);

	/*
	 * Set the remaining root properties
	 */
	if (!error && (error = zfs_set_prop_nvlist(zc->zc_name,
	    ZPROP_SRC_LOCAL, rootprops, NULL)) != 0)
		(void) spa_destroy(zc->zc_name);

	if (buf != NULL)
		history_str_free(buf);

pool_props_bad:
	nvlist_free(rootprops);
	nvlist_free(zplprops);
	nvlist_free(config);
	nvlist_free(props);

	return (error);
}

static int
zfs_ioc_pool_destroy(zfs_cmd_t *zc)
{
	int error;
	zfs_log_history(zc);
	error = spa_destroy(zc->zc_name);
	if (error == 0)
		zvol_remove_minors(zc->zc_name);
	return (error);
}

static int
zfs_ioc_pool_import(zfs_cmd_t *zc)
{
	nvlist_t *config, *props = NULL;
	uint64_t guid;
	int error;

	if ((error = get_nvlist(zc->zc_nvlist_conf, zc->zc_nvlist_conf_size,
	    zc->zc_iflags, &config)) != 0)
		return (error);

	if (zc->zc_nvlist_src_size != 0 && (error =
	    get_nvlist(zc->zc_nvlist_src, zc->zc_nvlist_src_size,
	    zc->zc_iflags, &props))) {
		nvlist_free(config);
		return (error);
	}

	if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID, &guid) != 0 ||
	    guid != zc->zc_guid)
		error = EINVAL;
	else if (zc->zc_cookie)
		error = spa_import_verbatim(zc->zc_name, config, props);
	else
		error = spa_import(zc->zc_name, config, props);

	if (zc->zc_nvlist_dst != 0)
		(void) put_nvlist(zc, config);

	nvlist_free(config);

	if (props)
		nvlist_free(props);

	return (error);
}

static int
zfs_ioc_pool_export(zfs_cmd_t *zc)
{
	int error;
	boolean_t force = (boolean_t)zc->zc_cookie;
	boolean_t hardforce = (boolean_t)zc->zc_guid;

	zfs_log_history(zc);
	error = spa_export(zc->zc_name, NULL, force, hardforce);
	if (error == 0)
		zvol_remove_minors(zc->zc_name);
	return (error);
}

static int
zfs_ioc_pool_configs(zfs_cmd_t *zc)
{
	nvlist_t *configs;
	int error;

	if ((configs = spa_all_configs(&zc->zc_cookie)) == NULL)
		return (EEXIST);

	error = put_nvlist(zc, configs);

	nvlist_free(configs);

	return (error);
}

static int
zfs_ioc_pool_stats(zfs_cmd_t *zc)
{
	nvlist_t *config;
	int error;
	int ret = 0;

	error = spa_get_stats(zc->zc_name, &config, zc->zc_value,
	    sizeof (zc->zc_value));

	if (config != NULL) {
		ret = put_nvlist(zc, config);
		nvlist_free(config);

		/*
		 * The config may be present even if 'error' is non-zero.
		 * In this case we return success, and preserve the real errno
		 * in 'zc_cookie'.
		 */
		zc->zc_cookie = error;
	} else {
		ret = error;
	}

	return (ret);
}

/*
 * Try to import the given pool, returning pool stats as appropriate so that
 * user land knows which devices are available and overall pool health.
 */
static int
zfs_ioc_pool_tryimport(zfs_cmd_t *zc)
{
	nvlist_t *tryconfig, *config;
	int error;

	if ((error = get_nvlist(zc->zc_nvlist_conf, zc->zc_nvlist_conf_size,
	    zc->zc_iflags, &tryconfig)) != 0)
		return (error);

	config = spa_tryimport(tryconfig);

	nvlist_free(tryconfig);

	if (config == NULL)
		return (EINVAL);

	error = put_nvlist(zc, config);
	nvlist_free(config);

	return (error);
}

/*
 * inputs:
 * zc_name              name of the pool
 * zc_cookie            scan func (pool_scan_func_t)
 */
static int
zfs_ioc_pool_scan(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0)
		return (error);

	if (zc->zc_cookie == POOL_SCAN_NONE)
		error = spa_scan_stop(spa);
	else
		error = spa_scan(spa, zc->zc_cookie);

	spa_close(spa, FTAG);

	return (error);
}

static int
zfs_ioc_pool_freeze(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;

	error = spa_open(zc->zc_name, &spa, FTAG);
	if (error == 0) {
		spa_freeze(spa);
		spa_close(spa, FTAG);
	}
	return (error);
}

static int
zfs_ioc_pool_upgrade(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0)
		return (error);

	if (zc->zc_cookie < spa_version(spa) || zc->zc_cookie > SPA_VERSION) {
		spa_close(spa, FTAG);
		return (EINVAL);
	}

	spa_upgrade(spa, zc->zc_cookie);
	spa_close(spa, FTAG);

	return (error);
}

static int
zfs_ioc_pool_get_history(zfs_cmd_t *zc)
{
	spa_t *spa;
	char *hist_buf;
	uint64_t size;
	int error;

	if ((size = zc->zc_history_len) == 0)
		return (EINVAL);

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0)
		return (error);

	if (spa_version(spa) < SPA_VERSION_ZPOOL_HISTORY) {
		spa_close(spa, FTAG);
		return (ENOTSUP);
	}

	hist_buf = kmem_alloc(size, KM_SLEEP);
	if ((error = spa_history_get(spa, &zc->zc_history_offset,
	    &zc->zc_history_len, hist_buf)) == 0) {
		error = ddi_copyout(hist_buf,
		    (void *)(uintptr_t)zc->zc_history,
		    zc->zc_history_len, zc->zc_iflags);
	}

	spa_close(spa, FTAG);
	kmem_free(hist_buf, size);
	return (error);
}

static int
zfs_ioc_dsobj_to_dsname(zfs_cmd_t *zc)
{
	int error;

	if (error = dsl_dsobj_to_dsname(zc->zc_name, zc->zc_obj, zc->zc_value))
		return (error);

	return (0);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_obj		object to find
 *
 * outputs:
 * zc_value		name of object
 */
static int
zfs_ioc_obj_to_path(zfs_cmd_t *zc)
{
	objset_t *os;
	int error;

	/* XXX reading from objset not owned */
	if ((error = dmu_objset_hold(zc->zc_name, FTAG, &os)) != 0)
		return (error);
	if (dmu_objset_type(os) != DMU_OST_ZFS) {
		dmu_objset_rele(os, FTAG);
		return (EINVAL);
	}
	error = zfs_obj_to_path(os, zc->zc_obj, zc->zc_value,
	    sizeof (zc->zc_value));
	dmu_objset_rele(os, FTAG);

	return (error);
}

static int
zfs_ioc_vdev_add(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;
	nvlist_t *config, **l2cache, **spares;
	uint_t nl2cache = 0, nspares = 0;

	error = spa_open(zc->zc_name, &spa, FTAG);
	if (error != 0)
		return (error);

	error = get_nvlist(zc->zc_nvlist_conf, zc->zc_nvlist_conf_size,
	    zc->zc_iflags, &config);
	(void) nvlist_lookup_nvlist_array(config, ZPOOL_CONFIG_L2CACHE,
	    &l2cache, &nl2cache);

	(void) nvlist_lookup_nvlist_array(config, ZPOOL_CONFIG_SPARES,
	    &spares, &nspares);

	/*
	 * A root pool with concatenated devices is not supported.
	 * Thus, can not add a device to a root pool.
	 *
	 * Intent log device can not be added to a rootpool because
	 * during mountroot, zil is replayed, a seperated log device
	 * can not be accessed during the mountroot time.
	 *
	 * l2cache and spare devices are ok to be added to a rootpool.
	 */
	if (spa_bootfs(spa) != 0 && nl2cache == 0 && nspares == 0) {
		nvlist_free(config);
		spa_close(spa, FTAG);
		return (EDOM);
	}

	if (error == 0) {
		error = spa_vdev_add(spa, config);
		nvlist_free(config);
	}
	spa_close(spa, FTAG);
	return (error);
}

/*
 * inputs:
 * zc_name		name of the pool
 * zc_nvlist_conf	nvlist of devices to remove
 * zc_cookie		to stop the remove?
 */
static int
zfs_ioc_vdev_remove(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;

	error = spa_open(zc->zc_name, &spa, FTAG);
	if (error != 0)
		return (error);
	error = spa_vdev_remove(spa, zc->zc_guid, B_FALSE);
	spa_close(spa, FTAG);
	return (error);
}

static int
zfs_ioc_vdev_set_state(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;
	vdev_state_t newstate = VDEV_STATE_UNKNOWN;

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0)
		return (error);
	switch (zc->zc_cookie) {
	case VDEV_STATE_ONLINE:
		error = vdev_online(spa, zc->zc_guid, zc->zc_obj, &newstate);
		break;

	case VDEV_STATE_OFFLINE:
		error = vdev_offline(spa, zc->zc_guid, zc->zc_obj);
		break;

	case VDEV_STATE_FAULTED:
		if (zc->zc_obj != VDEV_AUX_ERR_EXCEEDED &&
		    zc->zc_obj != VDEV_AUX_EXTERNAL)
			zc->zc_obj = VDEV_AUX_ERR_EXCEEDED;

		error = vdev_fault(spa, zc->zc_guid, zc->zc_obj);
		break;

	case VDEV_STATE_DEGRADED:
		if (zc->zc_obj != VDEV_AUX_ERR_EXCEEDED &&
		    zc->zc_obj != VDEV_AUX_EXTERNAL)
			zc->zc_obj = VDEV_AUX_ERR_EXCEEDED;

		error = vdev_degrade(spa, zc->zc_guid, zc->zc_obj);
		break;

	default:
		error = EINVAL;
	}
	zc->zc_cookie = newstate;
	spa_close(spa, FTAG);
	return (error);
}

static int
zfs_ioc_vdev_attach(zfs_cmd_t *zc)
{
	spa_t *spa;
	int replacing = zc->zc_cookie;
	nvlist_t *config;
	int error;

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0)
		return (error);

	if ((error = get_nvlist(zc->zc_nvlist_conf, zc->zc_nvlist_conf_size,
	    zc->zc_iflags, &config)) == 0) {
		error = spa_vdev_attach(spa, zc->zc_guid, config, replacing);
		nvlist_free(config);
	}

	spa_close(spa, FTAG);
	return (error);
}

static int
zfs_ioc_vdev_detach(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0)
		return (error);

	error = spa_vdev_detach(spa, zc->zc_guid, 0, B_FALSE);

	spa_close(spa, FTAG);
	return (error);
}

static int
zfs_ioc_vdev_split(zfs_cmd_t *zc)
{
	spa_t *spa;
	nvlist_t *config, *props = NULL;
	int error;
	boolean_t exp = !!(zc->zc_cookie & ZPOOL_EXPORT_AFTER_SPLIT);

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0)
		return (error);

	if (error = get_nvlist(zc->zc_nvlist_conf, zc->zc_nvlist_conf_size,
	    zc->zc_iflags, &config)) {
		spa_close(spa, FTAG);
		return (error);
	}

	if (zc->zc_nvlist_src_size != 0 && (error =
	    get_nvlist(zc->zc_nvlist_src, zc->zc_nvlist_src_size,
	    zc->zc_iflags, &props))) {
		spa_close(spa, FTAG);
		nvlist_free(config);
		return (error);
	}

	error = spa_vdev_split_mirror(spa, zc->zc_string, config, props, exp);

	spa_close(spa, FTAG);

	nvlist_free(config);
	nvlist_free(props);

	return (error);
}

static int
zfs_ioc_vdev_setpath(zfs_cmd_t *zc)
{
	spa_t *spa;
	char *path = zc->zc_value;
	uint64_t guid = zc->zc_guid;
	int error;

	error = spa_open(zc->zc_name, &spa, FTAG);
	if (error != 0)
		return (error);

	error = spa_vdev_setpath(spa, guid, path);
	spa_close(spa, FTAG);
	return (error);
}

static int
zfs_ioc_vdev_setfru(zfs_cmd_t *zc)
{
	spa_t *spa;
	char *fru = zc->zc_value;
	uint64_t guid = zc->zc_guid;
	int error;

	error = spa_open(zc->zc_name, &spa, FTAG);
	if (error != 0)
		return (error);

	error = spa_vdev_setfru(spa, guid, fru);
	spa_close(spa, FTAG);
	return (error);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_nvlist_dst_size	size of buffer for property nvlist
 *
 * outputs:
 * zc_objset_stats	stats
 * zc_nvlist_dst	property nvlist
 * zc_nvlist_dst_size	size of property nvlist
 */
static int
zfs_ioc_objset_stats(zfs_cmd_t *zc)
{
	objset_t *os = NULL;
	int error;
	nvlist_t *nv;

	if (error = dmu_objset_hold(zc->zc_name, FTAG, &os))
		return (error);

	dmu_objset_fast_stat(os, &zc->zc_objset_stats);

	if (zc->zc_nvlist_dst != 0 &&
	    (error = dsl_prop_get_all(os, &nv)) == 0) {
		dmu_objset_stats(os, nv);
		/*
		 * NB: zvol_get_stats() will read the objset contents,
		 * which we aren't supposed to do with a
		 * DS_MODE_USER hold, because it could be
		 * inconsistent.  So this is a bit of a workaround...
		 * XXX reading with out owning
		 */
		if (!zc->zc_objset_stats.dds_inconsistent) {
			if (dmu_objset_type(os) == DMU_OST_ZVOL)
				VERIFY(zvol_get_stats(os, nv) == 0);
		}
		error = put_nvlist(zc, nv);
		nvlist_free(nv);
	}

	dmu_objset_rele(os, FTAG);
	return (error);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_nvlist_dst_size	size of buffer for property nvlist
 *
 * outputs:
 * zc_nvlist_dst	received property nvlist
 * zc_nvlist_dst_size	size of received property nvlist
 *
 * Gets received properties (distinct from local properties on or after
 * SPA_VERSION_RECVD_PROPS) for callers who want to differentiate received from
 * local property values.
 */
static int
zfs_ioc_objset_recvd_props(zfs_cmd_t *zc)
{
	objset_t *os = NULL;
	int error;
	nvlist_t *nv;

	if (error = dmu_objset_hold(zc->zc_name, FTAG, &os))
		return (error);

	/*
	 * Without this check, we would return local property values if the
	 * caller has not already received properties on or after
	 * SPA_VERSION_RECVD_PROPS.
	 */
	if (!dsl_prop_get_hasrecvd(os)) {
		dmu_objset_rele(os, FTAG);
		return (ENOTSUP);
	}

	if (zc->zc_nvlist_dst != 0 &&
	    (error = dsl_prop_get_received(os, &nv)) == 0) {
		error = put_nvlist(zc, nv);
		nvlist_free(nv);
	}

	dmu_objset_rele(os, FTAG);
	return (error);
}

static int
nvl_add_zplprop(objset_t *os, nvlist_t *props, zfs_prop_t prop)
{
	uint64_t value;
	int error;

	/*
	 * zfs_get_zplprop() will either find a value or give us
	 * the default value (if there is one).
	 */
	if ((error = zfs_get_zplprop(os, prop, &value)) != 0)
		return (error);
	VERIFY(nvlist_add_uint64(props, zfs_prop_to_name(prop), value) == 0);
	return (0);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_nvlist_dst_size	size of buffer for zpl property nvlist
 *
 * outputs:
 * zc_nvlist_dst	zpl property nvlist
 * zc_nvlist_dst_size	size of zpl property nvlist
 */
static int
zfs_ioc_objset_zplprops(zfs_cmd_t *zc)
{
	objset_t *os;
	int err;

	/* XXX reading without owning */
	if (err = dmu_objset_hold(zc->zc_name, FTAG, &os))
		return (err);

	dmu_objset_fast_stat(os, &zc->zc_objset_stats);

	/*
	 * NB: nvl_add_zplprop() will read the objset contents,
	 * which we aren't supposed to do with a DS_MODE_USER
	 * hold, because it could be inconsistent.
	 */
	if (zc->zc_nvlist_dst != NULL &&
	    !zc->zc_objset_stats.dds_inconsistent &&
	    dmu_objset_type(os) == DMU_OST_ZFS) {
		nvlist_t *nv;

		VERIFY(nvlist_alloc(&nv, NV_UNIQUE_NAME, KM_SLEEP) == 0);
		if ((err = nvl_add_zplprop(os, nv, ZFS_PROP_VERSION)) == 0 &&
		    (err = nvl_add_zplprop(os, nv, ZFS_PROP_NORMALIZE)) == 0 &&
		    (err = nvl_add_zplprop(os, nv, ZFS_PROP_UTF8ONLY)) == 0 &&
		    (err = nvl_add_zplprop(os, nv, ZFS_PROP_CASE)) == 0)
			err = put_nvlist(zc, nv);
		nvlist_free(nv);
	} else {
		err = ENOENT;
	}
	dmu_objset_rele(os, FTAG);
	return (err);
}

static boolean_t
dataset_name_hidden(const char *name)
{
	/*
	 * Skip over datasets that are not visible in this zone,
	 * internal datasets (which have a $ in their name), and
	 * temporary datasets (which have a % in their name).
	 */
	if (strchr(name, '$') != NULL)
		return (B_TRUE);
	if (strchr(name, '%') != NULL)
		return (B_TRUE);
	if (!INGLOBALZONE(curproc) && !zone_dataset_visible(name, NULL))
		return (B_TRUE);
	return (B_FALSE);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_cookie		zap cursor
 * zc_nvlist_dst_size	size of buffer for property nvlist
 *
 * outputs:
 * zc_name		name of next filesystem
 * zc_cookie		zap cursor
 * zc_objset_stats	stats
 * zc_nvlist_dst	property nvlist
 * zc_nvlist_dst_size	size of property nvlist
 */
static int
zfs_ioc_dataset_list_next(zfs_cmd_t *zc)
{
	objset_t *os;
	int error;
	char *p;
	size_t orig_len = strlen(zc->zc_name);

top:
	if (error = dmu_objset_hold(zc->zc_name, FTAG, &os)) {
		if (error == ENOENT)
			error = ESRCH;
		return (error);
	}

	p = strrchr(zc->zc_name, '/');
	if (p == NULL || p[1] != '\0')
		(void) strlcat(zc->zc_name, "/", sizeof (zc->zc_name));
	p = zc->zc_name + strlen(zc->zc_name);

	/*
	 * Pre-fetch the datasets.  dmu_objset_prefetch() always returns 0
	 * but is not declared void because its called by dmu_objset_find().
	 */
	if (zc->zc_cookie == 0) {
		uint64_t cookie = 0;
		int len = sizeof (zc->zc_name) - (p - zc->zc_name);

		while (dmu_dir_list_next(os, len, p, NULL, &cookie) == 0)
			(void) dmu_objset_prefetch(p, NULL);
	}

	do {
		error = dmu_dir_list_next(os,
		    sizeof (zc->zc_name) - (p - zc->zc_name), p,
		    NULL, &zc->zc_cookie);
		if (error == ENOENT)
			error = ESRCH;
	} while (error == 0 && dataset_name_hidden(zc->zc_name) &&
	    !(zc->zc_iflags & FKIOCTL));
	dmu_objset_rele(os, FTAG);

	/*
	 * If it's an internal dataset (ie. with a '$' in its name),
	 * don't try to get stats for it, otherwise we'll return ENOENT.
	 */
	if (error == 0 && strchr(zc->zc_name, '$') == NULL) {
		error = zfs_ioc_objset_stats(zc); /* fill in the stats */
		if (error == ENOENT) {
			/* We lost a race with destroy, get the next one. */
			zc->zc_name[orig_len] = '\0';
			goto top;
		}
	}
	return (error);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_cookie		zap cursor
 * zc_nvlist_dst_size	size of buffer for property nvlist
 *
 * outputs:
 * zc_name		name of next snapshot
 * zc_objset_stats	stats
 * zc_nvlist_dst	property nvlist
 * zc_nvlist_dst_size	size of property nvlist
 */
static int
zfs_ioc_snapshot_list_next(zfs_cmd_t *zc)
{
	objset_t *os;
	int error;

top:
	if (zc->zc_cookie == 0)
		(void) dmu_objset_find(zc->zc_name, dmu_objset_prefetch,
		    NULL, DS_FIND_SNAPSHOTS);

	error = dmu_objset_hold(zc->zc_name, FTAG, &os);
	if (error)
		return (error == ENOENT ? ESRCH : error);

	/*
	 * A dataset name of maximum length cannot have any snapshots,
	 * so exit immediately.
	 */
	if (strlcat(zc->zc_name, "@", sizeof (zc->zc_name)) >= MAXNAMELEN) {
		dmu_objset_rele(os, FTAG);
		return (ESRCH);
	}

	error = dmu_snapshot_list_next(os,
	    sizeof (zc->zc_name) - strlen(zc->zc_name),
	    zc->zc_name + strlen(zc->zc_name), NULL, &zc->zc_cookie, NULL);
	dmu_objset_rele(os, FTAG);
	if (error == 0) {
		error = zfs_ioc_objset_stats(zc); /* fill in the stats */
		if (error == ENOENT)  {
			/* We lost a race with destroy, get the next one. */
			*strchr(zc->zc_name, '@') = '\0';
			goto top;
		}
	} else if (error == ENOENT) {
		error = ESRCH;
	}

	/* if we failed, undo the @ that we tacked on to zc_name */
	if (error)
		*strchr(zc->zc_name, '@') = '\0';
	return (error);
}

static int
zfs_prop_set_userquota(const char *dsname, nvpair_t *pair)
{
	const char *propname = nvpair_name(pair);
	uint64_t *valary;
	unsigned int vallen;
	const char *domain;
	char *dash;
	zfs_userquota_prop_t type;
	uint64_t rid;
	uint64_t quota;
	zfsvfs_t *zfsvfs;
	int err;

	if (nvpair_type(pair) == DATA_TYPE_NVLIST) {
		nvlist_t *attrs;
		VERIFY(nvpair_value_nvlist(pair, &attrs) == 0);
		if (nvlist_lookup_nvpair(attrs, ZPROP_VALUE,
		    &pair) != 0)
			return (EINVAL);
	}

	/*
	 * A correctly constructed propname is encoded as
	 * userquota@<rid>-<domain>.
	 */
	if ((dash = strchr(propname, '-')) == NULL ||
	    nvpair_value_uint64_array(pair, &valary, &vallen) != 0 ||
	    vallen != 3)
		return (EINVAL);

	domain = dash + 1;
	type = valary[0];
	rid = valary[1];
	quota = valary[2];

	err = zfsvfs_hold(dsname, FTAG, &zfsvfs);
	if (err == 0) {
		err = zfs_set_userquota(zfsvfs, type, domain, rid, quota);
		zfsvfs_rele(zfsvfs, FTAG);
	}

	return (err);
}

/*
 * If the named property is one that has a special function to set its value,
 * return 0 on success and a positive error code on failure; otherwise if it is
 * not one of the special properties handled by this function, return -1.
 *
 * XXX: It would be better for callers of the property interface if we handled
 * these special cases in dsl_prop.c (in the dsl layer).
 */
static int
zfs_prop_set_special(const char *dsname, zprop_source_t source,
    nvpair_t *pair)
{
	const char *propname = nvpair_name(pair);
	zfs_prop_t prop = zfs_name_to_prop(propname);
	uint64_t intval;
	int err;

	if (prop == ZPROP_INVAL) {
		if (zfs_prop_userquota(propname))
			return (zfs_prop_set_userquota(dsname, pair));
		return (-1);
	}

	if (nvpair_type(pair) == DATA_TYPE_NVLIST) {
		nvlist_t *attrs;
		VERIFY(nvpair_value_nvlist(pair, &attrs) == 0);
		VERIFY(nvlist_lookup_nvpair(attrs, ZPROP_VALUE,
		    &pair) == 0);
	}

	if (zfs_prop_get_type(prop) == PROP_TYPE_STRING)
		return (-1);

	VERIFY(0 == nvpair_value_uint64(pair, &intval));

	switch (prop) {
	case ZFS_PROP_QUOTA:
		err = dsl_dir_set_quota(dsname, source, intval);
		break;
	case ZFS_PROP_REFQUOTA:
		err = dsl_dataset_set_quota(dsname, source, intval);
		break;
	case ZFS_PROP_RESERVATION:
		err = dsl_dir_set_reservation(dsname, source, intval);
		break;
	case ZFS_PROP_REFRESERVATION:
		err = dsl_dataset_set_reservation(dsname, source, intval);
		break;
	case ZFS_PROP_VOLSIZE:
		err = zvol_set_volsize(dsname, ddi_driver_major(zfs_dip),
		    intval);
		break;
	case ZFS_PROP_VERSION:
	{
		zfsvfs_t *zfsvfs;

		if ((err = zfsvfs_hold(dsname, FTAG, &zfsvfs)) != 0)
			break;

		err = zfs_set_version(zfsvfs, intval);
		zfsvfs_rele(zfsvfs, FTAG);

		if (err == 0 && intval >= ZPL_VERSION_USERSPACE) {
			zfs_cmd_t *zc;

			zc = kmem_zalloc(sizeof (zfs_cmd_t), KM_SLEEP);
			(void) strcpy(zc->zc_name, dsname);
			(void) zfs_ioc_userspace_upgrade(zc);
			kmem_free(zc, sizeof (zfs_cmd_t));
		}
		break;
	}

	default:
		err = -1;
	}

	return (err);
}

/*
 * This function is best effort. If it fails to set any of the given properties,
 * it continues to set as many as it can and returns the first error
 * encountered. If the caller provides a non-NULL errlist, it also gives the
 * complete list of names of all the properties it failed to set along with the
 * corresponding error numbers. The caller is responsible for freeing the
 * returned errlist.
 *
 * If every property is set successfully, zero is returned and the list pointed
 * at by errlist is NULL.
 */
int
zfs_set_prop_nvlist(const char *dsname, zprop_source_t source, nvlist_t *nvl,
    nvlist_t **errlist)
{
	nvpair_t *pair;
	nvpair_t *propval;
	int rv = 0;
	uint64_t intval;
	char *strval;
	nvlist_t *genericnvl;
	nvlist_t *errors;
	nvlist_t *retrynvl;

	VERIFY(nvlist_alloc(&genericnvl, NV_UNIQUE_NAME, KM_SLEEP) == 0);
	VERIFY(nvlist_alloc(&errors, NV_UNIQUE_NAME, KM_SLEEP) == 0);
	VERIFY(nvlist_alloc(&retrynvl, NV_UNIQUE_NAME, KM_SLEEP) == 0);

retry:
	pair = NULL;
	while ((pair = nvlist_next_nvpair(nvl, pair)) != NULL) {
		const char *propname = nvpair_name(pair);
		zfs_prop_t prop = zfs_name_to_prop(propname);
		int err = 0;

		/* decode the property value */
		propval = pair;
		if (nvpair_type(pair) == DATA_TYPE_NVLIST) {
			nvlist_t *attrs;
			VERIFY(nvpair_value_nvlist(pair, &attrs) == 0);
			if (nvlist_lookup_nvpair(attrs, ZPROP_VALUE,
			    &propval) != 0)
				err = EINVAL;
		}

		/* Validate value type */
		if (err == 0 && prop == ZPROP_INVAL) {
			if (zfs_prop_user(propname)) {
				if (nvpair_type(propval) != DATA_TYPE_STRING)
					err = EINVAL;
			} else if (zfs_prop_userquota(propname)) {
				if (nvpair_type(propval) !=
				    DATA_TYPE_UINT64_ARRAY)
					err = EINVAL;
			}
		} else if (err == 0) {
			if (nvpair_type(propval) == DATA_TYPE_STRING) {
				if (zfs_prop_get_type(prop) != PROP_TYPE_STRING)
					err = EINVAL;
			} else if (nvpair_type(propval) == DATA_TYPE_UINT64) {
				const char *unused;

				VERIFY(nvpair_value_uint64(propval,
				    &intval) == 0);

				switch (zfs_prop_get_type(prop)) {
				case PROP_TYPE_NUMBER:
					break;
				case PROP_TYPE_STRING:
					err = EINVAL;
					break;
				case PROP_TYPE_INDEX:
					if (zfs_prop_index_to_string(prop,
					    intval, &unused) != 0)
						err = EINVAL;
					break;
				default:
					cmn_err(CE_PANIC,
					    "unknown property type");
				}
			} else {
				err = EINVAL;
			}
		}

		/* Validate permissions */
		if (err == 0)
			err = zfs_check_settable(dsname, pair, CRED());

		if (err == 0) {
			err = zfs_prop_set_special(dsname, source, pair);
			if (err == -1) {
				/*
				 * For better performance we build up a list of
				 * properties to set in a single transaction.
				 */
				err = nvlist_add_nvpair(genericnvl, pair);
			} else if (err != 0 && nvl != retrynvl) {
				/*
				 * This may be a spurious error caused by
				 * receiving quota and reservation out of order.
				 * Try again in a second pass.
				 */
				err = nvlist_add_nvpair(retrynvl, pair);
			}
		}

		if (err != 0)
			VERIFY(nvlist_add_int32(errors, propname, err) == 0);
	}

	if (nvl != retrynvl && !nvlist_empty(retrynvl)) {
		nvl = retrynvl;
		goto retry;
	}

	if (!nvlist_empty(genericnvl) &&
	    dsl_props_set(dsname, source, genericnvl) != 0) {
		/*
		 * If this fails, we still want to set as many properties as we
		 * can, so try setting them individually.
		 */
		pair = NULL;
		while ((pair = nvlist_next_nvpair(genericnvl, pair)) != NULL) {
			const char *propname = nvpair_name(pair);
			int err = 0;

			propval = pair;
			if (nvpair_type(pair) == DATA_TYPE_NVLIST) {
				nvlist_t *attrs;
				VERIFY(nvpair_value_nvlist(pair, &attrs) == 0);
				VERIFY(nvlist_lookup_nvpair(attrs, ZPROP_VALUE,
				    &propval) == 0);
			}

			if (nvpair_type(propval) == DATA_TYPE_STRING) {
				VERIFY(nvpair_value_string(propval,
				    &strval) == 0);
				err = dsl_prop_set(dsname, propname, source, 1,
				    strlen(strval) + 1, strval);
			} else {
				VERIFY(nvpair_value_uint64(propval,
				    &intval) == 0);
				err = dsl_prop_set(dsname, propname, source, 8,
				    1, &intval);
			}

			if (err != 0) {
				VERIFY(nvlist_add_int32(errors, propname,
				    err) == 0);
			}
		}
	}
	nvlist_free(genericnvl);
	nvlist_free(retrynvl);

	if ((pair = nvlist_next_nvpair(errors, NULL)) == NULL) {
		nvlist_free(errors);
		errors = NULL;
	} else {
		VERIFY(nvpair_value_int32(pair, &rv) == 0);
	}

	if (errlist == NULL)
		nvlist_free(errors);
	else
		*errlist = errors;

	return (rv);
}

/*
 * Check that all the properties are valid user properties.
 */
static int
zfs_check_userprops(char *fsname, nvlist_t *nvl)
{
	nvpair_t *pair = NULL;
	int error = 0;

	while ((pair = nvlist_next_nvpair(nvl, pair)) != NULL) {
		const char *propname = nvpair_name(pair);
		char *valstr;

		if (!zfs_prop_user(propname) ||
		    nvpair_type(pair) != DATA_TYPE_STRING)
			return (EINVAL);

		if (error = zfs_secpolicy_write_perms(fsname,
		    ZFS_DELEG_PERM_USERPROP, CRED()))
			return (error);

		if (strlen(propname) >= ZAP_MAXNAMELEN)
			return (ENAMETOOLONG);

		VERIFY(nvpair_value_string(pair, &valstr) == 0);
		if (strlen(valstr) >= ZAP_MAXVALUELEN)
			return (E2BIG);
	}
	return (0);
}

static void
props_skip(nvlist_t *props, nvlist_t *skipped, nvlist_t **newprops)
{
	nvpair_t *pair;

	VERIFY(nvlist_alloc(newprops, NV_UNIQUE_NAME, KM_SLEEP) == 0);

	pair = NULL;
	while ((pair = nvlist_next_nvpair(props, pair)) != NULL) {
		if (nvlist_exists(skipped, nvpair_name(pair)))
			continue;

		VERIFY(nvlist_add_nvpair(*newprops, pair) == 0);
	}
}

static int
clear_received_props(objset_t *os, const char *fs, nvlist_t *props,
    nvlist_t *skipped)
{
	int err = 0;
	nvlist_t *cleared_props = NULL;
	props_skip(props, skipped, &cleared_props);
	if (!nvlist_empty(cleared_props)) {
		/*
		 * Acts on local properties until the dataset has received
		 * properties at least once on or after SPA_VERSION_RECVD_PROPS.
		 */
		zprop_source_t flags = (ZPROP_SRC_NONE |
		    (dsl_prop_get_hasrecvd(os) ? ZPROP_SRC_RECEIVED : 0));
		err = zfs_set_prop_nvlist(fs, flags, cleared_props, NULL);
	}
	nvlist_free(cleared_props);
	return (err);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_value		name of property to set
 * zc_nvlist_src{_size}	nvlist of properties to apply
 * zc_cookie		received properties flag
 *
 * outputs:
 * zc_nvlist_dst{_size} error for each unapplied received property
 */
static int
zfs_ioc_set_prop(zfs_cmd_t *zc)
{
	nvlist_t *nvl;
	boolean_t received = zc->zc_cookie;
	zprop_source_t source = (received ? ZPROP_SRC_RECEIVED :
	    ZPROP_SRC_LOCAL);
	nvlist_t *errors = NULL;
	int error;

	if ((error = get_nvlist(zc->zc_nvlist_src, zc->zc_nvlist_src_size,
	    zc->zc_iflags, &nvl)) != 0)
		return (error);

	if (received) {
		nvlist_t *origprops;
		objset_t *os;

		if (dmu_objset_hold(zc->zc_name, FTAG, &os) == 0) {
			if (dsl_prop_get_received(os, &origprops) == 0) {
				(void) clear_received_props(os,
				    zc->zc_name, origprops, nvl);
				nvlist_free(origprops);
			}

			dsl_prop_set_hasrecvd(os);
			dmu_objset_rele(os, FTAG);
		}
	}

	error = zfs_set_prop_nvlist(zc->zc_name, source, nvl, &errors);

	if (zc->zc_nvlist_dst != NULL && errors != NULL) {
		(void) put_nvlist(zc, errors);
	}

	nvlist_free(errors);
	nvlist_free(nvl);
	return (error);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_value		name of property to inherit
 * zc_cookie		revert to received value if TRUE
 *
 * outputs:		none
 */
static int
zfs_ioc_inherit_prop(zfs_cmd_t *zc)
{
	const char *propname = zc->zc_value;
	zfs_prop_t prop = zfs_name_to_prop(propname);
	boolean_t received = zc->zc_cookie;
	zprop_source_t source = (received
	    ? ZPROP_SRC_NONE		/* revert to received value, if any */
	    : ZPROP_SRC_INHERITED);	/* explicitly inherit */

	if (received) {
		nvlist_t *dummy;
		nvpair_t *pair;
		zprop_type_t type;
		int err;

		/*
		 * zfs_prop_set_special() expects properties in the form of an
		 * nvpair with type info.
		 */
		if (prop == ZPROP_INVAL) {
			if (!zfs_prop_user(propname))
				return (EINVAL);

			type = PROP_TYPE_STRING;
		} else if (prop == ZFS_PROP_VOLSIZE ||
		    prop == ZFS_PROP_VERSION) {
			return (EINVAL);
		} else {
			type = zfs_prop_get_type(prop);
		}

		VERIFY(nvlist_alloc(&dummy, NV_UNIQUE_NAME, KM_SLEEP) == 0);

		switch (type) {
		case PROP_TYPE_STRING:
			VERIFY(0 == nvlist_add_string(dummy, propname, ""));
			break;
		case PROP_TYPE_NUMBER:
		case PROP_TYPE_INDEX:
			VERIFY(0 == nvlist_add_uint64(dummy, propname, 0));
			break;
		default:
			nvlist_free(dummy);
			return (EINVAL);
		}

		pair = nvlist_next_nvpair(dummy, NULL);
		err = zfs_prop_set_special(zc->zc_name, source, pair);
		nvlist_free(dummy);
		if (err != -1)
			return (err); /* special property already handled */
	} else {
		/*
		 * Only check this in the non-received case. We want to allow
		 * 'inherit -S' to revert non-inheritable properties like quota
		 * and reservation to the received or default values even though
		 * they are not considered inheritable.
		 */
		if (prop != ZPROP_INVAL && !zfs_prop_inheritable(prop))
			return (EINVAL);
	}

	/* the property name has been validated by zfs_secpolicy_inherit() */
	return (dsl_prop_set(zc->zc_name, zc->zc_value, source, 0, 0, NULL));
}

static int
zfs_ioc_pool_set_props(zfs_cmd_t *zc)
{
	nvlist_t *props;
	spa_t *spa;
	int error;
	nvpair_t *pair;

	if (error = get_nvlist(zc->zc_nvlist_src, zc->zc_nvlist_src_size,
	    zc->zc_iflags, &props))
		return (error);

	/*
	 * If the only property is the configfile, then just do a spa_lookup()
	 * to handle the faulted case.
	 */
	pair = nvlist_next_nvpair(props, NULL);
	if (pair != NULL && strcmp(nvpair_name(pair),
	    zpool_prop_to_name(ZPOOL_PROP_CACHEFILE)) == 0 &&
	    nvlist_next_nvpair(props, pair) == NULL) {
		mutex_enter(&spa_namespace_lock);
		if ((spa = spa_lookup(zc->zc_name)) != NULL) {
			spa_configfile_set(spa, props, B_FALSE);
			spa_config_sync(spa, B_FALSE, B_TRUE);
		}
		mutex_exit(&spa_namespace_lock);
		if (spa != NULL) {
			nvlist_free(props);
			return (0);
		}
	}

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0) {
		nvlist_free(props);
		return (error);
	}

	error = spa_prop_set(spa, props);

	nvlist_free(props);
	spa_close(spa, FTAG);

	return (error);
}

static int
zfs_ioc_pool_get_props(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;
	nvlist_t *nvp = NULL;

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0) {
		/*
		 * If the pool is faulted, there may be properties we can still
		 * get (such as altroot and cachefile), so attempt to get them
		 * anyway.
		 */
		mutex_enter(&spa_namespace_lock);
		if ((spa = spa_lookup(zc->zc_name)) != NULL)
			error = spa_prop_get(spa, &nvp);
		mutex_exit(&spa_namespace_lock);
	} else {
		error = spa_prop_get(spa, &nvp);
		spa_close(spa, FTAG);
	}

	if (error == 0 && zc->zc_nvlist_dst != NULL)
		error = put_nvlist(zc, nvp);
	else
		error = EFAULT;

	nvlist_free(nvp);
	return (error);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_nvlist_src{_size}	nvlist of delegated permissions
 * zc_perm_action	allow/unallow flag
 *
 * outputs:		none
 */
static int
zfs_ioc_set_fsacl(zfs_cmd_t *zc)
{
	int error;
	nvlist_t *fsaclnv = NULL;

	if ((error = get_nvlist(zc->zc_nvlist_src, zc->zc_nvlist_src_size,
	    zc->zc_iflags, &fsaclnv)) != 0)
		return (error);

	/*
	 * Verify nvlist is constructed correctly
	 */
	if ((error = zfs_deleg_verify_nvlist(fsaclnv)) != 0) {
		nvlist_free(fsaclnv);
		return (EINVAL);
	}

	/*
	 * If we don't have PRIV_SYS_MOUNT, then validate
	 * that user is allowed to hand out each permission in
	 * the nvlist(s)
	 */

	error = secpolicy_zfs(CRED());
	if (error) {
		if (zc->zc_perm_action == B_FALSE) {
			error = dsl_deleg_can_allow(zc->zc_name,
			    fsaclnv, CRED());
		} else {
			error = dsl_deleg_can_unallow(zc->zc_name,
			    fsaclnv, CRED());
		}
	}

	if (error == 0)
		error = dsl_deleg_set(zc->zc_name, fsaclnv, zc->zc_perm_action);

	nvlist_free(fsaclnv);
	return (error);
}

/*
 * inputs:
 * zc_name		name of filesystem
 *
 * outputs:
 * zc_nvlist_src{_size}	nvlist of delegated permissions
 */
static int
zfs_ioc_get_fsacl(zfs_cmd_t *zc)
{
	nvlist_t *nvp;
	int error;

	if ((error = dsl_deleg_get(zc->zc_name, &nvp)) == 0) {
		error = put_nvlist(zc, nvp);
		nvlist_free(nvp);
	}

	return (error);
}

/*
 * Search the vfs list for a specified resource.  Returns a pointer to it
 * or NULL if no suitable entry is found. The caller of this routine
 * is responsible for releasing the returned vfs pointer.
 */
static vfs_t *
zfs_get_vfs(const char *resource)
{
	struct vfs *vfsp;
	struct vfs *vfs_found = NULL;

	vfs_list_read_lock();
	vfsp = rootvfs;
	do {
		if (strcmp(refstr_value(vfsp->vfs_resource), resource) == 0) {
			VFS_HOLD(vfsp);
			vfs_found = vfsp;
			break;
		}
		vfsp = vfsp->vfs_next;
	} while (vfsp != rootvfs);
	vfs_list_unlock();
	return (vfs_found);
}

/* ARGSUSED */
static void
zfs_create_cb(objset_t *os, void *arg, cred_t *cr, dmu_tx_t *tx)
{
	zfs_creat_t *zct = arg;

	zfs_create_fs(os, cr, zct->zct_zplprops, tx);
}

#define	ZFS_PROP_UNDEFINED	((uint64_t)-1)

/*
 * inputs:
 * createprops		list of properties requested by creator
 * default_zplver	zpl version to use if unspecified in createprops
 * fuids_ok		fuids allowed in this version of the spa?
 * os			parent objset pointer (NULL if root fs)
 *
 * outputs:
 * zplprops	values for the zplprops we attach to the master node object
 * is_ci	true if requested file system will be purely case-insensitive
 *
 * Determine the settings for utf8only, normalization and
 * casesensitivity.  Specific values may have been requested by the
 * creator and/or we can inherit values from the parent dataset.  If
 * the file system is of too early a vintage, a creator can not
 * request settings for these properties, even if the requested
 * setting is the default value.  We don't actually want to create dsl
 * properties for these, so remove them from the source nvlist after
 * processing.
 */
static int
zfs_fill_zplprops_impl(objset_t *os, uint64_t zplver,
    boolean_t fuids_ok, boolean_t sa_ok, nvlist_t *createprops,
    nvlist_t *zplprops, boolean_t *is_ci)
{
	uint64_t sense = ZFS_PROP_UNDEFINED;
	uint64_t norm = ZFS_PROP_UNDEFINED;
	uint64_t u8 = ZFS_PROP_UNDEFINED;

	ASSERT(zplprops != NULL);

	/*
	 * Pull out creator prop choices, if any.
	 */
	if (createprops) {
		(void) nvlist_lookup_uint64(createprops,
		    zfs_prop_to_name(ZFS_PROP_VERSION), &zplver);
		(void) nvlist_lookup_uint64(createprops,
		    zfs_prop_to_name(ZFS_PROP_NORMALIZE), &norm);
		(void) nvlist_remove_all(createprops,
		    zfs_prop_to_name(ZFS_PROP_NORMALIZE));
		(void) nvlist_lookup_uint64(createprops,
		    zfs_prop_to_name(ZFS_PROP_UTF8ONLY), &u8);
		(void) nvlist_remove_all(createprops,
		    zfs_prop_to_name(ZFS_PROP_UTF8ONLY));
		(void) nvlist_lookup_uint64(createprops,
		    zfs_prop_to_name(ZFS_PROP_CASE), &sense);
		(void) nvlist_remove_all(createprops,
		    zfs_prop_to_name(ZFS_PROP_CASE));
	}

	/*
	 * If the zpl version requested is whacky or the file system
	 * or pool is version is too "young" to support normalization
	 * and the creator tried to set a value for one of the props,
	 * error out.
	 */
	if ((zplver < ZPL_VERSION_INITIAL || zplver > ZPL_VERSION) ||
	    (zplver >= ZPL_VERSION_FUID && !fuids_ok) ||
	    (zplver >= ZPL_VERSION_SA && !sa_ok) ||
	    (zplver < ZPL_VERSION_NORMALIZATION &&
	    (norm != ZFS_PROP_UNDEFINED || u8 != ZFS_PROP_UNDEFINED ||
	    sense != ZFS_PROP_UNDEFINED)))
		return (ENOTSUP);

	/*
	 * Put the version in the zplprops
	 */
	VERIFY(nvlist_add_uint64(zplprops,
	    zfs_prop_to_name(ZFS_PROP_VERSION), zplver) == 0);

	if (norm == ZFS_PROP_UNDEFINED)
		VERIFY(zfs_get_zplprop(os, ZFS_PROP_NORMALIZE, &norm) == 0);
	VERIFY(nvlist_add_uint64(zplprops,
	    zfs_prop_to_name(ZFS_PROP_NORMALIZE), norm) == 0);

	/*
	 * If we're normalizing, names must always be valid UTF-8 strings.
	 */
	if (norm)
		u8 = 1;
	if (u8 == ZFS_PROP_UNDEFINED)
		VERIFY(zfs_get_zplprop(os, ZFS_PROP_UTF8ONLY, &u8) == 0);
	VERIFY(nvlist_add_uint64(zplprops,
	    zfs_prop_to_name(ZFS_PROP_UTF8ONLY), u8) == 0);

	if (sense == ZFS_PROP_UNDEFINED)
		VERIFY(zfs_get_zplprop(os, ZFS_PROP_CASE, &sense) == 0);
	VERIFY(nvlist_add_uint64(zplprops,
	    zfs_prop_to_name(ZFS_PROP_CASE), sense) == 0);

	if (is_ci)
		*is_ci = (sense == ZFS_CASE_INSENSITIVE);

	return (0);
}

static int
zfs_fill_zplprops(const char *dataset, nvlist_t *createprops,
    nvlist_t *zplprops, boolean_t *is_ci)
{
	boolean_t fuids_ok, sa_ok;
	uint64_t zplver = ZPL_VERSION;
	objset_t *os = NULL;
	char parentname[MAXNAMELEN];
	char *cp;
	spa_t *spa;
	uint64_t spa_vers;
	int error;

	(void) strlcpy(parentname, dataset, sizeof (parentname));
	cp = strrchr(parentname, '/');
	ASSERT(cp != NULL);
	cp[0] = '\0';

	if ((error = spa_open(dataset, &spa, FTAG)) != 0)
		return (error);

	spa_vers = spa_version(spa);
	spa_close(spa, FTAG);

	zplver = zfs_zpl_version_map(spa_vers);
	fuids_ok = (zplver >= ZPL_VERSION_FUID);
	sa_ok = (zplver >= ZPL_VERSION_SA);

	/*
	 * Open parent object set so we can inherit zplprop values.
	 */
	if ((error = dmu_objset_hold(parentname, FTAG, &os)) != 0)
		return (error);

	error = zfs_fill_zplprops_impl(os, zplver, fuids_ok, sa_ok, createprops,
	    zplprops, is_ci);
	dmu_objset_rele(os, FTAG);
	return (error);
}

static int
zfs_fill_zplprops_root(uint64_t spa_vers, nvlist_t *createprops,
    nvlist_t *zplprops, boolean_t *is_ci)
{
	boolean_t fuids_ok;
	boolean_t sa_ok;
	uint64_t zplver = ZPL_VERSION;
	int error;

	zplver = zfs_zpl_version_map(spa_vers);
	fuids_ok = (zplver >= ZPL_VERSION_FUID);
	sa_ok = (zplver >= ZPL_VERSION_SA);

	error = zfs_fill_zplprops_impl(NULL, zplver, fuids_ok, sa_ok,
	    createprops, zplprops, is_ci);
	return (error);
}

/*
 * inputs:
 * zc_objset_type	type of objset to create (fs vs zvol)
 * zc_name		name of new objset
 * zc_value		name of snapshot to clone from (may be empty)
 * zc_nvlist_src{_size}	nvlist of properties to apply
 *
 * outputs: none
 */
static int
zfs_ioc_create(zfs_cmd_t *zc)
{
	objset_t *clone;
	int error = 0;
	zfs_creat_t zct;
	nvlist_t *nvprops = NULL;
	void (*cbfunc)(objset_t *os, void *arg, cred_t *cr, dmu_tx_t *tx);
	dmu_objset_type_t type = zc->zc_objset_type;

	switch (type) {

	case DMU_OST_ZFS:
		cbfunc = zfs_create_cb;
		break;

	case DMU_OST_ZVOL:
		cbfunc = zvol_create_cb;
		break;

	default:
		cbfunc = NULL;
		break;
	}
	if (strchr(zc->zc_name, '@') ||
	    strchr(zc->zc_name, '%'))
		return (EINVAL);

	if (zc->zc_nvlist_src != NULL &&
	    (error = get_nvlist(zc->zc_nvlist_src, zc->zc_nvlist_src_size,
	    zc->zc_iflags, &nvprops)) != 0)
		return (error);

	zct.zct_zplprops = NULL;
	zct.zct_props = nvprops;

	if (zc->zc_value[0] != '\0') {
		/*
		 * We're creating a clone of an existing snapshot.
		 */
		zc->zc_value[sizeof (zc->zc_value) - 1] = '\0';
		if (dataset_namecheck(zc->zc_value, NULL, NULL) != 0) {
			nvlist_free(nvprops);
			return (EINVAL);
		}

		error = dmu_objset_hold(zc->zc_value, FTAG, &clone);
		if (error) {
			nvlist_free(nvprops);
			return (error);
		}

		error = dmu_objset_clone(zc->zc_name, dmu_objset_ds(clone), 0);
		dmu_objset_rele(clone, FTAG);
		if (error) {
			nvlist_free(nvprops);
			return (error);
		}
	} else {
		boolean_t is_insensitive = B_FALSE;

		if (cbfunc == NULL) {
			nvlist_free(nvprops);
			return (EINVAL);
		}

		if (type == DMU_OST_ZVOL) {
			uint64_t volsize, volblocksize;

			if (nvprops == NULL ||
			    nvlist_lookup_uint64(nvprops,
			    zfs_prop_to_name(ZFS_PROP_VOLSIZE),
			    &volsize) != 0) {
				nvlist_free(nvprops);
				return (EINVAL);
			}

			if ((error = nvlist_lookup_uint64(nvprops,
			    zfs_prop_to_name(ZFS_PROP_VOLBLOCKSIZE),
			    &volblocksize)) != 0 && error != ENOENT) {
				nvlist_free(nvprops);
				return (EINVAL);
			}

			if (error != 0)
				volblocksize = zfs_prop_default_numeric(
				    ZFS_PROP_VOLBLOCKSIZE);

			if ((error = zvol_check_volblocksize(
			    volblocksize)) != 0 ||
			    (error = zvol_check_volsize(volsize,
			    volblocksize)) != 0) {
				nvlist_free(nvprops);
				return (error);
			}
		} else if (type == DMU_OST_ZFS) {
			int error;

			/*
			 * We have to have normalization and
			 * case-folding flags correct when we do the
			 * file system creation, so go figure them out
			 * now.
			 */
			VERIFY(nvlist_alloc(&zct.zct_zplprops,
			    NV_UNIQUE_NAME, KM_SLEEP) == 0);
			error = zfs_fill_zplprops(zc->zc_name, nvprops,
			    zct.zct_zplprops, &is_insensitive);
			if (error != 0) {
				nvlist_free(nvprops);
				nvlist_free(zct.zct_zplprops);
				return (error);
			}
		}
		error = dmu_objset_create(zc->zc_name, type,
		    is_insensitive ? DS_FLAG_CI_DATASET : 0, cbfunc, &zct);
		nvlist_free(zct.zct_zplprops);
	}

	/*
	 * It would be nice to do this atomically.
	 */
	if (error == 0) {
		error = zfs_set_prop_nvlist(zc->zc_name, ZPROP_SRC_LOCAL,
		    nvprops, NULL);
		if (error != 0)
			(void) dmu_objset_destroy(zc->zc_name, B_FALSE);
	}
	nvlist_free(nvprops);
	return (error);
}

/*
 * inputs:
 * zc_name	name of filesystem
 * zc_value	short name of snapshot
 * zc_cookie	recursive flag
 * zc_nvlist_src[_size] property list
 *
 * outputs:
 * zc_value	short snapname (i.e. part after the '@')
 */
static int
zfs_ioc_snapshot(zfs_cmd_t *zc)
{
	nvlist_t *nvprops = NULL;
	int error;
	boolean_t recursive = zc->zc_cookie;

	if (snapshot_namecheck(zc->zc_value, NULL, NULL) != 0)
		return (EINVAL);

	if (zc->zc_nvlist_src != NULL &&
	    (error = get_nvlist(zc->zc_nvlist_src, zc->zc_nvlist_src_size,
	    zc->zc_iflags, &nvprops)) != 0)
		return (error);

	error = zfs_check_userprops(zc->zc_name, nvprops);
	if (error)
		goto out;

	if (!nvlist_empty(nvprops) &&
	    zfs_earlier_version(zc->zc_name, SPA_VERSION_SNAP_PROPS)) {
		error = ENOTSUP;
		goto out;
	}

	error = dmu_objset_snapshot(zc->zc_name, zc->zc_value,
	    nvprops, recursive);

out:
	nvlist_free(nvprops);
	return (error);
}

int
zfs_unmount_snap(const char *name, void *arg)
{
	vfs_t *vfsp = NULL;

	if (arg) {
		char *snapname = arg;
		char *fullname = kmem_asprintf("%s@%s", name, snapname);
		vfsp = zfs_get_vfs(fullname);
		strfree(fullname);
	} else if (strchr(name, '@')) {
		vfsp = zfs_get_vfs(name);
	}

	if (vfsp) {
		/*
		 * Always force the unmount for snapshots.
		 */
		int flag = MS_FORCE;
		int err;

		if ((err = vn_vfswlock(vfsp->vfs_vnodecovered)) != 0) {
			VFS_RELE(vfsp);
			return (err);
		}
		VFS_RELE(vfsp);
		if ((err = dounmount(vfsp, flag, kcred)) != 0)
			return (err);
	}
	return (0);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_value		short name of snapshot
 * zc_defer_destroy	mark for deferred destroy
 *
 * outputs:	none
 */
static int
zfs_ioc_destroy_snaps(zfs_cmd_t *zc)
{
	int err;

	if (snapshot_namecheck(zc->zc_value, NULL, NULL) != 0)
		return (EINVAL);
	err = dmu_objset_find(zc->zc_name,
	    zfs_unmount_snap, zc->zc_value, DS_FIND_CHILDREN);
	if (err)
		return (err);
	return (dmu_snapshots_destroy(zc->zc_name, zc->zc_value,
	    zc->zc_defer_destroy));
}

/*
 * inputs:
 * zc_name		name of dataset to destroy
 * zc_objset_type	type of objset
 * zc_defer_destroy	mark for deferred destroy
 *
 * outputs:		none
 */
static int
zfs_ioc_destroy(zfs_cmd_t *zc)
{
	int err;
	if (strchr(zc->zc_name, '@') && zc->zc_objset_type == DMU_OST_ZFS) {
		err = zfs_unmount_snap(zc->zc_name, NULL);
		if (err)
			return (err);
	}

	err = dmu_objset_destroy(zc->zc_name, zc->zc_defer_destroy);
	if (zc->zc_objset_type == DMU_OST_ZVOL && err == 0)
		(void) zvol_remove_minor(zc->zc_name);
	return (err);
}

/*
 * inputs:
 * zc_name	name of dataset to rollback (to most recent snapshot)
 *
 * outputs:	none
 */
static int
zfs_ioc_rollback(zfs_cmd_t *zc)
{
	dsl_dataset_t *ds, *clone;
	int error;
	zfsvfs_t *zfsvfs;
	char *clone_name;

	error = dsl_dataset_hold(zc->zc_name, FTAG, &ds);
	if (error)
		return (error);

	/* must not be a snapshot */
	if (dsl_dataset_is_snapshot(ds)) {
		dsl_dataset_rele(ds, FTAG);
		return (EINVAL);
	}

	/* must have a most recent snapshot */
	if (ds->ds_phys->ds_prev_snap_txg < TXG_INITIAL) {
		dsl_dataset_rele(ds, FTAG);
		return (EINVAL);
	}

	/*
	 * Create clone of most recent snapshot.
	 */
	clone_name = kmem_asprintf("%s/%%rollback", zc->zc_name);
	error = dmu_objset_clone(clone_name, ds->ds_prev, DS_FLAG_INCONSISTENT);
	if (error)
		goto out;

	error = dsl_dataset_own(clone_name, B_TRUE, FTAG, &clone);
	if (error)
		goto out;

	/*
	 * Do clone swap.
	 */
	if (getzfsvfs(zc->zc_name, &zfsvfs) == 0) {
		error = zfs_suspend_fs(zfsvfs);
		if (error == 0) {
			int resume_err;

			if (dsl_dataset_tryown(ds, B_FALSE, FTAG)) {
				error = dsl_dataset_clone_swap(clone, ds,
				    B_TRUE);
				dsl_dataset_disown(ds, FTAG);
				ds = NULL;
			} else {
				error = EBUSY;
			}
			resume_err = zfs_resume_fs(zfsvfs, zc->zc_name);
			error = error ? error : resume_err;
		}
		VFS_RELE(zfsvfs->z_vfs);
	} else {
		if (dsl_dataset_tryown(ds, B_FALSE, FTAG)) {
			error = dsl_dataset_clone_swap(clone, ds, B_TRUE);
			dsl_dataset_disown(ds, FTAG);
			ds = NULL;
		} else {
			error = EBUSY;
		}
	}

	/*
	 * Destroy clone (which also closes it).
	 */
	(void) dsl_dataset_destroy(clone, FTAG, B_FALSE);

out:
	strfree(clone_name);
	if (ds)
		dsl_dataset_rele(ds, FTAG);
	return (error);
}

/*
 * inputs:
 * zc_name	old name of dataset
 * zc_value	new name of dataset
 * zc_cookie	recursive flag (only valid for snapshots)
 *
 * outputs:	none
 */
static int
zfs_ioc_rename(zfs_cmd_t *zc)
{
	boolean_t recursive = zc->zc_cookie & 1;

	zc->zc_value[sizeof (zc->zc_value) - 1] = '\0';
	if (dataset_namecheck(zc->zc_value, NULL, NULL) != 0 ||
	    strchr(zc->zc_value, '%'))
		return (EINVAL);

	/*
	 * Unmount snapshot unless we're doing a recursive rename,
	 * in which case the dataset code figures out which snapshots
	 * to unmount.
	 */
	if (!recursive && strchr(zc->zc_name, '@') != NULL &&
	    zc->zc_objset_type == DMU_OST_ZFS) {
		int err = zfs_unmount_snap(zc->zc_name, NULL);
		if (err)
			return (err);
	}
	if (zc->zc_objset_type == DMU_OST_ZVOL)
		(void) zvol_remove_minor(zc->zc_name);
	return (dmu_objset_rename(zc->zc_name, zc->zc_value, recursive));
}

static int
zfs_check_settable(const char *dsname, nvpair_t *pair, cred_t *cr)
{
	const char *propname = nvpair_name(pair);
	boolean_t issnap = (strchr(dsname, '@') != NULL);
	zfs_prop_t prop = zfs_name_to_prop(propname);
	uint64_t intval;
	int err;

	if (prop == ZPROP_INVAL) {
		if (zfs_prop_user(propname)) {
			if (err = zfs_secpolicy_write_perms(dsname,
			    ZFS_DELEG_PERM_USERPROP, cr))
				return (err);
			return (0);
		}

		if (!issnap && zfs_prop_userquota(propname)) {
			const char *perm = NULL;
			const char *uq_prefix =
			    zfs_userquota_prop_prefixes[ZFS_PROP_USERQUOTA];
			const char *gq_prefix =
			    zfs_userquota_prop_prefixes[ZFS_PROP_GROUPQUOTA];

			if (strncmp(propname, uq_prefix,
			    strlen(uq_prefix)) == 0) {
				perm = ZFS_DELEG_PERM_USERQUOTA;
			} else if (strncmp(propname, gq_prefix,
			    strlen(gq_prefix)) == 0) {
				perm = ZFS_DELEG_PERM_GROUPQUOTA;
			} else {
				/* USERUSED and GROUPUSED are read-only */
				return (EINVAL);
			}

			if (err = zfs_secpolicy_write_perms(dsname, perm, cr))
				return (err);
			return (0);
		}

		return (EINVAL);
	}

	if (issnap)
		return (EINVAL);

	if (nvpair_type(pair) == DATA_TYPE_NVLIST) {
		/*
		 * dsl_prop_get_all_impl() returns properties in this
		 * format.
		 */
		nvlist_t *attrs;
		VERIFY(nvpair_value_nvlist(pair, &attrs) == 0);
		VERIFY(nvlist_lookup_nvpair(attrs, ZPROP_VALUE,
		    &pair) == 0);
	}

	/*
	 * Check that this value is valid for this pool version
	 */
	switch (prop) {
	case ZFS_PROP_COMPRESSION:
		/*
		 * If the user specified gzip compression, make sure
		 * the SPA supports it. We ignore any errors here since
		 * we'll catch them later.
		 */
		if (nvpair_type(pair) == DATA_TYPE_UINT64 &&
		    nvpair_value_uint64(pair, &intval) == 0) {
			if (intval >= ZIO_COMPRESS_GZIP_1 &&
			    intval <= ZIO_COMPRESS_GZIP_9 &&
			    zfs_earlier_version(dsname,
			    SPA_VERSION_GZIP_COMPRESSION)) {
				return (ENOTSUP);
			}

			if (intval == ZIO_COMPRESS_ZLE &&
			    zfs_earlier_version(dsname,
			    SPA_VERSION_ZLE_COMPRESSION))
				return (ENOTSUP);

			/*
			 * If this is a bootable dataset then
			 * verify that the compression algorithm
			 * is supported for booting. We must return
			 * something other than ENOTSUP since it
			 * implies a downrev pool version.
			 */
			if (zfs_is_bootfs(dsname) &&
			    !BOOTFS_COMPRESS_VALID(intval)) {
				return (ERANGE);
			}
		}
		break;

	case ZFS_PROP_COPIES:
		if (zfs_earlier_version(dsname, SPA_VERSION_DITTO_BLOCKS))
			return (ENOTSUP);
		break;

	case ZFS_PROP_DEDUP:
		if (zfs_earlier_version(dsname, SPA_VERSION_DEDUP))
			return (ENOTSUP);
		break;

	case ZFS_PROP_SHARESMB:
		if (zpl_earlier_version(dsname, ZPL_VERSION_FUID))
			return (ENOTSUP);
		break;

	case ZFS_PROP_ACLINHERIT:
		if (nvpair_type(pair) == DATA_TYPE_UINT64 &&
		    nvpair_value_uint64(pair, &intval) == 0) {
			if (intval == ZFS_ACL_PASSTHROUGH_X &&
			    zfs_earlier_version(dsname,
			    SPA_VERSION_PASSTHROUGH_X))
				return (ENOTSUP);
		}
		break;
	}

	return (zfs_secpolicy_setprop(dsname, prop, pair, CRED()));
}

/*
 * Removes properties from the given props list that fail permission checks
 * needed to clear them and to restore them in case of a receive error. For each
 * property, make sure we have both set and inherit permissions.
 *
 * Returns the first error encountered if any permission checks fail. If the
 * caller provides a non-NULL errlist, it also gives the complete list of names
 * of all the properties that failed a permission check along with the
 * corresponding error numbers. The caller is responsible for freeing the
 * returned errlist.
 *
 * If every property checks out successfully, zero is returned and the list
 * pointed at by errlist is NULL.
 */
static int
zfs_check_clearable(char *dataset, nvlist_t *props, nvlist_t **errlist)
{
	zfs_cmd_t *zc;
	nvpair_t *pair, *next_pair;
	nvlist_t *errors;
	int err, rv = 0;

	if (props == NULL)
		return (0);

	VERIFY(nvlist_alloc(&errors, NV_UNIQUE_NAME, KM_SLEEP) == 0);

	zc = kmem_alloc(sizeof (zfs_cmd_t), KM_SLEEP);
	(void) strcpy(zc->zc_name, dataset);
	pair = nvlist_next_nvpair(props, NULL);
	while (pair != NULL) {
		next_pair = nvlist_next_nvpair(props, pair);

		(void) strcpy(zc->zc_value, nvpair_name(pair));
		if ((err = zfs_check_settable(dataset, pair, CRED())) != 0 ||
		    (err = zfs_secpolicy_inherit(zc, CRED())) != 0) {
			VERIFY(nvlist_remove_nvpair(props, pair) == 0);
			VERIFY(nvlist_add_int32(errors,
			    zc->zc_value, err) == 0);
		}
		pair = next_pair;
	}
	kmem_free(zc, sizeof (zfs_cmd_t));

	if ((pair = nvlist_next_nvpair(errors, NULL)) == NULL) {
		nvlist_free(errors);
		errors = NULL;
	} else {
		VERIFY(nvpair_value_int32(pair, &rv) == 0);
	}

	if (errlist == NULL)
		nvlist_free(errors);
	else
		*errlist = errors;

	return (rv);
}

static boolean_t
propval_equals(nvpair_t *p1, nvpair_t *p2)
{
	if (nvpair_type(p1) == DATA_TYPE_NVLIST) {
		/* dsl_prop_get_all_impl() format */
		nvlist_t *attrs;
		VERIFY(nvpair_value_nvlist(p1, &attrs) == 0);
		VERIFY(nvlist_lookup_nvpair(attrs, ZPROP_VALUE,
		    &p1) == 0);
	}

	if (nvpair_type(p2) == DATA_TYPE_NVLIST) {
		nvlist_t *attrs;
		VERIFY(nvpair_value_nvlist(p2, &attrs) == 0);
		VERIFY(nvlist_lookup_nvpair(attrs, ZPROP_VALUE,
		    &p2) == 0);
	}

	if (nvpair_type(p1) != nvpair_type(p2))
		return (B_FALSE);

	if (nvpair_type(p1) == DATA_TYPE_STRING) {
		char *valstr1, *valstr2;

		VERIFY(nvpair_value_string(p1, (char **)&valstr1) == 0);
		VERIFY(nvpair_value_string(p2, (char **)&valstr2) == 0);
		return (strcmp(valstr1, valstr2) == 0);
	} else {
		uint64_t intval1, intval2;

		VERIFY(nvpair_value_uint64(p1, &intval1) == 0);
		VERIFY(nvpair_value_uint64(p2, &intval2) == 0);
		return (intval1 == intval2);
	}
}

/*
 * Remove properties from props if they are not going to change (as determined
 * by comparison with origprops). Remove them from origprops as well, since we
 * do not need to clear or restore properties that won't change.
 */
static void
props_reduce(nvlist_t *props, nvlist_t *origprops)
{
	nvpair_t *pair, *next_pair;

	if (origprops == NULL)
		return; /* all props need to be received */

	pair = nvlist_next_nvpair(props, NULL);
	while (pair != NULL) {
		const char *propname = nvpair_name(pair);
		nvpair_t *match;

		next_pair = nvlist_next_nvpair(props, pair);

		if ((nvlist_lookup_nvpair(origprops, propname,
		    &match) != 0) || !propval_equals(pair, match))
			goto next; /* need to set received value */

		/* don't clear the existing received value */
		(void) nvlist_remove_nvpair(origprops, match);
		/* don't bother receiving the property */
		(void) nvlist_remove_nvpair(props, pair);
next:
		pair = next_pair;
	}
}

#ifdef	DEBUG
static boolean_t zfs_ioc_recv_inject_err;
#endif

/*
 * inputs:
 * zc_name		name of containing filesystem
 * zc_nvlist_src{_size}	nvlist of properties to apply
 * zc_value		name of snapshot to create
 * zc_string		name of clone origin (if DRR_FLAG_CLONE)
 * zc_cookie		file descriptor to recv from
 * zc_begin_record	the BEGIN record of the stream (not byteswapped)
 * zc_guid		force flag
 *
 * outputs:
 * zc_cookie		number of bytes read
 * zc_nvlist_dst{_size} error for each unapplied received property
 * zc_obj		zprop_errflags_t
 */
static int
zfs_ioc_recv(zfs_cmd_t *zc)
{
	file_t *fp;
	objset_t *os;
	dmu_recv_cookie_t drc;
	boolean_t force = (boolean_t)zc->zc_guid;
	int fd;
	int error = 0;
	int props_error = 0;
	nvlist_t *errors;
	offset_t off;
	nvlist_t *props = NULL; /* sent properties */
	nvlist_t *origprops = NULL; /* existing properties */
	objset_t *origin = NULL;
	char *tosnap;
	char tofs[ZFS_MAXNAMELEN];
	boolean_t first_recvd_props = B_FALSE;

	if (dataset_namecheck(zc->zc_value, NULL, NULL) != 0 ||
	    strchr(zc->zc_value, '@') == NULL ||
	    strchr(zc->zc_value, '%'))
		return (EINVAL);

	(void) strcpy(tofs, zc->zc_value);
	tosnap = strchr(tofs, '@');
	*tosnap++ = '\0';

	if (zc->zc_nvlist_src != NULL &&
	    (error = get_nvlist(zc->zc_nvlist_src, zc->zc_nvlist_src_size,
	    zc->zc_iflags, &props)) != 0)
		return (error);

	fd = zc->zc_cookie;
	fp = getf(fd);
	if (fp == NULL) {
		nvlist_free(props);
		return (EBADF);
	}

	VERIFY(nvlist_alloc(&errors, NV_UNIQUE_NAME, KM_SLEEP) == 0);

	if (props && dmu_objset_hold(tofs, FTAG, &os) == 0) {
		if ((spa_version(os->os_spa) >= SPA_VERSION_RECVD_PROPS) &&
		    !dsl_prop_get_hasrecvd(os)) {
			first_recvd_props = B_TRUE;
		}

		/*
		 * If new received properties are supplied, they are to
		 * completely replace the existing received properties, so stash
		 * away the existing ones.
		 */
		if (dsl_prop_get_received(os, &origprops) == 0) {
			nvlist_t *errlist = NULL;
			/*
			 * Don't bother writing a property if its value won't
			 * change (and avoid the unnecessary security checks).
			 *
			 * The first receive after SPA_VERSION_RECVD_PROPS is a
			 * special case where we blow away all local properties
			 * regardless.
			 */
			if (!first_recvd_props)
				props_reduce(props, origprops);
			if (zfs_check_clearable(tofs, origprops,
			    &errlist) != 0)
				(void) nvlist_merge(errors, errlist, 0);
			nvlist_free(errlist);
		}

		dmu_objset_rele(os, FTAG);
	}

	if (zc->zc_string[0]) {
		error = dmu_objset_hold(zc->zc_string, FTAG, &origin);
		if (error)
			goto out;
	}

	error = dmu_recv_begin(tofs, tosnap, zc->zc_top_ds,
	    &zc->zc_begin_record, force, origin, &drc);
	if (origin)
		dmu_objset_rele(origin, FTAG);
	if (error)
		goto out;

	/*
	 * Set properties before we receive the stream so that they are applied
	 * to the new data. Note that we must call dmu_recv_stream() if
	 * dmu_recv_begin() succeeds.
	 */
	if (props) {
		nvlist_t *errlist;

		if (dmu_objset_from_ds(drc.drc_logical_ds, &os) == 0) {
			if (drc.drc_newfs) {
				if (spa_version(os->os_spa) >=
				    SPA_VERSION_RECVD_PROPS)
					first_recvd_props = B_TRUE;
			} else if (origprops != NULL) {
				if (clear_received_props(os, tofs, origprops,
				    first_recvd_props ? NULL : props) != 0)
					zc->zc_obj |= ZPROP_ERR_NOCLEAR;
			} else {
				zc->zc_obj |= ZPROP_ERR_NOCLEAR;
			}
			dsl_prop_set_hasrecvd(os);
		} else if (!drc.drc_newfs) {
			zc->zc_obj |= ZPROP_ERR_NOCLEAR;
		}

		(void) zfs_set_prop_nvlist(tofs, ZPROP_SRC_RECEIVED,
		    props, &errlist);
		(void) nvlist_merge(errors, errlist, 0);
		nvlist_free(errlist);
	}

	if (fit_error_list(zc, &errors) != 0 || put_nvlist(zc, errors) != 0) {
		/*
		 * Caller made zc->zc_nvlist_dst less than the minimum expected
		 * size or supplied an invalid address.
		 */
		props_error = EINVAL;
	}

	off = fp->f_offset;
	error = dmu_recv_stream(&drc, fp->f_vnode, &off);

	if (error == 0) {
		zfsvfs_t *zfsvfs = NULL;

		if (getzfsvfs(tofs, &zfsvfs) == 0) {
			/* online recv */
			int end_err;

			error = zfs_suspend_fs(zfsvfs);
			/*
			 * If the suspend fails, then the recv_end will
			 * likely also fail, and clean up after itself.
			 */
			end_err = dmu_recv_end(&drc);
			if (error == 0)
				error = zfs_resume_fs(zfsvfs, tofs);
			error = error ? error : end_err;
			VFS_RELE(zfsvfs->z_vfs);
		} else {
			error = dmu_recv_end(&drc);
		}
	}

	zc->zc_cookie = off - fp->f_offset;
	if (VOP_SEEK(fp->f_vnode, fp->f_offset, &off, NULL) == 0)
		fp->f_offset = off;

#ifdef	DEBUG
	if (zfs_ioc_recv_inject_err) {
		zfs_ioc_recv_inject_err = B_FALSE;
		error = 1;
	}
#endif
	/*
	 * On error, restore the original props.
	 */
	if (error && props) {
		if (dmu_objset_hold(tofs, FTAG, &os) == 0) {
			if (clear_received_props(os, tofs, props, NULL) != 0) {
				/*
				 * We failed to clear the received properties.
				 * Since we may have left a $recvd value on the
				 * system, we can't clear the $hasrecvd flag.
				 */
				zc->zc_obj |= ZPROP_ERR_NORESTORE;
			} else if (first_recvd_props) {
				dsl_prop_unset_hasrecvd(os);
			}
			dmu_objset_rele(os, FTAG);
		} else if (!drc.drc_newfs) {
			/* We failed to clear the received properties. */
			zc->zc_obj |= ZPROP_ERR_NORESTORE;
		}

		if (origprops == NULL && !drc.drc_newfs) {
			/* We failed to stash the original properties. */
			zc->zc_obj |= ZPROP_ERR_NORESTORE;
		}

		/*
		 * dsl_props_set() will not convert RECEIVED to LOCAL on or
		 * after SPA_VERSION_RECVD_PROPS, so we need to specify LOCAL
		 * explictly if we're restoring local properties cleared in the
		 * first new-style receive.
		 */
		if (origprops != NULL &&
		    zfs_set_prop_nvlist(tofs, (first_recvd_props ?
		    ZPROP_SRC_LOCAL : ZPROP_SRC_RECEIVED),
		    origprops, NULL) != 0) {
			/*
			 * We stashed the original properties but failed to
			 * restore them.
			 */
			zc->zc_obj |= ZPROP_ERR_NORESTORE;
		}
	}
out:
	nvlist_free(props);
	nvlist_free(origprops);
	nvlist_free(errors);
	releasef(fd);

	if (error == 0)
		error = props_error;

	return (error);
}

/*
 * inputs:
 * zc_name	name of snapshot to send
 * zc_value	short name of incremental fromsnap (may be empty)
 * zc_cookie	file descriptor to send stream to
 * zc_obj	fromorigin flag (mutually exclusive with zc_value)
 *
 * outputs: none
 */
static int
zfs_ioc_send(zfs_cmd_t *zc)
{
	objset_t *fromsnap = NULL;
	objset_t *tosnap;
	file_t *fp;
	int error;
	offset_t off;

	error = dmu_objset_hold(zc->zc_name, FTAG, &tosnap);
	if (error)
		return (error);

	if (zc->zc_value[0] != '\0') {
		char *buf;
		char *cp;

		buf = kmem_alloc(MAXPATHLEN, KM_SLEEP);
		(void) strncpy(buf, zc->zc_name, MAXPATHLEN);
		cp = strchr(buf, '@');
		if (cp)
			*(cp+1) = 0;
		(void) strncat(buf, zc->zc_value, MAXPATHLEN);
		error = dmu_objset_hold(buf, FTAG, &fromsnap);
		kmem_free(buf, MAXPATHLEN);
		if (error) {
			dmu_objset_rele(tosnap, FTAG);
			return (error);
		}
	}

	fp = getf(zc->zc_cookie);
	if (fp == NULL) {
		dmu_objset_rele(tosnap, FTAG);
		if (fromsnap)
			dmu_objset_rele(fromsnap, FTAG);
		return (EBADF);
	}

	off = fp->f_offset;
	error = dmu_sendbackup(tosnap, fromsnap, zc->zc_obj, fp->f_vnode, &off);

	if (VOP_SEEK(fp->f_vnode, fp->f_offset, &off, NULL) == 0)
		fp->f_offset = off;
	releasef(zc->zc_cookie);
	if (fromsnap)
		dmu_objset_rele(fromsnap, FTAG);
	dmu_objset_rele(tosnap, FTAG);
	return (error);
}

static int
zfs_ioc_inject_fault(zfs_cmd_t *zc)
{
	int id, error;

	error = zio_inject_fault(zc->zc_name, (int)zc->zc_guid, &id,
	    &zc->zc_inject_record);

	if (error == 0)
		zc->zc_guid = (uint64_t)id;

	return (error);
}

static int
zfs_ioc_clear_fault(zfs_cmd_t *zc)
{
	return (zio_clear_fault((int)zc->zc_guid));
}

static int
zfs_ioc_inject_list_next(zfs_cmd_t *zc)
{
	int id = (int)zc->zc_guid;
	int error;

	error = zio_inject_list_next(&id, zc->zc_name, sizeof (zc->zc_name),
	    &zc->zc_inject_record);

	zc->zc_guid = id;

	return (error);
}

static int
zfs_ioc_error_log(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;
	size_t count = (size_t)zc->zc_nvlist_dst_size;

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0)
		return (error);

	error = spa_get_errlog(spa, (void *)(uintptr_t)zc->zc_nvlist_dst,
	    &count);
	if (error == 0)
		zc->zc_nvlist_dst_size = count;
	else
		zc->zc_nvlist_dst_size = spa_get_errlog_size(spa);

	spa_close(spa, FTAG);

	return (error);
}

static int
zfs_ioc_clear(zfs_cmd_t *zc)
{
	spa_t *spa;
	vdev_t *vd;
	int error;

	/*
	 * On zpool clear we also fix up missing slogs
	 */
	mutex_enter(&spa_namespace_lock);
	spa = spa_lookup(zc->zc_name);
	if (spa == NULL) {
		mutex_exit(&spa_namespace_lock);
		return (EIO);
	}
	if (spa_get_log_state(spa) == SPA_LOG_MISSING) {
		/* we need to let spa_open/spa_load clear the chains */
		spa_set_log_state(spa, SPA_LOG_CLEAR);
	}
	spa->spa_last_open_failed = 0;
	mutex_exit(&spa_namespace_lock);

	if (zc->zc_cookie & ZPOOL_NO_REWIND) {
		error = spa_open(zc->zc_name, &spa, FTAG);
	} else {
		nvlist_t *policy;
		nvlist_t *config = NULL;

		if (zc->zc_nvlist_src == NULL)
			return (EINVAL);

		if ((error = get_nvlist(zc->zc_nvlist_src,
		    zc->zc_nvlist_src_size, zc->zc_iflags, &policy)) == 0) {
			error = spa_open_rewind(zc->zc_name, &spa, FTAG,
			    policy, &config);
			if (config != NULL) {
				(void) put_nvlist(zc, config);
				nvlist_free(config);
			}
			nvlist_free(policy);
		}
	}

	if (error)
		return (error);

	spa_vdev_state_enter(spa, SCL_NONE);

	if (zc->zc_guid == 0) {
		vd = NULL;
	} else {
		vd = spa_lookup_by_guid(spa, zc->zc_guid, B_TRUE);
		if (vd == NULL) {
			(void) spa_vdev_state_exit(spa, NULL, ENODEV);
			spa_close(spa, FTAG);
			return (ENODEV);
		}
	}

	vdev_clear(spa, vd);

	(void) spa_vdev_state_exit(spa, NULL, 0);

	/*
	 * Resume any suspended I/Os.
	 */
	if (zio_resume(spa) != 0)
		error = EIO;

	spa_close(spa, FTAG);

	return (error);
}

/*
 * inputs:
 * zc_name	name of filesystem
 * zc_value	name of origin snapshot
 *
 * outputs:
 * zc_string	name of conflicting snapshot, if there is one
 */
static int
zfs_ioc_promote(zfs_cmd_t *zc)
{
	char *cp;

	/*
	 * We don't need to unmount *all* the origin fs's snapshots, but
	 * it's easier.
	 */
	cp = strchr(zc->zc_value, '@');
	if (cp)
		*cp = '\0';
	(void) dmu_objset_find(zc->zc_value,
	    zfs_unmount_snap, NULL, DS_FIND_SNAPSHOTS);
	return (dsl_dataset_promote(zc->zc_name, zc->zc_string));
}

/*
 * Retrieve a single {user|group}{used|quota}@... property.
 *
 * inputs:
 * zc_name	name of filesystem
 * zc_objset_type zfs_userquota_prop_t
 * zc_value	domain name (eg. "S-1-234-567-89")
 * zc_guid	RID/UID/GID
 *
 * outputs:
 * zc_cookie	property value
 */
static int
zfs_ioc_userspace_one(zfs_cmd_t *zc)
{
	zfsvfs_t *zfsvfs;
	int error;

	if (zc->zc_objset_type >= ZFS_NUM_USERQUOTA_PROPS)
		return (EINVAL);

	error = zfsvfs_hold(zc->zc_name, FTAG, &zfsvfs);
	if (error)
		return (error);

	error = zfs_userspace_one(zfsvfs,
	    zc->zc_objset_type, zc->zc_value, zc->zc_guid, &zc->zc_cookie);
	zfsvfs_rele(zfsvfs, FTAG);

	return (error);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_cookie		zap cursor
 * zc_objset_type	zfs_userquota_prop_t
 * zc_nvlist_dst[_size] buffer to fill (not really an nvlist)
 *
 * outputs:
 * zc_nvlist_dst[_size]	data buffer (array of zfs_useracct_t)
 * zc_cookie	zap cursor
 */
static int
zfs_ioc_userspace_many(zfs_cmd_t *zc)
{
	zfsvfs_t *zfsvfs;
	int bufsize = zc->zc_nvlist_dst_size;

	if (bufsize <= 0)
		return (ENOMEM);

	int error = zfsvfs_hold(zc->zc_name, FTAG, &zfsvfs);
	if (error)
		return (error);

	void *buf = kmem_alloc(bufsize, KM_SLEEP);

	error = zfs_userspace_many(zfsvfs, zc->zc_objset_type, &zc->zc_cookie,
	    buf, &zc->zc_nvlist_dst_size);

	if (error == 0) {
		error = xcopyout(buf,
		    (void *)(uintptr_t)zc->zc_nvlist_dst,
		    zc->zc_nvlist_dst_size);
	}
	kmem_free(buf, bufsize);
	zfsvfs_rele(zfsvfs, FTAG);

	return (error);
}

/*
 * inputs:
 * zc_name		name of filesystem
 *
 * outputs:
 * none
 */
static int
zfs_ioc_userspace_upgrade(zfs_cmd_t *zc)
{
	objset_t *os;
	int error = 0;
	zfsvfs_t *zfsvfs;

	if (getzfsvfs(zc->zc_name, &zfsvfs) == 0) {
		if (!dmu_objset_userused_enabled(zfsvfs->z_os)) {
			/*
			 * If userused is not enabled, it may be because the
			 * objset needs to be closed & reopened (to grow the
			 * objset_phys_t).  Suspend/resume the fs will do that.
			 */
			error = zfs_suspend_fs(zfsvfs);
			if (error == 0)
				error = zfs_resume_fs(zfsvfs, zc->zc_name);
		}
		if (error == 0)
			error = dmu_objset_userspace_upgrade(zfsvfs->z_os);
		VFS_RELE(zfsvfs->z_vfs);
	} else {
		/* XXX kind of reading contents without owning */
		error = dmu_objset_hold(zc->zc_name, FTAG, &os);
		if (error)
			return (error);

		error = dmu_objset_userspace_upgrade(os);
		dmu_objset_rele(os, FTAG);
	}

	return (error);
}

/*
 * We don't want to have a hard dependency
 * against some special symbols in sharefs
 * nfs, and smbsrv.  Determine them if needed when
 * the first file system is shared.
 * Neither sharefs, nfs or smbsrv are unloadable modules.
 */
int (*znfsexport_fs)(void *arg);
int (*zshare_fs)(enum sharefs_sys_op, share_t *, uint32_t);
int (*zsmbexport_fs)(void *arg, boolean_t add_share);

int zfs_nfsshare_inited;
int zfs_smbshare_inited;

ddi_modhandle_t nfs_mod;
ddi_modhandle_t sharefs_mod;
ddi_modhandle_t smbsrv_mod;
kmutex_t zfs_share_lock;

static int
zfs_init_sharefs()
{
	int error;

	ASSERT(MUTEX_HELD(&zfs_share_lock));
	/* Both NFS and SMB shares also require sharetab support. */
	if (sharefs_mod == NULL && ((sharefs_mod =
	    ddi_modopen("fs/sharefs",
	    KRTLD_MODE_FIRST, &error)) == NULL)) {
		return (ENOSYS);
	}
	if (zshare_fs == NULL && ((zshare_fs =
	    (int (*)(enum sharefs_sys_op, share_t *, uint32_t))
	    ddi_modsym(sharefs_mod, "sharefs_impl", &error)) == NULL)) {
		return (ENOSYS);
	}
	return (0);
}

static int
zfs_ioc_share(zfs_cmd_t *zc)
{
	int error;
	int opcode;

	switch (zc->zc_share.z_sharetype) {
	case ZFS_SHARE_NFS:
	case ZFS_UNSHARE_NFS:
		if (zfs_nfsshare_inited == 0) {
			mutex_enter(&zfs_share_lock);
			if (nfs_mod == NULL && ((nfs_mod = ddi_modopen("fs/nfs",
			    KRTLD_MODE_FIRST, &error)) == NULL)) {
				mutex_exit(&zfs_share_lock);
				return (ENOSYS);
			}
			if (znfsexport_fs == NULL &&
			    ((znfsexport_fs = (int (*)(void *))
			    ddi_modsym(nfs_mod,
			    "nfs_export", &error)) == NULL)) {
				mutex_exit(&zfs_share_lock);
				return (ENOSYS);
			}
			error = zfs_init_sharefs();
			if (error) {
				mutex_exit(&zfs_share_lock);
				return (ENOSYS);
			}
			zfs_nfsshare_inited = 1;
			mutex_exit(&zfs_share_lock);
		}
		break;
	case ZFS_SHARE_SMB:
	case ZFS_UNSHARE_SMB:
		if (zfs_smbshare_inited == 0) {
			mutex_enter(&zfs_share_lock);
			if (smbsrv_mod == NULL && ((smbsrv_mod =
			    ddi_modopen("drv/smbsrv",
			    KRTLD_MODE_FIRST, &error)) == NULL)) {
				mutex_exit(&zfs_share_lock);
				return (ENOSYS);
			}
			if (zsmbexport_fs == NULL && ((zsmbexport_fs =
			    (int (*)(void *, boolean_t))ddi_modsym(smbsrv_mod,
			    "smb_server_share", &error)) == NULL)) {
				mutex_exit(&zfs_share_lock);
				return (ENOSYS);
			}
			error = zfs_init_sharefs();
			if (error) {
				mutex_exit(&zfs_share_lock);
				return (ENOSYS);
			}
			zfs_smbshare_inited = 1;
			mutex_exit(&zfs_share_lock);
		}
		break;
	default:
		return (EINVAL);
	}

	switch (zc->zc_share.z_sharetype) {
	case ZFS_SHARE_NFS:
	case ZFS_UNSHARE_NFS:
		if (error =
		    znfsexport_fs((void *)
		    (uintptr_t)zc->zc_share.z_exportdata))
			return (error);
		break;
	case ZFS_SHARE_SMB:
	case ZFS_UNSHARE_SMB:
		if (error = zsmbexport_fs((void *)
		    (uintptr_t)zc->zc_share.z_exportdata,
		    zc->zc_share.z_sharetype == ZFS_SHARE_SMB ?
		    B_TRUE: B_FALSE)) {
			return (error);
		}
		break;
	}

	opcode = (zc->zc_share.z_sharetype == ZFS_SHARE_NFS ||
	    zc->zc_share.z_sharetype == ZFS_SHARE_SMB) ?
	    SHAREFS_ADD : SHAREFS_REMOVE;

	/*
	 * Add or remove share from sharetab
	 */
	error = zshare_fs(opcode,
	    (void *)(uintptr_t)zc->zc_share.z_sharedata,
	    zc->zc_share.z_sharemax);

	return (error);

}

ace_t full_access[] = {
	{(uid_t)-1, ACE_ALL_PERMS, ACE_EVERYONE, 0}
};

/*
 * Remove all ACL files in shares dir
 */
static int
zfs_smb_acl_purge(znode_t *dzp)
{
	zap_cursor_t	zc;
	zap_attribute_t	zap;
	zfsvfs_t *zfsvfs = dzp->z_zfsvfs;
	int error;

	for (zap_cursor_init(&zc, zfsvfs->z_os, dzp->z_id);
	    (error = zap_cursor_retrieve(&zc, &zap)) == 0;
	    zap_cursor_advance(&zc)) {
		if ((error = VOP_REMOVE(ZTOV(dzp), zap.za_name, kcred,
		    NULL, 0)) != 0)
			break;
	}
	zap_cursor_fini(&zc);
	return (error);
}

static int
zfs_ioc_smb_acl(zfs_cmd_t *zc)
{
	vnode_t *vp;
	znode_t *dzp;
	vnode_t *resourcevp = NULL;
	znode_t *sharedir;
	zfsvfs_t *zfsvfs;
	nvlist_t *nvlist;
	char *src, *target;
	vattr_t vattr;
	vsecattr_t vsec;
	int error = 0;

	if ((error = lookupname(zc->zc_value, UIO_SYSSPACE,
	    NO_FOLLOW, NULL, &vp)) != 0)
		return (error);

	/* Now make sure mntpnt and dataset are ZFS */

	if (vp->v_vfsp->vfs_fstype != zfsfstype ||
	    (strcmp((char *)refstr_value(vp->v_vfsp->vfs_resource),
	    zc->zc_name) != 0)) {
		VN_RELE(vp);
		return (EINVAL);
	}

	dzp = VTOZ(vp);
	zfsvfs = dzp->z_zfsvfs;
	ZFS_ENTER(zfsvfs);

	/*
	 * Create share dir if its missing.
	 */
	mutex_enter(&zfsvfs->z_lock);
	if (zfsvfs->z_shares_dir == 0) {
		dmu_tx_t *tx;

		tx = dmu_tx_create(zfsvfs->z_os);
		dmu_tx_hold_zap(tx, MASTER_NODE_OBJ, TRUE,
		    ZFS_SHARES_DIR);
		dmu_tx_hold_zap(tx, DMU_NEW_OBJECT, FALSE, NULL);
		error = dmu_tx_assign(tx, TXG_WAIT);
		if (error) {
			dmu_tx_abort(tx);
		} else {
			error = zfs_create_share_dir(zfsvfs, tx);
			dmu_tx_commit(tx);
		}
		if (error) {
			mutex_exit(&zfsvfs->z_lock);
			VN_RELE(vp);
			ZFS_EXIT(zfsvfs);
			return (error);
		}
	}
	mutex_exit(&zfsvfs->z_lock);

	ASSERT(zfsvfs->z_shares_dir);
	if ((error = zfs_zget(zfsvfs, zfsvfs->z_shares_dir, &sharedir)) != 0) {
		VN_RELE(vp);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	switch (zc->zc_cookie) {
	case ZFS_SMB_ACL_ADD:
		vattr.va_mask = AT_MODE|AT_UID|AT_GID|AT_TYPE;
		vattr.va_type = VREG;
		vattr.va_mode = S_IFREG|0777;
		vattr.va_uid = 0;
		vattr.va_gid = 0;

		vsec.vsa_mask = VSA_ACE;
		vsec.vsa_aclentp = &full_access;
		vsec.vsa_aclentsz = sizeof (full_access);
		vsec.vsa_aclcnt = 1;

		error = VOP_CREATE(ZTOV(sharedir), zc->zc_string,
		    &vattr, EXCL, 0, &resourcevp, kcred, 0, NULL, &vsec);
		if (resourcevp)
			VN_RELE(resourcevp);
		break;

	case ZFS_SMB_ACL_REMOVE:
		error = VOP_REMOVE(ZTOV(sharedir), zc->zc_string, kcred,
		    NULL, 0);
		break;

	case ZFS_SMB_ACL_RENAME:
		if ((error = get_nvlist(zc->zc_nvlist_src,
		    zc->zc_nvlist_src_size, zc->zc_iflags, &nvlist)) != 0) {
			VN_RELE(vp);
			ZFS_EXIT(zfsvfs);
			return (error);
		}
		if (nvlist_lookup_string(nvlist, ZFS_SMB_ACL_SRC, &src) ||
		    nvlist_lookup_string(nvlist, ZFS_SMB_ACL_TARGET,
		    &target)) {
			VN_RELE(vp);
			VN_RELE(ZTOV(sharedir));
			ZFS_EXIT(zfsvfs);
			nvlist_free(nvlist);
			return (error);
		}
		error = VOP_RENAME(ZTOV(sharedir), src, ZTOV(sharedir), target,
		    kcred, NULL, 0);
		nvlist_free(nvlist);
		break;

	case ZFS_SMB_ACL_PURGE:
		error = zfs_smb_acl_purge(sharedir);
		break;

	default:
		error = EINVAL;
		break;
	}

	VN_RELE(vp);
	VN_RELE(ZTOV(sharedir));

	ZFS_EXIT(zfsvfs);

	return (error);
}

/*
 * inputs:
 * zc_name	name of filesystem
 * zc_value	short name of snap
 * zc_string	user-supplied tag for this reference
 * zc_cookie	recursive flag
 * zc_temphold	set if hold is temporary
 *
 * outputs:		none
 */
static int
zfs_ioc_hold(zfs_cmd_t *zc)
{
	boolean_t recursive = zc->zc_cookie;

	if (snapshot_namecheck(zc->zc_value, NULL, NULL) != 0)
		return (EINVAL);

	return (dsl_dataset_user_hold(zc->zc_name, zc->zc_value,
	    zc->zc_string, recursive, zc->zc_temphold));
}

/*
 * inputs:
 * zc_name	name of dataset from which we're releasing a user reference
 * zc_value	short name of snap
 * zc_string	user-supplied tag for this reference
 * zc_cookie	recursive flag
 *
 * outputs:		none
 */
static int
zfs_ioc_release(zfs_cmd_t *zc)
{
	boolean_t recursive = zc->zc_cookie;

	if (snapshot_namecheck(zc->zc_value, NULL, NULL) != 0)
		return (EINVAL);

	return (dsl_dataset_user_release(zc->zc_name, zc->zc_value,
	    zc->zc_string, recursive));
}

/*
 * inputs:
 * zc_name		name of filesystem
 *
 * outputs:
 * zc_nvlist_src{_size}	nvlist of snapshot holds
 */
static int
zfs_ioc_get_holds(zfs_cmd_t *zc)
{
	nvlist_t *nvp;
	int error;

	if ((error = dsl_dataset_get_holds(zc->zc_name, &nvp)) == 0) {
		error = put_nvlist(zc, nvp);
		nvlist_free(nvp);
	}

	return (error);
}

/*
 * pool create, destroy, and export don't log the history as part of
 * zfsdev_ioctl, but rather zfs_ioc_pool_create, and zfs_ioc_pool_export
 * do the logging of those commands.
 */
static zfs_ioc_vec_t zfs_ioc_vec[] = {
	{ zfs_ioc_pool_create, zfs_secpolicy_config, POOL_NAME, B_FALSE,
	    B_FALSE },
	{ zfs_ioc_pool_destroy,	zfs_secpolicy_config, POOL_NAME, B_FALSE,
	    B_FALSE },
	{ zfs_ioc_pool_import, zfs_secpolicy_config, POOL_NAME, B_TRUE,
	    B_FALSE },
	{ zfs_ioc_pool_export, zfs_secpolicy_config, POOL_NAME, B_FALSE,
	    B_FALSE },
	{ zfs_ioc_pool_configs,	zfs_secpolicy_none, NO_NAME, B_FALSE,
	    B_FALSE },
	{ zfs_ioc_pool_stats, zfs_secpolicy_read, POOL_NAME, B_FALSE,
	    B_FALSE },
	{ zfs_ioc_pool_tryimport, zfs_secpolicy_config, NO_NAME, B_FALSE,
	    B_FALSE },
	{ zfs_ioc_pool_scan, zfs_secpolicy_config, POOL_NAME, B_TRUE,
	    B_TRUE },
	{ zfs_ioc_pool_freeze, zfs_secpolicy_config, NO_NAME, B_FALSE,
	    B_FALSE },
	{ zfs_ioc_pool_upgrade,	zfs_secpolicy_config, POOL_NAME, B_TRUE,
	    B_TRUE },
	{ zfs_ioc_pool_get_history, zfs_secpolicy_config, POOL_NAME, B_FALSE,
	    B_FALSE },
	{ zfs_ioc_vdev_add, zfs_secpolicy_config, POOL_NAME, B_TRUE,
	    B_TRUE },
	{ zfs_ioc_vdev_remove, zfs_secpolicy_config, POOL_NAME, B_TRUE,
	    B_TRUE },
	{ zfs_ioc_vdev_set_state, zfs_secpolicy_config,	POOL_NAME, B_TRUE,
	    B_FALSE },
	{ zfs_ioc_vdev_attach, zfs_secpolicy_config, POOL_NAME, B_TRUE,
	    B_TRUE },
	{ zfs_ioc_vdev_detach, zfs_secpolicy_config, POOL_NAME, B_TRUE,
	    B_TRUE },
	{ zfs_ioc_vdev_setpath,	zfs_secpolicy_config, POOL_NAME, B_FALSE,
	    B_TRUE },
	{ zfs_ioc_vdev_setfru,	zfs_secpolicy_config, POOL_NAME, B_FALSE,
	    B_TRUE },
	{ zfs_ioc_objset_stats,	zfs_secpolicy_read, DATASET_NAME, B_FALSE,
	    B_TRUE },
	{ zfs_ioc_objset_zplprops, zfs_secpolicy_read, DATASET_NAME, B_FALSE,
	    B_FALSE },
	{ zfs_ioc_dataset_list_next, zfs_secpolicy_read, DATASET_NAME, B_FALSE,
	    B_TRUE },
	{ zfs_ioc_snapshot_list_next, zfs_secpolicy_read, DATASET_NAME, B_FALSE,
	    B_TRUE },
	{ zfs_ioc_set_prop, zfs_secpolicy_none, DATASET_NAME, B_TRUE, B_TRUE },
	{ zfs_ioc_create, zfs_secpolicy_create, DATASET_NAME, B_TRUE, B_TRUE },
	{ zfs_ioc_destroy, zfs_secpolicy_destroy, DATASET_NAME, B_TRUE,
	    B_TRUE},
	{ zfs_ioc_rollback, zfs_secpolicy_rollback, DATASET_NAME, B_TRUE,
	    B_TRUE },
	{ zfs_ioc_rename, zfs_secpolicy_rename,	DATASET_NAME, B_TRUE, B_TRUE },
	{ zfs_ioc_recv, zfs_secpolicy_receive, DATASET_NAME, B_TRUE, B_TRUE },
	{ zfs_ioc_send, zfs_secpolicy_send, DATASET_NAME, B_TRUE, B_FALSE },
	{ zfs_ioc_inject_fault,	zfs_secpolicy_inject, NO_NAME, B_FALSE,
	    B_FALSE },
	{ zfs_ioc_clear_fault, zfs_secpolicy_inject, NO_NAME, B_FALSE,
	    B_FALSE },
	{ zfs_ioc_inject_list_next, zfs_secpolicy_inject, NO_NAME, B_FALSE,
	    B_FALSE },
	{ zfs_ioc_error_log, zfs_secpolicy_inject, POOL_NAME, B_FALSE,
	    B_FALSE },
	{ zfs_ioc_clear, zfs_secpolicy_config, POOL_NAME, B_TRUE, B_FALSE },
	{ zfs_ioc_promote, zfs_secpolicy_promote, DATASET_NAME, B_TRUE,
	    B_TRUE },
	{ zfs_ioc_destroy_snaps, zfs_secpolicy_destroy_snaps, DATASET_NAME,
	    B_TRUE, B_TRUE },
	{ zfs_ioc_snapshot, zfs_secpolicy_snapshot, DATASET_NAME, B_TRUE,
	    B_TRUE },
	{ zfs_ioc_dsobj_to_dsname, zfs_secpolicy_config, POOL_NAME, B_FALSE,
	    B_FALSE },
	{ zfs_ioc_obj_to_path, zfs_secpolicy_config, DATASET_NAME, B_FALSE,
	    B_TRUE },
	{ zfs_ioc_pool_set_props, zfs_secpolicy_config,	POOL_NAME, B_TRUE,
	    B_TRUE },
	{ zfs_ioc_pool_get_props, zfs_secpolicy_read, POOL_NAME, B_FALSE,
	    B_FALSE },
	{ zfs_ioc_set_fsacl, zfs_secpolicy_fsacl, DATASET_NAME, B_TRUE,
	    B_TRUE },
	{ zfs_ioc_get_fsacl, zfs_secpolicy_read, DATASET_NAME, B_FALSE,
	    B_FALSE },
	{ zfs_ioc_share, zfs_secpolicy_share, DATASET_NAME, B_FALSE, B_FALSE },
	{ zfs_ioc_inherit_prop, zfs_secpolicy_inherit, DATASET_NAME, B_TRUE,
	    B_TRUE },
	{ zfs_ioc_smb_acl, zfs_secpolicy_smb_acl, DATASET_NAME, B_FALSE,
	    B_FALSE },
	{ zfs_ioc_userspace_one, zfs_secpolicy_userspace_one,
	    DATASET_NAME, B_FALSE, B_FALSE },
	{ zfs_ioc_userspace_many, zfs_secpolicy_userspace_many,
	    DATASET_NAME, B_FALSE, B_FALSE },
	{ zfs_ioc_userspace_upgrade, zfs_secpolicy_userspace_upgrade,
	    DATASET_NAME, B_FALSE, B_TRUE },
	{ zfs_ioc_hold, zfs_secpolicy_hold, DATASET_NAME, B_TRUE, B_TRUE },
	{ zfs_ioc_release, zfs_secpolicy_release, DATASET_NAME, B_TRUE,
	    B_TRUE },
	{ zfs_ioc_get_holds, zfs_secpolicy_read, DATASET_NAME, B_FALSE,
	    B_TRUE },
	{ zfs_ioc_objset_recvd_props, zfs_secpolicy_read, DATASET_NAME, B_FALSE,
	    B_FALSE },
	{ zfs_ioc_vdev_split, zfs_secpolicy_config, POOL_NAME, B_TRUE,
	    B_TRUE }
};

int
pool_status_check(const char *name, zfs_ioc_namecheck_t type)
{
	spa_t *spa;
	int error;

	ASSERT(type == POOL_NAME || type == DATASET_NAME);

	error = spa_open(name, &spa, FTAG);
	if (error == 0) {
		if (spa_suspended(spa))
			error = EAGAIN;
		spa_close(spa, FTAG);
	}
	return (error);
}

static int
zfsdev_ioctl(dev_t dev, int cmd, intptr_t arg, int flag, cred_t *cr, int *rvalp)
{
	zfs_cmd_t *zc;
	uint_t vec;
	int error, rc;

	if (getminor(dev) != 0)
		return (zvol_ioctl(dev, cmd, arg, flag, cr, rvalp));

	vec = cmd - ZFS_IOC;
	ASSERT3U(getmajor(dev), ==, ddi_driver_major(zfs_dip));

	if (vec >= sizeof (zfs_ioc_vec) / sizeof (zfs_ioc_vec[0]))
		return (EINVAL);

	zc = kmem_zalloc(sizeof (zfs_cmd_t), KM_SLEEP);

	error = ddi_copyin((void *)arg, zc, sizeof (zfs_cmd_t), flag);
	if (error != 0)
		error = EFAULT;

	if ((error == 0) && !(flag & FKIOCTL))
		error = zfs_ioc_vec[vec].zvec_secpolicy(zc, cr);

	/*
	 * Ensure that all pool/dataset names are valid before we pass down to
	 * the lower layers.
	 */
	if (error == 0) {
		zc->zc_name[sizeof (zc->zc_name) - 1] = '\0';
		zc->zc_iflags = flag & FKIOCTL;
		switch (zfs_ioc_vec[vec].zvec_namecheck) {
		case POOL_NAME:
			if (pool_namecheck(zc->zc_name, NULL, NULL) != 0)
				error = EINVAL;
			if (zfs_ioc_vec[vec].zvec_pool_check)
				error = pool_status_check(zc->zc_name,
				    zfs_ioc_vec[vec].zvec_namecheck);
			break;

		case DATASET_NAME:
			if (dataset_namecheck(zc->zc_name, NULL, NULL) != 0)
				error = EINVAL;
			if (zfs_ioc_vec[vec].zvec_pool_check)
				error = pool_status_check(zc->zc_name,
				    zfs_ioc_vec[vec].zvec_namecheck);
			break;

		case NO_NAME:
			break;
		}
	}

	if (error == 0)
		error = zfs_ioc_vec[vec].zvec_func(zc);

	rc = ddi_copyout(zc, (void *)arg, sizeof (zfs_cmd_t), flag);
	if (error == 0) {
		if (rc != 0)
			error = EFAULT;
		if (zfs_ioc_vec[vec].zvec_his_log)
			zfs_log_history(zc);
	}

	kmem_free(zc, sizeof (zfs_cmd_t));
	return (error);
}

static int
zfs_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);

	if (ddi_create_minor_node(dip, "zfs", S_IFCHR, 0,
	    DDI_PSEUDO, 0) == DDI_FAILURE)
		return (DDI_FAILURE);

	zfs_dip = dip;

	ddi_report_dev(dip);

	return (DDI_SUCCESS);
}

static int
zfs_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	if (spa_busy() || zfs_busy() || zvol_busy())
		return (DDI_FAILURE);

	if (cmd != DDI_DETACH)
		return (DDI_FAILURE);

	zfs_dip = NULL;

	ddi_prop_remove_all(dip);
	ddi_remove_minor_node(dip, NULL);

	return (DDI_SUCCESS);
}

/*ARGSUSED*/
static int
zfs_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		*result = zfs_dip;
		return (DDI_SUCCESS);

	case DDI_INFO_DEVT2INSTANCE:
		*result = (void *)0;
		return (DDI_SUCCESS);
	}

	return (DDI_FAILURE);
}

/*
 * OK, so this is a little weird.
 *
 * /dev/zfs is the control node, i.e. minor 0.
 * /dev/zvol/[r]dsk/pool/dataset are the zvols, minor > 0.
 *
 * /dev/zfs has basically nothing to do except serve up ioctls,
 * so most of the standard driver entry points are in zvol.c.
 */
static struct cb_ops zfs_cb_ops = {
	zvol_open,	/* open */
	zvol_close,	/* close */
	zvol_strategy,	/* strategy */
	nodev,		/* print */
	zvol_dump,	/* dump */
	zvol_read,	/* read */
	zvol_write,	/* write */
	zfsdev_ioctl,	/* ioctl */
	nodev,		/* devmap */
	nodev,		/* mmap */
	nodev,		/* segmap */
	nochpoll,	/* poll */
	ddi_prop_op,	/* prop_op */
	NULL,		/* streamtab */
	D_NEW | D_MP | D_64BIT,		/* Driver compatibility flag */
	CB_REV,		/* version */
	nodev,		/* async read */
	nodev,		/* async write */
};

static struct dev_ops zfs_dev_ops = {
	DEVO_REV,	/* version */
	0,		/* refcnt */
	zfs_info,	/* info */
	nulldev,	/* identify */
	nulldev,	/* probe */
	zfs_attach,	/* attach */
	zfs_detach,	/* detach */
	nodev,		/* reset */
	&zfs_cb_ops,	/* driver operations */
	NULL,		/* no bus operations */
	NULL,		/* power */
	ddi_quiesce_not_needed,	/* quiesce */
};

static struct modldrv zfs_modldrv = {
	&mod_driverops,
	"ZFS storage pool",
	&zfs_dev_ops
};

static struct modlinkage modlinkage = {
	MODREV_1,
	(void *)&zfs_modlfs,
	(void *)&zfs_modldrv,
	NULL
};


uint_t zfs_fsyncer_key;
extern uint_t rrw_tsd_key;

int
_init(void)
{
	int error;

	spa_init(FREAD | FWRITE);
	zfs_init();
	zvol_init();

	if ((error = mod_install(&modlinkage)) != 0) {
		zvol_fini();
		zfs_fini();
		spa_fini();
		return (error);
	}

	tsd_create(&zfs_fsyncer_key, NULL);
	tsd_create(&rrw_tsd_key, NULL);

	error = ldi_ident_from_mod(&modlinkage, &zfs_li);
	ASSERT(error == 0);
	mutex_init(&zfs_share_lock, NULL, MUTEX_DEFAULT, NULL);

	return (0);
}

int
_fini(void)
{
	int error;

	if (spa_busy() || zfs_busy() || zvol_busy() || zio_injection_enabled)
		return (EBUSY);

	if ((error = mod_remove(&modlinkage)) != 0)
		return (error);

	zvol_fini();
	zfs_fini();
	spa_fini();
	if (zfs_nfsshare_inited)
		(void) ddi_modclose(nfs_mod);
	if (zfs_smbshare_inited)
		(void) ddi_modclose(smbsrv_mod);
	if (zfs_nfsshare_inited || zfs_smbshare_inited)
		(void) ddi_modclose(sharefs_mod);

	tsd_destroy(&zfs_fsyncer_key);
	ldi_ident_release(zfs_li);
	zfs_li = NULL;
	mutex_destroy(&zfs_share_lock);

	return (error);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}
