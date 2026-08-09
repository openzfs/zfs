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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2014, 2015 by Delphix. All rights reserved.
 * Copyright 2016 The MathWorks, Inc. All rights reserved.
 */

/*
 * A Zero Reference Lock (ZRL) is a reference count that can lock out new
 * references only when the count is zero and only without waiting if the count
 * is not already zero. It is similar to a read-write lock in that it allows
 * multiple readers and only a single writer, but it does not allow a writer to
 * block while waiting for readers to exit, and therefore the question of
 * reader/writer priority is moot (no WRWANT bit). Since the equivalent of
 * rw_enter(&lock, RW_WRITER) is disallowed and only tryenter() is allowed, it
 * is perfectly safe for the same reader to acquire the same lock multiple
 * times. The fact that a ZRL is reentrant for readers (through multiple calls
 * to zrl_add()) makes it convenient for determining whether something is
 * actively referenced without the fuss of flagging lock ownership across
 * function calls.
 */
#include <sys/zrlock.h>
#include <sys/trace_zfs.h>

/*
 * A ZRL can be locked only while there are zero references, so ZRL_LOCKED is
 * treated as zero references.
 */
#define	ZRL_LOCKED	-1
#define	ZRL_DESTROYED	-2

void
zrl_init(zrlock_t *zrl)
{
	mutex_init(&zrl->zr_mtx, NULL, MUTEX_DEFAULT, NULL);
	zrl->zr_refcount = 0;
	cv_init(&zrl->zr_cv, NULL, CV_DEFAULT, NULL);
#ifdef	ZFS_DEBUG
	zrl->zr_owner = NULL;
	zrl->zr_caller = NULL;
#endif
}

void
zrl_destroy(zrlock_t *zrl)
{
	ASSERT0(zrl->zr_refcount);

	mutex_destroy(&zrl->zr_mtx);
	zrl->zr_refcount = ZRL_DESTROYED;
	cv_destroy(&zrl->zr_cv);
}

void
zrl_add_impl(zrlock_t *zrl, const char *zc)
{
	for (;;) {
		uint32_t n = (uint32_t)zrl->zr_refcount;
		while (n != ZRL_LOCKED) {
			uint32_t cas = atomic_cas_32(
			    (uint32_t *)&zrl->zr_refcount, n, n + 1);
			if (cas == n) {
				ASSERT3S((int32_t)n, >=, 0);
#ifdef	ZFS_DEBUG
				if (zrl->zr_owner == curthread) {
					DTRACE_PROBE3(zrlock__reentry,
					    zrlock_t *, zrl,
					    kthread_t *, curthread,
					    uint32_t, n);
				}
				zrl->zr_owner = curthread;
				zrl->zr_caller = zc;
#endif
				return;
			}
			n = cas;
		}

		mutex_enter(&zrl->zr_mtx);
		while (zrl->zr_refcount == ZRL_LOCKED) {
			cv_wait(&zrl->zr_cv, &zrl->zr_mtx);
		}
		mutex_exit(&zrl->zr_mtx);
	}
}

void
zrl_remove(zrlock_t *zrl)
{
#ifdef	ZFS_DEBUG
	if (zrl->zr_owner == curthread) {
		zrl->zr_owner = NULL;
		zrl->zr_caller = NULL;
	}
	int32_t n = atomic_dec_32_nv((uint32_t *)&zrl->zr_refcount);
	ASSERT3S(n, >=, 0);
#else
	atomic_dec_32((uint32_t *)&zrl->zr_refcount);
#endif
}

int
zrl_tryenter(zrlock_t *zrl)
{
	uint32_t n = (uint32_t)zrl->zr_refcount;

	if (n == 0) {
		uint32_t cas = atomic_cas_32(
		    (uint32_t *)&zrl->zr_refcount, 0, ZRL_LOCKED);
		if (cas == 0) {
#ifdef	ZFS_DEBUG
			ASSERT0P(zrl->zr_owner);
			zrl->zr_owner = curthread;
#endif
			return (1);
		}
	}

	ASSERT3S((int32_t)n, >, ZRL_DESTROYED);

	return (0);
}

void
zrl_exit(zrlock_t *zrl)
{
	ASSERT3S(zrl->zr_refcount, ==, ZRL_LOCKED);

	mutex_enter(&zrl->zr_mtx);
#ifdef	ZFS_DEBUG
	ASSERT3P(zrl->zr_owner, ==, curthread);
	zrl->zr_owner = NULL;
	membar_producer();	/* make sure the owner store happens first */
#endif
	zrl->zr_refcount = 0;
	cv_broadcast(&zrl->zr_cv);
	mutex_exit(&zrl->zr_mtx);
}

int
zrl_is_zero(zrlock_t *zrl)
{
	ASSERT3S(zrl->zr_refcount, >, ZRL_DESTROYED);

	return (zrl->zr_refcount <= 0);
}

int
zrl_is_locked(zrlock_t *zrl)
{
	ASSERT3S(zrl->zr_refcount, >, ZRL_DESTROYED);

	return (zrl->zr_refcount == ZRL_LOCKED);
}

#ifdef	ZFS_DEBUG
kthread_t *
zrl_owner(zrlock_t *zrl)
{
	return (zrl->zr_owner);
}
#endif

#if defined(_KERNEL)

EXPORT_SYMBOL(zrl_add_impl);
EXPORT_SYMBOL(zrl_remove);

#endif
