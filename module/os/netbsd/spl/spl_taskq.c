// SPDX-License-Identifier: BSD-2-Clause
/*
 * NetBSD taskq: kthread workers + condvar queue.
 *
 * Workers are real kthreads.  taskq_destroy sets tq_destroyed and
 * kthread_exit's them, same as Linux/FreeBSD.  Parking a single
 * worker forever serializes ZIO onto one LWP; amap_wipeout is
 * handled in spl_misc.c.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/proc.h>
#include <sys/kthread.h>
#include <sys/taskq.h>
#include <sys/atomic.h>
#include <sys/taskqueue.h>
#include <sys/cpu.h>
#include <sys/workqueue.h>
#include <sys/lwp.h>
#include <sys/rmlock.h>

struct taskq {
	char		tq_name[TASKQ_NAMELEN + 1];
	int		tq_nthreads;
	int		tq_nalloc;
	int		tq_minthreads;
	int		tq_nspawn;
	clock_t		tq_lastspawn;
	pri_t		tq_pri;
	uint_t		tq_flags;
	int		tq_active;
	int		tq_nrunning;
	int		tq_nexec;
	int		tq_nstarted;
	volatile uint_t	tq_destroyed;
	volatile uint_t	tq_suspended;
	volatile uint_t	tq_closing;
	kmutex_t	tq_lock;
	kcondvar_t	tq_cv;
	kcondvar_t	tq_wait_cv;
	TAILQ_HEAD(, taskq_ent) tq_list;
	TAILQ_HEAD(, taskq_ent) tq_delay_list;
	kthread_t	**tq_threads;
	taskqid_t	*tq_exec;
	struct workqueue *tq_ipl_wq;
	volatile uint_t	tq_ipl_busy;
	volatile uint_t	tq_ipl_pending;
	volatile uint64_t tq_issued;
	struct work	tq_dwork;
};

static void	tq_issued_update(taskq_t *, taskqid_t);
#define	tq_issued_get(tq) \
	atomic_load_64(&(tq)->tq_issued)

taskq_t *system_taskq;
taskq_t *system_delay_taskq;
proc_t *system_proc;

static specificdata_key_t taskq_lwp_key;
specificdata_key_t spl_rms_lwp_key;
static volatile uint64_t tqid_next = 1;
static kmutex_t taskqueue_lock;
static kcondvar_t taskqueue_cv;
static struct workqueue *spl_tq_dwq;

static void	taskq_ready_insert(taskq_t *, taskq_ent_t *, int);
static void	taskq_maybe_spawn(taskq_t *);
taskq_t		*taskq_of_curthread(void);
taskq_t		*taskq_of_curthread(void);

static struct workqueue *spl_tq_ipl_fb;

static void
taskq_ipl_work(struct work *wk, void *arg)
{
	taskq_ent_t *tqe;
	taskq_t *tq;
	task_func_t *func;
	void *farg;

	tqe = (taskq_ent_t *)((char *)wk - offsetof(taskq_ent_t, tqent_work));
	tq = tqe->tqent_tq;
	if (tq == NULL)
		tq = arg;
	ASSERT(tq != NULL);
	mutex_enter(&tq->tq_lock);
	if (tq->tq_destroyed) {
		func = tqe->tqent_func;
		farg = tqe->tqent_arg;
		(void) atomic_swap_uint(&tqe->tqent_state, TQENT_IDLE);
		cv_broadcast(&tq->tq_wait_cv);
		/*
		 * Last tq mutation while tq_lock is held.  Destroy
		 * polls ipl_pending without the lock; dropping it
		 * here means it cannot kmem_free(tq) until this
		 * worker is done with tq_lock.
		 */
		atomic_dec_uint((unsigned int *)&tq->tq_ipl_pending);
		mutex_exit(&tq->tq_lock);
		if (func != NULL)
			(*func)(farg);
		return;
	}
	atomic_dec_uint((unsigned int *)&tq->tq_ipl_pending);
	taskq_ready_insert(tq, tqe, (tqe->tqent_flags & TQ_FRONT) != 0);
	cv_signal(&tq->tq_cv);
	cv_broadcast(&tq->tq_wait_cv);
	mutex_exit(&tq->tq_lock);
}

static void
taskq_ready_insert(taskq_t *tq, taskq_ent_t *tqe, int front)
{
	if (front)
		TAILQ_INSERT_HEAD(&tq->tq_list, tqe, tqent_list);
	else
		TAILQ_INSERT_TAIL(&tq->tq_list, tqe, tqent_list);
	tq->tq_active++;
}

static void
taskq_delay_insert(taskq_t *tq, taskq_ent_t *tqe)
{
	taskq_ent_t *cur, *prev = NULL;

	TAILQ_FOREACH(cur, &tq->tq_delay_list, tqent_list) {
		if (zfs_lbolt_delta(tqe->tqent_expire, cur->tqent_expire) < 0)
			break;
		prev = cur;
	}
	if (prev == NULL)
		TAILQ_INSERT_HEAD(&tq->tq_delay_list, tqe, tqent_list);
	else
		TAILQ_INSERT_AFTER(&tq->tq_delay_list, prev, tqe,
		    tqent_list);
}

static void
taskq_promote_delayed(taskq_t *tq)
{
	/*
	 * Expire times are absolute OpenZFS lbolt (ddi_get_lbolt /
	 * getnanouptime ticks), matching zio_delay_interrupt and
	 * cv_timedwait.  getticks() is hardclock_ticks and lags when
	 * the guest misses clock interrupts, so a compare against it
	 * leaves zinject-delayed ZIOs on the delay list.
	 */
	clock_t now = ddi_get_lbolt();
	taskq_ent_t *tqe;

	while ((tqe = TAILQ_FIRST(&tq->tq_delay_list)) != NULL &&
	    zfs_lbolt_delta(tqe->tqent_expire, now) <= 0) {
		TAILQ_REMOVE(&tq->tq_delay_list, tqe, tqent_list);
		taskq_ready_insert(tq, tqe, 0);
	}
}

