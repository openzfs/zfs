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

/* Async mirror: live properties and txg switches. */

#include <sys/zfs_context.h>
#include <sys/sysevent/eventdefs.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_async_mirror.h>
#include <sys/vdev_async_mirror_impl.h>
#include <sys/uberblock_impl.h>
#include <sys/metaslab_impl.h>
#include <sys/zap.h>
#include <sys/zfeature.h>
#include <sys/dmu.h>
#include <sys/dmu_tx.h>
#include <sys/zio.h>
#include <sys/abd.h>
#include <sys/txg.h>
#include <sys/zthr.h>
#include <sys/dsl_synctask.h>
#include <sys/dsl_scan.h>
#include <sys/dsl_dataset.h>
#include <sys/dmu_traverse.h>
#include <sys/dmu_objset.h>
#include <sys/dnode.h>
#include <sys/arc.h>
#include <sys/zio_checksum.h>
#include <sys/zil.h>

static boolean_t
async_mirror_idle(vdev_async_mirror_t *am, uint64_t guid)
{
	ASSERT(MUTEX_HELD(&am->am_lock));

	boolean_t dtl_child_present = (am->am_dtl_child_guid != 0 &&
	    (am->am_dtl_child_guid == guid ||
	    vdev_lookup_by_guid(am->am_spa->spa_root_vdev,
	    am->am_dtl_child_guid) != NULL));

	return ((am->am_state == AMS_OFF || am->am_state == AMS_DRAINING) &&
	    list_is_empty(&am->am_txgs) && am->am_zap_obj == 0 &&
	    am->am_defer_zap == 0 && !am->am_frozen_entry &&
	    !am->am_off_entry && !dtl_child_present && am->am_thread == NULL);
}

static void
async_mirror_reset(vdev_async_mirror_t *am)
{
	ASSERT(MUTEX_HELD(&am->am_lock));
	ASSERT(async_mirror_idle(am, am->am_on_guid));
	ASSERT0(am->am_held_bytes);
	ASSERT0(am->am_held_dspace);
	ASSERT0(am->am_held_log_bytes);
	ASSERT(!am->am_off);
	ASSERT(!am->am_defer_io);
	ASSERT3U(am->am_persist, ==, AMP_NONE);

	am->am_stop = B_FALSE;
	am->am_pass_txg = 0;
	am->am_fail_from = am->am_fail_open_txg = 0;
	am->am_halt_txg = am->am_dtl_txg = 0;
	am->am_dtl_range_txg = 0;
	am->am_dtl_child_guid = am->am_dtl_child_txg = 0;
	am->am_dtl_label_txg = am->am_dtl_label_write_txg = 0;
	am->am_dtl_ub_txg = am->am_dtl_ub_write_txg = 0;
	am->am_entry_guid = 0;
	am->am_giveup = B_FALSE;
	am->am_giveup_why = AMG_NONE;
	am->am_freeze_read = B_FALSE;
	am->am_giveups = 0;
	am->am_limit_since = 0;
	am->am_frozen_from = 0;
	am->am_frozen_stale = B_FALSE;
	am->am_release_more = am->am_release_capped = B_FALSE;
	am->am_release_futile_txg = 0;
	am->am_ready_dspace = 0;
	am->am_defer_txg = 0;
	am->am_defer_failed = am->am_defer_quick = B_FALSE;
	am->am_pending_bytes = am->am_pending_txgs = 0;
}

static vdev_t *
async_mirror_vdev_parent(vdev_t *vd)
{
	vdev_t *mirror;

	mirror = vd->vdev_parent;
	while (mirror != NULL && mirror->vdev_ops != &vdev_mirror_ops)
		mirror = mirror->vdev_parent;
	return (mirror);
}

static boolean_t
async_mirror_normal_member(vdev_t *vd, const vdev_t *target,
    uint64_t target_read_mode, uint64_t target_write_mode,
    const vdev_t *exclude)
{
	if (vd == exclude)
		return (B_FALSE);

	if (vd->vdev_ops->vdev_op_leaf) {
		if (vd == target)
			return (target_read_mode == VDEV_READ_MODE_NORMAL &&
			    target_write_mode == VDEV_WRITE_MODE_SYNC);
		return (vd->vdev_read_mode == VDEV_READ_MODE_NORMAL &&
		    vd->vdev_async_mirror != VDEV_ASYNC_MIRROR_LAGGING);
	}

	for (uint64_t c = 0; c < vd->vdev_children; c++) {
		if (async_mirror_normal_member(vd->vdev_child[c], target,
		    target_read_mode, target_write_mode, exclude))
			return (B_TRUE);
	}
	return (B_FALSE);
}

static int
async_mirror_normal_member_check(vdev_t *vd, uint64_t read_mode,
    uint64_t write_mode)
{
	vdev_t *mirror = async_mirror_vdev_parent(vd);

	if (mirror == NULL)
		return (SET_ERROR(ENOTSUP));

	if (!async_mirror_normal_member(mirror, vd, read_mode, write_mode,
	    NULL))
		return (SET_ERROR(EINVAL));
	return (0);
}

int
vdev_async_mirror_topology_check(vdev_t *vd)
{
	for (vdev_t *mirror = async_mirror_vdev_parent(vd); mirror != NULL;
	    mirror = async_mirror_vdev_parent(mirror)) {
		if (!async_mirror_normal_member(mirror, NULL,
		    VDEV_READ_MODE_NORMAL, VDEV_WRITE_MODE_SYNC, vd))
			return (SET_ERROR(ZFS_ERR_ASYNC_MIRROR_NO_NORMAL));
	}
	return (0);
}

int
vdev_async_mirror_replace_check(vdev_t *vd, vdev_t *newvd)
{
	boolean_t new_normal = (newvd->vdev_read_mode ==
	    VDEV_READ_MODE_NORMAL && newvd->vdev_async_mirror !=
	    VDEV_ASYNC_MIRROR_LAGGING);

	for (vdev_t *mirror = async_mirror_vdev_parent(vd); mirror != NULL;
	    mirror = async_mirror_vdev_parent(mirror)) {
		if (!new_normal && !async_mirror_normal_member(mirror, NULL,
		    VDEV_READ_MODE_NORMAL, VDEV_WRITE_MODE_SYNC, vd))
			return (SET_ERROR(ZFS_ERR_ASYNC_MIRROR_NO_NORMAL));
	}
	return (0);
}

