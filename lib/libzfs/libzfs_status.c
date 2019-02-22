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
 * Copyright (c) 2012 by Delphix. All rights reserved.
 * Copyright (c) 2013 Steven Hartland. All rights reserved.
 */

/*
 * This file contains the functions which analyze the status of a pool.  This
 * include both the status of an active pool, as well as the status exported
 * pools.  Returns one of the ZPOOL_STATUS_* defines describing the status of
 * the pool.  This status is independent (to a certain degree) from the state of
 * the pool.  A pool's state describes only whether or not it is capable of
 * providing the necessary fault tolerance for data.  The status describes the
 * overall status of devices.  A pool that is online can still have a device
 * that is experiencing errors.
 *
 * Only a subset of the possible faults can be detected using 'zpool status',
 * and not all possible errors correspond to a FMA message ID.  The explanation
 * is left up to the caller, depending on whether it is a live pool or an
 * import.
 */

#include <libzfs.h>
#include <libzutil.h>
#include <string.h>
#include <unistd.h>
#include <sys/systeminfo.h>
#include "libzfs_impl.h"
#include "zfeature_common.h"

/*
 * Message ID table.  This must be kept in sync with the ZPOOL_STATUS_* defines
 * in include/libzfs.h.  Note that there are some status results which go past
 * the end of this table, and hence have no associated message ID.
 */
static char *zfs_msgid_table[] = {
	"ZFS-8000-14", /* ZPOOL_STATUS_CORRUPT_CACHE */
	"ZFS-8000-2Q", /* ZPOOL_STATUS_MISSING_DEV_R */
	"ZFS-8000-3C", /* ZPOOL_STATUS_MISSING_DEV_NR */
	"ZFS-8000-4J", /* ZPOOL_STATUS_CORRUPT_LABEL_R */
	"ZFS-8000-5E", /* ZPOOL_STATUS_CORRUPT_LABEL_NR */
	"ZFS-8000-6X", /* ZPOOL_STATUS_BAD_GUID_SUM */
	"ZFS-8000-72", /* ZPOOL_STATUS_CORRUPT_POOL */
	"ZFS-8000-8A", /* ZPOOL_STATUS_CORRUPT_DATA */
	"ZFS-8000-9P", /* ZPOOL_STATUS_FAILING_DEV */
	"ZFS-8000-A5", /* ZPOOL_STATUS_VERSION_NEWER */
	"ZFS-8000-EY", /* ZPOOL_STATUS_HOSTID_MISMATCH */
	"ZFS-8000-EY", /* ZPOOL_STATUS_HOSTID_ACTIVE */
	"ZFS-8000-EY", /* ZPOOL_STATUS_HOSTID_REQUIRED */
	"ZFS-8000-HC", /* ZPOOL_STATUS_IO_FAILURE_WAIT */
	"ZFS-8000-JQ", /* ZPOOL_STATUS_IO_FAILURE_CONTINUE */
	"ZFS-8000-MM", /* ZPOOL_STATUS_IO_FAILURE_MMP */
	"ZFS-8000-K4", /* ZPOOL_STATUS_BAD_LOG */
	"ZFS-8000-ER", /* ZPOOL_STATUS_ERRATA */
	/*
	 * The following results have no message ID.
	 *	ZPOOL_STATUS_UNSUP_FEAT_READ
	 *	ZPOOL_STATUS_UNSUP_FEAT_WRITE
	 *	ZPOOL_STATUS_FAULTED_DEV_R
	 *	ZPOOL_STATUS_FAULTED_DEV_NR
	 *	ZPOOL_STATUS_VERSION_OLDER
	 *	ZPOOL_STATUS_FEAT_DISABLED
	 *	ZPOOL_STATUS_RESILVERING
	 *	ZPOOL_STATUS_OFFLINE_DEV
	 *	ZPOOL_STATUS_REMOVED_DEV
	 *	ZPOOL_STATUS_OK
	 */
};

#define	NMSGID	(sizeof (zfs_msgid_table) / sizeof (zfs_msgid_table[0]))

