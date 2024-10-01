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
 * Copyright (c) 2011, 2020 by Delphix. All rights reserved.
 * Copyright (c) 2012, Joyent, Inc. All rights reserved.
 * Copyright (c) 2012 Pawel Jakub Dawidek <pawel@dawidek.net>.
 * All rights reserved
 * Copyright (c) 2013 Steven Hartland. All rights reserved.
 * Copyright 2015, OmniTI Computer Consulting, Inc. All rights reserved.
 * Copyright 2016 Igor Kozhukhov <ikozhukhov@gmail.com>
 * Copyright (c) 2018, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
 * Copyright (c) 2019 Datto Inc.
 * Copyright (c) 2024, Klara, Inc.
 */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <libintl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stddef.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/mntent.h>
#include <sys/mnttab.h>
#include <sys/avl.h>
#include <sys/debug.h>
#include <sys/stat.h>
#include <pthread.h>
#include <umem.h>
#include <time.h>

#include <libzfs.h>
#include <libzfs_core.h>
#include <libzutil.h>

#include "zfs_namecheck.h"
#include "zfs_prop.h"
#include "zfs_fletcher.h"
#include "libzfs_impl.h"
#include <cityhash.h>
#include <zlib.h>
#include <sys/zio_checksum.h>
#include <sys/dsl_crypt.h>
#include <sys/ddt.h>
#include <sys/socket.h>
#include <sys/sha2.h>

static int zfs_receive_impl(libzfs_handle_t *, const char *, const char *,
    recvflags_t *, int, const char *, nvlist_t *, avl_tree_t *, char **,
    const char *, nvlist_t *);
static int guid_to_name_redact_snaps(libzfs_handle_t *hdl, const char *parent,
    uint64_t guid, boolean_t bookmark_ok, uint64_t *redact_snap_guids,
    uint64_t num_redact_snaps, char *name);
static int guid_to_name(libzfs_handle_t *, const char *,
    uint64_t, boolean_t, char *);

typedef struct progress_arg {
	zfs_handle_t *pa_zhp;
	int pa_fd;
	boolean_t pa_parsable;
	boolean_t pa_estimate;
	int pa_verbosity;
	boolean_t pa_astitle;
	boolean_t pa_progress;
	uint64_t pa_size;
} progress_arg_t;

static int
dump_record(dmu_replay_record_t *drr, void *payload, size_t payload_len,
    zio_cksum_t *zc, int outfd)
{
	ASSERT3U(offsetof(dmu_replay_record_t, drr_u.drr_checksum.drr_checksum),
	    ==, sizeof (dmu_replay_record_t) - sizeof (zio_cksum_t));
	fletcher_4_incremental_native(drr,
	    offsetof(dmu_replay_record_t, drr_u.drr_checksum.drr_checksum), zc);
	if (drr->drr_type != DRR_BEGIN) {
		ASSERT(ZIO_CHECKSUM_IS_ZERO(&drr->drr_u.
		    drr_checksum.drr_checksum));
		drr->drr_u.drr_checksum.drr_checksum = *zc;
	}
	fletcher_4_incremental_native(&drr->drr_u.drr_checksum.drr_checksum,
	    sizeof (zio_cksum_t), zc);
	if (write(outfd, drr, sizeof (*drr)) == -1)
		return (errno);
	if (payload_len != 0) {
		fletcher_4_incremental_native(payload, payload_len, zc);
		if (write(outfd, payload, payload_len) == -1)
			return (errno);
	}
	return (0);
}

/*
 * Routines for dealing with the AVL tree of fs-nvlists
 */
typedef struct fsavl_node {
	avl_node_t fn_node;
	nvlist_t *fn_nvfs;
	const char *fn_snapname;
	uint64_t fn_guid;
} fsavl_node_t;

static int
fsavl_compare(const void *arg1, const void *arg2)
{
	const fsavl_node_t *fn1 = (const fsavl_node_t *)arg1;
	const fsavl_node_t *fn2 = (const fsavl_node_t *)arg2;

	return (TREE_CMP(fn1->fn_guid, fn2->fn_guid));
}

/*
 * Given the GUID of a snapshot, find its containing filesystem and
 * (optionally) name.
 */
static nvlist_t *
fsavl_find(avl_tree_t *avl, uint64_t snapguid, const char **snapname)
{
	fsavl_node_t fn_find;
	fsavl_node_t *fn;

	fn_find.fn_guid = snapguid;

	fn = avl_find(avl, &fn_find, NULL);
	if (fn) {
		if (snapname)
			*snapname = fn->fn_snapname;
		return (fn->fn_nvfs);
	}
	return (NULL);
}

static void
fsavl_destroy(avl_tree_t *avl)
{
	fsavl_node_t *fn;
	void *cookie;

	if (avl == NULL)
		return;

	cookie = NULL;
	while ((fn = avl_destroy_nodes(avl, &cookie)) != NULL)
		free(fn);
	avl_destroy(avl);
	free(avl);
}

/*
 * Given an nvlist, produce an avl tree of snapshots, ordered by guid
 */
static avl_tree_t *
fsavl_create(nvlist_t *fss)
{
	avl_tree_t *fsavl;
	nvpair_t *fselem = NULL;

	if ((fsavl = malloc(sizeof (avl_tree_t))) == NULL)
		return (NULL);

	avl_create(fsavl, fsavl_compare, sizeof (fsavl_node_t),
	    offsetof(fsavl_node_t, fn_node));

	while ((fselem = nvlist_next_nvpair(fss, fselem)) != NULL) {
		nvlist_t *nvfs, *snaps;
		nvpair_t *snapelem = NULL;

		nvfs = fnvpair_value_nvlist(fselem);
		snaps = fnvlist_lookup_nvlist(nvfs, "snaps");

		while ((snapelem =
		    nvlist_next_nvpair(snaps, snapelem)) != NULL) {
			fsavl_node_t *fn;

			if ((fn = malloc(sizeof (fsavl_node_t))) == NULL) {
				fsavl_destroy(fsavl);
				return (NULL);
			}
			fn->fn_nvfs = nvfs;
			fn->fn_snapname = nvpair_name(snapelem);
			fn->fn_guid = fnvpair_value_uint64(snapelem);

			/*
			 * Note: if there are multiple snaps with the
			 * same GUID, we ignore all but one.
			 */
			avl_index_t where = 0;
			if (avl_find(fsavl, fn, &where) == NULL)
				avl_insert(fsavl, fn, where);
			else
				free(fn);
		}
	}

	return (fsavl);
}

/*
 * Routines for dealing with the giant nvlist of fs-nvlists, etc.
 */
typedef struct send_data {
	/*
	 * assigned inside every recursive call,
	 * restored from *_save on return:
	 *
	 * guid of fromsnap snapshot in parent dataset
	 * txg of fromsnap snapshot in current dataset
	 * txg of tosnap snapshot in current dataset
	 */

	uint64_t parent_fromsnap_guid;
	uint64_t fromsnap_txg;
	uint64_t tosnap_txg;

	/* the nvlists get accumulated during depth-first traversal */
	nvlist_t *parent_snaps;
	nvlist_t *fss;
	nvlist_t *snapprops;
	nvlist_t *snapholds;	/* user holds */

	/* send-receive configuration, does not change during traversal */
	const char *fsname;
	const char *fromsnap;
	const char *tosnap;
	boolean_t recursive;
	boolean_t raw;
	boolean_t doall;
	boolean_t replicate;
	boolean_t skipmissing;
	boolean_t verbose;
	boolean_t backup;
	boolean_t seenfrom;
	boolean_t seento;
	boolean_t holds;	/* were holds requested with send -h */
	boolean_t props;

	/*
	 * The header nvlist is of the following format:
	 * {
	 *   "tosnap" -> string
	 *   "fromsnap" -> string (if incremental)
	 *   "fss" -> {
	 *	id -> {
	 *
	 *	 "name" -> string (full name; for debugging)
	 *	 "parentfromsnap" -> number (guid of fromsnap in parent)
	 *
	 *	 "props" -> { name -> value (only if set here) }
	 *	 "snaps" -> { name (lastname) -> number (guid) }
	 *	 "snapprops" -> { name (lastname) -> { name -> value } }
	 *	 "snapholds" -> { name (lastname) -> { holdname -> crtime } }
	 *
	 *	 "origin" -> number (guid) (if clone)
	 *	 "is_encroot" -> boolean
	 *	 "sent" -> boolean (not on-disk)
	 *	}
	 *   }
	 * }
	 *
	 */
} send_data_t;

static void
send_iterate_prop(zfs_handle_t *zhp, boolean_t received_only, nvlist_t *nv);

/*
 * Collect guid, valid props, optionally holds, etc. of a snapshot.
 * This interface is intended for use as a zfs_iter_snapshots_v2_sorted visitor.
 */
static int
send_iterate_snap(zfs_handle_t *zhp, void *arg)
{
	send_data_t *sd = arg;
	uint64_t guid = zhp->zfs_dmustats.dds_guid;
	uint64_t txg = zhp->zfs_dmustats.dds_creation_txg;
	boolean_t isfromsnap, istosnap, istosnapwithnofrom;
	char *snapname;
	const char *from = sd->fromsnap;
	const char *to = sd->tosnap;

	snapname = strrchr(zhp->zfs_name, '@');
	assert(snapname != NULL);
	++snapname;

	isfromsnap = (from != NULL && strcmp(from, snapname) == 0);
	istosnap = (to != NULL && strcmp(to, snapname) == 0);
	istosnapwithnofrom = (istosnap && from == NULL);

	if (sd->tosnap_txg != 0 && txg > sd->tosnap_txg) {
		if (sd->verbose) {
			(void) fprintf(stderr, dgettext(TEXT_DOMAIN,
			    "skipping snapshot %s because it was created "
			    "after the destination snapshot (%s)\n"),
			    zhp->zfs_name, to);
		}
		zfs_close(zhp);
		return (0);
	}

	fnvlist_add_uint64(sd->parent_snaps, snapname, guid);

	/*
	 * NB: if there is no fromsnap here (it's a newly created fs in
	 * an incremental replication), we will substitute the tosnap.
	 */
	if (isfromsnap || (sd->parent_fromsnap_guid == 0 && istosnap))
		sd->parent_fromsnap_guid = guid;

	if (!sd->recursive) {
		/*
		 * To allow a doall stream to work properly
		 * with a NULL fromsnap
		 */
		if (sd->doall && from == NULL && !sd->seenfrom)
			sd->seenfrom = B_TRUE;

		if (!sd->seenfrom && isfromsnap) {
			sd->seenfrom = B_TRUE;
			zfs_close(zhp);
			return (0);
		}

		if ((sd->seento || !sd->seenfrom) && !istosnapwithnofrom) {
			zfs_close(zhp);
			return (0);
		}

		if (istosnap)
			sd->seento = B_TRUE;
	}

	nvlist_t *nv = fnvlist_alloc();
	send_iterate_prop(zhp, sd->backup, nv);
	fnvlist_add_nvlist(sd->snapprops, snapname, nv);
	fnvlist_free(nv);

	if (sd->holds) {
		nvlist_t *holds;
		if (lzc_get_holds(zhp->zfs_name, &holds) == 0) {
			fnvlist_add_nvlist(sd->snapholds, snapname, holds);
			fnvlist_free(holds);
		}
	}

	zfs_close(zhp);
	return (0);
}

/*
 * Collect all valid props from the handle snap into an nvlist.
 */
static void
send_iterate_prop(zfs_handle_t *zhp, boolean_t received_only, nvlist_t *nv)
{
	nvlist_t *props;

	if (received_only)
		props = zfs_get_recvd_props(zhp);
	else
		props = zhp->zfs_props;

	nvpair_t *elem = NULL;
	while ((elem = nvlist_next_nvpair(props, elem)) != NULL) {
		const char *propname = nvpair_name(elem);
		zfs_prop_t prop = zfs_name_to_prop(propname);

		if (!zfs_prop_user(propname)) {
			/*
			 * Realistically, this should never happen.  However,
			 * we want the ability to add DSL properties without
			 * needing to make incompatible version changes.  We
			 * need to ignore unknown properties to allow older
			 * software to still send datasets containing these
			 * properties, with the unknown properties elided.
			 */
			if (prop == ZPROP_INVAL)
				continue;

			if (zfs_prop_readonly(prop))
				continue;
		}

		nvlist_t *propnv = fnvpair_value_nvlist(elem);

		boolean_t isspacelimit = (prop == ZFS_PROP_QUOTA ||
		    prop == ZFS_PROP_RESERVATION ||
		    prop == ZFS_PROP_REFQUOTA ||
		    prop == ZFS_PROP_REFRESERVATION);
		if (isspacelimit && zhp->zfs_type == ZFS_TYPE_SNAPSHOT)
			continue;

		const char *source;
		if (nvlist_lookup_string(propnv, ZPROP_SOURCE, &source) == 0) {
			if (strcmp(source, zhp->zfs_name) != 0 &&
			    strcmp(source, ZPROP_SOURCE_VAL_RECVD) != 0)
				continue;
		} else {
			/*
			 * May have no source before SPA_VERSION_RECVD_PROPS,
			 * but is still modifiable.
			 */
			if (!isspacelimit)
				continue;
		}

		if (zfs_prop_user(propname) ||
		    zfs_prop_get_type(prop) == PROP_TYPE_STRING) {
			const char *value;
			value = fnvlist_lookup_string(propnv, ZPROP_VALUE);
			fnvlist_add_string(nv, propname, value);
		} else {
			uint64_t value;
			value = fnvlist_lookup_uint64(propnv, ZPROP_VALUE);
			fnvlist_add_uint64(nv, propname, value);
		}
	}
}

/*
 * returns snapshot guid
 * and returns 0 if the snapshot does not exist
 */
static uint64_t
get_snap_guid(libzfs_handle_t *hdl, const char *fs, const char *snap)
{
	char name[MAXPATHLEN + 1];
	uint64_t guid = 0;

	if (fs == NULL || fs[0] == '\0' || snap == NULL || snap[0] == '\0')
		return (guid);

	(void) snprintf(name, sizeof (name), "%s@%s", fs, snap);
	zfs_handle_t *zhp = zfs_open(hdl, name, ZFS_TYPE_SNAPSHOT);
	if (zhp != NULL) {
		guid = zfs_prop_get_int(zhp, ZFS_PROP_GUID);
		zfs_close(zhp);
	}

	return (guid);
}

/*
 * returns snapshot creation txg
 * and returns 0 if the snapshot does not exist
 */
static uint64_t
get_snap_txg(libzfs_handle_t *hdl, const char *fs, const char *snap)
{
	char name[ZFS_MAX_DATASET_NAME_LEN];
	uint64_t txg = 0;

	if (fs == NULL || fs[0] == '\0' || snap == NULL || snap[0] == '\0')
		return (txg);

	(void) snprintf(name, sizeof (name), "%s@%s", fs, snap);
	if (zfs_dataset_exists(hdl, name, ZFS_TYPE_SNAPSHOT)) {
		zfs_handle_t *zhp = zfs_open(hdl, name, ZFS_TYPE_SNAPSHOT);
		if (zhp != NULL) {
			txg = zfs_prop_get_int(zhp, ZFS_PROP_CREATETXG);
			zfs_close(zhp);
		}
	}

	return (txg);
}

/*
 * Recursively generate nvlists describing datasets.  See comment
 * for the data structure send_data_t above for description of contents
 * of the nvlist.
 */
static int
send_iterate_fs(zfs_handle_t *zhp, void *arg)
{
	send_data_t *sd = arg;
	nvlist_t *nvfs = NULL, *nv = NULL;
	int rv = 0;
	uint64_t min_txg = 0, max_txg = 0;
	uint64_t txg = zhp->zfs_dmustats.dds_creation_txg;
	uint64_t guid = zhp->zfs_dmustats.dds_guid;
	uint64_t fromsnap_txg, tosnap_txg;
	char guidstring[64];

	/* These fields are restored on return from a recursive call. */
	uint64_t parent_fromsnap_guid_save = sd->parent_fromsnap_guid;
	uint64_t fromsnap_txg_save = sd->fromsnap_txg;
	uint64_t tosnap_txg_save = sd->tosnap_txg;

	fromsnap_txg = get_snap_txg(zhp->zfs_hdl, zhp->zfs_name, sd->fromsnap);
	if (fromsnap_txg != 0)
		sd->fromsnap_txg = fromsnap_txg;

	tosnap_txg = get_snap_txg(zhp->zfs_hdl, zhp->zfs_name, sd->tosnap);
	if (tosnap_txg != 0)
		sd->tosnap_txg = tosnap_txg;

	/*
	 * On the send side, if the current dataset does not have tosnap,
	 * perform two additional checks:
	 *
	 * - Skip sending the current dataset if it was created later than
	 *   the parent tosnap.
	 * - Return error if the current dataset was created earlier than
	 *   the parent tosnap, unless --skip-missing specified. Then
	 *   just print a warning.
	 */
	if (sd->tosnap != NULL && tosnap_txg == 0) {
		if (sd->tosnap_txg != 0 && txg > sd->tosnap_txg) {
			if (sd->verbose) {
				(void) fprintf(stderr, dgettext(TEXT_DOMAIN,
				    "skipping dataset %s: snapshot %s does "
				    "not exist\n"), zhp->zfs_name, sd->tosnap);
			}
		} else if (sd->skipmissing) {
			(void) fprintf(stderr, dgettext(TEXT_DOMAIN,
			    "WARNING: skipping dataset %s and its children:"
			    " snapshot %s does not exist\n"),
			    zhp->zfs_name, sd->tosnap);
		} else {
			(void) fprintf(stderr, dgettext(TEXT_DOMAIN,
			    "cannot send %s@%s%s: snapshot %s@%s does not "
			    "exist\n"), sd->fsname, sd->tosnap, sd->recursive ?
			    dgettext(TEXT_DOMAIN, " recursively") : "",
			    zhp->zfs_name, sd->tosnap);
			rv = EZFS_NOENT;
		}
		goto out;
	}

	nvfs = fnvlist_alloc();
	fnvlist_add_string(nvfs, "name", zhp->zfs_name);
	fnvlist_add_uint64(nvfs, "parentfromsnap", sd->parent_fromsnap_guid);

	if (zhp->zfs_dmustats.dds_origin[0] != '\0') {
		zfs_handle_t *origin = zfs_open(zhp->zfs_hdl,
		    zhp->zfs_dmustats.dds_origin, ZFS_TYPE_SNAPSHOT);
		if (origin == NULL) {
			rv = -1;
			goto out;
		}
		fnvlist_add_uint64(nvfs, "origin",
		    origin->zfs_dmustats.dds_guid);
		zfs_close(origin);
	}

	/* Iterate over props. */
	if (sd->props || sd->backup || sd->recursive) {
		nv = fnvlist_alloc();
		send_iterate_prop(zhp, sd->backup, nv);
		fnvlist_add_nvlist(nvfs, "props", nv);
	}
	if (zfs_prop_get_int(zhp, ZFS_PROP_ENCRYPTION) != ZIO_CRYPT_OFF) {
		boolean_t encroot;

		/* Determine if this dataset is an encryption root. */
		if (zfs_crypto_get_encryption_root(zhp, &encroot, NULL) != 0) {
			rv = -1;
			goto out;
		}

		if (encroot)
			fnvlist_add_boolean(nvfs, "is_encroot");

		/*
		 * Encrypted datasets can only be sent with properties if
		 * the raw flag is specified because the receive side doesn't
		 * currently have a mechanism for recursively asking the user
		 * for new encryption parameters.
		 */
		if (!sd->raw) {
			(void) fprintf(stderr, dgettext(TEXT_DOMAIN,
			    "cannot send %s@%s: encrypted dataset %s may not "
			    "be sent with properties without the raw flag\n"),
			    sd->fsname, sd->tosnap, zhp->zfs_name);
			rv = -1;
			goto out;
		}

	}

	/*
	 * Iterate over snaps, and set sd->parent_fromsnap_guid.
	 *
	 * If this is a "doall" send, a replicate send or we're just trying
	 * to gather a list of previous snapshots, iterate through all the
	 * snaps in the txg range. Otherwise just look at the one we're
	 * interested in.
	 */
	sd->parent_fromsnap_guid = 0;
	sd->parent_snaps = fnvlist_alloc();
	sd->snapprops = fnvlist_alloc();
	if (sd->holds)
		sd->snapholds = fnvlist_alloc();
	if (sd->doall || sd->replicate || sd->tosnap == NULL) {
		if (!sd->replicate && fromsnap_txg != 0)
			min_txg = fromsnap_txg;
		if (!sd->replicate && tosnap_txg != 0)
			max_txg = tosnap_txg;
		(void) zfs_iter_snapshots_sorted_v2(zhp, 0, send_iterate_snap,
		    sd, min_txg, max_txg);
	} else {
		char snapname[MAXPATHLEN] = { 0 };
		zfs_handle_t *snap;

		(void) snprintf(snapname, sizeof (snapname), "%s@%s",
		    zhp->zfs_name, sd->tosnap);
		if (sd->fromsnap != NULL)
			sd->seenfrom = B_TRUE;
		snap = zfs_open(zhp->zfs_hdl, snapname, ZFS_TYPE_SNAPSHOT);
		if (snap != NULL)
			(void) send_iterate_snap(snap, sd);
	}

	fnvlist_add_nvlist(nvfs, "snaps", sd->parent_snaps);
	fnvlist_free(sd->parent_snaps);
	fnvlist_add_nvlist(nvfs, "snapprops", sd->snapprops);
	fnvlist_free(sd->snapprops);
	if (sd->holds) {
		fnvlist_add_nvlist(nvfs, "snapholds", sd->snapholds);
		fnvlist_free(sd->snapholds);
	}

	/* Do not allow the size of the properties list to exceed the limit */
	if ((fnvlist_size(nvfs) + fnvlist_size(sd->fss)) >
	    zhp->zfs_hdl->libzfs_max_nvlist) {
		(void) fprintf(stderr, dgettext(TEXT_DOMAIN,
		    "warning: cannot send %s@%s: the size of the list of "
		    "snapshots and properties is too large to be received "
		    "successfully.\n"
		    "Select a smaller number of snapshots to send.\n"),
		    zhp->zfs_name, sd->tosnap);
		rv = EZFS_NOSPC;
		goto out;
	}
	/* Add this fs to nvlist. */
	(void) snprintf(guidstring, sizeof (guidstring),
	    "0x%llx", (longlong_t)guid);
	fnvlist_add_nvlist(sd->fss, guidstring, nvfs);

	/* Iterate over children. */
	if (sd->recursive)
		rv = zfs_iter_filesystems_v2(zhp, 0, send_iterate_fs, sd);

out:
	/* Restore saved fields. */
	sd->parent_fromsnap_guid = parent_fromsnap_guid_save;
	sd->fromsnap_txg = fromsnap_txg_save;
	sd->tosnap_txg = tosnap_txg_save;

	fnvlist_free(nv);
	fnvlist_free(nvfs);

	zfs_close(zhp);
	return (rv);
}

static int
gather_nvlist(libzfs_handle_t *hdl, const char *fsname, const char *fromsnap,
    const char *tosnap, boolean_t recursive, boolean_t raw, boolean_t doall,
    boolean_t replicate, boolean_t skipmissing, boolean_t verbose,
    boolean_t backup, boolean_t holds, boolean_t props, nvlist_t **nvlp,
    avl_tree_t **avlp)
{
	zfs_handle_t *zhp;
	send_data_t sd = { 0 };
	int error;

	zhp = zfs_open(hdl, fsname, ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME);
	if (zhp == NULL)
		return (EZFS_BADTYPE);

	sd.fss = fnvlist_alloc();
	sd.fsname = fsname;
	sd.fromsnap = fromsnap;
	sd.tosnap = tosnap;
	sd.recursive = recursive;
	sd.raw = raw;
	sd.doall = doall;
	sd.replicate = replicate;
	sd.skipmissing = skipmissing;
	sd.verbose = verbose;
	sd.backup = backup;
	sd.holds = holds;
	sd.props = props;

	if ((error = send_iterate_fs(zhp, &sd)) != 0) {
		fnvlist_free(sd.fss);
		if (avlp != NULL)
			*avlp = NULL;
		*nvlp = NULL;
		return (error);
	}

	if (avlp != NULL && (*avlp = fsavl_create(sd.fss)) == NULL) {
		fnvlist_free(sd.fss);
		*nvlp = NULL;
		return (EZFS_NOMEM);
	}

	*nvlp = sd.fss;
	return (0);
}

