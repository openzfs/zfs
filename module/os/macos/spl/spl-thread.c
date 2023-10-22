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
 *
 * Copyright (C) 2008 MacZFS
 * Copyright (C) 2013, 2020 Jorgen Lundman <lundman@lundman.net>
 * Copyright (C) 2023 Sean Doran <smd@use.net>
 *
 */

#include <sys/thread.h>
#include <mach/thread_act.h>
#include <sys/kmem.h>
#include <sys/tsd.h>
#include <sys/debug.h>
#include <sys/vnode.h>
#include <sys/callb.h>
#include <sys/systm.h>
#include <TargetConditionals.h>
#include <AvailabilityMacros.h>
#include <sys/mutex.h>

uint64_t zfs_threads = 0;

typedef struct initialize_thread_args {
	lck_mtx_t *lck;
	const char *child_name;
	thread_func_t proc;
	void *arg;
	pri_t pri;
	int state;
	thread_extended_policy_t tmsharepol;
	thread_throughput_qos_policy_t throughpol;
	thread_latency_qos_policy_t latpol;
	int child_done;
	void *wait_channel;
#ifdef SPL_DEBUG_THREAD
	const char *caller_filename;
	int caller_line;
#endif
} initialize_thread_args_t;

/*
 * do setup work inside the child thread, then launch
 * the work, proc(arg)
 */
void
spl_thread_setup(void *v, wait_result_t wr)
{

	/* we have been created!  sanity check and take lock */
	spl_data_barrier();
	VERIFY3P(v, !=, NULL);

	initialize_thread_args_t *a = v;

	lck_mtx_lock(a->lck);
	spl_data_barrier();

	/* set things up */

	if (a->child_name == NULL)
		a->child_name = "anonymous zfs thread";

#if	defined(MAC_OS_X_VERSION_10_15) &&				\
	(MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_15)
	thread_set_thread_name(current_thread(), a->child_name);
#endif

	spl_set_thread_importance(current_thread(), a->pri, a->child_name);

	if (a->tmsharepol) {
		spl_set_thread_timeshare(current_thread(),
		    a->tmsharepol, a->child_name);
	}

	if (a->throughpol) {
		if (a->tmsharepol) {
			ASSERT(a->tmsharepol->timeshare);
		}
		spl_set_thread_throughput(current_thread(),
		    a->throughpol, a->child_name);
	}

	if (a->latpol) {
		if (a->tmsharepol) {
			ASSERT(a->tmsharepol->timeshare);
		}
		spl_set_thread_latency(current_thread(),
		    a->latpol, a->child_name);
	}

	/* save proc and args */

	thread_func_t proc = a->proc;
	void *arg = a->arg;

	/* set done with setup flag, wake parent, release lck */

	a->child_done = 1;
	spl_data_barrier();
	wakeup_one(a->wait_channel);
	spl_data_barrier();
	lck_mtx_unlock(a->lck);

	/* jump to proc, which doesn't come back here */

	proc(arg);
	__builtin_unreachable();
	panic("SPL: proc called from spl_thread_setup() returned");
}

kthread_t *
spl_thread_create_named(
    const char *name,
    caddr_t stk,
    size_t stksize,
    thread_func_t proc,
    void *arg,
    size_t len,
    int state,
#ifdef SPL_DEBUG_THREAD
    const char *filename,
    int line,
#endif
    pri_t pri)
{
	thread_extended_policy_data_t tmsharepol = {
		.timeshare = TRUE
	};

	return (spl_thread_create_named_with_extpol_and_qos(
	    &tmsharepol, NULL, NULL,
	    name, stk, stksize, proc, arg,
	    len, state,
#ifdef SPL_DEBUG_THREAD
	    filename, line,
#endif
	    pri));
}

/*
 * For each of the first three args, if NULL then kernel default
 *
 * no timesharing, no throughput qos, no latency qos
 */
