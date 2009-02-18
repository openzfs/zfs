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

#include <sys/zfs_context.h>
#include <sys/fm/fs/zfs.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/dmu.h>
#include <sys/dmu_tx.h>
#include <sys/vdev_impl.h>
#include <sys/uberblock_impl.h>
#include <sys/metaslab.h>
#include <sys/metaslab_impl.h>
#include <sys/space_map.h>
#include <sys/zio.h>
#include <sys/zap.h>
#include <sys/fs/zfs.h>
#include <sys/arc.h>

/*
 * Virtual device management.
 */

static vdev_ops_t *vdev_ops_table[] = {
	&vdev_root_ops,
	&vdev_raidz_ops,
	&vdev_mirror_ops,
	&vdev_replacing_ops,
	&vdev_spare_ops,
	&vdev_disk_ops,
	&vdev_file_ops,
	&vdev_missing_ops,
	NULL
};

/* maximum scrub/resilver I/O queue per leaf vdev */
int zfs_scrub_limit = 10;

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
 * Default asize function: return the MAX of psize with the asize of
 * all children.  This is what's used by anything other than RAID-Z.
 */
uint64_t
vdev_default_asize(vdev_t *vd, uint64_t psize)
{
	uint64_t asize = P2ROUNDUP(psize, 1ULL << vd->vdev_top->vdev_ashift);
	uint64_t csize;
	uint64_t c;

	for (c = 0; c < vd->vdev_children; c++) {
		csize = vdev_psize_to_asize(vd->vdev_child[c], psize);
		asize = MAX(asize, csize);
	}

	return (asize);
}

/*
 * Get the replaceable or attachable device size.
 * If the parent is a mirror or raidz, the replaceable size is the minimum
 * psize of all its children. For the rest, just return our own psize.
 *
 * e.g.
 *			psize	rsize
 * root			-	-
 *	mirror/raidz	-	-
 *	    disk1	20g	20g
 *	    disk2 	40g	20g
 *	disk3 		80g	80g
 */
uint64_t
vdev_get_rsize(vdev_t *vd)
{
	vdev_t *pvd, *cvd;
	uint64_t c, rsize;

	pvd = vd->vdev_parent;

	/*
	 * If our parent is NULL or the root, just return our own psize.
	 */
	if (pvd == NULL || pvd->vdev_parent == NULL)
		return (vd->vdev_psize);

	rsize = 0;

	for (c = 0; c < pvd->vdev_children; c++) {
		cvd = pvd->vdev_child[c];
		rsize = MIN(rsize - 1, cvd->vdev_psize - 1) + 1;
	}

	return (rsize);
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
	int c;
	vdev_t *mvd;

	if (vd->vdev_guid == guid)
		return (vd);

	for (c = 0; c < vd->vdev_children; c++)
		if ((mvd = vdev_lookup_by_guid(vd->vdev_child[c], guid)) !=
		    NULL)
			return (mvd);

	return (NULL);
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

	newchild = kmem_zalloc(newsize, KM_SLEEP);
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

	if (cvd->vdev_ops->vdev_op_leaf)
		cvd->vdev_spa->spa_scrub_maxinflight += zfs_scrub_limit;
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

	/*
	 * Walk up all ancestors to update guid sum.
	 */
	for (; pvd != NULL; pvd = pvd->vdev_parent)
		pvd->vdev_guid_sum -= cvd->vdev_guid_sum;

	if (cvd->vdev_ops->vdev_op_leaf)
		cvd->vdev_spa->spa_scrub_maxinflight -= zfs_scrub_limit;
}

/*
 * Remove any holes in the child array.
 */
void
vdev_compact_children(vdev_t *pvd)
{
	vdev_t **newchild, *cvd;
	int oldc = pvd->vdev_children;
	int newc, c;

	ASSERT(spa_config_held(pvd->vdev_spa, SCL_ALL, RW_WRITER) == SCL_ALL);

	for (c = newc = 0; c < oldc; c++)
		if (pvd->vdev_child[c])
			newc++;

	newchild = kmem_alloc(newc * sizeof (vdev_t *), KM_SLEEP);

	for (c = newc = 0; c < oldc; c++) {
		if ((cvd = pvd->vdev_child[c]) != NULL) {
			newchild[newc] = cvd;
			cvd->vdev_id = newc++;
		}
	}

	kmem_free(pvd->vdev_child, oldc * sizeof (vdev_t *));
	pvd->vdev_child = newchild;
	pvd->vdev_children = newc;
}

/*
 * Allocate and minimally initialize a vdev_t.
 */
