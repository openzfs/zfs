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

/*
 * Copyright (c) 2011, 2019 by Delphix. All rights reserved.
 */

#ifndef _SYS_METASLAB_IMPL_H
#define	_SYS_METASLAB_IMPL_H

#include <sys/metaslab.h>
#include <sys/space_map.h>
#include <sys/range_tree.h>
#include <sys/vdev.h>
#include <sys/txg.h>
#include <sys/avl.h>
#include <sys/multilist.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Metaslab allocation tracing record.
 */
typedef struct metaslab_alloc_trace {
	list_node_t			mat_list_node;
	metaslab_group_t		*mat_mg;
	metaslab_t			*mat_msp;
	uint64_t			mat_size;
	uint64_t			mat_weight;
	uint32_t			mat_dva_id;
	uint64_t			mat_offset;
	int					mat_allocator;
} metaslab_alloc_trace_t;

/*
 * Used by the metaslab allocation tracing facility to indicate
 * error conditions. These errors are stored to the offset member
 * of the metaslab_alloc_trace_t record and displayed by mdb.
 */
typedef enum trace_alloc_type {
	TRACE_ALLOC_FAILURE	= -1ULL,
	TRACE_TOO_SMALL		= -2ULL,
	TRACE_FORCE_GANG	= -3ULL,
	TRACE_NOT_ALLOCATABLE	= -4ULL,
	TRACE_GROUP_FAILURE	= -5ULL,
	TRACE_ENOSPC		= -6ULL,
	TRACE_CONDENSING	= -7ULL,
	TRACE_VDEV_ERROR	= -8ULL,
	TRACE_DISABLED		= -9ULL,
} trace_alloc_type_t;

#define	METASLAB_WEIGHT_PRIMARY		(1ULL << 63)
#define	METASLAB_WEIGHT_SECONDARY	(1ULL << 62)
#define	METASLAB_WEIGHT_CLAIM		(1ULL << 61)
#define	METASLAB_WEIGHT_TYPE		(1ULL << 60)
#define	METASLAB_ACTIVE_MASK		\
	(METASLAB_WEIGHT_PRIMARY | METASLAB_WEIGHT_SECONDARY | \
	METASLAB_WEIGHT_CLAIM)

/*
 * The metaslab weight is used to encode the amount of free space in a
 * metaslab, such that the "best" metaslab appears first when sorting the
 * metaslabs by weight. The weight (and therefore the "best" metaslab) can
 * be determined in two different ways: by computing a weighted sum of all
 * the free space in the metaslab (a space based weight) or by counting only
 * the free segments of the largest size (a segment based weight). We prefer
 * the segment based weight because it reflects how the free space is
 * comprised, but we cannot always use it -- legacy pools do not have the
 * space map histogram information necessary to determine the largest
 * contiguous regions. Pools that have the space map histogram determine
 * the segment weight by looking at each bucket in the histogram and
 * determining the free space whose size in bytes is in the range:
 *	[2^i, 2^(i+1))
 * We then encode the largest index, i, that contains regions into the
 * segment-weighted value.
 *
 * Space-based weight:
 *
 *      64      56      48      40      32      24      16      8       0
 *      +-------+-------+-------+-------+-------+-------+-------+-------+
 *      |PSC1|                  weighted-free space                     |
 *      +-------+-------+-------+-------+-------+-------+-------+-------+
 *
 *	PS - indicates primary and secondary activation
 *	C - indicates activation for claimed block zio
 *	space - the fragmentation-weighted space
 *
 * Segment-based weight:
 *
 *      64      56      48      40      32      24      16      8       0
 *      +-------+-------+-------+-------+-------+-------+-------+-------+
 *      |PSC0| idx|            count of segments in region              |
 *      +-------+-------+-------+-------+-------+-------+-------+-------+
 *
 *	PS - indicates primary and secondary activation
 *	C - indicates activation for claimed block zio
 *	idx - index for the highest bucket in the histogram
 *	count - number of segments in the specified bucket
 */
#define	WEIGHT_GET_ACTIVE(weight)		BF64_GET((weight), 61, 3)
#define	WEIGHT_SET_ACTIVE(weight, x)		BF64_SET((weight), 61, 3, x)

#define	WEIGHT_IS_SPACEBASED(weight)		\
	((weight) == 0 || BF64_GET((weight), 60, 1))
#define	WEIGHT_SET_SPACEBASED(weight)		BF64_SET((weight), 60, 1, 1)

/*
 * These macros are only applicable to segment-based weighting.
 */
