// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2009 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * All rights reserved.
 *
 * Copyright (c) 2012 Spectra Logic Corporation.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/kmem.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/taskq.h>
#include <sys/taskqueue.h>
#include <sys/zfs_context.h>

#if defined(__i386__) || defined(__amd64__) || defined(__aarch64__)
#include <machine/pcb.h>
#endif

#include <vm/uma.h>

static uint_t taskq_tsd;
static uma_zone_t taskq_zone;

/*
 * Global system-wide dynamic task queue available for all consumers. This
 * taskq is not intended for long-running tasks; instead, a dedicated taskq
 * should be created.
 */
taskq_t *system_taskq = NULL;
taskq_t *system_delay_taskq = NULL;
taskq_t *dynamic_taskq = NULL;

proc_t *system_proc;

static MALLOC_DEFINE(M_TASKQ, "taskq", "taskq structures");

static LIST_HEAD(tqenthashhead, taskq_ent) *tqenthashtbl;
static unsigned long tqenthash;
static unsigned long tqenthashlock;
static struct sx *tqenthashtbl_lock;

static taskqid_t tqidnext;

#define	TQIDHASH(tqid) (&tqenthashtbl[(tqid) & tqenthash])
#define	TQIDHASHLOCK(tqid) (&tqenthashtbl_lock[((tqid) & tqenthashlock)])

#define	NORMAL_TASK 0
#define	TIMEOUT_TASK 1

static void
system_taskq_init(void *arg)
{
	int i;

	tsd_create(&taskq_tsd, NULL);
	tqenthashtbl = hashinit(mp_ncpus * 8, M_TASKQ, &tqenthash);
	tqenthashlock = (tqenthash + 1) / 8;
	if (tqenthashlock > 0)
		tqenthashlock--;
	tqenthashtbl_lock =
	    malloc(sizeof (*tqenthashtbl_lock) * (tqenthashlock + 1),
	    M_TASKQ, M_WAITOK | M_ZERO);
	for (i = 0; i < tqenthashlock + 1; i++)
		sx_init_flags(&tqenthashtbl_lock[i], "tqenthash", SX_DUPOK);
	taskq_zone = uma_zcreate("taskq_zone", sizeof (taskq_ent_t),
	    NULL, NULL, NULL, NULL,
	    UMA_ALIGN_CACHE, 0);
	system_taskq = taskq_create("system_taskq", mp_ncpus, minclsyspri,
	    0, 0, 0);
	system_delay_taskq = taskq_create("system_delay_taskq", mp_ncpus,
	    minclsyspri, 0, 0, 0);
}
SYSINIT(system_taskq_init, SI_SUB_CONFIGURE, SI_ORDER_ANY, system_taskq_init,
    NULL);

static void
system_taskq_fini(void *arg)
{
	int i;

	taskq_destroy(system_delay_taskq);
	taskq_destroy(system_taskq);
	uma_zdestroy(taskq_zone);
	tsd_destroy(&taskq_tsd);
	for (i = 0; i < tqenthashlock + 1; i++)
		sx_destroy(&tqenthashtbl_lock[i]);
	for (i = 0; i < tqenthash + 1; i++)
		VERIFY(LIST_EMPTY(&tqenthashtbl[i]));
	free(tqenthashtbl_lock, M_TASKQ);
	free(tqenthashtbl, M_TASKQ);
}
SYSUNINIT(system_taskq_fini, SI_SUB_CONFIGURE, SI_ORDER_ANY, system_taskq_fini,
    NULL);

#ifdef __LP64__
static taskqid_t
__taskq_genid(void)
{
	taskqid_t tqid;

	/*
	 * Assume a 64-bit counter will not wrap in practice.
	 */
	tqid = atomic_add_64_nv(&tqidnext, 1);
	VERIFY(tqid);
	return (tqid);
}
#else
static taskqid_t
__taskq_genid(void)
{
	taskqid_t tqid;

	for (;;) {
		tqid = atomic_add_32_nv(&tqidnext, 1);
		if (__predict_true(tqid != 0))
			break;
	}
	VERIFY(tqid);
	return (tqid);
}
#endif

