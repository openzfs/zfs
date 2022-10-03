/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2013, 2018 by Delphix. All rights reserved.
 * Copyright 2017 Nexenta Systems, Inc.
 * Copyright 2019, 2020 by Christian Schwarz. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_synctask.h>
#include <sys/dsl_destroy.h>
#include <sys/dmu_impl.h>
#include <sys/dmu_tx.h>
#include <sys/arc.h>
#include <sys/zap.h>
#include <sys/zfeature.h>
#include <sys/spa.h>
#include <sys/dsl_bookmark.h>
#include <zfs_namecheck.h>
#include <sys/dmu_send.h>

static int
dsl_bookmark_hold_ds(dsl_pool_t *dp, const char *fullname,
    dsl_dataset_t **dsp, const void *tag, char **shortnamep)
{
	char buf[ZFS_MAX_DATASET_NAME_LEN];
	char *hashp;

	if (strlen(fullname) >= ZFS_MAX_DATASET_NAME_LEN)
		return (SET_ERROR(ENAMETOOLONG));
	hashp = strchr(fullname, '#');
	if (hashp == NULL)
		return (SET_ERROR(EINVAL));

	*shortnamep = hashp + 1;
	if (zfs_component_namecheck(*shortnamep, NULL, NULL))
		return (SET_ERROR(EINVAL));
	(void) strlcpy(buf, fullname, hashp - fullname + 1);
	return (dsl_dataset_hold(dp, buf, tag, dsp));
}

/*
 * When reading BOOKMARK_V1 bookmarks, the BOOKMARK_V2 fields are guaranteed
 * to be zeroed.
 *
 * Returns ESRCH if bookmark is not found.
 * Note, we need to use the ZAP rather than the AVL to look up bookmarks
 * by name, because only the ZAP honors the casesensitivity setting.
 */
int
dsl_bookmark_lookup_impl(dsl_dataset_t *ds, const char *shortname,
    zfs_bookmark_phys_t *bmark_phys)
{
	objset_t *mos = ds->ds_dir->dd_pool->dp_meta_objset;
	uint64_t bmark_zapobj = ds->ds_bookmarks_obj;
	matchtype_t mt = 0;
	int err;

	if (bmark_zapobj == 0)
		return (SET_ERROR(ESRCH));

	if (dsl_dataset_phys(ds)->ds_flags & DS_FLAG_CI_DATASET)
		mt = MT_NORMALIZE;

	/*
	 * Zero out the bookmark in case the one stored on disk
	 * is in an older, shorter format.
	 */
	memset(bmark_phys, 0, sizeof (*bmark_phys));

	err = zap_lookup_norm(mos, bmark_zapobj, shortname, sizeof (uint64_t),
	    sizeof (*bmark_phys) / sizeof (uint64_t), bmark_phys, mt, NULL, 0,
	    NULL);

	return (err == ENOENT ? SET_ERROR(ESRCH) : err);
}

/*
 * If later_ds is non-NULL, this will return EXDEV if the specified bookmark
 * does not represents an earlier point in later_ds's timeline.  However,
 * bmp will still be filled in if we return EXDEV.
 *
 * Returns ENOENT if the dataset containing the bookmark does not exist.
 * Returns ESRCH if the dataset exists but the bookmark was not found in it.
 */
int
dsl_bookmark_lookup(dsl_pool_t *dp, const char *fullname,
    dsl_dataset_t *later_ds, zfs_bookmark_phys_t *bmp)
{
	char *shortname;
	dsl_dataset_t *ds;
	int error;

	error = dsl_bookmark_hold_ds(dp, fullname, &ds, FTAG, &shortname);
	if (error != 0)
		return (error);

	error = dsl_bookmark_lookup_impl(ds, shortname, bmp);
	if (error == 0 && later_ds != NULL) {
		if (!dsl_dataset_is_before(later_ds, ds, bmp->zbm_creation_txg))
			error = SET_ERROR(EXDEV);
	}
	dsl_dataset_rele(ds, FTAG);
	return (error);
}

/*
 * Validates that
 * - bmark is a full dataset path of a bookmark (bookmark_namecheck)
 * - source is a full path of a snapshot or bookmark
 *   ({bookmark,snapshot}_namecheck)
 *
 * Returns 0 if valid, -1 otherwise.
 */
static int
dsl_bookmark_create_nvl_validate_pair(const char *bmark, const char *source)
{
	if (bookmark_namecheck(bmark, NULL, NULL) != 0)
		return (-1);

	int is_bmark, is_snap;
	is_bmark = bookmark_namecheck(source, NULL, NULL) == 0;
	is_snap = snapshot_namecheck(source, NULL, NULL) == 0;
	if (!is_bmark && !is_snap)
		return (-1);

	return (0);
}

/*
 * Check that the given nvlist corresponds to the following schema:
 *  { newbookmark -> source, ... }
 * where
 * - each pair passes dsl_bookmark_create_nvl_validate_pair
 * - all newbookmarks are in the same pool
 * - all newbookmarks have unique names
 *
 * Note that this function is only validates above schema. Callers must ensure
 * that the bookmarks can be created, e.g. that sources exist.
 *
 * Returns 0 if the nvlist adheres to above schema.
 * Returns -1 if it doesn't.
 */
int
dsl_bookmark_create_nvl_validate(nvlist_t *bmarks)
{
	char *first = NULL;
	size_t first_len = 0;

	for (nvpair_t *pair = nvlist_next_nvpair(bmarks, NULL);
	    pair != NULL; pair = nvlist_next_nvpair(bmarks, pair)) {

		char *bmark = nvpair_name(pair);
		char *source;

		/* list structure: values must be snapshots XOR bookmarks */
		if (nvpair_value_string(pair, &source) != 0)
			return (-1);
		if (dsl_bookmark_create_nvl_validate_pair(bmark, source) != 0)
			return (-1);

		/* same pool check */
		if (first == NULL) {
			char *cp = strpbrk(bmark, "/#");
			if (cp == NULL)
				return (-1);
			first = bmark;
			first_len = cp - bmark;
		}
		if (strncmp(first, bmark, first_len) != 0)
			return (-1);
		switch (*(bmark + first_len)) {
			case '/': /* fallthrough */
			case '#':
				break;
			default:
				return (-1);
		}

		/* unique newbookmark names; todo: O(n^2) */
		for (nvpair_t *pair2 = nvlist_next_nvpair(bmarks, pair);
		    pair2 != NULL; pair2 = nvlist_next_nvpair(bmarks, pair2)) {
			if (strcmp(nvpair_name(pair), nvpair_name(pair2)) == 0)
				return (-1);
		}

	}
	return (0);
}

/*
 * expects that newbm and source have been validated using
 * dsl_bookmark_create_nvl_validate_pair
 */
static int
dsl_bookmark_create_check_impl(dsl_pool_t *dp,
    const char *newbm, const char *source)
{
	ASSERT0(dsl_bookmark_create_nvl_validate_pair(newbm, source));
	/* defer source namecheck until we know it's a snapshot or bookmark */

	int error;
	dsl_dataset_t *newbm_ds;
	char *newbm_short;
	zfs_bookmark_phys_t bmark_phys;

	error = dsl_bookmark_hold_ds(dp, newbm, &newbm_ds, FTAG, &newbm_short);
	if (error != 0)
		return (error);

	/* Verify that the new bookmark does not already exist */
	error = dsl_bookmark_lookup_impl(newbm_ds, newbm_short, &bmark_phys);
	switch (error) {
	case ESRCH:
		/* happy path: new bmark doesn't exist, proceed after switch */
		break;
	case 0:
		error = SET_ERROR(EEXIST);
		goto eholdnewbmds;
	default:
		/* dsl_bookmark_lookup_impl already did SET_ERROR */
		goto eholdnewbmds;
	}

	/* error is retval of the following if-cascade */
	if (strchr(source, '@') != NULL) {
		dsl_dataset_t *source_snap_ds;
		ASSERT3S(snapshot_namecheck(source, NULL, NULL), ==, 0);
		error = dsl_dataset_hold(dp, source, FTAG, &source_snap_ds);
		if (error == 0) {
			VERIFY(source_snap_ds->ds_is_snapshot);
			/*
			 * Verify that source snapshot is an earlier point in
			 * newbm_ds's timeline (source may be newbm_ds's origin)
			 */
			if (!dsl_dataset_is_before(newbm_ds, source_snap_ds, 0))
				error = SET_ERROR(
				    ZFS_ERR_BOOKMARK_SOURCE_NOT_ANCESTOR);
			dsl_dataset_rele(source_snap_ds, FTAG);
		}
	} else if (strchr(source, '#') != NULL) {
		zfs_bookmark_phys_t source_phys;
		ASSERT3S(bookmark_namecheck(source, NULL, NULL), ==, 0);
		/*
		 * Source must exists and be an earlier point in newbm_ds's
		 * timeline (newbm_ds's origin may be a snap of source's ds)
		 */
		error = dsl_bookmark_lookup(dp, source, newbm_ds, &source_phys);
		switch (error) {
		case 0:
			break; /* happy path */
		case EXDEV:
			error = SET_ERROR(ZFS_ERR_BOOKMARK_SOURCE_NOT_ANCESTOR);
			break;
		default:
			/* dsl_bookmark_lookup already did SET_ERROR */
			break;
		}
	} else {
		/*
		 * dsl_bookmark_create_nvl_validate validates that source is
		 * either snapshot or bookmark
		 */
		panic("unreachable code: %s", source);
	}

eholdnewbmds:
	dsl_dataset_rele(newbm_ds, FTAG);
	return (error);
}

