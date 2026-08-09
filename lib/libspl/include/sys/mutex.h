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

#ifndef _SYS_MUTEX_H
#define	_SYS_MUTEX_H

#include <pthread.h>

/*
 * Mutexes
 */
typedef struct kmutex {
	pthread_mutex_t		m_lock;
	pthread_t		m_owner;
} kmutex_t;

#define	MUTEX_DEFAULT		0
#define	MUTEX_NOLOCKDEP		MUTEX_DEFAULT
#define	MUTEX_HELD(mp)		pthread_equal((mp)->m_owner, pthread_self())
#define	MUTEX_NOT_HELD(mp)	!MUTEX_HELD(mp)

extern void mutex_init(kmutex_t *mp, char *name, int type, void *cookie);
extern void mutex_destroy(kmutex_t *mp);
extern void mutex_enter(kmutex_t *mp);
extern int mutex_enter_check_return(kmutex_t *mp);
extern void mutex_exit(kmutex_t *mp);
extern int mutex_tryenter(kmutex_t *mp);

#define	NESTED_SINGLE 1
#define	mutex_enter_nested(mp, class) mutex_enter(mp)
#define	mutex_enter_interruptible(mp) mutex_enter_check_return(mp)

#endif /* _SYS_MUTEX_H */
