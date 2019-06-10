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
 * Copyright (c) 2011, 2019 by Delphix. All rights reserved.
 * Copyright (c) 2013 by Saso Kiselkov. All rights reserved.
 * Copyright (c) 2017, Intel Corporation.
 */

#include <sys/zfs_context.h>
#include <sys/dmu.h>
#include <sys/dmu_tx.h>
#include <sys/space_map.h>
#include <sys/metaslab_impl.h>
#include <sys/vdev_impl.h>
#include <sys/zio.h>
#include <sys/spa_impl.h>
#include <sys/zfeature.h>
#include <sys/vdev_indirect_mapping.h>
#include <sys/zap.h>

#define	WITH_DF_BLOCK_ALLOCATOR

#define	GANG_ALLOCATION(flags) \
	((flags) & (METASLAB_GANG_CHILD | METASLAB_GANG_HEADER))

/*
 * Metaslab granularity, in bytes. This is roughly similar to what would be
 * referred to as the "stripe size" in traditional RAID arrays. In normal
 * operation, we will try to write this amount of data to a top-level vdev
 * before moving on to the next one.
 */
unsigned long metaslab_aliquot = 512 << 10;

/*
 * For testing, make some blocks above a certain size be gang blocks.
 */
unsigned long metaslab_force_ganging = SPA_MAXBLOCKSIZE + 1;

/*
 * Since we can touch multiple metaslabs (and their respective space maps)
 * with each transaction group, we benefit from having a smaller space map
 * block size since it allows us to issue more I/O operations scattered
 * around the disk.
 */
int zfs_metaslab_sm_blksz = (1 << 12);

/*
 * The in-core space map representation is more compact than its on-disk form.
 * The zfs_condense_pct determines how much more compact the in-core
 * space map representation must be before we compact it on-disk.
 * Values should be greater than or equal to 100.
 */
int zfs_condense_pct = 200;

/*
 * Condensing a metaslab is not guaranteed to actually reduce the amount of
 * space used on disk. In particular, a space map uses data in increments of
 * MAX(1 << ashift, space_map_blksz), so a metaslab might use the
 * same number of blocks after condensing. Since the goal of condensing is to
 * reduce the number of IOPs required to read the space map, we only want to
 * condense when we can be sure we will reduce the number of blocks used by the
 * space map. Unfortunately, we cannot precisely compute whether or not this is
 * the case in metaslab_should_condense since we are holding ms_lock. Instead,
 * we apply the following heuristic: do not condense a spacemap unless the
 * uncondensed size consumes greater than zfs_metaslab_condense_block_threshold
 * blocks.
 */
int zfs_metaslab_condense_block_threshold = 4;

/*
 * The zfs_mg_noalloc_threshold defines which metaslab groups should
 * be eligible for allocation. The value is defined as a percentage of
 * free space. Metaslab groups that have more free space than
 * zfs_mg_noalloc_threshold are always eligible for allocations. Once
 * a metaslab group's free space is less than or equal to the
 * zfs_mg_noalloc_threshold the allocator will avoid allocating to that
 * group unless all groups in the pool have reached zfs_mg_noalloc_threshold.
 * Once all groups in the pool reach zfs_mg_noalloc_threshold then all
 * groups are allowed to accept allocations. Gang blocks are always
 * eligible to allocate on any metaslab group. The default value of 0 means
 * no metaslab group will be excluded based on this criterion.
 */
int zfs_mg_noalloc_threshold = 0;

/*
 * Metaslab groups are considered eligible for allocations if their
 * fragmenation metric (measured as a percentage) is less than or
 * equal to zfs_mg_fragmentation_threshold. If a metaslab group
 * exceeds this threshold then it will be skipped unless all metaslab
 * groups within the metaslab class have also crossed this threshold.
 *
 * This tunable was introduced to avoid edge cases where we continue
 * allocating from very fragmented disks in our pool while other, less
 * fragmented disks, exists. On the other hand, if all disks in the
 * pool are uniformly approaching the threshold, the threshold can
 * be a speed bump in performance, where we keep switching the disks
 * that we allocate from (e.g. we allocate some segments from disk A
 * making it bypassing the threshold while freeing segments from disk
 * B getting its fragmentation below the threshold).
 *
 * Empirically, we've seen that our vdev selection for allocations is
 * good enough that fragmentation increases uniformly across all vdevs
 * the majority of the time. Thus we set the threshold percentage high
 * enough to avoid hitting the speed bump on pools that are being pushed
 * to the edge.
 */
int zfs_mg_fragmentation_threshold = 95;

/*
 * Allow metaslabs to keep their active state as long as their fragmentation
 * percentage is less than or equal to zfs_metaslab_fragmentation_threshold. An
 * active metaslab that exceeds this threshold will no longer keep its active
 * status allowing better metaslabs to be selected.
 */
int zfs_metaslab_fragmentation_threshold = 70;

/*
 * When set will load all metaslabs when pool is first opened.
 */
int metaslab_debug_load = 0;

/*
 * When set will prevent metaslabs from being unloaded.
 */
int metaslab_debug_unload = 0;

/*
 * Minimum size which forces the dynamic allocator to change
 * it's allocation strategy.  Once the space map cannot satisfy
 * an allocation of this size then it switches to using more
 * aggressive strategy (i.e search by size rather than offset).
 */
uint64_t metaslab_df_alloc_threshold = SPA_OLD_MAXBLOCKSIZE;

/*
 * The minimum free space, in percent, which must be available
 * in a space map to continue allocations in a first-fit fashion.
 * Once the space map's free space drops below this level we dynamically
 * switch to using best-fit allocations.
 */
int metaslab_df_free_pct = 4;

/*
 * Maximum distance to search forward from the last offset. Without this
 * limit, fragmented pools can see >100,000 iterations and
 * metaslab_block_picker() becomes the performance limiting factor on
 * high-performance storage.
 *
 * With the default setting of 16MB, we typically see less than 500
 * iterations, even with very fragmented, ashift=9 pools. The maximum number
 * of iterations possible is:
 *     metaslab_df_max_search / (2 * (1<<ashift))
 * With the default setting of 16MB this is 16*1024 (with ashift=9) or
 * 2048 (with ashift=12).
 */
int metaslab_df_max_search = 16 * 1024 * 1024;

/*
 * If we are not searching forward (due to metaslab_df_max_search,
 * metaslab_df_free_pct, or metaslab_df_alloc_threshold), this tunable
 * controls what segment is used.  If it is set, we will use the largest free
 * segment.  If it is not set, we will use a segment of exactly the requested
 * size (or larger).
 */
int metaslab_df_use_largest_segment = B_FALSE;

/*
 * Percentage of all cpus that can be used by the metaslab taskq.
 */
int metaslab_load_pct = 50;

/*
 * Determines how many txgs a metaslab may remain loaded without having any
 * allocations from it. As long as a metaslab continues to be used we will
 * keep it loaded.
 */
int metaslab_unload_delay = TXG_SIZE * 2;

/*
 * Max number of metaslabs per group to preload.
 */
int metaslab_preload_limit = SPA_DVAS_PER_BP;

/*
 * Enable/disable preloading of metaslab.
 */
int metaslab_preload_enabled = B_TRUE;

/*
 * Enable/disable fragmentation weighting on metaslabs.
 */
int metaslab_fragmentation_factor_enabled = B_TRUE;

/*
 * Enable/disable lba weighting (i.e. outer tracks are given preference).
 */
int metaslab_lba_weighting_enabled = B_TRUE;

/*
 * Enable/disable metaslab group biasing.
 */
int metaslab_bias_enabled = B_TRUE;

/*
 * Enable/disable remapping of indirect DVAs to their concrete vdevs.
 */
boolean_t zfs_remap_blkptr_enable = B_TRUE;

/*
 * Enable/disable segment-based metaslab selection.
 */
int zfs_metaslab_segment_weight_enabled = B_TRUE;

/*
 * When using segment-based metaslab selection, we will continue
 * allocating from the active metaslab until we have exhausted
 * zfs_metaslab_switch_threshold of its buckets.
 */
int zfs_metaslab_switch_threshold = 2;

/*
 * Internal switch to enable/disable the metaslab allocation tracing
 * facility.
 */
#ifdef _METASLAB_TRACING
boolean_t metaslab_trace_enabled = B_TRUE;
#endif

/*
 * Maximum entries that the metaslab allocation tracing facility will keep
 * in a given list when running in non-debug mode. We limit the number
 * of entries in non-debug mode to prevent us from using up too much memory.
 * The limit should be sufficiently large that we don't expect any allocation
 * to every exceed this value. In debug mode, the system will panic if this
 * limit is ever reached allowing for further investigation.
 */
#ifdef _METASLAB_TRACING
uint64_t metaslab_trace_max_entries = 5000;
#endif

/*
 * Maximum number of metaslabs per group that can be disabled
 * simultaneously.
 */
int max_disabled_ms = 3;

static uint64_t metaslab_weight(metaslab_t *);
static void metaslab_set_fragmentation(metaslab_t *);
static void metaslab_free_impl(vdev_t *, uint64_t, uint64_t, boolean_t);
static void metaslab_check_free_impl(vdev_t *, uint64_t, uint64_t);

static void metaslab_passivate(metaslab_t *msp, uint64_t weight);
static uint64_t metaslab_weight_from_range_tree(metaslab_t *msp);
#ifdef _METASLAB_TRACING
kmem_cache_t *metaslab_alloc_trace_cache;
#endif

/*
 * ==========================================================================
 * Metaslab classes
 * ==========================================================================
 */
metaslab_class_t *
metaslab_class_create(spa_t *spa, metaslab_ops_t *ops)
{
	metaslab_class_t *mc;

	mc = kmem_zalloc(sizeof (metaslab_class_t), KM_SLEEP);

	mc->mc_spa = spa;
	mc->mc_rotor = NULL;
	mc->mc_ops = ops;
	mutex_init(&mc->mc_lock, NULL, MUTEX_DEFAULT, NULL);
	mc->mc_alloc_slots = kmem_zalloc(spa->spa_alloc_count *
	    sizeof (zfs_refcount_t), KM_SLEEP);
	mc->mc_alloc_max_slots = kmem_zalloc(spa->spa_alloc_count *
	    sizeof (uint64_t), KM_SLEEP);
	for (int i = 0; i < spa->spa_alloc_count; i++)
		zfs_refcount_create_tracked(&mc->mc_alloc_slots[i]);

	return (mc);
}

void
metaslab_class_destroy(metaslab_class_t *mc)
{
	ASSERT(mc->mc_rotor == NULL);
	ASSERT(mc->mc_alloc == 0);
	ASSERT(mc->mc_deferred == 0);
	ASSERT(mc->mc_space == 0);
	ASSERT(mc->mc_dspace == 0);

	for (int i = 0; i < mc->mc_spa->spa_alloc_count; i++)
		zfs_refcount_destroy(&mc->mc_alloc_slots[i]);
	kmem_free(mc->mc_alloc_slots, mc->mc_spa->spa_alloc_count *
	    sizeof (zfs_refcount_t));
	kmem_free(mc->mc_alloc_max_slots, mc->mc_spa->spa_alloc_count *
	    sizeof (uint64_t));
	mutex_destroy(&mc->mc_lock);
	kmem_free(mc, sizeof (metaslab_class_t));
}

int
metaslab_class_validate(metaslab_class_t *mc)
{
	metaslab_group_t *mg;
	vdev_t *vd;

	/*
	 * Must hold one of the spa_config locks.
	 */
	ASSERT(spa_config_held(mc->mc_spa, SCL_ALL, RW_READER) ||
	    spa_config_held(mc->mc_spa, SCL_ALL, RW_WRITER));

	if ((mg = mc->mc_rotor) == NULL)
		return (0);

	do {
		vd = mg->mg_vd;
		ASSERT(vd->vdev_mg != NULL);
		ASSERT3P(vd->vdev_top, ==, vd);
		ASSERT3P(mg->mg_class, ==, mc);
		ASSERT3P(vd->vdev_ops, !=, &vdev_hole_ops);
	} while ((mg = mg->mg_next) != mc->mc_rotor);

	return (0);
}

static void
metaslab_class_space_update(metaslab_class_t *mc, int64_t alloc_delta,
    int64_t defer_delta, int64_t space_delta, int64_t dspace_delta)
{
	atomic_add_64(&mc->mc_alloc, alloc_delta);
	atomic_add_64(&mc->mc_deferred, defer_delta);
	atomic_add_64(&mc->mc_space, space_delta);
	atomic_add_64(&mc->mc_dspace, dspace_delta);
}

uint64_t
metaslab_class_get_alloc(metaslab_class_t *mc)
{
	return (mc->mc_alloc);
}

uint64_t
metaslab_class_get_deferred(metaslab_class_t *mc)
{
	return (mc->mc_deferred);
}

uint64_t
metaslab_class_get_space(metaslab_class_t *mc)
{
	return (mc->mc_space);
}

uint64_t
metaslab_class_get_dspace(metaslab_class_t *mc)
{
	return (spa_deflate(mc->mc_spa) ? mc->mc_dspace : mc->mc_space);
}

void
metaslab_class_histogram_verify(metaslab_class_t *mc)
{
	spa_t *spa = mc->mc_spa;
	vdev_t *rvd = spa->spa_root_vdev;
	uint64_t *mc_hist;
	int i;

	if ((zfs_flags & ZFS_DEBUG_HISTOGRAM_VERIFY) == 0)
		return;

	mc_hist = kmem_zalloc(sizeof (uint64_t) * RANGE_TREE_HISTOGRAM_SIZE,
	    KM_SLEEP);

	for (int c = 0; c < rvd->vdev_children; c++) {
		vdev_t *tvd = rvd->vdev_child[c];
		metaslab_group_t *mg = tvd->vdev_mg;

		/*
		 * Skip any holes, uninitialized top-levels, or
		 * vdevs that are not in this metalab class.
		 */
		if (!vdev_is_concrete(tvd) || tvd->vdev_ms_shift == 0 ||
		    mg->mg_class != mc) {
			continue;
		}

		for (i = 0; i < RANGE_TREE_HISTOGRAM_SIZE; i++)
			mc_hist[i] += mg->mg_histogram[i];
	}

	for (i = 0; i < RANGE_TREE_HISTOGRAM_SIZE; i++)
		VERIFY3U(mc_hist[i], ==, mc->mc_histogram[i]);

	kmem_free(mc_hist, sizeof (uint64_t) * RANGE_TREE_HISTOGRAM_SIZE);
}

/*
 * Calculate the metaslab class's fragmentation metric. The metric
 * is weighted based on the space contribution of each metaslab group.
 * The return value will be a number between 0 and 100 (inclusive), or
 * ZFS_FRAG_INVALID if the metric has not been set. See comment above the
 * zfs_frag_table for more information about the metric.
 */
uint64_t
metaslab_class_fragmentation(metaslab_class_t *mc)
{
	vdev_t *rvd = mc->mc_spa->spa_root_vdev;
	uint64_t fragmentation = 0;

	spa_config_enter(mc->mc_spa, SCL_VDEV, FTAG, RW_READER);

	for (int c = 0; c < rvd->vdev_children; c++) {
		vdev_t *tvd = rvd->vdev_child[c];
		metaslab_group_t *mg = tvd->vdev_mg;

		/*
		 * Skip any holes, uninitialized top-levels,
		 * or vdevs that are not in this metalab class.
		 */
		if (!vdev_is_concrete(tvd) || tvd->vdev_ms_shift == 0 ||
		    mg->mg_class != mc) {
			continue;
		}

		/*
		 * If a metaslab group does not contain a fragmentation
		 * metric then just bail out.
		 */
		if (mg->mg_fragmentation == ZFS_FRAG_INVALID) {
			spa_config_exit(mc->mc_spa, SCL_VDEV, FTAG);
			return (ZFS_FRAG_INVALID);
		}

		/*
		 * Determine how much this metaslab_group is contributing
		 * to the overall pool fragmentation metric.
		 */
		fragmentation += mg->mg_fragmentation *
		    metaslab_group_get_space(mg);
	}
	fragmentation /= metaslab_class_get_space(mc);

	ASSERT3U(fragmentation, <=, 100);
	spa_config_exit(mc->mc_spa, SCL_VDEV, FTAG);
	return (fragmentation);
}

/*
 * Calculate the amount of expandable space that is available in
 * this metaslab class. If a device is expanded then its expandable
 * space will be the amount of allocatable space that is currently not
 * part of this metaslab class.
 */
uint64_t
metaslab_class_expandable_space(metaslab_class_t *mc)
{
	vdev_t *rvd = mc->mc_spa->spa_root_vdev;
	uint64_t space = 0;

	spa_config_enter(mc->mc_spa, SCL_VDEV, FTAG, RW_READER);
	for (int c = 0; c < rvd->vdev_children; c++) {
		vdev_t *tvd = rvd->vdev_child[c];
		metaslab_group_t *mg = tvd->vdev_mg;

		if (!vdev_is_concrete(tvd) || tvd->vdev_ms_shift == 0 ||
		    mg->mg_class != mc) {
			continue;
		}

		/*
		 * Calculate if we have enough space to add additional
		 * metaslabs. We report the expandable space in terms
		 * of the metaslab size since that's the unit of expansion.
		 */
		space += P2ALIGN(tvd->vdev_max_asize - tvd->vdev_asize,
		    1ULL << tvd->vdev_ms_shift);
	}
	spa_config_exit(mc->mc_spa, SCL_VDEV, FTAG);
	return (space);
}

static int
metaslab_compare(const void *x1, const void *x2)
{
	const metaslab_t *m1 = (const metaslab_t *)x1;
	const metaslab_t *m2 = (const metaslab_t *)x2;

	int sort1 = 0;
	int sort2 = 0;
	if (m1->ms_allocator != -1 && m1->ms_primary)
		sort1 = 1;
	else if (m1->ms_allocator != -1 && !m1->ms_primary)
		sort1 = 2;
	if (m2->ms_allocator != -1 && m2->ms_primary)
		sort2 = 1;
	else if (m2->ms_allocator != -1 && !m2->ms_primary)
		sort2 = 2;

	/*
	 * Sort inactive metaslabs first, then primaries, then secondaries. When
	 * selecting a metaslab to allocate from, an allocator first tries its
	 * primary, then secondary active metaslab. If it doesn't have active
	 * metaslabs, or can't allocate from them, it searches for an inactive
	 * metaslab to activate. If it can't find a suitable one, it will steal
	 * a primary or secondary metaslab from another allocator.
	 */
	if (sort1 < sort2)
		return (-1);
	if (sort1 > sort2)
		return (1);

	int cmp = AVL_CMP(m2->ms_weight, m1->ms_weight);
	if (likely(cmp))
		return (cmp);

	IMPLY(AVL_CMP(m1->ms_start, m2->ms_start) == 0, m1 == m2);

	return (AVL_CMP(m1->ms_start, m2->ms_start));
}

uint64_t
metaslab_allocated_space(metaslab_t *msp)
{
	return (msp->ms_allocated_space);
}

/*
 * Verify that the space accounting on disk matches the in-core range_trees.
 */
static void
metaslab_verify_space(metaslab_t *msp, uint64_t txg)
{
	spa_t *spa = msp->ms_group->mg_vd->vdev_spa;
	uint64_t allocating = 0;
	uint64_t sm_free_space, msp_free_space;

	ASSERT(MUTEX_HELD(&msp->ms_lock));
	ASSERT(!msp->ms_condensing);

	if ((zfs_flags & ZFS_DEBUG_METASLAB_VERIFY) == 0)
		return;

	/*
	 * We can only verify the metaslab space when we're called
	 * from syncing context with a loaded metaslab that has an
	 * allocated space map. Calling this in non-syncing context
	 * does not provide a consistent view of the metaslab since
	 * we're performing allocations in the future.
	 */
	if (txg != spa_syncing_txg(spa) || msp->ms_sm == NULL ||
	    !msp->ms_loaded)
		return;

	/*
	 * Even though the smp_alloc field can get negative (e.g.
	 * see vdev_checkpoint_sm), that should never be the case
	 * when it come's to a metaslab's space map.
	 */
	ASSERT3S(space_map_allocated(msp->ms_sm), >=, 0);

	sm_free_space = msp->ms_size - metaslab_allocated_space(msp);

	/*
	 * Account for future allocations since we would have
	 * already deducted that space from the ms_allocatable.
	 */
	for (int t = 0; t < TXG_CONCURRENT_STATES; t++) {
		allocating +=
		    range_tree_space(msp->ms_allocating[(txg + t) & TXG_MASK]);
	}

	ASSERT3U(msp->ms_deferspace, ==,
	    range_tree_space(msp->ms_defer[0]) +
	    range_tree_space(msp->ms_defer[1]));

	msp_free_space = range_tree_space(msp->ms_allocatable) + allocating +
	    msp->ms_deferspace + range_tree_space(msp->ms_freed);

	VERIFY3U(sm_free_space, ==, msp_free_space);
}

/*
 * ==========================================================================
 * Metaslab groups
 * ==========================================================================
 */
/*
 * Update the allocatable flag and the metaslab group's capacity.
 * The allocatable flag is set to true if the capacity is below
 * the zfs_mg_noalloc_threshold or has a fragmentation value that is
 * greater than zfs_mg_fragmentation_threshold. If a metaslab group
 * transitions from allocatable to non-allocatable or vice versa then the
 * metaslab group's class is updated to reflect the transition.
 */
static void
metaslab_group_alloc_update(metaslab_group_t *mg)
{
	vdev_t *vd = mg->mg_vd;
	metaslab_class_t *mc = mg->mg_class;
	vdev_stat_t *vs = &vd->vdev_stat;
	boolean_t was_allocatable;
	boolean_t was_initialized;

	ASSERT(vd == vd->vdev_top);
	ASSERT3U(spa_config_held(mc->mc_spa, SCL_ALLOC, RW_READER), ==,
	    SCL_ALLOC);

	mutex_enter(&mg->mg_lock);
	was_allocatable = mg->mg_allocatable;
	was_initialized = mg->mg_initialized;

	mg->mg_free_capacity = ((vs->vs_space - vs->vs_alloc) * 100) /
	    (vs->vs_space + 1);

	mutex_enter(&mc->mc_lock);

	/*
	 * If the metaslab group was just added then it won't
	 * have any space until we finish syncing out this txg.
	 * At that point we will consider it initialized and available
	 * for allocations.  We also don't consider non-activated
	 * metaslab groups (e.g. vdevs that are in the middle of being removed)
	 * to be initialized, because they can't be used for allocation.
	 */
	mg->mg_initialized = metaslab_group_initialized(mg);
	if (!was_initialized && mg->mg_initialized) {
		mc->mc_groups++;
	} else if (was_initialized && !mg->mg_initialized) {
		ASSERT3U(mc->mc_groups, >, 0);
		mc->mc_groups--;
	}
	if (mg->mg_initialized)
		mg->mg_no_free_space = B_FALSE;

	/*
	 * A metaslab group is considered allocatable if it has plenty
	 * of free space or is not heavily fragmented. We only take
	 * fragmentation into account if the metaslab group has a valid
	 * fragmentation metric (i.e. a value between 0 and 100).
	 */
	mg->mg_allocatable = (mg->mg_activation_count > 0 &&
	    mg->mg_free_capacity > zfs_mg_noalloc_threshold &&
	    (mg->mg_fragmentation == ZFS_FRAG_INVALID ||
	    mg->mg_fragmentation <= zfs_mg_fragmentation_threshold));

	/*
	 * The mc_alloc_groups maintains a count of the number of
	 * groups in this metaslab class that are still above the
	 * zfs_mg_noalloc_threshold. This is used by the allocating
	 * threads to determine if they should avoid allocations to
	 * a given group. The allocator will avoid allocations to a group
	 * if that group has reached or is below the zfs_mg_noalloc_threshold
	 * and there are still other groups that are above the threshold.
	 * When a group transitions from allocatable to non-allocatable or
	 * vice versa we update the metaslab class to reflect that change.
	 * When the mc_alloc_groups value drops to 0 that means that all
	 * groups have reached the zfs_mg_noalloc_threshold making all groups
	 * eligible for allocations. This effectively means that all devices
	 * are balanced again.
	 */
	if (was_allocatable && !mg->mg_allocatable)
		mc->mc_alloc_groups--;
	else if (!was_allocatable && mg->mg_allocatable)
		mc->mc_alloc_groups++;
	mutex_exit(&mc->mc_lock);

	mutex_exit(&mg->mg_lock);
}

