/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <stdlib.h>
#include <stddef.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>
#include <sys/list.h>
#include <sys/debug.h>

#include <sys/dmu_ctl.h>
#include <sys/dmu_ctl_impl.h>

static dctl_thr_info_t thr_pool = {
	.dti_mtx = PTHREAD_MUTEX_INITIALIZER
};

/*
 * Create n threads.
 * Callers must acquire thr_pool.dti_mtx first.
 */
static int dctl_thr_create(int n)
{
	dctl_thr_info_t *p = &thr_pool;
	int error;

	for (int i = 0; i < n; i++) {
		wthr_info_t *thr = malloc(sizeof(wthr_info_t));
		if (thr == NULL)
			return ENOMEM;

		thr->wthr_exit = B_FALSE;
		thr->wthr_free = B_TRUE;

		error = pthread_create(&thr->wthr_id, NULL, p->dti_thr_func,
		    thr);
		if (error) {
			free(thr);
			return error;
		}

		p->dti_free++;

		list_insert_tail(&p->dti_list, thr);
	}
	return 0;
}

/*
 * Mark the thread as dead.
 * Must be called right before exiting the main thread function.
 */
void dctl_thr_die(wthr_info_t *thr)
{
	dctl_thr_info_t *p = &thr_pool;

	thr->wthr_exit = B_TRUE;
	dctl_thr_rebalance(thr, B_FALSE);

	pthread_mutex_lock(&p->dti_mtx);

	list_remove(&p->dti_list, thr);
	list_insert_tail(&p->dti_join_list, thr);

	pthread_mutex_unlock(&p->dti_mtx);
}

/*
 * Clean-up dead threads.
 */
void dctl_thr_join()
{
	dctl_thr_info_t *p = &thr_pool;
	wthr_info_t *thr;

	pthread_mutex_lock(&p->dti_mtx);

	while ((thr = list_head(&p->dti_join_list))) {
		list_remove(&p->dti_join_list, thr);

		ASSERT(!pthread_equal(thr->wthr_id, pthread_self()));

		/*
		 * This should not block because all the threads
		 * on this list should have died already.
		 *
		 * pthread_join() can only return an error if
		 * we made a programming mistake.
		 */
		VERIFY(pthread_join(thr->wthr_id, NULL) == 0);

		ASSERT(thr->wthr_exit);
		ASSERT(!thr->wthr_free);

		free(thr);
	}

	pthread_mutex_unlock(&p->dti_mtx);
}

/*
 * Adjust the number of free threads in the pool and the thread status.
 *
 * Callers must acquire thr_pool.dti_mtx first.
 */
static void dctl_thr_adjust_free(wthr_info_t *thr, boolean_t set_free)
{
	dctl_thr_info_t *p = &thr_pool;

	ASSERT(p->dti_free >= 0);

	if (!thr->wthr_free && set_free)
		p->dti_free++;
	else if (thr->wthr_free && !set_free)
		p->dti_free--;

	ASSERT(p->dti_free >= 0);

	thr->wthr_free = set_free;
}

/*
 * Rebalance threads. Also adjusts the free status of the thread.
 * Will set the thread exit flag if the number of free threads is above
 * the limit.
 */
void dctl_thr_rebalance(wthr_info_t *thr, boolean_t set_free)
{
	dctl_thr_info_t *p = &thr_pool;

	pthread_mutex_lock(&p->dti_mtx);

	if (p->dti_exit || p->dti_free > p->dti_max_free)
		thr->wthr_exit = B_TRUE;

	if (thr->wthr_exit)
		set_free = B_FALSE;

	dctl_thr_adjust_free(thr, set_free);

	if (!p->dti_exit && p->dti_free == 0)
		dctl_thr_create(1);

	pthread_mutex_unlock(&p->dti_mtx);
}

/*
 * Stop the thread pool.
 *
 * This can take a while since it actually waits for all threads to exit.
 */
void dctl_thr_pool_stop()
{
	dctl_thr_info_t *p = &thr_pool;
	wthr_info_t *thr;
	struct timespec ts;

	pthread_mutex_lock(&p->dti_mtx);

	ASSERT(!p->dti_exit);
	p->dti_exit = B_TRUE;

	/* Let's flag the threads first */
	thr = list_head(&p->dti_list);
	while (thr != NULL) {
		thr->wthr_exit = B_TRUE;
		dctl_thr_adjust_free(thr, B_FALSE);

		thr = list_next(&p->dti_list, thr);
	}

	pthread_mutex_unlock(&p->dti_mtx);

	/* Now let's wait for them to exit */
	ts.tv_sec = 0;
	ts.tv_nsec = 50000000; /* 50ms */
	do {
		nanosleep(&ts, NULL);

		pthread_mutex_lock(&p->dti_mtx);
		thr = list_head(&p->dti_list);
		pthread_mutex_unlock(&p->dti_mtx);

		dctl_thr_join();
	} while(thr != NULL);

	ASSERT(p->dti_free == 0);

	ASSERT(list_is_empty(&p->dti_list));
	ASSERT(list_is_empty(&p->dti_join_list));

	list_destroy(&p->dti_list);
	list_destroy(&p->dti_join_list);
}

/*
 * Create thread pool.
 *
 * If at least one thread creation fails, it will stop all previous
 * threads and return a non-zero value.
 */
int dctl_thr_pool_create(int min_thr, int max_free_thr,
    thr_func_t *thr_func)
{
	int error;
	dctl_thr_info_t *p = &thr_pool;

	ASSERT(p->dti_free == 0);

	/* Initialize global variables */
	p->dti_min = min_thr;
	p->dti_max_free = max_free_thr;
	p->dti_exit = B_FALSE;
	p->dti_thr_func = thr_func;

	list_create(&p->dti_list, sizeof(wthr_info_t), offsetof(wthr_info_t,
	    wthr_node));
	list_create(&p->dti_join_list, sizeof(wthr_info_t),
	    offsetof(wthr_info_t, wthr_node));

	pthread_mutex_lock(&p->dti_mtx);
	error = dctl_thr_create(min_thr);
	pthread_mutex_unlock(&p->dti_mtx);

	if (error)
		dctl_thr_pool_stop();

	return error;
}