static taskq_ent_t *
taskq_lookup(taskqid_t tqid)
{
	taskq_ent_t *ent = NULL;

	if (tqid == 0)
		return (NULL);
	sx_slock(TQIDHASHLOCK(tqid));
	LIST_FOREACH(ent, TQIDHASH(tqid), tqent_hash) {
		if (ent->tqent_id == tqid)
			break;
	}
	if (ent != NULL)
		refcount_acquire(&ent->tqent_rc);
	sx_sunlock(TQIDHASHLOCK(tqid));
	return (ent);
}

static taskqid_t
taskq_insert(taskq_ent_t *ent)
{
	taskqid_t tqid = __taskq_genid();

	ent->tqent_id = tqid;
	sx_xlock(TQIDHASHLOCK(tqid));
	LIST_INSERT_HEAD(TQIDHASH(tqid), ent, tqent_hash);
	sx_xunlock(TQIDHASHLOCK(tqid));
	return (tqid);
}

static void
taskq_remove(taskq_ent_t *ent)
{
	taskqid_t tqid = ent->tqent_id;

	if (tqid == 0)
		return;
	sx_xlock(TQIDHASHLOCK(tqid));
	if (ent->tqent_id != 0) {
		LIST_REMOVE(ent, tqent_hash);
		ent->tqent_id = 0;
	}
	sx_xunlock(TQIDHASHLOCK(tqid));
}

static void
taskq_tsd_set(void *context)
{
	taskq_t *tq = context;

#if defined(__amd64__) || defined(__i386__) || defined(__aarch64__)
	if (context != NULL && tsd_get(taskq_tsd) == NULL)
		fpu_kern_thread(FPU_KERN_NORMAL);
#endif
	tsd_set(taskq_tsd, tq);
}

static taskq_t *
taskq_create_impl(const char *name, int nthreads, pri_t pri,
    proc_t *proc __maybe_unused, uint_t flags)
{
	taskq_t *tq;

	if ((flags & TASKQ_THREADS_CPU_PCT) != 0)
		nthreads = MAX((mp_ncpus * nthreads) / 100, 1);

	tq = kmem_alloc(sizeof (*tq), KM_SLEEP);
	tq->tq_nthreads = nthreads;
	tq->tq_queue = taskqueue_create(name, M_WAITOK,
	    taskqueue_thread_enqueue, &tq->tq_queue);
	taskqueue_set_callback(tq->tq_queue, TASKQUEUE_CALLBACK_TYPE_INIT,
	    taskq_tsd_set, tq);
	taskqueue_set_callback(tq->tq_queue, TASKQUEUE_CALLBACK_TYPE_SHUTDOWN,
	    taskq_tsd_set, NULL);
	(void) taskqueue_start_threads_in_proc(&tq->tq_queue, nthreads, pri,
	    proc, "%s", name);

	return ((taskq_t *)tq);
}

taskq_t *
taskq_create(const char *name, int nthreads, pri_t pri, int minalloc __unused,
    int maxalloc __unused, uint_t flags)
{
	return (taskq_create_impl(name, nthreads, pri, system_proc, flags));
}

taskq_t *
taskq_create_proc(const char *name, int nthreads, pri_t pri,
    int minalloc __unused, int maxalloc __unused, proc_t *proc, uint_t flags)
{
	return (taskq_create_impl(name, nthreads, pri, proc, flags));
}

void
taskq_destroy(taskq_t *tq)
{

	taskqueue_free(tq->tq_queue);
	kmem_free(tq, sizeof (*tq));
}

static void taskq_sync_assign(void *arg);

typedef struct taskq_sync_arg {
	kthread_t	*tqa_thread;
	kcondvar_t	tqa_cv;
	kmutex_t 	tqa_lock;
	int		tqa_ready;
} taskq_sync_arg_t;

static void
taskq_sync_assign(void *arg)
{
	taskq_sync_arg_t *tqa = arg;

	mutex_enter(&tqa->tqa_lock);
	tqa->tqa_thread = curthread;
	tqa->tqa_ready = 1;
	cv_signal(&tqa->tqa_cv);
	while (tqa->tqa_ready == 1)
		cv_wait(&tqa->tqa_cv, &tqa->tqa_lock);
	mutex_exit(&tqa->tqa_lock);
}

