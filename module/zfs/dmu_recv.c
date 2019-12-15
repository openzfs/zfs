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
 * Copyright 2011 Nexenta Systems, Inc. All rights reserved.
 * Copyright (c) 2011, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2014, Joyent, Inc. All rights reserved.
 * Copyright 2014 HybridCluster. All rights reserved.
 * Copyright (c) 2018, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
 */

#include <sys/dmu.h>
#include <sys/dmu_impl.h>
#include <sys/dmu_send.h>
#include <sys/dmu_recv.h>
#include <sys/dmu_tx.h>
#include <sys/dbuf.h>
#include <sys/dnode.h>
#include <sys/zfs_context.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_traverse.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_synctask.h>
#include <sys/zfs_ioctl.h>
#include <sys/zap.h>
#include <sys/zvol.h>
#include <sys/zio_checksum.h>
#include <sys/zfs_znode.h>
#include <zfs_fletcher.h>
#include <sys/avl.h>
#include <sys/ddt.h>
#include <sys/zfs_onexit.h>
#include <sys/dmu_send.h>
#include <sys/dsl_destroy.h>
#include <sys/blkptr.h>
#include <sys/dsl_bookmark.h>
#include <sys/zfeature.h>
#include <sys/bqueue.h>
#include <sys/objlist.h>
#ifdef _KERNEL
#include <sys/zfs_vfsops.h>
#endif
#include <sys/zfs_file.h>

int zfs_recv_queue_length = SPA_MAXBLOCKSIZE;
int zfs_recv_queue_ff = 20;

static char *dmu_recv_tag = "dmu_recv_tag";
const char *recv_clone_name = "%recv";

static int receive_read_payload_and_next_header(dmu_recv_cookie_t *ra, int len,
    void *buf);

struct receive_record_arg {
	dmu_replay_record_t header;
	void *payload; /* Pointer to a buffer containing the payload */
	/*
	 * If the record is a write, pointer to the arc_buf_t containing the
	 * payload.
	 */
	arc_buf_t *arc_buf;
	int payload_size;
	uint64_t bytes_read; /* bytes read from stream when record created */
	boolean_t eos_marker; /* Marks the end of the stream */
	bqueue_node_t node;
};

struct receive_writer_arg {
	objset_t *os;
	boolean_t byteswap;
	bqueue_t q;

	/*
	 * These three args are used to signal to the main thread that we're
	 * done.
	 */
	kmutex_t mutex;
	kcondvar_t cv;
	boolean_t done;

	int err;
	/* A map from guid to dataset to help handle dedup'd streams. */
	avl_tree_t *guid_to_ds_map;
	boolean_t resumable;
	boolean_t raw;   /* DMU_BACKUP_FEATURE_RAW set */
	boolean_t spill; /* DRR_FLAG_SPILL_BLOCK set */
	uint64_t last_object;
	uint64_t last_offset;
	uint64_t max_object; /* highest object ID referenced in stream */
	uint64_t bytes_read; /* bytes read when current record created */

	/* Encryption parameters for the last received DRR_OBJECT_RANGE */
	boolean_t or_crypt_params_present;
	uint64_t or_firstobj;
	uint64_t or_numslots;
	uint8_t or_salt[ZIO_DATA_SALT_LEN];
	uint8_t or_iv[ZIO_DATA_IV_LEN];
	uint8_t or_mac[ZIO_DATA_MAC_LEN];
	boolean_t or_byteorder;
};

typedef struct guid_map_entry {
	uint64_t	guid;
	boolean_t	raw;
	dsl_dataset_t	*gme_ds;
	avl_node_t	avlnode;
} guid_map_entry_t;

typedef struct dmu_recv_begin_arg {
	const char *drba_origin;
	dmu_recv_cookie_t *drba_cookie;
	cred_t *drba_cred;
	dsl_crypto_params_t *drba_dcp;
} dmu_recv_begin_arg_t;

static void
byteswap_record(dmu_replay_record_t *drr)
{
#define	DO64(X) (drr->drr_u.X = BSWAP_64(drr->drr_u.X))
#define	DO32(X) (drr->drr_u.X = BSWAP_32(drr->drr_u.X))
	drr->drr_type = BSWAP_32(drr->drr_type);
	drr->drr_payloadlen = BSWAP_32(drr->drr_payloadlen);

	switch (drr->drr_type) {
	case DRR_BEGIN:
		DO64(drr_begin.drr_magic);
		DO64(drr_begin.drr_versioninfo);
		DO64(drr_begin.drr_creation_time);
		DO32(drr_begin.drr_type);
		DO32(drr_begin.drr_flags);
		DO64(drr_begin.drr_toguid);
		DO64(drr_begin.drr_fromguid);
		break;
	case DRR_OBJECT:
		DO64(drr_object.drr_object);
		DO32(drr_object.drr_type);
		DO32(drr_object.drr_bonustype);
		DO32(drr_object.drr_blksz);
		DO32(drr_object.drr_bonuslen);
		DO32(drr_object.drr_raw_bonuslen);
		DO64(drr_object.drr_toguid);
		DO64(drr_object.drr_maxblkid);
		break;
	case DRR_FREEOBJECTS:
		DO64(drr_freeobjects.drr_firstobj);
		DO64(drr_freeobjects.drr_numobjs);
		DO64(drr_freeobjects.drr_toguid);
		break;
	case DRR_WRITE:
		DO64(drr_write.drr_object);
		DO32(drr_write.drr_type);
		DO64(drr_write.drr_offset);
		DO64(drr_write.drr_logical_size);
		DO64(drr_write.drr_toguid);
		ZIO_CHECKSUM_BSWAP(&drr->drr_u.drr_write.drr_key.ddk_cksum);
		DO64(drr_write.drr_key.ddk_prop);
		DO64(drr_write.drr_compressed_size);
		break;
	case DRR_WRITE_BYREF:
		DO64(drr_write_byref.drr_object);
		DO64(drr_write_byref.drr_offset);
		DO64(drr_write_byref.drr_length);
		DO64(drr_write_byref.drr_toguid);
		DO64(drr_write_byref.drr_refguid);
		DO64(drr_write_byref.drr_refobject);
		DO64(drr_write_byref.drr_refoffset);
		ZIO_CHECKSUM_BSWAP(&drr->drr_u.drr_write_byref.
		    drr_key.ddk_cksum);
		DO64(drr_write_byref.drr_key.ddk_prop);
		break;
	case DRR_WRITE_EMBEDDED:
		DO64(drr_write_embedded.drr_object);
		DO64(drr_write_embedded.drr_offset);
		DO64(drr_write_embedded.drr_length);
		DO64(drr_write_embedded.drr_toguid);
		DO32(drr_write_embedded.drr_lsize);
		DO32(drr_write_embedded.drr_psize);
		break;
	case DRR_FREE:
		DO64(drr_free.drr_object);
		DO64(drr_free.drr_offset);
		DO64(drr_free.drr_length);
		DO64(drr_free.drr_toguid);
		break;
	case DRR_SPILL:
		DO64(drr_spill.drr_object);
		DO64(drr_spill.drr_length);
		DO64(drr_spill.drr_toguid);
		DO64(drr_spill.drr_compressed_size);
		DO32(drr_spill.drr_type);
		break;
	case DRR_OBJECT_RANGE:
		DO64(drr_object_range.drr_firstobj);
		DO64(drr_object_range.drr_numslots);
		DO64(drr_object_range.drr_toguid);
		break;
	case DRR_REDACT:
		DO64(drr_redact.drr_object);
		DO64(drr_redact.drr_offset);
		DO64(drr_redact.drr_length);
		DO64(drr_redact.drr_toguid);
		break;
	case DRR_END:
		DO64(drr_end.drr_toguid);
		ZIO_CHECKSUM_BSWAP(&drr->drr_u.drr_end.drr_checksum);
		break;
	default:
		break;
	}

	if (drr->drr_type != DRR_BEGIN) {
		ZIO_CHECKSUM_BSWAP(&drr->drr_u.drr_checksum.drr_checksum);
	}

#undef DO64
#undef DO32
}

static boolean_t
redact_snaps_contains(uint64_t *snaps, uint64_t num_snaps, uint64_t guid)
{
	for (int i = 0; i < num_snaps; i++) {
		if (snaps[i] == guid)
			return (B_TRUE);
	}
	return (B_FALSE);
}

/*
 * Check that the new stream we're trying to receive is redacted with respect to
 * a subset of the snapshots that the origin was redacted with respect to.  For
 * the reasons behind this, see the man page on redacted zfs sends and receives.
 */
static boolean_t
compatible_redact_snaps(uint64_t *origin_snaps, uint64_t origin_num_snaps,
    uint64_t *redact_snaps, uint64_t num_redact_snaps)
{
	/*
	 * Short circuit the comparison; if we are redacted with respect to
	 * more snapshots than the origin, we can't be redacted with respect
	 * to a subset.
	 */
	if (num_redact_snaps > origin_num_snaps) {
		return (B_FALSE);
	}

	for (int i = 0; i < num_redact_snaps; i++) {
		if (!redact_snaps_contains(origin_snaps, origin_num_snaps,
		    redact_snaps[i])) {
			return (B_FALSE);
		}
	}
	return (B_TRUE);
}

static boolean_t
redact_check(dmu_recv_begin_arg_t *drba, dsl_dataset_t *origin)
{
	uint64_t *origin_snaps;
	uint64_t origin_num_snaps;
	dmu_recv_cookie_t *drc = drba->drba_cookie;
	struct drr_begin *drrb = drc->drc_drrb;
	int featureflags = DMU_GET_FEATUREFLAGS(drrb->drr_versioninfo);
	int err = 0;
	boolean_t ret = B_TRUE;
	uint64_t *redact_snaps;
	uint_t numredactsnaps;

	/*
	 * If this is a full send stream, we're safe no matter what.
	 */
	if (drrb->drr_fromguid == 0)
		return (ret);

	VERIFY(dsl_dataset_get_uint64_array_feature(origin,
	    SPA_FEATURE_REDACTED_DATASETS, &origin_num_snaps, &origin_snaps));

	if (nvlist_lookup_uint64_array(drc->drc_begin_nvl,
	    BEGINNV_REDACT_FROM_SNAPS, &redact_snaps, &numredactsnaps) ==
	    0) {
		/*
		 * If the send stream was sent from the redaction bookmark or
		 * the redacted version of the dataset, then we're safe.  Verify
		 * that this is from the a compatible redaction bookmark or
		 * redacted dataset.
		 */
		if (!compatible_redact_snaps(origin_snaps, origin_num_snaps,
		    redact_snaps, numredactsnaps)) {
			err = EINVAL;
		}
	} else if (featureflags & DMU_BACKUP_FEATURE_REDACTED) {
		/*
		 * If the stream is redacted, it must be redacted with respect
		 * to a subset of what the origin is redacted with respect to.
		 * See case number 2 in the zfs man page section on redacted zfs
		 * send.
		 */
		err = nvlist_lookup_uint64_array(drc->drc_begin_nvl,
		    BEGINNV_REDACT_SNAPS, &redact_snaps, &numredactsnaps);

		if (err != 0 || !compatible_redact_snaps(origin_snaps,
		    origin_num_snaps, redact_snaps, numredactsnaps)) {
			err = EINVAL;
		}
	} else if (!redact_snaps_contains(origin_snaps, origin_num_snaps,
	    drrb->drr_toguid)) {
		/*
		 * If the stream isn't redacted but the origin is, this must be
		 * one of the snapshots the origin is redacted with respect to.
		 * See case number 1 in the zfs man page section on redacted zfs
		 * send.
		 */
		err = EINVAL;
	}

	if (err != 0)
		ret = B_FALSE;
	return (ret);
}

static int
recv_begin_check_existing_impl(dmu_recv_begin_arg_t *drba, dsl_dataset_t *ds,
    uint64_t fromguid, uint64_t featureflags)
{
	uint64_t val;
	uint64_t children;
	int error;
	dsl_pool_t *dp = ds->ds_dir->dd_pool;
	boolean_t encrypted = ds->ds_dir->dd_crypto_obj != 0;
	boolean_t raw = (featureflags & DMU_BACKUP_FEATURE_RAW) != 0;
	boolean_t embed = (featureflags & DMU_BACKUP_FEATURE_EMBED_DATA) != 0;

	/* Temporary clone name must not exist. */
	error = zap_lookup(dp->dp_meta_objset,
	    dsl_dir_phys(ds->ds_dir)->dd_child_dir_zapobj, recv_clone_name,
	    8, 1, &val);
	if (error != ENOENT)
		return (error == 0 ? SET_ERROR(EBUSY) : error);

	/* Resume state must not be set. */
	if (dsl_dataset_has_resume_receive_state(ds))
		return (SET_ERROR(EBUSY));

	/* New snapshot name must not exist. */
	error = zap_lookup(dp->dp_meta_objset,
	    dsl_dataset_phys(ds)->ds_snapnames_zapobj,
	    drba->drba_cookie->drc_tosnap, 8, 1, &val);
	if (error != ENOENT)
		return (error == 0 ? SET_ERROR(EEXIST) : error);

	/* Must not have children if receiving a ZVOL. */
	error = zap_count(dp->dp_meta_objset,
	    dsl_dir_phys(ds->ds_dir)->dd_child_dir_zapobj, &children);
	if (error != 0)
		return (error);
	if (drba->drba_cookie->drc_drrb->drr_type != DMU_OST_ZFS &&
	    children > 0)
		return (SET_ERROR(ZFS_ERR_WRONG_PARENT));

	/*
	 * Check snapshot limit before receiving. We'll recheck again at the
	 * end, but might as well abort before receiving if we're already over
	 * the limit.
	 *
	 * Note that we do not check the file system limit with
	 * dsl_dir_fscount_check because the temporary %clones don't count
	 * against that limit.
	 */
	error = dsl_fs_ss_limit_check(ds->ds_dir, 1, ZFS_PROP_SNAPSHOT_LIMIT,
	    NULL, drba->drba_cred);
	if (error != 0)
		return (error);

	if (fromguid != 0) {
		dsl_dataset_t *snap;
		uint64_t obj = dsl_dataset_phys(ds)->ds_prev_snap_obj;

		/* Can't perform a raw receive on top of a non-raw receive */
		if (!encrypted && raw)
			return (SET_ERROR(EINVAL));

		/* Encryption is incompatible with embedded data */
		if (encrypted && embed)
			return (SET_ERROR(EINVAL));

		/* Find snapshot in this dir that matches fromguid. */
		while (obj != 0) {
			error = dsl_dataset_hold_obj(dp, obj, FTAG,
			    &snap);
			if (error != 0)
				return (SET_ERROR(ENODEV));
			if (snap->ds_dir != ds->ds_dir) {
				dsl_dataset_rele(snap, FTAG);
				return (SET_ERROR(ENODEV));
			}
			if (dsl_dataset_phys(snap)->ds_guid == fromguid)
				break;
			obj = dsl_dataset_phys(snap)->ds_prev_snap_obj;
			dsl_dataset_rele(snap, FTAG);
		}
		if (obj == 0)
			return (SET_ERROR(ENODEV));

		if (drba->drba_cookie->drc_force) {
			drba->drba_cookie->drc_fromsnapobj = obj;
		} else {
			/*
			 * If we are not forcing, there must be no
			 * changes since fromsnap. Raw sends have an
			 * additional constraint that requires that
			 * no "noop" snapshots exist between fromsnap
			 * and tosnap for the IVset checking code to
			 * work properly.
			 */
			if (dsl_dataset_modified_since_snap(ds, snap) ||
			    (raw &&
			    dsl_dataset_phys(ds)->ds_prev_snap_obj !=
			    snap->ds_object)) {
				dsl_dataset_rele(snap, FTAG);
				return (SET_ERROR(ETXTBSY));
			}
			drba->drba_cookie->drc_fromsnapobj =
			    ds->ds_prev->ds_object;
		}

		if (dsl_dataset_feature_is_active(snap,
		    SPA_FEATURE_REDACTED_DATASETS) && !redact_check(drba,
		    snap)) {
			dsl_dataset_rele(snap, FTAG);
			return (SET_ERROR(EINVAL));
		}

		dsl_dataset_rele(snap, FTAG);
	} else {
		/* if full, then must be forced */
		if (!drba->drba_cookie->drc_force)
			return (SET_ERROR(EEXIST));

		/*
		 * We don't support using zfs recv -F to blow away
		 * encrypted filesystems. This would require the
		 * dsl dir to point to the old encryption key and
		 * the new one at the same time during the receive.
		 */
		if ((!encrypted && raw) || encrypted)
			return (SET_ERROR(EINVAL));

		/*
		 * Perform the same encryption checks we would if
		 * we were creating a new dataset from scratch.
		 */
		if (!raw) {
			boolean_t will_encrypt;

			error = dmu_objset_create_crypt_check(
			    ds->ds_dir->dd_parent, drba->drba_dcp,
			    &will_encrypt);
			if (error != 0)
				return (error);

			if (will_encrypt && embed)
				return (SET_ERROR(EINVAL));
		}
	}

	return (0);

}

