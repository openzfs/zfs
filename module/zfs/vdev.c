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
 * Copyright (c) 2011, 2021 by Delphix. All rights reserved.
 * Copyright 2017 Nexenta Systems, Inc.
 * Copyright (c) 2014 Integros [integros.com]
 * Copyright 2016 Toomas Soome <tsoome@me.com>
 * Copyright 2017 Joyent, Inc.
 * Copyright (c) 2017, Intel Corporation.
 * Copyright (c) 2019, Datto Inc. All rights reserved.
 * Copyright [2021] Hewlett Packard Enterprise Development LP
 */

#include <sys/zfs_context.h>
#include <sys/fm/fs/zfs.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/bpobj.h>
#include <sys/dmu.h>
#include <sys/dmu_tx.h>
#include <sys/dsl_dir.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_rebuild.h>
#include <sys/vdev_draid.h>
#include <sys/uberblock_impl.h>
#include <sys/metaslab.h>
#include <sys/metaslab_impl.h>
#include <sys/space_map.h>
#include <sys/space_reftree.h>
#include <sys/zio.h>
#include <sys/zap.h>
#include <sys/fs/zfs.h>
#include <sys/arc.h>
#include <sys/zil.h>
#include <sys/dsl_scan.h>
#include <sys/vdev_raidz.h>
#include <sys/abd.h>
#include <sys/vdev_initialize.h>
#include <sys/vdev_trim.h>
#include <sys/zvol.h>
#include <sys/zfs_ratelimit.h>

/*
 * One metaslab from each (normal-class) vdev is used by the ZIL.  These are
 * called "embedded slog metaslabs", are referenced by vdev_log_mg, and are
 * part of the spa_embedded_log_class.  The metaslab with the most free space
 * in each vdev is selected for this purpose when the pool is opened (or a
 * vdev is added).  See vdev_metaslab_init().
 *
 * Log blocks can be allocated from the following locations.  Each one is tried
 * in order until the allocation succeeds:
 * 1. dedicated log vdevs, aka "slog" (spa_log_class)
 * 2. embedded slog metaslabs (spa_embedded_log_class)
 * 3. other metaslabs in normal vdevs (spa_normal_class)
 *
 * zfs_embedded_slog_min_ms disables the embedded slog if there are fewer
 * than this number of metaslabs in the vdev.  This ensures that we don't set
 * aside an unreasonable amount of space for the ZIL.  If set to less than
 * 1 << (spa_slop_shift + 1), on small pools the usable space may be reduced
 * (by more than 1<<spa_slop_shift) due to the embedded slog metaslab.
 */
int zfs_embedded_slog_min_ms = 64;

/* default target for number of metaslabs per top-level vdev */
int zfs_vdev_default_ms_count = 200;

/* minimum number of metaslabs per top-level vdev */
int zfs_vdev_min_ms_count = 16;

/* practical upper limit of total metaslabs per top-level vdev */
int zfs_vdev_ms_count_limit = 1ULL << 17;

/* lower limit for metaslab size (512M) */
int zfs_vdev_default_ms_shift = 29;

/* upper limit for metaslab size (16G) */
int zfs_vdev_max_ms_shift = 34;

int vdev_validate_skip = B_FALSE;

/*
 * Since the DTL space map of a vdev is not expected to have a lot of
 * entries, we default its block size to 4K.
 */
int zfs_vdev_dtl_sm_blksz = (1 << 12);

/*
 * Rate limit slow IO (delay) events to this many per second.
 */
unsigned int zfs_slow_io_events_per_second = 20;

/*
 * Rate limit checksum events after this many checksum errors per second.
 */
unsigned int zfs_checksum_events_per_second = 20;

/*
 * Ignore errors during scrub/resilver.  Allows to work around resilver
 * upon import when there are pool errors.
 */
int zfs_scan_ignore_errors = 0;

/*
 * vdev-wide space maps that have lots of entries written to them at
 * the end of each transaction can benefit from a higher I/O bandwidth
 * (e.g. vdev_obsolete_sm), thus we default their block size to 128K.
 */
int zfs_vdev_standard_sm_blksz = (1 << 17);

/*
 * Tunable parameter for debugging or performance analysis. Setting this
 * will cause pool corruption on power loss if a volatile out-of-order
 * write cache is enabled.
 */
int zfs_nocacheflush = 0;

uint64_t zfs_vdev_max_auto_ashift = ASHIFT_MAX;
uint64_t zfs_vdev_min_auto_ashift = ASHIFT_MIN;

void
vdev_dbgmsg(vdev_t *vd, const char *fmt, ...)
{
	va_list adx;
	char buf[256];

	va_start(adx, fmt);
	(void) vsnprintf(buf, sizeof (buf), fmt, adx);
	va_end(adx);

	if (vd->vdev_path != NULL) {
		zfs_dbgmsg("%s vdev '%s': %s", vd->vdev_ops->vdev_op_type,
		    vd->vdev_path, buf);
	} else {
		zfs_dbgmsg("%s-%llu vdev (guid %llu): %s",
		    vd->vdev_ops->vdev_op_type,
		    (u_longlong_t)vd->vdev_id,
		    (u_longlong_t)vd->vdev_guid, buf);
	}
}

void
vdev_dbgmsg_print_tree(vdev_t *vd, int indent)
{
	char state[20];

	if (vd->vdev_ishole || vd->vdev_ops == &vdev_missing_ops) {
		zfs_dbgmsg("%*svdev %llu: %s", indent, "",
		    (u_longlong_t)vd->vdev_id,
		    vd->vdev_ops->vdev_op_type);
		return;
	}

	switch (vd->vdev_state) {
	case VDEV_STATE_UNKNOWN:
		(void) snprintf(state, sizeof (state), "unknown");
		break;
	case VDEV_STATE_CLOSED:
		(void) snprintf(state, sizeof (state), "closed");
		break;
	case VDEV_STATE_OFFLINE:
		(void) snprintf(state, sizeof (state), "offline");
		break;
	case VDEV_STATE_REMOVED:
		(void) snprintf(state, sizeof (state), "removed");
		break;
	case VDEV_STATE_CANT_OPEN:
		(void) snprintf(state, sizeof (state), "can't open");
		break;
	case VDEV_STATE_FAULTED:
		(void) snprintf(state, sizeof (state), "faulted");
		break;
	case VDEV_STATE_DEGRADED:
		(void) snprintf(state, sizeof (state), "degraded");
		break;
	case VDEV_STATE_HEALTHY:
		(void) snprintf(state, sizeof (state), "healthy");
		break;
	default:
		(void) snprintf(state, sizeof (state), "<state %u>",
		    (uint_t)vd->vdev_state);
	}

	zfs_dbgmsg("%*svdev %u: %s%s, guid: %llu, path: %s, %s", indent,
	    "", (int)vd->vdev_id, vd->vdev_ops->vdev_op_type,
	    vd->vdev_islog ? " (log)" : "",
	    (u_longlong_t)vd->vdev_guid,
	    vd->vdev_path ? vd->vdev_path : "N/A", state);

	for (uint64_t i = 0; i < vd->vdev_children; i++)
		vdev_dbgmsg_print_tree(vd->vdev_child[i], indent + 2);
}

/*
 * Virtual device management.
 */

static vdev_ops_t *vdev_ops_table[] = {
	&vdev_root_ops,
	&vdev_raidz_ops,
	&vdev_draid_ops,
	&vdev_draid_spare_ops,
	&vdev_mirror_ops,
	&vdev_replacing_ops,
	&vdev_spare_ops,
	&vdev_disk_ops,
	&vdev_file_ops,
	&vdev_missing_ops,
	&vdev_hole_ops,
	&vdev_indirect_ops,
	NULL
};

/*
 * Given a vdev type, return the appropriate ops vector.
 */
static vdev_ops_t *
vdev_getops(const char *type)
{
	vdev_ops_t *ops, **opspp;

	for (opspp = vdev_ops_table; (ops = *opspp) != NULL; opspp++)
		if (strcmp(ops->vdev_op_type, type) == 0)
			break;

	return (ops);
}

/*
 * Given a vdev and a metaslab class, find which metaslab group we're
 * interested in. All vdevs may belong to two different metaslab classes.
 * Dedicated slog devices use only the primary metaslab group, rather than a
 * separate log group. For embedded slogs, the vdev_log_mg will be non-NULL.
 */
metaslab_group_t *
vdev_get_mg(vdev_t *vd, metaslab_class_t *mc)
{
	if (mc == spa_embedded_log_class(vd->vdev_spa) &&
	    vd->vdev_log_mg != NULL)
		return (vd->vdev_log_mg);
	else
		return (vd->vdev_mg);
}

/* ARGSUSED */
void
vdev_default_xlate(vdev_t *vd, const range_seg64_t *logical_rs,
    range_seg64_t *physical_rs, range_seg64_t *remain_rs)
{
	physical_rs->rs_start = logical_rs->rs_start;
	physical_rs->rs_end = logical_rs->rs_end;
}

/*
 * Derive the enumerated allocation bias from string input.
 * String origin is either the per-vdev zap or zpool(8).
 */
static vdev_alloc_bias_t
vdev_derive_alloc_bias(const char *bias)
{
	vdev_alloc_bias_t alloc_bias = VDEV_BIAS_NONE;

	if (strcmp(bias, VDEV_ALLOC_BIAS_LOG) == 0)
		alloc_bias = VDEV_BIAS_LOG;
	else if (strcmp(bias, VDEV_ALLOC_BIAS_SPECIAL) == 0)
		alloc_bias = VDEV_BIAS_SPECIAL;
	else if (strcmp(bias, VDEV_ALLOC_BIAS_DEDUP) == 0)
		alloc_bias = VDEV_BIAS_DEDUP;

	return (alloc_bias);
}

/*
 * Default asize function: return the MAX of psize with the asize of
 * all children.  This is what's used by anything other than RAID-Z.
 */
uint64_t
vdev_default_asize(vdev_t *vd, uint64_t psize)
{
	uint64_t asize = P2ROUNDUP(psize, 1ULL << vd->vdev_top->vdev_ashift);
	uint64_t csize;

	for (int c = 0; c < vd->vdev_children; c++) {
		csize = vdev_psize_to_asize(vd->vdev_child[c], psize);
		asize = MAX(asize, csize);
	}

	return (asize);
}

uint64_t
vdev_default_min_asize(vdev_t *vd)
{
	return (vd->vdev_min_asize);
}

/*
 * Get the minimum allocatable size. We define the allocatable size as
 * the vdev's asize rounded to the nearest metaslab. This allows us to
 * replace or attach devices which don't have the same physical size but
 * can still satisfy the same number of allocations.
 */
uint64_t
vdev_get_min_asize(vdev_t *vd)
{
	vdev_t *pvd = vd->vdev_parent;

	/*
	 * If our parent is NULL (inactive spare or cache) or is the root,
	 * just return our own asize.
	 */
	if (pvd == NULL)
		return (vd->vdev_asize);

	/*
	 * The top-level vdev just returns the allocatable size rounded
	 * to the nearest metaslab.
	 */
	if (vd == vd->vdev_top)
		return (P2ALIGN(vd->vdev_asize, 1ULL << vd->vdev_ms_shift));

	return (pvd->vdev_ops->vdev_op_min_asize(pvd));
}

void
vdev_set_min_asize(vdev_t *vd)
{
	vd->vdev_min_asize = vdev_get_min_asize(vd);

	for (int c = 0; c < vd->vdev_children; c++)
		vdev_set_min_asize(vd->vdev_child[c]);
}

/*
 * Get the minimal allocation size for the top-level vdev.
 */
uint64_t
vdev_get_min_alloc(vdev_t *vd)
{
	uint64_t min_alloc = 1ULL << vd->vdev_ashift;

	if (vd->vdev_ops->vdev_op_min_alloc != NULL)
		min_alloc = vd->vdev_ops->vdev_op_min_alloc(vd);

	return (min_alloc);
}

/*
 * Get the parity level for a top-level vdev.
 */
uint64_t
vdev_get_nparity(vdev_t *vd)
{
	uint64_t nparity = 0;

	if (vd->vdev_ops->vdev_op_nparity != NULL)
		nparity = vd->vdev_ops->vdev_op_nparity(vd);

	return (nparity);
}

/*
 * Get the number of data disks for a top-level vdev.
 */
uint64_t
vdev_get_ndisks(vdev_t *vd)
{
	uint64_t ndisks = 1;

	if (vd->vdev_ops->vdev_op_ndisks != NULL)
		ndisks = vd->vdev_ops->vdev_op_ndisks(vd);

	return (ndisks);
}

vdev_t *
vdev_lookup_top(spa_t *spa, uint64_t vdev)
{
	vdev_t *rvd = spa->spa_root_vdev;

	ASSERT(spa_config_held(spa, SCL_ALL, RW_READER) != 0);

	if (vdev < rvd->vdev_children) {
		ASSERT(rvd->vdev_child[vdev] != NULL);
		return (rvd->vdev_child[vdev]);
	}

	return (NULL);
}

vdev_t *
vdev_lookup_by_guid(vdev_t *vd, uint64_t guid)
{
	vdev_t *mvd;

	if (vd->vdev_guid == guid)
		return (vd);

	for (int c = 0; c < vd->vdev_children; c++)
		if ((mvd = vdev_lookup_by_guid(vd->vdev_child[c], guid)) !=
		    NULL)
			return (mvd);

	return (NULL);
}

static int
vdev_count_leaves_impl(vdev_t *vd)
{
	int n = 0;

	if (vd->vdev_ops->vdev_op_leaf)
		return (1);

	for (int c = 0; c < vd->vdev_children; c++)
		n += vdev_count_leaves_impl(vd->vdev_child[c]);

	return (n);
}

int
vdev_count_leaves(spa_t *spa)
{
	int rc;

	spa_config_enter(spa, SCL_VDEV, FTAG, RW_READER);
	rc = vdev_count_leaves_impl(spa->spa_root_vdev);
	spa_config_exit(spa, SCL_VDEV, FTAG);

	return (rc);
}

void
vdev_add_child(vdev_t *pvd, vdev_t *cvd)
{
	size_t oldsize, newsize;
	uint64_t id = cvd->vdev_id;
	vdev_t **newchild;

	ASSERT(spa_config_held(cvd->vdev_spa, SCL_ALL, RW_WRITER) == SCL_ALL);
	ASSERT(cvd->vdev_parent == NULL);

	cvd->vdev_parent = pvd;

	if (pvd == NULL)
		return;

	ASSERT(id >= pvd->vdev_children || pvd->vdev_child[id] == NULL);

	oldsize = pvd->vdev_children * sizeof (vdev_t *);
	pvd->vdev_children = MAX(pvd->vdev_children, id + 1);
	newsize = pvd->vdev_children * sizeof (vdev_t *);

	newchild = kmem_alloc(newsize, KM_SLEEP);
	if (pvd->vdev_child != NULL) {
		bcopy(pvd->vdev_child, newchild, oldsize);
		kmem_free(pvd->vdev_child, oldsize);
	}

	pvd->vdev_child = newchild;
	pvd->vdev_child[id] = cvd;

	cvd->vdev_top = (pvd->vdev_top ? pvd->vdev_top: cvd);
	ASSERT(cvd->vdev_top->vdev_parent->vdev_parent == NULL);

	/*
	 * Walk up all ancestors to update guid sum.
	 */
	for (; pvd != NULL; pvd = pvd->vdev_parent)
		pvd->vdev_guid_sum += cvd->vdev_guid_sum;

	if (cvd->vdev_ops->vdev_op_leaf) {
		list_insert_head(&cvd->vdev_spa->spa_leaf_list, cvd);
		cvd->vdev_spa->spa_leaf_list_gen++;
	}
}

void
vdev_remove_child(vdev_t *pvd, vdev_t *cvd)
{
	int c;
	uint_t id = cvd->vdev_id;

	ASSERT(cvd->vdev_parent == pvd);

	if (pvd == NULL)
		return;

	ASSERT(id < pvd->vdev_children);
	ASSERT(pvd->vdev_child[id] == cvd);

	pvd->vdev_child[id] = NULL;
	cvd->vdev_parent = NULL;

	for (c = 0; c < pvd->vdev_children; c++)
		if (pvd->vdev_child[c])
			break;

	if (c == pvd->vdev_children) {
		kmem_free(pvd->vdev_child, c * sizeof (vdev_t *));
		pvd->vdev_child = NULL;
		pvd->vdev_children = 0;
	}

	if (cvd->vdev_ops->vdev_op_leaf) {
		spa_t *spa = cvd->vdev_spa;
		list_remove(&spa->spa_leaf_list, cvd);
		spa->spa_leaf_list_gen++;
	}

	/*
	 * Walk up all ancestors to update guid sum.
	 */
	for (; pvd != NULL; pvd = pvd->vdev_parent)
		pvd->vdev_guid_sum -= cvd->vdev_guid_sum;
}

/*
 * Remove any holes in the child array.
 */
void
vdev_compact_children(vdev_t *pvd)
{
	vdev_t **newchild, *cvd;
	int oldc = pvd->vdev_children;
	int newc;

	ASSERT(spa_config_held(pvd->vdev_spa, SCL_ALL, RW_WRITER) == SCL_ALL);

	if (oldc == 0)
		return;

	for (int c = newc = 0; c < oldc; c++)
		if (pvd->vdev_child[c])
			newc++;

	if (newc > 0) {
		newchild = kmem_zalloc(newc * sizeof (vdev_t *), KM_SLEEP);

		for (int c = newc = 0; c < oldc; c++) {
			if ((cvd = pvd->vdev_child[c]) != NULL) {
				newchild[newc] = cvd;
				cvd->vdev_id = newc++;
			}
		}
	} else {
		newchild = NULL;
	}

	kmem_free(pvd->vdev_child, oldc * sizeof (vdev_t *));
	pvd->vdev_child = newchild;
	pvd->vdev_children = newc;
}

/*
 * Allocate and minimally initialize a vdev_t.
 */
vdev_t *
vdev_alloc_common(spa_t *spa, uint_t id, uint64_t guid, vdev_ops_t *ops)
{
	vdev_t *vd;
	vdev_indirect_config_t *vic;

	vd = kmem_zalloc(sizeof (vdev_t), KM_SLEEP);
	vic = &vd->vdev_indirect_config;

	if (spa->spa_root_vdev == NULL) {
		ASSERT(ops == &vdev_root_ops);
		spa->spa_root_vdev = vd;
		spa->spa_load_guid = spa_generate_guid(NULL);
	}

	if (guid == 0 && ops != &vdev_hole_ops) {
		if (spa->spa_root_vdev == vd) {
			/*
			 * The root vdev's guid will also be the pool guid,
			 * which must be unique among all pools.
			 */
			guid = spa_generate_guid(NULL);
		} else {
			/*
			 * Any other vdev's guid must be unique within the pool.
			 */
			guid = spa_generate_guid(spa);
		}
		ASSERT(!spa_guid_exists(spa_guid(spa), guid));
	}

	vd->vdev_spa = spa;
	vd->vdev_id = id;
	vd->vdev_guid = guid;
	vd->vdev_guid_sum = guid;
	vd->vdev_ops = ops;
	vd->vdev_state = VDEV_STATE_CLOSED;
	vd->vdev_ishole = (ops == &vdev_hole_ops);
	vic->vic_prev_indirect_vdev = UINT64_MAX;

	rw_init(&vd->vdev_indirect_rwlock, NULL, RW_DEFAULT, NULL);
	mutex_init(&vd->vdev_obsolete_lock, NULL, MUTEX_DEFAULT, NULL);
	vd->vdev_obsolete_segments = range_tree_create(NULL, RANGE_SEG64, NULL,
	    0, 0);

	/*
	 * Initialize rate limit structs for events.  We rate limit ZIO delay
	 * and checksum events so that we don't overwhelm ZED with thousands
	 * of events when a disk is acting up.
	 */
	zfs_ratelimit_init(&vd->vdev_delay_rl, &zfs_slow_io_events_per_second,
	    1);
	zfs_ratelimit_init(&vd->vdev_deadman_rl, &zfs_slow_io_events_per_second,
	    1);
	zfs_ratelimit_init(&vd->vdev_checksum_rl,
	    &zfs_checksum_events_per_second, 1);

	list_link_init(&vd->vdev_config_dirty_node);
	list_link_init(&vd->vdev_state_dirty_node);
	list_link_init(&vd->vdev_initialize_node);
	list_link_init(&vd->vdev_leaf_node);
	list_link_init(&vd->vdev_trim_node);

	mutex_init(&vd->vdev_dtl_lock, NULL, MUTEX_NOLOCKDEP, NULL);
	mutex_init(&vd->vdev_stat_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&vd->vdev_probe_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&vd->vdev_scan_io_queue_lock, NULL, MUTEX_DEFAULT, NULL);

	mutex_init(&vd->vdev_initialize_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&vd->vdev_initialize_io_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&vd->vdev_initialize_cv, NULL, CV_DEFAULT, NULL);
	cv_init(&vd->vdev_initialize_io_cv, NULL, CV_DEFAULT, NULL);

	mutex_init(&vd->vdev_trim_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&vd->vdev_autotrim_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&vd->vdev_trim_io_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&vd->vdev_trim_cv, NULL, CV_DEFAULT, NULL);
	cv_init(&vd->vdev_autotrim_cv, NULL, CV_DEFAULT, NULL);
	cv_init(&vd->vdev_trim_io_cv, NULL, CV_DEFAULT, NULL);

	mutex_init(&vd->vdev_rebuild_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&vd->vdev_rebuild_cv, NULL, CV_DEFAULT, NULL);

	for (int t = 0; t < DTL_TYPES; t++) {
		vd->vdev_dtl[t] = range_tree_create(NULL, RANGE_SEG64, NULL, 0,
		    0);
	}

	txg_list_create(&vd->vdev_ms_list, spa,
	    offsetof(struct metaslab, ms_txg_node));
	txg_list_create(&vd->vdev_dtl_list, spa,
	    offsetof(struct vdev, vdev_dtl_node));
	vd->vdev_stat.vs_timestamp = gethrtime();
	vdev_queue_init(vd);
	vdev_cache_init(vd);

	return (vd);
}

/*
 * Allocate a new vdev.  The 'alloctype' is used to control whether we are
 * creating a new vdev or loading an existing one - the behavior is slightly
 * different for each case.
 */
