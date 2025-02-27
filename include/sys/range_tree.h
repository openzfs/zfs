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
 */

#ifndef _SYS_RANGE_TREE_H
#define	_SYS_RANGE_TREE_H

#include <sys/btree.h>
#include <sys/dmu.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	ZFS_RANGE_TREE_HISTOGRAM_SIZE	64

typedef struct zfs_range_tree_ops zfs_range_tree_ops_t;

typedef enum zfs_range_seg_type {
	ZFS_RANGE_SEG32,
	ZFS_RANGE_SEG64,
	ZFS_RANGE_SEG_GAP,
	ZFS_RANGE_SEG_NUM_TYPES,
} zfs_range_seg_type_t;

/*
 * Note: the range_tree may not be accessed concurrently; consumers
 * must provide external locking if required.
 */
typedef struct zfs_range_tree {
	zfs_btree_t	rt_root;	/* offset-ordered segment b-tree */
	uint64_t	rt_space;	/* sum of all segments in the map */
	zfs_range_seg_type_t rt_type;	/* type of zfs_range_seg_t in use */
	/*
	 * All data that is stored in the range tree must have a start higher
	 * than or equal to rt_start, and all sizes and offsets must be
	 * multiples of 1 << rt_shift.
	 */
	uint8_t		rt_shift;
	uint64_t	rt_start;
	const zfs_range_tree_ops_t *rt_ops;
	void		*rt_arg;
	uint64_t	rt_gap;		/* allowable inter-segment gap */

	/*
	 * The rt_histogram maintains a histogram of ranges. Each bucket,
	 * rt_histogram[i], contains the number of ranges whose size is:
	 * 2^i <= size of range in bytes < 2^(i+1)
	 */
	uint64_t	rt_histogram[ZFS_RANGE_TREE_HISTOGRAM_SIZE];
} zfs_range_tree_t;

typedef struct zfs_range_seg32 {
	uint32_t	rs_start;	/* starting offset of this segment */
	uint32_t	rs_end;		/* ending offset (non-inclusive) */
} zfs_range_seg32_t;

/*
 * Extremely large metaslabs, vdev-wide trees, and dnode-wide trees may
 * require 64-bit integers for ranges.
 */
typedef struct zfs_range_seg64 {
	uint64_t	rs_start;	/* starting offset of this segment */
	uint64_t	rs_end;		/* ending offset (non-inclusive) */
} zfs_range_seg64_t;

typedef struct zfs_range_seg_gap {
	uint64_t	rs_start;	/* starting offset of this segment */
	uint64_t	rs_end;		/* ending offset (non-inclusive) */
	uint64_t	rs_fill;	/* actual fill if gap mode is on */
} zfs_range_seg_gap_t;

/*
 * This type needs to be the largest of the range segs, since it will be stack
 * allocated and then cast the actual type to do tree operations.
 */
typedef zfs_range_seg_gap_t zfs_range_seg_max_t;

/*
 * This is just for clarity of code purposes, so we can make it clear that a
 * pointer is to a range seg of some type; when we need to do the actual math,
 * we'll figure out the real type.
 */
typedef void zfs_range_seg_t;

struct zfs_range_tree_ops {
	void    (*rtop_create)(zfs_range_tree_t *rt, void *arg);
	void    (*rtop_destroy)(zfs_range_tree_t *rt, void *arg);
	void	(*rtop_add)(zfs_range_tree_t *rt, void *rs, void *arg);
	void    (*rtop_remove)(zfs_range_tree_t *rt, void *rs, void *arg);
	void	(*rtop_vacate)(zfs_range_tree_t *rt, void *arg);
};

static inline uint64_t
zfs_rs_get_start_raw(const zfs_range_seg_t *rs, const zfs_range_tree_t *rt)
{
	ASSERT3U(rt->rt_type, <=, ZFS_RANGE_SEG_NUM_TYPES);
	switch (rt->rt_type) {
	case ZFS_RANGE_SEG32:
		return (((const zfs_range_seg32_t *)rs)->rs_start);
	case ZFS_RANGE_SEG64:
		return (((const zfs_range_seg64_t *)rs)->rs_start);
	case ZFS_RANGE_SEG_GAP:
		return (((const zfs_range_seg_gap_t *)rs)->rs_start);
	default:
		VERIFY(0);
		return (0);
	}
}

