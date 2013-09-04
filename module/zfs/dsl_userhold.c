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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/dsl_userhold.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_destroy.h>
#include <sys/dsl_synctask.h>
#include <sys/dmu_tx.h>
#include <sys/zfs_onexit.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_dir.h>
#include <sys/zfs_ioctl.h>
#include <sys/zap.h>

typedef struct dsl_dataset_user_hold_arg {
	nvlist_t *dduha_holds;
	nvlist_t *dduha_errlist;
	minor_t dduha_minor;
} dsl_dataset_user_hold_arg_t;

/*
 * If you add new checks here, you may need to add additional checks to the
 * "temporary" case in snapshot_check() in dmu_objset.c.
 */
int
dsl_dataset_user_hold_check_one(dsl_dataset_t *ds, const char *htag,
    boolean_t temphold, dmu_tx_t *tx)
{
	dsl_pool_t *dp = dmu_tx_pool(tx);
	objset_t *mos = dp->dp_meta_objset;
	int error = 0;

	if (strlen(htag) > MAXNAMELEN)
		return (E2BIG);
	/* Tempholds have a more restricted length */
	if (temphold && strlen(htag) + MAX_TAG_PREFIX_LEN >= MAXNAMELEN)
		return (E2BIG);

	/* tags must be unique (if ds already exists) */
	if (ds != NULL) {
		mutex_enter(&ds->ds_lock);
		if (ds->ds_phys->ds_userrefs_obj != 0) {
			uint64_t value;
			error = zap_lookup(mos, ds->ds_phys->ds_userrefs_obj,
			    htag, 8, 1, &value);
			if (error == 0)
				error = EEXIST;
			else if (error == ENOENT)
				error = 0;
		}
		mutex_exit(&ds->ds_lock);
	}

	return (error);
}

static int
dsl_dataset_user_hold_check(void *arg, dmu_tx_t *tx)
{
	dsl_dataset_user_hold_arg_t *dduha = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	nvpair_t *pair;
	int rv = 0;

	if (spa_version(dp->dp_spa) < SPA_VERSION_USERREFS)
		return (ENOTSUP);

	for (pair = nvlist_next_nvpair(dduha->dduha_holds, NULL); pair != NULL;
	    pair = nvlist_next_nvpair(dduha->dduha_holds, pair)) {
		int error = 0;
		dsl_dataset_t *ds;
		char *htag;

		/* must be a snapshot */
		if (strchr(nvpair_name(pair), '@') == NULL)
			error = EINVAL;

		if (error == 0)
			error = nvpair_value_string(pair, &htag);
		if (error == 0) {
			error = dsl_dataset_hold(dp,
			    nvpair_name(pair), FTAG, &ds);
		}
		if (error == 0) {
			error = dsl_dataset_user_hold_check_one(ds, htag,
			    dduha->dduha_minor != 0, tx);
			dsl_dataset_rele(ds, FTAG);
		}

		if (error != 0) {
			rv = error;
			fnvlist_add_int32(dduha->dduha_errlist,
			    nvpair_name(pair), error);
		}
	}
	return (rv);
}

void
dsl_dataset_user_hold_sync_one(dsl_dataset_t *ds, const char *htag,
    minor_t minor, uint64_t now, dmu_tx_t *tx)
{
	dsl_pool_t *dp = ds->ds_dir->dd_pool;
	objset_t *mos = dp->dp_meta_objset;
	uint64_t zapobj;

	mutex_enter(&ds->ds_lock);
	if (ds->ds_phys->ds_userrefs_obj == 0) {
		/*
		 * This is the first user hold for this dataset.  Create
		 * the userrefs zap object.
		 */
		dmu_buf_will_dirty(ds->ds_dbuf, tx);
		zapobj = ds->ds_phys->ds_userrefs_obj =
		    zap_create(mos, DMU_OT_USERREFS, DMU_OT_NONE, 0, tx);
	} else {
		zapobj = ds->ds_phys->ds_userrefs_obj;
	}
	ds->ds_userrefs++;
	mutex_exit(&ds->ds_lock);

	VERIFY0(zap_add(mos, zapobj, htag, 8, 1, &now, tx));

	if (minor != 0) {
		VERIFY0(dsl_pool_user_hold(dp, ds->ds_object,
		    htag, now, tx));
		dsl_register_onexit_hold_cleanup(ds, htag, minor);
	}

	spa_history_log_internal_ds(ds, "hold", tx,
	    "tag=%s temp=%d refs=%llu",
	    htag, minor != 0, ds->ds_userrefs);
}

