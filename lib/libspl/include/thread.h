/*
 * Copyright (c) 2006 OmniTI, Inc. All rights reserved
 * This header file distributed under the terms of the CDDL.
 * Portions Copyright 2004 Sun Microsystems, Inc. All Rights reserved.
 */

#include <pthread.h>
#include <sys/time.h>
#include <sys/types.h>

#ifndef _SYS_THREAD_H
#define _SYS_THREAD_H

typedef pthread_t			thread_t;
typedef pthread_mutex_t			mutex_t;
typedef pthread_cond_t			cond_t;

#define THR_BOUND			1
#define THR_DETACHED			2
#define THR_DAEMON			4

#define USYNC_THREAD			0x00 /* private to a process */
#define USYNC_PROCESS			0x01 /* shared by processes */

#define thr_self()			pthread_self()
#define thr_sigsetmask			pthread_sigmask
#define __nthreads()			2 /* XXX: Force multi-thread */

static inline int
thr_create(void *stack_base, size_t stack_size,
	   void *(*start_func)(void *), void *arg,
	   long flags, thread_t *new_thread_id)
{
	pthread_attr_t attr;
	int rc;

	pthread_attr_init(&attr);

	if (flags & THR_DETACHED)
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	rc = pthread_create(new_thread_id, &attr, start_func, arg);
	pthread_attr_destroy(&attr);

	return rc;
}
#endif /* _SYS_THREAD_H */

#ifndef _SYS_MUTEX_H
#define _SYS_MUTEX_H

static inline int
_mutex_held(mutex_t *mp)
{
	int rc;

	rc = pthread_mutex_trylock(mp);
	if (rc)
		return rc;

	pthread_mutex_unlock(mp);
	return rc;
}

static inline void
_mutex_init(mutex_t *mp, int type, void *arg)
{
	pthread_mutex_init(mp, NULL);
}

#define mutex_init(mp, type, arg)	_mutex_init(mp, type, arg)
#define mutex_lock(mp)			pthread_mutex_lock(mp)
#define mutex_unlock(mp)		pthread_mutex_unlock(mp)
#define mutex_destroy(mp)		pthread_mutex_destroy(mp)
#define mutex_trylock(mp)		pthread_mutex_trylock(mp)
#define DEFAULTMUTEX			PTHREAD_MUTEX_INITIALIZER
#define DEFAULTCV			PTHREAD_COND_INITIALIZER
#define MUTEX_HELD(mp)			_mutex_held(mp)
#endif /* _SYS_MUTEX_H */

#ifndef _SYS_CONDVAR_H
#define _SYS_CONDVAR_H

#define cond_init(c, type, arg)		pthread_cond_init(c, NULL)
#define cond_wait(c, m)			pthread_cond_wait(c, m)
#define _cond_wait(c, m)		pthread_cond_wait(c, m)
#define cond_signal(c)			pthread_cond_signal(c)
#define cond_broadcast(c)		pthread_cond_broadcast(c)
#define cond_destroy(c)			pthread_cond_destroy(c)
#define cond_timedwait			pthread_cond_timedwait
#define _cond_timedwait			pthread_cond_timedwait
#endif /* _SYS_CONDVAR_H */

#ifndef RTLD_FIRST
#define RTLD_FIRST			0
#endif
