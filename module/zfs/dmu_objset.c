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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/cred.h>
#include <sys/zfs_context.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_synctask.h>
#include <sys/dsl_deleg.h>
#include <sys/dnode.h>
#include <sys/dbuf.h>
#include <sys/zvol.h>
#include <sys/dmu_tx.h>
#include <sys/zio_checksum.h>
#include <sys/zap.h>
#include <sys/zil.h>
#include <sys/dmu_impl.h>
#include <sys/zfs_ioctl.h>

spa_t *
dmu_objset_spa(objset_t *os)
{
	return (os->os->os_spa);
}

zilog_t *
dmu_objset_zil(objset_t *os)
{
	return (os->os->os_zil);
}

dsl_pool_t *
dmu_objset_pool(objset_t *os)
{
	dsl_dataset_t *ds;

	if ((ds = os->os->os_dsl_dataset) != NULL && ds->ds_dir)
		return (ds->ds_dir->dd_pool);
	else
		return (spa_get_dsl(os->os->os_spa));
}

dsl_dataset_t *
dmu_objset_ds(objset_t *os)
{
	return (os->os->os_dsl_dataset);
}

dmu_objset_type_t
dmu_objset_type(objset_t *os)
{
	return (os->os->os_phys->os_type);
}

void
dmu_objset_name(objset_t *os, char *buf)
{
	dsl_dataset_name(os->os->os_dsl_dataset, buf);
}

uint64_t
dmu_objset_id(objset_t *os)
{
	dsl_dataset_t *ds = os->os->os_dsl_dataset;

	return (ds ? ds->ds_object : 0);
}

static void
checksum_changed_cb(void *arg, uint64_t newval)
{
	objset_impl_t *osi = arg;

	/*
	 * Inheritance should have been done by now.
	 */
	ASSERT(newval != ZIO_CHECKSUM_INHERIT);

	osi->os_checksum = zio_checksum_select(newval, ZIO_CHECKSUM_ON_VALUE);
}

static void
compression_changed_cb(void *arg, uint64_t newval)
{
	objset_impl_t *osi = arg;

	/*
	 * Inheritance and range checking should have been done by now.
	 */
	ASSERT(newval != ZIO_COMPRESS_INHERIT);

	osi->os_compress = zio_compress_select(newval, ZIO_COMPRESS_ON_VALUE);
}

static void
copies_changed_cb(void *arg, uint64_t newval)
{
	objset_impl_t *osi = arg;

	/*
	 * Inheritance and range checking should have been done by now.
	 */
	ASSERT(newval > 0);
	ASSERT(newval <= spa_max_replication(osi->os_spa));

	osi->os_copies = newval;
}

static void
primary_cache_changed_cb(void *arg, uint64_t newval)
{
	objset_impl_t *osi = arg;

	/*
	 * Inheritance and range checking should have been done by now.
	 */
	ASSERT(newval == ZFS_CACHE_ALL || newval == ZFS_CACHE_NONE ||
	    newval == ZFS_CACHE_METADATA);

	osi->os_primary_cache = newval;
}

static void
secondary_cache_changed_cb(void *arg, uint64_t newval)
{
	objset_impl_t *osi = arg;

	/*
	 * Inheritance and range checking should have been done by now.
	 */
	ASSERT(newval == ZFS_CACHE_ALL || newval == ZFS_CACHE_NONE ||
	    newval == ZFS_CACHE_METADATA);

	osi->os_secondary_cache = newval;
}

void
dmu_objset_byteswap(void *buf, size_t size)
{
	objset_phys_t *osp = buf;

	ASSERT(size == OBJSET_OLD_PHYS_SIZE || size == sizeof (objset_phys_t));
	dnode_byteswap(&osp->os_meta_dnode);
	byteswap_uint64_array(&osp->os_zil_header, sizeof (zil_header_t));
	osp->os_type = BSWAP_64(osp->os_type);
	osp->os_flags = BSWAP_64(osp->os_flags);
	if (size == sizeof (objset_phys_t)) {
		dnode_byteswap(&osp->os_userused_dnode);
		dnode_byteswap(&osp->os_groupused_dnode);
	}
}

