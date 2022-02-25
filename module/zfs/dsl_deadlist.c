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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2019 by Delphix. All rights reserved.
 * Copyright (c) 2014 Spectra Logic Corporation, All rights reserved.
 */

#include <sys/dmu.h>
#include <sys/zap.h>
#include <sys/zfs_context.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_dataset.h>

/*
 * Deadlist concurrency:
 *
 * Deadlists can only be modified from the syncing thread.
 *
 * Except for dsl_deadlist_insert(), it can only be modified with the
 * dp_config_rwlock held with RW_WRITER.
 *
 * The accessors (dsl_deadlist_space() and dsl_deadlist_space_range()) can
 * be called concurrently, from open context, with the dl_config_rwlock held
 * with RW_READER.
 *
 * Therefore, we only need to provide locking between dsl_deadlist_insert() and
 * the accessors, protecting:
 *     dl_phys->dl_used,comp,uncomp
 *     and protecting the dl_tree from being loaded.
 * The locking is provided by dl_lock.  Note that locking on the bpobj_t
 * provides its own locking, and dl_oldfmt is immutable.
 */

/*
 * Livelist Overview
 * ================
 *
 * Livelists use the same 'deadlist_t' struct as deadlists and are also used
 * to track blkptrs over the lifetime of a dataset. Livelists however, belong
 * to clones and track the blkptrs that are clone-specific (were born after
 * the clone's creation). The exception is embedded block pointers which are
 * not included in livelists because they do not need to be freed.
 *
 * When it comes time to delete the clone, the livelist provides a quick
 * reference as to what needs to be freed. For this reason, livelists also track
 * when clone-specific blkptrs are freed before deletion to prevent double
 * frees. Each blkptr in a livelist is marked as a FREE or an ALLOC and the
 * deletion algorithm iterates backwards over the livelist, matching
 * FREE/ALLOC pairs and then freeing those ALLOCs which remain. livelists
 * are also updated in the case when blkptrs are remapped: the old version
 * of the blkptr is cancelled out with a FREE and the new version is tracked
 * with an ALLOC.
 *
 * To bound the amount of memory required for deletion, livelists over a
 * certain size are spread over multiple entries. Entries are grouped by
 * birth txg so we can be sure the ALLOC/FREE pair for a given blkptr will
 * be in the same entry. This allows us to delete livelists incrementally
 * over multiple syncs, one entry at a time.
 *
 * During the lifetime of the clone, livelists can get extremely large.
 * Their size is managed by periodic condensing (preemptively cancelling out
 * FREE/ALLOC pairs). Livelists are disabled when a clone is promoted or when
 * the shared space between the clone and its origin is so small that it
 * doesn't make sense to use livelists anymore.
 */

/*
 * The threshold sublist size at which we create a new sub-livelist for the
 * next txg. However, since blkptrs of the same transaction group must be in
 * the same sub-list, the actual sublist size may exceed this. When picking the
 * size we had to balance the fact that larger sublists mean fewer sublists
 * (decreasing the cost of insertion) against the consideration that sublists
 * will be loaded into memory and shouldn't take up an inordinate amount of
 * space. We settled on ~500000 entries, corresponding to roughly 128M.
 */
unsigned long zfs_livelist_max_entries = 500000;

/*
 * We can approximate how much of a performance gain a livelist will give us
 * based on the percentage of blocks shared between the clone and its origin.
 * 0 percent shared means that the clone has completely diverged and that the
 * old method is maximally effective: every read from the block tree will
 * result in lots of frees. Livelists give us gains when they track blocks
 * scattered across the tree, when one read in the old method might only
 * result in a few frees. Once the clone has been overwritten enough,
 * writes are no longer sparse and we'll no longer get much of a benefit from
 * tracking them with a livelist. We chose a lower limit of 75 percent shared
 * (25 percent overwritten). This means that 1/4 of all block pointers will be
 * freed (e.g. each read frees 256, out of a max of 1024) so we expect livelists
 * to make deletion 4x faster. Once the amount of shared space drops below this
 * threshold, the clone will revert to the old deletion method.
 */
int zfs_livelist_min_percent_shared = 75;

static int
dsl_deadlist_compare(const void *arg1, const void *arg2)
{
	const dsl_deadlist_entry_t *dle1 = arg1;
	const dsl_deadlist_entry_t *dle2 = arg2;

	return (TREE_CMP(dle1->dle_mintxg, dle2->dle_mintxg));
}

static int
dsl_deadlist_cache_compare(const void *arg1, const void *arg2)
{
	const dsl_deadlist_cache_entry_t *dlce1 = arg1;
	const dsl_deadlist_cache_entry_t *dlce2 = arg2;

	return (TREE_CMP(dlce1->dlce_mintxg, dlce2->dlce_mintxg));
}

