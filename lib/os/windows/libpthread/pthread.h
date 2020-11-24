/*
 * Posix Threads library for Microsoft Windows
 *
 * Use at own risk, there is no implied warranty to this code.
 * It uses undocumented features of Microsoft Windows that can change
 * at any time in the future.
 *
 * (C) 2010 Lockless Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of Lockless Inc. nor the names of its contributors may be
 *    used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AN
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * You may want to use the MingW64 winpthreads library instead.
 * It is based on this, but adds error checking.
 */

/*
 * Version 1.0.1 Released 2 Feb 2012
 * Fixes pthread_barrier_destroy() to wait for threads to exit the barrier.
 */

#ifndef WIN_PTHREADS
#define WIN_PTHREADS

#define _WIN32_MEAN_AND_LEAN
#include <windows.h>
#include <setjmp.h>
#include <errno.h>
#include <sys/timeb.h>
#include <process.h>
#include <time.h>

//#define ETIMEDOUT	110
//#define ENOTSUP		134

// warning C4018: '>': signed/unsigned mismatch 
#pragma warning (disable: 4018)

#define PTHREAD_CANCEL_DISABLE 0
#define PTHREAD_CANCEL_ENABLE 0x01

#define PTHREAD_CANCEL_DEFERRED 0
#define PTHREAD_CANCEL_ASYNCHRONOUS 0x02

#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 0x04

#define PTHREAD_EXPLICT_SCHED 0
#define PTHREAD_INHERIT_SCHED 0x08

#define PTHREAD_SCOPE_PROCESS 0
#define PTHREAD_SCOPE_SYSTEM 0x10

#define PTHREAD_DEFAULT_ATTR (PTHREAD_CANCEL_ENABLE)

#define PTHREAD_CANCELED ((void *) 0xDEADBEEFULL)

#define PTHREAD_ONCE_INIT 0
#define PTHREAD_MUTEX_INITIALIZER {(void*)-1,-1,0,0,0,0}
#define PTHREAD_RWLOCK_INITIALIZER {0}
#define PTHREAD_COND_INITIALIZER {0}
#define PTHREAD_BARRIER_INITIALIZER \
	{0,0,PTHREAD_MUTEX_INITIALIZER,PTHREAD_COND_INITIALIZER}
#define PTHREAD_SPINLOCK_INITIALIZER 0

#define PTHREAD_DESTRUCTOR_ITERATIONS 256
#define PTHREAD_KEYS_MAX (1<<20)

#define PTHREAD_MUTEX_NORMAL 0
#define PTHREAD_MUTEX_ERRORCHECK 1
#define PTHREAD_MUTEX_RECURSIVE 2
#define PTHREAD_MUTEX_DEFAULT 3
#define PTHREAD_MUTEX_SHARED 4
#define PTHREAD_MUTEX_PRIVATE 0
#define PTHREAD_PRIO_NONE 0
#define PTHREAD_PRIO_INHERIT 8
#define PTHREAD_PRIO_PROTECT 16
#define PTHREAD_PRIO_MULT 32
#define PTHREAD_PROCESS_SHARED 0
#define PTHREAD_PROCESS_PRIVATE 1

#define PTHREAD_BARRIER_SERIAL_THREAD 1

#define PTHREAD_STACK_MIN 0



/* Windows doesn't have this, so declare it ourselves. */
//struct timespec
//{
//	/* long long in windows is the same as long in unix for 64bit */
//	long long tv_sec;
//	long long tv_nsec;
//};

typedef struct _pthread_cleanup _pthread_cleanup;
struct _pthread_cleanup
{
	void (*func)(void *);
	void *arg;
	_pthread_cleanup *next;
};

struct _pthread_v
{
	void *ret_arg;
	void *(* func)(void *);
	_pthread_cleanup *clean;
	HANDLE h;
	int cancelled;
	unsigned p_state;
	int keymax;
	void **keyval;

	jmp_buf jb;
};

typedef struct _pthread_v *pthread_t;

typedef struct pthread_barrier_t pthread_barrier_t;
struct pthread_barrier_t
{
	int count;
	int total;
	CRITICAL_SECTION m;
	CONDITION_VARIABLE cv;
};

typedef struct pthread_attr_t pthread_attr_t;
struct pthread_attr_t
{
	unsigned p_state;
	void *stack;
	size_t s_size;
};

typedef long pthread_once_t;
typedef unsigned pthread_mutexattr_t;
typedef SRWLOCK pthread_rwlock_t;
typedef CRITICAL_SECTION pthread_mutex_t;
typedef unsigned pthread_key_t;
typedef void *pthread_barrierattr_t;
typedef long pthread_spinlock_t;
typedef int pthread_condattr_t;
typedef CONDITION_VARIABLE pthread_cond_t;
typedef int pthread_rwlockattr_t;

volatile long _pthread_cancelling;

int _pthread_concur;

/* Will default to zero as needed */
pthread_once_t _pthread_tls_once;
DWORD _pthread_tls;

/* Note initializer is zero, so this works */
pthread_rwlock_t _pthread_key_lock;
long _pthread_key_max;
long _pthread_key_sch;
void (**_pthread_key_dest)(void *);


#define pthread_cleanup_push(F, A)\
{\
	const _pthread_cleanup _pthread_cup = {(F), (A), pthread_self()->clean};\
	_ReadWriteBarrier();\
	pthread_self()->clean = (_pthread_cleanup *) &_pthread_cup;\
	_ReadWriteBarrier()

/* Note that if async cancelling is used, then there is a race here */
#define pthread_cleanup_pop(E)\
	(pthread_self()->clean = _pthread_cup.next, (E?_pthread_cup.func(_pthread_cup.arg):0));}

static void _pthread_once_cleanup(pthread_once_t *o)
{
	*o = 0;
}

static pthread_t pthread_self(void);
static int pthread_once(pthread_once_t *o, void (*func)(void))
{
	long state = *o;

	_ReadWriteBarrier();

	while (state != 1)
	{
		if (!state)
		{
			if (!_InterlockedCompareExchange(o, 2, 0))
			{
				/* Success */
				pthread_cleanup_push(_pthread_once_cleanup, o);
				func();
				pthread_cleanup_pop(0);

				/* Mark as done */
				*o = 1;

				return 0;
			}
		}

		YieldProcessor();

		_ReadWriteBarrier();

		state = *o;
	}

	/* Done */
	return 0;
}

static int _pthread_once_raw(pthread_once_t *o, void (*func)(void))
{
	long state = *o;

	_ReadWriteBarrier();

	while (state != 1)
	{
		if (!state)
		{
			if (!_InterlockedCompareExchange(o, 2, 0))
			{
				/* Success */
				func();

				/* Mark as done */
				*o = 1;

				return 0;
			}
		}

		YieldProcessor();

		_ReadWriteBarrier();

		state = *o;
	}

	/* Done */
	return 0;
}

static int pthread_mutex_lock(pthread_mutex_t *m)
{
	EnterCriticalSection(m);
	return 0;
}