static void
taskq_worker(void *arg)
{
	taskq_t *tq = arg;
	taskq_ent_t *tqe;
	task_func_t *func;
	void *farg;
	int dynamic;
	int extra;
	int myidx;
	int i;

	lwp_setspecific(taskq_lwp_key, tq);
	mutex_enter(&tq->tq_lock);
	/*
	 * In-flight kthread_create holds tq_nspawn until this LWP
	 * either claims a slot or commits to exit.  Destroy waits
	 * nspawn==0 so tq is still alive here.
	 */
	if (tq->tq_destroyed || tq->tq_closing) {
		if (tq->tq_nspawn > 0)
			tq->tq_nspawn--;
		cv_broadcast(&tq->tq_wait_cv);
		mutex_exit(&tq->tq_lock);
		lwp_setspecific(taskq_lwp_key, NULL);
		kthread_exit(0);
	}
	myidx = -1;
	extra = 0;
	for (i = 0; i < tq->tq_nalloc; i++) {
		if (tq->tq_threads[i] == NULL) {
			tq->tq_threads[i] = curlwp;
			myidx = i;
			extra = (tq->tq_nthreads >= tq->tq_minthreads);
			tq->tq_nthreads++;
			tq->tq_nrunning++;
			if (tq->tq_nspawn > 0)
				tq->tq_nspawn--;
			cv_broadcast(&tq->tq_wait_cv);
			break;
		}
	}
	if (myidx < 0) {
		if (tq->tq_nspawn > 0)
			tq->tq_nspawn--;
		cv_broadcast(&tq->tq_wait_cv);
		mutex_exit(&tq->tq_lock);
		lwp_setspecific(taskq_lwp_key, NULL);
		kthread_exit(0);
	}
	for (;;) {
		if (tq->tq_destroyed) {
			/*
			 * Teardown: run leftover delayed/ready work
			 * instead of exiting with queued ents.  A
			 * dispatch_ent that won the window after the
			 * last taskq_wait() and before tq_destroyed
			 * must still run, or the prealloc ent stays
			 * TQENT_QUEUED at a freed taskq.
			 */
			while ((tqe = TAILQ_FIRST(&tq->tq_delay_list)) !=
			    NULL) {
				TAILQ_REMOVE(&tq->tq_delay_list, tqe,
				    tqent_list);
				taskq_ready_insert(tq, tqe, 0);
			}
		} else {
			taskq_promote_delayed(tq);
		}
		if (TAILQ_EMPTY(&tq->tq_list) ||
		    (!tq->tq_destroyed && tq->tq_suspended)) {
			if (tq->tq_destroyed)
				break;
			if (ISSET(tq->tq_flags, TASKQ_DYNAMIC) && extra &&
			    tq->tq_nthreads > tq->tq_minthreads &&
			    tq->tq_nspawn == 0 &&
			    TAILQ_EMPTY(&tq->tq_delay_list) &&
			    zfs_lbolt_delta(getticks(), tq->tq_lastspawn) >
			    5 * hz) {
				tq->tq_threads[myidx] = NULL;
				tq->tq_nthreads--;
				tq->tq_nrunning--;
				tq->tq_lastspawn = getticks();
				cv_broadcast(&tq->tq_wait_cv);
				mutex_exit(&tq->tq_lock);
				lwp_setspecific(taskq_lwp_key, NULL);
				kthread_exit(0);
			}
			/*
			 * Sleep until a ready task arrives, or until the
			 * next delayed task is due.  cv_timedwait is the
			 * OpenZFS wrapper and takes an absolute lbolt.
			 */
			tqe = TAILQ_FIRST(&tq->tq_delay_list);
			if (!tq->tq_suspended && tqe != NULL) {
				(void) cv_timedwait(&tq->tq_cv, &tq->tq_lock,
				    tqe->tqent_expire);
			} else {
				cv_wait(&tq->tq_cv, &tq->tq_lock);
			}
			continue;
		}
		tqe = TAILQ_FIRST(&tq->tq_list);
		TAILQ_REMOVE(&tq->tq_list, tqe, tqent_list);
		/*
		 * Latch func/arg under the lock, then mark empty
		 * *before* the callback.  zio->io_tqent is
		 * re-dispatched from inside the file-vdev worker
		 * (vdev_file_io_strategy -> zio_interrupt ->
		 * spa_taskq_dispatch).  A "busy until func returns"
		 * bit drops that dispatch and hangs zio_wait.
		 * Writing tqent_func without a lock (dispatch_ent)
		 * would otherwise race this read.
		 */
		func = tqe->tqent_func;
		farg = tqe->tqent_arg;
		tqe->tqent_state = TQENT_IDLE;
		dynamic = tqe->tqent_dynamic;
		if (myidx >= 0 && myidx < tq->tq_nalloc)
			tq->tq_exec[myidx] = tqe->tqent_id;
		tq->tq_nexec++;
		mutex_exit(&tq->tq_lock);

		(*func)(farg);

		mutex_enter(&tq->tq_lock);
		tq->tq_nexec--;
		if (myidx >= 0 && myidx < tq->tq_nalloc)
			tq->tq_exec[myidx] = 0;
		if (dynamic)
			kmem_free(tqe, sizeof (*tqe));
		tq->tq_active--;
		cv_broadcast(&tq->tq_wait_cv);
		taskq_maybe_spawn(tq);
	}
	tq->tq_nrunning--;
	cv_broadcast(&tq->tq_wait_cv);
	mutex_exit(&tq->tq_lock);
	lwp_setspecific(taskq_lwp_key, NULL);
	kthread_exit(0);
}

