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
#include <sys/dsl_pool.h>
#include <sys/dsl_synctask.h>
#include <sys/zfs_ioctl.h>
#include <sys/zap.h>
#include <sys/zio_checksum.h>

static char *dmu_recv_tag = "dmu_recv_tag";

struct backuparg {
	dmu_replay_record_t *drr;
	vnode_t *vp;
	offset_t *off;
	objset_t *os;
	zio_cksum_t zc;
	int err;
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
	/* write a FREE record */
	bzero(ba->drr, sizeof (dmu_replay_record_t));
	ba->drr->drr_type = DRR_FREE;
	ba->drr->drr_u.drr_free.drr_object = object;
	ba->drr->drr_u.drr_free.drr_offset = offset;
	ba->drr->drr_u.drr_free.drr_length = length;

	if (dump_bytes(ba, ba->drr, sizeof (dmu_replay_record_t)))
		return (EINTR);
	return (0);
}

static int
dump_data(struct backuparg *ba, dmu_object_type_t type,
    uint64_t object, uint64_t offset, int blksz, void *data)
{
	/* write a DATA record */
	bzero(ba->drr, sizeof (dmu_replay_record_t));
	ba->drr->drr_type = DRR_WRITE;
	ba->drr->drr_u.drr_write.drr_object = object;
	ba->drr->drr_u.drr_write.drr_type = type;
	ba->drr->drr_u.drr_write.drr_offset = offset;
	ba->drr->drr_u.drr_write.drr_length = blksz;

	if (dump_bytes(ba, ba->drr, sizeof (dmu_replay_record_t)))
		return (EINTR);
	if (dump_bytes(ba, data, blksz))
		return (EINTR);
	return (0);
}

static int
dump_freeobjects(struct backuparg *ba, uint64_t firstobj, uint64_t numobjs)
{
	/* write a FREEOBJECTS record */
	bzero(ba->drr, sizeof (dmu_replay_record_t));
	ba->drr->drr_type = DRR_FREEOBJECTS;
	ba->drr->drr_u.drr_freeobjects.drr_firstobj = firstobj;
	ba->drr->drr_u.drr_freeobjects.drr_numobjs = numobjs;

	if (dump_bytes(ba, ba->drr, sizeof (dmu_replay_record_t)))
		return (EINTR);
	return (0);
}

