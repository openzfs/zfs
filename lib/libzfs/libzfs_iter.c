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
 * Copyright (c) 2013 by Delphix. All rights reserved.
 * Copyright 2014 Nexenta Systems, Inc.  All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <stddef.h>
#include <libintl.h>
#include <libzfs.h>
#include <zfs_type.h>

#include "libzfs_impl.h"

/*
 * XXX: Workaround for conflicting type declarations for sa_handle_t between
 * sys/sa.h and libshare.h
 */
extern int dmu_objset_stat_nvlts(nvlist_t *nvl, dmu_objset_stats_t *stat);

int
zfs_iter_clones(zfs_handle_t *zhp, zfs_iter_f func, void *data)
{
	nvlist_t *nvl = zfs_get_clones_nvl(zhp);
	nvpair_t *pair;

	if (nvl == NULL)
		return (0);

	for (pair = nvlist_next_nvpair(nvl, NULL); pair != NULL;
	    pair = nvlist_next_nvpair(nvl, pair)) {
		zfs_handle_t *clone = zfs_open(zhp->zfs_hdl, nvpair_name(pair),
		    ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME);
		if (clone != NULL) {
			int err = func(clone, data);
			if (err != 0)
				return (err);
		}
	}
	return (0);
}

typedef struct zfs_iter_cb_data {
	libzfs_handle_t		*zicb_hdl;
	zfs_handle_t		zicb_zhp;
	void 			*zicb_data;
	zfs_iter_f		zicb_func;
	zfs_cmd_t		zicb_zc;
} zfs_iter_cb_data_t;

int
zfs_iter_cb(nvlist_t *nvl, void *data)
{
	nvlist_t *nvl_prop, *nvl_dds;
	zfs_iter_cb_data_t *cb = data;
	zfs_handle_t *nzhp;
	size_t nvsz;
	char *name;
	int ret;

	if ((ret = nvlist_lookup_nvlist(nvl, "properties", &nvl_prop))
	    != 0 ||
	    (ret = nvlist_lookup_string(nvl, "name", &name)) != 0 ||
	    (ret = nvlist_lookup_nvlist(nvl, "dmu_objset_stats",
	    &nvl_dds)) != 0 ||
	    (ret = dmu_objset_stat_nvlts(nvl_dds, &cb->zicb_zc.zc_objset_stats))
	    != 0) {
		return (EINVAL);
	}

	strlcpy(cb->zicb_zc.zc_name, name, sizeof (cb->zicb_zc.zc_name));

	cb->zicb_zc.zc_nvlist_dst_size = nvsz = fnvlist_size(nvl_prop);
	if ((ret = zcmd_expand_dst_nvlist(cb->zicb_hdl, &cb->zicb_zc))) {
		if (ret == -1)
			ret = ENOMEM;
		return (ret);
	}

	ret = nvlist_pack(nvl_prop, (char **) &cb->zicb_zc.zc_nvlist_dst,
	    &nvsz, NV_ENCODE_NATIVE, 0);

	cb->zicb_zc.zc_nvlist_dst_filled = B_TRUE;

	/*
	 * Errors here do not make sense, so we bail.
	 */
	if (strchr(name, '#') != NULL) {
		zfs_handle_t *zhp = &cb->zicb_zhp;
		bzero(zhp, sizeof (zfs_handle_t));
		zhp->zfs_hdl = cb->zicb_hdl;
		switch (cb->zicb_zc.zc_objset_stats.dds_type) {
		case DMU_OST_ZFS:
			zhp->zfs_head_type = ZFS_TYPE_FILESYSTEM;
			break;
		case DMU_OST_ZVOL:
			zhp->zfs_head_type = ZFS_TYPE_VOLUME;
			break;
		default:
			return (EINVAL);
		}
		nzhp = make_bookmark_handle(zhp, name, nvl_prop);
	} else if ((ret != 0) ||
	    (nzhp = make_dataset_handle_zc(cb->zicb_hdl, &cb->zicb_zc)) == NULL)
		return (EINVAL);

	ret = (*cb->zicb_func)(nzhp, cb->zicb_data);

	return (ret);
}

/*
 * Iterate over all children filesystems
 */