int
vdev_async_mirror_read_mode_check(spa_t *spa, vdev_t *vd, uint64_t value)
{
	vdev_async_mirror_t *am = spa->spa_async_mirror;
	vdev_t *mirror;
	int error;

	if (value > VDEV_READ_MODE_FALLBACK)
		return (SET_ERROR(EINVAL));
	if (!vd->vdev_ops->vdev_op_leaf || vd->vdev_leaf_zap == 0)
		return (SET_ERROR(ENOTSUP));
	mirror = async_mirror_vdev_parent(vd);
	if (mirror == NULL || vd->vdev_parent != mirror)
		return (SET_ERROR(ENOTSUP));
	if (value == VDEV_READ_MODE_NORMAL) {
		if (vd->vdev_async_mirror == VDEV_ASYNC_MIRROR_LAGGING)
			return (SET_ERROR(EINVAL));
		if (am != NULL) {
			boolean_t busy;

			mutex_enter(&am->am_lock);
			busy = (am->am_lagging == vd ||
			    am->am_on_guid == vd->vdev_guid) &&
			    !async_mirror_idle(am, vd->vdev_guid);
			mutex_exit(&am->am_lock);
			if (busy)
				return (SET_ERROR(EBUSY));
		}
		return (0);
	}

	if (vd->vdev_read_mode == VDEV_READ_MODE_FALLBACK)
		return (0);
	error = async_mirror_normal_member_check(vd,
	    VDEV_READ_MODE_FALLBACK,
	    vd->vdev_async_mirror == VDEV_ASYNC_MIRROR_LAGGING ?
	    VDEV_WRITE_MODE_BEHIND : VDEV_WRITE_MODE_SYNC);
	return (error);
}

int
vdev_async_mirror_props_set_check(void *arg, dmu_tx_t *tx)
{
	nvlist_t *nvp = arg;
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;
	nvlist_t *nvprops = fnvlist_lookup_nvlist(nvp,
	    ZPOOL_VDEV_PROPS_SET_PROPS);
	nvpair_t *elem = NULL;
	uint64_t guid = fnvlist_lookup_uint64(nvp,
	    ZPOOL_VDEV_PROPS_SET_VDEV);

	if (!dmu_tx_is_syncing(tx))
		return (0);

	vdev_t *vd = spa_lookup_by_guid(spa, guid, B_TRUE);
	if (vd == NULL)
		return (SET_ERROR(ENODEV));

	while ((elem = nvlist_next_nvpair(nvprops, elem)) != NULL) {
		if (vdev_name_to_prop(nvpair_name(elem)) ==
		    VDEV_PROP_READ_MODE) {
			uint64_t value;

			if (nvpair_value_uint64(elem, &value) != 0)
				return (SET_ERROR(EINVAL));
			return (vdev_async_mirror_read_mode_check(spa, vd,
			    value));
		}
	}
	return (0);
}

static void
async_mirror_prop_add_list(nvlist_t *nvl, const char *name,
    const char *strval, uint64_t intval, zprop_source_t src)
{
	nvlist_t *propval = fnvlist_alloc();

	fnvlist_add_uint64(propval, ZPROP_SOURCE, src);
	if (strval != NULL)
		fnvlist_add_string(propval, ZPROP_VALUE, strval);
	else
		fnvlist_add_uint64(propval, ZPROP_VALUE, intval);
	fnvlist_add_nvlist(nvl, name, propval);
	nvlist_free(propval);
}

boolean_t
vdev_async_mirror_prop_get(vdev_t *vd, vdev_prop_t prop,
    const char *propname, nvlist_t *outnvl, int *errorp)
{
	if (prop == VDEV_PROP_ASYNC_MIRROR_STATE) {
		if (!vd->vdev_ops->vdev_op_leaf)
			return (B_FALSE);
		async_mirror_prop_add_list(outnvl, propname,
		    vdev_async_mirror_state(vd), 0, ZPROP_SRC_NONE);
		return (B_TRUE);
	}

	if (!vd->vdev_ops->vdev_op_leaf || vd->vdev_parent == NULL ||
	    vd->vdev_parent->vdev_ops != &vdev_mirror_ops)
		return (B_FALSE);

	uint64_t value = (prop == VDEV_PROP_WRITE_MODE) ?
	    (vd->vdev_async_mirror == VDEV_ASYNC_MIRROR_LAGGING ?
	    VDEV_WRITE_MODE_BEHIND : VDEV_WRITE_MODE_SYNC) :
	    (vd->vdev_async_mirror == VDEV_ASYNC_MIRROR_LAGGING ?
	    VDEV_READ_MODE_FALLBACK : vd->vdev_read_mode);
	zprop_source_t src = ZPROP_SRC_DEFAULT;

	if (vd->vdev_async_mirror == VDEV_ASYNC_MIRROR_LAGGING) {
		src = ZPROP_SRC_LOCAL;
	} else {
		uint64_t stored;
		int error = vdev_prop_get_int(vd, prop, &stored);

		if (error == 0)
			src = ZPROP_SRC_LOCAL;
		else if (error != ENOENT) {
			*errorp = error;
			return (B_TRUE);
		}
	}

	async_mirror_prop_add_list(outnvl, propname, NULL, value, src);
	return (B_TRUE);
}

