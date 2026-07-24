// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the Common
 * Development and Distribution License ("CDDL"), version 1.0. You may only use
 * this file in accordance with the terms of version 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this source. A
 * copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
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
 *   checks that each producer's items arrive in the same order they
 *   were enqueued.
 *
 * - qi_check is a hash of (qi_seed, qi_producer, qi_seq). The processing
 *   function verifies it and then XORs in TRANSFORM_MAGIC. The consumer
 *   checks that the transform happened iff cost > 0.
 *
 * - qi_pattern[] is filled from qi_check and verified both by the process
 *   function and the consumer, to catch any corruption of the shallow
 *   copies in and out of the ring buffer.
 *
 * - qi_process_count counts invocations of the process function, which
 *   must be exactly one for cost > 0 items and zero for cost == 0 items.
 *
 * Global conservation checks: the number of items dequeued must equal the
 * number enqueued, and the total number of process-function invocations
 * must equal the number of nonzero-cost items enqueued.
 */

#include <atomic.h>
#include <err.h>
#include <pthread.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "zstream_queue.h"
#include "zstream_selftest.h"
#include "zstream_util.h"

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
	size_t		qc_queue_length;
	size_t		qc_batch_budget;
	size_t		qc_pattern_len;		/* Extra payload bytes */
	uint32_t	qc_zero_cost_pct;	/* % of items fast-tracked */
	size_t		qc_max_cost;		/* Nonzero costs are 1..max */
	uint32_t	qc_delay_pct;		/* % of items slept on */
	uint32_t	qc_max_delay_us;
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
	alignas(uint64_t) uint8_t item_buffer[sizeof (qtest_item_t) +
	    cfg->qc_pattern_len];
	qtest_item_t *item = (qtest_item_t *)item_buffer;

	pthread_register_self();
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

		if (item->qi_cost > 0 && cfg->qc_max_delay_us > 0) {
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
	alignas(uint64_t) uint8_t item_buffer[sizeof (qtest_item_t) +
	    cfg->qc_pattern_len];
	qtest_item_t *item = (qtest_item_t *)item_buffer;

	pthread_register_self();
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
			    sizeof (qtest_item_t) + cfgs[i].qc_pattern_len,
			.qp_batch_budget = cfgs[i].qc_batch_budget,
			.qp_queue_length = cfgs[i].qc_queue_length,
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
		.qc_queue_length = 64,
		.qc_batch_budget = 256,
		.qc_pattern_len = 32,
		.qc_zero_cost_pct = 20,
		.qc_max_cost = 64,
	};
	run_queue_workload(&cfg);
}

/*
 * A long, randomized stream with heavy-tailed processing delays, a large
 * fraction of fast-tracked items, costs that exceed the batch budget, and a
 * consumer that periodically stalls so the queue backs up and enqueue
 * blocks on Q_FULL. The ring indices wrap hundreds of times.
 */
