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
 * Copyright (c) 2011, 2015 by Delphix. All rights reserved.
 * Copyright (c) 2014, Joyent, Inc. All rights reserved.
 * Copyright 2014 HybridCluster. All rights reserved.
 * Copyright 2016 RackTop Systems.
 * Copyright (c) 2016 Actifio, Inc. All rights reserved.
 */

#include <sys/dmu.h>
#include <sys/dmu_impl.h>
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
#include <sys/spa_impl.h>
#include <sys/zfs_ioctl.h>
#include <sys/zap.h>
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
#include <sys/zvol.h>
#include <sys/policy.h>

/* Set this tunable to TRUE to replace corrupt data with 0x2f5baddb10c */
int zfs_send_corrupt_data = B_FALSE;
int zfs_send_queue_length = SPA_MAXBLOCKSIZE;
/* Set this tunable to FALSE to disable setting of DRR_FLAG_FREERECORDS */
int zfs_send_set_freerecords_bit = B_TRUE;
/* Set this tunable to FALSE is disable sending unmodified spill blocks. */
int zfs_send_unmodified_spill_blocks = B_TRUE;

/*
 * Use this to override the recordsize calculation for fast zfs send estimates.
 */
unsigned long zfs_override_estimate_recordsize = 0;

#define	BP_SPAN(datablkszsec, indblkshift, level) \
	(((uint64_t)datablkszsec) << (SPA_MINBLOCKSHIFT + \
	(level) * (indblkshift - SPA_BLKPTRSHIFT)))

struct send_thread_arg {
	bqueue_t	q;
	dsl_dataset_t	*ds;		/* Dataset to traverse */
	uint64_t	fromtxg;	/* Traverse from this txg */
	int		flags;		/* flags to pass to traverse_dataset */
	int		error_code;
	boolean_t	cancel;
	zbookmark_phys_t resume;
};

struct send_block_record {
	boolean_t		eos_marker; /* Marks the end of the stream */
	blkptr_t		bp;
	zbookmark_phys_t	zb;
	uint8_t			indblkshift;
	uint16_t		datablkszsec;
	bqueue_node_t		ln;
};

typedef struct dump_bytes_io {
	dmu_sendarg_t	*dbi_dsp;
	void		*dbi_buf;
	int		dbi_len;
} dump_bytes_io_t;

static int do_dump(dmu_sendarg_t *dsa, struct send_block_record *data);

static void
dump_bytes_cb(void *arg)
{
	dump_bytes_io_t *dbi = (dump_bytes_io_t *)arg;
	dmu_sendarg_t *dsp = dbi->dbi_dsp;
	dsl_dataset_t *ds = dmu_objset_ds(dsp->dsa_os);
	ssize_t resid; /* have to get resid to get detailed errno */

	/*
	 * The code does not rely on len being a multiple of 8.  We keep
	 * this assertion because of the corresponding assertion in
	 * receive_read().  Keeping this assertion ensures that we do not
	 * inadvertently break backwards compatibility (causing the assertion
	 * in receive_read() to trigger on old software). Newer feature flags
	 * (such as raw send) may break this assertion since they were
	 * introduced after the requirement was made obsolete.
	 */

	ASSERT(dbi->dbi_len % 8 == 0 ||
	    (dsp->dsa_featureflags & DMU_BACKUP_FEATURE_RAW) != 0);

	dsp->dsa_err = vn_rdwr(UIO_WRITE, dsp->dsa_vp,
	    (caddr_t)dbi->dbi_buf, dbi->dbi_len,
	    0, UIO_SYSSPACE, FAPPEND, RLIM64_INFINITY, CRED(), &resid);

	mutex_enter(&ds->ds_sendstream_lock);
	*dsp->dsa_off += dbi->dbi_len;
	mutex_exit(&ds->ds_sendstream_lock);
}

static int
dump_bytes(dmu_sendarg_t *dsp, void *buf, int len)
{
	dump_bytes_io_t dbi;

	dbi.dbi_dsp = dsp;
	dbi.dbi_buf = buf;
	dbi.dbi_len = len;

#if defined(HAVE_LARGE_STACKS)
	dump_bytes_cb(&dbi);
#else
	/*
	 * The vn_rdwr() call is performed in a taskq to ensure that there is
	 * always enough stack space to write safely to the target filesystem.
	 * The ZIO_TYPE_FREE threads are used because there can be a lot of
	 * them and they are used in vdev_file.c for a similar purpose.
	 */
	spa_taskq_dispatch_sync(dmu_objset_spa(dsp->dsa_os), ZIO_TYPE_FREE,
	    ZIO_TASKQ_ISSUE, dump_bytes_cb, &dbi, TQ_SLEEP);
#endif /* HAVE_LARGE_STACKS */

	return (dsp->dsa_err);
}

/*
 * For all record types except BEGIN, fill in the checksum (overlaid in
 * drr_u.drr_checksum.drr_checksum).  The checksum verifies everything
 * up to the start of the checksum itself.
 */
static int
dump_record(dmu_sendarg_t *dsp, void *payload, int payload_len)
{
	ASSERT3U(offsetof(dmu_replay_record_t, drr_u.drr_checksum.drr_checksum),
	    ==, sizeof (dmu_replay_record_t) - sizeof (zio_cksum_t));
	(void) fletcher_4_incremental_native(dsp->dsa_drr,
	    offsetof(dmu_replay_record_t, drr_u.drr_checksum.drr_checksum),
	    &dsp->dsa_zc);
	if (dsp->dsa_drr->drr_type == DRR_BEGIN) {
		dsp->dsa_sent_begin = B_TRUE;
	} else {
		ASSERT(ZIO_CHECKSUM_IS_ZERO(&dsp->dsa_drr->drr_u.
		    drr_checksum.drr_checksum));
		dsp->dsa_drr->drr_u.drr_checksum.drr_checksum = dsp->dsa_zc;
	}
	if (dsp->dsa_drr->drr_type == DRR_END) {
		dsp->dsa_sent_end = B_TRUE;
	}
	(void) fletcher_4_incremental_native(&dsp->dsa_drr->
	    drr_u.drr_checksum.drr_checksum,
	    sizeof (zio_cksum_t), &dsp->dsa_zc);
	if (dump_bytes(dsp, dsp->dsa_drr, sizeof (dmu_replay_record_t)) != 0)
		return (SET_ERROR(EINTR));
	if (payload_len != 0) {
		(void) fletcher_4_incremental_native(payload, payload_len,
		    &dsp->dsa_zc);
		if (dump_bytes(dsp, payload, payload_len) != 0)
			return (SET_ERROR(EINTR));
	}
	return (0);
}

/*
 * Fill in the drr_free struct, or perform aggregation if the previous record is
 * also a free record, and the two are adjacent.
 *
 * Note that we send free records even for a full send, because we want to be
 * able to receive a full send as a clone, which requires a list of all the free
 * and freeobject records that were generated on the source.
 */