/*
 * Routines specific to "zfs send"
 */
typedef struct send_dump_data {
	/* these are all just the short snapname (the part after the @) */
	const char *fromsnap;
	const char *tosnap;
	char prevsnap[ZFS_MAX_DATASET_NAME_LEN];
	uint64_t prevsnap_obj;
	boolean_t seenfrom, seento, replicate, doall, fromorigin;
	boolean_t dryrun, parsable, progress, embed_data, std_out;
	boolean_t large_block, compress, raw, holds;
	boolean_t progressastitle;
	int outfd;
	boolean_t err;
	nvlist_t *fss;
	nvlist_t *snapholds;
	avl_tree_t *fsavl;
	snapfilter_cb_t *filter_cb;
	void *filter_cb_arg;
	nvlist_t *debugnv;
	char holdtag[ZFS_MAX_DATASET_NAME_LEN];
	int cleanup_fd;
	int verbosity;
	uint64_t size;
} send_dump_data_t;

static int
zfs_send_space(zfs_handle_t *zhp, const char *snapname, const char *from,
    enum lzc_send_flags flags, uint64_t *spacep)
{
	assert(snapname != NULL);

	int error = lzc_send_space(snapname, from, flags, spacep);
	if (error == 0)
		return (0);

	char errbuf[ERRBUFLEN];
	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "warning: cannot estimate space for '%s'"), snapname);

	libzfs_handle_t *hdl = zhp->zfs_hdl;
	switch (error) {
	case EXDEV:
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "not an earlier snapshot from the same fs"));
		return (zfs_error(hdl, EZFS_CROSSTARGET, errbuf));

	case ENOENT:
		if (zfs_dataset_exists(hdl, snapname,
		    ZFS_TYPE_SNAPSHOT)) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "incremental source (%s) does not exist"),
			    snapname);
		}
		return (zfs_error(hdl, EZFS_NOENT, errbuf));

	case EDQUOT:
	case EFBIG:
	case EIO:
	case ENOLINK:
	case ENOSPC:
	case ENOSTR:
	case ENXIO:
	case EPIPE:
	case ERANGE:
	case EFAULT:
	case EROFS:
	case EINVAL:
		zfs_error_aux(hdl, "%s", zfs_strerror(error));
		return (zfs_error(hdl, EZFS_BADBACKUP, errbuf));

	default:
		return (zfs_standard_error(hdl, error, errbuf));
	}
}

/*
 * Dumps a backup of the given snapshot (incremental from fromsnap if it's not
 * NULL) to the file descriptor specified by outfd.
 */
static int
dump_ioctl(zfs_handle_t *zhp, const char *fromsnap, uint64_t fromsnap_obj,
    boolean_t fromorigin, int outfd, enum lzc_send_flags flags,
    nvlist_t *debugnv)
{
	zfs_cmd_t zc = {"\0"};
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	nvlist_t *thisdbg;

	assert(zhp->zfs_type == ZFS_TYPE_SNAPSHOT);
	assert(fromsnap_obj == 0 || !fromorigin);

	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));
	zc.zc_cookie = outfd;
	zc.zc_obj = fromorigin;
	zc.zc_sendobj = zfs_prop_get_int(zhp, ZFS_PROP_OBJSETID);
	zc.zc_fromobj = fromsnap_obj;
	zc.zc_flags = flags;

	if (debugnv != NULL) {
		thisdbg = fnvlist_alloc();
		if (fromsnap != NULL && fromsnap[0] != '\0')
			fnvlist_add_string(thisdbg, "fromsnap", fromsnap);
	}

	if (zfs_ioctl(zhp->zfs_hdl, ZFS_IOC_SEND, &zc) != 0) {
		char errbuf[ERRBUFLEN];
		int error = errno;

		(void) snprintf(errbuf, sizeof (errbuf), "%s '%s'",
		    dgettext(TEXT_DOMAIN, "warning: cannot send"),
		    zhp->zfs_name);

		if (debugnv != NULL) {
			fnvlist_add_uint64(thisdbg, "error", error);
			fnvlist_add_nvlist(debugnv, zhp->zfs_name, thisdbg);
			fnvlist_free(thisdbg);
		}

		switch (error) {
		case EXDEV:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "not an earlier snapshot from the same fs"));
			return (zfs_error(hdl, EZFS_CROSSTARGET, errbuf));

		case EACCES:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "source key must be loaded"));
			return (zfs_error(hdl, EZFS_CRYPTOFAILED, errbuf));

		case ENOENT:
			if (zfs_dataset_exists(hdl, zc.zc_name,
			    ZFS_TYPE_SNAPSHOT)) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "incremental source (@%s) does not exist"),
				    zc.zc_value);
			}
			return (zfs_error(hdl, EZFS_NOENT, errbuf));

		case EDQUOT:
		case EFBIG:
		case EIO:
		case ENOLINK:
		case ENOSPC:
		case ENOSTR:
		case ENXIO:
		case EPIPE:
		case ERANGE:
		case EFAULT:
		case EROFS:
		case EINVAL:
			zfs_error_aux(hdl, "%s", zfs_strerror(errno));
			return (zfs_error(hdl, EZFS_BADBACKUP, errbuf));

		default:
			return (zfs_standard_error(hdl, errno, errbuf));
		}
	}

	if (debugnv != NULL) {
		fnvlist_add_nvlist(debugnv, zhp->zfs_name, thisdbg);
		fnvlist_free(thisdbg);
	}

	return (0);
}

static void
gather_holds(zfs_handle_t *zhp, send_dump_data_t *sdd)
{
	assert(zhp->zfs_type == ZFS_TYPE_SNAPSHOT);

	/*
	 * zfs_send() only sets snapholds for sends that need them,
	 * e.g. replication and doall.
	 */
	if (sdd->snapholds == NULL)
		return;

	fnvlist_add_string(sdd->snapholds, zhp->zfs_name, sdd->holdtag);
}

int
zfs_send_progress(zfs_handle_t *zhp, int fd, uint64_t *bytes_written,
    uint64_t *blocks_visited)
{
	zfs_cmd_t zc = {"\0"};

	if (bytes_written != NULL)
		*bytes_written = 0;
	if (blocks_visited != NULL)
		*blocks_visited = 0;
	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));
	zc.zc_cookie = fd;
	if (zfs_ioctl(zhp->zfs_hdl, ZFS_IOC_SEND_PROGRESS, &zc) != 0)
		return (errno);
	if (bytes_written != NULL)
		*bytes_written = zc.zc_cookie;
	if (blocks_visited != NULL)
		*blocks_visited = zc.zc_objset_type;
	return (0);
}

static volatile boolean_t send_progress_thread_signal_duetotimer;
static void
send_progress_thread_act(int sig, siginfo_t *info, void *ucontext)
{
	(void) sig, (void) ucontext;
	send_progress_thread_signal_duetotimer = info->si_code == SI_TIMER;
}

struct timer_desirability {
	timer_t timer;
	boolean_t desired;
};
static void
timer_delete_cleanup(void *timer)
{
	struct timer_desirability *td = timer;
	if (td->desired)
		timer_delete(td->timer);
}

#ifdef SIGINFO
#define	SEND_PROGRESS_THREAD_PARENT_BLOCK_SIGINFO sigaddset(&new, SIGINFO)
#else
#define	SEND_PROGRESS_THREAD_PARENT_BLOCK_SIGINFO
#endif
#define	SEND_PROGRESS_THREAD_PARENT_BLOCK(old) { \
	sigset_t new; \
	sigemptyset(&new); \
	sigaddset(&new, SIGUSR1); \
	SEND_PROGRESS_THREAD_PARENT_BLOCK_SIGINFO; \
	pthread_sigmask(SIG_BLOCK, &new, old); \
}

static void *
send_progress_thread(void *arg)
{
	progress_arg_t *pa = arg;
	zfs_handle_t *zhp = pa->pa_zhp;
	uint64_t bytes;
	uint64_t blocks;
	uint64_t total = pa->pa_size / 100;
	char buf[16];
	time_t t;
	struct tm tm;
	int err;

	const struct sigaction signal_action =
	    {.sa_sigaction = send_progress_thread_act, .sa_flags = SA_SIGINFO};
	struct sigevent timer_cfg =
	    {.sigev_notify = SIGEV_SIGNAL, .sigev_signo = SIGUSR1};
	const struct itimerspec timer_time =
	    {.it_value = {.tv_sec = 1}, .it_interval = {.tv_sec = 1}};
	struct timer_desirability timer = {};

	sigaction(SIGUSR1, &signal_action, NULL);
#ifdef SIGINFO
	sigaction(SIGINFO, &signal_action, NULL);
#endif

	if ((timer.desired = pa->pa_progress || pa->pa_astitle)) {
		if (timer_create(CLOCK_MONOTONIC, &timer_cfg, &timer.timer))
			return ((void *)(uintptr_t)errno);
		(void) timer_settime(timer.timer, 0, &timer_time, NULL);
	}
	pthread_cleanup_push(timer_delete_cleanup, &timer);

	if (!pa->pa_parsable && pa->pa_progress) {
		(void) fprintf(stderr,
		    "TIME       %s   %sSNAPSHOT %s\n",
		    pa->pa_estimate ? "BYTES" : " SENT",
		    pa->pa_verbosity >= 2 ? "   BLOCKS    " : "",
		    zhp->zfs_name);
	}

	/*
	 * Print the progress from ZFS_IOC_SEND_PROGRESS every second.
	 */
	for (;;) {
		pause();
		if ((err = zfs_send_progress(zhp, pa->pa_fd, &bytes,
		    &blocks)) != 0) {
			if (err == EINTR || err == ENOENT)
				err = 0;
			pthread_exit(((void *)(uintptr_t)err));
		}

		(void) time(&t);
		localtime_r(&t, &tm);

		if (pa->pa_astitle) {
			char buf_bytes[16];
			char buf_size[16];
			int pct;
			zfs_nicenum(bytes, buf_bytes, sizeof (buf_bytes));
			zfs_nicenum(pa->pa_size, buf_size, sizeof (buf_size));
			pct = (total > 0) ? bytes / total : 100;
			zfs_setproctitle("sending %s (%d%%: %s/%s)",
			    zhp->zfs_name, MIN(pct, 100), buf_bytes, buf_size);
		}

		if (pa->pa_verbosity >= 2 && pa->pa_parsable) {
			(void) fprintf(stderr,
			    "%02d:%02d:%02d\t%llu\t%llu\t%s\n",
			    tm.tm_hour, tm.tm_min, tm.tm_sec,
			    (u_longlong_t)bytes, (u_longlong_t)blocks,
			    zhp->zfs_name);
		} else if (pa->pa_verbosity >= 2) {
			zfs_nicenum(bytes, buf, sizeof (buf));
			(void) fprintf(stderr,
			    "%02d:%02d:%02d   %5s    %8llu    %s\n",
			    tm.tm_hour, tm.tm_min, tm.tm_sec,
			    buf, (u_longlong_t)blocks, zhp->zfs_name);
		} else if (pa->pa_parsable) {
			(void) fprintf(stderr, "%02d:%02d:%02d\t%llu\t%s\n",
			    tm.tm_hour, tm.tm_min, tm.tm_sec,
			    (u_longlong_t)bytes, zhp->zfs_name);
		} else if (pa->pa_progress ||
		    !send_progress_thread_signal_duetotimer) {
			zfs_nicebytes(bytes, buf, sizeof (buf));
			(void) fprintf(stderr, "%02d:%02d:%02d   %5s   %s\n",
			    tm.tm_hour, tm.tm_min, tm.tm_sec,
			    buf, zhp->zfs_name);
		}
	}
	pthread_cleanup_pop(B_TRUE);
	return (NULL);
}

static boolean_t
send_progress_thread_exit(
    libzfs_handle_t *hdl, pthread_t ptid, sigset_t *oldmask)
{
	void *status = NULL;
	(void) pthread_cancel(ptid);
	(void) pthread_join(ptid, &status);
	pthread_sigmask(SIG_SETMASK, oldmask, NULL);
	int error = (int)(uintptr_t)status;
	if (error != 0 && status != PTHREAD_CANCELED)
		return (zfs_standard_error(hdl, error,
		    dgettext(TEXT_DOMAIN, "progress thread exited nonzero")));
	else
		return (B_FALSE);
}

static void
send_print_verbose(FILE *fout, const char *tosnap, const char *fromsnap,
    uint64_t size, boolean_t parsable)
{
	if (parsable) {
		if (fromsnap != NULL) {
			(void) fprintf(fout, dgettext(TEXT_DOMAIN,
			    "incremental\t%s\t%s"), fromsnap, tosnap);
		} else {
/*
 * Workaround for GCC 12+ with UBSan enabled deficencies.
 *
 * GCC 12+ invoked with -fsanitize=undefined incorrectly reports the code
 * below as violating -Wformat-overflow.
 */
#if defined(__GNUC__) && !defined(__clang__) && \
	defined(ZFS_UBSAN_ENABLED) && defined(HAVE_FORMAT_OVERFLOW)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-overflow"
#endif
			(void) fprintf(fout, dgettext(TEXT_DOMAIN,
			    "full\t%s"), tosnap);
#if defined(__GNUC__) && !defined(__clang__) && \
	defined(ZFS_UBSAN_ENABLED) && defined(HAVE_FORMAT_OVERFLOW)
#pragma GCC diagnostic pop
#endif
		}
		(void) fprintf(fout, "\t%llu", (longlong_t)size);
	} else {
		if (fromsnap != NULL) {
			if (strchr(fromsnap, '@') == NULL &&
			    strchr(fromsnap, '#') == NULL) {
				(void) fprintf(fout, dgettext(TEXT_DOMAIN,
				    "send from @%s to %s"), fromsnap, tosnap);
			} else {
				(void) fprintf(fout, dgettext(TEXT_DOMAIN,
				    "send from %s to %s"), fromsnap, tosnap);
			}
		} else {
			(void) fprintf(fout, dgettext(TEXT_DOMAIN,
			    "full send of %s"), tosnap);
		}
		if (size != 0) {
			char buf[16];
			zfs_nicebytes(size, buf, sizeof (buf));
/*
 * Workaround for GCC 12+ with UBSan enabled deficencies.
 *
 * GCC 12+ invoked with -fsanitize=undefined incorrectly reports the code
 * below as violating -Wformat-overflow.
 */
#if defined(__GNUC__) && !defined(__clang__) && \
	defined(ZFS_UBSAN_ENABLED) && defined(HAVE_FORMAT_OVERFLOW)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-overflow"
#endif
			(void) fprintf(fout, dgettext(TEXT_DOMAIN,
			    " estimated size is %s"), buf);
#if defined(__GNUC__) && !defined(__clang__) && \
	defined(ZFS_UBSAN_ENABLED) && defined(HAVE_FORMAT_OVERFLOW)
#pragma GCC diagnostic pop
#endif
		}
	}
	(void) fprintf(fout, "\n");
}

/*
 * Send a single filesystem snapshot, updating the send dump data.
 * This interface is intended for use as a zfs_iter_snapshots_v2_sorted visitor.
 */
static int
dump_snapshot(zfs_handle_t *zhp, void *arg)
{
	send_dump_data_t *sdd = arg;
	progress_arg_t pa = { 0 };
	pthread_t tid;
	char *thissnap;
	enum lzc_send_flags flags = 0;
	int err;
	boolean_t isfromsnap, istosnap, fromorigin;
	boolean_t exclude = B_FALSE;
	FILE *fout = sdd->std_out ? stdout : stderr;

	err = 0;
	thissnap = strchr(zhp->zfs_name, '@') + 1;
	isfromsnap = (sdd->fromsnap != NULL &&
	    strcmp(sdd->fromsnap, thissnap) == 0);

	if (!sdd->seenfrom && isfromsnap) {
		gather_holds(zhp, sdd);
		sdd->seenfrom = B_TRUE;
		(void) strlcpy(sdd->prevsnap, thissnap, sizeof (sdd->prevsnap));
		sdd->prevsnap_obj = zfs_prop_get_int(zhp, ZFS_PROP_OBJSETID);
		zfs_close(zhp);
		return (0);
	}

	if (sdd->seento || !sdd->seenfrom) {
		zfs_close(zhp);
		return (0);
	}

	istosnap = (strcmp(sdd->tosnap, thissnap) == 0);
	if (istosnap)
		sdd->seento = B_TRUE;

	if (sdd->large_block)
		flags |= LZC_SEND_FLAG_LARGE_BLOCK;
	if (sdd->embed_data)
		flags |= LZC_SEND_FLAG_EMBED_DATA;
	if (sdd->compress)
		flags |= LZC_SEND_FLAG_COMPRESS;
	if (sdd->raw)
		flags |= LZC_SEND_FLAG_RAW;

	if (!sdd->doall && !isfromsnap && !istosnap) {
		if (sdd->replicate) {
			const char *snapname;
			nvlist_t *snapprops;
			/*
			 * Filter out all intermediate snapshots except origin
			 * snapshots needed to replicate clones.
			 */
			nvlist_t *nvfs = fsavl_find(sdd->fsavl,
			    zhp->zfs_dmustats.dds_guid, &snapname);

			if (nvfs != NULL) {
				snapprops = fnvlist_lookup_nvlist(nvfs,
				    "snapprops");
				snapprops = fnvlist_lookup_nvlist(snapprops,
				    thissnap);
				exclude = !nvlist_exists(snapprops,
				    "is_clone_origin");
			}
		} else {
			exclude = B_TRUE;
		}
	}

	/*
	 * If a filter function exists, call it to determine whether
	 * this snapshot will be sent.
	 */
	if (exclude || (sdd->filter_cb != NULL &&
	    sdd->filter_cb(zhp, sdd->filter_cb_arg) == B_FALSE)) {
		/*
		 * This snapshot is filtered out.  Don't send it, and don't
		 * set prevsnap_obj, so it will be as if this snapshot didn't
		 * exist, and the next accepted snapshot will be sent as
		 * an incremental from the last accepted one, or as the
		 * first (and full) snapshot in the case of a replication,
		 * non-incremental send.
		 */
		zfs_close(zhp);
		return (0);
	}

	gather_holds(zhp, sdd);
	fromorigin = sdd->prevsnap[0] == '\0' &&
	    (sdd->fromorigin || sdd->replicate);

	if (sdd->verbosity != 0) {
		uint64_t size = 0;
		char fromds[ZFS_MAX_DATASET_NAME_LEN];

		if (sdd->prevsnap[0] != '\0') {
			(void) strlcpy(fromds, zhp->zfs_name, sizeof (fromds));
			*(strchr(fromds, '@') + 1) = '\0';
			(void) strlcat(fromds, sdd->prevsnap, sizeof (fromds));
		}
		if (zfs_send_space(zhp, zhp->zfs_name,
		    sdd->prevsnap[0] ? fromds : NULL, flags, &size) == 0) {
			send_print_verbose(fout, zhp->zfs_name,
			    sdd->prevsnap[0] ? sdd->prevsnap : NULL,
			    size, sdd->parsable);
			sdd->size += size;
		}
	}

	if (!sdd->dryrun) {
		/*
		 * If progress reporting is requested, spawn a new thread to
		 * poll ZFS_IOC_SEND_PROGRESS at a regular interval.
		 */
		sigset_t oldmask;
		{
			pa.pa_zhp = zhp;
			pa.pa_fd = sdd->outfd;
			pa.pa_parsable = sdd->parsable;
			pa.pa_estimate = B_FALSE;
			pa.pa_verbosity = sdd->verbosity;
			pa.pa_size = sdd->size;
			pa.pa_astitle = sdd->progressastitle;
			pa.pa_progress = sdd->progress;

			if ((err = pthread_create(&tid, NULL,
			    send_progress_thread, &pa)) != 0) {
				zfs_close(zhp);
				return (err);
			}
			SEND_PROGRESS_THREAD_PARENT_BLOCK(&oldmask);
		}

		err = dump_ioctl(zhp, sdd->prevsnap, sdd->prevsnap_obj,
		    fromorigin, sdd->outfd, flags, sdd->debugnv);

		if (send_progress_thread_exit(zhp->zfs_hdl, tid, &oldmask))
			return (-1);
	}

	(void) strlcpy(sdd->prevsnap, thissnap, sizeof (sdd->prevsnap));
	sdd->prevsnap_obj = zfs_prop_get_int(zhp, ZFS_PROP_OBJSETID);
	zfs_close(zhp);
	return (err);
}

/*
 * Send all snapshots for a filesystem, updating the send dump data.
 */
static int
dump_filesystem(zfs_handle_t *zhp, send_dump_data_t *sdd)
{
	int rv = 0;
	boolean_t missingfrom = B_FALSE;
	zfs_cmd_t zc = {"\0"};
	uint64_t min_txg = 0, max_txg = 0;

	/*
	 * Make sure the tosnap exists.
	 */
	(void) snprintf(zc.zc_name, sizeof (zc.zc_name), "%s@%s",
	    zhp->zfs_name, sdd->tosnap);
	if (zfs_ioctl(zhp->zfs_hdl, ZFS_IOC_OBJSET_STATS, &zc) != 0) {
		(void) fprintf(stderr, dgettext(TEXT_DOMAIN,
		    "WARNING: could not send %s@%s: does not exist\n"),
		    zhp->zfs_name, sdd->tosnap);
		sdd->err = B_TRUE;
		return (0);
	}

	/*
	 * If this fs does not have fromsnap, and we're doing
	 * recursive, we need to send a full stream from the
	 * beginning (or an incremental from the origin if this
	 * is a clone).  If we're doing non-recursive, then let
	 * them get the error.
	 */
	if (sdd->replicate && sdd->fromsnap) {
		/*
		 * Make sure the fromsnap exists.
		 */
		(void) snprintf(zc.zc_name, sizeof (zc.zc_name), "%s@%s",
		    zhp->zfs_name, sdd->fromsnap);
		if (zfs_ioctl(zhp->zfs_hdl, ZFS_IOC_OBJSET_STATS, &zc) != 0)
			missingfrom = B_TRUE;
	}

	sdd->seenfrom = sdd->seento = B_FALSE;
	sdd->prevsnap[0] = '\0';
	sdd->prevsnap_obj = 0;
	if (sdd->fromsnap == NULL || missingfrom)
		sdd->seenfrom = B_TRUE;

	/*
	 * Iterate through all snapshots and process the ones we will be
	 * sending. If we only have a "from" and "to" snapshot to deal
	 * with, we can avoid iterating through all the other snapshots.
	 */
	if (sdd->doall || sdd->replicate || sdd->tosnap == NULL) {
		if (!sdd->replicate) {
			if (sdd->fromsnap != NULL) {
				min_txg = get_snap_txg(zhp->zfs_hdl,
				    zhp->zfs_name, sdd->fromsnap);
			}
			if (sdd->tosnap != NULL) {
				max_txg = get_snap_txg(zhp->zfs_hdl,
				    zhp->zfs_name, sdd->tosnap);
			}
		}
		rv = zfs_iter_snapshots_sorted_v2(zhp, 0, dump_snapshot, sdd,
		    min_txg, max_txg);
	} else {
		char snapname[MAXPATHLEN] = { 0 };
		zfs_handle_t *snap;

		/* Dump fromsnap. */
		if (!sdd->seenfrom) {
			(void) snprintf(snapname, sizeof (snapname),
			    "%s@%s", zhp->zfs_name, sdd->fromsnap);
			snap = zfs_open(zhp->zfs_hdl, snapname,
			    ZFS_TYPE_SNAPSHOT);
			if (snap != NULL)
				rv = dump_snapshot(snap, sdd);
			else
				rv = errno;
		}

		/* Dump tosnap. */
		if (rv == 0) {
			(void) snprintf(snapname, sizeof (snapname),
			    "%s@%s", zhp->zfs_name, sdd->tosnap);
			snap = zfs_open(zhp->zfs_hdl, snapname,
			    ZFS_TYPE_SNAPSHOT);
			if (snap != NULL)
				rv = dump_snapshot(snap, sdd);
			else
				rv = errno;
		}
	}

	if (!sdd->seenfrom) {
		(void) fprintf(stderr, dgettext(TEXT_DOMAIN,
		    "WARNING: could not send %s@%s:\n"
		    "incremental source (%s@%s) does not exist\n"),
		    zhp->zfs_name, sdd->tosnap,
		    zhp->zfs_name, sdd->fromsnap);
		sdd->err = B_TRUE;
	} else if (!sdd->seento) {
		if (sdd->fromsnap) {
			(void) fprintf(stderr, dgettext(TEXT_DOMAIN,
			    "WARNING: could not send %s@%s:\n"
			    "incremental source (%s@%s) "
			    "is not earlier than it\n"),
			    zhp->zfs_name, sdd->tosnap,
			    zhp->zfs_name, sdd->fromsnap);
		} else {
			(void) fprintf(stderr, dgettext(TEXT_DOMAIN,
			    "WARNING: "
			    "could not send %s@%s: does not exist\n"),
			    zhp->zfs_name, sdd->tosnap);
		}
		sdd->err = B_TRUE;
	}

	return (rv);
}

