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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/ck.h>
#include <sys/epoch.h>
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

#if __FreeBSD_version < 1201522
#define	taskqueue_start_threads_in_proc(tqp, count, pri, proc, name, ...) \
    taskqueue_start_threads(tqp, count, pri, name, __VA_ARGS__)
#endif

static uint_t taskq_tsd;
static uma_zone_t taskq_zone;

taskq_t *system_taskq = NULL;
taskq_t *system_delay_taskq = NULL;
taskq_t *dynamic_taskq = NULL;

proc_t *system_proc;

extern int uma_align_cache;

static MALLOC_DEFINE(M_TASKQ, "taskq", "taskq structures");

static CK_LIST_HEAD(tqenthashhead, taskq_ent) *tqenthashtbl;
static unsigned long tqenthash;
static unsigned long tqenthashlock;
static struct sx *tqenthashtbl_lock;

static taskqid_t tqidnext;

#define	TQIDHASH(tqid) (&tqenthashtbl[(tqid) & tqenthash])
#define	TQIDHASHLOCK(tqid) (&tqenthashtbl_lock[((tqid) & tqenthashlock)])

#define	TIMEOUT_TASK 1
#define	NORMAL_TASK 2

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
		VERIFY(CK_LIST_EMPTY(&tqenthashtbl[i]));
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

	sx_xlock(TQIDHASHLOCK(tqid));
	CK_LIST_FOREACH(ent, TQIDHASH(tqid), tqent_hash) {
		if (ent->tqent_id == tqid)
			break;
	}
	if (ent != NULL)
		refcount_acquire(&ent->tqent_rc);
	sx_xunlock(TQIDHASHLOCK(tqid));
	return (ent);
}

static taskqid_t
taskq_insert(taskq_ent_t *ent)
{
	taskqid_t tqid;

	tqid = __taskq_genid();
	ent->tqent_id = tqid;
	ent->tqent_registered = B_TRUE;
	sx_xlock(TQIDHASHLOCK(tqid));
	CK_LIST_INSERT_HEAD(TQIDHASH(tqid), ent, tqent_hash);
	sx_xunlock(TQIDHASHLOCK(tqid));
	return (tqid);
}

static void
taskq_remove(taskq_ent_t *ent)
{
	taskqid_t tqid = ent->tqent_id;

	if (!ent->tqent_registered)
		return;

	sx_xlock(TQIDHASHLOCK(tqid));
	CK_LIST_REMOVE(ent, tqent_hash);
	sx_xunlock(TQIDHASHLOCK(tqid));
	ent->tqent_registered = B_FALSE;
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

	if (tid == 0)
		return (0);

	if ((ent = taskq_lookup(tid)) == NULL)
		return (0);

	ent->tqent_cancelled = B_TRUE;
	if (ent->tqent_type == TIMEOUT_TASK) {
		rc = taskqueue_cancel_timeout(tq->tq_queue,
		    &ent->tqent_timeout_task, &pend);
	} else
		rc = taskqueue_cancel(tq->tq_queue, &ent->tqent_task, &pend);
	if (rc == EBUSY) {
		taskqueue_drain(tq->tq_queue, &ent->tqent_task);
	} else if (pend) {
		/*
		 * Tasks normally free themselves when run, but here the task
		 * was cancelled so it did not free itself.
		 */
		taskq_free(ent);
	}
	/* Free the extra reference we added with taskq_lookup. */
	taskq_free(ent);
	return (rc);
}

static void
taskq_run(void *arg, int pending __unused)
{
	taskq_ent_t *task = arg;

	if (!task->tqent_cancelled)
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
	task->tqent_cancelled = B_FALSE;
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
	task->tqent_cancelled = B_FALSE;
	task->tqent_type = NORMAL_TASK;
	tqid = taskq_insert(task);
	TASK_INIT(&task->tqent_task, prio, taskq_run, task);
	taskqueue_enqueue(tq->tq_queue, &task->tqent_task);
	return (tqid);
}

static void
taskq_run_ent(void *arg, int pending __unused)
{
	taskq_ent_t *task = arg;

	task->tqent_func(task->tqent_arg);
}

void
taskq_dispatch_ent(taskq_t *tq, task_func_t func, void *arg, uint32_t flags,
    taskq_ent_t *task)
{
	int prio;

	/*
	 * If TQ_FRONT is given, we want higher priority for this task, so it
	 * can go at the front of the queue.
	 */
	prio = !!(flags & TQ_FRONT);
	task->tqent_cancelled = B_FALSE;
	task->tqent_registered = B_FALSE;
	task->tqent_id = 0;
	task->tqent_func = func;
	task->tqent_arg = arg;

	TASK_INIT(&task->tqent_task, prio, taskq_run_ent, task);
	taskqueue_enqueue(tq->tq_queue, &task->tqent_task);
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

	if (tid == 0)
		return;
	if ((ent = taskq_lookup(tid)) == NULL)
		return;

	taskqueue_drain(tq->tq_queue, &ent->tqent_task);
	taskq_free(ent);
}

void
taskq_wait_outstanding(taskq_t *tq, taskqid_t id __unused)
{
	taskqueue_drain_all(tq->tq_queue);
}

int
taskq_empty_ent(taskq_ent_t *t)
{
	return (t->tqent_task.ta_pending == 0);
}
