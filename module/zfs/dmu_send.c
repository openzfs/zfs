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
int zfs_send_queue_length = 16 * 1024 * 1024;
int zfs_recv_queue_length = 16 * 1024 * 1024;
/* Set this tunable to FALSE to disable setting of DRR_FLAG_FREERECORDS */
int zfs_send_set_freerecords_bit = B_TRUE;

static char *dmu_recv_tag = "dmu_recv_tag";
const char *recv_clone_name = "%recv";

#define	BP_SPAN(datablkszsec, indblkshift, level) \
	(((uint64_t)datablkszsec) << (SPA_MINBLOCKSHIFT + \
	(level) * (indblkshift - SPA_BLKPTRSHIFT)))

static void byteswap_record(dmu_replay_record_t *drr);

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

	if (length != -1ULL && offset + length < offset)
		length = -1ULL;

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
		 * There should never be a PENDING_FREE if length is -1
		 * (because dump_dnode is the only place where this
		 * function is called with a -1, and only after flushing
		 * any pending record).
		 */
		ASSERT(length != -1ULL);
		/*
		 * Check to see whether this free block can be aggregated
		 * with pending one.
		 */
		if (drrf->drr_object == object && drrf->drr_offset +
		    drrf->drr_length == offset) {
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
	drrf->drr_length = length;
	drrf->drr_toguid = dsp->dsa_toguid;
	if (length == -1ULL) {
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

	/* handle raw send fields */
	if (dsp->dsa_featureflags & DMU_BACKUP_FEATURE_RAW) {
		ASSERT(BP_IS_PROTECTED(bp));

		if (BP_SHOULD_BYTESWAP(bp))
			drrs->drr_flags |= DRR_RAW_BYTESWAP;
		drrs->drr_compressiontype = BP_GET_COMPRESS(bp);
		drrs->drr_compressed_size = BP_GET_PSIZE(bp);
		zio_crypt_decode_params_bp(bp, drrs->drr_salt, drrs->drr_iv);
		zio_crypt_decode_mac_bp(bp, drrs->drr_mac);
	}

	if (dump_record(dsp, data, blksz) != 0)
		return (SET_ERROR(EINTR));
	return (0);
}

static int
dump_freeobjects(dmu_sendarg_t *dsp, uint64_t firstobj, uint64_t numobjs)
{
	struct drr_freeobjects *drrfo = &(dsp->dsa_drr->drr_u.drr_freeobjects);

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
	int bonuslen = P2ROUNDUP(dnp->dn_bonuslen, 8);

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

	if ((dsp->dsa_featureflags & DMU_BACKUP_FEATURE_RAW)) {
		ASSERT(BP_IS_ENCRYPTED(bp));

		if (BP_SHOULD_BYTESWAP(bp))
			drro->drr_flags |= DRR_RAW_BYTESWAP;

		/* needed for reconstructing dnp on recv side */
		drro->drr_indblkshift = dnp->dn_indblkshift;
		drro->drr_nlevels = dnp->dn_nlevels;
		drro->drr_nblkptr = dnp->dn_nblkptr;

		/*
		 * Since we encrypt the entire bonus area, the (raw) part
		 * beyond the the bonuslen is actually nonzero, so we need
		 * to send it.
		 */
		if (bonuslen != 0) {
			drro->drr_raw_bonuslen = DN_MAX_BONUS_LEN(dnp);
			bonuslen = drro->drr_raw_bonuslen;
		}
	}

	if (dump_record(dsp, DN_BONUS(dnp), bonuslen) != 0)
		return (SET_ERROR(EINTR));

	/* Free anything past the end of the file. */
	if (dump_free(dsp, object, (dnp->dn_maxblkid + 1) *
	    (dnp->dn_datablkszsec << SPA_MINBLOCKSHIFT), -1ULL) != 0)
		return (SET_ERROR(EINTR));
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
	uint64_t dnobj;

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
		err = dump_free(dsa, zb->zb_object, offset, span);
	} else if (zb->zb_level > 0 || type == DMU_OT_OBJSET) {
		return (0);
	} else if (type == DMU_OT_DNODE) {
		dnode_phys_t *blk;
		int epb = BP_GET_LSIZE(bp) >> DNODE_SHIFT;
		arc_flags_t aflags = ARC_FLAG_WAIT;
		arc_buf_t *abuf;
		enum zio_flag zioflags = ZIO_FLAG_CANFAIL;
		int i;

		if (dsa->dsa_featureflags & DMU_BACKUP_FEATURE_RAW) {
			ASSERT(BP_IS_ENCRYPTED(bp));
			ASSERT3U(BP_GET_COMPRESS(bp), ==, ZIO_COMPRESS_OFF);
			zioflags |= ZIO_FLAG_RAW;
		}

		ASSERT0(zb->zb_level);

		if (arc_read(NULL, spa, bp, arc_getbuf_func, &abuf,
		    ZIO_PRIORITY_ASYNC_READ, zioflags, &aflags, zb) != 0)
			return (SET_ERROR(EIO));

		blk = abuf->b_data;
		dnobj = zb->zb_blkid * epb;

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
			for (i = 0; i < epb; i += blk[i].dn_extra_slots + 1) {
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
		err = arc_untransform(os->os_phys_buf, os->os_spa,
		    to_ds->ds_object, B_FALSE);
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
	    to_ds->ds_feature_inuse[SPA_FEATURE_LARGE_BLOCKS])
		featureflags |= DMU_BACKUP_FEATURE_LARGE_BLOCKS;
	if (to_ds->ds_feature_inuse[SPA_FEATURE_LARGE_DNODE])
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
			ASSERT(os->os_encrypted);

			err = dsl_crypto_populate_key_nvlist(to_ds, &keynvl);
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

	err = bqueue_init(&to_arg.q, zfs_send_queue_length,
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
		zfs_bookmark_phys_t zb;
		boolean_t is_clone;

		err = dsl_dataset_hold_obj(dp, fromsnap, FTAG, &fromds);
		if (err != 0) {
			dsl_dataset_rele_flags(ds, dsflags, FTAG);
			dsl_pool_rele(dp, FTAG);
			return (err);
		}
		if (!dsl_dataset_is_before(ds, fromds, 0))
			err = SET_ERROR(EXDEV);
		zb.zbm_creation_time =
		    dsl_dataset_phys(fromds)->ds_creation_time;
		zb.zbm_creation_txg = dsl_dataset_phys(fromds)->ds_creation_txg;
		zb.zbm_guid = dsl_dataset_phys(fromds)->ds_guid;
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
		zfs_bookmark_phys_t zb;
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
	int err;
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
	if (os->os_phys->os_type == DMU_OST_ZVOL) {
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

typedef struct dmu_recv_begin_arg {
	const char *drba_origin;
	dmu_recv_cookie_t *drba_cookie;
	cred_t *drba_cred;
	uint64_t drba_snapobj;
} dmu_recv_begin_arg_t;

static int
recv_begin_check_existing_impl(dmu_recv_begin_arg_t *drba, dsl_dataset_t *ds,
    uint64_t fromguid)
{
	uint64_t val;
	int error;
	dsl_pool_t *dp = ds->ds_dir->dd_pool;

	/* temporary clone name must not exist */
	error = zap_lookup(dp->dp_meta_objset,
	    dsl_dir_phys(ds->ds_dir)->dd_child_dir_zapobj, recv_clone_name,
	    8, 1, &val);
	if (error != ENOENT)
		return (error == 0 ? EBUSY : error);

	/* new snapshot name must not exist */
	error = zap_lookup(dp->dp_meta_objset,
	    dsl_dataset_phys(ds)->ds_snapnames_zapobj,
	    drba->drba_cookie->drc_tosnap, 8, 1, &val);
	if (error != ENOENT)
		return (error == 0 ? EEXIST : error);

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
			drba->drba_snapobj = obj;
		} else {
			/*
			 * If we are not forcing, there must be no
			 * changes since fromsnap.
			 */
			if (dsl_dataset_modified_since_snap(ds, snap)) {
				dsl_dataset_rele(snap, FTAG);
				return (SET_ERROR(ETXTBSY));
			}
			drba->drba_snapobj = ds->ds_prev->ds_object;
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
		if (ds->ds_dir->dd_crypto_obj != 0)
			return (SET_ERROR(EINVAL));

		drba->drba_snapobj = 0;
	}

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
	uint64_t featureflags = DMU_GET_FEATUREFLAGS(drrb->drr_versioninfo);
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

	/* Verify pool version supports SA if SA_SPILL feature set */
	if ((featureflags & DMU_BACKUP_FEATURE_SA_SPILL) &&
	    spa_version(dp->dp_spa) < SPA_VERSION_SA)
		return (SET_ERROR(ENOTSUP));

	if (drba->drba_cookie->drc_resumable &&
	    !spa_feature_is_enabled(dp->dp_spa, SPA_FEATURE_EXTENSIBLE_DATASET))
		return (SET_ERROR(ENOTSUP));

	/*
	 * The receiving code doesn't know how to translate a WRITE_EMBEDDED
	 * record to a plain WRITE record, so the pool must have the
	 * EMBEDDED_DATA feature enabled if the stream has WRITE_EMBEDDED
	 * records.  Same with WRITE_EMBEDDED records that use LZ4 compression.
	 */
	if ((featureflags & DMU_BACKUP_FEATURE_EMBED_DATA) &&
	    !spa_feature_is_enabled(dp->dp_spa, SPA_FEATURE_EMBEDDED_DATA))
		return (SET_ERROR(ENOTSUP));
	if ((featureflags & DMU_BACKUP_FEATURE_LZ4) &&
	    !spa_feature_is_enabled(dp->dp_spa, SPA_FEATURE_LZ4_COMPRESS))
		return (SET_ERROR(ENOTSUP));

	/*
	 * The receiving code doesn't know how to translate large blocks
	 * to smaller ones, so the pool must have the LARGE_BLOCKS
	 * feature enabled if the stream has LARGE_BLOCKS. Same with
	 * large dnodes.
	 */
	if ((featureflags & DMU_BACKUP_FEATURE_LARGE_BLOCKS) &&
	    !spa_feature_is_enabled(dp->dp_spa, SPA_FEATURE_LARGE_BLOCKS))
		return (SET_ERROR(ENOTSUP));
	if ((featureflags & DMU_BACKUP_FEATURE_LARGE_DNODE) &&
	    !spa_feature_is_enabled(dp->dp_spa, SPA_FEATURE_LARGE_DNODE))
		return (SET_ERROR(ENOTSUP));

	if ((featureflags & DMU_BACKUP_FEATURE_RAW)) {
		/* raw receives require the encryption feature */
		if (!spa_feature_is_enabled(dp->dp_spa, SPA_FEATURE_ENCRYPTION))
			return (SET_ERROR(ENOTSUP));
	} else {
		dsflags |= DS_HOLD_FLAG_DECRYPT;
	}

	error = dsl_dataset_hold_flags(dp, tofs, dsflags, FTAG, &ds);
	if (error == 0) {
		/* target fs already exists; recv into temp clone */

		/* Can't recv a clone into an existing fs */
		if (flags & DRR_FLAG_CLONE || drba->drba_origin) {
			dsl_dataset_rele_flags(ds, dsflags, FTAG);
			return (SET_ERROR(EINVAL));
		}

		error = recv_begin_check_existing_impl(drba, ds, fromguid);
		dsl_dataset_rele_flags(ds, dsflags, FTAG);
	} else if (error == ENOENT) {
		/* target fs does not exist; must be a full backup or clone */
		char buf[ZFS_MAX_DATASET_NAME_LEN];

		/*
		 * If it's a non-clone incremental, we are missing the
		 * target fs, so fail the recv.
		 */
		if (fromguid != 0 && !(flags & DRR_FLAG_CLONE ||
		    drba->drba_origin))
			return (SET_ERROR(ENOENT));

		/*
		 * If we're receiving a full send as a clone, and it doesn't
		 * contain all the necessary free records and freeobject
		 * records, reject it.
		 */
		if (fromguid == 0 && drba->drba_origin &&
		    !(flags & DRR_FLAG_FREERECORDS))
			return (SET_ERROR(EINVAL));

		/* Open the parent of tofs */
		ASSERT3U(strlen(tofs), <, sizeof (buf));
		(void) strlcpy(buf, tofs, strrchr(tofs, '/') - tofs + 1);
		error = dsl_dataset_hold_flags(dp, buf, dsflags, FTAG, &ds);
		if (error != 0)
			return (error);

		/*
		 * Check filesystem and snapshot limits before receiving. We'll
		 * recheck snapshot limits again at the end (we create the
		 * filesystems and increment those counts during begin_sync).
		 */
		error = dsl_fs_ss_limit_check(ds->ds_dir, 1,
		    ZFS_PROP_FILESYSTEM_LIMIT, NULL, drba->drba_cred);
		if (error != 0) {
			dsl_dataset_rele_flags(ds, dsflags, FTAG);
			return (error);
		}

		error = dsl_fs_ss_limit_check(ds->ds_dir, 1,
		    ZFS_PROP_SNAPSHOT_LIMIT, NULL, drba->drba_cred);
		if (error != 0) {
			dsl_dataset_rele_flags(ds, dsflags, FTAG);
			return (error);
		}

		if (drba->drba_origin != NULL) {
			dsl_dataset_t *origin;

			error = dsl_dataset_hold_flags(dp, drba->drba_origin,
			    dsflags, FTAG, &origin);
			if (error != 0) {
				dsl_dataset_rele_flags(ds, dsflags, FTAG);
				return (error);
			}
			if (!origin->ds_is_snapshot) {
				dsl_dataset_rele_flags(origin, dsflags, FTAG);
				dsl_dataset_rele_flags(ds, dsflags, FTAG);
				return (SET_ERROR(EINVAL));
			}
			if (dsl_dataset_phys(origin)->ds_guid != fromguid &&
			    fromguid != 0) {
				dsl_dataset_rele_flags(origin, dsflags, FTAG);
				dsl_dataset_rele_flags(ds, dsflags, FTAG);
				return (SET_ERROR(ENODEV));
			}
			dsl_dataset_rele_flags(origin,
			    dsflags, FTAG);
		}
		dsl_dataset_rele_flags(ds, dsflags, FTAG);
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
	struct drr_begin *drrb = drba->drba_cookie->drc_drrb;
	const char *tofs = drba->drba_cookie->drc_tofs;
	uint64_t featureflags = DMU_GET_FEATUREFLAGS(drrb->drr_versioninfo);
	dsl_dataset_t *ds, *newds;
	objset_t *os;
	uint64_t dsobj;
	ds_hold_flags_t dsflags = 0;
	int error;
	uint64_t crflags = 0;
	dsl_crypto_params_t *dcpp = NULL;
	dsl_crypto_params_t dcp = { 0 };

	if (drrb->drr_flags & DRR_FLAG_CI_DATA)
		crflags |= DS_FLAG_CI_DATASET;
	if ((featureflags & DMU_BACKUP_FEATURE_RAW) == 0) {
		dsflags |= DS_HOLD_FLAG_DECRYPT;
	} else {
		dcp.cp_cmd = DCP_CMD_RAW_RECV;
	}

	error = dsl_dataset_hold_flags(dp, tofs, dsflags, FTAG, &ds);
	if (error == 0) {
		/* create temporary clone */
		dsl_dataset_t *snap = NULL;

		if (drba->drba_snapobj != 0) {
			VERIFY0(dsl_dataset_hold_obj(dp,
			    drba->drba_snapobj, FTAG, &snap));
		} else {
			/* we use the dcp whenever we are not making a clone */
			dcpp = &dcp;
		}

		dsobj = dsl_dataset_create_sync(ds->ds_dir, recv_clone_name,
		    snap, crflags, drba->drba_cred, dcpp, tx);
		if (drba->drba_snapobj != 0)
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
		} else {
			/* we use the dcp whenever we are not making a clone */
			dcpp = &dcp;
		}

		/* Create new dataset. */
		dsobj = dsl_dataset_create_sync(dd, strrchr(tofs, '/') + 1,
		    origin, crflags, drba->drba_cred, dcpp, tx);
		if (origin != NULL)
			dsl_dataset_rele(origin, FTAG);
		dsl_dir_rele(dd, FTAG);
		drba->drba_cookie->drc_newfs = B_TRUE;
	}
	VERIFY0(dsl_dataset_own_obj(dp, dsobj, dsflags, dmu_recv_tag, &newds));
	VERIFY0(dmu_objset_from_ds(newds, &os));

	if (drba->drba_cookie->drc_resumable) {
		uint64_t one = 1;
		uint64_t zero = 0;

		dsl_dataset_zapify(newds, tx);
		if (drrb->drr_fromguid != 0) {
			VERIFY0(zap_add(mos, dsobj, DS_FIELD_RESUME_FROMGUID,
			    8, 1, &drrb->drr_fromguid, tx));
		}
		VERIFY0(zap_add(mos, dsobj, DS_FIELD_RESUME_TOGUID,
		    8, 1, &drrb->drr_toguid, tx));
		VERIFY0(zap_add(mos, dsobj, DS_FIELD_RESUME_TONAME,
		    1, strlen(drrb->drr_toname) + 1, drrb->drr_toname, tx));
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

	spa_history_log_internal_ds(newds, "receive", tx, "");
}

static int
dmu_recv_resume_begin_check(void *arg, dmu_tx_t *tx)
{
	dmu_recv_begin_arg_t *drba = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	struct drr_begin *drrb = drba->drba_cookie->drc_drrb;
	int error;
	ds_hold_flags_t dsflags = 0;
	uint64_t featureflags = DMU_GET_FEATUREFLAGS(drrb->drr_versioninfo);
	dsl_dataset_t *ds;
	const char *tofs = drba->drba_cookie->drc_tofs;
	uint64_t val;

	/* 6 extra bytes for /%recv */
	char recvname[ZFS_MAX_DATASET_NAME_LEN + 6];

	/* already checked */
	ASSERT3U(drrb->drr_magic, ==, DMU_BACKUP_MAGIC);
	ASSERT(featureflags & DMU_BACKUP_FEATURE_RESUMING);

	if (DMU_GET_STREAM_HDRTYPE(drrb->drr_versioninfo) ==
	    DMU_COMPOUNDSTREAM ||
	    drrb->drr_type >= DMU_OST_NUMTYPES)
		return (SET_ERROR(EINVAL));

	/* Verify pool version supports SA if SA_SPILL feature set */
	if ((featureflags & DMU_BACKUP_FEATURE_SA_SPILL) &&
	    spa_version(dp->dp_spa) < SPA_VERSION_SA)
		return (SET_ERROR(ENOTSUP));

	/*
	 * The receiving code doesn't know how to translate a WRITE_EMBEDDED
	 * record to a plain WRITE record, so the pool must have the
	 * EMBEDDED_DATA feature enabled if the stream has WRITE_EMBEDDED
	 * records.  Same with WRITE_EMBEDDED records that use LZ4 compression.
	 */
	if ((featureflags & DMU_BACKUP_FEATURE_EMBED_DATA) &&
	    !spa_feature_is_enabled(dp->dp_spa, SPA_FEATURE_EMBEDDED_DATA))
		return (SET_ERROR(ENOTSUP));
	if ((featureflags & DMU_BACKUP_FEATURE_LZ4) &&
	    !spa_feature_is_enabled(dp->dp_spa, SPA_FEATURE_LZ4_COMPRESS))
		return (SET_ERROR(ENOTSUP));

	/*
	 * The receiving code doesn't know how to translate large blocks
	 * to smaller ones, so the pool must have the LARGE_BLOCKS
	 * feature enabled if the stream has LARGE_BLOCKS. Same with
	 * large dnodes.
	 */
	if ((featureflags & DMU_BACKUP_FEATURE_LARGE_BLOCKS) &&
	    !spa_feature_is_enabled(dp->dp_spa, SPA_FEATURE_LARGE_BLOCKS))
		return (SET_ERROR(ENOTSUP));
	if ((featureflags & DMU_BACKUP_FEATURE_LARGE_DNODE) &&
	    !spa_feature_is_enabled(dp->dp_spa, SPA_FEATURE_LARGE_DNODE))
		return (SET_ERROR(ENOTSUP));

	(void) snprintf(recvname, sizeof (recvname), "%s/%s",
	    tofs, recv_clone_name);

	if ((featureflags & DMU_BACKUP_FEATURE_RAW) == 0)
		dsflags |= DS_HOLD_FLAG_DECRYPT;

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

	dsl_dataset_rele_flags(ds, dsflags, FTAG);
	return (0);
}

static void
dmu_recv_resume_begin_sync(void *arg, dmu_tx_t *tx)
{
	dmu_recv_begin_arg_t *drba = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	const char *tofs = drba->drba_cookie->drc_tofs;
	struct drr_begin *drrb = drba->drba_cookie->drc_drrb;
	uint64_t featureflags = DMU_GET_FEATUREFLAGS(drrb->drr_versioninfo);
	dsl_dataset_t *ds;
	objset_t *os;
	ds_hold_flags_t dsflags = 0;
	uint64_t dsobj;
	/* 6 extra bytes for /%recv */
	char recvname[ZFS_MAX_DATASET_NAME_LEN + 6];

	(void) snprintf(recvname, sizeof (recvname), "%s/%s",
	    tofs, recv_clone_name);

	if (featureflags & DMU_BACKUP_FEATURE_RAW) {
		drba->drba_cookie->drc_raw = B_TRUE;
	} else {
		dsflags |= DS_HOLD_FLAG_DECRYPT;
	}

	if (dsl_dataset_hold_flags(dp, recvname, dsflags, FTAG, &ds) != 0) {
		/* %recv does not exist; continue in tofs */
		VERIFY0(dsl_dataset_hold_flags(dp, tofs, dsflags, FTAG, &ds));
		drba->drba_cookie->drc_newfs = B_TRUE;
	}

	/* clear the inconsistent flag so that we can own it */
	ASSERT(DS_IS_INCONSISTENT(ds));
	dmu_buf_will_dirty(ds->ds_dbuf, tx);
	dsl_dataset_phys(ds)->ds_flags &= ~DS_FLAG_INCONSISTENT;
	dsobj = ds->ds_object;
	dsl_dataset_rele_flags(ds, dsflags, FTAG);

	VERIFY0(dsl_dataset_own_obj(dp, dsobj, dsflags, dmu_recv_tag, &ds));
	VERIFY0(dmu_objset_from_ds(ds, &os));

	dmu_buf_will_dirty(ds->ds_dbuf, tx);
	dsl_dataset_phys(ds)->ds_flags |= DS_FLAG_INCONSISTENT;

	rrw_enter(&ds->ds_bp_rwlock, RW_READER, FTAG);
	ASSERT(!BP_IS_HOLE(dsl_dataset_get_blkptr(ds)));
	rrw_exit(&ds->ds_bp_rwlock, FTAG);

	drba->drba_cookie->drc_ds = ds;

	spa_history_log_internal_ds(ds, "resume receive", tx, "");
}

/*
 * NB: callers *MUST* call dmu_recv_stream() if dmu_recv_begin()
 * succeeds; otherwise we will leak the holds on the datasets.
 */
int
dmu_recv_begin(char *tofs, char *tosnap, dmu_replay_record_t *drr_begin,
    boolean_t force, boolean_t resumable, char *origin, dmu_recv_cookie_t *drc)
{
	dmu_recv_begin_arg_t drba = { 0 };

	bzero(drc, sizeof (dmu_recv_cookie_t));
	drc->drc_drr_begin = drr_begin;
	drc->drc_drrb = &drr_begin->drr_u.drr_begin;
	drc->drc_tosnap = tosnap;
	drc->drc_tofs = tofs;
	drc->drc_force = force;
	drc->drc_resumable = resumable;
	drc->drc_cred = CRED();

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

	drba.drba_origin = origin;
	drba.drba_cookie = drc;
	drba.drba_cred = CRED();

	if (DMU_GET_FEATUREFLAGS(drc->drc_drrb->drr_versioninfo) &
	    DMU_BACKUP_FEATURE_RESUMING) {
		return (dsl_sync_task(tofs,
		    dmu_recv_resume_begin_check, dmu_recv_resume_begin_sync,
		    &drba, 5, ZFS_SPACE_CHECK_NORMAL));
	} else  {
		return (dsl_sync_task(tofs,
		    dmu_recv_begin_check, dmu_recv_begin_sync,
		    &drba, 5, ZFS_SPACE_CHECK_NORMAL));
	}
}

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
	boolean_t raw;
	uint64_t last_object, last_offset;
	uint64_t bytes_read; /* bytes read when current record created */
};

struct objlist {
	list_t list; /* List of struct receive_objnode. */
	/*
	 * Last object looked up. Used to assert that objects are being looked
	 * up in ascending order.
	 */
	uint64_t last_lookup;
};

struct receive_objnode {
	list_node_t node;
	uint64_t object;
};

struct receive_arg  {
	objset_t *os;
	vnode_t *vp; /* The vnode to read the stream from */
	uint64_t voff; /* The current offset in the stream */
	uint64_t bytes_read;
	/*
	 * A record that has had its payload read in, but hasn't yet been handed
	 * off to the worker thread.
	 */
	struct receive_record_arg *rrd;
	/* A record that has had its header read in, but not its payload. */
	struct receive_record_arg *next_rrd;
	zio_cksum_t cksum;
	zio_cksum_t prev_cksum;
	int err;
	boolean_t byteswap;
	boolean_t raw;
	uint64_t featureflags;
	/* Sorted list of objects not to issue prefetches for. */
	struct objlist ignore_objlist;
};

typedef struct guid_map_entry {
	uint64_t	guid;
	boolean_t	raw;
	dsl_dataset_t	*gme_ds;
	avl_node_t	avlnode;
} guid_map_entry_t;

static int
guid_compare(const void *arg1, const void *arg2)
{
	const guid_map_entry_t *gmep1 = (const guid_map_entry_t *)arg1;
	const guid_map_entry_t *gmep2 = (const guid_map_entry_t *)arg2;

	return (AVL_CMP(gmep1->guid, gmep2->guid));
}

static void
free_guid_map_onexit(void *arg)
{
	avl_tree_t *ca = arg;
	void *cookie = NULL;
	guid_map_entry_t *gmep;

	while ((gmep = avl_destroy_nodes(ca, &cookie)) != NULL) {
		dsl_dataset_long_rele(gmep->gme_ds, gmep);
		dsl_dataset_rele_flags(gmep->gme_ds,
		    (gmep->raw) ? 0 : DS_HOLD_FLAG_DECRYPT, gmep);
		kmem_free(gmep, sizeof (guid_map_entry_t));
	}
	avl_destroy(ca);
	kmem_free(ca, sizeof (avl_tree_t));
}

static int
receive_read(struct receive_arg *ra, int len, void *buf)
{
	int done = 0;

	/*
	 * The code doesn't rely on this (lengths being multiples of 8).  See
	 * comment in dump_bytes.
	 */
	ASSERT(len % 8 == 0 ||
	    (ra->featureflags & DMU_BACKUP_FEATURE_RAW) != 0);

	while (done < len) {
		ssize_t resid;

		ra->err = vn_rdwr(UIO_READ, ra->vp,
		    (char *)buf + done, len - done,
		    ra->voff, UIO_SYSSPACE, FAPPEND,
		    RLIM64_INFINITY, CRED(), &resid);

		if (resid == len - done) {
			/*
			 * Note: ECKSUM indicates that the receive
			 * was interrupted and can potentially be resumed.
			 */
			ra->err = SET_ERROR(ECKSUM);
		}
		ra->voff += len - done - resid;
		done = len - resid;
		if (ra->err != 0)
			return (ra->err);
	}

	ra->bytes_read += len;

	ASSERT3U(done, ==, len);
	return (0);
}

noinline static void
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
	    drro->drr_dn_slots >
	    (spa_maxdnodesize(dmu_objset_spa(rwa->os)) >> DNODE_SHIFT))  {
		return (SET_ERROR(EINVAL));
	}

	if (rwa->raw) {
		if (drro->drr_raw_bonuslen < drro->drr_bonuslen ||
		    drro->drr_indblkshift > SPA_MAXBLOCKSHIFT ||
		    drro->drr_nlevels > DN_MAX_LEVELS ||
		    drro->drr_nblkptr > DN_MAX_NBLKPTR ||
		    DN_SLOTS_TO_BONUSLEN(drro->drr_dn_slots) <
		    drro->drr_raw_bonuslen)
			return (SET_ERROR(EINVAL));
	} else {
		if (drro->drr_flags != 0 || drro->drr_raw_bonuslen != 0 ||
		    drro->drr_indblkshift != 0 || drro->drr_nlevels != 0 ||
		    drro->drr_nblkptr != 0)
			return (SET_ERROR(EINVAL));
	}

	err = dmu_object_info(rwa->os, drro->drr_object, &doi);

	if (err != 0 && err != ENOENT)
		return (SET_ERROR(EINVAL));
	object = err == 0 ? drro->drr_object : DMU_NEW_OBJECT;

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

		/* nblkptr will be bounded by the bonus size and type */
		if (rwa->raw && nblkptr != drro->drr_nblkptr)
			return (SET_ERROR(EINVAL));

		if (drro->drr_blksz != doi.doi_data_block_size ||
		    nblkptr < doi.doi_nblkptr ||
		    (rwa->raw &&
		    (indblksz != doi.doi_metadata_block_size ||
		    drro->drr_nlevels < doi.doi_indirection))) {
			err = dmu_free_long_range(rwa->os, drro->drr_object,
			    0, DMU_OBJECT_END);
			if (err != 0)
				return (SET_ERROR(EINVAL));
		}
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
		/* currently free, want to be allocated */
		err = dmu_object_claim_dnsize(rwa->os, drro->drr_object,
		    drro->drr_type, drro->drr_blksz,
		    drro->drr_bonustype, drro->drr_bonuslen,
		    drro->drr_dn_slots << DNODE_SHIFT, tx);
	} else if (drro->drr_type != doi.doi_type ||
	    drro->drr_blksz != doi.doi_data_block_size ||
	    drro->drr_bonustype != doi.doi_bonus_type ||
	    drro->drr_bonuslen != doi.doi_bonus_size) {
		/* currently allocated, but with different properties */
		err = dmu_object_reclaim(rwa->os, drro->drr_object,
		    drro->drr_type, drro->drr_blksz,
		    drro->drr_bonustype, drro->drr_bonuslen, tx);
	}
	if (err != 0) {
		dmu_tx_commit(tx);
		return (SET_ERROR(EINVAL));
	}

	if (rwa->raw)
		VERIFY0(dmu_object_dirty_raw(rwa->os, drro->drr_object, tx));

	dmu_object_set_checksum(rwa->os, drro->drr_object,
	    drro->drr_checksumtype, tx);
	dmu_object_set_compress(rwa->os, drro->drr_object,
	    drro->drr_compress, tx);

	/* handle more restrictive dnode structuring for raw recvs */
	if (rwa->raw) {
		/*
		 * Set the indirect block shift and nlevels. This will not fail
		 * because we ensured all of the blocks were free earlier if
		 * this is a new object.
		 */
		VERIFY0(dmu_object_set_blocksize(rwa->os, drro->drr_object,
		    drro->drr_blksz, drro->drr_indblkshift, tx));
		VERIFY0(dmu_object_set_nlevels(rwa->os, drro->drr_object,
		    drro->drr_nlevels, tx));
	}

	if (data != NULL) {
		dmu_buf_t *db;
		uint32_t flags = DMU_READ_NO_PREFETCH;

		if (rwa->raw)
			flags |= DMU_READ_NO_DECRYPT;

		VERIFY0(dmu_bonus_hold_impl(rwa->os, drro->drr_object,
		    FTAG, flags, &db));
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
	    obj < drrfo->drr_firstobj + drrfo->drr_numobjs && next_err == 0;
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
	dmu_tx_t *tx;
	dmu_buf_t *bonus;
	int err;

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

	if (rwa->raw)
		VERIFY0(dmu_object_dirty_raw(rwa->os, drrw->drr_object, tx));

	if (rwa->byteswap && !arc_is_encrypted(abuf) &&
	    arc_get_compression(abuf) == ZIO_COMPRESS_OFF) {
		dmu_object_byteswap_t byteswap =
		    DMU_OT_BYTESWAP(drrw->drr_type);
		dmu_ot_byteswap[byteswap].ob_func(abuf->b_data,
		    DRR_WRITE_PAYLOAD_SIZE(drrw));
	}

	/* use the bonus buf to look up the dnode in dmu_assign_arcbuf */
	if (dmu_bonus_hold(rwa->os, drrw->drr_object, FTAG, &bonus) != 0)
		return (SET_ERROR(EINVAL));
	dmu_assign_arcbuf(bonus, drrw->drr_offset, abuf, tx);

	/*
	 * Note: If the receive fails, we want the resume stream to start
	 * with the same record that we last successfully received (as opposed
	 * to the next record), so that we can verify that we are
	 * resuming from the correct location.
	 */
	save_resume_state(rwa, drrw->drr_object, drrw->drr_offset, tx);
	dmu_tx_commit(tx);
	dmu_buf_rele(bonus, FTAG);

	return (0);
}

/*
 * Handle a DRR_WRITE_BYREF record.  This record is used in dedup'ed
 * streams to refer to a copy of the data that is already on the
 * system because it came in earlier in the stream.  This function
 * finds the earlier copy of the data, and uses that copy instead of
 * data from the stream to fulfill this write.
 */
static int
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
		VERIFY0(dmu_object_dirty_raw(rwa->os, drrwbr->drr_object, tx));
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

	if (rwa->raw) {
		if (!DMU_OT_IS_VALID(drrs->drr_type) ||
		    drrs->drr_compressiontype >= ZIO_COMPRESS_FUNCTIONS ||
		    drrs->drr_compressed_size == 0)
			return (SET_ERROR(EINVAL));
	}

	if (dmu_object_info(rwa->os, drrs->drr_object, NULL) != 0)
		return (SET_ERROR(EINVAL));

	VERIFY0(dmu_bonus_hold(rwa->os, drrs->drr_object, FTAG, &db));
	if ((err = dmu_spill_hold_by_bonus(db, FTAG, &db_spill)) != 0) {
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
	dmu_buf_will_dirty(db_spill, tx);
	if (rwa->raw)
		VERIFY0(dmu_object_dirty_raw(rwa->os, drrs->drr_object, tx));

	if (db_spill->db_size < drrs->drr_length)
		VERIFY(0 == dbuf_spill_set_blksz(db_spill,
		    drrs->drr_length, tx));
	dmu_assign_arcbuf_impl(db_spill, abuf, tx);

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

	err = dmu_free_long_range(rwa->os, drrf->drr_object,
	    drrf->drr_offset, drrf->drr_length);

	return (err);
}

static int
receive_object_range(struct receive_writer_arg *rwa,
    struct drr_object_range *drror)
{
	int ret;
	dmu_tx_t *tx;
	dnode_t *mdn = NULL;
	dmu_buf_t *db = NULL;
	uint64_t offset;

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

	offset = drror->drr_firstobj * sizeof (dnode_phys_t);
	mdn = DMU_META_DNODE(rwa->os);

	tx = dmu_tx_create(rwa->os);
	ret = dmu_tx_assign(tx, TXG_WAIT);
	if (ret != 0) {
		dmu_tx_abort(tx);
		return (ret);
	}

	ret = dmu_buf_hold_by_dnode(mdn, offset, FTAG, &db,
	    DMU_READ_PREFETCH | DMU_READ_NO_DECRYPT);
	if (ret != 0) {
		dmu_tx_commit(tx);
		return (ret);
	}

	/*
	 * Convert the buffer associated with this range of dnodes to a
	 * raw buffer. This ensures that it will be written out as a raw
	 * buffer when we fill in the dnode objects in future records.
	 * Since we are commiting this tx now, it is technically possible
	 * for the dnode block to end up on-disk with the incorrect MAC.
	 * Despite this, the dataset is marked as inconsistent so no other
	 * code paths (apart from scrubs) will attempt to read this data.
	 * Scrubs will not be effected by this either since scrubs only
	 * read raw data and do not attempt to check the MAC.
	 */
	dmu_convert_to_raw(db, byteorder, drror->drr_salt, drror->drr_iv,
	    drror->drr_mac, tx);
	dmu_buf_rele(db, FTAG);
	dmu_tx_commit(tx);
	return (0);
}

/* used to destroy the drc_ds on error */
static void
dmu_recv_cleanup_ds(dmu_recv_cookie_t *drc)
{
	ds_hold_flags_t dsflags = (drc->drc_raw) ? 0 : DS_HOLD_FLAG_DECRYPT;

	/*
	 * Wait for the txg sync before cleaning up the receive. For
	 * resumable receives, this ensures that our resume state has
	 * been written out to disk. For raw receives, this ensures
	 * that the user accounting code will not attempt to do anything
	 * after we stopped receiving the dataset.
	 */
	txg_wait_synced(drc->drc_ds->ds_dir->dd_pool, 0);

	if (drc->drc_resumable) {
		dsl_dataset_disown(drc->drc_ds, dsflags, dmu_recv_tag);
	} else {
		char name[ZFS_MAX_DATASET_NAME_LEN];
		dsl_dataset_name(drc->drc_ds, name);
		dsl_dataset_disown(drc->drc_ds, dsflags, dmu_recv_tag);
		(void) dsl_destroy_head(name);
	}
}

static void
receive_cksum(struct receive_arg *ra, int len, void *buf)
{
	if (ra->byteswap) {
		(void) fletcher_4_incremental_byteswap(buf, len, &ra->cksum);
	} else {
		(void) fletcher_4_incremental_native(buf, len, &ra->cksum);
	}
}

/*
 * Read the payload into a buffer of size len, and update the current record's
 * payload field.
 * Allocate ra->next_rrd and read the next record's header into
 * ra->next_rrd->header.
 * Verify checksum of payload and next record.
 */
static int
receive_read_payload_and_next_header(struct receive_arg *ra, int len, void *buf)
{
	int err;
	zio_cksum_t cksum_orig;
	zio_cksum_t *cksump;

	if (len != 0) {
		ASSERT3U(len, <=, SPA_MAXBLOCKSIZE);
		err = receive_read(ra, len, buf);
		if (err != 0)
			return (err);
		receive_cksum(ra, len, buf);

		/* note: rrd is NULL when reading the begin record's payload */
		if (ra->rrd != NULL) {
			ra->rrd->payload = buf;
			ra->rrd->payload_size = len;
			ra->rrd->bytes_read = ra->bytes_read;
		}
	}

	ra->prev_cksum = ra->cksum;

	ra->next_rrd = kmem_zalloc(sizeof (*ra->next_rrd), KM_SLEEP);
	err = receive_read(ra, sizeof (ra->next_rrd->header),
	    &ra->next_rrd->header);
	ra->next_rrd->bytes_read = ra->bytes_read;

	if (err != 0) {
		kmem_free(ra->next_rrd, sizeof (*ra->next_rrd));
		ra->next_rrd = NULL;
		return (err);
	}
	if (ra->next_rrd->header.drr_type == DRR_BEGIN) {
		kmem_free(ra->next_rrd, sizeof (*ra->next_rrd));
		ra->next_rrd = NULL;
		return (SET_ERROR(EINVAL));
	}

	/*
	 * Note: checksum is of everything up to but not including the
	 * checksum itself.
	 */
	ASSERT3U(offsetof(dmu_replay_record_t, drr_u.drr_checksum.drr_checksum),
	    ==, sizeof (dmu_replay_record_t) - sizeof (zio_cksum_t));
	receive_cksum(ra,
	    offsetof(dmu_replay_record_t, drr_u.drr_checksum.drr_checksum),
	    &ra->next_rrd->header);

	cksum_orig = ra->next_rrd->header.drr_u.drr_checksum.drr_checksum;
	cksump = &ra->next_rrd->header.drr_u.drr_checksum.drr_checksum;

	if (ra->byteswap)
		byteswap_record(&ra->next_rrd->header);

	if ((!ZIO_CHECKSUM_IS_ZERO(cksump)) &&
	    !ZIO_CHECKSUM_EQUAL(ra->cksum, *cksump)) {
		kmem_free(ra->next_rrd, sizeof (*ra->next_rrd));
		ra->next_rrd = NULL;
		return (SET_ERROR(ECKSUM));
	}

	receive_cksum(ra, sizeof (cksum_orig), &cksum_orig);

	return (0);
}

static void
objlist_create(struct objlist *list)
{
	list_create(&list->list, sizeof (struct receive_objnode),
	    offsetof(struct receive_objnode, node));
	list->last_lookup = 0;
}

static void
objlist_destroy(struct objlist *list)
{
	struct receive_objnode *n;

	for (n = list_remove_head(&list->list);
	    n != NULL; n = list_remove_head(&list->list)) {
		kmem_free(n, sizeof (*n));
	}
	list_destroy(&list->list);
}

/*
 * This function looks through the objlist to see if the specified object number
 * is contained in the objlist.  In the process, it will remove all object
 * numbers in the list that are smaller than the specified object number.  Thus,
 * any lookup of an object number smaller than a previously looked up object
 * number will always return false; therefore, all lookups should be done in
 * ascending order.
 */
static boolean_t
objlist_exists(struct objlist *list, uint64_t object)
{
	struct receive_objnode *node = list_head(&list->list);
	ASSERT3U(object, >=, list->last_lookup);
	list->last_lookup = object;
	while (node != NULL && node->object < object) {
		VERIFY3P(node, ==, list_remove_head(&list->list));
		kmem_free(node, sizeof (*node));
		node = list_head(&list->list);
	}
	return (node != NULL && node->object == object);
}

/*
 * The objlist is a list of object numbers stored in ascending order.  However,
 * the insertion of new object numbers does not seek out the correct location to
 * store a new object number; instead, it appends it to the list for simplicity.
 * Thus, any users must take care to only insert new object numbers in ascending
 * order.
 */
static void
objlist_insert(struct objlist *list, uint64_t object)
{
	struct receive_objnode *node = kmem_zalloc(sizeof (*node), KM_SLEEP);
	node->object = object;
#ifdef ZFS_DEBUG
	{
	struct receive_objnode *last_object = list_tail(&list->list);
	uint64_t last_objnum = (last_object != NULL ? last_object->object : 0);
	ASSERT3U(node->object, >, last_objnum);
	}
#endif
	list_insert_tail(&list->list, node);
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
receive_read_prefetch(struct receive_arg *ra,
    uint64_t object, uint64_t offset, uint64_t length)
{
	if (!objlist_exists(&ra->ignore_objlist, object)) {
		dmu_prefetch(ra->os, object, 1, offset, length,
		    ZIO_PRIORITY_SYNC_READ);
	}
}

/*
 * Read records off the stream, issuing any necessary prefetches.
 */
static int
receive_read_record(struct receive_arg *ra)
{
	int err;

	switch (ra->rrd->header.drr_type) {
	case DRR_OBJECT:
	{
		struct drr_object *drro = &ra->rrd->header.drr_u.drr_object;
		uint32_t size = DRR_OBJECT_PAYLOAD_SIZE(drro);
		void *buf = kmem_zalloc(size, KM_SLEEP);
		dmu_object_info_t doi;

		err = receive_read_payload_and_next_header(ra, size, buf);
		if (err != 0) {
			kmem_free(buf, size);
			return (err);
		}
		err = dmu_object_info(ra->os, drro->drr_object, &doi);
		/*
		 * See receive_read_prefetch for an explanation why we're
		 * storing this object in the ignore_obj_list.
		 */
		if (err == ENOENT ||
		    (err == 0 && doi.doi_data_block_size != drro->drr_blksz)) {
			objlist_insert(&ra->ignore_objlist, drro->drr_object);
			err = 0;
		}
		return (err);
	}
	case DRR_FREEOBJECTS:
	{
		err = receive_read_payload_and_next_header(ra, 0, NULL);
		return (err);
	}
	case DRR_WRITE:
	{
		struct drr_write *drrw = &ra->rrd->header.drr_u.drr_write;
		arc_buf_t *abuf;
		boolean_t is_meta = DMU_OT_IS_METADATA(drrw->drr_type);

		if (ra->raw) {
			boolean_t byteorder = ZFS_HOST_BYTEORDER ^
			    !!DRR_IS_RAW_BYTESWAPPED(drrw->drr_flags) ^
			    ra->byteswap;

			abuf = arc_loan_raw_buf(dmu_objset_spa(ra->os),
			    drrw->drr_object, byteorder, drrw->drr_salt,
			    drrw->drr_iv, drrw->drr_mac, drrw->drr_type,
			    drrw->drr_compressed_size, drrw->drr_logical_size,
			    drrw->drr_compressiontype);
		} else if (DRR_WRITE_COMPRESSED(drrw)) {
			ASSERT3U(drrw->drr_compressed_size, >, 0);
			ASSERT3U(drrw->drr_logical_size, >=,
			    drrw->drr_compressed_size);
			ASSERT(!is_meta);
			abuf = arc_loan_compressed_buf(
			    dmu_objset_spa(ra->os),
			    drrw->drr_compressed_size, drrw->drr_logical_size,
			    drrw->drr_compressiontype);
		} else {
			abuf = arc_loan_buf(dmu_objset_spa(ra->os),
			    is_meta, drrw->drr_logical_size);
		}

		err = receive_read_payload_and_next_header(ra,
		    DRR_WRITE_PAYLOAD_SIZE(drrw), abuf->b_data);
		if (err != 0) {
			dmu_return_arcbuf(abuf);
			return (err);
		}
		ra->rrd->arc_buf = abuf;
		receive_read_prefetch(ra, drrw->drr_object, drrw->drr_offset,
		    drrw->drr_logical_size);
		return (err);
	}
	case DRR_WRITE_BYREF:
	{
		struct drr_write_byref *drrwb =
		    &ra->rrd->header.drr_u.drr_write_byref;
		err = receive_read_payload_and_next_header(ra, 0, NULL);
		receive_read_prefetch(ra, drrwb->drr_object, drrwb->drr_offset,
		    drrwb->drr_length);
		return (err);
	}
	case DRR_WRITE_EMBEDDED:
	{
		struct drr_write_embedded *drrwe =
		    &ra->rrd->header.drr_u.drr_write_embedded;
		uint32_t size = P2ROUNDUP(drrwe->drr_psize, 8);
		void *buf = kmem_zalloc(size, KM_SLEEP);

		err = receive_read_payload_and_next_header(ra, size, buf);
		if (err != 0) {
			kmem_free(buf, size);
			return (err);
		}

		receive_read_prefetch(ra, drrwe->drr_object, drrwe->drr_offset,
		    drrwe->drr_length);
		return (err);
	}
	case DRR_FREE:
	{
		/*
		 * It might be beneficial to prefetch indirect blocks here, but
		 * we don't really have the data to decide for sure.
		 */
		err = receive_read_payload_and_next_header(ra, 0, NULL);
		return (err);
	}
	case DRR_END:
	{
		struct drr_end *drre = &ra->rrd->header.drr_u.drr_end;
		if (!ZIO_CHECKSUM_EQUAL(ra->prev_cksum, drre->drr_checksum))
			return (SET_ERROR(ECKSUM));
		return (0);
	}
	case DRR_SPILL:
	{
		struct drr_spill *drrs = &ra->rrd->header.drr_u.drr_spill;
		arc_buf_t *abuf;
		int len = DRR_SPILL_PAYLOAD_SIZE(drrs);

		/* DRR_SPILL records are either raw or uncompressed */
		if (ra->raw) {
			boolean_t byteorder = ZFS_HOST_BYTEORDER ^
			    !!DRR_IS_RAW_BYTESWAPPED(drrs->drr_flags) ^
			    ra->byteswap;

			abuf = arc_loan_raw_buf(dmu_objset_spa(ra->os),
			    drrs->drr_object, byteorder, drrs->drr_salt,
			    drrs->drr_iv, drrs->drr_mac, drrs->drr_type,
			    drrs->drr_compressed_size, drrs->drr_length,
			    drrs->drr_compressiontype);
		} else {
			abuf = arc_loan_buf(dmu_objset_spa(ra->os),
			    DMU_OT_IS_METADATA(drrs->drr_type),
			    drrs->drr_length);
		}

		err = receive_read_payload_and_next_header(ra, len,
		    abuf->b_data);
		if (err != 0) {
			dmu_return_arcbuf(abuf);
			return (err);
		}
		ra->rrd->arc_buf = abuf;
		return (err);
	}
	case DRR_OBJECT_RANGE:
	{
		err = receive_read_payload_and_next_header(ra, 0, NULL);
		return (err);
	}
	default:
		return (SET_ERROR(EINVAL));
	}
}

static void
dprintf_drr(struct receive_record_arg *rrd, int err)
{
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
		    "lsize = %llu cksumtype = %u cksumflags = %u "
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
		    "cksumflags = %u err = %d\n",
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
	default:
		return;
	}
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
		/* if receive_spill() is successful, it consumes the arc_buf */
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
		return (receive_object_range(rwa, drror));
	}
	default:
		return (SET_ERROR(EINVAL));
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
resume_check(struct receive_arg *ra, nvlist_t *begin_nvl)
{
	uint64_t val;
	objset_t *mos = dmu_objset_pool(ra->os)->dp_meta_objset;
	uint64_t dsobj = dmu_objset_id(ra->os);
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
dmu_recv_stream(dmu_recv_cookie_t *drc, vnode_t *vp, offset_t *voffp,
    int cleanup_fd, uint64_t *action_handlep)
{
	int err = 0;
	struct receive_arg *ra;
	struct receive_writer_arg *rwa;
	int featureflags;
	uint32_t payloadlen;
	void *payload;
	nvlist_t *begin_nvl = NULL;

	ra = kmem_zalloc(sizeof (*ra), KM_SLEEP);
	rwa = kmem_zalloc(sizeof (*rwa), KM_SLEEP);

	ra->byteswap = drc->drc_byteswap;
	ra->raw = drc->drc_raw;
	ra->cksum = drc->drc_cksum;
	ra->vp = vp;
	ra->voff = *voffp;

	if (dsl_dataset_is_zapified(drc->drc_ds)) {
		(void) zap_lookup(drc->drc_ds->ds_dir->dd_pool->dp_meta_objset,
		    drc->drc_ds->ds_object, DS_FIELD_RESUME_BYTES,
		    sizeof (ra->bytes_read), 1, &ra->bytes_read);
	}

	objlist_create(&ra->ignore_objlist);

	/* these were verified in dmu_recv_begin */
	ASSERT3U(DMU_GET_STREAM_HDRTYPE(drc->drc_drrb->drr_versioninfo), ==,
	    DMU_SUBSTREAM);
	ASSERT3U(drc->drc_drrb->drr_type, <, DMU_OST_NUMTYPES);

	/*
	 * Open the objset we are modifying.
	 */
	VERIFY0(dmu_objset_from_ds(drc->drc_ds, &ra->os));

	ASSERT(dsl_dataset_phys(drc->drc_ds)->ds_flags & DS_FLAG_INCONSISTENT);

	featureflags = DMU_GET_FEATUREFLAGS(drc->drc_drrb->drr_versioninfo);
	ra->featureflags = featureflags;

	/* embedded data is incompatible with encrypted datasets */
	if (ra->os->os_encrypted &&
	    (featureflags & DMU_BACKUP_FEATURE_EMBED_DATA)) {
		err = SET_ERROR(EINVAL);
		goto out;
	}

	/* if this stream is dedup'ed, set up the avl tree for guid mapping */
	if (featureflags & DMU_BACKUP_FEATURE_DEDUP) {
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

	payloadlen = drc->drc_drr_begin->drr_payloadlen;
	payload = NULL;
	if (payloadlen != 0)
		payload = kmem_alloc(payloadlen, KM_SLEEP);

	err = receive_read_payload_and_next_header(ra, payloadlen, payload);
	if (err != 0) {
		if (payloadlen != 0)
			kmem_free(payload, payloadlen);
		goto out;
	}
	if (payloadlen != 0) {
		err = nvlist_unpack(payload, payloadlen, &begin_nvl, KM_SLEEP);
		kmem_free(payload, payloadlen);
		if (err != 0)
			goto out;
	}

	/* handle DSL encryption key payload */
	if (featureflags & DMU_BACKUP_FEATURE_RAW) {
		nvlist_t *keynvl = NULL;

		ASSERT(ra->os->os_encrypted);
		ASSERT(drc->drc_raw);

		err = nvlist_lookup_nvlist(begin_nvl, "crypt_keydata", &keynvl);
		if (err != 0)
			goto out;

		err = dsl_crypto_recv_key(spa_name(ra->os->os_spa),
		    drc->drc_ds->ds_object, drc->drc_drrb->drr_type,
		    keynvl);
		if (err != 0)
			goto out;
	}

	if (featureflags & DMU_BACKUP_FEATURE_RESUMING) {
		err = resume_check(ra, begin_nvl);
		if (err != 0)
			goto out;
	}

	(void) bqueue_init(&rwa->q, zfs_recv_queue_length,
	    offsetof(struct receive_record_arg, node));
	cv_init(&rwa->cv, NULL, CV_DEFAULT, NULL);
	mutex_init(&rwa->mutex, NULL, MUTEX_DEFAULT, NULL);
	rwa->os = ra->os;
	rwa->byteswap = drc->drc_byteswap;
	rwa->resumable = drc->drc_resumable;
	rwa->raw = drc->drc_raw;

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
	 * first loop and ra->rrd was never allocated, or it's later and ra->rrd
	 * has been handed off to the writer thread who will free it.  Finally,
	 * if receive_read_record fails or we're at the end of the stream, then
	 * we free ra->rrd and exit.
	 */
	while (rwa->err == 0) {
		if (issig(JUSTLOOKING) && issig(FORREAL)) {
			err = SET_ERROR(EINTR);
			break;
		}

		ASSERT3P(ra->rrd, ==, NULL);
		ra->rrd = ra->next_rrd;
		ra->next_rrd = NULL;
		/* Allocates and loads header into ra->next_rrd */
		err = receive_read_record(ra);

		if (ra->rrd->header.drr_type == DRR_END || err != 0) {
			kmem_free(ra->rrd, sizeof (*ra->rrd));
			ra->rrd = NULL;
			break;
		}

		bqueue_enqueue(&rwa->q, ra->rrd,
		    sizeof (struct receive_record_arg) + ra->rrd->payload_size);
		ra->rrd = NULL;
	}
	if (ra->next_rrd == NULL)
		ra->next_rrd = kmem_zalloc(sizeof (*ra->next_rrd), KM_SLEEP);
	ra->next_rrd->eos_marker = B_TRUE;
	bqueue_enqueue(&rwa->q, ra->next_rrd, 1);

	mutex_enter(&rwa->mutex);
	while (!rwa->done) {
		cv_wait(&rwa->cv, &rwa->mutex);
	}
	mutex_exit(&rwa->mutex);

	cv_destroy(&rwa->cv);
	mutex_destroy(&rwa->mutex);
	bqueue_destroy(&rwa->q);
	if (err == 0)
		err = rwa->err;

out:
	nvlist_free(begin_nvl);
	if ((featureflags & DMU_BACKUP_FEATURE_DEDUP) && (cleanup_fd != -1))
		zfs_onexit_fd_rele(cleanup_fd);

	if (err != 0) {
		/*
		 * Clean up references. If receive is not resumable,
		 * destroy what we created, so we don't leave it in
		 * the inconsistent state.
		 */
		dmu_recv_cleanup_ds(drc);
	}

	*voffp = ra->voff;
	objlist_destroy(&ra->ignore_objlist);
	kmem_free(ra, sizeof (*ra));
	kmem_free(rwa, sizeof (*rwa));
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
		}
		drc->drc_newsnapobj =
		    dsl_dataset_phys(drc->drc_ds)->ds_prev_snap_obj;
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
	ds_hold_flags_t dsflags = (raw) ? 0 : DS_HOLD_FLAG_DECRYPT;
	int err;

	ASSERT(guid_map != NULL);

	err = dsl_pool_hold(name, FTAG, &dp);
	if (err != 0)
		return (err);
	gmep = kmem_alloc(sizeof (*gmep), KM_SLEEP);
	err = dsl_dataset_hold_obj_flags(dp, snapobj, dsflags, gmep, &snapds);
	if (err == 0) {
		gmep->guid = dsl_dataset_phys(snapds)->ds_guid;
		gmep->raw = raw;
		gmep->gme_ds = snapds;
		avl_add(guid_map, gmep);
		dsl_dataset_long_hold(snapds, gmep);
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

#if defined(_KERNEL)
module_param(zfs_send_corrupt_data, int, 0644);
MODULE_PARM_DESC(zfs_send_corrupt_data, "Allow sending corrupt data");
#endif
