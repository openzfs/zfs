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
 * Selftests for the zstream_queue multithreaded FIFO queue API.
 *
 * All tests are built on one generic workload runner. A workload is
 * described by a qtest_config_t: some number of producer threads each
 * enqueue a stream of self-describing items with randomized costs,
 * payloads, and processing delays, while one consumer thread per queue
 * dequeues and verifies. Several workloads can run concurrently on separate
 * queues to exercise the shared thread pool.
 *
 * Every item carries enough information to be verified independently:
 *
 * - The tuple (qi_producer, qi_seq) identifies each item; the consumer
 * checks that each producer's items arrive in the same order they were
 * enqueued.
 *
 * - qi_check is a hash of (qi_seed, qi_producer, qi_seq). The processing
 * function verifies it and then XORs in TRANSFORM_MAGIC. The consumer
 * checks that the transform happened iff cost > 0.
 *
 * - qi_pattern[] is filled from qi_check and verified both by the process
 * function and the consumer, to catch any corruption of the shallow copies
 * in and out of the ring buffer.
 *
 * - qi_process_count counts invocations of the process function, which must
 * be exactly one for cost > 0 items and zero for cost == 0 items.
 *
 * Global conservation checks: the number of items dequeued must equal the
 * number enqueued, and the total number of process-function invocations
 * must equal the number of nonzero-cost items enqueued.
 *
 * Costs are not passive values. Each queue measures how long its own work
 * takes per unit of cost and sizes its batches accordingly. So, a
 * workload's cost distribution decides how the implementation will behave.
 * Configs can set qc_ns_per_cost to make processing time genuinely
 * proportional to cost (the relationship the queue assumes), or leave it at
 * zero to get delays unrelated to cost. Both are worth testing; see
 * queue_batch_tuning().
 */

#include <assert.h>
#include <atomic.h>
#include <err.h>
#include <pthread.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <unistd.h>

#include "zstream_queue.h"
#include "zstream_selftest.h"

#define	TRANSFORM_MAGIC	0xf00dfeedbeefcafeULL

/*
 * Number of times per 1000 processing function invocations to use an
 * extra-long "outlier" processing delay to force overtly out-of-order
 * completion.
 */
#define	LONG_DELAYS_PER_THOUSAND	3
#define	LONG_DELAY_MULTIPLIER		20

typedef struct {
	uint32_t	qi_producer;
	uint32_t	qi_delay_us;
	uint64_t	qi_seq;
	uint64_t	qi_check;
	size_t		qi_cost;
	uint32_t	qi_process_count;
	uint8_t		qi_pattern[];
} qtest_item_t;

typedef struct {
	uint32_t	qc_producers;		/* Number of producers */
	uint64_t	qc_items;		/* Items per producer */
	size_t		qc_pattern_len;		/* Extra payload bytes */
	uint32_t	qc_zero_cost_pct;	/* % of items fast-tracked */
	size_t		qc_max_cost;		/* Nonzero costs are 1..max */
	uint32_t	qc_delay_pct;		/* % of items slept on */
	uint32_t	qc_max_delay_us;
	uint32_t	qc_ns_per_cost;		/* Delay of cost * this, ns */
	uint32_t	qc_producer_stall_pct;	/* % chance producer naps */
	uint32_t	qc_consumer_stall_pct;	/* % chance consumer naps */
	uint32_t	qc_stall_max_us;
	uint64_t	qc_rng_stream;		/* base PRNG stream number */
} qtest_config_t;

typedef struct {
	const qtest_config_t	*qr_cfg;
	zstream_queue_t		*qr_queue;
	uint32_t		qr_producers_left;
	uint64_t		qr_expect_processed;	/* Atomic */
	uint64_t		qr_processed;		/* Atomic */
	uint64_t		qr_dequeued;
} qtest_run_t;

typedef struct {
	qtest_run_t	*qp_run;
	uint32_t	qp_id;
} qtest_producer_arg_t;

