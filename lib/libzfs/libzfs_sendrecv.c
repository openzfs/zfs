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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <libdevinfo.h>
#include <libintl.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <stddef.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/mntent.h>
#include <sys/mnttab.h>
#include <sys/avl.h>
#include <stddef.h>

#include <libzfs.h>

#include "zfs_namecheck.h"
#include "zfs_prop.h"
#include "libzfs_impl.h"

#include <fletcher.c> /* XXX */

static int zfs_receive_impl(libzfs_handle_t *, const char *, recvflags_t,
    int, avl_tree_t *, char **);

/*
 * Routines for dealing with the AVL tree of fs-nvlists
 */
typedef struct fsavl_node {
	avl_node_t fn_node;
	nvlist_t *fn_nvfs;
	char *fn_snapname;
	uint64_t fn_guid;
} fsavl_node_t;

static int
fsavl_compare(const void *arg1, const void *arg2)
{
	const fsavl_node_t *fn1 = arg1;
	const fsavl_node_t *fn2 = arg2;

	if (fn1->fn_guid > fn2->fn_guid)
		return (+1);
	else if (fn1->fn_guid < fn2->fn_guid)
		return (-1);
	else
		return (0);
}

/*
 * Given the GUID of a snapshot, find its containing filesystem and
 * (optionally) name.
 */
static nvlist_t *
fsavl_find(avl_tree_t *avl, uint64_t snapguid, char **snapname)
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

		VERIFY(0 == nvpair_value_nvlist(fselem, &nvfs));
		VERIFY(0 == nvlist_lookup_nvlist(nvfs, "snaps", &snaps));

		while ((snapelem =
		    nvlist_next_nvpair(snaps, snapelem)) != NULL) {
			fsavl_node_t *fn;
			uint64_t guid;

			VERIFY(0 == nvpair_value_uint64(snapelem, &guid));
			if ((fn = malloc(sizeof (fsavl_node_t))) == NULL) {
				fsavl_destroy(fsavl);
				return (NULL);
			}
			fn->fn_nvfs = nvfs;
			fn->fn_snapname = nvpair_name(snapelem);
			fn->fn_guid = guid;

			/*
			 * Note: if there are multiple snaps with the
			 * same GUID, we ignore all but one.
			 */
			if (avl_find(fsavl, fn, NULL) == NULL)
				avl_add(fsavl, fn);
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
	uint64_t parent_fromsnap_guid;
	nvlist_t *parent_snaps;
	nvlist_t *fss;
	nvlist_t *snapprops;
	const char *fromsnap;
	const char *tosnap;

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
	 *
	 *	 "origin" -> number (guid) (if clone)
	 *	 "sent" -> boolean (not on-disk)
	 *	}
	 *   }
	 * }
	 *
	 */
} send_data_t;

static void send_iterate_prop(zfs_handle_t *zhp, nvlist_t *nv);

static int
send_iterate_snap(zfs_handle_t *zhp, void *arg)
{
	send_data_t *sd = arg;
	uint64_t guid = zhp->zfs_dmustats.dds_guid;
	char *snapname;
	nvlist_t *nv;

	snapname = strrchr(zhp->zfs_name, '@')+1;

	VERIFY(0 == nvlist_add_uint64(sd->parent_snaps, snapname, guid));
	/*
	 * NB: if there is no fromsnap here (it's a newly created fs in
	 * an incremental replication), we will substitute the tosnap.
	 */
	if ((sd->fromsnap && strcmp(snapname, sd->fromsnap) == 0) ||
	    (sd->parent_fromsnap_guid == 0 && sd->tosnap &&
	    strcmp(snapname, sd->tosnap) == 0)) {
		sd->parent_fromsnap_guid = guid;
	}

	VERIFY(0 == nvlist_alloc(&nv, NV_UNIQUE_NAME, 0));
	send_iterate_prop(zhp, nv);
	VERIFY(0 == nvlist_add_nvlist(sd->snapprops, snapname, nv));
	nvlist_free(nv);

	zfs_close(zhp);
	return (0);
}

static void
send_iterate_prop(zfs_handle_t *zhp, nvlist_t *nv)
{
	nvpair_t *elem = NULL;

	while ((elem = nvlist_next_nvpair(zhp->zfs_props, elem)) != NULL) {
		char *propname = nvpair_name(elem);
		zfs_prop_t prop = zfs_name_to_prop(propname);
		nvlist_t *propnv;

		if (!zfs_prop_user(propname) && zfs_prop_readonly(prop))
			continue;

		verify(nvpair_value_nvlist(elem, &propnv) == 0);
		if (prop == ZFS_PROP_QUOTA || prop == ZFS_PROP_RESERVATION) {
			/* these guys are modifyable, but have no source */
			uint64_t value;
			verify(nvlist_lookup_uint64(propnv,
			    ZPROP_VALUE, &value) == 0);
			if (zhp->zfs_type == ZFS_TYPE_SNAPSHOT)
				continue;
		} else {
			char *source;
			if (nvlist_lookup_string(propnv,
			    ZPROP_SOURCE, &source) != 0)
				continue;
			if (strcmp(source, zhp->zfs_name) != 0)
				continue;
		}

		if (zfs_prop_user(propname) ||
		    zfs_prop_get_type(prop) == PROP_TYPE_STRING) {
			char *value;
			verify(nvlist_lookup_string(propnv,
			    ZPROP_VALUE, &value) == 0);
			VERIFY(0 == nvlist_add_string(nv, propname, value));
		} else {
			uint64_t value;
			verify(nvlist_lookup_uint64(propnv,
			    ZPROP_VALUE, &value) == 0);
			VERIFY(0 == nvlist_add_uint64(nv, propname, value));
		}
	}
}

static int
send_iterate_fs(zfs_handle_t *zhp, void *arg)
{
	send_data_t *sd = arg;
	nvlist_t *nvfs, *nv;
	int rv;
	uint64_t parent_fromsnap_guid_save = sd->parent_fromsnap_guid;
	uint64_t guid = zhp->zfs_dmustats.dds_guid;
	char guidstring[64];

	VERIFY(0 == nvlist_alloc(&nvfs, NV_UNIQUE_NAME, 0));
	VERIFY(0 == nvlist_add_string(nvfs, "name", zhp->zfs_name));
	VERIFY(0 == nvlist_add_uint64(nvfs, "parentfromsnap",
	    sd->parent_fromsnap_guid));

	if (zhp->zfs_dmustats.dds_origin[0]) {
		zfs_handle_t *origin = zfs_open(zhp->zfs_hdl,
		    zhp->zfs_dmustats.dds_origin, ZFS_TYPE_SNAPSHOT);
		if (origin == NULL)
			return (-1);
		VERIFY(0 == nvlist_add_uint64(nvfs, "origin",
		    origin->zfs_dmustats.dds_guid));
	}

	/* iterate over props */
	VERIFY(0 == nvlist_alloc(&nv, NV_UNIQUE_NAME, 0));
	send_iterate_prop(zhp, nv);
	VERIFY(0 == nvlist_add_nvlist(nvfs, "props", nv));
	nvlist_free(nv);

	/* iterate over snaps, and set sd->parent_fromsnap_guid */
	sd->parent_fromsnap_guid = 0;
	VERIFY(0 == nvlist_alloc(&sd->parent_snaps, NV_UNIQUE_NAME, 0));
	VERIFY(0 == nvlist_alloc(&sd->snapprops, NV_UNIQUE_NAME, 0));
	(void) zfs_iter_snapshots(zhp, send_iterate_snap, sd);
	VERIFY(0 == nvlist_add_nvlist(nvfs, "snaps", sd->parent_snaps));
	VERIFY(0 == nvlist_add_nvlist(nvfs, "snapprops", sd->snapprops));
	nvlist_free(sd->parent_snaps);
	nvlist_free(sd->snapprops);

	/* add this fs to nvlist */
	(void) snprintf(guidstring, sizeof (guidstring),
	    "0x%llx", (longlong_t)guid);
	VERIFY(0 == nvlist_add_nvlist(sd->fss, guidstring, nvfs));
	nvlist_free(nvfs);

	/* iterate over children */
	rv = zfs_iter_filesystems(zhp, send_iterate_fs, sd);

	sd->parent_fromsnap_guid = parent_fromsnap_guid_save;

	zfs_close(zhp);
	return (rv);
}

