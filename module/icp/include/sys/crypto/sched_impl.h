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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SYS_CRYPTO_SCHED_IMPL_H
#define	_SYS_CRYPTO_SCHED_IMPL_H

/*
 * Scheduler internal structures.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/zfs_context.h>
#include <sys/crypto/api.h>
#include <sys/crypto/spi.h>
#include <sys/crypto/impl.h>
#include <sys/crypto/common.h>

typedef struct kcf_prov_tried {
	kcf_provider_desc_t	*pt_pd;
	struct kcf_prov_tried	*pt_next;
} kcf_prov_tried_t;

#define	IS_FG_SUPPORTED(mdesc, fg)		\
	(((mdesc)->pm_mech_info.cm_func_group_mask & (fg)) != 0)

#define	IS_PROVIDER_TRIED(pd, tlist)		\
	(tlist != NULL && is_in_triedlist(pd, tlist))

#define	IS_RECOVERABLE(error)			\
	(error == CRYPTO_BUSY ||			\
	error == CRYPTO_KEY_SIZE_RANGE)

/*
 * Internal representation of a canonical context. We contain crypto_ctx_t
 * structure in order to have just one memory allocation. The SPI
 * ((crypto_ctx_t *)ctx)->cc_framework_private maps to this structure.
 */
typedef struct kcf_context {
	crypto_ctx_t		kc_glbl_ctx;
	uint_t			kc_refcnt;
	kcf_provider_desc_t	*kc_prov_desc;	/* Prov. descriptor */
	kcf_provider_desc_t	*kc_sw_prov_desc;	/* Prov. descriptor */
} kcf_context_t;

/*
 * Decrement the reference count on the framework private context.
 * When the last reference is released, the framework private
 * context structure is freed along with the global context.
 */
#define	KCF_CONTEXT_REFRELE(ictx) {				\
	ASSERT((ictx)->kc_refcnt != 0);				\
	membar_exit();						\
	if (atomic_add_32_nv(&(ictx)->kc_refcnt, -1) == 0)	\
		kcf_free_context(ictx);				\
}

/*
 * Check if we can release the context now. In case of CRYPTO_BUSY,
 * the client can retry the request using the context,
 * so we do not release the context.
 *
 * This macro should be called only from the final routine in
 * an init/update/final sequence. We do not release the context in case
 * of update operations. We require the consumer to free it
 * explicitly, in case it wants to abandon the operation. This is done
 * as there may be mechanisms in ECB mode that can continue even if
 * an operation on a block fails.
 */
#define	KCF_CONTEXT_COND_RELEASE(rv, kcf_ctx) {			\
	if (KCF_CONTEXT_DONE(rv))				\
		KCF_CONTEXT_REFRELE(kcf_ctx);			\
}

/*
 * This macro determines whether we're done with a context.
 */
#define	KCF_CONTEXT_DONE(rv)					\
	((rv) != CRYPTO_BUSY &&	(rv) != CRYPTO_BUFFER_TOO_SMALL)


#define	KCF_SET_PROVIDER_MECHNUM(fmtype, pd, mechp)			\
	(mechp)->cm_type =						\
	    KCF_TO_PROV_MECHNUM(pd, fmtype);

/*
 * A crypto_ctx_template_t is internally a pointer to this struct
 */
typedef	struct kcf_ctx_template {
	size_t				ct_size;	/* for freeing */
	crypto_spi_ctx_template_t	ct_prov_tmpl;	/* context template */
							/* from the provider */
} kcf_ctx_template_t;


extern void kcf_free_triedlist(kcf_prov_tried_t *);
extern kcf_prov_tried_t *kcf_insert_triedlist(kcf_prov_tried_t **,
    kcf_provider_desc_t *, int);
extern kcf_provider_desc_t *kcf_get_mech_provider(crypto_mech_type_t,
    kcf_mech_entry_t **, int *, kcf_prov_tried_t *, crypto_func_group_t);
extern crypto_ctx_t *kcf_new_ctx(kcf_provider_desc_t *);
extern void kcf_sched_destroy(void);
extern void kcf_sched_init(void);
extern void kcf_free_context(kcf_context_t *);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_CRYPTO_SCHED_IMPL_H */