metaslab_group_t *
metaslab_group_create(metaslab_class_t *mc, vdev_t *vd, int allocators)
{
	metaslab_group_t *mg;

	mg = kmem_zalloc(sizeof (metaslab_group_t), KM_SLEEP);
	mutex_init(&mg->mg_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&mg->mg_ms_disabled_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&mg->mg_ms_disabled_cv, NULL, CV_DEFAULT, NULL);
	mg->mg_primaries = kmem_zalloc(allocators * sizeof (metaslab_t *),
	    KM_SLEEP);
	mg->mg_secondaries = kmem_zalloc(allocators * sizeof (metaslab_t *),
	    KM_SLEEP);
	avl_create(&mg->mg_metaslab_tree, metaslab_compare,
	    sizeof (metaslab_t), offsetof(struct metaslab, ms_group_node));
	mg->mg_vd = vd;
	mg->mg_class = mc;
	mg->mg_activation_count = 0;
	mg->mg_initialized = B_FALSE;
	mg->mg_no_free_space = B_TRUE;
	mg->mg_allocators = allocators;

	mg->mg_alloc_queue_depth = kmem_zalloc(allocators *
	    sizeof (zfs_refcount_t), KM_SLEEP);
	mg->mg_cur_max_alloc_queue_depth = kmem_zalloc(allocators *
	    sizeof (uint64_t), KM_SLEEP);
	for (int i = 0; i < allocators; i++) {
		zfs_refcount_create_tracked(&mg->mg_alloc_queue_depth[i]);
		mg->mg_cur_max_alloc_queue_depth[i] = 0;
	}

	mg->mg_taskq = taskq_create("metaslab_group_taskq", metaslab_load_pct,
	    maxclsyspri, 10, INT_MAX, TASKQ_THREADS_CPU_PCT | TASKQ_DYNAMIC);

	return (mg);
}

void
metaslab_group_destroy(metaslab_group_t *mg)
{
	ASSERT(mg->mg_prev == NULL);
	ASSERT(mg->mg_next == NULL);
	/*
	 * We may have gone below zero with the activation count
	 * either because we never activated in the first place or
	 * because we're done, and possibly removing the vdev.
	 */
	ASSERT(mg->mg_activation_count <= 0);

	taskq_destroy(mg->mg_taskq);
	avl_destroy(&mg->mg_metaslab_tree);
	kmem_free(mg->mg_primaries, mg->mg_allocators * sizeof (metaslab_t *));
	kmem_free(mg->mg_secondaries, mg->mg_allocators *
	    sizeof (metaslab_t *));
	mutex_destroy(&mg->mg_lock);
	mutex_destroy(&mg->mg_ms_disabled_lock);
	cv_destroy(&mg->mg_ms_disabled_cv);

	for (int i = 0; i < mg->mg_allocators; i++) {
		zfs_refcount_destroy(&mg->mg_alloc_queue_depth[i]);
		mg->mg_cur_max_alloc_queue_depth[i] = 0;
	}
	kmem_free(mg->mg_alloc_queue_depth, mg->mg_allocators *
	    sizeof (zfs_refcount_t));
	kmem_free(mg->mg_cur_max_alloc_queue_depth, mg->mg_allocators *
	    sizeof (uint64_t));

	kmem_free(mg, sizeof (metaslab_group_t));
}

void
metaslab_group_activate(metaslab_group_t *mg)
{
	metaslab_class_t *mc = mg->mg_class;
	metaslab_group_t *mgprev, *mgnext;

	ASSERT3U(spa_config_held(mc->mc_spa, SCL_ALLOC, RW_WRITER), !=, 0);

	ASSERT(mc->mc_rotor != mg);
	ASSERT(mg->mg_prev == NULL);
	ASSERT(mg->mg_next == NULL);
	ASSERT(mg->mg_activation_count <= 0);

	if (++mg->mg_activation_count <= 0)
		return;

	mg->mg_aliquot = metaslab_aliquot * MAX(1, mg->mg_vd->vdev_children);
	metaslab_group_alloc_update(mg);

	if ((mgprev = mc->mc_rotor) == NULL) {
		mg->mg_prev = mg;
		mg->mg_next = mg;
	} else {
		mgnext = mgprev->mg_next;
		mg->mg_prev = mgprev;
		mg->mg_next = mgnext;
		mgprev->mg_next = mg;
		mgnext->mg_prev = mg;
	}
	mc->mc_rotor = mg;
}

/*
 * Passivate a metaslab group and remove it from the allocation rotor.
 * Callers must hold both the SCL_ALLOC and SCL_ZIO lock prior to passivating
 * a metaslab group. This function will momentarily drop spa_config_locks
 * that are lower than the SCL_ALLOC lock (see comment below).
 */
void
metaslab_group_passivate(metaslab_group_t *mg)
{
	metaslab_class_t *mc = mg->mg_class;
	spa_t *spa = mc->mc_spa;
	metaslab_group_t *mgprev, *mgnext;
	int locks = spa_config_held(spa, SCL_ALL, RW_WRITER);

	ASSERT3U(spa_config_held(spa, SCL_ALLOC | SCL_ZIO, RW_WRITER), ==,
	    (SCL_ALLOC | SCL_ZIO));

	if (--mg->mg_activation_count != 0) {
		ASSERT(mc->mc_rotor != mg);
		ASSERT(mg->mg_prev == NULL);
		ASSERT(mg->mg_next == NULL);
		ASSERT(mg->mg_activation_count < 0);
		return;
	}

	/*
	 * The spa_config_lock is an array of rwlocks, ordered as
	 * follows (from highest to lowest):
	 *	SCL_CONFIG > SCL_STATE > SCL_L2ARC > SCL_ALLOC >
	 *	SCL_ZIO > SCL_FREE > SCL_VDEV
	 * (For more information about the spa_config_lock see spa_misc.c)
	 * The higher the lock, the broader its coverage. When we passivate
	 * a metaslab group, we must hold both the SCL_ALLOC and the SCL_ZIO
	 * config locks. However, the metaslab group's taskq might be trying
	 * to preload metaslabs so we must drop the SCL_ZIO lock and any
	 * lower locks to allow the I/O to complete. At a minimum,
	 * we continue to hold the SCL_ALLOC lock, which prevents any future
	 * allocations from taking place and any changes to the vdev tree.
	 */
	spa_config_exit(spa, locks & ~(SCL_ZIO - 1), spa);
	taskq_wait_outstanding(mg->mg_taskq, 0);
	spa_config_enter(spa, locks & ~(SCL_ZIO - 1), spa, RW_WRITER);
	metaslab_group_alloc_update(mg);
	for (int i = 0; i < mg->mg_allocators; i++) {
		metaslab_t *msp = mg->mg_primaries[i];
		if (msp != NULL) {
			mutex_enter(&msp->ms_lock);
			metaslab_passivate(msp,
			    metaslab_weight_from_range_tree(msp));
			mutex_exit(&msp->ms_lock);
		}
		msp = mg->mg_secondaries[i];
		if (msp != NULL) {
			mutex_enter(&msp->ms_lock);
			metaslab_passivate(msp,
			    metaslab_weight_from_range_tree(msp));
			mutex_exit(&msp->ms_lock);
		}
	}

	mgprev = mg->mg_prev;
	mgnext = mg->mg_next;

	if (mg == mgnext) {
		mc->mc_rotor = NULL;
	} else {
		mc->mc_rotor = mgnext;
		mgprev->mg_next = mgnext;
		mgnext->mg_prev = mgprev;
	}

	mg->mg_prev = NULL;
	mg->mg_next = NULL;
}

boolean_t
metaslab_group_initialized(metaslab_group_t *mg)
{
	vdev_t *vd = mg->mg_vd;
	vdev_stat_t *vs = &vd->vdev_stat;

	return (vs->vs_space != 0 && mg->mg_activation_count > 0);
}

uint64_t
metaslab_group_get_space(metaslab_group_t *mg)
{
	return ((1ULL << mg->mg_vd->vdev_ms_shift) * mg->mg_vd->vdev_ms_count);
}

void
metaslab_group_histogram_verify(metaslab_group_t *mg)
{
	uint64_t *mg_hist;
	vdev_t *vd = mg->mg_vd;
	uint64_t ashift = vd->vdev_ashift;
	int i;

	if ((zfs_flags & ZFS_DEBUG_HISTOGRAM_VERIFY) == 0)
		return;

	mg_hist = kmem_zalloc(sizeof (uint64_t) * RANGE_TREE_HISTOGRAM_SIZE,
	    KM_SLEEP);

	ASSERT3U(RANGE_TREE_HISTOGRAM_SIZE, >=,
	    SPACE_MAP_HISTOGRAM_SIZE + ashift);

	for (int m = 0; m < vd->vdev_ms_count; m++) {
		metaslab_t *msp = vd->vdev_ms[m];
		ASSERT(msp != NULL);

		/* skip if not active or not a member */
		if (msp->ms_sm == NULL || msp->ms_group != mg)
			continue;

		for (i = 0; i < SPACE_MAP_HISTOGRAM_SIZE; i++)
			mg_hist[i + ashift] +=
			    msp->ms_sm->sm_phys->smp_histogram[i];
	}

	for (i = 0; i < RANGE_TREE_HISTOGRAM_SIZE; i ++)
		VERIFY3U(mg_hist[i], ==, mg->mg_histogram[i]);

	kmem_free(mg_hist, sizeof (uint64_t) * RANGE_TREE_HISTOGRAM_SIZE);
}

static void
metaslab_group_histogram_add(metaslab_group_t *mg, metaslab_t *msp)
{
	metaslab_class_t *mc = mg->mg_class;
	uint64_t ashift = mg->mg_vd->vdev_ashift;

	ASSERT(MUTEX_HELD(&msp->ms_lock));
	if (msp->ms_sm == NULL)
		return;

	mutex_enter(&mg->mg_lock);
	for (int i = 0; i < SPACE_MAP_HISTOGRAM_SIZE; i++) {
		mg->mg_histogram[i + ashift] +=
		    msp->ms_sm->sm_phys->smp_histogram[i];
		mc->mc_histogram[i + ashift] +=
		    msp->ms_sm->sm_phys->smp_histogram[i];
	}
	mutex_exit(&mg->mg_lock);
}

void
metaslab_group_histogram_remove(metaslab_group_t *mg, metaslab_t *msp)
{
	metaslab_class_t *mc = mg->mg_class;
	uint64_t ashift = mg->mg_vd->vdev_ashift;

	ASSERT(MUTEX_HELD(&msp->ms_lock));
	if (msp->ms_sm == NULL)
		return;

	mutex_enter(&mg->mg_lock);
	for (int i = 0; i < SPACE_MAP_HISTOGRAM_SIZE; i++) {
		ASSERT3U(mg->mg_histogram[i + ashift], >=,
		    msp->ms_sm->sm_phys->smp_histogram[i]);
		ASSERT3U(mc->mc_histogram[i + ashift], >=,
		    msp->ms_sm->sm_phys->smp_histogram[i]);

		mg->mg_histogram[i + ashift] -=
		    msp->ms_sm->sm_phys->smp_histogram[i];
		mc->mc_histogram[i + ashift] -=
		    msp->ms_sm->sm_phys->smp_histogram[i];
	}
	mutex_exit(&mg->mg_lock);
}

static void
metaslab_group_add(metaslab_group_t *mg, metaslab_t *msp)
{
	ASSERT(msp->ms_group == NULL);
	mutex_enter(&mg->mg_lock);
	msp->ms_group = mg;
	msp->ms_weight = 0;
	avl_add(&mg->mg_metaslab_tree, msp);
	mutex_exit(&mg->mg_lock);

	mutex_enter(&msp->ms_lock);
	metaslab_group_histogram_add(mg, msp);
	mutex_exit(&msp->ms_lock);
}

static void
metaslab_group_remove(metaslab_group_t *mg, metaslab_t *msp)
{
	mutex_enter(&msp->ms_lock);
	metaslab_group_histogram_remove(mg, msp);
	mutex_exit(&msp->ms_lock);

	mutex_enter(&mg->mg_lock);
	ASSERT(msp->ms_group == mg);
	avl_remove(&mg->mg_metaslab_tree, msp);
	msp->ms_group = NULL;
	mutex_exit(&mg->mg_lock);
}

static void
metaslab_group_sort_impl(metaslab_group_t *mg, metaslab_t *msp, uint64_t weight)
{
	ASSERT(MUTEX_HELD(&msp->ms_lock));
	ASSERT(MUTEX_HELD(&mg->mg_lock));
	ASSERT(msp->ms_group == mg);

	avl_remove(&mg->mg_metaslab_tree, msp);
	msp->ms_weight = weight;
	avl_add(&mg->mg_metaslab_tree, msp);

}

static void
metaslab_group_sort(metaslab_group_t *mg, metaslab_t *msp, uint64_t weight)
{
	/*
	 * Although in principle the weight can be any value, in
	 * practice we do not use values in the range [1, 511].
	 */
	ASSERT(weight >= SPA_MINBLOCKSIZE || weight == 0);
	ASSERT(MUTEX_HELD(&msp->ms_lock));

	mutex_enter(&mg->mg_lock);
	metaslab_group_sort_impl(mg, msp, weight);
	mutex_exit(&mg->mg_lock);
}

/*
 * Calculate the fragmentation for a given metaslab group. We can use
 * a simple average here since all metaslabs within the group must have
 * the same size. The return value will be a value between 0 and 100
 * (inclusive), or ZFS_FRAG_INVALID if less than half of the metaslab in this
 * group have a fragmentation metric.
 */
uint64_t
metaslab_group_fragmentation(metaslab_group_t *mg)
{
	vdev_t *vd = mg->mg_vd;
	uint64_t fragmentation = 0;
	uint64_t valid_ms = 0;

	for (int m = 0; m < vd->vdev_ms_count; m++) {
		metaslab_t *msp = vd->vdev_ms[m];

		if (msp->ms_fragmentation == ZFS_FRAG_INVALID)
			continue;
		if (msp->ms_group != mg)
			continue;

		valid_ms++;
		fragmentation += msp->ms_fragmentation;
	}

	if (valid_ms <= mg->mg_vd->vdev_ms_count / 2)
		return (ZFS_FRAG_INVALID);

	fragmentation /= valid_ms;
	ASSERT3U(fragmentation, <=, 100);
	return (fragmentation);
}

/*
 * Determine if a given metaslab group should skip allocations. A metaslab
 * group should avoid allocations if its free capacity is less than the
 * zfs_mg_noalloc_threshold or its fragmentation metric is greater than
 * zfs_mg_fragmentation_threshold and there is at least one metaslab group
 * that can still handle allocations. If the allocation throttle is enabled
 * then we skip allocations to devices that have reached their maximum
 * allocation queue depth unless the selected metaslab group is the only
 * eligible group remaining.
 */
static boolean_t
metaslab_group_allocatable(metaslab_group_t *mg, metaslab_group_t *rotor,
    uint64_t psize, int allocator, int d)
{
	spa_t *spa = mg->mg_vd->vdev_spa;
	metaslab_class_t *mc = mg->mg_class;

	/*
	 * We can only consider skipping this metaslab group if it's
	 * in the normal metaslab class and there are other metaslab
	 * groups to select from. Otherwise, we always consider it eligible
	 * for allocations.
	 */
	if ((mc != spa_normal_class(spa) &&
	    mc != spa_special_class(spa) &&
	    mc != spa_dedup_class(spa)) ||
	    mc->mc_groups <= 1)
		return (B_TRUE);

	/*
	 * If the metaslab group's mg_allocatable flag is set (see comments
	 * in metaslab_group_alloc_update() for more information) and
	 * the allocation throttle is disabled then allow allocations to this
	 * device. However, if the allocation throttle is enabled then
	 * check if we have reached our allocation limit (mg_alloc_queue_depth)
	 * to determine if we should allow allocations to this metaslab group.
	 * If all metaslab groups are no longer considered allocatable
	 * (mc_alloc_groups == 0) or we're trying to allocate the smallest
	 * gang block size then we allow allocations on this metaslab group
	 * regardless of the mg_allocatable or throttle settings.
	 */
	if (mg->mg_allocatable) {
		metaslab_group_t *mgp;
		int64_t qdepth;
		uint64_t qmax = mg->mg_cur_max_alloc_queue_depth[allocator];

		if (!mc->mc_alloc_throttle_enabled)
			return (B_TRUE);

		/*
		 * If this metaslab group does not have any free space, then
		 * there is no point in looking further.
		 */
		if (mg->mg_no_free_space)
			return (B_FALSE);

		/*
		 * Relax allocation throttling for ditto blocks.  Due to
		 * random imbalances in allocation it tends to push copies
		 * to one vdev, that looks a bit better at the moment.
		 */
		qmax = qmax * (4 + d) / 4;

		qdepth = zfs_refcount_count(
		    &mg->mg_alloc_queue_depth[allocator]);

		/*
		 * If this metaslab group is below its qmax or it's
		 * the only allocatable metasable group, then attempt
		 * to allocate from it.
		 */
		if (qdepth < qmax || mc->mc_alloc_groups == 1)
			return (B_TRUE);
		ASSERT3U(mc->mc_alloc_groups, >, 1);

		/*
		 * Since this metaslab group is at or over its qmax, we
		 * need to determine if there are metaslab groups after this
		 * one that might be able to handle this allocation. This is
		 * racy since we can't hold the locks for all metaslab
		 * groups at the same time when we make this check.
		 */
		for (mgp = mg->mg_next; mgp != rotor; mgp = mgp->mg_next) {
			qmax = mgp->mg_cur_max_alloc_queue_depth[allocator];
			qmax = qmax * (4 + d) / 4;
			qdepth = zfs_refcount_count(
			    &mgp->mg_alloc_queue_depth[allocator]);

			/*
			 * If there is another metaslab group that
			 * might be able to handle the allocation, then
			 * we return false so that we skip this group.
			 */
			if (qdepth < qmax && !mgp->mg_no_free_space)
				return (B_FALSE);
		}

		/*
		 * We didn't find another group to handle the allocation
		 * so we can't skip this metaslab group even though
		 * we are at or over our qmax.
		 */
		return (B_TRUE);

	} else if (mc->mc_alloc_groups == 0 || psize == SPA_MINBLOCKSIZE) {
		return (B_TRUE);
	}
	return (B_FALSE);
}

/*
 * ==========================================================================
 * Range tree callbacks
 * ==========================================================================
 */

/*
 * Comparison function for the private size-ordered tree. Tree is sorted
 * by size, larger sizes at the end of the tree.
 */
static int
metaslab_rangesize_compare(const void *x1, const void *x2)
{
	const range_seg_t *r1 = x1;
	const range_seg_t *r2 = x2;
	uint64_t rs_size1 = r1->rs_end - r1->rs_start;
	uint64_t rs_size2 = r2->rs_end - r2->rs_start;

	int cmp = AVL_CMP(rs_size1, rs_size2);
	if (likely(cmp))
		return (cmp);

	return (AVL_CMP(r1->rs_start, r2->rs_start));
}

/*
 * ==========================================================================
 * Common allocator routines
 * ==========================================================================
 */

/*
 * Return the maximum contiguous segment within the metaslab.
 */
uint64_t
metaslab_block_maxsize(metaslab_t *msp)
{
	avl_tree_t *t = &msp->ms_allocatable_by_size;
	range_seg_t *rs;

	if (t == NULL || (rs = avl_last(t)) == NULL)
		return (0ULL);

	return (rs->rs_end - rs->rs_start);
}

static range_seg_t *
metaslab_block_find(avl_tree_t *t, uint64_t start, uint64_t size)
{
	range_seg_t *rs, rsearch;
	avl_index_t where;

	rsearch.rs_start = start;
	rsearch.rs_end = start + size;

	rs = avl_find(t, &rsearch, &where);
	if (rs == NULL) {
		rs = avl_nearest(t, where, AVL_AFTER);
	}

	return (rs);
}

#if defined(WITH_DF_BLOCK_ALLOCATOR) || \
    defined(WITH_CF_BLOCK_ALLOCATOR)
/*
 * This is a helper function that can be used by the allocator to find
 * a suitable block to allocate. This will search the specified AVL
 * tree looking for a block that matches the specified criteria.
 */
static uint64_t
metaslab_block_picker(avl_tree_t *t, uint64_t *cursor, uint64_t size,
    uint64_t max_search)
{
	range_seg_t *rs = metaslab_block_find(t, *cursor, size);
	uint64_t first_found;

	if (rs != NULL)
		first_found = rs->rs_start;

	while (rs != NULL && rs->rs_start - first_found <= max_search) {
		uint64_t offset = rs->rs_start;
		if (offset + size <= rs->rs_end) {
			*cursor = offset + size;
			return (offset);
		}
		rs = AVL_NEXT(t, rs);
	}

	*cursor = 0;
	return (-1ULL);
}
#endif /* WITH_DF/CF_BLOCK_ALLOCATOR */

#if defined(WITH_DF_BLOCK_ALLOCATOR)
/*
 * ==========================================================================
 * Dynamic Fit (df) block allocator
 *
 * Search for a free chunk of at least this size, starting from the last
 * offset (for this alignment of block) looking for up to
 * metaslab_df_max_search bytes (16MB).  If a large enough free chunk is not
 * found within 16MB, then return a free chunk of exactly the requested size (or
 * larger).
 *
 * If it seems like searching from the last offset will be unproductive, skip
 * that and just return a free chunk of exactly the requested size (or larger).
 * This is based on metaslab_df_alloc_threshold and metaslab_df_free_pct.  This
 * mechanism is probably not very useful and may be removed in the future.
 *
 * The behavior when not searching can be changed to return the largest free
 * chunk, instead of a free chunk of exactly the requested size, by setting
 * metaslab_df_use_largest_segment.
 * ==========================================================================
 */
