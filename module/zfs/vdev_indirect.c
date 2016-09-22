/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2014, 2015 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>
#include <sys/fs/zfs.h>
#include <sys/zio.h>
#include <sys/metaslab.h>
#include <sys/refcount.h>
#include <sys/dmu.h>
#include <sys/vdev_indirect_mapping.h>
#include <sys/dmu_tx.h>
#include <sys/dsl_synctask.h>
#include <sys/zap.h>

/*
 * An indirect vdev corresponds to a vdev that has been removed.  Since
 * we cannot rewrite block pointers of snapshots, etc., we keep a
 * mapping from old location on the removed device to the new location
 * on another device in the pool and use this mapping whenever we need
 * to access the DVA.  Unfortunately, this mapping did not respect
 * logical block boundaries when it was first created, and so a DVA on
 * this indirect vdev may be "split" into multiple sections that each
 * map to a different location.  As a consequence, not all DVAs can be
 * translated to an equivalent new DVA.  Instead we must provide a
 * "vdev_remap" operation that executes a callback on each contiguous
 * segment of the new location.  This function is used in multiple ways:
 *
 *  - reads and repair writes to this device use the callback to create
 *    a child io for each mapped segment.
 *
 *  - frees and claims to this device use the callback to free or claim
 *    each mapped segment.  (Note that we don't actually need to claim
 *    log blocks on indirect vdevs, because we don't allocate to
 *    removing vdevs.  However, zdb uses zio_claim() for its leak
 *    detection.)
 */

/*
 * "Big theory statement" for how we mark blocks obsolete.
 *
 * When a block on an indirect vdev is freed or remapped, a section of
 * that vdev's mapping may no longer be referenced (aka "obsolete").  We
 * keep track of how much of each mapping entry is obsolete.  When
 * an entry becomes completely obsolete, we can remove it, thus reducing
 * the memory used by the mapping.  The complete picture of obsolescence
 * is given by the following data structures, described below:
 *  - the entry-specific obsolete count
 *  - the vdev-specific obsolete spacemap
 *  - the pool-specific obsolete bpobj
 *
 * == On disk data structures used ==
 *
 * We track the obsolete space for the pool using several objects.  Each
 * of these objects is created on demand and freed when no longer
 * needed, and is assumed to be empty if it does not exist.
 * SPA_FEATURE_OBSOLETE_COUNTS includes the count of these objects.
 *
 *  - Each vic_mapping_object (associated with an indirect vdev) can
 *    have a vimp_counts_object.  This is an array of uint32_t's
 *    with the same number of entries as the vic_mapping_object.  When
 *    the mapping is condensed, entries from the vic_obsolete_sm_object
 *    (see below) are folded into the counts.  Therefore, each
 *    obsolete_counts entry tells us the number of bytes in the
 *    corresponding mapping entry that were not referenced when the
 *    mapping was last condensed.
 *
 *  - Each indirect or removing vdev can have a vic_obsolete_sm_object.
 *    This is a space map containing an alloc entry for every DVA that
 *    has been obsoleted since the last time this indirect vdev was
 *    condensed.  We use this object in order to improve performance
 *    when marking a DVA as obsolete.  Instead of modifying an arbitrary
 *    offset of the vimp_counts_object, we only need to append an entry
 *    to the end of this object.  When a DVA becomes obsolete, it is
 *    added to the obsolete space map.  This happens when the DVA is
 *    freed, remapped and not referenced by a snapshot, or the last
 *    snapshot referencing it is destroyed.
 *
 *  - Each dataset can have a ds_remap_deadlist object.  This is a
 *    deadlist object containing all blocks that were remapped in this
 *    dataset but referenced in a previous snapshot.  Blocks can *only*
 *    appear on this list if they were remapped (dsl_dataset_block_remapped);
 *    blocks that were killed in a head dataset are put on the normal
 *    ds_deadlist and marked obsolete when they are freed.
 *
 *  - The pool can have a dp_obsolete_bpobj.  This is a list of blocks
 *    in the pool that need to be marked obsolete.  When a snapshot is
 *    destroyed, we move some of the ds_remap_deadlist to the obsolete
 *    bpobj (see dsl_destroy_snapshot_handle_remaps()).  We then
 *    asynchronously process the obsolete bpobj, moving its entries to
 *    the specific vdevs' obsolete space maps.
 *
 * == Summary of how we mark blocks as obsolete ==
 *
 * - When freeing a block: if any DVA is on an indirect vdev, append to
 *   vic_obsolete_sm_object.
 * - When remapping a block, add dva to ds_remap_deadlist (if prev snap
 *   references; otherwise append to vic_obsolete_sm_object).
 * - When freeing a snapshot: move parts of ds_remap_deadlist to
 *   dp_obsolete_bpobj (same algorithm as ds_deadlist).
 * - When syncing the spa: process dp_obsolete_bpobj, moving ranges to
 *   individual vdev's vic_obsolete_sm_object.
 */