static void
taskq_maybe_spawn(taskq_t *tq)
{
	int error;

	ASSERT(mutex_owned(&tq->tq_lock));
	if (!ISSET(tq->tq_flags, TASKQ_DYNAMIC))
		return;
	if (tq->tq_closing || tq->tq_destroyed)
		return;
	if (tq->tq_nthreads + tq->tq_nspawn >= tq->tq_nalloc)
		return;
	/*
	 * Grow when every live worker is executing, or when this
	 * LWP is a member (child ZIO queued to the same taskq).
	 */
	if (tq->tq_nexec < tq->tq_nthreads && taskq_of_curthread() != tq)
		return;
	tq->tq_nspawn++;
	tq->tq_lastspawn = getticks();
	mutex_exit(&tq->tq_lock);
	error = kthread_create(tq->tq_pri, KTHREAD_MPSAFE, NULL,
	    taskq_worker, tq, NULL, "%s/+", tq->tq_name);
	mutex_enter(&tq->tq_lock);
	if (error != 0) {
		tq->tq_nspawn--;
		cv_broadcast(&tq->tq_wait_cv);
		printf("ZFS: taskq spawn %s failed %d\n", tq->tq_name, error);
	}
}

static taskq_t *
taskq_create_impl(const char *name, int nthreads, pri_t pri, uint_t flags)
{
	taskq_t *tq;
	int i, error, minth, maxth, create_n;

	if (nthreads < 1)
		nthreads = 1;
	if (ISSET(flags, TASKQ_THREADS_CPU_PCT)) {
		int n = ncpu;

		if (n < 1)
			n = 1;
		nthreads = (n * nthreads) / 100;
		if (nthreads < 1)
			nthreads = 1;
	}
	minth = nthreads;
	maxth = nthreads;
	if (ISSET(flags, TASKQ_DYNAMIC)) {
		int n = ncpu;

		if (n < 1)
			n = 1;
		/*
		 * Linux TASKQ_DYNAMIC starts at one thread and grows
		 * on demand.  A 1-thread queue deadlocks when a child
		 * ZIO is queued to the same taskq and zio_wait's it,
		 * so keep a floor of MAX(ncpu, 2) live threads and
		 * spawn up to MAX(ncpu*4, 8, requested).
		 */
		minth = MAX(n, 2);
		maxth = MAX(n * 4, 8);
		if (nthreads > maxth)
			maxth = nthreads;
		if (minth > maxth)
			minth = maxth;
	}

	tq = kmem_zalloc(sizeof (*tq), KM_SLEEP);
	strlcpy(tq->tq_name, name, sizeof (tq->tq_name));
	tq->tq_nalloc = maxth;
	tq->tq_minthreads = minth;
	tq->tq_nthreads = 0;
	tq->tq_pri = pri;
	tq->tq_flags = flags;
	mutex_init(&tq->tq_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&tq->tq_cv, "taskq", CV_DEFAULT, NULL);
	cv_init(&tq->tq_wait_cv, "taskqwait", CV_DEFAULT, NULL);
	TAILQ_INIT(&tq->tq_list);
	TAILQ_INIT(&tq->tq_delay_list);
	tq->tq_threads = kmem_zalloc(sizeof (kthread_t *) * maxth, KM_SLEEP);
	tq->tq_exec = kmem_zalloc(sizeof (taskqid_t) * maxth, KM_SLEEP);

	create_n = 0;
	for (i = 0; i < minth; i++) {
		error = kthread_create(pri, KTHREAD_MPSAFE, NULL,
		    taskq_worker, tq, NULL, "%s/%d", name, i);
		if (error != 0) {
			printf("ZFS: kthread_create %s/%d failed %d\n",
			    name, i, error);
			if (i == 0) {
				kmem_free(tq->tq_exec,
				    sizeof (taskqid_t) * maxth);
				kmem_free(tq->tq_threads,
				    sizeof (kthread_t *) * maxth);
				cv_destroy(&tq->tq_cv);
				cv_destroy(&tq->tq_wait_cv);
				mutex_destroy(&tq->tq_lock);
				kmem_free(tq, sizeof (*tq));
				return (NULL);
			}
			break;
		}
		create_n++;
	}
	mutex_enter(&tq->tq_lock);
	while (tq->tq_nthreads < create_n)
		cv_wait(&tq->tq_wait_cv, &tq->tq_lock);
	mutex_exit(&tq->tq_lock);
	/*
	 * IPL-safe hop: biodone must not take the adaptive tq_lock.
	 * WQ_PERCPU so completions are not serialized on one worker.
	 */
	error = workqueue_create(&tq->tq_ipl_wq, name, taskq_ipl_work,
	    tq, pri, IPL_VM, WQ_MPSAFE | WQ_PERCPU);
	if (error != 0) {
		printf("ZFS: workqueue_create %s failed %d\n", name, error);
		mutex_enter(&tq->tq_lock);
		tq->tq_destroyed = 1;
		cv_broadcast(&tq->tq_cv);
		while (tq->tq_nrunning > 0)
			cv_wait(&tq->tq_wait_cv, &tq->tq_lock);
		mutex_exit(&tq->tq_lock);
		if (tq->tq_exec != NULL)
			kmem_free(tq->tq_exec, sizeof (taskqid_t) * maxth);
		if (tq->tq_threads != NULL)
			kmem_free(tq->tq_threads, sizeof (kthread_t *) *
			    maxth);
		cv_destroy(&tq->tq_cv);
		cv_destroy(&tq->tq_wait_cv);
		mutex_destroy(&tq->tq_lock);
		kmem_free(tq, sizeof (*tq));
		return (NULL);
	}
	return (tq);
}

taskq_t *
taskq_create(const char *name, int nthreads, pri_t pri, int minalloc,
    int maxalloc, uint_t flags)
{
	(void) minalloc;
	(void) maxalloc;
	return (taskq_create_impl(name, nthreads, pri, flags));
}

taskq_t *
taskq_create_instance(const char *name, int instance, int nthreads, pri_t pri,
    int minalloc, int maxalloc, uint_t flags)
{
	char buf[TASKQ_NAMELEN + 1];

	(void) snprintf(buf, sizeof (buf), "%s_%d", name, instance);
	return (taskq_create(buf, nthreads, pri, minalloc, maxalloc, flags));
}