static int
gather_nvlist(libzfs_handle_t *hdl, const char *fsname, const char *fromsnap,
    const char *tosnap, nvlist_t **nvlp, avl_tree_t **avlp)
{
	zfs_handle_t *zhp;
	send_data_t sd = { 0 };
	int error;

	zhp = zfs_open(hdl, fsname, ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME);
	if (zhp == NULL)
		return (EZFS_BADTYPE);

	VERIFY(0 == nvlist_alloc(&sd.fss, NV_UNIQUE_NAME, 0));
	sd.fromsnap = fromsnap;
	sd.tosnap = tosnap;

	if ((error = send_iterate_fs(zhp, &sd)) != 0) {
		nvlist_free(sd.fss);
		if (avlp != NULL)
			*avlp = NULL;
		*nvlp = NULL;
		return (error);
	}

	if (avlp != NULL && (*avlp = fsavl_create(sd.fss)) == NULL) {
		nvlist_free(sd.fss);
		*nvlp = NULL;
		return (EZFS_NOMEM);
	}

	*nvlp = sd.fss;
	return (0);
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
	zfs_node_t *node = zfs_alloc(zhp->zfs_hdl, sizeof (zfs_node_t));

	node->zn_handle = zhp;
	avl_add(avl, node);
	return (0);
}

/* ARGSUSED */
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

	if (lcreate < rcreate)
		return (-1);
	else if (lcreate > rcreate)
		return (+1);
	else
		return (0);
}

static int
zfs_iter_snapshots_sorted(zfs_handle_t *zhp, zfs_iter_f callback, void *data)
{
	int ret = 0;
	zfs_node_t *node;
	avl_tree_t avl;
	void *cookie = NULL;

	avl_create(&avl, zfs_snapshot_compare,
	    sizeof (zfs_node_t), offsetof(zfs_node_t, zn_avlnode));

	ret = zfs_iter_snapshots(zhp, zfs_sort_snaps, &avl);

	for (node = avl_first(&avl); node != NULL; node = AVL_NEXT(&avl, node))
		ret |= callback(node->zn_handle, data);

	while ((node = avl_destroy_nodes(&avl, &cookie)) != NULL)
		free(node);

	avl_destroy(&avl);

	return (ret);
}

/*
 * Routines specific to "zfs send"
 */
typedef struct send_dump_data {
	/* these are all just the short snapname (the part after the @) */
	const char *fromsnap;
	const char *tosnap;
	char lastsnap[ZFS_MAXNAMELEN];
	boolean_t seenfrom, seento, replicate, doall, fromorigin;
	boolean_t verbose;
	int outfd;
	boolean_t err;
	nvlist_t *fss;
	avl_tree_t *fsavl;
} send_dump_data_t;

/*
 * Dumps a backup of the given snapshot (incremental from fromsnap if it's not
 * NULL) to the file descriptor specified by outfd.
 */
static int
dump_ioctl(zfs_handle_t *zhp, const char *fromsnap, boolean_t fromorigin,
    int outfd)
{
	zfs_cmd_t zc = { 0 };
	libzfs_handle_t *hdl = zhp->zfs_hdl;

	assert(zhp->zfs_type == ZFS_TYPE_SNAPSHOT);
	assert(fromsnap == NULL || fromsnap[0] == '\0' || !fromorigin);

	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));
	if (fromsnap)
		(void) strlcpy(zc.zc_value, fromsnap, sizeof (zc.zc_value));
	zc.zc_cookie = outfd;
	zc.zc_obj = fromorigin;

	if (ioctl(zhp->zfs_hdl->libzfs_fd, ZFS_IOC_SEND, &zc) != 0) {
		char errbuf[1024];
		(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
		    "warning: cannot send '%s'"), zhp->zfs_name);

		switch (errno) {

		case EXDEV:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "not an earlier snapshot from the same fs"));
			return (zfs_error(hdl, EZFS_CROSSTARGET, errbuf));

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
			zfs_error_aux(hdl, strerror(errno));
			return (zfs_error(hdl, EZFS_BADBACKUP, errbuf));

		default:
			return (zfs_standard_error(hdl, errno, errbuf));
		}
	}

	return (0);
}

static int
dump_snapshot(zfs_handle_t *zhp, void *arg)
{
	send_dump_data_t *sdd = arg;
	const char *thissnap;
	int err;

	thissnap = strchr(zhp->zfs_name, '@') + 1;

	if (sdd->fromsnap && !sdd->seenfrom &&
	    strcmp(sdd->fromsnap, thissnap) == 0) {
		sdd->seenfrom = B_TRUE;
		(void) strcpy(sdd->lastsnap, thissnap);
		zfs_close(zhp);
		return (0);
	}

	if (sdd->seento || !sdd->seenfrom) {
		zfs_close(zhp);
		return (0);
	}

	/* send it */
	if (sdd->verbose) {
		(void) fprintf(stderr, "sending from @%s to %s\n",
		    sdd->lastsnap, zhp->zfs_name);
	}

	err = dump_ioctl(zhp, sdd->lastsnap,
	    sdd->lastsnap[0] == '\0' && (sdd->fromorigin || sdd->replicate),
	    sdd->outfd);

	if (!sdd->seento && strcmp(sdd->tosnap, thissnap) == 0)
		sdd->seento = B_TRUE;

	(void) strcpy(sdd->lastsnap, thissnap);
	zfs_close(zhp);
	return (err);
}