int
dmu_objset_open_impl(spa_t *spa, dsl_dataset_t *ds, blkptr_t *bp,
    objset_impl_t **osip)
{
	objset_impl_t *osi;
	int i, err;

	ASSERT(ds == NULL || MUTEX_HELD(&ds->ds_opening_lock));

	osi = kmem_zalloc(sizeof (objset_impl_t), KM_SLEEP);
	osi->os.os = osi;
	osi->os_dsl_dataset = ds;
	osi->os_spa = spa;
	osi->os_rootbp = bp;
	if (!BP_IS_HOLE(osi->os_rootbp)) {
		uint32_t aflags = ARC_WAIT;
		zbookmark_t zb;
		zb.zb_objset = ds ? ds->ds_object : 0;
		zb.zb_object = 0;
		zb.zb_level = -1;
		zb.zb_blkid = 0;
		if (DMU_OS_IS_L2CACHEABLE(osi))
			aflags |= ARC_L2CACHE;

		dprintf_bp(osi->os_rootbp, "reading %s", "");
		/*
		 * NB: when bprewrite scrub can change the bp,
		 * and this is called from dmu_objset_open_ds_os, the bp
		 * could change, and we'll need a lock.
		 */
		err = arc_read_nolock(NULL, spa, osi->os_rootbp,
		    arc_getbuf_func, &osi->os_phys_buf,
		    ZIO_PRIORITY_SYNC_READ, ZIO_FLAG_CANFAIL, &aflags, &zb);
		if (err) {
			kmem_free(osi, sizeof (objset_impl_t));
			/* convert checksum errors into IO errors */
			if (err == ECKSUM)
				err = EIO;
			return (err);
		}

		/* Increase the blocksize if we are permitted. */
		if (spa_version(spa) >= SPA_VERSION_USERSPACE &&
		    arc_buf_size(osi->os_phys_buf) < sizeof (objset_phys_t)) {
			arc_buf_t *buf = arc_buf_alloc(spa,
			    sizeof (objset_phys_t), &osi->os_phys_buf,
			    ARC_BUFC_METADATA);
			bzero(buf->b_data, sizeof (objset_phys_t));
			bcopy(osi->os_phys_buf->b_data, buf->b_data,
			    arc_buf_size(osi->os_phys_buf));
			(void) arc_buf_remove_ref(osi->os_phys_buf,
			    &osi->os_phys_buf);
			osi->os_phys_buf = buf;
		}

		osi->os_phys = osi->os_phys_buf->b_data;
		osi->os_flags = osi->os_phys->os_flags;
	} else {
		int size = spa_version(spa) >= SPA_VERSION_USERSPACE ?
		    sizeof (objset_phys_t) : OBJSET_OLD_PHYS_SIZE;
		osi->os_phys_buf = arc_buf_alloc(spa, size,
		    &osi->os_phys_buf, ARC_BUFC_METADATA);
		osi->os_phys = osi->os_phys_buf->b_data;
		bzero(osi->os_phys, size);
	}

	/*
	 * Note: the changed_cb will be called once before the register
	 * func returns, thus changing the checksum/compression from the
	 * default (fletcher2/off).  Snapshots don't need to know about
	 * checksum/compression/copies.
	 */
	if (ds) {
		err = dsl_prop_register(ds, "primarycache",
		    primary_cache_changed_cb, osi);
		if (err == 0)
			err = dsl_prop_register(ds, "secondarycache",
			    secondary_cache_changed_cb, osi);
		if (!dsl_dataset_is_snapshot(ds)) {
			if (err == 0)
				err = dsl_prop_register(ds, "checksum",
				    checksum_changed_cb, osi);
			if (err == 0)
				err = dsl_prop_register(ds, "compression",
				    compression_changed_cb, osi);
			if (err == 0)
				err = dsl_prop_register(ds, "copies",
				    copies_changed_cb, osi);
		}
		if (err) {
			VERIFY(arc_buf_remove_ref(osi->os_phys_buf,
			    &osi->os_phys_buf) == 1);
			kmem_free(osi, sizeof (objset_impl_t));
			return (err);
		}
	} else if (ds == NULL) {
		/* It's the meta-objset. */
		osi->os_checksum = ZIO_CHECKSUM_FLETCHER_4;
		osi->os_compress = ZIO_COMPRESS_LZJB;
		osi->os_copies = spa_max_replication(spa);
		osi->os_primary_cache = ZFS_CACHE_ALL;
		osi->os_secondary_cache = ZFS_CACHE_ALL;
	}

	osi->os_zil_header = osi->os_phys->os_zil_header;
	osi->os_zil = zil_alloc(&osi->os, &osi->os_zil_header);

	for (i = 0; i < TXG_SIZE; i++) {
		list_create(&osi->os_dirty_dnodes[i], sizeof (dnode_t),
		    offsetof(dnode_t, dn_dirty_link[i]));
		list_create(&osi->os_free_dnodes[i], sizeof (dnode_t),
		    offsetof(dnode_t, dn_dirty_link[i]));
	}
	list_create(&osi->os_dnodes, sizeof (dnode_t),
	    offsetof(dnode_t, dn_link));
	list_create(&osi->os_downgraded_dbufs, sizeof (dmu_buf_impl_t),
	    offsetof(dmu_buf_impl_t, db_link));

	mutex_init(&osi->os_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&osi->os_obj_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&osi->os_user_ptr_lock, NULL, MUTEX_DEFAULT, NULL);

	osi->os_meta_dnode = dnode_special_open(osi,
	    &osi->os_phys->os_meta_dnode, DMU_META_DNODE_OBJECT);
	if (arc_buf_size(osi->os_phys_buf) >= sizeof (objset_phys_t)) {
		osi->os_userused_dnode = dnode_special_open(osi,
		    &osi->os_phys->os_userused_dnode, DMU_USERUSED_OBJECT);
		osi->os_groupused_dnode = dnode_special_open(osi,
		    &osi->os_phys->os_groupused_dnode, DMU_GROUPUSED_OBJECT);
	}

	/*
	 * We should be the only thread trying to do this because we
	 * have ds_opening_lock
	 */
	if (ds) {
		VERIFY(NULL == dsl_dataset_set_user_ptr(ds, osi,
		    dmu_objset_evict));
	}

	*osip = osi;
	return (0);
}

static int
dmu_objset_open_ds_os(dsl_dataset_t *ds, objset_t *os, dmu_objset_type_t type)
{
	objset_impl_t *osi;

	mutex_enter(&ds->ds_opening_lock);
	osi = dsl_dataset_get_user_ptr(ds);
	if (osi == NULL) {
		int err;

		err = dmu_objset_open_impl(dsl_dataset_get_spa(ds),
		    ds, &ds->ds_phys->ds_bp, &osi);
		if (err) {
			mutex_exit(&ds->ds_opening_lock);
			return (err);
		}
	}
	mutex_exit(&ds->ds_opening_lock);

	os->os = osi;
	os->os_mode = DS_MODE_NOHOLD;

	if (type != DMU_OST_ANY && type != os->os->os_phys->os_type)
		return (EINVAL);
	return (0);
}

int
dmu_objset_open_ds(dsl_dataset_t *ds, dmu_objset_type_t type, objset_t **osp)
{
	objset_t *os;
	int err;

	os = kmem_alloc(sizeof (objset_t), KM_SLEEP);
	err = dmu_objset_open_ds_os(ds, os, type);
	if (err)
		kmem_free(os, sizeof (objset_t));
	else
		*osp = os;
	return (err);
}

/* called from zpl */
int
dmu_objset_open(const char *name, dmu_objset_type_t type, int mode,
    objset_t **osp)
{
	objset_t *os;
	dsl_dataset_t *ds;
	int err;

	ASSERT(DS_MODE_TYPE(mode) == DS_MODE_USER ||
	    DS_MODE_TYPE(mode) == DS_MODE_OWNER);

	os = kmem_alloc(sizeof (objset_t), KM_SLEEP);
	if (DS_MODE_TYPE(mode) == DS_MODE_USER)
		err = dsl_dataset_hold(name, os, &ds);
	else
		err = dsl_dataset_own(name, mode, os, &ds);
	if (err) {
		kmem_free(os, sizeof (objset_t));
		return (err);
	}

	err = dmu_objset_open_ds_os(ds, os, type);
	if (err) {
		if (DS_MODE_TYPE(mode) == DS_MODE_USER)
			dsl_dataset_rele(ds, os);
		else
			dsl_dataset_disown(ds, os);
		kmem_free(os, sizeof (objset_t));
	} else {
		os->os_mode = mode;
		*osp = os;
	}
	return (err);
}

