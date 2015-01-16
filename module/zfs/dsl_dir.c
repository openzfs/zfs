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
 * Copyright (c) 2013 by Delphix. All rights reserved.
 * Copyright (c) 2013 Martin Matuska. All rights reserved.
 */

#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_tx.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_synctask.h>
#include <sys/dsl_deleg.h>
#include <sys/dmu_impl.h>
#include <sys/spa.h>
#include <sys/metaslab.h>
#include <sys/zap.h>
#include <sys/zio.h>
#include <sys/arc.h>
#include <sys/sunddi.h>
#include <sys/zvol.h>
#include "zfs_namecheck.h"

static uint64_t dsl_dir_space_towrite(dsl_dir_t *dd);

/* ARGSUSED */
static void
dsl_dir_evict(dmu_buf_t *db, void *arg)
{
	dsl_dir_t *dd = arg;
	int t;
	ASSERTV(dsl_pool_t *dp = dd->dd_pool);

	for (t = 0; t < TXG_SIZE; t++) {
		ASSERT(!txg_list_member(&dp->dp_dirty_dirs, dd, t));
		ASSERT(dd->dd_tempreserved[t] == 0);
		ASSERT(dd->dd_space_towrite[t] == 0);
	}

	if (dd->dd_parent)
		dsl_dir_rele(dd->dd_parent, dd);

	spa_close(dd->dd_pool->dp_spa, dd);

	/*
	 * The props callback list should have been cleaned up by
	 * objset_evict().
	 */
	list_destroy(&dd->dd_prop_cbs);
	mutex_destroy(&dd->dd_lock);
	kmem_free(dd, sizeof (dsl_dir_t));
}

int
dsl_dir_hold_obj(dsl_pool_t *dp, uint64_t ddobj,
    const char *tail, void *tag, dsl_dir_t **ddp)
{
	dmu_buf_t *dbuf;
	dsl_dir_t *dd;
	int err;

	ASSERT(dsl_pool_config_held(dp));

	err = dmu_bonus_hold(dp->dp_meta_objset, ddobj, tag, &dbuf);
	if (err != 0)
		return (err);
	dd = dmu_buf_get_user(dbuf);
#ifdef ZFS_DEBUG
	{
		dmu_object_info_t doi;
		dmu_object_info_from_db(dbuf, &doi);
		ASSERT3U(doi.doi_bonus_type, ==, DMU_OT_DSL_DIR);
		ASSERT3U(doi.doi_bonus_size, >=, sizeof (dsl_dir_phys_t));
	}
#endif
	if (dd == NULL) {
		dsl_dir_t *winner;

		dd = kmem_zalloc(sizeof (dsl_dir_t), KM_SLEEP);
		dd->dd_object = ddobj;
		dd->dd_dbuf = dbuf;
		dd->dd_pool = dp;
		dd->dd_phys = dbuf->db_data;
		mutex_init(&dd->dd_lock, NULL, MUTEX_DEFAULT, NULL);

		list_create(&dd->dd_prop_cbs, sizeof (dsl_prop_cb_record_t),
		    offsetof(dsl_prop_cb_record_t, cbr_node));

		dsl_dir_snap_cmtime_update(dd);

		if (dd->dd_phys->dd_parent_obj) {
			err = dsl_dir_hold_obj(dp, dd->dd_phys->dd_parent_obj,
			    NULL, dd, &dd->dd_parent);
			if (err != 0)
				goto errout;
			if (tail) {
#ifdef ZFS_DEBUG
				uint64_t foundobj;

				err = zap_lookup(dp->dp_meta_objset,
				    dd->dd_parent->dd_phys->dd_child_dir_zapobj,
				    tail, sizeof (foundobj), 1, &foundobj);
				ASSERT(err || foundobj == ddobj);
#endif
				(void) strcpy(dd->dd_myname, tail);
			} else {
				err = zap_value_search(dp->dp_meta_objset,
				    dd->dd_parent->dd_phys->dd_child_dir_zapobj,
				    ddobj, 0, dd->dd_myname);
			}
			if (err != 0)
				goto errout;
		} else {
			(void) strcpy(dd->dd_myname, spa_name(dp->dp_spa));
		}

		if (dsl_dir_is_clone(dd)) {
			dmu_buf_t *origin_bonus;
			dsl_dataset_phys_t *origin_phys;

			/*
			 * We can't open the origin dataset, because
			 * that would require opening this dsl_dir.
			 * Just look at its phys directly instead.
			 */
			err = dmu_bonus_hold(dp->dp_meta_objset,
			    dd->dd_phys->dd_origin_obj, FTAG, &origin_bonus);
			if (err != 0)
				goto errout;
			origin_phys = origin_bonus->db_data;
			dd->dd_origin_txg =
			    origin_phys->ds_creation_txg;
			dmu_buf_rele(origin_bonus, FTAG);
		}

		winner = dmu_buf_set_user_ie(dbuf, dd, &dd->dd_phys,
		    dsl_dir_evict);
		if (winner) {
			if (dd->dd_parent)
				dsl_dir_rele(dd->dd_parent, dd);
			mutex_destroy(&dd->dd_lock);
			kmem_free(dd, sizeof (dsl_dir_t));
			dd = winner;
		} else {
			spa_open_ref(dp->dp_spa, dd);
		}
	}

	/*
	 * The dsl_dir_t has both open-to-close and instantiate-to-evict
	 * holds on the spa.  We need the open-to-close holds because
	 * otherwise the spa_refcnt wouldn't change when we open a
	 * dir which the spa also has open, so we could incorrectly
	 * think it was OK to unload/export/destroy the pool.  We need
	 * the instantiate-to-evict hold because the dsl_dir_t has a
	 * pointer to the dd_pool, which has a pointer to the spa_t.
	 */
	spa_open_ref(dp->dp_spa, tag);
	ASSERT3P(dd->dd_pool, ==, dp);
	ASSERT3U(dd->dd_object, ==, ddobj);
	ASSERT3P(dd->dd_dbuf, ==, dbuf);
	*ddp = dd;
	return (0);

errout:
	if (dd->dd_parent)
		dsl_dir_rele(dd->dd_parent, dd);
	mutex_destroy(&dd->dd_lock);
	kmem_free(dd, sizeof (dsl_dir_t));
	dmu_buf_rele(dbuf, tag);
	return (err);
}