static int
dump_filesystem(zfs_handle_t *zhp, void *arg)
{
	int rv = 0;
	send_dump_data_t *sdd = arg;
	boolean_t missingfrom = B_FALSE;
	zfs_cmd_t zc = { 0 };

	(void) snprintf(zc.zc_name, sizeof (zc.zc_name), "%s@%s",
	    zhp->zfs_name, sdd->tosnap);
	if (ioctl(zhp->zfs_hdl->libzfs_fd, ZFS_IOC_OBJSET_STATS, &zc) != 0) {
		(void) fprintf(stderr, "WARNING: "
		    "could not send %s@%s: does not exist\n",
		    zhp->zfs_name, sdd->tosnap);
		sdd->err = B_TRUE;
		return (0);
	}

	if (sdd->replicate && sdd->fromsnap) {
		/*
		 * If this fs does not have fromsnap, and we're doing
		 * recursive, we need to send a full stream from the
		 * beginning (or an incremental from the origin if this
		 * is a clone).  If we're doing non-recursive, then let
		 * them get the error.
		 */
		(void) snprintf(zc.zc_name, sizeof (zc.zc_name), "%s@%s",
		    zhp->zfs_name, sdd->fromsnap);
		if (ioctl(zhp->zfs_hdl->libzfs_fd,
		    ZFS_IOC_OBJSET_STATS, &zc) != 0) {
			missingfrom = B_TRUE;
		}
	}

	if (sdd->doall) {
		sdd->seenfrom = sdd->seento = sdd->lastsnap[0] = 0;
		if (sdd->fromsnap == NULL || missingfrom)
			sdd->seenfrom = B_TRUE;

		rv = zfs_iter_snapshots_sorted(zhp, dump_snapshot, arg);
		if (!sdd->seenfrom) {
			(void) fprintf(stderr,
			    "WARNING: could not send %s@%s:\n"
			    "incremental source (%s@%s) does not exist\n",
			    zhp->zfs_name, sdd->tosnap,
			    zhp->zfs_name, sdd->fromsnap);
			sdd->err = B_TRUE;
		} else if (!sdd->seento) {
			(void) fprintf(stderr,
			    "WARNING: could not send %s@%s:\n"
			    "incremental source (%s@%s) "
			    "is not earlier than it\n",
			    zhp->zfs_name, sdd->tosnap,
			    zhp->zfs_name, sdd->fromsnap);
			sdd->err = B_TRUE;
		}
	} else {
		zfs_handle_t *snapzhp;
		char snapname[ZFS_MAXNAMELEN];

		(void) snprintf(snapname, sizeof (snapname), "%s@%s",
		    zfs_get_name(zhp), sdd->tosnap);
		snapzhp = zfs_open(zhp->zfs_hdl, snapname, ZFS_TYPE_SNAPSHOT);
		if (snapzhp == NULL) {
			rv = -1;
		} else {
			rv = dump_ioctl(snapzhp,
			    missingfrom ? NULL : sdd->fromsnap,
			    sdd->fromorigin || missingfrom,
			    sdd->outfd);
			sdd->seento = B_TRUE;
			zfs_close(snapzhp);
		}
	}

	return (rv);
}

static int
dump_filesystems(zfs_handle_t *rzhp, void *arg)
{
	send_dump_data_t *sdd = arg;
	nvpair_t *fspair;
	boolean_t needagain, progress;

	if (!sdd->replicate)
		return (dump_filesystem(rzhp, sdd));

again:
	needagain = progress = B_FALSE;
	for (fspair = nvlist_next_nvpair(sdd->fss, NULL); fspair;
	    fspair = nvlist_next_nvpair(sdd->fss, fspair)) {
		nvlist_t *fslist;
		char *fsname;
		zfs_handle_t *zhp;
		int err;
		uint64_t origin_guid = 0;
		nvlist_t *origin_nv;

		VERIFY(nvpair_value_nvlist(fspair, &fslist) == 0);
		if (nvlist_lookup_boolean(fslist, "sent") == 0)
			continue;

		VERIFY(nvlist_lookup_string(fslist, "name", &fsname) == 0);
		(void) nvlist_lookup_uint64(fslist, "origin", &origin_guid);

		origin_nv = fsavl_find(sdd->fsavl, origin_guid, NULL);
		if (origin_nv &&
		    nvlist_lookup_boolean(origin_nv, "sent") == ENOENT) {
			/*
			 * origin has not been sent yet;
			 * skip this clone.
			 */
			needagain = B_TRUE;
			continue;
		}

		zhp = zfs_open(rzhp->zfs_hdl, fsname, ZFS_TYPE_DATASET);
		if (zhp == NULL)
			return (-1);
		err = dump_filesystem(zhp, sdd);
		VERIFY(nvlist_add_boolean(fslist, "sent") == 0);
		progress = B_TRUE;
		zfs_close(zhp);
		if (err)
			return (err);
	}
	if (needagain) {
		assert(progress);
		goto again;
	}
	return (0);
}

/*
 * Dumps a backup of tosnap, incremental from fromsnap if it isn't NULL.
 * If 'doall', dump all intermediate snaps.
 * If 'replicate', dump special header and do recursively.
 */
int
zfs_send(zfs_handle_t *zhp, const char *fromsnap, const char *tosnap,
    boolean_t replicate, boolean_t doall, boolean_t fromorigin,
    boolean_t verbose, int outfd)
{
	char errbuf[1024];
	send_dump_data_t sdd = { 0 };
	int err;
	nvlist_t *fss = NULL;
	avl_tree_t *fsavl = NULL;

	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "cannot send '%s'"), zhp->zfs_name);

	if (fromsnap && fromsnap[0] == '\0') {
		zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
		    "zero-length incremental source"));
		return (zfs_error(zhp->zfs_hdl, EZFS_NOENT, errbuf));
	}

	if (replicate || doall) {
		dmu_replay_record_t drr = { 0 };
		char *packbuf = NULL;
		size_t buflen = 0;
		zio_cksum_t zc = { 0 };

		assert(fromsnap || doall);

		if (replicate) {
			nvlist_t *hdrnv;

			VERIFY(0 == nvlist_alloc(&hdrnv, NV_UNIQUE_NAME, 0));
			if (fromsnap) {
				VERIFY(0 == nvlist_add_string(hdrnv,
				    "fromsnap", fromsnap));
			}
			VERIFY(0 == nvlist_add_string(hdrnv, "tosnap", tosnap));

			err = gather_nvlist(zhp->zfs_hdl, zhp->zfs_name,
			    fromsnap, tosnap, &fss, &fsavl);
			if (err)
				return (err);
			VERIFY(0 == nvlist_add_nvlist(hdrnv, "fss", fss));
			err = nvlist_pack(hdrnv, &packbuf, &buflen,
			    NV_ENCODE_XDR, 0);
			nvlist_free(hdrnv);
			if (err) {
				fsavl_destroy(fsavl);
				nvlist_free(fss);
				return (zfs_standard_error(zhp->zfs_hdl,
				    err, errbuf));
			}
		}

		/* write first begin record */
		drr.drr_type = DRR_BEGIN;
		drr.drr_u.drr_begin.drr_magic = DMU_BACKUP_MAGIC;
		drr.drr_u.drr_begin.drr_version = DMU_BACKUP_HEADER_VERSION;
		(void) snprintf(drr.drr_u.drr_begin.drr_toname,
		    sizeof (drr.drr_u.drr_begin.drr_toname),
		    "%s@%s", zhp->zfs_name, tosnap);
		drr.drr_payloadlen = buflen;
		fletcher_4_incremental_native(&drr, sizeof (drr), &zc);
		err = write(outfd, &drr, sizeof (drr));

		/* write header nvlist */
		if (err != -1) {
			fletcher_4_incremental_native(packbuf, buflen, &zc);
			err = write(outfd, packbuf, buflen);
		}
		free(packbuf);
		if (err == -1) {
			fsavl_destroy(fsavl);
			nvlist_free(fss);
			return (zfs_standard_error(zhp->zfs_hdl,
			    errno, errbuf));
		}

		/* write end record */
		if (err != -1) {
			bzero(&drr, sizeof (drr));
			drr.drr_type = DRR_END;
			drr.drr_u.drr_end.drr_checksum = zc;
			err = write(outfd, &drr, sizeof (drr));
			if (err == -1) {
				fsavl_destroy(fsavl);
				nvlist_free(fss);
				return (zfs_standard_error(zhp->zfs_hdl,
				    errno, errbuf));
			}
		}
	}

	/* dump each stream */
	sdd.fromsnap = fromsnap;
	sdd.tosnap = tosnap;
	sdd.outfd = outfd;
	sdd.replicate = replicate;
	sdd.doall = doall;
	sdd.fromorigin = fromorigin;
	sdd.fss = fss;
	sdd.fsavl = fsavl;
	sdd.verbose = verbose;
	err = dump_filesystems(zhp, &sdd);
	fsavl_destroy(fsavl);
	nvlist_free(fss);

	if (replicate || doall) {
		/*
		 * write final end record.  NB: want to do this even if
		 * there was some error, because it might not be totally
		 * failed.
		 */
		dmu_replay_record_t drr = { 0 };
		drr.drr_type = DRR_END;
		if (write(outfd, &drr, sizeof (drr)) == -1) {
			return (zfs_standard_error(zhp->zfs_hdl,
			    errno, errbuf));
		}
	}

	return (err || sdd.err);
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
	if (buf == NULL)
		return (ENOMEM);

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

