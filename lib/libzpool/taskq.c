/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
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
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/zfs_context.h>

int taskq_now;
taskq_t *system_taskq;

typedef struct task {
	struct task	*task_next;
	struct task	*task_prev;
	task_func_t	*task_func;
	void		*task_arg;
} task_t;

#define	TASKQ_ACTIVE	0x00010000

struct taskq {
	kmutex_t	tq_lock;
	krwlock_t	tq_threadlock;
	kcondvar_t	tq_dispatch_cv;
	kcondvar_t	tq_wait_cv;
	thread_t	*tq_threadlist;
	int		tq_flags;
	int		tq_active;
	int		tq_nthreads;
	int		tq_nalloc;
	int		tq_minalloc;
	int		tq_maxalloc;
	kcondvar_t	tq_maxalloc_cv;
	int		tq_maxalloc_wait;
	task_t		*tq_freelist;
	task_t		tq_task;
};

static task_t *
task_alloc(taskq_t *tq, int tqflags)
{
	task_t *t;
	int rv;

again:	if ((t = tq->tq_freelist) != NULL && tq->tq_nalloc >= tq->tq_minalloc) {
		tq->tq_freelist = t->task_next;
	} else {
		if (tq->tq_nalloc >= tq->tq_maxalloc) {
			if (!(tqflags & KM_SLEEP))
				return (NULL);

			/*
			 * We don't want to exceed tq_maxalloc, but we can't
			 * wait for other tasks to complete (and thus free up
			 * task structures) without risking deadlock with
			 * the caller.  So, we just delay for one second
			 * to throttle the allocation rate. If we have tasks
			 * complete before one second timeout expires then
			 * taskq_ent_free will signal us and we will
			 * immediately retry the allocation.
			 */
			tq->tq_maxalloc_wait++;
			rv = cv_timedwait(&tq->tq_maxalloc_cv,
			    &tq->tq_lock, ddi_get_lbolt() + hz);
			tq->tq_maxalloc_wait--;
			if (rv > 0)
				goto again;		/* signaled */
		}
		mutex_exit(&tq->tq_lock);

		t = kmem_alloc(sizeof (task_t), tqflags);

		mutex_enter(&tq->tq_lock);
		if (t != NULL)
			tq->tq_nalloc++;
	}
	return (t);
}

static void
task_free(taskq_t *tq, task_t *t)
{
	if (tq->tq_nalloc <= tq->tq_minalloc) {
		t->task_next = tq->tq_freelist;
		tq->tq_freelist = t;
	} else {
		tq->tq_nalloc--;
		mutex_exit(&tq->tq_lock);
		kmem_free(t, sizeof (task_t));
		mutex_enter(&tq->tq_lock);
	}

	if (tq->tq_maxalloc_wait)
		cv_signal(&tq->tq_maxalloc_cv);
}

taskqid_t
taskq_dispatch(taskq_t *tq, task_func_t func, void *arg, uint_t tqflags)
{
	task_t *t;

	if (taskq_now) {
		func(arg);
		return (1);
	}

	mutex_enter(&tq->tq_lock);
	ASSERT(tq->tq_flags & TASKQ_ACTIVE);
	if ((t = task_alloc(tq, tqflags)) == NULL) {
		mutex_exit(&tq->tq_lock);
		return (0);
	}
	if (tqflags & TQ_FRONT) {
		t->task_next = tq->tq_task.task_next;
		t->task_prev = &tq->tq_task;
	} else {
		t->task_next = &tq->tq_task;
		t->task_prev = tq->tq_task.task_prev;
	}
	t->task_next->task_prev = t;
	t->task_prev->task_next = t;
	t->task_func = func;
	t->task_arg = arg;
	cv_signal(&tq->tq_dispatch_cv);
	mutex_exit(&tq->tq_lock);
	return (1);
}

void
taskq_wait(taskq_t *tq)
{
	mutex_enter(&tq->tq_lock);
	while (tq->tq_task.task_next != &tq->tq_task || tq->tq_active != 0)
		cv_wait(&tq->tq_wait_cv, &tq->tq_lock);
	mutex_exit(&tq->tq_lock);
}