/*
 * Send all snapshots for all filesystems in sdd.
 */
static int
dump_filesystems(zfs_handle_t *rzhp, send_dump_data_t *sdd)
{
	nvpair_t *fspair;
	boolean_t needagain, progress;

	if (!sdd->replicate)
		return (dump_filesystem(rzhp, sdd));

	/* Mark the clone origin snapshots. */
	for (fspair = nvlist_next_nvpair(sdd->fss, NULL); fspair;
	    fspair = nvlist_next_nvpair(sdd->fss, fspair)) {
		nvlist_t *nvfs;
		uint64_t origin_guid = 0;

		nvfs = fnvpair_value_nvlist(fspair);
		(void) nvlist_lookup_uint64(nvfs, "origin", &origin_guid);
		if (origin_guid != 0) {
			const char *snapname;
			nvlist_t *origin_nv = fsavl_find(sdd->fsavl,
			    origin_guid, &snapname);
			if (origin_nv != NULL) {
				nvlist_t *snapprops;
				snapprops = fnvlist_lookup_nvlist(origin_nv,
				    "snapprops");
				snapprops = fnvlist_lookup_nvlist(snapprops,
				    snapname);
				fnvlist_add_boolean(snapprops,
				    "is_clone_origin");
			}
		}
	}
again:
	needagain = progress = B_FALSE;
	for (fspair = nvlist_next_nvpair(sdd->fss, NULL); fspair;
	    fspair = nvlist_next_nvpair(sdd->fss, fspair)) {
		nvlist_t *fslist, *parent_nv;
		const char *fsname;
		zfs_handle_t *zhp;
		int err;
		uint64_t origin_guid = 0;
		uint64_t parent_guid = 0;

		fslist = fnvpair_value_nvlist(fspair);
		if (nvlist_lookup_boolean(fslist, "sent") == 0)
			continue;

		fsname = fnvlist_lookup_string(fslist, "name");
		(void) nvlist_lookup_uint64(fslist, "origin", &origin_guid);
		(void) nvlist_lookup_uint64(fslist, "parentfromsnap",
		    &parent_guid);

		if (parent_guid != 0) {
			parent_nv = fsavl_find(sdd->fsavl, parent_guid, NULL);
			if (!nvlist_exists(parent_nv, "sent")) {
				/* Parent has not been sent; skip this one. */
				needagain = B_TRUE;
				continue;
			}
		}

		if (origin_guid != 0) {
			nvlist_t *origin_nv = fsavl_find(sdd->fsavl,
			    origin_guid, NULL);
			if (origin_nv != NULL &&
			    !nvlist_exists(origin_nv, "sent")) {
				/*
				 * Origin has not been sent yet;
				 * skip this clone.
				 */
				needagain = B_TRUE;
				continue;
			}
		}

		zhp = zfs_open(rzhp->zfs_hdl, fsname, ZFS_TYPE_DATASET);
		if (zhp == NULL)
			return (-1);
		err = dump_filesystem(zhp, sdd);
		fnvlist_add_boolean(fslist, "sent");
		progress = B_TRUE;
		zfs_close(zhp);
		if (err)
			return (err);
	}
	if (needagain) {
		assert(progress);
		goto again;
	}

	/* Clean out the sent flags in case we reuse this fss. */
	for (fspair = nvlist_next_nvpair(sdd->fss, NULL); fspair;
	    fspair = nvlist_next_nvpair(sdd->fss, fspair)) {
		nvlist_t *fslist;

		fslist = fnvpair_value_nvlist(fspair);
		(void) nvlist_remove_all(fslist, "sent");
	}

	return (0);
}

nvlist_t *
zfs_send_resume_token_to_nvlist(libzfs_handle_t *hdl, const char *token)
{
	unsigned int version;
	int nread, i;
	unsigned long long checksum, packed_len;

	/*
	 * Decode token header, which is:
	 *   <token version>-<checksum of payload>-<uncompressed payload length>
	 * Note that the only supported token version is 1.
	 */
	nread = sscanf(token, "%u-%llx-%llx-",
	    &version, &checksum, &packed_len);
	if (nread != 3) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "resume token is corrupt (invalid format)"));
		return (NULL);
	}

	if (version != ZFS_SEND_RESUME_TOKEN_VERSION) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "resume token is corrupt (invalid version %u)"),
		    version);
		return (NULL);
	}

	/* Convert hexadecimal representation to binary. */
	token = strrchr(token, '-') + 1;
	int len = strlen(token) / 2;
	unsigned char *compressed = zfs_alloc(hdl, len);
	for (i = 0; i < len; i++) {
		nread = sscanf(token + i * 2, "%2hhx", compressed + i);
		if (nread != 1) {
			free(compressed);
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "resume token is corrupt "
			    "(payload is not hex-encoded)"));
			return (NULL);
		}
	}

	/* Verify checksum. */
	zio_cksum_t cksum;
	fletcher_4_native_varsize(compressed, len, &cksum);
	if (cksum.zc_word[0] != checksum) {
		free(compressed);
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "resume token is corrupt (incorrect checksum)"));
		return (NULL);
	}

	/* Uncompress. */
	void *packed = zfs_alloc(hdl, packed_len);
	uLongf packed_len_long = packed_len;
	if (uncompress(packed, &packed_len_long, compressed, len) != Z_OK ||
	    packed_len_long != packed_len) {
		free(packed);
		free(compressed);
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "resume token is corrupt (decompression failed)"));
		return (NULL);
	}

	/* Unpack nvlist. */
	nvlist_t *nv;
	int error = nvlist_unpack(packed, packed_len, &nv, KM_SLEEP);
	free(packed);
	free(compressed);
	if (error != 0) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "resume token is corrupt (nvlist_unpack failed)"));
		return (NULL);
	}
	return (nv);
}

static enum lzc_send_flags
lzc_flags_from_sendflags(const sendflags_t *flags)
{
	enum lzc_send_flags lzc_flags = 0;

	if (flags->largeblock)
		lzc_flags |= LZC_SEND_FLAG_LARGE_BLOCK;
	if (flags->embed_data)
		lzc_flags |= LZC_SEND_FLAG_EMBED_DATA;
	if (flags->compress)
		lzc_flags |= LZC_SEND_FLAG_COMPRESS;
	if (flags->raw)
		lzc_flags |= LZC_SEND_FLAG_RAW;
	if (flags->saved)
		lzc_flags |= LZC_SEND_FLAG_SAVED;

	return (lzc_flags);
}

static int
estimate_size(zfs_handle_t *zhp, const char *from, int fd, sendflags_t *flags,
    uint64_t resumeobj, uint64_t resumeoff, uint64_t bytes,
    const char *redactbook, char *errbuf, uint64_t *sizep)
{
	uint64_t size;
	FILE *fout = flags->dryrun ? stdout : stderr;
	progress_arg_t pa = { 0 };
	int err = 0;
	pthread_t ptid;
	sigset_t oldmask;

	{
		pa.pa_zhp = zhp;
		pa.pa_fd = fd;
		pa.pa_parsable = flags->parsable;
		pa.pa_estimate = B_TRUE;
		pa.pa_verbosity = flags->verbosity;

		err = pthread_create(&ptid, NULL,
		    send_progress_thread, &pa);
		if (err != 0) {
			zfs_error_aux(zhp->zfs_hdl, "%s", zfs_strerror(errno));
			return (zfs_error(zhp->zfs_hdl,
			    EZFS_THREADCREATEFAILED, errbuf));
		}
		SEND_PROGRESS_THREAD_PARENT_BLOCK(&oldmask);
	}

	err = lzc_send_space_resume_redacted(zhp->zfs_name, from,
	    lzc_flags_from_sendflags(flags), resumeobj, resumeoff, bytes,
	    redactbook, fd, &size);
	*sizep = size;

	if (send_progress_thread_exit(zhp->zfs_hdl, ptid, &oldmask))
		return (-1);

	if (!flags->progress && !flags->parsable)
		return (err);

	if (err != 0) {
		zfs_error_aux(zhp->zfs_hdl, "%s", zfs_strerror(err));
		return (zfs_error(zhp->zfs_hdl, EZFS_BADBACKUP,
		    errbuf));
	}
	send_print_verbose(fout, zhp->zfs_name, from, size,
	    flags->parsable);

	if (flags->parsable) {
		(void) fprintf(fout, "size\t%llu\n", (longlong_t)size);
	} else {
		char buf[16];
		zfs_nicenum(size, buf, sizeof (buf));
		(void) fprintf(fout, dgettext(TEXT_DOMAIN,
		    "total estimated size is %s\n"), buf);
	}
	return (0);
}

static boolean_t
redact_snaps_contains(const uint64_t *snaps, uint64_t num_snaps, uint64_t guid)
{
	for (int i = 0; i < num_snaps; i++) {
		if (snaps[i] == guid)
			return (B_TRUE);
	}
	return (B_FALSE);
}

static boolean_t
redact_snaps_equal(const uint64_t *snaps1, uint64_t num_snaps1,
    const uint64_t *snaps2, uint64_t num_snaps2)
{
	if (num_snaps1 != num_snaps2)
		return (B_FALSE);
	for (int i = 0; i < num_snaps1; i++) {
		if (!redact_snaps_contains(snaps2, num_snaps2, snaps1[i]))
			return (B_FALSE);
	}
	return (B_TRUE);
}

static int
get_bookmarks(const char *path, nvlist_t **bmarksp)
{
	nvlist_t *props = fnvlist_alloc();
	int error;

	fnvlist_add_boolean(props, "redact_complete");
	fnvlist_add_boolean(props, zfs_prop_to_name(ZFS_PROP_REDACT_SNAPS));
	error = lzc_get_bookmarks(path, props, bmarksp);
	fnvlist_free(props);
	return (error);
}

static nvpair_t *
find_redact_pair(nvlist_t *bmarks, const uint64_t *redact_snap_guids,
    int num_redact_snaps)
{
	nvpair_t *pair;

	for (pair = nvlist_next_nvpair(bmarks, NULL); pair;
	    pair = nvlist_next_nvpair(bmarks, pair)) {

		nvlist_t *bmark = fnvpair_value_nvlist(pair);
		nvlist_t *vallist = fnvlist_lookup_nvlist(bmark,
		    zfs_prop_to_name(ZFS_PROP_REDACT_SNAPS));
		uint_t len = 0;
		uint64_t *bmarksnaps = fnvlist_lookup_uint64_array(vallist,
		    ZPROP_VALUE, &len);
		if (redact_snaps_equal(redact_snap_guids,
		    num_redact_snaps, bmarksnaps, len)) {
			break;
		}
	}
	return (pair);
}

static boolean_t
get_redact_complete(nvpair_t *pair)
{
	nvlist_t *bmark = fnvpair_value_nvlist(pair);
	nvlist_t *vallist = fnvlist_lookup_nvlist(bmark, "redact_complete");
	boolean_t complete = fnvlist_lookup_boolean_value(vallist,
	    ZPROP_VALUE);

	return (complete);
}

/*
 * Check that the list of redaction snapshots in the bookmark matches the send
 * we're resuming, and return whether or not it's complete.
 *
 * Note that the caller needs to free the contents of *bookname with free() if
 * this function returns successfully.
 */
static int
find_redact_book(libzfs_handle_t *hdl, const char *path,
    const uint64_t *redact_snap_guids, int num_redact_snaps,
    char **bookname)
{
	char errbuf[ERRBUFLEN];
	nvlist_t *bmarks;

	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "cannot resume send"));

	int error = get_bookmarks(path, &bmarks);
	if (error != 0) {
		if (error == ESRCH) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "nonexistent redaction bookmark provided"));
		} else if (error == ENOENT) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "dataset to be sent no longer exists"));
		} else {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "unknown error: %s"), zfs_strerror(error));
		}
		return (zfs_error(hdl, EZFS_BADPROP, errbuf));
	}
	nvpair_t *pair = find_redact_pair(bmarks, redact_snap_guids,
	    num_redact_snaps);
	if (pair == NULL)  {
		fnvlist_free(bmarks);
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "no appropriate redaction bookmark exists"));
		return (zfs_error(hdl, EZFS_BADPROP, errbuf));
	}
	boolean_t complete = get_redact_complete(pair);
	if (!complete) {
		fnvlist_free(bmarks);
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "incomplete redaction bookmark provided"));
		return (zfs_error(hdl, EZFS_BADPROP, errbuf));
	}
	*bookname = strndup(nvpair_name(pair), ZFS_MAX_DATASET_NAME_LEN);
	ASSERT3P(*bookname, !=, NULL);
	fnvlist_free(bmarks);
	return (0);
}

static enum lzc_send_flags
lzc_flags_from_resume_nvl(nvlist_t *resume_nvl)
{
	enum lzc_send_flags lzc_flags = 0;

	if (nvlist_exists(resume_nvl, "largeblockok"))
		lzc_flags |= LZC_SEND_FLAG_LARGE_BLOCK;
	if (nvlist_exists(resume_nvl, "embedok"))
		lzc_flags |= LZC_SEND_FLAG_EMBED_DATA;
	if (nvlist_exists(resume_nvl, "compressok"))
		lzc_flags |= LZC_SEND_FLAG_COMPRESS;
	if (nvlist_exists(resume_nvl, "rawok"))
		lzc_flags |= LZC_SEND_FLAG_RAW;
	if (nvlist_exists(resume_nvl, "savedok"))
		lzc_flags |= LZC_SEND_FLAG_SAVED;

	return (lzc_flags);
}

static int
zfs_send_resume_impl_cb_impl(libzfs_handle_t *hdl, sendflags_t *flags,
    int outfd, nvlist_t *resume_nvl)
{
	char errbuf[ERRBUFLEN];
	const char *toname;
	const char *fromname = NULL;
	uint64_t resumeobj, resumeoff, toguid, fromguid, bytes;
	zfs_handle_t *zhp;
	int error = 0;
	char name[ZFS_MAX_DATASET_NAME_LEN];
	FILE *fout = (flags->verbosity > 0 && flags->dryrun) ? stdout : stderr;
	uint64_t *redact_snap_guids = NULL;
	int num_redact_snaps = 0;
	char *redact_book = NULL;
	uint64_t size = 0;

	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "cannot resume send"));

	if (flags->verbosity != 0) {
		(void) fprintf(fout, dgettext(TEXT_DOMAIN,
		    "resume token contents:\n"));
		nvlist_print(fout, resume_nvl);
	}

	if (nvlist_lookup_string(resume_nvl, "toname", &toname) != 0 ||
	    nvlist_lookup_uint64(resume_nvl, "object", &resumeobj) != 0 ||
	    nvlist_lookup_uint64(resume_nvl, "offset", &resumeoff) != 0 ||
	    nvlist_lookup_uint64(resume_nvl, "bytes", &bytes) != 0 ||
	    nvlist_lookup_uint64(resume_nvl, "toguid", &toguid) != 0) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "resume token is corrupt"));
		return (zfs_error(hdl, EZFS_FAULT, errbuf));
	}
	fromguid = 0;
	(void) nvlist_lookup_uint64(resume_nvl, "fromguid", &fromguid);

	if (flags->saved) {
		(void) strlcpy(name, toname, sizeof (name));
	} else {
		error = guid_to_name(hdl, toname, toguid, B_FALSE, name);
		if (error != 0) {
			if (zfs_dataset_exists(hdl, toname, ZFS_TYPE_DATASET)) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "'%s' is no longer the same snapshot "
				    "used in the initial send"), toname);
			} else {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "'%s' used in the initial send no "
				    "longer exists"), toname);
			}
			return (zfs_error(hdl, EZFS_BADPATH, errbuf));
		}
	}

	zhp = zfs_open(hdl, name, ZFS_TYPE_DATASET);
	if (zhp == NULL) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "unable to access '%s'"), name);
		return (zfs_error(hdl, EZFS_BADPATH, errbuf));
	}

	if (nvlist_lookup_uint64_array(resume_nvl, "book_redact_snaps",
	    &redact_snap_guids, (uint_t *)&num_redact_snaps) != 0) {
		num_redact_snaps = -1;
	}

	if (fromguid != 0) {
		if (guid_to_name_redact_snaps(hdl, toname, fromguid, B_TRUE,
		    redact_snap_guids, num_redact_snaps, name) != 0) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "incremental source %#llx no longer exists"),
			    (longlong_t)fromguid);
			return (zfs_error(hdl, EZFS_BADPATH, errbuf));
		}
		fromname = name;
	}

	redact_snap_guids = NULL;

	if (nvlist_lookup_uint64_array(resume_nvl,
	    zfs_prop_to_name(ZFS_PROP_REDACT_SNAPS), &redact_snap_guids,
	    (uint_t *)&num_redact_snaps) == 0) {
		char path[ZFS_MAX_DATASET_NAME_LEN];

		(void) strlcpy(path, toname, sizeof (path));
		char *at = strchr(path, '@');
		ASSERT3P(at, !=, NULL);

		*at = '\0';

		if ((error = find_redact_book(hdl, path, redact_snap_guids,
		    num_redact_snaps, &redact_book)) != 0) {
			return (error);
		}
	}

	enum lzc_send_flags lzc_flags = lzc_flags_from_sendflags(flags) |
	    lzc_flags_from_resume_nvl(resume_nvl);

	if (flags->verbosity != 0 || flags->progressastitle) {
		/*
		 * Some of these may have come from the resume token, set them
		 * here for size estimate purposes.
		 */
		sendflags_t tmpflags = *flags;
		if (lzc_flags & LZC_SEND_FLAG_LARGE_BLOCK)
			tmpflags.largeblock = B_TRUE;
		if (lzc_flags & LZC_SEND_FLAG_COMPRESS)
			tmpflags.compress = B_TRUE;
		if (lzc_flags & LZC_SEND_FLAG_EMBED_DATA)
			tmpflags.embed_data = B_TRUE;
		if (lzc_flags & LZC_SEND_FLAG_RAW)
			tmpflags.raw = B_TRUE;
		if (lzc_flags & LZC_SEND_FLAG_SAVED)
			tmpflags.saved = B_TRUE;
		error = estimate_size(zhp, fromname, outfd, &tmpflags,
		    resumeobj, resumeoff, bytes, redact_book, errbuf, &size);
	}

	if (!flags->dryrun) {
		progress_arg_t pa = { 0 };
		pthread_t tid;
		sigset_t oldmask;
		/*
		 * If progress reporting is requested, spawn a new thread to
		 * poll ZFS_IOC_SEND_PROGRESS at a regular interval.
		 */
		{
			pa.pa_zhp = zhp;
			pa.pa_fd = outfd;
			pa.pa_parsable = flags->parsable;
			pa.pa_estimate = B_FALSE;
			pa.pa_verbosity = flags->verbosity;
			pa.pa_size = size;
			pa.pa_astitle = flags->progressastitle;
			pa.pa_progress = flags->progress;

			error = pthread_create(&tid, NULL,
			    send_progress_thread, &pa);
			if (error != 0) {
				if (redact_book != NULL)
					free(redact_book);
				zfs_close(zhp);
				return (error);
			}
			SEND_PROGRESS_THREAD_PARENT_BLOCK(&oldmask);
		}

		error = lzc_send_resume_redacted(zhp->zfs_name, fromname, outfd,
		    lzc_flags, resumeobj, resumeoff, redact_book);
		if (redact_book != NULL)
			free(redact_book);

		if (send_progress_thread_exit(hdl, tid, &oldmask)) {
			zfs_close(zhp);
			return (-1);
		}

		char errbuf[ERRBUFLEN];
		(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
		    "warning: cannot send '%s'"), zhp->zfs_name);

		zfs_close(zhp);

		switch (error) {
		case 0:
			return (0);
		case EACCES:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "source key must be loaded"));
			return (zfs_error(hdl, EZFS_CRYPTOFAILED, errbuf));
		case ESRCH:
			if (lzc_exists(zhp->zfs_name)) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "incremental source could not be found"));
			}
			return (zfs_error(hdl, EZFS_NOENT, errbuf));

		case EXDEV:
		case ENOENT:
		case EDQUOT:
		case EFBIG:
		case EIO:
		case ENOLINK:
		case ENOSPC:
		case ENOSTR:
		case ENXIO:
		case EPIPE:
		case ERANGE:
		case EFAULT:
		case EROFS:
			zfs_error_aux(hdl, "%s", zfs_strerror(errno));
			return (zfs_error(hdl, EZFS_BADBACKUP, errbuf));

		default:
			return (zfs_standard_error(hdl, errno, errbuf));
		}
	} else {
		if (redact_book != NULL)
			free(redact_book);
	}

	zfs_close(zhp);

	return (error);
}

struct zfs_send_resume_impl {
	libzfs_handle_t *hdl;
	sendflags_t *flags;
	nvlist_t *resume_nvl;
};

static int
zfs_send_resume_impl_cb(int outfd, void *arg)
{
	struct zfs_send_resume_impl *zsri = arg;
	return (zfs_send_resume_impl_cb_impl(zsri->hdl, zsri->flags, outfd,
	    zsri->resume_nvl));
}

static int
zfs_send_resume_impl(libzfs_handle_t *hdl, sendflags_t *flags, int outfd,
    nvlist_t *resume_nvl)
{
	struct zfs_send_resume_impl zsri = {
		.hdl = hdl,
		.flags = flags,
		.resume_nvl = resume_nvl,
	};
	return (lzc_send_wrapper(zfs_send_resume_impl_cb, outfd, &zsri));
}

int
zfs_send_resume(libzfs_handle_t *hdl, sendflags_t *flags, int outfd,
    const char *resume_token)
{
	int ret;
	char errbuf[ERRBUFLEN];
	nvlist_t *resume_nvl;

	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "cannot resume send"));

	resume_nvl = zfs_send_resume_token_to_nvlist(hdl, resume_token);
	if (resume_nvl == NULL) {
		/*
		 * zfs_error_aux has already been set by
		 * zfs_send_resume_token_to_nvlist()
		 */
		return (zfs_error(hdl, EZFS_FAULT, errbuf));
	}

	ret = zfs_send_resume_impl(hdl, flags, outfd, resume_nvl);
	fnvlist_free(resume_nvl);

	return (ret);
}

int
zfs_send_saved(zfs_handle_t *zhp, sendflags_t *flags, int outfd,
    const char *resume_token)
{
	int ret;
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	nvlist_t *saved_nvl = NULL, *resume_nvl = NULL;
	uint64_t saved_guid = 0, resume_guid = 0;
	uint64_t obj = 0, off = 0, bytes = 0;
	char token_buf[ZFS_MAXPROPLEN];
	char errbuf[ERRBUFLEN];

	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "saved send failed"));

	ret = zfs_prop_get(zhp, ZFS_PROP_RECEIVE_RESUME_TOKEN,
	    token_buf, sizeof (token_buf), NULL, NULL, 0, B_TRUE);
	if (ret != 0)
		goto out;

	saved_nvl = zfs_send_resume_token_to_nvlist(hdl, token_buf);
	if (saved_nvl == NULL) {
		/*
		 * zfs_error_aux has already been set by
		 * zfs_send_resume_token_to_nvlist()
		 */
		ret = zfs_error(hdl, EZFS_FAULT, errbuf);
		goto out;
	}

	/*
	 * If a resume token is provided we use the object and offset
	 * from that instead of the default, which starts from the
	 * beginning.
	 */
	if (resume_token != NULL) {
		resume_nvl = zfs_send_resume_token_to_nvlist(hdl,
		    resume_token);
		if (resume_nvl == NULL) {
			ret = zfs_error(hdl, EZFS_FAULT, errbuf);
			goto out;
		}

		if (nvlist_lookup_uint64(resume_nvl, "object", &obj) != 0 ||
		    nvlist_lookup_uint64(resume_nvl, "offset", &off) != 0 ||
		    nvlist_lookup_uint64(resume_nvl, "bytes", &bytes) != 0 ||
		    nvlist_lookup_uint64(resume_nvl, "toguid",
		    &resume_guid) != 0) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "provided resume token is corrupt"));
			ret = zfs_error(hdl, EZFS_FAULT, errbuf);
			goto out;
		}

		if (nvlist_lookup_uint64(saved_nvl, "toguid",
		    &saved_guid)) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "dataset's resume token is corrupt"));
			ret = zfs_error(hdl, EZFS_FAULT, errbuf);
			goto out;
		}

		if (resume_guid != saved_guid) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "provided resume token does not match dataset"));
			ret = zfs_error(hdl, EZFS_BADBACKUP, errbuf);
			goto out;
		}
	}

	(void) nvlist_remove_all(saved_nvl, "object");
	fnvlist_add_uint64(saved_nvl, "object", obj);

	(void) nvlist_remove_all(saved_nvl, "offset");
	fnvlist_add_uint64(saved_nvl, "offset", off);

	(void) nvlist_remove_all(saved_nvl, "bytes");
	fnvlist_add_uint64(saved_nvl, "bytes", bytes);

	(void) nvlist_remove_all(saved_nvl, "toname");
	fnvlist_add_string(saved_nvl, "toname", zhp->zfs_name);

	ret = zfs_send_resume_impl(hdl, flags, outfd, saved_nvl);

