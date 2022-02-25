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

#include <sys/zfs_context.h>
#include <sys/crypto/common.h>
#include <sys/crypto/api.h>
#include <sys/crypto/impl.h>

/* Cryptographic mechanisms tables and their access functions */

/*
 * Internal numbers assigned to mechanisms are coded as follows:
 *
 * +----------------+----------------+
 * | mech. class    | mech. index    |
 * <--- 32-bits --->+<--- 32-bits --->
 *
 * the mech_class identifies the table the mechanism belongs to.
 * mech_index  is the index for that mechanism in the table.
 * A mechanism belongs to exactly 1 table.
 * The tables are:
 * . digest_mechs_tab[] for the msg digest mechs.
 * . cipher_mechs_tab[] for encrypt/decrypt and wrap/unwrap mechs.
 * . mac_mechs_tab[] for MAC mechs.
 * . sign_mechs_tab[] for sign & verify mechs.
 * . keyops_mechs_tab[] for key/key pair generation, and key derivation.
 * . misc_mechs_tab[] for mechs that don't belong to any of the above.
 *
 * There are no holes in the tables.
 */

/*
 * Locking conventions:
 * --------------------
 * A mutex is associated with every entry of the tables.
 * The mutex is acquired whenever the entry is accessed for
 * 1) retrieving the mech_id (comparing the mech name)
 * 2) finding a provider for an xxx_init() or atomic operation.
 * 3) altering the mechs entry to add or remove a provider.
 *
 * In 2), after a provider is chosen, its prov_desc is held and the
 * entry's mutex must be dropped. The provider's working function (SPI) is
 * called outside the mech_entry's mutex.
 *
 * The number of providers for a particular mechanism is not expected to be
 * long enough to justify the cost of using rwlocks, so the per-mechanism
 * entry mutex won't be very *hot*.
 *
 */

		/* Mechanisms tables */


/* RFE 4687834 Will deal with the extensibility of these tables later */

static kcf_mech_entry_t kcf_digest_mechs_tab[KCF_MAXDIGEST];
static kcf_mech_entry_t kcf_cipher_mechs_tab[KCF_MAXCIPHER];
static kcf_mech_entry_t kcf_mac_mechs_tab[KCF_MAXMAC];

const kcf_mech_entry_tab_t kcf_mech_tabs_tab[KCF_LAST_OPSCLASS + 1] = {
	{0, NULL},				/* No class zero */
	{KCF_MAXDIGEST, kcf_digest_mechs_tab},
	{KCF_MAXCIPHER, kcf_cipher_mechs_tab},
	{KCF_MAXMAC, kcf_mac_mechs_tab},
};

static avl_tree_t kcf_mech_hash;

static int
kcf_mech_hash_compar(const void *lhs, const void *rhs)
{
	const kcf_mech_entry_t *l = lhs, *r = rhs;
	int cmp = strncmp(l->me_name, r->me_name, CRYPTO_MAX_MECH_NAME);
	return ((0 < cmp) - (cmp < 0));
}

void
kcf_destroy_mech_tabs(void)
{
	for (void *cookie = NULL; avl_destroy_nodes(&kcf_mech_hash, &cookie); )
		;
	avl_destroy(&kcf_mech_hash);
}

/*
 * kcf_init_mech_tabs()
 *
 * Called by the misc/kcf's _init() routine to initialize the tables
 * of mech_entry's.
 */
void
kcf_init_mech_tabs(void)
{
	avl_create(&kcf_mech_hash, kcf_mech_hash_compar,
	    sizeof (kcf_mech_entry_t), offsetof(kcf_mech_entry_t, me_node));
}

/*
 * kcf_create_mech_entry()
 *
 * Arguments:
 *	. The class of mechanism.
 *	. the name of the new mechanism.
 *
 * Description:
 *	Creates a new mech_entry for a mechanism not yet known to the
 *	framework.
 *	This routine is called by kcf_add_mech_provider, which is
 *	in turn invoked for each mechanism supported by a provider.
 *	The'class' argument depends on the crypto_func_group_t bitmask
 *	in the registering provider's mech_info struct for this mechanism.
 *	When there is ambiguity in the mapping between the crypto_func_group_t
 *	and a class (dual ops, ...) the KCF_MISC_CLASS should be used.
 *
 * Context:
 *	User context only.
 *
 * Returns:
 *	KCF_INVALID_MECH_CLASS or KCF_INVALID_MECH_NAME if the class or
 *	the mechname is bogus.
 *	KCF_MECH_TAB_FULL when there is no room left in the mech. tabs.
 *	KCF_SUCCESS otherwise.
 */
