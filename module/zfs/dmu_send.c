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
 * Copyright (c) 2011 by Delphix. All rights reserved.
 * Copyright 2011 Nexenta Systems, Inc. All rights reserved.
 * Copyright (c) 2011 by Delphix. All rights reserved.
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
#include <sys/zfs_ioctl.h>
#include <sys/zap.h>
#include <sys/zio_checksum.h>
#include <sys/zfs_znode.h>
#include <zfs_fletcher.h>
#include <sys/avl.h>
#include <sys/ddt.h>
#include <sys/zfs_onexit.h>

/* Set this tunable to TRUE to replace corrupt data with 0x2f5baddb10c */
int zfs_send_corrupt_data = B_FALSE;

static char *dmu_recv_tag = "dmu_recv_tag";

/*
 * The list of data whose inclusion in a send stream can be pending from
 * one call to backup_cb to another.  Multiple calls to dump_free() and
 * dump_freeobjects() can be aggregated into a single DRR_FREE or
 * DRR_FREEOBJECTS replay record.
 */
typedef enum {
	PENDING_NONE,
	PENDING_FREE,
	PENDING_FREEOBJECTS
} pendop_t;

struct backuparg {
	dmu_replay_record_t *drr;
	vnode_t *vp;
	offset_t *off;
	objset_t *os;
	zio_cksum_t zc;
	uint64_t toguid;
	int err;
	pendop_t pending_op;
};

static int
dump_bytes(struct backuparg *ba, void *buf, int len)
{
	ssize_t resid; /* have to get resid to get detailed errno */
	ASSERT3U(len % 8, ==, 0);

	fletcher_4_incremental_native(buf, len, &ba->zc);
	ba->err = vn_rdwr(UIO_WRITE, ba->vp,
	    (caddr_t)buf, len,
	    0, UIO_SYSSPACE, FAPPEND, RLIM64_INFINITY, CRED(), &resid);
	*ba->off += len;
	return (ba->err);
}

static int
dump_free(struct backuparg *ba, uint64_t object, uint64_t offset,
    uint64_t length)
{
	struct drr_free *drrf = &(ba->drr->drr_u.drr_free);

	if (length != -1ULL && offset + length < offset)
		length = -1ULL;

	/*
	 * If there is a pending op, but it's not PENDING_FREE, push it out,
	 * since free block aggregation can only be done for blocks of the
	 * same type (i.e., DRR_FREE records can only be aggregated with
	 * other DRR_FREE records.  DRR_FREEOBJECTS records can only be
	 * aggregated with other DRR_FREEOBJECTS records.
	 */
	if (ba->pending_op != PENDING_NONE && ba->pending_op != PENDING_FREE) {
		if (dump_bytes(ba, ba->drr, sizeof (dmu_replay_record_t)) != 0)
			return (EINTR);
		ba->pending_op = PENDING_NONE;
	}

	if (ba->pending_op == PENDING_FREE) {
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
			if (dump_bytes(ba, ba->drr,
			    sizeof (dmu_replay_record_t)) != 0)
				return (EINTR);
			ba->pending_op = PENDING_NONE;
		}
	}
	/* create a FREE record and make it pending */
	bzero(ba->drr, sizeof (dmu_replay_record_t));
	ba->drr->drr_type = DRR_FREE;
	drrf->drr_object = object;
	drrf->drr_offset = offset;
	drrf->drr_length = length;
	drrf->drr_toguid = ba->toguid;
	if (length == -1ULL) {
		if (dump_bytes(ba, ba->drr, sizeof (dmu_replay_record_t)) != 0)
			return (EINTR);
	} else {
		ba->pending_op = PENDING_FREE;
	}

	return (0);
}

static int
dump_data(struct backuparg *ba, dmu_object_type_t type,
    uint64_t object, uint64_t offset, int blksz, const blkptr_t *bp, void *data)
{
	struct drr_write *drrw = &(ba->drr->drr_u.drr_write);


	/*
	 * If there is any kind of pending aggregation (currently either
	 * a grouping of free objects or free blocks), push it out to
	 * the stream, since aggregation can't be done across operations
	 * of different types.
	 */
	if (ba->pending_op != PENDING_NONE) {
		if (dump_bytes(ba, ba->drr, sizeof (dmu_replay_record_t)) != 0)
			return (EINTR);
		ba->pending_op = PENDING_NONE;
	}
	/* write a DATA record */
	bzero(ba->drr, sizeof (dmu_replay_record_t));
	ba->drr->drr_type = DRR_WRITE;
	drrw->drr_object = object;
	drrw->drr_type = type;
	drrw->drr_offset = offset;
	drrw->drr_length = blksz;
	drrw->drr_toguid = ba->toguid;
	drrw->drr_checksumtype = BP_GET_CHECKSUM(bp);
	if (zio_checksum_table[drrw->drr_checksumtype].ci_dedup)
		drrw->drr_checksumflags |= DRR_CHECKSUM_DEDUP;
	DDK_SET_LSIZE(&drrw->drr_key, BP_GET_LSIZE(bp));
	DDK_SET_PSIZE(&drrw->drr_key, BP_GET_PSIZE(bp));
	DDK_SET_COMPRESS(&drrw->drr_key, BP_GET_COMPRESS(bp));
	drrw->drr_key.ddk_cksum = bp->blk_cksum;

	if (dump_bytes(ba, ba->drr, sizeof (dmu_replay_record_t)) != 0)
		return (EINTR);
	if (dump_bytes(ba, data, blksz) != 0)
		return (EINTR);
	return (0);
}

static int
dump_spill(struct backuparg *ba, uint64_t object, int blksz, void *data)
{
	struct drr_spill *drrs = &(ba->drr->drr_u.drr_spill);

	if (ba->pending_op != PENDING_NONE) {
		if (dump_bytes(ba, ba->drr, sizeof (dmu_replay_record_t)) != 0)
			return (EINTR);
		ba->pending_op = PENDING_NONE;
	}

	/* write a SPILL record */
	bzero(ba->drr, sizeof (dmu_replay_record_t));
	ba->drr->drr_type = DRR_SPILL;
	drrs->drr_object = object;
	drrs->drr_length = blksz;
	drrs->drr_toguid = ba->toguid;

	if (dump_bytes(ba, ba->drr, sizeof (dmu_replay_record_t)))
		return (EINTR);
	if (dump_bytes(ba, data, blksz))
		return (EINTR);
	return (0);
}

static int
dump_freeobjects(struct backuparg *ba, uint64_t firstobj, uint64_t numobjs)
{
	struct drr_freeobjects *drrfo = &(ba->drr->drr_u.drr_freeobjects);

	/*
	 * If there is a pending op, but it's not PENDING_FREEOBJECTS,
	 * push it out, since free block aggregation can only be done for
	 * blocks of the same type (i.e., DRR_FREE records can only be
	 * aggregated with other DRR_FREE records.  DRR_FREEOBJECTS records
	 * can only be aggregated with other DRR_FREEOBJECTS records.
	 */
	if (ba->pending_op != PENDING_NONE &&
	    ba->pending_op != PENDING_FREEOBJECTS) {
		if (dump_bytes(ba, ba->drr, sizeof (dmu_replay_record_t)) != 0)
			return (EINTR);
		ba->pending_op = PENDING_NONE;
	}
	if (ba->pending_op == PENDING_FREEOBJECTS) {
		/*
		 * See whether this free object array can be aggregated
		 * with pending one
		 */
		if (drrfo->drr_firstobj + drrfo->drr_numobjs == firstobj) {
			drrfo->drr_numobjs += numobjs;
			return (0);
		} else {
			/* can't be aggregated.  Push out pending record */
			if (dump_bytes(ba, ba->drr,
			    sizeof (dmu_replay_record_t)) != 0)
				return (EINTR);
			ba->pending_op = PENDING_NONE;
		}
	}

	/* write a FREEOBJECTS record */
	bzero(ba->drr, sizeof (dmu_replay_record_t));
	ba->drr->drr_type = DRR_FREEOBJECTS;
	drrfo->drr_firstobj = firstobj;
	drrfo->drr_numobjs = numobjs;
	drrfo->drr_toguid = ba->toguid;

	ba->pending_op = PENDING_FREEOBJECTS;

	return (0);
}

