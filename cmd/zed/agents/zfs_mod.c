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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012 by Delphix. All rights reserved.
 * Copyright 2014 Nexenta Systems, Inc. All rights reserved.
 * Copyright (c) 2016, 2017, Intel Corporation.
 * Copyright (c) 2017 Open-E, Inc. All Rights Reserved.
 */

/*
 * ZFS syseventd module.
 *
 * file origin: openzfs/usr/src/cmd/syseventd/modules/zfs_mod/zfs_mod.c
 *
 * The purpose of this module is to identify when devices are added to the
 * system, and appropriately online or replace the affected vdevs.
 *
 * When a device is added to the system:
 *
 * 	1. Search for any vdevs whose devid matches that of the newly added
 *	   device.
 *
 * 	2. If no vdevs are found, then search for any vdevs whose udev path
 *	   matches that of the new device.
 *
 *	3. If no vdevs match by either method, then ignore the event.
 *
 * 	4. Attempt to online the device with a flag to indicate that it should
 *	   be unspared when resilvering completes.  If this succeeds, then the
 *	   same device was inserted and we should continue normally.
 *
 *	5. If the pool does not have the 'autoreplace' property set, attempt to
 *	   online the device again without the unspare flag, which will
 *	   generate a FMA fault.
 *
 *	6. If the pool has the 'autoreplace' property set, and the matching vdev
 *	   is a whole disk, then label the new disk and attempt a 'zpool
 *	   replace'.
 *
 * The module responds to EC_DEV_ADD events.  The special ESC_ZFS_VDEV_CHECK
 * event indicates that a device failed to open during pool load, but the
 * autoreplace property was set.  In this case, we deferred the associated
 * FMA fault until our module had a chance to process the autoreplace logic.
 * If the device could not be replaced, then the second online attempt will
 * trigger the FMA fault that we skipped earlier.
 *
 * ZFS on Linux porting notes:
 *	Linux udev provides a disk insert for both the disk and the partition
 *
 */

#include <ctype.h>
#include <devid.h>
#include <fcntl.h>
#include <libnvpair.h>
#include <libzfs.h>
#include <libzutil.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/list.h>
#include <sys/sunddi.h>
#include <sys/sysevent/eventdefs.h>
#include <sys/sysevent/dev.h>
#include <thread_pool.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include "zfs_agents.h"
#include "../zed_log.h"

#define	DEV_BYID_PATH	"/dev/disk/by-id/"
#define	DEV_BYPATH_PATH	"/dev/disk/by-path/"
#define	DEV_BYVDEV_PATH	"/dev/disk/by-vdev/"

typedef void (*zfs_process_func_t)(zpool_handle_t *, nvlist_t *, boolean_t);

libzfs_handle_t *g_zfshdl;
list_t g_pool_list;	/* list of unavailable pools at initialization */
list_t g_device_list;	/* list of disks with asynchronous label request */
tpool_t *g_tpool;
boolean_t g_enumeration_done;
pthread_t g_zfs_tid;	/* zfs_enum_pools() thread */

typedef struct unavailpool {
	zpool_handle_t	*uap_zhp;
	list_node_t	uap_node;
} unavailpool_t;

typedef struct pendingdev {
	char		pd_physpath[128];
	list_node_t	pd_node;
} pendingdev_t;

static int
zfs_toplevel_state(zpool_handle_t *zhp)
{
	nvlist_t *nvroot;
	vdev_stat_t *vs;
	unsigned int c;

	verify(nvlist_lookup_nvlist(zpool_get_config(zhp, NULL),
	    ZPOOL_CONFIG_VDEV_TREE, &nvroot) == 0);
	verify(nvlist_lookup_uint64_array(nvroot, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&vs, &c) == 0);
	return (vs->vs_state);
}

static int
zfs_unavail_pool(zpool_handle_t *zhp, void *data)
{
	zed_log_msg(LOG_INFO, "zfs_unavail_pool: examining '%s' (state %d)",
	    zpool_get_name(zhp), (int)zfs_toplevel_state(zhp));

	if (zfs_toplevel_state(zhp) < VDEV_STATE_DEGRADED) {
		unavailpool_t *uap;
		uap = malloc(sizeof (unavailpool_t));
		uap->uap_zhp = zhp;
		list_insert_tail((list_t *)data, uap);
	} else {
		zpool_close(zhp);
	}
	return (0);
}

