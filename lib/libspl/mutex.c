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
 */

#include <assert.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <sys/mutex.h>

/*
 * =========================================================================
 * mutexes
 * =========================================================================
 */

void
mutex_init(kmutex_t *mp, char *name, int type, void *cookie)
{
	(void) name, (void) type, (void) cookie;
	VERIFY0(pthread_mutex_init(&mp->m_lock, NULL));
	memset(&mp->m_owner, 0, sizeof (pthread_t));
}

void
mutex_destroy(kmutex_t *mp)
{
	VERIFY0(pthread_mutex_destroy(&mp->m_lock));
}

void
mutex_enter(kmutex_t *mp)
{
	VERIFY0(pthread_mutex_lock(&mp->m_lock));
	mp->m_owner = pthread_self();
}

int
mutex_enter_check_return(kmutex_t *mp)
{
	int error = pthread_mutex_lock(&mp->m_lock);
	if (error == 0)
		mp->m_owner = pthread_self();
	return (error);
}

int
mutex_tryenter(kmutex_t *mp)
{
	int error = pthread_mutex_trylock(&mp->m_lock);
	if (error == 0) {
		mp->m_owner = pthread_self();
		return (1);
	} else {
		VERIFY3S(error, ==, EBUSY);
		return (0);
	}
}

void
mutex_exit(kmutex_t *mp)
{
	memset(&mp->m_owner, 0, sizeof (pthread_t));
	VERIFY0(pthread_mutex_unlock(&mp->m_lock));
}