/*
 * "Big theory statement" for how we condense indirect vdevs.
 *
 * Condensing an indirect vdev's mapping is the process of determining
 * the precise counts of obsolete space for each mapping entry (by
 * integrating the obsolete spacemap into the obsolete counts) and
 * writing out a new mapping that contains only referenced entries.
 *
 * We condense a vdev when we expect the mapping to shrink (see
 * vdev_indirect_should_condense()), but only perform one condense at a
 * time to limit the memory usage.  In addition, we use a separate
 * open-context thread (spa_condense_indirect_thread) to incrementally
 * create the new mapping object in a way that minimizes the impact on
 * the rest of the system.
 *
 * == Generating a new mapping ==
 *
 * To generate a new mapping, we follow these steps:
 *
 * 1. Save the old obsolete space map and create a new mapping object
 *    (see spa_condense_indirect_start_sync()).  This initializes the
 *    spa_condensing_indirect_phys with the "previous obsolete space map",
 *    which is now read only.  Newly obsolete DVAs will be added to a
 *    new (initially empty) obsolete space map, and will not be
 *    considered as part of this condense operation.
 *
 * 2. Construct in memory the precise counts of obsolete space for each
 *    mapping entry, by incorporating the obsolete space map into the
 *    counts.  (See vdev_indirect_mapping_load_obsolete_{counts,spacemap}().)
 *
 * 3. Iterate through each mapping entry, writing to the new mapping any
 *    entries that are not completely obsolete (i.e. which don't have
 *    obsolete count == mapping length).  (See
 *    spa_condense_indirect_generate_new_mapping().)
 *
 * 4. Destroy the old mapping object and switch over to the new one
 *    (spa_condense_indirect_complete_sync).
 *
 * == Restarting from failure ==
 *
 * To restart the condense when we import/open the pool, we must start
 * at the 2nd step above: reconstruct the precise counts in memory,
 * based on the space map + counts.  Then in the 3rd step, we start
 * iterating where we left off: at vimp_max_offset of the new mapping
 * object.
 */

boolean_t zfs_condense_indirect_vdevs_enable = B_TRUE;

/*
 * Condense if at least this percent of the bytes in the mapping is
 * obsolete.  With the default of 25%, the amount of space mapped
 * will be reduced to 1% of its original size after at most 16
 * condenses.  Higher values will condense less often (causing less
 * i/o); lower values will reduce the mapping size more quickly.
 */
int zfs_indirect_condense_obsolete_pct = 25;

/*
 * Condense if the obsolete space map takes up more than this amount of
 * space on disk (logically).  This limits the amount of disk space
 * consumed by the obsolete space map; the default of 1GB is small enough
 * that we typically don't mind "wasting" it.
 */
uint64_t zfs_condense_max_obsolete_bytes = 1024 * 1024 * 1024;

/*
 * Don't bother condensing if the mapping uses less than this amount of
 * memory.  The default of 128KB is considered a "trivial" amount of
 * memory and not worth reducing.
 */
unsigned long zfs_condense_min_mapping_bytes = 128 * 1024;

/*
 * This is used by the test suite so that it can ensure that certain
 * actions happen while in the middle of a condense (which might otherwise
 * complete too quickly).  If used to reduce the performance impact of
 * condensing in production, a maximum value of 1 should be sufficient.
 */
int zfs_condense_indirect_commit_entry_delay_ms = 0;

/*
 * Mark the given offset and size as being obsolete in the given txg.
 */
void
vdev_indirect_mark_obsolete(vdev_t *vd, uint64_t offset, uint64_t size,
    uint64_t txg)
{
	spa_t *spa = vd->vdev_spa;
	ASSERT3U(spa_syncing_txg(spa), ==, txg);
	ASSERT3U(vd->vdev_indirect_config.vic_mapping_object, !=, 0);
	ASSERT(vd->vdev_removing || vd->vdev_ops == &vdev_indirect_ops);
	ASSERT(size > 0);
	VERIFY(vdev_indirect_mapping_entry_for_offset(
	    vd->vdev_indirect_mapping, offset) != NULL);

	if (spa_feature_is_enabled(spa, SPA_FEATURE_OBSOLETE_COUNTS)) {
		mutex_enter(&vd->vdev_obsolete_lock);
		range_tree_add(vd->vdev_obsolete_segments, offset, size);
		mutex_exit(&vd->vdev_obsolete_lock);
		vdev_dirty(vd, 0, NULL, txg);
	}
}

/*
 * Mark the DVA vdev_id:offset:size as being obsolete in the given tx. This
 * wrapper is provided because the DMU does not know about vdev_t's and
 * cannot directly call vdev_indirect_mark_obsolete.
 */
void
spa_vdev_indirect_mark_obsolete(spa_t *spa, uint64_t vdev_id, uint64_t offset,
    uint64_t size, dmu_tx_t *tx)
{
	vdev_t *vd = vdev_lookup_top(spa, vdev_id);
	ASSERT(dmu_tx_is_syncing(tx));

	/* The DMU can only remap indirect vdevs. */
	ASSERT3P(vd->vdev_ops, ==, &vdev_indirect_ops);
	vdev_indirect_mark_obsolete(vd, offset, size, dmu_tx_get_txg(tx));
}

static spa_condensing_indirect_t *
spa_condensing_indirect_create(spa_t *spa)
{
	spa_condensing_indirect_phys_t *scip =
	    &spa->spa_condensing_indirect_phys;
	spa_condensing_indirect_t *sci = kmem_zalloc(sizeof (*sci), KM_SLEEP);
	objset_t *mos = spa->spa_meta_objset;

	for (int i = 0; i < TXG_SIZE; i++) {
		list_create(&sci->sci_new_mapping_entries[i],
		    sizeof (vdev_indirect_mapping_entry_t),
		    offsetof(vdev_indirect_mapping_entry_t, vime_node));
	}

	sci->sci_new_mapping =
	    vdev_indirect_mapping_open(mos, scip->scip_next_mapping_object);

	return (sci);
}