static inline uint64_t
zfs_rs_get_end_raw(const zfs_range_seg_t *rs, const zfs_range_tree_t *rt)
{
	ASSERT3U(rt->rt_type, <=, ZFS_RANGE_SEG_NUM_TYPES);
	switch (rt->rt_type) {
	case ZFS_RANGE_SEG32:
		return (((const zfs_range_seg32_t *)rs)->rs_end);
	case ZFS_RANGE_SEG64:
		return (((const zfs_range_seg64_t *)rs)->rs_end);
	case ZFS_RANGE_SEG_GAP:
		return (((const zfs_range_seg_gap_t *)rs)->rs_end);
	default:
		VERIFY(0);
		return (0);
	}
}

static inline uint64_t
zfs_rs_get_fill_raw(const zfs_range_seg_t *rs, const zfs_range_tree_t *rt)
{
	ASSERT3U(rt->rt_type, <=, ZFS_RANGE_SEG_NUM_TYPES);
	switch (rt->rt_type) {
	case ZFS_RANGE_SEG32: {
		const zfs_range_seg32_t *r32 = (const zfs_range_seg32_t *)rs;
		return (r32->rs_end - r32->rs_start);
	}
	case ZFS_RANGE_SEG64: {
		const zfs_range_seg64_t *r64 = (const zfs_range_seg64_t *)rs;
		return (r64->rs_end - r64->rs_start);
	}
	case ZFS_RANGE_SEG_GAP:
		return (((const zfs_range_seg_gap_t *)rs)->rs_fill);
	default:
		VERIFY(0);
		return (0);
	}

}

static inline uint64_t
zfs_rs_get_start(const zfs_range_seg_t *rs, const zfs_range_tree_t *rt)
{
	return ((zfs_rs_get_start_raw(rs, rt) << rt->rt_shift) + rt->rt_start);
}

static inline uint64_t
zfs_rs_get_end(const zfs_range_seg_t *rs, const zfs_range_tree_t *rt)
{
	return ((zfs_rs_get_end_raw(rs, rt) << rt->rt_shift) + rt->rt_start);
}

static inline uint64_t
zfs_rs_get_fill(const zfs_range_seg_t *rs, const zfs_range_tree_t *rt)
{
	return (zfs_rs_get_fill_raw(rs, rt) << rt->rt_shift);
}

static inline void
zfs_rs_set_start_raw(zfs_range_seg_t *rs, zfs_range_tree_t *rt, uint64_t start)
{
	ASSERT3U(rt->rt_type, <=, ZFS_RANGE_SEG_NUM_TYPES);
	switch (rt->rt_type) {
	case ZFS_RANGE_SEG32:
		ASSERT3U(start, <=, UINT32_MAX);
		((zfs_range_seg32_t *)rs)->rs_start = (uint32_t)start;
		break;
	case ZFS_RANGE_SEG64:
		((zfs_range_seg64_t *)rs)->rs_start = start;
		break;
	case ZFS_RANGE_SEG_GAP:
		((zfs_range_seg_gap_t *)rs)->rs_start = start;
		break;
	default:
		VERIFY(0);
	}
}

static inline void
zfs_rs_set_end_raw(zfs_range_seg_t *rs, zfs_range_tree_t *rt, uint64_t end)
{
	ASSERT3U(rt->rt_type, <=, ZFS_RANGE_SEG_NUM_TYPES);
	switch (rt->rt_type) {
	case ZFS_RANGE_SEG32:
		ASSERT3U(end, <=, UINT32_MAX);
		((zfs_range_seg32_t *)rs)->rs_end = (uint32_t)end;
		break;
	case ZFS_RANGE_SEG64:
		((zfs_range_seg64_t *)rs)->rs_end = end;
		break;
	case ZFS_RANGE_SEG_GAP:
		((zfs_range_seg_gap_t *)rs)->rs_end = end;
		break;
	default:
		VERIFY(0);
	}
}

static inline void
zfs_zfs_rs_set_fill_raw(zfs_range_seg_t *rs, zfs_range_tree_t *rt,
    uint64_t fill)
{
	ASSERT3U(rt->rt_type, <=, ZFS_RANGE_SEG_NUM_TYPES);
	switch (rt->rt_type) {
	case ZFS_RANGE_SEG32:
		/* fall through */
	case ZFS_RANGE_SEG64:
		ASSERT3U(fill, ==, zfs_rs_get_end_raw(rs, rt) -
		    zfs_rs_get_start_raw(rs, rt));
		break;
	case ZFS_RANGE_SEG_GAP:
		((zfs_range_seg_gap_t *)rs)->rs_fill = fill;
		break;
	default:
		VERIFY(0);
	}
}