out:
	fnvlist_free(saved_nvl);
	fnvlist_free(resume_nvl);
	return (ret);
}

/*
 * This function informs the target system that the recursive send is complete.
 * The record is also expected in the case of a send -p.
 */
static int
send_conclusion_record(int fd, zio_cksum_t *zc)
{
	dmu_replay_record_t drr;
	memset(&drr, 0, sizeof (dmu_replay_record_t));
	drr.drr_type = DRR_END;
	if (zc != NULL)
		drr.drr_u.drr_end.drr_checksum = *zc;
	if (write(fd, &drr, sizeof (drr)) == -1) {
		return (errno);
	}
	return (0);
}

/*
 * This function is responsible for sending the records that contain the
 * necessary information for the target system's libzfs to be able to set the
 * properties of the filesystem being received, or to be able to prepare for
 * a recursive receive.
 *
 * The "zhp" argument is the handle of the snapshot we are sending
 * (the "tosnap").  The "from" argument is the short snapshot name (the part
 * after the @) of the incremental source.
 */
static int
send_prelim_records(zfs_handle_t *zhp, const char *from, int fd,
    boolean_t gather_props, boolean_t recursive, boolean_t verbose,
    boolean_t dryrun, boolean_t raw, boolean_t replicate, boolean_t skipmissing,
    boolean_t backup, boolean_t holds, boolean_t props, boolean_t doall,
    nvlist_t **fssp, avl_tree_t **fsavlp)
{
	int err = 0;
	char *packbuf = NULL;
	size_t buflen = 0;
	zio_cksum_t zc = { {0} };
	int featureflags = 0;
	/* name of filesystem/volume that contains snapshot we are sending */
	char tofs[ZFS_MAX_DATASET_NAME_LEN];
	/* short name of snap we are sending */
	const char *tosnap = "";

	char errbuf[ERRBUFLEN];
	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "warning: cannot send '%s'"), zhp->zfs_name);
	if (zhp->zfs_type == ZFS_TYPE_FILESYSTEM && zfs_prop_get_int(zhp,
	    ZFS_PROP_VERSION) >= ZPL_VERSION_SA) {
		featureflags |= DMU_BACKUP_FEATURE_SA_SPILL;
	}

	if (holds)
		featureflags |= DMU_BACKUP_FEATURE_HOLDS;

	(void) strlcpy(tofs, zhp->zfs_name, ZFS_MAX_DATASET_NAME_LEN);
	char *at = strchr(tofs, '@');
	if (at != NULL) {
		*at = '\0';
		tosnap = at + 1;
	}

	if (gather_props) {
		nvlist_t *hdrnv = fnvlist_alloc();
		nvlist_t *fss = NULL;

		if (from != NULL)
			fnvlist_add_string(hdrnv, "fromsnap", from);
		fnvlist_add_string(hdrnv, "tosnap", tosnap);
		if (!recursive)
			fnvlist_add_boolean(hdrnv, "not_recursive");

		if (raw) {
			fnvlist_add_boolean(hdrnv, "raw");
		}

		if (gather_nvlist(zhp->zfs_hdl, tofs,
		    from, tosnap, recursive, raw, doall, replicate, skipmissing,
		    verbose, backup, holds, props, &fss, fsavlp) != 0) {
			return (zfs_error(zhp->zfs_hdl, EZFS_BADBACKUP,
			    errbuf));
		}
		/*
		 * Do not allow the size of the properties list to exceed
		 * the limit
		 */
		if ((fnvlist_size(fss) + fnvlist_size(hdrnv)) >
		    zhp->zfs_hdl->libzfs_max_nvlist) {
			(void) snprintf(errbuf, sizeof (errbuf),
			    dgettext(TEXT_DOMAIN, "warning: cannot send '%s': "
			    "the size of the list of snapshots and properties "
			    "is too large to be received successfully.\n"
			    "Select a smaller number of snapshots to send.\n"),
			    zhp->zfs_name);
			return (zfs_error(zhp->zfs_hdl, EZFS_NOSPC,
			    errbuf));
		}
		fnvlist_add_nvlist(hdrnv, "fss", fss);
		VERIFY0(nvlist_pack(hdrnv, &packbuf, &buflen, NV_ENCODE_XDR,
		    0));
		if (fssp != NULL) {
			*fssp = fss;
		} else {
			fnvlist_free(fss);
		}
		fnvlist_free(hdrnv);
	}

	if (!dryrun) {
		dmu_replay_record_t drr;
		memset(&drr, 0, sizeof (dmu_replay_record_t));
		/* write first begin record */
		drr.drr_type = DRR_BEGIN;
		drr.drr_u.drr_begin.drr_magic = DMU_BACKUP_MAGIC;
		DMU_SET_STREAM_HDRTYPE(drr.drr_u.drr_begin.
		    drr_versioninfo, DMU_COMPOUNDSTREAM);
		DMU_SET_FEATUREFLAGS(drr.drr_u.drr_begin.
		    drr_versioninfo, featureflags);
		if (snprintf(drr.drr_u.drr_begin.drr_toname,
		    sizeof (drr.drr_u.drr_begin.drr_toname), "%s@%s", tofs,
		    tosnap) >= sizeof (drr.drr_u.drr_begin.drr_toname)) {
			return (zfs_error(zhp->zfs_hdl, EZFS_BADBACKUP,
			    errbuf));
		}
		drr.drr_payloadlen = buflen;

		err = dump_record(&drr, packbuf, buflen, &zc, fd);
		free(packbuf);
		if (err != 0) {
			zfs_error_aux(zhp->zfs_hdl, "%s", zfs_strerror(err));
			return (zfs_error(zhp->zfs_hdl, EZFS_BADBACKUP,
			    errbuf));
		}
		err = send_conclusion_record(fd, &zc);
		if (err != 0) {
			zfs_error_aux(zhp->zfs_hdl, "%s", zfs_strerror(err));
			return (zfs_error(zhp->zfs_hdl, EZFS_BADBACKUP,
			    errbuf));
		}
	}
	return (0);
}

/*
 * Generate a send stream.  The "zhp" argument is the filesystem/volume
 * that contains the snapshot to send.  The "fromsnap" argument is the
 * short name (the part after the '@') of the snapshot that is the
 * incremental source to send from (if non-NULL).  The "tosnap" argument
 * is the short name of the snapshot to send.
 *
 * The content of the send stream is the snapshot identified by
 * 'tosnap'.  Incremental streams are requested in two ways:
 *     - from the snapshot identified by "fromsnap" (if non-null) or
 *     - from the origin of the dataset identified by zhp, which must
 *	 be a clone.  In this case, "fromsnap" is null and "fromorigin"
 *	 is TRUE.
 *
 * The send stream is recursive (i.e. dumps a hierarchy of snapshots) and
 * uses a special header (with a hdrtype field of DMU_COMPOUNDSTREAM)
 * if "replicate" is set.  If "doall" is set, dump all the intermediate
 * snapshots. The DMU_COMPOUNDSTREAM header is used in the "doall"
 * case too. If "props" is set, send properties.
 *
 * Pre-wrapped (cf. lzc_send_wrapper()).
 */
static int
zfs_send_cb_impl(zfs_handle_t *zhp, const char *fromsnap, const char *tosnap,
    sendflags_t *flags, int outfd, snapfilter_cb_t filter_func,
    void *cb_arg, nvlist_t **debugnvp)
{
	char errbuf[ERRBUFLEN];
	send_dump_data_t sdd = { 0 };
	int err = 0;
	nvlist_t *fss = NULL;
	avl_tree_t *fsavl = NULL;
	static uint64_t holdseq;
	int spa_version;
	FILE *fout;

	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "cannot send '%s'"), zhp->zfs_name);

	if (fromsnap && fromsnap[0] == '\0') {
		zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
		    "zero-length incremental source"));
		return (zfs_error(zhp->zfs_hdl, EZFS_NOENT, errbuf));
	}

	if (fromsnap) {
		char full_fromsnap_name[ZFS_MAX_DATASET_NAME_LEN];
		if (snprintf(full_fromsnap_name, sizeof (full_fromsnap_name),
		    "%s@%s", zhp->zfs_name, fromsnap) >=
		    sizeof (full_fromsnap_name)) {
			err = EINVAL;
			goto stderr_out;
		}
		zfs_handle_t *fromsnapn = zfs_open(zhp->zfs_hdl,
		    full_fromsnap_name, ZFS_TYPE_SNAPSHOT);
		if (fromsnapn == NULL) {
			err = -1;
			goto err_out;
		}
		zfs_close(fromsnapn);
	}

	if (flags->replicate || flags->doall || flags->props ||
	    flags->holds || flags->backup) {
		char full_tosnap_name[ZFS_MAX_DATASET_NAME_LEN];
		if (snprintf(full_tosnap_name, sizeof (full_tosnap_name),
		    "%s@%s", zhp->zfs_name, tosnap) >=
		    sizeof (full_tosnap_name)) {
			err = EINVAL;
			goto stderr_out;
		}
		zfs_handle_t *tosnap = zfs_open(zhp->zfs_hdl,
		    full_tosnap_name, ZFS_TYPE_SNAPSHOT);
		if (tosnap == NULL) {
			err = -1;
			goto err_out;
		}
		err = send_prelim_records(tosnap, fromsnap, outfd,
		    flags->replicate || flags->props || flags->holds,
		    flags->replicate, flags->verbosity > 0, flags->dryrun,
		    flags->raw, flags->replicate, flags->skipmissing,
		    flags->backup, flags->holds, flags->props, flags->doall,
		    &fss, &fsavl);
		zfs_close(tosnap);
		if (err != 0)
			goto err_out;
	}

	/* dump each stream */
	sdd.fromsnap = fromsnap;
	sdd.tosnap = tosnap;
	sdd.outfd = outfd;
	sdd.replicate = flags->replicate;
	sdd.doall = flags->doall;
	sdd.fromorigin = flags->fromorigin;
	sdd.fss = fss;
	sdd.fsavl = fsavl;
	sdd.verbosity = flags->verbosity;
	sdd.parsable = flags->parsable;
	sdd.progress = flags->progress;
	sdd.progressastitle = flags->progressastitle;
	sdd.dryrun = flags->dryrun;
	sdd.large_block = flags->largeblock;
	sdd.embed_data = flags->embed_data;
	sdd.compress = flags->compress;
	sdd.raw = flags->raw;
	sdd.holds = flags->holds;
	sdd.filter_cb = filter_func;
	sdd.filter_cb_arg = cb_arg;
	if (debugnvp)
		sdd.debugnv = *debugnvp;
	if (sdd.verbosity != 0 && sdd.dryrun)
		sdd.std_out = B_TRUE;
	fout = sdd.std_out ? stdout : stderr;

	/*
	 * Some flags require that we place user holds on the datasets that are
	 * being sent so they don't get destroyed during the send. We can skip
	 * this step if the pool is imported read-only since the datasets cannot
	 * be destroyed.
	 */
	if (!flags->dryrun && !zpool_get_prop_int(zfs_get_pool_handle(zhp),
	    ZPOOL_PROP_READONLY, NULL) &&
	    zfs_spa_version(zhp, &spa_version) == 0 &&
	    spa_version >= SPA_VERSION_USERREFS &&
	    (flags->doall || flags->replicate)) {
		++holdseq;
		(void) snprintf(sdd.holdtag, sizeof (sdd.holdtag),
		    ".send-%d-%llu", getpid(), (u_longlong_t)holdseq);
		sdd.cleanup_fd = open(ZFS_DEV, O_RDWR | O_CLOEXEC);
		if (sdd.cleanup_fd < 0) {
			err = errno;
			goto stderr_out;
		}
		sdd.snapholds = fnvlist_alloc();
	} else {
		sdd.cleanup_fd = -1;
		sdd.snapholds = NULL;
	}

	if (flags->verbosity != 0 || sdd.snapholds != NULL) {
		/*
		 * Do a verbose no-op dry run to get all the verbose output
		 * or to gather snapshot hold's before generating any data,
		 * then do a non-verbose real run to generate the streams.
		 */
		sdd.dryrun = B_TRUE;
		err = dump_filesystems(zhp, &sdd);

		if (err != 0)
			goto stderr_out;

		if (flags->verbosity != 0) {
			if (flags->parsable) {
				(void) fprintf(fout, "size\t%llu\n",
				    (longlong_t)sdd.size);
			} else {
				char buf[16];
				zfs_nicebytes(sdd.size, buf, sizeof (buf));
				(void) fprintf(fout, dgettext(TEXT_DOMAIN,
				    "total estimated size is %s\n"), buf);
			}
		}

		/* Ensure no snaps found is treated as an error. */
		if (!sdd.seento) {
			err = ENOENT;
			goto err_out;
		}

		/* Skip the second run if dryrun was requested. */
		if (flags->dryrun)
			goto err_out;

		if (sdd.snapholds != NULL) {
			err = zfs_hold_nvl(zhp, sdd.cleanup_fd, sdd.snapholds);
			if (err != 0)
				goto stderr_out;

			fnvlist_free(sdd.snapholds);
			sdd.snapholds = NULL;
		}

		sdd.dryrun = B_FALSE;
		sdd.verbosity = 0;
	}

	err = dump_filesystems(zhp, &sdd);
	fsavl_destroy(fsavl);
	fnvlist_free(fss);

	/* Ensure no snaps found is treated as an error. */
	if (err == 0 && !sdd.seento)
		err = ENOENT;

	if (sdd.cleanup_fd != -1) {
		VERIFY(0 == close(sdd.cleanup_fd));
		sdd.cleanup_fd = -1;
	}

	if (!flags->dryrun && (flags->replicate || flags->doall ||
	    flags->props || flags->backup || flags->holds)) {
		/*
		 * write final end record.  NB: want to do this even if
		 * there was some error, because it might not be totally
		 * failed.
		 */
		int err2 = send_conclusion_record(outfd, NULL);
		if (err2 != 0)
			return (zfs_standard_error(zhp->zfs_hdl, err2, errbuf));
	}

	return (err || sdd.err);

stderr_out:
	err = zfs_standard_error(zhp->zfs_hdl, err, errbuf);
err_out:
	fsavl_destroy(fsavl);
	fnvlist_free(fss);
	fnvlist_free(sdd.snapholds);

	if (sdd.cleanup_fd != -1)
		VERIFY(0 == close(sdd.cleanup_fd));
	return (err);
}

struct zfs_send {
	zfs_handle_t *zhp;
	const char *fromsnap;
	const char *tosnap;
	sendflags_t *flags;
	snapfilter_cb_t *filter_func;
	void *cb_arg;
	nvlist_t **debugnvp;
};

static int
zfs_send_cb(int outfd, void *arg)
{
	struct zfs_send *zs = arg;
	return (zfs_send_cb_impl(zs->zhp, zs->fromsnap, zs->tosnap, zs->flags,
	    outfd, zs->filter_func, zs->cb_arg, zs->debugnvp));
}

int
zfs_send(zfs_handle_t *zhp, const char *fromsnap, const char *tosnap,
    sendflags_t *flags, int outfd, snapfilter_cb_t filter_func,
    void *cb_arg, nvlist_t **debugnvp)
{
	struct zfs_send arg = {
		.zhp = zhp,
		.fromsnap = fromsnap,
		.tosnap = tosnap,
		.flags = flags,
		.filter_func = filter_func,
		.cb_arg = cb_arg,
		.debugnvp = debugnvp,
	};
	return (lzc_send_wrapper(zfs_send_cb, outfd, &arg));
}


static zfs_handle_t *
name_to_dir_handle(libzfs_handle_t *hdl, const char *snapname)
{
	char dirname[ZFS_MAX_DATASET_NAME_LEN];
	(void) strlcpy(dirname, snapname, ZFS_MAX_DATASET_NAME_LEN);
	char *c = strchr(dirname, '@');
	if (c != NULL)
		*c = '\0';
	return (zfs_open(hdl, dirname, ZFS_TYPE_DATASET));
}

/*
 * Returns B_TRUE if earlier is an earlier snapshot in later's timeline; either
 * an earlier snapshot in the same filesystem, or a snapshot before later's
 * origin, or it's origin's origin, etc.
 */
static boolean_t
snapshot_is_before(zfs_handle_t *earlier, zfs_handle_t *later)
{
	boolean_t ret;
	uint64_t later_txg =
	    (later->zfs_type == ZFS_TYPE_FILESYSTEM ||
	    later->zfs_type == ZFS_TYPE_VOLUME ?
	    UINT64_MAX : zfs_prop_get_int(later, ZFS_PROP_CREATETXG));
	uint64_t earlier_txg = zfs_prop_get_int(earlier, ZFS_PROP_CREATETXG);

	if (earlier_txg >= later_txg)
		return (B_FALSE);

	zfs_handle_t *earlier_dir = name_to_dir_handle(earlier->zfs_hdl,
	    earlier->zfs_name);
	zfs_handle_t *later_dir = name_to_dir_handle(later->zfs_hdl,
	    later->zfs_name);

	if (strcmp(earlier_dir->zfs_name, later_dir->zfs_name) == 0) {
		zfs_close(earlier_dir);
		zfs_close(later_dir);
		return (B_TRUE);
	}

	char clonename[ZFS_MAX_DATASET_NAME_LEN];
	if (zfs_prop_get(later_dir, ZFS_PROP_ORIGIN, clonename,
	    ZFS_MAX_DATASET_NAME_LEN, NULL, NULL, 0, B_TRUE) != 0) {
		zfs_close(earlier_dir);
		zfs_close(later_dir);
		return (B_FALSE);
	}

	zfs_handle_t *origin = zfs_open(earlier->zfs_hdl, clonename,
	    ZFS_TYPE_DATASET);
	uint64_t origin_txg = zfs_prop_get_int(origin, ZFS_PROP_CREATETXG);

	/*
	 * If "earlier" is exactly the origin, then
	 * snapshot_is_before(earlier, origin) will return false (because
	 * they're the same).
	 */
	if (origin_txg == earlier_txg &&
	    strcmp(origin->zfs_name, earlier->zfs_name) == 0) {
		zfs_close(earlier_dir);
		zfs_close(later_dir);
		zfs_close(origin);
		return (B_TRUE);
	}
	zfs_close(earlier_dir);
	zfs_close(later_dir);

	ret = snapshot_is_before(earlier, origin);
	zfs_close(origin);
	return (ret);
}

/*
 * The "zhp" argument is the handle of the dataset to send (typically a
 * snapshot).  The "from" argument is the full name of the snapshot or
 * bookmark that is the incremental source.
 *
 * Pre-wrapped (cf. lzc_send_wrapper()).
 */
static int
zfs_send_one_cb_impl(zfs_handle_t *zhp, const char *from, int fd,
    sendflags_t *flags, const char *redactbook)
{
	int err;
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	char *name = zhp->zfs_name;
	pthread_t ptid;
	progress_arg_t pa = { 0 };
	uint64_t size = 0;

	char errbuf[ERRBUFLEN];
	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "warning: cannot send '%s'"), name);

	if (from != NULL && strchr(from, '@')) {
		zfs_handle_t *from_zhp = zfs_open(hdl, from,
		    ZFS_TYPE_DATASET);
		if (from_zhp == NULL)
			return (-1);
		if (!snapshot_is_before(from_zhp, zhp)) {
			zfs_close(from_zhp);
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "not an earlier snapshot from the same fs"));
			return (zfs_error(hdl, EZFS_CROSSTARGET, errbuf));
		}
		zfs_close(from_zhp);
	}

	if (redactbook != NULL) {
		char bookname[ZFS_MAX_DATASET_NAME_LEN];
		nvlist_t *redact_snaps;
		zfs_handle_t *book_zhp;
		char *at, *pound;
		int dsnamelen;

		pound = strchr(redactbook, '#');
		if (pound != NULL)
			redactbook = pound + 1;
		at = strchr(name, '@');
		if (at == NULL) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "cannot do a redacted send to a filesystem"));
			return (zfs_error(hdl, EZFS_BADTYPE, errbuf));
		}
		dsnamelen = at - name;
		if (snprintf(bookname, sizeof (bookname), "%.*s#%s",
		    dsnamelen, name, redactbook)
		    >= sizeof (bookname)) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "invalid bookmark name"));
			return (zfs_error(hdl, EZFS_INVALIDNAME, errbuf));
		}
		book_zhp = zfs_open(hdl, bookname, ZFS_TYPE_BOOKMARK);
		if (book_zhp == NULL)
			return (-1);
		if (nvlist_lookup_nvlist(book_zhp->zfs_props,
		    zfs_prop_to_name(ZFS_PROP_REDACT_SNAPS),
		    &redact_snaps) != 0 || redact_snaps == NULL) {
			zfs_close(book_zhp);
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "not a redaction bookmark"));
			return (zfs_error(hdl, EZFS_BADTYPE, errbuf));
		}
		zfs_close(book_zhp);
	}

	/*
	 * Send fs properties
	 */
	if (flags->props || flags->holds || flags->backup) {
		/*
		 * Note: the header generated by send_prelim_records()
		 * assumes that the incremental source is in the same
		 * filesystem/volume as the target (which is a requirement
		 * when doing "zfs send -R").  But that isn't always the
		 * case here (e.g. send from snap in origin, or send from
		 * bookmark).  We pass from=NULL, which will omit this
		 * information from the prelim records; it isn't used
		 * when receiving this type of stream.
		 */
		err = send_prelim_records(zhp, NULL, fd, B_TRUE, B_FALSE,
		    flags->verbosity > 0, flags->dryrun, flags->raw,
		    flags->replicate, B_FALSE, flags->backup, flags->holds,
		    flags->props, flags->doall, NULL, NULL);
		if (err != 0)
			return (err);
	}

	/*
	 * Perform size estimate if verbose was specified.
	 */
	if (flags->verbosity != 0 || flags->progressastitle) {
		err = estimate_size(zhp, from, fd, flags, 0, 0, 0, redactbook,
		    errbuf, &size);
		if (err != 0)
			return (err);
	}

	if (flags->dryrun)
		return (0);

	/*
	 * If progress reporting is requested, spawn a new thread to poll
	 * ZFS_IOC_SEND_PROGRESS at a regular interval.
	 */
	sigset_t oldmask;
	{
		pa.pa_zhp = zhp;
		pa.pa_fd = fd;
		pa.pa_parsable = flags->parsable;
		pa.pa_estimate = B_FALSE;
		pa.pa_verbosity = flags->verbosity;
		pa.pa_size = size;
		pa.pa_astitle = flags->progressastitle;
		pa.pa_progress = flags->progress;

		err = pthread_create(&ptid, NULL,
		    send_progress_thread, &pa);
		if (err != 0) {
			zfs_error_aux(zhp->zfs_hdl, "%s", zfs_strerror(errno));
			return (zfs_error(zhp->zfs_hdl,
			    EZFS_THREADCREATEFAILED, errbuf));
		}
		SEND_PROGRESS_THREAD_PARENT_BLOCK(&oldmask);
	}

	err = lzc_send_redacted(name, from, fd,
	    lzc_flags_from_sendflags(flags), redactbook);

	if (send_progress_thread_exit(hdl, ptid, &oldmask))
			return (-1);

	if (err == 0 && (flags->props || flags->holds || flags->backup)) {
		/* Write the final end record. */
		err = send_conclusion_record(fd, NULL);
		if (err != 0)
			return (zfs_standard_error(hdl, err, errbuf));
	}
	if (err != 0) {
		switch (errno) {
		case EXDEV:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "not an earlier snapshot from the same fs"));
			return (zfs_error(hdl, EZFS_CROSSTARGET, errbuf));

		case ENOENT:
		case ESRCH:
			if (lzc_exists(name)) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "incremental source (%s) does not exist"),
				    from);
			}
			return (zfs_error(hdl, EZFS_NOENT, errbuf));

		case EACCES:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "dataset key must be loaded"));
			return (zfs_error(hdl, EZFS_CRYPTOFAILED, errbuf));

		case EBUSY:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "target is busy; if a filesystem, "
			    "it must not be mounted"));
			return (zfs_error(hdl, EZFS_BUSY, errbuf));

		case EDQUOT:
		case EFAULT:
		case EFBIG:
		case EINVAL:
		case EIO:
		case ENOLINK:
		case ENOSPC:
		case ENOSTR:
		case ENXIO:
		case EPIPE:
		case ERANGE:
		case EROFS:
			zfs_error_aux(hdl, "%s", zfs_strerror(errno));
			return (zfs_error(hdl, EZFS_BADBACKUP, errbuf));
		case ZFS_ERR_STREAM_LARGE_MICROZAP:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "source snapshot contains large microzaps, "
			    "need -L (--large-block) or -w (--raw) to "
			    "generate stream"));
			return (zfs_error(hdl, EZFS_BADBACKUP, errbuf));
		default:
			return (zfs_standard_error(hdl, errno, errbuf));
		}
	}
	return (err != 0);
}