static void
spa_condensing_indirect_destroy(spa_condensing_indirect_t *sci)
{
	for (int i = 0; i < TXG_SIZE; i++)
		list_destroy(&sci->sci_new_mapping_entries[i]);

	if (sci->sci_new_mapping != NULL)
		vdev_indirect_mapping_close(sci->sci_new_mapping);

	kmem_free(sci, sizeof (*sci));
}

boolean_t
vdev_indirect_should_condense(vdev_t *vd)
{
	vdev_indirect_mapping_t *vim = vd->vdev_indirect_mapping;
	spa_t *spa = vd->vdev_spa;

	ASSERT(dsl_pool_sync_context(spa->spa_dsl_pool));

	if (!zfs_condense_indirect_vdevs_enable)
		return (B_FALSE);

	/*
	 * We can only condense one indirect vdev at a time.
	 */
	if (spa->spa_condensing_indirect != NULL)
		return (B_FALSE);

	if (spa_shutting_down(spa))
		return (B_FALSE);

	/*
	 * The mapping object size must not change while we are
	 * condensing, so we can only condense indirect vdevs
	 * (not vdevs that are still in the middle of being removed).
	 */
	if (vd->vdev_ops != &vdev_indirect_ops)
		return (B_FALSE);

	/*
	 * If nothing new has been marked obsolete, there is no
	 * point in condensing.
	 */
	if (vd->vdev_obsolete_sm == NULL) {
		ASSERT0(vdev_obsolete_sm_object(vd));
		return (B_FALSE);
	}

	ASSERT(vd->vdev_obsolete_sm != NULL);

	ASSERT3U(vdev_obsolete_sm_object(vd), ==,
	    space_map_object(vd->vdev_obsolete_sm));

	uint64_t bytes_mapped = vdev_indirect_mapping_bytes_mapped(vim);
	uint64_t bytes_obsolete = space_map_allocated(vd->vdev_obsolete_sm);
	uint64_t mapping_size = vdev_indirect_mapping_size(vim);
	uint64_t obsolete_sm_size = space_map_length(vd->vdev_obsolete_sm);

	ASSERT3U(bytes_obsolete, <=, bytes_mapped);

	/*
	 * If a high percentage of the bytes that are mapped have become
	 * obsolete, condense (unless the mapping is already small enough).
	 * This has a good chance of reducing the amount of memory used
	 * by the mapping.
	 */
	if (bytes_obsolete * 100 / bytes_mapped >=
	    zfs_indirect_condense_obsolete_pct &&
	    mapping_size > zfs_condense_min_mapping_bytes) {
		zfs_dbgmsg("should condense vdev %llu because obsolete "
		    "spacemap covers %d%% of %lluMB mapping",
		    (u_longlong_t)vd->vdev_id,
		    (int)(bytes_obsolete * 100 / bytes_mapped),
		    (u_longlong_t)bytes_mapped / 1024 / 1024);
		return (B_TRUE);
	}

	/*
	 * If the obsolete space map takes up too much space on disk,
	 * condense in order to free up this disk space.
	 */
	if (obsolete_sm_size >= zfs_condense_max_obsolete_bytes) {
		zfs_dbgmsg("should condense vdev %llu because obsolete sm "
		    "length %lluMB >= max size %lluMB",
		    (u_longlong_t)vd->vdev_id,
		    (u_longlong_t)obsolete_sm_size / 1024 / 1024,
		    (u_longlong_t)zfs_condense_max_obsolete_bytes /
		    1024 / 1024);
		return (B_TRUE);
	}

	return (B_FALSE);
}

/*
 * This sync task completes (finishes) a condense, deleting the old
 * mapping and replacing it with the new one.
 */
static void
spa_condense_indirect_complete_sync(void *arg, dmu_tx_t *tx)
{
	spa_condensing_indirect_t *sci = arg;
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;
	spa_condensing_indirect_phys_t *scip =
	    &spa->spa_condensing_indirect_phys;
	vdev_t *vd = vdev_lookup_top(spa, scip->scip_vdev);
	vdev_indirect_config_t *vic = &vd->vdev_indirect_config;
	objset_t *mos = spa->spa_meta_objset;
	vdev_indirect_mapping_t *old_mapping = vd->vdev_indirect_mapping;
	uint64_t old_count = vdev_indirect_mapping_num_entries(old_mapping);
	uint64_t new_count =
	    vdev_indirect_mapping_num_entries(sci->sci_new_mapping);

	ASSERT(dmu_tx_is_syncing(tx));
	ASSERT3P(vd->vdev_ops, ==, &vdev_indirect_ops);
	ASSERT3P(sci, ==, spa->spa_condensing_indirect);
	for (int i = 0; i < TXG_SIZE; i++) {
		ASSERT(list_is_empty(&sci->sci_new_mapping_entries[i]));
	}
	ASSERT(vic->vic_mapping_object != 0);
	ASSERT3U(vd->vdev_id, ==, scip->scip_vdev);
	ASSERT(scip->scip_next_mapping_object != 0);
	ASSERT(scip->scip_prev_obsolete_sm_object != 0);

	/*
	 * Reset vdev_indirect_mapping to refer to the new object.
	 */
	rw_enter(&vd->vdev_indirect_rwlock, RW_WRITER);
	vdev_indirect_mapping_close(vd->vdev_indirect_mapping);
	vd->vdev_indirect_mapping = sci->sci_new_mapping;
	rw_exit(&vd->vdev_indirect_rwlock);

	sci->sci_new_mapping = NULL;
	vdev_indirect_mapping_free(mos, vic->vic_mapping_object, tx);
	vic->vic_mapping_object = scip->scip_next_mapping_object;
	scip->scip_next_mapping_object = 0;

	space_map_free_obj(mos, scip->scip_prev_obsolete_sm_object, tx);
	spa_feature_decr(spa, SPA_FEATURE_OBSOLETE_COUNTS, tx);
	scip->scip_prev_obsolete_sm_object = 0;

	scip->scip_vdev = 0;

	VERIFY0(zap_remove(mos, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_CONDENSING_INDIRECT, tx));
	spa_condensing_indirect_destroy(spa->spa_condensing_indirect);
	spa->spa_condensing_indirect = NULL;

	zfs_dbgmsg("finished condense of vdev %llu in txg %llu: "
	    "new mapping object %llu has %llu entries "
	    "(was %llu entries)",
	    vd->vdev_id, dmu_tx_get_txg(tx), vic->vic_mapping_object,
	    new_count, old_count);

	vdev_config_dirty(spa->spa_root_vdev);
}