kthread_t *
spl_thread_create_named_with_extpol_and_qos(
    thread_extended_policy_t tmsharepol,
    thread_throughput_qos_policy_t throughpol,
    thread_latency_qos_policy_t latpol,
    const char *name,
    caddr_t stk,
    size_t stksize,
    thread_func_t proc,
    void *arg,
    size_t len,
    int state,
#ifdef SPL_DEBUG_THREAD
    const char *filename,
    int line,
#endif
    pri_t pri)
{
	kern_return_t result;
	thread_t thread;

#ifdef SPL_DEBUG_THREAD
	printf("Start thread pri %d by '%s':%d\n", pri,
	    filename, line);
#endif

	uint64_t wait_location;

	wrapper_mutex_t lck;

	lck_mtx_init((lck_mtx_t *)&lck, spl_mtx_grp, spl_mtx_lck_attr);

	initialize_thread_args_t childargs = {
		.lck = (lck_mtx_t *)&lck,
		.child_name = name,
		.proc = proc,
		.arg = arg,
		.pri = pri,
		.state = state,
		.tmsharepol = tmsharepol,
		.throughpol = throughpol,
		.latpol = latpol,
		.child_done = 0,
		.wait_channel = &wait_location,
#ifdef SPL_DEBUG_THREAD
		.caller_filename = filename,
		.caller_line = line,
#endif
	};

	spl_data_barrier();
	lck_mtx_lock((lck_mtx_t *)&lck);
	spl_data_barrier();

	result = kernel_thread_start(spl_thread_setup,
	    (void *)&childargs, &thread);

	if (result != KERN_SUCCESS) {
		lck_mtx_unlock((lck_mtx_t *)&lck);
		lck_mtx_destroy((lck_mtx_t *)&lck, spl_mtx_grp);
		printf("SPL: %s:%d kernel_thread_start error return %d\n",
		    __func__, __LINE__, result);
		return (NULL);
	}

	for (; ; ) {
		spl_data_barrier();
		(void) msleep(&wait_location, (lck_mtx_t *)&lck, PRIBIO,
		    "spl thread initialization", 0);
		spl_data_barrier();
		if (childargs.child_done != 0)
			break;
	}

	thread_deallocate(thread);

	atomic_inc_64(&zfs_threads);

	lck_mtx_unlock((lck_mtx_t *)&lck);
	lck_mtx_destroy((lck_mtx_t *)&lck, spl_mtx_grp);

	return ((kthread_t *)thread);
}

kthread_t *
spl_current_thread(void)
{
	thread_t cur_thread = current_thread();
	return ((kthread_t *)cur_thread);
}

__attribute__((noreturn)) void
spl_thread_exit(void)
{
	atomic_dec_64(&zfs_threads);

	tsd_thread_exit();
	(void) thread_terminate(current_thread());
	__builtin_unreachable();
}


/*
 * IllumOS has callout.c - place it here until we find a better place
 */
callout_id_t
timeout_generic(int type, void (*func)(void *), void *arg,
    hrtime_t expiration, hrtime_t resolution, int flags)
{
	struct timespec ts;
	hrt2ts(expiration, &ts);
	bsd_timeout(func, arg, &ts);
	/*
	 * bsd_untimeout() requires func and arg to cancel the timeout, so
	 * pass it back as the callout_id. If we one day were to implement
	 * untimeout_generic() they would pass it back to us
	 */
	return ((callout_id_t)arg);
}

/*
 * Set xnu kernel thread importance based on openzfs pri_t.
 *
 * Thread importance adjusts upwards and downwards from BASEPRI_KERNEL (defined
 * as 81).  Higher value is higher priority (e.g. BASEPRI_REALTIME is 96),
 * BASEPRI_GRAPHICS is 76, and MAXPRI_USER is 63.
 *
 * (See osfmk/kern/sched.h)
 *
 * Many important kernel tasks run at BASEPRI_KERNEL,
 * with networking and kernel graphics (Metal etc) running
 * at BASEPRI_KERNEL + 1.
 *
 * We want maxclsyspri threads to have less xnu priority
 * BASEPRI_KERNEL, so as to avoid UI stuttering, network
 * disconnection and other side-effects of high zfs load with
 * high thread priority.
 *
 * In <sysmacros.h> we define maxclsyspri to 80 with
 * defclsyspri and minclsyspri set below that.
 */