struct zfs_send_one {
	zfs_handle_t *zhp;
	const char *from;
	sendflags_t *flags;
	const char *redactbook;
};

static int
zfs_send_one_cb(int fd, void *arg)
{
	struct zfs_send_one *zso = arg;
	return (zfs_send_one_cb_impl(zso->zhp, zso->from, fd, zso->flags,
	    zso->redactbook));
}

int
zfs_send_one(zfs_handle_t *zhp, const char *from, int fd, sendflags_t *flags,
    const char *redactbook)
{
	struct zfs_send_one zso = {
		.zhp = zhp,
		.from = from,
		.flags = flags,
		.redactbook = redactbook,
	};
	return (lzc_send_wrapper(zfs_send_one_cb, fd, &zso));
}

/*
 * Routines specific to "zfs recv"
 */

static int
recv_read(libzfs_handle_t *hdl, int fd, void *buf, int ilen,
    boolean_t byteswap, zio_cksum_t *zc)
{
	char *cp = buf;
	int rv;
	int len = ilen;

	do {
		rv = read(fd, cp, len);
		cp += rv;
		len -= rv;
	} while (rv > 0);

	if (rv < 0 || len != 0) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "failed to read from stream"));
		return (zfs_error(hdl, EZFS_BADSTREAM, dgettext(TEXT_DOMAIN,
		    "cannot receive")));
	}

	if (zc) {
		if (byteswap)
			fletcher_4_incremental_byteswap(buf, ilen, zc);
		else
			fletcher_4_incremental_native(buf, ilen, zc);
	}
	return (0);
}

static int
recv_read_nvlist(libzfs_handle_t *hdl, int fd, int len, nvlist_t **nvp,
    boolean_t byteswap, zio_cksum_t *zc)
{
	char *buf;
	int err;

	buf = zfs_alloc(hdl, len);

	if (len > hdl->libzfs_max_nvlist) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, "nvlist too large"));
		free(buf);
		return (ENOMEM);
	}

	err = recv_read(hdl, fd, buf, len, byteswap, zc);
	if (err != 0) {
		free(buf);
		return (err);
	}

	err = nvlist_unpack(buf, len, nvp, 0);
	free(buf);
	if (err != 0) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, "invalid "
		    "stream (malformed nvlist)"));
		return (EINVAL);
	}
	return (0);
}

/*
 * Returns the grand origin (origin of origin of origin...) of a given handle.
 * If this dataset is not a clone, it simply returns a copy of the original
 * handle.
 */
static zfs_handle_t *
recv_open_grand_origin(zfs_handle_t *zhp)
{
	char origin[ZFS_MAX_DATASET_NAME_LEN];
	zprop_source_t src;
	zfs_handle_t *ozhp = zfs_handle_dup(zhp);

	while (ozhp != NULL) {
		if (zfs_prop_get(ozhp, ZFS_PROP_ORIGIN, origin,
		    sizeof (origin), &src, NULL, 0, B_FALSE) != 0)
			break;

		(void) zfs_close(ozhp);
		ozhp = zfs_open(zhp->zfs_hdl, origin, ZFS_TYPE_FILESYSTEM);
	}

	return (ozhp);
}

static int
recv_rename_impl(zfs_handle_t *zhp, const char *name, const char *newname)
{
	int err;
	zfs_handle_t *ozhp = NULL;

	/*
	 * Attempt to rename the dataset. If it fails with EACCES we have
	 * attempted to rename the dataset outside of its encryption root.
	 * Force the dataset to become an encryption root and try again.
	 */
	err = lzc_rename(name, newname);
	if (err == EACCES) {
		ozhp = recv_open_grand_origin(zhp);
		if (ozhp == NULL) {
			err = ENOENT;
			goto out;
		}

		err = lzc_change_key(ozhp->zfs_name, DCP_CMD_FORCE_NEW_KEY,
		    NULL, NULL, 0);
		if (err != 0)
			goto out;

		err = lzc_rename(name, newname);
	}

out:
	if (ozhp != NULL)
		zfs_close(ozhp);
	return (err);
}

static int
recv_rename(libzfs_handle_t *hdl, const char *name, const char *tryname,
    int baselen, char *newname, recvflags_t *flags)
{
	static int seq;
	int err;
	prop_changelist_t *clp = NULL;
	zfs_handle_t *zhp = NULL;

	zhp = zfs_open(hdl, name, ZFS_TYPE_DATASET);
	if (zhp == NULL) {
		err = -1;
		goto out;
	}
	clp = changelist_gather(zhp, ZFS_PROP_NAME, 0,
	    flags->force ? MS_FORCE : 0);
	if (clp == NULL) {
		err = -1;
		goto out;
	}
	err = changelist_prefix(clp);
	if (err)
		goto out;

	if (tryname) {
		(void) strlcpy(newname, tryname, ZFS_MAX_DATASET_NAME_LEN);
		if (flags->verbose) {
			(void) printf("attempting rename %s to %s\n",
			    name, newname);
		}
		err = recv_rename_impl(zhp, name, newname);
		if (err == 0)
			changelist_rename(clp, name, tryname);
	} else {
		err = ENOENT;
	}

	if (err != 0 && strncmp(name + baselen, "recv-", 5) != 0) {
		seq++;

		(void) snprintf(newname, ZFS_MAX_DATASET_NAME_LEN,
		    "%.*srecv-%u-%u", baselen, name, getpid(), seq);

		if (flags->verbose) {
			(void) printf("failed - trying rename %s to %s\n",
			    name, newname);
		}
		err = recv_rename_impl(zhp, name, newname);
		if (err == 0)
			changelist_rename(clp, name, newname);
		if (err && flags->verbose) {
			(void) printf("failed (%u) - "
			    "will try again on next pass\n", errno);
		}
		err = EAGAIN;
	} else if (flags->verbose) {
		if (err == 0)
			(void) printf("success\n");
		else
			(void) printf("failed (%u)\n", errno);
	}

	(void) changelist_postfix(clp);

out:
	if (clp != NULL)
		changelist_free(clp);
	if (zhp != NULL)
		zfs_close(zhp);

	return (err);
}

static int
recv_promote(libzfs_handle_t *hdl, const char *fsname,
    const char *origin_fsname, recvflags_t *flags)
{
	int err;
	zfs_cmd_t zc = {"\0"};
	zfs_handle_t *zhp = NULL, *ozhp = NULL;

	if (flags->verbose)
		(void) printf("promoting %s\n", fsname);

	(void) strlcpy(zc.zc_value, origin_fsname, sizeof (zc.zc_value));
	(void) strlcpy(zc.zc_name, fsname, sizeof (zc.zc_name));

	/*
	 * Attempt to promote the dataset. If it fails with EACCES the
	 * promotion would cause this dataset to leave its encryption root.
	 * Force the origin to become an encryption root and try again.
	 */
	err = zfs_ioctl(hdl, ZFS_IOC_PROMOTE, &zc);
	if (err == EACCES) {
		zhp = zfs_open(hdl, fsname, ZFS_TYPE_DATASET);
		if (zhp == NULL) {
			err = -1;
			goto out;
		}

		ozhp = recv_open_grand_origin(zhp);
		if (ozhp == NULL) {
			err = -1;
			goto out;
		}

		err = lzc_change_key(ozhp->zfs_name, DCP_CMD_FORCE_NEW_KEY,
		    NULL, NULL, 0);
		if (err != 0)
			goto out;

		err = zfs_ioctl(hdl, ZFS_IOC_PROMOTE, &zc);
	}

out:
	if (zhp != NULL)
		zfs_close(zhp);
	if (ozhp != NULL)
		zfs_close(ozhp);

	return (err);
}

static int
recv_destroy(libzfs_handle_t *hdl, const char *name, int baselen,
    char *newname, recvflags_t *flags)
{
	int err = 0;
	prop_changelist_t *clp;
	zfs_handle_t *zhp;
	boolean_t defer = B_FALSE;
	int spa_version;

	zhp = zfs_open(hdl, name, ZFS_TYPE_DATASET);
	if (zhp == NULL)
		return (-1);
	zfs_type_t type = zfs_get_type(zhp);
	if (type == ZFS_TYPE_SNAPSHOT &&
	    zfs_spa_version(zhp, &spa_version) == 0 &&
	    spa_version >= SPA_VERSION_USERREFS)
		defer = B_TRUE;
	clp = changelist_gather(zhp, ZFS_PROP_NAME, 0,
	    flags->force ? MS_FORCE : 0);
	zfs_close(zhp);
	if (clp == NULL)
		return (-1);

	err = changelist_prefix(clp);
	if (err)
		return (err);

	if (flags->verbose)
		(void) printf("attempting destroy %s\n", name);
	if (type == ZFS_TYPE_SNAPSHOT) {
		nvlist_t *nv = fnvlist_alloc();
		fnvlist_add_boolean(nv, name);
		err = lzc_destroy_snaps(nv, defer, NULL);
		fnvlist_free(nv);
	} else {
		err = lzc_destroy(name);
	}
	if (err == 0) {
		if (flags->verbose)
			(void) printf("success\n");
		changelist_remove(clp, name);
	}

	(void) changelist_postfix(clp);
	changelist_free(clp);

	/*
	 * Deferred destroy might destroy the snapshot or only mark it to be
	 * destroyed later, and it returns success in either case.
	 */
	if (err != 0 || (defer && zfs_dataset_exists(hdl, name,
	    ZFS_TYPE_SNAPSHOT))) {
		err = recv_rename(hdl, name, NULL, baselen, newname, flags);
	}

	return (err);
}

typedef struct guid_to_name_data {
	uint64_t guid;
	boolean_t bookmark_ok;
	char *name;
	char *skip;
	uint64_t *redact_snap_guids;
	uint64_t num_redact_snaps;
} guid_to_name_data_t;

static boolean_t
redact_snaps_match(zfs_handle_t *zhp, guid_to_name_data_t *gtnd)
{
	uint64_t *bmark_snaps;
	uint_t bmark_num_snaps;
	nvlist_t *nvl;
	if (zhp->zfs_type != ZFS_TYPE_BOOKMARK)
		return (B_FALSE);

	nvl = fnvlist_lookup_nvlist(zhp->zfs_props,
	    zfs_prop_to_name(ZFS_PROP_REDACT_SNAPS));
	bmark_snaps = fnvlist_lookup_uint64_array(nvl, ZPROP_VALUE,
	    &bmark_num_snaps);
	if (bmark_num_snaps != gtnd->num_redact_snaps)
		return (B_FALSE);
	int i = 0;
	for (; i < bmark_num_snaps; i++) {
		int j = 0;
		for (; j < bmark_num_snaps; j++) {
			if (bmark_snaps[i] == gtnd->redact_snap_guids[j])
				break;
		}
		if (j == bmark_num_snaps)
			break;
	}
	return (i == bmark_num_snaps);
}

static int
guid_to_name_cb(zfs_handle_t *zhp, void *arg)
{
	guid_to_name_data_t *gtnd = arg;
	const char *slash;
	int err;

	if (gtnd->skip != NULL &&
	    (slash = strrchr(zhp->zfs_name, '/')) != NULL &&
	    strcmp(slash + 1, gtnd->skip) == 0) {
		zfs_close(zhp);
		return (0);
	}

	if (zfs_prop_get_int(zhp, ZFS_PROP_GUID) == gtnd->guid &&
	    (gtnd->num_redact_snaps == -1 || redact_snaps_match(zhp, gtnd))) {
		(void) strcpy(gtnd->name, zhp->zfs_name);
		zfs_close(zhp);
		return (EEXIST);
	}

	err = zfs_iter_children_v2(zhp, 0, guid_to_name_cb, gtnd);
	if (err != EEXIST && gtnd->bookmark_ok)
		err = zfs_iter_bookmarks_v2(zhp, 0, guid_to_name_cb, gtnd);
	zfs_close(zhp);
	return (err);
}

/*
 * Attempt to find the local dataset associated with this guid.  In the case of
 * multiple matches, we attempt to find the "best" match by searching
 * progressively larger portions of the hierarchy.  This allows one to send a
 * tree of datasets individually and guarantee that we will find the source
 * guid within that hierarchy, even if there are multiple matches elsewhere.
 *
 * If num_redact_snaps is not -1, we attempt to find a redaction bookmark with
 * the specified number of redaction snapshots.  If num_redact_snaps isn't 0 or
 * -1, then redact_snap_guids will be an array of the guids of the snapshots the
 * redaction bookmark was created with.  If num_redact_snaps is -1, then we will
 * attempt to find a snapshot or bookmark (if bookmark_ok is passed) with the
 * given guid.  Note that a redaction bookmark can be returned if
 * num_redact_snaps == -1.
 */
static int
guid_to_name_redact_snaps(libzfs_handle_t *hdl, const char *parent,
    uint64_t guid, boolean_t bookmark_ok, uint64_t *redact_snap_guids,
    uint64_t num_redact_snaps, char *name)
{
	char pname[ZFS_MAX_DATASET_NAME_LEN];
	guid_to_name_data_t gtnd;

	gtnd.guid = guid;
	gtnd.bookmark_ok = bookmark_ok;
	gtnd.name = name;
	gtnd.skip = NULL;
	gtnd.redact_snap_guids = redact_snap_guids;
	gtnd.num_redact_snaps = num_redact_snaps;

	/*
	 * Search progressively larger portions of the hierarchy, starting
	 * with the filesystem specified by 'parent'.  This will
	 * select the "most local" version of the origin snapshot in the case
	 * that there are multiple matching snapshots in the system.
	 */
	(void) strlcpy(pname, parent, sizeof (pname));
	char *cp = strrchr(pname, '@');
	if (cp == NULL)
		cp = strchr(pname, '\0');
	for (; cp != NULL; cp = strrchr(pname, '/')) {
		/* Chop off the last component and open the parent */
		*cp = '\0';
		zfs_handle_t *zhp = make_dataset_handle(hdl, pname);

		if (zhp == NULL)
			continue;
		int err = guid_to_name_cb(zfs_handle_dup(zhp), &gtnd);
		if (err != EEXIST)
			err = zfs_iter_children_v2(zhp, 0, guid_to_name_cb,
			    &gtnd);
		if (err != EEXIST && bookmark_ok)
			err = zfs_iter_bookmarks_v2(zhp, 0, guid_to_name_cb,
			    &gtnd);
		zfs_close(zhp);
		if (err == EEXIST)
			return (0);

		/*
		 * Remember the last portion of the dataset so we skip it next
		 * time through (as we've already searched that portion of the
		 * hierarchy).
		 */
		gtnd.skip = strrchr(pname, '/') + 1;
	}

	return (ENOENT);
}

static int
guid_to_name(libzfs_handle_t *hdl, const char *parent, uint64_t guid,
    boolean_t bookmark_ok, char *name)
{
	return (guid_to_name_redact_snaps(hdl, parent, guid, bookmark_ok, NULL,
	    -1, name));
}

/*
 * Return +1 if guid1 is before guid2, 0 if they are the same, and -1 if
 * guid1 is after guid2.
 */
static int
created_before(libzfs_handle_t *hdl, avl_tree_t *avl,
    uint64_t guid1, uint64_t guid2)
{
	nvlist_t *nvfs;
	const char *fsname = NULL, *snapname = NULL;
	char buf[ZFS_MAX_DATASET_NAME_LEN];
	int rv;
	zfs_handle_t *guid1hdl, *guid2hdl;
	uint64_t create1, create2;

	if (guid2 == 0)
		return (0);
	if (guid1 == 0)
		return (1);

	nvfs = fsavl_find(avl, guid1, &snapname);
	fsname = fnvlist_lookup_string(nvfs, "name");
	(void) snprintf(buf, sizeof (buf), "%s@%s", fsname, snapname);
	guid1hdl = zfs_open(hdl, buf, ZFS_TYPE_SNAPSHOT);
	if (guid1hdl == NULL)
		return (-1);

	nvfs = fsavl_find(avl, guid2, &snapname);
	fsname = fnvlist_lookup_string(nvfs, "name");
	(void) snprintf(buf, sizeof (buf), "%s@%s", fsname, snapname);
	guid2hdl = zfs_open(hdl, buf, ZFS_TYPE_SNAPSHOT);
	if (guid2hdl == NULL) {
		zfs_close(guid1hdl);
		return (-1);
	}

	create1 = zfs_prop_get_int(guid1hdl, ZFS_PROP_CREATETXG);
	create2 = zfs_prop_get_int(guid2hdl, ZFS_PROP_CREATETXG);

	if (create1 < create2)
		rv = -1;
	else if (create1 > create2)
		rv = +1;
	else
		rv = 0;

	zfs_close(guid1hdl);
	zfs_close(guid2hdl);

	return (rv);
}

/*
 * This function reestablishes the hierarchy of encryption roots after a
 * recursive incremental receive has completed. This must be done after the
 * second call to recv_incremental_replication() has renamed and promoted all
 * sent datasets to their final locations in the dataset hierarchy.
 */
static int
recv_fix_encryption_hierarchy(libzfs_handle_t *hdl, const char *top_zfs,
    nvlist_t *stream_nv)
{
	int err;
	nvpair_t *fselem = NULL;
	nvlist_t *stream_fss;

	stream_fss = fnvlist_lookup_nvlist(stream_nv, "fss");

	while ((fselem = nvlist_next_nvpair(stream_fss, fselem)) != NULL) {
		zfs_handle_t *zhp = NULL;
		uint64_t crypt;
		nvlist_t *snaps, *props, *stream_nvfs = NULL;
		nvpair_t *snapel = NULL;
		boolean_t is_encroot, is_clone, stream_encroot;
		char *cp;
		const char *stream_keylocation = NULL;
		char keylocation[MAXNAMELEN];
		char fsname[ZFS_MAX_DATASET_NAME_LEN];

		keylocation[0] = '\0';
		stream_nvfs = fnvpair_value_nvlist(fselem);
		snaps = fnvlist_lookup_nvlist(stream_nvfs, "snaps");
		props = fnvlist_lookup_nvlist(stream_nvfs, "props");
		stream_encroot = nvlist_exists(stream_nvfs, "is_encroot");

		/* find a snapshot from the stream that exists locally */
		err = ENOENT;
		while ((snapel = nvlist_next_nvpair(snaps, snapel)) != NULL) {
			uint64_t guid;

			guid = fnvpair_value_uint64(snapel);
			err = guid_to_name(hdl, top_zfs, guid, B_FALSE,
			    fsname);
			if (err == 0)
				break;
		}

		if (err != 0)
			continue;

		cp = strchr(fsname, '@');
		if (cp != NULL)
			*cp = '\0';

		zhp = zfs_open(hdl, fsname, ZFS_TYPE_DATASET);
		if (zhp == NULL) {
			err = ENOENT;
			goto error;
		}

		crypt = zfs_prop_get_int(zhp, ZFS_PROP_ENCRYPTION);
		is_clone = zhp->zfs_dmustats.dds_origin[0] != '\0';
		(void) zfs_crypto_get_encryption_root(zhp, &is_encroot, NULL);

		/* we don't need to do anything for unencrypted datasets */
		if (crypt == ZIO_CRYPT_OFF) {
			zfs_close(zhp);
			continue;
		}

		/*
		 * If the dataset is flagged as an encryption root, was not
		 * received as a clone and is not currently an encryption root,
		 * force it to become one. Fixup the keylocation if necessary.
		 */
		if (stream_encroot) {
			if (!is_clone && !is_encroot) {
				err = lzc_change_key(fsname,
				    DCP_CMD_FORCE_NEW_KEY, NULL, NULL, 0);
				if (err != 0) {
					zfs_close(zhp);
					goto error;
				}
			}

			stream_keylocation = fnvlist_lookup_string(props,
			    zfs_prop_to_name(ZFS_PROP_KEYLOCATION));

			/*
			 * Refresh the properties in case the call to
			 * lzc_change_key() changed the value.
			 */
			zfs_refresh_properties(zhp);
			err = zfs_prop_get(zhp, ZFS_PROP_KEYLOCATION,
			    keylocation, sizeof (keylocation), NULL, NULL,
			    0, B_TRUE);
			if (err != 0) {
				zfs_close(zhp);
				goto error;
			}

			if (strcmp(keylocation, stream_keylocation) != 0) {
				err = zfs_prop_set(zhp,
				    zfs_prop_to_name(ZFS_PROP_KEYLOCATION),
				    stream_keylocation);
				if (err != 0) {
					zfs_close(zhp);
					goto error;
				}
			}
		}

		/*
		 * If the dataset is not flagged as an encryption root and is
		 * currently an encryption root, force it to inherit from its
		 * parent. The root of a raw send should never be
		 * force-inherited.
		 */
		if (!stream_encroot && is_encroot &&
		    strcmp(top_zfs, fsname) != 0) {
			err = lzc_change_key(fsname, DCP_CMD_FORCE_INHERIT,
			    NULL, NULL, 0);
			if (err != 0) {
				zfs_close(zhp);
				goto error;
			}
		}

		zfs_close(zhp);
	}

	return (0);

error:
	return (err);
}

