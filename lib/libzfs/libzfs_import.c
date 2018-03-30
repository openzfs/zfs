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
 * Copyright 2015 Nexenta Systems, Inc. All rights reserved.
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright 2015 RackTop Systems.
 * Copyright (c) 2016, Intel Corporation.
 */

/*
 * Pool import support functions.
 *
 * To import a pool, we rely on reading the configuration information from the
 * ZFS label of each device.  If we successfully read the label, then we
 * organize the configuration information in the following hierarchy:
 *
 * 	pool guid -> toplevel vdev guid -> label txg
 *
 * Duplicate entries matching this same tuple will be discarded.  Once we have
 * examined every device, we pick the best label txg config for each toplevel
 * vdev.  We then arrange these toplevel vdevs into a complete pool config, and
 * update any paths that have changed.  Finally, we attempt to import the pool
 * using our derived config, and record the results.
 */

#include <ctype.h>
#include <devid.h>
#include <dirent.h>
#include <errno.h>
#include <libintl.h>
#ifdef HAVE_LIBUDEV
#include <libudev.h>
#include <sched.h>
#endif
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/vtoc.h>
#include <sys/dktp/fdisk.h>
#include <sys/efi_partition.h>
#include <sys/vdev_impl.h>
#include <blkid/blkid.h>
#include "libzfs.h"
#include "libzfs_impl.h"
#include <libzfs.h>

/*
 * Intermediate structures used to gather configuration information.
 */
typedef struct config_entry {
	uint64_t		ce_txg;
	nvlist_t		*ce_config;
	struct config_entry	*ce_next;
} config_entry_t;

typedef struct vdev_entry {
	uint64_t		ve_guid;
	config_entry_t		*ve_configs;
	struct vdev_entry	*ve_next;
} vdev_entry_t;

typedef struct pool_entry {
	uint64_t		pe_guid;
	vdev_entry_t		*pe_vdevs;
	struct pool_entry	*pe_next;
} pool_entry_t;

typedef struct name_entry {
	char			*ne_name;
	uint64_t		ne_guid;
	uint64_t		ne_order;
	uint64_t		ne_num_labels;
	struct name_entry	*ne_next;
} name_entry_t;

typedef struct pool_list {
	pool_entry_t		*pools;
	name_entry_t		*names;
} pool_list_t;

#define	DEV_BYID_PATH	"/dev/disk/by-id/"

/*
 * Linux persistent device strings for vdev labels
 *
 * based on libudev for consistency with libudev disk add/remove events
 */
#ifdef HAVE_LIBUDEV

typedef struct vdev_dev_strs {
	char	vds_devid[128];
	char	vds_devphys[128];
} vdev_dev_strs_t;

/*
 * Obtain the persistent device id string (describes what)
 *
 * used by ZED vdev matching for auto-{online,expand,replace}
 */
int
zfs_device_get_devid(struct udev_device *dev, char *bufptr, size_t buflen)
{
	struct udev_list_entry *entry;
	const char *bus;
	char devbyid[MAXPATHLEN];

	/* The bus based by-id path is preferred */
	bus = udev_device_get_property_value(dev, "ID_BUS");

	if (bus == NULL) {
		const char *dm_uuid;

		/*
		 * For multipath nodes use the persistent uuid based identifier
		 *
		 * Example: /dev/disk/by-id/dm-uuid-mpath-35000c5006304de3f
		 */
		dm_uuid = udev_device_get_property_value(dev, "DM_UUID");
		if (dm_uuid != NULL) {
			(void) snprintf(bufptr, buflen, "dm-uuid-%s", dm_uuid);
			return (0);
		}

		/*
		 * NVME 'by-id' symlinks are similar to bus case
		 */
		struct udev_device *parent;

		parent = udev_device_get_parent_with_subsystem_devtype(dev,
		    "nvme", NULL);
		if (parent != NULL)
			bus = "nvme";	/* continue with bus symlink search */
		else
			return (ENODATA);
	}

	/*
	 * locate the bus specific by-id link
	 */
	(void) snprintf(devbyid, sizeof (devbyid), "%s%s-", DEV_BYID_PATH, bus);
	entry = udev_device_get_devlinks_list_entry(dev);
	while (entry != NULL) {
		const char *name;

		name = udev_list_entry_get_name(entry);
		if (strncmp(name, devbyid, strlen(devbyid)) == 0) {
			name += strlen(DEV_BYID_PATH);
			(void) strlcpy(bufptr, name, buflen);
			return (0);
		}
		entry = udev_list_entry_get_next(entry);
	}

	return (ENODATA);
}

/*
 * Obtain the persistent physical location string (describes where)
 *
 * used by ZED vdev matching for auto-{online,expand,replace}
 */
int
zfs_device_get_physical(struct udev_device *dev, char *bufptr, size_t buflen)
{
	const char *physpath = NULL;

	/*
	 * Normal disks use ID_PATH for their physical path.  Device mapper
	 * devices are virtual and don't have a physical path.  For them we
	 * use ID_VDEV instead, which is setup via the /etc/vdev_id.conf file.
	 * ID_VDEV provides a persistent path to a virtual device.  If you
	 * don't have vdev_id.conf setup, you cannot use multipath autoreplace.
	 */
	if (!((physpath = udev_device_get_property_value(dev, "ID_PATH")) &&
	    physpath[0])) {
		if (!((physpath =
		    udev_device_get_property_value(dev, "ID_VDEV")) &&
		    physpath[0])) {
			return (ENODATA);
		}
	}

	(void) strlcpy(bufptr, physpath, buflen);

	return (0);
}

boolean_t
udev_is_mpath(struct udev_device *dev)
{
	return udev_device_get_property_value(dev, "DM_UUID") &&
	    udev_device_get_property_value(dev, "MPATH_SBIN_PATH");
}

/*
 * A disk is considered a multipath whole disk when:
 *	DEVNAME key value has "dm-"
 *	DM_NAME key value has "mpath" prefix
 *	DM_UUID key exists
 *	ID_PART_TABLE_TYPE key does not exist or is not gpt
 */
static boolean_t
udev_mpath_whole_disk(struct udev_device *dev)
{
	const char *devname, *type, *uuid;

	devname = udev_device_get_property_value(dev, "DEVNAME");
	type = udev_device_get_property_value(dev, "ID_PART_TABLE_TYPE");
	uuid = udev_device_get_property_value(dev, "DM_UUID");

	if ((devname != NULL && strncmp(devname, "/dev/dm-", 8) == 0) &&
	    ((type == NULL) || (strcmp(type, "gpt") != 0)) &&
	    (uuid != NULL)) {
		return (B_TRUE);
	}

	return (B_FALSE);
}

/*
 * Check if a disk is effectively a multipath whole disk
 */
boolean_t
is_mpath_whole_disk(const char *path)
{
	struct udev *udev;
	struct udev_device *dev = NULL;
	char nodepath[MAXPATHLEN];
	char *sysname;
	boolean_t wholedisk = B_FALSE;

	if (realpath(path, nodepath) == NULL)
		return (B_FALSE);
	sysname = strrchr(nodepath, '/') + 1;
	if (strncmp(sysname, "dm-", 3) != 0)
		return (B_FALSE);
	if ((udev = udev_new()) == NULL)
		return (B_FALSE);
	if ((dev = udev_device_new_from_subsystem_sysname(udev, "block",
	    sysname)) == NULL) {
		udev_device_unref(dev);
		return (B_FALSE);
	}

	wholedisk = udev_mpath_whole_disk(dev);

	udev_device_unref(dev);
	return (wholedisk);
}

static int
udev_device_is_ready(struct udev_device *dev)
{
#ifdef HAVE_LIBUDEV_UDEV_DEVICE_GET_IS_INITIALIZED
	return (udev_device_get_is_initialized(dev));
#else
	/* wait for DEVLINKS property to be initialized */
	return (udev_device_get_property_value(dev, "DEVLINKS") != NULL);
#endif
}

/*
 * Wait up to timeout_ms for udev to set up the device node.  The device is
 * considered ready when libudev determines it has been initialized, all of
 * the device links have been verified to exist, and it has been allowed to
 * settle.  At this point the device the device can be accessed reliably.
 * Depending on the complexity of the udev rules this process could take
 * several seconds.
 */
int
zpool_label_disk_wait(char *path, int timeout_ms)
{
	struct udev *udev;
	struct udev_device *dev = NULL;
	char nodepath[MAXPATHLEN];
	char *sysname = NULL;
	int ret = ENODEV;
	int settle_ms = 50;
	long sleep_ms = 10;
	hrtime_t start, settle;

	if ((udev = udev_new()) == NULL)
		return (ENXIO);

	start = gethrtime();
	settle = 0;

	do {
		if (sysname == NULL) {
			if (realpath(path, nodepath) != NULL) {
				sysname = strrchr(nodepath, '/') + 1;
			} else {
				(void) usleep(sleep_ms * MILLISEC);
				continue;
			}
		}

		dev = udev_device_new_from_subsystem_sysname(udev,
		    "block", sysname);
		if ((dev != NULL) && udev_device_is_ready(dev)) {
			struct udev_list_entry *links, *link = NULL;

			ret = 0;
			links = udev_device_get_devlinks_list_entry(dev);

			udev_list_entry_foreach(link, links) {
				struct stat64 statbuf;
				const char *name;

				name = udev_list_entry_get_name(link);
				errno = 0;
				if (stat64(name, &statbuf) == 0 && errno == 0)
					continue;

				settle = 0;
				ret = ENODEV;
				break;
			}

			if (ret == 0) {
				if (settle == 0) {
					settle = gethrtime();
				} else if (NSEC2MSEC(gethrtime() - settle) >=
				    settle_ms) {
					udev_device_unref(dev);
					break;
				}
			}
		}

		udev_device_unref(dev);
		(void) usleep(sleep_ms * MILLISEC);

	} while (NSEC2MSEC(gethrtime() - start) < timeout_ms);

	udev_unref(udev);

	return (ret);
}


