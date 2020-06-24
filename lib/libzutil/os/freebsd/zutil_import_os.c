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
 * Copyright (c) 2012, 2017 by Delphix. All rights reserved.
 * Copyright 2015 RackTop Systems.
 * Copyright 2016 Nexenta Systems, Inc.
 */

/*
 * Pool import support functions.
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

#include <aio.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <libintl.h>
#include <libgen.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/disk.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/efi_partition.h>
#include <thread_pool.h>
#include <libgeom.h>

#include <sys/vdev_impl.h>

#include <libzutil.h>

#include "zutil_import.h"

/*
 * Update a leaf vdev's persistent device strings
 *
 * - only applies for a dedicated leaf vdev (aka whole disk)
 * - updated during pool create|add|attach|import
 * - used for matching device matching during auto-{online,expand,replace}
 * - stored in a leaf disk config label (i.e. alongside 'path' NVP)
 * - these strings are currently not used in kernel (i.e. for vdev_disk_open)
 *
 * On FreeBSD we currently just strip devid and phys_path to avoid confusion.
 */
void
update_vdev_config_dev_strs(nvlist_t *nv)
{
	(void) nvlist_remove_all(nv, ZPOOL_CONFIG_DEVID);
	(void) nvlist_remove_all(nv, ZPOOL_CONFIG_PHYS_PATH);
}

/*
 * Do not even look at these devices.
 */
static const char * const excluded_devs[] = {
	"nfslock",
	"sequencer",
	"zfs",
};
#define	EXCLUDED_DIR		"/dev/"
#define	EXCLUDED_DIR_LEN	5

void
zpool_open_func(void *arg)
{
	rdsk_node_t *rn = arg;
	struct stat64 statbuf;
	nvlist_t *config;
	size_t i;
	int num_labels;
	int fd;
	off_t mediasize = 0;

	/*
	 * Do not even look at excluded devices.
	 */
	if (strncmp(rn->rn_name, EXCLUDED_DIR, EXCLUDED_DIR_LEN) == 0) {
		char *name = rn->rn_name + EXCLUDED_DIR_LEN;
		for (i = 0; i < nitems(excluded_devs); ++i) {
			const char *excluded_name = excluded_devs[i];
			size_t len = strlen(excluded_name);
			if (strncmp(name, excluded_name, len) == 0) {
				return;
			}
		}
	}

	/*
	 * O_NONBLOCK so we don't hang trying to open things like serial ports.
	 */
	if ((fd = open(rn->rn_name, O_RDONLY|O_NONBLOCK)) < 0)
		return;

	/*
	 * Ignore failed stats.
	 */
	if (fstat64(fd, &statbuf) != 0)
		goto out;
	/*
	 * We only want regular files, character devs and block devs.
	 */
	if (S_ISREG(statbuf.st_mode)) {
		/* Check if this file is too small to hold a zpool. */
		if (statbuf.st_size < SPA_MINDEVSIZE) {
			goto out;
		}
	} else if (S_ISCHR(statbuf.st_mode) || S_ISBLK(statbuf.st_mode)) {
		/* Check if this device is too small to hold a zpool. */
		if (ioctl(fd, DIOCGMEDIASIZE, &mediasize) != 0 ||
		    mediasize < SPA_MINDEVSIZE) {
			goto out;
		}
	} else {
		goto out;
	}

	if (zpool_read_label(fd, &config, &num_labels) != 0)
		goto out;
	if (num_labels == 0) {
		nvlist_free(config);
		goto out;
	}

	rn->rn_config = config;
	rn->rn_num_labels = num_labels;

	/* TODO: Reuse labelpaths logic from Linux? */
out:
	(void) close(fd);
}

static const char *
zpool_default_import_path[] = {
	"/dev"
};

const char * const *
zpool_default_search_paths(size_t *count)
{
	*count = nitems(zpool_default_import_path);
	return (zpool_default_import_path);
}

int
zpool_find_import_blkid(libpc_handle_t *hdl, pthread_mutex_t *lock,
    avl_tree_t **slice_cache)
{
	char *end, path[MAXPATHLEN];
	rdsk_node_t *slice;
	struct gmesh mesh;
	struct gclass *mp;
	struct ggeom *gp;
	struct gprovider *pp;
	avl_index_t where;
	size_t pathleft;
	int error;

	end = stpcpy(path, "/dev/");
	pathleft = &path[sizeof (path)] - end;

	error = geom_gettree(&mesh);
	if (error != 0)
		return (error);

	*slice_cache = zutil_alloc(hdl, sizeof (avl_tree_t));
	avl_create(*slice_cache, slice_cache_compare, sizeof (rdsk_node_t),
	    offsetof(rdsk_node_t, rn_node));

	LIST_FOREACH(mp, &mesh.lg_class, lg_class) {
		LIST_FOREACH(gp, &mp->lg_geom, lg_geom) {
			LIST_FOREACH(pp, &gp->lg_provider, lg_provider) {
				strlcpy(end, pp->lg_name, pathleft);
				slice = zutil_alloc(hdl, sizeof (rdsk_node_t));
				slice->rn_name = zutil_strdup(hdl, path);
				slice->rn_vdev_guid = 0;
				slice->rn_lock = lock;
				slice->rn_avl = *slice_cache;
				slice->rn_hdl = hdl;
				slice->rn_labelpaths = B_FALSE;
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
		}
	}

	geom_deletetree(&mesh);

	return (0);
}

int
zfs_dev_flush(int fd __unused)
{
	return (0);
}