static int
dump_free(dmu_sendarg_t *dsp, uint64_t object, uint64_t offset,
    uint64_t length)
{
	struct drr_free *drrf = &(dsp->dsa_drr->drr_u.drr_free);

	/*
	 * When we receive a free record, dbuf_free_range() assumes
	 * that the receiving system doesn't have any dbufs in the range
	 * being freed.  This is always true because there is a one-record
	 * constraint: we only send one WRITE record for any given
	 * object,offset.  We know that the one-record constraint is
	 * true because we always send data in increasing order by
	 * object,offset.
	 *
	 * If the increasing-order constraint ever changes, we should find
	 * another way to assert that the one-record constraint is still
	 * satisfied.
	 */
	ASSERT(object > dsp->dsa_last_data_object ||
	    (object == dsp->dsa_last_data_object &&
	    offset > dsp->dsa_last_data_offset));

	/*
	 * If there is a pending op, but it's not PENDING_FREE, push it out,
	 * since free block aggregation can only be done for blocks of the
	 * same type (i.e., DRR_FREE records can only be aggregated with
	 * other DRR_FREE records.  DRR_FREEOBJECTS records can only be
	 * aggregated with other DRR_FREEOBJECTS records.
	 */
	if (dsp->dsa_pending_op != PENDING_NONE &&
	    dsp->dsa_pending_op != PENDING_FREE) {
		if (dump_record(dsp, NULL, 0) != 0)
			return (SET_ERROR(EINTR));
		dsp->dsa_pending_op = PENDING_NONE;
	}

	if (dsp->dsa_pending_op == PENDING_FREE) {
		/*
		 * There should never be a PENDING_FREE if length is
		 * DMU_OBJECT_END (because dump_dnode is the only place where
		 * this function is called with a DMU_OBJECT_END, and only after
		 * flushing any pending record).
		 */
		ASSERT(length != DMU_OBJECT_END);
		/*
		 * Check to see whether this free block can be aggregated
		 * with pending one.
		 */
		if (drrf->drr_object == object && drrf->drr_offset +
		    drrf->drr_length == offset) {
			if (offset + length < offset)
				drrf->drr_length = DMU_OBJECT_END;
			else
				drrf->drr_length += length;
			return (0);
		} else {
			/* not a continuation.  Push out pending record */
			if (dump_record(dsp, NULL, 0) != 0)
				return (SET_ERROR(EINTR));
			dsp->dsa_pending_op = PENDING_NONE;
		}
	}
	/* create a FREE record and make it pending */
	bzero(dsp->dsa_drr, sizeof (dmu_replay_record_t));
	dsp->dsa_drr->drr_type = DRR_FREE;
	drrf->drr_object = object;
	drrf->drr_offset = offset;
	if (offset + length < offset)
		drrf->drr_length = DMU_OBJECT_END;
	else
		drrf->drr_length = length;
	drrf->drr_toguid = dsp->dsa_toguid;
	if (length == DMU_OBJECT_END) {
		if (dump_record(dsp, NULL, 0) != 0)
			return (SET_ERROR(EINTR));
	} else {
		dsp->dsa_pending_op = PENDING_FREE;
	}

	return (0);
}

static int
dump_write(dmu_sendarg_t *dsp, dmu_object_type_t type, uint64_t object,
    uint64_t offset, int lsize, int psize, const blkptr_t *bp, void *data)
{
	uint64_t payload_size;
	boolean_t raw = (dsp->dsa_featureflags & DMU_BACKUP_FEATURE_RAW);
	struct drr_write *drrw = &(dsp->dsa_drr->drr_u.drr_write);

	/*
	 * We send data in increasing object, offset order.
	 * See comment in dump_free() for details.
	 */
	ASSERT(object > dsp->dsa_last_data_object ||
	    (object == dsp->dsa_last_data_object &&
	    offset > dsp->dsa_last_data_offset));
	dsp->dsa_last_data_object = object;
	dsp->dsa_last_data_offset = offset + lsize - 1;

	/*
	 * If there is any kind of pending aggregation (currently either
	 * a grouping of free objects or free blocks), push it out to
	 * the stream, since aggregation can't be done across operations
	 * of different types.
	 */
	if (dsp->dsa_pending_op != PENDING_NONE) {
		if (dump_record(dsp, NULL, 0) != 0)
			return (SET_ERROR(EINTR));
		dsp->dsa_pending_op = PENDING_NONE;
	}
	/* write a WRITE record */
	bzero(dsp->dsa_drr, sizeof (dmu_replay_record_t));
	dsp->dsa_drr->drr_type = DRR_WRITE;
	drrw->drr_object = object;
	drrw->drr_type = type;
	drrw->drr_offset = offset;
	drrw->drr_toguid = dsp->dsa_toguid;
	drrw->drr_logical_size = lsize;

	/* only set the compression fields if the buf is compressed or raw */
	if (raw || lsize != psize) {
		ASSERT(!BP_IS_EMBEDDED(bp));
		ASSERT3S(psize, >, 0);

		if (raw) {
			ASSERT(BP_IS_PROTECTED(bp));

			/*
			 * This is a raw protected block so we need to pass
			 * along everything the receiving side will need to
			 * interpret this block, including the byteswap, salt,
			 * IV, and MAC.
			 */
			if (BP_SHOULD_BYTESWAP(bp))
				drrw->drr_flags |= DRR_RAW_BYTESWAP;
			zio_crypt_decode_params_bp(bp, drrw->drr_salt,
			    drrw->drr_iv);
			zio_crypt_decode_mac_bp(bp, drrw->drr_mac);
		} else {
			/* this is a compressed block */
			ASSERT(dsp->dsa_featureflags &
			    DMU_BACKUP_FEATURE_COMPRESSED);
			ASSERT(!BP_SHOULD_BYTESWAP(bp));
			ASSERT(!DMU_OT_IS_METADATA(BP_GET_TYPE(bp)));
			ASSERT3U(BP_GET_COMPRESS(bp), !=, ZIO_COMPRESS_OFF);
			ASSERT3S(lsize, >=, psize);
		}

		/* set fields common to compressed and raw sends */
		drrw->drr_compressiontype = BP_GET_COMPRESS(bp);
		drrw->drr_compressed_size = psize;
		payload_size = drrw->drr_compressed_size;
	} else {
		payload_size = drrw->drr_logical_size;
	}

	if (bp == NULL || BP_IS_EMBEDDED(bp) || (BP_IS_PROTECTED(bp) && !raw)) {
		/*
		 * There's no pre-computed checksum for partial-block writes,
		 * embedded BP's, or encrypted BP's that are being sent as
		 * plaintext, so (like fletcher4-checkummed blocks) userland
		 * will have to compute a dedup-capable checksum itself.
		 */
		drrw->drr_checksumtype = ZIO_CHECKSUM_OFF;
	} else {
		drrw->drr_checksumtype = BP_GET_CHECKSUM(bp);
		if (zio_checksum_table[drrw->drr_checksumtype].ci_flags &
		    ZCHECKSUM_FLAG_DEDUP)
			drrw->drr_flags |= DRR_CHECKSUM_DEDUP;
		DDK_SET_LSIZE(&drrw->drr_key, BP_GET_LSIZE(bp));
		DDK_SET_PSIZE(&drrw->drr_key, BP_GET_PSIZE(bp));
		DDK_SET_COMPRESS(&drrw->drr_key, BP_GET_COMPRESS(bp));
		DDK_SET_CRYPT(&drrw->drr_key, BP_IS_PROTECTED(bp));
		drrw->drr_key.ddk_cksum = bp->blk_cksum;
	}

	if (dump_record(dsp, data, payload_size) != 0)
		return (SET_ERROR(EINTR));
	return (0);
}

static int
dump_write_embedded(dmu_sendarg_t *dsp, uint64_t object, uint64_t offset,
    int blksz, const blkptr_t *bp)
{
	char buf[BPE_PAYLOAD_SIZE];
	struct drr_write_embedded *drrw =
	    &(dsp->dsa_drr->drr_u.drr_write_embedded);

	if (dsp->dsa_pending_op != PENDING_NONE) {
		if (dump_record(dsp, NULL, 0) != 0)
			return (SET_ERROR(EINTR));
		dsp->dsa_pending_op = PENDING_NONE;
	}

	ASSERT(BP_IS_EMBEDDED(bp));

	bzero(dsp->dsa_drr, sizeof (dmu_replay_record_t));
	dsp->dsa_drr->drr_type = DRR_WRITE_EMBEDDED;
	drrw->drr_object = object;
	drrw->drr_offset = offset;
	drrw->drr_length = blksz;
	drrw->drr_toguid = dsp->dsa_toguid;
	drrw->drr_compression = BP_GET_COMPRESS(bp);
	drrw->drr_etype = BPE_GET_ETYPE(bp);
	drrw->drr_lsize = BPE_GET_LSIZE(bp);
	drrw->drr_psize = BPE_GET_PSIZE(bp);

	decode_embedded_bp_compressed(bp, buf);

	if (dump_record(dsp, buf, P2ROUNDUP(drrw->drr_psize, 8)) != 0)
		return (SET_ERROR(EINTR));
	return (0);
}