/*
 * This sync task appends entries to the new mapping object.
 */
static void
spa_condense_indirect_commit_sync(void *arg, dmu_tx_t *tx)
{
	spa_condensing_indirect_t *sci = arg;
	uint64_t txg = dmu_tx_get_txg(tx);
	ASSERTV(spa_t *spa = dmu_tx_pool(tx)->dp_spa);

	ASSERT(dmu_tx_is_syncing(tx));
	ASSERT3P(sci, ==, spa->spa_condensing_indirect);

	vdev_indirect_mapping_add_entries(sci->sci_new_mapping,
	    &sci->sci_new_mapping_entries[txg & TXG_MASK], tx);
	ASSERT(list_is_empty(&sci->sci_new_mapping_entries[txg & TXG_MASK]));
}

/*
 * Open-context function to add one entry to the new mapping.  The new
 * entry will be remembered and written from syncing context.
 */
static void
spa_condense_indirect_commit_entry(spa_t *spa,
    vdev_indirect_mapping_entry_phys_t *vimep, uint32_t count)
{
	spa_condensing_indirect_t *sci = spa->spa_condensing_indirect;

	ASSERT3U(count, <, DVA_GET_ASIZE(&vimep->vimep_dst));

	dmu_tx_t *tx = dmu_tx_create_dd(spa_get_dsl(spa)->dp_mos_dir);
	dmu_tx_hold_space(tx, sizeof (*vimep) + sizeof (count));
	VERIFY0(dmu_tx_assign(tx, TXG_WAIT));
	int txgoff = dmu_tx_get_txg(tx) & TXG_MASK;

	/*
	 * If we are the first entry committed this txg, kick off the sync
	 * task to write to the MOS on our behalf.
	 */
	if (list_is_empty(&sci->sci_new_mapping_entries[txgoff])) {
		dsl_sync_task_nowait(dmu_tx_pool(tx),
		    spa_condense_indirect_commit_sync, sci,
		    0, ZFS_SPACE_CHECK_NONE, tx);
	}

	vdev_indirect_mapping_entry_t *vime =
	    kmem_alloc(sizeof (*vime), KM_SLEEP);
	vime->vime_mapping = *vimep;
	vime->vime_obsolete_count = count;
	list_insert_tail(&sci->sci_new_mapping_entries[txgoff], vime);

	dmu_tx_commit(tx);
}

static void
spa_condense_indirect_generate_new_mapping(vdev_t *vd,
    uint32_t *obsolete_counts, uint64_t start_index)
{
	spa_t *spa = vd->vdev_spa;
	uint64_t mapi = start_index;
	vdev_indirect_mapping_t *old_mapping = vd->vdev_indirect_mapping;
	uint64_t old_num_entries =
	    vdev_indirect_mapping_num_entries(old_mapping);

	ASSERT3P(vd->vdev_ops, ==, &vdev_indirect_ops);
	ASSERT3U(vd->vdev_id, ==, spa->spa_condensing_indirect_phys.scip_vdev);

	zfs_dbgmsg("starting condense of vdev %llu from index %llu",
	    (u_longlong_t)vd->vdev_id,
	    (u_longlong_t)mapi);

	while (mapi < old_num_entries && !spa_shutting_down(spa)) {
		vdev_indirect_mapping_entry_phys_t *entry =
		    &old_mapping->vim_entries[mapi];
		uint64_t entry_size = DVA_GET_ASIZE(&entry->vimep_dst);
		ASSERT3U(obsolete_counts[mapi], <=, entry_size);
		if (obsolete_counts[mapi] < entry_size) {
			spa_condense_indirect_commit_entry(spa, entry,
			    obsolete_counts[mapi]);

			/*
			 * This delay may be requested for testing, debugging,
			 * or performance reasons.
			 */
			hrtime_t now = gethrtime();
			hrtime_t sleep_until = now + MSEC2NSEC(
			    zfs_condense_indirect_commit_entry_delay_ms);
			zfs_sleep_until(sleep_until);
		}

		mapi++;
	}
	if (spa_shutting_down(spa)) {
		zfs_dbgmsg("pausing condense of vdev %llu at index %llu",
		    (u_longlong_t)vd->vdev_id,
		    (u_longlong_t)mapi);
	}
}