static int
recv_rename(libzfs_handle_t *hdl, const char *name, const char *tryname,
    int baselen, char *newname, recvflags_t flags)
{
	static int seq;
	zfs_cmd_t zc = { 0 };
	int err;
	prop_changelist_t *clp;
	zfs_handle_t *zhp;

	zhp = zfs_open(hdl, name, ZFS_TYPE_DATASET);
	if (zhp == NULL)
		return (-1);
	clp = changelist_gather(zhp, ZFS_PROP_NAME, 0,
	    flags.force ? MS_FORCE : 0);
	zfs_close(zhp);
	if (clp == NULL)
		return (-1);
	err = changelist_prefix(clp);
	if (err)
		return (err);

	if (tryname) {
		(void) strcpy(newname, tryname);

		zc.zc_objset_type = DMU_OST_ZFS;
		(void) strlcpy(zc.zc_name, name, sizeof (zc.zc_name));
		(void) strlcpy(zc.zc_value, tryname, sizeof (zc.zc_value));

		if (flags.verbose) {
			(void) printf("attempting rename %s to %s\n",
			    zc.zc_name, zc.zc_value);
		}
		err = ioctl(hdl->libzfs_fd, ZFS_IOC_RENAME, &zc);
		if (err == 0)
			changelist_rename(clp, name, tryname);
	} else {
		err = ENOENT;
	}

	if (err != 0 && strncmp(name+baselen, "recv-", 5) != 0) {
		seq++;

		(void) strncpy(newname, name, baselen);
		(void) snprintf(newname+baselen, ZFS_MAXNAMELEN-baselen,
		    "recv-%u-%u", getpid(), seq);
		(void) strlcpy(zc.zc_value, newname, sizeof (zc.zc_value));

		if (flags.verbose) {
			(void) printf("failed - trying rename %s to %s\n",
			    zc.zc_name, zc.zc_value);
		}
		err = ioctl(hdl->libzfs_fd, ZFS_IOC_RENAME, &zc);
		if (err == 0)
			changelist_rename(clp, name, newname);
		if (err && flags.verbose) {
			(void) printf("failed (%u) - "
			    "will try again on next pass\n", errno);
		}
		err = EAGAIN;
	} else if (flags.verbose) {
		if (err == 0)
			(void) printf("success\n");
		else
			(void) printf("failed (%u)\n", errno);
	}

	(void) changelist_postfix(clp);
	changelist_free(clp);

	return (err);
}

static int
recv_destroy(libzfs_handle_t *hdl, const char *name, int baselen,
    char *newname, recvflags_t flags)
{
	zfs_cmd_t zc = { 0 };
	int err = 0;
	prop_changelist_t *clp;
	zfs_handle_t *zhp;

	zhp = zfs_open(hdl, name, ZFS_TYPE_DATASET);
	if (zhp == NULL)
		return (-1);
	clp = changelist_gather(zhp, ZFS_PROP_NAME, 0,
	    flags.force ? MS_FORCE : 0);
	zfs_close(zhp);
	if (clp == NULL)
		return (-1);
	err = changelist_prefix(clp);
	if (err)
		return (err);

	zc.zc_objset_type = DMU_OST_ZFS;
	(void) strlcpy(zc.zc_name, name, sizeof (zc.zc_name));

	if (flags.verbose)
		(void) printf("attempting destroy %s\n", zc.zc_name);
	err = ioctl(hdl->libzfs_fd, ZFS_IOC_DESTROY, &zc);

	if (err == 0) {
		if (flags.verbose)
			(void) printf("success\n");
		changelist_remove(clp, zc.zc_name);
	}

	(void) changelist_postfix(clp);
	changelist_free(clp);

	if (err != 0)
		err = recv_rename(hdl, name, NULL, baselen, newname, flags);

	return (err);
}

typedef struct guid_to_name_data {
	uint64_t guid;
	char *name;
} guid_to_name_data_t;

static int
guid_to_name_cb(zfs_handle_t *zhp, void *arg)
{
	guid_to_name_data_t *gtnd = arg;
	int err;

	if (zhp->zfs_dmustats.dds_guid == gtnd->guid) {
		(void) strcpy(gtnd->name, zhp->zfs_name);
		return (EEXIST);
	}
	err = zfs_iter_children(zhp, guid_to_name_cb, gtnd);
	zfs_close(zhp);
	return (err);
}

static int
guid_to_name(libzfs_handle_t *hdl, const char *parent, uint64_t guid,
    char *name)
{
	/* exhaustive search all local snapshots */
	guid_to_name_data_t gtnd;
	int err = 0;
	zfs_handle_t *zhp;
	char *cp;

	gtnd.guid = guid;
	gtnd.name = name;

	if (strchr(parent, '@') == NULL) {
		zhp = make_dataset_handle(hdl, parent);
		if (zhp != NULL) {
			err = zfs_iter_children(zhp, guid_to_name_cb, &gtnd);
			zfs_close(zhp);
			if (err == EEXIST)
				return (0);
		}
	}

	cp = strchr(parent, '/');
	if (cp)
		*cp = '\0';
	zhp = make_dataset_handle(hdl, parent);
	if (cp)
		*cp = '/';

	if (zhp) {
		err = zfs_iter_children(zhp, guid_to_name_cb, &gtnd);
		zfs_close(zhp);
	}

	return (err == EEXIST ? 0 : ENOENT);

}

/*
 * Return true if dataset guid1 is created before guid2.
 */
static int
created_before(libzfs_handle_t *hdl, avl_tree_t *avl,
    uint64_t guid1, uint64_t guid2)
{
	nvlist_t *nvfs;
	char *fsname, *snapname;
	char buf[ZFS_MAXNAMELEN];
	int rv;
	zfs_node_t zn1, zn2;

	if (guid2 == 0)
		return (0);
	if (guid1 == 0)
		return (1);

	nvfs = fsavl_find(avl, guid1, &snapname);
	VERIFY(0 == nvlist_lookup_string(nvfs, "name", &fsname));
	(void) snprintf(buf, sizeof (buf), "%s@%s", fsname, snapname);
	zn1.zn_handle = zfs_open(hdl, buf, ZFS_TYPE_SNAPSHOT);
	if (zn1.zn_handle == NULL)
		return (-1);

	nvfs = fsavl_find(avl, guid2, &snapname);
	VERIFY(0 == nvlist_lookup_string(nvfs, "name", &fsname));
	(void) snprintf(buf, sizeof (buf), "%s@%s", fsname, snapname);
	zn2.zn_handle = zfs_open(hdl, buf, ZFS_TYPE_SNAPSHOT);
	if (zn2.zn_handle == NULL) {
		zfs_close(zn2.zn_handle);
		return (-1);
	}

	rv = (zfs_snapshot_compare(&zn1, &zn2) == -1);

	zfs_close(zn1.zn_handle);
	zfs_close(zn2.zn_handle);

	return (rv);
}