int
vdev_alloc(spa_t *spa, vdev_t **vdp, nvlist_t *nv, vdev_t *parent, uint_t id,
    int alloctype)
{
	vdev_ops_t *ops;
	char *type;
	uint64_t guid = 0, islog;
	vdev_t *vd;
	vdev_indirect_config_t *vic;
	char *tmp = NULL;
	int rc;
	vdev_alloc_bias_t alloc_bias = VDEV_BIAS_NONE;
	boolean_t top_level = (parent && !parent->vdev_parent);

	ASSERT(spa_config_held(spa, SCL_ALL, RW_WRITER) == SCL_ALL);

	if (nvlist_lookup_string(nv, ZPOOL_CONFIG_TYPE, &type) != 0)
		return (SET_ERROR(EINVAL));

	if ((ops = vdev_getops(type)) == NULL)
		return (SET_ERROR(EINVAL));

	/*
	 * If this is a load, get the vdev guid from the nvlist.
	 * Otherwise, vdev_alloc_common() will generate one for us.
	 */
	if (alloctype == VDEV_ALLOC_LOAD) {
		uint64_t label_id;

		if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_ID, &label_id) ||
		    label_id != id)
			return (SET_ERROR(EINVAL));

		if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_GUID, &guid) != 0)
			return (SET_ERROR(EINVAL));
	} else if (alloctype == VDEV_ALLOC_SPARE) {
		if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_GUID, &guid) != 0)
			return (SET_ERROR(EINVAL));
	} else if (alloctype == VDEV_ALLOC_L2CACHE) {
		if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_GUID, &guid) != 0)
			return (SET_ERROR(EINVAL));
	} else if (alloctype == VDEV_ALLOC_ROOTPOOL) {
		if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_GUID, &guid) != 0)
			return (SET_ERROR(EINVAL));
	}

	/*
	 * The first allocated vdev must be of type 'root'.
	 */
	if (ops != &vdev_root_ops && spa->spa_root_vdev == NULL)
		return (SET_ERROR(EINVAL));

	/*
	 * Determine whether we're a log vdev.
	 */
	islog = 0;
	(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_IS_LOG, &islog);
	if (islog && spa_version(spa) < SPA_VERSION_SLOGS)
		return (SET_ERROR(ENOTSUP));

	if (ops == &vdev_hole_ops && spa_version(spa) < SPA_VERSION_HOLES)
		return (SET_ERROR(ENOTSUP));

	if (top_level && alloctype == VDEV_ALLOC_ADD) {
		char *bias;

		/*
		 * If creating a top-level vdev, check for allocation
		 * classes input.
		 */
		if (nvlist_lookup_string(nv, ZPOOL_CONFIG_ALLOCATION_BIAS,
		    &bias) == 0) {
			alloc_bias = vdev_derive_alloc_bias(bias);

			/* spa_vdev_add() expects feature to be enabled */
			if (spa->spa_load_state != SPA_LOAD_CREATE &&
			    !spa_feature_is_enabled(spa,
			    SPA_FEATURE_ALLOCATION_CLASSES)) {
				return (SET_ERROR(ENOTSUP));
			}
		}

		/* spa_vdev_add() expects feature to be enabled */
		if (ops == &vdev_draid_ops &&
		    spa->spa_load_state != SPA_LOAD_CREATE &&
		    !spa_feature_is_enabled(spa, SPA_FEATURE_DRAID)) {
			return (SET_ERROR(ENOTSUP));
		}
	}

	/*
	 * Initialize the vdev specific data.  This is done before calling
	 * vdev_alloc_common() since it may fail and this simplifies the
	 * error reporting and cleanup code paths.
	 */
	void *tsd = NULL;
	if (ops->vdev_op_init != NULL) {
		rc = ops->vdev_op_init(spa, nv, &tsd);
		if (rc != 0) {
			return (rc);
		}
	}

	vd = vdev_alloc_common(spa, id, guid, ops);
	vd->vdev_tsd = tsd;
	vd->vdev_islog = islog;

	if (top_level && alloc_bias != VDEV_BIAS_NONE)
		vd->vdev_alloc_bias = alloc_bias;

	if (nvlist_lookup_string(nv, ZPOOL_CONFIG_PATH, &vd->vdev_path) == 0)
		vd->vdev_path = spa_strdup(vd->vdev_path);

	/*
	 * ZPOOL_CONFIG_AUX_STATE = "external" means we previously forced a
	 * fault on a vdev and want it to persist across imports (like with
	 * zpool offline -f).
	 */
	rc = nvlist_lookup_string(nv, ZPOOL_CONFIG_AUX_STATE, &tmp);
	if (rc == 0 && tmp != NULL && strcmp(tmp, "external") == 0) {
		vd->vdev_stat.vs_aux = VDEV_AUX_EXTERNAL;
		vd->vdev_faulted = 1;
		vd->vdev_label_aux = VDEV_AUX_EXTERNAL;
	}

	if (nvlist_lookup_string(nv, ZPOOL_CONFIG_DEVID, &vd->vdev_devid) == 0)
		vd->vdev_devid = spa_strdup(vd->vdev_devid);
	if (nvlist_lookup_string(nv, ZPOOL_CONFIG_PHYS_PATH,
	    &vd->vdev_physpath) == 0)
		vd->vdev_physpath = spa_strdup(vd->vdev_physpath);

	if (nvlist_lookup_string(nv, ZPOOL_CONFIG_VDEV_ENC_SYSFS_PATH,
	    &vd->vdev_enc_sysfs_path) == 0)
		vd->vdev_enc_sysfs_path = spa_strdup(vd->vdev_enc_sysfs_path);

	if (nvlist_lookup_string(nv, ZPOOL_CONFIG_FRU, &vd->vdev_fru) == 0)
		vd->vdev_fru = spa_strdup(vd->vdev_fru);

	/*
	 * Set the whole_disk property.  If it's not specified, leave the value
	 * as -1.
	 */
	if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_WHOLE_DISK,
	    &vd->vdev_wholedisk) != 0)
		vd->vdev_wholedisk = -1ULL;

	vic = &vd->vdev_indirect_config;

	ASSERT0(vic->vic_mapping_object);
	(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_INDIRECT_OBJECT,
	    &vic->vic_mapping_object);
	ASSERT0(vic->vic_births_object);
	(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_INDIRECT_BIRTHS,
	    &vic->vic_births_object);
	ASSERT3U(vic->vic_prev_indirect_vdev, ==, UINT64_MAX);
	(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_PREV_INDIRECT_VDEV,
	    &vic->vic_prev_indirect_vdev);

	/*
	 * Look for the 'not present' flag.  This will only be set if the device
	 * was not present at the time of import.
	 */
	(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_NOT_PRESENT,
	    &vd->vdev_not_present);

	/*
	 * Get the alignment requirement.
	 */
	(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_ASHIFT, &vd->vdev_ashift);

	/*
	 * Retrieve the vdev creation time.
	 */
	(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_CREATE_TXG,
	    &vd->vdev_crtxg);

	/*
	 * If we're a top-level vdev, try to load the allocation parameters.
	 */
	if (top_level &&
	    (alloctype == VDEV_ALLOC_LOAD || alloctype == VDEV_ALLOC_SPLIT)) {
		(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_METASLAB_ARRAY,
		    &vd->vdev_ms_array);
		(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_METASLAB_SHIFT,
		    &vd->vdev_ms_shift);
		(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_ASIZE,
		    &vd->vdev_asize);
		(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_REMOVING,
		    &vd->vdev_removing);
		(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_VDEV_TOP_ZAP,
		    &vd->vdev_top_zap);
	} else {
		ASSERT0(vd->vdev_top_zap);
	}

	if (top_level && alloctype != VDEV_ALLOC_ATTACH) {
		ASSERT(alloctype == VDEV_ALLOC_LOAD ||
		    alloctype == VDEV_ALLOC_ADD ||
		    alloctype == VDEV_ALLOC_SPLIT ||
		    alloctype == VDEV_ALLOC_ROOTPOOL);
		/* Note: metaslab_group_create() is now deferred */
	}

	if (vd->vdev_ops->vdev_op_leaf &&
	    (alloctype == VDEV_ALLOC_LOAD || alloctype == VDEV_ALLOC_SPLIT)) {
		(void) nvlist_lookup_uint64(nv,
		    ZPOOL_CONFIG_VDEV_LEAF_ZAP, &vd->vdev_leaf_zap);
	} else {
		ASSERT0(vd->vdev_leaf_zap);
	}

	/*
	 * If we're a leaf vdev, try to load the DTL object and other state.
	 */

	if (vd->vdev_ops->vdev_op_leaf &&
	    (alloctype == VDEV_ALLOC_LOAD || alloctype == VDEV_ALLOC_L2CACHE ||
	    alloctype == VDEV_ALLOC_ROOTPOOL)) {
		if (alloctype == VDEV_ALLOC_LOAD) {
			(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_DTL,
			    &vd->vdev_dtl_object);
			(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_UNSPARE,
			    &vd->vdev_unspare);
		}

		if (alloctype == VDEV_ALLOC_ROOTPOOL) {
			uint64_t spare = 0;

			if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_IS_SPARE,
			    &spare) == 0 && spare)
				spa_spare_add(vd);
		}

		(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_OFFLINE,
		    &vd->vdev_offline);

		(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_RESILVER_TXG,
		    &vd->vdev_resilver_txg);

		(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_REBUILD_TXG,
		    &vd->vdev_rebuild_txg);

		if (nvlist_exists(nv, ZPOOL_CONFIG_RESILVER_DEFER))
			vdev_defer_resilver(vd);

		/*
		 * In general, when importing a pool we want to ignore the
		 * persistent fault state, as the diagnosis made on another
		 * system may not be valid in the current context.  The only
		 * exception is if we forced a vdev to a persistently faulted
		 * state with 'zpool offline -f'.  The persistent fault will
		 * remain across imports until cleared.
		 *
		 * Local vdevs will remain in the faulted state.
		 */
		if (spa_load_state(spa) == SPA_LOAD_OPEN ||
		    spa_load_state(spa) == SPA_LOAD_IMPORT) {
			(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_FAULTED,
			    &vd->vdev_faulted);
			(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_DEGRADED,
			    &vd->vdev_degraded);
			(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_REMOVED,
			    &vd->vdev_removed);

			if (vd->vdev_faulted || vd->vdev_degraded) {
				char *aux;

				vd->vdev_label_aux =
				    VDEV_AUX_ERR_EXCEEDED;
				if (nvlist_lookup_string(nv,
				    ZPOOL_CONFIG_AUX_STATE, &aux) == 0 &&
				    strcmp(aux, "external") == 0)
					vd->vdev_label_aux = VDEV_AUX_EXTERNAL;
				else
					vd->vdev_faulted = 0ULL;
			}
		}
	}

	/*
	 * Add ourselves to the parent's list of children.
	 */
	vdev_add_child(parent, vd);

	*vdp = vd;

	return (0);
}

void
vdev_free(vdev_t *vd)
{
	spa_t *spa = vd->vdev_spa;

	ASSERT3P(vd->vdev_initialize_thread, ==, NULL);
	ASSERT3P(vd->vdev_trim_thread, ==, NULL);
	ASSERT3P(vd->vdev_autotrim_thread, ==, NULL);
	ASSERT3P(vd->vdev_rebuild_thread, ==, NULL);

	/*
	 * Scan queues are normally destroyed at the end of a scan. If the
	 * queue exists here, that implies the vdev is being removed while
	 * the scan is still running.
	 */
	if (vd->vdev_scan_io_queue != NULL) {
		mutex_enter(&vd->vdev_scan_io_queue_lock);
		dsl_scan_io_queue_destroy(vd->vdev_scan_io_queue);
		vd->vdev_scan_io_queue = NULL;
		mutex_exit(&vd->vdev_scan_io_queue_lock);
	}

	/*
	 * vdev_free() implies closing the vdev first.  This is simpler than
	 * trying to ensure complicated semantics for all callers.
	 */
	vdev_close(vd);

	ASSERT(!list_link_active(&vd->vdev_config_dirty_node));
	ASSERT(!list_link_active(&vd->vdev_state_dirty_node));

	/*
	 * Free all children.
	 */
	for (int c = 0; c < vd->vdev_children; c++)
		vdev_free(vd->vdev_child[c]);

	ASSERT(vd->vdev_child == NULL);
	ASSERT(vd->vdev_guid_sum == vd->vdev_guid);

	if (vd->vdev_ops->vdev_op_fini != NULL)
		vd->vdev_ops->vdev_op_fini(vd);

	/*
	 * Discard allocation state.
	 */
	if (vd->vdev_mg != NULL) {
		vdev_metaslab_fini(vd);
		metaslab_group_destroy(vd->vdev_mg);
		vd->vdev_mg = NULL;
	}
	if (vd->vdev_log_mg != NULL) {
		ASSERT0(vd->vdev_ms_count);
		metaslab_group_destroy(vd->vdev_log_mg);
		vd->vdev_log_mg = NULL;
	}

	ASSERT0(vd->vdev_stat.vs_space);
	ASSERT0(vd->vdev_stat.vs_dspace);
	ASSERT0(vd->vdev_stat.vs_alloc);

	/*
	 * Remove this vdev from its parent's child list.
	 */
	vdev_remove_child(vd->vdev_parent, vd);

	ASSERT(vd->vdev_parent == NULL);
	ASSERT(!list_link_active(&vd->vdev_leaf_node));

	/*
	 * Clean up vdev structure.
	 */
	vdev_queue_fini(vd);
	vdev_cache_fini(vd);

	if (vd->vdev_path)
		spa_strfree(vd->vdev_path);
	if (vd->vdev_devid)
		spa_strfree(vd->vdev_devid);
	if (vd->vdev_physpath)
		spa_strfree(vd->vdev_physpath);

	if (vd->vdev_enc_sysfs_path)
		spa_strfree(vd->vdev_enc_sysfs_path);

	if (vd->vdev_fru)
		spa_strfree(vd->vdev_fru);

	if (vd->vdev_isspare)
		spa_spare_remove(vd);
	if (vd->vdev_isl2cache)
		spa_l2cache_remove(vd);

	txg_list_destroy(&vd->vdev_ms_list);
	txg_list_destroy(&vd->vdev_dtl_list);

	mutex_enter(&vd->vdev_dtl_lock);
	space_map_close(vd->vdev_dtl_sm);
	for (int t = 0; t < DTL_TYPES; t++) {
		range_tree_vacate(vd->vdev_dtl[t], NULL, NULL);
		range_tree_destroy(vd->vdev_dtl[t]);
	}
	mutex_exit(&vd->vdev_dtl_lock);

	EQUIV(vd->vdev_indirect_births != NULL,
	    vd->vdev_indirect_mapping != NULL);
	if (vd->vdev_indirect_births != NULL) {
		vdev_indirect_mapping_close(vd->vdev_indirect_mapping);
		vdev_indirect_births_close(vd->vdev_indirect_births);
	}

	if (vd->vdev_obsolete_sm != NULL) {
		ASSERT(vd->vdev_removing ||
		    vd->vdev_ops == &vdev_indirect_ops);
		space_map_close(vd->vdev_obsolete_sm);
		vd->vdev_obsolete_sm = NULL;
	}
	range_tree_destroy(vd->vdev_obsolete_segments);
	rw_destroy(&vd->vdev_indirect_rwlock);
	mutex_destroy(&vd->vdev_obsolete_lock);

	mutex_destroy(&vd->vdev_dtl_lock);
	mutex_destroy(&vd->vdev_stat_lock);
	mutex_destroy(&vd->vdev_probe_lock);
	mutex_destroy(&vd->vdev_scan_io_queue_lock);

	mutex_destroy(&vd->vdev_initialize_lock);
	mutex_destroy(&vd->vdev_initialize_io_lock);
	cv_destroy(&vd->vdev_initialize_io_cv);
	cv_destroy(&vd->vdev_initialize_cv);

	mutex_destroy(&vd->vdev_trim_lock);
	mutex_destroy(&vd->vdev_autotrim_lock);
	mutex_destroy(&vd->vdev_trim_io_lock);
	cv_destroy(&vd->vdev_trim_cv);
	cv_destroy(&vd->vdev_autotrim_cv);
	cv_destroy(&vd->vdev_trim_io_cv);

	mutex_destroy(&vd->vdev_rebuild_lock);
	cv_destroy(&vd->vdev_rebuild_cv);

	zfs_ratelimit_fini(&vd->vdev_delay_rl);
	zfs_ratelimit_fini(&vd->vdev_deadman_rl);
	zfs_ratelimit_fini(&vd->vdev_checksum_rl);

	if (vd == spa->spa_root_vdev)
		spa->spa_root_vdev = NULL;

	kmem_free(vd, sizeof (vdev_t));
}

/*
 * Transfer top-level vdev state from svd to tvd.
 */
static void
vdev_top_transfer(vdev_t *svd, vdev_t *tvd)
{
	spa_t *spa = svd->vdev_spa;
	metaslab_t *msp;
	vdev_t *vd;
	int t;

	ASSERT(tvd == tvd->vdev_top);

	tvd->vdev_pending_fastwrite = svd->vdev_pending_fastwrite;
	tvd->vdev_ms_array = svd->vdev_ms_array;
	tvd->vdev_ms_shift = svd->vdev_ms_shift;
	tvd->vdev_ms_count = svd->vdev_ms_count;
	tvd->vdev_top_zap = svd->vdev_top_zap;

	svd->vdev_ms_array = 0;
	svd->vdev_ms_shift = 0;
	svd->vdev_ms_count = 0;
	svd->vdev_top_zap = 0;

	if (tvd->vdev_mg)
		ASSERT3P(tvd->vdev_mg, ==, svd->vdev_mg);
	if (tvd->vdev_log_mg)
		ASSERT3P(tvd->vdev_log_mg, ==, svd->vdev_log_mg);
	tvd->vdev_mg = svd->vdev_mg;
	tvd->vdev_log_mg = svd->vdev_log_mg;
	tvd->vdev_ms = svd->vdev_ms;

	svd->vdev_mg = NULL;
	svd->vdev_log_mg = NULL;
	svd->vdev_ms = NULL;

	if (tvd->vdev_mg != NULL)
		tvd->vdev_mg->mg_vd = tvd;
	if (tvd->vdev_log_mg != NULL)
		tvd->vdev_log_mg->mg_vd = tvd;

	tvd->vdev_checkpoint_sm = svd->vdev_checkpoint_sm;
	svd->vdev_checkpoint_sm = NULL;

	tvd->vdev_alloc_bias = svd->vdev_alloc_bias;
	svd->vdev_alloc_bias = VDEV_BIAS_NONE;

	tvd->vdev_stat.vs_alloc = svd->vdev_stat.vs_alloc;
	tvd->vdev_stat.vs_space = svd->vdev_stat.vs_space;
	tvd->vdev_stat.vs_dspace = svd->vdev_stat.vs_dspace;

	svd->vdev_stat.vs_alloc = 0;
	svd->vdev_stat.vs_space = 0;
	svd->vdev_stat.vs_dspace = 0;

	/*
	 * State which may be set on a top-level vdev that's in the
	 * process of being removed.
	 */
	ASSERT0(tvd->vdev_indirect_config.vic_births_object);
	ASSERT0(tvd->vdev_indirect_config.vic_mapping_object);
	ASSERT3U(tvd->vdev_indirect_config.vic_prev_indirect_vdev, ==, -1ULL);
	ASSERT3P(tvd->vdev_indirect_mapping, ==, NULL);
	ASSERT3P(tvd->vdev_indirect_births, ==, NULL);
	ASSERT3P(tvd->vdev_obsolete_sm, ==, NULL);
	ASSERT0(tvd->vdev_removing);
	ASSERT0(tvd->vdev_rebuilding);
	tvd->vdev_removing = svd->vdev_removing;
	tvd->vdev_rebuilding = svd->vdev_rebuilding;
	tvd->vdev_rebuild_config = svd->vdev_rebuild_config;
	tvd->vdev_indirect_config = svd->vdev_indirect_config;
	tvd->vdev_indirect_mapping = svd->vdev_indirect_mapping;
	tvd->vdev_indirect_births = svd->vdev_indirect_births;
	range_tree_swap(&svd->vdev_obsolete_segments,
	    &tvd->vdev_obsolete_segments);
	tvd->vdev_obsolete_sm = svd->vdev_obsolete_sm;
	svd->vdev_indirect_config.vic_mapping_object = 0;
	svd->vdev_indirect_config.vic_births_object = 0;
	svd->vdev_indirect_config.vic_prev_indirect_vdev = -1ULL;
	svd->vdev_indirect_mapping = NULL;
	svd->vdev_indirect_births = NULL;
	svd->vdev_obsolete_sm = NULL;
	svd->vdev_removing = 0;
	svd->vdev_rebuilding = 0;

	for (t = 0; t < TXG_SIZE; t++) {
		while ((msp = txg_list_remove(&svd->vdev_ms_list, t)) != NULL)
			(void) txg_list_add(&tvd->vdev_ms_list, msp, t);
		while ((vd = txg_list_remove(&svd->vdev_dtl_list, t)) != NULL)
			(void) txg_list_add(&tvd->vdev_dtl_list, vd, t);
		if (txg_list_remove_this(&spa->spa_vdev_txg_list, svd, t))
			(void) txg_list_add(&spa->spa_vdev_txg_list, tvd, t);
	}

	if (list_link_active(&svd->vdev_config_dirty_node)) {
		vdev_config_clean(svd);
		vdev_config_dirty(tvd);
	}

	if (list_link_active(&svd->vdev_state_dirty_node)) {
		vdev_state_clean(svd);
		vdev_state_dirty(tvd);
	}

	tvd->vdev_deflate_ratio = svd->vdev_deflate_ratio;
	svd->vdev_deflate_ratio = 0;

	tvd->vdev_islog = svd->vdev_islog;
	svd->vdev_islog = 0;

	dsl_scan_io_queue_vdev_xfer(svd, tvd);
}

static void
vdev_top_update(vdev_t *tvd, vdev_t *vd)
{
	if (vd == NULL)
		return;

	vd->vdev_top = tvd;

	for (int c = 0; c < vd->vdev_children; c++)
		vdev_top_update(tvd, vd->vdev_child[c]);
}

/*
 * Add a mirror/replacing vdev above an existing vdev.  There is no need to
 * call .vdev_op_init() since mirror/replacing vdevs do not have private state.
 */
vdev_t *
vdev_add_parent(vdev_t *cvd, vdev_ops_t *ops)
{
	spa_t *spa = cvd->vdev_spa;
	vdev_t *pvd = cvd->vdev_parent;
	vdev_t *mvd;

	ASSERT(spa_config_held(spa, SCL_ALL, RW_WRITER) == SCL_ALL);

	mvd = vdev_alloc_common(spa, cvd->vdev_id, 0, ops);

	mvd->vdev_asize = cvd->vdev_asize;
	mvd->vdev_min_asize = cvd->vdev_min_asize;
	mvd->vdev_max_asize = cvd->vdev_max_asize;
	mvd->vdev_psize = cvd->vdev_psize;
	mvd->vdev_ashift = cvd->vdev_ashift;
	mvd->vdev_logical_ashift = cvd->vdev_logical_ashift;
	mvd->vdev_physical_ashift = cvd->vdev_physical_ashift;
	mvd->vdev_state = cvd->vdev_state;
	mvd->vdev_crtxg = cvd->vdev_crtxg;

	vdev_remove_child(pvd, cvd);
	vdev_add_child(pvd, mvd);
	cvd->vdev_id = mvd->vdev_children;
	vdev_add_child(mvd, cvd);
	vdev_top_update(cvd->vdev_top, cvd->vdev_top);

	if (mvd == mvd->vdev_top)
		vdev_top_transfer(cvd, mvd);

	return (mvd);
}

/*
 * Remove a 1-way mirror/replacing vdev from the tree.
 */
void
vdev_remove_parent(vdev_t *cvd)
{
	vdev_t *mvd = cvd->vdev_parent;
	vdev_t *pvd = mvd->vdev_parent;

	ASSERT(spa_config_held(cvd->vdev_spa, SCL_ALL, RW_WRITER) == SCL_ALL);

	ASSERT(mvd->vdev_children == 1);
	ASSERT(mvd->vdev_ops == &vdev_mirror_ops ||
	    mvd->vdev_ops == &vdev_replacing_ops ||
	    mvd->vdev_ops == &vdev_spare_ops);
	cvd->vdev_ashift = mvd->vdev_ashift;
	cvd->vdev_logical_ashift = mvd->vdev_logical_ashift;
	cvd->vdev_physical_ashift = mvd->vdev_physical_ashift;
	vdev_remove_child(mvd, cvd);
	vdev_remove_child(pvd, mvd);

	/*
	 * If cvd will replace mvd as a top-level vdev, preserve mvd's guid.
	 * Otherwise, we could have detached an offline device, and when we
	 * go to import the pool we'll think we have two top-level vdevs,
	 * instead of a different version of the same top-level vdev.
	 */
	if (mvd->vdev_top == mvd) {
		uint64_t guid_delta = mvd->vdev_guid - cvd->vdev_guid;
		cvd->vdev_orig_guid = cvd->vdev_guid;
		cvd->vdev_guid += guid_delta;
		cvd->vdev_guid_sum += guid_delta;

		/*
		 * If pool not set for autoexpand, we need to also preserve
		 * mvd's asize to prevent automatic expansion of cvd.
		 * Otherwise if we are adjusting the mirror by attaching and
		 * detaching children of non-uniform sizes, the mirror could
		 * autoexpand, unexpectedly requiring larger devices to
		 * re-establish the mirror.
		 */
		if (!cvd->vdev_spa->spa_autoexpand)
			cvd->vdev_asize = mvd->vdev_asize;
	}
	cvd->vdev_id = mvd->vdev_id;
	vdev_add_child(pvd, cvd);
	vdev_top_update(cvd->vdev_top, cvd->vdev_top);

	if (cvd == cvd->vdev_top)
		vdev_top_transfer(mvd, cvd);

	ASSERT(mvd->vdev_children == 0);
	vdev_free(mvd);
}

void
vdev_metaslab_group_create(vdev_t *vd)
{
	spa_t *spa = vd->vdev_spa;

	/*
	 * metaslab_group_create was delayed until allocation bias was available
	 */
	if (vd->vdev_mg == NULL) {
		metaslab_class_t *mc;

		if (vd->vdev_islog && vd->vdev_alloc_bias == VDEV_BIAS_NONE)
			vd->vdev_alloc_bias = VDEV_BIAS_LOG;

		ASSERT3U(vd->vdev_islog, ==,
		    (vd->vdev_alloc_bias == VDEV_BIAS_LOG));

		switch (vd->vdev_alloc_bias) {
		case VDEV_BIAS_LOG:
			mc = spa_log_class(spa);
			break;
		case VDEV_BIAS_SPECIAL:
			mc = spa_special_class(spa);
			break;
		case VDEV_BIAS_DEDUP:
			mc = spa_dedup_class(spa);
			break;
		default:
			mc = spa_normal_class(spa);
		}

		vd->vdev_mg = metaslab_group_create(mc, vd,
		    spa->spa_alloc_count);

		if (!vd->vdev_islog) {
			vd->vdev_log_mg = metaslab_group_create(
			    spa_embedded_log_class(spa), vd, 1);
		}

		/*
		 * The spa ashift min/max only apply for the normal metaslab
		 * class. Class destination is late binding so ashift boundary
		 * setting had to wait until now.
		 */
		if (vd->vdev_top == vd && vd->vdev_ashift != 0 &&
		    mc == spa_normal_class(spa) && vd->vdev_aux == NULL) {
			if (vd->vdev_ashift > spa->spa_max_ashift)
				spa->spa_max_ashift = vd->vdev_ashift;
			if (vd->vdev_ashift < spa->spa_min_ashift)
				spa->spa_min_ashift = vd->vdev_ashift;

			uint64_t min_alloc = vdev_get_min_alloc(vd);
			if (min_alloc < spa->spa_min_alloc)
				spa->spa_min_alloc = min_alloc;
		}
	}
}