void
dsl_dir_rele(dsl_dir_t *dd, void *tag)
{
	dprintf_dd(dd, "%s\n", "");
	spa_close(dd->dd_pool->dp_spa, tag);
	dmu_buf_rele(dd->dd_dbuf, tag);
}

/* buf must be long enough (MAXNAMELEN + strlen(MOS_DIR_NAME) + 1 should do) */
void
dsl_dir_name(dsl_dir_t *dd, char *buf)
{
	if (dd->dd_parent) {
		dsl_dir_name(dd->dd_parent, buf);
		(void) strcat(buf, "/");
	} else {
		buf[0] = '\0';
	}
	if (!MUTEX_HELD(&dd->dd_lock)) {
		/*
		 * recursive mutex so that we can use
		 * dprintf_dd() with dd_lock held
		 */
		mutex_enter(&dd->dd_lock);
		(void) strcat(buf, dd->dd_myname);
		mutex_exit(&dd->dd_lock);
	} else {
		(void) strcat(buf, dd->dd_myname);
	}
}

/* Calculate name length, avoiding all the strcat calls of dsl_dir_name */
int
dsl_dir_namelen(dsl_dir_t *dd)
{
	int result = 0;

	if (dd->dd_parent) {
		/* parent's name + 1 for the "/" */
		result = dsl_dir_namelen(dd->dd_parent) + 1;
	}

	if (!MUTEX_HELD(&dd->dd_lock)) {
		/* see dsl_dir_name */
		mutex_enter(&dd->dd_lock);
		result += strlen(dd->dd_myname);
		mutex_exit(&dd->dd_lock);
	} else {
		result += strlen(dd->dd_myname);
	}

	return (result);
}

static int
getcomponent(const char *path, char *component, const char **nextp)
{
	char *p;

	if ((path == NULL) || (path[0] == '\0'))
		return (SET_ERROR(ENOENT));
	/* This would be a good place to reserve some namespace... */
	p = strpbrk(path, "/@");
	if (p && (p[1] == '/' || p[1] == '@')) {
		/* two separators in a row */
		return (SET_ERROR(EINVAL));
	}
	if (p == NULL || p == path) {
		/*
		 * if the first thing is an @ or /, it had better be an
		 * @ and it had better not have any more ats or slashes,
		 * and it had better have something after the @.
		 */
		if (p != NULL &&
		    (p[0] != '@' || strpbrk(path+1, "/@") || p[1] == '\0'))
			return (SET_ERROR(EINVAL));
		if (strlen(path) >= MAXNAMELEN)
			return (SET_ERROR(ENAMETOOLONG));
		(void) strcpy(component, path);
		p = NULL;
	} else if (p[0] == '/') {
		if (p - path >= MAXNAMELEN)
			return (SET_ERROR(ENAMETOOLONG));
		(void) strncpy(component, path, p - path);
		component[p - path] = '\0';
		p++;
	} else if (p[0] == '@') {
		/*
		 * if the next separator is an @, there better not be
		 * any more slashes.
		 */
		if (strchr(path, '/'))
			return (SET_ERROR(EINVAL));
		if (p - path >= MAXNAMELEN)
			return (SET_ERROR(ENAMETOOLONG));
		(void) strncpy(component, path, p - path);
		component[p - path] = '\0';
	} else {
		panic("invalid p=%p", (void *)p);
	}
	*nextp = p;
	return (0);
}

/*
 * Return the dsl_dir_t, and possibly the last component which couldn't
 * be found in *tail.  The name must be in the specified dsl_pool_t.  This
 * thread must hold the dp_config_rwlock for the pool.  Returns NULL if the
 * path is bogus, or if tail==NULL and we couldn't parse the whole name.
 * (*tail)[0] == '@' means that the last component is a snapshot.
 */
int
dsl_dir_hold(dsl_pool_t *dp, const char *name, void *tag,
    dsl_dir_t **ddp, const char **tailp)
{
	char *buf;
	const char *spaname, *next, *nextnext = NULL;
	int err;
	dsl_dir_t *dd;
	uint64_t ddobj;

	buf = kmem_alloc(MAXNAMELEN, KM_SLEEP);
	err = getcomponent(name, buf, &next);
	if (err != 0)
		goto error;

	/* Make sure the name is in the specified pool. */
	spaname = spa_name(dp->dp_spa);
	if (strcmp(buf, spaname) != 0) {
		err = SET_ERROR(EXDEV);
		goto error;
	}

	ASSERT(dsl_pool_config_held(dp));

	err = dsl_dir_hold_obj(dp, dp->dp_root_dir_obj, NULL, tag, &dd);
	if (err != 0) {
		goto error;
	}

	while (next != NULL) {
		dsl_dir_t *child_ds;
		err = getcomponent(next, buf, &nextnext);
		if (err != 0)
			break;
		ASSERT(next[0] != '\0');
		if (next[0] == '@')
			break;
		dprintf("looking up %s in obj%lld\n",
		    buf, dd->dd_phys->dd_child_dir_zapobj);

		err = zap_lookup(dp->dp_meta_objset,
		    dd->dd_phys->dd_child_dir_zapobj,
		    buf, sizeof (ddobj), 1, &ddobj);
		if (err != 0) {
			if (err == ENOENT)
				err = 0;
			break;
		}

		err = dsl_dir_hold_obj(dp, ddobj, buf, tag, &child_ds);
		if (err != 0)
			break;
		dsl_dir_rele(dd, tag);
		dd = child_ds;
		next = nextnext;
	}

	if (err != 0) {
		dsl_dir_rele(dd, tag);
		goto error;
	}

	/*
	 * It's an error if there's more than one component left, or
	 * tailp==NULL and there's any component left.
	 */
	if (next != NULL &&
	    (tailp == NULL || (nextnext && nextnext[0] != '\0'))) {
		/* bad path name */
		dsl_dir_rele(dd, tag);
		dprintf("next=%p (%s) tail=%p\n", next, next?next:"", tailp);
		err = SET_ERROR(ENOENT);
	}
	if (tailp != NULL)
		*tailp = next;
	*ddp = dd;
error:
	kmem_free(buf, MAXNAMELEN);
	return (err);
}

