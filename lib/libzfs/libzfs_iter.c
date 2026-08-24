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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2013, 2019 by Delphix. All rights reserved.
 * Copyright 2014 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2019 Datto Inc.
 * Copyright (c) 2026, Wolfgang Hoschek
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stddef.h>
#include <libintl.h>
#include <libzfs.h>
#include <libzutil.h>
#include <sys/mntent.h>

#include "libzfs_impl.h"

static int
zfs_iter_clones(zfs_handle_t *zhp, int flags __maybe_unused, zfs_iter_f func,
    void *data)
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

static int
zfs_do_list_ioctl(zfs_handle_t *zhp, int arg, zfs_cmd_t *zc)
{
	int rc;
	uint64_t	orig_cookie;

	orig_cookie = zc->zc_cookie;
top:
	(void) strlcpy(zc->zc_name, zhp->zfs_name, sizeof (zc->zc_name));
	zc->zc_objset_stats.dds_creation_txg = 0;
	rc = zfs_ioctl(zhp->zfs_hdl, arg, zc);

	if (rc == -1) {
		switch (errno) {
		case ENOMEM:
			/* expand nvlist memory and try again */
			zcmd_expand_dst_nvlist(zhp->zfs_hdl, zc);
			zc->zc_cookie = orig_cookie;
			goto top;
		/*
		 * An errno value of ESRCH indicates normal completion.
		 * If ENOENT is returned, then the underlying dataset
		 * has been removed since we obtained the handle.
		 */
		case ESRCH:
		case ENOENT:
			rc = 1;
			break;
		default:
			rc = zfs_standard_error(zhp->zfs_hdl, errno,
			    dgettext(TEXT_DOMAIN,
			    "cannot iterate filesystems"));
			break;
		}
	}
	return (rc);
}

static int
zfs_batch_add_uint64_prop(nvlist_t *props, zfs_prop_t prop, uint64_t value)
{
	nvlist_t *propval;
	int error;

	if ((error = nvlist_alloc(&propval, NV_UNIQUE_NAME, 0)) != 0)
		return (error);
	if ((error = nvlist_add_uint64(propval, ZPROP_VALUE, value)) == 0) {
		error = nvlist_add_nvlist(props, zfs_prop_to_name(prop),
		    propval);
	}
	nvlist_free(propval);
	return (error);
}

/*
 * Keep the userspace limit independent of the kernel tunable so a future
 * kernel can increase its limit without exceeding this caller's destination
 * buffer.  A native packed result needs at most 264 bytes for its name (the
 * string-array slot and ZFS_MAX_DATASET_NAME_LEN bytes), plus eight bytes for
 * each uint64 array and one byte for each uint8 array.  Reserving twice
 * ZFS_MAX_DATASET_NAME_LEN bytes for each name, plus the array storage and
 * 4 KiB for nvpair headers, alignment, and fixed metadata, is a conservative
 * bound for every currently supported property combination.
 */
#define	SNAPSHOT_LIST_BATCH_MAX_RESULTS		1024
#define	SNAPSHOT_LIST_BATCH_NVLIST_SIZE(num_uint64_arrays, num_uint8_arrays) \
	((4 * 1024) + SNAPSHOT_LIST_BATCH_MAX_RESULTS *			\
	(2 * ZFS_MAX_DATASET_NAME_LEN +					\
	(num_uint64_arrays) * sizeof (uint64_t) +			\
	(num_uint8_arrays) * sizeof (uint8_t)))