/*
 * Create a taskq with a specified number of pool threads. Allocate
 * and return an array of nthreads kthread_t pointers, one for each
 * thread in the pool. The array is not ordered and must be freed
 * by the caller.
 */
taskq_t *
taskq_create_synced(const char *name, int nthreads, pri_t pri,
    int minalloc, int maxalloc, uint_t flags, kthread_t ***ktpp)
{
	taskq_t *tq;
	taskq_sync_arg_t *tqs = kmem_zalloc(sizeof (*tqs) * nthreads, KM_SLEEP);
	kthread_t **kthreads = kmem_zalloc(sizeof (*kthreads) * nthreads,
	    KM_SLEEP);

	flags &= ~(TASKQ_DYNAMIC | TASKQ_THREADS_CPU_PCT | TASKQ_DC_BATCH);

	tq = taskq_create(name, nthreads, minclsyspri, nthreads, INT_MAX,
	    flags | TASKQ_PREPOPULATE);
	VERIFY(tq != NULL);
	VERIFY(tq->tq_nthreads == nthreads);

	/* spawn all syncthreads */
	for (int i = 0; i < nthreads; i++) {
		cv_init(&tqs[i].tqa_cv, NULL, CV_DEFAULT, NULL);
		mutex_init(&tqs[i].tqa_lock, NULL, MUTEX_DEFAULT, NULL);
		(void) taskq_dispatch(tq, taskq_sync_assign,
		    &tqs[i], TQ_FRONT);
	}

	/* wait on all syncthreads to start */
	for (int i = 0; i < nthreads; i++) {
		mutex_enter(&tqs[i].tqa_lock);
		while (tqs[i].tqa_ready == 0)
			cv_wait(&tqs[i].tqa_cv, &tqs[i].tqa_lock);
		mutex_exit(&tqs[i].tqa_lock);
	}

	/* let all syncthreads resume, finish */
	for (int i = 0; i < nthreads; i++) {
		mutex_enter(&tqs[i].tqa_lock);
		tqs[i].tqa_ready = 2;
		cv_broadcast(&tqs[i].tqa_cv);
		mutex_exit(&tqs[i].tqa_lock);
	}
	taskq_wait(tq);

	for (int i = 0; i < nthreads; i++) {
		kthreads[i] = tqs[i].tqa_thread;
		mutex_destroy(&tqs[i].tqa_lock);
		cv_destroy(&tqs[i].tqa_cv);
	}
	kmem_free(tqs, sizeof (*tqs) * nthreads);

	*ktpp = kthreads;
	return (tq);
}

int
taskq_member(taskq_t *tq, kthread_t *thread)
{

	return (taskqueue_member(tq->tq_queue, thread));
}

taskq_t *
taskq_of_curthread(void)
{
	return (tsd_get(taskq_tsd));
}

static void
taskq_free(taskq_ent_t *task)
{
	taskq_remove(task);
	if (refcount_release(&task->tqent_rc))
		uma_zfree(taskq_zone, task);
}

int
taskq_cancel_id(taskq_t *tq, taskqid_t tid)
{
	uint32_t pend;
	int rc;
	taskq_ent_t *ent;

	if ((ent = taskq_lookup(tid)) == NULL)
		return (ENOENT);

	if (ent->tqent_type == NORMAL_TASK) {
		rc = taskqueue_cancel(tq->tq_queue, &ent->tqent_task, &pend);
		if (rc == EBUSY)
			taskqueue_drain(tq->tq_queue, &ent->tqent_task);
	} else {
		rc = taskqueue_cancel_timeout(tq->tq_queue,
		    &ent->tqent_timeout_task, &pend);
		if (rc == EBUSY) {
			taskqueue_drain_timeout(tq->tq_queue,
			    &ent->tqent_timeout_task);
		}
	}
	if (pend) {
		/*
		 * Tasks normally free themselves when run, but here the task
		 * was cancelled so it did not free itself.
		 */
		taskq_free(ent);
	}
	/* Free the extra reference we added with taskq_lookup. */
	taskq_free(ent);
	return (pend ? 0 : ENOENT);
}

static void
taskq_run(void *arg, int pending)
{
	taskq_ent_t *task = arg;

	if (pending == 0)
		return;
	task->tqent_func(task->tqent_arg);
	taskq_free(task);
}