static void
spa_condense_indirect_thread(void *arg)
{
	vdev_t *vd = arg;
	spa_t *spa = vd->vdev_spa;
	spa_condensing_indirect_t *sci = spa->spa_condensing_indirect;
	spa_condensing_indirect_phys_t *scip =
	    &spa->spa_condensing_indirect_phys;
	uint32_t *counts;
	uint64_t start_index;
	vdev_indirect_mapping_t *old_mapping = vd->vdev_indirect_mapping;
	space_map_t *prev_obsolete_sm = NULL;

	ASSERT3U(vd->vdev_id, ==, scip->scip_vdev);
	ASSERT(scip->scip_next_mapping_object != 0);
	ASSERT(scip->scip_prev_obsolete_sm_object != 0);
	ASSERT3P(vd->vdev_ops, ==, &vdev_indirect_ops);

	for (int i = 0; i < TXG_SIZE; i++) {
		/*
		 * The list must start out empty in order for the
		 * _commit_sync() sync task to be properly registered
		 * on the first call to _commit_entry(); so it's wise
		 * to double check and ensure we actually are starting
		 * with empty lists.
		 */
		ASSERT(list_is_empty(&sci->sci_new_mapping_entries[i]));
	}

	VERIFY0(space_map_open(&prev_obsolete_sm, spa->spa_meta_objset,
	    scip->scip_prev_obsolete_sm_object, 0, vd->vdev_asize, 0));
	space_map_update(prev_obsolete_sm);
	counts = vdev_indirect_mapping_load_obsolete_counts(old_mapping);
	if (prev_obsolete_sm != NULL) {
		vdev_indirect_mapping_load_obsolete_spacemap(old_mapping,
		    counts, prev_obsolete_sm);
	}
	space_map_close(prev_obsolete_sm);

	/*
	 * Generate new mapping.  Determine what index to continue from
	 * based on the max offset that we've already written in the
	 * new mapping.
	 */
	uint64_t max_offset =
	    vdev_indirect_mapping_max_offset(sci->sci_new_mapping);
	if (max_offset == 0) {
		/* We haven't written anything to the new mapping yet. */
		start_index = 0;
	} else {
		/*
		 * Pick up from where we left off. _entry_for_offset()
		 * returns a pointer into the vim_entries array. If
		 * max_offset is greater than any of the mappings
		 * contained in the table  NULL will be returned and
		 * that indicates we've exhausted our iteration of the
		 * old_mapping.
		 */

		vdev_indirect_mapping_entry_phys_t *entry =
		    vdev_indirect_mapping_entry_for_offset_or_next(old_mapping,
		    max_offset);

		if (entry == NULL) {
			/*
			 * We've already written the whole new mapping.
			 * This special value will cause us to skip the
			 * generate_new_mapping step and just do the sync
			 * task to complete the condense.
			 */
			start_index = UINT64_MAX;
		} else {
			start_index = entry - old_mapping->vim_entries;
			ASSERT3U(start_index, <,
			    vdev_indirect_mapping_num_entries(old_mapping));
		}
	}

	spa_condense_indirect_generate_new_mapping(vd, counts, start_index);

	vdev_indirect_mapping_free_obsolete_counts(old_mapping, counts);

	/*
	 * We may have bailed early from generate_new_mapping(), if
	 * the spa is shutting down.  In this case, do not complete
	 * the condense.
	 */
	if (!spa_shutting_down(spa)) {
		VERIFY0(dsl_sync_task(spa_name(spa), NULL,
		    spa_condense_indirect_complete_sync, sci, 0,
		    ZFS_SPACE_CHECK_NONE));
	}

	mutex_enter(&spa->spa_async_lock);
	spa->spa_condense_thread = NULL;
	cv_broadcast(&spa->spa_async_cv);
	mutex_exit(&spa->spa_async_lock);
}

/*
 * Sync task to begin the condensing process.
 */
void
spa_condense_indirect_start_sync(vdev_t *vd, dmu_tx_t *tx)
{
	spa_t *spa = vd->vdev_spa;
	spa_condensing_indirect_phys_t *scip =
	    &spa->spa_condensing_indirect_phys;

	ASSERT0(scip->scip_next_mapping_object);
	ASSERT0(scip->scip_prev_obsolete_sm_object);
	ASSERT0(scip->scip_vdev);
	ASSERT(dmu_tx_is_syncing(tx));
	ASSERT3P(vd->vdev_ops, ==, &vdev_indirect_ops);
	ASSERT(spa_feature_is_active(spa, SPA_FEATURE_OBSOLETE_COUNTS));
	ASSERT(vdev_indirect_mapping_num_entries(vd->vdev_indirect_mapping));

	uint64_t obsolete_sm_obj = vdev_obsolete_sm_object(vd);
	ASSERT(obsolete_sm_obj != 0);

	scip->scip_vdev = vd->vdev_id;
	scip->scip_next_mapping_object =
	    vdev_indirect_mapping_alloc(spa->spa_meta_objset, tx);

	scip->scip_prev_obsolete_sm_object = obsolete_sm_obj;

	/*
	 * We don't need to allocate a new space map object, since
	 * vdev_indirect_sync_obsolete will allocate one when needed.
	 */
	space_map_close(vd->vdev_obsolete_sm);
	vd->vdev_obsolete_sm = NULL;
	VERIFY0(zap_remove(spa->spa_meta_objset, vd->vdev_top_zap,
	    VDEV_TOP_ZAP_INDIRECT_OBSOLETE_SM, tx));

	VERIFY0(zap_add(spa->spa_dsl_pool->dp_meta_objset,
	    DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_CONDENSING_INDIRECT, sizeof (uint64_t),
	    sizeof (*scip) / sizeof (uint64_t), scip, tx));

	ASSERT3P(spa->spa_condensing_indirect, ==, NULL);
	spa->spa_condensing_indirect = spa_condensing_indirect_create(spa);

	zfs_dbgmsg("starting condense of vdev %llu in txg %llu: "
	    "posm=%llu nm=%llu",
	    vd->vdev_id, dmu_tx_get_txg(tx),
	    (u_longlong_t)scip->scip_prev_obsolete_sm_object,
	    (u_longlong_t)scip->scip_next_mapping_object);

	ASSERT3P(spa->spa_condense_thread, ==, NULL);
	spa->spa_condense_thread = thread_create(NULL, 0,
	    spa_condense_indirect_thread, vd, 0, &p0, TS_RUN, minclsyspri);
}

