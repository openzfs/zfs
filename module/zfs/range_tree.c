/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Adding an allocation via zfs_range_tree_add to the range tree can either:
 * 1) create a new extent
 * 2) extend an adjacent extent
 * 3) merge two adjacent extents
 * Conversely, removing an allocation via zfs_range_tree_remove can:
 * 1) completely remove an extent
 * 2) shorten an extent (if the allocation was near one of its ends)
 * 3) split an extent into two extents, in effect punching a hole
 *
 * A range tree is also capable of 'bridging' gaps when adding
 * allocations. This is useful for cases when close proximity of
 * allocations is an important detail that needs to be represented
 * in the range tree. See zfs_range_tree_set_gap(). The default behavior
 * is not to bridge gaps (i.e. the maximum allowed gap size is 0).
 *
 * In order to traverse a range tree, use either the zfs_range_tree_walk()
 * or zfs_range_tree_vacate() functions.
 *
 * To obtain more accurate information on individual segment
 * operations that the range tree performs "under the hood", you can
 * specify a set of callbacks by passing a zfs_range_tree_ops_t structure
 * to the zfs_range_tree_create function. Any callbacks that are non-NULL
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
zfs_rs_copy(zfs_range_seg_t *src, zfs_range_seg_t *dest, zfs_range_tree_t *rt)
{
	ASSERT3U(rt->rt_type, <, ZFS_RANGE_SEG_NUM_TYPES);
	size_t size = 0;
	switch (rt->rt_type) {
	case ZFS_RANGE_SEG32:
		size = sizeof (zfs_range_seg32_t);
		break;
	case ZFS_RANGE_SEG64:
		size = sizeof (zfs_range_seg64_t);
		break;
	case ZFS_RANGE_SEG_GAP:
		size = sizeof (zfs_range_seg_gap_t);
		break;
	default:
		__builtin_unreachable();
	}
	memcpy(dest, src, size);
}

void
zfs_range_tree_stat_verify(zfs_range_tree_t *rt)
{
	zfs_range_seg_t *rs;
	zfs_btree_index_t where;
	uint64_t hist[ZFS_RANGE_TREE_HISTOGRAM_SIZE] = { 0 };
	int i;

	for (rs = zfs_btree_first(&rt->rt_root, &where); rs != NULL;
	    rs = zfs_btree_next(&rt->rt_root, &where, &where)) {
		uint64_t size = zfs_rs_get_end(rs, rt) -
		    zfs_rs_get_start(rs, rt);
		int idx	= highbit64(size) - 1;

		hist[idx]++;
		ASSERT3U(hist[idx], !=, 0);
	}

	for (i = 0; i < ZFS_RANGE_TREE_HISTOGRAM_SIZE; i++) {
		if (hist[i] != rt->rt_histogram[i]) {
			zfs_dbgmsg("i=%d, hist=%px, hist=%llu, rt_hist=%llu",
			    i, hist, (u_longlong_t)hist[i],
			    (u_longlong_t)rt->rt_histogram[i]);
		}
		VERIFY3U(hist[i], ==, rt->rt_histogram[i]);
	}
}

static void
zfs_range_tree_stat_incr(zfs_range_tree_t *rt, zfs_range_seg_t *rs)
{
	uint64_t size = zfs_rs_get_end(rs, rt) - zfs_rs_get_start(rs, rt);
	int idx = highbit64(size) - 1;

	ASSERT(size != 0);
	ASSERT3U(idx, <,
	    sizeof (rt->rt_histogram) / sizeof (*rt->rt_histogram));

	rt->rt_histogram[idx]++;
	ASSERT3U(rt->rt_histogram[idx], !=, 0);
}

static void
zfs_range_tree_stat_decr(zfs_range_tree_t *rt, zfs_range_seg_t *rs)
{
	uint64_t size = zfs_rs_get_end(rs, rt) - zfs_rs_get_start(rs, rt);
	int idx = highbit64(size) - 1;

	ASSERT(size != 0);
	ASSERT3U(idx, <,
	    sizeof (rt->rt_histogram) / sizeof (*rt->rt_histogram));

	ASSERT3U(rt->rt_histogram[idx], !=, 0);
	rt->rt_histogram[idx]--;
}