#define	WEIGHT_GET_INDEX(weight)		BF64_GET((weight), 54, 6)
#define	WEIGHT_SET_INDEX(weight, x)		BF64_SET((weight), 54, 6, x)
#define	WEIGHT_GET_COUNT(weight)		BF64_GET((weight), 0, 54)
#define	WEIGHT_SET_COUNT(weight, x)		BF64_SET((weight), 0, 54, x)

/*
 * Per-allocator data structure.
 */
typedef struct metaslab_class_allocator {
	metaslab_group_t	*mca_rotor;
	uint64_t		mca_aliquot;

	/*
	 * The allocation throttle works on a reservation system. Whenever
	 * an asynchronous zio wants to perform an allocation it must
	 * first reserve the number of blocks that it wants to allocate.
	 * If there aren't sufficient slots available for the pending zio
	 * then that I/O is throttled until more slots free up. The current
	 * number of reserved allocations is maintained by the mca_alloc_slots
	 * refcount. The mca_alloc_max_slots value determines the maximum
	 * number of allocations that the system allows. Gang blocks are
	 * allowed to reserve slots even if we've reached the maximum
	 * number of allocations allowed.
	 */
	uint64_t		mca_alloc_max_slots;
	zfs_refcount_t		mca_alloc_slots;
} metaslab_class_allocator_t;

/*
 * A metaslab class encompasses a category of allocatable top-level vdevs.
 * Each top-level vdev is associated with a metaslab group which defines
 * the allocatable region for that vdev. Examples of these categories include
 * "normal" for data block allocations (i.e. main pool allocations) or "log"
 * for allocations designated for intent log devices (i.e. slog devices).
 * When a block allocation is requested from the SPA it is associated with a
 * metaslab_class_t, and only top-level vdevs (i.e. metaslab groups) belonging
 * to the class can be used to satisfy that request. Allocations are done
 * by traversing the metaslab groups that are linked off of the mca_rotor field.
 * This rotor points to the next metaslab group where allocations will be
 * attempted. Allocating a block is a 3 step process -- select the metaslab
 * group, select the metaslab, and then allocate the block. The metaslab
 * class defines the low-level block allocator that will be used as the
 * final step in allocation. These allocators are pluggable allowing each class
 * to use a block allocator that best suits that class.
 */
struct metaslab_class {
	kmutex_t		mc_lock;
	spa_t			*mc_spa;
	metaslab_ops_t		*mc_ops;

	/*
	 * Track the number of metaslab groups that have been initialized
	 * and can accept allocations. An initialized metaslab group is
	 * one has been completely added to the config (i.e. we have
	 * updated the MOS config and the space has been added to the pool).
	 */
	uint64_t		mc_groups;

	/*
	 * Toggle to enable/disable the allocation throttle.
	 */
	boolean_t		mc_alloc_throttle_enabled;

	uint64_t		mc_alloc_groups; /* # of allocatable groups */

	uint64_t		mc_alloc;	/* total allocated space */
	uint64_t		mc_deferred;	/* total deferred frees */
	uint64_t		mc_space;	/* total space (alloc + free) */
	uint64_t		mc_dspace;	/* total deflated space */
	uint64_t		mc_histogram[RANGE_TREE_HISTOGRAM_SIZE];

	/*
	 * List of all loaded metaslabs in the class, sorted in order of most
	 * recent use.
	 */
	multilist_t		*mc_metaslab_txg_list;

	metaslab_class_allocator_t	mc_allocator[];
};

/*
 * Per-allocator data structure.
 */
typedef struct metaslab_group_allocator {
	uint64_t	mga_cur_max_alloc_queue_depth;
	zfs_refcount_t	mga_alloc_queue_depth;
	metaslab_t	*mga_primary;
	metaslab_t	*mga_secondary;
} metaslab_group_allocator_t;

/*
 * Metaslab groups encapsulate all the allocatable regions (i.e. metaslabs)
 * of a top-level vdev. They are linked together to form a circular linked
 * list and can belong to only one metaslab class. Metaslab groups may become
 * ineligible for allocations for a number of reasons such as limited free
 * space, fragmentation, or going offline. When this happens the allocator will
 * simply find the next metaslab group in the linked list and attempt
 * to allocate from that group instead.
 */
struct metaslab_group {
	kmutex_t		mg_lock;
	avl_tree_t		mg_metaslab_tree;
	uint64_t		mg_aliquot;
	boolean_t		mg_allocatable;		/* can we allocate? */
	uint64_t		mg_ms_ready;