int
zfs_iter_generic(libzfs_handle_t *hdl, const char *name, zfs_type_t type,
    int64_t mindepth, int64_t maxdepth, boolean_t depth_specified,
    zfs_iter_f func, void *data)
{
	zfs_iter_cb_data_t cb_data;
	nvlist_t *tnvl, *opts;
	int ret;

	bzero(&cb_data.zicb_zc, sizeof (cb_data.zicb_zc));
	if (zcmd_alloc_dst_nvlist(hdl, &cb_data.zicb_zc, 0) != 0)
		return (-1);

	opts = fnvlist_alloc();
	if (depth_specified) {
		switch (maxdepth) {
		case -1:
			fnvlist_add_boolean(opts, "recurse");
		default:
			if (maxdepth < 0) {
				fnvlist_free(opts);
				ret = -1;
				goto out;
			}
			fnvlist_add_uint64(opts, "maxrecurse", maxdepth);
		}
		fnvlist_add_uint64(opts, "minrecurse", mindepth);
	} else
		fnvlist_add_boolean(opts, "recurse");

	tnvl = zfs_type_to_nvl(type);
	fnvlist_add_nvlist(opts, "type", tnvl);

	cb_data.zicb_hdl = hdl;
	cb_data.zicb_func = func;
	cb_data.zicb_data = data;

	ret = lzc_list_iter(name, opts, &zfs_iter_cb, &cb_data);

	fnvlist_free(tnvl);
	fnvlist_free(opts);
out:
	zcmd_free_nvlists(&cb_data.zicb_zc);

	return (ret);
}

static int
zfs_do_list_call(zfs_handle_t *zhp, zfs_type_t type, zfs_iter_f func,
    void *data)
{
	int rc;

	rc = zfs_iter_generic(zhp->zfs_hdl, zhp->zfs_name,
	    type, 1, 1, B_TRUE, func, data);

	switch (rc) {
	/*
	 * An rc value of 0 indicates normal completion.
	 * Treat a missing dataset as a dataset with no relevant children.
	 */
	case 0:
	case ENOENT:
		rc = 1;
		break;
	default:
		rc = zfs_standard_error(zhp->zfs_hdl, rc,
		    dgettext(TEXT_DOMAIN,
		    "cannot iterate filesystems"));
		break;
	}
	return (rc);
}

/*
 * Iterate over all child filesystems
 */
int
zfs_iter_filesystems(zfs_handle_t *zhp, zfs_iter_f func, void *data)
{
	int ret;

	if (zhp->zfs_type != ZFS_TYPE_FILESYSTEM)
		return (0);

	ret = zfs_do_list_call(zhp, ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME,
	    func, data);

	return ((ret < 0) ? ret : 0);
}

/*
 * Iterate over all snapshots
 */
int
zfs_iter_snapshots(zfs_handle_t *zhp, boolean_t simple, zfs_iter_f func,
    void *data)
{
	int ret;

	if (zhp->zfs_type == ZFS_TYPE_SNAPSHOT ||
	    zhp->zfs_type == ZFS_TYPE_BOOKMARK)
		return (0);

	ret = zfs_do_list_call(zhp, ZFS_TYPE_SNAPSHOT, func, data);

	return ((ret < 0) ? ret : 0);
}

/*
 * Iterate over all bookmarks
 */
int
zfs_iter_bookmarks(zfs_handle_t *zhp, zfs_iter_f func, void *data)
{
	zfs_handle_t *nzhp;
	nvlist_t *props = NULL;
	nvlist_t *bmarks = NULL;
	int err;
	nvpair_t *pair;

	if ((zfs_get_type(zhp) & (ZFS_TYPE_SNAPSHOT | ZFS_TYPE_BOOKMARK)) != 0)
		return (0);

	/* Setup the requested properties nvlist. */
	props = fnvlist_alloc();
	fnvlist_add_boolean(props, zfs_prop_to_name(ZFS_PROP_GUID));
	fnvlist_add_boolean(props, zfs_prop_to_name(ZFS_PROP_CREATETXG));
	fnvlist_add_boolean(props, zfs_prop_to_name(ZFS_PROP_CREATION));

	if ((err = lzc_get_bookmarks(zhp->zfs_name, props, &bmarks)) != 0)
		goto out;

	for (pair = nvlist_next_nvpair(bmarks, NULL);
	    pair != NULL; pair = nvlist_next_nvpair(bmarks, pair)) {
		char name[ZFS_MAX_DATASET_NAME_LEN];
		char *bmark_name;
		nvlist_t *bmark_props;

		bmark_name = nvpair_name(pair);
		bmark_props = fnvpair_value_nvlist(pair);

		(void) snprintf(name, sizeof (name), "%s#%s", zhp->zfs_name,
		    bmark_name);

		nzhp = make_bookmark_handle(zhp, name, bmark_props);
		if (nzhp == NULL)
			continue;

		if ((err = func(nzhp, data)) != 0)
			goto out;
	}

out:
	fnvlist_free(props);
	fnvlist_free(bmarks);

	return (err);
}

/*
 * Routines for dealing with the sorted snapshot functionality
 */