static int
dump_dnode(struct backuparg *ba, uint64_t object, dnode_phys_t *dnp)
{
	struct drr_object *drro = &(ba->drr->drr_u.drr_object);

	if (dnp == NULL || dnp->dn_type == DMU_OT_NONE)
		return (dump_freeobjects(ba, object, 1));

	if (ba->pending_op != PENDING_NONE) {
		if (dump_bytes(ba, ba->drr, sizeof (dmu_replay_record_t)) != 0)
			return (EINTR);
		ba->pending_op = PENDING_NONE;
	}

	/* write an OBJECT record */
	bzero(ba->drr, sizeof (dmu_replay_record_t));
	ba->drr->drr_type = DRR_OBJECT;
	drro->drr_object = object;
	drro->drr_type = dnp->dn_type;
	drro->drr_bonustype = dnp->dn_bonustype;
	drro->drr_blksz = dnp->dn_datablkszsec << SPA_MINBLOCKSHIFT;
	drro->drr_bonuslen = dnp->dn_bonuslen;
	drro->drr_checksumtype = dnp->dn_checksum;
	drro->drr_compress = dnp->dn_compress;
	drro->drr_toguid = ba->toguid;

	if (dump_bytes(ba, ba->drr, sizeof (dmu_replay_record_t)) != 0)
		return (EINTR);

	if (dump_bytes(ba, DN_BONUS(dnp), P2ROUNDUP(dnp->dn_bonuslen, 8)) != 0)
		return (EINTR);

	/* free anything past the end of the file */
	if (dump_free(ba, object, (dnp->dn_maxblkid + 1) *
	    (dnp->dn_datablkszsec << SPA_MINBLOCKSHIFT), -1ULL))
		return (EINTR);
	if (ba->err)
		return (EINTR);
	return (0);
}

#define	BP_SPAN(dnp, level) \
	(((uint64_t)dnp->dn_datablkszsec) << (SPA_MINBLOCKSHIFT + \
	(level) * (dnp->dn_indblkshift - SPA_BLKPTRSHIFT)))

/* ARGSUSED */
static int
backup_cb(spa_t *spa, zilog_t *zilog, const blkptr_t *bp, arc_buf_t *pbuf,
    const zbookmark_t *zb, const dnode_phys_t *dnp, void *arg)
{
	struct backuparg *ba = arg;
	dmu_object_type_t type = bp ? BP_GET_TYPE(bp) : DMU_OT_NONE;
	int err = 0;

	if (issig(JUSTLOOKING) && issig(FORREAL))
		return (EINTR);

	if (zb->zb_object != DMU_META_DNODE_OBJECT &&
	    DMU_OBJECT_IS_SPECIAL(zb->zb_object)) {
		return (0);
	} else if (bp == NULL && zb->zb_object == DMU_META_DNODE_OBJECT) {
		uint64_t span = BP_SPAN(dnp, zb->zb_level);
		uint64_t dnobj = (zb->zb_blkid * span) >> DNODE_SHIFT;
		err = dump_freeobjects(ba, dnobj, span >> DNODE_SHIFT);
	} else if (bp == NULL) {
		uint64_t span = BP_SPAN(dnp, zb->zb_level);
		err = dump_free(ba, zb->zb_object, zb->zb_blkid * span, span);
	} else if (zb->zb_level > 0 || type == DMU_OT_OBJSET) {
		return (0);
	} else if (type == DMU_OT_DNODE) {
		dnode_phys_t *blk;
		int i;
		int blksz = BP_GET_LSIZE(bp);
		uint32_t aflags = ARC_WAIT;
		arc_buf_t *abuf;

		if (dsl_read(NULL, spa, bp, pbuf,
		    arc_getbuf_func, &abuf, ZIO_PRIORITY_ASYNC_READ,
		    ZIO_FLAG_CANFAIL, &aflags, zb) != 0)
			return (EIO);

		blk = abuf->b_data;
		for (i = 0; i < blksz >> DNODE_SHIFT; i++) {
			uint64_t dnobj = (zb->zb_blkid <<
			    (DNODE_BLOCK_SHIFT - DNODE_SHIFT)) + i;
			err = dump_dnode(ba, dnobj, blk+i);
			if (err)
				break;
		}
		(void) arc_buf_remove_ref(abuf, &abuf);
	} else if (type == DMU_OT_SA) {
		uint32_t aflags = ARC_WAIT;
		arc_buf_t *abuf;
		int blksz = BP_GET_LSIZE(bp);

		if (arc_read_nolock(NULL, spa, bp,
		    arc_getbuf_func, &abuf, ZIO_PRIORITY_ASYNC_READ,
		    ZIO_FLAG_CANFAIL, &aflags, zb) != 0)
			return (EIO);

		err = dump_spill(ba, zb->zb_object, blksz, abuf->b_data);
		(void) arc_buf_remove_ref(abuf, &abuf);
	} else { /* it's a level-0 block of a regular object */
		uint32_t aflags = ARC_WAIT;
		arc_buf_t *abuf;
		int blksz = BP_GET_LSIZE(bp);

		if (dsl_read(NULL, spa, bp, pbuf,
		    arc_getbuf_func, &abuf, ZIO_PRIORITY_ASYNC_READ,
		    ZIO_FLAG_CANFAIL, &aflags, zb) != 0) {
			if (zfs_send_corrupt_data) {
				uint64_t *ptr;
				/* Send a block filled with 0x"zfs badd bloc" */
				abuf = arc_buf_alloc(spa, blksz, &abuf,
				    ARC_BUFC_DATA);
				for (ptr = abuf->b_data;
				    (char *)ptr < (char *)abuf->b_data + blksz;
				    ptr++)
					*ptr = 0x2f5baddb10c;
			} else {
				return (EIO);
			}
		}

		err = dump_data(ba, type, zb->zb_object, zb->zb_blkid * blksz,
		    blksz, bp, abuf->b_data);
		(void) arc_buf_remove_ref(abuf, &abuf);
	}

	ASSERT(err == 0 || err == EINTR);
	return (err);
}