	/*
	 * A metaslab group is considered to be initialized only after
	 * we have updated the MOS config and added the space to the pool.
	 * We only allow allocation attempts to a metaslab group if it
	 * has been initialized.
	 */
	boolean_t		mg_initialized;

	uint64_t		mg_free_capacity;	/* percentage free */
	int64_t			mg_bias;
	int64_t			mg_activation_count;
	metaslab_class_t	*mg_class;
	vdev_t			*mg_vd;
	taskq_t			*mg_taskq;
	metaslab_group_t	*mg_prev;
	metaslab_group_t	*mg_next;

	/*
	 * In order for the allocation throttle to function properly, we cannot
	 * have too many IOs going to each disk by default; the throttle
	 * operates by allocating more work to disks that finish quickly, so
	 * allocating larger chunks to each disk reduces its effectiveness.
	 * However, if the number of IOs going to each allocator is too small,
	 * we will not perform proper aggregation at the vdev_queue layer,
	 * also resulting in decreased performance. Therefore, we will use a
	 * ramp-up strategy.
	 *
	 * Each allocator in each metaslab group has a current queue depth
	 * (mg_alloc_queue_depth[allocator]) and a current max queue depth
	 * (mga_cur_max_alloc_queue_depth[allocator]), and each metaslab group
	 * has an absolute max queue depth (mg_max_alloc_queue_depth).  We
	 * add IOs to an allocator until the mg_alloc_queue_depth for that
	 * allocator hits the cur_max. Every time an IO completes for a given
	 * allocator on a given metaslab group, we increment its cur_max until
	 * it reaches mg_max_alloc_queue_depth. The cur_max resets every txg to
	 * help protect against disks that decrease in performance over time.
	 *
	 * It's possible for an allocator to handle more allocations than
	 * its max. This can occur when gang blocks are required or when other
	 * groups are unable to handle their share of allocations.
	 */
	uint64_t		mg_max_alloc_queue_depth;

	/*
	 * A metalab group that can no longer allocate the minimum block
	 * size will set mg_no_free_space. Once a metaslab group is out
	 * of space then its share of work must be distributed to other
	 * groups.
	 */
	boolean_t		mg_no_free_space;

	uint64_t		mg_allocations;
	uint64_t		mg_failed_allocations;
	uint64_t		mg_fragmentation;
	uint64_t		mg_histogram[RANGE_TREE_HISTOGRAM_SIZE];

	int			mg_ms_disabled;
	boolean_t		mg_disabled_updating;
	kmutex_t		mg_ms_disabled_lock;
	kcondvar_t		mg_ms_disabled_cv;

	int			mg_allocators;
	metaslab_group_allocator_t	mg_allocator[];
};

/*
 * This value defines the number of elements in the ms_lbas array. The value
 * of 64 was chosen as it covers all power of 2 buckets up to UINT64_MAX.
 * This is the equivalent of highbit(UINT64_MAX).
 */
#define	MAX_LBAS	64

/*
 * Each metaslab maintains a set of in-core trees to track metaslab
 * operations.  The in-core free tree (ms_allocatable) contains the list of
 * free segments which are eligible for allocation.  As blocks are
 * allocated, the allocated segment are removed from the ms_allocatable and
 * added to a per txg allocation tree (ms_allocating).  As blocks are
 * freed, they are added to the free tree (ms_freeing).  These trees
 * allow us to process all allocations and frees in syncing context
 * where it is safe to update the on-disk space maps.  An additional set
 * of in-core trees is maintained to track deferred frees
 * (ms_defer).  Once a block is freed it will move from the
 * ms_freed to the ms_defer tree.  A deferred free means that a block
 * has been freed but cannot be used by the pool until TXG_DEFER_SIZE
 * transactions groups later.  For example, a block that is freed in txg
 * 50 will not be available for reallocation until txg 52 (50 +
 * TXG_DEFER_SIZE).  This provides a safety net for uberblock rollback.
 * A pool could be safely rolled back TXG_DEFERS_SIZE transactions
 * groups and ensure that no block has been reallocated.
 *
 * The simplified transition diagram looks like this:
 *
 *
 *      ALLOCATE
 *         |
 *         V
 *    free segment (ms_allocatable) -> ms_allocating[4] -> (write to space map)
 *         ^
 *         |                        ms_freeing <--- FREE
 *         |                             |
 *         |                             v
 *         |                         ms_freed
 *         |                             |
 *         +-------- ms_defer[2] <-------+-------> (write to space map)
 *
 *
 * Each metaslab's space is tracked in a single space map in the MOS,
 * which is only updated in syncing context.  Each time we sync a txg,
 * we append the allocs and frees from that txg to the space map.  The
 * pool space is only updated once all metaslabs have finished syncing.
 *
 * To load the in-core free tree we read the space map from disk.  This
 * object contains a series of alloc and free records that are combined
 * to make up the list of all free segments in this metaslab.  These
 * segments are represented in-core by the ms_allocatable and are stored
 * in an AVL tree.
 *
 * As the space map grows (as a result of the appends) it will
 * eventually become space-inefficient.  When the metaslab's in-core
 * free tree is zfs_condense_pct/100 times the size of the minimal
 * on-disk representation, we rewrite it in its minimized form.  If a
 * metaslab needs to condense then we must set the ms_condensing flag to
 * ensure that allocations are not performed on the metaslab that is
 * being written.
 */
