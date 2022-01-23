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
 * Used by zpool, ztest, zdb, and zhack to locate importable configs. Since
 * these commands are expected to run in the global zone, we can assume
 * that the devices are all readable when called.
 *
 * To import a pool, we rely on reading the configuration information from the
 * ZFS label of each device.  If we successfully read the label, then we
 * organize the configuration information in the following hierarchy:
 *
 *	pool guid -> toplevel vdev guid -> label txg
 *
 * Duplicate entries matching this same tuple will be discarded.  Once we have
 * examined every device, we pick the best label txg config for each toplevel
 * vdev.  We then arrange these toplevel vdevs into a complete pool config, and
 * update any paths that have changed.  Finally, we attempt to import the pool
 * using our derived config, and record the results.
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <libintl.h>
#include <libgen.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/dktp/fdisk.h>
#include <sys/vdev_impl.h>
#include <sys/fs/zfs.h>

#include <thread_pool.h>
#include <libzutil.h>
#include <libnvpair.h>
#include <libzfs.h>

#include "zutil_import.h"

#ifdef HAVE_LIBUDEV
#include <libudev.h>
#include <sched.h>
#endif
#include <blkid/blkid.h>

#define	DEV_BYID_PATH	"/dev/disk/by-id/"

/*
 * Skip devices with well known prefixes:
 * there can be side effects when opening devices which need to be avoided.
 *
 * hpet        - High Precision Event Timer
 * watchdog[N] - Watchdog must be closed in a special way.
 */
static boolean_t
should_skip_dev(const char *dev)
{
	return ((strcmp(dev, "watchdog") == 0) ||
	    (strncmp(dev, "watchdog", 8) == 0 && isdigit(dev[8])) ||
	    (strcmp(dev, "hpet") == 0));
}

int
zfs_dev_flush(int fd)
{
	return (ioctl(fd, BLKFLSBUF));
}

void
zpool_open_func(void *arg)
{
	rdsk_node_t *rn = arg;
	libpc_handle_t *hdl = rn->rn_hdl;
	struct stat64 statbuf;
	nvlist_t *config;
	uint64_t vdev_guid = 0;
	int error;
	int num_labels = 0;
	int fd;

	if (should_skip_dev(zfs_basename(rn->rn_name)))
		return;

	/*
	 * Ignore failed stats.  We only want regular files and block devices.
	 * Ignore files that are too small to hold a zpool.
	 */
	if (stat64(rn->rn_name, &statbuf) != 0 ||
	    (!S_ISREG(statbuf.st_mode) && !S_ISBLK(statbuf.st_mode)) ||
	    (S_ISREG(statbuf.st_mode) && statbuf.st_size < SPA_MINDEVSIZE))
		return;

	/*
	 * Preferentially open using O_DIRECT to bypass the block device
	 * cache which may be stale for multipath devices.  An EINVAL errno
	 * indicates O_DIRECT is unsupported so fallback to just O_RDONLY.
	 */
	fd = open(rn->rn_name, O_RDONLY | O_DIRECT | O_CLOEXEC);
	if ((fd < 0) && (errno == EINVAL))
		fd = open(rn->rn_name, O_RDONLY | O_CLOEXEC);
	if ((fd < 0) && (errno == EACCES))
		hdl->lpc_open_access_error = B_TRUE;
	if (fd < 0)
		return;

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
		char *env = NULL;
		rdsk_node_t *slice;
		avl_index_t where;
		int timeout;
		int error;

		if (label_paths(rn->rn_hdl, rn->rn_config, &path, &devid))
			return;

		env = getenv("ZPOOL_IMPORT_UDEV_TIMEOUT_MS");
		if ((env == NULL) || sscanf(env, "%d", &timeout) != 1 ||
		    timeout < 0) {
			timeout = DISK_LABEL_WAIT;
		}

		/*
		 * Allow devlinks to stabilize so all paths are available.
		 */
		zpool_label_disk_wait(rn->rn_name, timeout);

		if (path != NULL) {
			slice = zutil_alloc(hdl, sizeof (rdsk_node_t));
			slice->rn_name = zutil_strdup(hdl, path);
			slice->rn_vdev_guid = vdev_guid;
			slice->rn_avl = rn->rn_avl;
			slice->rn_hdl = hdl;
			slice->rn_order = IMPORT_ORDER_PREFERRED_1;
			slice->rn_labelpaths = B_FALSE;
			pthread_mutex_lock(rn->rn_lock);
			if (avl_find(rn->rn_avl, slice, &where)) {
			pthread_mutex_unlock(rn->rn_lock);
				free(slice->rn_name);
				free(slice);
			} else {
				avl_insert(rn->rn_avl, slice, where);
				pthread_mutex_unlock(rn->rn_lock);
				zpool_open_func(slice);
			}
		}

		if (devid != NULL) {
			slice = zutil_alloc(hdl, sizeof (rdsk_node_t));
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
			pthread_mutex_lock(rn->rn_lock);
			if (avl_find(rn->rn_avl, slice, &where)) {
				pthread_mutex_unlock(rn->rn_lock);
				free(slice->rn_name);
				free(slice);
			} else {
				avl_insert(rn->rn_avl, slice, where);
				pthread_mutex_unlock(rn->rn_lock);
				zpool_open_func(slice);
			}
		}
	}
}