static int
make_dataset_batch_handle(zfs_handle_t *pzhp, const char *snapname,
    dmu_objset_type_t dmu_type, uint8_t dds_flags,
    const uint64_t *createtxg, const uint64_t *guid,
    const uint64_t *objsetid, const uint64_t *creation,
    const uint64_t *userrefs, const uint64_t *numclones,
    const uint64_t *used, const uint64_t *referenced,
    const uint64_t *logicalreferenced, const uint8_t *inconsistent,
    const uint8_t *redacted, const uint8_t *defer_destroy,
    zfs_handle_t **result)
{
	zfs_handle_t *zhp = calloc(1, sizeof (zfs_handle_t));
	int error;

	*result = NULL;
	if (zhp == NULL)
		return (ENOMEM);

	zhp->zfs_hdl = pzhp->zfs_hdl;
	zhp->zpool_hdl = zfs_get_pool_handle(pzhp);
	zhp->zfs_type = ZFS_TYPE_SNAPSHOT;
	zhp->zfs_head_type = dmu_type == DMU_OST_ZVOL ?
	    ZFS_TYPE_VOLUME : ZFS_TYPE_FILESYSTEM;
	zhp->zfs_dmustats.dds_type = dmu_type;
	zhp->zfs_dmustats.dds_is_snapshot = B_TRUE;
	if (createtxg != NULL)
		zhp->zfs_dmustats.dds_creation_txg = *createtxg;
	if (guid != NULL)
		zhp->zfs_dmustats.dds_guid = *guid;
	if (numclones != NULL)
		zhp->zfs_dmustats.dds_num_clones = *numclones;
	if (inconsistent != NULL)
		zhp->zfs_dmustats.dds_inconsistent = *inconsistent;
	if (redacted != NULL)
		zhp->zfs_dmustats.dds_redacted = *redacted;
	zhp->zfs_dmustats.dds_flags = dds_flags;
	if (creation != NULL) {
		zhp->zfs_projected_creation = *creation;
		zhp->zfs_projected_props |= ZFS_PROJECTED_CREATION;
	}
	if (userrefs != NULL) {
		zhp->zfs_projected_userrefs = *userrefs;
		zhp->zfs_projected_props |= ZFS_PROJECTED_USERREFS;
	}

	if (strlcpy(zhp->zfs_name, pzhp->zfs_name,
	    sizeof (zhp->zfs_name)) >= sizeof (zhp->zfs_name) ||
	    strlcat(zhp->zfs_name, "@", sizeof (zhp->zfs_name)) >=
	    sizeof (zhp->zfs_name) ||
	    strlcat(zhp->zfs_name, snapname, sizeof (zhp->zfs_name)) >=
	    sizeof (zhp->zfs_name)) {
		error = ENAMETOOLONG;
		goto fail;
	}

	if ((error = nvlist_alloc(&zhp->zfs_props, NV_UNIQUE_NAME, 0)) != 0 ||
	    (error = nvlist_alloc(&zhp->zfs_user_props, NV_UNIQUE_NAME, 0)) !=
	    0) {
		goto fail;
	}
	if ((used != NULL && (error = zfs_batch_add_uint64_prop(
	    zhp->zfs_props, ZFS_PROP_USED, *used)) != 0) ||
	    (objsetid != NULL && (error = zfs_batch_add_uint64_prop(
	    zhp->zfs_props, ZFS_PROP_OBJSETID, *objsetid)) != 0) ||
	    (referenced != NULL && (error = zfs_batch_add_uint64_prop(
	    zhp->zfs_props, ZFS_PROP_REFERENCED, *referenced)) != 0) ||
	    (logicalreferenced != NULL &&
	    (error = zfs_batch_add_uint64_prop(zhp->zfs_props,
	    ZFS_PROP_LOGICALREFERENCED, *logicalreferenced)) != 0) ||
	    (defer_destroy != NULL && (error = zfs_batch_add_uint64_prop(
	    zhp->zfs_props, ZFS_PROP_DEFER_DESTROY, *defer_destroy)) != 0)) {
		goto fail;
	}

	*result = zhp;
	return (0);

fail:
	zfs_close(zhp);
	return (error);
}

