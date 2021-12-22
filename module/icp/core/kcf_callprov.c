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

#include <sys/crypto/common.h>
#include <sys/crypto/impl.h>
#include <sys/crypto/sched_impl.h>

void
kcf_free_triedlist(kcf_prov_tried_t *list)
{
	kcf_prov_tried_t *l;

	while ((l = list) != NULL) {
		list = list->pt_next;
		KCF_PROV_REFRELE(l->pt_pd);
		kmem_free(l, sizeof (kcf_prov_tried_t));
	}
}

kcf_prov_tried_t *
kcf_insert_triedlist(kcf_prov_tried_t **list, kcf_provider_desc_t *pd,
    int kmflag)
{
	kcf_prov_tried_t *l;

	l = kmem_alloc(sizeof (kcf_prov_tried_t), kmflag);
	if (l == NULL)
		return (NULL);

	l->pt_pd = pd;
	l->pt_next = *list;
	*list = l;

	return (l);
}

static boolean_t
is_in_triedlist(kcf_provider_desc_t *pd, kcf_prov_tried_t *triedl)
{
	while (triedl != NULL) {
		if (triedl->pt_pd == pd)
			return (B_TRUE);
		triedl = triedl->pt_next;
	};

	return (B_FALSE);
}

/*
 * Search a mech entry's hardware provider list for the specified
 * provider. Return true if found.
 */
static boolean_t
is_valid_provider_for_mech(kcf_provider_desc_t *pd, kcf_mech_entry_t *me,
    crypto_func_group_t fg)
{
	kcf_prov_mech_desc_t *prov_chain;

	prov_chain = me->me_hw_prov_chain;
	if (prov_chain != NULL) {
		ASSERT(me->me_num_hwprov > 0);
		for (; prov_chain != NULL; prov_chain = prov_chain->pm_next) {
			if (prov_chain->pm_prov_desc == pd &&
			    IS_FG_SUPPORTED(prov_chain, fg)) {
				return (B_TRUE);
			}
		}
	}
	return (B_FALSE);
}

/*
 * This routine, given a logical provider, returns the least loaded
 * provider belonging to the logical provider. The provider must be
 * able to do the specified mechanism, i.e. check that the mechanism
 * hasn't been disabled. In addition, just in case providers are not
 * entirely equivalent, the provider's entry point is checked for
 * non-nullness. This is accomplished by having the caller pass, as
 * arguments, the offset of the function group (offset_1), and the
 * offset of the function within the function group (offset_2).
 * Returns NULL if no provider can be found.
 */
int
kcf_get_hardware_provider(crypto_mech_type_t mech_type_1,
    crypto_mech_type_t mech_type_2, boolean_t call_restrict,
    kcf_provider_desc_t *old, kcf_provider_desc_t **new, crypto_func_group_t fg)
{
	kcf_provider_desc_t *provider, *real_pd = old;
	kcf_provider_desc_t *gpd = NULL;	/* good provider */
	kcf_provider_desc_t *bpd = NULL;	/* busy provider */
	kcf_provider_list_t *p;
	kcf_ops_class_t class;
	kcf_mech_entry_t *me;
	const kcf_mech_entry_tab_t *me_tab;
	int index, len, gqlen = INT_MAX, rv = CRYPTO_SUCCESS;

	/* get the mech entry for the specified mechanism */
	class = KCF_MECH2CLASS(mech_type_1);
	if ((class < KCF_FIRST_OPSCLASS) || (class > KCF_LAST_OPSCLASS)) {
		return (CRYPTO_MECHANISM_INVALID);
	}

	me_tab = &kcf_mech_tabs_tab[class];
	index = KCF_MECH2INDEX(mech_type_1);
	if ((index < 0) || (index >= me_tab->met_size)) {
		return (CRYPTO_MECHANISM_INVALID);
	}

	me = &((me_tab->met_tab)[index]);
	mutex_enter(&me->me_mutex);

	/*
	 * We assume the provider descriptor will not go away because
	 * it is being held somewhere, i.e. its reference count has been
	 * incremented. In the case of the crypto module, the provider
	 * descriptor is held by the session structure.
	 */
	if (old->pd_prov_type == CRYPTO_LOGICAL_PROVIDER) {
		if (old->pd_provider_list == NULL) {
			real_pd = NULL;
			rv = CRYPTO_DEVICE_ERROR;
			goto out;
		}
		/*
		 * Find the least loaded real provider. KCF_PROV_LOAD gives
		 * the load (number of pending requests) of the provider.
		 */
		mutex_enter(&old->pd_lock);
		p = old->pd_provider_list;
		while (p != NULL) {
			provider = p->pl_provider;

			ASSERT(provider->pd_prov_type !=
			    CRYPTO_LOGICAL_PROVIDER);

			if (call_restrict &&
			    (provider->pd_flags & KCF_PROV_RESTRICTED)) {
				p = p->pl_next;
				continue;
			}

			if (!is_valid_provider_for_mech(provider, me, fg)) {
				p = p->pl_next;
				continue;
			}

			/* provider does second mech */
			if (mech_type_2 != CRYPTO_MECH_INVALID) {
				int i;

				i = KCF_TO_PROV_MECH_INDX(provider,
				    mech_type_2);
				if (i == KCF_INVALID_INDX) {
					p = p->pl_next;
					continue;
				}
			}

			if (provider->pd_state != KCF_PROV_READY) {
				/* choose BUSY if no READY providers */
				if (provider->pd_state == KCF_PROV_BUSY)
					bpd = provider;
				p = p->pl_next;
				continue;
			}

			len = KCF_PROV_LOAD(provider);
			if (len < gqlen) {
				gqlen = len;
				gpd = provider;
			}

			p = p->pl_next;
		}

		if (gpd != NULL) {
			real_pd = gpd;
			KCF_PROV_REFHOLD(real_pd);
		} else if (bpd != NULL) {
			real_pd = bpd;
			KCF_PROV_REFHOLD(real_pd);
		} else {
			/* can't find provider */
			real_pd = NULL;
			rv = CRYPTO_MECHANISM_INVALID;
		}
		mutex_exit(&old->pd_lock);

	} else {
		if (!KCF_IS_PROV_USABLE(old) ||
		    (call_restrict && (old->pd_flags & KCF_PROV_RESTRICTED))) {
			real_pd = NULL;
			rv = CRYPTO_DEVICE_ERROR;
			goto out;
		}

		if (!is_valid_provider_for_mech(old, me, fg)) {
			real_pd = NULL;
			rv = CRYPTO_MECHANISM_INVALID;
			goto out;
		}

		KCF_PROV_REFHOLD(real_pd);
	}
out:
	mutex_exit(&me->me_mutex);
	*new = real_pd;
	return (rv);
}

