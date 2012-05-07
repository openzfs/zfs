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
 * Copyright (c) 2012 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/dmu.h>
#include <sys/dmu_tx.h>
#include <sys/space_map.h>
#include <sys/metaslab_impl.h>
#include <sys/vdev_impl.h>
#include <sys/zio.h>

#define WITH_DF_BLOCK_ALLOCATOR

/*
 * Allow allocations to switch to gang blocks quickly. We do this to
 * avoid having to load lots of space_maps in a given txg. There are,
 * however, some cases where we want to avoid "fast" ganging and instead
 * we want to do an exhaustive search of all metaslabs on this device.
 * Currently we don't allow any gang, zil, or dump device related allocations
 * to "fast" gang.
 */
#define	CAN_FASTGANG(flags) \
	(!((flags) & (METASLAB_GANG_CHILD | METASLAB_GANG_HEADER | \
	METASLAB_GANG_AVOID)))

uint64_t metaslab_aliquot = 512ULL << 10;
uint64_t metaslab_gang_bang = SPA_MAXBLOCKSIZE + 1;	/* force gang blocks */

/*
 * This value defines the number of allowed allocation failures per vdev.
 * If a device reaches this threshold in a given txg then we consider skipping
 * allocations on that device.
 */
int zfs_mg_alloc_failures;

/*
 * Metaslab debugging: when set, keeps all space maps in core to verify frees.
 */
static int metaslab_debug = 0;

/*
 * Minimum size which forces the dynamic allocator to change
 * it's allocation strategy.  Once the space map cannot satisfy
 * an allocation of this size then it switches to using more
 * aggressive strategy (i.e search by size rather than offset).
 */
uint64_t metaslab_df_alloc_threshold = SPA_MAXBLOCKSIZE;

/*
 * The minimum free space, in percent, which must be available
 * in a space map to continue allocations in a first-fit fashion.
 * Once the space_map's free space drops below this level we dynamically
 * switch to using best-fit allocations.
 */
int metaslab_df_free_pct = 4;

/*
 * A metaslab is considered "free" if it contains a contiguous
 * segment which is greater than metaslab_min_alloc_size.
 */
uint64_t metaslab_min_alloc_size = DMU_MAX_ACCESS;

/*
 * Max number of space_maps to prefetch.
 */
int metaslab_prefetch_limit = SPA_DVAS_PER_BP;

/*
 * Percentage bonus multiplier for metaslabs that are in the bonus area.
 */
int metaslab_smo_bonus_pct = 150;

/*
 * ==========================================================================
 * Metaslab classes
 * ==========================================================================
 */
