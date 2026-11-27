// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */

/*
 * Copyright (c) 2026 by Garth Snyder. All rights reserved.
 */

#ifndef _ZSTREAM_HEAP_H
#define	_ZSTREAM_HEAP_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <sys/stdtypes.h>

#define	heap_count(heap)	((heap)->count)

/*
 * This API implements a simple binary heap of uint64_t's, ordered either
 * smallest first (a min-heap) or largest first (a max-heap). Any value can
 * be stored (including 0), and duplicates are allowed.
 *
 * The heap's array starts with room for initial_size entries and doubles as
 * needed, but never past max_size entries (0 = no limit). A push onto a
 * heap that is full at its limit is fatal. The array never shrinks.
 *
 * linear_hash_t uses a min-heap to track overflow buckets for reuse,
 * because lower-numbered buckets are more likely to be stored in memory
 * rather than on disk. When recycling, it always wants the lowest available
 * bucket.
 *
 * allocator_t uses a max-heap to record the page clusters stored in memory.
 * When yielding memory, the highest-numbered cluster is sacrificed.
 */

typedef enum {
	HEAP_MIN,	/* Pops return the smallest value */
	HEAP_MAX,	/* Pops return the largest value */
} heap_order_t;

typedef struct {
	heap_order_t	order;
	uint64_t	size;		/* Entries allocated */
	uint64_t	max_size;	/* Limit on size; 0 = none */
	uint64_t	count;		/* Entries in use */
	uint64_t	*values;
} heap_t;

/*
 * Create and return a heap_t. max_size, if nonzero, must be at least
 * initial_size.
 */
heap_t
heap_init(heap_order_t order, uint64_t initial_size, uint64_t max_size);

/*
 * Insert a value.
 */
void
heap_push(heap_t *heap, uint64_t value);

/*
 * Remove the root value (the smallest in a min-heap, the largest in a
 * max-heap) and store it in *value. Returns B_FALSE, leaving *value
 * untouched, if the heap is empty.
 */
boolean_t
heap_pop(heap_t *heap, uint64_t *value);

/*
 * Peek at the root node and return:
 *
 *   -1 if the supplied value is greater
 *    0 if the root is equal or the heap is empty
 *    1 if the supplied value is smaller
 */
int
heap_compare_root(heap_t *heap, uint64_t value);

void
heap_fini(heap_t *heap);

#ifdef	__cplusplus
}
#endif

#endif /* _ZSTREAM_HEAP_H */