typedef struct zfs_node {
	zfs_handle_t	*zn_handle;
	avl_node_t	zn_avlnode;
} zfs_node_t;

static int
zfs_sort_snaps(zfs_handle_t *zhp, void *data)
{
	avl_tree_t *avl = data;
	zfs_node_t *node;
	zfs_node_t search;

	search.zn_handle = zhp;
	node = avl_find(avl, &search, NULL);
	if (node) {
		/*
		 * If this snapshot was renamed while we were creating the
		 * AVL tree, it's possible that we already inserted it under
		 * its old name. Remove the old handle before adding the new
		 * one.
		 */
		zfs_close(node->zn_handle);
		avl_remove(avl, node);
		free(node);
	}

	node = zfs_alloc(zhp->zfs_hdl, sizeof (zfs_node_t));
	node->zn_handle = zhp;
	avl_add(avl, node);

	return (0);
}

static int
zfs_snapshot_compare(const void *larg, const void *rarg)
{
	zfs_handle_t *l = ((zfs_node_t *)larg)->zn_handle;
	zfs_handle_t *r = ((zfs_node_t *)rarg)->zn_handle;
	uint64_t lcreate, rcreate;

	/*
	 * Sort them according to creation time.  We use the hidden
	 * CREATETXG property to get an absolute ordering of snapshots.
	 */
	lcreate = zfs_prop_get_int(l, ZFS_PROP_CREATETXG);
	rcreate = zfs_prop_get_int(r, ZFS_PROP_CREATETXG);

	return (AVL_CMP(lcreate, rcreate));
}

int
zfs_iter_snapshots_sorted(zfs_handle_t *zhp, zfs_iter_f callback, void *data)
{
	int ret = 0;
	zfs_node_t *node;
	avl_tree_t avl;
	void *cookie = NULL;

	avl_create(&avl, zfs_snapshot_compare,
	    sizeof (zfs_node_t), offsetof(zfs_node_t, zn_avlnode));

	ret = zfs_iter_snapshots(zhp, B_FALSE, zfs_sort_snaps, &avl);

	for (node = avl_first(&avl); node != NULL; node = AVL_NEXT(&avl, node))
		ret |= callback(node->zn_handle, data);

	while ((node = avl_destroy_nodes(&avl, &cookie)) != NULL)
		free(node);

	avl_destroy(&avl);

	return (ret);
}

typedef struct {
	char *ssa_first;
	char *ssa_last;
	boolean_t ssa_seenfirst;
	boolean_t ssa_seenlast;
	zfs_iter_f ssa_func;
	void *ssa_arg;
} snapspec_arg_t;

static int
snapspec_cb(zfs_handle_t *zhp, void *arg) {
	snapspec_arg_t *ssa = arg;
	char *shortsnapname;
	int err = 0;

	if (ssa->ssa_seenlast)
		return (0);
	shortsnapname = zfs_strdup(zhp->zfs_hdl,
	    strchr(zfs_get_name(zhp), '@') + 1);

	if (!ssa->ssa_seenfirst && strcmp(shortsnapname, ssa->ssa_first) == 0)
		ssa->ssa_seenfirst = B_TRUE;

	if (ssa->ssa_seenfirst) {
		err = ssa->ssa_func(zhp, ssa->ssa_arg);
	} else {
		zfs_close(zhp);
	}

	if (strcmp(shortsnapname, ssa->ssa_last) == 0)
		ssa->ssa_seenlast = B_TRUE;
	free(shortsnapname);

	return (err);
}

/*
 * spec is a string like "A,B%C,D"
 *
 * <snaps>, where <snaps> can be:
 *      <snap>          (single snapshot)
 *      <snap>%<snap>   (range of snapshots, inclusive)
 *      %<snap>         (range of snapshots, starting with earliest)
 *      <snap>%         (range of snapshots, ending with last)
 *      %               (all snapshots)
 *      <snaps>[,...]   (comma separated list of the above)
 *
 * If a snapshot can not be opened, continue trying to open the others, but
 * return ENOENT at the end.
 */
