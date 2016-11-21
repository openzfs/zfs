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
 * Copyright (c) 2016 by Jinshan Xiong. All rights reserved.
 */

#ifndef _SPINLOCK_H
#define	_SPINLOCK_H

#ifdef _KERNEL
#include <linux/spinlock.h>

#else

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	pthread_spinlock_t lock;
} spinlock_t;

static inline void spin_lock(spinlock_t *lock)
{
	pthread_spin_lock(&lock->lock);
}

static inline void spin_unlock(spinlock_t *lock)
{
	pthread_spin_unlock(&lock->lock);
}

static inline void spin_lock_init(spinlock_t *lock)
{
	pthread_spin_init(&lock->lock, 0);
}

#ifdef __cplusplus
}
#endif

#endif /* !_KERNEL */

#endif	/* _SPINLOCK_H */