/*
 * Check that any feature flags used in the data stream we're receiving are
 * supported by the pool we are receiving into.
 *
 * Note that some of the features we explicitly check here have additional
 * (implicit) features they depend on, but those dependencies are enforced
 * through the zfeature_register() calls declaring the features that we
 * explicitly check.
 */
static int
recv_begin_check_feature_flags_impl(uint64_t featureflags, spa_t *spa)
{
	/*
	 * Check if there are any unsupported feature flags.
	 */
	if (!DMU_STREAM_SUPPORTED(featureflags)) {
		return (SET_ERROR(ZFS_ERR_UNKNOWN_SEND_STREAM_FEATURE));
	}

	/* Verify pool version supports SA if SA_SPILL feature set */
	if ((featureflags & DMU_BACKUP_FEATURE_SA_SPILL) &&
	    spa_version(spa) < SPA_VERSION_SA)
		return (SET_ERROR(ENOTSUP));

	/*
	 * LZ4 compressed, embedded, mooched, large blocks, and large_dnodes
	 * in the stream can only be used if those pool features are enabled
	 * because we don't attempt to decompress / un-embed / un-mooch /
	 * split up the blocks / dnodes during the receive process.
	 */
	if ((featureflags & DMU_BACKUP_FEATURE_LZ4) &&
	    !spa_feature_is_enabled(spa, SPA_FEATURE_LZ4_COMPRESS))
		return (SET_ERROR(ENOTSUP));
	if ((featureflags & DMU_BACKUP_FEATURE_EMBED_DATA) &&
	    !spa_feature_is_enabled(spa, SPA_FEATURE_EMBEDDED_DATA))
		return (SET_ERROR(ENOTSUP));
	if ((featureflags & DMU_BACKUP_FEATURE_LARGE_BLOCKS) &&
	    !spa_feature_is_enabled(spa, SPA_FEATURE_LARGE_BLOCKS))
		return (SET_ERROR(ENOTSUP));
	if ((featureflags & DMU_BACKUP_FEATURE_LARGE_DNODE) &&
	    !spa_feature_is_enabled(spa, SPA_FEATURE_LARGE_DNODE))
		return (SET_ERROR(ENOTSUP));

	/*
	 * Receiving redacted streams requires that redacted datasets are
	 * enabled.
	 */
	if ((featureflags & DMU_BACKUP_FEATURE_REDACTED) &&
	    !spa_feature_is_enabled(spa, SPA_FEATURE_REDACTED_DATASETS))
		return (SET_ERROR(ENOTSUP));

	return (0);
}

static int
dmu_recv_begin_check(void *arg, dmu_tx_t *tx)
{
	dmu_recv_begin_arg_t *drba = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	struct drr_begin *drrb = drba->drba_cookie->drc_drrb;
	uint64_t fromguid = drrb->drr_fromguid;
	int flags = drrb->drr_flags;
	ds_hold_flags_t dsflags = 0;
	int error;
	uint64_t featureflags = drba->drba_cookie->drc_featureflags;
	dsl_dataset_t *ds;
	const char *tofs = drba->drba_cookie->drc_tofs;

	/* already checked */
	ASSERT3U(drrb->drr_magic, ==, DMU_BACKUP_MAGIC);
	ASSERT(!(featureflags & DMU_BACKUP_FEATURE_RESUMING));

	if (DMU_GET_STREAM_HDRTYPE(drrb->drr_versioninfo) ==
	    DMU_COMPOUNDSTREAM ||
	    drrb->drr_type >= DMU_OST_NUMTYPES ||
	    ((flags & DRR_FLAG_CLONE) && drba->drba_origin == NULL))
		return (SET_ERROR(EINVAL));

	error = recv_begin_check_feature_flags_impl(featureflags, dp->dp_spa);
	if (error != 0)
		return (error);

	/* Resumable receives require extensible datasets */
	if (drba->drba_cookie->drc_resumable &&
	    !spa_feature_is_enabled(dp->dp_spa, SPA_FEATURE_EXTENSIBLE_DATASET))
		return (SET_ERROR(ENOTSUP));

	if (featureflags & DMU_BACKUP_FEATURE_RAW) {
		/* raw receives require the encryption feature */
		if (!spa_feature_is_enabled(dp->dp_spa, SPA_FEATURE_ENCRYPTION))
			return (SET_ERROR(ENOTSUP));

		/* embedded data is incompatible with encryption and raw recv */
		if (featureflags & DMU_BACKUP_FEATURE_EMBED_DATA)
			return (SET_ERROR(EINVAL));

		/* raw receives require spill block allocation flag */
		if (!(flags & DRR_FLAG_SPILL_BLOCK))
			return (SET_ERROR(ZFS_ERR_SPILL_BLOCK_FLAG_MISSING));
	} else {
		dsflags |= DS_HOLD_FLAG_DECRYPT;
	}

	if ((featureflags & DMU_BACKUP_FEATURE_ZSTD) &&
	    !spa_feature_is_enabled(dp->dp_spa, SPA_FEATURE_ZSTD_COMPRESS))
		return (SET_ERROR(ENOTSUP));

	if (!(DMU_STREAM_SUPPORTED(featureflags)))
		return (SET_ERROR(ENOTSUP));

	error = dsl_dataset_hold_flags(dp, tofs, dsflags, FTAG, &ds);
	if (error == 0) {
		/* target fs already exists; recv into temp clone */

		/* Can't recv a clone into an existing fs */
		if (flags & DRR_FLAG_CLONE || drba->drba_origin) {
			dsl_dataset_rele_flags(ds, dsflags, FTAG);
			return (SET_ERROR(EINVAL));
		}

		error = recv_begin_check_existing_impl(drba, ds, fromguid,
		    featureflags);
		dsl_dataset_rele_flags(ds, dsflags, FTAG);
	} else if (error == ENOENT) {
		/* target fs does not exist; must be a full backup or clone */
		char buf[ZFS_MAX_DATASET_NAME_LEN];
		objset_t *os;

		/*
		 * If it's a non-clone incremental, we are missing the
		 * target fs, so fail the recv.
		 */
		if (fromguid != 0 && !((flags & DRR_FLAG_CLONE) ||
		    drba->drba_origin))
			return (SET_ERROR(ENOENT));

		/*
		 * If we're receiving a full send as a clone, and it doesn't
		 * contain all the necessary free records and freeobject
		 * records, reject it.
		 */
		if (fromguid == 0 && drba->drba_origin != NULL &&
		    !(flags & DRR_FLAG_FREERECORDS))
			return (SET_ERROR(EINVAL));

		/* Open the parent of tofs */
		ASSERT3U(strlen(tofs), <, sizeof (buf));
		(void) strlcpy(buf, tofs, strrchr(tofs, '/') - tofs + 1);
		error = dsl_dataset_hold(dp, buf, FTAG, &ds);
		if (error != 0)
			return (error);

		if ((featureflags & DMU_BACKUP_FEATURE_RAW) == 0 &&
		    drba->drba_origin == NULL) {
			boolean_t will_encrypt;

			/*
			 * Check that we aren't breaking any encryption rules
			 * and that we have all the parameters we need to
			 * create an encrypted dataset if necessary. If we are
			 * making an encrypted dataset the stream can't have
			 * embedded data.
			 */
			error = dmu_objset_create_crypt_check(ds->ds_dir,
			    drba->drba_dcp, &will_encrypt);
			if (error != 0) {
				dsl_dataset_rele(ds, FTAG);
				return (error);
			}

			if (will_encrypt &&
			    (featureflags & DMU_BACKUP_FEATURE_EMBED_DATA)) {
				dsl_dataset_rele(ds, FTAG);
				return (SET_ERROR(EINVAL));
			}
		}

		/*
		 * Check filesystem and snapshot limits before receiving. We'll
		 * recheck snapshot limits again at the end (we create the
		 * filesystems and increment those counts during begin_sync).
		 */
		error = dsl_fs_ss_limit_check(ds->ds_dir, 1,
		    ZFS_PROP_FILESYSTEM_LIMIT, NULL, drba->drba_cred);
		if (error != 0) {
			dsl_dataset_rele(ds, FTAG);
			return (error);
		}

		error = dsl_fs_ss_limit_check(ds->ds_dir, 1,
		    ZFS_PROP_SNAPSHOT_LIMIT, NULL, drba->drba_cred);
		if (error != 0) {
			dsl_dataset_rele(ds, FTAG);
			return (error);
		}

		/* can't recv below anything but filesystems (eg. no ZVOLs) */
		error = dmu_objset_from_ds(ds, &os);
		if (error != 0) {
			dsl_dataset_rele(ds, FTAG);
			return (error);
		}
		if (dmu_objset_type(os) != DMU_OST_ZFS) {
			dsl_dataset_rele(ds, FTAG);
			return (SET_ERROR(ZFS_ERR_WRONG_PARENT));
		}

		if (drba->drba_origin != NULL) {
			dsl_dataset_t *origin;
			error = dsl_dataset_hold_flags(dp, drba->drba_origin,
			    dsflags, FTAG, &origin);
			if (error != 0) {
				dsl_dataset_rele(ds, FTAG);
				return (error);
			}
			if (!origin->ds_is_snapshot) {
				dsl_dataset_rele_flags(origin, dsflags, FTAG);
				dsl_dataset_rele(ds, FTAG);
				return (SET_ERROR(EINVAL));
			}
			if (dsl_dataset_phys(origin)->ds_guid != fromguid &&
			    fromguid != 0) {
				dsl_dataset_rele_flags(origin, dsflags, FTAG);
				dsl_dataset_rele(ds, FTAG);
				return (SET_ERROR(ENODEV));
			}

			if (origin->ds_dir->dd_crypto_obj != 0 &&
			    (featureflags & DMU_BACKUP_FEATURE_EMBED_DATA)) {
				dsl_dataset_rele_flags(origin, dsflags, FTAG);
				dsl_dataset_rele(ds, FTAG);
				return (SET_ERROR(EINVAL));
			}

			/*
			 * If the origin is redacted we need to verify that this
			 * send stream can safely be received on top of the
			 * origin.
			 */
			if (dsl_dataset_feature_is_active(origin,
			    SPA_FEATURE_REDACTED_DATASETS)) {
				if (!redact_check(drba, origin)) {
					dsl_dataset_rele_flags(origin, dsflags,
					    FTAG);
					dsl_dataset_rele_flags(ds, dsflags,
					    FTAG);
					return (SET_ERROR(EINVAL));
				}
			}

			dsl_dataset_rele_flags(origin, dsflags, FTAG);
		}

		dsl_dataset_rele(ds, FTAG);
		error = 0;
	}
	return (error);
}

