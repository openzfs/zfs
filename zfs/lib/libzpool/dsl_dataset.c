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

#pragma ident	"@(#)dsl_dataset.c	1.42	08/04/28 SMI"

#include <sys/dmu_objset.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_synctask.h>
#include <sys/dmu_traverse.h>
#include <sys/dmu_tx.h>
#include <sys/arc.h>
#include <sys/zio.h>
#include <sys/zap.h>
#include <sys/unique.h>
#include <sys/zfs_context.h>
#include <sys/zfs_ioctl.h>
#include <sys/spa.h>
#include <sys/sunddi.h>

static dsl_checkfunc_t dsl_dataset_destroy_begin_check;
static dsl_syncfunc_t dsl_dataset_destroy_begin_sync;
static dsl_checkfunc_t dsl_dataset_rollback_check;
static dsl_syncfunc_t dsl_dataset_rollback_sync;
static dsl_syncfunc_t dsl_dataset_set_reservation_sync;

#define	DS_REF_MAX	(1ULL << 62)

#define	DSL_DEADLIST_BLOCKSIZE	SPA_MAXBLOCKSIZE

/*
 * We use weighted reference counts to express the various forms of exclusion
 * between different open modes.  A STANDARD open is 1 point, an EXCLUSIVE open
 * is DS_REF_MAX, and a PRIMARY open is little more than half of an EXCLUSIVE.
 * This makes the exclusion logic simple: the total refcnt for all opens cannot
 * exceed DS_REF_MAX.  For example, EXCLUSIVE opens are exclusive because their
 * weight (DS_REF_MAX) consumes the entire refcnt space.  PRIMARY opens consume
 * just over half of the refcnt space, so there can't be more than one, but it
 * can peacefully coexist with any number of STANDARD opens.
 */
static uint64_t ds_refcnt_weight[DS_MODE_LEVELS] = {
	0,			/* DS_MODE_NONE - invalid		*/
	1,			/* DS_MODE_STANDARD - unlimited number	*/
	(DS_REF_MAX >> 1) + 1,	/* DS_MODE_PRIMARY - only one of these	*/
	DS_REF_MAX		/* DS_MODE_EXCLUSIVE - no other opens	*/
};

/*
 * Figure out how much of this delta should be propogated to the dsl_dir
 * layer.  If there's a refreservation, that space has already been
 * partially accounted for in our ancestors.
 */
static int64_t
parent_delta(dsl_dataset_t *ds, int64_t delta)
{
	uint64_t old_bytes, new_bytes;

	if (ds->ds_reserved == 0)
		return (delta);

	old_bytes = MAX(ds->ds_phys->ds_unique_bytes, ds->ds_reserved);
	new_bytes = MAX(ds->ds_phys->ds_unique_bytes + delta, ds->ds_reserved);

	ASSERT3U(ABS((int64_t)(new_bytes - old_bytes)), <=, ABS(delta));
	return (new_bytes - old_bytes);
}

void
dsl_dataset_block_born(dsl_dataset_t *ds, blkptr_t *bp, dmu_tx_t *tx)
{
	int used = bp_get_dasize(tx->tx_pool->dp_spa, bp);
	int compressed = BP_GET_PSIZE(bp);
	int uncompressed = BP_GET_UCSIZE(bp);
	int64_t delta;

	dprintf_bp(bp, "born, ds=%p\n", ds);

	ASSERT(dmu_tx_is_syncing(tx));
	/* It could have been compressed away to nothing */
	if (BP_IS_HOLE(bp))
		return;
	ASSERT(BP_GET_TYPE(bp) != DMU_OT_NONE);
	ASSERT3U(BP_GET_TYPE(bp), <, DMU_OT_NUMTYPES);
	if (ds == NULL) {
		/*
		 * Account for the meta-objset space in its placeholder
		 * dsl_dir.
		 */
		ASSERT3U(compressed, ==, uncompressed); /* it's all metadata */
		dsl_dir_diduse_space(tx->tx_pool->dp_mos_dir,
		    used, compressed, uncompressed, tx);
		dsl_dir_dirty(tx->tx_pool->dp_mos_dir, tx);
		return;
	}
	dmu_buf_will_dirty(ds->ds_dbuf, tx);
	mutex_enter(&ds->ds_lock);
	delta = parent_delta(ds, used);
	ds->ds_phys->ds_used_bytes += used;
	ds->ds_phys->ds_compressed_bytes += compressed;
	ds->ds_phys->ds_uncompressed_bytes += uncompressed;
	ds->ds_phys->ds_unique_bytes += used;
	mutex_exit(&ds->ds_lock);
	dsl_dir_diduse_space(ds->ds_dir, delta, compressed, uncompressed, tx);
}

void
dsl_dataset_block_kill(dsl_dataset_t *ds, blkptr_t *bp, zio_t *pio,
    dmu_tx_t *tx)
{
	int used = bp_get_dasize(tx->tx_pool->dp_spa, bp);
	int compressed = BP_GET_PSIZE(bp);
	int uncompressed = BP_GET_UCSIZE(bp);

	ASSERT(dmu_tx_is_syncing(tx));
	/* No block pointer => nothing to free */
	if (BP_IS_HOLE(bp))
		return;

	ASSERT(used > 0);
	if (ds == NULL) {
		int err;
		/*
		 * Account for the meta-objset space in its placeholder
		 * dataset.
		 */
		err = arc_free(pio, tx->tx_pool->dp_spa,
		    tx->tx_txg, bp, NULL, NULL, pio ? ARC_NOWAIT: ARC_WAIT);
		ASSERT(err == 0);

		dsl_dir_diduse_space(tx->tx_pool->dp_mos_dir,
		    -used, -compressed, -uncompressed, tx);
		dsl_dir_dirty(tx->tx_pool->dp_mos_dir, tx);
		return;
	}
	ASSERT3P(tx->tx_pool, ==, ds->ds_dir->dd_pool);

	dmu_buf_will_dirty(ds->ds_dbuf, tx);

	if (bp->blk_birth > ds->ds_phys->ds_prev_snap_txg) {
		int err;
		int64_t delta;

		dprintf_bp(bp, "freeing: %s", "");
		err = arc_free(pio, tx->tx_pool->dp_spa,
		    tx->tx_txg, bp, NULL, NULL, pio ? ARC_NOWAIT: ARC_WAIT);
		ASSERT(err == 0);

		mutex_enter(&ds->ds_lock);
		ASSERT(ds->ds_phys->ds_unique_bytes >= used ||
		    !DS_UNIQUE_IS_ACCURATE(ds));
		delta = parent_delta(ds, -used);
		ds->ds_phys->ds_unique_bytes -= used;
		mutex_exit(&ds->ds_lock);
		dsl_dir_diduse_space(ds->ds_dir,
		    delta, -compressed, -uncompressed, tx);
	} else {
		dprintf_bp(bp, "putting on dead list: %s", "");
		VERIFY(0 == bplist_enqueue(&ds->ds_deadlist, bp, tx));
		ASSERT3U(ds->ds_prev->ds_object, ==,
		    ds->ds_phys->ds_prev_snap_obj);
		ASSERT(ds->ds_prev->ds_phys->ds_num_children > 0);
		/* if (bp->blk_birth > prev prev snap txg) prev unique += bs */
		if (ds->ds_prev->ds_phys->ds_next_snap_obj ==
		    ds->ds_object && bp->blk_birth >
		    ds->ds_prev->ds_phys->ds_prev_snap_txg) {
			dmu_buf_will_dirty(ds->ds_prev->ds_dbuf, tx);
			mutex_enter(&ds->ds_prev->ds_lock);
			ds->ds_prev->ds_phys->ds_unique_bytes += used;
			mutex_exit(&ds->ds_prev->ds_lock);
		}
	}
	mutex_enter(&ds->ds_lock);
	ASSERT3U(ds->ds_phys->ds_used_bytes, >=, used);
	ds->ds_phys->ds_used_bytes -= used;
	ASSERT3U(ds->ds_phys->ds_compressed_bytes, >=, compressed);
	ds->ds_phys->ds_compressed_bytes -= compressed;
	ASSERT3U(ds->ds_phys->ds_uncompressed_bytes, >=, uncompressed);
	ds->ds_phys->ds_uncompressed_bytes -= uncompressed;
	mutex_exit(&ds->ds_lock);
}

uint64_t
dsl_dataset_prev_snap_txg(dsl_dataset_t *ds)
{
	uint64_t trysnap = 0;

	if (ds == NULL)
		return (0);
	/*
	 * The snapshot creation could fail, but that would cause an
	 * incorrect FALSE return, which would only result in an
	 * overestimation of the amount of space that an operation would
	 * consume, which is OK.
	 *
	 * There's also a small window where we could miss a pending
	 * snapshot, because we could set the sync task in the quiescing
	 * phase.  So this should only be used as a guess.
	 */
	if (ds->ds_trysnap_txg >
	    spa_last_synced_txg(ds->ds_dir->dd_pool->dp_spa))
		trysnap = ds->ds_trysnap_txg;
	return (MAX(ds->ds_phys->ds_prev_snap_txg, trysnap));
}

int
dsl_dataset_block_freeable(dsl_dataset_t *ds, uint64_t blk_birth)
{
	return (blk_birth > dsl_dataset_prev_snap_txg(ds));
}

/* ARGSUSED */
static void
dsl_dataset_evict(dmu_buf_t *db, void *dsv)
{
	dsl_dataset_t *ds = dsv;

	/* open_refcount == DS_REF_MAX when deleting */
	ASSERT(ds->ds_open_refcount == 0 ||
	    ds->ds_open_refcount == DS_REF_MAX);

	dprintf_ds(ds, "evicting %s\n", "");

	unique_remove(ds->ds_fsid_guid);

	if (ds->ds_user_ptr != NULL)
		ds->ds_user_evict_func(ds, ds->ds_user_ptr);

	if (ds->ds_prev) {
		dsl_dataset_close(ds->ds_prev, DS_MODE_NONE, ds);
		ds->ds_prev = NULL;
	}

	bplist_close(&ds->ds_deadlist);
	dsl_dir_close(ds->ds_dir, ds);

	ASSERT(!list_link_active(&ds->ds_synced_link));

	mutex_destroy(&ds->ds_lock);
	mutex_destroy(&ds->ds_opening_lock);
	mutex_destroy(&ds->ds_deadlist.bpl_lock);

	kmem_free(ds, sizeof (dsl_dataset_t));
}

static int
dsl_dataset_get_snapname(dsl_dataset_t *ds)
{
	dsl_dataset_phys_t *headphys;
	int err;
	dmu_buf_t *headdbuf;
	dsl_pool_t *dp = ds->ds_dir->dd_pool;
	objset_t *mos = dp->dp_meta_objset;

	if (ds->ds_snapname[0])
		return (0);
	if (ds->ds_phys->ds_next_snap_obj == 0)
		return (0);

	err = dmu_bonus_hold(mos, ds->ds_dir->dd_phys->dd_head_dataset_obj,
	    FTAG, &headdbuf);
	if (err)
		return (err);
	headphys = headdbuf->db_data;
	err = zap_value_search(dp->dp_meta_objset,
	    headphys->ds_snapnames_zapobj, ds->ds_object, 0, ds->ds_snapname);
	dmu_buf_rele(headdbuf, FTAG);
	return (err);
}

static int
dsl_dataset_snap_lookup(objset_t *os, uint64_t flags,
    uint64_t snapnames_zapobj, const char *name, uint64_t *value)
{
	matchtype_t mt;
	int err;

	if (flags & DS_FLAG_CI_DATASET)
		mt = MT_FIRST;
	else
		mt = MT_EXACT;

	err = zap_lookup_norm(os, snapnames_zapobj, name, 8, 1,
	    value, mt, NULL, 0, NULL);
	if (err == ENOTSUP && mt == MT_FIRST)
		err = zap_lookup(os, snapnames_zapobj, name, 8, 1, value);
	return (err);
}

static int
dsl_dataset_snap_remove(objset_t *os, uint64_t flags,
    uint64_t snapnames_zapobj, char *name, dmu_tx_t *tx)
{
	matchtype_t mt;
	int err;

	if (flags & DS_FLAG_CI_DATASET)
		mt = MT_FIRST;
	else
		mt = MT_EXACT;

	err = zap_remove_norm(os, snapnames_zapobj, name, mt, tx);
	if (err == ENOTSUP && mt == MT_FIRST)
		err = zap_remove(os, snapnames_zapobj, name, tx);
	return (err);
}