static void
dsl_dataset_user_hold_sync(void *arg, dmu_tx_t *tx)
{
	dsl_dataset_user_hold_arg_t *dduha = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	nvpair_t *pair;
	uint64_t now = gethrestime_sec();

	for (pair = nvlist_next_nvpair(dduha->dduha_holds, NULL); pair != NULL;
	    pair = nvlist_next_nvpair(dduha->dduha_holds, pair)) {
		dsl_dataset_t *ds;
		VERIFY0(dsl_dataset_hold(dp, nvpair_name(pair), FTAG, &ds));
		dsl_dataset_user_hold_sync_one(ds, fnvpair_value_string(pair),
		    dduha->dduha_minor, now, tx);
		dsl_dataset_rele(ds, FTAG);
	}
}

/*
 * holds is nvl of snapname -> holdname
 * errlist will be filled in with snapname -> error
 * if cleanup_minor is not 0, the holds will be temporary, cleaned up
 * when the process exits.
 *
 * if any fails, all will fail.
 */
int
dsl_dataset_user_hold(nvlist_t *holds, minor_t cleanup_minor, nvlist_t *errlist)
{
	dsl_dataset_user_hold_arg_t dduha;
	nvpair_t *pair;

	pair = nvlist_next_nvpair(holds, NULL);
	if (pair == NULL)
		return (0);

	dduha.dduha_holds = holds;
	dduha.dduha_errlist = errlist;
	dduha.dduha_minor = cleanup_minor;

	return (dsl_sync_task(nvpair_name(pair), dsl_dataset_user_hold_check,
	    dsl_dataset_user_hold_sync, &dduha, fnvlist_num_pairs(holds)));
}

typedef struct dsl_dataset_user_release_arg {
	nvlist_t *ddura_holds;
	nvlist_t *ddura_todelete;
	nvlist_t *ddura_errlist;
} dsl_dataset_user_release_arg_t;

static int
dsl_dataset_user_release_check_one(dsl_dataset_t *ds,
    nvlist_t *holds, boolean_t *todelete)
{
	uint64_t zapobj;
	nvpair_t *pair;
	objset_t *mos = ds->ds_dir->dd_pool->dp_meta_objset;
	int error;
	int numholds = 0;

	*todelete = B_FALSE;

	if (!dsl_dataset_is_snapshot(ds))
		return (EINVAL);

	zapobj = ds->ds_phys->ds_userrefs_obj;
	if (zapobj == 0)
		return (ESRCH);

	for (pair = nvlist_next_nvpair(holds, NULL); pair != NULL;
	    pair = nvlist_next_nvpair(holds, pair)) {
		/* Make sure the hold exists */
		uint64_t tmp;
		error = zap_lookup(mos, zapobj, nvpair_name(pair), 8, 1, &tmp);
		if (error == ENOENT)
			error = ESRCH;
		if (error != 0)
			return (error);
		numholds++;
	}

	if (DS_IS_DEFER_DESTROY(ds) && ds->ds_phys->ds_num_children == 1 &&
	    ds->ds_userrefs == numholds) {
		/* we need to destroy the snapshot as well */

		if (dsl_dataset_long_held(ds))
			return (EBUSY);
		*todelete = B_TRUE;
	}
	return (0);
}

static int
dsl_dataset_user_release_check(void *arg, dmu_tx_t *tx)
{
	dsl_dataset_user_release_arg_t *ddura = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	nvpair_t *pair;
	int rv = 0;

	if (!dmu_tx_is_syncing(tx))
		return (0);

	for (pair = nvlist_next_nvpair(ddura->ddura_holds, NULL); pair != NULL;
	    pair = nvlist_next_nvpair(ddura->ddura_holds, pair)) {
		const char *name = nvpair_name(pair);
		int error;
		dsl_dataset_t *ds;
		nvlist_t *holds;

		error = nvpair_value_nvlist(pair, &holds);
		if (error != 0)
			return (EINVAL);

		error = dsl_dataset_hold(dp, name, FTAG, &ds);
		if (error == 0) {
			boolean_t deleteme;
			error = dsl_dataset_user_release_check_one(ds,
			    holds, &deleteme);
			if (error == 0 && deleteme) {
				fnvlist_add_boolean(ddura->ddura_todelete,
				    name);
			}
			dsl_dataset_rele(ds, FTAG);
		}
		if (error != 0) {
			if (ddura->ddura_errlist != NULL) {
				fnvlist_add_int32(ddura->ddura_errlist,
				    name, error);
			}
			rv = error;
		}
	}
	return (rv);
}