static void
dmu_recv_begin_sync(void *arg, dmu_tx_t *tx)
{
	dmu_recv_begin_arg_t *drba = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	objset_t *mos = dp->dp_meta_objset;
	dmu_recv_cookie_t *drc = drba->drba_cookie;
	struct drr_begin *drrb = drc->drc_drrb;
	const char *tofs = drc->drc_tofs;
	uint64_t featureflags = drc->drc_featureflags;
	dsl_dataset_t *ds, *newds;
	objset_t *os;
	uint64_t dsobj;
	ds_hold_flags_t dsflags = 0;
	int error;
	uint64_t crflags = 0;
	dsl_crypto_params_t dummy_dcp = { 0 };
	dsl_crypto_params_t *dcp = drba->drba_dcp;

	if (drrb->drr_flags & DRR_FLAG_CI_DATA)
		crflags |= DS_FLAG_CI_DATASET;

	if ((featureflags & DMU_BACKUP_FEATURE_RAW) == 0)
		dsflags |= DS_HOLD_FLAG_DECRYPT;

	/*
	 * Raw, non-incremental recvs always use a dummy dcp with
	 * the raw cmd set. Raw incremental recvs do not use a dcp
	 * since the encryption parameters are already set in stone.
	 */
	if (dcp == NULL && drrb->drr_fromguid == 0 &&
	    drba->drba_origin == NULL) {
		ASSERT3P(dcp, ==, NULL);
		dcp = &dummy_dcp;

		if (featureflags & DMU_BACKUP_FEATURE_RAW)
			dcp->cp_cmd = DCP_CMD_RAW_RECV;
	}

	error = dsl_dataset_hold_flags(dp, tofs, dsflags, FTAG, &ds);
	if (error == 0) {
		/* create temporary clone */
		dsl_dataset_t *snap = NULL;

		if (drba->drba_cookie->drc_fromsnapobj != 0) {
			VERIFY0(dsl_dataset_hold_obj(dp,
			    drba->drba_cookie->drc_fromsnapobj, FTAG, &snap));
			ASSERT3P(dcp, ==, NULL);
		}
		dsobj = dsl_dataset_create_sync(ds->ds_dir, recv_clone_name,
		    snap, crflags, drba->drba_cred, dcp, tx);
		if (drba->drba_cookie->drc_fromsnapobj != 0)
			dsl_dataset_rele(snap, FTAG);
		dsl_dataset_rele_flags(ds, dsflags, FTAG);
	} else {
		dsl_dir_t *dd;
		const char *tail;
		dsl_dataset_t *origin = NULL;

		VERIFY0(dsl_dir_hold(dp, tofs, FTAG, &dd, &tail));

		if (drba->drba_origin != NULL) {
			VERIFY0(dsl_dataset_hold(dp, drba->drba_origin,
			    FTAG, &origin));
			ASSERT3P(dcp, ==, NULL);
		}

		/* Create new dataset. */
		dsobj = dsl_dataset_create_sync(dd, strrchr(tofs, '/') + 1,
		    origin, crflags, drba->drba_cred, dcp, tx);
		if (origin != NULL)
			dsl_dataset_rele(origin, FTAG);
		dsl_dir_rele(dd, FTAG);
		drc->drc_newfs = B_TRUE;
	}
	VERIFY0(dsl_dataset_own_obj_force(dp, dsobj, dsflags, dmu_recv_tag,
	    &newds));
	if (dsl_dataset_feature_is_active(newds,
	    SPA_FEATURE_REDACTED_DATASETS)) {
		/*
		 * If the origin dataset is redacted, the child will be redacted
		 * when we create it.  We clear the new dataset's
		 * redaction info; if it should be redacted, we'll fill
		 * in its information later.
		 */
		dsl_dataset_deactivate_feature(newds,
		    SPA_FEATURE_REDACTED_DATASETS, tx);
	}
	VERIFY0(dmu_objset_from_ds(newds, &os));

	if (drc->drc_resumable) {
		dsl_dataset_zapify(newds, tx);
		if (drrb->drr_fromguid != 0) {
			VERIFY0(zap_add(mos, dsobj, DS_FIELD_RESUME_FROMGUID,
			    8, 1, &drrb->drr_fromguid, tx));
		}
		VERIFY0(zap_add(mos, dsobj, DS_FIELD_RESUME_TOGUID,
		    8, 1, &drrb->drr_toguid, tx));
		VERIFY0(zap_add(mos, dsobj, DS_FIELD_RESUME_TONAME,
		    1, strlen(drrb->drr_toname) + 1, drrb->drr_toname, tx));
		uint64_t one = 1;
		uint64_t zero = 0;
		VERIFY0(zap_add(mos, dsobj, DS_FIELD_RESUME_OBJECT,
		    8, 1, &one, tx));
		VERIFY0(zap_add(mos, dsobj, DS_FIELD_RESUME_OFFSET,
		    8, 1, &zero, tx));
		VERIFY0(zap_add(mos, dsobj, DS_FIELD_RESUME_BYTES,
		    8, 1, &zero, tx));
		if (featureflags & DMU_BACKUP_FEATURE_LARGE_BLOCKS) {
			VERIFY0(zap_add(mos, dsobj, DS_FIELD_RESUME_LARGEBLOCK,
			    8, 1, &one, tx));
		}
		if (featureflags & DMU_BACKUP_FEATURE_EMBED_DATA) {
			VERIFY0(zap_add(mos, dsobj, DS_FIELD_RESUME_EMBEDOK,
			    8, 1, &one, tx));
		}
		if (featureflags & DMU_BACKUP_FEATURE_COMPRESSED) {
			VERIFY0(zap_add(mos, dsobj, DS_FIELD_RESUME_COMPRESSOK,
			    8, 1, &one, tx));
		}
		if (featureflags & DMU_BACKUP_FEATURE_RAW) {
			VERIFY0(zap_add(mos, dsobj, DS_FIELD_RESUME_RAWOK,
			    8, 1, &one, tx));
		}

		uint64_t *redact_snaps;
		uint_t numredactsnaps;
		if (nvlist_lookup_uint64_array(drc->drc_begin_nvl,
		    BEGINNV_REDACT_FROM_SNAPS, &redact_snaps,
		    &numredactsnaps) == 0) {
			VERIFY0(zap_add(mos, dsobj,
			    DS_FIELD_RESUME_REDACT_BOOKMARK_SNAPS,
			    sizeof (*redact_snaps), numredactsnaps,
			    redact_snaps, tx));
		}
	}

	/*
	 * Usually the os->os_encrypted value is tied to the presence of a
	 * DSL Crypto Key object in the dd. However, that will not be received
	 * until dmu_recv_stream(), so we set the value manually for now.
	 */
	if (featureflags & DMU_BACKUP_FEATURE_RAW) {
		os->os_encrypted = B_TRUE;
		drba->drba_cookie->drc_raw = B_TRUE;
	}


	if (featureflags & DMU_BACKUP_FEATURE_REDACTED) {
		uint64_t *redact_snaps;
		uint_t numredactsnaps;
		VERIFY0(nvlist_lookup_uint64_array(drc->drc_begin_nvl,
		    BEGINNV_REDACT_SNAPS, &redact_snaps, &numredactsnaps));
		dsl_dataset_activate_redaction(newds, redact_snaps,
		    numredactsnaps, tx);
	}

	dmu_buf_will_dirty(newds->ds_dbuf, tx);
	dsl_dataset_phys(newds)->ds_flags |= DS_FLAG_INCONSISTENT;

	/*
	 * If we actually created a non-clone, we need to create the objset
	 * in our new dataset. If this is a raw send we postpone this until
	 * dmu_recv_stream() so that we can allocate the metadnode with the
	 * properties from the DRR_BEGIN payload.
	 */
	rrw_enter(&newds->ds_bp_rwlock, RW_READER, FTAG);
	if (BP_IS_HOLE(dsl_dataset_get_blkptr(newds)) &&
	    (featureflags & DMU_BACKUP_FEATURE_RAW) == 0) {
		(void) dmu_objset_create_impl(dp->dp_spa,
		    newds, dsl_dataset_get_blkptr(newds), drrb->drr_type, tx);
	}
	rrw_exit(&newds->ds_bp_rwlock, FTAG);

	drba->drba_cookie->drc_ds = newds;

	spa_history_log_internal_ds(newds, "receive", tx, " ");
}

static int
dmu_recv_resume_begin_check(void *arg, dmu_tx_t *tx)
{
	dmu_recv_begin_arg_t *drba = arg;
	dmu_recv_cookie_t *drc = drba->drba_cookie;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	struct drr_begin *drrb = drc->drc_drrb;
	int error;
	ds_hold_flags_t dsflags = 0;
	dsl_dataset_t *ds;
	const char *tofs = drc->drc_tofs;

	/* already checked */
	ASSERT3U(drrb->drr_magic, ==, DMU_BACKUP_MAGIC);
	ASSERT(drc->drc_featureflags & DMU_BACKUP_FEATURE_RESUMING);

	if (DMU_GET_STREAM_HDRTYPE(drrb->drr_versioninfo) ==
	    DMU_COMPOUNDSTREAM ||
	    drrb->drr_type >= DMU_OST_NUMTYPES)
		return (SET_ERROR(EINVAL));

	/*
	 * This is mostly a sanity check since we should have already done these
	 * checks during a previous attempt to receive the data.
	 */
	error = recv_begin_check_feature_flags_impl(drc->drc_featureflags,
	    dp->dp_spa);
	if (error != 0)
		return (error);

	/* 6 extra bytes for /%recv */
	char recvname[ZFS_MAX_DATASET_NAME_LEN + 6];

	(void) snprintf(recvname, sizeof (recvname), "%s/%s",
	    tofs, recv_clone_name);

	if (drc->drc_featureflags & DMU_BACKUP_FEATURE_RAW) {
		/* raw receives require spill block allocation flag */
		if (!(drrb->drr_flags & DRR_FLAG_SPILL_BLOCK))
			return (SET_ERROR(ZFS_ERR_SPILL_BLOCK_FLAG_MISSING));
	} else {
		dsflags |= DS_HOLD_FLAG_DECRYPT;
	}

	if (dsl_dataset_hold_flags(dp, recvname, dsflags, FTAG, &ds) != 0) {
		/* %recv does not exist; continue in tofs */
		error = dsl_dataset_hold_flags(dp, tofs, dsflags, FTAG, &ds);
		if (error != 0)
			return (error);
	}

	/* check that ds is marked inconsistent */
	if (!DS_IS_INCONSISTENT(ds)) {
		dsl_dataset_rele_flags(ds, dsflags, FTAG);
		return (SET_ERROR(EINVAL));
	}

	/* check that there is resuming data, and that the toguid matches */
	if (!dsl_dataset_is_zapified(ds)) {
		dsl_dataset_rele_flags(ds, dsflags, FTAG);
		return (SET_ERROR(EINVAL));
	}
	uint64_t val;
	error = zap_lookup(dp->dp_meta_objset, ds->ds_object,
	    DS_FIELD_RESUME_TOGUID, sizeof (val), 1, &val);
	if (error != 0 || drrb->drr_toguid != val) {
		dsl_dataset_rele_flags(ds, dsflags, FTAG);
		return (SET_ERROR(EINVAL));
	}

	/*
	 * Check if the receive is still running.  If so, it will be owned.
	 * Note that nothing else can own the dataset (e.g. after the receive
	 * fails) because it will be marked inconsistent.
	 */
	if (dsl_dataset_has_owner(ds)) {
		dsl_dataset_rele_flags(ds, dsflags, FTAG);
		return (SET_ERROR(EBUSY));
	}

	/* There should not be any snapshots of this fs yet. */
	if (ds->ds_prev != NULL && ds->ds_prev->ds_dir == ds->ds_dir) {
		dsl_dataset_rele_flags(ds, dsflags, FTAG);
		return (SET_ERROR(EINVAL));
	}

	/*
	 * Note: resume point will be checked when we process the first WRITE
	 * record.
	 */

	/* check that the origin matches */
	val = 0;
	(void) zap_lookup(dp->dp_meta_objset, ds->ds_object,
	    DS_FIELD_RESUME_FROMGUID, sizeof (val), 1, &val);
	if (drrb->drr_fromguid != val) {
		dsl_dataset_rele_flags(ds, dsflags, FTAG);
		return (SET_ERROR(EINVAL));
	}

	/*
	 * If we're resuming, and the send is redacted, then the original send
	 * must have been redacted, and must have been redacted with respect to
	 * the same snapshots.
	 */
	if (drc->drc_featureflags & DMU_BACKUP_FEATURE_REDACTED) {
		uint64_t num_ds_redact_snaps;
		uint64_t *ds_redact_snaps;

		uint_t num_stream_redact_snaps;
		uint64_t *stream_redact_snaps;

		if (nvlist_lookup_uint64_array(drc->drc_begin_nvl,
		    BEGINNV_REDACT_SNAPS, &stream_redact_snaps,
		    &num_stream_redact_snaps) != 0) {
			dsl_dataset_rele_flags(ds, dsflags, FTAG);
			return (SET_ERROR(EINVAL));
		}

		if (!dsl_dataset_get_uint64_array_feature(ds,
		    SPA_FEATURE_REDACTED_DATASETS, &num_ds_redact_snaps,
		    &ds_redact_snaps)) {
			dsl_dataset_rele_flags(ds, dsflags, FTAG);
			return (SET_ERROR(EINVAL));
		}

		for (int i = 0; i < num_ds_redact_snaps; i++) {
			if (!redact_snaps_contains(ds_redact_snaps,
			    num_ds_redact_snaps, stream_redact_snaps[i])) {
				dsl_dataset_rele_flags(ds, dsflags, FTAG);
				return (SET_ERROR(EINVAL));
			}
		}
	}
	dsl_dataset_rele_flags(ds, dsflags, FTAG);
	return (0);
}

static void
dmu_recv_resume_begin_sync(void *arg, dmu_tx_t *tx)
{
	dmu_recv_begin_arg_t *drba = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	const char *tofs = drba->drba_cookie->drc_tofs;
	uint64_t featureflags = drba->drba_cookie->drc_featureflags;
	dsl_dataset_t *ds;
	ds_hold_flags_t dsflags = 0;
	/* 6 extra bytes for /%recv */
	char recvname[ZFS_MAX_DATASET_NAME_LEN + 6];

	(void) snprintf(recvname, sizeof (recvname), "%s/%s", tofs,
	    recv_clone_name);

	if (featureflags & DMU_BACKUP_FEATURE_RAW) {
		drba->drba_cookie->drc_raw = B_TRUE;
	} else {
		dsflags |= DS_HOLD_FLAG_DECRYPT;
	}

	if (dsl_dataset_own_force(dp, recvname, dsflags, dmu_recv_tag, &ds)
	    != 0) {
		/* %recv does not exist; continue in tofs */
		VERIFY0(dsl_dataset_own_force(dp, tofs, dsflags, dmu_recv_tag,
		    &ds));
		drba->drba_cookie->drc_newfs = B_TRUE;
	}

	ASSERT(DS_IS_INCONSISTENT(ds));
	rrw_enter(&ds->ds_bp_rwlock, RW_READER, FTAG);
	ASSERT(!BP_IS_HOLE(dsl_dataset_get_blkptr(ds)) ||
	    drba->drba_cookie->drc_raw);
	rrw_exit(&ds->ds_bp_rwlock, FTAG);

	drba->drba_cookie->drc_ds = ds;

	spa_history_log_internal_ds(ds, "resume receive", tx, " ");
}

/*
 * NB: callers *MUST* call dmu_recv_stream() if dmu_recv_begin()
 * succeeds; otherwise we will leak the holds on the datasets.
 */
int
dmu_recv_begin(char *tofs, char *tosnap, dmu_replay_record_t *drr_begin,
    boolean_t force, boolean_t resumable, nvlist_t *localprops,
    nvlist_t *hidden_args, char *origin, dmu_recv_cookie_t *drc,
    zfs_file_t *fp, offset_t *voffp)
{
	dmu_recv_begin_arg_t drba = { 0 };
	int err;

	bzero(drc, sizeof (dmu_recv_cookie_t));
	drc->drc_drr_begin = drr_begin;
	drc->drc_drrb = &drr_begin->drr_u.drr_begin;
	drc->drc_tosnap = tosnap;
	drc->drc_tofs = tofs;
	drc->drc_force = force;
	drc->drc_resumable = resumable;
	drc->drc_cred = CRED();
	drc->drc_clone = (origin != NULL);

	if (drc->drc_drrb->drr_magic == BSWAP_64(DMU_BACKUP_MAGIC)) {
		drc->drc_byteswap = B_TRUE;
		(void) fletcher_4_incremental_byteswap(drr_begin,
		    sizeof (dmu_replay_record_t), &drc->drc_cksum);
		byteswap_record(drr_begin);
	} else if (drc->drc_drrb->drr_magic == DMU_BACKUP_MAGIC) {
		(void) fletcher_4_incremental_native(drr_begin,
		    sizeof (dmu_replay_record_t), &drc->drc_cksum);
	} else {
		return (SET_ERROR(EINVAL));
	}

	drc->drc_fp = fp;
	drc->drc_voff = *voffp;
	drc->drc_featureflags =
	    DMU_GET_FEATUREFLAGS(drc->drc_drrb->drr_versioninfo);

	uint32_t payloadlen = drc->drc_drr_begin->drr_payloadlen;
	void *payload = NULL;
	if (payloadlen != 0)
		payload = kmem_alloc(payloadlen, KM_SLEEP);

	err = receive_read_payload_and_next_header(drc, payloadlen,
	    payload);
	if (err != 0) {
		kmem_free(payload, payloadlen);
		return (err);
	}
	if (payloadlen != 0) {
		err = nvlist_unpack(payload, payloadlen, &drc->drc_begin_nvl,
		    KM_SLEEP);
		kmem_free(payload, payloadlen);
		if (err != 0) {
			kmem_free(drc->drc_next_rrd,
			    sizeof (*drc->drc_next_rrd));
			return (err);
		}
	}

	if (drc->drc_drrb->drr_flags & DRR_FLAG_SPILL_BLOCK)
		drc->drc_spill = B_TRUE;

	drba.drba_origin = origin;
	drba.drba_cookie = drc;
	drba.drba_cred = CRED();

	if (drc->drc_featureflags & DMU_BACKUP_FEATURE_RESUMING) {
		err = dsl_sync_task(tofs,
		    dmu_recv_resume_begin_check, dmu_recv_resume_begin_sync,
		    &drba, 5, ZFS_SPACE_CHECK_NORMAL);
	} else {

		/*
		 * For non-raw, non-incremental, non-resuming receives the
		 * user can specify encryption parameters on the command line
		 * with "zfs recv -o". For these receives we create a dcp and
		 * pass it to the sync task. Creating the dcp will implicitly
		 * remove the encryption params from the localprops nvlist,
		 * which avoids errors when trying to set these normally
		 * read-only properties. Any other kind of receive that
		 * attempts to set these properties will fail as a result.
		 */
		if ((DMU_GET_FEATUREFLAGS(drc->drc_drrb->drr_versioninfo) &
		    DMU_BACKUP_FEATURE_RAW) == 0 &&
		    origin == NULL && drc->drc_drrb->drr_fromguid == 0) {
			err = dsl_crypto_params_create_nvlist(DCP_CMD_NONE,
			    localprops, hidden_args, &drba.drba_dcp);
		}

		if (err == 0) {
			err = dsl_sync_task(tofs,
			    dmu_recv_begin_check, dmu_recv_begin_sync,
			    &drba, 5, ZFS_SPACE_CHECK_NORMAL);
			dsl_crypto_params_free(drba.drba_dcp, !!err);
		}
	}

	if (err != 0) {
		kmem_free(drc->drc_next_rrd, sizeof (*drc->drc_next_rrd));
		nvlist_free(drc->drc_begin_nvl);
	}
	return (err);
}

