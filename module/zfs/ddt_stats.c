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
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2016 by Delphix. All rights reserved.
 * Copyright (c) 2022 by Pawel Jakub Dawidek
 * Copyright (c) 2023, Klara Inc.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/ddt.h>
#include <sys/ddt_impl.h>

static void
ddt_stat_generate(ddt_t *ddt, ddt_entry_t *dde, ddt_stat_t *dds)
{
	spa_t *spa = ddt->ddt_spa;
	ddt_phys_t *ddp = dde->dde_phys;
	ddt_key_t *ddk = &dde->dde_key;
	uint64_t lsize = DDK_GET_LSIZE(ddk);
	uint64_t psize = DDK_GET_PSIZE(ddk);

	memset(dds, 0, sizeof (*dds));

	for (int p = 0; p < DDT_PHYS_TYPES; p++, ddp++) {
		uint64_t dsize = 0;
		uint64_t refcnt = ddp->ddp_refcnt;

		if (ddp->ddp_phys_birth == 0)
			continue;

		int ndvas = DDK_GET_CRYPT(&dde->dde_key) ?
		    SPA_DVAS_PER_BP - 1 : SPA_DVAS_PER_BP;
		for (int d = 0; d < ndvas; d++)
			dsize += dva_get_dsize_sync(spa, &ddp->ddp_dva[d]);

		dds->dds_blocks += 1;
		dds->dds_lsize += lsize;
		dds->dds_psize += psize;
		dds->dds_dsize += dsize;

		dds->dds_ref_blocks += refcnt;
		dds->dds_ref_lsize += lsize * refcnt;
		dds->dds_ref_psize += psize * refcnt;
		dds->dds_ref_dsize += dsize * refcnt;
	}
}

void
ddt_stat_add(ddt_stat_t *dst, const ddt_stat_t *src, uint64_t neg)
{
	const uint64_t *s = (const uint64_t *)src;
	uint64_t *d = (uint64_t *)dst;
	uint64_t *d_end = (uint64_t *)(dst + 1);

	ASSERT(neg == 0 || neg == -1ULL);	/* add or subtract */

	for (int i = 0; i < d_end - d; i++)
		d[i] += (s[i] ^ neg) - neg;
}

void
ddt_stat_update(ddt_t *ddt, ddt_entry_t *dde, uint64_t neg)
{
	ddt_stat_t dds;
	ddt_histogram_t *ddh;
	int bucket;

	ddt_stat_generate(ddt, dde, &dds);

	bucket = highbit64(dds.dds_ref_blocks) - 1;
	ASSERT3U(bucket, >=, 0);

	ddh = &ddt->ddt_histogram[dde->dde_type][dde->dde_class];

	ddt_stat_add(&ddh->ddh_stat[bucket], &dds, neg);
}

void
ddt_histogram_add(ddt_histogram_t *dst, const ddt_histogram_t *src)
{
	for (int h = 0; h < 64; h++)
		ddt_stat_add(&dst->ddh_stat[h], &src->ddh_stat[h], 0);
}

void
ddt_histogram_stat(ddt_stat_t *dds, const ddt_histogram_t *ddh)
{
	memset(dds, 0, sizeof (*dds));

	for (int h = 0; h < 64; h++)
		ddt_stat_add(dds, &ddh->ddh_stat[h], 0);
}

boolean_t
ddt_histogram_empty(const ddt_histogram_t *ddh)
{
	const uint64_t *s = (const uint64_t *)ddh;
	const uint64_t *s_end = (const uint64_t *)(ddh + 1);

	while (s < s_end)
		if (*s++ != 0)
			return (B_FALSE);

	return (B_TRUE);
}

void
ddt_get_dedup_object_stats(spa_t *spa, ddt_object_t *ddo_total)
{
	/* Sum the statistics we cached in ddt_object_sync(). */
	for (enum zio_checksum c = 0; c < ZIO_CHECKSUM_FUNCTIONS; c++) {
		ddt_t *ddt = spa->spa_ddt[c];
		if (!ddt)
			continue;

		for (ddt_type_t type = 0; type < DDT_TYPES; type++) {
			for (ddt_class_t class = 0; class < DDT_CLASSES;
			    class++) {
				ddt_object_t *ddo =
				    &ddt->ddt_object_stats[type][class];
				ddo_total->ddo_count += ddo->ddo_count;
				ddo_total->ddo_dspace += ddo->ddo_dspace;
				ddo_total->ddo_mspace += ddo->ddo_mspace;
			}
		}
	}

	/* ... and compute the averages. */
	if (ddo_total->ddo_count != 0) {
		ddo_total->ddo_dspace /= ddo_total->ddo_count;
		ddo_total->ddo_mspace /= ddo_total->ddo_count;
	}
}

void
ddt_get_dedup_histogram(spa_t *spa, ddt_histogram_t *ddh)
{
	for (enum zio_checksum c = 0; c < ZIO_CHECKSUM_FUNCTIONS; c++) {
		ddt_t *ddt = spa->spa_ddt[c];
		if (!ddt)
			continue;

		for (ddt_type_t type = 0; type < DDT_TYPES; type++) {
			for (ddt_class_t class = 0; class < DDT_CLASSES;
			    class++) {
				ddt_histogram_add(ddh,
				    &ddt->ddt_histogram_cache[type][class]);
			}
		}
	}
}

void
ddt_get_dedup_stats(spa_t *spa, ddt_stat_t *dds_total)
{
	ddt_histogram_t *ddh_total;

	ddh_total = kmem_zalloc(sizeof (ddt_histogram_t), KM_SLEEP);
	ddt_get_dedup_histogram(spa, ddh_total);
	ddt_histogram_stat(dds_total, ddh_total);
	kmem_free(ddh_total, sizeof (ddt_histogram_t));
}

uint64_t
ddt_get_dedup_dspace(spa_t *spa)
{
	ddt_stat_t dds_total;

	if (spa->spa_dedup_dspace != ~0ULL)
		return (spa->spa_dedup_dspace);

	memset(&dds_total, 0, sizeof (ddt_stat_t));

	/* Calculate and cache the stats */
	ddt_get_dedup_stats(spa, &dds_total);
	spa->spa_dedup_dspace = dds_total.dds_ref_dsize - dds_total.dds_dsize;
	return (spa->spa_dedup_dspace);
}

uint64_t
ddt_get_pool_dedup_ratio(spa_t *spa)
{
	ddt_stat_t dds_total = { 0 };

	ddt_get_dedup_stats(spa, &dds_total);
	if (dds_total.dds_dsize == 0)
		return (100);

	return (dds_total.dds_ref_dsize * 100 / dds_total.dds_dsize);
}
