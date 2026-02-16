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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2019 Joyent, Inc.
 * Copyright (c) 2011, 2020 by Delphix. All rights reserved.
 * Copyright (c) 2012 DEY Storage Systems, Inc.  All rights reserved.
 * Copyright (c) 2012 Pawel Jakub Dawidek <pawel@dawidek.net>.
 * Copyright (c) 2013 Martin Matuska. All rights reserved.
 * Copyright (c) 2013 Steven Hartland. All rights reserved.
 * Copyright 2017 Nexenta Systems, Inc.
 * Copyright 2016 Igor Kozhukhov <ikozhukhov@gmail.com>
 * Copyright 2017-2018 RackTop Systems.
 * Copyright (c) 2019 Datto Inc.
 * Copyright (c) 2019, loli10K <ezomori.nozomu@gmail.com>
 * Copyright (c) 2021 Matt Fiddaman
 * Copyright (c) 2026, TrueNAS.
 */

#include <sys/mntent.h>
#include <libzfs.h>
#include "libzfs_impl.h"

typedef struct mnttab_node {
	struct mnttab mtn_mt;
	avl_node_t mtn_node;
} mnttab_node_t;

static int
libzfs_mnttab_cache_compare(const void *arg1, const void *arg2)
{
	const mnttab_node_t *mtn1 = (const mnttab_node_t *)arg1;
	const mnttab_node_t *mtn2 = (const mnttab_node_t *)arg2;
	int rv;

	rv = strcmp(mtn1->mtn_mt.mnt_special, mtn2->mtn_mt.mnt_special);

	return (TREE_ISIGN(rv));
}

void
libzfs_mnttab_init(libzfs_handle_t *hdl)
{
	pthread_mutex_init(&hdl->libzfs_mnttab_cache_lock, NULL);
	assert(avl_numnodes(&hdl->libzfs_mnttab_cache) == 0);
	avl_create(&hdl->libzfs_mnttab_cache, libzfs_mnttab_cache_compare,
	    sizeof (mnttab_node_t), offsetof(mnttab_node_t, mtn_node));
}

static int
libzfs_mnttab_update(libzfs_handle_t *hdl)
{
	FILE *mnttab;
	struct mnttab entry;

	if ((mnttab = fopen(MNTTAB, "re")) == NULL)
		return (ENOENT);

	while (getmntent(mnttab, &entry) == 0) {
		mnttab_node_t *mtn;
		avl_index_t where;

		if (strcmp(entry.mnt_fstype, MNTTYPE_ZFS) != 0)
			continue;

		mtn = zfs_alloc(hdl, sizeof (mnttab_node_t));
		mtn->mtn_mt.mnt_special = zfs_strdup(hdl, entry.mnt_special);
		mtn->mtn_mt.mnt_mountp = zfs_strdup(hdl, entry.mnt_mountp);
		mtn->mtn_mt.mnt_fstype = zfs_strdup(hdl, entry.mnt_fstype);
		mtn->mtn_mt.mnt_mntopts = zfs_strdup(hdl, entry.mnt_mntopts);

		/* Exclude duplicate mounts */
		if (avl_find(&hdl->libzfs_mnttab_cache, mtn, &where) != NULL) {
			free(mtn->mtn_mt.mnt_special);
			free(mtn->mtn_mt.mnt_mountp);
			free(mtn->mtn_mt.mnt_fstype);
			free(mtn->mtn_mt.mnt_mntopts);
			free(mtn);
			continue;
		}

		avl_add(&hdl->libzfs_mnttab_cache, mtn);
	}

	(void) fclose(mnttab);
	return (0);
}

void
libzfs_mnttab_fini(libzfs_handle_t *hdl)
{
	void *cookie = NULL;
	mnttab_node_t *mtn;

	while ((mtn = avl_destroy_nodes(&hdl->libzfs_mnttab_cache, &cookie))
	    != NULL) {
		free(mtn->mtn_mt.mnt_special);
		free(mtn->mtn_mt.mnt_mountp);
		free(mtn->mtn_mt.mnt_fstype);
		free(mtn->mtn_mt.mnt_mntopts);
		free(mtn);
	}
	avl_destroy(&hdl->libzfs_mnttab_cache);
	(void) pthread_mutex_destroy(&hdl->libzfs_mnttab_cache_lock);
}