int
dmu_sendbackup(objset_t *tosnap, objset_t *fromsnap, boolean_t fromorigin,
    vnode_t *vp, offset_t *off)
{
	dsl_dataset_t *ds = tosnap->os_dsl_dataset;
	dsl_dataset_t *fromds = fromsnap ? fromsnap->os_dsl_dataset : NULL;
	dmu_replay_record_t *drr;
	struct backuparg ba;
	int err;
	uint64_t fromtxg = 0;

	/* tosnap must be a snapshot */
	if (ds->ds_phys->ds_next_snap_obj == 0)
		return (EINVAL);

	/* fromsnap must be an earlier snapshot from the same fs as tosnap */
	if (fromds && (ds->ds_dir != fromds->ds_dir ||
	    fromds->ds_phys->ds_creation_txg >= ds->ds_phys->ds_creation_txg))
		return (EXDEV);

	if (fromorigin) {
		dsl_pool_t *dp = ds->ds_dir->dd_pool;

		if (fromsnap)
			return (EINVAL);

		if (dsl_dir_is_clone(ds->ds_dir)) {
			rw_enter(&dp->dp_config_rwlock, RW_READER);
			err = dsl_dataset_hold_obj(dp,
			    ds->ds_dir->dd_phys->dd_origin_obj, FTAG, &fromds);
			rw_exit(&dp->dp_config_rwlock);
			if (err)
				return (err);
		} else {
			fromorigin = B_FALSE;
		}
	}


	drr = kmem_zalloc(sizeof (dmu_replay_record_t), KM_SLEEP);
	drr->drr_type = DRR_BEGIN;
	drr->drr_u.drr_begin.drr_magic = DMU_BACKUP_MAGIC;
	DMU_SET_STREAM_HDRTYPE(drr->drr_u.drr_begin.drr_versioninfo,
	    DMU_SUBSTREAM);

#ifdef _KERNEL
	if (dmu_objset_type(tosnap) == DMU_OST_ZFS) {
		uint64_t version;
		if (zfs_get_zplprop(tosnap, ZFS_PROP_VERSION, &version) != 0)
			return (EINVAL);
		if (version == ZPL_VERSION_SA) {
			DMU_SET_FEATUREFLAGS(
			    drr->drr_u.drr_begin.drr_versioninfo,
			    DMU_BACKUP_FEATURE_SA_SPILL);
		}
	}
#endif

	drr->drr_u.drr_begin.drr_creation_time =
	    ds->ds_phys->ds_creation_time;
	drr->drr_u.drr_begin.drr_type = tosnap->os_phys->os_type;
	if (fromorigin)
		drr->drr_u.drr_begin.drr_flags |= DRR_FLAG_CLONE;
	drr->drr_u.drr_begin.drr_toguid = ds->ds_phys->ds_guid;
	if (ds->ds_phys->ds_flags & DS_FLAG_CI_DATASET)
		drr->drr_u.drr_begin.drr_flags |= DRR_FLAG_CI_DATA;

	if (fromds)
		drr->drr_u.drr_begin.drr_fromguid = fromds->ds_phys->ds_guid;
	dsl_dataset_name(ds, drr->drr_u.drr_begin.drr_toname);

	if (fromds)
		fromtxg = fromds->ds_phys->ds_creation_txg;
	if (fromorigin)
		dsl_dataset_rele(fromds, FTAG);

	ba.drr = drr;
	ba.vp = vp;
	ba.os = tosnap;
	ba.off = off;
	ba.toguid = ds->ds_phys->ds_guid;
	ZIO_SET_CHECKSUM(&ba.zc, 0, 0, 0, 0);
	ba.pending_op = PENDING_NONE;

	if (dump_bytes(&ba, drr, sizeof (dmu_replay_record_t)) != 0) {
		kmem_free(drr, sizeof (dmu_replay_record_t));
		return (ba.err);
	}

	err = traverse_dataset(ds, fromtxg, TRAVERSE_PRE | TRAVERSE_PREFETCH,
	    backup_cb, &ba);

	if (ba.pending_op != PENDING_NONE)
		if (dump_bytes(&ba, drr, sizeof (dmu_replay_record_t)) != 0)
			err = EINTR;

	if (err) {
		if (err == EINTR && ba.err)
			err = ba.err;
		kmem_free(drr, sizeof (dmu_replay_record_t));
		return (err);
	}

	bzero(drr, sizeof (dmu_replay_record_t));
	drr->drr_type = DRR_END;
	drr->drr_u.drr_end.drr_checksum = ba.zc;
	drr->drr_u.drr_end.drr_toguid = ba.toguid;

	if (dump_bytes(&ba, drr, sizeof (dmu_replay_record_t)) != 0) {
		kmem_free(drr, sizeof (dmu_replay_record_t));
		return (ba.err);
	}

	kmem_free(drr, sizeof (dmu_replay_record_t));

	return (0);
}

int
dmu_send_estimate(objset_t *tosnap, objset_t *fromsnap, boolean_t fromorigin,
    uint64_t *sizep)
{
	dsl_dataset_t *ds = tosnap->os_dsl_dataset;
	dsl_dataset_t *fromds = fromsnap ? fromsnap->os_dsl_dataset : NULL;
	dsl_pool_t *dp = ds->ds_dir->dd_pool;
	int err;
	uint64_t size, recordsize;

	/* tosnap must be a snapshot */
	if (ds->ds_phys->ds_next_snap_obj == 0)
		return (EINVAL);

	/* fromsnap must be an earlier snapshot from the same fs as tosnap */
	if (fromds && (ds->ds_dir != fromds->ds_dir ||
	    fromds->ds_phys->ds_creation_txg >= ds->ds_phys->ds_creation_txg))
		return (EXDEV);

	if (fromorigin) {
		if (fromsnap)
			return (EINVAL);

		if (dsl_dir_is_clone(ds->ds_dir)) {
			rw_enter(&dp->dp_config_rwlock, RW_READER);
			err = dsl_dataset_hold_obj(dp,
			    ds->ds_dir->dd_phys->dd_origin_obj, FTAG, &fromds);
			rw_exit(&dp->dp_config_rwlock);
			if (err)
				return (err);
		} else {
			fromorigin = B_FALSE;
		}
	}

	/* Get uncompressed size estimate of changed data. */
	if (fromds == NULL) {
		size = ds->ds_phys->ds_uncompressed_bytes;
	} else {
		uint64_t used, comp;
		err = dsl_dataset_space_written(fromds, ds,
		    &used, &comp, &size);
		if (fromorigin)
			dsl_dataset_rele(fromds, FTAG);
		if (err)
			return (err);
	}

	/*
	 * Assume that space (both on-disk and in-stream) is dominated by
	 * data.  We will adjust for indirect blocks and the copies property,
	 * but ignore per-object space used (eg, dnodes and DRR_OBJECT records).
	 */

	/*
	 * Subtract out approximate space used by indirect blocks.
	 * Assume most space is used by data blocks (non-indirect, non-dnode).
	 * Assume all blocks are recordsize.  Assume ditto blocks and
	 * internal fragmentation counter out compression.
	 *
	 * Therefore, space used by indirect blocks is sizeof(blkptr_t) per
	 * block, which we observe in practice.
	 */
	rw_enter(&dp->dp_config_rwlock, RW_READER);
	err = dsl_prop_get_ds(ds, "recordsize",
	    sizeof (recordsize), 1, &recordsize, NULL);
	rw_exit(&dp->dp_config_rwlock);
	if (err)
		return (err);
	size -= size / recordsize * sizeof (blkptr_t);

	/* Add in the space for the record associated with each block. */
	size += size / recordsize * sizeof (dmu_replay_record_t);

	*sizep = size;

	return (0);
}

struct recvbeginsyncarg {
	const char *tofs;
	const char *tosnap;
	dsl_dataset_t *origin;
	uint64_t fromguid;
	dmu_objset_type_t type;
	void *tag;
	boolean_t force;
	uint64_t dsflags;
	char clonelastname[MAXNAMELEN];
	dsl_dataset_t *ds; /* the ds to recv into; returned from the syncfunc */
	cred_t *cr;
};