struct metaslab {
	/*
	 * This is the main lock of the metaslab and its purpose is to
	 * coordinate our allocations and frees [e.g metaslab_block_alloc(),
	 * metaslab_free_concrete(), ..etc] with our various syncing
	 * procedures [e.g. metaslab_sync(), metaslab_sync_done(), ..etc].
	 *
	 * The lock is also used during some miscellaneous operations like
	 * using the metaslab's histogram for the metaslab group's histogram
	 * aggregation, or marking the metaslab for initialization.
	 */
	kmutex_t	ms_lock;

	/*
	 * Acquired together with the ms_lock whenever we expect to
	 * write to metaslab data on-disk (i.e flushing entries to
	 * the metaslab's space map). It helps coordinate readers of
	 * the metaslab's space map [see spa_vdev_remove_thread()]
	 * with writers [see metaslab_sync() or metaslab_flush()].
	 *
	 * Note that metaslab_load(), even though a reader, uses
	 * a completely different mechanism to deal with the reading
	 * of the metaslab's space map based on ms_synced_length. That
	 * said, the function still uses the ms_sync_lock after it
	 * has read the ms_sm [see relevant comment in metaslab_load()
	 * as to why].
	 */
	kmutex_t	ms_sync_lock;

	kcondvar_t	ms_load_cv;
	space_map_t	*ms_sm;
	uint64_t	ms_id;
	uint64_t	ms_start;
	uint64_t	ms_size;
	uint64_t	ms_fragmentation;

	range_tree_t	*ms_allocating[TXG_SIZE];
	range_tree_t	*ms_allocatable;
	uint64_t	ms_allocated_this_txg;
	uint64_t	ms_allocating_total;

	/*
	 * The following range trees are accessed only from syncing context.
	 * ms_free*tree only have entries while syncing, and are empty
	 * between syncs.
	 */
	range_tree_t	*ms_freeing;	/* to free this syncing txg */
	range_tree_t	*ms_freed;	/* already freed this syncing txg */
	range_tree_t	*ms_defer[TXG_DEFER_SIZE];
	range_tree_t	*ms_checkpointing; /* to add to the checkpoint */

	/*
	 * The ms_trim tree is the set of allocatable segments which are
	 * eligible for trimming. (When the metaslab is loaded, it's a
	 * subset of ms_allocatable.)  It's kept in-core as long as the
	 * autotrim property is set and is not vacated when the metaslab
	 * is unloaded.  Its purpose is to aggregate freed ranges to
	 * facilitate efficient trimming.
	 */
	range_tree_t	*ms_trim;

	boolean_t	ms_condensing;	/* condensing? */
	boolean_t	ms_condense_wanted;

	/*
	 * The number of consumers which have disabled the metaslab.
	 */
	uint64_t	ms_disabled;

	/*
	 * We must always hold the ms_lock when modifying ms_loaded
	 * and ms_loading.
	 */
	boolean_t	ms_loaded;
	boolean_t	ms_loading;
	kcondvar_t	ms_flush_cv;
	boolean_t	ms_flushing;

