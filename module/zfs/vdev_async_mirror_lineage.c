// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */

/*
 * Copyright (c) 2026, John White.
 */

/* Async mirror: lineage and divergent-copy checks. */

#include <sys/zfs_context.h>
#include <sys/abd.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_async_mirror.h>
#include <sys/vdev_async_mirror_impl.h>
#include <sys/uberblock_impl.h>
#include <sys/zap.h>
#include <sys/zfeature.h>
#include <sys/dmu.h>
#include <sys/dmu_tx.h>
#include <sys/dsl_pool.h>
#include <sys/fs/zfs.h>

static uint64_t
async_mirror_new_lineage(void)
{
	uint64_t v;

	do {
		(void) random_get_pseudo_bytes((uint8_t *)&v, sizeof (v));
	} while (v == 0);
	return (v);
}

void
async_mirror_lineage_start(spa_t *spa)
{
	async_mirror_lineage_phys_t *lin = &spa->spa_async_mirror_lin;

	memset(lin, 0, sizeof (*lin));
	lin->aml_version = ASYNC_MIRROR_LINEAGE_VERSION;
	lin->aml_lineage = async_mirror_new_lineage();
}

uint64_t
spa_async_mirror_lineage(spa_t *spa)
{
	return (spa->spa_async_mirror_lin.aml_lineage);
}

static boolean_t
async_mirror_lineage_tvd(const vdev_t *tvd)
{
	return (tvd->vdev_ops == &vdev_mirror_ops &&
	    tvd->vdev_children == 2 && !tvd->vdev_islog);
}

static uint64_t
async_mirror_label_value_locked(vdev_t *vd, uint64_t txg, const char *key)
{
	nvlist_t *label;
	uint64_t value = 0;

	label = vdev_label_read_config(vd, txg, VDEV_TRUSTED_LABELS(vd));
	if (label != NULL) {
		(void) nvlist_lookup_uint64(label, key, &value);
		nvlist_free(label);
	}
	return (value);
}

static boolean_t
async_mirror_lineage_compatible(spa_t *spa, vdev_t *cvd, uint64_t newest,
    uint64_t label)
{
	const async_mirror_lineage_phys_t *lin = &spa->spa_async_mirror_lin;

	if (newest == 0)
		return (B_TRUE);
	if (label != 0 && label != cvd->vdev_async_label_lineage)
		return (B_FALSE);

	if (lin->aml_parent == 0 ||
	    !async_mirror_lineage_absent(lin, cvd->vdev_guid) ||
	    newest <= lin->aml_fork_txg || label == lin->aml_lineage)
		return (B_TRUE);
	return ((lin->aml_flags & ASYNC_MIRROR_FORK_REWIND) != 0 &&
	    newest <= lin->aml_seen);
}

static boolean_t
async_mirror_lineage_clean(spa_t *spa, vdev_t **missingp)
{
	vdev_t *rvd = spa->spa_root_vdev;
	vdev_t *missing = NULL;
	boolean_t clean = B_TRUE;

	for (uint64_t c = 0; c < rvd->vdev_children; c++) {
		vdev_t *tvd = rvd->vdev_child[c];

		if (!async_mirror_lineage_tvd(tvd))
			continue;
		for (uint64_t i = 0; i < 2; i++) {
			vdev_t *cvd = tvd->vdev_child[i];

			if (!vdev_readable(cvd) || !vdev_writeable(cvd)) {
				if (missing == NULL)
					missing = cvd;
				clean = B_FALSE;
			} else if (async_mirror_child_needs_resilver(spa,
			    cvd)) {
				clean = B_FALSE;
			}
		}
	}
	if (missingp != NULL)
		*missingp = missing;
	return (clean);
}

