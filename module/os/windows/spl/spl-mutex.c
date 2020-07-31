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

#include <sys/mutex.h>
#include <string.h>
#include <sys/debug.h>
#include <sys/thread.h>
#include <sys/types.h>
#include <fltkernel.h>

uint64_t zfs_active_mutex = 0;

#define MUTEX_INITIALISED 0x23456789
#define MUTEX_DESTROYED 0x98765432

int spl_mutex_subsystem_init(void)
{
	return 0;
}

void spl_mutex_subsystem_fini(void)
{

}

void spl_mutex_init(kmutex_t *mp, char *name, kmutex_type_t type, void *ibc)
{
	(void)name;
	ASSERT(type != MUTEX_SPIN);
	ASSERT(ibc == NULL);

	if (mp->initialised == MUTEX_INITIALISED)
		panic("%s: mutex already initialised\n", __func__);
	mp->initialised = MUTEX_INITIALISED;
	mp->set_event_guard = 0;

	mp->m_owner = NULL;

	// Initialise it to 'Signaled' as mutex is 'free'.
	KeInitializeEvent((PRKEVENT)&mp->m_lock, SynchronizationEvent, TRUE); 
	atomic_inc_64(&zfs_active_mutex);
}

void spl_mutex_destroy(kmutex_t *mp)
{
	if (!mp) return;

	if (mp->initialised != MUTEX_INITIALISED) 
		panic("%s: mutex not initialised\n", __func__);

	// Make sure any call to KeSetEvent() has completed.
	while (mp->set_event_guard != 0) {
		kpreempt(KPREEMPT_SYNC);
	}

	mp->initialised = MUTEX_DESTROYED;

	if (mp->m_owner != 0) 
		panic("SPL: releasing held mutex");

	// There is no FREE member for events
	// KeDeleteEvent();

	atomic_dec_64(&zfs_active_mutex);
}

void spl_mutex_enter(kmutex_t *mp)
{
	NTSTATUS Status;
	kthread_t *thisthread = current_thread();

	if (mp->initialised != MUTEX_INITIALISED)
		panic("%s: mutex not initialised\n", __func__);
	
	if (mp->m_owner == thisthread)
		panic("mutex_enter: locking against myself!");

	VERIFY3P(mp->m_owner, != , 0xdeadbeefdeadbeef);

	// Test if "m_owner" is NULL, if so, set it to "thisthread".
	// Returns original value, so if NULL, it succeeded.
again:
	if (InterlockedCompareExchangePointer(&mp->m_owner, 
		thisthread, NULL) != NULL) {

		// Failed to CAS-in 'thisthread', as owner was not NULL
		// Wait forever for event to be signaled.
		Status = KeWaitForSingleObject(
			(PRKEVENT)&mp->m_lock,
			Executive,
			KernelMode,
			FALSE,
			NULL
		);

		// We waited, but someone else may have beaten us to it
		// so we need to attempt CAS again
		goto again;
	}

	ASSERT(mp->m_owner == thisthread);
}

void spl_mutex_exit(kmutex_t *mp)
{
	if (mp->m_owner != current_thread())
		panic("%s: releasing not held/not our lock?\n", __func__);

	VERIFY3P(mp->m_owner, != , 0xdeadbeefdeadbeef);

	atomic_inc_32(&mp->set_event_guard);

	mp->m_owner = NULL;

	VERIFY3U(KeGetCurrentIrql(), <= , DISPATCH_LEVEL);

	// Wake up one waiter now that it is available.
	KeSetEvent((PRKEVENT)&mp->m_lock, SEMAPHORE_INCREMENT, FALSE);
	atomic_dec_32(&mp->set_event_guard);
}

int spl_mutex_tryenter(kmutex_t *mp)
{
	LARGE_INTEGER timeout;
	NTSTATUS Status;
	kthread_t *thisthread = current_thread();

	if (mp->initialised != MUTEX_INITIALISED)
		panic("%s: mutex not initialised\n", __func__);

	if (mp->m_owner == thisthread)
		panic("mutex_tryenter: locking against myself!");

	// Test if "m_owner" is NULL, if so, set it to "thisthread".
	// Returns original value, so if NULL, it succeeded.
	if (InterlockedCompareExchangePointer(&mp->m_owner,
		thisthread, NULL) != NULL) {
		return 0; // Not held.
	}

	ASSERT(mp->m_owner == thisthread);

	// held
	return (1);
}

int spl_mutex_owned(kmutex_t *mp)
{
	return (mp->m_owner == current_thread());
}

struct kthread *spl_mutex_owner(kmutex_t *mp)
{
	return (mp->m_owner);
}