static void *
taskq_thread(void *arg)
{
	taskq_t *tq = arg;
	task_t *t;

	mutex_enter(&tq->tq_lock);
	while (tq->tq_flags & TASKQ_ACTIVE) {
		if ((t = tq->tq_task.task_next) == &tq->tq_task) {
			if (--tq->tq_active == 0)
				cv_broadcast(&tq->tq_wait_cv);
			cv_wait(&tq->tq_dispatch_cv, &tq->tq_lock);
			tq->tq_active++;
			continue;
		}
		t->task_prev->task_next = t->task_next;
		t->task_next->task_prev = t->task_prev;
		mutex_exit(&tq->tq_lock);

		rw_enter(&tq->tq_threadlock, RW_READER);
		t->task_func(t->task_arg);
		rw_exit(&tq->tq_threadlock);

		mutex_enter(&tq->tq_lock);
		task_free(tq, t);
	}
	tq->tq_nthreads--;
	cv_broadcast(&tq->tq_wait_cv);
	mutex_exit(&tq->tq_lock);
	return (NULL);
}

/*ARGSUSED*/
taskq_t *
taskq_create(const char *name, int nthreads, pri_t pri,
	int minalloc, int maxalloc, uint_t flags)
{
	taskq_t *tq = kmem_zalloc(sizeof (taskq_t), KM_SLEEP);
	int t;

	if (flags & TASKQ_THREADS_CPU_PCT) {
		int pct;
		ASSERT3S(nthreads, >=, 0);
		ASSERT3S(nthreads, <=, 100);
		pct = MIN(nthreads, 100);
		pct = MAX(pct, 0);

		nthreads = (sysconf(_SC_NPROCESSORS_ONLN) * pct) / 100;
		nthreads = MAX(nthreads, 1);	/* need at least 1 thread */
	} else {
		ASSERT3S(nthreads, >=, 1);
	}

	rw_init(&tq->tq_threadlock, NULL, RW_DEFAULT, NULL);
	mutex_init(&tq->tq_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&tq->tq_dispatch_cv, NULL, CV_DEFAULT, NULL);
	cv_init(&tq->tq_wait_cv, NULL, CV_DEFAULT, NULL);
	cv_init(&tq->tq_maxalloc_cv, NULL, CV_DEFAULT, NULL);
	tq->tq_flags = flags | TASKQ_ACTIVE;
	tq->tq_active = nthreads;
	tq->tq_nthreads = nthreads;
	tq->tq_minalloc = minalloc;
	tq->tq_maxalloc = maxalloc;
	tq->tq_task.task_next = &tq->tq_task;
	tq->tq_task.task_prev = &tq->tq_task;
	tq->tq_threadlist = kmem_alloc(nthreads * sizeof (thread_t), KM_SLEEP);

	if (flags & TASKQ_PREPOPULATE) {
		mutex_enter(&tq->tq_lock);
		while (minalloc-- > 0)
			task_free(tq, task_alloc(tq, KM_SLEEP));
		mutex_exit(&tq->tq_lock);
	}

	for (t = 0; t < nthreads; t++)
		(void) thr_create(0, 0, taskq_thread,
		    tq, THR_BOUND, &tq->tq_threadlist[t]);

	return (tq);
}

void
taskq_destroy(taskq_t *tq)
{
	int t;
	int nthreads = tq->tq_nthreads;

	taskq_wait(tq);

	mutex_enter(&tq->tq_lock);

	tq->tq_flags &= ~TASKQ_ACTIVE;
	cv_broadcast(&tq->tq_dispatch_cv);

	while (tq->tq_nthreads != 0)
		cv_wait(&tq->tq_wait_cv, &tq->tq_lock);

	tq->tq_minalloc = 0;
	while (tq->tq_nalloc != 0) {
		ASSERT(tq->tq_freelist != NULL);
		task_free(tq, task_alloc(tq, KM_SLEEP));
	}

	mutex_exit(&tq->tq_lock);

	for (t = 0; t < nthreads; t++)
		(void) thr_join(tq->tq_threadlist[t], NULL, NULL);

	kmem_free(tq->tq_threadlist, nthreads * sizeof (thread_t));

	rw_destroy(&tq->tq_threadlock);
	mutex_destroy(&tq->tq_lock);
	cv_destroy(&tq->tq_dispatch_cv);
	cv_destroy(&tq->tq_wait_cv);
	cv_destroy(&tq->tq_maxalloc_cv);

	kmem_free(tq, sizeof (taskq_t));
}

int
taskq_member(taskq_t *tq, void *t)
{
	int i;

	if (taskq_now)
		return (1);

	for (i = 0; i < tq->tq_nthreads; i++)
		if (tq->tq_threadlist[i] == (thread_t)(uintptr_t)t)
			return (1);

	return (0);
}

void
system_taskq_init(void)
{
	system_taskq = taskq_create("system_taskq", 64, minclsyspri, 4, 512,
	    TASKQ_DYNAMIC | TASKQ_PREPOPULATE);
}

void
system_taskq_fini(void)
{
	taskq_destroy(system_taskq);
	system_taskq = NULL; /* defensive */
}