static int
recv_incremental_replication(libzfs_handle_t *hdl, const char *tofs,
    recvflags_t flags, nvlist_t *stream_nv, avl_tree_t *stream_avl)
{
	nvlist_t *local_nv;
	avl_tree_t *local_avl;
	nvpair_t *fselem, *nextfselem;
	char *tosnap, *fromsnap;
	char newname[ZFS_MAXNAMELEN];
	int error;
	boolean_t needagain, progress;

	VERIFY(0 == nvlist_lookup_string(stream_nv, "fromsnap", &fromsnap));
	VERIFY(0 == nvlist_lookup_string(stream_nv, "tosnap", &tosnap));

	if (flags.dryrun)
		return (0);

again:
	needagain = progress = B_FALSE;

	if ((error = gather_nvlist(hdl, tofs, fromsnap, NULL,
	    &local_nv, &local_avl)) != 0)
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
		char *fsname, *stream_fsname;

		nextfselem = nvlist_next_nvpair(local_nv, fselem);

		VERIFY(0 == nvpair_value_nvlist(fselem, &nvfs));
		VERIFY(0 == nvlist_lookup_nvlist(nvfs, "snaps", &snaps));
		VERIFY(0 == nvlist_lookup_string(nvfs, "name", &fsname));
		VERIFY(0 == nvlist_lookup_uint64(nvfs, "parentfromsnap",
		    &parent_fromsnap_guid));
		(void) nvlist_lookup_uint64(nvfs, "origin", &originguid);

		/*
		 * First find the stream's fs, so we can check for
		 * a different origin (due to "zfs promote")
		 */
		for (snapelem = nvlist_next_nvpair(snaps, NULL);
		    snapelem; snapelem = nvlist_next_nvpair(snaps, snapelem)) {
			uint64_t thisguid;

			VERIFY(0 == nvpair_value_uint64(snapelem, &thisguid));
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
				zfs_cmd_t zc = { 0 };
				nvlist_t *origin_nvfs;
				char *origin_fsname;

				if (flags.verbose)
					(void) printf("promoting %s\n", fsname);

				origin_nvfs = fsavl_find(local_avl, originguid,
				    NULL);
				VERIFY(0 == nvlist_lookup_string(origin_nvfs,
				    "name", &origin_fsname));
				(void) strlcpy(zc.zc_value, origin_fsname,
				    sizeof (zc.zc_value));
				(void) strlcpy(zc.zc_name, fsname,
				    sizeof (zc.zc_name));
				error = zfs_ioctl(hdl, ZFS_IOC_PROMOTE, &zc);
				if (error == 0)
					progress = B_TRUE;
				break;
			}
			default:
				break;
			case -1:
				fsavl_destroy(local_avl);
				nvlist_free(local_nv);
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
			char *stream_snapname;
			nvlist_t *found, *props;

			nextsnapelem = nvlist_next_nvpair(snaps, snapelem);

			VERIFY(0 == nvpair_value_uint64(snapelem, &thisguid));
			found = fsavl_find(stream_avl, thisguid,
			    &stream_snapname);

			/* check for delete */
			if (found == NULL) {
				char name[ZFS_MAXNAMELEN];

				if (!flags.force)
					continue;

				(void) snprintf(name, sizeof (name), "%s@%s",
				    fsname, nvpair_name(snapelem));

				error = recv_destroy(hdl, name,
				    strlen(fsname)+1, newname, flags);
				if (error)
					needagain = B_TRUE;
				else
					progress = B_TRUE;
				continue;
			}

			stream_nvfs = found;

			if (0 == nvlist_lookup_nvlist(stream_nvfs, "snapprops",
			    &props) && 0 == nvlist_lookup_nvlist(props,
			    stream_snapname, &props)) {
				zfs_cmd_t zc = { 0 };

				zc.zc_cookie = B_TRUE; /* clear current props */
				(void) snprintf(zc.zc_name, sizeof (zc.zc_name),
				    "%s@%s", fsname, nvpair_name(snapelem));
				if (zcmd_write_src_nvlist(hdl, &zc,
				    props) == 0) {
					(void) zfs_ioctl(hdl,
					    ZFS_IOC_SET_PROP, &zc);
					zcmd_free_nvlists(&zc);
				}
			}

			/* check for different snapname */
			if (strcmp(nvpair_name(snapelem),
			    stream_snapname) != 0) {
				char name[ZFS_MAXNAMELEN];
				char tryname[ZFS_MAXNAMELEN];

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
			if (!flags.force)
				continue;

			error = recv_destroy(hdl, fsname, strlen(tofs)+1,
			    newname, flags);
			if (error)
				needagain = B_TRUE;
			else
				progress = B_TRUE;
			continue;
		}

		if (fromguid == 0 && flags.verbose) {
			(void) printf("local fs %s does not have fromsnap "
			    "(%s in stream); must have been deleted locally; "
			    "ignoring\n", fsname, fromsnap);
			continue;
		}

		VERIFY(0 == nvlist_lookup_string(stream_nvfs,
		    "name", &stream_fsname));
		VERIFY(0 == nvlist_lookup_uint64(stream_nvfs,
		    "parentfromsnap", &stream_parent_fromsnap_guid));

		/* check for rename */
		if ((stream_parent_fromsnap_guid != 0 &&
		    stream_parent_fromsnap_guid != parent_fromsnap_guid) ||
		    strcmp(strrchr(fsname, '/'),
		    strrchr(stream_fsname, '/')) != 0) {
			nvlist_t *parent;
			char tryname[ZFS_MAXNAMELEN];

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
				char *pname;

				VERIFY(0 == nvlist_lookup_string(parent, "name",
				    &pname));
				(void) snprintf(tryname, sizeof (tryname),
				    "%s%s", pname, strrchr(stream_fsname, '/'));
			} else {
				tryname[0] = '\0';
				if (flags.verbose) {
					(void) printf("local fs %s new parent "
					    "not found\n", fsname);
				}
			}

			error = recv_rename(hdl, fsname, tryname,
			    strlen(tofs)+1, newname, flags);
			if (error)
				needagain = B_TRUE;
			else
				progress = B_TRUE;
		}
	}

	fsavl_destroy(local_avl);
	nvlist_free(local_nv);

	if (needagain && progress) {
		/* do another pass to fix up temporary names */
		if (flags.verbose)
			(void) printf("another pass:\n");
		goto again;
	}

	return (needagain);
}

