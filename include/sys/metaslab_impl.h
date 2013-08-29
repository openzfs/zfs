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
 * Copyright (c) 2013 by Delphix. All rights reserved.
 */

#ifndef _SYS_METASLAB_IMPL_H
#define	_SYS_METASLAB_IMPL_H

#include <sys/metaslab.h>
#include <sys/space_map.h>
#include <sys/vdev.h>
#include <sys/txg.h>
#include <sys/avl.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct metaslab_class {
	spa_t			*mc_spa;
	metaslab_group_t	*mc_rotor;
	space_map_ops_t		*mc_ops;
	uint64_t		mc_aliquot;
	uint64_t		mc_alloc_groups; /* # of allocatable groups */
	uint64_t		mc_alloc;	/* total allocated space */
	uint64_t		mc_deferred;	/* total deferred frees */
	uint64_t		mc_space;	/* total space (alloc + free) */
	uint64_t		mc_dspace;	/* total deflated space */
};

struct metaslab_group {
	kmutex_t		mg_lock;
	avl_tree_t		mg_metaslab_tree;
	uint64_t		mg_aliquot;
	uint64_t		mg_bonus_area;
	uint64_t		mg_alloc_failures;
	boolean_t		mg_allocatable;		/* can we allocate? */
	uint64_t		mg_free_capacity;	/* percentage free */
	int64_t			mg_bias;
	int64_t			mg_activation_count;
	metaslab_class_t	*mg_class;
	vdev_t			*mg_vd;
	metaslab_group_t	*mg_prev;
	metaslab_group_t	*mg_next;
};

/*
 * Each metaslab maintains an in-core free map (ms_map) that contains the
 * current list of free segments. As blocks are allocated, the allocated
 * segment is removed from the ms_map and added to a per txg allocation map.
 * As blocks are freed, they are added to the per txg free map. These per
 * txg maps allow us to process all allocations and frees in syncing context
 * where it is safe to update the on-disk space maps.
 *
 * Each metaslab's free space is tracked in a space map object in the MOS,
 * which is only updated in syncing context. Each time we sync a txg,
 * we append the allocs and frees from that txg to the space map object.
 * When the txg is done syncing, metaslab_sync_done() updates ms_smo
 * to ms_smo_syncing. Everything in ms_smo is always safe to allocate.
 *
 * To load the in-core free map we read the space map object from disk.
 * This object contains a series of alloc and free records that are
 * combined to make up the list of all free segments in this metaslab. These
 * segments are represented in-core by the ms_map and are stored in an
 * AVL tree.
 *
 * As the space map objects grows (as a result of the appends) it will
 * eventually become space-inefficient. When the space map object is
 * zfs_condense_pct/100 times the size of the minimal on-disk representation,
 * we rewrite it in its minimized form.
 */
struct metaslab {
	kmutex_t	ms_lock;	/* metaslab lock		*/
	space_map_obj_t	ms_smo;		/* synced space map object	*/
	space_map_obj_t	ms_smo_syncing;	/* syncing space map object	*/
	space_map_t	*ms_allocmap[TXG_SIZE];	/* allocated this txg	*/
	space_map_t	*ms_freemap[TXG_SIZE];	/* freed this txg	*/
	space_map_t	*ms_defermap[TXG_DEFER_SIZE];	/* deferred frees */
	space_map_t	*ms_map;	/* in-core free space map	*/
	int64_t		ms_deferspace;	/* sum of ms_defermap[] space	*/
	uint64_t	ms_weight;	/* weight vs. others in group	*/
	metaslab_group_t *ms_group;	/* metaslab group		*/
	avl_node_t	ms_group_node;	/* node in metaslab group tree	*/
	txg_node_t	ms_txg_node;	/* per-txg dirty metaslab links	*/
};

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_METASLAB_IMPL_H */