int
dsl_dataset_open_obj(dsl_pool_t *dp, uint64_t dsobj, const char *snapname,
    int mode, void *tag, dsl_dataset_t **dsp)
{
	uint64_t weight = ds_refcnt_weight[DS_MODE_LEVEL(mode)];
	objset_t *mos = dp->dp_meta_objset;
	dmu_buf_t *dbuf;
	dsl_dataset_t *ds;
	int err;

	ASSERT(RW_LOCK_HELD(&dp->dp_config_rwlock) ||
	    dsl_pool_sync_context(dp));

	err = dmu_bonus_hold(mos, dsobj, tag, &dbuf);
	if (err)
		return (err);
	ds = dmu_buf_get_user(dbuf);
	if (ds == NULL) {
		dsl_dataset_t *winner;

		ds = kmem_zalloc(sizeof (dsl_dataset_t), KM_SLEEP);
		ds->ds_dbuf = dbuf;
		ds->ds_object = dsobj;
		ds->ds_phys = dbuf->db_data;

		mutex_init(&ds->ds_lock, NULL, MUTEX_DEFAULT, NULL);
		mutex_init(&ds->ds_opening_lock, NULL, MUTEX_DEFAULT, NULL);
		mutex_init(&ds->ds_deadlist.bpl_lock, NULL, MUTEX_DEFAULT,
		    NULL);

		err = bplist_open(&ds->ds_deadlist,
		    mos, ds->ds_phys->ds_deadlist_obj);
		if (err == 0) {
			err = dsl_dir_open_obj(dp,
			    ds->ds_phys->ds_dir_obj, NULL, ds, &ds->ds_dir);
		}
		if (err) {
			/*
			 * we don't really need to close the blist if we
			 * just opened it.
			 */
			mutex_destroy(&ds->ds_lock);
			mutex_destroy(&ds->ds_opening_lock);
			mutex_destroy(&ds->ds_deadlist.bpl_lock);
			kmem_free(ds, sizeof (dsl_dataset_t));
			dmu_buf_rele(dbuf, tag);
			return (err);
		}

		if (ds->ds_dir->dd_phys->dd_head_dataset_obj == dsobj) {
			ds->ds_snapname[0] = '\0';
			if (ds->ds_phys->ds_prev_snap_obj) {
				err = dsl_dataset_open_obj(dp,
				    ds->ds_phys->ds_prev_snap_obj, NULL,
				    DS_MODE_NONE, ds, &ds->ds_prev);
			}
		} else {
			if (snapname) {
#ifdef ZFS_DEBUG
				dsl_dataset_phys_t *headphys;
				dmu_buf_t *headdbuf;
				err = dmu_bonus_hold(mos,
				    ds->ds_dir->dd_phys->dd_head_dataset_obj,
				    FTAG, &headdbuf);
				if (err == 0) {
					uint64_t foundobj;

					headphys = headdbuf->db_data;
					err = dsl_dataset_snap_lookup(
					    dp->dp_meta_objset,
					    headphys->ds_flags,
					    headphys->ds_snapnames_zapobj,
					    snapname, &foundobj);
					ASSERT3U(foundobj, ==, dsobj);
					dmu_buf_rele(headdbuf, FTAG);
				}
#endif
				(void) strcat(ds->ds_snapname, snapname);
			} else if (zfs_flags & ZFS_DEBUG_SNAPNAMES) {
				err = dsl_dataset_get_snapname(ds);
			}
		}

		if (!dsl_dataset_is_snapshot(ds)) {
			/*
			 * In sync context, we're called with either no lock
			 * or with the write lock.  If we're not syncing,
			 * we're always called with the read lock held.
			 */
			boolean_t need_lock =
			    !RW_WRITE_HELD(&dp->dp_config_rwlock) &&
			    dsl_pool_sync_context(dp);

			if (need_lock)
				rw_enter(&dp->dp_config_rwlock, RW_READER);

			err = dsl_prop_get_ds_locked(ds->ds_dir,
			    "refreservation", sizeof (uint64_t), 1,
			    &ds->ds_reserved, NULL);
			if (err == 0) {
				err = dsl_prop_get_ds_locked(ds->ds_dir,
				    "refquota", sizeof (uint64_t), 1,
				    &ds->ds_quota, NULL);
			}

			if (need_lock)
				rw_exit(&dp->dp_config_rwlock);
		} else {
			ds->ds_reserved = ds->ds_quota = 0;
		}

		if (err == 0) {
			winner = dmu_buf_set_user_ie(dbuf, ds, &ds->ds_phys,
			    dsl_dataset_evict);
		}
		if (err || winner) {
			bplist_close(&ds->ds_deadlist);
			if (ds->ds_prev) {
				dsl_dataset_close(ds->ds_prev,
				    DS_MODE_NONE, ds);
			}
			dsl_dir_close(ds->ds_dir, ds);
			mutex_destroy(&ds->ds_lock);
			mutex_destroy(&ds->ds_opening_lock);
			mutex_destroy(&ds->ds_deadlist.bpl_lock);
			kmem_free(ds, sizeof (dsl_dataset_t));
			if (err) {
				dmu_buf_rele(dbuf, tag);
				return (err);
			}
			ds = winner;
		} else {
			ds->ds_fsid_guid =
			    unique_insert(ds->ds_phys->ds_fsid_guid);
		}
	}
	ASSERT3P(ds->ds_dbuf, ==, dbuf);
	ASSERT3P(ds->ds_phys, ==, dbuf->db_data);

	mutex_enter(&ds->ds_lock);
	if ((DS_MODE_LEVEL(mode) == DS_MODE_PRIMARY &&
	    (ds->ds_phys->ds_flags & DS_FLAG_INCONSISTENT) &&
	    !DS_MODE_IS_INCONSISTENT(mode)) ||
	    (ds->ds_open_refcount + weight > DS_REF_MAX)) {
		mutex_exit(&ds->ds_lock);
		dsl_dataset_close(ds, DS_MODE_NONE, tag);
		return (EBUSY);
	}
	ds->ds_open_refcount += weight;
	mutex_exit(&ds->ds_lock);

	*dsp = ds;
	return (0);
}

int
dsl_dataset_open_spa(spa_t *spa, const char *name, int mode,
    void *tag, dsl_dataset_t **dsp)
{
	dsl_dir_t *dd;
	dsl_pool_t *dp;
	const char *tail;
	uint64_t obj;
	dsl_dataset_t *ds = NULL;
	int err = 0;

	err = dsl_dir_open_spa(spa, name, FTAG, &dd, &tail);
	if (err)
		return (err);

	dp = dd->dd_pool;
	obj = dd->dd_phys->dd_head_dataset_obj;
	rw_enter(&dp->dp_config_rwlock, RW_READER);
	if (obj == 0) {
		/* A dataset with no associated objset */
		err = ENOENT;
		goto out;
	}

	if (tail != NULL) {
		objset_t *mos = dp->dp_meta_objset;
		uint64_t flags;

		err = dsl_dataset_open_obj(dp, obj, NULL,
		    DS_MODE_NONE, tag, &ds);
		if (err)
			goto out;
		flags = ds->ds_phys->ds_flags;
		obj = ds->ds_phys->ds_snapnames_zapobj;
		dsl_dataset_close(ds, DS_MODE_NONE, tag);
		ds = NULL;

		if (tail[0] != '@') {
			err = ENOENT;
			goto out;
		}
		tail++;

		/* Look for a snapshot */
		if (!DS_MODE_IS_READONLY(mode)) {
			err = EROFS;
			goto out;
		}
		dprintf("looking for snapshot '%s'\n", tail);
		err = dsl_dataset_snap_lookup(mos, flags, obj, tail, &obj);
		if (err)
			goto out;
	}
	err = dsl_dataset_open_obj(dp, obj, tail, mode, tag, &ds);

out:
	rw_exit(&dp->dp_config_rwlock);
	dsl_dir_close(dd, FTAG);

	ASSERT3U((err == 0), ==, (ds != NULL));
	/* ASSERT(ds == NULL || strcmp(name, ds->ds_name) == 0); */

	*dsp = ds;
	return (err);
}

int
dsl_dataset_open(const char *name, int mode, void *tag, dsl_dataset_t **dsp)
{
	return (dsl_dataset_open_spa(NULL, name, mode, tag, dsp));
}

void
dsl_dataset_name(dsl_dataset_t *ds, char *name)
{
	if (ds == NULL) {
		(void) strcpy(name, "mos");
	} else {
		dsl_dir_name(ds->ds_dir, name);
		VERIFY(0 == dsl_dataset_get_snapname(ds));
		if (ds->ds_snapname[0]) {
			(void) strcat(name, "@");
			if (!MUTEX_HELD(&ds->ds_lock)) {
				/*
				 * We use a "recursive" mutex so that we
				 * can call dprintf_ds() with ds_lock held.
				 */
				mutex_enter(&ds->ds_lock);
				(void) strcat(name, ds->ds_snapname);
				mutex_exit(&ds->ds_lock);
			} else {
				(void) strcat(name, ds->ds_snapname);
			}
		}
	}
}

static int
dsl_dataset_namelen(dsl_dataset_t *ds)
{
	int result;

	if (ds == NULL) {
		result = 3;	/* "mos" */
	} else {
		result = dsl_dir_namelen(ds->ds_dir);
		VERIFY(0 == dsl_dataset_get_snapname(ds));
		if (ds->ds_snapname[0]) {
			++result;	/* adding one for the @-sign */
			if (!MUTEX_HELD(&ds->ds_lock)) {
				/* see dsl_datset_name */
				mutex_enter(&ds->ds_lock);
				result += strlen(ds->ds_snapname);
				mutex_exit(&ds->ds_lock);
			} else {
				result += strlen(ds->ds_snapname);
			}
		}
	}

	return (result);
}

void
dsl_dataset_close(dsl_dataset_t *ds, int mode, void *tag)
{
	uint64_t weight = ds_refcnt_weight[DS_MODE_LEVEL(mode)];
	mutex_enter(&ds->ds_lock);
	ASSERT3U(ds->ds_open_refcount, >=, weight);
	ds->ds_open_refcount -= weight;
	mutex_exit(&ds->ds_lock);

	dmu_buf_rele(ds->ds_dbuf, tag);
}

void
dsl_dataset_downgrade(dsl_dataset_t *ds, int oldmode, int newmode)
{
	uint64_t oldweight = ds_refcnt_weight[DS_MODE_LEVEL(oldmode)];
	uint64_t newweight = ds_refcnt_weight[DS_MODE_LEVEL(newmode)];
	mutex_enter(&ds->ds_lock);
	ASSERT3U(ds->ds_open_refcount, >=, oldweight);
	ASSERT3U(oldweight, >=, newweight);
	ds->ds_open_refcount -= oldweight;
	ds->ds_open_refcount += newweight;
	mutex_exit(&ds->ds_lock);
}

boolean_t
dsl_dataset_tryupgrade(dsl_dataset_t *ds, int oldmode, int newmode)
{
	boolean_t rv;
	uint64_t oldweight = ds_refcnt_weight[DS_MODE_LEVEL(oldmode)];
	uint64_t newweight = ds_refcnt_weight[DS_MODE_LEVEL(newmode)];
	mutex_enter(&ds->ds_lock);
	ASSERT3U(ds->ds_open_refcount, >=, oldweight);
	ASSERT3U(newweight, >=, oldweight);
	if (ds->ds_open_refcount - oldweight + newweight > DS_REF_MAX) {
		rv = B_FALSE;
	} else {
		ds->ds_open_refcount -= oldweight;
		ds->ds_open_refcount += newweight;
		rv = B_TRUE;
	}
	mutex_exit(&ds->ds_lock);
	return (rv);
}

void
dsl_dataset_create_root(dsl_pool_t *dp, uint64_t *ddobjp, dmu_tx_t *tx)
{
	objset_t *mos = dp->dp_meta_objset;
	dmu_buf_t *dbuf;
	dsl_dataset_phys_t *dsphys;
	dsl_dataset_t *ds;
	uint64_t dsobj;
	dsl_dir_t *dd;

	dsl_dir_create_root(mos, ddobjp, tx);
	VERIFY(0 == dsl_dir_open_obj(dp, *ddobjp, NULL, FTAG, &dd));

	dsobj = dmu_object_alloc(mos, DMU_OT_DSL_DATASET, 0,
	    DMU_OT_DSL_DATASET, sizeof (dsl_dataset_phys_t), tx);
	VERIFY(0 == dmu_bonus_hold(mos, dsobj, FTAG, &dbuf));
	dmu_buf_will_dirty(dbuf, tx);
	dsphys = dbuf->db_data;
	dsphys->ds_dir_obj = dd->dd_object;
	dsphys->ds_fsid_guid = unique_create();
	(void) random_get_pseudo_bytes((void*)&dsphys->ds_guid,
	    sizeof (dsphys->ds_guid));
	dsphys->ds_snapnames_zapobj =
	    zap_create_norm(mos, U8_TEXTPREP_TOUPPER, DMU_OT_DSL_DS_SNAP_MAP,
	    DMU_OT_NONE, 0, tx);
	dsphys->ds_creation_time = gethrestime_sec();
	dsphys->ds_creation_txg = tx->tx_txg;
	dsphys->ds_deadlist_obj =
	    bplist_create(mos, DSL_DEADLIST_BLOCKSIZE, tx);
	if (spa_version(dp->dp_spa) >= SPA_VERSION_UNIQUE_ACCURATE)
		dsphys->ds_flags |= DS_FLAG_UNIQUE_ACCURATE;
	dmu_buf_rele(dbuf, FTAG);

	dmu_buf_will_dirty(dd->dd_dbuf, tx);
	dd->dd_phys->dd_head_dataset_obj = dsobj;
	dsl_dir_close(dd, FTAG);

	VERIFY(0 ==
	    dsl_dataset_open_obj(dp, dsobj, NULL, DS_MODE_NONE, FTAG, &ds));
	(void) dmu_objset_create_impl(dp->dp_spa, ds,
	    &ds->ds_phys->ds_bp, DMU_OST_ZFS, tx);
	dsl_dataset_close(ds, DS_MODE_NONE, FTAG);
}