/* ARGSUSED */
static int
recv_new_check(void *arg1, void *arg2, dmu_tx_t *tx)
{
	dsl_dir_t *dd = arg1;
	struct recvbeginsyncarg *rbsa = arg2;
	objset_t *mos = dd->dd_pool->dp_meta_objset;
	uint64_t val;
	int err;

	err = zap_lookup(mos, dd->dd_phys->dd_child_dir_zapobj,
	    strrchr(rbsa->tofs, '/') + 1, sizeof (uint64_t), 1, &val);

	if (err != ENOENT)
		return (err ? err : EEXIST);

	if (rbsa->origin) {
		/* make sure it's a snap in the same pool */
		if (rbsa->origin->ds_dir->dd_pool != dd->dd_pool)
			return (EXDEV);
		if (!dsl_dataset_is_snapshot(rbsa->origin))
			return (EINVAL);
		if (rbsa->origin->ds_phys->ds_guid != rbsa->fromguid)
			return (ENODEV);
	}

	return (0);
}

static void
recv_new_sync(void *arg1, void *arg2, dmu_tx_t *tx)
{
	dsl_dir_t *dd = arg1;
	struct recvbeginsyncarg *rbsa = arg2;
	uint64_t flags = DS_FLAG_INCONSISTENT | rbsa->dsflags;
	uint64_t dsobj;

	/* Create and open new dataset. */
	dsobj = dsl_dataset_create_sync(dd, strrchr(rbsa->tofs, '/') + 1,
	    rbsa->origin, flags, rbsa->cr, tx);
	VERIFY(0 == dsl_dataset_own_obj(dd->dd_pool, dsobj,
	    B_TRUE, dmu_recv_tag, &rbsa->ds));

	if (rbsa->origin == NULL) {
		(void) dmu_objset_create_impl(dd->dd_pool->dp_spa,
		    rbsa->ds, &rbsa->ds->ds_phys->ds_bp, rbsa->type, tx);
	}

	spa_history_log_internal(LOG_DS_REPLAY_FULL_SYNC,
	    dd->dd_pool->dp_spa, tx, "dataset = %lld", dsobj);
}

/* ARGSUSED */
static int
recv_existing_check(void *arg1, void *arg2, dmu_tx_t *tx)
{
	dsl_dataset_t *ds = arg1;
	struct recvbeginsyncarg *rbsa = arg2;
	int err;
	uint64_t val;

	/* must not have any changes since most recent snapshot */
	if (!rbsa->force && dsl_dataset_modified_since_lastsnap(ds))
		return (ETXTBSY);

	/* new snapshot name must not exist */
	err = zap_lookup(ds->ds_dir->dd_pool->dp_meta_objset,
	    ds->ds_phys->ds_snapnames_zapobj, rbsa->tosnap, 8, 1, &val);
	if (err == 0)
		return (EEXIST);
	if (err != ENOENT)
		return (err);

	if (rbsa->fromguid) {
		/* if incremental, most recent snapshot must match fromguid */
		if (ds->ds_prev == NULL)
			return (ENODEV);

		/*
		 * most recent snapshot must match fromguid, or there are no
		 * changes since the fromguid one
		 */
		if (ds->ds_prev->ds_phys->ds_guid != rbsa->fromguid) {
			uint64_t birth = ds->ds_prev->ds_phys->ds_bp.blk_birth;
			uint64_t obj = ds->ds_prev->ds_phys->ds_prev_snap_obj;
			while (obj != 0) {
				dsl_dataset_t *snap;
				err = dsl_dataset_hold_obj(ds->ds_dir->dd_pool,
				    obj, FTAG, &snap);
				if (err)
					return (ENODEV);
				if (snap->ds_phys->ds_creation_txg < birth) {
					dsl_dataset_rele(snap, FTAG);
					return (ENODEV);
				}
				if (snap->ds_phys->ds_guid == rbsa->fromguid) {
					dsl_dataset_rele(snap, FTAG);
					break; /* it's ok */
				}
				obj = snap->ds_phys->ds_prev_snap_obj;
				dsl_dataset_rele(snap, FTAG);
			}
			if (obj == 0)
				return (ENODEV);
		}
	} else {
		/* if full, most recent snapshot must be $ORIGIN */
		if (ds->ds_phys->ds_prev_snap_txg >= TXG_INITIAL)
			return (ENODEV);
	}

	/* temporary clone name must not exist */
	err = zap_lookup(ds->ds_dir->dd_pool->dp_meta_objset,
	    ds->ds_dir->dd_phys->dd_child_dir_zapobj,
	    rbsa->clonelastname, 8, 1, &val);
	if (err == 0)
		return (EEXIST);
	if (err != ENOENT)
		return (err);

	return (0);
}

/* ARGSUSED */
static void
recv_existing_sync(void *arg1, void *arg2, dmu_tx_t *tx)
{
	dsl_dataset_t *ohds = arg1;
	struct recvbeginsyncarg *rbsa = arg2;
	dsl_pool_t *dp = ohds->ds_dir->dd_pool;
	dsl_dataset_t *cds;
	uint64_t flags = DS_FLAG_INCONSISTENT | rbsa->dsflags;
	uint64_t dsobj;

	/* create and open the temporary clone */
	dsobj = dsl_dataset_create_sync(ohds->ds_dir, rbsa->clonelastname,
	    ohds->ds_prev, flags, rbsa->cr, tx);
	VERIFY(0 == dsl_dataset_own_obj(dp, dsobj, B_TRUE, dmu_recv_tag, &cds));

	/*
	 * If we actually created a non-clone, we need to create the
	 * objset in our new dataset.
	 */
	if (BP_IS_HOLE(dsl_dataset_get_blkptr(cds))) {
		(void) dmu_objset_create_impl(dp->dp_spa,
		    cds, dsl_dataset_get_blkptr(cds), rbsa->type, tx);
	}

	rbsa->ds = cds;

	spa_history_log_internal(LOG_DS_REPLAY_INC_SYNC,
	    dp->dp_spa, tx, "dataset = %lld", dsobj);
}

static boolean_t
dmu_recv_verify_features(dsl_dataset_t *ds, struct drr_begin *drrb)
{
	int featureflags;

	featureflags = DMU_GET_FEATUREFLAGS(drrb->drr_versioninfo);

	/* Verify pool version supports SA if SA_SPILL feature set */
	return ((featureflags & DMU_BACKUP_FEATURE_SA_SPILL) &&
	    (spa_version(dsl_dataset_get_spa(ds)) < SPA_VERSION_SA));
}

/*
 * NB: callers *MUST* call dmu_recv_stream() if dmu_recv_begin()
 * succeeds; otherwise we will leak the holds on the datasets.
 */