int
dsl_bookmark_create_check(void *arg, dmu_tx_t *tx)
{
	dsl_bookmark_create_arg_t *dbca = arg;
	int rv = 0;
	int schema_err = 0;
	ASSERT3P(dbca, !=, NULL);
	ASSERT3P(dbca->dbca_bmarks, !=, NULL);
	/* dbca->dbca_errors is allowed to be NULL */

	dsl_pool_t *dp = dmu_tx_pool(tx);

	if (!spa_feature_is_enabled(dp->dp_spa, SPA_FEATURE_BOOKMARKS))
		return (SET_ERROR(ENOTSUP));

	if (dsl_bookmark_create_nvl_validate(dbca->dbca_bmarks) != 0)
		rv = schema_err = SET_ERROR(EINVAL);

	for (nvpair_t *pair = nvlist_next_nvpair(dbca->dbca_bmarks, NULL);
	    pair != NULL; pair = nvlist_next_nvpair(dbca->dbca_bmarks, pair)) {
		char *new = nvpair_name(pair);

		int error = schema_err;
		if (error == 0) {
			char *source = fnvpair_value_string(pair);
			error = dsl_bookmark_create_check_impl(dp, new, source);
			if (error != 0)
				error = SET_ERROR(error);
		}

		if (error != 0) {
			rv = error;
			if (dbca->dbca_errors != NULL)
				fnvlist_add_int32(dbca->dbca_errors,
				    new, error);
		}
	}

	return (rv);
}

static dsl_bookmark_node_t *
dsl_bookmark_node_alloc(char *shortname)
{
	dsl_bookmark_node_t *dbn = kmem_alloc(sizeof (*dbn), KM_SLEEP);
	dbn->dbn_name = spa_strdup(shortname);
	dbn->dbn_dirty = B_FALSE;
	mutex_init(&dbn->dbn_lock, NULL, MUTEX_DEFAULT, NULL);
	return (dbn);
}

/*
 * Set the fields in the zfs_bookmark_phys_t based on the specified snapshot.
 */
static void
dsl_bookmark_set_phys(zfs_bookmark_phys_t *zbm, dsl_dataset_t *snap)
{
	spa_t *spa = dsl_dataset_get_spa(snap);
	objset_t *mos = spa_get_dsl(spa)->dp_meta_objset;
	dsl_dataset_phys_t *dsp = dsl_dataset_phys(snap);

	memset(zbm, 0, sizeof (zfs_bookmark_phys_t));
	zbm->zbm_guid = dsp->ds_guid;
	zbm->zbm_creation_txg = dsp->ds_creation_txg;
	zbm->zbm_creation_time = dsp->ds_creation_time;
	zbm->zbm_redaction_obj = 0;

	/*
	 * If the dataset is encrypted create a larger bookmark to
	 * accommodate the IVset guid. The IVset guid was added
	 * after the encryption feature to prevent a problem with
	 * raw sends. If we encounter an encrypted dataset without
	 * an IVset guid we fall back to a normal bookmark.
	 */
	if (snap->ds_dir->dd_crypto_obj != 0 &&
	    spa_feature_is_enabled(spa, SPA_FEATURE_BOOKMARK_V2)) {
		(void) zap_lookup(mos, snap->ds_object,
		    DS_FIELD_IVSET_GUID, sizeof (uint64_t), 1,
		    &zbm->zbm_ivset_guid);
	}

	if (spa_feature_is_enabled(spa, SPA_FEATURE_BOOKMARK_WRITTEN)) {
		zbm->zbm_flags = ZBM_FLAG_SNAPSHOT_EXISTS | ZBM_FLAG_HAS_FBN;
		zbm->zbm_referenced_bytes_refd = dsp->ds_referenced_bytes;
		zbm->zbm_compressed_bytes_refd = dsp->ds_compressed_bytes;
		zbm->zbm_uncompressed_bytes_refd = dsp->ds_uncompressed_bytes;

		dsl_dataset_t *nextds;
		VERIFY0(dsl_dataset_hold_obj(snap->ds_dir->dd_pool,
		    dsp->ds_next_snap_obj, FTAG, &nextds));
		dsl_deadlist_space(&nextds->ds_deadlist,
		    &zbm->zbm_referenced_freed_before_next_snap,
		    &zbm->zbm_compressed_freed_before_next_snap,
		    &zbm->zbm_uncompressed_freed_before_next_snap);
		dsl_dataset_rele(nextds, FTAG);
	}
}

/*
 * Add dsl_bookmark_node_t `dbn` to the given dataset and increment appropriate
 * SPA feature counters.
 */
void
dsl_bookmark_node_add(dsl_dataset_t *hds, dsl_bookmark_node_t *dbn,
    dmu_tx_t *tx)
{
	dsl_pool_t *dp = dmu_tx_pool(tx);
	objset_t *mos = dp->dp_meta_objset;

	if (hds->ds_bookmarks_obj == 0) {
		hds->ds_bookmarks_obj = zap_create_norm(mos,
		    U8_TEXTPREP_TOUPPER, DMU_OTN_ZAP_METADATA, DMU_OT_NONE, 0,
		    tx);
		spa_feature_incr(dp->dp_spa, SPA_FEATURE_BOOKMARKS, tx);

		dsl_dataset_zapify(hds, tx);
		VERIFY0(zap_add(mos, hds->ds_object,
		    DS_FIELD_BOOKMARK_NAMES,
		    sizeof (hds->ds_bookmarks_obj), 1,
		    &hds->ds_bookmarks_obj, tx));
	}

	avl_add(&hds->ds_bookmarks, dbn);

	/*
	 * To maintain backwards compatibility with software that doesn't
	 * understand SPA_FEATURE_BOOKMARK_V2, we need to use the smallest
	 * possible bookmark size.
	 */
	uint64_t bookmark_phys_size = BOOKMARK_PHYS_SIZE_V1;
	if (spa_feature_is_enabled(dp->dp_spa, SPA_FEATURE_BOOKMARK_V2) &&
	    (dbn->dbn_phys.zbm_ivset_guid != 0 || dbn->dbn_phys.zbm_flags &
	    ZBM_FLAG_HAS_FBN || dbn->dbn_phys.zbm_redaction_obj != 0)) {
		bookmark_phys_size = BOOKMARK_PHYS_SIZE_V2;
		spa_feature_incr(dp->dp_spa, SPA_FEATURE_BOOKMARK_V2, tx);
	}

	zfs_bookmark_phys_t zero_phys = { 0 };
	ASSERT0(memcmp(((char *)&dbn->dbn_phys) + bookmark_phys_size,
	    &zero_phys, sizeof (zfs_bookmark_phys_t) - bookmark_phys_size));

	VERIFY0(zap_add(mos, hds->ds_bookmarks_obj, dbn->dbn_name,
	    sizeof (uint64_t), bookmark_phys_size / sizeof (uint64_t),
	    &dbn->dbn_phys, tx));
}

/*
 * If redaction_list is non-null, we create a redacted bookmark and redaction
 * list, and store the object number of the redaction list in redact_obj.
 */
