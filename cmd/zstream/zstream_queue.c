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

#include <assert.h>
#include <atomic.h>
#include <err.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/random.h>
#include <sys/stdtypes.h>
#include <unistd.h>

#include "zstream_queue.h"
#include "zstream_util.h"

#define	MIN_THREADS 	6
#define	MAX_QUEUES 	16	/* Largest # of queues simultaneously active */

#define	PLENTY_OF_WORK		6	/* "Many" items to claim */
#define	NO_WORK			0.0001	/* No-work score threshold */
#define	DEQUEUE_SCORE_WEIGHT	0.3	/* Dequeue score relative weight */

#define	Q_MOD(queue, index)	((index) % (queue)->zq_params.qp_queue_length)
#define	Q_SLOT(queue, index)	((queue)->zq_slots[Q_MOD((queue), (index))])

#define	Q_FULL(queue)	((queue)->zq_ix.enqueue - (queue)->zq_ix.dequeue >= \
	    (queue)->zq_params.qp_queue_length)

/*
 * A zstream_queue is a ring buffer with four indices: enqueue, claim,
 * complete, and dequeue, in that order. No index can move beyond its
 * preceding index. Every interval between indices contains work items in a
 * particular state: enqueued, claimed for work, or completed. Items never
 * leave the ring buffer, so FIFO order is guaranteed on dequeueing.
 *
 * In concept, every index has a corresponding condition that threads can
 * wait on if they are interested in knowing when that index moves:
 * enqueued, claimed, completed, dequeued. However, the exact reality
 * deviates from this model in two ways:
 *
 * - There is no "claimed" condition, because no thread would wait on it.
 * Claiming and processing are one unified operation. Dequeuers await the
 * "completed" condition.
 *
 * - All queues share one thread pool, so idle threads are not bound to any
 * particular queue. Instead of having queue-specific "enqueued" conditions,
 * one condition is shared by all queues. On awakening, each worker thread
 * is dynamically assigned to a queue using a scoring mechanism described
 * in the comments at score_queue().
 *
 * THREAD SAFETY STRATEGY
 *
 * There are four types of lock:
 *
 * - One global lock that gates changes to the thread pool and queue cohort
 * - A second global lock that controls the creation of new queues
 * - A third global lock associated with the shared "enqueued" condition
 * - One lock for each queue
 *
 * Although the "enqueued" condition and its associated lock are stored as
 * part of the global thread pool, they are logically separate from the pool
 * itself.
 *
 * For the most part, locking is straightforward. Any operation that adds or
 * removes queues or threads should hold the pool lock. Any operation that
 * moves a queue's indices should hold the queue lock. Any thread waiting
 * for work should wait on the "enqueued" condition.
 *
 * Worker threads hold no locks while they are actually processing items.
 *
 * Several operations require multiple locks. In these cases, a standardized
 * locking order is used to avoid deadlocks:
 *
 *   enqueue -> pool -> queue -> create
 *
 * Several operations merit additional comments about locking. These are
 * marked with a "locking note" in the comments preceding the relevant
 * function.
 */

typedef struct {
	queue_item_t	*qs_item;
	size_t		qs_cost;
	boolean_t	qs_completed;
	boolean_t	qs_end_of_stream;
} queue_slot_t;

typedef struct {
	uint64_t	enqueue;
	uint64_t	claim;
	uint64_t	complete;
	uint64_t	dequeue;
} zq_indices_t;

typedef struct {
	pthread_cond_t	completed;
	pthread_cond_t	dequeued;
} zq_conditions_t;

typedef struct {
	int		min_depth;
	int		max_depth;
} zq_stats_t;

struct zstream_queue {
	queue_slot_t	*zq_slots;
	pthread_mutex_t	zq_mutex;
	zq_indices_t	zq_ix;
	zq_conditions_t	zq_cond;
	zq_params_t	zq_params;
	zq_stats_t	zq_stats;
	boolean_t	zq_disallow_enqueue;
};

typedef struct {
	pthread_mutex_t	tp_pool_mutex;
	pthread_mutex_t tp_create_mutex;
	pthread_mutex_t tp_enqueue_mutex;
	pthread_cond_t	tp_enqueued;
	zstream_queue_t	*tp_queues[MAX_QUEUES];
	int		tp_num_queues;
	pthread_t	*tp_threads;
	int		tp_num_threads;
} thread_pool_t;