static void
dsl_deadlist_load_tree(dsl_deadlist_t *dl)
{
	zap_cursor_t zc;
	zap_attribute_t za;
	int error;

	ASSERT(MUTEX_HELD(&dl->dl_lock));

	ASSERT(!dl->dl_oldfmt);
	if (dl->dl_havecache) {
		/*
		 * After loading the tree, the caller may modify the tree,
		 * e.g. to add or remove nodes, or to make a node no longer
		 * refer to the empty_bpobj.  These changes would make the
		 * dl_cache incorrect.  Therefore we discard the cache here,
		 * so that it can't become incorrect.
		 */
		dsl_deadlist_cache_entry_t *dlce;
		void *cookie = NULL;
		while ((dlce = avl_destroy_nodes(&dl->dl_cache, &cookie))
		    != NULL) {
			kmem_free(dlce, sizeof (*dlce));
		}
		avl_destroy(&dl->dl_cache);
		dl->dl_havecache = B_FALSE;
	}
	if (dl->dl_havetree)
		return;

	avl_create(&dl->dl_tree, dsl_deadlist_compare,
	    sizeof (dsl_deadlist_entry_t),
	    offsetof(dsl_deadlist_entry_t, dle_node));
	for (zap_cursor_init(&zc, dl->dl_os, dl->dl_object);
	    (error = zap_cursor_retrieve(&zc, &za)) == 0;
	    zap_cursor_advance(&zc)) {
		dsl_deadlist_entry_t *dle = kmem_alloc(sizeof (*dle), KM_SLEEP);
		dle->dle_mintxg = zfs_strtonum(za.za_name, NULL);

		/*
		 * Prefetch all the bpobj's so that we do that i/o
		 * in parallel.  Then open them all in a second pass.
		 */
		dle->dle_bpobj.bpo_object = za.za_first_integer;
		dmu_prefetch(dl->dl_os, dle->dle_bpobj.bpo_object,
		    0, 0, 0, ZIO_PRIORITY_SYNC_READ);

		avl_add(&dl->dl_tree, dle);
	}
	VERIFY3U(error, ==, ENOENT);
	zap_cursor_fini(&zc);

	for (dsl_deadlist_entry_t *dle = avl_first(&dl->dl_tree);
	    dle != NULL; dle = AVL_NEXT(&dl->dl_tree, dle)) {
		VERIFY0(bpobj_open(&dle->dle_bpobj, dl->dl_os,
		    dle->dle_bpobj.bpo_object));
	}
	dl->dl_havetree = B_TRUE;
}

/*
 * Load only the non-empty bpobj's into the dl_cache.  The cache is an analog
 * of the dl_tree, but contains only non-empty_bpobj nodes from the ZAP. It
 * is used only for gathering space statistics.  The dl_cache has two
 * advantages over the dl_tree:
 *
 * 1. Loading the dl_cache is ~5x faster than loading the dl_tree (if it's
 * mostly empty_bpobj's), due to less CPU overhead to open the empty_bpobj
 * many times and to inquire about its (zero) space stats many times.
 *
 * 2. The dl_cache uses less memory than the dl_tree.  We only need to load
 * the dl_tree of snapshots when deleting a snapshot, after which we free the
 * dl_tree with dsl_deadlist_discard_tree
 */
static void
dsl_deadlist_load_cache(dsl_deadlist_t *dl)
{
	zap_cursor_t zc;
	zap_attribute_t za;
	int error;

	ASSERT(MUTEX_HELD(&dl->dl_lock));

	ASSERT(!dl->dl_oldfmt);
	if (dl->dl_havecache)
		return;

	uint64_t empty_bpobj = dmu_objset_pool(dl->dl_os)->dp_empty_bpobj;

	avl_create(&dl->dl_cache, dsl_deadlist_cache_compare,
	    sizeof (dsl_deadlist_cache_entry_t),
	    offsetof(dsl_deadlist_cache_entry_t, dlce_node));
	for (zap_cursor_init(&zc, dl->dl_os, dl->dl_object);
	    (error = zap_cursor_retrieve(&zc, &za)) == 0;
	    zap_cursor_advance(&zc)) {
		if (za.za_first_integer == empty_bpobj)
			continue;
		dsl_deadlist_cache_entry_t *dlce =
		    kmem_zalloc(sizeof (*dlce), KM_SLEEP);
		dlce->dlce_mintxg = zfs_strtonum(za.za_name, NULL);

		/*
		 * Prefetch all the bpobj's so that we do that i/o
		 * in parallel.  Then open them all in a second pass.
		 */
		dlce->dlce_bpobj = za.za_first_integer;
		dmu_prefetch(dl->dl_os, dlce->dlce_bpobj,
		    0, 0, 0, ZIO_PRIORITY_SYNC_READ);
		avl_add(&dl->dl_cache, dlce);
	}
	VERIFY3U(error, ==, ENOENT);
	zap_cursor_fini(&zc);

	for (dsl_deadlist_cache_entry_t *dlce = avl_first(&dl->dl_cache);
	    dlce != NULL; dlce = AVL_NEXT(&dl->dl_cache, dlce)) {
		bpobj_t bpo;
		VERIFY0(bpobj_open(&bpo, dl->dl_os, dlce->dlce_bpobj));

		VERIFY0(bpobj_space(&bpo,
		    &dlce->dlce_bytes, &dlce->dlce_comp, &dlce->dlce_uncomp));
		bpobj_close(&bpo);
	}
	dl->dl_havecache = B_TRUE;
}

/*
 * Discard the tree to save memory.
 */