static void
dsl_bookmark_create_sync_impl_snap(const char *bookmark, const char *snapshot,
    dmu_tx_t *tx, uint64_t num_redact_snaps, uint64_t *redact_snaps,
    const void *tag, redaction_list_t **redaction_list)
{
	dsl_pool_t *dp = dmu_tx_pool(tx);
	objset_t *mos = dp->dp_meta_objset;
	dsl_dataset_t *snapds, *bmark_fs;
	char *shortname;
	boolean_t bookmark_redacted;
	uint64_t *dsredactsnaps;
	uint64_t dsnumsnaps;

	VERIFY0(dsl_dataset_hold(dp, snapshot, FTAG, &snapds));
	VERIFY0(dsl_bookmark_hold_ds(dp, bookmark, &bmark_fs, FTAG,
	    &shortname));

	dsl_bookmark_node_t *dbn = dsl_bookmark_node_alloc(shortname);
	dsl_bookmark_set_phys(&dbn->dbn_phys, snapds);

	bookmark_redacted = dsl_dataset_get_uint64_array_feature(snapds,
	    SPA_FEATURE_REDACTED_DATASETS, &dsnumsnaps, &dsredactsnaps);
	if (redaction_list != NULL || bookmark_redacted) {
		redaction_list_t *local_rl;
		if (bookmark_redacted) {
			redact_snaps = dsredactsnaps;
			num_redact_snaps = dsnumsnaps;
		}
		dbn->dbn_phys.zbm_redaction_obj = dmu_object_alloc(mos,
		    DMU_OTN_UINT64_METADATA, SPA_OLD_MAXBLOCKSIZE,
		    DMU_OTN_UINT64_METADATA, sizeof (redaction_list_phys_t) +
		    num_redact_snaps * sizeof (uint64_t), tx);
		spa_feature_incr(dp->dp_spa,
		    SPA_FEATURE_REDACTION_BOOKMARKS, tx);

		VERIFY0(dsl_redaction_list_hold_obj(dp,
		    dbn->dbn_phys.zbm_redaction_obj, tag, &local_rl));
		dsl_redaction_list_long_hold(dp, local_rl, tag);

		ASSERT3U((local_rl)->rl_dbuf->db_size, >=,
		    sizeof (redaction_list_phys_t) + num_redact_snaps *
		    sizeof (uint64_t));
		dmu_buf_will_dirty(local_rl->rl_dbuf, tx);
		memcpy(local_rl->rl_phys->rlp_snaps, redact_snaps,
		    sizeof (uint64_t) * num_redact_snaps);
		local_rl->rl_phys->rlp_num_snaps = num_redact_snaps;
		if (bookmark_redacted) {
			ASSERT3P(redaction_list, ==, NULL);
			local_rl->rl_phys->rlp_last_blkid = UINT64_MAX;
			local_rl->rl_phys->rlp_last_object = UINT64_MAX;
			dsl_redaction_list_long_rele(local_rl, tag);
			dsl_redaction_list_rele(local_rl, tag);
		} else {
			*redaction_list = local_rl;
		}
	}

	if (dbn->dbn_phys.zbm_flags & ZBM_FLAG_HAS_FBN) {
		spa_feature_incr(dp->dp_spa,
		    SPA_FEATURE_BOOKMARK_WRITTEN, tx);
	}

	dsl_bookmark_node_add(bmark_fs, dbn, tx);

	spa_history_log_internal_ds(bmark_fs, "bookmark", tx,
	    "name=%s creation_txg=%llu target_snap=%llu redact_obj=%llu",
	    shortname, (longlong_t)dbn->dbn_phys.zbm_creation_txg,
	    (longlong_t)snapds->ds_object,
	    (longlong_t)dbn->dbn_phys.zbm_redaction_obj);

	dsl_dataset_rele(bmark_fs, FTAG);
	dsl_dataset_rele(snapds, FTAG);
}


static void
dsl_bookmark_create_sync_impl_book(
    const char *new_name, const char *source_name, dmu_tx_t *tx)
{
	dsl_pool_t *dp = dmu_tx_pool(tx);
	dsl_dataset_t *bmark_fs_source, *bmark_fs_new;
	char *source_shortname, *new_shortname;
	zfs_bookmark_phys_t source_phys;

	VERIFY0(dsl_bookmark_hold_ds(dp, source_name, &bmark_fs_source, FTAG,
	    &source_shortname));
	VERIFY0(dsl_bookmark_hold_ds(dp, new_name, &bmark_fs_new, FTAG,
	    &new_shortname));

	/*
	 * create a copy of the source bookmark by copying most of its members
	 *
	 * Caveat: bookmarking a redaction bookmark yields a normal bookmark
	 * -----------------------------------------------------------------
	 * Reasoning:
	 * - The zbm_redaction_obj would be referred to by both source and new
	 *   bookmark, but would be destroyed once either source or new is
	 *   destroyed, resulting in use-after-free of the referred object.
	 * - User expectation when issuing the `zfs bookmark` command is that
	 *   a normal bookmark of the source is created
	 *
	 * Design Alternatives For Full Redaction Bookmark Copying:
	 * - reference-count the redaction object => would require on-disk
	 *   format change for existing redaction objects
	 * - Copy the redaction object => cannot be done in syncing context
	 *   because the redaction object might be too large
	 */

	VERIFY0(dsl_bookmark_lookup_impl(bmark_fs_source, source_shortname,
	    &source_phys));
	dsl_bookmark_node_t *new_dbn = dsl_bookmark_node_alloc(new_shortname);

	memcpy(&new_dbn->dbn_phys, &source_phys, sizeof (source_phys));
	new_dbn->dbn_phys.zbm_redaction_obj = 0;

	/* update feature counters */
	if (new_dbn->dbn_phys.zbm_flags & ZBM_FLAG_HAS_FBN) {
		spa_feature_incr(dp->dp_spa,
		    SPA_FEATURE_BOOKMARK_WRITTEN, tx);
	}
	/* no need for redaction bookmark counter; nulled zbm_redaction_obj */
	/* dsl_bookmark_node_add bumps bookmarks and v2-bookmarks counter */

	/*
	 * write new bookmark
	 *
	 * Note that dsl_bookmark_lookup_impl guarantees that, if source is a
	 * v1 bookmark, the v2-only fields are zeroed.
	 * And dsl_bookmark_node_add writes back a v1-sized bookmark if
	 * v2 bookmarks are disabled and/or v2-only fields are zeroed.
	 * => bookmark copying works on pre-bookmark-v2 pools
	 */
	dsl_bookmark_node_add(bmark_fs_new, new_dbn, tx);

	spa_history_log_internal_ds(bmark_fs_source, "bookmark", tx,
	    "name=%s creation_txg=%llu source_guid=%llu",
	    new_shortname, (longlong_t)new_dbn->dbn_phys.zbm_creation_txg,
	    (longlong_t)source_phys.zbm_guid);

	dsl_dataset_rele(bmark_fs_source, FTAG);
	dsl_dataset_rele(bmark_fs_new, FTAG);
}

void
dsl_bookmark_create_sync(void *arg, dmu_tx_t *tx)
{
	dsl_bookmark_create_arg_t *dbca = arg;

	ASSERT(spa_feature_is_enabled(dmu_tx_pool(tx)->dp_spa,
	    SPA_FEATURE_BOOKMARKS));

	for (nvpair_t *pair = nvlist_next_nvpair(dbca->dbca_bmarks, NULL);
	    pair != NULL; pair = nvlist_next_nvpair(dbca->dbca_bmarks, pair)) {

		char *new = nvpair_name(pair);
		char *source = fnvpair_value_string(pair);

		if (strchr(source, '@') != NULL) {
			dsl_bookmark_create_sync_impl_snap(new, source, tx,
			    0, NULL, NULL, NULL);
		} else if (strchr(source, '#') != NULL) {
			dsl_bookmark_create_sync_impl_book(new, source, tx);
		} else {
			panic("unreachable code");
		}

	}
}

/*
 * The bookmarks must all be in the same pool.
 */
int
dsl_bookmark_create(nvlist_t *bmarks, nvlist_t *errors)
{
	nvpair_t *pair;
	dsl_bookmark_create_arg_t dbca;

	pair = nvlist_next_nvpair(bmarks, NULL);
	if (pair == NULL)
		return (0);

	dbca.dbca_bmarks = bmarks;
	dbca.dbca_errors = errors;

	return (dsl_sync_task(nvpair_name(pair), dsl_bookmark_create_check,
	    dsl_bookmark_create_sync, &dbca,
	    fnvlist_num_pairs(bmarks), ZFS_SPACE_CHECK_NORMAL));
}

static int
dsl_bookmark_create_redacted_check(void *arg, dmu_tx_t *tx)
{
	dsl_bookmark_create_redacted_arg_t *dbcra = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	int rv = 0;

	if (!spa_feature_is_enabled(dp->dp_spa,
	    SPA_FEATURE_REDACTION_BOOKMARKS))
		return (SET_ERROR(ENOTSUP));
	/*
	 * If the list of redact snaps will not fit in the bonus buffer with
	 * the furthest reached object and offset, fail.
	 */
	if (dbcra->dbcra_numsnaps > (dmu_bonus_max() -
	    sizeof (redaction_list_phys_t)) / sizeof (uint64_t))
		return (SET_ERROR(E2BIG));

	if (dsl_bookmark_create_nvl_validate_pair(
	    dbcra->dbcra_bmark, dbcra->dbcra_snap) != 0)
		return (SET_ERROR(EINVAL));

	rv = dsl_bookmark_create_check_impl(dp,
	    dbcra->dbcra_bmark, dbcra->dbcra_snap);
	return (rv);
}

