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

#include <time.h>
#include <assert.h>
#include <err.h>
#include <libspl.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/abd.h>
#include <sys/param.h>
#include <sys/stdtypes.h>
#include <sys/zio.h>
#include <sys/zstd/zstd.h>
#include <sys/zfs_refcount.h>
#include <zfs_fletcher.h>

#include "zstream_chain.h"
#include "zstream_queue.h"
#include "zstream_util.h"

#define	MAX_CHAIN_LENGTH 32

/*
 * Calculated information about a chain. Not to be confused with
 * chain_attrs_t, which is a global set of options and stream attributes
 * available to all chain steps.
 */
typedef struct {
	int	ct_num_steps;
	int	ct_num_queues;
	size_t	ct_item_size;
} chain_stats_t;

/*
 * Data passed to worker threads
 */
typedef struct {
	chain_step_t	*wc_steps;
	int		wc_num_steps;
	size_t		wc_buffer_size;
	zstream_queue_t	*wc_in_queue;
	zstream_queue_t	*wc_out_queue;
} worker_context_t;

typedef void *chain_worker_f(void *);

chain_attrs_t *chain_attrs;

chain_step_t
chain_terminator(void)
{
	chain_step_t step = { .cs_type = CS_TERMINATE };
	return (step);
}

static void
libraries_init(void)
{
	zfs_refcount_init();
	abd_init();
	zio_init();
	zstd_init();
	libspl_init();
	fletcher_4_init();
}

static void
libraries_fini(void)
{
	fletcher_4_fini();
	libspl_fini();
	zio_fini();
	zstd_fini();
	abd_fini();
	zfs_refcount_fini();
}

/*
 * Body function for worker threads
 */
static void *
zstream_chain_worker(worker_context_t *ctxt)
{
	uint8_t buffer[ctxt->wc_buffer_size];
	boolean_t done = B_FALSE;

	pthread_register_self();
	while (!done) {
		for (int i = 0; i < ctxt->wc_num_steps; i++) {
			chain_step_t *step = &ctxt->wc_steps[i];
			if (step->cs_type == CS_SERIAL) {
				if (done) {
					(void) step->cs_serial.process(NULL,
					    step->cs_context);
				} else {
					disposition_t dispo =
					    step->cs_serial.process(buffer,
					    step->cs_context);
					if (dispo == D_EOF) {
						done = B_TRUE;
					} else if (dispo == D_DROP) {
						break;
					}
				}
			} else if (i == 0) {
				done = done ||
				    !zstream_dequeue(ctxt->wc_in_queue, buffer);
			} else if (done) {
				zstream_queue_fini(ctxt->wc_out_queue);
			} else {
				zstream_enqueue(ctxt->wc_out_queue, buffer);
			}
		}
	}
	return (NULL);
}

/*
 * Validate chain and calculate number of steps, max packet size, and number
 * of zstream_queues that must be created.
 */
static chain_stats_t
validate_chain(zstream_chain_t chain)
{
	int num_steps = 0;
	int num_queues = 0;
	size_t item_size = 0;

	while (chain[num_steps].cs_type != CS_TERMINATE) {
		if (num_steps >= MAX_CHAIN_LENGTH) {
			errx(1, "unterminated zstream_chain");
		}
		chain_step_t *step = &chain[num_steps];
		item_size = MAX(item_size, step->cs_out_size);
		if (step->cs_type == CS_PARALLEL) {
			num_queues++;
		}
		num_steps++;
	}
	VERIFY3U(num_steps, >, 0);

	boolean_t first_parallel = chain[0].cs_type == CS_PARALLEL;
	boolean_t last_parallel = chain[num_steps-1].cs_type == CS_PARALLEL;
	if (first_parallel || last_parallel) {
		errx(1, "a chain cannot start or end with a parallel step");
	}

	/*
	 * Check for consistency of input and output packet sizes in
	 * adjacent steps.
	 */
	for (int i = 0; i < num_steps; i++) {
		if (i > 0 && chain[i].cs_in_size != chain[i-1].cs_out_size) {
			warnx("adjacent chain steps %d and %d declare "
			    "incompatible packet sizes", i - 1, i);
		}
	}

	chain_stats_t stats = {
	    .ct_num_steps = num_steps,
	    .ct_num_queues = num_queues,
	    .ct_item_size = item_size
	};
	return (stats);
}