/*
 * Two stage replace on Linux
 * since we get disk notifications
 * we can wait for partitioned disk slice to show up!
 *
 * First stage tags the disk, initiates async partitioning, and returns
 * Second stage finds the tag and proceeds to ZFS labeling/replace
 *
 * disk-add --> label-disk + tag-disk --> partition-add --> zpool_vdev_attach
 *
 * 1. physical match with no fs, no partition
 *	tag it top, partition disk
 *
 * 2. physical match again, see partition and tag
 *
 */

/*
 * The device associated with the given vdev (either by devid or physical path)
 * has been added to the system.  If 'isdisk' is set, then we only attempt a
 * replacement if it's a whole disk.  This also implies that we should label the
 * disk first.
 *
 * First, we attempt to online the device (making sure to undo any spare
 * operation when finished).  If this succeeds, then we're done.  If it fails,
 * and the new state is VDEV_CANT_OPEN, it indicates that the device was opened,
 * but that the label was not what we expected.  If the 'autoreplace' property
 * is enabled, then we relabel the disk (if specified), and attempt a 'zpool
 * replace'.  If the online is successful, but the new state is something else
 * (REMOVED or FAULTED), it indicates that we're out of sync or in some sort of
 * race, and we should avoid attempting to relabel the disk.
 *
 * Also can arrive here from a ESC_ZFS_VDEV_CHECK event
 */
