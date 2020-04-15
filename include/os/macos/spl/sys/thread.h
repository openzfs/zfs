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
 * Copyright (C) 2013 Jorgen Lundman <lundman@lundman.net>
 *
 */

#ifndef _SPL_THREAD_H
#define	_SPL_THREAD_H

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/tsd.h>
#include <sys/condvar.h>
#include <kern/sched_prim.h>
#include <mach/thread_policy.h>
#include <TargetConditionals.h>
#include <AvailabilityMacros.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * OsX thread type is
 * typedef struct thread *thread_t;
 *
 * Map that to the ZFS thread type: kthread_t
 */
#define	kthread thread
#define	kthread_t struct kthread

/*
 * Thread interfaces
 */
#define	TP_MAGIC			0x53535353

#define	TS_FREE		0x00 /* Thread at loose ends */
#define	TS_SLEEP	0x01 /* Awaiting an event */
#define	TS_RUN		0x02 /* Runnable, but not yet on a processor */
#define	TS_ONPROC	0x04 /* Thread is being run on a processor */
#define	TS_ZOMB		0x08 /* Thread has died but hasn't been reaped */
#define	TS_STOPPED	0x10 /* Stopped, initial state */
#define	TS_WAIT		0x20 /* Waiting to become runnable */


typedef void (*thread_func_t)(void *);


#define	curthread ((kthread_t *)current_thread()) /* current thread pointer */
#define	curproj   (ttoproj(curthread))		  /* current project pointer */

#define	thread_join(t)	VERIFY(0)

// Drop the p0 argument, not used.

#ifdef SPL_DEBUG_THREAD

#define	thread_create(A, B, C, D, E, F, G, H) \
    spl_thread_create_named(__FILE__, A, B, C, D, E, G, __FILE__, __LINE__, H)
#define	thread_create_named(name, A, B, C, D, E, F, G, H)	\
    spl_thread_create_named(name, A, B, C, D, E, G, __FILE__, __LINE__, H)

extern kthread_t *spl_thread_create_named(const char *name,
    caddr_t stk, size_t stksize,
    void (*proc)(void *), void *arg, size_t len, /* proc_t *pp, */ int state,
    const char *, int, pri_t pri);

#else

#define	thread_create(A, B, C, D, E, F, G, H) \
    spl_thread_create_named(__FILE__, A, B, C, D, E, G, H)
#define	thread_create_named(name, A, B, C, D, E, F, G, H)	\
    spl_thread_create_named(name, A, B, C, D, E, G, H)
extern kthread_t *spl_thread_create_named(const char *name,
    caddr_t stk, size_t stksize,
    void (*proc)(void *), void *arg, size_t len, /* proc_t *pp, */ int state,
    pri_t pri);

#endif

#if defined(MAC_OS_X_VERSION_10_9) && \
	(MAC_OS_X_VERSION_MIN_REQUIRED <= MAC_OS_X_VERSION_10_9)
/* Missing in 10.9 - none of this will be run, but handles us compile */
#define	THREAD_LATENCY_QOS_POLICY 7
#define	THREAD_LATENCY_QOS_POLICY_COUNT ((mach_msg_type_number_t) \
	(sizeof (thread_latency_qos_policy_data_t) / sizeof (integer_t)))
#define	THREAD_THROUGHPUT_QOS_POLICY 8
#define	THREAD_THROUGHPUT_QOS_POLICY_COUNT ((mach_msg_type_number_t) \
	(sizeof (thread_throughput_qos_policy_data_t) / sizeof (integer_t)))
typedef integer_t thread_latency_qos_t;
typedef integer_t thread_throughput_qos_t;
struct thread_throughput_qos_policy {
	thread_throughput_qos_t thread_throughput_qos_tier;
};
struct thread_latency_qos_policy {
	thread_latency_qos_t thread_latency_qos_tier;
};
typedef struct thread_throughput_qos_policy
thread_throughput_qos_policy_data_t;
typedef struct thread_latency_qos_policy
thread_latency_qos_policy_data_t;
#endif

#define	thread_exit spl_thread_exit
extern void spl_thread_exit(void) __attribute__((noreturn));

extern kthread_t *spl_current_thread(void);

extern void set_thread_importance_named(thread_t, pri_t, const char *);
extern void set_thread_importance(thread_t, pri_t);

extern void set_thread_throughput_named(thread_t,
    thread_throughput_qos_t, const char *);
extern void set_thread_throughput(thread_t,
    thread_throughput_qos_t);

extern void set_thread_latency_named(thread_t,
    thread_latency_qos_t, const char *);
extern void set_thread_latency(thread_t,
    thread_latency_qos_t);

extern void set_thread_timeshare_named(thread_t,
    const char *);
extern void set_thread_timeshare(thread_t);

extern void spl_throttle_set_thread_io_policy(int);

#define	delay osx_delay
extern void osx_delay(int);

#define	KPREEMPT_SYNC 0
static inline void kpreempt(int flags)
{
	(void) thread_block(THREAD_CONTINUE_NULL);
}

static inline char *
getcomm(void)
{
	static char name[MAXCOMLEN + 1];
	proc_selfname(name, sizeof (name));
	/* Not thread safe */
	return (name);
}

#define	getpid() proc_selfpid()

#ifdef	__cplusplus
}
#endif

#endif  /* _SPL_THREAD_H */