static int
dump_dnode(struct backuparg *ba, uint64_t object, dnode_phys_t *dnp)
{
	if (dnp == NULL || dnp->dn_type == DMU_OT_NONE)
		return (dump_freeobjects(ba, object, 1));

	/* write an OBJECT record */
	bzero(ba->drr, sizeof (dmu_replay_record_t));
	ba->drr->drr_type = DRR_OBJECT;
	ba->drr->drr_u.drr_object.drr_object = object;
	ba->drr->drr_u.drr_object.drr_type = dnp->dn_type;
	ba->drr->drr_u.drr_object.drr_bonustype = dnp->dn_bonustype;
	ba->drr->drr_u.drr_object.drr_blksz =
	    dnp->dn_datablkszsec << SPA_MINBLOCKSHIFT;
	ba->drr->drr_u.drr_object.drr_bonuslen = dnp->dn_bonuslen;
	ba->drr->drr_u.drr_object.drr_checksum = dnp->dn_checksum;
	ba->drr->drr_u.drr_object.drr_compress = dnp->dn_compress;

	if (dump_bytes(ba, ba->drr, sizeof (dmu_replay_record_t)))
		return (EINTR);

	if (dump_bytes(ba, DN_BONUS(dnp), P2ROUNDUP(dnp->dn_bonuslen, 8)))
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

static int
backup_cb(spa_t *spa, blkptr_t *bp, const zbookmark_t *zb,
    const dnode_phys_t *dnp, void *arg)
{
	struct backuparg *ba = arg;
	dmu_object_type_t type = bp ? BP_GET_TYPE(bp) : DMU_OT_NONE;
	int err = 0;

	if (issig(JUSTLOOKING) && issig(FORREAL))
		return (EINTR);

	if (bp == NULL && zb->zb_object == 0) {
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

		if (arc_read_nolock(NULL, spa, bp,
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
	} else { /* it's a level-0 block of a regular object */
		uint32_t aflags = ARC_WAIT;
		arc_buf_t *abuf;
		int blksz = BP_GET_LSIZE(bp);

		if (arc_read_nolock(NULL, spa, bp,
		    arc_getbuf_func, &abuf, ZIO_PRIORITY_ASYNC_READ,
		    ZIO_FLAG_CANFAIL, &aflags, zb) != 0)
			return (EIO);

		err = dump_data(ba, type, zb->zb_object, zb->zb_blkid * blksz,
		    blksz, abuf->b_data);
		(void) arc_buf_remove_ref(abuf, &abuf);
	}

	ASSERT(err == 0 || err == EINTR);
	return (err);
}

int
dmu_sendbackup(objset_t *tosnap, objset_t *fromsnap, boolean_t fromorigin,
    vnode_t *vp, offset_t *off)
{
	dsl_dataset_t *ds = tosnap->os->os_dsl_dataset;
	dsl_dataset_t *fromds = fromsnap ? fromsnap->os->os_dsl_dataset : NULL;
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
	drr->drr_u.drr_begin.drr_version = DMU_BACKUP_STREAM_VERSION;
	drr->drr_u.drr_begin.drr_creation_time =
	    ds->ds_phys->ds_creation_time;
	drr->drr_u.drr_begin.drr_type = tosnap->os->os_phys->os_type;
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
	ZIO_SET_CHECKSUM(&ba.zc, 0, 0, 0, 0);

	if (dump_bytes(&ba, drr, sizeof (dmu_replay_record_t))) {
		kmem_free(drr, sizeof (dmu_replay_record_t));
		return (ba.err);
	}

	err = traverse_dataset(ds, fromtxg, TRAVERSE_PRE | TRAVERSE_PREFETCH,
	    backup_cb, &ba);

	if (err) {
		if (err == EINTR && ba.err)
			err = ba.err;
		kmem_free(drr, sizeof (dmu_replay_record_t));
		return (err);
	}

	bzero(drr, sizeof (dmu_replay_record_t));
	drr->drr_type = DRR_END;
	drr->drr_u.drr_end.drr_checksum = ba.zc;

	if (dump_bytes(&ba, drr, sizeof (dmu_replay_record_t))) {
		kmem_free(drr, sizeof (dmu_replay_record_t));
		return (ba.err);
	}

	kmem_free(drr, sizeof (dmu_replay_record_t));

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
};

static dsl_dataset_t *
recv_full_sync_impl(dsl_pool_t *dp, uint64_t dsobj, dmu_objset_type_t type,
    cred_t *cr, dmu_tx_t *tx)
{
	dsl_dataset_t *ds;

	/* This should always work, since we just created it */
	/* XXX - create should return an owned ds */
	VERIFY(0 == dsl_dataset_own_obj(dp, dsobj,
	    DS_MODE_INCONSISTENT, dmu_recv_tag, &ds));

	if (type != DMU_OST_NONE) {
		(void) dmu_objset_create_impl(dp->dp_spa,
		    ds, &ds->ds_phys->ds_bp, type, tx);
	}

	spa_history_internal_log(LOG_DS_REPLAY_FULL_SYNC,
	    dp->dp_spa, tx, cr, "dataset = %lld", dsobj);

	return (ds);
}

/* ARGSUSED */
static int
recv_full_check(void *arg1, void *arg2, dmu_tx_t *tx)
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
		if (rbsa->origin->ds_phys->ds_num_children == 0)
			return (EINVAL);
		if (rbsa->origin->ds_phys->ds_guid != rbsa->fromguid)
			return (ENODEV);
	}

	return (0);
}

static void
recv_full_sync(void *arg1, void *arg2, cred_t *cr, dmu_tx_t *tx)
{
	dsl_dir_t *dd = arg1;
	struct recvbeginsyncarg *rbsa = arg2;
	uint64_t flags = DS_FLAG_INCONSISTENT | rbsa->dsflags;
	uint64_t dsobj;

	dsobj = dsl_dataset_create_sync(dd, strrchr(rbsa->tofs, '/') + 1,
	    rbsa->origin, flags, cr, tx);

	rbsa->ds = recv_full_sync_impl(dd->dd_pool, dsobj,
	    rbsa->origin ? DMU_OST_NONE : rbsa->type, cr, tx);
}

static int
recv_full_existing_check(void *arg1, void *arg2, dmu_tx_t *tx)
{
	dsl_dataset_t *ds = arg1;
	struct recvbeginsyncarg *rbsa = arg2;
	int err;

	/* must be a head ds */
	if (ds->ds_phys->ds_next_snap_obj != 0)
		return (EINVAL);

	/* must not be a clone ds */
	if (dsl_dir_is_clone(ds->ds_dir))
		return (EINVAL);

	err = dsl_dataset_destroy_check(ds, rbsa->tag, tx);
	if (err)
		return (err);

	if (rbsa->origin) {
		/* make sure it's a snap in the same pool */
		if (rbsa->origin->ds_dir->dd_pool != ds->ds_dir->dd_pool)
			return (EXDEV);
		if (rbsa->origin->ds_phys->ds_num_children == 0)
			return (EINVAL);
		if (rbsa->origin->ds_phys->ds_guid != rbsa->fromguid)
			return (ENODEV);
	}

	return (0);
}

static void
recv_full_existing_sync(void *arg1, void *arg2, cred_t *cr, dmu_tx_t *tx)
{
	dsl_dataset_t *ds = arg1;
	struct recvbeginsyncarg *rbsa = arg2;
	dsl_dir_t *dd = ds->ds_dir;
	uint64_t flags = DS_FLAG_INCONSISTENT | rbsa->dsflags;
	uint64_t dsobj;

	/*
	 * NB: caller must provide an extra hold on the dsl_dir_t, so it
	 * won't go away when dsl_dataset_destroy_sync() closes the
	 * dataset.
	 */
	dsl_dataset_destroy_sync(ds, rbsa->tag, cr, tx);

	dsobj = dsl_dataset_create_sync_dd(dd, rbsa->origin, flags, tx);

	rbsa->ds = recv_full_sync_impl(dd->dd_pool, dsobj,
	    rbsa->origin ? DMU_OST_NONE : rbsa->type, cr, tx);
}

/* ARGSUSED */
static int
recv_incremental_check(void *arg1, void *arg2, dmu_tx_t *tx)
{
	dsl_dataset_t *ds = arg1;
	struct recvbeginsyncarg *rbsa = arg2;
	int err;
	uint64_t val;

	/* must not have any changes since most recent snapshot */
	if (!rbsa->force && dsl_dataset_modified_since_lastsnap(ds))
		return (ETXTBSY);

	/* must already be a snapshot of this fs */
	if (ds->ds_phys->ds_prev_snap_obj == 0)
		return (ENODEV);

	/* most recent snapshot must match fromguid */
	if (ds->ds_prev->ds_phys->ds_guid != rbsa->fromguid)
		return (ENODEV);

	/* temporary clone name must not exist */
	err = zap_lookup(ds->ds_dir->dd_pool->dp_meta_objset,
	    ds->ds_dir->dd_phys->dd_child_dir_zapobj,
	    rbsa->clonelastname, 8, 1, &val);
	if (err == 0)
		return (EEXIST);
	if (err != ENOENT)
		return (err);

	/* new snapshot name must not exist */
	err = zap_lookup(ds->ds_dir->dd_pool->dp_meta_objset,
	    ds->ds_phys->ds_snapnames_zapobj, rbsa->tosnap, 8, 1, &val);
	if (err == 0)
		return (EEXIST);
	if (err != ENOENT)
		return (err);
	return (0);
}

/* ARGSUSED */
static void
recv_online_incremental_sync(void *arg1, void *arg2, cred_t *cr, dmu_tx_t *tx)
{
	dsl_dataset_t *ohds = arg1;
	struct recvbeginsyncarg *rbsa = arg2;
	dsl_pool_t *dp = ohds->ds_dir->dd_pool;
	dsl_dataset_t *ods, *cds;
	uint64_t flags = DS_FLAG_INCONSISTENT | rbsa->dsflags;
	uint64_t dsobj;

	/* create the temporary clone */
	VERIFY(0 == dsl_dataset_hold_obj(dp, ohds->ds_phys->ds_prev_snap_obj,
	    FTAG, &ods));
	dsobj = dsl_dataset_create_sync(ohds->ds_dir,
	    rbsa->clonelastname, ods, flags, cr, tx);
	dsl_dataset_rele(ods, FTAG);

	/* open the temporary clone */
	VERIFY(0 == dsl_dataset_own_obj(dp, dsobj,
	    DS_MODE_INCONSISTENT, dmu_recv_tag, &cds));

	/* copy the refquota from the target fs to the clone */
	if (ohds->ds_quota > 0)
		dsl_dataset_set_quota_sync(cds, &ohds->ds_quota, cr, tx);

	rbsa->ds = cds;

	spa_history_internal_log(LOG_DS_REPLAY_INC_SYNC,
	    dp->dp_spa, tx, cr, "dataset = %lld", dsobj);
}

/* ARGSUSED */
static void
recv_offline_incremental_sync(void *arg1, void *arg2, cred_t *cr, dmu_tx_t *tx)
{
	dsl_dataset_t *ds = arg1;

	dmu_buf_will_dirty(ds->ds_dbuf, tx);
	ds->ds_phys->ds_flags |= DS_FLAG_INCONSISTENT;

	spa_history_internal_log(LOG_DS_REPLAY_INC_SYNC,
	    ds->ds_dir->dd_pool->dp_spa, tx, cr, "dataset = %lld",
	    ds->ds_object);
}

/*
 * NB: callers *MUST* call dmu_recv_stream() if dmu_recv_begin()
 * succeeds; otherwise we will leak the holds on the datasets.
 */
int
dmu_recv_begin(char *tofs, char *tosnap, struct drr_begin *drrb,
    boolean_t force, objset_t *origin, boolean_t online, dmu_recv_cookie_t *drc)
{
	int err = 0;
	boolean_t byteswap;
	struct recvbeginsyncarg rbsa;
	uint64_t version;
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
	rbsa.origin = origin ? origin->os->os_dsl_dataset : NULL;
	rbsa.fromguid = drrb->drr_fromguid;
	rbsa.type = drrb->drr_type;
	rbsa.tag = FTAG;
	rbsa.dsflags = 0;
	version = drrb->drr_version;
	flags = drrb->drr_flags;

	if (byteswap) {
		rbsa.type = BSWAP_32(rbsa.type);
		rbsa.fromguid = BSWAP_64(rbsa.fromguid);
		version = BSWAP_64(version);
		flags = BSWAP_32(flags);
	}

	if (version != DMU_BACKUP_STREAM_VERSION ||
	    rbsa.type >= DMU_OST_NUMTYPES ||
	    ((flags & DRR_FLAG_CLONE) && origin == NULL))
		return (EINVAL);

	if (flags & DRR_FLAG_CI_DATA)
		rbsa.dsflags = DS_FLAG_CI_DATASET;

	bzero(drc, sizeof (dmu_recv_cookie_t));
	drc->drc_drrb = drrb;
	drc->drc_tosnap = tosnap;
	drc->drc_force = force;

	/*
	 * Process the begin in syncing context.
	 */
	if (rbsa.fromguid && !(flags & DRR_FLAG_CLONE) && !online) {
		/* offline incremental receive */
		err = dsl_dataset_own(tofs, 0, dmu_recv_tag, &ds);
		if (err)
			return (err);

		/*
		 * Only do the rollback if the most recent snapshot
		 * matches the incremental source
		 */
		if (force) {
			if (ds->ds_prev == NULL ||
			    ds->ds_prev->ds_phys->ds_guid !=
			    rbsa.fromguid) {
				dsl_dataset_disown(ds, dmu_recv_tag);
				return (ENODEV);
			}
			(void) dsl_dataset_rollback(ds, DMU_OST_NONE);
		}
		rbsa.force = B_FALSE;
		err = dsl_sync_task_do(ds->ds_dir->dd_pool,
		    recv_incremental_check,
		    recv_offline_incremental_sync, ds, &rbsa, 1);
		if (err) {
			dsl_dataset_disown(ds, dmu_recv_tag);
			return (err);
		}
		drc->drc_logical_ds = drc->drc_real_ds = ds;
	} else if (rbsa.fromguid && !(flags & DRR_FLAG_CLONE)) {
		/* online incremental receive */

		/* tmp clone name is: tofs/%tosnap" */
		(void) snprintf(rbsa.clonelastname, sizeof (rbsa.clonelastname),
		    "%%%s", tosnap);

		/* open the dataset we are logically receiving into */
		err = dsl_dataset_hold(tofs, dmu_recv_tag, &ds);
		if (err)
			return (err);

		rbsa.force = force;
		err = dsl_sync_task_do(ds->ds_dir->dd_pool,
		    recv_incremental_check,
		    recv_online_incremental_sync, ds, &rbsa, 5);
		if (err) {
			dsl_dataset_rele(ds, dmu_recv_tag);
			return (err);
		}
		drc->drc_logical_ds = ds;
		drc->drc_real_ds = rbsa.ds;
	} else {
		/* create new fs -- full backup or clone */
		dsl_dir_t *dd = NULL;
		const char *tail;

		err = dsl_dir_open(tofs, FTAG, &dd, &tail);
		if (err)
			return (err);
		if (tail == NULL) {
			if (!force) {
				dsl_dir_close(dd, FTAG);
				return (EEXIST);
			}

			rw_enter(&dd->dd_pool->dp_config_rwlock, RW_READER);
			err = dsl_dataset_own_obj(dd->dd_pool,
			    dd->dd_phys->dd_head_dataset_obj,
			    DS_MODE_INCONSISTENT, FTAG, &ds);
			rw_exit(&dd->dd_pool->dp_config_rwlock);
			if (err) {
				dsl_dir_close(dd, FTAG);
				return (err);
			}

			dsl_dataset_make_exclusive(ds, FTAG);
			err = dsl_sync_task_do(dd->dd_pool,
			    recv_full_existing_check,
			    recv_full_existing_sync, ds, &rbsa, 5);
			dsl_dataset_disown(ds, FTAG);
		} else {
			err = dsl_sync_task_do(dd->dd_pool, recv_full_check,
			    recv_full_sync, dd, &rbsa, 5);
		}
		dsl_dir_close(dd, FTAG);
		if (err)
			return (err);
		drc->drc_logical_ds = drc->drc_real_ds = rbsa.ds;
		drc->drc_newfs = B_TRUE;
	}

	return (0);
}

struct restorearg {
	int err;
	int byteswap;
	vnode_t *vp;
	char *buf;
	uint64_t voff;
	int bufsize; /* amount of memory allocated for buf */
	zio_cksum_t cksum;
};

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

static void
backup_byteswap(dmu_replay_record_t *drr)
{
#define	DO64(X) (drr->drr_u.X = BSWAP_64(drr->drr_u.X))
#define	DO32(X) (drr->drr_u.X = BSWAP_32(drr->drr_u.X))
	drr->drr_type = BSWAP_32(drr->drr_type);
	drr->drr_payloadlen = BSWAP_32(drr->drr_payloadlen);
	switch (drr->drr_type) {
	case DRR_BEGIN:
		DO64(drr_begin.drr_magic);
		DO64(drr_begin.drr_version);
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
		break;
	case DRR_FREEOBJECTS:
		DO64(drr_freeobjects.drr_firstobj);
		DO64(drr_freeobjects.drr_numobjs);
		break;
	case DRR_WRITE:
		DO64(drr_write.drr_object);
		DO32(drr_write.drr_type);
		DO64(drr_write.drr_offset);
		DO64(drr_write.drr_length);
		break;
	case DRR_FREE:
		DO64(drr_free.drr_object);
		DO64(drr_free.drr_offset);
		DO64(drr_free.drr_length);
		break;
	case DRR_END:
		DO64(drr_end.drr_checksum.zc_word[0]);
		DO64(drr_end.drr_checksum.zc_word[1]);
		DO64(drr_end.drr_checksum.zc_word[2]);
		DO64(drr_end.drr_checksum.zc_word[3]);
		break;
	}
#undef DO64
#undef DO32
}

static int
restore_object(struct restorearg *ra, objset_t *os, struct drr_object *drro)
{
	int err;
	dmu_tx_t *tx;
	void *data = NULL;

	err = dmu_object_info(os, drro->drr_object, NULL);

	if (err != 0 && err != ENOENT)
		return (EINVAL);

	if (drro->drr_type == DMU_OT_NONE ||
	    drro->drr_type >= DMU_OT_NUMTYPES ||
	    drro->drr_bonustype >= DMU_OT_NUMTYPES ||
	    drro->drr_checksum >= ZIO_CHECKSUM_FUNCTIONS ||
	    drro->drr_compress >= ZIO_COMPRESS_FUNCTIONS ||
	    P2PHASE(drro->drr_blksz, SPA_MINBLOCKSIZE) ||
	    drro->drr_blksz < SPA_MINBLOCKSIZE ||
	    drro->drr_blksz > SPA_MAXBLOCKSIZE ||
	    drro->drr_bonuslen > DN_MAX_BONUSLEN) {
		return (EINVAL);
	}

	if (drro->drr_bonuslen) {
		data = restore_read(ra, P2ROUNDUP(drro->drr_bonuslen, 8));
		if (ra->err)
			return (ra->err);
	}

	tx = dmu_tx_create(os);

	if (err == ENOENT) {
		/* currently free, want to be allocated */
		dmu_tx_hold_bonus(tx, DMU_NEW_OBJECT);
		dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0, 1);
		err = dmu_tx_assign(tx, TXG_WAIT);
		if (err) {
			dmu_tx_abort(tx);
			return (err);
		}
		err = dmu_object_claim(os, drro->drr_object,
		    drro->drr_type, drro->drr_blksz,
		    drro->drr_bonustype, drro->drr_bonuslen, tx);
	} else {
		/* currently allocated, want to be allocated */
		dmu_tx_hold_bonus(tx, drro->drr_object);
		/*
		 * We may change blocksize, so need to
		 * hold_write
		 */
		dmu_tx_hold_write(tx, drro->drr_object, 0, 1);
		err = dmu_tx_assign(tx, TXG_WAIT);
		if (err) {
			dmu_tx_abort(tx);
			return (err);
		}

		err = dmu_object_reclaim(os, drro->drr_object,
		    drro->drr_type, drro->drr_blksz,
		    drro->drr_bonustype, drro->drr_bonuslen, tx);
	}
	if (err) {
		dmu_tx_commit(tx);
		return (EINVAL);
	}

	dmu_object_set_checksum(os, drro->drr_object, drro->drr_checksum, tx);
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
static int
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

static int
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

/* ARGSUSED */
static int
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

void
dmu_recv_abort_cleanup(dmu_recv_cookie_t *drc)
{
	if (drc->drc_newfs || drc->drc_real_ds != drc->drc_logical_ds) {
		/*
		 * online incremental or new fs: destroy the fs (which
		 * may be a clone) that we created
		 */
		(void) dsl_dataset_destroy(drc->drc_real_ds, dmu_recv_tag);
		if (drc->drc_real_ds != drc->drc_logical_ds)
			dsl_dataset_rele(drc->drc_logical_ds, dmu_recv_tag);
	} else {
		/*
		 * offline incremental: rollback to most recent snapshot.
		 */
		(void) dsl_dataset_rollback(drc->drc_real_ds, DMU_OST_NONE);
		dsl_dataset_disown(drc->drc_real_ds, dmu_recv_tag);
	}
}

/*
 * NB: callers *must* call dmu_recv_end() if this succeeds.
 */
int
dmu_recv_stream(dmu_recv_cookie_t *drc, vnode_t *vp, offset_t *voffp)
{
	struct restorearg ra = { 0 };
	dmu_replay_record_t *drr;
	objset_t *os;
	zio_cksum_t pcksum;

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
		drrb->drr_version = BSWAP_64(drrb->drr_version);
		drrb->drr_creation_time = BSWAP_64(drrb->drr_creation_time);
		drrb->drr_type = BSWAP_32(drrb->drr_type);
		drrb->drr_toguid = BSWAP_64(drrb->drr_toguid);
		drrb->drr_fromguid = BSWAP_64(drrb->drr_fromguid);
	}

	ra.vp = vp;
	ra.voff = *voffp;
	ra.bufsize = 1<<20;
	ra.buf = kmem_alloc(ra.bufsize, KM_SLEEP);

	/* these were verified in dmu_recv_begin */
	ASSERT(drc->drc_drrb->drr_version == DMU_BACKUP_STREAM_VERSION);
	ASSERT(drc->drc_drrb->drr_type < DMU_OST_NUMTYPES);

	/*
	 * Open the objset we are modifying.
	 */
	VERIFY(dmu_objset_open_ds(drc->drc_real_ds, DMU_OST_ANY, &os) == 0);

	ASSERT(drc->drc_real_ds->ds_phys->ds_flags & DS_FLAG_INCONSISTENT);

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
		default:
			ra.err = EINVAL;
			goto out;
		}
		pcksum = ra.cksum;
	}
	ASSERT(ra.err != 0);

