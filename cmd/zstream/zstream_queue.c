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
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/random.h>
#include <sys/stdtypes.h>
#include <sys/sysmacros.h>
#include <time.h>
#include <unistd.h>

#include "zstream.h"
#include "zstream_queue.h"
#include "zstream_util.h"

#define	MIN_THREADS		6
#define	MAX_QUEUES		16	/* Maximum simultaneously active */
#define	ENQUEUE_DELAY_NSEC	1E5	/* Signal delay for enqueues, 100us */

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
 * - A second global lock associated with the shared "enqueued" condition
 * - A third global lock that protects the enqueue signal delay
 * - One lock for each queue
 *
 * Any operation that adds or removes queues or threads should hold the
 * pool lock. Any operation that moves a queue's indices should hold the
 * queue lock. Any thread waiting for work should wait on the "enqueued"
 * condition.
 *
 * Worker threads hold no locks while they are actually processing items.
 *
 * Several operations require multiple locks. In these cases, a standardized
 * locking order is used to avoid deadlocks:
 *
 *   enqueue -> pool -> queue -> delay
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
	int		zq_id;
	queue_slot_t	*zq_slots;
	pthread_mutex_t	zq_mutex;
	zq_indices_t	zq_ix;
	zq_conditions_t	zq_cond;
	zq_params_t	zq_params;
	zq_stats_t	zq_stats;
	boolean_t	zq_disallow_enqueue;
	uint64_t	zq_histogram[MAX_BATCH+1];	/* Batch sizes */
};

typedef struct {
	pthread_mutex_t		enqueue_mutex;
	pthread_mutex_t		delay_mutex;
	pthread_cond_t		enqueued;
	timer_t			timer;
	boolean_t		pending;
	struct itimerspec	delay_nsec;
} enqueue_control_t;

typedef struct {
	pthread_mutex_t		tp_pool_mutex;
	enqueue_control_t	tp_enqueue;
	zstream_queue_t		*tp_queues[MAX_QUEUES];
	int			tp_num_queues;
	boolean_t		tp_threads_created;
	int			tp_num_threads;
} thread_pool_t;

typedef union {
	long		long ll;
	long		double ld;
	void		*p;
	void		(*fp)(void);
} worst_case_alignment_t;

static void *queue_worker(void *);
static void *enqueue_signal_worker(void *);

#ifdef MONITOR_QUEUES
static void *cpu_and_queue_monitor(void *);
static void print_batch_size_histogram(zstream_queue_t *);
#endif

static thread_pool_t	pool = {0};
static pthread_once_t	once_control = PTHREAD_ONCE_INIT;

static void
thread_pool_init(void)
{
	enqueue_control_t *ctl = &pool.tp_enqueue;

	pthread_mutex_init(&pool.tp_pool_mutex, NULL);

	pthread_mutex_init(&ctl->enqueue_mutex, NULL);
	pthread_mutex_init(&ctl->delay_mutex, NULL);
	pthread_cond_init(&ctl->enqueued, NULL);

	safe_create_thread(enqueue_signal_worker, NULL, "enqueue", B_TRUE);

	struct sigevent sev = {
		.sigev_notify = SIGEV_SIGNAL,
		.sigev_signo = ENQUEUE_SIGNAL
	};
	ctl->delay_nsec.it_value.tv_nsec = ENQUEUE_DELAY_NSEC;
	if (timer_create(CLOCK_MONOTONIC, &sev, &ctl->timer) != 0)
		err(1, "could not create enqueue signal timer");
}

/*
 * If this function is to be called at all, it must be called before any
 * queues have been created.
 */
