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
 * Copyright (C) 2017,2019 Jorgen Lundman <lundman@lundman.net>
 * Portions Copyright 2022 Andrew Innes <andrew.c12@gmail.com>
 *
 */

/*
 * Implementation details.
 * Using SynchronizationEvent that autoresets. When in 'Signaled'
 * state the mutex is considered FREE/Available to be locked.
 * Call KeWaitForSingleObject() to wait for it to be made
 * 'available' (either blocking, or polling for *Try method)
 * Calling KeSetEvent() sets event to Signaled, and wakes 'one'
 * waiter, before Clearing it again.
 * We attempt to avoid calling KeWaitForSingleObject() by
 * using atomic CAS on m_owner, in the simple cases.
 */

#include <sys/atomic.h>
#include <sys/mutex.h>
#include <string.h>
#include <sys/debug.h>
#include <sys/thread.h>
#include <sys/types.h>
#include <fltkernel.h>

uint64_t zfs_active_mutex = 0;

#define	MUTEX_INITIALISED 0x23456789
#define	MUTEX_DESTROYED 0x98765432

int
spl_mutex_subsystem_init(void)
{
	return (0);
}

void
spl_mutex_subsystem_fini(void)
{

}

void
spl_mutex_init(kmutex_t *mp, char *name, kmutex_type_t type, void *ibc)
{
	(void) name;
	ASSERT(type != MUTEX_SPIN);
	ASSERT(ibc == NULL);

	if (mp->m_initialised == MUTEX_INITIALISED)
		panic("%s: mutex already m_initialised\n", __func__);
	mp->m_initialised = MUTEX_INITIALISED;
	KeInitializeSpinLock(&mp->m_destroy_lock);

	mp->m_owner = NULL;
	mp->m_waiters = 0;

	// Initialise it to 'Signaled' as mutex is 'free'.
	KeInitializeEvent((PRKEVENT)&mp->m_lock, SynchronizationEvent, TRUE);
	atomic_inc_64(&zfs_active_mutex);
}

void
spl_mutex_destroy(kmutex_t *mp)
{
	KIRQL oldq;

	if (!mp)
		return;

	if (mp->m_initialised != MUTEX_INITIALISED)
		panic("%s: mutex not m_initialised\n", __func__);

	if (mp->m_owner != 0)
		panic("SPL: releasing held mutex");

	// Make sure any call to KeSetEvent() has completed.
	KeAcquireSpinLock(&mp->m_destroy_lock, &oldq);
	mp->m_initialised = MUTEX_DESTROYED;
	KeReleaseSpinLock(&mp->m_destroy_lock, oldq);

	// There is no FREE member for events
	// KeDeleteEvent();

	atomic_dec_64(&zfs_active_mutex);
}

void
spl_mutex_enter(kmutex_t *mp)
{
	kthread_t *thisthread = current_thread();

	if (mp->m_initialised != MUTEX_INITIALISED)
		panic("%s: mutex not m_initialised\n", __func__);

	if (mp->m_owner == thisthread)
		panic("mutex_enter: locking against myself!");

	VERIFY3P(mp->m_owner, !=, 0xdeadbeefdeadbeef);

	/*
	 * Fast path: uncontested acquire — single CAS, no kernel objects.
	 */
	if (InterlockedCompareExchangePointer(&mp->m_owner,
	    thisthread, NULL) == NULL) {
		ASSERT(mp->m_owner == thisthread);
		return;
	}

	/*
	 * Slow path: register as a waiter so that mutex_exit knows it must
	 * signal the event.  We increment before the retry loop so there is
	 * no window where we sleep but the exiting thread skips the signal.
	 */
	atomic_inc_32(&mp->m_waiters);
again:
	if (InterlockedCompareExchangePointer(&mp->m_owner,
	    thisthread, NULL) != NULL) {

		/* Failed to CAS-in 'thisthread'; sleep until signaled. */
		(void) KeWaitForSingleObject(
		    (PRKEVENT)&mp->m_lock,
		    Executive,
		    KernelMode,
		    FALSE,
		    NULL);

		/* Someone else may have beaten us; retry CAS. */
		goto again;
	}
	atomic_dec_32(&mp->m_waiters);
	ASSERT(mp->m_owner == thisthread);
}

void
spl_mutex_exit(kmutex_t *mp)
{
	if (mp->m_owner != current_thread())
		panic("%s: releasing not held/not our lock?\n", __func__);

	VERIFY3P(mp->m_owner, !=, 0xdeadbeefdeadbeef);

	/*
	 * Release ownership with a full memory barrier so that any thread
	 * which subsequently reads m_owner == NULL is guaranteed to also see
	 * all stores made while the mutex was held.
	 */
	(void) InterlockedExchangePointer(&mp->m_owner, NULL);

	/*
	 * Only pay the spinlock + KeSetEvent cost when a thread is actually
	 * sleeping in the slow path.  In the common uncontested case this
	 * makes mutex_exit a single interlocked exchange — no kernel objects.
	 *
	 * The m_destroy_lock spinlock still guards against a racing
	 * mutex_destroy() tearing down the KEVENT while we signal it.
	 */
	if (mp->m_waiters > 0) {
		KIRQL oldq;
		KeAcquireSpinLock(&mp->m_destroy_lock, &oldq);
		KeSetEvent((PRKEVENT)&mp->m_lock, SEMAPHORE_INCREMENT, FALSE);
		KeReleaseSpinLock(&mp->m_destroy_lock, oldq);
	}

	VERIFY3U(KeGetCurrentIrql(), <=, DISPATCH_LEVEL);
}

int
spl_mutex_tryenter(kmutex_t *mp)
{
	// LARGE_INTEGER timeout;
	// NTSTATUS Status;
	kthread_t *thisthread = current_thread();

	if (mp->m_initialised != MUTEX_INITIALISED)
		panic("%s: mutex not m_initialised\n", __func__);



	// Test if "m_owner" is NULL, if so, set it to "thisthread".
	// Returns original value, so if NULL, it succeeded.
	if (InterlockedCompareExchangePointer(&mp->m_owner,
	    thisthread, NULL) != NULL) {
		return (0); // Not held.
	}

	ASSERT(mp->m_owner == thisthread);

	// held
	return (1);
}

int
spl_mutex_owned(kmutex_t *mp)
{
	return (mp->m_owner == current_thread());
}

kthread_t *
spl_mutex_owner(kmutex_t *mp)
{
	return (mp->m_owner);
}