int
dmu_recv_begin(char *tofs, char *tosnap, char *top_ds, struct drr_begin *drrb,
    boolean_t force, objset_t *origin, dmu_recv_cookie_t *drc)
{
	int err = 0;
	boolean_t byteswap;
	struct recvbeginsyncarg rbsa = { 0 };
	uint64_t versioninfo;
	int flags;
	dsl_dataset_t *ds;

	if (drrb->drr_magic == DMU_BACKUP_MAGIC)
		byteswap = FALSE;
	else if (drrb->drr_magic == BSWAP_64(DMU_BACKUP_MAGIC))
		byteswap = TRUE;
	else
		return (EINVAL);

	rbsa.tofs = tofs;
	rbsa.tosnap = tosnap;
	rbsa.origin = origin ? origin->os_dsl_dataset : NULL;
	rbsa.fromguid = drrb->drr_fromguid;
	rbsa.type = drrb->drr_type;
	rbsa.tag = FTAG;
	rbsa.dsflags = 0;
	rbsa.cr = CRED();
	versioninfo = drrb->drr_versioninfo;
	flags = drrb->drr_flags;

	if (byteswap) {
		rbsa.type = BSWAP_32(rbsa.type);
		rbsa.fromguid = BSWAP_64(rbsa.fromguid);
		versioninfo = BSWAP_64(versioninfo);
		flags = BSWAP_32(flags);
	}

	if (DMU_GET_STREAM_HDRTYPE(versioninfo) == DMU_COMPOUNDSTREAM ||
	    rbsa.type >= DMU_OST_NUMTYPES ||
	    ((flags & DRR_FLAG_CLONE) && origin == NULL))
		return (EINVAL);

	if (flags & DRR_FLAG_CI_DATA)
		rbsa.dsflags = DS_FLAG_CI_DATASET;

	bzero(drc, sizeof (dmu_recv_cookie_t));
	drc->drc_drrb = drrb;
	drc->drc_tosnap = tosnap;
	drc->drc_top_ds = top_ds;
	drc->drc_force = force;

	/*
	 * Process the begin in syncing context.
	 */

	/* open the dataset we are logically receiving into */
	err = dsl_dataset_hold(tofs, dmu_recv_tag, &ds);
	if (err == 0) {
		if (dmu_recv_verify_features(ds, drrb)) {
			dsl_dataset_rele(ds, dmu_recv_tag);
			return (ENOTSUP);
		}
		/* target fs already exists; recv into temp clone */

		/* Can't recv a clone into an existing fs */
		if (flags & DRR_FLAG_CLONE) {
			dsl_dataset_rele(ds, dmu_recv_tag);
			return (EINVAL);
		}

		/* must not have an incremental recv already in progress */
		if (!mutex_tryenter(&ds->ds_recvlock)) {
			dsl_dataset_rele(ds, dmu_recv_tag);
			return (EBUSY);
		}

		/* tmp clone name is: tofs/%tosnap" */
		(void) snprintf(rbsa.clonelastname, sizeof (rbsa.clonelastname),
		    "%%%s", tosnap);
		rbsa.force = force;
		err = dsl_sync_task_do(ds->ds_dir->dd_pool,
		    recv_existing_check, recv_existing_sync, ds, &rbsa, 5);
		if (err) {
			mutex_exit(&ds->ds_recvlock);
			dsl_dataset_rele(ds, dmu_recv_tag);
			return (err);
		}
		drc->drc_logical_ds = ds;
		drc->drc_real_ds = rbsa.ds;
	} else if (err == ENOENT) {
		/* target fs does not exist; must be a full backup or clone */
		char *cp;

		/*
		 * If it's a non-clone incremental, we are missing the
		 * target fs, so fail the recv.
		 */
		if (rbsa.fromguid && !(flags & DRR_FLAG_CLONE))
			return (ENOENT);

		/* Open the parent of tofs */
		cp = strrchr(tofs, '/');
		*cp = '\0';
		err = dsl_dataset_hold(tofs, FTAG, &ds);
		*cp = '/';
		if (err)
			return (err);

		if (dmu_recv_verify_features(ds, drrb)) {
			dsl_dataset_rele(ds, FTAG);
			return (ENOTSUP);
		}

		err = dsl_sync_task_do(ds->ds_dir->dd_pool,
		    recv_new_check, recv_new_sync, ds->ds_dir, &rbsa, 5);
		dsl_dataset_rele(ds, FTAG);
		if (err)
			return (err);
		drc->drc_logical_ds = drc->drc_real_ds = rbsa.ds;
		drc->drc_newfs = B_TRUE;
	}

	return (err);
}

struct restorearg {
	int err;
	int byteswap;
	vnode_t *vp;
	char *buf;
	uint64_t voff;
	int bufsize; /* amount of memory allocated for buf */
	zio_cksum_t cksum;
	avl_tree_t *guid_to_ds_map;
};

typedef struct guid_map_entry {
	uint64_t	guid;
	dsl_dataset_t	*gme_ds;
	avl_node_t	avlnode;
} guid_map_entry_t;

static int
guid_compare(const void *arg1, const void *arg2)
{
	const guid_map_entry_t *gmep1 = arg1;
	const guid_map_entry_t *gmep2 = arg2;

	if (gmep1->guid < gmep2->guid)
		return (-1);
	else if (gmep1->guid > gmep2->guid)
		return (1);
	return (0);
}

static void
free_guid_map_onexit(void *arg)
{
	avl_tree_t *ca = arg;
	void *cookie = NULL;
	guid_map_entry_t *gmep;

	while ((gmep = avl_destroy_nodes(ca, &cookie)) != NULL) {
		dsl_dataset_rele(gmep->gme_ds, ca);
		kmem_free(gmep, sizeof (guid_map_entry_t));
	}
	avl_destroy(ca);
	kmem_free(ca, sizeof (avl_tree_t));
}

static void *
restore_read(struct restorearg *ra, int len)
{
	void *rv;
	int done = 0;

	/* some things will require 8-byte alignment, so everything must */
	ASSERT3U(len % 8, ==, 0);

	while (done < len) {
		ssize_t resid;

		ra->err = vn_rdwr(UIO_READ, ra->vp,
		    (caddr_t)ra->buf + done, len - done,
		    ra->voff, UIO_SYSSPACE, FAPPEND,
		    RLIM64_INFINITY, CRED(), &resid);

		if (resid == len - done)
			ra->err = EINVAL;
		ra->voff += len - done - resid;
		done = len - resid;
		if (ra->err)
			return (NULL);
	}

	ASSERT3U(done, ==, len);
	rv = ra->buf;
	if (ra->byteswap)
		fletcher_4_incremental_byteswap(rv, len, &ra->cksum);
	else
		fletcher_4_incremental_native(rv, len, &ra->cksum);
	return (rv);
}

noinline static void
backup_byteswap(dmu_replay_record_t *drr)
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
		/* DO64(drr_object.drr_allocation_txg); */
		DO32(drr_object.drr_type);
		DO32(drr_object.drr_bonustype);
		DO32(drr_object.drr_blksz);
		DO32(drr_object.drr_bonuslen);
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
		DO64(drr_write.drr_length);
		DO64(drr_write.drr_toguid);
		DO64(drr_write.drr_key.ddk_cksum.zc_word[0]);
		DO64(drr_write.drr_key.ddk_cksum.zc_word[1]);
		DO64(drr_write.drr_key.ddk_cksum.zc_word[2]);
		DO64(drr_write.drr_key.ddk_cksum.zc_word[3]);
		DO64(drr_write.drr_key.ddk_prop);
		break;
	case DRR_WRITE_BYREF:
		DO64(drr_write_byref.drr_object);
		DO64(drr_write_byref.drr_offset);
		DO64(drr_write_byref.drr_length);
		DO64(drr_write_byref.drr_toguid);
		DO64(drr_write_byref.drr_refguid);
		DO64(drr_write_byref.drr_refobject);
		DO64(drr_write_byref.drr_refoffset);
		DO64(drr_write_byref.drr_key.ddk_cksum.zc_word[0]);
		DO64(drr_write_byref.drr_key.ddk_cksum.zc_word[1]);
		DO64(drr_write_byref.drr_key.ddk_cksum.zc_word[2]);
		DO64(drr_write_byref.drr_key.ddk_cksum.zc_word[3]);
		DO64(drr_write_byref.drr_key.ddk_prop);
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
		break;
	case DRR_END:
		DO64(drr_end.drr_checksum.zc_word[0]);
		DO64(drr_end.drr_checksum.zc_word[1]);
		DO64(drr_end.drr_checksum.zc_word[2]);
		DO64(drr_end.drr_checksum.zc_word[3]);
		DO64(drr_end.drr_toguid);
		break;
	default:
		break;
	}
