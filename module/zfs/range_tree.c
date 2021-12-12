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
 * Copyright (c) 2013, 2019 by Delphix. All rights reserved.
 * Copyright (c) 2015, Nexenta Systems, Inc. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/dmu.h>
#include <sys/dnode.h>
#include <sys/zio.h>
#include <sys/range_tree.h>

/*
 * Range trees are tree-based data structures that can be used to
 * track free space or generally any space allocation information.
 * A range tree keeps track of individual segments and automatically
 * provides facilities such as adjacent extent merging and extent
 * splitting in response to range add/remove requests.
 *
 * A range tree starts out completely empty, with no segments in it.
 * Adding an allocation via range_tree_add to the range tree can either:
 * 1) create a new extent
 * 2) extend an adjacent extent
 * 3) merge two adjacent extents
 * Conversely, removing an allocation via range_tree_remove can:
 * 1) completely remove an extent
 * 2) shorten an extent (if the allocation was near one of its ends)
 * 3) split an extent into two extents, in effect punching a hole
 *
 * A range tree is also capable of 'bridging' gaps when adding
 * allocations. This is useful for cases when close proximity of
 * allocations is an important detail that needs to be represented
 * in the range tree. See range_tree_set_gap(). The default behavior
 * is not to bridge gaps (i.e. the maximum allowed gap size is 0).
 *
 * In order to traverse a range tree, use either the range_tree_walk()
 * or range_tree_vacate() functions.
 *
 * To obtain more accurate information on individual segment
 * operations that the range tree performs "under the hood", you can
 * specify a set of callbacks by passing a range_tree_ops_t structure
 * to the range_tree_create function. Any callbacks that are non-NULL
 * are then called at the appropriate times.
 *
 * The range tree code also supports a special variant of range trees
 * that can bridge small gaps between segments. This kind of tree is used
 * by the dsl scanning code to group I/Os into mostly sequential chunks to
 * optimize disk performance. The code here attempts to do this with as
 * little memory and computational overhead as possible. One limitation of
 * this implementation is that segments of range trees with gaps can only
 * support removing complete segments.
 */

static inline void
rs_copy(range_seg_t *src, range_seg_t *dest, range_tree_t *rt)
{
	ASSERT3U(rt->rt_type, <=, RANGE_SEG_NUM_TYPES);
	size_t size = 0;
	switch (rt->rt_type) {
	case RANGE_SEG32:
		size = sizeof (range_seg32_t);
		break;
	case RANGE_SEG64:
		size = sizeof (range_seg64_t);
		break;
	case RANGE_SEG_GAP:
		size = sizeof (range_seg_gap_t);
		break;
	default:
		VERIFY(0);
	}
	bcopy(src, dest, size);
}

void
range_tree_stat_verify(range_tree_t *rt)
{
	range_seg_t *rs;
	zfs_btree_index_t where;
	uint64_t hist[RANGE_TREE_HISTOGRAM_SIZE] = { 0 };
	int i;

	for (rs = zfs_btree_first(&rt->rt_root, &where); rs != NULL;
	    rs = zfs_btree_next(&rt->rt_root, &where, &where)) {
		uint64_t size = rs_get_end(rs, rt) - rs_get_start(rs, rt);
		int idx	= highbit64(size) - 1;

		hist[idx]++;
		ASSERT3U(hist[idx], !=, 0);
	}

	for (i = 0; i < RANGE_TREE_HISTOGRAM_SIZE; i++) {
		if (hist[i] != rt->rt_histogram[i]) {
			zfs_dbgmsg("i=%d, hist=%px, hist=%llu, rt_hist=%llu",
			    i, hist, (u_longlong_t)hist[i],
			    (u_longlong_t)rt->rt_histogram[i]);
		}
		VERIFY3U(hist[i], ==, rt->rt_histogram[i]);
	}
}

