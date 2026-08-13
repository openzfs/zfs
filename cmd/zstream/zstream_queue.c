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

#include <assert.h>
#include <atomic.h>
#include <err.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/random.h>
#include <sys/stdtypes.h>
#include <sys/sysmacros.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "zstream_queue.h"
#include "zstream_util.h"

#define	ENQUEUE_DELAY_NSEC	(100 * 1000)		/* 100us */
#define	DISPATCH_BACKUP_NSEC	(1000 * 1000)		/* 1ms */

#define	PLENTY_OF_WORK		6	/* "Many" items to claim */
#define	NO_WORK			1.0E-6	/* No-work score threshold */
#define	DEQUEUE_SCORE_WEIGHT	0.3	/* Dequeue score relative weight */

#define	Q_MOD(queue, index)	((index) % (queue)->zq_params.qp_queue_length)
#define	Q_SLOT(queue, index)	((queue)->zq_slots[Q_MOD((queue), (index))])

#define	Q_FULL(queue)	((queue)->zq_ix.enqueue - (queue)->zq_ix.dequeue >= \
	    (queue)->zq_params.qp_queue_length)

/*
 * A zstream_queue is a ring buffer with four indexes: enqueue, claim,
 * complete, and dequeue, in that order. No index can move beyond its
 * preceding index. Every interval between indexes contains work items in a
 * particular state: enqueued, claimed for work, or completed. Items never
 * leave the ring buffer, so FIFO order is guaranteed on dequeueing.
 *
 * In concept, every index has a corresponding condition that threads can
 * wait on if they are interested in knowing when that index moves:
 * enqueued, claimed, completed, dequeued. However, the reality deviates
 * from this model in two ways:
 *
 * - There is no "claimed" condition, because no thread would wait on it.
 * Claiming and processing are one unified operation. Dequeuers await the
 * "completed" condition.
 *
 * - All queues share one thread pool, so idle threads are not bound to any
 * particular queue. Instead of having queue-specific "enqueued" conditions,
 * queues share a centralized dispatch system. On being awakened, worker
 * threads assign themselves to a queue through a scoring mechanism
 * described in the comments at score_queue().
 *
 * LOCKING
 *
 * There are three types of lock:
 *
 * - One global lock that gates changes to the thread pool and queue cohort.
 * This lock also acts as the mutex for the tp_wake_worker condition.
 *
 * - A second, low-contention global lock that protects the dispatch system
 *
 * - One lock for each queue
 *
 * Any operation that adds or removes queues or threads should hold the pool
 * lock. Any operation that moves a queue's indexes should hold the queue
 * lock. Any thread waiting for work waits on tp_wake_worker.
 *
 * Worker threads hold no locks while they are actually processing items.
 *
 * The global locking order is dispatch -> pool -> queue.
 *
 * DISPATCH
 *
 * Four events trigger dispatch loops:
 *
 * 1) A worker thread completing its batch. Threads always check to see if
 * there's more claimable work before going to sleep.
 *
 * 2) A worker thread discovering more work than it can handle on its own.
 * Before starting work on its own batch, the worker attempts to signal
 * another thread to wake up and assess the current state.
 *
 * 3) Enqueues. These go through the dispatch system and are batched.
 * Roughly ENQUEUE_DELAY_NSEC after an enqueue (on any queue), the dispatch
 * thread attempts to awaken a worker.
 *
 * 4) The expiration of a backup timer. The atomic value tp_unclaimed tracks
 * the total number of enqueued-but-unclaimed items across all queues. When
 * zero, it indicates that no worker dispatch is currently necessary. This
 * value is rigorously maintained and the increments and decrements are
 * sequentially consistent. However, the reads are relaxed, so a reader may
 * see a stale value. In the event that a critical worker wakeup is dropped,
 * the backup timer intervenes to keep dispatches running.
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
} zq_indexes_t;

typedef struct {
	pthread_cond_t	completed;
	pthread_cond_t	dequeued;
} zq_conditions_t;

typedef struct {
	int		min_depth;
	int		max_depth;
} zq_stats_t;

struct zstream_queue {
	int		zq_id;
	queue_slot_t	*zq_slots;
	pthread_mutex_t	zq_mutex;
	zq_indexes_t	zq_ix;
	zq_conditions_t	zq_cond;
	zq_params_t	zq_params;
	boolean_t	zq_disallow_enqueue;
#ifdef MONITOR_QUEUES
	zq_stats_t	zq_stats;
	uint64_t	zq_histogram[ZQ_MAX_BATCH+1];	/* Batch sizes */
