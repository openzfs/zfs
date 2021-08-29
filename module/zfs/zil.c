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
 * consists of 3 parts:
 *
 * 	- a single, per-dataset, ZIL header; which points to a chain of
 * 	- zero or more ZIL blocks; each of which contains
 * 	- zero or more ZIL records
 *
 * A ZIL record holds the information necessary to replay a single
 * system call transaction. A ZIL block can hold many ZIL records, and
 * the blocks are chained together, similarly to a singly linked list.
 *
 * Each ZIL block contains a block pointer (blkptr_t) to the next ZIL
 * block in the chain, and the ZIL header points to the first block in
 * the chain.
 *
 * Note, there is not a fixed place in the pool to hold these ZIL
 * blocks; they are dynamically allocated and freed as needed from the
 * blocks available on the pool, though they can be preferentially
 * allocated from a dedicated "log" vdev.
 */


/*
 * Disable intent logging replay.  This global ZIL switch affects all pools.
 */
int zil_replay_disable = 0;

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
zil_itx_destroy(itx_t *itx)
{
	IMPLY(itx->itx_lr.lrc_txtype == TX_COMMIT, itx->itx_callback == NULL);
	IMPLY(itx->itx_callback != NULL, itx->itx_lr.lrc_txtype != TX_COMMIT);

	if (itx->itx_callback != NULL)
		itx->itx_callback(itx->itx_callback_data);

	zio_data_buf_free(itx, itx->itx_size);
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
		 * zil_lwb_commit(), and free'd in that function. Having
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
		 * because zil_create() or zil_commit_writer_stall() is
		 * called) we will hit this case.
		 */
		if (itx->itx_lr.lrc_txtype == TX_COMMIT)
			zil_commit_waiter_skip(itx->itx_private);

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

void
zil_itx_assign(zilog_t *zilog, itx_t *itx, dmu_tx_t *tx)
{
	uint64_t txg;
	itxg_t *itxg;
	itxs_t *itxs, *clean = NULL;

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

	itx->itx_lr.lrc_txg = dmu_tx_get_txg(tx);

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
 * committed, and move them onto the ZIL's zl_itx_commit_list.
 */
void
zil_get_commit_list(zilog_t *zilog)
{
	uint64_t otxg, txg;
	list_t *commit_list = &zilog->zl_itx_commit_list;

	ASSERT(MUTEX_HELD(&zilog->zl_issuer_lock));

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
		 * If we're adding itx records to the zl_itx_commit_list,
		 * then the zil better be dirty in this "txg". We can assert
		 * that here since we're holding the itxg_lock which will
		 * prevent spa_sync from cleaning it. Once we add the itxs
		 * to the zl_itx_commit_list we must commit it to disk even
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

zilog_t *
zil_alloc(objset_t *os, zil_header_t *zh_phys)
{
	zilog_t *zilog;

	zilog = kmem_zalloc(sizeof (zilog_t), KM_SLEEP);

	zilog->zl_header = zh_phys;
	zilog->zl_os = os;
	zilog->zl_spa = dmu_objset_spa(os);
	zilog->zl_dmu_pool = dmu_objset_pool(os);
	zilog->zl_destroy_txg = TXG_INITIAL - 1;
	zilog->zl_logbias = dmu_objset_logbias(os);
	zilog->zl_sync = dmu_objset_syncprop(os);
	zilog->zl_dirty_max_txg = 0;
	zilog->zl_last_lwb_opened = NULL;
	zilog->zl_last_lwb_latency = 0;
	zilog->zl_max_block_size = zil_maxblocksize;

	mutex_init(&zilog->zl_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&zilog->zl_issuer_lock, NULL, MUTEX_DEFAULT, NULL);

	for (int i = 0; i < TXG_SIZE; i++) {
		mutex_init(&zilog->zl_itxg[i].itxg_lock, NULL,
		    MUTEX_DEFAULT, NULL);
	}

	list_create(&zilog->zl_lwb_list, sizeof (lwb_t),
	    offsetof(lwb_t, lwb_node));

	list_create(&zilog->zl_itx_commit_list, sizeof (itx_t),
	    offsetof(itx_t, itx_node));

	cv_init(&zilog->zl_cv_suspend, NULL, CV_DEFAULT, NULL);

	return (zilog);
}

void
zil_free(zilog_t *zilog)
{
	int i;

	zilog->zl_stop_sync = 1;

	ASSERT0(zilog->zl_suspend);
	ASSERT0(zilog->zl_suspending);

	ASSERT(list_is_empty(&zilog->zl_lwb_list));
	list_destroy(&zilog->zl_lwb_list);

	ASSERT(list_is_empty(&zilog->zl_itx_commit_list));
	list_destroy(&zilog->zl_itx_commit_list);

	for (i = 0; i < TXG_SIZE; i++) {
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

	mutex_destroy(&zilog->zl_issuer_lock);
	mutex_destroy(&zilog->zl_lock);

	cv_destroy(&zilog->zl_cv_suspend);

	kmem_free(zilog, sizeof (zilog_t));
}

/*
 * Open an intent log.
 */
zilog_t *
zil_open(objset_t *os, zil_get_data_t *get_data)
{
	zilog_t *zilog = dmu_objset_zil(os);

	ASSERT3P(zilog->zl_get_data, ==, NULL);
	ASSERT3P(zilog->zl_last_lwb_opened, ==, NULL);
	ASSERT(list_is_empty(&zilog->zl_lwb_list));

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

EXPORT_SYMBOL(zil_alloc);
EXPORT_SYMBOL(zil_free);
EXPORT_SYMBOL(zil_open);
EXPORT_SYMBOL(zil_itx_create);
EXPORT_SYMBOL(zil_itx_destroy);
EXPORT_SYMBOL(zil_itx_assign);
EXPORT_SYMBOL(zil_sync);
EXPORT_SYMBOL(zil_clean);
EXPORT_SYMBOL(zil_set_sync);
EXPORT_SYMBOL(zil_set_logbias);

/* BEGIN CSTYLED */
ZFS_MODULE_PARAM(zfs_zil, zil_, replay_disable, INT, ZMOD_RW,
	"Disable intent logging replay");
/* END CSTYLED */
