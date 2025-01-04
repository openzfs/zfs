// SPDX-License-Identifier: CDDL-1.0
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
 * Copyright (c) 2006, 2010, Oracle and/or its affiliates. All rights reserved.
 *
 * Copyright (c) 2016, Intel Corporation.
 * Copyright (c) 2018, loli10K <ezomori.nozomu@gmail.com>
 */

/*
 * The ZFS retire agent is responsible for managing hot spares across all pools.
 * When we see a device fault or a device removal, we try to open the associated
 * pool and look for any hot spares.  We iterate over any available hot spares
 * and attempt a 'zpool replace' for each one.
 *
 * For vdevs diagnosed as faulty, the agent is also responsible for proactively
 * marking the vdev FAULTY (for I/O errors) or DEGRADED (for checksum errors).
 */

#include <sys/fs/zfs.h>
#include <sys/fm/protocol.h>
#include <sys/fm/fs/zfs.h>
#include <libzutil.h>
#include <libzfs.h>
#include <string.h>
#include <libgen.h>

#include "zfs_agents.h"
#include "fmd_api.h"


typedef struct zfs_retire_repaired {
	struct zfs_retire_repaired	*zrr_next;
	uint64_t			zrr_pool;
	uint64_t			zrr_vdev;
} zfs_retire_repaired_t;

typedef struct zfs_retire_data {
	libzfs_handle_t			*zrd_hdl;
	zfs_retire_repaired_t		*zrd_repaired;
} zfs_retire_data_t;

static void
zfs_retire_clear_data(fmd_hdl_t *hdl, zfs_retire_data_t *zdp)
{
	zfs_retire_repaired_t *zrp;

	while ((zrp = zdp->zrd_repaired) != NULL) {
		zdp->zrd_repaired = zrp->zrr_next;
		fmd_hdl_free(hdl, zrp, sizeof (zfs_retire_repaired_t));
	}
}

/*
 * Find a pool with a matching GUID.
 */
typedef struct find_cbdata {
	uint64_t	cb_guid;
	zpool_handle_t	*cb_zhp;
	nvlist_t	*cb_vdev;
	uint64_t	cb_vdev_guid;
	uint64_t	cb_num_spares;
} find_cbdata_t;

static int
find_pool(zpool_handle_t *zhp, void *data)
{
	find_cbdata_t *cbp = data;

	if (cbp->cb_guid ==
	    zpool_get_prop_int(zhp, ZPOOL_PROP_GUID, NULL)) {
		cbp->cb_zhp = zhp;
		return (1);
	}

	zpool_close(zhp);
	return (0);
}

/*
 * Find a vdev within a tree with a matching GUID.
 */
static nvlist_t *
find_vdev(libzfs_handle_t *zhdl, nvlist_t *nv, uint64_t search_guid)
{
	uint64_t guid;
	nvlist_t **child;
	uint_t c, children;
	nvlist_t *ret;

	if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_GUID, &guid) == 0 &&
	    guid == search_guid) {
		fmd_hdl_debug(fmd_module_hdl("zfs-retire"),
		    "matched vdev %llu", guid);
		return (nv);
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0)
		return (NULL);

	for (c = 0; c < children; c++) {
		if ((ret = find_vdev(zhdl, child[c], search_guid)) != NULL)
			return (ret);
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_L2CACHE,
	    &child, &children) != 0)
		return (NULL);

	for (c = 0; c < children; c++) {
		if ((ret = find_vdev(zhdl, child[c], search_guid)) != NULL)
			return (ret);
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_SPARES,
	    &child, &children) != 0)
		return (NULL);

	for (c = 0; c < children; c++) {
		if ((ret = find_vdev(zhdl, child[c], search_guid)) != NULL)
			return (ret);
	}

	return (NULL);
}

static int
remove_spares(zpool_handle_t *zhp, void *data)
{
	nvlist_t *config, *nvroot;
	nvlist_t **spares;
	uint_t nspares;
	char *devname;
	find_cbdata_t *cbp = data;
	uint64_t spareguid = 0;
	vdev_stat_t *vs;
	unsigned int c;

	config = zpool_get_config(zhp, NULL);
	if (nvlist_lookup_nvlist(config,
	    ZPOOL_CONFIG_VDEV_TREE, &nvroot) != 0) {
		zpool_close(zhp);
		return (0);
	}

	if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_SPARES,
	    &spares, &nspares) != 0) {
		zpool_close(zhp);
		return (0);
	}

	for (int i = 0; i < nspares; i++) {
		if (nvlist_lookup_uint64(spares[i], ZPOOL_CONFIG_GUID,
		    &spareguid) == 0 && spareguid == cbp->cb_vdev_guid) {
			devname = zpool_vdev_name(NULL, zhp, spares[i],
			    B_FALSE);
			nvlist_lookup_uint64_array(spares[i],
			    ZPOOL_CONFIG_VDEV_STATS, (uint64_t **)&vs, &c);
			if (vs->vs_state != VDEV_STATE_REMOVED &&
			    zpool_vdev_remove_wanted(zhp, devname) == 0)
				cbp->cb_num_spares++;
			break;
		}
	}

	zpool_close(zhp);
	return (0);
}