/*
 * Sync to the given vdev's obsolete space map any segments that are no longer
 * referenced as of the given txg.
 *
 * If the obsolete space map doesn't exist yet, create and open it.
 */
void
vdev_indirect_sync_obsolete(vdev_t *vd, dmu_tx_t *tx)
{
	spa_t *spa = vd->vdev_spa;
	ASSERTV(vdev_indirect_config_t *vic = &vd->vdev_indirect_config);

	ASSERT3U(vic->vic_mapping_object, !=, 0);
	ASSERT(range_tree_space(vd->vdev_obsolete_segments) > 0);
	ASSERT(vd->vdev_removing || vd->vdev_ops == &vdev_indirect_ops);
	ASSERT(spa_feature_is_enabled(spa, SPA_FEATURE_OBSOLETE_COUNTS));

	if (vdev_obsolete_sm_object(vd) == 0) {
		uint64_t obsolete_sm_object =
		    space_map_alloc(spa->spa_meta_objset, tx);

		ASSERT(vd->vdev_top_zap != 0);
		VERIFY0(zap_add(vd->vdev_spa->spa_meta_objset, vd->vdev_top_zap,
		    VDEV_TOP_ZAP_INDIRECT_OBSOLETE_SM,
		    sizeof (obsolete_sm_object), 1, &obsolete_sm_object, tx));
		ASSERT3U(vdev_obsolete_sm_object(vd), !=, 0);

		spa_feature_incr(spa, SPA_FEATURE_OBSOLETE_COUNTS, tx);
		VERIFY0(space_map_open(&vd->vdev_obsolete_sm,
		    spa->spa_meta_objset, obsolete_sm_object,
		    0, vd->vdev_asize, 0));
		space_map_update(vd->vdev_obsolete_sm);
	}

	ASSERT(vd->vdev_obsolete_sm != NULL);
	ASSERT3U(vdev_obsolete_sm_object(vd), ==,
	    space_map_object(vd->vdev_obsolete_sm));

	space_map_write(vd->vdev_obsolete_sm,
	    vd->vdev_obsolete_segments, SM_ALLOC, tx);
	space_map_update(vd->vdev_obsolete_sm);
	range_tree_vacate(vd->vdev_obsolete_segments, NULL, NULL);
}

int
spa_condense_init(spa_t *spa)
{
	int error = zap_lookup(spa->spa_meta_objset,
	    DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_CONDENSING_INDIRECT, sizeof (uint64_t),
	    sizeof (spa->spa_condensing_indirect_phys) / sizeof (uint64_t),
	    &spa->spa_condensing_indirect_phys);
	if (error == 0) {
		if (spa_writeable(spa)) {
			spa->spa_condensing_indirect =
			    spa_condensing_indirect_create(spa);
		}
		return (0);
	} else if (error == ENOENT) {
		return (0);
	} else {
		return (error);
	}
}

void
spa_condense_fini(spa_t *spa)
{
	if (spa->spa_condensing_indirect != NULL) {
		spa_condensing_indirect_destroy(spa->spa_condensing_indirect);
		spa->spa_condensing_indirect = NULL;
	}
}

/*
 * Restart the condense - called when the pool is opened.
 */
void
spa_condense_indirect_restart(spa_t *spa)
{
	vdev_t *vd;
	ASSERT(spa->spa_condensing_indirect != NULL);
	spa_config_enter(spa, SCL_VDEV, FTAG, RW_READER);
	vd = vdev_lookup_top(spa,
	    spa->spa_condensing_indirect_phys.scip_vdev);
	ASSERT(vd != NULL);
	spa_config_exit(spa, SCL_VDEV, FTAG);

	ASSERT3P(spa->spa_condense_thread, ==, NULL);
	spa->spa_condense_thread = thread_create(NULL, 0,
	    spa_condense_indirect_thread, vd, 0, &p0, TS_RUN,
	    minclsyspri);
}

/*
 * Gets the obsolete spacemap object from the vdev's ZAP.
 * Returns the spacemap object, or 0 if it wasn't in the ZAP or the ZAP doesn't
 * exist yet.
 */
