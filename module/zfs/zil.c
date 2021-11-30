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
 * Copyright (c) 2011, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 * Copyright (c) 2018 Datto Inc.
 */

/* Portions Copyright 2010 Robert Milkowski */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/dmu.h>
#include <sys/zap.h>
#include <sys/arc.h>
#include <sys/stat.h>
#include <sys/zil.h>
#include <sys/zil_impl.h>
#include <sys/zil_lwb.h> /* XXX eliminate the need for this */
#include <sys/dsl_dataset.h>
#include <sys/vdev_impl.h>
#include <sys/dmu_tx.h>
#include <sys/dsl_pool.h>
#include <sys/metaslab.h>
#include <sys/trace_zfs.h>
#include <sys/abd.h>


/*
 * The ZFS Intent Log (ZIL) saves "transaction records" (itxs) of system
 * calls that change the file system. Each itx has enough information to
 * be able to replay them after a system crash, power loss, or
 * equivalent failure mode. These are stored in memory until either:
 *
 *   1. they are committed to the pool by the DMU transaction group
 *      (txg), at which point they can be discarded; or
 *   2. they are committed to the on-disk ZIL for the dataset being
 *      modified (e.g. due to an fsync, O_DSYNC, or other synchronous
 *      requirement).
 *
 * In the event of a crash or power loss, the itxs contained by each
 * dataset's on-disk ZIL will be replayed when that dataset is first
 * instantiated (e.g. if the dataset is a normal filesystem, when it is
 * first mounted).
 *
 * As hinted at above, there is one ZIL per dataset (both the in-memory
 * representation, and the on-disk representation). The on-disk format
 * is documented in zil_lwb.c.
 */


const zil_const_zil_vtable_ptr_t zil_vtables[ZIL_KIND_COUNT] = {
	NULL,			/* ZIL_KIND_UNINIT	*/
	&zillwb_vtable,	/* ZIL_KIND_LWB	*/
	&zilpmem_vtable		/* ZIL_KIND_PMEM	*/
};

static struct {
	int zdk_val;
	rrwlock_t zdk_rwl;
} zil_default_kind;

/*
 * Disable intent logging replay.  This global ZIL switch affects all pools.
 */
int zil_replay_disable = 0;



static inline
boolean_t
zil_header_kind_matches_vtable(const zilog_t *zilog)
{
	zil_vtable_t const *vt = NULL;
	VERIFY0(zil_kind_specific_data_from_header(zilog->zl_spa, zilog->zl_header, NULL, NULL, &vt, NULL));
	return (zilog->zl_vtable == vt);
}


#define	ZIL_VCALL_void(zilog, vfunc, ...) \
	do { \
		zilog_t *zl = zilog; \
		ASSERT(zil_header_kind_matches_vtable(zl)); \
		zl->zl_vtable->vfunc(__VA_ARGS__); \
	} while (0);

#define	ZIL_VCALL_ret(outptr, zilog, vfunc, ...) \
	do { \
		zilog_t *zl = zilog; \
		ASSERT(zil_header_kind_matches_vtable(zl)); \
		*(outptr) = zl->zl_vtable->vfunc(__VA_ARGS__); \
	} while (0);


enum zil_itxg_bypass_stat_id {
	ZIL_ITXG_BYPASS_STAT_WRITE_UPGRADE,
	ZIL_ITXG_BYPASS_STAT_DOWNGRADE,
	ZIL_ITXG_BYPASS_STAT_AQUISITION_TOTAL,
	ZIL_ITXG_BYPASS_STAT_VTABLE,
	ZIL_ITXG_BYPASS_STAT_EXIT,
	ZIL_ITXG_BYPASS_STAT_TOTAL,
	ZIL_ITXG_BYPASS_STAT_COMMIT_TOTAL,
	ZIL_ITXG_BYPASS_STAT_COMMIT_AQUIRE,
	ZIL_ITXG_BYPASS_STAT__COUNT,
};

struct zfs_percpu_counter_stat zil_itxg_bypass_stats[ZIL_ITXG_BYPASS_STAT__COUNT] = {
	{ZIL_ITXG_BYPASS_STAT_WRITE_UPGRADE, "assign__write_upgrade" },
	{ZIL_ITXG_BYPASS_STAT_DOWNGRADE, "assign__downgrade" },
	{ZIL_ITXG_BYPASS_STAT_AQUISITION_TOTAL, "assign__aquisition_total" },
	{ZIL_ITXG_BYPASS_STAT_VTABLE, "assign__vtable" },
	{ZIL_ITXG_BYPASS_STAT_EXIT, "assign__exit" },
	{ZIL_ITXG_BYPASS_STAT_TOTAL, "assign__total" },
	{ZIL_ITXG_BYPASS_STAT_COMMIT_TOTAL, "commit__total"},
	{ZIL_ITXG_BYPASS_STAT_COMMIT_AQUIRE, "commit__aquire"},
};
struct zfs_percpu_counter_statset zil_itxg_bypass_statset = {
	.kstat_name = "zil_itxg_bypass",
	.ncounters = ZIL_ITXG_BYPASS_STAT__COUNT,
	.counters = zil_itxg_bypass_stats,
};


/*
 * Called when we create in-memory log transactions so that we know
 * to cleanup the itxs at the end of spa_sync().
 */
static void
zilog_dirty(zilog_t *zilog, uint64_t txg)
{
	dsl_pool_t *dp = zilog->zl_dmu_pool;
	dsl_dataset_t *ds = dmu_objset_ds(zilog->zl_os);

	ASSERT(spa_writeable(zilog->zl_spa));

	if (ds->ds_is_snapshot)
		panic("dirtying snapshot!");

	if (txg_list_add(&dp->dp_dirty_zilogs, zilog, txg)) {
		/* up the hold count until we can be written out */
		dmu_buf_add_ref(ds->ds_dbuf, zilog);

		/* XXX review locking, this seems to be protected by itxg_lock but we have 4 of those...  */
		zilog->zl_dirty_max_txg = MAX(txg, zilog->zl_dirty_max_txg);
	}
}

/*
 * Determine if the zil is dirty in the specified txg. Callers wanting to
 * ensure that the dirty state does not change must hold the itxg_lock for
 * the specified txg. Holding the lock will ensure that the zil cannot be
 * dirtied (zil_itx_assign) or cleaned (zil_clean) while we check its current
 * state.
 */
static boolean_t __maybe_unused
zilog_is_dirty_in_txg(zilog_t *zilog, uint64_t txg)
{
	dsl_pool_t *dp = zilog->zl_dmu_pool;

	if (txg_list_member(&dp->dp_dirty_zilogs, zilog, txg & TXG_MASK))
		return (B_TRUE);
	return (B_FALSE);
}

/*
 * Determine if the zil is dirty. The zil is considered dirty if it has
 * any pending itx records that have not been cleaned by zil_clean().
 */