static void
dsl_dataset_user_release_sync_one(dsl_dataset_t *ds, nvlist_t *holds,
    dmu_tx_t *tx)
{
	dsl_pool_t *dp = ds->ds_dir->dd_pool;
	objset_t *mos = dp->dp_meta_objset;
	uint64_t zapobj;
	int error;
	nvpair_t *pair;

	for (pair = nvlist_next_nvpair(holds, NULL); pair != NULL;
	    pair = nvlist_next_nvpair(holds, pair)) {
		ds->ds_userrefs--;
		error = dsl_pool_user_release(dp, ds->ds_object,
		    nvpair_name(pair), tx);
		VERIFY(error == 0 || error == ENOENT);
		zapobj = ds->ds_phys->ds_userrefs_obj;
		VERIFY0(zap_remove(mos, zapobj, nvpair_name(pair), tx));

		spa_history_log_internal_ds(ds, "release", tx,
		    "tag=%s refs=%lld", nvpair_name(pair),
		    (longlong_t)ds->ds_userrefs);
	}
}

static void
dsl_dataset_user_release_sync(void *arg, dmu_tx_t *tx)
{
	dsl_dataset_user_release_arg_t *ddura = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	nvpair_t *pair;

	for (pair = nvlist_next_nvpair(ddura->ddura_holds, NULL); pair != NULL;
	    pair = nvlist_next_nvpair(ddura->ddura_holds, pair)) {
		dsl_dataset_t *ds;

		VERIFY0(dsl_dataset_hold(dp, nvpair_name(pair), FTAG, &ds));
		dsl_dataset_user_release_sync_one(ds,
		    fnvpair_value_nvlist(pair), tx);
		if (nvlist_exists(ddura->ddura_todelete,
		    nvpair_name(pair))) {
			ASSERT(ds->ds_userrefs == 0 &&
			    ds->ds_phys->ds_num_children == 1 &&
			    DS_IS_DEFER_DESTROY(ds));
			dsl_destroy_snapshot_sync_impl(ds, B_FALSE, tx);
		}
		dsl_dataset_rele(ds, FTAG);
	}
}

/*
 * holds is nvl of snapname -> { holdname, ... }
 * errlist will be filled in with snapname -> error
 *
 * if any fails, all will fail.
 */
int
dsl_dataset_user_release(nvlist_t *holds, nvlist_t *errlist)
{
	dsl_dataset_user_release_arg_t ddura;
	nvpair_t *pair;
	int error;

	pair = nvlist_next_nvpair(holds, NULL);
	if (pair == NULL)
		return (0);

	ddura.ddura_holds = holds;
	ddura.ddura_errlist = errlist;
	ddura.ddura_todelete = fnvlist_alloc();

	error = dsl_sync_task(nvpair_name(pair), dsl_dataset_user_release_check,
	    dsl_dataset_user_release_sync, &ddura, fnvlist_num_pairs(holds));
	fnvlist_free(ddura.ddura_todelete);
	return (error);
}

typedef struct dsl_dataset_user_release_tmp_arg {
	uint64_t ddurta_dsobj;
	nvlist_t *ddurta_holds;
	boolean_t ddurta_deleteme;
} dsl_dataset_user_release_tmp_arg_t;

static int
dsl_dataset_user_release_tmp_check(void *arg, dmu_tx_t *tx)
{
	dsl_dataset_user_release_tmp_arg_t *ddurta = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	dsl_dataset_t *ds;
	int error;

	if (!dmu_tx_is_syncing(tx))
		return (0);

	error = dsl_dataset_hold_obj(dp, ddurta->ddurta_dsobj, FTAG, &ds);
	if (error)
		return (error);

	error = dsl_dataset_user_release_check_one(ds,
	    ddurta->ddurta_holds, &ddurta->ddurta_deleteme);
	dsl_dataset_rele(ds, FTAG);
	return (error);
}

static void
dsl_dataset_user_release_tmp_sync(void *arg, dmu_tx_t *tx)
{
	dsl_dataset_user_release_tmp_arg_t *ddurta = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	dsl_dataset_t *ds;

	VERIFY0(dsl_dataset_hold_obj(dp, ddurta->ddurta_dsobj, FTAG, &ds));
	dsl_dataset_user_release_sync_one(ds, ddurta->ddurta_holds, tx);
	if (ddurta->ddurta_deleteme) {
		ASSERT(ds->ds_userrefs == 0 &&
		    ds->ds_phys->ds_num_children == 1 &&
		    DS_IS_DEFER_DESTROY(ds));
		dsl_destroy_snapshot_sync_impl(ds, B_FALSE, tx);
	}
	dsl_dataset_rele(ds, FTAG);
}

