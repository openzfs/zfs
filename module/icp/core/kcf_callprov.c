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
 * Return the best provider for the specified mechanism. The provider
 * is held and it is the caller's responsibility to release it when done.
 * The fg input argument is used as a search criterion to pick a provider.
 * A provider has to support this function group to be picked.
 *
 * Find the least loaded provider in the list of providers. We do a linear
 * search to find one. This is fine as we assume there are only a few
 * number of providers in this list. If this assumption ever changes,
 * we should revisit this.
 */
kcf_provider_desc_t *
kcf_get_mech_provider(crypto_mech_type_t mech_type, kcf_mech_entry_t **mepp,
    int *error, kcf_prov_tried_t *triedl, crypto_func_group_t fg)
{
	kcf_provider_desc_t *pd = NULL;
	kcf_prov_mech_desc_t *mdesc;
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

	/* Is there a provider? */
	if (pd == NULL && (mdesc = me->me_sw_prov) != NULL) {
		pd = mdesc->pm_prov_desc;
		if (!IS_FG_SUPPORTED(mdesc, fg) ||
		    !KCF_IS_PROV_USABLE(pd) ||
		    IS_PROVIDER_TRIED(pd, triedl))
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

	return (pd);
}