void
dsl_deadlist_discard_tree(dsl_deadlist_t *dl)
{
	mutex_enter(&dl->dl_lock);

	if (!dl->dl_havetree) {
		mutex_exit(&dl->dl_lock);
		return;
	}
	dsl_deadlist_entry_t *dle;
	void *cookie = NULL;
	while ((dle = avl_destroy_nodes(&dl->dl_tree, &cookie)) != NULL) {
		bpobj_close(&dle->dle_bpobj);
		kmem_free(dle, sizeof (*dle));
	}
	avl_destroy(&dl->dl_tree);

	dl->dl_havetree = B_FALSE;
	mutex_exit(&dl->dl_lock);
}

void
dsl_deadlist_iterate(dsl_deadlist_t *dl, deadlist_iter_t func, void *args)
{
	dsl_deadlist_entry_t *dle;

	ASSERT(dsl_deadlist_is_open(dl));

	mutex_enter(&dl->dl_lock);
	dsl_deadlist_load_tree(dl);
	mutex_exit(&dl->dl_lock);
	for (dle = avl_first(&dl->dl_tree); dle != NULL;
	    dle = AVL_NEXT(&dl->dl_tree, dle)) {
		if (func(args, dle) != 0)
			break;
	}
}

void
dsl_deadlist_open(dsl_deadlist_t *dl, objset_t *os, uint64_t object)
{
	dmu_object_info_t doi;

	ASSERT(!dsl_deadlist_is_open(dl));

	mutex_init(&dl->dl_lock, NULL, MUTEX_DEFAULT, NULL);
	dl->dl_os = os;
	dl->dl_object = object;
	VERIFY0(dmu_bonus_hold(os, object, dl, &dl->dl_dbuf));
	dmu_object_info_from_db(dl->dl_dbuf, &doi);
	if (doi.doi_type == DMU_OT_BPOBJ) {
		dmu_buf_rele(dl->dl_dbuf, dl);
		dl->dl_dbuf = NULL;
		dl->dl_oldfmt = B_TRUE;
		VERIFY0(bpobj_open(&dl->dl_bpobj, os, object));
		return;
	}

	dl->dl_oldfmt = B_FALSE;
	dl->dl_phys = dl->dl_dbuf->db_data;
	dl->dl_havetree = B_FALSE;
	dl->dl_havecache = B_FALSE;
}

boolean_t
dsl_deadlist_is_open(dsl_deadlist_t *dl)
{
	return (dl->dl_os != NULL);
}

void
dsl_deadlist_close(dsl_deadlist_t *dl)
{
	ASSERT(dsl_deadlist_is_open(dl));
	mutex_destroy(&dl->dl_lock);

	if (dl->dl_oldfmt) {
		dl->dl_oldfmt = B_FALSE;
		bpobj_close(&dl->dl_bpobj);
		dl->dl_os = NULL;
		dl->dl_object = 0;
		return;
	}

	if (dl->dl_havetree) {
		dsl_deadlist_entry_t *dle;
		void *cookie = NULL;
		while ((dle = avl_destroy_nodes(&dl->dl_tree, &cookie))
		    != NULL) {
			bpobj_close(&dle->dle_bpobj);
			kmem_free(dle, sizeof (*dle));
		}
		avl_destroy(&dl->dl_tree);
	}
	if (dl->dl_havecache) {
		dsl_deadlist_cache_entry_t *dlce;
		void *cookie = NULL;
		while ((dlce = avl_destroy_nodes(&dl->dl_cache, &cookie))
		    != NULL) {
			kmem_free(dlce, sizeof (*dlce));
		}
		avl_destroy(&dl->dl_cache);
	}
	dmu_buf_rele(dl->dl_dbuf, dl);
	dl->dl_dbuf = NULL;
	dl->dl_phys = NULL;
	dl->dl_os = NULL;
	dl->dl_object = 0;
}

uint64_t
dsl_deadlist_alloc(objset_t *os, dmu_tx_t *tx)
{
	if (spa_version(dmu_objset_spa(os)) < SPA_VERSION_DEADLISTS)
		return (bpobj_alloc(os, SPA_OLD_MAXBLOCKSIZE, tx));
	return (zap_create(os, DMU_OT_DEADLIST, DMU_OT_DEADLIST_HDR,
	    sizeof (dsl_deadlist_phys_t), tx));
}

void
dsl_deadlist_free(objset_t *os, uint64_t dlobj, dmu_tx_t *tx)
{
	dmu_object_info_t doi;
	zap_cursor_t zc;
	zap_attribute_t za;
	int error;

	VERIFY0(dmu_object_info(os, dlobj, &doi));
	if (doi.doi_type == DMU_OT_BPOBJ) {
		bpobj_free(os, dlobj, tx);
		return;
	}

	for (zap_cursor_init(&zc, os, dlobj);
	    (error = zap_cursor_retrieve(&zc, &za)) == 0;
	    zap_cursor_advance(&zc)) {
		uint64_t obj = za.za_first_integer;
		if (obj == dmu_objset_pool(os)->dp_empty_bpobj)
			bpobj_decr_empty(os, tx);
		else
			bpobj_free(os, obj, tx);
	}
	VERIFY3U(error, ==, ENOENT);
	zap_cursor_fini(&zc);
	VERIFY0(dmu_object_free(os, dlobj, tx));
}