static int
recv_incremental_replication(libzfs_handle_t *hdl, const char *tofs,
    recvflags_t *flags, nvlist_t *stream_nv, avl_tree_t *stream_avl,
    nvlist_t *renamed)
{
	nvlist_t *local_nv, *deleted = NULL;
	avl_tree_t *local_avl;
	nvpair_t *fselem, *nextfselem;
	const char *fromsnap;
	char newname[ZFS_MAX_DATASET_NAME_LEN];
	char guidname[32];
	int error;
	boolean_t needagain, progress, recursive;
	const char *s1, *s2;

	fromsnap = fnvlist_lookup_string(stream_nv, "fromsnap");

	recursive = (nvlist_lookup_boolean(stream_nv, "not_recursive") ==
	    ENOENT);

	if (flags->dryrun)
		return (0);

again:
	needagain = progress = B_FALSE;

	deleted = fnvlist_alloc();

	if ((error = gather_nvlist(hdl, tofs, fromsnap, NULL,
	    recursive, B_TRUE, B_FALSE, recursive, B_FALSE, B_FALSE, B_FALSE,
	    B_FALSE, B_TRUE, &local_nv, &local_avl)) != 0)
		return (error);

	/*
	 * Process deletes and renames
	 */
	for (fselem = nvlist_next_nvpair(local_nv, NULL);
	    fselem; fselem = nextfselem) {
		nvlist_t *nvfs, *snaps;
		nvlist_t *stream_nvfs = NULL;
		nvpair_t *snapelem, *nextsnapelem;
		uint64_t fromguid = 0;
		uint64_t originguid = 0;
		uint64_t stream_originguid = 0;
		uint64_t parent_fromsnap_guid, stream_parent_fromsnap_guid;
		const char *fsname, *stream_fsname;

		nextfselem = nvlist_next_nvpair(local_nv, fselem);

		nvfs = fnvpair_value_nvlist(fselem);
		snaps = fnvlist_lookup_nvlist(nvfs, "snaps");
		fsname = fnvlist_lookup_string(nvfs, "name");
		parent_fromsnap_guid = fnvlist_lookup_uint64(nvfs,
		    "parentfromsnap");
		(void) nvlist_lookup_uint64(nvfs, "origin", &originguid);

		/*
		 * First find the stream's fs, so we can check for
		 * a different origin (due to "zfs promote")
		 */
		for (snapelem = nvlist_next_nvpair(snaps, NULL);
		    snapelem; snapelem = nvlist_next_nvpair(snaps, snapelem)) {
			uint64_t thisguid;

			thisguid = fnvpair_value_uint64(snapelem);
			stream_nvfs = fsavl_find(stream_avl, thisguid, NULL);

			if (stream_nvfs != NULL)
				break;
		}

		/* check for promote */
		(void) nvlist_lookup_uint64(stream_nvfs, "origin",
		    &stream_originguid);
		if (stream_nvfs && originguid != stream_originguid) {
			switch (created_before(hdl, local_avl,
			    stream_originguid, originguid)) {
			case 1: {
				/* promote it! */
				nvlist_t *origin_nvfs;
				const char *origin_fsname;

				origin_nvfs = fsavl_find(local_avl, originguid,
				    NULL);
				origin_fsname = fnvlist_lookup_string(
				    origin_nvfs, "name");
				error = recv_promote(hdl, fsname, origin_fsname,
				    flags);
				if (error == 0)
					progress = B_TRUE;
				break;
			}
			default:
				break;
			case -1:
				fsavl_destroy(local_avl);
				fnvlist_free(local_nv);
				return (-1);
			}
			/*
			 * We had/have the wrong origin, therefore our
			 * list of snapshots is wrong.  Need to handle
			 * them on the next pass.
			 */
			needagain = B_TRUE;
			continue;
		}

		for (snapelem = nvlist_next_nvpair(snaps, NULL);
		    snapelem; snapelem = nextsnapelem) {
			uint64_t thisguid;
			const char *stream_snapname;
			nvlist_t *found, *props;

			nextsnapelem = nvlist_next_nvpair(snaps, snapelem);

			thisguid = fnvpair_value_uint64(snapelem);
			found = fsavl_find(stream_avl, thisguid,
			    &stream_snapname);

			/* check for delete */
			if (found == NULL) {
				char name[ZFS_MAX_DATASET_NAME_LEN];

				if (!flags->force)
					continue;

				(void) snprintf(name, sizeof (name), "%s@%s",
				    fsname, nvpair_name(snapelem));

				error = recv_destroy(hdl, name,
				    strlen(fsname)+1, newname, flags);
				if (error)
					needagain = B_TRUE;
				else
					progress = B_TRUE;
				sprintf(guidname, "%llu",
				    (u_longlong_t)thisguid);
				nvlist_add_boolean(deleted, guidname);
				continue;
			}

			stream_nvfs = found;

			if (0 == nvlist_lookup_nvlist(stream_nvfs, "snapprops",
			    &props) && 0 == nvlist_lookup_nvlist(props,
			    stream_snapname, &props)) {
				zfs_cmd_t zc = {"\0"};

				zc.zc_cookie = B_TRUE; /* received */
				(void) snprintf(zc.zc_name, sizeof (zc.zc_name),
				    "%s@%s", fsname, nvpair_name(snapelem));
				zcmd_write_src_nvlist(hdl, &zc, props);
				(void) zfs_ioctl(hdl,
				    ZFS_IOC_SET_PROP, &zc);
				zcmd_free_nvlists(&zc);
			}

			/* check for different snapname */
			if (strcmp(nvpair_name(snapelem),
			    stream_snapname) != 0) {
				char name[ZFS_MAX_DATASET_NAME_LEN];
				char tryname[ZFS_MAX_DATASET_NAME_LEN];

				(void) snprintf(name, sizeof (name), "%s@%s",
				    fsname, nvpair_name(snapelem));
				(void) snprintf(tryname, sizeof (name), "%s@%s",
				    fsname, stream_snapname);

				error = recv_rename(hdl, name, tryname,
				    strlen(fsname)+1, newname, flags);
				if (error)
					needagain = B_TRUE;
				else
					progress = B_TRUE;
			}

			if (strcmp(stream_snapname, fromsnap) == 0)
				fromguid = thisguid;
		}

		/* check for delete */
		if (stream_nvfs == NULL) {
			if (!flags->force)
				continue;

			error = recv_destroy(hdl, fsname, strlen(tofs)+1,
			    newname, flags);
			if (error)
				needagain = B_TRUE;
			else
				progress = B_TRUE;
			sprintf(guidname, "%llu",
			    (u_longlong_t)parent_fromsnap_guid);
			nvlist_add_boolean(deleted, guidname);
			continue;
		}

		if (fromguid == 0) {
			if (flags->verbose) {
				(void) printf("local fs %s does not have "
				    "fromsnap (%s in stream); must have "
				    "been deleted locally; ignoring\n",
				    fsname, fromsnap);
			}
			continue;
		}

		stream_fsname = fnvlist_lookup_string(stream_nvfs, "name");
		stream_parent_fromsnap_guid = fnvlist_lookup_uint64(
		    stream_nvfs, "parentfromsnap");

		s1 = strrchr(fsname, '/');
		s2 = strrchr(stream_fsname, '/');

		/*
		 * Check if we're going to rename based on parent guid change
		 * and the current parent guid was also deleted. If it was then
		 * rename will fail and is likely unneeded, so avoid this and
		 * force an early retry to determine the new
		 * parent_fromsnap_guid.
		 */
		if (stream_parent_fromsnap_guid != 0 &&
		    parent_fromsnap_guid != 0 &&
		    stream_parent_fromsnap_guid != parent_fromsnap_guid) {
			sprintf(guidname, "%llu",
			    (u_longlong_t)parent_fromsnap_guid);
			if (nvlist_exists(deleted, guidname)) {
				progress = B_TRUE;
				needagain = B_TRUE;
				goto doagain;
			}
		}

		/*
		 * Check for rename. If the exact receive path is specified, it
		 * does not count as a rename, but we still need to check the
		 * datasets beneath it.
		 */
		if ((stream_parent_fromsnap_guid != 0 &&
		    parent_fromsnap_guid != 0 &&
		    stream_parent_fromsnap_guid != parent_fromsnap_guid) ||
		    ((flags->isprefix || strcmp(tofs, fsname) != 0) &&
		    (s1 != NULL) && (s2 != NULL) && strcmp(s1, s2) != 0)) {
			nvlist_t *parent;
			char tryname[ZFS_MAX_DATASET_NAME_LEN];

			parent = fsavl_find(local_avl,
			    stream_parent_fromsnap_guid, NULL);
			/*
			 * NB: parent might not be found if we used the
			 * tosnap for stream_parent_fromsnap_guid,
			 * because the parent is a newly-created fs;
			 * we'll be able to rename it after we recv the
			 * new fs.
			 */
			if (parent != NULL) {
				const char *pname;

				pname = fnvlist_lookup_string(parent, "name");
				(void) snprintf(tryname, sizeof (tryname),
				    "%s%s", pname, strrchr(stream_fsname, '/'));
			} else {
				tryname[0] = '\0';
				if (flags->verbose) {
					(void) printf("local fs %s new parent "
					    "not found\n", fsname);
				}
			}

			newname[0] = '\0';

			error = recv_rename(hdl, fsname, tryname,
			    strlen(tofs)+1, newname, flags);

			if (renamed != NULL && newname[0] != '\0') {
				fnvlist_add_boolean(renamed, newname);
			}

			if (error)
				needagain = B_TRUE;
			else
				progress = B_TRUE;
		}
	}

doagain:
	fsavl_destroy(local_avl);
	fnvlist_free(local_nv);
	fnvlist_free(deleted);

	if (needagain && progress) {
		/* do another pass to fix up temporary names */
		if (flags->verbose)
			(void) printf("another pass:\n");
		goto again;
	}

	return (needagain || error != 0);
}

static int
zfs_receive_package(libzfs_handle_t *hdl, int fd, const char *destname,
    recvflags_t *flags, dmu_replay_record_t *drr, zio_cksum_t *zc,
    char **top_zfs, nvlist_t *cmdprops)
{
	nvlist_t *stream_nv = NULL;
	avl_tree_t *stream_avl = NULL;
	const char *fromsnap = NULL;
	const char *sendsnap = NULL;
	char *cp;
	char tofs[ZFS_MAX_DATASET_NAME_LEN];
	char sendfs[ZFS_MAX_DATASET_NAME_LEN];
	char errbuf[ERRBUFLEN];
	dmu_replay_record_t drre;
	int error;
	boolean_t anyerr = B_FALSE;
	boolean_t softerr = B_FALSE;
	boolean_t recursive, raw;

	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "cannot receive"));

	assert(drr->drr_type == DRR_BEGIN);
	assert(drr->drr_u.drr_begin.drr_magic == DMU_BACKUP_MAGIC);
	assert(DMU_GET_STREAM_HDRTYPE(drr->drr_u.drr_begin.drr_versioninfo) ==
	    DMU_COMPOUNDSTREAM);

	/*
	 * Read in the nvlist from the stream.
	 */
	if (drr->drr_payloadlen != 0) {
		error = recv_read_nvlist(hdl, fd, drr->drr_payloadlen,
		    &stream_nv, flags->byteswap, zc);
		if (error) {
			error = zfs_error(hdl, EZFS_BADSTREAM, errbuf);
			goto out;
		}
	}

	recursive = (nvlist_lookup_boolean(stream_nv, "not_recursive") ==
	    ENOENT);
	raw = (nvlist_lookup_boolean(stream_nv, "raw") == 0);

	if (recursive && strchr(destname, '@')) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "cannot specify snapshot name for multi-snapshot stream"));
		error = zfs_error(hdl, EZFS_BADSTREAM, errbuf);
		goto out;
	}

	/*
	 * Read in the end record and verify checksum.
	 */
	if (0 != (error = recv_read(hdl, fd, &drre, sizeof (drre),
	    flags->byteswap, NULL)))
		goto out;
	if (flags->byteswap) {
		drre.drr_type = BSWAP_32(drre.drr_type);
		drre.drr_u.drr_end.drr_checksum.zc_word[0] =
		    BSWAP_64(drre.drr_u.drr_end.drr_checksum.zc_word[0]);
		drre.drr_u.drr_end.drr_checksum.zc_word[1] =
		    BSWAP_64(drre.drr_u.drr_end.drr_checksum.zc_word[1]);
		drre.drr_u.drr_end.drr_checksum.zc_word[2] =
		    BSWAP_64(drre.drr_u.drr_end.drr_checksum.zc_word[2]);
		drre.drr_u.drr_end.drr_checksum.zc_word[3] =
		    BSWAP_64(drre.drr_u.drr_end.drr_checksum.zc_word[3]);
	}
	if (drre.drr_type != DRR_END) {
		error = zfs_error(hdl, EZFS_BADSTREAM, errbuf);
		goto out;
	}
	if (!ZIO_CHECKSUM_EQUAL(drre.drr_u.drr_end.drr_checksum, *zc)) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "incorrect header checksum"));
		error = zfs_error(hdl, EZFS_BADSTREAM, errbuf);
		goto out;
	}

	(void) nvlist_lookup_string(stream_nv, "fromsnap", &fromsnap);

	if (drr->drr_payloadlen != 0) {
		nvlist_t *stream_fss;

		stream_fss = fnvlist_lookup_nvlist(stream_nv, "fss");
		if ((stream_avl = fsavl_create(stream_fss)) == NULL) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "couldn't allocate avl tree"));
			error = zfs_error(hdl, EZFS_NOMEM, errbuf);
			goto out;
		}

		if (fromsnap != NULL && recursive) {
			nvlist_t *renamed = NULL;
			nvpair_t *pair = NULL;

			(void) strlcpy(tofs, destname, sizeof (tofs));
			if (flags->isprefix) {
				struct drr_begin *drrb = &drr->drr_u.drr_begin;
				int i;

				if (flags->istail) {
					cp = strrchr(drrb->drr_toname, '/');
					if (cp == NULL) {
						(void) strlcat(tofs, "/",
						    sizeof (tofs));
						i = 0;
					} else {
						i = (cp - drrb->drr_toname);
					}
				} else {
					i = strcspn(drrb->drr_toname, "/@");
				}
				/* zfs_receive_one() will create_parents() */
				(void) strlcat(tofs, &drrb->drr_toname[i],
				    sizeof (tofs));
				*strchr(tofs, '@') = '\0';
			}

			if (!flags->dryrun && !flags->nomount) {
				renamed = fnvlist_alloc();
			}

			softerr = recv_incremental_replication(hdl, tofs, flags,
			    stream_nv, stream_avl, renamed);

			/* Unmount renamed filesystems before receiving. */
			while ((pair = nvlist_next_nvpair(renamed,
			    pair)) != NULL) {
				zfs_handle_t *zhp;
				prop_changelist_t *clp = NULL;

				zhp = zfs_open(hdl, nvpair_name(pair),
				    ZFS_TYPE_FILESYSTEM);
				if (zhp != NULL) {
					clp = changelist_gather(zhp,
					    ZFS_PROP_MOUNTPOINT, 0,
					    flags->forceunmount ? MS_FORCE : 0);
					zfs_close(zhp);
					if (clp != NULL) {
						softerr |=
						    changelist_prefix(clp);
						changelist_free(clp);
					}
				}
			}

			fnvlist_free(renamed);
		}
	}

	/*
	 * Get the fs specified by the first path in the stream (the top level
	 * specified by 'zfs send') and pass it to each invocation of
	 * zfs_receive_one().
	 */
	(void) strlcpy(sendfs, drr->drr_u.drr_begin.drr_toname,
	    sizeof (sendfs));
	if ((cp = strchr(sendfs, '@')) != NULL) {
		*cp = '\0';
		/*
		 * Find the "sendsnap", the final snapshot in a replication
		 * stream.  zfs_receive_one() handles certain errors
		 * differently, depending on if the contained stream is the
		 * last one or not.
		 */
		sendsnap = (cp + 1);
	}

	/* Finally, receive each contained stream */
	do {
		/*
		 * we should figure out if it has a recoverable
		 * error, in which case do a recv_skip() and drive on.
		 * Note, if we fail due to already having this guid,
		 * zfs_receive_one() will take care of it (ie,
		 * recv_skip() and return 0).
		 */
		error = zfs_receive_impl(hdl, destname, NULL, flags, fd,
		    sendfs, stream_nv, stream_avl, top_zfs, sendsnap, cmdprops);
		if (error == ENODATA) {
			error = 0;
			break;
		}
		anyerr |= error;
	} while (error == 0);

	if (drr->drr_payloadlen != 0 && recursive && fromsnap != NULL) {
		/*
		 * Now that we have the fs's they sent us, try the
		 * renames again.
		 */
		softerr = recv_incremental_replication(hdl, tofs, flags,
		    stream_nv, stream_avl, NULL);
	}

	if (raw && softerr == 0 && *top_zfs != NULL) {
		softerr = recv_fix_encryption_hierarchy(hdl, *top_zfs,
		    stream_nv);
	}

out:
	fsavl_destroy(stream_avl);
	fnvlist_free(stream_nv);
	if (softerr)
		error = -2;
	if (anyerr)
		error = -1;
	return (error);
}

static void
trunc_prop_errs(int truncated)
{
	ASSERT(truncated != 0);

	if (truncated == 1)
		(void) fprintf(stderr, dgettext(TEXT_DOMAIN,
		    "1 more property could not be set\n"));
	else
		(void) fprintf(stderr, dgettext(TEXT_DOMAIN,
		    "%d more properties could not be set\n"), truncated);
}

static int
recv_skip(libzfs_handle_t *hdl, int fd, boolean_t byteswap)
{
	dmu_replay_record_t *drr;
	void *buf = zfs_alloc(hdl, SPA_MAXBLOCKSIZE);
	uint64_t payload_size;
	char errbuf[ERRBUFLEN];

	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "cannot receive"));

	/* XXX would be great to use lseek if possible... */
	drr = buf;

	while (recv_read(hdl, fd, drr, sizeof (dmu_replay_record_t),
	    byteswap, NULL) == 0) {
		if (byteswap)
			drr->drr_type = BSWAP_32(drr->drr_type);

		switch (drr->drr_type) {
		case DRR_BEGIN:
			if (drr->drr_payloadlen != 0) {
				(void) recv_read(hdl, fd, buf,
				    drr->drr_payloadlen, B_FALSE, NULL);
			}
			break;

		case DRR_END:
			free(buf);
			return (0);

		case DRR_OBJECT:
			if (byteswap) {
				drr->drr_u.drr_object.drr_bonuslen =
				    BSWAP_32(drr->drr_u.drr_object.
				    drr_bonuslen);
				drr->drr_u.drr_object.drr_raw_bonuslen =
				    BSWAP_32(drr->drr_u.drr_object.
				    drr_raw_bonuslen);
			}

			payload_size =
			    DRR_OBJECT_PAYLOAD_SIZE(&drr->drr_u.drr_object);
			(void) recv_read(hdl, fd, buf, payload_size,
			    B_FALSE, NULL);
			break;

		case DRR_WRITE:
			if (byteswap) {
				drr->drr_u.drr_write.drr_logical_size =
				    BSWAP_64(
				    drr->drr_u.drr_write.drr_logical_size);
				drr->drr_u.drr_write.drr_compressed_size =
				    BSWAP_64(
				    drr->drr_u.drr_write.drr_compressed_size);
			}
			payload_size =
			    DRR_WRITE_PAYLOAD_SIZE(&drr->drr_u.drr_write);
			assert(payload_size <= SPA_MAXBLOCKSIZE);
			(void) recv_read(hdl, fd, buf,
			    payload_size, B_FALSE, NULL);
			break;
		case DRR_SPILL:
			if (byteswap) {
				drr->drr_u.drr_spill.drr_length =
				    BSWAP_64(drr->drr_u.drr_spill.drr_length);
				drr->drr_u.drr_spill.drr_compressed_size =
				    BSWAP_64(drr->drr_u.drr_spill.
				    drr_compressed_size);
			}

			payload_size =
			    DRR_SPILL_PAYLOAD_SIZE(&drr->drr_u.drr_spill);
			(void) recv_read(hdl, fd, buf, payload_size,
			    B_FALSE, NULL);
			break;
		case DRR_WRITE_EMBEDDED:
			if (byteswap) {
				drr->drr_u.drr_write_embedded.drr_psize =
				    BSWAP_32(drr->drr_u.drr_write_embedded.
				    drr_psize);
			}
			(void) recv_read(hdl, fd, buf,
			    P2ROUNDUP(drr->drr_u.drr_write_embedded.drr_psize,
			    8), B_FALSE, NULL);
			break;
		case DRR_OBJECT_RANGE:
		case DRR_WRITE_BYREF:
		case DRR_FREEOBJECTS:
		case DRR_FREE:
			break;

		default:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "invalid record type"));
			free(buf);
			return (zfs_error(hdl, EZFS_BADSTREAM, errbuf));
		}
	}

	free(buf);
	return (-1);
}

static void
recv_ecksum_set_aux(libzfs_handle_t *hdl, const char *target_snap,
    boolean_t resumable, boolean_t checksum)
{
	char target_fs[ZFS_MAX_DATASET_NAME_LEN];

	zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, (checksum ?
	    "checksum mismatch" : "incomplete stream")));

	if (!resumable)
		return;
	(void) strlcpy(target_fs, target_snap, sizeof (target_fs));
	*strchr(target_fs, '@') = '\0';
	zfs_handle_t *zhp = zfs_open(hdl, target_fs,
	    ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME);
	if (zhp == NULL)
		return;

	char token_buf[ZFS_MAXPROPLEN];
	int error = zfs_prop_get(zhp, ZFS_PROP_RECEIVE_RESUME_TOKEN,
	    token_buf, sizeof (token_buf),
	    NULL, NULL, 0, B_TRUE);
	if (error == 0) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "checksum mismatch or incomplete stream.\n"
		    "Partially received snapshot is saved.\n"
		    "A resuming stream can be generated on the sending "
		    "system by running:\n"
		    "    zfs send -t %s"),
		    token_buf);
	}
	zfs_close(zhp);
}

/*
 * Prepare a new nvlist of properties that are to override (-o) or be excluded
 * (-x) from the received dataset
 * recvprops: received properties from the send stream
 * cmdprops: raw input properties from command line
 * origprops: properties, both locally-set and received, currently set on the
 *            target dataset if it exists, NULL otherwise.
 * oxprops: valid output override (-o) and excluded (-x) properties
 */
static int
zfs_setup_cmdline_props(libzfs_handle_t *hdl, zfs_type_t type,
    char *fsname, boolean_t zoned, boolean_t recursive, boolean_t newfs,
    boolean_t raw, boolean_t toplevel, nvlist_t *recvprops, nvlist_t *cmdprops,
    nvlist_t *origprops, nvlist_t **oxprops, uint8_t **wkeydata_out,
    uint_t *wkeylen_out, const char *errbuf)
{
	nvpair_t *nvp;
	nvlist_t *oprops, *voprops;
	zfs_handle_t *zhp = NULL;
	zpool_handle_t *zpool_hdl = NULL;
	char *cp;
	int ret = 0;
	char namebuf[ZFS_MAX_DATASET_NAME_LEN];

	if (nvlist_empty(cmdprops))
		return (0); /* No properties to override or exclude */

	*oxprops = fnvlist_alloc();
	oprops = fnvlist_alloc();

	strlcpy(namebuf, fsname, ZFS_MAX_DATASET_NAME_LEN);

	/*
	 * Get our dataset handle. The target dataset may not exist yet.
	 */
	if (zfs_dataset_exists(hdl, namebuf, ZFS_TYPE_DATASET)) {
		zhp = zfs_open(hdl, namebuf, ZFS_TYPE_DATASET);
		if (zhp == NULL) {
			ret = -1;
			goto error;
		}
	}

	/* open the zpool handle */
	cp = strchr(namebuf, '/');
	if (cp != NULL)
		*cp = '\0';
	zpool_hdl = zpool_open(hdl, namebuf);
	if (zpool_hdl == NULL) {
		ret = -1;
		goto error;
	}

	/* restore namebuf to match fsname for later use */
	if (cp != NULL)
		*cp = '/';

	/*
	 * first iteration: process excluded (-x) properties now and gather
	 * added (-o) properties to be later processed by zfs_valid_proplist()
	 */
	nvp = NULL;
	while ((nvp = nvlist_next_nvpair(cmdprops, nvp)) != NULL) {
		const char *name = nvpair_name(nvp);
		zfs_prop_t prop = zfs_name_to_prop(name);

		/*
		 * It turns out, if we don't normalize "aliased" names
		 * e.g. compress= against the "real" names (e.g. compression)
		 * here, then setting/excluding them does not work as
		 * intended.
		 *
		 * But since user-defined properties wouldn't have a valid
		 * mapping here, we do this conditional dance.
		 */
		const char *newname = name;
		if (prop >= ZFS_PROP_TYPE)
			newname = zfs_prop_to_name(prop);

		/* "origin" is processed separately, don't handle it here */
		if (prop == ZFS_PROP_ORIGIN)
			continue;

		/* raw streams can't override encryption properties */
		if ((zfs_prop_encryption_key_param(prop) ||
		    prop == ZFS_PROP_ENCRYPTION) && raw) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "encryption property '%s' cannot "
			    "be set or excluded for raw streams."), name);
			ret = zfs_error(hdl, EZFS_BADPROP, errbuf);
			goto error;
		}

		/*
		 * For plain replicated send, we can ignore encryption
		 * properties other than first stream
		 */
		if ((zfs_prop_encryption_key_param(prop) || prop ==
		    ZFS_PROP_ENCRYPTION) && !newfs && recursive && !raw) {
			continue;
		}

		/* incremental streams can only exclude encryption properties */
		if ((zfs_prop_encryption_key_param(prop) ||
		    prop == ZFS_PROP_ENCRYPTION) && !newfs &&
		    nvpair_type(nvp) != DATA_TYPE_BOOLEAN) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "encryption property '%s' cannot "
			    "be set for incremental streams."), name);
			ret = zfs_error(hdl, EZFS_BADPROP, errbuf);
			goto error;
		}

		switch (nvpair_type(nvp)) {
		case DATA_TYPE_BOOLEAN: /* -x property */
			/*
			 * DATA_TYPE_BOOLEAN is the way we're asked to "exclude"
			 * a property: this is done by forcing an explicit
			 * inherit on the destination so the effective value is
			 * not the one we received from the send stream.
			 */
			if (!zfs_prop_valid_for_type(prop, type, B_FALSE) &&
			    !zfs_prop_user(name)) {
				(void) fprintf(stderr, dgettext(TEXT_DOMAIN,
				    "Warning: %s: property '%s' does not "
				    "apply to datasets of this type\n"),
				    fsname, name);
				continue;
			}
			/*
			 * We do this only if the property is not already
			 * locally-set, in which case its value will take
			 * priority over the received anyway.
			 */
			if (nvlist_exists(origprops, newname)) {
				nvlist_t *attrs;
				const char *source = NULL;

				attrs = fnvlist_lookup_nvlist(origprops,
				    newname);
				if (nvlist_lookup_string(attrs,
				    ZPROP_SOURCE, &source) == 0 &&
				    strcmp(source, ZPROP_SOURCE_VAL_RECVD) != 0)
					continue;
			}
			/*
			 * We can't force an explicit inherit on non-inheritable
			 * properties: if we're asked to exclude this kind of
			 * values we remove them from "recvprops" input nvlist.
			 */
			if (!zfs_prop_user(name) && /* can be inherited too */
			    !zfs_prop_inheritable(prop) &&
			    nvlist_exists(recvprops, newname))
				fnvlist_remove(recvprops, newname);
			else
				fnvlist_add_boolean(*oxprops, newname);
			break;
		case DATA_TYPE_STRING: /* -o property=value */
			/*
			 * we're trying to override a property that does not
			 * make sense for this type of dataset, but we don't
			 * want to fail if the receive is recursive: this comes
			 * in handy when the send stream contains, for
			 * instance, a child ZVOL and we're trying to receive
			 * it with "-o atime=on"
			 */
			if (!zfs_prop_valid_for_type(prop, type, B_FALSE) &&
			    !zfs_prop_user(name)) {
				if (recursive)
					continue;
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "property '%s' does not apply to datasets "
				    "of this type"), name);
				ret = zfs_error(hdl, EZFS_BADPROP, errbuf);
				goto error;
			}
			fnvlist_add_string(oprops, newname,
			    fnvpair_value_string(nvp));
			break;
		default:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "property '%s' must be a string or boolean"), name);
			ret = zfs_error(hdl, EZFS_BADPROP, errbuf);
			goto error;
		}
	}

	if (toplevel) {
		/* convert override strings properties to native */
		if ((voprops = zfs_valid_proplist(hdl, ZFS_TYPE_DATASET,
		    oprops, zoned, zhp, zpool_hdl, B_FALSE, errbuf)) == NULL) {
			ret = zfs_error(hdl, EZFS_BADPROP, errbuf);
			goto error;
		}

		/*
		 * zfs_crypto_create() requires the parent name. Get it
		 * by truncating the fsname copy stored in namebuf.
		 */
		cp = strrchr(namebuf, '/');
		if (cp != NULL)
			*cp = '\0';

		if (!raw && !(!newfs && recursive) &&
		    zfs_crypto_create(hdl, namebuf, voprops, NULL,
		    B_FALSE, wkeydata_out, wkeylen_out) != 0) {
			fnvlist_free(voprops);
			ret = zfs_error(hdl, EZFS_CRYPTOFAILED, errbuf);
			goto error;
		}

		/* second pass: process "-o" properties */
		fnvlist_merge(*oxprops, voprops);
		fnvlist_free(voprops);
	} else {
		/* override props on child dataset are inherited */
		nvp = NULL;
		while ((nvp = nvlist_next_nvpair(oprops, nvp)) != NULL) {
			const char *name = nvpair_name(nvp);
			fnvlist_add_boolean(*oxprops, name);
		}
	}