static void
range_tree_stat_incr(range_tree_t *rt, range_seg_t *rs)
{
	uint64_t size = rs_get_end(rs, rt) - rs_get_start(rs, rt);
	int idx = highbit64(size) - 1;

	ASSERT(size != 0);
	ASSERT3U(idx, <,
	    sizeof (rt->rt_histogram) / sizeof (*rt->rt_histogram));

	rt->rt_histogram[idx]++;
	ASSERT3U(rt->rt_histogram[idx], !=, 0);
}

static void
range_tree_stat_decr(range_tree_t *rt, range_seg_t *rs)
{
	uint64_t size = rs_get_end(rs, rt) - rs_get_start(rs, rt);
	int idx = highbit64(size) - 1;

	ASSERT(size != 0);
	ASSERT3U(idx, <,
	    sizeof (rt->rt_histogram) / sizeof (*rt->rt_histogram));

	ASSERT3U(rt->rt_histogram[idx], !=, 0);
	rt->rt_histogram[idx]--;
}

static int
range_tree_seg32_compare(const void *x1, const void *x2)
{
	const range_seg32_t *r1 = x1;
	const range_seg32_t *r2 = x2;

	ASSERT3U(r1->rs_start, <=, r1->rs_end);
	ASSERT3U(r2->rs_start, <=, r2->rs_end);

	return ((r1->rs_start >= r2->rs_end) - (r1->rs_end <= r2->rs_start));
}

static int
range_tree_seg64_compare(const void *x1, const void *x2)
{
	const range_seg64_t *r1 = x1;
	const range_seg64_t *r2 = x2;

	ASSERT3U(r1->rs_start, <=, r1->rs_end);
	ASSERT3U(r2->rs_start, <=, r2->rs_end);

	return ((r1->rs_start >= r2->rs_end) - (r1->rs_end <= r2->rs_start));
}

static int
range_tree_seg_gap_compare(const void *x1, const void *x2)
{
	const range_seg_gap_t *r1 = x1;
	const range_seg_gap_t *r2 = x2;

	ASSERT3U(r1->rs_start, <=, r1->rs_end);
	ASSERT3U(r2->rs_start, <=, r2->rs_end);

	return ((r1->rs_start >= r2->rs_end) - (r1->rs_end <= r2->rs_start));
}

range_tree_t *
range_tree_create_impl(range_tree_ops_t *ops, range_seg_type_t type, void *arg,
    uint64_t start, uint64_t shift,
    int (*zfs_btree_compare) (const void *, const void *),
    uint64_t gap)
{
	range_tree_t *rt = kmem_zalloc(sizeof (range_tree_t), KM_SLEEP);

	ASSERT3U(shift, <, 64);
	ASSERT3U(type, <=, RANGE_SEG_NUM_TYPES);
	size_t size;
	int (*compare) (const void *, const void *);
	switch (type) {
	case RANGE_SEG32:
		size = sizeof (range_seg32_t);
		compare = range_tree_seg32_compare;
		break;
	case RANGE_SEG64:
		size = sizeof (range_seg64_t);
		compare = range_tree_seg64_compare;
		break;
	case RANGE_SEG_GAP:
		size = sizeof (range_seg_gap_t);
		compare = range_tree_seg_gap_compare;
		break;
	default:
		panic("Invalid range seg type %d", type);
	}
	zfs_btree_create(&rt->rt_root, compare, size);

	rt->rt_ops = ops;
	rt->rt_gap = gap;
	rt->rt_arg = arg;
	rt->rt_type = type;
	rt->rt_start = start;
	rt->rt_shift = shift;
	rt->rt_btree_compare = zfs_btree_compare;

	if (rt->rt_ops != NULL && rt->rt_ops->rtop_create != NULL)
		rt->rt_ops->rtop_create(rt, rt->rt_arg);

	return (rt);
}

range_tree_t *
range_tree_create(range_tree_ops_t *ops, range_seg_type_t type,
    void *arg, uint64_t start, uint64_t shift)
{
	return (range_tree_create_impl(ops, type, arg, start, shift, NULL, 0));
}

