/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://github.com/behlendorf/spl/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************
 *  Solaris Porting Layer (SPL) Task Queue Implementation.
\*****************************************************************************/

#include <sys/taskq.h>
#include <sys/kmem.h>
#include <spl-debug.h>

#ifdef SS_DEBUG_SUBSYS
#undef SS_DEBUG_SUBSYS
#endif

#define SS_DEBUG_SUBSYS SS_TASKQ

/* Global system-wide dynamic task queue available for all consumers */
taskq_t *system_taskq;
EXPORT_SYMBOL(system_taskq);

typedef struct spl_task {
        spinlock_t              t_lock;
        struct list_head        t_list;
        taskqid_t               t_id;
        task_func_t             *t_func;
        void                    *t_arg;
} spl_task_t;

/*
 * NOTE: Must be called with tq->tq_lock held, returns a list_t which
 * is not attached to the free, work, or pending taskq lists.
 */
static spl_task_t *
task_alloc(taskq_t *tq, uint_t flags)
{
        spl_task_t *t;
        int count = 0;
        SENTRY;

        ASSERT(tq);
        ASSERT(flags & (TQ_SLEEP | TQ_NOSLEEP));               /* One set */
        ASSERT(!((flags & TQ_SLEEP) && (flags & TQ_NOSLEEP))); /* Not both */
        ASSERT(spin_is_locked(&tq->tq_lock));
retry:
        /* Acquire spl_task_t's from free list if available */
        if (!list_empty(&tq->tq_free_list) && !(flags & TQ_NEW)) {
                t = list_entry(tq->tq_free_list.next, spl_task_t, t_list);
                list_del_init(&t->t_list);
                SRETURN(t);
        }

        /* Free list is empty and memory allocations are prohibited */
        if (flags & TQ_NOALLOC)
                SRETURN(NULL);

        /* Hit maximum spl_task_t pool size */
        if (tq->tq_nalloc >= tq->tq_maxalloc) {
                if (flags & TQ_NOSLEEP)
                        SRETURN(NULL);

                /*
                 * Sleep periodically polling the free list for an available
                 * spl_task_t. Dispatching with TQ_SLEEP should always succeed
                 * but we cannot block forever waiting for an spl_taskq_t to
                 * show up in the free list, otherwise a deadlock can happen.
                 *
                 * Therefore, we need to allocate a new task even if the number
                 * of allocated tasks is above tq->tq_maxalloc, but we still
                 * end up delaying the task allocation by one second, thereby
                 * throttling the task dispatch rate.
                 */
                 spin_unlock_irqrestore(&tq->tq_lock, tq->tq_lock_flags);
                 schedule_timeout(HZ / 100);
                 spin_lock_irqsave(&tq->tq_lock, tq->tq_lock_flags);
                 if (count < 100)
                        SGOTO(retry, count++);
        }

        spin_unlock_irqrestore(&tq->tq_lock, tq->tq_lock_flags);
        t = kmem_alloc(sizeof(spl_task_t), flags & (TQ_SLEEP | TQ_NOSLEEP));
        spin_lock_irqsave(&tq->tq_lock, tq->tq_lock_flags);

        if (t) {
                spin_lock_init(&t->t_lock);
                INIT_LIST_HEAD(&t->t_list);
                t->t_id = 0;
                t->t_func = NULL;
                t->t_arg = NULL;
                tq->tq_nalloc++;
        }

        SRETURN(t);
}

/*
 * NOTE: Must be called with tq->tq_lock held, expects the spl_task_t
 * to already be removed from the free, work, or pending taskq lists.
 */
static void
task_free(taskq_t *tq, spl_task_t *t)
{
        SENTRY;

        ASSERT(tq);
        ASSERT(t);
	ASSERT(spin_is_locked(&tq->tq_lock));
	ASSERT(list_empty(&t->t_list));

        kmem_free(t, sizeof(spl_task_t));
        tq->tq_nalloc--;

	SEXIT;
}