error:
	if (zhp != NULL)
		zfs_close(zhp);
	if (zpool_hdl != NULL)
		zpool_close(zpool_hdl);
	fnvlist_free(oprops);
	return (ret);
}

/*
 * Restores a backup of tosnap from the file descriptor specified by infd.
 */
static int
zfs_receive_one(libzfs_handle_t *hdl, int infd, const char *tosnap,
    const char *originsnap, recvflags_t *flags, dmu_replay_record_t *drr,
    dmu_replay_record_t *drr_noswap, const char *sendfs, nvlist_t *stream_nv,
    avl_tree_t *stream_avl, char **top_zfs,
    const char *finalsnap, nvlist_t *cmdprops)
{
	struct timespec begin_time;
	int ioctl_err, ioctl_errno, err;
	char *cp;
	struct drr_begin *drrb = &drr->drr_u.drr_begin;
	char errbuf[ERRBUFLEN];
	const char *chopprefix;
	boolean_t newfs = B_FALSE;
	boolean_t stream_wantsnewfs, stream_resumingnewfs;
	boolean_t newprops = B_FALSE;
	uint64_t read_bytes = 0;
	uint64_t errflags = 0;
	uint64_t parent_snapguid = 0;
	prop_changelist_t *clp = NULL;
	nvlist_t *snapprops_nvlist = NULL;
	nvlist_t *snapholds_nvlist = NULL;
	zprop_errflags_t prop_errflags;
	nvlist_t *prop_errors = NULL;
	boolean_t recursive;
	const char *snapname = NULL;
	char destsnap[MAXPATHLEN * 2];
	char origin[MAXNAMELEN] = {0};
	char name[MAXPATHLEN];
	char tmp_keylocation[MAXNAMELEN] = {0};
	nvlist_t *rcvprops = NULL; /* props received from the send stream */
	nvlist_t *oxprops = NULL; /* override (-o) and exclude (-x) props */
	nvlist_t *origprops = NULL; /* original props (if destination exists) */
	zfs_type_t type = ZFS_TYPE_INVALID;
	boolean_t toplevel = B_FALSE;
	boolean_t zoned = B_FALSE;
	boolean_t hastoken = B_FALSE;
	boolean_t redacted;
	uint8_t *wkeydata = NULL;
	uint_t wkeylen = 0;

#ifndef CLOCK_MONOTONIC_RAW
#define	CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC
#endif
	clock_gettime(CLOCK_MONOTONIC_RAW, &begin_time);

	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "cannot receive"));

	recursive = (nvlist_lookup_boolean(stream_nv, "not_recursive") ==
	    ENOENT);

	/* Did the user request holds be skipped via zfs recv -k? */
	boolean_t holds = flags->holds && !flags->skipholds;

	if (stream_avl != NULL) {
		const char *keylocation = NULL;
		nvlist_t *lookup = NULL;
		nvlist_t *fs = fsavl_find(stream_avl, drrb->drr_toguid,
		    &snapname);

		(void) nvlist_lookup_uint64(fs, "parentfromsnap",
		    &parent_snapguid);
		err = nvlist_lookup_nvlist(fs, "props", &rcvprops);
		if (err) {
			rcvprops = fnvlist_alloc();
			newprops = B_TRUE;
		}

		/*
		 * The keylocation property may only be set on encryption roots,
		 * but this dataset might not become an encryption root until
		 * recv_fix_encryption_hierarchy() is called. That function
		 * will fixup the keylocation anyway, so we temporarily unset
		 * the keylocation for now to avoid any errors from the receive
		 * ioctl.
		 */
		err = nvlist_lookup_string(rcvprops,
		    zfs_prop_to_name(ZFS_PROP_KEYLOCATION), &keylocation);
		if (err == 0) {
			strlcpy(tmp_keylocation, keylocation, MAXNAMELEN);
			(void) nvlist_remove_all(rcvprops,
			    zfs_prop_to_name(ZFS_PROP_KEYLOCATION));
		}

		if (flags->canmountoff) {
			fnvlist_add_uint64(rcvprops,
			    zfs_prop_to_name(ZFS_PROP_CANMOUNT), 0);
		} else if (newprops) {	/* nothing in rcvprops, eliminate it */
			fnvlist_free(rcvprops);
			rcvprops = NULL;
			newprops = B_FALSE;
		}
		if (0 == nvlist_lookup_nvlist(fs, "snapprops", &lookup)) {
			snapprops_nvlist = fnvlist_lookup_nvlist(lookup,
			    snapname);
		}
		if (holds) {
			if (0 == nvlist_lookup_nvlist(fs, "snapholds",
			    &lookup)) {
				snapholds_nvlist = fnvlist_lookup_nvlist(
				    lookup, snapname);
			}
		}
	}

	cp = NULL;

	/*
	 * Determine how much of the snapshot name stored in the stream
	 * we are going to tack on to the name they specified on the
	 * command line, and how much we are going to chop off.
	 *
	 * If they specified a snapshot, chop the entire name stored in
	 * the stream.
	 */
	if (flags->istail) {
		/*
		 * A filesystem was specified with -e. We want to tack on only
		 * the tail of the sent snapshot path.
		 */
		if (strchr(tosnap, '@')) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, "invalid "
			    "argument - snapshot not allowed with -e"));
			err = zfs_error(hdl, EZFS_INVALIDNAME, errbuf);
			goto out;
		}

		chopprefix = strrchr(sendfs, '/');

		if (chopprefix == NULL) {
			/*
			 * The tail is the poolname, so we need to
			 * prepend a path separator.
			 */
			int len = strlen(drrb->drr_toname);
			cp = umem_alloc(len + 2, UMEM_NOFAIL);
			cp[0] = '/';
			(void) strcpy(&cp[1], drrb->drr_toname);
			chopprefix = cp;
		} else {
			chopprefix = drrb->drr_toname + (chopprefix - sendfs);
		}
	} else if (flags->isprefix) {
		/*
		 * A filesystem was specified with -d. We want to tack on
		 * everything but the first element of the sent snapshot path
		 * (all but the pool name).
		 */
		if (strchr(tosnap, '@')) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, "invalid "
			    "argument - snapshot not allowed with -d"));
			err = zfs_error(hdl, EZFS_INVALIDNAME, errbuf);
			goto out;
		}

		chopprefix = strchr(drrb->drr_toname, '/');
		if (chopprefix == NULL)
			chopprefix = strchr(drrb->drr_toname, '@');
	} else if (strchr(tosnap, '@') == NULL) {
		/*
		 * If a filesystem was specified without -d or -e, we want to
		 * tack on everything after the fs specified by 'zfs send'.
		 */
		chopprefix = drrb->drr_toname + strlen(sendfs);
	} else {
		/* A snapshot was specified as an exact path (no -d or -e). */
		if (recursive) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "cannot specify snapshot name for multi-snapshot "
			    "stream"));
			err = zfs_error(hdl, EZFS_BADSTREAM, errbuf);
			goto out;
		}
		chopprefix = drrb->drr_toname + strlen(drrb->drr_toname);
	}

	ASSERT(strstr(drrb->drr_toname, sendfs) == drrb->drr_toname);
	ASSERT(chopprefix > drrb->drr_toname || strchr(sendfs, '/') == NULL);
	ASSERT(chopprefix <= drrb->drr_toname + strlen(drrb->drr_toname) ||
	    strchr(sendfs, '/') == NULL);
	ASSERT(chopprefix[0] == '/' || chopprefix[0] == '@' ||
	    chopprefix[0] == '\0');

	/*
	 * Determine name of destination snapshot.
	 */
	(void) strlcpy(destsnap, tosnap, sizeof (destsnap));
	(void) strlcat(destsnap, chopprefix, sizeof (destsnap));
	if (cp != NULL)
		umem_free(cp, strlen(cp) + 1);
	if (!zfs_name_valid(destsnap, ZFS_TYPE_SNAPSHOT)) {
		err = zfs_error(hdl, EZFS_INVALIDNAME, errbuf);
		goto out;
	}

	/*
	 * Determine the name of the origin snapshot.
	 */
	if (originsnap) {
		(void) strlcpy(origin, originsnap, sizeof (origin));
		if (flags->verbose)
			(void) printf("using provided clone origin %s\n",
			    origin);
	} else if (drrb->drr_flags & DRR_FLAG_CLONE) {
		if (guid_to_name(hdl, destsnap,
		    drrb->drr_fromguid, B_FALSE, origin) != 0) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "local origin for clone %s does not exist"),
			    destsnap);
			err = zfs_error(hdl, EZFS_NOENT, errbuf);
			goto out;
		}
		if (flags->verbose)
			(void) printf("found clone origin %s\n", origin);
	}

	if ((DMU_GET_FEATUREFLAGS(drrb->drr_versioninfo) &
	    DMU_BACKUP_FEATURE_DEDUP)) {
		(void) fprintf(stderr,
		    gettext("ERROR: \"zfs receive\" no longer supports "
		    "deduplicated send streams.  Use\n"
		    "the \"zstream redup\" command to convert this stream "
		    "to a regular,\n"
		    "non-deduplicated stream.\n"));
		err = zfs_error(hdl, EZFS_NOTSUP, errbuf);
		goto out;
	}

	boolean_t resuming = DMU_GET_FEATUREFLAGS(drrb->drr_versioninfo) &
	    DMU_BACKUP_FEATURE_RESUMING;
	boolean_t raw = DMU_GET_FEATUREFLAGS(drrb->drr_versioninfo) &
	    DMU_BACKUP_FEATURE_RAW;
	boolean_t embedded = DMU_GET_FEATUREFLAGS(drrb->drr_versioninfo) &
	    DMU_BACKUP_FEATURE_EMBED_DATA;
	stream_wantsnewfs = (drrb->drr_fromguid == 0 ||
	    (drrb->drr_flags & DRR_FLAG_CLONE) || originsnap) && !resuming;
	stream_resumingnewfs = (drrb->drr_fromguid == 0 ||
	    (drrb->drr_flags & DRR_FLAG_CLONE) || originsnap) && resuming;

	if (stream_wantsnewfs) {
		/*
		 * if the parent fs does not exist, look for it based on
		 * the parent snap GUID
		 */
		(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
		    "cannot receive new filesystem stream"));

		(void) strlcpy(name, destsnap, sizeof (name));
		cp = strrchr(name, '/');
		if (cp)
			*cp = '\0';
		if (cp &&
		    !zfs_dataset_exists(hdl, name, ZFS_TYPE_DATASET)) {
			char suffix[ZFS_MAX_DATASET_NAME_LEN];
			(void) strlcpy(suffix, strrchr(destsnap, '/'),
			    sizeof (suffix));
			if (guid_to_name(hdl, name, parent_snapguid,
			    B_FALSE, destsnap) == 0) {
				*strchr(destsnap, '@') = '\0';
				(void) strlcat(destsnap, suffix,
				    sizeof (destsnap));
			}
		}
	} else {
		/*
		 * If the fs does not exist, look for it based on the
		 * fromsnap GUID.
		 */
		if (resuming) {
			(void) snprintf(errbuf, sizeof (errbuf),
			    dgettext(TEXT_DOMAIN,
			    "cannot receive resume stream"));
		} else {
			(void) snprintf(errbuf, sizeof (errbuf),
			    dgettext(TEXT_DOMAIN,
			    "cannot receive incremental stream"));
		}

		(void) strlcpy(name, destsnap, sizeof (name));
		*strchr(name, '@') = '\0';

		/*
		 * If the exact receive path was specified and this is the
		 * topmost path in the stream, then if the fs does not exist we
		 * should look no further.
		 */
		if ((flags->isprefix || (*(chopprefix = drrb->drr_toname +
		    strlen(sendfs)) != '\0' && *chopprefix != '@')) &&
		    !zfs_dataset_exists(hdl, name, ZFS_TYPE_DATASET)) {
			char snap[ZFS_MAX_DATASET_NAME_LEN];
			(void) strlcpy(snap, strchr(destsnap, '@'),
			    sizeof (snap));
			if (guid_to_name(hdl, name, drrb->drr_fromguid,
			    B_FALSE, destsnap) == 0) {
				*strchr(destsnap, '@') = '\0';
				(void) strlcat(destsnap, snap,
				    sizeof (destsnap));
			}
		}
	}

	(void) strlcpy(name, destsnap, sizeof (name));
	*strchr(name, '@') = '\0';

	redacted = DMU_GET_FEATUREFLAGS(drrb->drr_versioninfo) &
	    DMU_BACKUP_FEATURE_REDACTED;

	if (flags->heal) {
		if (flags->isprefix || flags->istail || flags->force ||
		    flags->canmountoff || flags->resumable || flags->nomount ||
		    flags->skipholds) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "corrective recv can not be used when combined with"
			    " this flag"));
			err = zfs_error(hdl, EZFS_INVALIDNAME, errbuf);
			goto out;
		}
		uint64_t guid =
		    get_snap_guid(hdl, name, strchr(destsnap, '@') + 1);
		if (guid == 0) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "corrective recv must specify an existing snapshot"
			    " to heal"));
			err = zfs_error(hdl, EZFS_INVALIDNAME, errbuf);
			goto out;
		} else if (guid != drrb->drr_toguid) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "local snapshot doesn't match the snapshot"
			    " in the provided stream"));
			err = zfs_error(hdl, EZFS_WRONG_PARENT, errbuf);
			goto out;
		}
	} else if (zfs_dataset_exists(hdl, name, ZFS_TYPE_DATASET)) {
		zfs_cmd_t zc = {"\0"};
		zfs_handle_t *zhp = NULL;
		boolean_t encrypted;

		(void) strcpy(zc.zc_name, name);

		/*
		 * Destination fs exists.  It must be one of these cases:
		 *  - an incremental send stream
		 *  - the stream specifies a new fs (full stream or clone)
		 *    and they want us to blow away the existing fs (and
		 *    have therefore specified -F and removed any snapshots)
		 *  - we are resuming a failed receive.
		 */
		if (stream_wantsnewfs) {
			boolean_t is_volume = drrb->drr_type == DMU_OST_ZVOL;
			if (!flags->force) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "destination '%s' exists\n"
				    "must specify -F to overwrite it"), name);
				err = zfs_error(hdl, EZFS_EXISTS, errbuf);
				goto out;
			}
			if (zfs_ioctl(hdl, ZFS_IOC_SNAPSHOT_LIST_NEXT,
			    &zc) == 0) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "destination has snapshots (eg. %s)\n"
				    "must destroy them to overwrite it"),
				    zc.zc_name);
				err = zfs_error(hdl, EZFS_EXISTS, errbuf);
				goto out;
			}
			if (is_volume && strrchr(name, '/') == NULL) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "destination %s is the root dataset\n"
				    "cannot overwrite with a ZVOL"),
				    name);
				err = zfs_error(hdl, EZFS_EXISTS, errbuf);
				goto out;
			}
			if (is_volume &&
			    zfs_ioctl(hdl, ZFS_IOC_DATASET_LIST_NEXT,
			    &zc) == 0) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "destination has children (eg. %s)\n"
				    "cannot overwrite with a ZVOL"),
				    zc.zc_name);
				err = zfs_error(hdl, EZFS_WRONG_PARENT, errbuf);
				goto out;
			}
		}

		if ((zhp = zfs_open(hdl, name,
		    ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME)) == NULL) {
			err = -1;
			goto out;
		}

		/*
		 * When receiving full/newfs on existing dataset, then it
		 * should be done with "-F" flag. Its enforced for initial
		 * receive in previous checks in this function.
		 * Similarly, on resuming full/newfs recv on existing dataset,
		 * it should be done with "-F" flag.
		 *
		 * When dataset doesn't exist, then full/newfs recv is done on
		 * newly created dataset and it's marked INCONSISTENT. But
		 * When receiving on existing dataset, recv is first done on
		 * %recv and its marked INCONSISTENT. Existing dataset is not
		 * marked INCONSISTENT.
		 * Resume of full/newfs receive with dataset not INCONSISTENT
		 * indicates that its resuming newfs on existing dataset. So,
		 * enforce "-F" flag in this case.
		 */
		if (stream_resumingnewfs &&
		    !zfs_prop_get_int(zhp, ZFS_PROP_INCONSISTENT) &&
		    !flags->force) {
			zfs_close(zhp);
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "Resuming recv on existing destination '%s'\n"
			    "must specify -F to overwrite it"), name);
			err = zfs_error(hdl, EZFS_RESUME_EXISTS, errbuf);
			goto out;
		}

		if (stream_wantsnewfs &&
		    zhp->zfs_dmustats.dds_origin[0]) {
			zfs_close(zhp);
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "destination '%s' is a clone\n"
			    "must destroy it to overwrite it"), name);
			err = zfs_error(hdl, EZFS_EXISTS, errbuf);
			goto out;
		}

		/*
		 * Raw sends can not be performed as an incremental on top
		 * of existing unencrypted datasets. zfs recv -F can't be
		 * used to blow away an existing encrypted filesystem. This
		 * is because it would require the dsl dir to point to the
		 * new key (or lack of a key) and the old key at the same
		 * time. The -F flag may still be used for deleting
		 * intermediate snapshots that would otherwise prevent the
		 * receive from working.
		 */
		encrypted = zfs_prop_get_int(zhp, ZFS_PROP_ENCRYPTION) !=
		    ZIO_CRYPT_OFF;
		if (!stream_wantsnewfs && !encrypted && raw) {
			zfs_close(zhp);
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "cannot perform raw receive on top of "
			    "existing unencrypted dataset"));
			err = zfs_error(hdl, EZFS_BADRESTORE, errbuf);
			goto out;
		}

		if (stream_wantsnewfs && flags->force &&
		    ((raw && !encrypted) || encrypted)) {
			zfs_close(zhp);
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "zfs receive -F cannot be used to destroy an "
			    "encrypted filesystem or overwrite an "
			    "unencrypted one with an encrypted one"));
			err = zfs_error(hdl, EZFS_BADRESTORE, errbuf);
			goto out;
		}

		if (!flags->dryrun && zhp->zfs_type == ZFS_TYPE_FILESYSTEM &&
		    (stream_wantsnewfs || stream_resumingnewfs)) {
			/* We can't do online recv in this case */
			clp = changelist_gather(zhp, ZFS_PROP_NAME, 0,
			    flags->forceunmount ? MS_FORCE : 0);
			if (clp == NULL) {
				zfs_close(zhp);
				err = -1;
				goto out;
			}
			if (changelist_prefix(clp) != 0) {
				changelist_free(clp);
				zfs_close(zhp);
				err = -1;
				goto out;
			}
		}

		/*
		 * If we are resuming a newfs, set newfs here so that we will
		 * mount it if the recv succeeds this time.  We can tell
		 * that it was a newfs on the first recv because the fs
		 * itself will be inconsistent (if the fs existed when we
		 * did the first recv, we would have received it into
		 * .../%recv).
		 */
		if (resuming && zfs_prop_get_int(zhp, ZFS_PROP_INCONSISTENT))
			newfs = B_TRUE;

		/* we want to know if we're zoned when validating -o|-x props */
		zoned = zfs_prop_get_int(zhp, ZFS_PROP_ZONED);

		/* may need this info later, get it now we have zhp around */
		if (zfs_prop_get(zhp, ZFS_PROP_RECEIVE_RESUME_TOKEN, NULL, 0,
		    NULL, NULL, 0, B_TRUE) == 0)
			hastoken = B_TRUE;

		/* gather existing properties on destination */
		origprops = fnvlist_alloc();
		fnvlist_merge(origprops, zhp->zfs_props);
		fnvlist_merge(origprops, zhp->zfs_user_props);

		zfs_close(zhp);
	} else {
		zfs_handle_t *zhp;

		/*
		 * Destination filesystem does not exist.  Therefore we better
		 * be creating a new filesystem (either from a full backup, or
		 * a clone).  It would therefore be invalid if the user
		 * specified only the pool name (i.e. if the destination name
		 * contained no slash character).
		 */
		cp = strrchr(name, '/');

		if (!stream_wantsnewfs || cp == NULL) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "destination '%s' does not exist"), name);
			err = zfs_error(hdl, EZFS_NOENT, errbuf);
			goto out;
		}

		/*
		 * Trim off the final dataset component so we perform the
		 * recvbackup ioctl to the filesystems's parent.
		 */
		*cp = '\0';

		if (flags->isprefix && !flags->istail && !flags->dryrun &&
		    create_parents(hdl, destsnap, strlen(tosnap)) != 0) {
			err = zfs_error(hdl, EZFS_BADRESTORE, errbuf);
			goto out;
		}

		/* validate parent */
		zhp = zfs_open(hdl, name, ZFS_TYPE_DATASET);
		if (zhp == NULL) {
			err = zfs_error(hdl, EZFS_BADRESTORE, errbuf);
			goto out;
		}
		if (zfs_get_type(zhp) != ZFS_TYPE_FILESYSTEM) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "parent '%s' is not a filesystem"), name);
			err = zfs_error(hdl, EZFS_WRONG_PARENT, errbuf);
			zfs_close(zhp);
			goto out;
		}

		zfs_close(zhp);

		newfs = B_TRUE;
		*cp = '/';
	}

	if (flags->verbose) {
		(void) printf("%s %s%s stream of %s into %s\n",
		    flags->dryrun ? "would receive" : "receiving",
		    flags->heal ? "corrective " : "",
		    drrb->drr_fromguid ? "incremental" : "full",
		    drrb->drr_toname, destsnap);
		(void) fflush(stdout);
	}

	/*
	 * If this is the top-level dataset, record it so we can use it
	 * for recursive operations later.
	 */
	if (top_zfs != NULL &&
	    (*top_zfs == NULL || strcmp(*top_zfs, name) == 0)) {
		toplevel = B_TRUE;
		if (*top_zfs == NULL)
			*top_zfs = zfs_strdup(hdl, name);
	}

	if (drrb->drr_type == DMU_OST_ZVOL) {
		type = ZFS_TYPE_VOLUME;
	} else if (drrb->drr_type == DMU_OST_ZFS) {
		type = ZFS_TYPE_FILESYSTEM;
	} else {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "invalid record type: 0x%d"), drrb->drr_type);
		err = zfs_error(hdl, EZFS_BADSTREAM, errbuf);
		goto out;
	}
	if ((err = zfs_setup_cmdline_props(hdl, type, name, zoned, recursive,
	    stream_wantsnewfs, raw, toplevel, rcvprops, cmdprops, origprops,
	    &oxprops, &wkeydata, &wkeylen, errbuf)) != 0)
		goto out;

	/*
	 * When sending with properties (zfs send -p), the encryption property
	 * is not included because it is a SETONCE property and therefore
	 * treated as read only. However, we are always able to determine its
	 * value because raw sends will include it in the DRR_BDEGIN payload
	 * and non-raw sends with properties are not allowed for encrypted
	 * datasets. Therefore, if this is a non-raw properties stream, we can
	 * infer that the value should be ZIO_CRYPT_OFF and manually add that
	 * to the received properties.
	 */
	if (stream_wantsnewfs && !raw && rcvprops != NULL &&
	    !nvlist_exists(cmdprops, zfs_prop_to_name(ZFS_PROP_ENCRYPTION))) {
		if (oxprops == NULL)
			oxprops = fnvlist_alloc();
		fnvlist_add_uint64(oxprops,
		    zfs_prop_to_name(ZFS_PROP_ENCRYPTION), ZIO_CRYPT_OFF);
	}

	if (flags->dryrun) {
		void *buf = zfs_alloc(hdl, SPA_MAXBLOCKSIZE);

		/*
		 * We have read the DRR_BEGIN record, but we have
		 * not yet read the payload. For non-dryrun sends
		 * this will be done by the kernel, so we must
		 * emulate that here, before attempting to read
		 * more records.
		 */
		err = recv_read(hdl, infd, buf, drr->drr_payloadlen,
		    flags->byteswap, NULL);
		free(buf);
		if (err != 0)
			goto out;

		err = recv_skip(hdl, infd, flags->byteswap);
		goto out;
	}

	if (flags->heal) {
		err = ioctl_err = lzc_receive_with_heal(destsnap, rcvprops,
		    oxprops, wkeydata, wkeylen, origin, flags->force,
		    flags->heal, flags->resumable, raw, infd, drr_noswap, -1,
		    &read_bytes, &errflags, NULL, &prop_errors);
	} else {
		err = ioctl_err = lzc_receive_with_cmdprops(destsnap, rcvprops,
		    oxprops, wkeydata, wkeylen, origin, flags->force,
		    flags->resumable, raw, infd, drr_noswap, -1, &read_bytes,
		    &errflags, NULL, &prop_errors);
	}
	ioctl_errno = ioctl_err;
	prop_errflags = errflags;

	if (err == 0) {
		nvpair_t *prop_err = NULL;

		while ((prop_err = nvlist_next_nvpair(prop_errors,
		    prop_err)) != NULL) {
			char tbuf[1024];
			zfs_prop_t prop;
			int intval;

			prop = zfs_name_to_prop(nvpair_name(prop_err));
			(void) nvpair_value_int32(prop_err, &intval);
			if (strcmp(nvpair_name(prop_err),
			    ZPROP_N_MORE_ERRORS) == 0) {
				trunc_prop_errs(intval);
				break;
			} else if (snapname == NULL || finalsnap == NULL ||
			    strcmp(finalsnap, snapname) == 0 ||
			    strcmp(nvpair_name(prop_err),
			    zfs_prop_to_name(ZFS_PROP_REFQUOTA)) != 0) {
				/*
				 * Skip the special case of, for example,
				 * "refquota", errors on intermediate
				 * snapshots leading up to a final one.
				 * That's why we have all of the checks above.
				 *
				 * See zfs_ioctl.c's extract_delay_props() for
				 * a list of props which can fail on
				 * intermediate snapshots, but shouldn't
				 * affect the overall receive.
				 */
				(void) snprintf(tbuf, sizeof (tbuf),
				    dgettext(TEXT_DOMAIN,
				    "cannot receive %s property on %s"),
				    nvpair_name(prop_err), name);
				zfs_setprop_error(hdl, prop, intval, tbuf);
			}
		}
	}

	if (err == 0 && snapprops_nvlist) {
		zfs_cmd_t zc = {"\0"};

		(void) strlcpy(zc.zc_name, destsnap, sizeof (zc.zc_name));
		zc.zc_cookie = B_TRUE; /* received */
		zcmd_write_src_nvlist(hdl, &zc, snapprops_nvlist);
		(void) zfs_ioctl(hdl, ZFS_IOC_SET_PROP, &zc);
		zcmd_free_nvlists(&zc);
	}
	if (err == 0 && snapholds_nvlist) {
		nvpair_t *pair;
		nvlist_t *holds, *errors = NULL;
		int cleanup_fd = -1;

		VERIFY(0 == nvlist_alloc(&holds, 0, KM_SLEEP));
		for (pair = nvlist_next_nvpair(snapholds_nvlist, NULL);
		    pair != NULL;
		    pair = nvlist_next_nvpair(snapholds_nvlist, pair)) {
			fnvlist_add_string(holds, destsnap, nvpair_name(pair));
		}
		(void) lzc_hold(holds, cleanup_fd, &errors);
		fnvlist_free(snapholds_nvlist);
		fnvlist_free(holds);
	}

	if (err && (ioctl_errno == ENOENT || ioctl_errno == EEXIST)) {
		/*
		 * It may be that this snapshot already exists,
		 * in which case we want to consume & ignore it
		 * rather than failing.
		 */
		avl_tree_t *local_avl;
		nvlist_t *local_nv, *fs;
		cp = strchr(destsnap, '@');

		/*
		 * XXX Do this faster by just iterating over snaps in
		 * this fs.  Also if zc_value does not exist, we will
		 * get a strange "does not exist" error message.
		 */
		*cp = '\0';
		if (gather_nvlist(hdl, destsnap, NULL, NULL, B_FALSE, B_TRUE,
		    B_FALSE, B_FALSE, B_FALSE, B_FALSE, B_FALSE, B_FALSE,
		    B_TRUE, &local_nv, &local_avl) == 0) {
			*cp = '@';
			fs = fsavl_find(local_avl, drrb->drr_toguid, NULL);
			fsavl_destroy(local_avl);
			fnvlist_free(local_nv);

			if (fs != NULL) {
				if (flags->verbose) {
					(void) printf("snap %s already exists; "
					    "ignoring\n", destsnap);
				}
				err = ioctl_err = recv_skip(hdl, infd,
				    flags->byteswap);
			}
		}
		*cp = '@';
	}

	if (ioctl_err != 0) {
		switch (ioctl_errno) {
		case ENODEV:
			cp = strchr(destsnap, '@');
			*cp = '\0';
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "most recent snapshot of %s does not\n"
			    "match incremental source"), destsnap);
			(void) zfs_error(hdl, EZFS_BADRESTORE, errbuf);
			*cp = '@';
			break;
		case ETXTBSY:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "destination %s has been modified\n"
			    "since most recent snapshot"), name);
			(void) zfs_error(hdl, EZFS_BADRESTORE, errbuf);
			break;
		case EACCES:
			if (flags->heal) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "key must be loaded to do a non-raw "
				    "corrective recv on an encrypted "
				    "dataset."));
			} else if (raw && stream_wantsnewfs) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "failed to create encryption key"));
			} else if (raw && !stream_wantsnewfs) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "encryption key does not match "
				    "existing key"));
			} else {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "inherited key must be loaded"));
			}
			(void) zfs_error(hdl, EZFS_CRYPTOFAILED, errbuf);
			break;
		case EEXIST:
			cp = strchr(destsnap, '@');
			if (newfs) {
				/* it's the containing fs that exists */
				*cp = '\0';
			}
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "destination already exists"));
			(void) zfs_error_fmt(hdl, EZFS_EXISTS,
			    dgettext(TEXT_DOMAIN, "cannot restore to %s"),
			    destsnap);
			*cp = '@';
			break;
		case EINVAL:
			if (embedded && !raw) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "incompatible embedded data stream "
				    "feature with encrypted receive."));
			} else if (flags->resumable) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "kernel modules must be upgraded to "
				    "receive this stream."));
			}
			(void) zfs_error(hdl, EZFS_BADSTREAM, errbuf);
			break;
		case ECKSUM:
		case ZFS_ERR_STREAM_TRUNCATED:
			if (flags->heal)
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "corrective receive was not able to "
				    "reconstruct the data needed for "
				    "healing."));
			else
				recv_ecksum_set_aux(hdl, destsnap,
				    flags->resumable, ioctl_err == ECKSUM);
			(void) zfs_error(hdl, EZFS_BADSTREAM, errbuf);
			break;
		case ZFS_ERR_STREAM_LARGE_BLOCK_MISMATCH:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "incremental send stream requires -L "
			    "(--large-block), to match previous receive."));
			(void) zfs_error(hdl, EZFS_BADSTREAM, errbuf);
			break;
		case ENOTSUP:
			if (flags->heal)
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "stream is not compatible with the "
				    "data in the pool."));
			else
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "pool must be upgraded to receive this "
				    "stream."));
			(void) zfs_error(hdl, EZFS_BADVERSION, errbuf);
			break;
		case ZFS_ERR_CRYPTO_NOTSUP:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "stream uses crypto parameters not compatible with "
			    "this pool"));
			(void) zfs_error(hdl, EZFS_BADSTREAM, errbuf);
			break;
		case EDQUOT:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "destination %s space quota exceeded."), name);
			(void) zfs_error(hdl, EZFS_NOSPC, errbuf);
			break;
		case ZFS_ERR_FROM_IVSET_GUID_MISSING:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "IV set guid missing. See errata %u at "
			    "https://openzfs.github.io/openzfs-docs/msg/"
			    "ZFS-8000-ER."),
			    ZPOOL_ERRATA_ZOL_8308_ENCRYPTION);
			(void) zfs_error(hdl, EZFS_BADSTREAM, errbuf);
			break;
		case ZFS_ERR_FROM_IVSET_GUID_MISMATCH:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "IV set guid mismatch. See the 'zfs receive' "
			    "man page section\n discussing the limitations "
			    "of raw encrypted send streams."));
			(void) zfs_error(hdl, EZFS_BADSTREAM, errbuf);
			break;
		case ZFS_ERR_SPILL_BLOCK_FLAG_MISSING:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "Spill block flag missing for raw send.\n"
			    "The zfs software on the sending system must "
			    "be updated."));
			(void) zfs_error(hdl, EZFS_BADSTREAM, errbuf);
			break;
		case ZFS_ERR_RESUME_EXISTS:
			cp = strchr(destsnap, '@');
			if (newfs) {
				/* it's the containing fs that exists */
				*cp = '\0';
			}
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "Resuming recv on existing dataset without force"));
			(void) zfs_error_fmt(hdl, EZFS_RESUME_EXISTS,
			    dgettext(TEXT_DOMAIN, "cannot resume recv %s"),
			    destsnap);
			*cp = '@';
			break;
		case E2BIG:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "zfs receive required kernel memory allocation "
			    "larger than the system can support. Please file "
			    "an issue at the OpenZFS issue tracker:\n"
			    "https://github.com/openzfs/zfs/issues/new"));
			(void) zfs_error(hdl, EZFS_BADSTREAM, errbuf);
			break;
		case EBUSY:
			if (hastoken) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "destination %s contains "
				    "partially-complete state from "
				    "\"zfs receive -s\"."), name);
				(void) zfs_error(hdl, EZFS_BUSY, errbuf);
				break;
			}
			zfs_fallthrough;
		default:
			(void) zfs_standard_error(hdl, ioctl_errno, errbuf);
		}
	}

	/*
	 * Mount the target filesystem (if created).  Also mount any
	 * children of the target filesystem if we did a replication
	 * receive (indicated by stream_avl being non-NULL).
	 */
	if (clp) {
		if (!flags->nomount)
			err |= changelist_postfix(clp);
		changelist_free(clp);
	}

	if ((newfs || stream_avl) && type == ZFS_TYPE_FILESYSTEM && !redacted)
		flags->domount = B_TRUE;

	if (prop_errflags & ZPROP_ERR_NOCLEAR) {
		(void) fprintf(stderr, dgettext(TEXT_DOMAIN, "Warning: "
		    "failed to clear unreceived properties on %s"), name);
		(void) fprintf(stderr, "\n");
	}
	if (prop_errflags & ZPROP_ERR_NORESTORE) {
		(void) fprintf(stderr, dgettext(TEXT_DOMAIN, "Warning: "
		    "failed to restore original properties on %s"), name);
		(void) fprintf(stderr, "\n");
	}

	if (err || ioctl_err) {
		err = -1;
		goto out;
	}

	if (flags->verbose) {
		char buf1[64];
		char buf2[64];
		uint64_t bytes = read_bytes;
		struct timespec delta;
		clock_gettime(CLOCK_MONOTONIC_RAW, &delta);
		if (begin_time.tv_nsec > delta.tv_nsec) {
			delta.tv_nsec =
			    1000000000 + delta.tv_nsec - begin_time.tv_nsec;
			delta.tv_sec -= 1;
		} else
			delta.tv_nsec -= begin_time.tv_nsec;
		delta.tv_sec -= begin_time.tv_sec;
		if (delta.tv_sec == 0 && delta.tv_nsec == 0)
			delta.tv_nsec = 1;
		double delta_f = delta.tv_sec + (delta.tv_nsec / 1e9);
		zfs_nicebytes(bytes, buf1, sizeof (buf1));
		zfs_nicebytes(bytes / delta_f, buf2, sizeof (buf2));

		(void) printf("received %s stream in %.2f seconds (%s/sec)\n",
		    buf1, delta_f, buf2);
	}

	err = 0;