static void
dsl_bookmark_create_redacted_sync(void *arg, dmu_tx_t *tx)
{
	dsl_bookmark_create_redacted_arg_t *dbcra = arg;
	dsl_bookmark_create_sync_impl_snap(dbcra->dbcra_bmark,
	    dbcra->dbcra_snap, tx, dbcra->dbcra_numsnaps, dbcra->dbcra_snaps,
	    dbcra->dbcra_tag, dbcra->dbcra_rl);
}

int
dsl_bookmark_create_redacted(const char *bookmark, const char *snapshot,
    uint64_t numsnaps, uint64_t *snapguids, const void *tag,
    redaction_list_t **rl)
{
	dsl_bookmark_create_redacted_arg_t dbcra;

	dbcra.dbcra_bmark = bookmark;
	dbcra.dbcra_snap = snapshot;
	dbcra.dbcra_rl = rl;
	dbcra.dbcra_numsnaps = numsnaps;
	dbcra.dbcra_snaps = snapguids;
	dbcra.dbcra_tag = tag;

	return (dsl_sync_task(bookmark, dsl_bookmark_create_redacted_check,
	    dsl_bookmark_create_redacted_sync, &dbcra, 5,
	    ZFS_SPACE_CHECK_NORMAL));
}

/*
 * Retrieve the list of properties given in the 'props' nvlist for a bookmark.
 * If 'props' is NULL, retrieves all properties.
 */
static void
dsl_bookmark_fetch_props(dsl_pool_t *dp, zfs_bookmark_phys_t *bmark_phys,
    nvlist_t *props, nvlist_t *out_props)
{
	ASSERT3P(dp, !=, NULL);
	ASSERT3P(bmark_phys, !=, NULL);
	ASSERT3P(out_props, !=, NULL);
	ASSERT(RRW_LOCK_HELD(&dp->dp_config_rwlock));

	if (props == NULL || nvlist_exists(props,
	    zfs_prop_to_name(ZFS_PROP_GUID))) {
		dsl_prop_nvlist_add_uint64(out_props,
		    ZFS_PROP_GUID, bmark_phys->zbm_guid);
	}
	if (props == NULL || nvlist_exists(props,
	    zfs_prop_to_name(ZFS_PROP_CREATETXG))) {
		dsl_prop_nvlist_add_uint64(out_props,
		    ZFS_PROP_CREATETXG, bmark_phys->zbm_creation_txg);
	}
	if (props == NULL || nvlist_exists(props,
	    zfs_prop_to_name(ZFS_PROP_CREATION))) {
		dsl_prop_nvlist_add_uint64(out_props,
		    ZFS_PROP_CREATION, bmark_phys->zbm_creation_time);
	}
	if (props == NULL || nvlist_exists(props,
	    zfs_prop_to_name(ZFS_PROP_IVSET_GUID))) {
		dsl_prop_nvlist_add_uint64(out_props,
		    ZFS_PROP_IVSET_GUID, bmark_phys->zbm_ivset_guid);
	}
	if (bmark_phys->zbm_flags & ZBM_FLAG_HAS_FBN) {
		if (props == NULL || nvlist_exists(props,
		    zfs_prop_to_name(ZFS_PROP_REFERENCED))) {
			dsl_prop_nvlist_add_uint64(out_props,
			    ZFS_PROP_REFERENCED,
			    bmark_phys->zbm_referenced_bytes_refd);
		}
		if (props == NULL || nvlist_exists(props,
		    zfs_prop_to_name(ZFS_PROP_LOGICALREFERENCED))) {
			dsl_prop_nvlist_add_uint64(out_props,
			    ZFS_PROP_LOGICALREFERENCED,
			    bmark_phys->zbm_uncompressed_bytes_refd);
		}
		if (props == NULL || nvlist_exists(props,
		    zfs_prop_to_name(ZFS_PROP_REFRATIO))) {
			uint64_t ratio =
			    bmark_phys->zbm_compressed_bytes_refd == 0 ? 100 :
			    bmark_phys->zbm_uncompressed_bytes_refd * 100 /
			    bmark_phys->zbm_compressed_bytes_refd;
			dsl_prop_nvlist_add_uint64(out_props,
			    ZFS_PROP_REFRATIO, ratio);
		}
	}

	if ((props == NULL || nvlist_exists(props, "redact_snaps") ||
	    nvlist_exists(props, "redact_complete")) &&
	    bmark_phys->zbm_redaction_obj != 0) {
		redaction_list_t *rl;
		int err = dsl_redaction_list_hold_obj(dp,
		    bmark_phys->zbm_redaction_obj, FTAG, &rl);
		if (err == 0) {
			if (nvlist_exists(props, "redact_snaps")) {
				nvlist_t *nvl;
				nvl = fnvlist_alloc();
				fnvlist_add_uint64_array(nvl, ZPROP_VALUE,
				    rl->rl_phys->rlp_snaps,
				    rl->rl_phys->rlp_num_snaps);
				fnvlist_add_nvlist(out_props, "redact_snaps",
				    nvl);
				nvlist_free(nvl);
			}
			if (nvlist_exists(props, "redact_complete")) {
				nvlist_t *nvl;
				nvl = fnvlist_alloc();
				fnvlist_add_boolean_value(nvl, ZPROP_VALUE,
				    rl->rl_phys->rlp_last_blkid == UINT64_MAX &&
				    rl->rl_phys->rlp_last_object == UINT64_MAX);
				fnvlist_add_nvlist(out_props, "redact_complete",
				    nvl);
				nvlist_free(nvl);
			}
			dsl_redaction_list_rele(rl, FTAG);
		}
	}
}

int
dsl_get_bookmarks_impl(dsl_dataset_t *ds, nvlist_t *props, nvlist_t *outnvl)
{
	dsl_pool_t *dp = ds->ds_dir->dd_pool;

	ASSERT(dsl_pool_config_held(dp));

	if (dsl_dataset_is_snapshot(ds))
		return (SET_ERROR(EINVAL));

	for (dsl_bookmark_node_t *dbn = avl_first(&ds->ds_bookmarks);
	    dbn != NULL; dbn = AVL_NEXT(&ds->ds_bookmarks, dbn)) {
		nvlist_t *out_props = fnvlist_alloc();

		dsl_bookmark_fetch_props(dp, &dbn->dbn_phys, props, out_props);

		fnvlist_add_nvlist(outnvl, dbn->dbn_name, out_props);
		fnvlist_free(out_props);
	}
	return (0);
}

/*
 * Comparison func for ds_bookmarks AVL tree.  We sort the bookmarks by
 * their TXG, then by their FBN-ness.  The "FBN-ness" component ensures
 * that all bookmarks at the same TXG that HAS_FBN are adjacent, which
 * dsl_bookmark_destroy_sync_impl() depends on.  Note that there may be
 * multiple bookmarks at the same TXG (with the same FBN-ness).  In this
 * case we differentiate them by an arbitrary metric (in this case,
 * their names).
 */
static int
dsl_bookmark_compare(const void *l, const void *r)
{
	const dsl_bookmark_node_t *ldbn = l;
	const dsl_bookmark_node_t *rdbn = r;

	int64_t cmp = TREE_CMP(ldbn->dbn_phys.zbm_creation_txg,
	    rdbn->dbn_phys.zbm_creation_txg);
	if (likely(cmp))
		return (cmp);
	cmp = TREE_CMP((ldbn->dbn_phys.zbm_flags & ZBM_FLAG_HAS_FBN),
	    (rdbn->dbn_phys.zbm_flags & ZBM_FLAG_HAS_FBN));
	if (likely(cmp))
		return (cmp);
	cmp = strcmp(ldbn->dbn_name, rdbn->dbn_name);
	return (TREE_ISIGN(cmp));
}

/*
 * Cache this (head) dataset's bookmarks in the ds_bookmarks AVL tree.
 */
int
dsl_bookmark_init_ds(dsl_dataset_t *ds)
{
	dsl_pool_t *dp = ds->ds_dir->dd_pool;
	objset_t *mos = dp->dp_meta_objset;

	ASSERT(!ds->ds_is_snapshot);

	avl_create(&ds->ds_bookmarks, dsl_bookmark_compare,
	    sizeof (dsl_bookmark_node_t),
	    offsetof(dsl_bookmark_node_t, dbn_node));

	if (!dsl_dataset_is_zapified(ds))
		return (0);

	int zaperr = zap_lookup(mos, ds->ds_object, DS_FIELD_BOOKMARK_NAMES,
	    sizeof (ds->ds_bookmarks_obj), 1, &ds->ds_bookmarks_obj);
	if (zaperr == ENOENT)
		return (0);
	if (zaperr != 0)
		return (zaperr);

	if (ds->ds_bookmarks_obj == 0)
		return (0);

	int err = 0;
	zap_cursor_t zc;
	zap_attribute_t attr;

	for (zap_cursor_init(&zc, mos, ds->ds_bookmarks_obj);
	    (err = zap_cursor_retrieve(&zc, &attr)) == 0;
	    zap_cursor_advance(&zc)) {
		dsl_bookmark_node_t *dbn =
		    dsl_bookmark_node_alloc(attr.za_name);

		err = dsl_bookmark_lookup_impl(ds,
		    dbn->dbn_name, &dbn->dbn_phys);
		ASSERT3U(err, !=, ENOENT);
		if (err != 0) {
			kmem_free(dbn, sizeof (*dbn));
			break;
		}
		avl_add(&ds->ds_bookmarks, dbn);
	}
	zap_cursor_fini(&zc);
	if (err == ENOENT)
		err = 0;
	return (err);
}