static int pthread_mutex_unlock(pthread_mutex_t *m)
{
	LeaveCriticalSection(m);
	return 0;
}

static int pthread_mutex_trylock(pthread_mutex_t *m)
{
	return TryEnterCriticalSection(m) ? 0 : EBUSY;
}

static int pthread_mutex_init(pthread_mutex_t *m, pthread_mutexattr_t *a)
{
	(void) a;
	InitializeCriticalSection(m);

	return 0;
}

static int pthread_mutex_destroy(pthread_mutex_t *m)
{
	DeleteCriticalSection(m);
	return 0;
}

#define pthread_mutex_getprioceiling(M, P) ENOTSUP
#define pthread_mutex_setprioceiling(M, P) ENOTSUP

static int pthread_equal(pthread_t t1, pthread_t t2)
{
	return t1 == t2;
}

static void pthread_testcancel(void);

static int pthread_rwlock_init(pthread_rwlock_t *l, pthread_rwlockattr_t *a)
{
	(void) a;
	InitializeSRWLock(l);

	return 0;
}

static int pthread_rwlock_destroy(pthread_rwlock_t *l)
{
	(void) *l;
	return 0;
}

static int pthread_rwlock_rdlock(pthread_rwlock_t *l)
{
	pthread_testcancel();
	AcquireSRWLockShared(l);

	return 0;
}

static int pthread_rwlock_wrlock(pthread_rwlock_t *l)
{
	pthread_testcancel();
	AcquireSRWLockExclusive(l);

	return 0;
}

static void pthread_tls_init(void)
{
	_pthread_tls = TlsAlloc();

	/* Cannot continue if out of indexes */
	if (_pthread_tls == TLS_OUT_OF_INDEXES) abort();
}

static int pthread_rwlock_unlock(pthread_rwlock_t* l);

static void _pthread_cleanup_dest(pthread_t t)
{
	int i, j;

	for (j = 0; j < PTHREAD_DESTRUCTOR_ITERATIONS; j++)
	{
		int flag = 0;

		for (i = 0; i < t->keymax; i++)
		{
			void *val = t->keyval[i];

			if (val)
			{
				pthread_rwlock_rdlock(&_pthread_key_lock);
				if ((uintptr_t) _pthread_key_dest[i] > 1)
				{
					/* Call destructor */
					t->keyval[i] = NULL;
					_pthread_key_dest[i](val);
					flag = 1;
				}
				pthread_rwlock_unlock(&_pthread_key_lock);
			}
		}

		/* Nothing to do? */
		if (!flag) return;
	}
}

static pthread_t pthread_self(void)
{
	pthread_t t;

	_pthread_once_raw(&_pthread_tls_once, pthread_tls_init);

	t = TlsGetValue(_pthread_tls);

	/* Main thread? */
	if (!t)
	{
		t = malloc(sizeof(struct _pthread_v));

		/* If cannot initialize main thread, then the only thing we can do is abort */
		if (!t) abort();

		t->ret_arg = NULL;
		t->func = NULL;
		t->clean = NULL;
		t->cancelled = 0;
		t->p_state = PTHREAD_DEFAULT_ATTR;
		t->keymax = 0;
		t->keyval = NULL;
		t->h = GetCurrentThread();

		/* Save for later */
		TlsSetValue(_pthread_tls, t);

		if (setjmp(t->jb))
		{
			/* Make sure we free ourselves if we are detached */
			if (!t->h) free(t);

			/* Time to die */
			_endthreadex(0);
		}
	}

	return t;
}

static int pthread_rwlock_unlock(pthread_rwlock_t *l)
{
	void *state = *(void **)l;

	if (state == (void *) 1)
	{
		/* Known to be an exclusive lock */
		ReleaseSRWLockExclusive(l);
	}
	else
	{
		/* A shared unlock will work */
		ReleaseSRWLockShared(l);
	}

	return 0;
}


static int pthread_rwlock_tryrdlock(pthread_rwlock_t *l)
{
	/* Get the current state of the lock */
	void *state = *(void **) l;

	if (!state)
	{
		/* Unlocked to locked */
		if (!_InterlockedCompareExchangePointer((void *) l, (void *)0x11, NULL)) return 0;
		return EBUSY;
	}

	/* A single writer exists */
	if (state == (void *) 1) return EBUSY;

	/* Multiple writers exist? */
	if ((uintptr_t) state & 14) return EBUSY;

	if (_InterlockedCompareExchangePointer((void *) l, (void *) ((uintptr_t)state + 16), state) == state) return 0;

	return EBUSY;
}

static int pthread_rwlock_trywrlock(pthread_rwlock_t *l)
{
	/* Try to grab lock if it has no users */
	if (!_InterlockedCompareExchangePointer((void *) l, (void *)1, NULL)) return 0;

	return EBUSY;
}

static unsigned long long _pthread_time_in_ms(void)
{
	struct __timeb64 tb;

	_ftime64(&tb);

	return tb.time * 1000 + tb.millitm;
}

static unsigned long long _pthread_time_in_ms_from_timespec(const struct timespec *ts)
{
	unsigned long long t = ts->tv_sec * 1000;
	t += ts->tv_nsec / 1000000;

	return t;
}

static unsigned long long _pthread_rel_time_in_ms(const struct timespec *ts)
{
	unsigned long long t1 = _pthread_time_in_ms_from_timespec(ts);
	unsigned long long t2 = _pthread_time_in_ms();

	/* Prevent underflow */
	if (t1 < t2) return 0;
	return t1 - t2;
}

static int pthread_rwlock_timedrdlock(pthread_rwlock_t *l, const struct timespec *ts)
{
	unsigned long long ct = _pthread_time_in_ms();
	unsigned long long t = _pthread_time_in_ms_from_timespec(ts);

	pthread_testcancel();

	/* Use a busy-loop */
	while (1)
	{
		/* Try to grab lock */
		if (!pthread_rwlock_tryrdlock(l)) return 0;

		/* Get current time */
		ct = _pthread_time_in_ms();

		/* Have we waited long enough? */
		if (ct > t) return ETIMEDOUT;
	}
}

static int pthread_rwlock_timedwrlock(pthread_rwlock_t *l, const struct timespec *ts)
{
	unsigned long long ct = _pthread_time_in_ms();
	unsigned long long t = _pthread_time_in_ms_from_timespec(ts);

	pthread_testcancel();

	/* Use a busy-loop */
	while (1)
	{
		/* Try to grab lock */
		if (!pthread_rwlock_trywrlock(l)) return 0;

		/* Get current time */
		ct = _pthread_time_in_ms();

		/* Have we waited long enough? */
		if (ct > t) return ETIMEDOUT;
	}
}

static int pthread_get_concurrency(int *val)
{
	*val = _pthread_concur;
	return 0;
}

static int pthread_set_concurrency(int val)
{
	_pthread_concur = val;
	return 0;
}