static uint64_t
metaslab_df_alloc(metaslab_t *msp, uint64_t size)
{
	/*
	 * Find the largest power of 2 block size that evenly divides the
	 * requested size. This is used to try to allocate blocks with similar
	 * alignment from the same area of the metaslab (i.e. same cursor
	 * bucket) but it does not guarantee that other allocations sizes
	 * may exist in the same region.
	 */
	uint64_t align = size & -size;
	uint64_t *cursor = &msp->ms_lbas[highbit64(align) - 1];
	range_tree_t *rt = msp->ms_allocatable;
	int free_pct = range_tree_space(rt) * 100 / msp->ms_size;
	uint64_t offset;

	ASSERT(MUTEX_HELD(&msp->ms_lock));
	ASSERT3U(avl_numnodes(&rt->rt_root), ==,
	    avl_numnodes(&msp->ms_allocatable_by_size));

	/*
	 * If we're running low on space, find a segment based on size,
	 * rather than iterating based on offset.
	 */
	if (metaslab_block_maxsize(msp) < metaslab_df_alloc_threshold ||
	    free_pct < metaslab_df_free_pct) {
		offset = -1;
	} else {
		offset = metaslab_block_picker(&rt->rt_root,
		    cursor, size, metaslab_df_max_search);
	}

	if (offset == -1) {
		range_seg_t *rs;
		if (metaslab_df_use_largest_segment) {
			/* use largest free segment */
			rs = avl_last(&msp->ms_allocatable_by_size);
		} else {
			/* use segment of this size, or next largest */
			rs = metaslab_block_find(&msp->ms_allocatable_by_size,
			    0, size);
		}
		if (rs != NULL && rs->rs_start + size <= rs->rs_end) {
			offset = rs->rs_start;
			*cursor = offset + size;
		}
	}

	return (offset);
}

static metaslab_ops_t metaslab_df_ops = {
	metaslab_df_alloc
};

metaslab_ops_t *zfs_metaslab_ops = &metaslab_df_ops;
#endif /* WITH_DF_BLOCK_ALLOCATOR */

#if defined(WITH_CF_BLOCK_ALLOCATOR)
/*
 * ==========================================================================
 * Cursor fit block allocator -
 * Select the largest region in the metaslab, set the cursor to the beginning
 * of the range and the cursor_end to the end of the range. As allocations
 * are made advance the cursor. Continue allocating from the cursor until
 * the range is exhausted and then find a new range.
 * ==========================================================================
 */
static uint64_t
metaslab_cf_alloc(metaslab_t *msp, uint64_t size)
{
	range_tree_t *rt = msp->ms_allocatable;
	avl_tree_t *t = &msp->ms_allocatable_by_size;
	uint64_t *cursor = &msp->ms_lbas[0];
	uint64_t *cursor_end = &msp->ms_lbas[1];
	uint64_t offset = 0;

	ASSERT(MUTEX_HELD(&msp->ms_lock));
	ASSERT3U(avl_numnodes(t), ==, avl_numnodes(&rt->rt_root));

	ASSERT3U(*cursor_end, >=, *cursor);

	if ((*cursor + size) > *cursor_end) {
		range_seg_t *rs;

		rs = avl_last(&msp->ms_allocatable_by_size);
		if (rs == NULL || (rs->rs_end - rs->rs_start) < size)
			return (-1ULL);

		*cursor = rs->rs_start;
		*cursor_end = rs->rs_end;
	}

	offset = *cursor;
	*cursor += size;

	return (offset);
}

static metaslab_ops_t metaslab_cf_ops = {
	metaslab_cf_alloc
};

metaslab_ops_t *zfs_metaslab_ops = &metaslab_cf_ops;
#endif /* WITH_CF_BLOCK_ALLOCATOR */

#if defined(WITH_NDF_BLOCK_ALLOCATOR)
/*
 * ==========================================================================
 * New dynamic fit allocator -
 * Select a region that is large enough to allocate 2^metaslab_ndf_clump_shift
 * contiguous blocks. If no region is found then just use the largest segment
 * that remains.
 * ==========================================================================
 */

/*
 * Determines desired number of contiguous blocks (2^metaslab_ndf_clump_shift)
 * to request from the allocator.
 */
uint64_t metaslab_ndf_clump_shift = 4;

static uint64_t
metaslab_ndf_alloc(metaslab_t *msp, uint64_t size)
{
	avl_tree_t *t = &msp->ms_allocatable->rt_root;
	avl_index_t where;
	range_seg_t *rs, rsearch;
	uint64_t hbit = highbit64(size);
	uint64_t *cursor = &msp->ms_lbas[hbit - 1];
	uint64_t max_size = metaslab_block_maxsize(msp);

	ASSERT(MUTEX_HELD(&msp->ms_lock));
	ASSERT3U(avl_numnodes(t), ==,
	    avl_numnodes(&msp->ms_allocatable_by_size));

	if (max_size < size)
		return (-1ULL);

	rsearch.rs_start = *cursor;
	rsearch.rs_end = *cursor + size;

	rs = avl_find(t, &rsearch, &where);
	if (rs == NULL || (rs->rs_end - rs->rs_start) < size) {
		t = &msp->ms_allocatable_by_size;

		rsearch.rs_start = 0;
		rsearch.rs_end = MIN(max_size,
		    1ULL << (hbit + metaslab_ndf_clump_shift));
		rs = avl_find(t, &rsearch, &where);
		if (rs == NULL)
			rs = avl_nearest(t, where, AVL_AFTER);
		ASSERT(rs != NULL);
	}

	if ((rs->rs_end - rs->rs_start) >= size) {
		*cursor = rs->rs_start + size;
		return (rs->rs_start);
	}
	return (-1ULL);
}

static metaslab_ops_t metaslab_ndf_ops = {
	metaslab_ndf_alloc
};

metaslab_ops_t *zfs_metaslab_ops = &metaslab_ndf_ops;
#endif /* WITH_NDF_BLOCK_ALLOCATOR */


/*
 * ==========================================================================
 * Metaslabs
 * ==========================================================================
 */

static void
metaslab_aux_histograms_clear(metaslab_t *msp)
{
	/*
	 * Auxiliary histograms are only cleared when resetting them,
	 * which can only happen while the metaslab is loaded.
	 */
	ASSERT(msp->ms_loaded);

	bzero(msp->ms_synchist, sizeof (msp->ms_synchist));
	for (int t = 0; t < TXG_DEFER_SIZE; t++)
		bzero(msp->ms_deferhist[t], sizeof (msp->ms_deferhist[t]));
}

static void
metaslab_aux_histogram_add(uint64_t *histogram, uint64_t shift,
    range_tree_t *rt)
{
	/*
	 * This is modeled after space_map_histogram_add(), so refer to that
	 * function for implementation details. We want this to work like
	 * the space map histogram, and not the range tree histogram, as we
	 * are essentially constructing a delta that will be later subtracted
	 * from the space map histogram.
	 */
	int idx = 0;
	for (int i = shift; i < RANGE_TREE_HISTOGRAM_SIZE; i++) {
		ASSERT3U(i, >=, idx + shift);
		histogram[idx] += rt->rt_histogram[i] << (i - idx - shift);

		if (idx < SPACE_MAP_HISTOGRAM_SIZE - 1) {
			ASSERT3U(idx + shift, ==, i);
			idx++;
			ASSERT3U(idx, <, SPACE_MAP_HISTOGRAM_SIZE);
		}
	}
}

/*
 * Called at every sync pass that the metaslab gets synced.
 *
 * The reason is that we want our auxiliary histograms to be updated
 * wherever the metaslab's space map histogram is updated. This way
 * we stay consistent on which parts of the metaslab space map's
 * histogram are currently not available for allocations (e.g because
 * they are in the defer, freed, and freeing trees).
 */
static void
metaslab_aux_histograms_update(metaslab_t *msp)
{
	space_map_t *sm = msp->ms_sm;
	ASSERT(sm != NULL);

	/*
	 * This is similar to the metaslab's space map histogram updates
	 * that take place in metaslab_sync(). The only difference is that
	 * we only care about segments that haven't made it into the
	 * ms_allocatable tree yet.
	 */
	if (msp->ms_loaded) {
		metaslab_aux_histograms_clear(msp);

		metaslab_aux_histogram_add(msp->ms_synchist,
		    sm->sm_shift, msp->ms_freed);

		for (int t = 0; t < TXG_DEFER_SIZE; t++) {
			metaslab_aux_histogram_add(msp->ms_deferhist[t],
			    sm->sm_shift, msp->ms_defer[t]);
		}
	}

	metaslab_aux_histogram_add(msp->ms_synchist,
	    sm->sm_shift, msp->ms_freeing);
}

/*
 * Called every time we are done syncing (writing to) the metaslab,
 * i.e. at the end of each sync pass.
 * [see the comment in metaslab_impl.h for ms_synchist, ms_deferhist]
 */
static void
metaslab_aux_histograms_update_done(metaslab_t *msp, boolean_t defer_allowed)
{
	spa_t *spa = msp->ms_group->mg_vd->vdev_spa;
	space_map_t *sm = msp->ms_sm;

	if (sm == NULL) {
		/*
		 * We came here from metaslab_init() when creating/opening a
		 * pool, looking at a metaslab that hasn't had any allocations
		 * yet.
		 */
		return;
	}

	/*
	 * This is similar to the actions that we take for the ms_freed
	 * and ms_defer trees in metaslab_sync_done().
	 */
	uint64_t hist_index = spa_syncing_txg(spa) % TXG_DEFER_SIZE;
	if (defer_allowed) {
		bcopy(msp->ms_synchist, msp->ms_deferhist[hist_index],
		    sizeof (msp->ms_synchist));
	} else {
		bzero(msp->ms_deferhist[hist_index],
		    sizeof (msp->ms_deferhist[hist_index]));
	}
	bzero(msp->ms_synchist, sizeof (msp->ms_synchist));
}

/*
 * Ensure that the metaslab's weight and fragmentation are consistent
 * with the contents of the histogram (either the range tree's histogram
 * or the space map's depending whether the metaslab is loaded).
 */
static void
metaslab_verify_weight_and_frag(metaslab_t *msp)
{
	ASSERT(MUTEX_HELD(&msp->ms_lock));

	if ((zfs_flags & ZFS_DEBUG_METASLAB_VERIFY) == 0)
		return;

	/* see comment in metaslab_verify_unflushed_changes() */
	if (msp->ms_group == NULL)
		return;

	/*
	 * Devices being removed always return a weight of 0 and leave
	 * fragmentation and ms_max_size as is - there is nothing for
	 * us to verify here.
	 */
	vdev_t *vd = msp->ms_group->mg_vd;
	if (vd->vdev_removing)
		return;

	/*
	 * If the metaslab is dirty it probably means that we've done
	 * some allocations or frees that have changed our histograms
	 * and thus the weight.
	 */
	for (int t = 0; t < TXG_SIZE; t++) {
		if (txg_list_member(&vd->vdev_ms_list, msp, t))
			return;
	}

	/*
	 * This verification checks that our in-memory state is consistent
	 * with what's on disk. If the pool is read-only then there aren't
	 * any changes and we just have the initially-loaded state.
	 */
	if (!spa_writeable(msp->ms_group->mg_vd->vdev_spa))
		return;

	/* some extra verification for in-core tree if you can */
	if (msp->ms_loaded) {
		range_tree_stat_verify(msp->ms_allocatable);
		VERIFY(space_map_histogram_verify(msp->ms_sm,
		    msp->ms_allocatable));
	}

	uint64_t weight = msp->ms_weight;
	uint64_t was_active = msp->ms_weight & METASLAB_ACTIVE_MASK;
	boolean_t space_based = WEIGHT_IS_SPACEBASED(msp->ms_weight);
	uint64_t frag = msp->ms_fragmentation;
	uint64_t max_segsize = msp->ms_max_size;

	msp->ms_weight = 0;
	msp->ms_fragmentation = 0;
	msp->ms_max_size = 0;

	/*
	 * This function is used for verification purposes. Regardless of
	 * whether metaslab_weight() thinks this metaslab should be active or
	 * not, we want to ensure that the actual weight (and therefore the
	 * value of ms_weight) would be the same if it was to be recalculated
	 * at this point.
	 */
	msp->ms_weight = metaslab_weight(msp) | was_active;

	VERIFY3U(max_segsize, ==, msp->ms_max_size);

	/*
	 * If the weight type changed then there is no point in doing
	 * verification. Revert fields to their original values.
	 */
	if ((space_based && !WEIGHT_IS_SPACEBASED(msp->ms_weight)) ||
	    (!space_based && WEIGHT_IS_SPACEBASED(msp->ms_weight))) {
		msp->ms_fragmentation = frag;
		msp->ms_weight = weight;
		return;
	}

	VERIFY3U(msp->ms_fragmentation, ==, frag);
	VERIFY3U(msp->ms_weight, ==, weight);
}

/*
 * Wait for any in-progress metaslab loads to complete.
 */
static void
metaslab_load_wait(metaslab_t *msp)
{
	ASSERT(MUTEX_HELD(&msp->ms_lock));

	while (msp->ms_loading) {
		ASSERT(!msp->ms_loaded);
		cv_wait(&msp->ms_load_cv, &msp->ms_lock);
	}
}

static int
metaslab_load_impl(metaslab_t *msp)
{
	int error = 0;

	ASSERT(MUTEX_HELD(&msp->ms_lock));
	ASSERT(msp->ms_loading);
	ASSERT(!msp->ms_condensing);

	/*
	 * We temporarily drop the lock to unblock other operations while we
	 * are reading the space map. Therefore, metaslab_sync() and
	 * metaslab_sync_done() can run at the same time as we do.
	 *
	 * metaslab_sync() can append to the space map while we are loading.
	 * Therefore we load only entries that existed when we started the
	 * load. Additionally, metaslab_sync_done() has to wait for the load
	 * to complete because there are potential races like metaslab_load()
	 * loading parts of the space map that are currently being appended
	 * by metaslab_sync(). If we didn't, the ms_allocatable would have
	 * entries that metaslab_sync_done() would try to re-add later.
	 *
	 * That's why before dropping the lock we remember the synced length
	 * of the metaslab and read up to that point of the space map,
	 * ignoring entries appended by metaslab_sync() that happen after we
	 * drop the lock.
	 */
	uint64_t length = msp->ms_synced_length;
	mutex_exit(&msp->ms_lock);

	if (msp->ms_sm != NULL) {
		error = space_map_load_length(msp->ms_sm, msp->ms_allocatable,
		    SM_FREE, length);
	} else {
		/*
		 * The space map has not been allocated yet, so treat
		 * all the space in the metaslab as free and add it to the
		 * ms_allocatable tree.
		 */
		range_tree_add(msp->ms_allocatable,
		    msp->ms_start, msp->ms_size);
	}

	/*
	 * We need to grab the ms_sync_lock to prevent metaslab_sync() from
	 * changing the ms_sm and the metaslab's range trees while we are
	 * about to use them and populate the ms_allocatable. The ms_lock
	 * is insufficient for this because metaslab_sync() doesn't hold
	 * the ms_lock while writing the ms_checkpointing tree to disk.
	 */
	mutex_enter(&msp->ms_sync_lock);
	mutex_enter(&msp->ms_lock);
	ASSERT(!msp->ms_condensing);

	if (error != 0) {
		mutex_exit(&msp->ms_sync_lock);
		return (error);
	}

	ASSERT3P(msp->ms_group, !=, NULL);
	msp->ms_loaded = B_TRUE;

	/*
	 * The ms_allocatable contains the segments that exist in the
	 * ms_defer trees [see ms_synced_length]. Thus we need to remove
	 * them from ms_allocatable as they will be added again in
	 * metaslab_sync_done().
	 */
	for (int t = 0; t < TXG_DEFER_SIZE; t++) {
		range_tree_walk(msp->ms_defer[t],
		    range_tree_remove, msp->ms_allocatable);
	}

	/*
	 * Call metaslab_recalculate_weight_and_sort() now that the
	 * metaslab is loaded so we get the metaslab's real weight.
	 *
	 * Unless this metaslab was created with older software and
	 * has not yet been converted to use segment-based weight, we
	 * expect the new weight to be better or equal to the weight
	 * that the metaslab had while it was not loaded. This is
	 * because the old weight does not take into account the
	 * consolidation of adjacent segments between TXGs. [see
	 * comment for ms_synchist and ms_deferhist[] for more info]
	 */
	uint64_t weight = msp->ms_weight;
	metaslab_recalculate_weight_and_sort(msp);
	if (!WEIGHT_IS_SPACEBASED(weight))
		ASSERT3U(weight, <=, msp->ms_weight);
	msp->ms_max_size = metaslab_block_maxsize(msp);

	spa_t *spa = msp->ms_group->mg_vd->vdev_spa;
	metaslab_verify_space(msp, spa_syncing_txg(spa));
	mutex_exit(&msp->ms_sync_lock);

	return (0);
}

int
metaslab_load(metaslab_t *msp)
{
	ASSERT(MUTEX_HELD(&msp->ms_lock));

	/*
	 * There may be another thread loading the same metaslab, if that's
	 * the case just wait until the other thread is done and return.
	 */
	metaslab_load_wait(msp);
	if (msp->ms_loaded)
		return (0);
	VERIFY(!msp->ms_loading);
	ASSERT(!msp->ms_condensing);

	msp->ms_loading = B_TRUE;
	int error = metaslab_load_impl(msp);
	msp->ms_loading = B_FALSE;
	cv_broadcast(&msp->ms_load_cv);

	return (error);
}

void
metaslab_unload(metaslab_t *msp)
{
	ASSERT(MUTEX_HELD(&msp->ms_lock));

	metaslab_verify_weight_and_frag(msp);

	range_tree_vacate(msp->ms_allocatable, NULL, NULL);
	msp->ms_loaded = B_FALSE;

	msp->ms_activation_weight = 0;
	msp->ms_weight &= ~METASLAB_ACTIVE_MASK;
	msp->ms_max_size = 0;

	/*
	 * We explicitly recalculate the metaslab's weight based on its space
	 * map (as it is now not loaded). We want unload metaslabs to always
	 * have their weights calculated from the space map histograms, while
	 * loaded ones have it calculated from their in-core range tree
	 * [see metaslab_load()]. This way, the weight reflects the information
	 * available in-core, whether it is loaded or not
	 *
	 * If ms_group == NULL means that we came here from metaslab_fini(),
	 * at which point it doesn't make sense for us to do the recalculation
	 * and the sorting.
	 */
	if (msp->ms_group != NULL)
		metaslab_recalculate_weight_and_sort(msp);
}

static void
metaslab_space_update(vdev_t *vd, metaslab_class_t *mc, int64_t alloc_delta,
    int64_t defer_delta, int64_t space_delta)
{
	vdev_space_update(vd, alloc_delta, defer_delta, space_delta);

	ASSERT3P(vd->vdev_spa->spa_root_vdev, ==, vd->vdev_parent);
	ASSERT(vd->vdev_ms_count != 0);

	metaslab_class_space_update(mc, alloc_delta, defer_delta, space_delta,
	    vdev_deflated_space(vd, space_delta));
}

int
metaslab_init(metaslab_group_t *mg, uint64_t id, uint64_t object, uint64_t txg,
    metaslab_t **msp)
{
	vdev_t *vd = mg->mg_vd;
	spa_t *spa = vd->vdev_spa;
	objset_t *mos = spa->spa_meta_objset;
	metaslab_t *ms;
	int error;

	ms = kmem_zalloc(sizeof (metaslab_t), KM_SLEEP);
	mutex_init(&ms->ms_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&ms->ms_sync_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&ms->ms_load_cv, NULL, CV_DEFAULT, NULL);

	ms->ms_id = id;
	ms->ms_start = id << vd->vdev_ms_shift;
	ms->ms_size = 1ULL << vd->vdev_ms_shift;
	ms->ms_allocator = -1;
	ms->ms_new = B_TRUE;

	/*
	 * We only open space map objects that already exist. All others
	 * will be opened when we finally allocate an object for it.
	 *
	 * Note:
	 * When called from vdev_expand(), we can't call into the DMU as
	 * we are holding the spa_config_lock as a writer and we would
	 * deadlock [see relevant comment in vdev_metaslab_init()]. in
	 * that case, the object parameter is zero though, so we won't
	 * call into the DMU.
	 */
	if (object != 0) {
		error = space_map_open(&ms->ms_sm, mos, object, ms->ms_start,
		    ms->ms_size, vd->vdev_ashift);

		if (error != 0) {
			kmem_free(ms, sizeof (metaslab_t));
			return (error);
		}

		ASSERT(ms->ms_sm != NULL);
		ms->ms_allocated_space = space_map_allocated(ms->ms_sm);
	}

	/*
	 * We create the ms_allocatable here, but we don't create the
	 * other range trees until metaslab_sync_done().  This serves
	 * two purposes: it allows metaslab_sync_done() to detect the
	 * addition of new space; and for debugging, it ensures that
	 * we'd data fault on any attempt to use this metaslab before
	 * it's ready.
	 */
	ms->ms_allocatable = range_tree_create_impl(&rt_avl_ops,
	    &ms->ms_allocatable_by_size, metaslab_rangesize_compare, 0);

	ms->ms_trim = range_tree_create(NULL, NULL);

	metaslab_group_add(mg, ms);
	metaslab_set_fragmentation(ms);

	/*
	 * If we're opening an existing pool (txg == 0) or creating
	 * a new one (txg == TXG_INITIAL), all space is available now.
	 * If we're adding space to an existing pool, the new space
	 * does not become available until after this txg has synced.
	 * The metaslab's weight will also be initialized when we sync
	 * out this txg. This ensures that we don't attempt to allocate
	 * from it before we have initialized it completely.
	 */
	if (txg <= TXG_INITIAL) {
		metaslab_sync_done(ms, 0);
		metaslab_space_update(vd, mg->mg_class,
		    metaslab_allocated_space(ms), 0, 0);
	}

	/*
	 * If metaslab_debug_load is set and we're initializing a metaslab
	 * that has an allocated space map object then load the space map
	 * so that we can verify frees.
	 */
	if (metaslab_debug_load && ms->ms_sm != NULL) {
		mutex_enter(&ms->ms_lock);
		VERIFY0(metaslab_load(ms));
		mutex_exit(&ms->ms_lock);
	}

	if (txg != 0) {
		vdev_dirty(vd, 0, NULL, txg);
		vdev_dirty(vd, VDD_METASLAB, ms, txg);
	}

	*msp = ms;

	return (0);
}

