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

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_tx.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_synctask.h>
#include <sys/spa.h>
#include <sys/zio_checksum.h> /* for the default checksum value */
#include <sys/zap.h>
#include <sys/fs/zfs.h>

#include "zfs_prop.h"

static int
dodefault(const char *propname, int intsz, int numint, void *buf)
{
	zfs_prop_t prop;

	/*
	 * The setonce properties are read-only, BUT they still
	 * have a default value that can be used as the initial
	 * value.
	 */
	if ((prop = zfs_name_to_prop(propname)) == ZPROP_INVAL ||
	    (zfs_prop_readonly(prop) && !zfs_prop_setonce(prop)))
		return (ENOENT);

	if (zfs_prop_get_type(prop) == PROP_TYPE_STRING) {
		if (intsz != 1)
			return (EOVERFLOW);
		(void) strncpy(buf, zfs_prop_default_string(prop),
		    numint);
	} else {
		if (intsz != 8 || numint < 1)
			return (EOVERFLOW);

		*(uint64_t *)buf = zfs_prop_default_numeric(prop);
	}

	return (0);
}

int
dsl_prop_get_dd(dsl_dir_t *dd, const char *propname,
    int intsz, int numint, void *buf, char *setpoint)
{
	int err = ENOENT;
	objset_t *mos = dd->dd_pool->dp_meta_objset;
	zfs_prop_t prop;

	ASSERT(RW_LOCK_HELD(&dd->dd_pool->dp_config_rwlock));

	if (setpoint)
		setpoint[0] = '\0';

	prop = zfs_name_to_prop(propname);

	/*
	 * Note: dd may be NULL, therefore we shouldn't dereference it
	 * ouside this loop.
	 */
	for (; dd != NULL; dd = dd->dd_parent) {
		ASSERT(RW_LOCK_HELD(&dd->dd_pool->dp_config_rwlock));
		err = zap_lookup(mos, dd->dd_phys->dd_props_zapobj,
		    propname, intsz, numint, buf);
		if (err != ENOENT) {
			if (setpoint)
				dsl_dir_name(dd, setpoint);
			break;
		}

		/*
		 * Break out of this loop for non-inheritable properties.
		 */
		if (prop != ZPROP_INVAL && !zfs_prop_inheritable(prop))
			break;
	}
	if (err == ENOENT)
		err = dodefault(propname, intsz, numint, buf);

	return (err);
}

int
dsl_prop_get_ds(dsl_dataset_t *ds, const char *propname,
    int intsz, int numint, void *buf, char *setpoint)
{
	ASSERT(RW_LOCK_HELD(&ds->ds_dir->dd_pool->dp_config_rwlock));

	if (ds->ds_phys->ds_props_obj) {
		int err = zap_lookup(ds->ds_dir->dd_pool->dp_meta_objset,
		    ds->ds_phys->ds_props_obj, propname, intsz, numint, buf);
		if (err != ENOENT) {
			if (setpoint)
				dsl_dataset_name(ds, setpoint);
			return (err);
		}
	}

	return (dsl_prop_get_dd(ds->ds_dir, propname,
	    intsz, numint, buf, setpoint));
}

/*
 * Register interest in the named property.  We'll call the callback
 * once to notify it of the current property value, and again each time
 * the property changes, until this callback is unregistered.
 *
 * Return 0 on success, errno if the prop is not an integer value.
 */