void
dsl_bookmark_fini_ds(dsl_dataset_t *ds)
{
	void *cookie = NULL;
	dsl_bookmark_node_t *dbn;

	if (ds->ds_is_snapshot)
		return;

	while ((dbn = avl_destroy_nodes(&ds->ds_bookmarks, &cookie)) != NULL) {
		spa_strfree(dbn->dbn_name);
		mutex_destroy(&dbn->dbn_lock);
		kmem_free(dbn, sizeof (*dbn));
	}
	avl_destroy(&ds->ds_bookmarks);
}

/*
 * Retrieve the bookmarks that exist in the specified dataset, and the
 * requested properties of each bookmark.
 *
 * The "props" nvlist specifies which properties are requested.
 * See lzc_get_bookmarks() for the list of valid properties.
 */
int
dsl_get_bookmarks(const char *dsname, nvlist_t *props, nvlist_t *outnvl)
{
	dsl_pool_t *dp;
	dsl_dataset_t *ds;
	int err;

	err = dsl_pool_hold(dsname, FTAG, &dp);
	if (err != 0)
		return (err);
	err = dsl_dataset_hold(dp, dsname, FTAG, &ds);
	if (err != 0) {
		dsl_pool_rele(dp, FTAG);
		return (err);
	}

	err = dsl_get_bookmarks_impl(ds, props, outnvl);

	dsl_dataset_rele(ds, FTAG);
	dsl_pool_rele(dp, FTAG);
	return (err);
}

/*
 * Retrieve all properties for a single bookmark in the given dataset.
 */
int
dsl_get_bookmark_props(const char *dsname, const char *bmname, nvlist_t *props)
{
	dsl_pool_t *dp;
	dsl_dataset_t *ds;
	zfs_bookmark_phys_t bmark_phys = { 0 };
	int err;

	err = dsl_pool_hold(dsname, FTAG, &dp);
	if (err != 0)
		return (err);
	err = dsl_dataset_hold(dp, dsname, FTAG, &ds);
	if (err != 0) {
		dsl_pool_rele(dp, FTAG);
		return (err);
	}

	err = dsl_bookmark_lookup_impl(ds, bmname, &bmark_phys);
	if (err != 0)
		goto out;

	dsl_bookmark_fetch_props(dp, &bmark_phys, NULL, props);
out:
	dsl_dataset_rele(ds, FTAG);
	dsl_pool_rele(dp, FTAG);
	return (err);
}

typedef struct dsl_bookmark_destroy_arg {
	nvlist_t *dbda_bmarks;
	nvlist_t *dbda_success;
	nvlist_t *dbda_errors;
} dsl_bookmark_destroy_arg_t;

static void
dsl_bookmark_destroy_sync_impl(dsl_dataset_t *ds, const char *name,
    dmu_tx_t *tx)
{
	objset_t *mos = ds->ds_dir->dd_pool->dp_meta_objset;
	uint64_t bmark_zapobj = ds->ds_bookmarks_obj;
	matchtype_t mt = 0;
	uint64_t int_size, num_ints;
	/*
	 * 'search' must be zeroed so that dbn_flags (which is used in
	 * dsl_bookmark_compare()) will be zeroed even if the on-disk
	 * (in ZAP) bookmark is shorter than offsetof(dbn_flags).
	 */
	dsl_bookmark_node_t search = { 0 };
	char realname[ZFS_MAX_DATASET_NAME_LEN];

	/*
	 * Find the real name of this bookmark, which may be different
	 * from the given name if the dataset is case-insensitive.  Then
	 * use the real name to find the node in the ds_bookmarks AVL tree.
	 */

	if (dsl_dataset_phys(ds)->ds_flags & DS_FLAG_CI_DATASET)
		mt = MT_NORMALIZE;

	VERIFY0(zap_length(mos, bmark_zapobj, name, &int_size, &num_ints));

	ASSERT3U(int_size, ==, sizeof (uint64_t));

	if (num_ints * int_size > BOOKMARK_PHYS_SIZE_V1) {
		spa_feature_decr(dmu_objset_spa(mos),
		    SPA_FEATURE_BOOKMARK_V2, tx);
	}
	VERIFY0(zap_lookup_norm(mos, bmark_zapobj, name, sizeof (uint64_t),
	    num_ints, &search.dbn_phys, mt, realname, sizeof (realname), NULL));

	search.dbn_name = realname;
	dsl_bookmark_node_t *dbn = avl_find(&ds->ds_bookmarks, &search, NULL);
	ASSERT(dbn != NULL);

	if (dbn->dbn_phys.zbm_flags & ZBM_FLAG_HAS_FBN) {
		/*
		 * If this bookmark HAS_FBN, and it is before the most
		 * recent snapshot, then its TXG is a key in the head's
		 * deadlist (and all clones' heads' deadlists).  If this is
		 * the last thing keeping the key (i.e. there are no more
		 * bookmarks with HAS_FBN at this TXG, and there is no
		 * snapshot at this TXG), then remove the key.
		 *
		 * Note that this algorithm depends on ds_bookmarks being
		 * sorted such that all bookmarks at the same TXG with
		 * HAS_FBN are adjacent (with no non-HAS_FBN bookmarks
		 * at the same TXG in between them).  If this were not
		 * the case, we would need to examine *all* bookmarks
		 * at this TXG, rather than just the adjacent ones.
		 */

		dsl_bookmark_node_t *dbn_prev =
		    AVL_PREV(&ds->ds_bookmarks, dbn);
		dsl_bookmark_node_t *dbn_next =
		    AVL_NEXT(&ds->ds_bookmarks, dbn);

		boolean_t more_bookmarks_at_this_txg =
		    (dbn_prev != NULL && dbn_prev->dbn_phys.zbm_creation_txg ==
		    dbn->dbn_phys.zbm_creation_txg &&
		    (dbn_prev->dbn_phys.zbm_flags & ZBM_FLAG_HAS_FBN)) ||
		    (dbn_next != NULL && dbn_next->dbn_phys.zbm_creation_txg ==
		    dbn->dbn_phys.zbm_creation_txg &&
		    (dbn_next->dbn_phys.zbm_flags & ZBM_FLAG_HAS_FBN));

		if (!(dbn->dbn_phys.zbm_flags & ZBM_FLAG_SNAPSHOT_EXISTS) &&
		    !more_bookmarks_at_this_txg &&
		    dbn->dbn_phys.zbm_creation_txg <
		    dsl_dataset_phys(ds)->ds_prev_snap_txg) {
			dsl_dir_remove_clones_key(ds->ds_dir,
			    dbn->dbn_phys.zbm_creation_txg, tx);
			dsl_deadlist_remove_key(&ds->ds_deadlist,
			    dbn->dbn_phys.zbm_creation_txg, tx);
		}

		spa_feature_decr(dmu_objset_spa(mos),
		    SPA_FEATURE_BOOKMARK_WRITTEN, tx);
	}

	if (dbn->dbn_phys.zbm_redaction_obj != 0) {
		VERIFY0(dmu_object_free(mos,
		    dbn->dbn_phys.zbm_redaction_obj, tx));
		spa_feature_decr(dmu_objset_spa(mos),
		    SPA_FEATURE_REDACTION_BOOKMARKS, tx);
	}

	avl_remove(&ds->ds_bookmarks, dbn);
	spa_strfree(dbn->dbn_name);
	mutex_destroy(&dbn->dbn_lock);
	kmem_free(dbn, sizeof (*dbn));

	VERIFY0(zap_remove_norm(mos, bmark_zapobj, name, mt, tx));
}