#define pthread_getschedparam(T, P, S) ENOTSUP
#define pthread_setschedparam(T, P, S) ENOTSUP
#define pthread_getcpuclockid(T, C) ENOTSUP

static int pthread_exit(void *res)
{
	pthread_t t = pthread_self();

	t->ret_arg = res;

	_pthread_cleanup_dest(t);

	longjmp(t->jb, 1);
}


static void _pthread_invoke_cancel(void)
{
	_pthread_cleanup *pcup;

	_InterlockedDecrement(&_pthread_cancelling);

	/* Call cancel queue */
	for (pcup = pthread_self()->clean; pcup; pcup = pcup->next)
	{
		pcup->func(pcup->arg);
	}

	pthread_exit(PTHREAD_CANCELED);
}

static void pthread_testcancel(void)
{
	if (_pthread_cancelling)
	{
		pthread_t t = pthread_self();

		if (t->cancelled && (t->p_state & PTHREAD_CANCEL_ENABLE))
		{
			_pthread_invoke_cancel();
		}
	}
}


static int pthread_cancel(pthread_t t)
{
	if (t->p_state & PTHREAD_CANCEL_ASYNCHRONOUS)
	{
		/* Dangerous asynchronous cancelling */
		CONTEXT ctxt;

		/* Already done? */
		if (t->cancelled) return ESRCH;

		ctxt.ContextFlags = CONTEXT_CONTROL;

		SuspendThread(t->h);
		GetThreadContext(t->h, &ctxt);
#ifdef _M_X64
		ctxt.Rip = (uintptr_t) _pthread_invoke_cancel;
#else
		ctxt.Eip = (uintptr_t) _pthread_invoke_cancel;
#endif
		SetThreadContext(t->h, &ctxt);

		/* Also try deferred Cancelling */
		t->cancelled = 1;

		/* Notify everyone to look */
		_InterlockedIncrement(&_pthread_cancelling);

		ResumeThread(t->h);
	}
	else
	{
		/* Safe deferred Cancelling */
		t->cancelled = 1;

		/* Notify everyone to look */
		_InterlockedIncrement(&_pthread_cancelling);
	}

	return 0;
}

static unsigned _pthread_get_state(pthread_attr_t *attr, unsigned flag)
{
	return attr->p_state & flag;
}

static int _pthread_set_state(pthread_attr_t *attr, unsigned flag, unsigned val)
{
	if (~flag & val) return EINVAL;
	attr->p_state &= ~flag;
	attr->p_state |= val;

	return 0;
}

static int pthread_attr_init(pthread_attr_t *attr)
{
	attr->p_state = PTHREAD_DEFAULT_ATTR;
	attr->stack = NULL;
	attr->s_size = 0;
	return 0;
}

static int pthread_attr_destroy(pthread_attr_t *attr)
{
	/* No need to do anything */
	return 0;
}


static int pthread_attr_setdetachstate(pthread_attr_t *a, int flag)
{
	return _pthread_set_state(a, PTHREAD_CREATE_DETACHED, flag);
}

static int pthread_attr_getdetachstate(pthread_attr_t *a, int *flag)
{
	*flag = _pthread_get_state(a, PTHREAD_CREATE_DETACHED);
	return 0;
}

static int pthread_attr_setinheritsched(pthread_attr_t *a, int flag)
{
	return _pthread_set_state(a, PTHREAD_INHERIT_SCHED, flag);
}

static int pthread_attr_getinheritsched(pthread_attr_t *a, int *flag)
{
	*flag = _pthread_get_state(a, PTHREAD_INHERIT_SCHED);
	return 0;
}

static int pthread_attr_setscope(pthread_attr_t *a, int flag)
{
	return _pthread_set_state(a, PTHREAD_SCOPE_SYSTEM, flag);
}

static int pthread_attr_getscope(pthread_attr_t *a, int *flag)
{
	*flag = _pthread_get_state(a, PTHREAD_SCOPE_SYSTEM);
	return 0;
}

static int pthread_attr_getstackaddr(pthread_attr_t *attr, void **stack)
{
	*stack = attr->stack;
	return 0;
}

static int pthread_attr_setstackaddr(pthread_attr_t *attr, void *stack)
{
	attr->stack = stack;
	return 0;
}

static int pthread_attr_getstacksize(pthread_attr_t *attr, size_t *size)
{
	*size = attr->s_size;
	return 0;
}

static int pthread_attr_setstacksize(pthread_attr_t *attr, size_t size)
{
	attr->s_size = size;
	return 0;
}

#define pthread_attr_getguardsize(A, S) ENOTSUP
#define pthread_attr_setguardsize(A, S) ENOTSUP
#define pthread_attr_getschedparam(A, S) ENOTSUP
#define pthread_attr_setschedparam(A, S) ENOTSUP
#define pthread_attr_getschedpolicy(A, S) ENOTSUP
#define pthread_attr_setschedpolicy(A, S) ENOTSUP


static int pthread_setcancelstate(int state, int *oldstate)
{
	pthread_t t = pthread_self();

	if ((state & PTHREAD_CANCEL_ENABLE) != state) return EINVAL;
	if (oldstate) *oldstate = t->p_state & PTHREAD_CANCEL_ENABLE;
	t->p_state &= ~PTHREAD_CANCEL_ENABLE;
	t->p_state |= state;

	return 0;
}

static int pthread_setcanceltype(int type, int *oldtype)
{
	pthread_t t = pthread_self();

	if ((type & PTHREAD_CANCEL_ASYNCHRONOUS) != type) return EINVAL;
	if (oldtype) *oldtype = t->p_state & PTHREAD_CANCEL_ASYNCHRONOUS;
	t->p_state &= ~PTHREAD_CANCEL_ASYNCHRONOUS;
	t->p_state |= type;

	return 0;
}

static int __stdcall pthread_create_wrapper(void *args)
{
	struct _pthread_v *tv = args;

	_pthread_once_raw(&_pthread_tls_once, pthread_tls_init);

	TlsSetValue(_pthread_tls, tv);

	if (!setjmp(tv->jb))
	{
		/* Call function and save return value */
		tv->ret_arg = tv->func(tv->ret_arg);

		/* Clean up destructors */
		_pthread_cleanup_dest(tv);
	}

	/* If we exit too early, then we can race with create */
	while (tv->h == (HANDLE) -1)
	{
		YieldProcessor();
		_ReadWriteBarrier();
	}

	/* Make sure we free ourselves if we are detached */
	if (!tv->h) free(tv);

	return 0;
}