static int
zfs_do_snapshot_list_batch_ioctl(zfs_handle_t *zhp, int flags,
    uint64_t cursor, uint64_t min_txg, uint64_t max_txg, nvlist_t **result,
    boolean_t *ioctl_eof)
{
	zfs_cmd_t zc = {"\0"};
	nvlist_t *args = fnvlist_alloc();
	nvlist_t *props = fnvlist_alloc();
	int error = 0;

	*result = NULL;
	*ioctl_eof = B_FALSE;

	fnvlist_add_uint64(args, SNAP_ITER_BATCH_CURSOR, cursor);
	fnvlist_add_uint64(args, SNAP_ITER_BATCH_MAX_RESULTS,
	    SNAPSHOT_LIST_BATCH_MAX_RESULTS);
	if (min_txg != 0)
		fnvlist_add_uint64(args, SNAP_ITER_MIN_TXG, min_txg);
	if (max_txg != 0)
		fnvlist_add_uint64(args, SNAP_ITER_MAX_TXG, max_txg);
	if (flags & ZFS_ITER_BATCHED_CREATETXG) {
		fnvlist_add_boolean(props,
		    zfs_prop_to_name(ZFS_PROP_CREATETXG));
	}
	if (flags & ZFS_ITER_BATCHED_CREATION) {
		fnvlist_add_boolean(props,
		    zfs_prop_to_name(ZFS_PROP_CREATION));
	}
	if (flags & ZFS_ITER_BATCHED_GUID)
		fnvlist_add_boolean(props, zfs_prop_to_name(ZFS_PROP_GUID));
	if (flags & ZFS_ITER_BATCHED_USERREFS) {
		fnvlist_add_boolean(props,
		    zfs_prop_to_name(ZFS_PROP_USERREFS));
	}
	if (flags & ZFS_ITER_BATCHED_NUMCLONES) {
		fnvlist_add_boolean(props,
		    zfs_prop_to_name(ZFS_PROP_NUMCLONES));
	}
	if (flags & ZFS_ITER_BATCHED_INCONSISTENT) {
		fnvlist_add_boolean(props,
		    zfs_prop_to_name(ZFS_PROP_INCONSISTENT));
	}
	if (flags & ZFS_ITER_BATCHED_REDACTED) {
		fnvlist_add_boolean(props,
		    zfs_prop_to_name(ZFS_PROP_REDACTED));
	}
	if (flags & ZFS_ITER_BATCHED_USED)
		fnvlist_add_boolean(props, zfs_prop_to_name(ZFS_PROP_USED));
	if (flags & ZFS_ITER_BATCHED_REFERENCED) {
		fnvlist_add_boolean(props,
		    zfs_prop_to_name(ZFS_PROP_REFERENCED));
	}
	if (flags & ZFS_ITER_BATCHED_LOGICALREFERENCED) {
		fnvlist_add_boolean(props,
		    zfs_prop_to_name(ZFS_PROP_LOGICALREFERENCED));
	}
	if (flags & ZFS_ITER_BATCHED_DEFER_DESTROY) {
		fnvlist_add_boolean(props,
		    zfs_prop_to_name(ZFS_PROP_DEFER_DESTROY));
	}
	if (flags & ZFS_ITER_BATCHED_OBJSETID)
		fnvlist_add_boolean(props, zfs_prop_to_name(ZFS_PROP_OBJSETID));
	fnvlist_add_nvlist(args, SNAP_ITER_BATCH_PROPS, props);

	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));
	zcmd_write_src_nvlist(zhp->zfs_hdl, &zc, args);
	zcmd_alloc_dst_nvlist(zhp->zfs_hdl, &zc,
	    SNAPSHOT_LIST_BATCH_NVLIST_SIZE(9, 3));
	for (;;) {
		int ioctl_errno = 0;

		if (zfs_ioctl(zhp->zfs_hdl, ZFS_IOC_SNAPSHOT_LIST_BATCH,
		    &zc) != 0) {
			ioctl_errno = errno;
		}
		/*
		 * An ioctl error does not imply that the output nvlist is
		 * empty. It can contain snapshots collected before the error.
		 */
		if (zc.zc_nvlist_dst_filled) {
			if (zcmd_read_dst_nvlist(zhp->zfs_hdl, &zc,
			    result) != 0) {
				nvlist_free(*result);
				*result = NULL;
				error = ENOMEM;
			} else {
				error = ioctl_errno;
				/*
				 * Handler errors may return an empty
				 * output nvlist.
				 */
				if (error != 0 && nvlist_empty(*result)) {
					nvlist_free(*result);
					*result = NULL;
				}
			}
			break;
		}
		if (ioctl_errno == ENOMEM) {
			zcmd_expand_dst_nvlist(zhp->zfs_hdl, &zc);
			continue;
		}
		error = ioctl_errno != 0 ? ioctl_errno : EPROTO;
		break;
	}
	if (*result == NULL)
		*ioctl_eof = error == ENOENT || error == ESRCH;

	zcmd_free_nvlists(&zc);
	fnvlist_free(props);
	fnvlist_free(args);
	return (error);
}