void
range_tree_destroy(range_tree_t *rt)
{
	VERIFY0(rt->rt_space);

	if (rt->rt_ops != NULL && rt->rt_ops->rtop_destroy != NULL)
		rt->rt_ops->rtop_destroy(rt, rt->rt_arg);

	zfs_btree_destroy(&rt->rt_root);
	kmem_free(rt, sizeof (*rt));
}

void
range_tree_adjust_fill(range_tree_t *rt, range_seg_t *rs, int64_t delta)
{
	if (delta < 0 && delta * -1 >= rs_get_fill(rs, rt)) {
		zfs_panic_recover("zfs: attempting to decrease fill to or "
		    "below 0; probable double remove in segment [%llx:%llx]",
		    (longlong_t)rs_get_start(rs, rt),
		    (longlong_t)rs_get_end(rs, rt));
	}
	if (rs_get_fill(rs, rt) + delta > rs_get_end(rs, rt) -
	    rs_get_start(rs, rt)) {
		zfs_panic_recover("zfs: attempting to increase fill beyond "
		    "max; probable double add in segment [%llx:%llx]",
		    (longlong_t)rs_get_start(rs, rt),
		    (longlong_t)rs_get_end(rs, rt));
	}

	if (rt->rt_ops != NULL && rt->rt_ops->rtop_remove != NULL)
		rt->rt_ops->rtop_remove(rt, rs, rt->rt_arg);
	rs_set_fill(rs, rt, rs_get_fill(rs, rt) + delta);
	if (rt->rt_ops != NULL && rt->rt_ops->rtop_add != NULL)
		rt->rt_ops->rtop_add(rt, rs, rt->rt_arg);
}