void
dmu_objset_close(objset_t *os)
{
	ASSERT(DS_MODE_TYPE(os->os_mode) == DS_MODE_USER ||
	    DS_MODE_TYPE(os->os_mode) == DS_MODE_OWNER ||
	    DS_MODE_TYPE(os->os_mode) == DS_MODE_NOHOLD);

	if (DS_MODE_TYPE(os->os_mode) == DS_MODE_USER)
		dsl_dataset_rele(os->os->os_dsl_dataset, os);
	else if (DS_MODE_TYPE(os->os_mode) == DS_MODE_OWNER)
		dsl_dataset_disown(os->os->os_dsl_dataset, os);
	kmem_free(os, sizeof (objset_t));
}

int
dmu_objset_evict_dbufs(objset_t *os)
{
	objset_impl_t *osi = os->os;
	dnode_t *dn;

	mutex_enter(&osi->os_lock);

	/* process the mdn last, since the other dnodes have holds on it */
	list_remove(&osi->os_dnodes, osi->os_meta_dnode);
	list_insert_tail(&osi->os_dnodes, osi->os_meta_dnode);

	/*
	 * Find the first dnode with holds.  We have to do this dance
	 * because dnode_add_ref() only works if you already have a
	 * hold.  If there are no holds then it has no dbufs so OK to
	 * skip.
	 */
	for (dn = list_head(&osi->os_dnodes);
	    dn && !dnode_add_ref(dn, FTAG);
	    dn = list_next(&osi->os_dnodes, dn))
		continue;

	while (dn) {
		dnode_t *next_dn = dn;

		do {
			next_dn = list_next(&osi->os_dnodes, next_dn);
		} while (next_dn && !dnode_add_ref(next_dn, FTAG));

		mutex_exit(&osi->os_lock);
		dnode_evict_dbufs(dn);
		dnode_rele(dn, FTAG);
		mutex_enter(&osi->os_lock);
		dn = next_dn;
	}
	mutex_exit(&osi->os_lock);
	return (list_head(&osi->os_dnodes) != osi->os_meta_dnode);
}

void
dmu_objset_evict(dsl_dataset_t *ds, void *arg)
{
	objset_impl_t *osi = arg;
	objset_t os;
	int i;

	for (i = 0; i < TXG_SIZE; i++) {
		ASSERT(list_head(&osi->os_dirty_dnodes[i]) == NULL);
		ASSERT(list_head(&osi->os_free_dnodes[i]) == NULL);
	}

	if (ds) {
		if (!dsl_dataset_is_snapshot(ds)) {
			VERIFY(0 == dsl_prop_unregister(ds, "checksum",
			    checksum_changed_cb, osi));
			VERIFY(0 == dsl_prop_unregister(ds, "compression",
			    compression_changed_cb, osi));
			VERIFY(0 == dsl_prop_unregister(ds, "copies",
			    copies_changed_cb, osi));
		}
		VERIFY(0 == dsl_prop_unregister(ds, "primarycache",
		    primary_cache_changed_cb, osi));
		VERIFY(0 == dsl_prop_unregister(ds, "secondarycache",
		    secondary_cache_changed_cb, osi));
	}

	/*
	 * We should need only a single pass over the dnode list, since
	 * nothing can be added to the list at this point.
	 */
	os.os = osi;
	(void) dmu_objset_evict_dbufs(&os);

	dnode_special_close(osi->os_meta_dnode);
	if (osi->os_userused_dnode) {
		dnode_special_close(osi->os_userused_dnode);
		dnode_special_close(osi->os_groupused_dnode);
	}
	zil_free(osi->os_zil);

	ASSERT3P(list_head(&osi->os_dnodes), ==, NULL);

	VERIFY(arc_buf_remove_ref(osi->os_phys_buf, &osi->os_phys_buf) == 1);
	mutex_destroy(&osi->os_lock);
	mutex_destroy(&osi->os_obj_lock);
	mutex_destroy(&osi->os_user_ptr_lock);
	kmem_free(osi, sizeof (objset_impl_t));
}

/* called from dsl for meta-objset */
objset_impl_t *
dmu_objset_create_impl(spa_t *spa, dsl_dataset_t *ds, blkptr_t *bp,
    dmu_objset_type_t type, dmu_tx_t *tx)
{
	objset_impl_t *osi;
	dnode_t *mdn;

	ASSERT(dmu_tx_is_syncing(tx));
	if (ds)
		mutex_enter(&ds->ds_opening_lock);
	VERIFY(0 == dmu_objset_open_impl(spa, ds, bp, &osi));
	if (ds)
		mutex_exit(&ds->ds_opening_lock);
	mdn = osi->os_meta_dnode;

	dnode_allocate(mdn, DMU_OT_DNODE, 1 << DNODE_BLOCK_SHIFT,
	    DN_MAX_INDBLKSHIFT, DMU_OT_NONE, 0, tx);

	/*
	 * We don't want to have to increase the meta-dnode's nlevels
	 * later, because then we could do it in quescing context while
	 * we are also accessing it in open context.
	 *
	 * This precaution is not necessary for the MOS (ds == NULL),
	 * because the MOS is only updated in syncing context.
	 * This is most fortunate: the MOS is the only objset that
	 * needs to be synced multiple times as spa_sync() iterates
	 * to convergence, so minimizing its dn_nlevels matters.
	 */
	if (ds != NULL) {
		int levels = 1;

		/*
		 * Determine the number of levels necessary for the meta-dnode
		 * to contain DN_MAX_OBJECT dnodes.
		 */
		while ((uint64_t)mdn->dn_nblkptr << (mdn->dn_datablkshift +
		    (levels - 1) * (mdn->dn_indblkshift - SPA_BLKPTRSHIFT)) <
		    DN_MAX_OBJECT * sizeof (dnode_phys_t))
			levels++;

		mdn->dn_next_nlevels[tx->tx_txg & TXG_MASK] =
		    mdn->dn_nlevels = levels;
	}

	ASSERT(type != DMU_OST_NONE);
	ASSERT(type != DMU_OST_ANY);
	ASSERT(type < DMU_OST_NUMTYPES);
	osi->os_phys->os_type = type;
	if (dmu_objset_userused_enabled(osi)) {
		osi->os_phys->os_flags |= OBJSET_FLAG_USERACCOUNTING_COMPLETE;
		osi->os_flags = osi->os_phys->os_flags;
	}

	dsl_dataset_dirty(ds, tx);

	return (osi);
}

