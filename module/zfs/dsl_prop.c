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
 */

#include <sys/zfs_context.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_tx.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_synctask.h>
#include <sys/spa.h>
#include <sys/zap.h>
#include <sys/fs/zfs.h>

#include "zfs_prop.h"

#define	ZPROP_INHERIT_SUFFIX "$inherit"
#define	ZPROP_RECVD_SUFFIX "$recvd"

static int
dodefault(const char *propname, int intsz, int numints, void *buf)
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
		    numints);
	} else {
		if (intsz != 8 || numints < 1)
			return (EOVERFLOW);

		*(uint64_t *)buf = zfs_prop_default_numeric(prop);
	}

	return (0);
}

int
dsl_prop_get_dd(dsl_dir_t *dd, const char *propname,
    int intsz, int numints, void *buf, char *setpoint, boolean_t snapshot)
{
	int err = ENOENT;
	dsl_dir_t *target = dd;
	objset_t *mos = dd->dd_pool->dp_meta_objset;
	zfs_prop_t prop;
	boolean_t inheritable;
	boolean_t inheriting = B_FALSE;
	char *inheritstr;
	char *recvdstr;

	ASSERT(RW_LOCK_HELD(&dd->dd_pool->dp_config_rwlock));

	if (setpoint)
		setpoint[0] = '\0';

	prop = zfs_name_to_prop(propname);
	inheritable = (prop == ZPROP_INVAL || zfs_prop_inheritable(prop));
	inheritstr = kmem_asprintf("%s%s", propname, ZPROP_INHERIT_SUFFIX);
	recvdstr = kmem_asprintf("%s%s", propname, ZPROP_RECVD_SUFFIX);

	/*
	 * Note: dd may become NULL, therefore we shouldn't dereference it
	 * after this loop.
	 */
	for (; dd != NULL; dd = dd->dd_parent) {
		ASSERT(RW_LOCK_HELD(&dd->dd_pool->dp_config_rwlock));

		if (dd != target || snapshot) {
			if (!inheritable)
				break;
			inheriting = B_TRUE;
		}

		/* Check for a local value. */
		err = zap_lookup(mos, dd->dd_phys->dd_props_zapobj, propname,
		    intsz, numints, buf);
		if (err != ENOENT) {
			if (setpoint != NULL && err == 0)
				dsl_dir_name(dd, setpoint);
			break;
		}

		/*
		 * Skip the check for a received value if there is an explicit
		 * inheritance entry.
		 */
		err = zap_contains(mos, dd->dd_phys->dd_props_zapobj,
		    inheritstr);
		if (err != 0 && err != ENOENT)
			break;

		if (err == ENOENT) {
			/* Check for a received value. */
			err = zap_lookup(mos, dd->dd_phys->dd_props_zapobj,
			    recvdstr, intsz, numints, buf);
			if (err != ENOENT) {
				if (setpoint != NULL && err == 0) {
					if (inheriting) {
						dsl_dir_name(dd, setpoint);
					} else {
						(void) strcpy(setpoint,
						    ZPROP_SOURCE_VAL_RECVD);
					}
				}
				break;
			}
		}

		/*
		 * If we found an explicit inheritance entry, err is zero even
		 * though we haven't yet found the value, so reinitializing err
		 * at the end of the loop (instead of at the beginning) ensures
		 * that err has a valid post-loop value.
		 */
		err = ENOENT;
	}

	if (err == ENOENT)
		err = dodefault(propname, intsz, numints, buf);

	strfree(inheritstr);
	strfree(recvdstr);

	return (err);
}