static int pthread_create(pthread_t *th, pthread_attr_t *attr, void *(* func)(void *), void *arg)
{
	struct _pthread_v *tv = malloc(sizeof(struct _pthread_v));
	unsigned ssize = 0;

	if (!tv) return 1;

	*th = tv;

	/* Save data in pthread_t */
	tv->ret_arg = arg;
	tv->func = func;
	tv->clean = NULL;
	tv->cancelled = 0;
	tv->p_state = PTHREAD_DEFAULT_ATTR;
	tv->keymax = 0;
	tv->keyval = NULL;
	tv->h = (HANDLE) -1;

	if (attr)
	{
		tv->p_state = attr->p_state;
		ssize = attr->s_size;
	}

	/* Make sure tv->h has value of -1 */
	_ReadWriteBarrier();

	tv->h = (HANDLE) _beginthreadex(NULL, ssize, pthread_create_wrapper, tv, 0, NULL);

	/* Failed */
	if (!tv->h) return 1;

	if (tv->p_state & PTHREAD_CREATE_DETACHED)
	{
		CloseHandle(tv->h);
		_ReadWriteBarrier();
		tv->h = 0;
	}

	return 0;
}

static int pthread_join(pthread_t t, void **res)
{
	struct _pthread_v *tv = t;

	pthread_testcancel();

	WaitForSingleObject(tv->h, INFINITE);
	CloseHandle(tv->h);

	/* Obtain return value */
	if (res) *res = tv->ret_arg;

	free(tv);

	return 0;
}

static int pthread_detach(pthread_t t)
{
	struct _pthread_v *tv = t;

	/*
	 * This can't race with thread exit because
	 * our call would be undefined if called on a dead thread.
	 */

	CloseHandle(tv->h);
	_ReadWriteBarrier();
	tv->h = 0;

	return 0;
}

static int pthread_mutexattr_init(pthread_mutexattr_t *a)
{
	*a = 0;
	return 0;
}

static int pthread_mutexattr_destroy(pthread_mutexattr_t *a)
{
	(void) a;
	return 0;
}

static int pthread_mutexattr_gettype(pthread_mutexattr_t *a, int *type)
{
	*type = *a & 3;

	return 0;
}

static int pthread_mutexattr_settype(pthread_mutexattr_t *a, int type)
{
	if ((unsigned) type > 3) return EINVAL;
	*a &= ~3;
	*a |= type;

	return 0;
}

static int pthread_mutexattr_getpshared(pthread_mutexattr_t *a, int *type)
{
	*type = *a & 4;

	return 0;
}

static int pthread_mutexattr_setpshared(pthread_mutexattr_t * a, int type)
{
	if ((type & 4) != type) return EINVAL;

	*a &= ~4;
	*a |= type;

	return 0;
}

static int pthread_mutexattr_getprotocol(pthread_mutexattr_t *a, int *type)
{
	*type = *a & (8 + 16);

	return 0;
}

static int pthread_mutexattr_setprotocol(pthread_mutexattr_t *a, int type)
{
	if ((type & (8 + 16)) != 8 + 16) return EINVAL;

	*a &= ~(8 + 16);
	*a |= type;

	return 0;
}

static int pthread_mutexattr_getprioceiling(pthread_mutexattr_t *a, int * prio)
{
	*prio = *a / PTHREAD_PRIO_MULT;
	return 0;
}

static int pthread_mutexattr_setprioceiling(pthread_mutexattr_t *a, int prio)
{
	*a &= (PTHREAD_PRIO_MULT - 1);
	*a += prio * PTHREAD_PRIO_MULT;

	return 0;
}

static int pthread_mutex_timedlock(pthread_mutex_t *m, struct timespec *ts)
{
	unsigned long long t, ct;

	struct _pthread_crit_t
	{
		void *debug;
		LONG count;
		LONG r_count;
		HANDLE owner;
		HANDLE sem;
		ULONG_PTR spin;
	};

	/* Try to lock it without waiting */
	if (!pthread_mutex_trylock(m)) return 0;

	ct = _pthread_time_in_ms();
	t = _pthread_time_in_ms_from_timespec(ts);

	while (1)
	{
		/* Have we waited long enough? */
		if (ct > t) return ETIMEDOUT;

		/* Wait on semaphore within critical section */
		WaitForSingleObject(((struct _pthread_crit_t *)m)->sem, t - ct);

		/* Try to grab lock */
		if (!pthread_mutex_trylock(m)) return 0;

		/* Get current time */
		ct = _pthread_time_in_ms();
	}
}

#define _PTHREAD_BARRIER_FLAG (1<<30)

static int pthread_barrier_destroy(pthread_barrier_t *b)
{
	EnterCriticalSection(&b->m);

	while (b->total > _PTHREAD_BARRIER_FLAG)
	{
		/* Wait until everyone exits the barrier */
		SleepConditionVariableCS(&b->cv, &b->m, INFINITE);
	}

	LeaveCriticalSection(&b->m);

	DeleteCriticalSection(&b->m);

	return 0;
}

static int pthread_barrier_init(pthread_barrier_t *b, void *attr, int count)
{
	/* Ignore attr */
	(void) attr;

	b->count = count;
	b->total = 0;

	InitializeCriticalSection(&b->m);
	InitializeConditionVariable(&b->cv);

	return 0;
}

static int pthread_barrier_wait(pthread_barrier_t *b)
{
	EnterCriticalSection(&b->m);

	while (b->total > _PTHREAD_BARRIER_FLAG)
	{
		/* Wait until everyone exits the barrier */
		SleepConditionVariableCS(&b->cv, &b->m, INFINITE);
	}

	/* Are we the first to enter? */
	if (b->total == _PTHREAD_BARRIER_FLAG) b->total = 0;

	b->total++;

	if (b->total == b->count)
	{
		b->total += _PTHREAD_BARRIER_FLAG - 1;
		WakeAllConditionVariable(&b->cv);

		LeaveCriticalSection(&b->m);

		return 1;
	}
	else
	{
		while (b->total < _PTHREAD_BARRIER_FLAG)
		{
			/* Wait until enough threads enter the barrier */
			SleepConditionVariableCS(&b->cv, &b->m, INFINITE);
		}

		b->total--;

		/* Get entering threads to wake up */
		if (b->total == _PTHREAD_BARRIER_FLAG) WakeAllConditionVariable(&b->cv);

		LeaveCriticalSection(&b->m);

		return 0;
	}
}

static int pthread_barrierattr_init(void **attr)
{
	*attr = NULL;
	return 0;
}

static int pthread_barrierattr_destroy(void **attr)
{
	/* Ignore attr */
	(void) attr;

	return 0;
}

static int pthread_barrierattr_setpshared(void **attr, int s)
{
	*attr = (void *)(uintptr_t)s;
	return 0;
}

static int pthread_barrierattr_getpshared(void **attr, int *s)
{
	*s = (int) (size_t) *attr;

	return 0;
}

