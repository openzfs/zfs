/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Portions Copyright 2011 Martin Matuska
 * Copyright 2015, OmniTI Computer Consulting, Inc. All rights reserved.
 * Copyright (c) 2012 Pawel Jakub Dawidek
 * Copyright (c) 2014, 2016 Joyent, Inc. All rights reserved.
 * Copyright 2016 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2014, Joyent, Inc. All rights reserved.
 * Copyright (c) 2011, 2024 by Delphix. All rights reserved.
 * Copyright (c) 2013 by Saso Kiselkov. All rights reserved.
 * Copyright (c) 2013 Steven Hartland. All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 * Copyright 2016 Toomas Soome <tsoome@me.com>
 * Copyright (c) 2016 Actifio, Inc. All rights reserved.
 * Copyright (c) 2018, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
 * Copyright 2017 RackTop Systems.
 * Copyright (c) 2017 Open-E, Inc. All Rights Reserved.
 * Copyright (c) 2019 Datto Inc.
 * Copyright (c) 2019, 2020 by Christian Schwarz. All rights reserved.
 * Copyright (c) 2019, 2021, 2023, 2024, Klara Inc.
 * Copyright (c) 2019, Allan Jude
 * Copyright 2024 Oxide Computer Company
 */

/*
 * ZFS ioctls.
 *
 * This file handles the ioctls to /dev/zfs, used for configuring ZFS storage
 * pools and filesystems, e.g. with /sbin/zfs and /sbin/zpool.
 *
 * There are two ways that we handle ioctls: the legacy way where almost
 * all of the logic is in the ioctl callback, and the new way where most
 * of the marshalling is handled in the common entry point, zfsdev_ioctl().
 *
 * Non-legacy ioctls should be registered by calling
 * zfs_ioctl_register() from zfs_ioctl_init().  The ioctl is invoked
 * from userland by lzc_ioctl().
 *
 * The registration arguments are as follows:
 *
 * const char *name
 *   The name of the ioctl.  This is used for history logging.  If the
 *   ioctl returns successfully (the callback returns 0), and allow_log
 *   is true, then a history log entry will be recorded with the input &
 *   output nvlists.  The log entry can be printed with "zpool history -i".
 *
 * zfs_ioc_t ioc
 *   The ioctl request number, which userland will pass to ioctl(2).
 *   We want newer versions of libzfs and libzfs_core to run against
 *   existing zfs kernel modules (i.e. a deferred reboot after an update).
 *   Therefore the ioctl numbers cannot change from release to release.
 *
 * zfs_secpolicy_func_t *secpolicy
 *   This function will be called before the zfs_ioc_func_t, to
 *   determine if this operation is permitted.  It should return EPERM
 *   on failure, and 0 on success.  Checks include determining if the
 *   dataset is visible in this zone, and if the user has either all
 *   zfs privileges in the zone (SYS_MOUNT), or has been granted permission
 *   to do this operation on this dataset with "zfs allow".
 *
 * zfs_ioc_namecheck_t namecheck
 *   This specifies what to expect in the zfs_cmd_t:zc_name -- a pool
 *   name, a dataset name, or nothing.  If the name is not well-formed,
 *   the ioctl will fail and the callback will not be called.
 *   Therefore, the callback can assume that the name is well-formed
 *   (e.g. is null-terminated, doesn't have more than one '@' character,
 *   doesn't have invalid characters).
 *
 * zfs_ioc_poolcheck_t pool_check
 *   This specifies requirements on the pool state.  If the pool does
 *   not meet them (is suspended or is readonly), the ioctl will fail
 *   and the callback will not be called.  If any checks are specified
 *   (i.e. it is not POOL_CHECK_NONE), namecheck must not be NO_NAME.
 *   Multiple checks can be or-ed together (e.g. POOL_CHECK_SUSPENDED |
 *   POOL_CHECK_READONLY).
 *
 * zfs_ioc_key_t *nvl_keys
 *  The list of expected/allowable innvl input keys. This list is used
 *  to validate the nvlist input to the ioctl.
 *
 * boolean_t smush_outnvlist
 *   If smush_outnvlist is true, then the output is presumed to be a
 *   list of errors, and it will be "smushed" down to fit into the
 *   caller's buffer, by removing some entries and replacing them with a
 *   single "N_MORE_ERRORS" entry indicating how many were removed.  See
 *   nvlist_smush() for details.  If smush_outnvlist is false, and the
 *   outnvlist does not fit into the userland-provided buffer, then the
 *   ioctl will fail with ENOMEM.
 *
 * zfs_ioc_func_t *func
 *   The callback function that will perform the operation.
 *
 *   The callback should return 0 on success, or an error number on
 *   failure.  If the function fails, the userland ioctl will return -1,
 *   and errno will be set to the callback's return value.  The callback
 *   will be called with the following arguments:
 *
 *   const char *name
 *     The name of the pool or dataset to operate on, from
 *     zfs_cmd_t:zc_name.  The 'namecheck' argument specifies the
 *     expected type (pool, dataset, or none).
 *
 *   nvlist_t *innvl
 *     The input nvlist, deserialized from zfs_cmd_t:zc_nvlist_src.  Or
 *     NULL if no input nvlist was provided.  Changes to this nvlist are
 *     ignored.  If the input nvlist could not be deserialized, the
 *     ioctl will fail and the callback will not be called.
 *
 *   nvlist_t *outnvl
 *     The output nvlist, initially empty.  The callback can fill it in,
 *     and it will be returned to userland by serializing it into
 *     zfs_cmd_t:zc_nvlist_dst.  If it is non-empty, and serialization
 *     fails (e.g. because the caller didn't supply a large enough
 *     buffer), then the overall ioctl will fail.  See the
 *     'smush_nvlist' argument above for additional behaviors.
 *
 *     There are two typical uses of the output nvlist:
 *       - To return state, e.g. property values.  In this case,
 *         smush_outnvlist should be false.  If the buffer was not large
 *         enough, the caller will reallocate a larger buffer and try
 *         the ioctl again.
 *
 *       - To return multiple errors from an ioctl which makes on-disk
 *         changes.  In this case, smush_outnvlist should be true.
 *         Ioctls which make on-disk modifications should generally not
 *         use the outnvl if they succeed, because the caller can not
 *         distinguish between the operation failing, and
 *         deserialization failing.
 *
 * IOCTL Interface Errors
 *
 * The following ioctl input errors can be returned:
 *   ZFS_ERR_IOC_CMD_UNAVAIL	the ioctl number is not supported by kernel
 *   ZFS_ERR_IOC_ARG_UNAVAIL	an input argument is not supported by kernel
 *   ZFS_ERR_IOC_ARG_REQUIRED	a required input argument is missing
 *   ZFS_ERR_IOC_ARG_BADTYPE	an input argument has an invalid type
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/uio_impl.h>
#include <sys/file.h>
#include <sys/kmem.h>
#include <sys/cmn_err.h>
#include <sys/stat.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_quota.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_znode.h>
#include <sys/zap.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/vdev.h>
#include <sys/vdev_impl.h>
#include <sys/dmu.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_deleg.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_impl.h>
#include <sys/dmu_redact.h>
#include <sys/dmu_tx.h>
#include <sys/sunddi.h>
#include <sys/policy.h>
#include <sys/zone.h>
#include <sys/nvpair.h>
#include <sys/pathname.h>
#include <sys/fs/zfs.h>
#include <sys/zfs_ctldir.h>
#include <sys/zfs_dir.h>
#include <sys/zfs_onexit.h>
#include <sys/zvol.h>
#include <sys/dsl_scan.h>
#include <sys/fm/util.h>
#include <sys/dsl_crypt.h>
#include <sys/rrwlock.h>
#include <sys/zfs_file.h>

#include <sys/dmu_recv.h>
#include <sys/dmu_send.h>
#include <sys/dmu_recv.h>
#include <sys/dsl_destroy.h>
#include <sys/dsl_bookmark.h>
#include <sys/dsl_userhold.h>
#include <sys/zfeature.h>
#include <sys/zcp.h>
#include <sys/zio_checksum.h>
#include <sys/vdev_removal.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_initialize.h>
#include <sys/vdev_trim.h>

#include "zfs_namecheck.h"
#include "zfs_prop.h"
#include "zfs_deleg.h"
#include "zfs_comutil.h"

#include <sys/lua/lua.h>
#include <sys/lua/lauxlib.h>
#include <sys/zfs_ioctl_impl.h>

kmutex_t zfsdev_state_lock;
static zfsdev_state_t zfsdev_state_listhead;

/*
 * Limit maximum nvlist size.  We don't want users passing in insane values
 * for zc->zc_nvlist_src_size, since we will need to allocate that much memory.
 * Defaults to 0=auto which is handled by platform code.
 */
uint64_t zfs_max_nvlist_src_size = 0;

/*
 * When logging the output nvlist of an ioctl in the on-disk history, limit
 * the logged size to this many bytes.  This must be less than DMU_MAX_ACCESS.
 * This applies primarily to zfs_ioc_channel_program().
 */
static uint64_t zfs_history_output_max = 1024 * 1024;

uint_t zfs_allow_log_key;

/* DATA_TYPE_ANY is used when zkey_type can vary. */
#define	DATA_TYPE_ANY	DATA_TYPE_UNKNOWN

typedef struct zfs_ioc_vec {
	zfs_ioc_legacy_func_t	*zvec_legacy_func;
	zfs_ioc_func_t		*zvec_func;
	zfs_secpolicy_func_t	*zvec_secpolicy;
	zfs_ioc_namecheck_t	zvec_namecheck;
	boolean_t		zvec_allow_log;
	zfs_ioc_poolcheck_t	zvec_pool_check;
	boolean_t		zvec_smush_outnvlist;
	const char		*zvec_name;
	const zfs_ioc_key_t	*zvec_nvl_keys;
	size_t			zvec_nvl_key_count;
} zfs_ioc_vec_t;

/* This array is indexed by zfs_userquota_prop_t */
static const char *userquota_perms[] = {
	ZFS_DELEG_PERM_USERUSED,
	ZFS_DELEG_PERM_USERQUOTA,
	ZFS_DELEG_PERM_GROUPUSED,
	ZFS_DELEG_PERM_GROUPQUOTA,
	ZFS_DELEG_PERM_USEROBJUSED,
	ZFS_DELEG_PERM_USEROBJQUOTA,
	ZFS_DELEG_PERM_GROUPOBJUSED,
	ZFS_DELEG_PERM_GROUPOBJQUOTA,
	ZFS_DELEG_PERM_PROJECTUSED,
	ZFS_DELEG_PERM_PROJECTQUOTA,
	ZFS_DELEG_PERM_PROJECTOBJUSED,
	ZFS_DELEG_PERM_PROJECTOBJQUOTA,
};

static int zfs_ioc_userspace_upgrade(zfs_cmd_t *zc);
static int zfs_ioc_id_quota_upgrade(zfs_cmd_t *zc);
static int zfs_check_settable(const char *name, nvpair_t *property,
    cred_t *cr);
static int zfs_check_clearable(const char *dataset, nvlist_t *props,
    nvlist_t **errors);
static int zfs_fill_zplprops_root(uint64_t, nvlist_t *, nvlist_t *,
    boolean_t *);
int zfs_set_prop_nvlist(const char *, zprop_source_t, nvlist_t *, nvlist_t *);
static int get_nvlist(uint64_t nvl, uint64_t size, int iflag, nvlist_t **nvp);

static void
history_str_free(char *buf)
{
	kmem_free(buf, HIS_MAX_RECORD_LEN);
}

static char *
history_str_get(zfs_cmd_t *zc)
{
	char *buf;

	if (zc->zc_history == 0)
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
 * Return non-zero if the spa version is less than requested version.
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
			(void) spa_history_log(spa, buf);
		spa_close(spa, FTAG);
	}
	history_str_free(buf);
}

/*
 * Policy for top-level read operations (list pools).  Requires no privileges,
 * and can be used in the local zone, as there is no associated dataset.
 */
static int
zfs_secpolicy_none(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	(void) zc, (void) innvl, (void) cr;
	return (0);
}

/*
 * Policy for dataset read operations (list children, get statistics).  Requires
 * no privileges, but must be visible in the local zone.
 */
static int
zfs_secpolicy_read(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	(void) innvl, (void) cr;
	if (INGLOBALZONE(curproc) ||
	    zone_dataset_visible(zc->zc_name, NULL))
		return (0);

	return (SET_ERROR(ENOENT));
}

static int
zfs_dozonecheck_impl(const char *dataset, uint64_t zoned, cred_t *cr)
{
	int writable = 1;

	/*
	 * The dataset must be visible by this zone -- check this first
	 * so they don't see EPERM on something they shouldn't know about.
	 */
	if (!INGLOBALZONE(curproc) &&
	    !zone_dataset_visible(dataset, &writable))
		return (SET_ERROR(ENOENT));

	if (INGLOBALZONE(curproc)) {
		/*
		 * If the fs is zoned, only root can access it from the
		 * global zone.
		 */
		if (secpolicy_zfs(cr) && zoned)
			return (SET_ERROR(EPERM));
	} else {
		/*
		 * If we are in a local zone, the 'zoned' property must be set.
		 */
		if (!zoned)
			return (SET_ERROR(EPERM));

		/* must be writable by this zone */
		if (!writable)
			return (SET_ERROR(EPERM));
	}
	return (0);
}

static int
zfs_dozonecheck(const char *dataset, cred_t *cr)
{
	uint64_t zoned;

	if (dsl_prop_get_integer(dataset, zfs_prop_to_name(ZFS_PROP_ZONED),
	    &zoned, NULL))
		return (SET_ERROR(ENOENT));

	return (zfs_dozonecheck_impl(dataset, zoned, cr));
}

static int
zfs_dozonecheck_ds(const char *dataset, dsl_dataset_t *ds, cred_t *cr)
{
	uint64_t zoned;

	if (dsl_prop_get_int_ds(ds, zfs_prop_to_name(ZFS_PROP_ZONED), &zoned))
		return (SET_ERROR(ENOENT));

	return (zfs_dozonecheck_impl(dataset, zoned, cr));
}

static int
zfs_secpolicy_write_perms_ds(const char *name, dsl_dataset_t *ds,
    const char *perm, cred_t *cr)
{
	int error;

	error = zfs_dozonecheck_ds(name, ds, cr);
	if (error == 0) {
		error = secpolicy_zfs(cr);
		if (error != 0)
			error = dsl_deleg_access_impl(ds, perm, cr);
	}
	return (error);
}

static int
zfs_secpolicy_write_perms(const char *name, const char *perm, cred_t *cr)
{
	int error;
	dsl_dataset_t *ds;
	dsl_pool_t *dp;

	/*
	 * First do a quick check for root in the global zone, which
	 * is allowed to do all write_perms.  This ensures that zfs_ioc_*
	 * will get to handle nonexistent datasets.
	 */
	if (INGLOBALZONE(curproc) && secpolicy_zfs(cr) == 0)
		return (0);

	error = dsl_pool_hold(name, FTAG, &dp);
	if (error != 0)
		return (error);

	error = dsl_dataset_hold(dp, name, FTAG, &ds);
	if (error != 0) {
		dsl_pool_rele(dp, FTAG);
		return (error);
	}

	error = zfs_secpolicy_write_perms_ds(name, ds, perm, cr);

	dsl_dataset_rele(ds, FTAG);
	dsl_pool_rele(dp, FTAG);
	return (error);
}

/*
 * Policy for setting the security label property.
 *
 * Returns 0 for success, non-zero for access and other errors.
 */
