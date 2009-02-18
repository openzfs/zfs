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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/dsl_pool.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_synctask.h>
#include <sys/dnode.h>
#include <sys/dmu_tx.h>
#include <sys/dmu_objset.h>
#include <sys/arc.h>
#include <sys/zap.h>
#include <sys/zio.h>
#include <sys/zfs_context.h>
#include <sys/fs/zfs.h>
#include <sys/zfs_znode.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>
#include <sys/zil_impl.h>

typedef int (scrub_cb_t)(dsl_pool_t *, const blkptr_t *, const zbookmark_t *);

static scrub_cb_t dsl_pool_scrub_clean_cb;
static dsl_syncfunc_t dsl_pool_scrub_cancel_sync;

int zfs_scrub_min_time = 1; /* scrub for at least 1 sec each txg */
int zfs_resilver_min_time = 3; /* resilver for at least 3 sec each txg */
boolean_t zfs_no_scrub_io = B_FALSE; /* set to disable scrub i/o */

extern int zfs_txg_timeout;

static scrub_cb_t *scrub_funcs[SCRUB_FUNC_NUMFUNCS] = {
	NULL,
	dsl_pool_scrub_clean_cb
};

#define	SET_BOOKMARK(zb, objset, object, level, blkid)  \
{                                                       \
	(zb)->zb_objset = objset;                       \
	(zb)->zb_object = object;                       \
	(zb)->zb_level = level;                         \
	(zb)->zb_blkid = blkid;                         \
}

/* ARGSUSED */
static void
dsl_pool_scrub_setup_sync(void *arg1, void *arg2, cred_t *cr, dmu_tx_t *tx)
{
	dsl_pool_t *dp = arg1;
	enum scrub_func *funcp = arg2;
	dmu_object_type_t ot = 0;
	boolean_t complete = B_FALSE;

	dsl_pool_scrub_cancel_sync(dp, &complete, cr, tx);

	ASSERT(dp->dp_scrub_func == SCRUB_FUNC_NONE);
	ASSERT(*funcp > SCRUB_FUNC_NONE);
	ASSERT(*funcp < SCRUB_FUNC_NUMFUNCS);

	dp->dp_scrub_min_txg = 0;
	dp->dp_scrub_max_txg = tx->tx_txg;

	if (*funcp == SCRUB_FUNC_CLEAN) {
		vdev_t *rvd = dp->dp_spa->spa_root_vdev;

		/* rewrite all disk labels */
		vdev_config_dirty(rvd);

		if (vdev_resilver_needed(rvd,
		    &dp->dp_scrub_min_txg, &dp->dp_scrub_max_txg)) {
			spa_event_notify(dp->dp_spa, NULL,
			    ESC_ZFS_RESILVER_START);
			dp->dp_scrub_max_txg = MIN(dp->dp_scrub_max_txg,
			    tx->tx_txg);
		} else {
			spa_event_notify(dp->dp_spa, NULL,
			    ESC_ZFS_SCRUB_START);
		}

		/* zero out the scrub stats in all vdev_stat_t's */
		vdev_scrub_stat_update(rvd,
		    dp->dp_scrub_min_txg ? POOL_SCRUB_RESILVER :
		    POOL_SCRUB_EVERYTHING, B_FALSE);

		dp->dp_spa->spa_scrub_started = B_TRUE;
	}

	/* back to the generic stuff */

	if (dp->dp_blkstats == NULL) {
		dp->dp_blkstats =
		    kmem_alloc(sizeof (zfs_all_blkstats_t), KM_SLEEP);
	}
	bzero(dp->dp_blkstats, sizeof (zfs_all_blkstats_t));

	if (spa_version(dp->dp_spa) < SPA_VERSION_DSL_SCRUB)
		ot = DMU_OT_ZAP_OTHER;

	dp->dp_scrub_func = *funcp;
	dp->dp_scrub_queue_obj = zap_create(dp->dp_meta_objset,
	    ot ? ot : DMU_OT_SCRUB_QUEUE, DMU_OT_NONE, 0, tx);
	bzero(&dp->dp_scrub_bookmark, sizeof (zbookmark_t));
	dp->dp_scrub_restart = B_FALSE;
	dp->dp_spa->spa_scrub_errors = 0;

	VERIFY(0 == zap_add(dp->dp_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_SCRUB_FUNC, sizeof (uint32_t), 1,
	    &dp->dp_scrub_func, tx));
	VERIFY(0 == zap_add(dp->dp_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_SCRUB_QUEUE, sizeof (uint64_t), 1,
	    &dp->dp_scrub_queue_obj, tx));
	VERIFY(0 == zap_add(dp->dp_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_SCRUB_MIN_TXG, sizeof (uint64_t), 1,
	    &dp->dp_scrub_min_txg, tx));
	VERIFY(0 == zap_add(dp->dp_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_SCRUB_MAX_TXG, sizeof (uint64_t), 1,
	    &dp->dp_scrub_max_txg, tx));
	VERIFY(0 == zap_add(dp->dp_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_SCRUB_BOOKMARK, sizeof (uint64_t), 4,
	    &dp->dp_scrub_bookmark, tx));
	VERIFY(0 == zap_add(dp->dp_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_SCRUB_ERRORS, sizeof (uint64_t), 1,
	    &dp->dp_spa->spa_scrub_errors, tx));

	spa_history_internal_log(LOG_POOL_SCRUB, dp->dp_spa, tx, cr,
	    "func=%u mintxg=%llu maxtxg=%llu",
	    *funcp, dp->dp_scrub_min_txg, dp->dp_scrub_max_txg);
}

