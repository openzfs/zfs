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

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/dmu.h>
#include <sys/zio.h>
#include <sys/space_map.h>

/*
 * Space map routines.
 * NOTE: caller is responsible for all locking.
 */
static int
space_map_seg_compare(const void *x1, const void *x2)
{
	const space_seg_t *s1 = x1;
	const space_seg_t *s2 = x2;

	if (s1->ss_start < s2->ss_start) {
		if (s1->ss_end > s2->ss_start)
			return (0);
		return (-1);
	}
	if (s1->ss_start > s2->ss_start) {
		if (s1->ss_start < s2->ss_end)
			return (0);
		return (1);
	}
	return (0);
}

void
space_map_create(space_map_t *sm, uint64_t start, uint64_t size, uint8_t shift,
	kmutex_t *lp)
{
	bzero(sm, sizeof (*sm));

	avl_create(&sm->sm_root, space_map_seg_compare,
	    sizeof (space_seg_t), offsetof(struct space_seg, ss_node));

	sm->sm_start = start;
	sm->sm_size = size;
	sm->sm_shift = shift;
	sm->sm_lock = lp;
}

void
space_map_destroy(space_map_t *sm)
{
	ASSERT(!sm->sm_loaded && !sm->sm_loading);
	VERIFY3U(sm->sm_space, ==, 0);
	avl_destroy(&sm->sm_root);
}

void
space_map_add(space_map_t *sm, uint64_t start, uint64_t size)
{
	avl_index_t where;
	space_seg_t ssearch, *ss_before, *ss_after, *ss;
	uint64_t end = start + size;
	int merge_before, merge_after;

	ASSERT(MUTEX_HELD(sm->sm_lock));
	VERIFY(size != 0);
	VERIFY3U(start, >=, sm->sm_start);
	VERIFY3U(end, <=, sm->sm_start + sm->sm_size);
	VERIFY(sm->sm_space + size <= sm->sm_size);
	VERIFY(P2PHASE(start, 1ULL << sm->sm_shift) == 0);
	VERIFY(P2PHASE(size, 1ULL << sm->sm_shift) == 0);

	ssearch.ss_start = start;
	ssearch.ss_end = end;
	ss = avl_find(&sm->sm_root, &ssearch, &where);

	if (ss != NULL && ss->ss_start <= start && ss->ss_end >= end) {
		zfs_panic_recover("zfs: allocating allocated segment"
		    "(offset=%llu size=%llu)\n",
		    (longlong_t)start, (longlong_t)size);
		return;
	}

	/* Make sure we don't overlap with either of our neighbors */
	VERIFY(ss == NULL);

	ss_before = avl_nearest(&sm->sm_root, where, AVL_BEFORE);
	ss_after = avl_nearest(&sm->sm_root, where, AVL_AFTER);

	merge_before = (ss_before != NULL && ss_before->ss_end == start);
	merge_after = (ss_after != NULL && ss_after->ss_start == end);

	if (merge_before && merge_after) {
		avl_remove(&sm->sm_root, ss_before);
		ss_after->ss_start = ss_before->ss_start;
		kmem_free(ss_before, sizeof (*ss_before));
	} else if (merge_before) {
		ss_before->ss_end = end;
	} else if (merge_after) {
		ss_after->ss_start = start;
	} else {
		ss = kmem_alloc(sizeof (*ss), KM_SLEEP);
		ss->ss_start = start;
		ss->ss_end = end;
		avl_insert(&sm->sm_root, ss, where);
	}

	sm->sm_space += size;
}