/*
 * Given a vdev guid, find and remove all spares associated with it.
 */
static int
find_and_remove_spares(libzfs_handle_t *zhdl, uint64_t vdev_guid)
{
	find_cbdata_t cb;

	cb.cb_num_spares = 0;
	cb.cb_vdev_guid = vdev_guid;
	zpool_iter(zhdl, remove_spares, &cb);

	return (cb.cb_num_spares);
}

/*
 * Given a (pool, vdev) GUID pair, find the matching pool and vdev.
 */
static zpool_handle_t *
find_by_guid(libzfs_handle_t *zhdl, uint64_t pool_guid, uint64_t vdev_guid,
    nvlist_t **vdevp)
{
	find_cbdata_t cb;
	zpool_handle_t *zhp;
	nvlist_t *config, *nvroot;

	/*
	 * Find the corresponding pool and make sure the vdev still exists.
	 */
	cb.cb_guid = pool_guid;
	if (zpool_iter(zhdl, find_pool, &cb) != 1)
		return (NULL);

	zhp = cb.cb_zhp;
	config = zpool_get_config(zhp, NULL);
	if (nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) != 0) {
		zpool_close(zhp);
		return (NULL);
	}

	if (vdev_guid != 0) {
		if ((*vdevp = find_vdev(zhdl, nvroot, vdev_guid)) == NULL) {
			zpool_close(zhp);
			return (NULL);
		}
	}

	return (zhp);
}

/*
 * Given a vdev, attempt to replace it with every known spare until one
 * succeeds or we run out of devices to try.
 * Return whether we were successful or not in replacing the device.
 */
static boolean_t
replace_with_spare(fmd_hdl_t *hdl, zpool_handle_t *zhp, nvlist_t *vdev)
{
	nvlist_t *config, *nvroot, *replacement;
	nvlist_t **spares;
	uint_t s, nspares;
	char *dev_name;
	zprop_source_t source;
	int ashift;

	config = zpool_get_config(zhp, NULL);
	if (nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) != 0)
		return (B_FALSE);

	/*
	 * Find out if there are any hot spares available in the pool.
	 */
	if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_SPARES,
	    &spares, &nspares) != 0)
		return (B_FALSE);

	/*
	 * lookup "ashift" pool property, we may need it for the replacement
	 */
	ashift = zpool_get_prop_int(zhp, ZPOOL_PROP_ASHIFT, &source);

	replacement = fmd_nvl_alloc(hdl, FMD_SLEEP);

	(void) nvlist_add_string(replacement, ZPOOL_CONFIG_TYPE,
	    VDEV_TYPE_ROOT);

	dev_name = zpool_vdev_name(NULL, zhp, vdev, B_FALSE);

	/*
	 * Try to replace each spare, ending when we successfully
	 * replace it.
	 */
	for (s = 0; s < nspares; s++) {
		boolean_t rebuild = B_FALSE;
		const char *spare_name, *type;

		if (nvlist_lookup_string(spares[s], ZPOOL_CONFIG_PATH,
		    &spare_name) != 0)
			continue;

		/* prefer sequential resilvering for distributed spares */
		if ((nvlist_lookup_string(spares[s], ZPOOL_CONFIG_TYPE,
		    &type) == 0) && strcmp(type, VDEV_TYPE_DRAID_SPARE) == 0)
			rebuild = B_TRUE;

		/* if set, add the "ashift" pool property to the spare nvlist */
		if (source != ZPROP_SRC_DEFAULT)
			(void) nvlist_add_uint64(spares[s],
			    ZPOOL_CONFIG_ASHIFT, ashift);

		(void) nvlist_add_nvlist_array(replacement,
		    ZPOOL_CONFIG_CHILDREN, (const nvlist_t **)&spares[s], 1);

		fmd_hdl_debug(hdl, "zpool_vdev_replace '%s' with spare '%s'",
		    dev_name, zfs_basename(spare_name));

		if (zpool_vdev_attach(zhp, dev_name, spare_name,
		    replacement, B_TRUE, rebuild) == 0) {
			free(dev_name);
			nvlist_free(replacement);
			return (B_TRUE);
		}
	}

	free(dev_name);
	nvlist_free(replacement);

	return (B_FALSE);
}