static int
dsl_bookmark_destroy_check(void *arg, dmu_tx_t *tx)
{
	dsl_bookmark_destroy_arg_t *dbda = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	int rv = 0;

	ASSERT(nvlist_empty(dbda->dbda_success));
	ASSERT(nvlist_empty(dbda->dbda_errors));

	if (!spa_feature_is_enabled(dp->dp_spa, SPA_FEATURE_BOOKMARKS))
		return (0);

	for (nvpair_t *pair = nvlist_next_nvpair(dbda->dbda_bmarks, NULL);
	    pair != NULL; pair = nvlist_next_nvpair(dbda->dbda_bmarks, pair)) {
		const char *fullname = nvpair_name(pair);
		dsl_dataset_t *ds;
		zfs_bookmark_phys_t bm;
		int error;
		char *shortname;

		error = dsl_bookmark_hold_ds(dp, fullname, &ds,
		    FTAG, &shortname);
		if (error == ENOENT) {
			/* ignore it; the bookmark is "already destroyed" */
			continue;
		}
		if (error == 0) {
			error = dsl_bookmark_lookup_impl(ds, shortname, &bm);
			dsl_dataset_rele(ds, FTAG);
			if (error == ESRCH) {
				/*
				 * ignore it; the bookmark is
				 * "already destroyed"
				 */
				continue;
			}
			if (error == 0 && bm.zbm_redaction_obj != 0) {
				redaction_list_t *rl = NULL;
				error = dsl_redaction_list_hold_obj(tx->tx_pool,
				    bm.zbm_redaction_obj, FTAG, &rl);
				if (error == ENOENT) {
					error = 0;
				} else if (error == 0 &&
				    dsl_redaction_list_long_held(rl)) {
					error = SET_ERROR(EBUSY);
				}
				if (rl != NULL) {
					dsl_redaction_list_rele(rl, FTAG);
				}
			}
		}
		if (error == 0) {
			if (dmu_tx_is_syncing(tx)) {
				fnvlist_add_boolean(dbda->dbda_success,
				    fullname);
			}
		} else {
			fnvlist_add_int32(dbda->dbda_errors, fullname, error);
			rv = error;
		}
	}
	return (rv);
}

static void
dsl_bookmark_destroy_sync(void *arg, dmu_tx_t *tx)
{
	dsl_bookmark_destroy_arg_t *dbda = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	objset_t *mos = dp->dp_meta_objset;

	for (nvpair_t *pair = nvlist_next_nvpair(dbda->dbda_success, NULL);
	    pair != NULL; pair = nvlist_next_nvpair(dbda->dbda_success, pair)) {
		dsl_dataset_t *ds;
		char *shortname;
		uint64_t zap_cnt;

		VERIFY0(dsl_bookmark_hold_ds(dp, nvpair_name(pair),
		    &ds, FTAG, &shortname));
		dsl_bookmark_destroy_sync_impl(ds, shortname, tx);

		/*
		 * If all of this dataset's bookmarks have been destroyed,
		 * free the zap object and decrement the feature's use count.
		 */
		VERIFY0(zap_count(mos, ds->ds_bookmarks_obj, &zap_cnt));
		if (zap_cnt == 0) {
			dmu_buf_will_dirty(ds->ds_dbuf, tx);
			VERIFY0(zap_destroy(mos, ds->ds_bookmarks_obj, tx));
			ds->ds_bookmarks_obj = 0;
			spa_feature_decr(dp->dp_spa, SPA_FEATURE_BOOKMARKS, tx);
			VERIFY0(zap_remove(mos, ds->ds_object,
			    DS_FIELD_BOOKMARK_NAMES, tx));
		}

		spa_history_log_internal_ds(ds, "remove bookmark", tx,
		    "name=%s", shortname);

		dsl_dataset_rele(ds, FTAG);
	}
}

/*
 * The bookmarks must all be in the same pool.
 */
int
dsl_bookmark_destroy(nvlist_t *bmarks, nvlist_t *errors)
{
	int rv;
	dsl_bookmark_destroy_arg_t dbda;
	nvpair_t *pair = nvlist_next_nvpair(bmarks, NULL);
	if (pair == NULL)
		return (0);

	dbda.dbda_bmarks = bmarks;
	dbda.dbda_errors = errors;
	dbda.dbda_success = fnvlist_alloc();

	rv = dsl_sync_task(nvpair_name(pair), dsl_bookmark_destroy_check,
	    dsl_bookmark_destroy_sync, &dbda, fnvlist_num_pairs(bmarks),
	    ZFS_SPACE_CHECK_RESERVED);
	fnvlist_free(dbda.dbda_success);
	return (rv);
}

/* Return B_TRUE if there are any long holds on this dataset. */
boolean_t
dsl_redaction_list_long_held(redaction_list_t *rl)
{
	return (!zfs_refcount_is_zero(&rl->rl_longholds));
}

void
dsl_redaction_list_long_hold(dsl_pool_t *dp, redaction_list_t *rl,
    const void *tag)
{
	ASSERT(dsl_pool_config_held(dp));
	(void) zfs_refcount_add(&rl->rl_longholds, tag);
}

void
dsl_redaction_list_long_rele(redaction_list_t *rl, const void *tag)
{
	(void) zfs_refcount_remove(&rl->rl_longholds, tag);
}

static void
redaction_list_evict_sync(void *rlu)
{
	redaction_list_t *rl = rlu;
	zfs_refcount_destroy(&rl->rl_longholds);

	kmem_free(rl, sizeof (redaction_list_t));
}

void
dsl_redaction_list_rele(redaction_list_t *rl, const void *tag)
{
	dmu_buf_rele(rl->rl_dbuf, tag);
}

int
dsl_redaction_list_hold_obj(dsl_pool_t *dp, uint64_t rlobj, const void *tag,
    redaction_list_t **rlp)
{
	objset_t *mos = dp->dp_meta_objset;
	dmu_buf_t *dbuf;
	redaction_list_t *rl;
	int err;

	ASSERT(dsl_pool_config_held(dp));

	err = dmu_bonus_hold(mos, rlobj, tag, &dbuf);
	if (err != 0)
		return (err);

	rl = dmu_buf_get_user(dbuf);
	if (rl == NULL) {
		redaction_list_t *winner = NULL;

		rl = kmem_zalloc(sizeof (redaction_list_t), KM_SLEEP);
		rl->rl_dbuf = dbuf;
		rl->rl_object = rlobj;
		rl->rl_phys = dbuf->db_data;
		rl->rl_mos = dp->dp_meta_objset;
		zfs_refcount_create(&rl->rl_longholds);
		dmu_buf_init_user(&rl->rl_dbu, redaction_list_evict_sync, NULL,
		    &rl->rl_dbuf);
		if ((winner = dmu_buf_set_user_ie(dbuf, &rl->rl_dbu)) != NULL) {
			kmem_free(rl, sizeof (*rl));
			rl = winner;
		}
	}
	*rlp = rl;
	return (0);
}

/*
 * Snapshot ds is being destroyed.
 *
 * Adjust the "freed_before_next" of any bookmarks between this snap
 * and the previous snapshot, because their "next snapshot" is changing.
 *
 * If there are any bookmarks with HAS_FBN at this snapshot, remove
 * their HAS_SNAP flag (note: there can be at most one snapshot of
 * each filesystem at a given txg), and return B_TRUE.  In this case
 * the caller can not remove the key in the deadlist at this TXG, because
 * the HAS_FBN bookmarks require the key be there.
 *
 * Returns B_FALSE if there are no bookmarks with HAS_FBN at this
 * snapshot's TXG.  In this case the caller can remove the key in the
 * deadlist at this TXG.
 */
