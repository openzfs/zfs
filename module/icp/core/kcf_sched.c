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

/* kmem caches used by the scheduler */
static kmem_cache_t *kcf_context_cache;

/*
 * Create a new context.
 */
crypto_ctx_t *
kcf_new_ctx(kcf_provider_desc_t *pd)
{
	crypto_ctx_t *ctx;
	kcf_context_t *kcf_ctx;

	kcf_ctx = kmem_cache_alloc(kcf_context_cache, KM_SLEEP);
	if (kcf_ctx == NULL)
		return (NULL);

	/* initialize the context for the consumer */
	kcf_ctx->kc_refcnt = 1;
	KCF_PROV_REFHOLD(pd);
	kcf_ctx->kc_prov_desc = pd;
	kcf_ctx->kc_sw_prov_desc = NULL;

	ctx = &kcf_ctx->kc_glbl_ctx;
	ctx->cc_provider_private = NULL;
	ctx->cc_framework_private = (void *)kcf_ctx;

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

	kmem_cache_free(kcf_context_cache, kcf_ctx);
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

	return (0);
}

static void
kcf_context_cache_destructor(void *buf, void *cdrarg)
{
	(void) cdrarg;
	kcf_context_t *kctx = (kcf_context_t *)buf;

	ASSERT(kctx->kc_refcnt == 0);
}

void
kcf_sched_destroy(void)
{
	if (kcf_context_cache)
		kmem_cache_destroy(kcf_context_cache);
}

/*
 * Creates and initializes all the structures needed by the framework.
 */
void
kcf_sched_init(void)
{
	/*
	 * Create all the kmem caches needed by the framework. We set the
	 * align argument to 64, to get a slab aligned to 64-byte as well as
	 * have the objects (cache_chunksize) to be a 64-byte multiple.
	 * This helps to avoid false sharing as this is the size of the
	 * CPU cache line.
	 */
	kcf_context_cache = kmem_cache_create("kcf_context_cache",
	    sizeof (struct kcf_context), 64, kcf_context_cache_constructor,
	    kcf_context_cache_destructor, NULL, NULL, NULL, 0);
}