int
dsl_prop_register(dsl_dataset_t *ds, const char *propname,
    dsl_prop_changed_cb_t *callback, void *cbarg)
{
	dsl_dir_t *dd = ds->ds_dir;
	dsl_pool_t *dp = dd->dd_pool;
	uint64_t value;
	dsl_prop_cb_record_t *cbr;
	int err;
	int need_rwlock;

	need_rwlock = !RW_WRITE_HELD(&dp->dp_config_rwlock);
	if (need_rwlock)
		rw_enter(&dp->dp_config_rwlock, RW_READER);

	err = dsl_prop_get_ds(ds, propname, 8, 1, &value, NULL);
	if (err != 0) {
		if (need_rwlock)
			rw_exit(&dp->dp_config_rwlock);
		return (err);
	}

	cbr = kmem_alloc(sizeof (dsl_prop_cb_record_t), KM_SLEEP);
	cbr->cbr_ds = ds;
	cbr->cbr_propname = kmem_alloc(strlen(propname)+1, KM_SLEEP);
	(void) strcpy((char *)cbr->cbr_propname, propname);
	cbr->cbr_func = callback;
	cbr->cbr_arg = cbarg;
	mutex_enter(&dd->dd_lock);
	list_insert_head(&dd->dd_prop_cbs, cbr);
	mutex_exit(&dd->dd_lock);

	cbr->cbr_func(cbr->cbr_arg, value);

	VERIFY(0 == dsl_dir_open_obj(dp, dd->dd_object,
	    NULL, cbr, &dd));
	if (need_rwlock)
		rw_exit(&dp->dp_config_rwlock);
	/* Leave dir open until this callback is unregistered */
	return (0);
}

int
dsl_prop_get(const char *dsname, const char *propname,
    int intsz, int numints, void *buf, char *setpoint)
{
	dsl_dataset_t *ds;
	int err;

	err = dsl_dataset_hold(dsname, FTAG, &ds);
	if (err)
		return (err);

	rw_enter(&ds->ds_dir->dd_pool->dp_config_rwlock, RW_READER);
	err = dsl_prop_get_ds(ds, propname, intsz, numints, buf, setpoint);
	rw_exit(&ds->ds_dir->dd_pool->dp_config_rwlock);

	dsl_dataset_rele(ds, FTAG);
	return (err);
}

/*
 * Get the current property value.  It may have changed by the time this
 * function returns, so it is NOT safe to follow up with
 * dsl_prop_register() and assume that the value has not changed in
 * between.
 *
 * Return 0 on success, ENOENT if ddname is invalid.
 */
int
dsl_prop_get_integer(const char *ddname, const char *propname,
    uint64_t *valuep, char *setpoint)
{
	return (dsl_prop_get(ddname, propname, 8, 1, valuep, setpoint));
}

/*
 * Unregister this callback.  Return 0 on success, ENOENT if ddname is
 * invalid, ENOMSG if no matching callback registered.
 */
int
dsl_prop_unregister(dsl_dataset_t *ds, const char *propname,
    dsl_prop_changed_cb_t *callback, void *cbarg)
{
	dsl_dir_t *dd = ds->ds_dir;
	dsl_prop_cb_record_t *cbr;

	mutex_enter(&dd->dd_lock);
	for (cbr = list_head(&dd->dd_prop_cbs);
	    cbr; cbr = list_next(&dd->dd_prop_cbs, cbr)) {
		if (cbr->cbr_ds == ds &&
		    cbr->cbr_func == callback &&
		    cbr->cbr_arg == cbarg &&
		    strcmp(cbr->cbr_propname, propname) == 0)
			break;
	}

	if (cbr == NULL) {
		mutex_exit(&dd->dd_lock);
		return (ENOMSG);
	}

	list_remove(&dd->dd_prop_cbs, cbr);
	mutex_exit(&dd->dd_lock);
	kmem_free((void*)cbr->cbr_propname, strlen(cbr->cbr_propname)+1);
	kmem_free(cbr, sizeof (dsl_prop_cb_record_t));

	/* Clean up from dsl_prop_register */
	dsl_dir_close(dd, cbr);
	return (0);
}

/*
 * Return the number of callbacks that are registered for this dataset.
 */
int
dsl_prop_numcb(dsl_dataset_t *ds)
{
	dsl_dir_t *dd = ds->ds_dir;
	dsl_prop_cb_record_t *cbr;
	int num = 0;

	mutex_enter(&dd->dd_lock);
	for (cbr = list_head(&dd->dd_prop_cbs);
	    cbr; cbr = list_next(&dd->dd_prop_cbs, cbr)) {
		if (cbr->cbr_ds == ds)
			num++;
	}
	mutex_exit(&dd->dd_lock);

	return (num);
}

