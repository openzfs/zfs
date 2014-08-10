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
 * Copyright (c) 2011, 2014 by Delphix. All rights reserved.
 */

#ifndef _SYS_METASLAB_IMPL_H
#define	_SYS_METASLAB_IMPL_H

#include <sys/metaslab.h>
#include <sys/space_map.h>
#include <sys/range_tree.h>
#include <sys/vdev.h>
#include <sys/txg.h>
#include <sys/avl.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct metaslab_class {
	spa_t			*mc_spa;
	metaslab_group_t	*mc_rotor;
	metaslab_ops_t		*mc_ops;
	uint64_t		mc_aliquot;
	uint64_t		mc_alloc_groups; /* # of allocatable groups */
	uint64_t		mc_alloc;	/* total allocated space */
	uint64_t		mc_deferred;	/* total deferred frees */
	uint64_t		mc_space;	/* total space (alloc + free) */
	uint64_t		mc_dspace;	/* total deflated space */
	kmutex_t		mc_fastwrite_lock;
};

struct metaslab_group {
	kmutex_t		mg_lock;
	avl_tree_t		mg_metaslab_tree;
	uint64_t		mg_aliquot;
	boolean_t		mg_allocatable;		/* can we allocate? */
	uint64_t		mg_free_capacity;	/* percentage free */
	int64_t			mg_bias;
	int64_t			mg_activation_count;
	metaslab_class_t	*mg_class;
	vdev_t			*mg_vd;
	taskq_t			*mg_taskq;
	metaslab_group_t	*mg_prev;
	metaslab_group_t	*mg_next;
};

/*
 * This value defines the number of elements in the ms_lbas array. The value
 * of 64 was chosen as it covers to cover all power of 2 buckets up to
 * UINT64_MAX. This is the equivalent of highbit(UINT64_MAX).
 */
#define	MAX_LBAS	64

/*
 * Each metaslab maintains a set of in-core trees to track metaslab operations.
 * The in-core free tree (ms_tree) contains the current list of free segments.
 * As blocks are allocated, the allocated segment are removed from the ms_tree
 * and added to a per txg allocation tree (ms_alloctree). As blocks are freed,
 * they are added to the per txg free tree (ms_freetree). These per txg
 * trees allow us to process all allocations and frees in syncing context
 * where it is safe to update the on-disk space maps. One additional in-core
 * tree is maintained to track deferred frees (ms_defertree). Once a block
 * is freed it will move from the ms_freetree to the ms_defertree. A deferred
 * free means that a block has been freed but cannot be used by the pool
 * until TXG_DEFER_SIZE transactions groups later. For example, a block
 * that is freed in txg 50 will not be available for reallocation until
 * txg 52 (50 + TXG_DEFER_SIZE).  This provides a safety net for uberblock
 * rollback. A pool could be safely rolled back TXG_DEFERS_SIZE
 * transactions groups and ensure that no block has been reallocated.
 *
 * The simplified transition diagram looks like this:
 *
 *
 *      ALLOCATE
 *         |
 *         V
 *    free segment (ms_tree) --------> ms_alloctree ----> (write to space map)
 *         ^
 *         |
 *         |                           ms_freetree <--- FREE
 *         |                                 |
 *         |                                 |
 *         |                                 |
 *         +----------- ms_defertree <-------+---------> (write to space map)
 *
 *
 * Each metaslab's space is tracked in a single space map in the MOS,
 * which is only updated in syncing context. Each time we sync a txg,
 * we append the allocs and frees from that txg to the space map.
 * The pool space is only updated once all metaslabs have finished syncing.
 *
 * To load the in-core free tree we read the space map from disk.
 * This object contains a series of alloc and free records that are
 * combined to make up the list of all free segments in this metaslab. These
 * segments are represented in-core by the ms_tree and are stored in an
 * AVL tree.
 *
 * As the space map grows (as a result of the appends) it will
 * eventually become space-inefficient. When the metaslab's in-core free tree
 * is zfs_condense_pct/100 times the size of the minimal on-disk
 * representation, we rewrite it in its minimized form. If a metaslab
 * needs to condense then we must set the ms_condensing flag to ensure
 * that allocations are not performed on the metaslab that is being written.
 */
struct metaslab {
	kmutex_t	ms_lock;
	kcondvar_t	ms_load_cv;
	space_map_t	*ms_sm;
	metaslab_ops_t	*ms_ops;
	uint64_t	ms_id;
	uint64_t	ms_start;
	uint64_t	ms_size;

	range_tree_t	*ms_alloctree[TXG_SIZE];
	range_tree_t	*ms_freetree[TXG_SIZE];
	range_tree_t	*ms_defertree[TXG_DEFER_SIZE];
	range_tree_t	*ms_tree;

	boolean_t	ms_condensing;	/* condensing? */
	boolean_t	ms_loaded;
	boolean_t	ms_loading;

	int64_t		ms_deferspace;	/* sum of ms_defermap[] space	*/
	uint64_t	ms_weight;	/* weight vs. others in group	*/
	uint64_t	ms_factor;
	uint64_t	ms_access_txg;

	/*
	 * The metaslab block allocators can optionally use a size-ordered
	 * range tree and/or an array of LBAs. Not all allocators use
	 * this functionality. The ms_size_tree should always contain the
	 * same number of segments as the ms_tree. The only difference
	 * is that the ms_size_tree is ordered by segment sizes.
	 */
	avl_tree_t	ms_size_tree;
	uint64_t	ms_lbas[MAX_LBAS];

	metaslab_group_t *ms_group;	/* metaslab group		*/
	avl_node_t	ms_group_node;	/* node in metaslab group tree	*/
	txg_node_t	ms_txg_node;	/* per-txg dirty metaslab links	*/
};

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_METASLAB_IMPL_H */