/* ARGSUSED */
static int
vdev_missing(uint64_t state, uint64_t aux, uint64_t errs)
{
	return (state == VDEV_STATE_CANT_OPEN &&
	    aux == VDEV_AUX_OPEN_FAILED);
}

/* ARGSUSED */
static int
vdev_faulted(uint64_t state, uint64_t aux, uint64_t errs)
{
	return (state == VDEV_STATE_FAULTED);
}

/* ARGSUSED */
static int
vdev_errors(uint64_t state, uint64_t aux, uint64_t errs)
{
	return (state == VDEV_STATE_DEGRADED || errs != 0);
}

/* ARGSUSED */
static int
vdev_broken(uint64_t state, uint64_t aux, uint64_t errs)
{
	return (state == VDEV_STATE_CANT_OPEN);
}

/* ARGSUSED */
static int
vdev_offlined(uint64_t state, uint64_t aux, uint64_t errs)
{
	return (state == VDEV_STATE_OFFLINE);
}

/* ARGSUSED */
static int
vdev_removed(uint64_t state, uint64_t aux, uint64_t errs)
{
	return (state == VDEV_STATE_REMOVED);
}

/*
 * Detect if any leaf devices that have seen errors or could not be opened.
 */
static boolean_t
find_vdev_problem(nvlist_t *vdev, int (*func)(uint64_t, uint64_t, uint64_t))
{
	nvlist_t **child;
	vdev_stat_t *vs;
	uint_t c, children;
	char *type;

	/*
	 * Ignore problems within a 'replacing' vdev, since we're presumably in
	 * the process of repairing any such errors, and don't want to call them
	 * out again.  We'll pick up the fact that a resilver is happening
	 * later.
	 */
	verify(nvlist_lookup_string(vdev, ZPOOL_CONFIG_TYPE, &type) == 0);
	if (strcmp(type, VDEV_TYPE_REPLACING) == 0)
		return (B_FALSE);

	if (nvlist_lookup_nvlist_array(vdev, ZPOOL_CONFIG_CHILDREN, &child,
	    &children) == 0) {
		for (c = 0; c < children; c++)
			if (find_vdev_problem(child[c], func))
				return (B_TRUE);
	} else {
		verify(nvlist_lookup_uint64_array(vdev, ZPOOL_CONFIG_VDEV_STATS,
		    (uint64_t **)&vs, &c) == 0);

		if (func(vs->vs_state, vs->vs_aux,
		    vs->vs_read_errors +
		    vs->vs_write_errors +
		    vs->vs_checksum_errors))
			return (B_TRUE);
	}

	/*
	 * Check any L2 cache devs
	 */
	if (nvlist_lookup_nvlist_array(vdev, ZPOOL_CONFIG_L2CACHE, &child,
	    &children) == 0) {
		for (c = 0; c < children; c++)
			if (find_vdev_problem(child[c], func))
				return (B_TRUE);
	}

	return (B_FALSE);
}

/*
 * Active pool health status.
 *
 * To determine the status for a pool, we make several passes over the config,
 * picking the most egregious error we find.  In order of importance, we do the
 * following:
 *
 *	- Check for a complete and valid configuration
 *	- Look for any faulted or missing devices in a non-replicated config
 *	- Check for any data errors
 *	- Check for any faulted or missing devices in a replicated config
 *	- Look for any devices showing errors
 *	- Check for any resilvering devices
 *
 * There can obviously be multiple errors within a single pool, so this routine
 * only picks the most damaging of all the current errors to report.
 */