static void
range_tree_add_impl(void *arg, uint64_t start, uint64_t size, uint64_t fill)
{
	range_tree_t *rt = arg;
	zfs_btree_index_t where;
	range_seg_t *rs_before, *rs_after, *rs;
	range_seg_max_t tmp, rsearch;
	uint64_t end = start + size, gap = rt->rt_gap;
	uint64_t bridge_size = 0;
	boolean_t merge_before, merge_after;

	ASSERT3U(size, !=, 0);
	ASSERT3U(fill, <=, size);
	ASSERT3U(start + size, >, start);

	rs_set_start(&rsearch, rt, start);
	rs_set_end(&rsearch, rt, end);
	rs = zfs_btree_find(&rt->rt_root, &rsearch, &where);

	/*
	 * If this is a gap-supporting range tree, it is possible that we
	 * are inserting into an existing segment. In this case simply
	 * bump the fill count and call the remove / add callbacks. If the
	 * new range will extend an existing segment, we remove the
	 * existing one, apply the new extent to it and re-insert it using
	 * the normal code paths.
	 */
	if (rs != NULL) {
		if (gap == 0) {
			zfs_panic_recover("zfs: adding existent segment to "
			    "range tree (offset=%llx size=%llx)",
			    (longlong_t)start, (longlong_t)size);
			return;
		}
		uint64_t rstart = rs_get_start(rs, rt);
		uint64_t rend = rs_get_end(rs, rt);
		if (rstart <= start && rend >= end) {
			range_tree_adjust_fill(rt, rs, fill);
			return;
		}

		if (rt->rt_ops != NULL && rt->rt_ops->rtop_remove != NULL)
			rt->rt_ops->rtop_remove(rt, rs, rt->rt_arg);

		range_tree_stat_decr(rt, rs);
		rt->rt_space -= rend - rstart;

		fill += rs_get_fill(rs, rt);
		start = MIN(start, rstart);
		end = MAX(end, rend);
		size = end - start;

		zfs_btree_remove(&rt->rt_root, rs);
		range_tree_add_impl(rt, start, size, fill);
		return;
	}

	ASSERT3P(rs, ==, NULL);

	/*
	 * Determine whether or not we will have to merge with our neighbors.
	 * If gap != 0, we might need to merge with our neighbors even if we
	 * aren't directly touching.
	 */
	zfs_btree_index_t where_before, where_after;
	rs_before = zfs_btree_prev(&rt->rt_root, &where, &where_before);
	rs_after = zfs_btree_next(&rt->rt_root, &where, &where_after);

	merge_before = (rs_before != NULL && rs_get_end(rs_before, rt) >=
	    start - gap);
	merge_after = (rs_after != NULL && rs_get_start(rs_after, rt) <= end +
	    gap);

	if (merge_before && gap != 0)
		bridge_size += start - rs_get_end(rs_before, rt);
	if (merge_after && gap != 0)
		bridge_size += rs_get_start(rs_after, rt) - end;

	if (merge_before && merge_after) {
		if (rt->rt_ops != NULL && rt->rt_ops->rtop_remove != NULL) {
			rt->rt_ops->rtop_remove(rt, rs_before, rt->rt_arg);
			rt->rt_ops->rtop_remove(rt, rs_after, rt->rt_arg);
		}

		range_tree_stat_decr(rt, rs_before);
		range_tree_stat_decr(rt, rs_after);

		rs_copy(rs_after, &tmp, rt);
		uint64_t before_start = rs_get_start_raw(rs_before, rt);
		uint64_t before_fill = rs_get_fill(rs_before, rt);
		uint64_t after_fill = rs_get_fill(rs_after, rt);
		zfs_btree_remove_idx(&rt->rt_root, &where_before);

		/*
		 * We have to re-find the node because our old reference is
		 * invalid as soon as we do any mutating btree operations.
		 */
		rs_after = zfs_btree_find(&rt->rt_root, &tmp, &where_after);
		rs_set_start_raw(rs_after, rt, before_start);
		rs_set_fill(rs_after, rt, after_fill + before_fill + fill);
		rs = rs_after;
	} else if (merge_before) {
		if (rt->rt_ops != NULL && rt->rt_ops->rtop_remove != NULL)
			rt->rt_ops->rtop_remove(rt, rs_before, rt->rt_arg);

		range_tree_stat_decr(rt, rs_before);

		uint64_t before_fill = rs_get_fill(rs_before, rt);
		rs_set_end(rs_before, rt, end);
		rs_set_fill(rs_before, rt, before_fill + fill);
		rs = rs_before;
	} else if (merge_after) {
		if (rt->rt_ops != NULL && rt->rt_ops->rtop_remove != NULL)
			rt->rt_ops->rtop_remove(rt, rs_after, rt->rt_arg);

		range_tree_stat_decr(rt, rs_after);

		uint64_t after_fill = rs_get_fill(rs_after, rt);
		rs_set_start(rs_after, rt, start);
		rs_set_fill(rs_after, rt, after_fill + fill);
		rs = rs_after;
	} else {
		rs = &tmp;

		rs_set_start(rs, rt, start);
		rs_set_end(rs, rt, end);
		rs_set_fill(rs, rt, fill);
		zfs_btree_add_idx(&rt->rt_root, rs, &where);
	}

	if (gap != 0) {
		ASSERT3U(rs_get_fill(rs, rt), <=, rs_get_end(rs, rt) -
		    rs_get_start(rs, rt));
	} else {
		ASSERT3U(rs_get_fill(rs, rt), ==, rs_get_end(rs, rt) -
		    rs_get_start(rs, rt));
	}

	if (rt->rt_ops != NULL && rt->rt_ops->rtop_add != NULL)
		rt->rt_ops->rtop_add(rt, rs, rt->rt_arg);

	range_tree_stat_incr(rt, rs);
	rt->rt_space += size + bridge_size;
}

void
range_tree_add(void *arg, uint64_t start, uint64_t size)
{
	range_tree_add_impl(arg, start, size, size);
}