typedef void cleanup_f(void *);

static void *
queue_worker(void *);

#ifdef MONITOR_QUEUES
static void
start_monitor_thread(void);
#endif

static thread_pool_t	pool = {0};
static int		num_threads = 0;
static boolean_t	pool_initialized = B_FALSE;
static pthread_once_t	once_control = PTHREAD_ONCE_INIT;

void
zstream_queue_set_num_threads(uint_t n)
{
	if (pool_initialized) {
		errx(1, "thread pool size must be set before creating queues");
	} else if (n == 0) {
		errx(1, "number of threads must be at least 1");
	} else if (n < MIN_THREADS) {
		warnx("using only %u threads may limit performance, setting "
		    "anyway...", n);
	} else if (n > 256) {
		warnx("num_threads = %u seems suspiciously high, setting "
		    "anyway...", n);
	}
	num_threads = n;
}

static void
thread_pool_init(void)
{
	pthread_mutex_init(&pool.tp_pool_mutex, NULL);
	pthread_mutex_init(&pool.tp_create_mutex, NULL);
	pthread_mutex_init(&pool.tp_enqueue_mutex, NULL);
	pthread_cond_init(&pool.tp_enqueued, NULL);
	pool_initialized = B_TRUE;
}

/*
 * Locking note: must be called by a function holding the pool mutex
 *
 * If num_threads is nonzero, it sets the number of threads to spawn.
 * Otherwise, one thread is spawned per core.
 *
 * sched_affinity() is a better estimate of available threads than sysconf
 * because sysconf doesn't account for limits that might be set on, e.g., a
 * container.
 */
static void
thread_pool_spinup(void)
{
	pool.tp_num_queues = 0;
	if (num_threads > 0) {
		pool.tp_num_threads = num_threads;
	} else {
#ifdef	CPU_COUNT
		cpu_set_t cpu_set;
		sched_getaffinity(0, sizeof (cpu_set_t), &cpu_set);
		pool.tp_num_threads = CPU_COUNT(&cpu_set);
#else
		pool.tp_num_threads = sysconf(_SC_NPROCESSORS_ONLN);
#endif
		pool.tp_num_threads = MAX(pool.tp_num_threads, MIN_THREADS);
	}
	pool.tp_threads = safe_malloc(sizeof (pthread_t) * pool.tp_num_threads);
	for (int i = 0; i < pool.tp_num_threads; i++) {
		char buff[32];
		pthread_t *thread = &pool.tp_threads[i];
		int ret = pthread_create(thread, NULL, queue_worker, NULL);
		VERIFY3S(ret, ==, 0);
		snprintf(buff, sizeof (buff), "queue-%d", i);
		pthread_setname_np(*thread, buff);
	}
#ifdef MONITOR_QUEUES
	start_monitor_thread();
#endif
}

/*
 * Locking note: thread_pool_spindown() is a pool-level operation and by
 * rights should hold the pool mutex. (And in fact, the caller must already
 * hold that mutex.)
 *
 * However, we can't leave the pool mutex locked while canceling threads
 * because most worker threads will be waiting on the "enqueued" condition.
 * That condition is protected by the enqueue mutex, which threads need to
 * lock just to wake up and be canceled.
 *
 * Even though the two mutexes are locked by different threads, it is still
 * one composite operation for which the locking order is pool -> enqueue.
 * That's incompatible with the standard locking order of enqueue -> pool ->
 * queue -> create, so continuing to hold the pool mutex risks deadlock.
 *
 * If we are here, that means there are no existing queues, so we needn't
 * worry about operations being attempted on queues. The one potential
 * conflict is with zstream_queue_create(). That's the reason for the
 * seemingly redundant "create" mutex. It lets us prevent the creation of
 * new queues while simultaneously dropping the pool lock.
 *
 * This function must be called with the pool mutex held, and it returns
 * with the pool mutex unlocked.
 */
static void
thread_pool_spindown(void)
{
	pthread_mutex_lock(&pool.tp_create_mutex);
	pthread_mutex_unlock(&pool.tp_pool_mutex);

	for (int i = 0; i < pool.tp_num_threads; i++) {
		VERIFY3S(pthread_cancel(pool.tp_threads[i]), ==, 0);
		VERIFY3S(pthread_join(pool.tp_threads[i], NULL), ==, 0);
	}
	free(pool.tp_threads);
	pool.tp_threads = NULL;
	pool.tp_num_threads = 0;

	pthread_mutex_unlock(&pool.tp_create_mutex);
}