int
dsl_pool_scrub_setup(dsl_pool_t *dp, enum scrub_func func)
{
	return (dsl_sync_task_do(dp, NULL,
	    dsl_pool_scrub_setup_sync, dp, &func, 0));
}

/* ARGSUSED */
static void
dsl_pool_scrub_cancel_sync(void *arg1, void *arg2, cred_t *cr, dmu_tx_t *tx)
{
	dsl_pool_t *dp = arg1;
	boolean_t *completep = arg2;

	if (dp->dp_scrub_func == SCRUB_FUNC_NONE)
		return;

	mutex_enter(&dp->dp_scrub_cancel_lock);

	if (dp->dp_scrub_restart) {
		dp->dp_scrub_restart = B_FALSE;
		*completep = B_FALSE;
	}

	/* XXX this is scrub-clean specific */
	mutex_enter(&dp->dp_spa->spa_scrub_lock);
	while (dp->dp_spa->spa_scrub_inflight > 0) {
		cv_wait(&dp->dp_spa->spa_scrub_io_cv,
		    &dp->dp_spa->spa_scrub_lock);
	}
	mutex_exit(&dp->dp_spa->spa_scrub_lock);
	dp->dp_spa->spa_scrub_started = B_FALSE;
	dp->dp_spa->spa_scrub_active = B_FALSE;

	dp->dp_scrub_func = SCRUB_FUNC_NONE;
	VERIFY(0 == dmu_object_free(dp->dp_meta_objset,
	    dp->dp_scrub_queue_obj, tx));
	dp->dp_scrub_queue_obj = 0;
	bzero(&dp->dp_scrub_bookmark, sizeof (zbookmark_t));

	VERIFY(0 == zap_remove(dp->dp_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_SCRUB_QUEUE, tx));
	VERIFY(0 == zap_remove(dp->dp_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_SCRUB_MIN_TXG, tx));
	VERIFY(0 == zap_remove(dp->dp_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_SCRUB_MAX_TXG, tx));
	VERIFY(0 == zap_remove(dp->dp_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_SCRUB_BOOKMARK, tx));
	VERIFY(0 == zap_remove(dp->dp_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_SCRUB_FUNC, tx));
	VERIFY(0 == zap_remove(dp->dp_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_SCRUB_ERRORS, tx));

	spa_history_internal_log(LOG_POOL_SCRUB_DONE, dp->dp_spa, tx, cr,
	    "complete=%u", *completep);

	/* below is scrub-clean specific */
	vdev_scrub_stat_update(dp->dp_spa->spa_root_vdev, POOL_SCRUB_NONE,
	    *completep);
	/*
	 * If the scrub/resilver completed, update all DTLs to reflect this.
	 * Whether it succeeded or not, vacate all temporary scrub DTLs.
	 */
	vdev_dtl_reassess(dp->dp_spa->spa_root_vdev, tx->tx_txg,
	    *completep ? dp->dp_scrub_max_txg : 0, B_TRUE);
	if (*completep)
		spa_event_notify(dp->dp_spa, NULL, dp->dp_scrub_min_txg ?
		    ESC_ZFS_RESILVER_FINISH : ESC_ZFS_SCRUB_FINISH);
	spa_errlog_rotate(dp->dp_spa);

	/*
	 * We may have finished replacing a device.
	 * Let the async thread assess this and handle the detach.
	 */
	spa_async_request(dp->dp_spa, SPA_ASYNC_RESILVER_DONE);

	dp->dp_scrub_min_txg = dp->dp_scrub_max_txg = 0;
	mutex_exit(&dp->dp_scrub_cancel_lock);
}

int
dsl_pool_scrub_cancel(dsl_pool_t *dp)
{
	boolean_t complete = B_FALSE;

	return (dsl_sync_task_do(dp, NULL,
	    dsl_pool_scrub_cancel_sync, dp, &complete, 3));
}

int
dsl_free(zio_t *pio, dsl_pool_t *dp, uint64_t txg, const blkptr_t *bpp,
    zio_done_func_t *done, void *private, uint32_t arc_flags)
{
	/*
	 * This function will be used by bp-rewrite wad to intercept frees.
	 */
	return (arc_free(pio, dp->dp_spa, txg, (blkptr_t *)bpp,
	    done, private, arc_flags));
}

static boolean_t
bookmark_is_zero(const zbookmark_t *zb)
{
	return (zb->zb_objset == 0 && zb->zb_object == 0 &&
	    zb->zb_level == 0 && zb->zb_blkid == 0);
}