taskqid_t
taskq_dispatch_delay(taskq_t *tq, task_func_t func, void *arg,
    uint_t flags, clock_t expire_time)
{
	taskq_ent_t *task;
	taskqid_t tqid;
	clock_t timo;
	int mflag;

	timo = expire_time - ddi_get_lbolt();
	if (timo <= 0)
		return (taskq_dispatch(tq, func, arg, flags));

	if ((flags & (TQ_SLEEP | TQ_NOQUEUE)) == TQ_SLEEP)
		mflag = M_WAITOK;
	else
		mflag = M_NOWAIT;

	task = uma_zalloc(taskq_zone, mflag);
	if (task == NULL)
		return (0);
	task->tqent_func = func;
	task->tqent_arg = arg;
	task->tqent_type = TIMEOUT_TASK;
	refcount_init(&task->tqent_rc, 1);
	tqid = taskq_insert(task);
	TIMEOUT_TASK_INIT(tq->tq_queue, &task->tqent_timeout_task, 0,
	    taskq_run, task);

	taskqueue_enqueue_timeout(tq->tq_queue, &task->tqent_timeout_task,
	    timo);
	return (tqid);
}

taskqid_t
taskq_dispatch(taskq_t *tq, task_func_t func, void *arg, uint_t flags)
{
	taskq_ent_t *task;
	int mflag, prio;
	taskqid_t tqid;

	if ((flags & (TQ_SLEEP | TQ_NOQUEUE)) == TQ_SLEEP)
		mflag = M_WAITOK;
	else
		mflag = M_NOWAIT;
	/*
	 * If TQ_FRONT is given, we want higher priority for this task, so it
	 * can go at the front of the queue.
	 */
	prio = !!(flags & TQ_FRONT);

	task = uma_zalloc(taskq_zone, mflag);
	if (task == NULL)
		return (0);
	refcount_init(&task->tqent_rc, 1);
	task->tqent_func = func;
	task->tqent_arg = arg;
	task->tqent_type = NORMAL_TASK;
	tqid = taskq_insert(task);
	TASK_INIT(&task->tqent_task, prio, taskq_run, task);
	taskqueue_enqueue(tq->tq_queue, &task->tqent_task);
	return (tqid);
}

static void
taskq_run_ent(void *arg, int pending)
{
	taskq_ent_t *task = arg;

	if (pending == 0)
		return;
	task->tqent_func(task->tqent_arg);
}

void
taskq_dispatch_ent(taskq_t *tq, task_func_t func, void *arg, uint32_t flags,
    taskq_ent_t *task)
{
	/*
	 * If TQ_FRONT is given, we want higher priority for this task, so it
	 * can go at the front of the queue.
	 */
	task->tqent_task.ta_priority = !!(flags & TQ_FRONT);
	task->tqent_func = func;
	task->tqent_arg = arg;
	taskqueue_enqueue(tq->tq_queue, &task->tqent_task);
}

void
taskq_init_ent(taskq_ent_t *task)
{
	TASK_INIT(&task->tqent_task, 0, taskq_run_ent, task);
	task->tqent_func = NULL;
	task->tqent_arg = NULL;
	task->tqent_id = 0;
	task->tqent_type = NORMAL_TASK;
	task->tqent_rc = 0;
}

int
taskq_empty_ent(taskq_ent_t *task)
{
	return (task->tqent_task.ta_pending == 0);
}

void
taskq_wait(taskq_t *tq)
{
	taskqueue_quiesce(tq->tq_queue);
}

void
taskq_wait_id(taskq_t *tq, taskqid_t tid)
{
	taskq_ent_t *ent;

	if ((ent = taskq_lookup(tid)) == NULL)
		return;

	if (ent->tqent_type == NORMAL_TASK)
		taskqueue_drain(tq->tq_queue, &ent->tqent_task);
	else
		taskqueue_drain_timeout(tq->tq_queue, &ent->tqent_timeout_task);
	taskq_free(ent);
}

void
taskq_wait_outstanding(taskq_t *tq, taskqid_t id __unused)
{
	taskqueue_drain_all(tq->tq_queue);
}