static void
dsl_prop_changed_notify(dsl_pool_t *dp, uint64_t ddobj,
    const char *propname, uint64_t value, int first)
{
	dsl_dir_t *dd;
	dsl_prop_cb_record_t *cbr;
	objset_t *mos = dp->dp_meta_objset;
	zap_cursor_t zc;
	zap_attribute_t *za;
	int err;
	uint64_t dummyval;

	ASSERT(RW_WRITE_HELD(&dp->dp_config_rwlock));
	err = dsl_dir_open_obj(dp, ddobj, NULL, FTAG, &dd);
	if (err)
		return;

	if (!first) {
		/*
		 * If the prop is set here, then this change is not
		 * being inherited here or below; stop the recursion.
		 */
		err = zap_lookup(mos, dd->dd_phys->dd_props_zapobj, propname,
		    8, 1, &dummyval);
		if (err == 0) {
			dsl_dir_close(dd, FTAG);
			return;
		}
		ASSERT3U(err, ==, ENOENT);
	}

	mutex_enter(&dd->dd_lock);
	for (cbr = list_head(&dd->dd_prop_cbs); cbr;
	    cbr = list_next(&dd->dd_prop_cbs, cbr)) {
		uint64_t propobj = cbr->cbr_ds->ds_phys->ds_props_obj;

		if (strcmp(cbr->cbr_propname, propname) != 0)
			continue;

		/*
		 * If the property is set on this ds, then it is not
		 * inherited here; don't call the callback.
		 */
		if (propobj && 0 == zap_lookup(mos, propobj, propname,
		    8, 1, &dummyval))
			continue;

		cbr->cbr_func(cbr->cbr_arg, value);
	}
	mutex_exit(&dd->dd_lock);

	za = kmem_alloc(sizeof (zap_attribute_t), KM_SLEEP);
	for (zap_cursor_init(&zc, mos,
	    dd->dd_phys->dd_child_dir_zapobj);
	    zap_cursor_retrieve(&zc, za) == 0;
	    zap_cursor_advance(&zc)) {
		dsl_prop_changed_notify(dp, za->za_first_integer,
		    propname, value, FALSE);
	}
	kmem_free(za, sizeof (zap_attribute_t));
	zap_cursor_fini(&zc);
	dsl_dir_close(dd, FTAG);
}

struct prop_set_arg {
	const char *name;
	int intsz;
	int numints;
	const void *buf;
};


static void
dsl_prop_set_sync(void *arg1, void *arg2, cred_t *cr, dmu_tx_t *tx)
{
	dsl_dataset_t *ds = arg1;
	struct prop_set_arg *psa = arg2;
	objset_t *mos = ds->ds_dir->dd_pool->dp_meta_objset;
	uint64_t zapobj, intval;
	int isint;
	char valbuf[32];
	char *valstr;

	isint = (dodefault(psa->name, 8, 1, &intval) == 0);

	if (dsl_dataset_is_snapshot(ds)) {
		ASSERT(spa_version(ds->ds_dir->dd_pool->dp_spa) >=
		    SPA_VERSION_SNAP_PROPS);
		if (ds->ds_phys->ds_props_obj == 0) {
			dmu_buf_will_dirty(ds->ds_dbuf, tx);
			ds->ds_phys->ds_props_obj =
			    zap_create(mos,
			    DMU_OT_DSL_PROPS, DMU_OT_NONE, 0, tx);
		}
		zapobj = ds->ds_phys->ds_props_obj;
	} else {
		zapobj = ds->ds_dir->dd_phys->dd_props_zapobj;
	}

	if (psa->numints == 0) {
		int err = zap_remove(mos, zapobj, psa->name, tx);
		ASSERT(err == 0 || err == ENOENT);
		if (isint) {
			VERIFY(0 == dsl_prop_get_ds(ds,
			    psa->name, 8, 1, &intval, NULL));
		}
	} else {
		VERIFY(0 == zap_update(mos, zapobj, psa->name,
		    psa->intsz, psa->numints, psa->buf, tx));
		if (isint)
			intval = *(uint64_t *)psa->buf;
	}

	if (isint) {
		if (dsl_dataset_is_snapshot(ds)) {
			dsl_prop_cb_record_t *cbr;
			/*
			 * It's a snapshot; nothing can inherit this
			 * property, so just look for callbacks on this
			 * ds here.
			 */
			mutex_enter(&ds->ds_dir->dd_lock);
			for (cbr = list_head(&ds->ds_dir->dd_prop_cbs); cbr;
			    cbr = list_next(&ds->ds_dir->dd_prop_cbs, cbr)) {
				if (cbr->cbr_ds == ds &&
				    strcmp(cbr->cbr_propname, psa->name) == 0)
					cbr->cbr_func(cbr->cbr_arg, intval);
			}
			mutex_exit(&ds->ds_dir->dd_lock);
		} else {
			dsl_prop_changed_notify(ds->ds_dir->dd_pool,
			    ds->ds_dir->dd_object, psa->name, intval, TRUE);
		}
	}
	if (isint) {
		(void) snprintf(valbuf, sizeof (valbuf),
		    "%lld", (longlong_t)intval);
		valstr = valbuf;
	} else {
		valstr = (char *)psa->buf;
	}
	spa_history_internal_log((psa->numints == 0) ? LOG_DS_INHERIT :
	    LOG_DS_PROPSET, ds->ds_dir->dd_pool->dp_spa, tx, cr,
	    "%s=%s dataset = %llu", psa->name, valstr, ds->ds_object);
}