void
metaslab_fini(metaslab_t *msp)
{
	metaslab_group_t *mg = msp->ms_group;
	vdev_t *vd = mg->mg_vd;

	metaslab_group_remove(mg, msp);

	mutex_enter(&msp->ms_lock);
	VERIFY(msp->ms_group == NULL);
	metaslab_space_update(vd, mg->mg_class,
	    -metaslab_allocated_space(msp), 0, -msp->ms_size);

	space_map_close(msp->ms_sm);

	metaslab_unload(msp);

	range_tree_destroy(msp->ms_allocatable);
	range_tree_destroy(msp->ms_freeing);
	range_tree_destroy(msp->ms_freed);

	for (int t = 0; t < TXG_SIZE; t++) {
		range_tree_destroy(msp->ms_allocating[t]);
	}

	for (int t = 0; t < TXG_DEFER_SIZE; t++) {
		range_tree_destroy(msp->ms_defer[t]);
	}
	ASSERT0(msp->ms_deferspace);

	range_tree_destroy(msp->ms_checkpointing);

	for (int t = 0; t < TXG_SIZE; t++)
		ASSERT(!txg_list_member(&vd->vdev_ms_list, msp, t));

	range_tree_vacate(msp->ms_trim, NULL, NULL);
	range_tree_destroy(msp->ms_trim);

	mutex_exit(&msp->ms_lock);
	cv_destroy(&msp->ms_load_cv);
	mutex_destroy(&msp->ms_lock);
	mutex_destroy(&msp->ms_sync_lock);
	ASSERT3U(msp->ms_allocator, ==, -1);

	kmem_free(msp, sizeof (metaslab_t));
}

#define	FRAGMENTATION_TABLE_SIZE	17

/*
 * This table defines a segment size based fragmentation metric that will
 * allow each metaslab to derive its own fragmentation value. This is done
 * by calculating the space in each bucket of the spacemap histogram and
 * multiplying that by the fragmentation metric in this table. Doing
 * this for all buckets and dividing it by the total amount of free
 * space in this metaslab (i.e. the total free space in all buckets) gives
 * us the fragmentation metric. This means that a high fragmentation metric
 * equates to most of the free space being comprised of small segments.
 * Conversely, if the metric is low, then most of the free space is in
 * large segments. A 10% change in fragmentation equates to approximately
 * double the number of segments.
 *
 * This table defines 0% fragmented space using 16MB segments. Testing has
 * shown that segments that are greater than or equal to 16MB do not suffer
 * from drastic performance problems. Using this value, we derive the rest
 * of the table. Since the fragmentation value is never stored on disk, it
 * is possible to change these calculations in the future.
 */
int zfs_frag_table[FRAGMENTATION_TABLE_SIZE] = {
	100,	/* 512B	*/
	100,	/* 1K	*/
	98,	/* 2K	*/
	95,	/* 4K	*/
	90,	/* 8K	*/
	80,	/* 16K	*/
	70,	/* 32K	*/
	60,	/* 64K	*/
	50,	/* 128K	*/
	40,	/* 256K	*/
	30,	/* 512K	*/
	20,	/* 1M	*/
	15,	/* 2M	*/
	10,	/* 4M	*/
	5,	/* 8M	*/
	0	/* 16M	*/
};

/*
 * Calculate the metaslab's fragmentation metric and set ms_fragmentation.
 * Setting this value to ZFS_FRAG_INVALID means that the metaslab has not
 * been upgraded and does not support this metric. Otherwise, the return
 * value should be in the range [0, 100].
 */
static void
metaslab_set_fragmentation(metaslab_t *msp)
{
	spa_t *spa = msp->ms_group->mg_vd->vdev_spa;
	uint64_t fragmentation = 0;
	uint64_t total = 0;
	boolean_t feature_enabled = spa_feature_is_enabled(spa,
	    SPA_FEATURE_SPACEMAP_HISTOGRAM);

	if (!feature_enabled) {
		msp->ms_fragmentation = ZFS_FRAG_INVALID;
		return;
	}

	/*
	 * A null space map means that the entire metaslab is free
	 * and thus is not fragmented.
	 */
	if (msp->ms_sm == NULL) {
		msp->ms_fragmentation = 0;
		return;
	}

	/*
	 * If this metaslab's space map has not been upgraded, flag it
	 * so that we upgrade next time we encounter it.
	 */
	if (msp->ms_sm->sm_dbuf->db_size != sizeof (space_map_phys_t)) {
		uint64_t txg = spa_syncing_txg(spa);
		vdev_t *vd = msp->ms_group->mg_vd;

		/*
		 * If we've reached the final dirty txg, then we must
		 * be shutting down the pool. We don't want to dirty
		 * any data past this point so skip setting the condense
		 * flag. We can retry this action the next time the pool
		 * is imported.
		 */
		if (spa_writeable(spa) && txg < spa_final_dirty_txg(spa)) {
			msp->ms_condense_wanted = B_TRUE;
			vdev_dirty(vd, VDD_METASLAB, msp, txg + 1);
			zfs_dbgmsg("txg %llu, requesting force condense: "
			    "ms_id %llu, vdev_id %llu", txg, msp->ms_id,
			    vd->vdev_id);
		}
		msp->ms_fragmentation = ZFS_FRAG_INVALID;
		return;
	}

	for (int i = 0; i < SPACE_MAP_HISTOGRAM_SIZE; i++) {
		uint64_t space = 0;
		uint8_t shift = msp->ms_sm->sm_shift;

		int idx = MIN(shift - SPA_MINBLOCKSHIFT + i,
		    FRAGMENTATION_TABLE_SIZE - 1);

		if (msp->ms_sm->sm_phys->smp_histogram[i] == 0)
			continue;

		space = msp->ms_sm->sm_phys->smp_histogram[i] << (i + shift);
		total += space;

		ASSERT3U(idx, <, FRAGMENTATION_TABLE_SIZE);
		fragmentation += space * zfs_frag_table[idx];
	}

	if (total > 0)
		fragmentation /= total;
	ASSERT3U(fragmentation, <=, 100);

	msp->ms_fragmentation = fragmentation;
}

/*
 * Compute a weight -- a selection preference value -- for the given metaslab.
 * This is based on the amount of free space, the level of fragmentation,
 * the LBA range, and whether the metaslab is loaded.
 */
static uint64_t
metaslab_space_weight(metaslab_t *msp)
{
	metaslab_group_t *mg = msp->ms_group;
	vdev_t *vd = mg->mg_vd;
	uint64_t weight, space;

	ASSERT(MUTEX_HELD(&msp->ms_lock));
	ASSERT(!vd->vdev_removing);

	/*
	 * The baseline weight is the metaslab's free space.
	 */
	space = msp->ms_size - metaslab_allocated_space(msp);

	if (metaslab_fragmentation_factor_enabled &&
	    msp->ms_fragmentation != ZFS_FRAG_INVALID) {
		/*
		 * Use the fragmentation information to inversely scale
		 * down the baseline weight. We need to ensure that we
		 * don't exclude this metaslab completely when it's 100%
		 * fragmented. To avoid this we reduce the fragmented value
		 * by 1.
		 */
		space = (space * (100 - (msp->ms_fragmentation - 1))) / 100;

		/*
		 * If space < SPA_MINBLOCKSIZE, then we will not allocate from
		 * this metaslab again. The fragmentation metric may have
		 * decreased the space to something smaller than
		 * SPA_MINBLOCKSIZE, so reset the space to SPA_MINBLOCKSIZE
		 * so that we can consume any remaining space.
		 */
		if (space > 0 && space < SPA_MINBLOCKSIZE)
			space = SPA_MINBLOCKSIZE;
	}
	weight = space;

	/*
	 * Modern disks have uniform bit density and constant angular velocity.
	 * Therefore, the outer recording zones are faster (higher bandwidth)
	 * than the inner zones by the ratio of outer to inner track diameter,
	 * which is typically around 2:1.  We account for this by assigning
	 * higher weight to lower metaslabs (multiplier ranging from 2x to 1x).
	 * In effect, this means that we'll select the metaslab with the most
	 * free bandwidth rather than simply the one with the most free space.
	 */
	if (!vd->vdev_nonrot && metaslab_lba_weighting_enabled) {
		weight = 2 * weight - (msp->ms_id * weight) / vd->vdev_ms_count;
		ASSERT(weight >= space && weight <= 2 * space);
	}

	/*
	 * If this metaslab is one we're actively using, adjust its
	 * weight to make it preferable to any inactive metaslab so
	 * we'll polish it off. If the fragmentation on this metaslab
	 * has exceed our threshold, then don't mark it active.
	 */
	if (msp->ms_loaded && msp->ms_fragmentation != ZFS_FRAG_INVALID &&
	    msp->ms_fragmentation <= zfs_metaslab_fragmentation_threshold) {
		weight |= (msp->ms_weight & METASLAB_ACTIVE_MASK);
	}

	WEIGHT_SET_SPACEBASED(weight);
	return (weight);
}

/*
 * Return the weight of the specified metaslab, according to the segment-based
 * weighting algorithm. The metaslab must be loaded. This function can
 * be called within a sync pass since it relies only on the metaslab's
 * range tree which is always accurate when the metaslab is loaded.
 */
static uint64_t
metaslab_weight_from_range_tree(metaslab_t *msp)
{
	uint64_t weight = 0;
	uint32_t segments = 0;

	ASSERT(msp->ms_loaded);

	for (int i = RANGE_TREE_HISTOGRAM_SIZE - 1; i >= SPA_MINBLOCKSHIFT;
	    i--) {
		uint8_t shift = msp->ms_group->mg_vd->vdev_ashift;
		int max_idx = SPACE_MAP_HISTOGRAM_SIZE + shift - 1;

		segments <<= 1;
		segments += msp->ms_allocatable->rt_histogram[i];

		/*
		 * The range tree provides more precision than the space map
		 * and must be downgraded so that all values fit within the
		 * space map's histogram. This allows us to compare loaded
		 * vs. unloaded metaslabs to determine which metaslab is
		 * considered "best".
		 */
		if (i > max_idx)
			continue;

		if (segments != 0) {
			WEIGHT_SET_COUNT(weight, segments);
			WEIGHT_SET_INDEX(weight, i);
			WEIGHT_SET_ACTIVE(weight, 0);
			break;
		}
	}
	return (weight);
}

/*
 * Calculate the weight based on the on-disk histogram. This should only
 * be called after a sync pass has completely finished since the on-disk
 * information is updated in metaslab_sync().
 */
static uint64_t
metaslab_weight_from_spacemap(metaslab_t *msp)
{
	space_map_t *sm = msp->ms_sm;
	ASSERT(!msp->ms_loaded);
	ASSERT(sm != NULL);
	ASSERT3U(space_map_object(sm), !=, 0);
	ASSERT3U(sm->sm_dbuf->db_size, ==, sizeof (space_map_phys_t));

	/*
	 * Create a joint histogram from all the segments that have made
	 * it to the metaslab's space map histogram, that are not yet
	 * available for allocation because they are still in the freeing
	 * pipeline (e.g. freeing, freed, and defer trees). Then subtract
	 * these segments from the space map's histogram to get a more
	 * accurate weight.
	 */
	uint64_t deferspace_histogram[SPACE_MAP_HISTOGRAM_SIZE] = {0};
	for (int i = 0; i < SPACE_MAP_HISTOGRAM_SIZE; i++)
		deferspace_histogram[i] += msp->ms_synchist[i];
	for (int t = 0; t < TXG_DEFER_SIZE; t++) {
		for (int i = 0; i < SPACE_MAP_HISTOGRAM_SIZE; i++) {
			deferspace_histogram[i] += msp->ms_deferhist[t][i];
		}
	}

	uint64_t weight = 0;
	for (int i = SPACE_MAP_HISTOGRAM_SIZE - 1; i >= 0; i--) {
		ASSERT3U(sm->sm_phys->smp_histogram[i], >=,
		    deferspace_histogram[i]);
		uint64_t count =
		    sm->sm_phys->smp_histogram[i] - deferspace_histogram[i];
		if (count != 0) {
			WEIGHT_SET_COUNT(weight, count);
			WEIGHT_SET_INDEX(weight, i + sm->sm_shift);
			WEIGHT_SET_ACTIVE(weight, 0);
			break;
		}
	}
	return (weight);
}

/*
 * Compute a segment-based weight for the specified metaslab. The weight
 * is determined by highest bucket in the histogram. The information
 * for the highest bucket is encoded into the weight value.
 */
static uint64_t
metaslab_segment_weight(metaslab_t *msp)
{
	metaslab_group_t *mg = msp->ms_group;
	uint64_t weight = 0;
	uint8_t shift = mg->mg_vd->vdev_ashift;

	ASSERT(MUTEX_HELD(&msp->ms_lock));

	/*
	 * The metaslab is completely free.
	 */
	if (metaslab_allocated_space(msp) == 0) {
		int idx = highbit64(msp->ms_size) - 1;
		int max_idx = SPACE_MAP_HISTOGRAM_SIZE + shift - 1;

		if (idx < max_idx) {
			WEIGHT_SET_COUNT(weight, 1ULL);
			WEIGHT_SET_INDEX(weight, idx);
		} else {
			WEIGHT_SET_COUNT(weight, 1ULL << (idx - max_idx));
			WEIGHT_SET_INDEX(weight, max_idx);
		}
		WEIGHT_SET_ACTIVE(weight, 0);
		ASSERT(!WEIGHT_IS_SPACEBASED(weight));

		return (weight);
	}

	ASSERT3U(msp->ms_sm->sm_dbuf->db_size, ==, sizeof (space_map_phys_t));

	/*
	 * If the metaslab is fully allocated then just make the weight 0.
	 */
	if (metaslab_allocated_space(msp) == msp->ms_size)
		return (0);
	/*
	 * If the metaslab is already loaded, then use the range tree to
	 * determine the weight. Otherwise, we rely on the space map information
	 * to generate the weight.
	 */
	if (msp->ms_loaded) {
		weight = metaslab_weight_from_range_tree(msp);
	} else {
		weight = metaslab_weight_from_spacemap(msp);
	}

	/*
	 * If the metaslab was active the last time we calculated its weight
	 * then keep it active. We want to consume the entire region that
	 * is associated with this weight.
	 */
	if (msp->ms_activation_weight != 0 && weight != 0)
		WEIGHT_SET_ACTIVE(weight, WEIGHT_GET_ACTIVE(msp->ms_weight));
	return (weight);
}

/*
 * Determine if we should attempt to allocate from this metaslab. If the
 * metaslab has a maximum size then we can quickly determine if the desired
 * allocation size can be satisfied. Otherwise, if we're using segment-based
 * weighting then we can determine the maximum allocation that this metaslab
 * can accommodate based on the index encoded in the weight. If we're using
 * space-based weights then rely on the entire weight (excluding the weight
 * type bit).
 */
boolean_t
metaslab_should_allocate(metaslab_t *msp, uint64_t asize)
{
	if (msp->ms_max_size != 0)
		return (msp->ms_max_size >= asize);

	boolean_t should_allocate;
	if (!WEIGHT_IS_SPACEBASED(msp->ms_weight)) {
		/*
		 * The metaslab segment weight indicates segments in the
		 * range [2^i, 2^(i+1)), where i is the index in the weight.
		 * Since the asize might be in the middle of the range, we
		 * should attempt the allocation if asize < 2^(i+1).
		 */
		should_allocate = (asize <
		    1ULL << (WEIGHT_GET_INDEX(msp->ms_weight) + 1));
	} else {
		should_allocate = (asize <=
		    (msp->ms_weight & ~METASLAB_WEIGHT_TYPE));
	}

	return (should_allocate);
}
static uint64_t
metaslab_weight(metaslab_t *msp)
{
	vdev_t *vd = msp->ms_group->mg_vd;
	spa_t *spa = vd->vdev_spa;
	uint64_t weight;

	ASSERT(MUTEX_HELD(&msp->ms_lock));

	/*
	 * If this vdev is in the process of being removed, there is nothing
	 * for us to do here.
	 */
	if (vd->vdev_removing)
		return (0);

	metaslab_set_fragmentation(msp);

	/*
	 * Update the maximum size if the metaslab is loaded. This will
	 * ensure that we get an accurate maximum size if newly freed space
	 * has been added back into the free tree.
	 */
	if (msp->ms_loaded)
		msp->ms_max_size = metaslab_block_maxsize(msp);
	else
		ASSERT0(msp->ms_max_size);

	/*
	 * Segment-based weighting requires space map histogram support.
	 */
	if (zfs_metaslab_segment_weight_enabled &&
	    spa_feature_is_enabled(spa, SPA_FEATURE_SPACEMAP_HISTOGRAM) &&
	    (msp->ms_sm == NULL || msp->ms_sm->sm_dbuf->db_size ==
	    sizeof (space_map_phys_t))) {
		weight = metaslab_segment_weight(msp);
	} else {
		weight = metaslab_space_weight(msp);
	}
	return (weight);
}

void
metaslab_recalculate_weight_and_sort(metaslab_t *msp)
{
	ASSERT(MUTEX_HELD(&msp->ms_lock));

	/* note: we preserve the mask (e.g. indication of primary, etc..) */
	uint64_t was_active = msp->ms_weight & METASLAB_ACTIVE_MASK;
	metaslab_group_sort(msp->ms_group, msp,
	    metaslab_weight(msp) | was_active);
}

static int
metaslab_activate_allocator(metaslab_group_t *mg, metaslab_t *msp,
    int allocator, uint64_t activation_weight)
{
	ASSERT(MUTEX_HELD(&msp->ms_lock));

	/*
	 * If we're activating for the claim code, we don't want to actually
	 * set the metaslab up for a specific allocator.
	 */
	if (activation_weight == METASLAB_WEIGHT_CLAIM)
		return (0);

	metaslab_t **arr = (activation_weight == METASLAB_WEIGHT_PRIMARY ?
	    mg->mg_primaries : mg->mg_secondaries);

	mutex_enter(&mg->mg_lock);
	if (arr[allocator] != NULL) {
		mutex_exit(&mg->mg_lock);
		return (EEXIST);
	}

	arr[allocator] = msp;
	ASSERT3S(msp->ms_allocator, ==, -1);
	msp->ms_allocator = allocator;
	msp->ms_primary = (activation_weight == METASLAB_WEIGHT_PRIMARY);
	mutex_exit(&mg->mg_lock);

	return (0);
}

static int
metaslab_activate(metaslab_t *msp, int allocator, uint64_t activation_weight)
{
	ASSERT(MUTEX_HELD(&msp->ms_lock));

	/*
	 * The current metaslab is already activated for us so there
	 * is nothing to do. Already activated though, doesn't mean
	 * that this metaslab is activated for our allocator nor our
	 * requested activation weight. The metaslab could have started
	 * as an active one for our allocator but changed allocators
	 * while we were waiting to grab its ms_lock or we stole it
	 * [see find_valid_metaslab()]. This means that there is a
	 * possibility of passivating a metaslab of another allocator
	 * or from a different activation mask, from this thread.
	 */
	if ((msp->ms_weight & METASLAB_ACTIVE_MASK) != 0) {
		ASSERT(msp->ms_loaded);
		return (0);
	}

	int error = metaslab_load(msp);
	if (error != 0) {
		metaslab_group_sort(msp->ms_group, msp, 0);
		return (error);
	}

	/*
	 * When entering metaslab_load() we may have dropped the
	 * ms_lock because we were loading this metaslab, or we
	 * were waiting for another thread to load it for us. In
	 * that scenario, we recheck the weight of the metaslab
	 * to see if it was activated by another thread.
	 *
	 * If the metaslab was activated for another allocator or
	 * it was activated with a different activation weight (e.g.
	 * we wanted to make it a primary but it was activated as
	 * secondary) we return error (EBUSY).
	 *
	 * If the metaslab was activated for the same allocator
	 * and requested activation mask, skip activating it.
	 */
	if ((msp->ms_weight & METASLAB_ACTIVE_MASK) != 0) {
		if (msp->ms_allocator != allocator)
			return (EBUSY);

		if ((msp->ms_weight & activation_weight) == 0)
			return (SET_ERROR(EBUSY));

		EQUIV((activation_weight == METASLAB_WEIGHT_PRIMARY),
		    msp->ms_primary);
		return (0);
	}

	/*
	 * If the metaslab has literally 0 space, it will have weight 0. In
	 * that case, don't bother activating it. This can happen if the
	 * metaslab had space during find_valid_metaslab, but another thread
	 * loaded it and used all that space while we were waiting to grab the
	 * lock.
	 */
	if (msp->ms_weight == 0) {
		ASSERT0(range_tree_space(msp->ms_allocatable));
		return (SET_ERROR(ENOSPC));
	}

	if ((error = metaslab_activate_allocator(msp->ms_group, msp,
	    allocator, activation_weight)) != 0) {
		return (error);
	}

	ASSERT0(msp->ms_activation_weight);
	msp->ms_activation_weight = msp->ms_weight;
	metaslab_group_sort(msp->ms_group, msp,
	    msp->ms_weight | activation_weight);

	ASSERT(msp->ms_loaded);
	ASSERT(msp->ms_weight & METASLAB_ACTIVE_MASK);

	return (0);
}

static void
metaslab_passivate_allocator(metaslab_group_t *mg, metaslab_t *msp,
    uint64_t weight)
{
	ASSERT(MUTEX_HELD(&msp->ms_lock));
	ASSERT(msp->ms_loaded);

	if (msp->ms_weight & METASLAB_WEIGHT_CLAIM) {
		metaslab_group_sort(mg, msp, weight);
		return;
	}

	mutex_enter(&mg->mg_lock);
	ASSERT3P(msp->ms_group, ==, mg);
	ASSERT3S(0, <=, msp->ms_allocator);
	ASSERT3U(msp->ms_allocator, <, mg->mg_allocators);

	if (msp->ms_primary) {
		ASSERT3P(mg->mg_primaries[msp->ms_allocator], ==, msp);
		ASSERT(msp->ms_weight & METASLAB_WEIGHT_PRIMARY);
		mg->mg_primaries[msp->ms_allocator] = NULL;
	} else {
		ASSERT3P(mg->mg_secondaries[msp->ms_allocator], ==, msp);
		ASSERT(msp->ms_weight & METASLAB_WEIGHT_SECONDARY);
		mg->mg_secondaries[msp->ms_allocator] = NULL;
	}
	msp->ms_allocator = -1;
	metaslab_group_sort_impl(mg, msp, weight);
	mutex_exit(&mg->mg_lock);
}

static void
metaslab_passivate(metaslab_t *msp, uint64_t weight)
{
	ASSERTV(uint64_t size = weight & ~METASLAB_WEIGHT_TYPE);

	/*
	 * If size < SPA_MINBLOCKSIZE, then we will not allocate from
	 * this metaslab again.  In that case, it had better be empty,
	 * or we would be leaving space on the table.
	 */
	ASSERT(!WEIGHT_IS_SPACEBASED(msp->ms_weight) ||
	    size >= SPA_MINBLOCKSIZE ||
	    range_tree_space(msp->ms_allocatable) == 0);
	ASSERT0(weight & METASLAB_ACTIVE_MASK);

	ASSERT(msp->ms_activation_weight != 0);
	msp->ms_activation_weight = 0;
	metaslab_passivate_allocator(msp->ms_group, msp, weight);
	ASSERT0(msp->ms_weight & METASLAB_ACTIVE_MASK);
}

/*
 * Segment-based metaslabs are activated once and remain active until
 * we either fail an allocation attempt (similar to space-based metaslabs)
 * or have exhausted the free space in zfs_metaslab_switch_threshold
 * buckets since the metaslab was activated. This function checks to see
 * if we've exhaused the zfs_metaslab_switch_threshold buckets in the
 * metaslab and passivates it proactively. This will allow us to select a
 * metaslab with a larger contiguous region, if any, remaining within this
 * metaslab group. If we're in sync pass > 1, then we continue using this
 * metaslab so that we don't dirty more block and cause more sync passes.
 */