boolean_t
zilog_is_dirty(zilog_t *zilog)
{
	dsl_pool_t *dp = zilog->zl_dmu_pool;

	for (int t = 0; t < TXG_SIZE; t++) {
		if (txg_list_member(&dp->dp_dirty_zilogs, zilog, t))
			return (B_TRUE);
	}
	return (B_FALSE);
}

itx_t *
zil_itx_create(uint64_t txtype, size_t olrsize)
{
	size_t itxsize, lrsize;
	itx_t *itx;

	lrsize = P2ROUNDUP_TYPED(olrsize, sizeof (uint64_t), size_t);
	itxsize = offsetof(itx_t, itx_lr) + lrsize;

	itx = zio_data_buf_alloc(itxsize);
	itx->itx_lr.lrc_txtype = txtype;
	itx->itx_lr.lrc_reclen = lrsize;
	itx->itx_lr.lrc_seq = 0;	/* defensive */
	bzero((char *)&itx->itx_lr + olrsize, lrsize - olrsize);
	itx->itx_sync = B_TRUE;		/* default is synchronous */
	itx->itx_callback = NULL;
	itx->itx_callback_data = NULL;
	itx->itx_size = itxsize;

	return (itx);
}

void
zil_itx_free_do_not_run_callback(itx_t *itx)
{
	zio_data_buf_free(itx, itx->itx_size);
}

void
zil_itx_destroy(itx_t *itx)
{
	IMPLY(itx->itx_lr.lrc_txtype == TX_COMMIT, itx->itx_callback == NULL);
	IMPLY(itx->itx_callback != NULL, itx->itx_lr.lrc_txtype != TX_COMMIT);

	if (itx->itx_callback != NULL)
		itx->itx_callback(itx->itx_callback_data);

	zil_itx_free_do_not_run_callback(itx);
}

/*
 * Free up the sync and async itxs. The itxs_t has already been detached
 * so no locks are needed.
 */
static void
zil_itxg_clean(void *arg)
{
	itx_t *itx;
	list_t *list;
	avl_tree_t *t;
	void *cookie;
	itxs_t *itxs = arg;
	itx_async_node_t *ian;

	list = &itxs->i_sync_list;
	while ((itx = list_head(list)) != NULL) {
		/*
		 * In the general case, commit itxs will not be found
		 * here, as they'll be committed to an lwb via
		 * zillwb_lwb_commit(), and free'd in that function. Having
		 * said that, it is still possible for commit itxs to be
		 * found here, due to the following race:
		 *
		 *	- a thread calls zil_commit() which assigns the
		 *	  commit itx to a per-txg i_sync_list
		 *	- zil_itxg_clean() is called (e.g. via spa_sync())
		 *	  while the waiter is still on the i_sync_list
		 *
		 * There's nothing to prevent syncing the txg while the
		 * waiter is on the i_sync_list. This normally doesn't
		 * happen because spa_sync() is slower than zil_commit(),
		 * but if zil_commit() calls txg_wait_synced() (e.g.
		 * because zil_create() or zillwb_commit_writer_stall() is
		 * called) we will hit this case.
		 */
		if (itx->itx_lr.lrc_txtype == TX_COMMIT)
			zillwb_commit_waiter_skip(itx->itx_private);

		list_remove(list, itx);
		zil_itx_destroy(itx);
	}

	cookie = NULL;
	t = &itxs->i_async_tree;
	while ((ian = avl_destroy_nodes(t, &cookie)) != NULL) {
		list = &ian->ia_list;
		while ((itx = list_head(list)) != NULL) {
			list_remove(list, itx);
			/* commit itxs should never be on the async lists. */
			ASSERT3U(itx->itx_lr.lrc_txtype, !=, TX_COMMIT);
			zil_itx_destroy(itx);
		}
		list_destroy(list);
		kmem_free(ian, sizeof (itx_async_node_t));
	}
	avl_destroy(t);

	kmem_free(itxs, sizeof (itxs_t));
}

static int
zil_aitx_compare(const void *x1, const void *x2)
{
	const uint64_t o1 = ((itx_async_node_t *)x1)->ia_foid;
	const uint64_t o2 = ((itx_async_node_t *)x2)->ia_foid;

	return (TREE_CMP(o1, o2));
}

/*
 * Remove all async itx with the given oid.
 */
void
zil_remove_async(zilog_t *zilog, uint64_t oid)
{
	uint64_t otxg, txg;
	itx_async_node_t *ian;
	avl_tree_t *t;
	avl_index_t where;
	list_t clean_list;
	itx_t *itx;

	ASSERT(oid != 0);
	list_create(&clean_list, sizeof (itx_t), offsetof(itx_t, itx_node));

	if (spa_freeze_txg(zilog->zl_spa) != UINT64_MAX) /* ziltest support */
		otxg = ZILTEST_TXG;
	else
		otxg = spa_last_synced_txg(zilog->zl_spa) + 1;

	for (txg = otxg; txg < (otxg + TXG_CONCURRENT_STATES); txg++) {
		itxg_t *itxg = &zilog->zl_itxg[txg & TXG_MASK];

		mutex_enter(&itxg->itxg_lock);
		if (itxg->itxg_txg != txg) {
			mutex_exit(&itxg->itxg_lock);
			continue;
		}

		/*
		 * Locate the object node and append its list.
		 */
		t = &itxg->itxg_itxs->i_async_tree;
		ian = avl_find(t, &oid, &where);
		if (ian != NULL)
			list_move_tail(&clean_list, &ian->ia_list);
		mutex_exit(&itxg->itxg_lock);
	}
	while ((itx = list_head(&clean_list)) != NULL) {
		list_remove(&clean_list, itx);
		/* commit itxs should never be on the async lists. */
		ASSERT3U(itx->itx_lr.lrc_txtype, !=, TX_COMMIT);
		zil_itx_destroy(itx);
	}
	list_destroy(&clean_list);
}

/*
 * Returns B_TRUE if the zil kind supports WR_INDIRECT lr_write_t / itxs.
 * Returns B_FALSE otherwise.
 */
boolean_t
zil_supports_wr_indirect(zilog_t *zilog)
{
	return (zilog->zl_vtable->zlvt_supports_wr_indirect);
}

boolean_t zil_lr_is_indirect_write(const lr_t *lr)
{
	uint64_t txtype = lr->lrc_txtype & (~TX_CI);
	const lr_write_t *lrw = (const lr_write_t *)lr;
	if (txtype == TX_WRITE) {
		return (BP_IS_HOLE(&lrw->lr_blkptr) ? B_FALSE : B_TRUE);
	} else {
		if (txtype == TX_WRITE2) {
			ASSERT(BP_IS_HOLE(&lrw->lr_blkptr)); /* FIXME shouldn't panic here, it's on-disk input */
		}
		return (B_FALSE);
	}
}

/*
 * - Panics if itx is WR_INDIRECT and !zil_supports_wr_indirect.
 * - Panics if itx is WR_COPIED and lr_length > zil_max_copied_data.
 */