static vdev_t *
vdev_alloc_common(spa_t *spa, uint_t id, uint64_t guid, vdev_ops_t *ops)
{
	vdev_t *vd;

	vd = kmem_zalloc(sizeof (vdev_t), KM_SLEEP);

	if (spa->spa_root_vdev == NULL) {
		ASSERT(ops == &vdev_root_ops);
		spa->spa_root_vdev = vd;
	}

	if (guid == 0) {
		if (spa->spa_root_vdev == vd) {
			/*
			 * The root vdev's guid will also be the pool guid,
			 * which must be unique among all pools.
			 */
			while (guid == 0 || spa_guid_exists(guid, 0))
				guid = spa_get_random(-1ULL);
		} else {
			/*
			 * Any other vdev's guid must be unique within the pool.
			 */
			while (guid == 0 ||
			    spa_guid_exists(spa_guid(spa), guid))
				guid = spa_get_random(-1ULL);
		}
		ASSERT(!spa_guid_exists(spa_guid(spa), guid));
	}

	vd->vdev_spa = spa;
	vd->vdev_id = id;
	vd->vdev_guid = guid;
	vd->vdev_guid_sum = guid;
	vd->vdev_ops = ops;
	vd->vdev_state = VDEV_STATE_CLOSED;

	mutex_init(&vd->vdev_dtl_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&vd->vdev_stat_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&vd->vdev_probe_lock, NULL, MUTEX_DEFAULT, NULL);
	for (int t = 0; t < DTL_TYPES; t++) {
		space_map_create(&vd->vdev_dtl[t], 0, -1ULL, 0,
		    &vd->vdev_dtl_lock);
	}
	txg_list_create(&vd->vdev_ms_list,
	    offsetof(struct metaslab, ms_txg_node));
	txg_list_create(&vd->vdev_dtl_list,
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
	uint64_t guid = 0, islog, nparity;
	vdev_t *vd;

	ASSERT(spa_config_held(spa, SCL_ALL, RW_WRITER) == SCL_ALL);

	if (nvlist_lookup_string(nv, ZPOOL_CONFIG_TYPE, &type) != 0)
		return (EINVAL);

	if ((ops = vdev_getops(type)) == NULL)
		return (EINVAL);

	/*
	 * If this is a load, get the vdev guid from the nvlist.
	 * Otherwise, vdev_alloc_common() will generate one for us.
	 */
	if (alloctype == VDEV_ALLOC_LOAD) {
		uint64_t label_id;

		if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_ID, &label_id) ||
		    label_id != id)
			return (EINVAL);

		if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_GUID, &guid) != 0)
			return (EINVAL);
	} else if (alloctype == VDEV_ALLOC_SPARE) {
		if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_GUID, &guid) != 0)
			return (EINVAL);
	} else if (alloctype == VDEV_ALLOC_L2CACHE) {
		if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_GUID, &guid) != 0)
			return (EINVAL);
	}

	/*
	 * The first allocated vdev must be of type 'root'.
	 */
	if (ops != &vdev_root_ops && spa->spa_root_vdev == NULL)
		return (EINVAL);

	/*
	 * Determine whether we're a log vdev.
	 */
	islog = 0;
	(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_IS_LOG, &islog);
	if (islog && spa_version(spa) < SPA_VERSION_SLOGS)
		return (ENOTSUP);

	/*
	 * Set the nparity property for RAID-Z vdevs.
	 */
	nparity = -1ULL;
	if (ops == &vdev_raidz_ops) {
		if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_NPARITY,
		    &nparity) == 0) {
			/*
			 * Currently, we can only support 2 parity devices.
			 */
			if (nparity == 0 || nparity > 2)
				return (EINVAL);
			/*
			 * Older versions can only support 1 parity device.
			 */
			if (nparity == 2 &&
			    spa_version(spa) < SPA_VERSION_RAID6)
				return (ENOTSUP);
		} else {
			/*
			 * We require the parity to be specified for SPAs that
			 * support multiple parity levels.
			 */
			if (spa_version(spa) >= SPA_VERSION_RAID6)
				return (EINVAL);
			/*
			 * Otherwise, we default to 1 parity device for RAID-Z.
			 */
			nparity = 1;
		}
	} else {
		nparity = 0;
	}
	ASSERT(nparity != -1ULL);

	vd = vdev_alloc_common(spa, id, guid, ops);

	vd->vdev_islog = islog;
	vd->vdev_nparity = nparity;

	if (nvlist_lookup_string(nv, ZPOOL_CONFIG_PATH, &vd->vdev_path) == 0)
		vd->vdev_path = spa_strdup(vd->vdev_path);
	if (nvlist_lookup_string(nv, ZPOOL_CONFIG_DEVID, &vd->vdev_devid) == 0)
		vd->vdev_devid = spa_strdup(vd->vdev_devid);
	if (nvlist_lookup_string(nv, ZPOOL_CONFIG_PHYS_PATH,
	    &vd->vdev_physpath) == 0)
		vd->vdev_physpath = spa_strdup(vd->vdev_physpath);

	/*
	 * Set the whole_disk property.  If it's not specified, leave the value
	 * as -1.
	 */
	if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_WHOLE_DISK,
	    &vd->vdev_wholedisk) != 0)
		vd->vdev_wholedisk = -1ULL;

	/*
	 * Look for the 'not present' flag.  This will only be set if the device
	 * was not present at the time of import.
	 */
	if (!spa->spa_import_faulted)
		(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_NOT_PRESENT,
		    &vd->vdev_not_present);

	/*
	 * Get the alignment requirement.
	 */
	(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_ASHIFT, &vd->vdev_ashift);

	/*
	 * If we're a top-level vdev, try to load the allocation parameters.
	 */
	if (parent && !parent->vdev_parent && alloctype == VDEV_ALLOC_LOAD) {
		(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_METASLAB_ARRAY,
		    &vd->vdev_ms_array);
		(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_METASLAB_SHIFT,
		    &vd->vdev_ms_shift);
		(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_ASIZE,
		    &vd->vdev_asize);
	}

	/*
	 * If we're a leaf vdev, try to load the DTL object and other state.
	 */
	if (vd->vdev_ops->vdev_op_leaf &&
	    (alloctype == VDEV_ALLOC_LOAD || alloctype == VDEV_ALLOC_L2CACHE)) {
		if (alloctype == VDEV_ALLOC_LOAD) {
			(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_DTL,
			    &vd->vdev_dtl_smo.smo_object);
			(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_UNSPARE,
			    &vd->vdev_unspare);
		}
		(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_OFFLINE,
		    &vd->vdev_offline);

		/*
		 * When importing a pool, we want to ignore the persistent fault
		 * state, as the diagnosis made on another system may not be
		 * valid in the current context.
		 */
		if (spa->spa_load_state == SPA_LOAD_OPEN) {
			(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_FAULTED,
			    &vd->vdev_faulted);
			(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_DEGRADED,
			    &vd->vdev_degraded);
			(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_REMOVED,
			    &vd->vdev_removed);
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
	int c;
	spa_t *spa = vd->vdev_spa;

	/*
	 * vdev_free() implies closing the vdev first.  This is simpler than
	 * trying to ensure complicated semantics for all callers.
	 */
	vdev_close(vd);

	ASSERT(!list_link_active(&vd->vdev_config_dirty_node));

	/*
	 * Free all children.
	 */
	for (c = 0; c < vd->vdev_children; c++)
		vdev_free(vd->vdev_child[c]);

	ASSERT(vd->vdev_child == NULL);
	ASSERT(vd->vdev_guid_sum == vd->vdev_guid);

	/*
	 * Discard allocation state.
	 */
	if (vd == vd->vdev_top)
		vdev_metaslab_fini(vd);

	ASSERT3U(vd->vdev_stat.vs_space, ==, 0);
	ASSERT3U(vd->vdev_stat.vs_dspace, ==, 0);
	ASSERT3U(vd->vdev_stat.vs_alloc, ==, 0);

	/*
	 * Remove this vdev from its parent's child list.
	 */
	vdev_remove_child(vd->vdev_parent, vd);

	ASSERT(vd->vdev_parent == NULL);

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

	if (vd->vdev_isspare)
		spa_spare_remove(vd);
	if (vd->vdev_isl2cache)
		spa_l2cache_remove(vd);

	txg_list_destroy(&vd->vdev_ms_list);
	txg_list_destroy(&vd->vdev_dtl_list);

	mutex_enter(&vd->vdev_dtl_lock);
	for (int t = 0; t < DTL_TYPES; t++) {
		space_map_unload(&vd->vdev_dtl[t]);
		space_map_destroy(&vd->vdev_dtl[t]);
	}
	mutex_exit(&vd->vdev_dtl_lock);

	mutex_destroy(&vd->vdev_dtl_lock);
	mutex_destroy(&vd->vdev_stat_lock);
	mutex_destroy(&vd->vdev_probe_lock);

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

	tvd->vdev_ms_array = svd->vdev_ms_array;
	tvd->vdev_ms_shift = svd->vdev_ms_shift;
	tvd->vdev_ms_count = svd->vdev_ms_count;

	svd->vdev_ms_array = 0;
	svd->vdev_ms_shift = 0;
	svd->vdev_ms_count = 0;

	tvd->vdev_mg = svd->vdev_mg;
	tvd->vdev_ms = svd->vdev_ms;

	svd->vdev_mg = NULL;
	svd->vdev_ms = NULL;

	if (tvd->vdev_mg != NULL)
		tvd->vdev_mg->mg_vd = tvd;

	tvd->vdev_stat.vs_alloc = svd->vdev_stat.vs_alloc;
	tvd->vdev_stat.vs_space = svd->vdev_stat.vs_space;
	tvd->vdev_stat.vs_dspace = svd->vdev_stat.vs_dspace;

	svd->vdev_stat.vs_alloc = 0;
	svd->vdev_stat.vs_space = 0;
	svd->vdev_stat.vs_dspace = 0;

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
}

static void
vdev_top_update(vdev_t *tvd, vdev_t *vd)
{
	int c;

	if (vd == NULL)
		return;

	vd->vdev_top = tvd;

	for (c = 0; c < vd->vdev_children; c++)
		vdev_top_update(tvd, vd->vdev_child[c]);
}

/*
 * Add a mirror/replacing vdev above an existing vdev.
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
	mvd->vdev_ashift = cvd->vdev_ashift;
	mvd->vdev_state = cvd->vdev_state;

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
		cvd->vdev_guid += guid_delta;
		cvd->vdev_guid_sum += guid_delta;
	}
	cvd->vdev_id = mvd->vdev_id;
	vdev_add_child(pvd, cvd);
	vdev_top_update(cvd->vdev_top, cvd->vdev_top);

	if (cvd == cvd->vdev_top)
		vdev_top_transfer(mvd, cvd);

	ASSERT(mvd->vdev_children == 0);
	vdev_free(mvd);
}

int
vdev_metaslab_init(vdev_t *vd, uint64_t txg)
{
	spa_t *spa = vd->vdev_spa;
	objset_t *mos = spa->spa_meta_objset;
	metaslab_class_t *mc;
	uint64_t m;
	uint64_t oldc = vd->vdev_ms_count;
	uint64_t newc = vd->vdev_asize >> vd->vdev_ms_shift;
	metaslab_t **mspp;
	int error;

	if (vd->vdev_ms_shift == 0)	/* not being allocated from yet */
		return (0);

	ASSERT(oldc <= newc);

	if (vd->vdev_islog)
		mc = spa->spa_log_class;
	else
		mc = spa->spa_normal_class;

	if (vd->vdev_mg == NULL)
		vd->vdev_mg = metaslab_group_create(mc, vd);

	mspp = kmem_zalloc(newc * sizeof (*mspp), KM_SLEEP);

	if (oldc != 0) {
		bcopy(vd->vdev_ms, mspp, oldc * sizeof (*mspp));
		kmem_free(vd->vdev_ms, oldc * sizeof (*mspp));
	}

	vd->vdev_ms = mspp;
	vd->vdev_ms_count = newc;

	for (m = oldc; m < newc; m++) {
		space_map_obj_t smo = { 0, 0, 0 };
		if (txg == 0) {
			uint64_t object = 0;
			error = dmu_read(mos, vd->vdev_ms_array,
			    m * sizeof (uint64_t), sizeof (uint64_t), &object);
			if (error)
				return (error);
			if (object != 0) {
				dmu_buf_t *db;
				error = dmu_bonus_hold(mos, object, FTAG, &db);
				if (error)
					return (error);
				ASSERT3U(db->db_size, >=, sizeof (smo));
				bcopy(db->db_data, &smo, sizeof (smo));
				ASSERT3U(smo.smo_object, ==, object);
				dmu_buf_rele(db, FTAG);
			}
		}
		vd->vdev_ms[m] = metaslab_init(vd->vdev_mg, &smo,
		    m << vd->vdev_ms_shift, 1ULL << vd->vdev_ms_shift, txg);
	}

	return (0);
}