/*
 * Encode the persistent devices strings
 * used for the vdev disk label
 */
static int
encode_device_strings(const char *path, vdev_dev_strs_t *ds,
    boolean_t wholedisk)
{
	struct udev *udev;
	struct udev_device *dev = NULL;
	char nodepath[MAXPATHLEN];
	char *sysname;
	int ret = ENODEV;
	hrtime_t start;

	if ((udev = udev_new()) == NULL)
		return (ENXIO);

	/* resolve path to a runtime device node instance */
	if (realpath(path, nodepath) == NULL)
		goto no_dev;

	sysname = strrchr(nodepath, '/') + 1;

	/*
	 * Wait up to 3 seconds for udev to set up the device node context
	 */
	start = gethrtime();
	do {
		dev = udev_device_new_from_subsystem_sysname(udev, "block",
		    sysname);
		if (dev == NULL)
			goto no_dev;
		if (udev_device_is_ready(dev))
			break;  /* udev ready */

		udev_device_unref(dev);
		dev = NULL;

		if (NSEC2MSEC(gethrtime() - start) < 10)
			(void) sched_yield();	/* yield/busy wait up to 10ms */
		else
			(void) usleep(10 * MILLISEC);

	} while (NSEC2MSEC(gethrtime() - start) < (3 * MILLISEC));

	if (dev == NULL)
		goto no_dev;

	/*
	 * Only whole disks require extra device strings
	 */
	if (!wholedisk && !udev_mpath_whole_disk(dev))
		goto no_dev;

	ret = zfs_device_get_devid(dev, ds->vds_devid, sizeof (ds->vds_devid));
	if (ret != 0)
		goto no_dev_ref;

	/* physical location string (optional) */
	if (zfs_device_get_physical(dev, ds->vds_devphys,
	    sizeof (ds->vds_devphys)) != 0) {
		ds->vds_devphys[0] = '\0'; /* empty string --> not available */
	}

no_dev_ref:
	udev_device_unref(dev);
no_dev:
	udev_unref(udev);

	return (ret);
}

/*
 * Update a leaf vdev's persistent device strings (Linux only)
 *
 * - only applies for a dedicated leaf vdev (aka whole disk)
 * - updated during pool create|add|attach|import
 * - used for matching device matching during auto-{online,expand,replace}
 * - stored in a leaf disk config label (i.e. alongside 'path' NVP)
 * - these strings are currently not used in kernel (i.e. for vdev_disk_open)
 *
 * single device node example:
 * 	devid:		'scsi-MG03SCA300_350000494a8cb3d67-part1'
 * 	phys_path:	'pci-0000:04:00.0-sas-0x50000394a8cb3d67-lun-0'
 *
 * multipath device node example:
 * 	devid:		'dm-uuid-mpath-35000c5006304de3f'
 *
 * We also store the enclosure sysfs path for turning on enclosure LEDs
 * (if applicable):
 *	vdev_enc_sysfs_path: '/sys/class/enclosure/11:0:1:0/SLOT 4'
 */
void
update_vdev_config_dev_strs(nvlist_t *nv)
{
	vdev_dev_strs_t vds;
	char *env, *type, *path;
	uint64_t wholedisk = 0;
	char *upath, *spath;

	/*
	 * For the benefit of legacy ZFS implementations, allow
	 * for opting out of devid strings in the vdev label.
	 *
	 * example use:
	 *	env ZFS_VDEV_DEVID_OPT_OUT=YES zpool import dozer
	 *
	 * explanation:
	 * Older ZFS on Linux implementations had issues when attempting to
	 * display pool config VDEV names if a "devid" NVP value is present
	 * in the pool's config.
	 *
	 * For example, a pool that originated on illumos platform would
	 * have a devid value in the config and "zpool status" would fail
	 * when listing the config.
	 *
	 * A pool can be stripped of any "devid" values on import or
	 * prevented from adding them on zpool create|add by setting
	 * ZFS_VDEV_DEVID_OPT_OUT.
	 */
	env = getenv("ZFS_VDEV_DEVID_OPT_OUT");
	if (env && (strtoul(env, NULL, 0) > 0 ||
	    !strncasecmp(env, "YES", 3) || !strncasecmp(env, "ON", 2))) {
		(void) nvlist_remove_all(nv, ZPOOL_CONFIG_DEVID);
		(void) nvlist_remove_all(nv, ZPOOL_CONFIG_PHYS_PATH);
		return;
	}

	if (nvlist_lookup_string(nv, ZPOOL_CONFIG_TYPE, &type) != 0 ||
	    strcmp(type, VDEV_TYPE_DISK) != 0) {
		return;
	}
	if (nvlist_lookup_string(nv, ZPOOL_CONFIG_PATH, &path) != 0)
		return;
	(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_WHOLE_DISK, &wholedisk);

	/*
	 * Update device string values in config nvlist
	 */
	if (encode_device_strings(path, &vds, (boolean_t)wholedisk) == 0) {
		(void) nvlist_add_string(nv, ZPOOL_CONFIG_DEVID, vds.vds_devid);
		if (vds.vds_devphys[0] != '\0') {
			(void) nvlist_add_string(nv, ZPOOL_CONFIG_PHYS_PATH,
			    vds.vds_devphys);
		}

		/* Add enclosure sysfs path (if disk is in an enclosure) */
		upath = zfs_get_underlying_path(path);
		spath = zfs_get_enclosure_sysfs_path(upath);
		if (spath)
			nvlist_add_string(nv, ZPOOL_CONFIG_VDEV_ENC_SYSFS_PATH,
			    spath);
		else
			nvlist_remove_all(nv, ZPOOL_CONFIG_VDEV_ENC_SYSFS_PATH);

		free(upath);
		free(spath);
	} else {
		/* clear out any stale entries */
		(void) nvlist_remove_all(nv, ZPOOL_CONFIG_DEVID);
		(void) nvlist_remove_all(nv, ZPOOL_CONFIG_PHYS_PATH);
		(void) nvlist_remove_all(nv, ZPOOL_CONFIG_VDEV_ENC_SYSFS_PATH);
	}
}
#else

boolean_t
is_mpath_whole_disk(const char *path)
{
	return (B_FALSE);
}

/*
 * Wait up to timeout_ms for udev to set up the device node.  The device is
 * considered ready when the provided path have been verified to exist and
 * it has been allowed to settle.  At this point the device the device can
 * be accessed reliably.  Depending on the complexity of the udev rules thisi
 * process could take several seconds.
 */
int
zpool_label_disk_wait(char *path, int timeout_ms)
{
	int settle_ms = 50;
	long sleep_ms = 10;
	hrtime_t start, settle;
	struct stat64 statbuf;

	start = gethrtime();
	settle = 0;

	do {
		errno = 0;
		if ((stat64(path, &statbuf) == 0) && (errno == 0)) {
			if (settle == 0)
				settle = gethrtime();
			else if (NSEC2MSEC(gethrtime() - settle) >= settle_ms)
				return (0);
		} else if (errno != ENOENT) {
			return (errno);
		}

		usleep(sleep_ms * MILLISEC);
	} while (NSEC2MSEC(gethrtime() - start) < timeout_ms);

	return (ENODEV);
}

void
update_vdev_config_dev_strs(nvlist_t *nv)
{
}

#endif /* HAVE_LIBUDEV */

/*
 * Go through and fix up any path and/or devid information for the given vdev
 * configuration.
 */
static int
fix_paths(nvlist_t *nv, name_entry_t *names)
{
	nvlist_t **child;
	uint_t c, children;
	uint64_t guid;
	name_entry_t *ne, *best;
	char *path;

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) == 0) {
		for (c = 0; c < children; c++)
			if (fix_paths(child[c], names) != 0)
				return (-1);
		return (0);
	}

	/*
	 * This is a leaf (file or disk) vdev.  In either case, go through
	 * the name list and see if we find a matching guid.  If so, replace
	 * the path and see if we can calculate a new devid.
	 *
	 * There may be multiple names associated with a particular guid, in
	 * which case we have overlapping partitions or multiple paths to the
	 * same disk.  In this case we prefer to use the path name which
	 * matches the ZPOOL_CONFIG_PATH.  If no matching entry is found we
	 * use the lowest order device which corresponds to the first match
	 * while traversing the ZPOOL_IMPORT_PATH search path.
	 */
	verify(nvlist_lookup_uint64(nv, ZPOOL_CONFIG_GUID, &guid) == 0);
	if (nvlist_lookup_string(nv, ZPOOL_CONFIG_PATH, &path) != 0)
		path = NULL;

	best = NULL;
	for (ne = names; ne != NULL; ne = ne->ne_next) {
		if (ne->ne_guid == guid) {
			if (path == NULL) {
				best = ne;
				break;
			}

			if ((strlen(path) == strlen(ne->ne_name)) &&
			    strncmp(path, ne->ne_name, strlen(path)) == 0) {
				best = ne;
				break;
			}

			if (best == NULL) {
				best = ne;
				continue;
			}

			/* Prefer paths with move vdev labels. */
			if (ne->ne_num_labels > best->ne_num_labels) {
				best = ne;
				continue;
			}

			/* Prefer paths earlier in the search order. */
			if (ne->ne_num_labels == best->ne_num_labels &&
			    ne->ne_order < best->ne_order) {
				best = ne;
				continue;
			}
		}
	}

	if (best == NULL)
		return (0);

	if (nvlist_add_string(nv, ZPOOL_CONFIG_PATH, best->ne_name) != 0)
		return (-1);

	/* Linux only - update ZPOOL_CONFIG_DEVID and ZPOOL_CONFIG_PHYS_PATH */
	update_vdev_config_dev_strs(nv);

	return (0);
}