static int
guid_compare(const void *arg1, const void *arg2)
{
	const guid_map_entry_t *gmep1 = (const guid_map_entry_t *)arg1;
	const guid_map_entry_t *gmep2 = (const guid_map_entry_t *)arg2;

	return (TREE_CMP(gmep1->guid, gmep2->guid));
}

static void
free_guid_map_onexit(void *arg)
{
	avl_tree_t *ca = arg;
	void *cookie = NULL;
	guid_map_entry_t *gmep;

	while ((gmep = avl_destroy_nodes(ca, &cookie)) != NULL) {
		ds_hold_flags_t dsflags = DS_HOLD_FLAG_DECRYPT;

		if (gmep->raw) {
			gmep->gme_ds->ds_objset->os_raw_receive = B_FALSE;
			dsflags &= ~DS_HOLD_FLAG_DECRYPT;
		}

		dsl_dataset_disown(gmep->gme_ds, dsflags, gmep);
		kmem_free(gmep, sizeof (guid_map_entry_t));
	}
	avl_destroy(ca);
	kmem_free(ca, sizeof (avl_tree_t));
}

static int
receive_read(dmu_recv_cookie_t *drc, int len, void *buf)
{
	int done = 0;

	/*
	 * The code doesn't rely on this (lengths being multiples of 8).  See
	 * comment in dump_bytes.
	 */
	ASSERT(len % 8 == 0 ||
	    (drc->drc_featureflags & DMU_BACKUP_FEATURE_RAW) != 0);

	while (done < len) {
		ssize_t resid;
		zfs_file_t *fp;

		fp = drc->drc_fp;
		drc->drc_err = zfs_file_read(fp, (char *)buf + done,
		    len - done, &resid);
		if (resid == len - done) {
			/*
			 * Note: ECKSUM indicates that the receive
			 * was interrupted and can potentially be resumed.
			 */
			drc->drc_err = SET_ERROR(ECKSUM);
		}
		drc->drc_voff += len - done - resid;
		done = len - resid;
		if (drc->drc_err != 0)
			return (drc->drc_err);
	}

	drc->drc_bytes_read += len;

	ASSERT3U(done, ==, len);
	return (0);
}

static inline uint8_t
deduce_nblkptr(dmu_object_type_t bonus_type, uint64_t bonus_size)
{
	if (bonus_type == DMU_OT_SA) {
		return (1);
	} else {
		return (1 +
		    ((DN_OLD_MAX_BONUSLEN -
		    MIN(DN_OLD_MAX_BONUSLEN, bonus_size)) >> SPA_BLKPTRSHIFT));
	}
}

static void
save_resume_state(struct receive_writer_arg *rwa,
    uint64_t object, uint64_t offset, dmu_tx_t *tx)
{
	int txgoff = dmu_tx_get_txg(tx) & TXG_MASK;

	if (!rwa->resumable)
		return;

	/*
	 * We use ds_resume_bytes[] != 0 to indicate that we need to
	 * update this on disk, so it must not be 0.
	 */
	ASSERT(rwa->bytes_read != 0);

	/*
	 * We only resume from write records, which have a valid
	 * (non-meta-dnode) object number.
	 */
	ASSERT(object != 0);

	/*
	 * For resuming to work correctly, we must receive records in order,
	 * sorted by object,offset.  This is checked by the callers, but
	 * assert it here for good measure.
	 */
	ASSERT3U(object, >=, rwa->os->os_dsl_dataset->ds_resume_object[txgoff]);
	ASSERT(object != rwa->os->os_dsl_dataset->ds_resume_object[txgoff] ||
	    offset >= rwa->os->os_dsl_dataset->ds_resume_offset[txgoff]);
	ASSERT3U(rwa->bytes_read, >=,
	    rwa->os->os_dsl_dataset->ds_resume_bytes[txgoff]);

	rwa->os->os_dsl_dataset->ds_resume_object[txgoff] = object;
	rwa->os->os_dsl_dataset->ds_resume_offset[txgoff] = offset;
	rwa->os->os_dsl_dataset->ds_resume_bytes[txgoff] = rwa->bytes_read;
}

noinline static int
receive_object(struct receive_writer_arg *rwa, struct drr_object *drro,
    void *data)
{
	dmu_object_info_t doi;
	dmu_tx_t *tx;
	uint64_t object;
	int err;
	uint8_t dn_slots = drro->drr_dn_slots != 0 ?
	    drro->drr_dn_slots : DNODE_MIN_SLOTS;

	if (drro->drr_type == DMU_OT_NONE ||
	    !DMU_OT_IS_VALID(drro->drr_type) ||
	    !DMU_OT_IS_VALID(drro->drr_bonustype) ||
	    drro->drr_checksumtype >= ZIO_CHECKSUM_FUNCTIONS ||
	    drro->drr_compress >= ZIO_COMPRESS_FUNCTIONS ||
	    P2PHASE(drro->drr_blksz, SPA_MINBLOCKSIZE) ||
	    drro->drr_blksz < SPA_MINBLOCKSIZE ||
	    drro->drr_blksz > spa_maxblocksize(dmu_objset_spa(rwa->os)) ||
	    drro->drr_bonuslen >
	    DN_BONUS_SIZE(spa_maxdnodesize(dmu_objset_spa(rwa->os))) ||
	    dn_slots >
	    (spa_maxdnodesize(dmu_objset_spa(rwa->os)) >> DNODE_SHIFT)) {
		return (SET_ERROR(EINVAL));
	}

	if (rwa->raw) {
		/*
		 * We should have received a DRR_OBJECT_RANGE record
		 * containing this block and stored it in rwa.
		 */
		if (drro->drr_object < rwa->or_firstobj ||
		    drro->drr_object >= rwa->or_firstobj + rwa->or_numslots ||
		    drro->drr_raw_bonuslen < drro->drr_bonuslen ||
		    drro->drr_indblkshift > SPA_MAXBLOCKSHIFT ||
		    drro->drr_nlevels > DN_MAX_LEVELS ||
		    drro->drr_nblkptr > DN_MAX_NBLKPTR ||
		    DN_SLOTS_TO_BONUSLEN(dn_slots) <
		    drro->drr_raw_bonuslen)
			return (SET_ERROR(EINVAL));
	} else {
		/*
		 * The DRR_OBJECT_SPILL flag is valid when the DRR_BEGIN
		 * record indicates this by setting DRR_FLAG_SPILL_BLOCK.
		 */
		if (((drro->drr_flags & ~(DRR_OBJECT_SPILL))) ||
		    (!rwa->spill && DRR_OBJECT_HAS_SPILL(drro->drr_flags))) {
			return (SET_ERROR(EINVAL));
		}

		if (drro->drr_raw_bonuslen != 0 || drro->drr_nblkptr != 0 ||
		    drro->drr_indblkshift != 0 || drro->drr_nlevels != 0) {
			return (SET_ERROR(EINVAL));
		}
	}

	err = dmu_object_info(rwa->os, drro->drr_object, &doi);

	if (err != 0 && err != ENOENT && err != EEXIST)
		return (SET_ERROR(EINVAL));

	if (drro->drr_object > rwa->max_object)
		rwa->max_object = drro->drr_object;

	/*
	 * If we are losing blkptrs or changing the block size this must
	 * be a new file instance.  We must clear out the previous file
	 * contents before we can change this type of metadata in the dnode.
	 * Raw receives will also check that the indirect structure of the
	 * dnode hasn't changed.
	 */
	if (err == 0) {
		uint32_t indblksz = drro->drr_indblkshift ?
		    1ULL << drro->drr_indblkshift : 0;
		int nblkptr = deduce_nblkptr(drro->drr_bonustype,
		    drro->drr_bonuslen);
		boolean_t did_free = B_FALSE;

		object = drro->drr_object;

		/* nblkptr should be bounded by the bonus size and type */
		if (rwa->raw && nblkptr != drro->drr_nblkptr)
			return (SET_ERROR(EINVAL));

		/*
		 * Check for indicators that the object was freed and
		 * reallocated. For all sends, these indicators are:
		 *     - A changed block size
		 *     - A smaller nblkptr
		 *     - A changed dnode size
		 * For raw sends we also check a few other fields to
		 * ensure we are preserving the objset structure exactly
		 * as it was on the receive side:
		 *     - A changed indirect block size
		 *     - A smaller nlevels
		 */
		if (drro->drr_blksz != doi.doi_data_block_size ||
		    nblkptr < doi.doi_nblkptr ||
		    dn_slots != doi.doi_dnodesize >> DNODE_SHIFT ||
		    (rwa->raw &&
		    (indblksz != doi.doi_metadata_block_size ||
		    drro->drr_nlevels < doi.doi_indirection))) {
			err = dmu_free_long_range(rwa->os, drro->drr_object,
			    0, DMU_OBJECT_END);
			if (err != 0)
				return (SET_ERROR(EINVAL));
			else
				did_free = B_TRUE;
		}

		/*
		 * The dmu does not currently support decreasing nlevels
		 * or changing the number of dnode slots on an object. For
		 * non-raw sends, this does not matter and the new object
		 * can just use the previous one's nlevels. For raw sends,
		 * however, the structure of the received dnode (including
		 * nlevels and dnode slots) must match that of the send
		 * side. Therefore, instead of using dmu_object_reclaim(),
		 * we must free the object completely and call
		 * dmu_object_claim_dnsize() instead.
		 */
		if ((rwa->raw && drro->drr_nlevels < doi.doi_indirection) ||
		    dn_slots != doi.doi_dnodesize >> DNODE_SHIFT) {
			err = dmu_free_long_object(rwa->os, drro->drr_object);
			if (err != 0)
				return (SET_ERROR(EINVAL));

			txg_wait_synced(dmu_objset_pool(rwa->os), 0);
			object = DMU_NEW_OBJECT;
		}

		/*
		 * For raw receives, free everything beyond the new incoming
		 * maxblkid. Normally this would be done with a DRR_FREE
		 * record that would come after this DRR_OBJECT record is
		 * processed. However, for raw receives we manually set the
		 * maxblkid from the drr_maxblkid and so we must first free
		 * everything above that blkid to ensure the DMU is always
		 * consistent with itself. We will never free the first block
		 * of the object here because a maxblkid of 0 could indicate
		 * an object with a single block or one with no blocks. This
		 * free may be skipped when dmu_free_long_range() was called
		 * above since it covers the entire object's contents.
		 */
		if (rwa->raw && object != DMU_NEW_OBJECT && !did_free) {
			err = dmu_free_long_range(rwa->os, drro->drr_object,
			    (drro->drr_maxblkid + 1) * doi.doi_data_block_size,
			    DMU_OBJECT_END);
			if (err != 0)
				return (SET_ERROR(EINVAL));
		}
	} else if (err == EEXIST) {
		/*
		 * The object requested is currently an interior slot of a
		 * multi-slot dnode. This will be resolved when the next txg
		 * is synced out, since the send stream will have told us
		 * to free this slot when we freed the associated dnode
		 * earlier in the stream.
		 */
		txg_wait_synced(dmu_objset_pool(rwa->os), 0);

		if (dmu_object_info(rwa->os, drro->drr_object, NULL) != ENOENT)
			return (SET_ERROR(EINVAL));

		/* object was freed and we are about to allocate a new one */
		object = DMU_NEW_OBJECT;
	} else {
		/* object is free and we are about to allocate a new one */
		object = DMU_NEW_OBJECT;
	}

	/*
	 * If this is a multi-slot dnode there is a chance that this
	 * object will expand into a slot that is already used by
	 * another object from the previous snapshot. We must free
	 * these objects before we attempt to allocate the new dnode.
	 */
	if (dn_slots > 1) {
		boolean_t need_sync = B_FALSE;

		for (uint64_t slot = drro->drr_object + 1;
		    slot < drro->drr_object + dn_slots;
		    slot++) {
			dmu_object_info_t slot_doi;

			err = dmu_object_info(rwa->os, slot, &slot_doi);
			if (err == ENOENT || err == EEXIST)
				continue;
			else if (err != 0)
				return (err);

			err = dmu_free_long_object(rwa->os, slot);
			if (err != 0)
				return (err);

			need_sync = B_TRUE;
		}

		if (need_sync)
			txg_wait_synced(dmu_objset_pool(rwa->os), 0);
	}

	tx = dmu_tx_create(rwa->os);
	dmu_tx_hold_bonus(tx, object);
	dmu_tx_hold_write(tx, object, 0, 0);
	err = dmu_tx_assign(tx, TXG_WAIT);
	if (err != 0) {
		dmu_tx_abort(tx);
		return (err);
	}

	if (object == DMU_NEW_OBJECT) {
		/* Currently free, wants to be allocated */
		err = dmu_object_claim_dnsize(rwa->os, drro->drr_object,
		    drro->drr_type, drro->drr_blksz,
		    drro->drr_bonustype, drro->drr_bonuslen,
		    dn_slots << DNODE_SHIFT, tx);
	} else if (drro->drr_type != doi.doi_type ||
	    drro->drr_blksz != doi.doi_data_block_size ||
	    drro->drr_bonustype != doi.doi_bonus_type ||
	    drro->drr_bonuslen != doi.doi_bonus_size) {
		/* Currently allocated, but with different properties */
		err = dmu_object_reclaim_dnsize(rwa->os, drro->drr_object,
		    drro->drr_type, drro->drr_blksz,
		    drro->drr_bonustype, drro->drr_bonuslen,
		    dn_slots << DNODE_SHIFT, rwa->spill ?
		    DRR_OBJECT_HAS_SPILL(drro->drr_flags) : B_FALSE, tx);
	} else if (rwa->spill && !DRR_OBJECT_HAS_SPILL(drro->drr_flags)) {
		/*
		 * Currently allocated, the existing version of this object
		 * may reference a spill block that is no longer allocated
		 * at the source and needs to be freed.
		 */
		err = dmu_object_rm_spill(rwa->os, drro->drr_object, tx);
	}

	if (err != 0) {
		dmu_tx_commit(tx);
		return (SET_ERROR(EINVAL));
	}

	if (rwa->or_crypt_params_present) {
		/*
		 * Set the crypt params for the buffer associated with this
		 * range of dnodes.  This causes the blkptr_t to have the
		 * same crypt params (byteorder, salt, iv, mac) as on the
		 * sending side.
		 *
		 * Since we are committing this tx now, it is possible for
		 * the dnode block to end up on-disk with the incorrect MAC,
		 * if subsequent objects in this block are received in a
		 * different txg.  However, since the dataset is marked as
		 * inconsistent, no code paths will do a non-raw read (or
		 * decrypt the block / verify the MAC). The receive code and
		 * scrub code can safely do raw reads and verify the
		 * checksum.  They don't need to verify the MAC.
		 */
		dmu_buf_t *db = NULL;
		uint64_t offset = rwa->or_firstobj * DNODE_MIN_SIZE;

		err = dmu_buf_hold_by_dnode(DMU_META_DNODE(rwa->os),
		    offset, FTAG, &db, DMU_READ_PREFETCH | DMU_READ_NO_DECRYPT);
		if (err != 0) {
			dmu_tx_commit(tx);
			return (SET_ERROR(EINVAL));
		}

		dmu_buf_set_crypt_params(db, rwa->or_byteorder,
		    rwa->or_salt, rwa->or_iv, rwa->or_mac, tx);

		dmu_buf_rele(db, FTAG);

		rwa->or_crypt_params_present = B_FALSE;
	}

	dmu_object_set_checksum(rwa->os, drro->drr_object,
	    drro->drr_checksumtype, tx);
	dmu_object_set_compress(rwa->os, drro->drr_object,
	    drro->drr_compress, tx);

	/* handle more restrictive dnode structuring for raw recvs */
	if (rwa->raw) {
		/*
		 * Set the indirect block size, block shift, nlevels.
		 * This will not fail because we ensured all of the
		 * blocks were freed earlier if this is a new object.
		 * For non-new objects block size and indirect block
		 * shift cannot change and nlevels can only increase.
		 */
		VERIFY0(dmu_object_set_blocksize(rwa->os, drro->drr_object,
		    drro->drr_blksz, drro->drr_indblkshift, tx));
		VERIFY0(dmu_object_set_nlevels(rwa->os, drro->drr_object,
		    drro->drr_nlevels, tx));

		/*
		 * Set the maxblkid. This will always succeed because
		 * we freed all blocks beyond the new maxblkid above.
		 */
		VERIFY0(dmu_object_set_maxblkid(rwa->os, drro->drr_object,
		    drro->drr_maxblkid, tx));
	}

	if (data != NULL) {
		dmu_buf_t *db;
		dnode_t *dn;
		uint32_t flags = DMU_READ_NO_PREFETCH;

		if (rwa->raw)
			flags |= DMU_READ_NO_DECRYPT;

		VERIFY0(dnode_hold(rwa->os, drro->drr_object, FTAG, &dn));
		VERIFY0(dmu_bonus_hold_by_dnode(dn, FTAG, &db, flags));

		dmu_buf_will_dirty(db, tx);

		ASSERT3U(db->db_size, >=, drro->drr_bonuslen);
		bcopy(data, db->db_data, DRR_OBJECT_PAYLOAD_SIZE(drro));

		/*
		 * Raw bonus buffers have their byteorder determined by the
		 * DRR_OBJECT_RANGE record.
		 */
		if (rwa->byteswap && !rwa->raw) {
			dmu_object_byteswap_t byteswap =
			    DMU_OT_BYTESWAP(drro->drr_bonustype);
			dmu_ot_byteswap[byteswap].ob_func(db->db_data,
			    DRR_OBJECT_PAYLOAD_SIZE(drro));
		}
		dmu_buf_rele(db, FTAG);
		dnode_rele(dn, FTAG);
	}
	dmu_tx_commit(tx);

	return (0);
}

