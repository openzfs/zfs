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