static uint64_t
item_check_value(uint32_t producer, uint64_t seq)
{
	return (selftest_mix64(selftest_seed ^
	    (((uint64_t)producer << 40) + seq)));
}

static void
fill_pattern(uint8_t *pattern, size_t len, uint64_t check)
{
	for (size_t i = 0; i < len; i++)
		pattern[i] = (uint8_t)(check >> ((i & 7) << 3)) ^ (uint8_t)i;
}

static void
verify_pattern(const uint8_t *pattern, size_t len, uint64_t check,
    const char *who)
{
	for (size_t i = 0; i < len; i++) {
		uint8_t expect =
		    (uint8_t)(check >> ((i & 7) << 3)) ^ (uint8_t)i;
		if (pattern[i] != expect) {
			errx(1, "%s: payload corrupted at byte %zu "
			    "(0x%02x != 0x%02x)", who, i, pattern[i], expect);
		}
	}
}

static size_t
qtest_cost(void *item_in, void *context)
{
	(void) context;
	qtest_item_t *item = item_in;
	return (item->qi_cost);
}

static void
qtest_process(void *item_in, void *context)
{
	qtest_run_t *run = context;
	qtest_item_t *item = item_in;

	/* Cost-0 items should never reach the process function */
	VERIFY3U(item->qi_cost, >, 0);
	VERIFY3U(item->qi_check, ==,
	    item_check_value(item->qi_producer, item->qi_seq));
	verify_pattern(item->qi_pattern, run->qr_cfg->qc_pattern_len,
	    item->qi_check, "process");
	VERIFY3U(atomic_add_32_nv(&item->qi_process_count, 1), ==, 1);

	if (item->qi_delay_us > 0)
		(void) usleep(item->qi_delay_us);

	item->qi_check ^= TRANSFORM_MAGIC;
	atomic_add_64(&run->qr_processed, 1);
}

/*
 * Pthreads worker function for enqueuers
 */
static void *
qtest_producer(void *arg)
{
	qtest_producer_arg_t *pa = arg;
	qtest_run_t *run = pa->qp_run;
	const qtest_config_t *cfg = run->qr_cfg;
	uint64_t local_expect = 0;
	selftest_rng_t rng;
	alignas(__alignof__(uint64_t)) uint8_t item_buffer[
	    sizeof (qtest_item_t) + cfg->qc_pattern_len];
	qtest_item_t *item = (qtest_item_t *)item_buffer;

	selftest_rng_init(&rng, cfg->qc_rng_stream + 1000 + pa->qp_id);

	for (uint64_t seq = 0; seq < cfg->qc_items; seq++) {

		qtest_item_t item_xfer = {
			.qi_producer = pa->qp_id,
			.qi_seq = seq,
			.qi_process_count = 0,
			.qi_check = item_check_value(pa->qp_id, seq)
		};
		*item = item_xfer;
		fill_pattern(item->qi_pattern, cfg->qc_pattern_len,
		    item->qi_check);

		if (selftest_rng_below(&rng, 100) < cfg->qc_zero_cost_pct) {
			item->qi_cost = 0;
		} else {
			item->qi_cost =
			    1 + selftest_rng_below(&rng, cfg->qc_max_cost);
			local_expect++;
		}

		if (item->qi_cost > 0 && cfg->qc_ns_per_cost > 0) {
			/*
			 * Make processing time proportional to cost, which
			 * is the relationship the queue's batch sizing
			 * assumes. Costs here are small enough that the
			 * product can't overflow, but clamp anyway so that
			 * a future config can't turn a tuning test into a
			 * multi-second sleep.
			 */
			uint64_t ns = (uint64_t)item->qi_cost *
			    cfg->qc_ns_per_cost;
			item->qi_delay_us = MIN(ns / 1000, 100000);
		} else if (item->qi_cost > 0 && cfg->qc_max_delay_us > 0) {
			if (selftest_rng_below(&rng, 1000) <
			    LONG_DELAYS_PER_THOUSAND) {
				item->qi_delay_us = cfg->qc_max_delay_us *
				    LONG_DELAY_MULTIPLIER;
			} else if (selftest_rng_below(&rng, 100) <
			    cfg->qc_delay_pct) {
				item->qi_delay_us = selftest_rng_below(&rng,
				    cfg->qc_max_delay_us);
			}
		}

		if (cfg->qc_producer_stall_pct > 0 &&
		    selftest_rng_below(&rng, 100) < cfg->qc_producer_stall_pct)
			(void) usleep(selftest_rng_below(&rng,
			    cfg->qc_stall_max_us));

		zstream_enqueue(run->qr_queue, item);
	}

	atomic_add_64(&run->qr_expect_processed, local_expect);
	if (atomic_add_32_nv(&run->qr_producers_left, -1) == 0)
		zstream_queue_fini(run->qr_queue);
	return (NULL);
}