void
vdev_metaslab_fini(vdev_t *vd)
{
	uint64_t m;
	uint64_t count = vd->vdev_ms_count;

	if (vd->vdev_ms != NULL) {
		for (m = 0; m < count; m++)
			if (vd->vdev_ms[m] != NULL)
				metaslab_fini(vd->vdev_ms[m]);
		kmem_free(vd->vdev_ms, count * sizeof (metaslab_t *));
		vd->vdev_ms = NULL;
	}
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
			    zio->io_offset, zio->io_size, zio->io_data,
			    ZIO_CHECKSUM_OFF, vdev_probe_done, vps,
			    ZIO_PRIORITY_SYNC_WRITE, vps->vps_flags, B_TRUE));
		} else {
			zio_buf_free(zio->io_data, zio->io_size);
		}
	} else if (zio->io_type == ZIO_TYPE_WRITE) {
		if (zio->io_error == 0)
			vps->vps_writeable = 1;
		zio_buf_free(zio->io_data, zio->io_size);
	} else if (zio->io_type == ZIO_TYPE_NULL) {
		zio_t *pio;

		vd->vdev_cant_read |= !vps->vps_readable;
		vd->vdev_cant_write |= !vps->vps_writeable;

		if (vdev_readable(vd) &&
		    (vdev_writeable(vd) || !spa_writeable(spa))) {
			zio->io_error = 0;
		} else {
			ASSERT(zio->io_error != 0);
			zfs_ereport_post(FM_EREPORT_ZFS_PROBE_FAILURE,
			    spa, vd, NULL, 0, 0);
			zio->io_error = ENXIO;
		}

		mutex_enter(&vd->vdev_probe_lock);
		ASSERT(vd->vdev_probe_zio == zio);
		vd->vdev_probe_zio = NULL;
		mutex_exit(&vd->vdev_probe_lock);

		while ((pio = zio_walk_parents(zio)) != NULL)
			if (!vdev_accessible(vd, pio))
				pio->io_error = ENXIO;

		kmem_free(vps, sizeof (*vps));
	}
}

/*
 * Determine whether this device is accessible by reading and writing
 * to several known locations: the pad regions of each vdev label
 * but the first (which we leave alone in case it contains a VTOC).
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
		    ZIO_FLAG_DONT_RETRY;

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
		    offsetof(vdev_label_t, vl_pad)),
		    VDEV_SKIP_SIZE, zio_buf_alloc(VDEV_SKIP_SIZE),
		    ZIO_CHECKSUM_OFF, vdev_probe_done, vps,
		    ZIO_PRIORITY_SYNC_READ, vps->vps_flags, B_TRUE));
	}

	if (zio == NULL)
		return (pio);

	zio_nowait(pio);
	return (NULL);
}

/*
 * Prepare a virtual device for access.
 */