int
vdev_async_mirror_prop_set(spa_t *spa, vdev_t **vdp, uint64_t guid,
    vdev_prop_t prop, nvpair_t *elem, nvlist_t *outnvl)
{
	vdev_t *vd = *vdp;
	uint64_t value = 0;
	int error;

	ASSERT(spa_config_held(spa, SCL_CONFIG, RW_READER));
	if (nvpair_value_uint64(elem, &value) != 0 ||
	    (prop == VDEV_PROP_READ_MODE && value > VDEV_READ_MODE_FALLBACK) ||
	    (prop == VDEV_PROP_WRITE_MODE && value > VDEV_WRITE_MODE_BEHIND)) {
		fnvlist_add_string(outnvl, ZPOOL_VDEV_PROPS_SET_ERROR,
		    prop == VDEV_PROP_READ_MODE ? "invalid read_mode value" :
		    "invalid write_mode value");
		return (SET_ERROR(EINVAL));
	}

	if (prop == VDEV_PROP_READ_MODE) {
		error = vdev_async_mirror_read_mode_check(spa, vd, value);
		if (error == EINVAL) {
			fnvlist_add_string(outnvl, ZPOOL_VDEV_PROPS_SET_ERROR,
			    value == VDEV_READ_MODE_NORMAL ?
			    "read_mode=normal is not allowed while "
			    "write_mode=behind" :
			    "the mirror must keep a member with "
			    "read_mode=normal and write_mode=sync");
		}
		return (error);
	}

	if (!vd->vdev_ops->vdev_op_leaf || vd->vdev_leaf_zap == 0)
		return (SET_ERROR(ENOTSUP));
	error = vdev_async_mirror_read_mode_check(spa, vd,
	    vd->vdev_read_mode);
	if (error != 0)
		return (error);

	uint64_t async_value = value == VDEV_WRITE_MODE_BEHIND ?
	    VDEV_ASYNC_MIRROR_LAGGING : VDEV_ASYNC_MIRROR_OFF;
	uint64_t other = 0;
	error = vdev_async_mirror_set_check(spa, vd, async_value, &other);
	if (error == EEXIST) {
		fnvlist_add_uint64(outnvl,
		    ZPOOL_VDEV_PROPS_SET_ASYNC_MIRROR_LEAF, other);
	} else if (error == EINVAL && value == VDEV_WRITE_MODE_BEHIND) {
		fnvlist_add_string(outnvl, ZPOOL_VDEV_PROPS_SET_ERROR,
		    "the mirror must keep a member with "
		    "read_mode=normal and write_mode=sync");
	}
	if (error != 0)
		return (error);

	spa_config_exit(spa, SCL_CONFIG, FTAG);
	error = vdev_async_mirror_set(spa, guid, async_value);
	spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);
	*vdp = spa_lookup_by_guid(spa, guid, B_TRUE);
	if (*vdp == NULL && error == 0)
		error = SET_ERROR(ENODEV);
	return (error);
}

void
vdev_async_mirror_prop_set_sync(spa_t *spa, vdev_t *vd, vdev_prop_t prop,
    nvpair_t *elem, uint64_t objid, dmu_tx_t *tx)
{
	if (prop == VDEV_PROP_WRITE_MODE)
		return;

	VERIFY3U(prop, ==, VDEV_PROP_READ_MODE);
	uint64_t value = fnvpair_value_uint64(elem);
	const char *name = vdev_prop_to_name(prop);

	if (vd->vdev_async_mirror == VDEV_ASYNC_MIRROR_LAGGING) {
		vd->vdev_read_mode = VDEV_READ_MODE_FALLBACK;
		return;
	}

	const char *unused;
	VERIFY0(vdev_prop_index_to_string(prop, value, &unused));
	VERIFY0(zap_update(spa->spa_meta_objset, objid, name,
	    sizeof (uint64_t), 1, &value, tx));
	vd->vdev_read_mode = value;
	spa_history_log_internal(spa, "vdev set", tx,
	    "vdev_guid=%llu: %s=%lld", (u_longlong_t)vd->vdev_guid,
	    nvpair_name(elem), (longlong_t)value);
}

static int
async_mirror_set_allowed(spa_t *spa, vdev_t *vd, uint64_t value)
{
	vdev_async_mirror_t *am = spa->spa_async_mirror;
	vdev_t *lead = NULL;

	if (!vd->vdev_ops->vdev_op_leaf)
		return (SET_ERROR(ENOTSUP));
	if (value == VDEV_ASYNC_MIRROR_OFF)
		return (0);
	if (am == NULL)
		return (async_mirror_can_start(spa, vd, &lead));

	mutex_enter(&am->am_lock);
	boolean_t same = (am->am_lagging == vd ||
	    (am->am_state == AMS_STARTING && am->am_on_guid == vd->vdev_guid));
	boolean_t busy = same ? am->am_off :
	    !async_mirror_idle(am, vd->vdev_guid);
	mutex_exit(&am->am_lock);

	if (busy)
		return (SET_ERROR(EBUSY));
	if (same)
		return (0);
	return (async_mirror_can_start(spa, vd, &lead));
}

static vdev_t *
async_mirror_other_lagging(vdev_t *vd, const vdev_t *except)
{
	if (vd->vdev_ops->vdev_op_leaf) {
		return ((vd != except && vd->vdev_async_mirror ==
		    VDEV_ASYNC_MIRROR_LAGGING) ? vd : NULL);
	}
	for (uint64_t c = 0; c < vd->vdev_children; c++) {
		vdev_t *other = async_mirror_other_lagging(vd->vdev_child[c],
		    except);

		if (other != NULL)
			return (other);
	}
	return (NULL);
}

static int
async_mirror_set_validate(spa_t *spa, vdev_t *vd, uint64_t *otherp)
{
	vdev_t *other = async_mirror_other_lagging(spa->spa_root_vdev, vd);

	if (other != NULL) {
		if (otherp != NULL)
			*otherp = other->vdev_guid;
		return (SET_ERROR(EEXIST));
	}
	return (async_mirror_normal_member_check(vd,
	    VDEV_READ_MODE_FALLBACK, VDEV_WRITE_MODE_BEHIND));
}

int
vdev_async_mirror_set_check(spa_t *spa, vdev_t *vd, uint64_t value,
    uint64_t *otherp)
{
	ASSERT(spa_config_held(spa, SCL_CONFIG, RW_READER));

	*otherp = 0;
	if (!spa_writeable(spa))
		return (SET_ERROR(EROFS));
	if (value == VDEV_ASYNC_MIRROR_LAGGING && vd->vdev_ops->vdev_op_leaf) {
		int error = async_mirror_set_validate(spa, vd, otherp);

		if (error != 0)
			return (error);
	}
	return (async_mirror_set_allowed(spa, vd, value));
}