/*
 * NOTE: Must be called with tq->tq_lock held, either destroys the
 * spl_task_t if too many exist or moves it to the free list for later use.
 */
static void
task_done(taskq_t *tq, spl_task_t *t)
{
	SENTRY;
	ASSERT(tq);
	ASSERT(t);
	ASSERT(spin_is_locked(&tq->tq_lock));

	list_del_init(&t->t_list);

        if (tq->tq_nalloc <= tq->tq_minalloc) {
		t->t_id = 0;
		t->t_func = NULL;
		t->t_arg = NULL;
                list_add_tail(&t->t_list, &tq->tq_free_list);
	} else {
		task_free(tq, t);
	}

        SEXIT;
}

/*
 * As tasks are submitted to the task queue they are assigned a
 * monotonically increasing taskqid and added to the tail of the pending
 * list.  As worker threads become available the tasks are removed from
 * the head of the pending or priority list, giving preference to the
 * priority list.  The tasks are then added to the work list, preserving
 * the ordering by taskqid.  Finally, as tasks complete they are removed
 * from the work list.  This means that the pending and work lists are
 * always kept sorted by taskqid.  Thus the lowest outstanding
 * incomplete taskqid can be determined simply by checking the min
 * taskqid for each head item on the pending, priority, and work list.
 * This value is stored in tq->tq_lowest_id and only updated to the new
 * lowest id when the previous lowest id completes.  All taskqids lower
 * than tq->tq_lowest_id must have completed.  It is also possible
 * larger taskqid's have completed because they may be processed in
 * parallel by several worker threads.  However, this is not a problem
 * because the behavior of taskq_wait_id() is to block until all
 * previously submitted taskqid's have completed.
 *
 * XXX: Taskqid_t wrapping is not handled.  However, taskqid_t's are
 * 64-bit values so even if a taskq is processing 2^24 (16,777,216)
 * taskqid_ts per second it will still take 2^40 seconds, 34,865 years,
 * before the wrap occurs.  I can live with that for now.
 */
static int
taskq_wait_check(taskq_t *tq, taskqid_t id)
{
	int rc;

	spin_lock_irqsave(&tq->tq_lock, tq->tq_lock_flags);
	rc = (id < tq->tq_lowest_id);
	spin_unlock_irqrestore(&tq->tq_lock, tq->tq_lock_flags);

	SRETURN(rc);
}

void
__taskq_wait_id(taskq_t *tq, taskqid_t id)
{
	SENTRY;
	ASSERT(tq);

	wait_event(tq->tq_wait_waitq, taskq_wait_check(tq, id));

	SEXIT;
}
EXPORT_SYMBOL(__taskq_wait_id);

void
__taskq_wait(taskq_t *tq)
{
	taskqid_t id;
	SENTRY;
	ASSERT(tq);

	/* Wait for the largest outstanding taskqid */
	spin_lock_irqsave(&tq->tq_lock, tq->tq_lock_flags);
	id = tq->tq_next_id - 1;
	spin_unlock_irqrestore(&tq->tq_lock, tq->tq_lock_flags);

	__taskq_wait_id(tq, id);

	SEXIT;

}
EXPORT_SYMBOL(__taskq_wait);

int
__taskq_member(taskq_t *tq, void *t)
{
        int i;
        SENTRY;

	ASSERT(tq);
        ASSERT(t);

        for (i = 0; i < tq->tq_nthreads; i++)
                if (tq->tq_threads[i] == (struct task_struct *)t)
                        SRETURN(1);

        SRETURN(0);
}
EXPORT_SYMBOL(__taskq_member);