uint64_t
dsl_dir_create_sync(dsl_pool_t *dp, dsl_dir_t *pds, const char *name,
    dmu_tx_t *tx)
{
	objset_t *mos = dp->dp_meta_objset;
	uint64_t ddobj;
	dsl_dir_phys_t *ddphys;
	dmu_buf_t *dbuf;

	ddobj = dmu_object_alloc(mos, DMU_OT_DSL_DIR, 0,
	    DMU_OT_DSL_DIR, sizeof (dsl_dir_phys_t), tx);
	if (pds) {
		VERIFY(0 == zap_add(mos, pds->dd_phys->dd_child_dir_zapobj,
		    name, sizeof (uint64_t), 1, &ddobj, tx));
	} else {
		/* it's the root dir */
		VERIFY(0 == zap_add(mos, DMU_POOL_DIRECTORY_OBJECT,
		    DMU_POOL_ROOT_DATASET, sizeof (uint64_t), 1, &ddobj, tx));
	}
	VERIFY(0 == dmu_bonus_hold(mos, ddobj, FTAG, &dbuf));
	dmu_buf_will_dirty(dbuf, tx);
	ddphys = dbuf->db_data;

	ddphys->dd_creation_time = gethrestime_sec();
	if (pds)
		ddphys->dd_parent_obj = pds->dd_object;
	ddphys->dd_props_zapobj = zap_create(mos,
	    DMU_OT_DSL_PROPS, DMU_OT_NONE, 0, tx);
	ddphys->dd_child_dir_zapobj = zap_create(mos,
	    DMU_OT_DSL_DIR_CHILD_MAP, DMU_OT_NONE, 0, tx);
	if (spa_version(dp->dp_spa) >= SPA_VERSION_USED_BREAKDOWN)
		ddphys->dd_flags |= DD_FLAG_USED_BREAKDOWN;
	dmu_buf_rele(dbuf, FTAG);

	return (ddobj);
}

boolean_t
dsl_dir_is_clone(dsl_dir_t *dd)
{
	return (dd->dd_phys->dd_origin_obj &&
	    (dd->dd_pool->dp_origin_snap == NULL ||
	    dd->dd_phys->dd_origin_obj !=
	    dd->dd_pool->dp_origin_snap->ds_object));
}

void
dsl_dir_stats(dsl_dir_t *dd, nvlist_t *nv)
{
	mutex_enter(&dd->dd_lock);
	dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_USED,
	    dd->dd_phys->dd_used_bytes);
	dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_QUOTA, dd->dd_phys->dd_quota);
	dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_RESERVATION,
	    dd->dd_phys->dd_reserved);
	dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_COMPRESSRATIO,
	    dd->dd_phys->dd_compressed_bytes == 0 ? 100 :
	    (dd->dd_phys->dd_uncompressed_bytes * 100 /
	    dd->dd_phys->dd_compressed_bytes));
	dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_LOGICALUSED,
	    dd->dd_phys->dd_uncompressed_bytes);
	if (dd->dd_phys->dd_flags & DD_FLAG_USED_BREAKDOWN) {
		dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_USEDSNAP,
		    dd->dd_phys->dd_used_breakdown[DD_USED_SNAP]);
		dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_USEDDS,
		    dd->dd_phys->dd_used_breakdown[DD_USED_HEAD]);
		dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_USEDREFRESERV,
		    dd->dd_phys->dd_used_breakdown[DD_USED_REFRSRV]);
		dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_USEDCHILD,
		    dd->dd_phys->dd_used_breakdown[DD_USED_CHILD] +
		    dd->dd_phys->dd_used_breakdown[DD_USED_CHILD_RSRV]);
	}
	mutex_exit(&dd->dd_lock);

	if (dsl_dir_is_clone(dd)) {
		dsl_dataset_t *ds;
		char buf[MAXNAMELEN];

		VERIFY0(dsl_dataset_hold_obj(dd->dd_pool,
		    dd->dd_phys->dd_origin_obj, FTAG, &ds));
		dsl_dataset_name(ds, buf);
		dsl_dataset_rele(ds, FTAG);
		dsl_prop_nvlist_add_string(nv, ZFS_PROP_ORIGIN, buf);
	}
}

void
dsl_dir_dirty(dsl_dir_t *dd, dmu_tx_t *tx)
{
	dsl_pool_t *dp = dd->dd_pool;

	ASSERT(dd->dd_phys);

	if (txg_list_add(&dp->dp_dirty_dirs, dd, tx->tx_txg)) {
		/* up the hold count until we can be written out */
		dmu_buf_add_ref(dd->dd_dbuf, dd);
	}
}

static int64_t
parent_delta(dsl_dir_t *dd, uint64_t used, int64_t delta)
{
	uint64_t old_accounted = MAX(used, dd->dd_phys->dd_reserved);
	uint64_t new_accounted = MAX(used + delta, dd->dd_phys->dd_reserved);
	return (new_accounted - old_accounted);
}

void
dsl_dir_sync(dsl_dir_t *dd, dmu_tx_t *tx)
{
	ASSERT(dmu_tx_is_syncing(tx));

	mutex_enter(&dd->dd_lock);
	ASSERT0(dd->dd_tempreserved[tx->tx_txg&TXG_MASK]);
	dprintf_dd(dd, "txg=%llu towrite=%lluK\n", tx->tx_txg,
	    dd->dd_space_towrite[tx->tx_txg&TXG_MASK] / 1024);
	dd->dd_space_towrite[tx->tx_txg&TXG_MASK] = 0;
	mutex_exit(&dd->dd_lock);

	/* release the hold from dsl_dir_dirty */
	dmu_buf_rele(dd->dd_dbuf, dd);
}

static uint64_t
dsl_dir_space_towrite(dsl_dir_t *dd)
{
	uint64_t space = 0;
	int i;

	ASSERT(MUTEX_HELD(&dd->dd_lock));

	for (i = 0; i < TXG_SIZE; i++) {
		space += dd->dd_space_towrite[i&TXG_MASK];
		ASSERT3U(dd->dd_space_towrite[i&TXG_MASK], >=, 0);
	}
	return (space);
}

/*
 * How much space would dd have available if ancestor had delta applied
 * to it?  If ondiskonly is set, we're only interested in what's
 * on-disk, not estimated pending changes.
 */
