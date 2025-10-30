// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
typedef struct proc {
	uintptr_t	this_is_never_used_dont_dereference_it;
} proc_t;

extern struct proc p0;
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
