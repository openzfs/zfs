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
	int64_t			mg_bias;
	int64_t			mg_activation_count;
	metaslab_class_t	*mg_class;
	vdev_t			*mg_vd;
	metaslab_group_t	*mg_prev;
	metaslab_group_t	*mg_next;
};

/*
 * Each metaslab's free space is tracked in space map object in the MOS,
 * which is only updated in syncing context.  Each time we sync a txg,
 * we append the allocs and frees from that txg to the space map object.
 * When the txg is done syncing, metaslab_sync_done() updates ms_smo
 * to ms_smo_syncing.  Everything in ms_smo is always safe to allocate.
 */
struct metaslab {
	kmutex_t	ms_lock;	/* metaslab lock		*/
	space_map_obj_t	ms_smo;		/* synced space map object	*/
	space_map_obj_t	ms_smo_syncing;	/* syncing space map object	*/
	space_map_t	ms_allocmap[TXG_SIZE];  /* allocated this txg	*/
	space_map_t	ms_freemap[TXG_SIZE];	/* freed this txg	*/
	space_map_t	ms_defermap[TXG_DEFER_SIZE]; /* deferred frees	*/
	space_map_t	ms_map;		/* in-core free space map	*/
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