typedef struct async_mirror_set_arg {
	uint64_t	asa_guid;
	uint64_t	asa_value;
	uint64_t	asa_txg;
	boolean_t	asa_replace_off;
} async_mirror_set_arg_t;

static int
async_mirror_set_check_sync(void *arg, dmu_tx_t *tx)
{
	async_mirror_set_arg_t *asa = arg;
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;

	if (!dmu_tx_is_syncing(tx))
		return (0);

	vdev_t *vd = spa_lookup_by_guid(spa, asa->asa_guid, B_FALSE);

	if (vd == NULL)
		return (SET_ERROR(ENODEV));
	if (vd->vdev_leaf_zap == 0)
		return (SET_ERROR(ENOTSUP));

	if (asa->asa_value == VDEV_ASYNC_MIRROR_LAGGING &&
	    spa->spa_async_mirror == NULL)
		return (SET_ERROR(ENOTSUP));
	if (asa->asa_value == VDEV_ASYNC_MIRROR_LAGGING) {
		int error = async_mirror_set_validate(spa, vd, NULL);

		if (error != 0)
			return (error);
	}
	return (async_mirror_set_allowed(spa, vd, asa->asa_value));
}

static void
async_mirror_prop_store(spa_t *spa, vdev_t *vd, uint64_t value, dmu_tx_t *tx)
{
	uint64_t write_mode = value == VDEV_ASYNC_MIRROR_LAGGING ?
	    VDEV_WRITE_MODE_BEHIND : VDEV_WRITE_MODE_SYNC;
	int error;
	boolean_t was_behind = (vd->vdev_async_mirror ==
	    VDEV_ASYNC_MIRROR_LAGGING);

	VERIFY0(zap_update(spa_meta_objset(spa), vd->vdev_leaf_zap,
	    vdev_prop_to_name(VDEV_PROP_WRITE_MODE), sizeof (write_mode), 1,
	    &write_mode, tx));
	if (value == VDEV_ASYNC_MIRROR_LAGGING) {
		error = zap_remove(spa_meta_objset(spa), vd->vdev_leaf_zap,
		    vdev_prop_to_name(VDEV_PROP_READ_MODE), tx);
		VERIFY(error == 0 || error == ENOENT);
	} else if (was_behind) {
		uint64_t read_mode = VDEV_READ_MODE_FALLBACK;

		VERIFY0(zap_update(spa_meta_objset(spa), vd->vdev_leaf_zap,
		    vdev_prop_to_name(VDEV_PROP_READ_MODE),
		    sizeof (read_mode), 1, &read_mode, tx));
	}
	vd->vdev_async_mirror = value;
	if (value == VDEV_ASYNC_MIRROR_LAGGING || was_behind)
		vd->vdev_read_mode = VDEV_READ_MODE_FALLBACK;
	spa_history_log_internal(spa, "vdev set", tx,
	    "vdev_guid=%llu: write_mode=%s", (u_longlong_t)vd->vdev_guid,
	    value == VDEV_ASYNC_MIRROR_LAGGING ? "behind" : "sync");
}

static void
async_mirror_prop_off_all(spa_t *spa, dmu_tx_t *tx, vdev_t *except,
    boolean_t leaves_only, const char *reason)
{
	vdev_t *rvd = spa->spa_root_vdev;

	for (uint64_t c = 0; c < rvd->vdev_children; c++) {
		vdev_t *tvd = rvd->vdev_child[c];

		if (tvd->vdev_ops != &vdev_mirror_ops)
			continue;
		for (uint64_t i = 0; i < tvd->vdev_children; i++) {
			vdev_t *vd = tvd->vdev_child[i];

			if (vd == except ||
			    (leaves_only && !vd->vdev_ops->vdev_op_leaf) ||
			    vd->vdev_leaf_zap == 0 ||
			    (vd->vdev_async_mirror !=
			    VDEV_ASYNC_MIRROR_LAGGING))
				continue;
			async_mirror_prop_store(spa, vd,
			    VDEV_ASYNC_MIRROR_OFF, tx);
			if (reason != NULL) {
				spa_history_log_internal(spa,
				    "async mirror", tx,
				    "write_mode=sync for child %llu because %s",
				    (u_longlong_t)vd->vdev_guid, reason);
			}
		}
	}
}

static void
async_mirror_set_sync(void *arg, dmu_tx_t *tx)
{
	async_mirror_set_arg_t *asa = arg;
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;
	vdev_async_mirror_t *am = spa->spa_async_mirror;
	vdev_t *vd = spa_lookup_by_guid(spa, asa->asa_guid, B_FALSE);
	uint64_t txg = dmu_tx_get_txg(tx);
	boolean_t replace_marker = B_FALSE;

	VERIFY3P(vd, !=, NULL);
	asa->asa_txg = txg;
	boolean_t replacing = asa->asa_replace_off;
	if (am != NULL) {
		mutex_enter(&am->am_lock);
		replace_marker = (am->am_replace_guid == vd->vdev_guid);

		if (am->am_failed_guid == vd->vdev_guid) {
			am->am_failed_guid = 0;
			async_mirror_update_stats(am);
		}
		mutex_exit(&am->am_lock);
	}

	if (asa->asa_value == VDEV_ASYNC_MIRROR_LAGGING)
		async_mirror_prop_off_all(spa, tx, vd, B_FALSE, NULL);
	async_mirror_prop_store(spa, vd, asa->asa_value, tx);
	if (asa->asa_value == VDEV_ASYNC_MIRROR_OFF && !replacing &&
	    replace_marker)
		vdev_async_mirror_replace_clear_sync(spa, vd->vdev_guid, tx);

	if (am == NULL) {
		ASSERT3U(asa->asa_value, ==, VDEV_ASYNC_MIRROR_OFF);
		return;
	}

	boolean_t start = B_FALSE;
	boolean_t off = B_FALSE;

	mutex_enter(&am->am_lock);
	if (asa->asa_value == VDEV_ASYNC_MIRROR_LAGGING) {
		if (am->am_state == AMS_OFF || am->am_state == AMS_DRAINING) {
			am->am_on_guid = vd->vdev_guid;
			am->am_on_txg = txg + 1;
			async_mirror_set_state(am, AMS_STARTING);
			start = B_TRUE;
		}
	} else if (am->am_state == AMS_STARTING &&
	    am->am_on_guid == vd->vdev_guid) {
		am->am_on_txg = 0;
		async_mirror_set_state(am, AMS_OFF);
	} else if (am->am_lagging == vd) {
		off = !am->am_off && async_mirror_copying(am->am_state);
		am->am_off = B_TRUE;
		spa_notify_waiters(spa);
	}
	mutex_exit(&am->am_lock);
	if (off)
		spa_event_notify(spa, vd, NULL, ESC_ZFS_ASYNC_MIRROR_OFF);

	if (start && spa_async_mirror_lineage(spa) == 0)
		async_mirror_lineage_start(spa);
}