/* dnp is the dnode for zb1->zb_object */
static boolean_t
bookmark_is_before(dnode_phys_t *dnp, const zbookmark_t *zb1,
    const zbookmark_t *zb2)
{
	uint64_t zb1nextL0, zb2thisobj;

	ASSERT(zb1->zb_objset == zb2->zb_objset);
	ASSERT(zb1->zb_object != -1ULL);
	ASSERT(zb2->zb_level == 0);

	/*
	 * A bookmark in the deadlist is considered to be after
	 * everything else.
	 */
	if (zb2->zb_object == -1ULL)
		return (B_TRUE);

	/* The objset_phys_t isn't before anything. */
	if (dnp == NULL)
		return (B_FALSE);

	zb1nextL0 = (zb1->zb_blkid + 1) <<
	    ((zb1->zb_level) * (dnp->dn_indblkshift - SPA_BLKPTRSHIFT));

	zb2thisobj = zb2->zb_object ? zb2->zb_object :
	    zb2->zb_blkid << (DNODE_BLOCK_SHIFT - DNODE_SHIFT);

	if (zb1->zb_object == 0) {
		uint64_t nextobj = zb1nextL0 *
		    (dnp->dn_datablkszsec << SPA_MINBLOCKSHIFT) >> DNODE_SHIFT;
		return (nextobj <= zb2thisobj);
	}

	if (zb1->zb_object < zb2thisobj)
		return (B_TRUE);
	if (zb1->zb_object > zb2thisobj)
		return (B_FALSE);
	if (zb2->zb_object == 0)
		return (B_FALSE);
	return (zb1nextL0 <= zb2->zb_blkid);
}

static boolean_t
scrub_pause(dsl_pool_t *dp, const zbookmark_t *zb)
{
	int elapsed_ticks;
	int mintime;

	if (dp->dp_scrub_pausing)
		return (B_TRUE); /* we're already pausing */

	if (!bookmark_is_zero(&dp->dp_scrub_bookmark))
		return (B_FALSE); /* we're resuming */

	/* We only know how to resume from level-0 blocks. */
	if (zb->zb_level != 0)
		return (B_FALSE);

	mintime = dp->dp_scrub_isresilver ? zfs_resilver_min_time :
	    zfs_scrub_min_time;
	elapsed_ticks = lbolt64 - dp->dp_scrub_start_time;
	if (elapsed_ticks > hz * zfs_txg_timeout ||
	    (elapsed_ticks > hz * mintime && txg_sync_waiting(dp))) {
		dprintf("pausing at %llx/%llx/%llx/%llx\n",
		    (longlong_t)zb->zb_objset, (longlong_t)zb->zb_object,
		    (longlong_t)zb->zb_level, (longlong_t)zb->zb_blkid);
		dp->dp_scrub_pausing = B_TRUE;
		dp->dp_scrub_bookmark = *zb;
		return (B_TRUE);
	}
	return (B_FALSE);
}

typedef struct zil_traverse_arg {
	dsl_pool_t	*zta_dp;
	zil_header_t	*zta_zh;
} zil_traverse_arg_t;

/* ARGSUSED */
static void
traverse_zil_block(zilog_t *zilog, blkptr_t *bp, void *arg, uint64_t claim_txg)
{
	zil_traverse_arg_t *zta = arg;
	dsl_pool_t *dp = zta->zta_dp;
	zil_header_t *zh = zta->zta_zh;
	zbookmark_t zb;

	if (bp->blk_birth <= dp->dp_scrub_min_txg)
		return;

	if (claim_txg == 0 && bp->blk_birth >= spa_first_txg(dp->dp_spa))
		return;

	zb.zb_objset = zh->zh_log.blk_cksum.zc_word[ZIL_ZC_OBJSET];
	zb.zb_object = 0;
	zb.zb_level = -1;
	zb.zb_blkid = bp->blk_cksum.zc_word[ZIL_ZC_SEQ];
	VERIFY(0 == scrub_funcs[dp->dp_scrub_func](dp, bp, &zb));
}

/* ARGSUSED */
static void
traverse_zil_record(zilog_t *zilog, lr_t *lrc, void *arg, uint64_t claim_txg)
{
	if (lrc->lrc_txtype == TX_WRITE) {
		zil_traverse_arg_t *zta = arg;
		dsl_pool_t *dp = zta->zta_dp;
		zil_header_t *zh = zta->zta_zh;
		lr_write_t *lr = (lr_write_t *)lrc;
		blkptr_t *bp = &lr->lr_blkptr;
		zbookmark_t zb;

		if (bp->blk_birth <= dp->dp_scrub_min_txg)
			return;

		if (claim_txg == 0 || bp->blk_birth < claim_txg)
			return;

		zb.zb_objset = zh->zh_log.blk_cksum.zc_word[ZIL_ZC_OBJSET];
		zb.zb_object = lr->lr_foid;
		zb.zb_level = BP_GET_LEVEL(bp);
		zb.zb_blkid = lr->lr_offset / BP_GET_LSIZE(bp);
		VERIFY(0 == scrub_funcs[dp->dp_scrub_func](dp, bp, &zb));
	}
}