struct oscarg {
	void (*userfunc)(objset_t *os, void *arg, cred_t *cr, dmu_tx_t *tx);
	void *userarg;
	dsl_dataset_t *clone_parent;
	const char *lastname;
	dmu_objset_type_t type;
	uint64_t flags;
};

/*ARGSUSED*/
static int
dmu_objset_create_check(void *arg1, void *arg2, dmu_tx_t *tx)
{
	dsl_dir_t *dd = arg1;
	struct oscarg *oa = arg2;
	objset_t *mos = dd->dd_pool->dp_meta_objset;
	int err;
	uint64_t ddobj;

	err = zap_lookup(mos, dd->dd_phys->dd_child_dir_zapobj,
	    oa->lastname, sizeof (uint64_t), 1, &ddobj);
	if (err != ENOENT)
		return (err ? err : EEXIST);

	if (oa->clone_parent != NULL) {
		/*
		 * You can't clone across pools.
		 */
		if (oa->clone_parent->ds_dir->dd_pool != dd->dd_pool)
			return (EXDEV);

		/*
		 * You can only clone snapshots, not the head datasets.
		 */
		if (oa->clone_parent->ds_phys->ds_num_children == 0)
			return (EINVAL);
	}

	return (0);
}

static void
dmu_objset_create_sync(void *arg1, void *arg2, cred_t *cr, dmu_tx_t *tx)
{
	dsl_dir_t *dd = arg1;
	struct oscarg *oa = arg2;
	dsl_dataset_t *ds;
	blkptr_t *bp;
	uint64_t dsobj;

	ASSERT(dmu_tx_is_syncing(tx));

	dsobj = dsl_dataset_create_sync(dd, oa->lastname,
	    oa->clone_parent, oa->flags, cr, tx);

	VERIFY(0 == dsl_dataset_hold_obj(dd->dd_pool, dsobj, FTAG, &ds));
	bp = dsl_dataset_get_blkptr(ds);
	if (BP_IS_HOLE(bp)) {
		objset_impl_t *osi;

		/* This is an empty dmu_objset; not a clone. */
		osi = dmu_objset_create_impl(dsl_dataset_get_spa(ds),
		    ds, bp, oa->type, tx);

		if (oa->userfunc)
			oa->userfunc(&osi->os, oa->userarg, cr, tx);
	}

	spa_history_internal_log(LOG_DS_CREATE, dd->dd_pool->dp_spa,
	    tx, cr, "dataset = %llu", dsobj);

	dsl_dataset_rele(ds, FTAG);
}

int
dmu_objset_create(const char *name, dmu_objset_type_t type,
    objset_t *clone_parent, uint64_t flags,
    void (*func)(objset_t *os, void *arg, cred_t *cr, dmu_tx_t *tx), void *arg)
{
	dsl_dir_t *pdd;
	const char *tail;
	int err = 0;
	struct oscarg oa = { 0 };

	ASSERT(strchr(name, '@') == NULL);
	err = dsl_dir_open(name, FTAG, &pdd, &tail);
	if (err)
		return (err);
	if (tail == NULL) {
		dsl_dir_close(pdd, FTAG);
		return (EEXIST);
	}

	dprintf("name=%s\n", name);

	oa.userfunc = func;
	oa.userarg = arg;
	oa.lastname = tail;
	oa.type = type;
	oa.flags = flags;

	if (clone_parent != NULL) {
		/*
		 * You can't clone to a different type.
		 */
		if (clone_parent->os->os_phys->os_type != type) {
			dsl_dir_close(pdd, FTAG);
			return (EINVAL);
		}
		oa.clone_parent = clone_parent->os->os_dsl_dataset;
	}
	err = dsl_sync_task_do(pdd->dd_pool, dmu_objset_create_check,
	    dmu_objset_create_sync, pdd, &oa, 5);
	dsl_dir_close(pdd, FTAG);
	return (err);
}

int
dmu_objset_destroy(const char *name)
{
	objset_t *os;
	int error;

	/*
	 * If it looks like we'll be able to destroy it, and there's
	 * an unplayed replay log sitting around, destroy the log.
	 * It would be nicer to do this in dsl_dataset_destroy_sync(),
	 * but the replay log objset is modified in open context.
	 */
	error = dmu_objset_open(name, DMU_OST_ANY,
	    DS_MODE_OWNER|DS_MODE_READONLY|DS_MODE_INCONSISTENT, &os);
	if (error == 0) {
		dsl_dataset_t *ds = os->os->os_dsl_dataset;
		zil_destroy(dmu_objset_zil(os), B_FALSE);

		error = dsl_dataset_destroy(ds, os);
		/*
		 * dsl_dataset_destroy() closes the ds.
		 */
		kmem_free(os, sizeof (objset_t));
	}

	return (error);
}

/*
 * This will close the objset.
 */
int
dmu_objset_rollback(objset_t *os)
{
	int err;
	dsl_dataset_t *ds;

	ds = os->os->os_dsl_dataset;

	if (!dsl_dataset_tryown(ds, TRUE, os)) {
		dmu_objset_close(os);
		return (EBUSY);
	}

	err = dsl_dataset_rollback(ds, os->os->os_phys->os_type);

	/*
	 * NB: we close the objset manually because the rollback
	 * actually implicitly called dmu_objset_evict(), thus freeing
	 * the objset_impl_t.
	 */
	dsl_dataset_disown(ds, os);
	kmem_free(os, sizeof (objset_t));
	return (err);
}

struct snaparg {
	dsl_sync_task_group_t *dstg;
	char *snapname;
	char failed[MAXPATHLEN];
	boolean_t checkperms;
	nvlist_t *props;
};

static int
snapshot_check(void *arg1, void *arg2, dmu_tx_t *tx)
{
	objset_t *os = arg1;
	struct snaparg *sn = arg2;

	/* The props have already been checked by zfs_check_userprops(). */

	return (dsl_dataset_snapshot_check(os->os->os_dsl_dataset,
	    sn->snapname, tx));
}