void
metaslab_segment_may_passivate(metaslab_t *msp)
{
	spa_t *spa = msp->ms_group->mg_vd->vdev_spa;

	if (WEIGHT_IS_SPACEBASED(msp->ms_weight) || spa_sync_pass(spa) > 1)
		return;

	/*
	 * Since we are in the middle of a sync pass, the most accurate
	 * information that is accessible to us is the in-core range tree
	 * histogram; calculate the new weight based on that information.
	 */
	uint64_t weight = metaslab_weight_from_range_tree(msp);
	int activation_idx = WEIGHT_GET_INDEX(msp->ms_activation_weight);
	int current_idx = WEIGHT_GET_INDEX(weight);

	if (current_idx <= activation_idx - zfs_metaslab_switch_threshold)
		metaslab_passivate(msp, weight);
}

static void
metaslab_preload(void *arg)
{
	metaslab_t *msp = arg;
	spa_t *spa = msp->ms_group->mg_vd->vdev_spa;
	fstrans_cookie_t cookie = spl_fstrans_mark();

	ASSERT(!MUTEX_HELD(&msp->ms_group->mg_lock));

	mutex_enter(&msp->ms_lock);
	(void) metaslab_load(msp);
	msp->ms_selected_txg = spa_syncing_txg(spa);
	mutex_exit(&msp->ms_lock);
	spl_fstrans_unmark(cookie);
}

static void
metaslab_group_preload(metaslab_group_t *mg)
{
	spa_t *spa = mg->mg_vd->vdev_spa;
	metaslab_t *msp;
	avl_tree_t *t = &mg->mg_metaslab_tree;
	int m = 0;

	if (spa_shutting_down(spa) || !metaslab_preload_enabled) {
		taskq_wait_outstanding(mg->mg_taskq, 0);
		return;
	}

	mutex_enter(&mg->mg_lock);

	/*
	 * Load the next potential metaslabs
	 */
	for (msp = avl_first(t); msp != NULL; msp = AVL_NEXT(t, msp)) {
		ASSERT3P(msp->ms_group, ==, mg);

		/*
		 * We preload only the maximum number of metaslabs specified
		 * by metaslab_preload_limit. If a metaslab is being forced
		 * to condense then we preload it too. This will ensure
		 * that force condensing happens in the next txg.
		 */
		if (++m > metaslab_preload_limit && !msp->ms_condense_wanted) {
			continue;
		}

		VERIFY(taskq_dispatch(mg->mg_taskq, metaslab_preload,
		    msp, TQ_SLEEP) != TASKQID_INVALID);
	}
	mutex_exit(&mg->mg_lock);
}

/*
 * Determine if the space map's on-disk footprint is past our tolerance
 * for inefficiency. We would like to use the following criteria to make
 * our decision:
 *
 * 1. The size of the space map object should not dramatically increase as a
 * result of writing out the free space range tree.
 *
 * 2. The minimal on-disk space map representation is zfs_condense_pct/100
 * times the size than the free space range tree representation
 * (i.e. zfs_condense_pct = 110 and in-core = 1MB, minimal = 1.1MB).
 *
 * 3. The on-disk size of the space map should actually decrease.
 *
 * Unfortunately, we cannot compute the on-disk size of the space map in this
 * context because we cannot accurately compute the effects of compression, etc.
 * Instead, we apply the heuristic described in the block comment for
 * zfs_metaslab_condense_block_threshold - we only condense if the space used
 * is greater than a threshold number of blocks.
 */
static boolean_t
metaslab_should_condense(metaslab_t *msp)
{
	space_map_t *sm = msp->ms_sm;
	vdev_t *vd = msp->ms_group->mg_vd;
	uint64_t vdev_blocksize = 1 << vd->vdev_ashift;
	uint64_t current_txg = spa_syncing_txg(vd->vdev_spa);

	ASSERT(MUTEX_HELD(&msp->ms_lock));
	ASSERT(msp->ms_loaded);

	/*
	 * Allocations and frees in early passes are generally more space
	 * efficient (in terms of blocks described in space map entries)
	 * than the ones in later passes (e.g. we don't compress after
	 * sync pass 5) and condensing a metaslab multiple times in a txg
	 * could degrade performance.
	 *
	 * Thus we prefer condensing each metaslab at most once every txg at
	 * the earliest sync pass possible. If a metaslab is eligible for
	 * condensing again after being considered for condensing within the
	 * same txg, it will hopefully be dirty in the next txg where it will
	 * be condensed at an earlier pass.
	 */
	if (msp->ms_condense_checked_txg == current_txg)
		return (B_FALSE);
	msp->ms_condense_checked_txg = current_txg;

	/*
	 * We always condense metaslabs that are empty and metaslabs for
	 * which a condense request has been made.
	 */
	if (avl_is_empty(&msp->ms_allocatable_by_size) ||
	    msp->ms_condense_wanted)
		return (B_TRUE);

	uint64_t object_size = space_map_length(msp->ms_sm);
	uint64_t optimal_size = space_map_estimate_optimal_size(sm,
	    msp->ms_allocatable, SM_NO_VDEVID);

	dmu_object_info_t doi;
	dmu_object_info_from_db(sm->sm_dbuf, &doi);
	uint64_t record_size = MAX(doi.doi_data_block_size, vdev_blocksize);

	return (object_size >= (optimal_size * zfs_condense_pct / 100) &&
	    object_size > zfs_metaslab_condense_block_threshold * record_size);
}

/*
 * Condense the on-disk space map representation to its minimized form.
 * The minimized form consists of a small number of allocations followed by
 * the entries of the free range tree.
 */
static void
metaslab_condense(metaslab_t *msp, uint64_t txg, dmu_tx_t *tx)
{
	range_tree_t *condense_tree;
	space_map_t *sm = msp->ms_sm;

	ASSERT(MUTEX_HELD(&msp->ms_lock));
	ASSERT(msp->ms_loaded);


	zfs_dbgmsg("condensing: txg %llu, msp[%llu] %px, vdev id %llu, "
	    "spa %s, smp size %llu, segments %lu, forcing condense=%s", txg,
	    msp->ms_id, msp, msp->ms_group->mg_vd->vdev_id,
	    msp->ms_group->mg_vd->vdev_spa->spa_name,
	    space_map_length(msp->ms_sm),
	    avl_numnodes(&msp->ms_allocatable->rt_root),
	    msp->ms_condense_wanted ? "TRUE" : "FALSE");

	msp->ms_condense_wanted = B_FALSE;

	/*
	 * Create an range tree that is 100% allocated. We remove segments
	 * that have been freed in this txg, any deferred frees that exist,
	 * and any allocation in the future. Removing segments should be
	 * a relatively inexpensive operation since we expect these trees to
	 * have a small number of nodes.
	 */
	condense_tree = range_tree_create(NULL, NULL);
	range_tree_add(condense_tree, msp->ms_start, msp->ms_size);

	range_tree_walk(msp->ms_freeing, range_tree_remove, condense_tree);
	range_tree_walk(msp->ms_freed, range_tree_remove, condense_tree);

	for (int t = 0; t < TXG_DEFER_SIZE; t++) {
		range_tree_walk(msp->ms_defer[t],
		    range_tree_remove, condense_tree);
	}

	for (int t = 1; t < TXG_CONCURRENT_STATES; t++) {
		range_tree_walk(msp->ms_allocating[(txg + t) & TXG_MASK],
		    range_tree_remove, condense_tree);
	}

	/*
	 * We're about to drop the metaslab's lock thus allowing
	 * other consumers to change it's content. Set the
	 * metaslab's ms_condensing flag to ensure that
	 * allocations on this metaslab do not occur while we're
	 * in the middle of committing it to disk. This is only critical
	 * for ms_allocatable as all other range trees use per txg
	 * views of their content.
	 */
	msp->ms_condensing = B_TRUE;

	mutex_exit(&msp->ms_lock);
	space_map_truncate(sm, zfs_metaslab_sm_blksz, tx);

	/*
	 * While we would ideally like to create a space map representation
	 * that consists only of allocation records, doing so can be
	 * prohibitively expensive because the in-core free tree can be
	 * large, and therefore computationally expensive to subtract
	 * from the condense_tree. Instead we sync out two trees, a cheap
	 * allocation only tree followed by the in-core free tree. While not
	 * optimal, this is typically close to optimal, and much cheaper to
	 * compute.
	 */
	space_map_write(sm, condense_tree, SM_ALLOC, SM_NO_VDEVID, tx);
	range_tree_vacate(condense_tree, NULL, NULL);
	range_tree_destroy(condense_tree);

	space_map_write(sm, msp->ms_allocatable, SM_FREE, SM_NO_VDEVID, tx);
	mutex_enter(&msp->ms_lock);
	msp->ms_condensing = B_FALSE;
}

/*
 * Write a metaslab to disk in the context of the specified transaction group.
 */
void
metaslab_sync(metaslab_t *msp, uint64_t txg)
{
	metaslab_group_t *mg = msp->ms_group;
	vdev_t *vd = mg->mg_vd;
	spa_t *spa = vd->vdev_spa;
	objset_t *mos = spa_meta_objset(spa);
	range_tree_t *alloctree = msp->ms_allocating[txg & TXG_MASK];
	dmu_tx_t *tx;
	uint64_t object = space_map_object(msp->ms_sm);

	ASSERT(!vd->vdev_ishole);

	/*
	 * This metaslab has just been added so there's no work to do now.
	 */
	if (msp->ms_freeing == NULL) {
		ASSERT3P(alloctree, ==, NULL);
		return;
	}

	ASSERT3P(alloctree, !=, NULL);
	ASSERT3P(msp->ms_freeing, !=, NULL);
	ASSERT3P(msp->ms_freed, !=, NULL);
	ASSERT3P(msp->ms_checkpointing, !=, NULL);
	ASSERT3P(msp->ms_trim, !=, NULL);

	/*
	 * Normally, we don't want to process a metaslab if there are no
	 * allocations or frees to perform. However, if the metaslab is being
	 * forced to condense, it's loaded and we're not beyond the final
	 * dirty txg, we need to let it through. Not condensing beyond the
	 * final dirty txg prevents an issue where metaslabs that need to be
	 * condensed but were loaded for other reasons could cause a panic
	 * here. By only checking the txg in that branch of the conditional,
	 * we preserve the utility of the VERIFY statements in all other
	 * cases.
	 */
	if (range_tree_is_empty(alloctree) &&
	    range_tree_is_empty(msp->ms_freeing) &&
	    range_tree_is_empty(msp->ms_checkpointing) &&
	    !(msp->ms_loaded && msp->ms_condense_wanted &&
	    txg <= spa_final_dirty_txg(spa)))
		return;


	VERIFY(txg <= spa_final_dirty_txg(spa));

	/*
	 * The only state that can actually be changing concurrently
	 * with metaslab_sync() is the metaslab's ms_allocatable. No
	 * other thread can be modifying this txg's alloc, freeing,
	 * freed, or space_map_phys_t.  We drop ms_lock whenever we
	 * could call into the DMU, because the DMU can call down to
	 * us (e.g. via zio_free()) at any time.
	 *
	 * The spa_vdev_remove_thread() can be reading metaslab state
	 * concurrently, and it is locked out by the ms_sync_lock.
	 * Note that the ms_lock is insufficient for this, because it
	 * is dropped by space_map_write().
	 */
	tx = dmu_tx_create_assigned(spa_get_dsl(spa), txg);

	if (msp->ms_sm == NULL) {
		uint64_t new_object;

		new_object = space_map_alloc(mos, zfs_metaslab_sm_blksz, tx);
		VERIFY3U(new_object, !=, 0);

		VERIFY0(space_map_open(&msp->ms_sm, mos, new_object,
		    msp->ms_start, msp->ms_size, vd->vdev_ashift));

		ASSERT(msp->ms_sm != NULL);
		ASSERT0(metaslab_allocated_space(msp));
	}

	if (!range_tree_is_empty(msp->ms_checkpointing) &&
	    vd->vdev_checkpoint_sm == NULL) {
		ASSERT(spa_has_checkpoint(spa));

		uint64_t new_object = space_map_alloc(mos,
		    vdev_standard_sm_blksz, tx);
		VERIFY3U(new_object, !=, 0);

		VERIFY0(space_map_open(&vd->vdev_checkpoint_sm,
		    mos, new_object, 0, vd->vdev_asize, vd->vdev_ashift));
		ASSERT3P(vd->vdev_checkpoint_sm, !=, NULL);

		/*
		 * We save the space map object as an entry in vdev_top_zap
		 * so it can be retrieved when the pool is reopened after an
		 * export or through zdb.
		 */
		VERIFY0(zap_add(vd->vdev_spa->spa_meta_objset,
		    vd->vdev_top_zap, VDEV_TOP_ZAP_POOL_CHECKPOINT_SM,
		    sizeof (new_object), 1, &new_object, tx));
	}

	mutex_enter(&msp->ms_sync_lock);
	mutex_enter(&msp->ms_lock);

	/*
	 * Note: metaslab_condense() clears the space map's histogram.
	 * Therefore we must verify and remove this histogram before
	 * condensing.
	 */
	metaslab_group_histogram_verify(mg);
	metaslab_class_histogram_verify(mg->mg_class);
	metaslab_group_histogram_remove(mg, msp);

	if (msp->ms_loaded && metaslab_should_condense(msp)) {
		metaslab_condense(msp, txg, tx);
	} else {
		mutex_exit(&msp->ms_lock);
		space_map_write(msp->ms_sm, alloctree, SM_ALLOC,
		    SM_NO_VDEVID, tx);
		space_map_write(msp->ms_sm, msp->ms_freeing, SM_FREE,
		    SM_NO_VDEVID, tx);
		mutex_enter(&msp->ms_lock);
	}

	msp->ms_allocated_space += range_tree_space(alloctree);
	ASSERT3U(msp->ms_allocated_space, >=,
	    range_tree_space(msp->ms_freeing));
	msp->ms_allocated_space -= range_tree_space(msp->ms_freeing);

	if (!range_tree_is_empty(msp->ms_checkpointing)) {
		ASSERT(spa_has_checkpoint(spa));
		ASSERT3P(vd->vdev_checkpoint_sm, !=, NULL);

		/*
		 * Since we are doing writes to disk and the ms_checkpointing
		 * tree won't be changing during that time, we drop the
		 * ms_lock while writing to the checkpoint space map.
		 */
		mutex_exit(&msp->ms_lock);
		space_map_write(vd->vdev_checkpoint_sm,
		    msp->ms_checkpointing, SM_FREE, SM_NO_VDEVID, tx);
		mutex_enter(&msp->ms_lock);

		spa->spa_checkpoint_info.sci_dspace +=
		    range_tree_space(msp->ms_checkpointing);
		vd->vdev_stat.vs_checkpoint_space +=
		    range_tree_space(msp->ms_checkpointing);
		ASSERT3U(vd->vdev_stat.vs_checkpoint_space, ==,
		    -space_map_allocated(vd->vdev_checkpoint_sm));

		range_tree_vacate(msp->ms_checkpointing, NULL, NULL);
	}

	if (msp->ms_loaded) {
		/*
		 * When the space map is loaded, we have an accurate
		 * histogram in the range tree. This gives us an opportunity
		 * to bring the space map's histogram up-to-date so we clear
		 * it first before updating it.
		 */
		space_map_histogram_clear(msp->ms_sm);
		space_map_histogram_add(msp->ms_sm, msp->ms_allocatable, tx);

		/*
		 * Since we've cleared the histogram we need to add back
		 * any free space that has already been processed, plus
		 * any deferred space. This allows the on-disk histogram
		 * to accurately reflect all free space even if some space
		 * is not yet available for allocation (i.e. deferred).
		 */
		space_map_histogram_add(msp->ms_sm, msp->ms_freed, tx);

		/*
		 * Add back any deferred free space that has not been
		 * added back into the in-core free tree yet. This will
		 * ensure that we don't end up with a space map histogram
		 * that is completely empty unless the metaslab is fully
		 * allocated.
		 */
		for (int t = 0; t < TXG_DEFER_SIZE; t++) {
			space_map_histogram_add(msp->ms_sm,
			    msp->ms_defer[t], tx);
		}
	}

	/*
	 * Always add the free space from this sync pass to the space
	 * map histogram. We want to make sure that the on-disk histogram
	 * accounts for all free space. If the space map is not loaded,
	 * then we will lose some accuracy but will correct it the next
	 * time we load the space map.
	 */
	space_map_histogram_add(msp->ms_sm, msp->ms_freeing, tx);
	metaslab_aux_histograms_update(msp);

	metaslab_group_histogram_add(mg, msp);
	metaslab_group_histogram_verify(mg);
	metaslab_class_histogram_verify(mg->mg_class);

	/*
	 * For sync pass 1, we avoid traversing this txg's free range tree
	 * and instead will just swap the pointers for freeing and freed.
	 * We can safely do this since the freed_tree is guaranteed to be
	 * empty on the initial pass.
	 */
	if (spa_sync_pass(spa) == 1) {
		range_tree_swap(&msp->ms_freeing, &msp->ms_freed);
		ASSERT0(msp->ms_allocated_this_txg);
	} else {
		range_tree_vacate(msp->ms_freeing,
		    range_tree_add, msp->ms_freed);
	}
	msp->ms_allocated_this_txg += range_tree_space(alloctree);
	range_tree_vacate(alloctree, NULL, NULL);

	ASSERT0(range_tree_space(msp->ms_allocating[txg & TXG_MASK]));
	ASSERT0(range_tree_space(msp->ms_allocating[TXG_CLEAN(txg)
	    & TXG_MASK]));
	ASSERT0(range_tree_space(msp->ms_freeing));
	ASSERT0(range_tree_space(msp->ms_checkpointing));

	mutex_exit(&msp->ms_lock);

	if (object != space_map_object(msp->ms_sm)) {
		object = space_map_object(msp->ms_sm);
		dmu_write(mos, vd->vdev_ms_array, sizeof (uint64_t) *
		    msp->ms_id, sizeof (uint64_t), &object, tx);
	}
	mutex_exit(&msp->ms_sync_lock);
	dmu_tx_commit(tx);
}

void
metaslab_potentially_unload(metaslab_t *msp, uint64_t txg)
{
	/*
	 * If the metaslab is loaded and we've not tried to load or allocate
	 * from it in 'metaslab_unload_delay' txgs, then unload it.
	 */
	if (msp->ms_loaded &&
	    msp->ms_disabled == 0 &&
	    msp->ms_selected_txg + metaslab_unload_delay < txg) {
		for (int t = 1; t < TXG_CONCURRENT_STATES; t++) {
			VERIFY0(range_tree_space(
			    msp->ms_allocating[(txg + t) & TXG_MASK]));
		}
		if (msp->ms_allocator != -1) {
			metaslab_passivate(msp, msp->ms_weight &
			    ~METASLAB_ACTIVE_MASK);
		}

		if (!metaslab_debug_unload)
			metaslab_unload(msp);
	}
}

/*
 * Called after a transaction group has completely synced to mark
 * all of the metaslab's free space as usable.
 */
void
metaslab_sync_done(metaslab_t *msp, uint64_t txg)
{
	metaslab_group_t *mg = msp->ms_group;
	vdev_t *vd = mg->mg_vd;
	spa_t *spa = vd->vdev_spa;
	range_tree_t **defer_tree;
	int64_t alloc_delta, defer_delta;
	boolean_t defer_allowed = B_TRUE;

	ASSERT(!vd->vdev_ishole);

	mutex_enter(&msp->ms_lock);

	/*
	 * If this metaslab is just becoming available, initialize its
	 * range trees and add its capacity to the vdev.
	 */
	if (msp->ms_freed == NULL) {
		for (int t = 0; t < TXG_SIZE; t++) {
			ASSERT(msp->ms_allocating[t] == NULL);

			msp->ms_allocating[t] = range_tree_create(NULL, NULL);
		}

		ASSERT3P(msp->ms_freeing, ==, NULL);
		msp->ms_freeing = range_tree_create(NULL, NULL);

		ASSERT3P(msp->ms_freed, ==, NULL);
		msp->ms_freed = range_tree_create(NULL, NULL);

		for (int t = 0; t < TXG_DEFER_SIZE; t++) {
			ASSERT(msp->ms_defer[t] == NULL);

			msp->ms_defer[t] = range_tree_create(NULL, NULL);
		}

		ASSERT3P(msp->ms_checkpointing, ==, NULL);
		msp->ms_checkpointing = range_tree_create(NULL, NULL);

		metaslab_space_update(vd, mg->mg_class, 0, 0, msp->ms_size);
	}
	ASSERT0(range_tree_space(msp->ms_freeing));
	ASSERT0(range_tree_space(msp->ms_checkpointing));

	defer_tree = &msp->ms_defer[txg % TXG_DEFER_SIZE];

	uint64_t free_space = metaslab_class_get_space(spa_normal_class(spa)) -
	    metaslab_class_get_alloc(spa_normal_class(spa));
	if (free_space <= spa_get_slop_space(spa) || vd->vdev_removing) {
		defer_allowed = B_FALSE;
	}

	defer_delta = 0;
	alloc_delta = msp->ms_allocated_this_txg -
	    range_tree_space(msp->ms_freed);
	if (defer_allowed) {
		defer_delta = range_tree_space(msp->ms_freed) -
		    range_tree_space(*defer_tree);
	} else {
		defer_delta -= range_tree_space(*defer_tree);
	}

	metaslab_space_update(vd, mg->mg_class, alloc_delta + defer_delta,
	    defer_delta, 0);

	/*
	 * If there's a metaslab_load() in progress, wait for it to complete
	 * so that we have a consistent view of the in-core space map.
	 */
	metaslab_load_wait(msp);

	/*
	 * When auto-trimming is enabled, free ranges which are added to
	 * ms_allocatable are also be added to ms_trim.  The ms_trim tree is
	 * periodically consumed by the vdev_autotrim_thread() which issues
	 * trims for all ranges and then vacates the tree.  The ms_trim tree
	 * can be discarded at any time with the sole consequence of recent
	 * frees not being trimmed.
	 */
	if (spa_get_autotrim(spa) == SPA_AUTOTRIM_ON) {
		range_tree_walk(*defer_tree, range_tree_add, msp->ms_trim);
		if (!defer_allowed) {
			range_tree_walk(msp->ms_freed, range_tree_add,
			    msp->ms_trim);
		}
	} else {
		range_tree_vacate(msp->ms_trim, NULL, NULL);
	}

	/*
	 * Move the frees from the defer_tree back to the free
	 * range tree (if it's loaded). Swap the freed_tree and
	 * the defer_tree -- this is safe to do because we've
	 * just emptied out the defer_tree.
	 */
	range_tree_vacate(*defer_tree,
	    msp->ms_loaded ? range_tree_add : NULL, msp->ms_allocatable);
	if (defer_allowed) {
		range_tree_swap(&msp->ms_freed, defer_tree);
	} else {
		range_tree_vacate(msp->ms_freed,
		    msp->ms_loaded ? range_tree_add : NULL,
		    msp->ms_allocatable);
	}

	msp->ms_synced_length = space_map_length(msp->ms_sm);

	msp->ms_deferspace += defer_delta;
	ASSERT3S(msp->ms_deferspace, >=, 0);
	ASSERT3S(msp->ms_deferspace, <=, msp->ms_size);
	if (msp->ms_deferspace != 0) {
		/*
		 * Keep syncing this metaslab until all deferred frees
		 * are back in circulation.
		 */
		vdev_dirty(vd, VDD_METASLAB, msp, txg + 1);
	}
	metaslab_aux_histograms_update_done(msp, defer_allowed);

	if (msp->ms_new) {
		msp->ms_new = B_FALSE;
		mutex_enter(&mg->mg_lock);
		mg->mg_ms_ready++;
		mutex_exit(&mg->mg_lock);
	}

	/*
	 * Re-sort metaslab within its group now that we've adjusted
	 * its allocatable space.
	 */
	metaslab_recalculate_weight_and_sort(msp);

	ASSERT0(range_tree_space(msp->ms_allocating[txg & TXG_MASK]));
	ASSERT0(range_tree_space(msp->ms_freeing));
	ASSERT0(range_tree_space(msp->ms_freed));
	ASSERT0(range_tree_space(msp->ms_checkpointing));

	msp->ms_allocated_this_txg = 0;
	mutex_exit(&msp->ms_lock);
}