static void
traverse_zil(dsl_pool_t *dp, zil_header_t *zh)
{
	uint64_t claim_txg = zh->zh_claim_txg;
	zil_traverse_arg_t zta = { dp, zh };
	zilog_t *zilog;

	/*
	 * We only want to visit blocks that have been claimed but not yet
	 * replayed (or, in read-only mode, blocks that *would* be claimed).
	 */
	if (claim_txg == 0 && spa_writeable(dp->dp_spa))
		return;

	zilog = zil_alloc(dp->dp_meta_objset, zh);

	(void) zil_parse(zilog, traverse_zil_block, traverse_zil_record, &zta,
	    claim_txg);

	zil_free(zilog);
}

static void
scrub_visitbp(dsl_pool_t *dp, dnode_phys_t *dnp,
    arc_buf_t *pbuf, blkptr_t *bp, const zbookmark_t *zb)
{
	int err;
	arc_buf_t *buf = NULL;

	if (bp->blk_birth <= dp->dp_scrub_min_txg)
		return;

	if (scrub_pause(dp, zb))
		return;

	if (!bookmark_is_zero(&dp->dp_scrub_bookmark)) {
		/*
		 * If we already visited this bp & everything below (in
		 * a prior txg), don't bother doing it again.
		 */
		if (bookmark_is_before(dnp, zb, &dp->dp_scrub_bookmark))
			return;

		/*
		 * If we found the block we're trying to resume from, or
		 * we went past it to a different object, zero it out to
		 * indicate that it's OK to start checking for pausing
		 * again.
		 */
		if (bcmp(zb, &dp->dp_scrub_bookmark, sizeof (*zb)) == 0 ||
		    zb->zb_object > dp->dp_scrub_bookmark.zb_object) {
			dprintf("resuming at %llx/%llx/%llx/%llx\n",
			    (longlong_t)zb->zb_objset,
			    (longlong_t)zb->zb_object,
			    (longlong_t)zb->zb_level,
			    (longlong_t)zb->zb_blkid);
			bzero(&dp->dp_scrub_bookmark, sizeof (*zb));
		}
	}

	if (BP_GET_LEVEL(bp) > 0) {
		uint32_t flags = ARC_WAIT;
		int i;
		blkptr_t *cbp;
		int epb = BP_GET_LSIZE(bp) >> SPA_BLKPTRSHIFT;

		err = arc_read(NULL, dp->dp_spa, bp, pbuf,
		    arc_getbuf_func, &buf,
		    ZIO_PRIORITY_ASYNC_READ, ZIO_FLAG_CANFAIL, &flags, zb);
		if (err) {
			mutex_enter(&dp->dp_spa->spa_scrub_lock);
			dp->dp_spa->spa_scrub_errors++;
			mutex_exit(&dp->dp_spa->spa_scrub_lock);
			return;
		}
		cbp = buf->b_data;

		for (i = 0; i < epb; i++, cbp++) {
			zbookmark_t czb;

			SET_BOOKMARK(&czb, zb->zb_objset, zb->zb_object,
			    zb->zb_level - 1,
			    zb->zb_blkid * epb + i);
			scrub_visitbp(dp, dnp, buf, cbp, &czb);
		}
	} else if (BP_GET_TYPE(bp) == DMU_OT_DNODE) {
		uint32_t flags = ARC_WAIT;
		dnode_phys_t *child_dnp;
		int i, j;
		int epb = BP_GET_LSIZE(bp) >> DNODE_SHIFT;

		err = arc_read(NULL, dp->dp_spa, bp, pbuf,
		    arc_getbuf_func, &buf,
		    ZIO_PRIORITY_ASYNC_READ, ZIO_FLAG_CANFAIL, &flags, zb);
		if (err) {
			mutex_enter(&dp->dp_spa->spa_scrub_lock);
			dp->dp_spa->spa_scrub_errors++;
			mutex_exit(&dp->dp_spa->spa_scrub_lock);
			return;
		}
		child_dnp = buf->b_data;

		for (i = 0; i < epb; i++, child_dnp++) {
			for (j = 0; j < child_dnp->dn_nblkptr; j++) {
				zbookmark_t czb;

				SET_BOOKMARK(&czb, zb->zb_objset,
				    zb->zb_blkid * epb + i,
				    child_dnp->dn_nlevels - 1, j);
				scrub_visitbp(dp, child_dnp, buf,
				    &child_dnp->dn_blkptr[j], &czb);
			}
		}
	} else if (BP_GET_TYPE(bp) == DMU_OT_OBJSET) {
		uint32_t flags = ARC_WAIT;
		objset_phys_t *osp;
		int j;

		err = arc_read_nolock(NULL, dp->dp_spa, bp,
		    arc_getbuf_func, &buf,
		    ZIO_PRIORITY_ASYNC_READ, ZIO_FLAG_CANFAIL, &flags, zb);
		if (err) {
			mutex_enter(&dp->dp_spa->spa_scrub_lock);
			dp->dp_spa->spa_scrub_errors++;
			mutex_exit(&dp->dp_spa->spa_scrub_lock);
			return;
		}

		osp = buf->b_data;

		traverse_zil(dp, &osp->os_zil_header);

		for (j = 0; j < osp->os_meta_dnode.dn_nblkptr; j++) {
			zbookmark_t czb;

			SET_BOOKMARK(&czb, zb->zb_objset, 0,
			    osp->os_meta_dnode.dn_nlevels - 1, j);
			scrub_visitbp(dp, &osp->os_meta_dnode, buf,
			    &osp->os_meta_dnode.dn_blkptr[j], &czb);
		}
	}

	(void) scrub_funcs[dp->dp_scrub_func](dp, bp, zb);
	if (buf)
		(void) arc_buf_remove_ref(buf, &buf);
}