static int
dump_spill(dmu_sendarg_t *dsp, const blkptr_t *bp, uint64_t object, void *data)
{
	struct drr_spill *drrs = &(dsp->dsa_drr->drr_u.drr_spill);
	uint64_t blksz = BP_GET_LSIZE(bp);
	uint64_t payload_size = blksz;

	if (dsp->dsa_pending_op != PENDING_NONE) {
		if (dump_record(dsp, NULL, 0) != 0)
			return (SET_ERROR(EINTR));
		dsp->dsa_pending_op = PENDING_NONE;
	}

	/* write a SPILL record */
	bzero(dsp->dsa_drr, sizeof (dmu_replay_record_t));
	dsp->dsa_drr->drr_type = DRR_SPILL;
	drrs->drr_object = object;
	drrs->drr_length = blksz;
	drrs->drr_toguid = dsp->dsa_toguid;

	/* See comment in dump_dnode() for full details */
	if (zfs_send_unmodified_spill_blocks &&
	    (bp->blk_birth <= dsp->dsa_fromtxg)) {
		drrs->drr_flags |= DRR_SPILL_UNMODIFIED;
	}

	/* handle raw send fields */
	if (dsp->dsa_featureflags & DMU_BACKUP_FEATURE_RAW) {
		ASSERT(BP_IS_PROTECTED(bp));

		if (BP_SHOULD_BYTESWAP(bp))
			drrs->drr_flags |= DRR_RAW_BYTESWAP;
		drrs->drr_compressiontype = BP_GET_COMPRESS(bp);
		drrs->drr_compressed_size = BP_GET_PSIZE(bp);
		zio_crypt_decode_params_bp(bp, drrs->drr_salt, drrs->drr_iv);
		zio_crypt_decode_mac_bp(bp, drrs->drr_mac);
		payload_size = drrs->drr_compressed_size;
	}

	if (dump_record(dsp, data, payload_size) != 0)
		return (SET_ERROR(EINTR));
	return (0);
}

static int
dump_freeobjects(dmu_sendarg_t *dsp, uint64_t firstobj, uint64_t numobjs)
{
	struct drr_freeobjects *drrfo = &(dsp->dsa_drr->drr_u.drr_freeobjects);
	uint64_t maxobj = DNODES_PER_BLOCK *
	    (DMU_META_DNODE(dsp->dsa_os)->dn_maxblkid + 1);

	/*
	 * ZoL < 0.7 does not handle large FREEOBJECTS records correctly,
	 * leading to zfs recv never completing. to avoid this issue, don't
	 * send FREEOBJECTS records for object IDs which cannot exist on the
	 * receiving side.
	 */
	if (maxobj > 0) {
		if (maxobj < firstobj)
			return (0);

		if (maxobj < firstobj + numobjs)
			numobjs = maxobj - firstobj;
	}

	/*
	 * If there is a pending op, but it's not PENDING_FREEOBJECTS,
	 * push it out, since free block aggregation can only be done for
	 * blocks of the same type (i.e., DRR_FREE records can only be
	 * aggregated with other DRR_FREE records.  DRR_FREEOBJECTS records
	 * can only be aggregated with other DRR_FREEOBJECTS records.
	 */
	if (dsp->dsa_pending_op != PENDING_NONE &&
	    dsp->dsa_pending_op != PENDING_FREEOBJECTS) {
		if (dump_record(dsp, NULL, 0) != 0)
			return (SET_ERROR(EINTR));
		dsp->dsa_pending_op = PENDING_NONE;
	}
	if (dsp->dsa_pending_op == PENDING_FREEOBJECTS) {
		/*
		 * See whether this free object array can be aggregated
		 * with pending one
		 */
		if (drrfo->drr_firstobj + drrfo->drr_numobjs == firstobj) {
			drrfo->drr_numobjs += numobjs;
			return (0);
		} else {
			/* can't be aggregated.  Push out pending record */
			if (dump_record(dsp, NULL, 0) != 0)
				return (SET_ERROR(EINTR));
			dsp->dsa_pending_op = PENDING_NONE;
		}
	}

	/* write a FREEOBJECTS record */
	bzero(dsp->dsa_drr, sizeof (dmu_replay_record_t));
	dsp->dsa_drr->drr_type = DRR_FREEOBJECTS;
	drrfo->drr_firstobj = firstobj;
	drrfo->drr_numobjs = numobjs;
	drrfo->drr_toguid = dsp->dsa_toguid;

	dsp->dsa_pending_op = PENDING_FREEOBJECTS;

	return (0);
}

static int
dump_dnode(dmu_sendarg_t *dsp, const blkptr_t *bp, uint64_t object,
    dnode_phys_t *dnp)
{
	struct drr_object *drro = &(dsp->dsa_drr->drr_u.drr_object);
	int bonuslen;

	if (object < dsp->dsa_resume_object) {
		/*
		 * Note: when resuming, we will visit all the dnodes in
		 * the block of dnodes that we are resuming from.  In
		 * this case it's unnecessary to send the dnodes prior to
		 * the one we are resuming from.  We should be at most one
		 * block's worth of dnodes behind the resume point.
		 */
		ASSERT3U(dsp->dsa_resume_object - object, <,
		    1 << (DNODE_BLOCK_SHIFT - DNODE_SHIFT));
		return (0);
	}

	if (dnp == NULL || dnp->dn_type == DMU_OT_NONE)
		return (dump_freeobjects(dsp, object, 1));

	if (dsp->dsa_pending_op != PENDING_NONE) {
		if (dump_record(dsp, NULL, 0) != 0)
			return (SET_ERROR(EINTR));
		dsp->dsa_pending_op = PENDING_NONE;
	}

	/* write an OBJECT record */
	bzero(dsp->dsa_drr, sizeof (dmu_replay_record_t));
	dsp->dsa_drr->drr_type = DRR_OBJECT;
	drro->drr_object = object;
	drro->drr_type = dnp->dn_type;
	drro->drr_bonustype = dnp->dn_bonustype;
	drro->drr_blksz = dnp->dn_datablkszsec << SPA_MINBLOCKSHIFT;
	drro->drr_bonuslen = dnp->dn_bonuslen;
	drro->drr_dn_slots = dnp->dn_extra_slots + 1;
	drro->drr_checksumtype = dnp->dn_checksum;
	drro->drr_compress = dnp->dn_compress;
	drro->drr_toguid = dsp->dsa_toguid;

	if (!(dsp->dsa_featureflags & DMU_BACKUP_FEATURE_LARGE_BLOCKS) &&
	    drro->drr_blksz > SPA_OLD_MAXBLOCKSIZE)
		drro->drr_blksz = SPA_OLD_MAXBLOCKSIZE;

	bonuslen = P2ROUNDUP(dnp->dn_bonuslen, 8);

	if ((dsp->dsa_featureflags & DMU_BACKUP_FEATURE_RAW)) {
		ASSERT(BP_IS_ENCRYPTED(bp));

		if (BP_SHOULD_BYTESWAP(bp))
			drro->drr_flags |= DRR_RAW_BYTESWAP;

		/* needed for reconstructing dnp on recv side */
		drro->drr_maxblkid = dnp->dn_maxblkid;
		drro->drr_indblkshift = dnp->dn_indblkshift;
		drro->drr_nlevels = dnp->dn_nlevels;
		drro->drr_nblkptr = dnp->dn_nblkptr;

		/*
		 * Since we encrypt the entire bonus area, the (raw) part
		 * beyond the bonuslen is actually nonzero, so we need
		 * to send it.
		 */
		if (bonuslen != 0) {
			drro->drr_raw_bonuslen = DN_MAX_BONUS_LEN(dnp);
			bonuslen = drro->drr_raw_bonuslen;
		}
	}

	/*
	 * DRR_OBJECT_SPILL is set for every dnode which references a
	 * spill block.  This allows the receiving pool to definitively
	 * determine when a spill block should be kept or freed.
	 */
	if (dnp->dn_flags & DNODE_FLAG_SPILL_BLKPTR)
		drro->drr_flags |= DRR_OBJECT_SPILL;

	if (dump_record(dsp, DN_BONUS(dnp), bonuslen) != 0)
		return (SET_ERROR(EINTR));

	/* Free anything past the end of the file. */
	if (dump_free(dsp, object, (dnp->dn_maxblkid + 1) *
	    (dnp->dn_datablkszsec << SPA_MINBLOCKSHIFT), DMU_OBJECT_END) != 0)
		return (SET_ERROR(EINTR));

	/*
	 * Send DRR_SPILL records for unmodified spill blocks.  This is useful
	 * because changing certain attributes of the object (e.g. blocksize)
	 * can cause old versions of ZFS to incorrectly remove a spill block.
	 * Including these records in the stream forces an up to date version
	 * to always be written ensuring they're never lost.  Current versions
	 * of the code which understand the DRR_FLAG_SPILL_BLOCK feature can
	 * ignore these unmodified spill blocks.
	 */
	if (zfs_send_unmodified_spill_blocks &&
	    (dnp->dn_flags & DNODE_FLAG_SPILL_BLKPTR) &&
	    (DN_SPILL_BLKPTR(dnp)->blk_birth <= dsp->dsa_fromtxg)) {
		struct send_block_record record;

		bzero(&record, sizeof (struct send_block_record));
		record.eos_marker = B_FALSE;
		record.bp = *DN_SPILL_BLKPTR(dnp);
		SET_BOOKMARK(&(record.zb), dmu_objset_id(dsp->dsa_os),
		    object, 0, DMU_SPILL_BLKID);

		if (do_dump(dsp, &record) != 0)
			return (SET_ERROR(EINTR));
	}

	if (dsp->dsa_err != 0)
		return (SET_ERROR(EINTR));

	return (0);
}

