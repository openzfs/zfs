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

#ifndef _LIBSPL_WINDOWS_SEMAPHORE_H
#define	_LIBSPL_WINDOWS_SEMAPHORE_H

#include <windows.h>
#include <time.h>

typedef struct {
	HANDLE sem_handle;
} sem_t;

extern int sem_init(sem_t *sem, int pshared, unsigned int value);
extern int sem_destroy(sem_t *sem);
extern int sem_post(sem_t *sem);
extern int sem_wait(sem_t *sem);
extern int sem_timedwait(sem_t *sem, const struct timespec *abs_timeout);

#endif /* _LIBSPL_WINDOWS_SEMAPHORE_H */