__attribute__((always_inline)) inline
static int
zfs_range_tree_seg32_compare(const void *x1, const void *x2)
{
	const zfs_range_seg32_t *r1 = x1;
	const zfs_range_seg32_t *r2 = x2;

	ASSERT3U(r1->rs_start, <=, r1->rs_end);
	ASSERT3U(r2->rs_start, <=, r2->rs_end);

	return ((r1->rs_start >= r2->rs_end) - (r1->rs_end <= r2->rs_start));
}

__attribute__((always_inline)) inline
static int
zfs_range_tree_seg64_compare(const void *x1, const void *x2)
{
	const zfs_range_seg64_t *r1 = x1;
	const zfs_range_seg64_t *r2 = x2;

	ASSERT3U(r1->rs_start, <=, r1->rs_end);
	ASSERT3U(r2->rs_start, <=, r2->rs_end);

	return ((r1->rs_start >= r2->rs_end) - (r1->rs_end <= r2->rs_start));
}

__attribute__((always_inline)) inline
static int
zfs_range_tree_seg_gap_compare(const void *x1, const void *x2)
{
	const zfs_range_seg_gap_t *r1 = x1;
	const zfs_range_seg_gap_t *r2 = x2;

	ASSERT3U(r1->rs_start, <=, r1->rs_end);
	ASSERT3U(r2->rs_start, <=, r2->rs_end);

	return ((r1->rs_start >= r2->rs_end) - (r1->rs_end <= r2->rs_start));
}

ZFS_BTREE_FIND_IN_BUF_FUNC(zfs_range_tree_seg32_find_in_buf, zfs_range_seg32_t,
    zfs_range_tree_seg32_compare)

ZFS_BTREE_FIND_IN_BUF_FUNC(zfs_range_tree_seg64_find_in_buf, zfs_range_seg64_t,
    zfs_range_tree_seg64_compare)

ZFS_BTREE_FIND_IN_BUF_FUNC(zfs_range_tree_seg_gap_find_in_buf,
    zfs_range_seg_gap_t, zfs_range_tree_seg_gap_compare)

zfs_range_tree_t *
zfs_range_tree_create_gap(const zfs_range_tree_ops_t *ops,
    zfs_range_seg_type_t type, void *arg, uint64_t start, uint64_t shift,
    uint64_t gap)
{
	zfs_range_tree_t *rt = kmem_zalloc(sizeof (zfs_range_tree_t), KM_SLEEP);

	ASSERT3U(shift, <, 64);
	ASSERT3U(type, <=, ZFS_RANGE_SEG_NUM_TYPES);
	size_t size;
	int (*compare) (const void *, const void *);
	bt_find_in_buf_f bt_find;
	switch (type) {
	case ZFS_RANGE_SEG32:
		size = sizeof (zfs_range_seg32_t);
		compare = zfs_range_tree_seg32_compare;
		bt_find = zfs_range_tree_seg32_find_in_buf;
		break;
	case ZFS_RANGE_SEG64:
		size = sizeof (zfs_range_seg64_t);
		compare = zfs_range_tree_seg64_compare;
		bt_find = zfs_range_tree_seg64_find_in_buf;
		break;
	case ZFS_RANGE_SEG_GAP:
		size = sizeof (zfs_range_seg_gap_t);
		compare = zfs_range_tree_seg_gap_compare;
		bt_find = zfs_range_tree_seg_gap_find_in_buf;
		break;
	default:
		panic("Invalid range seg type %d", type);
	}
	zfs_btree_create(&rt->rt_root, compare, bt_find, size);

	rt->rt_ops = ops;
	rt->rt_gap = gap;
	rt->rt_arg = arg;
	rt->rt_type = type;
	rt->rt_start = start;
	rt->rt_shift = shift;

	if (rt->rt_ops != NULL && rt->rt_ops->rtop_create != NULL)
		rt->rt_ops->rtop_create(rt, rt->rt_arg);

	return (rt);
}

zfs_range_tree_t *
zfs_range_tree_create(const zfs_range_tree_ops_t *ops,
    zfs_range_seg_type_t type, void *arg, uint64_t start, uint64_t shift)
{
	return (zfs_range_tree_create_gap(ops, type, arg, start, shift, 0));
}

void
zfs_range_tree_destroy(zfs_range_tree_t *rt)
{
	VERIFY0(rt->rt_space);

	if (rt->rt_ops != NULL && rt->rt_ops->rtop_destroy != NULL)
		rt->rt_ops->rtop_destroy(rt, rt->rt_arg);

	zfs_btree_destroy(&rt->rt_root);
	kmem_free(rt, sizeof (*rt));
}