static void
zfs_process_add(zpool_handle_t *zhp, nvlist_t *vdev, boolean_t labeled)
{
	char *path;
	vdev_state_t newstate;
	nvlist_t *nvroot, *newvd;
	pendingdev_t *device;
	uint64_t wholedisk = 0ULL;
	uint64_t offline = 0ULL;
	uint64_t guid = 0ULL;
	char *physpath = NULL, *new_devid = NULL, *enc_sysfs_path = NULL;
	char rawpath[PATH_MAX], fullpath[PATH_MAX];
	char devpath[PATH_MAX];
	int ret;
	int is_dm = 0;
	int is_sd = 0;
	uint_t c;
	vdev_stat_t *vs;

	if (nvlist_lookup_string(vdev, ZPOOL_CONFIG_PATH, &path) != 0)
		return;

	/* Skip healthy disks */
	verify(nvlist_lookup_uint64_array(vdev, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&vs, &c) == 0);
	if (vs->vs_state == VDEV_STATE_HEALTHY) {
		zed_log_msg(LOG_INFO, "%s: %s is already healthy, skip it.",
		    __func__, path);
		return;
	}

	(void) nvlist_lookup_string(vdev, ZPOOL_CONFIG_PHYS_PATH, &physpath);
	(void) nvlist_lookup_string(vdev, ZPOOL_CONFIG_VDEV_ENC_SYSFS_PATH,
	    &enc_sysfs_path);
	(void) nvlist_lookup_uint64(vdev, ZPOOL_CONFIG_WHOLE_DISK, &wholedisk);
	(void) nvlist_lookup_uint64(vdev, ZPOOL_CONFIG_OFFLINE, &offline);
	(void) nvlist_lookup_uint64(vdev, ZPOOL_CONFIG_GUID, &guid);

	if (offline)
		return;  /* don't intervene if it was taken offline */

	is_dm = zfs_dev_is_dm(path);
	zed_log_msg(LOG_INFO, "zfs_process_add: pool '%s' vdev '%s', phys '%s'"
	    " wholedisk %d, dm %d (%llu)", zpool_get_name(zhp), path,
	    physpath ? physpath : "NULL", wholedisk, is_dm,
	    (long long unsigned int)guid);

	/*
	 * The VDEV guid is preferred for identification (gets passed in path)
	 */
	if (guid != 0) {
		(void) snprintf(fullpath, sizeof (fullpath), "%llu",
		    (long long unsigned int)guid);
	} else {
		/*
		 * otherwise use path sans partition suffix for whole disks
		 */
		(void) strlcpy(fullpath, path, sizeof (fullpath));
		if (wholedisk) {
			char *spath = zfs_strip_partition(fullpath);
			if (!spath) {
				zed_log_msg(LOG_INFO, "%s: Can't alloc",
				    __func__);
				return;
			}

			(void) strlcpy(fullpath, spath, sizeof (fullpath));
			free(spath);
		}
	}

	/*
	 * Attempt to online the device.
	 */
	if (zpool_vdev_online(zhp, fullpath,
	    ZFS_ONLINE_CHECKREMOVE | ZFS_ONLINE_UNSPARE, &newstate) == 0 &&
	    (newstate == VDEV_STATE_HEALTHY ||
	    newstate == VDEV_STATE_DEGRADED)) {
		zed_log_msg(LOG_INFO, "  zpool_vdev_online: vdev %s is %s",
		    fullpath, (newstate == VDEV_STATE_HEALTHY) ?
		    "HEALTHY" : "DEGRADED");
		return;
	}

	/*
	 * vdev_id alias rule for using scsi_debug devices (FMA automated
	 * testing)
	 */
	if (physpath != NULL && strcmp("scsidebug", physpath) == 0)
		is_sd = 1;

	/*
	 * If the pool doesn't have the autoreplace property set, then use
	 * vdev online to trigger a FMA fault by posting an ereport.
	 */
	if (!zpool_get_prop_int(zhp, ZPOOL_PROP_AUTOREPLACE, NULL) ||
	    !(wholedisk || is_dm) || (physpath == NULL)) {
		(void) zpool_vdev_online(zhp, fullpath, ZFS_ONLINE_FORCEFAULT,
		    &newstate);
		zed_log_msg(LOG_INFO, "Pool's autoreplace is not enabled or "
		    "not a whole disk for '%s'", fullpath);
		return;
	}

	/*
	 * Convert physical path into its current device node.  Rawpath
	 * needs to be /dev/disk/by-vdev for a scsi_debug device since
	 * /dev/disk/by-path will not be present.
	 */
	(void) snprintf(rawpath, sizeof (rawpath), "%s%s",
	    is_sd ? DEV_BYVDEV_PATH : DEV_BYPATH_PATH, physpath);

	if (realpath(rawpath, devpath) == NULL && !is_dm) {
		zed_log_msg(LOG_INFO, "  realpath: %s failed (%s)",
		    rawpath, strerror(errno));

		(void) zpool_vdev_online(zhp, fullpath, ZFS_ONLINE_FORCEFAULT,
		    &newstate);

		zed_log_msg(LOG_INFO, "  zpool_vdev_online: %s FORCEFAULT (%s)",
		    fullpath, libzfs_error_description(g_zfshdl));
		return;
	}

	/* Only autoreplace bad disks */
	if ((vs->vs_state != VDEV_STATE_DEGRADED) &&
	    (vs->vs_state != VDEV_STATE_FAULTED) &&
	    (vs->vs_state != VDEV_STATE_CANT_OPEN)) {
		return;
	}

	nvlist_lookup_string(vdev, "new_devid", &new_devid);

	if (is_dm) {
		/* Don't label device mapper or multipath disks. */
	} else if (!labeled) {
		/*
		 * we're auto-replacing a raw disk, so label it first
		 */
		char *leafname;

		/*
		 * If this is a request to label a whole disk, then attempt to
		 * write out the label.  Before we can label the disk, we need
		 * to map the physical string that was matched on to the under
		 * lying device node.
		 *
		 * If any part of this process fails, then do a force online
		 * to trigger a ZFS fault for the device (and any hot spare
		 * replacement).
		 */
		leafname = strrchr(devpath, '/') + 1;

		/*
		 * If this is a request to label a whole disk, then attempt to
		 * write out the label.
		 */
		if (zpool_label_disk(g_zfshdl, zhp, leafname) != 0) {
			zed_log_msg(LOG_INFO, "  zpool_label_disk: could not "
			    "label '%s' (%s)", leafname,
			    libzfs_error_description(g_zfshdl));

			(void) zpool_vdev_online(zhp, fullpath,
			    ZFS_ONLINE_FORCEFAULT, &newstate);
			return;
		}

		/*
		 * The disk labeling is asynchronous on Linux. Just record
		 * this label request and return as there will be another
		 * disk add event for the partition after the labeling is
		 * completed.
		 */
		device = malloc(sizeof (pendingdev_t));
		(void) strlcpy(device->pd_physpath, physpath,
		    sizeof (device->pd_physpath));
		list_insert_tail(&g_device_list, device);

		zed_log_msg(LOG_INFO, "  zpool_label_disk: async '%s' (%llu)",
		    leafname, (u_longlong_t)guid);

		return;	/* resumes at EC_DEV_ADD.ESC_DISK for partition */

	} else /* labeled */ {
		boolean_t found = B_FALSE;
		/*
		 * match up with request above to label the disk
		 */
		for (device = list_head(&g_device_list); device != NULL;
		    device = list_next(&g_device_list, device)) {
			if (strcmp(physpath, device->pd_physpath) == 0) {
				list_remove(&g_device_list, device);
				free(device);
				found = B_TRUE;
				break;
			}
			zed_log_msg(LOG_INFO, "zpool_label_disk: %s != %s",
			    physpath, device->pd_physpath);
		}
		if (!found) {
			/* unexpected partition slice encountered */
			zed_log_msg(LOG_INFO, "labeled disk %s unexpected here",
			    fullpath);
			(void) zpool_vdev_online(zhp, fullpath,
			    ZFS_ONLINE_FORCEFAULT, &newstate);
			return;
		}

		zed_log_msg(LOG_INFO, "  zpool_label_disk: resume '%s' (%llu)",
		    physpath, (u_longlong_t)guid);

		(void) snprintf(devpath, sizeof (devpath), "%s%s",
		    DEV_BYID_PATH, new_devid);
	}

	/*
	 * Construct the root vdev to pass to zpool_vdev_attach().  While adding
	 * the entire vdev structure is harmless, we construct a reduced set of
	 * path/physpath/wholedisk to keep it simple.
	 */
	if (nvlist_alloc(&nvroot, NV_UNIQUE_NAME, 0) != 0) {
		zed_log_msg(LOG_WARNING, "zfs_mod: nvlist_alloc out of memory");
		return;
	}
	if (nvlist_alloc(&newvd, NV_UNIQUE_NAME, 0) != 0) {
		zed_log_msg(LOG_WARNING, "zfs_mod: nvlist_alloc out of memory");
		nvlist_free(nvroot);
		return;
	}

	if (nvlist_add_string(newvd, ZPOOL_CONFIG_TYPE, VDEV_TYPE_DISK) != 0 ||
	    nvlist_add_string(newvd, ZPOOL_CONFIG_PATH, path) != 0 ||
	    nvlist_add_string(newvd, ZPOOL_CONFIG_DEVID, new_devid) != 0 ||
	    (physpath != NULL && nvlist_add_string(newvd,
	    ZPOOL_CONFIG_PHYS_PATH, physpath) != 0) ||
	    (enc_sysfs_path != NULL && nvlist_add_string(newvd,
	    ZPOOL_CONFIG_VDEV_ENC_SYSFS_PATH, enc_sysfs_path) != 0) ||
	    nvlist_add_uint64(newvd, ZPOOL_CONFIG_WHOLE_DISK, wholedisk) != 0 ||
	    nvlist_add_string(nvroot, ZPOOL_CONFIG_TYPE, VDEV_TYPE_ROOT) != 0 ||
	    nvlist_add_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN, &newvd,
	    1) != 0) {
		zed_log_msg(LOG_WARNING, "zfs_mod: unable to add nvlist pairs");
		nvlist_free(newvd);
		nvlist_free(nvroot);
		return;
	}

	nvlist_free(newvd);

	/*
	 * Wait for udev to verify the links exist, then auto-replace
	 * the leaf disk at same physical location.
	 */
	if (zpool_label_disk_wait(path, 3000) != 0) {
		zed_log_msg(LOG_WARNING, "zfs_mod: expected replacement "
		    "disk %s is missing", path);
		nvlist_free(nvroot);
		return;
	}

	ret = zpool_vdev_attach(zhp, fullpath, path, nvroot, B_TRUE);

	zed_log_msg(LOG_INFO, "  zpool_vdev_replace: %s with %s (%s)",
	    fullpath, path, (ret == 0) ? "no errors" :
	    libzfs_error_description(g_zfshdl));

	nvlist_free(nvroot);
}