uint64_t
zil_itx_assign(zilog_t *zilog, itx_t *itx, dmu_tx_t *tx)
{
	uint64_t txg;
	itxg_t *itxg;
	itxs_t *itxs, *clean = NULL;

	const lr_t *lrc = &itx->itx_lr;
	IMPLY((lrc->lrc_txtype == TX_WRITE && itx->itx_wr_state == WR_INDIRECT),
	    (zilog->zl_vtable->zlvt_supports_wr_indirect));
	const lr_write_t *lrw = (const lr_write_t *)&itx->itx_lr;
	IMPLY((lrc->lrc_txtype == TX_WRITE && itx->itx_wr_state == WR_COPIED),
	    (lrw->lr_length <= zil_max_copied_data(zilog)));


	itx->itx_lr.lrc_txg = dmu_tx_get_txg(tx);

	const enum zil_itxg_bypass_mode bypass_mode = zilog->zl_itxg_bypass.mode.mode;
	if (bypass_mode != ZIL_ITXG_BYPASS_MODE_DISABLED) {
		VERIFY(bypass_mode == ZIL_ITXG_BYPASS_MODE_CORRECT || bypass_mode == ZIL_ITXG_BYPASS_MODE_NOSERIALIZATION_INCORRECT_FOR_EVALUATION_ONLY);
		ASSERT(zilog->zl_vtable->zlvt_itxg_bypass);

		// FIXME: ASSERT that all zl_itxg are empty (we don't provide mechanismss to switch zl_itxg_bypass.enabled after allocation)

		krwlock_t *rwlp = &zilog->zl_itxg_bypass.rwl;

		hrtime_t stat_pre_exit = 0;
		hrtime_t stat_post_exit = 0;
		hrtime_t stat_pre_vtable_call = 0;
		hrtime_t stat_post_vtable_call = 0;
		hrtime_t stat_dontneed_write_pre_dowgrade = 0;
		hrtime_t stat_post_write_aquire = 0;
		hrtime_t stat_started = gethrtime();
		if (bypass_mode == ZIL_ITXG_BYPASS_MODE_CORRECT)
			rw_enter(rwlp, RW_READER);
		hrtime_t stat_read_aquired = gethrtime();
retry:
		(void) 0;

		boolean_t depends_on_any_previously_written_entry;
		boolean_t ooo_flank;
		boolean_t this_txtype_ooo;
		if (bypass_mode == ZIL_ITXG_BYPASS_MODE_CORRECT) {
			this_txtype_ooo = TX_OOO(itx->itx_lr.lrc_txtype);
			ooo_flank = zilog->zl_itxg_bypass.last_txtype_ooo != this_txtype_ooo;
			depends_on_any_previously_written_entry =
				(zilog->zl_itxg_bypass.zil_commit_called || ooo_flank);

			/* rwl serves two purposes:
			* (1) protect zl_itxg_bypass data from concurrent udpates
			* (2) ensure that only one thread at a time calls zlvt_itx_bypass()
			*     iff depends_on_any_previously_written_entry. It's our
			*     job as a caller to guarantee this, see doc omment of
			*     zlvt_itx_bypass().
			*/
			const boolean_t hold_rwl_write_iff = depends_on_any_previously_written_entry || ooo_flank || zilog->zl_itxg_bypass.zil_commit_called;
			if (hold_rwl_write_iff) {
				if (!RW_WRITE_HELD(rwlp)) {
					VERIFY0(stat_post_write_aquire); // only once on this code path
					if (!rw_tryupgrade(rwlp)) {
						rw_exit(rwlp);
						rw_enter(rwlp, RW_WRITER);
						stat_post_write_aquire = gethrtime();
						goto retry;
					}
					stat_post_write_aquire = gethrtime();
				}
			}
			if (!hold_rwl_write_iff && RW_WRITE_HELD(rwlp)) {
				stat_dontneed_write_pre_dowgrade = gethrtime();
				rw_downgrade(rwlp);
			}
			ASSERT(RW_LOCK_HELD(rwlp));
			EQUIV(hold_rwl_write_iff, RW_WRITE_HELD(rwlp));
		} else {
			ASSERT(!RW_LOCK_HELD(rwlp));
			depends_on_any_previously_written_entry = B_FALSE;
		}

		stat_pre_vtable_call = gethrtime();
		uint64_t needs_wait_txg =
		    zilog->zl_vtable->zlvt_itxg_bypass(zilog, tx, itx, depends_on_any_previously_written_entry);
		stat_post_vtable_call = gethrtime();
		if (unlikely(needs_wait_txg)) {
			goto unlock;
		} else {
			zil_itx_destroy(itx);
		}

		if (bypass_mode == ZIL_ITXG_BYPASS_MODE_CORRECT) {
			if (ooo_flank) {
				ASSERT(RW_WRITE_HELD(rwlp));
				zilog->zl_itxg_bypass.last_txtype_ooo = this_txtype_ooo;
			} else {
				ASSERT3B(zilog->zl_itxg_bypass.last_txtype_ooo, ==, this_txtype_ooo);
			}

			if (zilog->zl_itxg_bypass.zil_commit_called) {
				ASSERT(RW_WRITE_HELD(rwlp));
				zilog->zl_itxg_bypass.zil_commit_called = B_FALSE;
			}
		}

unlock:
		stat_pre_exit = gethrtime();
		if (bypass_mode == ZIL_ITXG_BYPASS_MODE_CORRECT)
			rw_exit(rwlp);
		stat_post_exit = gethrtime();

		// FIXME dedup with zilog_dirty() call below
		/*
		* We don't want to dirty the ZIL using ZILTEST_TXG, because
		* zil_clean() will never be called using ZILTEST_TXG. Thus, we
		* need to be careful to always dirty the ZIL using the "real"
		* TXG (not itxg_txg) even when the SPA is frozen.
		*/
		zilog_dirty(zilog, dmu_tx_get_txg(tx)); /* XXX locking!? */

		hrtime_t stat_ended = gethrtime();

		if (stat_post_write_aquire != 0) {
			zfs_percpu_counter_statset_add(&zil_itxg_bypass_statset, ZIL_ITXG_BYPASS_STAT_WRITE_UPGRADE, stat_post_write_aquire - stat_read_aquired);
		}
		if (stat_dontneed_write_pre_dowgrade != 0) {
			ASSERT(stat_post_write_aquire);
			zfs_percpu_counter_statset_add(&zil_itxg_bypass_statset, ZIL_ITXG_BYPASS_STAT_DOWNGRADE, stat_pre_vtable_call - stat_dontneed_write_pre_dowgrade);
		}
		zfs_percpu_counter_statset_add(&zil_itxg_bypass_statset, ZIL_ITXG_BYPASS_STAT_AQUISITION_TOTAL, stat_pre_vtable_call - stat_started);
		zfs_percpu_counter_statset_add(&zil_itxg_bypass_statset, ZIL_ITXG_BYPASS_STAT_VTABLE, stat_post_vtable_call - stat_pre_vtable_call);
		zfs_percpu_counter_statset_add(&zil_itxg_bypass_statset, ZIL_ITXG_BYPASS_STAT_EXIT, stat_post_exit - stat_pre_exit);
		zfs_percpu_counter_statset_add(&zil_itxg_bypass_statset, ZIL_ITXG_BYPASS_STAT_TOTAL, stat_ended - stat_started);


		return needs_wait_txg; // did all the work, yay
	}

	/*
	 * Ensure the data of a renamed file is committed before the rename.
	 */
	if ((itx->itx_lr.lrc_txtype & ~TX_CI) == TX_RENAME)
		zil_async_to_sync(zilog, itx->itx_oid);

	if (spa_freeze_txg(zilog->zl_spa) != UINT64_MAX)
		txg = ZILTEST_TXG;
	else
		txg = dmu_tx_get_txg(tx);

	itxg = &zilog->zl_itxg[txg & TXG_MASK];
	mutex_enter(&itxg->itxg_lock);
	itxs = itxg->itxg_itxs;
	if (itxg->itxg_txg != txg) {
		if (itxs != NULL) {
			/*
			 * The zil_clean callback hasn't got around to cleaning
			 * this itxg. Save the itxs for release below.
			 * This should be rare.
			 */
			zfs_dbgmsg("zil_itx_assign: missed itx cleanup for "
			    "txg %llu", (u_longlong_t)itxg->itxg_txg);
			clean = itxg->itxg_itxs;
		}
		itxg->itxg_txg = txg;
		itxs = itxg->itxg_itxs = kmem_zalloc(sizeof (itxs_t),
		    KM_SLEEP);

		list_create(&itxs->i_sync_list, sizeof (itx_t),
		    offsetof(itx_t, itx_node));
		avl_create(&itxs->i_async_tree, zil_aitx_compare,
		    sizeof (itx_async_node_t),
		    offsetof(itx_async_node_t, ia_node));
	}
	if (itx->itx_sync) {
		list_insert_tail(&itxs->i_sync_list, itx);
	} else {
		avl_tree_t *t = &itxs->i_async_tree;
		uint64_t foid =
		    LR_FOID_GET_OBJ(((lr_ooo_t *)&itx->itx_lr)->lr_foid);
		itx_async_node_t *ian;
		avl_index_t where;

		ian = avl_find(t, &foid, &where);
		if (ian == NULL) {
			ian = kmem_alloc(sizeof (itx_async_node_t),
			    KM_SLEEP);
			list_create(&ian->ia_list, sizeof (itx_t),
			    offsetof(itx_t, itx_node));
			ian->ia_foid = foid;
			avl_insert(t, ian, where);
		}
		list_insert_tail(&ian->ia_list, itx);
	}


	/*
	 * We don't want to dirty the ZIL using ZILTEST_TXG, because
	 * zil_clean() will never be called using ZILTEST_TXG. Thus, we
	 * need to be careful to always dirty the ZIL using the "real"
	 * TXG (not itxg_txg) even when the SPA is frozen.
	 */
	zilog_dirty(zilog, dmu_tx_get_txg(tx));
	mutex_exit(&itxg->itxg_lock);

	/* Release the old itxs now we've dropped the lock */
	if (clean != NULL)
		zil_itxg_clean(clean);

	return (0);
}