#endif
};

typedef struct {
	pthread_mutex_t	tp_pool_mutex;
	pthread_cond_t	tp_wake_worker;		/* Awaited by workers */

	pthread_mutex_t	tp_dispatch_mutex;
	pthread_cond_t	tp_request_dispatch;	/* By dispatch thread */
	boolean_t	tp_dispatch_requested;

	zstream_queue_t	*tp_queues[ZQ_MAX_QUEUES];
	int		tp_num_queues;

	boolean_t	tp_threads_created;
	int		tp_num_threads;

	uint64_t	tp_unclaimed;		/* Atomic, all queues */
} thread_pool_t;

typedef union {
	long long	ll;
	long double	ld;
	void		*p;
	void		(*fp)(void);
} worst_case_alignment_t;

static void *queue_worker(void *);
static void *dispatch_worker(void *);

#ifdef MONITOR_QUEUES
static void *cpu_and_queue_monitor(void *);
static void print_batch_size_histogram(zstream_queue_t *);
#endif

static thread_pool_t	pool = {0};
static pthread_once_t	once_control = PTHREAD_ONCE_INIT;

/*
 * The dispatch timer needs sub-millisecond accuracy. POSIX timers on
 * FreeBSD don't implement that, but nanosleep() works fine.
 */
static void
sleep_nsec(uint64_t nsec)
{
	struct timespec ts = {
		.tv_sec = nsec / NANOSEC,
		.tv_nsec = nsec % NANOSEC
	};
	while (nanosleep(&ts, &ts) != 0) {
		if (errno != EINTR)
			err(1, "nanosleep failed");
	}
}

static void
thread_pool_init(void)
{
	pthread_mutex_init(&pool.tp_pool_mutex, NULL);
	pthread_cond_init(&pool.tp_wake_worker, NULL);

	pthread_mutex_init(&pool.tp_dispatch_mutex, NULL);
	pthread_cond_init(&pool.tp_request_dispatch, NULL);

	safe_create_thread(dispatch_worker, NULL, "dispatch", B_TRUE);
}

/*
 * If this function is to be called at all, it must be called before any
 * queues have been created.
 */
void
zstream_queue_set_num_threads(int n)
{
	pthread_once(&once_control, thread_pool_init);
	pthread_mutex_lock(&pool.tp_pool_mutex);
	if (pool.tp_threads_created) {
		errx(1, "thread pool size must be set before creating queues");
	} else if (n < 1) {
		errx(1, "number of threads must be at least 1");
	} else if (n < ZQ_MIN_THREADS) {
		warnx("using only %d threads may limit performance, setting "
		    "anyway...", n);
	} else if (n > 256) {
		warnx("num_threads = %d seems suspiciously high, setting "
		    "anyway...", n);
	}
	pool.tp_num_threads = n;
	pthread_mutex_unlock(&pool.tp_pool_mutex);
}

/*
 * Locking: the caller must hold the pool mutex.
 *
 * If tp_num_threads is nonzero, it sets the number of threads to spawn.
 * Otherwise, one thread is spawned per core, with a minimum of 6 threads.
 *
 * sched_getaffinity() is a better estimate of available threads than
 * sysconf because sysconf doesn't account for limits that might be set on,
 * e.g., a container.
 */