/*
 * Repair this vdev if we had diagnosed a 'fault.fs.zfs.device' and
 * ASRU is now usable.  ZFS has found the device to be present and
 * functioning.
 */
static void
zfs_vdev_repair(fmd_hdl_t *hdl, nvlist_t *nvl)
{
	zfs_retire_data_t *zdp = fmd_hdl_getspecific(hdl);
	zfs_retire_repaired_t *zrp;
	uint64_t pool_guid, vdev_guid;
	if (nvlist_lookup_uint64(nvl, FM_EREPORT_PAYLOAD_ZFS_POOL_GUID,
	    &pool_guid) != 0 || nvlist_lookup_uint64(nvl,
	    FM_EREPORT_PAYLOAD_ZFS_VDEV_GUID, &vdev_guid) != 0)
		return;

	/*
	 * Before checking the state of the ASRU, go through and see if we've
	 * already made an attempt to repair this ASRU.  This list is cleared
	 * whenever we receive any kind of list event, and is designed to
	 * prevent us from generating a feedback loop when we attempt repairs
	 * against a faulted pool.  The problem is that checking the unusable
	 * state of the ASRU can involve opening the pool, which can post
	 * statechange events but otherwise leave the pool in the faulted
	 * state.  This list allows us to detect when a statechange event is
	 * due to our own request.
	 */
	for (zrp = zdp->zrd_repaired; zrp != NULL; zrp = zrp->zrr_next) {
		if (zrp->zrr_pool == pool_guid &&
		    zrp->zrr_vdev == vdev_guid)
			return;
	}

	zrp = fmd_hdl_alloc(hdl, sizeof (zfs_retire_repaired_t), FMD_SLEEP);
	zrp->zrr_next = zdp->zrd_repaired;
	zrp->zrr_pool = pool_guid;
	zrp->zrr_vdev = vdev_guid;
	zdp->zrd_repaired = zrp;

	fmd_hdl_debug(hdl, "marking repaired vdev %llu on pool %llu",
	    vdev_guid, pool_guid);
}