boolean_t
dsl_bookmark_ds_destroyed(dsl_dataset_t *ds, dmu_tx_t *tx)
{
	dsl_pool_t *dp = ds->ds_dir->dd_pool;

	dsl_dataset_t *head, *next;
	VERIFY0(dsl_dataset_hold_obj(dp,
	    dsl_dir_phys(ds->ds_dir)->dd_head_dataset_obj, FTAG, &head));
	VERIFY0(dsl_dataset_hold_obj(dp,
	    dsl_dataset_phys(ds)->ds_next_snap_obj, FTAG, &next));

	/*
	 * Find the first bookmark that HAS_FBN at or after the
	 * previous snapshot.
	 */
	dsl_bookmark_node_t search = { 0 };
	avl_index_t idx;
	search.dbn_phys.zbm_creation_txg =
	    dsl_dataset_phys(ds)->ds_prev_snap_txg;
	search.dbn_phys.zbm_flags = ZBM_FLAG_HAS_FBN;
	/*
	 * The empty-string name can't be in the AVL, and it compares
	 * before any entries with this TXG.
	 */
	search.dbn_name = (char *)"";
	VERIFY3P(avl_find(&head->ds_bookmarks, &search, &idx), ==, NULL);
	dsl_bookmark_node_t *dbn =
	    avl_nearest(&head->ds_bookmarks, idx, AVL_AFTER);

	/*
	 * Iterate over all bookmarks that are at or after the previous
	 * snapshot, and before this (being deleted) snapshot.  Adjust
	 * their FBN based on their new next snapshot.
	 */
	for (; dbn != NULL && dbn->dbn_phys.zbm_creation_txg <
	    dsl_dataset_phys(ds)->ds_creation_txg;
	    dbn = AVL_NEXT(&head->ds_bookmarks, dbn)) {
		if (!(dbn->dbn_phys.zbm_flags & ZBM_FLAG_HAS_FBN))
			continue;
		/*
		 * Increase our FBN by the amount of space that was live
		 * (referenced) at the time of this bookmark (i.e.
		 * birth <= zbm_creation_txg), and killed between this
		 * (being deleted) snapshot and the next snapshot (i.e.
		 * on the next snapshot's deadlist).  (Space killed before
		 * this are already on our FBN.)
		 */
		uint64_t referenced, compressed, uncompressed;
		dsl_deadlist_space_range(&next->ds_deadlist,
		    0, dbn->dbn_phys.zbm_creation_txg,
		    &referenced, &compressed, &uncompressed);
		dbn->dbn_phys.zbm_referenced_freed_before_next_snap +=
		    referenced;
		dbn->dbn_phys.zbm_compressed_freed_before_next_snap +=
		    compressed;
		dbn->dbn_phys.zbm_uncompressed_freed_before_next_snap +=
		    uncompressed;
		VERIFY0(zap_update(dp->dp_meta_objset, head->ds_bookmarks_obj,
		    dbn->dbn_name, sizeof (uint64_t),
		    sizeof (zfs_bookmark_phys_t) / sizeof (uint64_t),
		    &dbn->dbn_phys, tx));
	}
	dsl_dataset_rele(next, FTAG);

	/*
	 * There may be several bookmarks at this txg (the TXG of the
	 * snapshot being deleted).  We need to clear the SNAPSHOT_EXISTS
	 * flag on all of them, and return TRUE if there is at least 1
	 * bookmark here with HAS_FBN (thus preventing the deadlist
	 * key from being removed).
	 */
	boolean_t rv = B_FALSE;
	for (; dbn != NULL && dbn->dbn_phys.zbm_creation_txg ==
	    dsl_dataset_phys(ds)->ds_creation_txg;
	    dbn = AVL_NEXT(&head->ds_bookmarks, dbn)) {
		if (!(dbn->dbn_phys.zbm_flags & ZBM_FLAG_HAS_FBN)) {
			ASSERT(!(dbn->dbn_phys.zbm_flags &
			    ZBM_FLAG_SNAPSHOT_EXISTS));
			continue;
		}
		ASSERT(dbn->dbn_phys.zbm_flags & ZBM_FLAG_SNAPSHOT_EXISTS);
		dbn->dbn_phys.zbm_flags &= ~ZBM_FLAG_SNAPSHOT_EXISTS;
		VERIFY0(zap_update(dp->dp_meta_objset, head->ds_bookmarks_obj,
		    dbn->dbn_name, sizeof (uint64_t),
		    sizeof (zfs_bookmark_phys_t) / sizeof (uint64_t),
		    &dbn->dbn_phys, tx));
		rv = B_TRUE;
	}
	dsl_dataset_rele(head, FTAG);
	return (rv);
}

/*
 * A snapshot is being created of this (head) dataset.
 *
 * We don't keep keys in the deadlist for the most recent snapshot, or any
 * bookmarks at or after it, because there can't be any blocks on the
 * deadlist in this range.  Now that the most recent snapshot is after
 * all bookmarks, we need to add these keys.  Note that the caller always
 * adds a key at the previous snapshot, so we only add keys for bookmarks
 * after that.
 */
void
dsl_bookmark_snapshotted(dsl_dataset_t *ds, dmu_tx_t *tx)
{
	uint64_t last_key_added = UINT64_MAX;
	for (dsl_bookmark_node_t *dbn = avl_last(&ds->ds_bookmarks);
	    dbn != NULL && dbn->dbn_phys.zbm_creation_txg >
	    dsl_dataset_phys(ds)->ds_prev_snap_txg;
	    dbn = AVL_PREV(&ds->ds_bookmarks, dbn)) {
		uint64_t creation_txg = dbn->dbn_phys.zbm_creation_txg;
		ASSERT3U(creation_txg, <=, last_key_added);
		/*
		 * Note, there may be multiple bookmarks at this TXG,
		 * and we only want to add the key for this TXG once.
		 * The ds_bookmarks AVL is sorted by TXG, so we will visit
		 * these bookmarks in sequence.
		 */
		if ((dbn->dbn_phys.zbm_flags & ZBM_FLAG_HAS_FBN) &&
		    creation_txg != last_key_added) {
			dsl_deadlist_add_key(&ds->ds_deadlist,
			    creation_txg, tx);
			last_key_added = creation_txg;
		}
	}
}

/*
 * The next snapshot of the origin dataset has changed, due to
 * promote or clone swap.  If there are any bookmarks at this dataset,
 * we need to update their zbm_*_freed_before_next_snap to reflect this.
 * The head dataset has the relevant bookmarks in ds_bookmarks.
 */
void
dsl_bookmark_next_changed(dsl_dataset_t *head, dsl_dataset_t *origin,
    dmu_tx_t *tx)
{
	dsl_pool_t *dp = dmu_tx_pool(tx);

	/*
	 * Find the first bookmark that HAS_FBN at the origin snapshot.
	 */
	dsl_bookmark_node_t search = { 0 };
	avl_index_t idx;
	search.dbn_phys.zbm_creation_txg =
	    dsl_dataset_phys(origin)->ds_creation_txg;
	search.dbn_phys.zbm_flags = ZBM_FLAG_HAS_FBN;
	/*
	 * The empty-string name can't be in the AVL, and it compares
	 * before any entries with this TXG.
	 */
	search.dbn_name = (char *)"";
	VERIFY3P(avl_find(&head->ds_bookmarks, &search, &idx), ==, NULL);
	dsl_bookmark_node_t *dbn =
	    avl_nearest(&head->ds_bookmarks, idx, AVL_AFTER);

	/*
	 * Iterate over all bookmarks that are at the origin txg.
	 * Adjust their FBN based on their new next snapshot.
	 */
	for (; dbn != NULL && dbn->dbn_phys.zbm_creation_txg ==
	    dsl_dataset_phys(origin)->ds_creation_txg &&
	    (dbn->dbn_phys.zbm_flags & ZBM_FLAG_HAS_FBN);
	    dbn = AVL_NEXT(&head->ds_bookmarks, dbn)) {

		/*
		 * Bookmark is at the origin, therefore its
		 * "next dataset" is changing, so we need
		 * to reset its FBN by recomputing it in
		 * dsl_bookmark_set_phys().
		 */
		ASSERT3U(dbn->dbn_phys.zbm_guid, ==,
		    dsl_dataset_phys(origin)->ds_guid);
		ASSERT3U(dbn->dbn_phys.zbm_referenced_bytes_refd, ==,
		    dsl_dataset_phys(origin)->ds_referenced_bytes);
		ASSERT(dbn->dbn_phys.zbm_flags &
		    ZBM_FLAG_SNAPSHOT_EXISTS);
		/*
		 * Save and restore the zbm_redaction_obj, which
		 * is zeroed by dsl_bookmark_set_phys().
		 */
		uint64_t redaction_obj =
		    dbn->dbn_phys.zbm_redaction_obj;
		dsl_bookmark_set_phys(&dbn->dbn_phys, origin);
		dbn->dbn_phys.zbm_redaction_obj = redaction_obj;

		VERIFY0(zap_update(dp->dp_meta_objset, head->ds_bookmarks_obj,
		    dbn->dbn_name, sizeof (uint64_t),
		    sizeof (zfs_bookmark_phys_t) / sizeof (uint64_t),
		    &dbn->dbn_phys, tx));
	}
}

/*
 * This block is no longer referenced by this (head) dataset.
 *
 * Adjust the FBN of any bookmarks that reference this block, whose "next"
 * is the head dataset.
 */
void
dsl_bookmark_block_killed(dsl_dataset_t *ds, const blkptr_t *bp, dmu_tx_t *tx)
{
	(void) tx;

	/*
	 * Iterate over bookmarks whose "next" is the head dataset.
	 */
	for (dsl_bookmark_node_t *dbn = avl_last(&ds->ds_bookmarks);
	    dbn != NULL && dbn->dbn_phys.zbm_creation_txg >=
	    dsl_dataset_phys(ds)->ds_prev_snap_txg;
	    dbn = AVL_PREV(&ds->ds_bookmarks, dbn)) {
		/*
		 * If the block was live (referenced) at the time of this
		 * bookmark, add its space to the bookmark's FBN.
		 */
		if (bp->blk_birth <= dbn->dbn_phys.zbm_creation_txg &&
		    (dbn->dbn_phys.zbm_flags & ZBM_FLAG_HAS_FBN)) {
			mutex_enter(&dbn->dbn_lock);
			dbn->dbn_phys.zbm_referenced_freed_before_next_snap +=
			    bp_get_dsize_sync(dsl_dataset_get_spa(ds), bp);
			dbn->dbn_phys.zbm_compressed_freed_before_next_snap +=
			    BP_GET_PSIZE(bp);
			dbn->dbn_phys.zbm_uncompressed_freed_before_next_snap +=
			    BP_GET_UCSIZE(bp);
			/*
			 * Changing the ZAP object here would be too
			 * expensive.  Also, we may be called from the zio
			 * interrupt thread, which can't block on i/o.
			 * Therefore, we mark this bookmark as dirty and
			 * modify the ZAP once per txg, in
			 * dsl_bookmark_sync_done().
			 */
			dbn->dbn_dirty = B_TRUE;
			mutex_exit(&dbn->dbn_lock);
		}
	}
}