static void
snapshot_sync(void *arg1, void *arg2, cred_t *cr, dmu_tx_t *tx)
{
	objset_t *os = arg1;
	dsl_dataset_t *ds = os->os->os_dsl_dataset;
	struct snaparg *sn = arg2;

	dsl_dataset_snapshot_sync(ds, sn->snapname, cr, tx);

	if (sn->props)
		dsl_props_set_sync(ds->ds_prev, sn->props, cr, tx);
}

static int
dmu_objset_snapshot_one(char *name, void *arg)
{
	struct snaparg *sn = arg;
	objset_t *os;
	int err;

	(void) strcpy(sn->failed, name);

	/*
	 * Check permissions only when requested.  This only applies when
	 * doing a recursive snapshot.  The permission checks for the starting
	 * dataset have already been performed in zfs_secpolicy_snapshot()
	 */
	if (sn->checkperms == B_TRUE &&
	    (err = zfs_secpolicy_snapshot_perms(name, CRED())))
		return (err);

	err = dmu_objset_open(name, DMU_OST_ANY, DS_MODE_USER, &os);
	if (err != 0)
		return (err);

	/* If the objset is in an inconsistent state, return busy */
	if (os->os->os_dsl_dataset->ds_phys->ds_flags & DS_FLAG_INCONSISTENT) {
		dmu_objset_close(os);
		return (EBUSY);
	}

	/*
	 * NB: we need to wait for all in-flight changes to get to disk,
	 * so that we snapshot those changes.  zil_suspend does this as
	 * a side effect.
	 */
	err = zil_suspend(dmu_objset_zil(os));
	if (err == 0) {
		dsl_sync_task_create(sn->dstg, snapshot_check,
		    snapshot_sync, os, sn, 3);
	} else {
		dmu_objset_close(os);
	}

	return (err);
}

int
dmu_objset_snapshot(char *fsname, char *snapname,
    nvlist_t *props, boolean_t recursive)
{
	dsl_sync_task_t *dst;
	struct snaparg sn;
	spa_t *spa;
	int err;

	(void) strcpy(sn.failed, fsname);

	err = spa_open(fsname, &spa, FTAG);
	if (err)
		return (err);

	sn.dstg = dsl_sync_task_group_create(spa_get_dsl(spa));
	sn.snapname = snapname;
	sn.props = props;

	if (recursive) {
		sn.checkperms = B_TRUE;
		err = dmu_objset_find(fsname,
		    dmu_objset_snapshot_one, &sn, DS_FIND_CHILDREN);
	} else {
		sn.checkperms = B_FALSE;
		err = dmu_objset_snapshot_one(fsname, &sn);
	}

	if (err == 0)
		err = dsl_sync_task_group_wait(sn.dstg);

	for (dst = list_head(&sn.dstg->dstg_tasks); dst;
	    dst = list_next(&sn.dstg->dstg_tasks, dst)) {
		objset_t *os = dst->dst_arg1;
		dsl_dataset_t *ds = os->os->os_dsl_dataset;
		if (dst->dst_err)
			dsl_dataset_name(ds, sn.failed);
		zil_resume(dmu_objset_zil(os));
		dmu_objset_close(os);
	}

	if (err)
		(void) strcpy(fsname, sn.failed);
	dsl_sync_task_group_destroy(sn.dstg);
	spa_close(spa, FTAG);
	return (err);
}

static void
dmu_objset_sync_dnodes(list_t *list, list_t *newlist, dmu_tx_t *tx)
{
	dnode_t *dn;

	while (dn = list_head(list)) {
		ASSERT(dn->dn_object != DMU_META_DNODE_OBJECT);
		ASSERT(dn->dn_dbuf->db_data_pending);
		/*
		 * Initialize dn_zio outside dnode_sync() because the
		 * meta-dnode needs to set it ouside dnode_sync().
		 */
		dn->dn_zio = dn->dn_dbuf->db_data_pending->dr_zio;
		ASSERT(dn->dn_zio);

		ASSERT3U(dn->dn_nlevels, <=, DN_MAX_LEVELS);
		list_remove(list, dn);

		if (newlist) {
			(void) dnode_add_ref(dn, newlist);
			list_insert_tail(newlist, dn);
		}

		dnode_sync(dn, tx);
	}
}

/* ARGSUSED */
static void
ready(zio_t *zio, arc_buf_t *abuf, void *arg)
{
	blkptr_t *bp = zio->io_bp;
	blkptr_t *bp_orig = &zio->io_bp_orig;
	objset_impl_t *os = arg;
	dnode_phys_t *dnp = &os->os_phys->os_meta_dnode;

	ASSERT(bp == os->os_rootbp);
	ASSERT(BP_GET_TYPE(bp) == DMU_OT_OBJSET);
	ASSERT(BP_GET_LEVEL(bp) == 0);

	/*
	 * Update rootbp fill count: it should be the number of objects
	 * allocated in the object set (not counting the "special"
	 * objects that are stored in the objset_phys_t -- the meta
	 * dnode and user/group accounting objects).
	 */
	bp->blk_fill = 0;
	for (int i = 0; i < dnp->dn_nblkptr; i++)
		bp->blk_fill += dnp->dn_blkptr[i].blk_fill;

	if (zio->io_flags & ZIO_FLAG_IO_REWRITE) {
		ASSERT(DVA_EQUAL(BP_IDENTITY(bp), BP_IDENTITY(bp_orig)));
	} else {
		if (zio->io_bp_orig.blk_birth == os->os_synctx->tx_txg)
			(void) dsl_dataset_block_kill(os->os_dsl_dataset,
			    &zio->io_bp_orig, zio, os->os_synctx);
		dsl_dataset_block_born(os->os_dsl_dataset, bp, os->os_synctx);
	}
}

