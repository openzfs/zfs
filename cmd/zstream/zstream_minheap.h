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

#ifndef _ZSTREAM_MINHEAP_H
#define	_ZSTREAM_MINHEAP_H

#ifdef	__cplusplus
extern "C" {
#endif

#include "zstream_alloc.h"

/*
 * This API implements a simple binary min-heap for record_ix_t's. It's used
 * to track linear_hash_t overflow buckets for reuse. (These are the only
 * records that are ever recycled.)
 *
 * On average, bucket splits relocate half the chain's entries, potentially
 * moving one or more overflow buckets to the free list.
 *
 * It's a heap rather than some other structure because lower-numbered
 * buckets are more likely to be stored in memory rather than on disk. When
 * recycling, we always want to use the lowest available bucket.
 */

typedef struct {
	uint64_t	size;
	uint64_t	count;
	record_ix_t	*buckets;
} minheap_t;

/*
 * Create and return a minheap_t.
 */
minheap_t
minheap_init(uint64_t initial_size);

/*
 * Insert a record index.
 */
void
minheap_push(minheap_t *heap, record_ix_t bucket);

/*
 * Remove and return the lowest value from the heap, or 0 if the heap is
 * empty.
 */
record_ix_t
minheap_pop(minheap_t *heap);

void
minheap_fini(minheap_t *heap);

#ifdef	__cplusplus
}
#endif

#endif /* _ZSTREAM_MINHEAP_H */