static void
queue_torture(void)
{
	qtest_config_t cfg = {
		.qc_producers = 1,
		.qc_items = 100000,
		.qc_queue_length = 512,
		.qc_batch_budget = 2048,
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
 * Off-by-one hunting: sweep the degenerate corners of queue length,
 * batch budget, and stream length, including a zero-item stream and
 * zero-length payloads.
 */
static void
queue_edge_cases(void)
{
	static const size_t lengths[] =
	    { 1, 2, MAX_BATCH - 1, MAX_BATCH, MAX_BATCH + 1, 64 };
	static const size_t budgets[] = { 0, 1, 16, SIZE_MAX / 2 };
	uint64_t stream = 200;

	for (int l = 0; l < 6; l++) {
		for (int b = 0; b < 4; b++) {
			uint64_t counts[] = { 0, lengths[l], lengths[l] + 1,
			    4 * lengths[l] + 3 };
			for (int n = 0; n < 4; n++) {
				qtest_config_t cfg = {
					.qc_producers = 1,
					.qc_items = counts[n],
					.qc_queue_length = lengths[l],
					.qc_batch_budget = budgets[b],
					.qc_pattern_len =
					    (lengths[l] & 1) ? 0 : 24,
					.qc_zero_cost_pct = 25,
					.qc_max_cost = 8,
					.qc_rng_stream = stream++,
				};
				run_queue_workload(&cfg);
			}
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
		.qc_queue_length = 128,
		.qc_batch_budget = 1024,
		.qc_pattern_len = 16,
		.qc_zero_cost_pct = 100,
		.qc_max_cost = 8,
		.qc_rng_stream = 300,
	};
	run_queue_workload(&cfg);
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
		.qc_queue_length = 256,
		.qc_batch_budget = 512,
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
			.qc_queue_length = (size_t)4 << (i % 6),
			.qc_batch_budget =
			    (i % 4 == 0) ? 0 : (size_t)64 << (i % 5),
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

static void
expect_spindown(uint32_t baseline)
{
	/*
	 * thread_pool_spindown() joins its workers synchronously, but give
	 * the kernel a moment to retire task entries before declaring
	 * failure.
	 */
	for (int i = 0; i < 500; i++) {
		if (atomic_add_32_nv(&num_pthreads, 0) == baseline)
			return;
		(void) usleep(2000);
	}
	errx(1, "thread pool failed to spin down (%u threads, expected %u)",
	    atomic_add_32_nv(&num_pthreads, 0), baseline);
}

static void *
run_queue_workload_thread(void *arg)
{
	pthread_register_self();
	run_queue_workload(arg);
	return (NULL);
}

/*
 * Spin the thread pool up and down repeatedly. After every drain the
 * process must be back to its baseline thread count. The second phase
 * races a fresh zstream_queue_create() against the previous queue's
 * spin-down to exercise the create-vs-spindown locking.
 */
static void
queue_cycles(void)
{
	uint32_t baseline = atomic_add_32_nv(&num_pthreads, 0);
	qtest_config_t cfg = {
		.qc_producers = 1,
		.qc_items = 400,
		.qc_queue_length = 32,
		.qc_batch_budget = 64,
		.qc_pattern_len = 16,
		.qc_zero_cost_pct = 20,
		.qc_max_cost = 32,
		.qc_rng_stream = 600,
	};

	for (int iter = 0; iter < 30; iter++) {
		cfg.qc_producers = 1 + iter % 2;
		cfg.qc_rng_stream = 600 + iter;
		run_queue_workload(&cfg);
		expect_spindown(baseline);
	}

	selftest_rng_t rng;
	selftest_rng_init(&rng, 650);
	for (int iter = 0; iter < 10; iter++) {
		qtest_config_t racer = cfg;
		racer.qc_producers = 1;
		racer.qc_rng_stream = 700 + iter;
		pthread_t bg;
		VERIFY3S(pthread_create(&bg, NULL, run_queue_workload_thread,
		    &racer), ==, 0);
		(void) usleep(selftest_rng_below(&rng, 2000));
		cfg.qc_rng_stream = 800 + iter;
		run_queue_workload(&cfg);
		VERIFY3S(pthread_join(bg, NULL), ==, 0);
		expect_spindown(baseline);
	}
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
			qtest_config_t cfg = {
				.qc_producers = producers,
				.qc_items = (2000 +
				    selftest_rng_below(&rng, 8000)) /
				    producers,
				.qc_queue_length = (size_t)1 <<
				    selftest_rng_below(&rng, 10),
				.qc_batch_budget =
				    (selftest_rng_below(&rng, 3) == 0) ? 0 :
				    selftest_rng_below(&rng, 4096),
				.qc_pattern_len =
				    selftest_rng_below(&rng, 64),
				.qc_zero_cost_pct =
				    selftest_rng_below(&rng, 101),
				.qc_max_cost =
				    1 + selftest_rng_below(&rng, 2048),
				.qc_delay_pct = selftest_rng_below(&rng, 4),
				.qc_max_delay_us =
				    selftest_rng_below(&rng, 120),
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
	{ "queue_torture",		queue_torture },
	{ "queue_multi_producer",	queue_multi_producer },
	{ "queue_multi_queue",		queue_multi_queue },
	{ "queue_cycles",		queue_cycles },
	{ "queue_stress",		queue_stress },
	{ NULL,				NULL },
};
