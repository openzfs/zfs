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
 * Copyright (c) 2012 by Delphix. All rights reserved.
 * Copyright (c) 2012 Pawel Jakub Dawidek <pawel@dawidek.net>.
 * Copyright (c) 2012, Joyent, Inc. All rights reserved.
 * All rights reserved
 */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
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
#include <sys/debug.h>
#include <stddef.h>
#include <pthread.h>
#include <umem.h>
#include <time.h>

#include <libzfs.h>

#include "zfs_namecheck.h"
#include "zfs_prop.h"
#include "zfs_fletcher.h"
#include "libzfs_impl.h"
#include <sys/zio_checksum.h>
#include <sys/ddt.h>
#include <sys/socket.h>

/* in libzfs_dataset.c */
extern void zfs_setprop_error(libzfs_handle_t *, zfs_prop_t, int, char *);

static int zfs_receive_impl(libzfs_handle_t *, const char *, recvflags_t *,
    int, const char *, nvlist_t *, avl_tree_t *, char **, int, uint64_t *);

static const zio_cksum_t zero_cksum = { { 0 } };

typedef struct dedup_arg {
	int	inputfd;
	int	outputfd;
	libzfs_handle_t  *dedup_hdl;
} dedup_arg_t;

typedef struct progress_arg {
	zfs_handle_t *pa_zhp;
	int pa_fd;
	boolean_t pa_parsable;
} progress_arg_t;

typedef struct dataref {
	uint64_t ref_guid;
	uint64_t ref_object;
	uint64_t ref_offset;
} dataref_t;

typedef struct dedup_entry {
	struct dedup_entry	*dde_next;
	zio_cksum_t dde_chksum;
	uint64_t dde_prop;
	dataref_t dde_ref;
} dedup_entry_t;

#define	MAX_DDT_PHYSMEM_PERCENT		20
#define	SMALLEST_POSSIBLE_MAX_DDT_MB		128

typedef struct dedup_table {
	dedup_entry_t	**dedup_hash_array;
	umem_cache_t	*ddecache;
	uint64_t	max_ddt_size;  /* max dedup table size in bytes */
	uint64_t	cur_ddt_size;  /* current dedup table size in bytes */
	uint64_t	ddt_count;
	int		numhashbits;
	boolean_t	ddt_full;
} dedup_table_t;

static int
high_order_bit(uint64_t n)
{
	int count;

	for (count = 0; n != 0; count++)
		n >>= 1;
	return (count);
}

static size_t
ssread(void *buf, size_t len, FILE *stream)
{
	size_t outlen;

	if ((outlen = fread(buf, len, 1, stream)) == 0)
		return (0);

	return (outlen);
}

static void
ddt_hash_append(libzfs_handle_t *hdl, dedup_table_t *ddt, dedup_entry_t **ddepp,
    zio_cksum_t *cs, uint64_t prop, dataref_t *dr)
{
	dedup_entry_t	*dde;

	if (ddt->cur_ddt_size >= ddt->max_ddt_size) {
		if (ddt->ddt_full == B_FALSE) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "Dedup table full.  Deduplication will continue "
			    "with existing table entries"));
			ddt->ddt_full = B_TRUE;
		}
		return;
	}

	if ((dde = umem_cache_alloc(ddt->ddecache, UMEM_DEFAULT))
	    != NULL) {
		assert(*ddepp == NULL);
		dde->dde_next = NULL;
		dde->dde_chksum = *cs;
		dde->dde_prop = prop;
		dde->dde_ref = *dr;
		*ddepp = dde;
		ddt->cur_ddt_size += sizeof (dedup_entry_t);
		ddt->ddt_count++;
	}
}

/*
 * Using the specified dedup table, do a lookup for an entry with
 * the checksum cs.  If found, return the block's reference info
 * in *dr. Otherwise, insert a new entry in the dedup table, using
 * the reference information specified by *dr.
 *
 * return value:  true - entry was found
 *		  false - entry was not found
 */
static boolean_t
ddt_update(libzfs_handle_t *hdl, dedup_table_t *ddt, zio_cksum_t *cs,
    uint64_t prop, dataref_t *dr)
{
	uint32_t hashcode;
	dedup_entry_t **ddepp;

	hashcode = BF64_GET(cs->zc_word[0], 0, ddt->numhashbits);

	for (ddepp = &(ddt->dedup_hash_array[hashcode]); *ddepp != NULL;
	    ddepp = &((*ddepp)->dde_next)) {
		if (ZIO_CHECKSUM_EQUAL(((*ddepp)->dde_chksum), *cs) &&
		    (*ddepp)->dde_prop == prop) {
			*dr = (*ddepp)->dde_ref;
			return (B_TRUE);
		}
	}
	ddt_hash_append(hdl, ddt, ddepp, cs, prop, dr);
	return (B_FALSE);
}

static int
cksum_and_write(const void *buf, uint64_t len, zio_cksum_t *zc, int outfd)
{
	fletcher_4_incremental_native(buf, len, zc);
	return (write(outfd, buf, len));
}

/*
 * This function is started in a separate thread when the dedup option
 * has been requested.  The main send thread determines the list of
 * snapshots to be included in the send stream and makes the ioctl calls
 * for each one.  But instead of having the ioctl send the output to the
 * the output fd specified by the caller of zfs_send()), the
 * ioctl is told to direct the output to a pipe, which is read by the
 * alternate thread running THIS function.  This function does the
 * dedup'ing by:
 *  1. building a dedup table (the DDT)
 *  2. doing checksums on each data block and inserting a record in the DDT
 *  3. looking for matching checksums, and
 *  4.  sending a DRR_WRITE_BYREF record instead of a write record whenever
 *      a duplicate block is found.
 * The output of this function then goes to the output fd requested
 * by the caller of zfs_send().
 */