int
vdev_async_mirror_lineage_load(spa_t *spa)
{
	objset_t *mos = spa_meta_objset(spa);
	async_mirror_lineage_phys_t *lin = &spa->spa_async_mirror_lin;
	vdev_t *rvd = spa->spa_root_vdev;
	uint64_t ilen, num;
	int error;

	memset(lin, 0, sizeof (*lin));
	error = zap_length(mos, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_ASYNC_MIRROR_LINEAGE, &ilen, &num);
	if (error == 0 && (ilen != sizeof (uint64_t) ||
	    num != ASYNC_MIRROR_LINEAGE_WORDS))
		error = SET_ERROR(EINVAL);
	if (error == 0) {
		error = zap_lookup(mos, DMU_POOL_DIRECTORY_OBJECT,
		    DMU_POOL_ASYNC_MIRROR_LINEAGE, sizeof (uint64_t),
		    ASYNC_MIRROR_LINEAGE_WORDS, lin);
	}
	if (error == 0 && (lin->aml_version != ASYNC_MIRROR_LINEAGE_VERSION ||
	    lin->aml_lineage == 0))
		error = SET_ERROR(EINVAL);
	if (error == 0 && ((lin->aml_flags &
	    ~ASYNC_MIRROR_LINEAGE_FLAGS) != 0 || lin->aml_nabsent >
	    ASYNC_MIRROR_LINEAGE_ABSENT))
		error = SET_ERROR(ENOTSUP);
	if (error == 0 && lin->aml_parent != 0 && lin->aml_nabsent == 0)
		error = SET_ERROR(EINVAL);
	for (uint64_t i = 0; error == 0 && i < lin->aml_nabsent; i++) {
		if (lin->aml_absent[i] == 0)
			error = SET_ERROR(EINVAL);
	}
	if (error == ENOENT) {
		memset(lin, 0, sizeof (*lin));
		error = 0;
	}
	if (error != 0) {
		spa_load_note(spa, "async mirror: cannot read the lineage "
		    "entry: error %d", error);
		return (error);
	}
	spa->spa_async_mirror_lin_disk = *lin;
	lin->aml_flags &= ~ASYNC_MIRROR_LINEAGE_ACTIVE;

	spa->spa_async_mirror_label_keys = B_FALSE;
	if (lin->aml_lineage == 0)
		return (0);
	for (uint64_t c = 0; c < rvd->vdev_children; c++) {
		vdev_t *tvd = rvd->vdev_child[c];

		if (!async_mirror_lineage_tvd(tvd))
			continue;
		for (uint64_t i = 0; i < 2; i++) {
			vdev_t *cvd = tvd->vdev_child[i];
			uint64_t v = 0;

			if (cvd->vdev_leaf_zap != 0) {
				error = zap_lookup(mos, cvd->vdev_leaf_zap,
				    VDEV_LEAF_ZAP_ASYNC_MIRROR_LINEAGE,
				    sizeof (uint64_t), 1, &v);
			}
			if (error == ENOENT) {
				v = 0;
				error = 0;
			}
			if (error != 0) {
				spa_load_note(spa, "async mirror: cannot read "
				    "the lineage of child guid %llu: error %d",
				    (u_longlong_t)cvd->vdev_guid, error);
				return (error);
			}
			cvd->vdev_async_label_lineage = v;
			if (v != 0)
				spa->spa_async_mirror_label_keys = B_TRUE;
		}
	}
	return (0);
}