static int
zfs_iter_snapshots_batch(zfs_handle_t *zhp, int flags, zfs_iter_f func,
    void *data, uint64_t min_txg, uint64_t max_txg, boolean_t *unavailable)
{
	uint64_t cursor = 0;
	int ret;
	boolean_t callback_invoked = B_FALSE;

	*unavailable = B_FALSE;

	for (;;) {
		char **names = NULL;
		uint64_t *createtxgs = NULL, *guids = NULL, *objsetids = NULL;
		uint64_t *creations = NULL, *userrefs = NULL;
		uint64_t *numclones = NULL, *used = NULL;
		uint64_t *referenced = NULL, *logicalreferenced = NULL;
		uint8_t *inconsistent = NULL, *redacted = NULL;
		uint8_t *defer_destroy = NULL;
		uint_t count = 0, createtxg_count = 0, guid_count = 0;
		uint_t objsetid_count = 0;
		uint_t creation_count = 0, userref_count = 0;
		uint_t numclone_count = 0, used_count = 0;
		uint_t referenced_count = 0, logicalreferenced_count = 0;
		uint_t inconsistent_count = 0, redacted_count = 0;
		uint_t defer_destroy_count = 0;
		uint64_t next_cursor, dmu_type, dds_flags;
		nvlist_t *batch = NULL;
		boolean_t eof = B_FALSE, ioctl_eof;
		int ioctl_errno;

		ret = zfs_do_snapshot_list_batch_ioctl(zhp, flags, cursor,
		    min_txg, max_txg, &batch, &ioctl_eof);
		if (ioctl_eof)
			return (0);
		if (batch == NULL) {
			if (ret == 0)
				ret = EPROTO;
			if (ret == ZFS_ERR_IOC_CMD_UNAVAIL ||
			    ret == ZFS_ERR_IOC_ARG_UNAVAIL || ret == ENOTTY ||
			    ret == ENOTSUP) {
				if (callback_invoked == B_FALSE) {
					*unavailable = B_TRUE;
					return (ret);
				}
			}
			errno = ret;
			return (zfs_standard_error(zhp->zfs_hdl, errno,
			    dgettext(TEXT_DOMAIN,
			    "cannot iterate filesystems")));
		}
		ioctl_errno = ret;

		if (nvlist_lookup_boolean_value(batch, SNAP_ITER_BATCH_EOF,
		    &eof) != 0 ||
		    nvlist_lookup_uint64(batch, SNAP_ITER_BATCH_CURSOR,
		    &next_cursor) != 0) {
			ret = EPROTO;
			goto malformed;
		}
		if (nvlist_lookup_uint64(batch, SNAP_ITER_BATCH_DMU_TYPE,
		    &dmu_type) != 0 ||
		    nvlist_lookup_uint64(batch, SNAP_ITER_BATCH_DDS_FLAGS,
		    &dds_flags) != 0 ||
		    (dmu_type != DMU_OST_ZFS && dmu_type != DMU_OST_ZVOL) ||
		    dds_flags > UINT8_MAX ||
		    (dds_flags & DDS_FLAG_HAS_ENCRYPTED) == 0) {
			ret = EPROTO;
			goto malformed;
		}

		ret = nvlist_lookup_string_array(batch, SNAP_ITER_BATCH_NAMES,
		    &names, &count);
		if (ret == ENOENT) {
			count = 0;
			ret = 0;
		} else if (ret != 0) {
			ret = EPROTO;
			goto malformed;
		}
		if (count > SNAPSHOT_LIST_BATCH_MAX_RESULTS) {
			ret = EPROTO;
			goto malformed;
		}
		if (count != 0 && (flags & ZFS_ITER_BATCHED_CREATETXG) &&
		    (nvlist_lookup_uint64_array(batch,
		    zfs_prop_to_name(ZFS_PROP_CREATETXG), &createtxgs,
		    &createtxg_count) != 0 || count != createtxg_count)) {
			ret = EPROTO;
			goto malformed;
		}
		if (count != 0 && (flags & ZFS_ITER_BATCHED_GUID) &&
		    (nvlist_lookup_uint64_array(batch,
		    zfs_prop_to_name(ZFS_PROP_GUID), &guids,
		    &guid_count) != 0 || count != guid_count)) {
			ret = EPROTO;
			goto malformed;
		}
		if (count != 0 && (flags & ZFS_ITER_BATCHED_OBJSETID) &&
		    (nvlist_lookup_uint64_array(batch,
		    zfs_prop_to_name(ZFS_PROP_OBJSETID), &objsetids,
		    &objsetid_count) != 0 || count != objsetid_count)) {
			ret = EPROTO;
			goto malformed;
		}

		if (count != 0 && (flags & ZFS_ITER_BATCHED_CREATION) &&
		    (nvlist_lookup_uint64_array(batch,
		    zfs_prop_to_name(ZFS_PROP_CREATION), &creations,
		    &creation_count) != 0 || count != creation_count)) {
			ret = EPROTO;
			goto malformed;
		}
		if (count != 0 && (flags & ZFS_ITER_BATCHED_USERREFS) &&
		    (nvlist_lookup_uint64_array(batch,
		    zfs_prop_to_name(ZFS_PROP_USERREFS), &userrefs,
		    &userref_count) != 0 || count != userref_count)) {
			ret = EPROTO;
			goto malformed;
		}
		if (count != 0 && (flags & ZFS_ITER_BATCHED_NUMCLONES) &&
		    (nvlist_lookup_uint64_array(batch,
		    zfs_prop_to_name(ZFS_PROP_NUMCLONES), &numclones,
		    &numclone_count) != 0 || count != numclone_count)) {
			ret = EPROTO;
			goto malformed;
		}
		if (count != 0 && (flags & ZFS_ITER_BATCHED_USED) &&
		    (nvlist_lookup_uint64_array(batch,
		    zfs_prop_to_name(ZFS_PROP_USED), &used, &used_count) != 0 ||
		    count != used_count)) {
			ret = EPROTO;
			goto malformed;
		}
		if (count != 0 && (flags & ZFS_ITER_BATCHED_REFERENCED) &&
		    (nvlist_lookup_uint64_array(batch,
		    zfs_prop_to_name(ZFS_PROP_REFERENCED), &referenced,
		    &referenced_count) != 0 || count != referenced_count)) {
			ret = EPROTO;
			goto malformed;
		}
		if (count != 0 &&
		    (flags & ZFS_ITER_BATCHED_LOGICALREFERENCED) &&
		    (nvlist_lookup_uint64_array(batch,
		    zfs_prop_to_name(ZFS_PROP_LOGICALREFERENCED),
		    &logicalreferenced,
		    &logicalreferenced_count) != 0 ||
		    count != logicalreferenced_count)) {
			ret = EPROTO;
			goto malformed;
		}
		if (count != 0 && (flags & ZFS_ITER_BATCHED_INCONSISTENT) &&
		    (nvlist_lookup_uint8_array(batch,
		    zfs_prop_to_name(ZFS_PROP_INCONSISTENT), &inconsistent,
		    &inconsistent_count) != 0 ||
		    count != inconsistent_count)) {
			ret = EPROTO;
			goto malformed;
		}
		if (count != 0 && (flags & ZFS_ITER_BATCHED_REDACTED) &&
		    (nvlist_lookup_uint8_array(batch,
		    zfs_prop_to_name(ZFS_PROP_REDACTED), &redacted,
		    &redacted_count) != 0 || count != redacted_count)) {
			ret = EPROTO;
			goto malformed;
		}
		if (count != 0 && (flags & ZFS_ITER_BATCHED_DEFER_DESTROY) &&
		    (nvlist_lookup_uint8_array(batch,
		    zfs_prop_to_name(ZFS_PROP_DEFER_DESTROY), &defer_destroy,
		    &defer_destroy_count) != 0 ||
		    count != defer_destroy_count)) {
			ret = EPROTO;
			goto malformed;
		}
		for (uint_t i = 0; i < count; i++) {
			if (((flags & ZFS_ITER_BATCHED_INCONSISTENT) &&
			    inconsistent[i] > 1) ||
			    ((flags & ZFS_ITER_BATCHED_REDACTED) &&
			    redacted[i] > 1) ||
			    ((flags & ZFS_ITER_BATCHED_DEFER_DESTROY) &&
			    defer_destroy[i] > 1)) {
				ret = EPROTO;
				goto malformed;
			}
		}

		for (uint_t i = 0; i < count; i++) {
			const uint64_t *createtxg =
			    (flags & ZFS_ITER_BATCHED_CREATETXG) ?
			    &createtxgs[i] : NULL;
			const uint64_t *guid =
			    (flags & ZFS_ITER_BATCHED_GUID) ? &guids[i] : NULL;
			const uint64_t *objsetid =
			    (flags & ZFS_ITER_BATCHED_OBJSETID) ?
			    &objsetids[i] : NULL;
			const uint64_t *creation =
			    (flags & ZFS_ITER_BATCHED_CREATION) ?
			    &creations[i] : NULL;
			const uint64_t *userref =
			    (flags & ZFS_ITER_BATCHED_USERREFS) ?
			    &userrefs[i] : NULL;
			const uint64_t *numclone =
			    (flags & ZFS_ITER_BATCHED_NUMCLONES) ?
			    &numclones[i] : NULL;
			const uint64_t *used_value =
			    (flags & ZFS_ITER_BATCHED_USED) ? &used[i] : NULL;
			const uint64_t *referenced_value =
			    (flags & ZFS_ITER_BATCHED_REFERENCED) ?
			    &referenced[i] : NULL;
			const uint64_t *logicalreferenced_value =
			    (flags & ZFS_ITER_BATCHED_LOGICALREFERENCED) ?
			    &logicalreferenced[i] : NULL;
			const uint8_t *inconsistent_value =
			    (flags & ZFS_ITER_BATCHED_INCONSISTENT) ?
			    &inconsistent[i] : NULL;
			const uint8_t *redacted_value =
			    (flags & ZFS_ITER_BATCHED_REDACTED) ?
			    &redacted[i] : NULL;
			const uint8_t *defer_destroy_value =
			    (flags & ZFS_ITER_BATCHED_DEFER_DESTROY) ?
			    &defer_destroy[i] : NULL;
			zfs_handle_t *nzhp;

			ret = make_dataset_batch_handle(zhp, names[i],
			    (dmu_objset_type_t)dmu_type, (uint8_t)dds_flags,
			    createtxg, guid, objsetid, creation, userref,
			    numclone,
			    used_value, referenced_value,
			    logicalreferenced_value, inconsistent_value,
			    redacted_value, defer_destroy_value, &nzhp);
			if (ret != 0) {
				int error = ret;

				nvlist_free(batch);
				errno = error;
				if (error == ENOMEM)
					ret = no_memory(zhp->zfs_hdl);
				else
					ret = zfs_standard_error(zhp->zfs_hdl,
					    error, dgettext(TEXT_DOMAIN,
					    "cannot iterate filesystems"));
				errno = error;
				return (ret);
			}
			callback_invoked = B_TRUE;
			if ((ret = func(nzhp, data)) != 0) {
				nvlist_free(batch);
				return (ret);
			}
		}

		nvlist_free(batch);
		if (ioctl_errno != 0) {
			if (ioctl_errno == ENOENT || ioctl_errno == ESRCH)
				return (0);
			errno = ioctl_errno;
			return (zfs_standard_error(zhp->zfs_hdl, errno,
			    dgettext(TEXT_DOMAIN,
			    "cannot iterate filesystems")));
		}
		if (eof)
			return (0);
		if (next_cursor == cursor) {
			ret = EPROTO;
			batch = NULL;
			goto malformed;
		}
		cursor = next_cursor;
		continue;

malformed:
		nvlist_free(batch);
		errno = ret;
		return (zfs_standard_error(zhp->zfs_hdl, errno,
		    dgettext(TEXT_DOMAIN, "cannot iterate filesystems")));
	}
}