void
dsl_prop_set_uint64_sync(dsl_dir_t *dd, const char *name, uint64_t val,
    cred_t *cr, dmu_tx_t *tx)
{
	objset_t *mos = dd->dd_pool->dp_meta_objset;
	uint64_t zapobj = dd->dd_phys->dd_props_zapobj;

	ASSERT(dmu_tx_is_syncing(tx));

	VERIFY(0 == zap_update(mos, zapobj, name, sizeof (val), 1, &val, tx));

	dsl_prop_changed_notify(dd->dd_pool, dd->dd_object, name, val, TRUE);

	spa_history_internal_log(LOG_DS_PROPSET, dd->dd_pool->dp_spa, tx, cr,
	    "%s=%llu dataset = %llu", name, (u_longlong_t)val,
	    dd->dd_phys->dd_head_dataset_obj);
}

int
dsl_prop_set(const char *dsname, const char *propname,
    int intsz, int numints, const void *buf)
{
	dsl_dataset_t *ds;
	int err;
	struct prop_set_arg psa;

	/*
	 * We must do these checks before we get to the syncfunc, since
	 * it can't fail.
	 */
	if (strlen(propname) >= ZAP_MAXNAMELEN)
		return (ENAMETOOLONG);
	if (intsz * numints >= ZAP_MAXVALUELEN)
		return (E2BIG);

	err = dsl_dataset_hold(dsname, FTAG, &ds);
	if (err)
		return (err);

	if (dsl_dataset_is_snapshot(ds) &&
	    spa_version(ds->ds_dir->dd_pool->dp_spa) < SPA_VERSION_SNAP_PROPS) {
		dsl_dataset_rele(ds, FTAG);
		return (ENOTSUP);
	}

	psa.name = propname;
	psa.intsz = intsz;
	psa.numints = numints;
	psa.buf = buf;
	err = dsl_sync_task_do(ds->ds_dir->dd_pool,
	    NULL, dsl_prop_set_sync, ds, &psa, 2);

	dsl_dataset_rele(ds, FTAG);
	return (err);
}

/*
 * Iterate over all properties for this dataset and return them in an nvlist.
 */