taskq_t *
taskq_create_proc(const char *name, int nthreads, pri_t pri, int minalloc,
    int maxalloc, struct proc *pp, uint_t flags)
{
	(void) pp;
	return (taskq_create(name, nthreads, pri, minalloc, maxalloc, flags));
}

taskq_t *
taskq_create_sysdc(const char *name, int nthreads, int minalloc, int maxalloc,
    struct proc *pp, uint_t dc, uint_t flags)
{
	(void) dc;
	return (taskq_create_proc(name, nthreads, maxclsyspri, minalloc,
	    maxalloc, pp, flags));
}

taskq_t *
taskq_create_synced(const char *name, int nthreads, pri_t pri, int minalloc,
    int maxalloc, uint_t flags, kthread_t ***tpp)
{
	taskq_t *tq;
	kthread_t **kthreads;
	int n, copy;

	tq = taskq_create(name, nthreads, pri, minalloc, maxalloc, flags);
	if (tq == NULL)
		return (NULL);
	if (nthreads > 0 && tq->tq_nthreads < tq->tq_minthreads) {
		taskq_destroy(tq);
		return (NULL);
	}
	if (tpp != NULL) {
		/* Caller frees this with the requested nthreads. */
		n = (nthreads < 1) ? 1 : nthreads;
		kthreads = kmem_zalloc(sizeof (kthread_t *) * n, KM_SLEEP);
		copy = tq->tq_nthreads;
		if (copy > n)
			copy = n;
		if (tq->tq_threads != NULL && copy > 0)
			memcpy(kthreads, tq->tq_threads,
			    sizeof (kthread_t *) * copy);
		*tpp = kthreads;
	}
	return (tq);
}

static void
taskq_destroy_later(void *arg)
{
	taskq_destroy(arg);
}

static void
taskq_destroy_kthread(void *arg)
{
	taskq_destroy(arg);
	kthread_exit(0);
}

static void
taskq_destroy_work(struct work *wk, void *arg)
{
	taskq_t *tq;

	(void) arg;
	tq = (taskq_t *)((char *)wk - offsetof(taskq_t, tq_dwork));
	taskq_destroy(tq);
}

void
taskq_destroy(taskq_t *tq)
{
	taskq_ent_t *tqe;
	int nthreads, error;

	if (tq == NULL)
		return;
	if (taskq_of_curthread() == tq) {
		/*
		 * Never wait for our own workers.  Prefer the
		 * module-lifetime IPL_NONE workqueue, then
		 * system_taskq, then a one-shot kthread.  Returning
		 * with only tq_closing set leaked the taskq: workers
		 * wait on tq_destroyed, not tq_closing.
		 */
		if (spl_tq_dwq != NULL) {
			workqueue_enqueue(spl_tq_dwq, &tq->tq_dwork, NULL);
			return;
		}
		if (system_taskq != NULL && tq != system_taskq &&
		    taskq_dispatch(system_taskq, taskq_destroy_later, tq,
		    TQ_NOSLEEP) != TASKQID_INVALID)
			return;
		error = kthread_create(PRI_NONE, KTHREAD_MPSAFE, NULL,
		    taskq_destroy_kthread, tq, NULL, "zfstqdest");
		if (error != 0) {
			panic("ZFS: taskq_destroy(%s) from own worker, "
			    "no handoff (%d)", tq->tq_name, error);
		}
		return;
	}
	mutex_enter(&tq->tq_lock);
	tq->tq_closing = 1;
	tq->tq_suspended = 0;
	membar_sync();
	while ((tqe = TAILQ_FIRST(&tq->tq_delay_list)) != NULL) {
		TAILQ_REMOVE(&tq->tq_delay_list, tqe, tqent_list);
		taskq_ready_insert(tq, tqe, 0);
	}
	cv_broadcast(&tq->tq_cv);
	mutex_exit(&tq->tq_lock);
	/*
	 * Drain ready/IPL work *before* workqueue_destroy().
	 * NetBSD workqueue_finiqueue() KASSERTs
	 * SIMPLEQ_EMPTY(&q->q_queue_pending).  tq_ipl_busy only
	 * covers the enqueue window; tq_ipl_pending is the queued
	 * work itself and is waited in taskq_wait().
	 */
	taskq_wait(tq);
	{
		struct workqueue *ipl_wq;

		/*
		 * Detach the IPL hop before destroy so biodone
		 * cannot enqueue onto a workqueue we are tearing
		 * down.  A completion that already bumped busy
		 * must still run: taskq_dispatch_ent falls back
		 * to spl_tq_ipl_fb instead of dropping the work.
		 */
		while (tq->tq_ipl_busy != 0 || tq->tq_ipl_pending != 0)
			kpause("tqipl", false, 1, NULL);
		ipl_wq = tq->tq_ipl_wq;
		tq->tq_ipl_wq = NULL;
		membar_sync();
		while (tq->tq_ipl_busy != 0 || tq->tq_ipl_pending != 0)
			kpause("tqipl", false, 1, NULL);
		taskq_wait(tq);
		if (ipl_wq != NULL)
			workqueue_destroy(ipl_wq);
	}
	mutex_enter(&tq->tq_lock);
	tq->tq_destroyed = 1;
	cv_broadcast(&tq->tq_cv);
	cv_broadcast(&tq->tq_wait_cv);
	while (tq->tq_nrunning > 0 || tq->tq_nspawn > 0)
		cv_wait(&tq->tq_wait_cv, &tq->tq_lock);
	mutex_exit(&tq->tq_lock);
	membar_sync();
	/*
	 * Fallback dispatch after tq_ipl_wq was NULLed can still
	 * CAS-claim, bump ipl_busy/pending, and enqueue on
	 * spl_tq_ipl_fb.  Destroy used to stop watching pending
	 * here and then kmem_free(tq) while taskq_ipl_work still
	 * mutex_enter'd tq_lock.  Wait until the worker has
	 * dropped pending (after its last tq access).
	 */
	while (tq->tq_ipl_busy != 0 || tq->tq_ipl_pending != 0)
		kpause("tqipl", false, 1, NULL);
	mutex_enter(&tq->tq_lock);
	while (tq->tq_nrunning > 0 || tq->tq_nspawn > 0)
		cv_wait(&tq->tq_wait_cv, &tq->tq_lock);
	mutex_exit(&tq->tq_lock);
	while (tq->tq_ipl_busy != 0 || tq->tq_ipl_pending != 0)
		kpause("tqipl", false, 1, NULL);

	nthreads = tq->tq_nalloc;
	if (tq->tq_exec != NULL)
		kmem_free(tq->tq_exec, sizeof (taskqid_t) * nthreads);
	if (tq->tq_threads != NULL)
		kmem_free(tq->tq_threads, sizeof (kthread_t *) * nthreads);
	cv_destroy(&tq->tq_cv);
	cv_destroy(&tq->tq_wait_cv);
	mutex_destroy(&tq->tq_lock);
	kmem_free(tq, sizeof (*tq));
}

