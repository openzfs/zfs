// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */

/*
 *
 * Copyright (C) 2013 Jorgen Lundman <lundman@lundman.net>
 *
 */

#ifndef _SPL_THREAD_H
#define	_SPL_THREAD_H

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/tsd.h>
#include <sys/condvar.h>

typedef struct _KTHREAD kthread_t;
typedef struct _KTHREAD thread_t;

/*
 * Thread interfaces
 */
#define	TP_MAGIC	0x53535353

#define	TS_FREE		0x00    /* Thread at loose ends */
#define	TS_SLEEP	0x01    /* Awaiting an event */
#define	TS_RUN		0x02    /* Runnable, but not yet on a processor */
#define	TS_ONPROC	0x04    /* Thread is being run on a processor */
#define	TS_ZOMB		0x08    /* Thread has died but hasn't been reaped */
#define	TS_STOPPED	0x10    /* Stopped, initial state */
#define	TS_WAIT		0x20    /* Waiting to become runnable */


typedef void (*thread_func_t)(void *);

// This should be ThreadId, but that dies in taskq_member,
// for now, dsl_pool_sync_context calls it instead.
#define	current_thread PsGetCurrentThread
#define	curthread ((void *)current_thread()) /* current thread pointer */
#define	curproj (ttoproj(curthread)) /* current project pointer */

#define	thread_join(t)			VERIFY(0)

// Drop the p0 argument, not used.

#ifdef SPL_DEBUG_THREAD

#define	thread_create(A, B, C, D, E, F, G, H) \
	spl_thread_create(A, B, C, D, E, G, __FILE__, __LINE__, H)
extern kthread_t *spl_thread_create(caddr_t stk, size_t stksize,
	void (*proc)(void *), void *arg, size_t len, /* proc_t *pp, */
	int state, char *, int, pri_t pri);
#define	thread_create_named(name, A, B, C, D, E, F, G, H)       \
    spl_thread_create(A, B, C, D, E, G, __FILE__, __LINE__, H)
#else

#define	thread_create(A, B, C, D, E, F, G, H) \
	spl_thread_create(A, B, C, D, E, G, H)
extern kthread_t *spl_thread_create(caddr_t stk, size_t stksize,
	void (*proc)(void *), void *arg, size_t len, /* proc_t *pp, */
	int state, pri_t pri);
#define	thread_create_named(name, A, B, C, D, E, F, G, H)       \
    spl_thread_create(A, B, C, D, E, G, H)
#endif

#define	thread_exit spl_thread_exit
extern void __declspec(noreturn) spl_thread_exit(void);

extern kthread_t *spl_current_thread(void);

#define	delay windows_delay
#define	IOSleep windows_delay
extern void windows_delay(int);

#define	KPREEMPT_SYNC 0
static inline void kpreempt(int flags)
{
	(void) flags;
	LARGE_INTEGER interval;
	interval.QuadPart = 0;
	KeDelayExecutionThread(KernelMode, FALSE, &interval);
}

static inline void kpreempt_ms(int ms)
{
	LARGE_INTEGER interval;
	interval.QuadPart = -10000 * ms;
	KeDelayExecutionThread(KernelMode, FALSE, &interval);
}

#endif  /* _SPL_THREAD_H */
