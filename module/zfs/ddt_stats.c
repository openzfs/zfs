// SPDX-License-Identifier: CDDL-1.0
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
ddt_stat_generate(ddt_t *ddt, const ddt_lightweight_entry_t *ddlwe,
    ddt_stat_t *dds)
{
	spa_t *spa = ddt->ddt_spa;
	uint64_t lsize = DDK_GET_LSIZE(&ddlwe->ddlwe_key);
	uint64_t psize = DDK_GET_PSIZE(&ddlwe->ddlwe_key);

	memset(dds, 0, sizeof (*dds));

	for (int p = 0; p < DDT_NPHYS(ddt); p++) {
		const ddt_univ_phys_t *ddp = &ddlwe->ddlwe_phys;
		ddt_phys_variant_t v = DDT_PHYS_VARIANT(ddt, p);

		if (ddt_phys_birth(ddp, v) == 0)
			continue;

		int ndvas = ddt_phys_dva_count(ddp, v,
		    DDK_GET_CRYPT(&ddlwe->ddlwe_key));
		const dva_t *dvas = (ddt->ddt_flags & DDT_FLAG_FLAT) ?
		    ddp->ddp_flat.ddp_dva : ddp->ddp_trad[p].ddp_dva;

		uint64_t dsize = 0;
		for (int d = 0; d < ndvas; d++)
			dsize += dva_get_dsize_sync(spa, &dvas[d]);

		uint64_t refcnt = ddt_phys_refcnt(ddp, v);

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

static void
ddt_stat_add(ddt_stat_t *dst, const ddt_stat_t *src)
{
	dst->dds_blocks		+= src->dds_blocks;
	dst->dds_lsize		+= src->dds_lsize;
	dst->dds_psize		+= src->dds_psize;
	dst->dds_dsize		+= src->dds_dsize;
	dst->dds_ref_blocks	+= src->dds_ref_blocks;
	dst->dds_ref_lsize	+= src->dds_ref_lsize;
	dst->dds_ref_psize	+= src->dds_ref_psize;
	dst->dds_ref_dsize	+= src->dds_ref_dsize;
}

static void
ddt_stat_sub(ddt_stat_t *dst, const ddt_stat_t *src)
{
	/* This caught more during development than you might expect... */
	ASSERT3U(dst->dds_blocks, >=, src->dds_blocks);
	ASSERT3U(dst->dds_lsize, >=, src->dds_lsize);
	ASSERT3U(dst->dds_psize, >=, src->dds_psize);
	ASSERT3U(dst->dds_dsize, >=, src->dds_dsize);
	ASSERT3U(dst->dds_ref_blocks, >=, src->dds_ref_blocks);
	ASSERT3U(dst->dds_ref_lsize, >=, src->dds_ref_lsize);
	ASSERT3U(dst->dds_ref_psize, >=, src->dds_ref_psize);
	ASSERT3U(dst->dds_ref_dsize, >=, src->dds_ref_dsize);

	dst->dds_blocks		-= src->dds_blocks;
	dst->dds_lsize		-= src->dds_lsize;
	dst->dds_psize		-= src->dds_psize;
	dst->dds_dsize		-= src->dds_dsize;
	dst->dds_ref_blocks	-= src->dds_ref_blocks;
	dst->dds_ref_lsize	-= src->dds_ref_lsize;
	dst->dds_ref_psize	-= src->dds_ref_psize;
	dst->dds_ref_dsize	-= src->dds_ref_dsize;
}

void
ddt_histogram_add_entry(ddt_t *ddt, ddt_histogram_t *ddh,
    const ddt_lightweight_entry_t *ddlwe)
{
	ddt_stat_t dds;
	int bucket;

	ddt_stat_generate(ddt, ddlwe, &dds);

	bucket = highbit64(dds.dds_ref_blocks) - 1;
	if (bucket < 0)
		return;

	ddt_stat_add(&ddh->ddh_stat[bucket], &dds);
}

void
ddt_histogram_sub_entry(ddt_t *ddt, ddt_histogram_t *ddh,
    const ddt_lightweight_entry_t *ddlwe)
{
	ddt_stat_t dds;
	int bucket;

	ddt_stat_generate(ddt, ddlwe, &dds);

	bucket = highbit64(dds.dds_ref_blocks) - 1;
	if (bucket < 0)
		return;

	ddt_stat_sub(&ddh->ddh_stat[bucket], &dds);
}

void
ddt_histogram_add(ddt_histogram_t *dst, const ddt_histogram_t *src)
{
	for (int h = 0; h < 64; h++)
		ddt_stat_add(&dst->ddh_stat[h], &src->ddh_stat[h]);
}

void
ddt_histogram_total(ddt_stat_t *dds, const ddt_histogram_t *ddh)
{
	memset(dds, 0, sizeof (*dds));

	for (int h = 0; h < 64; h++)
		ddt_stat_add(dds, &ddh->ddh_stat[h]);
}

boolean_t
ddt_histogram_empty(const ddt_histogram_t *ddh)
{
	for (int h = 0; h < 64; h++) {
		const ddt_stat_t *dds = &ddh->ddh_stat[h];

		if (dds->dds_blocks == 0 &&
		    dds->dds_lsize == 0 &&
		    dds->dds_psize == 0 &&
		    dds->dds_dsize == 0 &&
		    dds->dds_ref_blocks == 0 &&
		    dds->dds_ref_lsize == 0 &&
		    dds->dds_ref_psize == 0 &&
		    dds->dds_ref_dsize == 0)
			continue;

		return (B_FALSE);
	}

	return (B_TRUE);
}

void
ddt_get_dedup_object_stats(spa_t *spa, ddt_object_t *ddo_total)
{
	memset(ddo_total, 0, sizeof (*ddo_total));

	for (enum zio_checksum c = 0; c < ZIO_CHECKSUM_FUNCTIONS; c++) {
		ddt_t *ddt = spa->spa_ddt[c];
		if (!ddt)
			continue;

		for (ddt_type_t type = 0; type < DDT_TYPES; type++) {
			for (ddt_class_t class = 0; class < DDT_CLASSES;
			    class++) {
				dmu_object_info_t doi;
				uint64_t cnt;
				int err;

				/*
				 * These stats were originally calculated
				 * during ddt_object_load().
				 */

				err = ddt_object_info(ddt, type, class, &doi);
				if (err != 0)
					continue;

				err = ddt_object_count(ddt, type, class, &cnt);
				if (err != 0)
					continue;

				ddt_object_t *ddo =
				    &ddt->ddt_object_stats[type][class];

				ddo->ddo_count = cnt;
				ddo->ddo_dspace =
				    doi.doi_physical_blocks_512 << 9;
				ddo->ddo_mspace = doi.doi_fill_count *
				    doi.doi_data_block_size;

				ddo_total->ddo_count += ddo->ddo_count;
				ddo_total->ddo_dspace += ddo->ddo_dspace;
				ddo_total->ddo_mspace += ddo->ddo_mspace;
			}
		}

		ddt_object_t *ddo = &ddt->ddt_log_stats;
		ddo_total->ddo_count += ddo->ddo_count;
		ddo_total->ddo_dspace += ddo->ddo_dspace;
		ddo_total->ddo_mspace += ddo->ddo_mspace;
	}

	/*
	 * This returns raw counts (not averages). One of the consumers,
	 * print_dedup_stats(), historically has expected raw counts.
	 */

	spa->spa_dedup_dsize = ddo_total->ddo_dspace;
}

uint64_t
ddt_get_ddt_dsize(spa_t *spa)
{
	ddt_object_t ddo_total;

	/* recalculate after each txg sync */
	if (spa->spa_dedup_dsize == ~0ULL)
		ddt_get_dedup_object_stats(spa, &ddo_total);

	return (spa->spa_dedup_dsize);
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

		ddt_histogram_add(ddh, &ddt->ddt_log_histogram);
	}
}

void
ddt_get_dedup_stats(spa_t *spa, ddt_stat_t *dds_total)
{
	ddt_histogram_t *ddh_total;

	ddh_total = kmem_zalloc(sizeof (ddt_histogram_t), KM_SLEEP);
	ddt_get_dedup_histogram(spa, ddh_total);
	ddt_histogram_total(dds_total, ddh_total);
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

int
ddt_get_pool_dedup_cached(spa_t *spa, uint64_t *psize)
{
	uint64_t l1sz, l1tot, l2sz, l2tot;
	int err = 0;

	l1tot = l2tot = 0;
	*psize = 0;
	for (enum zio_checksum c = 0; c < ZIO_CHECKSUM_FUNCTIONS; c++) {
		ddt_t *ddt = spa->spa_ddt[c];
		if (ddt == NULL)
			continue;
		for (ddt_type_t type = 0; type < DDT_TYPES; type++) {
			for (ddt_class_t class = 0; class < DDT_CLASSES;
			    class++) {
				err = dmu_object_cached_size(ddt->ddt_os,
				    ddt->ddt_object[type][class], &l1sz, &l2sz);
				if (err != 0)
					return (err);
				l1tot += l1sz;
				l2tot += l2sz;
			}
		}
	}

	*psize = l1tot + l2tot;
	return (err);
}
