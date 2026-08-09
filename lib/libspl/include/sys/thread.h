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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2012, Joyent, Inc. All rights reserved.
 */

#ifndef _SYS_THREAD_H
#define	_SYS_THREAD_H

#include <pthread.h>

/*
 * Threads.
 */
typedef pthread_t	kthread_t;

#define	TS_RUN		0x00000002
#define	TS_JOINABLE	0x00000004

#define	curthread	((void *)(uintptr_t)pthread_self())
#define	getcomm()	"unknown"

#define	thread_create_named(name, stk, stksize, func, arg, len, \
    pp, state, pri)	\
	zk_thread_create(name, func, arg, stksize, state)
#define	thread_create(stk, stksize, func, arg, len, pp, state, pri)	\
	zk_thread_create(#func, func, arg, stksize, state)
#define	thread_exit()	pthread_exit(NULL)
#define	thread_join(t)	pthread_join((pthread_t)(t), NULL)

#define	newproc(f, a, cid, pri, ctp, pid)	(ENOSYS)
/*
 * Check if the current thread is a memory reclaim thread.
 * Always returns false in userspace (no memory reclaim thread).
 */
#define	current_is_reclaim_thread()	(0)

/* in libzpool, p0 exists only to have its address taken */
typedef void (proc_t)(void);
extern void p0(void);

#define	curproc		(&p0)

#define	PS_NONE		-1

extern kthread_t *zk_thread_create(const char *name, void (*func)(void *),
    void *arg, size_t stksize, int state);

#define	issig()		(FALSE)

#define	KPREEMPT_SYNC		(-1)

#define	kpreempt(x)		sched_yield()
#define	kpreempt_disable()	((void)0)
#define	kpreempt_enable()	((void)0)

#endif /* _SYS_THREAD_H */