static void
async_mirror_label_sync(spa_t *spa, dmu_tx_t *tx)
{
	objset_t *mos = spa_meta_objset(spa);
	vdev_t *rvd = spa->spa_root_vdev;
	uint64_t lineage = spa->spa_async_mirror_lin.aml_lineage;

	for (uint64_t c = 0; c < rvd->vdev_children; c++) {
		vdev_t *tvd = rvd->vdev_child[c];

		if (!async_mirror_lineage_tvd(tvd))
			continue;
		for (uint64_t i = 0; i < 2; i++) {
			vdev_t *cvd = tvd->vdev_child[i];

			if (!cvd->vdev_ops->vdev_op_leaf ||
			    cvd->vdev_leaf_zap == 0 ||
			    cvd->vdev_async_label_lineage == lineage ||
			    !vdev_writeable(cvd) ||
			    vdev_async_mirror_skip_labels(cvd))
				continue;
			if (lineage != 0) {
				VERIFY0(zap_update(mos, cvd->vdev_leaf_zap,
				    VDEV_LEAF_ZAP_ASYNC_MIRROR_LINEAGE,
				    sizeof (uint64_t), 1, &lineage, tx));
				spa->spa_async_mirror_label_keys = B_TRUE;
			} else {
				VERIFY0(zap_remove(mos, cvd->vdev_leaf_zap,
				    VDEV_LEAF_ZAP_ASYNC_MIRROR_LINEAGE, tx));
			}
			cvd->vdev_async_label_lineage = lineage;
			vdev_config_dirty(tvd);
		}
	}
}

void
vdev_async_mirror_sync_lineage(spa_t *spa, dmu_tx_t *tx)
{
	async_mirror_lineage_phys_t want = spa->spa_async_mirror_lin;
	async_mirror_lineage_phys_t *disk = &spa->spa_async_mirror_lin_disk;
	objset_t *mos = spa_meta_objset(spa);

	ASSERT(dsl_pool_sync_context(spa_get_dsl(spa)));

	if (want.aml_lineage == 0 && disk->aml_version == 0 &&
	    !spa->spa_async_mirror_label_keys)
		return;

	async_mirror_label_sync(spa, tx);

	if (want.aml_lineage == 0)
		memset(&want, 0, sizeof (want));
	else if (want.aml_parent != 0 || !async_mirror_lineage_clean(spa, NULL))
		want.aml_flags |= ASYNC_MIRROR_LINEAGE_ACTIVE;
	if (memcmp(&want, disk, sizeof (want)) == 0)
		return;

	boolean_t was = (disk->aml_flags & ASYNC_MIRROR_LINEAGE_ACTIVE) != 0;
	boolean_t is = (want.aml_flags & ASYNC_MIRROR_LINEAGE_ACTIVE) != 0;

	if (want.aml_lineage != 0) {
		ASSERT(spa_feature_is_enabled(spa, SPA_FEATURE_ASYNC_MIRROR));
		VERIFY0(zap_update(mos, DMU_POOL_DIRECTORY_OBJECT,
		    DMU_POOL_ASYNC_MIRROR_LINEAGE, sizeof (uint64_t),
		    ASYNC_MIRROR_LINEAGE_WORDS, &want, tx));
	} else {
		VERIFY0(zap_remove(mos, DMU_POOL_DIRECTORY_OBJECT,
		    DMU_POOL_ASYNC_MIRROR_LINEAGE, tx));
	}
	if (is && !was) {
		spa_feature_incr(spa, SPA_FEATURE_ASYNC_MIRROR, tx);
		spa_history_log_internal(spa, "async mirror", tx,
		    "the copies of lineage %llx have not met; the feature "
		    "stays active", (u_longlong_t)want.aml_lineage);
	} else if (was && !is) {
		spa_feature_decr(spa, SPA_FEATURE_ASYNC_MIRROR, tx);
		spa_history_log_internal(spa, "async mirror", tx,
		    "the copies of lineage %llx have met",
		    (u_longlong_t)disk->aml_lineage);
	}
	*disk = want;
}