void
zfs_range_tree_adjust_fill(zfs_range_tree_t *rt, zfs_range_seg_t *rs,
    int64_t delta)
{
	if (delta < 0 && delta * -1 >= zfs_rs_get_fill(rs, rt)) {
		zfs_panic_recover("zfs: attempting to decrease fill to or "
		    "below 0; probable double remove in segment [%llx:%llx]",
		    (longlong_t)zfs_rs_get_start(rs, rt),
		    (longlong_t)zfs_rs_get_end(rs, rt));
	}
	if (zfs_rs_get_fill(rs, rt) + delta > zfs_rs_get_end(rs, rt) -
	    zfs_rs_get_start(rs, rt)) {
		zfs_panic_recover("zfs: attempting to increase fill beyond "
		    "max; probable double add in segment [%llx:%llx]",
		    (longlong_t)zfs_rs_get_start(rs, rt),
		    (longlong_t)zfs_rs_get_end(rs, rt));
	}

	if (rt->rt_ops != NULL && rt->rt_ops->rtop_remove != NULL)
		rt->rt_ops->rtop_remove(rt, rs, rt->rt_arg);
	zfs_rs_set_fill(rs, rt, zfs_rs_get_fill(rs, rt) + delta);
	if (rt->rt_ops != NULL && rt->rt_ops->rtop_add != NULL)
		rt->rt_ops->rtop_add(rt, rs, rt->rt_arg);
}

static void
zfs_range_tree_add_impl(void *arg, uint64_t start, uint64_t size, uint64_t fill)
{
	zfs_range_tree_t *rt = arg;
	zfs_btree_index_t where;
	zfs_range_seg_t *rs_before, *rs_after, *rs;
	zfs_range_seg_max_t tmp, rsearch;
	uint64_t end = start + size, gap = rt->rt_gap;
	uint64_t bridge_size = 0;
	boolean_t merge_before, merge_after;

	ASSERT3U(size, !=, 0);
	ASSERT3U(fill, <=, size);
	ASSERT3U(start + size, >, start);

	zfs_rs_set_start(&rsearch, rt, start);
	zfs_rs_set_end(&rsearch, rt, end);
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
		uint64_t rstart = zfs_rs_get_start(rs, rt);
		uint64_t rend = zfs_rs_get_end(rs, rt);
		if (rstart <= start && rend >= end) {
			zfs_range_tree_adjust_fill(rt, rs, fill);
			return;
		}

		if (rt->rt_ops != NULL && rt->rt_ops->rtop_remove != NULL)
			rt->rt_ops->rtop_remove(rt, rs, rt->rt_arg);

		zfs_range_tree_stat_decr(rt, rs);
		rt->rt_space -= rend - rstart;

		fill += zfs_rs_get_fill(rs, rt);
		start = MIN(start, rstart);
		end = MAX(end, rend);
		size = end - start;

		zfs_btree_remove(&rt->rt_root, rs);
		zfs_range_tree_add_impl(rt, start, size, fill);
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

	merge_before = (rs_before != NULL && zfs_rs_get_end(rs_before, rt) >=
	    start - gap);
	merge_after = (rs_after != NULL && zfs_rs_get_start(rs_after, rt) <=
	    end + gap);

	if (merge_before && gap != 0)
		bridge_size += start - zfs_rs_get_end(rs_before, rt);
	if (merge_after && gap != 0)
		bridge_size += zfs_rs_get_start(rs_after, rt) - end;

	if (merge_before && merge_after) {
		if (rt->rt_ops != NULL && rt->rt_ops->rtop_remove != NULL) {
			rt->rt_ops->rtop_remove(rt, rs_before, rt->rt_arg);
			rt->rt_ops->rtop_remove(rt, rs_after, rt->rt_arg);
		}

		zfs_range_tree_stat_decr(rt, rs_before);
		zfs_range_tree_stat_decr(rt, rs_after);

		zfs_rs_copy(rs_after, &tmp, rt);
		uint64_t before_start = zfs_rs_get_start_raw(rs_before, rt);
		uint64_t before_fill = zfs_rs_get_fill(rs_before, rt);
		uint64_t after_fill = zfs_rs_get_fill(rs_after, rt);
		zfs_btree_remove_idx(&rt->rt_root, &where_before);

		/*
		 * We have to re-find the node because our old reference is
		 * invalid as soon as we do any mutating btree operations.
		 */
		rs_after = zfs_btree_find(&rt->rt_root, &tmp, &where_after);
		ASSERT3P(rs_after, !=, NULL);
		zfs_rs_set_start_raw(rs_after, rt, before_start);
		zfs_rs_set_fill(rs_after, rt, after_fill + before_fill + fill);
		rs = rs_after;
	} else if (merge_before) {
		if (rt->rt_ops != NULL && rt->rt_ops->rtop_remove != NULL)
			rt->rt_ops->rtop_remove(rt, rs_before, rt->rt_arg);

		zfs_range_tree_stat_decr(rt, rs_before);

		uint64_t before_fill = zfs_rs_get_fill(rs_before, rt);
		zfs_rs_set_end(rs_before, rt, end);
		zfs_rs_set_fill(rs_before, rt, before_fill + fill);
		rs = rs_before;
	} else if (merge_after) {
		if (rt->rt_ops != NULL && rt->rt_ops->rtop_remove != NULL)
			rt->rt_ops->rtop_remove(rt, rs_after, rt->rt_arg);

		zfs_range_tree_stat_decr(rt, rs_after);

		uint64_t after_fill = zfs_rs_get_fill(rs_after, rt);
		zfs_rs_set_start(rs_after, rt, start);
		zfs_rs_set_fill(rs_after, rt, after_fill + fill);
		rs = rs_after;
	} else {
		rs = &tmp;

		zfs_rs_set_start(rs, rt, start);
		zfs_rs_set_end(rs, rt, end);
		zfs_rs_set_fill(rs, rt, fill);
		zfs_btree_add_idx(&rt->rt_root, rs, &where);
	}

	if (gap != 0) {
		ASSERT3U(zfs_rs_get_fill(rs, rt), <=, zfs_rs_get_end(rs, rt) -
		    zfs_rs_get_start(rs, rt));
	} else {
		ASSERT3U(zfs_rs_get_fill(rs, rt), ==, zfs_rs_get_end(rs, rt) -
		    zfs_rs_get_start(rs, rt));
	}

	if (rt->rt_ops != NULL && rt->rt_ops->rtop_add != NULL)
		rt->rt_ops->rtop_add(rt, rs, rt->rt_arg);

	zfs_range_tree_stat_incr(rt, rs);
	rt->rt_space += size + bridge_size;
}