taskqid_t
__taskq_dispatch(taskq_t *tq, task_func_t func, void *arg, uint_t flags)
{
        spl_task_t *t;
	taskqid_t rc = 0;
        SENTRY;

        ASSERT(tq);
        ASSERT(func);

	/* Solaris assumes TQ_SLEEP if not passed explicitly */
	if (!(flags & (TQ_SLEEP | TQ_NOSLEEP)))
		flags |= TQ_SLEEP;

	if (unlikely(in_atomic() && (flags & TQ_SLEEP)))
		PANIC("May schedule while atomic: %s/0x%08x/%d\n",
		    current->comm, preempt_count(), current->pid);

        spin_lock_irqsave(&tq->tq_lock, tq->tq_lock_flags);

	/* Taskq being destroyed and all tasks drained */
	if (!(tq->tq_flags & TQ_ACTIVE))
		SGOTO(out, rc = 0);

	/* Do not queue the task unless there is idle thread for it */
	ASSERT(tq->tq_nactive <= tq->tq_nthreads);
	if ((flags & TQ_NOQUEUE) && (tq->tq_nactive == tq->tq_nthreads))
		SGOTO(out, rc = 0);

        if ((t = task_alloc(tq, flags)) == NULL)
		SGOTO(out, rc = 0);

	spin_lock(&t->t_lock);

	/* Queue to the priority list instead of the pending list */
	if (flags & TQ_FRONT)
		list_add_tail(&t->t_list, &tq->tq_prio_list);
	else
		list_add_tail(&t->t_list, &tq->tq_pend_list);

	t->t_id = rc = tq->tq_next_id;
	tq->tq_next_id++;
        t->t_func = func;
        t->t_arg = arg;
	spin_unlock(&t->t_lock);

	wake_up(&tq->tq_work_waitq);
out:
	spin_unlock_irqrestore(&tq->tq_lock, tq->tq_lock_flags);
	SRETURN(rc);
}
EXPORT_SYMBOL(__taskq_dispatch);

/*
 * Returns the lowest incomplete taskqid_t.  The taskqid_t may
 * be queued on the pending list, on the priority list,  or on
 * the work list currently being handled, but it is not 100%
 * complete yet.
 */
static taskqid_t
taskq_lowest_id(taskq_t *tq)
{
	taskqid_t lowest_id = tq->tq_next_id;
        spl_task_t *t;
	SENTRY;

	ASSERT(tq);
	ASSERT(spin_is_locked(&tq->tq_lock));

	if (!list_empty(&tq->tq_pend_list)) {
		t = list_entry(tq->tq_pend_list.next, spl_task_t, t_list);
		lowest_id = MIN(lowest_id, t->t_id);
	}

	if (!list_empty(&tq->tq_prio_list)) {
		t = list_entry(tq->tq_prio_list.next, spl_task_t, t_list);
		lowest_id = MIN(lowest_id, t->t_id);
	}

	if (!list_empty(&tq->tq_work_list)) {
		t = list_entry(tq->tq_work_list.next, spl_task_t, t_list);
		lowest_id = MIN(lowest_id, t->t_id);
	}

	SRETURN(lowest_id);
}

/*
 * Insert a task into a list keeping the list sorted by increasing
 * taskqid.
 */
static void
taskq_insert_in_order(taskq_t *tq, spl_task_t *t)
{
	spl_task_t *w;
	struct list_head *l;

	SENTRY;
	ASSERT(tq);
	ASSERT(t);
	ASSERT(spin_is_locked(&tq->tq_lock));

	list_for_each_prev(l, &tq->tq_work_list) {
		w = list_entry(l, spl_task_t, t_list);
		if (w->t_id < t->t_id) {
			list_add(&t->t_list, l);
			break;
		}
	}
	if (l == &tq->tq_work_list)
		list_add(&t->t_list, &tq->tq_work_list);

	SEXIT;
}