static int
async_mirror_state_get(spa_t *spa)
{
	int error = 0;

	mutex_enter(&spa->spa_async_lock);
	if (spa->spa_async_mirror == NULL) {
		if (spa->spa_async_suspended != 0) {
			error = SET_ERROR(EBUSY);
		} else {
			vdev_async_mirror_t *am = async_mirror_create(spa);

			spa_async_mirror_stats_init(spa);
			am->am_release_zthr = zthr_create(
			    ASYNC_MIRROR_RELEASE_THREAD,
			    async_mirror_release_thread_check,
			    async_mirror_release_thread, am, minclsyspri);
			am->am_giveup_zthr =
			    async_mirror_giveup_zthr_create(am);

			membar_producer();
			spa->spa_async_mirror = am;
		}
	}
	mutex_exit(&spa->spa_async_lock);
	return (error);
}

typedef struct async_mirror_replace_arg {
	uint64_t	amra_guid;
} async_mirror_replace_arg_t;

static int
async_mirror_replace_check(void *arg, dmu_tx_t *tx)
{
	async_mirror_replace_arg_t *amra = arg;
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;
	uint64_t guid;
	int error;

	if (!dmu_tx_is_syncing(tx))
		return (0);

	error = async_mirror_child_replace_lookup(spa, &guid);
	if (error == ENOENT)
		return (0);
	if (error != 0)
		return (error);

	if (guid == amra->amra_guid)
		return (0);

	vdev_async_mirror_t *am = spa->spa_async_mirror;
	if (am != NULL) {
		mutex_enter(&am->am_lock);
		boolean_t stale = (am->am_replace_cleanup_guid == guid);
		mutex_exit(&am->am_lock);
		if (stale)
			return (0);
	}

	return (SET_ERROR(EBUSY));
}

static void
async_mirror_replace_sync(void *arg, dmu_tx_t *tx)
{
	async_mirror_replace_arg_t *amra = arg;
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;
	uint64_t guid;
	int error;

	error = async_mirror_child_replace_lookup(spa, &guid);
	if (error == 0 && guid != amra->amra_guid) {
		vdev_async_mirror_t *am = spa->spa_async_mirror;
		boolean_t stale = B_FALSE;

		if (am != NULL) {
			mutex_enter(&am->am_lock);
			stale = (am->am_replace_cleanup_guid == guid);
			mutex_exit(&am->am_lock);
		}
		if (stale) {
			async_mirror_child_flags(spa, guid, 0,
			    ASYNC_MIRROR_CHILD_REPLACE, tx);
			error = ENOENT;
		}
	}
	if (error == ENOENT) {
		guid = amra->amra_guid;
		async_mirror_child_flags(spa, guid,
		    ASYNC_MIRROR_CHILD_REPLACE, 0, tx);
		spa_history_log_internal(spa, "async mirror", tx,
		    "replacement of lagging leaf guid %llu is pending",
		    (u_longlong_t)guid);
	} else {
		VERIFY0(error);
		VERIFY3U(guid, ==, amra->amra_guid);
	}

	vdev_async_mirror_t *am = spa->spa_async_mirror;
	if (am != NULL) {
		mutex_enter(&am->am_lock);
		am->am_replace_guid = amra->amra_guid;
		am->am_replace_cleanup_guid = 0;
		mutex_exit(&am->am_lock);
	}
}

int
vdev_async_mirror_replace_pending(spa_t *spa, uint64_t *guidp)
{
	vdev_async_mirror_t *am = spa->spa_async_mirror;

	*guidp = 0;
	if (am == NULL)
		return (ENOENT);

	mutex_enter(&am->am_lock);
	*guidp = am->am_replace_guid;
	mutex_exit(&am->am_lock);
	return (*guidp == 0 ? ENOENT : 0);
}

int
vdev_async_mirror_replace_begin(spa_t *spa, uint64_t guid)
{
	async_mirror_replace_arg_t amra = { .amra_guid = guid };
	int error = async_mirror_state_get(spa);

	if (error != 0)
		return (error);

	error = dsl_sync_task(spa_name(spa), async_mirror_replace_check,
	    async_mirror_replace_sync, &amra, 1, ZFS_SPACE_CHECK_NONE);
	if (error == 0) {
		vdev_async_mirror_t *am = spa->spa_async_mirror;
		mutex_enter(&am->am_lock);
		am->am_replace_cleanup_guid = 0;
		am->am_replace_guid = guid;
		mutex_exit(&am->am_lock);
	}
	return (error);
}

int
vdev_async_mirror_replace_claim(spa_t *spa, uint64_t guid)
{
	vdev_async_mirror_t *am = spa->spa_async_mirror;
	int error;

	if (am == NULL) {
		error = async_mirror_state_get(spa);
		if (error != 0)
			return (error);
		am = spa->spa_async_mirror;
	}

	mutex_enter(&am->am_lock);
	if (am->am_replace_in_progress || (am->am_replace_guid != 0 &&
	    am->am_replace_guid != guid)) {
		error = SET_ERROR(EBUSY);
	} else {
		am->am_replace_guid = guid;
		am->am_replace_in_progress = B_TRUE;
		error = 0;
	}
	mutex_exit(&am->am_lock);
	return (error);
}