static int
zfs_receive_package(libzfs_handle_t *hdl, int fd, const char *destname,
    recvflags_t flags, dmu_replay_record_t *drr, zio_cksum_t *zc,
    char **top_zfs)
{
	nvlist_t *stream_nv = NULL;
	avl_tree_t *stream_avl = NULL;
	char *fromsnap = NULL;
	char tofs[ZFS_MAXNAMELEN];
	char errbuf[1024];
	dmu_replay_record_t drre;
	int error;
	boolean_t anyerr = B_FALSE;
	boolean_t softerr = B_FALSE;

	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "cannot receive"));

	if (strchr(destname, '@')) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "can not specify snapshot name for multi-snapshot stream"));
		return (zfs_error(hdl, EZFS_BADSTREAM, errbuf));
	}

	assert(drr->drr_type == DRR_BEGIN);
	assert(drr->drr_u.drr_begin.drr_magic == DMU_BACKUP_MAGIC);
	assert(drr->drr_u.drr_begin.drr_version == DMU_BACKUP_HEADER_VERSION);

	/*
	 * Read in the nvlist from the stream.
	 */
	if (drr->drr_payloadlen != 0) {
		if (!flags.isprefix) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "must use -d to receive replication "
			    "(send -R) stream"));
			return (zfs_error(hdl, EZFS_BADSTREAM, errbuf));
		}

		error = recv_read_nvlist(hdl, fd, drr->drr_payloadlen,
		    &stream_nv, flags.byteswap, zc);
		if (error) {
			error = zfs_error(hdl, EZFS_BADSTREAM, errbuf);
			goto out;
		}
	}

	/*
	 * Read in the end record and verify checksum.
	 */
	if (0 != (error = recv_read(hdl, fd, &drre, sizeof (drre),
	    flags.byteswap, NULL)))
		goto out;
	if (flags.byteswap) {
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

		VERIFY(0 == nvlist_lookup_nvlist(stream_nv, "fss",
		    &stream_fss));
		if ((stream_avl = fsavl_create(stream_fss)) == NULL) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "couldn't allocate avl tree"));
			error = zfs_error(hdl, EZFS_NOMEM, errbuf);
			goto out;
		}

		if (fromsnap != NULL) {
			(void) strlcpy(tofs, destname, ZFS_MAXNAMELEN);
			if (flags.isprefix) {
				int i = strcspn(drr->drr_u.drr_begin.drr_toname,
				    "/@");
				/* zfs_receive_one() will create_parents() */
				(void) strlcat(tofs,
				    &drr->drr_u.drr_begin.drr_toname[i],
				    ZFS_MAXNAMELEN);
				*strchr(tofs, '@') = '\0';
			}
			softerr = recv_incremental_replication(hdl, tofs,
			    flags, stream_nv, stream_avl);
		}
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
		error = zfs_receive_impl(hdl, destname, flags, fd,
		    stream_avl, top_zfs);
		if (error == ENODATA) {
			error = 0;
			break;
		}
		anyerr |= error;
	} while (error == 0);

	if (drr->drr_payloadlen != 0 && fromsnap != NULL) {
		/*
		 * Now that we have the fs's they sent us, try the
		 * renames again.
		 */
		softerr = recv_incremental_replication(hdl, tofs, flags,
		    stream_nv, stream_avl);
	}

out:
	fsavl_destroy(stream_avl);
	if (stream_nv)
		nvlist_free(stream_nv);
	if (softerr)
		error = -2;
	if (anyerr)
		error = -1;
	return (error);
}

static int
recv_skip(libzfs_handle_t *hdl, int fd, boolean_t byteswap)
{
	dmu_replay_record_t *drr;
	void *buf = malloc(1<<20);

	/* XXX would be great to use lseek if possible... */
	drr = buf;

	while (recv_read(hdl, fd, drr, sizeof (dmu_replay_record_t),
	    byteswap, NULL) == 0) {
		if (byteswap)
			drr->drr_type = BSWAP_32(drr->drr_type);

		switch (drr->drr_type) {
		case DRR_BEGIN:
			/* NB: not to be used on v2 stream packages */
			assert(drr->drr_payloadlen == 0);
			break;

		case DRR_END:
			free(buf);
			return (0);

		case DRR_OBJECT:
			if (byteswap) {
				drr->drr_u.drr_object.drr_bonuslen =
				    BSWAP_32(drr->drr_u.drr_object.
				    drr_bonuslen);
			}
			(void) recv_read(hdl, fd, buf,
			    P2ROUNDUP(drr->drr_u.drr_object.drr_bonuslen, 8),
			    B_FALSE, NULL);
			break;

		case DRR_WRITE:
			if (byteswap) {
				drr->drr_u.drr_write.drr_length =
				    BSWAP_64(drr->drr_u.drr_write.drr_length);
			}
			(void) recv_read(hdl, fd, buf,
			    drr->drr_u.drr_write.drr_length, B_FALSE, NULL);
			break;

		case DRR_FREEOBJECTS:
		case DRR_FREE:
			break;

		default:
			assert(!"invalid record type");
		}
	}

	free(buf);
	return (-1);
}

/*
 * Restores a backup of tosnap from the file descriptor specified by infd.
 */