static int
kcf_create_mech_entry(kcf_ops_class_t class, const char *mechname)
{
	if ((class < KCF_FIRST_OPSCLASS) || (class > KCF_LAST_OPSCLASS))
		return (KCF_INVALID_MECH_CLASS);

	if ((mechname == NULL) || (mechname[0] == 0))
		return (KCF_INVALID_MECH_NAME);
	/*
	 * First check if the mechanism is already in one of the tables.
	 * The mech_entry could be in another class.
	 */
	avl_index_t where = 0;
	kcf_mech_entry_t tmptab;
	strlcpy(tmptab.me_name, mechname, CRYPTO_MAX_MECH_NAME);
	if (avl_find(&kcf_mech_hash, &tmptab, &where) != NULL)
		return (KCF_SUCCESS);
	/* Now take the next unused mech entry in the class's tab */
	kcf_mech_entry_t *me_tab = kcf_mech_tabs_tab[class].met_tab;
	int size = kcf_mech_tabs_tab[class].met_size;

	for (int i = 0; i < size; ++i)
		if (me_tab[i].me_name[0] == 0) {
			/* Found an empty spot */
			strlcpy(me_tab[i].me_name, mechname,
			    CRYPTO_MAX_MECH_NAME);
			me_tab[i].me_mechid = KCF_MECHID(class, i);

			/* Add the new mechanism to the hash table */
			avl_insert(&kcf_mech_hash, &me_tab[i], where);
			return (KCF_SUCCESS);
		}

	return (KCF_MECH_TAB_FULL);
}

/*
 * kcf_add_mech_provider()
 *
 * Arguments:
 *	. An index in to  the provider mechanism array
 *      . A pointer to the provider descriptor
 *	. A storage for the kcf_prov_mech_desc_t the entry was added at.
 *
 * Description:
 *      Adds  a new provider of a mechanism to the mechanism's mech_entry
 *	chain.
 *
 * Context:
 *      User context only.
 *
 * Returns
 *      KCF_SUCCESS on success
 *      KCF_MECH_TAB_FULL otherwise.
 */
int
kcf_add_mech_provider(short mech_indx,
    kcf_provider_desc_t *prov_desc, kcf_prov_mech_desc_t **pmdpp)
{
	int error;
	kcf_mech_entry_t *mech_entry = NULL;
	const crypto_mech_info_t *mech_info;
	crypto_mech_type_t kcf_mech_type;
	kcf_prov_mech_desc_t *prov_mech;

	mech_info = &prov_desc->pd_mechanisms[mech_indx];

	/*
	 * A mechanism belongs to exactly one mechanism table.
	 * Find the class corresponding to the function group flag of
	 * the mechanism.
	 */
	kcf_mech_type = crypto_mech2id(mech_info->cm_mech_name);
	if (kcf_mech_type == CRYPTO_MECH_INVALID) {
		crypto_func_group_t fg = mech_info->cm_func_group_mask;
		kcf_ops_class_t class;

		if (fg & CRYPTO_FG_DIGEST || fg & CRYPTO_FG_DIGEST_ATOMIC)
			class = KCF_DIGEST_CLASS;
		else if (fg & CRYPTO_FG_ENCRYPT || fg & CRYPTO_FG_DECRYPT ||
		    fg & CRYPTO_FG_ENCRYPT_ATOMIC ||
		    fg & CRYPTO_FG_DECRYPT_ATOMIC)
			class = KCF_CIPHER_CLASS;
		else if (fg & CRYPTO_FG_MAC || fg & CRYPTO_FG_MAC_ATOMIC)
			class = KCF_MAC_CLASS;
		else
			__builtin_unreachable();

		/*
		 * Attempt to create a new mech_entry for the specified
		 * mechanism. kcf_create_mech_entry() can handle the case
		 * where such an entry already exists.
		 */
		if ((error = kcf_create_mech_entry(class,
		    mech_info->cm_mech_name)) != KCF_SUCCESS) {
			return (error);
		}
		/* get the KCF mech type that was assigned to the mechanism */
		kcf_mech_type = crypto_mech2id(mech_info->cm_mech_name);
		ASSERT(kcf_mech_type != CRYPTO_MECH_INVALID);
	}

	error = kcf_get_mech_entry(kcf_mech_type, &mech_entry);
	ASSERT(error == KCF_SUCCESS);

	/* allocate and initialize new kcf_prov_mech_desc */
	prov_mech = kmem_zalloc(sizeof (kcf_prov_mech_desc_t), KM_SLEEP);
	memcpy(&prov_mech->pm_mech_info, mech_info,
	    sizeof (crypto_mech_info_t));
	prov_mech->pm_prov_desc = prov_desc;
	prov_desc->pd_mech_indx[KCF_MECH2CLASS(kcf_mech_type)]
	    [KCF_MECH2INDEX(kcf_mech_type)] = mech_indx;

	KCF_PROV_REFHOLD(prov_desc);
	KCF_PROV_IREFHOLD(prov_desc);

	/*
	 * Add new kcf_prov_mech_desc at the front of HW providers
	 * chain.
	 */
	if (mech_entry->me_sw_prov != NULL) {
		/*
		 * There is already a provider for this mechanism.
		 * Since we allow only one provider per mechanism,
		 * report this condition.
		 */
		cmn_err(CE_WARN, "The cryptographic provider "
		    "\"%s\" will not be used for %s. The provider "
		    "\"%s\" will be used for this mechanism "
		    "instead.", prov_desc->pd_description,
		    mech_info->cm_mech_name,
		    mech_entry->me_sw_prov->pm_prov_desc->
		    pd_description);
		KCF_PROV_REFRELE(prov_desc);
		kmem_free(prov_mech, sizeof (kcf_prov_mech_desc_t));
		prov_mech = NULL;
	} else {
		/*
		 * Set the provider as the provider for
		 * this mechanism.
		 */
		mech_entry->me_sw_prov = prov_mech;
	}

	*pmdpp = prov_mech;

	return (KCF_SUCCESS);
}