/*
 * Iterate over all child filesystems
 */
int
zfs_iter_filesystems(zfs_handle_t *zhp, zfs_iter_f func, void *data)
{
	return (zfs_iter_filesystems_v2(zhp, 0, func, data));
}

int
zfs_iter_filesystems_v2(zfs_handle_t *zhp, int flags, zfs_iter_f func,
    void *data)
{
	zfs_cmd_t zc = {"\0"};
	zfs_handle_t *nzhp;
	int ret;

	if (zhp->zfs_type != ZFS_TYPE_FILESYSTEM)
		return (0);

	zcmd_alloc_dst_nvlist(zhp->zfs_hdl, &zc, 0);

	if ((flags & ZFS_ITER_SIMPLE) == ZFS_ITER_SIMPLE)
		zc.zc_simple = B_TRUE;

	while ((ret = zfs_do_list_ioctl(zhp, ZFS_IOC_DATASET_LIST_NEXT,
	    &zc)) == 0) {
		if (zc.zc_simple)
			nzhp = make_dataset_simple_handle_zc(zhp, &zc);
		else
			nzhp = make_dataset_handle_zc(zhp->zfs_hdl, &zc);
		/*
		 * Silently ignore errors, as the only plausible explanation is
		 * that the pool has since been removed.
		 */
		if (nzhp == NULL)
			continue;

		if ((ret = func(nzhp, data)) != 0) {
			zcmd_free_nvlists(&zc);
			return (ret);
		}
	}
	zcmd_free_nvlists(&zc);
	return ((ret < 0) ? ret : 0);
}