int
vdev_metaslab_init(vdev_t *vd, uint64_t txg)
{
	spa_t *spa = vd->vdev_spa;
	uint64_t oldc = vd->vdev_ms_count;
	uint64_t newc = vd->vdev_asize >> vd->vdev_ms_shift;
	metaslab_t **mspp;
	int error;
	boolean_t expanding = (oldc != 0);

	ASSERT(txg == 0 || spa_config_held(spa, SCL_ALLOC, RW_WRITER));

	/*
	 * This vdev is not being allocated from yet or is a hole.
	 */
	if (vd->vdev_ms_shift == 0)
		return (0);

	ASSERT(!vd->vdev_ishole);

	ASSERT(oldc <= newc);

	mspp = vmem_zalloc(newc * sizeof (*mspp), KM_SLEEP);

	if (expanding) {
		bcopy(vd->vdev_ms, mspp, oldc * sizeof (*mspp));
		vmem_free(vd->vdev_ms, oldc * sizeof (*mspp));
	}

	vd->vdev_ms = mspp;
	vd->vdev_ms_count = newc;

	for (uint64_t m = oldc; m < newc; m++) {
		uint64_t object = 0;
		/*
		 * vdev_ms_array may be 0 if we are creating the "fake"
		 * metaslabs for an indirect vdev for zdb's leak detection.
		 * See zdb_leak_init().
		 */
		if (txg == 0 && vd->vdev_ms_array != 0) {
			error = dmu_read(spa->spa_meta_objset,
			    vd->vdev_ms_array,
			    m * sizeof (uint64_t), sizeof (uint64_t), &object,
			    DMU_READ_PREFETCH);
			if (error != 0) {
				vdev_dbgmsg(vd, "unable to read the metaslab "
				    "array [error=%d]", error);
				return (error);
			}
		}

		error = metaslab_init(vd->vdev_mg, m, object, txg,
		    &(vd->vdev_ms[m]));
		if (error != 0) {
			vdev_dbgmsg(vd, "metaslab_init failed [error=%d]",
			    error);
			return (error);
		}
	}

	/*
	 * Find the emptiest metaslab on the vdev and mark it for use for
	 * embedded slog by moving it from the regular to the log metaslab
	 * group.
	 */
	if (vd->vdev_mg->mg_class == spa_normal_class(spa) &&
	    vd->vdev_ms_count > zfs_embedded_slog_min_ms &&
	    avl_is_empty(&vd->vdev_log_mg->mg_metaslab_tree)) {
		uint64_t slog_msid = 0;
		uint64_t smallest = UINT64_MAX;

		/*
		 * Note, we only search the new metaslabs, because the old
		 * (pre-existing) ones may be active (e.g. have non-empty
		 * range_tree's), and we don't move them to the new
		 * metaslab_t.
		 */
		for (uint64_t m = oldc; m < newc; m++) {
			uint64_t alloc =
			    space_map_allocated(vd->vdev_ms[m]->ms_sm);
			if (alloc < smallest) {
				slog_msid = m;
				smallest = alloc;
			}
		}
		metaslab_t *slog_ms = vd->vdev_ms[slog_msid];
		/*
		 * The metaslab was marked as dirty at the end of
		 * metaslab_init(). Remove it from the dirty list so that we
		 * can uninitialize and reinitialize it to the new class.
		 */
		if (txg != 0) {
			(void) txg_list_remove_this(&vd->vdev_ms_list,
			    slog_ms, txg);
		}
		uint64_t sm_obj = space_map_object(slog_ms->ms_sm);
		metaslab_fini(slog_ms);
		VERIFY0(metaslab_init(vd->vdev_log_mg, slog_msid, sm_obj, txg,
		    &vd->vdev_ms[slog_msid]));
	}

	if (txg == 0)
		spa_config_enter(spa, SCL_ALLOC, FTAG, RW_WRITER);

	/*
	 * If the vdev is being removed we don't activate
	 * the metaslabs since we want to ensure that no new
	 * allocations are performed on this device.
	 */
	if (!expanding && !vd->vdev_removing) {
		metaslab_group_activate(vd->vdev_mg);
		if (vd->vdev_log_mg != NULL)
			metaslab_group_activate(vd->vdev_log_mg);
	}

	if (txg == 0)
		spa_config_exit(spa, SCL_ALLOC, FTAG);

	/*
	 * Regardless whether this vdev was just added or it is being
	 * expanded, the metaslab count has changed. Recalculate the
	 * block limit.
	 */
	spa_log_sm_set_blocklimit(spa);

	return (0);
}

void
vdev_metaslab_fini(vdev_t *vd)
{
	if (vd->vdev_checkpoint_sm != NULL) {
		ASSERT(spa_feature_is_active(vd->vdev_spa,
		    SPA_FEATURE_POOL_CHECKPOINT));
		space_map_close(vd->vdev_checkpoint_sm);
		/*
		 * Even though we close the space map, we need to set its
		 * pointer to NULL. The reason is that vdev_metaslab_fini()
		 * may be called multiple times for certain operations
		 * (i.e. when destroying a pool) so we need to ensure that
		 * this clause never executes twice. This logic is similar
		 * to the one used for the vdev_ms clause below.
		 */
		vd->vdev_checkpoint_sm = NULL;
	}

	if (vd->vdev_ms != NULL) {
		metaslab_group_t *mg = vd->vdev_mg;

		metaslab_group_passivate(mg);
		if (vd->vdev_log_mg != NULL) {
			ASSERT(!vd->vdev_islog);
			metaslab_group_passivate(vd->vdev_log_mg);
		}

		uint64_t count = vd->vdev_ms_count;
		for (uint64_t m = 0; m < count; m++) {
			metaslab_t *msp = vd->vdev_ms[m];
			if (msp != NULL)
				metaslab_fini(msp);
		}
		vmem_free(vd->vdev_ms, count * sizeof (metaslab_t *));
		vd->vdev_ms = NULL;
		vd->vdev_ms_count = 0;

		for (int i = 0; i < RANGE_TREE_HISTOGRAM_SIZE; i++) {
			ASSERT0(mg->mg_histogram[i]);
			if (vd->vdev_log_mg != NULL)
				ASSERT0(vd->vdev_log_mg->mg_histogram[i]);
		}
	}
	ASSERT0(vd->vdev_ms_count);
	ASSERT3U(vd->vdev_pending_fastwrite, ==, 0);
}

typedef struct vdev_probe_stats {
	boolean_t	vps_readable;
	boolean_t	vps_writeable;
	int		vps_flags;
} vdev_probe_stats_t;

static void
vdev_probe_done(zio_t *zio)
{
	spa_t *spa = zio->io_spa;
	vdev_t *vd = zio->io_vd;
	vdev_probe_stats_t *vps = zio->io_private;

	ASSERT(vd->vdev_probe_zio != NULL);

	if (zio->io_type == ZIO_TYPE_READ) {
		if (zio->io_error == 0)
			vps->vps_readable = 1;
		if (zio->io_error == 0 && spa_writeable(spa)) {
			zio_nowait(zio_write_phys(vd->vdev_probe_zio, vd,
			    zio->io_offset, zio->io_size, zio->io_abd,
			    ZIO_CHECKSUM_OFF, vdev_probe_done, vps,
			    ZIO_PRIORITY_SYNC_WRITE, vps->vps_flags, B_TRUE));
		} else {
			abd_free(zio->io_abd);
		}
	} else if (zio->io_type == ZIO_TYPE_WRITE) {
		if (zio->io_error == 0)
			vps->vps_writeable = 1;
		abd_free(zio->io_abd);
	} else if (zio->io_type == ZIO_TYPE_NULL) {
		zio_t *pio;
		zio_link_t *zl;

		vd->vdev_cant_read |= !vps->vps_readable;
		vd->vdev_cant_write |= !vps->vps_writeable;

		if (vdev_readable(vd) &&
		    (vdev_writeable(vd) || !spa_writeable(spa))) {
			zio->io_error = 0;
		} else {
			ASSERT(zio->io_error != 0);
			vdev_dbgmsg(vd, "failed probe");
			(void) zfs_ereport_post(FM_EREPORT_ZFS_PROBE_FAILURE,
			    spa, vd, NULL, NULL, 0);
			zio->io_error = SET_ERROR(ENXIO);
		}

		mutex_enter(&vd->vdev_probe_lock);
		ASSERT(vd->vdev_probe_zio == zio);
		vd->vdev_probe_zio = NULL;
		mutex_exit(&vd->vdev_probe_lock);

		zl = NULL;
		while ((pio = zio_walk_parents(zio, &zl)) != NULL)
			if (!vdev_accessible(vd, pio))
				pio->io_error = SET_ERROR(ENXIO);

		kmem_free(vps, sizeof (*vps));
	}
}

/*
 * Determine whether this device is accessible.
 *
 * Read and write to several known locations: the pad regions of each
 * vdev label but the first, which we leave alone in case it contains
 * a VTOC.
 */
zio_t *
vdev_probe(vdev_t *vd, zio_t *zio)
{
	spa_t *spa = vd->vdev_spa;
	vdev_probe_stats_t *vps = NULL;
	zio_t *pio;

	ASSERT(vd->vdev_ops->vdev_op_leaf);

	/*
	 * Don't probe the probe.
	 */
	if (zio && (zio->io_flags & ZIO_FLAG_PROBE))
		return (NULL);

	/*
	 * To prevent 'probe storms' when a device fails, we create
	 * just one probe i/o at a time.  All zios that want to probe
	 * this vdev will become parents of the probe io.
	 */
	mutex_enter(&vd->vdev_probe_lock);

	if ((pio = vd->vdev_probe_zio) == NULL) {
		vps = kmem_zalloc(sizeof (*vps), KM_SLEEP);

		vps->vps_flags = ZIO_FLAG_CANFAIL | ZIO_FLAG_PROBE |
		    ZIO_FLAG_DONT_CACHE | ZIO_FLAG_DONT_AGGREGATE |
		    ZIO_FLAG_TRYHARD;

		if (spa_config_held(spa, SCL_ZIO, RW_WRITER)) {
			/*
			 * vdev_cant_read and vdev_cant_write can only
			 * transition from TRUE to FALSE when we have the
			 * SCL_ZIO lock as writer; otherwise they can only
			 * transition from FALSE to TRUE.  This ensures that
			 * any zio looking at these values can assume that
			 * failures persist for the life of the I/O.  That's
			 * important because when a device has intermittent
			 * connectivity problems, we want to ensure that
			 * they're ascribed to the device (ENXIO) and not
			 * the zio (EIO).
			 *
			 * Since we hold SCL_ZIO as writer here, clear both
			 * values so the probe can reevaluate from first
			 * principles.
			 */
			vps->vps_flags |= ZIO_FLAG_CONFIG_WRITER;
			vd->vdev_cant_read = B_FALSE;
			vd->vdev_cant_write = B_FALSE;
		}

		vd->vdev_probe_zio = pio = zio_null(NULL, spa, vd,
		    vdev_probe_done, vps,
		    vps->vps_flags | ZIO_FLAG_DONT_PROPAGATE);

		/*
		 * We can't change the vdev state in this context, so we
		 * kick off an async task to do it on our behalf.
		 */
		if (zio != NULL) {
			vd->vdev_probe_wanted = B_TRUE;
			spa_async_request(spa, SPA_ASYNC_PROBE);
		}
	}

	if (zio != NULL)
		zio_add_child(zio, pio);

	mutex_exit(&vd->vdev_probe_lock);

	if (vps == NULL) {
		ASSERT(zio != NULL);
		return (NULL);
	}

	for (int l = 1; l < VDEV_LABELS; l++) {
		zio_nowait(zio_read_phys(pio, vd,
		    vdev_label_offset(vd->vdev_psize, l,
		    offsetof(vdev_label_t, vl_be)), VDEV_PAD_SIZE,
		    abd_alloc_for_io(VDEV_PAD_SIZE, B_TRUE),
		    ZIO_CHECKSUM_OFF, vdev_probe_done, vps,
		    ZIO_PRIORITY_SYNC_READ, vps->vps_flags, B_TRUE));
	}

	if (zio == NULL)
		return (pio);

	zio_nowait(pio);
	return (NULL);
}

static void
vdev_load_child(void *arg)
{
	vdev_t *vd = arg;

	vd->vdev_load_error = vdev_load(vd);
}

static void
vdev_open_child(void *arg)
{
	vdev_t *vd = arg;

	vd->vdev_open_thread = curthread;
	vd->vdev_open_error = vdev_open(vd);
	vd->vdev_open_thread = NULL;
}

static boolean_t
vdev_uses_zvols(vdev_t *vd)
{
#ifdef _KERNEL
	if (zvol_is_zvol(vd->vdev_path))
		return (B_TRUE);
#endif

	for (int c = 0; c < vd->vdev_children; c++)
		if (vdev_uses_zvols(vd->vdev_child[c]))
			return (B_TRUE);

	return (B_FALSE);
}

/*
 * Returns B_TRUE if the passed child should be opened.
 */
static boolean_t
vdev_default_open_children_func(vdev_t *vd)
{
	return (B_TRUE);
}

/*
 * Open the requested child vdevs.  If any of the leaf vdevs are using
 * a ZFS volume then do the opens in a single thread.  This avoids a
 * deadlock when the current thread is holding the spa_namespace_lock.
 */
static void
vdev_open_children_impl(vdev_t *vd, vdev_open_children_func_t *open_func)
{
	int children = vd->vdev_children;

	taskq_t *tq = taskq_create("vdev_open", children, minclsyspri,
	    children, children, TASKQ_PREPOPULATE);
	vd->vdev_nonrot = B_TRUE;

	for (int c = 0; c < children; c++) {
		vdev_t *cvd = vd->vdev_child[c];

		if (open_func(cvd) == B_FALSE)
			continue;

		if (tq == NULL || vdev_uses_zvols(vd)) {
			cvd->vdev_open_error = vdev_open(cvd);
		} else {
			VERIFY(taskq_dispatch(tq, vdev_open_child,
			    cvd, TQ_SLEEP) != TASKQID_INVALID);
		}

		vd->vdev_nonrot &= cvd->vdev_nonrot;
	}

	if (tq != NULL) {
		taskq_wait(tq);
		taskq_destroy(tq);
	}
}

/*
 * Open all child vdevs.
 */
void
vdev_open_children(vdev_t *vd)
{
	vdev_open_children_impl(vd, vdev_default_open_children_func);
}

/*
 * Conditionally open a subset of child vdevs.
 */
void
vdev_open_children_subset(vdev_t *vd, vdev_open_children_func_t *open_func)
{
	vdev_open_children_impl(vd, open_func);
}

/*
 * Compute the raidz-deflation ratio.  Note, we hard-code
 * in 128k (1 << 17) because it is the "typical" blocksize.
 * Even though SPA_MAXBLOCKSIZE changed, this algorithm can not change,
 * otherwise it would inconsistently account for existing bp's.
 */
static void
vdev_set_deflate_ratio(vdev_t *vd)
{
	if (vd == vd->vdev_top && !vd->vdev_ishole && vd->vdev_ashift != 0) {
		vd->vdev_deflate_ratio = (1 << 17) /
		    (vdev_psize_to_asize(vd, 1 << 17) >> SPA_MINBLOCKSHIFT);
	}
}

/*
 * Maximize performance by inflating the configured ashift for top level
 * vdevs to be as close to the physical ashift as possible while maintaining
 * administrator defined limits and ensuring it doesn't go below the
 * logical ashift.
 */
static void
vdev_ashift_optimize(vdev_t *vd)
{
	ASSERT(vd == vd->vdev_top);

	if (vd->vdev_ashift < vd->vdev_physical_ashift) {
		vd->vdev_ashift = MIN(
		    MAX(zfs_vdev_max_auto_ashift, vd->vdev_ashift),
		    MAX(zfs_vdev_min_auto_ashift,
		    vd->vdev_physical_ashift));
	} else {
		/*
		 * If the logical and physical ashifts are the same, then
		 * we ensure that the top-level vdev's ashift is not smaller
		 * than our minimum ashift value. For the unusual case
		 * where logical ashift > physical ashift, we can't cap
		 * the calculated ashift based on max ashift as that
		 * would cause failures.
		 * We still check if we need to increase it to match
		 * the min ashift.
		 */
		vd->vdev_ashift = MAX(zfs_vdev_min_auto_ashift,
		    vd->vdev_ashift);
	}
}

/*
 * Prepare a virtual device for access.
 */
int
vdev_open(vdev_t *vd)
{
	spa_t *spa = vd->vdev_spa;
	int error;
	uint64_t osize = 0;
	uint64_t max_osize = 0;
	uint64_t asize, max_asize, psize;
	uint64_t logical_ashift = 0;
	uint64_t physical_ashift = 0;

	ASSERT(vd->vdev_open_thread == curthread ||
	    spa_config_held(spa, SCL_STATE_ALL, RW_WRITER) == SCL_STATE_ALL);
	ASSERT(vd->vdev_state == VDEV_STATE_CLOSED ||
	    vd->vdev_state == VDEV_STATE_CANT_OPEN ||
	    vd->vdev_state == VDEV_STATE_OFFLINE);

	vd->vdev_stat.vs_aux = VDEV_AUX_NONE;
	vd->vdev_cant_read = B_FALSE;
	vd->vdev_cant_write = B_FALSE;
	vd->vdev_min_asize = vdev_get_min_asize(vd);

	/*
	 * If this vdev is not removed, check its fault status.  If it's
	 * faulted, bail out of the open.
	 */
	if (!vd->vdev_removed && vd->vdev_faulted) {
		ASSERT(vd->vdev_children == 0);
		ASSERT(vd->vdev_label_aux == VDEV_AUX_ERR_EXCEEDED ||
		    vd->vdev_label_aux == VDEV_AUX_EXTERNAL);
		vdev_set_state(vd, B_TRUE, VDEV_STATE_FAULTED,
		    vd->vdev_label_aux);
		return (SET_ERROR(ENXIO));
	} else if (vd->vdev_offline) {
		ASSERT(vd->vdev_children == 0);
		vdev_set_state(vd, B_TRUE, VDEV_STATE_OFFLINE, VDEV_AUX_NONE);
		return (SET_ERROR(ENXIO));
	}

	error = vd->vdev_ops->vdev_op_open(vd, &osize, &max_osize,
	    &logical_ashift, &physical_ashift);
	/*
	 * Physical volume size should never be larger than its max size, unless
	 * the disk has shrunk while we were reading it or the device is buggy
	 * or damaged: either way it's not safe for use, bail out of the open.
	 */
	if (osize > max_osize) {
		vdev_set_state(vd, B_TRUE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_OPEN_FAILED);
		return (SET_ERROR(ENXIO));
	}

	/*
	 * Reset the vdev_reopening flag so that we actually close
	 * the vdev on error.
	 */
	vd->vdev_reopening = B_FALSE;
	if (zio_injection_enabled && error == 0)
		error = zio_handle_device_injection(vd, NULL, SET_ERROR(ENXIO));

	if (error) {
		if (vd->vdev_removed &&
		    vd->vdev_stat.vs_aux != VDEV_AUX_OPEN_FAILED)
			vd->vdev_removed = B_FALSE;

		if (vd->vdev_stat.vs_aux == VDEV_AUX_CHILDREN_OFFLINE) {
			vdev_set_state(vd, B_TRUE, VDEV_STATE_OFFLINE,
			    vd->vdev_stat.vs_aux);
		} else {
			vdev_set_state(vd, B_TRUE, VDEV_STATE_CANT_OPEN,
			    vd->vdev_stat.vs_aux);
		}
		return (error);
	}

	vd->vdev_removed = B_FALSE;

	/*
	 * Recheck the faulted flag now that we have confirmed that
	 * the vdev is accessible.  If we're faulted, bail.
	 */
	if (vd->vdev_faulted) {
		ASSERT(vd->vdev_children == 0);
		ASSERT(vd->vdev_label_aux == VDEV_AUX_ERR_EXCEEDED ||
		    vd->vdev_label_aux == VDEV_AUX_EXTERNAL);
		vdev_set_state(vd, B_TRUE, VDEV_STATE_FAULTED,
		    vd->vdev_label_aux);
		return (SET_ERROR(ENXIO));
	}

	if (vd->vdev_degraded) {
		ASSERT(vd->vdev_children == 0);
		vdev_set_state(vd, B_TRUE, VDEV_STATE_DEGRADED,
		    VDEV_AUX_ERR_EXCEEDED);
	} else {
		vdev_set_state(vd, B_TRUE, VDEV_STATE_HEALTHY, 0);
	}

	/*
	 * For hole or missing vdevs we just return success.
	 */
	if (vd->vdev_ishole || vd->vdev_ops == &vdev_missing_ops)
		return (0);

	for (int c = 0; c < vd->vdev_children; c++) {
		if (vd->vdev_child[c]->vdev_state != VDEV_STATE_HEALTHY) {
			vdev_set_state(vd, B_TRUE, VDEV_STATE_DEGRADED,
			    VDEV_AUX_NONE);
			break;
		}
	}

	osize = P2ALIGN(osize, (uint64_t)sizeof (vdev_label_t));
	max_osize = P2ALIGN(max_osize, (uint64_t)sizeof (vdev_label_t));

	if (vd->vdev_children == 0) {
		if (osize < SPA_MINDEVSIZE) {
			vdev_set_state(vd, B_TRUE, VDEV_STATE_CANT_OPEN,
			    VDEV_AUX_TOO_SMALL);
			return (SET_ERROR(EOVERFLOW));
		}
		psize = osize;
		asize = osize - (VDEV_LABEL_START_SIZE + VDEV_LABEL_END_SIZE);
		max_asize = max_osize - (VDEV_LABEL_START_SIZE +
		    VDEV_LABEL_END_SIZE);
	} else {
		if (vd->vdev_parent != NULL && osize < SPA_MINDEVSIZE -
		    (VDEV_LABEL_START_SIZE + VDEV_LABEL_END_SIZE)) {
			vdev_set_state(vd, B_TRUE, VDEV_STATE_CANT_OPEN,
			    VDEV_AUX_TOO_SMALL);
			return (SET_ERROR(EOVERFLOW));
		}
		psize = 0;
		asize = osize;
		max_asize = max_osize;
	}

	/*
	 * If the vdev was expanded, record this so that we can re-create the
	 * uberblock rings in labels {2,3}, during the next sync.
	 */
	if ((psize > vd->vdev_psize) && (vd->vdev_psize != 0))
		vd->vdev_copy_uberblocks = B_TRUE;

	vd->vdev_psize = psize;

	/*
	 * Make sure the allocatable size hasn't shrunk too much.
	 */
	if (asize < vd->vdev_min_asize) {
		vdev_set_state(vd, B_TRUE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_BAD_LABEL);
		return (SET_ERROR(EINVAL));
	}

	/*
	 * We can always set the logical/physical ashift members since
	 * their values are only used to calculate the vdev_ashift when
	 * the device is first added to the config. These values should
	 * not be used for anything else since they may change whenever
	 * the device is reopened and we don't store them in the label.
	 */
	vd->vdev_physical_ashift =
	    MAX(physical_ashift, vd->vdev_physical_ashift);
	vd->vdev_logical_ashift = MAX(logical_ashift,
	    vd->vdev_logical_ashift);

	if (vd->vdev_asize == 0) {
		/*
		 * This is the first-ever open, so use the computed values.
		 * For compatibility, a different ashift can be requested.
		 */
		vd->vdev_asize = asize;
		vd->vdev_max_asize = max_asize;

		/*
		 * If the vdev_ashift was not overridden at creation time,
		 * then set it the logical ashift and optimize the ashift.
		 */
		if (vd->vdev_ashift == 0) {
			vd->vdev_ashift = vd->vdev_logical_ashift;

			if (vd->vdev_logical_ashift > ASHIFT_MAX) {
				vdev_set_state(vd, B_TRUE, VDEV_STATE_CANT_OPEN,
				    VDEV_AUX_ASHIFT_TOO_BIG);
				return (SET_ERROR(EDOM));
			}

			if (vd->vdev_top == vd) {
				vdev_ashift_optimize(vd);
			}
		}
		if (vd->vdev_ashift != 0 && (vd->vdev_ashift < ASHIFT_MIN ||
		    vd->vdev_ashift > ASHIFT_MAX)) {
			vdev_set_state(vd, B_TRUE, VDEV_STATE_CANT_OPEN,
			    VDEV_AUX_BAD_ASHIFT);
			return (SET_ERROR(EDOM));
		}
	} else {
		/*
		 * Make sure the alignment required hasn't increased.
		 */
		if (vd->vdev_ashift > vd->vdev_top->vdev_ashift &&
		    vd->vdev_ops->vdev_op_leaf) {
			(void) zfs_ereport_post(
			    FM_EREPORT_ZFS_DEVICE_BAD_ASHIFT,
			    spa, vd, NULL, NULL, 0);
			vdev_set_state(vd, B_TRUE, VDEV_STATE_CANT_OPEN,
			    VDEV_AUX_BAD_LABEL);
			return (SET_ERROR(EDOM));
		}
		vd->vdev_max_asize = max_asize;
	}

	/*
	 * If all children are healthy we update asize if either:
	 * The asize has increased, due to a device expansion caused by dynamic
	 * LUN growth or vdev replacement, and automatic expansion is enabled;
	 * making the additional space available.
	 *
	 * The asize has decreased, due to a device shrink usually caused by a
	 * vdev replace with a smaller device. This ensures that calculations
	 * based of max_asize and asize e.g. esize are always valid. It's safe
	 * to do this as we've already validated that asize is greater than
	 * vdev_min_asize.
	 */
	if (vd->vdev_state == VDEV_STATE_HEALTHY &&
	    ((asize > vd->vdev_asize &&
	    (vd->vdev_expanding || spa->spa_autoexpand)) ||
	    (asize < vd->vdev_asize)))
		vd->vdev_asize = asize;

	vdev_set_min_asize(vd);

	/*
	 * Ensure we can issue some IO before declaring the
	 * vdev open for business.
	 */
	if (vd->vdev_ops->vdev_op_leaf &&
	    (error = zio_wait(vdev_probe(vd, NULL))) != 0) {
		vdev_set_state(vd, B_TRUE, VDEV_STATE_FAULTED,
		    VDEV_AUX_ERR_EXCEEDED);
		return (error);
	}

	/*
	 * Track the minimum allocation size.
	 */
	if (vd->vdev_top == vd && vd->vdev_ashift != 0 &&
	    vd->vdev_islog == 0 && vd->vdev_aux == NULL) {
		uint64_t min_alloc = vdev_get_min_alloc(vd);
		if (min_alloc < spa->spa_min_alloc)
			spa->spa_min_alloc = min_alloc;
	}

	/*
	 * If this is a leaf vdev, assess whether a resilver is needed.
	 * But don't do this if we are doing a reopen for a scrub, since
	 * this would just restart the scrub we are already doing.
	 */
	if (vd->vdev_ops->vdev_op_leaf && !spa->spa_scrub_reopen)
		dsl_scan_assess_vdev(spa->spa_dsl_pool, vd);

	return (0);
}

