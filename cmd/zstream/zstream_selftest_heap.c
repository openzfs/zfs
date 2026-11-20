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

/*
 * Selftests for heap_t.
 *
 * The order tests run a random mix of pushes and pops against both a heap
 * and a model (an unsorted array searched linearly), then drain the heap.
 * Each heap starts with room for one entry, so it must grow repeatedly.
 * The size limit test checks that growth stops at max_size.
 */

#include <assert.h>
#include <stdint.h>

#include "zstream_heap.h"
#include "zstream_selftest.h"

#define	HEAP_OPS	5000
#define	SENTINEL	0x5a5a5a5a5a5a5a5aULL

typedef struct {
	heap_order_t	m_order;
	uint64_t	m_count;
	uint64_t	m_values[HEAP_OPS];
} model_t;

/*
 * Half of the values come from a small set that includes the extremes, so
 * duplicates are common; the rest are arbitrary.
 */
static uint64_t
random_value(selftest_rng_t *rng)
{
	static const uint64_t common[] = {
		0, 1, 2, UINT64_MAX - 1, UINT64_MAX
	};
	if (selftest_rng_below(rng, 2) == 0)
		return (selftest_rng_next(rng));
	return (common[selftest_rng_below(rng,
	    sizeof (common) / sizeof (common[0]))]);
}

/* Index of the model value that the heap should pop next */
static uint64_t
model_root(const model_t *m)
{
	uint64_t best = 0;
	for (uint64_t i = 1; i < m->m_count; i++) {
		uint64_t v = m->m_values[i];
		if (m->m_order == HEAP_MIN ? v < m->m_values[best] :
		    v > m->m_values[best])
			best = i;
	}
	return (best);
}

static void
check_root(heap_t *heap, const model_t *m)
{
	VERIFY3U(heap_count(heap), ==, m->m_count);
	if (m->m_count == 0) {
		/* A stale root can't equal both */
		VERIFY0(heap_compare_root(heap, 0));
		VERIFY0(heap_compare_root(heap, UINT64_MAX));
		return;
	}
	uint64_t root = m->m_values[model_root(m)];
	VERIFY0(heap_compare_root(heap, root));
	if (root < UINT64_MAX)
		VERIFY3S(heap_compare_root(heap, root + 1), ==, -1);
	if (root > 0)
		VERIFY3S(heap_compare_root(heap, root - 1), ==, 1);
}

static void
pop_and_check(heap_t *heap, model_t *m)
{
	uint64_t value = SENTINEL;
	boolean_t popped = heap_pop(heap, &value);
	if (m->m_count == 0) {
		VERIFY(!popped);
		VERIFY3U(value, ==, SENTINEL);
		return;
	}
	uint64_t i = model_root(m);
	VERIFY(popped);
	VERIFY3U(value, ==, m->m_values[i]);
	m->m_values[i] = m->m_values[--m->m_count];
}

/*
 * Pushes outnumber pops 2:1 for HEAP_OPS operations; then the heap drains
 * and one more pop finds it empty.
 */
static void
heap_vs_model(heap_order_t order)
{
	static model_t m;
	selftest_rng_t rng;

	m.m_order = order;
	m.m_count = 0;
	selftest_rng_init(&rng, order);
	heap_t heap = heap_init(order, 1, 0);
	for (int op = 0; op < HEAP_OPS || m.m_count > 0; op++) {
		if (op < HEAP_OPS && selftest_rng_below(&rng, 3) != 0) {
			uint64_t value = random_value(&rng);
			heap_push(&heap, value);
			m.m_values[m.m_count++] = value;
		} else {
			pop_and_check(&heap, &m);
		}
		check_root(&heap, &m);
	}
	pop_and_check(&heap, &m);
	heap_fini(&heap);
}

static void
heap_min_order(void)
{
	heap_vs_model(HEAP_MIN);
}

static void
heap_max_order(void)
{
	heap_vs_model(HEAP_MAX);
}

/*
 * Growth from 1 doubles to 2 and 4, then clamps to the limit of 5. The
 * sixth push must fail.
 */
static void
overfill(void *arg)
{
	(void) arg;
	heap_t heap = heap_init(HEAP_MIN, 1, 5);
	for (uint64_t i = 0; i < 5; i++)
		heap_push(&heap, i);
	VERIFY3U(heap.size, ==, 5);
	heap_push(&heap, 5);
}

static void
heap_size_limit(void)
{
	selftest_expect_exit("overfill", overfill, NULL,
	    "heap size limit of 5 entries exceeded");
}

const test_case_t selftest_heap_cases[] = {
	{ "heap_min_order",	heap_min_order },
	{ "heap_max_order",	heap_max_order },
	{ "heap_size_limit",	heap_size_limit },
	{ NULL,			NULL },
};