static int
taskq_thread(void *args)
{
        DECLARE_WAITQUEUE(wait, current);
        sigset_t blocked;
	taskqid_t id;
        taskq_t *tq = args;
        spl_task_t *t;
	struct list_head *pend_list;
	SENTRY;

        ASSERT(tq);
        current->flags |= PF_NOFREEZE;

        sigfillset(&blocked);
        sigprocmask(SIG_BLOCK, &blocked, NULL);
        flush_signals(current);

        spin_lock_irqsave(&tq->tq_lock, tq->tq_lock_flags);
        tq->tq_nthreads++;
        wake_up(&tq->tq_wait_waitq);
        set_current_state(TASK_INTERRUPTIBLE);

        while (!kthread_should_stop()) {

		add_wait_queue(&tq->tq_work_waitq, &wait);
		if (list_empty(&tq->tq_pend_list) &&
		    list_empty(&tq->tq_prio_list)) {
			spin_unlock_irqrestore(&tq->tq_lock, tq->tq_lock_flags);
			schedule();
			spin_lock_irqsave(&tq->tq_lock, tq->tq_lock_flags);
		} else {
			__set_current_state(TASK_RUNNING);
		}

		remove_wait_queue(&tq->tq_work_waitq, &wait);

		if (!list_empty(&tq->tq_prio_list))
			pend_list = &tq->tq_prio_list;
		else if (!list_empty(&tq->tq_pend_list))
			pend_list = &tq->tq_pend_list;
		else
			pend_list = NULL;

		if (pend_list) {
                        t = list_entry(pend_list->next, spl_task_t, t_list);
                        list_del_init(&t->t_list);
			taskq_insert_in_order(tq, t);
                        tq->tq_nactive++;
			spin_unlock_irqrestore(&tq->tq_lock, tq->tq_lock_flags);

			/* Perform the requested task */
                        t->t_func(t->t_arg);

			spin_lock_irqsave(&tq->tq_lock, tq->tq_lock_flags);
                        tq->tq_nactive--;
			id = t->t_id;
                        task_done(tq, t);

			/* When the current lowest outstanding taskqid is
			 * done calculate the new lowest outstanding id */
			if (tq->tq_lowest_id == id) {
				tq->tq_lowest_id = taskq_lowest_id(tq);
				ASSERT(tq->tq_lowest_id > id);
			}

                        wake_up_all(&tq->tq_wait_waitq);
		}

		set_current_state(TASK_INTERRUPTIBLE);

        }

	__set_current_state(TASK_RUNNING);
        tq->tq_nthreads--;
        spin_unlock_irqrestore(&tq->tq_lock, tq->tq_lock_flags);

	SRETURN(0);
}