static void *
cksummer(void *arg)
{
	dedup_arg_t *dda = arg;
	char *buf = malloc(1<<20);
	dmu_replay_record_t thedrr;
	dmu_replay_record_t *drr = &thedrr;
	struct drr_begin *drrb = &thedrr.drr_u.drr_begin;
	struct drr_end *drre = &thedrr.drr_u.drr_end;
	struct drr_object *drro = &thedrr.drr_u.drr_object;
	struct drr_write *drrw = &thedrr.drr_u.drr_write;
	struct drr_spill *drrs = &thedrr.drr_u.drr_spill;
	FILE *ofp;
	int outfd;
	dmu_replay_record_t wbr_drr = {0};
	struct drr_write_byref *wbr_drrr = &wbr_drr.drr_u.drr_write_byref;
	dedup_table_t ddt;
	zio_cksum_t stream_cksum;
	uint64_t physmem = sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGESIZE);
	uint64_t numbuckets;

	ddt.max_ddt_size =
	    MAX((physmem * MAX_DDT_PHYSMEM_PERCENT)/100,
	    SMALLEST_POSSIBLE_MAX_DDT_MB<<20);

	numbuckets = ddt.max_ddt_size/(sizeof (dedup_entry_t));

	/*
	 * numbuckets must be a power of 2.  Increase number to
	 * a power of 2 if necessary.
	 */
	if (!ISP2(numbuckets))
		numbuckets = 1 << high_order_bit(numbuckets);

	ddt.dedup_hash_array = calloc(numbuckets, sizeof (dedup_entry_t *));
	ddt.ddecache = umem_cache_create("dde", sizeof (dedup_entry_t), 0,
	    NULL, NULL, NULL, NULL, NULL, 0);
	ddt.cur_ddt_size = numbuckets * sizeof (dedup_entry_t *);
	ddt.numhashbits = high_order_bit(numbuckets) - 1;
	ddt.ddt_full = B_FALSE;

	/* Initialize the write-by-reference block. */
	wbr_drr.drr_type = DRR_WRITE_BYREF;
	wbr_drr.drr_payloadlen = 0;

	outfd = dda->outputfd;
	ofp = fdopen(dda->inputfd, "r");
	while (ssread(drr, sizeof (dmu_replay_record_t), ofp) != 0) {

		switch (drr->drr_type) {
		case DRR_BEGIN:
		{
			int	fflags;
			ZIO_SET_CHECKSUM(&stream_cksum, 0, 0, 0, 0);

			/* set the DEDUP feature flag for this stream */
			fflags = DMU_GET_FEATUREFLAGS(drrb->drr_versioninfo);
			fflags |= (DMU_BACKUP_FEATURE_DEDUP |
			    DMU_BACKUP_FEATURE_DEDUPPROPS);
			DMU_SET_FEATUREFLAGS(drrb->drr_versioninfo, fflags);

			if (cksum_and_write(drr, sizeof (dmu_replay_record_t),
			    &stream_cksum, outfd) == -1)
				goto out;
			if (DMU_GET_STREAM_HDRTYPE(drrb->drr_versioninfo) ==
			    DMU_COMPOUNDSTREAM && drr->drr_payloadlen != 0) {
				int sz = drr->drr_payloadlen;

				if (sz > 1<<20) {
					free(buf);
					buf = malloc(sz);
				}
				(void) ssread(buf, sz, ofp);
				if (ferror(stdin))
					perror("fread");
				if (cksum_and_write(buf, sz, &stream_cksum,
				    outfd) == -1)
					goto out;
			}
			break;
		}

		case DRR_END:
		{
			/* use the recalculated checksum */
			ZIO_SET_CHECKSUM(&drre->drr_checksum,
			    stream_cksum.zc_word[0], stream_cksum.zc_word[1],
			    stream_cksum.zc_word[2], stream_cksum.zc_word[3]);
			if ((write(outfd, drr,
			    sizeof (dmu_replay_record_t))) == -1)
				goto out;
			break;
		}

		case DRR_OBJECT:
		{
			if (cksum_and_write(drr, sizeof (dmu_replay_record_t),
			    &stream_cksum, outfd) == -1)
				goto out;
			if (drro->drr_bonuslen > 0) {
				(void) ssread(buf,
				    P2ROUNDUP((uint64_t)drro->drr_bonuslen, 8),
				    ofp);
				if (cksum_and_write(buf,
				    P2ROUNDUP((uint64_t)drro->drr_bonuslen, 8),
				    &stream_cksum, outfd) == -1)
					goto out;
			}
			break;
		}

		case DRR_SPILL:
		{
			if (cksum_and_write(drr, sizeof (dmu_replay_record_t),
			    &stream_cksum, outfd) == -1)
				goto out;
			(void) ssread(buf, drrs->drr_length, ofp);
			if (cksum_and_write(buf, drrs->drr_length,
			    &stream_cksum, outfd) == -1)
				goto out;
			break;
		}

		case DRR_FREEOBJECTS:
		{
			if (cksum_and_write(drr, sizeof (dmu_replay_record_t),
			    &stream_cksum, outfd) == -1)
				goto out;
			break;
		}

		case DRR_WRITE:
		{
			dataref_t	dataref;

			(void) ssread(buf, drrw->drr_length, ofp);

			/*
			 * Use the existing checksum if it's dedup-capable,
			 * else calculate a SHA256 checksum for it.
			 */

			if (ZIO_CHECKSUM_EQUAL(drrw->drr_key.ddk_cksum,
			    zero_cksum) ||
			    !DRR_IS_DEDUP_CAPABLE(drrw->drr_checksumflags)) {
				zio_cksum_t tmpsha256;

				zio_checksum_SHA256(buf,
				    drrw->drr_length, &tmpsha256);

				drrw->drr_key.ddk_cksum.zc_word[0] =
				    BE_64(tmpsha256.zc_word[0]);
				drrw->drr_key.ddk_cksum.zc_word[1] =
				    BE_64(tmpsha256.zc_word[1]);
				drrw->drr_key.ddk_cksum.zc_word[2] =
				    BE_64(tmpsha256.zc_word[2]);
				drrw->drr_key.ddk_cksum.zc_word[3] =
				    BE_64(tmpsha256.zc_word[3]);
				drrw->drr_checksumtype = ZIO_CHECKSUM_SHA256;
				drrw->drr_checksumflags = DRR_CHECKSUM_DEDUP;
			}

			dataref.ref_guid = drrw->drr_toguid;
			dataref.ref_object = drrw->drr_object;
			dataref.ref_offset = drrw->drr_offset;

			if (ddt_update(dda->dedup_hdl, &ddt,
			    &drrw->drr_key.ddk_cksum, drrw->drr_key.ddk_prop,
			    &dataref)) {
				/* block already present in stream */
				wbr_drrr->drr_object = drrw->drr_object;
				wbr_drrr->drr_offset = drrw->drr_offset;
				wbr_drrr->drr_length = drrw->drr_length;
				wbr_drrr->drr_toguid = drrw->drr_toguid;
				wbr_drrr->drr_refguid = dataref.ref_guid;
				wbr_drrr->drr_refobject =
				    dataref.ref_object;
				wbr_drrr->drr_refoffset =
				    dataref.ref_offset;

				wbr_drrr->drr_checksumtype =
				    drrw->drr_checksumtype;
				wbr_drrr->drr_checksumflags =
				    drrw->drr_checksumtype;
				wbr_drrr->drr_key.ddk_cksum =
				    drrw->drr_key.ddk_cksum;
				wbr_drrr->drr_key.ddk_prop =
				    drrw->drr_key.ddk_prop;

				if (cksum_and_write(&wbr_drr,
				    sizeof (dmu_replay_record_t), &stream_cksum,
				    outfd) == -1)
					goto out;
			} else {
				/* block not previously seen */
				if (cksum_and_write(drr,
				    sizeof (dmu_replay_record_t), &stream_cksum,
				    outfd) == -1)
					goto out;
				if (cksum_and_write(buf,
				    drrw->drr_length,
				    &stream_cksum, outfd) == -1)
					goto out;
			}
			break;
		}

		case DRR_FREE:
		{
			if (cksum_and_write(drr, sizeof (dmu_replay_record_t),
			    &stream_cksum, outfd) == -1)
				goto out;
			break;
		}

		default:
			(void) printf("INVALID record type 0x%x\n",
			    drr->drr_type);
			/* should never happen, so assert */
			assert(B_FALSE);
		}
	}
out:
	umem_cache_destroy(ddt.ddecache);
	free(ddt.dedup_hash_array);
	free(buf);
	(void) fclose(ofp);

	return (NULL);
}

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
	boolean_t recursive;

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

		verify(nvpair_value_nvlist(elem, &propnv) == 0);
		if (prop == ZFS_PROP_QUOTA || prop == ZFS_PROP_RESERVATION ||
		    prop == ZFS_PROP_REFQUOTA ||
		    prop == ZFS_PROP_REFRESERVATION) {
			char *source;
			uint64_t value;
			verify(nvlist_lookup_uint64(propnv,
			    ZPROP_VALUE, &value) == 0);
			if (zhp->zfs_type == ZFS_TYPE_SNAPSHOT)
				continue;
			/*
			 * May have no source before SPA_VERSION_RECVD_PROPS,
			 * but is still modifiable.
			 */
			if (nvlist_lookup_string(propnv,
			    ZPROP_SOURCE, &source) == 0) {
				if ((strcmp(source, zhp->zfs_name) != 0) &&
				    (strcmp(source,
				    ZPROP_SOURCE_VAL_RECVD) != 0))
					continue;
			}
		} else {
			char *source;
			if (nvlist_lookup_string(propnv,
			    ZPROP_SOURCE, &source) != 0)
				continue;
			if ((strcmp(source, zhp->zfs_name) != 0) &&
			    (strcmp(source, ZPROP_SOURCE_VAL_RECVD) != 0))
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

/*
 * recursively generate nvlists describing datasets.  See comment
 * for the data structure send_data_t above for description of contents
 * of the nvlist.
 */
static int
send_iterate_fs(zfs_handle_t *zhp, void *arg)
{
	send_data_t *sd = arg;
	nvlist_t *nvfs, *nv;
	int rv = 0;
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
	(void) zfs_iter_snapshots(zhp, B_FALSE, send_iterate_snap, sd);
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
	if (sd->recursive)
		rv = zfs_iter_filesystems(zhp, send_iterate_fs, sd);

	sd->parent_fromsnap_guid = parent_fromsnap_guid_save;

	zfs_close(zhp);
	return (rv);
}

static int
gather_nvlist(libzfs_handle_t *hdl, const char *fsname, const char *fromsnap,
    const char *tosnap, boolean_t recursive, nvlist_t **nvlp, avl_tree_t **avlp)
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
	sd.recursive = recursive;

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
 * Routines specific to "zfs send"
 */
typedef struct send_dump_data {
	/* these are all just the short snapname (the part after the @) */
	const char *fromsnap;
	const char *tosnap;
	char prevsnap[ZFS_MAXNAMELEN];
	uint64_t prevsnap_obj;
	boolean_t seenfrom, seento, replicate, doall, fromorigin;
	boolean_t verbose, dryrun, parsable, progress;
	int outfd;
	boolean_t err;
	nvlist_t *fss;
	avl_tree_t *fsavl;
	snapfilter_cb_t *filter_cb;
	void *filter_cb_arg;
	nvlist_t *debugnv;
	char holdtag[ZFS_MAXNAMELEN];
	int cleanup_fd;
	uint64_t size;
} send_dump_data_t;

static int
estimate_ioctl(zfs_handle_t *zhp, uint64_t fromsnap_obj,
    boolean_t fromorigin, uint64_t *sizep)
{
	zfs_cmd_t zc = { "\0", "\0", "\0", "\0", 0 };
	libzfs_handle_t *hdl = zhp->zfs_hdl;

	assert(zhp->zfs_type == ZFS_TYPE_SNAPSHOT);
	assert(fromsnap_obj == 0 || !fromorigin);

	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));
	zc.zc_obj = fromorigin;
	zc.zc_sendobj = zfs_prop_get_int(zhp, ZFS_PROP_OBJSETID);
	zc.zc_fromobj = fromsnap_obj;
	zc.zc_guid = 1;  /* estimate flag */

	if (zfs_ioctl(zhp->zfs_hdl, ZFS_IOC_SEND, &zc) != 0) {
		char errbuf[1024];
		(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
		    "warning: cannot estimate space for '%s'"), zhp->zfs_name);

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

	*sizep = zc.zc_objset_type;

	return (0);
}

