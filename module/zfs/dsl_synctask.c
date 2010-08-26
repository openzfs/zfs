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
 */

#include <sys/dmu.h>
#include <sys/dmu_tx.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_synctask.h>
#include <sys/metaslab.h>

#define	DST_AVG_BLKSHIFT 14

/* ARGSUSED */
static int
dsl_null_checkfunc(void *arg1, void *arg2, dmu_tx_t *tx)
{
	return (0);
}

dsl_sync_task_group_t *
dsl_sync_task_group_create(dsl_pool_t *dp)
{
	dsl_sync_task_group_t *dstg;

	dstg = kmem_zalloc(sizeof (dsl_sync_task_group_t), KM_SLEEP);
	list_create(&dstg->dstg_tasks, sizeof (dsl_sync_task_t),
	    offsetof(dsl_sync_task_t, dst_node));
	dstg->dstg_pool = dp;

	return (dstg);
}

void
dsl_sync_task_create(dsl_sync_task_group_t *dstg,
    dsl_checkfunc_t *checkfunc, dsl_syncfunc_t *syncfunc,
    void *arg1, void *arg2, int blocks_modified)
{
	dsl_sync_task_t *dst;

	if (checkfunc == NULL)
		checkfunc = dsl_null_checkfunc;
	dst = kmem_zalloc(sizeof (dsl_sync_task_t), KM_SLEEP);
	dst->dst_checkfunc = checkfunc;
	dst->dst_syncfunc = syncfunc;
	dst->dst_arg1 = arg1;
	dst->dst_arg2 = arg2;
	list_insert_tail(&dstg->dstg_tasks, dst);

	dstg->dstg_space += blocks_modified << DST_AVG_BLKSHIFT;
}

int
dsl_sync_task_group_wait(dsl_sync_task_group_t *dstg)
{
	dmu_tx_t *tx;
	uint64_t txg;
	dsl_sync_task_t *dst;

top:
	tx = dmu_tx_create_dd(dstg->dstg_pool->dp_mos_dir);
	VERIFY(0 == dmu_tx_assign(tx, TXG_WAIT));

	txg = dmu_tx_get_txg(tx);

	/* Do a preliminary error check. */
	dstg->dstg_err = 0;
	rw_enter(&dstg->dstg_pool->dp_config_rwlock, RW_READER);
	for (dst = list_head(&dstg->dstg_tasks); dst;
	    dst = list_next(&dstg->dstg_tasks, dst)) {
#ifdef ZFS_DEBUG
		/*
		 * Only check half the time, otherwise, the sync-context
		 * check will almost never fail.
		 */
		if (spa_get_random(2) == 0)
			continue;
#endif
		dst->dst_err =
		    dst->dst_checkfunc(dst->dst_arg1, dst->dst_arg2, tx);
		if (dst->dst_err)
			dstg->dstg_err = dst->dst_err;
	}
	rw_exit(&dstg->dstg_pool->dp_config_rwlock);

	if (dstg->dstg_err) {
		dmu_tx_commit(tx);
		return (dstg->dstg_err);
	}

	/*
	 * We don't generally have many sync tasks, so pay the price of
	 * add_tail to get the tasks executed in the right order.
	 */
	VERIFY(0 == txg_list_add_tail(&dstg->dstg_pool->dp_sync_tasks,
	    dstg, txg));

	dmu_tx_commit(tx);

	txg_wait_synced(dstg->dstg_pool, txg);

	if (dstg->dstg_err == EAGAIN) {
		txg_wait_synced(dstg->dstg_pool, txg + TXG_DEFER_SIZE);
		goto top;
	}

	return (dstg->dstg_err);
}

void
dsl_sync_task_group_nowait(dsl_sync_task_group_t *dstg, dmu_tx_t *tx)
{
	uint64_t txg;

	dstg->dstg_nowaiter = B_TRUE;
	txg = dmu_tx_get_txg(tx);
	/*
	 * We don't generally have many sync tasks, so pay the price of
	 * add_tail to get the tasks executed in the right order.
	 */
	VERIFY(0 == txg_list_add_tail(&dstg->dstg_pool->dp_sync_tasks,
	    dstg, txg));
}

