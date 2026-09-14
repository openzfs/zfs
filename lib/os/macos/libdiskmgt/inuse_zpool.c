// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */
/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Attempt to dynamically link in the ZFS libzfs.so.1 so that we can
 * see if there are any ZFS zpools on any of the slices.
 */

#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <synch.h>
#include <dlfcn.h>
#include <ctype.h>
#include <sys/fs/zfs.h>

#include <libzfs.h>
#include "libdiskmgt.h"
#include "disks_private.h"

/*
 * Pointers to libzfs.so functions that we dynamically resolve.
 */
static int (*zfsdl_zpool_in_use)(libzfs_handle_t *hdl, int fd,
pool_state_t *state, char **name, boolean_t *);
static libzfs_handle_t *(*zfsdl_libzfs_init)(boolean_t);

static boolean_t		initialized = false;
static libzfs_handle_t		*zfs_hdl;

static void	*init_zpool(void);

static int
inuse_zpool_common(char *slice, nvlist_t *attrs, int *errp, const char *type)
{
	int		found = 0;
	char		*name;
	int		fd;
	pool_state_t	state;
	boolean_t	used;

	*errp = 0;
	if (slice == NULL) {
		return (found);
	}

	/*
	 * Dynamically load libzfs
	 */
	if (!initialized) {
		if (!init_zpool()) {
			return (found);
		}
		initialized = B_TRUE;
	}

	if ((fd = open(slice, O_RDONLY)) > 0) {
		name = NULL;
		if (zfsdl_zpool_in_use(zfs_hdl, fd, &state,
		    &name, &used) == 0 && used) {
			if (strcmp(type, DM_USE_ACTIVE_ZPOOL) == 0) {
				if (state == POOL_STATE_ACTIVE) {
					found = 1;
				} else if (state == POOL_STATE_SPARE) {
					found = 1;
					type = DM_USE_SPARE_ZPOOL;
				} else if (state == POOL_STATE_L2CACHE) {
					found = 1;
					type = DM_USE_L2CACHE_ZPOOL;
				}
			} else {
				found = 1;
			}

			if (found) {
				libdiskmgt_add_str(attrs, DM_USED_BY,
				    type, errp);
				libdiskmgt_add_str(attrs, DM_USED_NAME,
				    name, errp);
			}
		}
		if (name)
			free(name);
		(void) close(fd);
	}

	return (found);
}

int
inuse_active_zpool(char *slice, nvlist_t *attrs, int *errp)
{
	return (inuse_zpool_common(slice, attrs, errp, DM_USE_ACTIVE_ZPOOL));
}

int
inuse_exported_zpool(char *slice, nvlist_t *attrs, int *errp)
{
	return (inuse_zpool_common(slice, attrs, errp, DM_USE_EXPORTED_ZPOOL));
}

/*
 * Try to dynamically link the zfs functions we need.
 */
static void*
init_zpool(void)
{
	void	*lh = NULL;

	if ((lh = dlopen("libzfs.dylib", RTLD_NOW)) == NULL) {
		return (lh);
	}

	/*
	 * Instantiate the functions needed to get zpool configuration
	 * data
	 */
	if ((zfsdl_libzfs_init = (libzfs_handle_t *(*)(boolean_t))
	    dlsym(lh, "libzfs_init")) == NULL ||
	    (zfsdl_zpool_in_use = (int (*)(libzfs_handle_t *, int,
	    pool_state_t *, char **, boolean_t *))
	    dlsym(lh, "zpool_in_use")) == NULL) {
		(void) dlclose(lh);
		return (NULL);
	}

	if ((zfs_hdl = (*zfsdl_libzfs_init)(B_FALSE)) == NULL) {
		(void) dlclose(lh);
		return (NULL);
	}

	return (lh);
}