static int
zfs_set_slabel_policy(const char *name, const char *strval, cred_t *cr)
{
#ifdef HAVE_MLSLABEL
	char		ds_hexsl[MAXNAMELEN];
	bslabel_t	ds_sl, new_sl;
	boolean_t	new_default = FALSE;
	uint64_t	zoned;
	int		needed_priv = -1;
	int		error;

	/* First get the existing dataset label. */
	error = dsl_prop_get(name, zfs_prop_to_name(ZFS_PROP_MLSLABEL),
	    1, sizeof (ds_hexsl), &ds_hexsl, NULL);
	if (error != 0)
		return (SET_ERROR(EPERM));

	if (strcasecmp(strval, ZFS_MLSLABEL_DEFAULT) == 0)
		new_default = TRUE;

	/* The label must be translatable */
	if (!new_default && (hexstr_to_label(strval, &new_sl) != 0))
		return (SET_ERROR(EINVAL));

	/*
	 * In a non-global zone, disallow attempts to set a label that
	 * doesn't match that of the zone; otherwise no other checks
	 * are needed.
	 */
	if (!INGLOBALZONE(curproc)) {
		if (new_default || !blequal(&new_sl, CR_SL(CRED())))
			return (SET_ERROR(EPERM));
		return (0);
	}

	/*
	 * For global-zone datasets (i.e., those whose zoned property is
	 * "off", verify that the specified new label is valid for the
	 * global zone.
	 */
	if (dsl_prop_get_integer(name,
	    zfs_prop_to_name(ZFS_PROP_ZONED), &zoned, NULL))
		return (SET_ERROR(EPERM));
	if (!zoned) {
		if (zfs_check_global_label(name, strval) != 0)
			return (SET_ERROR(EPERM));
	}

	/*
	 * If the existing dataset label is nondefault, check if the
	 * dataset is mounted (label cannot be changed while mounted).
	 * Get the zfsvfs_t; if there isn't one, then the dataset isn't
	 * mounted (or isn't a dataset, doesn't exist, ...).
	 */
	if (strcasecmp(ds_hexsl, ZFS_MLSLABEL_DEFAULT) != 0) {
		objset_t *os;
		static const char *setsl_tag = "setsl_tag";

		/*
		 * Try to own the dataset; abort if there is any error,
		 * (e.g., already mounted, in use, or other error).
		 */
		error = dmu_objset_own(name, DMU_OST_ZFS, B_TRUE, B_TRUE,
		    setsl_tag, &os);
		if (error != 0)
			return (SET_ERROR(EPERM));

		dmu_objset_disown(os, B_TRUE, setsl_tag);

		if (new_default) {
			needed_priv = PRIV_FILE_DOWNGRADE_SL;
			goto out_check;
		}

		if (hexstr_to_label(strval, &new_sl) != 0)
			return (SET_ERROR(EPERM));

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
#else
	return (SET_ERROR(ENOTSUP));
#endif /* HAVE_MLSLABEL */
}

static int
zfs_secpolicy_setprop(const char *dsname, zfs_prop_t prop, nvpair_t *propval,
    cred_t *cr)
{
	const char *strval;

	/*
	 * Check permissions for special properties.
	 */
	switch (prop) {
	default:
		break;
	case ZFS_PROP_ZONED:
		/*
		 * Disallow setting of 'zoned' from within a local zone.
		 */
		if (!INGLOBALZONE(curproc))
			return (SET_ERROR(EPERM));
		break;

	case ZFS_PROP_QUOTA:
	case ZFS_PROP_FILESYSTEM_LIMIT:
	case ZFS_PROP_SNAPSHOT_LIMIT:
		if (!INGLOBALZONE(curproc)) {
			uint64_t zoned;
			char setpoint[ZFS_MAX_DATASET_NAME_LEN];
			/*
			 * Unprivileged users are allowed to modify the
			 * limit on things *under* (ie. contained by)
			 * the thing they own.
			 */
			if (dsl_prop_get_integer(dsname,
			    zfs_prop_to_name(ZFS_PROP_ZONED), &zoned, setpoint))
				return (SET_ERROR(EPERM));
			if (!zoned || strlen(dsname) <= strlen(setpoint))
				return (SET_ERROR(EPERM));
		}
		break;

	case ZFS_PROP_MLSLABEL:
		if (!is_system_labeled())
			return (SET_ERROR(EPERM));

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

static int
zfs_secpolicy_set_fsacl(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	/*
	 * permission to set permissions will be evaluated later in
	 * dsl_deleg_can_allow()
	 */
	(void) innvl;
	return (zfs_dozonecheck(zc->zc_name, cr));
}

static int
zfs_secpolicy_rollback(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	(void) innvl;
	return (zfs_secpolicy_write_perms(zc->zc_name,
	    ZFS_DELEG_PERM_ROLLBACK, cr));
}

static int
zfs_secpolicy_send(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	(void) innvl;
	dsl_pool_t *dp;
	dsl_dataset_t *ds;
	const char *cp;
	int error;

	/*
	 * Generate the current snapshot name from the given objsetid, then
	 * use that name for the secpolicy/zone checks.
	 */
	cp = strchr(zc->zc_name, '@');
	if (cp == NULL)
		return (SET_ERROR(EINVAL));
	error = dsl_pool_hold(zc->zc_name, FTAG, &dp);
	if (error != 0)
		return (error);

	error = dsl_dataset_hold_obj(dp, zc->zc_sendobj, FTAG, &ds);
	if (error != 0) {
		dsl_pool_rele(dp, FTAG);
		return (error);
	}

	dsl_dataset_name(ds, zc->zc_name);

	error = zfs_secpolicy_write_perms_ds(zc->zc_name, ds,
	    ZFS_DELEG_PERM_SEND, cr);
	dsl_dataset_rele(ds, FTAG);
	dsl_pool_rele(dp, FTAG);

	return (error);
}

static int
zfs_secpolicy_send_new(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	(void) innvl;
	return (zfs_secpolicy_write_perms(zc->zc_name,
	    ZFS_DELEG_PERM_SEND, cr));
}

static int
zfs_secpolicy_share(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	(void) zc, (void) innvl, (void) cr;
	return (SET_ERROR(ENOTSUP));
}

static int
zfs_secpolicy_smb_acl(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	(void) zc, (void) innvl, (void) cr;
	return (SET_ERROR(ENOTSUP));
}

static int
zfs_get_parent(const char *datasetname, char *parent, int parentsize)
{
	char *cp;

	/*
	 * Remove the @bla or /bla from the end of the name to get the parent.
	 */
	(void) strlcpy(parent, datasetname, parentsize);
	cp = strrchr(parent, '@');
	if (cp != NULL) {
		cp[0] = '\0';
	} else {
		cp = strrchr(parent, '/');
		if (cp == NULL)
			return (SET_ERROR(ENOENT));
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
zfs_secpolicy_destroy(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	(void) innvl;
	return (zfs_secpolicy_destroy_perms(zc->zc_name, cr));
}

/*
 * Destroying snapshots with delegated permissions requires
 * descendant mount and destroy permissions.
 */
static int
zfs_secpolicy_destroy_snaps(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	(void) zc;
	nvlist_t *snaps;
	nvpair_t *pair, *nextpair;
	int error = 0;

	snaps = fnvlist_lookup_nvlist(innvl, "snaps");

	for (pair = nvlist_next_nvpair(snaps, NULL); pair != NULL;
	    pair = nextpair) {
		nextpair = nvlist_next_nvpair(snaps, pair);
		error = zfs_secpolicy_destroy_perms(nvpair_name(pair), cr);
		if (error == ENOENT) {
			/*
			 * Ignore any snapshots that don't exist (we consider
			 * them "already destroyed").  Remove the name from the
			 * nvl here in case the snapshot is created between
			 * now and when we try to destroy it (in which case
			 * we don't want to destroy it since we haven't
			 * checked for permission).
			 */
			fnvlist_remove_nvpair(snaps, pair);
			error = 0;
		}
		if (error != 0)
			break;
	}

	return (error);
}

int
zfs_secpolicy_rename_perms(const char *from, const char *to, cred_t *cr)
{
	char	parentname[ZFS_MAX_DATASET_NAME_LEN];
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
zfs_secpolicy_rename(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	(void) innvl;
	return (zfs_secpolicy_rename_perms(zc->zc_name, zc->zc_value, cr));
}

static int
zfs_secpolicy_promote(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	(void) innvl;
	dsl_pool_t *dp;
	dsl_dataset_t *clone;
	int error;

	error = zfs_secpolicy_write_perms(zc->zc_name,
	    ZFS_DELEG_PERM_PROMOTE, cr);
	if (error != 0)
		return (error);

	error = dsl_pool_hold(zc->zc_name, FTAG, &dp);
	if (error != 0)
		return (error);

	error = dsl_dataset_hold(dp, zc->zc_name, FTAG, &clone);

	if (error == 0) {
		char parentname[ZFS_MAX_DATASET_NAME_LEN];
		dsl_dataset_t *origin = NULL;
		dsl_dir_t *dd;
		dd = clone->ds_dir;

		error = dsl_dataset_hold_obj(dd->dd_pool,
		    dsl_dir_phys(dd)->dd_origin_obj, FTAG, &origin);
		if (error != 0) {
			dsl_dataset_rele(clone, FTAG);
			dsl_pool_rele(dp, FTAG);
			return (error);
		}

		error = zfs_secpolicy_write_perms_ds(zc->zc_name, clone,
		    ZFS_DELEG_PERM_MOUNT, cr);

		dsl_dataset_name(origin, parentname);
		if (error == 0) {
			error = zfs_secpolicy_write_perms_ds(parentname, origin,
			    ZFS_DELEG_PERM_PROMOTE, cr);
		}
		dsl_dataset_rele(clone, FTAG);
		dsl_dataset_rele(origin, FTAG);
	}
	dsl_pool_rele(dp, FTAG);
	return (error);
}

static int
zfs_secpolicy_recv(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	(void) innvl;
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

/*
 * Check for permission to create each snapshot in the nvlist.
 */
static int
zfs_secpolicy_snapshot(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	(void) zc;
	nvlist_t *snaps;
	int error = 0;
	nvpair_t *pair;

	snaps = fnvlist_lookup_nvlist(innvl, "snaps");

	for (pair = nvlist_next_nvpair(snaps, NULL); pair != NULL;
	    pair = nvlist_next_nvpair(snaps, pair)) {
		char *name = (char *)nvpair_name(pair);
		char *atp = strchr(name, '@');

		if (atp == NULL) {
			error = SET_ERROR(EINVAL);
			break;
		}
		*atp = '\0';
		error = zfs_secpolicy_snapshot_perms(name, cr);
		*atp = '@';
		if (error != 0)
			break;
	}
	return (error);
}

/*
 * Check for permission to create each bookmark in the nvlist.
 */
static int
zfs_secpolicy_bookmark(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	(void) zc;
	int error = 0;

	for (nvpair_t *pair = nvlist_next_nvpair(innvl, NULL);
	    pair != NULL; pair = nvlist_next_nvpair(innvl, pair)) {
		char *name = (char *)nvpair_name(pair);
		char *hashp = strchr(name, '#');

		if (hashp == NULL) {
			error = SET_ERROR(EINVAL);
			break;
		}
		*hashp = '\0';
		error = zfs_secpolicy_write_perms(name,
		    ZFS_DELEG_PERM_BOOKMARK, cr);
		*hashp = '#';
		if (error != 0)
			break;
	}
	return (error);
}

static int
zfs_secpolicy_destroy_bookmarks(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	(void) zc;
	nvpair_t *pair, *nextpair;
	int error = 0;

	for (pair = nvlist_next_nvpair(innvl, NULL); pair != NULL;
	    pair = nextpair) {
		char *name = (char *)nvpair_name(pair);
		char *hashp = strchr(name, '#');
		nextpair = nvlist_next_nvpair(innvl, pair);

		if (hashp == NULL) {
			error = SET_ERROR(EINVAL);
			break;
		}

		*hashp = '\0';
		error = zfs_secpolicy_write_perms(name,
		    ZFS_DELEG_PERM_DESTROY, cr);
		*hashp = '#';
		if (error == ENOENT) {
			/*
			 * Ignore any filesystems that don't exist (we consider
			 * their bookmarks "already destroyed").  Remove
			 * the name from the nvl here in case the filesystem
			 * is created between now and when we try to destroy
			 * the bookmark (in which case we don't want to
			 * destroy it since we haven't checked for permission).
			 */
			fnvlist_remove_nvpair(innvl, pair);
			error = 0;
		}
		if (error != 0)
			break;
	}

	return (error);
}

static int
zfs_secpolicy_log_history(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	(void) zc, (void) innvl, (void) cr;
	/*
	 * Even root must have a proper TSD so that we know what pool
	 * to log to.
	 */
	if (tsd_get(zfs_allow_log_key) == NULL)
		return (SET_ERROR(EPERM));
	return (0);
}

static int
zfs_secpolicy_create_clone(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	char		parentname[ZFS_MAX_DATASET_NAME_LEN];
	int		error;
	const char	*origin;

	if ((error = zfs_get_parent(zc->zc_name, parentname,
	    sizeof (parentname))) != 0)
		return (error);

	if (nvlist_lookup_string(innvl, "origin", &origin) == 0 &&
	    (error = zfs_secpolicy_write_perms(origin,
	    ZFS_DELEG_PERM_CLONE, cr)) != 0)
		return (error);

	if ((error = zfs_secpolicy_write_perms(parentname,
	    ZFS_DELEG_PERM_CREATE, cr)) != 0)
		return (error);

	return (zfs_secpolicy_write_perms(parentname,
	    ZFS_DELEG_PERM_MOUNT, cr));
}

/*
 * Policy for pool operations - create/destroy pools, add vdevs, etc.  Requires
 * SYS_CONFIG privilege, which is not available in a local zone.
 */
int
zfs_secpolicy_config(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	(void) zc, (void) innvl;

	if (secpolicy_sys_config(cr, B_FALSE) != 0)
		return (SET_ERROR(EPERM));

	return (0);
}

/*
 * Policy for object to name lookups.
 */
static int
zfs_secpolicy_diff(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	(void) innvl;
	int error;

	if (secpolicy_sys_config(cr, B_FALSE) == 0)
		return (0);

	error = zfs_secpolicy_write_perms(zc->zc_name, ZFS_DELEG_PERM_DIFF, cr);
	return (error);
}

/*
 * Policy for fault injection.  Requires all privileges.
 */
static int
zfs_secpolicy_inject(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	(void) zc, (void) innvl;
	return (secpolicy_zinject(cr));
}

static int
zfs_secpolicy_inherit_prop(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	(void) innvl;
	zfs_prop_t prop = zfs_name_to_prop(zc->zc_value);

	if (prop == ZPROP_USERPROP) {
		if (!zfs_prop_user(zc->zc_value))
			return (SET_ERROR(EINVAL));
		return (zfs_secpolicy_write_perms(zc->zc_name,
		    ZFS_DELEG_PERM_USERPROP, cr));
	} else {
		return (zfs_secpolicy_setprop(zc->zc_name, prop,
		    NULL, cr));
	}
}

static int
zfs_secpolicy_userspace_one(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	int err = zfs_secpolicy_read(zc, innvl, cr);
	if (err)
		return (err);

	if (zc->zc_objset_type >= ZFS_NUM_USERQUOTA_PROPS)
		return (SET_ERROR(EINVAL));

	if (zc->zc_value[0] == 0) {
		/*
		 * They are asking about a posix uid/gid.  If it's
		 * themself, allow it.
		 */
		if (zc->zc_objset_type == ZFS_PROP_USERUSED ||
		    zc->zc_objset_type == ZFS_PROP_USERQUOTA ||
		    zc->zc_objset_type == ZFS_PROP_USEROBJUSED ||
		    zc->zc_objset_type == ZFS_PROP_USEROBJQUOTA) {
			if (zc->zc_guid == crgetuid(cr))
				return (0);
		} else if (zc->zc_objset_type == ZFS_PROP_GROUPUSED ||
		    zc->zc_objset_type == ZFS_PROP_GROUPQUOTA ||
		    zc->zc_objset_type == ZFS_PROP_GROUPOBJUSED ||
		    zc->zc_objset_type == ZFS_PROP_GROUPOBJQUOTA) {
			if (groupmember(zc->zc_guid, cr))
				return (0);
		}
		/* else is for project quota/used */
	}

	return (zfs_secpolicy_write_perms(zc->zc_name,
	    userquota_perms[zc->zc_objset_type], cr));
}

static int
zfs_secpolicy_userspace_many(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	int err = zfs_secpolicy_read(zc, innvl, cr);
	if (err)
		return (err);

	if (zc->zc_objset_type >= ZFS_NUM_USERQUOTA_PROPS)
		return (SET_ERROR(EINVAL));

	return (zfs_secpolicy_write_perms(zc->zc_name,
	    userquota_perms[zc->zc_objset_type], cr));
}

static int
zfs_secpolicy_userspace_upgrade(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	(void) innvl;
	return (zfs_secpolicy_setprop(zc->zc_name, ZFS_PROP_VERSION,
	    NULL, cr));
}

static int
zfs_secpolicy_hold(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	(void) zc;
	nvpair_t *pair;
	nvlist_t *holds;
	int error;

	holds = fnvlist_lookup_nvlist(innvl, "holds");

	for (pair = nvlist_next_nvpair(holds, NULL); pair != NULL;
	    pair = nvlist_next_nvpair(holds, pair)) {
		char fsname[ZFS_MAX_DATASET_NAME_LEN];
		error = dmu_fsname(nvpair_name(pair), fsname);
		if (error != 0)
			return (error);
		error = zfs_secpolicy_write_perms(fsname,
		    ZFS_DELEG_PERM_HOLD, cr);
		if (error != 0)
			return (error);
	}
	return (0);
}

static int
zfs_secpolicy_release(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	(void) zc;
	nvpair_t *pair;
	int error;

	for (pair = nvlist_next_nvpair(innvl, NULL); pair != NULL;
	    pair = nvlist_next_nvpair(innvl, pair)) {
		char fsname[ZFS_MAX_DATASET_NAME_LEN];
		error = dmu_fsname(nvpair_name(pair), fsname);
		if (error != 0)
			return (error);
		error = zfs_secpolicy_write_perms(fsname,
		    ZFS_DELEG_PERM_RELEASE, cr);
		if (error != 0)
			return (error);
	}
	return (0);
}

/*
 * Policy for allowing temporary snapshots to be taken or released
 */
static int
zfs_secpolicy_tmp_snapshot(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	/*
	 * A temporary snapshot is the same as a snapshot,
	 * hold, destroy and release all rolled into one.
	 * Delegated diff alone is sufficient that we allow this.
	 */
	int error;

	if (zfs_secpolicy_write_perms(zc->zc_name,
	    ZFS_DELEG_PERM_DIFF, cr) == 0)
		return (0);

	error = zfs_secpolicy_snapshot_perms(zc->zc_name, cr);

	if (innvl != NULL) {
		if (error == 0)
			error = zfs_secpolicy_hold(zc, innvl, cr);
		if (error == 0)
			error = zfs_secpolicy_release(zc, innvl, cr);
		if (error == 0)
			error = zfs_secpolicy_destroy(zc, innvl, cr);
	}
	return (error);
}

static int
zfs_secpolicy_load_key(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	return (zfs_secpolicy_write_perms(zc->zc_name,
	    ZFS_DELEG_PERM_LOAD_KEY, cr));
}

static int
zfs_secpolicy_change_key(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	return (zfs_secpolicy_write_perms(zc->zc_name,
	    ZFS_DELEG_PERM_CHANGE_KEY, cr));
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
		return (SET_ERROR(EINVAL));

	packed = vmem_alloc(size, KM_SLEEP);

	if (ddi_copyin((void *)(uintptr_t)nvl, packed, size, iflag) != 0) {
		vmem_free(packed, size);
		return (SET_ERROR(EFAULT));
	}

	if ((error = nvlist_unpack(packed, size, &list, 0)) != 0) {
		vmem_free(packed, size);
		return (error);
	}

	vmem_free(packed, size);

	*nvp = list;
	return (0);
}

/*
 * Reduce the size of this nvlist until it can be serialized in 'max' bytes.
 * Entries will be removed from the end of the nvlist, and one int32 entry
 * named "N_MORE_ERRORS" will be added indicating how many entries were
 * removed.
 */
static int
nvlist_smush(nvlist_t *errors, size_t max)
{
	size_t size;

	size = fnvlist_size(errors);

	if (size > max) {
		nvpair_t *more_errors;
		int n = 0;

		if (max < 1024)
			return (SET_ERROR(ENOMEM));

		fnvlist_add_int32(errors, ZPROP_N_MORE_ERRORS, 0);
		more_errors = nvlist_prev_nvpair(errors, NULL);

		do {
			nvpair_t *pair = nvlist_prev_nvpair(errors,
			    more_errors);
			fnvlist_remove_nvpair(errors, pair);
			n++;
			size = fnvlist_size(errors);
		} while (size > max);

		fnvlist_remove_nvpair(errors, more_errors);
		fnvlist_add_int32(errors, ZPROP_N_MORE_ERRORS, n);
		ASSERT3U(fnvlist_size(errors), <=, max);
	}

	return (0);
}

static int
put_nvlist(zfs_cmd_t *zc, nvlist_t *nvl)
{
	char *packed = NULL;
	int error = 0;
	size_t size;

	size = fnvlist_size(nvl);

	if (size > zc->zc_nvlist_dst_size) {
		error = SET_ERROR(ENOMEM);
	} else {
		packed = fnvlist_pack(nvl, &size);
		if (ddi_copyout(packed, (void *)(uintptr_t)zc->zc_nvlist_dst,
		    size, zc->zc_iflags) != 0)
			error = SET_ERROR(EFAULT);
		fnvlist_pack_free(packed, size);
	}

	zc->zc_nvlist_dst_size = size;
	zc->zc_nvlist_dst_filled = B_TRUE;
	return (error);
}

int
getzfsvfs_impl(objset_t *os, zfsvfs_t **zfvp)
{
	int error = 0;
	if (dmu_objset_type(os) != DMU_OST_ZFS) {
		return (SET_ERROR(EINVAL));
	}

	mutex_enter(&os->os_user_ptr_lock);
	*zfvp = dmu_objset_get_user(os);
	/* bump s_active only when non-zero to prevent umount race */
	error = zfs_vfs_ref(zfvp);
	mutex_exit(&os->os_user_ptr_lock);
	return (error);
}

int
getzfsvfs(const char *dsname, zfsvfs_t **zfvp)
{
	objset_t *os;
	int error;

	error = dmu_objset_hold(dsname, FTAG, &os);
	if (error != 0)
		return (error);

	error = getzfsvfs_impl(os, zfvp);
	dmu_objset_rele(os, FTAG);
	return (error);
}

/*
 * Find a zfsvfs_t for a mounted filesystem, or create our own, in which
 * case its z_sb will be NULL, and it will be opened as the owner.
 * If 'writer' is set, the z_teardown_lock will be held for RW_WRITER,
 * which prevents all inode ops from running.
 */
static int
zfsvfs_hold(const char *name, const void *tag, zfsvfs_t **zfvp,
    boolean_t writer)
{
	int error = 0;

	if (getzfsvfs(name, zfvp) != 0)
		error = zfsvfs_create(name, B_FALSE, zfvp);
	if (error == 0) {
		if (writer)
			ZFS_TEARDOWN_ENTER_WRITE(*zfvp, tag);
		else
			ZFS_TEARDOWN_ENTER_READ(*zfvp, tag);
		if ((*zfvp)->z_unmounted) {
			/*
			 * XXX we could probably try again, since the unmounting
			 * thread should be just about to disassociate the
			 * objset from the zfsvfs.
			 */
			ZFS_TEARDOWN_EXIT(*zfvp, tag);
			return (SET_ERROR(EBUSY));
		}
	}
	return (error);
}

static void
zfsvfs_rele(zfsvfs_t *zfsvfs, const void *tag)
{
	ZFS_TEARDOWN_EXIT(zfsvfs, tag);

	if (zfs_vfs_held(zfsvfs)) {
		zfs_vfs_rele(zfsvfs);
	} else {
		dmu_objset_disown(zfsvfs->z_os, B_TRUE, zfsvfs);
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
	dsl_crypto_params_t *dcp = NULL;
	const char *spa_name = zc->zc_name;
	boolean_t unload_wkey = B_TRUE;

	if ((error = get_nvlist(zc->zc_nvlist_conf, zc->zc_nvlist_conf_size,
	    zc->zc_iflags, &config)))
		return (error);

	if (zc->zc_nvlist_src_size != 0 && (error =
	    get_nvlist(zc->zc_nvlist_src, zc->zc_nvlist_src_size,
	    zc->zc_iflags, &props))) {
		nvlist_free(config);
		return (error);
	}

	if (props) {
		nvlist_t *nvl = NULL;
		nvlist_t *hidden_args = NULL;
		uint64_t version = SPA_VERSION;
		const char *tname;

		(void) nvlist_lookup_uint64(props,
		    zpool_prop_to_name(ZPOOL_PROP_VERSION), &version);
		if (!SPA_VERSION_IS_SUPPORTED(version)) {
			error = SET_ERROR(EINVAL);
			goto pool_props_bad;
		}
		(void) nvlist_lookup_nvlist(props, ZPOOL_ROOTFS_PROPS, &nvl);
		if (nvl) {
			error = nvlist_dup(nvl, &rootprops, KM_SLEEP);
			if (error != 0)
				goto pool_props_bad;
			(void) nvlist_remove_all(props, ZPOOL_ROOTFS_PROPS);
		}

		(void) nvlist_lookup_nvlist(props, ZPOOL_HIDDEN_ARGS,
		    &hidden_args);
		error = dsl_crypto_params_create_nvlist(DCP_CMD_NONE,
		    rootprops, hidden_args, &dcp);
		if (error != 0)
			goto pool_props_bad;
		(void) nvlist_remove_all(props, ZPOOL_HIDDEN_ARGS);

		VERIFY(nvlist_alloc(&zplprops, NV_UNIQUE_NAME, KM_SLEEP) == 0);
		error = zfs_fill_zplprops_root(version, rootprops,
		    zplprops, NULL);
		if (error != 0)
			goto pool_props_bad;

		if (nvlist_lookup_string(props,
		    zpool_prop_to_name(ZPOOL_PROP_TNAME), &tname) == 0)
			spa_name = tname;
	}

	error = spa_create(zc->zc_name, config, props, zplprops, dcp);

	/*
	 * Set the remaining root properties
	 */
	if (!error && (error = zfs_set_prop_nvlist(spa_name,
	    ZPROP_SRC_LOCAL, rootprops, NULL)) != 0) {
		(void) spa_destroy(spa_name);
		unload_wkey = B_FALSE; /* spa_destroy() unloads wrapping keys */
	}

pool_props_bad:
	nvlist_free(rootprops);
	nvlist_free(zplprops);
	nvlist_free(config);
	nvlist_free(props);
	dsl_crypto_params_free(dcp, unload_wkey && !!error);

	return (error);
}

static int
zfs_ioc_pool_destroy(zfs_cmd_t *zc)
{
	int error;
	zfs_log_history(zc);
	error = spa_destroy(zc->zc_name);

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
		error = SET_ERROR(EINVAL);
	else
		error = spa_import(zc->zc_name, config, props, zc->zc_cookie);

	if (zc->zc_nvlist_dst != 0) {
		int err;

		if ((err = put_nvlist(zc, config)) != 0)
			error = err;
	}

	nvlist_free(config);
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

	return (error);
}

static int
zfs_ioc_pool_configs(zfs_cmd_t *zc)
{
	nvlist_t *configs;
	int error;

	error = spa_all_configs(&zc->zc_cookie, &configs);
	if (error)
		return (error);

	error = put_nvlist(zc, configs);

	nvlist_free(configs);

	return (error);
}

/*
 * inputs:
 * zc_name		name of the pool
 *
 * outputs:
 * zc_cookie		real errno
 * zc_nvlist_dst	config nvlist
 * zc_nvlist_dst_size	size of config nvlist
 */
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
	nvlist_t *tryconfig, *config = NULL;
	int error;

	if ((error = get_nvlist(zc->zc_nvlist_conf, zc->zc_nvlist_conf_size,
	    zc->zc_iflags, &tryconfig)) != 0)
		return (error);

	config = spa_tryimport(tryconfig);

	nvlist_free(tryconfig);

	if (config == NULL)
		return (SET_ERROR(EINVAL));

	error = put_nvlist(zc, config);
	nvlist_free(config);

	return (error);
}

/*
 * inputs:
 * zc_name              name of the pool
 * zc_cookie            scan func (pool_scan_func_t)
 * zc_flags             scrub pause/resume flag (pool_scrub_cmd_t)
 */
static int
zfs_ioc_pool_scan(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;

	if (zc->zc_flags >= POOL_SCRUB_FLAGS_END)
		return (SET_ERROR(EINVAL));

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0)
		return (error);

	if (zc->zc_flags == POOL_SCRUB_PAUSE)
		error = spa_scrub_pause_resume(spa, POOL_SCRUB_PAUSE);
	else if (zc->zc_cookie == POOL_SCAN_NONE)
		error = spa_scan_stop(spa);
	else
		error = spa_scan(spa, zc->zc_cookie);

	spa_close(spa, FTAG);

	return (error);
}

/*
 * inputs:
 * poolname             name of the pool
 * scan_type            scan func (pool_scan_func_t)
 * scan_command         scrub pause/resume flag (pool_scrub_cmd_t)
 */
static const zfs_ioc_key_t zfs_keys_pool_scrub[] = {
	{"scan_type",		DATA_TYPE_UINT64,	0},
	{"scan_command",	DATA_TYPE_UINT64,	0},
};

static int
zfs_ioc_pool_scrub(const char *poolname, nvlist_t *innvl, nvlist_t *outnvl)
{
	spa_t *spa;
	int error;
	uint64_t scan_type, scan_cmd;

	if (nvlist_lookup_uint64(innvl, "scan_type", &scan_type) != 0)
		return (SET_ERROR(EINVAL));
	if (nvlist_lookup_uint64(innvl, "scan_command", &scan_cmd) != 0)
		return (SET_ERROR(EINVAL));

	if (scan_cmd >= POOL_SCRUB_FLAGS_END)
		return (SET_ERROR(EINVAL));

	if ((error = spa_open(poolname, &spa, FTAG)) != 0)
		return (error);

	if (scan_cmd == POOL_SCRUB_PAUSE) {
		error = spa_scrub_pause_resume(spa, POOL_SCRUB_PAUSE);
	} else if (scan_type == POOL_SCAN_NONE) {
		error = spa_scan_stop(spa);
	} else {
		error = spa_scan(spa, scan_type);
	}

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

	if (zc->zc_cookie < spa_version(spa) ||
	    !SPA_VERSION_IS_SUPPORTED(zc->zc_cookie)) {
		spa_close(spa, FTAG);
		return (SET_ERROR(EINVAL));
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
		return (SET_ERROR(EINVAL));

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0)
		return (error);

	if (spa_version(spa) < SPA_VERSION_ZPOOL_HISTORY) {
		spa_close(spa, FTAG);
		return (SET_ERROR(ENOTSUP));
	}

	hist_buf = vmem_alloc(size, KM_SLEEP);
	if ((error = spa_history_get(spa, &zc->zc_history_offset,
	    &zc->zc_history_len, hist_buf)) == 0) {
		error = ddi_copyout(hist_buf,
		    (void *)(uintptr_t)zc->zc_history,
		    zc->zc_history_len, zc->zc_iflags);
	}

	spa_close(spa, FTAG);
	vmem_free(hist_buf, size);
	return (error);
}

static int
zfs_ioc_pool_reguid(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;

	error = spa_open(zc->zc_name, &spa, FTAG);
	if (error == 0) {
		error = spa_change_guid(spa);
		spa_close(spa, FTAG);
	}
	return (error);
}

static int
zfs_ioc_dsobj_to_dsname(zfs_cmd_t *zc)
{
	return (dsl_dsobj_to_dsname(zc->zc_name, zc->zc_obj, zc->zc_value));
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
	if ((error = dmu_objset_hold_flags(zc->zc_name, B_TRUE,
	    FTAG, &os)) != 0)
		return (error);
	if (dmu_objset_type(os) != DMU_OST_ZFS) {
		dmu_objset_rele_flags(os, B_TRUE, FTAG);
		return (SET_ERROR(EINVAL));
	}
	error = zfs_obj_to_path(os, zc->zc_obj, zc->zc_value,
	    sizeof (zc->zc_value));
	dmu_objset_rele_flags(os, B_TRUE, FTAG);

	return (error);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_obj		object to find
 *
 * outputs:
 * zc_stat		stats on object
 * zc_value		path to object
 */
static int
zfs_ioc_obj_to_stats(zfs_cmd_t *zc)
{
	objset_t *os;
	int error;

	/* XXX reading from objset not owned */
	if ((error = dmu_objset_hold_flags(zc->zc_name, B_TRUE,
	    FTAG, &os)) != 0)
		return (error);
	if (dmu_objset_type(os) != DMU_OST_ZFS) {
		dmu_objset_rele_flags(os, B_TRUE, FTAG);
		return (SET_ERROR(EINVAL));
	}
	error = zfs_obj_to_stats(os, zc->zc_obj, &zc->zc_stat, zc->zc_value,
	    sizeof (zc->zc_value));
	dmu_objset_rele_flags(os, B_TRUE, FTAG);

	return (error);
}

static int
zfs_ioc_vdev_add(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;
	nvlist_t *config;

	error = spa_open(zc->zc_name, &spa, FTAG);
	if (error != 0)
		return (error);

	error = get_nvlist(zc->zc_nvlist_conf, zc->zc_nvlist_conf_size,
	    zc->zc_iflags, &config);
	if (error == 0) {
		error = spa_vdev_add(spa, config, zc->zc_flags);
		nvlist_free(config);
	}
	spa_close(spa, FTAG);
	return (error);
}

/*
 * inputs:
 * zc_name		name of the pool
 * zc_guid		guid of vdev to remove
 * zc_cookie		cancel removal
 */
static int
zfs_ioc_vdev_remove(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;

	error = spa_open(zc->zc_name, &spa, FTAG);
	if (error != 0)
		return (error);
	if (zc->zc_cookie != 0) {
		error = spa_vdev_remove_cancel(spa);
	} else {
		error = spa_vdev_remove(spa, zc->zc_guid, B_FALSE);
	}
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
		    zc->zc_obj != VDEV_AUX_EXTERNAL &&
		    zc->zc_obj != VDEV_AUX_EXTERNAL_PERSIST)
			zc->zc_obj = VDEV_AUX_ERR_EXCEEDED;

		error = vdev_fault(spa, zc->zc_guid, zc->zc_obj);
		break;

	case VDEV_STATE_DEGRADED:
		if (zc->zc_obj != VDEV_AUX_ERR_EXCEEDED &&
		    zc->zc_obj != VDEV_AUX_EXTERNAL)
			zc->zc_obj = VDEV_AUX_ERR_EXCEEDED;

		error = vdev_degrade(spa, zc->zc_guid, zc->zc_obj);
		break;

	case VDEV_STATE_REMOVED:
		error = vdev_remove_wanted(spa, zc->zc_guid);
		break;

	default:
		error = SET_ERROR(EINVAL);
	}
	zc->zc_cookie = newstate;
	spa_close(spa, FTAG);
	return (error);
}

static int
zfs_ioc_vdev_attach(zfs_cmd_t *zc)
{
	spa_t *spa;
	nvlist_t *config;
	int replacing = zc->zc_cookie;
	int rebuild = zc->zc_simple;
	int error;

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0)
		return (error);

	if ((error = get_nvlist(zc->zc_nvlist_conf, zc->zc_nvlist_conf_size,
	    zc->zc_iflags, &config)) == 0) {
		error = spa_vdev_attach(spa, zc->zc_guid, config, replacing,
		    rebuild);
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

	if ((error = get_nvlist(zc->zc_nvlist_conf, zc->zc_nvlist_conf_size,
	    zc->zc_iflags, &config))) {
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
	const char *path = zc->zc_value;
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
	const char *fru = zc->zc_value;
	uint64_t guid = zc->zc_guid;
	int error;

	error = spa_open(zc->zc_name, &spa, FTAG);
	if (error != 0)
		return (error);

	error = spa_vdev_setfru(spa, guid, fru);
	spa_close(spa, FTAG);
	return (error);
}

static int
zfs_ioc_objset_stats_impl(zfs_cmd_t *zc, objset_t *os)
{
	int error = 0;
	nvlist_t *nv;

	dmu_objset_fast_stat(os, &zc->zc_objset_stats);

	if (!zc->zc_simple && zc->zc_nvlist_dst != 0 &&
	    (error = dsl_prop_get_all(os, &nv)) == 0) {
		dmu_objset_stats(os, nv);
		/*
		 * NB: zvol_get_stats() will read the objset contents,
		 * which we aren't supposed to do with a
		 * DS_MODE_USER hold, because it could be
		 * inconsistent.  So this is a bit of a workaround...
		 * XXX reading without owning
		 */
		if (!zc->zc_objset_stats.dds_inconsistent &&
		    dmu_objset_type(os) == DMU_OST_ZVOL) {
			error = zvol_get_stats(os, nv);
			if (error == EIO) {
				nvlist_free(nv);
				return (error);
			}
			VERIFY0(error);
		}
		if (error == 0)
			error = put_nvlist(zc, nv);
		nvlist_free(nv);
	}

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
	objset_t *os;
	int error;

	error = dmu_objset_hold(zc->zc_name, FTAG, &os);
	if (error == 0) {
		error = zfs_ioc_objset_stats_impl(zc, os);
		dmu_objset_rele(os, FTAG);
	}

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
	int error = 0;
	nvlist_t *nv;

	/*
	 * Without this check, we would return local property values if the
	 * caller has not already received properties on or after
	 * SPA_VERSION_RECVD_PROPS.
	 */
	if (!dsl_prop_get_hasrecvd(zc->zc_name))
		return (SET_ERROR(ENOTSUP));

	if (zc->zc_nvlist_dst != 0 &&
	    (error = dsl_prop_get_received(zc->zc_name, &nv)) == 0) {
		error = put_nvlist(zc, nv);
		nvlist_free(nv);
	}

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
	if ((err = dmu_objset_hold(zc->zc_name, FTAG, &os)))
		return (err);

	dmu_objset_fast_stat(os, &zc->zc_objset_stats);

	/*
	 * NB: nvl_add_zplprop() will read the objset contents,
	 * which we aren't supposed to do with a DS_MODE_USER
	 * hold, because it could be inconsistent.
	 */
	if (zc->zc_nvlist_dst != 0 &&
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
		err = SET_ERROR(ENOENT);
	}
	dmu_objset_rele(os, FTAG);
	return (err);
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
	if ((error = dmu_objset_hold(zc->zc_name, FTAG, &os))) {
		if (error == ENOENT)
			error = SET_ERROR(ESRCH);
		return (error);
	}

	p = strrchr(zc->zc_name, '/');
	if (p == NULL || p[1] != '\0')
		(void) strlcat(zc->zc_name, "/", sizeof (zc->zc_name));
	p = zc->zc_name + strlen(zc->zc_name);

	do {
		error = dmu_dir_list_next(os,
		    sizeof (zc->zc_name) - (p - zc->zc_name), p,
		    NULL, &zc->zc_cookie);
		if (error == ENOENT)
			error = SET_ERROR(ESRCH);
	} while (error == 0 && zfs_dataset_name_hidden(zc->zc_name));
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
 * zc_nvlist_src	iteration range nvlist
 * zc_nvlist_src_size	size of iteration range nvlist
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
	int error;
	objset_t *os, *ossnap;
	dsl_dataset_t *ds;
	uint64_t min_txg = 0, max_txg = 0;

	if (zc->zc_nvlist_src_size != 0) {
		nvlist_t *props = NULL;
		error = get_nvlist(zc->zc_nvlist_src, zc->zc_nvlist_src_size,
		    zc->zc_iflags, &props);
		if (error != 0)
			return (error);
		(void) nvlist_lookup_uint64(props, SNAP_ITER_MIN_TXG,
		    &min_txg);
		(void) nvlist_lookup_uint64(props, SNAP_ITER_MAX_TXG,
		    &max_txg);
		nvlist_free(props);
	}

	error = dmu_objset_hold(zc->zc_name, FTAG, &os);
	if (error != 0) {
		return (error == ENOENT ? SET_ERROR(ESRCH) : error);
	}

	/*
	 * A dataset name of maximum length cannot have any snapshots,
	 * so exit immediately.
	 */
	if (strlcat(zc->zc_name, "@", sizeof (zc->zc_name)) >=
	    ZFS_MAX_DATASET_NAME_LEN) {
		dmu_objset_rele(os, FTAG);
		return (SET_ERROR(ESRCH));
	}

	while (error == 0) {
		if (issig()) {
			error = SET_ERROR(EINTR);
			break;
		}

		error = dmu_snapshot_list_next(os,
		    sizeof (zc->zc_name) - strlen(zc->zc_name),
		    zc->zc_name + strlen(zc->zc_name), &zc->zc_obj,
		    &zc->zc_cookie, NULL);
		if (error == ENOENT) {
			error = SET_ERROR(ESRCH);
			break;
		} else if (error != 0) {
			break;
		}

		error = dsl_dataset_hold_obj(dmu_objset_pool(os), zc->zc_obj,
		    FTAG, &ds);
		if (error != 0)
			break;

		if ((min_txg != 0 && dsl_get_creationtxg(ds) < min_txg) ||
		    (max_txg != 0 && dsl_get_creationtxg(ds) > max_txg)) {
			dsl_dataset_rele(ds, FTAG);
			/* undo snapshot name append */
			*(strchr(zc->zc_name, '@') + 1) = '\0';
			/* skip snapshot */
			continue;
		}

		if (zc->zc_simple) {
			dsl_dataset_fast_stat(ds, &zc->zc_objset_stats);
			dsl_dataset_rele(ds, FTAG);
			break;
		}

		if ((error = dmu_objset_from_ds(ds, &ossnap)) != 0) {
			dsl_dataset_rele(ds, FTAG);
			break;
		}
		if ((error = zfs_ioc_objset_stats_impl(zc, ossnap)) != 0) {
			dsl_dataset_rele(ds, FTAG);
			break;
		}
		dsl_dataset_rele(ds, FTAG);
		break;
	}

	dmu_objset_rele(os, FTAG);
	/* if we failed, undo the @ that we tacked on to zc_name */
	if (error != 0)
		*strchr(zc->zc_name, '@') = '\0';
	return (error);
}

static int
zfs_prop_set_userquota(const char *dsname, nvpair_t *pair)
{
	const char *propname = nvpair_name(pair);
	uint64_t *valary;
	unsigned int vallen;
	const char *dash, *domain;
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
			return (SET_ERROR(EINVAL));
	}

	/*
	 * A correctly constructed propname is encoded as
	 * userquota@<rid>-<domain>.
	 */
	if ((dash = strchr(propname, '-')) == NULL ||
	    nvpair_value_uint64_array(pair, &valary, &vallen) != 0 ||
	    vallen != 3)
		return (SET_ERROR(EINVAL));

	domain = dash + 1;
	type = valary[0];
	rid = valary[1];
	quota = valary[2];

	err = zfsvfs_hold(dsname, FTAG, &zfsvfs, B_FALSE);
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
	uint64_t intval = 0;
	const char *strval = NULL;
	int err = -1;

	if (prop == ZPROP_USERPROP) {
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

	/* all special properties are numeric except for keylocation */
	if (zfs_prop_get_type(prop) == PROP_TYPE_STRING) {
		strval = fnvpair_value_string(pair);
	} else {
		intval = fnvpair_value_uint64(pair);
	}

	switch (prop) {
	case ZFS_PROP_QUOTA:
		err = dsl_dir_set_quota(dsname, source, intval);
		break;
	case ZFS_PROP_REFQUOTA:
		err = dsl_dataset_set_refquota(dsname, source, intval);
		break;
	case ZFS_PROP_FILESYSTEM_LIMIT:
	case ZFS_PROP_SNAPSHOT_LIMIT:
		if (intval == UINT64_MAX) {
			/* clearing the limit, just do it */
			err = 0;
		} else {
			err = dsl_dir_activate_fs_ss_limit(dsname);
		}
		/*
		 * Set err to -1 to force the zfs_set_prop_nvlist code down the
		 * default path to set the value in the nvlist.
		 */
		if (err == 0)
			err = -1;
		break;
	case ZFS_PROP_KEYLOCATION:
		err = dsl_crypto_can_set_keylocation(dsname, strval);

		/*
		 * Set err to -1 to force the zfs_set_prop_nvlist code down the
		 * default path to set the value in the nvlist.
		 */
		if (err == 0)
			err = -1;
		break;
	case ZFS_PROP_RESERVATION:
		err = dsl_dir_set_reservation(dsname, source, intval);
		break;
	case ZFS_PROP_REFRESERVATION:
		err = dsl_dataset_set_refreservation(dsname, source, intval);
		break;
	case ZFS_PROP_COMPRESSION:
		err = dsl_dataset_set_compression(dsname, source, intval);
		/*
		 * Set err to -1 to force the zfs_set_prop_nvlist code down the
		 * default path to set the value in the nvlist.
		 */
		if (err == 0)
			err = -1;
		break;
	case ZFS_PROP_VOLSIZE:
		err = zvol_set_volsize(dsname, intval);
		break;
	case ZFS_PROP_VOLTHREADING:
		err = zvol_set_volthreading(dsname, intval);
		/*
		 * Set err to -1 to force the zfs_set_prop_nvlist code down the
		 * default path to set the value in the nvlist.
		 */
		if (err == 0)
			err = -1;
		break;
	case ZFS_PROP_SNAPDEV:
	case ZFS_PROP_VOLMODE:
		err = zvol_set_common(dsname, prop, source, intval);
		break;
	case ZFS_PROP_READONLY:
		err = zvol_set_ro(dsname, intval);
		/*
		 * Set err to -1 to force the zfs_set_prop_nvlist code down the
		 * default path to set the value in the nvlist.
		 */
		if (err == 0)
			err = -1;
		break;
	case ZFS_PROP_VERSION:
	{
		zfsvfs_t *zfsvfs;

		if ((err = zfsvfs_hold(dsname, FTAG, &zfsvfs, B_TRUE)) != 0)
			break;

		err = zfs_set_version(zfsvfs, intval);
		zfsvfs_rele(zfsvfs, FTAG);

		if (err == 0 && intval >= ZPL_VERSION_USERSPACE) {
			zfs_cmd_t *zc;

			zc = kmem_zalloc(sizeof (zfs_cmd_t), KM_SLEEP);
			(void) strlcpy(zc->zc_name, dsname,
			    sizeof (zc->zc_name));
			(void) zfs_ioc_userspace_upgrade(zc);
			(void) zfs_ioc_id_quota_upgrade(zc);
			kmem_free(zc, sizeof (zfs_cmd_t));
		}
		break;
	}
	default:
		err = -1;
	}

	return (err);
}

static boolean_t
zfs_is_namespace_prop(zfs_prop_t prop)
{
	switch (prop) {

	case ZFS_PROP_ATIME:
	case ZFS_PROP_RELATIME:
	case ZFS_PROP_DEVICES:
	case ZFS_PROP_EXEC:
	case ZFS_PROP_SETUID:
	case ZFS_PROP_READONLY:
	case ZFS_PROP_XATTR:
	case ZFS_PROP_NBMAND:
		return (B_TRUE);

	default:
		return (B_FALSE);
	}
}

/*
 * This function is best effort. If it fails to set any of the given properties,
 * it continues to set as many as it can and returns the last error
 * encountered. If the caller provides a non-NULL errlist, it will be filled in
 * with the list of names of all the properties that failed along with the
 * corresponding error numbers.
 *
 * If every property is set successfully, zero is returned and errlist is not
 * modified.
 */
int
zfs_set_prop_nvlist(const char *dsname, zprop_source_t source, nvlist_t *nvl,
    nvlist_t *errlist)
{
	nvpair_t *pair;
	nvpair_t *propval;
	int rv = 0;
	int err;
	uint64_t intval;
	const char *strval;
	boolean_t should_update_mount_cache = B_FALSE;

	nvlist_t *genericnvl = fnvlist_alloc();
	nvlist_t *retrynvl = fnvlist_alloc();
retry:
	pair = NULL;
	while ((pair = nvlist_next_nvpair(nvl, pair)) != NULL) {
		const char *propname = nvpair_name(pair);
		zfs_prop_t prop = zfs_name_to_prop(propname);
		err = 0;

		/* decode the property value */
		propval = pair;
		if (nvpair_type(pair) == DATA_TYPE_NVLIST) {
			nvlist_t *attrs;
			attrs = fnvpair_value_nvlist(pair);
			if (nvlist_lookup_nvpair(attrs, ZPROP_VALUE,
			    &propval) != 0)
				err = SET_ERROR(EINVAL);
		}

		/* Validate value type */
		if (err == 0 && source == ZPROP_SRC_INHERITED) {
			/* inherited properties are expected to be booleans */
			if (nvpair_type(propval) != DATA_TYPE_BOOLEAN)
				err = SET_ERROR(EINVAL);
		} else if (err == 0 && prop == ZPROP_USERPROP) {
			if (zfs_prop_user(propname)) {
				if (nvpair_type(propval) != DATA_TYPE_STRING)
					err = SET_ERROR(EINVAL);
			} else if (zfs_prop_userquota(propname)) {
				if (nvpair_type(propval) !=
				    DATA_TYPE_UINT64_ARRAY)
					err = SET_ERROR(EINVAL);
			} else {
				err = SET_ERROR(EINVAL);
			}
		} else if (err == 0) {
			if (nvpair_type(propval) == DATA_TYPE_STRING) {
				if (zfs_prop_get_type(prop) != PROP_TYPE_STRING)
					err = SET_ERROR(EINVAL);
			} else if (nvpair_type(propval) == DATA_TYPE_UINT64) {
				const char *unused;

				intval = fnvpair_value_uint64(propval);

				switch (zfs_prop_get_type(prop)) {
				case PROP_TYPE_NUMBER:
					break;
				case PROP_TYPE_STRING:
					err = SET_ERROR(EINVAL);
					break;
				case PROP_TYPE_INDEX:
					if (zfs_prop_index_to_string(prop,
					    intval, &unused) != 0)
						err =
						    SET_ERROR(ZFS_ERR_BADPROP);
					break;
				default:
					cmn_err(CE_PANIC,
					    "unknown property type");
				}
			} else {
				err = SET_ERROR(EINVAL);
			}
		}

		/* Validate permissions */
		if (err == 0)
			err = zfs_check_settable(dsname, pair, CRED());

		if (err == 0) {
			if (source == ZPROP_SRC_INHERITED)
				err = -1; /* does not need special handling */
			else
				err = zfs_prop_set_special(dsname, source,
				    pair);
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

		if (err != 0) {
			if (errlist != NULL)
				fnvlist_add_int32(errlist, propname, err);
			rv = err;
		}

		if (zfs_is_namespace_prop(prop))
			should_update_mount_cache = B_TRUE;
	}

	if (nvl != retrynvl && !nvlist_empty(retrynvl)) {
		nvl = retrynvl;
		goto retry;
	}

	if (nvlist_empty(genericnvl))
		goto out;

	/*
	 * Try to set them all in one batch.
	 */
	err = dsl_props_set(dsname, source, genericnvl);
	if (err == 0)
		goto out;

	/*
	 * If batching fails, we still want to set as many properties as we
	 * can, so try setting them individually.
	 */
	pair = NULL;
	while ((pair = nvlist_next_nvpair(genericnvl, pair)) != NULL) {
		const char *propname = nvpair_name(pair);

		propval = pair;
		if (nvpair_type(pair) == DATA_TYPE_NVLIST) {
			nvlist_t *attrs;
			attrs = fnvpair_value_nvlist(pair);
			propval = fnvlist_lookup_nvpair(attrs, ZPROP_VALUE);
		}

		if (nvpair_type(propval) == DATA_TYPE_STRING) {
			strval = fnvpair_value_string(propval);
			err = dsl_prop_set_string(dsname, propname,
			    source, strval);
		} else if (nvpair_type(propval) == DATA_TYPE_BOOLEAN) {
			err = dsl_prop_inherit(dsname, propname, source);
		} else {
			intval = fnvpair_value_uint64(propval);
			err = dsl_prop_set_int(dsname, propname, source,
			    intval);
		}

		if (err != 0) {
			if (errlist != NULL) {
				fnvlist_add_int32(errlist, propname, err);
			}
			rv = err;
		}
	}

out:
	if (should_update_mount_cache)
		zfs_ioctl_update_mount_cache(dsname);

	nvlist_free(genericnvl);
	nvlist_free(retrynvl);

	return (rv);
}

/*
 * Check that all the properties are valid user properties.
 */
static int
zfs_check_userprops(nvlist_t *nvl)
{
	nvpair_t *pair = NULL;

	while ((pair = nvlist_next_nvpair(nvl, pair)) != NULL) {
		const char *propname = nvpair_name(pair);

		if (!zfs_prop_user(propname) ||
		    nvpair_type(pair) != DATA_TYPE_STRING)
			return (SET_ERROR(EINVAL));

		if (strlen(propname) >= ZAP_MAXNAMELEN)
			return (SET_ERROR(ENAMETOOLONG));

		if (strlen(fnvpair_value_string(pair)) >= ZAP_MAXVALUELEN)
			return (SET_ERROR(E2BIG));
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
clear_received_props(const char *dsname, nvlist_t *props,
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
		    (dsl_prop_get_hasrecvd(dsname) ? ZPROP_SRC_RECEIVED : 0));
		err = zfs_set_prop_nvlist(dsname, flags, cleared_props, NULL);
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
	nvlist_t *errors;
	int error;

	if ((error = get_nvlist(zc->zc_nvlist_src, zc->zc_nvlist_src_size,
	    zc->zc_iflags, &nvl)) != 0)
		return (error);

	if (received) {
		nvlist_t *origprops;

		if (dsl_prop_get_received(zc->zc_name, &origprops) == 0) {
			(void) clear_received_props(zc->zc_name,
			    origprops, nvl);
			nvlist_free(origprops);
		}

		error = dsl_prop_set_hasrecvd(zc->zc_name);
	}

	errors = fnvlist_alloc();
	if (error == 0)
		error = zfs_set_prop_nvlist(zc->zc_name, source, nvl, errors);

	if (zc->zc_nvlist_dst != 0 && errors != NULL) {
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
	nvlist_t *dummy;
	nvpair_t *pair;
	zprop_type_t type;
	int err;

	if (!received) {
		/*
		 * Only check this in the non-received case. We want to allow
		 * 'inherit -S' to revert non-inheritable properties like quota
		 * and reservation to the received or default values even though
		 * they are not considered inheritable.
		 */
		if (prop != ZPROP_USERPROP && !zfs_prop_inheritable(prop))
			return (SET_ERROR(EINVAL));
	}

	if (prop == ZPROP_USERPROP) {
		if (!zfs_prop_user(propname))
			return (SET_ERROR(EINVAL));

		type = PROP_TYPE_STRING;
	} else if (prop == ZFS_PROP_VOLSIZE || prop == ZFS_PROP_VERSION) {
		return (SET_ERROR(EINVAL));
	} else {
		type = zfs_prop_get_type(prop);
	}

	/*
	 * zfs_prop_set_special() expects properties in the form of an
	 * nvpair with type info.
	 */
	dummy = fnvlist_alloc();

	switch (type) {
	case PROP_TYPE_STRING:
		VERIFY(0 == nvlist_add_string(dummy, propname, ""));
		break;
	case PROP_TYPE_NUMBER:
	case PROP_TYPE_INDEX:
		VERIFY(0 == nvlist_add_uint64(dummy, propname, 0));
		break;
	default:
		err = SET_ERROR(EINVAL);
		goto errout;
	}

	pair = nvlist_next_nvpair(dummy, NULL);
	if (pair == NULL) {
		err = SET_ERROR(EINVAL);
	} else {
		err = zfs_prop_set_special(zc->zc_name, source, pair);
		if (err == -1) /* property is not "special", needs handling */
			err = dsl_prop_inherit(zc->zc_name, zc->zc_value,
			    source);
	}

errout:
	nvlist_free(dummy);
	return (err);
}

static int
zfs_ioc_pool_set_props(zfs_cmd_t *zc)
{
	nvlist_t *props;
	spa_t *spa;
	int error;
	nvpair_t *pair;

	if ((error = get_nvlist(zc->zc_nvlist_src, zc->zc_nvlist_src_size,
	    zc->zc_iflags, &props)))
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
			spa_write_cachefile(spa, B_FALSE, B_TRUE, B_FALSE);
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

/*
 * innvl: {
 *	"get_props_names": [ "prop1", "prop2", ..., "propN" ]
 * }
 */

static const zfs_ioc_key_t zfs_keys_get_props[] = {
	{ ZPOOL_GET_PROPS_NAMES,	DATA_TYPE_STRING_ARRAY,	ZK_OPTIONAL },
};

static int
zfs_ioc_pool_get_props(const char *pool, nvlist_t *innvl, nvlist_t *outnvl)
{
	nvlist_t *nvp = outnvl;
	spa_t *spa;
	char **props = NULL;
	unsigned int n_props = 0;
	int error;

	if (nvlist_lookup_string_array(innvl, ZPOOL_GET_PROPS_NAMES,
	    &props, &n_props) != 0) {
		props = NULL;
	}

	if ((error = spa_open(pool, &spa, FTAG)) != 0) {
		/*
		 * If the pool is faulted, there may be properties we can still
		 * get (such as altroot and cachefile), so attempt to get them
		 * anyway.
		 */
		mutex_enter(&spa_namespace_lock);
		if ((spa = spa_lookup(pool)) != NULL) {
			error = spa_prop_get(spa, &nvp);
			if (error == 0 && props != NULL)
				error = spa_prop_get_nvlist(spa, props, n_props,
				    &nvp);
		}
		mutex_exit(&spa_namespace_lock);
	} else {
		error = spa_prop_get(spa, &nvp);
		if (error == 0 && props != NULL)
			error = spa_prop_get_nvlist(spa, props, n_props, &nvp);
		spa_close(spa, FTAG);
	}

	return (error);
}

/*
 * innvl: {
 *     "vdevprops_set_vdev" -> guid
 *     "vdevprops_set_props" -> { prop -> value }
 * }
 *
 * outnvl: propname -> error code (int32)
 */
static const zfs_ioc_key_t zfs_keys_vdev_set_props[] = {
	{ZPOOL_VDEV_PROPS_SET_VDEV,	DATA_TYPE_UINT64,	0},
	{ZPOOL_VDEV_PROPS_SET_PROPS,	DATA_TYPE_NVLIST,	0}
};

static int
zfs_ioc_vdev_set_props(const char *poolname, nvlist_t *innvl, nvlist_t *outnvl)
{
	spa_t *spa;
	int error;
	vdev_t *vd;
	uint64_t vdev_guid;

	/* Early validation */
	if (nvlist_lookup_uint64(innvl, ZPOOL_VDEV_PROPS_SET_VDEV,
	    &vdev_guid) != 0)
		return (SET_ERROR(EINVAL));

	if (outnvl == NULL)
		return (SET_ERROR(EINVAL));

	if ((error = spa_open(poolname, &spa, FTAG)) != 0)
		return (error);

	ASSERT(spa_writeable(spa));

	if ((vd = spa_lookup_by_guid(spa, vdev_guid, B_TRUE)) == NULL) {
		spa_close(spa, FTAG);
		return (SET_ERROR(ENOENT));
	}

	error = vdev_prop_set(vd, innvl, outnvl);

	spa_close(spa, FTAG);

	return (error);
}

/*
 * innvl: {
 *     "vdevprops_get_vdev" -> guid
 *     (optional) "vdevprops_get_props" -> { propname -> propid }
 * }
 *
 * outnvl: propname -> value
 */
static const zfs_ioc_key_t zfs_keys_vdev_get_props[] = {
	{ZPOOL_VDEV_PROPS_GET_VDEV,	DATA_TYPE_UINT64,	0},
	{ZPOOL_VDEV_PROPS_GET_PROPS,	DATA_TYPE_NVLIST,	ZK_OPTIONAL}
};

static int
zfs_ioc_vdev_get_props(const char *poolname, nvlist_t *innvl, nvlist_t *outnvl)
{
	spa_t *spa;
	int error;
	vdev_t *vd;
	uint64_t vdev_guid;

	/* Early validation */
	if (nvlist_lookup_uint64(innvl, ZPOOL_VDEV_PROPS_GET_VDEV,
	    &vdev_guid) != 0)
		return (SET_ERROR(EINVAL));

	if (outnvl == NULL)
		return (SET_ERROR(EINVAL));

	if ((error = spa_open(poolname, &spa, FTAG)) != 0)
		return (error);

	if ((vd = spa_lookup_by_guid(spa, vdev_guid, B_TRUE)) == NULL) {
		spa_close(spa, FTAG);
		return (SET_ERROR(ENOENT));
	}

	error = vdev_prop_get(vd, innvl, outnvl);

	spa_close(spa, FTAG);

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
	if (zfs_deleg_verify_nvlist(fsaclnv) != 0) {
		nvlist_free(fsaclnv);
		return (SET_ERROR(EINVAL));
	}

	/*
	 * If we don't have PRIV_SYS_MOUNT, then validate
	 * that user is allowed to hand out each permission in
	 * the nvlist(s)
	 */

	error = secpolicy_zfs(CRED());
	if (error != 0) {
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

static void
zfs_create_cb(objset_t *os, void *arg, cred_t *cr, dmu_tx_t *tx)
{
	zfs_creat_t *zct = arg;

	zfs_create_fs(os, cr, zct->zct_zplprops, tx);
}

#define	ZFS_PROP_UNDEFINED	((uint64_t)-1)

/*
 * inputs:
 * os			parent objset pointer (NULL if root fs)
 * fuids_ok		fuids allowed in this version of the spa?
 * sa_ok		SAs allowed in this version of the spa?
 * createprops		list of properties requested by creator
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
	int error;

	ASSERT(zplprops != NULL);

	/* parent dataset must be a filesystem */
	if (os != NULL && os->os_phys->os_type != DMU_OST_ZFS)
		return (SET_ERROR(ZFS_ERR_WRONG_PARENT));

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
		return (SET_ERROR(ENOTSUP));

	/*
	 * Put the version in the zplprops
	 */
	VERIFY(nvlist_add_uint64(zplprops,
	    zfs_prop_to_name(ZFS_PROP_VERSION), zplver) == 0);

	if (norm == ZFS_PROP_UNDEFINED &&
	    (error = zfs_get_zplprop(os, ZFS_PROP_NORMALIZE, &norm)) != 0)
		return (error);
	VERIFY(nvlist_add_uint64(zplprops,
	    zfs_prop_to_name(ZFS_PROP_NORMALIZE), norm) == 0);

	/*
	 * If we're normalizing, names must always be valid UTF-8 strings.
	 */
	if (norm)
		u8 = 1;
	if (u8 == ZFS_PROP_UNDEFINED &&
	    (error = zfs_get_zplprop(os, ZFS_PROP_UTF8ONLY, &u8)) != 0)
		return (error);
	VERIFY(nvlist_add_uint64(zplprops,
	    zfs_prop_to_name(ZFS_PROP_UTF8ONLY), u8) == 0);

	if (sense == ZFS_PROP_UNDEFINED &&
	    (error = zfs_get_zplprop(os, ZFS_PROP_CASE, &sense)) != 0)
		return (error);
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
	char parentname[ZFS_MAX_DATASET_NAME_LEN];
	spa_t *spa;
	uint64_t spa_vers;
	int error;

	zfs_get_parent(dataset, parentname, sizeof (parentname));

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
 * innvl: {
 *     "type" -> dmu_objset_type_t (int32)
 *     (optional) "props" -> { prop -> value }
 *     (optional) "hidden_args" -> { "wkeydata" -> value }
 *         raw uint8_t array of encryption wrapping key data (32 bytes)
 * }
 *
 * outnvl: propname -> error code (int32)
 */

static const zfs_ioc_key_t zfs_keys_create[] = {
	{"type",	DATA_TYPE_INT32,	0},
	{"props",	DATA_TYPE_NVLIST,	ZK_OPTIONAL},
	{"hidden_args",	DATA_TYPE_NVLIST,	ZK_OPTIONAL},
};

static int
zfs_ioc_create(const char *fsname, nvlist_t *innvl, nvlist_t *outnvl)
{
	int error = 0;
	zfs_creat_t zct = { 0 };
	nvlist_t *nvprops = NULL;
	nvlist_t *hidden_args = NULL;
	void (*cbfunc)(objset_t *os, void *arg, cred_t *cr, dmu_tx_t *tx);
	dmu_objset_type_t type;
	boolean_t is_insensitive = B_FALSE;
	dsl_crypto_params_t *dcp = NULL;

	type = (dmu_objset_type_t)fnvlist_lookup_int32(innvl, "type");
	(void) nvlist_lookup_nvlist(innvl, "props", &nvprops);
	(void) nvlist_lookup_nvlist(innvl, ZPOOL_HIDDEN_ARGS, &hidden_args);

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
	if (strchr(fsname, '@') ||
	    strchr(fsname, '%'))
		return (SET_ERROR(EINVAL));

	zct.zct_props = nvprops;

	if (cbfunc == NULL)
		return (SET_ERROR(EINVAL));

	if (type == DMU_OST_ZVOL) {
		uint64_t volsize, volblocksize;

		if (nvprops == NULL)
			return (SET_ERROR(EINVAL));
		if (nvlist_lookup_uint64(nvprops,
		    zfs_prop_to_name(ZFS_PROP_VOLSIZE), &volsize) != 0)
			return (SET_ERROR(EINVAL));

		if ((error = nvlist_lookup_uint64(nvprops,
		    zfs_prop_to_name(ZFS_PROP_VOLBLOCKSIZE),
		    &volblocksize)) != 0 && error != ENOENT)
			return (SET_ERROR(EINVAL));

		if (error != 0)
			volblocksize = zfs_prop_default_numeric(
			    ZFS_PROP_VOLBLOCKSIZE);

		if ((error = zvol_check_volblocksize(fsname,
		    volblocksize)) != 0 ||
		    (error = zvol_check_volsize(volsize,
		    volblocksize)) != 0)
			return (error);
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
		error = zfs_fill_zplprops(fsname, nvprops,
		    zct.zct_zplprops, &is_insensitive);
		if (error != 0) {
			nvlist_free(zct.zct_zplprops);
			return (error);
		}
	}

	error = dsl_crypto_params_create_nvlist(DCP_CMD_NONE, nvprops,
	    hidden_args, &dcp);
	if (error != 0) {
		nvlist_free(zct.zct_zplprops);
		return (error);
	}

	error = dmu_objset_create(fsname, type,
	    is_insensitive ? DS_FLAG_CI_DATASET : 0, dcp, cbfunc, &zct);

	nvlist_free(zct.zct_zplprops);
	dsl_crypto_params_free(dcp, !!error);

	/*
	 * It would be nice to do this atomically.
	 */
	if (error == 0) {
		error = zfs_set_prop_nvlist(fsname, ZPROP_SRC_LOCAL,
		    nvprops, outnvl);
		if (error != 0) {
			spa_t *spa;
			int error2;

			/*
			 * Volumes will return EBUSY and cannot be destroyed
			 * until all asynchronous minor handling (e.g. from
			 * setting the volmode property) has completed. Wait for
			 * the spa_zvol_taskq to drain then retry.
			 */
			error2 = dsl_destroy_head(fsname);
			while ((error2 == EBUSY) && (type == DMU_OST_ZVOL)) {
				error2 = spa_open(fsname, &spa, FTAG);
				if (error2 == 0) {
					taskq_wait(spa->spa_zvol_taskq);
					spa_close(spa, FTAG);
				}
				error2 = dsl_destroy_head(fsname);
			}
		}
	}
	return (error);
}

/*
 * innvl: {
 *     "origin" -> name of origin snapshot
 *     (optional) "props" -> { prop -> value }
 *     (optional) "hidden_args" -> { "wkeydata" -> value }
 *         raw uint8_t array of encryption wrapping key data (32 bytes)
 * }
 *
 * outputs:
 * outnvl: propname -> error code (int32)
 */
static const zfs_ioc_key_t zfs_keys_clone[] = {
	{"origin",	DATA_TYPE_STRING,	0},
	{"props",	DATA_TYPE_NVLIST,	ZK_OPTIONAL},
	{"hidden_args",	DATA_TYPE_NVLIST,	ZK_OPTIONAL},
};

static int
zfs_ioc_clone(const char *fsname, nvlist_t *innvl, nvlist_t *outnvl)
{
	int error = 0;
	nvlist_t *nvprops = NULL;
	const char *origin_name;

	origin_name = fnvlist_lookup_string(innvl, "origin");
	(void) nvlist_lookup_nvlist(innvl, "props", &nvprops);

	if (strchr(fsname, '@') ||
	    strchr(fsname, '%'))
		return (SET_ERROR(EINVAL));

	if (dataset_namecheck(origin_name, NULL, NULL) != 0)
		return (SET_ERROR(EINVAL));

	error = dmu_objset_clone(fsname, origin_name);

	/*
	 * It would be nice to do this atomically.
	 */
	if (error == 0) {
		error = zfs_set_prop_nvlist(fsname, ZPROP_SRC_LOCAL,
		    nvprops, outnvl);
		if (error != 0)
			(void) dsl_destroy_head(fsname);
	}
	return (error);
}

static const zfs_ioc_key_t zfs_keys_remap[] = {
	/* no nvl keys */
};

static int
zfs_ioc_remap(const char *fsname, nvlist_t *innvl, nvlist_t *outnvl)
{
	/* This IOCTL is no longer supported. */
	(void) fsname, (void) innvl, (void) outnvl;
	return (0);
}

/*
 * innvl: {
 *     "snaps" -> { snapshot1, snapshot2 }
 *     (optional) "props" -> { prop -> value (string) }
 * }
 *
 * outnvl: snapshot -> error code (int32)
 */
static const zfs_ioc_key_t zfs_keys_snapshot[] = {
	{"snaps",	DATA_TYPE_NVLIST,	0},
	{"props",	DATA_TYPE_NVLIST,	ZK_OPTIONAL},
};

static int
zfs_ioc_snapshot(const char *poolname, nvlist_t *innvl, nvlist_t *outnvl)
{
	nvlist_t *snaps;
	nvlist_t *props = NULL;
	int error, poollen;
	nvpair_t *pair;

	(void) nvlist_lookup_nvlist(innvl, "props", &props);
	if (!nvlist_empty(props) &&
	    zfs_earlier_version(poolname, SPA_VERSION_SNAP_PROPS))
		return (SET_ERROR(ENOTSUP));
	if ((error = zfs_check_userprops(props)) != 0)
		return (error);

	snaps = fnvlist_lookup_nvlist(innvl, "snaps");
	poollen = strlen(poolname);
	for (pair = nvlist_next_nvpair(snaps, NULL); pair != NULL;
	    pair = nvlist_next_nvpair(snaps, pair)) {
		const char *name = nvpair_name(pair);
		char *cp = strchr(name, '@');

		/*
		 * The snap name must contain an @, and the part after it must
		 * contain only valid characters.
		 */
		if (cp == NULL ||
		    zfs_component_namecheck(cp + 1, NULL, NULL) != 0)
			return (SET_ERROR(EINVAL));

		/*
		 * The snap must be in the specified pool.
		 */
		if (strncmp(name, poolname, poollen) != 0 ||
		    (name[poollen] != '/' && name[poollen] != '@'))
			return (SET_ERROR(EXDEV));

		/*
		 * Check for permission to set the properties on the fs.
		 */
		if (!nvlist_empty(props)) {
			*cp = '\0';
			error = zfs_secpolicy_write_perms(name,
			    ZFS_DELEG_PERM_USERPROP, CRED());
			*cp = '@';
			if (error != 0)
				return (error);
		}

		/* This must be the only snap of this fs. */
		for (nvpair_t *pair2 = nvlist_next_nvpair(snaps, pair);
		    pair2 != NULL; pair2 = nvlist_next_nvpair(snaps, pair2)) {
			if (strncmp(name, nvpair_name(pair2), cp - name + 1)
			    == 0) {
				return (SET_ERROR(EXDEV));
			}
		}
	}

	error = dsl_dataset_snapshot(snaps, props, outnvl);

	return (error);
}

/*
 * innvl: "message" -> string
 */
static const zfs_ioc_key_t zfs_keys_log_history[] = {
	{"message",	DATA_TYPE_STRING,	0},
};

static int
zfs_ioc_log_history(const char *unused, nvlist_t *innvl, nvlist_t *outnvl)
{
	(void) unused, (void) outnvl;
	const char *message;
	char *poolname;
	spa_t *spa;
	int error;

	/*
	 * The poolname in the ioctl is not set, we get it from the TSD,
	 * which was set at the end of the last successful ioctl that allows
	 * logging.  The secpolicy func already checked that it is set.
	 * Only one log ioctl is allowed after each successful ioctl, so
	 * we clear the TSD here.
	 */
	poolname = tsd_get(zfs_allow_log_key);
	if (poolname == NULL)
		return (SET_ERROR(EINVAL));
	(void) tsd_set(zfs_allow_log_key, NULL);
	error = spa_open(poolname, &spa, FTAG);
	kmem_strfree(poolname);
	if (error != 0)
		return (error);

	message = fnvlist_lookup_string(innvl, "message");

	if (spa_version(spa) < SPA_VERSION_ZPOOL_HISTORY) {
		spa_close(spa, FTAG);
		return (SET_ERROR(ENOTSUP));
	}

	error = spa_history_log(spa, message);
	spa_close(spa, FTAG);
	return (error);
}

/*
 * This ioctl is used to set the bootenv configuration on the current
 * pool. This configuration is stored in the second padding area of the label,
 * and it is used by the bootloader(s) to store the bootloader and/or system
 * specific data.
 * The data is stored as nvlist data stream, and is protected by
 * an embedded checksum.
 * The version can have two possible values:
 * VB_RAW: nvlist should have key GRUB_ENVMAP, value DATA_TYPE_STRING.
 * VB_NVLIST: nvlist with arbitrary <key, value> pairs.
 */
static const zfs_ioc_key_t zfs_keys_set_bootenv[] = {
	{"version",	DATA_TYPE_UINT64,	0},
	{"<keys>",	DATA_TYPE_ANY, ZK_OPTIONAL | ZK_WILDCARDLIST},
};

static int
zfs_ioc_set_bootenv(const char *name, nvlist_t *innvl, nvlist_t *outnvl)
{
	int error;
	spa_t *spa;

	if ((error = spa_open(name, &spa, FTAG)) != 0)
		return (error);
	spa_vdev_state_enter(spa, SCL_ALL);
	error = vdev_label_write_bootenv(spa->spa_root_vdev, innvl);
	(void) spa_vdev_state_exit(spa, NULL, 0);
	spa_close(spa, FTAG);
	return (error);
}

static const zfs_ioc_key_t zfs_keys_get_bootenv[] = {
	/* no nvl keys */
};

static int
zfs_ioc_get_bootenv(const char *name, nvlist_t *innvl, nvlist_t *outnvl)
{
	spa_t *spa;
	int error;

	if ((error = spa_open(name, &spa, FTAG)) != 0)
		return (error);
	spa_vdev_state_enter(spa, SCL_ALL);
	error = vdev_label_read_bootenv(spa->spa_root_vdev, outnvl);
	(void) spa_vdev_state_exit(spa, NULL, 0);
	spa_close(spa, FTAG);
	return (error);
}

/*
 * The dp_config_rwlock must not be held when calling this, because the
 * unmount may need to write out data.
 *
 * This function is best-effort.  Callers must deal gracefully if it
 * remains mounted (or is remounted after this call).
 *
 * Returns 0 if the argument is not a snapshot, or it is not currently a
 * filesystem, or we were able to unmount it.  Returns error code otherwise.
 */
void
zfs_unmount_snap(const char *snapname)
{
	if (strchr(snapname, '@') == NULL)
		return;

	(void) zfsctl_snapshot_unmount(snapname, MNT_FORCE);
}

static int
zfs_unmount_snap_cb(const char *snapname, void *arg)
{
	(void) arg;
	zfs_unmount_snap(snapname);
	return (0);
}

/*
 * When a clone is destroyed, its origin may also need to be destroyed,
 * in which case it must be unmounted.  This routine will do that unmount
 * if necessary.
 */
void
zfs_destroy_unmount_origin(const char *fsname)
{
	int error;
	objset_t *os;
	dsl_dataset_t *ds;

	error = dmu_objset_hold(fsname, FTAG, &os);
	if (error != 0)
		return;
	ds = dmu_objset_ds(os);
	if (dsl_dir_is_clone(ds->ds_dir) && DS_IS_DEFER_DESTROY(ds->ds_prev)) {
		char originname[ZFS_MAX_DATASET_NAME_LEN];
		dsl_dataset_name(ds->ds_prev, originname);
		dmu_objset_rele(os, FTAG);
		zfs_unmount_snap(originname);
	} else {
		dmu_objset_rele(os, FTAG);
	}
}

/*
 * innvl: {
 *     "snaps" -> { snapshot1, snapshot2 }
 *     (optional boolean) "defer"
 * }
 *
 * outnvl: snapshot -> error code (int32)
 */
static const zfs_ioc_key_t zfs_keys_destroy_snaps[] = {
	{"snaps",	DATA_TYPE_NVLIST,	0},
	{"defer",	DATA_TYPE_BOOLEAN,	ZK_OPTIONAL},
};

static int
zfs_ioc_destroy_snaps(const char *poolname, nvlist_t *innvl, nvlist_t *outnvl)
{
	int poollen;
	nvlist_t *snaps;
	nvpair_t *pair;
	boolean_t defer;
	spa_t *spa;

	snaps = fnvlist_lookup_nvlist(innvl, "snaps");
	defer = nvlist_exists(innvl, "defer");

	poollen = strlen(poolname);
	for (pair = nvlist_next_nvpair(snaps, NULL); pair != NULL;
	    pair = nvlist_next_nvpair(snaps, pair)) {
		const char *name = nvpair_name(pair);

		/*
		 * The snap must be in the specified pool to prevent the
		 * invalid removal of zvol minors below.
		 */
		if (strncmp(name, poolname, poollen) != 0 ||
		    (name[poollen] != '/' && name[poollen] != '@'))
			return (SET_ERROR(EXDEV));

		zfs_unmount_snap(nvpair_name(pair));
		if (spa_open(name, &spa, FTAG) == 0) {
			zvol_remove_minors(spa, name, B_TRUE);
			spa_close(spa, FTAG);
		}
	}

	return (dsl_destroy_snapshots_nvl(snaps, defer, outnvl));
}

/*
 * Create bookmarks. The bookmark names are of the form <fs>#<bmark>.
 * All bookmarks and snapshots must be in the same pool.
 * dsl_bookmark_create_nvl_validate describes the nvlist schema in more detail.
 *
 * innvl: {
 *     new_bookmark1 -> existing_snapshot,
 *     new_bookmark2 -> existing_bookmark,
 * }
 *
 * outnvl: bookmark -> error code (int32)
 *
 */
static const zfs_ioc_key_t zfs_keys_bookmark[] = {
	{"<bookmark>...",	DATA_TYPE_STRING,	ZK_WILDCARDLIST},
};

static int
zfs_ioc_bookmark(const char *poolname, nvlist_t *innvl, nvlist_t *outnvl)
{
	(void) poolname;
	return (dsl_bookmark_create(innvl, outnvl));
}

/*
 * innvl: {
 *     property 1, property 2, ...
 * }
 *
 * outnvl: {
 *     bookmark name 1 -> { property 1, property 2, ... },
 *     bookmark name 2 -> { property 1, property 2, ... }
 * }
 *
 */
static const zfs_ioc_key_t zfs_keys_get_bookmarks[] = {
	{"<property>...", DATA_TYPE_BOOLEAN, ZK_WILDCARDLIST | ZK_OPTIONAL},
};

static int
zfs_ioc_get_bookmarks(const char *fsname, nvlist_t *innvl, nvlist_t *outnvl)
{
	return (dsl_get_bookmarks(fsname, innvl, outnvl));
}

/*
 * innvl is not used.
 *
 * outnvl: {
 *     property 1, property 2, ...
 * }
 *
 */
static const zfs_ioc_key_t zfs_keys_get_bookmark_props[] = {
	/* no nvl keys */
};

static int
zfs_ioc_get_bookmark_props(const char *bookmark, nvlist_t *innvl,
    nvlist_t *outnvl)
{
	(void) innvl;
	char fsname[ZFS_MAX_DATASET_NAME_LEN];
	char *bmname;

	bmname = strchr(bookmark, '#');
	if (bmname == NULL)
		return (SET_ERROR(EINVAL));
	bmname++;

	(void) strlcpy(fsname, bookmark, sizeof (fsname));
	*(strchr(fsname, '#')) = '\0';

	return (dsl_get_bookmark_props(fsname, bmname, outnvl));
}

/*
 * innvl: {
 *     bookmark name 1, bookmark name 2
 * }
 *
 * outnvl: bookmark -> error code (int32)
 *
 */
static const zfs_ioc_key_t zfs_keys_destroy_bookmarks[] = {
	{"<bookmark>...",	DATA_TYPE_BOOLEAN,	ZK_WILDCARDLIST},
};

static int
zfs_ioc_destroy_bookmarks(const char *poolname, nvlist_t *innvl,
    nvlist_t *outnvl)
{
	int error, poollen;

	poollen = strlen(poolname);
	for (nvpair_t *pair = nvlist_next_nvpair(innvl, NULL);
	    pair != NULL; pair = nvlist_next_nvpair(innvl, pair)) {
		const char *name = nvpair_name(pair);
		const char *cp = strchr(name, '#');

		/*
		 * The bookmark name must contain an #, and the part after it
		 * must contain only valid characters.
		 */
		if (cp == NULL ||
		    zfs_component_namecheck(cp + 1, NULL, NULL) != 0)
			return (SET_ERROR(EINVAL));

		/*
		 * The bookmark must be in the specified pool.
		 */
		if (strncmp(name, poolname, poollen) != 0 ||
		    (name[poollen] != '/' && name[poollen] != '#'))
			return (SET_ERROR(EXDEV));
	}

	error = dsl_bookmark_destroy(innvl, outnvl);
	return (error);
}

static const zfs_ioc_key_t zfs_keys_channel_program[] = {
	{"program",	DATA_TYPE_STRING,		0},
	{"arg",		DATA_TYPE_ANY,			0},
	{"sync",	DATA_TYPE_BOOLEAN_VALUE,	ZK_OPTIONAL},
	{"instrlimit",	DATA_TYPE_UINT64,		ZK_OPTIONAL},
	{"memlimit",	DATA_TYPE_UINT64,		ZK_OPTIONAL},
};

static int
zfs_ioc_channel_program(const char *poolname, nvlist_t *innvl,
    nvlist_t *outnvl)
{
	const char *program;
	uint64_t instrlimit, memlimit;
	boolean_t sync_flag;
	nvpair_t *nvarg = NULL;

	program = fnvlist_lookup_string(innvl, ZCP_ARG_PROGRAM);
	if (0 != nvlist_lookup_boolean_value(innvl, ZCP_ARG_SYNC, &sync_flag)) {
		sync_flag = B_TRUE;
	}
	if (0 != nvlist_lookup_uint64(innvl, ZCP_ARG_INSTRLIMIT, &instrlimit)) {
		instrlimit = ZCP_DEFAULT_INSTRLIMIT;
	}
	if (0 != nvlist_lookup_uint64(innvl, ZCP_ARG_MEMLIMIT, &memlimit)) {
		memlimit = ZCP_DEFAULT_MEMLIMIT;
	}
	nvarg = fnvlist_lookup_nvpair(innvl, ZCP_ARG_ARGLIST);

	if (instrlimit == 0 || instrlimit > zfs_lua_max_instrlimit)
		return (SET_ERROR(EINVAL));
	if (memlimit == 0 || memlimit > zfs_lua_max_memlimit)
		return (SET_ERROR(EINVAL));

	return (zcp_eval(poolname, program, sync_flag, instrlimit, memlimit,
	    nvarg, outnvl));
}

/*
 * innvl: unused
 * outnvl: empty
 */
static const zfs_ioc_key_t zfs_keys_pool_checkpoint[] = {
	/* no nvl keys */
};

static int
zfs_ioc_pool_checkpoint(const char *poolname, nvlist_t *innvl, nvlist_t *outnvl)
{
	(void) innvl, (void) outnvl;
	return (spa_checkpoint(poolname));
}

/*
 * innvl: unused
 * outnvl: empty
 */
static const zfs_ioc_key_t zfs_keys_pool_discard_checkpoint[] = {
	/* no nvl keys */
};

static int
zfs_ioc_pool_discard_checkpoint(const char *poolname, nvlist_t *innvl,
    nvlist_t *outnvl)
{
	(void) innvl, (void) outnvl;
	return (spa_checkpoint_discard(poolname));
}

/*
 * Loads specific types of data for the given pool
 *
 * innvl: {
 *     "prefetch_type" -> int32_t
 * }
 *
 * outnvl: empty
 */
static const zfs_ioc_key_t zfs_keys_pool_prefetch[] = {
	{ZPOOL_PREFETCH_TYPE,	DATA_TYPE_INT32,	0},
};

static int
zfs_ioc_pool_prefetch(const char *poolname, nvlist_t *innvl, nvlist_t *outnvl)
{
	(void) outnvl;

	int error;
	spa_t *spa;
	int32_t type;

	/*
	 * Currently, only ZPOOL_PREFETCH_DDT is supported
	 */
	if (nvlist_lookup_int32(innvl, ZPOOL_PREFETCH_TYPE, &type) != 0 ||
	    type != ZPOOL_PREFETCH_DDT) {
		return (EINVAL);
	}

	error = spa_open(poolname, &spa, FTAG);
	if (error != 0)
		return (error);

	hrtime_t start_time = gethrtime();

	ddt_prefetch_all(spa);

	zfs_dbgmsg("pool '%s': loaded ddt into ARC in %llu ms", spa->spa_name,
	    (u_longlong_t)NSEC2MSEC(gethrtime() - start_time));

	spa_close(spa, FTAG);

	return (error);
}

/*
 * inputs:
 * zc_name		name of dataset to destroy
 * zc_defer_destroy	mark for deferred destroy
 *
 * outputs:		none
 */
static int
zfs_ioc_destroy(zfs_cmd_t *zc)
{
	objset_t *os;
	dmu_objset_type_t ost;
	int err;

	err = dmu_objset_hold(zc->zc_name, FTAG, &os);
	if (err != 0)
		return (err);
	ost = dmu_objset_type(os);
	dmu_objset_rele(os, FTAG);

	if (ost == DMU_OST_ZFS)
		zfs_unmount_snap(zc->zc_name);

	if (strchr(zc->zc_name, '@')) {
		err = dsl_destroy_snapshot(zc->zc_name, zc->zc_defer_destroy);
	} else {
		err = dsl_destroy_head(zc->zc_name);
		if (err == EEXIST) {
			/*
			 * It is possible that the given DS may have
			 * hidden child (%recv) datasets - "leftovers"
			 * resulting from the previously interrupted
			 * 'zfs receive'.
			 *
			 * 6 extra bytes for /%recv
			 */
			char namebuf[ZFS_MAX_DATASET_NAME_LEN + 6];

			if (snprintf(namebuf, sizeof (namebuf), "%s/%s",
			    zc->zc_name, recv_clone_name) >=
			    sizeof (namebuf))
				return (SET_ERROR(EINVAL));

			/*
			 * Try to remove the hidden child (%recv) and after
			 * that try to remove the target dataset.
			 * If the hidden child (%recv) does not exist
			 * the original error (EEXIST) will be returned
			 */
			err = dsl_destroy_head(namebuf);
			if (err == 0)
				err = dsl_destroy_head(zc->zc_name);
			else if (err == ENOENT)
				err = SET_ERROR(EEXIST);
		}
	}

	return (err);
}

/*
 * innvl: {
 *     "initialize_command" -> POOL_INITIALIZE_{CANCEL|START|SUSPEND} (uint64)
 *     "initialize_vdevs": { -> guids to initialize (nvlist)
 *         "vdev_path_1": vdev_guid_1, (uint64),
 *         "vdev_path_2": vdev_guid_2, (uint64),
 *         ...
 *     },
 * }
 *
 * outnvl: {
 *     "initialize_vdevs": { -> initialization errors (nvlist)
 *         "vdev_path_1": errno, see function body for possible errnos (uint64)
 *         "vdev_path_2": errno, ... (uint64)
 *         ...
 *     }
 * }
 *
 * EINVAL is returned for an unknown commands or if any of the provided vdev
 * guids have be specified with a type other than uint64.
 */
static const zfs_ioc_key_t zfs_keys_pool_initialize[] = {
	{ZPOOL_INITIALIZE_COMMAND,	DATA_TYPE_UINT64,	0},
	{ZPOOL_INITIALIZE_VDEVS,	DATA_TYPE_NVLIST,	0}
};

static int
zfs_ioc_pool_initialize(const char *poolname, nvlist_t *innvl, nvlist_t *outnvl)
{
	uint64_t cmd_type;
	if (nvlist_lookup_uint64(innvl, ZPOOL_INITIALIZE_COMMAND,
	    &cmd_type) != 0) {
		return (SET_ERROR(EINVAL));
	}

	if (!(cmd_type == POOL_INITIALIZE_CANCEL ||
	    cmd_type == POOL_INITIALIZE_START ||
	    cmd_type == POOL_INITIALIZE_SUSPEND ||
	    cmd_type == POOL_INITIALIZE_UNINIT)) {
		return (SET_ERROR(EINVAL));
	}

	nvlist_t *vdev_guids;
	if (nvlist_lookup_nvlist(innvl, ZPOOL_INITIALIZE_VDEVS,
	    &vdev_guids) != 0) {
		return (SET_ERROR(EINVAL));
	}

	for (nvpair_t *pair = nvlist_next_nvpair(vdev_guids, NULL);
	    pair != NULL; pair = nvlist_next_nvpair(vdev_guids, pair)) {
		uint64_t vdev_guid;
		if (nvpair_value_uint64(pair, &vdev_guid) != 0) {
			return (SET_ERROR(EINVAL));
		}
	}

	spa_t *spa;
	int error = spa_open(poolname, &spa, FTAG);
	if (error != 0)
		return (error);

	nvlist_t *vdev_errlist = fnvlist_alloc();
	int total_errors = spa_vdev_initialize(spa, vdev_guids, cmd_type,
	    vdev_errlist);

	if (fnvlist_size(vdev_errlist) > 0) {
		fnvlist_add_nvlist(outnvl, ZPOOL_INITIALIZE_VDEVS,
		    vdev_errlist);
	}
	fnvlist_free(vdev_errlist);

	spa_close(spa, FTAG);
	return (total_errors > 0 ? SET_ERROR(EINVAL) : 0);
}

/*
 * innvl: {
 *     "trim_command" -> POOL_TRIM_{CANCEL|START|SUSPEND} (uint64)
 *     "trim_vdevs": { -> guids to TRIM (nvlist)
 *         "vdev_path_1": vdev_guid_1, (uint64),
 *         "vdev_path_2": vdev_guid_2, (uint64),
 *         ...
 *     },
 *     "trim_rate" -> Target TRIM rate in bytes/sec.
 *     "trim_secure" -> Set to request a secure TRIM.
 * }
 *
 * outnvl: {
 *     "trim_vdevs": { -> TRIM errors (nvlist)
 *         "vdev_path_1": errno, see function body for possible errnos (uint64)
 *         "vdev_path_2": errno, ... (uint64)
 *         ...
 *     }
 * }
 *
 * EINVAL is returned for an unknown commands or if any of the provided vdev
 * guids have be specified with a type other than uint64.
 */
static const zfs_ioc_key_t zfs_keys_pool_trim[] = {
	{ZPOOL_TRIM_COMMAND,	DATA_TYPE_UINT64,		0},
	{ZPOOL_TRIM_VDEVS,	DATA_TYPE_NVLIST,		0},
	{ZPOOL_TRIM_RATE,	DATA_TYPE_UINT64,		ZK_OPTIONAL},
	{ZPOOL_TRIM_SECURE,	DATA_TYPE_BOOLEAN_VALUE,	ZK_OPTIONAL},
};

static int
zfs_ioc_pool_trim(const char *poolname, nvlist_t *innvl, nvlist_t *outnvl)
{
	uint64_t cmd_type;
	if (nvlist_lookup_uint64(innvl, ZPOOL_TRIM_COMMAND, &cmd_type) != 0)
		return (SET_ERROR(EINVAL));

	if (!(cmd_type == POOL_TRIM_CANCEL ||
	    cmd_type == POOL_TRIM_START ||
	    cmd_type == POOL_TRIM_SUSPEND)) {
		return (SET_ERROR(EINVAL));
	}

	nvlist_t *vdev_guids;
	if (nvlist_lookup_nvlist(innvl, ZPOOL_TRIM_VDEVS, &vdev_guids) != 0)
		return (SET_ERROR(EINVAL));

	for (nvpair_t *pair = nvlist_next_nvpair(vdev_guids, NULL);
	    pair != NULL; pair = nvlist_next_nvpair(vdev_guids, pair)) {
		uint64_t vdev_guid;
		if (nvpair_value_uint64(pair, &vdev_guid) != 0) {
			return (SET_ERROR(EINVAL));
		}
	}

	/* Optional, defaults to maximum rate when not provided */
	uint64_t rate;
	if (nvlist_lookup_uint64(innvl, ZPOOL_TRIM_RATE, &rate) != 0)
		rate = 0;

	/* Optional, defaults to standard TRIM when not provided */
	boolean_t secure;
	if (nvlist_lookup_boolean_value(innvl, ZPOOL_TRIM_SECURE,
	    &secure) != 0) {
		secure = B_FALSE;
	}

	spa_t *spa;
	int error = spa_open(poolname, &spa, FTAG);
	if (error != 0)
		return (error);

	nvlist_t *vdev_errlist = fnvlist_alloc();
	int total_errors = spa_vdev_trim(spa, vdev_guids, cmd_type,
	    rate, !!zfs_trim_metaslab_skip, secure, vdev_errlist);

	if (fnvlist_size(vdev_errlist) > 0)
		fnvlist_add_nvlist(outnvl, ZPOOL_TRIM_VDEVS, vdev_errlist);

	fnvlist_free(vdev_errlist);

	spa_close(spa, FTAG);
	return (total_errors > 0 ? SET_ERROR(EINVAL) : 0);
}

/*
 * This ioctl waits for activity of a particular type to complete. If there is
 * no activity of that type in progress, it returns immediately, and the
 * returned value "waited" is false. If there is activity in progress, and no
 * tag is passed in, the ioctl blocks until all activity of that type is
 * complete, and then returns with "waited" set to true.
 *
 * If a tag is provided, it identifies a particular instance of an activity to
 * wait for. Currently, this is only valid for use with 'initialize', because
 * that is the only activity for which there can be multiple instances running
 * concurrently. In the case of 'initialize', the tag corresponds to the guid of
 * the vdev on which to wait.
 *
 * If a thread waiting in the ioctl receives a signal, the call will return
 * immediately, and the return value will be EINTR.
 *
 * innvl: {
 *     "wait_activity" -> int32_t
 *     (optional) "wait_tag" -> uint64_t
 * }
 *
 * outnvl: "waited" -> boolean_t
 */
static const zfs_ioc_key_t zfs_keys_pool_wait[] = {
	{ZPOOL_WAIT_ACTIVITY,	DATA_TYPE_INT32,		0},
	{ZPOOL_WAIT_TAG,	DATA_TYPE_UINT64,		ZK_OPTIONAL},
};

static int
zfs_ioc_wait(const char *name, nvlist_t *innvl, nvlist_t *outnvl)
{
	int32_t activity;
	uint64_t tag;
	boolean_t waited;
	int error;

	if (nvlist_lookup_int32(innvl, ZPOOL_WAIT_ACTIVITY, &activity) != 0)
		return (EINVAL);

	if (nvlist_lookup_uint64(innvl, ZPOOL_WAIT_TAG, &tag) == 0)
		error = spa_wait_tag(name, activity, tag, &waited);
	else
		error = spa_wait(name, activity, &waited);

	if (error == 0)
		fnvlist_add_boolean_value(outnvl, ZPOOL_WAIT_WAITED, waited);

	return (error);
}

/*
 * This ioctl waits for activity of a particular type to complete. If there is
 * no activity of that type in progress, it returns immediately, and the
 * returned value "waited" is false. If there is activity in progress, and no
 * tag is passed in, the ioctl blocks until all activity of that type is
 * complete, and then returns with "waited" set to true.
 *
 * If a thread waiting in the ioctl receives a signal, the call will return
 * immediately, and the return value will be EINTR.
 *
 * innvl: {
 *     "wait_activity" -> int32_t
 * }
 *
 * outnvl: "waited" -> boolean_t
 */
static const zfs_ioc_key_t zfs_keys_fs_wait[] = {
	{ZFS_WAIT_ACTIVITY,	DATA_TYPE_INT32,		0},
};

static int
zfs_ioc_wait_fs(const char *name, nvlist_t *innvl, nvlist_t *outnvl)
{
	int32_t activity;
	boolean_t waited = B_FALSE;
	int error;
	dsl_pool_t *dp;
	dsl_dir_t *dd;
	dsl_dataset_t *ds;

	if (nvlist_lookup_int32(innvl, ZFS_WAIT_ACTIVITY, &activity) != 0)
		return (SET_ERROR(EINVAL));

	if (activity >= ZFS_WAIT_NUM_ACTIVITIES || activity < 0)
		return (SET_ERROR(EINVAL));

	if ((error = dsl_pool_hold(name, FTAG, &dp)) != 0)
		return (error);

	if ((error = dsl_dataset_hold(dp, name, FTAG, &ds)) != 0) {
		dsl_pool_rele(dp, FTAG);
		return (error);
	}

	dd = ds->ds_dir;
	mutex_enter(&dd->dd_activity_lock);
	dd->dd_activity_waiters++;

	/*
	 * We get a long-hold here so that the dsl_dataset_t and dsl_dir_t
	 * aren't evicted while we're waiting. Normally this is prevented by
	 * holding the pool, but we can't do that while we're waiting since
	 * that would prevent TXGs from syncing out. Some of the functionality
	 * of long-holds (e.g. preventing deletion) is unnecessary for this
	 * case, since we would cancel the waiters before proceeding with a
	 * deletion. An alternative mechanism for keeping the dataset around
	 * could be developed but this is simpler.
	 */
	dsl_dataset_long_hold(ds, FTAG);
	dsl_pool_rele(dp, FTAG);

	error = dsl_dir_wait(dd, ds, activity, &waited);

	dsl_dataset_long_rele(ds, FTAG);
	dd->dd_activity_waiters--;
	if (dd->dd_activity_waiters == 0)
		cv_signal(&dd->dd_activity_cv);
	mutex_exit(&dd->dd_activity_lock);

	dsl_dataset_rele(ds, FTAG);

	if (error == 0)
		fnvlist_add_boolean_value(outnvl, ZFS_WAIT_WAITED, waited);

	return (error);
}

/*
 * fsname is name of dataset to rollback (to most recent snapshot)
 *
 * innvl may contain name of expected target snapshot
 *
 * outnvl: "target" -> name of most recent snapshot
 * }
 */
static const zfs_ioc_key_t zfs_keys_rollback[] = {
	{"target",	DATA_TYPE_STRING,	ZK_OPTIONAL},
};

static int
zfs_ioc_rollback(const char *fsname, nvlist_t *innvl, nvlist_t *outnvl)
{
	zfsvfs_t *zfsvfs;
	zvol_state_handle_t *zv;
	const char *target = NULL;
	int error;

	(void) nvlist_lookup_string(innvl, "target", &target);
	if (target != NULL) {
		const char *cp = strchr(target, '@');

		/*
		 * The snap name must contain an @, and the part after it must
		 * contain only valid characters.
		 */
		if (cp == NULL ||
		    zfs_component_namecheck(cp + 1, NULL, NULL) != 0)
			return (SET_ERROR(EINVAL));
	}

	if (getzfsvfs(fsname, &zfsvfs) == 0) {
		dsl_dataset_t *ds;

		ds = dmu_objset_ds(zfsvfs->z_os);
		error = zfs_suspend_fs(zfsvfs);
		if (error == 0) {
			int resume_err;

			error = dsl_dataset_rollback(fsname, target, zfsvfs,
			    outnvl);
			resume_err = zfs_resume_fs(zfsvfs, ds);
			error = error ? error : resume_err;
		}
		zfs_vfs_rele(zfsvfs);
	} else if ((zv = zvol_suspend(fsname)) != NULL) {
		error = dsl_dataset_rollback(fsname, target, zvol_tag(zv),
		    outnvl);
		zvol_resume(zv);
	} else {
		error = dsl_dataset_rollback(fsname, target, NULL, outnvl);
	}
	return (error);
}

static int
recursive_unmount(const char *fsname, void *arg)
{
	const char *snapname = arg;
	char *fullname;

	fullname = kmem_asprintf("%s@%s", fsname, snapname);
	zfs_unmount_snap(fullname);
	kmem_strfree(fullname);

	return (0);
}

/*
 *
 * snapname is the snapshot to redact.
 * innvl: {
 *     "bookname" -> (string)
 *         shortname of the redaction bookmark to generate
 *     "snapnv" -> (nvlist, values ignored)
 *         snapshots to redact snapname with respect to
 * }
 *
 * outnvl is unused
 */

static const zfs_ioc_key_t zfs_keys_redact[] = {
	{"bookname",		DATA_TYPE_STRING,	0},
	{"snapnv",		DATA_TYPE_NVLIST,	0},
};

static int
zfs_ioc_redact(const char *snapname, nvlist_t *innvl, nvlist_t *outnvl)
{
	(void) outnvl;
	nvlist_t *redactnvl = NULL;
	const char *redactbook = NULL;

	if (nvlist_lookup_nvlist(innvl, "snapnv", &redactnvl) != 0)
		return (SET_ERROR(EINVAL));
	if (fnvlist_num_pairs(redactnvl) == 0)
		return (SET_ERROR(ENXIO));
	if (nvlist_lookup_string(innvl, "bookname", &redactbook) != 0)
		return (SET_ERROR(EINVAL));

	return (dmu_redact_snap(snapname, redactnvl, redactbook));
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
	objset_t *os;
	dmu_objset_type_t ost;
	boolean_t recursive = zc->zc_cookie & 1;
	boolean_t nounmount = !!(zc->zc_cookie & 2);
	char *at;
	int err;

	/* "zfs rename" from and to ...%recv datasets should both fail */
	zc->zc_name[sizeof (zc->zc_name) - 1] = '\0';
	zc->zc_value[sizeof (zc->zc_value) - 1] = '\0';
	if (dataset_namecheck(zc->zc_name, NULL, NULL) != 0 ||
	    dataset_namecheck(zc->zc_value, NULL, NULL) != 0 ||
	    strchr(zc->zc_name, '%') || strchr(zc->zc_value, '%'))
		return (SET_ERROR(EINVAL));

	err = dmu_objset_hold(zc->zc_name, FTAG, &os);
	if (err != 0)
		return (err);
	ost = dmu_objset_type(os);
	dmu_objset_rele(os, FTAG);

	at = strchr(zc->zc_name, '@');
	if (at != NULL) {
		/* snaps must be in same fs */
		int error;

		if (strncmp(zc->zc_name, zc->zc_value, at - zc->zc_name + 1))
			return (SET_ERROR(EXDEV));
		*at = '\0';
		if (ost == DMU_OST_ZFS && !nounmount) {
			error = dmu_objset_find(zc->zc_name,
			    recursive_unmount, at + 1,
			    recursive ? DS_FIND_CHILDREN : 0);
			if (error != 0) {
				*at = '@';
				return (error);
			}
		}
		error = dsl_dataset_rename_snapshot(zc->zc_name,
		    at + 1, strchr(zc->zc_value, '@') + 1, recursive);
		*at = '@';

		return (error);
	} else {
		return (dsl_dir_rename(zc->zc_name, zc->zc_value));
	}
}

static int
zfs_check_settable(const char *dsname, nvpair_t *pair, cred_t *cr)
{
	const char *propname = nvpair_name(pair);
	boolean_t issnap = (strchr(dsname, '@') != NULL);
	zfs_prop_t prop = zfs_name_to_prop(propname);
	uint64_t intval, compval;
	int err;

	if (prop == ZPROP_USERPROP) {
		if (zfs_prop_user(propname)) {
			if ((err = zfs_secpolicy_write_perms(dsname,
			    ZFS_DELEG_PERM_USERPROP, cr)))
				return (err);
			return (0);
		}

		if (!issnap && zfs_prop_userquota(propname)) {
			const char *perm = NULL;
			const char *uq_prefix =
			    zfs_userquota_prop_prefixes[ZFS_PROP_USERQUOTA];
			const char *gq_prefix =
			    zfs_userquota_prop_prefixes[ZFS_PROP_GROUPQUOTA];
			const char *uiq_prefix =
			    zfs_userquota_prop_prefixes[ZFS_PROP_USEROBJQUOTA];
			const char *giq_prefix =
			    zfs_userquota_prop_prefixes[ZFS_PROP_GROUPOBJQUOTA];
			const char *pq_prefix =
			    zfs_userquota_prop_prefixes[ZFS_PROP_PROJECTQUOTA];
			const char *piq_prefix = zfs_userquota_prop_prefixes[\
			    ZFS_PROP_PROJECTOBJQUOTA];

			if (strncmp(propname, uq_prefix,
			    strlen(uq_prefix)) == 0) {
				perm = ZFS_DELEG_PERM_USERQUOTA;
			} else if (strncmp(propname, uiq_prefix,
			    strlen(uiq_prefix)) == 0) {
				perm = ZFS_DELEG_PERM_USEROBJQUOTA;
			} else if (strncmp(propname, gq_prefix,
			    strlen(gq_prefix)) == 0) {
				perm = ZFS_DELEG_PERM_GROUPQUOTA;
			} else if (strncmp(propname, giq_prefix,
			    strlen(giq_prefix)) == 0) {
				perm = ZFS_DELEG_PERM_GROUPOBJQUOTA;
			} else if (strncmp(propname, pq_prefix,
			    strlen(pq_prefix)) == 0) {
				perm = ZFS_DELEG_PERM_PROJECTQUOTA;
			} else if (strncmp(propname, piq_prefix,
			    strlen(piq_prefix)) == 0) {
				perm = ZFS_DELEG_PERM_PROJECTOBJQUOTA;
			} else {
				/* {USER|GROUP|PROJECT}USED are read-only */
				return (SET_ERROR(EINVAL));
			}

			if ((err = zfs_secpolicy_write_perms(dsname, perm, cr)))
				return (err);
			return (0);
		}

		return (SET_ERROR(EINVAL));
	}

	if (issnap)
		return (SET_ERROR(EINVAL));

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
		if (nvpair_value_uint64(pair, &intval) == 0) {
			compval = ZIO_COMPRESS_ALGO(intval);
			if (compval >= ZIO_COMPRESS_GZIP_1 &&
			    compval <= ZIO_COMPRESS_GZIP_9 &&
			    zfs_earlier_version(dsname,
			    SPA_VERSION_GZIP_COMPRESSION)) {
				return (SET_ERROR(ENOTSUP));
			}

			if (compval == ZIO_COMPRESS_ZLE &&
			    zfs_earlier_version(dsname,
			    SPA_VERSION_ZLE_COMPRESSION))
				return (SET_ERROR(ENOTSUP));

			if (compval == ZIO_COMPRESS_LZ4) {
				spa_t *spa;

				if ((err = spa_open(dsname, &spa, FTAG)) != 0)
					return (err);

				if (!spa_feature_is_enabled(spa,
				    SPA_FEATURE_LZ4_COMPRESS)) {
					spa_close(spa, FTAG);
					return (SET_ERROR(ENOTSUP));
				}
				spa_close(spa, FTAG);
			}

			if (compval == ZIO_COMPRESS_ZSTD) {
				spa_t *spa;

				if ((err = spa_open(dsname, &spa, FTAG)) != 0)
					return (err);

				if (!spa_feature_is_enabled(spa,
				    SPA_FEATURE_ZSTD_COMPRESS)) {
					spa_close(spa, FTAG);
					return (SET_ERROR(ENOTSUP));
				}
				spa_close(spa, FTAG);
			}
		}
		break;

	case ZFS_PROP_COPIES:
		if (zfs_earlier_version(dsname, SPA_VERSION_DITTO_BLOCKS))
			return (SET_ERROR(ENOTSUP));
		break;

	case ZFS_PROP_VOLBLOCKSIZE:
	case ZFS_PROP_RECORDSIZE:
		/* Record sizes above 128k need the feature to be enabled */
		if (nvpair_value_uint64(pair, &intval) == 0 &&
		    intval > SPA_OLD_MAXBLOCKSIZE) {
			spa_t *spa;

			/*
			 * We don't allow setting the property above 1MB,
			 * unless the tunable has been changed.
			 */
			if (intval > zfs_max_recordsize ||
			    intval > SPA_MAXBLOCKSIZE)
				return (SET_ERROR(ERANGE));

			if ((err = spa_open(dsname, &spa, FTAG)) != 0)
				return (err);

			if (!spa_feature_is_enabled(spa,
			    SPA_FEATURE_LARGE_BLOCKS)) {
				spa_close(spa, FTAG);
				return (SET_ERROR(ENOTSUP));
			}
			spa_close(spa, FTAG);
		}
		break;

	case ZFS_PROP_DNODESIZE:
		/* Dnode sizes above 512 need the feature to be enabled */
		if (nvpair_value_uint64(pair, &intval) == 0 &&
		    intval != ZFS_DNSIZE_LEGACY) {
			spa_t *spa;

			if ((err = spa_open(dsname, &spa, FTAG)) != 0)
				return (err);

			if (!spa_feature_is_enabled(spa,
			    SPA_FEATURE_LARGE_DNODE)) {
				spa_close(spa, FTAG);
				return (SET_ERROR(ENOTSUP));
			}
			spa_close(spa, FTAG);
		}
		break;

	case ZFS_PROP_SPECIAL_SMALL_BLOCKS:
		/*
		 * This property could require the allocation classes
		 * feature to be active for setting, however we allow
		 * it so that tests of settable properties succeed.
		 * The CLI will issue a warning in this case.
		 */
		break;

	case ZFS_PROP_SHARESMB:
		if (zpl_earlier_version(dsname, ZPL_VERSION_FUID))
			return (SET_ERROR(ENOTSUP));
		break;

	case ZFS_PROP_ACLINHERIT:
		if (nvpair_type(pair) == DATA_TYPE_UINT64 &&
		    nvpair_value_uint64(pair, &intval) == 0) {
			if (intval == ZFS_ACL_PASSTHROUGH_X &&
			    zfs_earlier_version(dsname,
			    SPA_VERSION_PASSTHROUGH_X))
				return (SET_ERROR(ENOTSUP));
		}
		break;
	case ZFS_PROP_CHECKSUM:
	case ZFS_PROP_DEDUP:
	{
		spa_feature_t feature;
		spa_t *spa;
		int err;

		/* dedup feature version checks */
		if (prop == ZFS_PROP_DEDUP &&
		    zfs_earlier_version(dsname, SPA_VERSION_DEDUP))
			return (SET_ERROR(ENOTSUP));

		if (nvpair_type(pair) == DATA_TYPE_UINT64 &&
		    nvpair_value_uint64(pair, &intval) == 0) {
			/* check prop value is enabled in features */
			feature = zio_checksum_to_feature(
			    intval & ZIO_CHECKSUM_MASK);
			if (feature == SPA_FEATURE_NONE)
				break;

			if ((err = spa_open(dsname, &spa, FTAG)) != 0)
				return (err);

			if (!spa_feature_is_enabled(spa, feature)) {
				spa_close(spa, FTAG);
				return (SET_ERROR(ENOTSUP));
			}
			spa_close(spa, FTAG);
		}
		break;
	}

	default:
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
zfs_check_clearable(const char *dataset, nvlist_t *props, nvlist_t **errlist)
{
	zfs_cmd_t *zc;
	nvpair_t *pair, *next_pair;
	nvlist_t *errors;
	int err, rv = 0;

	if (props == NULL)
		return (0);

	VERIFY(nvlist_alloc(&errors, NV_UNIQUE_NAME, KM_SLEEP) == 0);

	zc = kmem_alloc(sizeof (zfs_cmd_t), KM_SLEEP);
	(void) strlcpy(zc->zc_name, dataset, sizeof (zc->zc_name));
	pair = nvlist_next_nvpair(props, NULL);
	while (pair != NULL) {
		next_pair = nvlist_next_nvpair(props, pair);

		(void) strlcpy(zc->zc_value, nvpair_name(pair),
		    sizeof (zc->zc_value));
		if ((err = zfs_check_settable(dataset, pair, CRED())) != 0 ||
		    (err = zfs_secpolicy_inherit_prop(zc, NULL, CRED())) != 0) {
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
		const char *valstr1, *valstr2;

		VERIFY(nvpair_value_string(p1, &valstr1) == 0);
		VERIFY(nvpair_value_string(p2, &valstr2) == 0);
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

/*
 * Extract properties that cannot be set PRIOR to the receipt of a dataset.
 * For example, refquota cannot be set until after the receipt of a dataset,
 * because in replication streams, an older/earlier snapshot may exceed the
 * refquota.  We want to receive the older/earlier snapshot, but setting
 * refquota pre-receipt will set the dsl's ACTUAL quota, which will prevent
 * the older/earlier snapshot from being received (with EDQUOT).
 *
 * The ZFS test "zfs_receive_011_pos" demonstrates such a scenario.
 *
 * libzfs will need to be judicious handling errors encountered by props
 * extracted by this function.
 */
static nvlist_t *
extract_delay_props(nvlist_t *props)
{
	nvlist_t *delayprops;
	nvpair_t *nvp, *tmp;
	static const zfs_prop_t delayable[] = {
		ZFS_PROP_REFQUOTA,
		ZFS_PROP_KEYLOCATION,
		/*
		 * Setting ZFS_PROP_SHARESMB requires the objset type to be
		 * known, which is not possible prior to receipt of raw sends.
		 */
		ZFS_PROP_SHARESMB,
		0
	};
	int i;

	VERIFY(nvlist_alloc(&delayprops, NV_UNIQUE_NAME, KM_SLEEP) == 0);

	for (nvp = nvlist_next_nvpair(props, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(props, nvp)) {
		/*
		 * strcmp() is safe because zfs_prop_to_name() always returns
		 * a bounded string.
		 */
		for (i = 0; delayable[i] != 0; i++) {
			if (strcmp(zfs_prop_to_name(delayable[i]),
			    nvpair_name(nvp)) == 0) {
				break;
			}
		}
		if (delayable[i] != 0) {
			tmp = nvlist_prev_nvpair(props, nvp);
			VERIFY(nvlist_add_nvpair(delayprops, nvp) == 0);
			VERIFY(nvlist_remove_nvpair(props, nvp) == 0);
			nvp = tmp;
		}
	}

	if (nvlist_empty(delayprops)) {
		nvlist_free(delayprops);
		delayprops = NULL;
	}
	return (delayprops);
}

static void
zfs_allow_log_destroy(void *arg)
{
	char *poolname = arg;

	if (poolname != NULL)
		kmem_strfree(poolname);
}

#ifdef	ZFS_DEBUG
static boolean_t zfs_ioc_recv_inject_err;
#endif

/*
 * nvlist 'errors' is always allocated. It will contain descriptions of
 * encountered errors, if any. It's the callers responsibility to free.
 */
static int
zfs_ioc_recv_impl(char *tofs, char *tosnap, const char *origin,
    nvlist_t *recvprops, nvlist_t *localprops, nvlist_t *hidden_args,
    boolean_t force, boolean_t heal, boolean_t resumable, int input_fd,
    dmu_replay_record_t *begin_record, uint64_t *read_bytes,
    uint64_t *errflags, nvlist_t **errors)
{
	dmu_recv_cookie_t drc;
	int error = 0;
	int props_error = 0;
	offset_t off, noff;
	nvlist_t *local_delayprops = NULL;
	nvlist_t *recv_delayprops = NULL;
	nvlist_t *inherited_delayprops = NULL;
	nvlist_t *origprops = NULL; /* existing properties */
	nvlist_t *origrecvd = NULL; /* existing received properties */
	boolean_t first_recvd_props = B_FALSE;
	boolean_t tofs_was_redacted;
	zfs_file_t *input_fp;

	*read_bytes = 0;
	*errflags = 0;
	*errors = fnvlist_alloc();
	off = 0;

	if ((input_fp = zfs_file_get(input_fd)) == NULL)
		return (SET_ERROR(EBADF));

	noff = off = zfs_file_off(input_fp);
	error = dmu_recv_begin(tofs, tosnap, begin_record, force, heal,
	    resumable, localprops, hidden_args, origin, &drc, input_fp,
	    &off);
	if (error != 0)
		goto out;
	tofs_was_redacted = dsl_get_redacted(drc.drc_ds);

	/*
	 * Set properties before we receive the stream so that they are applied
	 * to the new data. Note that we must call dmu_recv_stream() if
	 * dmu_recv_begin() succeeds.
	 */
	if (recvprops != NULL && !drc.drc_newfs) {
		if (spa_version(dsl_dataset_get_spa(drc.drc_ds)) >=
		    SPA_VERSION_RECVD_PROPS &&
		    !dsl_prop_get_hasrecvd(tofs))
			first_recvd_props = B_TRUE;

		/*
		 * If new received properties are supplied, they are to
		 * completely replace the existing received properties,
		 * so stash away the existing ones.
		 */
		if (dsl_prop_get_received(tofs, &origrecvd) == 0) {
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
				props_reduce(recvprops, origrecvd);
			if (zfs_check_clearable(tofs, origrecvd, &errlist) != 0)
				(void) nvlist_merge(*errors, errlist, 0);
			nvlist_free(errlist);

			if (clear_received_props(tofs, origrecvd,
			    first_recvd_props ? NULL : recvprops) != 0)
				*errflags |= ZPROP_ERR_NOCLEAR;
		} else {
			*errflags |= ZPROP_ERR_NOCLEAR;
		}
	}

	/*
	 * Stash away existing properties so we can restore them on error unless
	 * we're doing the first receive after SPA_VERSION_RECVD_PROPS, in which
	 * case "origrecvd" will take care of that.
	 */
	if (localprops != NULL && !drc.drc_newfs && !first_recvd_props) {
		objset_t *os;
		if (dmu_objset_hold(tofs, FTAG, &os) == 0) {
			if (dsl_prop_get_all(os, &origprops) != 0) {
				*errflags |= ZPROP_ERR_NOCLEAR;
			}
			dmu_objset_rele(os, FTAG);
		} else {
			*errflags |= ZPROP_ERR_NOCLEAR;
		}
	}

	if (recvprops != NULL) {
		props_error = dsl_prop_set_hasrecvd(tofs);

		if (props_error == 0) {
			recv_delayprops = extract_delay_props(recvprops);
			(void) zfs_set_prop_nvlist(tofs, ZPROP_SRC_RECEIVED,
			    recvprops, *errors);
		}
	}

	if (localprops != NULL) {
		nvlist_t *oprops = fnvlist_alloc();
		nvlist_t *xprops = fnvlist_alloc();
		nvpair_t *nvp = NULL;

		while ((nvp = nvlist_next_nvpair(localprops, nvp)) != NULL) {
			if (nvpair_type(nvp) == DATA_TYPE_BOOLEAN) {
				/* -x property */
				const char *name = nvpair_name(nvp);
				zfs_prop_t prop = zfs_name_to_prop(name);
				if (prop != ZPROP_USERPROP) {
					if (!zfs_prop_inheritable(prop))
						continue;
				} else if (!zfs_prop_user(name))
					continue;
				fnvlist_add_boolean(xprops, name);
			} else {
				/* -o property=value */
				fnvlist_add_nvpair(oprops, nvp);
			}
		}

		local_delayprops = extract_delay_props(oprops);
		(void) zfs_set_prop_nvlist(tofs, ZPROP_SRC_LOCAL,
		    oprops, *errors);
		inherited_delayprops = extract_delay_props(xprops);
		(void) zfs_set_prop_nvlist(tofs, ZPROP_SRC_INHERITED,
		    xprops, *errors);

		nvlist_free(oprops);
		nvlist_free(xprops);
	}

	error = dmu_recv_stream(&drc, &off);

	if (error == 0) {
		zfsvfs_t *zfsvfs = NULL;
		zvol_state_handle_t *zv = NULL;

		if (getzfsvfs(tofs, &zfsvfs) == 0) {
			/* online recv */
			dsl_dataset_t *ds;
			int end_err;
			boolean_t stream_is_redacted = DMU_GET_FEATUREFLAGS(
			    begin_record->drr_u.drr_begin.
			    drr_versioninfo) & DMU_BACKUP_FEATURE_REDACTED;

			ds = dmu_objset_ds(zfsvfs->z_os);
			error = zfs_suspend_fs(zfsvfs);
			/*
			 * If the suspend fails, then the recv_end will
			 * likely also fail, and clean up after itself.
			 */
			end_err = dmu_recv_end(&drc, zfsvfs);
			/*
			 * If the dataset was not redacted, but we received a
			 * redacted stream onto it, we need to unmount the
			 * dataset.  Otherwise, resume the filesystem.
			 */
			if (error == 0 && !drc.drc_newfs &&
			    stream_is_redacted && !tofs_was_redacted) {
				error = zfs_end_fs(zfsvfs, ds);
			} else if (error == 0) {
				error = zfs_resume_fs(zfsvfs, ds);
			}
			error = error ? error : end_err;
			zfs_vfs_rele(zfsvfs);
		} else if ((zv = zvol_suspend(tofs)) != NULL) {
			error = dmu_recv_end(&drc, zvol_tag(zv));
			zvol_resume(zv);
		} else {
			error = dmu_recv_end(&drc, NULL);
		}

		/* Set delayed properties now, after we're done receiving. */
		if (recv_delayprops != NULL && error == 0) {
			(void) zfs_set_prop_nvlist(tofs, ZPROP_SRC_RECEIVED,
			    recv_delayprops, *errors);
		}
		if (local_delayprops != NULL && error == 0) {
			(void) zfs_set_prop_nvlist(tofs, ZPROP_SRC_LOCAL,
			    local_delayprops, *errors);
		}
		if (inherited_delayprops != NULL && error == 0) {
			(void) zfs_set_prop_nvlist(tofs, ZPROP_SRC_INHERITED,
			    inherited_delayprops, *errors);
		}
	}

	/*
	 * Merge delayed props back in with initial props, in case
	 * we're DEBUG and zfs_ioc_recv_inject_err is set (which means
	 * we have to make sure clear_received_props() includes
	 * the delayed properties).
	 *
	 * Since zfs_ioc_recv_inject_err is only in DEBUG kernels,
	 * using ASSERT() will be just like a VERIFY.
	 */
	if (recv_delayprops != NULL) {
		ASSERT(nvlist_merge(recvprops, recv_delayprops, 0) == 0);
		nvlist_free(recv_delayprops);
	}
	if (local_delayprops != NULL) {
		ASSERT(nvlist_merge(localprops, local_delayprops, 0) == 0);
		nvlist_free(local_delayprops);
	}
	if (inherited_delayprops != NULL) {
		ASSERT(nvlist_merge(localprops, inherited_delayprops, 0) == 0);
		nvlist_free(inherited_delayprops);
	}
	*read_bytes = off - noff;

#ifdef	ZFS_DEBUG
	if (zfs_ioc_recv_inject_err) {
		zfs_ioc_recv_inject_err = B_FALSE;
		error = 1;
	}
#endif

	/*
	 * On error, restore the original props.
	 */
	if (error != 0 && recvprops != NULL && !drc.drc_newfs) {
		if (clear_received_props(tofs, recvprops, NULL) != 0) {
			/*
			 * We failed to clear the received properties.
			 * Since we may have left a $recvd value on the
			 * system, we can't clear the $hasrecvd flag.
			 */
			*errflags |= ZPROP_ERR_NORESTORE;
		} else if (first_recvd_props) {
			dsl_prop_unset_hasrecvd(tofs);
		}

		if (origrecvd == NULL && !drc.drc_newfs) {
			/* We failed to stash the original properties. */
			*errflags |= ZPROP_ERR_NORESTORE;
		}

		/*
		 * dsl_props_set() will not convert RECEIVED to LOCAL on or
		 * after SPA_VERSION_RECVD_PROPS, so we need to specify LOCAL
		 * explicitly if we're restoring local properties cleared in the
		 * first new-style receive.
		 */
		if (origrecvd != NULL &&
		    zfs_set_prop_nvlist(tofs, (first_recvd_props ?
		    ZPROP_SRC_LOCAL : ZPROP_SRC_RECEIVED),
		    origrecvd, NULL) != 0) {
			/*
			 * We stashed the original properties but failed to
			 * restore them.
			 */
			*errflags |= ZPROP_ERR_NORESTORE;
		}
	}
	if (error != 0 && localprops != NULL && !drc.drc_newfs &&
	    !first_recvd_props) {
		nvlist_t *setprops;
		nvlist_t *inheritprops;
		nvpair_t *nvp;

		if (origprops == NULL) {
			/* We failed to stash the original properties. */
			*errflags |= ZPROP_ERR_NORESTORE;
			goto out;
		}

		/* Restore original props */
		setprops = fnvlist_alloc();
		inheritprops = fnvlist_alloc();
		nvp = NULL;
		while ((nvp = nvlist_next_nvpair(localprops, nvp)) != NULL) {
			const char *name = nvpair_name(nvp);
			const char *source;
			nvlist_t *attrs;

			if (!nvlist_exists(origprops, name)) {
				/*
				 * Property was not present or was explicitly
				 * inherited before the receive, restore this.
				 */
				fnvlist_add_boolean(inheritprops, name);
				continue;
			}
			attrs = fnvlist_lookup_nvlist(origprops, name);
			source = fnvlist_lookup_string(attrs, ZPROP_SOURCE);

			/* Skip received properties */
			if (strcmp(source, ZPROP_SOURCE_VAL_RECVD) == 0)
				continue;

			if (strcmp(source, tofs) == 0) {
				/* Property was locally set */
				fnvlist_add_nvlist(setprops, name, attrs);
			} else {
				/* Property was implicitly inherited */
				fnvlist_add_boolean(inheritprops, name);
			}
		}

		if (zfs_set_prop_nvlist(tofs, ZPROP_SRC_LOCAL, setprops,
		    NULL) != 0)
			*errflags |= ZPROP_ERR_NORESTORE;
		if (zfs_set_prop_nvlist(tofs, ZPROP_SRC_INHERITED, inheritprops,
		    NULL) != 0)
			*errflags |= ZPROP_ERR_NORESTORE;

		nvlist_free(setprops);
		nvlist_free(inheritprops);
	}
out:
	zfs_file_put(input_fp);
	nvlist_free(origrecvd);
	nvlist_free(origprops);

	if (error == 0)
		error = props_error;

	return (error);
}

/*
 * inputs:
 * zc_name		name of containing filesystem (unused)
 * zc_nvlist_src{_size}	nvlist of properties to apply
 * zc_nvlist_conf{_size}	nvlist of properties to exclude
 *			(DATA_TYPE_BOOLEAN) and override (everything else)
 * zc_value		name of snapshot to create
 * zc_string		name of clone origin (if DRR_FLAG_CLONE)
 * zc_cookie		file descriptor to recv from
 * zc_begin_record	the BEGIN record of the stream (not byteswapped)
 * zc_guid		force flag
 *
 * outputs:
 * zc_cookie		number of bytes read
 * zc_obj		zprop_errflags_t
 * zc_nvlist_dst{_size} error for each unapplied received property
 */
static int
zfs_ioc_recv(zfs_cmd_t *zc)
{
	dmu_replay_record_t begin_record;
	nvlist_t *errors = NULL;
	nvlist_t *recvdprops = NULL;
	nvlist_t *localprops = NULL;
	const char *origin = NULL;
	char *tosnap;
	char tofs[ZFS_MAX_DATASET_NAME_LEN];
	int error = 0;

	if (dataset_namecheck(zc->zc_value, NULL, NULL) != 0 ||
	    strchr(zc->zc_value, '@') == NULL ||
	    strchr(zc->zc_value, '%') != NULL) {
		return (SET_ERROR(EINVAL));
	}

	(void) strlcpy(tofs, zc->zc_value, sizeof (tofs));
	tosnap = strchr(tofs, '@');
	*tosnap++ = '\0';

	if (zc->zc_nvlist_src != 0 &&
	    (error = get_nvlist(zc->zc_nvlist_src, zc->zc_nvlist_src_size,
	    zc->zc_iflags, &recvdprops)) != 0) {
		goto out;
	}

	if (zc->zc_nvlist_conf != 0 &&
	    (error = get_nvlist(zc->zc_nvlist_conf, zc->zc_nvlist_conf_size,
	    zc->zc_iflags, &localprops)) != 0) {
		goto out;
	}

	if (zc->zc_string[0])
		origin = zc->zc_string;

	begin_record.drr_type = DRR_BEGIN;
	begin_record.drr_payloadlen = 0;
	begin_record.drr_u.drr_begin = zc->zc_begin_record;

	error = zfs_ioc_recv_impl(tofs, tosnap, origin, recvdprops, localprops,
	    NULL, zc->zc_guid, B_FALSE, B_FALSE, zc->zc_cookie, &begin_record,
	    &zc->zc_cookie, &zc->zc_obj, &errors);

	/*
	 * Now that all props, initial and delayed, are set, report the prop
	 * errors to the caller.
	 */
	if (zc->zc_nvlist_dst_size != 0 && errors != NULL &&
	    (nvlist_smush(errors, zc->zc_nvlist_dst_size) != 0 ||
	    put_nvlist(zc, errors) != 0)) {
		/*
		 * Caller made zc->zc_nvlist_dst less than the minimum expected
		 * size or supplied an invalid address.
		 */
		error = SET_ERROR(EINVAL);
	}

out:
	nvlist_free(errors);
	nvlist_free(recvdprops);
	nvlist_free(localprops);

	return (error);
}

/*
 * innvl: {
 *     "snapname" -> full name of the snapshot to create
 *     (optional) "props" -> received properties to set (nvlist)
 *     (optional) "localprops" -> override and exclude properties (nvlist)
 *     (optional) "origin" -> name of clone origin (DRR_FLAG_CLONE)
 *     "begin_record" -> non-byteswapped dmu_replay_record_t
 *     "input_fd" -> file descriptor to read stream from (int32)
 *     (optional) "force" -> force flag (value ignored)
 *     (optional) "heal" -> use send stream to heal data corruption
 *     (optional) "resumable" -> resumable flag (value ignored)
 *     (optional) "cleanup_fd" -> unused
 *     (optional) "action_handle" -> unused
 *     (optional) "hidden_args" -> { "wkeydata" -> value }
 * }
 *
 * outnvl: {
 *     "read_bytes" -> number of bytes read
 *     "error_flags" -> zprop_errflags_t
 *     "errors" -> error for each unapplied received property (nvlist)
 * }
 */
static const zfs_ioc_key_t zfs_keys_recv_new[] = {
	{"snapname",		DATA_TYPE_STRING,	0},
	{"props",		DATA_TYPE_NVLIST,	ZK_OPTIONAL},
	{"localprops",		DATA_TYPE_NVLIST,	ZK_OPTIONAL},
	{"origin",		DATA_TYPE_STRING,	ZK_OPTIONAL},
	{"begin_record",	DATA_TYPE_BYTE_ARRAY,	0},
	{"input_fd",		DATA_TYPE_INT32,	0},
	{"force",		DATA_TYPE_BOOLEAN,	ZK_OPTIONAL},
	{"heal",		DATA_TYPE_BOOLEAN,	ZK_OPTIONAL},
	{"resumable",		DATA_TYPE_BOOLEAN,	ZK_OPTIONAL},
	{"cleanup_fd",		DATA_TYPE_INT32,	ZK_OPTIONAL},
	{"action_handle",	DATA_TYPE_UINT64,	ZK_OPTIONAL},
	{"hidden_args",		DATA_TYPE_NVLIST,	ZK_OPTIONAL},
};

static int
zfs_ioc_recv_new(const char *fsname, nvlist_t *innvl, nvlist_t *outnvl)
{
	dmu_replay_record_t *begin_record;
	uint_t begin_record_size;
	nvlist_t *errors = NULL;
	nvlist_t *recvprops = NULL;
	nvlist_t *localprops = NULL;
	nvlist_t *hidden_args = NULL;
	const char *snapname;
	const char *origin = NULL;
	char *tosnap;
	char tofs[ZFS_MAX_DATASET_NAME_LEN];
	boolean_t force;
	boolean_t heal;
	boolean_t resumable;
	uint64_t read_bytes = 0;
	uint64_t errflags = 0;
	int input_fd = -1;
	int error;

	snapname = fnvlist_lookup_string(innvl, "snapname");

	if (dataset_namecheck(snapname, NULL, NULL) != 0 ||
	    strchr(snapname, '@') == NULL ||
	    strchr(snapname, '%') != NULL) {
		return (SET_ERROR(EINVAL));
	}

	(void) strlcpy(tofs, snapname, sizeof (tofs));
	tosnap = strchr(tofs, '@');
	*tosnap++ = '\0';

	error = nvlist_lookup_string(innvl, "origin", &origin);
	if (error && error != ENOENT)
		return (error);

	error = nvlist_lookup_byte_array(innvl, "begin_record",
	    (uchar_t **)&begin_record, &begin_record_size);
	if (error != 0 || begin_record_size != sizeof (*begin_record))
		return (SET_ERROR(EINVAL));

	input_fd = fnvlist_lookup_int32(innvl, "input_fd");

	force = nvlist_exists(innvl, "force");
	heal = nvlist_exists(innvl, "heal");
	resumable = nvlist_exists(innvl, "resumable");

	/* we still use "props" here for backwards compatibility */
	error = nvlist_lookup_nvlist(innvl, "props", &recvprops);
	if (error && error != ENOENT)
		goto out;

	error = nvlist_lookup_nvlist(innvl, "localprops", &localprops);
	if (error && error != ENOENT)
		goto out;

	error = nvlist_lookup_nvlist(innvl, ZPOOL_HIDDEN_ARGS, &hidden_args);
	if (error && error != ENOENT)
		goto out;

	error = zfs_ioc_recv_impl(tofs, tosnap, origin, recvprops, localprops,
	    hidden_args, force, heal, resumable, input_fd, begin_record,
	    &read_bytes, &errflags, &errors);

	fnvlist_add_uint64(outnvl, "read_bytes", read_bytes);
	fnvlist_add_uint64(outnvl, "error_flags", errflags);
	fnvlist_add_nvlist(outnvl, "errors", errors);

out:
	nvlist_free(errors);
	nvlist_free(recvprops);
	nvlist_free(localprops);
	nvlist_free(hidden_args);

	return (error);
}

/*
 * When stack space is limited, we write replication stream data to the target
 * on a separate taskq thread, to make sure there's enough stack space.
 */
#ifndef HAVE_LARGE_STACKS
#define	USE_SEND_TASKQ	1
#endif

typedef struct dump_bytes_io {
	zfs_file_t	*dbi_fp;
	caddr_t		dbi_buf;
	int		dbi_len;
	int		dbi_err;
} dump_bytes_io_t;

static void
dump_bytes_cb(void *arg)
{
	dump_bytes_io_t *dbi = (dump_bytes_io_t *)arg;
	zfs_file_t *fp;
	caddr_t buf;

	fp = dbi->dbi_fp;
	buf = dbi->dbi_buf;

	dbi->dbi_err = zfs_file_write(fp, buf, dbi->dbi_len, NULL);
}

typedef struct dump_bytes_arg {
	zfs_file_t	*dba_fp;
#ifdef USE_SEND_TASKQ
	taskq_t		*dba_tq;
	taskq_ent_t	dba_tqent;
#endif
} dump_bytes_arg_t;

static int
dump_bytes(objset_t *os, void *buf, int len, void *arg)
{
	dump_bytes_arg_t *dba = (dump_bytes_arg_t *)arg;
	dump_bytes_io_t dbi;

	dbi.dbi_fp = dba->dba_fp;
	dbi.dbi_buf = buf;
	dbi.dbi_len = len;

#ifdef USE_SEND_TASKQ
	taskq_dispatch_ent(dba->dba_tq, dump_bytes_cb, &dbi, TQ_SLEEP,
	    &dba->dba_tqent);
	taskq_wait(dba->dba_tq);
#else
	dump_bytes_cb(&dbi);
#endif

	return (dbi.dbi_err);
}

static int
dump_bytes_init(dump_bytes_arg_t *dba, int fd, dmu_send_outparams_t *out)
{
	zfs_file_t *fp = zfs_file_get(fd);
	if (fp == NULL)
		return (SET_ERROR(EBADF));

	dba->dba_fp = fp;
#ifdef USE_SEND_TASKQ
	dba->dba_tq = taskq_create("z_send", 1, defclsyspri, 0, 0, 0);
	taskq_init_ent(&dba->dba_tqent);
#endif

	memset(out, 0, sizeof (dmu_send_outparams_t));
	out->dso_outfunc = dump_bytes;
	out->dso_arg = dba;
	out->dso_dryrun = B_FALSE;

	return (0);
}

static void
dump_bytes_fini(dump_bytes_arg_t *dba)
{
	zfs_file_put(dba->dba_fp);
#ifdef USE_SEND_TASKQ
	taskq_destroy(dba->dba_tq);
#endif
}

/*
 * inputs:
 * zc_name	name of snapshot to send
 * zc_cookie	file descriptor to send stream to
 * zc_obj	fromorigin flag (mutually exclusive with zc_fromobj)
 * zc_sendobj	objsetid of snapshot to send
 * zc_fromobj	objsetid of incremental fromsnap (may be zero)
 * zc_guid	if set, estimate size of stream only.  zc_cookie is ignored.
 *		output size in zc_objset_type.
 * zc_flags	lzc_send_flags
 *
 * outputs:
 * zc_objset_type	estimated size, if zc_guid is set
 *
 * NOTE: This is no longer the preferred interface, any new functionality
 *	  should be added to zfs_ioc_send_new() instead.
 */
static int
zfs_ioc_send(zfs_cmd_t *zc)
{
	int error;
	offset_t off;
	boolean_t estimate = (zc->zc_guid != 0);
	boolean_t embedok = (zc->zc_flags & 0x1);
	boolean_t large_block_ok = (zc->zc_flags & 0x2);
	boolean_t compressok = (zc->zc_flags & 0x4);
	boolean_t rawok = (zc->zc_flags & 0x8);
	boolean_t savedok = (zc->zc_flags & 0x10);

	if (zc->zc_obj != 0) {
		dsl_pool_t *dp;
		dsl_dataset_t *tosnap;

		error = dsl_pool_hold(zc->zc_name, FTAG, &dp);
		if (error != 0)
			return (error);

		error = dsl_dataset_hold_obj(dp, zc->zc_sendobj, FTAG, &tosnap);
		if (error != 0) {
			dsl_pool_rele(dp, FTAG);
			return (error);
		}

		if (dsl_dir_is_clone(tosnap->ds_dir))
			zc->zc_fromobj =
			    dsl_dir_phys(tosnap->ds_dir)->dd_origin_obj;
		dsl_dataset_rele(tosnap, FTAG);
		dsl_pool_rele(dp, FTAG);
	}

	if (estimate) {
		dsl_pool_t *dp;
		dsl_dataset_t *tosnap;
		dsl_dataset_t *fromsnap = NULL;

		error = dsl_pool_hold(zc->zc_name, FTAG, &dp);
		if (error != 0)
			return (error);

		error = dsl_dataset_hold_obj(dp, zc->zc_sendobj,
		    FTAG, &tosnap);
		if (error != 0) {
			dsl_pool_rele(dp, FTAG);
			return (error);
		}

		if (zc->zc_fromobj != 0) {
			error = dsl_dataset_hold_obj(dp, zc->zc_fromobj,
			    FTAG, &fromsnap);
			if (error != 0) {
				dsl_dataset_rele(tosnap, FTAG);
				dsl_pool_rele(dp, FTAG);
				return (error);
			}
		}

		error = dmu_send_estimate_fast(tosnap, fromsnap, NULL,
		    compressok || rawok, savedok, &zc->zc_objset_type);

		if (fromsnap != NULL)
			dsl_dataset_rele(fromsnap, FTAG);
		dsl_dataset_rele(tosnap, FTAG);
		dsl_pool_rele(dp, FTAG);
	} else {
		dump_bytes_arg_t dba;
		dmu_send_outparams_t out;
		error = dump_bytes_init(&dba, zc->zc_cookie, &out);
		if (error)
			return (error);

		off = zfs_file_off(dba.dba_fp);
		error = dmu_send_obj(zc->zc_name, zc->zc_sendobj,
		    zc->zc_fromobj, embedok, large_block_ok, compressok,
		    rawok, savedok, zc->zc_cookie, &off, &out);

		dump_bytes_fini(&dba);
	}
	return (error);
}

/*
 * inputs:
 * zc_name		name of snapshot on which to report progress
 * zc_cookie		file descriptor of send stream
 *
 * outputs:
 * zc_cookie		number of bytes written in send stream thus far
 * zc_objset_type	logical size of data traversed by send thus far
 */
static int
zfs_ioc_send_progress(zfs_cmd_t *zc)
{
	dsl_pool_t *dp;
	dsl_dataset_t *ds;
	dmu_sendstatus_t *dsp = NULL;
	int error;

	error = dsl_pool_hold(zc->zc_name, FTAG, &dp);
	if (error != 0)
		return (error);

	error = dsl_dataset_hold(dp, zc->zc_name, FTAG, &ds);
	if (error != 0) {
		dsl_pool_rele(dp, FTAG);
		return (error);
	}

	mutex_enter(&ds->ds_sendstream_lock);

	/*
	 * Iterate over all the send streams currently active on this dataset.
	 * If there's one which matches the specified file descriptor _and_ the
	 * stream was started by the current process, return the progress of
	 * that stream.
	 */

	for (dsp = list_head(&ds->ds_sendstreams); dsp != NULL;
	    dsp = list_next(&ds->ds_sendstreams, dsp)) {
		if (dsp->dss_outfd == zc->zc_cookie &&
		    zfs_proc_is_caller(dsp->dss_proc))
			break;
	}

	if (dsp != NULL) {
		zc->zc_cookie = atomic_cas_64((volatile uint64_t *)dsp->dss_off,
		    0, 0);
		/* This is the closest thing we have to atomic_read_64. */
		zc->zc_objset_type = atomic_cas_64(&dsp->dss_blocks, 0, 0);
	} else {
		error = SET_ERROR(ENOENT);
	}

	mutex_exit(&ds->ds_sendstream_lock);
	dsl_dataset_rele(ds, FTAG);
	dsl_pool_rele(dp, FTAG);
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

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0)
		return (error);

	error = spa_get_errlog(spa, (void *)(uintptr_t)zc->zc_nvlist_dst,
	    &zc->zc_nvlist_dst_size);

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
		return (SET_ERROR(EIO));
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

		if (zc->zc_nvlist_src == 0)
			return (SET_ERROR(EINVAL));

		if ((error = get_nvlist(zc->zc_nvlist_src,
		    zc->zc_nvlist_src_size, zc->zc_iflags, &policy)) == 0) {
			error = spa_open_rewind(zc->zc_name, &spa, FTAG,
			    policy, &config);
			if (config != NULL) {
				int err;

				if ((err = put_nvlist(zc, config)) != 0)
					error = err;
				nvlist_free(config);
			}
			nvlist_free(policy);
		}
	}

	if (error != 0)
		return (error);

	/*
	 * If multihost is enabled, resuming I/O is unsafe as another
	 * host may have imported the pool. Check for remote activity.
	 */
	if (spa_multihost(spa) && spa_suspended(spa) &&
	    spa_mmp_remote_host_activity(spa)) {
		spa_close(spa, FTAG);
		return (SET_ERROR(EREMOTEIO));
	}

	spa_vdev_state_enter(spa, SCL_NONE);

	if (zc->zc_guid == 0) {
		vd = NULL;
	} else {
		vd = spa_lookup_by_guid(spa, zc->zc_guid, B_TRUE);
		if (vd == NULL) {
			error = SET_ERROR(ENODEV);
			(void) spa_vdev_state_exit(spa, NULL, error);
			spa_close(spa, FTAG);
			return (error);
		}
	}

	vdev_clear(spa, vd);

	(void) spa_vdev_state_exit(spa, spa_suspended(spa) ?
	    NULL : spa->spa_root_vdev, 0);

	/*
	 * Resume any suspended I/Os.
	 */
	if (zio_resume(spa) != 0)
		error = SET_ERROR(EIO);

	spa_close(spa, FTAG);

	return (error);
}

/*
 * Reopen all the vdevs associated with the pool.
 *
 * innvl: {
 *  "scrub_restart" -> when true and scrub is running, allow to restart
 *              scrub as the side effect of the reopen (boolean).
 * }
 *
 * outnvl is unused
 */
static const zfs_ioc_key_t zfs_keys_pool_reopen[] = {
	{"scrub_restart",	DATA_TYPE_BOOLEAN_VALUE,	ZK_OPTIONAL},
};

static int
zfs_ioc_pool_reopen(const char *pool, nvlist_t *innvl, nvlist_t *outnvl)
{
	(void) outnvl;
	spa_t *spa;
	int error;
	boolean_t rc, scrub_restart = B_TRUE;

	if (innvl) {
		error = nvlist_lookup_boolean_value(innvl,
		    "scrub_restart", &rc);
		if (error == 0)
			scrub_restart = rc;
	}

	error = spa_open(pool, &spa, FTAG);
	if (error != 0)
		return (error);

	spa_vdev_state_enter(spa, SCL_NONE);

	/*
	 * If the scrub_restart flag is B_FALSE and a scrub is already
	 * in progress then set spa_scrub_reopen flag to B_TRUE so that
	 * we don't restart the scrub as a side effect of the reopen.
	 * Otherwise, let vdev_open() decided if a resilver is required.
	 */

	spa->spa_scrub_reopen = (!scrub_restart &&
	    dsl_scan_scrubbing(spa->spa_dsl_pool));
	vdev_reopen(spa->spa_root_vdev);
	spa->spa_scrub_reopen = B_FALSE;

	(void) spa_vdev_state_exit(spa, NULL, 0);
	spa_close(spa, FTAG);
	return (0);
}

/*
 * inputs:
 * zc_name	name of filesystem
 *
 * outputs:
 * zc_string	name of conflicting snapshot, if there is one
 */
static int
zfs_ioc_promote(zfs_cmd_t *zc)
{
	dsl_pool_t *dp;
	dsl_dataset_t *ds, *ods;
	char origin[ZFS_MAX_DATASET_NAME_LEN];
	char *cp;
	int error;

	zc->zc_name[sizeof (zc->zc_name) - 1] = '\0';
	if (dataset_namecheck(zc->zc_name, NULL, NULL) != 0 ||
	    strchr(zc->zc_name, '%'))
		return (SET_ERROR(EINVAL));

	error = dsl_pool_hold(zc->zc_name, FTAG, &dp);
	if (error != 0)
		return (error);

	error = dsl_dataset_hold(dp, zc->zc_name, FTAG, &ds);
	if (error != 0) {
		dsl_pool_rele(dp, FTAG);
		return (error);
	}

	if (!dsl_dir_is_clone(ds->ds_dir)) {
		dsl_dataset_rele(ds, FTAG);
		dsl_pool_rele(dp, FTAG);
		return (SET_ERROR(EINVAL));
	}

	error = dsl_dataset_hold_obj(dp,
	    dsl_dir_phys(ds->ds_dir)->dd_origin_obj, FTAG, &ods);
	if (error != 0) {
		dsl_dataset_rele(ds, FTAG);
		dsl_pool_rele(dp, FTAG);
		return (error);
	}

	dsl_dataset_name(ods, origin);
	dsl_dataset_rele(ods, FTAG);
	dsl_dataset_rele(ds, FTAG);
	dsl_pool_rele(dp, FTAG);

	/*
	 * We don't need to unmount *all* the origin fs's snapshots, but
	 * it's easier.
	 */
	cp = strchr(origin, '@');
	if (cp)
		*cp = '\0';
	(void) dmu_objset_find(origin,
	    zfs_unmount_snap_cb, NULL, DS_FIND_SNAPSHOTS);
	return (dsl_dataset_promote(zc->zc_name, zc->zc_string));
}

/*
 * Retrieve a single {user|group|project}{used|quota}@... property.
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
		return (SET_ERROR(EINVAL));

	error = zfsvfs_hold(zc->zc_name, FTAG, &zfsvfs, B_FALSE);
	if (error != 0)
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
		return (SET_ERROR(ENOMEM));

	int error = zfsvfs_hold(zc->zc_name, FTAG, &zfsvfs, B_FALSE);
	if (error != 0)
		return (error);

	void *buf = vmem_alloc(bufsize, KM_SLEEP);

	error = zfs_userspace_many(zfsvfs, zc->zc_objset_type, &zc->zc_cookie,
	    buf, &zc->zc_nvlist_dst_size);

	if (error == 0) {
		error = xcopyout(buf,
		    (void *)(uintptr_t)zc->zc_nvlist_dst,
		    zc->zc_nvlist_dst_size);
	}
	vmem_free(buf, bufsize);
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
	int error = 0;
	zfsvfs_t *zfsvfs;

	if (getzfsvfs(zc->zc_name, &zfsvfs) == 0) {
		if (!dmu_objset_userused_enabled(zfsvfs->z_os)) {
			/*
			 * If userused is not enabled, it may be because the
			 * objset needs to be closed & reopened (to grow the
			 * objset_phys_t).  Suspend/resume the fs will do that.
			 */
			dsl_dataset_t *ds, *newds;

			ds = dmu_objset_ds(zfsvfs->z_os);
			error = zfs_suspend_fs(zfsvfs);
			if (error == 0) {
				dmu_objset_refresh_ownership(ds, &newds,
				    B_TRUE, zfsvfs);
				error = zfs_resume_fs(zfsvfs, newds);
			}
		}
		if (error == 0) {
			mutex_enter(&zfsvfs->z_os->os_upgrade_lock);
			if (zfsvfs->z_os->os_upgrade_id == 0) {
				/* clear potential error code and retry */
				zfsvfs->z_os->os_upgrade_status = 0;
				mutex_exit(&zfsvfs->z_os->os_upgrade_lock);

				dsl_pool_config_enter(
				    dmu_objset_pool(zfsvfs->z_os), FTAG);
				dmu_objset_userspace_upgrade(zfsvfs->z_os);
				dsl_pool_config_exit(
				    dmu_objset_pool(zfsvfs->z_os), FTAG);
			} else {
				mutex_exit(&zfsvfs->z_os->os_upgrade_lock);
			}

			taskq_wait_id(zfsvfs->z_os->os_spa->spa_upgrade_taskq,
			    zfsvfs->z_os->os_upgrade_id);
			error = zfsvfs->z_os->os_upgrade_status;
		}
		zfs_vfs_rele(zfsvfs);
	} else {
		objset_t *os;

		/* XXX kind of reading contents without owning */
		error = dmu_objset_hold_flags(zc->zc_name, B_TRUE, FTAG, &os);
		if (error != 0)
			return (error);

		mutex_enter(&os->os_upgrade_lock);
		if (os->os_upgrade_id == 0) {
			/* clear potential error code and retry */
			os->os_upgrade_status = 0;
			mutex_exit(&os->os_upgrade_lock);

			dmu_objset_userspace_upgrade(os);
		} else {
			mutex_exit(&os->os_upgrade_lock);
		}

		dsl_pool_rele(dmu_objset_pool(os), FTAG);

		taskq_wait_id(os->os_spa->spa_upgrade_taskq, os->os_upgrade_id);
		error = os->os_upgrade_status;

		dsl_dataset_rele_flags(dmu_objset_ds(os), DS_HOLD_FLAG_DECRYPT,
		    FTAG);
	}
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
zfs_ioc_id_quota_upgrade(zfs_cmd_t *zc)
{
	objset_t *os;
	int error;

	error = dmu_objset_hold_flags(zc->zc_name, B_TRUE, FTAG, &os);
	if (error != 0)
		return (error);

	if (dmu_objset_userobjspace_upgradable(os) ||
	    dmu_objset_projectquota_upgradable(os)) {
		mutex_enter(&os->os_upgrade_lock);
		if (os->os_upgrade_id == 0) {
			/* clear potential error code and retry */
			os->os_upgrade_status = 0;
			mutex_exit(&os->os_upgrade_lock);

			dmu_objset_id_quota_upgrade(os);
		} else {
			mutex_exit(&os->os_upgrade_lock);
		}

		dsl_pool_rele(dmu_objset_pool(os), FTAG);

		taskq_wait_id(os->os_spa->spa_upgrade_taskq, os->os_upgrade_id);
		error = os->os_upgrade_status;
	} else {
		dsl_pool_rele(dmu_objset_pool(os), FTAG);
	}

	dsl_dataset_rele_flags(dmu_objset_ds(os), DS_HOLD_FLAG_DECRYPT, FTAG);

	return (error);
}

static int
zfs_ioc_share(zfs_cmd_t *zc)
{
	return (SET_ERROR(ENOSYS));
}

/*
 * inputs:
 * zc_name		name of containing filesystem
 * zc_obj		object # beyond which we want next in-use object #
 *
 * outputs:
 * zc_obj		next in-use object #
 */
static int
zfs_ioc_next_obj(zfs_cmd_t *zc)
{
	objset_t *os = NULL;
	int error;

	error = dmu_objset_hold(zc->zc_name, FTAG, &os);
	if (error != 0)
		return (error);

	error = dmu_object_next(os, &zc->zc_obj, B_FALSE, 0);

	dmu_objset_rele(os, FTAG);
	return (error);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_value		prefix name for snapshot
 * zc_cleanup_fd	cleanup-on-exit file descriptor for calling process
 *
 * outputs:
 * zc_value		short name of new snapshot
 */
static int
zfs_ioc_tmp_snapshot(zfs_cmd_t *zc)
{
	char *snap_name;
	char *hold_name;
	minor_t minor;

	zfs_file_t *fp = zfs_onexit_fd_hold(zc->zc_cleanup_fd, &minor);
	if (fp == NULL)
		return (SET_ERROR(EBADF));

	snap_name = kmem_asprintf("%s-%016llx", zc->zc_value,
	    (u_longlong_t)ddi_get_lbolt64());
	hold_name = kmem_asprintf("%%%s", zc->zc_value);

	int error = dsl_dataset_snapshot_tmp(zc->zc_name, snap_name, minor,
	    hold_name);
	if (error == 0)
		(void) strlcpy(zc->zc_value, snap_name,
		    sizeof (zc->zc_value));
	kmem_strfree(snap_name);
	kmem_strfree(hold_name);
	zfs_onexit_fd_rele(fp);
	return (error);
}

/*
 * inputs:
 * zc_name		name of "to" snapshot
 * zc_value		name of "from" snapshot
 * zc_cookie		file descriptor to write diff data on
 *
 * outputs:
 * dmu_diff_record_t's to the file descriptor
 */
static int
zfs_ioc_diff(zfs_cmd_t *zc)
{
	zfs_file_t *fp;
	offset_t off;
	int error;

	if ((fp = zfs_file_get(zc->zc_cookie)) == NULL)
		return (SET_ERROR(EBADF));

	off = zfs_file_off(fp);
	error = dmu_diff(zc->zc_name, zc->zc_value, fp, &off);

	zfs_file_put(fp);

	return (error);
}

static int
zfs_ioc_smb_acl(zfs_cmd_t *zc)
{
	return (SET_ERROR(ENOTSUP));
}

/*
 * innvl: {
 *     "holds" -> { snapname -> holdname (string), ... }
 *     (optional) "cleanup_fd" -> fd (int32)
 * }
 *
 * outnvl: {
 *     snapname -> error value (int32)
 *     ...
 * }
 */
static const zfs_ioc_key_t zfs_keys_hold[] = {
	{"holds",		DATA_TYPE_NVLIST,	0},
	{"cleanup_fd",		DATA_TYPE_INT32,	ZK_OPTIONAL},
};

static int
zfs_ioc_hold(const char *pool, nvlist_t *args, nvlist_t *errlist)
{
	(void) pool;
	nvpair_t *pair;
	nvlist_t *holds;
	int cleanup_fd = -1;
	int error;
	minor_t minor = 0;
	zfs_file_t *fp = NULL;

	holds = fnvlist_lookup_nvlist(args, "holds");

	/* make sure the user didn't pass us any invalid (empty) tags */
	for (pair = nvlist_next_nvpair(holds, NULL); pair != NULL;
	    pair = nvlist_next_nvpair(holds, pair)) {
		const char *htag;

		error = nvpair_value_string(pair, &htag);
		if (error != 0)
			return (SET_ERROR(error));

		if (strlen(htag) == 0)
			return (SET_ERROR(EINVAL));
	}

	if (nvlist_lookup_int32(args, "cleanup_fd", &cleanup_fd) == 0) {
		fp = zfs_onexit_fd_hold(cleanup_fd, &minor);
		if (fp == NULL)
			return (SET_ERROR(EBADF));
	}

	error = dsl_dataset_user_hold(holds, minor, errlist);
	if (fp != NULL) {
		ASSERT3U(minor, !=, 0);
		zfs_onexit_fd_rele(fp);
	}
	return (SET_ERROR(error));
}

/*
 * innvl is not used.
 *
 * outnvl: {
 *    holdname -> time added (uint64 seconds since epoch)
 *    ...
 * }
 */
static const zfs_ioc_key_t zfs_keys_get_holds[] = {
	/* no nvl keys */
};

static int
zfs_ioc_get_holds(const char *snapname, nvlist_t *args, nvlist_t *outnvl)
{
	(void) args;
	return (dsl_dataset_get_holds(snapname, outnvl));
}

/*
 * innvl: {
 *     snapname -> { holdname, ... }
 *     ...
 * }
 *
 * outnvl: {
 *     snapname -> error value (int32)
 *     ...
 * }
 */
static const zfs_ioc_key_t zfs_keys_release[] = {
	{"<snapname>...",	DATA_TYPE_NVLIST,	ZK_WILDCARDLIST},
};

static int
zfs_ioc_release(const char *pool, nvlist_t *holds, nvlist_t *errlist)
{
	(void) pool;
	return (dsl_dataset_user_release(holds, errlist));
}

/*
 * inputs:
 * zc_guid		flags (ZEVENT_NONBLOCK)
 * zc_cleanup_fd	zevent file descriptor
 *
 * outputs:
 * zc_nvlist_dst	next nvlist event
 * zc_cookie		dropped events since last get
 */
static int
zfs_ioc_events_next(zfs_cmd_t *zc)
{
	zfs_zevent_t *ze;
	nvlist_t *event = NULL;
	minor_t minor;
	uint64_t dropped = 0;
	int error;

	zfs_file_t *fp = zfs_zevent_fd_hold(zc->zc_cleanup_fd, &minor, &ze);
	if (fp == NULL)
		return (SET_ERROR(EBADF));

	do {
		error = zfs_zevent_next(ze, &event,
		    &zc->zc_nvlist_dst_size, &dropped);
		if (event != NULL) {
			zc->zc_cookie = dropped;
			error = put_nvlist(zc, event);
			nvlist_free(event);
		}

		if (zc->zc_guid & ZEVENT_NONBLOCK)
			break;

		if ((error == 0) || (error != ENOENT))
			break;

		error = zfs_zevent_wait(ze);
		if (error != 0)
			break;
	} while (1);

	zfs_zevent_fd_rele(fp);

	return (error);
}

/*
 * outputs:
 * zc_cookie		cleared events count
 */
static int
zfs_ioc_events_clear(zfs_cmd_t *zc)
{
	uint_t count;

	zfs_zevent_drain_all(&count);
	zc->zc_cookie = count;

	return (0);
}

/*
 * inputs:
 * zc_guid		eid | ZEVENT_SEEK_START | ZEVENT_SEEK_END
 * zc_cleanup		zevent file descriptor
 */
static int
zfs_ioc_events_seek(zfs_cmd_t *zc)
{
	zfs_zevent_t *ze;
	minor_t minor;
	int error;

	zfs_file_t *fp = zfs_zevent_fd_hold(zc->zc_cleanup_fd, &minor, &ze);
	if (fp == NULL)
		return (SET_ERROR(EBADF));

	error = zfs_zevent_seek(ze, zc->zc_guid);
	zfs_zevent_fd_rele(fp);

	return (error);
}

/*
 * inputs:
 * zc_name		name of later filesystem or snapshot
 * zc_value		full name of old snapshot or bookmark
 *
 * outputs:
 * zc_cookie		space in bytes
 * zc_objset_type	compressed space in bytes
 * zc_perm_action	uncompressed space in bytes
 */
static int
zfs_ioc_space_written(zfs_cmd_t *zc)
{
	int error;
	dsl_pool_t *dp;
	dsl_dataset_t *new;

	error = dsl_pool_hold(zc->zc_name, FTAG, &dp);
	if (error != 0)
		return (error);
	error = dsl_dataset_hold(dp, zc->zc_name, FTAG, &new);
	if (error != 0) {
		dsl_pool_rele(dp, FTAG);
		return (error);
	}
	if (strchr(zc->zc_value, '#') != NULL) {
		zfs_bookmark_phys_t bmp;
		error = dsl_bookmark_lookup(dp, zc->zc_value,
		    new, &bmp);
		if (error == 0) {
			error = dsl_dataset_space_written_bookmark(&bmp, new,
			    &zc->zc_cookie,
			    &zc->zc_objset_type, &zc->zc_perm_action);
		}
	} else {
		dsl_dataset_t *old;
		error = dsl_dataset_hold(dp, zc->zc_value, FTAG, &old);

		if (error == 0) {
			error = dsl_dataset_space_written(old, new,
			    &zc->zc_cookie,
			    &zc->zc_objset_type, &zc->zc_perm_action);
			dsl_dataset_rele(old, FTAG);
		}
	}
	dsl_dataset_rele(new, FTAG);
	dsl_pool_rele(dp, FTAG);
	return (error);
}

/*
 * innvl: {
 *     "firstsnap" -> snapshot name
 * }
 *
 * outnvl: {
 *     "used" -> space in bytes
 *     "compressed" -> compressed space in bytes
 *     "uncompressed" -> uncompressed space in bytes
 * }
 */
static const zfs_ioc_key_t zfs_keys_space_snaps[] = {
	{"firstsnap",	DATA_TYPE_STRING,	0},
};

static int
zfs_ioc_space_snaps(const char *lastsnap, nvlist_t *innvl, nvlist_t *outnvl)
{
	int error;
	dsl_pool_t *dp;
	dsl_dataset_t *new, *old;
	const char *firstsnap;
	uint64_t used, comp, uncomp;

	firstsnap = fnvlist_lookup_string(innvl, "firstsnap");

	error = dsl_pool_hold(lastsnap, FTAG, &dp);
	if (error != 0)
		return (error);

	error = dsl_dataset_hold(dp, lastsnap, FTAG, &new);
	if (error == 0 && !new->ds_is_snapshot) {
		dsl_dataset_rele(new, FTAG);
		error = SET_ERROR(EINVAL);
	}
	if (error != 0) {
		dsl_pool_rele(dp, FTAG);
		return (error);
	}
	error = dsl_dataset_hold(dp, firstsnap, FTAG, &old);
	if (error == 0 && !old->ds_is_snapshot) {
		dsl_dataset_rele(old, FTAG);
		error = SET_ERROR(EINVAL);
	}
	if (error != 0) {
		dsl_dataset_rele(new, FTAG);
		dsl_pool_rele(dp, FTAG);
		return (error);
	}

	error = dsl_dataset_space_wouldfree(old, new, &used, &comp, &uncomp);
	dsl_dataset_rele(old, FTAG);
	dsl_dataset_rele(new, FTAG);
	dsl_pool_rele(dp, FTAG);
	fnvlist_add_uint64(outnvl, "used", used);
	fnvlist_add_uint64(outnvl, "compressed", comp);
	fnvlist_add_uint64(outnvl, "uncompressed", uncomp);
	return (error);
}

/*
 * innvl: {
 *     "fd" -> file descriptor to write stream to (int32)
 *     (optional) "fromsnap" -> full snap name to send an incremental from
 *     (optional) "largeblockok" -> (value ignored)
 *         indicates that blocks > 128KB are permitted
 *     (optional) "embedok" -> (value ignored)
 *         presence indicates DRR_WRITE_EMBEDDED records are permitted
 *     (optional) "compressok" -> (value ignored)
 *         presence indicates compressed DRR_WRITE records are permitted
 *     (optional) "rawok" -> (value ignored)
 *         presence indicates raw encrypted records should be used.
 *     (optional) "savedok" -> (value ignored)
 *         presence indicates we should send a partially received snapshot
 *     (optional) "resume_object" and "resume_offset" -> (uint64)
 *         if present, resume send stream from specified object and offset.
 *     (optional) "redactbook" -> (string)
 *         if present, use this bookmark's redaction list to generate a redacted
 *         send stream
 * }
 *
 * outnvl is unused
 */
static const zfs_ioc_key_t zfs_keys_send_new[] = {
	{"fd",			DATA_TYPE_INT32,	0},
	{"fromsnap",		DATA_TYPE_STRING,	ZK_OPTIONAL},
	{"largeblockok",	DATA_TYPE_BOOLEAN,	ZK_OPTIONAL},
	{"embedok",		DATA_TYPE_BOOLEAN,	ZK_OPTIONAL},
	{"compressok",		DATA_TYPE_BOOLEAN,	ZK_OPTIONAL},
	{"rawok",		DATA_TYPE_BOOLEAN,	ZK_OPTIONAL},
	{"savedok",		DATA_TYPE_BOOLEAN,	ZK_OPTIONAL},
	{"resume_object",	DATA_TYPE_UINT64,	ZK_OPTIONAL},
	{"resume_offset",	DATA_TYPE_UINT64,	ZK_OPTIONAL},
	{"redactbook",		DATA_TYPE_STRING,	ZK_OPTIONAL},
};

static int
zfs_ioc_send_new(const char *snapname, nvlist_t *innvl, nvlist_t *outnvl)
{
	(void) outnvl;
	int error;
	offset_t off;
	const char *fromname = NULL;
	int fd;
	boolean_t largeblockok;
	boolean_t embedok;
	boolean_t compressok;
	boolean_t rawok;
	boolean_t savedok;
	uint64_t resumeobj = 0;
	uint64_t resumeoff = 0;
	const char *redactbook = NULL;

	fd = fnvlist_lookup_int32(innvl, "fd");

	(void) nvlist_lookup_string(innvl, "fromsnap", &fromname);

	largeblockok = nvlist_exists(innvl, "largeblockok");
	embedok = nvlist_exists(innvl, "embedok");
	compressok = nvlist_exists(innvl, "compressok");
	rawok = nvlist_exists(innvl, "rawok");
	savedok = nvlist_exists(innvl, "savedok");

	(void) nvlist_lookup_uint64(innvl, "resume_object", &resumeobj);
	(void) nvlist_lookup_uint64(innvl, "resume_offset", &resumeoff);

	(void) nvlist_lookup_string(innvl, "redactbook", &redactbook);

	dump_bytes_arg_t dba;
	dmu_send_outparams_t out;
	error = dump_bytes_init(&dba, fd, &out);
	if (error)
		return (error);

	off = zfs_file_off(dba.dba_fp);
	error = dmu_send(snapname, fromname, embedok, largeblockok,
	    compressok, rawok, savedok, resumeobj, resumeoff,
	    redactbook, fd, &off, &out);

	dump_bytes_fini(&dba);

	return (error);
}

static int
send_space_sum(objset_t *os, void *buf, int len, void *arg)
{
	(void) os, (void) buf;
	uint64_t *size = arg;

	*size += len;
	return (0);
}

/*
 * Determine approximately how large a zfs send stream will be -- the number
 * of bytes that will be written to the fd supplied to zfs_ioc_send_new().
 *
 * innvl: {
 *     (optional) "from" -> full snap or bookmark name to send an incremental
 *                          from
 *     (optional) "largeblockok" -> (value ignored)
 *         indicates that blocks > 128KB are permitted
 *     (optional) "embedok" -> (value ignored)
 *         presence indicates DRR_WRITE_EMBEDDED records are permitted
 *     (optional) "compressok" -> (value ignored)
 *         presence indicates compressed DRR_WRITE records are permitted
 *     (optional) "rawok" -> (value ignored)
 *         presence indicates raw encrypted records should be used.
 *     (optional) "resume_object" and "resume_offset" -> (uint64)
 *         if present, resume send stream from specified object and offset.
 *     (optional) "fd" -> file descriptor to use as a cookie for progress
 *         tracking (int32)
 * }
 *
 * outnvl: {
 *     "space" -> bytes of space (uint64)
 * }
 */
static const zfs_ioc_key_t zfs_keys_send_space[] = {
	{"from",		DATA_TYPE_STRING,	ZK_OPTIONAL},
	{"fromsnap",		DATA_TYPE_STRING,	ZK_OPTIONAL},
	{"largeblockok",	DATA_TYPE_BOOLEAN,	ZK_OPTIONAL},
	{"embedok",		DATA_TYPE_BOOLEAN,	ZK_OPTIONAL},
	{"compressok",		DATA_TYPE_BOOLEAN,	ZK_OPTIONAL},
	{"rawok",		DATA_TYPE_BOOLEAN,	ZK_OPTIONAL},
	{"fd",			DATA_TYPE_INT32,	ZK_OPTIONAL},
	{"redactbook",		DATA_TYPE_STRING,	ZK_OPTIONAL},
	{"resume_object",	DATA_TYPE_UINT64,	ZK_OPTIONAL},
	{"resume_offset",	DATA_TYPE_UINT64,	ZK_OPTIONAL},
	{"bytes",		DATA_TYPE_UINT64,	ZK_OPTIONAL},
};

static int
zfs_ioc_send_space(const char *snapname, nvlist_t *innvl, nvlist_t *outnvl)
{
	dsl_pool_t *dp;
	dsl_dataset_t *tosnap;
	dsl_dataset_t *fromsnap = NULL;
	int error;
	const char *fromname = NULL;
	const char *redactlist_book = NULL;
	boolean_t largeblockok;
	boolean_t embedok;
	boolean_t compressok;
	boolean_t rawok;
	boolean_t savedok;
	uint64_t space = 0;
	boolean_t full_estimate = B_FALSE;
	uint64_t resumeobj = 0;
	uint64_t resumeoff = 0;
	uint64_t resume_bytes = 0;
	int32_t fd = -1;
	zfs_bookmark_phys_t zbm = {0};

	error = dsl_pool_hold(snapname, FTAG, &dp);
	if (error != 0)
		return (error);

	error = dsl_dataset_hold(dp, snapname, FTAG, &tosnap);
	if (error != 0) {
		dsl_pool_rele(dp, FTAG);
		return (error);
	}
	(void) nvlist_lookup_int32(innvl, "fd", &fd);

	largeblockok = nvlist_exists(innvl, "largeblockok");
	embedok = nvlist_exists(innvl, "embedok");
	compressok = nvlist_exists(innvl, "compressok");
	rawok = nvlist_exists(innvl, "rawok");
	savedok = nvlist_exists(innvl, "savedok");
	boolean_t from = (nvlist_lookup_string(innvl, "from", &fromname) == 0);
	boolean_t altbook = (nvlist_lookup_string(innvl, "redactbook",
	    &redactlist_book) == 0);

	(void) nvlist_lookup_uint64(innvl, "resume_object", &resumeobj);
	(void) nvlist_lookup_uint64(innvl, "resume_offset", &resumeoff);
	(void) nvlist_lookup_uint64(innvl, "bytes", &resume_bytes);

	if (altbook) {
		full_estimate = B_TRUE;
	} else if (from) {
		if (strchr(fromname, '#')) {
			error = dsl_bookmark_lookup(dp, fromname, tosnap, &zbm);

			/*
			 * dsl_bookmark_lookup() will fail with EXDEV if
			 * the from-bookmark and tosnap are at the same txg.
			 * However, it's valid to do a send (and therefore,
			 * a send estimate) from and to the same time point,
			 * if the bookmark is redacted (the incremental send
			 * can change what's redacted on the target).  In
			 * this case, dsl_bookmark_lookup() fills in zbm
			 * but returns EXDEV.  Ignore this error.
			 */
			if (error == EXDEV && zbm.zbm_redaction_obj != 0 &&
			    zbm.zbm_guid ==
			    dsl_dataset_phys(tosnap)->ds_guid)
				error = 0;

			if (error != 0) {
				dsl_dataset_rele(tosnap, FTAG);
				dsl_pool_rele(dp, FTAG);
				return (error);
			}
			if (zbm.zbm_redaction_obj != 0 || !(zbm.zbm_flags &
			    ZBM_FLAG_HAS_FBN)) {
				full_estimate = B_TRUE;
			}
		} else if (strchr(fromname, '@')) {
			error = dsl_dataset_hold(dp, fromname, FTAG, &fromsnap);
			if (error != 0) {
				dsl_dataset_rele(tosnap, FTAG);
				dsl_pool_rele(dp, FTAG);
				return (error);
			}

			if (!dsl_dataset_is_before(tosnap, fromsnap, 0)) {
				full_estimate = B_TRUE;
				dsl_dataset_rele(fromsnap, FTAG);
			}
		} else {
			/*
			 * from is not properly formatted as a snapshot or
			 * bookmark
			 */
			dsl_dataset_rele(tosnap, FTAG);
			dsl_pool_rele(dp, FTAG);
			return (SET_ERROR(EINVAL));
		}
	}

	if (full_estimate) {
		dmu_send_outparams_t out = {0};
		offset_t off = 0;
		out.dso_outfunc = send_space_sum;
		out.dso_arg = &space;
		out.dso_dryrun = B_TRUE;
		/*
		 * We have to release these holds so dmu_send can take them.  It
		 * will do all the error checking we need.
		 */
		dsl_dataset_rele(tosnap, FTAG);
		dsl_pool_rele(dp, FTAG);
		error = dmu_send(snapname, fromname, embedok, largeblockok,
		    compressok, rawok, savedok, resumeobj, resumeoff,
		    redactlist_book, fd, &off, &out);
	} else {
		error = dmu_send_estimate_fast(tosnap, fromsnap,
		    (from && strchr(fromname, '#') != NULL ? &zbm : NULL),
		    compressok || rawok, savedok, &space);
		space -= resume_bytes;
		if (fromsnap != NULL)
			dsl_dataset_rele(fromsnap, FTAG);
		dsl_dataset_rele(tosnap, FTAG);
		dsl_pool_rele(dp, FTAG);
	}

	fnvlist_add_uint64(outnvl, "space", space);

	return (error);
}

/*
 * Sync the currently open TXG to disk for the specified pool.
 * This is somewhat similar to 'zfs_sync()'.
 * For cases that do not result in error this ioctl will wait for
 * the currently open TXG to commit before returning back to the caller.
 *
 * innvl: {
 *  "force" -> when true, force uberblock update even if there is no dirty data.
 *             In addition this will cause the vdev configuration to be written
 *             out including updating the zpool cache file. (boolean_t)
 * }
 *
 * onvl is unused
 */
static const zfs_ioc_key_t zfs_keys_pool_sync[] = {
	{"force",	DATA_TYPE_BOOLEAN_VALUE,	0},
};

static int
zfs_ioc_pool_sync(const char *pool, nvlist_t *innvl, nvlist_t *onvl)
{
	(void) onvl;
	int err;
	boolean_t rc, force = B_FALSE;
	spa_t *spa;

	if ((err = spa_open(pool, &spa, FTAG)) != 0)
		return (err);

	if (innvl) {
		err = nvlist_lookup_boolean_value(innvl, "force", &rc);
		if (err == 0)
			force = rc;
	}

	if (force) {
		spa_config_enter(spa, SCL_CONFIG, FTAG, RW_WRITER);
		vdev_config_dirty(spa->spa_root_vdev);
		spa_config_exit(spa, SCL_CONFIG, FTAG);
	}
	txg_wait_synced(spa_get_dsl(spa), 0);

	spa_close(spa, FTAG);

	return (0);
}

/*
 * Load a user's wrapping key into the kernel.
 * innvl: {
 *     "hidden_args" -> { "wkeydata" -> value }
 *         raw uint8_t array of encryption wrapping key data (32 bytes)
 *     (optional) "noop" -> (value ignored)
 *         presence indicated key should only be verified, not loaded
 * }
 */
static const zfs_ioc_key_t zfs_keys_load_key[] = {
	{"hidden_args",	DATA_TYPE_NVLIST,	0},
	{"noop",	DATA_TYPE_BOOLEAN,	ZK_OPTIONAL},
};

static int
zfs_ioc_load_key(const char *dsname, nvlist_t *innvl, nvlist_t *outnvl)
{
	(void) outnvl;
	int ret;
	dsl_crypto_params_t *dcp = NULL;
	nvlist_t *hidden_args;
	boolean_t noop = nvlist_exists(innvl, "noop");

	if (strchr(dsname, '@') != NULL || strchr(dsname, '%') != NULL) {
		ret = SET_ERROR(EINVAL);
		goto error;
	}

	hidden_args = fnvlist_lookup_nvlist(innvl, ZPOOL_HIDDEN_ARGS);

	ret = dsl_crypto_params_create_nvlist(DCP_CMD_NONE, NULL,
	    hidden_args, &dcp);
	if (ret != 0)
		goto error;

	ret = spa_keystore_load_wkey(dsname, dcp, noop);
	if (ret != 0)
		goto error;

	dsl_crypto_params_free(dcp, noop);

	return (0);

error:
	dsl_crypto_params_free(dcp, B_TRUE);
	return (ret);
}

/*
 * Unload a user's wrapping key from the kernel.
 * Both innvl and outnvl are unused.
 */
static const zfs_ioc_key_t zfs_keys_unload_key[] = {
	/* no nvl keys */
};

static int
zfs_ioc_unload_key(const char *dsname, nvlist_t *innvl, nvlist_t *outnvl)
{
	(void) innvl, (void) outnvl;
	int ret = 0;

	if (strchr(dsname, '@') != NULL || strchr(dsname, '%') != NULL) {
		ret = (SET_ERROR(EINVAL));
		goto out;
	}

	ret = spa_keystore_unload_wkey(dsname);
	if (ret != 0)
		goto out;

out:
	return (ret);
}

/*
 * Changes a user's wrapping key used to decrypt a dataset. The keyformat,
 * keylocation, pbkdf2salt, and pbkdf2iters properties can also be specified
 * here to change how the key is derived in userspace.
 *
 * innvl: {
 *    "hidden_args" (optional) -> { "wkeydata" -> value }
 *         raw uint8_t array of new encryption wrapping key data (32 bytes)
 *    "props" (optional) -> { prop -> value }
 * }
 *
 * outnvl is unused
 */
static const zfs_ioc_key_t zfs_keys_change_key[] = {
	{"crypt_cmd",	DATA_TYPE_UINT64,	ZK_OPTIONAL},
	{"hidden_args",	DATA_TYPE_NVLIST,	ZK_OPTIONAL},
	{"props",	DATA_TYPE_NVLIST,	ZK_OPTIONAL},
};

static int
zfs_ioc_change_key(const char *dsname, nvlist_t *innvl, nvlist_t *outnvl)
{
	(void) outnvl;
	int ret;
	uint64_t cmd = DCP_CMD_NONE;
	dsl_crypto_params_t *dcp = NULL;
	nvlist_t *args = NULL, *hidden_args = NULL;

	if (strchr(dsname, '@') != NULL || strchr(dsname, '%') != NULL) {
		ret = (SET_ERROR(EINVAL));
		goto error;
	}

	(void) nvlist_lookup_uint64(innvl, "crypt_cmd", &cmd);
	(void) nvlist_lookup_nvlist(innvl, "props", &args);
	(void) nvlist_lookup_nvlist(innvl, ZPOOL_HIDDEN_ARGS, &hidden_args);

	ret = dsl_crypto_params_create_nvlist(cmd, args, hidden_args, &dcp);
	if (ret != 0)
		goto error;

	ret = spa_keystore_change_key(dsname, dcp);
	if (ret != 0)
		goto error;

	dsl_crypto_params_free(dcp, B_FALSE);

	return (0);

error:
	dsl_crypto_params_free(dcp, B_TRUE);
	return (ret);
}

static zfs_ioc_vec_t zfs_ioc_vec[ZFS_IOC_LAST - ZFS_IOC_FIRST];

static void
zfs_ioctl_register_legacy(zfs_ioc_t ioc, zfs_ioc_legacy_func_t *func,
    zfs_secpolicy_func_t *secpolicy, zfs_ioc_namecheck_t namecheck,
    boolean_t log_history, zfs_ioc_poolcheck_t pool_check)
{
	zfs_ioc_vec_t *vec = &zfs_ioc_vec[ioc - ZFS_IOC_FIRST];

	ASSERT3U(ioc, >=, ZFS_IOC_FIRST);
	ASSERT3U(ioc, <, ZFS_IOC_LAST);
	ASSERT3P(vec->zvec_legacy_func, ==, NULL);
	ASSERT3P(vec->zvec_func, ==, NULL);

	vec->zvec_legacy_func = func;
	vec->zvec_secpolicy = secpolicy;
	vec->zvec_namecheck = namecheck;
	vec->zvec_allow_log = log_history;
	vec->zvec_pool_check = pool_check;
}

/*
 * See the block comment at the beginning of this file for details on
 * each argument to this function.
 */
void
zfs_ioctl_register(const char *name, zfs_ioc_t ioc, zfs_ioc_func_t *func,
    zfs_secpolicy_func_t *secpolicy, zfs_ioc_namecheck_t namecheck,
    zfs_ioc_poolcheck_t pool_check, boolean_t smush_outnvlist,
    boolean_t allow_log, const zfs_ioc_key_t *nvl_keys, size_t num_keys)
{
	zfs_ioc_vec_t *vec = &zfs_ioc_vec[ioc - ZFS_IOC_FIRST];

	ASSERT3U(ioc, >=, ZFS_IOC_FIRST);
	ASSERT3U(ioc, <, ZFS_IOC_LAST);
	ASSERT3P(vec->zvec_legacy_func, ==, NULL);
	ASSERT3P(vec->zvec_func, ==, NULL);

	/* if we are logging, the name must be valid */
	ASSERT(!allow_log || namecheck != NO_NAME);

	vec->zvec_name = name;
	vec->zvec_func = func;
	vec->zvec_secpolicy = secpolicy;
	vec->zvec_namecheck = namecheck;
	vec->zvec_pool_check = pool_check;
	vec->zvec_smush_outnvlist = smush_outnvlist;
	vec->zvec_allow_log = allow_log;
	vec->zvec_nvl_keys = nvl_keys;
	vec->zvec_nvl_key_count = num_keys;
}

static void
zfs_ioctl_register_pool(zfs_ioc_t ioc, zfs_ioc_legacy_func_t *func,
    zfs_secpolicy_func_t *secpolicy, boolean_t log_history,
    zfs_ioc_poolcheck_t pool_check)
{
	zfs_ioctl_register_legacy(ioc, func, secpolicy,
	    POOL_NAME, log_history, pool_check);
}

void
zfs_ioctl_register_dataset_nolog(zfs_ioc_t ioc, zfs_ioc_legacy_func_t *func,
    zfs_secpolicy_func_t *secpolicy, zfs_ioc_poolcheck_t pool_check)
{
	zfs_ioctl_register_legacy(ioc, func, secpolicy,
	    DATASET_NAME, B_FALSE, pool_check);
}

static void
zfs_ioctl_register_pool_modify(zfs_ioc_t ioc, zfs_ioc_legacy_func_t *func)
{
	zfs_ioctl_register_legacy(ioc, func, zfs_secpolicy_config,
	    POOL_NAME, B_TRUE, POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY);
}

static void
zfs_ioctl_register_pool_meta(zfs_ioc_t ioc, zfs_ioc_legacy_func_t *func,
    zfs_secpolicy_func_t *secpolicy)
{
	zfs_ioctl_register_legacy(ioc, func, secpolicy,
	    NO_NAME, B_FALSE, POOL_CHECK_NONE);
}

static void
zfs_ioctl_register_dataset_read_secpolicy(zfs_ioc_t ioc,
    zfs_ioc_legacy_func_t *func, zfs_secpolicy_func_t *secpolicy)
{
	zfs_ioctl_register_legacy(ioc, func, secpolicy,
	    DATASET_NAME, B_FALSE, POOL_CHECK_SUSPENDED);
}

static void
zfs_ioctl_register_dataset_read(zfs_ioc_t ioc, zfs_ioc_legacy_func_t *func)
{
	zfs_ioctl_register_dataset_read_secpolicy(ioc, func,
	    zfs_secpolicy_read);
}

static void
zfs_ioctl_register_dataset_modify(zfs_ioc_t ioc, zfs_ioc_legacy_func_t *func,
    zfs_secpolicy_func_t *secpolicy)
{
	zfs_ioctl_register_legacy(ioc, func, secpolicy,
	    DATASET_NAME, B_TRUE, POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY);
}

static void
zfs_ioctl_init(void)
{
	zfs_ioctl_register("snapshot", ZFS_IOC_SNAPSHOT,
	    zfs_ioc_snapshot, zfs_secpolicy_snapshot, POOL_NAME,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_TRUE, B_TRUE,
	    zfs_keys_snapshot, ARRAY_SIZE(zfs_keys_snapshot));

	zfs_ioctl_register("log_history", ZFS_IOC_LOG_HISTORY,
	    zfs_ioc_log_history, zfs_secpolicy_log_history, NO_NAME,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_FALSE, B_FALSE,
	    zfs_keys_log_history, ARRAY_SIZE(zfs_keys_log_history));

	zfs_ioctl_register("space_snaps", ZFS_IOC_SPACE_SNAPS,
	    zfs_ioc_space_snaps, zfs_secpolicy_read, DATASET_NAME,
	    POOL_CHECK_SUSPENDED, B_FALSE, B_FALSE,
	    zfs_keys_space_snaps, ARRAY_SIZE(zfs_keys_space_snaps));

	zfs_ioctl_register("send", ZFS_IOC_SEND_NEW,
	    zfs_ioc_send_new, zfs_secpolicy_send_new, DATASET_NAME,
	    POOL_CHECK_SUSPENDED, B_FALSE, B_FALSE,
	    zfs_keys_send_new, ARRAY_SIZE(zfs_keys_send_new));

	zfs_ioctl_register("send_space", ZFS_IOC_SEND_SPACE,
	    zfs_ioc_send_space, zfs_secpolicy_read, DATASET_NAME,
	    POOL_CHECK_SUSPENDED, B_FALSE, B_FALSE,
	    zfs_keys_send_space, ARRAY_SIZE(zfs_keys_send_space));

	zfs_ioctl_register("create", ZFS_IOC_CREATE,
	    zfs_ioc_create, zfs_secpolicy_create_clone, DATASET_NAME,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_TRUE, B_TRUE,
	    zfs_keys_create, ARRAY_SIZE(zfs_keys_create));

	zfs_ioctl_register("clone", ZFS_IOC_CLONE,
	    zfs_ioc_clone, zfs_secpolicy_create_clone, DATASET_NAME,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_TRUE, B_TRUE,
	    zfs_keys_clone, ARRAY_SIZE(zfs_keys_clone));

	zfs_ioctl_register("remap", ZFS_IOC_REMAP,
	    zfs_ioc_remap, zfs_secpolicy_none, DATASET_NAME,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_FALSE, B_TRUE,
	    zfs_keys_remap, ARRAY_SIZE(zfs_keys_remap));

	zfs_ioctl_register("destroy_snaps", ZFS_IOC_DESTROY_SNAPS,
	    zfs_ioc_destroy_snaps, zfs_secpolicy_destroy_snaps, POOL_NAME,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_TRUE, B_TRUE,
	    zfs_keys_destroy_snaps, ARRAY_SIZE(zfs_keys_destroy_snaps));

	zfs_ioctl_register("hold", ZFS_IOC_HOLD,
	    zfs_ioc_hold, zfs_secpolicy_hold, POOL_NAME,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_TRUE, B_TRUE,
	    zfs_keys_hold, ARRAY_SIZE(zfs_keys_hold));
	zfs_ioctl_register("release", ZFS_IOC_RELEASE,
	    zfs_ioc_release, zfs_secpolicy_release, POOL_NAME,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_TRUE, B_TRUE,
	    zfs_keys_release, ARRAY_SIZE(zfs_keys_release));

	zfs_ioctl_register("get_holds", ZFS_IOC_GET_HOLDS,
	    zfs_ioc_get_holds, zfs_secpolicy_read, DATASET_NAME,
	    POOL_CHECK_SUSPENDED, B_FALSE, B_FALSE,
	    zfs_keys_get_holds, ARRAY_SIZE(zfs_keys_get_holds));

	zfs_ioctl_register("rollback", ZFS_IOC_ROLLBACK,
	    zfs_ioc_rollback, zfs_secpolicy_rollback, DATASET_NAME,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_FALSE, B_TRUE,
	    zfs_keys_rollback, ARRAY_SIZE(zfs_keys_rollback));

	zfs_ioctl_register("bookmark", ZFS_IOC_BOOKMARK,
	    zfs_ioc_bookmark, zfs_secpolicy_bookmark, POOL_NAME,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_TRUE, B_TRUE,
	    zfs_keys_bookmark, ARRAY_SIZE(zfs_keys_bookmark));

	zfs_ioctl_register("get_bookmarks", ZFS_IOC_GET_BOOKMARKS,
	    zfs_ioc_get_bookmarks, zfs_secpolicy_read, DATASET_NAME,
	    POOL_CHECK_SUSPENDED, B_FALSE, B_FALSE,
	    zfs_keys_get_bookmarks, ARRAY_SIZE(zfs_keys_get_bookmarks));

	zfs_ioctl_register("get_bookmark_props", ZFS_IOC_GET_BOOKMARK_PROPS,
	    zfs_ioc_get_bookmark_props, zfs_secpolicy_read, ENTITY_NAME,
	    POOL_CHECK_SUSPENDED, B_FALSE, B_FALSE, zfs_keys_get_bookmark_props,
	    ARRAY_SIZE(zfs_keys_get_bookmark_props));

	zfs_ioctl_register("destroy_bookmarks", ZFS_IOC_DESTROY_BOOKMARKS,
	    zfs_ioc_destroy_bookmarks, zfs_secpolicy_destroy_bookmarks,
	    POOL_NAME,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_TRUE, B_TRUE,
	    zfs_keys_destroy_bookmarks,
	    ARRAY_SIZE(zfs_keys_destroy_bookmarks));

	zfs_ioctl_register("receive", ZFS_IOC_RECV_NEW,
	    zfs_ioc_recv_new, zfs_secpolicy_recv, DATASET_NAME,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_TRUE, B_TRUE,
	    zfs_keys_recv_new, ARRAY_SIZE(zfs_keys_recv_new));
	zfs_ioctl_register("load-key", ZFS_IOC_LOAD_KEY,
	    zfs_ioc_load_key, zfs_secpolicy_load_key,
	    DATASET_NAME, POOL_CHECK_SUSPENDED, B_TRUE, B_TRUE,
	    zfs_keys_load_key, ARRAY_SIZE(zfs_keys_load_key));
	zfs_ioctl_register("unload-key", ZFS_IOC_UNLOAD_KEY,
	    zfs_ioc_unload_key, zfs_secpolicy_load_key,
	    DATASET_NAME, POOL_CHECK_SUSPENDED, B_TRUE, B_TRUE,
	    zfs_keys_unload_key, ARRAY_SIZE(zfs_keys_unload_key));
	zfs_ioctl_register("change-key", ZFS_IOC_CHANGE_KEY,
	    zfs_ioc_change_key, zfs_secpolicy_change_key,
	    DATASET_NAME, POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY,
	    B_TRUE, B_TRUE, zfs_keys_change_key,
	    ARRAY_SIZE(zfs_keys_change_key));

	zfs_ioctl_register("sync", ZFS_IOC_POOL_SYNC,
	    zfs_ioc_pool_sync, zfs_secpolicy_none, POOL_NAME,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_FALSE, B_FALSE,
	    zfs_keys_pool_sync, ARRAY_SIZE(zfs_keys_pool_sync));
	zfs_ioctl_register("reopen", ZFS_IOC_POOL_REOPEN, zfs_ioc_pool_reopen,
	    zfs_secpolicy_config, POOL_NAME, POOL_CHECK_SUSPENDED, B_TRUE,
	    B_TRUE, zfs_keys_pool_reopen, ARRAY_SIZE(zfs_keys_pool_reopen));

	zfs_ioctl_register("channel_program", ZFS_IOC_CHANNEL_PROGRAM,
	    zfs_ioc_channel_program, zfs_secpolicy_config,
	    POOL_NAME, POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_TRUE,
	    B_TRUE, zfs_keys_channel_program,
	    ARRAY_SIZE(zfs_keys_channel_program));

	zfs_ioctl_register("redact", ZFS_IOC_REDACT,
	    zfs_ioc_redact, zfs_secpolicy_config, DATASET_NAME,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_TRUE, B_TRUE,
	    zfs_keys_redact, ARRAY_SIZE(zfs_keys_redact));

	zfs_ioctl_register("zpool_checkpoint", ZFS_IOC_POOL_CHECKPOINT,
	    zfs_ioc_pool_checkpoint, zfs_secpolicy_config, POOL_NAME,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_TRUE, B_TRUE,
	    zfs_keys_pool_checkpoint, ARRAY_SIZE(zfs_keys_pool_checkpoint));

	zfs_ioctl_register("zpool_discard_checkpoint",
	    ZFS_IOC_POOL_DISCARD_CHECKPOINT, zfs_ioc_pool_discard_checkpoint,
	    zfs_secpolicy_config, POOL_NAME,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_TRUE, B_TRUE,
	    zfs_keys_pool_discard_checkpoint,
	    ARRAY_SIZE(zfs_keys_pool_discard_checkpoint));

	zfs_ioctl_register("zpool_prefetch",
	    ZFS_IOC_POOL_PREFETCH, zfs_ioc_pool_prefetch,
	    zfs_secpolicy_config, POOL_NAME,
	    POOL_CHECK_SUSPENDED, B_TRUE, B_TRUE,
	    zfs_keys_pool_prefetch, ARRAY_SIZE(zfs_keys_pool_prefetch));

	zfs_ioctl_register("initialize", ZFS_IOC_POOL_INITIALIZE,
	    zfs_ioc_pool_initialize, zfs_secpolicy_config, POOL_NAME,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_TRUE, B_TRUE,
	    zfs_keys_pool_initialize, ARRAY_SIZE(zfs_keys_pool_initialize));

	zfs_ioctl_register("trim", ZFS_IOC_POOL_TRIM,
	    zfs_ioc_pool_trim, zfs_secpolicy_config, POOL_NAME,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_TRUE, B_TRUE,
	    zfs_keys_pool_trim, ARRAY_SIZE(zfs_keys_pool_trim));

	zfs_ioctl_register("wait", ZFS_IOC_WAIT,
	    zfs_ioc_wait, zfs_secpolicy_none, POOL_NAME,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_FALSE, B_FALSE,
	    zfs_keys_pool_wait, ARRAY_SIZE(zfs_keys_pool_wait));

	zfs_ioctl_register("wait_fs", ZFS_IOC_WAIT_FS,
	    zfs_ioc_wait_fs, zfs_secpolicy_none, DATASET_NAME,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_FALSE, B_FALSE,
	    zfs_keys_fs_wait, ARRAY_SIZE(zfs_keys_fs_wait));

	zfs_ioctl_register("set_bootenv", ZFS_IOC_SET_BOOTENV,
	    zfs_ioc_set_bootenv, zfs_secpolicy_config, POOL_NAME,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_FALSE, B_TRUE,
	    zfs_keys_set_bootenv, ARRAY_SIZE(zfs_keys_set_bootenv));

	zfs_ioctl_register("get_bootenv", ZFS_IOC_GET_BOOTENV,
	    zfs_ioc_get_bootenv, zfs_secpolicy_none, POOL_NAME,
	    POOL_CHECK_SUSPENDED, B_FALSE, B_TRUE,
	    zfs_keys_get_bootenv, ARRAY_SIZE(zfs_keys_get_bootenv));

	zfs_ioctl_register("zpool_vdev_get_props", ZFS_IOC_VDEV_GET_PROPS,
	    zfs_ioc_vdev_get_props, zfs_secpolicy_read, POOL_NAME,
	    POOL_CHECK_NONE, B_FALSE, B_FALSE, zfs_keys_vdev_get_props,
	    ARRAY_SIZE(zfs_keys_vdev_get_props));

	zfs_ioctl_register("zpool_vdev_set_props", ZFS_IOC_VDEV_SET_PROPS,
	    zfs_ioc_vdev_set_props, zfs_secpolicy_config, POOL_NAME,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_FALSE, B_FALSE,
	    zfs_keys_vdev_set_props, ARRAY_SIZE(zfs_keys_vdev_set_props));

	zfs_ioctl_register("scrub", ZFS_IOC_POOL_SCRUB,
	    zfs_ioc_pool_scrub, zfs_secpolicy_config, POOL_NAME,
	    POOL_CHECK_NONE, B_TRUE, B_TRUE,
	    zfs_keys_pool_scrub, ARRAY_SIZE(zfs_keys_pool_scrub));

	zfs_ioctl_register("get_props", ZFS_IOC_POOL_GET_PROPS,
	    zfs_ioc_pool_get_props, zfs_secpolicy_read, POOL_NAME,
	    POOL_CHECK_NONE, B_FALSE, B_FALSE,
	    zfs_keys_get_props, ARRAY_SIZE(zfs_keys_get_props));

	/* IOCTLS that use the legacy function signature */

	zfs_ioctl_register_legacy(ZFS_IOC_POOL_FREEZE, zfs_ioc_pool_freeze,
	    zfs_secpolicy_config, NO_NAME, B_FALSE, POOL_CHECK_READONLY);

	zfs_ioctl_register_pool(ZFS_IOC_POOL_CREATE, zfs_ioc_pool_create,
	    zfs_secpolicy_config, B_TRUE, POOL_CHECK_NONE);
	zfs_ioctl_register_pool_modify(ZFS_IOC_POOL_SCAN,
	    zfs_ioc_pool_scan);
	zfs_ioctl_register_pool_modify(ZFS_IOC_POOL_UPGRADE,
	    zfs_ioc_pool_upgrade);
	zfs_ioctl_register_pool_modify(ZFS_IOC_VDEV_ADD,
	    zfs_ioc_vdev_add);
	zfs_ioctl_register_pool_modify(ZFS_IOC_VDEV_REMOVE,
	    zfs_ioc_vdev_remove);
	zfs_ioctl_register_pool_modify(ZFS_IOC_VDEV_SET_STATE,
	    zfs_ioc_vdev_set_state);
	zfs_ioctl_register_pool_modify(ZFS_IOC_VDEV_ATTACH,
	    zfs_ioc_vdev_attach);
	zfs_ioctl_register_pool_modify(ZFS_IOC_VDEV_DETACH,
	    zfs_ioc_vdev_detach);
	zfs_ioctl_register_pool_modify(ZFS_IOC_VDEV_SETPATH,
	    zfs_ioc_vdev_setpath);
	zfs_ioctl_register_pool_modify(ZFS_IOC_VDEV_SETFRU,
	    zfs_ioc_vdev_setfru);
	zfs_ioctl_register_pool_modify(ZFS_IOC_POOL_SET_PROPS,
	    zfs_ioc_pool_set_props);
	zfs_ioctl_register_pool_modify(ZFS_IOC_VDEV_SPLIT,
	    zfs_ioc_vdev_split);
	zfs_ioctl_register_pool_modify(ZFS_IOC_POOL_REGUID,
	    zfs_ioc_pool_reguid);

	zfs_ioctl_register_pool_meta(ZFS_IOC_POOL_CONFIGS,
	    zfs_ioc_pool_configs, zfs_secpolicy_none);
	zfs_ioctl_register_pool_meta(ZFS_IOC_POOL_TRYIMPORT,
	    zfs_ioc_pool_tryimport, zfs_secpolicy_config);
	zfs_ioctl_register_pool_meta(ZFS_IOC_INJECT_FAULT,
	    zfs_ioc_inject_fault, zfs_secpolicy_inject);
	zfs_ioctl_register_pool_meta(ZFS_IOC_CLEAR_FAULT,
	    zfs_ioc_clear_fault, zfs_secpolicy_inject);
	zfs_ioctl_register_pool_meta(ZFS_IOC_INJECT_LIST_NEXT,
	    zfs_ioc_inject_list_next, zfs_secpolicy_inject);

	/*
	 * pool destroy, and export don't log the history as part of
	 * zfsdev_ioctl, but rather zfs_ioc_pool_export
	 * does the logging of those commands.
	 */
	zfs_ioctl_register_pool(ZFS_IOC_POOL_DESTROY, zfs_ioc_pool_destroy,
	    zfs_secpolicy_config, B_FALSE, POOL_CHECK_SUSPENDED);
	zfs_ioctl_register_pool(ZFS_IOC_POOL_EXPORT, zfs_ioc_pool_export,
	    zfs_secpolicy_config, B_FALSE, POOL_CHECK_SUSPENDED);

	zfs_ioctl_register_pool(ZFS_IOC_POOL_STATS, zfs_ioc_pool_stats,
	    zfs_secpolicy_read, B_FALSE, POOL_CHECK_NONE);

	zfs_ioctl_register_pool(ZFS_IOC_ERROR_LOG, zfs_ioc_error_log,
	    zfs_secpolicy_inject, B_FALSE, POOL_CHECK_SUSPENDED);
	zfs_ioctl_register_pool(ZFS_IOC_DSOBJ_TO_DSNAME,
	    zfs_ioc_dsobj_to_dsname,
	    zfs_secpolicy_diff, B_FALSE, POOL_CHECK_SUSPENDED);
	zfs_ioctl_register_pool(ZFS_IOC_POOL_GET_HISTORY,
	    zfs_ioc_pool_get_history,
	    zfs_secpolicy_config, B_FALSE, POOL_CHECK_SUSPENDED);

	zfs_ioctl_register_pool(ZFS_IOC_POOL_IMPORT, zfs_ioc_pool_import,
	    zfs_secpolicy_config, B_TRUE, POOL_CHECK_NONE);

	zfs_ioctl_register_pool(ZFS_IOC_CLEAR, zfs_ioc_clear,
	    zfs_secpolicy_config, B_TRUE, POOL_CHECK_READONLY);

	zfs_ioctl_register_dataset_read(ZFS_IOC_SPACE_WRITTEN,
	    zfs_ioc_space_written);
	zfs_ioctl_register_dataset_read(ZFS_IOC_OBJSET_RECVD_PROPS,
	    zfs_ioc_objset_recvd_props);
	zfs_ioctl_register_dataset_read(ZFS_IOC_NEXT_OBJ,
	    zfs_ioc_next_obj);
	zfs_ioctl_register_dataset_read(ZFS_IOC_GET_FSACL,
	    zfs_ioc_get_fsacl);
	zfs_ioctl_register_dataset_read(ZFS_IOC_OBJSET_STATS,
	    zfs_ioc_objset_stats);
	zfs_ioctl_register_dataset_read(ZFS_IOC_OBJSET_ZPLPROPS,
	    zfs_ioc_objset_zplprops);
	zfs_ioctl_register_dataset_read(ZFS_IOC_DATASET_LIST_NEXT,
	    zfs_ioc_dataset_list_next);
	zfs_ioctl_register_dataset_read(ZFS_IOC_SNAPSHOT_LIST_NEXT,
	    zfs_ioc_snapshot_list_next);
	zfs_ioctl_register_dataset_read(ZFS_IOC_SEND_PROGRESS,
	    zfs_ioc_send_progress);

	zfs_ioctl_register_dataset_read_secpolicy(ZFS_IOC_DIFF,
	    zfs_ioc_diff, zfs_secpolicy_diff);
	zfs_ioctl_register_dataset_read_secpolicy(ZFS_IOC_OBJ_TO_STATS,
	    zfs_ioc_obj_to_stats, zfs_secpolicy_diff);
	zfs_ioctl_register_dataset_read_secpolicy(ZFS_IOC_OBJ_TO_PATH,
	    zfs_ioc_obj_to_path, zfs_secpolicy_diff);
	zfs_ioctl_register_dataset_read_secpolicy(ZFS_IOC_USERSPACE_ONE,
	    zfs_ioc_userspace_one, zfs_secpolicy_userspace_one);
	zfs_ioctl_register_dataset_read_secpolicy(ZFS_IOC_USERSPACE_MANY,
	    zfs_ioc_userspace_many, zfs_secpolicy_userspace_many);
	zfs_ioctl_register_dataset_read_secpolicy(ZFS_IOC_SEND,
	    zfs_ioc_send, zfs_secpolicy_send);

	zfs_ioctl_register_dataset_modify(ZFS_IOC_SET_PROP, zfs_ioc_set_prop,
	    zfs_secpolicy_none);
	zfs_ioctl_register_dataset_modify(ZFS_IOC_DESTROY, zfs_ioc_destroy,
	    zfs_secpolicy_destroy);
	zfs_ioctl_register_dataset_modify(ZFS_IOC_RENAME, zfs_ioc_rename,
	    zfs_secpolicy_rename);
	zfs_ioctl_register_dataset_modify(ZFS_IOC_RECV, zfs_ioc_recv,
	    zfs_secpolicy_recv);
	zfs_ioctl_register_dataset_modify(ZFS_IOC_PROMOTE, zfs_ioc_promote,
	    zfs_secpolicy_promote);
	zfs_ioctl_register_dataset_modify(ZFS_IOC_INHERIT_PROP,
	    zfs_ioc_inherit_prop, zfs_secpolicy_inherit_prop);
	zfs_ioctl_register_dataset_modify(ZFS_IOC_SET_FSACL, zfs_ioc_set_fsacl,
	    zfs_secpolicy_set_fsacl);

	zfs_ioctl_register_dataset_nolog(ZFS_IOC_SHARE, zfs_ioc_share,
	    zfs_secpolicy_share, POOL_CHECK_NONE);
	zfs_ioctl_register_dataset_nolog(ZFS_IOC_SMB_ACL, zfs_ioc_smb_acl,
	    zfs_secpolicy_smb_acl, POOL_CHECK_NONE);
	zfs_ioctl_register_dataset_nolog(ZFS_IOC_USERSPACE_UPGRADE,
	    zfs_ioc_userspace_upgrade, zfs_secpolicy_userspace_upgrade,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY);
	zfs_ioctl_register_dataset_nolog(ZFS_IOC_TMP_SNAPSHOT,
	    zfs_ioc_tmp_snapshot, zfs_secpolicy_tmp_snapshot,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY);

	zfs_ioctl_register_legacy(ZFS_IOC_EVENTS_NEXT, zfs_ioc_events_next,
	    zfs_secpolicy_config, NO_NAME, B_FALSE, POOL_CHECK_NONE);
	zfs_ioctl_register_legacy(ZFS_IOC_EVENTS_CLEAR, zfs_ioc_events_clear,
	    zfs_secpolicy_config, NO_NAME, B_FALSE, POOL_CHECK_NONE);
	zfs_ioctl_register_legacy(ZFS_IOC_EVENTS_SEEK, zfs_ioc_events_seek,
	    zfs_secpolicy_config, NO_NAME, B_FALSE, POOL_CHECK_NONE);

	zfs_ioctl_init_os();
}

/*
 * Verify that for non-legacy ioctls the input nvlist
 * pairs match against the expected input.
 *
 * Possible errors are:
 * ZFS_ERR_IOC_ARG_UNAVAIL	An unrecognized nvpair was encountered
 * ZFS_ERR_IOC_ARG_REQUIRED	A required nvpair is missing
 * ZFS_ERR_IOC_ARG_BADTYPE	Invalid type for nvpair
 */
static int
zfs_check_input_nvpairs(nvlist_t *innvl, const zfs_ioc_vec_t *vec)
{
	const zfs_ioc_key_t *nvl_keys = vec->zvec_nvl_keys;
	boolean_t required_keys_found = B_FALSE;

	/*
	 * examine each input pair
	 */
	for (nvpair_t *pair = nvlist_next_nvpair(innvl, NULL);
	    pair != NULL; pair = nvlist_next_nvpair(innvl, pair)) {
		const char *name = nvpair_name(pair);
		data_type_t type = nvpair_type(pair);
		boolean_t identified = B_FALSE;

		/*
		 * check pair against the documented names and type
		 */
		for (int k = 0; k < vec->zvec_nvl_key_count; k++) {
			/* if not a wild card name, check for an exact match */
			if ((nvl_keys[k].zkey_flags & ZK_WILDCARDLIST) == 0 &&
			    strcmp(nvl_keys[k].zkey_name, name) != 0)
				continue;

			identified = B_TRUE;

			if (nvl_keys[k].zkey_type != DATA_TYPE_ANY &&
			    nvl_keys[k].zkey_type != type) {
				return (SET_ERROR(ZFS_ERR_IOC_ARG_BADTYPE));
			}

			if (nvl_keys[k].zkey_flags & ZK_OPTIONAL)
				continue;

			required_keys_found = B_TRUE;
			break;
		}

		/* allow an 'optional' key, everything else is invalid */
		if (!identified &&
		    (strcmp(name, "optional") != 0 ||
		    type != DATA_TYPE_NVLIST)) {
			return (SET_ERROR(ZFS_ERR_IOC_ARG_UNAVAIL));
		}
	}

	/* verify that all required keys were found */
	for (int k = 0; k < vec->zvec_nvl_key_count; k++) {
		if (nvl_keys[k].zkey_flags & ZK_OPTIONAL)
			continue;

		if (nvl_keys[k].zkey_flags & ZK_WILDCARDLIST) {
			/* at least one non-optional key is expected here */
			if (!required_keys_found)
				return (SET_ERROR(ZFS_ERR_IOC_ARG_REQUIRED));
			continue;
		}

		if (!nvlist_exists(innvl, nvl_keys[k].zkey_name))
			return (SET_ERROR(ZFS_ERR_IOC_ARG_REQUIRED));
	}

	return (0);
}

static int
pool_status_check(const char *name, zfs_ioc_namecheck_t type,
    zfs_ioc_poolcheck_t check)
{
	spa_t *spa;
	int error;

	ASSERT(type == POOL_NAME || type == DATASET_NAME ||
	    type == ENTITY_NAME);

	if (check & POOL_CHECK_NONE)
		return (0);

	error = spa_open(name, &spa, FTAG);
	if (error == 0) {
		if ((check & POOL_CHECK_SUSPENDED) && spa_suspended(spa))
			error = SET_ERROR(EAGAIN);
		else if ((check & POOL_CHECK_READONLY) && !spa_writeable(spa))
			error = SET_ERROR(EROFS);
		spa_close(spa, FTAG);
	}
	return (error);
}

int
zfsdev_getminor(zfs_file_t *fp, minor_t *minorp)
{
	zfsdev_state_t *zs, *fpd;

	ASSERT(!MUTEX_HELD(&zfsdev_state_lock));

	fpd = zfs_file_private(fp);
	if (fpd == NULL)
		return (SET_ERROR(EBADF));

	mutex_enter(&zfsdev_state_lock);

	for (zs = &zfsdev_state_listhead; zs != NULL; zs = zs->zs_next) {

		if (zs->zs_minor == -1)
			continue;

		if (fpd == zs) {
			*minorp = fpd->zs_minor;
			mutex_exit(&zfsdev_state_lock);
			return (0);
		}
	}

	mutex_exit(&zfsdev_state_lock);

	return (SET_ERROR(EBADF));
}

void *
zfsdev_get_state(minor_t minor, enum zfsdev_state_type which)
{
	zfsdev_state_t *zs;

	for (zs = &zfsdev_state_listhead; zs != NULL; zs = zs->zs_next) {
		if (zs->zs_minor == minor) {
			membar_consumer();
			switch (which) {
			case ZST_ONEXIT:
				return (zs->zs_onexit);
			case ZST_ZEVENT:
				return (zs->zs_zevent);
			case ZST_ALL:
				return (zs);
			}
		}
	}

	return (NULL);
}

/*
 * Find a free minor number.  The zfsdev_state_list is expected to
 * be short since it is only a list of currently open file handles.
 */
static minor_t
zfsdev_minor_alloc(void)
{
	static minor_t last_minor = 0;
	minor_t m;

	ASSERT(MUTEX_HELD(&zfsdev_state_lock));

	for (m = last_minor + 1; m != last_minor; m++) {
		if (m > ZFSDEV_MAX_MINOR)
			m = 1;
		if (zfsdev_get_state(m, ZST_ALL) == NULL) {
			last_minor = m;
			return (m);
		}
	}

	return (0);
}

int
zfsdev_state_init(void *priv)
{
	zfsdev_state_t *zs, *zsprev = NULL;
	minor_t minor;
	boolean_t newzs = B_FALSE;

	ASSERT(MUTEX_HELD(&zfsdev_state_lock));

	minor = zfsdev_minor_alloc();
	if (minor == 0)
		return (SET_ERROR(ENXIO));

	for (zs = &zfsdev_state_listhead; zs != NULL; zs = zs->zs_next) {
		if (zs->zs_minor == -1)
			break;
		zsprev = zs;
	}

	if (!zs) {
		zs = kmem_zalloc(sizeof (zfsdev_state_t), KM_SLEEP);
		newzs = B_TRUE;
	}

	zfsdev_private_set_state(priv, zs);

	zfs_onexit_init((zfs_onexit_t **)&zs->zs_onexit);
	zfs_zevent_init((zfs_zevent_t **)&zs->zs_zevent);

	/*
	 * In order to provide for lock-free concurrent read access
	 * to the minor list in zfsdev_get_state(), new entries
	 * must be completely written before linking them into the
	 * list whereas existing entries are already linked; the last
	 * operation must be updating zs_minor (from -1 to the new
	 * value).
	 */
	if (newzs) {
		zs->zs_minor = minor;
		membar_producer();
		zsprev->zs_next = zs;
	} else {
		membar_producer();
		zs->zs_minor = minor;
	}

	return (0);
}

void
zfsdev_state_destroy(void *priv)
{
	zfsdev_state_t *zs = zfsdev_private_get_state(priv);

	ASSERT(zs != NULL);
	ASSERT3S(zs->zs_minor, >, 0);

	/*
	 * The last reference to this zfsdev file descriptor is being dropped.
	 * We don't have to worry about lookup grabbing this state object, and
	 * zfsdev_state_init() will not try to reuse this object until it is
	 * invalidated by setting zs_minor to -1.  Invalidation must be done
	 * last, with a memory barrier to ensure ordering.  This lets us avoid
	 * taking the global zfsdev state lock around destruction.
	 */
	zfs_onexit_destroy(zs->zs_onexit);
	zfs_zevent_destroy(zs->zs_zevent);
	zs->zs_onexit = NULL;
	zs->zs_zevent = NULL;
	membar_producer();
	zs->zs_minor = -1;
}

long
zfsdev_ioctl_common(uint_t vecnum, zfs_cmd_t *zc, int flag)
{
	int error, cmd;
	const zfs_ioc_vec_t *vec;
	char *saved_poolname = NULL;
	uint64_t max_nvlist_src_size;
	size_t saved_poolname_len = 0;
	nvlist_t *innvl = NULL;
	fstrans_cookie_t cookie;
	hrtime_t start_time = gethrtime();

	cmd = vecnum;
	error = 0;
	if (vecnum >= sizeof (zfs_ioc_vec) / sizeof (zfs_ioc_vec[0]))
		return (SET_ERROR(ZFS_ERR_IOC_CMD_UNAVAIL));

	vec = &zfs_ioc_vec[vecnum];

	/*
	 * The registered ioctl list may be sparse, verify that either
	 * a normal or legacy handler are registered.
	 */
	if (vec->zvec_func == NULL && vec->zvec_legacy_func == NULL)
		return (SET_ERROR(ZFS_ERR_IOC_CMD_UNAVAIL));

	zc->zc_iflags = flag & FKIOCTL;
	max_nvlist_src_size = zfs_max_nvlist_src_size_os();
	if (zc->zc_nvlist_src_size > max_nvlist_src_size) {
		/*
		 * Make sure the user doesn't pass in an insane value for
		 * zc_nvlist_src_size.  We have to check, since we will end
		 * up allocating that much memory inside of get_nvlist().  This
		 * prevents a nefarious user from allocating tons of kernel
		 * memory.
		 *
		 * Also, we return EINVAL instead of ENOMEM here.  The reason
		 * being that returning ENOMEM from an ioctl() has a special
		 * connotation; that the user's size value is too small and
		 * needs to be expanded to hold the nvlist.  See
		 * zcmd_expand_dst_nvlist() for details.
		 */
		error = SET_ERROR(EINVAL);	/* User's size too big */

	} else if (zc->zc_nvlist_src_size != 0) {
		error = get_nvlist(zc->zc_nvlist_src, zc->zc_nvlist_src_size,
		    zc->zc_iflags, &innvl);
		if (error != 0)
			goto out;
	}

	/*
	 * Ensure that all pool/dataset names are valid before we pass down to
	 * the lower layers.
	 */
	zc->zc_name[sizeof (zc->zc_name) - 1] = '\0';
	switch (vec->zvec_namecheck) {
	case POOL_NAME:
		if (pool_namecheck(zc->zc_name, NULL, NULL) != 0)
			error = SET_ERROR(EINVAL);
		else
			error = pool_status_check(zc->zc_name,
			    vec->zvec_namecheck, vec->zvec_pool_check);
		break;

	case DATASET_NAME:
		if (dataset_namecheck(zc->zc_name, NULL, NULL) != 0)
			error = SET_ERROR(EINVAL);
		else
			error = pool_status_check(zc->zc_name,
			    vec->zvec_namecheck, vec->zvec_pool_check);
		break;

	case ENTITY_NAME:
		if (entity_namecheck(zc->zc_name, NULL, NULL) != 0) {
			error = SET_ERROR(EINVAL);
		} else {
			error = pool_status_check(zc->zc_name,
			    vec->zvec_namecheck, vec->zvec_pool_check);
		}
		break;

	case NO_NAME:
		break;
	}
	/*
	 * Ensure that all input pairs are valid before we pass them down
	 * to the lower layers.
	 *
	 * The vectored functions can use fnvlist_lookup_{type} for any
	 * required pairs since zfs_check_input_nvpairs() confirmed that
	 * they exist and are of the correct type.
	 */
	if (error == 0 && vec->zvec_func != NULL) {
		error = zfs_check_input_nvpairs(innvl, vec);
		if (error != 0)
			goto out;
	}

	if (error == 0) {
		cookie = spl_fstrans_mark();
		error = vec->zvec_secpolicy(zc, innvl, CRED());
		spl_fstrans_unmark(cookie);
	}

	if (error != 0)
		goto out;

	/* legacy ioctls can modify zc_name */
	/*
	 * Can't use kmem_strdup() as we might truncate the string and
	 * kmem_strfree() would then free with incorrect size.
	 */
	saved_poolname_len = strlen(zc->zc_name) + 1;
	saved_poolname = kmem_alloc(saved_poolname_len, KM_SLEEP);

	strlcpy(saved_poolname, zc->zc_name, saved_poolname_len);
	saved_poolname[strcspn(saved_poolname, "/@#")] = '\0';

	if (vec->zvec_func != NULL) {
		nvlist_t *outnvl;
		int puterror = 0;
		spa_t *spa;
		nvlist_t *lognv = NULL;

		ASSERT(vec->zvec_legacy_func == NULL);

		/*
		 * Add the innvl to the lognv before calling the func,
		 * in case the func changes the innvl.
		 */
		if (vec->zvec_allow_log) {
			lognv = fnvlist_alloc();
			fnvlist_add_string(lognv, ZPOOL_HIST_IOCTL,
			    vec->zvec_name);
			if (!nvlist_empty(innvl)) {
				fnvlist_add_nvlist(lognv, ZPOOL_HIST_INPUT_NVL,
				    innvl);
			}
		}

		outnvl = fnvlist_alloc();
		cookie = spl_fstrans_mark();
		error = vec->zvec_func(zc->zc_name, innvl, outnvl);
		spl_fstrans_unmark(cookie);

		/*
		 * Some commands can partially execute, modify state, and still
		 * return an error.  In these cases, attempt to record what
		 * was modified.
		 */
		if ((error == 0 ||
		    (cmd == ZFS_IOC_CHANNEL_PROGRAM && error != EINVAL)) &&
		    vec->zvec_allow_log &&
		    spa_open(zc->zc_name, &spa, FTAG) == 0) {
			if (!nvlist_empty(outnvl)) {
				size_t out_size = fnvlist_size(outnvl);
				if (out_size > zfs_history_output_max) {
					fnvlist_add_int64(lognv,
					    ZPOOL_HIST_OUTPUT_SIZE, out_size);
				} else {
					fnvlist_add_nvlist(lognv,
					    ZPOOL_HIST_OUTPUT_NVL, outnvl);
				}
			}
			if (error != 0) {
				fnvlist_add_int64(lognv, ZPOOL_HIST_ERRNO,
				    error);
			}
			fnvlist_add_int64(lognv, ZPOOL_HIST_ELAPSED_NS,
			    gethrtime() - start_time);
			(void) spa_history_log_nvl(spa, lognv);
			spa_close(spa, FTAG);
		}
		fnvlist_free(lognv);

		if (!nvlist_empty(outnvl) || zc->zc_nvlist_dst_size != 0) {
			int smusherror = 0;
			if (vec->zvec_smush_outnvlist) {
				smusherror = nvlist_smush(outnvl,
				    zc->zc_nvlist_dst_size);
			}
			if (smusherror == 0)
				puterror = put_nvlist(zc, outnvl);
		}

		if (puterror != 0)
			error = puterror;

		nvlist_free(outnvl);
	} else {
		cookie = spl_fstrans_mark();
		error = vec->zvec_legacy_func(zc);
		spl_fstrans_unmark(cookie);
	}

out:
	nvlist_free(innvl);
	if (error == 0 && vec->zvec_allow_log) {
		char *s = tsd_get(zfs_allow_log_key);
		if (s != NULL)
			kmem_strfree(s);
		(void) tsd_set(zfs_allow_log_key, kmem_strdup(saved_poolname));
	}
	if (saved_poolname != NULL)
		kmem_free(saved_poolname, saved_poolname_len);

	return (error);
}

int
zfs_kmod_init(void)
{
	int error;

	if ((error = zvol_init()) != 0)
		return (error);

	spa_init(SPA_MODE_READ | SPA_MODE_WRITE);
	zfs_init();

	zfs_ioctl_init();

	mutex_init(&zfsdev_state_lock, NULL, MUTEX_DEFAULT, NULL);
	zfsdev_state_listhead.zs_minor = -1;

	if ((error = zfsdev_attach()) != 0)
		goto out;

	tsd_create(&rrw_tsd_key, rrw_tsd_destroy);
	tsd_create(&zfs_allow_log_key, zfs_allow_log_destroy);

	return (0);
out:
	zfs_fini();
	spa_fini();
	zvol_fini();

	return (error);
}

void
zfs_kmod_fini(void)
{
	zfsdev_state_t *zs, *zsnext = NULL;

	zfsdev_detach();

	mutex_destroy(&zfsdev_state_lock);

	for (zs = &zfsdev_state_listhead; zs != NULL; zs = zsnext) {
		zsnext = zs->zs_next;
		if (zs->zs_onexit)
			zfs_onexit_destroy(zs->zs_onexit);
		if (zs->zs_zevent)
			zfs_zevent_destroy(zs->zs_zevent);
		if (zs != &zfsdev_state_listhead)
			kmem_free(zs, sizeof (zfsdev_state_t));
	}

	zfs_ereport_taskq_fini();	/* run before zfs_fini() on Linux */
	zfs_fini();
	spa_fini();
	zvol_fini();

	tsd_destroy(&rrw_tsd_key);
	tsd_destroy(&zfs_allow_log_key);
}

ZFS_MODULE_PARAM(zfs, zfs_, max_nvlist_src_size, U64, ZMOD_RW,
	"Maximum size in bytes allowed for src nvlist passed with ZFS ioctls");

ZFS_MODULE_PARAM(zfs, zfs_, history_output_max, U64, ZMOD_RW,
	"Maximum size in bytes of ZFS ioctl output that will be logged");