/*
 * Pthreads worker function for dequeuers
 */
static void *
qtest_consumer(void *arg)
{
	qtest_run_t *run = arg;
	const qtest_config_t *cfg = run->qr_cfg;
	selftest_rng_t rng;
	uint64_t expected_seq[cfg->qc_producers];
	alignas(__alignof__(uint64_t)) uint8_t item_buffer[
	    sizeof (qtest_item_t) + cfg->qc_pattern_len];
	qtest_item_t *item = (qtest_item_t *)item_buffer;

	memset(expected_seq, 0, sizeof (expected_seq));
	selftest_rng_init(&rng, cfg->qc_rng_stream + 999);

	while (zstream_dequeue(run->qr_queue, item)) {
		VERIFY3U(item->qi_producer, <, cfg->qc_producers);
		if (item->qi_seq != expected_seq[item->qi_producer]) {
			errx(1, "consumer: FIFO order violated: got "
			    "producer %u seq %ju, expected seq %ju",
			    item->qi_producer, (uintmax_t)item->qi_seq,
			    (uintmax_t)expected_seq[item->qi_producer]);
		}
		expected_seq[item->qi_producer]++;

		uint64_t check =
		    item_check_value(item->qi_producer, item->qi_seq);
		if (item->qi_cost > 0) {
			VERIFY3U(item->qi_process_count, ==, 1);
			VERIFY3U(item->qi_check, ==, check ^ TRANSFORM_MAGIC);
		} else {
			VERIFY3U(item->qi_process_count, ==, 0);
			VERIFY3U(item->qi_check, ==, check);
		}
		verify_pattern(item->qi_pattern, cfg->qc_pattern_len, check,
		    "consumer");
		run->qr_dequeued++;

		if (cfg->qc_consumer_stall_pct > 0 &&
		    selftest_rng_below(&rng, 100) < cfg->qc_consumer_stall_pct)
			(void) usleep(selftest_rng_below(&rng,
			    cfg->qc_stall_max_us));
	}

	for (uint32_t p = 0; p < cfg->qc_producers; p++)
		VERIFY3U(expected_seq[p], ==, cfg->qc_items);
	VERIFY3U(run->qr_dequeued, ==,
	    (uint64_t)cfg->qc_producers * cfg->qc_items);

	return (NULL);
}

/*
 * Run several workloads at once, one queue per config, with a dedicated
 * consumer thread and qc_producers producer threads per queue. Returns
 * after every queue has been drained to end-of-stream (and therefore
 * destroyed) and all verification checks have passed.
 */