/*
 * Locking note: see comments on thread_pool_spindown() for an explanation
 * of why this operation acquires two locks.
 */
zstream_queue_t *
zstream_queue_create(zq_params_t *params)
{
	pthread_once(&once_control, thread_pool_init);
	pthread_mutex_lock(&pool.tp_pool_mutex);
	pthread_mutex_lock(&pool.tp_create_mutex);
	VERIFY3S(pool.tp_num_queues, <, MAX_QUEUES);

	if (!pool.tp_num_threads) {
		thread_pool_spinup();
	}
	zstream_queue_t *queue = safe_malloc(sizeof (zstream_queue_t));
	pool.tp_queues[pool.tp_num_queues] = queue;

	size_t qpis_rounded = P2ROUNDUP(params->qp_item_size, 8);
	zstream_queue_t new_queue = {
		.zq_params = *params,
		.zq_slots = safe_malloc(params->qp_queue_length *
		    ((sizeof (queue_slot_t)) + qpis_rounded))
	};
	*queue = new_queue;
	/*
	 * Queue slots and item storage are allocated in one block, so we
	 * need to manually wire each slot to its item buffer.
	 */
	uint8_t *item = (uint8_t *)&queue->zq_slots[params->qp_queue_length];
	queue_slot_t *slot = &queue->zq_slots[0];
	for (int i = 0; i < params->qp_queue_length; i++) {
		slot->qs_item = item;
		item += qpis_rounded;
		slot++;
	}

	pthread_mutex_init(&queue->zq_mutex, NULL);
	pthread_cond_init(&queue->zq_cond.completed, NULL);
	pthread_cond_init(&queue->zq_cond.dequeued, NULL);

	pool.tp_num_queues++;

	pthread_mutex_unlock(&pool.tp_create_mutex);
	pthread_mutex_unlock(&pool.tp_pool_mutex);

	return (queue);
}

/*
 * Try to advance the "complete" index as far as possible by examining the
 * qs_completed flag on each item. This can't be done directly by the
 * threads that complete work, for a couple of reasons:
 *
 * - Items can be completed in any order. Just because you (a thread) have
 * finished your batch doesn't mean that all prior batches have completed.
 * If there are uncompleted items ahead of you in the ring buffer, you can't
 * advance the completion index past them.
 *
 * - Items for which the cost function returns 0 are marked as qs_completed
 * on enqueue and are never seen by a worker thread. So, there needs to be
 * an independent mechanism to sweep the completion index past these items
 * whenever that becomes possible.
 *
 * This function is called:
 *
 * - Whenever a thread completes a batch
 * - Whenever a thread claims a batch
 * - Whenever an item of cost 0 is enqueued
 *
 * Strictly speaking, advancing on claiming a batch is not logically
 * necessary. However, the claimer already holds the queue mutex, and
 * it's in our interest to make completed items available for dequeueing as
 * expeditiously as possible.
 *
 * Locking note: the calling thread must hold the queue mutex.
 */
static inline void
advance_completion_index(zstream_queue_t *queue)
{
	boolean_t any_completed = B_FALSE;
	while (queue->zq_ix.complete < queue->zq_ix.claim &&
	    Q_SLOT(queue, queue->zq_ix.complete).qs_completed) {
		queue->zq_ix.complete++;
		any_completed = B_TRUE;
	}
	if (any_completed) {
		pthread_cond_signal(&queue->zq_cond.completed);
	}
}