void
space_map_remove(space_map_t *sm, uint64_t start, uint64_t size)
{
	avl_index_t where;
	space_seg_t ssearch, *ss, *newseg;
	uint64_t end = start + size;
	int left_over, right_over;

	ASSERT(MUTEX_HELD(sm->sm_lock));
	VERIFY(size != 0);
	VERIFY(P2PHASE(start, 1ULL << sm->sm_shift) == 0);
	VERIFY(P2PHASE(size, 1ULL << sm->sm_shift) == 0);

	ssearch.ss_start = start;
	ssearch.ss_end = end;
	ss = avl_find(&sm->sm_root, &ssearch, &where);

	/* Make sure we completely overlap with someone */
	if (ss == NULL) {
		zfs_panic_recover("zfs: freeing free segment "
		    "(offset=%llu size=%llu)",
		    (longlong_t)start, (longlong_t)size);
		return;
	}
	VERIFY3U(ss->ss_start, <=, start);
	VERIFY3U(ss->ss_end, >=, end);
	VERIFY(sm->sm_space - size <= sm->sm_size);

	left_over = (ss->ss_start != start);
	right_over = (ss->ss_end != end);

	if (left_over && right_over) {
		newseg = kmem_alloc(sizeof (*newseg), KM_SLEEP);
		newseg->ss_start = end;
		newseg->ss_end = ss->ss_end;
		ss->ss_end = start;
		avl_insert_here(&sm->sm_root, newseg, ss, AVL_AFTER);
	} else if (left_over) {
		ss->ss_end = start;
	} else if (right_over) {
		ss->ss_start = end;
	} else {
		avl_remove(&sm->sm_root, ss);
		kmem_free(ss, sizeof (*ss));
	}

	sm->sm_space -= size;
}

int
space_map_contains(space_map_t *sm, uint64_t start, uint64_t size)
{
	avl_index_t where;
	space_seg_t ssearch, *ss;
	uint64_t end = start + size;

	ASSERT(MUTEX_HELD(sm->sm_lock));
	VERIFY(size != 0);
	VERIFY(P2PHASE(start, 1ULL << sm->sm_shift) == 0);
	VERIFY(P2PHASE(size, 1ULL << sm->sm_shift) == 0);

	ssearch.ss_start = start;
	ssearch.ss_end = end;
	ss = avl_find(&sm->sm_root, &ssearch, &where);

	return (ss != NULL && ss->ss_start <= start && ss->ss_end >= end);
}

void
space_map_vacate(space_map_t *sm, space_map_func_t *func, space_map_t *mdest)
{
	space_seg_t *ss;
	void *cookie = NULL;

	ASSERT(MUTEX_HELD(sm->sm_lock));

	while ((ss = avl_destroy_nodes(&sm->sm_root, &cookie)) != NULL) {
		if (func != NULL)
			func(mdest, ss->ss_start, ss->ss_end - ss->ss_start);
		kmem_free(ss, sizeof (*ss));
	}
	sm->sm_space = 0;
}

void
space_map_walk(space_map_t *sm, space_map_func_t *func, space_map_t *mdest)
{
	space_seg_t *ss;

	for (ss = avl_first(&sm->sm_root); ss; ss = AVL_NEXT(&sm->sm_root, ss))
		func(mdest, ss->ss_start, ss->ss_end - ss->ss_start);
}

void
space_map_excise(space_map_t *sm, uint64_t start, uint64_t size)
{
	avl_tree_t *t = &sm->sm_root;
	avl_index_t where;
	space_seg_t *ss, search;
	uint64_t end = start + size;
	uint64_t rm_start, rm_end;

	ASSERT(MUTEX_HELD(sm->sm_lock));

	search.ss_start = start;
	search.ss_end = start;

	for (;;) {
		ss = avl_find(t, &search, &where);

		if (ss == NULL)
			ss = avl_nearest(t, where, AVL_AFTER);

		if (ss == NULL || ss->ss_start >= end)
			break;

		rm_start = MAX(ss->ss_start, start);
		rm_end = MIN(ss->ss_end, end);

		space_map_remove(sm, rm_start, rm_end - rm_start);
	}
}

/*
 * Replace smd with the union of smd and sms.
 */