void
spl_set_thread_importance(thread_t thread, pri_t pri, const char *name)
{
	thread_precedence_policy_data_t policy = { 0 };

	/*
	 * start by finding an offset from BASEPRI_KERNEL,
	 * which is found in osfmk/kern/sched.h
	 *
	 * (it's 81, importance is a signed-offset from that)
	 */

	const pri_t basepri = 81;
	const pri_t importance = pri - basepri;
	const pri_t importance_floor = DSL_SCAN_ISS_SYSPRI - basepri;

	policy.importance = importance;

	/*
	 * dont let ANY of our threads run as high as networking & GPU
	 *
	 * hard cap on our maximum priority at 81 (BASEPRI_KERNEL),
	 * which is then our maxclsyspri.
	 */
	if (policy.importance > 0)
		policy.importance = 0;
	/*
	 * set a floor on importance at priority 59, which is just below
	 * bluetoothd and userland audio, which are of relatively high
	 * userland importance.
	 */
	else if (policy.importance < importance_floor)
		policy.importance = importance_floor;

	int i = policy.importance;
	kern_return_t pol_prec_kret = thread_policy_set(thread,
	    THREAD_PRECEDENCE_POLICY,
	    (thread_policy_t)&policy,
	    THREAD_PRECEDENCE_POLICY_COUNT);
	if (pol_prec_kret != KERN_SUCCESS) {
		printf("SPL: %s:%d: ERROR failed to set"
		    " thread precedence to %d ret %d name %s\n",
		    __func__, __LINE__, i, pol_prec_kret, name);
	}
}

/*
 * Set a kernel throughput qos for this thread,
 */

void
spl_set_thread_throughput(thread_t thread,
    thread_throughput_qos_policy_t throughput, const char *name)
{


	ASSERT(throughput);

	if (!throughput)
		return;

	if (!name)
		name = "anonymous zfs thread (throughput)";

	/*
	 * TIERs:
	 *
	 * 0 is USER_INTERACTIVE, 1 is USER_INITIATED, 1 is LEGACY,
	 * 2 is UTILITY, 5 is BACKGROUND, 5 is MAINTENANCE
	 *
	 * (from xnu/osfmk/kern/thread_policy.c)
	 */

	kern_return_t qoskret = thread_policy_set(thread,
	    THREAD_THROUGHPUT_QOS_POLICY,
	    (thread_policy_t)throughput,
	    THREAD_THROUGHPUT_QOS_POLICY_COUNT);
	if (qoskret != KERN_SUCCESS) {
		printf("SPL: %s:%d: WARNING failed to set"
		    " thread throughput policy retval: %d "
		    " (THREAD_THROUGHPUT_QOS_POLICY %x), %s\n",
		    __func__, __LINE__, qoskret,
		    throughput->thread_throughput_qos_tier, name);
	}
}

void
spl_set_thread_latency(thread_t thread,
    thread_latency_qos_policy_t latency, const char *name)
{

	ASSERT(latency);

	if (!latency)
		return;

	if (!name)
		name = "anonymous zfs thread (latency)";

	/*
	 * TIERs:
	 * 0 is USER_INTERACTIVE, 1 is USER_INITIATED, 1 is LEGACY,
	 * 3 is UTILITY, 3 is BACKGROUND, 5 is MAINTENANCE
	 *
	 * (from xnu/osfmk/kern/thread_policy.c)
	 *
	 * NB: these differ from throughput tier mapping
	 */

	kern_return_t qoskret = thread_policy_set(thread,
	    THREAD_LATENCY_QOS_POLICY,
	    (thread_policy_t)latency,
	    THREAD_LATENCY_QOS_POLICY_COUNT);
	if (qoskret != KERN_SUCCESS) {
		printf("SPL: %s:%d: WARNING failed to set"
		    " thread latency policy to %x, retval: %d, '%s'\n",
		    __func__, __LINE__,
		    latency->thread_latency_qos_tier,
		    qoskret,
		    name);
	}
}

/*
 * XNU will dynamically adjust TIMESHARE
 * threads around the chosen thread priority.
 * The lower the importance (signed value),
 * the more XNU will adjust a thread.
 * Threads may be adjusted *upwards* from their
 * base priority by XNU as well.
 */

void
spl_set_thread_timeshare(thread_t thread,
    thread_extended_policy_t policy,
    const char *name)
{

	ASSERT(policy);

	if (!policy)
		return;

	if (!name) {
		if (policy->timeshare)
			name = "anonymous zfs thread (timeshare->off)";
		else
			name = "anonymous zfs thread (timeshare->on)";
	}

	kern_return_t kret = thread_policy_set(thread,
	    THREAD_EXTENDED_POLICY,
	    (thread_policy_t)policy,
	    THREAD_EXTENDED_POLICY_COUNT);
	if (kret != KERN_SUCCESS) {
		printf("SPL: %s:%d: WARNING failed to set"
		    " timeshare policy to %d, retval: %d, %s\n",
		    __func__, __LINE__, kret,
		    policy->timeshare, name);
	}
}