/*
 * Locking note: the calling thread must hold the enqueue mutex and the
 * thread pool mutex.
 *
 * This function scores a queue according to its need for workers. Higher is
 * better. The scoring tries to assign threads to queues that are running
 * out of space for new enqueuements or that have little completed work
 * available to dequeue. The broader goal is to try to avoid pipeline
 * stalls.
 *
 * Two measures are used for scoring. The "open score" is 1/M where M is the
 * number of slots available to receive new items. The "dequeue score" is
 * 1/N where N is the number of completed items available to dequeue. These
 * two measures are added together with the dequeue score scaled by
 * DEQUEUE_SCORE_WEIGHT.
 *
 * The composite score is scaled by a factor that reflects how much work is
 * actually available to be claimed on the queue; there's no point assigning
 * threads to queues that have no work.
 *
 * To score queues, a thread must hold both the thread pool mutex and the
 * global enqueue mutex. However, it does not need to hold the mutex for the
 * queue being scored. Several corollaries:
 *
 * 1) Only one thread may score queues at a time.
 *
 * 2) Worker threads can still complete work during scoring, so queue scores
 * may become stale before they are used.
 *
 * 3) If a queue score is stale, it will always err on the side of
 * overstating the amount of work that a queue has available. This is fine
 * because at worst, a thread is assigned to a no-work queue and loops
 * immediately.
 *
 * 4) Understatement is not possible because the enqueue mutex is locked
 * during scoring. We don't want a queue to be scored and then receive new
 * work while the scorer is looking at other queues. That would create a
 * potential race condition in which a scorer concludes that there is no
 * work available on any queue and goes back to sleep. If no further items
 * are submitted to any queue, no worker thread will ever be awakened to
 * process the newly-enqueued item.
 */
static inline double
score_queue(zstream_queue_t *queue)
{
	uint64_t claimable = queue->zq_ix.enqueue - queue->zq_ix.claim;
	uint64_t dequeueable = queue->zq_ix.complete - queue->zq_ix.dequeue;
	uint64_t in_queue = queue->zq_ix.enqueue - queue->zq_ix.dequeue;
	uint64_t open_slots = queue->zq_params.qp_queue_length - in_queue;

	double open_score = (open_slots > 0) ? (1.0 / open_slots) : 2.0;
	double dq_score = (dequeueable > 0) ? (1.0 / dequeueable) : 2.0;
	double claim_factor = MIN(claimable, PLENTY_OF_WORK) /
	    (double)PLENTY_OF_WORK;
	double need = open_score + dq_score * DEQUEUE_SCORE_WEIGHT;
	return (need * claim_factor);
}

/*
 * Return a random index from an array of doubles, with the likelihood of
 * index i being selected equal to weights[i] / sum(weights).
 */
static inline int
select_stochastic(double weights[], int num_values)
{
	uint32_t numerator;
	uint32_t denominator = UINT32_MAX;
	double total = 0.0;

	for (int i = 0; i < num_values; i++) {
		total += weights[i];
	}
	random_get_pseudo_bytes((uint8_t *)&numerator, sizeof (uint32_t));
	double select_val = total * numerator / denominator;
	for (int i = 0; i < num_values; i++) {
		if (select_val <= weights[i])
			return (i);
		select_val -= weights[i];
	}
	return (num_values - 1);
}

static void
auto_unlock_mutex(pthread_mutex_t *mutex)
{
	pthread_mutex_unlock(mutex);
}

static void
await_condition(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
	pthread_cleanup_push((cleanup_f *)auto_unlock_mutex, mutex);
	pthread_cond_wait(cond, mutex);
	pthread_cleanup_pop(0);
}

/*
 * Claim up to MAX_BATCH work items from the given queue, trying to
 * accumulate at least queue->qp_batch_budget worth of work data (==
 * "cost"). All items in a batch will be drawn from the same queue.
 *
 * Does not block waiting to fill the budget; returns whatever is available
 * now.
 *
 * Locking note: this function must be called with both the queue mutex and
 * the thread pool mutex held. zstream_queue_destroy() can't hold a queue's
 * mutex while destroying it (because destruction entails destroying the
 * queue mutex, which must be unlocked), so holding the queue mutex while
 * attempting to claim work is not a sufficient guarantee of correctness.
 *
 * In other contexts, we have more certainty about whether a queue still has
 * work to do. If it does, it can't be destroyed while we hold the queue
 * mutex alone. But here, we merely suspect that there's work available
 * based on possibly outdated queue scoring information. By the time we get
 * here, the queue might already have been finalized. Holding the thread
 * pool mutex guarantees that the queue won't have been destroyed out from
 * under us.
 */