out:
	if (prop_errors != NULL)
		fnvlist_free(prop_errors);

	if (tmp_keylocation[0] != '\0') {
		fnvlist_add_string(rcvprops,
		    zfs_prop_to_name(ZFS_PROP_KEYLOCATION), tmp_keylocation);
	}

	if (newprops)
		fnvlist_free(rcvprops);

	fnvlist_free(oxprops);
	fnvlist_free(origprops);

	return (err);
}

/*
 * Check properties we were asked to override (both -o|-x)
 */
static boolean_t
zfs_receive_checkprops(libzfs_handle_t *hdl, nvlist_t *props,
    const char *errbuf)
{
	nvpair_t *nvp = NULL;
	zfs_prop_t prop;
	const char *name;

	while ((nvp = nvlist_next_nvpair(props, nvp)) != NULL) {
		name = nvpair_name(nvp);
		prop = zfs_name_to_prop(name);

		if (prop == ZPROP_USERPROP) {
			if (!zfs_prop_user(name)) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "%s: invalid property '%s'"), errbuf, name);
				return (B_FALSE);
			}
			continue;
		}
		/*
		 * "origin" is readonly but is used to receive datasets as
		 * clones so we don't raise an error here
		 */
		if (prop == ZFS_PROP_ORIGIN)
			continue;

		/* encryption params have their own verification later */
		if (prop == ZFS_PROP_ENCRYPTION ||
		    zfs_prop_encryption_key_param(prop))
			continue;

		/*
		 * cannot override readonly, set-once and other specific
		 * settable properties
		 */
		if (zfs_prop_readonly(prop) || prop == ZFS_PROP_VERSION ||
		    prop == ZFS_PROP_VOLSIZE) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "%s: invalid property '%s'"), errbuf, name);
			return (B_FALSE);
		}
	}

	return (B_TRUE);
}

static int
zfs_receive_impl(libzfs_handle_t *hdl, const char *tosnap,
    const char *originsnap, recvflags_t *flags, int infd, const char *sendfs,
    nvlist_t *stream_nv, avl_tree_t *stream_avl, char **top_zfs,
    const char *finalsnap, nvlist_t *cmdprops)
{
	int err;
	dmu_replay_record_t drr, drr_noswap;
	struct drr_begin *drrb = &drr.drr_u.drr_begin;
	char errbuf[ERRBUFLEN];
	zio_cksum_t zcksum = { { 0 } };
	uint64_t featureflags;
	int hdrtype;

	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "cannot receive"));

	/* check cmdline props, raise an error if they cannot be received */
	if (!zfs_receive_checkprops(hdl, cmdprops, errbuf))
		return (zfs_error(hdl, EZFS_BADPROP, errbuf));

	if (flags->isprefix &&
	    !zfs_dataset_exists(hdl, tosnap, ZFS_TYPE_DATASET)) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, "specified fs "
		    "(%s) does not exist"), tosnap);
		return (zfs_error(hdl, EZFS_NOENT, errbuf));
	}
	if (originsnap &&
	    !zfs_dataset_exists(hdl, originsnap, ZFS_TYPE_DATASET)) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, "specified origin fs "
		    "(%s) does not exist"), originsnap);
		return (zfs_error(hdl, EZFS_NOENT, errbuf));
	}

	/* read in the BEGIN record */
	if (0 != (err = recv_read(hdl, infd, &drr, sizeof (drr), B_FALSE,
	    &zcksum)))
		return (err);

	if (drr.drr_type == DRR_END || drr.drr_type == BSWAP_32(DRR_END)) {
		/* It's the double end record at the end of a package */
		return (ENODATA);
	}

	/* the kernel needs the non-byteswapped begin record */
	drr_noswap = drr;

	flags->byteswap = B_FALSE;
	if (drrb->drr_magic == BSWAP_64(DMU_BACKUP_MAGIC)) {
		/*
		 * We computed the checksum in the wrong byteorder in
		 * recv_read() above; do it again correctly.
		 */
		memset(&zcksum, 0, sizeof (zio_cksum_t));
		fletcher_4_incremental_byteswap(&drr, sizeof (drr), &zcksum);
		flags->byteswap = B_TRUE;

		drr.drr_type = BSWAP_32(drr.drr_type);
		drr.drr_payloadlen = BSWAP_32(drr.drr_payloadlen);
		drrb->drr_magic = BSWAP_64(drrb->drr_magic);
		drrb->drr_versioninfo = BSWAP_64(drrb->drr_versioninfo);
		drrb->drr_creation_time = BSWAP_64(drrb->drr_creation_time);
		drrb->drr_type = BSWAP_32(drrb->drr_type);
		drrb->drr_flags = BSWAP_32(drrb->drr_flags);
		drrb->drr_toguid = BSWAP_64(drrb->drr_toguid);
		drrb->drr_fromguid = BSWAP_64(drrb->drr_fromguid);
	}

	if (drrb->drr_magic != DMU_BACKUP_MAGIC || drr.drr_type != DRR_BEGIN) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, "invalid "
		    "stream (bad magic number)"));
		return (zfs_error(hdl, EZFS_BADSTREAM, errbuf));
	}

	featureflags = DMU_GET_FEATUREFLAGS(drrb->drr_versioninfo);
	hdrtype = DMU_GET_STREAM_HDRTYPE(drrb->drr_versioninfo);

	if (!DMU_STREAM_SUPPORTED(featureflags) ||
	    (hdrtype != DMU_SUBSTREAM && hdrtype != DMU_COMPOUNDSTREAM)) {
		/*
		 * Let's be explicit about this one, since rather than
		 * being a new feature we can't know, it's an old
		 * feature we dropped.
		 */
		if (featureflags & DMU_BACKUP_FEATURE_DEDUP) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "stream has deprecated feature: dedup, try "
			    "'zstream redup [send in a file] | zfs recv "
			    "[...]'"));
		} else {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "stream has unsupported feature, feature flags = "
			    "%llx (unknown flags = %llx)"),
			    (u_longlong_t)featureflags,
			    (u_longlong_t)((featureflags) &
			    ~DMU_BACKUP_FEATURE_MASK));
		}
		return (zfs_error(hdl, EZFS_BADSTREAM, errbuf));
	}

	/* Holds feature is set once in the compound stream header. */
	if (featureflags & DMU_BACKUP_FEATURE_HOLDS)
		flags->holds = B_TRUE;

	if (strchr(drrb->drr_toname, '@') == NULL) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, "invalid "
		    "stream (bad snapshot name)"));
		return (zfs_error(hdl, EZFS_BADSTREAM, errbuf));
	}

	if (DMU_GET_STREAM_HDRTYPE(drrb->drr_versioninfo) == DMU_SUBSTREAM) {
		char nonpackage_sendfs[ZFS_MAX_DATASET_NAME_LEN];
		if (sendfs == NULL) {
			/*
			 * We were not called from zfs_receive_package(). Get
			 * the fs specified by 'zfs send'.
			 */
			char *cp;
			(void) strlcpy(nonpackage_sendfs,
			    drr.drr_u.drr_begin.drr_toname,
			    sizeof (nonpackage_sendfs));
			if ((cp = strchr(nonpackage_sendfs, '@')) != NULL)
				*cp = '\0';
			sendfs = nonpackage_sendfs;
			VERIFY(finalsnap == NULL);
		}
		return (zfs_receive_one(hdl, infd, tosnap, originsnap, flags,
		    &drr, &drr_noswap, sendfs, stream_nv, stream_avl, top_zfs,
		    finalsnap, cmdprops));
	} else {
		assert(DMU_GET_STREAM_HDRTYPE(drrb->drr_versioninfo) ==
		    DMU_COMPOUNDSTREAM);
		return (zfs_receive_package(hdl, infd, tosnap, flags, &drr,
		    &zcksum, top_zfs, cmdprops));
	}
}

/*
 * Restores a backup of tosnap from the file descriptor specified by infd.
 * Return 0 on total success, -2 if some things couldn't be
 * destroyed/renamed/promoted, -1 if some things couldn't be received.
 * (-1 will override -2, if -1 and the resumable flag was specified the
 * transfer can be resumed if the sending side supports it).
 */
int
zfs_receive(libzfs_handle_t *hdl, const char *tosnap, nvlist_t *props,
    recvflags_t *flags, int infd, avl_tree_t *stream_avl)
{
	char *top_zfs = NULL;
	int err;
	struct stat sb;
	const char *originsnap = NULL;

	/*
	 * The only way fstat can fail is if we do not have a valid file
	 * descriptor.
	 */
	if (fstat(infd, &sb) == -1) {
		perror("fstat");
		return (-2);
	}

	if (props) {
		err = nvlist_lookup_string(props, "origin", &originsnap);
		if (err && err != ENOENT)
			return (err);
	}

	err = zfs_receive_impl(hdl, tosnap, originsnap, flags, infd, NULL, NULL,
	    stream_avl, &top_zfs, NULL, props);

	if (err == 0 && !flags->nomount && flags->domount && top_zfs) {
		zfs_handle_t *zhp = NULL;
		prop_changelist_t *clp = NULL;

		zhp = zfs_open(hdl, top_zfs,
		    ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME);
		if (zhp == NULL) {
			err = -1;
			goto out;
		} else {
			if (zhp->zfs_type == ZFS_TYPE_VOLUME) {
				zfs_close(zhp);
				goto out;
			}

			clp = changelist_gather(zhp, ZFS_PROP_MOUNTPOINT,
			    CL_GATHER_MOUNT_ALWAYS,
			    flags->forceunmount ? MS_FORCE : 0);
			zfs_close(zhp);
			if (clp == NULL) {
				err = -1;
				goto out;
			}

			/* mount and share received datasets */
			err = changelist_postfix(clp);
			changelist_free(clp);
			if (err != 0)
				err = -1;
		}
	}

out:
	if (top_zfs)
		free(top_zfs);

	return (err);
}
