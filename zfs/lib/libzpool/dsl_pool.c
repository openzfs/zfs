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

#pragma ident	"@(#)dsl_pool.c	1.12	08/03/20 SMI"

#include <sys/dsl_pool.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_synctask.h>
#include <sys/dmu_tx.h>
#include <sys/dmu_objset.h>
#include <sys/arc.h>
#include <sys/zap.h>
#include <sys/zio.h>
#include <sys/zfs_context.h>
#include <sys/fs/zfs.h>

int zfs_no_write_throttle = 0;
uint64_t zfs_write_limit_override = 0;

static int
dsl_pool_open_mos_dir(dsl_pool_t *dp, dsl_dir_t **ddp)
{
	uint64_t obj;
	int err;

	err = zap_lookup(dp->dp_meta_objset,
	    dp->dp_root_dir->dd_phys->dd_child_dir_zapobj,
	    MOS_DIR_NAME, sizeof (obj), 1, &obj);
	if (err)
		return (err);

	return (dsl_dir_open_obj(dp, obj, MOS_DIR_NAME, dp, ddp));
}

static dsl_pool_t *
dsl_pool_open_impl(spa_t *spa, uint64_t txg)
{
	dsl_pool_t *dp;
	blkptr_t *bp = spa_get_rootblkptr(spa);
	extern uint64_t zfs_write_limit_min;

	dp = kmem_zalloc(sizeof (dsl_pool_t), KM_SLEEP);
	dp->dp_spa = spa;
	dp->dp_meta_rootbp = *bp;
	rw_init(&dp->dp_config_rwlock, NULL, RW_DEFAULT, NULL);
	dp->dp_write_limit = zfs_write_limit_min;
	txg_init(dp, txg);

	txg_list_create(&dp->dp_dirty_datasets,
	    offsetof(dsl_dataset_t, ds_dirty_link));
	txg_list_create(&dp->dp_dirty_dirs,
	    offsetof(dsl_dir_t, dd_dirty_link));
	txg_list_create(&dp->dp_sync_tasks,
	    offsetof(dsl_sync_task_group_t, dstg_node));
	list_create(&dp->dp_synced_datasets, sizeof (dsl_dataset_t),
	    offsetof(dsl_dataset_t, ds_synced_link));

	mutex_init(&dp->dp_lock, NULL, MUTEX_DEFAULT, NULL);

	return (dp);
}

int
dsl_pool_open(spa_t *spa, uint64_t txg, dsl_pool_t **dpp)
{
	int err;
	dsl_pool_t *dp = dsl_pool_open_impl(spa, txg);
	objset_impl_t *osi;

	rw_enter(&dp->dp_config_rwlock, RW_READER);
	err = dmu_objset_open_impl(spa, NULL, &dp->dp_meta_rootbp, &osi);
	if (err)
		goto out;
	dp->dp_meta_objset = &osi->os;

	err = zap_lookup(dp->dp_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_ROOT_DATASET, sizeof (uint64_t), 1,
	    &dp->dp_root_dir_obj);
	if (err)
		goto out;

	err = dsl_dir_open_obj(dp, dp->dp_root_dir_obj,
	    NULL, dp, &dp->dp_root_dir);
	if (err)
		goto out;

	err = dsl_pool_open_mos_dir(dp, &dp->dp_mos_dir);
	if (err)
		goto out;

out:
	rw_exit(&dp->dp_config_rwlock);
	if (err)
		dsl_pool_close(dp);
	else
		*dpp = dp;

	return (err);
}