uint64_t
dsl_dataset_create_sync_impl(dsl_dir_t *dd, dsl_dataset_t *origin,
    uint64_t flags, dmu_tx_t *tx)
{
	dsl_pool_t *dp = dd->dd_pool;
	dmu_buf_t *dbuf;
	dsl_dataset_phys_t *dsphys;
	uint64_t dsobj;
	objset_t *mos = dp->dp_meta_objset;

	ASSERT(origin == NULL || origin->ds_dir->dd_pool == dp);
	ASSERT(origin == NULL || origin->ds_phys->ds_num_children > 0);
	ASSERT(dmu_tx_is_syncing(tx));
	ASSERT(dd->dd_phys->dd_head_dataset_obj == 0);

	dsobj = dmu_object_alloc(mos, DMU_OT_DSL_DATASET, 0,
	    DMU_OT_DSL_DATASET, sizeof (dsl_dataset_phys_t), tx);
	VERIFY(0 == dmu_bonus_hold(mos, dsobj, FTAG, &dbuf));
	dmu_buf_will_dirty(dbuf, tx);
	dsphys = dbuf->db_data;
	dsphys->ds_dir_obj = dd->dd_object;
	dsphys->ds_flags = flags;
	dsphys->ds_fsid_guid = unique_create();
	(void) random_get_pseudo_bytes((void*)&dsphys->ds_guid,
	    sizeof (dsphys->ds_guid));
	dsphys->ds_snapnames_zapobj =
	    zap_create_norm(mos, U8_TEXTPREP_TOUPPER, DMU_OT_DSL_DS_SNAP_MAP,
	    DMU_OT_NONE, 0, tx);
	dsphys->ds_creation_time = gethrestime_sec();
	dsphys->ds_creation_txg = tx->tx_txg;
	dsphys->ds_deadlist_obj =
	    bplist_create(mos, DSL_DEADLIST_BLOCKSIZE, tx);

	if (origin) {
		dsphys->ds_prev_snap_obj = origin->ds_object;
		dsphys->ds_prev_snap_txg =
		    origin->ds_phys->ds_creation_txg;
		dsphys->ds_used_bytes =
		    origin->ds_phys->ds_used_bytes;
		dsphys->ds_compressed_bytes =
		    origin->ds_phys->ds_compressed_bytes;
		dsphys->ds_uncompressed_bytes =
		    origin->ds_phys->ds_uncompressed_bytes;
		dsphys->ds_bp = origin->ds_phys->ds_bp;
		dsphys->ds_flags |= origin->ds_phys->ds_flags;

		dmu_buf_will_dirty(origin->ds_dbuf, tx);
		origin->ds_phys->ds_num_children++;

		dmu_buf_will_dirty(dd->dd_dbuf, tx);
		dd->dd_phys->dd_origin_obj = origin->ds_object;
	}

	if (spa_version(dp->dp_spa) >= SPA_VERSION_UNIQUE_ACCURATE)
		dsphys->ds_flags |= DS_FLAG_UNIQUE_ACCURATE;

	dmu_buf_rele(dbuf, FTAG);

	dmu_buf_will_dirty(dd->dd_dbuf, tx);
	dd->dd_phys->dd_head_dataset_obj = dsobj;

	return (dsobj);
}

uint64_t
dsl_dataset_create_sync(dsl_dir_t *pdd, const char *lastname,
    dsl_dataset_t *origin, uint64_t flags, cred_t *cr, dmu_tx_t *tx)
{
	dsl_pool_t *dp = pdd->dd_pool;
	uint64_t dsobj, ddobj;
	dsl_dir_t *dd;

	ASSERT(lastname[0] != '@');

	ddobj = dsl_dir_create_sync(pdd, lastname, tx);
	VERIFY(0 == dsl_dir_open_obj(dp, ddobj, lastname, FTAG, &dd));

	dsobj = dsl_dataset_create_sync_impl(dd, origin, flags, tx);

	dsl_deleg_set_create_perms(dd, tx, cr);

	dsl_dir_close(dd, FTAG);

	return (dsobj);
}

struct destroyarg {
	dsl_sync_task_group_t *dstg;
	char *snapname;
	char *failed;
};

static int
dsl_snapshot_destroy_one(char *name, void *arg)
{
	struct destroyarg *da = arg;
	dsl_dataset_t *ds;
	char *cp;
	int err;

	(void) strcat(name, "@");
	(void) strcat(name, da->snapname);
	err = dsl_dataset_open(name,
	    DS_MODE_EXCLUSIVE | DS_MODE_READONLY | DS_MODE_INCONSISTENT,
	    da->dstg, &ds);
	cp = strchr(name, '@');
	*cp = '\0';
	if (err == ENOENT)
		return (0);
	if (err) {
		(void) strcpy(da->failed, name);
		return (err);
	}

	dsl_sync_task_create(da->dstg, dsl_dataset_destroy_check,
	    dsl_dataset_destroy_sync, ds, da->dstg, 0);
	return (0);
}

/*
 * Destroy 'snapname' in all descendants of 'fsname'.
 */
#pragma weak dmu_snapshots_destroy = dsl_snapshots_destroy
int
dsl_snapshots_destroy(char *fsname, char *snapname)
{
	int err;
	struct destroyarg da;
	dsl_sync_task_t *dst;
	spa_t *spa;

	err = spa_open(fsname, &spa, FTAG);
	if (err)
		return (err);
	da.dstg = dsl_sync_task_group_create(spa_get_dsl(spa));
	da.snapname = snapname;
	da.failed = fsname;

	err = dmu_objset_find(fsname,
	    dsl_snapshot_destroy_one, &da, DS_FIND_CHILDREN);

	if (err == 0)
		err = dsl_sync_task_group_wait(da.dstg);

	for (dst = list_head(&da.dstg->dstg_tasks); dst;
	    dst = list_next(&da.dstg->dstg_tasks, dst)) {
		dsl_dataset_t *ds = dst->dst_arg1;
		if (dst->dst_err) {
			dsl_dataset_name(ds, fsname);
			*strchr(fsname, '@') = '\0';
		}
		/*
		 * If it was successful, destroy_sync would have
		 * closed the ds
		 */
		if (err)
			dsl_dataset_close(ds, DS_MODE_EXCLUSIVE, da.dstg);
	}

	dsl_sync_task_group_destroy(da.dstg);
	spa_close(spa, FTAG);
	return (err);
}

/*
 * ds must be opened EXCLUSIVE or PRIMARY.  on return (whether
 * successful or not), ds will be closed and caller can no longer
 * dereference it.
 */
int
dsl_dataset_destroy(dsl_dataset_t *ds, void *tag)
{
	int err;
	dsl_sync_task_group_t *dstg;
	objset_t *os;
	dsl_dir_t *dd;
	uint64_t obj;

	if (ds->ds_open_refcount != DS_REF_MAX) {
		if (dsl_dataset_tryupgrade(ds, DS_MODE_PRIMARY,
		    DS_MODE_EXCLUSIVE) == 0) {
			dsl_dataset_close(ds, DS_MODE_PRIMARY, tag);
			return (EBUSY);
		}
	}

	if (dsl_dataset_is_snapshot(ds)) {
		/* Destroying a snapshot is simpler */
		err = dsl_sync_task_do(ds->ds_dir->dd_pool,
		    dsl_dataset_destroy_check, dsl_dataset_destroy_sync,
		    ds, tag, 0);
		goto out;
	}

	dd = ds->ds_dir;

	/*
	 * Check for errors and mark this ds as inconsistent, in
	 * case we crash while freeing the objects.
	 */
	err = dsl_sync_task_do(dd->dd_pool, dsl_dataset_destroy_begin_check,
	    dsl_dataset_destroy_begin_sync, ds, NULL, 0);
	if (err)
		goto out;

	err = dmu_objset_open_ds(ds, DMU_OST_ANY, &os);
	if (err)
		goto out;

	/*
	 * remove the objects in open context, so that we won't
	 * have too much to do in syncing context.
	 */
	for (obj = 0; err == 0; err = dmu_object_next(os, &obj, FALSE,
	    ds->ds_phys->ds_prev_snap_txg)) {
		dmu_tx_t *tx = dmu_tx_create(os);
		dmu_tx_hold_free(tx, obj, 0, DMU_OBJECT_END);
		dmu_tx_hold_bonus(tx, obj);
		err = dmu_tx_assign(tx, TXG_WAIT);
		if (err) {
			/*
			 * Perhaps there is not enough disk
			 * space.  Just deal with it from
			 * dsl_dataset_destroy_sync().
			 */
			dmu_tx_abort(tx);
			continue;
		}
		VERIFY(0 == dmu_object_free(os, obj, tx));
		dmu_tx_commit(tx);
	}
	/* Make sure it's not dirty before we finish destroying it. */
	txg_wait_synced(dd->dd_pool, 0);

	dmu_objset_close(os);
	if (err != ESRCH)
		goto out;

	if (ds->ds_user_ptr) {
		ds->ds_user_evict_func(ds, ds->ds_user_ptr);
		ds->ds_user_ptr = NULL;
	}

	rw_enter(&dd->dd_pool->dp_config_rwlock, RW_READER);
	err = dsl_dir_open_obj(dd->dd_pool, dd->dd_object, NULL, FTAG, &dd);
	rw_exit(&dd->dd_pool->dp_config_rwlock);

	if (err)
		goto out;

	/*
	 * Blow away the dsl_dir + head dataset.
	 */
	dstg = dsl_sync_task_group_create(ds->ds_dir->dd_pool);
	dsl_sync_task_create(dstg, dsl_dataset_destroy_check,
	    dsl_dataset_destroy_sync, ds, tag, 0);
	dsl_sync_task_create(dstg, dsl_dir_destroy_check,
	    dsl_dir_destroy_sync, dd, FTAG, 0);
	err = dsl_sync_task_group_wait(dstg);
	dsl_sync_task_group_destroy(dstg);
	/* if it is successful, *destroy_sync will close the ds+dd */
	if (err)
		dsl_dir_close(dd, FTAG);
out:
	if (err)
		dsl_dataset_close(ds, DS_MODE_EXCLUSIVE, tag);
	return (err);
}

int
dsl_dataset_rollback(dsl_dataset_t *ds, dmu_objset_type_t ost)
{
	ASSERT3U(ds->ds_open_refcount, ==, DS_REF_MAX);

	return (dsl_sync_task_do(ds->ds_dir->dd_pool,
	    dsl_dataset_rollback_check, dsl_dataset_rollback_sync,
	    ds, &ost, 0));
}

void *
dsl_dataset_set_user_ptr(dsl_dataset_t *ds,
    void *p, dsl_dataset_evict_func_t func)
{
	void *old;

	mutex_enter(&ds->ds_lock);
	old = ds->ds_user_ptr;
	if (old == NULL) {
		ds->ds_user_ptr = p;
		ds->ds_user_evict_func = func;
	}
	mutex_exit(&ds->ds_lock);
	return (old);
}

void *
dsl_dataset_get_user_ptr(dsl_dataset_t *ds)
{
	return (ds->ds_user_ptr);
}


blkptr_t *
dsl_dataset_get_blkptr(dsl_dataset_t *ds)
{
	return (&ds->ds_phys->ds_bp);
}

void
dsl_dataset_set_blkptr(dsl_dataset_t *ds, blkptr_t *bp, dmu_tx_t *tx)
{
	ASSERT(dmu_tx_is_syncing(tx));
	/* If it's the meta-objset, set dp_meta_rootbp */
	if (ds == NULL) {
		tx->tx_pool->dp_meta_rootbp = *bp;
	} else {
		dmu_buf_will_dirty(ds->ds_dbuf, tx);
		ds->ds_phys->ds_bp = *bp;
	}
}

spa_t *
dsl_dataset_get_spa(dsl_dataset_t *ds)
{
	return (ds->ds_dir->dd_pool->dp_spa);
}

void
dsl_dataset_dirty(dsl_dataset_t *ds, dmu_tx_t *tx)
{
	dsl_pool_t *dp;

	if (ds == NULL) /* this is the meta-objset */
		return;

	ASSERT(ds->ds_user_ptr != NULL);

	if (ds->ds_phys->ds_next_snap_obj != 0)
		panic("dirtying snapshot!");

	dp = ds->ds_dir->dd_pool;

	if (txg_list_add(&dp->dp_dirty_datasets, ds, tx->tx_txg) == 0) {
		/* up the hold count until we can be written out */
		dmu_buf_add_ref(ds->ds_dbuf, ds);
	}
}

/*
 * The unique space in the head dataset can be calculated by subtracting
 * the space used in the most recent snapshot, that is still being used
 * in this file system, from the space currently in use.  To figure out
 * the space in the most recent snapshot still in use, we need to take
 * the total space used in the snapshot and subtract out the space that
 * has been freed up since the snapshot was taken.
 */
static void
dsl_dataset_recalc_head_uniq(dsl_dataset_t *ds)
{
	uint64_t mrs_used;
	uint64_t dlused, dlcomp, dluncomp;

	ASSERT(ds->ds_object == ds->ds_dir->dd_phys->dd_head_dataset_obj);

	if (ds->ds_phys->ds_prev_snap_obj != 0)
		mrs_used = ds->ds_prev->ds_phys->ds_used_bytes;
	else
		mrs_used = 0;

	VERIFY(0 == bplist_space(&ds->ds_deadlist, &dlused, &dlcomp,
	    &dluncomp));

	ASSERT3U(dlused, <=, mrs_used);
	ds->ds_phys->ds_unique_bytes =
	    ds->ds_phys->ds_used_bytes - (mrs_used - dlused);

	if (!DS_UNIQUE_IS_ACCURATE(ds) &&
	    spa_version(ds->ds_dir->dd_pool->dp_spa) >=
	    SPA_VERSION_UNIQUE_ACCURATE)
		ds->ds_phys->ds_flags |= DS_FLAG_UNIQUE_ACCURATE;
}

