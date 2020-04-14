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
#include <sys/kernel.h>
#include <sys/kmem.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/taskqueue.h>
#include <sys/taskq.h>
#include <sys/zfs_context.h>

#include <vm/uma.h>

static uint_t taskq_tsd;
static uma_zone_t taskq_zone;

taskq_t *system_taskq = NULL;
taskq_t *system_delay_taskq = NULL;
taskq_t *dynamic_taskq = NULL;

extern int uma_align_cache;

#define	TQ_MASK uma_align_cache
#define	TQ_PTR_MASK ~uma_align_cache

#define	TIMEOUT_TASK 1
#define	NORMAL_TASK 2

static int
taskqent_init(void *mem, int size, int flags)
{
	bzero(mem, sizeof (taskq_ent_t));
	return (0);
}

static int
taskqent_ctor(void *mem, int size, void *arg, int flags)
{
	return (0);
}

static void
taskqent_dtor(void *mem, int size, void *arg)
{
	taskq_ent_t *ent = mem;

	ent->tqent_gen = (ent->tqent_gen + 1) & TQ_MASK;
}

static void
system_taskq_init(void *arg)
{

	tsd_create(&taskq_tsd, NULL);
	taskq_zone = uma_zcreate("taskq_zone", sizeof (taskq_ent_t),
	    taskqent_ctor, taskqent_dtor, taskqent_init, NULL,
	    UMA_ALIGN_CACHE, UMA_ZONE_NOFREE);
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

	taskq_destroy(system_taskq);
	uma_zdestroy(taskq_zone);
	tsd_destroy(&taskq_tsd);
}
SYSUNINIT(system_taskq_fini, SI_SUB_CONFIGURE, SI_ORDER_ANY, system_taskq_fini,
    NULL);

static void
taskq_tsd_set(void *context)
{
	taskq_t *tq = context;

	tsd_set(taskq_tsd, tq);
}

static taskq_t *
taskq_create_with_init(const char *name, int nthreads, pri_t pri,
    int minalloc __unused, int maxalloc __unused, uint_t flags)
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
	(void) taskqueue_start_threads(&tq->tq_queue, nthreads, pri,
	    "%s", name);

	return ((taskq_t *)tq);
}

taskq_t *
taskq_create(const char *name, int nthreads, pri_t pri, int minalloc __unused,
    int maxalloc __unused, uint_t flags)
{

	return (taskq_create_with_init(name, nthreads, pri, minalloc, maxalloc,
	    flags));
}

taskq_t *
taskq_create_proc(const char *name, int nthreads, pri_t pri, int minalloc,
    int maxalloc, proc_t *proc __unused, uint_t flags)
{

	return (taskq_create_with_init(name, nthreads, pri, minalloc, maxalloc,
	    flags));
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

int
taskq_cancel_id(taskq_t *tq, taskqid_t tid)
{
	uint32_t pend;
	int rc;
	taskq_ent_t *ent = (void*)(tid & TQ_PTR_MASK);

	if (ent == NULL)
		return (0);
	if ((tid & TQ_MASK) != ent->tqent_gen)
		return (0);
	if (ent->tqent_type == TIMEOUT_TASK) {
		rc = taskqueue_cancel_timeout(tq->tq_queue,
		    &ent->tqent_timeout_task, &pend);
	} else
		rc = taskqueue_cancel(tq->tq_queue, &ent->tqent_task, &pend);
	if (rc == EBUSY)
		taskq_wait_id(tq, tid);
	else
		uma_zfree(taskq_zone, ent);
	return (rc);
}

static void
taskq_run(void *arg, int pending __unused)
{
	taskq_ent_t *task = arg;

	task->tqent_func(task->tqent_arg);
	uma_zfree(taskq_zone, task);
}

taskqid_t
taskq_dispatch_delay(taskq_t *tq, task_func_t func, void *arg,
    uint_t flags, clock_t expire_time)
{
	taskq_ent_t *task;
	taskqid_t tid;
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
	tid = (uintptr_t)task;
	MPASS((tid & TQ_MASK) == 0);
	task->tqent_func = func;
	task->tqent_arg = arg;
	task->tqent_type = TIMEOUT_TASK;
	tid |= task->tqent_gen;
	TIMEOUT_TASK_INIT(tq->tq_queue, &task->tqent_timeout_task, 0,
	    taskq_run, task);

	taskqueue_enqueue_timeout(tq->tq_queue, &task->tqent_timeout_task,
	    timo);
	return (tid);
}

taskqid_t
taskq_dispatch(taskq_t *tq, task_func_t func, void *arg, uint_t flags)
{
	taskq_ent_t *task;
	int mflag, prio;
	taskqid_t tid;

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

	tid = (uintptr_t)task;
	MPASS((tid & TQ_MASK) == 0);
	task->tqent_func = func;
	task->tqent_arg = arg;
	task->tqent_type = NORMAL_TASK;
	TASK_INIT(&task->tqent_task, prio, taskq_run, task);
	tid |= task->tqent_gen;
	taskqueue_enqueue(tq->tq_queue, &task->tqent_task);
	return (tid);
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
	taskq_ent_t *ent = (void*)(tid & TQ_PTR_MASK);

	if ((tid & TQ_MASK) != ent->tqent_gen)
		return;

	taskqueue_drain(tq->tq_queue, &ent->tqent_task);
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