/*
 * Add the given configuration to the list of known devices.
 */
static int
add_config(libzfs_handle_t *hdl, pool_list_t *pl, const char *path,
    int order, int num_labels, nvlist_t *config)
{
	uint64_t pool_guid, vdev_guid, top_guid, txg, state;
	pool_entry_t *pe;
	vdev_entry_t *ve;
	config_entry_t *ce;
	name_entry_t *ne;

	/*
	 * If this is a hot spare not currently in use or level 2 cache
	 * device, add it to the list of names to translate, but don't do
	 * anything else.
	 */
	if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_STATE,
	    &state) == 0 &&
	    (state == POOL_STATE_SPARE || state == POOL_STATE_L2CACHE) &&
	    nvlist_lookup_uint64(config, ZPOOL_CONFIG_GUID, &vdev_guid) == 0) {
		if ((ne = zfs_alloc(hdl, sizeof (name_entry_t))) == NULL) {
			nvlist_free(config);
			return (-1);
		}

		if ((ne->ne_name = zfs_strdup(hdl, path)) == NULL) {
			free(ne);
			nvlist_free(config);
			return (-1);
		}
		ne->ne_guid = vdev_guid;
		ne->ne_order = order;
		ne->ne_num_labels = num_labels;
		ne->ne_next = pl->names;
		pl->names = ne;
		nvlist_free(config);
		return (0);
	}

	/*
	 * If we have a valid config but cannot read any of these fields, then
	 * it means we have a half-initialized label.  In vdev_label_init()
	 * we write a label with txg == 0 so that we can identify the device
	 * in case the user refers to the same disk later on.  If we fail to
	 * create the pool, we'll be left with a label in this state
	 * which should not be considered part of a valid pool.
	 */
	if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID,
	    &pool_guid) != 0 ||
	    nvlist_lookup_uint64(config, ZPOOL_CONFIG_GUID,
	    &vdev_guid) != 0 ||
	    nvlist_lookup_uint64(config, ZPOOL_CONFIG_TOP_GUID,
	    &top_guid) != 0 ||
	    nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_TXG,
	    &txg) != 0 || txg == 0) {
		nvlist_free(config);
		return (0);
	}

	/*
	 * First, see if we know about this pool.  If not, then add it to the
	 * list of known pools.
	 */
	for (pe = pl->pools; pe != NULL; pe = pe->pe_next) {
		if (pe->pe_guid == pool_guid)
			break;
	}

	if (pe == NULL) {
		if ((pe = zfs_alloc(hdl, sizeof (pool_entry_t))) == NULL) {
			nvlist_free(config);
			return (-1);
		}
		pe->pe_guid = pool_guid;
		pe->pe_next = pl->pools;
		pl->pools = pe;
	}

	/*
	 * Second, see if we know about this toplevel vdev.  Add it if its
	 * missing.
	 */
	for (ve = pe->pe_vdevs; ve != NULL; ve = ve->ve_next) {
		if (ve->ve_guid == top_guid)
			break;
	}

	if (ve == NULL) {
		if ((ve = zfs_alloc(hdl, sizeof (vdev_entry_t))) == NULL) {
			nvlist_free(config);
			return (-1);
		}
		ve->ve_guid = top_guid;
		ve->ve_next = pe->pe_vdevs;
		pe->pe_vdevs = ve;
	}

	/*
	 * Third, see if we have a config with a matching transaction group.  If
	 * so, then we do nothing.  Otherwise, add it to the list of known
	 * configs.
	 */
	for (ce = ve->ve_configs; ce != NULL; ce = ce->ce_next) {
		if (ce->ce_txg == txg)
			break;
	}

	if (ce == NULL) {
		if ((ce = zfs_alloc(hdl, sizeof (config_entry_t))) == NULL) {
			nvlist_free(config);
			return (-1);
		}
		ce->ce_txg = txg;
		ce->ce_config = config;
		ce->ce_next = ve->ve_configs;
		ve->ve_configs = ce;
	} else {
		nvlist_free(config);
	}

	/*
	 * At this point we've successfully added our config to the list of
	 * known configs.  The last thing to do is add the vdev guid -> path
	 * mappings so that we can fix up the configuration as necessary before
	 * doing the import.
	 */
	if ((ne = zfs_alloc(hdl, sizeof (name_entry_t))) == NULL)
		return (-1);

	if ((ne->ne_name = zfs_strdup(hdl, path)) == NULL) {
		free(ne);
		return (-1);
	}

	ne->ne_guid = vdev_guid;
	ne->ne_order = order;
	ne->ne_num_labels = num_labels;
	ne->ne_next = pl->names;
	pl->names = ne;

	return (0);
}

/*
 * Returns true if the named pool matches the given GUID.
 */
static int
pool_active(libzfs_handle_t *hdl, const char *name, uint64_t guid,
    boolean_t *isactive)
{
	zpool_handle_t *zhp;
	uint64_t theguid;

	if (zpool_open_silent(hdl, name, &zhp) != 0)
		return (-1);

	if (zhp == NULL) {
		*isactive = B_FALSE;
		return (0);
	}

	verify(nvlist_lookup_uint64(zhp->zpool_config, ZPOOL_CONFIG_POOL_GUID,
	    &theguid) == 0);

	zpool_close(zhp);

	*isactive = (theguid == guid);
	return (0);
}

static nvlist_t *
refresh_config(libzfs_handle_t *hdl, nvlist_t *config)
{
	nvlist_t *nvl;
	zfs_cmd_t zc = {"\0"};
	int err, dstbuf_size;

	if (zcmd_write_conf_nvlist(hdl, &zc, config) != 0)
		return (NULL);

	dstbuf_size = MAX(CONFIG_BUF_MINSIZE, zc.zc_nvlist_conf_size * 4);

	if (zcmd_alloc_dst_nvlist(hdl, &zc, dstbuf_size) != 0) {
		zcmd_free_nvlists(&zc);
		return (NULL);
	}

	while ((err = ioctl(hdl->libzfs_fd, ZFS_IOC_POOL_TRYIMPORT,
	    &zc)) != 0 && errno == ENOMEM) {
		if (zcmd_expand_dst_nvlist(hdl, &zc) != 0) {
			zcmd_free_nvlists(&zc);
			return (NULL);
		}
	}

	if (err) {
		zcmd_free_nvlists(&zc);
		return (NULL);
	}

	if (zcmd_read_dst_nvlist(hdl, &zc, &nvl) != 0) {
		zcmd_free_nvlists(&zc);
		return (NULL);
	}

	zcmd_free_nvlists(&zc);
	return (nvl);
}

/*
 * Determine if the vdev id is a hole in the namespace.
 */
boolean_t
vdev_is_hole(uint64_t *hole_array, uint_t holes, uint_t id)
{
	int c;

	for (c = 0; c < holes; c++) {

		/* Top-level is a hole */
		if (hole_array[c] == id)
			return (B_TRUE);
	}
	return (B_FALSE);
}

/*
 * Convert our list of pools into the definitive set of configurations.  We
 * start by picking the best config for each toplevel vdev.  Once that's done,
 * we assemble the toplevel vdevs into a full config for the pool.  We make a
 * pass to fix up any incorrect paths, and then add it to the main list to
 * return to the user.
 */
