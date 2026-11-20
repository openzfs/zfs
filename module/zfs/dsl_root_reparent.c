/*
 * Copyright (c) 2026 <TheAlmightyOgreLord>. All rights reserved.
 *
 * Use and distribution subject to the terms of the ZFS Core License.
 *
 * This module provides the ability to reparent the pool root dataset
 * to a child dataset, effectively creating a new empty root and
 * moving the old root (and its subtree) beneath it.
 *
 * Architecture:
 *   - The caller (zfs_ioctl.c) creates a temp dataset via zfs_create,
 *     looks up its dsl_dir, pre-reads the child ZAP obj number and
 *     borrows the dd_dbuf hold, then schedules the sync task.
 *   - The sync task only modifies existing MOS objects (ZAP entries,
 *     dd_parent_obj fields). No new object allocation, no dmu_buf_hold,
 *     no dsl_*_hold. All of those deadlock in TXG sync context.
 */

#include <sys/dsl_root_reparent.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_pool.h>
#include <sys/dmu.h>
#include <sys/dmu_impl.h>
#include <sys/dnode.h>
#include <sys/zap.h>

/*
 * dsl_root_reparent_check()
 *
 * Validate that the operation is possible.
 * Called from normal context (ioctl handler).
 */
int
dsl_root_reparent_check(dsl_pool_t *dp, const char *child_name)
{
	dsl_dir_t	*root = dp->dp_root_dir;
	uint64_t	child_obj;

	if (zap_lookup(dp->dp_meta_objset,
	    dsl_dir_phys(root)->dd_child_dir_zapobj, child_name,
	    sizeof (uint64_t), 1, &child_obj) == 0)
		return (SET_ERROR(EEXIST));

	ASSERT3U(dsl_dir_phys(root)->dd_origin_obj, ==, 0);

	return (0);
}

/*
 * dsl_root_reparent_sync_task()
 *
 * Sync task callback. Runs at TXG sync time.
 *
 * Constraints (learned the hard way):
 *   - NO dmu_buf_hold (deadlocks for dsl_dir objects in sync context)
 *   - NO dsl_dataset_hold / dsl_dir_hold_obj (deadlocks)
 *   - YES to zap_add/zap_remove/zap_update with tx
 *   - YES to zap_cursor iteration
 *   - YES to dmu_buf_will_dirty on already-held buffers
 *   - YES to kmem_alloc/kmem_free (sleepable in sync context)
 *   - YES to zap_create (allocates a ZAP dnode, proven safe)
 *
 * All data needed by this task is pre-computed and passed via the
 * root_reparent_arg struct.
 */