void
space_map_union(space_map_t *smd, space_map_t *sms)
{
	avl_tree_t *t = &sms->sm_root;
	space_seg_t *ss;

	ASSERT(MUTEX_HELD(smd->sm_lock));

	/*
	 * For each source segment, remove any intersections with the
	 * destination, then add the source segment to the destination.
	 */
	for (ss = avl_first(t); ss != NULL; ss = AVL_NEXT(t, ss)) {
		space_map_excise(smd, ss->ss_start, ss->ss_end - ss->ss_start);
		space_map_add(smd, ss->ss_start, ss->ss_end - ss->ss_start);
	}
}

/*
 * Wait for any in-progress space_map_load() to complete.
 */
void
space_map_load_wait(space_map_t *sm)
{
	ASSERT(MUTEX_HELD(sm->sm_lock));

	while (sm->sm_loading)
		cv_wait(&sm->sm_load_cv, sm->sm_lock);
}

/*
 * Note: space_map_load() will drop sm_lock across dmu_read() calls.
 * The caller must be OK with this.
 */
int
space_map_load(space_map_t *sm, space_map_ops_t *ops, uint8_t maptype,
	space_map_obj_t *smo, objset_t *os)
{
	uint64_t *entry, *entry_map, *entry_map_end;
	uint64_t bufsize, size, offset, end, space;
	uint64_t mapstart = sm->sm_start;
	int error = 0;

	ASSERT(MUTEX_HELD(sm->sm_lock));

	space_map_load_wait(sm);

	if (sm->sm_loaded)
		return (0);

	sm->sm_loading = B_TRUE;
	end = smo->smo_objsize;
	space = smo->smo_alloc;

	ASSERT(sm->sm_ops == NULL);
	VERIFY3U(sm->sm_space, ==, 0);

	if (maptype == SM_FREE) {
		space_map_add(sm, sm->sm_start, sm->sm_size);
		space = sm->sm_size - space;
	}

	bufsize = 1ULL << SPACE_MAP_BLOCKSHIFT;
	entry_map = zio_buf_alloc(bufsize);

	mutex_exit(sm->sm_lock);
	if (end > bufsize)
		dmu_prefetch(os, smo->smo_object, bufsize, end - bufsize);
	mutex_enter(sm->sm_lock);

	for (offset = 0; offset < end; offset += bufsize) {
		size = MIN(end - offset, bufsize);
		VERIFY(P2PHASE(size, sizeof (uint64_t)) == 0);
		VERIFY(size != 0);

		dprintf("object=%llu  offset=%llx  size=%llx\n",
		    smo->smo_object, offset, size);

		mutex_exit(sm->sm_lock);
		error = dmu_read(os, smo->smo_object, offset, size, entry_map);
		mutex_enter(sm->sm_lock);
		if (error != 0)
			break;

		entry_map_end = entry_map + (size / sizeof (uint64_t));
		for (entry = entry_map; entry < entry_map_end; entry++) {
			uint64_t e = *entry;

			if (SM_DEBUG_DECODE(e))		/* Skip debug entries */
				continue;

			(SM_TYPE_DECODE(e) == maptype ?
			    space_map_add : space_map_remove)(sm,
			    (SM_OFFSET_DECODE(e) << sm->sm_shift) + mapstart,
			    SM_RUN_DECODE(e) << sm->sm_shift);
		}
	}

	if (error == 0) {
		VERIFY3U(sm->sm_space, ==, space);

		sm->sm_loaded = B_TRUE;
		sm->sm_ops = ops;
		if (ops != NULL)
			ops->smop_load(sm);
	} else {
		space_map_vacate(sm, NULL, NULL);
	}

	zio_buf_free(entry_map, bufsize);

	sm->sm_loading = B_FALSE;

	cv_broadcast(&sm->sm_load_cv);

	return (error);
}

void
space_map_unload(space_map_t *sm)
{
	ASSERT(MUTEX_HELD(sm->sm_lock));

	if (sm->sm_loaded && sm->sm_ops != NULL)
		sm->sm_ops->smop_unload(sm);

	sm->sm_loaded = B_FALSE;
	sm->sm_ops = NULL;

	space_map_vacate(sm, NULL, NULL);
}