int
dsl_prop_get_all(objset_t *os, nvlist_t **nvp, boolean_t local)
{
	dsl_dataset_t *ds = os->os->os_dsl_dataset;
	dsl_dir_t *dd = ds->ds_dir;
	boolean_t snapshot = dsl_dataset_is_snapshot(ds);
	int err = 0;
	dsl_pool_t *dp = dd->dd_pool;
	objset_t *mos = dp->dp_meta_objset;
	uint64_t propobj = ds->ds_phys->ds_props_obj;

	VERIFY(nvlist_alloc(nvp, NV_UNIQUE_NAME, KM_SLEEP) == 0);

	if (local && snapshot && !propobj)
		return (0);

	rw_enter(&dp->dp_config_rwlock, RW_READER);
	while (dd != NULL) {
		char setpoint[MAXNAMELEN];
		zap_cursor_t zc;
		zap_attribute_t za;
		dsl_dir_t *dd_next;

		if (propobj) {
			dsl_dataset_name(ds, setpoint);
			dd_next = dd;
		} else {
			dsl_dir_name(dd, setpoint);
			propobj = dd->dd_phys->dd_props_zapobj;
			dd_next = dd->dd_parent;
		}

		for (zap_cursor_init(&zc, mos, propobj);
		    (err = zap_cursor_retrieve(&zc, &za)) == 0;
		    zap_cursor_advance(&zc)) {
			nvlist_t *propval;
			zfs_prop_t prop = zfs_name_to_prop(za.za_name);

			/* Skip non-inheritable properties. */
			if (prop != ZPROP_INVAL &&
			    !zfs_prop_inheritable(prop) &&
			    (dd != ds->ds_dir || (snapshot && dd != dd_next)))
				continue;

			/* Skip properties not valid for this type. */
			if (snapshot && prop != ZPROP_INVAL &&
			    !zfs_prop_valid_for_type(prop, ZFS_TYPE_SNAPSHOT))
				continue;

			/* Skip properties already defined */
			if (nvlist_lookup_nvlist(*nvp, za.za_name,
			    &propval) == 0)
				continue;

			VERIFY(nvlist_alloc(&propval, NV_UNIQUE_NAME,
			    KM_SLEEP) == 0);
			if (za.za_integer_length == 1) {
				/*
				 * String property
				 */
				char *tmp = kmem_alloc(za.za_num_integers,
				    KM_SLEEP);
				err = zap_lookup(mos, propobj,
				    za.za_name, 1, za.za_num_integers, tmp);
				if (err != 0) {
					kmem_free(tmp, za.za_num_integers);
					break;
				}
				VERIFY(nvlist_add_string(propval, ZPROP_VALUE,
				    tmp) == 0);
				kmem_free(tmp, za.za_num_integers);
			} else {
				/*
				 * Integer property
				 */
				ASSERT(za.za_integer_length == 8);
				(void) nvlist_add_uint64(propval, ZPROP_VALUE,
				    za.za_first_integer);
			}

			VERIFY(nvlist_add_string(propval, ZPROP_SOURCE,
			    setpoint) == 0);
			VERIFY(nvlist_add_nvlist(*nvp, za.za_name,
			    propval) == 0);
			nvlist_free(propval);
		}
		zap_cursor_fini(&zc);

		if (err != ENOENT)
			break;
		err = 0;
		/*
		 * If we are just after the props that have been set
		 * locally, then we are done after the first iteration.
		 */
		if (local)
			break;
		dd = dd_next;
		propobj = 0;
	}
	rw_exit(&dp->dp_config_rwlock);

	return (err);
}

void
dsl_prop_nvlist_add_uint64(nvlist_t *nv, zfs_prop_t prop, uint64_t value)
{
	nvlist_t *propval;

	VERIFY(nvlist_alloc(&propval, NV_UNIQUE_NAME, KM_SLEEP) == 0);
	VERIFY(nvlist_add_uint64(propval, ZPROP_VALUE, value) == 0);
	VERIFY(nvlist_add_nvlist(nv, zfs_prop_to_name(prop), propval) == 0);
	nvlist_free(propval);
}

void
dsl_prop_nvlist_add_string(nvlist_t *nv, zfs_prop_t prop, const char *value)
{
	nvlist_t *propval;

	VERIFY(nvlist_alloc(&propval, NV_UNIQUE_NAME, KM_SLEEP) == 0);
	VERIFY(nvlist_add_string(propval, ZPROP_VALUE, value) == 0);
	VERIFY(nvlist_add_nvlist(nv, zfs_prop_to_name(prop), propval) == 0);
	nvlist_free(propval);
}
