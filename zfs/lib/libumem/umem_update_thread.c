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
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Portions Copyright 2006 OmniTI, Inc.
 */

/* #pragma ident	"@(#)umem_update_thread.c	1.2	05/06/08 SMI" */

#include "config.h"
#include "umem_base.h"
#include "vmem_base.h"

#include <signal.h>

/*
 * we use the _ version, since we don't want to be cancelled.
 */
extern int _cond_timedwait(cond_t *cv, mutex_t *mutex, const timespec_t *delay);

/*ARGSUSED*/
static THR_RETURN
THR_API umem_update_thread(void *arg)
{
	struct timeval now;
	int in_update = 0;

	(void) mutex_lock(&umem_update_lock);

	ASSERT(umem_update_thr == thr_self());
	ASSERT(umem_st_update_thr == 0);

	for (;;) {
		umem_process_updates();

		if (in_update) {
			in_update = 0;
			/*
			 * we wait until now to set the next update time
			 * so that the updates are self-throttling
			 */
			(void) gettimeofday(&umem_update_next, NULL);
			umem_update_next.tv_sec += umem_reap_interval;
		}

		switch (umem_reaping) {
		case UMEM_REAP_DONE:
		case UMEM_REAP_ADDING:
			break;

		case UMEM_REAP_ACTIVE:
			umem_reap_next = gethrtime() +
			    (hrtime_t)umem_reap_interval * NANOSEC;
			umem_reaping = UMEM_REAP_DONE;
			break;

		default:
			ASSERT(umem_reaping == UMEM_REAP_DONE ||
			    umem_reaping == UMEM_REAP_ADDING ||
			    umem_reaping == UMEM_REAP_ACTIVE);
			break;
		}

		(void) gettimeofday(&now, NULL);
		if (now.tv_sec > umem_update_next.tv_sec ||
		    (now.tv_sec == umem_update_next.tv_sec &&
		    now.tv_usec >= umem_update_next.tv_usec)) {
			/*
			 * Time to run an update
			 */
			(void) mutex_unlock(&umem_update_lock);

			vmem_update(NULL);
			/*
			 * umem_cache_update can use umem_add_update to
			 * request further work.  The update is not complete
			 * until all such work is finished.
			 */
			umem_cache_applyall(umem_cache_update);

			(void) mutex_lock(&umem_update_lock);
			in_update = 1;
			continue;	/* start processing immediately */
		}

		/*
		 * if there is no work to do, we wait until it is time for
		 * next update, or someone wakes us.
		 */
		if (umem_null_cache.cache_unext == &umem_null_cache) {
			timespec_t abs_time;
			abs_time.tv_sec = umem_update_next.tv_sec;
			abs_time.tv_nsec = umem_update_next.tv_usec * 1000;

			(void) _cond_timedwait(&umem_update_cv,
			    &umem_update_lock, &abs_time);
		}
	}
	/* LINTED no return statement */
}

int
umem_create_update_thread(void)
{
#ifndef _WIN32
	sigset_t sigmask, oldmask;
#endif

	ASSERT(MUTEX_HELD(&umem_update_lock));
	ASSERT(umem_update_thr == 0);

#ifndef _WIN32
	/*
	 * The update thread handles no signals
	 */
	(void) sigfillset(&sigmask);
	(void) thr_sigsetmask(SIG_BLOCK, &sigmask, &oldmask);
#endif
	if (thr_create(NULL, 0, umem_update_thread, NULL,
	    THR_BOUND | THR_DAEMON | THR_DETACHED, &umem_update_thr) == 0) {
#ifndef _WIN32
		(void) thr_sigsetmask(SIG_SETMASK, &oldmask, NULL);
#endif
		return (1);
	}
	umem_update_thr = 0;
#ifndef _WIN32
	(void) thr_sigsetmask(SIG_SETMASK, &oldmask, NULL);
#endif
	return (0);
}