/*
 * If there are any in-memory intent log transactions which have now been
 * synced then start up a taskq to free them. We should only do this after we
 * have written out the uberblocks (i.e. txg has been committed) so that
 * don't inadvertently clean out in-memory log records that would be required
 * by zil_commit().
 */
void
zil_clean(zilog_t *zilog, uint64_t synced_txg)
{
	itxg_t *itxg = &zilog->zl_itxg[synced_txg & TXG_MASK];
	itxs_t *clean_me;

	ASSERT3U(synced_txg, <, ZILTEST_TXG);

	mutex_enter(&itxg->itxg_lock);
	if (itxg->itxg_itxs == NULL || itxg->itxg_txg == ZILTEST_TXG) {
		mutex_exit(&itxg->itxg_lock);
		return;
	}
	ASSERT3U(itxg->itxg_txg, <=, synced_txg);
	ASSERT3U(itxg->itxg_txg, !=, 0);
	clean_me = itxg->itxg_itxs;
	itxg->itxg_itxs = NULL;
	itxg->itxg_txg = 0;
	mutex_exit(&itxg->itxg_lock);
	/*
	 * Preferably start a task queue to free up the old itxs but
	 * if taskq_dispatch can't allocate resources to do that then
	 * free it in-line. This should be rare. Note, using TQ_SLEEP
	 * created a bad performance problem.
	 */
	ASSERT3P(zilog->zl_dmu_pool, !=, NULL);
	ASSERT3P(zilog->zl_dmu_pool->dp_zil_clean_taskq, !=, NULL);
	taskqid_t id = taskq_dispatch(zilog->zl_dmu_pool->dp_zil_clean_taskq,
	    zil_itxg_clean, clean_me, TQ_NOSLEEP);
	if (id == TASKQID_INVALID)
		zil_itxg_clean(clean_me);
}

/*
 * This function will traverse the queue of itxs that need to be
 * committed, and move them to the tail of commit_list
 */
void
zil_fill_commit_list(zilog_t *zilog, list_t *commit_list)
{
	uint64_t otxg, txg;

	if (zilog->zl_vtable == &zillwb_vtable)
		ASSERT(MUTEX_HELD(&zillwb_downcast(zilog)->zl_issuer_lock));

	if (spa_freeze_txg(zilog->zl_spa) != UINT64_MAX) /* ziltest support */
		otxg = ZILTEST_TXG;
	else
		otxg = spa_last_synced_txg(zilog->zl_spa) + 1;

	/*
	 * This is inherently racy, since there is nothing to prevent
	 * the last synced txg from changing. That's okay since we'll
	 * only commit things in the future.
	 */
	for (txg = otxg; txg < (otxg + TXG_CONCURRENT_STATES); txg++) {
		itxg_t *itxg = &zilog->zl_itxg[txg & TXG_MASK];

		mutex_enter(&itxg->itxg_lock);
		if (itxg->itxg_txg != txg) {
			mutex_exit(&itxg->itxg_lock);
			continue;
		}

		/*
		 * If we're adding itx records to the commit list,
		 * then the zil better be dirty in this "txg". We can assert
		 * that here since we're holding the itxg_lock which will
		 * prevent spa_sync from cleaning it. Once we add the itxs
		 * to the commit list we must commit it to disk even
		 * if it's unnecessary (i.e. the txg was synced).
		 */
		ASSERT(zilog_is_dirty_in_txg(zilog, txg) ||
		    spa_freeze_txg(zilog->zl_spa) != UINT64_MAX);
		list_move_tail(commit_list, &itxg->itxg_itxs->i_sync_list);

		mutex_exit(&itxg->itxg_lock);
	}
}

