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

#ifndef _SYS_RWLOCK_H
#define	_SYS_RWLOCK_H

#include <pthread.h>

/*
 * RW locks
 */
typedef struct krwlock {
	pthread_rwlock_t	rw_lock;
	pthread_t		rw_owner;
	uint_t			rw_readers;
} krwlock_t;

typedef int krw_t;

#define	RW_READER		0
#define	RW_WRITER		1
#define	RW_DEFAULT		RW_READER
#define	RW_NOLOCKDEP		RW_READER

#define	RW_READ_HELD(rw)	((rw)->rw_readers > 0)
#define	RW_WRITE_HELD(rw)	pthread_equal((rw)->rw_owner, pthread_self())
#define	RW_LOCK_HELD(rw)	(RW_READ_HELD(rw) || RW_WRITE_HELD(rw))

extern void rw_init(krwlock_t *rwlp, char *name, int type, void *arg);
extern void rw_destroy(krwlock_t *rwlp);
extern void rw_enter(krwlock_t *rwlp, krw_t rw);
extern int rw_tryenter(krwlock_t *rwlp, krw_t rw);
extern int rw_tryupgrade(krwlock_t *rwlp);
extern void rw_exit(krwlock_t *rwlp);
#define	rw_downgrade(rwlp) do { } while (0)

#endif /* _SYS_RWLOCK_H */