out:
	dmu_objset_close(os);

	if (ra.err != 0) {
		/*
		 * rollback or destroy what we created, so we don't
		 * leave it in the restoring state.
		 */
		txg_wait_synced(drc->drc_real_ds->ds_dir->dd_pool, 0);
		dmu_recv_abort_cleanup(drc);
	}

	kmem_free(ra.buf, ra.bufsize);
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
recv_end_sync(void *arg1, void *arg2, cred_t *cr, dmu_tx_t *tx)
{
	dsl_dataset_t *ds = arg1;
	struct recvendsyncarg *resa = arg2;

	dsl_dataset_snapshot_sync(ds, resa->tosnap, cr, tx);

	/* set snapshot's creation time and guid */
	dmu_buf_will_dirty(ds->ds_prev->ds_dbuf, tx);
	ds->ds_prev->ds_phys->ds_creation_time = resa->creation_time;
	ds->ds_prev->ds_phys->ds_guid = resa->toguid;
	ds->ds_prev->ds_phys->ds_flags &= ~DS_FLAG_INCONSISTENT;

	dmu_buf_will_dirty(ds->ds_dbuf, tx);
	ds->ds_phys->ds_flags &= ~DS_FLAG_INCONSISTENT;
}

int
dmu_recv_end(dmu_recv_cookie_t *drc)
{
	struct recvendsyncarg resa;
	dsl_dataset_t *ds = drc->drc_logical_ds;
	int err;

	/*
	 * XXX hack; seems the ds is still dirty and
	 * dsl_pool_zil_clean() expects it to have a ds_user_ptr
	 * (and zil), but clone_swap() can close it.
	 */
	txg_wait_synced(ds->ds_dir->dd_pool, 0);

	if (ds != drc->drc_real_ds) {
		/* we are doing an online recv */
		if (dsl_dataset_tryown(ds, FALSE, dmu_recv_tag)) {
			err = dsl_dataset_clone_swap(drc->drc_real_ds, ds,
			    drc->drc_force);
			if (err)
				dsl_dataset_disown(ds, dmu_recv_tag);
		} else {
			err = EBUSY;
			dsl_dataset_rele(ds, dmu_recv_tag);
		}
		/* dsl_dataset_destroy() will disown the ds */
		(void) dsl_dataset_destroy(drc->drc_real_ds, dmu_recv_tag);
		if (err)
			return (err);
	}

	resa.creation_time = drc->drc_drrb->drr_creation_time;
	resa.toguid = drc->drc_drrb->drr_toguid;
	resa.tosnap = drc->drc_tosnap;

	err = dsl_sync_task_do(ds->ds_dir->dd_pool,
	    recv_end_check, recv_end_sync, ds, &resa, 3);
	if (err) {
		if (drc->drc_newfs) {
			ASSERT(ds == drc->drc_real_ds);
			(void) dsl_dataset_destroy(ds, dmu_recv_tag);
			return (err);
		} else {
			(void) dsl_dataset_rollback(ds, DMU_OST_NONE);
		}
	}

	/* release the hold from dmu_recv_begin */
	dsl_dataset_disown(ds, dmu_recv_tag);
	return (err);
}