/*
 * Move the async itxs for a specified object to commit into sync lists.
 */
void
zil_async_to_sync(zilog_t *zilog, uint64_t foid)
{
	uint64_t otxg, txg;
	itx_async_node_t *ian;
	avl_tree_t *t;
	avl_index_t where;

	if (spa_freeze_txg(zilog->zl_spa) != UINT64_MAX) /* ziltest support */
		otxg = ZILTEST_TXG;
	else
		otxg = spa_last_synced_txg(zilog->zl_spa) + 1;

	/*
	 * This is inherently racy, since there is nothing to prevent
	 * the last synced txg from changing.
	 */
	for (txg = otxg; txg < (otxg + TXG_CONCURRENT_STATES); txg++) {
		itxg_t *itxg = &zilog->zl_itxg[txg & TXG_MASK];

		mutex_enter(&itxg->itxg_lock);
		if (itxg->itxg_txg != txg) {
			mutex_exit(&itxg->itxg_lock);
			continue;
		}

		/*
		 * If a foid is specified then find that node and append its
		 * list. Otherwise walk the tree appending all the lists
		 * to the sync list. We add to the end rather than the
		 * beginning to ensure the create has happened.
		 */
		t = &itxg->itxg_itxs->i_async_tree;
		if (foid != 0) {
			ian = avl_find(t, &foid, &where);
			if (ian != NULL) {
				list_move_tail(&itxg->itxg_itxs->i_sync_list,
				    &ian->ia_list);
			}
		} else {
			void *cookie = NULL;

			while ((ian = avl_destroy_nodes(t, &cookie)) != NULL) {
				list_move_tail(&itxg->itxg_itxs->i_sync_list,
				    &ian->ia_list);
				list_destroy(&ian->ia_list);
				kmem_free(ian, sizeof (itx_async_node_t));
			}
		}
		mutex_exit(&itxg->itxg_lock);
	}
}

void
zil_set_sync(zilog_t *zilog, uint64_t sync)
{
	zilog->zl_sync = sync;
}

void
zil_set_logbias(zilog_t *zilog, uint64_t logbias)
{
	zilog->zl_logbias = logbias;
}

void
zil_init_header(spa_t *spa, zil_header_t *zh, zh_kind_t kind)
{
	bzero(zh, sizeof (*zh));

	if (spa_feature_is_active(spa, SPA_FEATURE_ZIL_KINDS)) {
		VERIFY(!zil_validate_header_format(spa, zh));
		zh->zh_v2.zh_kind = kind;
	} else {
		/* zeroed header is a valid zillwb header */
	}

	const zil_vtable_t *vt = NULL;
	const void *zhk = NULL;
	size_t size = 0;
	zh_kind_t zk_out = ZIL_KIND_UNINIT;
	int err = zil_kind_specific_data_from_header(spa, zh, &zhk, &size, &vt, &zk_out);
	VERIFY0(err);
	VERIFY3S(zk_out, ==, kind);
	VERIFY(vt);

	vt->zlvt_init_header((void *)zhk, size);

	VERIFY(zil_validate_header_format(spa, zh));
}

boolean_t
zil_validate_header_format(spa_t *spa, const zil_header_t *zh)
{
	const zil_vtable_t *vt = NULL;
	const void *zhk;
	size_t size;
	if (zil_kind_specific_data_from_header(spa, zh, &zhk, &size, &vt, NULL) != 0)
		return (B_FALSE);
	VERIFY(vt);

	return (vt->zlvt_validate_header_format(zhk, size));
}

int zfs_zil_itxg_bypass = 0;
ZFS_MODULE_PARAM(zfs_zil, zfs_zil_, itxg_bypass, INT, ZMOD_RW,
	"Enable experimental itxg bypass");

zilog_t *
zil_alloc(objset_t *os, zil_header_t *zh)
{
	const zil_vtable_t *vt = NULL;
	VERIFY0(zil_kind_specific_data_from_header(dmu_objset_spa(os), zh, NULL, NULL, &vt, NULL));
	ASSERT3U(vt->zlvt_alloc_size, >, 0);
	zilog_t *zilog = kmem_zalloc(vt->zlvt_alloc_size, KM_SLEEP);
	zilog->zl_vtable = vt;

	zilog->zl_header = zh;
	zilog->zl_os = os;
	zilog->zl_spa = dmu_objset_spa(os);
	zilog->zl_dmu_pool = dmu_objset_pool(os);
	zilog->zl_logbias = dmu_objset_logbias(os);
	zilog->zl_sync = dmu_objset_syncprop(os);
	zilog->zl_dirty_max_txg = 0;

	for (int i = 0; i < TXG_SIZE; i++) {
		mutex_init(&zilog->zl_itxg[i].itxg_lock, NULL,
		    MUTEX_DEFAULT, NULL);
	}

	uint64_t os_type = os->os_phys->os_type;
	enum zil_itxg_bypass_mode want_bypass = __atomic_load_n(&zfs_zil_itxg_bypass, __ATOMIC_SEQ_CST);
	boolean_t can_bypass = os_type == DMU_OST_ZVOL;
	char osname[ZFS_MAX_DATASET_NAME_LEN];
	dmu_objset_name(os, osname);
	if (want_bypass && !can_bypass) {
#ifdef KERNEL
		pr_debug("objset %s: zfs_zil_itxg_bypass not supported for os_type=%llu\n", osname, os_type);
#endif
		want_bypass = ZIL_ITXG_BYPASS_MODE_DISABLED;
	}
	zilog->zl_itxg_bypass.mode.mode = want_bypass;
#ifdef KERNEL
	pr_debug("objset %s: itxg bypass mode=%d\n", osname, zilog->zl_itxg_bypass.mode.mode);
#endif
	rw_init(&zilog->zl_itxg_bypass.rwl, NULL, RW_DEFAULT, NULL);

	ZIL_VCALL_void(zilog, zlvt_ctor, zilog);

	return (zilog);
}

void
zil_free(zilog_t *zilog)
{
	ZIL_VCALL_void(zilog, zlvt_dtor, zilog);

	rw_destroy(&zilog->zl_itxg_bypass.rwl);

	for (int i = 0; i < TXG_SIZE; i++) {
		/*
		 * It's possible for an itx to be generated that doesn't dirty
		 * a txg (e.g. ztest TX_TRUNCATE). So there's no zil_clean()
		 * callback to remove the entry. We remove those here.
		 *
		 * Also free up the ziltest itxs.
		 */
		if (zilog->zl_itxg[i].itxg_itxs)
			zil_itxg_clean(zilog->zl_itxg[i].itxg_itxs);
		mutex_destroy(&zilog->zl_itxg[i].itxg_lock);
	}

	kmem_free(zilog, zilog->zl_vtable->zlvt_alloc_size);
}