static int
dump_object_range(dmu_sendarg_t *dsp, const blkptr_t *bp, uint64_t firstobj,
    uint64_t numslots)
{
	struct drr_object_range *drror =
	    &(dsp->dsa_drr->drr_u.drr_object_range);

	/* we only use this record type for raw sends */
	ASSERT(BP_IS_PROTECTED(bp));
	ASSERT(dsp->dsa_featureflags & DMU_BACKUP_FEATURE_RAW);
	ASSERT3U(BP_GET_COMPRESS(bp), ==, ZIO_COMPRESS_OFF);
	ASSERT3U(BP_GET_TYPE(bp), ==, DMU_OT_DNODE);
	ASSERT0(BP_GET_LEVEL(bp));

	if (dsp->dsa_pending_op != PENDING_NONE) {
		if (dump_record(dsp, NULL, 0) != 0)
			return (SET_ERROR(EINTR));
		dsp->dsa_pending_op = PENDING_NONE;
	}

	bzero(dsp->dsa_drr, sizeof (dmu_replay_record_t));
	dsp->dsa_drr->drr_type = DRR_OBJECT_RANGE;
	drror->drr_firstobj = firstobj;
	drror->drr_numslots = numslots;
	drror->drr_toguid = dsp->dsa_toguid;
	if (BP_SHOULD_BYTESWAP(bp))
		drror->drr_flags |= DRR_RAW_BYTESWAP;
	zio_crypt_decode_params_bp(bp, drror->drr_salt, drror->drr_iv);
	zio_crypt_decode_mac_bp(bp, drror->drr_mac);

	if (dump_record(dsp, NULL, 0) != 0)
		return (SET_ERROR(EINTR));
	return (0);
}

static boolean_t
backup_do_embed(dmu_sendarg_t *dsp, const blkptr_t *bp)
{
	if (!BP_IS_EMBEDDED(bp))
		return (B_FALSE);

	/*
	 * Compression function must be legacy, or explicitly enabled.
	 */
	if ((BP_GET_COMPRESS(bp) >= ZIO_COMPRESS_LEGACY_FUNCTIONS &&
	    !(dsp->dsa_featureflags & DMU_BACKUP_FEATURE_LZ4)))
		return (B_FALSE);

	/*
	 * Embed type must be explicitly enabled.
	 */
	switch (BPE_GET_ETYPE(bp)) {
	case BP_EMBEDDED_TYPE_DATA:
		if (dsp->dsa_featureflags & DMU_BACKUP_FEATURE_EMBED_DATA)
			return (B_TRUE);
		break;
	default:
		return (B_FALSE);
	}
	return (B_FALSE);
}

/*
 * This is the callback function to traverse_dataset that acts as the worker
 * thread for dmu_send_impl.
 */
/*ARGSUSED*/
static int
send_cb(spa_t *spa, zilog_t *zilog, const blkptr_t *bp,
    const zbookmark_phys_t *zb, const struct dnode_phys *dnp, void *arg)
{
	struct send_thread_arg *sta = arg;
	struct send_block_record *record;
	uint64_t record_size;
	int err = 0;

	ASSERT(zb->zb_object == DMU_META_DNODE_OBJECT ||
	    zb->zb_object >= sta->resume.zb_object);
	ASSERT3P(sta->ds, !=, NULL);

	if (sta->cancel)
		return (SET_ERROR(EINTR));

	if (bp == NULL) {
		ASSERT3U(zb->zb_level, ==, ZB_DNODE_LEVEL);
		return (0);
	} else if (zb->zb_level < 0) {
		return (0);
	}

	record = kmem_zalloc(sizeof (struct send_block_record), KM_SLEEP);
	record->eos_marker = B_FALSE;
	record->bp = *bp;
	record->zb = *zb;
	record->indblkshift = dnp->dn_indblkshift;
	record->datablkszsec = dnp->dn_datablkszsec;
	record_size = dnp->dn_datablkszsec << SPA_MINBLOCKSHIFT;
	bqueue_enqueue(&sta->q, record, record_size);

	return (err);
}

/*
 * This function kicks off the traverse_dataset.  It also handles setting the
 * error code of the thread in case something goes wrong, and pushes the End of
 * Stream record when the traverse_dataset call has finished.  If there is no
 * dataset to traverse, the thread immediately pushes End of Stream marker.
 */
static void
send_traverse_thread(void *arg)
{
	struct send_thread_arg *st_arg = arg;
	int err;
	struct send_block_record *data;
	fstrans_cookie_t cookie = spl_fstrans_mark();

	if (st_arg->ds != NULL) {
		err = traverse_dataset_resume(st_arg->ds,
		    st_arg->fromtxg, &st_arg->resume,
		    st_arg->flags, send_cb, st_arg);

		if (err != EINTR)
			st_arg->error_code = err;
	}
	data = kmem_zalloc(sizeof (*data), KM_SLEEP);
	data->eos_marker = B_TRUE;
	bqueue_enqueue(&st_arg->q, data, 1);
	spl_fstrans_unmark(cookie);
	thread_exit();
}

/*
 * This function actually handles figuring out what kind of record needs to be
 * dumped, reading the data (which has hopefully been prefetched), and calling
 * the appropriate helper function.
 */