static int pthread_key_create(pthread_key_t *key, void (* dest)(void *))
{
	int i;
	long nmax;
	void (**d)(void *);

	if (!key) return EINVAL;

	pthread_rwlock_wrlock(&_pthread_key_lock);

	for (i = _pthread_key_sch; i < _pthread_key_max; i++)
	{
		if (!_pthread_key_dest[i])
		{
			*key = i;
			if (dest)
			{
				_pthread_key_dest[i] = dest;
			}
			else
			{
				_pthread_key_dest[i] = (void(*)(void *))1;
			}
			pthread_rwlock_unlock(&_pthread_key_lock);

			return 0;
		}
	}

	for (i = 0; i < _pthread_key_sch; i++)
	{
		if (!_pthread_key_dest[i])
		{
			*key = i;
			if (dest)
			{
				_pthread_key_dest[i] = dest;
			}
			else
			{
				_pthread_key_dest[i] = (void(*)(void *))1;
			}
			pthread_rwlock_unlock(&_pthread_key_lock);

			return 0;
		}
	}

	if (!_pthread_key_max) _pthread_key_max = 1;
	if (_pthread_key_max == PTHREAD_KEYS_MAX)
	{
		pthread_rwlock_unlock(&_pthread_key_lock);

		return ENOMEM;
	}

	nmax = _pthread_key_max * 2;
	if (nmax > PTHREAD_KEYS_MAX) nmax = PTHREAD_KEYS_MAX;

	/* No spare room anywhere */
	d = realloc(_pthread_key_dest, nmax * sizeof(*d));
	if (!d)
	{
		pthread_rwlock_unlock(&_pthread_key_lock);

		return ENOMEM;
	}

	/* Clear new region */
	memset((void *) &d[_pthread_key_max], 0, (nmax-_pthread_key_max)*sizeof(void *));

	/* Use new region */
	_pthread_key_dest = d;
	_pthread_key_sch = _pthread_key_max + 1;
	*key = _pthread_key_max;
	_pthread_key_max = nmax;

	if (dest)
	{
		_pthread_key_dest[*key] = dest;
	}
	else
	{
		_pthread_key_dest[*key] = (void(*)(void *))1;
	}

	pthread_rwlock_unlock(&_pthread_key_lock);

	return 0;
}

static int pthread_key_delete(pthread_key_t key)
{
	if (key > _pthread_key_max) return EINVAL;
	if (!_pthread_key_dest) return EINVAL;

	pthread_rwlock_wrlock(&_pthread_key_lock);
	_pthread_key_dest[key] = NULL;

	/* Start next search from our location */
	if (_pthread_key_sch > key) _pthread_key_sch = key;

	pthread_rwlock_unlock(&_pthread_key_lock);

	return 0;
}

static void *pthread_getspecific(pthread_key_t key)
{
	pthread_t t = pthread_self();

	if (key >= t->keymax) return NULL;

	return t->keyval[key];

}

static int pthread_setspecific(pthread_key_t key, const void *value)
{
	pthread_t t = pthread_self();

	if (key > t->keymax)
	{
		int keymax = (key + 1) * 2;
		void **kv = realloc(t->keyval, keymax * sizeof(void *));

		if (!kv) return ENOMEM;

		/* Clear new region */
		memset(&kv[t->keymax], 0, (keymax - t->keymax)*sizeof(void*));

		t->keyval = kv;
		t->keymax = keymax;
	}

	t->keyval[key] = (void *) value;

	return 0;
}


static int pthread_spin_init(pthread_spinlock_t *l, int pshared)
{
	(void) pshared;

	*l = 0;
	return 0;
}

static int pthread_spin_destroy(pthread_spinlock_t *l)
{
	(void) l;
	return 0;
}

/* No-fair spinlock due to lack of knowledge of thread number */
static int pthread_spin_lock(pthread_spinlock_t *l)
{
	while (_InterlockedExchange(l, EBUSY))
	{
		/* Don't lock the bus whilst waiting */
		while (*l)
		{
			YieldProcessor();

			/* Compiler barrier.  Prevent caching of *l */
			_ReadWriteBarrier();
		}
	}

	return 0;
}

static int pthread_spin_trylock(pthread_spinlock_t *l)
{
	return _InterlockedExchange(l, EBUSY);
}

static int pthread_spin_unlock(pthread_spinlock_t *l)
{
	/* Compiler barrier.  The store below acts with release symmantics */
	_ReadWriteBarrier();

	*l = 0;

	return 0;
}

static int pthread_cond_init(pthread_cond_t *c, pthread_condattr_t *a)
{
	(void) a;

	InitializeConditionVariable(c);
	return 0;
}

static int pthread_cond_signal(pthread_cond_t *c)
{
	WakeConditionVariable(c);
	return 0;
}

static int pthread_cond_broadcast(pthread_cond_t *c)
{
	WakeAllConditionVariable(c);
	return 0;
}

static int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m)
{
	pthread_testcancel();
	SleepConditionVariableCS(c, m, INFINITE);
	return 0;
}

static int pthread_cond_destroy(pthread_cond_t *c)
{
	(void) c;
	return 0;
}

static int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, struct timespec *t)
{
	unsigned long long tm = _pthread_rel_time_in_ms(t);

	pthread_testcancel();

	if (!SleepConditionVariableCS(c, m, tm)) return ETIMEDOUT;

	/* We can have a spurious wakeup after the timeout */
	if (!_pthread_rel_time_in_ms(t)) return ETIMEDOUT;

	return 0;
}

static int pthread_condattr_destroy(pthread_condattr_t *a)
{
	(void) a;
	return 0;
}

#define pthread_condattr_getclock(A, C) ENOTSUP
#define pthread_condattr_setclock(A, C) ENOTSUP

static int pthread_condattr_init(pthread_condattr_t *a)
{
	*a = 0;
	return 0;
}

static int pthread_condattr_getpshared(pthread_condattr_t *a, int *s)
{
	*s = *a;
	return 0;
}

static int pthread_condattr_setpshared(pthread_condattr_t *a, int s)
{
	*a = s;
	return 0;
}

static int pthread_rwlockattr_destroy(pthread_rwlockattr_t *a)
{
	(void) a;
	return 0;
}

static int pthread_rwlockattr_init(pthread_rwlockattr_t *a)
{
	*a = 0;
}

static int pthread_rwlockattr_getpshared(pthread_rwlockattr_t *a, int *s)
{
	*s = *a;
	return 0;
}

static int pthread_rwlockattr_setpshared(pthread_rwlockattr_t *a, int s)
{
	*a = s;
	return 0;
}


/* No fork() in windows - so ignore this */
#define pthread_atfork(F1,F2,F3) 0

/* Windows has rudimentary signals support */
#define pthread_kill(T, S) 0
#define pthread_sigmask(H, S1, S2) 0