static int
claim_batch(zstream_queue_t *queue, queue_slot_t **batch)
{
	size_t cost_claimed = 0;
	int count = 0;
	boolean_t more_to_claim, more_slots, more_budget;
	boolean_t first_and_only, ok_to_claim;

	while (B_TRUE) {
		more_to_claim = queue->zq_ix.claim < queue->zq_ix.enqueue;
		more_slots = count < MAX_BATCH;
		more_budget = cost_claimed < queue->zq_params.qp_batch_budget;
		first_and_only = queue->zq_params.qp_batch_budget == 0 &&
		    count == 0;
		ok_to_claim = first_and_only || more_budget;

		if (!more_to_claim || !more_slots || !ok_to_claim) {
			break;
		}
		queue_slot_t *slot = &Q_SLOT(queue, queue->zq_ix.claim);
		if (!slot->qs_completed) {
			cost_claimed += slot->qs_cost;
			batch[count++] = slot;
		}
		queue->zq_ix.claim++;
	}

	advance_completion_index(queue);
	return (count);
}

/*
 * Threads are assigned to a queue on each loop so they can be shifted
 * dynamically to follow available work. Idle threads will typically
 * be awaiting the "enqueued" condition within this function.
 *
 * Locking note: this function has complex locking behavior. At first we
 * must hold both the enqueue mutex (to be sure new work doesn't get sneaked
 * in after a queue is scored, which might cause it to be overlooked
 * entirely) and the thread pool mutex (to guarantee that no queue can be
 * destroyed out from under us).
 *
 * After scoring, we can release the enqueue mutex. However, we need to then
 * obtain the mutex of the selected queue without releasing the pool mutex
 * because there is still the potential for a claim-vs-destroy race.
 *
 * This sequence dictates the lock acquisition ordering for all of
 * zstream_queue:
 *
 *   enqueue -> pool -> queue -> create
 *
 * If everyone follows that order, deadlocks can't occur. Unfortunately,
 * thread_pool_spindown() would like to acquire two of these locks in the
 * wrong order, so it uses a separate work-around. See the comments for that
 * function.
 */
static int
assign_queue_and_get_work(zstream_queue_t **queue, queue_slot_t **batch)
{
	pthread_mutex_lock(&pool.tp_enqueue_mutex);
	pthread_mutex_lock(&pool.tp_pool_mutex);

	while (B_TRUE) {
		int num_queues = pool.tp_num_queues;
		double weights[MAX_QUEUES];
		int queues_with_work = 0;

		for (int i = 0; i < num_queues; i++) {
			weights[i] = score_queue(pool.tp_queues[i]);
			if (weights[i] > NO_WORK)
				queues_with_work++;
		}
		if (!queues_with_work) {
			pthread_mutex_unlock(&pool.tp_pool_mutex);
			await_condition(&pool.tp_enqueued,
			    &pool.tp_enqueue_mutex);
			pthread_mutex_lock(&pool.tp_pool_mutex);
		} else {
			pthread_mutex_unlock(&pool.tp_enqueue_mutex);
			int q = select_stochastic(weights, num_queues);
			*queue = pool.tp_queues[q];
			pthread_mutex_lock(&(*queue)->zq_mutex);
			int count = claim_batch(*queue, batch);
			/*
			 * If we didn't claim all available work, wake up
			 * another worker thread.
			 */
			boolean_t more_here = (*queue)->zq_ix.claim <
			    (*queue)->zq_ix.enqueue;
			if (more_here || queues_with_work > 1) {
				pthread_cond_signal(&pool.tp_enqueued);
			}
			pthread_mutex_unlock(&(*queue)->zq_mutex);
			pthread_mutex_unlock(&pool.tp_pool_mutex);
			return (count);
		}
	}
}

static uint32_t items_claimed = 0;  /* Used for tuning/debugging */

static void *
queue_worker(void *dummy)
{
	(void) dummy;
	zstream_queue_t *queue;
	queue_slot_t *batch[MAX_BATCH];
	int count;

	pthread_register_self();
	while (B_TRUE) {
		count = assign_queue_and_get_work(&queue, batch);
		if (count) {
			zq_process_item_f *process =
			    queue->zq_params.qp_process;
			void *context = queue->zq_params.qp_context;
			atomic_add_32(&items_claimed, count);
			/*
			 * Locking note: we complete the whole batch without
			 * holding any locks. However, we can't mark items
			 * as completed without holding the queue lock
			 * because that creates a race condition with
			 * advance_completion_index().
			 */
			for (int i = 0; i < count; i++) {
				process(batch[i]->qs_item, context);
			}
			pthread_mutex_lock(&queue->zq_mutex);
			for (int i = 0; i < count; i++) {
				batch[i]->qs_completed = B_TRUE;
			}
			advance_completion_index(queue);
			atomic_sub_32(&items_claimed, count);
			pthread_mutex_unlock(&queue->zq_mutex);
		}
	}
	return (NULL);
}