static taskqid_t
taskq_next_id(void)
{
	uint64_t id;

	id = atomic_inc_64_nv(&tqid_next);
	if (id == 0)
		id = atomic_inc_64_nv(&tqid_next);
	return ((taskqid_t)id);
}

/*
 * tq_issued is a watermark, not "last writer wins."  IDs are
 * allocated before tq_lock; two dispatchers can publish in the
 * opposite order from allocation.  A swap would let a smaller ID
 * clobber a larger one so taskq_wait_outstanding(tq, 0) could
 * skip already-queued work (zvol prefetch after zpool create).
 */
static void
tq_issued_update(taskq_t *tq, taskqid_t id)
{
	uint64_t nid = (uint64_t)id;
	uint64_t old;

	if (nid == 0)
		return;
	do {
		old = atomic_load_64(&tq->tq_issued);
		if (old >= nid)
			return;
	} while (atomic_cas_64(&tq->tq_issued, old, nid) != old);
}

taskqid_t
taskq_dispatch(taskq_t *tq, task_func_t func, void *arg, uint_t flags)
{
	taskq_ent_t *tqe;
	taskqid_t id;
	int kmflags;

	if (tq == NULL)
		return (TASKQID_INVALID);
	if (tq->tq_closing || tq->tq_destroyed)
		return (TASKQID_INVALID);
	kmflags = (flags & TQ_NOSLEEP) ? KM_NOSLEEP : KM_SLEEP;
	tqe = kmem_zalloc(sizeof (*tqe), kmflags);
	if (tqe == NULL)
		return (TASKQID_INVALID);
	tqe->tqent_func = func;
	tqe->tqent_arg = arg;
	tqe->tqent_dynamic = 1;
	id = tqe->tqent_id = taskq_next_id();
	tqe->tqent_state = TQENT_QUEUED;
	mutex_enter(&tq->tq_lock);
	if (tq->tq_closing || tq->tq_destroyed) {
		mutex_exit(&tq->tq_lock);
		kmem_free(tqe, sizeof (*tqe));
		return (TASKQID_INVALID);
	}
	tq_issued_update(tq, id);
	taskq_ready_insert(tq, tqe, (flags & TQ_FRONT) != 0);
	taskq_maybe_spawn(tq);
	cv_signal(&tq->tq_cv);
	mutex_exit(&tq->tq_lock);
	/*
	 * Worker may already have run and kmem_free'd a dynamic
	 * entry.  Return the stashed id, not tqe->tqent_id.
	 */
	return (id);
}

taskqid_t
taskq_dispatch_delay(taskq_t *tq, task_func_t func, void *arg, uint_t flags,
    clock_t expire_time)
{
	taskq_ent_t *tqe;
	taskqid_t id;
	int kmflags;
	clock_t now;

	if (tq == NULL)
		return (TASKQID_INVALID);
	now = ddi_get_lbolt();
	if (zfs_lbolt_delta(expire_time, now) <= 0)
		return (taskq_dispatch(tq, func, arg, flags));

	/*
	 * ddi_get_lbolt() is nanouptime truncated to the tick already
	 * in progress, so lbolt + N expires up to one tick before N
	 * tick periods of gethrtime() have elapsed.  Deadman ereports
	 * are limited to an integer number of seconds of that clock,
	 * and a wakeup a few microseconds early is dropped as the same
	 * second.  One extra tick puts a future deadline strictly
	 * after the requested interval.
	 */
	expire_time++;

	kmflags = (flags & TQ_NOSLEEP) ? KM_NOSLEEP : KM_SLEEP;
	tqe = kmem_zalloc(sizeof (*tqe), kmflags);
	if (tqe == NULL)
		return (TASKQID_INVALID);
	tqe->tqent_func = func;
	tqe->tqent_arg = arg;
	tqe->tqent_dynamic = 1;
	id = tqe->tqent_id = taskq_next_id();
	tqe->tqent_expire = expire_time;
	tqe->tqent_state = TQENT_QUEUED;
	mutex_enter(&tq->tq_lock);
	if (tq->tq_closing || tq->tq_destroyed) {
		mutex_exit(&tq->tq_lock);
		kmem_free(tqe, sizeof (*tqe));
		return (TASKQID_INVALID);
	}
	taskq_delay_insert(tq, tqe);
	tq_issued_update(tq, id);
	cv_signal(&tq->tq_cv);
	mutex_exit(&tq->tq_lock);
	return (id);
}