int
vdev_obsolete_sm_object(vdev_t *vd)
{
	ASSERT0(spa_config_held(vd->vdev_spa, SCL_ALL, RW_WRITER));
	if (vd->vdev_top_zap == 0) {
		return (0);
	}

	uint64_t sm_obj = 0;
	int err;
	err = zap_lookup(vd->vdev_spa->spa_meta_objset, vd->vdev_top_zap,
	    VDEV_TOP_ZAP_INDIRECT_OBSOLETE_SM, sizeof (sm_obj), 1, &sm_obj);

	ASSERT(err == 0 || err == ENOENT);

	return (sm_obj);
}

boolean_t
vdev_obsolete_counts_are_precise(vdev_t *vd)
{
	ASSERT0(spa_config_held(vd->vdev_spa, SCL_ALL, RW_WRITER));
	if (vd->vdev_top_zap == 0) {
		return (B_FALSE);
	}

	uint64_t val = 0;
	int err;
	err = zap_lookup(vd->vdev_spa->spa_meta_objset, vd->vdev_top_zap,
	    VDEV_TOP_ZAP_OBSOLETE_COUNTS_ARE_PRECISE, sizeof (val), 1, &val);

	ASSERT(err == 0 || err == ENOENT);

	return (val != 0);
}

/* ARGSUSED */
static void
vdev_indirect_close(vdev_t *vd)
{
}

/* ARGSUSED */
static void
vdev_indirect_io_done(zio_t *zio)
{
}

/* ARGSUSED */
static int
vdev_indirect_open(vdev_t *vd, uint64_t *psize, uint64_t *max_psize,
    uint64_t *ashift)
{
	*psize = *max_psize = vd->vdev_asize +
	    VDEV_LABEL_START_SIZE + VDEV_LABEL_END_SIZE;
	*ashift = vd->vdev_ashift;
	return (0);
}

typedef struct remap_segment {
	vdev_t *rs_vd;
	uint64_t rs_offset;
	uint64_t rs_asize;
	uint64_t rs_split_offset;
	list_node_t rs_node;
} remap_segment_t;

remap_segment_t *
rs_alloc(vdev_t *vd, uint64_t offset, uint64_t asize, uint64_t split_offset)
{
	remap_segment_t *rs = kmem_alloc(sizeof (remap_segment_t), KM_SLEEP);
	rs->rs_vd = vd;
	rs->rs_offset = offset;
	rs->rs_asize = asize;
	rs->rs_split_offset = split_offset;
	return (rs);
}

/*
 * Goes through the relevant indirect mappings until it hits a concrete vdev
 * and issues the callback. On the way to the concrete vdev, if any other
 * indirect vdevs are encountered, then the callback will also be called on
 * each of those indirect vdevs. For example, if the segment is mapped to
 * segment A on indirect vdev 1, and then segment A on indirect vdev 1 is
 * mapped to segment B on concrete vdev 2, then the callback will be called on
 * both vdev 1 and vdev 2.
 *
 * While the callback passed to vdev_indirect_remap() is called on every vdev
 * the function encounters, certain callbacks only care about concrete vdevs.
 * These types of callbacks should return immediately and explicitly when they
 * are called on an indirect vdev.
 *
 * Because there is a possibility that a DVA section in the indirect device
 * has been split into multiple sections in our mapping, we keep track
 * of the relevant contiguous segments of the new location (remap_segment_t)
 * in a stack. This way we can call the callback for each of the new sections
 * created by a single section of the indirect device. Note though, that in
 * this scenario the callbacks in each split block won't occur in-order in
 * terms of offset, so callers should not make any assumptions about that.
 *
 * For callbacks that don't handle split blocks and immediately return when
 * they encounter them (as is the case for remap_blkptr_cb), the caller can
 * assume that its callback will be applied from the first indirect vdev
 * encountered to the last one and then the concrete vdev, in that order.
 */