/*
 * Implements both _enqueue and _fini. item == NULL for fini.
 */
void
zstream_enqueue(zstream_queue_t *queue, queue_item_t *item)
{
	pthread_mutex_lock(&queue->zq_mutex);
	VERIFY3B(queue->zq_disallow_enqueue, ==, B_FALSE);

	while (Q_FULL(queue)) {
		await_condition(&queue->zq_cond.dequeued, &queue->zq_mutex);
	}

	queue_slot_t *slot = &Q_SLOT(queue, queue->zq_ix.enqueue);
	if (item) {
		slot->qs_cost =
		    queue->zq_params.qp_cost(item, queue->zq_params.qp_context);
		slot->qs_completed = slot->qs_cost == 0;
		slot->qs_end_of_stream = B_FALSE;
		memcpy(slot->qs_item, item, queue->zq_params.qp_item_size);
		if (slot->qs_completed) {
			advance_completion_index(queue);
		}
	} else {
		slot->qs_cost = 0;
		slot->qs_completed = B_TRUE;
		slot->qs_end_of_stream = B_TRUE;
		queue->zq_disallow_enqueue = B_TRUE;
	}
	queue->zq_ix.enqueue++;

#ifdef MONITOR_QUEUES
	/* Maintain queue usage data per monitor interval */
	int depth = queue->zq_ix.enqueue - queue->zq_ix.dequeue;
	queue->zq_stats.max_depth = MAX(queue->zq_stats.max_depth, depth);
	queue->zq_stats.min_depth = MIN(queue->zq_stats.min_depth, depth);
#endif

	pthread_mutex_unlock(&queue->zq_mutex);
	pthread_mutex_lock(&pool.tp_enqueue_mutex);
	pthread_cond_signal(&pool.tp_enqueued);
	pthread_mutex_unlock(&pool.tp_enqueue_mutex);
}

void
zstream_queue_fini(zstream_queue_t *queue) {
	zstream_enqueue(queue, NULL);
}

/*
 * Note that this function is not public. The only way to destroy a queue
 * through the public API is to call zstream_queue_fini(), wait for all
 * items to be processed, and then dequeue all items.
 */
static void
zstream_queue_destroy(zstream_queue_t *queue)
{
	pthread_mutex_lock(&pool.tp_pool_mutex);

	pthread_mutex_destroy(&queue->zq_mutex);
	pthread_cond_destroy(&queue->zq_cond.dequeued);

	if (pthread_cond_destroy(&queue->zq_cond.completed) != 0) {
		errx(1, "cannot destroy zstream_queue completed condition - "
		    "are you attempting to dequeue from multiple threads "
		    "simultaneously?");
	}

	free(queue->zq_slots);
	queue->zq_slots = NULL;
	free(queue);
	pool.tp_num_queues--;

	if (pool.tp_num_queues == 0) {
		thread_pool_spindown();  /* Unlocks pool mutex */
	} else {
		/* Gaps are not allowed in the tp_queues array */
		zstream_queue_t **qscan = &pool.tp_queues[0];
		int i = pool.tp_num_queues;
		while (*qscan != queue) { qscan++; i--; }
		memmove(qscan, qscan + 1, i * sizeof (*qscan));
		pthread_mutex_unlock(&pool.tp_pool_mutex);
	}
}

/*
 * Locking note: if more than one thread attempts to dequeue items
 * simultaneously, disaster is nearly certain. It will work fine until the
 * end of the stream, at which point it's a tossup between a race condition
 * with multiple attempts to destroy the whole queue vs. an attempt to
 * delete a condition that another thread is waiting on. The latter will be
 * trapped in zstream_queue_destroy(), but the former will likely just
 * crash. Hence the warning not to do multithreaded dequeues in
 * zstream_queue.h.
 */