static void
thread_pool_spinup(void)
{
	if (pool.tp_num_threads == 0) {
#ifdef	CPU_COUNT
		cpu_set_t cpu_set;
		if (sched_getaffinity(0, sizeof (cpu_set_t), &cpu_set) != 0) {
			warn("sched_getaffinity failed, using sysconf");
			pool.tp_num_threads = sysconf(_SC_NPROCESSORS_ONLN);
		} else {
			pool.tp_num_threads = CPU_COUNT(&cpu_set);
		}
#else
		pool.tp_num_threads = sysconf(_SC_NPROCESSORS_ONLN);
#endif
		pool.tp_num_threads = MAX(pool.tp_num_threads, ZQ_MIN_THREADS);
	}
	for (int i = 0; i < pool.tp_num_threads; i++) {
		char name[32];
		snprintf(name, sizeof (name), "queue-%d", i);
		safe_create_thread(queue_worker, NULL, name, B_TRUE);
	}
#ifdef MONITOR_QUEUES
	safe_create_thread(cpu_and_queue_monitor, NULL, "monitor", B_TRUE);
#endif
}

zstream_queue_t *
zstream_queue_create(zq_params_t *params)
{
	static int next_queue_id = 0;

	VERIFY3P(params->qp_process, !=, NULL);
	VERIFY3P(params->qp_cost, !=, NULL);
	VERIFY3U(params->qp_item_size, >, 0);
	VERIFY3U(params->qp_queue_length, >, 0);
	VERIFY3U(params->qp_queue_length, <, 1 << 18);

	pthread_once(&once_control, thread_pool_init);
	pthread_mutex_lock(&pool.tp_pool_mutex);
	VERIFY3S(pool.tp_num_queues, <, ZQ_MAX_QUEUES);

	if (!pool.tp_threads_created) {
		thread_pool_spinup();
		pool.tp_threads_created = B_TRUE;
	}

	zstream_queue_t *queue = safe_malloc(sizeof (zstream_queue_t));
	*queue = (zstream_queue_t) {
		.zq_id = next_queue_id++,
		.zq_params = *params,
		.zq_slots = safe_malloc(params->qp_queue_length *
		    (sizeof (queue_slot_t))),
#ifdef MONITOR_QUEUES
		.zq_stats.min_depth = INT_MAX
#endif
	};
	pool.tp_queues[pool.tp_num_queues] = queue;

	size_t qpis_rounded = P2ROUNDUP(params->qp_item_size,
	    _Alignof(worst_case_alignment_t));
	uint8_t *items = safe_malloc(params->qp_queue_length * qpis_rounded);
	for (size_t i = 0; i < params->qp_queue_length; i++) {
		queue->zq_slots[i].qs_item =
		    (queue_item_t *)(items + i * qpis_rounded);
	}

	pthread_mutex_init(&queue->zq_mutex, NULL);
	pthread_cond_init(&queue->zq_cond.completed, NULL);
	pthread_cond_init(&queue->zq_cond.dequeued, NULL);

	pool.tp_num_queues++;
	pthread_mutex_unlock(&pool.tp_pool_mutex);
	return (queue);
}

/*
 * Try to advance the "claim" and "complete" indexes as far as possible by
 * examining the qs_completed flag on each item. This can't be done directly
 * by the threads that complete work, for a couple of reasons:
 *
 * - Items can be completed in any order. Just because you (a thread) have
 * finished your batch doesn't mean that all prior batches have completed.
 * If there are uncompleted items ahead of you in the ring buffer, you can't
 * advance the completion index past them on your way out.
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
 * necessary. However, the claimer already holds the queue mutex, and it's
 * in our interest to make completed items available for dequeueing as
 * expeditiously as possible.
 *
 * Sweeping of the "claim" index is also an optimization. It is not
 * necessary for correctness. However, if we don't do it here, it can only
 * be done by threads as they claim jobs to work on. In some cases, not
 * advancing the "claim" index here can result in an empty batch and a
 * wasted claim cycle.
 *
 * Locking: the caller must hold the queue mutex.
 */
static inline void
advance_indexes(zstream_queue_t *queue)
{
	boolean_t any_completed = B_FALSE;
	uint64_t claimed = 0;

	while (queue->zq_ix.claim < queue->zq_ix.enqueue &&
	    Q_SLOT(queue, queue->zq_ix.claim).qs_completed) {
		queue->zq_ix.claim++;
		claimed++;
	}
	if (claimed > 0) {
		/*
		 * tp_unclaimed is decremented both here and in
		 * claim_batch(). The conditions are mutually exclusive, so
		 * double counting will not occur.
		 */
		atomic_sub_64(&pool.tp_unclaimed, claimed);
	}
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
 * Score a queue according to its need for workers. Higher is better. The
 * scoring tries to assign threads to queues that are running out of space
 * for new enqueuements or that have little completed work available to
 * dequeue. The broader goal is to try to avoid pipeline stalls.
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
 * Locking: the caller must hold the thread pool mutex and the queue mutex.
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
	double claim_factor = MIN(claimable, (uint64_t)PLENTY_OF_WORK) /
	    (double)PLENTY_OF_WORK;
	double need = open_score + dq_score * DEQUEUE_SCORE_WEIGHT;
	return (need * claim_factor);
}

