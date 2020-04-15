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
#include <sys/vdev_impl.h>

#include <thread_pool.h>
#include <libzutil.h>
#include <libnvpair.h>

#include "zutil_import.h"

#ifdef HAVE_LIBUDEV
#include <libudev.h>
#include <sched.h>
#endif

/*
 * We allow /dev/ to be search in DEBUG build
 * DEFAULT_IMPORT_PATH_SIZE is decremented by one to remove /dev!
 * See below in zpool_find_import_blkid() to skip.
 */
#define	DEFAULT_IMPORT_PATH_SIZE	4

#define	DEV_BYID_PATH "/private/var/run/disk/by-id/"

static const char *
zpool_default_import_path[DEFAULT_IMPORT_PATH_SIZE] = {
	"/private/var/run/disk/by-id",
	"/private/var/run/disk/by-path",
	"/private/var/run/disk/by-serial",
	"/dev"	/* Only with DEBUG build */
};

static boolean_t
is_watchdog_dev(const char *dev)
{
	/* For 'watchdog' dev */
	if (strcmp(dev, "watchdog") == 0)
		return (B_TRUE);

	/* For 'watchdog<digit><whatever> */
	if (strstr(dev, "watchdog") == dev && isdigit(dev[8]))
		return (B_TRUE);

	return (B_FALSE);
}

int
zfs_dev_flush(int fd)
{
	(void) fd;
//	return (ioctl(fd, BLKFLSBUF));
	return (0);
}

static uint64_t
label_offset(uint64_t size, int l)
{
	ASSERT(P2PHASE_TYPED(size, sizeof (vdev_label_t), uint64_t) == 0);
	return (l * sizeof (vdev_label_t) + (l < VDEV_LABELS / 2 ?
	    0 : size - VDEV_LABELS * sizeof (vdev_label_t)));
}

/*
 * We have had issues with lio_listio() and AIO on BigSur, where
 * we receive waves of EAGAIN, and have to loop, often up to
 * 100 times before labels are read. Until this problem can be
 * understood better, we use the old serial style here.
 */