static void
vdev_validate_child(void *arg)
{
	vdev_t *vd = arg;

	vd->vdev_validate_thread = curthread;
	vd->vdev_validate_error = vdev_validate(vd);
	vd->vdev_validate_thread = NULL;
}

/*
 * Called once the vdevs are all opened, this routine validates the label
 * contents. This needs to be done before vdev_load() so that we don't
 * inadvertently do repair I/Os to the wrong device.
 *
 * This function will only return failure if one of the vdevs indicates that it
 * has since been destroyed or exported.  This is only possible if
 * /etc/zfs/zpool.cache was readonly at the time.  Otherwise, the vdev state
 * will be updated but the function will return 0.
 */
int
vdev_validate(vdev_t *vd)
{
	spa_t *spa = vd->vdev_spa;
	taskq_t *tq = NULL;
	nvlist_t *label;
	uint64_t guid = 0, aux_guid = 0, top_guid;
	uint64_t state;
	nvlist_t *nvl;
	uint64_t txg;
	int children = vd->vdev_children;

	if (vdev_validate_skip)
		return (0);

	if (children > 0) {
		tq = taskq_create("vdev_validate", children, minclsyspri,
		    children, children, TASKQ_PREPOPULATE);
	}

	for (uint64_t c = 0; c < children; c++) {
		vdev_t *cvd = vd->vdev_child[c];

		if (tq == NULL || vdev_uses_zvols(cvd)) {
			vdev_validate_child(cvd);
		} else {
			VERIFY(taskq_dispatch(tq, vdev_validate_child, cvd,
			    TQ_SLEEP) != TASKQID_INVALID);
		}
	}
	if (tq != NULL) {
		taskq_wait(tq);
		taskq_destroy(tq);
	}
	for (int c = 0; c < children; c++) {
		int error = vd->vdev_child[c]->vdev_validate_error;

		if (error != 0)
			return (SET_ERROR(EBADF));
	}


	/*
	 * If the device has already failed, or was marked offline, don't do
	 * any further validation.  Otherwise, label I/O will fail and we will
	 * overwrite the previous state.
	 */
	if (!vd->vdev_ops->vdev_op_leaf || !vdev_readable(vd))
		return (0);

	/*
	 * If we are performing an extreme rewind, we allow for a label that
	 * was modified at a point after the current txg.
	 * If config lock is not held do not check for the txg. spa_sync could
	 * be updating the vdev's label before updating spa_last_synced_txg.
	 */
	if (spa->spa_extreme_rewind || spa_last_synced_txg(spa) == 0 ||
	    spa_config_held(spa, SCL_CONFIG, RW_WRITER) != SCL_CONFIG)
		txg = UINT64_MAX;
	else
		txg = spa_last_synced_txg(spa);

	if ((label = vdev_label_read_config(vd, txg)) == NULL) {
		vdev_set_state(vd, B_FALSE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_BAD_LABEL);
		vdev_dbgmsg(vd, "vdev_validate: failed reading config for "
		    "txg %llu", (u_longlong_t)txg);
		return (0);
	}

	/*
	 * Determine if this vdev has been split off into another
	 * pool.  If so, then refuse to open it.
	 */
	if (nvlist_lookup_uint64(label, ZPOOL_CONFIG_SPLIT_GUID,
	    &aux_guid) == 0 && aux_guid == spa_guid(spa)) {
		vdev_set_state(vd, B_FALSE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_SPLIT_POOL);
		nvlist_free(label);
		vdev_dbgmsg(vd, "vdev_validate: vdev split into other pool");
		return (0);
	}

	if (nvlist_lookup_uint64(label, ZPOOL_CONFIG_POOL_GUID, &guid) != 0) {
		vdev_set_state(vd, B_FALSE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_CORRUPT_DATA);
		nvlist_free(label);
		vdev_dbgmsg(vd, "vdev_validate: '%s' missing from label",
		    ZPOOL_CONFIG_POOL_GUID);
		return (0);
	}

	/*
	 * If config is not trusted then ignore the spa guid check. This is
	 * necessary because if the machine crashed during a re-guid the new
	 * guid might have been written to all of the vdev labels, but not the
	 * cached config. The check will be performed again once we have the
	 * trusted config from the MOS.
	 */
	if (spa->spa_trust_config && guid != spa_guid(spa)) {
		vdev_set_state(vd, B_FALSE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_CORRUPT_DATA);
		nvlist_free(label);
		vdev_dbgmsg(vd, "vdev_validate: vdev label pool_guid doesn't "
		    "match config (%llu != %llu)", (u_longlong_t)guid,
		    (u_longlong_t)spa_guid(spa));
		return (0);
	}

	if (nvlist_lookup_nvlist(label, ZPOOL_CONFIG_VDEV_TREE, &nvl)
	    != 0 || nvlist_lookup_uint64(nvl, ZPOOL_CONFIG_ORIG_GUID,
	    &aux_guid) != 0)
		aux_guid = 0;

	if (nvlist_lookup_uint64(label, ZPOOL_CONFIG_GUID, &guid) != 0) {
		vdev_set_state(vd, B_FALSE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_CORRUPT_DATA);
		nvlist_free(label);
		vdev_dbgmsg(vd, "vdev_validate: '%s' missing from label",
		    ZPOOL_CONFIG_GUID);
		return (0);
	}

	if (nvlist_lookup_uint64(label, ZPOOL_CONFIG_TOP_GUID, &top_guid)
	    != 0) {
		vdev_set_state(vd, B_FALSE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_CORRUPT_DATA);
		nvlist_free(label);
		vdev_dbgmsg(vd, "vdev_validate: '%s' missing from label",
		    ZPOOL_CONFIG_TOP_GUID);
		return (0);
	}

	/*
	 * If this vdev just became a top-level vdev because its sibling was
	 * detached, it will have adopted the parent's vdev guid -- but the
	 * label may or may not be on disk yet. Fortunately, either version
	 * of the label will have the same top guid, so if we're a top-level
	 * vdev, we can safely compare to that instead.
	 * However, if the config comes from a cachefile that failed to update
	 * after the detach, a top-level vdev will appear as a non top-level
	 * vdev in the config. Also relax the constraints if we perform an
	 * extreme rewind.
	 *
	 * If we split this vdev off instead, then we also check the
	 * original pool's guid. We don't want to consider the vdev
	 * corrupt if it is partway through a split operation.
	 */
	if (vd->vdev_guid != guid && vd->vdev_guid != aux_guid) {
		boolean_t mismatch = B_FALSE;
		if (spa->spa_trust_config && !spa->spa_extreme_rewind) {
			if (vd != vd->vdev_top || vd->vdev_guid != top_guid)
				mismatch = B_TRUE;
		} else {
			if (vd->vdev_guid != top_guid &&
			    vd->vdev_top->vdev_guid != guid)
				mismatch = B_TRUE;
		}

		if (mismatch) {
			vdev_set_state(vd, B_FALSE, VDEV_STATE_CANT_OPEN,
			    VDEV_AUX_CORRUPT_DATA);
			nvlist_free(label);
			vdev_dbgmsg(vd, "vdev_validate: config guid "
			    "doesn't match label guid");
			vdev_dbgmsg(vd, "CONFIG: guid %llu, top_guid %llu",
			    (u_longlong_t)vd->vdev_guid,
			    (u_longlong_t)vd->vdev_top->vdev_guid);
			vdev_dbgmsg(vd, "LABEL: guid %llu, top_guid %llu, "
			    "aux_guid %llu", (u_longlong_t)guid,
			    (u_longlong_t)top_guid, (u_longlong_t)aux_guid);
			return (0);
		}
	}

	if (nvlist_lookup_uint64(label, ZPOOL_CONFIG_POOL_STATE,
	    &state) != 0) {
		vdev_set_state(vd, B_FALSE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_CORRUPT_DATA);
		nvlist_free(label);
		vdev_dbgmsg(vd, "vdev_validate: '%s' missing from label",
		    ZPOOL_CONFIG_POOL_STATE);
		return (0);
	}

	nvlist_free(label);

	/*
	 * If this is a verbatim import, no need to check the
	 * state of the pool.
	 */
	if (!(spa->spa_import_flags & ZFS_IMPORT_VERBATIM) &&
	    spa_load_state(spa) == SPA_LOAD_OPEN &&
	    state != POOL_STATE_ACTIVE) {
		vdev_dbgmsg(vd, "vdev_validate: invalid pool state (%llu) "
		    "for spa %s", (u_longlong_t)state, spa->spa_name);
		return (SET_ERROR(EBADF));
	}

	/*
	 * If we were able to open and validate a vdev that was
	 * previously marked permanently unavailable, clear that state
	 * now.
	 */
	if (vd->vdev_not_present)
		vd->vdev_not_present = 0;

	return (0);
}

static void
vdev_copy_path_impl(vdev_t *svd, vdev_t *dvd)
{
	char *old, *new;
	if (svd->vdev_path != NULL && dvd->vdev_path != NULL) {
		if (strcmp(svd->vdev_path, dvd->vdev_path) != 0) {
			zfs_dbgmsg("vdev_copy_path: vdev %llu: path changed "
			    "from '%s' to '%s'", (u_longlong_t)dvd->vdev_guid,
			    dvd->vdev_path, svd->vdev_path);
			spa_strfree(dvd->vdev_path);
			dvd->vdev_path = spa_strdup(svd->vdev_path);
		}
	} else if (svd->vdev_path != NULL) {
		dvd->vdev_path = spa_strdup(svd->vdev_path);
		zfs_dbgmsg("vdev_copy_path: vdev %llu: path set to '%s'",
		    (u_longlong_t)dvd->vdev_guid, dvd->vdev_path);
	}

	/*
	 * Our enclosure sysfs path may have changed between imports
	 */
	old = dvd->vdev_enc_sysfs_path;
	new = svd->vdev_enc_sysfs_path;
	if ((old != NULL && new == NULL) ||
	    (old == NULL && new != NULL) ||
	    ((old != NULL && new != NULL) && strcmp(new, old) != 0)) {
		zfs_dbgmsg("vdev_copy_path: vdev %llu: vdev_enc_sysfs_path "
		    "changed from '%s' to '%s'", (u_longlong_t)dvd->vdev_guid,
		    old, new);

		if (dvd->vdev_enc_sysfs_path)
			spa_strfree(dvd->vdev_enc_sysfs_path);

		if (svd->vdev_enc_sysfs_path) {
			dvd->vdev_enc_sysfs_path = spa_strdup(
			    svd->vdev_enc_sysfs_path);
		} else {
			dvd->vdev_enc_sysfs_path = NULL;
		}
	}
}

/*
 * Recursively copy vdev paths from one vdev to another. Source and destination
 * vdev trees must have same geometry otherwise return error. Intended to copy
 * paths from userland config into MOS config.
 */
int
vdev_copy_path_strict(vdev_t *svd, vdev_t *dvd)
{
	if ((svd->vdev_ops == &vdev_missing_ops) ||
	    (svd->vdev_ishole && dvd->vdev_ishole) ||
	    (dvd->vdev_ops == &vdev_indirect_ops))
		return (0);

	if (svd->vdev_ops != dvd->vdev_ops) {
		vdev_dbgmsg(svd, "vdev_copy_path: vdev type mismatch: %s != %s",
		    svd->vdev_ops->vdev_op_type, dvd->vdev_ops->vdev_op_type);
		return (SET_ERROR(EINVAL));
	}

	if (svd->vdev_guid != dvd->vdev_guid) {
		vdev_dbgmsg(svd, "vdev_copy_path: guids mismatch (%llu != "
		    "%llu)", (u_longlong_t)svd->vdev_guid,
		    (u_longlong_t)dvd->vdev_guid);
		return (SET_ERROR(EINVAL));
	}

	if (svd->vdev_children != dvd->vdev_children) {
		vdev_dbgmsg(svd, "vdev_copy_path: children count mismatch: "
		    "%llu != %llu", (u_longlong_t)svd->vdev_children,
		    (u_longlong_t)dvd->vdev_children);
		return (SET_ERROR(EINVAL));
	}

	for (uint64_t i = 0; i < svd->vdev_children; i++) {
		int error = vdev_copy_path_strict(svd->vdev_child[i],
		    dvd->vdev_child[i]);
		if (error != 0)
			return (error);
	}

	if (svd->vdev_ops->vdev_op_leaf)
		vdev_copy_path_impl(svd, dvd);

	return (0);
}

static void
vdev_copy_path_search(vdev_t *stvd, vdev_t *dvd)
{
	ASSERT(stvd->vdev_top == stvd);
	ASSERT3U(stvd->vdev_id, ==, dvd->vdev_top->vdev_id);

	for (uint64_t i = 0; i < dvd->vdev_children; i++) {
		vdev_copy_path_search(stvd, dvd->vdev_child[i]);
	}

	if (!dvd->vdev_ops->vdev_op_leaf || !vdev_is_concrete(dvd))
		return;

	/*
	 * The idea here is that while a vdev can shift positions within
	 * a top vdev (when replacing, attaching mirror, etc.) it cannot
	 * step outside of it.
	 */
	vdev_t *vd = vdev_lookup_by_guid(stvd, dvd->vdev_guid);

	if (vd == NULL || vd->vdev_ops != dvd->vdev_ops)
		return;

	ASSERT(vd->vdev_ops->vdev_op_leaf);

	vdev_copy_path_impl(vd, dvd);
}

/*
 * Recursively copy vdev paths from one root vdev to another. Source and
 * destination vdev trees may differ in geometry. For each destination leaf
 * vdev, search a vdev with the same guid and top vdev id in the source.
 * Intended to copy paths from userland config into MOS config.
 */
void
vdev_copy_path_relaxed(vdev_t *srvd, vdev_t *drvd)
{
	uint64_t children = MIN(srvd->vdev_children, drvd->vdev_children);
	ASSERT(srvd->vdev_ops == &vdev_root_ops);
	ASSERT(drvd->vdev_ops == &vdev_root_ops);

	for (uint64_t i = 0; i < children; i++) {
		vdev_copy_path_search(srvd->vdev_child[i],
		    drvd->vdev_child[i]);
	}
}

/*
 * Close a virtual device.
 */
void
vdev_close(vdev_t *vd)
{
	vdev_t *pvd = vd->vdev_parent;
	spa_t *spa __maybe_unused = vd->vdev_spa;

	ASSERT(vd != NULL);
	ASSERT(vd->vdev_open_thread == curthread ||
	    spa_config_held(spa, SCL_STATE_ALL, RW_WRITER) == SCL_STATE_ALL);

	/*
	 * If our parent is reopening, then we are as well, unless we are
	 * going offline.
	 */
	if (pvd != NULL && pvd->vdev_reopening)
		vd->vdev_reopening = (pvd->vdev_reopening && !vd->vdev_offline);

	vd->vdev_ops->vdev_op_close(vd);

	vdev_cache_purge(vd);

	/*
	 * We record the previous state before we close it, so that if we are
	 * doing a reopen(), we don't generate FMA ereports if we notice that
	 * it's still faulted.
	 */
	vd->vdev_prevstate = vd->vdev_state;

	if (vd->vdev_offline)
		vd->vdev_state = VDEV_STATE_OFFLINE;
	else
		vd->vdev_state = VDEV_STATE_CLOSED;
	vd->vdev_stat.vs_aux = VDEV_AUX_NONE;
}

void
vdev_hold(vdev_t *vd)
{
	spa_t *spa = vd->vdev_spa;

	ASSERT(spa_is_root(spa));
	if (spa->spa_state == POOL_STATE_UNINITIALIZED)
		return;

	for (int c = 0; c < vd->vdev_children; c++)
		vdev_hold(vd->vdev_child[c]);

	if (vd->vdev_ops->vdev_op_leaf && vd->vdev_ops->vdev_op_hold != NULL)
		vd->vdev_ops->vdev_op_hold(vd);
}

void
vdev_rele(vdev_t *vd)
{
	ASSERT(spa_is_root(vd->vdev_spa));
	for (int c = 0; c < vd->vdev_children; c++)
		vdev_rele(vd->vdev_child[c]);

	if (vd->vdev_ops->vdev_op_leaf && vd->vdev_ops->vdev_op_rele != NULL)
		vd->vdev_ops->vdev_op_rele(vd);
}

/*
 * Reopen all interior vdevs and any unopened leaves.  We don't actually
 * reopen leaf vdevs which had previously been opened as they might deadlock
 * on the spa_config_lock.  Instead we only obtain the leaf's physical size.
 * If the leaf has never been opened then open it, as usual.
 */
void
vdev_reopen(vdev_t *vd)
{
	spa_t *spa = vd->vdev_spa;

	ASSERT(spa_config_held(spa, SCL_STATE_ALL, RW_WRITER) == SCL_STATE_ALL);

	/* set the reopening flag unless we're taking the vdev offline */
	vd->vdev_reopening = !vd->vdev_offline;
	vdev_close(vd);
	(void) vdev_open(vd);

	/*
	 * Call vdev_validate() here to make sure we have the same device.
	 * Otherwise, a device with an invalid label could be successfully
	 * opened in response to vdev_reopen().
	 */
	if (vd->vdev_aux) {
		(void) vdev_validate_aux(vd);
		if (vdev_readable(vd) && vdev_writeable(vd) &&
		    vd->vdev_aux == &spa->spa_l2cache) {
			/*
			 * In case the vdev is present we should evict all ARC
			 * buffers and pointers to log blocks and reclaim their
			 * space before restoring its contents to L2ARC.
			 */
			if (l2arc_vdev_present(vd)) {
				l2arc_rebuild_vdev(vd, B_TRUE);
			} else {
				l2arc_add_vdev(spa, vd);
			}
			spa_async_request(spa, SPA_ASYNC_L2CACHE_REBUILD);
			spa_async_request(spa, SPA_ASYNC_L2CACHE_TRIM);
		}
	} else {
		(void) vdev_validate(vd);
	}

	/*
	 * Reassess parent vdev's health.
	 */
	vdev_propagate_state(vd);
}

int
vdev_create(vdev_t *vd, uint64_t txg, boolean_t isreplacing)
{
	int error;

	/*
	 * Normally, partial opens (e.g. of a mirror) are allowed.
	 * For a create, however, we want to fail the request if
	 * there are any components we can't open.
	 */
	error = vdev_open(vd);

	if (error || vd->vdev_state != VDEV_STATE_HEALTHY) {
		vdev_close(vd);
		return (error ? error : SET_ERROR(ENXIO));
	}

	/*
	 * Recursively load DTLs and initialize all labels.
	 */
	if ((error = vdev_dtl_load(vd)) != 0 ||
	    (error = vdev_label_init(vd, txg, isreplacing ?
	    VDEV_LABEL_REPLACE : VDEV_LABEL_CREATE)) != 0) {
		vdev_close(vd);
		return (error);
	}

	return (0);
}

void
vdev_metaslab_set_size(vdev_t *vd)
{
	uint64_t asize = vd->vdev_asize;
	uint64_t ms_count = asize >> zfs_vdev_default_ms_shift;
	uint64_t ms_shift;

	/*
	 * There are two dimensions to the metaslab sizing calculation:
	 * the size of the metaslab and the count of metaslabs per vdev.
	 *
	 * The default values used below are a good balance between memory
	 * usage (larger metaslab size means more memory needed for loaded
	 * metaslabs; more metaslabs means more memory needed for the
	 * metaslab_t structs), metaslab load time (larger metaslabs take
	 * longer to load), and metaslab sync time (more metaslabs means
	 * more time spent syncing all of them).
	 *
	 * In general, we aim for zfs_vdev_default_ms_count (200) metaslabs.
	 * The range of the dimensions are as follows:
	 *
	 *	2^29 <= ms_size  <= 2^34
	 *	  16 <= ms_count <= 131,072
	 *
	 * On the lower end of vdev sizes, we aim for metaslabs sizes of
	 * at least 512MB (2^29) to minimize fragmentation effects when
	 * testing with smaller devices.  However, the count constraint
	 * of at least 16 metaslabs will override this minimum size goal.
	 *
	 * On the upper end of vdev sizes, we aim for a maximum metaslab
	 * size of 16GB.  However, we will cap the total count to 2^17
	 * metaslabs to keep our memory footprint in check and let the
	 * metaslab size grow from there if that limit is hit.
	 *
	 * The net effect of applying above constrains is summarized below.
	 *
	 *   vdev size       metaslab count
	 *  --------------|-----------------
	 *      < 8GB        ~16
	 *  8GB   - 100GB   one per 512MB
	 *  100GB - 3TB     ~200
	 *  3TB   - 2PB     one per 16GB
	 *      > 2PB       ~131,072
	 *  --------------------------------
	 *
	 *  Finally, note that all of the above calculate the initial
	 *  number of metaslabs. Expanding a top-level vdev will result
	 *  in additional metaslabs being allocated making it possible
	 *  to exceed the zfs_vdev_ms_count_limit.
	 */

	if (ms_count < zfs_vdev_min_ms_count)
		ms_shift = highbit64(asize / zfs_vdev_min_ms_count);
	else if (ms_count > zfs_vdev_default_ms_count)
		ms_shift = highbit64(asize / zfs_vdev_default_ms_count);
	else
		ms_shift = zfs_vdev_default_ms_shift;

	if (ms_shift < SPA_MAXBLOCKSHIFT) {
		ms_shift = SPA_MAXBLOCKSHIFT;
	} else if (ms_shift > zfs_vdev_max_ms_shift) {
		ms_shift = zfs_vdev_max_ms_shift;
		/* cap the total count to constrain memory footprint */
		if ((asize >> ms_shift) > zfs_vdev_ms_count_limit)
			ms_shift = highbit64(asize / zfs_vdev_ms_count_limit);
	}

	vd->vdev_ms_shift = ms_shift;
	ASSERT3U(vd->vdev_ms_shift, >=, SPA_MAXBLOCKSHIFT);
}

void
vdev_dirty(vdev_t *vd, int flags, void *arg, uint64_t txg)
{
	ASSERT(vd == vd->vdev_top);
	/* indirect vdevs don't have metaslabs or dtls */
	ASSERT(vdev_is_concrete(vd) || flags == 0);
	ASSERT(ISP2(flags));
	ASSERT(spa_writeable(vd->vdev_spa));

	if (flags & VDD_METASLAB)
		(void) txg_list_add(&vd->vdev_ms_list, arg, txg);

	if (flags & VDD_DTL)
		(void) txg_list_add(&vd->vdev_dtl_list, arg, txg);

	(void) txg_list_add(&vd->vdev_spa->spa_vdev_txg_list, vd, txg);
}

void
vdev_dirty_leaves(vdev_t *vd, int flags, uint64_t txg)
{
	for (int c = 0; c < vd->vdev_children; c++)
		vdev_dirty_leaves(vd->vdev_child[c], flags, txg);

	if (vd->vdev_ops->vdev_op_leaf)
		vdev_dirty(vd->vdev_top, flags, vd, txg);
}

/*
 * DTLs.
 *
 * A vdev's DTL (dirty time log) is the set of transaction groups for which
 * the vdev has less than perfect replication.  There are four kinds of DTL:
 *
 * DTL_MISSING: txgs for which the vdev has no valid copies of the data
 *
 * DTL_PARTIAL: txgs for which data is available, but not fully replicated
 *
 * DTL_SCRUB: the txgs that could not be repaired by the last scrub; upon
 *	scrub completion, DTL_SCRUB replaces DTL_MISSING in the range of
 *	txgs that was scrubbed.
 *
 * DTL_OUTAGE: txgs which cannot currently be read, whether due to
 *	persistent errors or just some device being offline.
 *	Unlike the other three, the DTL_OUTAGE map is not generally
 *	maintained; it's only computed when needed, typically to
 *	determine whether a device can be detached.
 *
 * For leaf vdevs, DTL_MISSING and DTL_PARTIAL are identical: the device
 * either has the data or it doesn't.
 *
 * For interior vdevs such as mirror and RAID-Z the picture is more complex.
 * A vdev's DTL_PARTIAL is the union of its children's DTL_PARTIALs, because
 * if any child is less than fully replicated, then so is its parent.
 * A vdev's DTL_MISSING is a modified union of its children's DTL_MISSINGs,
 * comprising only those txgs which appear in 'maxfaults' or more children;
 * those are the txgs we don't have enough replication to read.  For example,
 * double-parity RAID-Z can tolerate up to two missing devices (maxfaults == 2);
 * thus, its DTL_MISSING consists of the set of txgs that appear in more than
 * two child DTL_MISSING maps.
 *
 * It should be clear from the above that to compute the DTLs and outage maps
 * for all vdevs, it suffices to know just the leaf vdevs' DTL_MISSING maps.
 * Therefore, that is all we keep on disk.  When loading the pool, or after
 * a configuration change, we generate all other DTLs from first principles.
 */
