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
#include <sys/mutex.h>
#include <libzfs.h>
#include "libzfs_impl.h"

typedef struct mnttab_node {
	struct mnttab mtn_mt;
	avl_node_t mtn_node;
} mnttab_node_t;

static mnttab_node_t *
mnttab_node_alloc(libzfs_handle_t *hdl, const char *special,
    const char *mountp, const char *mntopts)
{
	mnttab_node_t *mtn = zfs_alloc(hdl, sizeof (mnttab_node_t));
	mtn->mtn_mt.mnt_special = zfs_strdup(hdl, special);
	mtn->mtn_mt.mnt_mountp = zfs_strdup(hdl, mountp);
	mtn->mtn_mt.mnt_fstype = (char *)MNTTYPE_ZFS;
	mtn->mtn_mt.mnt_mntopts = zfs_strdup(hdl, mntopts);
	return (mtn);
}

static void
mnttab_node_free(libzfs_handle_t *hdl, mnttab_node_t *mtn)
{
	(void) hdl;
	free(mtn->mtn_mt.mnt_special);
	free(mtn->mtn_mt.mnt_mountp);
	free(mtn->mtn_mt.mnt_mntopts);
	free(mtn);
}

static int
mnttab_compare(const void *arg1, const void *arg2)
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
	mutex_init(&hdl->zh_mnttab_lock, NULL, MUTEX_DEFAULT, NULL);
	assert(avl_numnodes(&hdl->zh_mnttab) == 0);
	avl_create(&hdl->zh_mnttab, mnttab_compare,
	    sizeof (mnttab_node_t), offsetof(mnttab_node_t, mtn_node));
}

void
libzfs_mnttab_fini(libzfs_handle_t *hdl)
{
	void *cookie = NULL;
	mnttab_node_t *mtn;

	while ((mtn = avl_destroy_nodes(&hdl->zh_mnttab, &cookie))
	    != NULL)
		mnttab_node_free(hdl, mtn);

	avl_destroy(&hdl->zh_mnttab);
	(void) mutex_destroy(&hdl->zh_mnttab_lock);
}

void
libzfs_mnttab_cache(libzfs_handle_t *hdl, boolean_t enable)
{
	/* This is a no-op to preserve ABI backward compatibility. */
	(void) hdl, (void) enable;
}

static int
mnttab_update(libzfs_handle_t *hdl)
{
	FILE *mnttab;
	struct mnttab entry;

	ASSERT(MUTEX_HELD(&hdl->zh_mnttab_lock));

	if ((mnttab = fopen(MNTTAB, "re")) == NULL)
		return (ENOENT);

	while (getmntent(mnttab, &entry) == 0) {
		mnttab_node_t *mtn;
		avl_index_t where;

		if (strcmp(entry.mnt_fstype, MNTTYPE_ZFS) != 0)
			continue;

		mtn = mnttab_node_alloc(hdl, entry.mnt_special,
		    entry.mnt_mountp, entry.mnt_mntopts);

		/* Exclude duplicate mounts */
		if (avl_find(&hdl->zh_mnttab, mtn, &where) != NULL) {
			mnttab_node_free(hdl, mtn);
			continue;
		}

		avl_add(&hdl->zh_mnttab, mtn);
	}

	(void) fclose(mnttab);
	return (0);
}

int
libzfs_mnttab_find(libzfs_handle_t *hdl, const char *fsname,
    struct mnttab *entry)
{
	mnttab_node_t find;
	mnttab_node_t *mtn;
	int ret = ENOENT;

	mutex_enter(&hdl->zh_mnttab_lock);
	if (avl_numnodes(&hdl->zh_mnttab) == 0) {
		int error;

		if ((error = mnttab_update(hdl)) != 0) {
			mutex_exit(&hdl->zh_mnttab_lock);
			return (error);
		}
	}

	find.mtn_mt.mnt_special = (char *)fsname;
	mtn = avl_find(&hdl->zh_mnttab, &find, NULL);
	if (mtn) {
		*entry = mtn->mtn_mt;
		ret = 0;
	}
	mutex_exit(&hdl->zh_mnttab_lock);
	return (ret);
}

void
libzfs_mnttab_add(libzfs_handle_t *hdl, const char *special,
    const char *mountp, const char *mntopts)
{
	mnttab_node_t *mtn;

	mutex_enter(&hdl->zh_mnttab_lock);

	mtn = mnttab_node_alloc(hdl, special, mountp, mntopts);

	/*
	 * Another thread may have already added this entry
	 * via mnttab_update. If so we should skip it.
	 */
	if (avl_find(&hdl->zh_mnttab, mtn, NULL) != NULL)
		mnttab_node_free(hdl, mtn);
	else
		avl_add(&hdl->zh_mnttab, mtn);

	mutex_exit(&hdl->zh_mnttab_lock);
}

void
libzfs_mnttab_remove(libzfs_handle_t *hdl, const char *fsname)
{
	mnttab_node_t find;
	mnttab_node_t *ret;

	mutex_enter(&hdl->zh_mnttab_lock);
	find.mtn_mt.mnt_special = (char *)fsname;
	if ((ret = avl_find(&hdl->zh_mnttab, (void *)&find, NULL)) != NULL) {
		avl_remove(&hdl->zh_mnttab, ret);
		mnttab_node_free(hdl, ret);
	}
	mutex_exit(&hdl->zh_mnttab_lock);
}