/* called from dsl */
void
dmu_objset_sync(objset_impl_t *os, zio_t *pio, dmu_tx_t *tx)
{
	int txgoff;
	zbookmark_t zb;
	writeprops_t wp = { 0 };
	zio_t *zio;
	list_t *list;
	list_t *newlist = NULL;
	dbuf_dirty_record_t *dr;

	dprintf_ds(os->os_dsl_dataset, "txg=%llu\n", tx->tx_txg);

	ASSERT(dmu_tx_is_syncing(tx));
	/* XXX the write_done callback should really give us the tx... */
	os->os_synctx = tx;

	if (os->os_dsl_dataset == NULL) {
		/*
		 * This is the MOS.  If we have upgraded,
		 * spa_max_replication() could change, so reset
		 * os_copies here.
		 */
		os->os_copies = spa_max_replication(os->os_spa);
	}

	/*
	 * Create the root block IO
	 */
	zb.zb_objset = os->os_dsl_dataset ? os->os_dsl_dataset->ds_object : 0;
	zb.zb_object = 0;
	zb.zb_level = -1;	/* for block ordering; it's level 0 on disk */
	zb.zb_blkid = 0;

	wp.wp_type = DMU_OT_OBJSET;
	wp.wp_level = 0;	/* on-disk BP level; see above */
	wp.wp_copies = os->os_copies;
	wp.wp_oschecksum = os->os_checksum;
	wp.wp_oscompress = os->os_compress;

	if (BP_IS_OLDER(os->os_rootbp, tx->tx_txg)) {
		(void) dsl_dataset_block_kill(os->os_dsl_dataset,
		    os->os_rootbp, pio, tx);
	}

	arc_release(os->os_phys_buf, &os->os_phys_buf);

	zio = arc_write(pio, os->os_spa, &wp, DMU_OS_IS_L2CACHEABLE(os),
	    tx->tx_txg, os->os_rootbp, os->os_phys_buf, ready, NULL, os,
	    ZIO_PRIORITY_ASYNC_WRITE, ZIO_FLAG_MUSTSUCCEED, &zb);

	/*
	 * Sync special dnodes - the parent IO for the sync is the root block
	 */
	os->os_meta_dnode->dn_zio = zio;
	dnode_sync(os->os_meta_dnode, tx);

	os->os_phys->os_flags = os->os_flags;

	if (os->os_userused_dnode &&
	    os->os_userused_dnode->dn_type != DMU_OT_NONE) {
		os->os_userused_dnode->dn_zio = zio;
		dnode_sync(os->os_userused_dnode, tx);
		os->os_groupused_dnode->dn_zio = zio;
		dnode_sync(os->os_groupused_dnode, tx);
	}

	txgoff = tx->tx_txg & TXG_MASK;

	if (dmu_objset_userused_enabled(os)) {
		newlist = &os->os_synced_dnodes;
		/*
		 * We must create the list here because it uses the
		 * dn_dirty_link[] of this txg.
		 */
		list_create(newlist, sizeof (dnode_t),
		    offsetof(dnode_t, dn_dirty_link[txgoff]));
	}

	dmu_objset_sync_dnodes(&os->os_free_dnodes[txgoff], newlist, tx);
	dmu_objset_sync_dnodes(&os->os_dirty_dnodes[txgoff], newlist, tx);

	list = &os->os_meta_dnode->dn_dirty_records[txgoff];
	while (dr = list_head(list)) {
		ASSERT(dr->dr_dbuf->db_level == 0);
		list_remove(list, dr);
		if (dr->dr_zio)
			zio_nowait(dr->dr_zio);
	}
	/*
	 * Free intent log blocks up to this tx.
	 */
	zil_sync(os->os_zil, tx);
	os->os_phys->os_zil_header = os->os_zil_header;
	zio_nowait(zio);
}

static objset_used_cb_t *used_cbs[DMU_OST_NUMTYPES];

void
dmu_objset_register_type(dmu_objset_type_t ost, objset_used_cb_t *cb)
{
	used_cbs[ost] = cb;
}

boolean_t
dmu_objset_userused_enabled(objset_impl_t *os)
{
	return (spa_version(os->os_spa) >= SPA_VERSION_USERSPACE &&
	    used_cbs[os->os_phys->os_type] &&
	    os->os_userused_dnode);
}

void
dmu_objset_do_userquota_callbacks(objset_impl_t *os, dmu_tx_t *tx)
{
	dnode_t *dn;
	list_t *list = &os->os_synced_dnodes;
	static const char zerobuf[DN_MAX_BONUSLEN] = {0};

	ASSERT(list_head(list) == NULL || dmu_objset_userused_enabled(os));

	while (dn = list_head(list)) {
		dmu_object_type_t bonustype;

		ASSERT(!DMU_OBJECT_IS_SPECIAL(dn->dn_object));
		ASSERT(dn->dn_oldphys);
		ASSERT(dn->dn_phys->dn_type == DMU_OT_NONE ||
		    dn->dn_phys->dn_flags &
		    DNODE_FLAG_USERUSED_ACCOUNTED);

		/* Allocate the user/groupused objects if necessary. */
		if (os->os_userused_dnode->dn_type == DMU_OT_NONE) {
			VERIFY(0 == zap_create_claim(&os->os,
			    DMU_USERUSED_OBJECT,
			    DMU_OT_USERGROUP_USED, DMU_OT_NONE, 0, tx));
			VERIFY(0 == zap_create_claim(&os->os,
			    DMU_GROUPUSED_OBJECT,
			    DMU_OT_USERGROUP_USED, DMU_OT_NONE, 0, tx));
		}

		/*
		 * If the object was not previously
		 * accounted, pretend that it was free.
		 */
		if (!(dn->dn_oldphys->dn_flags &
		    DNODE_FLAG_USERUSED_ACCOUNTED)) {
			bzero(dn->dn_oldphys, sizeof (dnode_phys_t));
		}

		/*
		 * If the object was freed, use the previous bonustype.
		 */
		bonustype = dn->dn_phys->dn_bonustype ?
		    dn->dn_phys->dn_bonustype : dn->dn_oldphys->dn_bonustype;
		ASSERT(dn->dn_phys->dn_type != 0 ||
		    (bcmp(DN_BONUS(dn->dn_phys), zerobuf,
		    DN_MAX_BONUSLEN) == 0 &&
		    DN_USED_BYTES(dn->dn_phys) == 0));
		ASSERT(dn->dn_oldphys->dn_type != 0 ||
		    (bcmp(DN_BONUS(dn->dn_oldphys), zerobuf,
		    DN_MAX_BONUSLEN) == 0 &&
		    DN_USED_BYTES(dn->dn_oldphys) == 0));
		used_cbs[os->os_phys->os_type](&os->os, bonustype,
		    DN_BONUS(dn->dn_oldphys), DN_BONUS(dn->dn_phys),
		    DN_USED_BYTES(dn->dn_oldphys),
		    DN_USED_BYTES(dn->dn_phys), tx);

		/*
		 * The mutex is needed here for interlock with dnode_allocate.
		 */
		mutex_enter(&dn->dn_mtx);
		zio_buf_free(dn->dn_oldphys, sizeof (dnode_phys_t));
		dn->dn_oldphys = NULL;
		mutex_exit(&dn->dn_mtx);

		list_remove(list, dn);
		dnode_rele(dn, list);
	}
}