void
vdev_dtl_dirty(vdev_t *vd, vdev_dtl_type_t t, uint64_t txg, uint64_t size)
{
	range_tree_t *rt = vd->vdev_dtl[t];

	ASSERT(t < DTL_TYPES);
	ASSERT(vd != vd->vdev_spa->spa_root_vdev);
	ASSERT(spa_writeable(vd->vdev_spa));

	mutex_enter(&vd->vdev_dtl_lock);
	if (!range_tree_contains(rt, txg, size))
		range_tree_add(rt, txg, size);
	mutex_exit(&vd->vdev_dtl_lock);
}

boolean_t
vdev_dtl_contains(vdev_t *vd, vdev_dtl_type_t t, uint64_t txg, uint64_t size)
{
	range_tree_t *rt = vd->vdev_dtl[t];
	boolean_t dirty = B_FALSE;

	ASSERT(t < DTL_TYPES);
	ASSERT(vd != vd->vdev_spa->spa_root_vdev);

	/*
	 * While we are loading the pool, the DTLs have not been loaded yet.
	 * This isn't a problem but it can result in devices being tried
	 * which are known to not have the data.  In which case, the import
	 * is relying on the checksum to ensure that we get the right data.
	 * Note that while importing we are only reading the MOS, which is
	 * always checksummed.
	 */
	mutex_enter(&vd->vdev_dtl_lock);
	if (!range_tree_is_empty(rt))
		dirty = range_tree_contains(rt, txg, size);
	mutex_exit(&vd->vdev_dtl_lock);

	return (dirty);
}

boolean_t
vdev_dtl_empty(vdev_t *vd, vdev_dtl_type_t t)
{
	range_tree_t *rt = vd->vdev_dtl[t];
	boolean_t empty;

	mutex_enter(&vd->vdev_dtl_lock);
	empty = range_tree_is_empty(rt);
	mutex_exit(&vd->vdev_dtl_lock);

	return (empty);
}

/*
 * Check if the txg falls within the range which must be
 * resilvered.  DVAs outside this range can always be skipped.
 */
boolean_t
vdev_default_need_resilver(vdev_t *vd, const dva_t *dva, size_t psize,
    uint64_t phys_birth)
{
	/* Set by sequential resilver. */
	if (phys_birth == TXG_UNKNOWN)
		return (B_TRUE);

	return (vdev_dtl_contains(vd, DTL_PARTIAL, phys_birth, 1));
}

/*
 * Returns B_TRUE if the vdev determines the DVA needs to be resilvered.
 */
boolean_t
vdev_dtl_need_resilver(vdev_t *vd, const dva_t *dva, size_t psize,
    uint64_t phys_birth)
{
	ASSERT(vd != vd->vdev_spa->spa_root_vdev);

	if (vd->vdev_ops->vdev_op_need_resilver == NULL ||
	    vd->vdev_ops->vdev_op_leaf)
		return (B_TRUE);

	return (vd->vdev_ops->vdev_op_need_resilver(vd, dva, psize,
	    phys_birth));
}

/*
 * Returns the lowest txg in the DTL range.
 */
static uint64_t
vdev_dtl_min(vdev_t *vd)
{
	ASSERT(MUTEX_HELD(&vd->vdev_dtl_lock));
	ASSERT3U(range_tree_space(vd->vdev_dtl[DTL_MISSING]), !=, 0);
	ASSERT0(vd->vdev_children);

	return (range_tree_min(vd->vdev_dtl[DTL_MISSING]) - 1);
}

/*
 * Returns the highest txg in the DTL.
 */
static uint64_t
vdev_dtl_max(vdev_t *vd)
{
	ASSERT(MUTEX_HELD(&vd->vdev_dtl_lock));
	ASSERT3U(range_tree_space(vd->vdev_dtl[DTL_MISSING]), !=, 0);
	ASSERT0(vd->vdev_children);

	return (range_tree_max(vd->vdev_dtl[DTL_MISSING]));
}

/*
 * Determine if a resilvering vdev should remove any DTL entries from
 * its range. If the vdev was resilvering for the entire duration of the
 * scan then it should excise that range from its DTLs. Otherwise, this
 * vdev is considered partially resilvered and should leave its DTL
 * entries intact. The comment in vdev_dtl_reassess() describes how we
 * excise the DTLs.
 */
static boolean_t
vdev_dtl_should_excise(vdev_t *vd, boolean_t rebuild_done)
{
	ASSERT0(vd->vdev_children);

	if (vd->vdev_state < VDEV_STATE_DEGRADED)
		return (B_FALSE);

	if (vd->vdev_resilver_deferred)
		return (B_FALSE);

	if (range_tree_is_empty(vd->vdev_dtl[DTL_MISSING]))
		return (B_TRUE);

	if (rebuild_done) {
		vdev_rebuild_t *vr = &vd->vdev_top->vdev_rebuild_config;
		vdev_rebuild_phys_t *vrp = &vr->vr_rebuild_phys;

		/* Rebuild not initiated by attach */
		if (vd->vdev_rebuild_txg == 0)
			return (B_TRUE);

		/*
		 * When a rebuild completes without error then all missing data
		 * up to the rebuild max txg has been reconstructed and the DTL
		 * is eligible for excision.
		 */
		if (vrp->vrp_rebuild_state == VDEV_REBUILD_COMPLETE &&
		    vdev_dtl_max(vd) <= vrp->vrp_max_txg) {
			ASSERT3U(vrp->vrp_min_txg, <=, vdev_dtl_min(vd));
			ASSERT3U(vrp->vrp_min_txg, <, vd->vdev_rebuild_txg);
			ASSERT3U(vd->vdev_rebuild_txg, <=, vrp->vrp_max_txg);
			return (B_TRUE);
		}
	} else {
		dsl_scan_t *scn = vd->vdev_spa->spa_dsl_pool->dp_scan;
		dsl_scan_phys_t *scnp __maybe_unused = &scn->scn_phys;

		/* Resilver not initiated by attach */
		if (vd->vdev_resilver_txg == 0)
			return (B_TRUE);

		/*
		 * When a resilver is initiated the scan will assign the
		 * scn_max_txg value to the highest txg value that exists
		 * in all DTLs. If this device's max DTL is not part of this
		 * scan (i.e. it is not in the range (scn_min_txg, scn_max_txg]
		 * then it is not eligible for excision.
		 */
		if (vdev_dtl_max(vd) <= scn->scn_phys.scn_max_txg) {
			ASSERT3U(scnp->scn_min_txg, <=, vdev_dtl_min(vd));
			ASSERT3U(scnp->scn_min_txg, <, vd->vdev_resilver_txg);
			ASSERT3U(vd->vdev_resilver_txg, <=, scnp->scn_max_txg);
			return (B_TRUE);
		}
	}

	return (B_FALSE);
}

/*
 * Reassess DTLs after a config change or scrub completion. If txg == 0 no
 * write operations will be issued to the pool.
 */
void
vdev_dtl_reassess(vdev_t *vd, uint64_t txg, uint64_t scrub_txg,
    boolean_t scrub_done, boolean_t rebuild_done)
{
	spa_t *spa = vd->vdev_spa;
	avl_tree_t reftree;
	int minref;

	ASSERT(spa_config_held(spa, SCL_ALL, RW_READER) != 0);

	for (int c = 0; c < vd->vdev_children; c++)
		vdev_dtl_reassess(vd->vdev_child[c], txg,
		    scrub_txg, scrub_done, rebuild_done);

	if (vd == spa->spa_root_vdev || !vdev_is_concrete(vd) || vd->vdev_aux)
		return;

	if (vd->vdev_ops->vdev_op_leaf) {
		dsl_scan_t *scn = spa->spa_dsl_pool->dp_scan;
		vdev_rebuild_t *vr = &vd->vdev_top->vdev_rebuild_config;
		boolean_t check_excise = B_FALSE;
		boolean_t wasempty = B_TRUE;

		mutex_enter(&vd->vdev_dtl_lock);

		/*
		 * If requested, pretend the scan or rebuild completed cleanly.
		 */
		if (zfs_scan_ignore_errors) {
			if (scn != NULL)
				scn->scn_phys.scn_errors = 0;
			if (vr != NULL)
				vr->vr_rebuild_phys.vrp_errors = 0;
		}

		if (scrub_txg != 0 &&
		    !range_tree_is_empty(vd->vdev_dtl[DTL_MISSING])) {
			wasempty = B_FALSE;
			zfs_dbgmsg("guid:%llu txg:%llu scrub:%llu started:%d "
			    "dtl:%llu/%llu errors:%llu",
			    (u_longlong_t)vd->vdev_guid, (u_longlong_t)txg,
			    (u_longlong_t)scrub_txg, spa->spa_scrub_started,
			    (u_longlong_t)vdev_dtl_min(vd),
			    (u_longlong_t)vdev_dtl_max(vd),
			    (u_longlong_t)(scn ? scn->scn_phys.scn_errors : 0));
		}

		/*
		 * If we've completed a scrub/resilver or a rebuild cleanly
		 * then determine if this vdev should remove any DTLs. We
		 * only want to excise regions on vdevs that were available
		 * during the entire duration of this scan.
		 */
		if (rebuild_done &&
		    vr != NULL && vr->vr_rebuild_phys.vrp_errors == 0) {
			check_excise = B_TRUE;
		} else {
			if (spa->spa_scrub_started ||
			    (scn != NULL && scn->scn_phys.scn_errors == 0)) {
				check_excise = B_TRUE;
			}
		}

		if (scrub_txg && check_excise &&
		    vdev_dtl_should_excise(vd, rebuild_done)) {
			/*
			 * We completed a scrub, resilver or rebuild up to
			 * scrub_txg.  If we did it without rebooting, then
			 * the scrub dtl will be valid, so excise the old
			 * region and fold in the scrub dtl.  Otherwise,
			 * leave the dtl as-is if there was an error.
			 *
			 * There's little trick here: to excise the beginning
			 * of the DTL_MISSING map, we put it into a reference
			 * tree and then add a segment with refcnt -1 that
			 * covers the range [0, scrub_txg).  This means
			 * that each txg in that range has refcnt -1 or 0.
			 * We then add DTL_SCRUB with a refcnt of 2, so that
			 * entries in the range [0, scrub_txg) will have a
			 * positive refcnt -- either 1 or 2.  We then convert
			 * the reference tree into the new DTL_MISSING map.
			 */
			space_reftree_create(&reftree);
			space_reftree_add_map(&reftree,
			    vd->vdev_dtl[DTL_MISSING], 1);
			space_reftree_add_seg(&reftree, 0, scrub_txg, -1);
			space_reftree_add_map(&reftree,
			    vd->vdev_dtl[DTL_SCRUB], 2);
			space_reftree_generate_map(&reftree,
			    vd->vdev_dtl[DTL_MISSING], 1);
			space_reftree_destroy(&reftree);

			if (!range_tree_is_empty(vd->vdev_dtl[DTL_MISSING])) {
				zfs_dbgmsg("update DTL_MISSING:%llu/%llu",
				    (u_longlong_t)vdev_dtl_min(vd),
				    (u_longlong_t)vdev_dtl_max(vd));
			} else if (!wasempty) {
				zfs_dbgmsg("DTL_MISSING is now empty");
			}
		}
		range_tree_vacate(vd->vdev_dtl[DTL_PARTIAL], NULL, NULL);
		range_tree_walk(vd->vdev_dtl[DTL_MISSING],
		    range_tree_add, vd->vdev_dtl[DTL_PARTIAL]);
		if (scrub_done)
			range_tree_vacate(vd->vdev_dtl[DTL_SCRUB], NULL, NULL);
		range_tree_vacate(vd->vdev_dtl[DTL_OUTAGE], NULL, NULL);
		if (!vdev_readable(vd))
			range_tree_add(vd->vdev_dtl[DTL_OUTAGE], 0, -1ULL);
		else
			range_tree_walk(vd->vdev_dtl[DTL_MISSING],
			    range_tree_add, vd->vdev_dtl[DTL_OUTAGE]);

		/*
		 * If the vdev was resilvering or rebuilding and no longer
		 * has any DTLs then reset the appropriate flag and dirty
		 * the top level so that we persist the change.
		 */
		if (txg != 0 &&
		    range_tree_is_empty(vd->vdev_dtl[DTL_MISSING]) &&
		    range_tree_is_empty(vd->vdev_dtl[DTL_OUTAGE])) {
			if (vd->vdev_rebuild_txg != 0) {
				vd->vdev_rebuild_txg = 0;
				vdev_config_dirty(vd->vdev_top);
			} else if (vd->vdev_resilver_txg != 0) {
				vd->vdev_resilver_txg = 0;
				vdev_config_dirty(vd->vdev_top);
			}
		}

		mutex_exit(&vd->vdev_dtl_lock);

		if (txg != 0)
			vdev_dirty(vd->vdev_top, VDD_DTL, vd, txg);
		return;
	}

	mutex_enter(&vd->vdev_dtl_lock);
	for (int t = 0; t < DTL_TYPES; t++) {
		/* account for child's outage in parent's missing map */
		int s = (t == DTL_MISSING) ? DTL_OUTAGE: t;
		if (t == DTL_SCRUB)
			continue;			/* leaf vdevs only */
		if (t == DTL_PARTIAL)
			minref = 1;			/* i.e. non-zero */
		else if (vdev_get_nparity(vd) != 0)
			minref = vdev_get_nparity(vd) + 1; /* RAID-Z, dRAID */
		else
			minref = vd->vdev_children;	/* any kind of mirror */
		space_reftree_create(&reftree);
		for (int c = 0; c < vd->vdev_children; c++) {
			vdev_t *cvd = vd->vdev_child[c];
			mutex_enter(&cvd->vdev_dtl_lock);
			space_reftree_add_map(&reftree, cvd->vdev_dtl[s], 1);
			mutex_exit(&cvd->vdev_dtl_lock);
		}
		space_reftree_generate_map(&reftree, vd->vdev_dtl[t], minref);
		space_reftree_destroy(&reftree);
	}
	mutex_exit(&vd->vdev_dtl_lock);
}

int
vdev_dtl_load(vdev_t *vd)
{
	spa_t *spa = vd->vdev_spa;
	objset_t *mos = spa->spa_meta_objset;
	range_tree_t *rt;
	int error = 0;

	if (vd->vdev_ops->vdev_op_leaf && vd->vdev_dtl_object != 0) {
		ASSERT(vdev_is_concrete(vd));

		/*
		 * If the dtl cannot be sync'd there is no need to open it.
		 */
		if (spa->spa_mode == SPA_MODE_READ && !spa->spa_read_spacemaps)
			return (0);

		error = space_map_open(&vd->vdev_dtl_sm, mos,
		    vd->vdev_dtl_object, 0, -1ULL, 0);
		if (error)
			return (error);
		ASSERT(vd->vdev_dtl_sm != NULL);

		rt = range_tree_create(NULL, RANGE_SEG64, NULL, 0, 0);
		error = space_map_load(vd->vdev_dtl_sm, rt, SM_ALLOC);
		if (error == 0) {
			mutex_enter(&vd->vdev_dtl_lock);
			range_tree_walk(rt, range_tree_add,
			    vd->vdev_dtl[DTL_MISSING]);
			mutex_exit(&vd->vdev_dtl_lock);
		}

		range_tree_vacate(rt, NULL, NULL);
		range_tree_destroy(rt);

		return (error);
	}

	for (int c = 0; c < vd->vdev_children; c++) {
		error = vdev_dtl_load(vd->vdev_child[c]);
		if (error != 0)
			break;
	}

	return (error);
}

static void
vdev_zap_allocation_data(vdev_t *vd, dmu_tx_t *tx)
{
	spa_t *spa = vd->vdev_spa;
	objset_t *mos = spa->spa_meta_objset;
	vdev_alloc_bias_t alloc_bias = vd->vdev_alloc_bias;
	const char *string;

	ASSERT(alloc_bias != VDEV_BIAS_NONE);

	string =
	    (alloc_bias == VDEV_BIAS_LOG) ? VDEV_ALLOC_BIAS_LOG :
	    (alloc_bias == VDEV_BIAS_SPECIAL) ? VDEV_ALLOC_BIAS_SPECIAL :
	    (alloc_bias == VDEV_BIAS_DEDUP) ? VDEV_ALLOC_BIAS_DEDUP : NULL;

	ASSERT(string != NULL);
	VERIFY0(zap_add(mos, vd->vdev_top_zap, VDEV_TOP_ZAP_ALLOCATION_BIAS,
	    1, strlen(string) + 1, string, tx));

	if (alloc_bias == VDEV_BIAS_SPECIAL || alloc_bias == VDEV_BIAS_DEDUP) {
		spa_activate_allocation_classes(spa, tx);
	}
}

void
vdev_destroy_unlink_zap(vdev_t *vd, uint64_t zapobj, dmu_tx_t *tx)
{
	spa_t *spa = vd->vdev_spa;

	VERIFY0(zap_destroy(spa->spa_meta_objset, zapobj, tx));
	VERIFY0(zap_remove_int(spa->spa_meta_objset, spa->spa_all_vdev_zaps,
	    zapobj, tx));
}

uint64_t
vdev_create_link_zap(vdev_t *vd, dmu_tx_t *tx)
{
	spa_t *spa = vd->vdev_spa;
	uint64_t zap = zap_create(spa->spa_meta_objset, DMU_OTN_ZAP_METADATA,
	    DMU_OT_NONE, 0, tx);

	ASSERT(zap != 0);
	VERIFY0(zap_add_int(spa->spa_meta_objset, spa->spa_all_vdev_zaps,
	    zap, tx));

	return (zap);
}

void
vdev_construct_zaps(vdev_t *vd, dmu_tx_t *tx)
{
	if (vd->vdev_ops != &vdev_hole_ops &&
	    vd->vdev_ops != &vdev_missing_ops &&
	    vd->vdev_ops != &vdev_root_ops &&
	    !vd->vdev_top->vdev_removing) {
		if (vd->vdev_ops->vdev_op_leaf && vd->vdev_leaf_zap == 0) {
			vd->vdev_leaf_zap = vdev_create_link_zap(vd, tx);
		}
		if (vd == vd->vdev_top && vd->vdev_top_zap == 0) {
			vd->vdev_top_zap = vdev_create_link_zap(vd, tx);
			if (vd->vdev_alloc_bias != VDEV_BIAS_NONE)
				vdev_zap_allocation_data(vd, tx);
		}
	}

	for (uint64_t i = 0; i < vd->vdev_children; i++) {
		vdev_construct_zaps(vd->vdev_child[i], tx);
	}
}

static void
vdev_dtl_sync(vdev_t *vd, uint64_t txg)
{
	spa_t *spa = vd->vdev_spa;
	range_tree_t *rt = vd->vdev_dtl[DTL_MISSING];
	objset_t *mos = spa->spa_meta_objset;
	range_tree_t *rtsync;
	dmu_tx_t *tx;
	uint64_t object = space_map_object(vd->vdev_dtl_sm);

	ASSERT(vdev_is_concrete(vd));
	ASSERT(vd->vdev_ops->vdev_op_leaf);

	tx = dmu_tx_create_assigned(spa->spa_dsl_pool, txg);

	if (vd->vdev_detached || vd->vdev_top->vdev_removing) {
		mutex_enter(&vd->vdev_dtl_lock);
		space_map_free(vd->vdev_dtl_sm, tx);
		space_map_close(vd->vdev_dtl_sm);
		vd->vdev_dtl_sm = NULL;
		mutex_exit(&vd->vdev_dtl_lock);

		/*
		 * We only destroy the leaf ZAP for detached leaves or for
		 * removed log devices. Removed data devices handle leaf ZAP
		 * cleanup later, once cancellation is no longer possible.
		 */
		if (vd->vdev_leaf_zap != 0 && (vd->vdev_detached ||
		    vd->vdev_top->vdev_islog)) {
			vdev_destroy_unlink_zap(vd, vd->vdev_leaf_zap, tx);
			vd->vdev_leaf_zap = 0;
		}

		dmu_tx_commit(tx);
		return;
	}

	if (vd->vdev_dtl_sm == NULL) {
		uint64_t new_object;

		new_object = space_map_alloc(mos, zfs_vdev_dtl_sm_blksz, tx);
		VERIFY3U(new_object, !=, 0);

		VERIFY0(space_map_open(&vd->vdev_dtl_sm, mos, new_object,
		    0, -1ULL, 0));
		ASSERT(vd->vdev_dtl_sm != NULL);
	}

	rtsync = range_tree_create(NULL, RANGE_SEG64, NULL, 0, 0);

	mutex_enter(&vd->vdev_dtl_lock);
	range_tree_walk(rt, range_tree_add, rtsync);
	mutex_exit(&vd->vdev_dtl_lock);

	space_map_truncate(vd->vdev_dtl_sm, zfs_vdev_dtl_sm_blksz, tx);
	space_map_write(vd->vdev_dtl_sm, rtsync, SM_ALLOC, SM_NO_VDEVID, tx);
	range_tree_vacate(rtsync, NULL, NULL);

	range_tree_destroy(rtsync);

	/*
	 * If the object for the space map has changed then dirty
	 * the top level so that we update the config.
	 */
	if (object != space_map_object(vd->vdev_dtl_sm)) {
		vdev_dbgmsg(vd, "txg %llu, spa %s, DTL old object %llu, "
		    "new object %llu", (u_longlong_t)txg, spa_name(spa),
		    (u_longlong_t)object,
		    (u_longlong_t)space_map_object(vd->vdev_dtl_sm));
		vdev_config_dirty(vd->vdev_top);
	}

	dmu_tx_commit(tx);
}

/*
 * Determine whether the specified vdev can be offlined/detached/removed
 * without losing data.
 */
boolean_t
vdev_dtl_required(vdev_t *vd)
{
	spa_t *spa = vd->vdev_spa;
	vdev_t *tvd = vd->vdev_top;
	uint8_t cant_read = vd->vdev_cant_read;
	boolean_t required;

	ASSERT(spa_config_held(spa, SCL_STATE_ALL, RW_WRITER) == SCL_STATE_ALL);

	if (vd == spa->spa_root_vdev || vd == tvd)
		return (B_TRUE);

	/*
	 * Temporarily mark the device as unreadable, and then determine
	 * whether this results in any DTL outages in the top-level vdev.
	 * If not, we can safely offline/detach/remove the device.
	 */
	vd->vdev_cant_read = B_TRUE;
	vdev_dtl_reassess(tvd, 0, 0, B_FALSE, B_FALSE);
	required = !vdev_dtl_empty(tvd, DTL_OUTAGE);
	vd->vdev_cant_read = cant_read;
	vdev_dtl_reassess(tvd, 0, 0, B_FALSE, B_FALSE);

	if (!required && zio_injection_enabled) {
		required = !!zio_handle_device_injection(vd, NULL,
		    SET_ERROR(ECHILD));
	}

	return (required);
}

/*
 * Determine if resilver is needed, and if so the txg range.
 */
boolean_t
vdev_resilver_needed(vdev_t *vd, uint64_t *minp, uint64_t *maxp)
{
	boolean_t needed = B_FALSE;
	uint64_t thismin = UINT64_MAX;
	uint64_t thismax = 0;

	if (vd->vdev_children == 0) {
		mutex_enter(&vd->vdev_dtl_lock);
		if (!range_tree_is_empty(vd->vdev_dtl[DTL_MISSING]) &&
		    vdev_writeable(vd)) {

			thismin = vdev_dtl_min(vd);
			thismax = vdev_dtl_max(vd);
			needed = B_TRUE;
		}
		mutex_exit(&vd->vdev_dtl_lock);
	} else {
		for (int c = 0; c < vd->vdev_children; c++) {
			vdev_t *cvd = vd->vdev_child[c];
			uint64_t cmin, cmax;

			if (vdev_resilver_needed(cvd, &cmin, &cmax)) {
				thismin = MIN(thismin, cmin);
				thismax = MAX(thismax, cmax);
				needed = B_TRUE;
			}
		}
	}

	if (needed && minp) {
		*minp = thismin;
		*maxp = thismax;
	}
	return (needed);
}

/*
 * Gets the checkpoint space map object from the vdev's ZAP.  On success sm_obj
 * will contain either the checkpoint spacemap object or zero if none exists.
 * All other errors are returned to the caller.
 */
int
vdev_checkpoint_sm_object(vdev_t *vd, uint64_t *sm_obj)
{
	ASSERT0(spa_config_held(vd->vdev_spa, SCL_ALL, RW_WRITER));

	if (vd->vdev_top_zap == 0) {
		*sm_obj = 0;
		return (0);
	}

	int error = zap_lookup(spa_meta_objset(vd->vdev_spa), vd->vdev_top_zap,
	    VDEV_TOP_ZAP_POOL_CHECKPOINT_SM, sizeof (uint64_t), 1, sm_obj);
	if (error == ENOENT) {
		*sm_obj = 0;
		error = 0;
	}

	return (error);
}