	/*
	 * The following histograms count entries that are in the
	 * metaslab's space map (and its histogram) but are not in
	 * ms_allocatable yet, because they are in ms_freed, ms_freeing,
	 * or ms_defer[].
	 *
	 * When the metaslab is not loaded, its ms_weight needs to
	 * reflect what is allocatable (i.e. what will be part of
	 * ms_allocatable if it is loaded).  The weight is computed from
	 * the spacemap histogram, but that includes ranges that are
	 * not yet allocatable (because they are in ms_freed,
	 * ms_freeing, or ms_defer[]).  Therefore, when calculating the
	 * weight, we need to remove those ranges.
	 *
	 * The ranges in the ms_freed and ms_defer[] range trees are all
	 * present in the spacemap.  However, the spacemap may have
	 * multiple entries to represent a contiguous range, because it
	 * is written across multiple sync passes, but the changes of
	 * all sync passes are consolidated into the range trees.
	 * Adjacent ranges that are freed in different sync passes of
	 * one txg will be represented separately (as 2 or more entries)
	 * in the space map (and its histogram), but these adjacent
	 * ranges will be consolidated (represented as one entry) in the
	 * ms_freed/ms_defer[] range trees (and their histograms).
	 *
	 * When calculating the weight, we can not simply subtract the
	 * range trees' histograms from the spacemap's histogram,
	 * because the range trees' histograms may have entries in
	 * higher buckets than the spacemap, due to consolidation.
	 * Instead we must subtract the exact entries that were added to
	 * the spacemap's histogram.  ms_synchist and ms_deferhist[]
	 * represent these exact entries, so we can subtract them from
	 * the spacemap's histogram when calculating ms_weight.
	 *
	 * ms_synchist represents the same ranges as ms_freeing +
	 * ms_freed, but without consolidation across sync passes.
	 *
	 * ms_deferhist[i] represents the same ranges as ms_defer[i],
	 * but without consolidation across sync passes.
	 */
	uint64_t	ms_synchist[SPACE_MAP_HISTOGRAM_SIZE];
	uint64_t	ms_deferhist[TXG_DEFER_SIZE][SPACE_MAP_HISTOGRAM_SIZE];

	/*
	 * Tracks the exact amount of allocated space of this metaslab
	 * (and specifically the metaslab's space map) up to the most
	 * recently completed sync pass [see usage in metaslab_sync()].
	 */
	uint64_t	ms_allocated_space;
	int64_t		ms_deferspace;	/* sum of ms_defermap[] space	*/
	uint64_t	ms_weight;	/* weight vs. others in group	*/
	uint64_t	ms_activation_weight;	/* activation weight	*/

	/*
	 * Track of whenever a metaslab is selected for loading or allocation.
	 * We use this value to determine how long the metaslab should
	 * stay cached.
	 */
	uint64_t	ms_selected_txg;
	/*
	 * ms_load/unload_time can be used for performance monitoring
	 * (e.g. by dtrace or mdb).
	 */
	hrtime_t	ms_load_time;	/* time last loaded */
	hrtime_t	ms_unload_time;	/* time last unloaded */
	hrtime_t	ms_selected_time; /* time last allocated from */

	uint64_t	ms_alloc_txg;	/* last successful alloc (debug only) */
	uint64_t	ms_max_size;	/* maximum allocatable size	*/

	/*
	 * -1 if it's not active in an allocator, otherwise set to the allocator
	 * this metaslab is active for.
	 */
	int		ms_allocator;
	boolean_t	ms_primary; /* Only valid if ms_allocator is not -1 */

	/*
	 * The metaslab block allocators can optionally use a size-ordered
	 * range tree and/or an array of LBAs. Not all allocators use
	 * this functionality. The ms_allocatable_by_size should always
	 * contain the same number of segments as the ms_allocatable. The
	 * only difference is that the ms_allocatable_by_size is ordered by
	 * segment sizes.
	 */
	zfs_btree_t		ms_allocatable_by_size;
	zfs_btree_t		ms_unflushed_frees_by_size;
	uint64_t	ms_lbas[MAX_LBAS];

	metaslab_group_t *ms_group;	/* metaslab group		*/
	avl_node_t	ms_group_node;	/* node in metaslab group tree	*/
	txg_node_t	ms_txg_node;	/* per-txg dirty metaslab links	*/
	avl_node_t	ms_spa_txg_node; /* node in spa_metaslabs_by_txg */
	/*
	 * Node in metaslab class's selected txg list
	 */
	multilist_node_t	ms_class_txg_node;

	/*
	 * Allocs and frees that are committed to the vdev log spacemap but
	 * not yet to this metaslab's spacemap.
	 */
	range_tree_t	*ms_unflushed_allocs;
	range_tree_t	*ms_unflushed_frees;

	/*
	 * We have flushed entries up to but not including this TXG. In
	 * other words, all changes from this TXG and onward should not
	 * be in this metaslab's space map and must be read from the
	 * log space maps.
	 */
	uint64_t	ms_unflushed_txg;

	/* updated every time we are done syncing the metaslab's space map */
	uint64_t	ms_synced_length;

	boolean_t	ms_new;
};

typedef struct metaslab_unflushed_phys {
	/* on-disk counterpart of ms_unflushed_txg */
	uint64_t	msp_unflushed_txg;
} metaslab_unflushed_phys_t;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_METASLAB_IMPL_H */