int
vdev_open(vdev_t *vd)
{
	spa_t *spa = vd->vdev_spa;
	int error;
	int c;
	uint64_t osize = 0;
	uint64_t asize, psize;
	uint64_t ashift = 0;

	ASSERT(spa_config_held(spa, SCL_STATE_ALL, RW_WRITER) == SCL_STATE_ALL);

	ASSERT(vd->vdev_state == VDEV_STATE_CLOSED ||
	    vd->vdev_state == VDEV_STATE_CANT_OPEN ||
	    vd->vdev_state == VDEV_STATE_OFFLINE);

	vd->vdev_stat.vs_aux = VDEV_AUX_NONE;

	if (!vd->vdev_removed && vd->vdev_faulted) {
		ASSERT(vd->vdev_children == 0);
		vdev_set_state(vd, B_TRUE, VDEV_STATE_FAULTED,
		    VDEV_AUX_ERR_EXCEEDED);
		return (ENXIO);
	} else if (vd->vdev_offline) {
		ASSERT(vd->vdev_children == 0);
		vdev_set_state(vd, B_TRUE, VDEV_STATE_OFFLINE, VDEV_AUX_NONE);
		return (ENXIO);
	}

	error = vd->vdev_ops->vdev_op_open(vd, &osize, &ashift);

	if (zio_injection_enabled && error == 0)
		error = zio_handle_device_injection(vd, ENXIO);

	if (error) {
		if (vd->vdev_removed &&
		    vd->vdev_stat.vs_aux != VDEV_AUX_OPEN_FAILED)
			vd->vdev_removed = B_FALSE;

		vdev_set_state(vd, B_TRUE, VDEV_STATE_CANT_OPEN,
		    vd->vdev_stat.vs_aux);
		return (error);
	}

	vd->vdev_removed = B_FALSE;

	if (vd->vdev_degraded) {
		ASSERT(vd->vdev_children == 0);
		vdev_set_state(vd, B_TRUE, VDEV_STATE_DEGRADED,
		    VDEV_AUX_ERR_EXCEEDED);
	} else {
		vd->vdev_state = VDEV_STATE_HEALTHY;
	}

	for (c = 0; c < vd->vdev_children; c++)
		if (vd->vdev_child[c]->vdev_state != VDEV_STATE_HEALTHY) {
			vdev_set_state(vd, B_TRUE, VDEV_STATE_DEGRADED,
			    VDEV_AUX_NONE);
			break;
		}

	osize = P2ALIGN(osize, (uint64_t)sizeof (vdev_label_t));

	if (vd->vdev_children == 0) {
		if (osize < SPA_MINDEVSIZE) {
			vdev_set_state(vd, B_TRUE, VDEV_STATE_CANT_OPEN,
			    VDEV_AUX_TOO_SMALL);
			return (EOVERFLOW);
		}
		psize = osize;
		asize = osize - (VDEV_LABEL_START_SIZE + VDEV_LABEL_END_SIZE);
	} else {
		if (vd->vdev_parent != NULL && osize < SPA_MINDEVSIZE -
		    (VDEV_LABEL_START_SIZE + VDEV_LABEL_END_SIZE)) {
			vdev_set_state(vd, B_TRUE, VDEV_STATE_CANT_OPEN,
			    VDEV_AUX_TOO_SMALL);
			return (EOVERFLOW);
		}
		psize = 0;
		asize = osize;
	}

	vd->vdev_psize = psize;

	if (vd->vdev_asize == 0) {
		/*
		 * This is the first-ever open, so use the computed values.
		 * For testing purposes, a higher ashift can be requested.
		 */
		vd->vdev_asize = asize;
		vd->vdev_ashift = MAX(ashift, vd->vdev_ashift);
	} else {
		/*
		 * Make sure the alignment requirement hasn't increased.
		 */
		if (ashift > vd->vdev_top->vdev_ashift) {
			vdev_set_state(vd, B_TRUE, VDEV_STATE_CANT_OPEN,
			    VDEV_AUX_BAD_LABEL);
			return (EINVAL);
		}

		/*
		 * Make sure the device hasn't shrunk.
		 */
		if (asize < vd->vdev_asize) {
			vdev_set_state(vd, B_TRUE, VDEV_STATE_CANT_OPEN,
			    VDEV_AUX_BAD_LABEL);
			return (EINVAL);
		}

		/*
		 * If all children are healthy and the asize has increased,
		 * then we've experienced dynamic LUN growth.
		 */
		if (vd->vdev_state == VDEV_STATE_HEALTHY &&
		    asize > vd->vdev_asize) {
			vd->vdev_asize = asize;
		}
	}

	/*
	 * Ensure we can issue some IO before declaring the
	 * vdev open for business.
	 */
	if (vd->vdev_ops->vdev_op_leaf &&
	    (error = zio_wait(vdev_probe(vd, NULL))) != 0) {
		vdev_set_state(vd, B_TRUE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_IO_FAILURE);
		return (error);
	}

	/*
	 * If this is a top-level vdev, compute the raidz-deflation
	 * ratio.  Note, we hard-code in 128k (1<<17) because it is the
	 * current "typical" blocksize.  Even if SPA_MAXBLOCKSIZE
	 * changes, this algorithm must never change, or we will
	 * inconsistently account for existing bp's.
	 */
	if (vd->vdev_top == vd) {
		vd->vdev_deflate_ratio = (1<<17) /
		    (vdev_psize_to_asize(vd, 1<<17) >> SPA_MINBLOCKSHIFT);
	}

	/*
	 * If a leaf vdev has a DTL, and seems healthy, then kick off a
	 * resilver.  But don't do this if we are doing a reopen for a scrub,
	 * since this would just restart the scrub we are already doing.
	 */
	if (vd->vdev_ops->vdev_op_leaf && !spa->spa_scrub_reopen &&
	    vdev_resilver_needed(vd, NULL, NULL))
		spa_async_request(spa, SPA_ASYNC_RESILVER);

	return (0);
}

/*
 * Called once the vdevs are all opened, this routine validates the label
 * contents.  This needs to be done before vdev_load() so that we don't
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
	int c;
	nvlist_t *label;
	uint64_t guid, top_guid;
	uint64_t state;

	for (c = 0; c < vd->vdev_children; c++)
		if (vdev_validate(vd->vdev_child[c]) != 0)
			return (EBADF);

	/*
	 * If the device has already failed, or was marked offline, don't do
	 * any further validation.  Otherwise, label I/O will fail and we will
	 * overwrite the previous state.
	 */
	if (vd->vdev_ops->vdev_op_leaf && vdev_readable(vd)) {

		if ((label = vdev_label_read_config(vd)) == NULL) {
			vdev_set_state(vd, B_TRUE, VDEV_STATE_CANT_OPEN,
			    VDEV_AUX_BAD_LABEL);
			return (0);
		}

		if (nvlist_lookup_uint64(label, ZPOOL_CONFIG_POOL_GUID,
		    &guid) != 0 || guid != spa_guid(spa)) {
			vdev_set_state(vd, B_FALSE, VDEV_STATE_CANT_OPEN,
			    VDEV_AUX_CORRUPT_DATA);
			nvlist_free(label);
			return (0);
		}

		/*
		 * If this vdev just became a top-level vdev because its
		 * sibling was detached, it will have adopted the parent's
		 * vdev guid -- but the label may or may not be on disk yet.
		 * Fortunately, either version of the label will have the
		 * same top guid, so if we're a top-level vdev, we can
		 * safely compare to that instead.
		 */
		if (nvlist_lookup_uint64(label, ZPOOL_CONFIG_GUID,
		    &guid) != 0 ||
		    nvlist_lookup_uint64(label, ZPOOL_CONFIG_TOP_GUID,
		    &top_guid) != 0 ||
		    (vd->vdev_guid != guid &&
		    (vd->vdev_guid != top_guid || vd != vd->vdev_top))) {
			vdev_set_state(vd, B_FALSE, VDEV_STATE_CANT_OPEN,
			    VDEV_AUX_CORRUPT_DATA);
			nvlist_free(label);
			return (0);
		}

		if (nvlist_lookup_uint64(label, ZPOOL_CONFIG_POOL_STATE,
		    &state) != 0) {
			vdev_set_state(vd, B_FALSE, VDEV_STATE_CANT_OPEN,
			    VDEV_AUX_CORRUPT_DATA);
			nvlist_free(label);
			return (0);
		}

		nvlist_free(label);

		if (spa->spa_load_state == SPA_LOAD_OPEN &&
		    state != POOL_STATE_ACTIVE)
			return (EBADF);

		/*
		 * If we were able to open and validate a vdev that was
		 * previously marked permanently unavailable, clear that state
		 * now.
		 */
		if (vd->vdev_not_present)
			vd->vdev_not_present = 0;
	}

	return (0);
}

/*
 * Close a virtual device.
 */
void
vdev_close(vdev_t *vd)
{
	spa_t *spa = vd->vdev_spa;

	ASSERT(spa_config_held(spa, SCL_STATE_ALL, RW_WRITER) == SCL_STATE_ALL);

	vd->vdev_ops->vdev_op_close(vd);

	vdev_cache_purge(vd);

	/*
	 * We record the previous state before we close it, so  that if we are
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
vdev_reopen(vdev_t *vd)
{
	spa_t *spa = vd->vdev_spa;

	ASSERT(spa_config_held(spa, SCL_STATE_ALL, RW_WRITER) == SCL_STATE_ALL);

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
		    !l2arc_vdev_present(vd)) {
			uint64_t size = vdev_get_rsize(vd);
			l2arc_add_vdev(spa, vd,
			    VDEV_LABEL_START_SIZE,
			    size - VDEV_LABEL_START_SIZE);
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
		return (error ? error : ENXIO);
	}

	/*
	 * Recursively initialize all labels.
	 */
	if ((error = vdev_label_init(vd, txg, isreplacing ?
	    VDEV_LABEL_REPLACE : VDEV_LABEL_CREATE)) != 0) {
		vdev_close(vd);
		return (error);
	}

	return (0);
}

/*
 * The is the latter half of vdev_create().  It is distinct because it
 * involves initiating transactions in order to do metaslab creation.
 * For creation, we want to try to create all vdevs at once and then undo it
 * if anything fails; this is much harder if we have pending transactions.
 */