static void
zfs_retire_recv(fmd_hdl_t *hdl, fmd_event_t *ep, nvlist_t *nvl,
    const char *class)
{
	(void) ep;
	uint64_t pool_guid, vdev_guid;
	zpool_handle_t *zhp;
	nvlist_t *resource, *fault;
	nvlist_t **faults;
	uint_t f, nfaults;
	zfs_retire_data_t *zdp = fmd_hdl_getspecific(hdl);
	libzfs_handle_t *zhdl = zdp->zrd_hdl;
	boolean_t fault_device, degrade_device;
	boolean_t is_repair;
	boolean_t l2arc = B_FALSE;
	boolean_t spare = B_FALSE;
	const char *scheme;
	nvlist_t *vdev = NULL;
	const char *uuid;
	int repair_done = 0;
	boolean_t retire;
	boolean_t is_disk;
	vdev_aux_t aux;
	uint64_t state = 0;
	vdev_stat_t *vs;
	unsigned int c;

	fmd_hdl_debug(hdl, "zfs_retire_recv: '%s'", class);

	(void) nvlist_lookup_uint64(nvl, FM_EREPORT_PAYLOAD_ZFS_VDEV_STATE,
	    &state);

	/*
	 * If this is a resource notifying us of device removal then simply
	 * check for an available spare and continue unless the device is a
	 * l2arc vdev, in which case we just offline it.
	 */
	if (strcmp(class, "resource.fs.zfs.removed") == 0 ||
	    (strcmp(class, "resource.fs.zfs.statechange") == 0 &&
	    (state == VDEV_STATE_REMOVED || state == VDEV_STATE_FAULTED))) {
		const char *devtype;
		char *devname;

		if (nvlist_lookup_string(nvl, FM_EREPORT_PAYLOAD_ZFS_VDEV_TYPE,
		    &devtype) == 0) {
			if (strcmp(devtype, VDEV_TYPE_SPARE) == 0)
				spare = B_TRUE;
			else if (strcmp(devtype, VDEV_TYPE_L2CACHE) == 0)
				l2arc = B_TRUE;
		}

		if (nvlist_lookup_uint64(nvl,
		    FM_EREPORT_PAYLOAD_ZFS_VDEV_GUID, &vdev_guid) != 0)
			return;

		if (vdev_guid == 0) {
			fmd_hdl_debug(hdl, "Got a zero GUID");
			return;
		}

		if (spare) {
			int nspares = find_and_remove_spares(zhdl, vdev_guid);
			fmd_hdl_debug(hdl, "%d spares removed", nspares);
			return;
		}

		if (nvlist_lookup_uint64(nvl, FM_EREPORT_PAYLOAD_ZFS_POOL_GUID,
		    &pool_guid) != 0)
			return;

		if ((zhp = find_by_guid(zhdl, pool_guid, vdev_guid,
		    &vdev)) == NULL)
			return;

		devname = zpool_vdev_name(NULL, zhp, vdev, B_FALSE);

		nvlist_lookup_uint64_array(vdev, ZPOOL_CONFIG_VDEV_STATS,
		    (uint64_t **)&vs, &c);

		/*
		 * If state removed is requested for already removed vdev,
		 * its a loopback event from spa_async_remove(). Just
		 * ignore it.
		 */
		if ((vs->vs_state == VDEV_STATE_REMOVED && state ==
		    VDEV_STATE_REMOVED) || vs->vs_state == VDEV_STATE_OFFLINE)
			return;

		/* Remove the vdev since device is unplugged */
		int remove_status = 0;
		if (l2arc || (strcmp(class, "resource.fs.zfs.removed") == 0)) {
			remove_status = zpool_vdev_remove_wanted(zhp, devname);
			fmd_hdl_debug(hdl, "zpool_vdev_remove_wanted '%s'"
			    ", err:%d", devname, libzfs_errno(zhdl));
		}

		/* Replace the vdev with a spare if its not a l2arc */
		if (!l2arc && !remove_status &&
		    (!fmd_prop_get_int32(hdl, "spare_on_remove") ||
		    replace_with_spare(hdl, zhp, vdev) == B_FALSE)) {
			/* Could not handle with spare */
			fmd_hdl_debug(hdl, "no spare for '%s'", devname);
		}

		free(devname);
		zpool_close(zhp);
		return;
	}

	if (strcmp(class, FM_LIST_RESOLVED_CLASS) == 0)
		return;

	/*
	 * Note: on Linux statechange events are more than just
	 * healthy ones so we need to confirm the actual state value.
	 */
	if (strcmp(class, "resource.fs.zfs.statechange") == 0 &&
	    state == VDEV_STATE_HEALTHY) {
		zfs_vdev_repair(hdl, nvl);
		return;
	}
	if (strcmp(class, "sysevent.fs.zfs.vdev_remove") == 0) {
		zfs_vdev_repair(hdl, nvl);
		return;
	}

	zfs_retire_clear_data(hdl, zdp);

	if (strcmp(class, FM_LIST_REPAIRED_CLASS) == 0)
		is_repair = B_TRUE;
	else
		is_repair = B_FALSE;

	/*
	 * We subscribe to zfs faults as well as all repair events.
	 */
	if (nvlist_lookup_nvlist_array(nvl, FM_SUSPECT_FAULT_LIST,
	    &faults, &nfaults) != 0)
		return;

	for (f = 0; f < nfaults; f++) {
		fault = faults[f];

		fault_device = B_FALSE;
		degrade_device = B_FALSE;
		is_disk = B_FALSE;

		if (nvlist_lookup_boolean_value(fault, FM_SUSPECT_RETIRE,
		    &retire) == 0 && retire == 0)
			continue;

		/*
		 * While we subscribe to fault.fs.zfs.*, we only take action
		 * for faults targeting a specific vdev (open failure or SERD
		 * failure).  We also subscribe to fault.io.* events, so that
		 * faulty disks will be faulted in the ZFS configuration.
		 */
		if (fmd_nvl_class_match(hdl, fault, "fault.fs.zfs.vdev.io")) {
			fault_device = B_TRUE;
		} else if (fmd_nvl_class_match(hdl, fault,
		    "fault.fs.zfs.vdev.checksum")) {
			degrade_device = B_TRUE;
		} else if (fmd_nvl_class_match(hdl, fault,
		    "fault.fs.zfs.vdev.slow_io")) {
			degrade_device = B_TRUE;
		} else if (fmd_nvl_class_match(hdl, fault,
		    "fault.fs.zfs.device")) {
			fault_device = B_FALSE;
		} else if (fmd_nvl_class_match(hdl, fault, "fault.io.*")) {
			is_disk = B_TRUE;
			fault_device = B_TRUE;
		} else {
			continue;
		}

		if (is_disk) {
			continue;
		} else {
			/*
			 * This is a ZFS fault.  Lookup the resource, and
			 * attempt to find the matching vdev.
			 */
			if (nvlist_lookup_nvlist(fault, FM_FAULT_RESOURCE,
			    &resource) != 0 ||
			    nvlist_lookup_string(resource, FM_FMRI_SCHEME,
			    &scheme) != 0)
				continue;

			if (strcmp(scheme, FM_FMRI_SCHEME_ZFS) != 0)
				continue;

			if (nvlist_lookup_uint64(resource, FM_FMRI_ZFS_POOL,
			    &pool_guid) != 0)
				continue;

			if (nvlist_lookup_uint64(resource, FM_FMRI_ZFS_VDEV,
			    &vdev_guid) != 0) {
				if (is_repair)
					vdev_guid = 0;
				else
					continue;
			}

			if ((zhp = find_by_guid(zhdl, pool_guid, vdev_guid,
			    &vdev)) == NULL)
				continue;

			aux = VDEV_AUX_ERR_EXCEEDED;
		}

		if (vdev_guid == 0) {
			/*
			 * For pool-level repair events, clear the entire pool.
			 */
			fmd_hdl_debug(hdl, "zpool_clear of pool '%s'",
			    zpool_get_name(zhp));
			(void) zpool_clear(zhp, NULL, NULL);
			zpool_close(zhp);
			continue;
		}

		/*
		 * If this is a repair event, then mark the vdev as repaired and
		 * continue.
		 */
		if (is_repair) {
			repair_done = 1;
			fmd_hdl_debug(hdl, "zpool_clear of pool '%s' vdev %llu",
			    zpool_get_name(zhp), vdev_guid);
			(void) zpool_vdev_clear(zhp, vdev_guid);
			zpool_close(zhp);
			continue;
		}

		/*
		 * Actively fault the device if needed.
		 */
		if (fault_device)
			(void) zpool_vdev_fault(zhp, vdev_guid, aux);
		if (degrade_device)
			(void) zpool_vdev_degrade(zhp, vdev_guid, aux);

		if (fault_device || degrade_device)
			fmd_hdl_debug(hdl, "zpool_vdev_%s: vdev %llu on '%s'",
			    fault_device ? "fault" : "degrade", vdev_guid,
			    zpool_get_name(zhp));

		/*
		 * Attempt to substitute a hot spare.
		 */
		(void) replace_with_spare(hdl, zhp, vdev);

		zpool_close(zhp);
	}

	if (strcmp(class, FM_LIST_REPAIRED_CLASS) == 0 && repair_done &&
	    nvlist_lookup_string(nvl, FM_SUSPECT_UUID, &uuid) == 0)
		fmd_case_uuresolved(hdl, uuid);
}