#undef DO64
#undef DO32
}

noinline static int
restore_object(struct restorearg *ra, objset_t *os, struct drr_object *drro)
{
	int err;
	dmu_tx_t *tx;
	void *data = NULL;

	if (drro->drr_type == DMU_OT_NONE ||
	    drro->drr_type >= DMU_OT_NUMTYPES ||
	    drro->drr_bonustype >= DMU_OT_NUMTYPES ||
	    drro->drr_checksumtype >= ZIO_CHECKSUM_FUNCTIONS ||
	    drro->drr_compress >= ZIO_COMPRESS_FUNCTIONS ||
	    P2PHASE(drro->drr_blksz, SPA_MINBLOCKSIZE) ||
	    drro->drr_blksz < SPA_MINBLOCKSIZE ||
	    drro->drr_blksz > SPA_MAXBLOCKSIZE ||
	    drro->drr_bonuslen > DN_MAX_BONUSLEN) {
		return (EINVAL);
	}

	err = dmu_object_info(os, drro->drr_object, NULL);

	if (err != 0 && err != ENOENT)
		return (EINVAL);

	if (drro->drr_bonuslen) {
		data = restore_read(ra, P2ROUNDUP(drro->drr_bonuslen, 8));
		if (ra->err)
			return (ra->err);
	}

	if (err == ENOENT) {
		/* currently free, want to be allocated */
		tx = dmu_tx_create(os);
		dmu_tx_hold_bonus(tx, DMU_NEW_OBJECT);
		err = dmu_tx_assign(tx, TXG_WAIT);
		if (err) {
			dmu_tx_abort(tx);
			return (err);
		}
		err = dmu_object_claim(os, drro->drr_object,
		    drro->drr_type, drro->drr_blksz,
		    drro->drr_bonustype, drro->drr_bonuslen, tx);
		dmu_tx_commit(tx);
	} else {
		/* currently allocated, want to be allocated */
		err = dmu_object_reclaim(os, drro->drr_object,
		    drro->drr_type, drro->drr_blksz,
		    drro->drr_bonustype, drro->drr_bonuslen);
	}
	if (err) {
		return (EINVAL);
	}

	tx = dmu_tx_create(os);
	dmu_tx_hold_bonus(tx, drro->drr_object);
	err = dmu_tx_assign(tx, TXG_WAIT);
	if (err) {
		dmu_tx_abort(tx);
		return (err);
	}

	dmu_object_set_checksum(os, drro->drr_object, drro->drr_checksumtype,
	    tx);
	dmu_object_set_compress(os, drro->drr_object, drro->drr_compress, tx);

	if (data != NULL) {
		dmu_buf_t *db;

		VERIFY(0 == dmu_bonus_hold(os, drro->drr_object, FTAG, &db));
		dmu_buf_will_dirty(db, tx);

		ASSERT3U(db->db_size, >=, drro->drr_bonuslen);
		bcopy(data, db->db_data, drro->drr_bonuslen);
		if (ra->byteswap) {
			dmu_ot[drro->drr_bonustype].ot_byteswap(db->db_data,
			    drro->drr_bonuslen);
		}
		dmu_buf_rele(db, FTAG);
	}
	dmu_tx_commit(tx);
	return (0);
}

/* ARGSUSED */
noinline static int
restore_freeobjects(struct restorearg *ra, objset_t *os,
    struct drr_freeobjects *drrfo)
{
	uint64_t obj;

	if (drrfo->drr_firstobj + drrfo->drr_numobjs < drrfo->drr_firstobj)
		return (EINVAL);

	for (obj = drrfo->drr_firstobj;
	    obj < drrfo->drr_firstobj + drrfo->drr_numobjs;
	    (void) dmu_object_next(os, &obj, FALSE, 0)) {
		int err;

		if (dmu_object_info(os, obj, NULL) != 0)
			continue;

		err = dmu_free_object(os, obj);
		if (err)
			return (err);
	}
	return (0);
}

noinline static int
restore_write(struct restorearg *ra, objset_t *os,
    struct drr_write *drrw)
{
	dmu_tx_t *tx;
	void *data;
	int err;

	if (drrw->drr_offset + drrw->drr_length < drrw->drr_offset ||
	    drrw->drr_type >= DMU_OT_NUMTYPES)
		return (EINVAL);

	data = restore_read(ra, drrw->drr_length);
	if (data == NULL)
		return (ra->err);

	if (dmu_object_info(os, drrw->drr_object, NULL) != 0)
		return (EINVAL);

	tx = dmu_tx_create(os);

	dmu_tx_hold_write(tx, drrw->drr_object,
	    drrw->drr_offset, drrw->drr_length);
	err = dmu_tx_assign(tx, TXG_WAIT);
	if (err) {
		dmu_tx_abort(tx);
		return (err);
	}
	if (ra->byteswap)
		dmu_ot[drrw->drr_type].ot_byteswap(data, drrw->drr_length);
	dmu_write(os, drrw->drr_object,
	    drrw->drr_offset, drrw->drr_length, data, tx);
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
static int
restore_write_byref(struct restorearg *ra, objset_t *os,
    struct drr_write_byref *drrwbr)
{
	dmu_tx_t *tx;
	int err;
	guid_map_entry_t gmesrch;
	guid_map_entry_t *gmep;
	avl_index_t	where;
	objset_t *ref_os = NULL;
	dmu_buf_t *dbp;

	if (drrwbr->drr_offset + drrwbr->drr_length < drrwbr->drr_offset)
		return (EINVAL);

	/*
	 * If the GUID of the referenced dataset is different from the
	 * GUID of the target dataset, find the referenced dataset.
	 */
	if (drrwbr->drr_toguid != drrwbr->drr_refguid) {
		gmesrch.guid = drrwbr->drr_refguid;
		if ((gmep = avl_find(ra->guid_to_ds_map, &gmesrch,
		    &where)) == NULL) {
			return (EINVAL);
		}
		if (dmu_objset_from_ds(gmep->gme_ds, &ref_os))
			return (EINVAL);
	} else {
		ref_os = os;
	}

	err = dmu_buf_hold(ref_os, drrwbr->drr_refobject,
	    drrwbr->drr_refoffset, FTAG, &dbp, DMU_READ_PREFETCH);
	if (err)
		return (err);

	tx = dmu_tx_create(os);