uint64_t
dsl_dir_space_available(dsl_dir_t *dd,
    dsl_dir_t *ancestor, int64_t delta, int ondiskonly)
{
	uint64_t parentspace, myspace, quota, used;

	/*
	 * If there are no restrictions otherwise, assume we have
	 * unlimited space available.
	 */
	quota = UINT64_MAX;
	parentspace = UINT64_MAX;

	if (dd->dd_parent != NULL) {
		parentspace = dsl_dir_space_available(dd->dd_parent,
		    ancestor, delta, ondiskonly);
	}

	mutex_enter(&dd->dd_lock);
	if (dd->dd_phys->dd_quota != 0)
		quota = dd->dd_phys->dd_quota;
	used = dd->dd_phys->dd_used_bytes;
	if (!ondiskonly)
		used += dsl_dir_space_towrite(dd);

	if (dd->dd_parent == NULL) {
		uint64_t poolsize = dsl_pool_adjustedsize(dd->dd_pool, FALSE);
		quota = MIN(quota, poolsize);
	}

	if (dd->dd_phys->dd_reserved > used && parentspace != UINT64_MAX) {
		/*
		 * We have some space reserved, in addition to what our
		 * parent gave us.
		 */
		parentspace += dd->dd_phys->dd_reserved - used;
	}

	if (dd == ancestor) {
		ASSERT(delta <= 0);
		ASSERT(used >= -delta);
		used += delta;
		if (parentspace != UINT64_MAX)
			parentspace -= delta;
	}

	if (used > quota) {
		/* over quota */
		myspace = 0;
	} else {
		/*
		 * the lesser of the space provided by our parent and
		 * the space left in our quota
		 */
		myspace = MIN(parentspace, quota - used);
	}

	mutex_exit(&dd->dd_lock);

	return (myspace);
}

struct tempreserve {
	list_node_t tr_node;
	dsl_dir_t *tr_ds;
	uint64_t tr_size;
};

static int
dsl_dir_tempreserve_impl(dsl_dir_t *dd, uint64_t asize, boolean_t netfree,
    boolean_t ignorequota, boolean_t checkrefquota, list_t *tr_list,
    dmu_tx_t *tx, boolean_t first)
{
	uint64_t txg = tx->tx_txg;
	uint64_t est_inflight, used_on_disk, quota, parent_rsrv;
	uint64_t deferred = 0;
	struct tempreserve *tr;
	int retval = EDQUOT;
	int txgidx = txg & TXG_MASK;
	int i;
	uint64_t ref_rsrv = 0;

	ASSERT3U(txg, !=, 0);
	ASSERT3S(asize, >, 0);

	mutex_enter(&dd->dd_lock);

	/*
	 * Check against the dsl_dir's quota.  We don't add in the delta
	 * when checking for over-quota because they get one free hit.
	 */
	est_inflight = dsl_dir_space_towrite(dd);
	for (i = 0; i < TXG_SIZE; i++)
		est_inflight += dd->dd_tempreserved[i];
	used_on_disk = dd->dd_phys->dd_used_bytes;

	/*
	 * On the first iteration, fetch the dataset's used-on-disk and
	 * refreservation values. Also, if checkrefquota is set, test if
	 * allocating this space would exceed the dataset's refquota.
	 */
	if (first && tx->tx_objset) {
		int error;
		dsl_dataset_t *ds = tx->tx_objset->os_dsl_dataset;

		error = dsl_dataset_check_quota(ds, checkrefquota,
		    asize, est_inflight, &used_on_disk, &ref_rsrv);
		if (error) {
			mutex_exit(&dd->dd_lock);
			DMU_TX_STAT_BUMP(dmu_tx_quota);
			return (error);
		}
	}

	/*
	 * If this transaction will result in a net free of space,
	 * we want to let it through.
	 */
	if (ignorequota || netfree || dd->dd_phys->dd_quota == 0)
		quota = UINT64_MAX;
	else
		quota = dd->dd_phys->dd_quota;

	/*
	 * Adjust the quota against the actual pool size at the root
	 * minus any outstanding deferred frees.
	 * To ensure that it's possible to remove files from a full
	 * pool without inducing transient overcommits, we throttle
	 * netfree transactions against a quota that is slightly larger,
	 * but still within the pool's allocation slop.  In cases where
	 * we're very close to full, this will allow a steady trickle of
	 * removes to get through.
	 */
	if (dd->dd_parent == NULL) {
		spa_t *spa = dd->dd_pool->dp_spa;
		uint64_t poolsize = dsl_pool_adjustedsize(dd->dd_pool, netfree);
		deferred = metaslab_class_get_deferred(spa_normal_class(spa));
		if (poolsize - deferred < quota) {
			quota = poolsize - deferred;
			retval = ENOSPC;
		}
	}

	/*
	 * If they are requesting more space, and our current estimate
	 * is over quota, they get to try again unless the actual
	 * on-disk is over quota and there are no pending changes (which
	 * may free up space for us).
	 */
	if (used_on_disk + est_inflight >= quota) {
		if (est_inflight > 0 || used_on_disk < quota ||
		    (retval == ENOSPC && used_on_disk < quota + deferred))
			retval = ERESTART;
		dprintf_dd(dd, "failing: used=%lluK inflight = %lluK "
		    "quota=%lluK tr=%lluK err=%d\n",
		    used_on_disk>>10, est_inflight>>10,
		    quota>>10, asize>>10, retval);
		mutex_exit(&dd->dd_lock);
		DMU_TX_STAT_BUMP(dmu_tx_quota);
		return (SET_ERROR(retval));
	}

	/* We need to up our estimated delta before dropping dd_lock */
	dd->dd_tempreserved[txgidx] += asize;

	parent_rsrv = parent_delta(dd, used_on_disk + est_inflight,
	    asize - ref_rsrv);
	mutex_exit(&dd->dd_lock);

	tr = kmem_zalloc(sizeof (struct tempreserve), KM_SLEEP);
	tr->tr_ds = dd;
	tr->tr_size = asize;
	list_insert_tail(tr_list, tr);

	/* see if it's OK with our parent */
	if (dd->dd_parent && parent_rsrv) {
		boolean_t ismos = (dd->dd_phys->dd_head_dataset_obj == 0);

		return (dsl_dir_tempreserve_impl(dd->dd_parent,
		    parent_rsrv, netfree, ismos, TRUE, tr_list, tx, FALSE));
	} else {
		return (0);
	}
}