void
vdev_init(vdev_t *vd, uint64_t txg)
{
	/*
	 * Aim for roughly 200 metaslabs per vdev.
	 */
	vd->vdev_ms_shift = highbit(vd->vdev_asize / 200);
	vd->vdev_ms_shift = MAX(vd->vdev_ms_shift, SPA_MAXBLOCKSHIFT);

	/*
	 * Initialize the vdev's metaslabs.  This can't fail because
	 * there's nothing to read when creating all new metaslabs.
	 */
	VERIFY(vdev_metaslab_init(vd, txg) == 0);
}

void
vdev_dirty(vdev_t *vd, int flags, void *arg, uint64_t txg)
{
	ASSERT(vd == vd->vdev_top);
	ASSERT(ISP2(flags));

	if (flags & VDD_METASLAB)
		(void) txg_list_add(&vd->vdev_ms_list, arg, txg);

	if (flags & VDD_DTL)
		(void) txg_list_add(&vd->vdev_dtl_list, arg, txg);

	(void) txg_list_add(&vd->vdev_spa->spa_vdev_txg_list, vd, txg);
}

/*
 * DTLs.
 *
 * A vdev's DTL (dirty time log) is the set of transaction groups for which
 * the vdev has less than perfect replication.  There are three kinds of DTL:
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
	space_map_t *sm = &vd->vdev_dtl[t];

	ASSERT(t < DTL_TYPES);
	ASSERT(vd != vd->vdev_spa->spa_root_vdev);

	mutex_enter(sm->sm_lock);
	if (!space_map_contains(sm, txg, size))
		space_map_add(sm, txg, size);
	mutex_exit(sm->sm_lock);
}

boolean_t
vdev_dtl_contains(vdev_t *vd, vdev_dtl_type_t t, uint64_t txg, uint64_t size)
{
	space_map_t *sm = &vd->vdev_dtl[t];
	boolean_t dirty = B_FALSE;

	ASSERT(t < DTL_TYPES);
	ASSERT(vd != vd->vdev_spa->spa_root_vdev);

	mutex_enter(sm->sm_lock);
	if (sm->sm_space != 0)
		dirty = space_map_contains(sm, txg, size);
	mutex_exit(sm->sm_lock);

	return (dirty);
}

boolean_t
vdev_dtl_empty(vdev_t *vd, vdev_dtl_type_t t)
{
	space_map_t *sm = &vd->vdev_dtl[t];
	boolean_t empty;

	mutex_enter(sm->sm_lock);
	empty = (sm->sm_space == 0);
	mutex_exit(sm->sm_lock);

	return (empty);
}

/*
 * Reassess DTLs after a config change or scrub completion.
 */
