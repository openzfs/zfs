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
#define _SPL_THREAD_H

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/tsd.h>
#include <sys/condvar.h>
//#include <kern/sched_prim.h>


//struct kthread {
//	void *something;
//};
typedef struct _KTHREAD kthread_t;
typedef struct _KTHREAD thread_t;

/*
 * Thread interfaces
 */
#define TP_MAGIC			0x53535353

#define TS_FREE         0x00    /* Thread at loose ends */
#define TS_SLEEP        0x01    /* Awaiting an event */
#define TS_RUN          0x02    /* Runnable, but not yet on a processor */
#define TS_ONPROC       0x04    /* Thread is being run on a processor */
#define TS_ZOMB         0x08    /* Thread has died but hasn't been reaped */
#define TS_STOPPED      0x10    /* Stopped, initial state */
#define TS_WAIT         0x20    /* Waiting to become runnable */


typedef void (*thread_func_t)(void *);

//HANDLE PsGetCurrentThreadId();

// This should be ThreadId, but that dies in taskq_member,
// for now, dsl_pool_sync_context calls it instead.
#define current_thread PsGetCurrentThread
#define   curthread       ((void *)current_thread())      /* current thread pointer */
#define   curproj         (ttoproj(curthread))    /* current project pointer */

#define thread_join(t)			VERIFY(0)

// Drop the p0 argument, not used.

#ifdef SPL_DEBUG_THREAD

#define	thread_create(A,B,C,D,E,F,G,H) spl_thread_create(A,B,C,D,E,G,__FILE__, __LINE__, H)
extern kthread_t *spl_thread_create(caddr_t stk, size_t stksize,
	void (*proc)(void *), void *arg, size_t len, /*proc_t *pp,*/ int state,
									char *, int, pri_t pri);

#else

#define	thread_create(A,B,C,D,E,F,G,H) spl_thread_create(A,B,C,D,E,G,H)
extern kthread_t *spl_thread_create(caddr_t stk, size_t stksize,
	void (*proc)(void *), void *arg, size_t len, /*proc_t *pp,*/ int state,
    pri_t pri);

#endif

#define	thread_exit spl_thread_exit
extern void spl_thread_exit(void);

extern kthread_t *spl_current_thread(void);

#define	delay windows_delay
#define IOSleep windows_delay
extern void windows_delay(int);


#define KPREEMPT_SYNC 0
static inline void kpreempt(int flags)
{
    (void)flags;
    //ZwYieldExecution();
    LARGE_INTEGER interval;
    interval.QuadPart = 0;
    KeDelayExecutionThread(KernelMode, FALSE, &interval);
}

#endif  /* _SPL_THREAD_H */