void
vdev_async_mirror_replace_release(spa_t *spa, uint64_t guid)
{
	vdev_async_mirror_t *am = spa->spa_async_mirror;

	if (am == NULL)
		return;

	mutex_enter(&am->am_lock);
	if (am->am_replace_in_progress && am->am_replace_guid == guid) {
		am->am_replace_in_progress = B_FALSE;
	}
	mutex_exit(&am->am_lock);
}

void
vdev_async_mirror_replace_clear_sync(spa_t *spa, uint64_t expected_guid,
    dmu_tx_t *tx)
{
	uint64_t guid = 0;
	boolean_t removed = B_FALSE;
	int error;

	error = async_mirror_child_replace_lookup(spa, &guid);
	if (error == 0 && guid == expected_guid) {
		async_mirror_child_flags(spa, guid, 0,
		    ASYNC_MIRROR_CHILD_REPLACE, tx);
		removed = B_TRUE;
	} else if (error != 0 && error != ENOENT) {
		VERIFY0(error);
	}
	vdev_async_mirror_t *am = spa->spa_async_mirror;
	if (am != NULL) {
		mutex_enter(&am->am_lock);
		if ((removed || error == ENOENT) &&
		    am->am_replace_guid == expected_guid) {
			am->am_replace_guid = 0;
			am->am_replace_in_progress = B_FALSE;
		}
		if (am->am_replace_cleanup_guid == expected_guid)
			am->am_replace_cleanup_guid = 0;
		mutex_exit(&am->am_lock);
	}
	if (removed) {
		spa_history_log_internal(spa, "async mirror", tx,
		    "replacement reservation for lagging leaf guid %llu "
		    "is cleared",
		    (u_longlong_t)guid);
	}
}

typedef struct async_mirror_replace_abort_arg {
	uint64_t	amraa_guid;
} async_mirror_replace_abort_arg_t;

static int
async_mirror_replace_abort_check(void *arg, dmu_tx_t *tx)
{
	async_mirror_replace_abort_arg_t *amraa = arg;
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;
	uint64_t guid;
	int error;

	if (!dmu_tx_is_syncing(tx))
		return (0);

	error = async_mirror_child_replace_lookup(spa, &guid);
	if (error == ENOENT || (error == 0 && guid != amraa->amraa_guid))
		return (0);
	return (error);
}

static void
async_mirror_replace_abort_sync(void *arg, dmu_tx_t *tx)
{
	async_mirror_replace_abort_arg_t *amraa = arg;
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;
	uint64_t guid;
	int error;

	error = async_mirror_child_replace_lookup(spa, &guid);
	if (error == ENOENT || (error == 0 && guid != amraa->amraa_guid))
		return;
	VERIFY0(error);
	vdev_async_mirror_replace_clear_sync(spa, amraa->amraa_guid, tx);
}

int
vdev_async_mirror_replace_abort(spa_t *spa, uint64_t guid)
{
	async_mirror_replace_abort_arg_t amraa = { .amraa_guid = guid };

	return (dsl_sync_task(spa_name(spa), async_mirror_replace_abort_check,
	    async_mirror_replace_abort_sync, &amraa, 1, ZFS_SPACE_CHECK_NONE));
}

static int
vdev_async_mirror_set_impl(spa_t *spa, uint64_t guid, uint64_t value,
    boolean_t replace_off)
{
	async_mirror_set_arg_t asa = {
		.asa_guid = guid,
		.asa_value = value,
		.asa_replace_off = replace_off
	};
	int error = 0;

	if (value == VDEV_ASYNC_MIRROR_LAGGING)
		error = async_mirror_state_get(spa);
	if (error == 0) {
		error = dsl_sync_task(spa_name(spa),
		    async_mirror_set_check_sync, async_mirror_set_sync, &asa, 1,
		    ZFS_SPACE_CHECK_EXTRA_RESERVED);
	}

	if (error != 0 || value != VDEV_ASYNC_MIRROR_LAGGING)
		return (error);

	txg_wait_synced(spa_get_dsl(spa), asa.asa_txg + 1);

	spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);
	vdev_t *vd = spa_lookup_by_guid(spa, guid, B_FALSE);

	if (vd == NULL)
		error = SET_ERROR(ENODEV);
	else if (vd->vdev_async_mirror != VDEV_ASYNC_MIRROR_LAGGING)
		error = SET_ERROR(EBUSY);
	else {
		vdev_async_mirror_t *am = spa->spa_async_mirror;
		if (am == NULL) {
			error = SET_ERROR(EBUSY);
		} else {
			mutex_enter(&am->am_lock);
			if (am->am_lagging != vd ||
			    (!async_mirror_copying(am->am_state) &&
			    am->am_state != AMS_FAILED &&
			    am->am_state != AMS_FROZEN))
				error = SET_ERROR(EBUSY);
			mutex_exit(&am->am_lock);
		}
	}
	spa_config_exit(spa, SCL_CONFIG, FTAG);
	return (error);
}

int
vdev_async_mirror_set(spa_t *spa, uint64_t guid, uint64_t value)
{
	return (vdev_async_mirror_set_impl(spa, guid, value, B_FALSE));
}

int
vdev_async_mirror_replace_off(spa_t *spa, uint64_t guid)
{
	return (vdev_async_mirror_set_impl(spa, guid,
	    VDEV_ASYNC_MIRROR_OFF, B_TRUE));
}