/* ARGSUSED */
noinline static int
receive_freeobjects(struct receive_writer_arg *rwa,
    struct drr_freeobjects *drrfo)
{
	uint64_t obj;
	int next_err = 0;

	if (drrfo->drr_firstobj + drrfo->drr_numobjs < drrfo->drr_firstobj)
		return (SET_ERROR(EINVAL));

	for (obj = drrfo->drr_firstobj == 0 ? 1 : drrfo->drr_firstobj;
	    obj < drrfo->drr_firstobj + drrfo->drr_numobjs &&
	    obj < DN_MAX_OBJECT && next_err == 0;
	    next_err = dmu_object_next(rwa->os, &obj, FALSE, 0)) {
		dmu_object_info_t doi;
		int err;

		err = dmu_object_info(rwa->os, obj, &doi);
		if (err == ENOENT)
			continue;
		else if (err != 0)
			return (err);

		err = dmu_free_long_object(rwa->os, obj);

		if (err != 0)
			return (err);
	}
	if (next_err != ESRCH)
		return (next_err);
	return (0);
}

noinline static int
receive_write(struct receive_writer_arg *rwa, struct drr_write *drrw,
    arc_buf_t *abuf)
{
	int err;
	dmu_tx_t *tx;
	dnode_t *dn;

	if (drrw->drr_offset + drrw->drr_logical_size < drrw->drr_offset ||
	    !DMU_OT_IS_VALID(drrw->drr_type))
		return (SET_ERROR(EINVAL));

	/*
	 * For resuming to work, records must be in increasing order
	 * by (object, offset).
	 */
	if (drrw->drr_object < rwa->last_object ||
	    (drrw->drr_object == rwa->last_object &&
	    drrw->drr_offset < rwa->last_offset)) {
		return (SET_ERROR(EINVAL));
	}
	rwa->last_object = drrw->drr_object;
	rwa->last_offset = drrw->drr_offset;

	if (rwa->last_object > rwa->max_object)
		rwa->max_object = rwa->last_object;

	if (dmu_object_info(rwa->os, drrw->drr_object, NULL) != 0)
		return (SET_ERROR(EINVAL));

	tx = dmu_tx_create(rwa->os);
	dmu_tx_hold_write(tx, drrw->drr_object,
	    drrw->drr_offset, drrw->drr_logical_size);
	err = dmu_tx_assign(tx, TXG_WAIT);
	if (err != 0) {
		dmu_tx_abort(tx);
		return (err);
	}

	if (rwa->byteswap && !arc_is_encrypted(abuf) &&
	    arc_get_compression(abuf) == ZIO_COMPRESS_OFF) {
		dmu_object_byteswap_t byteswap =
		    DMU_OT_BYTESWAP(drrw->drr_type);
		dmu_ot_byteswap[byteswap].ob_func(abuf->b_data,
		    DRR_WRITE_PAYLOAD_SIZE(drrw));
	}

	/* use the bonus buf to look up the dnode in dmu_assign_arcbuf */
	VERIFY0(dnode_hold(rwa->os, drrw->drr_object, FTAG, &dn));
	err = dmu_assign_arcbuf_by_dnode(dn, drrw->drr_offset, abuf, tx);
	if (err != 0) {
		dnode_rele(dn, FTAG);
		dmu_tx_commit(tx);
		return (err);
	}
	dnode_rele(dn, FTAG);

	/*
	 * Note: If the receive fails, we want the resume stream to start
	 * with the same record that we last successfully received (as opposed
	 * to the next record), so that we can verify that we are
	 * resuming from the correct location.
	 */
	save_resume_state(rwa, drrw->drr_object, drrw->drr_offset, tx);
	dmu_tx_commit(tx);

	return (0);
}

/*
 * Handle a DRR_WRITE_BYREF record.  This record is used in dedup'ed
 * streams to refer to a copy of the data that is already on the
 * system because it came in earlier in the stream.  This function
 * finds the earlier copy of the data, and uses that copy instead of
 * data from the stream to fulfill this write.
 */
noinline static int
receive_write_byref(struct receive_writer_arg *rwa,
    struct drr_write_byref *drrwbr)
{
	dmu_tx_t *tx;
	int err;
	guid_map_entry_t gmesrch;
	guid_map_entry_t *gmep;
	avl_index_t where;
	objset_t *ref_os = NULL;
	int flags = DMU_READ_PREFETCH;
	dmu_buf_t *dbp;

	if (drrwbr->drr_offset + drrwbr->drr_length < drrwbr->drr_offset)
		return (SET_ERROR(EINVAL));

	/*
	 * If the GUID of the referenced dataset is different from the
	 * GUID of the target dataset, find the referenced dataset.
	 */
	if (drrwbr->drr_toguid != drrwbr->drr_refguid) {
		gmesrch.guid = drrwbr->drr_refguid;
		if ((gmep = avl_find(rwa->guid_to_ds_map, &gmesrch,
		    &where)) == NULL) {
			return (SET_ERROR(EINVAL));
		}
		if (dmu_objset_from_ds(gmep->gme_ds, &ref_os))
			return (SET_ERROR(EINVAL));
	} else {
		ref_os = rwa->os;
	}

	if (drrwbr->drr_object > rwa->max_object)
		rwa->max_object = drrwbr->drr_object;

	if (rwa->raw)
		flags |= DMU_READ_NO_DECRYPT;

	/* may return either a regular db or an encrypted one */
	err = dmu_buf_hold(ref_os, drrwbr->drr_refobject,
	    drrwbr->drr_refoffset, FTAG, &dbp, flags);
	if (err != 0)
		return (err);

	tx = dmu_tx_create(rwa->os);

	dmu_tx_hold_write(tx, drrwbr->drr_object,
	    drrwbr->drr_offset, drrwbr->drr_length);
	err = dmu_tx_assign(tx, TXG_WAIT);
	if (err != 0) {
		dmu_tx_abort(tx);
		return (err);
	}

	if (rwa->raw) {
		dmu_copy_from_buf(rwa->os, drrwbr->drr_object,
		    drrwbr->drr_offset, dbp, tx);
	} else {
		dmu_write(rwa->os, drrwbr->drr_object,
		    drrwbr->drr_offset, drrwbr->drr_length, dbp->db_data, tx);
	}
	dmu_buf_rele(dbp, FTAG);

	/* See comment in restore_write. */
	save_resume_state(rwa, drrwbr->drr_object, drrwbr->drr_offset, tx);
	dmu_tx_commit(tx);
	return (0);
}

static int
receive_write_embedded(struct receive_writer_arg *rwa,
    struct drr_write_embedded *drrwe, void *data)
{
	dmu_tx_t *tx;
	int err;

	if (drrwe->drr_offset + drrwe->drr_length < drrwe->drr_offset)
		return (SET_ERROR(EINVAL));

	if (drrwe->drr_psize > BPE_PAYLOAD_SIZE)
		return (SET_ERROR(EINVAL));

	if (drrwe->drr_etype >= NUM_BP_EMBEDDED_TYPES)
		return (SET_ERROR(EINVAL));
	if (drrwe->drr_compression >= ZIO_COMPRESS_FUNCTIONS)
		return (SET_ERROR(EINVAL));
	if (rwa->raw)
		return (SET_ERROR(EINVAL));

	if (drrwe->drr_object > rwa->max_object)
		rwa->max_object = drrwe->drr_object;

	tx = dmu_tx_create(rwa->os);

	dmu_tx_hold_write(tx, drrwe->drr_object,
	    drrwe->drr_offset, drrwe->drr_length);
	err = dmu_tx_assign(tx, TXG_WAIT);
	if (err != 0) {
		dmu_tx_abort(tx);
		return (err);
	}

	dmu_write_embedded(rwa->os, drrwe->drr_object,
	    drrwe->drr_offset, data, drrwe->drr_etype,
	    drrwe->drr_compression, drrwe->drr_lsize, drrwe->drr_psize,
	    rwa->byteswap ^ ZFS_HOST_BYTEORDER, tx);

	/* See comment in restore_write. */
	save_resume_state(rwa, drrwe->drr_object, drrwe->drr_offset, tx);
	dmu_tx_commit(tx);
	return (0);
}

static int
receive_spill(struct receive_writer_arg *rwa, struct drr_spill *drrs,
    arc_buf_t *abuf)
{
	dmu_tx_t *tx;
	dmu_buf_t *db, *db_spill;
	int err;

	if (drrs->drr_length < SPA_MINBLOCKSIZE ||
	    drrs->drr_length > spa_maxblocksize(dmu_objset_spa(rwa->os)))
		return (SET_ERROR(EINVAL));

	/*
	 * This is an unmodified spill block which was added to the stream
	 * to resolve an issue with incorrectly removing spill blocks.  It
	 * should be ignored by current versions of the code which support
	 * the DRR_FLAG_SPILL_BLOCK flag.
	 */
	if (rwa->spill && DRR_SPILL_IS_UNMODIFIED(drrs->drr_flags)) {
		dmu_return_arcbuf(abuf);
		return (0);
	}

	if (rwa->raw) {
		if (!DMU_OT_IS_VALID(drrs->drr_type) ||
		    drrs->drr_compressiontype >= ZIO_COMPRESS_FUNCTIONS ||
		    drrs->drr_compressed_size == 0)
			return (SET_ERROR(EINVAL));
	}

	if (dmu_object_info(rwa->os, drrs->drr_object, NULL) != 0)
		return (SET_ERROR(EINVAL));

	if (drrs->drr_object > rwa->max_object)
		rwa->max_object = drrs->drr_object;

	VERIFY0(dmu_bonus_hold(rwa->os, drrs->drr_object, FTAG, &db));
	if ((err = dmu_spill_hold_by_bonus(db, DMU_READ_NO_DECRYPT, FTAG,
	    &db_spill)) != 0) {
		dmu_buf_rele(db, FTAG);
		return (err);
	}

	tx = dmu_tx_create(rwa->os);

	dmu_tx_hold_spill(tx, db->db_object);

	err = dmu_tx_assign(tx, TXG_WAIT);
	if (err != 0) {
		dmu_buf_rele(db, FTAG);
		dmu_buf_rele(db_spill, FTAG);
		dmu_tx_abort(tx);
		return (err);
	}

	/*
	 * Spill blocks may both grow and shrink.  When a change in size
	 * occurs any existing dbuf must be updated to match the logical
	 * size of the provided arc_buf_t.
	 */
	if (db_spill->db_size != drrs->drr_length) {
		dmu_buf_will_fill(db_spill, tx);
		VERIFY(0 == dbuf_spill_set_blksz(db_spill,
		    drrs->drr_length, tx));
	}

	if (rwa->byteswap && !arc_is_encrypted(abuf) &&
	    arc_get_compression(abuf) == ZIO_COMPRESS_OFF) {
		dmu_object_byteswap_t byteswap =
		    DMU_OT_BYTESWAP(drrs->drr_type);
		dmu_ot_byteswap[byteswap].ob_func(abuf->b_data,
		    DRR_SPILL_PAYLOAD_SIZE(drrs));
	}

	dbuf_assign_arcbuf((dmu_buf_impl_t *)db_spill, abuf, tx);

	dmu_buf_rele(db, FTAG);
	dmu_buf_rele(db_spill, FTAG);

	dmu_tx_commit(tx);
	return (0);
}

/* ARGSUSED */
noinline static int
receive_free(struct receive_writer_arg *rwa, struct drr_free *drrf)
{
	int err;

	if (drrf->drr_length != -1ULL &&
	    drrf->drr_offset + drrf->drr_length < drrf->drr_offset)
		return (SET_ERROR(EINVAL));

	if (dmu_object_info(rwa->os, drrf->drr_object, NULL) != 0)
		return (SET_ERROR(EINVAL));

	if (drrf->drr_object > rwa->max_object)
		rwa->max_object = drrf->drr_object;

	err = dmu_free_long_range(rwa->os, drrf->drr_object,
	    drrf->drr_offset, drrf->drr_length);

	return (err);
}

static int
receive_object_range(struct receive_writer_arg *rwa,
    struct drr_object_range *drror)
{
	/*
	 * By default, we assume this block is in our native format
	 * (ZFS_HOST_BYTEORDER). We then take into account whether
	 * the send stream is byteswapped (rwa->byteswap). Finally,
	 * we need to byteswap again if this particular block was
	 * in non-native format on the send side.
	 */
	boolean_t byteorder = ZFS_HOST_BYTEORDER ^ rwa->byteswap ^
	    !!DRR_IS_RAW_BYTESWAPPED(drror->drr_flags);