boolean_t
dmu_objset_userspace_present(objset_t *os)
{
	return (os->os->os_phys->os_flags &
	    OBJSET_FLAG_USERACCOUNTING_COMPLETE);
}

int
dmu_objset_userspace_upgrade(objset_t *os)
{
	uint64_t obj;
	int err = 0;

	if (dmu_objset_userspace_present(os))
		return (0);
	if (!dmu_objset_userused_enabled(os->os))
		return (ENOTSUP);
	if (dmu_objset_is_snapshot(os))
		return (EINVAL);

	/*
	 * We simply need to mark every object dirty, so that it will be
	 * synced out and now accounted.  If this is called
	 * concurrently, or if we already did some work before crashing,
	 * that's fine, since we track each object's accounted state
	 * independently.
	 */

	for (obj = 0; err == 0; err = dmu_object_next(os, &obj, FALSE, 0)) {
		dmu_tx_t *tx = dmu_tx_create(os);
		dmu_buf_t *db;
		int objerr;

		if (issig(JUSTLOOKING) && issig(FORREAL))
			return (EINTR);

		objerr = dmu_bonus_hold(os, obj, FTAG, &db);
		if (objerr)
			continue;
		dmu_tx_hold_bonus(tx, obj);
		objerr = dmu_tx_assign(tx, TXG_WAIT);
		if (objerr) {
			dmu_tx_abort(tx);
			continue;
		}
		dmu_buf_will_dirty(db, tx);
		dmu_buf_rele(db, FTAG);
		dmu_tx_commit(tx);
	}

	os->os->os_flags |= OBJSET_FLAG_USERACCOUNTING_COMPLETE;
	txg_wait_synced(dmu_objset_pool(os), 0);
	return (0);
}

void
dmu_objset_space(objset_t *os, uint64_t *refdbytesp, uint64_t *availbytesp,
    uint64_t *usedobjsp, uint64_t *availobjsp)
{
	dsl_dataset_space(os->os->os_dsl_dataset, refdbytesp, availbytesp,
	    usedobjsp, availobjsp);
}

uint64_t
dmu_objset_fsid_guid(objset_t *os)
{
	return (dsl_dataset_fsid_guid(os->os->os_dsl_dataset));
}

void
dmu_objset_fast_stat(objset_t *os, dmu_objset_stats_t *stat)
{
	stat->dds_type = os->os->os_phys->os_type;
	if (os->os->os_dsl_dataset)
		dsl_dataset_fast_stat(os->os->os_dsl_dataset, stat);
}

void
dmu_objset_stats(objset_t *os, nvlist_t *nv)
{
	ASSERT(os->os->os_dsl_dataset ||
	    os->os->os_phys->os_type == DMU_OST_META);

	if (os->os->os_dsl_dataset != NULL)
		dsl_dataset_stats(os->os->os_dsl_dataset, nv);

	dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_TYPE,
	    os->os->os_phys->os_type);
	dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_USERACCOUNTING,
	    dmu_objset_userspace_present(os));
}

int
dmu_objset_is_snapshot(objset_t *os)
{
	if (os->os->os_dsl_dataset != NULL)
		return (dsl_dataset_is_snapshot(os->os->os_dsl_dataset));
	else
		return (B_FALSE);
}

int
dmu_snapshot_realname(objset_t *os, char *name, char *real, int maxlen,
    boolean_t *conflict)
{
	dsl_dataset_t *ds = os->os->os_dsl_dataset;
	uint64_t ignored;

	if (ds->ds_phys->ds_snapnames_zapobj == 0)
		return (ENOENT);

	return (zap_lookup_norm(ds->ds_dir->dd_pool->dp_meta_objset,
	    ds->ds_phys->ds_snapnames_zapobj, name, 8, 1, &ignored, MT_FIRST,
	    real, maxlen, conflict));
}

int
dmu_snapshot_list_next(objset_t *os, int namelen, char *name,
    uint64_t *idp, uint64_t *offp, boolean_t *case_conflict)
{
	dsl_dataset_t *ds = os->os->os_dsl_dataset;
	zap_cursor_t cursor;
	zap_attribute_t attr;

	if (ds->ds_phys->ds_snapnames_zapobj == 0)
		return (ENOENT);

	zap_cursor_init_serialized(&cursor,
	    ds->ds_dir->dd_pool->dp_meta_objset,
	    ds->ds_phys->ds_snapnames_zapobj, *offp);

	if (zap_cursor_retrieve(&cursor, &attr) != 0) {
		zap_cursor_fini(&cursor);
		return (ENOENT);
	}

	if (strlen(attr.za_name) + 1 > namelen) {
		zap_cursor_fini(&cursor);
		return (ENAMETOOLONG);
	}

	(void) strcpy(name, attr.za_name);
	if (idp)
		*idp = attr.za_first_integer;
	if (case_conflict)
		*case_conflict = attr.za_normalization_conflict;
	zap_cursor_advance(&cursor);
	*offp = zap_cursor_serialize(&cursor);
	zap_cursor_fini(&cursor);

	return (0);
}

int
dmu_dir_list_next(objset_t *os, int namelen, char *name,
    uint64_t *idp, uint64_t *offp)
{
	dsl_dir_t *dd = os->os->os_dsl_dataset->ds_dir;
	zap_cursor_t cursor;
	zap_attribute_t attr;

	/* there is no next dir on a snapshot! */
	if (os->os->os_dsl_dataset->ds_object !=
	    dd->dd_phys->dd_head_dataset_obj)
		return (ENOENT);

	zap_cursor_init_serialized(&cursor,
	    dd->dd_pool->dp_meta_objset,
	    dd->dd_phys->dd_child_dir_zapobj, *offp);

	if (zap_cursor_retrieve(&cursor, &attr) != 0) {
		zap_cursor_fini(&cursor);
		return (ENOENT);
	}

	if (strlen(attr.za_name) + 1 > namelen) {
		zap_cursor_fini(&cursor);
		return (ENAMETOOLONG);
	}

	(void) strcpy(name, attr.za_name);
	if (idp)
		*idp = attr.za_first_integer;
	zap_cursor_advance(&cursor);
	*offp = zap_cursor_serialize(&cursor);
	zap_cursor_fini(&cursor);

	return (0);
}

