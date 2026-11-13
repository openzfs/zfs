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

#include <err.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/sysmacros.h>

#include "zstream_heap.h"
#include "zstream_util.h"

#define	precedes(heap, a, b)	(((heap)->order == HEAP_MIN) ? \
				    (a) < (b) : (a) > (b))

heap_t
heap_init(heap_order_t order, uint64_t initial_size, uint64_t max_size)
{
	VERIFY(order == HEAP_MIN || order == HEAP_MAX);
	VERIFY3U(initial_size, >, 0);
	VERIFY3U(initial_size, <=, SIZE_MAX / sizeof (uint64_t));
	VERIFY(max_size == 0 || max_size >= initial_size);
	heap_t heap = {
		.order = order,
		.size = initial_size,
		.max_size = max_size,
		.values = safe_malloc(initial_size * sizeof (uint64_t))
	};
	return (heap);
}

/*
 * Add a value to the heap, first doubling the array (up to max_size) if
 * it's full.
 *
 * The new value goes in the first unused slot and then sifts up, trading
 * places with its parent until the parent doesn't come after it.
 */
void
heap_push(heap_t *heap, uint64_t value)
{
	if (heap->count >= heap->size) {
		if (heap->max_size != 0 && heap->size >= heap->max_size) {
			errx(1, "heap size limit of %llu entries exceeded",
			    (u_longlong_t)heap->max_size);
		}
		uint64_t size = 2 * heap->size;
		if (heap->max_size != 0)
			size = MIN(size, heap->max_size);
		VERIFY3U(size, <=, SIZE_MAX / sizeof (uint64_t));
		heap->size = size;
		heap->values = safe_realloc(heap->values,
		    heap->size * sizeof (uint64_t));
	}
	uint64_t i = heap->count++;
	while (i > 0) {
		uint64_t parent = (i - 1) / 2;
		if (!precedes(heap, value, heap->values[parent]))
			break;
		heap->values[i] = heap->values[parent];
		i = parent;
	}
	heap->values[i] = value;
}

/*
 * Remove the root value from the heap and store it in *value, or return
 * B_FALSE if the heap is empty. The last element moves into the root's
 * slot and then sifts down, trading places with whichever child comes
 * first until neither child comes before it.
 */
boolean_t
heap_pop(heap_t *heap, uint64_t *value)
{
	if (heap->count == 0)
		return (B_FALSE);
	*value = heap->values[0];
	uint64_t last = heap->values[--heap->count];
	uint64_t child, dest = 0;
	for (child = 2 * dest + 1; child < heap->count; child = 2 * dest + 1) {
		if (child + 1 < heap->count && precedes(heap,
		    heap->values[child + 1], heap->values[child]))
			child++;
		if (!precedes(heap, heap->values[child], last))
			break;
		heap->values[dest] = heap->values[child];
		dest = child;
	}
	heap->values[dest] = last;
	return (B_TRUE);
}

int
heap_compare_root(heap_t *heap, uint64_t value)
{
	if (heap->count == 0 || heap->values[0] == value)
		return (0);
	return ((heap->values[0] < value) ? -1 : 1);
}

void
heap_fini(heap_t *heap)
{
	VERIFY(heap != NULL && heap->values != NULL);
	free(heap->values);
}