taskq_t *
__taskq_create(const char *name, int nthreads, pri_t pri,
               int minalloc, int maxalloc, uint_t flags)
{
        taskq_t *tq;
        struct task_struct *t;
        int rc = 0, i, j = 0;
        SENTRY;

        ASSERT(name != NULL);
        ASSERT(pri <= maxclsyspri);
        ASSERT(minalloc >= 0);
        ASSERT(maxalloc <= INT_MAX);
        ASSERT(!(flags & (TASKQ_CPR_SAFE | TASKQ_DYNAMIC))); /* Unsupported */

	/* Scale the number of threads using nthreads as a percentage */
	if (flags & TASKQ_THREADS_CPU_PCT) {
		ASSERT(nthreads <= 100);
		ASSERT(nthreads >= 0);
		nthreads = MIN(nthreads, 100);
		nthreads = MAX(nthreads, 0);
		nthreads = MAX((num_online_cpus() * nthreads) / 100, 1);
	}

        tq = kmem_alloc(sizeof(*tq), KM_SLEEP);
        if (tq == NULL)
                SRETURN(NULL);

        tq->tq_threads = kmem_alloc(nthreads * sizeof(t), KM_SLEEP);
        if (tq->tq_threads == NULL) {
                kmem_free(tq, sizeof(*tq));
                SRETURN(NULL);
        }

        spin_lock_init(&tq->tq_lock);
        spin_lock_irqsave(&tq->tq_lock, tq->tq_lock_flags);
        tq->tq_name      = name;
        tq->tq_nactive   = 0;
	tq->tq_nthreads  = 0;
        tq->tq_pri       = pri;
        tq->tq_minalloc  = minalloc;
        tq->tq_maxalloc  = maxalloc;
	tq->tq_nalloc    = 0;
        tq->tq_flags     = (flags | TQ_ACTIVE);
	tq->tq_next_id   = 1;
	tq->tq_lowest_id = 1;
        INIT_LIST_HEAD(&tq->tq_free_list);
        INIT_LIST_HEAD(&tq->tq_work_list);
        INIT_LIST_HEAD(&tq->tq_pend_list);
        INIT_LIST_HEAD(&tq->tq_prio_list);
        init_waitqueue_head(&tq->tq_work_waitq);
        init_waitqueue_head(&tq->tq_wait_waitq);

        if (flags & TASKQ_PREPOPULATE)
                for (i = 0; i < minalloc; i++)
                        task_done(tq, task_alloc(tq, TQ_SLEEP | TQ_NEW));

        spin_unlock_irqrestore(&tq->tq_lock, tq->tq_lock_flags);

        for (i = 0; i < nthreads; i++) {
                t = kthread_create(taskq_thread, tq, "%s/%d", name, i);
                if (t) {
                        tq->tq_threads[i] = t;
                        kthread_bind(t, i % num_online_cpus());
                        set_user_nice(t, PRIO_TO_NICE(pri));
                        wake_up_process(t);
			j++;
                } else {
                        tq->tq_threads[i] = NULL;
                        rc = 1;
                }
        }

        /* Wait for all threads to be started before potential destroy */
	wait_event(tq->tq_wait_waitq, tq->tq_nthreads == j);

        if (rc) {
                __taskq_destroy(tq);
                tq = NULL;
        }

        SRETURN(tq);
}
EXPORT_SYMBOL(__taskq_create);

void
__taskq_destroy(taskq_t *tq)
{
	spl_task_t *t;
	int i, nthreads;
	SENTRY;

	ASSERT(tq);
	spin_lock_irqsave(&tq->tq_lock, tq->tq_lock_flags);
        tq->tq_flags &= ~TQ_ACTIVE;
	spin_unlock_irqrestore(&tq->tq_lock, tq->tq_lock_flags);

	/* TQ_ACTIVE cleared prevents new tasks being added to pending */
        __taskq_wait(tq);

	nthreads = tq->tq_nthreads;
	for (i = 0; i < nthreads; i++)
		if (tq->tq_threads[i])
			kthread_stop(tq->tq_threads[i]);

        spin_lock_irqsave(&tq->tq_lock, tq->tq_lock_flags);

        while (!list_empty(&tq->tq_free_list)) {
		t = list_entry(tq->tq_free_list.next, spl_task_t, t_list);
	        list_del_init(&t->t_list);
                task_free(tq, t);
        }

        ASSERT(tq->tq_nthreads == 0);
        ASSERT(tq->tq_nalloc == 0);
        ASSERT(list_empty(&tq->tq_free_list));
        ASSERT(list_empty(&tq->tq_work_list));
        ASSERT(list_empty(&tq->tq_pend_list));
        ASSERT(list_empty(&tq->tq_prio_list));

        spin_unlock_irqrestore(&tq->tq_lock, tq->tq_lock_flags);
        kmem_free(tq->tq_threads, nthreads * sizeof(spl_task_t *));
        kmem_free(tq, sizeof(taskq_t));

	SEXIT;
}
EXPORT_SYMBOL(__taskq_destroy);

int
spl_taskq_init(void)
{
        SENTRY;

	/* Solaris creates a dynamic taskq of up to 64 threads, however in
	 * a Linux environment 1 thread per-core is usually about right */
        system_taskq = taskq_create("spl_system_taskq", num_online_cpus(),
				    minclsyspri, 4, 512, TASKQ_PREPOPULATE);
	if (system_taskq == NULL)
		SRETURN(1);

        SRETURN(0);
}

void
spl_taskq_fini(void)
{
        SENTRY;
	taskq_destroy(system_taskq);
        SEXIT;
}