void
vdev_dtl_reassess(vdev_t *vd, uint64_t txg, uint64_t scrub_txg, int scrub_done)
{
	spa_t *spa = vd->vdev_spa;
	avl_tree_t reftree;
	int minref;

	ASSERT(spa_config_held(spa, SCL_ALL, RW_READER) != 0);

	for (int c = 0; c < vd->vdev_children; c++)
		vdev_dtl_reassess(vd->vdev_child[c], txg,
		    scrub_txg, scrub_done);

	if (vd == spa->spa_root_vdev)
		return;

	if (vd->vdev_ops->vdev_op_leaf) {
		mutex_enter(&vd->vdev_dtl_lock);
		if (scrub_txg != 0 &&
		    (spa->spa_scrub_started || spa->spa_scrub_errors == 0)) {
			/* XXX should check scrub_done? */
			/*
			 * We completed a scrub up to scrub_txg.  If we
			 * did it without rebooting, then the scrub dtl
			 * will be valid, so excise the old region and
			 * fold in the scrub dtl.  Otherwise, leave the
			 * dtl as-is if there was an error.
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
			space_map_ref_create(&reftree);
			space_map_ref_add_map(&reftree,
			    &vd->vdev_dtl[DTL_MISSING], 1);
			space_map_ref_add_seg(&reftree, 0, scrub_txg, -1);
			space_map_ref_add_map(&reftree,
			    &vd->vdev_dtl[DTL_SCRUB], 2);
			space_map_ref_generate_map(&reftree,
			    &vd->vdev_dtl[DTL_MISSING], 1);
			space_map_ref_destroy(&reftree);
		}
		space_map_vacate(&vd->vdev_dtl[DTL_PARTIAL], NULL, NULL);
		space_map_walk(&vd->vdev_dtl[DTL_MISSING],
		    space_map_add, &vd->vdev_dtl[DTL_PARTIAL]);
		if (scrub_done)
			space_map_vacate(&vd->vdev_dtl[DTL_SCRUB], NULL, NULL);
		space_map_vacate(&vd->vdev_dtl[DTL_OUTAGE], NULL, NULL);
		if (!vdev_readable(vd))
			space_map_add(&vd->vdev_dtl[DTL_OUTAGE], 0, -1ULL);
		else
			space_map_walk(&vd->vdev_dtl[DTL_MISSING],
			    space_map_add, &vd->vdev_dtl[DTL_OUTAGE]);
		mutex_exit(&vd->vdev_dtl_lock);

		if (txg != 0)
			vdev_dirty(vd->vdev_top, VDD_DTL, vd, txg);
		return;
	}

	mutex_enter(&vd->vdev_dtl_lock);
	for (int t = 0; t < DTL_TYPES; t++) {
		if (t == DTL_SCRUB)
			continue;			/* leaf vdevs only */
		if (t == DTL_PARTIAL)
			minref = 1;			/* i.e. non-zero */
		else if (vd->vdev_nparity != 0)
			minref = vd->vdev_nparity + 1;	/* RAID-Z */
		else
			minref = vd->vdev_children;	/* any kind of mirror */
		space_map_ref_create(&reftree);
		for (int c = 0; c < vd->vdev_children; c++) {
			vdev_t *cvd = vd->vdev_child[c];
			mutex_enter(&cvd->vdev_dtl_lock);
			space_map_ref_add_map(&reftree, &cvd->vdev_dtl[t], 1);
			mutex_exit(&cvd->vdev_dtl_lock);
		}
		space_map_ref_generate_map(&reftree, &vd->vdev_dtl[t], minref);
		space_map_ref_destroy(&reftree);
	}
	mutex_exit(&vd->vdev_dtl_lock);
}

static int
vdev_dtl_load(vdev_t *vd)
{
	spa_t *spa = vd->vdev_spa;
	space_map_obj_t *smo = &vd->vdev_dtl_smo;
	objset_t *mos = spa->spa_meta_objset;
	dmu_buf_t *db;
	int error;

	ASSERT(vd->vdev_children == 0);

	if (smo->smo_object == 0)
		return (0);

	if ((error = dmu_bonus_hold(mos, smo->smo_object, FTAG, &db)) != 0)
		return (error);

	ASSERT3U(db->db_size, >=, sizeof (*smo));
	bcopy(db->db_data, smo, sizeof (*smo));
	dmu_buf_rele(db, FTAG);

	mutex_enter(&vd->vdev_dtl_lock);
	error = space_map_load(&vd->vdev_dtl[DTL_MISSING],
	    NULL, SM_ALLOC, smo, mos);
	mutex_exit(&vd->vdev_dtl_lock);

	return (error);
}

void
vdev_dtl_sync(vdev_t *vd, uint64_t txg)
{
	spa_t *spa = vd->vdev_spa;
	space_map_obj_t *smo = &vd->vdev_dtl_smo;
	space_map_t *sm = &vd->vdev_dtl[DTL_MISSING];
	objset_t *mos = spa->spa_meta_objset;
	space_map_t smsync;
	kmutex_t smlock;
	dmu_buf_t *db;
	dmu_tx_t *tx;

	tx = dmu_tx_create_assigned(spa->spa_dsl_pool, txg);

	if (vd->vdev_detached) {
		if (smo->smo_object != 0) {
			int err = dmu_object_free(mos, smo->smo_object, tx);
			ASSERT3U(err, ==, 0);
			smo->smo_object = 0;
		}
		dmu_tx_commit(tx);
		return;
	}

	if (smo->smo_object == 0) {
		ASSERT(smo->smo_objsize == 0);
		ASSERT(smo->smo_alloc == 0);
		smo->smo_object = dmu_object_alloc(mos,
		    DMU_OT_SPACE_MAP, 1 << SPACE_MAP_BLOCKSHIFT,
		    DMU_OT_SPACE_MAP_HEADER, sizeof (*smo), tx);
		ASSERT(smo->smo_object != 0);
		vdev_config_dirty(vd->vdev_top);
	}

	mutex_init(&smlock, NULL, MUTEX_DEFAULT, NULL);

	space_map_create(&smsync, sm->sm_start, sm->sm_size, sm->sm_shift,
	    &smlock);

	mutex_enter(&smlock);

	mutex_enter(&vd->vdev_dtl_lock);
	space_map_walk(sm, space_map_add, &smsync);
	mutex_exit(&vd->vdev_dtl_lock);

	space_map_truncate(smo, mos, tx);
	space_map_sync(&smsync, SM_ALLOC, smo, mos, tx);

	space_map_destroy(&smsync);

	mutex_exit(&smlock);
	mutex_destroy(&smlock);

	VERIFY(0 == dmu_bonus_hold(mos, smo->smo_object, FTAG, &db));
	dmu_buf_will_dirty(db, tx);
	ASSERT3U(db->db_size, >=, sizeof (*smo));
	bcopy(smo, db->db_data, sizeof (*smo));
	dmu_buf_rele(db, FTAG);

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
	vdev_dtl_reassess(tvd, 0, 0, B_FALSE);
	required = !vdev_dtl_empty(tvd, DTL_OUTAGE);
	vd->vdev_cant_read = cant_read;
	vdev_dtl_reassess(tvd, 0, 0, B_FALSE);

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
		if (vd->vdev_dtl[DTL_MISSING].sm_space != 0 &&
		    vdev_writeable(vd)) {
			space_seg_t *ss;

			ss = avl_first(&vd->vdev_dtl[DTL_MISSING].sm_root);
			thismin = ss->ss_start - 1;
			ss = avl_last(&vd->vdev_dtl[DTL_MISSING].sm_root);
			thismax = ss->ss_end;
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

void
vdev_load(vdev_t *vd)
{
	/*
	 * Recursively load all children.
	 */
	for (int c = 0; c < vd->vdev_children; c++)
		vdev_load(vd->vdev_child[c]);

	/*
	 * If this is a top-level vdev, initialize its metaslabs.
	 */
	if (vd == vd->vdev_top &&
	    (vd->vdev_ashift == 0 || vd->vdev_asize == 0 ||
	    vdev_metaslab_init(vd, 0) != 0))
		vdev_set_state(vd, B_FALSE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_CORRUPT_DATA);

	/*
	 * If this is a leaf vdev, load its DTL.
	 */
	if (vd->vdev_ops->vdev_op_leaf && vdev_dtl_load(vd) != 0)
		vdev_set_state(vd, B_FALSE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_CORRUPT_DATA);
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

	if ((label = vdev_label_read_config(vd)) == NULL) {
		vdev_set_state(vd, B_TRUE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_CORRUPT_DATA);
		return (-1);
	}

	if (nvlist_lookup_uint64(label, ZPOOL_CONFIG_VERSION, &version) != 0 ||
	    version > SPA_VERSION ||
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

void
vdev_sync_done(vdev_t *vd, uint64_t txg)
{
	metaslab_t *msp;

	while (msp = txg_list_remove(&vd->vdev_ms_list, TXG_CLEAN(txg)))
		metaslab_sync_done(msp, txg);
}

void
vdev_sync(vdev_t *vd, uint64_t txg)
{
	spa_t *spa = vd->vdev_spa;
	vdev_t *lvd;
	metaslab_t *msp;
	dmu_tx_t *tx;

	if (vd->vdev_ms_array == 0 && vd->vdev_ms_shift != 0) {
		ASSERT(vd == vd->vdev_top);
		tx = dmu_tx_create_assigned(spa->spa_dsl_pool, txg);
		vd->vdev_ms_array = dmu_object_alloc(spa->spa_meta_objset,
		    DMU_OT_OBJECT_ARRAY, 0, DMU_OT_NONE, 0, tx);
		ASSERT(vd->vdev_ms_array != 0);
		vdev_config_dirty(vd);
		dmu_tx_commit(tx);
	}

	while ((msp = txg_list_remove(&vd->vdev_ms_list, txg)) != NULL) {
		metaslab_sync(msp, txg);
		(void) txg_list_add(&vd->vdev_ms_list, msp, TXG_CLEAN(txg));
	}

	while ((lvd = txg_list_remove(&vd->vdev_dtl_list, txg)) != NULL)
		vdev_dtl_sync(lvd, txg);

	(void) txg_list_add(&spa->spa_vdev_txg_list, vd, TXG_CLEAN(txg));
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
vdev_fault(spa_t *spa, uint64_t guid)
{
	vdev_t *vd;

	spa_vdev_state_enter(spa);

	if ((vd = spa_lookup_by_guid(spa, guid, B_TRUE)) == NULL)
		return (spa_vdev_state_exit(spa, NULL, ENODEV));

	if (!vd->vdev_ops->vdev_op_leaf)
		return (spa_vdev_state_exit(spa, NULL, ENOTSUP));

	/*
	 * Faulted state takes precedence over degraded.
	 */
	vd->vdev_faulted = 1ULL;
	vd->vdev_degraded = 0ULL;
	vdev_set_state(vd, B_FALSE, VDEV_STATE_FAULTED, VDEV_AUX_ERR_EXCEEDED);

	/*
	 * If marking the vdev as faulted cause the top-level vdev to become
	 * unavailable, then back off and simply mark the vdev as degraded
	 * instead.
	 */
	if (vdev_is_dead(vd->vdev_top) && vd->vdev_aux == NULL) {
		vd->vdev_degraded = 1ULL;
		vd->vdev_faulted = 0ULL;

		/*
		 * If we reopen the device and it's not dead, only then do we
		 * mark it degraded.
		 */
		vdev_reopen(vd);

		if (vdev_readable(vd)) {
			vdev_set_state(vd, B_FALSE, VDEV_STATE_DEGRADED,
			    VDEV_AUX_ERR_EXCEEDED);
		}
	}

	return (spa_vdev_state_exit(spa, vd, 0));
}

/*
 * Mark the given vdev degraded.  A degraded vdev is purely an indication to the
 * user that something is wrong.  The vdev continues to operate as normal as far
 * as I/O is concerned.
 */
int
vdev_degrade(spa_t *spa, uint64_t guid)
{
	vdev_t *vd;

	spa_vdev_state_enter(spa);

	if ((vd = spa_lookup_by_guid(spa, guid, B_TRUE)) == NULL)
		return (spa_vdev_state_exit(spa, NULL, ENODEV));

	if (!vd->vdev_ops->vdev_op_leaf)
		return (spa_vdev_state_exit(spa, NULL, ENOTSUP));

	/*
	 * If the vdev is already faulted, then don't do anything.
	 */
	if (vd->vdev_faulted || vd->vdev_degraded)
		return (spa_vdev_state_exit(spa, NULL, 0));

	vd->vdev_degraded = 1ULL;
	if (!vdev_is_dead(vd))
		vdev_set_state(vd, B_FALSE, VDEV_STATE_DEGRADED,
		    VDEV_AUX_ERR_EXCEEDED);

	return (spa_vdev_state_exit(spa, vd, 0));
}

/*
 * Online the given vdev.  If 'unspare' is set, it implies two things.  First,
 * any attached spare device should be detached when the device finishes
 * resilvering.  Second, the online should be treated like a 'test' online case,
 * so no FMA events are generated if the device fails to open.
 */
int
vdev_online(spa_t *spa, uint64_t guid, uint64_t flags, vdev_state_t *newstate)
{
	vdev_t *vd;

	spa_vdev_state_enter(spa);

	if ((vd = spa_lookup_by_guid(spa, guid, B_TRUE)) == NULL)
		return (spa_vdev_state_exit(spa, NULL, ENODEV));

	if (!vd->vdev_ops->vdev_op_leaf)
		return (spa_vdev_state_exit(spa, NULL, ENOTSUP));

	vd->vdev_offline = B_FALSE;
	vd->vdev_tmpoffline = B_FALSE;
	vd->vdev_checkremove = !!(flags & ZFS_ONLINE_CHECKREMOVE);
	vd->vdev_forcefault = !!(flags & ZFS_ONLINE_FORCEFAULT);
	vdev_reopen(vd->vdev_top);
	vd->vdev_checkremove = vd->vdev_forcefault = B_FALSE;

	if (newstate)
		*newstate = vd->vdev_state;
	if ((flags & ZFS_ONLINE_UNSPARE) &&
	    !vdev_is_dead(vd) && vd->vdev_parent &&
	    vd->vdev_parent->vdev_ops == &vdev_spare_ops &&
	    vd->vdev_parent->vdev_child[0] == vd)
		vd->vdev_unspare = B_TRUE;

	return (spa_vdev_state_exit(spa, vd, 0));
}

int
vdev_offline(spa_t *spa, uint64_t guid, uint64_t flags)
{
	vdev_t *vd;

	spa_vdev_state_enter(spa);

	if ((vd = spa_lookup_by_guid(spa, guid, B_TRUE)) == NULL)
		return (spa_vdev_state_exit(spa, NULL, ENODEV));

	if (!vd->vdev_ops->vdev_op_leaf)
		return (spa_vdev_state_exit(spa, NULL, ENOTSUP));

	/*
	 * If the device isn't already offline, try to offline it.
	 */
	if (!vd->vdev_offline) {
		/*
		 * If this device has the only valid copy of some data,
		 * don't allow it to be offlined.
		 */
		if (vd->vdev_aux == NULL && vdev_dtl_required(vd))
			return (spa_vdev_state_exit(spa, NULL, EBUSY));

		/*
		 * Offline this device and reopen its top-level vdev.
		 * If this action results in the top-level vdev becoming
		 * unusable, undo it and fail the request.
		 */
		vd->vdev_offline = B_TRUE;
		vdev_reopen(vd->vdev_top);
		if (vd->vdev_aux == NULL && vdev_is_dead(vd->vdev_top)) {
			vd->vdev_offline = B_FALSE;
			vdev_reopen(vd->vdev_top);
			return (spa_vdev_state_exit(spa, NULL, EBUSY));
		}
	}

	vd->vdev_tmpoffline = !!(flags & ZFS_OFFLINE_TEMPORARY);

	return (spa_vdev_state_exit(spa, vd, 0));
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

	for (int c = 0; c < vd->vdev_children; c++)
		vdev_clear(spa, vd->vdev_child[c]);

	/*
	 * If we're in the FAULTED state or have experienced failed I/O, then
	 * clear the persistent state and attempt to reopen the device.  We
	 * also mark the vdev config dirty, so that the new faulted state is
	 * written out to disk.
	 */
	if (vd->vdev_faulted || vd->vdev_degraded ||
	    !vdev_readable(vd) || !vdev_writeable(vd)) {

		vd->vdev_faulted = vd->vdev_degraded = 0;
		vd->vdev_cant_read = B_FALSE;
		vd->vdev_cant_write = B_FALSE;

		vdev_reopen(vd);

		if (vd != rvd)
			vdev_state_dirty(vd->vdev_top);

		if (vd->vdev_aux == NULL && !vdev_is_dead(vd))
			spa_async_request(spa, SPA_ASYNC_RESILVER);

		spa_event_notify(spa, vd, ESC_ZFS_VDEV_CLEAR);
	}
}

boolean_t
vdev_is_dead(vdev_t *vd)
{
	return (vd->vdev_state < VDEV_STATE_DEGRADED);
}

boolean_t
vdev_readable(vdev_t *vd)
{
	return (!vdev_is_dead(vd) && !vd->vdev_cant_read);
}

boolean_t
vdev_writeable(vdev_t *vd)
{
	return (!vdev_is_dead(vd) && !vd->vdev_cant_write);
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
	    !vd->vdev_cant_write);
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

/*
 * Get statistics for the given vdev.
 */
void
vdev_get_stats(vdev_t *vd, vdev_stat_t *vs)
{
	vdev_t *rvd = vd->vdev_spa->spa_root_vdev;

	mutex_enter(&vd->vdev_stat_lock);
	bcopy(&vd->vdev_stat, vs, sizeof (*vs));
	vs->vs_scrub_errors = vd->vdev_spa->spa_scrub_errors;
	vs->vs_timestamp = gethrtime() - vs->vs_timestamp;
	vs->vs_state = vd->vdev_state;
	vs->vs_rsize = vdev_get_rsize(vd);
	mutex_exit(&vd->vdev_stat_lock);

	/*
	 * If we're getting stats on the root vdev, aggregate the I/O counts
	 * over all top-level vdevs (i.e. the direct children of the root).
	 */
	if (vd == rvd) {
		for (int c = 0; c < rvd->vdev_children; c++) {
			vdev_t *cvd = rvd->vdev_child[c];
			vdev_stat_t *cvs = &cvd->vdev_stat;

			mutex_enter(&vd->vdev_stat_lock);
			for (int t = 0; t < ZIO_TYPES; t++) {
				vs->vs_ops[t] += cvs->vs_ops[t];
				vs->vs_bytes[t] += cvs->vs_bytes[t];
			}
			vs->vs_scrub_examined += cvs->vs_scrub_examined;
			mutex_exit(&vd->vdev_stat_lock);
		}
	}
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
vdev_stat_update(zio_t *zio, uint64_t psize)
{
	spa_t *spa = zio->io_spa;
	vdev_t *rvd = spa->spa_root_vdev;
	vdev_t *vd = zio->io_vd ? zio->io_vd : rvd;
	vdev_t *pvd;
	uint64_t txg = zio->io_txg;
	vdev_stat_t *vs = &vd->vdev_stat;
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
			if (flags & ZIO_FLAG_SCRUB_THREAD)
				vs->vs_scrub_repaired += psize;
			if (flags & ZIO_FLAG_SELF_HEAL)
				vs->vs_self_healed += psize;
		}

		vs->vs_ops[type]++;
		vs->vs_bytes[type] += psize;

		mutex_exit(&vd->vdev_stat_lock);
		return;
	}

	if (flags & ZIO_FLAG_SPECULATIVE)
		return;

	mutex_enter(&vd->vdev_stat_lock);
	if (type == ZIO_TYPE_READ) {
		if (zio->io_error == ECKSUM)
			vs->vs_checksum_errors++;
		else
			vs->vs_read_errors++;
	}
	if (type == ZIO_TYPE_WRITE)
		vs->vs_write_errors++;
	mutex_exit(&vd->vdev_stat_lock);

	if (type == ZIO_TYPE_WRITE && txg != 0 &&
	    (!(flags & ZIO_FLAG_IO_REPAIR) ||
	    (flags & ZIO_FLAG_SCRUB_THREAD))) {
		/*
		 * This is either a normal write (not a repair), or it's a
		 * repair induced by the scrub thread.  In the normal case,
		 * we commit the DTL change in the same txg as the block
		 * was born.  In the scrub-induced repair case, we know that
		 * scrubs run in first-pass syncing context, so we commit
		 * the DTL change in spa->spa_syncing_txg.
		 *
		 * We currently do not make DTL entries for failed spontaneous
		 * self-healing writes triggered by normal (non-scrubbing)
		 * reads, because we have no transactional context in which to
		 * do so -- and it's not clear that it'd be desirable anyway.
		 */
		if (vd->vdev_ops->vdev_op_leaf) {
			uint64_t commit_txg = txg;
			if (flags & ZIO_FLAG_SCRUB_THREAD) {
				ASSERT(flags & ZIO_FLAG_IO_REPAIR);
				ASSERT(spa_sync_pass(spa) == 1);
				vdev_dtl_dirty(vd, DTL_SCRUB, txg, 1);
				commit_txg = spa->spa_syncing_txg;
			}
			ASSERT(commit_txg >= spa->spa_syncing_txg);
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

void
vdev_scrub_stat_update(vdev_t *vd, pool_scrub_type_t type, boolean_t complete)
{
	int c;
	vdev_stat_t *vs = &vd->vdev_stat;

	for (c = 0; c < vd->vdev_children; c++)
		vdev_scrub_stat_update(vd->vdev_child[c], type, complete);

	mutex_enter(&vd->vdev_stat_lock);

	if (type == POOL_SCRUB_NONE) {
		/*
		 * Update completion and end time.  Leave everything else alone
		 * so we can report what happened during the previous scrub.
		 */
		vs->vs_scrub_complete = complete;
		vs->vs_scrub_end = gethrestime_sec();
	} else {
		vs->vs_scrub_type = type;
		vs->vs_scrub_complete = 0;
		vs->vs_scrub_examined = 0;
		vs->vs_scrub_repaired = 0;
		vs->vs_scrub_start = gethrestime_sec();
		vs->vs_scrub_end = 0;
	}

	mutex_exit(&vd->vdev_stat_lock);
}

/*
 * Update the in-core space usage stats for this vdev and the root vdev.
 */
void
vdev_space_update(vdev_t *vd, int64_t space_delta, int64_t alloc_delta,
    boolean_t update_root)
{
	int64_t dspace_delta = space_delta;
	spa_t *spa = vd->vdev_spa;
	vdev_t *rvd = spa->spa_root_vdev;

	ASSERT(vd == vd->vdev_top);

	/*
	 * Apply the inverse of the psize-to-asize (ie. RAID-Z) space-expansion
	 * factor.  We must calculate this here and not at the root vdev
	 * because the root vdev's psize-to-asize is simply the max of its
	 * childrens', thus not accurate enough for us.
	 */
	ASSERT((dspace_delta & (SPA_MINBLOCKSIZE-1)) == 0);
	dspace_delta = (dspace_delta >> SPA_MINBLOCKSHIFT) *
	    vd->vdev_deflate_ratio;

	mutex_enter(&vd->vdev_stat_lock);
	vd->vdev_stat.vs_space += space_delta;
	vd->vdev_stat.vs_alloc += alloc_delta;
	vd->vdev_stat.vs_dspace += dspace_delta;
	mutex_exit(&vd->vdev_stat_lock);

	if (update_root) {
		ASSERT(rvd == vd->vdev_parent);
		ASSERT(vd->vdev_ms_count != 0);

		/*
		 * Don't count non-normal (e.g. intent log) space as part of
		 * the pool's capacity.
		 */
		if (vd->vdev_mg->mg_class != spa->spa_normal_class)
			return;

		mutex_enter(&rvd->vdev_stat_lock);
		rvd->vdev_stat.vs_space += space_delta;
		rvd->vdev_stat.vs_alloc += alloc_delta;
		rvd->vdev_stat.vs_dspace += dspace_delta;
		mutex_exit(&rvd->vdev_stat_lock);
	}
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

	/*
	 * If this is an aux vdev (as with l2cache devices), then we update the
	 * vdev config manually and set the sync flag.
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

		VERIFY(nvlist_lookup_nvlist_array(sav->sav_config,
		    ZPOOL_CONFIG_L2CACHE, &aux, &naux) == 0);

		ASSERT(c < naux);

		/*
		 * Setting the nvlist in the middle if the array is a little
		 * sketchy, but it will work.
		 */
		nvlist_free(aux[c]);
		aux[c] = vdev_config_generate(spa, vd, B_TRUE, B_FALSE, B_TRUE);

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

		if (!list_link_active(&vd->vdev_config_dirty_node))
			list_insert_head(&spa->spa_config_dirty_list, vd);
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

	if (!list_link_active(&vd->vdev_state_dirty_node))
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
	int c;
	vdev_t *child;

	if (vd->vdev_children > 0) {
		for (c = 0; c < vd->vdev_children; c++) {
			child = vd->vdev_child[c];

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
		vd->vdev_stat.vs_aux = aux;
		return;
	}

	save_state = vd->vdev_state;

	vd->vdev_state = state;
	vd->vdev_stat.vs_aux = aux;

	/*
	 * If we are setting the vdev state to anything but an open state, then
	 * always close the underlying device.  Otherwise, we keep accessible
	 * but invalid devices open forever.  We don't call vdev_close() itself,
	 * because that implies some extra checks (offline, etc) that we don't
	 * want here.  This is limited to leaf devices, because otherwise
	 * closing the device will affect other children.
	 */
	if (vdev_is_dead(vd) && vd->vdev_ops->vdev_op_leaf)
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
		/*
		 * Indicate to the ZFS DE that this device has been removed, and
		 * any recent errors should be ignored.
		 */
		zfs_post_remove(spa, vd);
		vd->vdev_removed = B_TRUE;
	} else if (state == VDEV_STATE_CANT_OPEN) {
		/*
		 * If we fail to open a vdev during an import, we mark it as
		 * "not available", which signifies that it was never there to
		 * begin with.  Failure to open such a device is not considered
		 * an error.
		 */
		if (spa->spa_load_state == SPA_LOAD_IMPORT &&
		    !spa->spa_import_faulted &&
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
			case VDEV_AUX_IO_FAILURE:
				class = FM_EREPORT_ZFS_IO_FAILURE;
				break;
			default:
				class = FM_EREPORT_ZFS_DEVICE_UNKNOWN;
			}

			zfs_ereport_post(class, spa, vd, NULL, save_state, 0);
		}

		/* Erase any notion of persistent removed state */
		vd->vdev_removed = B_FALSE;
	} else {
		vd->vdev_removed = B_FALSE;
	}

	if (!isopen)
		vdev_propagate_state(vd);
}

/*
 * Check the vdev configuration to ensure that it's capable of supporting
 * a root pool. Currently, we do not support RAID-Z or partial configuration.
 * In addition, only a single top-level vdev is allowed and none of the leaves
 * can be wholedisks.
 */
boolean_t
vdev_is_bootable(vdev_t *vd)
{
	int c;

	if (!vd->vdev_ops->vdev_op_leaf) {
		char *vdev_type = vd->vdev_ops->vdev_op_type;

		if (strcmp(vdev_type, VDEV_TYPE_ROOT) == 0 &&
		    vd->vdev_children > 1) {
			return (B_FALSE);
		} else if (strcmp(vdev_type, VDEV_TYPE_RAIDZ) == 0 ||
		    strcmp(vdev_type, VDEV_TYPE_MISSING) == 0) {
			return (B_FALSE);
		}
	} else if (vd->vdev_wholedisk == 1) {
		return (B_FALSE);
	}

	for (c = 0; c < vd->vdev_children; c++) {
		if (!vdev_is_bootable(vd->vdev_child[c]))
			return (B_FALSE);
	}
	return (B_TRUE);
}