/*
 * Reserve space in this dsl_dir, to be used in this tx's txg.
 * After the space has been dirtied (and dsl_dir_willuse_space()
 * has been called), the reservation should be canceled, using
 * dsl_dir_tempreserve_clear().
 */
int
dsl_dir_tempreserve_space(dsl_dir_t *dd, uint64_t lsize, uint64_t asize,
    uint64_t fsize, uint64_t usize, void **tr_cookiep, dmu_tx_t *tx)
{
	int err;
	list_t *tr_list;

	if (asize == 0) {
		*tr_cookiep = NULL;
		return (0);
	}

	tr_list = kmem_alloc(sizeof (list_t), KM_SLEEP);
	list_create(tr_list, sizeof (struct tempreserve),
	    offsetof(struct tempreserve, tr_node));
	ASSERT3S(asize, >, 0);
	ASSERT3S(fsize, >=, 0);

	err = arc_tempreserve_space(lsize, tx->tx_txg);
	if (err == 0) {
		struct tempreserve *tr;

		tr = kmem_zalloc(sizeof (struct tempreserve), KM_SLEEP);
		tr->tr_size = lsize;
		list_insert_tail(tr_list, tr);
	} else {
		if (err == EAGAIN) {
			/*
			 * If arc_memory_throttle() detected that pageout
			 * is running and we are low on memory, we delay new
			 * non-pageout transactions to give pageout an
			 * advantage.
			 *
			 * It is unfortunate to be delaying while the caller's
			 * locks are held.
			 */
			txg_delay(dd->dd_pool, tx->tx_txg,
			    MSEC2NSEC(10), MSEC2NSEC(10));
			err = SET_ERROR(ERESTART);
		}
	}

	if (err == 0) {
		err = dsl_dir_tempreserve_impl(dd, asize, fsize >= asize,
		    FALSE, asize > usize, tr_list, tx, TRUE);
	}

	if (err != 0)
		dsl_dir_tempreserve_clear(tr_list, tx);
	else
		*tr_cookiep = tr_list;

	return (err);
}

/*
 * Clear a temporary reservation that we previously made with
 * dsl_dir_tempreserve_space().
 */
void
dsl_dir_tempreserve_clear(void *tr_cookie, dmu_tx_t *tx)
{
	int txgidx = tx->tx_txg & TXG_MASK;
	list_t *tr_list = tr_cookie;
	struct tempreserve *tr;

	ASSERT3U(tx->tx_txg, !=, 0);

	if (tr_cookie == NULL)
		return;

	while ((tr = list_head(tr_list)) != NULL) {
		if (tr->tr_ds) {
			mutex_enter(&tr->tr_ds->dd_lock);
			ASSERT3U(tr->tr_ds->dd_tempreserved[txgidx], >=,
			    tr->tr_size);
			tr->tr_ds->dd_tempreserved[txgidx] -= tr->tr_size;
			mutex_exit(&tr->tr_ds->dd_lock);
		} else {
			arc_tempreserve_clear(tr->tr_size);
		}
		list_remove(tr_list, tr);
		kmem_free(tr, sizeof (struct tempreserve));
	}

	kmem_free(tr_list, sizeof (list_t));
}

/*
 * This should be called from open context when we think we're going to write
 * or free space, for example when dirtying data. Be conservative; it's okay
 * to write less space or free more, but we don't want to write more or free
 * less than the amount specified.
 *
 * NOTE: The behavior of this function is identical to the Illumos / FreeBSD
 * version however it has been adjusted to use an iterative rather then
 * recursive algorithm to minimize stack usage.
 */
void
dsl_dir_willuse_space(dsl_dir_t *dd, int64_t space, dmu_tx_t *tx)
{
	int64_t parent_space;
	uint64_t est_used;

	do {
		mutex_enter(&dd->dd_lock);
		if (space > 0)
			dd->dd_space_towrite[tx->tx_txg & TXG_MASK] += space;

		est_used = dsl_dir_space_towrite(dd) +
		    dd->dd_phys->dd_used_bytes;
		parent_space = parent_delta(dd, est_used, space);
		mutex_exit(&dd->dd_lock);

		/* Make sure that we clean up dd_space_to* */
		dsl_dir_dirty(dd, tx);

		dd = dd->dd_parent;
		space = parent_space;
	} while (space && dd);
}

/* call from syncing context when we actually write/free space for this dd */
void
dsl_dir_diduse_space(dsl_dir_t *dd, dd_used_t type,
    int64_t used, int64_t compressed, int64_t uncompressed, dmu_tx_t *tx)
{
	int64_t accounted_delta;

	/*
	 * dsl_dataset_set_refreservation_sync_impl() calls this with
	 * dd_lock held, so that it can atomically update
	 * ds->ds_reserved and the dsl_dir accounting, so that
	 * dsl_dataset_check_quota() can see dataset and dir accounting
	 * consistently.
	 */
	boolean_t needlock = !MUTEX_HELD(&dd->dd_lock);

	ASSERT(dmu_tx_is_syncing(tx));
	ASSERT(type < DD_USED_NUM);

	dmu_buf_will_dirty(dd->dd_dbuf, tx);

	if (needlock)
		mutex_enter(&dd->dd_lock);
	accounted_delta = parent_delta(dd, dd->dd_phys->dd_used_bytes, used);
	ASSERT(used >= 0 || dd->dd_phys->dd_used_bytes >= -used);
	ASSERT(compressed >= 0 ||
	    dd->dd_phys->dd_compressed_bytes >= -compressed);
	ASSERT(uncompressed >= 0 ||
	    dd->dd_phys->dd_uncompressed_bytes >= -uncompressed);
	dd->dd_phys->dd_used_bytes += used;
	dd->dd_phys->dd_uncompressed_bytes += uncompressed;
	dd->dd_phys->dd_compressed_bytes += compressed;

	if (dd->dd_phys->dd_flags & DD_FLAG_USED_BREAKDOWN) {
		ASSERT(used > 0 ||
		    dd->dd_phys->dd_used_breakdown[type] >= -used);
		dd->dd_phys->dd_used_breakdown[type] += used;
#ifdef DEBUG
		{
			dd_used_t t;
			uint64_t u = 0;
			for (t = 0; t < DD_USED_NUM; t++)
				u += dd->dd_phys->dd_used_breakdown[t];
			ASSERT3U(u, ==, dd->dd_phys->dd_used_bytes);
		}
#endif
	}
	if (needlock)
		mutex_exit(&dd->dd_lock);

	if (dd->dd_parent != NULL) {
		dsl_dir_diduse_space(dd->dd_parent, DD_USED_CHILD,
		    accounted_delta, compressed, uncompressed, tx);
		dsl_dir_transfer_space(dd->dd_parent,
		    used - accounted_delta,
		    DD_USED_CHILD_RSRV, DD_USED_CHILD, tx);
	}
}