static int
do_dump(dmu_sendarg_t *dsa, struct send_block_record *data)
{
	dsl_dataset_t *ds = dmu_objset_ds(dsa->dsa_os);
	const blkptr_t *bp = &data->bp;
	const zbookmark_phys_t *zb = &data->zb;
	uint8_t indblkshift = data->indblkshift;
	uint16_t dblkszsec = data->datablkszsec;
	spa_t *spa = ds->ds_dir->dd_pool->dp_spa;
	dmu_object_type_t type = bp ? BP_GET_TYPE(bp) : DMU_OT_NONE;
	int err = 0;

	ASSERT3U(zb->zb_level, >=, 0);

	ASSERT(zb->zb_object == DMU_META_DNODE_OBJECT ||
	    zb->zb_object >= dsa->dsa_resume_object);

	/*
	 * All bps of an encrypted os should have the encryption bit set.
	 * If this is not true it indicates tampering and we report an error.
	 */
	if (dsa->dsa_os->os_encrypted &&
	    !BP_IS_HOLE(bp) && !BP_USES_CRYPT(bp)) {
		spa_log_error(spa, zb);
		zfs_panic_recover("unencrypted block in encrypted "
		    "object set %llu", ds->ds_object);
		return (SET_ERROR(EIO));
	}

	if (zb->zb_object != DMU_META_DNODE_OBJECT &&
	    DMU_OBJECT_IS_SPECIAL(zb->zb_object)) {
		return (0);
	} else if (BP_IS_HOLE(bp) &&
	    zb->zb_object == DMU_META_DNODE_OBJECT) {
		uint64_t span = BP_SPAN(dblkszsec, indblkshift, zb->zb_level);
		uint64_t dnobj = (zb->zb_blkid * span) >> DNODE_SHIFT;
		err = dump_freeobjects(dsa, dnobj, span >> DNODE_SHIFT);
	} else if (BP_IS_HOLE(bp)) {
		uint64_t span = BP_SPAN(dblkszsec, indblkshift, zb->zb_level);
		uint64_t offset = zb->zb_blkid * span;
		/* Don't dump free records for offsets > DMU_OBJECT_END */
		if (zb->zb_blkid == 0 || span <= DMU_OBJECT_END / zb->zb_blkid)
			err = dump_free(dsa, zb->zb_object, offset, span);
	} else if (zb->zb_level > 0 || type == DMU_OT_OBJSET) {
		return (0);
	} else if (type == DMU_OT_DNODE) {
		int epb = BP_GET_LSIZE(bp) >> DNODE_SHIFT;
		arc_flags_t aflags = ARC_FLAG_WAIT;
		arc_buf_t *abuf;
		enum zio_flag zioflags = ZIO_FLAG_CANFAIL;

		if (dsa->dsa_featureflags & DMU_BACKUP_FEATURE_RAW) {
			ASSERT(BP_IS_ENCRYPTED(bp));
			ASSERT3U(BP_GET_COMPRESS(bp), ==, ZIO_COMPRESS_OFF);
			zioflags |= ZIO_FLAG_RAW;
		}

		ASSERT0(zb->zb_level);

		if (arc_read(NULL, spa, bp, arc_getbuf_func, &abuf,
		    ZIO_PRIORITY_ASYNC_READ, zioflags, &aflags, zb) != 0)
			return (SET_ERROR(EIO));

		dnode_phys_t *blk = abuf->b_data;
		uint64_t dnobj = zb->zb_blkid * epb;

		/*
		 * Raw sends require sending encryption parameters for the
		 * block of dnodes. Regular sends do not need to send this
		 * info.
		 */
		if (dsa->dsa_featureflags & DMU_BACKUP_FEATURE_RAW) {
			ASSERT(arc_is_encrypted(abuf));
			err = dump_object_range(dsa, bp, dnobj, epb);
		}

		if (err == 0) {
			for (int i = 0; i < epb;
			    i += blk[i].dn_extra_slots + 1) {
				err = dump_dnode(dsa, bp, dnobj + i, blk + i);
				if (err != 0)
					break;
			}
		}
		arc_buf_destroy(abuf, &abuf);
	} else if (type == DMU_OT_SA) {
		arc_flags_t aflags = ARC_FLAG_WAIT;
		arc_buf_t *abuf;
		enum zio_flag zioflags = ZIO_FLAG_CANFAIL;

		if (dsa->dsa_featureflags & DMU_BACKUP_FEATURE_RAW) {
			ASSERT(BP_IS_PROTECTED(bp));
			zioflags |= ZIO_FLAG_RAW;
		}

		if (arc_read(NULL, spa, bp, arc_getbuf_func, &abuf,
		    ZIO_PRIORITY_ASYNC_READ, zioflags, &aflags, zb) != 0)
			return (SET_ERROR(EIO));

		err = dump_spill(dsa, bp, zb->zb_object, abuf->b_data);
		arc_buf_destroy(abuf, &abuf);
	} else if (backup_do_embed(dsa, bp)) {
		/* it's an embedded level-0 block of a regular object */
		int blksz = dblkszsec << SPA_MINBLOCKSHIFT;
		ASSERT0(zb->zb_level);
		err = dump_write_embedded(dsa, zb->zb_object,
		    zb->zb_blkid * blksz, blksz, bp);
	} else {
		/* it's a level-0 block of a regular object */
		arc_flags_t aflags = ARC_FLAG_WAIT;
		arc_buf_t *abuf;
		int blksz = dblkszsec << SPA_MINBLOCKSHIFT;
		uint64_t offset;

		/*
		 * If we have large blocks stored on disk but the send flags
		 * don't allow us to send large blocks, we split the data from
		 * the arc buf into chunks.
		 */
		boolean_t split_large_blocks = blksz > SPA_OLD_MAXBLOCKSIZE &&
		    !(dsa->dsa_featureflags & DMU_BACKUP_FEATURE_LARGE_BLOCKS);

		/*
		 * Raw sends require that we always get raw data as it exists
		 * on disk, so we assert that we are not splitting blocks here.
		 */
		boolean_t request_raw =
		    (dsa->dsa_featureflags & DMU_BACKUP_FEATURE_RAW) != 0;

		/*
		 * We should only request compressed data from the ARC if all
		 * the following are true:
		 *  - stream compression was requested
		 *  - we aren't splitting large blocks into smaller chunks
		 *  - the data won't need to be byteswapped before sending
		 *  - this isn't an embedded block
		 *  - this isn't metadata (if receiving on a different endian
		 *    system it can be byteswapped more easily)
		 */
		boolean_t request_compressed =
		    (dsa->dsa_featureflags & DMU_BACKUP_FEATURE_COMPRESSED) &&
		    !split_large_blocks && !BP_SHOULD_BYTESWAP(bp) &&
		    !BP_IS_EMBEDDED(bp) && !DMU_OT_IS_METADATA(BP_GET_TYPE(bp));

		IMPLY(request_raw, !split_large_blocks);
		IMPLY(request_raw, BP_IS_PROTECTED(bp));
		ASSERT0(zb->zb_level);
		ASSERT(zb->zb_object > dsa->dsa_resume_object ||
		    (zb->zb_object == dsa->dsa_resume_object &&
		    zb->zb_blkid * blksz >= dsa->dsa_resume_offset));

		ASSERT3U(blksz, ==, BP_GET_LSIZE(bp));

		enum zio_flag zioflags = ZIO_FLAG_CANFAIL;
		if (request_raw)
			zioflags |= ZIO_FLAG_RAW;
		else if (request_compressed)
			zioflags |= ZIO_FLAG_RAW_COMPRESS;

		if (arc_read(NULL, spa, bp, arc_getbuf_func, &abuf,
		    ZIO_PRIORITY_ASYNC_READ, zioflags, &aflags, zb) != 0) {
			if (zfs_send_corrupt_data) {
				/* Send a block filled with 0x"zfs badd bloc" */
				abuf = arc_alloc_buf(spa, &abuf, ARC_BUFC_DATA,
				    blksz);
				uint64_t *ptr;
				for (ptr = abuf->b_data;
				    (char *)ptr < (char *)abuf->b_data + blksz;
				    ptr++)
					*ptr = 0x2f5baddb10cULL;
			} else {
				return (SET_ERROR(EIO));
			}
		}

		offset = zb->zb_blkid * blksz;

		if (split_large_blocks) {
			ASSERT0(arc_is_encrypted(abuf));
			ASSERT3U(arc_get_compression(abuf), ==,
			    ZIO_COMPRESS_OFF);
			char *buf = abuf->b_data;
			while (blksz > 0 && err == 0) {
				int n = MIN(blksz, SPA_OLD_MAXBLOCKSIZE);
				err = dump_write(dsa, type, zb->zb_object,
				    offset, n, n, NULL, buf);
				offset += n;
				buf += n;
				blksz -= n;
			}
		} else {
			err = dump_write(dsa, type, zb->zb_object, offset,
			    blksz, arc_buf_size(abuf), bp, abuf->b_data);
		}
		arc_buf_destroy(abuf, &abuf);
	}

	ASSERT(err == 0 || err == EINTR);
	return (err);
}

/*
 * Pop the new data off the queue, and free the old data.
 */
static struct send_block_record *
get_next_record(bqueue_t *bq, struct send_block_record *data)
{
	struct send_block_record *tmp = bqueue_dequeue(bq);
	kmem_free(data, sizeof (*data));
	return (tmp);
}