void
zfs_range_tree_add(void *arg, uint64_t start, uint64_t size)
{
	zfs_range_tree_add_impl(arg, start, size, size);
}

static void
zfs_range_tree_remove_impl(zfs_range_tree_t *rt, uint64_t start, uint64_t size,
    boolean_t do_fill)
{
	zfs_btree_index_t where;
	zfs_range_seg_t *rs;
	zfs_range_seg_max_t rsearch, rs_tmp;
	uint64_t end = start + size;
	boolean_t left_over, right_over;

	VERIFY3U(size, !=, 0);
	VERIFY3U(size, <=, rt->rt_space);
	if (rt->rt_type == ZFS_RANGE_SEG64)
		ASSERT3U(start + size, >, start);

	zfs_rs_set_start(&rsearch, rt, start);
	zfs_rs_set_end(&rsearch, rt, end);
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
			if (zfs_rs_get_fill(rs, rt) == size) {
				start = zfs_rs_get_start(rs, rt);
				end = zfs_rs_get_end(rs, rt);
				size = end - start;
			} else {
				zfs_range_tree_adjust_fill(rt, rs, -size);
				return;
			}
		} else if (zfs_rs_get_start(rs, rt) != start ||
		    zfs_rs_get_end(rs, rt) != end) {
			zfs_panic_recover("zfs: freeing partial segment of "
			    "gap tree (offset=%llx size=%llx) of "
			    "(offset=%llx size=%llx)",
			    (longlong_t)start, (longlong_t)size,
			    (longlong_t)zfs_rs_get_start(rs, rt),
			    (longlong_t)zfs_rs_get_end(rs, rt) -
			    zfs_rs_get_start(rs, rt));
			return;
		}
	}

	VERIFY3U(zfs_rs_get_start(rs, rt), <=, start);
	VERIFY3U(zfs_rs_get_end(rs, rt), >=, end);

	left_over = (zfs_rs_get_start(rs, rt) != start);
	right_over = (zfs_rs_get_end(rs, rt) != end);

	zfs_range_tree_stat_decr(rt, rs);

	if (rt->rt_ops != NULL && rt->rt_ops->rtop_remove != NULL)
		rt->rt_ops->rtop_remove(rt, rs, rt->rt_arg);

	if (left_over && right_over) {
		zfs_range_seg_max_t newseg;
		zfs_rs_set_start(&newseg, rt, end);
		zfs_rs_set_end_raw(&newseg, rt, zfs_rs_get_end_raw(rs, rt));
		zfs_rs_set_fill(&newseg, rt, zfs_rs_get_end(rs, rt) - end);
		zfs_range_tree_stat_incr(rt, &newseg);

		// This modifies the buffer already inside the range tree
		zfs_rs_set_end(rs, rt, start);

		zfs_rs_copy(rs, &rs_tmp, rt);
		if (zfs_btree_next(&rt->rt_root, &where, &where) != NULL)
			zfs_btree_add_idx(&rt->rt_root, &newseg, &where);
		else
			zfs_btree_add(&rt->rt_root, &newseg);

		if (rt->rt_ops != NULL && rt->rt_ops->rtop_add != NULL)
			rt->rt_ops->rtop_add(rt, &newseg, rt->rt_arg);
	} else if (left_over) {
		// This modifies the buffer already inside the range tree
		zfs_rs_set_end(rs, rt, start);
		zfs_rs_copy(rs, &rs_tmp, rt);
	} else if (right_over) {
		// This modifies the buffer already inside the range tree
		zfs_rs_set_start(rs, rt, end);
		zfs_rs_copy(rs, &rs_tmp, rt);
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
		zfs_zfs_rs_set_fill_raw(rs, rt, zfs_rs_get_end_raw(rs, rt) -
		    zfs_rs_get_start_raw(rs, rt));
		zfs_range_tree_stat_incr(rt, &rs_tmp);

		if (rt->rt_ops != NULL && rt->rt_ops->rtop_add != NULL)
			rt->rt_ops->rtop_add(rt, &rs_tmp, rt->rt_arg);
	}

	rt->rt_space -= size;
}