/*
 * Return a random index from an array of doubles, with the likelihood of
 * index i being selected equal to weights[i] / sum(weights). Returns index
 * 0 if no weight is greater than 0.
 */
static inline int
select_stochastic(double weights[], int num_values)
{
	const double denominator = (double)UINT64_MAX;
	uint64_t numerator;
	double total = 0.0;

	for (int i = 0; i < num_values; i++) {
		total += weights[i];
	}
	random_get_pseudo_bytes((uint8_t *)&numerator, sizeof (numerator));
	double select_val = total * numerator / denominator;
	for (int i = 0; i < num_values; i++) {
		if (select_val < weights[i])
			return (i);
		select_val -= weights[i];
	}
	/* Fallback in case of FP rounding not producing a winner */
	for (int i = num_values - 1; i >= 0; i--) {
		if (weights[i] != 0.0)
			return (i);
	}
	return (0);
}

/*
 * Claim up to ZQ_MAX_BATCH work items from the given queue, trying to
 * accumulate at least qp_batch_budget worth of work data (== "cost"). All
 * items in a batch will be drawn from the same queue.
 *
 * Does not block waiting to fill the budget; returns whatever is available.
 *
 * Locking: this function must be called with both the queue mutex and the
 * thread pool mutex held. zstream_queue_destroy() can't hold a queue's
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
	uint64_t passed = 0;
	boolean_t more_to_claim, more_slots, more_budget;
	boolean_t first_and_only, ok_to_claim;

	while (B_TRUE) {
		more_to_claim = queue->zq_ix.claim < queue->zq_ix.enqueue;
		more_slots = count < ZQ_MAX_BATCH;
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
		passed++;
	}

	/*
	 * Every slot the claim index moved over leaves the unclaimed pool,
	 * whether we took it for the batch or skipped it as already complete.
	 */
	if (passed > 0) {
		atomic_sub_64(&pool.tp_unclaimed, passed);
	}
	advance_indexes(queue);
#ifdef MONITOR_QUEUES
	queue->zq_histogram[count]++;
#endif
	return (count);
}

/*
 * Threads are assigned to a queue on each loop so they can be shifted
 * dynamically to follow available work. Idle threads will typically be
 * waiting on the tp_wake_worker condition within this function.
 *
 * Locking: we hold the pool mutex throughout, both to keep a queue from
 * being destroyed out from under us while we score it or claim from it, and
 * because it is the mutex for tp_wake_worker. Individual queues are locked
 * for only as long as it takes to score or claim from them.
 */
static int
assign_queue_and_get_work(zstream_queue_t **queue, queue_slot_t **batch)
{
	pthread_mutex_lock(&pool.tp_pool_mutex);

	while (B_TRUE) {
		int num_queues = pool.tp_num_queues;
		double weights[ZQ_MAX_QUEUES];
		int queues_with_work = 0;

		for (int i = 0; i < num_queues; i++) {
			zstream_queue_t *to_score = pool.tp_queues[i];
			pthread_mutex_lock(&to_score->zq_mutex);
			weights[i] = score_queue(to_score);
			pthread_mutex_unlock(&to_score->zq_mutex);
			if (weights[i] > NO_WORK)
				queues_with_work++;
		}
		if (!queues_with_work) {
			pthread_cond_wait(&pool.tp_wake_worker,
			    &pool.tp_pool_mutex);
		} else {
			int q = select_stochastic(weights, num_queues);
			*queue = pool.tp_queues[q];
			pthread_mutex_lock(&(*queue)->zq_mutex);
			int count = claim_batch(*queue, batch);
			pthread_mutex_unlock(&(*queue)->zq_mutex);
			/*
			 * Try to wake up another worker thread if there
			 * still seems to be work available (on any queue).
			 */
			if (atomic_load_64(&pool.tp_unclaimed) > 0) {
				pthread_cond_signal(&pool.tp_wake_worker);
			}
			pthread_mutex_unlock(&pool.tp_pool_mutex);
			return (count);
		}
	}
}