static int
async_mirror_keep_copy(spa_t *spa, vdev_t *tvd, const uberblock_t *newest)
{
	uint64_t keep = spa->spa_async_mirror_import.ami_keep;
	int k;

	if (tvd->vdev_child[0]->vdev_guid == keep)
		k = 0;
	else if (tvd->vdev_child[1]->vdev_guid == keep)
		k = 1;
	else {
		spa_load_note(spa, "async mirror: the copy to keep, guid "
		    "%llu, is not a child of this mirror", (u_longlong_t)keep);
		return (SET_ERROR(ENOENT));
	}

	vdev_t *other = tvd->vdev_child[1 - k];

	if (newest[k].ub_txg == 0) {
		spa_load_note(spa, "async mirror: the copy to keep, guid "
		    "%llu, has no uberblock", (u_longlong_t)keep);
		return (SET_ERROR(ENOENT));
	}

	if (!other->vdev_ops->vdev_op_leaf || !vdev_writeable(other)) {
		spa_load_note(spa, "async mirror: cannot erase the uberblocks "
		    "of child guid %llu", (u_longlong_t)other->vdev_guid);
		return (SET_ERROR(ENXIO));
	}
	spa_load_note(spa, "async mirror: the operator keeps the copy of "
	    "child guid %llu at txg %llu; every uberblock of child guid %llu "
	    "is erased once that copy has loaded", (u_longlong_t)keep,
	    (u_longlong_t)newest[k].ub_txg, (u_longlong_t)other->vdev_guid);
	spa->spa_async_mirror_skip = other->vdev_guid;
	return (SET_ERROR(ERESTART));
}

int
vdev_async_mirror_check_lineage(spa_t *spa)
{
	vdev_t *rvd = spa->spa_root_vdev;
	int error = vdev_async_mirror_lineage_load(spa);

	if (error != 0)
		return (error);
	if (spa_async_mirror_lineage(spa) == 0)
		return (0);

	for (uint64_t c = 0; c < rvd->vdev_children; c++) {
		vdev_t *tvd = rvd->vdev_child[c];
		uberblock_t best, newest[2];
		boolean_t diverged = B_FALSE;

		if (!async_mirror_lineage_tvd(tvd))
			continue;
		for (uint64_t i = 0; i < 2; i++) {
			vdev_t *cvd = tvd->vdev_child[i];

			memset(&newest[i], 0, sizeof (newest[i]));
			if (!vdev_readable(cvd))
				continue;
			async_mirror_read_leaf_ub(spa, cvd, &best,
			    &newest[i], NULL);
			uint64_t label = 0;
			if (newest[i].ub_txg != 0 &&
			    cvd->vdev_ops->vdev_op_leaf) {
				spa_config_enter(spa, SCL_STATE_ALL, FTAG,
				    RW_WRITER);
				label = async_mirror_label_value_locked(cvd,
				    newest[i].ub_txg,
				    ZPOOL_CONFIG_ASYNC_MIRROR_LINEAGE);
				spa_config_exit(spa, SCL_STATE_ALL, FTAG);
			}
			if (!async_mirror_lineage_compatible(spa, cvd,
			    newest[i].ub_txg, label))
				diverged = B_TRUE;
		}
		if (!diverged)
			continue;

		uint64_t info[4] = {
			tvd->vdev_child[0]->vdev_guid, newest[0].ub_txg,
			tvd->vdev_child[1]->vdev_guid, newest[1].ub_txg
		};

		spa_load_note(spa, "async mirror: the copies were written "
		    "separately: child guid %llu at txg %llu, child guid %llu "
		    "at txg %llu", (u_longlong_t)info[0], (u_longlong_t)info[1],
		    (u_longlong_t)info[2], (u_longlong_t)info[3]);
		if ((spa->spa_mode & SPA_MODE_WRITE) &&
		    spa->spa_async_mirror_import.ami_keep != 0) {
			error = async_mirror_keep_copy(spa, tvd, newest);
			if (error == ERESTART)
				return (error);
		}
		fnvlist_add_uint64_array(spa->spa_load_info,
		    ZPOOL_CONFIG_ASYNC_MIRROR_DIVERGED, info, 4);
		if (spa->spa_mode & SPA_MODE_WRITE)
			return (SET_ERROR(ZFS_ERR_ASYNC_MIRROR_DIVERGED));
	}
	return (0);
}

