/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * This file is part of the core Kernel Cryptographic Framework.
 * It implements the SPI functions exported to cryptographic
 * providers.
 */


#include <sys/zfs_context.h>
#include <sys/crypto/common.h>
#include <sys/crypto/impl.h>
#include <sys/crypto/sched_impl.h>
#include <sys/crypto/spi.h>

static int init_prov_mechs(const crypto_provider_info_t *,
    kcf_provider_desc_t *);

/*
 * This routine is used to add cryptographic providers to the KEF framework.
 * Providers pass a crypto_provider_info structure to crypto_register_provider()
 * and get back a handle.  The crypto_provider_info structure contains a
 * list of mechanisms supported by the provider and an ops vector containing
 * provider entry points.  Providers call this routine in their _init() routine.
 */
int
crypto_register_provider(const crypto_provider_info_t *info,
    crypto_kcf_provider_handle_t *handle)
{
	kcf_provider_desc_t *prov_desc = NULL;
	int ret = CRYPTO_ARGUMENTS_BAD;

	/*
	 * Allocate and initialize a new provider descriptor. We also
	 * hold it and release it when done.
	 */
	prov_desc = kcf_alloc_provider_desc();
	KCF_PROV_REFHOLD(prov_desc);

	/* copy provider description string */
	prov_desc->pd_description = info->pi_provider_description;

	/* Change from Illumos: the ops vector is persistent. */
	prov_desc->pd_ops_vector = info->pi_ops_vector;

	/* process the mechanisms supported by the provider */
	if ((ret = init_prov_mechs(info, prov_desc)) != CRYPTO_SUCCESS)
		goto bail;

	/*
	 * Add provider to providers tables, also sets the descriptor
	 * pd_prov_id field.
	 */
	if ((ret = kcf_prov_tab_add_provider(prov_desc)) != CRYPTO_SUCCESS) {
		undo_register_provider(prov_desc, B_FALSE);
		goto bail;
	}

	/*
	 * The global queue is used for providers. We handle ordering
	 * of multi-part requests in the taskq routine. So, it is safe to
	 * have multiple threads for the taskq. We pass TASKQ_PREPOPULATE flag
	 * to keep some entries cached to improve performance.
	 */

	mutex_enter(&prov_desc->pd_lock);
	prov_desc->pd_state = KCF_PROV_READY;
	mutex_exit(&prov_desc->pd_lock);

	*handle = prov_desc->pd_kcf_prov_handle;
	ret = CRYPTO_SUCCESS;

bail:
	KCF_PROV_REFRELE(prov_desc);
	return (ret);
}

/*
 * This routine is used to notify the framework when a provider is being
 * removed.  Providers call this routine in their _fini() routine.
 */
int
crypto_unregister_provider(crypto_kcf_provider_handle_t handle)
{
	uint_t mech_idx;
	kcf_provider_desc_t *desc;
	kcf_prov_state_t saved_state;

	/* lookup provider descriptor */
	if ((desc = kcf_prov_tab_lookup((crypto_provider_id_t)handle)) == NULL)
		return (CRYPTO_UNKNOWN_PROVIDER);

	mutex_enter(&desc->pd_lock);
	/*
	 * Check if any other thread is disabling or removing
	 * this provider. We return if this is the case.
	 */
	if (desc->pd_state >= KCF_PROV_DISABLED) {
		mutex_exit(&desc->pd_lock);
		/* Release reference held by kcf_prov_tab_lookup(). */
		KCF_PROV_REFRELE(desc);
		return (CRYPTO_BUSY);
	}

	saved_state = desc->pd_state;
	desc->pd_state = KCF_PROV_REMOVED;

	/*
	 * Check if this provider is currently being used.
	 * pd_irefcnt is the number of holds from the internal
	 * structures. We add one to account for the above lookup.
	 */
	if (desc->pd_refcnt > desc->pd_irefcnt + 1) {
		desc->pd_state = saved_state;
		mutex_exit(&desc->pd_lock);
		/* Release reference held by kcf_prov_tab_lookup(). */
		KCF_PROV_REFRELE(desc);
		/*
		 * The administrator will presumably stop the clients,
		 * thus removing the holds, when they get the busy
		 * return value.  Any retry will succeed then.
		 */
		return (CRYPTO_BUSY);
	}
	mutex_exit(&desc->pd_lock);

	/* remove the provider from the mechanisms tables */
	for (mech_idx = 0; mech_idx < desc->pd_mech_list_count;
	    mech_idx++) {
		kcf_remove_mech_provider(
		    desc->pd_mechanisms[mech_idx].cm_mech_name, desc);
	}

	/* remove provider from providers table */
	if (kcf_prov_tab_rem_provider((crypto_provider_id_t)handle) !=
	    CRYPTO_SUCCESS) {
		/* Release reference held by kcf_prov_tab_lookup(). */
		KCF_PROV_REFRELE(desc);
		return (CRYPTO_UNKNOWN_PROVIDER);
	}

	/* Release reference held by kcf_prov_tab_lookup(). */
	KCF_PROV_REFRELE(desc);

	/*
	 * Wait till the existing requests complete.
	 */
	mutex_enter(&desc->pd_lock);
	while (desc->pd_state != KCF_PROV_FREED)
		cv_wait(&desc->pd_remove_cv, &desc->pd_lock);
	mutex_exit(&desc->pd_lock);

	/*
	 * This is the only place where kcf_free_provider_desc()
	 * is called directly. KCF_PROV_REFRELE() should free the
	 * structure in all other places.
	 */
	ASSERT(desc->pd_state == KCF_PROV_FREED &&
	    desc->pd_refcnt == 0);
	kcf_free_provider_desc(desc);

	return (CRYPTO_SUCCESS);
}