#if 0
/* Wrap cancellation points */
#define accept(...) (pthread_testcancel(), accept(__VA_ARGS__))
#define aio_suspend(...) (pthread_testcancel(), aio_suspend(__VA_ARGS__))
#define clock_nanosleep(...) (pthread_testcancel(), clock_nanosleep(__VA_ARGS__))
#define close(...) (pthread_testcancel(), close(__VA_ARGS__))
#define connect(...) (pthread_testcancel(), connect(__VA_ARGS__))
#define creat(...) (pthread_testcancel(), creat(__VA_ARGS__))
#define fcntl(...) (pthread_testcancel(), fcntl(__VA_ARGS__))
#define fdatasync(...) (pthread_testcancel(), fdatasync(__VA_ARGS__))
#define fsync(...) (pthread_testcancel(), fsync(__VA_ARGS__))
#define getmsg(...) (pthread_testcancel(), getmsg(__VA_ARGS__))
#define getpmsg(...) (pthread_testcancel(), getpmsg(__VA_ARGS__))
#define lockf(...) (pthread_testcancel(), lockf(__VA_ARGS__))
#define mg_receive(...) (pthread_testcancel(), mg_receive(__VA_ARGS__))
#define mg_send(...) (pthread_testcancel(), mg_send(__VA_ARGS__))
#define mg_timedreceive(...) (pthread_testcancel(), mg_timedreceive(__VA_ARGS__))
#define mg_timessend(...) (pthread_testcancel(), mg_timedsend(__VA_ARGS__))
#define msgrcv(...) (pthread_testcancel(), msgrecv(__VA_ARGS__))
#define msgsnd(...) (pthread_testcancel(), msgsnd(__VA_ARGS__))
#define msync(...) (pthread_testcancel(), msync(__VA_ARGS__))
#define nanosleep(...) (pthread_testcancel(), nanosleep(__VA_ARGS__))
#define open(...) (pthread_testcancel(), open(__VA_ARGS__))
#define pause(...) (pthread_testcancel(), pause(__VA_ARGS__))
#define poll(...) (pthread_testcancel(), poll(__VA_ARGS__))
#define pread(...) (pthread_testcancel(), pread(__VA_ARGS__))
#define pselect(...) (pthread_testcancel(), pselect(__VA_ARGS__))
#define putmsg(...) (pthread_testcancel(), putmsg(__VA_ARGS__))
#define putpmsg(...) (pthread_testcancel(), putpmsg(__VA_ARGS__))
#define pwrite(...) (pthread_testcancel(), pwrite(__VA_ARGS__))
#define read(...) (pthread_testcancel(), read(__VA_ARGS__))
#define readv(...) (pthread_testcancel(), readv(__VA_ARGS__))
#define recv(...) (pthread_testcancel(), recv(__VA_ARGS__))
#define recvfrom(...) (pthread_testcancel(), recvfrom(__VA_ARGS__))
#define recvmsg(...) (pthread_testcancel(), recvmsg(__VA_ARGS__))
#define select(...) (pthread_testcancel(), select(__VA_ARGS__))
#define sem_timedwait(...) (pthread_testcancel(), sem_timedwait(__VA_ARGS__))
#define sem_wait(...) (pthread_testcancel(), sem_wait(__VA_ARGS__))
#define send(...) (pthread_testcancel(), send(__VA_ARGS__))
#define sendmsg(...) (pthread_testcancel(), sendmsg(__VA_ARGS__))
#define sendto(...) (pthread_testcancel(), sendto(__VA_ARGS__))
#define sigpause(...) (pthread_testcancel(), sigpause(__VA_ARGS__))
#define sigsuspend(...) (pthread_testcancel(), sigsuspend(__VA_ARGS__))
#define sigwait(...) (pthread_testcancel(), sigwait(__VA_ARGS__))
#define sigwaitinfo(...) (pthread_testcancel(), sigwaitinfo(__VA_ARGS__))
#define sleep(...) (pthread_testcancel(), sleep(__VA_ARGS__))
//#define Sleep(...) (pthread_testcancel(), Sleep(__VA_ARGS__))
#define system(...) (pthread_testcancel(), system(__VA_ARGS__))