static uint64_t
dsl_dataset_unique(dsl_dataset_t *ds)
{
	if (!DS_UNIQUE_IS_ACCURATE(ds) && !dsl_dataset_is_snapshot(ds))
		dsl_dataset_recalc_head_uniq(ds);

	return (ds->ds_phys->ds_unique_bytes);
}

struct killarg {
	int64_t *usedp;
	int64_t *compressedp;
	int64_t *uncompressedp;
	zio_t *zio;
	dmu_tx_t *tx;
};

static int
kill_blkptr(traverse_blk_cache_t *bc, spa_t *spa, void *arg)
{
	struct killarg *ka = arg;
	blkptr_t *bp = &bc->bc_blkptr;

	ASSERT3U(bc->bc_errno, ==, 0);

	/*
	 * Since this callback is not called concurrently, no lock is
	 * needed on the accounting values.
	 */
	*ka->usedp += bp_get_dasize(spa, bp);
	*ka->compressedp += BP_GET_PSIZE(bp);
	*ka->uncompressedp += BP_GET_UCSIZE(bp);
	/* XXX check for EIO? */
	(void) arc_free(ka->zio, spa, ka->tx->tx_txg, bp, NULL, NULL,
	    ARC_NOWAIT);
	return (0);
}

/* ARGSUSED */
static int
dsl_dataset_rollback_check(void *arg1, void *arg2, dmu_tx_t *tx)
{
	dsl_dataset_t *ds = arg1;
	dmu_objset_type_t *ost = arg2;

	/*
	 * We can only roll back to emptyness if it is a ZPL objset.
	 */
	if (*ost != DMU_OST_ZFS && ds->ds_phys->ds_prev_snap_txg == 0)
		return (EINVAL);

	/*
	 * This must not be a snapshot.
	 */
	if (ds->ds_phys->ds_next_snap_obj != 0)
		return (EINVAL);

	/*
	 * If we made changes this txg, traverse_dsl_dataset won't find
	 * them.  Try again.
	 */
	if (ds->ds_phys->ds_bp.blk_birth >= tx->tx_txg)
		return (EAGAIN);

	return (0);
}

/* ARGSUSED */
static void
dsl_dataset_rollback_sync(void *arg1, void *arg2, cred_t *cr, dmu_tx_t *tx)
{
	dsl_dataset_t *ds = arg1;
	dmu_objset_type_t *ost = arg2;
	objset_t *mos = ds->ds_dir->dd_pool->dp_meta_objset;

	dmu_buf_will_dirty(ds->ds_dbuf, tx);

	/*
	 * Before the roll back destroy the zil.
	 */
	if (ds->ds_user_ptr != NULL) {
		zil_rollback_destroy(
		    ((objset_impl_t *)ds->ds_user_ptr)->os_zil, tx);

		/*
		 * We need to make sure that the objset_impl_t is reopened after
		 * we do the rollback, otherwise it will have the wrong
		 * objset_phys_t.  Normally this would happen when this
		 * DS_MODE_EXCLUSIVE dataset-open is closed, thus causing the
		 * dataset to be immediately evicted.  But when doing "zfs recv
		 * -F", we reopen the objset before that, so that there is no
		 * window where the dataset is closed and inconsistent.
		 */
		ds->ds_user_evict_func(ds, ds->ds_user_ptr);
		ds->ds_user_ptr = NULL;
	}

	/* Zero out the deadlist. */
	bplist_close(&ds->ds_deadlist);
	bplist_destroy(mos, ds->ds_phys->ds_deadlist_obj, tx);
	ds->ds_phys->ds_deadlist_obj =
	    bplist_create(mos, DSL_DEADLIST_BLOCKSIZE, tx);
	VERIFY(0 == bplist_open(&ds->ds_deadlist, mos,
	    ds->ds_phys->ds_deadlist_obj));

	{
		/* Free blkptrs that we gave birth to */
		zio_t *zio;
		int64_t used = 0, compressed = 0, uncompressed = 0;
		struct killarg ka;
		int64_t delta;

		zio = zio_root(tx->tx_pool->dp_spa, NULL, NULL,
		    ZIO_FLAG_MUSTSUCCEED);
		ka.usedp = &used;
		ka.compressedp = &compressed;
		ka.uncompressedp = &uncompressed;
		ka.zio = zio;
		ka.tx = tx;
		(void) traverse_dsl_dataset(ds, ds->ds_phys->ds_prev_snap_txg,
		    ADVANCE_POST, kill_blkptr, &ka);
		(void) zio_wait(zio);

		/* only deduct space beyond any refreservation */
		delta = parent_delta(ds, -used);
		dsl_dir_diduse_space(ds->ds_dir,
		    delta, -compressed, -uncompressed, tx);
	}

	if (ds->ds_prev) {
		/* Change our contents to that of the prev snapshot */
		ASSERT3U(ds->ds_prev->ds_object, ==,
		    ds->ds_phys->ds_prev_snap_obj);
		ds->ds_phys->ds_bp = ds->ds_prev->ds_phys->ds_bp;
		ds->ds_phys->ds_used_bytes =
		    ds->ds_prev->ds_phys->ds_used_bytes;
		ds->ds_phys->ds_compressed_bytes =
		    ds->ds_prev->ds_phys->ds_compressed_bytes;
		ds->ds_phys->ds_uncompressed_bytes =
		    ds->ds_prev->ds_phys->ds_uncompressed_bytes;
		ds->ds_phys->ds_flags = ds->ds_prev->ds_phys->ds_flags;
		ds->ds_phys->ds_unique_bytes = 0;

		if (ds->ds_prev->ds_phys->ds_next_snap_obj == ds->ds_object) {
			dmu_buf_will_dirty(ds->ds_prev->ds_dbuf, tx);
			ds->ds_prev->ds_phys->ds_unique_bytes = 0;
		}
	} else {
		/* Zero out our contents, recreate objset */
		bzero(&ds->ds_phys->ds_bp, sizeof (blkptr_t));
		ds->ds_phys->ds_used_bytes = 0;
		ds->ds_phys->ds_compressed_bytes = 0;
		ds->ds_phys->ds_uncompressed_bytes = 0;
		ds->ds_phys->ds_flags = 0;
		ds->ds_phys->ds_unique_bytes = 0;
		(void) dmu_objset_create_impl(ds->ds_dir->dd_pool->dp_spa, ds,
		    &ds->ds_phys->ds_bp, *ost, tx);
	}

	spa_history_internal_log(LOG_DS_ROLLBACK, ds->ds_dir->dd_pool->dp_spa,
	    tx, cr, "dataset = %llu", ds->ds_object);
}

/* ARGSUSED */
static int
dsl_dataset_destroy_begin_check(void *arg1, void *arg2, dmu_tx_t *tx)
{
	dsl_dataset_t *ds = arg1;
	objset_t *mos = ds->ds_dir->dd_pool->dp_meta_objset;
	uint64_t count;
	int err;

	/*
	 * Can't delete a head dataset if there are snapshots of it.
	 * (Except if the only snapshots are from the branch we cloned
	 * from.)
	 */
	if (ds->ds_prev != NULL &&
	    ds->ds_prev->ds_phys->ds_next_snap_obj == ds->ds_object)
		return (EINVAL);

	/*
	 * This is really a dsl_dir thing, but check it here so that
	 * we'll be less likely to leave this dataset inconsistent &
	 * nearly destroyed.
	 */
	err = zap_count(mos, ds->ds_dir->dd_phys->dd_child_dir_zapobj, &count);
	if (err)
		return (err);
	if (count != 0)
		return (EEXIST);

	return (0);
}

/* ARGSUSED */
static void
dsl_dataset_destroy_begin_sync(void *arg1, void *arg2, cred_t *cr, dmu_tx_t *tx)
{
	dsl_dataset_t *ds = arg1;
	dsl_pool_t *dp = ds->ds_dir->dd_pool;

	/* Mark it as inconsistent on-disk, in case we crash */
	dmu_buf_will_dirty(ds->ds_dbuf, tx);
	ds->ds_phys->ds_flags |= DS_FLAG_INCONSISTENT;

	spa_history_internal_log(LOG_DS_DESTROY_BEGIN, dp->dp_spa, tx,
	    cr, "dataset = %llu", ds->ds_object);
}

/* ARGSUSED */
int
dsl_dataset_destroy_check(void *arg1, void *arg2, dmu_tx_t *tx)
{
	dsl_dataset_t *ds = arg1;

	/* Can't delete a branch point. */
	if (ds->ds_phys->ds_num_children > 1)
		return (EEXIST);

	/*
	 * Can't delete a head dataset if there are snapshots of it.
	 * (Except if the only snapshots are from the branch we cloned
	 * from.)
	 */
	if (ds->ds_prev != NULL &&
	    ds->ds_prev->ds_phys->ds_next_snap_obj == ds->ds_object)
		return (EINVAL);

	/*
	 * If we made changes this txg, traverse_dsl_dataset won't find
	 * them.  Try again.
	 */
	if (ds->ds_phys->ds_bp.blk_birth >= tx->tx_txg)
		return (EAGAIN);

	/* XXX we should do some i/o error checking... */
	return (0);
}