/*
 * Iterate over all snapshots
 */
int
zfs_iter_snapshots(zfs_handle_t *zhp, boolean_t simple, zfs_iter_f func,
    void *data, uint64_t min_txg, uint64_t max_txg)
{
	return (zfs_iter_snapshots_v2(zhp, simple ? ZFS_ITER_SIMPLE : 0, func,
	    data, min_txg, max_txg));
}

int
zfs_iter_snapshots_v2(zfs_handle_t *zhp, int flags, zfs_iter_f func,
    void *data, uint64_t min_txg, uint64_t max_txg)
{
	zfs_cmd_t zc = {"\0"};
	zfs_handle_t *nzhp;
	int ret;
	boolean_t unavailable;
	nvlist_t *range_nvl = NULL;

	if (zhp->zfs_type == ZFS_TYPE_SNAPSHOT ||
	    zhp->zfs_type == ZFS_TYPE_BOOKMARK)
		return (0);
	if (flags & ZFS_ITER_BATCHED) {
		ret = zfs_iter_snapshots_batch(zhp, flags, func, data, min_txg,
		    max_txg, &unavailable);
		if (!unavailable)
			return (ret);
	}

	zc.zc_simple = (flags & ZFS_ITER_SIMPLE) != 0;

	zcmd_alloc_dst_nvlist(zhp->zfs_hdl, &zc, 0);

	if (min_txg != 0) {
		range_nvl = fnvlist_alloc();
		fnvlist_add_uint64(range_nvl, SNAP_ITER_MIN_TXG, min_txg);
	}
	if (max_txg != 0) {
		if (range_nvl == NULL)
			range_nvl = fnvlist_alloc();
		fnvlist_add_uint64(range_nvl, SNAP_ITER_MAX_TXG, max_txg);
	}

	if (range_nvl != NULL)
		zcmd_write_src_nvlist(zhp->zfs_hdl, &zc, range_nvl);

	while ((ret = zfs_do_list_ioctl(zhp, ZFS_IOC_SNAPSHOT_LIST_NEXT,
	    &zc)) == 0) {

		if (zc.zc_simple)
			nzhp = make_dataset_simple_handle_zc(zhp, &zc);
		else
			nzhp = make_dataset_handle_zc(zhp->zfs_hdl, &zc);
		if (nzhp == NULL)
			continue;

		if ((ret = func(nzhp, data)) != 0) {
			zcmd_free_nvlists(&zc);
			fnvlist_free(range_nvl);
			return (ret);
		}
	}
	zcmd_free_nvlists(&zc);
	fnvlist_free(range_nvl);
	return ((ret < 0) ? ret : 0);
}