/*
 * Batches are processed without holding any locks. The existence of the
 * items we're working on guarantees that the queue can't be destroyed out
 * from under us.
 *
 * However, we can't mark items completed without holding the queue lock
 * because that creates a potential race condition with advance_indexes()
 * being called on another thread.
 */
static void *
queue_worker(void *dummy)
{
	(void) dummy;
	zstream_queue_t *queue;
	queue_slot_t *batch[ZQ_MAX_BATCH];
	int count;

	while (B_TRUE) {
		count = assign_queue_and_get_work(&queue, batch);
		if (count) {
			zq_process_item_f *process =
			    queue->zq_params.qp_process;
			void *context = queue->zq_params.qp_context;
			for (int i = 0; i < count; i++) {
				process(batch[i]->qs_item, context);
			}
			pthread_mutex_lock(&queue->zq_mutex);
			for (int i = 0; i < count; i++) {
				batch[i]->qs_completed = B_TRUE;
			}
			advance_indexes(queue);
			pthread_mutex_unlock(&queue->zq_mutex);
		}
	}
	return (NULL);
}

/*
 * Locking: must be called with the dispatch mutex held
 *
 * Skips the wakeup if tp_unclaimed == 0.
 */
static inline void
maybe_wake_worker(void)
{
	pool.tp_dispatch_requested = B_FALSE;
	if (atomic_load_64(&pool.tp_unclaimed) > 0) {
		pthread_mutex_lock(&pool.tp_pool_mutex);
		pthread_cond_signal(&pool.tp_wake_worker);
		pthread_mutex_unlock(&pool.tp_pool_mutex);
	}
}

static inline struct timespec
timeout_timespec(void)
{
	struct timespec expire;
	struct timeval tv;

	if (gettimeofday(&tv, NULL) != 0)
		err(1, "couldn't gettimeofday()");
	uint64_t nsec = tv.tv_usec * 1000 + DISPATCH_BACKUP_NSEC;
	expire.tv_sec = tv.tv_sec + nsec / NANOSEC;
	expire.tv_nsec = nsec % NANOSEC;
	return (expire);
}

/*
 * The enqueue notification pacing thread, which converts a notification
 * from an enqueuer into a possible worker wakeup roughly ENQUEUE_DELAY_NSEC
 * later. The delay facilitates larger batch sizes and keeps enqueuers on a
 * less-contested mutex.
 *
 * The condwait timeout is necessary because the tp_unclaimed count is not
 * the final word on whether there is actually any work to claim. It is
 * calculated rigorously. However, it's a bare atomic and therefore
 * potentially out of date at any given moment. A backup strategy is
 * necessary to restart processing in the event of a race.
 */
static void *
dispatch_worker(void *nope)
{
	(void) nope;
	pthread_mutex_lock(&pool.tp_dispatch_mutex);
	while (B_TRUE) {
		while (!pool.tp_dispatch_requested) {
			int rc;
			struct timespec expire = timeout_timespec();
			rc = pthread_cond_timedwait(&pool.tp_request_dispatch,
			    &pool.tp_dispatch_mutex, &expire);
			if (rc == ETIMEDOUT) {
				maybe_wake_worker();
			} else if (rc != 0) {
				errx(1, "pthread_cond_timedwait() failed: %s",
				    strerror(rc));
			}
		}
		pthread_mutex_unlock(&pool.tp_dispatch_mutex);
		sleep_nsec(ENQUEUE_DELAY_NSEC);
		pthread_mutex_lock(&pool.tp_dispatch_mutex);
		maybe_wake_worker();
	}
	return (NULL);
}

/*
 * Implements both _enqueue and _fini. item == NULL for fini.
 */