/*
 * Utility functions to find a vdev matching given criteria.
 */
typedef struct dev_data {
	const char		*dd_compare;
	const char		*dd_prop;
	zfs_process_func_t	dd_func;
	boolean_t		dd_found;
	boolean_t		dd_islabeled;
	uint64_t		dd_pool_guid;
	uint64_t		dd_vdev_guid;
	const char		*dd_new_devid;
} dev_data_t;

static void
zfs_iter_vdev(zpool_handle_t *zhp, nvlist_t *nvl, void *data)
{
	dev_data_t *dp = data;
	char *path = NULL;
	uint_t c, children;
	nvlist_t **child;

	/*
	 * First iterate over any children.
	 */
	if (nvlist_lookup_nvlist_array(nvl, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) == 0) {
		for (c = 0; c < children; c++)
			zfs_iter_vdev(zhp, child[c], data);
	}

	/*
	 * Iterate over any spares and cache devices
	 */
	if (nvlist_lookup_nvlist_array(nvl, ZPOOL_CONFIG_SPARES,
	    &child, &children) == 0) {
		for (c = 0; c < children; c++)
			zfs_iter_vdev(zhp, child[c], data);
	}
	if (nvlist_lookup_nvlist_array(nvl, ZPOOL_CONFIG_L2CACHE,
	    &child, &children) == 0) {
		for (c = 0; c < children; c++)
			zfs_iter_vdev(zhp, child[c], data);
	}

	/* once a vdev was matched and processed there is nothing left to do */
	if (dp->dd_found)
		return;

	/*
	 * Match by GUID if available otherwise fallback to devid or physical
	 */
	if (dp->dd_vdev_guid != 0) {
		uint64_t guid;

		if (nvlist_lookup_uint64(nvl, ZPOOL_CONFIG_GUID,
		    &guid) != 0 || guid != dp->dd_vdev_guid) {
			return;
		}
		zed_log_msg(LOG_INFO, "  zfs_iter_vdev: matched on %llu", guid);
		dp->dd_found = B_TRUE;

	} else if (dp->dd_compare != NULL) {
		/*
		 * NOTE: On Linux there is an event for partition, so unlike
		 * illumos, substring matching is not required to accommodate
		 * the partition suffix. An exact match will be present in
		 * the dp->dd_compare value.
		 */
		if (nvlist_lookup_string(nvl, dp->dd_prop, &path) != 0 ||
		    strcmp(dp->dd_compare, path) != 0)
			return;

		zed_log_msg(LOG_INFO, "  zfs_iter_vdev: matched %s on %s",
		    dp->dd_prop, path);
		dp->dd_found = B_TRUE;

		/* pass the new devid for use by replacing code */
		if (dp->dd_new_devid != NULL) {
			(void) nvlist_add_string(nvl, "new_devid",
			    dp->dd_new_devid);
		}
	}

	(dp->dd_func)(zhp, nvl, dp->dd_islabeled);
}