void
metaslab_sync_reassess(metaslab_group_t *mg)
{
	spa_t *spa = mg->mg_class->mc_spa;

	spa_config_enter(spa, SCL_ALLOC, FTAG, RW_READER);
	metaslab_group_alloc_update(mg);
	mg->mg_fragmentation = metaslab_group_fragmentation(mg);

	/*
	 * Preload the next potential metaslabs but only on active
	 * metaslab groups. We can get into a state where the metaslab
	 * is no longer active since we dirty metaslabs as we remove a
	 * a device, thus potentially making the metaslab group eligible
	 * for preloading.
	 */
	if (mg->mg_activation_count > 0) {
		metaslab_group_preload(mg);
	}
	spa_config_exit(spa, SCL_ALLOC, FTAG);
}

/*
 * When writing a ditto block (i.e. more than one DVA for a given BP) on
 * the same vdev as an existing DVA of this BP, then try to allocate it
 * on a different metaslab than existing DVAs (i.e. a unique metaslab).
 */
static boolean_t
metaslab_is_unique(metaslab_t *msp, dva_t *dva)
{
	uint64_t dva_ms_id;

	if (DVA_GET_ASIZE(dva) == 0)
		return (B_TRUE);

	if (msp->ms_group->mg_vd->vdev_id != DVA_GET_VDEV(dva))
		return (B_TRUE);

	dva_ms_id = DVA_GET_OFFSET(dva) >> msp->ms_group->mg_vd->vdev_ms_shift;

	return (msp->ms_id != dva_ms_id);
}

/*
 * ==========================================================================
 * Metaslab allocation tracing facility
 * ==========================================================================
 */
#ifdef _METASLAB_TRACING
kstat_t *metaslab_trace_ksp;
kstat_named_t metaslab_trace_over_limit;

void
metaslab_alloc_trace_init(void)
{
	ASSERT(metaslab_alloc_trace_cache == NULL);
	metaslab_alloc_trace_cache = kmem_cache_create(
	    "metaslab_alloc_trace_cache", sizeof (metaslab_alloc_trace_t),
	    0, NULL, NULL, NULL, NULL, NULL, 0);
	metaslab_trace_ksp = kstat_create("zfs", 0, "metaslab_trace_stats",
	    "misc", KSTAT_TYPE_NAMED, 1, KSTAT_FLAG_VIRTUAL);
	if (metaslab_trace_ksp != NULL) {
		metaslab_trace_ksp->ks_data = &metaslab_trace_over_limit;
		kstat_named_init(&metaslab_trace_over_limit,
		    "metaslab_trace_over_limit", KSTAT_DATA_UINT64);
		kstat_install(metaslab_trace_ksp);
	}
}

void
metaslab_alloc_trace_fini(void)
{
	if (metaslab_trace_ksp != NULL) {
		kstat_delete(metaslab_trace_ksp);
		metaslab_trace_ksp = NULL;
	}
	kmem_cache_destroy(metaslab_alloc_trace_cache);
	metaslab_alloc_trace_cache = NULL;
}

/*
 * Add an allocation trace element to the allocation tracing list.
 */
static void
metaslab_trace_add(zio_alloc_list_t *zal, metaslab_group_t *mg,
    metaslab_t *msp, uint64_t psize, uint32_t dva_id, uint64_t offset,
    int allocator)
{
	metaslab_alloc_trace_t *mat;

	if (!metaslab_trace_enabled)
		return;

	/*
	 * When the tracing list reaches its maximum we remove
	 * the second element in the list before adding a new one.
	 * By removing the second element we preserve the original
	 * entry as a clue to what allocations steps have already been
	 * performed.
	 */
	if (zal->zal_size == metaslab_trace_max_entries) {
		metaslab_alloc_trace_t *mat_next;
#ifdef DEBUG
		panic("too many entries in allocation list");
#endif
		atomic_inc_64(&metaslab_trace_over_limit.value.ui64);
		zal->zal_size--;
		mat_next = list_next(&zal->zal_list, list_head(&zal->zal_list));
		list_remove(&zal->zal_list, mat_next);
		kmem_cache_free(metaslab_alloc_trace_cache, mat_next);
	}

	mat = kmem_cache_alloc(metaslab_alloc_trace_cache, KM_SLEEP);
	list_link_init(&mat->mat_list_node);
	mat->mat_mg = mg;
	mat->mat_msp = msp;
	mat->mat_size = psize;
	mat->mat_dva_id = dva_id;
	mat->mat_offset = offset;
	mat->mat_weight = 0;
	mat->mat_allocator = allocator;

	if (msp != NULL)
		mat->mat_weight = msp->ms_weight;

	/*
	 * The list is part of the zio so locking is not required. Only
	 * a single thread will perform allocations for a given zio.
	 */
	list_insert_tail(&zal->zal_list, mat);
	zal->zal_size++;

	ASSERT3U(zal->zal_size, <=, metaslab_trace_max_entries);
}

void
metaslab_trace_init(zio_alloc_list_t *zal)
{
	list_create(&zal->zal_list, sizeof (metaslab_alloc_trace_t),
	    offsetof(metaslab_alloc_trace_t, mat_list_node));
	zal->zal_size = 0;
}

void
metaslab_trace_fini(zio_alloc_list_t *zal)
{
	metaslab_alloc_trace_t *mat;

	while ((mat = list_remove_head(&zal->zal_list)) != NULL)
		kmem_cache_free(metaslab_alloc_trace_cache, mat);
	list_destroy(&zal->zal_list);
	zal->zal_size = 0;
}
#else

#define	metaslab_trace_add(zal, mg, msp, psize, id, off, alloc)

void
metaslab_alloc_trace_init(void)
{
}

void
metaslab_alloc_trace_fini(void)
{
}

void
metaslab_trace_init(zio_alloc_list_t *zal)
{
}

void
metaslab_trace_fini(zio_alloc_list_t *zal)
{
}

#endif /* _METASLAB_TRACING */

/*
 * ==========================================================================
 * Metaslab block operations
 * ==========================================================================
 */

static void
metaslab_group_alloc_increment(spa_t *spa, uint64_t vdev, void *tag, int flags,
    int allocator)
{
	if (!(flags & METASLAB_ASYNC_ALLOC) ||
	    (flags & METASLAB_DONT_THROTTLE))
		return;

	metaslab_group_t *mg = vdev_lookup_top(spa, vdev)->vdev_mg;
	if (!mg->mg_class->mc_alloc_throttle_enabled)
		return;

	(void) zfs_refcount_add(&mg->mg_alloc_queue_depth[allocator], tag);
}

static void
metaslab_group_increment_qdepth(metaslab_group_t *mg, int allocator)
{
	uint64_t max = mg->mg_max_alloc_queue_depth;
	uint64_t cur = mg->mg_cur_max_alloc_queue_depth[allocator];
	while (cur < max) {
		if (atomic_cas_64(&mg->mg_cur_max_alloc_queue_depth[allocator],
		    cur, cur + 1) == cur) {
			atomic_inc_64(
			    &mg->mg_class->mc_alloc_max_slots[allocator]);
			return;
		}
		cur = mg->mg_cur_max_alloc_queue_depth[allocator];
	}
}

void
metaslab_group_alloc_decrement(spa_t *spa, uint64_t vdev, void *tag, int flags,
    int allocator, boolean_t io_complete)
{
	if (!(flags & METASLAB_ASYNC_ALLOC) ||
	    (flags & METASLAB_DONT_THROTTLE))
		return;

	metaslab_group_t *mg = vdev_lookup_top(spa, vdev)->vdev_mg;
	if (!mg->mg_class->mc_alloc_throttle_enabled)
		return;

	(void) zfs_refcount_remove(&mg->mg_alloc_queue_depth[allocator], tag);
	if (io_complete)
		metaslab_group_increment_qdepth(mg, allocator);
}

void
metaslab_group_alloc_verify(spa_t *spa, const blkptr_t *bp, void *tag,
    int allocator)
{
#ifdef ZFS_DEBUG
	const dva_t *dva = bp->blk_dva;
	int ndvas = BP_GET_NDVAS(bp);

	for (int d = 0; d < ndvas; d++) {
		uint64_t vdev = DVA_GET_VDEV(&dva[d]);
		metaslab_group_t *mg = vdev_lookup_top(spa, vdev)->vdev_mg;
		VERIFY(zfs_refcount_not_held(
		    &mg->mg_alloc_queue_depth[allocator], tag));
	}
#endif
}

static uint64_t
metaslab_block_alloc(metaslab_t *msp, uint64_t size, uint64_t txg)
{
	uint64_t start;
	range_tree_t *rt = msp->ms_allocatable;
	metaslab_class_t *mc = msp->ms_group->mg_class;

	VERIFY(!msp->ms_condensing);
	VERIFY0(msp->ms_disabled);

	start = mc->mc_ops->msop_alloc(msp, size);
	if (start != -1ULL) {
		metaslab_group_t *mg = msp->ms_group;
		vdev_t *vd = mg->mg_vd;

		VERIFY0(P2PHASE(start, 1ULL << vd->vdev_ashift));
		VERIFY0(P2PHASE(size, 1ULL << vd->vdev_ashift));
		VERIFY3U(range_tree_space(rt) - size, <=, msp->ms_size);
		range_tree_remove(rt, start, size);
		range_tree_clear(msp->ms_trim, start, size);

		if (range_tree_is_empty(msp->ms_allocating[txg & TXG_MASK]))
			vdev_dirty(mg->mg_vd, VDD_METASLAB, msp, txg);

		range_tree_add(msp->ms_allocating[txg & TXG_MASK], start, size);

		/* Track the last successful allocation */
		msp->ms_alloc_txg = txg;
		metaslab_verify_space(msp, txg);
	}

	/*
	 * Now that we've attempted the allocation we need to update the
	 * metaslab's maximum block size since it may have changed.
	 */
	msp->ms_max_size = metaslab_block_maxsize(msp);
	return (start);
}

/*
 * Find the metaslab with the highest weight that is less than what we've
 * already tried.  In the common case, this means that we will examine each
 * metaslab at most once. Note that concurrent callers could reorder metaslabs
 * by activation/passivation once we have dropped the mg_lock. If a metaslab is
 * activated by another thread, and we fail to allocate from the metaslab we
 * have selected, we may not try the newly-activated metaslab, and instead
 * activate another metaslab.  This is not optimal, but generally does not cause
 * any problems (a possible exception being if every metaslab is completely full
 * except for the the newly-activated metaslab which we fail to examine).
 */
static metaslab_t *
find_valid_metaslab(metaslab_group_t *mg, uint64_t activation_weight,
    dva_t *dva, int d, boolean_t want_unique, uint64_t asize, int allocator,
    zio_alloc_list_t *zal, metaslab_t *search, boolean_t *was_active)
{
	avl_index_t idx;
	avl_tree_t *t = &mg->mg_metaslab_tree;
	metaslab_t *msp = avl_find(t, search, &idx);
	if (msp == NULL)
		msp = avl_nearest(t, idx, AVL_AFTER);

	for (; msp != NULL; msp = AVL_NEXT(t, msp)) {
		int i;
		if (!metaslab_should_allocate(msp, asize)) {
			metaslab_trace_add(zal, mg, msp, asize, d,
			    TRACE_TOO_SMALL, allocator);
			continue;
		}

		/*
		 * If the selected metaslab is condensing or disabled,
		 * skip it.
		 */
		if (msp->ms_condensing || msp->ms_disabled > 0)
			continue;

		*was_active = msp->ms_allocator != -1;
		/*
		 * If we're activating as primary, this is our first allocation
		 * from this disk, so we don't need to check how close we are.
		 * If the metaslab under consideration was already active,
		 * we're getting desperate enough to steal another allocator's
		 * metaslab, so we still don't care about distances.
		 */
		if (activation_weight == METASLAB_WEIGHT_PRIMARY || *was_active)
			break;

		for (i = 0; i < d; i++) {
			if (want_unique &&
			    !metaslab_is_unique(msp, &dva[i]))
				break;  /* try another metaslab */
		}
		if (i == d)
			break;
	}

	if (msp != NULL) {
		search->ms_weight = msp->ms_weight;
		search->ms_start = msp->ms_start + 1;
		search->ms_allocator = msp->ms_allocator;
		search->ms_primary = msp->ms_primary;
	}
	return (msp);
}

void
metaslab_active_mask_verify(metaslab_t *msp)
{
	ASSERT(MUTEX_HELD(&msp->ms_lock));

	if ((zfs_flags & ZFS_DEBUG_METASLAB_VERIFY) == 0)
		return;

	if ((msp->ms_weight & METASLAB_ACTIVE_MASK) == 0)
		return;

	if (msp->ms_weight & METASLAB_WEIGHT_PRIMARY) {
		VERIFY0(msp->ms_weight & METASLAB_WEIGHT_SECONDARY);
		VERIFY0(msp->ms_weight & METASLAB_WEIGHT_CLAIM);
		VERIFY3S(msp->ms_allocator, !=, -1);
		VERIFY(msp->ms_primary);
		return;
	}

	if (msp->ms_weight & METASLAB_WEIGHT_SECONDARY) {
		VERIFY0(msp->ms_weight & METASLAB_WEIGHT_PRIMARY);
		VERIFY0(msp->ms_weight & METASLAB_WEIGHT_CLAIM);
		VERIFY3S(msp->ms_allocator, !=, -1);
		VERIFY(!msp->ms_primary);
		return;
	}

	if (msp->ms_weight & METASLAB_WEIGHT_CLAIM) {
		VERIFY0(msp->ms_weight & METASLAB_WEIGHT_PRIMARY);
		VERIFY0(msp->ms_weight & METASLAB_WEIGHT_SECONDARY);
		VERIFY3S(msp->ms_allocator, ==, -1);
		return;
	}
}

/* ARGSUSED */
static uint64_t
metaslab_group_alloc_normal(metaslab_group_t *mg, zio_alloc_list_t *zal,
    uint64_t asize, uint64_t txg, boolean_t want_unique, dva_t *dva,
    int d, int allocator)
{
	metaslab_t *msp = NULL;
	uint64_t offset = -1ULL;

	uint64_t activation_weight = METASLAB_WEIGHT_PRIMARY;
	for (int i = 0; i < d; i++) {
		if (activation_weight == METASLAB_WEIGHT_PRIMARY &&
		    DVA_GET_VDEV(&dva[i]) == mg->mg_vd->vdev_id) {
			activation_weight = METASLAB_WEIGHT_SECONDARY;
		} else if (activation_weight == METASLAB_WEIGHT_SECONDARY &&
		    DVA_GET_VDEV(&dva[i]) == mg->mg_vd->vdev_id) {
			activation_weight = METASLAB_WEIGHT_CLAIM;
			break;
		}
	}

	/*
	 * If we don't have enough metaslabs active to fill the entire array, we
	 * just use the 0th slot.
	 */
	if (mg->mg_ms_ready < mg->mg_allocators * 3)
		allocator = 0;

	ASSERT3U(mg->mg_vd->vdev_ms_count, >=, 2);

	metaslab_t *search = kmem_alloc(sizeof (*search), KM_SLEEP);
	search->ms_weight = UINT64_MAX;
	search->ms_start = 0;
	/*
	 * At the end of the metaslab tree are the already-active metaslabs,
	 * first the primaries, then the secondaries. When we resume searching
	 * through the tree, we need to consider ms_allocator and ms_primary so
	 * we start in the location right after where we left off, and don't
	 * accidentally loop forever considering the same metaslabs.
	 */
	search->ms_allocator = -1;
	search->ms_primary = B_TRUE;
	for (;;) {
		boolean_t was_active = B_FALSE;

		mutex_enter(&mg->mg_lock);

		if (activation_weight == METASLAB_WEIGHT_PRIMARY &&
		    mg->mg_primaries[allocator] != NULL) {
			msp = mg->mg_primaries[allocator];

			/*
			 * Even though we don't hold the ms_lock for the
			 * primary metaslab, those fields should not
			 * change while we hold the mg_lock. Thus is is
			 * safe to make assertions on them.
			 */
			ASSERT(msp->ms_primary);
			ASSERT3S(msp->ms_allocator, ==, allocator);
			ASSERT(msp->ms_loaded);

			was_active = B_TRUE;
		} else if (activation_weight == METASLAB_WEIGHT_SECONDARY &&
		    mg->mg_secondaries[allocator] != NULL) {
			msp = mg->mg_secondaries[allocator];

			/*
			 * See comment above about the similar assertions
			 * for the primary metaslab.
			 */
			ASSERT(!msp->ms_primary);
			ASSERT3S(msp->ms_allocator, ==, allocator);
			ASSERT(msp->ms_loaded);

			was_active = B_TRUE;
		} else {
			msp = find_valid_metaslab(mg, activation_weight, dva, d,
			    want_unique, asize, allocator, zal, search,
			    &was_active);
		}

		mutex_exit(&mg->mg_lock);
		if (msp == NULL) {
			kmem_free(search, sizeof (*search));
			return (-1ULL);
		}
		mutex_enter(&msp->ms_lock);

		metaslab_active_mask_verify(msp);

		/*
		 * This code is disabled out because of issues with
		 * tracepoints in non-gpl kernel modules.
		 */
#if 0
		DTRACE_PROBE3(ms__activation__attempt,
		    metaslab_t *, msp, uint64_t, activation_weight,
		    boolean_t, was_active);
#endif

		/*
		 * Ensure that the metaslab we have selected is still
		 * capable of handling our request. It's possible that
		 * another thread may have changed the weight while we
		 * were blocked on the metaslab lock. We check the
		 * active status first to see if we need to reselect
		 * a new metaslab.
		 */
		if (was_active && !(msp->ms_weight & METASLAB_ACTIVE_MASK)) {
			ASSERT3S(msp->ms_allocator, ==, -1);
			mutex_exit(&msp->ms_lock);
			continue;
		}

		/*
		 * If the metaslab was activated for another allocator
		 * while we were waiting in the ms_lock above, or it's
		 * a primary and we're seeking a secondary (or vice versa),
		 * we go back and select a new metaslab.
		 */
		if (!was_active && (msp->ms_weight & METASLAB_ACTIVE_MASK) &&
		    (msp->ms_allocator != -1) &&
		    (msp->ms_allocator != allocator || ((activation_weight ==
		    METASLAB_WEIGHT_PRIMARY) != msp->ms_primary))) {
			ASSERT(msp->ms_loaded);
			ASSERT((msp->ms_weight & METASLAB_WEIGHT_CLAIM) ||
			    msp->ms_allocator != -1);
			mutex_exit(&msp->ms_lock);
			continue;
		}

		/*
		 * This metaslab was used for claiming regions allocated
		 * by the ZIL during pool import. Once these regions are
		 * claimed we don't need to keep the CLAIM bit set
		 * anymore. Passivate this metaslab to zero its activation
		 * mask.
		 */
		if (msp->ms_weight & METASLAB_WEIGHT_CLAIM &&
		    activation_weight != METASLAB_WEIGHT_CLAIM) {
			ASSERT(msp->ms_loaded);
			ASSERT3S(msp->ms_allocator, ==, -1);
			metaslab_passivate(msp, msp->ms_weight &
			    ~METASLAB_WEIGHT_CLAIM);
			mutex_exit(&msp->ms_lock);
			continue;
		}

		msp->ms_selected_txg = txg;

		int activation_error =
		    metaslab_activate(msp, allocator, activation_weight);
		metaslab_active_mask_verify(msp);

		/*
		 * If the metaslab was activated by another thread for
		 * another allocator or activation_weight (EBUSY), or it
		 * failed because another metaslab was assigned as primary
		 * for this allocator (EEXIST) we continue using this
		 * metaslab for our allocation, rather than going on to a
		 * worse metaslab (we waited for that metaslab to be loaded
		 * after all).
		 *
		 * If the activation failed due to an I/O error or ENOSPC we
		 * skip to the next metaslab.
		 */
		boolean_t activated;
		if (activation_error == 0) {
			activated = B_TRUE;
		} else if (activation_error == EBUSY ||
		    activation_error == EEXIST) {
			activated = B_FALSE;
		} else {
			mutex_exit(&msp->ms_lock);
			continue;
		}
		ASSERT(msp->ms_loaded);

		/*
		 * Now that we have the lock, recheck to see if we should
		 * continue to use this metaslab for this allocation. The
		 * the metaslab is now loaded so metaslab_should_allocate()
		 * can accurately determine if the allocation attempt should
		 * proceed.
		 */
		if (!metaslab_should_allocate(msp, asize)) {
			/* Passivate this metaslab and select a new one. */
			metaslab_trace_add(zal, mg, msp, asize, d,
			    TRACE_TOO_SMALL, allocator);
			goto next;
		}

		/*
		 * If this metaslab is currently condensing then pick again
		 * as we can't manipulate this metaslab until it's committed
		 * to disk. If this metaslab is being initialized, we shouldn't
		 * allocate from it since the allocated region might be
		 * overwritten after allocation.
		 */
		if (msp->ms_condensing) {
			metaslab_trace_add(zal, mg, msp, asize, d,
			    TRACE_CONDENSING, allocator);
			if (activated) {
				metaslab_passivate(msp, msp->ms_weight &
				    ~METASLAB_ACTIVE_MASK);
			}
			mutex_exit(&msp->ms_lock);
			continue;
		} else if (msp->ms_disabled > 0) {
			metaslab_trace_add(zal, mg, msp, asize, d,
			    TRACE_DISABLED, allocator);
			if (activated) {
				metaslab_passivate(msp, msp->ms_weight &
				    ~METASLAB_ACTIVE_MASK);
			}
			mutex_exit(&msp->ms_lock);
			continue;
		}

		offset = metaslab_block_alloc(msp, asize, txg);
		metaslab_trace_add(zal, mg, msp, asize, d, offset, allocator);

		if (offset != -1ULL) {
			/* Proactively passivate the metaslab, if needed */
			if (activated)
				metaslab_segment_may_passivate(msp);
			break;
		}
next:
		ASSERT(msp->ms_loaded);

		/*
		 * This code is disabled out because of issues with
		 * tracepoints in non-gpl kernel modules.
		 */
#if 0
		DTRACE_PROBE2(ms__alloc__failure, metaslab_t *, msp,
		    uint64_t, asize);
#endif

		/*
		 * We were unable to allocate from this metaslab so determine
		 * a new weight for this metaslab. Now that we have loaded
		 * the metaslab we can provide a better hint to the metaslab
		 * selector.
		 *
		 * For space-based metaslabs, we use the maximum block size.
		 * This information is only available when the metaslab
		 * is loaded and is more accurate than the generic free
		 * space weight that was calculated by metaslab_weight().
		 * This information allows us to quickly compare the maximum
		 * available allocation in the metaslab to the allocation
		 * size being requested.
		 *
		 * For segment-based metaslabs, determine the new weight
		 * based on the highest bucket in the range tree. We
		 * explicitly use the loaded segment weight (i.e. the range
		 * tree histogram) since it contains the space that is
		 * currently available for allocation and is accurate
		 * even within a sync pass.
		 */
		uint64_t weight;
		if (WEIGHT_IS_SPACEBASED(msp->ms_weight)) {
			weight = metaslab_block_maxsize(msp);
			WEIGHT_SET_SPACEBASED(weight);
		} else {
			weight = metaslab_weight_from_range_tree(msp);
		}

		if (activated) {
			metaslab_passivate(msp, weight);
		} else {
			/*
			 * For the case where we use the metaslab that is
			 * active for another allocator we want to make
			 * sure that we retain the activation mask.
			 *
			 * Note that we could attempt to use something like
			 * metaslab_recalculate_weight_and_sort() that
			 * retains the activation mask here. That function
			 * uses metaslab_weight() to set the weight though
			 * which is not as accurate as the calculations
			 * above.
			 */
			weight |= msp->ms_weight & METASLAB_ACTIVE_MASK;
			metaslab_group_sort(mg, msp, weight);
		}
		metaslab_active_mask_verify(msp);

		/*
		 * We have just failed an allocation attempt, check
		 * that metaslab_should_allocate() agrees. Otherwise,
		 * we may end up in an infinite loop retrying the same
		 * metaslab.
		 */
		ASSERT(!metaslab_should_allocate(msp, asize));

		mutex_exit(&msp->ms_lock);
	}
	mutex_exit(&msp->ms_lock);
	kmem_free(search, sizeof (*search));
	return (offset);
}