static void
run_queue_workloads(const qtest_config_t *cfgs, int ncfg)
{
	qtest_run_t runs[ncfg];
	pthread_t consumers[ncfg];
	uint32_t total_producers = 0;

	for (int i = 0; i < ncfg; i++)
		total_producers += cfgs[i].qc_producers;

	pthread_t producers[total_producers];
	qtest_producer_arg_t pargs[total_producers];
	memset(runs, 0, sizeof (runs));
	memset(pargs, 0, sizeof (pargs));

	for (int i = 0; i < ncfg; i++) {
		runs[i].qr_cfg = &cfgs[i];
		runs[i].qr_producers_left = cfgs[i].qc_producers;
		zq_params_t params = {
			.qp_process = qtest_process,
			.qp_cost = qtest_cost,
			.qp_context = &runs[i],
			.qp_item_size =
			    sizeof (qtest_item_t) + cfgs[i].qc_pattern_len
		};
		runs[i].qr_queue = zstream_queue_create(&params);
	}

	int p = 0;
	for (int i = 0; i < ncfg; i++) {
		VERIFY3S(pthread_create(&consumers[i], NULL, qtest_consumer,
		    &runs[i]), ==, 0);
		for (uint32_t j = 0; j < cfgs[i].qc_producers; j++, p++) {
			pargs[p].qp_run = &runs[i];
			pargs[p].qp_id = j;
			VERIFY3S(pthread_create(&producers[p], NULL,
			    qtest_producer, &pargs[p]), ==, 0);
		}
	}

	for (uint32_t i = 0; i < total_producers; i++)
		VERIFY3S(pthread_join(producers[i], NULL), ==, 0);
	for (int i = 0; i < ncfg; i++)
		VERIFY3S(pthread_join(consumers[i], NULL), ==, 0);

	for (int i = 0; i < ncfg; i++)
		VERIFY3U(runs[i].qr_processed, ==, runs[i].qr_expect_processed);
}

static void
run_queue_workload(const qtest_config_t *cfg)
{
	run_queue_workloads(cfg, 1);
}

/*
 * Basic single-producer smoke test: deterministic-ish costs, no delays.
 */
static void
queue_basic(void)
{
	qtest_config_t cfg = {
		.qc_producers = 1,
		.qc_items = 5000,
		.qc_pattern_len = 32,
		.qc_zero_cost_pct = 20,
		.qc_max_cost = 64,
	};
	run_queue_workload(&cfg);
}

/*
 * A long, randomized stream with heavy-tailed processing delays, a large
 * fraction of fast-tracked items, and a consumer that periodically stalls.
 * The producer outruns the consumer badly enough that the queue sits at or
 * near ZQ_SLOTS_PER_QUEUE for most of the run, so this is the main exercise
 * of Q_FULL and of enqueuers blocking on the dequeued condition. 100000
 * items wrap the ring indices a couple of dozen times.
 */
static void
queue_torture(void)
{
	qtest_config_t cfg = {
		.qc_producers = 1,
		.qc_items = 100000,
		.qc_pattern_len = 64,
		.qc_zero_cost_pct = 30,
		.qc_max_cost = 4096,
		.qc_delay_pct = 5,
		.qc_max_delay_us = 100,
		.qc_consumer_stall_pct = 1,
		.qc_stall_max_us = 500,
		.qc_rng_stream = 100,
	};
	run_queue_workload(&cfg);
}

/*
 * Off-by-one hunting: sweep stream lengths that land on and adjacent to
 * every boundary the implementation has, including a zero-item stream and
 * zero-length payloads.
 *
 * Queue depth and batch size are no longer caller-supplied, so the
 * boundaries worth probing are the implementation's own: ZQ_MAX_BATCH, the
 * point at which claim_batch() stops filling a batch, and
 * ZQ_SLOTS_PER_QUEUE, the point at which the ring index wraps and Q_FULL
 * can trip. Both are exported by zstream_queue.h for exactly this purpose,
 * so this sweep tracks them automatically if either one is retuned.
 *
 * A stalling consumer is used for the counts at or above
 * ZQ_SLOTS_PER_QUEUE. Without it the consumer keeps up, the queue never
 * approaches full, and the wrap and Q_FULL paths go untested no matter how
 * many items are sent.
 */
