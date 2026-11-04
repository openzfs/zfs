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
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2016 Actifio, Inc. All rights reserved.
 * Copyright (c) 2025, Klara, Inc.
 * Copyright (c) 2026, TrueNAS.
 */

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/thread.h>
#include <sys/sysmacros.h>
#include <sys/errno.h>
#include <sys/kmem.h>

/*
 * This implements the SPL "kthread" API via POSIX threads. The basics of these
 * two thread models are the same, but there are a few important mismatches
 * that we have to deal with.
 *
 * ## Thread context
 *
 * A kthread is represented by a context object, kthread_t. This context object
 * is matched to the entire lifetime of the thread: init, run, teardown. This
 * is largely related to its heritage; in the kernel, it usually represents a
 * process or task, which can be scheduled, run, stopped, interrupted, etc.
 *
 * The pthread equivalent, pthread_t, is an opaque ID representing a thread of
 * execution, very similar in concept to a process ID. Just as a process ID may
 * represent a process that no longer exists, or even a different process
 * entirely if the ID is reused, so it is with pthread_t. It's hard to even
 * detect this, as a pthread_t is totally opaque and so has no integral value,
 * and the portable API to interrogate the thread state is fairly limited.
 *
 * This distinction is important, as there are other SPL primitives that have a
 * notion of "owning thread" (eg kmutex_t), which means that there must be some
 * stable kthread_t identity that remains valid while any teardown/cleanup code
 * runs, so it can actually make use of those primitives (eg take a lock).
 *
 * Separately, POSIX doesn't require pthread_t to have any particular structure
 * - it could be a struct or a scalar type, and it could be larger than a
 * single pointer.
 *
 * All together, this means that the obvious path of simply casting pthread_t
 * to kthread_t can't work. Instead, we have to build an identity whose
 * lifetime we control, independently of pthread_t, such that a running thread
 * can reference itself (eg `curthread`) without knowing that it's running
 * inside the pthread runtime.
 *
 * ## Lifetime
 *
 * Under pthreads, thread creation (via pthread_create()) and execution (the
 * actual function running in the thread, in our case zk_thread_trampoline())
 * are decoupled. When pthread_create() returns, the thread may have already
 * started executing, or may be waiting. Similarly, for a detached thread, when
 * pthread_create() returns the thread may have already completed.
 *
 * This is why we cannot simply use a thread-local variable for its kthread_t:
 * we need something we can return from zk_thread_create(), but we can't obtain
 * a pointer to thread-local storage until the thread exists!
 *
 * Instead, we create a kthread_t on the heap in zk_thread_create(), so we can
 * return it, and we also hand it to pthread_create() to pass on to
 * zk_thread_trampoline(), so it can set zk_thread_current and so know its own
 * identity via zk_thread_self().
 *
 * ## Joinable threads
 *
 * libspl's threads also need to support "joining", that is, block until the
 * thread finishes, or if it has, clean it up. This has the challenge that the
 * underlying thread may be alive at the time of the call, or it may have
 * completed long ago. This means we need to understand which mode the thread
 * is operating in, and so when destruction of the kthread_t* will happen -
 * automatically when the thread run exits, or after it completes and
 * zk_thread_join() is called.
 *
 * This is mostly a matter of wrapping pthread_join(), and then understanding
 * the shutdown ordering, so we know when to clean up.
 *
 * ## Foreign threads
 *
 * In the kernel, kthread_t is mapped 1:1 to a kernel's thread or task object,
 * and so all threads of execution are valid kthreads, and so can be used as
 * "owning threads". In userspace, there may be other pthreads that are not
 * kthreads, that still want to use SPL primitives (eg kmutex_t). Even if there
 * are no explicit threads, main() still counts!
 *
 * To handle this, if we get a call to `zk_thread_self()` and we don't have a
 * kthread_t* (that is, zk_thread_current is NULL), we know it is a "foreign"
 * thread and we return a pointer to a dummy thread-local kthread_t,
 * zk_thread_local. This can't do anything, but the pointer is guaranteed to
 * be live until the thread exits, and so can be used to "own" other resources.
 *
 * ## Teardown
 *
 * Various parts of the SPL register thread teardown callbacks (eg pthread_key
 * destructors), which will run at thread exit. pthreads guarantee they will
 * run on the exiting thread with any thread-local storage (ie __thread
 * variables) still intact. This will occur _after_ our cleanup function
 * zk_thread_cleanup() runs, which for detached threads destroys the kthread_t.
 * However, those destructors may also want to use "owned" SPL primitives, so
 * they need zk_thread_self() to return something that remains stable until the
 * true thread death.
 *
 * To handle this, when our cleanup function is called, we simply reset
 * zk_thread_current to NULL. Any future calls to zk_thread_self() on that
 * thread will be treated like a foreign thread, and return zk_thread_local
 * instead, ensuring a stable identity.
 *
 * Note that joinable threads don't take this path; their kthread_t stays alive
 * until zk_thread_join() is called, and cleaned up directly.
 */