/*
 * Called at spa_load time to release a stale temporary user hold.
 * Also called by the onexit code.
 */
void
dsl_dataset_user_release_tmp(dsl_pool_t *dp, uint64_t dsobj, const char *htag)
{
	dsl_dataset_user_release_tmp_arg_t ddurta;

#ifdef _KERNEL
	dsl_dataset_t *ds;
	int error;

	/* Make sure it is not mounted. */
	dsl_pool_config_enter(dp, FTAG);
	error = dsl_dataset_hold_obj(dp, dsobj, FTAG, &ds);
	if (error == 0) {
		char name[MAXNAMELEN];
		dsl_dataset_name(ds, name);
		dsl_dataset_rele(ds, FTAG);
		dsl_pool_config_exit(dp, FTAG);
		zfs_unmount_snap(name);
	} else {
		dsl_pool_config_exit(dp, FTAG);
	}
#endif

	ddurta.ddurta_dsobj = dsobj;
	ddurta.ddurta_holds = fnvlist_alloc();
	fnvlist_add_boolean(ddurta.ddurta_holds, htag);

	(void) dsl_sync_task(spa_name(dp->dp_spa),
	    dsl_dataset_user_release_tmp_check,
	    dsl_dataset_user_release_tmp_sync, &ddurta, 1);
	fnvlist_free(ddurta.ddurta_holds);
}

typedef struct zfs_hold_cleanup_arg {
	char zhca_spaname[MAXNAMELEN];
	uint64_t zhca_spa_load_guid;
	uint64_t zhca_dsobj;
	char zhca_htag[MAXNAMELEN];
} zfs_hold_cleanup_arg_t;

static void
dsl_dataset_user_release_onexit(void *arg)
{
	zfs_hold_cleanup_arg_t *ca = arg;
	spa_t *spa;
	int error;

	error = spa_open(ca->zhca_spaname, &spa, FTAG);
	if (error != 0) {
		zfs_dbgmsg("couldn't release hold on pool=%s ds=%llu tag=%s "
		    "because pool is no longer loaded",
		    ca->zhca_spaname, ca->zhca_dsobj, ca->zhca_htag);
		return;
	}
	if (spa_load_guid(spa) != ca->zhca_spa_load_guid) {
		zfs_dbgmsg("couldn't release hold on pool=%s ds=%llu tag=%s "
		    "because pool is no longer loaded (guid doesn't match)",
		    ca->zhca_spaname, ca->zhca_dsobj, ca->zhca_htag);
		spa_close(spa, FTAG);
		return;
	}

	dsl_dataset_user_release_tmp(spa_get_dsl(spa),
	    ca->zhca_dsobj, ca->zhca_htag);
	kmem_free(ca, sizeof (zfs_hold_cleanup_arg_t));
	spa_close(spa, FTAG);
}

void
dsl_register_onexit_hold_cleanup(dsl_dataset_t *ds, const char *htag,
    minor_t minor)
{
	zfs_hold_cleanup_arg_t *ca = kmem_alloc(sizeof (*ca), KM_SLEEP);
	spa_t *spa = dsl_dataset_get_spa(ds);
	(void) strlcpy(ca->zhca_spaname, spa_name(spa),
	    sizeof (ca->zhca_spaname));
	ca->zhca_spa_load_guid = spa_load_guid(spa);
	ca->zhca_dsobj = ds->ds_object;
	(void) strlcpy(ca->zhca_htag, htag, sizeof (ca->zhca_htag));
	VERIFY0(zfs_onexit_add_cb(minor,
	    dsl_dataset_user_release_onexit, ca, NULL));
}

int
dsl_dataset_get_holds(const char *dsname, nvlist_t *nvl)
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

	if (ds->ds_phys->ds_userrefs_obj != 0) {
		zap_attribute_t *za;
		zap_cursor_t zc;

		za = kmem_alloc(sizeof (zap_attribute_t), KM_SLEEP);
		for (zap_cursor_init(&zc, ds->ds_dir->dd_pool->dp_meta_objset,
		    ds->ds_phys->ds_userrefs_obj);
		    zap_cursor_retrieve(&zc, za) == 0;
		    zap_cursor_advance(&zc)) {
			fnvlist_add_uint64(nvl, za->za_name,
			    za->za_first_integer);
		}
		zap_cursor_fini(&zc);
		kmem_free(za, sizeof (zap_attribute_t));
	}
	dsl_dataset_rele(ds, FTAG);
	dsl_pool_rele(dp, FTAG);
	return (0);
}