int
dsl_prop_get_ds(dsl_dataset_t *ds, const char *propname,
    int intsz, int numints, void *buf, char *setpoint)
{
	zfs_prop_t prop = zfs_name_to_prop(propname);
	boolean_t inheritable;
	boolean_t snapshot;
	uint64_t zapobj;

	ASSERT(RW_LOCK_HELD(&ds->ds_dir->dd_pool->dp_config_rwlock));
	inheritable = (prop == ZPROP_INVAL || zfs_prop_inheritable(prop));
	snapshot = (ds->ds_phys != NULL && dsl_dataset_is_snapshot(ds));
	zapobj = (ds->ds_phys == NULL ? 0 : ds->ds_phys->ds_props_obj);

	if (zapobj != 0) {
		objset_t *mos = ds->ds_dir->dd_pool->dp_meta_objset;
		int err;

		ASSERT(snapshot);

		/* Check for a local value. */
		err = zap_lookup(mos, zapobj, propname, intsz, numints, buf);
		if (err != ENOENT) {
			if (setpoint != NULL && err == 0)
				dsl_dataset_name(ds, setpoint);
			return (err);
		}

		/*
		 * Skip the check for a received value if there is an explicit
		 * inheritance entry.
		 */
		if (inheritable) {
			char *inheritstr = kmem_asprintf("%s%s", propname,
			    ZPROP_INHERIT_SUFFIX);
			err = zap_contains(mos, zapobj, inheritstr);
			strfree(inheritstr);
			if (err != 0 && err != ENOENT)
				return (err);
		}

		if (err == ENOENT) {
			/* Check for a received value. */
			char *recvdstr = kmem_asprintf("%s%s", propname,
			    ZPROP_RECVD_SUFFIX);
			err = zap_lookup(mos, zapobj, recvdstr,
			    intsz, numints, buf);
			strfree(recvdstr);
			if (err != ENOENT) {
				if (setpoint != NULL && err == 0)
					(void) strcpy(setpoint,
					    ZPROP_SOURCE_VAL_RECVD);
				return (err);
			}
		}
	}

	return (dsl_prop_get_dd(ds->ds_dir, propname,
	    intsz, numints, buf, setpoint, snapshot));
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

	if (need_rwlock)
		rw_exit(&dp->dp_config_rwlock);
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

void
dsl_prop_setarg_init_uint64(dsl_prop_setarg_t *psa, const char *propname,
    zprop_source_t source, uint64_t *value)
{
	psa->psa_name = propname;
	psa->psa_source = source;
	psa->psa_intsz = 8;
	psa->psa_numints = 1;
	psa->psa_value = value;

	psa->psa_effective_value = -1ULL;
}

/*
 * Predict the effective value of the given special property if it were set with
 * the given value and source. This is not a general purpose function. It exists
 * only to handle the special requirements of the quota and reservation
 * properties. The fact that these properties are non-inheritable greatly
 * simplifies the prediction logic.
 *
 * Returns 0 on success, a positive error code on failure, or -1 if called with
 * a property not handled by this function.
 */
int
dsl_prop_predict_sync(dsl_dir_t *dd, dsl_prop_setarg_t *psa)
{
	const char *propname = psa->psa_name;
	zfs_prop_t prop = zfs_name_to_prop(propname);
	zprop_source_t source = psa->psa_source;
	objset_t *mos;
	uint64_t zapobj;
	uint64_t version;
	char *recvdstr;
	int err = 0;

	switch (prop) {
	case ZFS_PROP_QUOTA:
	case ZFS_PROP_RESERVATION:
	case ZFS_PROP_REFQUOTA:
	case ZFS_PROP_REFRESERVATION:
		break;
	default:
		return (-1);
	}

	mos = dd->dd_pool->dp_meta_objset;
	zapobj = dd->dd_phys->dd_props_zapobj;
	recvdstr = kmem_asprintf("%s%s", propname, ZPROP_RECVD_SUFFIX);

	version = spa_version(dd->dd_pool->dp_spa);
	if (version < SPA_VERSION_RECVD_PROPS) {
		if (source & ZPROP_SRC_NONE)
			source = ZPROP_SRC_NONE;
		else if (source & ZPROP_SRC_RECEIVED)
			source = ZPROP_SRC_LOCAL;
	}

	switch (source) {
	case ZPROP_SRC_NONE:
		/* Revert to the received value, if any. */
		err = zap_lookup(mos, zapobj, recvdstr, 8, 1,
		    &psa->psa_effective_value);
		if (err == ENOENT)
			psa->psa_effective_value = 0;
		break;
	case ZPROP_SRC_LOCAL:
		psa->psa_effective_value = *(uint64_t *)psa->psa_value;
		break;
	case ZPROP_SRC_RECEIVED:
		/*
		 * If there's no local setting, then the new received value will
		 * be the effective value.
		 */
		err = zap_lookup(mos, zapobj, propname, 8, 1,
		    &psa->psa_effective_value);
		if (err == ENOENT)
			psa->psa_effective_value = *(uint64_t *)psa->psa_value;
		break;
	case (ZPROP_SRC_NONE | ZPROP_SRC_RECEIVED):
		/*
		 * We're clearing the received value, so the local setting (if
		 * it exists) remains the effective value.
		 */
		err = zap_lookup(mos, zapobj, propname, 8, 1,
		    &psa->psa_effective_value);
		if (err == ENOENT)
			psa->psa_effective_value = 0;
		break;
	default:
		cmn_err(CE_PANIC, "unexpected property source: %d", source);
	}

	strfree(recvdstr);

	if (err == ENOENT)
		return (0);

	return (err);
}

#ifdef	ZFS_DEBUG
void
dsl_prop_check_prediction(dsl_dir_t *dd, dsl_prop_setarg_t *psa)
{
	zfs_prop_t prop = zfs_name_to_prop(psa->psa_name);
	uint64_t intval;
	char setpoint[MAXNAMELEN];
	uint64_t version = spa_version(dd->dd_pool->dp_spa);
	int err;

	if (version < SPA_VERSION_RECVD_PROPS) {
		switch (prop) {
		case ZFS_PROP_QUOTA:
		case ZFS_PROP_RESERVATION:
			return;
		}
	}

	err = dsl_prop_get_dd(dd, psa->psa_name, 8, 1, &intval,
	    setpoint, B_FALSE);
	if (err == 0 && intval != psa->psa_effective_value) {
		cmn_err(CE_PANIC, "%s property, source: %x, "
		    "predicted effective value: %llu, "
		    "actual effective value: %llu (setpoint: %s)",
		    psa->psa_name, psa->psa_source,
		    (unsigned long long)psa->psa_effective_value,
		    (unsigned long long)intval, setpoint);
	}
}
#endif

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

	ASSERT(RW_WRITE_HELD(&dp->dp_config_rwlock));
	err = dsl_dir_open_obj(dp, ddobj, NULL, FTAG, &dd);
	if (err)
		return;

	if (!first) {
		/*
		 * If the prop is set here, then this change is not
		 * being inherited here or below; stop the recursion.
		 */
		err = zap_contains(mos, dd->dd_phys->dd_props_zapobj, propname);
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
		if (propobj && 0 == zap_contains(mos, propobj, propname))
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

void
dsl_prop_set_sync(void *arg1, void *arg2, dmu_tx_t *tx)
{
	dsl_dataset_t *ds = arg1;
	dsl_prop_setarg_t *psa = arg2;
	objset_t *mos = ds->ds_dir->dd_pool->dp_meta_objset;
	uint64_t zapobj, intval, dummy;
	int isint;
	char valbuf[32];
	char *valstr = NULL;
	char *inheritstr;
	char *recvdstr;
	char *tbuf = NULL;
	int err;
	uint64_t version = spa_version(ds->ds_dir->dd_pool->dp_spa);
	const char *propname = psa->psa_name;
	zprop_source_t source = psa->psa_source;

	isint = (dodefault(propname, 8, 1, &intval) == 0);

	if (ds->ds_phys != NULL && dsl_dataset_is_snapshot(ds)) {
		ASSERT(version >= SPA_VERSION_SNAP_PROPS);
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

	if (version < SPA_VERSION_RECVD_PROPS) {
		zfs_prop_t prop = zfs_name_to_prop(propname);
		if (prop == ZFS_PROP_QUOTA || prop == ZFS_PROP_RESERVATION)
			return;

		if (source & ZPROP_SRC_NONE)
			source = ZPROP_SRC_NONE;
		else if (source & ZPROP_SRC_RECEIVED)
			source = ZPROP_SRC_LOCAL;
	}

	inheritstr = kmem_asprintf("%s%s", propname, ZPROP_INHERIT_SUFFIX);
	recvdstr = kmem_asprintf("%s%s", propname, ZPROP_RECVD_SUFFIX);

	switch (source) {
	case ZPROP_SRC_NONE:
		/*
		 * revert to received value, if any (inherit -S)
		 * - remove propname
		 * - remove propname$inherit
		 */
		err = zap_remove(mos, zapobj, propname, tx);
		ASSERT(err == 0 || err == ENOENT);
		err = zap_remove(mos, zapobj, inheritstr, tx);
		ASSERT(err == 0 || err == ENOENT);
		break;
	case ZPROP_SRC_LOCAL:
		/*
		 * remove propname$inherit
		 * set propname -> value
		 */
		err = zap_remove(mos, zapobj, inheritstr, tx);
		ASSERT(err == 0 || err == ENOENT);
		VERIFY(0 == zap_update(mos, zapobj, propname,
		    psa->psa_intsz, psa->psa_numints, psa->psa_value, tx));
		break;
	case ZPROP_SRC_INHERITED:
		/*
		 * explicitly inherit
		 * - remove propname
		 * - set propname$inherit
		 */
		err = zap_remove(mos, zapobj, propname, tx);
		ASSERT(err == 0 || err == ENOENT);
		if (version >= SPA_VERSION_RECVD_PROPS &&
		    dsl_prop_get_ds(ds, ZPROP_HAS_RECVD, 8, 1, &dummy,
		    NULL) == 0) {
			dummy = 0;
			err = zap_update(mos, zapobj, inheritstr,
			    8, 1, &dummy, tx);
			ASSERT(err == 0);
		}
		break;
	case ZPROP_SRC_RECEIVED:
		/*
		 * set propname$recvd -> value
		 */
		err = zap_update(mos, zapobj, recvdstr,
		    psa->psa_intsz, psa->psa_numints, psa->psa_value, tx);
		ASSERT(err == 0);
		break;
	case (ZPROP_SRC_NONE | ZPROP_SRC_LOCAL | ZPROP_SRC_RECEIVED):
		/*
		 * clear local and received settings
		 * - remove propname
		 * - remove propname$inherit
		 * - remove propname$recvd
		 */
		err = zap_remove(mos, zapobj, propname, tx);
		ASSERT(err == 0 || err == ENOENT);
		err = zap_remove(mos, zapobj, inheritstr, tx);
		ASSERT(err == 0 || err == ENOENT);
		/* FALLTHRU */
	case (ZPROP_SRC_NONE | ZPROP_SRC_RECEIVED):
		/*
		 * remove propname$recvd
		 */
		err = zap_remove(mos, zapobj, recvdstr, tx);
		ASSERT(err == 0 || err == ENOENT);
		break;
	default:
		cmn_err(CE_PANIC, "unexpected property source: %d", source);
	}

	strfree(inheritstr);
	strfree(recvdstr);

	if (isint) {
		VERIFY(0 == dsl_prop_get_ds(ds, propname, 8, 1, &intval, NULL));

		if (ds->ds_phys != NULL && dsl_dataset_is_snapshot(ds)) {
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
				    strcmp(cbr->cbr_propname, propname) == 0)
					cbr->cbr_func(cbr->cbr_arg, intval);
			}
			mutex_exit(&ds->ds_dir->dd_lock);
		} else {
			dsl_prop_changed_notify(ds->ds_dir->dd_pool,
			    ds->ds_dir->dd_object, propname, intval, TRUE);
		}

		(void) snprintf(valbuf, sizeof (valbuf),
		    "%lld", (longlong_t)intval);
		valstr = valbuf;
	} else {
		if (source == ZPROP_SRC_LOCAL) {
			valstr = (char *)psa->psa_value;
		} else {
			tbuf = kmem_alloc(ZAP_MAXVALUELEN, KM_SLEEP);
			if (dsl_prop_get_ds(ds, propname, 1,
			    ZAP_MAXVALUELEN, tbuf, NULL) == 0)
				valstr = tbuf;
		}
	}

	spa_history_log_internal((source == ZPROP_SRC_NONE ||
	    source == ZPROP_SRC_INHERITED) ? LOG_DS_INHERIT :
	    LOG_DS_PROPSET, ds->ds_dir->dd_pool->dp_spa, tx,
	    "%s=%s dataset = %llu", propname,
	    (valstr == NULL ? "" : valstr), ds->ds_object);

	if (tbuf != NULL)
		kmem_free(tbuf, ZAP_MAXVALUELEN);
}

void
dsl_props_set_sync(void *arg1, void *arg2, dmu_tx_t *tx)
{
	dsl_dataset_t *ds = arg1;
	dsl_props_arg_t *pa = arg2;
	nvlist_t *props = pa->pa_props;
	dsl_prop_setarg_t psa;
	nvpair_t *elem = NULL;

	psa.psa_source = pa->pa_source;

	while ((elem = nvlist_next_nvpair(props, elem)) != NULL) {
		nvpair_t *pair = elem;

		psa.psa_name = nvpair_name(pair);

		if (nvpair_type(pair) == DATA_TYPE_NVLIST) {
			/*
			 * dsl_prop_get_all_impl() returns properties in this
			 * format.
			 */
			nvlist_t *attrs;
			VERIFY(nvpair_value_nvlist(pair, &attrs) == 0);
			VERIFY(nvlist_lookup_nvpair(attrs, ZPROP_VALUE,
			    &pair) == 0);
		}

		if (nvpair_type(pair) == DATA_TYPE_STRING) {
			VERIFY(nvpair_value_string(pair,
			    (char **)&psa.psa_value) == 0);
			psa.psa_intsz = 1;
			psa.psa_numints = strlen(psa.psa_value) + 1;
		} else {
			uint64_t intval;
			VERIFY(nvpair_value_uint64(pair, &intval) == 0);
			psa.psa_intsz = sizeof (intval);
			psa.psa_numints = 1;
			psa.psa_value = &intval;
		}
		dsl_prop_set_sync(ds, &psa, tx);
	}
}

void
dsl_dir_prop_set_uint64_sync(dsl_dir_t *dd, const char *name, uint64_t val,
    dmu_tx_t *tx)
{
	objset_t *mos = dd->dd_pool->dp_meta_objset;
	uint64_t zapobj = dd->dd_phys->dd_props_zapobj;

	ASSERT(dmu_tx_is_syncing(tx));

	VERIFY(0 == zap_update(mos, zapobj, name, sizeof (val), 1, &val, tx));

	dsl_prop_changed_notify(dd->dd_pool, dd->dd_object, name, val, TRUE);

	spa_history_log_internal(LOG_DS_PROPSET, dd->dd_pool->dp_spa, tx,
	    "%s=%llu dataset = %llu", name, (u_longlong_t)val,
	    dd->dd_phys->dd_head_dataset_obj);
}

int
dsl_prop_set(const char *dsname, const char *propname, zprop_source_t source,
    int intsz, int numints, const void *buf)
{
	dsl_dataset_t *ds;
	uint64_t version;
	int err;
	dsl_prop_setarg_t psa;

	/*
	 * We must do these checks before we get to the syncfunc, since
	 * it can't fail.
	 */
	if (strlen(propname) >= ZAP_MAXNAMELEN)
		return (ENAMETOOLONG);

	err = dsl_dataset_hold(dsname, FTAG, &ds);
	if (err)
		return (err);

	version = spa_version(ds->ds_dir->dd_pool->dp_spa);
	if (intsz * numints >= (version < SPA_VERSION_STMF_PROP ?
	    ZAP_OLDMAXVALUELEN : ZAP_MAXVALUELEN)) {
		dsl_dataset_rele(ds, FTAG);
		return (E2BIG);
	}
	if (dsl_dataset_is_snapshot(ds) &&
	    version < SPA_VERSION_SNAP_PROPS) {
		dsl_dataset_rele(ds, FTAG);
		return (ENOTSUP);
	}

	psa.psa_name = propname;
	psa.psa_source = source;
	psa.psa_intsz = intsz;
	psa.psa_numints = numints;
	psa.psa_value = buf;
	psa.psa_effective_value = -1ULL;

	err = dsl_sync_task_do(ds->ds_dir->dd_pool,
	    NULL, dsl_prop_set_sync, ds, &psa, 2);

	dsl_dataset_rele(ds, FTAG);
	return (err);
}

int
dsl_props_set(const char *dsname, zprop_source_t source, nvlist_t *props)
{
	dsl_dataset_t *ds;
	uint64_t version;
	nvpair_t *elem = NULL;
	dsl_props_arg_t pa;
	int err;

	if (err = dsl_dataset_hold(dsname, FTAG, &ds))
		return (err);
	/*
	 * Do these checks before the syncfunc, since it can't fail.
	 */
	version = spa_version(ds->ds_dir->dd_pool->dp_spa);
	while ((elem = nvlist_next_nvpair(props, elem)) != NULL) {
		if (strlen(nvpair_name(elem)) >= ZAP_MAXNAMELEN) {
			dsl_dataset_rele(ds, FTAG);
			return (ENAMETOOLONG);
		}
		if (nvpair_type(elem) == DATA_TYPE_STRING) {
			char *valstr;
			VERIFY(nvpair_value_string(elem, &valstr) == 0);
			if (strlen(valstr) >= (version <
			    SPA_VERSION_STMF_PROP ?
			    ZAP_OLDMAXVALUELEN : ZAP_MAXVALUELEN)) {
				dsl_dataset_rele(ds, FTAG);
				return (E2BIG);
			}
		}
	}

	if (dsl_dataset_is_snapshot(ds) &&
	    version < SPA_VERSION_SNAP_PROPS) {
		dsl_dataset_rele(ds, FTAG);
		return (ENOTSUP);
	}

	pa.pa_props = props;
	pa.pa_source = source;

	err = dsl_sync_task_do(ds->ds_dir->dd_pool,
	    NULL, dsl_props_set_sync, ds, &pa, 2);

	dsl_dataset_rele(ds, FTAG);
	return (err);
}

typedef enum dsl_prop_getflags {
	DSL_PROP_GET_INHERITING = 0x1,	/* searching parent of target ds */
	DSL_PROP_GET_SNAPSHOT = 0x2,	/* snapshot dataset */
	DSL_PROP_GET_LOCAL = 0x4,	/* local properties */
	DSL_PROP_GET_RECEIVED = 0x8	/* received properties */
} dsl_prop_getflags_t;

static int
dsl_prop_get_all_impl(objset_t *mos, uint64_t propobj,
    const char *setpoint, dsl_prop_getflags_t flags, nvlist_t *nv)
{
	zap_cursor_t zc;
	zap_attribute_t za;
	int err = 0;

	for (zap_cursor_init(&zc, mos, propobj);
	    (err = zap_cursor_retrieve(&zc, &za)) == 0;
	    zap_cursor_advance(&zc)) {
		nvlist_t *propval;
		zfs_prop_t prop;
		char buf[ZAP_MAXNAMELEN];
		char *valstr;
		const char *suffix;
		const char *propname;
		const char *source;

		suffix = strchr(za.za_name, '$');

		if (suffix == NULL) {
			/*
			 * Skip local properties if we only want received
			 * properties.
			 */
			if (flags & DSL_PROP_GET_RECEIVED)
				continue;

			propname = za.za_name;
			source = setpoint;
		} else if (strcmp(suffix, ZPROP_INHERIT_SUFFIX) == 0) {
			/* Skip explicitly inherited entries. */
			continue;
		} else if (strcmp(suffix, ZPROP_RECVD_SUFFIX) == 0) {
			if (flags & DSL_PROP_GET_LOCAL)
				continue;

			(void) strncpy(buf, za.za_name, (suffix - za.za_name));
			buf[suffix - za.za_name] = '\0';
			propname = buf;

			if (!(flags & DSL_PROP_GET_RECEIVED)) {
				/* Skip if locally overridden. */
				err = zap_contains(mos, propobj, propname);
				if (err == 0)
					continue;
				if (err != ENOENT)
					break;

				/* Skip if explicitly inherited. */
				valstr = kmem_asprintf("%s%s", propname,
				    ZPROP_INHERIT_SUFFIX);
				err = zap_contains(mos, propobj, valstr);
				strfree(valstr);
				if (err == 0)
					continue;
				if (err != ENOENT)
					break;
			}

			source = ((flags & DSL_PROP_GET_INHERITING) ?
			    setpoint : ZPROP_SOURCE_VAL_RECVD);
		} else {
			/*
			 * For backward compatibility, skip suffixes we don't
			 * recognize.
			 */
			continue;
		}

		prop = zfs_name_to_prop(propname);

		/* Skip non-inheritable properties. */
		if ((flags & DSL_PROP_GET_INHERITING) && prop != ZPROP_INVAL &&
		    !zfs_prop_inheritable(prop))
			continue;

		/* Skip properties not valid for this type. */
		if ((flags & DSL_PROP_GET_SNAPSHOT) && prop != ZPROP_INVAL &&
		    !zfs_prop_valid_for_type(prop, ZFS_TYPE_SNAPSHOT))
			continue;

		/* Skip properties already defined. */
		if (nvlist_exists(nv, propname))
			continue;

		VERIFY(nvlist_alloc(&propval, NV_UNIQUE_NAME, KM_SLEEP) == 0);
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

		VERIFY(nvlist_add_string(propval, ZPROP_SOURCE, source) == 0);
		VERIFY(nvlist_add_nvlist(nv, propname, propval) == 0);
		nvlist_free(propval);
	}
	zap_cursor_fini(&zc);
	if (err == ENOENT)
		err = 0;
	return (err);
}

/*
 * Iterate over all properties for this dataset and return them in an nvlist.
 */
static int
dsl_prop_get_all_ds(dsl_dataset_t *ds, nvlist_t **nvp,
    dsl_prop_getflags_t flags)
{
	dsl_dir_t *dd = ds->ds_dir;
	dsl_pool_t *dp = dd->dd_pool;
	objset_t *mos = dp->dp_meta_objset;
	int err = 0;
	char setpoint[MAXNAMELEN];

	VERIFY(nvlist_alloc(nvp, NV_UNIQUE_NAME, KM_SLEEP) == 0);

	if (dsl_dataset_is_snapshot(ds))
		flags |= DSL_PROP_GET_SNAPSHOT;

	rw_enter(&dp->dp_config_rwlock, RW_READER);

	if (ds->ds_phys->ds_props_obj != 0) {
		ASSERT(flags & DSL_PROP_GET_SNAPSHOT);
		dsl_dataset_name(ds, setpoint);
		err = dsl_prop_get_all_impl(mos, ds->ds_phys->ds_props_obj,
		    setpoint, flags, *nvp);
		if (err)
			goto out;
	}

	for (; dd != NULL; dd = dd->dd_parent) {
		if (dd != ds->ds_dir || (flags & DSL_PROP_GET_SNAPSHOT)) {
			if (flags & (DSL_PROP_GET_LOCAL |
			    DSL_PROP_GET_RECEIVED))
				break;
			flags |= DSL_PROP_GET_INHERITING;
		}
		dsl_dir_name(dd, setpoint);
		err = dsl_prop_get_all_impl(mos, dd->dd_phys->dd_props_zapobj,
		    setpoint, flags, *nvp);
		if (err)
			break;
	}
out:
	rw_exit(&dp->dp_config_rwlock);
	return (err);
}

boolean_t
dsl_prop_get_hasrecvd(objset_t *os)
{
	dsl_dataset_t *ds = os->os_dsl_dataset;
	int rc;
	uint64_t dummy;

	rw_enter(&ds->ds_dir->dd_pool->dp_config_rwlock, RW_READER);
	rc = dsl_prop_get_ds(ds, ZPROP_HAS_RECVD, 8, 1, &dummy, NULL);
	rw_exit(&ds->ds_dir->dd_pool->dp_config_rwlock);
	ASSERT(rc != 0 || spa_version(os->os_spa) >= SPA_VERSION_RECVD_PROPS);
	return (rc == 0);
}

static void
dsl_prop_set_hasrecvd_impl(objset_t *os, zprop_source_t source)
{
	dsl_dataset_t *ds = os->os_dsl_dataset;
	uint64_t dummy = 0;
	dsl_prop_setarg_t psa;

	if (spa_version(os->os_spa) < SPA_VERSION_RECVD_PROPS)
		return;

	dsl_prop_setarg_init_uint64(&psa, ZPROP_HAS_RECVD, source, &dummy);

	(void) dsl_sync_task_do(ds->ds_dir->dd_pool, NULL,
	    dsl_prop_set_sync, ds, &psa, 2);
}

/*
 * Call after successfully receiving properties to ensure that only the first
 * receive on or after SPA_VERSION_RECVD_PROPS blows away local properties.
 */
void
dsl_prop_set_hasrecvd(objset_t *os)
{
	if (dsl_prop_get_hasrecvd(os)) {
		ASSERT(spa_version(os->os_spa) >= SPA_VERSION_RECVD_PROPS);
		return;
	}
	dsl_prop_set_hasrecvd_impl(os, ZPROP_SRC_LOCAL);
}

void
dsl_prop_unset_hasrecvd(objset_t *os)
{
	dsl_prop_set_hasrecvd_impl(os, ZPROP_SRC_NONE);
}

int
dsl_prop_get_all(objset_t *os, nvlist_t **nvp)
{
	return (dsl_prop_get_all_ds(os->os_dsl_dataset, nvp, 0));
}

int
dsl_prop_get_received(objset_t *os, nvlist_t **nvp)
{
	/*
	 * Received properties are not distinguishable from local properties
	 * until the dataset has received properties on or after
	 * SPA_VERSION_RECVD_PROPS.
	 */
	dsl_prop_getflags_t flags = (dsl_prop_get_hasrecvd(os) ?
	    DSL_PROP_GET_RECEIVED : DSL_PROP_GET_LOCAL);
	return (dsl_prop_get_all_ds(os->os_dsl_dataset, nvp, flags));
}

void
dsl_prop_nvlist_add_uint64(nvlist_t *nv, zfs_prop_t prop, uint64_t value)
{
	nvlist_t *propval;
	const char *propname = zfs_prop_to_name(prop);
	uint64_t default_value;

	if (nvlist_lookup_nvlist(nv, propname, &propval) == 0) {
		VERIFY(nvlist_add_uint64(propval, ZPROP_VALUE, value) == 0);
		return;
	}

	VERIFY(nvlist_alloc(&propval, NV_UNIQUE_NAME, KM_SLEEP) == 0);
	VERIFY(nvlist_add_uint64(propval, ZPROP_VALUE, value) == 0);
	/* Indicate the default source if we can. */
	if (dodefault(propname, 8, 1, &default_value) == 0 &&
	    value == default_value) {
		VERIFY(nvlist_add_string(propval, ZPROP_SOURCE, "") == 0);
	}
	VERIFY(nvlist_add_nvlist(nv, propname, propval) == 0);
	nvlist_free(propval);
}

void
dsl_prop_nvlist_add_string(nvlist_t *nv, zfs_prop_t prop, const char *value)
{
	nvlist_t *propval;
	const char *propname = zfs_prop_to_name(prop);

	if (nvlist_lookup_nvlist(nv, propname, &propval) == 0) {
		VERIFY(nvlist_add_string(propval, ZPROP_VALUE, value) == 0);
		return;
	}

	VERIFY(nvlist_alloc(&propval, NV_UNIQUE_NAME, KM_SLEEP) == 0);
	VERIFY(nvlist_add_string(propval, ZPROP_VALUE, value) == 0);
	VERIFY(nvlist_add_nvlist(nv, propname, propval) == 0);
	nvlist_free(propval);
}