void
dsl_dataset_destroy_sync(void *arg1, void *tag, cred_t *cr, dmu_tx_t *tx)
{
	dsl_dataset_t *ds = arg1;
	int64_t used = 0, compressed = 0, uncompressed = 0;
	zio_t *zio;
	int err;
	int after_branch_point = FALSE;
	dsl_pool_t *dp = ds->ds_dir->dd_pool;
	objset_t *mos = dp->dp_meta_objset;
	dsl_dataset_t *ds_prev = NULL;
	uint64_t obj;

	ASSERT3U(ds->ds_open_refcount, ==, DS_REF_MAX);
	ASSERT3U(ds->ds_phys->ds_num_children, <=, 1);
	ASSERT(ds->ds_prev == NULL ||
	    ds->ds_prev->ds_phys->ds_next_snap_obj != ds->ds_object);
	ASSERT3U(ds->ds_phys->ds_bp.blk_birth, <=, tx->tx_txg);

	/* Remove our reservation */
	if (ds->ds_reserved != 0) {
		uint64_t val = 0;
		dsl_dataset_set_reservation_sync(ds, &val, cr, tx);
		ASSERT3U(ds->ds_reserved, ==, 0);
	}

	ASSERT(RW_WRITE_HELD(&dp->dp_config_rwlock));

	obj = ds->ds_object;

	if (ds->ds_phys->ds_prev_snap_obj != 0) {
		if (ds->ds_prev) {
			ds_prev = ds->ds_prev;
		} else {
			VERIFY(0 == dsl_dataset_open_obj(dp,
			    ds->ds_phys->ds_prev_snap_obj, NULL,
			    DS_MODE_NONE, FTAG, &ds_prev));
		}
		after_branch_point =
		    (ds_prev->ds_phys->ds_next_snap_obj != obj);

		dmu_buf_will_dirty(ds_prev->ds_dbuf, tx);
		if (after_branch_point &&
		    ds->ds_phys->ds_next_snap_obj == 0) {
			/* This clone is toast. */
			ASSERT(ds_prev->ds_phys->ds_num_children > 1);
			ds_prev->ds_phys->ds_num_children--;
		} else if (!after_branch_point) {
			ds_prev->ds_phys->ds_next_snap_obj =
			    ds->ds_phys->ds_next_snap_obj;
		}
	}

	zio = zio_root(dp->dp_spa, NULL, NULL, ZIO_FLAG_MUSTSUCCEED);

	if (ds->ds_phys->ds_next_snap_obj != 0) {
		blkptr_t bp;
		dsl_dataset_t *ds_next;
		uint64_t itor = 0;
		uint64_t old_unique;

		spa_scrub_restart(dp->dp_spa, tx->tx_txg);

		VERIFY(0 == dsl_dataset_open_obj(dp,
		    ds->ds_phys->ds_next_snap_obj, NULL,
		    DS_MODE_NONE, FTAG, &ds_next));
		ASSERT3U(ds_next->ds_phys->ds_prev_snap_obj, ==, obj);

		old_unique = dsl_dataset_unique(ds_next);

		dmu_buf_will_dirty(ds_next->ds_dbuf, tx);
		ds_next->ds_phys->ds_prev_snap_obj =
		    ds->ds_phys->ds_prev_snap_obj;
		ds_next->ds_phys->ds_prev_snap_txg =
		    ds->ds_phys->ds_prev_snap_txg;
		ASSERT3U(ds->ds_phys->ds_prev_snap_txg, ==,
		    ds_prev ? ds_prev->ds_phys->ds_creation_txg : 0);

		/*
		 * Transfer to our deadlist (which will become next's
		 * new deadlist) any entries from next's current
		 * deadlist which were born before prev, and free the
		 * other entries.
		 *
		 * XXX we're doing this long task with the config lock held
		 */
		while (bplist_iterate(&ds_next->ds_deadlist, &itor,
		    &bp) == 0) {
			if (bp.blk_birth <= ds->ds_phys->ds_prev_snap_txg) {
				VERIFY(0 == bplist_enqueue(&ds->ds_deadlist,
				    &bp, tx));
				if (ds_prev && !after_branch_point &&
				    bp.blk_birth >
				    ds_prev->ds_phys->ds_prev_snap_txg) {
					ds_prev->ds_phys->ds_unique_bytes +=
					    bp_get_dasize(dp->dp_spa, &bp);
				}
			} else {
				used += bp_get_dasize(dp->dp_spa, &bp);
				compressed += BP_GET_PSIZE(&bp);
				uncompressed += BP_GET_UCSIZE(&bp);
				/* XXX check return value? */
				(void) arc_free(zio, dp->dp_spa, tx->tx_txg,
				    &bp, NULL, NULL, ARC_NOWAIT);
			}
		}

		/* free next's deadlist */
		bplist_close(&ds_next->ds_deadlist);
		bplist_destroy(mos, ds_next->ds_phys->ds_deadlist_obj, tx);

		/* set next's deadlist to our deadlist */
		ds_next->ds_phys->ds_deadlist_obj =
		    ds->ds_phys->ds_deadlist_obj;
		VERIFY(0 == bplist_open(&ds_next->ds_deadlist, mos,
		    ds_next->ds_phys->ds_deadlist_obj));
		ds->ds_phys->ds_deadlist_obj = 0;

		if (ds_next->ds_phys->ds_next_snap_obj != 0) {
			/*
			 * Update next's unique to include blocks which
			 * were previously shared by only this snapshot
			 * and it.  Those blocks will be born after the
			 * prev snap and before this snap, and will have
			 * died after the next snap and before the one
			 * after that (ie. be on the snap after next's
			 * deadlist).
			 *
			 * XXX we're doing this long task with the
			 * config lock held
			 */
			dsl_dataset_t *ds_after_next;

			VERIFY(0 == dsl_dataset_open_obj(dp,
			    ds_next->ds_phys->ds_next_snap_obj, NULL,
			    DS_MODE_NONE, FTAG, &ds_after_next));
			itor = 0;
			while (bplist_iterate(&ds_after_next->ds_deadlist,
			    &itor, &bp) == 0) {
				if (bp.blk_birth >
				    ds->ds_phys->ds_prev_snap_txg &&
				    bp.blk_birth <=
				    ds->ds_phys->ds_creation_txg) {
					ds_next->ds_phys->ds_unique_bytes +=
					    bp_get_dasize(dp->dp_spa, &bp);
				}
			}

			dsl_dataset_close(ds_after_next, DS_MODE_NONE, FTAG);
			ASSERT3P(ds_next->ds_prev, ==, NULL);
		} else {
			ASSERT3P(ds_next->ds_prev, ==, ds);
			dsl_dataset_close(ds_next->ds_prev, DS_MODE_NONE,
			    ds_next);
			if (ds_prev) {
				VERIFY(0 == dsl_dataset_open_obj(dp,
				    ds->ds_phys->ds_prev_snap_obj, NULL,
				    DS_MODE_NONE, ds_next, &ds_next->ds_prev));
			} else {
				ds_next->ds_prev = NULL;
			}

			dsl_dataset_recalc_head_uniq(ds_next);

			/*
			 * Reduce the amount of our unconsmed refreservation
			 * being charged to our parent by the amount of
			 * new unique data we have gained.
			 */
			if (old_unique < ds_next->ds_reserved) {
				int64_t mrsdelta;
				uint64_t new_unique =
				    ds_next->ds_phys->ds_unique_bytes;

				ASSERT(old_unique <= new_unique);
				mrsdelta = MIN(new_unique - old_unique,
				    ds_next->ds_reserved - old_unique);
				dsl_dir_diduse_space(ds->ds_dir, -mrsdelta,
				    0, 0, tx);
			}
		}
		dsl_dataset_close(ds_next, DS_MODE_NONE, FTAG);

		/*
		 * NB: unique_bytes might not be accurate for the head objset.
		 * Before SPA_VERSION 9, we didn't update its value when we
		 * deleted the most recent snapshot.
		 */
		ASSERT3U(used, ==, ds->ds_phys->ds_unique_bytes);
	} else {
		/*
		 * There's no next snapshot, so this is a head dataset.
		 * Destroy the deadlist.  Unless it's a clone, the
		 * deadlist should be empty.  (If it's a clone, it's
		 * safe to ignore the deadlist contents.)
		 */
		struct killarg ka;

		ASSERT(after_branch_point || bplist_empty(&ds->ds_deadlist));
		bplist_close(&ds->ds_deadlist);
		bplist_destroy(mos, ds->ds_phys->ds_deadlist_obj, tx);
		ds->ds_phys->ds_deadlist_obj = 0;

		/*
		 * Free everything that we point to (that's born after
		 * the previous snapshot, if we are a clone)
		 *
		 * XXX we're doing this long task with the config lock held
		 */
		ka.usedp = &used;
		ka.compressedp = &compressed;
		ka.uncompressedp = &uncompressed;
		ka.zio = zio;
		ka.tx = tx;
		err = traverse_dsl_dataset(ds, ds->ds_phys->ds_prev_snap_txg,
		    ADVANCE_POST, kill_blkptr, &ka);
		ASSERT3U(err, ==, 0);
		ASSERT(spa_version(dp->dp_spa) <
		    SPA_VERSION_UNIQUE_ACCURATE ||
		    used == ds->ds_phys->ds_unique_bytes);
	}

	err = zio_wait(zio);
	ASSERT3U(err, ==, 0);

	dsl_dir_diduse_space(ds->ds_dir, -used, -compressed, -uncompressed, tx);

	if (ds->ds_phys->ds_snapnames_zapobj) {
		err = zap_destroy(mos, ds->ds_phys->ds_snapnames_zapobj, tx);
		ASSERT(err == 0);
	}

	if (ds->ds_dir->dd_phys->dd_head_dataset_obj == ds->ds_object) {
		/* Erase the link in the dataset */
		dmu_buf_will_dirty(ds->ds_dir->dd_dbuf, tx);
		ds->ds_dir->dd_phys->dd_head_dataset_obj = 0;
		/*
		 * dsl_dir_sync_destroy() called us, they'll destroy
		 * the dataset.
		 */
	} else {
		/* remove from snapshot namespace */
		dsl_dataset_t *ds_head;
		VERIFY(0 == dsl_dataset_open_obj(dp,
		    ds->ds_dir->dd_phys->dd_head_dataset_obj, NULL,
		    DS_MODE_NONE, FTAG, &ds_head));
		VERIFY(0 == dsl_dataset_get_snapname(ds));
#ifdef ZFS_DEBUG
		{
			uint64_t val;

			err = dsl_dataset_snap_lookup(mos,
			    ds_head->ds_phys->ds_flags,
			    ds_head->ds_phys->ds_snapnames_zapobj,
			    ds->ds_snapname, &val);
			ASSERT3U(err, ==, 0);
			ASSERT3U(val, ==, obj);
		}
#endif
		err = dsl_dataset_snap_remove(mos,
		    ds_head->ds_phys->ds_flags,
		    ds_head->ds_phys->ds_snapnames_zapobj,
		    ds->ds_snapname, tx);
		ASSERT(err == 0);
		dsl_dataset_close(ds_head, DS_MODE_NONE, FTAG);
	}

	if (ds_prev && ds->ds_prev != ds_prev)
		dsl_dataset_close(ds_prev, DS_MODE_NONE, FTAG);

	spa_prop_clear_bootfs(dp->dp_spa, ds->ds_object, tx);
	spa_history_internal_log(LOG_DS_DESTROY, dp->dp_spa, tx,
	    cr, "dataset = %llu", ds->ds_object);

	dsl_dataset_close(ds, DS_MODE_EXCLUSIVE, tag);
	VERIFY(0 == dmu_object_free(mos, obj, tx));

}

static int
dsl_dataset_snapshot_reserve_space(dsl_dataset_t *ds, dmu_tx_t *tx)
{
	uint64_t asize;

	if (!dmu_tx_is_syncing(tx))
		return (0);

	/*
	 * If there's an fs-only reservation, any blocks that might become
	 * owned by the snapshot dataset must be accommodated by space
	 * outside of the reservation.
	 */
	asize = MIN(dsl_dataset_unique(ds), ds->ds_reserved);
	if (asize > dsl_dir_space_available(ds->ds_dir, NULL, 0, FALSE))
		return (ENOSPC);

	/*
	 * Propogate any reserved space for this snapshot to other
	 * snapshot checks in this sync group.
	 */
	if (asize > 0)
		dsl_dir_willuse_space(ds->ds_dir, asize, tx);

	return (0);
}

/* ARGSUSED */
int
dsl_dataset_snapshot_check(void *arg1, void *arg2, dmu_tx_t *tx)
{
	dsl_dataset_t *ds = arg1;
	const char *snapname = arg2;
	objset_t *mos = ds->ds_dir->dd_pool->dp_meta_objset;
	int err;
	uint64_t value;

	/*
	 * We don't allow multiple snapshots of the same txg.  If there
	 * is already one, try again.
	 */
	if (ds->ds_phys->ds_prev_snap_txg >= tx->tx_txg)
		return (EAGAIN);

	/*
	 * Check for conflicting name snapshot name.
	 */
	err = dsl_dataset_snap_lookup(mos, ds->ds_phys->ds_flags,
	    ds->ds_phys->ds_snapnames_zapobj, snapname, &value);
	if (err == 0)
		return (EEXIST);
	if (err != ENOENT)
		return (err);

	/*
	 * Check that the dataset's name is not too long.  Name consists
	 * of the dataset's length + 1 for the @-sign + snapshot name's length
	 */
	if (dsl_dataset_namelen(ds) + 1 + strlen(snapname) >= MAXNAMELEN)
		return (ENAMETOOLONG);

	err = dsl_dataset_snapshot_reserve_space(ds, tx);
	if (err)
		return (err);

	ds->ds_trysnap_txg = tx->tx_txg;
	return (0);
}

void
dsl_dataset_snapshot_sync(void *arg1, void *arg2, cred_t *cr, dmu_tx_t *tx)
{
	dsl_dataset_t *ds = arg1;
	const char *snapname = arg2;
	dsl_pool_t *dp = ds->ds_dir->dd_pool;
	dmu_buf_t *dbuf;
	dsl_dataset_phys_t *dsphys;
	uint64_t dsobj;
	objset_t *mos = dp->dp_meta_objset;
	int err;

	spa_scrub_restart(dp->dp_spa, tx->tx_txg);
	ASSERT(RW_WRITE_HELD(&dp->dp_config_rwlock));

	dsobj = dmu_object_alloc(mos, DMU_OT_DSL_DATASET, 0,
	    DMU_OT_DSL_DATASET, sizeof (dsl_dataset_phys_t), tx);
	VERIFY(0 == dmu_bonus_hold(mos, dsobj, FTAG, &dbuf));
	dmu_buf_will_dirty(dbuf, tx);
	dsphys = dbuf->db_data;
	dsphys->ds_dir_obj = ds->ds_dir->dd_object;
	dsphys->ds_fsid_guid = unique_create();
	(void) random_get_pseudo_bytes((void*)&dsphys->ds_guid,
	    sizeof (dsphys->ds_guid));
	dsphys->ds_prev_snap_obj = ds->ds_phys->ds_prev_snap_obj;
	dsphys->ds_prev_snap_txg = ds->ds_phys->ds_prev_snap_txg;
	dsphys->ds_next_snap_obj = ds->ds_object;
	dsphys->ds_num_children = 1;
	dsphys->ds_creation_time = gethrestime_sec();
	dsphys->ds_creation_txg = tx->tx_txg;
	dsphys->ds_deadlist_obj = ds->ds_phys->ds_deadlist_obj;
	dsphys->ds_used_bytes = ds->ds_phys->ds_used_bytes;
	dsphys->ds_compressed_bytes = ds->ds_phys->ds_compressed_bytes;
	dsphys->ds_uncompressed_bytes = ds->ds_phys->ds_uncompressed_bytes;
	dsphys->ds_flags = ds->ds_phys->ds_flags;
	dsphys->ds_bp = ds->ds_phys->ds_bp;
	dmu_buf_rele(dbuf, FTAG);

	ASSERT3U(ds->ds_prev != 0, ==, ds->ds_phys->ds_prev_snap_obj != 0);
	if (ds->ds_prev) {
		ASSERT(ds->ds_prev->ds_phys->ds_next_snap_obj ==
		    ds->ds_object ||
		    ds->ds_prev->ds_phys->ds_num_children > 1);
		if (ds->ds_prev->ds_phys->ds_next_snap_obj == ds->ds_object) {
			dmu_buf_will_dirty(ds->ds_prev->ds_dbuf, tx);
			ASSERT3U(ds->ds_phys->ds_prev_snap_txg, ==,
			    ds->ds_prev->ds_phys->ds_creation_txg);
			ds->ds_prev->ds_phys->ds_next_snap_obj = dsobj;
		}
	}

	/*
	 * If we have a reference-reservation on this dataset, we will
	 * need to increase the amount of refreservation being charged
	 * since our unique space is going to zero.
	 */
	if (ds->ds_reserved) {
		int64_t add = MIN(dsl_dataset_unique(ds), ds->ds_reserved);
		dsl_dir_diduse_space(ds->ds_dir, add, 0, 0, tx);
	}

	bplist_close(&ds->ds_deadlist);
	dmu_buf_will_dirty(ds->ds_dbuf, tx);
	ASSERT3U(ds->ds_phys->ds_prev_snap_txg, <, tx->tx_txg);
	ds->ds_phys->ds_prev_snap_obj = dsobj;
	ds->ds_phys->ds_prev_snap_txg = tx->tx_txg;
	ds->ds_phys->ds_unique_bytes = 0;
	if (spa_version(dp->dp_spa) >= SPA_VERSION_UNIQUE_ACCURATE)
		ds->ds_phys->ds_flags |= DS_FLAG_UNIQUE_ACCURATE;
	ds->ds_phys->ds_deadlist_obj =
	    bplist_create(mos, DSL_DEADLIST_BLOCKSIZE, tx);
	VERIFY(0 == bplist_open(&ds->ds_deadlist, mos,
	    ds->ds_phys->ds_deadlist_obj));

	dprintf("snap '%s' -> obj %llu\n", snapname, dsobj);
	err = zap_add(mos, ds->ds_phys->ds_snapnames_zapobj,
	    snapname, 8, 1, &dsobj, tx);
	ASSERT(err == 0);

	if (ds->ds_prev)
		dsl_dataset_close(ds->ds_prev, DS_MODE_NONE, ds);
	VERIFY(0 == dsl_dataset_open_obj(dp,
	    ds->ds_phys->ds_prev_snap_obj, snapname,
	    DS_MODE_NONE, ds, &ds->ds_prev));

	spa_history_internal_log(LOG_DS_SNAPSHOT, dp->dp_spa, tx, cr,
	    "dataset = %llu", dsobj);
}