void
dsl_dir_transfer_space(dsl_dir_t *dd, int64_t delta,
    dd_used_t oldtype, dd_used_t newtype, dmu_tx_t *tx)
{
	ASSERT(dmu_tx_is_syncing(tx));
	ASSERT(oldtype < DD_USED_NUM);
	ASSERT(newtype < DD_USED_NUM);

	if (delta == 0 || !(dd->dd_phys->dd_flags & DD_FLAG_USED_BREAKDOWN))
		return;

	dmu_buf_will_dirty(dd->dd_dbuf, tx);
	mutex_enter(&dd->dd_lock);
	ASSERT(delta > 0 ?
	    dd->dd_phys->dd_used_breakdown[oldtype] >= delta :
	    dd->dd_phys->dd_used_breakdown[newtype] >= -delta);
	ASSERT(dd->dd_phys->dd_used_bytes >= ABS(delta));
	dd->dd_phys->dd_used_breakdown[oldtype] -= delta;
	dd->dd_phys->dd_used_breakdown[newtype] += delta;
	mutex_exit(&dd->dd_lock);
}

typedef struct dsl_dir_set_qr_arg {
	const char *ddsqra_name;
	zprop_source_t ddsqra_source;
	uint64_t ddsqra_value;
} dsl_dir_set_qr_arg_t;

static int
dsl_dir_set_quota_check(void *arg, dmu_tx_t *tx)
{
	dsl_dir_set_qr_arg_t *ddsqra = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	dsl_dataset_t *ds;
	int error;
	uint64_t towrite, newval;

	error = dsl_dataset_hold(dp, ddsqra->ddsqra_name, FTAG, &ds);
	if (error != 0)
		return (error);

	error = dsl_prop_predict(ds->ds_dir, "quota",
	    ddsqra->ddsqra_source, ddsqra->ddsqra_value, &newval);
	if (error != 0) {
		dsl_dataset_rele(ds, FTAG);
		return (error);
	}

	if (newval == 0) {
		dsl_dataset_rele(ds, FTAG);
		return (0);
	}

	mutex_enter(&ds->ds_dir->dd_lock);
	/*
	 * If we are doing the preliminary check in open context, and
	 * there are pending changes, then don't fail it, since the
	 * pending changes could under-estimate the amount of space to be
	 * freed up.
	 */
	towrite = dsl_dir_space_towrite(ds->ds_dir);
	if ((dmu_tx_is_syncing(tx) || towrite == 0) &&
	    (newval < ds->ds_dir->dd_phys->dd_reserved ||
	    newval < ds->ds_dir->dd_phys->dd_used_bytes + towrite)) {
		error = SET_ERROR(ENOSPC);
	}
	mutex_exit(&ds->ds_dir->dd_lock);
	dsl_dataset_rele(ds, FTAG);
	return (error);
}

static void
dsl_dir_set_quota_sync(void *arg, dmu_tx_t *tx)
{
	dsl_dir_set_qr_arg_t *ddsqra = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	dsl_dataset_t *ds;
	uint64_t newval;

	VERIFY0(dsl_dataset_hold(dp, ddsqra->ddsqra_name, FTAG, &ds));

	if (spa_version(dp->dp_spa) >= SPA_VERSION_RECVD_PROPS) {
		dsl_prop_set_sync_impl(ds, zfs_prop_to_name(ZFS_PROP_QUOTA),
		    ddsqra->ddsqra_source, sizeof (ddsqra->ddsqra_value), 1,
		    &ddsqra->ddsqra_value, tx);

		VERIFY0(dsl_prop_get_int_ds(ds,
		    zfs_prop_to_name(ZFS_PROP_QUOTA), &newval));
	} else {
		newval = ddsqra->ddsqra_value;
		spa_history_log_internal_ds(ds, "set", tx, "%s=%lld",
		    zfs_prop_to_name(ZFS_PROP_QUOTA), (longlong_t)newval);
	}

	dmu_buf_will_dirty(ds->ds_dir->dd_dbuf, tx);
	mutex_enter(&ds->ds_dir->dd_lock);
	ds->ds_dir->dd_phys->dd_quota = newval;
	mutex_exit(&ds->ds_dir->dd_lock);
	dsl_dataset_rele(ds, FTAG);
}

int
dsl_dir_set_quota(const char *ddname, zprop_source_t source, uint64_t quota)
{
	dsl_dir_set_qr_arg_t ddsqra;

	ddsqra.ddsqra_name = ddname;
	ddsqra.ddsqra_source = source;
	ddsqra.ddsqra_value = quota;

	return (dsl_sync_task(ddname, dsl_dir_set_quota_check,
	    dsl_dir_set_quota_sync, &ddsqra, 0));
}