static uint64_t
metaslab_group_alloc(metaslab_group_t *mg, zio_alloc_list_t *zal,
    uint64_t asize, uint64_t txg, boolean_t want_unique, dva_t *dva,
    int d, int allocator)
{
	uint64_t offset;
	ASSERT(mg->mg_initialized);

	offset = metaslab_group_alloc_normal(mg, zal, asize, txg, want_unique,
	    dva, d, allocator);

	mutex_enter(&mg->mg_lock);
	if (offset == -1ULL) {
		mg->mg_failed_allocations++;
		metaslab_trace_add(zal, mg, NULL, asize, d,
		    TRACE_GROUP_FAILURE, allocator);
		if (asize == SPA_GANGBLOCKSIZE) {
			/*
			 * This metaslab group was unable to allocate
			 * the minimum gang block size so it must be out of
			 * space. We must notify the allocation throttle
			 * to start skipping allocation attempts to this
			 * metaslab group until more space becomes available.
			 * Note: this failure cannot be caused by the
			 * allocation throttle since the allocation throttle
			 * is only responsible for skipping devices and
			 * not failing block allocations.
			 */
			mg->mg_no_free_space = B_TRUE;
		}
	}
	mg->mg_allocations++;
	mutex_exit(&mg->mg_lock);
	return (offset);
}

/*
 * Allocate a block for the specified i/o.
 */
int
metaslab_alloc_dva(spa_t *spa, metaslab_class_t *mc, uint64_t psize,
    dva_t *dva, int d, dva_t *hintdva, uint64_t txg, int flags,
    zio_alloc_list_t *zal, int allocator)
{
	metaslab_group_t *mg, *fast_mg, *rotor;
	vdev_t *vd;
	boolean_t try_hard = B_FALSE;

	ASSERT(!DVA_IS_VALID(&dva[d]));

	/*
	 * For testing, make some blocks above a certain size be gang blocks.
	 * This will result in more split blocks when using device removal,
	 * and a large number of split blocks coupled with ztest-induced
	 * damage can result in extremely long reconstruction times.  This
	 * will also test spilling from special to normal.
	 */
	if (psize >= metaslab_force_ganging && (spa_get_random(100) < 3)) {
		metaslab_trace_add(zal, NULL, NULL, psize, d, TRACE_FORCE_GANG,
		    allocator);
		return (SET_ERROR(ENOSPC));
	}

	/*
	 * Start at the rotor and loop through all mgs until we find something.
	 * Note that there's no locking on mc_rotor or mc_aliquot because
	 * nothing actually breaks if we miss a few updates -- we just won't
	 * allocate quite as evenly.  It all balances out over time.
	 *
	 * If we are doing ditto or log blocks, try to spread them across
	 * consecutive vdevs.  If we're forced to reuse a vdev before we've
	 * allocated all of our ditto blocks, then try and spread them out on
	 * that vdev as much as possible.  If it turns out to not be possible,
	 * gradually lower our standards until anything becomes acceptable.
	 * Also, allocating on consecutive vdevs (as opposed to random vdevs)
	 * gives us hope of containing our fault domains to something we're
	 * able to reason about.  Otherwise, any two top-level vdev failures
	 * will guarantee the loss of data.  With consecutive allocation,
	 * only two adjacent top-level vdev failures will result in data loss.
	 *
	 * If we are doing gang blocks (hintdva is non-NULL), try to keep
	 * ourselves on the same vdev as our gang block header.  That
	 * way, we can hope for locality in vdev_cache, plus it makes our
	 * fault domains something tractable.
	 */
	if (hintdva) {
		vd = vdev_lookup_top(spa, DVA_GET_VDEV(&hintdva[d]));

		/*
		 * It's possible the vdev we're using as the hint no
		 * longer exists or its mg has been closed (e.g. by
		 * device removal).  Consult the rotor when
		 * all else fails.
		 */
		if (vd != NULL && vd->vdev_mg != NULL) {
			mg = vd->vdev_mg;

			if (flags & METASLAB_HINTBP_AVOID &&
			    mg->mg_next != NULL)
				mg = mg->mg_next;
		} else {
			mg = mc->mc_rotor;
		}
	} else if (d != 0) {
		vd = vdev_lookup_top(spa, DVA_GET_VDEV(&dva[d - 1]));
		mg = vd->vdev_mg->mg_next;
	} else if (flags & METASLAB_FASTWRITE) {
		mg = fast_mg = mc->mc_rotor;

		do {
			if (fast_mg->mg_vd->vdev_pending_fastwrite <
			    mg->mg_vd->vdev_pending_fastwrite)
				mg = fast_mg;
		} while ((fast_mg = fast_mg->mg_next) != mc->mc_rotor);

	} else {
		ASSERT(mc->mc_rotor != NULL);
		mg = mc->mc_rotor;
	}

	/*
	 * If the hint put us into the wrong metaslab class, or into a
	 * metaslab group that has been passivated, just follow the rotor.
	 */
	if (mg->mg_class != mc || mg->mg_activation_count <= 0)
		mg = mc->mc_rotor;

	rotor = mg;
top:
	do {
		boolean_t allocatable;

		ASSERT(mg->mg_activation_count == 1);
		vd = mg->mg_vd;

		/*
		 * Don't allocate from faulted devices.
		 */
		if (try_hard) {
			spa_config_enter(spa, SCL_ZIO, FTAG, RW_READER);
			allocatable = vdev_allocatable(vd);
			spa_config_exit(spa, SCL_ZIO, FTAG);
		} else {
			allocatable = vdev_allocatable(vd);
		}

		/*
		 * Determine if the selected metaslab group is eligible
		 * for allocations. If we're ganging then don't allow
		 * this metaslab group to skip allocations since that would
		 * inadvertently return ENOSPC and suspend the pool
		 * even though space is still available.
		 */
		if (allocatable && !GANG_ALLOCATION(flags) && !try_hard) {
			allocatable = metaslab_group_allocatable(mg, rotor,
			    psize, allocator, d);
		}

		if (!allocatable) {
			metaslab_trace_add(zal, mg, NULL, psize, d,
			    TRACE_NOT_ALLOCATABLE, allocator);
			goto next;
		}

		ASSERT(mg->mg_initialized);

		/*
		 * Avoid writing single-copy data to a failing,
		 * non-redundant vdev, unless we've already tried all
		 * other vdevs.
		 */
		if ((vd->vdev_stat.vs_write_errors > 0 ||
		    vd->vdev_state < VDEV_STATE_HEALTHY) &&
		    d == 0 && !try_hard && vd->vdev_children == 0) {
			metaslab_trace_add(zal, mg, NULL, psize, d,
			    TRACE_VDEV_ERROR, allocator);
			goto next;
		}

		ASSERT(mg->mg_class == mc);

		uint64_t asize = vdev_psize_to_asize(vd, psize);
		ASSERT(P2PHASE(asize, 1ULL << vd->vdev_ashift) == 0);

		/*
		 * If we don't need to try hard, then require that the
		 * block be on an different metaslab from any other DVAs
		 * in this BP (unique=true).  If we are trying hard, then
		 * allow any metaslab to be used (unique=false).
		 */
		uint64_t offset = metaslab_group_alloc(mg, zal, asize, txg,
		    !try_hard, dva, d, allocator);

		if (offset != -1ULL) {
			/*
			 * If we've just selected this metaslab group,
			 * figure out whether the corresponding vdev is
			 * over- or under-used relative to the pool,
			 * and set an allocation bias to even it out.
			 *
			 * Bias is also used to compensate for unequally
			 * sized vdevs so that space is allocated fairly.
			 */
			if (mc->mc_aliquot == 0 && metaslab_bias_enabled) {
				vdev_stat_t *vs = &vd->vdev_stat;
				int64_t vs_free = vs->vs_space - vs->vs_alloc;
				int64_t mc_free = mc->mc_space - mc->mc_alloc;
				int64_t ratio;

				/*
				 * Calculate how much more or less we should
				 * try to allocate from this device during
				 * this iteration around the rotor.
				 *
				 * This basically introduces a zero-centered
				 * bias towards the devices with the most
				 * free space, while compensating for vdev
				 * size differences.
				 *
				 * Examples:
				 *  vdev V1 = 16M/128M
				 *  vdev V2 = 16M/128M
				 *  ratio(V1) = 100% ratio(V2) = 100%
				 *
				 *  vdev V1 = 16M/128M
				 *  vdev V2 = 64M/128M
				 *  ratio(V1) = 127% ratio(V2) =  72%
				 *
				 *  vdev V1 = 16M/128M
				 *  vdev V2 = 64M/512M
				 *  ratio(V1) =  40% ratio(V2) = 160%
				 */
				ratio = (vs_free * mc->mc_alloc_groups * 100) /
				    (mc_free + 1);
				mg->mg_bias = ((ratio - 100) *
				    (int64_t)mg->mg_aliquot) / 100;
			} else if (!metaslab_bias_enabled) {
				mg->mg_bias = 0;
			}

			if ((flags & METASLAB_FASTWRITE) ||
			    atomic_add_64_nv(&mc->mc_aliquot, asize) >=
			    mg->mg_aliquot + mg->mg_bias) {
				mc->mc_rotor = mg->mg_next;
				mc->mc_aliquot = 0;
			}

			DVA_SET_VDEV(&dva[d], vd->vdev_id);
			DVA_SET_OFFSET(&dva[d], offset);
			DVA_SET_GANG(&dva[d],
			    ((flags & METASLAB_GANG_HEADER) ? 1 : 0));
			DVA_SET_ASIZE(&dva[d], asize);

			if (flags & METASLAB_FASTWRITE) {
				atomic_add_64(&vd->vdev_pending_fastwrite,
				    psize);
			}

			return (0);
		}
next:
		mc->mc_rotor = mg->mg_next;
		mc->mc_aliquot = 0;
	} while ((mg = mg->mg_next) != rotor);

	/*
	 * If we haven't tried hard, do so now.
	 */
	if (!try_hard) {
		try_hard = B_TRUE;
		goto top;
	}

	bzero(&dva[d], sizeof (dva_t));

	metaslab_trace_add(zal, rotor, NULL, psize, d, TRACE_ENOSPC, allocator);
	return (SET_ERROR(ENOSPC));
}

void
metaslab_free_concrete(vdev_t *vd, uint64_t offset, uint64_t asize,
    boolean_t checkpoint)
{
	metaslab_t *msp;
	spa_t *spa = vd->vdev_spa;

	ASSERT(vdev_is_concrete(vd));
	ASSERT3U(spa_config_held(spa, SCL_ALL, RW_READER), !=, 0);
	ASSERT3U(offset >> vd->vdev_ms_shift, <, vd->vdev_ms_count);

	msp = vd->vdev_ms[offset >> vd->vdev_ms_shift];

	VERIFY(!msp->ms_condensing);
	VERIFY3U(offset, >=, msp->ms_start);
	VERIFY3U(offset + asize, <=, msp->ms_start + msp->ms_size);
	VERIFY0(P2PHASE(offset, 1ULL << vd->vdev_ashift));
	VERIFY0(P2PHASE(asize, 1ULL << vd->vdev_ashift));

	metaslab_check_free_impl(vd, offset, asize);

	mutex_enter(&msp->ms_lock);
	if (range_tree_is_empty(msp->ms_freeing) &&
	    range_tree_is_empty(msp->ms_checkpointing)) {
		vdev_dirty(vd, VDD_METASLAB, msp, spa_syncing_txg(spa));
	}

	if (checkpoint) {
		ASSERT(spa_has_checkpoint(spa));
		range_tree_add(msp->ms_checkpointing, offset, asize);
	} else {
		range_tree_add(msp->ms_freeing, offset, asize);
	}
	mutex_exit(&msp->ms_lock);
}

/* ARGSUSED */
void
metaslab_free_impl_cb(uint64_t inner_offset, vdev_t *vd, uint64_t offset,
    uint64_t size, void *arg)
{
	boolean_t *checkpoint = arg;

	ASSERT3P(checkpoint, !=, NULL);

	if (vd->vdev_ops->vdev_op_remap != NULL)
		vdev_indirect_mark_obsolete(vd, offset, size);
	else
		metaslab_free_impl(vd, offset, size, *checkpoint);
}

static void
metaslab_free_impl(vdev_t *vd, uint64_t offset, uint64_t size,
    boolean_t checkpoint)
{
	spa_t *spa = vd->vdev_spa;

	ASSERT3U(spa_config_held(spa, SCL_ALL, RW_READER), !=, 0);

	if (spa_syncing_txg(spa) > spa_freeze_txg(spa))
		return;

	if (spa->spa_vdev_removal != NULL &&
	    spa->spa_vdev_removal->svr_vdev_id == vd->vdev_id &&
	    vdev_is_concrete(vd)) {
		/*
		 * Note: we check if the vdev is concrete because when
		 * we complete the removal, we first change the vdev to be
		 * an indirect vdev (in open context), and then (in syncing
		 * context) clear spa_vdev_removal.
		 */
		free_from_removing_vdev(vd, offset, size);
	} else if (vd->vdev_ops->vdev_op_remap != NULL) {
		vdev_indirect_mark_obsolete(vd, offset, size);
		vd->vdev_ops->vdev_op_remap(vd, offset, size,
		    metaslab_free_impl_cb, &checkpoint);
	} else {
		metaslab_free_concrete(vd, offset, size, checkpoint);
	}
}

typedef struct remap_blkptr_cb_arg {
	blkptr_t *rbca_bp;
	spa_remap_cb_t rbca_cb;
	vdev_t *rbca_remap_vd;
	uint64_t rbca_remap_offset;
	void *rbca_cb_arg;
} remap_blkptr_cb_arg_t;

void
remap_blkptr_cb(uint64_t inner_offset, vdev_t *vd, uint64_t offset,
    uint64_t size, void *arg)
{
	remap_blkptr_cb_arg_t *rbca = arg;
	blkptr_t *bp = rbca->rbca_bp;

	/* We can not remap split blocks. */
	if (size != DVA_GET_ASIZE(&bp->blk_dva[0]))
		return;
	ASSERT0(inner_offset);

	if (rbca->rbca_cb != NULL) {
		/*
		 * At this point we know that we are not handling split
		 * blocks and we invoke the callback on the previous
		 * vdev which must be indirect.
		 */
		ASSERT3P(rbca->rbca_remap_vd->vdev_ops, ==, &vdev_indirect_ops);

		rbca->rbca_cb(rbca->rbca_remap_vd->vdev_id,
		    rbca->rbca_remap_offset, size, rbca->rbca_cb_arg);

		/* set up remap_blkptr_cb_arg for the next call */
		rbca->rbca_remap_vd = vd;
		rbca->rbca_remap_offset = offset;
	}

	/*
	 * The phys birth time is that of dva[0].  This ensures that we know
	 * when each dva was written, so that resilver can determine which
	 * blocks need to be scrubbed (i.e. those written during the time
	 * the vdev was offline).  It also ensures that the key used in
	 * the ARC hash table is unique (i.e. dva[0] + phys_birth).  If
	 * we didn't change the phys_birth, a lookup in the ARC for a
	 * remapped BP could find the data that was previously stored at
	 * this vdev + offset.
	 */
	vdev_t *oldvd = vdev_lookup_top(vd->vdev_spa,
	    DVA_GET_VDEV(&bp->blk_dva[0]));
	vdev_indirect_births_t *vib = oldvd->vdev_indirect_births;
	bp->blk_phys_birth = vdev_indirect_births_physbirth(vib,
	    DVA_GET_OFFSET(&bp->blk_dva[0]), DVA_GET_ASIZE(&bp->blk_dva[0]));

	DVA_SET_VDEV(&bp->blk_dva[0], vd->vdev_id);
	DVA_SET_OFFSET(&bp->blk_dva[0], offset);
}

/*
 * If the block pointer contains any indirect DVAs, modify them to refer to
 * concrete DVAs.  Note that this will sometimes not be possible, leaving
 * the indirect DVA in place.  This happens if the indirect DVA spans multiple
 * segments in the mapping (i.e. it is a "split block").
 *
 * If the BP was remapped, calls the callback on the original dva (note the
 * callback can be called multiple times if the original indirect DVA refers
 * to another indirect DVA, etc).
 *
 * Returns TRUE if the BP was remapped.
 */
boolean_t
spa_remap_blkptr(spa_t *spa, blkptr_t *bp, spa_remap_cb_t callback, void *arg)
{
	remap_blkptr_cb_arg_t rbca;

	if (!zfs_remap_blkptr_enable)
		return (B_FALSE);

	if (!spa_feature_is_enabled(spa, SPA_FEATURE_OBSOLETE_COUNTS))
		return (B_FALSE);

	/*
	 * Dedup BP's can not be remapped, because ddt_phys_select() depends
	 * on DVA[0] being the same in the BP as in the DDT (dedup table).
	 */
	if (BP_GET_DEDUP(bp))
		return (B_FALSE);

	/*
	 * Gang blocks can not be remapped, because
	 * zio_checksum_gang_verifier() depends on the DVA[0] that's in
	 * the BP used to read the gang block header (GBH) being the same
	 * as the DVA[0] that we allocated for the GBH.
	 */
	if (BP_IS_GANG(bp))
		return (B_FALSE);

	/*
	 * Embedded BP's have no DVA to remap.
	 */
	if (BP_GET_NDVAS(bp) < 1)
		return (B_FALSE);

	/*
	 * Note: we only remap dva[0].  If we remapped other dvas, we
	 * would no longer know what their phys birth txg is.
	 */
	dva_t *dva = &bp->blk_dva[0];

	uint64_t offset = DVA_GET_OFFSET(dva);
	uint64_t size = DVA_GET_ASIZE(dva);
	vdev_t *vd = vdev_lookup_top(spa, DVA_GET_VDEV(dva));

	if (vd->vdev_ops->vdev_op_remap == NULL)
		return (B_FALSE);

	rbca.rbca_bp = bp;
	rbca.rbca_cb = callback;
	rbca.rbca_remap_vd = vd;
	rbca.rbca_remap_offset = offset;
	rbca.rbca_cb_arg = arg;

	/*
	 * remap_blkptr_cb() will be called in order for each level of
	 * indirection, until a concrete vdev is reached or a split block is
	 * encountered. old_vd and old_offset are updated within the callback
	 * as we go from the one indirect vdev to the next one (either concrete
	 * or indirect again) in that order.
	 */
	vd->vdev_ops->vdev_op_remap(vd, offset, size, remap_blkptr_cb, &rbca);

	/* Check if the DVA wasn't remapped because it is a split block */
	if (DVA_GET_VDEV(&rbca.rbca_bp->blk_dva[0]) == vd->vdev_id)
		return (B_FALSE);

	return (B_TRUE);
}

/*
 * Undo the allocation of a DVA which happened in the given transaction group.
 */
void
metaslab_unalloc_dva(spa_t *spa, const dva_t *dva, uint64_t txg)
{
	metaslab_t *msp;
	vdev_t *vd;
	uint64_t vdev = DVA_GET_VDEV(dva);
	uint64_t offset = DVA_GET_OFFSET(dva);
	uint64_t size = DVA_GET_ASIZE(dva);

	ASSERT(DVA_IS_VALID(dva));
	ASSERT3U(spa_config_held(spa, SCL_ALL, RW_READER), !=, 0);

	if (txg > spa_freeze_txg(spa))
		return;

	if ((vd = vdev_lookup_top(spa, vdev)) == NULL || !DVA_IS_VALID(dva) ||
	    (offset >> vd->vdev_ms_shift) >= vd->vdev_ms_count) {
		zfs_panic_recover("metaslab_free_dva(): bad DVA %llu:%llu:%llu",
		    (u_longlong_t)vdev, (u_longlong_t)offset,
		    (u_longlong_t)size);
		return;
	}

	ASSERT(!vd->vdev_removing);
	ASSERT(vdev_is_concrete(vd));
	ASSERT0(vd->vdev_indirect_config.vic_mapping_object);
	ASSERT3P(vd->vdev_indirect_mapping, ==, NULL);

	if (DVA_GET_GANG(dva))
		size = vdev_psize_to_asize(vd, SPA_GANGBLOCKSIZE);

	msp = vd->vdev_ms[offset >> vd->vdev_ms_shift];

	mutex_enter(&msp->ms_lock);
	range_tree_remove(msp->ms_allocating[txg & TXG_MASK],
	    offset, size);

	VERIFY(!msp->ms_condensing);
	VERIFY3U(offset, >=, msp->ms_start);
	VERIFY3U(offset + size, <=, msp->ms_start + msp->ms_size);
	VERIFY3U(range_tree_space(msp->ms_allocatable) + size, <=,
	    msp->ms_size);
	VERIFY0(P2PHASE(offset, 1ULL << vd->vdev_ashift));
	VERIFY0(P2PHASE(size, 1ULL << vd->vdev_ashift));
	range_tree_add(msp->ms_allocatable, offset, size);
	mutex_exit(&msp->ms_lock);
}