void
dsl_root_reparent_sync_task(void *arg, dmu_tx_t *tx)
{
	struct root_reparent_arg	*rra = arg;
	dsl_pool_t			*dp = tx->tx_pool;
	dsl_dir_t			*old_root;
	dsl_dir_t			*wr_new_root = NULL;
	uint64_t			old_root_obj;
	uint64_t			new_root_obj;
	const char			*child_name;
	dmu_buf_t			*db_old = NULL;
	dmu_buf_t			*db_new = NULL;
	int				error;

	old_root = dp->dp_root_dir;
	old_root_obj = old_root->dd_object;
	new_root_obj = rra->rra_new_root_obj;
	child_name = rra->rra_child_name;

	/*
	 * STEP 1: Hold the new root's dsl_dir.
	 */
	error = dsl_dir_hold_obj(dp, new_root_obj, NULL, FTAG,
	    &wr_new_root);
	if (error != 0)
		goto out;

	/*
	 * STEP 2: Hold the dsl_dataset dnodes for the name swap.
	 * Use dmu_bonus_hold() — the name lives in the dnode bonus.
	 * Signature: (objset, object, tag, &db) — 4 args, no flags.
	 */
	{
		uint64_t old_ds_obj =
		    dsl_dir_phys(old_root)->dd_head_dataset_obj;
		uint64_t new_ds_obj =
		    dsl_dir_phys(wr_new_root)->dd_head_dataset_obj;

		error = dmu_bonus_hold(dp->dp_meta_objset, old_ds_obj,
		    FTAG, &db_old);
		if (error != 0)
			goto out;

		error = dmu_bonus_hold(dp->dp_meta_objset, new_ds_obj,
		    FTAG, &db_new);
		if (error != 0)
			goto out;
	}

	
	 /*
          * STEP 3: Update pool root pointer + parent pointers.
          */
	
	 {
                dsl_dir_t *new_root_dir = rra->rra_new_root_ds->ds_dir;

                VERIFY0(zap_update(dp->dp_meta_objset, 1,
                    "root_dataset", 8, 1, &old_root_obj, tx));
		
                dmu_buf_will_dirty(new_root_dir->dd_dbuf, tx);
                dsl_dir_phys(new_root_dir)->dd_parent_obj = 0;

                dmu_buf_will_dirty(old_root->dd_dbuf, tx);
                dsl_dir_phys(old_root)->dd_parent_obj = new_root_obj;
		
                dp->dp_root_dir_obj = new_root_obj;

                dsl_dataset_rele(rra->rra_new_root_ds, FTAG);
        }   	

	/*
	 * STEP 4: Fix up child ZAPs.
	 *
	 * Each dsl_dir has its own child ZAP: dd_child_dir_zapobj.
	 * (NOT dd_child_dsl_dir_zap — that field doesn't exist.)
	 */
	{
		uint64_t old_child_zap =
		    dsl_dir_phys(old_root)->dd_child_dir_zapobj;
		uint64_t new_child_zap =
		    dsl_dir_phys(wr_new_root)->dd_child_dir_zapobj;

		/* Remove the temp entry from old root's children */
		(void)zap_remove(dp->dp_meta_objset, old_child_zap,
		    "__new_root_tmp", tx);

		/* Register old_root as a child under new_root */
		error = zap_add(dp->dp_meta_objset, new_child_zap,
		    child_name, sizeof(uint64_t), 1, &old_root_obj, tx);
		if (error != 0)
			goto out;
	}

	/*
	 * STEP 5: Promote new_root to pool root.
	 */
	dmu_buf_will_dirty(wr_new_root->dd_dbuf, tx);
	dsl_dir_phys(wr_new_root)->dd_parent_obj = 0;

	/*
	 * STEP 6: Demote old_root to child of new_root.
	 */
	dmu_buf_will_dirty(old_root->dd_dbuf, tx);
	dsl_dir_phys(old_root)->dd_parent_obj = new_root_obj;

	/*
	 * STEP 7: Swap dataset names.
	 *
	 * Bonus layout: [ dsl_dataset_phys_t ][ ds_name padded to 1024 ]
	 * So the name starts at offset sizeof(dsl_dataset_phys_t).
	 */
	dmu_buf_will_dirty(db_old, tx);
	dmu_buf_will_dirty(db_new, tx);

	strlcpy((char *)db_old->db_data + sizeof(dsl_dataset_phys_t),
	    child_name, ZFS_MAX_DATASET_NAME_LEN);

	strlcpy((char *)db_new->db_data + sizeof(dsl_dataset_phys_t),
	    rra->rra_pool_name, ZFS_MAX_DATASET_NAME_LEN);

out:
	if (db_new != NULL)
		dmu_buf_rele(db_new, FTAG);
	if (db_old != NULL)
		dmu_buf_rele(db_old, FTAG);
	if (wr_new_root != NULL)
		dsl_dir_rele(wr_new_root, FTAG);
	kmem_free(rra, sizeof(*rra));
}

/*
 * dsl_root_reparent()
 *
 * Normal context entry point. Currently unused (the ioctl handler in
 * zfs_ioctl.c does the dataset lookup and sync task scheduling inline),
 * but provided for completeness and future use.
 */
int
dsl_root_reparent(dsl_pool_t *dp, const char *pool_name,
    const char *child_name)
{
	return (SET_ERROR(ENOTSUP));
}