void
zfs_enable_ds(void *arg)
{
	unavailpool_t *pool = (unavailpool_t *)arg;

	(void) zpool_enable_datasets(pool->uap_zhp, NULL, 0);
	zpool_close(pool->uap_zhp);
	free(pool);
}

static int
zfs_iter_pool(zpool_handle_t *zhp, void *data)
{
	nvlist_t *config, *nvl;
	dev_data_t *dp = data;
	uint64_t pool_guid;
	unavailpool_t *pool;

	zed_log_msg(LOG_INFO, "zfs_iter_pool: evaluating vdevs on %s (by %s)",
	    zpool_get_name(zhp), dp->dd_vdev_guid ? "GUID" : dp->dd_prop);

	/*
	 * For each vdev in this pool, look for a match to apply dd_func
	 */
	if ((config = zpool_get_config(zhp, NULL)) != NULL) {
		if (dp->dd_pool_guid == 0 ||
		    (nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID,
		    &pool_guid) == 0 && pool_guid == dp->dd_pool_guid)) {
			(void) nvlist_lookup_nvlist(config,
			    ZPOOL_CONFIG_VDEV_TREE, &nvl);
			zfs_iter_vdev(zhp, nvl, data);
		}
	}

	/*
	 * if this pool was originally unavailable,
	 * then enable its datasets asynchronously
	 */
	if (g_enumeration_done)  {
		for (pool = list_head(&g_pool_list); pool != NULL;
		    pool = list_next(&g_pool_list, pool)) {

			if (strcmp(zpool_get_name(zhp),
			    zpool_get_name(pool->uap_zhp)))
				continue;
			if (zfs_toplevel_state(zhp) >= VDEV_STATE_DEGRADED) {
				list_remove(&g_pool_list, pool);
				(void) tpool_dispatch(g_tpool, zfs_enable_ds,
				    pool);
				break;
			}
		}
	}

	zpool_close(zhp);
	return (dp->dd_found);	/* cease iteration after a match */
}