static void
range_tree_remove_impl(range_tree_t *rt, uint64_t start, uint64_t size,
    boolean_t do_fill)
{
	zfs_btree_index_t where;
	range_seg_t *rs;
	range_seg_max_t rsearch, rs_tmp;
	uint64_t end = start + size;
	boolean_t left_over, right_over;

	VERIFY3U(size, !=, 0);
	VERIFY3U(size, <=, rt->rt_space);
	if (rt->rt_type == RANGE_SEG64)
		ASSERT3U(start + size, >, start);

	rs_set_start(&rsearch, rt, start);
	rs_set_end(&rsearch, rt, end);
	rs = zfs_btree_find(&rt->rt_root, &rsearch, &where);

	/* Make sure we completely overlap with someone */
	if (rs == NULL) {
		zfs_panic_recover("zfs: removing nonexistent segment from "
		    "range tree (offset=%llx size=%llx)",
		    (longlong_t)start, (longlong_t)size);
		return;
	}

	/*
	 * Range trees with gap support must only remove complete segments
	 * from the tree. This allows us to maintain accurate fill accounting
	 * and to ensure that bridged sections are not leaked. If we need to
	 * remove less than the full segment, we can only adjust the fill count.
	 */
	if (rt->rt_gap != 0) {
		if (do_fill) {
			if (rs_get_fill(rs, rt) == size) {
				start = rs_get_start(rs, rt);
				end = rs_get_end(rs, rt);
				size = end - start;
			} else {
				range_tree_adjust_fill(rt, rs, -size);
				return;
			}
		} else if (rs_get_start(rs, rt) != start ||
		    rs_get_end(rs, rt) != end) {
			zfs_panic_recover("zfs: freeing partial segment of "
			    "gap tree (offset=%llx size=%llx) of "
			    "(offset=%llx size=%llx)",
			    (longlong_t)start, (longlong_t)size,
			    (longlong_t)rs_get_start(rs, rt),
			    (longlong_t)rs_get_end(rs, rt) - rs_get_start(rs,
			    rt));
			return;
		}
	}

	VERIFY3U(rs_get_start(rs, rt), <=, start);
	VERIFY3U(rs_get_end(rs, rt), >=, end);

	left_over = (rs_get_start(rs, rt) != start);
	right_over = (rs_get_end(rs, rt) != end);

	range_tree_stat_decr(rt, rs);

	if (rt->rt_ops != NULL && rt->rt_ops->rtop_remove != NULL)
		rt->rt_ops->rtop_remove(rt, rs, rt->rt_arg);

	if (left_over && right_over) {
		range_seg_max_t newseg;
		rs_set_start(&newseg, rt, end);
		rs_set_end_raw(&newseg, rt, rs_get_end_raw(rs, rt));
		rs_set_fill(&newseg, rt, rs_get_end(rs, rt) - end);
		range_tree_stat_incr(rt, &newseg);

		// This modifies the buffer already inside the range tree
		rs_set_end(rs, rt, start);

		rs_copy(rs, &rs_tmp, rt);
		if (zfs_btree_next(&rt->rt_root, &where, &where) != NULL)
			zfs_btree_add_idx(&rt->rt_root, &newseg, &where);
		else
			zfs_btree_add(&rt->rt_root, &newseg);

		if (rt->rt_ops != NULL && rt->rt_ops->rtop_add != NULL)
			rt->rt_ops->rtop_add(rt, &newseg, rt->rt_arg);
	} else if (left_over) {
		// This modifies the buffer already inside the range tree
		rs_set_end(rs, rt, start);
		rs_copy(rs, &rs_tmp, rt);
	} else if (right_over) {
		// This modifies the buffer already inside the range tree
		rs_set_start(rs, rt, end);
		rs_copy(rs, &rs_tmp, rt);
	} else {
		zfs_btree_remove_idx(&rt->rt_root, &where);
		rs = NULL;
	}

	if (rs != NULL) {
		/*
		 * The fill of the leftover segment will always be equal to
		 * the size, since we do not support removing partial segments
		 * of range trees with gaps.
		 */
		rs_set_fill_raw(rs, rt, rs_get_end_raw(rs, rt) -
		    rs_get_start_raw(rs, rt));
		range_tree_stat_incr(rt, &rs_tmp);

		if (rt->rt_ops != NULL && rt->rt_ops->rtop_add != NULL)
			rt->rt_ops->rtop_add(rt, &rs_tmp, rt->rt_arg);
	}

	rt->rt_space -= size;
}

void
range_tree_remove(void *arg, uint64_t start, uint64_t size)
{
	range_tree_remove_impl(arg, start, size, B_FALSE);
}