void
zstream_enqueue(zstream_queue_t *queue, queue_item_t *item)
{
	VERIFY3P(queue, !=, NULL);
	pthread_mutex_lock(&queue->zq_mutex);

	VERIFY3B(queue->zq_disallow_enqueue, ==, B_FALSE);
	while (Q_FULL(queue)) {
		pthread_cond_wait(&queue->zq_cond.dequeued, &queue->zq_mutex);
	}
	VERIFY3B(queue->zq_disallow_enqueue, ==, B_FALSE);
	queue_slot_t *slot = &Q_SLOT(queue, queue->zq_ix.enqueue);
	if (item) {
		slot->qs_cost =
		    queue->zq_params.qp_cost(item, queue->zq_params.qp_context);
		slot->qs_completed = slot->qs_cost == 0;
		slot->qs_end_of_stream = B_FALSE;
		memcpy(slot->qs_item, item, queue->zq_params.qp_item_size);
	} else {
		slot->qs_cost = 0;
		slot->qs_completed = B_TRUE;
		slot->qs_end_of_stream = B_TRUE;
		queue->zq_disallow_enqueue = B_TRUE;
	}
	queue->zq_ix.enqueue++;
	atomic_inc_64(&pool.tp_unclaimed);
	if (slot->qs_cost == 0)
		advance_indexes(queue);

#ifdef MONITOR_QUEUES
	/* Maintain queue usage data per monitor interval */
	uint64_t depth = queue->zq_ix.enqueue - queue->zq_ix.dequeue;
	queue->zq_stats.max_depth = MAX(queue->zq_stats.max_depth, depth);
	queue->zq_stats.min_depth = MIN(queue->zq_stats.min_depth, depth);
#endif

	pthread_mutex_unlock(&queue->zq_mutex);

	pthread_mutex_lock(&pool.tp_dispatch_mutex);
	pool.tp_dispatch_requested = B_TRUE;
	pthread_cond_signal(&pool.tp_request_dispatch);
	pthread_mutex_unlock(&pool.tp_dispatch_mutex);
}

void
zstream_queue_fini(zstream_queue_t *queue)
{
	zstream_enqueue(queue, NULL);
}

/*
 * This function is not public. The only way to destroy a queue through the
 * public API is to call zstream_queue_fini(), wait for all items to be
 * processed, and then dequeue all items. As a consequence, threads are
 * entitled to assume that any queue with unprocessed work will not be
 * removed without locking the pool mutex.
 *
 * Locking: the caller must NOT hold the queue lock. The pool mutex is held
 * while destroying the queue.
 */
static void
zstream_queue_destroy(zstream_queue_t *queue)
{
	pthread_mutex_lock(&pool.tp_pool_mutex);

#ifdef MONITOR_QUEUES
	print_batch_size_histogram(queue);
#endif

	VERIFY0(pthread_mutex_destroy(&queue->zq_mutex));
	VERIFY0(pthread_cond_destroy(&queue->zq_cond.dequeued));
	if (pthread_cond_destroy(&queue->zq_cond.completed) != 0) {
		errx(1, "cannot destroy zstream_queue completed condition - "
		    "are you attempting to dequeue from multiple threads "
		    "simultaneously?");
	}
	pool.tp_num_queues--;
	if (pool.tp_num_queues > 0) {
		/* Gaps are not allowed in the tp_queues array */
		zstream_queue_t **qscan = &pool.tp_queues[0];
		int i = pool.tp_num_queues;
		while (*qscan != queue) { qscan++; i--; }
		if (i > 0)
			memmove(qscan, qscan + 1, i * sizeof (*qscan));
	}
	/*
	 * Items are allocated as a single block. The address of the first
	 * item field is in fact the start of the block.
	 */
	free(queue->zq_slots[0].qs_item);
	free(queue->zq_slots);
	queue->zq_slots = NULL;
	free(queue);

	pthread_mutex_unlock(&pool.tp_pool_mutex);
}

/*
 * Locking: if more than one thread attempts to dequeue items
 * simultaneously, disaster is likely. It will work fine until the end of
 * the stream, at which point it becomes a tossup between a race condition
 * with multiple attempts to destroy the whole queue vs. an attempt to
 * delete a condition that another thread is waiting on. Hence the warning
 * not to do multithreaded dequeues in zstream_queue.h.
 *
 * Returns B_TRUE if real data is returned, B_FALSE if the end of the queue
 * has been reached.
 */