/*
 * Iterate over all bookmarks
 */
int
zfs_iter_bookmarks(zfs_handle_t *zhp, zfs_iter_f func, void *data)
{
	return (zfs_iter_bookmarks_v2(zhp, 0, func, data));
}

int
zfs_iter_bookmarks_v2(zfs_handle_t *zhp, int flags, zfs_iter_f func,
    void *data)
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
	if (flags & ZFS_ITER_BATCHED) {
		if (flags & ZFS_ITER_BATCHED_GUID) {
			fnvlist_add_boolean(props,
			    zfs_prop_to_name(ZFS_PROP_GUID));
		}
		if (flags & ZFS_ITER_BATCHED_CREATETXG) {
			fnvlist_add_boolean(props,
			    zfs_prop_to_name(ZFS_PROP_CREATETXG));
		}
		if (flags & ZFS_ITER_BATCHED_CREATION) {
			fnvlist_add_boolean(props,
			    zfs_prop_to_name(ZFS_PROP_CREATION));
		}
		if (flags & ZFS_ITER_BATCHED_REFERENCED) {
			fnvlist_add_boolean(props,
			    zfs_prop_to_name(ZFS_PROP_REFERENCED));
		}
		if (flags & ZFS_ITER_BATCHED_LOGICALREFERENCED) {
			fnvlist_add_boolean(props,
			    zfs_prop_to_name(ZFS_PROP_LOGICALREFERENCED));
		}
	} else {
		for (zfs_prop_t p = 0; p < ZFS_NUM_PROPS; p++) {
			if (zfs_prop_valid_for_type(p, ZFS_TYPE_BOOKMARK,
			    B_FALSE)) {
				fnvlist_add_boolean(props, zfs_prop_to_name(p));
			}
		}
		fnvlist_add_boolean(props, "redact_complete");
	}

	if ((err = lzc_get_bookmarks(zhp->zfs_name, props, &bmarks)) != 0)
		goto out;

	for (pair = nvlist_next_nvpair(bmarks, NULL);
	    pair != NULL; pair = nvlist_next_nvpair(bmarks, pair)) {
		char name[ZFS_MAX_DATASET_NAME_LEN];
		const char *bmark_name;
		nvlist_t *bmark_props;

		bmark_name = nvpair_name(pair);
		bmark_props = fnvpair_value_nvlist(pair);

		if (snprintf(name, sizeof (name), "%s#%s", zhp->zfs_name,
		    bmark_name) >= sizeof (name)) {
			err = EINVAL;
			goto out;
		}

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

	return (TREE_CMP(lcreate, rcreate));
}

int
zfs_iter_snapshots_sorted(zfs_handle_t *zhp, zfs_iter_f callback,
    void *data, uint64_t min_txg, uint64_t max_txg)
{
	return (zfs_iter_snapshots_sorted_v2(zhp, 0, callback, data,
	    min_txg, max_txg));
}