/*
 * FreeBSD/Linux taskq_dispatch_ent is void.  An idle prealloc ent
 * always queues: the worker unlinks before the callback, so the
 * same storage (zio->io_tqent) may be re-dispatched from it.
 * Already-queued ents coalesce (FreeBSD ta_pending).
 *
 * Thread-context dispatch is unconditional, matching illumos.
 * Workers do not exit while tq_list is non-empty even after
 * tq_destroyed, and destroy waits nrunning==0, so work that
 * arrives before workers exit still runs.  Refusing on
 * tq_closing/tq_destroyed was a silent no-op (void); the
 * taskqueue(9) shim increments ta_pending first, so a drop
 * left drain waiting forever.  The compensating unwind (IDLE
 * && !running => zero pending) raced the worker: it marks
 * TQENT_IDLE under tq_lock *before* the wrapper takes
 * taskqueue_lock and sets ta_running, so a successful dispatch
 * looked like a refusal and the callback was skipped.
 *
 * IPL still hops until destroyed (biodone); workers then drain
 * leftover ready entries.  A torn-down IPL hop falls back to
 * spl_tq_ipl_fb; both-wqs-NULL panics rather than run at IPL.
 * Do not drop live IPL work: that hangs zio_wait.  Dispatching
 * after destroy returns (tq already freed) remains the caller's
 * contract, same as illumos.
 */
void
taskq_dispatch_ent(taskq_t *tq, task_func_t func, void *arg, uint_t flags,
    taskq_ent_t *tqe)
{
	struct workqueue *wq;

	ASSERT(tq != NULL);
	ASSERT(tqe != NULL);
	ASSERT(func != NULL);

	if (cpu_intr_p() || cpu_softintr_p()) {
		/*
		 * Claim the entry before writing func/arg.  The worker
		 * latches those fields under tq_lock; scribbling on a
		 * queued entry races that read.  CAS failure coalesces
		 * (FreeBSD ta_pending).
		 */
		if (atomic_cas_uint(&tqe->tqent_state, TQENT_IDLE,
		    TQENT_QUEUED) != TQENT_IDLE)
			return;
		tqe->tqent_func = func;
		tqe->tqent_arg = arg;
		tqe->tqent_tq = tq;
		tqe->tqent_dynamic = 0;
		tqe->tqent_flags = flags;
		tqe->tqent_id = taskq_next_id();
		membar_producer();
		atomic_inc_uint((unsigned int *)&tq->tq_ipl_busy);
		membar_sync();
		if (tq->tq_destroyed) {
			atomic_dec_uint((unsigned int *)&tq->tq_ipl_busy);
			(void) atomic_swap_uint(&tqe->tqent_state, TQENT_IDLE);
			return;
		}
		wq = tq->tq_ipl_wq;
		if (wq == NULL)
			wq = spl_tq_ipl_fb;
		if (wq == NULL) {
			/*
			 * Module fini only.  Fallback is created
			 * first and destroyed last.  Running the
			 * callback here is the original C1 path
			 * (IPL_NONE pool_cache from biodone).
			 * Panic rather than corrupt the pool.
			 */
			atomic_dec_uint((unsigned int *)&tq->tq_ipl_busy);
			(void) atomic_swap_uint(&tqe->tqent_state, TQENT_IDLE);
			panic("ZFS: taskq IPL dispatch with no workqueue");
		}
		atomic_inc_uint((unsigned int *)&tq->tq_ipl_pending);
		tq_issued_update(tq, tqe->tqent_id);
		workqueue_enqueue(wq, &tqe->tqent_work, NULL);
		membar_sync();
		atomic_dec_uint((unsigned int *)&tq->tq_ipl_busy);
		return;
	}

	mutex_enter(&tq->tq_lock);
	if (tqe->tqent_state == TQENT_QUEUED) {
		mutex_exit(&tq->tq_lock);
		return;
	}
	tqe->tqent_func = func;
	tqe->tqent_arg = arg;
	tqe->tqent_tq = tq;
	tqe->tqent_dynamic = 0;
	tqe->tqent_flags = flags;
	tqe->tqent_id = taskq_next_id();
	tqe->tqent_state = TQENT_QUEUED;
	tq_issued_update(tq, tqe->tqent_id);
	taskq_ready_insert(tq, tqe, (flags & TQ_FRONT) != 0);
	taskq_maybe_spawn(tq);
	cv_signal(&tq->tq_cv);
	mutex_exit(&tq->tq_lock);
}

int
taskq_empty_ent(taskq_ent_t *tqe)
{
	/* 1 iff the entry is not on a queue.  True during the callback. */
	return (tqe->tqent_state == TQENT_IDLE);
}

void
taskq_init_ent(taskq_ent_t *tqe)
{
	memset(tqe, 0, sizeof (*tqe));
}

void
taskq_wait(taskq_t *tq)
{
	int self;

	/*
	 * A worker that waits its own queue will wait for itself
	 * forever (tq_active includes the running callback).  Skip
	 * this LWP's own in-flight task.  Production builds have
	 * no ASSERT, so the skip must be the real wait predicate.
	 */
	self = (taskq_of_curthread() == tq) ? 1 : 0;
	mutex_enter(&tq->tq_lock);
	while (tq->tq_active > self ||
	    (!self && !TAILQ_EMPTY(&tq->tq_list)) ||
	    (!self && tq->tq_ipl_pending != 0))
		cv_wait(&tq->tq_wait_cv, &tq->tq_lock);
	mutex_exit(&tq->tq_lock);
}

void
taskq_wait_id(taskq_t *tq, taskqid_t id)
{
	taskq_ent_t *tqe;
	int i, found;

	if (tq == NULL)
		return;
	if (id == TASKQID_INVALID) {
		taskq_wait(tq);
		return;
	}
	mutex_enter(&tq->tq_lock);
	for (;;) {
		found = 0;
		for (i = 0; i < tq->tq_nalloc; i++) {
			if (tq->tq_exec[i] == id) {
				/*
				 * This LWP is that task.  Waiting for
				 * our own exec slot never completes.
				 */
				if (tq->tq_threads[i] == curlwp)
					continue;
				found = 1;
				break;
			}
		}
		if (!found) {
			TAILQ_FOREACH(tqe, &tq->tq_list, tqent_list) {
				if (tqe->tqent_id == id) {
					found = 1;
					break;
				}
			}
		}
		if (!found) {
			TAILQ_FOREACH(tqe, &tq->tq_delay_list, tqent_list) {
				if (tqe->tqent_id == id) {
					found = 1;
					break;
				}
			}
		}
		if (!found)
			break;
		cv_wait(&tq->tq_wait_cv, &tq->tq_lock);
	}
	mutex_exit(&tq->tq_lock);
}

