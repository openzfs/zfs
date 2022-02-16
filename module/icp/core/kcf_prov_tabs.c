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
 * This file is part of the core Kernel Cryptographic Framework.
 * It implements the management of tables of Providers. Entries to
 * added and removed when cryptographic providers register with
 * and unregister from the framework, respectively. The KCF scheduler
 * and ioctl pseudo driver call this function to obtain the list
 * of available providers.
 *
 * The provider table is indexed by crypto_provider_id_t. Each
 * element of the table contains a pointer to a provider descriptor,
 * or NULL if the entry is free.
 *
 * This file also implements helper functions to allocate and free
 * provider descriptors.
 */

#include <sys/zfs_context.h>
#include <sys/crypto/common.h>
#include <sys/crypto/impl.h>
#include <sys/crypto/sched_impl.h>
#include <sys/crypto/spi.h>

#define	KCF_MAX_PROVIDERS	8	/* max number of providers */

/*
 * Prov_tab is an array of providers which is updated when
 * a crypto provider registers with kcf. The provider calls the
 * SPI routine, crypto_register_provider(), which in turn calls
 * kcf_prov_tab_add_provider().
 *
 * A provider unregisters by calling crypto_unregister_provider()
 * which triggers the removal of the prov_tab entry.
 * It also calls kcf_remove_mech_provider().
 *
 * prov_tab entries are not updated from kcf.conf or by cryptoadm(1M).
 */
static kcf_provider_desc_t *prov_tab[KCF_MAX_PROVIDERS];
static kmutex_t prov_tab_mutex; /* ensure exclusive access to the table */
static uint_t prov_tab_num = 0; /* number of providers in table */

void
kcf_prov_tab_destroy(void)
{
	mutex_destroy(&prov_tab_mutex);
}

/*
 * Initialize a mutex and the KCF providers table, prov_tab.
 * The providers table is dynamically allocated with KCF_MAX_PROVIDERS entries.
 * Called from kcf module _init().
 */
void
kcf_prov_tab_init(void)
{
	mutex_init(&prov_tab_mutex, NULL, MUTEX_DEFAULT, NULL);
}

/*
 * Add a provider to the provider table. If no free entry can be found
 * for the new provider, returns CRYPTO_HOST_MEMORY. Otherwise, add
 * the provider to the table, initialize the pd_prov_id field
 * of the specified provider descriptor to the index in that table,
 * and return CRYPTO_SUCCESS. Note that a REFHOLD is done on the
 * provider when pointed to by a table entry.
 */
int
kcf_prov_tab_add_provider(kcf_provider_desc_t *prov_desc)
{
	uint_t i;

	mutex_enter(&prov_tab_mutex);

	/* find free slot in providers table */
	for (i = 1; i < KCF_MAX_PROVIDERS && prov_tab[i] != NULL; i++)
		;
	if (i == KCF_MAX_PROVIDERS) {
		/* ran out of providers entries */
		mutex_exit(&prov_tab_mutex);
		cmn_err(CE_WARN, "out of providers entries");
		return (CRYPTO_HOST_MEMORY);
	}

	/* initialize entry */
	prov_tab[i] = prov_desc;
	KCF_PROV_REFHOLD(prov_desc);
	KCF_PROV_IREFHOLD(prov_desc);
	prov_tab_num++;

	mutex_exit(&prov_tab_mutex);

	/* update provider descriptor */
	prov_desc->pd_prov_id = i;

	/*
	 * The KCF-private provider handle is defined as the internal
	 * provider id.
	 */
	prov_desc->pd_kcf_prov_handle =
	    (crypto_kcf_provider_handle_t)prov_desc->pd_prov_id;

	return (CRYPTO_SUCCESS);
}

/*
 * Remove the provider specified by its id. A REFRELE is done on the
 * corresponding provider descriptor before this function returns.
 * Returns CRYPTO_UNKNOWN_PROVIDER if the provider id is not valid.
 */
int
kcf_prov_tab_rem_provider(crypto_provider_id_t prov_id)
{
	kcf_provider_desc_t *prov_desc;

	/*
	 * Validate provider id, since it can be specified by a 3rd-party
	 * provider.
	 */

	mutex_enter(&prov_tab_mutex);
	if (prov_id >= KCF_MAX_PROVIDERS ||
	    ((prov_desc = prov_tab[prov_id]) == NULL)) {
		mutex_exit(&prov_tab_mutex);
		return (CRYPTO_INVALID_PROVIDER_ID);
	}
	mutex_exit(&prov_tab_mutex);

	/*
	 * The provider id must remain valid until the associated provider
	 * descriptor is freed. For this reason, we simply release our
	 * reference to the descriptor here. When the reference count
	 * reaches zero, kcf_free_provider_desc() will be invoked and
	 * the associated entry in the providers table will be released
	 * at that time.
	 */

	KCF_PROV_REFRELE(prov_desc);
	KCF_PROV_IREFRELE(prov_desc);

	return (CRYPTO_SUCCESS);
}