static void
queue_edge_cases(void)
{
	static const uint64_t counts[] = {
		0, 1, 2, 3,
		ZQ_MAX_BATCH - 1, ZQ_MAX_BATCH, ZQ_MAX_BATCH + 1,
		2 * ZQ_MAX_BATCH,
		ZQ_SLOTS_PER_QUEUE - 1, ZQ_SLOTS_PER_QUEUE,
		ZQ_SLOTS_PER_QUEUE + 1, 2 * ZQ_SLOTS_PER_QUEUE + 3
	};
	const int ncounts = sizeof (counts) / sizeof (counts[0]);
	uint64_t stream = 200;

	for (int n = 0; n < ncounts; n++) {
		boolean_t big = counts[n] >= ZQ_SLOTS_PER_QUEUE;

		for (int p = 0; p < 2; p++) {
			qtest_config_t cfg = {
				.qc_producers = 1,
				.qc_items = counts[n],
				.qc_pattern_len = p ? 24 : 0,
				.qc_zero_cost_pct = 25,
				.qc_max_cost = 8,
				.qc_consumer_stall_pct = big ? 1 : 0,
				.qc_stall_max_us = big ? 200 : 0,
				.qc_rng_stream = stream++,
			};
			run_queue_workload(&cfg);
		}
	}
}

/*
 * All items cost 0, so every item takes the fast track and the process
 * function must never run (qtest_process VERIFYs cost > 0, and the
 * conservation check at the end of the run confirms zero invocations).
 * This exercises the completion-index sweep for items no worker ever
 * touches.
 */
static void
queue_zero_cost(void)
{
	qtest_config_t cfg = {
		.qc_producers = 1,
		.qc_items = 20000,
		.qc_pattern_len = 16,
		.qc_zero_cost_pct = 100,
		.qc_max_cost = 8,
		.qc_rng_stream = 300,
	};
	run_queue_workload(&cfg);
}

/*
 * Batch sizing is derived from each queue's own measured ns-per-unit-cost,
 * so the cost values reported by a caller now steer the implementation rather
 * than just gating the fast track. This test runs four queues at once whose
 * cost-to-time relationships are deliberately dissimilar and checks that
 * all of them still deliver every item exactly once and in order.
 *
 * The four cases, in the order configured below:
 *
 * - Faithful. Processing time really is proportional to cost, which is
 *   what the model assumes. Costs average ~2048 at 250ns each, putting a
 *   typical item just past the 400us target on its own, so batches come
 *   out at one or two items.
 *
 * - Too slow to batch. Every item costs 1 but takes 600us, so the implied
 *   budget is a fraction of a cost unit and has to be clamped up to 1.
 *   Batches should be single items; a rounding error that let the budget
 *   reach 0 would spin claim_batch() without claiming anything.
 *
 * - Too fast to measure. Costs are enormous and the work is nothing, so
 *   the implied budget overflows anything a size_t can hold and has to
 *   saturate. Sums of these costs wrap inside both claim_batch() and the
 *   queue's running total, which is allowed to produce silly batch sizes
 *   but must not produce wrong answers.
 *
 * - Uniform cost. Every item costs the same, the degenerate case for a
 *   ratio-based estimate.
 */
static void
queue_batch_tuning(void)
{
	qtest_config_t cfgs[] = {
		{
			.qc_producers = 2,
			.qc_items = 1500,
			.qc_pattern_len = 16,
			.qc_zero_cost_pct = 10,
			.qc_max_cost = 4096,
			.qc_ns_per_cost = 250,
			.qc_rng_stream = 600,
		}, {
			.qc_producers = 1,
			.qc_items = 400,
			.qc_pattern_len = 8,
			.qc_zero_cost_pct = 5,
			.qc_max_cost = 1,
			.qc_ns_per_cost = 600 * 1000,
			.qc_rng_stream = 610,
		}, {
			.qc_producers = 2,
			.qc_items = 5000,
			.qc_pattern_len = 32,
			.qc_zero_cost_pct = 20,
			.qc_max_cost = SIZE_MAX / 2,
			.qc_rng_stream = 620,
		}, {
			.qc_producers = 1,
			.qc_items = 20000,
			.qc_pattern_len = 0,
			.qc_zero_cost_pct = 0,
			.qc_max_cost = 1,
			.qc_rng_stream = 630,
		}
	};
	run_queue_workloads(cfgs, sizeof (cfgs) / sizeof (cfgs[0]));
}

/*
 * Eight producer threads hammering one queue with random pacing. The
 * consumer verifies per-producer FIFO order and exact counts.
 */