/*
 * Return the best provider for the specified mechanism. The provider
 * is held and it is the caller's responsibility to release it when done.
 * The fg input argument is used as a search criterion to pick a provider.
 * A provider has to support this function group to be picked.
 *
 * Find the least loaded provider in the list of providers. We do a linear
 * search to find one. This is fine as we assume there are only a few
 * number of providers in this list. If this assumption ever changes,
 * we should revisit this.
 *
 * call_restrict represents if the caller should not be allowed to
 * use restricted providers.
 */
kcf_provider_desc_t *
kcf_get_mech_provider(crypto_mech_type_t mech_type, kcf_mech_entry_t **mepp,
    int *error, kcf_prov_tried_t *triedl, crypto_func_group_t fg,
    boolean_t call_restrict, size_t data_size)
{
	kcf_provider_desc_t *pd = NULL, *gpd = NULL;
	kcf_prov_mech_desc_t *prov_chain, *mdesc;
	int len, gqlen = INT_MAX;
	kcf_ops_class_t class;
	int index;
	kcf_mech_entry_t *me;
	const kcf_mech_entry_tab_t *me_tab;

	class = KCF_MECH2CLASS(mech_type);
	if ((class < KCF_FIRST_OPSCLASS) || (class > KCF_LAST_OPSCLASS)) {
		*error = CRYPTO_MECHANISM_INVALID;
		return (NULL);
	}

	me_tab = &kcf_mech_tabs_tab[class];
	index = KCF_MECH2INDEX(mech_type);
	if ((index < 0) || (index >= me_tab->met_size)) {
		*error = CRYPTO_MECHANISM_INVALID;
		return (NULL);
	}

	me = &((me_tab->met_tab)[index]);
	if (mepp != NULL)
		*mepp = me;

	mutex_enter(&me->me_mutex);

	prov_chain = me->me_hw_prov_chain;

	/*
	 * We check for the threshold for using a hardware provider for
	 * this amount of data. If there is no software provider available
	 * for the mechanism, then the threshold is ignored.
	 */
	if ((prov_chain != NULL) &&
	    ((data_size == 0) || (me->me_threshold == 0) ||
	    (data_size >= me->me_threshold) ||
	    ((mdesc = me->me_sw_prov) == NULL) ||
	    (!IS_FG_SUPPORTED(mdesc, fg)) ||
	    (!KCF_IS_PROV_USABLE(mdesc->pm_prov_desc)))) {
		ASSERT(me->me_num_hwprov > 0);
		/* there is at least one provider */

		/*
		 * Find the least loaded real provider. KCF_PROV_LOAD gives
		 * the load (number of pending requests) of the provider.
		 */
		while (prov_chain != NULL) {
			pd = prov_chain->pm_prov_desc;

			if (!IS_FG_SUPPORTED(prov_chain, fg) ||
			    !KCF_IS_PROV_USABLE(pd) ||
			    IS_PROVIDER_TRIED(pd, triedl) ||
			    (call_restrict &&
			    (pd->pd_flags & KCF_PROV_RESTRICTED))) {
				prov_chain = prov_chain->pm_next;
				continue;
			}

			if ((len = KCF_PROV_LOAD(pd)) < gqlen) {
				gqlen = len;
				gpd = pd;
			}

			prov_chain = prov_chain->pm_next;
		}

		pd = gpd;
	}

	/* No HW provider for this mech, is there a SW provider? */
	if (pd == NULL && (mdesc = me->me_sw_prov) != NULL) {
		pd = mdesc->pm_prov_desc;
		if (!IS_FG_SUPPORTED(mdesc, fg) ||
		    !KCF_IS_PROV_USABLE(pd) ||
		    IS_PROVIDER_TRIED(pd, triedl) ||
		    (call_restrict && (pd->pd_flags & KCF_PROV_RESTRICTED)))
			pd = NULL;
	}

	if (pd == NULL) {
		/*
		 * We do not want to report CRYPTO_MECH_NOT_SUPPORTED, when
		 * we are in the "fallback to the next provider" case. Rather
		 * we preserve the error, so that the client gets the right
		 * error code.
		 */
		if (triedl == NULL)
			*error = CRYPTO_MECH_NOT_SUPPORTED;
	} else
		KCF_PROV_REFHOLD(pd);

	mutex_exit(&me->me_mutex);
	return (pd);
}