/*
 * Actually do the bulk of the work in a zfs send.
 *
 * Note: Releases dp using the specified tag.
 */
static int
dmu_send_impl(void *tag, dsl_pool_t *dp, dsl_dataset_t *to_ds,
    zfs_bookmark_phys_t *ancestor_zb, boolean_t is_clone,
    boolean_t embedok, boolean_t large_block_ok, boolean_t compressok,
    boolean_t rawok, int outfd, uint64_t resumeobj, uint64_t resumeoff,
    vnode_t *vp, offset_t *off)
{
	objset_t *os;
	dmu_replay_record_t *drr;
	dmu_sendarg_t *dsp;
	int err;
	uint64_t fromtxg = 0;
	uint64_t featureflags = 0;
	struct send_thread_arg to_arg;
	void *payload = NULL;
	size_t payload_len = 0;
	struct send_block_record *to_data;

	err = dmu_objset_from_ds(to_ds, &os);
	if (err != 0) {
		dsl_pool_rele(dp, tag);
		return (err);
	}

	/*
	 * If this is a non-raw send of an encrypted ds, we can ensure that
	 * the objset_phys_t is authenticated. This is safe because this is
	 * either a snapshot or we have owned the dataset, ensuring that
	 * it can't be modified.
	 */
	if (!rawok && os->os_encrypted &&
	    arc_is_unauthenticated(os->os_phys_buf)) {
		zbookmark_phys_t zb;

		SET_BOOKMARK(&zb, to_ds->ds_object, ZB_ROOT_OBJECT,
		    ZB_ROOT_LEVEL, ZB_ROOT_BLKID);
		err = arc_untransform(os->os_phys_buf, os->os_spa,
		    &zb, B_FALSE);
		if (err != 0) {
			dsl_pool_rele(dp, tag);
			return (err);
		}

		ASSERT0(arc_is_unauthenticated(os->os_phys_buf));
	}

	drr = kmem_zalloc(sizeof (dmu_replay_record_t), KM_SLEEP);
	drr->drr_type = DRR_BEGIN;
	drr->drr_u.drr_begin.drr_magic = DMU_BACKUP_MAGIC;
	DMU_SET_STREAM_HDRTYPE(drr->drr_u.drr_begin.drr_versioninfo,
	    DMU_SUBSTREAM);

	bzero(&to_arg, sizeof (to_arg));

#ifdef _KERNEL
	if (dmu_objset_type(os) == DMU_OST_ZFS) {
		uint64_t version;
		if (zfs_get_zplprop(os, ZFS_PROP_VERSION, &version) != 0) {
			kmem_free(drr, sizeof (dmu_replay_record_t));
			dsl_pool_rele(dp, tag);
			return (SET_ERROR(EINVAL));
		}
		if (version >= ZPL_VERSION_SA) {
			featureflags |= DMU_BACKUP_FEATURE_SA_SPILL;
		}
	}
#endif

	/* raw sends imply large_block_ok */
	if ((large_block_ok || rawok) &&
	    dsl_dataset_feature_is_active(to_ds, SPA_FEATURE_LARGE_BLOCKS))
		featureflags |= DMU_BACKUP_FEATURE_LARGE_BLOCKS;
	if (dsl_dataset_feature_is_active(to_ds, SPA_FEATURE_LARGE_DNODE))
		featureflags |= DMU_BACKUP_FEATURE_LARGE_DNODE;

	/* encrypted datasets will not have embedded blocks */
	if ((embedok || rawok) && !os->os_encrypted &&
	    spa_feature_is_active(dp->dp_spa, SPA_FEATURE_EMBEDDED_DATA)) {
		featureflags |= DMU_BACKUP_FEATURE_EMBED_DATA;
	}

	/* raw send implies compressok */
	if (compressok || rawok)
		featureflags |= DMU_BACKUP_FEATURE_COMPRESSED;

	if (rawok && os->os_encrypted)
		featureflags |= DMU_BACKUP_FEATURE_RAW;

	if ((featureflags &
	    (DMU_BACKUP_FEATURE_EMBED_DATA | DMU_BACKUP_FEATURE_COMPRESSED |
	    DMU_BACKUP_FEATURE_RAW)) != 0 &&
	    spa_feature_is_active(dp->dp_spa, SPA_FEATURE_LZ4_COMPRESS)) {
		featureflags |= DMU_BACKUP_FEATURE_LZ4;
	}

	if (resumeobj != 0 || resumeoff != 0) {
		featureflags |= DMU_BACKUP_FEATURE_RESUMING;
	}

	DMU_SET_FEATUREFLAGS(drr->drr_u.drr_begin.drr_versioninfo,
	    featureflags);

	drr->drr_u.drr_begin.drr_creation_time =
	    dsl_dataset_phys(to_ds)->ds_creation_time;
	drr->drr_u.drr_begin.drr_type = dmu_objset_type(os);
	if (is_clone)
		drr->drr_u.drr_begin.drr_flags |= DRR_FLAG_CLONE;
	drr->drr_u.drr_begin.drr_toguid = dsl_dataset_phys(to_ds)->ds_guid;
	if (dsl_dataset_phys(to_ds)->ds_flags & DS_FLAG_CI_DATASET)
		drr->drr_u.drr_begin.drr_flags |= DRR_FLAG_CI_DATA;
	if (zfs_send_set_freerecords_bit)
		drr->drr_u.drr_begin.drr_flags |= DRR_FLAG_FREERECORDS;

	drr->drr_u.drr_begin.drr_flags |= DRR_FLAG_SPILL_BLOCK;

	if (ancestor_zb != NULL) {
		drr->drr_u.drr_begin.drr_fromguid =
		    ancestor_zb->zbm_guid;
		fromtxg = ancestor_zb->zbm_creation_txg;
	}
	dsl_dataset_name(to_ds, drr->drr_u.drr_begin.drr_toname);
	if (!to_ds->ds_is_snapshot) {
		(void) strlcat(drr->drr_u.drr_begin.drr_toname, "@--head--",
		    sizeof (drr->drr_u.drr_begin.drr_toname));
	}

	dsp = kmem_zalloc(sizeof (dmu_sendarg_t), KM_SLEEP);

	dsp->dsa_drr = drr;
	dsp->dsa_vp = vp;
	dsp->dsa_outfd = outfd;
	dsp->dsa_proc = curproc;
	dsp->dsa_os = os;
	dsp->dsa_off = off;
	dsp->dsa_toguid = dsl_dataset_phys(to_ds)->ds_guid;
	dsp->dsa_fromtxg = fromtxg;
	dsp->dsa_pending_op = PENDING_NONE;
	dsp->dsa_featureflags = featureflags;
	dsp->dsa_resume_object = resumeobj;
	dsp->dsa_resume_offset = resumeoff;

	mutex_enter(&to_ds->ds_sendstream_lock);
	list_insert_head(&to_ds->ds_sendstreams, dsp);
	mutex_exit(&to_ds->ds_sendstream_lock);

	dsl_dataset_long_hold(to_ds, FTAG);
	dsl_pool_rele(dp, tag);

	/* handle features that require a DRR_BEGIN payload */
	if (featureflags &
	    (DMU_BACKUP_FEATURE_RESUMING | DMU_BACKUP_FEATURE_RAW)) {
		nvlist_t *keynvl = NULL;
		nvlist_t *nvl = fnvlist_alloc();

		if (featureflags & DMU_BACKUP_FEATURE_RESUMING) {
			dmu_object_info_t to_doi;
			err = dmu_object_info(os, resumeobj, &to_doi);
			if (err != 0) {
				fnvlist_free(nvl);
				goto out;
			}

			SET_BOOKMARK(&to_arg.resume, to_ds->ds_object,
			    resumeobj, 0,
			    resumeoff / to_doi.doi_data_block_size);

			fnvlist_add_uint64(nvl, "resume_object", resumeobj);
			fnvlist_add_uint64(nvl, "resume_offset", resumeoff);
		}

		if (featureflags & DMU_BACKUP_FEATURE_RAW) {
			uint64_t ivset_guid = (ancestor_zb != NULL) ?
			    ancestor_zb->zbm_ivset_guid : 0;

			ASSERT(os->os_encrypted);

			err = dsl_crypto_populate_key_nvlist(to_ds,
			    ivset_guid, &keynvl);
			if (err != 0) {
				fnvlist_free(nvl);
				goto out;
			}

			fnvlist_add_nvlist(nvl, "crypt_keydata", keynvl);
		}

		payload = fnvlist_pack(nvl, &payload_len);
		drr->drr_payloadlen = payload_len;
		fnvlist_free(keynvl);
		fnvlist_free(nvl);
	}

	err = dump_record(dsp, payload, payload_len);
	fnvlist_pack_free(payload, payload_len);
	if (err != 0) {
		err = dsp->dsa_err;
		goto out;
	}

	err = bqueue_init(&to_arg.q,
	    MAX(zfs_send_queue_length, 2 * zfs_max_recordsize),
	    offsetof(struct send_block_record, ln));
	to_arg.error_code = 0;
	to_arg.cancel = B_FALSE;
	to_arg.ds = to_ds;
	to_arg.fromtxg = fromtxg;
	to_arg.flags = TRAVERSE_PRE | TRAVERSE_PREFETCH;
	if (rawok)
		to_arg.flags |= TRAVERSE_NO_DECRYPT;
	(void) thread_create(NULL, 0, send_traverse_thread, &to_arg, 0, curproc,
	    TS_RUN, minclsyspri);

	to_data = bqueue_dequeue(&to_arg.q);

	while (!to_data->eos_marker && err == 0) {
		err = do_dump(dsp, to_data);
		to_data = get_next_record(&to_arg.q, to_data);
		if (issig(JUSTLOOKING) && issig(FORREAL))
			err = EINTR;
	}

	if (err != 0) {
		to_arg.cancel = B_TRUE;
		while (!to_data->eos_marker) {
			to_data = get_next_record(&to_arg.q, to_data);
		}
	}
	kmem_free(to_data, sizeof (*to_data));

	bqueue_destroy(&to_arg.q);

	if (err == 0 && to_arg.error_code != 0)
		err = to_arg.error_code;

	if (err != 0)
		goto out;

	if (dsp->dsa_pending_op != PENDING_NONE)
		if (dump_record(dsp, NULL, 0) != 0)
			err = SET_ERROR(EINTR);

	if (err != 0) {
		if (err == EINTR && dsp->dsa_err != 0)
			err = dsp->dsa_err;
		goto out;
	}

	bzero(drr, sizeof (dmu_replay_record_t));
	drr->drr_type = DRR_END;
	drr->drr_u.drr_end.drr_checksum = dsp->dsa_zc;
	drr->drr_u.drr_end.drr_toguid = dsp->dsa_toguid;

	if (dump_record(dsp, NULL, 0) != 0)
		err = dsp->dsa_err;
out:
	mutex_enter(&to_ds->ds_sendstream_lock);
	list_remove(&to_ds->ds_sendstreams, dsp);
	mutex_exit(&to_ds->ds_sendstream_lock);

	VERIFY(err != 0 || (dsp->dsa_sent_begin && dsp->dsa_sent_end));

	kmem_free(drr, sizeof (dmu_replay_record_t));
	kmem_free(dsp, sizeof (dmu_sendarg_t));

	dsl_dataset_long_rele(to_ds, FTAG);

	return (err);
}

