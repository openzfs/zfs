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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_THREAD_POOL_H_
#define	_THREAD_POOL_H_ extern __attribute__((visibility("default")))

#include <sys/types.h>
#include <thread.h>
#include <pthread.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef	struct tpool tpool_t;	/* opaque thread pool descriptor */

_THREAD_POOL_H_ tpool_t	*tpool_create(uint_t min_threads, uint_t max_threads,
			uint_t linger, pthread_attr_t *attr);
_THREAD_POOL_H_ int	tpool_dispatch(tpool_t *tpool,
			void (*func)(void *), void *arg);
_THREAD_POOL_H_ void	tpool_destroy(tpool_t *tpool);
_THREAD_POOL_H_ void	tpool_abandon(tpool_t *tpool);
_THREAD_POOL_H_ void	tpool_wait(tpool_t *tpool);
_THREAD_POOL_H_ void	tpool_suspend(tpool_t *tpool);
_THREAD_POOL_H_ int	tpool_suspended(tpool_t *tpool);
_THREAD_POOL_H_ void	tpool_resume(tpool_t *tpool);
_THREAD_POOL_H_ int	tpool_member(tpool_t *tpool);

#ifdef	__cplusplus
}
#endif

#endif	/* _THREAD_POOL_H_ */