static nvlist_t *
get_configs(libzfs_handle_t *hdl, pool_list_t *pl, boolean_t active_ok)
{
	pool_entry_t *pe;
	vdev_entry_t *ve;
	config_entry_t *ce;
	nvlist_t *ret = NULL, *config = NULL, *tmp = NULL, *nvtop, *nvroot;
	nvlist_t **spares, **l2cache;
	uint_t i, nspares, nl2cache;
	boolean_t config_seen;
	uint64_t best_txg;
	char *name, *hostname = NULL;
	uint64_t guid;
	uint_t children = 0;
	nvlist_t **child = NULL;
	uint_t holes;
	uint64_t *hole_array, max_id;
	uint_t c;
	boolean_t isactive;
	uint64_t hostid;
	nvlist_t *nvl;
	boolean_t valid_top_config = B_FALSE;

	if (nvlist_alloc(&ret, 0, 0) != 0)
		goto nomem;

	for (pe = pl->pools; pe != NULL; pe = pe->pe_next) {
		uint64_t id, max_txg = 0;

		if (nvlist_alloc(&config, NV_UNIQUE_NAME, 0) != 0)
			goto nomem;
		config_seen = B_FALSE;

		/*
		 * Iterate over all toplevel vdevs.  Grab the pool configuration
		 * from the first one we find, and then go through the rest and
		 * add them as necessary to the 'vdevs' member of the config.
		 */
		for (ve = pe->pe_vdevs; ve != NULL; ve = ve->ve_next) {

			/*
			 * Determine the best configuration for this vdev by
			 * selecting the config with the latest transaction
			 * group.
			 */
			best_txg = 0;
			for (ce = ve->ve_configs; ce != NULL;
			    ce = ce->ce_next) {

				if (ce->ce_txg > best_txg) {
					tmp = ce->ce_config;
					best_txg = ce->ce_txg;
				}
			}

			/*
			 * We rely on the fact that the max txg for the
			 * pool will contain the most up-to-date information
			 * about the valid top-levels in the vdev namespace.
			 */
			if (best_txg > max_txg) {
				(void) nvlist_remove(config,
				    ZPOOL_CONFIG_VDEV_CHILDREN,
				    DATA_TYPE_UINT64);
				(void) nvlist_remove(config,
				    ZPOOL_CONFIG_HOLE_ARRAY,
				    DATA_TYPE_UINT64_ARRAY);

				max_txg = best_txg;
				hole_array = NULL;
				holes = 0;
				max_id = 0;
				valid_top_config = B_FALSE;

				if (nvlist_lookup_uint64(tmp,
				    ZPOOL_CONFIG_VDEV_CHILDREN, &max_id) == 0) {
					verify(nvlist_add_uint64(config,
					    ZPOOL_CONFIG_VDEV_CHILDREN,
					    max_id) == 0);
					valid_top_config = B_TRUE;
				}

				if (nvlist_lookup_uint64_array(tmp,
				    ZPOOL_CONFIG_HOLE_ARRAY, &hole_array,
				    &holes) == 0) {
					verify(nvlist_add_uint64_array(config,
					    ZPOOL_CONFIG_HOLE_ARRAY,
					    hole_array, holes) == 0);
				}
			}

			if (!config_seen) {
				/*
				 * Copy the relevant pieces of data to the pool
				 * configuration:
				 *
				 *	version
				 *	pool guid
				 *	name
				 *	comment (if available)
				 *	pool state
				 *	hostid (if available)
				 *	hostname (if available)
				 */
				uint64_t state, version;
				char *comment = NULL;

				version = fnvlist_lookup_uint64(tmp,
				    ZPOOL_CONFIG_VERSION);
				fnvlist_add_uint64(config,
				    ZPOOL_CONFIG_VERSION, version);
				guid = fnvlist_lookup_uint64(tmp,
				    ZPOOL_CONFIG_POOL_GUID);
				fnvlist_add_uint64(config,
				    ZPOOL_CONFIG_POOL_GUID, guid);
				name = fnvlist_lookup_string(tmp,
				    ZPOOL_CONFIG_POOL_NAME);
				fnvlist_add_string(config,
				    ZPOOL_CONFIG_POOL_NAME, name);

				if (nvlist_lookup_string(tmp,
				    ZPOOL_CONFIG_COMMENT, &comment) == 0)
					fnvlist_add_string(config,
					    ZPOOL_CONFIG_COMMENT, comment);

				state = fnvlist_lookup_uint64(tmp,
				    ZPOOL_CONFIG_POOL_STATE);
				fnvlist_add_uint64(config,
				    ZPOOL_CONFIG_POOL_STATE, state);

				hostid = 0;
				if (nvlist_lookup_uint64(tmp,
				    ZPOOL_CONFIG_HOSTID, &hostid) == 0) {
					fnvlist_add_uint64(config,
					    ZPOOL_CONFIG_HOSTID, hostid);
					hostname = fnvlist_lookup_string(tmp,
					    ZPOOL_CONFIG_HOSTNAME);
					fnvlist_add_string(config,
					    ZPOOL_CONFIG_HOSTNAME, hostname);
				}

				config_seen = B_TRUE;
			}

			/*
			 * Add this top-level vdev to the child array.
			 */
			verify(nvlist_lookup_nvlist(tmp,
			    ZPOOL_CONFIG_VDEV_TREE, &nvtop) == 0);
			verify(nvlist_lookup_uint64(nvtop, ZPOOL_CONFIG_ID,
			    &id) == 0);

			if (id >= children) {
				nvlist_t **newchild;

				newchild = zfs_alloc(hdl, (id + 1) *
				    sizeof (nvlist_t *));
				if (newchild == NULL)
					goto nomem;

				for (c = 0; c < children; c++)
					newchild[c] = child[c];

				free(child);
				child = newchild;
				children = id + 1;
			}
			if (nvlist_dup(nvtop, &child[id], 0) != 0)
				goto nomem;

		}

		/*
		 * If we have information about all the top-levels then
		 * clean up the nvlist which we've constructed. This
		 * means removing any extraneous devices that are
		 * beyond the valid range or adding devices to the end
		 * of our array which appear to be missing.
		 */
		if (valid_top_config) {
			if (max_id < children) {
				for (c = max_id; c < children; c++)
					nvlist_free(child[c]);
				children = max_id;
			} else if (max_id > children) {
				nvlist_t **newchild;

				newchild = zfs_alloc(hdl, (max_id) *
				    sizeof (nvlist_t *));
				if (newchild == NULL)
					goto nomem;

				for (c = 0; c < children; c++)
					newchild[c] = child[c];

				free(child);
				child = newchild;
				children = max_id;
			}
		}

		verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID,
		    &guid) == 0);

		/*
		 * The vdev namespace may contain holes as a result of
		 * device removal. We must add them back into the vdev
		 * tree before we process any missing devices.
		 */
		if (holes > 0) {
			ASSERT(valid_top_config);

			for (c = 0; c < children; c++) {
				nvlist_t *holey;

				if (child[c] != NULL ||
				    !vdev_is_hole(hole_array, holes, c))
					continue;

				if (nvlist_alloc(&holey, NV_UNIQUE_NAME,
				    0) != 0)
					goto nomem;

				/*
				 * Holes in the namespace are treated as
				 * "hole" top-level vdevs and have a
				 * special flag set on them.
				 */
				if (nvlist_add_string(holey,
				    ZPOOL_CONFIG_TYPE,
				    VDEV_TYPE_HOLE) != 0 ||
				    nvlist_add_uint64(holey,
				    ZPOOL_CONFIG_ID, c) != 0 ||
				    nvlist_add_uint64(holey,
				    ZPOOL_CONFIG_GUID, 0ULL) != 0) {
					nvlist_free(holey);
					goto nomem;
				}
				child[c] = holey;
			}
		}

		/*
		 * Look for any missing top-level vdevs.  If this is the case,
		 * create a faked up 'missing' vdev as a placeholder.  We cannot
		 * simply compress the child array, because the kernel performs
		 * certain checks to make sure the vdev IDs match their location
		 * in the configuration.
		 */
		for (c = 0; c < children; c++) {
			if (child[c] == NULL) {
				nvlist_t *missing;
				if (nvlist_alloc(&missing, NV_UNIQUE_NAME,
				    0) != 0)
					goto nomem;
				if (nvlist_add_string(missing,
				    ZPOOL_CONFIG_TYPE,
				    VDEV_TYPE_MISSING) != 0 ||
				    nvlist_add_uint64(missing,
				    ZPOOL_CONFIG_ID, c) != 0 ||
				    nvlist_add_uint64(missing,
				    ZPOOL_CONFIG_GUID, 0ULL) != 0) {
					nvlist_free(missing);
					goto nomem;
				}
				child[c] = missing;
			}
		}

		/*
		 * Put all of this pool's top-level vdevs into a root vdev.
		 */
		if (nvlist_alloc(&nvroot, NV_UNIQUE_NAME, 0) != 0)
			goto nomem;
		if (nvlist_add_string(nvroot, ZPOOL_CONFIG_TYPE,
		    VDEV_TYPE_ROOT) != 0 ||
		    nvlist_add_uint64(nvroot, ZPOOL_CONFIG_ID, 0ULL) != 0 ||
		    nvlist_add_uint64(nvroot, ZPOOL_CONFIG_GUID, guid) != 0 ||
		    nvlist_add_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN,
		    child, children) != 0) {
			nvlist_free(nvroot);
			goto nomem;
		}

		for (c = 0; c < children; c++)
			nvlist_free(child[c]);
		free(child);
		children = 0;
		child = NULL;

		/*
		 * Go through and fix up any paths and/or devids based on our
		 * known list of vdev GUID -> path mappings.
		 */
		if (fix_paths(nvroot, pl->names) != 0) {
			nvlist_free(nvroot);
			goto nomem;
		}

		/*
		 * Add the root vdev to this pool's configuration.
		 */
		if (nvlist_add_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
		    nvroot) != 0) {
			nvlist_free(nvroot);
			goto nomem;
		}
		nvlist_free(nvroot);

		/*
		 * zdb uses this path to report on active pools that were
		 * imported or created using -R.
		 */
		if (active_ok)
			goto add_pool;

		/*
		 * Determine if this pool is currently active, in which case we
		 * can't actually import it.
		 */
		verify(nvlist_lookup_string(config, ZPOOL_CONFIG_POOL_NAME,
		    &name) == 0);
		verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID,
		    &guid) == 0);

		if (pool_active(hdl, name, guid, &isactive) != 0)
			goto error;

		if (isactive) {
			nvlist_free(config);
			config = NULL;
			continue;
		}

		if ((nvl = refresh_config(hdl, config)) == NULL) {
			nvlist_free(config);
			config = NULL;
			continue;
		}

		nvlist_free(config);
		config = nvl;

		/*
		 * Go through and update the paths for spares, now that we have
		 * them.
		 */
		verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
		    &nvroot) == 0);
		if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_SPARES,
		    &spares, &nspares) == 0) {
			for (i = 0; i < nspares; i++) {
				if (fix_paths(spares[i], pl->names) != 0)
					goto nomem;
			}
		}

		/*
		 * Update the paths for l2cache devices.
		 */
		if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_L2CACHE,
		    &l2cache, &nl2cache) == 0) {
			for (i = 0; i < nl2cache; i++) {
				if (fix_paths(l2cache[i], pl->names) != 0)
					goto nomem;
			}
		}

		/*
		 * Restore the original information read from the actual label.
		 */
		(void) nvlist_remove(config, ZPOOL_CONFIG_HOSTID,
		    DATA_TYPE_UINT64);
		(void) nvlist_remove(config, ZPOOL_CONFIG_HOSTNAME,
		    DATA_TYPE_STRING);
		if (hostid != 0) {
			verify(nvlist_add_uint64(config, ZPOOL_CONFIG_HOSTID,
			    hostid) == 0);
			verify(nvlist_add_string(config, ZPOOL_CONFIG_HOSTNAME,
			    hostname) == 0);
		}