/*
 * Given a physical device location, iterate over all
 * (pool, vdev) pairs which correspond to that location.
 */
static boolean_t
devphys_iter(const char *physical, const char *devid, zfs_process_func_t func,
    boolean_t is_slice)
{
	dev_data_t data = { 0 };

	data.dd_compare = physical;
	data.dd_func = func;
	data.dd_prop = ZPOOL_CONFIG_PHYS_PATH;
	data.dd_found = B_FALSE;
	data.dd_islabeled = is_slice;
	data.dd_new_devid = devid;	/* used by auto replace code */

	(void) zpool_iter(g_zfshdl, zfs_iter_pool, &data);

	return (data.dd_found);
}

/*
 * Given a device identifier, find any vdevs with a matching devid.
 * On Linux we can match devid directly which is always a whole disk.
 */
static boolean_t
devid_iter(const char *devid, zfs_process_func_t func, boolean_t is_slice)
{
	dev_data_t data = { 0 };

	data.dd_compare = devid;
	data.dd_func = func;
	data.dd_prop = ZPOOL_CONFIG_DEVID;
	data.dd_found = B_FALSE;
	data.dd_islabeled = is_slice;
	data.dd_new_devid = devid;

	(void) zpool_iter(g_zfshdl, zfs_iter_pool, &data);

	return (data.dd_found);
}

/*
 * Handle a EC_DEV_ADD.ESC_DISK event.
 *
 * illumos
 *	Expects: DEV_PHYS_PATH string in schema
 *	Matches: vdev's ZPOOL_CONFIG_PHYS_PATH or ZPOOL_CONFIG_DEVID
 *
 *      path: '/dev/dsk/c0t1d0s0' (persistent)
 *     devid: 'id1,sd@SATA_____Hitachi_HDS72101______JP2940HZ3H74MC/a'
 * phys_path: '/pci@0,0/pci103c,1609@11/disk@1,0:a'
 *
 * linux
 *	provides: DEV_PHYS_PATH and DEV_IDENTIFIER strings in schema
 *	Matches: vdev's ZPOOL_CONFIG_PHYS_PATH or ZPOOL_CONFIG_DEVID
 *
 *      path: '/dev/sdc1' (not persistent)
 *     devid: 'ata-SAMSUNG_HD204UI_S2HGJD2Z805891-part1'
 * phys_path: 'pci-0000:04:00.0-sas-0x4433221106000000-lun-0'
 */
static int
zfs_deliver_add(nvlist_t *nvl, boolean_t is_lofi)
{
	char *devpath = NULL, *devid;
	boolean_t is_slice;

	/*
	 * Expecting a devid string and an optional physical location
	 */
	if (nvlist_lookup_string(nvl, DEV_IDENTIFIER, &devid) != 0)
		return (-1);

	(void) nvlist_lookup_string(nvl, DEV_PHYS_PATH, &devpath);

	is_slice = (nvlist_lookup_boolean(nvl, DEV_IS_PART) == 0);

	zed_log_msg(LOG_INFO, "zfs_deliver_add: adding %s (%s) (is_slice %d)",
	    devid, devpath ? devpath : "NULL", is_slice);

	/*
	 * Iterate over all vdevs looking for a match in the following order:
	 * 1. ZPOOL_CONFIG_DEVID (identifies the unique disk)
	 * 2. ZPOOL_CONFIG_PHYS_PATH (identifies disk physical location).
	 *
	 * For disks, we only want to pay attention to vdevs marked as whole
	 * disks or are a multipath device.
	 */
	if (!devid_iter(devid, zfs_process_add, is_slice) && devpath != NULL)
		(void) devphys_iter(devpath, devid, zfs_process_add, is_slice);

	return (0);
}

/*
 * Called when we receive a VDEV_CHECK event, which indicates a device could not
 * be opened during initial pool open, but the autoreplace property was set on
 * the pool.  In this case, we treat it as if it were an add event.
 */