metaslab_class_t *
metaslab_class_create(spa_t *spa, space_map_ops_t *ops)
{
	metaslab_class_t *mc;

	mc = kmem_zalloc(sizeof (metaslab_class_t), KM_PUSHPAGE);

	mc->mc_spa = spa;
	mc->mc_rotor = NULL;
	mc->mc_ops = ops;

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

void
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

/*
 * ==========================================================================
 * Metaslab groups
 * ==========================================================================
 */
static int
metaslab_compare(const void *x1, const void *x2)
{
	const metaslab_t *m1 = x1;
	const metaslab_t *m2 = x2;

	if (m1->ms_weight < m2->ms_weight)
		return (1);
	if (m1->ms_weight > m2->ms_weight)
		return (-1);

	/*
	 * If the weights are identical, use the offset to force uniqueness.
	 */
	if (m1->ms_map.sm_start < m2->ms_map.sm_start)
		return (-1);
	if (m1->ms_map.sm_start > m2->ms_map.sm_start)
		return (1);

	ASSERT3P(m1, ==, m2);

	return (0);
}

metaslab_group_t *
metaslab_group_create(metaslab_class_t *mc, vdev_t *vd)
{
	metaslab_group_t *mg;

	mg = kmem_zalloc(sizeof (metaslab_group_t), KM_PUSHPAGE);
	mutex_init(&mg->mg_lock, NULL, MUTEX_DEFAULT, NULL);
	avl_create(&mg->mg_metaslab_tree, metaslab_compare,
	    sizeof (metaslab_t), offsetof(struct metaslab, ms_group_node));
	mg->mg_vd = vd;
	mg->mg_class = mc;
	mg->mg_activation_count = 0;

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

	avl_destroy(&mg->mg_metaslab_tree);
	mutex_destroy(&mg->mg_lock);
	kmem_free(mg, sizeof (metaslab_group_t));
}

void
metaslab_group_activate(metaslab_group_t *mg)
{
	metaslab_class_t *mc = mg->mg_class;
	metaslab_group_t *mgprev, *mgnext;

	ASSERT(spa_config_held(mc->mc_spa, SCL_ALLOC, RW_WRITER));

	ASSERT(mc->mc_rotor != mg);
	ASSERT(mg->mg_prev == NULL);
	ASSERT(mg->mg_next == NULL);
	ASSERT(mg->mg_activation_count <= 0);

	if (++mg->mg_activation_count <= 0)
		return;

	mg->mg_aliquot = metaslab_aliquot * MAX(1, mg->mg_vd->vdev_children);

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

void
metaslab_group_passivate(metaslab_group_t *mg)
{
	metaslab_class_t *mc = mg->mg_class;
	metaslab_group_t *mgprev, *mgnext;

	ASSERT(spa_config_held(mc->mc_spa, SCL_ALLOC, RW_WRITER));

	if (--mg->mg_activation_count != 0) {
		ASSERT(mc->mc_rotor != mg);
		ASSERT(mg->mg_prev == NULL);
		ASSERT(mg->mg_next == NULL);
		ASSERT(mg->mg_activation_count < 0);
		return;
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

static void
metaslab_group_add(metaslab_group_t *mg, metaslab_t *msp)
{
	mutex_enter(&mg->mg_lock);
	ASSERT(msp->ms_group == NULL);
	msp->ms_group = mg;
	msp->ms_weight = 0;
	avl_add(&mg->mg_metaslab_tree, msp);
	mutex_exit(&mg->mg_lock);
}

static void
metaslab_group_remove(metaslab_group_t *mg, metaslab_t *msp)
{
	mutex_enter(&mg->mg_lock);
	ASSERT(msp->ms_group == mg);
	avl_remove(&mg->mg_metaslab_tree, msp);
	msp->ms_group = NULL;
	mutex_exit(&mg->mg_lock);
}

static void
metaslab_group_sort(metaslab_group_t *mg, metaslab_t *msp, uint64_t weight)
{
	/*
	 * Although in principle the weight can be any value, in
	 * practice we do not use values in the range [1, 510].
	 */
	ASSERT(weight >= SPA_MINBLOCKSIZE-1 || weight == 0);
	ASSERT(MUTEX_HELD(&msp->ms_lock));

	mutex_enter(&mg->mg_lock);
	ASSERT(msp->ms_group == mg);
	avl_remove(&mg->mg_metaslab_tree, msp);
	msp->ms_weight = weight;
	avl_add(&mg->mg_metaslab_tree, msp);
	mutex_exit(&mg->mg_lock);
}

/*
 * ==========================================================================
 * Common allocator routines
 * ==========================================================================
 */
static int
metaslab_segsize_compare(const void *x1, const void *x2)
{
	const space_seg_t *s1 = x1;
	const space_seg_t *s2 = x2;
	uint64_t ss_size1 = s1->ss_end - s1->ss_start;
	uint64_t ss_size2 = s2->ss_end - s2->ss_start;

	if (ss_size1 < ss_size2)
		return (-1);
	if (ss_size1 > ss_size2)
		return (1);

	if (s1->ss_start < s2->ss_start)
		return (-1);
	if (s1->ss_start > s2->ss_start)
		return (1);

	return (0);
}

#if defined(WITH_FF_BLOCK_ALLOCATOR) || \
    defined(WITH_DF_BLOCK_ALLOCATOR) || \
    defined(WITH_CDF_BLOCK_ALLOCATOR)
/*
 * This is a helper function that can be used by the allocator to find
 * a suitable block to allocate. This will search the specified AVL
 * tree looking for a block that matches the specified criteria.
 */
static uint64_t
metaslab_block_picker(avl_tree_t *t, uint64_t *cursor, uint64_t size,
    uint64_t align)
{
	space_seg_t *ss, ssearch;
	avl_index_t where;

	ssearch.ss_start = *cursor;
	ssearch.ss_end = *cursor + size;

	ss = avl_find(t, &ssearch, &where);
	if (ss == NULL)
		ss = avl_nearest(t, where, AVL_AFTER);

	while (ss != NULL) {
		uint64_t offset = P2ROUNDUP(ss->ss_start, align);

		if (offset + size <= ss->ss_end) {
			*cursor = offset + size;
			return (offset);
		}
		ss = AVL_NEXT(t, ss);
	}

	/*
	 * If we know we've searched the whole map (*cursor == 0), give up.
	 * Otherwise, reset the cursor to the beginning and try again.
	 */
	if (*cursor == 0)
		return (-1ULL);

	*cursor = 0;
	return (metaslab_block_picker(t, cursor, size, align));
}
#endif /* WITH_FF/DF/CDF_BLOCK_ALLOCATOR */

static void
metaslab_pp_load(space_map_t *sm)
{
	space_seg_t *ss;

	ASSERT(sm->sm_ppd == NULL);
	sm->sm_ppd = kmem_zalloc(64 * sizeof (uint64_t), KM_PUSHPAGE);

	sm->sm_pp_root = kmem_alloc(sizeof (avl_tree_t), KM_PUSHPAGE);
	avl_create(sm->sm_pp_root, metaslab_segsize_compare,
	    sizeof (space_seg_t), offsetof(struct space_seg, ss_pp_node));

	for (ss = avl_first(&sm->sm_root); ss; ss = AVL_NEXT(&sm->sm_root, ss))
		avl_add(sm->sm_pp_root, ss);
}

static void
metaslab_pp_unload(space_map_t *sm)
{
	void *cookie = NULL;

	kmem_free(sm->sm_ppd, 64 * sizeof (uint64_t));
	sm->sm_ppd = NULL;

	while (avl_destroy_nodes(sm->sm_pp_root, &cookie) != NULL) {
		/* tear down the tree */
	}

	avl_destroy(sm->sm_pp_root);
	kmem_free(sm->sm_pp_root, sizeof (avl_tree_t));
	sm->sm_pp_root = NULL;
}

/* ARGSUSED */
static void
metaslab_pp_claim(space_map_t *sm, uint64_t start, uint64_t size)
{
	/* No need to update cursor */
}

/* ARGSUSED */
static void
metaslab_pp_free(space_map_t *sm, uint64_t start, uint64_t size)
{
	/* No need to update cursor */
}

/*
 * Return the maximum contiguous segment within the metaslab.
 */
uint64_t
metaslab_pp_maxsize(space_map_t *sm)
{
	avl_tree_t *t = sm->sm_pp_root;
	space_seg_t *ss;

	if (t == NULL || (ss = avl_last(t)) == NULL)
		return (0ULL);

	return (ss->ss_end - ss->ss_start);
}

#if defined(WITH_FF_BLOCK_ALLOCATOR)
/*
 * ==========================================================================
 * The first-fit block allocator
 * ==========================================================================
 */
static uint64_t
metaslab_ff_alloc(space_map_t *sm, uint64_t size)
{
	avl_tree_t *t = &sm->sm_root;
	uint64_t align = size & -size;
	uint64_t *cursor = (uint64_t *)sm->sm_ppd + highbit(align) - 1;

	return (metaslab_block_picker(t, cursor, size, align));
}

/* ARGSUSED */
boolean_t
metaslab_ff_fragmented(space_map_t *sm)
{
	return (B_TRUE);
}

static space_map_ops_t metaslab_ff_ops = {
	metaslab_pp_load,
	metaslab_pp_unload,
	metaslab_ff_alloc,
	metaslab_pp_claim,
	metaslab_pp_free,
	metaslab_pp_maxsize,
	metaslab_ff_fragmented
};

space_map_ops_t *zfs_metaslab_ops = &metaslab_ff_ops;
#endif /* WITH_FF_BLOCK_ALLOCATOR */

#if defined(WITH_DF_BLOCK_ALLOCATOR)
/*
 * ==========================================================================
 * Dynamic block allocator -
 * Uses the first fit allocation scheme until space get low and then
 * adjusts to a best fit allocation method. Uses metaslab_df_alloc_threshold
 * and metaslab_df_free_pct to determine when to switch the allocation scheme.
 * ==========================================================================
 */
static uint64_t
metaslab_df_alloc(space_map_t *sm, uint64_t size)
{
	avl_tree_t *t = &sm->sm_root;
	uint64_t align = size & -size;
	uint64_t *cursor = (uint64_t *)sm->sm_ppd + highbit(align) - 1;
	uint64_t max_size = metaslab_pp_maxsize(sm);
	int free_pct = sm->sm_space * 100 / sm->sm_size;

	ASSERT(MUTEX_HELD(sm->sm_lock));
	ASSERT3U(avl_numnodes(&sm->sm_root), ==, avl_numnodes(sm->sm_pp_root));

	if (max_size < size)
		return (-1ULL);

	/*
	 * If we're running low on space switch to using the size
	 * sorted AVL tree (best-fit).
	 */
	if (max_size < metaslab_df_alloc_threshold ||
	    free_pct < metaslab_df_free_pct) {
		t = sm->sm_pp_root;
		*cursor = 0;
	}

	return (metaslab_block_picker(t, cursor, size, 1ULL));
}

static boolean_t
metaslab_df_fragmented(space_map_t *sm)
{
	uint64_t max_size = metaslab_pp_maxsize(sm);
	int free_pct = sm->sm_space * 100 / sm->sm_size;

	if (max_size >= metaslab_df_alloc_threshold &&
	    free_pct >= metaslab_df_free_pct)
		return (B_FALSE);

	return (B_TRUE);
}

static space_map_ops_t metaslab_df_ops = {
	metaslab_pp_load,
	metaslab_pp_unload,
	metaslab_df_alloc,
	metaslab_pp_claim,
	metaslab_pp_free,
	metaslab_pp_maxsize,
	metaslab_df_fragmented
};

space_map_ops_t *zfs_metaslab_ops = &metaslab_df_ops;
#endif /* WITH_DF_BLOCK_ALLOCATOR */

/*
 * ==========================================================================
 * Other experimental allocators
 * ==========================================================================
 */
#if defined(WITH_CDF_BLOCK_ALLOCATOR)
static uint64_t
metaslab_cdf_alloc(space_map_t *sm, uint64_t size)
{
	avl_tree_t *t = &sm->sm_root;
	uint64_t *cursor = (uint64_t *)sm->sm_ppd;
	uint64_t *extent_end = (uint64_t *)sm->sm_ppd + 1;
	uint64_t max_size = metaslab_pp_maxsize(sm);
	uint64_t rsize = size;
	uint64_t offset = 0;

	ASSERT(MUTEX_HELD(sm->sm_lock));
	ASSERT3U(avl_numnodes(&sm->sm_root), ==, avl_numnodes(sm->sm_pp_root));

	if (max_size < size)
		return (-1ULL);

	ASSERT3U(*extent_end, >=, *cursor);

	/*
	 * If we're running low on space switch to using the size
	 * sorted AVL tree (best-fit).
	 */
	if ((*cursor + size) > *extent_end) {

		t = sm->sm_pp_root;
		*cursor = *extent_end = 0;

		if (max_size > 2 * SPA_MAXBLOCKSIZE)
			rsize = MIN(metaslab_min_alloc_size, max_size);
		offset = metaslab_block_picker(t, extent_end, rsize, 1ULL);
		if (offset != -1)
			*cursor = offset + size;
	} else {
		offset = metaslab_block_picker(t, cursor, rsize, 1ULL);
	}
	ASSERT3U(*cursor, <=, *extent_end);
	return (offset);
}

static boolean_t
metaslab_cdf_fragmented(space_map_t *sm)
{
	uint64_t max_size = metaslab_pp_maxsize(sm);

	if (max_size > (metaslab_min_alloc_size * 10))
		return (B_FALSE);
	return (B_TRUE);
}

static space_map_ops_t metaslab_cdf_ops = {
	metaslab_pp_load,
	metaslab_pp_unload,
	metaslab_cdf_alloc,
	metaslab_pp_claim,
	metaslab_pp_free,
	metaslab_pp_maxsize,
	metaslab_cdf_fragmented
};

space_map_ops_t *zfs_metaslab_ops = &metaslab_cdf_ops;
#endif /* WITH_CDF_BLOCK_ALLOCATOR */

#if defined(WITH_NDF_BLOCK_ALLOCATOR)
uint64_t metaslab_ndf_clump_shift = 4;

static uint64_t
metaslab_ndf_alloc(space_map_t *sm, uint64_t size)
{
	avl_tree_t *t = &sm->sm_root;
	avl_index_t where;
	space_seg_t *ss, ssearch;
	uint64_t hbit = highbit(size);
	uint64_t *cursor = (uint64_t *)sm->sm_ppd + hbit - 1;
	uint64_t max_size = metaslab_pp_maxsize(sm);

	ASSERT(MUTEX_HELD(sm->sm_lock));
	ASSERT3U(avl_numnodes(&sm->sm_root), ==, avl_numnodes(sm->sm_pp_root));

	if (max_size < size)
		return (-1ULL);

	ssearch.ss_start = *cursor;
	ssearch.ss_end = *cursor + size;

	ss = avl_find(t, &ssearch, &where);
	if (ss == NULL || (ss->ss_start + size > ss->ss_end)) {
		t = sm->sm_pp_root;

		ssearch.ss_start = 0;
		ssearch.ss_end = MIN(max_size,
		    1ULL << (hbit + metaslab_ndf_clump_shift));
		ss = avl_find(t, &ssearch, &where);
		if (ss == NULL)
			ss = avl_nearest(t, where, AVL_AFTER);
		ASSERT(ss != NULL);
	}

	if (ss != NULL) {
		if (ss->ss_start + size <= ss->ss_end) {
			*cursor = ss->ss_start + size;
			return (ss->ss_start);
		}
	}
	return (-1ULL);
}

static boolean_t
metaslab_ndf_fragmented(space_map_t *sm)
{
	uint64_t max_size = metaslab_pp_maxsize(sm);

	if (max_size > (metaslab_min_alloc_size << metaslab_ndf_clump_shift))
		return (B_FALSE);
	return (B_TRUE);
}


static space_map_ops_t metaslab_ndf_ops = {
	metaslab_pp_load,
	metaslab_pp_unload,
	metaslab_ndf_alloc,
	metaslab_pp_claim,
	metaslab_pp_free,
	metaslab_pp_maxsize,
	metaslab_ndf_fragmented
};

space_map_ops_t *zfs_metaslab_ops = &metaslab_ndf_ops;
#endif /* WITH_NDF_BLOCK_ALLOCATOR */

/*
 * ==========================================================================
 * Metaslabs
 * ==========================================================================
 */
metaslab_t *
metaslab_init(metaslab_group_t *mg, space_map_obj_t *smo,
	uint64_t start, uint64_t size, uint64_t txg)
{
	vdev_t *vd = mg->mg_vd;
	metaslab_t *msp;

	msp = kmem_zalloc(sizeof (metaslab_t), KM_PUSHPAGE);
	mutex_init(&msp->ms_lock, NULL, MUTEX_DEFAULT, NULL);

	msp->ms_smo_syncing = *smo;

	/*
	 * We create the main space map here, but we don't create the
	 * allocmaps and freemaps until metaslab_sync_done().  This serves
	 * two purposes: it allows metaslab_sync_done() to detect the
	 * addition of new space; and for debugging, it ensures that we'd
	 * data fault on any attempt to use this metaslab before it's ready.
	 */
	space_map_create(&msp->ms_map, start, size,
	    vd->vdev_ashift, &msp->ms_lock);

	metaslab_group_add(mg, msp);

	if (metaslab_debug && smo->smo_object != 0) {
		mutex_enter(&msp->ms_lock);
		VERIFY(space_map_load(&msp->ms_map, mg->mg_class->mc_ops,
		    SM_FREE, smo, spa_meta_objset(vd->vdev_spa)) == 0);
		mutex_exit(&msp->ms_lock);
	}

	/*
	 * If we're opening an existing pool (txg == 0) or creating
	 * a new one (txg == TXG_INITIAL), all space is available now.
	 * If we're adding space to an existing pool, the new space
	 * does not become available until after this txg has synced.
	 */
	if (txg <= TXG_INITIAL)
		metaslab_sync_done(msp, 0);

	if (txg != 0) {
		vdev_dirty(vd, 0, NULL, txg);
		vdev_dirty(vd, VDD_METASLAB, msp, txg);
	}

	return (msp);
}

void
metaslab_fini(metaslab_t *msp)
{
	metaslab_group_t *mg = msp->ms_group;
	int t;

	vdev_space_update(mg->mg_vd,
	    -msp->ms_smo.smo_alloc, 0, -msp->ms_map.sm_size);

	metaslab_group_remove(mg, msp);

	mutex_enter(&msp->ms_lock);

	space_map_unload(&msp->ms_map);
	space_map_destroy(&msp->ms_map);

	for (t = 0; t < TXG_SIZE; t++) {
		space_map_destroy(&msp->ms_allocmap[t]);
		space_map_destroy(&msp->ms_freemap[t]);
	}

	for (t = 0; t < TXG_DEFER_SIZE; t++)
		space_map_destroy(&msp->ms_defermap[t]);

	ASSERT3S(msp->ms_deferspace, ==, 0);

	mutex_exit(&msp->ms_lock);
	mutex_destroy(&msp->ms_lock);

	kmem_free(msp, sizeof (metaslab_t));
}

#define	METASLAB_WEIGHT_PRIMARY		(1ULL << 63)
#define	METASLAB_WEIGHT_SECONDARY	(1ULL << 62)
#define	METASLAB_ACTIVE_MASK		\
	(METASLAB_WEIGHT_PRIMARY | METASLAB_WEIGHT_SECONDARY)

static uint64_t
metaslab_weight(metaslab_t *msp)
{
	metaslab_group_t *mg = msp->ms_group;
	space_map_t *sm = &msp->ms_map;
	space_map_obj_t *smo = &msp->ms_smo;
	vdev_t *vd = mg->mg_vd;
	uint64_t weight, space;

	ASSERT(MUTEX_HELD(&msp->ms_lock));

	/*
	 * The baseline weight is the metaslab's free space.
	 */
	space = sm->sm_size - smo->smo_alloc;
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
	weight = 2 * weight -
	    ((sm->sm_start >> vd->vdev_ms_shift) * weight) / vd->vdev_ms_count;
	ASSERT(weight >= space && weight <= 2 * space);

	/*
	 * For locality, assign higher weight to metaslabs which have
	 * a lower offset than what we've already activated.
	 */
	if (sm->sm_start <= mg->mg_bonus_area)
		weight *= (metaslab_smo_bonus_pct / 100);
	ASSERT(weight >= space &&
	    weight <= 2 * (metaslab_smo_bonus_pct / 100) * space);

	if (sm->sm_loaded && !sm->sm_ops->smop_fragmented(sm)) {
		/*
		 * If this metaslab is one we're actively using, adjust its
		 * weight to make it preferable to any inactive metaslab so
		 * we'll polish it off.
		 */
		weight |= (msp->ms_weight & METASLAB_ACTIVE_MASK);
	}
	return (weight);
}

static void
metaslab_prefetch(metaslab_group_t *mg)
{
	spa_t *spa = mg->mg_vd->vdev_spa;
	metaslab_t *msp;
	avl_tree_t *t = &mg->mg_metaslab_tree;
	int m;

	mutex_enter(&mg->mg_lock);

	/*
	 * Prefetch the next potential metaslabs
	 */
	for (msp = avl_first(t), m = 0; msp; msp = AVL_NEXT(t, msp), m++) {
		space_map_t *sm = &msp->ms_map;
		space_map_obj_t *smo = &msp->ms_smo;

		/* If we have reached our prefetch limit then we're done */
		if (m >= metaslab_prefetch_limit)
			break;

		if (!sm->sm_loaded && smo->smo_object != 0) {
			mutex_exit(&mg->mg_lock);
			dmu_prefetch(spa_meta_objset(spa), smo->smo_object,
			    0ULL, smo->smo_objsize);
			mutex_enter(&mg->mg_lock);
		}
	}
	mutex_exit(&mg->mg_lock);
}

static int
metaslab_activate(metaslab_t *msp, uint64_t activation_weight)
{
	metaslab_group_t *mg = msp->ms_group;
	space_map_t *sm = &msp->ms_map;
	space_map_ops_t *sm_ops = msp->ms_group->mg_class->mc_ops;
	int t;

	ASSERT(MUTEX_HELD(&msp->ms_lock));

	if ((msp->ms_weight & METASLAB_ACTIVE_MASK) == 0) {
		space_map_load_wait(sm);
		if (!sm->sm_loaded) {
			int error = space_map_load(sm, sm_ops, SM_FREE,
			    &msp->ms_smo,
			    spa_meta_objset(msp->ms_group->mg_vd->vdev_spa));
			if (error)  {
				metaslab_group_sort(msp->ms_group, msp, 0);
				return (error);
			}
			for (t = 0; t < TXG_DEFER_SIZE; t++)
				space_map_walk(&msp->ms_defermap[t],
				    space_map_claim, sm);

		}

		/*
		 * Track the bonus area as we activate new metaslabs.
		 */
		if (sm->sm_start > mg->mg_bonus_area) {
			mutex_enter(&mg->mg_lock);
			mg->mg_bonus_area = sm->sm_start;
			mutex_exit(&mg->mg_lock);
		}

		metaslab_group_sort(msp->ms_group, msp,
		    msp->ms_weight | activation_weight);
	}
	ASSERT(sm->sm_loaded);
	ASSERT(msp->ms_weight & METASLAB_ACTIVE_MASK);

	return (0);
}

static void
metaslab_passivate(metaslab_t *msp, uint64_t size)
{
	/*
	 * If size < SPA_MINBLOCKSIZE, then we will not allocate from
	 * this metaslab again.  In that case, it had better be empty,
	 * or we would be leaving space on the table.
	 */
	ASSERT(size >= SPA_MINBLOCKSIZE || msp->ms_map.sm_space == 0);
	metaslab_group_sort(msp->ms_group, msp, MIN(msp->ms_weight, size));
	ASSERT((msp->ms_weight & METASLAB_ACTIVE_MASK) == 0);
}

/*
 * Write a metaslab to disk in the context of the specified transaction group.
 */
void
metaslab_sync(metaslab_t *msp, uint64_t txg)
{
	vdev_t *vd = msp->ms_group->mg_vd;
	spa_t *spa = vd->vdev_spa;
	objset_t *mos = spa_meta_objset(spa);
	space_map_t *allocmap = &msp->ms_allocmap[txg & TXG_MASK];
	space_map_t *freemap = &msp->ms_freemap[txg & TXG_MASK];
	space_map_t *freed_map = &msp->ms_freemap[TXG_CLEAN(txg) & TXG_MASK];
	space_map_t *sm = &msp->ms_map;
	space_map_obj_t *smo = &msp->ms_smo_syncing;
	dmu_buf_t *db;
	dmu_tx_t *tx;
	int t;

	ASSERT(!vd->vdev_ishole);

	if (allocmap->sm_space == 0 && freemap->sm_space == 0)
		return;

	/*
	 * The only state that can actually be changing concurrently with
	 * metaslab_sync() is the metaslab's ms_map.  No other thread can
	 * be modifying this txg's allocmap, freemap, freed_map, or smo.
	 * Therefore, we only hold ms_lock to satify space_map ASSERTs.
	 * We drop it whenever we call into the DMU, because the DMU
	 * can call down to us (e.g. via zio_free()) at any time.
	 */

	tx = dmu_tx_create_assigned(spa_get_dsl(spa), txg);

	if (smo->smo_object == 0) {
		ASSERT(smo->smo_objsize == 0);
		ASSERT(smo->smo_alloc == 0);
		smo->smo_object = dmu_object_alloc(mos,
		    DMU_OT_SPACE_MAP, 1 << SPACE_MAP_BLOCKSHIFT,
		    DMU_OT_SPACE_MAP_HEADER, sizeof (*smo), tx);
		ASSERT(smo->smo_object != 0);
		dmu_write(mos, vd->vdev_ms_array, sizeof (uint64_t) *
		    (sm->sm_start >> vd->vdev_ms_shift),
		    sizeof (uint64_t), &smo->smo_object, tx);
	}

	mutex_enter(&msp->ms_lock);

	space_map_walk(freemap, space_map_add, freed_map);

	if (sm->sm_loaded && spa_sync_pass(spa) == 1 && smo->smo_objsize >=
	    2 * sizeof (uint64_t) * avl_numnodes(&sm->sm_root)) {
		/*
		 * The in-core space map representation is twice as compact
		 * as the on-disk one, so it's time to condense the latter
		 * by generating a pure allocmap from first principles.
		 *
		 * This metaslab is 100% allocated,
		 * minus the content of the in-core map (sm),
		 * minus what's been freed this txg (freed_map),
		 * minus deferred frees (ms_defermap[]),
		 * minus allocations from txgs in the future
		 * (because they haven't been committed yet).
		 */
		space_map_vacate(allocmap, NULL, NULL);
		space_map_vacate(freemap, NULL, NULL);

		space_map_add(allocmap, allocmap->sm_start, allocmap->sm_size);

		space_map_walk(sm, space_map_remove, allocmap);
		space_map_walk(freed_map, space_map_remove, allocmap);

		for (t = 0; t < TXG_DEFER_SIZE; t++)
			space_map_walk(&msp->ms_defermap[t],
			    space_map_remove, allocmap);

		for (t = 1; t < TXG_CONCURRENT_STATES; t++)
			space_map_walk(&msp->ms_allocmap[(txg + t) & TXG_MASK],
			    space_map_remove, allocmap);

		mutex_exit(&msp->ms_lock);
		space_map_truncate(smo, mos, tx);
		mutex_enter(&msp->ms_lock);
	}

	space_map_sync(allocmap, SM_ALLOC, smo, mos, tx);
	space_map_sync(freemap, SM_FREE, smo, mos, tx);

	mutex_exit(&msp->ms_lock);

	VERIFY(0 == dmu_bonus_hold(mos, smo->smo_object, FTAG, &db));
	dmu_buf_will_dirty(db, tx);
	ASSERT3U(db->db_size, >=, sizeof (*smo));
	bcopy(smo, db->db_data, sizeof (*smo));
	dmu_buf_rele(db, FTAG);

	dmu_tx_commit(tx);
}

/*
 * Called after a transaction group has completely synced to mark
 * all of the metaslab's free space as usable.
 */
void
metaslab_sync_done(metaslab_t *msp, uint64_t txg)
{
	space_map_obj_t *smo = &msp->ms_smo;
	space_map_obj_t *smosync = &msp->ms_smo_syncing;
	space_map_t *sm = &msp->ms_map;
	space_map_t *freed_map = &msp->ms_freemap[TXG_CLEAN(txg) & TXG_MASK];
	space_map_t *defer_map = &msp->ms_defermap[txg % TXG_DEFER_SIZE];
	metaslab_group_t *mg = msp->ms_group;
	vdev_t *vd = mg->mg_vd;
	int64_t alloc_delta, defer_delta;
	int t;

	ASSERT(!vd->vdev_ishole);

	mutex_enter(&msp->ms_lock);

	/*
	 * If this metaslab is just becoming available, initialize its
	 * allocmaps and freemaps and add its capacity to the vdev.
	 */
	if (freed_map->sm_size == 0) {
		for (t = 0; t < TXG_SIZE; t++) {
			space_map_create(&msp->ms_allocmap[t], sm->sm_start,
			    sm->sm_size, sm->sm_shift, sm->sm_lock);
			space_map_create(&msp->ms_freemap[t], sm->sm_start,
			    sm->sm_size, sm->sm_shift, sm->sm_lock);
		}

		for (t = 0; t < TXG_DEFER_SIZE; t++)
			space_map_create(&msp->ms_defermap[t], sm->sm_start,
			    sm->sm_size, sm->sm_shift, sm->sm_lock);

		vdev_space_update(vd, 0, 0, sm->sm_size);
	}

	alloc_delta = smosync->smo_alloc - smo->smo_alloc;
	defer_delta = freed_map->sm_space - defer_map->sm_space;

	vdev_space_update(vd, alloc_delta + defer_delta, defer_delta, 0);

	ASSERT(msp->ms_allocmap[txg & TXG_MASK].sm_space == 0);
	ASSERT(msp->ms_freemap[txg & TXG_MASK].sm_space == 0);

	/*
	 * If there's a space_map_load() in progress, wait for it to complete
	 * so that we have a consistent view of the in-core space map.
	 * Then, add defer_map (oldest deferred frees) to this map and
	 * transfer freed_map (this txg's frees) to defer_map.
	 */
	space_map_load_wait(sm);
	space_map_vacate(defer_map, sm->sm_loaded ? space_map_free : NULL, sm);
	space_map_vacate(freed_map, space_map_add, defer_map);

	*smo = *smosync;

	msp->ms_deferspace += defer_delta;
	ASSERT3S(msp->ms_deferspace, >=, 0);
	ASSERT3S(msp->ms_deferspace, <=, sm->sm_size);
	if (msp->ms_deferspace != 0) {
		/*
		 * Keep syncing this metaslab until all deferred frees
		 * are back in circulation.
		 */
		vdev_dirty(vd, VDD_METASLAB, msp, txg + 1);
	}

	/*
	 * If the map is loaded but no longer active, evict it as soon as all
	 * future allocations have synced.  (If we unloaded it now and then
	 * loaded a moment later, the map wouldn't reflect those allocations.)
	 */
	if (sm->sm_loaded && (msp->ms_weight & METASLAB_ACTIVE_MASK) == 0) {
		int evictable = 1;

		for (t = 1; t < TXG_CONCURRENT_STATES; t++)
			if (msp->ms_allocmap[(txg + t) & TXG_MASK].sm_space)
				evictable = 0;

		if (evictable && !metaslab_debug)
			space_map_unload(sm);
	}

	metaslab_group_sort(mg, msp, metaslab_weight(msp));

	mutex_exit(&msp->ms_lock);
}

void
metaslab_sync_reassess(metaslab_group_t *mg)
{
	vdev_t *vd = mg->mg_vd;
	int64_t failures = mg->mg_alloc_failures;
	int m;

	/*
	 * Re-evaluate all metaslabs which have lower offsets than the
	 * bonus area.
	 */
	for (m = 0; m < vd->vdev_ms_count; m++) {
		metaslab_t *msp = vd->vdev_ms[m];

		if (msp->ms_map.sm_start > mg->mg_bonus_area)
			break;

		mutex_enter(&msp->ms_lock);
		metaslab_group_sort(mg, msp, metaslab_weight(msp));
		mutex_exit(&msp->ms_lock);
	}

	atomic_add_64(&mg->mg_alloc_failures, -failures);

	/*
	 * Prefetch the next potential metaslabs
	 */
	metaslab_prefetch(mg);
}

static uint64_t
metaslab_distance(metaslab_t *msp, dva_t *dva)
{
	uint64_t ms_shift = msp->ms_group->mg_vd->vdev_ms_shift;
	uint64_t offset = DVA_GET_OFFSET(dva) >> ms_shift;
	uint64_t start = msp->ms_map.sm_start >> ms_shift;

	if (msp->ms_group->mg_vd->vdev_id != DVA_GET_VDEV(dva))
		return (1ULL << 63);

	if (offset < start)
		return ((start - offset) << ms_shift);
	if (offset > start)
		return ((offset - start) << ms_shift);
	return (0);
}

static uint64_t
metaslab_group_alloc(metaslab_group_t *mg, uint64_t psize, uint64_t asize,
    uint64_t txg, uint64_t min_distance, dva_t *dva, int d, int flags)
{
	spa_t *spa = mg->mg_vd->vdev_spa;
	metaslab_t *msp = NULL;
	uint64_t offset = -1ULL;
	avl_tree_t *t = &mg->mg_metaslab_tree;
	uint64_t activation_weight;
	uint64_t target_distance;
	int i;

	activation_weight = METASLAB_WEIGHT_PRIMARY;
	for (i = 0; i < d; i++) {
		if (DVA_GET_VDEV(&dva[i]) == mg->mg_vd->vdev_id) {
			activation_weight = METASLAB_WEIGHT_SECONDARY;
			break;
		}
	}

	for (;;) {
		boolean_t was_active;

		mutex_enter(&mg->mg_lock);
		for (msp = avl_first(t); msp; msp = AVL_NEXT(t, msp)) {
			if (msp->ms_weight < asize) {
				spa_dbgmsg(spa, "%s: failed to meet weight "
				    "requirement: vdev %llu, txg %llu, mg %p, "
				    "msp %p, psize %llu, asize %llu, "
				    "failures %llu, weight %llu",
				    spa_name(spa), mg->mg_vd->vdev_id, txg,
				    mg, msp, psize, asize,
				    mg->mg_alloc_failures, msp->ms_weight);
				mutex_exit(&mg->mg_lock);
				return (-1ULL);
			}
			was_active = msp->ms_weight & METASLAB_ACTIVE_MASK;
			if (activation_weight == METASLAB_WEIGHT_PRIMARY)
				break;

			target_distance = min_distance +
			    (msp->ms_smo.smo_alloc ? 0 : min_distance >> 1);

			for (i = 0; i < d; i++)
				if (metaslab_distance(msp, &dva[i]) <
				    target_distance)
					break;
			if (i == d)
				break;
		}
		mutex_exit(&mg->mg_lock);
		if (msp == NULL)
			return (-1ULL);

		/*
		 * If we've already reached the allowable number of failed
		 * allocation attempts on this metaslab group then we
		 * consider skipping it. We skip it only if we're allowed
		 * to "fast" gang, the physical size is larger than
		 * a gang block, and we're attempting to allocate from
		 * the primary metaslab.
		 */
		if (mg->mg_alloc_failures > zfs_mg_alloc_failures &&
		    CAN_FASTGANG(flags) && psize > SPA_GANGBLOCKSIZE &&
		    activation_weight == METASLAB_WEIGHT_PRIMARY) {
			spa_dbgmsg(spa, "%s: skipping metaslab group: "
			    "vdev %llu, txg %llu, mg %p, psize %llu, "
			    "asize %llu, failures %llu", spa_name(spa),
			    mg->mg_vd->vdev_id, txg, mg, psize, asize,
			    mg->mg_alloc_failures);
			return (-1ULL);
		}

		mutex_enter(&msp->ms_lock);

		/*
		 * Ensure that the metaslab we have selected is still
		 * capable of handling our request. It's possible that
		 * another thread may have changed the weight while we
		 * were blocked on the metaslab lock.
		 */
		if (msp->ms_weight < asize || (was_active &&
		    !(msp->ms_weight & METASLAB_ACTIVE_MASK) &&
		    activation_weight == METASLAB_WEIGHT_PRIMARY)) {
			mutex_exit(&msp->ms_lock);
			continue;
		}

		if ((msp->ms_weight & METASLAB_WEIGHT_SECONDARY) &&
		    activation_weight == METASLAB_WEIGHT_PRIMARY) {
			metaslab_passivate(msp,
			    msp->ms_weight & ~METASLAB_ACTIVE_MASK);
			mutex_exit(&msp->ms_lock);
			continue;
		}

		if (metaslab_activate(msp, activation_weight) != 0) {
			mutex_exit(&msp->ms_lock);
			continue;
		}

		if ((offset = space_map_alloc(&msp->ms_map, asize)) != -1ULL)
			break;

		atomic_inc_64(&mg->mg_alloc_failures);

		metaslab_passivate(msp, space_map_maxsize(&msp->ms_map));

		mutex_exit(&msp->ms_lock);
	}

	if (msp->ms_allocmap[txg & TXG_MASK].sm_space == 0)
		vdev_dirty(mg->mg_vd, VDD_METASLAB, msp, txg);

	space_map_add(&msp->ms_allocmap[txg & TXG_MASK], offset, asize);

	mutex_exit(&msp->ms_lock);

	return (offset);
}

/*
 * Allocate a block for the specified i/o.
 */
static int
metaslab_alloc_dva(spa_t *spa, metaslab_class_t *mc, uint64_t psize,
    dva_t *dva, int d, dva_t *hintdva, uint64_t txg, int flags)
{
	metaslab_group_t *mg, *rotor;
	vdev_t *vd;
	int dshift = 3;
	int all_zero;
	int zio_lock = B_FALSE;
	boolean_t allocatable;
	uint64_t offset = -1ULL;
	uint64_t asize;
	uint64_t distance;

	ASSERT(!DVA_IS_VALID(&dva[d]));

	/*
	 * For testing, make some blocks above a certain size be gang blocks.
	 */
	if (psize >= metaslab_gang_bang && (ddi_get_lbolt() & 3) == 0)
		return (ENOSPC);

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
		 * longer exists (i.e. removed). Consult the rotor when
		 * all else fails.
		 */
		if (vd != NULL) {
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
	} else {
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
	all_zero = B_TRUE;
	do {
		ASSERT(mg->mg_activation_count == 1);

		vd = mg->mg_vd;

		/*
		 * Don't allocate from faulted devices.
		 */
		if (zio_lock) {
			spa_config_enter(spa, SCL_ZIO, FTAG, RW_READER);
			allocatable = vdev_allocatable(vd);
			spa_config_exit(spa, SCL_ZIO, FTAG);
		} else {
			allocatable = vdev_allocatable(vd);
		}
		if (!allocatable)
			goto next;

		/*
		 * Avoid writing single-copy data to a failing vdev
		 */
		if ((vd->vdev_stat.vs_write_errors > 0 ||
		    vd->vdev_state < VDEV_STATE_HEALTHY) &&
		    d == 0 && dshift == 3) {
			all_zero = B_FALSE;
			goto next;
		}

		ASSERT(mg->mg_class == mc);

		distance = vd->vdev_asize >> dshift;
		if (distance <= (1ULL << vd->vdev_ms_shift))
			distance = 0;
		else
			all_zero = B_FALSE;

		asize = vdev_psize_to_asize(vd, psize);
		ASSERT(P2PHASE(asize, 1ULL << vd->vdev_ashift) == 0);

		offset = metaslab_group_alloc(mg, psize, asize, txg, distance,
		    dva, d, flags);
		if (offset != -1ULL) {
			/*
			 * If we've just selected this metaslab group,
			 * figure out whether the corresponding vdev is
			 * over- or under-used relative to the pool,
			 * and set an allocation bias to even it out.
			 */
			if (mc->mc_aliquot == 0) {
				vdev_stat_t *vs = &vd->vdev_stat;
				int64_t vu, cu;

				vu = (vs->vs_alloc * 100) / (vs->vs_space + 1);
				cu = (mc->mc_alloc * 100) / (mc->mc_space + 1);

				/*
				 * Calculate how much more or less we should
				 * try to allocate from this device during
				 * this iteration around the rotor.
				 * For example, if a device is 80% full
				 * and the pool is 20% full then we should
				 * reduce allocations by 60% on this device.
				 *
				 * mg_bias = (20 - 80) * 512K / 100 = -307K
				 *
				 * This reduces allocations by 307K for this
				 * iteration.
				 */
				mg->mg_bias = ((cu - vu) *
				    (int64_t)mg->mg_aliquot) / 100;
			}

			if (atomic_add_64_nv(&mc->mc_aliquot, asize) >=
			    mg->mg_aliquot + mg->mg_bias) {
				mc->mc_rotor = mg->mg_next;
				mc->mc_aliquot = 0;
			}

			DVA_SET_VDEV(&dva[d], vd->vdev_id);
			DVA_SET_OFFSET(&dva[d], offset);
			DVA_SET_GANG(&dva[d], !!(flags & METASLAB_GANG_HEADER));
			DVA_SET_ASIZE(&dva[d], asize);

			return (0);
		}
next:
		mc->mc_rotor = mg->mg_next;
		mc->mc_aliquot = 0;
	} while ((mg = mg->mg_next) != rotor);

	if (!all_zero) {
		dshift++;
		ASSERT(dshift < 64);
		goto top;
	}

	if (!allocatable && !zio_lock) {
		dshift = 3;
		zio_lock = B_TRUE;
		goto top;
	}

	bzero(&dva[d], sizeof (dva_t));

	return (ENOSPC);
}

/*
 * Free the block represented by DVA in the context of the specified
 * transaction group.
 */
static void
metaslab_free_dva(spa_t *spa, const dva_t *dva, uint64_t txg, boolean_t now)
{
	uint64_t vdev = DVA_GET_VDEV(dva);
	uint64_t offset = DVA_GET_OFFSET(dva);
	uint64_t size = DVA_GET_ASIZE(dva);
	vdev_t *vd;
	metaslab_t *msp;

	ASSERT(DVA_IS_VALID(dva));

	if (txg > spa_freeze_txg(spa))
		return;

	if ((vd = vdev_lookup_top(spa, vdev)) == NULL ||
	    (offset >> vd->vdev_ms_shift) >= vd->vdev_ms_count) {
		cmn_err(CE_WARN, "metaslab_free_dva(): bad DVA %llu:%llu",
		    (u_longlong_t)vdev, (u_longlong_t)offset);
		ASSERT(0);
		return;
	}

	msp = vd->vdev_ms[offset >> vd->vdev_ms_shift];

	if (DVA_GET_GANG(dva))
		size = vdev_psize_to_asize(vd, SPA_GANGBLOCKSIZE);

	mutex_enter(&msp->ms_lock);

	if (now) {
		space_map_remove(&msp->ms_allocmap[txg & TXG_MASK],
		    offset, size);
		space_map_free(&msp->ms_map, offset, size);
	} else {
		if (msp->ms_freemap[txg & TXG_MASK].sm_space == 0)
			vdev_dirty(vd, VDD_METASLAB, msp, txg);
		space_map_add(&msp->ms_freemap[txg & TXG_MASK], offset, size);
	}

	mutex_exit(&msp->ms_lock);
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
	metaslab_t *msp;
	int error = 0;

	ASSERT(DVA_IS_VALID(dva));

	if ((vd = vdev_lookup_top(spa, vdev)) == NULL ||
	    (offset >> vd->vdev_ms_shift) >= vd->vdev_ms_count)
		return (ENXIO);

	msp = vd->vdev_ms[offset >> vd->vdev_ms_shift];

	if (DVA_GET_GANG(dva))
		size = vdev_psize_to_asize(vd, SPA_GANGBLOCKSIZE);

	mutex_enter(&msp->ms_lock);

	if ((txg != 0 && spa_writeable(spa)) || !msp->ms_map.sm_loaded)
		error = metaslab_activate(msp, METASLAB_WEIGHT_SECONDARY);

	if (error == 0 && !space_map_contains(&msp->ms_map, offset, size))
		error = ENOENT;

	if (error || txg == 0) {	/* txg == 0 indicates dry run */
		mutex_exit(&msp->ms_lock);
		return (error);
	}

	space_map_claim(&msp->ms_map, offset, size);

	if (spa_writeable(spa)) {	/* don't dirty if we're zdb(1M) */
		if (msp->ms_allocmap[txg & TXG_MASK].sm_space == 0)
			vdev_dirty(vd, VDD_METASLAB, msp, txg);
		space_map_add(&msp->ms_allocmap[txg & TXG_MASK], offset, size);
	}

	mutex_exit(&msp->ms_lock);

	return (0);
}

int
metaslab_alloc(spa_t *spa, metaslab_class_t *mc, uint64_t psize, blkptr_t *bp,
    int ndvas, uint64_t txg, blkptr_t *hintbp, int flags)
{
	dva_t *dva = bp->blk_dva;
	dva_t *hintdva = hintbp->blk_dva;
	int d, error = 0;

	ASSERT(bp->blk_birth == 0);
	ASSERT(BP_PHYSICAL_BIRTH(bp) == 0);

	spa_config_enter(spa, SCL_ALLOC, FTAG, RW_READER);

	if (mc->mc_rotor == NULL) {	/* no vdevs in this class */
		spa_config_exit(spa, SCL_ALLOC, FTAG);
		return (ENOSPC);
	}

	ASSERT(ndvas > 0 && ndvas <= spa_max_replication(spa));
	ASSERT(BP_GET_NDVAS(bp) == 0);
	ASSERT(hintbp == NULL || ndvas <= BP_GET_NDVAS(hintbp));

	for (d = 0; d < ndvas; d++) {
		error = metaslab_alloc_dva(spa, mc, psize, dva, d, hintdva,
		    txg, flags);
		if (error) {
			for (d--; d >= 0; d--) {
				metaslab_free_dva(spa, &dva[d], txg, B_TRUE);
				bzero(&dva[d], sizeof (dva_t));
			}
			spa_config_exit(spa, SCL_ALLOC, FTAG);
			return (error);
		}
	}
	ASSERT(error == 0);
	ASSERT(BP_GET_NDVAS(bp) == ndvas);

	spa_config_exit(spa, SCL_ALLOC, FTAG);

	BP_SET_BIRTH(bp, txg, txg);

	return (0);
}

void
metaslab_free(spa_t *spa, const blkptr_t *bp, uint64_t txg, boolean_t now)
{
	const dva_t *dva = bp->blk_dva;
	int d, ndvas = BP_GET_NDVAS(bp);

	ASSERT(!BP_IS_HOLE(bp));
	ASSERT(!now || bp->blk_birth >= spa_syncing_txg(spa));

	spa_config_enter(spa, SCL_FREE, FTAG, RW_READER);

	for (d = 0; d < ndvas; d++)
		metaslab_free_dva(spa, &dva[d], txg, now);

	spa_config_exit(spa, SCL_FREE, FTAG);
}

int
metaslab_claim(spa_t *spa, const blkptr_t *bp, uint64_t txg)
{
	const dva_t *dva = bp->blk_dva;
	int ndvas = BP_GET_NDVAS(bp);
	int d, error = 0;

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

	for (d = 0; d < ndvas; d++)
		if ((error = metaslab_claim_dva(spa, &dva[d], txg)) != 0)
			break;

	spa_config_exit(spa, SCL_ALLOC, FTAG);

	ASSERT(error == 0 || txg == 0);

	return (error);
}