add_pool:
		/*
		 * Add this pool to the list of configs.
		 */
		verify(nvlist_lookup_string(config, ZPOOL_CONFIG_POOL_NAME,
		    &name) == 0);
		if (nvlist_add_nvlist(ret, name, config) != 0)
			goto nomem;

		nvlist_free(config);
		config = NULL;
	}

	return (ret);

nomem:
	(void) no_memory(hdl);
error:
	nvlist_free(config);
	nvlist_free(ret);
	for (c = 0; c < children; c++)
		nvlist_free(child[c]);
	free(child);

	return (NULL);
}

/*
 * Return the offset of the given label.
 */
static uint64_t
label_offset(uint64_t size, int l)
{
	ASSERT(P2PHASE_TYPED(size, sizeof (vdev_label_t), uint64_t) == 0);
	return (l * sizeof (vdev_label_t) + (l < VDEV_LABELS / 2 ?
	    0 : size - VDEV_LABELS * sizeof (vdev_label_t)));
}

/*
 * Given a file descriptor, read the label information and return an nvlist
 * describing the configuration, if there is one.  The number of valid
 * labels found will be returned in num_labels when non-NULL.
 */
int
zpool_read_label(int fd, nvlist_t **config, int *num_labels)
{
	struct stat64 statbuf;
	int l, count = 0;
	vdev_label_t *label;
	nvlist_t *expected_config = NULL;
	uint64_t expected_guid = 0, size;
	int error;

	*config = NULL;

	if (fstat64_blk(fd, &statbuf) == -1)
		return (0);
	size = P2ALIGN_TYPED(statbuf.st_size, sizeof (vdev_label_t), uint64_t);

	error = posix_memalign((void **)&label, PAGESIZE, sizeof (*label));
	if (error)
		return (-1);

	for (l = 0; l < VDEV_LABELS; l++) {
		uint64_t state, guid, txg;

		if (pread64(fd, label, sizeof (vdev_label_t),
		    label_offset(size, l)) != sizeof (vdev_label_t))
			continue;

		if (nvlist_unpack(label->vl_vdev_phys.vp_nvlist,
		    sizeof (label->vl_vdev_phys.vp_nvlist), config, 0) != 0)
			continue;

		if (nvlist_lookup_uint64(*config, ZPOOL_CONFIG_GUID,
		    &guid) != 0 || guid == 0) {
			nvlist_free(*config);
			continue;
		}

		if (nvlist_lookup_uint64(*config, ZPOOL_CONFIG_POOL_STATE,
		    &state) != 0 || state > POOL_STATE_L2CACHE) {
			nvlist_free(*config);
			continue;
		}

		if (state != POOL_STATE_SPARE && state != POOL_STATE_L2CACHE &&
		    (nvlist_lookup_uint64(*config, ZPOOL_CONFIG_POOL_TXG,
		    &txg) != 0 || txg == 0)) {
			nvlist_free(*config);
			continue;
		}

		if (expected_guid) {
			if (expected_guid == guid)
				count++;

			nvlist_free(*config);
		} else {
			expected_config = *config;
			expected_guid = guid;
			count++;
		}
	}

	if (num_labels != NULL)
		*num_labels = count;

	free(label);
	*config = expected_config;

	return (0);
}

typedef struct rdsk_node {
	char *rn_name;			/* Full path to device */
	int rn_order;			/* Preferred order (low to high) */
	int rn_num_labels;		/* Number of valid labels */
	uint64_t rn_vdev_guid;		/* Expected vdev guid when set */
	libzfs_handle_t *rn_hdl;
	nvlist_t *rn_config;		/* Label config */
	avl_tree_t *rn_avl;
	avl_node_t rn_node;
	kmutex_t *rn_lock;
	boolean_t rn_labelpaths;
} rdsk_node_t;

/*
 * Sorted by vdev guid and full path to allow for multiple entries with
 * the same full path name.  This is required because it's possible to
 * have multiple block devices with labels that refer to the same
 * ZPOOL_CONFIG_PATH yet have different vdev guids.  In this case both
 * entries need to be added to the cache.  Scenarios where this can occur
 * include overwritten pool labels, devices which are visible from multiple
 * hosts and multipath devices.
 */
static int
slice_cache_compare(const void *arg1, const void *arg2)
{
	const char  *nm1 = ((rdsk_node_t *)arg1)->rn_name;
	const char  *nm2 = ((rdsk_node_t *)arg2)->rn_name;
	uint64_t guid1 = ((rdsk_node_t *)arg1)->rn_vdev_guid;
	uint64_t guid2 = ((rdsk_node_t *)arg2)->rn_vdev_guid;
	int rv;

	rv = AVL_CMP(guid1, guid2);
	if (rv)
		return (rv);

	return (AVL_ISIGN(strcmp(nm1, nm2)));
}

static boolean_t
is_watchdog_dev(char *dev)
{
	/* For 'watchdog' dev */
	if (strcmp(dev, "watchdog") == 0)
		return (B_TRUE);

	/* For 'watchdog<digit><whatever> */
	if (strstr(dev, "watchdog") == dev && isdigit(dev[8]))
		return (B_TRUE);

	return (B_FALSE);
}

static int
label_paths_impl(libzfs_handle_t *hdl, nvlist_t *nvroot, uint64_t pool_guid,
    uint64_t vdev_guid, char **path, char **devid)
{
	nvlist_t **child;
	uint_t c, children;
	uint64_t guid;
	char *val;
	int error;

	if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) == 0) {
		for (c = 0; c < children; c++) {
			error  = label_paths_impl(hdl, child[c],
			    pool_guid, vdev_guid, path, devid);
			if (error)
				return (error);
		}
		return (0);
	}

	if (nvroot == NULL)
		return (0);

	error = nvlist_lookup_uint64(nvroot, ZPOOL_CONFIG_GUID, &guid);
	if ((error != 0) || (guid != vdev_guid))
		return (0);

	error = nvlist_lookup_string(nvroot, ZPOOL_CONFIG_PATH, &val);
	if (error == 0)
		*path = val;

	error = nvlist_lookup_string(nvroot, ZPOOL_CONFIG_DEVID, &val);
	if (error == 0)
		*devid = val;

	return (0);
}

/*
 * Given a disk label fetch the ZPOOL_CONFIG_PATH and ZPOOL_CONFIG_DEVID
 * and store these strings as config_path and devid_path respectively.
 * The returned pointers are only valid as long as label remains valid.
 */
static int
label_paths(libzfs_handle_t *hdl, nvlist_t *label, char **path, char **devid)
{
	nvlist_t *nvroot;
	uint64_t pool_guid;
	uint64_t vdev_guid;

	*path = NULL;
	*devid = NULL;

	if (nvlist_lookup_nvlist(label, ZPOOL_CONFIG_VDEV_TREE, &nvroot) ||
	    nvlist_lookup_uint64(label, ZPOOL_CONFIG_POOL_GUID, &pool_guid) ||
	    nvlist_lookup_uint64(label, ZPOOL_CONFIG_GUID, &vdev_guid))
		return (ENOENT);

	return (label_paths_impl(hdl, nvroot, pool_guid, vdev_guid, path,
	    devid));
}