void
zfs_range_tree_remove(void *arg, uint64_t start, uint64_t size)
{
	zfs_range_tree_remove_impl(arg, start, size, B_FALSE);
}

void
zfs_range_tree_remove_fill(zfs_range_tree_t *rt, uint64_t start, uint64_t size)
{
	zfs_range_tree_remove_impl(rt, start, size, B_TRUE);
}

void
zfs_range_tree_resize_segment(zfs_range_tree_t *rt, zfs_range_seg_t *rs,
    uint64_t newstart, uint64_t newsize)
{
	int64_t delta = newsize - (zfs_rs_get_end(rs, rt) -
	    zfs_rs_get_start(rs, rt));

	zfs_range_tree_stat_decr(rt, rs);
	if (rt->rt_ops != NULL && rt->rt_ops->rtop_remove != NULL)
		rt->rt_ops->rtop_remove(rt, rs, rt->rt_arg);

	zfs_rs_set_start(rs, rt, newstart);
	zfs_rs_set_end(rs, rt, newstart + newsize);

	zfs_range_tree_stat_incr(rt, rs);
	if (rt->rt_ops != NULL && rt->rt_ops->rtop_add != NULL)
		rt->rt_ops->rtop_add(rt, rs, rt->rt_arg);

	rt->rt_space += delta;
}

static zfs_range_seg_t *
zfs_range_tree_find_impl(zfs_range_tree_t *rt, uint64_t start, uint64_t size)
{
	zfs_range_seg_max_t rsearch;
	uint64_t end = start + size;

	VERIFY(size != 0);

	zfs_rs_set_start(&rsearch, rt, start);
	zfs_rs_set_end(&rsearch, rt, end);
	return (zfs_btree_find(&rt->rt_root, &rsearch, NULL));
}

zfs_range_seg_t *
zfs_range_tree_find(zfs_range_tree_t *rt, uint64_t start, uint64_t size)
{
	if (rt->rt_type == ZFS_RANGE_SEG64)
		ASSERT3U(start + size, >, start);

	zfs_range_seg_t *rs = zfs_range_tree_find_impl(rt, start, size);
	if (rs != NULL && zfs_rs_get_start(rs, rt) <= start &&
	    zfs_rs_get_end(rs, rt) >= start + size) {
		return (rs);
	}
	return (NULL);
}