int
dsl_dir_set_reservation_check(void *arg, dmu_tx_t *tx)
{
	dsl_dir_set_qr_arg_t *ddsqra = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	dsl_dataset_t *ds;
	dsl_dir_t *dd;
	uint64_t newval, used, avail;
	int error;

	error = dsl_dataset_hold(dp, ddsqra->ddsqra_name, FTAG, &ds);
	if (error != 0)
		return (error);
	dd = ds->ds_dir;

	/*
	 * If we are doing the preliminary check in open context, the
	 * space estimates may be inaccurate.
	 */
	if (!dmu_tx_is_syncing(tx)) {
		dsl_dataset_rele(ds, FTAG);
		return (0);
	}

	error = dsl_prop_predict(ds->ds_dir,
	    zfs_prop_to_name(ZFS_PROP_RESERVATION),
	    ddsqra->ddsqra_source, ddsqra->ddsqra_value, &newval);
	if (error != 0) {
		dsl_dataset_rele(ds, FTAG);
		return (error);
	}

	mutex_enter(&dd->dd_lock);
	used = dd->dd_phys->dd_used_bytes;
	mutex_exit(&dd->dd_lock);

	if (dd->dd_parent) {
		avail = dsl_dir_space_available(dd->dd_parent,
		    NULL, 0, FALSE);
	} else {
		avail = dsl_pool_adjustedsize(dd->dd_pool, B_FALSE) - used;
	}

	if (MAX(used, newval) > MAX(used, dd->dd_phys->dd_reserved)) {
		uint64_t delta = MAX(used, newval) -
		    MAX(used, dd->dd_phys->dd_reserved);

		if (delta > avail ||
		    (dd->dd_phys->dd_quota > 0 &&
		    newval > dd->dd_phys->dd_quota))
			error = SET_ERROR(ENOSPC);
	}

	dsl_dataset_rele(ds, FTAG);
	return (error);
}

void
dsl_dir_set_reservation_sync_impl(dsl_dir_t *dd, uint64_t value, dmu_tx_t *tx)
{
	uint64_t used;
	int64_t delta;

	dmu_buf_will_dirty(dd->dd_dbuf, tx);

	mutex_enter(&dd->dd_lock);
	used = dd->dd_phys->dd_used_bytes;
	delta = MAX(used, value) - MAX(used, dd->dd_phys->dd_reserved);
	dd->dd_phys->dd_reserved = value;

	if (dd->dd_parent != NULL) {
		/* Roll up this additional usage into our ancestors */
		dsl_dir_diduse_space(dd->dd_parent, DD_USED_CHILD_RSRV,
		    delta, 0, 0, tx);
	}
	mutex_exit(&dd->dd_lock);
}

static void
dsl_dir_set_reservation_sync(void *arg, dmu_tx_t *tx)
{
	dsl_dir_set_qr_arg_t *ddsqra = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	dsl_dataset_t *ds;
	uint64_t newval;

	VERIFY0(dsl_dataset_hold(dp, ddsqra->ddsqra_name, FTAG, &ds));

	if (spa_version(dp->dp_spa) >= SPA_VERSION_RECVD_PROPS) {
		dsl_prop_set_sync_impl(ds,
		    zfs_prop_to_name(ZFS_PROP_RESERVATION),
		    ddsqra->ddsqra_source, sizeof (ddsqra->ddsqra_value), 1,
		    &ddsqra->ddsqra_value, tx);

		VERIFY0(dsl_prop_get_int_ds(ds,
		    zfs_prop_to_name(ZFS_PROP_RESERVATION), &newval));
	} else {
		newval = ddsqra->ddsqra_value;
		spa_history_log_internal_ds(ds, "set", tx, "%s=%lld",
		    zfs_prop_to_name(ZFS_PROP_RESERVATION),
		    (longlong_t)newval);
	}

	dsl_dir_set_reservation_sync_impl(ds->ds_dir, newval, tx);
	dsl_dataset_rele(ds, FTAG);
}

int
dsl_dir_set_reservation(const char *ddname, zprop_source_t source,
    uint64_t reservation)
{
	dsl_dir_set_qr_arg_t ddsqra;

	ddsqra.ddsqra_name = ddname;
	ddsqra.ddsqra_source = source;
	ddsqra.ddsqra_value = reservation;

	return (dsl_sync_task(ddname, dsl_dir_set_reservation_check,
	    dsl_dir_set_reservation_sync, &ddsqra, 0));
}

static dsl_dir_t *
closest_common_ancestor(dsl_dir_t *ds1, dsl_dir_t *ds2)
{
	for (; ds1; ds1 = ds1->dd_parent) {
		dsl_dir_t *dd;
		for (dd = ds2; dd; dd = dd->dd_parent) {
			if (ds1 == dd)
				return (dd);
		}
	}
	return (NULL);
}

/*
 * If delta is applied to dd, how much of that delta would be applied to
 * ancestor?  Syncing context only.
 */
static int64_t
would_change(dsl_dir_t *dd, int64_t delta, dsl_dir_t *ancestor)
{
	if (dd == ancestor)
		return (delta);

	mutex_enter(&dd->dd_lock);
	delta = parent_delta(dd, dd->dd_phys->dd_used_bytes, delta);
	mutex_exit(&dd->dd_lock);
	return (would_change(dd->dd_parent, delta, ancestor));
}

typedef struct dsl_dir_rename_arg {
	const char *ddra_oldname;
	const char *ddra_newname;
} dsl_dir_rename_arg_t;

/* ARGSUSED */
static int
dsl_valid_rename(dsl_pool_t *dp, dsl_dataset_t *ds, void *arg)
{
	int *deltap = arg;
	char namebuf[MAXNAMELEN];

	dsl_dataset_name(ds, namebuf);

	if (strlen(namebuf) + *deltap >= MAXNAMELEN)
		return (SET_ERROR(ENAMETOOLONG));
	return (0);
}

