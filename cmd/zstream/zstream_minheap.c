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

#include "zstream_minheap.h"
#include "zstream_util.h"

/*
 * Create a min-heap.
 */
minheap_t
minheap_init(uint64_t initial_size)
{
	VERIFY3U(initial_size, >, 0);
	minheap_t heap = {
		.size = initial_size,
		.buckets = safe_malloc(initial_size * sizeof (record_ix_t))
	};
	return (heap);
}

/*
 * Add an overflow bucket to the heap.
 *
 * The new bucket goes in the first unused slot and then sifts up, trading
 * places with its parent until the parent is smaller.
 */
void
minheap_push(minheap_t *heap, record_ix_t bucket)
{
	VERIFY3U(bucket, !=, 0);
	if (heap->count >= heap->size) {
		heap->size *= 2;
		heap->buckets = safe_realloc(heap->buckets,
		    heap->size * sizeof (record_ix_t));
	}
	uint64_t i = heap->count++;
	while (i > 0) {
		uint64_t parent = (i - 1) / 2;
		if (heap->buckets[parent] <= bucket)
			break;
		heap->buckets[i] = heap->buckets[parent];
		i = parent;
	}
	heap->buckets[i] = bucket;
}

/*
 * Remove and return the root value from the free heap, or 0 if the heap is
 * empty. The last element moves into the root's slot and then sifts down,
 * trading places with its smaller child until neither child is smaller.
 */
record_ix_t
minheap_pop(minheap_t *heap)
{
	switch (heap->count) {
		case 0:	return (0);
		case 1:	return (heap->buckets[--heap->count]);
	}
	record_ix_t min = heap->buckets[0];
	record_ix_t last = heap->buckets[--heap->count];
	uint64_t child, dest = 0;
	for (child = 2 * dest + 1; child < heap->count; child = 2 * dest + 1) {
		if (child + 1 < heap->count &&
		    heap->buckets[child + 1] < heap->buckets[child])
			child++;
		if (last <= heap->buckets[child])
			break;
		heap->buckets[dest] = heap->buckets[child];
		dest = child;
	}
	heap->buckets[dest] = last;
	return (min);
}

void
minheap_fini(minheap_t *heap)
{
	VERIFY(heap != NULL && heap->buckets != NULL);
	free(heap->buckets);
}
