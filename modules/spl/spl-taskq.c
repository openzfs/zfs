/*
 *  This file is part of the SPL: Solaris Porting Layer.
 *
 *  Copyright (c) 2008 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory
 *  Written by:
 *          Brian Behlendorf <behlendorf1@llnl.gov>,
 *          Herb Wartens <wartens2@llnl.gov>,
 *          Jim Garlick <garlick@llnl.gov>
 *  UCRL-CODE-235197
 *
 *  This is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#include <sys/taskq.h>
#include <sys/kmem.h>

#ifdef DEBUG_SUBSYSTEM
#undef DEBUG_SUBSYSTEM
#endif

#define DEBUG_SUBSYSTEM S_TASKQ

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

/* NOTE: Must be called with tq->tq_lock held, returns a list_t which
 * is not attached to the free, work, or pending taskq lists.
 */
static spl_task_t *
task_alloc(taskq_t *tq, uint_t flags)
{
        spl_task_t *t;
        int count = 0;
        ENTRY;

        ASSERT(tq);
        ASSERT(flags & (TQ_SLEEP | TQ_NOSLEEP));               /* One set */
        ASSERT(!((flags & TQ_SLEEP) && (flags & TQ_NOSLEEP))); /* Not both */
        ASSERT(spin_is_locked(&tq->tq_lock));
retry:
        /* Aquire spl_task_t's from free list if available */
        if (!list_empty(&tq->tq_free_list) && !(flags & TQ_NEW)) {
                t = list_entry(tq->tq_free_list.next, spl_task_t, t_list);
                list_del_init(&t->t_list);
                RETURN(t);
        }

        /* Free list is empty and memory allocs are prohibited */
        if (flags & TQ_NOALLOC)
                RETURN(NULL);

        /* Hit maximum spl_task_t pool size */
        if (tq->tq_nalloc >= tq->tq_maxalloc) {
                if (flags & TQ_NOSLEEP)
                        RETURN(NULL);

                /* Sleep periodically polling the free list for an available
                 * spl_task_t.  If a full second passes and we have not found
                 * one gives up and return a NULL to the caller. */
                if (flags & TQ_SLEEP) {
                        spin_unlock_irqrestore(&tq->tq_lock, tq->tq_lock_flags);
                        schedule_timeout(HZ / 100);
                        spin_lock_irqsave(&tq->tq_lock, tq->tq_lock_flags);
                        if (count < 100)
                                GOTO(retry, count++);

                        RETURN(NULL);
                }

                /* Unreachable, TQ_SLEEP xor TQ_NOSLEEP */
                SBUG();
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

        RETURN(t);
}

/* NOTE: Must be called with tq->tq_lock held, expectes the spl_task_t
 * to already be removed from the free, work, or pending taskq lists.
 */
static void
task_free(taskq_t *tq, spl_task_t *t)
{
        ENTRY;

        ASSERT(tq);
        ASSERT(t);
	ASSERT(spin_is_locked(&tq->tq_lock));
	ASSERT(list_empty(&t->t_list));

        kmem_free(t, sizeof(spl_task_t));
        tq->tq_nalloc--;

	EXIT;
}

/* NOTE: Must be called with tq->tq_lock held, either destroyes the
 * spl_task_t if too many exist or moves it to the free list for later use.
 */
static void
task_done(taskq_t *tq, spl_task_t *t)
{
	ENTRY;
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

        EXIT;
}

/* Taskqid's are handed out in a monotonically increasing fashion per
 * taskq_t.  We don't handle taskqid wrapping yet, but fortuntely it isi
 * a 64-bit value so this is probably never going to happen.  The lowest
 * pending taskqid is stored in the taskq_t to make it easy for any
 * taskq_wait()'ers to know if the tasks they're waiting for have
 * completed.  Unfortunately, tq_task_lowest is kept up to date is
 * a pretty brain dead way, something more clever should be done.
 */
static int
taskq_wait_check(taskq_t *tq, taskqid_t id)
{
	RETURN(tq->tq_lowest_id >= id);
}

/* Expected to wait for all previously scheduled tasks to complete.  We do
 * not need to wait for tasked scheduled after this call to complete.  In
 * otherwords we do not need to drain the entire taskq. */
void
__taskq_wait_id(taskq_t *tq, taskqid_t id)
{
	ENTRY;
	ASSERT(tq);

	wait_event(tq->tq_wait_waitq, taskq_wait_check(tq, id));

	EXIT;
}
EXPORT_SYMBOL(__taskq_wait_id);

void
__taskq_wait(taskq_t *tq)
{
	taskqid_t id;
	ENTRY;
	ASSERT(tq);

	spin_lock_irqsave(&tq->tq_lock, tq->tq_lock_flags);
	id = tq->tq_next_id;
	spin_unlock_irqrestore(&tq->tq_lock, tq->tq_lock_flags);

	__taskq_wait_id(tq, id);

	EXIT;

}
EXPORT_SYMBOL(__taskq_wait);

int
__taskq_member(taskq_t *tq, void *t)
{
        int i;
        ENTRY;

	ASSERT(tq);
        ASSERT(t);

        for (i = 0; i < tq->tq_nthreads; i++)
                if (tq->tq_threads[i] == (struct task_struct *)t)
                        RETURN(1);

        RETURN(0);
}
EXPORT_SYMBOL(__taskq_member);

taskqid_t
__taskq_dispatch(taskq_t *tq, task_func_t func, void *arg, uint_t flags)
{
        spl_task_t *t;
	taskqid_t rc = 0;
        ENTRY;

        ASSERT(tq);
        ASSERT(func);
        if (unlikely(in_atomic() && (flags & TQ_SLEEP))) {
		CERROR("May schedule while atomic: %s/0x%08x/%d\n",
                       current->comm, preempt_count(), current->pid);
		SBUG();
	}

        spin_lock_irqsave(&tq->tq_lock, tq->tq_lock_flags);

	/* Taskq being destroyed and all tasks drained */
	if (!(tq->tq_flags & TQ_ACTIVE))
		GOTO(out, rc = 0);

	/* Do not queue the task unless there is idle thread for it */
	ASSERT(tq->tq_nactive <= tq->tq_nthreads);
	if ((flags & TQ_NOQUEUE) && (tq->tq_nactive == tq->tq_nthreads))
		GOTO(out, rc = 0);

        if ((t = task_alloc(tq, flags)) == NULL)
		GOTO(out, rc = 0);

	spin_lock(&t->t_lock);
	list_add_tail(&t->t_list, &tq->tq_pend_list);
	t->t_id = rc = tq->tq_next_id;
	tq->tq_next_id++;
        t->t_func = func;
        t->t_arg = arg;
	spin_unlock(&t->t_lock);

	wake_up(&tq->tq_work_waitq);
out:
	spin_unlock_irqrestore(&tq->tq_lock, tq->tq_lock_flags);
	RETURN(rc);
}
EXPORT_SYMBOL(__taskq_dispatch);

/* NOTE: Must be called with tq->tq_lock held */
static taskqid_t
taskq_lowest_id(taskq_t *tq)
{
	taskqid_t lowest_id = ~0;
        spl_task_t *t;
	ENTRY;

	ASSERT(tq);
	ASSERT(spin_is_locked(&tq->tq_lock));

	list_for_each_entry(t, &tq->tq_pend_list, t_list)
		if (t->t_id < lowest_id)
			lowest_id = t->t_id;

	list_for_each_entry(t, &tq->tq_work_list, t_list)
		if (t->t_id < lowest_id)
			lowest_id = t->t_id;

	RETURN(lowest_id);
}

static int
taskq_thread(void *args)
{
        DECLARE_WAITQUEUE(wait, current);
        sigset_t blocked;
	taskqid_t id;
        taskq_t *tq = args;
        spl_task_t *t;
	ENTRY;

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
		if (list_empty(&tq->tq_pend_list)) {
			spin_unlock_irqrestore(&tq->tq_lock, tq->tq_lock_flags);
			schedule();
			spin_lock_irqsave(&tq->tq_lock, tq->tq_lock_flags);
		} else {
			__set_current_state(TASK_RUNNING);
		}

		remove_wait_queue(&tq->tq_work_waitq, &wait);
                if (!list_empty(&tq->tq_pend_list)) {
                        t = list_entry(tq->tq_pend_list.next, spl_task_t, t_list);
                        list_del_init(&t->t_list);
			list_add_tail(&t->t_list, &tq->tq_work_list);
                        tq->tq_nactive++;
			spin_unlock_irqrestore(&tq->tq_lock, tq->tq_lock_flags);

			/* Perform the requested task */
                        t->t_func(t->t_arg);

			spin_lock_irqsave(&tq->tq_lock, tq->tq_lock_flags);
                        tq->tq_nactive--;
			id = t->t_id;
                        task_done(tq, t);

			/* Update the lowest remaining taskqid yet to run */
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

	RETURN(0);
}

taskq_t *
__taskq_create(const char *name, int nthreads, pri_t pri,
               int minalloc, int maxalloc, uint_t flags)
{
        taskq_t *tq;
        struct task_struct *t;
        int rc = 0, i, j = 0;
        ENTRY;

        ASSERT(name != NULL);
        ASSERT(pri <= maxclsyspri);
        ASSERT(minalloc >= 0);
        ASSERT(maxalloc <= INT_MAX);
        ASSERT(!(flags & (TASKQ_CPR_SAFE | TASKQ_DYNAMIC))); /* Unsupported */

        tq = kmem_alloc(sizeof(*tq), KM_SLEEP);
        if (tq == NULL)
                RETURN(NULL);

        tq->tq_threads = kmem_alloc(nthreads * sizeof(t), KM_SLEEP);
        if (tq->tq_threads == NULL) {
                kmem_free(tq, sizeof(*tq));
                RETURN(NULL);
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

        RETURN(tq);
}
EXPORT_SYMBOL(__taskq_create);

void
__taskq_destroy(taskq_t *tq)
{
	spl_task_t *t;
	int i, nthreads;
	ENTRY;

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

        spin_unlock_irqrestore(&tq->tq_lock, tq->tq_lock_flags);
        kmem_free(tq->tq_threads, nthreads * sizeof(spl_task_t *));
        kmem_free(tq, sizeof(taskq_t));

	EXIT;
}
EXPORT_SYMBOL(__taskq_destroy);

int
spl_taskq_init(void)
{
        ENTRY;

        system_taskq = taskq_create("system_taskq", 64, minclsyspri, 4, 512,
                                    TASKQ_DYNAMIC | TASKQ_PREPOPULATE);
	if (system_taskq == NULL)
		RETURN(1);

        RETURN(0);
}

void
spl_taskq_fini(void)
{
        ENTRY;
	taskq_destroy(system_taskq);
        EXIT;
}