static const char * const
zpool_default_import_path[] = {
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

const char * const *
zpool_default_search_paths(size_t *count)
{
	*count = ARRAY_SIZE(zpool_default_import_path);
	return (zpool_default_import_path);
}

/*
 * Given a full path to a device determine if that device appears in the
 * import search path.  If it does return the first match and store the
 * index in the passed 'order' variable, otherwise return an error.
 */
static int
zfs_path_order(char *name, int *order)
{
	int i, error = ENOENT;
	char *dir, *env, *envdup, *tmp = NULL;

	env = getenv("ZPOOL_IMPORT_PATH");
	if (env) {
		envdup = strdup(env);
		for (dir = strtok_r(envdup, ":", &tmp), i = 0;
		    dir != NULL;
		    dir = strtok_r(NULL, ":", &tmp), i++) {
			if (strncmp(name, dir, strlen(dir)) == 0) {
				*order = i;
				error = 0;
				break;
			}
		}
		free(envdup);
	} else {
		for (i = 0; i < ARRAY_SIZE(zpool_default_import_path); i++) {
			if (strncmp(name, zpool_default_import_path[i],
			    strlen(zpool_default_import_path[i])) == 0) {
				*order = i;
				error = 0;
				break;
			}
		}
	}

	return (error);
}

/*
 * Use libblkid to quickly enumerate all known zfs devices.
 */
int
zpool_find_import_blkid(libpc_handle_t *hdl, pthread_mutex_t *lock,
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

	*slice_cache = zutil_alloc(hdl, sizeof (avl_tree_t));
	avl_create(*slice_cache, slice_cache_compare, sizeof (rdsk_node_t),
	    offsetof(rdsk_node_t, rn_node));

	while (blkid_dev_next(iter, &dev) == 0) {
		slice = zutil_alloc(hdl, sizeof (rdsk_node_t));
		slice->rn_name = zutil_strdup(hdl, blkid_dev_devname(dev));
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

		pthread_mutex_lock(lock);
		if (avl_find(*slice_cache, slice, &where)) {
			free(slice->rn_name);
			free(slice);
		} else {
			avl_insert(*slice_cache, slice, where);
		}
		pthread_mutex_unlock(lock);
	}

	blkid_dev_iterate_end(iter);
	blkid_put_cache(cache);

	return (0);
}

/*
 * Linux persistent device strings for vdev labels
 *
 * based on libudev for consistency with libudev disk add/remove events
 */

typedef struct vdev_dev_strs {
	char	vds_devid[128];
	char	vds_devphys[128];
} vdev_dev_strs_t;

#ifdef HAVE_LIBUDEV

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
		 * For volumes use the persistent /dev/zvol/dataset identifier
		 */
		entry = udev_device_get_devlinks_list_entry(dev);
		while (entry != NULL) {
			const char *name;

			name = udev_list_entry_get_name(entry);
			if (strncmp(name, ZVOL_ROOT, strlen(ZVOL_ROOT)) == 0) {
				(void) strlcpy(bufptr, name, buflen);
				return (0);
			}
			entry = udev_list_entry_get_next(entry);
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
	struct udev_list_entry *entry;

	/*
	 * Normal disks use ID_PATH for their physical path.
	 */
	physpath = udev_device_get_property_value(dev, "ID_PATH");
	if (physpath != NULL && strlen(physpath) > 0) {
		(void) strlcpy(bufptr, physpath, buflen);
		return (0);
	}

	/*
	 * Device mapper devices are virtual and don't have a physical
	 * path. For them we use ID_VDEV instead, which is setup via the
	 * /etc/vdev_id.conf file.  ID_VDEV provides a persistent path
	 * to a virtual device.  If you don't have vdev_id.conf setup,
	 * you cannot use multipath autoreplace with device mapper.
	 */
	physpath = udev_device_get_property_value(dev, "ID_VDEV");
	if (physpath != NULL && strlen(physpath) > 0) {
		(void) strlcpy(bufptr, physpath, buflen);
		return (0);
	}

	/*
	 * For ZFS volumes use the persistent /dev/zvol/dataset identifier
	 */
	entry = udev_device_get_devlinks_list_entry(dev);
	while (entry != NULL) {
		physpath = udev_list_entry_get_name(entry);
		if (strncmp(physpath, ZVOL_ROOT, strlen(ZVOL_ROOT)) == 0) {
			(void) strlcpy(bufptr, physpath, buflen);
			return (0);
		}
		entry = udev_list_entry_get_next(entry);
	}

	/*
	 * For all other devices fallback to using the by-uuid name.
	 */
	entry = udev_device_get_devlinks_list_entry(dev);
	while (entry != NULL) {
		physpath = udev_list_entry_get_name(entry);
		if (strncmp(physpath, "/dev/disk/by-uuid", 17) == 0) {
			(void) strlcpy(bufptr, physpath, buflen);
			return (0);
		}
		entry = udev_list_entry_get_next(entry);
	}

	return (ENODATA);
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

#else

int
zfs_device_get_devid(struct udev_device *dev, char *bufptr, size_t buflen)
{
	(void) dev, (void) bufptr, (void) buflen;
	return (ENODATA);
}

int
zfs_device_get_physical(struct udev_device *dev, char *bufptr, size_t buflen)
{
	(void) dev, (void) bufptr, (void) buflen;
	return (ENODATA);
}

#endif /* HAVE_LIBUDEV */

/*
 * Wait up to timeout_ms for udev to set up the device node.  The device is
 * considered ready when libudev determines it has been initialized, all of
 * the device links have been verified to exist, and it has been allowed to
 * settle.  At this point the device the device can be accessed reliably.
 * Depending on the complexity of the udev rules this process could take
 * several seconds.
 */
int
zpool_label_disk_wait(const char *path, int timeout_ms)
{
#ifdef HAVE_LIBUDEV
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
#else
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
#endif /* HAVE_LIBUDEV */
}

/*
 * Encode the persistent devices strings
 * used for the vdev disk label
 */
static int
encode_device_strings(const char *path, vdev_dev_strs_t *ds,
    boolean_t wholedisk)
{
#ifdef HAVE_LIBUDEV
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
#else
	return (ENOENT);
#endif
}

/*
 * Rescan the enclosure sysfs path for turning on enclosure LEDs and store it
 * in the nvlist * (if applicable).  Like:
 *    vdev_enc_sysfs_path: '/sys/class/enclosure/11:0:1:0/SLOT 4'
 */
static void
update_vdev_config_dev_sysfs_path(nvlist_t *nv, char *path)
{
	char *upath, *spath;

	/* Add enclosure sysfs path (if disk is in an enclosure). */
	upath = zfs_get_underlying_path(path);
	spath = zfs_get_enclosure_sysfs_path(upath);

	if (spath) {
		nvlist_add_string(nv, ZPOOL_CONFIG_VDEV_ENC_SYSFS_PATH, spath);
	} else {
		nvlist_remove_all(nv, ZPOOL_CONFIG_VDEV_ENC_SYSFS_PATH);
	}

	free(upath);
	free(spath);
}

/*
 * This will get called for each leaf vdev.
 */
static int
sysfs_path_pool_vdev_iter_f(void *hdl_data, nvlist_t *nv, void *data)
{
	(void) hdl_data, (void) data;

	char *path = NULL;
	if (nvlist_lookup_string(nv, ZPOOL_CONFIG_PATH, &path) != 0)
		return (1);

	/* Rescan our enclosure sysfs path for this vdev */
	update_vdev_config_dev_sysfs_path(nv, path);
	return (0);
}

/*
 * Given an nvlist for our pool (with vdev tree), iterate over all the
 * leaf vdevs and update their ZPOOL_CONFIG_VDEV_ENC_SYSFS_PATH.
 */
void
update_vdevs_config_dev_sysfs_path(nvlist_t *config)
{
	nvlist_t *nvroot = NULL;
	verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) == 0);
	for_each_vdev_in_nvlist(nvroot, sysfs_path_pool_vdev_iter_f, NULL);
}