boolean_t
async_mirror_label_has_legacy_mark(spa_t *spa, vdev_t *vd)
{
	if (vd == NULL || !vd->vdev_ops->vdev_op_leaf || !vdev_readable(vd))
		return (B_FALSE);

	spa_config_enter(spa, SCL_STATE_ALL, FTAG, RW_WRITER);
	uint64_t mark = async_mirror_label_value_locked(vd, UINT64_MAX,
	    ZPOOL_CONFIG_ASYNC_MIRROR_LEGACY_MARK);
	spa_config_exit(spa, SCL_STATE_ALL, FTAG);
	return (mark != 0);
}

int
vdev_async_mirror_check_legacy_mark(spa_t *spa)
{
	vdev_t *rvd = spa->spa_root_vdev;
	boolean_t forced = !(spa->spa_mode & SPA_MODE_WRITE) &&
	    (spa->spa_import_flags & ZFS_IMPORT_ANY_HOST) != 0;

	for (uint64_t c = 0; c < rvd->vdev_children; c++) {
		vdev_t *tvd = rvd->vdev_child[c];

		if (!async_mirror_lineage_tvd(tvd))
			continue;
		for (uint64_t i = 0; i < 2; i++) {
			vdev_t *cvd = tvd->vdev_child[i];
			vdev_t *other = tvd->vdev_child[1 - i];

			if (vdev_readable(other) ||
			    !async_mirror_label_has_legacy_mark(spa, cvd))
				continue;
			spa_load_note(spa,
			    "async mirror: child %llu has a legacy mark; "
			    "its mirror has no other copy%s",
			    (u_longlong_t)cvd->vdev_guid, forced ?
			    "; the forced read-only import goes on" : "");
#ifdef _KERNEL
			if (!forced &&
			    spa->spa_load_state != SPA_LOAD_TRYIMPORT) {
				return (SET_ERROR(
				    ZFS_ERR_ASYNC_MIRROR_CATCHING_UP));
			}
#endif
		}
	}
	return (0);
}

typedef struct async_mirror_ub_scan {
	uint64_t	aus_txg;	/* txg of the newest valid uberblock */
} async_mirror_ub_scan_t;

static void
async_mirror_ub_scan_done(zio_t *zio)
{
	zio_t *rio = zio->io_private;
	async_mirror_ub_scan_t *scan = rio->io_private;
	uberblock_t *ub = abd_to_buf(zio->io_abd);

	if (zio->io_error == 0 && uberblock_verify(ub) == 0) {
		mutex_enter(&rio->io_lock);
		scan->aus_txg = MAX(scan->aus_txg, ub->ub_txg);
		mutex_exit(&rio->io_lock);
	}
	abd_free(zio->io_abd);
}

static uint64_t
async_mirror_newest_ub_locked(vdev_t *vd)
{
	async_mirror_ub_scan_t scan = { 0 };
	int flags = ZIO_FLAG_CONFIG_WRITER | ZIO_FLAG_CANFAIL |
	    ZIO_FLAG_SPECULATIVE | ZIO_FLAG_TRYHARD;
	int labels = VDEV_TRUSTED_LABELS(vd);
	zio_t *zio = zio_root(vd->vdev_spa, NULL, &scan, flags);

	for (int l = 0; l < VDEV_LABELS; l++) {
		if (!(labels & (1 << l)))
			continue;
		for (int n = 0; n < VDEV_UBERBLOCK_COUNT(vd); n++) {
			zio_nowait(zio_read_phys(zio, vd,
			    vdev_label_offset(vd->vdev_psize, l,
			    VDEV_UBERBLOCK_OFFSET(vd, n)),
			    VDEV_UBERBLOCK_SIZE(vd),
			    abd_alloc_linear(VDEV_UBERBLOCK_SIZE(vd), B_TRUE),
			    ZIO_CHECKSUM_LABEL, async_mirror_ub_scan_done,
			    zio, ZIO_PRIORITY_SYNC_READ, flags, B_TRUE));
		}
	}
	(void) zio_wait(zio);
	return (scan.aus_txg);
}