void
dsl_dataset_sync(dsl_dataset_t *ds, zio_t *zio, dmu_tx_t *tx)
{
	ASSERT(dmu_tx_is_syncing(tx));
	ASSERT(ds->ds_user_ptr != NULL);
	ASSERT(ds->ds_phys->ds_next_snap_obj == 0);

	/*
	 * in case we had to change ds_fsid_guid when we opened it,
	 * sync it out now.
	 */
	dmu_buf_will_dirty(ds->ds_dbuf, tx);
	ds->ds_phys->ds_fsid_guid = ds->ds_fsid_guid;

	dsl_dir_dirty(ds->ds_dir, tx);
	dmu_objset_sync(ds->ds_user_ptr, zio, tx);
}

void
dsl_dataset_stats(dsl_dataset_t *ds, nvlist_t *nv)
{
	uint64_t refd, avail, uobjs, aobjs;

	dsl_dir_stats(ds->ds_dir, nv);

	dsl_dataset_space(ds, &refd, &avail, &uobjs, &aobjs);
	dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_AVAILABLE, avail);
	dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_REFERENCED, refd);

	dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_CREATION,
	    ds->ds_phys->ds_creation_time);
	dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_CREATETXG,
	    ds->ds_phys->ds_creation_txg);
	dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_REFQUOTA,
	    ds->ds_quota);
	dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_REFRESERVATION,
	    ds->ds_reserved);

	if (ds->ds_phys->ds_next_snap_obj) {
		/*
		 * This is a snapshot; override the dd's space used with
		 * our unique space and compression ratio.
		 */
		dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_USED,
		    ds->ds_phys->ds_unique_bytes);
		dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_COMPRESSRATIO,
		    ds->ds_phys->ds_compressed_bytes == 0 ? 100 :
		    (ds->ds_phys->ds_uncompressed_bytes * 100 /
		    ds->ds_phys->ds_compressed_bytes));
	}
}

void
dsl_dataset_fast_stat(dsl_dataset_t *ds, dmu_objset_stats_t *stat)
{
	stat->dds_creation_txg = ds->ds_phys->ds_creation_txg;
	stat->dds_inconsistent = ds->ds_phys->ds_flags & DS_FLAG_INCONSISTENT;
	stat->dds_guid = ds->ds_phys->ds_guid;
	if (ds->ds_phys->ds_next_snap_obj) {
		stat->dds_is_snapshot = B_TRUE;
		stat->dds_num_clones = ds->ds_phys->ds_num_children - 1;
	}

	/* clone origin is really a dsl_dir thing... */
	rw_enter(&ds->ds_dir->dd_pool->dp_config_rwlock, RW_READER);
	if (ds->ds_dir->dd_phys->dd_origin_obj) {
		dsl_dataset_t *ods;

		VERIFY(0 == dsl_dataset_open_obj(ds->ds_dir->dd_pool,
		    ds->ds_dir->dd_phys->dd_origin_obj,
		    NULL, DS_MODE_NONE, FTAG, &ods));
		dsl_dataset_name(ods, stat->dds_origin);
		dsl_dataset_close(ods, DS_MODE_NONE, FTAG);
	}
	rw_exit(&ds->ds_dir->dd_pool->dp_config_rwlock);
}

uint64_t
dsl_dataset_fsid_guid(dsl_dataset_t *ds)
{
	return (ds->ds_fsid_guid);
}

void
dsl_dataset_space(dsl_dataset_t *ds,
    uint64_t *refdbytesp, uint64_t *availbytesp,
    uint64_t *usedobjsp, uint64_t *availobjsp)
{
	*refdbytesp = ds->ds_phys->ds_used_bytes;
	*availbytesp = dsl_dir_space_available(ds->ds_dir, NULL, 0, TRUE);
	if (ds->ds_reserved > ds->ds_phys->ds_unique_bytes)
		*availbytesp += ds->ds_reserved - ds->ds_phys->ds_unique_bytes;
	if (ds->ds_quota != 0) {
		/*
		 * Adjust available bytes according to refquota
		 */
		if (*refdbytesp < ds->ds_quota)
			*availbytesp = MIN(*availbytesp,
			    ds->ds_quota - *refdbytesp);
		else
			*availbytesp = 0;
	}
	*usedobjsp = ds->ds_phys->ds_bp.blk_fill;
	*availobjsp = DN_MAX_OBJECT - *usedobjsp;
}

boolean_t
dsl_dataset_modified_since_lastsnap(dsl_dataset_t *ds)
{
	dsl_pool_t *dp = ds->ds_dir->dd_pool;

	ASSERT(RW_LOCK_HELD(&dp->dp_config_rwlock) ||
	    dsl_pool_sync_context(dp));
	if (ds->ds_prev == NULL)
		return (B_FALSE);
	if (ds->ds_phys->ds_bp.blk_birth >
	    ds->ds_prev->ds_phys->ds_creation_txg)
		return (B_TRUE);
	return (B_FALSE);
}

/* ARGSUSED */
static int
dsl_dataset_snapshot_rename_check(void *arg1, void *arg2, dmu_tx_t *tx)
{
	dsl_dataset_t *ds = arg1;
	char *newsnapname = arg2;
	dsl_dir_t *dd = ds->ds_dir;
	objset_t *mos = dd->dd_pool->dp_meta_objset;
	dsl_dataset_t *hds;
	uint64_t val;
	int err;

	err = dsl_dataset_open_obj(dd->dd_pool,
	    dd->dd_phys->dd_head_dataset_obj, NULL, DS_MODE_NONE, FTAG, &hds);
	if (err)
		return (err);

	/* new name better not be in use */
	err = dsl_dataset_snap_lookup(mos, hds->ds_phys->ds_flags,
	    hds->ds_phys->ds_snapnames_zapobj, newsnapname, &val);
	dsl_dataset_close(hds, DS_MODE_NONE, FTAG);

	if (err == 0)
		err = EEXIST;
	else if (err == ENOENT)
		err = 0;

	/* dataset name + 1 for the "@" + the new snapshot name must fit */
	if (dsl_dir_namelen(ds->ds_dir) + 1 + strlen(newsnapname) >= MAXNAMELEN)
		err = ENAMETOOLONG;

	return (err);
}

static void
dsl_dataset_snapshot_rename_sync(void *arg1, void *arg2,
    cred_t *cr, dmu_tx_t *tx)
{
	dsl_dataset_t *ds = arg1;
	const char *newsnapname = arg2;
	dsl_dir_t *dd = ds->ds_dir;
	objset_t *mos = dd->dd_pool->dp_meta_objset;
	dsl_dataset_t *hds;
	int err;

	ASSERT(ds->ds_phys->ds_next_snap_obj != 0);

	VERIFY(0 == dsl_dataset_open_obj(dd->dd_pool,
	    dd->dd_phys->dd_head_dataset_obj, NULL, DS_MODE_NONE, FTAG, &hds));

	VERIFY(0 == dsl_dataset_get_snapname(ds));
	err = dsl_dataset_snap_remove(mos, hds->ds_phys->ds_flags,
	    hds->ds_phys->ds_snapnames_zapobj, ds->ds_snapname, tx);
	ASSERT3U(err, ==, 0);
	mutex_enter(&ds->ds_lock);
	(void) strcpy(ds->ds_snapname, newsnapname);
	mutex_exit(&ds->ds_lock);
	err = zap_add(mos, hds->ds_phys->ds_snapnames_zapobj,
	    ds->ds_snapname, 8, 1, &ds->ds_object, tx);
	ASSERT3U(err, ==, 0);

	spa_history_internal_log(LOG_DS_RENAME, dd->dd_pool->dp_spa, tx,
	    cr, "dataset = %llu", ds->ds_object);
	dsl_dataset_close(hds, DS_MODE_NONE, FTAG);
}

struct renamesnaparg {
	dsl_sync_task_group_t *dstg;
	char failed[MAXPATHLEN];
	char *oldsnap;
	char *newsnap;
};

static int
dsl_snapshot_rename_one(char *name, void *arg)
{
	struct renamesnaparg *ra = arg;
	dsl_dataset_t *ds = NULL;
	char *cp;
	int err;

	cp = name + strlen(name);
	*cp = '@';
	(void) strcpy(cp + 1, ra->oldsnap);

	/*
	 * For recursive snapshot renames the parent won't be changing
	 * so we just pass name for both the to/from argument.
	 */
	if (err = zfs_secpolicy_rename_perms(name, name, CRED())) {
		(void) strcpy(ra->failed, name);
		return (err);
	}

	err = dsl_dataset_open(name, DS_MODE_READONLY | DS_MODE_STANDARD,
	    ra->dstg, &ds);
	if (err == ENOENT) {
		*cp = '\0';
		return (0);
	}
	if (err) {
		(void) strcpy(ra->failed, name);
		*cp = '\0';
		dsl_dataset_close(ds, DS_MODE_STANDARD, ra->dstg);
		return (err);
	}

#ifdef _KERNEL
	/* for all filesystems undergoing rename, we'll need to unmount it */
	(void) zfs_unmount_snap(name, NULL);
#endif

	*cp = '\0';

	dsl_sync_task_create(ra->dstg, dsl_dataset_snapshot_rename_check,
	    dsl_dataset_snapshot_rename_sync, ds, ra->newsnap, 0);

	return (0);
}

static int
dsl_recursive_rename(char *oldname, const char *newname)
{
	int err;
	struct renamesnaparg *ra;
	dsl_sync_task_t *dst;
	spa_t *spa;
	char *cp, *fsname = spa_strdup(oldname);
	int len = strlen(oldname);

	/* truncate the snapshot name to get the fsname */
	cp = strchr(fsname, '@');
	*cp = '\0';

	err = spa_open(fsname, &spa, FTAG);
	if (err) {
		kmem_free(fsname, len + 1);
		return (err);
	}
	ra = kmem_alloc(sizeof (struct renamesnaparg), KM_SLEEP);
	ra->dstg = dsl_sync_task_group_create(spa_get_dsl(spa));

	ra->oldsnap = strchr(oldname, '@') + 1;
	ra->newsnap = strchr(newname, '@') + 1;
	*ra->failed = '\0';

	err = dmu_objset_find(fsname, dsl_snapshot_rename_one, ra,
	    DS_FIND_CHILDREN);
	kmem_free(fsname, len + 1);

	if (err == 0) {
		err = dsl_sync_task_group_wait(ra->dstg);
	}

	for (dst = list_head(&ra->dstg->dstg_tasks); dst;
	    dst = list_next(&ra->dstg->dstg_tasks, dst)) {
		dsl_dataset_t *ds = dst->dst_arg1;
		if (dst->dst_err) {
			dsl_dir_name(ds->ds_dir, ra->failed);
			(void) strcat(ra->failed, "@");
			(void) strcat(ra->failed, ra->newsnap);
		}
		dsl_dataset_close(ds, DS_MODE_STANDARD, ra->dstg);
	}

	if (err)
		(void) strcpy(oldname, ra->failed);

	dsl_sync_task_group_destroy(ra->dstg);
	kmem_free(ra, sizeof (struct renamesnaparg));
	spa_close(spa, FTAG);
	return (err);
}

static int
dsl_valid_rename(char *oldname, void *arg)
{
	int delta = *(int *)arg;

	if (strlen(oldname) + delta >= MAXNAMELEN)
		return (ENAMETOOLONG);

	return (0);
}

#pragma weak dmu_objset_rename = dsl_dataset_rename
int
dsl_dataset_rename(char *oldname, const char *newname,
    boolean_t recursive)
{
	dsl_dir_t *dd;
	dsl_dataset_t *ds;
	const char *tail;
	int err;

	err = dsl_dir_open(oldname, FTAG, &dd, &tail);
	if (err)
		return (err);
	if (tail == NULL) {
		int delta = strlen(newname) - strlen(oldname);

		/* if we're growing, validate child size lengths */
		if (delta > 0)
			err = dmu_objset_find(oldname, dsl_valid_rename,
			    &delta, DS_FIND_CHILDREN | DS_FIND_SNAPSHOTS);

		if (!err)
			err = dsl_dir_rename(dd, newname);
		dsl_dir_close(dd, FTAG);
		return (err);
	}
	if (tail[0] != '@') {
		/* the name ended in a nonexistant component */
		dsl_dir_close(dd, FTAG);
		return (ENOENT);
	}

	dsl_dir_close(dd, FTAG);

	/* new name must be snapshot in same filesystem */
	tail = strchr(newname, '@');
	if (tail == NULL)
		return (EINVAL);
	tail++;
	if (strncmp(oldname, newname, tail - newname) != 0)
		return (EXDEV);

	if (recursive) {
		err = dsl_recursive_rename(oldname, newname);
	} else {
		err = dsl_dataset_open(oldname,
		    DS_MODE_READONLY | DS_MODE_STANDARD, FTAG, &ds);
		if (err)
			return (err);

		err = dsl_sync_task_do(ds->ds_dir->dd_pool,
		    dsl_dataset_snapshot_rename_check,
		    dsl_dataset_snapshot_rename_sync, ds, (char *)tail, 1);

		dsl_dataset_close(ds, DS_MODE_STANDARD, FTAG);
	}

	return (err);
}