	/*
	 * Since dnode block sizes are constant, we should not need to worry
	 * about making sure that the dnode block size is the same on the
	 * sending and receiving sides for the time being. For non-raw sends,
	 * this does not matter (and in fact we do not send a DRR_OBJECT_RANGE
	 * record at all). Raw sends require this record type because the
	 * encryption parameters are used to protect an entire block of bonus
	 * buffers. If the size of dnode blocks ever becomes variable,
	 * handling will need to be added to ensure that dnode block sizes
	 * match on the sending and receiving side.
	 */
	if (drror->drr_numslots != DNODES_PER_BLOCK ||
	    P2PHASE(drror->drr_firstobj, DNODES_PER_BLOCK) != 0 ||
	    !rwa->raw)
		return (SET_ERROR(EINVAL));

	if (drror->drr_firstobj > rwa->max_object)
		rwa->max_object = drror->drr_firstobj;

	/*
	 * The DRR_OBJECT_RANGE handling must be deferred to receive_object()
	 * so that the block of dnodes is not written out when it's empty,
	 * and converted to a HOLE BP.
	 */
	rwa->or_crypt_params_present = B_TRUE;
	rwa->or_firstobj = drror->drr_firstobj;
	rwa->or_numslots = drror->drr_numslots;
	bcopy(drror->drr_salt, rwa->or_salt, ZIO_DATA_SALT_LEN);
	bcopy(drror->drr_iv, rwa->or_iv, ZIO_DATA_IV_LEN);
	bcopy(drror->drr_mac, rwa->or_mac, ZIO_DATA_MAC_LEN);
	rwa->or_byteorder = byteorder;

	return (0);
}

/*
 * Until we have the ability to redact large ranges of data efficiently, we
 * process these records as frees.
 */
/* ARGSUSED */
noinline static int
receive_redact(struct receive_writer_arg *rwa, struct drr_redact *drrr)
{
	struct drr_free drrf = {0};
	drrf.drr_length = drrr->drr_length;
	drrf.drr_object = drrr->drr_object;
	drrf.drr_offset = drrr->drr_offset;
	drrf.drr_toguid = drrr->drr_toguid;
	return (receive_free(rwa, &drrf));
}

/* used to destroy the drc_ds on error */
static void
dmu_recv_cleanup_ds(dmu_recv_cookie_t *drc)
{
	dsl_dataset_t *ds = drc->drc_ds;
	ds_hold_flags_t dsflags = (drc->drc_raw) ? 0 : DS_HOLD_FLAG_DECRYPT;

	/*
	 * Wait for the txg sync before cleaning up the receive. For
	 * resumable receives, this ensures that our resume state has
	 * been written out to disk. For raw receives, this ensures
	 * that the user accounting code will not attempt to do anything
	 * after we stopped receiving the dataset.
	 */
	txg_wait_synced(ds->ds_dir->dd_pool, 0);
	ds->ds_objset->os_raw_receive = B_FALSE;

	rrw_enter(&ds->ds_bp_rwlock, RW_READER, FTAG);
	if (drc->drc_resumable && !BP_IS_HOLE(dsl_dataset_get_blkptr(ds))) {
		rrw_exit(&ds->ds_bp_rwlock, FTAG);
		dsl_dataset_disown(ds, dsflags, dmu_recv_tag);
	} else {
		char name[ZFS_MAX_DATASET_NAME_LEN];
		rrw_exit(&ds->ds_bp_rwlock, FTAG);
		dsl_dataset_name(ds, name);
		dsl_dataset_disown(ds, dsflags, dmu_recv_tag);
		(void) dsl_destroy_head(name);
	}
}

static void
receive_cksum(dmu_recv_cookie_t *drc, int len, void *buf)
{
	if (drc->drc_byteswap) {
		(void) fletcher_4_incremental_byteswap(buf, len,
		    &drc->drc_cksum);
	} else {
		(void) fletcher_4_incremental_native(buf, len, &drc->drc_cksum);
	}
}

/*
 * Read the payload into a buffer of size len, and update the current record's
 * payload field.
 * Allocate drc->drc_next_rrd and read the next record's header into
 * drc->drc_next_rrd->header.
 * Verify checksum of payload and next record.
 */
static int
receive_read_payload_and_next_header(dmu_recv_cookie_t *drc, int len, void *buf)
{
	int err;

	if (len != 0) {
		ASSERT3U(len, <=, SPA_MAXBLOCKSIZE);
		err = receive_read(drc, len, buf);
		if (err != 0)
			return (err);
		receive_cksum(drc, len, buf);

		/* note: rrd is NULL when reading the begin record's payload */
		if (drc->drc_rrd != NULL) {
			drc->drc_rrd->payload = buf;
			drc->drc_rrd->payload_size = len;
			drc->drc_rrd->bytes_read = drc->drc_bytes_read;
		}
	} else {
		ASSERT3P(buf, ==, NULL);
	}

	drc->drc_prev_cksum = drc->drc_cksum;

	drc->drc_next_rrd = kmem_zalloc(sizeof (*drc->drc_next_rrd), KM_SLEEP);
	err = receive_read(drc, sizeof (drc->drc_next_rrd->header),
	    &drc->drc_next_rrd->header);
	drc->drc_next_rrd->bytes_read = drc->drc_bytes_read;

	if (err != 0) {
		kmem_free(drc->drc_next_rrd, sizeof (*drc->drc_next_rrd));
		drc->drc_next_rrd = NULL;
		return (err);
	}
	if (drc->drc_next_rrd->header.drr_type == DRR_BEGIN) {
		kmem_free(drc->drc_next_rrd, sizeof (*drc->drc_next_rrd));
		drc->drc_next_rrd = NULL;
		return (SET_ERROR(EINVAL));
	}

	/*
	 * Note: checksum is of everything up to but not including the
	 * checksum itself.
	 */
	ASSERT3U(offsetof(dmu_replay_record_t, drr_u.drr_checksum.drr_checksum),
	    ==, sizeof (dmu_replay_record_t) - sizeof (zio_cksum_t));
	receive_cksum(drc,
	    offsetof(dmu_replay_record_t, drr_u.drr_checksum.drr_checksum),
	    &drc->drc_next_rrd->header);

	zio_cksum_t cksum_orig =
	    drc->drc_next_rrd->header.drr_u.drr_checksum.drr_checksum;
	zio_cksum_t *cksump =
	    &drc->drc_next_rrd->header.drr_u.drr_checksum.drr_checksum;

	if (drc->drc_byteswap)
		byteswap_record(&drc->drc_next_rrd->header);

	if ((!ZIO_CHECKSUM_IS_ZERO(cksump)) &&
	    !ZIO_CHECKSUM_EQUAL(drc->drc_cksum, *cksump)) {
		kmem_free(drc->drc_next_rrd, sizeof (*drc->drc_next_rrd));
		drc->drc_next_rrd = NULL;
		return (SET_ERROR(ECKSUM));
	}

	receive_cksum(drc, sizeof (cksum_orig), &cksum_orig);

	return (0);
}

/*
 * Issue the prefetch reads for any necessary indirect blocks.
 *
 * We use the object ignore list to tell us whether or not to issue prefetches
 * for a given object.  We do this for both correctness (in case the blocksize
 * of an object has changed) and performance (if the object doesn't exist, don't
 * needlessly try to issue prefetches).  We also trim the list as we go through
 * the stream to prevent it from growing to an unbounded size.
 *
 * The object numbers within will always be in sorted order, and any write
 * records we see will also be in sorted order, but they're not sorted with
 * respect to each other (i.e. we can get several object records before
 * receiving each object's write records).  As a result, once we've reached a
 * given object number, we can safely remove any reference to lower object
 * numbers in the ignore list. In practice, we receive up to 32 object records
 * before receiving write records, so the list can have up to 32 nodes in it.
 */
/* ARGSUSED */
static void
receive_read_prefetch(dmu_recv_cookie_t *drc, uint64_t object, uint64_t offset,
    uint64_t length)
{
	if (!objlist_exists(drc->drc_ignore_objlist, object)) {
		dmu_prefetch(drc->drc_os, object, 1, offset, length,
		    ZIO_PRIORITY_SYNC_READ);
	}
}

/*
 * Read records off the stream, issuing any necessary prefetches.
 */
static int
receive_read_record(dmu_recv_cookie_t *drc)
{
	int err;

	switch (drc->drc_rrd->header.drr_type) {
	case DRR_OBJECT:
	{
		struct drr_object *drro =
		    &drc->drc_rrd->header.drr_u.drr_object;
		uint32_t size = DRR_OBJECT_PAYLOAD_SIZE(drro);
		void *buf = NULL;
		dmu_object_info_t doi;

		if (size != 0)
			buf = kmem_zalloc(size, KM_SLEEP);

		err = receive_read_payload_and_next_header(drc, size, buf);
		if (err != 0) {
			kmem_free(buf, size);
			return (err);
		}
		err = dmu_object_info(drc->drc_os, drro->drr_object, &doi);
		/*
		 * See receive_read_prefetch for an explanation why we're
		 * storing this object in the ignore_obj_list.
		 */
		if (err == ENOENT || err == EEXIST ||
		    (err == 0 && doi.doi_data_block_size != drro->drr_blksz)) {
			objlist_insert(drc->drc_ignore_objlist,
			    drro->drr_object);
			err = 0;
		}
		return (err);
	}
	case DRR_FREEOBJECTS:
	{
		err = receive_read_payload_and_next_header(drc, 0, NULL);
		return (err);
	}
	case DRR_WRITE:
	{
		struct drr_write *drrw = &drc->drc_rrd->header.drr_u.drr_write;
		arc_buf_t *abuf;
		boolean_t is_meta = DMU_OT_IS_METADATA(drrw->drr_type);

		if (drc->drc_raw) {
			boolean_t byteorder = ZFS_HOST_BYTEORDER ^
			    !!DRR_IS_RAW_BYTESWAPPED(drrw->drr_flags) ^
			    drc->drc_byteswap;

			abuf = arc_loan_raw_buf(dmu_objset_spa(drc->drc_os),
			    drrw->drr_object, byteorder, drrw->drr_salt,
			    drrw->drr_iv, drrw->drr_mac, drrw->drr_type,
			    drrw->drr_compressed_size, drrw->drr_logical_size,
			    drrw->drr_compressiontype, 0);
		} else if (DRR_WRITE_COMPRESSED(drrw)) {
			ASSERT3U(drrw->drr_compressed_size, >, 0);
			ASSERT3U(drrw->drr_logical_size, >=,
			    drrw->drr_compressed_size);
			ASSERT(!is_meta);
			abuf = arc_loan_compressed_buf(
			    dmu_objset_spa(drc->drc_os),
			    drrw->drr_compressed_size, drrw->drr_logical_size,
			    drrw->drr_compressiontype, 0);
		} else {
			abuf = arc_loan_buf(dmu_objset_spa(drc->drc_os),
			    is_meta, drrw->drr_logical_size);
		}

		err = receive_read_payload_and_next_header(drc,
		    DRR_WRITE_PAYLOAD_SIZE(drrw), abuf->b_data);
		if (err != 0) {
			dmu_return_arcbuf(abuf);
			return (err);
		}
		drc->drc_rrd->arc_buf = abuf;
		receive_read_prefetch(drc, drrw->drr_object, drrw->drr_offset,
		    drrw->drr_logical_size);
		return (err);
	}
	case DRR_WRITE_BYREF:
	{
		struct drr_write_byref *drrwb =
		    &drc->drc_rrd->header.drr_u.drr_write_byref;
		err = receive_read_payload_and_next_header(drc, 0, NULL);
		receive_read_prefetch(drc, drrwb->drr_object, drrwb->drr_offset,
		    drrwb->drr_length);
		return (err);
	}
	case DRR_WRITE_EMBEDDED:
	{
		struct drr_write_embedded *drrwe =
		    &drc->drc_rrd->header.drr_u.drr_write_embedded;
		uint32_t size = P2ROUNDUP(drrwe->drr_psize, 8);
		void *buf = kmem_zalloc(size, KM_SLEEP);

		err = receive_read_payload_and_next_header(drc, size, buf);
		if (err != 0) {
			kmem_free(buf, size);
			return (err);
		}

		receive_read_prefetch(drc, drrwe->drr_object, drrwe->drr_offset,
		    drrwe->drr_length);
		return (err);
	}
	case DRR_FREE:
	case DRR_REDACT:
	{
		/*
		 * It might be beneficial to prefetch indirect blocks here, but
		 * we don't really have the data to decide for sure.
		 */
		err = receive_read_payload_and_next_header(drc, 0, NULL);
		return (err);
	}
	case DRR_END:
	{
		struct drr_end *drre = &drc->drc_rrd->header.drr_u.drr_end;
		if (!ZIO_CHECKSUM_EQUAL(drc->drc_prev_cksum,
		    drre->drr_checksum))
			return (SET_ERROR(ECKSUM));
		return (0);
	}
	case DRR_SPILL:
	{
		struct drr_spill *drrs = &drc->drc_rrd->header.drr_u.drr_spill;
		arc_buf_t *abuf;
		/* DRR_SPILL records are either raw or uncompressed */
		if (drc->drc_raw) {
			boolean_t byteorder = ZFS_HOST_BYTEORDER ^
			    !!DRR_IS_RAW_BYTESWAPPED(drrs->drr_flags) ^
			    drc->drc_byteswap;

			abuf = arc_loan_raw_buf(dmu_objset_spa(drc->drc_os),
			    drrs->drr_object, byteorder, drrs->drr_salt,
			    drrs->drr_iv, drrs->drr_mac, drrs->drr_type,
			    drrs->drr_compressed_size, drrs->drr_length,
			    drrs->drr_compressiontype, 0);
		} else {
			abuf = arc_loan_buf(dmu_objset_spa(drc->drc_os),
			    DMU_OT_IS_METADATA(drrs->drr_type),
			    drrs->drr_length);
		}
		err = receive_read_payload_and_next_header(drc,
		    DRR_SPILL_PAYLOAD_SIZE(drrs), abuf->b_data);
		if (err != 0)
			dmu_return_arcbuf(abuf);
		else
			drc->drc_rrd->arc_buf = abuf;
		return (err);
	}
	case DRR_OBJECT_RANGE:
	{
		err = receive_read_payload_and_next_header(drc, 0, NULL);
		return (err);

	}
	default:
		return (SET_ERROR(EINVAL));
	}
}