/*
 * Open an intent log.
 */
zilog_t *
zil_open(objset_t *os, zil_get_data_t *get_data)
{
	zilog_t *zilog = dmu_objset_zil(os);

	ASSERT3P(zilog->zl_get_data, ==, NULL);
	ZIL_VCALL_void(zilog, zlvt_open, zilog);

	zilog->zl_get_data = get_data;

	return (zilog);
}

void
zil_init_dirty_zilogs(txg_list_t *dp_dirty_zilogs, spa_t *spa)
{
	txg_list_create(dp_dirty_zilogs, spa, offsetof(zilog_t, zl_dirty_link));
}

objset_t *
zil_objset(zilog_t *zl)
{
	return (zl->zl_os);
}

void
zil_init(void)
{
	for (size_t i = ZIL_KIND_FIRST; i < ZIL_KIND_COUNT; i++) {
		zil_vtables[i]->zlvt_init();
	}

	ASSERT3S(zil_default_kind.zdk_val, ==, ZIL_KIND_UNINIT);
	zil_default_kind.zdk_val = ZIL_KIND_LWB;
	rrw_init(&zil_default_kind.zdk_rwl, B_FALSE);

	zfs_percpu_counter_statset_create(&zil_itxg_bypass_statset);
}


void
zil_fini(void)
{
	zfs_percpu_counter_statset_destroy(&zil_itxg_bypass_statset);

	zil_default_kind.zdk_val = ZIL_KIND_UNINIT;
	rrw_destroy(&zil_default_kind.zdk_rwl);

	for (size_t i = ZIL_KIND_FIRST; i < ZIL_KIND_COUNT; i++) {
		zil_vtables[i]->zlvt_fini();
	}
}

void
zil_close(zilog_t *zilog)
{
	ZIL_VCALL_void(zilog, zlvt_close, zilog);
	zilog->zl_get_data = NULL;
}

/*
 * Replay the claimed intent log records.
 *
 * Replay is part of regular pool operation and can take multiple txgs.
 * Replay tracks replay progress in the ZIL header so that we replay every log
 * record that was discovered during claiming exactly once in a crash-consistent
 * manner.
 */
void
zil_replay(objset_t *os, void *arg, zil_replay_func_t *replay_func[TX_MAX_TYPE])
{
	zilog_t *zilog = dmu_objset_zil(os);
	ZIL_VCALL_void(zilog, zlvt_replay, zilog, os, arg, replay_func);
}

/*
 * NOTE: This function is side-effectful, despite the name suggesting otherwise.
 *
 * It is called from the replay funcs during zil_replay() to notify the
 * replay procedure about the dmu_tx_t in which the replay func committed the
 * log record.
 *
 * The ZIL kind implementation should use this information to record replay
 * progress in the ZIL header in txg of `tx`.
 * Corrollary: `tx` must still be open when calling zil_replaying.
 */
boolean_t
zil_replaying(zilog_t *zilog, dmu_tx_t *tx)
{
	if (zilog->zl_sync == ZFS_SYNC_DISABLED)
		return (B_TRUE);

	boolean_t ret;
	ZIL_VCALL_ret(&ret, zilog, zlvt_replaying, zilog, tx);
	return (ret);
}

boolean_t
zil_get_is_replaying_no_sideffects(zilog_t *zilog)
{
	boolean_t ret;
	ZIL_VCALL_ret(&ret, zilog, zlvt_get_is_replaying_no_sideffects, zilog);
	return (ret);
}

/*
 * Called from various places in open-context.
 *
 * Blocks until the ZIL is destroyed and the corresponding ZIL header update
 * has been synced to the main pool.
 */
void
zil_destroy(zilog_t *zilog)
{
	ZIL_VCALL_void(zilog, zlvt_destroy, zilog);
}

/*
 * Called from the DSL synctasks that destroy the dataset.
 *
 * Does not update the ZIL header, so the caller must ensure that the ZIL header
 * will not be read while and after this function executes.
 */
void
zil_destroy_sync(zilog_t *zilog, dmu_tx_t *tx)
{
	ZIL_VCALL_void(zilog, zlvt_destroy_sync, zilog, tx);
}

void
zil_commit(zilog_t *zilog, uint64_t oid)
{
	/*
	 * We should never attempt to call zil_commit on a snapshot for
	 * a couple of reasons:
	 *
	 * 1. A snapshot may never be modified, thus it cannot have any
	 *    in-flight itxs that would have modified the dataset.
	 *
	 * 2. By design, when zil_commit() is called, a commit itx will
	 *    be assigned to this zilog; as a result, the zilog will be
	 *    dirtied. We must not dirty the zilog of a snapshot; there's
	 *    checks in the code that enforce this invariant, and will
	 *    cause a panic if it's not upheld.
	 */
	ASSERT3B(dmu_objset_is_snapshot(zilog->zl_os), ==, B_FALSE);

	if (!spa_writeable(zilog->zl_spa)) {
		/*
		 * If the SPA is not writable, there should never be any
		 * pending itxs waiting to be committed to disk. If that
		 * weren't true, we'd skip writing those itxs out, and
		 * would break the semantics of zil_commit(); thus, we're
		 * verifying that truth before we return to the caller.
		 */
		for (int i = 0; i < TXG_SIZE; i++)
			ASSERT3P(zilog->zl_itxg[i].itxg_itxs, ==, NULL);
		zilog->zl_vtable->zlvt_commit_on_spa_not_writeable(zilog);
		return;
	}

	if (zilog->zl_sync == ZFS_SYNC_DISABLED)
		return;

	switch (zilog->zl_itxg_bypass.mode.mode) {
	case ZIL_ITXG_BYPASS_MODE_DISABLED:
		ZIL_VCALL_void(zilog, zlvt_commit, zilog, oid);
		return;
	case ZIL_ITXG_BYPASS_MODE_NOSERIALIZATION_INCORRECT_FOR_EVALUATION_ONLY:
		return;
	case ZIL_ITXG_BYPASS_MODE_CORRECT:
		(void) 0;
		krwlock_t *rwlp = &zilog->zl_itxg_bypass.rwl;
		const hrtime_t start = gethrtime();
		rw_enter(rwlp, RW_WRITER);
		const hrtime_t entered = gethrtime();
		zfs_percpu_counter_statset_add(&zil_itxg_bypass_statset, ZIL_ITXG_BYPASS_STAT_COMMIT_AQUIRE,  entered - start);
		zilog->zl_itxg_bypass.zil_commit_called = B_TRUE;
		rw_exit(rwlp);
		const hrtime_t end = gethrtime();
		zfs_percpu_counter_statset_add(&zil_itxg_bypass_statset, ZIL_ITXG_BYPASS_STAT_COMMIT_TOTAL,  end - start);
		return;
	}
}