struct promotearg {
	uint64_t used, comp, uncomp, unique;
	uint64_t ds_flags, newnext_obj, snapnames_obj;
};

/* ARGSUSED */
static int
dsl_dataset_promote_check(void *arg1, void *arg2, dmu_tx_t *tx)
{
	dsl_dataset_t *hds = arg1;
	struct promotearg *pa = arg2;
	dsl_dir_t *dd = hds->ds_dir;
	dsl_pool_t *dp = hds->ds_dir->dd_pool;
	dsl_dir_t *odd = NULL;
	dsl_dataset_t *ds = NULL;
	dsl_dataset_t *origin_ds = NULL;
	dsl_dataset_t *newnext_ds = NULL;
	int err;
	char *name = NULL;
	uint64_t itor = 0;
	blkptr_t bp;

	bzero(pa, sizeof (*pa));

	/* Check that it is a clone */
	if (dd->dd_phys->dd_origin_obj == 0)
		return (EINVAL);

	/* Since this is so expensive, don't do the preliminary check */
	if (!dmu_tx_is_syncing(tx))
		return (0);

	if (err = dsl_dataset_open_obj(dp, dd->dd_phys->dd_origin_obj,
	    NULL, DS_MODE_EXCLUSIVE, FTAG, &origin_ds))
		goto out;
	odd = origin_ds->ds_dir;

	{
		dsl_dataset_t *phds;
		if (err = dsl_dataset_open_obj(dd->dd_pool,
		    odd->dd_phys->dd_head_dataset_obj,
		    NULL, DS_MODE_NONE, FTAG, &phds))
			goto out;
		pa->ds_flags = phds->ds_phys->ds_flags;
		pa->snapnames_obj = phds->ds_phys->ds_snapnames_zapobj;
		dsl_dataset_close(phds, DS_MODE_NONE, FTAG);
	}

	if (hds->ds_phys->ds_flags & DS_FLAG_NOPROMOTE) {
		err = EXDEV;
		goto out;
	}

	/* find origin's new next ds */
	VERIFY(0 == dsl_dataset_open_obj(dd->dd_pool, hds->ds_object,
	    NULL, DS_MODE_NONE, FTAG, &newnext_ds));
	while (newnext_ds->ds_phys->ds_prev_snap_obj != origin_ds->ds_object) {
		dsl_dataset_t *prev;

		if (err = dsl_dataset_open_obj(dd->dd_pool,
		    newnext_ds->ds_phys->ds_prev_snap_obj,
		    NULL, DS_MODE_NONE, FTAG, &prev))
			goto out;
		dsl_dataset_close(newnext_ds, DS_MODE_NONE, FTAG);
		newnext_ds = prev;
	}
	pa->newnext_obj = newnext_ds->ds_object;

	/* compute origin's new unique space */
	while ((err = bplist_iterate(&newnext_ds->ds_deadlist,
	    &itor, &bp)) == 0) {
		if (bp.blk_birth > origin_ds->ds_phys->ds_prev_snap_txg)
			pa->unique += bp_get_dasize(dd->dd_pool->dp_spa, &bp);
	}
	if (err != ENOENT)
		goto out;

	/* Walk the snapshots that we are moving */
	name = kmem_alloc(MAXPATHLEN, KM_SLEEP);
	ds = origin_ds;
	/* CONSTCOND */
	while (TRUE) {
		uint64_t val, dlused, dlcomp, dluncomp;
		dsl_dataset_t *prev;

		/* Check that the snapshot name does not conflict */
		dsl_dataset_name(ds, name);
		err = dsl_dataset_snap_lookup(dd->dd_pool->dp_meta_objset,
		    hds->ds_phys->ds_flags, hds->ds_phys->ds_snapnames_zapobj,
		    ds->ds_snapname, &val);
		if (err != ENOENT) {
			if (err == 0)
				err = EEXIST;
			goto out;
		}

		/*
		 * compute space to transfer.  Each snapshot gave birth to:
		 * (my used) - (prev's used) + (deadlist's used)
		 */
		pa->used += ds->ds_phys->ds_used_bytes;
		pa->comp += ds->ds_phys->ds_compressed_bytes;
		pa->uncomp += ds->ds_phys->ds_uncompressed_bytes;

		/* If we reach the first snapshot, we're done. */
		if (ds->ds_phys->ds_prev_snap_obj == 0)
			break;

		if (err = bplist_space(&ds->ds_deadlist,
		    &dlused, &dlcomp, &dluncomp))
			goto out;
		if (err = dsl_dataset_open_obj(dd->dd_pool,
		    ds->ds_phys->ds_prev_snap_obj, NULL, DS_MODE_EXCLUSIVE,
		    FTAG, &prev))
			goto out;
		pa->used += dlused - prev->ds_phys->ds_used_bytes;
		pa->comp += dlcomp - prev->ds_phys->ds_compressed_bytes;
		pa->uncomp += dluncomp - prev->ds_phys->ds_uncompressed_bytes;

		/*
		 * We could be a clone of a clone.  If we reach our
		 * parent's branch point, we're done.
		 */
		if (prev->ds_phys->ds_next_snap_obj != ds->ds_object) {
			dsl_dataset_close(prev, DS_MODE_EXCLUSIVE, FTAG);
			break;
		}
		if (ds != origin_ds)
			dsl_dataset_close(ds, DS_MODE_EXCLUSIVE, FTAG);
		ds = prev;
	}

	/* Check that there is enough space here */
	err = dsl_dir_transfer_possible(odd, dd, pa->used);

out:
	if (ds && ds != origin_ds)
		dsl_dataset_close(ds, DS_MODE_EXCLUSIVE, FTAG);
	if (origin_ds)
		dsl_dataset_close(origin_ds, DS_MODE_EXCLUSIVE, FTAG);
	if (newnext_ds)
		dsl_dataset_close(newnext_ds, DS_MODE_NONE, FTAG);
	if (name)
		kmem_free(name, MAXPATHLEN);
	return (err);
}

static void
dsl_dataset_promote_sync(void *arg1, void *arg2, cred_t *cr, dmu_tx_t *tx)
{
	dsl_dataset_t *hds = arg1;
	struct promotearg *pa = arg2;
	dsl_dir_t *dd = hds->ds_dir;
	dsl_pool_t *dp = hds->ds_dir->dd_pool;
	dsl_dir_t *odd = NULL;
	dsl_dataset_t *ds, *origin_ds;
	char *name;

	ASSERT(dd->dd_phys->dd_origin_obj != 0);
	ASSERT(0 == (hds->ds_phys->ds_flags & DS_FLAG_NOPROMOTE));

	VERIFY(0 == dsl_dataset_open_obj(dp, dd->dd_phys->dd_origin_obj,
	    NULL, DS_MODE_EXCLUSIVE, FTAG, &origin_ds));
	/*
	 * We need to explicitly open odd, since origin_ds's dd will be
	 * changing.
	 */
	VERIFY(0 == dsl_dir_open_obj(dp, origin_ds->ds_dir->dd_object,
	    NULL, FTAG, &odd));

	/* move snapshots to this dir */
	name = kmem_alloc(MAXPATHLEN, KM_SLEEP);
	ds = origin_ds;
	/* CONSTCOND */
	while (TRUE) {
		dsl_dataset_t *prev;

		/* move snap name entry */
		dsl_dataset_name(ds, name);
		VERIFY(0 == dsl_dataset_snap_remove(dp->dp_meta_objset,
		    pa->ds_flags, pa->snapnames_obj, ds->ds_snapname, tx));
		VERIFY(0 == zap_add(dp->dp_meta_objset,
		    hds->ds_phys->ds_snapnames_zapobj, ds->ds_snapname,
		    8, 1, &ds->ds_object, tx));

		/* change containing dsl_dir */
		dmu_buf_will_dirty(ds->ds_dbuf, tx);
		ASSERT3U(ds->ds_phys->ds_dir_obj, ==, odd->dd_object);
		ds->ds_phys->ds_dir_obj = dd->dd_object;
		ASSERT3P(ds->ds_dir, ==, odd);
		dsl_dir_close(ds->ds_dir, ds);
		VERIFY(0 == dsl_dir_open_obj(dp, dd->dd_object,
		    NULL, ds, &ds->ds_dir));

		ASSERT3U(dsl_prop_numcb(ds), ==, 0);

		if (ds->ds_phys->ds_prev_snap_obj == 0)
			break;

		VERIFY(0 == dsl_dataset_open_obj(dp,
		    ds->ds_phys->ds_prev_snap_obj, NULL, DS_MODE_EXCLUSIVE,
		    FTAG, &prev));

		if (prev->ds_phys->ds_next_snap_obj != ds->ds_object) {
			dsl_dataset_close(prev, DS_MODE_EXCLUSIVE, FTAG);
			break;
		}
		if (ds != origin_ds)
			dsl_dataset_close(ds, DS_MODE_EXCLUSIVE, FTAG);
		ds = prev;
	}
	if (ds != origin_ds)
		dsl_dataset_close(ds, DS_MODE_EXCLUSIVE, FTAG);

	/* change origin's next snap */
	dmu_buf_will_dirty(origin_ds->ds_dbuf, tx);
	origin_ds->ds_phys->ds_next_snap_obj = pa->newnext_obj;

	/* change origin */
	dmu_buf_will_dirty(dd->dd_dbuf, tx);
	ASSERT3U(dd->dd_phys->dd_origin_obj, ==, origin_ds->ds_object);
	dd->dd_phys->dd_origin_obj = odd->dd_phys->dd_origin_obj;
	dmu_buf_will_dirty(odd->dd_dbuf, tx);
	odd->dd_phys->dd_origin_obj = origin_ds->ds_object;

	/* change space accounting */
	dsl_dir_diduse_space(odd, -pa->used, -pa->comp, -pa->uncomp, tx);
	dsl_dir_diduse_space(dd, pa->used, pa->comp, pa->uncomp, tx);
	origin_ds->ds_phys->ds_unique_bytes = pa->unique;

	/* log history record */
	spa_history_internal_log(LOG_DS_PROMOTE, dd->dd_pool->dp_spa, tx,
	    cr, "dataset = %llu", ds->ds_object);

	dsl_dir_close(odd, FTAG);
	dsl_dataset_close(origin_ds, DS_MODE_EXCLUSIVE, FTAG);
	kmem_free(name, MAXPATHLEN);
}

int
dsl_dataset_promote(const char *name)
{
	dsl_dataset_t *ds;
	int err;
	dmu_object_info_t doi;
	struct promotearg pa;

	err = dsl_dataset_open(name, DS_MODE_NONE, FTAG, &ds);
	if (err)
		return (err);

	err = dmu_object_info(ds->ds_dir->dd_pool->dp_meta_objset,
	    ds->ds_phys->ds_snapnames_zapobj, &doi);
	if (err) {
		dsl_dataset_close(ds, DS_MODE_NONE, FTAG);
		return (err);
	}

	/*
	 * Add in 128x the snapnames zapobj size, since we will be moving
	 * a bunch of snapnames to the promoted ds, and dirtying their
	 * bonus buffers.
	 */
	err = dsl_sync_task_do(ds->ds_dir->dd_pool,
	    dsl_dataset_promote_check,
	    dsl_dataset_promote_sync, ds, &pa, 2 + 2 * doi.doi_physical_blks);
	dsl_dataset_close(ds, DS_MODE_NONE, FTAG);
	return (err);
}

struct cloneswaparg {
	dsl_dataset_t *cds; /* clone dataset */
	dsl_dataset_t *ohds; /* origin's head dataset */
	boolean_t force;
	int64_t unused_refres_delta; /* change in unconsumed refreservation */
};

/* ARGSUSED */
static int
dsl_dataset_clone_swap_check(void *arg1, void *arg2, dmu_tx_t *tx)
{
	struct cloneswaparg *csa = arg1;

	/* they should both be heads */
	if (dsl_dataset_is_snapshot(csa->cds) ||
	    dsl_dataset_is_snapshot(csa->ohds))
		return (EINVAL);

	/* the branch point should be just before them */
	if (csa->cds->ds_prev != csa->ohds->ds_prev)
		return (EINVAL);

	/* cds should be the clone */
	if (csa->cds->ds_prev->ds_phys->ds_next_snap_obj !=
	    csa->ohds->ds_object)
		return (EINVAL);

	/* the clone should be a child of the origin */
	if (csa->cds->ds_dir->dd_parent != csa->ohds->ds_dir)
		return (EINVAL);

	/* ohds shouldn't be modified unless 'force' */
	if (!csa->force && dsl_dataset_modified_since_lastsnap(csa->ohds))
		return (ETXTBSY);

	/* adjust amount of any unconsumed refreservation */
	csa->unused_refres_delta =
	    (int64_t)MIN(csa->ohds->ds_reserved,
	    csa->ohds->ds_phys->ds_unique_bytes) -
	    (int64_t)MIN(csa->ohds->ds_reserved,
	    csa->cds->ds_phys->ds_unique_bytes);

	if (csa->unused_refres_delta > 0 &&
	    csa->unused_refres_delta >
	    dsl_dir_space_available(csa->ohds->ds_dir, NULL, 0, TRUE))
		return (ENOSPC);

	return (0);
}