const char *
vdev_async_mirror_state(vdev_t *vd)
{
	vdev_async_mirror_t *am = vd->vdev_spa->spa_async_mirror;
	const char *state = "off";

	if (am == NULL)
		return (state);

	mutex_enter(&am->am_lock);
	if (am->am_lagging == vd) {
		if (am->am_state == AMS_FROZEN)
			state = "frozen";
		else if (am->am_state == AMS_FAILED)
			state = "failed";
		else if (am->am_off)
			state = "stopping";
		else if (am->am_pending_txgs > 0)
			state = "behind";
		else
			state = "current";
	} else if (am->am_failed_guid == vd->vdev_guid) {
		state = "failed";
	} else if (am->am_lagging != NULL && am->am_leader == vd) {
		state = "leader";
	} else if (am->am_state == AMS_STARTING &&
	    am->am_on_guid == vd->vdev_guid) {
		state = "starting";
	}
	mutex_exit(&am->am_lock);
	return (state);
}

static void
async_mirror_switch_on(vdev_async_mirror_t *am, dmu_tx_t *tx)
{
	spa_t *spa = am->am_spa;
	uint64_t txg = dmu_tx_get_txg(tx);
	uint64_t guid = am->am_on_guid;
	uint64_t replace_guid;
	uint64_t orphan_dtl_guid = 0;
	vdev_t *lag = spa_lookup_by_guid(spa, guid, B_FALSE);
	vdev_t *lead = NULL;
	int error;

	mutex_enter(&am->am_lock);
	ASSERT3U(am->am_state, ==, AMS_STARTING);
	am->am_on_txg = 0;
	async_mirror_set_state(am, AMS_OFF);
	boolean_t idle = async_mirror_idle(am, guid);
	if (idle)
		orphan_dtl_guid = am->am_dtl_child_guid;
	mutex_exit(&am->am_lock);

	if (lag == NULL || !lag->vdev_ops->vdev_op_leaf)
		error = SET_ERROR(ENODEV);
	else if (!idle)
		error = SET_ERROR(EBUSY);
	else
		error = async_mirror_can_start(spa, lag, &lead);
	if (error == 0 && orphan_dtl_guid != 0) {

		VERIFY3P(vdev_lookup_by_guid(spa->spa_root_vdev,
		    orphan_dtl_guid), ==, NULL);
		async_mirror_child_flags(spa, orphan_dtl_guid, 0,
		    ASYNC_MIRROR_CHILD_DTL_PENDING, tx);
	}
	if (error != 0) {
		if (lag != NULL && lag->vdev_ops->vdev_op_leaf &&
		    lag->vdev_leaf_zap != 0) {
			async_mirror_prop_store(spa, lag,
			    VDEV_ASYNC_MIRROR_OFF, tx);
		}
		spa_history_log_internal(spa, "async mirror", tx,
		    "leaf guid %llu cannot become the lagging child at txg "
		    "%llu, error %d", (u_longlong_t)guid, (u_longlong_t)txg,
		    error);
		return;
	}

	ASSERT3U(spa->spa_uberblock.ub_txg, <, txg);
	mutex_enter(&am->am_lock);
	async_mirror_reset(am);
	am->am_last_ub = spa->spa_uberblock;
	am->am_applied_txg = spa->spa_uberblock.ub_txg;
	am->am_child_ub = spa->spa_uberblock;
	mutex_exit(&am->am_lock);

	ASSERT3U(spa_async_mirror_lineage(spa), !=, 0);

	async_mirror_mark(spa, lag, am->am_applied_txg);

	if (vdev_async_mirror_replace_pending(spa, &replace_guid) == 0 &&
	    replace_guid == guid)
		vdev_async_mirror_replace_clear_sync(spa, guid, tx);

	mutex_enter(&am->am_lock);
	am->am_leader = lead;
	am->am_lagging = lag;
	mutex_exit(&am->am_lock);

	ASSERT3U(am->am_applied_txg, ==, am->am_child_ub.ub_txg);
	spa_config_enter(spa, SCL_STATE, FTAG, RW_READER);
	error = vdev_async_mirror_label_sync(lag, am->am_applied_txg,
	    am->am_child_ub.ub_txg);
	spa_config_exit(spa, SCL_STATE, FTAG);
	if (error != 0) {
		async_mirror_mark(spa, NULL, 0);
		mutex_enter(&am->am_lock);
		async_mirror_set_state(am, AMS_OFF);
		async_mirror_update_stats(am);
		mutex_exit(&am->am_lock);
		async_mirror_prop_store(spa, lag, VDEV_ASYNC_MIRROR_OFF, tx);
		spa_history_log_internal(spa, "async mirror", tx,
		    "leaf guid %llu could not refresh its lagging label at txg "
		    "%llu, error %d", (u_longlong_t)guid,
		    (u_longlong_t)am->am_applied_txg, error);
		return;
	}

	mutex_enter(&am->am_lock);
	async_mirror_set_state(am, AMS_ACTIVE);
	async_mirror_update_stats(am);
	am->am_thread = thread_create(NULL, 0, async_mirror_thread, am, 0, &p0,
	    TS_RUN, minclsyspri);
	mutex_exit(&am->am_lock);
	spa_history_log_internal(spa, "async mirror", tx,
	    "leaf guid %llu is the lagging child from txg %llu",
	    (u_longlong_t)guid, (u_longlong_t)txg);
	zfs_dbgmsg("async mirror: on, lagging child guid %llu at txg %llu",
	    (u_longlong_t)guid, (u_longlong_t)am->am_applied_txg);
}

