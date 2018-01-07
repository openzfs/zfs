/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Copyright 2014 Zettabyte Software, LLC.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _LIBSPL_SYNCH_H
#define	_LIBSPL_SYNCH_H

#ifndef __sun__

#include <assert.h>
#include <pthread.h>

/*
 * Definitions of synchronization types.
 */
#define	USYNC_THREAD	0x00		/* private to a process */
#define	USYNC_PROCESS	0x01		/* shared by processes */

typedef pthread_rwlock_t rwlock_t;

#define	DEFAULTRWLOCK		PTHREAD_RWLOCK_INITIALIZER

static inline int
rwlock_init(rwlock_t *rwlp, int type, void *arg)
{
	pthread_rwlockattr_t attr;
	int err = 0;

	VERIFY0(pthread_rwlockattr_init(&attr));
	switch (type) {
	case USYNC_THREAD:
		VERIFY0(pthread_rwlockattr_setpshared(&attr,
		    PTHREAD_PROCESS_PRIVATE));
		break;
	case USYNC_PROCESS:
		VERIFY0(pthread_rwlockattr_setpshared(&attr,
		    PTHREAD_PROCESS_SHARED));
		break;
	default:
		VERIFY0(1);
	}

	err = pthread_rwlock_init(rwlp, &attr);
	VERIFY0(pthread_rwlockattr_destroy(&attr));

	return (err);
}

#define	rwlock_destroy(x)	pthread_rwlock_destroy((x))
#define	rw_rdlock(x)		pthread_rwlock_rdlock((x))
#define	rw_wrlock(x)		pthread_rwlock_wrlock((x))
#define	rw_unlock(x)		pthread_rwlock_unlock((x))
#define	rw_tryrdlock(x)		pthread_rwlock_tryrdlock((x))
#define	rw_trywrlock(x)		pthread_rwlock_trywrlock((x))

#endif /* __sun__ */

#endif