int
vdev_async_mirror_online_check(spa_t *spa, vdev_t *vd)
{
	vdev_t *tvd = vd->vdev_parent;
	uint64_t newest, label = 0;

	ASSERT3U(spa_config_held(spa, SCL_STATE_ALL, RW_WRITER), ==,
	    SCL_STATE_ALL);

	if (spa_async_mirror_lineage(spa) == 0 || tvd == NULL ||
	    tvd != vd->vdev_top || !vd->vdev_ops->vdev_op_leaf ||
	    !async_mirror_lineage_tvd(tvd) || !vdev_readable(vd))
		return (0);

	newest = async_mirror_newest_ub_locked(vd);
	if (newest != 0)
		label = async_mirror_label_value_locked(vd, newest,
		    ZPOOL_CONFIG_ASYNC_MIRROR_LINEAGE);
	if (async_mirror_lineage_compatible(spa, vd, newest, label))
		return (0);

	vdev_dbgmsg(vd, "async mirror: the copy was written apart from the "
	    "pool (newest txg %llu, labels of lineage %llx, pool lineage "
	    "%llx); it stays offline", (u_longlong_t)newest,
	    (u_longlong_t)label, (u_longlong_t)spa_async_mirror_lineage(spa));
	return (SET_ERROR(ZFS_ERR_ASYNC_MIRROR_DIVERGED));
}

void
async_mirror_lineage_update(spa_t *spa, boolean_t keep)
{
	async_mirror_lineage_phys_t *lin = &spa->spa_async_mirror_lin;
	vdev_t *missing = NULL;

	ASSERT(spa_writeable(spa));
	ASSERT(!spa->spa_sync_on);

	if (lin->aml_lineage == 0)
		return;

	boolean_t clean = async_mirror_lineage_clean(spa, &missing);

	if (missing != NULL) {
		if (async_mirror_lineage_absent(lin, missing->vdev_guid))
			return;
		uint64_t parent = lin->aml_lineage;

		lin->aml_lineage = async_mirror_new_lineage();
		lin->aml_parent = parent;
		lin->aml_fork_txg = spa->spa_uberblock.ub_txg;

		memset(lin->aml_absent, 0, sizeof (lin->aml_absent));
		lin->aml_nabsent = 1;
		lin->aml_absent[0] = missing->vdev_guid;
		lin->aml_seen = 0;
		lin->aml_flags = 0;

		if (spa->spa_load_max_txg != UINT64_MAX) {
			vdev_t *tvd = missing->vdev_parent;

			lin->aml_flags = ASYNC_MIRROR_FORK_REWIND;
			for (uint64_t c = 0; c < tvd->vdev_children; c++) {
				vdev_t *cvd = tvd->vdev_child[c];
				uberblock_t cub, newest;

				if (cvd == missing || !vdev_readable(cvd))
					continue;
				async_mirror_read_leaf_ub(spa, cvd, &cub,
				    &newest, NULL);
				lin->aml_seen = MAX(lin->aml_seen,
				    newest.ub_txg);
			}
			spa_history_log_internal(spa, "async mirror", NULL,
			    "child guid %llu is missing; new lineage %llx "
			    "forks from %llx at txg %llu by a rewind, which "
			    "discards the txgs up to %llu",
			    (u_longlong_t)missing->vdev_guid,
			    (u_longlong_t)lin->aml_lineage,
			    (u_longlong_t)parent,
			    (u_longlong_t)lin->aml_fork_txg,
			    (u_longlong_t)lin->aml_seen);
		} else {
			spa_history_log_internal(spa, "async mirror", NULL,
			    "child guid %llu is missing; new lineage %llx "
			    "forks from %llx at txg %llu",
			    (u_longlong_t)missing->vdev_guid,
			    (u_longlong_t)lin->aml_lineage,
			    (u_longlong_t)parent,
			    (u_longlong_t)lin->aml_fork_txg);
		}
	} else if (clean) {
		if (lin->aml_parent != 0) {
			spa_history_log_internal(spa, "async mirror", NULL,
			    "both children present; the fork of lineage %llx "
			    "from %llx is resolved",
			    (u_longlong_t)lin->aml_lineage,
			    (u_longlong_t)lin->aml_parent);
		}
		lin->aml_parent = 0;
		lin->aml_fork_txg = 0;
		lin->aml_nabsent = 0;
		memset(lin->aml_absent, 0, sizeof (lin->aml_absent));
		lin->aml_seen = 0;
		lin->aml_flags = 0;
		if (!keep)
			memset(lin, 0, sizeof (*lin));
	}
}