static void
dle_enqueue(dsl_deadlist_t *dl, dsl_deadlist_entry_t *dle,
    const blkptr_t *bp, boolean_t bp_freed, dmu_tx_t *tx)
{
	ASSERT(MUTEX_HELD(&dl->dl_lock));
	if (dle->dle_bpobj.bpo_object ==
	    dmu_objset_pool(dl->dl_os)->dp_empty_bpobj) {
		uint64_t obj = bpobj_alloc(dl->dl_os, SPA_OLD_MAXBLOCKSIZE, tx);
		bpobj_close(&dle->dle_bpobj);
		bpobj_decr_empty(dl->dl_os, tx);
		VERIFY0(bpobj_open(&dle->dle_bpobj, dl->dl_os, obj));
		VERIFY0(zap_update_int_key(dl->dl_os, dl->dl_object,
		    dle->dle_mintxg, obj, tx));
	}
	bpobj_enqueue(&dle->dle_bpobj, bp, bp_freed, tx);
}

static void
dle_enqueue_subobj(dsl_deadlist_t *dl, dsl_deadlist_entry_t *dle,
    uint64_t obj, dmu_tx_t *tx)
{
	ASSERT(MUTEX_HELD(&dl->dl_lock));
	if (dle->dle_bpobj.bpo_object !=
	    dmu_objset_pool(dl->dl_os)->dp_empty_bpobj) {
		bpobj_enqueue_subobj(&dle->dle_bpobj, obj, tx);
	} else {
		bpobj_close(&dle->dle_bpobj);
		bpobj_decr_empty(dl->dl_os, tx);
		VERIFY0(bpobj_open(&dle->dle_bpobj, dl->dl_os, obj));
		VERIFY0(zap_update_int_key(dl->dl_os, dl->dl_object,
		    dle->dle_mintxg, obj, tx));
	}
}

void
dsl_deadlist_insert(dsl_deadlist_t *dl, const blkptr_t *bp, boolean_t bp_freed,
    dmu_tx_t *tx)
{
	dsl_deadlist_entry_t dle_tofind;
	dsl_deadlist_entry_t *dle;
	avl_index_t where;

	if (dl->dl_oldfmt) {
		bpobj_enqueue(&dl->dl_bpobj, bp, bp_freed, tx);
		return;
	}

	mutex_enter(&dl->dl_lock);
	dsl_deadlist_load_tree(dl);

	dmu_buf_will_dirty(dl->dl_dbuf, tx);

	int sign = bp_freed ? -1 : +1;
	dl->dl_phys->dl_used +=
	    sign * bp_get_dsize_sync(dmu_objset_spa(dl->dl_os), bp);
	dl->dl_phys->dl_comp += sign * BP_GET_PSIZE(bp);
	dl->dl_phys->dl_uncomp += sign * BP_GET_UCSIZE(bp);

	dle_tofind.dle_mintxg = bp->blk_birth;
	dle = avl_find(&dl->dl_tree, &dle_tofind, &where);
	if (dle == NULL)
		dle = avl_nearest(&dl->dl_tree, where, AVL_BEFORE);
	else
		dle = AVL_PREV(&dl->dl_tree, dle);

	if (dle == NULL) {
		zfs_panic_recover("blkptr at %p has invalid BLK_BIRTH %llu",
		    bp, (longlong_t)bp->blk_birth);
		dle = avl_first(&dl->dl_tree);
	}

	ASSERT3P(dle, !=, NULL);
	dle_enqueue(dl, dle, bp, bp_freed, tx);
	mutex_exit(&dl->dl_lock);
}

int
dsl_deadlist_insert_alloc_cb(void *arg, const blkptr_t *bp, dmu_tx_t *tx)
{
	dsl_deadlist_t *dl = arg;
	dsl_deadlist_insert(dl, bp, B_FALSE, tx);
	return (0);
}

int
dsl_deadlist_insert_free_cb(void *arg, const blkptr_t *bp, dmu_tx_t *tx)
{
	dsl_deadlist_t *dl = arg;
	dsl_deadlist_insert(dl, bp, B_TRUE, tx);
	return (0);
}

/*
 * Insert new key in deadlist, which must be > all current entries.
 * mintxg is not inclusive.
 */
void
dsl_deadlist_add_key(dsl_deadlist_t *dl, uint64_t mintxg, dmu_tx_t *tx)
{
	uint64_t obj;
	dsl_deadlist_entry_t *dle;

	if (dl->dl_oldfmt)
		return;

	dle = kmem_alloc(sizeof (*dle), KM_SLEEP);
	dle->dle_mintxg = mintxg;

	mutex_enter(&dl->dl_lock);
	dsl_deadlist_load_tree(dl);

	obj = bpobj_alloc_empty(dl->dl_os, SPA_OLD_MAXBLOCKSIZE, tx);
	VERIFY0(bpobj_open(&dle->dle_bpobj, dl->dl_os, obj));
	avl_add(&dl->dl_tree, dle);

	VERIFY0(zap_add_int_key(dl->dl_os, dl->dl_object,
	    mintxg, obj, tx));
	mutex_exit(&dl->dl_lock);
}