static int
zfs_receive_one(libzfs_handle_t *hdl, int infd, const char *tosnap,
    recvflags_t flags, dmu_replay_record_t *drr,
    dmu_replay_record_t *drr_noswap, avl_tree_t *stream_avl,
    char **top_zfs)
{
	zfs_cmd_t zc = { 0 };
	time_t begin_time;
	int ioctl_err, ioctl_errno, err, choplen;
	char *cp;
	struct drr_begin *drrb = &drr->drr_u.drr_begin;
	char errbuf[1024];
	char chopprefix[ZFS_MAXNAMELEN];
	boolean_t newfs = B_FALSE;
	boolean_t stream_wantsnewfs;
	uint64_t parent_snapguid = 0;
	prop_changelist_t *clp = NULL;
	nvlist_t *snapprops_nvlist = NULL;

	begin_time = time(NULL);

	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "cannot receive"));

	if (stream_avl != NULL) {
		char *snapname;
		nvlist_t *fs = fsavl_find(stream_avl, drrb->drr_toguid,
		    &snapname);
		nvlist_t *props;
		int ret;

		(void) nvlist_lookup_uint64(fs, "parentfromsnap",
		    &parent_snapguid);
		err = nvlist_lookup_nvlist(fs, "props", &props);
		if (err)
			VERIFY(0 == nvlist_alloc(&props, NV_UNIQUE_NAME, 0));

		if (flags.canmountoff) {
			VERIFY(0 == nvlist_add_uint64(props,
			    zfs_prop_to_name(ZFS_PROP_CANMOUNT), 0));
		}
		ret = zcmd_write_src_nvlist(hdl, &zc, props);
		if (err)
			nvlist_free(props);

		if (0 == nvlist_lookup_nvlist(fs, "snapprops", &props)) {
			VERIFY(0 == nvlist_lookup_nvlist(props,
			    snapname, &snapprops_nvlist));
		}

		if (ret != 0)
			return (-1);
	}

	/*
	 * Determine how much of the snapshot name stored in the stream
	 * we are going to tack on to the name they specified on the
	 * command line, and how much we are going to chop off.
	 *
	 * If they specified a snapshot, chop the entire name stored in
	 * the stream.
	 */
	(void) strcpy(chopprefix, drrb->drr_toname);
	if (flags.isprefix) {
		/*
		 * They specified a fs with -d, we want to tack on
		 * everything but the pool name stored in the stream
		 */
		if (strchr(tosnap, '@')) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, "invalid "
			    "argument - snapshot not allowed with -d"));
			return (zfs_error(hdl, EZFS_INVALIDNAME, errbuf));
		}
		cp = strchr(chopprefix, '/');
		if (cp == NULL)
			cp = strchr(chopprefix, '@');
		*cp = '\0';
	} else if (strchr(tosnap, '@') == NULL) {
		/*
		 * If they specified a filesystem without -d, we want to
		 * tack on everything after the fs specified in the
		 * first name from the stream.
		 */
		cp = strchr(chopprefix, '@');
		*cp = '\0';
	}
	choplen = strlen(chopprefix);

	/*
	 * Determine name of destination snapshot, store in zc_value.
	 */
	(void) strcpy(zc.zc_value, tosnap);
	(void) strncat(zc.zc_value, drrb->drr_toname+choplen,
	    sizeof (zc.zc_value));
	if (!zfs_name_valid(zc.zc_value, ZFS_TYPE_SNAPSHOT)) {
		zcmd_free_nvlists(&zc);
		return (zfs_error(hdl, EZFS_INVALIDNAME, errbuf));
	}

	/*
	 * Determine the name of the origin snapshot, store in zc_string.
	 */
	if (drrb->drr_flags & DRR_FLAG_CLONE) {
		if (guid_to_name(hdl, tosnap,
		    drrb->drr_fromguid, zc.zc_string) != 0) {
			zcmd_free_nvlists(&zc);
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "local origin for clone %s does not exist"),
			    zc.zc_value);
			return (zfs_error(hdl, EZFS_NOENT, errbuf));
		}
		if (flags.verbose)
			(void) printf("found clone origin %s\n", zc.zc_string);
	}

	stream_wantsnewfs = (drrb->drr_fromguid == NULL ||
	    (drrb->drr_flags & DRR_FLAG_CLONE));

	if (stream_wantsnewfs) {
		/*
		 * if the parent fs does not exist, look for it based on
		 * the parent snap GUID
		 */
		(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
		    "cannot receive new filesystem stream"));

		(void) strcpy(zc.zc_name, zc.zc_value);
		cp = strrchr(zc.zc_name, '/');
		if (cp)
			*cp = '\0';
		if (cp &&
		    !zfs_dataset_exists(hdl, zc.zc_name, ZFS_TYPE_DATASET)) {
			char suffix[ZFS_MAXNAMELEN];
			(void) strcpy(suffix, strrchr(zc.zc_value, '/'));
			if (guid_to_name(hdl, tosnap, parent_snapguid,
			    zc.zc_value) == 0) {
				*strchr(zc.zc_value, '@') = '\0';
				(void) strcat(zc.zc_value, suffix);
			}
		}
	} else {
		/*
		 * if the fs does not exist, look for it based on the
		 * fromsnap GUID
		 */
		(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
		    "cannot receive incremental stream"));

		(void) strcpy(zc.zc_name, zc.zc_value);
		*strchr(zc.zc_name, '@') = '\0';

		if (!zfs_dataset_exists(hdl, zc.zc_name, ZFS_TYPE_DATASET)) {
			char snap[ZFS_MAXNAMELEN];
			(void) strcpy(snap, strchr(zc.zc_value, '@'));
			if (guid_to_name(hdl, tosnap, drrb->drr_fromguid,
			    zc.zc_value) == 0) {
				*strchr(zc.zc_value, '@') = '\0';
				(void) strcat(zc.zc_value, snap);
			}
		}
	}

	(void) strcpy(zc.zc_name, zc.zc_value);
	*strchr(zc.zc_name, '@') = '\0';

	if (zfs_dataset_exists(hdl, zc.zc_name, ZFS_TYPE_DATASET)) {
		zfs_handle_t *zhp;
		/*
		 * Destination fs exists.  Therefore this should either
		 * be an incremental, or the stream specifies a new fs
		 * (full stream or clone) and they want us to blow it
		 * away (and have therefore specified -F and removed any
		 * snapshots).
		 */

		if (stream_wantsnewfs) {
			if (!flags.force) {
				zcmd_free_nvlists(&zc);
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "destination '%s' exists\n"
				    "must specify -F to overwrite it"),
				    zc.zc_name);
				return (zfs_error(hdl, EZFS_EXISTS, errbuf));
			}
			if (ioctl(hdl->libzfs_fd, ZFS_IOC_SNAPSHOT_LIST_NEXT,
			    &zc) == 0) {
				zcmd_free_nvlists(&zc);
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "destination has snapshots (eg. %s)\n"
				    "must destroy them to overwrite it"),
				    zc.zc_name);
				return (zfs_error(hdl, EZFS_EXISTS, errbuf));
			}
		}

		if ((zhp = zfs_open(hdl, zc.zc_name,
		    ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME)) == NULL) {
			zcmd_free_nvlists(&zc);
			return (-1);
		}

		if (stream_wantsnewfs &&
		    zhp->zfs_dmustats.dds_origin[0]) {
			zcmd_free_nvlists(&zc);
			zfs_close(zhp);
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "destination '%s' is a clone\n"
			    "must destroy it to overwrite it"),
			    zc.zc_name);
			return (zfs_error(hdl, EZFS_EXISTS, errbuf));
		}

		if (!flags.dryrun && zhp->zfs_type == ZFS_TYPE_FILESYSTEM &&
		    stream_wantsnewfs) {
			/* We can't do online recv in this case */
			clp = changelist_gather(zhp, ZFS_PROP_NAME, 0, 0);
			if (clp == NULL) {
				zcmd_free_nvlists(&zc);
				return (-1);
			}
			if (changelist_prefix(clp) != 0) {
				changelist_free(clp);
				zcmd_free_nvlists(&zc);
				return (-1);
			}
		}
		if (!flags.dryrun && zhp->zfs_type == ZFS_TYPE_VOLUME &&
		    zvol_remove_link(hdl, zhp->zfs_name) != 0) {
			zfs_close(zhp);
			zcmd_free_nvlists(&zc);
			return (-1);
		}
		zfs_close(zhp);
	} else {
		/*
		 * Destination filesystem does not exist.  Therefore we better
		 * be creating a new filesystem (either from a full backup, or
		 * a clone).  It would therefore be invalid if the user
		 * specified only the pool name (i.e. if the destination name
		 * contained no slash character).
		 */
		if (!stream_wantsnewfs ||
		    (cp = strrchr(zc.zc_name, '/')) == NULL) {
			zcmd_free_nvlists(&zc);
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "destination '%s' does not exist"), zc.zc_name);
			return (zfs_error(hdl, EZFS_NOENT, errbuf));
		}

		/*
		 * Trim off the final dataset component so we perform the
		 * recvbackup ioctl to the filesystems's parent.
		 */
		*cp = '\0';

		if (flags.isprefix && !flags.dryrun &&
		    create_parents(hdl, zc.zc_value, strlen(tosnap)) != 0) {
			zcmd_free_nvlists(&zc);
			return (zfs_error(hdl, EZFS_BADRESTORE, errbuf));
		}

		newfs = B_TRUE;
	}

	zc.zc_begin_record = drr_noswap->drr_u.drr_begin;
	zc.zc_cookie = infd;
	zc.zc_guid = flags.force;
	if (flags.verbose) {
		(void) printf("%s %s stream of %s into %s\n",
		    flags.dryrun ? "would receive" : "receiving",
		    drrb->drr_fromguid ? "incremental" : "full",
		    drrb->drr_toname, zc.zc_value);
		(void) fflush(stdout);
	}

	if (flags.dryrun) {
		zcmd_free_nvlists(&zc);
		return (recv_skip(hdl, infd, flags.byteswap));
	}

	err = ioctl_err = zfs_ioctl(hdl, ZFS_IOC_RECV, &zc);
	ioctl_errno = errno;
	zcmd_free_nvlists(&zc);

	if (err == 0 && snapprops_nvlist) {
		zfs_cmd_t zc2 = { 0 };

		(void) strcpy(zc2.zc_name, zc.zc_value);
		if (zcmd_write_src_nvlist(hdl, &zc2, snapprops_nvlist) == 0) {
			(void) zfs_ioctl(hdl, ZFS_IOC_SET_PROP, &zc2);
			zcmd_free_nvlists(&zc2);
		}
	}

	if (err && (ioctl_errno == ENOENT || ioctl_errno == ENODEV)) {
		/*
		 * It may be that this snapshot already exists,
		 * in which case we want to consume & ignore it
		 * rather than failing.
		 */
		avl_tree_t *local_avl;
		nvlist_t *local_nv, *fs;
		char *cp = strchr(zc.zc_value, '@');

		/*
		 * XXX Do this faster by just iterating over snaps in
		 * this fs.  Also if zc_value does not exist, we will
		 * get a strange "does not exist" error message.
		 */
		*cp = '\0';
		if (gather_nvlist(hdl, zc.zc_value, NULL, NULL,
		    &local_nv, &local_avl) == 0) {
			*cp = '@';
			fs = fsavl_find(local_avl, drrb->drr_toguid, NULL);
			fsavl_destroy(local_avl);
			nvlist_free(local_nv);

			if (fs != NULL) {
				if (flags.verbose) {
					(void) printf("snap %s already exists; "
					    "ignoring\n", zc.zc_value);
				}
				ioctl_err = recv_skip(hdl, infd,
				    flags.byteswap);
			}
		}
		*cp = '@';
	}


	if (ioctl_err != 0) {
		switch (ioctl_errno) {
		case ENODEV:
			cp = strchr(zc.zc_value, '@');
			*cp = '\0';
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "most recent snapshot of %s does not\n"
			    "match incremental source"), zc.zc_value);
			(void) zfs_error(hdl, EZFS_BADRESTORE, errbuf);
			*cp = '@';
			break;
		case ETXTBSY:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "destination %s has been modified\n"
			    "since most recent snapshot"), zc.zc_name);
			(void) zfs_error(hdl, EZFS_BADRESTORE, errbuf);
			break;
		case EEXIST:
			cp = strchr(zc.zc_value, '@');
			if (newfs) {
				/* it's the containing fs that exists */
				*cp = '\0';
			}
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "destination already exists"));
			(void) zfs_error_fmt(hdl, EZFS_EXISTS,
			    dgettext(TEXT_DOMAIN, "cannot restore to %s"),
			    zc.zc_value);
			*cp = '@';
			break;
		case EINVAL:
			(void) zfs_error(hdl, EZFS_BADSTREAM, errbuf);
			break;
		case ECKSUM:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "invalid stream (checksum mismatch)"));
			(void) zfs_error(hdl, EZFS_BADSTREAM, errbuf);
			break;
		default:
			(void) zfs_standard_error(hdl, ioctl_errno, errbuf);
		}
	}

	/*
	 * Mount or recreate the /dev links for the target filesystem
	 * (if created, or if we tore them down to do an incremental
	 * restore), and the /dev links for the new snapshot (if
	 * created). Also mount any children of the target filesystem
	 * if we did an incremental receive.
	 */
	cp = strchr(zc.zc_value, '@');
	if (cp && (ioctl_err == 0 || !newfs)) {
		zfs_handle_t *h;

		*cp = '\0';
		h = zfs_open(hdl, zc.zc_value,
		    ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME);
		if (h != NULL) {
			if (h->zfs_type == ZFS_TYPE_VOLUME) {
				*cp = '@';
				err = zvol_create_link(hdl, h->zfs_name);
				if (err == 0 && ioctl_err == 0)
					err = zvol_create_link(hdl,
					    zc.zc_value);
			} else if (newfs) {
				/*
				 * Track the first/top of hierarchy fs,
				 * for mounting and sharing later.
				 */
				if (top_zfs && *top_zfs == NULL)
					*top_zfs = zfs_strdup(hdl, zc.zc_value);
			}
			zfs_close(h);
		}
		*cp = '@';
	}

	if (clp) {
		err |= changelist_postfix(clp);
		changelist_free(clp);
	}

	if (err || ioctl_err)
		return (-1);

	if (flags.verbose) {
		char buf1[64];
		char buf2[64];
		uint64_t bytes = zc.zc_cookie;
		time_t delta = time(NULL) - begin_time;
		if (delta == 0)
			delta = 1;
		zfs_nicenum(bytes, buf1, sizeof (buf1));
		zfs_nicenum(bytes/delta, buf2, sizeof (buf1));

		(void) printf("received %sB stream in %lu seconds (%sB/sec)\n",
		    buf1, delta, buf2);
	}

	return (0);
}