void
dsl_pool_close(dsl_pool_t *dp)
{
	/* drop our reference from dsl_pool_open() */
	if (dp->dp_mos_dir)
		dsl_dir_close(dp->dp_mos_dir, dp);
	if (dp->dp_root_dir)
		dsl_dir_close(dp->dp_root_dir, dp);

	/* undo the dmu_objset_open_impl(mos) from dsl_pool_open() */
	if (dp->dp_meta_objset)
		dmu_objset_evict(NULL, dp->dp_meta_objset->os);

	txg_list_destroy(&dp->dp_dirty_datasets);
	txg_list_destroy(&dp->dp_dirty_dirs);
	txg_list_destroy(&dp->dp_sync_tasks);
	list_destroy(&dp->dp_synced_datasets);

	arc_flush(dp->dp_spa);
	txg_fini(dp);
	rw_destroy(&dp->dp_config_rwlock);
	mutex_destroy(&dp->dp_lock);
	kmem_free(dp, sizeof (dsl_pool_t));
}

dsl_pool_t *
dsl_pool_create(spa_t *spa, uint64_t txg)
{
	int err;
	dsl_pool_t *dp = dsl_pool_open_impl(spa, txg);
	dmu_tx_t *tx = dmu_tx_create_assigned(dp, txg);
	dp->dp_meta_objset = &dmu_objset_create_impl(spa,
	    NULL, &dp->dp_meta_rootbp, DMU_OST_META, tx)->os;

	/* create the pool directory */
	err = zap_create_claim(dp->dp_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_OT_OBJECT_DIRECTORY, DMU_OT_NONE, 0, tx);
	ASSERT3U(err, ==, 0);

	/* create and open the root dir */
	dsl_dataset_create_root(dp, &dp->dp_root_dir_obj, tx);
	VERIFY(0 == dsl_dir_open_obj(dp, dp->dp_root_dir_obj,
	    NULL, dp, &dp->dp_root_dir));

	/* create and open the meta-objset dir */
	(void) dsl_dir_create_sync(dp->dp_root_dir, MOS_DIR_NAME, tx);
	VERIFY(0 == dsl_pool_open_mos_dir(dp, &dp->dp_mos_dir));

	dmu_tx_commit(tx);

	return (dp);
}

void
dsl_pool_sync(dsl_pool_t *dp, uint64_t txg)
{
	zio_t *zio;
	dmu_tx_t *tx;
	dsl_dir_t *dd;
	dsl_dataset_t *ds;
	dsl_sync_task_group_t *dstg;
	objset_impl_t *mosi = dp->dp_meta_objset->os;
	int err;

	tx = dmu_tx_create_assigned(dp, txg);

	zio = zio_root(dp->dp_spa, NULL, NULL, ZIO_FLAG_MUSTSUCCEED);
	while (ds = txg_list_remove(&dp->dp_dirty_datasets, txg)) {
		if (!list_link_active(&ds->ds_synced_link))
			list_insert_tail(&dp->dp_synced_datasets, ds);
		else
			dmu_buf_rele(ds->ds_dbuf, ds);
		dsl_dataset_sync(ds, zio, tx);
	}
	err = zio_wait(zio);
	ASSERT(err == 0);

	while (dstg = txg_list_remove(&dp->dp_sync_tasks, txg))
		dsl_sync_task_group_sync(dstg, tx);
	while (dd = txg_list_remove(&dp->dp_dirty_dirs, txg))
		dsl_dir_sync(dd, tx);

	if (list_head(&mosi->os_dirty_dnodes[txg & TXG_MASK]) != NULL ||
	    list_head(&mosi->os_free_dnodes[txg & TXG_MASK]) != NULL) {
		zio = zio_root(dp->dp_spa, NULL, NULL, ZIO_FLAG_MUSTSUCCEED);
		dmu_objset_sync(mosi, zio, tx);
		err = zio_wait(zio);
		ASSERT(err == 0);
		dprintf_bp(&dp->dp_meta_rootbp, "meta objset rootbp is %s", "");
		spa_set_rootblkptr(dp->dp_spa, &dp->dp_meta_rootbp);
	}

	dmu_tx_commit(tx);
}

void
dsl_pool_zil_clean(dsl_pool_t *dp)
{
	dsl_dataset_t *ds;

	while (ds = list_head(&dp->dp_synced_datasets)) {
		list_remove(&dp->dp_synced_datasets, ds);
		ASSERT(ds->ds_user_ptr != NULL);
		zil_clean(((objset_impl_t *)ds->ds_user_ptr)->os_zil);
		dmu_buf_rele(ds->ds_dbuf, ds);
	}
}