static void
dprintf_drr(struct receive_record_arg *rrd, int err)
{
#ifdef ZFS_DEBUG
	switch (rrd->header.drr_type) {
	case DRR_OBJECT:
	{
		struct drr_object *drro = &rrd->header.drr_u.drr_object;
		dprintf("drr_type = OBJECT obj = %llu type = %u "
		    "bonustype = %u blksz = %u bonuslen = %u cksumtype = %u "
		    "compress = %u dn_slots = %u err = %d\n",
		    drro->drr_object, drro->drr_type,  drro->drr_bonustype,
		    drro->drr_blksz, drro->drr_bonuslen,
		    drro->drr_checksumtype, drro->drr_compress,
		    drro->drr_dn_slots, err);
		break;
	}
	case DRR_FREEOBJECTS:
	{
		struct drr_freeobjects *drrfo =
		    &rrd->header.drr_u.drr_freeobjects;
		dprintf("drr_type = FREEOBJECTS firstobj = %llu "
		    "numobjs = %llu err = %d\n",
		    drrfo->drr_firstobj, drrfo->drr_numobjs, err);
		break;
	}
	case DRR_WRITE:
	{
		struct drr_write *drrw = &rrd->header.drr_u.drr_write;
		dprintf("drr_type = WRITE obj = %llu type = %u offset = %llu "
		    "lsize = %llu cksumtype = %u flags = %u "
		    "compress = %u psize = %llu err = %d\n",
		    drrw->drr_object, drrw->drr_type, drrw->drr_offset,
		    drrw->drr_logical_size, drrw->drr_checksumtype,
		    drrw->drr_flags, drrw->drr_compressiontype,
		    drrw->drr_compressed_size, err);
		break;
	}
	case DRR_WRITE_BYREF:
	{
		struct drr_write_byref *drrwbr =
		    &rrd->header.drr_u.drr_write_byref;
		dprintf("drr_type = WRITE_BYREF obj = %llu offset = %llu "
		    "length = %llu toguid = %llx refguid = %llx "
		    "refobject = %llu refoffset = %llu cksumtype = %u "
		    "flags = %u err = %d\n",
		    drrwbr->drr_object, drrwbr->drr_offset,
		    drrwbr->drr_length, drrwbr->drr_toguid,
		    drrwbr->drr_refguid, drrwbr->drr_refobject,
		    drrwbr->drr_refoffset, drrwbr->drr_checksumtype,
		    drrwbr->drr_flags, err);
		break;
	}
	case DRR_WRITE_EMBEDDED:
	{
		struct drr_write_embedded *drrwe =
		    &rrd->header.drr_u.drr_write_embedded;
		dprintf("drr_type = WRITE_EMBEDDED obj = %llu offset = %llu "
		    "length = %llu compress = %u etype = %u lsize = %u "
		    "psize = %u err = %d\n",
		    drrwe->drr_object, drrwe->drr_offset, drrwe->drr_length,
		    drrwe->drr_compression, drrwe->drr_etype,
		    drrwe->drr_lsize, drrwe->drr_psize, err);
		break;
	}
	case DRR_FREE:
	{
		struct drr_free *drrf = &rrd->header.drr_u.drr_free;
		dprintf("drr_type = FREE obj = %llu offset = %llu "
		    "length = %lld err = %d\n",
		    drrf->drr_object, drrf->drr_offset, drrf->drr_length,
		    err);
		break;
	}
	case DRR_SPILL:
	{
		struct drr_spill *drrs = &rrd->header.drr_u.drr_spill;
		dprintf("drr_type = SPILL obj = %llu length = %llu "
		    "err = %d\n", drrs->drr_object, drrs->drr_length, err);
		break;
	}
	case DRR_OBJECT_RANGE:
	{
		struct drr_object_range *drror =
		    &rrd->header.drr_u.drr_object_range;
		dprintf("drr_type = OBJECT_RANGE firstobj = %llu "
		    "numslots = %llu flags = %u err = %d\n",
		    drror->drr_firstobj, drror->drr_numslots,
		    drror->drr_flags, err);
		break;
	}
	default:
		return;
	}
#endif
}

/*
 * Commit the records to the pool.
 */
static int
receive_process_record(struct receive_writer_arg *rwa,
    struct receive_record_arg *rrd)
{
	int err;

	/* Processing in order, therefore bytes_read should be increasing. */
	ASSERT3U(rrd->bytes_read, >=, rwa->bytes_read);
	rwa->bytes_read = rrd->bytes_read;

	switch (rrd->header.drr_type) {
	case DRR_OBJECT:
	{
		struct drr_object *drro = &rrd->header.drr_u.drr_object;
		err = receive_object(rwa, drro, rrd->payload);
		kmem_free(rrd->payload, rrd->payload_size);
		rrd->payload = NULL;
		break;
	}
	case DRR_FREEOBJECTS:
	{
		struct drr_freeobjects *drrfo =
		    &rrd->header.drr_u.drr_freeobjects;
		err = receive_freeobjects(rwa, drrfo);
		break;
	}
	case DRR_WRITE:
	{
		struct drr_write *drrw = &rrd->header.drr_u.drr_write;
		err = receive_write(rwa, drrw, rrd->arc_buf);
		/* if receive_write() is successful, it consumes the arc_buf */
		if (err != 0)
			dmu_return_arcbuf(rrd->arc_buf);
		rrd->arc_buf = NULL;
		rrd->payload = NULL;
		break;
	}
	case DRR_WRITE_BYREF:
	{
		struct drr_write_byref *drrwbr =
		    &rrd->header.drr_u.drr_write_byref;
		err = receive_write_byref(rwa, drrwbr);
		break;
	}
	case DRR_WRITE_EMBEDDED:
	{
		struct drr_write_embedded *drrwe =
		    &rrd->header.drr_u.drr_write_embedded;
		err = receive_write_embedded(rwa, drrwe, rrd->payload);
		kmem_free(rrd->payload, rrd->payload_size);
		rrd->payload = NULL;
		break;
	}
	case DRR_FREE:
	{
		struct drr_free *drrf = &rrd->header.drr_u.drr_free;
		err = receive_free(rwa, drrf);
		break;
	}
	case DRR_SPILL:
	{
		struct drr_spill *drrs = &rrd->header.drr_u.drr_spill;
		err = receive_spill(rwa, drrs, rrd->arc_buf);
		if (err != 0)
			dmu_return_arcbuf(rrd->arc_buf);
		rrd->arc_buf = NULL;
		rrd->payload = NULL;
		break;
	}
	case DRR_OBJECT_RANGE:
	{
		struct drr_object_range *drror =
		    &rrd->header.drr_u.drr_object_range;
		err = receive_object_range(rwa, drror);
		break;
	}
	case DRR_REDACT:
	{
		struct drr_redact *drrr = &rrd->header.drr_u.drr_redact;
		err = receive_redact(rwa, drrr);
		break;
	}
	default:
		err = (SET_ERROR(EINVAL));
	}

	if (err != 0)
		dprintf_drr(rrd, err);

	return (err);
}

/*
 * dmu_recv_stream's worker thread; pull records off the queue, and then call
 * receive_process_record  When we're done, signal the main thread and exit.
 */
static void
receive_writer_thread(void *arg)
{
	struct receive_writer_arg *rwa = arg;
	struct receive_record_arg *rrd;
	fstrans_cookie_t cookie = spl_fstrans_mark();

	for (rrd = bqueue_dequeue(&rwa->q); !rrd->eos_marker;
	    rrd = bqueue_dequeue(&rwa->q)) {
		/*
		 * If there's an error, the main thread will stop putting things
		 * on the queue, but we need to clear everything in it before we
		 * can exit.
		 */
		if (rwa->err == 0) {
			rwa->err = receive_process_record(rwa, rrd);
		} else if (rrd->arc_buf != NULL) {
			dmu_return_arcbuf(rrd->arc_buf);
			rrd->arc_buf = NULL;
			rrd->payload = NULL;
		} else if (rrd->payload != NULL) {
			kmem_free(rrd->payload, rrd->payload_size);
			rrd->payload = NULL;
		}
		kmem_free(rrd, sizeof (*rrd));
	}
	kmem_free(rrd, sizeof (*rrd));
	mutex_enter(&rwa->mutex);
	rwa->done = B_TRUE;
	cv_signal(&rwa->cv);
	mutex_exit(&rwa->mutex);
	spl_fstrans_unmark(cookie);
	thread_exit();
}

static int
resume_check(dmu_recv_cookie_t *drc, nvlist_t *begin_nvl)
{
	uint64_t val;
	objset_t *mos = dmu_objset_pool(drc->drc_os)->dp_meta_objset;
	uint64_t dsobj = dmu_objset_id(drc->drc_os);
	uint64_t resume_obj, resume_off;

	if (nvlist_lookup_uint64(begin_nvl,
	    "resume_object", &resume_obj) != 0 ||
	    nvlist_lookup_uint64(begin_nvl,
	    "resume_offset", &resume_off) != 0) {
		return (SET_ERROR(EINVAL));
	}
	VERIFY0(zap_lookup(mos, dsobj,
	    DS_FIELD_RESUME_OBJECT, sizeof (val), 1, &val));
	if (resume_obj != val)
		return (SET_ERROR(EINVAL));
	VERIFY0(zap_lookup(mos, dsobj,
	    DS_FIELD_RESUME_OFFSET, sizeof (val), 1, &val));
	if (resume_off != val)
		return (SET_ERROR(EINVAL));

	return (0);
}

/*
 * Read in the stream's records, one by one, and apply them to the pool.  There
 * are two threads involved; the thread that calls this function will spin up a
 * worker thread, read the records off the stream one by one, and issue
 * prefetches for any necessary indirect blocks.  It will then push the records
 * onto an internal blocking queue.  The worker thread will pull the records off
 * the queue, and actually write the data into the DMU.  This way, the worker
 * thread doesn't have to wait for reads to complete, since everything it needs
 * (the indirect blocks) will be prefetched.
 *
 * NB: callers *must* call dmu_recv_end() if this succeeds.
 */
int
dmu_recv_stream(dmu_recv_cookie_t *drc, int cleanup_fd,
    uint64_t *action_handlep, offset_t *voffp)
{
	int err = 0;
	struct receive_writer_arg *rwa = kmem_zalloc(sizeof (*rwa), KM_SLEEP);

	if (dsl_dataset_is_zapified(drc->drc_ds)) {
		uint64_t bytes;
		(void) zap_lookup(drc->drc_ds->ds_dir->dd_pool->dp_meta_objset,
		    drc->drc_ds->ds_object, DS_FIELD_RESUME_BYTES,
		    sizeof (bytes), 1, &bytes);
		drc->drc_bytes_read += bytes;
	}

	drc->drc_ignore_objlist = objlist_create();

	/* these were verified in dmu_recv_begin */
	ASSERT3U(DMU_GET_STREAM_HDRTYPE(drc->drc_drrb->drr_versioninfo), ==,
	    DMU_SUBSTREAM);
	ASSERT3U(drc->drc_drrb->drr_type, <, DMU_OST_NUMTYPES);

	/*
	 * Open the objset we are modifying.
	 */
	VERIFY0(dmu_objset_from_ds(drc->drc_ds, &drc->drc_os));
	ASSERT(dsl_dataset_phys(drc->drc_ds)->ds_flags & DS_FLAG_INCONSISTENT);
	ASSERT0(drc->drc_os->os_encrypted &&
	    (drc->drc_featureflags & DMU_BACKUP_FEATURE_EMBED_DATA));

	/* if this stream is dedup'ed, set up the avl tree for guid mapping */
	if (drc->drc_featureflags & DMU_BACKUP_FEATURE_DEDUP) {
		minor_t minor;

		if (cleanup_fd == -1) {
			err = SET_ERROR(EBADF);
			goto out;
		}
		err = zfs_onexit_fd_hold(cleanup_fd, &minor);
		if (err != 0) {
			cleanup_fd = -1;
			goto out;
		}

		if (*action_handlep == 0) {
			rwa->guid_to_ds_map =
			    kmem_alloc(sizeof (avl_tree_t), KM_SLEEP);
			avl_create(rwa->guid_to_ds_map, guid_compare,
			    sizeof (guid_map_entry_t),
			    offsetof(guid_map_entry_t, avlnode));
			err = zfs_onexit_add_cb(minor,
			    free_guid_map_onexit, rwa->guid_to_ds_map,
			    action_handlep);
			if (err != 0)
				goto out;
		} else {
			err = zfs_onexit_cb_data(minor, *action_handlep,
			    (void **)&rwa->guid_to_ds_map);
			if (err != 0)
				goto out;
		}

		drc->drc_guid_to_ds_map = rwa->guid_to_ds_map;
	}

	/* handle DSL encryption key payload */
	if (drc->drc_featureflags & DMU_BACKUP_FEATURE_RAW) {
		nvlist_t *keynvl = NULL;

		ASSERT(drc->drc_os->os_encrypted);
		ASSERT(drc->drc_raw);

		err = nvlist_lookup_nvlist(drc->drc_begin_nvl, "crypt_keydata",
		    &keynvl);
		if (err != 0)
			goto out;

		/*
		 * If this is a new dataset we set the key immediately.
		 * Otherwise we don't want to change the key until we
		 * are sure the rest of the receive succeeded so we stash
		 * the keynvl away until then.
		 */
		err = dsl_crypto_recv_raw(spa_name(drc->drc_os->os_spa),
		    drc->drc_ds->ds_object, drc->drc_fromsnapobj,
		    drc->drc_drrb->drr_type, keynvl, drc->drc_newfs);
		if (err != 0)
			goto out;

		/* see comment in dmu_recv_end_sync() */
		drc->drc_ivset_guid = 0;
		(void) nvlist_lookup_uint64(keynvl, "to_ivset_guid",
		    &drc->drc_ivset_guid);

		if (!drc->drc_newfs)
			drc->drc_keynvl = fnvlist_dup(keynvl);
	}

	if (drc->drc_featureflags & DMU_BACKUP_FEATURE_RESUMING) {
		err = resume_check(drc, drc->drc_begin_nvl);
		if (err != 0)
			goto out;
	}

	(void) bqueue_init(&rwa->q, zfs_recv_queue_ff,
	    MAX(zfs_recv_queue_length, 2 * zfs_max_recordsize),
	    offsetof(struct receive_record_arg, node));
	cv_init(&rwa->cv, NULL, CV_DEFAULT, NULL);
	mutex_init(&rwa->mutex, NULL, MUTEX_DEFAULT, NULL);
	rwa->os = drc->drc_os;
	rwa->byteswap = drc->drc_byteswap;
	rwa->resumable = drc->drc_resumable;
	rwa->raw = drc->drc_raw;
	rwa->spill = drc->drc_spill;
	rwa->os->os_raw_receive = drc->drc_raw;

	(void) thread_create(NULL, 0, receive_writer_thread, rwa, 0, curproc,
	    TS_RUN, minclsyspri);
	/*
	 * We're reading rwa->err without locks, which is safe since we are the
	 * only reader, and the worker thread is the only writer.  It's ok if we
	 * miss a write for an iteration or two of the loop, since the writer
	 * thread will keep freeing records we send it until we send it an eos
	 * marker.
	 *
	 * We can leave this loop in 3 ways:  First, if rwa->err is
	 * non-zero.  In that case, the writer thread will free the rrd we just
	 * pushed.  Second, if  we're interrupted; in that case, either it's the
	 * first loop and drc->drc_rrd was never allocated, or it's later, and
	 * drc->drc_rrd has been handed off to the writer thread who will free
	 * it.  Finally, if receive_read_record fails or we're at the end of the
	 * stream, then we free drc->drc_rrd and exit.
	 */
	while (rwa->err == 0) {
		if (issig(JUSTLOOKING) && issig(FORREAL)) {
			err = SET_ERROR(EINTR);
			break;
		}

		ASSERT3P(drc->drc_rrd, ==, NULL);
		drc->drc_rrd = drc->drc_next_rrd;
		drc->drc_next_rrd = NULL;
		/* Allocates and loads header into drc->drc_next_rrd */
		err = receive_read_record(drc);

		if (drc->drc_rrd->header.drr_type == DRR_END || err != 0) {
			kmem_free(drc->drc_rrd, sizeof (*drc->drc_rrd));
			drc->drc_rrd = NULL;
			break;
		}

		bqueue_enqueue(&rwa->q, drc->drc_rrd,
		    sizeof (struct receive_record_arg) +
		    drc->drc_rrd->payload_size);
		drc->drc_rrd = NULL;
	}

	ASSERT3P(drc->drc_rrd, ==, NULL);
	drc->drc_rrd = kmem_zalloc(sizeof (*drc->drc_rrd), KM_SLEEP);
	drc->drc_rrd->eos_marker = B_TRUE;
	bqueue_enqueue_flush(&rwa->q, drc->drc_rrd, 1);

	mutex_enter(&rwa->mutex);
	while (!rwa->done) {
		/*
		 * We need to use cv_wait_sig() so that any process that may
		 * be sleeping here can still fork.
		 */
		(void) cv_wait_sig(&rwa->cv, &rwa->mutex);
	}
	mutex_exit(&rwa->mutex);

	/*
	 * If we are receiving a full stream as a clone, all object IDs which
	 * are greater than the maximum ID referenced in the stream are
	 * by definition unused and must be freed.
	 */
	if (drc->drc_clone && drc->drc_drrb->drr_fromguid == 0) {
		uint64_t obj = rwa->max_object + 1;
		int free_err = 0;
		int next_err = 0;

		while (next_err == 0) {
			free_err = dmu_free_long_object(rwa->os, obj);
			if (free_err != 0 && free_err != ENOENT)
				break;

			next_err = dmu_object_next(rwa->os, &obj, FALSE, 0);
		}

		if (err == 0) {
			if (free_err != 0 && free_err != ENOENT)
				err = free_err;
			else if (next_err != ESRCH)
				err = next_err;
		}
	}

	cv_destroy(&rwa->cv);
	mutex_destroy(&rwa->mutex);
	bqueue_destroy(&rwa->q);
	if (err == 0)
		err = rwa->err;

out:
	/*
	 * If we hit an error before we started the receive_writer_thread
	 * we need to clean up the next_rrd we create by processing the
	 * DRR_BEGIN record.
	 */
	if (drc->drc_next_rrd != NULL)
		kmem_free(drc->drc_next_rrd, sizeof (*drc->drc_next_rrd));

	kmem_free(rwa, sizeof (*rwa));
	nvlist_free(drc->drc_begin_nvl);
	if ((drc->drc_featureflags & DMU_BACKUP_FEATURE_DEDUP) &&
	    (cleanup_fd != -1))
		zfs_onexit_fd_rele(cleanup_fd);

	if (err != 0) {
		/*
		 * Clean up references. If receive is not resumable,
		 * destroy what we created, so we don't leave it in
		 * the inconsistent state.
		 */
		dmu_recv_cleanup_ds(drc);
		nvlist_free(drc->drc_keynvl);
	}

	objlist_destroy(drc->drc_ignore_objlist);
	drc->drc_ignore_objlist = NULL;
	*voffp = drc->drc_voff;
	return (err);
}