static const fmd_hdl_ops_t fmd_ops = {
	zfs_retire_recv,	/* fmdo_recv */
	NULL,			/* fmdo_timeout */
	NULL,			/* fmdo_close */
	NULL,			/* fmdo_stats */
	NULL,			/* fmdo_gc */
};

static const fmd_prop_t fmd_props[] = {
	{ "spare_on_remove", FMD_TYPE_BOOL, "true" },
	{ NULL, 0, NULL }
};

static const fmd_hdl_info_t fmd_info = {
	"ZFS Retire Agent", "1.0", &fmd_ops, fmd_props
};

void
_zfs_retire_init(fmd_hdl_t *hdl)
{
	zfs_retire_data_t *zdp;
	libzfs_handle_t *zhdl;

	if ((zhdl = libzfs_init()) == NULL)
		return;

	if (fmd_hdl_register(hdl, FMD_API_VERSION, &fmd_info) != 0) {
		libzfs_fini(zhdl);
		return;
	}

	zdp = fmd_hdl_zalloc(hdl, sizeof (zfs_retire_data_t), FMD_SLEEP);
	zdp->zrd_hdl = zhdl;

	fmd_hdl_setspecific(hdl, zdp);
}

void
_zfs_retire_fini(fmd_hdl_t *hdl)
{
	zfs_retire_data_t *zdp = fmd_hdl_getspecific(hdl);

	if (zdp != NULL) {
		zfs_retire_clear_data(hdl, zdp);
		libzfs_fini(zdp->zrd_hdl);
		fmd_hdl_free(hdl, zdp, sizeof (zfs_retire_data_t));
	}
}