uint64_t
space_map_alloc(space_map_t *sm, uint64_t size)
{
	uint64_t start;

	start = sm->sm_ops->smop_alloc(sm, size);
	if (start != -1ULL)
		space_map_remove(sm, start, size);
	return (start);
}

void
space_map_claim(space_map_t *sm, uint64_t start, uint64_t size)
{
	sm->sm_ops->smop_claim(sm, start, size);
	space_map_remove(sm, start, size);
}

void
space_map_free(space_map_t *sm, uint64_t start, uint64_t size)
{
	space_map_add(sm, start, size);
	sm->sm_ops->smop_free(sm, start, size);
}

/*
 * Note: space_map_sync() will drop sm_lock across dmu_write() calls.
 */
void
space_map_sync(space_map_t *sm, uint8_t maptype,
	space_map_obj_t *smo, objset_t *os, dmu_tx_t *tx)
{
	spa_t *spa = dmu_objset_spa(os);
	void *cookie = NULL;
	space_seg_t *ss;
	uint64_t bufsize, start, size, run_len;
	uint64_t *entry, *entry_map, *entry_map_end;

	ASSERT(MUTEX_HELD(sm->sm_lock));

	if (sm->sm_space == 0)
		return;

	dprintf("object %4llu, txg %llu, pass %d, %c, count %lu, space %llx\n",
	    smo->smo_object, dmu_tx_get_txg(tx), spa_sync_pass(spa),
	    maptype == SM_ALLOC ? 'A' : 'F', avl_numnodes(&sm->sm_root),
	    sm->sm_space);

	if (maptype == SM_ALLOC)
		smo->smo_alloc += sm->sm_space;
	else
		smo->smo_alloc -= sm->sm_space;

	bufsize = (8 + avl_numnodes(&sm->sm_root)) * sizeof (uint64_t);
	bufsize = MIN(bufsize, 1ULL << SPACE_MAP_BLOCKSHIFT);
	entry_map = zio_buf_alloc(bufsize);
	entry_map_end = entry_map + (bufsize / sizeof (uint64_t));
	entry = entry_map;

	*entry++ = SM_DEBUG_ENCODE(1) |
	    SM_DEBUG_ACTION_ENCODE(maptype) |
	    SM_DEBUG_SYNCPASS_ENCODE(spa_sync_pass(spa)) |
	    SM_DEBUG_TXG_ENCODE(dmu_tx_get_txg(tx));

	while ((ss = avl_destroy_nodes(&sm->sm_root, &cookie)) != NULL) {
		size = ss->ss_end - ss->ss_start;
		start = (ss->ss_start - sm->sm_start) >> sm->sm_shift;

		sm->sm_space -= size;
		size >>= sm->sm_shift;

		while (size) {
			run_len = MIN(size, SM_RUN_MAX);

			if (entry == entry_map_end) {
				mutex_exit(sm->sm_lock);
				dmu_write(os, smo->smo_object, smo->smo_objsize,
				    bufsize, entry_map, tx);
				mutex_enter(sm->sm_lock);
				smo->smo_objsize += bufsize;
				entry = entry_map;
			}

			*entry++ = SM_OFFSET_ENCODE(start) |
			    SM_TYPE_ENCODE(maptype) |
			    SM_RUN_ENCODE(run_len);

			start += run_len;
			size -= run_len;
		}
		kmem_free(ss, sizeof (*ss));
	}

	if (entry != entry_map) {
		size = (entry - entry_map) * sizeof (uint64_t);
		mutex_exit(sm->sm_lock);
		dmu_write(os, smo->smo_object, smo->smo_objsize,
		    size, entry_map, tx);
		mutex_enter(sm->sm_lock);
		smo->smo_objsize += size;
	}

	zio_buf_free(entry_map, bufsize);

	VERIFY3U(sm->sm_space, ==, 0);
}

void
space_map_truncate(space_map_obj_t *smo, objset_t *os, dmu_tx_t *tx)
{
	VERIFY(dmu_free_range(os, smo->smo_object, 0, -1ULL, tx) == 0);

	smo->smo_objsize = 0;
	smo->smo_alloc = 0;
}