/*
 * Remove this key, merging its entries into the previous key.
 */
void
dsl_deadlist_remove_key(dsl_deadlist_t *dl, uint64_t mintxg, dmu_tx_t *tx)
{
	dsl_deadlist_entry_t dle_tofind;
	dsl_deadlist_entry_t *dle, *dle_prev;

	if (dl->dl_oldfmt)
		return;
	mutex_enter(&dl->dl_lock);
	dsl_deadlist_load_tree(dl);

	dle_tofind.dle_mintxg = mintxg;
	dle = avl_find(&dl->dl_tree, &dle_tofind, NULL);
	ASSERT3P(dle, !=, NULL);
	dle_prev = AVL_PREV(&dl->dl_tree, dle);

	dle_enqueue_subobj(dl, dle_prev, dle->dle_bpobj.bpo_object, tx);

	avl_remove(&dl->dl_tree, dle);
	bpobj_close(&dle->dle_bpobj);
	kmem_free(dle, sizeof (*dle));

	VERIFY0(zap_remove_int(dl->dl_os, dl->dl_object, mintxg, tx));
	mutex_exit(&dl->dl_lock);
}

/*
 * Remove a deadlist entry and all of its contents by removing the entry from
 * the deadlist's avl tree, freeing the entry's bpobj and adjusting the
 * deadlist's space accounting accordingly.
 */
void
dsl_deadlist_remove_entry(dsl_deadlist_t *dl, uint64_t mintxg, dmu_tx_t *tx)
{
	uint64_t used, comp, uncomp;
	dsl_deadlist_entry_t dle_tofind;
	dsl_deadlist_entry_t *dle;
	objset_t *os = dl->dl_os;

	if (dl->dl_oldfmt)
		return;

	mutex_enter(&dl->dl_lock);
	dsl_deadlist_load_tree(dl);

	dle_tofind.dle_mintxg = mintxg;
	dle = avl_find(&dl->dl_tree, &dle_tofind, NULL);
	VERIFY3P(dle, !=, NULL);

	avl_remove(&dl->dl_tree, dle);
	VERIFY0(zap_remove_int(os, dl->dl_object, mintxg, tx));
	VERIFY0(bpobj_space(&dle->dle_bpobj, &used, &comp, &uncomp));
	dmu_buf_will_dirty(dl->dl_dbuf, tx);
	dl->dl_phys->dl_used -= used;
	dl->dl_phys->dl_comp -= comp;
	dl->dl_phys->dl_uncomp -= uncomp;
	if (dle->dle_bpobj.bpo_object == dmu_objset_pool(os)->dp_empty_bpobj) {
		bpobj_decr_empty(os, tx);
	} else {
		bpobj_free(os, dle->dle_bpobj.bpo_object, tx);
	}
	bpobj_close(&dle->dle_bpobj);
	kmem_free(dle, sizeof (*dle));
	mutex_exit(&dl->dl_lock);
}

/*
 * Clear out the contents of a deadlist_entry by freeing its bpobj,
 * replacing it with an empty bpobj and adjusting the deadlist's
 * space accounting
 */
void
dsl_deadlist_clear_entry(dsl_deadlist_entry_t *dle, dsl_deadlist_t *dl,
    dmu_tx_t *tx)
{
	uint64_t new_obj, used, comp, uncomp;
	objset_t *os = dl->dl_os;

	mutex_enter(&dl->dl_lock);
	VERIFY0(zap_remove_int(os, dl->dl_object, dle->dle_mintxg, tx));
	VERIFY0(bpobj_space(&dle->dle_bpobj, &used, &comp, &uncomp));
	dmu_buf_will_dirty(dl->dl_dbuf, tx);
	dl->dl_phys->dl_used -= used;
	dl->dl_phys->dl_comp -= comp;
	dl->dl_phys->dl_uncomp -= uncomp;
	if (dle->dle_bpobj.bpo_object == dmu_objset_pool(os)->dp_empty_bpobj)
		bpobj_decr_empty(os, tx);
	else
		bpobj_free(os, dle->dle_bpobj.bpo_object, tx);
	bpobj_close(&dle->dle_bpobj);
	new_obj = bpobj_alloc_empty(os, SPA_OLD_MAXBLOCKSIZE, tx);
	VERIFY0(bpobj_open(&dle->dle_bpobj, os, new_obj));
	VERIFY0(zap_add_int_key(os, dl->dl_object, dle->dle_mintxg,
	    new_obj, tx));
	ASSERT(bpobj_is_empty(&dle->dle_bpobj));
	mutex_exit(&dl->dl_lock);
}

/*
 * Return the first entry in deadlist's avl tree
 */
dsl_deadlist_entry_t *
dsl_deadlist_first(dsl_deadlist_t *dl)
{
	dsl_deadlist_entry_t *dle;

	mutex_enter(&dl->dl_lock);
	dsl_deadlist_load_tree(dl);
	dle = avl_first(&dl->dl_tree);
	mutex_exit(&dl->dl_lock);

	return (dle);
}