/*
 * Execute a chain of processing steps, some parallel and some serial.
 *
 * For simplicity, we normalize the chain item size to that of the largest
 * output of any step. Payloads are allocated on the heap, so the maximal
 * item size is typically on the order of 512 bytes.
 *
 * Packets with data beyond the base drr_record_t should add their
 * additional data to the end of the packet, and this area may be reused for
 * different purposes as items travel down the chain.
 *
 * One worker thread is assigned to every contiguous sequence of serial
 * steps plus the parallel steps on either side of that block (if any).
 * Adjacent parallel steps also receive a worker. This isn't a special case,
 * it's just the same rule with the serial block consisting of zero steps.
 *
 * Parallel steps are double-covered, which is the intended behavior. If a
 * worker's domain begins with a parallel step, it dequeues items from the
 * associated queue. If the domain ends with a parallel step, it submits
 * items to that queue.
 */
void
zstream_chain_exec(zstream_chain_t chain, chain_attrs_t *attrs)
{
	chain_stats_t stats = validate_chain(chain);

	int num_workers = stats.ct_num_queues + 1;
	worker_context_t contexts[num_workers];
	pthread_t worker_threads[num_workers];
	zstream_queue_t *queue;

	/*
	 * Create parallel queues and worker thread contexts
	 *
	 * We do not need to track zstream_queues independently of worker
	 * contexts because queues clean themselves up once the last item
	 * has been dequeued. The stream eventually ends, so some worker
	 * thread will eventually call zstream_queue_fini() on every queue.
	 */

	int worker = 0;

	worker_context_t context = {
	    .wc_steps = chain,
	    .wc_num_steps = 0,
	    .wc_buffer_size = stats.ct_item_size,
	    .wc_in_queue = NULL
	};
	contexts[worker] = context;

	for (int i = 0; i < stats.ct_num_steps; i++) {
		contexts[worker].wc_num_steps++;
		if (chain[i].cs_type == CS_PARALLEL) {
			chain_step_t *cs = &chain[i];
			zq_params_t queue_params = {
				.qp_process	 = cs->cs_parallel.process,
				.qp_cost	 = cs->cs_parallel.cost,
				.qp_item_size	 = stats.ct_item_size,
				.qp_batch_budget = cs->cs_parallel.batch_budget,
				.qp_queue_length = cs->cs_parallel.queue_length,
				.qp_context	 = cs->cs_context
			};
			queue = zstream_queue_create(&queue_params);
			contexts[worker].wc_out_queue = queue;
			worker++;
			worker_context_t next_context = {
			    .wc_steps = cs,
			    .wc_num_steps = 1,
			    .wc_buffer_size = stats.ct_item_size,
			    .wc_in_queue = queue
			};
			contexts[worker] = next_context;
		}
	}

	contexts[worker].wc_out_queue = NULL;

	chain_attrs_t backup_attrs = {0};
	chain_attrs = attrs ? attrs : &backup_attrs;

	libraries_init();

	/* Spawn threads */
	for (int i = 0; i < num_workers; i++) {
		char buff[32];
		int ret = pthread_create(&worker_threads[i], NULL,
		    (chain_worker_f *)zstream_chain_worker,
		    &contexts[i]);
		VERIFY3S(ret, ==, 0);
		snprintf(buff, sizeof (buff), "chain-%d", i);
		pthread_setname_np(worker_threads[i], buff);
	}

	/* Reap threads */
	for (int i = 0; i < num_workers; i++) {
		int ret = pthread_join(worker_threads[i], NULL);
		VERIFY3S(ret, ==, 0);
	}

	libraries_fini();
}

/*
 * Execute a chain linearly, without queues and without multithreading. This
 * form of execution is intended as a debugging aid, both for clients and
 * for the chain mechanism itself. If this variant doesn't produce results
 * identical to zstream_chain_exec(), there's a multithreading-related bug
 * somewhere.
 *
 * It is not necessary to remove parallel steps from the input chain. They
 * are accepted as-is, but their execution won't be parallelized.
 */
void
zstream_chain_exec_serialized(zstream_chain_t chain, chain_attrs_t *attrs)
{
	chain_stats_t stats = validate_chain(chain);

	uint8_t buffer[stats.ct_item_size];
	boolean_t done = B_FALSE;

	chain_attrs_t backup_attrs = {0};
	chain_attrs = attrs ? attrs : &backup_attrs;

	libraries_init();

	while (!done) {
		for (int i = 0; i < stats.ct_num_steps; i++) {
			chain_step_t *step = &chain[i];
			if (step->cs_type == CS_SERIAL) {
				if (done) {
					(void) step->cs_serial.process(NULL,
					    step->cs_context);
				} else {
					disposition_t dispo =
					    step->cs_serial.process(buffer,
					    step->cs_context);
					if (dispo == D_EOF) {
						done = B_TRUE;
					} else if (dispo == D_DROP) {
						break;
					}
				}
			} else if (!done) {
				size_t cost = step->cs_parallel.cost(buffer,
				    step->cs_context);
				if (cost > 0) {
					step->cs_parallel.process(buffer,
					    step->cs_context);
				}
			}
		}
	}

	libraries_fini();
}