int
dmu_send_obj(const char *pool, uint64_t tosnap, uint64_t fromsnap,
    boolean_t embedok, boolean_t large_block_ok, boolean_t compressok,
    boolean_t rawok, int outfd, vnode_t *vp, offset_t *off)
{
	dsl_pool_t *dp;
	dsl_dataset_t *ds;
	dsl_dataset_t *fromds = NULL;
	ds_hold_flags_t dsflags = (rawok) ? 0 : DS_HOLD_FLAG_DECRYPT;
	int err;

	err = dsl_pool_hold(pool, FTAG, &dp);
	if (err != 0)
		return (err);

	err = dsl_dataset_hold_obj_flags(dp, tosnap, dsflags, FTAG, &ds);
	if (err != 0) {
		dsl_pool_rele(dp, FTAG);
		return (err);
	}

	if (fromsnap != 0) {
		zfs_bookmark_phys_t zb = { 0 };
		boolean_t is_clone;

		err = dsl_dataset_hold_obj(dp, fromsnap, FTAG, &fromds);
		if (err != 0) {
			dsl_dataset_rele_flags(ds, dsflags, FTAG);
			dsl_pool_rele(dp, FTAG);
			return (err);
		}
		if (!dsl_dataset_is_before(ds, fromds, 0)) {
			err = SET_ERROR(EXDEV);
			dsl_dataset_rele(fromds, FTAG);
			dsl_dataset_rele_flags(ds, dsflags, FTAG);
			dsl_pool_rele(dp, FTAG);
			return (err);
		}

		zb.zbm_creation_time =
		    dsl_dataset_phys(fromds)->ds_creation_time;
		zb.zbm_creation_txg = dsl_dataset_phys(fromds)->ds_creation_txg;
		zb.zbm_guid = dsl_dataset_phys(fromds)->ds_guid;

		if (dsl_dataset_is_zapified(fromds)) {
			(void) zap_lookup(dp->dp_meta_objset,
			    fromds->ds_object, DS_FIELD_IVSET_GUID, 8, 1,
			    &zb.zbm_ivset_guid);
		}

		is_clone = (fromds->ds_dir != ds->ds_dir);
		dsl_dataset_rele(fromds, FTAG);
		err = dmu_send_impl(FTAG, dp, ds, &zb, is_clone,
		    embedok, large_block_ok, compressok, rawok, outfd,
		    0, 0, vp, off);
	} else {
		err = dmu_send_impl(FTAG, dp, ds, NULL, B_FALSE,
		    embedok, large_block_ok, compressok, rawok, outfd,
		    0, 0, vp, off);
	}
	dsl_dataset_rele_flags(ds, dsflags, FTAG);
	return (err);
}

int
dmu_send(const char *tosnap, const char *fromsnap, boolean_t embedok,
    boolean_t large_block_ok, boolean_t compressok, boolean_t rawok,
    int outfd, uint64_t resumeobj, uint64_t resumeoff, vnode_t *vp,
    offset_t *off)
{
	dsl_pool_t *dp;
	dsl_dataset_t *ds;
	int err;
	ds_hold_flags_t dsflags = (rawok) ? 0 : DS_HOLD_FLAG_DECRYPT;
	boolean_t owned = B_FALSE;

	if (fromsnap != NULL && strpbrk(fromsnap, "@#") == NULL)
		return (SET_ERROR(EINVAL));

	err = dsl_pool_hold(tosnap, FTAG, &dp);
	if (err != 0)
		return (err);
	if (strchr(tosnap, '@') == NULL && spa_writeable(dp->dp_spa)) {
		/*
		 * We are sending a filesystem or volume.  Ensure
		 * that it doesn't change by owning the dataset.
		 */
		err = dsl_dataset_own(dp, tosnap, dsflags, FTAG, &ds);
		owned = B_TRUE;
	} else {
		err = dsl_dataset_hold_flags(dp, tosnap, dsflags, FTAG, &ds);
	}
	if (err != 0) {
		dsl_pool_rele(dp, FTAG);
		return (err);
	}

	if (fromsnap != NULL) {
		zfs_bookmark_phys_t zb = { 0 };
		boolean_t is_clone = B_FALSE;
		int fsnamelen = strchr(tosnap, '@') - tosnap;

		/*
		 * If the fromsnap is in a different filesystem, then
		 * mark the send stream as a clone.
		 */
		if (strncmp(tosnap, fromsnap, fsnamelen) != 0 ||
		    (fromsnap[fsnamelen] != '@' &&
		    fromsnap[fsnamelen] != '#')) {
			is_clone = B_TRUE;
		}

		if (strchr(fromsnap, '@')) {
			dsl_dataset_t *fromds;
			err = dsl_dataset_hold(dp, fromsnap, FTAG, &fromds);
			if (err == 0) {
				if (!dsl_dataset_is_before(ds, fromds, 0))
					err = SET_ERROR(EXDEV);
				zb.zbm_creation_time =
				    dsl_dataset_phys(fromds)->ds_creation_time;
				zb.zbm_creation_txg =
				    dsl_dataset_phys(fromds)->ds_creation_txg;
				zb.zbm_guid = dsl_dataset_phys(fromds)->ds_guid;
				is_clone = (ds->ds_dir != fromds->ds_dir);

				if (dsl_dataset_is_zapified(fromds)) {
					(void) zap_lookup(dp->dp_meta_objset,
					    fromds->ds_object,
					    DS_FIELD_IVSET_GUID, 8, 1,
					    &zb.zbm_ivset_guid);
				}
				dsl_dataset_rele(fromds, FTAG);
			}
		} else {
			err = dsl_bookmark_lookup(dp, fromsnap, ds, &zb);
		}
		if (err != 0) {
			if (owned)
				dsl_dataset_disown(ds, dsflags, FTAG);
			else
				dsl_dataset_rele_flags(ds, dsflags, FTAG);

			dsl_pool_rele(dp, FTAG);
			return (err);
		}
		err = dmu_send_impl(FTAG, dp, ds, &zb, is_clone,
		    embedok, large_block_ok, compressok, rawok,
		    outfd, resumeobj, resumeoff, vp, off);
	} else {
		err = dmu_send_impl(FTAG, dp, ds, NULL, B_FALSE,
		    embedok, large_block_ok, compressok, rawok,
		    outfd, resumeobj, resumeoff, vp, off);
	}
	if (owned)
		dsl_dataset_disown(ds, dsflags, FTAG);
	else
		dsl_dataset_rele_flags(ds, dsflags, FTAG);

	return (err);
}