	dmu_tx_hold_write(tx, drrwbr->drr_object,
	    drrwbr->drr_offset, drrwbr->drr_length);
	err = dmu_tx_assign(tx, TXG_WAIT);
	if (err) {
		dmu_tx_abort(tx);
		return (err);
	}
	dmu_write(os, drrwbr->drr_object,
	    drrwbr->drr_offset, drrwbr->drr_length, dbp->db_data, tx);
	dmu_buf_rele(dbp, FTAG);
	dmu_tx_commit(tx);
	return (0);
}

static int
restore_spill(struct restorearg *ra, objset_t *os, struct drr_spill *drrs)
{
	dmu_tx_t *tx;
	void *data;
	dmu_buf_t *db, *db_spill;
	int err;

	if (drrs->drr_length < SPA_MINBLOCKSIZE ||
	    drrs->drr_length > SPA_MAXBLOCKSIZE)
		return (EINVAL);

	data = restore_read(ra, drrs->drr_length);
	if (data == NULL)
		return (ra->err);

	if (dmu_object_info(os, drrs->drr_object, NULL) != 0)
		return (EINVAL);

	VERIFY(0 == dmu_bonus_hold(os, drrs->drr_object, FTAG, &db));
	if ((err = dmu_spill_hold_by_bonus(db, FTAG, &db_spill)) != 0) {
		dmu_buf_rele(db, FTAG);
		return (err);
	}

	tx = dmu_tx_create(os);

	dmu_tx_hold_spill(tx, db->db_object);

	err = dmu_tx_assign(tx, TXG_WAIT);
	if (err) {
		dmu_buf_rele(db, FTAG);
		dmu_buf_rele(db_spill, FTAG);
		dmu_tx_abort(tx);
		return (err);
	}
	dmu_buf_will_dirty(db_spill, tx);

	if (db_spill->db_size < drrs->drr_length)
		VERIFY(0 == dbuf_spill_set_blksz(db_spill,
		    drrs->drr_length, tx));
	bcopy(data, db_spill->db_data, drrs->drr_length);

	dmu_buf_rele(db, FTAG);
	dmu_buf_rele(db_spill, FTAG);

	dmu_tx_commit(tx);
	return (0);
}

/* ARGSUSED */
noinline static int
restore_free(struct restorearg *ra, objset_t *os,
    struct drr_free *drrf)
{
	int err;

	if (drrf->drr_length != -1ULL &&
	    drrf->drr_offset + drrf->drr_length < drrf->drr_offset)
		return (EINVAL);

	if (dmu_object_info(os, drrf->drr_object, NULL) != 0)
		return (EINVAL);