static zpool_status_t
check_status(nvlist_t *config, boolean_t isimport, zpool_errata_t *erratap)
{
	nvlist_t *nvroot;
	vdev_stat_t *vs;
	pool_scan_stat_t *ps = NULL;
	uint_t vsc, psc;
	uint64_t nerr;
	uint64_t version;
	uint64_t stateval;
	uint64_t suspended;
	uint64_t hostid = 0;
	uint64_t errata = 0;
	unsigned long system_hostid = get_system_hostid();

	verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_VERSION,
	    &version) == 0);
	verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) == 0);
	verify(nvlist_lookup_uint64_array(nvroot, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&vs, &vsc) == 0);
	verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_STATE,
	    &stateval) == 0);

	/*
	 * Currently resilvering a vdev
	 */
	(void) nvlist_lookup_uint64_array(nvroot, ZPOOL_CONFIG_SCAN_STATS,
	    (uint64_t **)&ps, &psc);
	if (ps != NULL && ps->pss_func == POOL_SCAN_RESILVER &&
	    ps->pss_state == DSS_SCANNING)
		return (ZPOOL_STATUS_RESILVERING);

	/*
	 * The multihost property is set and the pool may be active.
	 */
	if (vs->vs_state == VDEV_STATE_CANT_OPEN &&
	    vs->vs_aux == VDEV_AUX_ACTIVE) {
		mmp_state_t mmp_state;
		nvlist_t *nvinfo;

		nvinfo = fnvlist_lookup_nvlist(config, ZPOOL_CONFIG_LOAD_INFO);
		mmp_state = fnvlist_lookup_uint64(nvinfo,
		    ZPOOL_CONFIG_MMP_STATE);

		if (mmp_state == MMP_STATE_ACTIVE)
			return (ZPOOL_STATUS_HOSTID_ACTIVE);
		else if (mmp_state == MMP_STATE_NO_HOSTID)
			return (ZPOOL_STATUS_HOSTID_REQUIRED);
		else
			return (ZPOOL_STATUS_HOSTID_MISMATCH);
	}

	/*
	 * Pool last accessed by another system.
	 */
	(void) nvlist_lookup_uint64(config, ZPOOL_CONFIG_HOSTID, &hostid);
	if (hostid != 0 && (unsigned long)hostid != system_hostid &&
	    stateval == POOL_STATE_ACTIVE)
		return (ZPOOL_STATUS_HOSTID_MISMATCH);

	/*
	 * Newer on-disk version.
	 */
	if (vs->vs_state == VDEV_STATE_CANT_OPEN &&
	    vs->vs_aux == VDEV_AUX_VERSION_NEWER)
		return (ZPOOL_STATUS_VERSION_NEWER);

	/*
	 * Unsupported feature(s).
	 */
	if (vs->vs_state == VDEV_STATE_CANT_OPEN &&
	    vs->vs_aux == VDEV_AUX_UNSUP_FEAT) {
		nvlist_t *nvinfo;

		verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_LOAD_INFO,
		    &nvinfo) == 0);
		if (nvlist_exists(nvinfo, ZPOOL_CONFIG_CAN_RDONLY))
			return (ZPOOL_STATUS_UNSUP_FEAT_WRITE);
		return (ZPOOL_STATUS_UNSUP_FEAT_READ);
	}

	/*
	 * Check that the config is complete.
	 */
	if (vs->vs_state == VDEV_STATE_CANT_OPEN &&
	    vs->vs_aux == VDEV_AUX_BAD_GUID_SUM)
		return (ZPOOL_STATUS_BAD_GUID_SUM);

	/*
	 * Check whether the pool has suspended.
	 */
	if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_SUSPENDED,
	    &suspended) == 0) {
		uint64_t reason;

		if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_SUSPENDED_REASON,
		    &reason) == 0 && reason == ZIO_SUSPEND_MMP)
			return (ZPOOL_STATUS_IO_FAILURE_MMP);

		if (suspended == ZIO_FAILURE_MODE_CONTINUE)
			return (ZPOOL_STATUS_IO_FAILURE_CONTINUE);
		return (ZPOOL_STATUS_IO_FAILURE_WAIT);
	}

	/*
	 * Could not read a log.
	 */
	if (vs->vs_state == VDEV_STATE_CANT_OPEN &&
	    vs->vs_aux == VDEV_AUX_BAD_LOG) {
		return (ZPOOL_STATUS_BAD_LOG);
	}

	/*
	 * Bad devices in non-replicated config.
	 */
	if (vs->vs_state == VDEV_STATE_CANT_OPEN &&
	    find_vdev_problem(nvroot, vdev_faulted))
		return (ZPOOL_STATUS_FAULTED_DEV_NR);

	if (vs->vs_state == VDEV_STATE_CANT_OPEN &&
	    find_vdev_problem(nvroot, vdev_missing))
		return (ZPOOL_STATUS_MISSING_DEV_NR);

	if (vs->vs_state == VDEV_STATE_CANT_OPEN &&
	    find_vdev_problem(nvroot, vdev_broken))
		return (ZPOOL_STATUS_CORRUPT_LABEL_NR);

	/*
	 * Corrupted pool metadata
	 */
	if (vs->vs_state == VDEV_STATE_CANT_OPEN &&
	    vs->vs_aux == VDEV_AUX_CORRUPT_DATA)
		return (ZPOOL_STATUS_CORRUPT_POOL);

	/*
	 * Persistent data errors.
	 */
	if (!isimport) {
		if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_ERRCOUNT,
		    &nerr) == 0 && nerr != 0)
			return (ZPOOL_STATUS_CORRUPT_DATA);
	}

	/*
	 * Missing devices in a replicated config.
	 */
	if (find_vdev_problem(nvroot, vdev_faulted))
		return (ZPOOL_STATUS_FAULTED_DEV_R);
	if (find_vdev_problem(nvroot, vdev_missing))
		return (ZPOOL_STATUS_MISSING_DEV_R);
	if (find_vdev_problem(nvroot, vdev_broken))
		return (ZPOOL_STATUS_CORRUPT_LABEL_R);

	/*
	 * Devices with errors
	 */
	if (!isimport && find_vdev_problem(nvroot, vdev_errors))
		return (ZPOOL_STATUS_FAILING_DEV);

	/*
	 * Offlined devices
	 */
	if (find_vdev_problem(nvroot, vdev_offlined))
		return (ZPOOL_STATUS_OFFLINE_DEV);

	/*
	 * Removed device
	 */
	if (find_vdev_problem(nvroot, vdev_removed))
		return (ZPOOL_STATUS_REMOVED_DEV);

	/*
	 * Informational errata available.
	 */
	(void) nvlist_lookup_uint64(config, ZPOOL_CONFIG_ERRATA, &errata);
	if (errata) {
		*erratap = errata;
		return (ZPOOL_STATUS_ERRATA);
	}

	/*
	 * Outdated, but usable, version
	 */
	if (SPA_VERSION_IS_SUPPORTED(version) && version != SPA_VERSION)
		return (ZPOOL_STATUS_VERSION_OLDER);

	/*
	 * Usable pool with disabled features
	 */
	if (version >= SPA_VERSION_FEATURES) {
		int i;
		nvlist_t *feat;

		if (isimport) {
			feat = fnvlist_lookup_nvlist(config,
			    ZPOOL_CONFIG_LOAD_INFO);
			if (nvlist_exists(feat, ZPOOL_CONFIG_ENABLED_FEAT))
				feat = fnvlist_lookup_nvlist(feat,
				    ZPOOL_CONFIG_ENABLED_FEAT);
		} else {
			feat = fnvlist_lookup_nvlist(config,
			    ZPOOL_CONFIG_FEATURE_STATS);
		}

		for (i = 0; i < SPA_FEATURES; i++) {
			zfeature_info_t *fi = &spa_feature_table[i];
			if (!nvlist_exists(feat, fi->fi_guid))
				return (ZPOOL_STATUS_FEAT_DISABLED);
		}
	}

	return (ZPOOL_STATUS_OK);
}

zpool_status_t
zpool_get_status(zpool_handle_t *zhp, char **msgid, zpool_errata_t *errata)
{
	zpool_status_t ret = check_status(zhp->zpool_config, B_FALSE, errata);
	if (msgid != NULL) {
		if (ret >= NMSGID)
			*msgid = NULL;
		else
			*msgid = zfs_msgid_table[ret];
	}
	return (ret);
}

zpool_status_t
zpool_import_status(nvlist_t *config, char **msgid, zpool_errata_t *errata)
{
	zpool_status_t ret = check_status(config, B_TRUE, errata);

	if (ret >= NMSGID)
		*msgid = NULL;
	else
		*msgid = zfs_msgid_table[ret];

	return (ret);
}