/*
 * Free the block represented by the given DVA.
 */
void
metaslab_free_dva(spa_t *spa, const dva_t *dva, boolean_t checkpoint)
{
	uint64_t vdev = DVA_GET_VDEV(dva);
	uint64_t offset = DVA_GET_OFFSET(dva);
	uint64_t size = DVA_GET_ASIZE(dva);
	vdev_t *vd = vdev_lookup_top(spa, vdev);

	ASSERT(DVA_IS_VALID(dva));
	ASSERT3U(spa_config_held(spa, SCL_ALL, RW_READER), !=, 0);

	if (DVA_GET_GANG(dva)) {
		size = vdev_psize_to_asize(vd, SPA_GANGBLOCKSIZE);
	}

	metaslab_free_impl(vd, offset, size, checkpoint);
}

/*
 * Reserve some allocation slots. The reservation system must be called
 * before we call into the allocator. If there aren't any available slots
 * then the I/O will be throttled until an I/O completes and its slots are
 * freed up. The function returns true if it was successful in placing
 * the reservation.
 */
boolean_t
metaslab_class_throttle_reserve(metaslab_class_t *mc, int slots, int allocator,
    zio_t *zio, int flags)
{
	uint64_t available_slots = 0;
	boolean_t slot_reserved = B_FALSE;
	uint64_t max = mc->mc_alloc_max_slots[allocator];

	ASSERT(mc->mc_alloc_throttle_enabled);
	mutex_enter(&mc->mc_lock);

	uint64_t reserved_slots =
	    zfs_refcount_count(&mc->mc_alloc_slots[allocator]);
	if (reserved_slots < max)
		available_slots = max - reserved_slots;

	if (slots <= available_slots || GANG_ALLOCATION(flags) ||
	    flags & METASLAB_MUST_RESERVE) {
		/*
		 * We reserve the slots individually so that we can unreserve
		 * them individually when an I/O completes.
		 */
		for (int d = 0; d < slots; d++) {
			reserved_slots =
			    zfs_refcount_add(&mc->mc_alloc_slots[allocator],
			    zio);
		}
		zio->io_flags |= ZIO_FLAG_IO_ALLOCATING;
		slot_reserved = B_TRUE;
	}

	mutex_exit(&mc->mc_lock);
	return (slot_reserved);
}

void
metaslab_class_throttle_unreserve(metaslab_class_t *mc, int slots,
    int allocator, zio_t *zio)
{
	ASSERT(mc->mc_alloc_throttle_enabled);
	mutex_enter(&mc->mc_lock);
	for (int d = 0; d < slots; d++) {
		(void) zfs_refcount_remove(&mc->mc_alloc_slots[allocator],
		    zio);
	}
	mutex_exit(&mc->mc_lock);
}

static int
metaslab_claim_concrete(vdev_t *vd, uint64_t offset, uint64_t size,
    uint64_t txg)
{
	metaslab_t *msp;
	spa_t *spa = vd->vdev_spa;
	int error = 0;

	if (offset >> vd->vdev_ms_shift >= vd->vdev_ms_count)
		return (SET_ERROR(ENXIO));

	ASSERT3P(vd->vdev_ms, !=, NULL);
	msp = vd->vdev_ms[offset >> vd->vdev_ms_shift];

	mutex_enter(&msp->ms_lock);

	if ((txg != 0 && spa_writeable(spa)) || !msp->ms_loaded) {
		error = metaslab_activate(msp, 0, METASLAB_WEIGHT_CLAIM);
		if (error == EBUSY) {
			ASSERT(msp->ms_loaded);
			ASSERT(msp->ms_weight & METASLAB_ACTIVE_MASK);
			error = 0;
		}
	}

	if (error == 0 &&
	    !range_tree_contains(msp->ms_allocatable, offset, size))
		error = SET_ERROR(ENOENT);

	if (error || txg == 0) {	/* txg == 0 indicates dry run */
		mutex_exit(&msp->ms_lock);
		return (error);
	}

	VERIFY(!msp->ms_condensing);
	VERIFY0(P2PHASE(offset, 1ULL << vd->vdev_ashift));
	VERIFY0(P2PHASE(size, 1ULL << vd->vdev_ashift));
	VERIFY3U(range_tree_space(msp->ms_allocatable) - size, <=,
	    msp->ms_size);
	range_tree_remove(msp->ms_allocatable, offset, size);
	range_tree_clear(msp->ms_trim, offset, size);

	if (spa_writeable(spa)) {	/* don't dirty if we're zdb(1M) */
		if (range_tree_is_empty(msp->ms_allocating[txg & TXG_MASK]))
			vdev_dirty(vd, VDD_METASLAB, msp, txg);
		range_tree_add(msp->ms_allocating[txg & TXG_MASK],
		    offset, size);
	}

	mutex_exit(&msp->ms_lock);

	return (0);
}

typedef struct metaslab_claim_cb_arg_t {
	uint64_t	mcca_txg;
	int		mcca_error;
} metaslab_claim_cb_arg_t;

/* ARGSUSED */
static void
metaslab_claim_impl_cb(uint64_t inner_offset, vdev_t *vd, uint64_t offset,
    uint64_t size, void *arg)
{
	metaslab_claim_cb_arg_t *mcca_arg = arg;

	if (mcca_arg->mcca_error == 0) {
		mcca_arg->mcca_error = metaslab_claim_concrete(vd, offset,
		    size, mcca_arg->mcca_txg);
	}
}

int
metaslab_claim_impl(vdev_t *vd, uint64_t offset, uint64_t size, uint64_t txg)
{
	if (vd->vdev_ops->vdev_op_remap != NULL) {
		metaslab_claim_cb_arg_t arg;

		/*
		 * Only zdb(1M) can claim on indirect vdevs.  This is used
		 * to detect leaks of mapped space (that are not accounted
		 * for in the obsolete counts, spacemap, or bpobj).
		 */
		ASSERT(!spa_writeable(vd->vdev_spa));
		arg.mcca_error = 0;
		arg.mcca_txg = txg;

		vd->vdev_ops->vdev_op_remap(vd, offset, size,
		    metaslab_claim_impl_cb, &arg);

		if (arg.mcca_error == 0) {
			arg.mcca_error = metaslab_claim_concrete(vd,
			    offset, size, txg);
		}
		return (arg.mcca_error);
	} else {
		return (metaslab_claim_concrete(vd, offset, size, txg));
	}
}

/*
 * Intent log support: upon opening the pool after a crash, notify the SPA
 * of blocks that the intent log has allocated for immediate write, but
 * which are still considered free by the SPA because the last transaction
 * group didn't commit yet.
 */
static int
metaslab_claim_dva(spa_t *spa, const dva_t *dva, uint64_t txg)
{
	uint64_t vdev = DVA_GET_VDEV(dva);
	uint64_t offset = DVA_GET_OFFSET(dva);
	uint64_t size = DVA_GET_ASIZE(dva);
	vdev_t *vd;

	if ((vd = vdev_lookup_top(spa, vdev)) == NULL) {
		return (SET_ERROR(ENXIO));
	}

	ASSERT(DVA_IS_VALID(dva));

	if (DVA_GET_GANG(dva))
		size = vdev_psize_to_asize(vd, SPA_GANGBLOCKSIZE);

	return (metaslab_claim_impl(vd, offset, size, txg));
}

int
metaslab_alloc(spa_t *spa, metaslab_class_t *mc, uint64_t psize, blkptr_t *bp,
    int ndvas, uint64_t txg, blkptr_t *hintbp, int flags,
    zio_alloc_list_t *zal, zio_t *zio, int allocator)
{
	dva_t *dva = bp->blk_dva;
	dva_t *hintdva = (hintbp != NULL) ? hintbp->blk_dva : NULL;
	int error = 0;

	ASSERT(bp->blk_birth == 0);
	ASSERT(BP_PHYSICAL_BIRTH(bp) == 0);

	spa_config_enter(spa, SCL_ALLOC, FTAG, RW_READER);

	if (mc->mc_rotor == NULL) {	/* no vdevs in this class */
		spa_config_exit(spa, SCL_ALLOC, FTAG);
		return (SET_ERROR(ENOSPC));
	}

	ASSERT(ndvas > 0 && ndvas <= spa_max_replication(spa));
	ASSERT(BP_GET_NDVAS(bp) == 0);
	ASSERT(hintbp == NULL || ndvas <= BP_GET_NDVAS(hintbp));
	ASSERT3P(zal, !=, NULL);

	for (int d = 0; d < ndvas; d++) {
		error = metaslab_alloc_dva(spa, mc, psize, dva, d, hintdva,
		    txg, flags, zal, allocator);
		if (error != 0) {
			for (d--; d >= 0; d--) {
				metaslab_unalloc_dva(spa, &dva[d], txg);
				metaslab_group_alloc_decrement(spa,
				    DVA_GET_VDEV(&dva[d]), zio, flags,
				    allocator, B_FALSE);
				bzero(&dva[d], sizeof (dva_t));
			}
			spa_config_exit(spa, SCL_ALLOC, FTAG);
			return (error);
		} else {
			/*
			 * Update the metaslab group's queue depth
			 * based on the newly allocated dva.
			 */
			metaslab_group_alloc_increment(spa,
			    DVA_GET_VDEV(&dva[d]), zio, flags, allocator);
		}

	}
	ASSERT(error == 0);
	ASSERT(BP_GET_NDVAS(bp) == ndvas);

	spa_config_exit(spa, SCL_ALLOC, FTAG);

	BP_SET_BIRTH(bp, txg, 0);

	return (0);
}

void
metaslab_free(spa_t *spa, const blkptr_t *bp, uint64_t txg, boolean_t now)
{
	const dva_t *dva = bp->blk_dva;
	int ndvas = BP_GET_NDVAS(bp);

	ASSERT(!BP_IS_HOLE(bp));
	ASSERT(!now || bp->blk_birth >= spa_syncing_txg(spa));

	/*
	 * If we have a checkpoint for the pool we need to make sure that
	 * the blocks that we free that are part of the checkpoint won't be
	 * reused until the checkpoint is discarded or we revert to it.
	 *
	 * The checkpoint flag is passed down the metaslab_free code path
	 * and is set whenever we want to add a block to the checkpoint's
	 * accounting. That is, we "checkpoint" blocks that existed at the
	 * time the checkpoint was created and are therefore referenced by
	 * the checkpointed uberblock.
	 *
	 * Note that, we don't checkpoint any blocks if the current
	 * syncing txg <= spa_checkpoint_txg. We want these frees to sync
	 * normally as they will be referenced by the checkpointed uberblock.
	 */
	boolean_t checkpoint = B_FALSE;
	if (bp->blk_birth <= spa->spa_checkpoint_txg &&
	    spa_syncing_txg(spa) > spa->spa_checkpoint_txg) {
		/*
		 * At this point, if the block is part of the checkpoint
		 * there is no way it was created in the current txg.
		 */
		ASSERT(!now);
		ASSERT3U(spa_syncing_txg(spa), ==, txg);
		checkpoint = B_TRUE;
	}

	spa_config_enter(spa, SCL_FREE, FTAG, RW_READER);

	for (int d = 0; d < ndvas; d++) {
		if (now) {
			metaslab_unalloc_dva(spa, &dva[d], txg);
		} else {
			ASSERT3U(txg, ==, spa_syncing_txg(spa));
			metaslab_free_dva(spa, &dva[d], checkpoint);
		}
	}

	spa_config_exit(spa, SCL_FREE, FTAG);
}

int
metaslab_claim(spa_t *spa, const blkptr_t *bp, uint64_t txg)
{
	const dva_t *dva = bp->blk_dva;
	int ndvas = BP_GET_NDVAS(bp);
	int error = 0;

	ASSERT(!BP_IS_HOLE(bp));

	if (txg != 0) {
		/*
		 * First do a dry run to make sure all DVAs are claimable,
		 * so we don't have to unwind from partial failures below.
		 */
		if ((error = metaslab_claim(spa, bp, 0)) != 0)
			return (error);
	}

	spa_config_enter(spa, SCL_ALLOC, FTAG, RW_READER);

	for (int d = 0; d < ndvas; d++) {
		error = metaslab_claim_dva(spa, &dva[d], txg);
		if (error != 0)
			break;
	}

	spa_config_exit(spa, SCL_ALLOC, FTAG);

	ASSERT(error == 0 || txg == 0);

	return (error);
}

void
metaslab_fastwrite_mark(spa_t *spa, const blkptr_t *bp)
{
	const dva_t *dva = bp->blk_dva;
	int ndvas = BP_GET_NDVAS(bp);
	uint64_t psize = BP_GET_PSIZE(bp);
	int d;
	vdev_t *vd;

	ASSERT(!BP_IS_HOLE(bp));
	ASSERT(!BP_IS_EMBEDDED(bp));
	ASSERT(psize > 0);

	spa_config_enter(spa, SCL_VDEV, FTAG, RW_READER);

	for (d = 0; d < ndvas; d++) {
		if ((vd = vdev_lookup_top(spa, DVA_GET_VDEV(&dva[d]))) == NULL)
			continue;
		atomic_add_64(&vd->vdev_pending_fastwrite, psize);
	}

	spa_config_exit(spa, SCL_VDEV, FTAG);
}

void
metaslab_fastwrite_unmark(spa_t *spa, const blkptr_t *bp)
{
	const dva_t *dva = bp->blk_dva;
	int ndvas = BP_GET_NDVAS(bp);
	uint64_t psize = BP_GET_PSIZE(bp);
	int d;
	vdev_t *vd;

	ASSERT(!BP_IS_HOLE(bp));
	ASSERT(!BP_IS_EMBEDDED(bp));
	ASSERT(psize > 0);

	spa_config_enter(spa, SCL_VDEV, FTAG, RW_READER);

	for (d = 0; d < ndvas; d++) {
		if ((vd = vdev_lookup_top(spa, DVA_GET_VDEV(&dva[d]))) == NULL)
			continue;
		ASSERT3U(vd->vdev_pending_fastwrite, >=, psize);
		atomic_sub_64(&vd->vdev_pending_fastwrite, psize);
	}

	spa_config_exit(spa, SCL_VDEV, FTAG);
}

/* ARGSUSED */
static void
metaslab_check_free_impl_cb(uint64_t inner, vdev_t *vd, uint64_t offset,
    uint64_t size, void *arg)
{
	if (vd->vdev_ops == &vdev_indirect_ops)
		return;

	metaslab_check_free_impl(vd, offset, size);
}

static void
metaslab_check_free_impl(vdev_t *vd, uint64_t offset, uint64_t size)
{
	metaslab_t *msp;
	ASSERTV(spa_t *spa = vd->vdev_spa);

	if ((zfs_flags & ZFS_DEBUG_ZIO_FREE) == 0)
		return;

	if (vd->vdev_ops->vdev_op_remap != NULL) {
		vd->vdev_ops->vdev_op_remap(vd, offset, size,
		    metaslab_check_free_impl_cb, NULL);
		return;
	}

	ASSERT(vdev_is_concrete(vd));
	ASSERT3U(offset >> vd->vdev_ms_shift, <, vd->vdev_ms_count);
	ASSERT3U(spa_config_held(spa, SCL_ALL, RW_READER), !=, 0);

	msp = vd->vdev_ms[offset >> vd->vdev_ms_shift];

	mutex_enter(&msp->ms_lock);
	if (msp->ms_loaded) {
		range_tree_verify_not_present(msp->ms_allocatable,
		    offset, size);
	}

	range_tree_verify_not_present(msp->ms_trim, offset, size);
	range_tree_verify_not_present(msp->ms_freeing, offset, size);
	range_tree_verify_not_present(msp->ms_checkpointing, offset, size);
	range_tree_verify_not_present(msp->ms_freed, offset, size);
	for (int j = 0; j < TXG_DEFER_SIZE; j++)
		range_tree_verify_not_present(msp->ms_defer[j], offset, size);
	mutex_exit(&msp->ms_lock);
}

void
metaslab_check_free(spa_t *spa, const blkptr_t *bp)
{
	if ((zfs_flags & ZFS_DEBUG_ZIO_FREE) == 0)
		return;

	spa_config_enter(spa, SCL_VDEV, FTAG, RW_READER);
	for (int i = 0; i < BP_GET_NDVAS(bp); i++) {
		uint64_t vdev = DVA_GET_VDEV(&bp->blk_dva[i]);
		vdev_t *vd = vdev_lookup_top(spa, vdev);
		uint64_t offset = DVA_GET_OFFSET(&bp->blk_dva[i]);
		uint64_t size = DVA_GET_ASIZE(&bp->blk_dva[i]);

		if (DVA_GET_GANG(&bp->blk_dva[i]))
			size = vdev_psize_to_asize(vd, SPA_GANGBLOCKSIZE);

		ASSERT3P(vd, !=, NULL);

		metaslab_check_free_impl(vd, offset, size);
	}
	spa_config_exit(spa, SCL_VDEV, FTAG);
}

static void
metaslab_group_disable_wait(metaslab_group_t *mg)
{
	ASSERT(MUTEX_HELD(&mg->mg_ms_disabled_lock));
	while (mg->mg_disabled_updating) {
		cv_wait(&mg->mg_ms_disabled_cv, &mg->mg_ms_disabled_lock);
	}
}

static void
metaslab_group_disabled_increment(metaslab_group_t *mg)
{
	ASSERT(MUTEX_HELD(&mg->mg_ms_disabled_lock));
	ASSERT(mg->mg_disabled_updating);

	while (mg->mg_ms_disabled >= max_disabled_ms) {
		cv_wait(&mg->mg_ms_disabled_cv, &mg->mg_ms_disabled_lock);
	}
	mg->mg_ms_disabled++;
	ASSERT3U(mg->mg_ms_disabled, <=, max_disabled_ms);
}

/*
 * Mark the metaslab as disabled to prevent any allocations on this metaslab.
 * We must also track how many metaslabs are currently disabled within a
 * metaslab group and limit them to prevent allocation failures from
 * occurring because all metaslabs are disabled.
 */
void
metaslab_disable(metaslab_t *msp)
{
	ASSERT(!MUTEX_HELD(&msp->ms_lock));
	metaslab_group_t *mg = msp->ms_group;

	mutex_enter(&mg->mg_ms_disabled_lock);

	/*
	 * To keep an accurate count of how many threads have disabled
	 * a specific metaslab group, we only allow one thread to mark
	 * the metaslab group at a time. This ensures that the value of
	 * ms_disabled will be accurate when we decide to mark a metaslab
	 * group as disabled. To do this we force all other threads
	 * to wait till the metaslab's mg_disabled_updating flag is no
	 * longer set.
	 */
	metaslab_group_disable_wait(mg);
	mg->mg_disabled_updating = B_TRUE;
	if (msp->ms_disabled == 0) {
		metaslab_group_disabled_increment(mg);
	}
	mutex_enter(&msp->ms_lock);
	msp->ms_disabled++;
	mutex_exit(&msp->ms_lock);

	mg->mg_disabled_updating = B_FALSE;
	cv_broadcast(&mg->mg_ms_disabled_cv);
	mutex_exit(&mg->mg_ms_disabled_lock);
}

void
metaslab_enable(metaslab_t *msp, boolean_t sync)
{
	metaslab_group_t *mg = msp->ms_group;
	spa_t *spa = mg->mg_vd->vdev_spa;

	/*
	 * Wait for the outstanding IO to be synced to prevent newly
	 * allocated blocks from being overwritten.  This used by
	 * initialize and TRIM which are modifying unallocated space.
	 */
	if (sync)
		txg_wait_synced(spa_get_dsl(spa), 0);

	mutex_enter(&mg->mg_ms_disabled_lock);
	mutex_enter(&msp->ms_lock);
	if (--msp->ms_disabled == 0) {
		mg->mg_ms_disabled--;
		cv_broadcast(&mg->mg_ms_disabled_cv);
	}
	mutex_exit(&msp->ms_lock);
	mutex_exit(&mg->mg_ms_disabled_lock);
}

#if defined(_KERNEL)
/* BEGIN CSTYLED */
module_param(metaslab_aliquot, ulong, 0644);
MODULE_PARM_DESC(metaslab_aliquot,
	"allocation granularity (a.k.a. stripe size)");

module_param(metaslab_debug_load, int, 0644);
MODULE_PARM_DESC(metaslab_debug_load,
	"load all metaslabs when pool is first opened");

module_param(metaslab_debug_unload, int, 0644);
MODULE_PARM_DESC(metaslab_debug_unload,
	"prevent metaslabs from being unloaded");

module_param(metaslab_preload_enabled, int, 0644);
MODULE_PARM_DESC(metaslab_preload_enabled,
	"preload potential metaslabs during reassessment");

module_param(zfs_mg_noalloc_threshold, int, 0644);
MODULE_PARM_DESC(zfs_mg_noalloc_threshold,
	"percentage of free space for metaslab group to allow allocation");

module_param(zfs_mg_fragmentation_threshold, int, 0644);
MODULE_PARM_DESC(zfs_mg_fragmentation_threshold,
	"fragmentation for metaslab group to allow allocation");

module_param(zfs_metaslab_fragmentation_threshold, int, 0644);
MODULE_PARM_DESC(zfs_metaslab_fragmentation_threshold,
	"fragmentation for metaslab to allow allocation");

module_param(metaslab_fragmentation_factor_enabled, int, 0644);
MODULE_PARM_DESC(metaslab_fragmentation_factor_enabled,
	"use the fragmentation metric to prefer less fragmented metaslabs");

module_param(metaslab_lba_weighting_enabled, int, 0644);
MODULE_PARM_DESC(metaslab_lba_weighting_enabled,
	"prefer metaslabs with lower LBAs");

module_param(metaslab_bias_enabled, int, 0644);
MODULE_PARM_DESC(metaslab_bias_enabled,
	"enable metaslab group biasing");

module_param(zfs_metaslab_segment_weight_enabled, int, 0644);
MODULE_PARM_DESC(zfs_metaslab_segment_weight_enabled,
	"enable segment-based metaslab selection");

module_param(zfs_metaslab_switch_threshold, int, 0644);
MODULE_PARM_DESC(zfs_metaslab_switch_threshold,
	"segment-based metaslab selection maximum buckets before switching");

module_param(metaslab_force_ganging, ulong, 0644);
MODULE_PARM_DESC(metaslab_force_ganging,
	"blocks larger than this size are forced to be gang blocks");

module_param(metaslab_df_max_search, int, 0644);
MODULE_PARM_DESC(metaslab_df_max_search,
	"max distance (bytes) to search forward before using size tree");

module_param(metaslab_df_use_largest_segment, int, 0644);
MODULE_PARM_DESC(metaslab_df_use_largest_segment,
	"when looking in size tree, use largest segment instead of exact fit");
/* END CSTYLED */

#endif