boolean_t
zstream_dequeue(zstream_queue_t *queue, queue_item_t *item)
{
	pthread_mutex_lock(&queue->zq_mutex);
	while (queue->zq_ix.dequeue >= queue->zq_ix.complete) {
		await_condition(&queue->zq_cond.completed, &queue->zq_mutex);
	}
	queue_slot_t *slot = &Q_SLOT(queue, queue->zq_ix.dequeue);
	queue->zq_ix.dequeue++;
	if (slot->qs_end_of_stream) {
		pthread_mutex_unlock(&queue->zq_mutex);
		/* Potential race point */
		zstream_queue_destroy(queue);
		return (B_FALSE);
	} else {
		memcpy(item, slot->qs_item, queue->zq_params.qp_item_size);
		pthread_cond_signal(&queue->zq_cond.dequeued);
		pthread_mutex_unlock(&queue->zq_mutex);
		return (B_TRUE);
	}
}

#ifdef	MONITOR_QUEUES

#define	JIFFIES_PER_SEC 100
#define	SAMPLE_DURATION_US 1000000

/*
 * Monitor queue and CPU usage from a separate thread. This is all
 * Linux-specific, but it's needed only while tuning queue lengths and batch
 * sizes.
 */
static void *
cpu_and_queue_monitor(void *dummy)
{
	(void) dummy;
	uint64_t period = SAMPLE_DURATION_US;
	struct timespec clock = {};
	uint64_t start_us, end_us, delta_jif;
	uint64_t cpu_jif_prior = 0;
	uint64_t delta_cpu_jif;
	long unsigned int utime, stime;
	char buff[1024];
	boolean_t interrupt = B_FALSE;
	FILE *fp;

	pthread_register_self();

	/* Wait a few seconds for things to settle into steady state */
	usleep(3 * 1000 * 1000);

	while (B_TRUE) {
		usleep(period);
		fp = fopen("/proc/self/stat", "r");
		VERIFY3P(fp, !=, NULL);
		VERIFY3P(fgets(buff, sizeof (buff), fp), !=, NULL);
		fclose(fp);
		char *p = strrchr(buff, ')');
		VERIFY3P(p, !=, NULL);
		p += 2;  /* skip ") " and fields 3-13 */
		for (int i = 0; i < 11; i++) {
			p = strchr(p, ' ');
			VERIFY3P(p, !=, NULL);
			p++;
		}
		VERIFY3U(sscanf(p, "%lu %lu", &utime, &stime), ==, 2);
		pthread_mutex_lock(&pool.tp_pool_mutex);
		clock_gettime(CLOCK_MONOTONIC, &clock);
		end_us = clock.tv_sec * 1000000 + clock.tv_nsec / 1000;
		if (cpu_jif_prior) {
			delta_cpu_jif = utime + stime - cpu_jif_prior;
			delta_jif = (end_us - start_us) /
			    (JIFFIES_PER_SEC * 100);
			double cpu_pct = (double)delta_cpu_jif /
			    (pool.tp_num_threads * delta_jif);
			fprintf(stderr, "CPU: %.2f%%  ", 100 * cpu_pct);
			/* Stop to investigate low CPU usage */
			if (interrupt && cpu_pct < 0.85 && cpu_pct > 0.1) {
				kill(getpid(), SIGSTOP);
			}
		}

		/* Report queue depths */
		for (int i = 0; i < pool.tp_num_queues; i++) {
			zstream_queue_t *q = pool.tp_queues[i];
			fprintf(stderr, "Queue %d: %d-%d  ", i,
			    q->zq_stats.min_depth, q->zq_stats.max_depth);
			q->zq_stats.min_depth = 999999999;
			q->zq_stats.max_depth = 0;
		}

		pthread_mutex_unlock(&pool.tp_pool_mutex);
		fprintf(stderr, "\n");
		cpu_jif_prior = utime + stime;
		start_us = end_us;
	}
	return (NULL);
}

static void
start_monitor_thread(void)
{
	static boolean_t started = B_FALSE;
	pthread_t monitor;

	if (!started) {
		pthread_create(&monitor, NULL, cpu_and_queue_monitor, NULL);
		pthread_setname_np(monitor, "monitor-0");
		pthread_detach(monitor);
		started = B_TRUE;
	}
}

#endif	/* MONITOR_QUEUES */