int
zfs_iter_snapshots_sorted_v2(zfs_handle_t *zhp, int flags, zfs_iter_f callback,
    void *data, uint64_t min_txg, uint64_t max_txg)
{
	int ret = 0;
	zfs_node_t *node;
	avl_tree_t avl;
	void *cookie = NULL;

	avl_create(&avl, zfs_snapshot_compare,
	    sizeof (zfs_node_t), offsetof(zfs_node_t, zn_avlnode));

	/* zfs_snapshot_compare() requires the creation TXG. */
	if (flags & ZFS_ITER_BATCHED)
		flags |= ZFS_ITER_BATCHED_CREATETXG;
	ret = zfs_iter_snapshots_v2(zhp, flags, zfs_sort_snaps, &avl, min_txg,
	    max_txg);

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
snapspec_cb(zfs_handle_t *zhp, void *arg)
{
	snapspec_arg_t *ssa = arg;
	const char *shortsnapname;
	int err = 0;

	if (ssa->ssa_seenlast)
		return (0);

	shortsnapname = strchr(zfs_get_name(zhp), '@') + 1;
	if (!ssa->ssa_seenfirst && strcmp(shortsnapname, ssa->ssa_first) == 0)
		ssa->ssa_seenfirst = B_TRUE;
	if (strcmp(shortsnapname, ssa->ssa_last) == 0)
		ssa->ssa_seenlast = B_TRUE;

	if (ssa->ssa_seenfirst) {
		err = ssa->ssa_func(zhp, ssa->ssa_arg);
	} else {
		zfs_close(zhp);
	}

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
	return (zfs_iter_snapspec_v2(fs_zhp, 0, spec_orig, func, arg));
}

int
zfs_iter_snapspec_v2(zfs_handle_t *fs_zhp, int flags, const char *spec_orig,
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

			err = zfs_iter_snapshots_sorted_v2(fs_zhp, flags,
			    snapspec_cb, &ssa, 0, 0);
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
 * Process snapshots before filesystems because they are nearer the input
 * handle: this is extremely important when used with zfs_iter_f functions
 * looking for data, following the logic that we would like to find it as soon
 * and as close as possible.
 */
int
zfs_iter_children(zfs_handle_t *zhp, zfs_iter_f func, void *data)
{
	return (zfs_iter_children_v2(zhp, 0, func, data));
}

int
zfs_iter_children_v2(zfs_handle_t *zhp, int flags, zfs_iter_f func, void *data)
{
	int ret;

	if ((ret = zfs_iter_snapshots_v2(zhp, flags, func, data, 0, 0)) != 0)
		return (ret);

	return (zfs_iter_filesystems_v2(zhp, flags, func, data));
}


typedef struct iter_stack_frame {
	struct iter_stack_frame *next;
	zfs_handle_t *zhp;
} iter_stack_frame_t;

typedef struct iter_dependents_arg {
	boolean_t first;
	int flags;
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
		err = zfs_iter_clones(zhp, ida->flags, iter_dependents_cb, ida);
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
		err = zfs_iter_filesystems_v2(zhp, ida->flags,
		    iter_dependents_cb, ida);
		if (err == 0)
			err = zfs_iter_snapshots_sorted_v2(zhp, ida->flags,
			    iter_dependents_cb, ida, 0, 0);
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
	return (zfs_iter_dependents_v2(zhp, 0, allowrecursion, func, data));
}

/*
 * Iterate dependents with full snapshot handles because clone discovery reads
 * properties which projected handles deliberately omit.
 */
int
zfs_iter_dependents_v2(zfs_handle_t *zhp, int flags, boolean_t allowrecursion,
    zfs_iter_f func, void *data)
{
	iter_dependents_arg_t ida;

	/*
	 * Both batched and simple snapshot handles omit properties needed for
	 * clone discovery, so dependent traversal must use full handles.
	 */
	ida.flags = flags & ~(ZFS_ITER_BATCHED | ZFS_ITER_SIMPLE);
	ida.allowrecursion = allowrecursion;
	ida.stack = NULL;
	ida.func = func;
	ida.data = data;
	ida.first = B_TRUE;
	return (iter_dependents_cb(zfs_handle_dup(zhp), &ida));
}

/*
 * Iterate over mounted children of the specified dataset
 */
int
zfs_iter_mounted(zfs_handle_t *zhp, zfs_iter_f func, void *data)
{
	char mnt_prop[ZFS_MAXPROPLEN];
	struct mnttab entry;
	zfs_handle_t *mtab_zhp;
	size_t namelen = strlen(zhp->zfs_name);
	FILE *mnttab;
	int err = 0;

	if ((mnttab = fopen(MNTTAB, "re")) == NULL)
		return (ENOENT);

	while (err == 0 && getmntent(mnttab, &entry) == 0) {
		/* Ignore non-ZFS entries */
		if (strcmp(entry.mnt_fstype, MNTTYPE_ZFS) != 0)
			continue;

		/* Ignore datasets not within the provided dataset */
		if (strncmp(entry.mnt_special, zhp->zfs_name, namelen) != 0 ||
		    entry.mnt_special[namelen] != '/')
			continue;

		/* Skip snapshot of any child dataset */
		if (strchr(entry.mnt_special, '@') != NULL)
			continue;

		if ((mtab_zhp = zfs_open(zhp->zfs_hdl, entry.mnt_special,
		    ZFS_TYPE_FILESYSTEM)) == NULL)
			continue;

		/* Ignore legacy mounts as they are user managed */
		verify(zfs_prop_get(mtab_zhp, ZFS_PROP_MOUNTPOINT, mnt_prop,
		    sizeof (mnt_prop), NULL, NULL, 0, B_FALSE) == 0);
		if (strcmp(mnt_prop, "legacy") == 0) {
			zfs_close(mtab_zhp);
			continue;
		}

		err = func(mtab_zhp, data);
	}

	fclose(mnttab);

	return (err);
}