void
range_tree_remove_fill(range_tree_t *rt, uint64_t start, uint64_t size)
{
	range_tree_remove_impl(rt, start, size, B_TRUE);
}

void
range_tree_resize_segment(range_tree_t *rt, range_seg_t *rs,
    uint64_t newstart, uint64_t newsize)
{
	int64_t delta = newsize - (rs_get_end(rs, rt) - rs_get_start(rs, rt));

	range_tree_stat_decr(rt, rs);
	if (rt->rt_ops != NULL && rt->rt_ops->rtop_remove != NULL)
		rt->rt_ops->rtop_remove(rt, rs, rt->rt_arg);

	rs_set_start(rs, rt, newstart);
	rs_set_end(rs, rt, newstart + newsize);

	range_tree_stat_incr(rt, rs);
	if (rt->rt_ops != NULL && rt->rt_ops->rtop_add != NULL)
		rt->rt_ops->rtop_add(rt, rs, rt->rt_arg);

	rt->rt_space += delta;
}

static range_seg_t *
range_tree_find_impl(range_tree_t *rt, uint64_t start, uint64_t size)
{
	range_seg_max_t rsearch;
	uint64_t end = start + size;

	VERIFY(size != 0);

	rs_set_start(&rsearch, rt, start);
	rs_set_end(&rsearch, rt, end);
	return (zfs_btree_find(&rt->rt_root, &rsearch, NULL));
}

range_seg_t *
range_tree_find(range_tree_t *rt, uint64_t start, uint64_t size)
{
	if (rt->rt_type == RANGE_SEG64)
		ASSERT3U(start + size, >, start);

	range_seg_t *rs = range_tree_find_impl(rt, start, size);
	if (rs != NULL && rs_get_start(rs, rt) <= start &&
	    rs_get_end(rs, rt) >= start + size) {
		return (rs);
	}
	return (NULL);
}

void
range_tree_verify_not_present(range_tree_t *rt, uint64_t off, uint64_t size)
{
	range_seg_t *rs = range_tree_find(rt, off, size);
	if (rs != NULL)
		panic("segment already in tree; rs=%p", (void *)rs);
}

boolean_t
range_tree_contains(range_tree_t *rt, uint64_t start, uint64_t size)
{
	return (range_tree_find(rt, start, size) != NULL);
}

/*
 * Returns the first subset of the given range which overlaps with the range
 * tree. Returns true if there is a segment in the range, and false if there
 * isn't.
 */
boolean_t
range_tree_find_in(range_tree_t *rt, uint64_t start, uint64_t size,
    uint64_t *ostart, uint64_t *osize)
{
	if (rt->rt_type == RANGE_SEG64)
		ASSERT3U(start + size, >, start);

	range_seg_max_t rsearch;
	rs_set_start(&rsearch, rt, start);
	rs_set_end_raw(&rsearch, rt, rs_get_start_raw(&rsearch, rt) + 1);

	zfs_btree_index_t where;
	range_seg_t *rs = zfs_btree_find(&rt->rt_root, &rsearch, &where);
	if (rs != NULL) {
		*ostart = start;
		*osize = MIN(size, rs_get_end(rs, rt) - start);
		return (B_TRUE);
	}

	rs = zfs_btree_next(&rt->rt_root, &where, &where);
	if (rs == NULL || rs_get_start(rs, rt) > start + size)
		return (B_FALSE);

	*ostart = rs_get_start(rs, rt);
	*osize = MIN(start + size, rs_get_end(rs, rt)) -
	    rs_get_start(rs, rt);
	return (B_TRUE);
}

/*
 * Ensure that this range is not in the tree, regardless of whether
 * it is currently in the tree.
 */
void
range_tree_clear(range_tree_t *rt, uint64_t start, uint64_t size)
{
	range_seg_t *rs;

	if (size == 0)
		return;

	if (rt->rt_type == RANGE_SEG64)
		ASSERT3U(start + size, >, start);

	while ((rs = range_tree_find_impl(rt, start, size)) != NULL) {
		uint64_t free_start = MAX(rs_get_start(rs, rt), start);
		uint64_t free_end = MIN(rs_get_end(rs, rt), start + size);
		range_tree_remove(rt, free_start, free_end - free_start);
	}
}