int
vdev_load(vdev_t *vd)
{
	int children = vd->vdev_children;
	int error = 0;
	taskq_t *tq = NULL;

	/*
	 * It's only worthwhile to use the taskq for the root vdev, because the
	 * slow part is metaslab_init, and that only happens for top-level
	 * vdevs.
	 */
	if (vd->vdev_ops == &vdev_root_ops && vd->vdev_children > 0) {
		tq = taskq_create("vdev_load", children, minclsyspri,
		    children, children, TASKQ_PREPOPULATE);
	}

	/*
	 * Recursively load all children.
	 */
	for (int c = 0; c < vd->vdev_children; c++) {
		vdev_t *cvd = vd->vdev_child[c];

		if (tq == NULL || vdev_uses_zvols(cvd)) {
			cvd->vdev_load_error = vdev_load(cvd);
		} else {
			VERIFY(taskq_dispatch(tq, vdev_load_child,
			    cvd, TQ_SLEEP) != TASKQID_INVALID);
		}
	}

	if (tq != NULL) {
		taskq_wait(tq);
		taskq_destroy(tq);
	}

	for (int c = 0; c < vd->vdev_children; c++) {
		int error = vd->vdev_child[c]->vdev_load_error;

		if (error != 0)
			return (error);
	}

	vdev_set_deflate_ratio(vd);

	/*
	 * On spa_load path, grab the allocation bias from our zap
	 */
	if (vd == vd->vdev_top && vd->vdev_top_zap != 0) {
		spa_t *spa = vd->vdev_spa;
		char bias_str[64];

		error = zap_lookup(spa->spa_meta_objset, vd->vdev_top_zap,
		    VDEV_TOP_ZAP_ALLOCATION_BIAS, 1, sizeof (bias_str),
		    bias_str);
		if (error == 0) {
			ASSERT(vd->vdev_alloc_bias == VDEV_BIAS_NONE);
			vd->vdev_alloc_bias = vdev_derive_alloc_bias(bias_str);
		} else if (error != ENOENT) {
			vdev_set_state(vd, B_FALSE, VDEV_STATE_CANT_OPEN,
			    VDEV_AUX_CORRUPT_DATA);
			vdev_dbgmsg(vd, "vdev_load: zap_lookup(top_zap=%llu) "
			    "failed [error=%d]",
			    (u_longlong_t)vd->vdev_top_zap, error);
			return (error);
		}
	}

	/*
	 * Load any rebuild state from the top-level vdev zap.
	 */
	if (vd == vd->vdev_top && vd->vdev_top_zap != 0) {
		error = vdev_rebuild_load(vd);
		if (error && error != ENOTSUP) {
			vdev_set_state(vd, B_FALSE, VDEV_STATE_CANT_OPEN,
			    VDEV_AUX_CORRUPT_DATA);
			vdev_dbgmsg(vd, "vdev_load: vdev_rebuild_load "
			    "failed [error=%d]", error);
			return (error);
		}
	}

	/*
	 * If this is a top-level vdev, initialize its metaslabs.
	 */
	if (vd == vd->vdev_top && vdev_is_concrete(vd)) {
		vdev_metaslab_group_create(vd);

		if (vd->vdev_ashift == 0 || vd->vdev_asize == 0) {
			vdev_set_state(vd, B_FALSE, VDEV_STATE_CANT_OPEN,
			    VDEV_AUX_CORRUPT_DATA);
			vdev_dbgmsg(vd, "vdev_load: invalid size. ashift=%llu, "
			    "asize=%llu", (u_longlong_t)vd->vdev_ashift,
			    (u_longlong_t)vd->vdev_asize);
			return (SET_ERROR(ENXIO));
		}

		error = vdev_metaslab_init(vd, 0);
		if (error != 0) {
			vdev_dbgmsg(vd, "vdev_load: metaslab_init failed "
			    "[error=%d]", error);
			vdev_set_state(vd, B_FALSE, VDEV_STATE_CANT_OPEN,
			    VDEV_AUX_CORRUPT_DATA);
			return (error);
		}

		uint64_t checkpoint_sm_obj;
		error = vdev_checkpoint_sm_object(vd, &checkpoint_sm_obj);
		if (error == 0 && checkpoint_sm_obj != 0) {
			objset_t *mos = spa_meta_objset(vd->vdev_spa);
			ASSERT(vd->vdev_asize != 0);
			ASSERT3P(vd->vdev_checkpoint_sm, ==, NULL);

			error = space_map_open(&vd->vdev_checkpoint_sm,
			    mos, checkpoint_sm_obj, 0, vd->vdev_asize,
			    vd->vdev_ashift);
			if (error != 0) {
				vdev_dbgmsg(vd, "vdev_load: space_map_open "
				    "failed for checkpoint spacemap (obj %llu) "
				    "[error=%d]",
				    (u_longlong_t)checkpoint_sm_obj, error);
				return (error);
			}
			ASSERT3P(vd->vdev_checkpoint_sm, !=, NULL);

			/*
			 * Since the checkpoint_sm contains free entries
			 * exclusively we can use space_map_allocated() to
			 * indicate the cumulative checkpointed space that
			 * has been freed.
			 */
			vd->vdev_stat.vs_checkpoint_space =
			    -space_map_allocated(vd->vdev_checkpoint_sm);
			vd->vdev_spa->spa_checkpoint_info.sci_dspace +=
			    vd->vdev_stat.vs_checkpoint_space;
		} else if (error != 0) {
			vdev_dbgmsg(vd, "vdev_load: failed to retrieve "
			    "checkpoint space map object from vdev ZAP "
			    "[error=%d]", error);
			return (error);
		}
	}

	/*
	 * If this is a leaf vdev, load its DTL.
	 */
	if (vd->vdev_ops->vdev_op_leaf && (error = vdev_dtl_load(vd)) != 0) {
		vdev_set_state(vd, B_FALSE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_CORRUPT_DATA);
		vdev_dbgmsg(vd, "vdev_load: vdev_dtl_load failed "
		    "[error=%d]", error);
		return (error);
	}

	uint64_t obsolete_sm_object;
	error = vdev_obsolete_sm_object(vd, &obsolete_sm_object);
	if (error == 0 && obsolete_sm_object != 0) {
		objset_t *mos = vd->vdev_spa->spa_meta_objset;
		ASSERT(vd->vdev_asize != 0);
		ASSERT3P(vd->vdev_obsolete_sm, ==, NULL);

		if ((error = space_map_open(&vd->vdev_obsolete_sm, mos,
		    obsolete_sm_object, 0, vd->vdev_asize, 0))) {
			vdev_set_state(vd, B_FALSE, VDEV_STATE_CANT_OPEN,
			    VDEV_AUX_CORRUPT_DATA);
			vdev_dbgmsg(vd, "vdev_load: space_map_open failed for "
			    "obsolete spacemap (obj %llu) [error=%d]",
			    (u_longlong_t)obsolete_sm_object, error);
			return (error);
		}
	} else if (error != 0) {
		vdev_dbgmsg(vd, "vdev_load: failed to retrieve obsolete "
		    "space map object from vdev ZAP [error=%d]", error);
		return (error);
	}

	return (0);
}

/*
 * The special vdev case is used for hot spares and l2cache devices.  Its
 * sole purpose it to set the vdev state for the associated vdev.  To do this,
 * we make sure that we can open the underlying device, then try to read the
 * label, and make sure that the label is sane and that it hasn't been
 * repurposed to another pool.
 */
int
vdev_validate_aux(vdev_t *vd)
{
	nvlist_t *label;
	uint64_t guid, version;
	uint64_t state;

	if (!vdev_readable(vd))
		return (0);

	if ((label = vdev_label_read_config(vd, -1ULL)) == NULL) {
		vdev_set_state(vd, B_TRUE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_CORRUPT_DATA);
		return (-1);
	}

	if (nvlist_lookup_uint64(label, ZPOOL_CONFIG_VERSION, &version) != 0 ||
	    !SPA_VERSION_IS_SUPPORTED(version) ||
	    nvlist_lookup_uint64(label, ZPOOL_CONFIG_GUID, &guid) != 0 ||
	    guid != vd->vdev_guid ||
	    nvlist_lookup_uint64(label, ZPOOL_CONFIG_POOL_STATE, &state) != 0) {
		vdev_set_state(vd, B_TRUE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_CORRUPT_DATA);
		nvlist_free(label);
		return (-1);
	}

	/*
	 * We don't actually check the pool state here.  If it's in fact in
	 * use by another pool, we update this fact on the fly when requested.
	 */
	nvlist_free(label);
	return (0);
}

static void
vdev_destroy_ms_flush_data(vdev_t *vd, dmu_tx_t *tx)
{
	objset_t *mos = spa_meta_objset(vd->vdev_spa);

	if (vd->vdev_top_zap == 0)
		return;

	uint64_t object = 0;
	int err = zap_lookup(mos, vd->vdev_top_zap,
	    VDEV_TOP_ZAP_MS_UNFLUSHED_PHYS_TXGS, sizeof (uint64_t), 1, &object);
	if (err == ENOENT)
		return;
	VERIFY0(err);

	VERIFY0(dmu_object_free(mos, object, tx));
	VERIFY0(zap_remove(mos, vd->vdev_top_zap,
	    VDEV_TOP_ZAP_MS_UNFLUSHED_PHYS_TXGS, tx));
}

/*
 * Free the objects used to store this vdev's spacemaps, and the array
 * that points to them.
 */
void
vdev_destroy_spacemaps(vdev_t *vd, dmu_tx_t *tx)
{
	if (vd->vdev_ms_array == 0)
		return;

	objset_t *mos = vd->vdev_spa->spa_meta_objset;
	uint64_t array_count = vd->vdev_asize >> vd->vdev_ms_shift;
	size_t array_bytes = array_count * sizeof (uint64_t);
	uint64_t *smobj_array = kmem_alloc(array_bytes, KM_SLEEP);
	VERIFY0(dmu_read(mos, vd->vdev_ms_array, 0,
	    array_bytes, smobj_array, 0));

	for (uint64_t i = 0; i < array_count; i++) {
		uint64_t smobj = smobj_array[i];
		if (smobj == 0)
			continue;

		space_map_free_obj(mos, smobj, tx);
	}

	kmem_free(smobj_array, array_bytes);
	VERIFY0(dmu_object_free(mos, vd->vdev_ms_array, tx));
	vdev_destroy_ms_flush_data(vd, tx);
	vd->vdev_ms_array = 0;
}

static void
vdev_remove_empty_log(vdev_t *vd, uint64_t txg)
{
	spa_t *spa = vd->vdev_spa;

	ASSERT(vd->vdev_islog);
	ASSERT(vd == vd->vdev_top);
	ASSERT3U(txg, ==, spa_syncing_txg(spa));

	dmu_tx_t *tx = dmu_tx_create_assigned(spa_get_dsl(spa), txg);

	vdev_destroy_spacemaps(vd, tx);
	if (vd->vdev_top_zap != 0) {
		vdev_destroy_unlink_zap(vd, vd->vdev_top_zap, tx);
		vd->vdev_top_zap = 0;
	}

	dmu_tx_commit(tx);
}

void
vdev_sync_done(vdev_t *vd, uint64_t txg)
{
	metaslab_t *msp;
	boolean_t reassess = !txg_list_empty(&vd->vdev_ms_list, TXG_CLEAN(txg));

	ASSERT(vdev_is_concrete(vd));

	while ((msp = txg_list_remove(&vd->vdev_ms_list, TXG_CLEAN(txg)))
	    != NULL)
		metaslab_sync_done(msp, txg);

	if (reassess) {
		metaslab_sync_reassess(vd->vdev_mg);
		if (vd->vdev_log_mg != NULL)
			metaslab_sync_reassess(vd->vdev_log_mg);
	}
}

void
vdev_sync(vdev_t *vd, uint64_t txg)
{
	spa_t *spa = vd->vdev_spa;
	vdev_t *lvd;
	metaslab_t *msp;

	ASSERT3U(txg, ==, spa->spa_syncing_txg);
	dmu_tx_t *tx = dmu_tx_create_assigned(spa->spa_dsl_pool, txg);
	if (range_tree_space(vd->vdev_obsolete_segments) > 0) {
		ASSERT(vd->vdev_removing ||
		    vd->vdev_ops == &vdev_indirect_ops);

		vdev_indirect_sync_obsolete(vd, tx);

		/*
		 * If the vdev is indirect, it can't have dirty
		 * metaslabs or DTLs.
		 */
		if (vd->vdev_ops == &vdev_indirect_ops) {
			ASSERT(txg_list_empty(&vd->vdev_ms_list, txg));
			ASSERT(txg_list_empty(&vd->vdev_dtl_list, txg));
			dmu_tx_commit(tx);
			return;
		}
	}

	ASSERT(vdev_is_concrete(vd));

	if (vd->vdev_ms_array == 0 && vd->vdev_ms_shift != 0 &&
	    !vd->vdev_removing) {
		ASSERT(vd == vd->vdev_top);
		ASSERT0(vd->vdev_indirect_config.vic_mapping_object);
		vd->vdev_ms_array = dmu_object_alloc(spa->spa_meta_objset,
		    DMU_OT_OBJECT_ARRAY, 0, DMU_OT_NONE, 0, tx);
		ASSERT(vd->vdev_ms_array != 0);
		vdev_config_dirty(vd);
	}

	while ((msp = txg_list_remove(&vd->vdev_ms_list, txg)) != NULL) {
		metaslab_sync(msp, txg);
		(void) txg_list_add(&vd->vdev_ms_list, msp, TXG_CLEAN(txg));
	}

	while ((lvd = txg_list_remove(&vd->vdev_dtl_list, txg)) != NULL)
		vdev_dtl_sync(lvd, txg);

	/*
	 * If this is an empty log device being removed, destroy the
	 * metadata associated with it.
	 */
	if (vd->vdev_islog && vd->vdev_stat.vs_alloc == 0 && vd->vdev_removing)
		vdev_remove_empty_log(vd, txg);

	(void) txg_list_add(&spa->spa_vdev_txg_list, vd, TXG_CLEAN(txg));
	dmu_tx_commit(tx);
}

uint64_t
vdev_psize_to_asize(vdev_t *vd, uint64_t psize)
{
	return (vd->vdev_ops->vdev_op_asize(vd, psize));
}

/*
 * Mark the given vdev faulted.  A faulted vdev behaves as if the device could
 * not be opened, and no I/O is attempted.
 */
int
vdev_fault(spa_t *spa, uint64_t guid, vdev_aux_t aux)
{
	vdev_t *vd, *tvd;

	spa_vdev_state_enter(spa, SCL_NONE);

	if ((vd = spa_lookup_by_guid(spa, guid, B_TRUE)) == NULL)
		return (spa_vdev_state_exit(spa, NULL, SET_ERROR(ENODEV)));

	if (!vd->vdev_ops->vdev_op_leaf)
		return (spa_vdev_state_exit(spa, NULL, SET_ERROR(ENOTSUP)));

	tvd = vd->vdev_top;

	/*
	 * If user did a 'zpool offline -f' then make the fault persist across
	 * reboots.
	 */
	if (aux == VDEV_AUX_EXTERNAL_PERSIST) {
		/*
		 * There are two kinds of forced faults: temporary and
		 * persistent.  Temporary faults go away at pool import, while
		 * persistent faults stay set.  Both types of faults can be
		 * cleared with a zpool clear.
		 *
		 * We tell if a vdev is persistently faulted by looking at the
		 * ZPOOL_CONFIG_AUX_STATE nvpair.  If it's set to "external" at
		 * import then it's a persistent fault.  Otherwise, it's
		 * temporary.  We get ZPOOL_CONFIG_AUX_STATE set to "external"
		 * by setting vd.vdev_stat.vs_aux to VDEV_AUX_EXTERNAL.  This
		 * tells vdev_config_generate() (which gets run later) to set
		 * ZPOOL_CONFIG_AUX_STATE to "external" in the nvlist.
		 */
		vd->vdev_stat.vs_aux = VDEV_AUX_EXTERNAL;
		vd->vdev_tmpoffline = B_FALSE;
		aux = VDEV_AUX_EXTERNAL;
	} else {
		vd->vdev_tmpoffline = B_TRUE;
	}

	/*
	 * We don't directly use the aux state here, but if we do a
	 * vdev_reopen(), we need this value to be present to remember why we
	 * were faulted.
	 */
	vd->vdev_label_aux = aux;

	/*
	 * Faulted state takes precedence over degraded.
	 */
	vd->vdev_delayed_close = B_FALSE;
	vd->vdev_faulted = 1ULL;
	vd->vdev_degraded = 0ULL;
	vdev_set_state(vd, B_FALSE, VDEV_STATE_FAULTED, aux);

	/*
	 * If this device has the only valid copy of the data, then
	 * back off and simply mark the vdev as degraded instead.
	 */
	if (!tvd->vdev_islog && vd->vdev_aux == NULL && vdev_dtl_required(vd)) {
		vd->vdev_degraded = 1ULL;
		vd->vdev_faulted = 0ULL;

		/*
		 * If we reopen the device and it's not dead, only then do we
		 * mark it degraded.
		 */
		vdev_reopen(tvd);

		if (vdev_readable(vd))
			vdev_set_state(vd, B_FALSE, VDEV_STATE_DEGRADED, aux);
	}

	return (spa_vdev_state_exit(spa, vd, 0));
}

/*
 * Mark the given vdev degraded.  A degraded vdev is purely an indication to the
 * user that something is wrong.  The vdev continues to operate as normal as far
 * as I/O is concerned.
 */
int
vdev_degrade(spa_t *spa, uint64_t guid, vdev_aux_t aux)
{
	vdev_t *vd;

	spa_vdev_state_enter(spa, SCL_NONE);

	if ((vd = spa_lookup_by_guid(spa, guid, B_TRUE)) == NULL)
		return (spa_vdev_state_exit(spa, NULL, SET_ERROR(ENODEV)));

	if (!vd->vdev_ops->vdev_op_leaf)
		return (spa_vdev_state_exit(spa, NULL, SET_ERROR(ENOTSUP)));

	/*
	 * If the vdev is already faulted, then don't do anything.
	 */
	if (vd->vdev_faulted || vd->vdev_degraded)
		return (spa_vdev_state_exit(spa, NULL, 0));

	vd->vdev_degraded = 1ULL;
	if (!vdev_is_dead(vd))
		vdev_set_state(vd, B_FALSE, VDEV_STATE_DEGRADED,
		    aux);

	return (spa_vdev_state_exit(spa, vd, 0));
}

/*
 * Online the given vdev.
 *
 * If 'ZFS_ONLINE_UNSPARE' is set, it implies two things.  First, any attached
 * spare device should be detached when the device finishes resilvering.
 * Second, the online should be treated like a 'test' online case, so no FMA
 * events are generated if the device fails to open.
 */
int
vdev_online(spa_t *spa, uint64_t guid, uint64_t flags, vdev_state_t *newstate)
{
	vdev_t *vd, *tvd, *pvd, *rvd = spa->spa_root_vdev;
	boolean_t wasoffline;
	vdev_state_t oldstate;

	spa_vdev_state_enter(spa, SCL_NONE);

	if ((vd = spa_lookup_by_guid(spa, guid, B_TRUE)) == NULL)
		return (spa_vdev_state_exit(spa, NULL, SET_ERROR(ENODEV)));

	if (!vd->vdev_ops->vdev_op_leaf)
		return (spa_vdev_state_exit(spa, NULL, SET_ERROR(ENOTSUP)));

	wasoffline = (vd->vdev_offline || vd->vdev_tmpoffline);
	oldstate = vd->vdev_state;

	tvd = vd->vdev_top;
	vd->vdev_offline = B_FALSE;
	vd->vdev_tmpoffline = B_FALSE;
	vd->vdev_checkremove = !!(flags & ZFS_ONLINE_CHECKREMOVE);
	vd->vdev_forcefault = !!(flags & ZFS_ONLINE_FORCEFAULT);

	/* XXX - L2ARC 1.0 does not support expansion */
	if (!vd->vdev_aux) {
		for (pvd = vd; pvd != rvd; pvd = pvd->vdev_parent)
			pvd->vdev_expanding = !!((flags & ZFS_ONLINE_EXPAND) ||
			    spa->spa_autoexpand);
		vd->vdev_expansion_time = gethrestime_sec();
	}

	vdev_reopen(tvd);
	vd->vdev_checkremove = vd->vdev_forcefault = B_FALSE;

	if (!vd->vdev_aux) {
		for (pvd = vd; pvd != rvd; pvd = pvd->vdev_parent)
			pvd->vdev_expanding = B_FALSE;
	}

	if (newstate)
		*newstate = vd->vdev_state;
	if ((flags & ZFS_ONLINE_UNSPARE) &&
	    !vdev_is_dead(vd) && vd->vdev_parent &&
	    vd->vdev_parent->vdev_ops == &vdev_spare_ops &&
	    vd->vdev_parent->vdev_child[0] == vd)
		vd->vdev_unspare = B_TRUE;

	if ((flags & ZFS_ONLINE_EXPAND) || spa->spa_autoexpand) {

		/* XXX - L2ARC 1.0 does not support expansion */
		if (vd->vdev_aux)
			return (spa_vdev_state_exit(spa, vd, ENOTSUP));
		spa_async_request(spa, SPA_ASYNC_CONFIG_UPDATE);
	}

	/* Restart initializing if necessary */
	mutex_enter(&vd->vdev_initialize_lock);
	if (vdev_writeable(vd) &&
	    vd->vdev_initialize_thread == NULL &&
	    vd->vdev_initialize_state == VDEV_INITIALIZE_ACTIVE) {
		(void) vdev_initialize(vd);
	}
	mutex_exit(&vd->vdev_initialize_lock);

	/*
	 * Restart trimming if necessary. We do not restart trimming for cache
	 * devices here. This is triggered by l2arc_rebuild_vdev()
	 * asynchronously for the whole device or in l2arc_evict() as it evicts
	 * space for upcoming writes.
	 */
	mutex_enter(&vd->vdev_trim_lock);
	if (vdev_writeable(vd) && !vd->vdev_isl2cache &&
	    vd->vdev_trim_thread == NULL &&
	    vd->vdev_trim_state == VDEV_TRIM_ACTIVE) {
		(void) vdev_trim(vd, vd->vdev_trim_rate, vd->vdev_trim_partial,
		    vd->vdev_trim_secure);
	}
	mutex_exit(&vd->vdev_trim_lock);

	if (wasoffline ||
	    (oldstate < VDEV_STATE_DEGRADED &&
	    vd->vdev_state >= VDEV_STATE_DEGRADED))
		spa_event_notify(spa, vd, NULL, ESC_ZFS_VDEV_ONLINE);

	return (spa_vdev_state_exit(spa, vd, 0));
}

static int
vdev_offline_locked(spa_t *spa, uint64_t guid, uint64_t flags)
{
	vdev_t *vd, *tvd;
	int error = 0;
	uint64_t generation;
	metaslab_group_t *mg;

top:
	spa_vdev_state_enter(spa, SCL_ALLOC);

	if ((vd = spa_lookup_by_guid(spa, guid, B_TRUE)) == NULL)
		return (spa_vdev_state_exit(spa, NULL, SET_ERROR(ENODEV)));

	if (!vd->vdev_ops->vdev_op_leaf)
		return (spa_vdev_state_exit(spa, NULL, SET_ERROR(ENOTSUP)));

	if (vd->vdev_ops == &vdev_draid_spare_ops)
		return (spa_vdev_state_exit(spa, NULL, ENOTSUP));

	tvd = vd->vdev_top;
	mg = tvd->vdev_mg;
	generation = spa->spa_config_generation + 1;

	/*
	 * If the device isn't already offline, try to offline it.
	 */
	if (!vd->vdev_offline) {
		/*
		 * If this device has the only valid copy of some data,
		 * don't allow it to be offlined. Log devices are always
		 * expendable.
		 */
		if (!tvd->vdev_islog && vd->vdev_aux == NULL &&
		    vdev_dtl_required(vd))
			return (spa_vdev_state_exit(spa, NULL,
			    SET_ERROR(EBUSY)));

		/*
		 * If the top-level is a slog and it has had allocations
		 * then proceed.  We check that the vdev's metaslab group
		 * is not NULL since it's possible that we may have just
		 * added this vdev but not yet initialized its metaslabs.
		 */
		if (tvd->vdev_islog && mg != NULL) {
			/*
			 * Prevent any future allocations.
			 */
			ASSERT3P(tvd->vdev_log_mg, ==, NULL);
			metaslab_group_passivate(mg);
			(void) spa_vdev_state_exit(spa, vd, 0);

			error = spa_reset_logs(spa);

			/*
			 * If the log device was successfully reset but has
			 * checkpointed data, do not offline it.
			 */
			if (error == 0 &&
			    tvd->vdev_checkpoint_sm != NULL) {
				ASSERT3U(space_map_allocated(
				    tvd->vdev_checkpoint_sm), !=, 0);
				error = ZFS_ERR_CHECKPOINT_EXISTS;
			}

			spa_vdev_state_enter(spa, SCL_ALLOC);

			/*
			 * Check to see if the config has changed.
			 */
			if (error || generation != spa->spa_config_generation) {
				metaslab_group_activate(mg);
				if (error)
					return (spa_vdev_state_exit(spa,
					    vd, error));
				(void) spa_vdev_state_exit(spa, vd, 0);
				goto top;
			}
			ASSERT0(tvd->vdev_stat.vs_alloc);
		}

		/*
		 * Offline this device and reopen its top-level vdev.
		 * If the top-level vdev is a log device then just offline
		 * it. Otherwise, if this action results in the top-level
		 * vdev becoming unusable, undo it and fail the request.
		 */
		vd->vdev_offline = B_TRUE;
		vdev_reopen(tvd);

		if (!tvd->vdev_islog && vd->vdev_aux == NULL &&
		    vdev_is_dead(tvd)) {
			vd->vdev_offline = B_FALSE;
			vdev_reopen(tvd);
			return (spa_vdev_state_exit(spa, NULL,
			    SET_ERROR(EBUSY)));
		}

		/*
		 * Add the device back into the metaslab rotor so that
		 * once we online the device it's open for business.
		 */
		if (tvd->vdev_islog && mg != NULL)
			metaslab_group_activate(mg);
	}

	vd->vdev_tmpoffline = !!(flags & ZFS_OFFLINE_TEMPORARY);

	return (spa_vdev_state_exit(spa, vd, 0));
}