/* ARGSUSED */
static void
dsl_dataset_clone_swap_sync(void *arg1, void *arg2, cred_t *cr, dmu_tx_t *tx)
{
	struct cloneswaparg *csa = arg1;
	dsl_pool_t *dp = csa->cds->ds_dir->dd_pool;
	uint64_t itor = 0;
	blkptr_t bp;
	uint64_t unique = 0;
	int err;

	ASSERT(csa->cds->ds_reserved == 0);
	ASSERT(csa->cds->ds_quota == csa->ohds->ds_quota);

	dmu_buf_will_dirty(csa->cds->ds_dbuf, tx);
	dmu_buf_will_dirty(csa->ohds->ds_dbuf, tx);
	dmu_buf_will_dirty(csa->cds->ds_prev->ds_dbuf, tx);

	if (csa->cds->ds_user_ptr != NULL) {
		csa->cds->ds_user_evict_func(csa->cds, csa->cds->ds_user_ptr);
		csa->cds->ds_user_ptr = NULL;
	}

	if (csa->ohds->ds_user_ptr != NULL) {
		csa->ohds->ds_user_evict_func(csa->ohds,
		    csa->ohds->ds_user_ptr);
		csa->ohds->ds_user_ptr = NULL;
	}

	/* compute unique space */
	while ((err = bplist_iterate(&csa->cds->ds_deadlist,
	    &itor, &bp)) == 0) {
		if (bp.blk_birth > csa->cds->ds_prev->ds_phys->ds_prev_snap_txg)
			unique += bp_get_dasize(dp->dp_spa, &bp);
	}
	VERIFY(err == ENOENT);

	/* reset origin's unique bytes */
	csa->cds->ds_prev->ds_phys->ds_unique_bytes = unique;

	/* swap blkptrs */
	{
		blkptr_t tmp;
		tmp = csa->ohds->ds_phys->ds_bp;
		csa->ohds->ds_phys->ds_bp = csa->cds->ds_phys->ds_bp;
		csa->cds->ds_phys->ds_bp = tmp;
	}

	/* set dd_*_bytes */
	{
		int64_t dused, dcomp, duncomp;
		uint64_t cdl_used, cdl_comp, cdl_uncomp;
		uint64_t odl_used, odl_comp, odl_uncomp;

		VERIFY(0 == bplist_space(&csa->cds->ds_deadlist, &cdl_used,
		    &cdl_comp, &cdl_uncomp));
		VERIFY(0 == bplist_space(&csa->ohds->ds_deadlist, &odl_used,
		    &odl_comp, &odl_uncomp));
		dused = csa->cds->ds_phys->ds_used_bytes + cdl_used -
		    (csa->ohds->ds_phys->ds_used_bytes + odl_used);
		dcomp = csa->cds->ds_phys->ds_compressed_bytes + cdl_comp -
		    (csa->ohds->ds_phys->ds_compressed_bytes + odl_comp);
		duncomp = csa->cds->ds_phys->ds_uncompressed_bytes +
		    cdl_uncomp -
		    (csa->ohds->ds_phys->ds_uncompressed_bytes + odl_uncomp);

		dsl_dir_diduse_space(csa->ohds->ds_dir,
		    dused, dcomp, duncomp, tx);
		dsl_dir_diduse_space(csa->cds->ds_dir,
		    -dused, -dcomp, -duncomp, tx);
	}

#define	SWITCH64(x, y) \
	{ \
		uint64_t __tmp = (x); \
		(x) = (y); \
		(y) = __tmp; \
	}

	/* swap ds_*_bytes */
	SWITCH64(csa->ohds->ds_phys->ds_used_bytes,
	    csa->cds->ds_phys->ds_used_bytes);
	SWITCH64(csa->ohds->ds_phys->ds_compressed_bytes,
	    csa->cds->ds_phys->ds_compressed_bytes);
	SWITCH64(csa->ohds->ds_phys->ds_uncompressed_bytes,
	    csa->cds->ds_phys->ds_uncompressed_bytes);
	SWITCH64(csa->ohds->ds_phys->ds_unique_bytes,
	    csa->cds->ds_phys->ds_unique_bytes);

	/* apply any parent delta for change in unconsumed refreservation */
	dsl_dir_diduse_space(csa->ohds->ds_dir, csa->unused_refres_delta,
	    0, 0, tx);

	/* swap deadlists */
	bplist_close(&csa->cds->ds_deadlist);
	bplist_close(&csa->ohds->ds_deadlist);
	SWITCH64(csa->ohds->ds_phys->ds_deadlist_obj,
	    csa->cds->ds_phys->ds_deadlist_obj);
	VERIFY(0 == bplist_open(&csa->cds->ds_deadlist, dp->dp_meta_objset,
	    csa->cds->ds_phys->ds_deadlist_obj));
	VERIFY(0 == bplist_open(&csa->ohds->ds_deadlist, dp->dp_meta_objset,
	    csa->ohds->ds_phys->ds_deadlist_obj));
}

/*
 * Swap 'clone' with its origin head file system.
 */
int
dsl_dataset_clone_swap(dsl_dataset_t *clone, dsl_dataset_t *origin_head,
    boolean_t force)
{
	struct cloneswaparg csa;

	ASSERT(clone->ds_open_refcount == DS_REF_MAX);
	ASSERT(origin_head->ds_open_refcount == DS_REF_MAX);

	csa.cds = clone;
	csa.ohds = origin_head;
	csa.force = force;
	return (dsl_sync_task_do(clone->ds_dir->dd_pool,
	    dsl_dataset_clone_swap_check,
	    dsl_dataset_clone_swap_sync, &csa, NULL, 9));
}

/*
 * Given a pool name and a dataset object number in that pool,
 * return the name of that dataset.
 */
int
dsl_dsobj_to_dsname(char *pname, uint64_t obj, char *buf)
{
	spa_t *spa;
	dsl_pool_t *dp;
	dsl_dataset_t *ds = NULL;
	int error;

	if ((error = spa_open(pname, &spa, FTAG)) != 0)
		return (error);
	dp = spa_get_dsl(spa);
	rw_enter(&dp->dp_config_rwlock, RW_READER);
	if ((error = dsl_dataset_open_obj(dp, obj,
	    NULL, DS_MODE_NONE, FTAG, &ds)) != 0) {
		rw_exit(&dp->dp_config_rwlock);
		spa_close(spa, FTAG);
		return (error);
	}
	dsl_dataset_name(ds, buf);
	dsl_dataset_close(ds, DS_MODE_NONE, FTAG);
	rw_exit(&dp->dp_config_rwlock);
	spa_close(spa, FTAG);

	return (0);
}

int
dsl_dataset_check_quota(dsl_dataset_t *ds, boolean_t check_quota,
    uint64_t asize, uint64_t inflight, uint64_t *used,
    uint64_t *ref_rsrv)
{
	int error = 0;

	ASSERT3S(asize, >, 0);

	/*
	 * *ref_rsrv is the portion of asize that will come from any
	 * unconsumed refreservation space.
	 */
	*ref_rsrv = 0;

	mutex_enter(&ds->ds_lock);
	/*
	 * Make a space adjustment for reserved bytes.
	 */
	if (ds->ds_reserved > ds->ds_phys->ds_unique_bytes) {
		ASSERT3U(*used, >=,
		    ds->ds_reserved - ds->ds_phys->ds_unique_bytes);
		*used -= (ds->ds_reserved - ds->ds_phys->ds_unique_bytes);
		*ref_rsrv =
		    asize - MIN(asize, parent_delta(ds, asize + inflight));
	}

	if (!check_quota || ds->ds_quota == 0) {
		mutex_exit(&ds->ds_lock);
		return (0);
	}
	/*
	 * If they are requesting more space, and our current estimate
	 * is over quota, they get to try again unless the actual
	 * on-disk is over quota and there are no pending changes (which
	 * may free up space for us).
	 */
	if (ds->ds_phys->ds_used_bytes + inflight >= ds->ds_quota) {
		if (inflight > 0 || ds->ds_phys->ds_used_bytes < ds->ds_quota)
			error = ERESTART;
		else
			error = EDQUOT;
	}
	mutex_exit(&ds->ds_lock);

	return (error);
}

/* ARGSUSED */
static int
dsl_dataset_set_quota_check(void *arg1, void *arg2, dmu_tx_t *tx)
{
	dsl_dataset_t *ds = arg1;
	uint64_t *quotap = arg2;
	uint64_t new_quota = *quotap;

	if (spa_version(ds->ds_dir->dd_pool->dp_spa) < SPA_VERSION_REFQUOTA)
		return (ENOTSUP);

	if (new_quota == 0)
		return (0);

	if (new_quota < ds->ds_phys->ds_used_bytes ||
	    new_quota < ds->ds_reserved)
		return (ENOSPC);

	return (0);
}

/* ARGSUSED */
void
dsl_dataset_set_quota_sync(void *arg1, void *arg2, cred_t *cr, dmu_tx_t *tx)
{
	dsl_dataset_t *ds = arg1;
	uint64_t *quotap = arg2;
	uint64_t new_quota = *quotap;

	dmu_buf_will_dirty(ds->ds_dbuf, tx);

	mutex_enter(&ds->ds_lock);
	ds->ds_quota = new_quota;
	mutex_exit(&ds->ds_lock);

	dsl_prop_set_uint64_sync(ds->ds_dir, "refquota", new_quota, cr, tx);

	spa_history_internal_log(LOG_DS_REFQUOTA, ds->ds_dir->dd_pool->dp_spa,
	    tx, cr, "%lld dataset = %llu ",
	    (longlong_t)new_quota, ds->ds_dir->dd_phys->dd_head_dataset_obj);
}

int
dsl_dataset_set_quota(const char *dsname, uint64_t quota)
{
	dsl_dataset_t *ds;
	int err;

	err = dsl_dataset_open(dsname, DS_MODE_STANDARD, FTAG, &ds);
	if (err)
		return (err);

	if (quota != ds->ds_quota) {
		/*
		 * If someone removes a file, then tries to set the quota, we
		 * want to make sure the file freeing takes effect.
		 */
		txg_wait_open(ds->ds_dir->dd_pool, 0);

		err = dsl_sync_task_do(ds->ds_dir->dd_pool,
		    dsl_dataset_set_quota_check, dsl_dataset_set_quota_sync,
		    ds, &quota, 0);
	}
	dsl_dataset_close(ds, DS_MODE_STANDARD, FTAG);
	return (err);
}

static int
dsl_dataset_set_reservation_check(void *arg1, void *arg2, dmu_tx_t *tx)
{
	dsl_dataset_t *ds = arg1;
	uint64_t *reservationp = arg2;
	uint64_t new_reservation = *reservationp;
	int64_t delta;
	uint64_t unique;

	if (new_reservation > INT64_MAX)
		return (EOVERFLOW);

	if (spa_version(ds->ds_dir->dd_pool->dp_spa) <
	    SPA_VERSION_REFRESERVATION)
		return (ENOTSUP);

	if (dsl_dataset_is_snapshot(ds))
		return (EINVAL);

	/*
	 * If we are doing the preliminary check in open context, the
	 * space estimates may be inaccurate.
	 */
	if (!dmu_tx_is_syncing(tx))
		return (0);

	mutex_enter(&ds->ds_lock);
	unique = dsl_dataset_unique(ds);
	delta = MAX(unique, new_reservation) - MAX(unique, ds->ds_reserved);
	mutex_exit(&ds->ds_lock);

	if (delta > 0 &&
	    delta > dsl_dir_space_available(ds->ds_dir, NULL, 0, TRUE))
		return (ENOSPC);
	if (delta > 0 && ds->ds_quota > 0 &&
	    new_reservation > ds->ds_quota)
		return (ENOSPC);

	return (0);
}

/* ARGSUSED */
static void
dsl_dataset_set_reservation_sync(void *arg1, void *arg2, cred_t *cr,
    dmu_tx_t *tx)
{
	dsl_dataset_t *ds = arg1;
	uint64_t *reservationp = arg2;
	uint64_t new_reservation = *reservationp;
	uint64_t unique;
	int64_t delta;

	dmu_buf_will_dirty(ds->ds_dbuf, tx);

	mutex_enter(&ds->ds_lock);
	unique = dsl_dataset_unique(ds);
	delta = MAX(0, (int64_t)(new_reservation - unique)) -
	    MAX(0, (int64_t)(ds->ds_reserved - unique));
	ds->ds_reserved = new_reservation;
	mutex_exit(&ds->ds_lock);

	dsl_prop_set_uint64_sync(ds->ds_dir, "refreservation",
	    new_reservation, cr, tx);

	dsl_dir_diduse_space(ds->ds_dir, delta, 0, 0, tx);

	spa_history_internal_log(LOG_DS_REFRESERV,
	    ds->ds_dir->dd_pool->dp_spa, tx, cr, "%lld dataset = %llu",
	    (longlong_t)new_reservation,
	    ds->ds_dir->dd_phys->dd_head_dataset_obj);
}

int
dsl_dataset_set_reservation(const char *dsname, uint64_t reservation)
{
	dsl_dataset_t *ds;
	int err;

	err = dsl_dataset_open(dsname, DS_MODE_STANDARD, FTAG, &ds);
	if (err)
		return (err);

	err = dsl_sync_task_do(ds->ds_dir->dd_pool,
	    dsl_dataset_set_reservation_check,
	    dsl_dataset_set_reservation_sync, ds, &reservation, 0);
	dsl_dataset_close(ds, DS_MODE_STANDARD, FTAG);
	return (err);
}