/*
 * Dumps a backup of the given snapshot (incremental from fromsnap if it's not
 * NULL) to the file descriptor specified by outfd.
 */
static int
dump_ioctl(zfs_handle_t *zhp, const char *fromsnap, uint64_t fromsnap_obj,
    boolean_t fromorigin, int outfd, nvlist_t *debugnv)
{
	zfs_cmd_t zc = { "\0", "\0", "\0", "\0", 0 };
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	nvlist_t *thisdbg;

	assert(zhp->zfs_type == ZFS_TYPE_SNAPSHOT);
	assert(fromsnap_obj == 0 || !fromorigin);

	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));
	zc.zc_cookie = outfd;
	zc.zc_obj = fromorigin;
	zc.zc_sendobj = zfs_prop_get_int(zhp, ZFS_PROP_OBJSETID);
	zc.zc_fromobj = fromsnap_obj;

	VERIFY(0 == nvlist_alloc(&thisdbg, NV_UNIQUE_NAME, 0));
	if (fromsnap && fromsnap[0] != '\0') {
		VERIFY(0 == nvlist_add_string(thisdbg,
		    "fromsnap", fromsnap));
	}

	if (zfs_ioctl(zhp->zfs_hdl, ZFS_IOC_SEND, &zc) != 0) {
		char errbuf[1024];
		(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
		    "warning: cannot send '%s'"), zhp->zfs_name);

		VERIFY(0 == nvlist_add_uint64(thisdbg, "error", errno));
		if (debugnv) {
			VERIFY(0 == nvlist_add_nvlist(debugnv,
			    zhp->zfs_name, thisdbg));
		}
		nvlist_free(thisdbg);

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

	if (debugnv)
		VERIFY(0 == nvlist_add_nvlist(debugnv, zhp->zfs_name, thisdbg));
	nvlist_free(thisdbg);

	return (0);
}

static int
hold_for_send(zfs_handle_t *zhp, send_dump_data_t *sdd)
{
	zfs_handle_t *pzhp;
	int error = 0;
	char *thissnap;

	assert(zhp->zfs_type == ZFS_TYPE_SNAPSHOT);

	if (sdd->dryrun)
		return (0);

	/*
	 * zfs_send() only opens a cleanup_fd for sends that need it,
	 * e.g. replication and doall.
	 */
	if (sdd->cleanup_fd == -1)
		return (0);

	thissnap = strchr(zhp->zfs_name, '@') + 1;
	*(thissnap - 1) = '\0';
	pzhp = zfs_open(zhp->zfs_hdl, zhp->zfs_name, ZFS_TYPE_DATASET);
	*(thissnap - 1) = '@';

	/*
	 * It's OK if the parent no longer exists.  The send code will
	 * handle that error.
	 */
	if (pzhp) {
		error = zfs_hold(pzhp, thissnap, sdd->holdtag,
		    B_FALSE, B_TRUE, B_TRUE, sdd->cleanup_fd,
		    zfs_prop_get_int(zhp, ZFS_PROP_OBJSETID),
		    zfs_prop_get_int(zhp, ZFS_PROP_CREATETXG));
		zfs_close(pzhp);
	}

	return (error);
}

static void *
send_progress_thread(void *arg)
{
	progress_arg_t *pa = arg;

	zfs_cmd_t zc = { "\0", "\0", "\0", "\0", 0 };
	zfs_handle_t *zhp = pa->pa_zhp;
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	unsigned long long bytes;
	char buf[16];

	time_t t;
	struct tm *tm;

	assert(zhp->zfs_type == ZFS_TYPE_SNAPSHOT);
	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));

	if (!pa->pa_parsable)
		(void) fprintf(stderr, "TIME        SENT   SNAPSHOT\n");

	/*
	 * Print the progress from ZFS_IOC_SEND_PROGRESS every second.
	 */
	for (;;) {
		(void) sleep(1);

		zc.zc_cookie = pa->pa_fd;
		if (zfs_ioctl(hdl, ZFS_IOC_SEND_PROGRESS, &zc) != 0)
			return ((void *)-1);

		(void) time(&t);
		tm = localtime(&t);
		bytes = zc.zc_cookie;

		if (pa->pa_parsable) {
			(void) fprintf(stderr, "%02d:%02d:%02d\t%llu\t%s\n",
			    tm->tm_hour, tm->tm_min, tm->tm_sec,
			    bytes, zhp->zfs_name);
		} else {
			zfs_nicenum(bytes, buf, sizeof (buf));
			(void) fprintf(stderr, "%02d:%02d:%02d   %5s   %s\n",
			    tm->tm_hour, tm->tm_min, tm->tm_sec,
			    buf, zhp->zfs_name);
		}
	}
}