/*
 * Return the last entry in deadlist's avl tree
 */
dsl_deadlist_entry_t *
dsl_deadlist_last(dsl_deadlist_t *dl)
{
	dsl_deadlist_entry_t *dle;

	mutex_enter(&dl->dl_lock);
	dsl_deadlist_load_tree(dl);
	dle = avl_last(&dl->dl_tree);
	mutex_exit(&dl->dl_lock);

	return (dle);
}

/*
 * Walk ds's snapshots to regenerate generate ZAP & AVL.
 */
static void
dsl_deadlist_regenerate(objset_t *os, uint64_t dlobj,
    uint64_t mrs_obj, dmu_tx_t *tx)
{
	dsl_deadlist_t dl = { 0 };
	dsl_pool_t *dp = dmu_objset_pool(os);

	dsl_deadlist_open(&dl, os, dlobj);
	if (dl.dl_oldfmt) {
		dsl_deadlist_close(&dl);
		return;
	}

	while (mrs_obj != 0) {
		dsl_dataset_t *ds;
		VERIFY0(dsl_dataset_hold_obj(dp, mrs_obj, FTAG, &ds));
		dsl_deadlist_add_key(&dl,
		    dsl_dataset_phys(ds)->ds_prev_snap_txg, tx);
		mrs_obj = dsl_dataset_phys(ds)->ds_prev_snap_obj;
		dsl_dataset_rele(ds, FTAG);
	}
	dsl_deadlist_close(&dl);
}

uint64_t
dsl_deadlist_clone(dsl_deadlist_t *dl, uint64_t maxtxg,
    uint64_t mrs_obj, dmu_tx_t *tx)
{
	dsl_deadlist_entry_t *dle;
	uint64_t newobj;

	newobj = dsl_deadlist_alloc(dl->dl_os, tx);

	if (dl->dl_oldfmt) {
		dsl_deadlist_regenerate(dl->dl_os, newobj, mrs_obj, tx);
		return (newobj);
	}

	mutex_enter(&dl->dl_lock);
	dsl_deadlist_load_tree(dl);

	for (dle = avl_first(&dl->dl_tree); dle;
	    dle = AVL_NEXT(&dl->dl_tree, dle)) {
		uint64_t obj;

		if (dle->dle_mintxg >= maxtxg)
			break;

		obj = bpobj_alloc_empty(dl->dl_os, SPA_OLD_MAXBLOCKSIZE, tx);
		VERIFY0(zap_add_int_key(dl->dl_os, newobj,
		    dle->dle_mintxg, obj, tx));
	}
	mutex_exit(&dl->dl_lock);
	return (newobj);
}

void
dsl_deadlist_space(dsl_deadlist_t *dl,
    uint64_t *usedp, uint64_t *compp, uint64_t *uncompp)
{
	ASSERT(dsl_deadlist_is_open(dl));
	if (dl->dl_oldfmt) {
		VERIFY0(bpobj_space(&dl->dl_bpobj,
		    usedp, compp, uncompp));
		return;
	}

	mutex_enter(&dl->dl_lock);
	*usedp = dl->dl_phys->dl_used;
	*compp = dl->dl_phys->dl_comp;
	*uncompp = dl->dl_phys->dl_uncomp;
	mutex_exit(&dl->dl_lock);
}

/*
 * return space used in the range (mintxg, maxtxg].
 * Includes maxtxg, does not include mintxg.
 * mintxg and maxtxg must both be keys in the deadlist (unless maxtxg is
 * UINT64_MAX).
 */
void
dsl_deadlist_space_range(dsl_deadlist_t *dl, uint64_t mintxg, uint64_t maxtxg,
    uint64_t *usedp, uint64_t *compp, uint64_t *uncompp)
{
	dsl_deadlist_cache_entry_t *dlce;
	dsl_deadlist_cache_entry_t dlce_tofind;
	avl_index_t where;

	if (dl->dl_oldfmt) {
		VERIFY0(bpobj_space_range(&dl->dl_bpobj,
		    mintxg, maxtxg, usedp, compp, uncompp));
		return;
	}

	*usedp = *compp = *uncompp = 0;

	mutex_enter(&dl->dl_lock);
	dsl_deadlist_load_cache(dl);
	dlce_tofind.dlce_mintxg = mintxg;
	dlce = avl_find(&dl->dl_cache, &dlce_tofind, &where);

	/*
	 * If this mintxg doesn't exist, it may be an empty_bpobj which
	 * is omitted from the sparse tree.  Start at the next non-empty
	 * entry.
	 */
	if (dlce == NULL)
		dlce = avl_nearest(&dl->dl_cache, where, AVL_AFTER);

	for (; dlce && dlce->dlce_mintxg < maxtxg;
	    dlce = AVL_NEXT(&dl->dl_tree, dlce)) {
		*usedp += dlce->dlce_bytes;
		*compp += dlce->dlce_comp;
		*uncompp += dlce->dlce_uncomp;
	}

	mutex_exit(&dl->dl_lock);
}