static int
dmu_adjust_send_estimate_for_indirects(dsl_dataset_t *ds, uint64_t uncompressed,
    uint64_t compressed, boolean_t stream_compressed, uint64_t *sizep)
{
	int err = 0;
	uint64_t size;
	/*
	 * Assume that space (both on-disk and in-stream) is dominated by
	 * data.  We will adjust for indirect blocks and the copies property,
	 * but ignore per-object space used (eg, dnodes and DRR_OBJECT records).
	 */

	uint64_t recordsize;
	uint64_t record_count;
	objset_t *os;
	VERIFY0(dmu_objset_from_ds(ds, &os));

	/* Assume all (uncompressed) blocks are recordsize. */
	if (zfs_override_estimate_recordsize != 0) {
		recordsize = zfs_override_estimate_recordsize;
	} else if (os->os_phys->os_type == DMU_OST_ZVOL) {
		err = dsl_prop_get_int_ds(ds,
		    zfs_prop_to_name(ZFS_PROP_VOLBLOCKSIZE), &recordsize);
	} else {
		err = dsl_prop_get_int_ds(ds,
		    zfs_prop_to_name(ZFS_PROP_RECORDSIZE), &recordsize);
	}
	if (err != 0)
		return (err);
	record_count = uncompressed / recordsize;

	/*
	 * If we're estimating a send size for a compressed stream, use the
	 * compressed data size to estimate the stream size. Otherwise, use the
	 * uncompressed data size.
	 */
	size = stream_compressed ? compressed : uncompressed;

	/*
	 * Subtract out approximate space used by indirect blocks.
	 * Assume most space is used by data blocks (non-indirect, non-dnode).
	 * Assume no ditto blocks or internal fragmentation.
	 *
	 * Therefore, space used by indirect blocks is sizeof(blkptr_t) per
	 * block.
	 */
	size -= record_count * sizeof (blkptr_t);

	/* Add in the space for the record associated with each block. */
	size += record_count * sizeof (dmu_replay_record_t);

	*sizep = size;

	return (0);
}

int
dmu_send_estimate(dsl_dataset_t *ds, dsl_dataset_t *fromds,
    boolean_t stream_compressed, uint64_t *sizep)
{
	int err;
	uint64_t uncomp, comp;

	ASSERT(dsl_pool_config_held(ds->ds_dir->dd_pool));

	/* tosnap must be a snapshot */
	if (!ds->ds_is_snapshot)
		return (SET_ERROR(EINVAL));

	/* fromsnap, if provided, must be a snapshot */
	if (fromds != NULL && !fromds->ds_is_snapshot)
		return (SET_ERROR(EINVAL));

	/*
	 * fromsnap must be an earlier snapshot from the same fs as tosnap,
	 * or the origin's fs.
	 */
	if (fromds != NULL && !dsl_dataset_is_before(ds, fromds, 0))
		return (SET_ERROR(EXDEV));

	/* Get compressed and uncompressed size estimates of changed data. */
	if (fromds == NULL) {
		uncomp = dsl_dataset_phys(ds)->ds_uncompressed_bytes;
		comp = dsl_dataset_phys(ds)->ds_compressed_bytes;
	} else {
		uint64_t used;
		err = dsl_dataset_space_written(fromds, ds,
		    &used, &comp, &uncomp);
		if (err != 0)
			return (err);
	}

	err = dmu_adjust_send_estimate_for_indirects(ds, uncomp, comp,
	    stream_compressed, sizep);
	/*
	 * Add the size of the BEGIN and END records to the estimate.
	 */
	*sizep += 2 * sizeof (dmu_replay_record_t);
	return (err);
}

struct calculate_send_arg {
	uint64_t uncompressed;
	uint64_t compressed;
};

/*
 * Simple callback used to traverse the blocks of a snapshot and sum their
 * uncompressed and compressed sizes.
 */
/* ARGSUSED */
static int
dmu_calculate_send_traversal(spa_t *spa, zilog_t *zilog, const blkptr_t *bp,
    const zbookmark_phys_t *zb, const dnode_phys_t *dnp, void *arg)
{
	struct calculate_send_arg *space = arg;
	if (bp != NULL && !BP_IS_HOLE(bp)) {
		space->uncompressed += BP_GET_UCSIZE(bp);
		space->compressed += BP_GET_PSIZE(bp);
	}
	return (0);
}

/*
 * Given a desination snapshot and a TXG, calculate the approximate size of a
 * send stream sent from that TXG. from_txg may be zero, indicating that the
 * whole snapshot will be sent.
 */
int
dmu_send_estimate_from_txg(dsl_dataset_t *ds, uint64_t from_txg,
    boolean_t stream_compressed, uint64_t *sizep)
{
	int err;
	struct calculate_send_arg size = { 0 };

	ASSERT(dsl_pool_config_held(ds->ds_dir->dd_pool));

	/* tosnap must be a snapshot */
	if (!dsl_dataset_is_snapshot(ds))
		return (SET_ERROR(EINVAL));

	/* verify that from_txg is before the provided snapshot was taken */
	if (from_txg >= dsl_dataset_phys(ds)->ds_creation_txg) {
		return (SET_ERROR(EXDEV));
	}
	/*
	 * traverse the blocks of the snapshot with birth times after
	 * from_txg, summing their uncompressed size
	 */
	err = traverse_dataset(ds, from_txg,
	    TRAVERSE_POST | TRAVERSE_NO_DECRYPT,
	    dmu_calculate_send_traversal, &size);

	if (err)
		return (err);

	err = dmu_adjust_send_estimate_for_indirects(ds, size.uncompressed,
	    size.compressed, stream_compressed, sizep);
	return (err);
}


#if defined(_KERNEL)
/* BEGIN CSTYLED */
module_param(zfs_override_estimate_recordsize, ulong, 0644);
MODULE_PARM_DESC(zfs_override_estimate_recordsize,
	"Record size calculation override for zfs send estimates");
/* END CSTYLED */

module_param(zfs_send_corrupt_data, int, 0644);
MODULE_PARM_DESC(zfs_send_corrupt_data, "Allow sending corrupt data");

module_param(zfs_send_queue_length, int, 0644);
MODULE_PARM_DESC(zfs_send_queue_length, "Maximum send queue length");

module_param(zfs_send_unmodified_spill_blocks, int, 0644);
MODULE_PARM_DESC(zfs_send_unmodified_spill_blocks,
	"Send unmodified spill blocks");
#endif