int
async_mirror_erase_skipped(spa_t *spa)
{
	const async_mirror_lineage_phys_t *lin = &spa->spa_async_mirror_lin;
	uint64_t guid = spa->spa_async_mirror_skip;

	ASSERT(!spa->spa_sync_on);

	if (guid == 0)
		return (0);

	vdev_t *vd = vdev_lookup_by_guid(spa->spa_root_vdev, guid);
	uint64_t max_txg = (lin->aml_parent != 0 &&
	    async_mirror_lineage_absent(lin, guid)) ? lin->aml_fork_txg : 0;
	int error;

	spa->spa_async_mirror_skip = 0;
	if (vd == NULL || !vd->vdev_ops->vdev_op_leaf || !vdev_writeable(vd))
		error = SET_ERROR(ENXIO);
	else
		error = vdev_uberblock_erase(vd, max_txg);
	if (error != 0) {
		spa_load_note(spa, "async mirror: cannot erase the uberblocks "
		    "of child guid %llu, error %d", (u_longlong_t)guid, error);
		spa->spa_async_mirror_skip = guid;
		return (error);
	}
	spa->spa_async_mirror_import.ami_erased = guid;
	return (0);
}

void
async_mirror_mark_erased(spa_t *spa)
{
	vdev_t *rvd = spa->spa_root_vdev;
	const async_mirror_import_t *ami = &spa->spa_async_mirror_import;
	uint64_t import_txg = spa->spa_uberblock.ub_txg;

	if (spa_async_mirror_lineage(spa) == 0)
		return;

	for (uint64_t c = 0; c < rvd->vdev_children; c++) {
		vdev_t *tvd = rvd->vdev_child[c];

		if (!async_mirror_lineage_tvd(tvd))
			continue;
		for (uint64_t i = 0; i < 2; i++) {
			vdev_t *cvd = tvd->vdev_child[i];
			uberblock_t ub, newest;

			if (!async_mirror_child_usable(cvd) ||
			    !vdev_readable(tvd->vdev_child[1 - i]))
				continue;
			async_mirror_read_leaf_ub(spa, cvd, &ub, &newest, NULL);
			if (newest.ub_txg != 0)
				continue;
			if (ami->ami_erased == cvd->vdev_guid) {
				spa_history_log_internal(spa, "async mirror",
				    NULL, "the operator kept the copy of child "
				    "guid %llu at txg %llu; the copy of child "
				    "guid %llu is discarded and resilvered in "
				    "full",
				    (u_longlong_t)ami->ami_keep,
				    (u_longlong_t)import_txg,
				    (u_longlong_t)cvd->vdev_guid);
			}
			spa_load_note(spa, "async mirror: child guid %llu has "
			    "no uberblock; it is resilvered in full",
			    (u_longlong_t)cvd->vdev_guid);
			async_mirror_mark_dtl(spa, cvd, TXG_INITIAL,
			    import_txg - TXG_INITIAL + 1);
			async_mirror_note_resync(spa, cvd);
		}
	}
}
