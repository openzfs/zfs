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
 * Copyright (c) 2018 by Delphix. All rights reserved.
 * Copyright (c) 2018 Datto Inc.
 */

#include <sys/dataset_kstats.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_dataset.h>
#include <sys/spa.h>

static dataset_kstat_values_t empty_dataset_kstats = {
	{ "dataset_name",	KSTAT_DATA_STRING },
	{ "writes",	KSTAT_DATA_UINT64 },
	{ "nwritten",	KSTAT_DATA_UINT64 },
	{ "reads",	KSTAT_DATA_UINT64 },
	{ "nread",	KSTAT_DATA_UINT64 },
	{ "nunlinks",	KSTAT_DATA_UINT64 },
	{ "nunlinked",	KSTAT_DATA_UINT64 },
	{
	{ "zil_commit_count",			KSTAT_DATA_UINT64 },
	{ "zil_commit_writer_count",		KSTAT_DATA_UINT64 },
	{ "zil_itx_count",			KSTAT_DATA_UINT64 },
	{ "zil_itx_indirect_count",		KSTAT_DATA_UINT64 },
	{ "zil_itx_indirect_bytes",		KSTAT_DATA_UINT64 },
	{ "zil_itx_copied_count",		KSTAT_DATA_UINT64 },
	{ "zil_itx_copied_bytes",		KSTAT_DATA_UINT64 },
	{ "zil_itx_needcopy_count",		KSTAT_DATA_UINT64 },
	{ "zil_itx_needcopy_bytes",		KSTAT_DATA_UINT64 },
	{ "zil_itx_metaslab_normal_count",	KSTAT_DATA_UINT64 },
	{ "zil_itx_metaslab_normal_bytes",	KSTAT_DATA_UINT64 },
	{ "zil_itx_metaslab_slog_count",	KSTAT_DATA_UINT64 },
	{ "zil_itx_metaslab_slog_bytes",	KSTAT_DATA_UINT64 }
	}
};

static int
dataset_kstats_update(kstat_t *ksp, int rw)
{
	dataset_kstats_t *dk = ksp->ks_private;
	dataset_kstat_values_t *dkv = ksp->ks_data;
	ASSERT3P(dk->dk_kstats->ks_data, ==, dkv);

	if (rw == KSTAT_WRITE)
		return (EACCES);

	dkv->dkv_writes.value.ui64 =
	    wmsum_value(&dk->dk_sums.dss_writes);
	dkv->dkv_nwritten.value.ui64 =
	    wmsum_value(&dk->dk_sums.dss_nwritten);
	dkv->dkv_reads.value.ui64 =
	    wmsum_value(&dk->dk_sums.dss_reads);
	dkv->dkv_nread.value.ui64 =
	    wmsum_value(&dk->dk_sums.dss_nread);
	dkv->dkv_nunlinks.value.ui64 =
	    wmsum_value(&dk->dk_sums.dss_nunlinks);
	dkv->dkv_nunlinked.value.ui64 =
	    wmsum_value(&dk->dk_sums.dss_nunlinked);

	zil_kstat_values_update(&dkv->dkv_zil_stats, &dk->dk_zil_sums);

	return (0);
}