static void
scrub_visit_rootbp(dsl_pool_t *dp, dsl_dataset_t *ds, blkptr_t *bp)
{
	zbookmark_t zb;

	SET_BOOKMARK(&zb, ds ? ds->ds_object : 0, 0, -1, 0);
	scrub_visitbp(dp, NULL, NULL, bp, &zb);
}

void
dsl_pool_ds_destroyed(dsl_dataset_t *ds, dmu_tx_t *tx)
{
	dsl_pool_t *dp = ds->ds_dir->dd_pool;

	if (dp->dp_scrub_func == SCRUB_FUNC_NONE)
		return;

	if (dp->dp_scrub_bookmark.zb_objset == ds->ds_object) {
		SET_BOOKMARK(&dp->dp_scrub_bookmark, -1, 0, 0, 0);
	} else if (zap_remove_int(dp->dp_meta_objset, dp->dp_scrub_queue_obj,
	    ds->ds_object, tx) != 0) {
		return;
	}

	if (ds->ds_phys->ds_next_snap_obj != 0) {
		VERIFY(zap_add_int(dp->dp_meta_objset, dp->dp_scrub_queue_obj,
		    ds->ds_phys->ds_next_snap_obj, tx) == 0);
	}
	ASSERT3U(ds->ds_phys->ds_num_children, <=, 1);
}

void
dsl_pool_ds_snapshotted(dsl_dataset_t *ds, dmu_tx_t *tx)
{
	dsl_pool_t *dp = ds->ds_dir->dd_pool;

	if (dp->dp_scrub_func == SCRUB_FUNC_NONE)
		return;

	ASSERT(ds->ds_phys->ds_prev_snap_obj != 0);

	if (dp->dp_scrub_bookmark.zb_objset == ds->ds_object) {
		dp->dp_scrub_bookmark.zb_objset =
		    ds->ds_phys->ds_prev_snap_obj;
	} else if (zap_remove_int(dp->dp_meta_objset, dp->dp_scrub_queue_obj,
	    ds->ds_object, tx) == 0) {
		VERIFY(zap_add_int(dp->dp_meta_objset, dp->dp_scrub_queue_obj,
		    ds->ds_phys->ds_prev_snap_obj, tx) == 0);
	}
}

void
dsl_pool_ds_clone_swapped(dsl_dataset_t *ds1, dsl_dataset_t *ds2, dmu_tx_t *tx)
{
	dsl_pool_t *dp = ds1->ds_dir->dd_pool;

	if (dp->dp_scrub_func == SCRUB_FUNC_NONE)
		return;

	if (dp->dp_scrub_bookmark.zb_objset == ds1->ds_object) {
		dp->dp_scrub_bookmark.zb_objset = ds2->ds_object;
	} else if (dp->dp_scrub_bookmark.zb_objset == ds2->ds_object) {
		dp->dp_scrub_bookmark.zb_objset = ds1->ds_object;
	}

	if (zap_remove_int(dp->dp_meta_objset, dp->dp_scrub_queue_obj,
	    ds1->ds_object, tx) == 0) {
		int err = zap_add_int(dp->dp_meta_objset,
		    dp->dp_scrub_queue_obj, ds2->ds_object, tx);
		VERIFY(err == 0 || err == EEXIST);
		if (err == EEXIST) {
			/* Both were there to begin with */
			VERIFY(0 == zap_add_int(dp->dp_meta_objset,
			    dp->dp_scrub_queue_obj, ds1->ds_object, tx));
		}
	} else if (zap_remove_int(dp->dp_meta_objset, dp->dp_scrub_queue_obj,
	    ds2->ds_object, tx) == 0) {
		VERIFY(0 == zap_add_int(dp->dp_meta_objset,
		    dp->dp_scrub_queue_obj, ds1->ds_object, tx));
	}
}

struct enqueue_clones_arg {
	dmu_tx_t *tx;
	uint64_t originobj;
};