static void
async_mirror_switch_off(vdev_async_mirror_t *am, dmu_tx_t *tx)
{
	spa_t *spa = am->am_spa;
	uint64_t txg = dmu_tx_get_txg(tx);
	uint64_t last = spa_last_synced_txg(spa) + TXG_CONCURRENT_STATES;
	vdev_t *lag = am->am_lagging;
	uint64_t guid = lag->vdev_guid;

	mutex_enter(&am->am_lock);
	if (am->am_state == AMS_FROZEN) {
		uint64_t frozen_txg = am->am_applied_txg;

		if (!am->am_frozen_entry || am->am_frozen_stale ||
		    !async_mirror_durable(am, am->am_halt_txg) ||
		    !async_mirror_durable(am, am->am_entry_txg) ||
		    !list_is_empty(&am->am_txgs) || am->am_thread != NULL) {
			mutex_exit(&am->am_lock);
			return;
		}
		am->am_fail_from = frozen_txg + 1;
		if (am->am_frozen_from != 0) {
			am->am_fail_from = MIN(am->am_fail_from,
			    am->am_frozen_from);
		}
		am->am_fail_open_txg = last;
		async_mirror_set_state(am, AMS_FAILED);
		async_mirror_update_stats(am);
		mutex_exit(&am->am_lock);
		spa_history_log_internal(spa, "async mirror", tx,
		    "the freeze of the lagging child guid %llu at txg %llu "
		    "ends; it gets writes again from txg %llu",
		    (u_longlong_t)guid, (u_longlong_t)frozen_txg,
		    (u_longlong_t)txg);
		return;
	}

	if (am->am_state == AMS_FAILED) {
		boolean_t done = async_mirror_durable(am, am->am_dtl_txg) &&
		    list_is_empty(&am->am_txgs) && am->am_zap_obj == 0 &&
		    am->am_defer_zap == 0 && am->am_thread == NULL;

		if (done) {
			am->am_off = B_FALSE;
			async_mirror_set_state(am, AMS_DRAINING);
			async_mirror_update_stats(am);
		}
		mutex_exit(&am->am_lock);
		if (done) {
			spa_history_log_internal(spa, "async mirror", tx,
			    "the failed lagging child guid %llu leaves the "
			    "mode", (u_longlong_t)guid);
		}
		return;
	}

	if (am->am_state == AMS_ACTIVE) {
		am->am_pass_txg = last;
		async_mirror_set_state(am, AMS_PASSTHROUGH);
		mutex_exit(&am->am_lock);
		spa_history_log_internal(spa, "async mirror", tx,
		    "the lagging child guid %llu gets writes again from txg "
		    "%llu", (u_longlong_t)guid, (u_longlong_t)txg);
		return;
	}

	async_mirror_txg_t *at = list_head(&am->am_txgs);

	while (at != NULL && at->at_txg <= am->am_applied_txg)
		at = list_next(&am->am_txgs, at);
	ASSERT3U(am->am_state, ==, AMS_PASSTHROUGH);
	if (txg <= am->am_pass_txg || am->am_defer_txg != 0 ||
	    am->am_defer_io || am->am_persist != AMP_NONE ||
	    (at != NULL && at->at_txg <= am->am_pass_txg)) {
		mutex_exit(&am->am_lock);
		return;
	}
	ASSERT0(am->am_pending_txgs);
	am->am_off = B_FALSE;
	async_mirror_set_state(am, AMS_DRAINING);
	am->am_applied_txg = UINT64_MAX;
	boolean_t records = (am->am_zap_obj != 0);
	async_mirror_update_stats(am);
	cv_broadcast(&am->am_cv);
	mutex_exit(&am->am_lock);

	async_mirror_mark(spa, NULL, 0);
	if (records) {
		async_mirror_child_phys_t amc;

		(void) async_mirror_child_get(spa, guid, &amc);
		amc.amc_flags |= ASYNC_MIRROR_CHILD_OFF;
		amc.amc_off_txg = txg;
		async_mirror_child_put(spa, guid, &amc, tx);
		mutex_enter(&am->am_lock);
		am->am_entry_guid = guid;
		am->am_off_entry = B_TRUE;
		mutex_exit(&am->am_lock);
	}
	spa_history_log_internal(spa, "async mirror", tx,
	    "the lagging child guid %llu is a normal child of its mirror "
	    "from txg %llu", (u_longlong_t)guid, (u_longlong_t)txg);
	if (am->am_release_zthr != NULL)
		zthr_wakeup(am->am_release_zthr);
}

void
vdev_async_mirror_sync_start(spa_t *spa, dmu_tx_t *tx)
{
	vdev_async_mirror_t *am = spa->spa_async_mirror;

	ASSERT(dsl_pool_sync_context(spa_get_dsl(spa)));
	ASSERT(spa_config_held(spa, SCL_CONFIG, RW_READER));
	if (spa->spa_async_mirror_import.ami_off != 0) {
		uint64_t guid = spa->spa_async_mirror_import.ami_off;
		vdev_t *vd = spa_lookup_by_guid(spa, guid, B_FALSE);

		VERIFY3P(vd, !=, NULL);
		async_mirror_prop_store(spa, vd, VDEV_ASYNC_MIRROR_OFF, tx);
		spa_history_log_internal(spa, "async mirror", tx,
		    "write_mode=sync for child %llu because %s",
		    (u_longlong_t)guid,
		    spa->spa_async_mirror_import.ami_off_reason);
		spa->spa_async_mirror_import.ami_off = 0;
		spa->spa_async_mirror_import.ami_off_reason = NULL;
	}
	if (spa->spa_async_mirror_import.ami_off_all) {
		async_mirror_prop_off_all(spa, tx, NULL, B_TRUE,
		    "more than one leaf was marked behind at import");
		spa->spa_async_mirror_import.ami_off_all = B_FALSE;
	}

	if (am == NULL)
		return;

	mutex_enter(&am->am_lock);
	uint64_t cleanup_guid = am->am_replace_cleanup_guid;
	mutex_exit(&am->am_lock);
	if (cleanup_guid != 0)
		vdev_async_mirror_replace_clear_sync(spa, cleanup_guid, tx);

	if (am->am_on_txg != 0 && dmu_tx_get_txg(tx) >= am->am_on_txg)
		async_mirror_switch_on(am, tx);
	if (am->am_off && am->am_lagging != NULL)
		async_mirror_switch_off(am, tx);

	vdev_async_mirror_sync_check(spa);

	mutex_enter(&am->am_lock);
	boolean_t fail = (am->am_state == AMS_FAILED && am->am_dtl_txg == 0) ||
	    (am->am_dtl_child_guid != 0 &&
	    (am->am_dtl_child_txg == 0 ||
	    !async_mirror_durable(am, am->am_dtl_child_txg)));
	mutex_exit(&am->am_lock);
	if (fail)
		async_mirror_fail_sync(am, tx);
}