static int
zfs_receive_impl(libzfs_handle_t *hdl, const char *tosnap, recvflags_t flags,
    int infd, avl_tree_t *stream_avl, char **top_zfs)
{
	int err;
	dmu_replay_record_t drr, drr_noswap;
	struct drr_begin *drrb = &drr.drr_u.drr_begin;
	char errbuf[1024];
	zio_cksum_t zcksum = { 0 };

	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "cannot receive"));

	if (flags.isprefix &&
	    !zfs_dataset_exists(hdl, tosnap, ZFS_TYPE_DATASET)) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, "specified fs "
		    "(%s) does not exist"), tosnap);
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

	flags.byteswap = B_FALSE;
	if (drrb->drr_magic == BSWAP_64(DMU_BACKUP_MAGIC)) {
		/*
		 * We computed the checksum in the wrong byteorder in
		 * recv_read() above; do it again correctly.
		 */
		bzero(&zcksum, sizeof (zio_cksum_t));
		fletcher_4_incremental_byteswap(&drr, sizeof (drr), &zcksum);
		flags.byteswap = B_TRUE;

		drr.drr_type = BSWAP_32(drr.drr_type);
		drr.drr_payloadlen = BSWAP_32(drr.drr_payloadlen);
		drrb->drr_magic = BSWAP_64(drrb->drr_magic);
		drrb->drr_version = BSWAP_64(drrb->drr_version);
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

	if (strchr(drrb->drr_toname, '@') == NULL) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, "invalid "
		    "stream (bad snapshot name)"));
		return (zfs_error(hdl, EZFS_BADSTREAM, errbuf));
	}

	if (drrb->drr_version == DMU_BACKUP_STREAM_VERSION) {
		return (zfs_receive_one(hdl, infd, tosnap, flags,
		    &drr, &drr_noswap, stream_avl, top_zfs));
	} else if (drrb->drr_version == DMU_BACKUP_HEADER_VERSION) {
		return (zfs_receive_package(hdl, infd, tosnap, flags,
		    &drr, &zcksum, top_zfs));
	} else {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "stream is unsupported version %llu"),
		    drrb->drr_version);
		return (zfs_error(hdl, EZFS_BADSTREAM, errbuf));
	}
}

/*
 * Restores a backup of tosnap from the file descriptor specified by infd.
 * Return 0 on total success, -2 if some things couldn't be
 * destroyed/renamed/promoted, -1 if some things couldn't be received.
 * (-1 will override -2).
 */
int
zfs_receive(libzfs_handle_t *hdl, const char *tosnap, recvflags_t flags,
    int infd, avl_tree_t *stream_avl)
{
	char *top_zfs = NULL;
	int err;

	err = zfs_receive_impl(hdl, tosnap, flags, infd, stream_avl, &top_zfs);

	if (err == 0 && top_zfs) {
		zfs_handle_t *zhp;
		prop_changelist_t *clp;

		zhp = zfs_open(hdl, top_zfs, ZFS_TYPE_FILESYSTEM);
		if (zhp != NULL) {
			clp = changelist_gather(zhp, ZFS_PROP_MOUNTPOINT,
			    CL_GATHER_MOUNT_ALWAYS, 0);
			zfs_close(zhp);
			if (clp != NULL) {
				/* mount and share received datasets */
				err = changelist_postfix(clp);
				changelist_free(clp);
			}
		}
		if (zhp == NULL || clp == NULL || err)
			err = -1;
	}
	if (top_zfs)
		free(top_zfs);

	return (err);
}