/* ARGSUSED */
static int
enqueue_clones_cb(spa_t *spa, uint64_t dsobj, const char *dsname, void *arg)
{
	struct enqueue_clones_arg *eca = arg;
	dsl_dataset_t *ds;
	int err;
	dsl_pool_t *dp;

	err = dsl_dataset_hold_obj(spa->spa_dsl_pool, dsobj, FTAG, &ds);
	if (err)
		return (err);
	dp = ds->ds_dir->dd_pool;

	if (ds->ds_dir->dd_phys->dd_origin_obj == eca->originobj) {
		while (ds->ds_phys->ds_prev_snap_obj != eca->originobj) {
			dsl_dataset_t *prev;
			err = dsl_dataset_hold_obj(dp,
			    ds->ds_phys->ds_prev_snap_obj, FTAG, &prev);

			dsl_dataset_rele(ds, FTAG);
			if (err)
				return (err);
			ds = prev;
		}
		VERIFY(zap_add_int(dp->dp_meta_objset, dp->dp_scrub_queue_obj,
		    ds->ds_object, eca->tx) == 0);
	}
	dsl_dataset_rele(ds, FTAG);
	return (0);
}

static void
scrub_visitds(dsl_pool_t *dp, uint64_t dsobj, dmu_tx_t *tx)
{
	dsl_dataset_t *ds;
	uint64_t min_txg_save;

	VERIFY3U(0, ==, dsl_dataset_hold_obj(dp, dsobj, FTAG, &ds));

	/*
	 * Iterate over the bps in this ds.
	 */
	min_txg_save = dp->dp_scrub_min_txg;
	dp->dp_scrub_min_txg =
	    MAX(dp->dp_scrub_min_txg, ds->ds_phys->ds_prev_snap_txg);
	scrub_visit_rootbp(dp, ds, &ds->ds_phys->ds_bp);
	dp->dp_scrub_min_txg = min_txg_save;

	if (dp->dp_scrub_pausing)
		goto out;

	/*
	 * Add descendent datasets to work queue.
	 */
	if (ds->ds_phys->ds_next_snap_obj != 0) {
		VERIFY(zap_add_int(dp->dp_meta_objset, dp->dp_scrub_queue_obj,
		    ds->ds_phys->ds_next_snap_obj, tx) == 0);
	}
	if (ds->ds_phys->ds_num_children > 1) {
		if (spa_version(dp->dp_spa) < SPA_VERSION_DSL_SCRUB) {
			struct enqueue_clones_arg eca;
			eca.tx = tx;
			eca.originobj = ds->ds_object;

			(void) dmu_objset_find_spa(ds->ds_dir->dd_pool->dp_spa,
			    NULL, enqueue_clones_cb, &eca, DS_FIND_CHILDREN);
		} else {
			VERIFY(zap_join(dp->dp_meta_objset,
			    ds->ds_phys->ds_next_clones_obj,
			    dp->dp_scrub_queue_obj, tx) == 0);
		}
	}

out:
	dsl_dataset_rele(ds, FTAG);
}

/* ARGSUSED */
static int
enqueue_cb(spa_t *spa, uint64_t dsobj, const char *dsname, void *arg)
{
	dmu_tx_t *tx = arg;
	dsl_dataset_t *ds;
	int err;
	dsl_pool_t *dp;

	err = dsl_dataset_hold_obj(spa->spa_dsl_pool, dsobj, FTAG, &ds);
	if (err)
		return (err);

	dp = ds->ds_dir->dd_pool;

	while (ds->ds_phys->ds_prev_snap_obj != 0) {
		dsl_dataset_t *prev;
		err = dsl_dataset_hold_obj(dp, ds->ds_phys->ds_prev_snap_obj,
		    FTAG, &prev);
		if (err) {
			dsl_dataset_rele(ds, FTAG);
			return (err);
		}

		/*
		 * If this is a clone, we don't need to worry about it for now.
		 */
		if (prev->ds_phys->ds_next_snap_obj != ds->ds_object) {
			dsl_dataset_rele(ds, FTAG);
			dsl_dataset_rele(prev, FTAG);
			return (0);
		}
		dsl_dataset_rele(ds, FTAG);
		ds = prev;
	}

	VERIFY(zap_add_int(dp->dp_meta_objset, dp->dp_scrub_queue_obj,
	    ds->ds_object, tx) == 0);
	dsl_dataset_rele(ds, FTAG);
	return (0);
}