void
zfs_range_tree_verify_not_present(zfs_range_tree_t *rt, uint64_t off,
    uint64_t size)
{
	zfs_range_seg_t *rs = zfs_range_tree_find(rt, off, size);
	if (rs != NULL)
		panic("segment already in tree; rs=%p", (void *)rs);
}

boolean_t
zfs_range_tree_contains(zfs_range_tree_t *rt, uint64_t start, uint64_t size)
{
	return (zfs_range_tree_find(rt, start, size) != NULL);
}

/*
 * Returns the first subset of the given range which overlaps with the range
 * tree. Returns true if there is a segment in the range, and false if there
 * isn't.
 */
boolean_t
zfs_range_tree_find_in(zfs_range_tree_t *rt, uint64_t start, uint64_t size,
    uint64_t *ostart, uint64_t *osize)
{
	if (rt->rt_type == ZFS_RANGE_SEG64)
		ASSERT3U(start + size, >, start);

	zfs_range_seg_max_t rsearch;
	zfs_rs_set_start(&rsearch, rt, start);
	zfs_rs_set_end_raw(&rsearch, rt, zfs_rs_get_start_raw(&rsearch, rt) +
	    1);

	zfs_btree_index_t where;
	zfs_range_seg_t *rs = zfs_btree_find(&rt->rt_root, &rsearch, &where);
	if (rs != NULL) {
		*ostart = start;
		*osize = MIN(size, zfs_rs_get_end(rs, rt) - start);
		return (B_TRUE);
	}

	rs = zfs_btree_next(&rt->rt_root, &where, &where);
	if (rs == NULL || zfs_rs_get_start(rs, rt) > start + size)
		return (B_FALSE);

	*ostart = zfs_rs_get_start(rs, rt);
	*osize = MIN(start + size, zfs_rs_get_end(rs, rt)) -
	    zfs_rs_get_start(rs, rt);
	return (B_TRUE);
}

/*
 * Ensure that this range is not in the tree, regardless of whether
 * it is currently in the tree.
 */
void
zfs_range_tree_clear(zfs_range_tree_t *rt, uint64_t start, uint64_t size)
{
	zfs_range_seg_t *rs;

	if (size == 0)
		return;

	if (rt->rt_type == ZFS_RANGE_SEG64)
		ASSERT3U(start + size, >, start);

	while ((rs = zfs_range_tree_find_impl(rt, start, size)) != NULL) {
		uint64_t free_start = MAX(zfs_rs_get_start(rs, rt), start);
		uint64_t free_end = MIN(zfs_rs_get_end(rs, rt), start + size);
		zfs_range_tree_remove(rt, free_start, free_end - free_start);
	}
}

void
zfs_range_tree_swap(zfs_range_tree_t **rtsrc, zfs_range_tree_t **rtdst)
{
	zfs_range_tree_t *rt;

	ASSERT0(zfs_range_tree_space(*rtdst));
	ASSERT0(zfs_btree_numnodes(&(*rtdst)->rt_root));

	rt = *rtsrc;
	*rtsrc = *rtdst;
	*rtdst = rt;
}

void
zfs_range_tree_vacate(zfs_range_tree_t *rt, zfs_range_tree_func_t *func,
    void *arg)
{
	if (rt->rt_ops != NULL && rt->rt_ops->rtop_vacate != NULL)
		rt->rt_ops->rtop_vacate(rt, rt->rt_arg);

	if (func != NULL) {
		zfs_range_seg_t *rs;
		zfs_btree_index_t *cookie = NULL;

		while ((rs = zfs_btree_destroy_nodes(&rt->rt_root, &cookie)) !=
		    NULL) {
			func(arg, zfs_rs_get_start(rs, rt),
			    zfs_rs_get_end(rs, rt) - zfs_rs_get_start(rs, rt));
		}
	} else {
		zfs_btree_clear(&rt->rt_root);
	}

	memset(rt->rt_histogram, 0, sizeof (rt->rt_histogram));
	rt->rt_space = 0;
}

void
zfs_range_tree_walk(zfs_range_tree_t *rt, zfs_range_tree_func_t *func,
    void *arg)
{
	zfs_btree_index_t where;
	for (zfs_range_seg_t *rs = zfs_btree_first(&rt->rt_root, &where);
	    rs != NULL; rs = zfs_btree_next(&rt->rt_root, &where, &where)) {
		func(arg, zfs_rs_get_start(rs, rt), zfs_rs_get_end(rs, rt) -
		    zfs_rs_get_start(rs, rt));
	}
}