/*
 * TRUE if the current thread is the tx_sync_thread or if we
 * are being called from SPA context during pool initialization.
 */
int
dsl_pool_sync_context(dsl_pool_t *dp)
{
	return (curthread == dp->dp_tx.tx_sync_thread ||
	    spa_get_dsl(dp->dp_spa) == NULL);
}

uint64_t
dsl_pool_adjustedsize(dsl_pool_t *dp, boolean_t netfree)
{
	uint64_t space, resv;

	/*
	 * Reserve about 1.6% (1/64), or at least 32MB, for allocation
	 * efficiency.
	 * XXX The intent log is not accounted for, so it must fit
	 * within this slop.
	 *
	 * If we're trying to assess whether it's OK to do a free,
	 * cut the reservation in half to allow forward progress
	 * (e.g. make it possible to rm(1) files from a full pool).
	 */
	space = spa_get_dspace(dp->dp_spa);
	resv = MAX(space >> 6, SPA_MINDEVSIZE >> 1);
	if (netfree)
		resv >>= 1;

	return (space - resv);
}

int
dsl_pool_tempreserve_space(dsl_pool_t *dp, uint64_t space, dmu_tx_t *tx)
{
	uint64_t reserved = 0;
	uint64_t write_limit = (zfs_write_limit_override ?
	    zfs_write_limit_override : dp->dp_write_limit);

	if (zfs_no_write_throttle) {
		dp->dp_tempreserved[tx->tx_txg & TXG_MASK] += space;
		return (0);
	}

	/*
	 * Check to see if we have exceeded the maximum allowed IO for
	 * this transaction group.  We can do this without locks since
	 * a little slop here is ok.  Note that we do the reserved check
	 * with only half the requested reserve: this is because the
	 * reserve requests are worst-case, and we really don't want to
	 * throttle based off of worst-case estimates.
	 */
	if (write_limit > 0) {
		reserved = dp->dp_space_towrite[tx->tx_txg & TXG_MASK]
		    + dp->dp_tempreserved[tx->tx_txg & TXG_MASK] / 2;

		if (reserved && reserved > write_limit)
			return (ERESTART);
	}

	atomic_add_64(&dp->dp_tempreserved[tx->tx_txg & TXG_MASK], space);

	/*
	 * If this transaction group is over 7/8ths capacity, delay
	 * the caller 1 clock tick.  This will slow down the "fill"
	 * rate until the sync process can catch up with us.
	 */
	if (reserved && reserved > (write_limit - write_limit << 3))
		txg_delay(dp, tx->tx_txg, 1);

	return (0);
}

void
dsl_pool_tempreserve_clear(dsl_pool_t *dp, int64_t space, dmu_tx_t *tx)
{
	ASSERT(dp->dp_tempreserved[tx->tx_txg & TXG_MASK] >= space);
	atomic_add_64(&dp->dp_tempreserved[tx->tx_txg & TXG_MASK], -space);
}

void
dsl_pool_memory_pressure(dsl_pool_t *dp)
{
	extern uint64_t zfs_write_limit_min;
	uint64_t space_inuse = 0;
	int i;

	if (dp->dp_write_limit == zfs_write_limit_min)
		return;

	for (i = 0; i < TXG_SIZE; i++) {
		space_inuse += dp->dp_space_towrite[i];
		space_inuse += dp->dp_tempreserved[i];
	}
	dp->dp_write_limit = MAX(zfs_write_limit_min,
	    MIN(dp->dp_write_limit, space_inuse / 4));
}

void
dsl_pool_willuse_space(dsl_pool_t *dp, int64_t space, dmu_tx_t *tx)
{
	if (space > 0) {
		mutex_enter(&dp->dp_lock);
		dp->dp_space_towrite[tx->tx_txg & TXG_MASK] += space;
		mutex_exit(&dp->dp_lock);
	}
}
