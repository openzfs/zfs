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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * This file contains the core framework routines for the
 * kernel cryptographic framework. These routines are at the
 * layer, between the kernel API/ioctls and the SPI.
 */

#include <sys/zfs_context.h>
#include <sys/crypto/common.h>
#include <sys/crypto/impl.h>
#include <sys/crypto/sched_impl.h>
#include <sys/crypto/api.h>

static kcf_global_swq_t *gswq;	/* Global queue */

/* Thread pool related variables */
static kcf_pool_t *kcfpool;	/* Thread pool of kcfd LWPs */
static const int kcf_maxthreads = 2;
static const int kcf_minthreads = 1;

/* kmem caches used by the scheduler */
static kmem_cache_t *kcf_sreq_cache;
static kmem_cache_t *kcf_areq_cache;
static kmem_cache_t *kcf_context_cache;

/* Global request ID table */
static kcf_reqid_table_t *kcf_reqid_table[REQID_TABLES];

/* KCF stats. Not protected. */
static kcf_stats_t kcf_ksdata = {
	{ "total threads in pool",	KSTAT_DATA_UINT32},
	{ "idle threads in pool",	KSTAT_DATA_UINT32},
	{ "min threads in pool",	KSTAT_DATA_UINT32},
	{ "max threads in pool",	KSTAT_DATA_UINT32},
	{ "requests in gswq",		KSTAT_DATA_UINT32},
	{ "max requests in gswq",	KSTAT_DATA_UINT32},
	{ "maxalloc for gwsq",		KSTAT_DATA_UINT32}
};

static kstat_t *kcf_misc_kstat = NULL;
ulong_t kcf_swprov_hndl = 0;

static void kcfpool_alloc(void);
static int kcf_misc_kstat_update(kstat_t *ksp, int rw);

/*
 * Create a new context.
 */
crypto_ctx_t *
kcf_new_ctx(crypto_call_req_t *crq, kcf_provider_desc_t *pd,
    crypto_session_id_t sid)
{
	crypto_ctx_t *ctx;
	kcf_context_t *kcf_ctx;

	kcf_ctx = kmem_cache_alloc(kcf_context_cache,
	    (crq == NULL) ? KM_SLEEP : KM_NOSLEEP);
	if (kcf_ctx == NULL)
		return (NULL);

	/* initialize the context for the consumer */
	kcf_ctx->kc_refcnt = 1;
	kcf_ctx->kc_req_chain_first = NULL;
	kcf_ctx->kc_req_chain_last = NULL;
	kcf_ctx->kc_secondctx = NULL;
	KCF_PROV_REFHOLD(pd);
	kcf_ctx->kc_prov_desc = pd;
	kcf_ctx->kc_sw_prov_desc = NULL;
	kcf_ctx->kc_mech = NULL;

	ctx = &kcf_ctx->kc_glbl_ctx;
	ctx->cc_provider = pd->pd_prov_handle;
	ctx->cc_session = sid;
	ctx->cc_provider_private = NULL;
	ctx->cc_framework_private = (void *)kcf_ctx;
	ctx->cc_flags = 0;
	ctx->cc_opstate = NULL;

	return (ctx);
}

/*
 * We're done with this framework context, so free it. Note that freeing
 * framework context (kcf_context) frees the global context (crypto_ctx).
 *
 * The provider is responsible for freeing provider private context after a
 * final or single operation and resetting the cc_provider_private field
 * to NULL. It should do this before it notifies the framework of the
 * completion. We still need to call KCF_PROV_FREE_CONTEXT to handle cases
 * like crypto_cancel_ctx(9f).
 */
void
kcf_free_context(kcf_context_t *kcf_ctx)
{
	kcf_provider_desc_t *pd = kcf_ctx->kc_prov_desc;
	crypto_ctx_t *gctx = &kcf_ctx->kc_glbl_ctx;
	kcf_context_t *kcf_secondctx = kcf_ctx->kc_secondctx;

	/* Release the second context, if any */

	if (kcf_secondctx != NULL)
		KCF_CONTEXT_REFRELE(kcf_secondctx);

	if (gctx->cc_provider_private != NULL) {
		mutex_enter(&pd->pd_lock);
		if (!KCF_IS_PROV_REMOVED(pd)) {
			/*
			 * Increment the provider's internal refcnt so it
			 * doesn't unregister from the framework while
			 * we're calling the entry point.
			 */
			KCF_PROV_IREFHOLD(pd);
			mutex_exit(&pd->pd_lock);
			(void) KCF_PROV_FREE_CONTEXT(pd, gctx);
			KCF_PROV_IREFRELE(pd);
		} else {
			mutex_exit(&pd->pd_lock);
		}
	}

	/* kcf_ctx->kc_prov_desc has a hold on pd */
	KCF_PROV_REFRELE(kcf_ctx->kc_prov_desc);

	/* check if this context is shared with a provider */
	if ((gctx->cc_flags & CRYPTO_INIT_OPSTATE) &&
	    kcf_ctx->kc_sw_prov_desc != NULL) {
		KCF_PROV_REFRELE(kcf_ctx->kc_sw_prov_desc);
	}

	kmem_cache_free(kcf_context_cache, kcf_ctx);
}