/*
 * Do the actual work of calling the provider routines.
 *
 * pd - Provider structure
 * ctx - Context for this operation
 * params - Parameters for this operation
 * rhndl - Request handle to use for notification
 *
 * The return values are the same as that of the respective SPI.
 */
int
common_submit_request(kcf_provider_desc_t *pd, crypto_ctx_t *ctx,
    kcf_req_params_t *params, crypto_req_handle_t rhndl)
{
	int err = CRYPTO_ARGUMENTS_BAD;
	kcf_op_type_t optype;

	optype = params->rp_optype;

	switch (params->rp_opgrp) {
	case KCF_OG_DIGEST: {
		kcf_digest_ops_params_t *dops = &params->rp_u.digest_params;

		switch (optype) {
		case KCF_OP_INIT:
			/*
			 * We should do this only here and not in KCF_WRAP_*
			 * macros. This is because we may want to try other
			 * providers, in case we recover from a failure.
			 */
			KCF_SET_PROVIDER_MECHNUM(dops->do_framework_mechtype,
			    pd, &dops->do_mech);

			err = KCF_PROV_DIGEST_INIT(pd, ctx, &dops->do_mech,
			    rhndl);
			break;

		case KCF_OP_SINGLE:
			err = KCF_PROV_DIGEST(pd, ctx, dops->do_data,
			    dops->do_digest, rhndl);
			break;

		case KCF_OP_UPDATE:
			err = KCF_PROV_DIGEST_UPDATE(pd, ctx,
			    dops->do_data, rhndl);
			break;

		case KCF_OP_FINAL:
			err = KCF_PROV_DIGEST_FINAL(pd, ctx,
			    dops->do_digest, rhndl);
			break;

		case KCF_OP_ATOMIC:
			ASSERT(ctx == NULL);
			KCF_SET_PROVIDER_MECHNUM(dops->do_framework_mechtype,
			    pd, &dops->do_mech);
			err = KCF_PROV_DIGEST_ATOMIC(pd, dops->do_sid,
			    &dops->do_mech, dops->do_data, dops->do_digest,
			    rhndl);
			break;

		case KCF_OP_DIGEST_KEY:
			err = KCF_PROV_DIGEST_KEY(pd, ctx, dops->do_digest_key,
			    rhndl);
			break;

		default:
			break;
		}
		break;
	}

	case KCF_OG_MAC: {
		kcf_mac_ops_params_t *mops = &params->rp_u.mac_params;

		switch (optype) {
		case KCF_OP_INIT:
			KCF_SET_PROVIDER_MECHNUM(mops->mo_framework_mechtype,
			    pd, &mops->mo_mech);

			err = KCF_PROV_MAC_INIT(pd, ctx, &mops->mo_mech,
			    mops->mo_key, mops->mo_templ, rhndl);
			break;

		case KCF_OP_SINGLE:
			err = KCF_PROV_MAC(pd, ctx, mops->mo_data,
			    mops->mo_mac, rhndl);
			break;

		case KCF_OP_UPDATE:
			err = KCF_PROV_MAC_UPDATE(pd, ctx, mops->mo_data,
			    rhndl);
			break;

		case KCF_OP_FINAL:
			err = KCF_PROV_MAC_FINAL(pd, ctx, mops->mo_mac, rhndl);
			break;

		case KCF_OP_ATOMIC:
			ASSERT(ctx == NULL);
			KCF_SET_PROVIDER_MECHNUM(mops->mo_framework_mechtype,
			    pd, &mops->mo_mech);

			err = KCF_PROV_MAC_ATOMIC(pd, mops->mo_sid,
			    &mops->mo_mech, mops->mo_key, mops->mo_data,
			    mops->mo_mac, mops->mo_templ, rhndl);
			break;

		case KCF_OP_MAC_VERIFY_ATOMIC:
			ASSERT(ctx == NULL);
			KCF_SET_PROVIDER_MECHNUM(mops->mo_framework_mechtype,
			    pd, &mops->mo_mech);

			err = KCF_PROV_MAC_VERIFY_ATOMIC(pd, mops->mo_sid,
			    &mops->mo_mech, mops->mo_key, mops->mo_data,
			    mops->mo_mac, mops->mo_templ, rhndl);
			break;

		default:
			break;
		}
		break;
	}

	case KCF_OG_ENCRYPT: {
		kcf_encrypt_ops_params_t *eops = &params->rp_u.encrypt_params;

		switch (optype) {
		case KCF_OP_INIT:
			KCF_SET_PROVIDER_MECHNUM(eops->eo_framework_mechtype,
			    pd, &eops->eo_mech);

			err = KCF_PROV_ENCRYPT_INIT(pd, ctx, &eops->eo_mech,
			    eops->eo_key, eops->eo_templ, rhndl);
			break;

		case KCF_OP_SINGLE:
			err = KCF_PROV_ENCRYPT(pd, ctx, eops->eo_plaintext,
			    eops->eo_ciphertext, rhndl);
			break;

		case KCF_OP_UPDATE:
			err = KCF_PROV_ENCRYPT_UPDATE(pd, ctx,
			    eops->eo_plaintext, eops->eo_ciphertext, rhndl);
			break;

		case KCF_OP_FINAL:
			err = KCF_PROV_ENCRYPT_FINAL(pd, ctx,
			    eops->eo_ciphertext, rhndl);
			break;

		case KCF_OP_ATOMIC:
			ASSERT(ctx == NULL);
			KCF_SET_PROVIDER_MECHNUM(eops->eo_framework_mechtype,
			    pd, &eops->eo_mech);

			err = KCF_PROV_ENCRYPT_ATOMIC(pd, eops->eo_sid,
			    &eops->eo_mech, eops->eo_key, eops->eo_plaintext,
			    eops->eo_ciphertext, eops->eo_templ, rhndl);
			break;

		default:
			break;
		}
		break;
	}

	case KCF_OG_DECRYPT: {
		kcf_decrypt_ops_params_t *dcrops = &params->rp_u.decrypt_params;

		switch (optype) {
		case KCF_OP_INIT:
			KCF_SET_PROVIDER_MECHNUM(dcrops->dop_framework_mechtype,
			    pd, &dcrops->dop_mech);

			err = KCF_PROV_DECRYPT_INIT(pd, ctx, &dcrops->dop_mech,
			    dcrops->dop_key, dcrops->dop_templ, rhndl);
			break;

		case KCF_OP_SINGLE:
			err = KCF_PROV_DECRYPT(pd, ctx, dcrops->dop_ciphertext,
			    dcrops->dop_plaintext, rhndl);
			break;

		case KCF_OP_UPDATE:
			err = KCF_PROV_DECRYPT_UPDATE(pd, ctx,
			    dcrops->dop_ciphertext, dcrops->dop_plaintext,
			    rhndl);
			break;

		case KCF_OP_FINAL:
			err = KCF_PROV_DECRYPT_FINAL(pd, ctx,
			    dcrops->dop_plaintext, rhndl);
			break;

		case KCF_OP_ATOMIC:
			ASSERT(ctx == NULL);
			KCF_SET_PROVIDER_MECHNUM(dcrops->dop_framework_mechtype,
			    pd, &dcrops->dop_mech);

			err = KCF_PROV_DECRYPT_ATOMIC(pd, dcrops->dop_sid,
			    &dcrops->dop_mech, dcrops->dop_key,
			    dcrops->dop_ciphertext, dcrops->dop_plaintext,
			    dcrops->dop_templ, rhndl);
			break;

		default:
			break;
		}
		break;
	}
	default:
		break;
	}		/* end of switch(params->rp_opgrp) */

	KCF_PROV_INCRSTATS(pd, err);
	return (err);
}