static int
zfs_deliver_check(nvlist_t *nvl)
{
	dev_data_t data = { 0 };

	if (nvlist_lookup_uint64(nvl, ZFS_EV_POOL_GUID,
	    &data.dd_pool_guid) != 0 ||
	    nvlist_lookup_uint64(nvl, ZFS_EV_VDEV_GUID,
	    &data.dd_vdev_guid) != 0 ||
	    data.dd_vdev_guid == 0)
		return (0);

	zed_log_msg(LOG_INFO, "zfs_deliver_check: pool '%llu', vdev %llu",
	    data.dd_pool_guid, data.dd_vdev_guid);

	data.dd_func = zfs_process_add;

	(void) zpool_iter(g_zfshdl, zfs_iter_pool, &data);

	return (0);
}

static int
zfsdle_vdev_online(zpool_handle_t *zhp, void *data)
{
	char *devname = data;
	boolean_t avail_spare, l2cache;
	nvlist_t *tgt;
	int error;

	zed_log_msg(LOG_INFO, "zfsdle_vdev_online: searching for '%s' in '%s'",
	    devname, zpool_get_name(zhp));

	if ((tgt = zpool_find_vdev_by_physpath(zhp, devname,
	    &avail_spare, &l2cache, NULL)) != NULL) {
		char *path, fullpath[MAXPATHLEN];
		uint64_t wholedisk;

		error = nvlist_lookup_string(tgt, ZPOOL_CONFIG_PATH, &path);
		if (error) {
			zpool_close(zhp);
			return (0);
		}

		error = nvlist_lookup_uint64(tgt, ZPOOL_CONFIG_WHOLE_DISK,
		    &wholedisk);
		if (error)
			wholedisk = 0;

		if (wholedisk) {
			path = strrchr(path, '/');
			if (path != NULL) {
				path = zfs_strip_partition(path + 1);
				if (path == NULL) {
					zpool_close(zhp);
					return (0);
				}
			} else {
				zpool_close(zhp);
				return (0);
			}

			(void) strlcpy(fullpath, path, sizeof (fullpath));
			free(path);

			/*
			 * We need to reopen the pool associated with this
			 * device so that the kernel can update the size of
			 * the expanded device.  When expanding there is no
			 * need to restart the scrub from the beginning.
			 */
			boolean_t scrub_restart = B_FALSE;
			(void) zpool_reopen_one(zhp, &scrub_restart);
		} else {
			(void) strlcpy(fullpath, path, sizeof (fullpath));
		}

		if (zpool_get_prop_int(zhp, ZPOOL_PROP_AUTOEXPAND, NULL)) {
			vdev_state_t newstate;

			if (zpool_get_state(zhp) != POOL_STATE_UNAVAIL) {
				error = zpool_vdev_online(zhp, fullpath, 0,
				    &newstate);
				zed_log_msg(LOG_INFO, "zfsdle_vdev_online: "
				    "setting device '%s' to ONLINE state "
				    "in pool '%s': %d", fullpath,
				    zpool_get_name(zhp), error);
			}
		}
		zpool_close(zhp);
		return (1);
	}
	zpool_close(zhp);
	return (0);
}

/*
 * This function handles the ESC_DEV_DLE device change event.  Use the
 * provided vdev guid when looking up a disk or partition, when the guid
 * is not present assume the entire disk is owned by ZFS and append the
 * expected -part1 partition information then lookup by physical path.
 */
static int
zfs_deliver_dle(nvlist_t *nvl)
{
	char *devname, name[MAXPATHLEN];
	uint64_t guid;

	if (nvlist_lookup_uint64(nvl, ZFS_EV_VDEV_GUID, &guid) == 0) {
		sprintf(name, "%llu", (u_longlong_t)guid);
	} else if (nvlist_lookup_string(nvl, DEV_PHYS_PATH, &devname) == 0) {
		strlcpy(name, devname, MAXPATHLEN);
		zfs_append_partition(name, MAXPATHLEN);
	} else {
		zed_log_msg(LOG_INFO, "zfs_deliver_dle: no guid or physpath");
	}

	if (zpool_iter(g_zfshdl, zfsdle_vdev_online, name) != 1) {
		zed_log_msg(LOG_INFO, "zfs_deliver_dle: device '%s' not "
		    "found", name);
		return (1);
	}

	return (0);
}