/*
 * Free the request after releasing all the holds.
 */
void
kcf_free_req(kcf_areq_node_t *areq)
{
	KCF_PROV_REFRELE(areq->an_provider);
	if (areq->an_context != NULL)
		KCF_CONTEXT_REFRELE(areq->an_context);

	if (areq->an_tried_plist != NULL)
		kcf_free_triedlist(areq->an_tried_plist);
	kmem_cache_free(kcf_areq_cache, areq);
}

/*
 * kmem_cache_alloc constructor for sync request structure.
 */
static int
kcf_sreq_cache_constructor(void *buf, void *cdrarg, int kmflags)
{
	(void) cdrarg, (void) kmflags;
	kcf_sreq_node_t *sreq = (kcf_sreq_node_t *)buf;

	sreq->sn_type = CRYPTO_SYNCH;
	cv_init(&sreq->sn_cv, NULL, CV_DEFAULT, NULL);
	mutex_init(&sreq->sn_lock, NULL, MUTEX_DEFAULT, NULL);

	return (0);
}

static void
kcf_sreq_cache_destructor(void *buf, void *cdrarg)
{
	(void) cdrarg;
	kcf_sreq_node_t *sreq = (kcf_sreq_node_t *)buf;

	mutex_destroy(&sreq->sn_lock);
	cv_destroy(&sreq->sn_cv);
}

/*
 * kmem_cache_alloc constructor for async request structure.
 */
static int
kcf_areq_cache_constructor(void *buf, void *cdrarg, int kmflags)
{
	(void) cdrarg, (void) kmflags;
	kcf_areq_node_t *areq = (kcf_areq_node_t *)buf;

	areq->an_type = CRYPTO_ASYNCH;
	areq->an_refcnt = 0;
	mutex_init(&areq->an_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&areq->an_done, NULL, CV_DEFAULT, NULL);
	cv_init(&areq->an_turn_cv, NULL, CV_DEFAULT, NULL);

	return (0);
}

static void
kcf_areq_cache_destructor(void *buf, void *cdrarg)
{
	(void) cdrarg;
	kcf_areq_node_t *areq = (kcf_areq_node_t *)buf;

	ASSERT(areq->an_refcnt == 0);
	mutex_destroy(&areq->an_lock);
	cv_destroy(&areq->an_done);
	cv_destroy(&areq->an_turn_cv);
}

/*
 * kmem_cache_alloc constructor for kcf_context structure.
 */
static int
kcf_context_cache_constructor(void *buf, void *cdrarg, int kmflags)
{
	(void) cdrarg, (void) kmflags;
	kcf_context_t *kctx = (kcf_context_t *)buf;

	kctx->kc_refcnt = 0;
	mutex_init(&kctx->kc_in_use_lock, NULL, MUTEX_DEFAULT, NULL);

	return (0);
}

static void
kcf_context_cache_destructor(void *buf, void *cdrarg)
{
	(void) cdrarg;
	kcf_context_t *kctx = (kcf_context_t *)buf;

	ASSERT(kctx->kc_refcnt == 0);
	mutex_destroy(&kctx->kc_in_use_lock);
}

void
kcf_sched_destroy(void)
{
	int i;

	if (kcf_misc_kstat)
		kstat_delete(kcf_misc_kstat);

	if (kcfpool) {
		mutex_destroy(&kcfpool->kp_thread_lock);
		cv_destroy(&kcfpool->kp_nothr_cv);
		mutex_destroy(&kcfpool->kp_user_lock);
		cv_destroy(&kcfpool->kp_user_cv);

		kmem_free(kcfpool, sizeof (kcf_pool_t));
	}

	for (i = 0; i < REQID_TABLES; i++) {
		if (kcf_reqid_table[i]) {
			mutex_destroy(&(kcf_reqid_table[i]->rt_lock));
			kmem_free(kcf_reqid_table[i],
			    sizeof (kcf_reqid_table_t));
		}
	}

	if (gswq) {
		mutex_destroy(&gswq->gs_lock);
		cv_destroy(&gswq->gs_cv);
		kmem_free(gswq, sizeof (kcf_global_swq_t));
	}

	if (kcf_context_cache)
		kmem_cache_destroy(kcf_context_cache);
	if (kcf_areq_cache)
		kmem_cache_destroy(kcf_areq_cache);
	if (kcf_sreq_cache)
		kmem_cache_destroy(kcf_sreq_cache);

	mutex_destroy(&ntfy_list_lock);
	cv_destroy(&ntfy_list_cv);
}