struct findarg {
	int (*func)(char *, void *);
	void *arg;
};

/* ARGSUSED */
static int
findfunc(spa_t *spa, uint64_t dsobj, const char *dsname, void *arg)
{
	struct findarg *fa = arg;
	return (fa->func((char *)dsname, fa->arg));
}

/*
 * Find all objsets under name, and for each, call 'func(child_name, arg)'.
 * Perhaps change all callers to use dmu_objset_find_spa()?
 */
int
dmu_objset_find(char *name, int func(char *, void *), void *arg, int flags)
{
	struct findarg fa;
	fa.func = func;
	fa.arg = arg;
	return (dmu_objset_find_spa(NULL, name, findfunc, &fa, flags));
}

/*
 * Find all objsets under name, call func on each
 */
int
dmu_objset_find_spa(spa_t *spa, const char *name,
    int func(spa_t *, uint64_t, const char *, void *), void *arg, int flags)
{
	dsl_dir_t *dd;
	dsl_pool_t *dp;
	dsl_dataset_t *ds;
	zap_cursor_t zc;
	zap_attribute_t *attr;
	char *child;
	uint64_t thisobj;
	int err;

	if (name == NULL)
		name = spa_name(spa);
	err = dsl_dir_open_spa(spa, name, FTAG, &dd, NULL);
	if (err)
		return (err);

	/* Don't visit hidden ($MOS & $ORIGIN) objsets. */
	if (dd->dd_myname[0] == '$') {
		dsl_dir_close(dd, FTAG);
		return (0);
	}

	thisobj = dd->dd_phys->dd_head_dataset_obj;
	attr = kmem_alloc(sizeof (zap_attribute_t), KM_SLEEP);
	dp = dd->dd_pool;

	/*
	 * Iterate over all children.
	 */
	if (flags & DS_FIND_CHILDREN) {
		for (zap_cursor_init(&zc, dp->dp_meta_objset,
		    dd->dd_phys->dd_child_dir_zapobj);
		    zap_cursor_retrieve(&zc, attr) == 0;
		    (void) zap_cursor_advance(&zc)) {
			ASSERT(attr->za_integer_length == sizeof (uint64_t));
			ASSERT(attr->za_num_integers == 1);

			child = kmem_alloc(MAXPATHLEN, KM_SLEEP);
			(void) strcpy(child, name);
			(void) strcat(child, "/");
			(void) strcat(child, attr->za_name);
			err = dmu_objset_find_spa(spa, child, func, arg, flags);
			kmem_free(child, MAXPATHLEN);
			if (err)
				break;
		}
		zap_cursor_fini(&zc);

		if (err) {
			dsl_dir_close(dd, FTAG);
			kmem_free(attr, sizeof (zap_attribute_t));
			return (err);
		}
	}

	/*
	 * Iterate over all snapshots.
	 */
	if (flags & DS_FIND_SNAPSHOTS) {
		if (!dsl_pool_sync_context(dp))
			rw_enter(&dp->dp_config_rwlock, RW_READER);
		err = dsl_dataset_hold_obj(dp, thisobj, FTAG, &ds);
		if (!dsl_pool_sync_context(dp))
			rw_exit(&dp->dp_config_rwlock);

		if (err == 0) {
			uint64_t snapobj = ds->ds_phys->ds_snapnames_zapobj;
			dsl_dataset_rele(ds, FTAG);

			for (zap_cursor_init(&zc, dp->dp_meta_objset, snapobj);
			    zap_cursor_retrieve(&zc, attr) == 0;
			    (void) zap_cursor_advance(&zc)) {
				ASSERT(attr->za_integer_length ==
				    sizeof (uint64_t));
				ASSERT(attr->za_num_integers == 1);

				child = kmem_alloc(MAXPATHLEN, KM_SLEEP);
				(void) strcpy(child, name);
				(void) strcat(child, "@");
				(void) strcat(child, attr->za_name);
				err = func(spa, attr->za_first_integer,
				    child, arg);
				kmem_free(child, MAXPATHLEN);
				if (err)
					break;
			}
			zap_cursor_fini(&zc);
		}
	}

	dsl_dir_close(dd, FTAG);
	kmem_free(attr, sizeof (zap_attribute_t));

	if (err)
		return (err);

	/*
	 * Apply to self if appropriate.
	 */
	err = func(spa, thisobj, name, arg);
	return (err);
}

/* ARGSUSED */
int
dmu_objset_prefetch(char *name, void *arg)
{
	dsl_dataset_t *ds;

	if (dsl_dataset_hold(name, FTAG, &ds))
		return (0);

	if (!BP_IS_HOLE(&ds->ds_phys->ds_bp)) {
		mutex_enter(&ds->ds_opening_lock);
		if (!dsl_dataset_get_user_ptr(ds)) {
			uint32_t aflags = ARC_NOWAIT | ARC_PREFETCH;
			zbookmark_t zb;

			zb.zb_objset = ds->ds_object;
			zb.zb_object = 0;
			zb.zb_level = -1;
			zb.zb_blkid = 0;

			(void) arc_read_nolock(NULL, dsl_dataset_get_spa(ds),
			    &ds->ds_phys->ds_bp, NULL, NULL,
			    ZIO_PRIORITY_ASYNC_READ,
			    ZIO_FLAG_CANFAIL | ZIO_FLAG_SPECULATIVE,
			    &aflags, &zb);
		}
		mutex_exit(&ds->ds_opening_lock);
	}

	dsl_dataset_rele(ds, FTAG);
	return (0);
}

void
dmu_objset_set_user(objset_t *os, void *user_ptr)
{
	ASSERT(MUTEX_HELD(&os->os->os_user_ptr_lock));
	os->os->os_user_ptr = user_ptr;
}

void *
dmu_objset_get_user(objset_t *os)
{
	ASSERT(MUTEX_HELD(&os->os->os_user_ptr_lock));
	return (os->os->os_user_ptr);
}