static void
queue_multi_producer(void)
{
	qtest_config_t cfg = {
		.qc_producers = 8,
		.qc_items = 15000,
		.qc_pattern_len = 24,
		.qc_zero_cost_pct = 25,
		.qc_max_cost = 512,
		.qc_delay_pct = 2,
		.qc_max_delay_us = 50,
		.qc_producer_stall_pct = 1,
		.qc_stall_max_us = 200,
		.qc_rng_stream = 400,
	};
	run_queue_workload(&cfg);
}

/*
 * Many dissimilar queues live at once, stressing worker scoring and
 * assignment, per-queue index isolation, and destruction of queues while
 * others remain active (which compacts the pool's queue array).
 */
static void
queue_multi_queue(void)
{
	qtest_config_t cfgs[12];
	for (int i = 0; i < 12; i++) {
		uint32_t producers = 1 + i % 3;
		qtest_config_t cfg = {
			.qc_producers = producers,
			.qc_items = 4000 / producers,
			.qc_pattern_len = 8 * (i % 5),
			.qc_zero_cost_pct = 10 * (i % 6),
			.qc_max_cost = (size_t)16 << (i % 8),
			.qc_delay_pct = i % 3,
			.qc_max_delay_us = 60,
			.qc_rng_stream = 500 + i * 10000,
		};
		cfgs[i] = cfg;
	}
	run_queue_workloads(cfgs, 12);
}

/*
 * Seeded chaos: randomize every workload parameter within sane bounds
 * and run a few rounds of concurrent queues. Whatever the targeted tests
 * miss, this net catches over many CI runs; failures replay with -s.
 */
static void
queue_stress(void)
{
	selftest_rng_t rng;
	selftest_rng_init(&rng, 900);

	for (int iter = 0; iter < 8; iter++) {
		int nqueues = 1 + selftest_rng_below(&rng, 4);
		qtest_config_t cfgs[4];

		for (int i = 0; i < nqueues; i++) {
			uint32_t producers = 1 + selftest_rng_below(&rng, 4);
			/*
			 * A quarter of the queues get processing time tied
			 * to cost, so the batch-size estimator sees a mix of
			 * well-behaved and meaningless cost data.
			 */
			uint32_t ns_per_cost =
			    (selftest_rng_below(&rng, 4) == 0) ?
			    1 + selftest_rng_below(&rng, 400) : 0;
			qtest_config_t cfg = {
				.qc_producers = producers,
				.qc_items = (2000 +
				    selftest_rng_below(&rng, 8000)) /
				    producers,
				.qc_pattern_len =
				    selftest_rng_below(&rng, 64),
				.qc_zero_cost_pct =
				    selftest_rng_below(&rng, 101),
				.qc_max_cost =
				    1 + selftest_rng_below(&rng, 2048),
				.qc_delay_pct = selftest_rng_below(&rng, 4),
				.qc_max_delay_us =
				    selftest_rng_below(&rng, 120),
				.qc_ns_per_cost = ns_per_cost,
				.qc_producer_stall_pct =
				    selftest_rng_below(&rng, 2),
				.qc_consumer_stall_pct =
				    selftest_rng_below(&rng, 2),
				.qc_stall_max_us =
				    selftest_rng_below(&rng, 400),
				.qc_rng_stream = 1000000 + iter * 1000 +
				    i * 100,
			};
			cfgs[i] = cfg;
		}
		run_queue_workloads(cfgs, nqueues);
	}
}

const test_case_t selftest_queue_cases[] = {
	{ "queue_basic",		queue_basic },
	{ "queue_edge_cases",		queue_edge_cases },
	{ "queue_zero_cost",		queue_zero_cost },
	{ "queue_batch_tuning",		queue_batch_tuning },
	{ "queue_torture",		queue_torture },
	{ "queue_multi_producer",	queue_multi_producer },
	{ "queue_multi_queue",		queue_multi_queue },
	{ "queue_stress",		queue_stress },
	{ NULL,				NULL },
};
