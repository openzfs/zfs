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

/* #pragma ident	"@(#)umem_fork.c	1.3	05/06/08 SMI" */

#include "config.h"
/* #include "mtlib.h" */
#include "umem_base.h"
#include "vmem_base.h"

#ifndef _WIN32
#include <unistd.h>

/*
 * The following functions are for pre- and post-fork1(2) handling.
 */

static void
umem_lockup_cache(umem_cache_t *cp)
{
	int idx;
	int ncpus = cp->cache_cpu_mask + 1;

	for (idx = 0; idx < ncpus; idx++)
		(void) mutex_lock(&cp->cache_cpu[idx].cc_lock);

	(void) mutex_lock(&cp->cache_depot_lock);
	(void) mutex_lock(&cp->cache_lock);
}

static void
umem_release_cache(umem_cache_t *cp)
{
	int idx;
	int ncpus = cp->cache_cpu_mask + 1;

	(void) mutex_unlock(&cp->cache_lock);
	(void) mutex_unlock(&cp->cache_depot_lock);

	for (idx = 0; idx < ncpus; idx++)
		(void) mutex_unlock(&cp->cache_cpu[idx].cc_lock);
}

static void
umem_lockup_log_header(umem_log_header_t *lhp)
{
	int idx;
	if (lhp == NULL)
		return;
	for (idx = 0; idx < umem_max_ncpus; idx++)
		(void) mutex_lock(&lhp->lh_cpu[idx].clh_lock);

	(void) mutex_lock(&lhp->lh_lock);
}

static void
umem_release_log_header(umem_log_header_t *lhp)
{
	int idx;
	if (lhp == NULL)
		return;

	(void) mutex_unlock(&lhp->lh_lock);

	for (idx = 0; idx < umem_max_ncpus; idx++)
		(void) mutex_unlock(&lhp->lh_cpu[idx].clh_lock);
}

static void
umem_lockup(void)
{
	umem_cache_t *cp;

	(void) mutex_lock(&umem_init_lock);
	/*
	 * If another thread is busy initializing the library, we must
	 * wait for it to complete (by calling umem_init()) before allowing
	 * the fork() to proceed.
	 */
	if (umem_ready == UMEM_READY_INITING && umem_init_thr != thr_self()) {
		(void) mutex_unlock(&umem_init_lock);
		(void) umem_init();
		(void) mutex_lock(&umem_init_lock);
	}
	(void) mutex_lock(&umem_cache_lock);
	(void) mutex_lock(&umem_update_lock);
	(void) mutex_lock(&umem_flags_lock);

	umem_lockup_cache(&umem_null_cache);
	for (cp = umem_null_cache.cache_prev; cp != &umem_null_cache;
	    cp = cp->cache_prev)
		umem_lockup_cache(cp);

	umem_lockup_log_header(umem_transaction_log);
	umem_lockup_log_header(umem_content_log);
	umem_lockup_log_header(umem_failure_log);
	umem_lockup_log_header(umem_slab_log);

	(void) cond_broadcast(&umem_update_cv);

	vmem_sbrk_lockup();
	vmem_lockup();
}

static void
umem_release(void)
{
	umem_cache_t *cp;

	vmem_release();
	vmem_sbrk_release();

	umem_release_log_header(umem_slab_log);
	umem_release_log_header(umem_failure_log);
	umem_release_log_header(umem_content_log);
	umem_release_log_header(umem_transaction_log);

	for (cp = umem_null_cache.cache_next; cp != &umem_null_cache;
	    cp = cp->cache_next)
		umem_release_cache(cp);
	umem_release_cache(&umem_null_cache);

	(void) mutex_unlock(&umem_flags_lock);
	(void) mutex_unlock(&umem_update_lock);
	(void) mutex_unlock(&umem_cache_lock);
	(void) mutex_unlock(&umem_init_lock);
}

static void
umem_release_child(void)
{
	umem_cache_t *cp;

	/*
	 * Clean up the update state
	 */
	umem_update_thr = 0;

	if (umem_st_update_thr != thr_self()) {
		umem_st_update_thr = 0;
		umem_reaping = UMEM_REAP_DONE;

		for (cp = umem_null_cache.cache_next; cp != &umem_null_cache;
		    cp = cp->cache_next) {
			if (cp->cache_uflags & UMU_NOTIFY)
				cp->cache_uflags &= ~UMU_NOTIFY;

			/*
			 * If the cache is active, we just re-add it to
			 * the update list.  This will re-do any active
			 * updates on the cache, but that won't break
			 * anything.
			 *
			 * The worst that can happen is a cache has
			 * its magazines rescaled twice, instead of once.
			 */
			if (cp->cache_uflags & UMU_ACTIVE) {
				umem_cache_t *cnext, *cprev;

				ASSERT(cp->cache_unext == NULL &&
				    cp->cache_uprev == NULL);

				cp->cache_uflags &= ~UMU_ACTIVE;
				cp->cache_unext = cnext = &umem_null_cache;
				cp->cache_uprev = cprev =
				    umem_null_cache.cache_uprev;
				cnext->cache_uprev = cp;
				cprev->cache_unext = cp;
			}
		}
	}

	umem_release();
}
#endif

void
umem_forkhandler_init(void)
{
#ifndef _WIN32
	/*
	 * There is no way to unregister these atfork functions,
	 * but we don't need to.  The dynamic linker and libc take
	 * care of unregistering them if/when the library is unloaded.
	 */
	(void) pthread_atfork(umem_lockup, umem_release, umem_release_child);
#endif
}