static void
vdev_indirect_remap(vdev_t *vd, uint64_t offset, uint64_t asize,
    void (*func)(uint64_t, vdev_t *, uint64_t, uint64_t, void *), void *arg)
{
	list_t stack;
	spa_t *spa = vd->vdev_spa;

	list_create(&stack, sizeof (remap_segment_t),
	    offsetof(remap_segment_t, rs_node));

	for (remap_segment_t *rs = rs_alloc(vd, offset, asize, 0);
	    rs != NULL; rs = list_remove_head(&stack)) {
		vdev_t *v = rs->rs_vd;

		/*
		 * Note: this can be called from open context
		 * (eg. zio_read()), so we need the rwlock to prevent
		 * the mapping from being changed by condensing.
		 */
		rw_enter(&v->vdev_indirect_rwlock, RW_READER);
		vdev_indirect_mapping_t *vim = v->vdev_indirect_mapping;
		ASSERT3P(vim, !=, NULL);

		ASSERT(spa_config_held(spa, SCL_ALL, RW_READER) != 0);
		ASSERT(rs->rs_asize > 0);

		vdev_indirect_mapping_entry_phys_t *mapping =
		    vdev_indirect_mapping_entry_for_offset(vim, rs->rs_offset);
		ASSERT3P(mapping, !=, NULL);

		while (rs->rs_asize > 0) {
			/*
			 * Note: the vdev_indirect_mapping can not change
			 * while we are running.  It only changes while the
			 * removal is in progress, and then only from syncing
			 * context. While a removal is in progress, this
			 * function is only called for frees, which also only
			 * happen from syncing context.
			 */

			uint64_t size = DVA_GET_ASIZE(&mapping->vimep_dst);
			uint64_t dst_offset =
			    DVA_GET_OFFSET(&mapping->vimep_dst);
			uint64_t dst_vdev = DVA_GET_VDEV(&mapping->vimep_dst);

			ASSERT3U(rs->rs_offset, >=,
			    DVA_MAPPING_GET_SRC_OFFSET(mapping));
			ASSERT3U(rs->rs_offset, <,
			    DVA_MAPPING_GET_SRC_OFFSET(mapping) + size);
			ASSERT3U(dst_vdev, !=, v->vdev_id);

			uint64_t inner_offset = rs->rs_offset -
			    DVA_MAPPING_GET_SRC_OFFSET(mapping);
			uint64_t inner_size =
			    MIN(rs->rs_asize, size - inner_offset);

			vdev_t *dst_v = vdev_lookup_top(spa, dst_vdev);
			ASSERT3P(dst_v, !=, NULL);

			if (dst_v->vdev_ops == &vdev_indirect_ops) {
				list_insert_head(&stack,
				    rs_alloc(dst_v, dst_offset + inner_offset,
				    inner_size, rs->rs_split_offset));

			}

			if ((zfs_flags & ZFS_DEBUG_INDIRECT_REMAP) &&
			    IS_P2ALIGNED(inner_size, 2 * SPA_MINBLOCKSIZE)) {
				/*
				 * Note: This clause exists only solely for
				 * testing purposes. We use it to ensure that
				 * split blocks work and that the callbacks
				 * using them yield the same result if issued
				 * in reverse order.
				 */
				uint64_t inner_half = inner_size / 2;

				func(rs->rs_split_offset + inner_half, dst_v,
				    dst_offset + inner_offset + inner_half,
				    inner_half, arg);

				func(rs->rs_split_offset, dst_v,
				    dst_offset + inner_offset,
				    inner_half, arg);
			} else {
				func(rs->rs_split_offset, dst_v,
				    dst_offset + inner_offset,
				    inner_size, arg);
			}

			rs->rs_offset += inner_size;
			rs->rs_asize -= inner_size;
			rs->rs_split_offset += inner_size;
			mapping++;
		}

		rw_exit(&v->vdev_indirect_rwlock);
		kmem_free(rs, sizeof (remap_segment_t));
	}
	list_destroy(&stack);
}

static void
vdev_indirect_child_io_done(zio_t *zio)
{
	zio_t *pio = zio->io_private;

	mutex_enter(&pio->io_lock);
	pio->io_error = zio_worst_error(pio->io_error, zio->io_error);
	mutex_exit(&pio->io_lock);

	abd_put(zio->io_abd);
}

static void
vdev_indirect_io_start_cb(uint64_t split_offset, vdev_t *vd, uint64_t offset,
    uint64_t size, void *arg)
{
	zio_t *zio = arg;

	ASSERT3P(vd, !=, NULL);

	if (vd->vdev_ops == &vdev_indirect_ops)
		return;

	zio_nowait(zio_vdev_child_io(zio, NULL, vd, offset,
	    abd_get_offset(zio->io_abd, split_offset),
	    size, zio->io_type, zio->io_priority,
	    0, vdev_indirect_child_io_done, zio));
}

static void
vdev_indirect_io_start(zio_t *zio)
{
	ASSERTV(spa_t *spa = zio->io_spa);

	ASSERT(spa_config_held(spa, SCL_ALL, RW_READER) != 0);
	if (zio->io_type != ZIO_TYPE_READ) {
		ASSERT3U(zio->io_type, ==, ZIO_TYPE_WRITE);
		ASSERT((zio->io_flags &
		    (ZIO_FLAG_SELF_HEAL | ZIO_FLAG_INDUCE_DAMAGE)) != 0);
	}

	vdev_indirect_remap(zio->io_vd, zio->io_offset, zio->io_size,
	    vdev_indirect_io_start_cb, zio);

	zio_execute(zio);
}

vdev_ops_t vdev_indirect_ops = {
	vdev_indirect_open,
	vdev_indirect_close,
	vdev_default_asize,
	vdev_indirect_io_start,
	vdev_indirect_io_done,
	NULL,
	NULL,
	NULL,
	NULL,
	vdev_indirect_remap,
	VDEV_TYPE_INDIRECT,	/* name of this vdev type */
	B_FALSE			/* leaf vdev */
};

#if defined(_KERNEL) && defined(HAVE_SPL)
EXPORT_SYMBOL(rs_alloc);
EXPORT_SYMBOL(spa_condense_fini);
EXPORT_SYMBOL(spa_condense_indirect_restart);
EXPORT_SYMBOL(spa_condense_indirect_start_sync);
EXPORT_SYMBOL(spa_condense_init);
EXPORT_SYMBOL(spa_vdev_indirect_mark_obsolete);
EXPORT_SYMBOL(vdev_indirect_mark_obsolete);
EXPORT_SYMBOL(vdev_indirect_should_condense);
EXPORT_SYMBOL(vdev_indirect_sync_obsolete);
EXPORT_SYMBOL(vdev_obsolete_counts_are_precise);
EXPORT_SYMBOL(vdev_obsolete_sm_object);

/* CSTYLED */
module_param(zfs_condense_min_mapping_bytes, ulong, 0644);
MODULE_PARM_DESC(zfs_condense_min_mapping_bytes,
	"Minimum size of vdev mapping to condense");

module_param(zfs_condense_indirect_commit_entry_delay_ms, int, 0644);
MODULE_PARM_DESC(zfs_condense_indirect_commit_entry_delay_ms,
	"Delay while condensing vdev mapping");
#endif