void
dsl_pool_scrub_sync(dsl_pool_t *dp, dmu_tx_t *tx)
{
	spa_t *spa = dp->dp_spa;
	zap_cursor_t zc;
	zap_attribute_t za;
	boolean_t complete = B_TRUE;

	if (dp->dp_scrub_func == SCRUB_FUNC_NONE)
		return;

	/*
	 * If the pool is not loaded, or is trying to unload, leave it alone.
	 */
	if (spa->spa_load_state != SPA_LOAD_NONE || spa_shutting_down(spa))
		return;

	if (dp->dp_scrub_restart) {
		enum scrub_func func = dp->dp_scrub_func;
		dp->dp_scrub_restart = B_FALSE;
		dsl_pool_scrub_setup_sync(dp, &func, kcred, tx);
	}

	if (spa->spa_root_vdev->vdev_stat.vs_scrub_type == 0) {
		/*
		 * We must have resumed after rebooting; reset the vdev
		 * stats to know that we're doing a scrub (although it
		 * will think we're just starting now).
		 */
		vdev_scrub_stat_update(spa->spa_root_vdev,
		    dp->dp_scrub_min_txg ? POOL_SCRUB_RESILVER :
		    POOL_SCRUB_EVERYTHING, B_FALSE);
	}

	dp->dp_scrub_pausing = B_FALSE;
	dp->dp_scrub_start_time = lbolt64;
	dp->dp_scrub_isresilver = (dp->dp_scrub_min_txg != 0);
	spa->spa_scrub_active = B_TRUE;

	if (dp->dp_scrub_bookmark.zb_objset == 0) {
		/* First do the MOS & ORIGIN */
		scrub_visit_rootbp(dp, NULL, &dp->dp_meta_rootbp);
		if (dp->dp_scrub_pausing)
			goto out;

		if (spa_version(spa) < SPA_VERSION_DSL_SCRUB) {
			VERIFY(0 == dmu_objset_find_spa(spa,
			    NULL, enqueue_cb, tx, DS_FIND_CHILDREN));
		} else {
			scrub_visitds(dp, dp->dp_origin_snap->ds_object, tx);
		}
		ASSERT(!dp->dp_scrub_pausing);
	} else if (dp->dp_scrub_bookmark.zb_objset != -1ULL) {
		/*
		 * If we were paused, continue from here.  Note if the
		 * ds we were paused on was deleted, the zb_objset will
		 * be -1, so we will skip this and find a new objset
		 * below.
		 */
		scrub_visitds(dp, dp->dp_scrub_bookmark.zb_objset, tx);
		if (dp->dp_scrub_pausing)
			goto out;
	}

	/*
	 * In case we were paused right at the end of the ds, zero the
	 * bookmark so we don't think that we're still trying to resume.
	 */
	bzero(&dp->dp_scrub_bookmark, sizeof (zbookmark_t));

	/* keep pulling things out of the zap-object-as-queue */
	while (zap_cursor_init(&zc, dp->dp_meta_objset, dp->dp_scrub_queue_obj),
	    zap_cursor_retrieve(&zc, &za) == 0) {
		VERIFY(0 == zap_remove(dp->dp_meta_objset,
		    dp->dp_scrub_queue_obj, za.za_name, tx));
		scrub_visitds(dp, za.za_first_integer, tx);
		if (dp->dp_scrub_pausing)
			break;
		zap_cursor_fini(&zc);
	}
	zap_cursor_fini(&zc);
	if (dp->dp_scrub_pausing)
		goto out;

	/* done. */

	dsl_pool_scrub_cancel_sync(dp, &complete, kcred, tx);
	return;
out:
	VERIFY(0 == zap_update(dp->dp_meta_objset,
	    DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_SCRUB_BOOKMARK, sizeof (uint64_t), 4,
	    &dp->dp_scrub_bookmark, tx));
	VERIFY(0 == zap_update(dp->dp_meta_objset,
	    DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_SCRUB_ERRORS, sizeof (uint64_t), 1,
	    &spa->spa_scrub_errors, tx));

	/* XXX this is scrub-clean specific */
	mutex_enter(&spa->spa_scrub_lock);
	while (spa->spa_scrub_inflight > 0)
		cv_wait(&spa->spa_scrub_io_cv, &spa->spa_scrub_lock);
	mutex_exit(&spa->spa_scrub_lock);
}

void
dsl_pool_scrub_restart(dsl_pool_t *dp)
{
	mutex_enter(&dp->dp_scrub_cancel_lock);
	dp->dp_scrub_restart = B_TRUE;
	mutex_exit(&dp->dp_scrub_cancel_lock);
}

/*
 * scrub consumers
 */

static void
count_block(zfs_all_blkstats_t *zab, const blkptr_t *bp)
{
	int i;

	/*
	 * If we resume after a reboot, zab will be NULL; don't record
	 * incomplete stats in that case.
	 */
	if (zab == NULL)
		return;

	for (i = 0; i < 4; i++) {
		int l = (i < 2) ? BP_GET_LEVEL(bp) : DN_MAX_LEVELS;
		int t = (i & 1) ? BP_GET_TYPE(bp) : DMU_OT_TOTAL;
		zfs_blkstat_t *zb = &zab->zab_type[l][t];
		int equal;

		zb->zb_count++;
		zb->zb_asize += BP_GET_ASIZE(bp);
		zb->zb_lsize += BP_GET_LSIZE(bp);
		zb->zb_psize += BP_GET_PSIZE(bp);
		zb->zb_gangs += BP_COUNT_GANG(bp);

		switch (BP_GET_NDVAS(bp)) {
		case 2:
			if (DVA_GET_VDEV(&bp->blk_dva[0]) ==
			    DVA_GET_VDEV(&bp->blk_dva[1]))
				zb->zb_ditto_2_of_2_samevdev++;
			break;
		case 3:
			equal = (DVA_GET_VDEV(&bp->blk_dva[0]) ==
			    DVA_GET_VDEV(&bp->blk_dva[1])) +
			    (DVA_GET_VDEV(&bp->blk_dva[0]) ==
			    DVA_GET_VDEV(&bp->blk_dva[2])) +
			    (DVA_GET_VDEV(&bp->blk_dva[1]) ==
			    DVA_GET_VDEV(&bp->blk_dva[2]));
			if (equal == 1)
				zb->zb_ditto_2_of_3_samevdev++;
			else if (equal == 3)
				zb->zb_ditto_3_of_3_samevdev++;
			break;
		}
	}
}