void
taskq_wait_outstanding(taskq_t *tq, taskqid_t id)
{
	taskq_ent_t *tqe;
	int i, found;

	if (tq == NULL)
		return;
	mutex_enter(&tq->tq_lock);
	/*
	 * id 0 / INVALID: wait for work already issued, not for
	 * unexpired delayed tasks.  zvol_create_minors() uses this
	 * on system_taskq after prefetch dispatch; treating it as
	 * taskq_wait() hung zpool create on leftover delay/IPL state.
	 */
	if (id == 0 || id == TASKQID_INVALID)
		id = tq_issued_get(tq);
	if (id == 0) {
		mutex_exit(&tq->tq_lock);
		return;
	}
	for (;;) {
		found = 0;
		for (i = 0; i < tq->tq_nalloc; i++) {
			if (tq->tq_exec[i] != 0 && tq->tq_exec[i] <= id) {
				if (tq->tq_threads[i] == curlwp)
					continue;
				found = 1;
				break;
			}
		}
		if (!found) {
			TAILQ_FOREACH(tqe, &tq->tq_list, tqent_list) {
				if (tqe->tqent_id <= id) {
					found = 1;
					break;
				}
			}
		}
		/*
		 * Do not wait for tq_ipl_pending or the delay
		 * list.  IPL hops are not the prefetch jobs
		 * zvol_create_minors() issued, and a leaked
		 * pending count hung zpool create.  Destroy
		 * still uses taskq_wait(), which drains IPL.
		 */
		if (!found)
			break;
		cv_wait(&tq->tq_wait_cv, &tq->tq_lock);
	}
	mutex_exit(&tq->tq_lock);
}

int
taskq_cancel_id(taskq_t *tq, taskqid_t id, boolean_t wait)
{
	taskq_ent_t *tqe;
	int i;

	if (tq == NULL || id == TASKQID_INVALID)
		return (ENOENT);

	mutex_enter(&tq->tq_lock);

	TAILQ_FOREACH(tqe, &tq->tq_delay_list, tqent_list) {
		if (tqe->tqent_id == id) {
			TAILQ_REMOVE(&tq->tq_delay_list, tqe, tqent_list);
			(void) atomic_swap_uint(&tqe->tqent_state, TQENT_IDLE);
			cv_broadcast(&tq->tq_wait_cv);
			cv_signal(&tq->tq_cv);
			mutex_exit(&tq->tq_lock);
			if (tqe->tqent_dynamic)
				kmem_free(tqe, sizeof (*tqe));
			return (0);
		}
	}

	TAILQ_FOREACH(tqe, &tq->tq_list, tqent_list) {
		if (tqe->tqent_id == id) {
			TAILQ_REMOVE(&tq->tq_list, tqe, tqent_list);
			(void) atomic_swap_uint(&tqe->tqent_state, TQENT_IDLE);
			tq->tq_active--;
			cv_broadcast(&tq->tq_wait_cv);
			mutex_exit(&tq->tq_lock);
			if (tqe->tqent_dynamic)
				kmem_free(tqe, sizeof (*tqe));
			return (0);
		}
	}

	if (wait) {
		int running, self;

		/*
		 * Skip this LWP's own exec slot: a callback that
		 * cancels its own ID with wait cannot complete until
		 * the wait returns.  Return EBUSY like a running
		 * task that was not waited for.
		 */
		for (;;) {
			running = 0;
			self = 0;
			for (i = 0; i < tq->tq_nalloc; i++) {
				if (tq->tq_exec[i] != id)
					continue;
				if (tq->tq_threads[i] == curlwp) {
					self = 1;
					continue;
				}
				running = 1;
				break;
			}
			if (!running)
				break;
			cv_wait(&tq->tq_wait_cv, &tq->tq_lock);
		}
		mutex_exit(&tq->tq_lock);
		return (self ? EBUSY : ENOENT);
	}

	mutex_exit(&tq->tq_lock);
	return (ENOENT);
}

int
taskq_member(taskq_t *tq, kthread_t *t)
{
	int i;

	for (i = 0; i < tq->tq_nalloc; i++) {
		if (tq->tq_threads[i] == t)
			return (1);
	}
	return (0);
}

taskq_t *
taskq_of_curthread(void)
{
	return (lwp_getspecific(taskq_lwp_key));
}

void
taskq_suspend(taskq_t *tq)
{
	int self;

	mutex_enter(&tq->tq_lock);
	tq->tq_suspended = 1;
	self = (taskq_of_curthread() == tq);
	while (tq->tq_nexec > (self ? 1 : 0))
		cv_wait(&tq->tq_wait_cv, &tq->tq_lock);
	mutex_exit(&tq->tq_lock);
}

int
taskq_suspended(taskq_t *tq)
{
	return (atomic_cas_uint((volatile unsigned int *)&tq->tq_suspended,
	    0, 0) != 0);
}

void
taskq_resume(taskq_t *tq)
{
	mutex_enter(&tq->tq_lock);
	tq->tq_suspended = 0;
	cv_broadcast(&tq->tq_cv);
	mutex_exit(&tq->tq_lock);
}

void
nulltask(void *arg)
{
	(void) arg;
}

static void
spl_rms_dtor(void *v)
{
	if (v != NULL)
		kmem_free(v, sizeof (struct rms_tsd));
}

void
spl_rms_init(void)
{
	lwp_specific_key_create(&spl_rms_lwp_key, spl_rms_dtor);
}