void
libzfs_mnttab_cache(libzfs_handle_t *hdl, boolean_t enable)
{
	hdl->libzfs_mnttab_enable = enable;
}

int
libzfs_mnttab_find(libzfs_handle_t *hdl, const char *fsname,
    struct mnttab *entry)
{
	FILE *mnttab;
	mnttab_node_t find;
	mnttab_node_t *mtn;
	int ret = ENOENT;

	if (!hdl->libzfs_mnttab_enable) {
		struct mnttab srch = { 0 };

		if (avl_numnodes(&hdl->libzfs_mnttab_cache))
			libzfs_mnttab_fini(hdl);

		if ((mnttab = fopen(MNTTAB, "re")) == NULL)
			return (ENOENT);

		srch.mnt_special = (char *)fsname;
		srch.mnt_fstype = (char *)MNTTYPE_ZFS;
		ret = getmntany(mnttab, entry, &srch) ? ENOENT : 0;
		(void) fclose(mnttab);
		return (ret);
	}

	pthread_mutex_lock(&hdl->libzfs_mnttab_cache_lock);
	if (avl_numnodes(&hdl->libzfs_mnttab_cache) == 0) {
		int error;

		if ((error = libzfs_mnttab_update(hdl)) != 0) {
			pthread_mutex_unlock(&hdl->libzfs_mnttab_cache_lock);
			return (error);
		}
	}

	find.mtn_mt.mnt_special = (char *)fsname;
	mtn = avl_find(&hdl->libzfs_mnttab_cache, &find, NULL);
	if (mtn) {
		*entry = mtn->mtn_mt;
		ret = 0;
	}
	pthread_mutex_unlock(&hdl->libzfs_mnttab_cache_lock);
	return (ret);
}

void
libzfs_mnttab_add(libzfs_handle_t *hdl, const char *special,
    const char *mountp, const char *mntopts)
{
	mnttab_node_t *mtn;

	pthread_mutex_lock(&hdl->libzfs_mnttab_cache_lock);
	if (avl_numnodes(&hdl->libzfs_mnttab_cache) != 0) {
		mtn = zfs_alloc(hdl, sizeof (mnttab_node_t));
		mtn->mtn_mt.mnt_special = zfs_strdup(hdl, special);
		mtn->mtn_mt.mnt_mountp = zfs_strdup(hdl, mountp);
		mtn->mtn_mt.mnt_fstype = zfs_strdup(hdl, MNTTYPE_ZFS);
		mtn->mtn_mt.mnt_mntopts = zfs_strdup(hdl, mntopts);
		/*
		 * Another thread may have already added this entry
		 * via libzfs_mnttab_update. If so we should skip it.
		 */
		if (avl_find(&hdl->libzfs_mnttab_cache, mtn, NULL) != NULL) {
			free(mtn->mtn_mt.mnt_special);
			free(mtn->mtn_mt.mnt_mountp);
			free(mtn->mtn_mt.mnt_fstype);
			free(mtn->mtn_mt.mnt_mntopts);
			free(mtn);
		} else {
			avl_add(&hdl->libzfs_mnttab_cache, mtn);
		}
	}
	pthread_mutex_unlock(&hdl->libzfs_mnttab_cache_lock);
}

void
libzfs_mnttab_remove(libzfs_handle_t *hdl, const char *fsname)
{
	mnttab_node_t find;
	mnttab_node_t *ret;

	pthread_mutex_lock(&hdl->libzfs_mnttab_cache_lock);
	find.mtn_mt.mnt_special = (char *)fsname;
	if ((ret = avl_find(&hdl->libzfs_mnttab_cache, (void *)&find, NULL))
	    != NULL) {
		avl_remove(&hdl->libzfs_mnttab_cache, ret);
		free(ret->mtn_mt.mnt_special);
		free(ret->mtn_mt.mnt_mountp);
		free(ret->mtn_mt.mnt_fstype);
		free(ret->mtn_mt.mnt_mntopts);
		free(ret);
	}
	pthread_mutex_unlock(&hdl->libzfs_mnttab_cache_lock);
}
