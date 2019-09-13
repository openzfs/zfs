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
#include <devid.h>
#include <dirent.h>
#include <errno.h>
#include <libintl.h>
#include <libgen.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/dktp/fdisk.h>
#include <sys/vdev_impl.h>
#include <sys/fs/zfs.h>
#include <sys/vdev_impl.h>

#include <thread_pool.h>
#include <libzutil.h>
#include <libnvpair.h>

#ifdef HAVE_LIBUDEV
#include <libudev.h>
#include <sched.h>
#endif

#define	EZFS_BADCACHE	"invalid or missing cache file"
#define	EZFS_NOMEM	"out of memory"
#define	EZFS_EACESS	"some devices require root privileges"
#define	DEV_BYID_PATH	"/dev/disk/by-id/"

struct libpc_handle {
	boolean_t lpc_printerr;
	boolean_t lpc_open_access_error;
	boolean_t lpc_desc_active;
	char lpc_desc[1024];
	const pool_config_ops_t *lpc_ops;
	void *lpc_lib_handle;
};

/*PRINTFLIKE2*/
void
zutil_error_aux(libpc_handle_t *hdl, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);

	(void) vsnprintf(hdl->lpc_desc, sizeof (hdl->lpc_desc), fmt, ap);
	hdl->lpc_desc_active = B_TRUE;

	va_end(ap);
}

static void
zfs_verror(libpc_handle_t *hdl, const char *error, const char *fmt, va_list ap)
{
	char action[1024];

	(void) vsnprintf(action, sizeof (action), fmt, ap);

	if (hdl->lpc_desc_active)
		hdl->lpc_desc_active = B_FALSE;
	else
		hdl->lpc_desc[0] = '\0';

	if (hdl->lpc_printerr) {
		if (hdl->lpc_desc[0] != '\0')
			error = hdl->lpc_desc;

		(void) fprintf(stderr, "%s: %s\n", action, error);
	}
}

/*PRINTFLIKE3*/
int
zutil_error_fmt(libpc_handle_t *hdl, const char *error, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);

	zfs_verror(hdl, error, fmt, ap);

	va_end(ap);

	return (-1);
}

int
zutil_error(libpc_handle_t *hdl, const char *error, const char *msg)
{
	return (zutil_error_fmt(hdl, error, "%s", msg));
}

int
zutil_no_memory(libpc_handle_t *hdl)
{
	zutil_error(hdl, EZFS_NOMEM, "internal error");
	exit(1);
}

void *
zutil_alloc(libpc_handle_t *hdl, size_t size)
{
	void *data;

	if ((data = calloc(1, size)) == NULL)
		(void) zutil_no_memory(hdl);

	return (data);
}

char *
zutil_strdup(libpc_handle_t *hdl, const char *str)
{
	char *ret;

	if ((ret = strdup(str)) == NULL)
		(void) zutil_no_memory(hdl);

	return (ret);
}

#define	ZVOL_ROOT	"/dev/zvol"

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

int
zutil_pool_active(libpc_handle_t *hdl, const char *name, uint64_t guid,
    boolean_t *isactive)
{
	ASSERT(hdl->lpc_ops->pco_pool_active != NULL);

	int error = hdl->lpc_ops->pco_pool_active(hdl->lpc_lib_handle, name,
	    guid, isactive);

	return (error);
}

nvlist_t *
zutil_refresh_config(libpc_handle_t *hdl, nvlist_t *tryconfig)
{
	ASSERT(hdl->lpc_ops->pco_refresh_config != NULL);

	return (hdl->lpc_ops->pco_refresh_config(hdl->lpc_lib_handle,
	    tryconfig));
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

/*
 * Given a cache file, return the contents as a list of importable pools.
 * poolname or guid (but not both) are provided by the caller when trying
 * to import a specific pool.
 */
static nvlist_t *
zpool_find_import_cached(libpc_handle_t *hdl, const char *cachefile,
    const char *poolname, uint64_t guid)
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
		zutil_error_aux(hdl, "%s", strerror(errno));
		(void) zutil_error(hdl, EZFS_BADCACHE,
		    dgettext(TEXT_DOMAIN, "failed to open cache file"));
		return (NULL);
	}

	if (fstat64(fd, &statbuf) != 0) {
		zutil_error_aux(hdl, "%s", strerror(errno));
		(void) close(fd);
		(void) zutil_error(hdl, EZFS_BADCACHE,
		    dgettext(TEXT_DOMAIN, "failed to get size of cache file"));
		return (NULL);
	}

	if ((buf = zutil_alloc(hdl, statbuf.st_size)) == NULL) {
		(void) close(fd);
		return (NULL);
	}

	if (read(fd, buf, statbuf.st_size) != statbuf.st_size) {
		(void) close(fd);
		free(buf);
		(void) zutil_error(hdl, EZFS_BADCACHE,
		    dgettext(TEXT_DOMAIN,
		    "failed to read cache file contents"));
		return (NULL);
	}

	(void) close(fd);

	if (nvlist_unpack(buf, statbuf.st_size, &raw, 0) != 0) {
		free(buf);
		(void) zutil_error(hdl, EZFS_BADCACHE,
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
		(void) zutil_no_memory(hdl);
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

		if (zutil_pool_active(hdl, name, this_guid, &active) != 0) {
			nvlist_free(raw);
			nvlist_free(pools);
			return (NULL);
		}

		if (active)
			continue;

		if (nvlist_add_string(src, ZPOOL_CONFIG_CACHEFILE,
		    cachefile) != 0) {
			(void) zutil_no_memory(hdl);
			nvlist_free(raw);
			nvlist_free(pools);
			return (NULL);
		}

		if ((dst = zutil_refresh_config(hdl, src)) == NULL) {
			nvlist_free(raw);
			nvlist_free(pools);
			return (NULL);
		}

		if (nvlist_add_nvlist(pools, nvpair_name(elem), dst) != 0) {
			(void) zutil_no_memory(hdl);
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

nvlist_t *
zpool_search_import(void *hdl, importargs_t *import,
    const pool_config_ops_t *pco)
{
	libpc_handle_t handle = { 0 };
	nvlist_t *pools = NULL;

	handle.lpc_lib_handle = hdl;
	handle.lpc_ops = pco;
	handle.lpc_printerr = B_TRUE;

	verify(import->poolname == NULL || import->guid == 0);

	if (import->cachefile != NULL)
		pools = zpool_find_import_cached(&handle, import->cachefile,
		    import->poolname, import->guid);
	else
		pools = zpool_find_import_impl(&handle, import);

	if ((pools == NULL || nvlist_empty(pools)) &&
	    handle.lpc_open_access_error && geteuid() != 0) {
		(void) zutil_error(&handle, EZFS_EACESS, dgettext(TEXT_DOMAIN,
		    "no pools found"));
	}

	return (pools);
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
zpool_find_config(void *hdl, const char *target, nvlist_t **configp,
    importargs_t *args, const pool_config_ops_t *pco)
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

	pools = zpool_search_import(hdl, args, pco);

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
		free(targetdup);
		return (ENOENT);
	}

	if (count > 1) {
		free(targetdup);
		return (EINVAL);
	}

	*configp = match;
	free(targetdup);

	return (0);
}