static void
dsl_deadlist_insert_bpobj(dsl_deadlist_t *dl, uint64_t obj, uint64_t birth,
    dmu_tx_t *tx)
{
	dsl_deadlist_entry_t dle_tofind;
	dsl_deadlist_entry_t *dle;
	avl_index_t where;
	uint64_t used, comp, uncomp;
	bpobj_t bpo;

	ASSERT(MUTEX_HELD(&dl->dl_lock));

	VERIFY0(bpobj_open(&bpo, dl->dl_os, obj));
	VERIFY0(bpobj_space(&bpo, &used, &comp, &uncomp));
	bpobj_close(&bpo);

	dsl_deadlist_load_tree(dl);

	dmu_buf_will_dirty(dl->dl_dbuf, tx);
	dl->dl_phys->dl_used += used;
	dl->dl_phys->dl_comp += comp;
	dl->dl_phys->dl_uncomp += uncomp;

	dle_tofind.dle_mintxg = birth;
	dle = avl_find(&dl->dl_tree, &dle_tofind, &where);
	if (dle == NULL)
		dle = avl_nearest(&dl->dl_tree, where, AVL_BEFORE);
	dle_enqueue_subobj(dl, dle, obj, tx);
}

static int
dsl_deadlist_insert_cb(void *arg, const blkptr_t *bp, boolean_t bp_freed,
    dmu_tx_t *tx)
{
	dsl_deadlist_t *dl = arg;
	dsl_deadlist_insert(dl, bp, bp_freed, tx);
	return (0);
}

/*
 * Merge the deadlist pointed to by 'obj' into dl.  obj will be left as
 * an empty deadlist.
 */
void
dsl_deadlist_merge(dsl_deadlist_t *dl, uint64_t obj, dmu_tx_t *tx)
{
	zap_cursor_t zc;
	zap_attribute_t za;
	dmu_buf_t *bonus;
	dsl_deadlist_phys_t *dlp;
	dmu_object_info_t doi;
	int error;

	VERIFY0(dmu_object_info(dl->dl_os, obj, &doi));
	if (doi.doi_type == DMU_OT_BPOBJ) {
		bpobj_t bpo;
		VERIFY0(bpobj_open(&bpo, dl->dl_os, obj));
		VERIFY0(bpobj_iterate(&bpo, dsl_deadlist_insert_cb, dl, tx));
		bpobj_close(&bpo);
		return;
	}

	mutex_enter(&dl->dl_lock);
	for (zap_cursor_init(&zc, dl->dl_os, obj);
	    (error = zap_cursor_retrieve(&zc, &za)) == 0;
	    zap_cursor_advance(&zc)) {
		uint64_t mintxg = zfs_strtonum(za.za_name, NULL);
		dsl_deadlist_insert_bpobj(dl, za.za_first_integer, mintxg, tx);
		VERIFY0(zap_remove_int(dl->dl_os, obj, mintxg, tx));
	}
	VERIFY3U(error, ==, ENOENT);
	zap_cursor_fini(&zc);

	VERIFY0(dmu_bonus_hold(dl->dl_os, obj, FTAG, &bonus));
	dlp = bonus->db_data;
	dmu_buf_will_dirty(bonus, tx);
	memset(dlp, 0, sizeof (*dlp));
	dmu_buf_rele(bonus, FTAG);
	mutex_exit(&dl->dl_lock);
}

/*
 * Remove entries on dl that are born > mintxg, and put them on the bpobj.
 */
void
dsl_deadlist_move_bpobj(dsl_deadlist_t *dl, bpobj_t *bpo, uint64_t mintxg,
    dmu_tx_t *tx)
{
	dsl_deadlist_entry_t dle_tofind;
	dsl_deadlist_entry_t *dle;
	avl_index_t where;

	ASSERT(!dl->dl_oldfmt);

	mutex_enter(&dl->dl_lock);
	dmu_buf_will_dirty(dl->dl_dbuf, tx);
	dsl_deadlist_load_tree(dl);

	dle_tofind.dle_mintxg = mintxg;
	dle = avl_find(&dl->dl_tree, &dle_tofind, &where);
	if (dle == NULL)
		dle = avl_nearest(&dl->dl_tree, where, AVL_AFTER);
	while (dle) {
		uint64_t used, comp, uncomp;
		dsl_deadlist_entry_t *dle_next;

		bpobj_enqueue_subobj(bpo, dle->dle_bpobj.bpo_object, tx);

		VERIFY0(bpobj_space(&dle->dle_bpobj,
		    &used, &comp, &uncomp));
		ASSERT3U(dl->dl_phys->dl_used, >=, used);
		ASSERT3U(dl->dl_phys->dl_comp, >=, comp);
		ASSERT3U(dl->dl_phys->dl_uncomp, >=, uncomp);
		dl->dl_phys->dl_used -= used;
		dl->dl_phys->dl_comp -= comp;
		dl->dl_phys->dl_uncomp -= uncomp;

		VERIFY0(zap_remove_int(dl->dl_os, dl->dl_object,
		    dle->dle_mintxg, tx));

		dle_next = AVL_NEXT(&dl->dl_tree, dle);
		avl_remove(&dl->dl_tree, dle);
		bpobj_close(&dle->dle_bpobj);
		kmem_free(dle, sizeof (*dle));
		dle = dle_next;
	}
	mutex_exit(&dl->dl_lock);
}