/*
 * Update a leaf vdev's persistent device strings
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

	/*
	 * For the benefit of legacy ZFS implementations, allow
	 * for opting out of devid strings in the vdev label.
	 *
	 * example use:
	 *	env ZFS_VDEV_DEVID_OPT_OUT=YES zpool import dozer
	 *
	 * explanation:
	 * Older OpenZFS implementations had issues when attempting to
	 * display pool config VDEV names if a "devid" NVP value is
	 * present in the pool's config.
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
	 * Update device string values in the config nvlist.
	 */
	if (encode_device_strings(path, &vds, (boolean_t)wholedisk) == 0) {
		(void) nvlist_add_string(nv, ZPOOL_CONFIG_DEVID, vds.vds_devid);
		if (vds.vds_devphys[0] != '\0') {
			(void) nvlist_add_string(nv, ZPOOL_CONFIG_PHYS_PATH,
			    vds.vds_devphys);
		}
		update_vdev_config_dev_sysfs_path(nv, path);
	} else {
		/* Clear out any stale entries. */
		(void) nvlist_remove_all(nv, ZPOOL_CONFIG_DEVID);
		(void) nvlist_remove_all(nv, ZPOOL_CONFIG_PHYS_PATH);
		(void) nvlist_remove_all(nv, ZPOOL_CONFIG_VDEV_ENC_SYSFS_PATH);
	}
}
