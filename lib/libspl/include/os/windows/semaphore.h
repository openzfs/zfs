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