static int
dsl_dir_rename_check(void *arg, dmu_tx_t *tx)
{
	dsl_dir_rename_arg_t *ddra = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	dsl_dir_t *dd, *newparent;
	const char *mynewname;
	int error;
	int delta = strlen(ddra->ddra_newname) - strlen(ddra->ddra_oldname);

	/* target dir should exist */
	error = dsl_dir_hold(dp, ddra->ddra_oldname, FTAG, &dd, NULL);
	if (error != 0)
		return (error);

	/* new parent should exist */
	error = dsl_dir_hold(dp, ddra->ddra_newname, FTAG,
	    &newparent, &mynewname);
	if (error != 0) {
		dsl_dir_rele(dd, FTAG);
		return (error);
	}

	/* can't rename to different pool */
	if (dd->dd_pool != newparent->dd_pool) {
		dsl_dir_rele(newparent, FTAG);
		dsl_dir_rele(dd, FTAG);
		return (SET_ERROR(EXDEV));
	}

	/* new name should not already exist */
	if (mynewname == NULL) {
		dsl_dir_rele(newparent, FTAG);
		dsl_dir_rele(dd, FTAG);
		return (SET_ERROR(EEXIST));
	}

	/* if the name length is growing, validate child name lengths */
	if (delta > 0) {
		error = dmu_objset_find_dp(dp, dd->dd_object, dsl_valid_rename,
		    &delta, DS_FIND_CHILDREN | DS_FIND_SNAPSHOTS);
		if (error != 0) {
			dsl_dir_rele(newparent, FTAG);
			dsl_dir_rele(dd, FTAG);
			return (error);
		}
	}

	if (newparent != dd->dd_parent) {
		/* is there enough space? */
		uint64_t myspace =
		    MAX(dd->dd_phys->dd_used_bytes, dd->dd_phys->dd_reserved);

		/* no rename into our descendant */
		if (closest_common_ancestor(dd, newparent) == dd) {
			dsl_dir_rele(newparent, FTAG);
			dsl_dir_rele(dd, FTAG);
			return (SET_ERROR(EINVAL));
		}

		error = dsl_dir_transfer_possible(dd->dd_parent,
		    newparent, myspace);
		if (error != 0) {
			dsl_dir_rele(newparent, FTAG);
			dsl_dir_rele(dd, FTAG);
			return (error);
		}
	}

	dsl_dir_rele(newparent, FTAG);
	dsl_dir_rele(dd, FTAG);
	return (0);
}

static void
dsl_dir_rename_sync(void *arg, dmu_tx_t *tx)
{
	dsl_dir_rename_arg_t *ddra = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	dsl_dir_t *dd, *newparent;
	const char *mynewname;
	int error;
	objset_t *mos = dp->dp_meta_objset;

	VERIFY0(dsl_dir_hold(dp, ddra->ddra_oldname, FTAG, &dd, NULL));
	VERIFY0(dsl_dir_hold(dp, ddra->ddra_newname, FTAG, &newparent,
	    &mynewname));

	/* Log this before we change the name. */
	spa_history_log_internal_dd(dd, "rename", tx,
	    "-> %s", ddra->ddra_newname);

	if (newparent != dd->dd_parent) {
		dsl_dir_diduse_space(dd->dd_parent, DD_USED_CHILD,
		    -dd->dd_phys->dd_used_bytes,
		    -dd->dd_phys->dd_compressed_bytes,
		    -dd->dd_phys->dd_uncompressed_bytes, tx);
		dsl_dir_diduse_space(newparent, DD_USED_CHILD,
		    dd->dd_phys->dd_used_bytes,
		    dd->dd_phys->dd_compressed_bytes,
		    dd->dd_phys->dd_uncompressed_bytes, tx);

		if (dd->dd_phys->dd_reserved > dd->dd_phys->dd_used_bytes) {
			uint64_t unused_rsrv = dd->dd_phys->dd_reserved -
			    dd->dd_phys->dd_used_bytes;

			dsl_dir_diduse_space(dd->dd_parent, DD_USED_CHILD_RSRV,
			    -unused_rsrv, 0, 0, tx);
			dsl_dir_diduse_space(newparent, DD_USED_CHILD_RSRV,
			    unused_rsrv, 0, 0, tx);
		}
	}

	dmu_buf_will_dirty(dd->dd_dbuf, tx);

	/* remove from old parent zapobj */
	error = zap_remove(mos, dd->dd_parent->dd_phys->dd_child_dir_zapobj,
	    dd->dd_myname, tx);
	ASSERT0(error);

	(void) strcpy(dd->dd_myname, mynewname);
	dsl_dir_rele(dd->dd_parent, dd);
	dd->dd_phys->dd_parent_obj = newparent->dd_object;
	VERIFY0(dsl_dir_hold_obj(dp,
	    newparent->dd_object, NULL, dd, &dd->dd_parent));

	/* add to new parent zapobj */
	VERIFY0(zap_add(mos, newparent->dd_phys->dd_child_dir_zapobj,
	    dd->dd_myname, 8, 1, &dd->dd_object, tx));

#ifdef _KERNEL
	zvol_rename_minors(ddra->ddra_oldname, ddra->ddra_newname);
#endif

	dsl_prop_notify_all(dd);

	dsl_dir_rele(newparent, FTAG);
	dsl_dir_rele(dd, FTAG);
}

int
dsl_dir_rename(const char *oldname, const char *newname)
{
	dsl_dir_rename_arg_t ddra;

	ddra.ddra_oldname = oldname;
	ddra.ddra_newname = newname;

	return (dsl_sync_task(oldname,
	    dsl_dir_rename_check, dsl_dir_rename_sync, &ddra, 3));
}

int
dsl_dir_transfer_possible(dsl_dir_t *sdd, dsl_dir_t *tdd, uint64_t space)
{
	dsl_dir_t *ancestor;
	int64_t adelta;
	uint64_t avail;

	ancestor = closest_common_ancestor(sdd, tdd);
	adelta = would_change(sdd, -space, ancestor);
	avail = dsl_dir_space_available(tdd, ancestor, adelta, FALSE);
	if (avail < space)
		return (SET_ERROR(ENOSPC));

	return (0);
}

timestruc_t
dsl_dir_snap_cmtime(dsl_dir_t *dd)
{
	timestruc_t t;

	mutex_enter(&dd->dd_lock);
	t = dd->dd_snap_cmtime;
	mutex_exit(&dd->dd_lock);

	return (t);
}

void
dsl_dir_snap_cmtime_update(dsl_dir_t *dd)
{
	timestruc_t t;

	gethrestime(&t);
	mutex_enter(&dd->dd_lock);
	dd->dd_snap_cmtime = t;
	mutex_exit(&dd->dd_lock);
}

void
dsl_dir_zapify(dsl_dir_t *dd, dmu_tx_t *tx)
{
	objset_t *mos = dd->dd_pool->dp_meta_objset;
	dmu_object_zapify(mos, dd->dd_object, DMU_OT_DSL_DIR, tx);
}

#if defined(_KERNEL) && defined(HAVE_SPL)
EXPORT_SYMBOL(dsl_dir_set_quota);
EXPORT_SYMBOL(dsl_dir_set_reservation);
#endif