static void
zpool_open_func(void *arg)
{
	rdsk_node_t *rn = arg;
	libzfs_handle_t *hdl = rn->rn_hdl;
	struct stat64 statbuf;
	nvlist_t *config;
	char *bname, *dupname;
	uint64_t vdev_guid = 0;
	int error;
	int num_labels;
	int fd;

	/*
	 * Skip devices with well known prefixes there can be side effects
	 * when opening devices which need to be avoided.
	 *
	 * hpet     - High Precision Event Timer
	 * watchdog - Watchdog must be closed in a special way.
	 */
	dupname = zfs_strdup(hdl, rn->rn_name);
	bname = basename(dupname);
	error = ((strcmp(bname, "hpet") == 0) || is_watchdog_dev(bname));
	free(dupname);
	if (error)
		return;

	/*
	 * Ignore failed stats.  We only want regular files and block devices.
	 */
	if (stat64(rn->rn_name, &statbuf) != 0 ||
	    (!S_ISREG(statbuf.st_mode) && !S_ISBLK(statbuf.st_mode)))
		return;

	/*
	 * Preferentially open using O_DIRECT to bypass the block device
	 * cache which may be stale for multipath devices.  An EINVAL errno
	 * indicates O_DIRECT is unsupported so fallback to just O_RDONLY.
	 */
	fd = open(rn->rn_name, O_RDONLY | O_DIRECT);
	if ((fd < 0) && (errno == EINVAL))
		fd = open(rn->rn_name, O_RDONLY);

	if (fd < 0)
		return;

	/*
	 * This file is too small to hold a zpool
	 */
	if (S_ISREG(statbuf.st_mode) && statbuf.st_size < SPA_MINDEVSIZE) {
		(void) close(fd);
		return;
	}

	error = zpool_read_label(fd, &config, &num_labels);
	if (error != 0) {
		(void) close(fd);
		return;
	}

	if (num_labels == 0) {
		(void) close(fd);
		nvlist_free(config);
		return;
	}

	/*
	 * Check that the vdev is for the expected guid.  Additional entries
	 * are speculatively added based on the paths stored in the labels.
	 * Entries with valid paths but incorrect guids must be removed.
	 */
	error = nvlist_lookup_uint64(config, ZPOOL_CONFIG_GUID, &vdev_guid);
	if (error || (rn->rn_vdev_guid && rn->rn_vdev_guid != vdev_guid)) {
		(void) close(fd);
		nvlist_free(config);
		return;
	}

	(void) close(fd);

	rn->rn_config = config;
	rn->rn_num_labels = num_labels;

	/*
	 * Add additional entries for paths described by this label.
	 */
	if (rn->rn_labelpaths) {
		char *path = NULL;
		char *devid = NULL;
		rdsk_node_t *slice;
		avl_index_t where;
		int error;

		if (label_paths(rn->rn_hdl, rn->rn_config, &path, &devid))
			return;

		/*
		 * Allow devlinks to stabilize so all paths are available.
		 */
		zpool_label_disk_wait(rn->rn_name, DISK_LABEL_WAIT);

		if (path != NULL) {
			slice = zfs_alloc(hdl, sizeof (rdsk_node_t));
			slice->rn_name = zfs_strdup(hdl, path);
			slice->rn_vdev_guid = vdev_guid;
			slice->rn_avl = rn->rn_avl;
			slice->rn_hdl = hdl;
			slice->rn_order = IMPORT_ORDER_PREFERRED_1;
			slice->rn_labelpaths = B_FALSE;
			mutex_enter(rn->rn_lock);
			if (avl_find(rn->rn_avl, slice, &where)) {
			mutex_exit(rn->rn_lock);
				free(slice->rn_name);
				free(slice);
			} else {
				avl_insert(rn->rn_avl, slice, where);
				mutex_exit(rn->rn_lock);
				zpool_open_func(slice);
			}
		}

		if (devid != NULL) {
			slice = zfs_alloc(hdl, sizeof (rdsk_node_t));
			error = asprintf(&slice->rn_name, "%s%s",
			    DEV_BYID_PATH, devid);
			if (error == -1) {
				free(slice);
				return;
			}

			slice->rn_vdev_guid = vdev_guid;
			slice->rn_avl = rn->rn_avl;
			slice->rn_hdl = hdl;
			slice->rn_order = IMPORT_ORDER_PREFERRED_2;
			slice->rn_labelpaths = B_FALSE;
			mutex_enter(rn->rn_lock);
			if (avl_find(rn->rn_avl, slice, &where)) {
				mutex_exit(rn->rn_lock);
				free(slice->rn_name);
				free(slice);
			} else {
				avl_insert(rn->rn_avl, slice, where);
				mutex_exit(rn->rn_lock);
				zpool_open_func(slice);
			}
		}
	}
}

/*
 * Given a file descriptor, clear (zero) the label information.  This function
 * is used in the appliance stack as part of the ZFS sysevent module and
 * to implement the "zpool labelclear" command.
 */
int
zpool_clear_label(int fd)
{
	struct stat64 statbuf;
	int l;
	vdev_label_t *label;
	uint64_t size;

	if (fstat64_blk(fd, &statbuf) == -1)
		return (0);
	size = P2ALIGN_TYPED(statbuf.st_size, sizeof (vdev_label_t), uint64_t);

	if ((label = calloc(sizeof (vdev_label_t), 1)) == NULL)
		return (-1);

	for (l = 0; l < VDEV_LABELS; l++) {
		if (pwrite64(fd, label, sizeof (vdev_label_t),
		    label_offset(size, l)) != sizeof (vdev_label_t)) {
			free(label);
			return (-1);
		}
	}

	free(label);
	return (0);
}

/*
 * Scan a list of directories for zfs devices.
 */
static int
zpool_find_import_scan(libzfs_handle_t *hdl, kmutex_t *lock,
    avl_tree_t **slice_cache, char **dir, int dirs)
{
	avl_tree_t *cache;
	rdsk_node_t *slice;
	void *cookie;
	int i, error;

	*slice_cache = NULL;
	cache = zfs_alloc(hdl, sizeof (avl_tree_t));
	avl_create(cache, slice_cache_compare, sizeof (rdsk_node_t),
	    offsetof(rdsk_node_t, rn_node));

	for (i = 0; i < dirs; i++) {
		char path[MAXPATHLEN];
		struct dirent64 *dp;
		DIR *dirp;

		if (realpath(dir[i], path) == NULL) {
			error = errno;
			if (error == ENOENT)
				continue;

			zfs_error_aux(hdl, strerror(error));
			(void) zfs_error_fmt(hdl, EZFS_BADPATH, dgettext(
			    TEXT_DOMAIN, "cannot resolve path '%s'"), dir[i]);
			goto error;
		}

		dirp = opendir(path);
		if (dirp == NULL) {
			error = errno;
			zfs_error_aux(hdl, strerror(error));
			(void) zfs_error_fmt(hdl, EZFS_BADPATH,
			    dgettext(TEXT_DOMAIN, "cannot open '%s'"), path);
			goto error;
		}

		while ((dp = readdir64(dirp)) != NULL) {
			const char *name = dp->d_name;
			if (name[0] == '.' &&
			    (name[1] == 0 || (name[1] == '.' && name[2] == 0)))
				continue;

			slice = zfs_alloc(hdl, sizeof (rdsk_node_t));
			error = asprintf(&slice->rn_name, "%s/%s", path, name);
			if (error == -1) {
				free(slice);
				continue;
			}
			slice->rn_vdev_guid = 0;
			slice->rn_lock = lock;
			slice->rn_avl = cache;
			slice->rn_hdl = hdl;
			slice->rn_order = i + IMPORT_ORDER_SCAN_OFFSET;
			slice->rn_labelpaths = B_FALSE;
			mutex_enter(lock);
			avl_add(cache, slice);
			mutex_exit(lock);
		}

		(void) closedir(dirp);
	}

	*slice_cache = cache;
	return (0);

error:
	cookie = NULL;
	while ((slice = avl_destroy_nodes(cache, &cookie)) != NULL) {
		free(slice->rn_name);
		free(slice);
	}
	free(cache);

	return (error);
}

/*
 * Use libblkid to quickly enumerate all known zfs devices.
 */
static int
zpool_find_import_blkid(libzfs_handle_t *hdl, kmutex_t *lock,
    avl_tree_t **slice_cache)
{
	rdsk_node_t *slice;
	blkid_cache cache;
	blkid_dev_iterate iter;
	blkid_dev dev;
	avl_index_t where;
	int error;

	*slice_cache = NULL;

	error = blkid_get_cache(&cache, NULL);
	if (error != 0)
		return (error);

	error = blkid_probe_all_new(cache);
	if (error != 0) {
		blkid_put_cache(cache);
		return (error);
	}

	iter = blkid_dev_iterate_begin(cache);
	if (iter == NULL) {
		blkid_put_cache(cache);
		return (EINVAL);
	}

	error = blkid_dev_set_search(iter, "TYPE", "zfs_member");
	if (error != 0) {
		blkid_dev_iterate_end(iter);
		blkid_put_cache(cache);
		return (error);
	}

	*slice_cache = zfs_alloc(hdl, sizeof (avl_tree_t));
	avl_create(*slice_cache, slice_cache_compare, sizeof (rdsk_node_t),
	    offsetof(rdsk_node_t, rn_node));

	while (blkid_dev_next(iter, &dev) == 0) {
		slice = zfs_alloc(hdl, sizeof (rdsk_node_t));
		slice->rn_name = zfs_strdup(hdl, blkid_dev_devname(dev));
		slice->rn_vdev_guid = 0;
		slice->rn_lock = lock;
		slice->rn_avl = *slice_cache;
		slice->rn_hdl = hdl;
		slice->rn_labelpaths = B_TRUE;

		error = zfs_path_order(slice->rn_name, &slice->rn_order);
		if (error == 0)
			slice->rn_order += IMPORT_ORDER_SCAN_OFFSET;
		else
			slice->rn_order = IMPORT_ORDER_DEFAULT;

		mutex_enter(lock);
		if (avl_find(*slice_cache, slice, &where)) {
			free(slice->rn_name);
			free(slice);
		} else {
			avl_insert(*slice_cache, slice, where);
		}
		mutex_exit(lock);
	}

	blkid_dev_iterate_end(iter);
	blkid_put_cache(cache);

	return (0);
}

char *
zpool_default_import_path[DEFAULT_IMPORT_PATH_SIZE] = {
	"/dev/disk/by-vdev",	/* Custom rules, use first if they exist */
	"/dev/mapper",		/* Use multipath devices before components */
	"/dev/disk/by-partlabel", /* Single unique entry set by user */
	"/dev/disk/by-partuuid", /* Generated partition uuid */
	"/dev/disk/by-label",	/* Custom persistent labels */
	"/dev/disk/by-uuid",	/* Single unique entry and persistent */
	"/dev/disk/by-id",	/* May be multiple entries and persistent */
	"/dev/disk/by-path",	/* Encodes physical location and persistent */
	"/dev"			/* UNSAFE device names will change */
};