/*
 * Process the mechanism info structures specified by the provider
 * during registration. A NULL crypto_provider_info_t indicates
 * an already initialized provider descriptor.
 *
 * Returns CRYPTO_SUCCESS on success, CRYPTO_ARGUMENTS if one
 * of the specified mechanisms was malformed, or CRYPTO_HOST_MEMORY
 * if the table of mechanisms is full.
 */
static int
init_prov_mechs(const crypto_provider_info_t *info, kcf_provider_desc_t *desc)
{
	uint_t mech_idx;
	uint_t cleanup_idx;
	int err = CRYPTO_SUCCESS;
	kcf_prov_mech_desc_t *pmd;
	int desc_use_count = 0;

	/*
	 * Copy the mechanism list from the provider info to the provider
	 * descriptor. desc->pd_mechanisms has an extra crypto_mech_info_t
	 * element if the provider has random_ops since we keep an internal
	 * mechanism, SUN_RANDOM, in this case.
	 */
	if (info != NULL) {
		ASSERT(info->pi_mechanisms != NULL);
		desc->pd_mech_list_count = info->pi_mech_list_count;
		desc->pd_mechanisms = info->pi_mechanisms;
	}

	/*
	 * For each mechanism support by the provider, add the provider
	 * to the corresponding KCF mechanism mech_entry chain.
	 */
	for (mech_idx = 0; mech_idx < desc->pd_mech_list_count; mech_idx++) {
		if ((err = kcf_add_mech_provider(mech_idx, desc, &pmd)) !=
		    KCF_SUCCESS)
			break;

		if (pmd == NULL)
			continue;

		/* The provider will be used for this mechanism */
		desc_use_count++;
	}

	/*
	 * Don't allow multiple providers with disabled mechanisms
	 * to register. Subsequent enabling of mechanisms will result in
	 * an unsupported configuration, i.e. multiple providers
	 * per mechanism.
	 */
	if (desc_use_count == 0)
		return (CRYPTO_ARGUMENTS_BAD);

	if (err == KCF_SUCCESS)
		return (CRYPTO_SUCCESS);

	/*
	 * An error occurred while adding the mechanism, cleanup
	 * and bail.
	 */
	for (cleanup_idx = 0; cleanup_idx < mech_idx; cleanup_idx++) {
		kcf_remove_mech_provider(
		    desc->pd_mechanisms[cleanup_idx].cm_mech_name, desc);
	}

	if (err == KCF_MECH_TAB_FULL)
		return (CRYPTO_HOST_MEMORY);

	return (CRYPTO_ARGUMENTS_BAD);
}

/*
 * Utility routine called from failure paths in crypto_register_provider()
 * and from crypto_load_soft_disabled().
 */
void
undo_register_provider(kcf_provider_desc_t *desc, boolean_t remove_prov)
{
	uint_t mech_idx;

	/* remove the provider from the mechanisms tables */
	for (mech_idx = 0; mech_idx < desc->pd_mech_list_count;
	    mech_idx++) {
		kcf_remove_mech_provider(
		    desc->pd_mechanisms[mech_idx].cm_mech_name, desc);
	}

	/* remove provider from providers table */
	if (remove_prov)
		(void) kcf_prov_tab_rem_provider(desc->pd_prov_id);
}