void
range_tree_swap(range_tree_t **rtsrc, range_tree_t **rtdst)
{
	range_tree_t *rt;

	ASSERT0(range_tree_space(*rtdst));
	ASSERT0(zfs_btree_numnodes(&(*rtdst)->rt_root));

	rt = *rtsrc;
	*rtsrc = *rtdst;
	*rtdst = rt;
}

void
range_tree_vacate(range_tree_t *rt, range_tree_func_t *func, void *arg)
{
	if (rt->rt_ops != NULL && rt->rt_ops->rtop_vacate != NULL)
		rt->rt_ops->rtop_vacate(rt, rt->rt_arg);

	if (func != NULL) {
		range_seg_t *rs;
		zfs_btree_index_t *cookie = NULL;

		while ((rs = zfs_btree_destroy_nodes(&rt->rt_root, &cookie)) !=
		    NULL) {
			func(arg, rs_get_start(rs, rt), rs_get_end(rs, rt) -
			    rs_get_start(rs, rt));
		}
	} else {
		zfs_btree_clear(&rt->rt_root);
	}

	bzero(rt->rt_histogram, sizeof (rt->rt_histogram));
	rt->rt_space = 0;
}

void
range_tree_walk(range_tree_t *rt, range_tree_func_t *func, void *arg)
{
	zfs_btree_index_t where;
	for (range_seg_t *rs = zfs_btree_first(&rt->rt_root, &where);
	    rs != NULL; rs = zfs_btree_next(&rt->rt_root, &where, &where)) {
		func(arg, rs_get_start(rs, rt), rs_get_end(rs, rt) -
		    rs_get_start(rs, rt));
	}
}

range_seg_t *
range_tree_first(range_tree_t *rt)
{
	return (zfs_btree_first(&rt->rt_root, NULL));
}

uint64_t
range_tree_space(range_tree_t *rt)
{
	return (rt->rt_space);
}

uint64_t
range_tree_numsegs(range_tree_t *rt)
{
	return ((rt == NULL) ? 0 : zfs_btree_numnodes(&rt->rt_root));
}

boolean_t
range_tree_is_empty(range_tree_t *rt)
{
	ASSERT(rt != NULL);
	return (range_tree_space(rt) == 0);
}

void
rt_btree_create(range_tree_t *rt, void *arg)
{
	zfs_btree_t *size_tree = arg;

	size_t size;
	switch (rt->rt_type) {
	case RANGE_SEG32:
		size = sizeof (range_seg32_t);
		break;
	case RANGE_SEG64:
		size = sizeof (range_seg64_t);
		break;
	case RANGE_SEG_GAP:
		size = sizeof (range_seg_gap_t);
		break;
	default:
		panic("Invalid range seg type %d", rt->rt_type);
	}
	zfs_btree_create(size_tree, rt->rt_btree_compare, size);
}

void
rt_btree_destroy(range_tree_t *rt, void *arg)
{
	(void) rt;
	zfs_btree_t *size_tree = arg;
	ASSERT0(zfs_btree_numnodes(size_tree));

	zfs_btree_destroy(size_tree);
}

void
rt_btree_add(range_tree_t *rt, range_seg_t *rs, void *arg)
{
	(void) rt;
	zfs_btree_t *size_tree = arg;

	zfs_btree_add(size_tree, rs);
}

void
rt_btree_remove(range_tree_t *rt, range_seg_t *rs, void *arg)
{
	(void) rt;
	zfs_btree_t *size_tree = arg;

	zfs_btree_remove(size_tree, rs);
}

void
rt_btree_vacate(range_tree_t *rt, void *arg)
{
	zfs_btree_t *size_tree = arg;
	zfs_btree_clear(size_tree);
	zfs_btree_destroy(size_tree);

	rt_btree_create(rt, arg);
}