static int
dmu_recv_end_check(void *arg, dmu_tx_t *tx)
{
	dmu_recv_cookie_t *drc = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	int error;

	ASSERT3P(drc->drc_ds->ds_owner, ==, dmu_recv_tag);

	if (!drc->drc_newfs) {
		dsl_dataset_t *origin_head;

		error = dsl_dataset_hold(dp, drc->drc_tofs, FTAG, &origin_head);
		if (error != 0)
			return (error);
		if (drc->drc_force) {
			/*
			 * We will destroy any snapshots in tofs (i.e. before
			 * origin_head) that are after the origin (which is
			 * the snap before drc_ds, because drc_ds can not
			 * have any snaps of its own).
			 */
			uint64_t obj;

			obj = dsl_dataset_phys(origin_head)->ds_prev_snap_obj;
			while (obj !=
			    dsl_dataset_phys(drc->drc_ds)->ds_prev_snap_obj) {
				dsl_dataset_t *snap;
				error = dsl_dataset_hold_obj(dp, obj, FTAG,
				    &snap);
				if (error != 0)
					break;
				if (snap->ds_dir != origin_head->ds_dir)
					error = SET_ERROR(EINVAL);
				if (error == 0)  {
					error = dsl_destroy_snapshot_check_impl(
					    snap, B_FALSE);
				}
				obj = dsl_dataset_phys(snap)->ds_prev_snap_obj;
				dsl_dataset_rele(snap, FTAG);
				if (error != 0)
					break;
			}
			if (error != 0) {
				dsl_dataset_rele(origin_head, FTAG);
				return (error);
			}
		}
		if (drc->drc_keynvl != NULL) {
			error = dsl_crypto_recv_raw_key_check(drc->drc_ds,
			    drc->drc_keynvl, tx);
			if (error != 0) {
				dsl_dataset_rele(origin_head, FTAG);
				return (error);
			}
		}

		error = dsl_dataset_clone_swap_check_impl(drc->drc_ds,
		    origin_head, drc->drc_force, drc->drc_owner, tx);
		if (error != 0) {
			dsl_dataset_rele(origin_head, FTAG);
			return (error);
		}
		error = dsl_dataset_snapshot_check_impl(origin_head,
		    drc->drc_tosnap, tx, B_TRUE, 1, drc->drc_cred);
		dsl_dataset_rele(origin_head, FTAG);
		if (error != 0)
			return (error);

		error = dsl_destroy_head_check_impl(drc->drc_ds, 1);
	} else {
		error = dsl_dataset_snapshot_check_impl(drc->drc_ds,
		    drc->drc_tosnap, tx, B_TRUE, 1, drc->drc_cred);
	}
	return (error);
}

static void
dmu_recv_end_sync(void *arg, dmu_tx_t *tx)
{
	dmu_recv_cookie_t *drc = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	boolean_t encrypted = drc->drc_ds->ds_dir->dd_crypto_obj != 0;

	spa_history_log_internal_ds(drc->drc_ds, "finish receiving",
	    tx, "snap=%s", drc->drc_tosnap);
	drc->drc_ds->ds_objset->os_raw_receive = B_FALSE;

	if (!drc->drc_newfs) {
		dsl_dataset_t *origin_head;

		VERIFY0(dsl_dataset_hold(dp, drc->drc_tofs, FTAG,
		    &origin_head));

		if (drc->drc_force) {
			/*
			 * Destroy any snapshots of drc_tofs (origin_head)
			 * after the origin (the snap before drc_ds).
			 */
			uint64_t obj;

			obj = dsl_dataset_phys(origin_head)->ds_prev_snap_obj;
			while (obj !=
			    dsl_dataset_phys(drc->drc_ds)->ds_prev_snap_obj) {
				dsl_dataset_t *snap;
				VERIFY0(dsl_dataset_hold_obj(dp, obj, FTAG,
				    &snap));
				ASSERT3P(snap->ds_dir, ==, origin_head->ds_dir);
				obj = dsl_dataset_phys(snap)->ds_prev_snap_obj;
				dsl_destroy_snapshot_sync_impl(snap,
				    B_FALSE, tx);
				dsl_dataset_rele(snap, FTAG);
			}
		}
		if (drc->drc_keynvl != NULL) {
			dsl_crypto_recv_raw_key_sync(drc->drc_ds,
			    drc->drc_keynvl, tx);
			nvlist_free(drc->drc_keynvl);
			drc->drc_keynvl = NULL;
		}

		VERIFY3P(drc->drc_ds->ds_prev, ==,
		    origin_head->ds_prev);

		dsl_dataset_clone_swap_sync_impl(drc->drc_ds,
		    origin_head, tx);
		dsl_dataset_snapshot_sync_impl(origin_head,
		    drc->drc_tosnap, tx);

		/* set snapshot's creation time and guid */
		dmu_buf_will_dirty(origin_head->ds_prev->ds_dbuf, tx);
		dsl_dataset_phys(origin_head->ds_prev)->ds_creation_time =
		    drc->drc_drrb->drr_creation_time;
		dsl_dataset_phys(origin_head->ds_prev)->ds_guid =
		    drc->drc_drrb->drr_toguid;
		dsl_dataset_phys(origin_head->ds_prev)->ds_flags &=
		    ~DS_FLAG_INCONSISTENT;

		dmu_buf_will_dirty(origin_head->ds_dbuf, tx);
		dsl_dataset_phys(origin_head)->ds_flags &=
		    ~DS_FLAG_INCONSISTENT;

		drc->drc_newsnapobj =
		    dsl_dataset_phys(origin_head)->ds_prev_snap_obj;

		dsl_dataset_rele(origin_head, FTAG);
		dsl_destroy_head_sync_impl(drc->drc_ds, tx);

		if (drc->drc_owner != NULL)
			VERIFY3P(origin_head->ds_owner, ==, drc->drc_owner);
	} else {
		dsl_dataset_t *ds = drc->drc_ds;

		dsl_dataset_snapshot_sync_impl(ds, drc->drc_tosnap, tx);

		/* set snapshot's creation time and guid */
		dmu_buf_will_dirty(ds->ds_prev->ds_dbuf, tx);
		dsl_dataset_phys(ds->ds_prev)->ds_creation_time =
		    drc->drc_drrb->drr_creation_time;
		dsl_dataset_phys(ds->ds_prev)->ds_guid =
		    drc->drc_drrb->drr_toguid;
		dsl_dataset_phys(ds->ds_prev)->ds_flags &=
		    ~DS_FLAG_INCONSISTENT;

		dmu_buf_will_dirty(ds->ds_dbuf, tx);
		dsl_dataset_phys(ds)->ds_flags &= ~DS_FLAG_INCONSISTENT;
		if (dsl_dataset_has_resume_receive_state(ds)) {
			(void) zap_remove(dp->dp_meta_objset, ds->ds_object,
			    DS_FIELD_RESUME_FROMGUID, tx);
			(void) zap_remove(dp->dp_meta_objset, ds->ds_object,
			    DS_FIELD_RESUME_OBJECT, tx);
			(void) zap_remove(dp->dp_meta_objset, ds->ds_object,
			    DS_FIELD_RESUME_OFFSET, tx);
			(void) zap_remove(dp->dp_meta_objset, ds->ds_object,
			    DS_FIELD_RESUME_BYTES, tx);
			(void) zap_remove(dp->dp_meta_objset, ds->ds_object,
			    DS_FIELD_RESUME_TOGUID, tx);
			(void) zap_remove(dp->dp_meta_objset, ds->ds_object,
			    DS_FIELD_RESUME_TONAME, tx);
			(void) zap_remove(dp->dp_meta_objset, ds->ds_object,
			    DS_FIELD_RESUME_REDACT_BOOKMARK_SNAPS, tx);
		}
		drc->drc_newsnapobj =
		    dsl_dataset_phys(drc->drc_ds)->ds_prev_snap_obj;
	}

	/*
	 * If this is a raw receive, the crypt_keydata nvlist will include
	 * a to_ivset_guid for us to set on the new snapshot. This value
	 * will override the value generated by the snapshot code. However,
	 * this value may not be present, because older implementations of
	 * the raw send code did not include this value, and we are still
	 * allowed to receive them if the zfs_disable_ivset_guid_check
	 * tunable is set, in which case we will leave the newly-generated
	 * value.
	 */
	if (drc->drc_raw && drc->drc_ivset_guid != 0) {
		dmu_object_zapify(dp->dp_meta_objset, drc->drc_newsnapobj,
		    DMU_OT_DSL_DATASET, tx);
		VERIFY0(zap_update(dp->dp_meta_objset, drc->drc_newsnapobj,
		    DS_FIELD_IVSET_GUID, sizeof (uint64_t), 1,
		    &drc->drc_ivset_guid, tx));
	}

	zvol_create_minors(dp->dp_spa, drc->drc_tofs, B_TRUE);

	/*
	 * Release the hold from dmu_recv_begin.  This must be done before
	 * we return to open context, so that when we free the dataset's dnode
	 * we can evict its bonus buffer. Since the dataset may be destroyed
	 * at this point (and therefore won't have a valid pointer to the spa)
	 * we release the key mapping manually here while we do have a valid
	 * pointer, if it exists.
	 */
	if (!drc->drc_raw && encrypted) {
		(void) spa_keystore_remove_mapping(dmu_tx_pool(tx)->dp_spa,
		    drc->drc_ds->ds_object, drc->drc_ds);
	}
	dsl_dataset_disown(drc->drc_ds, 0, dmu_recv_tag);
	drc->drc_ds = NULL;
}

static int
add_ds_to_guidmap(const char *name, avl_tree_t *guid_map, uint64_t snapobj,
    boolean_t raw)
{
	dsl_pool_t *dp;
	dsl_dataset_t *snapds;
	guid_map_entry_t *gmep;
	objset_t *os;
	ds_hold_flags_t dsflags = (raw) ? 0 : DS_HOLD_FLAG_DECRYPT;
	int err;

	ASSERT(guid_map != NULL);

	err = dsl_pool_hold(name, FTAG, &dp);
	if (err != 0)
		return (err);
	gmep = kmem_alloc(sizeof (*gmep), KM_SLEEP);
	err = dsl_dataset_own_obj(dp, snapobj, dsflags, gmep, &snapds);

	if (err == 0) {
		/*
		 * If this is a deduplicated raw send stream, we need
		 * to make sure that we can still read raw blocks from
		 * earlier datasets in the stream, so we set the
		 * os_raw_receive flag now.
		 */
		if (raw) {
			err = dmu_objset_from_ds(snapds, &os);
			if (err != 0) {
				dsl_dataset_disown(snapds, dsflags, FTAG);
				dsl_pool_rele(dp, FTAG);
				kmem_free(gmep, sizeof (*gmep));
				return (err);
			}
			os->os_raw_receive = B_TRUE;
		}

		gmep->raw = raw;
		gmep->guid = dsl_dataset_phys(snapds)->ds_guid;
		gmep->gme_ds = snapds;
		avl_add(guid_map, gmep);
	} else {
		kmem_free(gmep, sizeof (*gmep));
	}

	dsl_pool_rele(dp, FTAG);
	return (err);
}

static int dmu_recv_end_modified_blocks = 3;

static int
dmu_recv_existing_end(dmu_recv_cookie_t *drc)
{
#ifdef _KERNEL
	/*
	 * We will be destroying the ds; make sure its origin is unmounted if
	 * necessary.
	 */
	char name[ZFS_MAX_DATASET_NAME_LEN];
	dsl_dataset_name(drc->drc_ds, name);
	zfs_destroy_unmount_origin(name);
#endif

	return (dsl_sync_task(drc->drc_tofs,
	    dmu_recv_end_check, dmu_recv_end_sync, drc,
	    dmu_recv_end_modified_blocks, ZFS_SPACE_CHECK_NORMAL));
}

static int
dmu_recv_new_end(dmu_recv_cookie_t *drc)
{
	return (dsl_sync_task(drc->drc_tofs,
	    dmu_recv_end_check, dmu_recv_end_sync, drc,
	    dmu_recv_end_modified_blocks, ZFS_SPACE_CHECK_NORMAL));
}

int
dmu_recv_end(dmu_recv_cookie_t *drc, void *owner)
{
	int error;

	drc->drc_owner = owner;

	if (drc->drc_newfs)
		error = dmu_recv_new_end(drc);
	else
		error = dmu_recv_existing_end(drc);

	if (error != 0) {
		dmu_recv_cleanup_ds(drc);
		nvlist_free(drc->drc_keynvl);
	} else if (drc->drc_guid_to_ds_map != NULL) {
		(void) add_ds_to_guidmap(drc->drc_tofs, drc->drc_guid_to_ds_map,
		    drc->drc_newsnapobj, drc->drc_raw);
	}
	return (error);
}

/*
 * Return TRUE if this objset is currently being received into.
 */
boolean_t
dmu_objset_is_receiving(objset_t *os)
{
	return (os->os_dsl_dataset != NULL &&
	    os->os_dsl_dataset->ds_owner == dmu_recv_tag);
}

/* BEGIN CSTYLED */
ZFS_MODULE_PARAM(zfs_recv, zfs_recv_, queue_length, INT, ZMOD_RW,
	"Maximum receive queue length");

ZFS_MODULE_PARAM(zfs_recv, zfs_recv_, queue_ff, INT, ZMOD_RW,
	"Receive queue fill fraction");
/* END CSTYLED */