int
zil_reset_logs(spa_t *spa)
{
	for (size_t i = ZIL_KIND_FIRST; i < ZIL_KIND_COUNT; i++) {
		int err = zil_vtables[i]->zlvt_reset_logs(spa);
		if (err != 0)
			return (err);
	}
	return (0);
}

/*
 * zil_claim_or_clear is called for each dataset from spa_load if the spa is
 * spa_writeable(). It implements the following behavior:
 *
 * - If SPA_LOG_CLEAR, clear the log using zlvt_clear.
 * - else if we are rewinding to a checkpoint and the log is unclaimed, clear it
 *   using zlvt_clear.
 * - else if the log is not yet claimed, zio_claim() all the blocks that we want
 *   to replay later, and spa_claim_notify().
 * - else: noop.
 *
 * Notes:
 * - ZIL implementations will generally have to record the fact that they
 *   finished claiming in the ZIL header so that they don't claim twice.
 *   Since spa_sync is not yet running, implementations can just modify the
 *   in-memory copy of zil_header_t that was passed to zil_alloc().
 *   zil_sync() will be called when spa_sync() starts and move the header to
 *   disk.
 *
 * - This has to happen during pool import because the log blocks that we
 *   want to replay are by definition those blocks that were allocated and
 *   used in a txg that has not yet synced to disk. If we didn't "remind" the
 *   metaslab allocator through zio_claim(), it would hand out those blocks
 *   again, leading to pool corruption down the road.
 *
 * - The minimum txg that should be claimed is spa_min_claim_txg().
 *
 * - spa_claim_notify() was already done by zil_check_log_chain, so, unless
 *   we had transiently corrupted log records that fixed themselves since
 *   zil_check_log_chain, this should be a no-op. Note that it doesn't hurt
 *   if the bitflip shortened the log, we'll just txg_wait_synced a little
 *   longer in spa_load. But it's crucial that if the log was extended due
 *   to the healing bitflip, we update spa_claim_max_txg through
 *   spa_claim_notify() so that there are no blocks from the future when
 *   spa_sync starts. XXX https://github.com/openzfs/zfs/issues/11364
 *
 * - The reason for the destroy-on-checkppint-rewind behavior is as follows:
 *     - The checkpointed state did not consider the unclaimed blocks
 *       alloctated.
 *     - Therefore, we did not preserve them for rewind in zio_free() while
 *       the checkpoint was active.
 *     - Therefore, the unclaimed log blocks might have been re-used for
 *       other data while the checkpoint was active, so they might be
 *       corrupted.
 *     - In any way, documented checkpoint rewind semantics are that we
 *       abandon unclaimed logs.
 */
int
zil_claim_or_clear(struct dsl_pool *dp, struct dsl_dataset *ds, void *txarg)
{
	dmu_tx_t *tx = txarg;
	objset_t *os;
	int error;

	ASSERT3U(tx->tx_txg, ==, spa_first_txg(dp->dp_spa));

	error = dmu_objset_own_obj(dp, ds->ds_object,
	    DMU_OST_ANY, B_FALSE, B_FALSE, FTAG, &os);
	if (error != 0) {
		/*
		 * EBUSY indicates that the objset is inconsistent, in which
		 * case it can not have a ZIL.
		 */
		if (error != EBUSY) {
			cmn_err(CE_WARN, "can't open objset for %llu, error %u",
			    (unsigned long long)ds->ds_object, error);
		}

		/*
		 * XXX: we really shouldn't be dropping the error here.
		 * We might be encountering a checksum (=EIO) error that
		 * slipped in after spa_load_verify_logs reported all OK.
		 * See https://github.com/openzfs/zfs/issues/11364
		 */
		return (0);
	}

	zilog_t *zilog = dmu_objset_zil(os);

	/*
	 * If the spa_log_state is not set to be cleared, check whether
	 * the current uberblock is a checkpoint one and if the current
	 * header has been claimed before moving on.
	 *
	 * If the current uberblock is a checkpointed uberblock then
	 * one of the following scenarios took place:
	 *
	 * 1] We are currently rewinding to the checkpoint of the pool.
	 * 2] We crashed in the middle of a checkpoint rewind but we
	 *    did manage to write the checkpointed uberblock to the
	 *    vdev labels, so when we tried to import the pool again
	 *    the checkpointed uberblock was selected from the import
	 *    procedure.
	 *
	 * In both cases we want to zero out all the ZIL blocks, except
	 * the ones that have been claimed at the time of the checkpoint
	 * (their zh_claim_txg != 0). The reason is that these blocks
	 * may be corrupted since we may have reused their locations on
	 * disk after we took the checkpoint.
	 *
	 * We could try to set spa_log_state to SPA_LOG_CLEAR earlier
	 * when we first figure out whether the current uberblock is
	 * checkpointed or not. Unfortunately, that would discard all
	 * the logs, including the ones that are claimed, and we would
	 * leak space.
	 */
	boolean_t is_claimed;
	ZIL_VCALL_ret(&is_claimed, zilog, zlvt_is_claimed, zilog);
	boolean_t is_checkpoint_rewind =
	    zilog->zl_spa->spa_uberblock.ub_checkpoint_txg != 0;

	if (spa_get_log_state(dp->dp_spa) == SPA_LOG_CLEAR ||
	    (is_checkpoint_rewind && !is_claimed)) {
		ZIL_VCALL_ret(&error, zilog, zlvt_clear, zilog, tx);
	} else {
		/*
		 * zil_claim isn't a no-op even if we have already claimed
		 * because it might update spa_claim_max_txg.
		 */
		ZIL_VCALL_ret(&error, zilog, zlvt_claim, zilog, tx);
	}

	dmu_objset_disown(os, B_FALSE, FTAG);

	/*
	 * XXX inconsistent error handling
	 * see https://github.com/openzfs/zfs/issues/11364
	 */
	return (error);
}


/*
 * zil_check_log_chain is called from spa_load, both for writeable and read-only
 * imports, for each dataset, unless SPA_LOG_CLEAR is set.
 * It serves two independent purposes:
 *
 * 1) Do a dry-run of ZIL claim / replay.
 *    The idea behind doing the dry run is that we can encounter errors early
 *    during pool import so that we can refuse to import the pool if we cannot
 *    read all the log entries that we'd *expect* to be present. What we expect
 *    to be present is dependent on the ZIL implementation. We recommend to read
 *    through the comments on zil_claim and the comments on the different
 *    ZIL implementation's vfuncs.
 *
 *    NOTE: There is an inherent TIME-OF-CHECK VS. TIME-OF-ACCESS problem with
 *    doing the check early. zil_claim and zil_replay SHOULD not rely on
 *    the results of this functions, as on-disk state could change inbetween,
 *    e.g., due to bitflips. See comment in ZIL-LWB's vfunc for details.
 *
 * 2) Call spa_claim_notify() with the max txg that was claimed.
 *    spa_load will, after claiming, wait for spa_claim_max_txg to sync before
 *    reporting pool import complete so that no code in ZFS will have to deal
 *    with blocks 'from the future' once spa_sync starts.
 *    We do the same thing again in zil_claim_or_clear, where it should almost
 *    always be a no-op. See the note there.
 */