void
spl_rms_fini(void)
{
	lwp_specific_key_delete(spl_rms_lwp_key);
}

void
spl_taskq_init(void)
{
	int error;

	lwp_specific_key_create(&taskq_lwp_key, NULL);
	spl_rms_init();
	mutex_init(&taskqueue_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&taskqueue_cv, "tqdrain", CV_DEFAULT, NULL);
	system_proc = curproc;
	error = workqueue_create(&spl_tq_ipl_fb, "z_tq_ipl_fb",
	    taskq_ipl_work, NULL, minclsyspri, IPL_VM, WQ_MPSAFE | WQ_PERCPU);
	if (error != 0)
		spl_tq_ipl_fb = NULL;
	error = workqueue_create(&spl_tq_dwq, "zfs_tq_d",
	    taskq_destroy_work, NULL, 0, IPL_NONE, WQ_MPSAFE);
	if (error != 0)
		spl_tq_dwq = NULL;
	system_taskq = taskq_create("system_taskq", MAX(ncpu, 1),
	    minclsyspri, 4, 512, TASKQ_DYNAMIC | TASKQ_PREPOPULATE);
	system_delay_taskq = taskq_create("system_delay_taskq", MAX(ncpu, 1),
	    minclsyspri, 4, 512, TASKQ_DYNAMIC | TASKQ_PREPOPULATE);
	if (system_taskq == NULL || system_delay_taskq == NULL) {
		if (system_delay_taskq != NULL)
			taskq_destroy(system_delay_taskq);
		if (system_taskq != NULL)
			taskq_destroy(system_taskq);
		system_taskq = system_delay_taskq = NULL;
		panic("ZFS: system taskq create failed");
	}
}

void
spl_taskq_fini(void)
{
	taskq_destroy(system_delay_taskq);
	taskq_destroy(system_taskq);
	system_delay_taskq = NULL;
	system_taskq = NULL;
	if (spl_tq_dwq != NULL) {
		workqueue_destroy(spl_tq_dwq);
		spl_tq_dwq = NULL;
	}
	if (spl_tq_ipl_fb != NULL) {
		workqueue_destroy(spl_tq_ipl_fb);
		spl_tq_ipl_fb = NULL;
	}
	mutex_destroy(&taskqueue_lock);
	cv_destroy(&taskqueue_cv);
	spl_rms_fini();
	lwp_specific_key_delete(taskq_lwp_key);
}

/*
 * FreeBSD taskqueue(9) on top of this taskq.
 *
 * ta_dispatched is the only "is this static entry on a queue or
 * about to run" bit.  It is set under taskqueue_lock *before*
 * taskq_dispatch_ent and cleared only when the wrapper is finished
 * and ta_pending is 0.  Do not use tqent_queued for that: the
 * worker clears it before calling the func, so enqueue would see
 * an idle entry and double-dispatch the same taskq_ent_t.
 */
static void
taskqueue_wrapper(void *arg)
{
	struct task *t = arg;

	mutex_enter(&taskqueue_lock);
	for (;;) {
		uint32_t n = t->ta_pending;

		t->ta_pending = 0;
		t->ta_running = 1;
		t->ta_lwp = curlwp;
		mutex_exit(&taskqueue_lock);
		if (n != 0 && t->ta_func != NULL)
			t->ta_func(t->ta_context, (int)n);
		mutex_enter(&taskqueue_lock);
		t->ta_running = 0;
		t->ta_lwp = NULL;
		if (t->ta_pending == 0) {
			t->ta_dispatched = 0;
			cv_broadcast(&taskqueue_cv);
			mutex_exit(&taskqueue_lock);
			return;
		}
		cv_broadcast(&taskqueue_cv);
	}
}

void
taskqueue_init_task(struct task *t, void (*func)(void *, int), void *ctx)
{
	memset(t, 0, sizeof (*t));
	t->ta_func = func;
	t->ta_context = ctx;
	taskq_init_ent(&t->ta_ent);
}

int
taskqueue_enqueue(struct taskqueue *q, struct task *t)
{
	taskq_t *tq = (taskq_t *)q;
	int dispatch = 0;

	if (t == NULL || t->ta_func == NULL)
		return (0);
	if (tq == NULL)
		tq = system_taskq;
	mutex_enter(&taskqueue_lock);
	t->ta_tq = tq;
	t->ta_pending++;
	if (t->ta_dispatched == 0) {
		t->ta_dispatched = 1;
		dispatch = 1;
	}
	mutex_exit(&taskqueue_lock);
	if (dispatch)
		taskq_dispatch_ent(tq, taskqueue_wrapper, t, 0, &t->ta_ent);
	return (0);
}

void
taskqueue_drain(struct taskqueue *q, struct task *t)
{
	taskq_t *tq;

	if (t == NULL)
		return;
	mutex_enter(&taskqueue_lock);
	tq = t->ta_tq;
	if (tq == NULL)
		tq = (taskq_t *)q;
	/*
	 * Never wait for a task we ourselves are running -- that
	 * deadlocks.  taskq_member() is true for *any* worker of
	 * this taskq, so key off ta_lwp instead.
	 */
	if (t->ta_running != 0 && t->ta_lwp == curlwp) {
		mutex_exit(&taskqueue_lock);
		return;
	}
	/*
	 * Nothing queued or running: a stale ta_dispatched bit is
	 * not work.  Waiting on that wedged umount on tqdrain
	 * after POSIX ACL xattr deletes (drain vs vn_lock/vflush,
	 * not a dispatch_ent no-op).
	 */
	if (t->ta_pending == 0 && t->ta_running == 0) {
		t->ta_dispatched = 0;
		mutex_exit(&taskqueue_lock);
		return;
	}
	while (t->ta_pending != 0 || t->ta_running != 0)
		cv_wait(&taskqueue_cv, &taskqueue_lock);
	t->ta_dispatched = 0;
	mutex_exit(&taskqueue_lock);
}