boolean_t
zstream_dequeue(zstream_queue_t *queue, queue_item_t *item)
{
	pthread_mutex_lock(&queue->zq_mutex);
	while (queue->zq_ix.dequeue >= queue->zq_ix.complete) {
		pthread_cond_wait(&queue->zq_cond.completed, &queue->zq_mutex);
	}
	queue_slot_t *slot = &Q_SLOT(queue, queue->zq_ix.dequeue);
	queue->zq_ix.dequeue++;
	if (slot->qs_end_of_stream) {
		pthread_mutex_unlock(&queue->zq_mutex);
		/* Potential multi-dequeuer race point */
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

#define	USEC_PER_JIFFY		10000
#define	SAMPLE_DURATION_USEC	1000000
#define	CPU_FIELD_WIDTH		14

/*
 * Called only during zstream_queue_destroy(), under the pool mutex
 */
static void
print_batch_size_histogram(zstream_queue_t *queue)
{
	int last_nonzero = 0;
	static int lines_printed = 0;

	if (lines_printed++ == 0)
		fprintf(stderr, "\nBatch size histograms:\n");
	for (last_nonzero = ZQ_MAX_BATCH; last_nonzero >= 0; last_nonzero--) {
		if (queue->zq_histogram[last_nonzero] > 0)
			break;
	}
	fprintf(stderr, "Queue %d: ", queue->zq_id);
	const char *sep = "";
	for (int i = 0; i <= last_nonzero; i++) {
		fprintf(stderr, "%s%llu", sep,
		    (u_longlong_t)queue->zq_histogram[i]);
		sep = ", ";
	}
	fprintf(stderr, "\n");
	fflush(stderr);
}

/*
 * Monitor queue and CPU usage from a separate thread. This is all
 * Linux-specific, but it's needed only while tuning queue lengths and
 * batch sizes. Prints the minimum and maximum queue depth observed
 * during each period.
 *
 * Example output:
 *
 *     CPU: 99.85%   Queue 0:  745-1024   Queue 1:  183-256
 */
static void *
cpu_and_queue_monitor(void *dummy)
{
	(void) dummy;
	uint64_t period = SAMPLE_DURATION_USEC;
	struct timespec clock = {0};
	uint64_t start_us, end_us;
	uint64_t cpu_jif_prior = 0;
	uint64_t delta_jif, delta_cpu_jif;
	long unsigned int utime, stime;
	char buff[1024];
	FILE *fp;

	fprintf(stderr, "Queue depths:\n");

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

		if (cpu_jif_prior > 0) {
			delta_cpu_jif = utime + stime - cpu_jif_prior;
			delta_jif = (end_us - start_us) / USEC_PER_JIFFY;
			double cpu_pct = (double)delta_cpu_jif /
			    (pool.tp_num_threads * delta_jif);
			cpu_pct = MIN(cpu_pct, 0.9999); /* Don't print 100% */
			fprintf(stderr, "CPU: %5.2f%%   ", 100 * cpu_pct);
		} else {
			/* No CPU data available for the first interval */
			fprintf(stderr, "%*s", CPU_FIELD_WIDTH, "");
		}

		for (int i = 0; i < pool.tp_num_queues; i++) {
			zstream_queue_t *q = pool.tp_queues[i];
			pthread_mutex_lock(&q->zq_mutex);
			int min = q->zq_stats.min_depth;
			int max = q->zq_stats.max_depth;
			if (min > max)
				min = max = 0;
			fprintf(stderr, "Queue %d: %4d-%-4d   ",
			    q->zq_id, min, max);
			q->zq_stats.min_depth = INT_MAX;
			q->zq_stats.max_depth = 0;
			pthread_mutex_unlock(&q->zq_mutex);
		}

		pthread_mutex_unlock(&pool.tp_pool_mutex);

		fprintf(stderr, "\n");
		fflush(stderr);

		cpu_jif_prior = utime + stime;
		start_us = end_us;
	}
	return (NULL);
}

#endif	/* MONITOR_QUEUES */