/*
 * syseventd daemon module event handler
 *
 * Handles syseventd daemon zfs device related events:
 *
 *	EC_DEV_ADD.ESC_DISK
 *	EC_DEV_STATUS.ESC_DEV_DLE
 *	EC_ZFS.ESC_ZFS_VDEV_CHECK
 *
 * Note: assumes only one thread active at a time (not thread safe)
 */
static int
zfs_slm_deliver_event(const char *class, const char *subclass, nvlist_t *nvl)
{
	int ret;
	boolean_t is_lofi = B_FALSE, is_check = B_FALSE, is_dle = B_FALSE;

	if (strcmp(class, EC_DEV_ADD) == 0) {
		/*
		 * We're mainly interested in disk additions, but we also listen
		 * for new loop devices, to allow for simplified testing.
		 */
		if (strcmp(subclass, ESC_DISK) == 0)
			is_lofi = B_FALSE;
		else if (strcmp(subclass, ESC_LOFI) == 0)
			is_lofi = B_TRUE;
		else
			return (0);

		is_check = B_FALSE;
	} else if (strcmp(class, EC_ZFS) == 0 &&
	    strcmp(subclass, ESC_ZFS_VDEV_CHECK) == 0) {
		/*
		 * This event signifies that a device failed to open
		 * during pool load, but the 'autoreplace' property was
		 * set, so we should pretend it's just been added.
		 */
		is_check = B_TRUE;
	} else if (strcmp(class, EC_DEV_STATUS) == 0 &&
	    strcmp(subclass, ESC_DEV_DLE) == 0) {
		is_dle = B_TRUE;
	} else {
		return (0);
	}

	if (is_dle)
		ret = zfs_deliver_dle(nvl);
	else if (is_check)
		ret = zfs_deliver_check(nvl);
	else
		ret = zfs_deliver_add(nvl, is_lofi);

	return (ret);
}

/*ARGSUSED*/
static void *
zfs_enum_pools(void *arg)
{
	(void) zpool_iter(g_zfshdl, zfs_unavail_pool, (void *)&g_pool_list);
	/*
	 * Linux - instead of using a thread pool, each list entry
	 * will spawn a thread when an unavailable pool transitions
	 * to available. zfs_slm_fini will wait for these threads.
	 */
	g_enumeration_done = B_TRUE;
	return (NULL);
}

/*
 * called from zed daemon at startup
 *
 * sent messages from zevents or udev monitor
 *
 * For now, each agent has its own libzfs instance
 */
int
zfs_slm_init()
{
	if ((g_zfshdl = libzfs_init()) == NULL)
		return (-1);

	/*
	 * collect a list of unavailable pools (asynchronously,
	 * since this can take a while)
	 */
	list_create(&g_pool_list, sizeof (struct unavailpool),
	    offsetof(struct unavailpool, uap_node));

	if (pthread_create(&g_zfs_tid, NULL, zfs_enum_pools, NULL) != 0) {
		list_destroy(&g_pool_list);
		libzfs_fini(g_zfshdl);
		return (-1);
	}

	list_create(&g_device_list, sizeof (struct pendingdev),
	    offsetof(struct pendingdev, pd_node));

	return (0);
}

void
zfs_slm_fini()
{
	unavailpool_t *pool;
	pendingdev_t *device;

	/* wait for zfs_enum_pools thread to complete */
	(void) pthread_join(g_zfs_tid, NULL);
	/* destroy the thread pool */
	if (g_tpool != NULL) {
		tpool_wait(g_tpool);
		tpool_destroy(g_tpool);
	}

	while ((pool = (list_head(&g_pool_list))) != NULL) {
		list_remove(&g_pool_list, pool);
		zpool_close(pool->uap_zhp);
		free(pool);
	}
	list_destroy(&g_pool_list);

	while ((device = (list_head(&g_device_list))) != NULL) {
		list_remove(&g_device_list, device);
		free(device);
	}
	list_destroy(&g_device_list);

	libzfs_fini(g_zfshdl);
}

void
zfs_slm_event(const char *class, const char *subclass, nvlist_t *nvl)
{
	zed_log_msg(LOG_INFO, "zfs_slm_event: %s.%s", class, subclass);
	(void) zfs_slm_deliver_event(class, subclass, nvl);
}