	err = dmu_free_long_range(os, drrf->drr_object,
	    drrf->drr_offset, drrf->drr_length);
	return (err);
}

/*
 * NB: callers *must* call dmu_recv_end() if this succeeds.
 */
int
dmu_recv_stream(dmu_recv_cookie_t *drc, vnode_t *vp, offset_t *voffp,
    int cleanup_fd, uint64_t *action_handlep)
{
	struct restorearg ra = { 0 };
	dmu_replay_record_t *drr;
	objset_t *os;
	zio_cksum_t pcksum;
	int featureflags;

	if (drc->drc_drrb->drr_magic == BSWAP_64(DMU_BACKUP_MAGIC))
		ra.byteswap = TRUE;

	{
		/* compute checksum of drr_begin record */
		dmu_replay_record_t *drr;
		drr = kmem_zalloc(sizeof (dmu_replay_record_t), KM_SLEEP);

		drr->drr_type = DRR_BEGIN;
		drr->drr_u.drr_begin = *drc->drc_drrb;
		if (ra.byteswap) {
			fletcher_4_incremental_byteswap(drr,
			    sizeof (dmu_replay_record_t), &ra.cksum);
		} else {
			fletcher_4_incremental_native(drr,
			    sizeof (dmu_replay_record_t), &ra.cksum);
		}
		kmem_free(drr, sizeof (dmu_replay_record_t));
	}

	if (ra.byteswap) {
		struct drr_begin *drrb = drc->drc_drrb;
		drrb->drr_magic = BSWAP_64(drrb->drr_magic);
		drrb->drr_versioninfo = BSWAP_64(drrb->drr_versioninfo);
		drrb->drr_creation_time = BSWAP_64(drrb->drr_creation_time);
		drrb->drr_type = BSWAP_32(drrb->drr_type);
		drrb->drr_toguid = BSWAP_64(drrb->drr_toguid);
		drrb->drr_fromguid = BSWAP_64(drrb->drr_fromguid);
	}

	ra.vp = vp;
	ra.voff = *voffp;
	ra.bufsize = 1<<20;
	ra.buf = vmem_alloc(ra.bufsize, KM_SLEEP);

	/* these were verified in dmu_recv_begin */
	ASSERT(DMU_GET_STREAM_HDRTYPE(drc->drc_drrb->drr_versioninfo) ==
	    DMU_SUBSTREAM);
	ASSERT(drc->drc_drrb->drr_type < DMU_OST_NUMTYPES);

	/*
	 * Open the objset we are modifying.
	 */
	VERIFY(dmu_objset_from_ds(drc->drc_real_ds, &os) == 0);

	ASSERT(drc->drc_real_ds->ds_phys->ds_flags & DS_FLAG_INCONSISTENT);

	featureflags = DMU_GET_FEATUREFLAGS(drc->drc_drrb->drr_versioninfo);

	/* if this stream is dedup'ed, set up the avl tree for guid mapping */
	if (featureflags & DMU_BACKUP_FEATURE_DEDUP) {
		minor_t minor;

		if (cleanup_fd == -1) {
			ra.err = EBADF;
			goto out;
		}
		ra.err = zfs_onexit_fd_hold(cleanup_fd, &minor);
		if (ra.err) {
			cleanup_fd = -1;
			goto out;
		}

		if (*action_handlep == 0) {
			ra.guid_to_ds_map =
			    kmem_alloc(sizeof (avl_tree_t), KM_SLEEP);
			avl_create(ra.guid_to_ds_map, guid_compare,
			    sizeof (guid_map_entry_t),
			    offsetof(guid_map_entry_t, avlnode));
			ra.err = zfs_onexit_add_cb(minor,
			    free_guid_map_onexit, ra.guid_to_ds_map,
			    action_handlep);
			if (ra.err)
				goto out;
		} else {
			ra.err = zfs_onexit_cb_data(minor, *action_handlep,
			    (void **)&ra.guid_to_ds_map);
			if (ra.err)
				goto out;
		}

		drc->drc_guid_to_ds_map = ra.guid_to_ds_map;
	}

	/*
	 * Read records and process them.
	 */
	pcksum = ra.cksum;
	while (ra.err == 0 &&
	    NULL != (drr = restore_read(&ra, sizeof (*drr)))) {
		if (issig(JUSTLOOKING) && issig(FORREAL)) {
			ra.err = EINTR;
			goto out;
		}

		if (ra.byteswap)
			backup_byteswap(drr);

		switch (drr->drr_type) {
		case DRR_OBJECT:
		{
			/*
			 * We need to make a copy of the record header,
			 * because restore_{object,write} may need to
			 * restore_read(), which will invalidate drr.
			 */
			struct drr_object drro = drr->drr_u.drr_object;
			ra.err = restore_object(&ra, os, &drro);
			break;
		}
		case DRR_FREEOBJECTS:
		{
			struct drr_freeobjects drrfo =
			    drr->drr_u.drr_freeobjects;
			ra.err = restore_freeobjects(&ra, os, &drrfo);
			break;
		}
		case DRR_WRITE:
		{
			struct drr_write drrw = drr->drr_u.drr_write;
			ra.err = restore_write(&ra, os, &drrw);
			break;
		}
		case DRR_WRITE_BYREF:
		{
			struct drr_write_byref drrwbr =
			    drr->drr_u.drr_write_byref;
			ra.err = restore_write_byref(&ra, os, &drrwbr);
			break;
		}
		case DRR_FREE:
		{
			struct drr_free drrf = drr->drr_u.drr_free;
			ra.err = restore_free(&ra, os, &drrf);
			break;
		}
		case DRR_END:
		{
			struct drr_end drre = drr->drr_u.drr_end;
			/*
			 * We compare against the *previous* checksum
			 * value, because the stored checksum is of
			 * everything before the DRR_END record.
			 */
			if (!ZIO_CHECKSUM_EQUAL(drre.drr_checksum, pcksum))
				ra.err = ECKSUM;
			goto out;
		}
		case DRR_SPILL:
		{
			struct drr_spill drrs = drr->drr_u.drr_spill;
			ra.err = restore_spill(&ra, os, &drrs);
			break;
		}
		default:
			ra.err = EINVAL;
			goto out;
		}
		pcksum = ra.cksum;
	}
	ASSERT(ra.err != 0);

out:
	if ((featureflags & DMU_BACKUP_FEATURE_DEDUP) && (cleanup_fd != -1))
		zfs_onexit_fd_rele(cleanup_fd);

	if (ra.err != 0) {
		/*
		 * destroy what we created, so we don't leave it in the
		 * inconsistent restoring state.
		 */
		txg_wait_synced(drc->drc_real_ds->ds_dir->dd_pool, 0);

		(void) dsl_dataset_destroy(drc->drc_real_ds, dmu_recv_tag,
		    B_FALSE);
		if (drc->drc_real_ds != drc->drc_logical_ds) {
			mutex_exit(&drc->drc_logical_ds->ds_recvlock);
			dsl_dataset_rele(drc->drc_logical_ds, dmu_recv_tag);
		}
	}

	vmem_free(ra.buf, ra.bufsize);
	*voffp = ra.voff;
	return (ra.err);
}

struct recvendsyncarg {
	char *tosnap;
	uint64_t creation_time;
	uint64_t toguid;
};

static int
recv_end_check(void *arg1, void *arg2, dmu_tx_t *tx)
{
	dsl_dataset_t *ds = arg1;
	struct recvendsyncarg *resa = arg2;

	return (dsl_dataset_snapshot_check(ds, resa->tosnap, tx));
}

static void
recv_end_sync(void *arg1, void *arg2, dmu_tx_t *tx)
{
	dsl_dataset_t *ds = arg1;
	struct recvendsyncarg *resa = arg2;

	dsl_dataset_snapshot_sync(ds, resa->tosnap, tx);

	/* set snapshot's creation time and guid */
	dmu_buf_will_dirty(ds->ds_prev->ds_dbuf, tx);
	ds->ds_prev->ds_phys->ds_creation_time = resa->creation_time;
	ds->ds_prev->ds_phys->ds_guid = resa->toguid;
	ds->ds_prev->ds_phys->ds_flags &= ~DS_FLAG_INCONSISTENT;

	dmu_buf_will_dirty(ds->ds_dbuf, tx);
	ds->ds_phys->ds_flags &= ~DS_FLAG_INCONSISTENT;
}

static int
add_ds_to_guidmap(avl_tree_t *guid_map, dsl_dataset_t *ds)
{
	dsl_pool_t *dp = ds->ds_dir->dd_pool;
	uint64_t snapobj = ds->ds_phys->ds_prev_snap_obj;
	dsl_dataset_t *snapds;
	guid_map_entry_t *gmep;
	int err;

	ASSERT(guid_map != NULL);

	rw_enter(&dp->dp_config_rwlock, RW_READER);
	err = dsl_dataset_hold_obj(dp, snapobj, guid_map, &snapds);
	if (err == 0) {
		gmep = kmem_alloc(sizeof (guid_map_entry_t), KM_SLEEP);
		gmep->guid = snapds->ds_phys->ds_guid;
		gmep->gme_ds = snapds;
		avl_add(guid_map, gmep);
	}

	rw_exit(&dp->dp_config_rwlock);
	return (err);
}

static int
dmu_recv_existing_end(dmu_recv_cookie_t *drc)
{
	struct recvendsyncarg resa;
	dsl_dataset_t *ds = drc->drc_logical_ds;
	int err, myerr;

	/*
	 * XXX hack; seems the ds is still dirty and dsl_pool_zil_clean()
	 * expects it to have a ds_user_ptr (and zil), but clone_swap()
	 * can close it.
	 */
	txg_wait_synced(ds->ds_dir->dd_pool, 0);

	if (dsl_dataset_tryown(ds, FALSE, dmu_recv_tag)) {
		err = dsl_dataset_clone_swap(drc->drc_real_ds, ds,
		    drc->drc_force);
		if (err)
			goto out;
	} else {
		mutex_exit(&ds->ds_recvlock);
		dsl_dataset_rele(ds, dmu_recv_tag);
		(void) dsl_dataset_destroy(drc->drc_real_ds, dmu_recv_tag,
		    B_FALSE);
		return (EBUSY);
	}

	resa.creation_time = drc->drc_drrb->drr_creation_time;
	resa.toguid = drc->drc_drrb->drr_toguid;
	resa.tosnap = drc->drc_tosnap;

	err = dsl_sync_task_do(ds->ds_dir->dd_pool,
	    recv_end_check, recv_end_sync, ds, &resa, 3);
	if (err) {
		/* swap back */
		(void) dsl_dataset_clone_swap(drc->drc_real_ds, ds, B_TRUE);
	}

out:
	mutex_exit(&ds->ds_recvlock);
	if (err == 0 && drc->drc_guid_to_ds_map != NULL)
		(void) add_ds_to_guidmap(drc->drc_guid_to_ds_map, ds);
	dsl_dataset_disown(ds, dmu_recv_tag);
	myerr = dsl_dataset_destroy(drc->drc_real_ds, dmu_recv_tag, B_FALSE);
	ASSERT3U(myerr, ==, 0);
	return (err);
}

static int
dmu_recv_new_end(dmu_recv_cookie_t *drc)
{
	struct recvendsyncarg resa;
	dsl_dataset_t *ds = drc->drc_logical_ds;
	int err;

	/*
	 * XXX hack; seems the ds is still dirty and dsl_pool_zil_clean()
	 * expects it to have a ds_user_ptr (and zil), but clone_swap()
	 * can close it.
	 */
	txg_wait_synced(ds->ds_dir->dd_pool, 0);

	resa.creation_time = drc->drc_drrb->drr_creation_time;
	resa.toguid = drc->drc_drrb->drr_toguid;
	resa.tosnap = drc->drc_tosnap;

	err = dsl_sync_task_do(ds->ds_dir->dd_pool,
	    recv_end_check, recv_end_sync, ds, &resa, 3);
	if (err) {
		/* clean up the fs we just recv'd into */
		(void) dsl_dataset_destroy(ds, dmu_recv_tag, B_FALSE);
	} else {
		if (drc->drc_guid_to_ds_map != NULL)
			(void) add_ds_to_guidmap(drc->drc_guid_to_ds_map, ds);
		/* release the hold from dmu_recv_begin */
		dsl_dataset_disown(ds, dmu_recv_tag);
	}
	return (err);
}

int
dmu_recv_end(dmu_recv_cookie_t *drc)
{
	if (drc->drc_logical_ds != drc->drc_real_ds)
		return (dmu_recv_existing_end(drc));
	else
		return (dmu_recv_new_end(drc));
}