static inline void
zfs_rs_set_start(zfs_range_seg_t *rs, zfs_range_tree_t *rt, uint64_t start)
{
	ASSERT3U(start, >=, rt->rt_start);
	ASSERT(IS_P2ALIGNED(start, 1ULL << rt->rt_shift));
	zfs_rs_set_start_raw(rs, rt, (start - rt->rt_start) >> rt->rt_shift);
}

static inline void
zfs_rs_set_end(zfs_range_seg_t *rs, zfs_range_tree_t *rt, uint64_t end)
{
	ASSERT3U(end, >=, rt->rt_start);
	ASSERT(IS_P2ALIGNED(end, 1ULL << rt->rt_shift));
	zfs_rs_set_end_raw(rs, rt, (end - rt->rt_start) >> rt->rt_shift);
}

static inline void
zfs_rs_set_fill(zfs_range_seg_t *rs, zfs_range_tree_t *rt, uint64_t fill)
{
	ASSERT(IS_P2ALIGNED(fill, 1ULL << rt->rt_shift));
	zfs_zfs_rs_set_fill_raw(rs, rt, fill >> rt->rt_shift);
}

typedef void zfs_range_tree_func_t(void *arg, uint64_t start, uint64_t size);

zfs_range_tree_t *zfs_range_tree_create_gap(const zfs_range_tree_ops_t *ops,
    zfs_range_seg_type_t type, void *arg, uint64_t start, uint64_t shift,
    uint64_t gap);
zfs_range_tree_t *zfs_range_tree_create(const zfs_range_tree_ops_t *ops,
    zfs_range_seg_type_t type, void *arg, uint64_t start, uint64_t shift);
void zfs_range_tree_destroy(zfs_range_tree_t *rt);
boolean_t zfs_range_tree_contains(zfs_range_tree_t *rt, uint64_t start,
    uint64_t size);
zfs_range_seg_t *zfs_range_tree_find(zfs_range_tree_t *rt, uint64_t start,
    uint64_t size);
boolean_t zfs_range_tree_find_in(zfs_range_tree_t *rt, uint64_t start,
    uint64_t size, uint64_t *ostart, uint64_t *osize);
void zfs_range_tree_verify_not_present(zfs_range_tree_t *rt,
    uint64_t start, uint64_t size);
void zfs_range_tree_resize_segment(zfs_range_tree_t *rt, zfs_range_seg_t *rs,
    uint64_t newstart, uint64_t newsize);
uint64_t zfs_range_tree_space(zfs_range_tree_t *rt);
uint64_t zfs_range_tree_numsegs(zfs_range_tree_t *rt);
boolean_t zfs_range_tree_is_empty(zfs_range_tree_t *rt);
void zfs_range_tree_swap(zfs_range_tree_t **rtsrc, zfs_range_tree_t **rtdst);
void zfs_range_tree_stat_verify(zfs_range_tree_t *rt);
uint64_t zfs_range_tree_min(zfs_range_tree_t *rt);
uint64_t zfs_range_tree_max(zfs_range_tree_t *rt);
uint64_t zfs_range_tree_span(zfs_range_tree_t *rt);

void zfs_range_tree_add(void *arg, uint64_t start, uint64_t size);
void zfs_range_tree_remove(void *arg, uint64_t start, uint64_t size);
void zfs_range_tree_remove_fill(zfs_range_tree_t *rt, uint64_t start,
    uint64_t size);
void zfs_range_tree_adjust_fill(zfs_range_tree_t *rt, zfs_range_seg_t *rs,
    int64_t delta);
void zfs_range_tree_clear(zfs_range_tree_t *rt, uint64_t start, uint64_t size);

void zfs_range_tree_vacate(zfs_range_tree_t *rt, zfs_range_tree_func_t *func,
    void *arg);
void zfs_range_tree_walk(zfs_range_tree_t *rt, zfs_range_tree_func_t *func,
    void *arg);
zfs_range_seg_t *zfs_range_tree_first(zfs_range_tree_t *rt);

void zfs_range_tree_remove_xor_add_segment(uint64_t start, uint64_t end,
    zfs_range_tree_t *removefrom, zfs_range_tree_t *addto);
void zfs_range_tree_remove_xor_add(zfs_range_tree_t *rt,
    zfs_range_tree_t *removefrom, zfs_range_tree_t *addto);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_RANGE_TREE_H */