/* kthread_t. */
struct kthread {
	/*
	 * True if created with TS_JOINABLE; if so, zk_thread_join() must be
	 * called to finish and destroy.
	 */
	bool		t_joinable;

	/*
	 * Underlying pthread_t, for pthread_join() only. Valid iff t_joinable
	 * is true.
	 */
	pthread_t	t_tid;

	/* Thread name, optional. Allocated at create, set in trampoline. */
	char		*t_name;

	/* User thread entry point and arg. */
	kthread_func_t	t_func;
	void		*t_arg;
};

/*
 * kthread_t* for this thread. Allocated in zk_thread_create(), set when it
 * starts running in zk_thread_trampoline(). Always NULL for foreign threads,
 * which never went through zk_thread_trampoline().
 */
static __thread kthread_t *zk_thread_current;

/*
 * Stub TLS thread, returned by zk_thread_self() when zk_thread_current is
 * NULL, which means either a detached thread has finished and gone through
 * zk_thread_cleanup(), or the thread is foreign to us. Having a valid
 * kthread_t* allows ownership to keep working until the thread is torn down.
 */
static __thread kthread_t zk_thread_local;

/* Helper; release kthread_t memory. */
static void
zk_thread_free(kthread_t *t)
{
	if (t->t_name != NULL)
		kmem_strfree(t->t_name);
	kmem_free(t, sizeof (kthread_t));
}

/*
 * Thread cleanup; called when a thread exits, set up with
 * pthread_cleanup_push() in zk_thread_trampoline().
 *
 * Joinable threads will come through here but do nothing, since they'll be
 * freed from zk_thread_join().
 */
static void
zk_thread_cleanup(void *arg)
{
	kthread_t *t = arg;
	if (!t->t_joinable) {
		zk_thread_current = NULL;
		zk_thread_free(t);
	}
}

/* Thread entry. */
static void *
zk_thread_trampoline(void *arg)
{
	kthread_t *t = arg;

	/* Set kthread pointer for zk_thread_self() to find. */
	zk_thread_current = t;

	/*
	 * Set thread name. Note that we use pthread_self(); t->t_tid is not
	 * set until pthread_create() returns, which may not have happened yet.
	 */
	if (t->t_name != NULL)
		pthread_setname_np(pthread_self(), t->t_name);

	/*
	 * Set the cleanup function. t_func may call thread_exit(), and so we
	 * may never return here, but the pthread API guarantees that stacked
	 * cleanup functions will be called on thread exit or cancellation.
	 */
	pthread_cleanup_push(zk_thread_cleanup, t);

	/* Good luck soldier. */
	t->t_func(t->t_arg);

	/* Normal return, pop the cleanup function and run it. */
	pthread_cleanup_pop(true);

	return (NULL);
}

/*
 * TS_STACK_MIN is dictated by the minimum allowed pthread stack size.  While
 * TS_STACK_MAX is somewhat arbitrary, it was selected to be large enough for
 * the expected stack depth while small enough to avoid exhausting address
 * space with high thread counts.
 */
#define	TS_STACK_MIN	MAX(PTHREAD_STACK_MIN, 32768)
#define	TS_STACK_MAX	(256 * 1024)

/*
 * We allow the default stack size in user space to be specified by setting the
 * ZFS_STACK_SIZE environment variable. This allows us the convenience of
 * observing and debugging stack overruns in user space. Explicitly specified
 * stack sizes will be honored. The usage of ZFS_STACK_SIZE is discussed
 * further in the ENVIRONMENT VARIABLES sections of the ztest(1) man page.
 */