range_tree_ops_t rt_btree_ops = {
	.rtop_create = rt_btree_create,
	.rtop_destroy = rt_btree_destroy,
	.rtop_add = rt_btree_add,
	.rtop_remove = rt_btree_remove,
	.rtop_vacate = rt_btree_vacate
};

/*
 * Remove any overlapping ranges between the given segment [start, end)
 * from removefrom. Add non-overlapping leftovers to addto.
 */
void
range_tree_remove_xor_add_segment(uint64_t start, uint64_t end,
    range_tree_t *removefrom, range_tree_t *addto)
{
	zfs_btree_index_t where;
	range_seg_max_t starting_rs;
	rs_set_start(&starting_rs, removefrom, start);
	rs_set_end_raw(&starting_rs, removefrom, rs_get_start_raw(&starting_rs,
	    removefrom) + 1);

	range_seg_t *curr = zfs_btree_find(&removefrom->rt_root,
	    &starting_rs, &where);

	if (curr == NULL)
		curr = zfs_btree_next(&removefrom->rt_root, &where, &where);

	range_seg_t *next;
	for (; curr != NULL; curr = next) {
		if (start == end)
			return;
		VERIFY3U(start, <, end);

		/* there is no overlap */
		if (end <= rs_get_start(curr, removefrom)) {
			range_tree_add(addto, start, end - start);
			return;
		}

		uint64_t overlap_start = MAX(rs_get_start(curr, removefrom),
		    start);
		uint64_t overlap_end = MIN(rs_get_end(curr, removefrom),
		    end);
		uint64_t overlap_size = overlap_end - overlap_start;
		ASSERT3S(overlap_size, >, 0);
		range_seg_max_t rs;
		rs_copy(curr, &rs, removefrom);

		range_tree_remove(removefrom, overlap_start, overlap_size);

		if (start < overlap_start)
			range_tree_add(addto, start, overlap_start - start);

		start = overlap_end;
		next = zfs_btree_find(&removefrom->rt_root, &rs, &where);
		/*
		 * If we find something here, we only removed part of the
		 * curr segment. Either there's some left at the end
		 * because we've reached the end of the range we're removing,
		 * or there's some left at the start because we started
		 * partway through the range.  Either way, we continue with
		 * the loop. If it's the former, we'll return at the start of
		 * the loop, and if it's the latter we'll see if there is more
		 * area to process.
		 */
		if (next != NULL) {
			ASSERT(start == end || start == rs_get_end(&rs,
			    removefrom));
		}

		next = zfs_btree_next(&removefrom->rt_root, &where, &where);
	}
	VERIFY3P(curr, ==, NULL);

	if (start != end) {
		VERIFY3U(start, <, end);
		range_tree_add(addto, start, end - start);
	} else {
		VERIFY3U(start, ==, end);
	}
}

/*
 * For each entry in rt, if it exists in removefrom, remove it
 * from removefrom. Otherwise, add it to addto.
 */
void
range_tree_remove_xor_add(range_tree_t *rt, range_tree_t *removefrom,
    range_tree_t *addto)
{
	zfs_btree_index_t where;
	for (range_seg_t *rs = zfs_btree_first(&rt->rt_root, &where); rs;
	    rs = zfs_btree_next(&rt->rt_root, &where, &where)) {
		range_tree_remove_xor_add_segment(rs_get_start(rs, rt),
		    rs_get_end(rs, rt), removefrom, addto);
	}
}

uint64_t
range_tree_min(range_tree_t *rt)
{
	range_seg_t *rs = zfs_btree_first(&rt->rt_root, NULL);
	return (rs != NULL ? rs_get_start(rs, rt) : 0);
}

uint64_t
range_tree_max(range_tree_t *rt)
{
	range_seg_t *rs = zfs_btree_last(&rt->rt_root, NULL);
	return (rs != NULL ? rs_get_end(rs, rt) : 0);
}

uint64_t
range_tree_span(range_tree_t *rt)
{
	return (range_tree_max(rt) - range_tree_min(rt));
}