/*
 * Given a list of directories to search, find all pools stored on disk.  This
 * includes partial pools which are not available to import.  If no args are
 * given (argc is 0), then the default directory (/dev/dsk) is searched.
 * poolname or guid (but not both) are provided by the caller when trying
 * to import a specific pool.
 */
static nvlist_t *
zpool_find_import_impl(libzfs_handle_t *hdl, importargs_t *iarg)
{
	nvlist_t *ret = NULL;
	pool_list_t pools = { 0 };
	pool_entry_t *pe, *penext;
	vdev_entry_t *ve, *venext;
	config_entry_t *ce, *cenext;
	name_entry_t *ne, *nenext;
	kmutex_t lock;
	avl_tree_t *cache;
	rdsk_node_t *slice;
	void *cookie;
	taskq_t *t;

	verify(iarg->poolname == NULL || iarg->guid == 0);
	mutex_init(&lock, NULL, MUTEX_DEFAULT, NULL);

	/*
	 * Locate pool member vdevs using libblkid or by directory scanning.
	 * On success a newly allocated AVL tree which is populated with an
	 * entry for each discovered vdev will be returned as the cache.
	 * It's the callers responsibility to consume and destroy this tree.
	 */
	if (iarg->scan || iarg->paths != 0) {
		int dirs = iarg->paths;
		char **dir = iarg->path;

		if (dirs == 0) {
			dir = zpool_default_import_path;
			dirs = DEFAULT_IMPORT_PATH_SIZE;
		}

		if (zpool_find_import_scan(hdl, &lock, &cache, dir,  dirs) != 0)
			return (NULL);
	} else {
		if (zpool_find_import_blkid(hdl, &lock, &cache) != 0)
			return (NULL);
	}

	/*
	 * Create a thread pool to parallelize the process of reading and
	 * validating labels, a large number of threads can be used due to
	 * minimal contention.
	 */
	t = taskq_create("z_import", 2 * boot_ncpus, defclsyspri,
	    2 * boot_ncpus, INT_MAX, TASKQ_PREPOPULATE);

	for (slice = avl_first(cache); slice;
	    (slice = avl_walk(cache, slice, AVL_AFTER)))
		(void) taskq_dispatch(t, zpool_open_func, slice, TQ_SLEEP);

	taskq_wait(t);
	taskq_destroy(t);

	/*
	 * Process the cache filtering out any entries which are not
	 * for the specificed pool then adding matching label configs.
	 */
	cookie = NULL;
	while ((slice = avl_destroy_nodes(cache, &cookie)) != NULL) {
		if (slice->rn_config != NULL) {
			nvlist_t *config = slice->rn_config;
			boolean_t matched = B_TRUE;
			boolean_t aux = B_FALSE;
			int fd;

			/*
			 * Check if it's a spare or l2cache device. If it is,
			 * we need to skip the name and guid check since they
			 * don't exist on aux device label.
			 */
			if (iarg->poolname != NULL || iarg->guid != 0) {
				uint64_t state;
				aux = nvlist_lookup_uint64(config,
				    ZPOOL_CONFIG_POOL_STATE, &state) == 0 &&
				    (state == POOL_STATE_SPARE ||
				    state == POOL_STATE_L2CACHE);
			}

			if (iarg->poolname != NULL && !aux) {
				char *pname;

				matched = nvlist_lookup_string(config,
				    ZPOOL_CONFIG_POOL_NAME, &pname) == 0 &&
				    strcmp(iarg->poolname, pname) == 0;
			} else if (iarg->guid != 0 && !aux) {
				uint64_t this_guid;

				matched = nvlist_lookup_uint64(config,
				    ZPOOL_CONFIG_POOL_GUID, &this_guid) == 0 &&
				    iarg->guid == this_guid;
			}
			if (!matched) {
				nvlist_free(config);
			} else {
				/*
				 * Verify all remaining entries can be opened
				 * exclusively. This will prune all underlying
				 * multipath devices which otherwise could
				 * result in the vdev appearing as UNAVAIL.
				 *
				 * Under zdb, this step isn't required and
				 * would prevent a zdb -e of active pools with
				 * no cachefile.
				 */
				fd = open(slice->rn_name, O_RDONLY | O_EXCL);
				if (fd >= 0 || iarg->can_be_active) {
					if (fd >= 0)
						close(fd);
					add_config(hdl, &pools,
					    slice->rn_name, slice->rn_order,
					    slice->rn_num_labels, config);
				} else {
					nvlist_free(config);
				}
			}
		}
		free(slice->rn_name);
		free(slice);
	}
	avl_destroy(cache);
	free(cache);
	mutex_destroy(&lock);

	ret = get_configs(hdl, &pools, iarg->can_be_active);

	for (pe = pools.pools; pe != NULL; pe = penext) {
		penext = pe->pe_next;
		for (ve = pe->pe_vdevs; ve != NULL; ve = venext) {
			venext = ve->ve_next;
			for (ce = ve->ve_configs; ce != NULL; ce = cenext) {
				cenext = ce->ce_next;
				nvlist_free(ce->ce_config);
				free(ce);
			}
			free(ve);
		}
		free(pe);
	}

	for (ne = pools.names; ne != NULL; ne = nenext) {
		nenext = ne->ne_next;
		free(ne->ne_name);
		free(ne);
	}

	return (ret);
}

nvlist_t *
zpool_find_import(libzfs_handle_t *hdl, int argc, char **argv)
{
	importargs_t iarg = { 0 };

	iarg.paths = argc;
	iarg.path = argv;

	return (zpool_find_import_impl(hdl, &iarg));
}

/*
 * Given a cache file, return the contents as a list of importable pools.
 * poolname or guid (but not both) are provided by the caller when trying
 * to import a specific pool.
 */
nvlist_t *
zpool_find_import_cached(libzfs_handle_t *hdl, const char *cachefile,
    char *poolname, uint64_t guid)
{
	char *buf;
	int fd;
	struct stat64 statbuf;
	nvlist_t *raw, *src, *dst;
	nvlist_t *pools;
	nvpair_t *elem;
	char *name;
	uint64_t this_guid;
	boolean_t active;

	verify(poolname == NULL || guid == 0);

	if ((fd = open(cachefile, O_RDONLY)) < 0) {
		zfs_error_aux(hdl, "%s", strerror(errno));
		(void) zfs_error(hdl, EZFS_BADCACHE,
		    dgettext(TEXT_DOMAIN, "failed to open cache file"));
		return (NULL);
	}

	if (fstat64(fd, &statbuf) != 0) {
		zfs_error_aux(hdl, "%s", strerror(errno));
		(void) close(fd);
		(void) zfs_error(hdl, EZFS_BADCACHE,
		    dgettext(TEXT_DOMAIN, "failed to get size of cache file"));
		return (NULL);
	}

	if ((buf = zfs_alloc(hdl, statbuf.st_size)) == NULL) {
		(void) close(fd);
		return (NULL);
	}

	if (read(fd, buf, statbuf.st_size) != statbuf.st_size) {
		(void) close(fd);
		free(buf);
		(void) zfs_error(hdl, EZFS_BADCACHE,
		    dgettext(TEXT_DOMAIN,
		    "failed to read cache file contents"));
		return (NULL);
	}

	(void) close(fd);

	if (nvlist_unpack(buf, statbuf.st_size, &raw, 0) != 0) {
		free(buf);
		(void) zfs_error(hdl, EZFS_BADCACHE,
		    dgettext(TEXT_DOMAIN,
		    "invalid or corrupt cache file contents"));
		return (NULL);
	}

	free(buf);

	/*
	 * Go through and get the current state of the pools and refresh their
	 * state.
	 */
	if (nvlist_alloc(&pools, 0, 0) != 0) {
		(void) no_memory(hdl);
		nvlist_free(raw);
		return (NULL);
	}

	elem = NULL;
	while ((elem = nvlist_next_nvpair(raw, elem)) != NULL) {
		src = fnvpair_value_nvlist(elem);

		name = fnvlist_lookup_string(src, ZPOOL_CONFIG_POOL_NAME);
		if (poolname != NULL && strcmp(poolname, name) != 0)
			continue;

		this_guid = fnvlist_lookup_uint64(src, ZPOOL_CONFIG_POOL_GUID);
		if (guid != 0 && guid != this_guid)
			continue;

		if (pool_active(hdl, name, this_guid, &active) != 0) {
			nvlist_free(raw);
			nvlist_free(pools);
			return (NULL);
		}

		if (active)
			continue;

		if ((dst = refresh_config(hdl, src)) == NULL) {
			nvlist_free(raw);
			nvlist_free(pools);
			return (NULL);
		}

		if (nvlist_add_nvlist(pools, nvpair_name(elem), dst) != 0) {
			(void) no_memory(hdl);
			nvlist_free(dst);
			nvlist_free(raw);
			nvlist_free(pools);
			return (NULL);
		}
		nvlist_free(dst);
	}

	nvlist_free(raw);
	return (pools);
}

static int
name_or_guid_exists(zpool_handle_t *zhp, void *data)
{
	importargs_t *import = data;
	int found = 0;

	if (import->poolname != NULL) {
		char *pool_name;

		verify(nvlist_lookup_string(zhp->zpool_config,
		    ZPOOL_CONFIG_POOL_NAME, &pool_name) == 0);
		if (strcmp(pool_name, import->poolname) == 0)
			found = 1;
	} else {
		uint64_t pool_guid;

		verify(nvlist_lookup_uint64(zhp->zpool_config,
		    ZPOOL_CONFIG_POOL_GUID, &pool_guid) == 0);
		if (pool_guid == import->guid)
			found = 1;
	}

	zpool_close(zhp);
	return (found);
}