static int
dump_snapshot(zfs_handle_t *zhp, void *arg)
{
	send_dump_data_t *sdd = arg;
	progress_arg_t pa = { 0 };
	pthread_t tid;

	char *thissnap;
	int err;
	boolean_t isfromsnap, istosnap, fromorigin;
	boolean_t exclude = B_FALSE;

	thissnap = strchr(zhp->zfs_name, '@') + 1;
	isfromsnap = (sdd->fromsnap != NULL &&
	    strcmp(sdd->fromsnap, thissnap) == 0);

	if (!sdd->seenfrom && isfromsnap) {
		err = hold_for_send(zhp, sdd);
		if (err == 0) {
			sdd->seenfrom = B_TRUE;
			(void) strcpy(sdd->prevsnap, thissnap);
			sdd->prevsnap_obj = zfs_prop_get_int(zhp,
			    ZFS_PROP_OBJSETID);
		} else if (err == ENOENT) {
			err = 0;
		}
		zfs_close(zhp);
		return (err);
	}

	if (sdd->seento || !sdd->seenfrom) {
		zfs_close(zhp);
		return (0);
	}

	istosnap = (strcmp(sdd->tosnap, thissnap) == 0);
	if (istosnap)
		sdd->seento = B_TRUE;

	if (!sdd->doall && !isfromsnap && !istosnap) {
		if (sdd->replicate) {
			char *snapname;
			nvlist_t *snapprops;
			/*
			 * Filter out all intermediate snapshots except origin
			 * snapshots needed to replicate clones.
			 */
			nvlist_t *nvfs = fsavl_find(sdd->fsavl,
			    zhp->zfs_dmustats.dds_guid, &snapname);

			VERIFY(0 == nvlist_lookup_nvlist(nvfs,
			    "snapprops", &snapprops));
			VERIFY(0 == nvlist_lookup_nvlist(snapprops,
			    thissnap, &snapprops));
			exclude = !nvlist_exists(snapprops, "is_clone_origin");
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

	err = hold_for_send(zhp, sdd);
	if (err) {
		if (err == ENOENT)
			err = 0;
		zfs_close(zhp);
		return (err);
	}

	fromorigin = sdd->prevsnap[0] == '\0' &&
	    (sdd->fromorigin || sdd->replicate);

	if (sdd->verbose) {
		uint64_t size;
		err = estimate_ioctl(zhp, sdd->prevsnap_obj,
		    fromorigin, &size);

		if (sdd->parsable) {
			if (sdd->prevsnap[0] != '\0') {
				(void) fprintf(stderr, "incremental\t%s\t%s",
				    sdd->prevsnap, zhp->zfs_name);
			} else {
				(void) fprintf(stderr, "full\t%s",
				    zhp->zfs_name);
			}
		} else {
			(void) fprintf(stderr, dgettext(TEXT_DOMAIN,
			    "send from @%s to %s"),
			    sdd->prevsnap, zhp->zfs_name);
		}
		if (err == 0) {
			if (sdd->parsable) {
				(void) fprintf(stderr, "\t%llu\n",
				    (longlong_t)size);
			} else {
				char buf[16];
				zfs_nicenum(size, buf, sizeof (buf));
				(void) fprintf(stderr, dgettext(TEXT_DOMAIN,
				    " estimated size is %s\n"), buf);
			}
			sdd->size += size;
		} else {
			(void) fprintf(stderr, "\n");
		}
	}

	if (!sdd->dryrun) {
		/*
		 * If progress reporting is requested, spawn a new thread to
		 * poll ZFS_IOC_SEND_PROGRESS at a regular interval.
		 */
		if (sdd->progress) {
			pa.pa_zhp = zhp;
			pa.pa_fd = sdd->outfd;
			pa.pa_parsable = sdd->parsable;

			if ((err = pthread_create(&tid, NULL,
			    send_progress_thread, &pa))) {
				zfs_close(zhp);
				return (err);
			}
		}

		err = dump_ioctl(zhp, sdd->prevsnap, sdd->prevsnap_obj,
		    fromorigin, sdd->outfd, sdd->debugnv);

		if (sdd->progress) {
			(void) pthread_cancel(tid);
			(void) pthread_join(tid, NULL);
		}
	}

	(void) strcpy(sdd->prevsnap, thissnap);
	sdd->prevsnap_obj = zfs_prop_get_int(zhp, ZFS_PROP_OBJSETID);
	zfs_close(zhp);
	return (err);
}

static int
dump_filesystem(zfs_handle_t *zhp, void *arg)
{
	int rv = 0;
	send_dump_data_t *sdd = arg;
	boolean_t missingfrom = B_FALSE;
	zfs_cmd_t zc = { "\0", "\0", "\0", "\0", 0 };

	(void) snprintf(zc.zc_name, sizeof (zc.zc_name), "%s@%s",
	    zhp->zfs_name, sdd->tosnap);
	if (ioctl(zhp->zfs_hdl->libzfs_fd, ZFS_IOC_OBJSET_STATS, &zc) != 0) {
		(void) fprintf(stderr, dgettext(TEXT_DOMAIN,
		    "WARNING: could not send %s@%s: does not exist\n"),
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

	sdd->seenfrom = sdd->seento = sdd->prevsnap[0] = 0;
	sdd->prevsnap_obj = 0;
	if (sdd->fromsnap == NULL || missingfrom)
		sdd->seenfrom = B_TRUE;

	rv = zfs_iter_snapshots_sorted(zhp, dump_snapshot, arg);
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

static int
dump_filesystems(zfs_handle_t *rzhp, void *arg)
{
	send_dump_data_t *sdd = arg;
	nvpair_t *fspair;
	boolean_t needagain, progress;

	if (!sdd->replicate)
		return (dump_filesystem(rzhp, sdd));

	/* Mark the clone origin snapshots. */
	for (fspair = nvlist_next_nvpair(sdd->fss, NULL); fspair;
	    fspair = nvlist_next_nvpair(sdd->fss, fspair)) {
		nvlist_t *nvfs;
		uint64_t origin_guid = 0;

		VERIFY(0 == nvpair_value_nvlist(fspair, &nvfs));
		(void) nvlist_lookup_uint64(nvfs, "origin", &origin_guid);
		if (origin_guid != 0) {
			char *snapname;
			nvlist_t *origin_nv = fsavl_find(sdd->fsavl,
			    origin_guid, &snapname);
			if (origin_nv != NULL) {
				nvlist_t *snapprops;
				VERIFY(0 == nvlist_lookup_nvlist(origin_nv,
				    "snapprops", &snapprops));
				VERIFY(0 == nvlist_lookup_nvlist(snapprops,
				    snapname, &snapprops));
				VERIFY(0 == nvlist_add_boolean(
				    snapprops, "is_clone_origin"));
			}
		}
	}
again:
	needagain = progress = B_FALSE;
	for (fspair = nvlist_next_nvpair(sdd->fss, NULL); fspair;
	    fspair = nvlist_next_nvpair(sdd->fss, fspair)) {
		nvlist_t *fslist, *parent_nv;
		char *fsname;
		zfs_handle_t *zhp;
		int err;
		uint64_t origin_guid = 0;
		uint64_t parent_guid = 0;

		VERIFY(nvpair_value_nvlist(fspair, &fslist) == 0);
		if (nvlist_lookup_boolean(fslist, "sent") == 0)
			continue;

		VERIFY(nvlist_lookup_string(fslist, "name", &fsname) == 0);
		(void) nvlist_lookup_uint64(fslist, "origin", &origin_guid);
		(void) nvlist_lookup_uint64(fslist, "parentfromsnap",
		    &parent_guid);

		if (parent_guid != 0) {
			parent_nv = fsavl_find(sdd->fsavl, parent_guid, NULL);
			if (!nvlist_exists(parent_nv, "sent")) {
				/* parent has not been sent; skip this one */
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
				 * origin has not been sent yet;
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

	/* clean out the sent flags in case we reuse this fss */
	for (fspair = nvlist_next_nvpair(sdd->fss, NULL); fspair;
	    fspair = nvlist_next_nvpair(sdd->fss, fspair)) {
		nvlist_t *fslist;

		VERIFY(nvpair_value_nvlist(fspair, &fslist) == 0);
		(void) nvlist_remove_all(fslist, "sent");
	}

	return (0);
}

/*
 * Generate a send stream for the dataset identified by the argument zhp.
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
 */
int
zfs_send(zfs_handle_t *zhp, const char *fromsnap, const char *tosnap,
    sendflags_t *flags, int outfd, snapfilter_cb_t filter_func,
    void *cb_arg, nvlist_t **debugnvp)
{
	char errbuf[1024];
	send_dump_data_t sdd = { 0 };
	int err = 0;
	nvlist_t *fss = NULL;
	avl_tree_t *fsavl = NULL;
	static uint64_t holdseq;
	int spa_version;
	pthread_t tid;
	int pipefd[2];
	dedup_arg_t dda = { 0 };
	int featureflags = 0;

	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "cannot send '%s'"), zhp->zfs_name);

	if (fromsnap && fromsnap[0] == '\0') {
		zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
		    "zero-length incremental source"));
		return (zfs_error(zhp->zfs_hdl, EZFS_NOENT, errbuf));
	}

	if (zhp->zfs_type == ZFS_TYPE_FILESYSTEM) {
		uint64_t version;
		version = zfs_prop_get_int(zhp, ZFS_PROP_VERSION);
		if (version >= ZPL_VERSION_SA) {
			featureflags |= DMU_BACKUP_FEATURE_SA_SPILL;
		}
	}

	if (flags->dedup && !flags->dryrun) {
		featureflags |= (DMU_BACKUP_FEATURE_DEDUP |
		    DMU_BACKUP_FEATURE_DEDUPPROPS);
		if ((err = socketpair(AF_UNIX, SOCK_STREAM, 0, pipefd))) {
			zfs_error_aux(zhp->zfs_hdl, strerror(errno));
			return (zfs_error(zhp->zfs_hdl, EZFS_PIPEFAILED,
			    errbuf));
		}
		dda.outputfd = outfd;
		dda.inputfd = pipefd[1];
		dda.dedup_hdl = zhp->zfs_hdl;
		if ((err = pthread_create(&tid, NULL, cksummer, &dda))) {
			(void) close(pipefd[0]);
			(void) close(pipefd[1]);
			zfs_error_aux(zhp->zfs_hdl, strerror(errno));
			return (zfs_error(zhp->zfs_hdl,
			    EZFS_THREADCREATEFAILED, errbuf));
		}
	}

	if (flags->replicate || flags->doall || flags->props) {
		dmu_replay_record_t drr = { 0 };
		char *packbuf = NULL;
		size_t buflen = 0;
		zio_cksum_t zc = { { 0 } };

		if (flags->replicate || flags->props) {
			nvlist_t *hdrnv;

			VERIFY(0 == nvlist_alloc(&hdrnv, NV_UNIQUE_NAME, 0));
			if (fromsnap) {
				VERIFY(0 == nvlist_add_string(hdrnv,
				    "fromsnap", fromsnap));
			}
			VERIFY(0 == nvlist_add_string(hdrnv, "tosnap", tosnap));
			if (!flags->replicate) {
				VERIFY(0 == nvlist_add_boolean(hdrnv,
				    "not_recursive"));
			}

			err = gather_nvlist(zhp->zfs_hdl, zhp->zfs_name,
			    fromsnap, tosnap, flags->replicate, &fss, &fsavl);
			if (err)
				goto err_out;
			VERIFY(0 == nvlist_add_nvlist(hdrnv, "fss", fss));
			err = nvlist_pack(hdrnv, &packbuf, &buflen,
			    NV_ENCODE_XDR, 0);
			if (debugnvp)
				*debugnvp = hdrnv;
			else
				nvlist_free(hdrnv);
			if (err) {
				fsavl_destroy(fsavl);
				nvlist_free(fss);
				goto stderr_out;
			}
		}

		if (!flags->dryrun) {
			/* write first begin record */
			drr.drr_type = DRR_BEGIN;
			drr.drr_u.drr_begin.drr_magic = DMU_BACKUP_MAGIC;
			DMU_SET_STREAM_HDRTYPE(drr.drr_u.drr_begin.
			    drr_versioninfo, DMU_COMPOUNDSTREAM);
			DMU_SET_FEATUREFLAGS(drr.drr_u.drr_begin.
			    drr_versioninfo, featureflags);
			(void) snprintf(drr.drr_u.drr_begin.drr_toname,
			    sizeof (drr.drr_u.drr_begin.drr_toname),
			    "%s@%s", zhp->zfs_name, tosnap);
			drr.drr_payloadlen = buflen;
			err = cksum_and_write(&drr, sizeof (drr), &zc, outfd);

			/* write header nvlist */
			if (err != -1 && packbuf != NULL) {
				err = cksum_and_write(packbuf, buflen, &zc,
				    outfd);
			}
			free(packbuf);
			if (err == -1) {
				fsavl_destroy(fsavl);
				nvlist_free(fss);
				err = errno;
				goto stderr_out;
			}

			/* write end record */
			bzero(&drr, sizeof (drr));
			drr.drr_type = DRR_END;
			drr.drr_u.drr_end.drr_checksum = zc;
			err = write(outfd, &drr, sizeof (drr));
			if (err == -1) {
				fsavl_destroy(fsavl);
				nvlist_free(fss);
				err = errno;
				goto stderr_out;
			}

			err = 0;
		}
	}

	/* dump each stream */
	sdd.fromsnap = fromsnap;
	sdd.tosnap = tosnap;
	if (flags->dedup)
		sdd.outfd = pipefd[0];
	else
		sdd.outfd = outfd;
	sdd.replicate = flags->replicate;
	sdd.doall = flags->doall;
	sdd.fromorigin = flags->fromorigin;
	sdd.fss = fss;
	sdd.fsavl = fsavl;
	sdd.verbose = flags->verbose;
	sdd.parsable = flags->parsable;
	sdd.progress = flags->progress;
	sdd.dryrun = flags->dryrun;
	sdd.filter_cb = filter_func;
	sdd.filter_cb_arg = cb_arg;
	if (debugnvp)
		sdd.debugnv = *debugnvp;

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
		sdd.cleanup_fd = open(ZFS_DEV, O_RDWR);
		if (sdd.cleanup_fd < 0) {
			err = errno;
			goto stderr_out;
		}
	} else {
		sdd.cleanup_fd = -1;
	}
	if (flags->verbose) {
		/*
		 * Do a verbose no-op dry run to get all the verbose output
		 * before generating any data.  Then do a non-verbose real
		 * run to generate the streams.
		 */
		sdd.dryrun = B_TRUE;
		err = dump_filesystems(zhp, &sdd);
		sdd.dryrun = flags->dryrun;
		sdd.verbose = B_FALSE;
		if (flags->parsable) {
			(void) fprintf(stderr, "size\t%llu\n",
			    (longlong_t)sdd.size);
		} else {
			char buf[16];
			zfs_nicenum(sdd.size, buf, sizeof (buf));
			(void) fprintf(stderr, dgettext(TEXT_DOMAIN,
			    "total estimated size is %s\n"), buf);
		}
	}
	err = dump_filesystems(zhp, &sdd);
	fsavl_destroy(fsavl);
	nvlist_free(fss);

	if (flags->dedup) {
		(void) close(pipefd[0]);
		(void) pthread_join(tid, NULL);
	}

	if (sdd.cleanup_fd != -1) {
		VERIFY(0 == close(sdd.cleanup_fd));
		sdd.cleanup_fd = -1;
	}

	if (!flags->dryrun && (flags->replicate || flags->doall ||
	    flags->props)) {
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

stderr_out:
	err = zfs_standard_error(zhp->zfs_hdl, err, errbuf);
err_out:
	if (sdd.cleanup_fd != -1)
		VERIFY(0 == close(sdd.cleanup_fd));
	if (flags->dedup) {
		(void) pthread_cancel(tid);
		(void) pthread_join(tid, NULL);
		(void) close(pipefd[0]);
	}
	return (err);
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
    int baselen, char *newname, recvflags_t *flags)
{
	static int seq;
	zfs_cmd_t zc = { "\0", "\0", "\0", "\0", 0 };
	int err;
	prop_changelist_t *clp;
	zfs_handle_t *zhp;

	zhp = zfs_open(hdl, name, ZFS_TYPE_DATASET);
	if (zhp == NULL)
		return (-1);
	clp = changelist_gather(zhp, ZFS_PROP_NAME, 0,
	    flags->force ? MS_FORCE : 0);
	zfs_close(zhp);
	if (clp == NULL)
		return (-1);
	err = changelist_prefix(clp);
	if (err)
		return (err);

	zc.zc_objset_type = DMU_OST_ZFS;
	(void) strlcpy(zc.zc_name, name, sizeof (zc.zc_name));

	if (tryname) {
		(void) strcpy(newname, tryname);

		(void) strlcpy(zc.zc_value, tryname, sizeof (zc.zc_value));

		if (flags->verbose) {
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
		    "recv-%ld-%u", (long) getpid(), seq);
		(void) strlcpy(zc.zc_value, newname, sizeof (zc.zc_value));

		if (flags->verbose) {
			(void) printf("failed - trying rename %s to %s\n",
			    zc.zc_name, zc.zc_value);
		}
		err = ioctl(hdl->libzfs_fd, ZFS_IOC_RENAME, &zc);
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
	changelist_free(clp);

	return (err);
}

static int
recv_destroy(libzfs_handle_t *hdl, const char *name, int baselen,
    char *newname, recvflags_t *flags)
{
	zfs_cmd_t zc = { "\0", "\0", "\0", "\0", 0 };
	int err = 0;
	prop_changelist_t *clp;
	zfs_handle_t *zhp;
	boolean_t defer = B_FALSE;
	int spa_version;

	zhp = zfs_open(hdl, name, ZFS_TYPE_DATASET);
	if (zhp == NULL)
		return (-1);
	clp = changelist_gather(zhp, ZFS_PROP_NAME, 0,
	    flags->force ? MS_FORCE : 0);
	if (zfs_get_type(zhp) == ZFS_TYPE_SNAPSHOT &&
	    zfs_spa_version(zhp, &spa_version) == 0 &&
	    spa_version >= SPA_VERSION_USERREFS)
		defer = B_TRUE;
	zfs_close(zhp);
	if (clp == NULL)
		return (-1);
	err = changelist_prefix(clp);
	if (err)
		return (err);

	zc.zc_objset_type = DMU_OST_ZFS;
	zc.zc_defer_destroy = defer;
	(void) strlcpy(zc.zc_name, name, sizeof (zc.zc_name));

	if (flags->verbose)
		(void) printf("attempting destroy %s\n", zc.zc_name);
	err = ioctl(hdl->libzfs_fd, ZFS_IOC_DESTROY, &zc);
	if (err == 0) {
		if (flags->verbose)
			(void) printf("success\n");
		changelist_remove(clp, zc.zc_name);
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
	char *name;
	char *skip;
} guid_to_name_data_t;

static int
guid_to_name_cb(zfs_handle_t *zhp, void *arg)
{
	guid_to_name_data_t *gtnd = arg;
	int err;

	if (gtnd->skip != NULL &&
	    strcmp(zhp->zfs_name, gtnd->skip) == 0) {
		return (0);
	}

	if (zhp->zfs_dmustats.dds_guid == gtnd->guid) {
		(void) strcpy(gtnd->name, zhp->zfs_name);
		zfs_close(zhp);
		return (EEXIST);
	}

	err = zfs_iter_children(zhp, guid_to_name_cb, gtnd);
	zfs_close(zhp);
	return (err);
}

/*
 * Attempt to find the local dataset associated with this guid.  In the case of
 * multiple matches, we attempt to find the "best" match by searching
 * progressively larger portions of the hierarchy.  This allows one to send a
 * tree of datasets individually and guarantee that we will find the source
 * guid within that hierarchy, even if there are multiple matches elsewhere.
 */
static int
guid_to_name(libzfs_handle_t *hdl, const char *parent, uint64_t guid,
    char *name)
{
	/* exhaustive search all local snapshots */
	char pname[ZFS_MAXNAMELEN];
	guid_to_name_data_t gtnd;
	int err = 0;
	zfs_handle_t *zhp;
	char *cp;

	gtnd.guid = guid;
	gtnd.name = name;
	gtnd.skip = NULL;

	(void) strlcpy(pname, parent, sizeof (pname));

	/*
	 * Search progressively larger portions of the hierarchy.  This will
	 * select the "most local" version of the origin snapshot in the case
	 * that there are multiple matching snapshots in the system.
	 */
	while ((cp = strrchr(pname, '/')) != NULL) {

		/* Chop off the last component and open the parent */
		*cp = '\0';
		zhp = make_dataset_handle(hdl, pname);

		if (zhp == NULL)
			continue;

		err = zfs_iter_children(zhp, guid_to_name_cb, &gtnd);
		zfs_close(zhp);
		if (err == EEXIST)
			return (0);

		/*
		 * Remember the dataset that we already searched, so we
		 * skip it next time through.
		 */
		gtnd.skip = pname;
	}

	return (ENOENT);
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
	char *fsname, *snapname;
	char buf[ZFS_MAXNAMELEN];
	int rv;
	zfs_handle_t *guid1hdl, *guid2hdl;
	uint64_t create1, create2;

	if (guid2 == 0)
		return (0);
	if (guid1 == 0)
		return (1);

	nvfs = fsavl_find(avl, guid1, &snapname);
	VERIFY(0 == nvlist_lookup_string(nvfs, "name", &fsname));
	(void) snprintf(buf, sizeof (buf), "%s@%s", fsname, snapname);
	guid1hdl = zfs_open(hdl, buf, ZFS_TYPE_SNAPSHOT);
	if (guid1hdl == NULL)
		return (-1);

	nvfs = fsavl_find(avl, guid2, &snapname);
	VERIFY(0 == nvlist_lookup_string(nvfs, "name", &fsname));
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

static int
recv_incremental_replication(libzfs_handle_t *hdl, const char *tofs,
    recvflags_t *flags, nvlist_t *stream_nv, avl_tree_t *stream_avl,
    nvlist_t *renamed)
{
	nvlist_t *local_nv;
	avl_tree_t *local_avl;
	nvpair_t *fselem, *nextfselem;
	char *fromsnap;
	char newname[ZFS_MAXNAMELEN];
	int error;
	boolean_t needagain, progress, recursive;
	char *s1, *s2;

	VERIFY(0 == nvlist_lookup_string(stream_nv, "fromsnap", &fromsnap));

	recursive = (nvlist_lookup_boolean(stream_nv, "not_recursive") ==
	    ENOENT);

	if (flags->dryrun)
		return (0);

again:
	needagain = progress = B_FALSE;

	if ((error = gather_nvlist(hdl, tofs, fromsnap, NULL,
	    recursive, &local_nv, &local_avl)) != 0)
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
				zfs_cmd_t zc = { "\0", "\0", "\0", "\0", 0 };
				nvlist_t *origin_nvfs;
				char *origin_fsname;

				if (flags->verbose)
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
				continue;
			}

			stream_nvfs = found;

			if (0 == nvlist_lookup_nvlist(stream_nvfs, "snapprops",
			    &props) && 0 == nvlist_lookup_nvlist(props,
			    stream_snapname, &props)) {
				zfs_cmd_t zc = { "\0", "\0", "\0", "\0", 0 };

				zc.zc_cookie = B_TRUE; /* received */
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
			if (!flags->force)
				continue;

			error = recv_destroy(hdl, fsname, strlen(tofs)+1,
			    newname, flags);
			if (error)
				needagain = B_TRUE;
			else
				progress = B_TRUE;
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

		VERIFY(0 == nvlist_lookup_string(stream_nvfs,
		    "name", &stream_fsname));
		VERIFY(0 == nvlist_lookup_uint64(stream_nvfs,
		    "parentfromsnap", &stream_parent_fromsnap_guid));

		s1 = strrchr(fsname, '/');
		s2 = strrchr(stream_fsname, '/');

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
				if (flags->verbose) {
					(void) printf("local fs %s new parent "
					    "not found\n", fsname);
				}
			}

			newname[0] = '\0';

			error = recv_rename(hdl, fsname, tryname,
			    strlen(tofs)+1, newname, flags);

			if (renamed != NULL && newname[0] != '\0') {
				VERIFY(0 == nvlist_add_boolean(renamed,
				    newname));
			}

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
		if (flags->verbose)
			(void) printf("another pass:\n");
		goto again;
	}

	return (needagain);
}

static int
zfs_receive_package(libzfs_handle_t *hdl, int fd, const char *destname,
    recvflags_t *flags, dmu_replay_record_t *drr, zio_cksum_t *zc,
    char **top_zfs, int cleanup_fd, uint64_t *action_handlep)
{
	nvlist_t *stream_nv = NULL;
	avl_tree_t *stream_avl = NULL;
	char *fromsnap = NULL;
	char *cp;
	char tofs[ZFS_MAXNAMELEN];
	char sendfs[ZFS_MAXNAMELEN];
	char errbuf[1024];
	dmu_replay_record_t drre;
	int error;
	boolean_t anyerr = B_FALSE;
	boolean_t softerr = B_FALSE;
	boolean_t recursive;

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

		VERIFY(0 == nvlist_lookup_nvlist(stream_nv, "fss",
		    &stream_fss));
		if ((stream_avl = fsavl_create(stream_fss)) == NULL) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "couldn't allocate avl tree"));
			error = zfs_error(hdl, EZFS_NOMEM, errbuf);
			goto out;
		}

		if (fromsnap != NULL) {
			nvlist_t *renamed = NULL;
			nvpair_t *pair = NULL;

			(void) strlcpy(tofs, destname, ZFS_MAXNAMELEN);
			if (flags->isprefix) {
				struct drr_begin *drrb = &drr->drr_u.drr_begin;
				int i;

				if (flags->istail) {
					cp = strrchr(drrb->drr_toname, '/');
					if (cp == NULL) {
						(void) strlcat(tofs, "/",
						    ZFS_MAXNAMELEN);
						i = 0;
					} else {
						i = (cp - drrb->drr_toname);
					}
				} else {
					i = strcspn(drrb->drr_toname, "/@");
				}
				/* zfs_receive_one() will create_parents() */
				(void) strlcat(tofs, &drrb->drr_toname[i],
				    ZFS_MAXNAMELEN);
				*strchr(tofs, '@') = '\0';
			}

			if (recursive && !flags->dryrun && !flags->nomount) {
				VERIFY(0 == nvlist_alloc(&renamed,
				    NV_UNIQUE_NAME, 0));
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
					    ZFS_PROP_MOUNTPOINT, 0, 0);
					zfs_close(zhp);
					if (clp != NULL) {
						softerr |=
						    changelist_prefix(clp);
						changelist_free(clp);
					}
				}
			}

			nvlist_free(renamed);
		}
	}

	/*
	 * Get the fs specified by the first path in the stream (the top level
	 * specified by 'zfs send') and pass it to each invocation of
	 * zfs_receive_one().
	 */
	(void) strlcpy(sendfs, drr->drr_u.drr_begin.drr_toname,
	    ZFS_MAXNAMELEN);
	if ((cp = strchr(sendfs, '@')) != NULL)
		*cp = '\0';

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
		    sendfs, stream_nv, stream_avl, top_zfs, cleanup_fd,
		    action_handlep);
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
		    stream_nv, stream_avl, NULL);
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
	void *buf = malloc(1<<20);
	char errbuf[1024];

	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "cannot receive:"));

	/* XXX would be great to use lseek if possible... */
	drr = buf;

	while (recv_read(hdl, fd, drr, sizeof (dmu_replay_record_t),
	    byteswap, NULL) == 0) {
		if (byteswap)
			drr->drr_type = BSWAP_32(drr->drr_type);

		switch (drr->drr_type) {
		case DRR_BEGIN:
			/* NB: not to be used on v2 stream packages */
			if (drr->drr_payloadlen != 0) {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "invalid substream header"));
				return (zfs_error(hdl, EZFS_BADSTREAM, errbuf));
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
		case DRR_SPILL:
			if (byteswap) {
				drr->drr_u.drr_write.drr_length =
				    BSWAP_64(drr->drr_u.drr_spill.drr_length);
			}
			(void) recv_read(hdl, fd, buf,
			    drr->drr_u.drr_spill.drr_length, B_FALSE, NULL);
			break;
		case DRR_WRITE_BYREF:
		case DRR_FREEOBJECTS:
		case DRR_FREE:
			break;

		default:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "invalid record type"));
			return (zfs_error(hdl, EZFS_BADSTREAM, errbuf));
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
    recvflags_t *flags, dmu_replay_record_t *drr,
    dmu_replay_record_t *drr_noswap, const char *sendfs,
    nvlist_t *stream_nv, avl_tree_t *stream_avl, char **top_zfs, int cleanup_fd,
    uint64_t *action_handlep)
{
	zfs_cmd_t zc = { "\0", "\0", "\0", "\0", 0 };
	time_t begin_time;
	int ioctl_err, ioctl_errno, err;
	char *cp;
	struct drr_begin *drrb = &drr->drr_u.drr_begin;
	char errbuf[1024];
	char prop_errbuf[1024];
	const char *chopprefix;
	boolean_t newfs = B_FALSE;
	boolean_t stream_wantsnewfs;
	uint64_t parent_snapguid = 0;
	prop_changelist_t *clp = NULL;
	nvlist_t *snapprops_nvlist = NULL;
	zprop_errflags_t prop_errflags;
	boolean_t recursive;

	begin_time = time(NULL);

	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "cannot receive"));

	recursive = (nvlist_lookup_boolean(stream_nv, "not_recursive") ==
	    ENOENT);

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

		if (flags->canmountoff) {
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
			return (zfs_error(hdl, EZFS_INVALIDNAME, errbuf));
		}

		chopprefix = strrchr(sendfs, '/');

		if (chopprefix == NULL) {
			/*
			 * The tail is the poolname, so we need to
			 * prepend a path separator.
			 */
			int len = strlen(drrb->drr_toname);
			cp = malloc(len + 2);
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
			return (zfs_error(hdl, EZFS_INVALIDNAME, errbuf));
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
			return (zfs_error(hdl, EZFS_BADSTREAM, errbuf));
		}
		chopprefix = drrb->drr_toname + strlen(drrb->drr_toname);
	}

	ASSERT(strstr(drrb->drr_toname, sendfs) == drrb->drr_toname);
	ASSERT(chopprefix > drrb->drr_toname);
	ASSERT(chopprefix <= drrb->drr_toname + strlen(drrb->drr_toname));
	ASSERT(chopprefix[0] == '/' || chopprefix[0] == '@' ||
	    chopprefix[0] == '\0');

	/*
	 * Determine name of destination snapshot, store in zc_value.
	 */
	(void) strcpy(zc.zc_top_ds, tosnap);
	(void) strcpy(zc.zc_value, tosnap);
	(void) strlcat(zc.zc_value, chopprefix, sizeof (zc.zc_value));
	free(cp);
	if (!zfs_name_valid(zc.zc_value, ZFS_TYPE_SNAPSHOT)) {
		zcmd_free_nvlists(&zc);
		return (zfs_error(hdl, EZFS_INVALIDNAME, errbuf));
	}

	/*
	 * Determine the name of the origin snapshot, store in zc_string.
	 */
	if (drrb->drr_flags & DRR_FLAG_CLONE) {
		if (guid_to_name(hdl, zc.zc_value,
		    drrb->drr_fromguid, zc.zc_string) != 0) {
			zcmd_free_nvlists(&zc);
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "local origin for clone %s does not exist"),
			    zc.zc_value);
			return (zfs_error(hdl, EZFS_NOENT, errbuf));
		}
		if (flags->verbose)
			(void) printf("found clone origin %s\n", zc.zc_string);
	}

	stream_wantsnewfs = (drrb->drr_fromguid == 0 ||
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
			if (guid_to_name(hdl, zc.zc_name, parent_snapguid,
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

		/*
		 * If the exact receive path was specified and this is the
		 * topmost path in the stream, then if the fs does not exist we
		 * should look no further.
		 */
		if ((flags->isprefix || (*(chopprefix = drrb->drr_toname +
		    strlen(sendfs)) != '\0' && *chopprefix != '@')) &&
		    !zfs_dataset_exists(hdl, zc.zc_name, ZFS_TYPE_DATASET)) {
			char snap[ZFS_MAXNAMELEN];
			(void) strcpy(snap, strchr(zc.zc_value, '@'));
			if (guid_to_name(hdl, zc.zc_name, drrb->drr_fromguid,
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
			if (!flags->force) {
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

		if (!flags->dryrun && zhp->zfs_type == ZFS_TYPE_FILESYSTEM &&
		    stream_wantsnewfs) {
			/* We can't do online recv in this case */
			clp = changelist_gather(zhp, ZFS_PROP_NAME, 0, 0);
			if (clp == NULL) {
				zfs_close(zhp);
				zcmd_free_nvlists(&zc);
				return (-1);
			}
			if (changelist_prefix(clp) != 0) {
				changelist_free(clp);
				zfs_close(zhp);
				zcmd_free_nvlists(&zc);
				return (-1);
			}
		}
		if (!flags->dryrun && zhp->zfs_type == ZFS_TYPE_VOLUME &&
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

		if (flags->isprefix && !flags->istail && !flags->dryrun &&
		    create_parents(hdl, zc.zc_value, strlen(tosnap)) != 0) {
			zcmd_free_nvlists(&zc);
			return (zfs_error(hdl, EZFS_BADRESTORE, errbuf));
		}

		newfs = B_TRUE;
	}

	zc.zc_begin_record = drr_noswap->drr_u.drr_begin;
	zc.zc_cookie = infd;
	zc.zc_guid = flags->force;
	if (flags->verbose) {
		(void) printf("%s %s stream of %s into %s\n",
		    flags->dryrun ? "would receive" : "receiving",
		    drrb->drr_fromguid ? "incremental" : "full",
		    drrb->drr_toname, zc.zc_value);
		(void) fflush(stdout);
	}

	if (flags->dryrun) {
		zcmd_free_nvlists(&zc);
		return (recv_skip(hdl, infd, flags->byteswap));
	}

	zc.zc_nvlist_dst = (uint64_t)(uintptr_t)prop_errbuf;
	zc.zc_nvlist_dst_size = sizeof (prop_errbuf);
	zc.zc_cleanup_fd = cleanup_fd;
	zc.zc_action_handle = *action_handlep;

	err = ioctl_err = zfs_ioctl(hdl, ZFS_IOC_RECV, &zc);
	ioctl_errno = errno;
	prop_errflags = (zprop_errflags_t)zc.zc_obj;

	if (err == 0) {
		nvlist_t *prop_errors;
		VERIFY(0 == nvlist_unpack((void *)(uintptr_t)zc.zc_nvlist_dst,
		    zc.zc_nvlist_dst_size, &prop_errors, 0));

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
			} else {
				(void) snprintf(tbuf, sizeof (tbuf),
				    dgettext(TEXT_DOMAIN,
				    "cannot receive %s property on %s"),
				    nvpair_name(prop_err), zc.zc_name);
				zfs_setprop_error(hdl, prop, intval, tbuf);
			}
		}
		nvlist_free(prop_errors);
	}

	zc.zc_nvlist_dst = 0;
	zc.zc_nvlist_dst_size = 0;
	zcmd_free_nvlists(&zc);

	if (err == 0 && snapprops_nvlist) {
		zfs_cmd_t zc2 = { "\0", "\0", "\0", "\0", 0 };

		(void) strcpy(zc2.zc_name, zc.zc_value);
		zc2.zc_cookie = B_TRUE; /* received */
		if (zcmd_write_src_nvlist(hdl, &zc2, snapprops_nvlist) == 0) {
			(void) zfs_ioctl(hdl, ZFS_IOC_SET_PROP, &zc2);
			zcmd_free_nvlists(&zc2);
		}
	}

	if (err && (ioctl_errno == ENOENT || ioctl_errno == EEXIST)) {
		/*
		 * It may be that this snapshot already exists,
		 * in which case we want to consume & ignore it
		 * rather than failing.
		 */
		avl_tree_t *local_avl;
		nvlist_t *local_nv, *fs;
		cp = strchr(zc.zc_value, '@');

		/*
		 * XXX Do this faster by just iterating over snaps in
		 * this fs.  Also if zc_value does not exist, we will
		 * get a strange "does not exist" error message.
		 */
		*cp = '\0';
		if (gather_nvlist(hdl, zc.zc_value, NULL, NULL, B_FALSE,
		    &local_nv, &local_avl) == 0) {
			*cp = '@';
			fs = fsavl_find(local_avl, drrb->drr_toguid, NULL);
			fsavl_destroy(local_avl);
			nvlist_free(local_nv);

			if (fs != NULL) {
				if (flags->verbose) {
					(void) printf("snap %s already exists; "
					    "ignoring\n", zc.zc_value);
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
		case ENOTSUP:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "pool must be upgraded to receive this stream."));
			(void) zfs_error(hdl, EZFS_BADVERSION, errbuf);
			break;
		case EDQUOT:
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "destination %s space quota exceeded"), zc.zc_name);
			(void) zfs_error(hdl, EZFS_NOSPC, errbuf);
			break;
		default:
			(void) zfs_standard_error(hdl, ioctl_errno, errbuf);
		}
	}

	/*
	 * Mount the target filesystem (if created).  Also mount any
	 * children of the target filesystem if we did a replication
	 * receive (indicated by stream_avl being non-NULL).
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
			} else if (newfs || stream_avl) {
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

	if (prop_errflags & ZPROP_ERR_NOCLEAR) {
		(void) fprintf(stderr, dgettext(TEXT_DOMAIN, "Warning: "
		    "failed to clear unreceived properties on %s"),
		    zc.zc_name);
		(void) fprintf(stderr, "\n");
	}
	if (prop_errflags & ZPROP_ERR_NORESTORE) {
		(void) fprintf(stderr, dgettext(TEXT_DOMAIN, "Warning: "
		    "failed to restore original properties on %s"),
		    zc.zc_name);
		(void) fprintf(stderr, "\n");
	}

	if (err || ioctl_err)
		return (-1);

	*action_handlep = zc.zc_action_handle;

	if (flags->verbose) {
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
zfs_receive_impl(libzfs_handle_t *hdl, const char *tosnap, recvflags_t *flags,
    int infd, const char *sendfs, nvlist_t *stream_nv, avl_tree_t *stream_avl,
    char **top_zfs, int cleanup_fd, uint64_t *action_handlep)
{
	int err;
	dmu_replay_record_t drr, drr_noswap;
	struct drr_begin *drrb = &drr.drr_u.drr_begin;
	char errbuf[1024];
	zio_cksum_t zcksum = { { 0 } };
	uint64_t featureflags;
	int hdrtype;

	(void) snprintf(errbuf, sizeof (errbuf), dgettext(TEXT_DOMAIN,
	    "cannot receive"));

	if (flags->isprefix &&
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

	flags->byteswap = B_FALSE;
	if (drrb->drr_magic == BSWAP_64(DMU_BACKUP_MAGIC)) {
		/*
		 * We computed the checksum in the wrong byteorder in
		 * recv_read() above; do it again correctly.
		 */
		bzero(&zcksum, sizeof (zio_cksum_t));
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
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "stream has unsupported feature, feature flags = %lx"),
		    featureflags);
		return (zfs_error(hdl, EZFS_BADSTREAM, errbuf));
	}

	if (strchr(drrb->drr_toname, '@') == NULL) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, "invalid "
		    "stream (bad snapshot name)"));
		return (zfs_error(hdl, EZFS_BADSTREAM, errbuf));
	}

	if (DMU_GET_STREAM_HDRTYPE(drrb->drr_versioninfo) == DMU_SUBSTREAM) {
		char nonpackage_sendfs[ZFS_MAXNAMELEN];
		if (sendfs == NULL) {
			/*
			 * We were not called from zfs_receive_package(). Get
			 * the fs specified by 'zfs send'.
			 */
			char *cp;
			(void) strlcpy(nonpackage_sendfs,
			    drr.drr_u.drr_begin.drr_toname, ZFS_MAXNAMELEN);
			if ((cp = strchr(nonpackage_sendfs, '@')) != NULL)
				*cp = '\0';
			sendfs = nonpackage_sendfs;
		}
		return (zfs_receive_one(hdl, infd, tosnap, flags,
		    &drr, &drr_noswap, sendfs, stream_nv, stream_avl,
		    top_zfs, cleanup_fd, action_handlep));
	} else {
		assert(DMU_GET_STREAM_HDRTYPE(drrb->drr_versioninfo) ==
		    DMU_COMPOUNDSTREAM);
		return (zfs_receive_package(hdl, infd, tosnap, flags,
		    &drr, &zcksum, top_zfs, cleanup_fd, action_handlep));
	}
}

/*
 * Restores a backup of tosnap from the file descriptor specified by infd.
 * Return 0 on total success, -2 if some things couldn't be
 * destroyed/renamed/promoted, -1 if some things couldn't be received.
 * (-1 will override -2).
 */
int
zfs_receive(libzfs_handle_t *hdl, const char *tosnap, recvflags_t *flags,
    int infd, avl_tree_t *stream_avl)
{
	char *top_zfs = NULL;
	int err;
	int cleanup_fd;
	uint64_t action_handle = 0;

	cleanup_fd = open(ZFS_DEV, O_RDWR);
	VERIFY(cleanup_fd >= 0);

	err = zfs_receive_impl(hdl, tosnap, flags, infd, NULL, NULL,
	    stream_avl, &top_zfs, cleanup_fd, &action_handle);

	VERIFY(0 == close(cleanup_fd));

	if (err == 0 && !flags->nomount && top_zfs) {
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