/*
 * Creates and initializes all the structures needed by the framework.
 */
void
kcf_sched_init(void)
{
	int i;
	kcf_reqid_table_t *rt;

	/*
	 * Create all the kmem caches needed by the framework. We set the
	 * align argument to 64, to get a slab aligned to 64-byte as well as
	 * have the objects (cache_chunksize) to be a 64-byte multiple.
	 * This helps to avoid false sharing as this is the size of the
	 * CPU cache line.
	 */
	kcf_sreq_cache = kmem_cache_create("kcf_sreq_cache",
	    sizeof (struct kcf_sreq_node), 64, kcf_sreq_cache_constructor,
	    kcf_sreq_cache_destructor, NULL, NULL, NULL, 0);

	kcf_areq_cache = kmem_cache_create("kcf_areq_cache",
	    sizeof (struct kcf_areq_node), 64, kcf_areq_cache_constructor,
	    kcf_areq_cache_destructor, NULL, NULL, NULL, 0);

	kcf_context_cache = kmem_cache_create("kcf_context_cache",
	    sizeof (struct kcf_context), 64, kcf_context_cache_constructor,
	    kcf_context_cache_destructor, NULL, NULL, NULL, 0);

	gswq = kmem_alloc(sizeof (kcf_global_swq_t), KM_SLEEP);

	mutex_init(&gswq->gs_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&gswq->gs_cv, NULL, CV_DEFAULT, NULL);
	gswq->gs_njobs = 0;
	gswq->gs_maxjobs = kcf_maxthreads * CRYPTO_TASKQ_MAX;
	gswq->gs_first = gswq->gs_last = NULL;

	/* Initialize the global reqid table */
	for (i = 0; i < REQID_TABLES; i++) {
		rt = kmem_zalloc(sizeof (kcf_reqid_table_t), KM_SLEEP);
		kcf_reqid_table[i] = rt;
		mutex_init(&rt->rt_lock, NULL, MUTEX_DEFAULT, NULL);
		rt->rt_curid = i;
	}

	/* Allocate and initialize the thread pool */
	kcfpool_alloc();

	/* Initialize the event notification list variables */
	mutex_init(&ntfy_list_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&ntfy_list_cv, NULL, CV_DEFAULT, NULL);

	/* Create the kcf kstat */
	kcf_misc_kstat = kstat_create("kcf", 0, "framework_stats", "crypto",
	    KSTAT_TYPE_NAMED, sizeof (kcf_stats_t) / sizeof (kstat_named_t),
	    KSTAT_FLAG_VIRTUAL);

	if (kcf_misc_kstat != NULL) {
		kcf_misc_kstat->ks_data = &kcf_ksdata;
		kcf_misc_kstat->ks_update = kcf_misc_kstat_update;
		kstat_install(kcf_misc_kstat);
	}
}

/*
 * Allocate the thread pool and initialize all the fields.
 */
static void
kcfpool_alloc()
{
	kcfpool = kmem_alloc(sizeof (kcf_pool_t), KM_SLEEP);

	kcfpool->kp_threads = kcfpool->kp_idlethreads = 0;
	kcfpool->kp_blockedthreads = 0;
	kcfpool->kp_signal_create_thread = B_FALSE;
	kcfpool->kp_nthrs = 0;
	kcfpool->kp_user_waiting = B_FALSE;

	mutex_init(&kcfpool->kp_thread_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&kcfpool->kp_nothr_cv, NULL, CV_DEFAULT, NULL);

	mutex_init(&kcfpool->kp_user_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&kcfpool->kp_user_cv, NULL, CV_DEFAULT, NULL);
}

/*
 * Update kstats.
 */
static int
kcf_misc_kstat_update(kstat_t *ksp, int rw)
{
	uint_t tcnt;
	kcf_stats_t *ks_data;

	if (rw == KSTAT_WRITE)
		return (EACCES);

	ks_data = ksp->ks_data;

	ks_data->ks_thrs_in_pool.value.ui32 = kcfpool->kp_threads;
	/*
	 * The failover thread is counted in kp_idlethreads in
	 * some corner cases. This is done to avoid doing more checks
	 * when submitting a request. We account for those cases below.
	 */
	if ((tcnt = kcfpool->kp_idlethreads) == (kcfpool->kp_threads + 1))
		tcnt--;
	ks_data->ks_idle_thrs.value.ui32 = tcnt;
	ks_data->ks_minthrs.value.ui32 = kcf_minthreads;
	ks_data->ks_maxthrs.value.ui32 = kcf_maxthreads;
	ks_data->ks_swq_njobs.value.ui32 = gswq->gs_njobs;
	ks_data->ks_swq_maxjobs.value.ui32 = gswq->gs_maxjobs;
	ks_data->ks_swq_maxalloc.value.ui32 = CRYPTO_TASKQ_MAX;

	return (0);
}