zfs_range_seg_t *
zfs_range_tree_first(zfs_range_tree_t *rt)
{
	return (zfs_btree_first(&rt->rt_root, NULL));
}

uint64_t
zfs_range_tree_space(zfs_range_tree_t *rt)
{
	return (rt->rt_space);
}

uint64_t
zfs_range_tree_numsegs(zfs_range_tree_t *rt)
{
	return ((rt == NULL) ? 0 : zfs_btree_numnodes(&rt->rt_root));
}

boolean_t
zfs_range_tree_is_empty(zfs_range_tree_t *rt)
{
	ASSERT(rt != NULL);
	return (zfs_range_tree_space(rt) == 0);
}

/*
 * Remove any overlapping ranges between the given segment [start, end)
 * from removefrom. Add non-overlapping leftovers to addto.
 */
void
zfs_range_tree_remove_xor_add_segment(uint64_t start, uint64_t end,
    zfs_range_tree_t *removefrom, zfs_range_tree_t *addto)
{
	zfs_btree_index_t where;
	zfs_range_seg_max_t starting_rs;
	zfs_rs_set_start(&starting_rs, removefrom, start);
	zfs_rs_set_end_raw(&starting_rs, removefrom,
	    zfs_rs_get_start_raw(&starting_rs, removefrom) + 1);

	zfs_range_seg_t *curr = zfs_btree_find(&removefrom->rt_root,
	    &starting_rs, &where);

	if (curr == NULL)
		curr = zfs_btree_next(&removefrom->rt_root, &where, &where);

	zfs_range_seg_t *next;
	for (; curr != NULL; curr = next) {
		if (start == end)
			return;
		VERIFY3U(start, <, end);

		/* there is no overlap */
		if (end <= zfs_rs_get_start(curr, removefrom)) {
			zfs_range_tree_add(addto, start, end - start);
			return;
		}

		uint64_t overlap_start = MAX(zfs_rs_get_start(curr, removefrom),
		    start);
		uint64_t overlap_end = MIN(zfs_rs_get_end(curr, removefrom),
		    end);
		uint64_t overlap_size = overlap_end - overlap_start;
		ASSERT3S(overlap_size, >, 0);
		zfs_range_seg_max_t rs;
		zfs_rs_copy(curr, &rs, removefrom);

		zfs_range_tree_remove(removefrom, overlap_start, overlap_size);

		if (start < overlap_start)
			zfs_range_tree_add(addto, start, overlap_start - start);

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
			ASSERT(start == end || start == zfs_rs_get_end(&rs,
			    removefrom));
		}

		next = zfs_btree_next(&removefrom->rt_root, &where, &where);
	}
	VERIFY3P(curr, ==, NULL);

	if (start != end) {
		VERIFY3U(start, <, end);
		zfs_range_tree_add(addto, start, end - start);
	} else {
		VERIFY3U(start, ==, end);
	}
}

/*
 * For each entry in rt, if it exists in removefrom, remove it
 * from removefrom. Otherwise, add it to addto.
 */
void
zfs_range_tree_remove_xor_add(zfs_range_tree_t *rt,
    zfs_range_tree_t *removefrom, zfs_range_tree_t *addto)
{
	zfs_btree_index_t where;
	for (zfs_range_seg_t *rs = zfs_btree_first(&rt->rt_root, &where); rs;
	    rs = zfs_btree_next(&rt->rt_root, &where, &where)) {
		zfs_range_tree_remove_xor_add_segment(zfs_rs_get_start(rs, rt),
		    zfs_rs_get_end(rs, rt), removefrom, addto);
	}
}

uint64_t
zfs_range_tree_min(zfs_range_tree_t *rt)
{
	zfs_range_seg_t *rs = zfs_btree_first(&rt->rt_root, NULL);
	return (rs != NULL ? zfs_rs_get_start(rs, rt) : 0);
}

uint64_t
zfs_range_tree_max(zfs_range_tree_t *rt)
{
	zfs_range_seg_t *rs = zfs_btree_last(&rt->rt_root, NULL);
	return (rs != NULL ? zfs_rs_get_end(rs, rt) : 0);
}

uint64_t
zfs_range_tree_span(zfs_range_tree_t *rt)
{
	return (zfs_range_tree_max(rt) - zfs_range_tree_min(rt));
}
