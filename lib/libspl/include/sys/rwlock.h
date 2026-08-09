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
