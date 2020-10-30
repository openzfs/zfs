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
 * Copyright (C) 2018 Jorgen Lundman <lundman@lundman.net>
 *
 */

#include <sys/rwlock.h>
//#include <kern/debug.h>
#include <sys/atomic.h>
#include <sys/mutex.h>

uint64_t zfs_active_rwlock = 0;

/* We run rwlock with DEBUG on for now, as it protects against
 * uninitialised access etc, and almost no cost.
 */
#ifndef DEBUG
#define DEBUG
#endif

#ifdef DEBUG
int rw_isinit(krwlock_t *rwlp)
{
	if (rwlp->rw_pad != 0x012345678)
		return 0;
	return 1;
}
#endif


void
rw_init(krwlock_t *rwlp, char *name, krw_type_t type, /*__unused*/ void *arg)
{
    ASSERT(type != RW_DRIVER);

#ifdef DEBUG
	VERIFY3U(rwlp->rw_pad, != , 0x012345678);
#endif
	ExInitializeResourceLite(&rwlp->rw_lock);
    rwlp->rw_owner = NULL;
    rwlp->rw_readers = 0;
#ifdef DEBUG
	rwlp->rw_pad = 0x012345678;
#endif
	atomic_inc_64(&zfs_active_rwlock);
}

void
rw_destroy(krwlock_t *rwlp)
{
	// Confirm it was initialised, and is unlocked, and not already destroyed.
#ifdef DEBUG
	VERIFY3U(rwlp->rw_pad, == , 0x012345678);
#endif
	VERIFY3U(rwlp->rw_owner, ==, 0);
	VERIFY3U(rwlp->rw_readers, ==, 0);

	// This has caused panic due to IRQL panic, from taskq->zap_evict->rw_destroy
	ExDeleteResourceLite(&rwlp->rw_lock);
#ifdef DEBUG
	rwlp->rw_pad = 0x99;
#endif
	atomic_dec_64(&zfs_active_rwlock);
}

void
rw_enter(krwlock_t *rwlp, krw_t rw)
{
#ifdef DEBUG
	if (rwlp->rw_pad != 0x012345678)
		panic("rwlock %p not initialised\n", rwlp);
#endif

    if (rw == RW_READER) {
		ExAcquireResourceSharedLite(&rwlp->rw_lock, TRUE);
        atomic_inc_32((volatile uint32_t *)&rwlp->rw_readers);
        ASSERT(rwlp->rw_owner == 0);
    } else {
        if (rwlp->rw_owner == current_thread())
            panic("rw_enter: locking against myself!");
		ExAcquireResourceExclusiveLite(&rwlp->rw_lock, TRUE);
        ASSERT(rwlp->rw_owner == 0);
        ASSERT(rwlp->rw_readers == 0);
        rwlp->rw_owner = current_thread();
    }
}

/*
 * kernel private from osfmk/kern/locks.h
 */

int
rw_tryenter(krwlock_t *rwlp, krw_t rw)
{
    int held = 0;

#ifdef DEBUG
	if (rwlp->rw_pad != 0x012345678)
		panic("rwlock %p not initialised\n", rwlp);
#endif

    if (rw == RW_READER) {
		held = ExAcquireResourceSharedLite(&rwlp->rw_lock, FALSE);
        if (held)
            atomic_inc_32((volatile uint32_t *)&rwlp->rw_readers);
    } else {
        if (rwlp->rw_owner == current_thread())
            panic("rw_tryenter: locking against myself!");

		held = ExAcquireResourceExclusiveLite(&rwlp->rw_lock, FALSE);
        if (held)
            rwlp->rw_owner = current_thread();
    }

    return (held);
}



/*
 * It appears a difference between Darwin's
 * lck_rw_lock_shared_to_exclusive() and Solaris's rw_tryupgrade() and
 * FreeBSD's sx_try_upgrade() is that on failure to upgrade, the prior
 * held shared/reader lock is lost on Darwin, but retained on
 * Solaris/FreeBSD. We could re-acquire the lock in this situation,
 * but it enters a possibility of blocking, when tryupgrade is meant
 * to be non-blocking.
 * Also note that XNU's lck_rw_lock_shared_to_exclusive() is always
 * blocking (when waiting on readers), which means we can not use it.
 */
int
rw_tryupgrade(krwlock_t *rwlp)
{
    int held = 0;

	if (rwlp->rw_owner == current_thread())
		panic("rw_enter: locking against myself!");

	/* More readers than us? give up */
	if (rwlp->rw_readers != 1) return 0;

	/*
	 * It is ON. We need to drop our READER lock, and try to
	 * grab the WRITER as quickly as possible.
	 */
	atomic_dec_32((volatile uint32_t *)&rwlp->rw_readers);
	ExReleaseResourceLite(&rwlp->rw_lock);

	/* Grab the WRITER lock */
	held = ExAcquireResourceExclusiveLite(&rwlp->rw_lock, FALSE);

	if (held) {
		/* Looks like we won */
		rwlp->rw_owner = current_thread();
        ASSERT(rwlp->rw_readers == 0);
		return 1;
	}

	/*
	 * The worst has happened, we failed to grab WRITE lock, either
	 * due to another WRITER lock, or, some READER came along.
	 * IllumOS implementation returns with the READER lock again
	 * so we need to grab it.
	 */
	rw_enter(rwlp, RW_READER);
	return 0;

}

void
rw_exit(krwlock_t *rwlp)
{
    if (rwlp->rw_owner == current_thread()) {
        rwlp->rw_owner = NULL;
        ASSERT(rwlp->rw_readers == 0);
		ExReleaseResourceLite(&rwlp->rw_lock);
    } else {
        atomic_dec_32((volatile uint32_t *)&rwlp->rw_readers);
        ASSERT(rwlp->rw_owner == 0);
		ExReleaseResourceLite(&rwlp->rw_lock);
    }
}

int
rw_read_held(krwlock_t *rwlp)
{
	return (rw_lock_held(rwlp) && rwlp->rw_owner == NULL);
}

int
rw_lock_held(krwlock_t *rwlp)
{
    /*
     * ### not sure about this one ###
     */
    return (rwlp->rw_owner == current_thread() || rwlp->rw_readers > 0);
}

int
rw_write_held(krwlock_t *rwlp)
{
    return (rwlp->rw_owner == current_thread());
}

void
rw_downgrade(krwlock_t *rwlp)
{
	if (rwlp->rw_owner != current_thread())
		panic("SPL: rw_downgrade not WRITE lock held\n");
	rw_exit(rwlp);
	rw_enter(rwlp, RW_READER);
}

int spl_rwlock_init(void)
{
    return 0;
}

void spl_rwlock_fini(void)
{
	ASSERT(zfs_active_rwlock == 0);
}