int
zfs_iter_snapspec(zfs_handle_t *fs_zhp, const char *spec_orig,
    zfs_iter_f func, void *arg)
{
	char *buf, *comma_separated, *cp;
	int err = 0;
	int ret = 0;

	buf = zfs_strdup(fs_zhp->zfs_hdl, spec_orig);
	cp = buf;

	while ((comma_separated = strsep(&cp, ",")) != NULL) {
		char *pct = strchr(comma_separated, '%');
		if (pct != NULL) {
			snapspec_arg_t ssa = { 0 };
			ssa.ssa_func = func;
			ssa.ssa_arg = arg;

			if (pct == comma_separated)
				ssa.ssa_seenfirst = B_TRUE;
			else
				ssa.ssa_first = comma_separated;
			*pct = '\0';
			ssa.ssa_last = pct + 1;

			/*
			 * If there is a lastname specified, make sure it
			 * exists.
			 */
			if (ssa.ssa_last[0] != '\0') {
				char snapname[ZFS_MAX_DATASET_NAME_LEN];
				(void) snprintf(snapname, sizeof (snapname),
				    "%s@%s", zfs_get_name(fs_zhp),
				    ssa.ssa_last);
				if (!zfs_dataset_exists(fs_zhp->zfs_hdl,
				    snapname, ZFS_TYPE_SNAPSHOT)) {
					ret = ENOENT;
					continue;
				}
			}

			err = zfs_iter_snapshots_sorted(fs_zhp,
			    snapspec_cb, &ssa);
			if (ret == 0)
				ret = err;
			if (ret == 0 && (!ssa.ssa_seenfirst ||
			    (ssa.ssa_last[0] != '\0' && !ssa.ssa_seenlast))) {
				ret = ENOENT;
			}
		} else {
			char snapname[ZFS_MAX_DATASET_NAME_LEN];
			zfs_handle_t *snap_zhp;
			(void) snprintf(snapname, sizeof (snapname), "%s@%s",
			    zfs_get_name(fs_zhp), comma_separated);
			snap_zhp = make_dataset_handle(fs_zhp->zfs_hdl,
			    snapname);
			if (snap_zhp == NULL) {
				ret = ENOENT;
				continue;
			}
			err = func(snap_zhp, arg);
			if (ret == 0)
				ret = err;
		}
	}

	free(buf);
	return (ret);
}

/*
 * Iterate over all children, snapshots and filesystems
 */
int
zfs_iter_children(zfs_handle_t *zhp, zfs_iter_f func, void *data)
{
	int ret;

	if ((ret = zfs_iter_filesystems(zhp, func, data)) != 0)
		return (ret);

	return (zfs_iter_snapshots(zhp, B_FALSE, func, data));
}


typedef struct iter_stack_frame {
	struct iter_stack_frame *next;
	zfs_handle_t *zhp;
} iter_stack_frame_t;

typedef struct iter_dependents_arg {
	boolean_t first;
	boolean_t allowrecursion;
	iter_stack_frame_t *stack;
	zfs_iter_f func;
	void *data;
} iter_dependents_arg_t;

static int
iter_dependents_cb(zfs_handle_t *zhp, void *arg)
{
	iter_dependents_arg_t *ida = arg;
	int err = 0;
	boolean_t first = ida->first;
	ida->first = B_FALSE;

	if (zhp->zfs_type == ZFS_TYPE_SNAPSHOT) {
		err = zfs_iter_clones(zhp, iter_dependents_cb, ida);
	} else if (zhp->zfs_type != ZFS_TYPE_BOOKMARK) {
		iter_stack_frame_t isf;
		iter_stack_frame_t *f;

		/*
		 * check if there is a cycle by seeing if this fs is already
		 * on the stack.
		 */
		for (f = ida->stack; f != NULL; f = f->next) {
			if (f->zhp->zfs_dmustats.dds_guid ==
			    zhp->zfs_dmustats.dds_guid) {
				if (ida->allowrecursion) {
					zfs_close(zhp);
					return (0);
				} else {
					zfs_error_aux(zhp->zfs_hdl,
					    dgettext(TEXT_DOMAIN,
					    "recursive dependency at '%s'"),
					    zfs_get_name(zhp));
					err = zfs_error(zhp->zfs_hdl,
					    EZFS_RECURSIVE,
					    dgettext(TEXT_DOMAIN,
					    "cannot determine dependent "
					    "datasets"));
					zfs_close(zhp);
					return (err);
				}
			}
		}

		isf.zhp = zhp;
		isf.next = ida->stack;
		ida->stack = &isf;
		err = zfs_iter_filesystems(zhp, iter_dependents_cb, ida);
		if (err == 0)
			err = zfs_iter_snapshots(zhp, B_FALSE,
			    iter_dependents_cb, ida);
		ida->stack = isf.next;
	}

	if (!first && err == 0)
		err = ida->func(zhp, ida->data);
	else
		zfs_close(zhp);

	return (err);
}

int
zfs_iter_dependents(zfs_handle_t *zhp, boolean_t allowrecursion,
    zfs_iter_f func, void *data)
{
	iter_dependents_arg_t ida;
	ida.allowrecursion = allowrecursion;
	ida.stack = NULL;
	ida.func = func;
	ida.data = data;
	ida.first = B_TRUE;
	return (iter_dependents_cb(zfs_handle_dup(zhp), &ida));
}
