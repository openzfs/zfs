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

#ifndef	_ZSTREAM_QUEUE_H
#define	_ZSTREAM_QUEUE_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <sys/stdtypes.h>

/*
 * This is a generalized implementation of multithreaded FIFO work queues.
 *
 * Callers define a fixed item size to be used by each queue and supply two
 * thread-safe functions that 1) estimate individual items' processing costs
 * and 2) perform the actual processing. The queue never inspects or
 * interprets items in the queue, so processing functions can modify them as
 * they wish.
 *
 * The cost function assigns a size_t cost that estimates the amount of work
 * needed to process an item. For operations like hashing and data
 * compression, the natural cost is typically payload length.
 *
 * It's expected that only a subset of input items will require processing.
 * If an item's cost is 0, it is fast-tracked and never presented to the
 * processing function.
 *
 * The cost function is run as items enter the queue, so it's
 * single-threaded and should return a value promptly. If cost estimation is
 * expensive and important, use a separate queue to implement it.
 *
 * Dispatch granularity is specified as a per-batch budget that is set for
 * each queue in the same (arbitrary) units used for item costs. Threads
 * claim items until the budget is met, there are no more items available,
 * or MAX_BATCH items have been claimed. When claiming items to work on,
 * threads never block waiting for additional work to arrive. They start
 * work as quickly as possible even if the budget has not been reached.
 *
 * All queues share a single thread pool that is managed to avoid
 * contention. Threads are assigned to queues dynamically according to
 * where work is available. When multiple queues have work, threads are
 * allocated among them stochastically with an eye toward preventing
 * pipeline stalls.
 */

#define	MAX_BATCH 16	/* The most items that can be claimed at once */

typedef void queue_item_t;

typedef struct zstream_queue zstream_queue_t;

/*
 * Signatures that cost and processing functions must conform to.
 */
typedef size_t
zq_estimate_cost_f(queue_item_t *item, void *context);

typedef void
zq_process_item_f(queue_item_t *item, void *context);

/*
 * Set the number of threads to be spawned for queue work. Since all queues
 * share a thread pool, this value affects all queues. The value must be set
 * before any queues are created. By default, one thread is spawned for
 * every CPU core.
 */
void
zstream_queue_set_num_threads(uint_t num_threads);

/*
 * Create a queue. The qp_context field is passed to the cost and processing
 * functions and is not examined by the queue itself.
 */

typedef struct {
	zq_process_item_f	*qp_process;
	zq_estimate_cost_f	*qp_cost;
	void			*qp_context;
	size_t			qp_item_size;
	size_t			qp_batch_budget;
	size_t			qp_queue_length;
} zq_params_t;

zstream_queue_t *
zstream_queue_create(zq_params_t *params);

/*
 * Submit a work item. Blocks if the queue is full. The work item is
 * shallow-copied into the queue. Multiple threads may enqueue at once.
 */
void
zstream_enqueue(zstream_queue_t *queue, queue_item_t *item);

/*
 * Retrieve a completed work item. The caller must provide a buffer into
 * which the dequeued item is shallow-copied. If the next item is not yet
 * ready, this call will block.
 *
 * If zstream_dequeue returns B_FALSE, the stream is complete. The returned
 * item is not valid and no further calls may be made on the queue.
 *
 * Only one thread may dequeue at once.
 */
boolean_t
zstream_dequeue(zstream_queue_t *queue, queue_item_t *item);

/*
 * Declare that all items have been submitted. The queue will continue to
 * function normally for dequeuers and worker threads until zstream_dequeue()
 * returns B_FALSE, at which point the queue will be destroyed.
 */
void
zstream_queue_fini(zstream_queue_t *queue);

#ifdef	__cplusplus
}
#endif

#endif	/* _ZSTREAM_QUEUE_H */