nvlist_t *
zpool_search_import(libzfs_handle_t *hdl, importargs_t *import)
{
	verify(import->poolname == NULL || import->guid == 0);

	if (import->unique)
		import->exists = zpool_iter(hdl, name_or_guid_exists, import);

	if (import->cachefile != NULL)
		return (zpool_find_import_cached(hdl, import->cachefile,
		    import->poolname, import->guid));

	return (zpool_find_import_impl(hdl, import));
}

static boolean_t
pool_match(nvlist_t *cfg, char *tgt)
{
	uint64_t v, guid = strtoull(tgt, NULL, 0);
	char *s;

	if (guid != 0) {
		if (nvlist_lookup_uint64(cfg, ZPOOL_CONFIG_POOL_GUID, &v) == 0)
			return (v == guid);
	} else {
		if (nvlist_lookup_string(cfg, ZPOOL_CONFIG_POOL_NAME, &s) == 0)
			return (strcmp(s, tgt) == 0);
	}
	return (B_FALSE);
}

int
zpool_tryimport(libzfs_handle_t *hdl, char *target, nvlist_t **configp,
    importargs_t *args)
{
	nvlist_t *pools;
	nvlist_t *match = NULL;
	nvlist_t *config = NULL;
	char *name = NULL, *sepp = NULL;
	char sep = '\0';
	int count = 0;
	char *targetdup = strdup(target);

	*configp = NULL;

	if ((sepp = strpbrk(targetdup, "/@")) != NULL) {
		sep = *sepp;
		*sepp = '\0';
	}

	pools = zpool_search_import(hdl, args);

	if (pools != NULL) {
		nvpair_t *elem = NULL;
		while ((elem = nvlist_next_nvpair(pools, elem)) != NULL) {
			VERIFY0(nvpair_value_nvlist(elem, &config));
			if (pool_match(config, targetdup)) {
				count++;
				if (match != NULL) {
					/* multiple matches found */
					continue;
				} else {
					match = config;
					name = nvpair_name(elem);
				}
			}
		}
	}

	if (count == 0) {
		(void) zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "no pools found"));
		free(targetdup);
		return (ENOENT);
	}

	if (count > 1) {
		(void) zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "%d pools found, use pool GUID\n"), count);
		free(targetdup);
		return (EINVAL);
	}

	*configp = match;
	free(targetdup);

	return (0);
}

boolean_t
find_guid(nvlist_t *nv, uint64_t guid)
{
	uint64_t tmp;
	nvlist_t **child;
	uint_t c, children;

	verify(nvlist_lookup_uint64(nv, ZPOOL_CONFIG_GUID, &tmp) == 0);
	if (tmp == guid)
		return (B_TRUE);

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) == 0) {
		for (c = 0; c < children; c++)
			if (find_guid(child[c], guid))
				return (B_TRUE);
	}

	return (B_FALSE);
}

typedef struct aux_cbdata {
	const char	*cb_type;
	uint64_t	cb_guid;
	zpool_handle_t	*cb_zhp;
} aux_cbdata_t;

static int
find_aux(zpool_handle_t *zhp, void *data)
{
	aux_cbdata_t *cbp = data;
	nvlist_t **list;
	uint_t i, count;
	uint64_t guid;
	nvlist_t *nvroot;

	verify(nvlist_lookup_nvlist(zhp->zpool_config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) == 0);

	if (nvlist_lookup_nvlist_array(nvroot, cbp->cb_type,
	    &list, &count) == 0) {
		for (i = 0; i < count; i++) {
			verify(nvlist_lookup_uint64(list[i],
			    ZPOOL_CONFIG_GUID, &guid) == 0);
			if (guid == cbp->cb_guid) {
				cbp->cb_zhp = zhp;
				return (1);
			}
		}
	}

	zpool_close(zhp);
	return (0);
}

/*
 * Determines if the pool is in use.  If so, it returns true and the state of
 * the pool as well as the name of the pool.  Name string is allocated and
 * must be freed by the caller.
 */
int
zpool_in_use(libzfs_handle_t *hdl, int fd, pool_state_t *state, char **namestr,
    boolean_t *inuse)
{
	nvlist_t *config;
	char *name;
	boolean_t ret;
	uint64_t guid, vdev_guid;
	zpool_handle_t *zhp;
	nvlist_t *pool_config;
	uint64_t stateval, isspare;
	aux_cbdata_t cb = { 0 };
	boolean_t isactive;

	*inuse = B_FALSE;

	if (zpool_read_label(fd, &config, NULL) != 0) {
		(void) no_memory(hdl);
		return (-1);
	}

	if (config == NULL)
		return (0);

	verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_STATE,
	    &stateval) == 0);
	verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_GUID,
	    &vdev_guid) == 0);

	if (stateval != POOL_STATE_SPARE && stateval != POOL_STATE_L2CACHE) {
		verify(nvlist_lookup_string(config, ZPOOL_CONFIG_POOL_NAME,
		    &name) == 0);
		verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID,
		    &guid) == 0);
	}

	switch (stateval) {
	case POOL_STATE_EXPORTED:
		/*
		 * A pool with an exported state may in fact be imported
		 * read-only, so check the in-core state to see if it's
		 * active and imported read-only.  If it is, set
		 * its state to active.
		 */
		if (pool_active(hdl, name, guid, &isactive) == 0 && isactive &&
		    (zhp = zpool_open_canfail(hdl, name)) != NULL) {
			if (zpool_get_prop_int(zhp, ZPOOL_PROP_READONLY, NULL))
				stateval = POOL_STATE_ACTIVE;

			/*
			 * All we needed the zpool handle for is the
			 * readonly prop check.
			 */
			zpool_close(zhp);
		}

		ret = B_TRUE;
		break;

	case POOL_STATE_ACTIVE:
		/*
		 * For an active pool, we have to determine if it's really part
		 * of a currently active pool (in which case the pool will exist
		 * and the guid will be the same), or whether it's part of an
		 * active pool that was disconnected without being explicitly
		 * exported.
		 */
		if (pool_active(hdl, name, guid, &isactive) != 0) {
			nvlist_free(config);
			return (-1);
		}

		if (isactive) {
			/*
			 * Because the device may have been removed while
			 * offlined, we only report it as active if the vdev is
			 * still present in the config.  Otherwise, pretend like
			 * it's not in use.
			 */
			if ((zhp = zpool_open_canfail(hdl, name)) != NULL &&
			    (pool_config = zpool_get_config(zhp, NULL))
			    != NULL) {
				nvlist_t *nvroot;

				verify(nvlist_lookup_nvlist(pool_config,
				    ZPOOL_CONFIG_VDEV_TREE, &nvroot) == 0);
				ret = find_guid(nvroot, vdev_guid);
			} else {
				ret = B_FALSE;
			}

			/*
			 * If this is an active spare within another pool, we
			 * treat it like an unused hot spare.  This allows the
			 * user to create a pool with a hot spare that currently
			 * in use within another pool.  Since we return B_TRUE,
			 * libdiskmgt will continue to prevent generic consumers
			 * from using the device.
			 */
			if (ret && nvlist_lookup_uint64(config,
			    ZPOOL_CONFIG_IS_SPARE, &isspare) == 0 && isspare)
				stateval = POOL_STATE_SPARE;

			if (zhp != NULL)
				zpool_close(zhp);
		} else {
			stateval = POOL_STATE_POTENTIALLY_ACTIVE;
			ret = B_TRUE;
		}
		break;

	case POOL_STATE_SPARE:
		/*
		 * For a hot spare, it can be either definitively in use, or
		 * potentially active.  To determine if it's in use, we iterate
		 * over all pools in the system and search for one with a spare
		 * with a matching guid.
		 *
		 * Due to the shared nature of spares, we don't actually report
		 * the potentially active case as in use.  This means the user
		 * can freely create pools on the hot spares of exported pools,
		 * but to do otherwise makes the resulting code complicated, and
		 * we end up having to deal with this case anyway.
		 */
		cb.cb_zhp = NULL;
		cb.cb_guid = vdev_guid;
		cb.cb_type = ZPOOL_CONFIG_SPARES;
		if (zpool_iter(hdl, find_aux, &cb) == 1) {
			name = (char *)zpool_get_name(cb.cb_zhp);
			ret = B_TRUE;
		} else {
			ret = B_FALSE;
		}
		break;

	case POOL_STATE_L2CACHE:

		/*
		 * Check if any pool is currently using this l2cache device.
		 */
		cb.cb_zhp = NULL;
		cb.cb_guid = vdev_guid;
		cb.cb_type = ZPOOL_CONFIG_L2CACHE;
		if (zpool_iter(hdl, find_aux, &cb) == 1) {
			name = (char *)zpool_get_name(cb.cb_zhp);
			ret = B_TRUE;
		} else {
			ret = B_FALSE;
		}
		break;

	default:
		ret = B_FALSE;
	}


	if (ret) {
		if ((*namestr = zfs_strdup(hdl, name)) == NULL) {
			if (cb.cb_zhp)
				zpool_close(cb.cb_zhp);
			nvlist_free(config);
			return (-1);
		}
		*state = (pool_state_t)stateval;
	}

	if (cb.cb_zhp)
		zpool_close(cb.cb_zhp);

	nvlist_free(config);
	*inuse = ret;
	return (0);
}