void
zstream_queue_set_num_threads(uint_t n)
{
	pthread_once(&once_control, thread_pool_init);
	pthread_mutex_lock(&pool.tp_pool_mutex);
	if (pool.tp_threads_created) {
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
	pool.tp_num_threads = n;
	pthread_mutex_unlock(&pool.tp_pool_mutex);
}

/*
 * Locking: the caller must hold the pool mutex.
 *
 * If tp_num_threads is nonzero, it sets the number of threads to spawn.
 * Otherwise, one thread is spawned per core.
 *
 * sched_affinity() is a better estimate of available threads than sysconf
 * because sysconf doesn't account for limits that might be set on, e.g., a
 * container.
 */
static void
thread_pool_spinup(void)
{
	if (pool.tp_num_threads == 0) {
#ifdef	CPU_COUNT
		cpu_set_t cpu_set;
		sched_getaffinity(0, sizeof (cpu_set_t), &cpu_set);
		pool.tp_num_threads = CPU_COUNT(&cpu_set);
#else
		pool.tp_num_threads = sysconf(_SC_NPROCESSORS_ONLN);
#endif
		pool.tp_num_threads = MAX(pool.tp_num_threads, MIN_THREADS);
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

	pthread_once(&once_control, thread_pool_init);
	pthread_mutex_lock(&pool.tp_pool_mutex);
	VERIFY3S(pool.tp_num_queues, <, MAX_QUEUES);

	if (!pool.tp_threads_created) {
		thread_pool_spinup();
		pool.tp_threads_created = B_TRUE;
	}
	zstream_queue_t *queue = safe_malloc(sizeof (zstream_queue_t));
	pool.tp_queues[pool.tp_num_queues] = queue;

	*queue = (zstream_queue_t) {
		.zq_id = next_queue_id++,
		.zq_params = *params,
		.zq_slots = safe_malloc(params->qp_queue_length *
		    (sizeof (queue_slot_t)))
	};

	size_t qpis_rounded = P2ROUNDUP(params->qp_item_size,
	    _Alignof(worst_case_alignment_t));
	uint8_t *items = safe_malloc(params->qp_queue_length * qpis_rounded);
	for (int i = 0; i < params->qp_queue_length; i++) {
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
 * necessary. However, the claimer already holds the queue mutex, and
 * it's in our interest to make completed items available for dequeueing
 * as expeditiously as possible.
 *
 * It's also expedient to sweep the "claim" index if we can. This is not
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
	while (queue->zq_ix.claim < queue->zq_ix.enqueue &&
	    Q_SLOT(queue, queue->zq_ix.claim).qs_completed) {
		queue->zq_ix.claim++;
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
 * Locking: the caller must hold the enqueue mutex, the thread pool mutex,
 * and the queue mutex. Several corollaries:
 *
 * 1) Only one thread may score queues at a time.
 *
 * 2) Worker threads can still complete work during the process of scoring
 * all queues, so there will likely be time skew among scores.
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

/*
 * Claim up to MAX_BATCH work items from the given queue, trying to
 * accumulate at least queue->qp_batch_budget worth of work data (==
 * "cost"). All items in a batch will be drawn from the same queue.
 *
 * Does not block waiting to fill the budget; returns whatever is available.
 *
 * Locking: this function must be called with both the queue mutex and
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

	advance_indexes(queue);
	return (count);
}

/*
 * Threads are assigned to a queue on each loop so they can be shifted
 * dynamically to follow available work. Idle threads will typically be
 * awaiting the "enqueued" condition within this function.
 *
 * Locking: this function has complex locking behavior. At first we must
 * hold both the enqueue mutex (to be sure new work doesn't get sneaked in
 * after a queue is scored, which might cause it to be overlooked entirely)
 * and the thread pool mutex (to guarantee that no queue can be destroyed
 * out from under us). We also lock individual queues while scoring them.
 *
 * After queue selection, we retain the enqueue mutex while claiming a batch
 * so that we can safely signal the "enqueued" condition if there appears to
 * be enough work for more than one thread dispatch. We need to obtain the
 * mutex of the selected queue without releasing the pool mutex because
 * there is still the potential for a claim-vs-destroy race.
 *
 * This sequence dictates the lock acquisition ordering for all of
 * zstream_queue:
 *
 *     enqueue -> pool -> queue -> delay
 *
 * If everyone follows that order, deadlocks should not occur.
 */
static int
assign_queue_and_get_work(zstream_queue_t **queue, queue_slot_t **batch)
{
	pthread_mutex_lock(&pool.tp_enqueue.enqueue_mutex);
	pthread_mutex_lock(&pool.tp_pool_mutex);

	while (B_TRUE) {
		int num_queues = pool.tp_num_queues;
		double weights[MAX_QUEUES];
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
			pthread_mutex_unlock(&pool.tp_pool_mutex);
			pthread_cond_wait(&pool.tp_enqueue.enqueued,
			    &pool.tp_enqueue.enqueue_mutex);
			pthread_mutex_lock(&pool.tp_pool_mutex);
		} else {
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
			pthread_mutex_unlock(&(*queue)->zq_mutex);
			if (more_here || queues_with_work > 1) {
				pthread_cond_signal(&pool.tp_enqueue.enqueued);
			}
			pthread_mutex_unlock(&pool.tp_pool_mutex);
			pthread_mutex_unlock(&pool.tp_enqueue.enqueue_mutex);
			return (count);
		}
	}
}

static void *
queue_worker(void *dummy)
{
	(void) dummy;
	zstream_queue_t *queue;
	queue_slot_t *batch[MAX_BATCH];
	int count;

	while (B_TRUE) {
		count = assign_queue_and_get_work(&queue, batch);
		queue->zq_histogram[count]++;
		if (count) {
			zq_process_item_f *process =
			    queue->zq_params.qp_process;
			void *context = queue->zq_params.qp_context;
			/*
			 * Locking note: we complete the whole batch without
			 * holding any locks. However, we can't mark items
			 * as completed without holding the queue lock
			 * because that creates a race condition with
			 * advance_indexes().
			 */
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
 * Body of the "enqueued" signal delay thread. This system does not delay
 * enqueueing itself, just the delivery of the "enqueued" signal.
 *
 * The ENQUEUE_SIGNAL signal is blocked as soon as zstream starts up.
 * Threads created later inherit this setting, so no thread will take the
 * signal. Once the signal transfers to the pending state, it's then
 * detectable by sigwait().
 */
static void *
enqueue_signal_worker(void *nope)
{
	(void) nope;

	sigset_t set;
	int which;
	enqueue_control_t *ctl = &pool.tp_enqueue;

	sigemptyset(&set);
	sigaddset(&set, ENQUEUE_SIGNAL);

	while (B_TRUE) {
		if (sigwait(&set, &which) != 0)
			err(1, "error sigwaiting for ENQUEUE_SIGNAL");
		else if (which != ENQUEUE_SIGNAL)
			errx(1, "received signal was not ENQUEUE_SIGNAL");
		pthread_mutex_lock(&ctl->enqueue_mutex);
		pthread_mutex_lock(&ctl->delay_mutex);
		ctl->pending = B_FALSE;
		pthread_cond_signal(&ctl->enqueued);
		pthread_mutex_unlock(&ctl->delay_mutex);
		pthread_mutex_unlock(&ctl->enqueue_mutex);
	}
}

/*
 * Schedule the "enqueued" signal to be sent in ENQUEUE_DELAY_NSEC ns.
 * Deferring the signal allows a separate thread to handle it, so the
 * enqueuer can return immediately.
 */
static inline void
signal_enqueue(void)
{
	enqueue_control_t *ctl = &pool.tp_enqueue;
	pthread_mutex_lock(&ctl->delay_mutex);
	if (!ctl->pending) {
		ctl->pending = B_TRUE;
		if (timer_settime(ctl->timer, 0, &ctl->delay_nsec, NULL))
			err(1, "could not set timer value");
	}
	pthread_mutex_unlock(&pool.tp_enqueue.delay_mutex);
}

/*
 * Implements both _enqueue and _fini. item == NULL for fini.
 */
void
zstream_enqueue(zstream_queue_t *queue, queue_item_t *item)
{
	VERIFY(queue != NULL);
	pthread_mutex_lock(&queue->zq_mutex);
	VERIFY3B(queue->zq_disallow_enqueue, ==, B_FALSE);

	while (Q_FULL(queue)) {
		pthread_cond_wait(&queue->zq_cond.dequeued, &queue->zq_mutex);
	}

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
	if (slot->qs_completed) {
		advance_indexes(queue);
	}

	/* Maintain queue usage data per monitor interval */
	int depth = queue->zq_ix.enqueue - queue->zq_ix.dequeue;
	queue->zq_stats.max_depth = MAX(queue->zq_stats.max_depth, depth);
	queue->zq_stats.min_depth = MIN(queue->zq_stats.min_depth, depth);

	pthread_mutex_unlock(&queue->zq_mutex);
	signal_enqueue();
}

void
zstream_queue_fini(zstream_queue_t *queue) {
	zstream_enqueue(queue, NULL);
}

/*
 * This function is not public. The only way to destroy a queue through the
 * public API is to call zstream_queue_fini(), wait for all items to be
 * processed, and then dequeue all items.
 *
 * Locking: the caller must NOT hold the queue lock. The pool mutex is
 * held while destroying the queue.
 */
static void
zstream_queue_destroy(zstream_queue_t *queue)
{
#ifdef MONITOR_QUEUES
	print_batch_size_histogram(queue);
#endif
	pthread_mutex_lock(&pool.tp_pool_mutex);

	pthread_mutex_destroy(&queue->zq_mutex);
	pthread_cond_destroy(&queue->zq_cond.dequeued);

	if (pthread_cond_destroy(&queue->zq_cond.completed) != 0) {
		errx(1, "cannot destroy zstream_queue completed condition - "
		    "are you attempting to dequeue from multiple threads "
		    "simultaneously?");
	}

	free(queue->zq_slots[0].qs_item);
	free(queue->zq_slots);
	queue->zq_slots = NULL;
	free(queue);
	pool.tp_num_queues--;

	if (pool.tp_num_queues > 0) {
		/* Gaps are not allowed in the tp_queues array */
		zstream_queue_t **qscan = &pool.tp_queues[0];
		int i = pool.tp_num_queues;
		while (*qscan != queue) { qscan++; i--; }
		memmove(qscan, qscan + 1, i * sizeof (*qscan));
	}
	pthread_mutex_unlock(&pool.tp_pool_mutex);
}

/*
 * Locking: if more than one thread attempts to dequeue items
 * simultaneously, disaster is likely. It will work fine until the end of
 * the stream, at which point it's a tossup between a race condition with
 * multiple attempts to destroy the whole queue vs. an attempt to delete a
 * condition that another thread is waiting on. The latter will be trapped
 * in zstream_queue_destroy(), but the former will likely just crash. Hence
 * the warning not to do multithreaded dequeues in zstream_queue.h.
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

#define	JIFFIES_PER_SEC		100
#define	SAMPLE_DURATION_US	1000000
#define	CPU_FIELD_WIDTH		14

static void
print_batch_size_histogram(zstream_queue_t *queue)
{
	int last_nonzero = 0;
	static int lines_printed = 0;

	if (lines_printed++ == 0)
		fprintf(stderr, "\nBatch size histograms:\n");
	for (last_nonzero = MAX_BATCH; last_nonzero >= 0; last_nonzero--) {
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
	uint64_t period = SAMPLE_DURATION_US;
	struct timespec clock = {0};
	uint64_t start_us, end_us;
	uint64_t cpu_jif_prior = 0;
	uint64_t delta_jif, delta_cpu_jif;
	long unsigned int utime, stime;
	char buff[1024];
	boolean_t interrupt = B_FALSE;
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

		boolean_t stop = B_FALSE;
		if (cpu_jif_prior > 0) {
			delta_cpu_jif = utime + stime - cpu_jif_prior;
			delta_jif = (end_us - start_us) /
			    (JIFFIES_PER_SEC * 100);
			double cpu_pct = (double)delta_cpu_jif /
			    (pool.tp_num_threads * delta_jif);
			cpu_pct = MIN(cpu_pct, 0.9999); /* Don't print 100% */
			fprintf(stderr, "CPU: %5.2f%%   ", 100 * cpu_pct);
			/* Stop to investigate low CPU usage? */
			stop = interrupt && cpu_pct < 0.85 && cpu_pct > 0.1;
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

		if (stop)
			kill(getpid(), SIGSTOP);

		cpu_jif_prior = utime + stime;
		start_us = end_us;
	}
	return (NULL);
}

#endif	/* MONITOR_QUEUES */
