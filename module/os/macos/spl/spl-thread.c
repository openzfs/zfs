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

uint64_t zfs_threads = 0;

kthread_t *
spl_thread_create_named(
    const char *name,
    caddr_t stk,
    size_t stksize,
    void (*proc)(void *),
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

	result = kernel_thread_start((thread_continue_t)proc, arg, &thread);

	if (result != KERN_SUCCESS)
		return (NULL);

	set_thread_importance_named(thread, pri, "anonymous new zfs thread");

	if (name == NULL)
		name = "unnamed zfs thread";

#if	defined(MAC_OS_X_VERSION_10_15) && \
	(MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_15)
	thread_set_thread_name(thread, name);
#endif

	thread_deallocate(thread);

	atomic_inc_64(&zfs_threads);

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

#if defined(MACOS_IMPURE)
extern void throttle_set_thread_io_policy(int priority);
#endif

void
spl_throttle_set_thread_io_policy(int priority)
{
#if defined(MACOS_IMPURE)
	throttle_set_thread_io_policy(priority);
#endif
}


/*
 * Set xnu kernel thread importance based on openzfs pri_t.
 *
 * Thread importance adjusts upwards and downards from
 * BASEPRI_KERNEL (defined as 81).
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
set_thread_importance_named(thread_t thread, pri_t pri, const char *name)
{
	thread_precedence_policy_data_t policy = { 0 };

	/*
	 * start by finding an offset from BASEPRI_KERNEL,
	 * which is found in osfmk/kern/sched.h
	 */

	policy.importance = pri - 81;

	/* dont let ANY of our threads run as high as networking & GPU */
	if (policy.importance > 0)
		policy.importance = 0;
	else if (policy.importance < (-11))
		policy.importance = -11;

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

void
set_thread_importance(thread_t thread, pri_t pri)
{
	set_thread_importance_named(thread, pri, "anonymous zfs thread");
}

/*
 * Set a kernel throughput qos for this thread,
 */

void
set_thread_throughput_named(thread_t thread,
    thread_throughput_qos_t throughput, const char *name)
{
	/*
	 * TIERs: 0 is USER_INTERACTIVE, 1 is USER_INITIATED, 1 is LEGACY,
	 *        2 is UTILITY, 5 is BACKGROUND, 5 is MAINTENANCE
	 *
	 *  (from xnu/osfmk/kern/thread_policy.c)
	 */

	thread_throughput_qos_policy_data_t qosp = { 0 };
	qosp.thread_throughput_qos_tier = throughput;

	kern_return_t qoskret = thread_policy_set(thread,
	    THREAD_THROUGHPUT_QOS_POLICY,
	    (thread_policy_t)&qosp,
	    THREAD_THROUGHPUT_QOS_POLICY_COUNT);
	if (qoskret != KERN_SUCCESS) {
		printf("SPL: %s:%d: WARNING failed to set"
		    " thread throughput policy retval: %d "
		    " (THREAD_THROUGHPUT_QOS_POLICY %x), %s\n",
		    __func__, __LINE__, qoskret,
		    qosp.thread_throughput_qos_tier, name);
	}
}

void
set_thread_throughput(thread_t thread,
    thread_throughput_qos_t throughput)
{
	set_thread_throughput_named(thread, throughput,
	    "anonymous zfs function");
}

void
set_thread_latency_named(thread_t thread,
    thread_latency_qos_t latency, const char *name)
{
	/*
	 * TIERs: 0 is USER_INTERACTIVE, 1 is USER_INITIATED, 1 is LEGACY,
	 *        3 is UTILITY, 3 is BACKGROUND, 5 is MAINTENANCE
	 *
	 *  (from xnu/osfmk/kern/thread_policy.c)
	 * NB: these differ from throughput tier mapping
	 */

	thread_latency_qos_policy_data_t qosp = { 0 };
	qosp.thread_latency_qos_tier = latency;
	kern_return_t qoskret = thread_policy_set(thread,
	    THREAD_LATENCY_QOS_POLICY,
	    (thread_policy_t)&qosp,
	    THREAD_LATENCY_QOS_POLICY_COUNT);
	if (qoskret != KERN_SUCCESS) {
		printf("SPL: %s:%d: WARNING failed to set"
		    " thread latency policy retval: %d "
		    " (THREAD_LATENCY_QOS_POLICY %x), %s",
		    __func__, __LINE__,
		    qoskret, qosp.thread_latency_qos_tier,
		    name);
	}
}

void
set_thread_latency(thread_t thread,
    thread_latency_qos_t latency)
{
	set_thread_latency_named(thread, latency, "anonymous zfs function");
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
set_thread_timeshare_named(thread_t thread, const char *name)
{
	thread_extended_policy_data_t policy = { .timeshare = TRUE };
	kern_return_t kret = thread_policy_set(thread,
	    THREAD_EXTENDED_POLICY,
	    (thread_policy_t)&policy,
	    THREAD_EXTENDED_POLICY_COUNT);
	if (kret != KERN_SUCCESS) {
		printf("SPL: %s:%d: WARNING failed to set"
		    " timeshare policy retval: %d, %s\n",
		    __func__, __LINE__, kret, name);
	}
}

void
set_thread_timeshare(thread_t thread)
{
	set_thread_timeshare_named(thread, "anonymous zfs function");
}