int
dataset_kstats_create(dataset_kstats_t *dk, objset_t *objset)
{
	/*
	 * There should not be anything wrong with having kstats for
	 * snapshots. Since we are not sure how useful they would be
	 * though nor how much their memory overhead would matter in
	 * a filesystem with many snapshots, we skip them for now.
	 */
	if (dmu_objset_is_snapshot(objset))
		return (0);

	/*
	 * At the time of this writing, KSTAT_STRLEN is 255 in Linux,
	 * and the spa_name can theoretically be up to 256 characters.
	 * In reality though the spa_name can be 240 characters max
	 * [see origin directory name check in pool_namecheck()]. Thus,
	 * the naming scheme for the module name below should not cause
	 * any truncations. In the event that a truncation does happen
	 * though, due to some future change, we silently skip creating
	 * the kstat and log the event.
	 */
	char kstat_module_name[KSTAT_STRLEN];
	int n = snprintf(kstat_module_name, sizeof (kstat_module_name),
	    "zfs/%s", spa_name(dmu_objset_spa(objset)));
	if (n < 0) {
		zfs_dbgmsg("failed to create dataset kstat for objset %lld: "
		    " snprintf() for kstat module name returned %d",
		    (unsigned long long)dmu_objset_id(objset), n);
		return (SET_ERROR(EINVAL));
	} else if (n >= KSTAT_STRLEN) {
		zfs_dbgmsg("failed to create dataset kstat for objset %lld: "
		    "kstat module name length (%d) exceeds limit (%d)",
		    (unsigned long long)dmu_objset_id(objset),
		    n, KSTAT_STRLEN);
		return (SET_ERROR(ENAMETOOLONG));
	}

	char kstat_name[KSTAT_STRLEN];
	n = snprintf(kstat_name, sizeof (kstat_name), "objset-0x%llx",
	    (unsigned long long)dmu_objset_id(objset));
	if (n < 0) {
		zfs_dbgmsg("failed to create dataset kstat for objset %lld: "
		    " snprintf() for kstat name returned %d",
		    (unsigned long long)dmu_objset_id(objset), n);
		return (SET_ERROR(EINVAL));
	} else if (n >= KSTAT_STRLEN) {
		zfs_dbgmsg("failed to create dataset kstat for objset %lld: "
		    "kstat name length (%d) exceeds limit (%d)",
		    (unsigned long long)dmu_objset_id(objset),
		    n, KSTAT_STRLEN);
		return (SET_ERROR(ENAMETOOLONG));
	}

	kstat_t *kstat = kstat_create(kstat_module_name, 0, kstat_name,
	    "dataset", KSTAT_TYPE_NAMED,
	    sizeof (empty_dataset_kstats) / sizeof (kstat_named_t),
	    KSTAT_FLAG_VIRTUAL);
	if (kstat == NULL)
		return (SET_ERROR(ENOMEM));

	dataset_kstat_values_t *dk_kstats =
	    kmem_alloc(sizeof (empty_dataset_kstats), KM_SLEEP);
	memcpy(dk_kstats, &empty_dataset_kstats,
	    sizeof (empty_dataset_kstats));

	char *ds_name = kmem_zalloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);
	dsl_dataset_name(objset->os_dsl_dataset, ds_name);
	KSTAT_NAMED_STR_PTR(&dk_kstats->dkv_ds_name) = ds_name;
	KSTAT_NAMED_STR_BUFLEN(&dk_kstats->dkv_ds_name) =
	    ZFS_MAX_DATASET_NAME_LEN;

	kstat->ks_data = dk_kstats;
	kstat->ks_update = dataset_kstats_update;
	kstat->ks_private = dk;
	kstat->ks_data_size += ZFS_MAX_DATASET_NAME_LEN;

	wmsum_init(&dk->dk_sums.dss_writes, 0);
	wmsum_init(&dk->dk_sums.dss_nwritten, 0);
	wmsum_init(&dk->dk_sums.dss_reads, 0);
	wmsum_init(&dk->dk_sums.dss_nread, 0);
	wmsum_init(&dk->dk_sums.dss_nunlinks, 0);
	wmsum_init(&dk->dk_sums.dss_nunlinked, 0);
	zil_sums_init(&dk->dk_zil_sums);

	dk->dk_kstats = kstat;
	kstat_install(kstat);
	return (0);
}

void
dataset_kstats_destroy(dataset_kstats_t *dk)
{
	if (dk->dk_kstats == NULL)
		return;

	dataset_kstat_values_t *dkv = dk->dk_kstats->ks_data;
	kstat_delete(dk->dk_kstats);
	dk->dk_kstats = NULL;
	kmem_free(KSTAT_NAMED_STR_PTR(&dkv->dkv_ds_name),
	    KSTAT_NAMED_STR_BUFLEN(&dkv->dkv_ds_name));
	kmem_free(dkv, sizeof (empty_dataset_kstats));

	wmsum_fini(&dk->dk_sums.dss_writes);
	wmsum_fini(&dk->dk_sums.dss_nwritten);
	wmsum_fini(&dk->dk_sums.dss_reads);
	wmsum_fini(&dk->dk_sums.dss_nread);
	wmsum_fini(&dk->dk_sums.dss_nunlinks);
	wmsum_fini(&dk->dk_sums.dss_nunlinked);
	zil_sums_fini(&dk->dk_zil_sums);
}

void
dataset_kstats_update_write_kstats(dataset_kstats_t *dk,
    int64_t nwritten)
{
	ASSERT3S(nwritten, >=, 0);

	if (dk->dk_kstats == NULL)
		return;

	wmsum_add(&dk->dk_sums.dss_writes, 1);
	wmsum_add(&dk->dk_sums.dss_nwritten, nwritten);
}

void
dataset_kstats_update_read_kstats(dataset_kstats_t *dk,
    int64_t nread)
{
	ASSERT3S(nread, >=, 0);

	if (dk->dk_kstats == NULL)
		return;

	wmsum_add(&dk->dk_sums.dss_reads, 1);
	wmsum_add(&dk->dk_sums.dss_nread, nread);
}

void
dataset_kstats_update_nunlinks_kstat(dataset_kstats_t *dk, int64_t delta)
{
	if (dk->dk_kstats == NULL)
		return;

	wmsum_add(&dk->dk_sums.dss_nunlinks, delta);
}

void
dataset_kstats_update_nunlinked_kstat(dataset_kstats_t *dk, int64_t delta)
{
	if (dk->dk_kstats == NULL)
		return;

	wmsum_add(&dk->dk_sums.dss_nunlinked, delta);
}