/*
 * Returns the provider descriptor corresponding to the specified
 * provider id. A REFHOLD is done on the descriptor before it is
 * returned to the caller. It is the responsibility of the caller
 * to do a REFRELE once it is done with the provider descriptor.
 */
kcf_provider_desc_t *
kcf_prov_tab_lookup(crypto_provider_id_t prov_id)
{
	kcf_provider_desc_t *prov_desc;

	mutex_enter(&prov_tab_mutex);

	prov_desc = prov_tab[prov_id];

	if (prov_desc == NULL) {
		mutex_exit(&prov_tab_mutex);
		return (NULL);
	}

	KCF_PROV_REFHOLD(prov_desc);

	mutex_exit(&prov_tab_mutex);

	return (prov_desc);
}

/*
 * Allocate a provider descriptor. mech_list_count specifies the
 * number of mechanisms supported by the providers, and is used
 * to allocate storage for the mechanism table.
 * This function may sleep while allocating memory, which is OK
 * since it is invoked from user context during provider registration.
 */
kcf_provider_desc_t *
kcf_alloc_provider_desc(void)
{
	kcf_provider_desc_t *desc =
	    kmem_zalloc(sizeof (kcf_provider_desc_t), KM_SLEEP);

	for (int i = 0; i < KCF_OPS_CLASSSIZE; i++)
		for (int j = 0; j < KCF_MAXMECHTAB; j++)
			desc->pd_mech_indx[i][j] = KCF_INVALID_INDX;

	desc->pd_prov_id = KCF_PROVID_INVALID;
	desc->pd_state = KCF_PROV_ALLOCATED;

	mutex_init(&desc->pd_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&desc->pd_remove_cv, NULL, CV_DEFAULT, NULL);

	return (desc);
}

/*
 * Called by KCF_PROV_REFRELE when a provider's reference count drops
 * to zero. We free the descriptor when the last reference is released.
 * However, for providers, we do not free it when there is an
 * unregister thread waiting. We signal that thread in this case and
 * that thread is responsible for freeing the descriptor.
 */
void
kcf_provider_zero_refcnt(kcf_provider_desc_t *desc)
{
	mutex_enter(&desc->pd_lock);
	if (desc->pd_state == KCF_PROV_REMOVED ||
	    desc->pd_state == KCF_PROV_DISABLED) {
		desc->pd_state = KCF_PROV_FREED;
		cv_broadcast(&desc->pd_remove_cv);
		mutex_exit(&desc->pd_lock);
		return;
	}

	mutex_exit(&desc->pd_lock);
	kcf_free_provider_desc(desc);
}

/*
 * Free a provider descriptor.
 */
void
kcf_free_provider_desc(kcf_provider_desc_t *desc)
{
	if (desc == NULL)
		return;

	mutex_enter(&prov_tab_mutex);
	if (desc->pd_prov_id != KCF_PROVID_INVALID) {
		/* release the associated providers table entry */
		ASSERT(prov_tab[desc->pd_prov_id] != NULL);
		prov_tab[desc->pd_prov_id] = NULL;
		prov_tab_num--;
	}
	mutex_exit(&prov_tab_mutex);

	/* free the kernel memory associated with the provider descriptor */

	mutex_destroy(&desc->pd_lock);
	cv_destroy(&desc->pd_remove_cv);

	kmem_free(desc, sizeof (kcf_provider_desc_t));
}

/*
 * Returns in the location pointed to by pd a pointer to the descriptor
 * for the provider for the specified mechanism.
 * The provider descriptor is returned held and it is the caller's
 * responsibility to release it when done. The mechanism entry
 * is returned if the optional argument mep is non NULL.
 *
 * Returns one of the CRYPTO_ * error codes on failure, and
 * CRYPTO_SUCCESS on success.
 */
int
kcf_get_sw_prov(crypto_mech_type_t mech_type, kcf_provider_desc_t **pd,
    kcf_mech_entry_t **mep, boolean_t log_warn)
{
	kcf_mech_entry_t *me;

	/* get the mechanism entry for this mechanism */
	if (kcf_get_mech_entry(mech_type, &me) != KCF_SUCCESS)
		return (CRYPTO_MECHANISM_INVALID);

	/* Get the provider for this mechanism. */
	if (me->me_sw_prov == NULL ||
	    (*pd = me->me_sw_prov->pm_prov_desc) == NULL) {
		/* no provider for this mechanism */
		if (log_warn)
			cmn_err(CE_WARN, "no provider for \"%s\"\n",
			    me->me_name);
		return (CRYPTO_MECH_NOT_SUPPORTED);
	}

	KCF_PROV_REFHOLD(*pd);

	if (mep != NULL)
		*mep = me;

	return (CRYPTO_SUCCESS);
}