/* ARGSUSED */
int
zil_check_log_chain(dsl_pool_t *dp, dsl_dataset_t *ds, void *tx)
{
	ASSERT3S(spa_get_log_state(dp->dp_spa), !=, SPA_LOG_CLEAR);

	objset_t *os;
	int error;

	ASSERT(tx == NULL);

	error = dmu_objset_from_ds(ds, &os);
	if (error != 0) {
		cmn_err(CE_WARN, "can't open objset %llu, error %d",
		    (unsigned long long)ds->ds_object, error);
		return (0);
	}

	zilog_t *zilog = dmu_objset_zil(os);

	/*
	 * See block comment in zil_claim_or_clear for why we don't
	 * check the log chain in zil_check_log_chain.
	 */
	boolean_t is_claimed;
	ZIL_VCALL_ret(&is_claimed, zilog, zlvt_is_claimed, zilog);
	boolean_t is_checkpoint_rewind =
	    zilog->zl_spa->spa_uberblock.ub_checkpoint_txg != 0;
	if (is_checkpoint_rewind && !is_claimed)
		return (0);

	int ret;
	ZIL_VCALL_ret(&ret, zilog, zlvt_check_log_chain, zilog);
	return (ret);
}

void
zil_sync(zilog_t *zilog, dmu_tx_t *tx)
{
	ZIL_VCALL_void(zilog, zlvt_sync, zilog, tx);
}

/* BEGIN CSTYLED */
/*
 * XXX The following commented-out methods are here to make the git diff
 * for this commit easier to read
 */
/*
void
zil_sync(zilog_t *zilog, dmu_tx_t *tx)
{
	zillwb_sync(zilog, tx);
}

int
zil_suspend(const char *osname, void **cookiep)
{
	return (zillwb_suspend(osname, cookiep));
}

void
zil_resume(void *cookie)
{
	zillwb_resume(cookie);
}

void
zil_lwb_add_block(struct lwb *lwb, const blkptr_t *bp)
{
	zillwb_lwb_add_block(lwb, bp);
}

void
zil_lwb_add_txg(struct lwb *lwb, uint64_t txg)
{
	zillwb_lwb_add_txg(lwb, txg);
}

int
zil_bp_tree_add(zilog_t *zilog, const blkptr_t *bp)
{
	return (zillwb_bp_tree_add(zilog, bp));
}
*/
/* END CSTYLED */


/*
 * Maximum amount of data in a WR_COPIED itx, i.e. max value for the itx's
 * lr_write_t::lr_length. Consumers MUST fall back to WR_NEED_COPY for longer
 * lr_write_t records.
 *
 * The idea behind enforcing a max size for WR_COPIED records at zil_itx_assign
 * time is that we want to give the ZIL kind's zil_commit implementation some
 * flexibility in how it stores the records. And for that, it is helfpul if the
 * ZIL kind can assume some maximum size for a record.
 */
uint64_t
zil_max_copied_data(zilog_t *zilog)
{
	uint64_t ret;
	ZIL_VCALL_ret(&ret, zilog, zlvt_max_copied_data, zilog);
	return (ret);
}



EXPORT_SYMBOL(zil_alloc);
EXPORT_SYMBOL(zil_free);
EXPORT_SYMBOL(zil_open);
EXPORT_SYMBOL(zil_close);
EXPORT_SYMBOL(zil_destroy);
EXPORT_SYMBOL(zil_destroy_sync);
EXPORT_SYMBOL(zil_itx_create);
EXPORT_SYMBOL(zil_itx_destroy);
EXPORT_SYMBOL(zil_itx_assign);
EXPORT_SYMBOL(zil_commit);
EXPORT_SYMBOL(zil_claim_or_clear);
EXPORT_SYMBOL(zil_check_log_chain);
EXPORT_SYMBOL(zil_sync);
EXPORT_SYMBOL(zil_clean);
EXPORT_SYMBOL(zil_set_sync);
EXPORT_SYMBOL(zil_set_logbias);


zh_kind_t
zil_default_kind_get(void)
{
	zh_kind_t ret;
	rrw_enter_read(&zil_default_kind.zdk_rwl, FTAG);
	ret = zil_default_kind.zdk_val;
	rrw_exit(&zil_default_kind.zdk_rwl, FTAG);
	return ret;
}

void
zil_default_kind_set(zh_kind_t kind)
{
	rrw_enter_write(&zil_default_kind.zdk_rwl);
	zil_default_kind.zdk_val = kind;
	rrw_exit(&zil_default_kind.zdk_rwl, FTAG);
}

zh_kind_t zil_default_kind_hold(void *ftag)
{
	rrw_enter_read(&zil_default_kind.zdk_rwl, ftag);
	return zil_default_kind.zdk_val;
}

void zil_default_kind_rele(void *ftag)
{
	rrw_exit(&zil_default_kind.zdk_rwl, ftag);
}

int
zil_kind_from_str(const char *val, zh_kind_t *out)
{
	/* NB: keep in sync with zil_kind_to_str */
	if (strcmp(val, "lwb") == 0) {
		*out = ZIL_KIND_LWB;
	} else if (strcmp(val, "pmem") == 0) {
		*out = ZIL_KIND_PMEM;
	} else {
		return SET_ERROR(EINVAL);
	}
	return (0);
}

#ifdef _KERNEL

static int
zil_default_kind__param_set(const char *val, zfs_kernel_param_t *unused)
{
	size_t val_len;

	val_len = strlen(val);
	while ((val_len > 0) && !!isspace(val[val_len-1])) /* trim '\n' */
		val_len--;

	/* TODO: fix benchmarking suite to support the 'named zil kinds'
	 * zil_kind_from_str above */

	if (strncmp(val, "1", val_len) == 0) {
		zil_default_kind_set(ZIL_KIND_LWB);
	} else if (strncmp(val, "2", val_len) == 0) {
		zil_default_kind_set(ZIL_KIND_PMEM);
	} else {
		return (-EINVAL);
	}

	return (0);
}

static int
zil_default_kind__param_get(char *buffer, zfs_kernel_param_t *unused)
{
	int ret = snprintf(buffer, 3, "%d\n", (int)zil_default_kind_get());
	VERIFY3S(ret, ==, 2);
	return ret;
}

/* BEGIN CSTYLED */
ZFS_MODULE_PARAM(zfs_zil, zil_, replay_disable, INT, ZMOD_RW,
	"Disable intent logging replay");

ZFS_MODULE_VIRTUAL_PARAM_CALL(zfs_zil, zil_, default_kind,
	zil_default_kind__param_set, zil_default_kind__param_get, ZMOD_RW,
	"Default ZIL kind for newly initialized ZILs");
/* END CSTYLED */

#endif /* _KERNEL */