static size_t zk_thread_stack_size = TS_STACK_MAX;
static pthread_once_t zk_thread_stack_size_once = PTHREAD_ONCE_INIT;

/* One-time initializer for default stack size. */
static void
zk_thread_stack_size_init(void)
{
	char *stkstr = getenv("ZFS_STACK_SIZE");
	if (stkstr != NULL)
		zk_thread_stack_size = MAX(atoi(stkstr), TS_STACK_MIN);
}

/* Create a new thread, return its kthread_t* */
kthread_t *
zk_thread_create(const char *name, kthread_func_t func, void *arg,
    size_t stksize, int state)
{
	if (stksize == 0) {
		/* Default stack size requested, set it from environment. */
		pthread_once(&zk_thread_stack_size_once,
		    zk_thread_stack_size_init);
		stksize = zk_thread_stack_size;
	}
	VERIFY3U(stksize, >, 0);
	stksize = P2ROUNDUP(MAX(stksize, TS_STACK_MIN), PAGESIZE);

	pthread_attr_t attr;
	VERIFY0(pthread_attr_init(&attr));

	/*
	 * If this ever fails, it may be because the stack size is not a
	 * multiple of system page size.
	 */
	VERIFY0(pthread_attr_setstacksize(&attr, stksize));
	VERIFY0(pthread_attr_setguardsize(&attr, PAGESIZE));

	bool joinable = (state & TS_JOINABLE);
	VERIFY0(pthread_attr_setdetachstate(&attr,
	    joinable ? PTHREAD_CREATE_JOINABLE : PTHREAD_CREATE_DETACHED));

	kthread_t *t = kmem_zalloc(sizeof (kthread_t), KM_SLEEP);
	t->t_joinable = joinable;
	t->t_name = kmem_strdup(name);
	t->t_func = func;
	t->t_arg = arg;

	/*
	 * Create the thread. The pthread API makes no guarantee about when the
	 * thread runs, and it may run and complete before pthread_create()
	 * returns. If it's not joinable, then the kthread may have already
	 * been freed. So we can't output the thread ID directly into t->t_tid;
	 * instead, it goes into a local variable, and then if it's joinable
	 * and we know the thread is still alive, we can record it. It doesn't
	 * matter for non-joinable threads, as we don't ever need the thread ID
	 * anyway.
	 */
	pthread_t tid;
	VERIFY0(pthread_create(&tid, &attr, zk_thread_trampoline, t));

	if (joinable)
		t->t_tid = tid;

	VERIFY0(pthread_attr_destroy(&attr));

	/*
	 * TODO: If the thread is detached, it may already be dead (see above).
	 * It would make more sense to return NULL here, however there are
	 * multiple callers that expect non-NULL to be returned. Auditing those
	 * call sites and fixing those (either removing the check/store, or
	 * making them joinable and then joining them) is a future project.
	 *   -- robn, 2026-10-20
	 */
	return (t);
}

/*
 * Get this thread's context. See the big comment above for the lifetime
 * requirements and how foreign and exiting threads work.
 */
kthread_t *
zk_thread_self(void)
{
	/*
	 * If we've already seen this thread before then we've cached the
	 * pointer; just return it.
	 */
	if (zk_thread_current != NULL)
		return (zk_thread_current);

	/*
	 * Foreign thread, or detached thread in cleanup after its kthread
	 * has been destroyed. Give it something that will be stable until it
	 * exits.
	 */
	return (&zk_thread_local);
}

/* Terminate ourselves immediately */
__attribute__((noreturn))
void
zk_thread_exit(void)
{
	pthread_exit(NULL);
}

/*
 * Wait for a joinable thread to complete, and then clean it up.
 *
 * Note that this is only available in libspl, and so really just a wrapper
 * around pthread_join(), including error codes. If it is ever generalised for
 * another thread backend we may need to describe its behaviour a little more
 * specifically.
 */
int
zk_thread_join(kthread_t *t)
{
	/*
	 * EINVAL is mostly matching pthread_join()'s behaviour for "unjoinable
	 * thread".
	 */
	if (t == NULL || !t->t_joinable)
		return (EINVAL);
	int err = pthread_join(t->t_tid, NULL);
	if (err == 0) {
		/* We are the last reference, so can free it. */
		zk_thread_free(t);
	}
	return (err);
}

/* this only exists to have its address taken */
void p0(void) {}