void
dsl_bookmark_sync_done(dsl_dataset_t *ds, dmu_tx_t *tx)
{
	dsl_pool_t *dp = dmu_tx_pool(tx);

	if (dsl_dataset_is_snapshot(ds))
		return;

	/*
	 * We only dirty bookmarks that are at or after the most recent
	 * snapshot.  We can't create snapshots between
	 * dsl_bookmark_block_killed() and dsl_bookmark_sync_done(), so we
	 * don't need to look at any bookmarks before ds_prev_snap_txg.
	 */
	for (dsl_bookmark_node_t *dbn = avl_last(&ds->ds_bookmarks);
	    dbn != NULL && dbn->dbn_phys.zbm_creation_txg >=
	    dsl_dataset_phys(ds)->ds_prev_snap_txg;
	    dbn = AVL_PREV(&ds->ds_bookmarks, dbn)) {
		if (dbn->dbn_dirty) {
			/*
			 * We only dirty nodes with HAS_FBN, therefore
			 * we can always use the current bookmark struct size.
			 */
			ASSERT(dbn->dbn_phys.zbm_flags & ZBM_FLAG_HAS_FBN);
			VERIFY0(zap_update(dp->dp_meta_objset,
			    ds->ds_bookmarks_obj,
			    dbn->dbn_name, sizeof (uint64_t),
			    sizeof (zfs_bookmark_phys_t) / sizeof (uint64_t),
			    &dbn->dbn_phys, tx));
			dbn->dbn_dirty = B_FALSE;
		}
	}
#ifdef ZFS_DEBUG
	for (dsl_bookmark_node_t *dbn = avl_first(&ds->ds_bookmarks);
	    dbn != NULL; dbn = AVL_NEXT(&ds->ds_bookmarks, dbn)) {
		ASSERT(!dbn->dbn_dirty);
	}
#endif
}

/*
 * Return the TXG of the most recent bookmark (or 0 if there are no bookmarks).
 */
uint64_t
dsl_bookmark_latest_txg(dsl_dataset_t *ds)
{
	ASSERT(dsl_pool_config_held(ds->ds_dir->dd_pool));
	dsl_bookmark_node_t *dbn = avl_last(&ds->ds_bookmarks);
	if (dbn == NULL)
		return (0);
	return (dbn->dbn_phys.zbm_creation_txg);
}

/*
 * Compare the redact_block_phys_t to the bookmark. If the last block in the
 * redact_block_phys_t is before the bookmark, return -1.  If the first block in
 * the redact_block_phys_t is after the bookmark, return 1.  Otherwise, the
 * bookmark is inside the range of the redact_block_phys_t, and we return 0.
 */
static int
redact_block_zb_compare(redact_block_phys_t *first,
    zbookmark_phys_t *second)
{
	/*
	 * If the block_phys is for a previous object, or the last block in the
	 * block_phys is strictly before the block in the bookmark, the
	 * block_phys is earlier.
	 */
	if (first->rbp_object < second->zb_object ||
	    (first->rbp_object == second->zb_object &&
	    first->rbp_blkid + (redact_block_get_count(first) - 1) <
	    second->zb_blkid)) {
		return (-1);
	}

	/*
	 * If the bookmark is for a previous object, or the block in the
	 * bookmark is strictly before the first block in the block_phys, the
	 * bookmark is earlier.
	 */
	if (first->rbp_object > second->zb_object ||
	    (first->rbp_object == second->zb_object &&
	    first->rbp_blkid > second->zb_blkid)) {
		return (1);
	}

	return (0);
}

/*
 * Traverse the redaction list in the provided object, and call the callback for
 * each entry we find. Don't call the callback for any records before resume.
 */
int
dsl_redaction_list_traverse(redaction_list_t *rl, zbookmark_phys_t *resume,
    rl_traverse_callback_t cb, void *arg)
{
	objset_t *mos = rl->rl_mos;
	int err = 0;

	if (rl->rl_phys->rlp_last_object != UINT64_MAX ||
	    rl->rl_phys->rlp_last_blkid != UINT64_MAX) {
		/*
		 * When we finish a send, we update the last object and offset
		 * to UINT64_MAX.  If a send fails partway through, the last
		 * object and offset will have some other value, indicating how
		 * far the send got. The redaction list must be complete before
		 * it can be traversed, so return EINVAL if the last object and
		 * blkid are not set to UINT64_MAX.
		 */
		return (SET_ERROR(EINVAL));
	}

	/*
	 * This allows us to skip the binary search and resume checking logic
	 * below, if we're not resuming a redacted send.
	 */
	if (ZB_IS_ZERO(resume))
		resume = NULL;

	/*
	 * Binary search for the point to resume from.
	 */
	uint64_t maxidx = rl->rl_phys->rlp_num_entries - 1;
	uint64_t minidx = 0;
	while (resume != NULL && maxidx > minidx) {
		redact_block_phys_t rbp = { 0 };
		ASSERT3U(maxidx, >, minidx);
		uint64_t mididx = minidx + ((maxidx - minidx) / 2);
		err = dmu_read(mos, rl->rl_object, mididx * sizeof (rbp),
		    sizeof (rbp), &rbp, DMU_READ_NO_PREFETCH);
		if (err != 0)
			break;

		int cmp = redact_block_zb_compare(&rbp, resume);

		if (cmp == 0) {
			minidx = mididx;
			break;
		} else if (cmp > 0) {
			maxidx =
			    (mididx == minidx ? minidx : mididx - 1);
		} else {
			minidx = mididx + 1;
		}
	}

	unsigned int bufsize = SPA_OLD_MAXBLOCKSIZE;
	redact_block_phys_t *buf = zio_data_buf_alloc(bufsize);

	unsigned int entries_per_buf = bufsize / sizeof (redact_block_phys_t);
	uint64_t start_block = minidx / entries_per_buf;
	err = dmu_read(mos, rl->rl_object, start_block * bufsize, bufsize, buf,
	    DMU_READ_PREFETCH);

	for (uint64_t curidx = minidx;
	    err == 0 && curidx < rl->rl_phys->rlp_num_entries;
	    curidx++) {
		/*
		 * We read in the redaction list one block at a time.  Once we
		 * finish with all the entries in a given block, we read in a
		 * new one.  The predictive prefetcher will take care of any
		 * prefetching, and this code shouldn't be the bottleneck, so we
		 * don't need to do manual prefetching.
		 */
		if (curidx % entries_per_buf == 0) {
			err = dmu_read(mos, rl->rl_object, curidx *
			    sizeof (*buf), bufsize, buf,
			    DMU_READ_PREFETCH);
			if (err != 0)
				break;
		}
		redact_block_phys_t *rb = &buf[curidx % entries_per_buf];
		/*
		 * If resume is non-null, we should either not send the data, or
		 * null out resume so we don't have to keep doing these
		 * comparisons.
		 */
		if (resume != NULL) {
			/*
			 * It is possible that after the binary search we got
			 * a record before the resume point. There's two cases
			 * where this can occur. If the record is the last
			 * redaction record, and the resume point is after the
			 * end of the redacted data, curidx will be the last
			 * redaction record. In that case, the loop will end
			 * after this iteration. The second case is if the
			 * resume point is between two redaction records, the
			 * binary search can return either the record before
			 * or after the resume point. In that case, the next
			 * iteration will be greater than the resume point.
			 */
			if (redact_block_zb_compare(rb, resume) < 0) {
				ASSERT3U(curidx, ==, minidx);
				continue;
			} else {
				/*
				 * If the place to resume is in the middle of
				 * the range described by this
				 * redact_block_phys, then modify the
				 * redact_block_phys in memory so we generate
				 * the right records.
				 */
				if (resume->zb_object == rb->rbp_object &&
				    resume->zb_blkid > rb->rbp_blkid) {
					uint64_t diff = resume->zb_blkid -
					    rb->rbp_blkid;
					rb->rbp_blkid = resume->zb_blkid;
					redact_block_set_count(rb,
					    redact_block_get_count(rb) - diff);
				}
				resume = NULL;
			}
		}

		if (cb(rb, arg) != 0) {
			err = EINTR;
			break;
		}
	}

	zio_data_buf_free(buf, bufsize);
	return (err);
}