int
vdev_offline(spa_t *spa, uint64_t guid, uint64_t flags)
{
	int error;

	mutex_enter(&spa->spa_vdev_top_lock);
	error = vdev_offline_locked(spa, guid, flags);
	mutex_exit(&spa->spa_vdev_top_lock);

	return (error);
}

/*
 * Clear the error counts associated with this vdev.  Unlike vdev_online() and
 * vdev_offline(), we assume the spa config is locked.  We also clear all
 * children.  If 'vd' is NULL, then the user wants to clear all vdevs.
 */
void
vdev_clear(spa_t *spa, vdev_t *vd)
{
	vdev_t *rvd = spa->spa_root_vdev;

	ASSERT(spa_config_held(spa, SCL_STATE_ALL, RW_WRITER) == SCL_STATE_ALL);

	if (vd == NULL)
		vd = rvd;

	vd->vdev_stat.vs_read_errors = 0;
	vd->vdev_stat.vs_write_errors = 0;
	vd->vdev_stat.vs_checksum_errors = 0;
	vd->vdev_stat.vs_slow_ios = 0;

	for (int c = 0; c < vd->vdev_children; c++)
		vdev_clear(spa, vd->vdev_child[c]);

	/*
	 * It makes no sense to "clear" an indirect vdev.
	 */
	if (!vdev_is_concrete(vd))
		return;

	/*
	 * If we're in the FAULTED state or have experienced failed I/O, then
	 * clear the persistent state and attempt to reopen the device.  We
	 * also mark the vdev config dirty, so that the new faulted state is
	 * written out to disk.
	 */
	if (vd->vdev_faulted || vd->vdev_degraded ||
	    !vdev_readable(vd) || !vdev_writeable(vd)) {
		/*
		 * When reopening in response to a clear event, it may be due to
		 * a fmadm repair request.  In this case, if the device is
		 * still broken, we want to still post the ereport again.
		 */
		vd->vdev_forcefault = B_TRUE;

		vd->vdev_faulted = vd->vdev_degraded = 0ULL;
		vd->vdev_cant_read = B_FALSE;
		vd->vdev_cant_write = B_FALSE;
		vd->vdev_stat.vs_aux = 0;

		vdev_reopen(vd == rvd ? rvd : vd->vdev_top);

		vd->vdev_forcefault = B_FALSE;

		if (vd != rvd && vdev_writeable(vd->vdev_top))
			vdev_state_dirty(vd->vdev_top);

		/* If a resilver isn't required, check if vdevs can be culled */
		if (vd->vdev_aux == NULL && !vdev_is_dead(vd) &&
		    !dsl_scan_resilvering(spa->spa_dsl_pool) &&
		    !dsl_scan_resilver_scheduled(spa->spa_dsl_pool))
			spa_async_request(spa, SPA_ASYNC_RESILVER_DONE);

		spa_event_notify(spa, vd, NULL, ESC_ZFS_VDEV_CLEAR);
	}

	/*
	 * When clearing a FMA-diagnosed fault, we always want to
	 * unspare the device, as we assume that the original spare was
	 * done in response to the FMA fault.
	 */
	if (!vdev_is_dead(vd) && vd->vdev_parent != NULL &&
	    vd->vdev_parent->vdev_ops == &vdev_spare_ops &&
	    vd->vdev_parent->vdev_child[0] == vd)
		vd->vdev_unspare = B_TRUE;

	/* Clear recent error events cache (i.e. duplicate events tracking) */
	zfs_ereport_clear(spa, vd);
}

boolean_t
vdev_is_dead(vdev_t *vd)
{
	/*
	 * Holes and missing devices are always considered "dead".
	 * This simplifies the code since we don't have to check for
	 * these types of devices in the various code paths.
	 * Instead we rely on the fact that we skip over dead devices
	 * before issuing I/O to them.
	 */
	return (vd->vdev_state < VDEV_STATE_DEGRADED ||
	    vd->vdev_ops == &vdev_hole_ops ||
	    vd->vdev_ops == &vdev_missing_ops);
}

boolean_t
vdev_readable(vdev_t *vd)
{
	return (!vdev_is_dead(vd) && !vd->vdev_cant_read);
}

boolean_t
vdev_writeable(vdev_t *vd)
{
	return (!vdev_is_dead(vd) && !vd->vdev_cant_write &&
	    vdev_is_concrete(vd));
}

boolean_t
vdev_allocatable(vdev_t *vd)
{
	uint64_t state = vd->vdev_state;

	/*
	 * We currently allow allocations from vdevs which may be in the
	 * process of reopening (i.e. VDEV_STATE_CLOSED). If the device
	 * fails to reopen then we'll catch it later when we're holding
	 * the proper locks.  Note that we have to get the vdev state
	 * in a local variable because although it changes atomically,
	 * we're asking two separate questions about it.
	 */
	return (!(state < VDEV_STATE_DEGRADED && state != VDEV_STATE_CLOSED) &&
	    !vd->vdev_cant_write && vdev_is_concrete(vd) &&
	    vd->vdev_mg->mg_initialized);
}

boolean_t
vdev_accessible(vdev_t *vd, zio_t *zio)
{
	ASSERT(zio->io_vd == vd);

	if (vdev_is_dead(vd) || vd->vdev_remove_wanted)
		return (B_FALSE);

	if (zio->io_type == ZIO_TYPE_READ)
		return (!vd->vdev_cant_read);

	if (zio->io_type == ZIO_TYPE_WRITE)
		return (!vd->vdev_cant_write);

	return (B_TRUE);
}

static void
vdev_get_child_stat(vdev_t *cvd, vdev_stat_t *vs, vdev_stat_t *cvs)
{
	/*
	 * Exclude the dRAID spare when aggregating to avoid double counting
	 * the ops and bytes.  These IOs are counted by the physical leaves.
	 */
	if (cvd->vdev_ops == &vdev_draid_spare_ops)
		return;

	for (int t = 0; t < VS_ZIO_TYPES; t++) {
		vs->vs_ops[t] += cvs->vs_ops[t];
		vs->vs_bytes[t] += cvs->vs_bytes[t];
	}

	cvs->vs_scan_removing = cvd->vdev_removing;
}

/*
 * Get extended stats
 */
static void
vdev_get_child_stat_ex(vdev_t *cvd, vdev_stat_ex_t *vsx, vdev_stat_ex_t *cvsx)
{
	int t, b;
	for (t = 0; t < ZIO_TYPES; t++) {
		for (b = 0; b < ARRAY_SIZE(vsx->vsx_disk_histo[0]); b++)
			vsx->vsx_disk_histo[t][b] += cvsx->vsx_disk_histo[t][b];

		for (b = 0; b < ARRAY_SIZE(vsx->vsx_total_histo[0]); b++) {
			vsx->vsx_total_histo[t][b] +=
			    cvsx->vsx_total_histo[t][b];
		}
	}

	for (t = 0; t < ZIO_PRIORITY_NUM_QUEUEABLE; t++) {
		for (b = 0; b < ARRAY_SIZE(vsx->vsx_queue_histo[0]); b++) {
			vsx->vsx_queue_histo[t][b] +=
			    cvsx->vsx_queue_histo[t][b];
		}
		vsx->vsx_active_queue[t] += cvsx->vsx_active_queue[t];
		vsx->vsx_pend_queue[t] += cvsx->vsx_pend_queue[t];

		for (b = 0; b < ARRAY_SIZE(vsx->vsx_ind_histo[0]); b++)
			vsx->vsx_ind_histo[t][b] += cvsx->vsx_ind_histo[t][b];

		for (b = 0; b < ARRAY_SIZE(vsx->vsx_agg_histo[0]); b++)
			vsx->vsx_agg_histo[t][b] += cvsx->vsx_agg_histo[t][b];
	}

}

boolean_t
vdev_is_spacemap_addressable(vdev_t *vd)
{
	if (spa_feature_is_active(vd->vdev_spa, SPA_FEATURE_SPACEMAP_V2))
		return (B_TRUE);

	/*
	 * If double-word space map entries are not enabled we assume
	 * 47 bits of the space map entry are dedicated to the entry's
	 * offset (see SM_OFFSET_BITS in space_map.h). We then use that
	 * to calculate the maximum address that can be described by a
	 * space map entry for the given device.
	 */
	uint64_t shift = vd->vdev_ashift + SM_OFFSET_BITS;

	if (shift >= 63) /* detect potential overflow */
		return (B_TRUE);

	return (vd->vdev_asize < (1ULL << shift));
}

/*
 * Get statistics for the given vdev.
 */
static void
vdev_get_stats_ex_impl(vdev_t *vd, vdev_stat_t *vs, vdev_stat_ex_t *vsx)
{
	int t;
	/*
	 * If we're getting stats on the root vdev, aggregate the I/O counts
	 * over all top-level vdevs (i.e. the direct children of the root).
	 */
	if (!vd->vdev_ops->vdev_op_leaf) {
		if (vs) {
			memset(vs->vs_ops, 0, sizeof (vs->vs_ops));
			memset(vs->vs_bytes, 0, sizeof (vs->vs_bytes));
		}
		if (vsx)
			memset(vsx, 0, sizeof (*vsx));

		for (int c = 0; c < vd->vdev_children; c++) {
			vdev_t *cvd = vd->vdev_child[c];
			vdev_stat_t *cvs = &cvd->vdev_stat;
			vdev_stat_ex_t *cvsx = &cvd->vdev_stat_ex;

			vdev_get_stats_ex_impl(cvd, cvs, cvsx);
			if (vs)
				vdev_get_child_stat(cvd, vs, cvs);
			if (vsx)
				vdev_get_child_stat_ex(cvd, vsx, cvsx);
		}
	} else {
		/*
		 * We're a leaf.  Just copy our ZIO active queue stats in.  The
		 * other leaf stats are updated in vdev_stat_update().
		 */
		if (!vsx)
			return;

		memcpy(vsx, &vd->vdev_stat_ex, sizeof (vd->vdev_stat_ex));

		for (t = 0; t < ARRAY_SIZE(vd->vdev_queue.vq_class); t++) {
			vsx->vsx_active_queue[t] =
			    vd->vdev_queue.vq_class[t].vqc_active;
			vsx->vsx_pend_queue[t] = avl_numnodes(
			    &vd->vdev_queue.vq_class[t].vqc_queued_tree);
		}
	}
}

void
vdev_get_stats_ex(vdev_t *vd, vdev_stat_t *vs, vdev_stat_ex_t *vsx)
{
	vdev_t *tvd = vd->vdev_top;
	mutex_enter(&vd->vdev_stat_lock);
	if (vs) {
		bcopy(&vd->vdev_stat, vs, sizeof (*vs));
		vs->vs_timestamp = gethrtime() - vs->vs_timestamp;
		vs->vs_state = vd->vdev_state;
		vs->vs_rsize = vdev_get_min_asize(vd);

		if (vd->vdev_ops->vdev_op_leaf) {
			vs->vs_rsize += VDEV_LABEL_START_SIZE +
			    VDEV_LABEL_END_SIZE;
			/*
			 * Report initializing progress. Since we don't
			 * have the initializing locks held, this is only
			 * an estimate (although a fairly accurate one).
			 */
			vs->vs_initialize_bytes_done =
			    vd->vdev_initialize_bytes_done;
			vs->vs_initialize_bytes_est =
			    vd->vdev_initialize_bytes_est;
			vs->vs_initialize_state = vd->vdev_initialize_state;
			vs->vs_initialize_action_time =
			    vd->vdev_initialize_action_time;

			/*
			 * Report manual TRIM progress. Since we don't have
			 * the manual TRIM locks held, this is only an
			 * estimate (although fairly accurate one).
			 */
			vs->vs_trim_notsup = !vd->vdev_has_trim;
			vs->vs_trim_bytes_done = vd->vdev_trim_bytes_done;
			vs->vs_trim_bytes_est = vd->vdev_trim_bytes_est;
			vs->vs_trim_state = vd->vdev_trim_state;
			vs->vs_trim_action_time = vd->vdev_trim_action_time;

			/* Set when there is a deferred resilver. */
			vs->vs_resilver_deferred = vd->vdev_resilver_deferred;
		}

		/*
		 * Report expandable space on top-level, non-auxiliary devices
		 * only. The expandable space is reported in terms of metaslab
		 * sized units since that determines how much space the pool
		 * can expand.
		 */
		if (vd->vdev_aux == NULL && tvd != NULL) {
			vs->vs_esize = P2ALIGN(
			    vd->vdev_max_asize - vd->vdev_asize,
			    1ULL << tvd->vdev_ms_shift);
		}

		vs->vs_configured_ashift = vd->vdev_top != NULL
		    ? vd->vdev_top->vdev_ashift : vd->vdev_ashift;
		vs->vs_logical_ashift = vd->vdev_logical_ashift;
		vs->vs_physical_ashift = vd->vdev_physical_ashift;

		/*
		 * Report fragmentation and rebuild progress for top-level,
		 * non-auxiliary, concrete devices.
		 */
		if (vd->vdev_aux == NULL && vd == vd->vdev_top &&
		    vdev_is_concrete(vd)) {
			/*
			 * The vdev fragmentation rating doesn't take into
			 * account the embedded slog metaslab (vdev_log_mg).
			 * Since it's only one metaslab, it would have a tiny
			 * impact on the overall fragmentation.
			 */
			vs->vs_fragmentation = (vd->vdev_mg != NULL) ?
			    vd->vdev_mg->mg_fragmentation : 0;
		}
	}

	vdev_get_stats_ex_impl(vd, vs, vsx);
	mutex_exit(&vd->vdev_stat_lock);
}

void
vdev_get_stats(vdev_t *vd, vdev_stat_t *vs)
{
	return (vdev_get_stats_ex(vd, vs, NULL));
}

void
vdev_clear_stats(vdev_t *vd)
{
	mutex_enter(&vd->vdev_stat_lock);
	vd->vdev_stat.vs_space = 0;
	vd->vdev_stat.vs_dspace = 0;
	vd->vdev_stat.vs_alloc = 0;
	mutex_exit(&vd->vdev_stat_lock);
}

void
vdev_scan_stat_init(vdev_t *vd)
{
	vdev_stat_t *vs = &vd->vdev_stat;

	for (int c = 0; c < vd->vdev_children; c++)
		vdev_scan_stat_init(vd->vdev_child[c]);

	mutex_enter(&vd->vdev_stat_lock);
	vs->vs_scan_processed = 0;
	mutex_exit(&vd->vdev_stat_lock);
}

void
vdev_stat_update(zio_t *zio, uint64_t psize)
{
	spa_t *spa = zio->io_spa;
	vdev_t *rvd = spa->spa_root_vdev;
	vdev_t *vd = zio->io_vd ? zio->io_vd : rvd;
	vdev_t *pvd;
	uint64_t txg = zio->io_txg;
	vdev_stat_t *vs = &vd->vdev_stat;
	vdev_stat_ex_t *vsx = &vd->vdev_stat_ex;
	zio_type_t type = zio->io_type;
	int flags = zio->io_flags;

	/*
	 * If this i/o is a gang leader, it didn't do any actual work.
	 */
	if (zio->io_gang_tree)
		return;

	if (zio->io_error == 0) {
		/*
		 * If this is a root i/o, don't count it -- we've already
		 * counted the top-level vdevs, and vdev_get_stats() will
		 * aggregate them when asked.  This reduces contention on
		 * the root vdev_stat_lock and implicitly handles blocks
		 * that compress away to holes, for which there is no i/o.
		 * (Holes never create vdev children, so all the counters
		 * remain zero, which is what we want.)
		 *
		 * Note: this only applies to successful i/o (io_error == 0)
		 * because unlike i/o counts, errors are not additive.
		 * When reading a ditto block, for example, failure of
		 * one top-level vdev does not imply a root-level error.
		 */
		if (vd == rvd)
			return;

		ASSERT(vd == zio->io_vd);

		if (flags & ZIO_FLAG_IO_BYPASS)
			return;

		mutex_enter(&vd->vdev_stat_lock);

		if (flags & ZIO_FLAG_IO_REPAIR) {
			/*
			 * Repair is the result of a resilver issued by the
			 * scan thread (spa_sync).
			 */
			if (flags & ZIO_FLAG_SCAN_THREAD) {
				dsl_scan_t *scn = spa->spa_dsl_pool->dp_scan;
				dsl_scan_phys_t *scn_phys = &scn->scn_phys;
				uint64_t *processed = &scn_phys->scn_processed;

				if (vd->vdev_ops->vdev_op_leaf)
					atomic_add_64(processed, psize);
				vs->vs_scan_processed += psize;
			}

			/*
			 * Repair is the result of a rebuild issued by the
			 * rebuild thread (vdev_rebuild_thread).  To avoid
			 * double counting repaired bytes the virtual dRAID
			 * spare vdev is excluded from the processed bytes.
			 */
			if (zio->io_priority == ZIO_PRIORITY_REBUILD) {
				vdev_t *tvd = vd->vdev_top;
				vdev_rebuild_t *vr = &tvd->vdev_rebuild_config;
				vdev_rebuild_phys_t *vrp = &vr->vr_rebuild_phys;
				uint64_t *rebuilt = &vrp->vrp_bytes_rebuilt;

				if (vd->vdev_ops->vdev_op_leaf &&
				    vd->vdev_ops != &vdev_draid_spare_ops) {
					atomic_add_64(rebuilt, psize);
				}
				vs->vs_rebuild_processed += psize;
			}

			if (flags & ZIO_FLAG_SELF_HEAL)
				vs->vs_self_healed += psize;
		}

		/*
		 * The bytes/ops/histograms are recorded at the leaf level and
		 * aggregated into the higher level vdevs in vdev_get_stats().
		 */
		if (vd->vdev_ops->vdev_op_leaf &&
		    (zio->io_priority < ZIO_PRIORITY_NUM_QUEUEABLE)) {
			zio_type_t vs_type = type;
			zio_priority_t priority = zio->io_priority;

			/*
			 * TRIM ops and bytes are reported to user space as
			 * ZIO_TYPE_IOCTL.  This is done to preserve the
			 * vdev_stat_t structure layout for user space.
			 */
			if (type == ZIO_TYPE_TRIM)
				vs_type = ZIO_TYPE_IOCTL;

			/*
			 * Solely for the purposes of 'zpool iostat -lqrw'
			 * reporting use the priority to categorize the IO.
			 * Only the following are reported to user space:
			 *
			 *   ZIO_PRIORITY_SYNC_READ,
			 *   ZIO_PRIORITY_SYNC_WRITE,
			 *   ZIO_PRIORITY_ASYNC_READ,
			 *   ZIO_PRIORITY_ASYNC_WRITE,
			 *   ZIO_PRIORITY_SCRUB,
			 *   ZIO_PRIORITY_TRIM,
			 *   ZIO_PRIORITY_REBUILD.
			 */
			if (priority == ZIO_PRIORITY_INITIALIZING) {
				ASSERT3U(type, ==, ZIO_TYPE_WRITE);
				priority = ZIO_PRIORITY_ASYNC_WRITE;
			} else if (priority == ZIO_PRIORITY_REMOVAL) {
				priority = ((type == ZIO_TYPE_WRITE) ?
				    ZIO_PRIORITY_ASYNC_WRITE :
				    ZIO_PRIORITY_ASYNC_READ);
			}

			vs->vs_ops[vs_type]++;
			vs->vs_bytes[vs_type] += psize;

			if (flags & ZIO_FLAG_DELEGATED) {
				vsx->vsx_agg_histo[priority]
				    [RQ_HISTO(zio->io_size)]++;
			} else {
				vsx->vsx_ind_histo[priority]
				    [RQ_HISTO(zio->io_size)]++;
			}

			if (zio->io_delta && zio->io_delay) {
				vsx->vsx_queue_histo[priority]
				    [L_HISTO(zio->io_delta - zio->io_delay)]++;
				vsx->vsx_disk_histo[type]
				    [L_HISTO(zio->io_delay)]++;
				vsx->vsx_total_histo[type]
				    [L_HISTO(zio->io_delta)]++;
			}
		}

		mutex_exit(&vd->vdev_stat_lock);
		return;
	}

	if (flags & ZIO_FLAG_SPECULATIVE)
		return;

	/*
	 * If this is an I/O error that is going to be retried, then ignore the
	 * error.  Otherwise, the user may interpret B_FAILFAST I/O errors as
	 * hard errors, when in reality they can happen for any number of
	 * innocuous reasons (bus resets, MPxIO link failure, etc).
	 */
	if (zio->io_error == EIO &&
	    !(zio->io_flags & ZIO_FLAG_IO_RETRY))
		return;

	/*
	 * Intent logs writes won't propagate their error to the root
	 * I/O so don't mark these types of failures as pool-level
	 * errors.
	 */
	if (zio->io_vd == NULL && (zio->io_flags & ZIO_FLAG_DONT_PROPAGATE))
		return;

	if (type == ZIO_TYPE_WRITE && txg != 0 &&
	    (!(flags & ZIO_FLAG_IO_REPAIR) ||
	    (flags & ZIO_FLAG_SCAN_THREAD) ||
	    spa->spa_claiming)) {
		/*
		 * This is either a normal write (not a repair), or it's
		 * a repair induced by the scrub thread, or it's a repair
		 * made by zil_claim() during spa_load() in the first txg.
		 * In the normal case, we commit the DTL change in the same
		 * txg as the block was born.  In the scrub-induced repair
		 * case, we know that scrubs run in first-pass syncing context,
		 * so we commit the DTL change in spa_syncing_txg(spa).
		 * In the zil_claim() case, we commit in spa_first_txg(spa).
		 *
		 * We currently do not make DTL entries for failed spontaneous
		 * self-healing writes triggered by normal (non-scrubbing)
		 * reads, because we have no transactional context in which to
		 * do so -- and it's not clear that it'd be desirable anyway.
		 */
		if (vd->vdev_ops->vdev_op_leaf) {
			uint64_t commit_txg = txg;
			if (flags & ZIO_FLAG_SCAN_THREAD) {
				ASSERT(flags & ZIO_FLAG_IO_REPAIR);
				ASSERT(spa_sync_pass(spa) == 1);
				vdev_dtl_dirty(vd, DTL_SCRUB, txg, 1);
				commit_txg = spa_syncing_txg(spa);
			} else if (spa->spa_claiming) {
				ASSERT(flags & ZIO_FLAG_IO_REPAIR);
				commit_txg = spa_first_txg(spa);
			}
			ASSERT(commit_txg >= spa_syncing_txg(spa));
			if (vdev_dtl_contains(vd, DTL_MISSING, txg, 1))
				return;
			for (pvd = vd; pvd != rvd; pvd = pvd->vdev_parent)
				vdev_dtl_dirty(pvd, DTL_PARTIAL, txg, 1);
			vdev_dirty(vd->vdev_top, VDD_DTL, vd, commit_txg);
		}
		if (vd != rvd)
			vdev_dtl_dirty(vd, DTL_MISSING, txg, 1);
	}
}

int64_t
vdev_deflated_space(vdev_t *vd, int64_t space)
{
	ASSERT((space & (SPA_MINBLOCKSIZE-1)) == 0);
	ASSERT(vd->vdev_deflate_ratio != 0 || vd->vdev_isl2cache);

	return ((space >> SPA_MINBLOCKSHIFT) * vd->vdev_deflate_ratio);
}

/*
 * Update the in-core space usage stats for this vdev, its metaslab class,
 * and the root vdev.
 */