#define access(...) (pthread_testcancel(), access(__VA_ARGS__))
#define asctime(...) (pthread_testcancel(), asctime(__VA_ARGS__))
#define asctime_r(...) (pthread_testcancel(), asctime_r(__VA_ARGS__))
#define catclose(...) (pthread_testcancel(), catclose(__VA_ARGS__))
#define catgets(...) (pthread_testcancel(), catgets(__VA_ARGS__))
#define catopen(...) (pthread_testcancel(), catopen(__VA_ARGS__))
#define closedir(...) (pthread_testcancel(), closedir(__VA_ARGS__))
#define closelog(...) (pthread_testcancel(), closelog(__VA_ARGS__))
#define ctermid(...) (pthread_testcancel(), ctermid(__VA_ARGS__))
#define ctime(...) (pthread_testcancel(), ctime(__VA_ARGS__))
#define ctime_r(...) (pthread_testcancel(), ctime_r(__VA_ARGS__))
#define dbm_close(...) (pthread_testcancel(), dbm_close(__VA_ARGS__))
#define dbm_delete(...) (pthread_testcancel(), dbm_delete(__VA_ARGS__))
#define dbm_fetch(...) (pthread_testcancel(), dbm_fetch(__VA_ARGS__))
#define dbm_nextkey(...) (pthread_testcancel(), dbm_nextkey(__VA_ARGS__))
#define dbm_open(...) (pthread_testcancel(), dbm_open(__VA_ARGS__))
#define dbm_store(...) (pthread_testcancel(), dbm_store(__VA_ARGS__))
#define dlclose(...) (pthread_testcancel(), dlclose(__VA_ARGS__))
#define dlopen(...) (pthread_testcancel(), dlopen(__VA_ARGS__))
#define endgrent(...) (pthread_testcancel(), endgrent(__VA_ARGS__))
#define endhostent(...) (pthread_testcancel(), endhostent(__VA_ARGS__))
#define endnetent(...) (pthread_testcancel(), endnetent(__VA_ARGS__))
#define endprotoent(...) (pthread_testcancel(), endprotoend(__VA_ARGS__))
#define endpwent(...) (pthread_testcancel(), endpwent(__VA_ARGS__))
#define endservent(...) (pthread_testcancel(), endservent(__VA_ARGS__))
#define endutxent(...) (pthread_testcancel(), endutxent(__VA_ARGS__))
#define fclose(...) (pthread_testcancel(), fclose(__VA_ARGS__))
#define fflush(...) (pthread_testcancel(), fflush(__VA_ARGS__))
#define fgetc(...) (pthread_testcancel(), fgetc(__VA_ARGS__))
#define fgetpos(...) (pthread_testcancel(), fgetpos(__VA_ARGS__))
#define fgets(...) (pthread_testcancel(), fgets(__VA_ARGS__))
#define fgetwc(...) (pthread_testcancel(), fgetwc(__VA_ARGS__))
#define fgetws(...) (pthread_testcancel(), fgetws(__VA_ARGS__))
#define fmtmsg(...) (pthread_testcancel(), fmtmsg(__VA_ARGS__))
#define fopen(...) (pthread_testcancel(), fopen(__VA_ARGS__))
#define fpathconf(...) (pthread_testcancel(), fpathconf(__VA_ARGS__))
#define fprintf(...) (pthread_testcancel(), fprintf(__VA_ARGS__))
#define fputc(...) (pthread_testcancel(), fputc(__VA_ARGS__))
#define fputs(...) (pthread_testcancel(), fputs(__VA_ARGS__))
#define fputwc(...) (pthread_testcancel(), fputwc(__VA_ARGS__))
#define fputws(...) (pthread_testcancel(), fputws(__VA_ARGS__))
#define fread(...) (pthread_testcancel(), fread(__VA_ARGS__))
#define freopen(...) (pthread_testcancel(), freopen(__VA_ARGS__))
#define fscanf(...) (pthread_testcancel(), fscanf(__VA_ARGS__))
#define fseek(...) (pthread_testcancel(), fseek(__VA_ARGS__))
#define fseeko(...) (pthread_testcancel(), fseeko(__VA_ARGS__))
#define fsetpos(...) (pthread_testcancel(), fsetpos(__VA_ARGS__))
#define fstat(...) (pthread_testcancel(), fstat(__VA_ARGS__))
#define ftell(...) (pthread_testcancel(), ftell(__VA_ARGS__))
#define ftello(...) (pthread_testcancel(), ftello(__VA_ARGS__))
#define ftw(...) (pthread_testcancel(), ftw(__VA_ARGS__))
#define fwprintf(...) (pthread_testcancel(), fwprintf(__VA_ARGS__))
#define fwrite(...) (pthread_testcancel(), fwrite(__VA_ARGS__))
#define fwscanf(...) (pthread_testcancel(), fwscanf(__VA_ARGS__))
#define getaddrinfo(...) (pthread_testcancel(), getaddrinfo(__VA_ARGS__))
#define getc(...) (pthread_testcancel(), getc(__VA_ARGS__))
#define getc_unlocked(...) (pthread_testcancel(), getc_unlocked(__VA_ARGS__))
#define getchar(...) (pthread_testcancel(), getchar(__VA_ARGS__))
#define getchar_unlocked(...) (pthread_testcancel(), getchar_unlocked(__VA_ARGS__))
#define getcwd(...) (pthread_testcancel(), getcwd(__VA_ARGS__))
#define getdate(...) (pthread_testcancel(), getdate(__VA_ARGS__))
#define getgrent(...) (pthread_testcancel(), getgrent(__VA_ARGS__))
#define getgrgid(...) (pthread_testcancel(), getgrgid(__VA_ARGS__))
#define getgrgid_r(...) (pthread_testcancel(), getgrgid_r(__VA_ARGS__))
#define gergrnam(...) (pthread_testcancel(), getgrnam(__VA_ARGS__))
#define getgrnam_r(...) (pthread_testcancel(), getgrnam_r(__VA_ARGS__))
#define gethostbyaddr(...) (pthread_testcancel(), gethostbyaddr(__VA_ARGS__))
#define gethostbyname(...) (pthread_testcancel(), gethostbyname(__VA_ARGS__))
#define gethostent(...) (pthread_testcancel(), gethostent(__VA_ARGS__))
#define gethostid(...) (pthread_testcancel(), gethostid(__VA_ARGS__))
#define gethostname(...) (pthread_testcancel(), gethostname(__VA_ARGS__))
#define getlogin(...) (pthread_testcancel(), getlogin(__VA_ARGS__))
#define getlogin_r(...) (pthread_testcancel(), getlogin_r(__VA_ARGS__))
#define getnameinfo(...) (pthread_testcancel(), getnameinfo(__VA_ARGS__))
#define getnetbyaddr(...) (pthread_testcancel(), getnetbyaddr(__VA_ARGS__))
#define getnetbyname(...) (pthread_testcancel(), getnetbyname(__VA_ARGS__))
#define getnetent(...) (pthread_testcancel(), getnetent(__VA_ARGS__))
#define getopt(...) (pthread_testcancel(), getopt(__VA_ARGS__))
#define getprotobyname(...) (pthread_testcancel(), getprotobyname(__VA_ARGS__))
#define getprotobynumber(...) (pthread_testcancel(), getprotobynumber(__VA_ARGS__))
#define getprotoent(...) (pthread_testcancel(), getprotoent(__VA_ARGS__))
#define getpwent(...) (pthread_testcancel(), getpwent(__VA_ARGS__))
#define getpwnam(...) (pthread_testcancel(), getpwnam(__VA_ARGS__))
#define getpwnam_r(...) (pthread_testcancel(), getpwnam_r(__VA_ARGS__))
#define getpwuid(...) (pthread_testcancel(), getpwuid(__VA_ARGS__))
#define getpwuid_r(...) (pthread_testcancel(), getpwuid_r(__VA_ARGS__))
#define gets(...) (pthread_testcancel(), gets(__VA_ARGS__))
#define getservbyname(...) (pthread_testcancel(), getservbyname(__VA_ARGS__))
#define getservbyport(...) (pthread_testcancel(), getservbyport(__VA_ARGS__))
#define getservent(...) (pthread_testcancel(), getservent(__VA_ARGS__))
#define getutxent(...) (pthread_testcancel(), getutxent(__VA_ARGS__))
#define getutxid(...) (pthread_testcancel(), getutxid(__VA_ARGS__))
#define getutxline(...) (pthread_testcancel(), getutxline(__VA_ARGS__))
#undef getwc
#define getwc(...) (pthread_testcancel(), getwc(__VA_ARGS__))
#undef getwchar
#define getwchar(...) (pthread_testcancel(), getwchar(__VA_ARGS__))
#define getwd(...) (pthread_testcancel(), getwd(__VA_ARGS__))
#define glob(...) (pthread_testcancel(), glob(__VA_ARGS__))
#define iconv_close(...) (pthread_testcancel(), iconv_close(__VA_ARGS__))
#define iconv_open(...) (pthread_testcancel(), iconv_open(__VA_ARGS__))
#define ioctl(...) (pthread_testcancel(), ioctl(__VA_ARGS__))
#define link(...) (pthread_testcancel(), link(__VA_ARGS__))
#define localtime(...) (pthread_testcancel(), localtime(__VA_ARGS__))
#define localtime_r(...) (pthread_testcancel(), localtime_r(__VA_ARGS__))
#define lseek(...) (pthread_testcancel(), lseek(__VA_ARGS__))
#define lstat(...) (pthread_testcancel(), lstat(__VA_ARGS__))
#define mkstemp(...) (pthread_testcancel(), mkstemp(__VA_ARGS__))
#define nftw(...) (pthread_testcancel(), nftw(__VA_ARGS__))
#define opendir(...) (pthread_testcancel(), opendir(__VA_ARGS__))
#define openlog(...) (pthread_testcancel(), openlog(__VA_ARGS__))
#define pathconf(...) (pthread_testcancel(), pathconf(__VA_ARGS__))
#define pclose(...) (pthread_testcancel(), pclose(__VA_ARGS__))
#define perror(...) (pthread_testcancel(), perror(__VA_ARGS__))
#define popen(...) (pthread_testcancel(), popen(__VA_ARGS__))
#define posix_fadvise(...) (pthread_testcancel(), posix_fadvise(__VA_ARGS__))
#define posix_fallocate(...) (pthread_testcancel(), posix_fallocate(__VA_ARGS__))
#define posix_madvise(...) (pthread_testcancel(), posix_madvise(__VA_ARGS__))
#define posix_openpt(...) (pthread_testcancel(), posix_openpt(__VA_ARGS__))
#define posix_spawn(...) (pthread_testcancel(), posix_spawn(__VA_ARGS__))
#define posix_spawnp(...) (pthread_testcancel(), posix_spawnp(__VA_ARGS__))
#define posix_trace_clear(...) (pthread_testcancel(), posix_trace_clear(__VA_ARGS__))
#define posix_trace_close(...) (pthread_testcancel(), posix_trace_close(__VA_ARGS__))
#define posix_trace_create(...) (pthread_testcancel(), posix_trace_create(__VA_ARGS__))
#define posix_trace_create_withlog(...) (pthread_testcancel(), posix_trace_create_withlog(__VA_ARGS__))
#define posix_trace_eventtypelist_getne(...) (pthread_testcancel(), posix_trace_eventtypelist_getne(__VA_ARGS__))
#define posix_trace_eventtypelist_rewin(...) (pthread_testcancel(), posix_trace_eventtypelist_rewin(__VA_ARGS__))
#define posix_trace_flush(...) (pthread_testcancel(), posix_trace_flush(__VA_ARGS__))
#define posix_trace_get_attr(...) (pthread_testcancel(), posix_trace_get_attr(__VA_ARGS__))
#define posix_trace_get_filter(...) (pthread_testcancel(), posix_trace_get_filter(__VA_ARGS__))
#define posix_trace_get_status(...) (pthread_testcancel(), posix_trace_get_status(__VA_ARGS__))
#define posix_trace_getnext_event(...) (pthread_testcancel(), posix_trace_getnext_event(__VA_ARGS__))
#define posix_trace_open(...) (pthread_testcancel(), posix_trace_open(__VA_ARGS__))
#define posix_trace_rewind(...) (pthread_testcancel(), posix_trace_rewind(__VA_ARGS__))
#define posix_trace_setfilter(...) (pthread_testcancel(), posix_trace_setfilter(__VA_ARGS__))
#define posix_trace_shutdown(...) (pthread_testcancel(), posix_trace_shutdown(__VA_ARGS__))
#define posix_trace_timedgetnext_event(...) (pthread_testcancel(), posix_trace_timedgetnext_event(__VA_ARGS__))
#define posix_typed_mem_open(...) (pthread_testcancel(), posix_typed_mem_open(__VA_ARGS__))
#define printf(...) (pthread_testcancel(), printf(__VA_ARGS__))
#define putc(...) (pthread_testcancel(), putc(__VA_ARGS__))
#define putc_unlocked(...) (pthread_testcancel(), putc_unlocked(__VA_ARGS__))
#define putchar(...) (pthread_testcancel(), putchar(__VA_ARGS__))
#define putchar_unlocked(...) (pthread_testcancel(), putchar_unlocked(__VA_ARGS__))
#define puts(...) (pthread_testcancel(), puts(__VA_ARGS__))
#define pututxline(...) (pthread_testcancel(), pututxline(__VA_ARGS__))
#undef putwc
#define putwc(...) (pthread_testcancel(), putwc(__VA_ARGS__))
#undef putwchar
#define putwchar(...) (pthread_testcancel(), putwchar(__VA_ARGS__))
#define readdir(...) (pthread_testcancel(), readdir(__VA_ARSG__))
#define readdir_r(...) (pthread_testcancel(), readdir_r(__VA_ARGS__))
#define remove(...) (pthread_testcancel(), remove(__VA_ARGS__))
#define rename(...) (pthread_testcancel(), rename(__VA_ARGS__))
#define rewind(...) (pthread_testcancel(), rewind(__VA_ARGS__))
#define rewinddir(...) (pthread_testcancel(), rewinddir(__VA_ARGS__))
#define scanf(...) (pthread_testcancel(), scanf(__VA_ARGS__))
#define seekdir(...) (pthread_testcancel(), seekdir(__VA_ARGS__))
#define semop(...) (pthread_testcancel(), semop(__VA_ARGS__))
#define setgrent(...) (pthread_testcancel(), setgrent(__VA_ARGS__))
#define sethostent(...) (pthread_testcancel(), sethostemt(__VA_ARGS__))
#define setnetent(...) (pthread_testcancel(), setnetent(__VA_ARGS__))
#define setprotoent(...) (pthread_testcancel(), setprotoent(__VA_ARGS__))
#define setpwent(...) (pthread_testcancel(), setpwent(__VA_ARGS__))
#define setservent(...) (pthread_testcancel(), setservent(__VA_ARGS__))
#define setutxent(...) (pthread_testcancel(), setutxent(__VA_ARGS__))
#define stat(...) (pthread_testcancel(), stat(__VA_ARGS__))
#define strerror(...) (pthread_testcancel(), strerror(__VA_ARGS__))
#define strerror_r(...) (pthread_testcancel(), strerror_r(__VA_ARGS__))
#define strftime(...) (pthread_testcancel(), strftime(__VA_ARGS__))
#define symlink(...) (pthread_testcancel(), symlink(__VA_ARGS__))
#define sync(...) (pthread_testcancel(), sync(__VA_ARGS__))
#define syslog(...) (pthread_testcancel(), syslog(__VA_ARGS__))
#define tmpfile(...) (pthread_testcancel(), tmpfile(__VA_ARGS__))
#define tmpnam(...) (pthread_testcancel(), tmpnam(__VA_ARGS__))
#define ttyname(...) (pthread_testcancel(), ttyname(__VA_ARGS__))
#define ttyname_r(...) (pthread_testcancel(), ttyname_r(__VA_ARGS__))
#define tzset(...) (pthread_testcancel(), tzset(__VA_ARGS__))
#define ungetc(...) (pthread_testcancel(), ungetc(__VA_ARGS__))
#define ungetwc(...) (pthread_testcancel(), ungetwc(__VA_ARGS__))
#define unlink(...) (pthread_testcancel(), unlink(__VA_ARGS__))
#define vfprintf(...) (pthread_testcancel(), vfprintf(__VA_ARGS__))
#define vfwprintf(...) (pthread_testcancel(), vfwprintf(__VA_ARGS__))
#define vprintf(...) (pthread_testcancel(), vprintf(__VA_ARGS__))
#define vwprintf(...) (pthread_testcancel(), vwprintf(__VA_ARGS__))
#define wcsftime(...) (pthread_testcancel(), wcsftime(__VA_ARGS__))
#define wordexp(...) (pthread_testcancel(), wordexp(__VA_ARGS__))
#define wprintf(...) (pthread_testcancel(), wprintf(__VA_ARGS__))
#define wscanf(...) (pthread_testcancel(), wscanf(__VA_ARGS__))
#endif

#endif /* WIN_PTHREADS */