/*
 * kcf_remove_mech_provider()
 *
 * Arguments:
 *      . mech_name: the name of the mechanism.
 *      . prov_desc: The provider descriptor
 *
 * Description:
 *      Removes a provider from chain of provider descriptors.
 *	The provider is made unavailable to kernel consumers for the specified
 *	mechanism.
 *
 * Context:
 *      User context only.
 */
void
kcf_remove_mech_provider(const char *mech_name, kcf_provider_desc_t *prov_desc)
{
	crypto_mech_type_t mech_type;
	kcf_prov_mech_desc_t *prov_mech = NULL;
	kcf_mech_entry_t *mech_entry;

	/* get the KCF mech type that was assigned to the mechanism */
	if ((mech_type = crypto_mech2id(mech_name)) ==
	    CRYPTO_MECH_INVALID) {
		/*
		 * Provider was not allowed for this mech due to policy or
		 * configuration.
		 */
		return;
	}

	/* get a ptr to the mech_entry that was created */
	if (kcf_get_mech_entry(mech_type, &mech_entry) != KCF_SUCCESS) {
		/*
		 * Provider was not allowed for this mech due to policy or
		 * configuration.
		 */
		return;
	}

	if (mech_entry->me_sw_prov == NULL ||
	    mech_entry->me_sw_prov->pm_prov_desc != prov_desc) {
		/* not the provider for this mechanism */
		return;
	}
	prov_mech = mech_entry->me_sw_prov;
	mech_entry->me_sw_prov = NULL;

	/* free entry  */
	KCF_PROV_REFRELE(prov_mech->pm_prov_desc);
	KCF_PROV_IREFRELE(prov_mech->pm_prov_desc);
	kmem_free(prov_mech, sizeof (kcf_prov_mech_desc_t));
}

/*
 * kcf_get_mech_entry()
 *
 * Arguments:
 *      . The framework mechanism type
 *      . Storage for the mechanism entry
 *
 * Description:
 *      Retrieves the mechanism entry for the mech.
 *
 * Context:
 *      User and interrupt contexts.
 *
 * Returns:
 *      KCF_MECHANISM_XXX appropriate error code.
 *      KCF_SUCCESS otherwise.
 */
int
kcf_get_mech_entry(crypto_mech_type_t mech_type, kcf_mech_entry_t **mep)
{
	kcf_ops_class_t		class;
	int			index;
	const kcf_mech_entry_tab_t	*me_tab;

	ASSERT(mep != NULL);

	class = KCF_MECH2CLASS(mech_type);

	if ((class < KCF_FIRST_OPSCLASS) || (class > KCF_LAST_OPSCLASS)) {
		/* the caller won't need to know it's an invalid class */
		return (KCF_INVALID_MECH_NUMBER);
	}

	me_tab = &kcf_mech_tabs_tab[class];
	index = KCF_MECH2INDEX(mech_type);

	if ((index < 0) || (index >= me_tab->met_size)) {
		return (KCF_INVALID_MECH_NUMBER);
	}

	*mep = &((me_tab->met_tab)[index]);

	return (KCF_SUCCESS);
}

/*
 * crypto_mech2id()
 *
 * Arguments:
 *	. mechname: A null-terminated string identifying the mechanism name.
 *
 * Description:
 *	Walks the mechanisms tables, looking for an entry that matches the
 *	mechname. Once it find it, it builds the 64-bit mech_type and returns
 *	it.
 *
 * Context:
 *	Process and interruption.
 *
 * Returns:
 *	The unique mechanism identified by 'mechname', if found.
 *	CRYPTO_MECH_INVALID otherwise.
 */
/*
 * Lookup the hash table for an entry that matches the mechname.
 * If there are no providers for the mechanism,
 * but there is an unloaded provider, this routine will attempt
 * to load it.
 */
crypto_mech_type_t
crypto_mech2id(const char *mechname)
{
	kcf_mech_entry_t tmptab, *found;
	strlcpy(tmptab.me_name, mechname, CRYPTO_MAX_MECH_NAME);

	if ((found = avl_find(&kcf_mech_hash, &tmptab, NULL))) {
		ASSERT(found->me_mechid != CRYPTO_MECH_INVALID);
		return (found->me_mechid);
	}

	return (CRYPTO_MECH_INVALID);
}

#if defined(_KERNEL)
EXPORT_SYMBOL(crypto_mech2id);
#endif