void
vdev_space_update(vdev_t *vd, int64_t alloc_delta, int64_t defer_delta,
    int64_t space_delta)
{
	int64_t dspace_delta;
	spa_t *spa = vd->vdev_spa;
	vdev_t *rvd = spa->spa_root_vdev;

	ASSERT(vd == vd->vdev_top);

	/*
	 * Apply the inverse of the psize-to-asize (ie. RAID-Z) space-expansion
	 * factor.  We must calculate this here and not at the root vdev
	 * because the root vdev's psize-to-asize is simply the max of its
	 * children's, thus not accurate enough for us.
	 */
	dspace_delta = vdev_deflated_space(vd, space_delta);

	mutex_enter(&vd->vdev_stat_lock);
	/* ensure we won't underflow */
	if (alloc_delta < 0) {
		ASSERT3U(vd->vdev_stat.vs_alloc, >=, -alloc_delta);
	}

	vd->vdev_stat.vs_alloc += alloc_delta;
	vd->vdev_stat.vs_space += space_delta;
	vd->vdev_stat.vs_dspace += dspace_delta;
	mutex_exit(&vd->vdev_stat_lock);

	/* every class but log contributes to root space stats */
	if (vd->vdev_mg != NULL && !vd->vdev_islog) {
		ASSERT(!vd->vdev_isl2cache);
		mutex_enter(&rvd->vdev_stat_lock);
		rvd->vdev_stat.vs_alloc += alloc_delta;
		rvd->vdev_stat.vs_space += space_delta;
		rvd->vdev_stat.vs_dspace += dspace_delta;
		mutex_exit(&rvd->vdev_stat_lock);
	}
	/* Note: metaslab_class_space_update moved to metaslab_space_update */
}

/*
 * Mark a top-level vdev's config as dirty, placing it on the dirty list
 * so that it will be written out next time the vdev configuration is synced.
 * If the root vdev is specified (vdev_top == NULL), dirty all top-level vdevs.
 */
void
vdev_config_dirty(vdev_t *vd)
{
	spa_t *spa = vd->vdev_spa;
	vdev_t *rvd = spa->spa_root_vdev;
	int c;

	ASSERT(spa_writeable(spa));

	/*
	 * If this is an aux vdev (as with l2cache and spare devices), then we
	 * update the vdev config manually and set the sync flag.
	 */
	if (vd->vdev_aux != NULL) {
		spa_aux_vdev_t *sav = vd->vdev_aux;
		nvlist_t **aux;
		uint_t naux;

		for (c = 0; c < sav->sav_count; c++) {
			if (sav->sav_vdevs[c] == vd)
				break;
		}

		if (c == sav->sav_count) {
			/*
			 * We're being removed.  There's nothing more to do.
			 */
			ASSERT(sav->sav_sync == B_TRUE);
			return;
		}

		sav->sav_sync = B_TRUE;

		if (nvlist_lookup_nvlist_array(sav->sav_config,
		    ZPOOL_CONFIG_L2CACHE, &aux, &naux) != 0) {
			VERIFY(nvlist_lookup_nvlist_array(sav->sav_config,
			    ZPOOL_CONFIG_SPARES, &aux, &naux) == 0);
		}

		ASSERT(c < naux);

		/*
		 * Setting the nvlist in the middle if the array is a little
		 * sketchy, but it will work.
		 */
		nvlist_free(aux[c]);
		aux[c] = vdev_config_generate(spa, vd, B_TRUE, 0);

		return;
	}

	/*
	 * The dirty list is protected by the SCL_CONFIG lock.  The caller
	 * must either hold SCL_CONFIG as writer, or must be the sync thread
	 * (which holds SCL_CONFIG as reader).  There's only one sync thread,
	 * so this is sufficient to ensure mutual exclusion.
	 */
	ASSERT(spa_config_held(spa, SCL_CONFIG, RW_WRITER) ||
	    (dsl_pool_sync_context(spa_get_dsl(spa)) &&
	    spa_config_held(spa, SCL_CONFIG, RW_READER)));

	if (vd == rvd) {
		for (c = 0; c < rvd->vdev_children; c++)
			vdev_config_dirty(rvd->vdev_child[c]);
	} else {
		ASSERT(vd == vd->vdev_top);

		if (!list_link_active(&vd->vdev_config_dirty_node) &&
		    vdev_is_concrete(vd)) {
			list_insert_head(&spa->spa_config_dirty_list, vd);
		}
	}
}

void
vdev_config_clean(vdev_t *vd)
{
	spa_t *spa = vd->vdev_spa;

	ASSERT(spa_config_held(spa, SCL_CONFIG, RW_WRITER) ||
	    (dsl_pool_sync_context(spa_get_dsl(spa)) &&
	    spa_config_held(spa, SCL_CONFIG, RW_READER)));

	ASSERT(list_link_active(&vd->vdev_config_dirty_node));
	list_remove(&spa->spa_config_dirty_list, vd);
}

/*
 * Mark a top-level vdev's state as dirty, so that the next pass of
 * spa_sync() can convert this into vdev_config_dirty().  We distinguish
 * the state changes from larger config changes because they require
 * much less locking, and are often needed for administrative actions.
 */
void
vdev_state_dirty(vdev_t *vd)
{
	spa_t *spa = vd->vdev_spa;

	ASSERT(spa_writeable(spa));
	ASSERT(vd == vd->vdev_top);

	/*
	 * The state list is protected by the SCL_STATE lock.  The caller
	 * must either hold SCL_STATE as writer, or must be the sync thread
	 * (which holds SCL_STATE as reader).  There's only one sync thread,
	 * so this is sufficient to ensure mutual exclusion.
	 */
	ASSERT(spa_config_held(spa, SCL_STATE, RW_WRITER) ||
	    (dsl_pool_sync_context(spa_get_dsl(spa)) &&
	    spa_config_held(spa, SCL_STATE, RW_READER)));

	if (!list_link_active(&vd->vdev_state_dirty_node) &&
	    vdev_is_concrete(vd))
		list_insert_head(&spa->spa_state_dirty_list, vd);
}

void
vdev_state_clean(vdev_t *vd)
{
	spa_t *spa = vd->vdev_spa;

	ASSERT(spa_config_held(spa, SCL_STATE, RW_WRITER) ||
	    (dsl_pool_sync_context(spa_get_dsl(spa)) &&
	    spa_config_held(spa, SCL_STATE, RW_READER)));

	ASSERT(list_link_active(&vd->vdev_state_dirty_node));
	list_remove(&spa->spa_state_dirty_list, vd);
}

/*
 * Propagate vdev state up from children to parent.
 */
void
vdev_propagate_state(vdev_t *vd)
{
	spa_t *spa = vd->vdev_spa;
	vdev_t *rvd = spa->spa_root_vdev;
	int degraded = 0, faulted = 0;
	int corrupted = 0;
	vdev_t *child;

	if (vd->vdev_children > 0) {
		for (int c = 0; c < vd->vdev_children; c++) {
			child = vd->vdev_child[c];

			/*
			 * Don't factor holes or indirect vdevs into the
			 * decision.
			 */
			if (!vdev_is_concrete(child))
				continue;

			if (!vdev_readable(child) ||
			    (!vdev_writeable(child) && spa_writeable(spa))) {
				/*
				 * Root special: if there is a top-level log
				 * device, treat the root vdev as if it were
				 * degraded.
				 */
				if (child->vdev_islog && vd == rvd)
					degraded++;
				else
					faulted++;
			} else if (child->vdev_state <= VDEV_STATE_DEGRADED) {
				degraded++;
			}

			if (child->vdev_stat.vs_aux == VDEV_AUX_CORRUPT_DATA)
				corrupted++;
		}

		vd->vdev_ops->vdev_op_state_change(vd, faulted, degraded);

		/*
		 * Root special: if there is a top-level vdev that cannot be
		 * opened due to corrupted metadata, then propagate the root
		 * vdev's aux state as 'corrupt' rather than 'insufficient
		 * replicas'.
		 */
		if (corrupted && vd == rvd &&
		    rvd->vdev_state == VDEV_STATE_CANT_OPEN)
			vdev_set_state(rvd, B_FALSE, VDEV_STATE_CANT_OPEN,
			    VDEV_AUX_CORRUPT_DATA);
	}

	if (vd->vdev_parent)
		vdev_propagate_state(vd->vdev_parent);
}

/*
 * Set a vdev's state.  If this is during an open, we don't update the parent
 * state, because we're in the process of opening children depth-first.
 * Otherwise, we propagate the change to the parent.
 *
 * If this routine places a device in a faulted state, an appropriate ereport is
 * generated.
 */
void
vdev_set_state(vdev_t *vd, boolean_t isopen, vdev_state_t state, vdev_aux_t aux)
{
	uint64_t save_state;
	spa_t *spa = vd->vdev_spa;

	if (state == vd->vdev_state) {
		/*
		 * Since vdev_offline() code path is already in an offline
		 * state we can miss a statechange event to OFFLINE. Check
		 * the previous state to catch this condition.
		 */
		if (vd->vdev_ops->vdev_op_leaf &&
		    (state == VDEV_STATE_OFFLINE) &&
		    (vd->vdev_prevstate >= VDEV_STATE_FAULTED)) {
			/* post an offline state change */
			zfs_post_state_change(spa, vd, vd->vdev_prevstate);
		}
		vd->vdev_stat.vs_aux = aux;
		return;
	}

	save_state = vd->vdev_state;

	vd->vdev_state = state;
	vd->vdev_stat.vs_aux = aux;

	/*
	 * If we are setting the vdev state to anything but an open state, then
	 * always close the underlying device unless the device has requested
	 * a delayed close (i.e. we're about to remove or fault the device).
	 * Otherwise, we keep accessible but invalid devices open forever.
	 * We don't call vdev_close() itself, because that implies some extra
	 * checks (offline, etc) that we don't want here.  This is limited to
	 * leaf devices, because otherwise closing the device will affect other
	 * children.
	 */
	if (!vd->vdev_delayed_close && vdev_is_dead(vd) &&
	    vd->vdev_ops->vdev_op_leaf)
		vd->vdev_ops->vdev_op_close(vd);

	if (vd->vdev_removed &&
	    state == VDEV_STATE_CANT_OPEN &&
	    (aux == VDEV_AUX_OPEN_FAILED || vd->vdev_checkremove)) {
		/*
		 * If the previous state is set to VDEV_STATE_REMOVED, then this
		 * device was previously marked removed and someone attempted to
		 * reopen it.  If this failed due to a nonexistent device, then
		 * keep the device in the REMOVED state.  We also let this be if
		 * it is one of our special test online cases, which is only
		 * attempting to online the device and shouldn't generate an FMA
		 * fault.
		 */
		vd->vdev_state = VDEV_STATE_REMOVED;
		vd->vdev_stat.vs_aux = VDEV_AUX_NONE;
	} else if (state == VDEV_STATE_REMOVED) {
		vd->vdev_removed = B_TRUE;
	} else if (state == VDEV_STATE_CANT_OPEN) {
		/*
		 * If we fail to open a vdev during an import or recovery, we
		 * mark it as "not available", which signifies that it was
		 * never there to begin with.  Failure to open such a device
		 * is not considered an error.
		 */
		if ((spa_load_state(spa) == SPA_LOAD_IMPORT ||
		    spa_load_state(spa) == SPA_LOAD_RECOVER) &&
		    vd->vdev_ops->vdev_op_leaf)
			vd->vdev_not_present = 1;

		/*
		 * Post the appropriate ereport.  If the 'prevstate' field is
		 * set to something other than VDEV_STATE_UNKNOWN, it indicates
		 * that this is part of a vdev_reopen().  In this case, we don't
		 * want to post the ereport if the device was already in the
		 * CANT_OPEN state beforehand.
		 *
		 * If the 'checkremove' flag is set, then this is an attempt to
		 * online the device in response to an insertion event.  If we
		 * hit this case, then we have detected an insertion event for a
		 * faulted or offline device that wasn't in the removed state.
		 * In this scenario, we don't post an ereport because we are
		 * about to replace the device, or attempt an online with
		 * vdev_forcefault, which will generate the fault for us.
		 */
		if ((vd->vdev_prevstate != state || vd->vdev_forcefault) &&
		    !vd->vdev_not_present && !vd->vdev_checkremove &&
		    vd != spa->spa_root_vdev) {
			const char *class;

			switch (aux) {
			case VDEV_AUX_OPEN_FAILED:
				class = FM_EREPORT_ZFS_DEVICE_OPEN_FAILED;
				break;
			case VDEV_AUX_CORRUPT_DATA:
				class = FM_EREPORT_ZFS_DEVICE_CORRUPT_DATA;
				break;
			case VDEV_AUX_NO_REPLICAS:
				class = FM_EREPORT_ZFS_DEVICE_NO_REPLICAS;
				break;
			case VDEV_AUX_BAD_GUID_SUM:
				class = FM_EREPORT_ZFS_DEVICE_BAD_GUID_SUM;
				break;
			case VDEV_AUX_TOO_SMALL:
				class = FM_EREPORT_ZFS_DEVICE_TOO_SMALL;
				break;
			case VDEV_AUX_BAD_LABEL:
				class = FM_EREPORT_ZFS_DEVICE_BAD_LABEL;
				break;
			case VDEV_AUX_BAD_ASHIFT:
				class = FM_EREPORT_ZFS_DEVICE_BAD_ASHIFT;
				break;
			default:
				class = FM_EREPORT_ZFS_DEVICE_UNKNOWN;
			}

			(void) zfs_ereport_post(class, spa, vd, NULL, NULL,
			    save_state);
		}

		/* Erase any notion of persistent removed state */
		vd->vdev_removed = B_FALSE;
	} else {
		vd->vdev_removed = B_FALSE;
	}

	/*
	 * Notify ZED of any significant state-change on a leaf vdev.
	 *
	 */
	if (vd->vdev_ops->vdev_op_leaf) {
		/* preserve original state from a vdev_reopen() */
		if ((vd->vdev_prevstate != VDEV_STATE_UNKNOWN) &&
		    (vd->vdev_prevstate != vd->vdev_state) &&
		    (save_state <= VDEV_STATE_CLOSED))
			save_state = vd->vdev_prevstate;

		/* filter out state change due to initial vdev_open */
		if (save_state > VDEV_STATE_CLOSED)
			zfs_post_state_change(spa, vd, save_state);
	}

	if (!isopen && vd->vdev_parent)
		vdev_propagate_state(vd->vdev_parent);
}

boolean_t
vdev_children_are_offline(vdev_t *vd)
{
	ASSERT(!vd->vdev_ops->vdev_op_leaf);

	for (uint64_t i = 0; i < vd->vdev_children; i++) {
		if (vd->vdev_child[i]->vdev_state != VDEV_STATE_OFFLINE)
			return (B_FALSE);
	}

	return (B_TRUE);
}

/*
 * Check the vdev configuration to ensure that it's capable of supporting
 * a root pool. We do not support partial configuration.
 */
boolean_t
vdev_is_bootable(vdev_t *vd)
{
	if (!vd->vdev_ops->vdev_op_leaf) {
		const char *vdev_type = vd->vdev_ops->vdev_op_type;

		if (strcmp(vdev_type, VDEV_TYPE_MISSING) == 0)
			return (B_FALSE);
	}

	for (int c = 0; c < vd->vdev_children; c++) {
		if (!vdev_is_bootable(vd->vdev_child[c]))
			return (B_FALSE);
	}
	return (B_TRUE);
}

boolean_t
vdev_is_concrete(vdev_t *vd)
{
	vdev_ops_t *ops = vd->vdev_ops;
	if (ops == &vdev_indirect_ops || ops == &vdev_hole_ops ||
	    ops == &vdev_missing_ops || ops == &vdev_root_ops) {
		return (B_FALSE);
	} else {
		return (B_TRUE);
	}
}

/*
 * Determine if a log device has valid content.  If the vdev was
 * removed or faulted in the MOS config then we know that
 * the content on the log device has already been written to the pool.
 */
boolean_t
vdev_log_state_valid(vdev_t *vd)
{
	if (vd->vdev_ops->vdev_op_leaf && !vd->vdev_faulted &&
	    !vd->vdev_removed)
		return (B_TRUE);

	for (int c = 0; c < vd->vdev_children; c++)
		if (vdev_log_state_valid(vd->vdev_child[c]))
			return (B_TRUE);

	return (B_FALSE);
}

/*
 * Expand a vdev if possible.
 */
void
vdev_expand(vdev_t *vd, uint64_t txg)
{
	ASSERT(vd->vdev_top == vd);
	ASSERT(spa_config_held(vd->vdev_spa, SCL_ALL, RW_WRITER) == SCL_ALL);
	ASSERT(vdev_is_concrete(vd));

	vdev_set_deflate_ratio(vd);

	if ((vd->vdev_asize >> vd->vdev_ms_shift) > vd->vdev_ms_count &&
	    vdev_is_concrete(vd)) {
		vdev_metaslab_group_create(vd);
		VERIFY(vdev_metaslab_init(vd, txg) == 0);
		vdev_config_dirty(vd);
	}
}

/*
 * Split a vdev.
 */
void
vdev_split(vdev_t *vd)
{
	vdev_t *cvd, *pvd = vd->vdev_parent;

	vdev_remove_child(pvd, vd);
	vdev_compact_children(pvd);

	cvd = pvd->vdev_child[0];
	if (pvd->vdev_children == 1) {
		vdev_remove_parent(cvd);
		cvd->vdev_splitting = B_TRUE;
	}
	vdev_propagate_state(cvd);
}

void
vdev_deadman(vdev_t *vd, char *tag)
{
	for (int c = 0; c < vd->vdev_children; c++) {
		vdev_t *cvd = vd->vdev_child[c];

		vdev_deadman(cvd, tag);
	}

	if (vd->vdev_ops->vdev_op_leaf) {
		vdev_queue_t *vq = &vd->vdev_queue;

		mutex_enter(&vq->vq_lock);
		if (avl_numnodes(&vq->vq_active_tree) > 0) {
			spa_t *spa = vd->vdev_spa;
			zio_t *fio;
			uint64_t delta;

			zfs_dbgmsg("slow vdev: %s has %lu active IOs",
			    vd->vdev_path, avl_numnodes(&vq->vq_active_tree));

			/*
			 * Look at the head of all the pending queues,
			 * if any I/O has been outstanding for longer than
			 * the spa_deadman_synctime invoke the deadman logic.
			 */
			fio = avl_first(&vq->vq_active_tree);
			delta = gethrtime() - fio->io_timestamp;
			if (delta > spa_deadman_synctime(spa))
				zio_deadman(fio, tag);
		}
		mutex_exit(&vq->vq_lock);
	}
}

void
vdev_defer_resilver(vdev_t *vd)
{
	ASSERT(vd->vdev_ops->vdev_op_leaf);

	vd->vdev_resilver_deferred = B_TRUE;
	vd->vdev_spa->spa_resilver_deferred = B_TRUE;
}

/*
 * Clears the resilver deferred flag on all leaf devs under vd. Returns
 * B_TRUE if we have devices that need to be resilvered and are available to
 * accept resilver I/Os.
 */
boolean_t
vdev_clear_resilver_deferred(vdev_t *vd, dmu_tx_t *tx)
{
	boolean_t resilver_needed = B_FALSE;
	spa_t *spa = vd->vdev_spa;

	for (int c = 0; c < vd->vdev_children; c++) {
		vdev_t *cvd = vd->vdev_child[c];
		resilver_needed |= vdev_clear_resilver_deferred(cvd, tx);
	}

	if (vd == spa->spa_root_vdev &&
	    spa_feature_is_active(spa, SPA_FEATURE_RESILVER_DEFER)) {
		spa_feature_decr(spa, SPA_FEATURE_RESILVER_DEFER, tx);
		vdev_config_dirty(vd);
		spa->spa_resilver_deferred = B_FALSE;
		return (resilver_needed);
	}

	if (!vdev_is_concrete(vd) || vd->vdev_aux ||
	    !vd->vdev_ops->vdev_op_leaf)
		return (resilver_needed);

	vd->vdev_resilver_deferred = B_FALSE;

	return (!vdev_is_dead(vd) && !vd->vdev_offline &&
	    vdev_resilver_needed(vd, NULL, NULL));
}

boolean_t
vdev_xlate_is_empty(range_seg64_t *rs)
{
	return (rs->rs_start == rs->rs_end);
}

/*
 * Translate a logical range to the first contiguous physical range for the
 * specified vdev_t.  This function is initially called with a leaf vdev and
 * will walk each parent vdev until it reaches a top-level vdev. Once the
 * top-level is reached the physical range is initialized and the recursive
 * function begins to unwind. As it unwinds it calls the parent's vdev
 * specific translation function to do the real conversion.
 */
void
vdev_xlate(vdev_t *vd, const range_seg64_t *logical_rs,
    range_seg64_t *physical_rs, range_seg64_t *remain_rs)
{
	/*
	 * Walk up the vdev tree
	 */
	if (vd != vd->vdev_top) {
		vdev_xlate(vd->vdev_parent, logical_rs, physical_rs,
		    remain_rs);
	} else {
		/*
		 * We've reached the top-level vdev, initialize the physical
		 * range to the logical range and set an empty remaining
		 * range then start to unwind.
		 */
		physical_rs->rs_start = logical_rs->rs_start;
		physical_rs->rs_end = logical_rs->rs_end;

		remain_rs->rs_start = logical_rs->rs_start;
		remain_rs->rs_end = logical_rs->rs_start;

		return;
	}

	vdev_t *pvd = vd->vdev_parent;
	ASSERT3P(pvd, !=, NULL);
	ASSERT3P(pvd->vdev_ops->vdev_op_xlate, !=, NULL);

	/*
	 * As this recursive function unwinds, translate the logical
	 * range into its physical and any remaining components by calling
	 * the vdev specific translate function.
	 */
	range_seg64_t intermediate = { 0 };
	pvd->vdev_ops->vdev_op_xlate(vd, physical_rs, &intermediate, remain_rs);

	physical_rs->rs_start = intermediate.rs_start;
	physical_rs->rs_end = intermediate.rs_end;
}

void
vdev_xlate_walk(vdev_t *vd, const range_seg64_t *logical_rs,
    vdev_xlate_func_t *func, void *arg)
{
	range_seg64_t iter_rs = *logical_rs;
	range_seg64_t physical_rs;
	range_seg64_t remain_rs;

	while (!vdev_xlate_is_empty(&iter_rs)) {

		vdev_xlate(vd, &iter_rs, &physical_rs, &remain_rs);

		/*
		 * With raidz and dRAID, it's possible that the logical range
		 * does not live on this leaf vdev. Only when there is a non-
		 * zero physical size call the provided function.
		 */
		if (!vdev_xlate_is_empty(&physical_rs))
			func(arg, &physical_rs);

		iter_rs = remain_rs;
	}
}

/*
 * Look at the vdev tree and determine whether any devices are currently being
 * replaced.
 */
boolean_t
vdev_replace_in_progress(vdev_t *vdev)
{
	ASSERT(spa_config_held(vdev->vdev_spa, SCL_ALL, RW_READER) != 0);

	if (vdev->vdev_ops == &vdev_replacing_ops)
		return (B_TRUE);

	/*
	 * A 'spare' vdev indicates that we have a replace in progress, unless
	 * it has exactly two children, and the second, the hot spare, has
	 * finished being resilvered.
	 */
	if (vdev->vdev_ops == &vdev_spare_ops && (vdev->vdev_children > 2 ||
	    !vdev_dtl_empty(vdev->vdev_child[1], DTL_MISSING)))
		return (B_TRUE);

	for (int i = 0; i < vdev->vdev_children; i++) {
		if (vdev_replace_in_progress(vdev->vdev_child[i]))
			return (B_TRUE);
	}

	return (B_FALSE);
}

EXPORT_SYMBOL(vdev_fault);
EXPORT_SYMBOL(vdev_degrade);
EXPORT_SYMBOL(vdev_online);
EXPORT_SYMBOL(vdev_offline);
EXPORT_SYMBOL(vdev_clear);

/* BEGIN CSTYLED */
ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, default_ms_count, INT, ZMOD_RW,
	"Target number of metaslabs per top-level vdev");

ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, default_ms_shift, INT, ZMOD_RW,
	"Default limit for metaslab size");

ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, min_ms_count, INT, ZMOD_RW,
	"Minimum number of metaslabs per top-level vdev");

ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, ms_count_limit, INT, ZMOD_RW,
	"Practical upper limit of total metaslabs per top-level vdev");

ZFS_MODULE_PARAM(zfs, zfs_, slow_io_events_per_second, UINT, ZMOD_RW,
	"Rate limit slow IO (delay) events to this many per second");

ZFS_MODULE_PARAM(zfs, zfs_, checksum_events_per_second, UINT, ZMOD_RW,
	"Rate limit checksum events to this many checksum errors per second "
	"(do not set below zed threshold).");

ZFS_MODULE_PARAM(zfs, zfs_, scan_ignore_errors, INT, ZMOD_RW,
	"Ignore errors during resilver/scrub");

ZFS_MODULE_PARAM(zfs_vdev, vdev_, validate_skip, INT, ZMOD_RW,
	"Bypass vdev_validate()");

ZFS_MODULE_PARAM(zfs, zfs_, nocacheflush, INT, ZMOD_RW,
	"Disable cache flushes");

ZFS_MODULE_PARAM(zfs, zfs_, embedded_slog_min_ms, INT, ZMOD_RW,
	"Minimum number of metaslabs required to dedicate one for log blocks");

ZFS_MODULE_PARAM_CALL(zfs_vdev, zfs_vdev_, min_auto_ashift,
	param_set_min_auto_ashift, param_get_ulong, ZMOD_RW,
	"Minimum ashift used when creating new top-level vdevs");

ZFS_MODULE_PARAM_CALL(zfs_vdev, zfs_vdev_, max_auto_ashift,
	param_set_max_auto_ashift, param_get_ulong, ZMOD_RW,
	"Maximum ashift used when optimizing for logical -> physical sector "
	"size on new top-level vdevs");
/* END CSTYLED */