int
zpool_read_label(int fd, nvlist_t **config, int *num_labels)
{
	struct stat64 statbuf;
	int l, count = 0;
	vdev_phys_t *label;
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
		off_t offset = label_offset(size, l) + VDEV_SKIP_SIZE;

		if (pread64(fd, label, sizeof (vdev_phys_t),
		    offset) != sizeof (vdev_phys_t))
			continue;

		if (nvlist_unpack(label->vp_nvlist,
		    sizeof (label->vp_nvlist), config, 0) != 0)
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

void
zpool_open_func(void *arg)
{
	rdsk_node_t *rn = arg;
	libpc_handle_t *hdl = rn->rn_hdl;
	struct stat64 statbuf;
	nvlist_t *config;
	const char *bname;
	char *dupname;
	uint64_t vdev_guid = 0;
	int error;
	int num_labels = 0;
	int fd;

	/*
	 * Skip devices with well known prefixes there can be side effects
	 * when opening devices which need to be avoided.
	 *
	 * hpet     - High Precision Event Timer
	 * watchdog - Watchdog must be closed in a special way.
	 */
	dupname = zutil_strdup(hdl, rn->rn_name);
	bname = zfs_basename(dupname);
	error = ((strcmp(bname, "hpet") == 0) || is_watchdog_dev(bname));
	if ((strncmp(bname, "core", 4) == 0) ||
	    (strncmp(bname, "fd", 2) == 0) ||
	    (strncmp(bname, "fuse", 4) == 0) ||
	    (strncmp(bname, "hpet", 4) == 0) ||
	    (strncmp(bname, "lp", 2) == 0) ||
	    (strncmp(bname, "parport", 7) == 0) ||
	    (strncmp(bname, "ppp", 3) == 0) ||
	    (strncmp(bname, "random", 6) == 0) ||
	    (strncmp(bname, "rtc", 3) == 0) ||
	    (strncmp(bname, "tty", 3) == 0) ||
	    (strncmp(bname, "urandom", 7) == 0) ||
	    (strncmp(bname, "usbmon", 6) == 0) ||
	    (strncmp(bname, "vcs", 3) == 0) ||
	    (strncmp(bname, "pty", 3) == 0) || // lots, skip for speed
	    (strncmp(bname, "bpf", 3) == 0) ||
	    (strncmp(bname, "audit", 5) == 0) ||
	    (strncmp(bname, "autofs", 6) == 0) ||
	    (strncmp(bname, "console", 7) == 0) ||
	    (strncmp(bname, "zfs", 3) == 0) ||
	    (strncmp(bname, "oslog_stream", 12) == 0) ||
	    (strncmp(bname, "com", 3) == 0)) // /dev/com_digidesign_semiface
		error = 1;

	free(dupname);
	if (error)
		return;

	/*
	 * Ignore failed stats.  We only want regular files and block devices.
	 */
	if (stat(rn->rn_name, &statbuf) != 0 ||
	    (!S_ISREG(statbuf.st_mode) &&
	    !S_ISBLK(statbuf.st_mode) &&
	    !S_ISCHR(statbuf.st_mode)))
		return;

	fd = open(rn->rn_name, O_RDONLY);
	if ((fd < 0) && (errno == EINVAL))
		fd = open(rn->rn_name, O_RDONLY);
	if ((fd < 0) && (errno == EACCES))
		hdl->lpc_open_access_error = B_TRUE;
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
#ifdef DEBUG
		printf("%s: zpool_read_label returned error %d "
		    "(errno: %d name: %s)\n",
		    __func__, error, errno, rn->rn_name);
#endif
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

const char * const *
zpool_default_search_paths(size_t *count)
{
	*count = DEFAULT_IMPORT_PATH_SIZE;
	return ((const char * const *)zpool_default_import_path);
}

int
zpool_find_import_blkid(libpc_handle_t *hdl, pthread_mutex_t *lock,
    avl_tree_t **slice_cache)
{
	int i, dirs;
	struct dirent *dp;
	char path[MAXPATHLEN];
	char *end;
	const char **dir;
	size_t pathleft;
	avl_index_t where;
	rdsk_node_t *slice;
	int error = 0;

	dir = zpool_default_import_path;
	dirs = DEFAULT_IMPORT_PATH_SIZE;

	/*
	 * Go through and read the label configuration information from every
	 * possible device, organizing the information according to pool GUID
	 * and toplevel GUID.
	 */
	*slice_cache = zutil_alloc(hdl, sizeof (avl_tree_t));
	avl_create(*slice_cache, slice_cache_compare,
	    sizeof (rdsk_node_t), offsetof(rdsk_node_t, rn_node));

	for (i = 0; i < dirs; i++) {
		char rdsk[MAXPATHLEN];
		int dfd;
		DIR *dirp;

#ifndef DEBUG
		/*
		 * We skip imports in /dev/ in release builds, due to the
		 * danger of cache/log devices and drive renumbering.
		 * We have it in zpool_default_import_path to allow
		 * zfs_resolve_shortname() to still work, ie
		 * "zpool create disk3" to resolve to /dev/disk3.
		 */
		if (strncmp("/dev", dir[i], 4) == 0)
			continue;
#endif

		/* use realpath to normalize the path */
		if (realpath(dir[i], path) == 0) {

			/* it is safe to skip missing search paths */
			if (errno == ENOENT)
				continue;

			return (EPERM);
		}
		end = &path[strlen(path)];
		*end++ = '/';
		*end = 0;
		pathleft = &path[sizeof (path)] - end;

		(void) strlcpy(rdsk, path, sizeof (rdsk));

		if ((dfd = open(rdsk, O_RDONLY)) < 0 ||
		    (dirp = fdopendir(dfd)) == NULL) {
			if (dfd >= 0)
				(void) close(dfd);
			return (ENOENT);
		}

		while ((dp = readdir(dirp)) != NULL) {
			const char *name = dp->d_name;
			if (name[0] == '.' &&
			    (name[1] == 0 || (name[1] == '.' && name[2] == 0)))
				continue;

			slice = zutil_alloc(hdl, sizeof (rdsk_node_t));

			error = asprintf(&slice->rn_name, "%s%s",
			    path, name);
			if (error == -1) {
				free(slice);
				return (ENOMEM);
			}

			slice->rn_vdev_guid = 0;
			slice->rn_lock = lock;
			slice->rn_avl = *slice_cache;
			slice->rn_hdl = hdl;
			slice->rn_labelpaths = B_FALSE;

			// Make rdisk have a lower priority than disk
			if (name[0] == 'r')
				slice->rn_order = IMPORT_ORDER_DEFAULT + i;
			else
				slice->rn_order = IMPORT_ORDER_SCAN_OFFSET + i;

			pthread_mutex_lock(lock);
			if (avl_find(*slice_cache, slice, &where)) {
				free(slice->rn_name);
				free(slice);
			} else {
				avl_insert(*slice_cache, slice, where);
			}
			pthread_mutex_unlock(lock);
		}

		(void) closedir(dirp);
	}

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

int
zfs_device_get_devid(struct udev_device *dev, char *bufptr, size_t buflen)
{
	(void) dev;
	(void) bufptr;
	(void) buflen;
	return (ENODATA);
}

int
zfs_device_get_physical(struct udev_device *dev, char *bufptr, size_t buflen)
{
	(void) dev;
	(void) bufptr;
	(void) buflen;
	return (ENODATA);
}

/*
 * Encode the persistent devices strings
 * used for the vdev disk label
 */
static int
encode_device_strings(const char *path, vdev_dev_strs_t *ds,
    boolean_t wholedisk)
{
	(void) path;
	(void) ds;
	(void) wholedisk;
	return (ENOENT);
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
	 * Update device string values in the config nvlist.
	 */
	if (encode_device_strings(path, &vds, (boolean_t)wholedisk) == 0) {
		(void) nvlist_add_string(nv, ZPOOL_CONFIG_DEVID, vds.vds_devid);
		if (vds.vds_devphys[0] != '\0') {
			(void) nvlist_add_string(nv, ZPOOL_CONFIG_PHYS_PATH,
			    vds.vds_devphys);
		}

	} else {
		/* Clear out any stale entries. */
		(void) nvlist_remove_all(nv, ZPOOL_CONFIG_DEVID);
		(void) nvlist_remove_all(nv, ZPOOL_CONFIG_PHYS_PATH);
		(void) nvlist_remove_all(nv, ZPOOL_CONFIG_VDEV_ENC_SYSFS_PATH);
	}
}

void
update_vdevs_config_dev_sysfs_path(nvlist_t *config)
{
	(void) config;
}