typedef struct livelist_entry {
	blkptr_t le_bp;
	uint32_t le_refcnt;
	avl_node_t le_node;
} livelist_entry_t;

static int
livelist_compare(const void *larg, const void *rarg)
{
	const blkptr_t *l = &((livelist_entry_t *)larg)->le_bp;
	const blkptr_t *r = &((livelist_entry_t *)rarg)->le_bp;

	/* Sort them according to dva[0] */
	uint64_t l_dva0_vdev = DVA_GET_VDEV(&l->blk_dva[0]);
	uint64_t r_dva0_vdev = DVA_GET_VDEV(&r->blk_dva[0]);

	if (l_dva0_vdev != r_dva0_vdev)
		return (TREE_CMP(l_dva0_vdev, r_dva0_vdev));

	/* if vdevs are equal, sort by offsets. */
	uint64_t l_dva0_offset = DVA_GET_OFFSET(&l->blk_dva[0]);
	uint64_t r_dva0_offset = DVA_GET_OFFSET(&r->blk_dva[0]);
	if (l_dva0_offset == r_dva0_offset)
		ASSERT3U(l->blk_birth, ==, r->blk_birth);
	return (TREE_CMP(l_dva0_offset, r_dva0_offset));
}

struct livelist_iter_arg {
	avl_tree_t *avl;
	bplist_t *to_free;
	zthr_t *t;
};

/*
 * Expects an AVL tree which is incrementally filled will FREE blkptrs
 * and used to match up ALLOC/FREE pairs. ALLOC'd blkptrs without a
 * corresponding FREE are stored in the supplied bplist.
 *
 * Note that multiple FREE and ALLOC entries for the same blkptr may
 * be encountered when dedup is involved. For this reason we keep a
 * refcount for all the FREE entries of each blkptr and ensure that
 * each of those FREE entries has a corresponding ALLOC preceding it.
 */
static int
dsl_livelist_iterate(void *arg, const blkptr_t *bp, boolean_t bp_freed,
    dmu_tx_t *tx)
{
	struct livelist_iter_arg *lia = arg;
	avl_tree_t *avl = lia->avl;
	bplist_t *to_free = lia->to_free;
	zthr_t *t = lia->t;
	ASSERT(tx == NULL);

	if ((t != NULL) && (zthr_has_waiters(t) || zthr_iscancelled(t)))
		return (SET_ERROR(EINTR));

	livelist_entry_t node;
	node.le_bp = *bp;
	livelist_entry_t *found = avl_find(avl, &node, NULL);
	if (bp_freed) {
		if (found == NULL) {
			/* first free entry for this blkptr */
			livelist_entry_t *e =
			    kmem_alloc(sizeof (livelist_entry_t), KM_SLEEP);
			e->le_bp = *bp;
			e->le_refcnt = 1;
			avl_add(avl, e);
		} else {
			/* dedup block free */
			ASSERT(BP_GET_DEDUP(bp));
			ASSERT3U(BP_GET_CHECKSUM(bp), ==,
			    BP_GET_CHECKSUM(&found->le_bp));
			ASSERT3U(found->le_refcnt + 1, >, found->le_refcnt);
			found->le_refcnt++;
		}
	} else {
		if (found == NULL) {
			/* block is currently marked as allocated */
			bplist_append(to_free, bp);
		} else {
			/* alloc matches a free entry */
			ASSERT3U(found->le_refcnt, !=, 0);
			found->le_refcnt--;
			if (found->le_refcnt == 0) {
				/* all tracked free pairs have been matched */
				avl_remove(avl, found);
				kmem_free(found, sizeof (livelist_entry_t));
			} else {
				/*
				 * This is definitely a deduped blkptr so
				 * let's validate it.
				 */
				ASSERT(BP_GET_DEDUP(bp));
				ASSERT3U(BP_GET_CHECKSUM(bp), ==,
				    BP_GET_CHECKSUM(&found->le_bp));
			}
		}
	}
	return (0);
}

/*
 * Accepts a bpobj and a bplist. Will insert into the bplist the blkptrs
 * which have an ALLOC entry but no matching FREE
 */
int
dsl_process_sub_livelist(bpobj_t *bpobj, bplist_t *to_free, zthr_t *t,
    uint64_t *size)
{
	avl_tree_t avl;
	avl_create(&avl, livelist_compare, sizeof (livelist_entry_t),
	    offsetof(livelist_entry_t, le_node));

	/* process the sublist */
	struct livelist_iter_arg arg = {
	    .avl = &avl,
	    .to_free = to_free,
	    .t = t
	};
	int err = bpobj_iterate_nofree(bpobj, dsl_livelist_iterate, &arg, size);

	VERIFY0(avl_numnodes(&avl));
	avl_destroy(&avl);
	return (err);
}

ZFS_MODULE_PARAM(zfs_livelist, zfs_livelist_, max_entries, ULONG, ZMOD_RW,
	"Size to start the next sub-livelist in a livelist");

ZFS_MODULE_PARAM(zfs_livelist, zfs_livelist_, min_percent_shared, INT, ZMOD_RW,
	"Threshold at which livelist is disabled");