void
dsl_sync_task_group_destroy(dsl_sync_task_group_t *dstg)
{
	dsl_sync_task_t *dst;

	while (dst = list_head(&dstg->dstg_tasks)) {
		list_remove(&dstg->dstg_tasks, dst);
		kmem_free(dst, sizeof (dsl_sync_task_t));
	}
	kmem_free(dstg, sizeof (dsl_sync_task_group_t));
}

void
dsl_sync_task_group_sync(dsl_sync_task_group_t *dstg, dmu_tx_t *tx)
{
	dsl_sync_task_t *dst;
	dsl_pool_t *dp = dstg->dstg_pool;
	uint64_t quota, used;

	ASSERT3U(dstg->dstg_err, ==, 0);

	/*
	 * Check for sufficient space.  We just check against what's
	 * on-disk; we don't want any in-flight accounting to get in our
	 * way, because open context may have already used up various
	 * in-core limits (arc_tempreserve, dsl_pool_tempreserve).
	 */
	quota = dsl_pool_adjustedsize(dp, B_FALSE) -
	    metaslab_class_get_deferred(spa_normal_class(dp->dp_spa));
	used = dp->dp_root_dir->dd_phys->dd_used_bytes;
	/* MOS space is triple-dittoed, so we multiply by 3. */
	if (dstg->dstg_space > 0 && used + dstg->dstg_space * 3 > quota) {
		dstg->dstg_err = ENOSPC;
		return;
	}

	/*
	 * Check for errors by calling checkfuncs.
	 */
	rw_enter(&dp->dp_config_rwlock, RW_WRITER);
	for (dst = list_head(&dstg->dstg_tasks); dst;
	    dst = list_next(&dstg->dstg_tasks, dst)) {
		dst->dst_err =
		    dst->dst_checkfunc(dst->dst_arg1, dst->dst_arg2, tx);
		if (dst->dst_err)
			dstg->dstg_err = dst->dst_err;
	}

	if (dstg->dstg_err == 0) {
		/*
		 * Execute sync tasks.
		 */
		for (dst = list_head(&dstg->dstg_tasks); dst;
		    dst = list_next(&dstg->dstg_tasks, dst)) {
			dst->dst_syncfunc(dst->dst_arg1, dst->dst_arg2, tx);
		}
	}
	rw_exit(&dp->dp_config_rwlock);

	if (dstg->dstg_nowaiter)
		dsl_sync_task_group_destroy(dstg);
}

int
dsl_sync_task_do(dsl_pool_t *dp,
    dsl_checkfunc_t *checkfunc, dsl_syncfunc_t *syncfunc,
    void *arg1, void *arg2, int blocks_modified)
{
	dsl_sync_task_group_t *dstg;
	int err;

	ASSERT(spa_writeable(dp->dp_spa));

	dstg = dsl_sync_task_group_create(dp);
	dsl_sync_task_create(dstg, checkfunc, syncfunc,
	    arg1, arg2, blocks_modified);
	err = dsl_sync_task_group_wait(dstg);
	dsl_sync_task_group_destroy(dstg);
	return (err);
}

void
dsl_sync_task_do_nowait(dsl_pool_t *dp,
    dsl_checkfunc_t *checkfunc, dsl_syncfunc_t *syncfunc,
    void *arg1, void *arg2, int blocks_modified, dmu_tx_t *tx)
{
	dsl_sync_task_group_t *dstg;

	if (!spa_writeable(dp->dp_spa))
		return;

	dstg = dsl_sync_task_group_create(dp);
	dsl_sync_task_create(dstg, checkfunc, syncfunc,
	    arg1, arg2, blocks_modified);
	dsl_sync_task_group_nowait(dstg, tx);
}