static void
dsl_pool_scrub_clean_done(zio_t *zio)
{
	spa_t *spa = zio->io_spa;

	zio_data_buf_free(zio->io_data, zio->io_size);

	mutex_enter(&spa->spa_scrub_lock);
	spa->spa_scrub_inflight--;
	cv_broadcast(&spa->spa_scrub_io_cv);

	if (zio->io_error && (zio->io_error != ECKSUM ||
	    !(zio->io_flags & ZIO_FLAG_SPECULATIVE)))
		spa->spa_scrub_errors++;
	mutex_exit(&spa->spa_scrub_lock);
}

static int
dsl_pool_scrub_clean_cb(dsl_pool_t *dp,
    const blkptr_t *bp, const zbookmark_t *zb)
{
	size_t size = BP_GET_PSIZE(bp);
	spa_t *spa = dp->dp_spa;
	boolean_t needs_io;
	int zio_flags = ZIO_FLAG_SCRUB_THREAD | ZIO_FLAG_RAW | ZIO_FLAG_CANFAIL;
	int zio_priority;

	ASSERT(bp->blk_birth > dp->dp_scrub_min_txg);

	if (bp->blk_birth >= dp->dp_scrub_max_txg)
		return (0);

	count_block(dp->dp_blkstats, bp);

	if (dp->dp_scrub_isresilver == 0) {
		/* It's a scrub */
		zio_flags |= ZIO_FLAG_SCRUB;
		zio_priority = ZIO_PRIORITY_SCRUB;
		needs_io = B_TRUE;
	} else {
		/* It's a resilver */
		zio_flags |= ZIO_FLAG_RESILVER;
		zio_priority = ZIO_PRIORITY_RESILVER;
		needs_io = B_FALSE;
	}

	/* If it's an intent log block, failure is expected. */
	if (zb->zb_level == -1 && BP_GET_TYPE(bp) != DMU_OT_OBJSET)
		zio_flags |= ZIO_FLAG_SPECULATIVE;

	for (int d = 0; d < BP_GET_NDVAS(bp); d++) {
		vdev_t *vd = vdev_lookup_top(spa,
		    DVA_GET_VDEV(&bp->blk_dva[d]));

		/*
		 * Keep track of how much data we've examined so that
		 * zpool(1M) status can make useful progress reports.
		 */
		mutex_enter(&vd->vdev_stat_lock);
		vd->vdev_stat.vs_scrub_examined +=
		    DVA_GET_ASIZE(&bp->blk_dva[d]);
		mutex_exit(&vd->vdev_stat_lock);

		/* if it's a resilver, this may not be in the target range */
		if (!needs_io) {
			if (DVA_GET_GANG(&bp->blk_dva[d])) {
				/*
				 * Gang members may be spread across multiple
				 * vdevs, so the best estimate we have is the
				 * scrub range, which has already been checked.
				 * XXX -- it would be better to change our
				 * allocation policy to ensure that all
				 * gang members reside on the same vdev.
				 */
				needs_io = B_TRUE;
			} else {
				needs_io = vdev_dtl_contains(vd, DTL_PARTIAL,
				    bp->blk_birth, 1);
			}
		}
	}

	if (needs_io && !zfs_no_scrub_io) {
		void *data = zio_data_buf_alloc(size);

		mutex_enter(&spa->spa_scrub_lock);
		while (spa->spa_scrub_inflight >= spa->spa_scrub_maxinflight)
			cv_wait(&spa->spa_scrub_io_cv, &spa->spa_scrub_lock);
		spa->spa_scrub_inflight++;
		mutex_exit(&spa->spa_scrub_lock);

		zio_nowait(zio_read(NULL, spa, bp, data, size,
		    dsl_pool_scrub_clean_done, NULL, zio_priority,
		    zio_flags, zb));
	}

	/* do not relocate this block */
	return (0);
}

int
dsl_pool_scrub_clean(dsl_pool_t *dp)
{
	/*
	 * Purge all vdev caches.  We do this here rather than in sync
	 * context because this requires a writer lock on the spa_config
	 * lock, which we can't do from sync context.  The
	 * spa_scrub_reopen flag indicates that vdev_open() should not
	 * attempt to start another scrub.
	 */
	spa_config_enter(dp->dp_spa, SCL_ALL, FTAG, RW_WRITER);
	dp->dp_spa->spa_scrub_reopen = B_TRUE;
	vdev_reopen(dp->dp_spa->spa_root_vdev);
	dp->dp_spa->spa_scrub_reopen = B_FALSE;
	spa_config_exit(dp->dp_spa, SCL_ALL, FTAG);

	return (dsl_pool_scrub_setup(dp, SCRUB_FUNC_CLEAN));
}
