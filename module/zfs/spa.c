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

/*
 * This file contains all the routines used when modifying on-disk SPA state.
 * This includes opening, importing, destroying, exporting a pool, and syncing a
 * pool.
 */

#include <sys/zfs_context.h>
#include <sys/fm/fs/zfs.h>
#include <sys/spa_impl.h>
#include <sys/zio.h>
#include <sys/zio_checksum.h>
#include <sys/zio_compress.h>
#include <sys/dmu.h>
#include <sys/dmu_tx.h>
#include <sys/zap.h>
#include <sys/zil.h>
#include <sys/vdev_impl.h>
#include <sys/metaslab.h>
#include <sys/uberblock_impl.h>
#include <sys/txg.h>
#include <sys/avl.h>
#include <sys/dmu_traverse.h>
#include <sys/dmu_objset.h>
#include <sys/unique.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_synctask.h>
#include <sys/fs/zfs.h>
#include <sys/arc.h>
#include <sys/callb.h>
#include <sys/systeminfo.h>
#include <sys/sunddi.h>
#include <sys/spa_boot.h>

#ifdef	_KERNEL
#include <sys/zone.h>
#endif	/* _KERNEL */

#include "zfs_prop.h"
#include "zfs_comutil.h"

int zio_taskq_threads[ZIO_TYPES][ZIO_TASKQ_TYPES] = {
	/*	ISSUE	INTR					*/
	{	1,	1	},	/* ZIO_TYPE_NULL	*/
	{	1,	8	},	/* ZIO_TYPE_READ	*/
	{	8,	1	},	/* ZIO_TYPE_WRITE	*/
	{	1,	1	},	/* ZIO_TYPE_FREE	*/
	{	1,	1	},	/* ZIO_TYPE_CLAIM	*/
	{	1,	1	},	/* ZIO_TYPE_IOCTL	*/
};

static void spa_sync_props(void *arg1, void *arg2, cred_t *cr, dmu_tx_t *tx);
static boolean_t spa_has_active_shared_spare(spa_t *spa);

/*
 * ==========================================================================
 * SPA properties routines
 * ==========================================================================
 */

/*
 * Add a (source=src, propname=propval) list to an nvlist.
 */
static void
spa_prop_add_list(nvlist_t *nvl, zpool_prop_t prop, char *strval,
    uint64_t intval, zprop_source_t src)
{
	const char *propname = zpool_prop_to_name(prop);
	nvlist_t *propval;

	VERIFY(nvlist_alloc(&propval, NV_UNIQUE_NAME, KM_SLEEP) == 0);
	VERIFY(nvlist_add_uint64(propval, ZPROP_SOURCE, src) == 0);

	if (strval != NULL)
		VERIFY(nvlist_add_string(propval, ZPROP_VALUE, strval) == 0);
	else
		VERIFY(nvlist_add_uint64(propval, ZPROP_VALUE, intval) == 0);

	VERIFY(nvlist_add_nvlist(nvl, propname, propval) == 0);
	nvlist_free(propval);
}

/*
 * Get property values from the spa configuration.
 */
static void
spa_prop_get_config(spa_t *spa, nvlist_t **nvp)
{
	uint64_t size;
	uint64_t used;
	uint64_t cap, version;
	zprop_source_t src = ZPROP_SRC_NONE;
	spa_config_dirent_t *dp;

	ASSERT(MUTEX_HELD(&spa->spa_props_lock));

	if (spa->spa_root_vdev != NULL) {
		size = spa_get_space(spa);
		used = spa_get_alloc(spa);
		spa_prop_add_list(*nvp, ZPOOL_PROP_NAME, spa_name(spa), 0, src);
		spa_prop_add_list(*nvp, ZPOOL_PROP_SIZE, NULL, size, src);
		spa_prop_add_list(*nvp, ZPOOL_PROP_USED, NULL, used, src);
		spa_prop_add_list(*nvp, ZPOOL_PROP_AVAILABLE, NULL,
		    size - used, src);

		cap = (size == 0) ? 0 : (used * 100 / size);
		spa_prop_add_list(*nvp, ZPOOL_PROP_CAPACITY, NULL, cap, src);

		spa_prop_add_list(*nvp, ZPOOL_PROP_HEALTH, NULL,
		    spa->spa_root_vdev->vdev_state, src);

		version = spa_version(spa);
		if (version == zpool_prop_default_numeric(ZPOOL_PROP_VERSION))
			src = ZPROP_SRC_DEFAULT;
		else
			src = ZPROP_SRC_LOCAL;
		spa_prop_add_list(*nvp, ZPOOL_PROP_VERSION, NULL, version, src);
	}

	spa_prop_add_list(*nvp, ZPOOL_PROP_GUID, NULL, spa_guid(spa), src);

	if (spa->spa_root != NULL)
		spa_prop_add_list(*nvp, ZPOOL_PROP_ALTROOT, spa->spa_root,
		    0, ZPROP_SRC_LOCAL);

	if ((dp = list_head(&spa->spa_config_list)) != NULL) {
		if (dp->scd_path == NULL) {
			spa_prop_add_list(*nvp, ZPOOL_PROP_CACHEFILE,
			    "none", 0, ZPROP_SRC_LOCAL);
		} else if (strcmp(dp->scd_path, spa_config_path) != 0) {
			spa_prop_add_list(*nvp, ZPOOL_PROP_CACHEFILE,
			    dp->scd_path, 0, ZPROP_SRC_LOCAL);
		}
	}
}

/*
 * Get zpool property values.
 */
int
spa_prop_get(spa_t *spa, nvlist_t **nvp)
{
	zap_cursor_t zc;
	zap_attribute_t za;
	objset_t *mos = spa->spa_meta_objset;
	int err;

	VERIFY(nvlist_alloc(nvp, NV_UNIQUE_NAME, KM_SLEEP) == 0);

	mutex_enter(&spa->spa_props_lock);

	/*
	 * Get properties from the spa config.
	 */
	spa_prop_get_config(spa, nvp);

	/* If no pool property object, no more prop to get. */
	if (spa->spa_pool_props_object == 0) {
		mutex_exit(&spa->spa_props_lock);
		return (0);
	}

	/*
	 * Get properties from the MOS pool property object.
	 */
	for (zap_cursor_init(&zc, mos, spa->spa_pool_props_object);
	    (err = zap_cursor_retrieve(&zc, &za)) == 0;
	    zap_cursor_advance(&zc)) {
		uint64_t intval = 0;
		char *strval = NULL;
		zprop_source_t src = ZPROP_SRC_DEFAULT;
		zpool_prop_t prop;

		if ((prop = zpool_name_to_prop(za.za_name)) == ZPROP_INVAL)
			continue;

		switch (za.za_integer_length) {
		case 8:
			/* integer property */
			if (za.za_first_integer !=
			    zpool_prop_default_numeric(prop))
				src = ZPROP_SRC_LOCAL;

			if (prop == ZPOOL_PROP_BOOTFS) {
				dsl_pool_t *dp;
				dsl_dataset_t *ds = NULL;

				dp = spa_get_dsl(spa);
				rw_enter(&dp->dp_config_rwlock, RW_READER);
				if (err = dsl_dataset_hold_obj(dp,
				    za.za_first_integer, FTAG, &ds)) {
					rw_exit(&dp->dp_config_rwlock);
					break;
				}

				strval = kmem_alloc(
				    MAXNAMELEN + strlen(MOS_DIR_NAME) + 1,
				    KM_SLEEP);
				dsl_dataset_name(ds, strval);
				dsl_dataset_rele(ds, FTAG);
				rw_exit(&dp->dp_config_rwlock);
			} else {
				strval = NULL;
				intval = za.za_first_integer;
			}

			spa_prop_add_list(*nvp, prop, strval, intval, src);

			if (strval != NULL)
				kmem_free(strval,
				    MAXNAMELEN + strlen(MOS_DIR_NAME) + 1);

			break;

		case 1:
			/* string property */
			strval = kmem_alloc(za.za_num_integers, KM_SLEEP);
			err = zap_lookup(mos, spa->spa_pool_props_object,
			    za.za_name, 1, za.za_num_integers, strval);
			if (err) {
				kmem_free(strval, za.za_num_integers);
				break;
			}
			spa_prop_add_list(*nvp, prop, strval, 0, src);
			kmem_free(strval, za.za_num_integers);
			break;

		default:
			break;
		}
	}
	zap_cursor_fini(&zc);
	mutex_exit(&spa->spa_props_lock);
out:
	if (err && err != ENOENT) {
		nvlist_free(*nvp);
		*nvp = NULL;
		return (err);
	}

	return (0);
}

/*
 * Validate the given pool properties nvlist and modify the list
 * for the property values to be set.
 */
static int
spa_prop_validate(spa_t *spa, nvlist_t *props)
{
	nvpair_t *elem;
	int error = 0, reset_bootfs = 0;
	uint64_t objnum;

	elem = NULL;
	while ((elem = nvlist_next_nvpair(props, elem)) != NULL) {
		zpool_prop_t prop;
		char *propname, *strval;
		uint64_t intval;
		objset_t *os;
		char *slash;

		propname = nvpair_name(elem);

		if ((prop = zpool_name_to_prop(propname)) == ZPROP_INVAL)
			return (EINVAL);

		switch (prop) {
		case ZPOOL_PROP_VERSION:
			error = nvpair_value_uint64(elem, &intval);
			if (!error &&
			    (intval < spa_version(spa) || intval > SPA_VERSION))
				error = EINVAL;
			break;

		case ZPOOL_PROP_DELEGATION:
		case ZPOOL_PROP_AUTOREPLACE:
		case ZPOOL_PROP_LISTSNAPS:
			error = nvpair_value_uint64(elem, &intval);
			if (!error && intval > 1)
				error = EINVAL;
			break;

		case ZPOOL_PROP_BOOTFS:
			if (spa_version(spa) < SPA_VERSION_BOOTFS) {
				error = ENOTSUP;
				break;
			}

			/*
			 * Make sure the vdev config is bootable
			 */
			if (!vdev_is_bootable(spa->spa_root_vdev)) {
				error = ENOTSUP;
				break;
			}

			reset_bootfs = 1;

			error = nvpair_value_string(elem, &strval);

			if (!error) {
				uint64_t compress;

				if (strval == NULL || strval[0] == '\0') {
					objnum = zpool_prop_default_numeric(
					    ZPOOL_PROP_BOOTFS);
					break;
				}

				if (error = dmu_objset_open(strval, DMU_OST_ZFS,
				    DS_MODE_USER | DS_MODE_READONLY, &os))
					break;

				/* We don't support gzip bootable datasets */
				if ((error = dsl_prop_get_integer(strval,
				    zfs_prop_to_name(ZFS_PROP_COMPRESSION),
				    &compress, NULL)) == 0 &&
				    !BOOTFS_COMPRESS_VALID(compress)) {
					error = ENOTSUP;
				} else {
					objnum = dmu_objset_id(os);
				}
				dmu_objset_close(os);
			}
			break;

		case ZPOOL_PROP_FAILUREMODE:
			error = nvpair_value_uint64(elem, &intval);
			if (!error && (intval < ZIO_FAILURE_MODE_WAIT ||
			    intval > ZIO_FAILURE_MODE_PANIC))
				error = EINVAL;

			/*
			 * This is a special case which only occurs when
			 * the pool has completely failed. This allows
			 * the user to change the in-core failmode property
			 * without syncing it out to disk (I/Os might
			 * currently be blocked). We do this by returning
			 * EIO to the caller (spa_prop_set) to trick it
			 * into thinking we encountered a property validation
			 * error.
			 */
			if (!error && spa_suspended(spa)) {
				spa->spa_failmode = intval;
				error = EIO;
			}
			break;

		case ZPOOL_PROP_CACHEFILE:
			if ((error = nvpair_value_string(elem, &strval)) != 0)
				break;

			if (strval[0] == '\0')
				break;

			if (strcmp(strval, "none") == 0)
				break;

			if (strval[0] != '/') {
				error = EINVAL;
				break;
			}

			slash = strrchr(strval, '/');
			ASSERT(slash != NULL);

			if (slash[1] == '\0' || strcmp(slash, "/.") == 0 ||
			    strcmp(slash, "/..") == 0)
				error = EINVAL;
			break;
		}

		if (error)
			break;
	}

	if (!error && reset_bootfs) {
		error = nvlist_remove(props,
		    zpool_prop_to_name(ZPOOL_PROP_BOOTFS), DATA_TYPE_STRING);

		if (!error) {
			error = nvlist_add_uint64(props,
			    zpool_prop_to_name(ZPOOL_PROP_BOOTFS), objnum);
		}
	}

	return (error);
}

void
spa_configfile_set(spa_t *spa, nvlist_t *nvp, boolean_t need_sync)
{
	char *cachefile;
	spa_config_dirent_t *dp;

	if (nvlist_lookup_string(nvp, zpool_prop_to_name(ZPOOL_PROP_CACHEFILE),
	    &cachefile) != 0)
		return;

	dp = kmem_alloc(sizeof (spa_config_dirent_t),
	    KM_SLEEP);

	if (cachefile[0] == '\0')
		dp->scd_path = spa_strdup(spa_config_path);
	else if (strcmp(cachefile, "none") == 0)
		dp->scd_path = NULL;
	else
		dp->scd_path = spa_strdup(cachefile);

	list_insert_head(&spa->spa_config_list, dp);
	if (need_sync)
		spa_async_request(spa, SPA_ASYNC_CONFIG_UPDATE);
}

int
spa_prop_set(spa_t *spa, nvlist_t *nvp)
{
	int error;
	nvpair_t *elem;
	boolean_t need_sync = B_FALSE;
	zpool_prop_t prop;

	if ((error = spa_prop_validate(spa, nvp)) != 0)
		return (error);

	elem = NULL;
	while ((elem = nvlist_next_nvpair(nvp, elem)) != NULL) {
		if ((prop = zpool_name_to_prop(
		    nvpair_name(elem))) == ZPROP_INVAL)
			return (EINVAL);

		if (prop == ZPOOL_PROP_CACHEFILE || prop == ZPOOL_PROP_ALTROOT)
			continue;

		need_sync = B_TRUE;
		break;
	}

	if (need_sync)
		return (dsl_sync_task_do(spa_get_dsl(spa), NULL, spa_sync_props,
		    spa, nvp, 3));
	else
		return (0);
}

/*
 * If the bootfs property value is dsobj, clear it.
 */
void
spa_prop_clear_bootfs(spa_t *spa, uint64_t dsobj, dmu_tx_t *tx)
{
	if (spa->spa_bootfs == dsobj && spa->spa_pool_props_object != 0) {
		VERIFY(zap_remove(spa->spa_meta_objset,
		    spa->spa_pool_props_object,
		    zpool_prop_to_name(ZPOOL_PROP_BOOTFS), tx) == 0);
		spa->spa_bootfs = 0;
	}
}

/*
 * ==========================================================================
 * SPA state manipulation (open/create/destroy/import/export)
 * ==========================================================================
 */

static int
spa_error_entry_compare(const void *a, const void *b)
{
	spa_error_entry_t *sa = (spa_error_entry_t *)a;
	spa_error_entry_t *sb = (spa_error_entry_t *)b;
	int ret;

	ret = bcmp(&sa->se_bookmark, &sb->se_bookmark,
	    sizeof (zbookmark_t));

	if (ret < 0)
		return (-1);
	else if (ret > 0)
		return (1);
	else
		return (0);
}

/*
 * Utility function which retrieves copies of the current logs and
 * re-initializes them in the process.
 */
void
spa_get_errlists(spa_t *spa, avl_tree_t *last, avl_tree_t *scrub)
{
	ASSERT(MUTEX_HELD(&spa->spa_errlist_lock));

	bcopy(&spa->spa_errlist_last, last, sizeof (avl_tree_t));
	bcopy(&spa->spa_errlist_scrub, scrub, sizeof (avl_tree_t));

	avl_create(&spa->spa_errlist_scrub,
	    spa_error_entry_compare, sizeof (spa_error_entry_t),
	    offsetof(spa_error_entry_t, se_avl));
	avl_create(&spa->spa_errlist_last,
	    spa_error_entry_compare, sizeof (spa_error_entry_t),
	    offsetof(spa_error_entry_t, se_avl));
}

/*
 * Activate an uninitialized pool.
 */
static void
spa_activate(spa_t *spa, int mode)
{
	ASSERT(spa->spa_state == POOL_STATE_UNINITIALIZED);

	spa->spa_state = POOL_STATE_ACTIVE;
	spa->spa_mode = mode;

	spa->spa_normal_class = metaslab_class_create();
	spa->spa_log_class = metaslab_class_create();

	for (int t = 0; t < ZIO_TYPES; t++) {
		for (int q = 0; q < ZIO_TASKQ_TYPES; q++) {
			spa->spa_zio_taskq[t][q] = taskq_create("spa_zio",
			    zio_taskq_threads[t][q], maxclsyspri, 50,
			    INT_MAX, TASKQ_PREPOPULATE);
		}
	}

	list_create(&spa->spa_config_dirty_list, sizeof (vdev_t),
	    offsetof(vdev_t, vdev_config_dirty_node));
	list_create(&spa->spa_state_dirty_list, sizeof (vdev_t),
	    offsetof(vdev_t, vdev_state_dirty_node));

	txg_list_create(&spa->spa_vdev_txg_list,
	    offsetof(struct vdev, vdev_txg_node));

	avl_create(&spa->spa_errlist_scrub,
	    spa_error_entry_compare, sizeof (spa_error_entry_t),
	    offsetof(spa_error_entry_t, se_avl));
	avl_create(&spa->spa_errlist_last,
	    spa_error_entry_compare, sizeof (spa_error_entry_t),
	    offsetof(spa_error_entry_t, se_avl));
}

/*
 * Opposite of spa_activate().
 */
static void
spa_deactivate(spa_t *spa)
{
	ASSERT(spa->spa_sync_on == B_FALSE);
	ASSERT(spa->spa_dsl_pool == NULL);
	ASSERT(spa->spa_root_vdev == NULL);

	ASSERT(spa->spa_state != POOL_STATE_UNINITIALIZED);

	txg_list_destroy(&spa->spa_vdev_txg_list);

	list_destroy(&spa->spa_config_dirty_list);
	list_destroy(&spa->spa_state_dirty_list);

	for (int t = 0; t < ZIO_TYPES; t++) {
		for (int q = 0; q < ZIO_TASKQ_TYPES; q++) {
			taskq_destroy(spa->spa_zio_taskq[t][q]);
			spa->spa_zio_taskq[t][q] = NULL;
		}
	}

	metaslab_class_destroy(spa->spa_normal_class);
	spa->spa_normal_class = NULL;

	metaslab_class_destroy(spa->spa_log_class);
	spa->spa_log_class = NULL;

	/*
	 * If this was part of an import or the open otherwise failed, we may
	 * still have errors left in the queues.  Empty them just in case.
	 */
	spa_errlog_drain(spa);

	avl_destroy(&spa->spa_errlist_scrub);
	avl_destroy(&spa->spa_errlist_last);

	spa->spa_state = POOL_STATE_UNINITIALIZED;
}

/*
 * Verify a pool configuration, and construct the vdev tree appropriately.  This
 * will create all the necessary vdevs in the appropriate layout, with each vdev
 * in the CLOSED state.  This will prep the pool before open/creation/import.
 * All vdev validation is done by the vdev_alloc() routine.
 */
static int
spa_config_parse(spa_t *spa, vdev_t **vdp, nvlist_t *nv, vdev_t *parent,
    uint_t id, int atype)
{
	nvlist_t **child;
	uint_t c, children;
	int error;

	if ((error = vdev_alloc(spa, vdp, nv, parent, id, atype)) != 0)
		return (error);

	if ((*vdp)->vdev_ops->vdev_op_leaf)
		return (0);

	error = nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children);

	if (error == ENOENT)
		return (0);

	if (error) {
		vdev_free(*vdp);
		*vdp = NULL;
		return (EINVAL);
	}

	for (c = 0; c < children; c++) {
		vdev_t *vd;
		if ((error = spa_config_parse(spa, &vd, child[c], *vdp, c,
		    atype)) != 0) {
			vdev_free(*vdp);
			*vdp = NULL;
			return (error);
		}
	}

	ASSERT(*vdp != NULL);

	return (0);
}

/*
 * Opposite of spa_load().
 */
static void
spa_unload(spa_t *spa)
{
	int i;

	ASSERT(MUTEX_HELD(&spa_namespace_lock));

	/*
	 * Stop async tasks.
	 */
	spa_async_suspend(spa);

	/*
	 * Stop syncing.
	 */
	if (spa->spa_sync_on) {
		txg_sync_stop(spa->spa_dsl_pool);
		spa->spa_sync_on = B_FALSE;
	}

	/*
	 * Wait for any outstanding async I/O to complete.
	 */
	mutex_enter(&spa->spa_async_root_lock);
	while (spa->spa_async_root_count != 0)
		cv_wait(&spa->spa_async_root_cv, &spa->spa_async_root_lock);
	mutex_exit(&spa->spa_async_root_lock);

	/*
	 * Close the dsl pool.
	 */
	if (spa->spa_dsl_pool) {
		dsl_pool_close(spa->spa_dsl_pool);
		spa->spa_dsl_pool = NULL;
	}

	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);

	/*
	 * Drop and purge level 2 cache
	 */
	spa_l2cache_drop(spa);

	/*
	 * Close all vdevs.
	 */
	if (spa->spa_root_vdev)
		vdev_free(spa->spa_root_vdev);
	ASSERT(spa->spa_root_vdev == NULL);

	for (i = 0; i < spa->spa_spares.sav_count; i++)
		vdev_free(spa->spa_spares.sav_vdevs[i]);
	if (spa->spa_spares.sav_vdevs) {
		kmem_free(spa->spa_spares.sav_vdevs,
		    spa->spa_spares.sav_count * sizeof (void *));
		spa->spa_spares.sav_vdevs = NULL;
	}
	if (spa->spa_spares.sav_config) {
		nvlist_free(spa->spa_spares.sav_config);
		spa->spa_spares.sav_config = NULL;
	}
	spa->spa_spares.sav_count = 0;

	for (i = 0; i < spa->spa_l2cache.sav_count; i++)
		vdev_free(spa->spa_l2cache.sav_vdevs[i]);
	if (spa->spa_l2cache.sav_vdevs) {
		kmem_free(spa->spa_l2cache.sav_vdevs,
		    spa->spa_l2cache.sav_count * sizeof (void *));
		spa->spa_l2cache.sav_vdevs = NULL;
	}
	if (spa->spa_l2cache.sav_config) {
		nvlist_free(spa->spa_l2cache.sav_config);
		spa->spa_l2cache.sav_config = NULL;
	}
	spa->spa_l2cache.sav_count = 0;

	spa->spa_async_suspended = 0;

	spa_config_exit(spa, SCL_ALL, FTAG);
}

/*
 * Load (or re-load) the current list of vdevs describing the active spares for
 * this pool.  When this is called, we have some form of basic information in
 * 'spa_spares.sav_config'.  We parse this into vdevs, try to open them, and
 * then re-generate a more complete list including status information.
 */
static void
spa_load_spares(spa_t *spa)
{
	nvlist_t **spares;
	uint_t nspares;
	int i;
	vdev_t *vd, *tvd;

	ASSERT(spa_config_held(spa, SCL_ALL, RW_WRITER) == SCL_ALL);

	/*
	 * First, close and free any existing spare vdevs.
	 */
	for (i = 0; i < spa->spa_spares.sav_count; i++) {
		vd = spa->spa_spares.sav_vdevs[i];

		/* Undo the call to spa_activate() below */
		if ((tvd = spa_lookup_by_guid(spa, vd->vdev_guid,
		    B_FALSE)) != NULL && tvd->vdev_isspare)
			spa_spare_remove(tvd);
		vdev_close(vd);
		vdev_free(vd);
	}

	if (spa->spa_spares.sav_vdevs)
		kmem_free(spa->spa_spares.sav_vdevs,
		    spa->spa_spares.sav_count * sizeof (void *));

	if (spa->spa_spares.sav_config == NULL)
		nspares = 0;
	else
		VERIFY(nvlist_lookup_nvlist_array(spa->spa_spares.sav_config,
		    ZPOOL_CONFIG_SPARES, &spares, &nspares) == 0);

	spa->spa_spares.sav_count = (int)nspares;
	spa->spa_spares.sav_vdevs = NULL;

	if (nspares == 0)
		return;

	/*
	 * Construct the array of vdevs, opening them to get status in the
	 * process.   For each spare, there is potentially two different vdev_t
	 * structures associated with it: one in the list of spares (used only
	 * for basic validation purposes) and one in the active vdev
	 * configuration (if it's spared in).  During this phase we open and
	 * validate each vdev on the spare list.  If the vdev also exists in the
	 * active configuration, then we also mark this vdev as an active spare.
	 */
	spa->spa_spares.sav_vdevs = kmem_alloc(nspares * sizeof (void *),
	    KM_SLEEP);
	for (i = 0; i < spa->spa_spares.sav_count; i++) {
		VERIFY(spa_config_parse(spa, &vd, spares[i], NULL, 0,
		    VDEV_ALLOC_SPARE) == 0);
		ASSERT(vd != NULL);

		spa->spa_spares.sav_vdevs[i] = vd;

		if ((tvd = spa_lookup_by_guid(spa, vd->vdev_guid,
		    B_FALSE)) != NULL) {
			if (!tvd->vdev_isspare)
				spa_spare_add(tvd);

			/*
			 * We only mark the spare active if we were successfully
			 * able to load the vdev.  Otherwise, importing a pool
			 * with a bad active spare would result in strange
			 * behavior, because multiple pool would think the spare
			 * is actively in use.
			 *
			 * There is a vulnerability here to an equally bizarre
			 * circumstance, where a dead active spare is later
			 * brought back to life (onlined or otherwise).  Given
			 * the rarity of this scenario, and the extra complexity
			 * it adds, we ignore the possibility.
			 */
			if (!vdev_is_dead(tvd))
				spa_spare_activate(tvd);
		}

		vd->vdev_top = vd;

		if (vdev_open(vd) != 0)
			continue;

		if (vdev_validate_aux(vd) == 0)
			spa_spare_add(vd);
	}

	/*
	 * Recompute the stashed list of spares, with status information
	 * this time.
	 */
	VERIFY(nvlist_remove(spa->spa_spares.sav_config, ZPOOL_CONFIG_SPARES,
	    DATA_TYPE_NVLIST_ARRAY) == 0);

	spares = kmem_alloc(spa->spa_spares.sav_count * sizeof (void *),
	    KM_SLEEP);
	for (i = 0; i < spa->spa_spares.sav_count; i++)
		spares[i] = vdev_config_generate(spa,
		    spa->spa_spares.sav_vdevs[i], B_TRUE, B_TRUE, B_FALSE);
	VERIFY(nvlist_add_nvlist_array(spa->spa_spares.sav_config,
	    ZPOOL_CONFIG_SPARES, spares, spa->spa_spares.sav_count) == 0);
	for (i = 0; i < spa->spa_spares.sav_count; i++)
		nvlist_free(spares[i]);
	kmem_free(spares, spa->spa_spares.sav_count * sizeof (void *));
}

/*
 * Load (or re-load) the current list of vdevs describing the active l2cache for
 * this pool.  When this is called, we have some form of basic information in
 * 'spa_l2cache.sav_config'.  We parse this into vdevs, try to open them, and
 * then re-generate a more complete list including status information.
 * Devices which are already active have their details maintained, and are
 * not re-opened.
 */
static void
spa_load_l2cache(spa_t *spa)
{
	nvlist_t **l2cache;
	uint_t nl2cache;
	int i, j, oldnvdevs;
	uint64_t guid, size;
	vdev_t *vd, **oldvdevs, **newvdevs;
	spa_aux_vdev_t *sav = &spa->spa_l2cache;

	ASSERT(spa_config_held(spa, SCL_ALL, RW_WRITER) == SCL_ALL);

	if (sav->sav_config != NULL) {
		VERIFY(nvlist_lookup_nvlist_array(sav->sav_config,
		    ZPOOL_CONFIG_L2CACHE, &l2cache, &nl2cache) == 0);
		newvdevs = kmem_alloc(nl2cache * sizeof (void *), KM_SLEEP);
	} else {
		nl2cache = 0;
	}

	oldvdevs = sav->sav_vdevs;
	oldnvdevs = sav->sav_count;
	sav->sav_vdevs = NULL;
	sav->sav_count = 0;

	/*
	 * Process new nvlist of vdevs.
	 */
	for (i = 0; i < nl2cache; i++) {
		VERIFY(nvlist_lookup_uint64(l2cache[i], ZPOOL_CONFIG_GUID,
		    &guid) == 0);

		newvdevs[i] = NULL;
		for (j = 0; j < oldnvdevs; j++) {
			vd = oldvdevs[j];
			if (vd != NULL && guid == vd->vdev_guid) {
				/*
				 * Retain previous vdev for add/remove ops.
				 */
				newvdevs[i] = vd;
				oldvdevs[j] = NULL;
				break;
			}
		}

		if (newvdevs[i] == NULL) {
			/*
			 * Create new vdev
			 */
			VERIFY(spa_config_parse(spa, &vd, l2cache[i], NULL, 0,
			    VDEV_ALLOC_L2CACHE) == 0);
			ASSERT(vd != NULL);
			newvdevs[i] = vd;

			/*
			 * Commit this vdev as an l2cache device,
			 * even if it fails to open.
			 */
			spa_l2cache_add(vd);

			vd->vdev_top = vd;
			vd->vdev_aux = sav;

			spa_l2cache_activate(vd);

			if (vdev_open(vd) != 0)
				continue;

			(void) vdev_validate_aux(vd);

			if (!vdev_is_dead(vd)) {
				size = vdev_get_rsize(vd);
				l2arc_add_vdev(spa, vd,
				    VDEV_LABEL_START_SIZE,
				    size - VDEV_LABEL_START_SIZE);
			}
		}
	}

	/*
	 * Purge vdevs that were dropped
	 */
	for (i = 0; i < oldnvdevs; i++) {
		uint64_t pool;

		vd = oldvdevs[i];
		if (vd != NULL) {
			if (spa_l2cache_exists(vd->vdev_guid, &pool) &&
			    pool != 0ULL && l2arc_vdev_present(vd))
				l2arc_remove_vdev(vd);
			(void) vdev_close(vd);
			spa_l2cache_remove(vd);
		}
	}

	if (oldvdevs)
		kmem_free(oldvdevs, oldnvdevs * sizeof (void *));

	if (sav->sav_config == NULL)
		goto out;

	sav->sav_vdevs = newvdevs;
	sav->sav_count = (int)nl2cache;

	/*
	 * Recompute the stashed list of l2cache devices, with status
	 * information this time.
	 */
	VERIFY(nvlist_remove(sav->sav_config, ZPOOL_CONFIG_L2CACHE,
	    DATA_TYPE_NVLIST_ARRAY) == 0);

	l2cache = kmem_alloc(sav->sav_count * sizeof (void *), KM_SLEEP);
	for (i = 0; i < sav->sav_count; i++)
		l2cache[i] = vdev_config_generate(spa,
		    sav->sav_vdevs[i], B_TRUE, B_FALSE, B_TRUE);
	VERIFY(nvlist_add_nvlist_array(sav->sav_config,
	    ZPOOL_CONFIG_L2CACHE, l2cache, sav->sav_count) == 0);
out:
	for (i = 0; i < sav->sav_count; i++)
		nvlist_free(l2cache[i]);
	if (sav->sav_count)
		kmem_free(l2cache, sav->sav_count * sizeof (void *));
}

static int
load_nvlist(spa_t *spa, uint64_t obj, nvlist_t **value)
{
	dmu_buf_t *db;
	char *packed = NULL;
	size_t nvsize = 0;
	int error;
	*value = NULL;

	VERIFY(0 == dmu_bonus_hold(spa->spa_meta_objset, obj, FTAG, &db));
	nvsize = *(uint64_t *)db->db_data;
	dmu_buf_rele(db, FTAG);

	packed = kmem_alloc(nvsize, KM_SLEEP);
	error = dmu_read(spa->spa_meta_objset, obj, 0, nvsize, packed);
	if (error == 0)
		error = nvlist_unpack(packed, nvsize, value, 0);
	kmem_free(packed, nvsize);

	return (error);
}

/*
 * Checks to see if the given vdev could not be opened, in which case we post a
 * sysevent to notify the autoreplace code that the device has been removed.
 */
static void
spa_check_removed(vdev_t *vd)
{
	int c;

	for (c = 0; c < vd->vdev_children; c++)
		spa_check_removed(vd->vdev_child[c]);

	if (vd->vdev_ops->vdev_op_leaf && vdev_is_dead(vd)) {
		zfs_post_autoreplace(vd->vdev_spa, vd);
		spa_event_notify(vd->vdev_spa, vd, ESC_ZFS_VDEV_CHECK);
	}
}

/*
 * Check for missing log devices
 */
int
spa_check_logs(spa_t *spa)
{
	switch (spa->spa_log_state) {
	case SPA_LOG_MISSING:
		/* need to recheck in case slog has been restored */
	case SPA_LOG_UNKNOWN:
		if (dmu_objset_find(spa->spa_name, zil_check_log_chain, NULL,
		    DS_FIND_CHILDREN)) {
			spa->spa_log_state = SPA_LOG_MISSING;
			return (1);
		}
		break;

	case SPA_LOG_CLEAR:
		(void) dmu_objset_find(spa->spa_name, zil_clear_log_chain, NULL,
		    DS_FIND_CHILDREN);
		break;
	}
	spa->spa_log_state = SPA_LOG_GOOD;
	return (0);
}

/*
 * Load an existing storage pool, using the pool's builtin spa_config as a
 * source of configuration information.
 */
static int
spa_load(spa_t *spa, nvlist_t *config, spa_load_state_t state, int mosconfig)
{
	int error = 0;
	nvlist_t *nvroot = NULL;
	vdev_t *rvd;
	uberblock_t *ub = &spa->spa_uberblock;
	uint64_t config_cache_txg = spa->spa_config_txg;
	uint64_t pool_guid;
	uint64_t version;
	uint64_t autoreplace = 0;
	int orig_mode = spa->spa_mode;
	char *ereport = FM_EREPORT_ZFS_POOL;

	/*
	 * If this is an untrusted config, access the pool in read-only mode.
	 * This prevents things like resilvering recently removed devices.
	 */
	if (!mosconfig)
		spa->spa_mode = FREAD;

	ASSERT(MUTEX_HELD(&spa_namespace_lock));

	spa->spa_load_state = state;

	if (nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE, &nvroot) ||
	    nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID, &pool_guid)) {
		error = EINVAL;
		goto out;
	}

	/*
	 * Versioning wasn't explicitly added to the label until later, so if
	 * it's not present treat it as the initial version.
	 */
	if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_VERSION, &version) != 0)
		version = SPA_VERSION_INITIAL;

	(void) nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_TXG,
	    &spa->spa_config_txg);

	if ((state == SPA_LOAD_IMPORT || state == SPA_LOAD_TRYIMPORT) &&
	    spa_guid_exists(pool_guid, 0)) {
		error = EEXIST;
		goto out;
	}

	spa->spa_load_guid = pool_guid;

	/*
	 * Parse the configuration into a vdev tree.  We explicitly set the
	 * value that will be returned by spa_version() since parsing the
	 * configuration requires knowing the version number.
	 */
	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
	spa->spa_ubsync.ub_version = version;
	error = spa_config_parse(spa, &rvd, nvroot, NULL, 0, VDEV_ALLOC_LOAD);
	spa_config_exit(spa, SCL_ALL, FTAG);

	if (error != 0)
		goto out;

	ASSERT(spa->spa_root_vdev == rvd);
	ASSERT(spa_guid(spa) == pool_guid);

	/*
	 * Try to open all vdevs, loading each label in the process.
	 */
	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
	error = vdev_open(rvd);
	spa_config_exit(spa, SCL_ALL, FTAG);
	if (error != 0)
		goto out;

	/*
	 * Validate the labels for all leaf vdevs.  We need to grab the config
	 * lock because all label I/O is done with ZIO_FLAG_CONFIG_WRITER.
	 */
	if (mosconfig) {
		spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
		error = vdev_validate(rvd);
		spa_config_exit(spa, SCL_ALL, FTAG);
		if (error != 0)
			goto out;
	}

	if (rvd->vdev_state <= VDEV_STATE_CANT_OPEN) {
		error = ENXIO;
		goto out;
	}

	/*
	 * Find the best uberblock.
	 */
	vdev_uberblock_load(NULL, rvd, ub);

	/*
	 * If we weren't able to find a single valid uberblock, return failure.
	 */
	if (ub->ub_txg == 0) {
		vdev_set_state(rvd, B_TRUE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_CORRUPT_DATA);
		error = ENXIO;
		goto out;
	}

	/*
	 * If the pool is newer than the code, we can't open it.
	 */
	if (ub->ub_version > SPA_VERSION) {
		vdev_set_state(rvd, B_TRUE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_VERSION_NEWER);
		error = ENOTSUP;
		goto out;
	}

	/*
	 * If the vdev guid sum doesn't match the uberblock, we have an
	 * incomplete configuration.
	 */
	if (rvd->vdev_guid_sum != ub->ub_guid_sum && mosconfig) {
		vdev_set_state(rvd, B_TRUE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_BAD_GUID_SUM);
		error = ENXIO;
		goto out;
	}

	/*
	 * Initialize internal SPA structures.
	 */
	spa->spa_state = POOL_STATE_ACTIVE;
	spa->spa_ubsync = spa->spa_uberblock;
	spa->spa_first_txg = spa_last_synced_txg(spa) + 1;
	error = dsl_pool_open(spa, spa->spa_first_txg, &spa->spa_dsl_pool);
	if (error) {
		vdev_set_state(rvd, B_TRUE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_CORRUPT_DATA);
		goto out;
	}
	spa->spa_meta_objset = spa->spa_dsl_pool->dp_meta_objset;

	if (zap_lookup(spa->spa_meta_objset,
	    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_CONFIG,
	    sizeof (uint64_t), 1, &spa->spa_config_object) != 0) {
		vdev_set_state(rvd, B_TRUE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_CORRUPT_DATA);
		error = EIO;
		goto out;
	}

	if (!mosconfig) {
		nvlist_t *newconfig;
		uint64_t hostid;

		if (load_nvlist(spa, spa->spa_config_object, &newconfig) != 0) {
			vdev_set_state(rvd, B_TRUE, VDEV_STATE_CANT_OPEN,
			    VDEV_AUX_CORRUPT_DATA);
			error = EIO;
			goto out;
		}

		if (!spa_is_root(spa) && nvlist_lookup_uint64(newconfig,
		    ZPOOL_CONFIG_HOSTID, &hostid) == 0) {
			char *hostname;
			unsigned long myhostid = 0;

			VERIFY(nvlist_lookup_string(newconfig,
			    ZPOOL_CONFIG_HOSTNAME, &hostname) == 0);

#ifdef	_KERNEL
			myhostid = zone_get_hostid(NULL);
#else	/* _KERNEL */
			/*
			 * We're emulating the system's hostid in userland, so
			 * we can't use zone_get_hostid().
			 */
			(void) ddi_strtoul(hw_serial, NULL, 10, &myhostid);
#endif	/* _KERNEL */
			if (hostid != 0 && myhostid != 0 &&
			    hostid != myhostid) {
				cmn_err(CE_WARN, "pool '%s' could not be "
				    "loaded as it was last accessed by "
				    "another system (host: %s hostid: 0x%lx). "
				    "See: http://www.sun.com/msg/ZFS-8000-EY",
				    spa_name(spa), hostname,
				    (unsigned long)hostid);
				error = EBADF;
				goto out;
			}
		}

		spa_config_set(spa, newconfig);
		spa_unload(spa);
		spa_deactivate(spa);
		spa_activate(spa, orig_mode);

		return (spa_load(spa, newconfig, state, B_TRUE));
	}

	if (zap_lookup(spa->spa_meta_objset,
	    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_SYNC_BPLIST,
	    sizeof (uint64_t), 1, &spa->spa_sync_bplist_obj) != 0) {
		vdev_set_state(rvd, B_TRUE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_CORRUPT_DATA);
		error = EIO;
		goto out;
	}

	/*
	 * Load the bit that tells us to use the new accounting function
	 * (raid-z deflation).  If we have an older pool, this will not
	 * be present.
	 */
	error = zap_lookup(spa->spa_meta_objset,
	    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_DEFLATE,
	    sizeof (uint64_t), 1, &spa->spa_deflate);
	if (error != 0 && error != ENOENT) {
		vdev_set_state(rvd, B_TRUE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_CORRUPT_DATA);
		error = EIO;
		goto out;
	}

	/*
	 * Load the persistent error log.  If we have an older pool, this will
	 * not be present.
	 */
	error = zap_lookup(spa->spa_meta_objset,
	    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_ERRLOG_LAST,
	    sizeof (uint64_t), 1, &spa->spa_errlog_last);
	if (error != 0 && error != ENOENT) {
		vdev_set_state(rvd, B_TRUE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_CORRUPT_DATA);
		error = EIO;
		goto out;
	}

	error = zap_lookup(spa->spa_meta_objset,
	    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_ERRLOG_SCRUB,
	    sizeof (uint64_t), 1, &spa->spa_errlog_scrub);
	if (error != 0 && error != ENOENT) {
		vdev_set_state(rvd, B_TRUE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_CORRUPT_DATA);
		error = EIO;
		goto out;
	}

	/*
	 * Load the history object.  If we have an older pool, this
	 * will not be present.
	 */
	error = zap_lookup(spa->spa_meta_objset,
	    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_HISTORY,
	    sizeof (uint64_t), 1, &spa->spa_history);
	if (error != 0 && error != ENOENT) {
		vdev_set_state(rvd, B_TRUE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_CORRUPT_DATA);
		error = EIO;
		goto out;
	}

	/*
	 * Load any hot spares for this pool.
	 */
	error = zap_lookup(spa->spa_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_SPARES, sizeof (uint64_t), 1, &spa->spa_spares.sav_object);
	if (error != 0 && error != ENOENT) {
		vdev_set_state(rvd, B_TRUE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_CORRUPT_DATA);
		error = EIO;
		goto out;
	}
	if (error == 0) {
		ASSERT(spa_version(spa) >= SPA_VERSION_SPARES);
		if (load_nvlist(spa, spa->spa_spares.sav_object,
		    &spa->spa_spares.sav_config) != 0) {
			vdev_set_state(rvd, B_TRUE, VDEV_STATE_CANT_OPEN,
			    VDEV_AUX_CORRUPT_DATA);
			error = EIO;
			goto out;
		}

		spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
		spa_load_spares(spa);
		spa_config_exit(spa, SCL_ALL, FTAG);
	}

	/*
	 * Load any level 2 ARC devices for this pool.
	 */
	error = zap_lookup(spa->spa_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_L2CACHE, sizeof (uint64_t), 1,
	    &spa->spa_l2cache.sav_object);
	if (error != 0 && error != ENOENT) {
		vdev_set_state(rvd, B_TRUE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_CORRUPT_DATA);
		error = EIO;
		goto out;
	}
	if (error == 0) {
		ASSERT(spa_version(spa) >= SPA_VERSION_L2CACHE);
		if (load_nvlist(spa, spa->spa_l2cache.sav_object,
		    &spa->spa_l2cache.sav_config) != 0) {
			vdev_set_state(rvd, B_TRUE,
			    VDEV_STATE_CANT_OPEN,
			    VDEV_AUX_CORRUPT_DATA);
			error = EIO;
			goto out;
		}

		spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
		spa_load_l2cache(spa);
		spa_config_exit(spa, SCL_ALL, FTAG);
	}

	if (spa_check_logs(spa)) {
		vdev_set_state(rvd, B_TRUE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_BAD_LOG);
		error = ENXIO;
		ereport = FM_EREPORT_ZFS_LOG_REPLAY;
		goto out;
	}


	spa->spa_delegation = zpool_prop_default_numeric(ZPOOL_PROP_DELEGATION);

	error = zap_lookup(spa->spa_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_PROPS, sizeof (uint64_t), 1, &spa->spa_pool_props_object);

	if (error && error != ENOENT) {
		vdev_set_state(rvd, B_TRUE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_CORRUPT_DATA);
		error = EIO;
		goto out;
	}

	if (error == 0) {
		(void) zap_lookup(spa->spa_meta_objset,
		    spa->spa_pool_props_object,
		    zpool_prop_to_name(ZPOOL_PROP_BOOTFS),
		    sizeof (uint64_t), 1, &spa->spa_bootfs);
		(void) zap_lookup(spa->spa_meta_objset,
		    spa->spa_pool_props_object,
		    zpool_prop_to_name(ZPOOL_PROP_AUTOREPLACE),
		    sizeof (uint64_t), 1, &autoreplace);
		(void) zap_lookup(spa->spa_meta_objset,
		    spa->spa_pool_props_object,
		    zpool_prop_to_name(ZPOOL_PROP_DELEGATION),
		    sizeof (uint64_t), 1, &spa->spa_delegation);
		(void) zap_lookup(spa->spa_meta_objset,
		    spa->spa_pool_props_object,
		    zpool_prop_to_name(ZPOOL_PROP_FAILUREMODE),
		    sizeof (uint64_t), 1, &spa->spa_failmode);
	}

	/*
	 * If the 'autoreplace' property is set, then post a resource notifying
	 * the ZFS DE that it should not issue any faults for unopenable
	 * devices.  We also iterate over the vdevs, and post a sysevent for any
	 * unopenable vdevs so that the normal autoreplace handler can take
	 * over.
	 */
	if (autoreplace && state != SPA_LOAD_TRYIMPORT)
		spa_check_removed(spa->spa_root_vdev);

	/*
	 * Load the vdev state for all toplevel vdevs.
	 */
	vdev_load(rvd);

	/*
	 * Propagate the leaf DTLs we just loaded all the way up the tree.
	 */
	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
	vdev_dtl_reassess(rvd, 0, 0, B_FALSE);
	spa_config_exit(spa, SCL_ALL, FTAG);

	/*
	 * Check the state of the root vdev.  If it can't be opened, it
	 * indicates one or more toplevel vdevs are faulted.
	 */
	if (rvd->vdev_state <= VDEV_STATE_CANT_OPEN) {
		error = ENXIO;
		goto out;
	}

	if (spa_writeable(spa)) {
		dmu_tx_t *tx;
		int need_update = B_FALSE;

		ASSERT(state != SPA_LOAD_TRYIMPORT);

		/*
		 * Claim log blocks that haven't been committed yet.
		 * This must all happen in a single txg.
		 */
		tx = dmu_tx_create_assigned(spa_get_dsl(spa),
		    spa_first_txg(spa));
		(void) dmu_objset_find(spa_name(spa),
		    zil_claim, tx, DS_FIND_CHILDREN);
		dmu_tx_commit(tx);

		spa->spa_sync_on = B_TRUE;
		txg_sync_start(spa->spa_dsl_pool);

		/*
		 * Wait for all claims to sync.
		 */
		txg_wait_synced(spa->spa_dsl_pool, 0);

		/*
		 * If the config cache is stale, or we have uninitialized
		 * metaslabs (see spa_vdev_add()), then update the config.
		 */
		if (config_cache_txg != spa->spa_config_txg ||
		    state == SPA_LOAD_IMPORT)
			need_update = B_TRUE;

		for (int c = 0; c < rvd->vdev_children; c++)
			if (rvd->vdev_child[c]->vdev_ms_array == 0)
				need_update = B_TRUE;

		/*
		 * Update the config cache asychronously in case we're the
		 * root pool, in which case the config cache isn't writable yet.
		 */
		if (need_update)
			spa_async_request(spa, SPA_ASYNC_CONFIG_UPDATE);

		/*
		 * Check all DTLs to see if anything needs resilvering.
		 */
		if (vdev_resilver_needed(rvd, NULL, NULL))
			spa_async_request(spa, SPA_ASYNC_RESILVER);
	}

	error = 0;
out:
	spa->spa_minref = refcount_count(&spa->spa_refcount);
	if (error && error != EBADF)
		zfs_ereport_post(ereport, spa, NULL, NULL, 0, 0);
	spa->spa_load_state = SPA_LOAD_NONE;
	spa->spa_ena = 0;

	return (error);
}

/*
 * Pool Open/Import
 *
 * The import case is identical to an open except that the configuration is sent
 * down from userland, instead of grabbed from the configuration cache.  For the
 * case of an open, the pool configuration will exist in the
 * POOL_STATE_UNINITIALIZED state.
 *
 * The stats information (gen/count/ustats) is used to gather vdev statistics at
 * the same time open the pool, without having to keep around the spa_t in some
 * ambiguous state.
 */
static int
spa_open_common(const char *pool, spa_t **spapp, void *tag, nvlist_t **config)
{
	spa_t *spa;
	int error;
	int locked = B_FALSE;

	*spapp = NULL;

	/*
	 * As disgusting as this is, we need to support recursive calls to this
	 * function because dsl_dir_open() is called during spa_load(), and ends
	 * up calling spa_open() again.  The real fix is to figure out how to
	 * avoid dsl_dir_open() calling this in the first place.
	 */
	if (mutex_owner(&spa_namespace_lock) != curthread) {
		mutex_enter(&spa_namespace_lock);
		locked = B_TRUE;
	}

	if ((spa = spa_lookup(pool)) == NULL) {
		if (locked)
			mutex_exit(&spa_namespace_lock);
		return (ENOENT);
	}
	if (spa->spa_state == POOL_STATE_UNINITIALIZED) {

		spa_activate(spa, spa_mode_global);

		error = spa_load(spa, spa->spa_config, SPA_LOAD_OPEN, B_FALSE);

		if (error == EBADF) {
			/*
			 * If vdev_validate() returns failure (indicated by
			 * EBADF), it indicates that one of the vdevs indicates
			 * that the pool has been exported or destroyed.  If
			 * this is the case, the config cache is out of sync and
			 * we should remove the pool from the namespace.
			 */
			spa_unload(spa);
			spa_deactivate(spa);
			spa_config_sync(spa, B_TRUE, B_TRUE);
			spa_remove(spa);
			if (locked)
				mutex_exit(&spa_namespace_lock);
			return (ENOENT);
		}

		if (error) {
			/*
			 * We can't open the pool, but we still have useful
			 * information: the state of each vdev after the
			 * attempted vdev_open().  Return this to the user.
			 */
			if (config != NULL && spa->spa_root_vdev != NULL)
				*config = spa_config_generate(spa, NULL, -1ULL,
				    B_TRUE);
			spa_unload(spa);
			spa_deactivate(spa);
			spa->spa_last_open_failed = B_TRUE;
			if (locked)
				mutex_exit(&spa_namespace_lock);
			*spapp = NULL;
			return (error);
		} else {
			spa->spa_last_open_failed = B_FALSE;
		}
	}

	spa_open_ref(spa, tag);

	if (locked)
		mutex_exit(&spa_namespace_lock);

	*spapp = spa;

	if (config != NULL)
		*config = spa_config_generate(spa, NULL, -1ULL, B_TRUE);

	return (0);
}

int
spa_open(const char *name, spa_t **spapp, void *tag)
{
	return (spa_open_common(name, spapp, tag, NULL));
}

/*
 * Lookup the given spa_t, incrementing the inject count in the process,
 * preventing it from being exported or destroyed.
 */
spa_t *
spa_inject_addref(char *name)
{
	spa_t *spa;

	mutex_enter(&spa_namespace_lock);
	if ((spa = spa_lookup(name)) == NULL) {
		mutex_exit(&spa_namespace_lock);
		return (NULL);
	}
	spa->spa_inject_ref++;
	mutex_exit(&spa_namespace_lock);

	return (spa);
}

void
spa_inject_delref(spa_t *spa)
{
	mutex_enter(&spa_namespace_lock);
	spa->spa_inject_ref--;
	mutex_exit(&spa_namespace_lock);
}

/*
 * Add spares device information to the nvlist.
 */
static void
spa_add_spares(spa_t *spa, nvlist_t *config)
{
	nvlist_t **spares;
	uint_t i, nspares;
	nvlist_t *nvroot;
	uint64_t guid;
	vdev_stat_t *vs;
	uint_t vsc;
	uint64_t pool;

	if (spa->spa_spares.sav_count == 0)
		return;

	VERIFY(nvlist_lookup_nvlist(config,
	    ZPOOL_CONFIG_VDEV_TREE, &nvroot) == 0);
	VERIFY(nvlist_lookup_nvlist_array(spa->spa_spares.sav_config,
	    ZPOOL_CONFIG_SPARES, &spares, &nspares) == 0);
	if (nspares != 0) {
		VERIFY(nvlist_add_nvlist_array(nvroot,
		    ZPOOL_CONFIG_SPARES, spares, nspares) == 0);
		VERIFY(nvlist_lookup_nvlist_array(nvroot,
		    ZPOOL_CONFIG_SPARES, &spares, &nspares) == 0);

		/*
		 * Go through and find any spares which have since been
		 * repurposed as an active spare.  If this is the case, update
		 * their status appropriately.
		 */
		for (i = 0; i < nspares; i++) {
			VERIFY(nvlist_lookup_uint64(spares[i],
			    ZPOOL_CONFIG_GUID, &guid) == 0);
			if (spa_spare_exists(guid, &pool, NULL) &&
			    pool != 0ULL) {
				VERIFY(nvlist_lookup_uint64_array(
				    spares[i], ZPOOL_CONFIG_STATS,
				    (uint64_t **)&vs, &vsc) == 0);
				vs->vs_state = VDEV_STATE_CANT_OPEN;
				vs->vs_aux = VDEV_AUX_SPARED;
			}
		}
	}
}

/*
 * Add l2cache device information to the nvlist, including vdev stats.
 */
static void
spa_add_l2cache(spa_t *spa, nvlist_t *config)
{
	nvlist_t **l2cache;
	uint_t i, j, nl2cache;
	nvlist_t *nvroot;
	uint64_t guid;
	vdev_t *vd;
	vdev_stat_t *vs;
	uint_t vsc;

	if (spa->spa_l2cache.sav_count == 0)
		return;

	spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);

	VERIFY(nvlist_lookup_nvlist(config,
	    ZPOOL_CONFIG_VDEV_TREE, &nvroot) == 0);
	VERIFY(nvlist_lookup_nvlist_array(spa->spa_l2cache.sav_config,
	    ZPOOL_CONFIG_L2CACHE, &l2cache, &nl2cache) == 0);
	if (nl2cache != 0) {
		VERIFY(nvlist_add_nvlist_array(nvroot,
		    ZPOOL_CONFIG_L2CACHE, l2cache, nl2cache) == 0);
		VERIFY(nvlist_lookup_nvlist_array(nvroot,
		    ZPOOL_CONFIG_L2CACHE, &l2cache, &nl2cache) == 0);

		/*
		 * Update level 2 cache device stats.
		 */

		for (i = 0; i < nl2cache; i++) {
			VERIFY(nvlist_lookup_uint64(l2cache[i],
			    ZPOOL_CONFIG_GUID, &guid) == 0);

			vd = NULL;
			for (j = 0; j < spa->spa_l2cache.sav_count; j++) {
				if (guid ==
				    spa->spa_l2cache.sav_vdevs[j]->vdev_guid) {
					vd = spa->spa_l2cache.sav_vdevs[j];
					break;
				}
			}
			ASSERT(vd != NULL);

			VERIFY(nvlist_lookup_uint64_array(l2cache[i],
			    ZPOOL_CONFIG_STATS, (uint64_t **)&vs, &vsc) == 0);
			vdev_get_stats(vd, vs);
		}
	}

	spa_config_exit(spa, SCL_CONFIG, FTAG);
}

int
spa_get_stats(const char *name, nvlist_t **config, char *altroot, size_t buflen)
{
	int error;
	spa_t *spa;

	*config = NULL;
	error = spa_open_common(name, &spa, FTAG, config);

	if (spa && *config != NULL) {
		VERIFY(nvlist_add_uint64(*config, ZPOOL_CONFIG_ERRCOUNT,
		    spa_get_errlog_size(spa)) == 0);

		if (spa_suspended(spa))
			VERIFY(nvlist_add_uint64(*config,
			    ZPOOL_CONFIG_SUSPENDED, spa->spa_failmode) == 0);

		spa_add_spares(spa, *config);
		spa_add_l2cache(spa, *config);
	}

	/*
	 * We want to get the alternate root even for faulted pools, so we cheat
	 * and call spa_lookup() directly.
	 */
	if (altroot) {
		if (spa == NULL) {
			mutex_enter(&spa_namespace_lock);
			spa = spa_lookup(name);
			if (spa)
				spa_altroot(spa, altroot, buflen);
			else
				altroot[0] = '\0';
			spa = NULL;
			mutex_exit(&spa_namespace_lock);
		} else {
			spa_altroot(spa, altroot, buflen);
		}
	}

	if (spa != NULL)
		spa_close(spa, FTAG);

	return (error);
}

/*
 * Validate that the auxiliary device array is well formed.  We must have an
 * array of nvlists, each which describes a valid leaf vdev.  If this is an
 * import (mode is VDEV_ALLOC_SPARE), then we allow corrupted spares to be
 * specified, as long as they are well-formed.
 */
static int
spa_validate_aux_devs(spa_t *spa, nvlist_t *nvroot, uint64_t crtxg, int mode,
    spa_aux_vdev_t *sav, const char *config, uint64_t version,
    vdev_labeltype_t label)
{
	nvlist_t **dev;
	uint_t i, ndev;
	vdev_t *vd;
	int error;

	ASSERT(spa_config_held(spa, SCL_ALL, RW_WRITER) == SCL_ALL);

	/*
	 * It's acceptable to have no devs specified.
	 */
	if (nvlist_lookup_nvlist_array(nvroot, config, &dev, &ndev) != 0)
		return (0);

	if (ndev == 0)
		return (EINVAL);

	/*
	 * Make sure the pool is formatted with a version that supports this
	 * device type.
	 */
	if (spa_version(spa) < version)
		return (ENOTSUP);

	/*
	 * Set the pending device list so we correctly handle device in-use
	 * checking.
	 */
	sav->sav_pending = dev;
	sav->sav_npending = ndev;

	for (i = 0; i < ndev; i++) {
		if ((error = spa_config_parse(spa, &vd, dev[i], NULL, 0,
		    mode)) != 0)
			goto out;

		if (!vd->vdev_ops->vdev_op_leaf) {
			vdev_free(vd);
			error = EINVAL;
			goto out;
		}

		/*
		 * The L2ARC currently only supports disk devices in
		 * kernel context.  For user-level testing, we allow it.
		 */
#ifdef _KERNEL
		if ((strcmp(config, ZPOOL_CONFIG_L2CACHE) == 0) &&
		    strcmp(vd->vdev_ops->vdev_op_type, VDEV_TYPE_DISK) != 0) {
			error = ENOTBLK;
			goto out;
		}
#endif
		vd->vdev_top = vd;

		if ((error = vdev_open(vd)) == 0 &&
		    (error = vdev_label_init(vd, crtxg, label)) == 0) {
			VERIFY(nvlist_add_uint64(dev[i], ZPOOL_CONFIG_GUID,
			    vd->vdev_guid) == 0);
		}

		vdev_free(vd);

		if (error &&
		    (mode != VDEV_ALLOC_SPARE && mode != VDEV_ALLOC_L2CACHE))
			goto out;
		else
			error = 0;
	}

out:
	sav->sav_pending = NULL;
	sav->sav_npending = 0;
	return (error);
}

static int
spa_validate_aux(spa_t *spa, nvlist_t *nvroot, uint64_t crtxg, int mode)
{
	int error;

	ASSERT(spa_config_held(spa, SCL_ALL, RW_WRITER) == SCL_ALL);

	if ((error = spa_validate_aux_devs(spa, nvroot, crtxg, mode,
	    &spa->spa_spares, ZPOOL_CONFIG_SPARES, SPA_VERSION_SPARES,
	    VDEV_LABEL_SPARE)) != 0) {
		return (error);
	}

	return (spa_validate_aux_devs(spa, nvroot, crtxg, mode,
	    &spa->spa_l2cache, ZPOOL_CONFIG_L2CACHE, SPA_VERSION_L2CACHE,
	    VDEV_LABEL_L2CACHE));
}

static void
spa_set_aux_vdevs(spa_aux_vdev_t *sav, nvlist_t **devs, int ndevs,
    const char *config)
{
	int i;

	if (sav->sav_config != NULL) {
		nvlist_t **olddevs;
		uint_t oldndevs;
		nvlist_t **newdevs;

		/*
		 * Generate new dev list by concatentating with the
		 * current dev list.
		 */
		VERIFY(nvlist_lookup_nvlist_array(sav->sav_config, config,
		    &olddevs, &oldndevs) == 0);

		newdevs = kmem_alloc(sizeof (void *) *
		    (ndevs + oldndevs), KM_SLEEP);
		for (i = 0; i < oldndevs; i++)
			VERIFY(nvlist_dup(olddevs[i], &newdevs[i],
			    KM_SLEEP) == 0);
		for (i = 0; i < ndevs; i++)
			VERIFY(nvlist_dup(devs[i], &newdevs[i + oldndevs],
			    KM_SLEEP) == 0);

		VERIFY(nvlist_remove(sav->sav_config, config,
		    DATA_TYPE_NVLIST_ARRAY) == 0);

		VERIFY(nvlist_add_nvlist_array(sav->sav_config,
		    config, newdevs, ndevs + oldndevs) == 0);
		for (i = 0; i < oldndevs + ndevs; i++)
			nvlist_free(newdevs[i]);
		kmem_free(newdevs, (oldndevs + ndevs) * sizeof (void *));
	} else {
		/*
		 * Generate a new dev list.
		 */
		VERIFY(nvlist_alloc(&sav->sav_config, NV_UNIQUE_NAME,
		    KM_SLEEP) == 0);
		VERIFY(nvlist_add_nvlist_array(sav->sav_config, config,
		    devs, ndevs) == 0);
	}
}

/*
 * Stop and drop level 2 ARC devices
 */
void
spa_l2cache_drop(spa_t *spa)
{
	vdev_t *vd;
	int i;
	spa_aux_vdev_t *sav = &spa->spa_l2cache;

	for (i = 0; i < sav->sav_count; i++) {
		uint64_t pool;

		vd = sav->sav_vdevs[i];
		ASSERT(vd != NULL);

		if (spa_l2cache_exists(vd->vdev_guid, &pool) &&
		    pool != 0ULL && l2arc_vdev_present(vd))
			l2arc_remove_vdev(vd);
		if (vd->vdev_isl2cache)
			spa_l2cache_remove(vd);
		vdev_clear_stats(vd);
		(void) vdev_close(vd);
	}
}

/*
 * Pool Creation
 */
int
spa_create(const char *pool, nvlist_t *nvroot, nvlist_t *props,
    const char *history_str, nvlist_t *zplprops)
{
	spa_t *spa;
	char *altroot = NULL;
	vdev_t *rvd;
	dsl_pool_t *dp;
	dmu_tx_t *tx;
	int c, error = 0;
	uint64_t txg = TXG_INITIAL;
	nvlist_t **spares, **l2cache;
	uint_t nspares, nl2cache;
	uint64_t version;

	/*
	 * If this pool already exists, return failure.
	 */
	mutex_enter(&spa_namespace_lock);
	if (spa_lookup(pool) != NULL) {
		mutex_exit(&spa_namespace_lock);
		return (EEXIST);
	}

	/*
	 * Allocate a new spa_t structure.
	 */
	(void) nvlist_lookup_string(props,
	    zpool_prop_to_name(ZPOOL_PROP_ALTROOT), &altroot);
	spa = spa_add(pool, altroot);
	spa_activate(spa, spa_mode_global);

	spa->spa_uberblock.ub_txg = txg - 1;

	if (props && (error = spa_prop_validate(spa, props))) {
		spa_unload(spa);
		spa_deactivate(spa);
		spa_remove(spa);
		mutex_exit(&spa_namespace_lock);
		return (error);
	}

	if (nvlist_lookup_uint64(props, zpool_prop_to_name(ZPOOL_PROP_VERSION),
	    &version) != 0)
		version = SPA_VERSION;
	ASSERT(version <= SPA_VERSION);
	spa->spa_uberblock.ub_version = version;
	spa->spa_ubsync = spa->spa_uberblock;

	/*
	 * Create the root vdev.
	 */
	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);

	error = spa_config_parse(spa, &rvd, nvroot, NULL, 0, VDEV_ALLOC_ADD);

	ASSERT(error != 0 || rvd != NULL);
	ASSERT(error != 0 || spa->spa_root_vdev == rvd);

	if (error == 0 && !zfs_allocatable_devs(nvroot))
		error = EINVAL;

	if (error == 0 &&
	    (error = vdev_create(rvd, txg, B_FALSE)) == 0 &&
	    (error = spa_validate_aux(spa, nvroot, txg,
	    VDEV_ALLOC_ADD)) == 0) {
		for (c = 0; c < rvd->vdev_children; c++)
			vdev_init(rvd->vdev_child[c], txg);
		vdev_config_dirty(rvd);
	}

	spa_config_exit(spa, SCL_ALL, FTAG);

	if (error != 0) {
		spa_unload(spa);
		spa_deactivate(spa);
		spa_remove(spa);
		mutex_exit(&spa_namespace_lock);
		return (error);
	}

	/*
	 * Get the list of spares, if specified.
	 */
	if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_SPARES,
	    &spares, &nspares) == 0) {
		VERIFY(nvlist_alloc(&spa->spa_spares.sav_config, NV_UNIQUE_NAME,
		    KM_SLEEP) == 0);
		VERIFY(nvlist_add_nvlist_array(spa->spa_spares.sav_config,
		    ZPOOL_CONFIG_SPARES, spares, nspares) == 0);
		spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
		spa_load_spares(spa);
		spa_config_exit(spa, SCL_ALL, FTAG);
		spa->spa_spares.sav_sync = B_TRUE;
	}

	/*
	 * Get the list of level 2 cache devices, if specified.
	 */
	if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_L2CACHE,
	    &l2cache, &nl2cache) == 0) {
		VERIFY(nvlist_alloc(&spa->spa_l2cache.sav_config,
		    NV_UNIQUE_NAME, KM_SLEEP) == 0);
		VERIFY(nvlist_add_nvlist_array(spa->spa_l2cache.sav_config,
		    ZPOOL_CONFIG_L2CACHE, l2cache, nl2cache) == 0);
		spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
		spa_load_l2cache(spa);
		spa_config_exit(spa, SCL_ALL, FTAG);
		spa->spa_l2cache.sav_sync = B_TRUE;
	}

	spa->spa_dsl_pool = dp = dsl_pool_create(spa, zplprops, txg);
	spa->spa_meta_objset = dp->dp_meta_objset;

	tx = dmu_tx_create_assigned(dp, txg);

	/*
	 * Create the pool config object.
	 */
	spa->spa_config_object = dmu_object_alloc(spa->spa_meta_objset,
	    DMU_OT_PACKED_NVLIST, SPA_CONFIG_BLOCKSIZE,
	    DMU_OT_PACKED_NVLIST_SIZE, sizeof (uint64_t), tx);

	if (zap_add(spa->spa_meta_objset,
	    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_CONFIG,
	    sizeof (uint64_t), 1, &spa->spa_config_object, tx) != 0) {
		cmn_err(CE_PANIC, "failed to add pool config");
	}

	/* Newly created pools with the right version are always deflated. */
	if (version >= SPA_VERSION_RAIDZ_DEFLATE) {
		spa->spa_deflate = TRUE;
		if (zap_add(spa->spa_meta_objset,
		    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_DEFLATE,
		    sizeof (uint64_t), 1, &spa->spa_deflate, tx) != 0) {
			cmn_err(CE_PANIC, "failed to add deflate");
		}
	}

	/*
	 * Create the deferred-free bplist object.  Turn off compression
	 * because sync-to-convergence takes longer if the blocksize
	 * keeps changing.
	 */
	spa->spa_sync_bplist_obj = bplist_create(spa->spa_meta_objset,
	    1 << 14, tx);
	dmu_object_set_compress(spa->spa_meta_objset, spa->spa_sync_bplist_obj,
	    ZIO_COMPRESS_OFF, tx);

	if (zap_add(spa->spa_meta_objset,
	    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_SYNC_BPLIST,
	    sizeof (uint64_t), 1, &spa->spa_sync_bplist_obj, tx) != 0) {
		cmn_err(CE_PANIC, "failed to add bplist");
	}

	/*
	 * Create the pool's history object.
	 */
	if (version >= SPA_VERSION_ZPOOL_HISTORY)
		spa_history_create_obj(spa, tx);

	/*
	 * Set pool properties.
	 */
	spa->spa_bootfs = zpool_prop_default_numeric(ZPOOL_PROP_BOOTFS);
	spa->spa_delegation = zpool_prop_default_numeric(ZPOOL_PROP_DELEGATION);
	spa->spa_failmode = zpool_prop_default_numeric(ZPOOL_PROP_FAILUREMODE);
	if (props != NULL) {
		spa_configfile_set(spa, props, B_FALSE);
		spa_sync_props(spa, props, CRED(), tx);
	}

	dmu_tx_commit(tx);

	spa->spa_sync_on = B_TRUE;
	txg_sync_start(spa->spa_dsl_pool);

	/*
	 * We explicitly wait for the first transaction to complete so that our
	 * bean counters are appropriately updated.
	 */
	txg_wait_synced(spa->spa_dsl_pool, txg);

	spa_config_sync(spa, B_FALSE, B_TRUE);

	if (version >= SPA_VERSION_ZPOOL_HISTORY && history_str != NULL)
		(void) spa_history_log(spa, history_str, LOG_CMD_POOL_CREATE);

	spa->spa_minref = refcount_count(&spa->spa_refcount);

	mutex_exit(&spa_namespace_lock);

	return (0);
}

/*
 * Import the given pool into the system.  We set up the necessary spa_t and
 * then call spa_load() to do the dirty work.
 */
static int
spa_import_common(const char *pool, nvlist_t *config, nvlist_t *props,
    boolean_t isroot, boolean_t allowfaulted)
{
	spa_t *spa;
	char *altroot = NULL;
	int error, loaderr;
	nvlist_t *nvroot;
	nvlist_t **spares, **l2cache;
	uint_t nspares, nl2cache;

	/*
	 * If a pool with this name exists, return failure.
	 */
	mutex_enter(&spa_namespace_lock);
	if ((spa = spa_lookup(pool)) != NULL) {
		if (isroot) {
			/*
			 * Remove the existing root pool from the
			 * namespace so that we can replace it with
			 * the correct config we just read in.
			 */
			ASSERT(spa->spa_state == POOL_STATE_UNINITIALIZED);
			spa_remove(spa);
		} else {
			mutex_exit(&spa_namespace_lock);
			return (EEXIST);
		}
	}

	/*
	 * Create and initialize the spa structure.
	 */
	(void) nvlist_lookup_string(props,
	    zpool_prop_to_name(ZPOOL_PROP_ALTROOT), &altroot);
	spa = spa_add(pool, altroot);
	spa_activate(spa, spa_mode_global);

	if (allowfaulted)
		spa->spa_import_faulted = B_TRUE;
	spa->spa_is_root = isroot;

	/*
	 * Pass off the heavy lifting to spa_load().
	 * Pass TRUE for mosconfig (unless this is a root pool) because
	 * the user-supplied config is actually the one to trust when
	 * doing an import.
	 */
	loaderr = error = spa_load(spa, config, SPA_LOAD_IMPORT, !isroot);

	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
	/*
	 * Toss any existing sparelist, as it doesn't have any validity anymore,
	 * and conflicts with spa_has_spare().
	 */
	if (!isroot && spa->spa_spares.sav_config) {
		nvlist_free(spa->spa_spares.sav_config);
		spa->spa_spares.sav_config = NULL;
		spa_load_spares(spa);
	}
	if (!isroot && spa->spa_l2cache.sav_config) {
		nvlist_free(spa->spa_l2cache.sav_config);
		spa->spa_l2cache.sav_config = NULL;
		spa_load_l2cache(spa);
	}

	VERIFY(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) == 0);
	if (error == 0)
		error = spa_validate_aux(spa, nvroot, -1ULL, VDEV_ALLOC_SPARE);
	if (error == 0)
		error = spa_validate_aux(spa, nvroot, -1ULL,
		    VDEV_ALLOC_L2CACHE);
	spa_config_exit(spa, SCL_ALL, FTAG);

	if (props != NULL)
		spa_configfile_set(spa, props, B_FALSE);

	if (error != 0 || (props && spa_writeable(spa) &&
	    (error = spa_prop_set(spa, props)))) {
		if (loaderr != 0 && loaderr != EINVAL && allowfaulted) {
			/*
			 * If we failed to load the pool, but 'allowfaulted' is
			 * set, then manually set the config as if the config
			 * passed in was specified in the cache file.
			 */
			error = 0;
			spa->spa_import_faulted = B_FALSE;
			if (spa->spa_config == NULL)
				spa->spa_config = spa_config_generate(spa,
				    NULL, -1ULL, B_TRUE);
			spa_unload(spa);
			spa_deactivate(spa);
			spa_config_sync(spa, B_FALSE, B_TRUE);
		} else {
			spa_unload(spa);
			spa_deactivate(spa);
			spa_remove(spa);
		}
		mutex_exit(&spa_namespace_lock);
		return (error);
	}

	/*
	 * Override any spares and level 2 cache devices as specified by
	 * the user, as these may have correct device names/devids, etc.
	 */
	if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_SPARES,
	    &spares, &nspares) == 0) {
		if (spa->spa_spares.sav_config)
			VERIFY(nvlist_remove(spa->spa_spares.sav_config,
			    ZPOOL_CONFIG_SPARES, DATA_TYPE_NVLIST_ARRAY) == 0);
		else
			VERIFY(nvlist_alloc(&spa->spa_spares.sav_config,
			    NV_UNIQUE_NAME, KM_SLEEP) == 0);
		VERIFY(nvlist_add_nvlist_array(spa->spa_spares.sav_config,
		    ZPOOL_CONFIG_SPARES, spares, nspares) == 0);
		spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
		spa_load_spares(spa);
		spa_config_exit(spa, SCL_ALL, FTAG);
		spa->spa_spares.sav_sync = B_TRUE;
	}
	if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_L2CACHE,
	    &l2cache, &nl2cache) == 0) {
		if (spa->spa_l2cache.sav_config)
			VERIFY(nvlist_remove(spa->spa_l2cache.sav_config,
			    ZPOOL_CONFIG_L2CACHE, DATA_TYPE_NVLIST_ARRAY) == 0);
		else
			VERIFY(nvlist_alloc(&spa->spa_l2cache.sav_config,
			    NV_UNIQUE_NAME, KM_SLEEP) == 0);
		VERIFY(nvlist_add_nvlist_array(spa->spa_l2cache.sav_config,
		    ZPOOL_CONFIG_L2CACHE, l2cache, nl2cache) == 0);
		spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
		spa_load_l2cache(spa);
		spa_config_exit(spa, SCL_ALL, FTAG);
		spa->spa_l2cache.sav_sync = B_TRUE;
	}

	if (spa_writeable(spa)) {
		/*
		 * Update the config cache to include the newly-imported pool.
		 */
		spa_config_update_common(spa, SPA_CONFIG_UPDATE_POOL, isroot);
	}

	spa->spa_import_faulted = B_FALSE;
	mutex_exit(&spa_namespace_lock);

	return (0);
}

#ifdef _KERNEL
/*
 * Build a "root" vdev for a top level vdev read in from a rootpool
 * device label.
 */
static void
spa_build_rootpool_config(nvlist_t *config)
{
	nvlist_t *nvtop, *nvroot;
	uint64_t pgid;

	/*
	 * Add this top-level vdev to the child array.
	 */
	VERIFY(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE, &nvtop)
	    == 0);
	VERIFY(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID, &pgid)
	    == 0);

	/*
	 * Put this pool's top-level vdevs into a root vdev.
	 */
	VERIFY(nvlist_alloc(&nvroot, NV_UNIQUE_NAME, KM_SLEEP) == 0);
	VERIFY(nvlist_add_string(nvroot, ZPOOL_CONFIG_TYPE, VDEV_TYPE_ROOT)
	    == 0);
	VERIFY(nvlist_add_uint64(nvroot, ZPOOL_CONFIG_ID, 0ULL) == 0);
	VERIFY(nvlist_add_uint64(nvroot, ZPOOL_CONFIG_GUID, pgid) == 0);
	VERIFY(nvlist_add_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN,
	    &nvtop, 1) == 0);

	/*
	 * Replace the existing vdev_tree with the new root vdev in
	 * this pool's configuration (remove the old, add the new).
	 */
	VERIFY(nvlist_add_nvlist(config, ZPOOL_CONFIG_VDEV_TREE, nvroot) == 0);
	nvlist_free(nvroot);
}

/*
 * Get the root pool information from the root disk, then import the root pool
 * during the system boot up time.
 */
extern int vdev_disk_read_rootlabel(char *, char *, nvlist_t **);

int
spa_check_rootconf(char *devpath, char *devid, nvlist_t **bestconf,
    uint64_t *besttxg)
{
	nvlist_t *config;
	uint64_t txg;
	int error;

	if (error = vdev_disk_read_rootlabel(devpath, devid, &config))
		return (error);

	VERIFY(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_TXG, &txg) == 0);

	if (bestconf != NULL)
		*bestconf = config;
	else
		nvlist_free(config);
	*besttxg = txg;
	return (0);
}

boolean_t
spa_rootdev_validate(nvlist_t *nv)
{
	uint64_t ival;

	if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_OFFLINE, &ival) == 0 ||
	    nvlist_lookup_uint64(nv, ZPOOL_CONFIG_FAULTED, &ival) == 0 ||
	    nvlist_lookup_uint64(nv, ZPOOL_CONFIG_REMOVED, &ival) == 0)
		return (B_FALSE);

	return (B_TRUE);
}


/*
 * Given the boot device's physical path or devid, check if the device
 * is in a valid state.  If so, return the configuration from the vdev
 * label.
 */
int
spa_get_rootconf(char *devpath, char *devid, nvlist_t **bestconf)
{
	nvlist_t *conf = NULL;
	uint64_t txg = 0;
	nvlist_t *nvtop, **child;
	char *type;
	char *bootpath = NULL;
	uint_t children, c;
	char *tmp;
	int error;

	if (devpath && ((tmp = strchr(devpath, ' ')) != NULL))
		*tmp = '\0';
	if (error = spa_check_rootconf(devpath, devid, &conf, &txg)) {
		cmn_err(CE_NOTE, "error reading device label");
		return (error);
	}
	if (txg == 0) {
		cmn_err(CE_NOTE, "this device is detached");
		nvlist_free(conf);
		return (EINVAL);
	}

	VERIFY(nvlist_lookup_nvlist(conf, ZPOOL_CONFIG_VDEV_TREE,
	    &nvtop) == 0);
	VERIFY(nvlist_lookup_string(nvtop, ZPOOL_CONFIG_TYPE, &type) == 0);

	if (strcmp(type, VDEV_TYPE_DISK) == 0) {
		if (spa_rootdev_validate(nvtop)) {
			goto out;
		} else {
			nvlist_free(conf);
			return (EINVAL);
		}
	}

	ASSERT(strcmp(type, VDEV_TYPE_MIRROR) == 0);

	VERIFY(nvlist_lookup_nvlist_array(nvtop, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) == 0);

	/*
	 * Go thru vdevs in the mirror to see if the given device
	 * has the most recent txg. Only the device with the most
	 * recent txg has valid information and should be booted.
	 */
	for (c = 0; c < children; c++) {
		char *cdevid, *cpath;
		uint64_t tmptxg;

		cpath = NULL;
		cdevid = NULL;
		if (nvlist_lookup_string(child[c], ZPOOL_CONFIG_PHYS_PATH,
		    &cpath) != 0 && nvlist_lookup_string(child[c],
		    ZPOOL_CONFIG_DEVID, &cdevid) != 0)
			return (EINVAL);
		if ((spa_check_rootconf(cpath, cdevid, NULL,
		    &tmptxg) == 0) && (tmptxg > txg)) {
			txg = tmptxg;
			VERIFY(nvlist_lookup_string(child[c],
			    ZPOOL_CONFIG_PATH, &bootpath) == 0);
		}
	}

	/* Does the best device match the one we've booted from? */
	if (bootpath) {
		cmn_err(CE_NOTE, "try booting from '%s'", bootpath);
		return (EINVAL);
	}
out:
	*bestconf = conf;
	return (0);
}

/*
 * Import a root pool.
 *
 * For x86. devpath_list will consist of devid and/or physpath name of
 * the vdev (e.g. "id1,sd@SSEAGATE..." or "/pci@1f,0/ide@d/disk@0,0:a").
 * The GRUB "findroot" command will return the vdev we should boot.
 *
 * For Sparc, devpath_list consists the physpath name of the booting device
 * no matter the rootpool is a single device pool or a mirrored pool.
 * e.g.
 *	"/pci@1f,0/ide@d/disk@0,0:a"
 */
int
spa_import_rootpool(char *devpath, char *devid)
{
	nvlist_t *conf = NULL;
	char *pname;
	int error;

	/*
	 * Get the vdev pathname and configuation from the most
	 * recently updated vdev (highest txg).
	 */
	if (error = spa_get_rootconf(devpath, devid, &conf))
		goto msg_out;

	/*
	 * Add type "root" vdev to the config.
	 */
	spa_build_rootpool_config(conf);

	VERIFY(nvlist_lookup_string(conf, ZPOOL_CONFIG_POOL_NAME, &pname) == 0);

	/*
	 * We specify 'allowfaulted' for this to be treated like spa_open()
	 * instead of spa_import().  This prevents us from marking vdevs as
	 * persistently unavailable, and generates FMA ereports as if it were a
	 * pool open, not import.
	 */
	error = spa_import_common(pname, conf, NULL, B_TRUE, B_TRUE);
	ASSERT(error != EEXIST);

	nvlist_free(conf);
	return (error);

msg_out:
	cmn_err(CE_NOTE, "\n"
	    "  ***************************************************  \n"
	    "  *  This device is not bootable!                   *  \n"
	    "  *  It is either offlined or detached or faulted.  *  \n"
	    "  *  Please try to boot from a different device.    *  \n"
	    "  ***************************************************  ");

	return (error);
}
#endif

/*
 * Import a non-root pool into the system.
 */
int
spa_import(const char *pool, nvlist_t *config, nvlist_t *props)
{
	return (spa_import_common(pool, config, props, B_FALSE, B_FALSE));
}

int
spa_import_faulted(const char *pool, nvlist_t *config, nvlist_t *props)
{
	return (spa_import_common(pool, config, props, B_FALSE, B_TRUE));
}


/*
 * This (illegal) pool name is used when temporarily importing a spa_t in order
 * to get the vdev stats associated with the imported devices.
 */
#define	TRYIMPORT_NAME	"$import"

nvlist_t *
spa_tryimport(nvlist_t *tryconfig)
{
	nvlist_t *config = NULL;
	char *poolname;
	spa_t *spa;
	uint64_t state;
	int error;

	if (nvlist_lookup_string(tryconfig, ZPOOL_CONFIG_POOL_NAME, &poolname))
		return (NULL);

	if (nvlist_lookup_uint64(tryconfig, ZPOOL_CONFIG_POOL_STATE, &state))
		return (NULL);

	/*
	 * Create and initialize the spa structure.
	 */
	mutex_enter(&spa_namespace_lock);
	spa = spa_add(TRYIMPORT_NAME, NULL);
	spa_activate(spa, FREAD);

	/*
	 * Pass off the heavy lifting to spa_load().
	 * Pass TRUE for mosconfig because the user-supplied config
	 * is actually the one to trust when doing an import.
	 */
	error = spa_load(spa, tryconfig, SPA_LOAD_TRYIMPORT, B_TRUE);

	/*
	 * If 'tryconfig' was at least parsable, return the current config.
	 */
	if (spa->spa_root_vdev != NULL) {
		config = spa_config_generate(spa, NULL, -1ULL, B_TRUE);
		VERIFY(nvlist_add_string(config, ZPOOL_CONFIG_POOL_NAME,
		    poolname) == 0);
		VERIFY(nvlist_add_uint64(config, ZPOOL_CONFIG_POOL_STATE,
		    state) == 0);
		VERIFY(nvlist_add_uint64(config, ZPOOL_CONFIG_TIMESTAMP,
		    spa->spa_uberblock.ub_timestamp) == 0);

		/*
		 * If the bootfs property exists on this pool then we
		 * copy it out so that external consumers can tell which
		 * pools are bootable.
		 */
		if ((!error || error == EEXIST) && spa->spa_bootfs) {
			char *tmpname = kmem_alloc(MAXPATHLEN, KM_SLEEP);

			/*
			 * We have to play games with the name since the
			 * pool was opened as TRYIMPORT_NAME.
			 */
			if (dsl_dsobj_to_dsname(spa_name(spa),
			    spa->spa_bootfs, tmpname) == 0) {
				char *cp;
				char *dsname = kmem_alloc(MAXPATHLEN, KM_SLEEP);

				cp = strchr(tmpname, '/');
				if (cp == NULL) {
					(void) strlcpy(dsname, tmpname,
					    MAXPATHLEN);
				} else {
					(void) snprintf(dsname, MAXPATHLEN,
					    "%s/%s", poolname, ++cp);
				}
				VERIFY(nvlist_add_string(config,
				    ZPOOL_CONFIG_BOOTFS, dsname) == 0);
				kmem_free(dsname, MAXPATHLEN);
			}
			kmem_free(tmpname, MAXPATHLEN);
		}

		/*
		 * Add the list of hot spares and level 2 cache devices.
		 */
		spa_add_spares(spa, config);
		spa_add_l2cache(spa, config);
	}

	spa_unload(spa);
	spa_deactivate(spa);
	spa_remove(spa);
	mutex_exit(&spa_namespace_lock);

	return (config);
}

/*
 * Pool export/destroy
 *
 * The act of destroying or exporting a pool is very simple.  We make sure there
 * is no more pending I/O and any references to the pool are gone.  Then, we
 * update the pool state and sync all the labels to disk, removing the
 * configuration from the cache afterwards. If the 'hardforce' flag is set, then
 * we don't sync the labels or remove the configuration cache.
 */
static int
spa_export_common(char *pool, int new_state, nvlist_t **oldconfig,
    boolean_t force, boolean_t hardforce)
{
	spa_t *spa;

	if (oldconfig)
		*oldconfig = NULL;

	if (!(spa_mode_global & FWRITE))
		return (EROFS);

	mutex_enter(&spa_namespace_lock);
	if ((spa = spa_lookup(pool)) == NULL) {
		mutex_exit(&spa_namespace_lock);
		return (ENOENT);
	}

	/*
	 * Put a hold on the pool, drop the namespace lock, stop async tasks,
	 * reacquire the namespace lock, and see if we can export.
	 */
	spa_open_ref(spa, FTAG);
	mutex_exit(&spa_namespace_lock);
	spa_async_suspend(spa);
	mutex_enter(&spa_namespace_lock);
	spa_close(spa, FTAG);

	/*
	 * The pool will be in core if it's openable,
	 * in which case we can modify its state.
	 */
	if (spa->spa_state != POOL_STATE_UNINITIALIZED && spa->spa_sync_on) {
		/*
		 * Objsets may be open only because they're dirty, so we
		 * have to force it to sync before checking spa_refcnt.
		 */
		txg_wait_synced(spa->spa_dsl_pool, 0);

		/*
		 * A pool cannot be exported or destroyed if there are active
		 * references.  If we are resetting a pool, allow references by
		 * fault injection handlers.
		 */
		if (!spa_refcount_zero(spa) ||
		    (spa->spa_inject_ref != 0 &&
		    new_state != POOL_STATE_UNINITIALIZED)) {
			spa_async_resume(spa);
			mutex_exit(&spa_namespace_lock);
			return (EBUSY);
		}

		/*
		 * A pool cannot be exported if it has an active shared spare.
		 * This is to prevent other pools stealing the active spare
		 * from an exported pool. At user's own will, such pool can
		 * be forcedly exported.
		 */
		if (!force && new_state == POOL_STATE_EXPORTED &&
		    spa_has_active_shared_spare(spa)) {
			spa_async_resume(spa);
			mutex_exit(&spa_namespace_lock);
			return (EXDEV);
		}

		/*
		 * We want this to be reflected on every label,
		 * so mark them all dirty.  spa_unload() will do the
		 * final sync that pushes these changes out.
		 */
		if (new_state != POOL_STATE_UNINITIALIZED && !hardforce) {
			spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
			spa->spa_state = new_state;
			spa->spa_final_txg = spa_last_synced_txg(spa) + 1;
			vdev_config_dirty(spa->spa_root_vdev);
			spa_config_exit(spa, SCL_ALL, FTAG);
		}
	}

	spa_event_notify(spa, NULL, ESC_ZFS_POOL_DESTROY);

	if (spa->spa_state != POOL_STATE_UNINITIALIZED) {
		spa_unload(spa);
		spa_deactivate(spa);
	}

	if (oldconfig && spa->spa_config)
		VERIFY(nvlist_dup(spa->spa_config, oldconfig, 0) == 0);

	if (new_state != POOL_STATE_UNINITIALIZED) {
		if (!hardforce)
			spa_config_sync(spa, B_TRUE, B_TRUE);
		spa_remove(spa);
	}
	mutex_exit(&spa_namespace_lock);

	return (0);
}

/*
 * Destroy a storage pool.
 */
int
spa_destroy(char *pool)
{
	return (spa_export_common(pool, POOL_STATE_DESTROYED, NULL,
	    B_FALSE, B_FALSE));
}

/*
 * Export a storage pool.
 */
int
spa_export(char *pool, nvlist_t **oldconfig, boolean_t force,
    boolean_t hardforce)
{
	return (spa_export_common(pool, POOL_STATE_EXPORTED, oldconfig,
	    force, hardforce));
}

/*
 * Similar to spa_export(), this unloads the spa_t without actually removing it
 * from the namespace in any way.
 */
int
spa_reset(char *pool)
{
	return (spa_export_common(pool, POOL_STATE_UNINITIALIZED, NULL,
	    B_FALSE, B_FALSE));
}

/*
 * ==========================================================================
 * Device manipulation
 * ==========================================================================
 */

/*
 * Add a device to a storage pool.
 */
int
spa_vdev_add(spa_t *spa, nvlist_t *nvroot)
{
	uint64_t txg;
	int error;
	vdev_t *rvd = spa->spa_root_vdev;
	vdev_t *vd, *tvd;
	nvlist_t **spares, **l2cache;
	uint_t nspares, nl2cache;

	txg = spa_vdev_enter(spa);

	if ((error = spa_config_parse(spa, &vd, nvroot, NULL, 0,
	    VDEV_ALLOC_ADD)) != 0)
		return (spa_vdev_exit(spa, NULL, txg, error));

	spa->spa_pending_vdev = vd;	/* spa_vdev_exit() will clear this */

	if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_SPARES, &spares,
	    &nspares) != 0)
		nspares = 0;

	if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_L2CACHE, &l2cache,
	    &nl2cache) != 0)
		nl2cache = 0;

	if (vd->vdev_children == 0 && nspares == 0 && nl2cache == 0)
		return (spa_vdev_exit(spa, vd, txg, EINVAL));

	if (vd->vdev_children != 0 &&
	    (error = vdev_create(vd, txg, B_FALSE)) != 0)
		return (spa_vdev_exit(spa, vd, txg, error));

	/*
	 * We must validate the spares and l2cache devices after checking the
	 * children.  Otherwise, vdev_inuse() will blindly overwrite the spare.
	 */
	if ((error = spa_validate_aux(spa, nvroot, txg, VDEV_ALLOC_ADD)) != 0)
		return (spa_vdev_exit(spa, vd, txg, error));

	/*
	 * Transfer each new top-level vdev from vd to rvd.
	 */
	for (int c = 0; c < vd->vdev_children; c++) {
		tvd = vd->vdev_child[c];
		vdev_remove_child(vd, tvd);
		tvd->vdev_id = rvd->vdev_children;
		vdev_add_child(rvd, tvd);
		vdev_config_dirty(tvd);
	}

	if (nspares != 0) {
		spa_set_aux_vdevs(&spa->spa_spares, spares, nspares,
		    ZPOOL_CONFIG_SPARES);
		spa_load_spares(spa);
		spa->spa_spares.sav_sync = B_TRUE;
	}

	if (nl2cache != 0) {
		spa_set_aux_vdevs(&spa->spa_l2cache, l2cache, nl2cache,
		    ZPOOL_CONFIG_L2CACHE);
		spa_load_l2cache(spa);
		spa->spa_l2cache.sav_sync = B_TRUE;
	}

	/*
	 * We have to be careful when adding new vdevs to an existing pool.
	 * If other threads start allocating from these vdevs before we
	 * sync the config cache, and we lose power, then upon reboot we may
	 * fail to open the pool because there are DVAs that the config cache
	 * can't translate.  Therefore, we first add the vdevs without
	 * initializing metaslabs; sync the config cache (via spa_vdev_exit());
	 * and then let spa_config_update() initialize the new metaslabs.
	 *
	 * spa_load() checks for added-but-not-initialized vdevs, so that
	 * if we lose power at any point in this sequence, the remaining
	 * steps will be completed the next time we load the pool.
	 */
	(void) spa_vdev_exit(spa, vd, txg, 0);

	mutex_enter(&spa_namespace_lock);
	spa_config_update(spa, SPA_CONFIG_UPDATE_POOL);
	mutex_exit(&spa_namespace_lock);

	return (0);
}

/*
 * Attach a device to a mirror.  The arguments are the path to any device
 * in the mirror, and the nvroot for the new device.  If the path specifies
 * a device that is not mirrored, we automatically insert the mirror vdev.
 *
 * If 'replacing' is specified, the new device is intended to replace the
 * existing device; in this case the two devices are made into their own
 * mirror using the 'replacing' vdev, which is functionally identical to
 * the mirror vdev (it actually reuses all the same ops) but has a few
 * extra rules: you can't attach to it after it's been created, and upon
 * completion of resilvering, the first disk (the one being replaced)
 * is automatically detached.
 */
int
spa_vdev_attach(spa_t *spa, uint64_t guid, nvlist_t *nvroot, int replacing)
{
	uint64_t txg, open_txg;
	vdev_t *rvd = spa->spa_root_vdev;
	vdev_t *oldvd, *newvd, *newrootvd, *pvd, *tvd;
	vdev_ops_t *pvops;
	dmu_tx_t *tx;
	char *oldvdpath, *newvdpath;
	int newvd_isspare;
	int error;

	txg = spa_vdev_enter(spa);

	oldvd = spa_lookup_by_guid(spa, guid, B_FALSE);

	if (oldvd == NULL)
		return (spa_vdev_exit(spa, NULL, txg, ENODEV));

	if (!oldvd->vdev_ops->vdev_op_leaf)
		return (spa_vdev_exit(spa, NULL, txg, ENOTSUP));

	pvd = oldvd->vdev_parent;

	if ((error = spa_config_parse(spa, &newrootvd, nvroot, NULL, 0,
	    VDEV_ALLOC_ADD)) != 0)
		return (spa_vdev_exit(spa, NULL, txg, EINVAL));

	if (newrootvd->vdev_children != 1)
		return (spa_vdev_exit(spa, newrootvd, txg, EINVAL));

	newvd = newrootvd->vdev_child[0];

	if (!newvd->vdev_ops->vdev_op_leaf)
		return (spa_vdev_exit(spa, newrootvd, txg, EINVAL));

	if ((error = vdev_create(newrootvd, txg, replacing)) != 0)
		return (spa_vdev_exit(spa, newrootvd, txg, error));

	/*
	 * Spares can't replace logs
	 */
	if (oldvd->vdev_top->vdev_islog && newvd->vdev_isspare)
		return (spa_vdev_exit(spa, newrootvd, txg, ENOTSUP));

	if (!replacing) {
		/*
		 * For attach, the only allowable parent is a mirror or the root
		 * vdev.
		 */
		if (pvd->vdev_ops != &vdev_mirror_ops &&
		    pvd->vdev_ops != &vdev_root_ops)
			return (spa_vdev_exit(spa, newrootvd, txg, ENOTSUP));

		pvops = &vdev_mirror_ops;
	} else {
		/*
		 * Active hot spares can only be replaced by inactive hot
		 * spares.
		 */
		if (pvd->vdev_ops == &vdev_spare_ops &&
		    pvd->vdev_child[1] == oldvd &&
		    !spa_has_spare(spa, newvd->vdev_guid))
			return (spa_vdev_exit(spa, newrootvd, txg, ENOTSUP));

		/*
		 * If the source is a hot spare, and the parent isn't already a
		 * spare, then we want to create a new hot spare.  Otherwise, we
		 * want to create a replacing vdev.  The user is not allowed to
		 * attach to a spared vdev child unless the 'isspare' state is
		 * the same (spare replaces spare, non-spare replaces
		 * non-spare).
		 */
		if (pvd->vdev_ops == &vdev_replacing_ops)
			return (spa_vdev_exit(spa, newrootvd, txg, ENOTSUP));
		else if (pvd->vdev_ops == &vdev_spare_ops &&
		    newvd->vdev_isspare != oldvd->vdev_isspare)
			return (spa_vdev_exit(spa, newrootvd, txg, ENOTSUP));
		else if (pvd->vdev_ops != &vdev_spare_ops &&
		    newvd->vdev_isspare)
			pvops = &vdev_spare_ops;
		else
			pvops = &vdev_replacing_ops;
	}

	/*
	 * Compare the new device size with the replaceable/attachable
	 * device size.
	 */
	if (newvd->vdev_psize < vdev_get_rsize(oldvd))
		return (spa_vdev_exit(spa, newrootvd, txg, EOVERFLOW));

	/*
	 * The new device cannot have a higher alignment requirement
	 * than the top-level vdev.
	 */
	if (newvd->vdev_ashift > oldvd->vdev_top->vdev_ashift)
		return (spa_vdev_exit(spa, newrootvd, txg, EDOM));

	/*
	 * If this is an in-place replacement, update oldvd's path and devid
	 * to make it distinguishable from newvd, and unopenable from now on.
	 */
	if (strcmp(oldvd->vdev_path, newvd->vdev_path) == 0) {
		spa_strfree(oldvd->vdev_path);
		oldvd->vdev_path = kmem_alloc(strlen(newvd->vdev_path) + 5,
		    KM_SLEEP);
		(void) sprintf(oldvd->vdev_path, "%s/%s",
		    newvd->vdev_path, "old");
		if (oldvd->vdev_devid != NULL) {
			spa_strfree(oldvd->vdev_devid);
			oldvd->vdev_devid = NULL;
		}
	}

	/*
	 * If the parent is not a mirror, or if we're replacing, insert the new
	 * mirror/replacing/spare vdev above oldvd.
	 */
	if (pvd->vdev_ops != pvops)
		pvd = vdev_add_parent(oldvd, pvops);

	ASSERT(pvd->vdev_top->vdev_parent == rvd);
	ASSERT(pvd->vdev_ops == pvops);
	ASSERT(oldvd->vdev_parent == pvd);

	/*
	 * Extract the new device from its root and add it to pvd.
	 */
	vdev_remove_child(newrootvd, newvd);
	newvd->vdev_id = pvd->vdev_children;
	vdev_add_child(pvd, newvd);

	/*
	 * If newvd is smaller than oldvd, but larger than its rsize,
	 * the addition of newvd may have decreased our parent's asize.
	 */
	pvd->vdev_asize = MIN(pvd->vdev_asize, newvd->vdev_asize);

	tvd = newvd->vdev_top;
	ASSERT(pvd->vdev_top == tvd);
	ASSERT(tvd->vdev_parent == rvd);

	vdev_config_dirty(tvd);

	/*
	 * Set newvd's DTL to [TXG_INITIAL, open_txg].  It will propagate
	 * upward when spa_vdev_exit() calls vdev_dtl_reassess().
	 */
	open_txg = txg + TXG_CONCURRENT_STATES - 1;

	vdev_dtl_dirty(newvd, DTL_MISSING,
	    TXG_INITIAL, open_txg - TXG_INITIAL + 1);

	if (newvd->vdev_isspare)
		spa_spare_activate(newvd);
	oldvdpath = spa_strdup(oldvd->vdev_path);
	newvdpath = spa_strdup(newvd->vdev_path);
	newvd_isspare = newvd->vdev_isspare;

	/*
	 * Mark newvd's DTL dirty in this txg.
	 */
	vdev_dirty(tvd, VDD_DTL, newvd, txg);

	(void) spa_vdev_exit(spa, newrootvd, open_txg, 0);

	tx = dmu_tx_create_dd(spa_get_dsl(spa)->dp_mos_dir);
	if (dmu_tx_assign(tx, TXG_WAIT) == 0) {
		spa_history_internal_log(LOG_POOL_VDEV_ATTACH, spa, tx,
		    CRED(),  "%s vdev=%s %s vdev=%s",
		    replacing && newvd_isspare ? "spare in" :
		    replacing ? "replace" : "attach", newvdpath,
		    replacing ? "for" : "to", oldvdpath);
		dmu_tx_commit(tx);
	} else {
		dmu_tx_abort(tx);
	}

	spa_strfree(oldvdpath);
	spa_strfree(newvdpath);

	/*
	 * Kick off a resilver to update newvd.
	 */
	VERIFY3U(spa_scrub(spa, POOL_SCRUB_RESILVER), ==, 0);

	return (0);
}

/*
 * Detach a device from a mirror or replacing vdev.
 * If 'replace_done' is specified, only detach if the parent
 * is a replacing vdev.
 */
int
spa_vdev_detach(spa_t *spa, uint64_t guid, uint64_t pguid, int replace_done)
{
	uint64_t txg;
	int error;
	vdev_t *rvd = spa->spa_root_vdev;
	vdev_t *vd, *pvd, *cvd, *tvd;
	boolean_t unspare = B_FALSE;
	uint64_t unspare_guid;
	size_t len;

	txg = spa_vdev_enter(spa);

	vd = spa_lookup_by_guid(spa, guid, B_FALSE);

	if (vd == NULL)
		return (spa_vdev_exit(spa, NULL, txg, ENODEV));

	if (!vd->vdev_ops->vdev_op_leaf)
		return (spa_vdev_exit(spa, NULL, txg, ENOTSUP));

	pvd = vd->vdev_parent;

	/*
	 * If the parent/child relationship is not as expected, don't do it.
	 * Consider M(A,R(B,C)) -- that is, a mirror of A with a replacing
	 * vdev that's replacing B with C.  The user's intent in replacing
	 * is to go from M(A,B) to M(A,C).  If the user decides to cancel
	 * the replace by detaching C, the expected behavior is to end up
	 * M(A,B).  But suppose that right after deciding to detach C,
	 * the replacement of B completes.  We would have M(A,C), and then
	 * ask to detach C, which would leave us with just A -- not what
	 * the user wanted.  To prevent this, we make sure that the
	 * parent/child relationship hasn't changed -- in this example,
	 * that C's parent is still the replacing vdev R.
	 */
	if (pvd->vdev_guid != pguid && pguid != 0)
		return (spa_vdev_exit(spa, NULL, txg, EBUSY));

	/*
	 * If replace_done is specified, only remove this device if it's
	 * the first child of a replacing vdev.  For the 'spare' vdev, either
	 * disk can be removed.
	 */
	if (replace_done) {
		if (pvd->vdev_ops == &vdev_replacing_ops) {
			if (vd->vdev_id != 0)
				return (spa_vdev_exit(spa, NULL, txg, ENOTSUP));
		} else if (pvd->vdev_ops != &vdev_spare_ops) {
			return (spa_vdev_exit(spa, NULL, txg, ENOTSUP));
		}
	}

	ASSERT(pvd->vdev_ops != &vdev_spare_ops ||
	    spa_version(spa) >= SPA_VERSION_SPARES);

	/*
	 * Only mirror, replacing, and spare vdevs support detach.
	 */
	if (pvd->vdev_ops != &vdev_replacing_ops &&
	    pvd->vdev_ops != &vdev_mirror_ops &&
	    pvd->vdev_ops != &vdev_spare_ops)
		return (spa_vdev_exit(spa, NULL, txg, ENOTSUP));

	/*
	 * If this device has the only valid copy of some data,
	 * we cannot safely detach it.
	 */
	if (vdev_dtl_required(vd))
		return (spa_vdev_exit(spa, NULL, txg, EBUSY));

	ASSERT(pvd->vdev_children >= 2);

	/*
	 * If we are detaching the second disk from a replacing vdev, then
	 * check to see if we changed the original vdev's path to have "/old"
	 * at the end in spa_vdev_attach().  If so, undo that change now.
	 */
	if (pvd->vdev_ops == &vdev_replacing_ops && vd->vdev_id == 1 &&
	    pvd->vdev_child[0]->vdev_path != NULL &&
	    pvd->vdev_child[1]->vdev_path != NULL) {
		ASSERT(pvd->vdev_child[1] == vd);
		cvd = pvd->vdev_child[0];
		len = strlen(vd->vdev_path);
		if (strncmp(cvd->vdev_path, vd->vdev_path, len) == 0 &&
		    strcmp(cvd->vdev_path + len, "/old") == 0) {
			spa_strfree(cvd->vdev_path);
			cvd->vdev_path = spa_strdup(vd->vdev_path);
		}
	}

	/*
	 * If we are detaching the original disk from a spare, then it implies
	 * that the spare should become a real disk, and be removed from the
	 * active spare list for the pool.
	 */
	if (pvd->vdev_ops == &vdev_spare_ops &&
	    vd->vdev_id == 0 && pvd->vdev_child[1]->vdev_isspare)
		unspare = B_TRUE;

	/*
	 * Erase the disk labels so the disk can be used for other things.
	 * This must be done after all other error cases are handled,
	 * but before we disembowel vd (so we can still do I/O to it).
	 * But if we can't do it, don't treat the error as fatal --
	 * it may be that the unwritability of the disk is the reason
	 * it's being detached!
	 */
	error = vdev_label_init(vd, 0, VDEV_LABEL_REMOVE);

	/*
	 * Remove vd from its parent and compact the parent's children.
	 */
	vdev_remove_child(pvd, vd);
	vdev_compact_children(pvd);

	/*
	 * Remember one of the remaining children so we can get tvd below.
	 */
	cvd = pvd->vdev_child[0];

	/*
	 * If we need to remove the remaining child from the list of hot spares,
	 * do it now, marking the vdev as no longer a spare in the process.
	 * We must do this before vdev_remove_parent(), because that can
	 * change the GUID if it creates a new toplevel GUID.  For a similar
	 * reason, we must remove the spare now, in the same txg as the detach;
	 * otherwise someone could attach a new sibling, change the GUID, and
	 * the subsequent attempt to spa_vdev_remove(unspare_guid) would fail.
	 */
	if (unspare) {
		ASSERT(cvd->vdev_isspare);
		spa_spare_remove(cvd);
		unspare_guid = cvd->vdev_guid;
		(void) spa_vdev_remove(spa, unspare_guid, B_TRUE);
	}

	/*
	 * If the parent mirror/replacing vdev only has one child,
	 * the parent is no longer needed.  Remove it from the tree.
	 */
	if (pvd->vdev_children == 1)
		vdev_remove_parent(cvd);

	/*
	 * We don't set tvd until now because the parent we just removed
	 * may have been the previous top-level vdev.
	 */
	tvd = cvd->vdev_top;
	ASSERT(tvd->vdev_parent == rvd);

	/*
	 * Reevaluate the parent vdev state.
	 */
	vdev_propagate_state(cvd);

	/*
	 * If the device we just detached was smaller than the others, it may be
	 * possible to add metaslabs (i.e. grow the pool).  vdev_metaslab_init()
	 * can't fail because the existing metaslabs are already in core, so
	 * there's nothing to read from disk.
	 */
	VERIFY(vdev_metaslab_init(tvd, txg) == 0);

	vdev_config_dirty(tvd);

	/*
	 * Mark vd's DTL as dirty in this txg.  vdev_dtl_sync() will see that
	 * vd->vdev_detached is set and free vd's DTL object in syncing context.
	 * But first make sure we're not on any *other* txg's DTL list, to
	 * prevent vd from being accessed after it's freed.
	 */
	for (int t = 0; t < TXG_SIZE; t++)
		(void) txg_list_remove_this(&tvd->vdev_dtl_list, vd, t);
	vd->vdev_detached = B_TRUE;
	vdev_dirty(tvd, VDD_DTL, vd, txg);

	spa_event_notify(spa, vd, ESC_ZFS_VDEV_REMOVE);

	error = spa_vdev_exit(spa, vd, txg, 0);

	/*
	 * If this was the removal of the original device in a hot spare vdev,
	 * then we want to go through and remove the device from the hot spare
	 * list of every other pool.
	 */
	if (unspare) {
		spa_t *myspa = spa;
		spa = NULL;
		mutex_enter(&spa_namespace_lock);
		while ((spa = spa_next(spa)) != NULL) {
			if (spa->spa_state != POOL_STATE_ACTIVE)
				continue;
			if (spa == myspa)
				continue;
			spa_open_ref(spa, FTAG);
			mutex_exit(&spa_namespace_lock);
			(void) spa_vdev_remove(spa, unspare_guid, B_TRUE);
			mutex_enter(&spa_namespace_lock);
			spa_close(spa, FTAG);
		}
		mutex_exit(&spa_namespace_lock);
	}

	return (error);
}

static nvlist_t *
spa_nvlist_lookup_by_guid(nvlist_t **nvpp, int count, uint64_t target_guid)
{
	for (int i = 0; i < count; i++) {
		uint64_t guid;

		VERIFY(nvlist_lookup_uint64(nvpp[i], ZPOOL_CONFIG_GUID,
		    &guid) == 0);

		if (guid == target_guid)
			return (nvpp[i]);
	}

	return (NULL);
}

static void
spa_vdev_remove_aux(nvlist_t *config, char *name, nvlist_t **dev, int count,
	nvlist_t *dev_to_remove)
{
	nvlist_t **newdev = NULL;

	if (count > 1)
		newdev = kmem_alloc((count - 1) * sizeof (void *), KM_SLEEP);

	for (int i = 0, j = 0; i < count; i++) {
		if (dev[i] == dev_to_remove)
			continue;
		VERIFY(nvlist_dup(dev[i], &newdev[j++], KM_SLEEP) == 0);
	}

	VERIFY(nvlist_remove(config, name, DATA_TYPE_NVLIST_ARRAY) == 0);
	VERIFY(nvlist_add_nvlist_array(config, name, newdev, count - 1) == 0);

	for (int i = 0; i < count - 1; i++)
		nvlist_free(newdev[i]);

	if (count > 1)
		kmem_free(newdev, (count - 1) * sizeof (void *));
}

/*
 * Remove a device from the pool.  Currently, this supports removing only hot
 * spares and level 2 ARC devices.
 */
int
spa_vdev_remove(spa_t *spa, uint64_t guid, boolean_t unspare)
{
	vdev_t *vd;
	nvlist_t **spares, **l2cache, *nv;
	uint_t nspares, nl2cache;
	uint64_t txg = 0;
	int error = 0;
	boolean_t locked = MUTEX_HELD(&spa_namespace_lock);

	if (!locked)
		txg = spa_vdev_enter(spa);

	vd = spa_lookup_by_guid(spa, guid, B_FALSE);

	if (spa->spa_spares.sav_vdevs != NULL &&
	    nvlist_lookup_nvlist_array(spa->spa_spares.sav_config,
	    ZPOOL_CONFIG_SPARES, &spares, &nspares) == 0 &&
	    (nv = spa_nvlist_lookup_by_guid(spares, nspares, guid)) != NULL) {
		/*
		 * Only remove the hot spare if it's not currently in use
		 * in this pool.
		 */
		if (vd == NULL || unspare) {
			spa_vdev_remove_aux(spa->spa_spares.sav_config,
			    ZPOOL_CONFIG_SPARES, spares, nspares, nv);
			spa_load_spares(spa);
			spa->spa_spares.sav_sync = B_TRUE;
		} else {
			error = EBUSY;
		}
	} else if (spa->spa_l2cache.sav_vdevs != NULL &&
	    nvlist_lookup_nvlist_array(spa->spa_l2cache.sav_config,
	    ZPOOL_CONFIG_L2CACHE, &l2cache, &nl2cache) == 0 &&
	    (nv = spa_nvlist_lookup_by_guid(l2cache, nl2cache, guid)) != NULL) {
		/*
		 * Cache devices can always be removed.
		 */
		spa_vdev_remove_aux(spa->spa_l2cache.sav_config,
		    ZPOOL_CONFIG_L2CACHE, l2cache, nl2cache, nv);
		spa_load_l2cache(spa);
		spa->spa_l2cache.sav_sync = B_TRUE;
	} else if (vd != NULL) {
		/*
		 * Normal vdevs cannot be removed (yet).
		 */
		error = ENOTSUP;
	} else {
		/*
		 * There is no vdev of any kind with the specified guid.
		 */
		error = ENOENT;
	}

	if (!locked)
		return (spa_vdev_exit(spa, NULL, txg, error));

	return (error);
}

/*
 * Find any device that's done replacing, or a vdev marked 'unspare' that's
 * current spared, so we can detach it.
 */
static vdev_t *
spa_vdev_resilver_done_hunt(vdev_t *vd)
{
	vdev_t *newvd, *oldvd;
	int c;

	for (c = 0; c < vd->vdev_children; c++) {
		oldvd = spa_vdev_resilver_done_hunt(vd->vdev_child[c]);
		if (oldvd != NULL)
			return (oldvd);
	}

	/*
	 * Check for a completed replacement.
	 */
	if (vd->vdev_ops == &vdev_replacing_ops && vd->vdev_children == 2) {
		oldvd = vd->vdev_child[0];
		newvd = vd->vdev_child[1];

		if (vdev_dtl_empty(newvd, DTL_MISSING) &&
		    !vdev_dtl_required(oldvd))
			return (oldvd);
	}

	/*
	 * Check for a completed resilver with the 'unspare' flag set.
	 */
	if (vd->vdev_ops == &vdev_spare_ops && vd->vdev_children == 2) {
		newvd = vd->vdev_child[0];
		oldvd = vd->vdev_child[1];

		if (newvd->vdev_unspare &&
		    vdev_dtl_empty(newvd, DTL_MISSING) &&
		    !vdev_dtl_required(oldvd)) {
			newvd->vdev_unspare = 0;
			return (oldvd);
		}
	}

	return (NULL);
}

static void
spa_vdev_resilver_done(spa_t *spa)
{
	vdev_t *vd, *pvd, *ppvd;
	uint64_t guid, sguid, pguid, ppguid;

	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);

	while ((vd = spa_vdev_resilver_done_hunt(spa->spa_root_vdev)) != NULL) {
		pvd = vd->vdev_parent;
		ppvd = pvd->vdev_parent;
		guid = vd->vdev_guid;
		pguid = pvd->vdev_guid;
		ppguid = ppvd->vdev_guid;
		sguid = 0;
		/*
		 * If we have just finished replacing a hot spared device, then
		 * we need to detach the parent's first child (the original hot
		 * spare) as well.
		 */
		if (ppvd->vdev_ops == &vdev_spare_ops && pvd->vdev_id == 0) {
			ASSERT(pvd->vdev_ops == &vdev_replacing_ops);
			ASSERT(ppvd->vdev_children == 2);
			sguid = ppvd->vdev_child[1]->vdev_guid;
		}
		spa_config_exit(spa, SCL_ALL, FTAG);
		if (spa_vdev_detach(spa, guid, pguid, B_TRUE) != 0)
			return;
		if (sguid && spa_vdev_detach(spa, sguid, ppguid, B_TRUE) != 0)
			return;
		spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
	}

	spa_config_exit(spa, SCL_ALL, FTAG);
}

/*
 * Update the stored path for this vdev.  Dirty the vdev configuration, relying
 * on spa_vdev_enter/exit() to synchronize the labels and cache.
 */
int
spa_vdev_setpath(spa_t *spa, uint64_t guid, const char *newpath)
{
	vdev_t *vd;
	uint64_t txg;

	txg = spa_vdev_enter(spa);

	if ((vd = spa_lookup_by_guid(spa, guid, B_TRUE)) == NULL) {
		/*
		 * Determine if this is a reference to a hot spare device.  If
		 * it is, update the path manually as there is no associated
		 * vdev_t that can be synced to disk.
		 */
		nvlist_t **spares;
		uint_t i, nspares;

		if (spa->spa_spares.sav_config != NULL) {
			VERIFY(nvlist_lookup_nvlist_array(
			    spa->spa_spares.sav_config, ZPOOL_CONFIG_SPARES,
			    &spares, &nspares) == 0);
			for (i = 0; i < nspares; i++) {
				uint64_t theguid;
				VERIFY(nvlist_lookup_uint64(spares[i],
				    ZPOOL_CONFIG_GUID, &theguid) == 0);
				if (theguid == guid) {
					VERIFY(nvlist_add_string(spares[i],
					    ZPOOL_CONFIG_PATH, newpath) == 0);
					spa_load_spares(spa);
					spa->spa_spares.sav_sync = B_TRUE;
					return (spa_vdev_exit(spa, NULL, txg,
					    0));
				}
			}
		}

		return (spa_vdev_exit(spa, NULL, txg, ENOENT));
	}

	if (!vd->vdev_ops->vdev_op_leaf)
		return (spa_vdev_exit(spa, NULL, txg, ENOTSUP));

	spa_strfree(vd->vdev_path);
	vd->vdev_path = spa_strdup(newpath);

	vdev_config_dirty(vd->vdev_top);

	return (spa_vdev_exit(spa, NULL, txg, 0));
}

/*
 * ==========================================================================
 * SPA Scrubbing
 * ==========================================================================
 */

int
spa_scrub(spa_t *spa, pool_scrub_type_t type)
{
	ASSERT(spa_config_held(spa, SCL_ALL, RW_WRITER) == 0);

	if ((uint_t)type >= POOL_SCRUB_TYPES)
		return (ENOTSUP);

	/*
	 * If a resilver was requested, but there is no DTL on a
	 * writeable leaf device, we have nothing to do.
	 */
	if (type == POOL_SCRUB_RESILVER &&
	    !vdev_resilver_needed(spa->spa_root_vdev, NULL, NULL)) {
		spa_async_request(spa, SPA_ASYNC_RESILVER_DONE);
		return (0);
	}

	if (type == POOL_SCRUB_EVERYTHING &&
	    spa->spa_dsl_pool->dp_scrub_func != SCRUB_FUNC_NONE &&
	    spa->spa_dsl_pool->dp_scrub_isresilver)
		return (EBUSY);

	if (type == POOL_SCRUB_EVERYTHING || type == POOL_SCRUB_RESILVER) {
		return (dsl_pool_scrub_clean(spa->spa_dsl_pool));
	} else if (type == POOL_SCRUB_NONE) {
		return (dsl_pool_scrub_cancel(spa->spa_dsl_pool));
	} else {
		return (EINVAL);
	}
}

/*
 * ==========================================================================
 * SPA async task processing
 * ==========================================================================
 */

static void
spa_async_remove(spa_t *spa, vdev_t *vd)
{
	if (vd->vdev_remove_wanted) {
		vd->vdev_remove_wanted = 0;
		vdev_set_state(vd, B_FALSE, VDEV_STATE_REMOVED, VDEV_AUX_NONE);
		vdev_clear(spa, vd);
		vdev_state_dirty(vd->vdev_top);
	}

	for (int c = 0; c < vd->vdev_children; c++)
		spa_async_remove(spa, vd->vdev_child[c]);
}

static void
spa_async_probe(spa_t *spa, vdev_t *vd)
{
	if (vd->vdev_probe_wanted) {
		vd->vdev_probe_wanted = 0;
		vdev_reopen(vd);	/* vdev_open() does the actual probe */
	}

	for (int c = 0; c < vd->vdev_children; c++)
		spa_async_probe(spa, vd->vdev_child[c]);
}

static void
spa_async_thread(spa_t *spa)
{
	int tasks;

	ASSERT(spa->spa_sync_on);

	mutex_enter(&spa->spa_async_lock);
	tasks = spa->spa_async_tasks;
	spa->spa_async_tasks = 0;
	mutex_exit(&spa->spa_async_lock);

	/*
	 * See if the config needs to be updated.
	 */
	if (tasks & SPA_ASYNC_CONFIG_UPDATE) {
		mutex_enter(&spa_namespace_lock);
		spa_config_update(spa, SPA_CONFIG_UPDATE_POOL);
		mutex_exit(&spa_namespace_lock);
	}

	/*
	 * See if any devices need to be marked REMOVED.
	 */
	if (tasks & SPA_ASYNC_REMOVE) {
		spa_vdev_state_enter(spa);
		spa_async_remove(spa, spa->spa_root_vdev);
		for (int i = 0; i < spa->spa_l2cache.sav_count; i++)
			spa_async_remove(spa, spa->spa_l2cache.sav_vdevs[i]);
		for (int i = 0; i < spa->spa_spares.sav_count; i++)
			spa_async_remove(spa, spa->spa_spares.sav_vdevs[i]);
		(void) spa_vdev_state_exit(spa, NULL, 0);
	}

	/*
	 * See if any devices need to be probed.
	 */
	if (tasks & SPA_ASYNC_PROBE) {
		spa_vdev_state_enter(spa);
		spa_async_probe(spa, spa->spa_root_vdev);
		(void) spa_vdev_state_exit(spa, NULL, 0);
	}

	/*
	 * If any devices are done replacing, detach them.
	 */
	if (tasks & SPA_ASYNC_RESILVER_DONE)
		spa_vdev_resilver_done(spa);

	/*
	 * Kick off a resilver.
	 */
	if (tasks & SPA_ASYNC_RESILVER)
		VERIFY(spa_scrub(spa, POOL_SCRUB_RESILVER) == 0);

	/*
	 * Let the world know that we're done.
	 */
	mutex_enter(&spa->spa_async_lock);
	spa->spa_async_thread = NULL;
	cv_broadcast(&spa->spa_async_cv);
	mutex_exit(&spa->spa_async_lock);
	thread_exit();
}

void
spa_async_suspend(spa_t *spa)
{
	mutex_enter(&spa->spa_async_lock);
	spa->spa_async_suspended++;
	while (spa->spa_async_thread != NULL)
		cv_wait(&spa->spa_async_cv, &spa->spa_async_lock);
	mutex_exit(&spa->spa_async_lock);
}

void
spa_async_resume(spa_t *spa)
{
	mutex_enter(&spa->spa_async_lock);
	ASSERT(spa->spa_async_suspended != 0);
	spa->spa_async_suspended--;
	mutex_exit(&spa->spa_async_lock);
}

static void
spa_async_dispatch(spa_t *spa)
{
	mutex_enter(&spa->spa_async_lock);
	if (spa->spa_async_tasks && !spa->spa_async_suspended &&
	    spa->spa_async_thread == NULL &&
	    rootdir != NULL && !vn_is_readonly(rootdir))
		spa->spa_async_thread = thread_create(NULL, 0,
		    spa_async_thread, spa, 0, &p0, TS_RUN, maxclsyspri);
	mutex_exit(&spa->spa_async_lock);
}

void
spa_async_request(spa_t *spa, int task)
{
	mutex_enter(&spa->spa_async_lock);
	spa->spa_async_tasks |= task;
	mutex_exit(&spa->spa_async_lock);
}

/*
 * ==========================================================================
 * SPA syncing routines
 * ==========================================================================
 */

static void
spa_sync_deferred_frees(spa_t *spa, uint64_t txg)
{
	bplist_t *bpl = &spa->spa_sync_bplist;
	dmu_tx_t *tx;
	blkptr_t blk;
	uint64_t itor = 0;
	zio_t *zio;
	int error;
	uint8_t c = 1;

	zio = zio_root(spa, NULL, NULL, ZIO_FLAG_CANFAIL);

	while (bplist_iterate(bpl, &itor, &blk) == 0) {
		ASSERT(blk.blk_birth < txg);
		zio_nowait(zio_free(zio, spa, txg, &blk, NULL, NULL,
		    ZIO_FLAG_MUSTSUCCEED));
	}

	error = zio_wait(zio);
	ASSERT3U(error, ==, 0);

	tx = dmu_tx_create_assigned(spa->spa_dsl_pool, txg);
	bplist_vacate(bpl, tx);

	/*
	 * Pre-dirty the first block so we sync to convergence faster.
	 * (Usually only the first block is needed.)
	 */
	dmu_write(spa->spa_meta_objset, spa->spa_sync_bplist_obj, 0, 1, &c, tx);
	dmu_tx_commit(tx);
}

static void
spa_sync_nvlist(spa_t *spa, uint64_t obj, nvlist_t *nv, dmu_tx_t *tx)
{
	char *packed = NULL;
	size_t bufsize;
	size_t nvsize = 0;
	dmu_buf_t *db;

	VERIFY(nvlist_size(nv, &nvsize, NV_ENCODE_XDR) == 0);

	/*
	 * Write full (SPA_CONFIG_BLOCKSIZE) blocks of configuration
	 * information.  This avoids the dbuf_will_dirty() path and
	 * saves us a pre-read to get data we don't actually care about.
	 */
	bufsize = P2ROUNDUP(nvsize, SPA_CONFIG_BLOCKSIZE);
	packed = kmem_alloc(bufsize, KM_SLEEP);

	VERIFY(nvlist_pack(nv, &packed, &nvsize, NV_ENCODE_XDR,
	    KM_SLEEP) == 0);
	bzero(packed + nvsize, bufsize - nvsize);

	dmu_write(spa->spa_meta_objset, obj, 0, bufsize, packed, tx);

	kmem_free(packed, bufsize);

	VERIFY(0 == dmu_bonus_hold(spa->spa_meta_objset, obj, FTAG, &db));
	dmu_buf_will_dirty(db, tx);
	*(uint64_t *)db->db_data = nvsize;
	dmu_buf_rele(db, FTAG);
}

static void
spa_sync_aux_dev(spa_t *spa, spa_aux_vdev_t *sav, dmu_tx_t *tx,
    const char *config, const char *entry)
{
	nvlist_t *nvroot;
	nvlist_t **list;
	int i;

	if (!sav->sav_sync)
		return;

	/*
	 * Update the MOS nvlist describing the list of available devices.
	 * spa_validate_aux() will have already made sure this nvlist is
	 * valid and the vdevs are labeled appropriately.
	 */
	if (sav->sav_object == 0) {
		sav->sav_object = dmu_object_alloc(spa->spa_meta_objset,
		    DMU_OT_PACKED_NVLIST, 1 << 14, DMU_OT_PACKED_NVLIST_SIZE,
		    sizeof (uint64_t), tx);
		VERIFY(zap_update(spa->spa_meta_objset,
		    DMU_POOL_DIRECTORY_OBJECT, entry, sizeof (uint64_t), 1,
		    &sav->sav_object, tx) == 0);
	}

	VERIFY(nvlist_alloc(&nvroot, NV_UNIQUE_NAME, KM_SLEEP) == 0);
	if (sav->sav_count == 0) {
		VERIFY(nvlist_add_nvlist_array(nvroot, config, NULL, 0) == 0);
	} else {
		list = kmem_alloc(sav->sav_count * sizeof (void *), KM_SLEEP);
		for (i = 0; i < sav->sav_count; i++)
			list[i] = vdev_config_generate(spa, sav->sav_vdevs[i],
			    B_FALSE, B_FALSE, B_TRUE);
		VERIFY(nvlist_add_nvlist_array(nvroot, config, list,
		    sav->sav_count) == 0);
		for (i = 0; i < sav->sav_count; i++)
			nvlist_free(list[i]);
		kmem_free(list, sav->sav_count * sizeof (void *));
	}

	spa_sync_nvlist(spa, sav->sav_object, nvroot, tx);
	nvlist_free(nvroot);

	sav->sav_sync = B_FALSE;
}

static void
spa_sync_config_object(spa_t *spa, dmu_tx_t *tx)
{
	nvlist_t *config;

	if (list_is_empty(&spa->spa_config_dirty_list))
		return;

	spa_config_enter(spa, SCL_STATE, FTAG, RW_READER);

	config = spa_config_generate(spa, spa->spa_root_vdev,
	    dmu_tx_get_txg(tx), B_FALSE);

	spa_config_exit(spa, SCL_STATE, FTAG);

	if (spa->spa_config_syncing)
		nvlist_free(spa->spa_config_syncing);
	spa->spa_config_syncing = config;

	spa_sync_nvlist(spa, spa->spa_config_object, config, tx);
}

/*
 * Set zpool properties.
 */
static void
spa_sync_props(void *arg1, void *arg2, cred_t *cr, dmu_tx_t *tx)
{
	spa_t *spa = arg1;
	objset_t *mos = spa->spa_meta_objset;
	nvlist_t *nvp = arg2;
	nvpair_t *elem;
	uint64_t intval;
	char *strval;
	zpool_prop_t prop;
	const char *propname;
	zprop_type_t proptype;

	mutex_enter(&spa->spa_props_lock);

	elem = NULL;
	while ((elem = nvlist_next_nvpair(nvp, elem))) {
		switch (prop = zpool_name_to_prop(nvpair_name(elem))) {
		case ZPOOL_PROP_VERSION:
			/*
			 * Only set version for non-zpool-creation cases
			 * (set/import). spa_create() needs special care
			 * for version setting.
			 */
			if (tx->tx_txg != TXG_INITIAL) {
				VERIFY(nvpair_value_uint64(elem,
				    &intval) == 0);
				ASSERT(intval <= SPA_VERSION);
				ASSERT(intval >= spa_version(spa));
				spa->spa_uberblock.ub_version = intval;
				vdev_config_dirty(spa->spa_root_vdev);
			}
			break;

		case ZPOOL_PROP_ALTROOT:
			/*
			 * 'altroot' is a non-persistent property. It should
			 * have been set temporarily at creation or import time.
			 */
			ASSERT(spa->spa_root != NULL);
			break;

		case ZPOOL_PROP_CACHEFILE:
			/*
			 * 'cachefile' is also a non-persisitent property.
			 */
			break;
		default:
			/*
			 * Set pool property values in the poolprops mos object.
			 */
			if (spa->spa_pool_props_object == 0) {
				objset_t *mos = spa->spa_meta_objset;

				VERIFY((spa->spa_pool_props_object =
				    zap_create(mos, DMU_OT_POOL_PROPS,
				    DMU_OT_NONE, 0, tx)) > 0);

				VERIFY(zap_update(mos,
				    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_PROPS,
				    8, 1, &spa->spa_pool_props_object, tx)
				    == 0);
			}

			/* normalize the property name */
			propname = zpool_prop_to_name(prop);
			proptype = zpool_prop_get_type(prop);

			if (nvpair_type(elem) == DATA_TYPE_STRING) {
				ASSERT(proptype == PROP_TYPE_STRING);
				VERIFY(nvpair_value_string(elem, &strval) == 0);
				VERIFY(zap_update(mos,
				    spa->spa_pool_props_object, propname,
				    1, strlen(strval) + 1, strval, tx) == 0);

			} else if (nvpair_type(elem) == DATA_TYPE_UINT64) {
				VERIFY(nvpair_value_uint64(elem, &intval) == 0);

				if (proptype == PROP_TYPE_INDEX) {
					const char *unused;
					VERIFY(zpool_prop_index_to_string(
					    prop, intval, &unused) == 0);
				}
				VERIFY(zap_update(mos,
				    spa->spa_pool_props_object, propname,
				    8, 1, &intval, tx) == 0);
			} else {
				ASSERT(0); /* not allowed */
			}

			switch (prop) {
			case ZPOOL_PROP_DELEGATION:
				spa->spa_delegation = intval;
				break;
			case ZPOOL_PROP_BOOTFS:
				spa->spa_bootfs = intval;
				break;
			case ZPOOL_PROP_FAILUREMODE:
				spa->spa_failmode = intval;
				break;
			default:
				break;
			}
		}

		/* log internal history if this is not a zpool create */
		if (spa_version(spa) >= SPA_VERSION_ZPOOL_HISTORY &&
		    tx->tx_txg != TXG_INITIAL) {
			spa_history_internal_log(LOG_POOL_PROPSET,
			    spa, tx, cr, "%s %lld %s",
			    nvpair_name(elem), intval, spa_name(spa));
		}
	}

	mutex_exit(&spa->spa_props_lock);
}

/*
 * Sync the specified transaction group.  New blocks may be dirtied as
 * part of the process, so we iterate until it converges.
 */
void
spa_sync(spa_t *spa, uint64_t txg)
{
	dsl_pool_t *dp = spa->spa_dsl_pool;
	objset_t *mos = spa->spa_meta_objset;
	bplist_t *bpl = &spa->spa_sync_bplist;
	vdev_t *rvd = spa->spa_root_vdev;
	vdev_t *vd;
	dmu_tx_t *tx;
	int dirty_vdevs;
	int error;

	/*
	 * Lock out configuration changes.
	 */
	spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);

	spa->spa_syncing_txg = txg;
	spa->spa_sync_pass = 0;

	/*
	 * If there are any pending vdev state changes, convert them
	 * into config changes that go out with this transaction group.
	 */
	spa_config_enter(spa, SCL_STATE, FTAG, RW_READER);
	while (list_head(&spa->spa_state_dirty_list) != NULL) {
		/*
		 * We need the write lock here because, for aux vdevs,
		 * calling vdev_config_dirty() modifies sav_config.
		 * This is ugly and will become unnecessary when we
		 * eliminate the aux vdev wart by integrating all vdevs
		 * into the root vdev tree.
		 */
		spa_config_exit(spa, SCL_CONFIG | SCL_STATE, FTAG);
		spa_config_enter(spa, SCL_CONFIG | SCL_STATE, FTAG, RW_WRITER);
		while ((vd = list_head(&spa->spa_state_dirty_list)) != NULL) {
			vdev_state_clean(vd);
			vdev_config_dirty(vd);
		}
		spa_config_exit(spa, SCL_CONFIG | SCL_STATE, FTAG);
		spa_config_enter(spa, SCL_CONFIG | SCL_STATE, FTAG, RW_READER);
	}
	spa_config_exit(spa, SCL_STATE, FTAG);

	VERIFY(0 == bplist_open(bpl, mos, spa->spa_sync_bplist_obj));

	tx = dmu_tx_create_assigned(dp, txg);

	/*
	 * If we are upgrading to SPA_VERSION_RAIDZ_DEFLATE this txg,
	 * set spa_deflate if we have no raid-z vdevs.
	 */
	if (spa->spa_ubsync.ub_version < SPA_VERSION_RAIDZ_DEFLATE &&
	    spa->spa_uberblock.ub_version >= SPA_VERSION_RAIDZ_DEFLATE) {
		int i;

		for (i = 0; i < rvd->vdev_children; i++) {
			vd = rvd->vdev_child[i];
			if (vd->vdev_deflate_ratio != SPA_MINBLOCKSIZE)
				break;
		}
		if (i == rvd->vdev_children) {
			spa->spa_deflate = TRUE;
			VERIFY(0 == zap_add(spa->spa_meta_objset,
			    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_DEFLATE,
			    sizeof (uint64_t), 1, &spa->spa_deflate, tx));
		}
	}

	if (spa->spa_ubsync.ub_version < SPA_VERSION_ORIGIN &&
	    spa->spa_uberblock.ub_version >= SPA_VERSION_ORIGIN) {
		dsl_pool_create_origin(dp, tx);

		/* Keeping the origin open increases spa_minref */
		spa->spa_minref += 3;
	}

	if (spa->spa_ubsync.ub_version < SPA_VERSION_NEXT_CLONES &&
	    spa->spa_uberblock.ub_version >= SPA_VERSION_NEXT_CLONES) {
		dsl_pool_upgrade_clones(dp, tx);
	}

	/*
	 * If anything has changed in this txg, push the deferred frees
	 * from the previous txg.  If not, leave them alone so that we
	 * don't generate work on an otherwise idle system.
	 */
	if (!txg_list_empty(&dp->dp_dirty_datasets, txg) ||
	    !txg_list_empty(&dp->dp_dirty_dirs, txg) ||
	    !txg_list_empty(&dp->dp_sync_tasks, txg))
		spa_sync_deferred_frees(spa, txg);

	/*
	 * Iterate to convergence.
	 */
	do {
		spa->spa_sync_pass++;

		spa_sync_config_object(spa, tx);
		spa_sync_aux_dev(spa, &spa->spa_spares, tx,
		    ZPOOL_CONFIG_SPARES, DMU_POOL_SPARES);
		spa_sync_aux_dev(spa, &spa->spa_l2cache, tx,
		    ZPOOL_CONFIG_L2CACHE, DMU_POOL_L2CACHE);
		spa_errlog_sync(spa, txg);
		dsl_pool_sync(dp, txg);

		dirty_vdevs = 0;
		while (vd = txg_list_remove(&spa->spa_vdev_txg_list, txg)) {
			vdev_sync(vd, txg);
			dirty_vdevs++;
		}

		bplist_sync(bpl, tx);
	} while (dirty_vdevs);

	bplist_close(bpl);

	dprintf("txg %llu passes %d\n", txg, spa->spa_sync_pass);

	/*
	 * Rewrite the vdev configuration (which includes the uberblock)
	 * to commit the transaction group.
	 *
	 * If there are no dirty vdevs, we sync the uberblock to a few
	 * random top-level vdevs that are known to be visible in the
	 * config cache (see spa_vdev_add() for a complete description).
	 * If there *are* dirty vdevs, sync the uberblock to all vdevs.
	 */
	for (;;) {
		/*
		 * We hold SCL_STATE to prevent vdev open/close/etc.
		 * while we're attempting to write the vdev labels.
		 */
		spa_config_enter(spa, SCL_STATE, FTAG, RW_READER);

		if (list_is_empty(&spa->spa_config_dirty_list)) {
			vdev_t *svd[SPA_DVAS_PER_BP];
			int svdcount = 0;
			int children = rvd->vdev_children;
			int c0 = spa_get_random(children);
			int c;

			for (c = 0; c < children; c++) {
				vd = rvd->vdev_child[(c0 + c) % children];
				if (vd->vdev_ms_array == 0 || vd->vdev_islog)
					continue;
				svd[svdcount++] = vd;
				if (svdcount == SPA_DVAS_PER_BP)
					break;
			}
			error = vdev_config_sync(svd, svdcount, txg);
		} else {
			error = vdev_config_sync(rvd->vdev_child,
			    rvd->vdev_children, txg);
		}

		spa_config_exit(spa, SCL_STATE, FTAG);

		if (error == 0)
			break;
		zio_suspend(spa, NULL);
		zio_resume_wait(spa);
	}
	dmu_tx_commit(tx);

	/*
	 * Clear the dirty config list.
	 */
	while ((vd = list_head(&spa->spa_config_dirty_list)) != NULL)
		vdev_config_clean(vd);

	/*
	 * Now that the new config has synced transactionally,
	 * let it become visible to the config cache.
	 */
	if (spa->spa_config_syncing != NULL) {
		spa_config_set(spa, spa->spa_config_syncing);
		spa->spa_config_txg = txg;
		spa->spa_config_syncing = NULL;
	}

	spa->spa_ubsync = spa->spa_uberblock;

	/*
	 * Clean up the ZIL records for the synced txg.
	 */
	dsl_pool_zil_clean(dp);

	/*
	 * Update usable space statistics.
	 */
	while (vd = txg_list_remove(&spa->spa_vdev_txg_list, TXG_CLEAN(txg)))
		vdev_sync_done(vd, txg);

	/*
	 * It had better be the case that we didn't dirty anything
	 * since vdev_config_sync().
	 */
	ASSERT(txg_list_empty(&dp->dp_dirty_datasets, txg));
	ASSERT(txg_list_empty(&dp->dp_dirty_dirs, txg));
	ASSERT(txg_list_empty(&spa->spa_vdev_txg_list, txg));
	ASSERT(bpl->bpl_queue == NULL);

	spa_config_exit(spa, SCL_CONFIG, FTAG);

	/*
	 * If any async tasks have been requested, kick them off.
	 */
	spa_async_dispatch(spa);
}

/*
 * Sync all pools.  We don't want to hold the namespace lock across these
 * operations, so we take a reference on the spa_t and drop the lock during the
 * sync.
 */
void
spa_sync_allpools(void)
{
	spa_t *spa = NULL;
	mutex_enter(&spa_namespace_lock);
	while ((spa = spa_next(spa)) != NULL) {
		if (spa_state(spa) != POOL_STATE_ACTIVE || spa_suspended(spa))
			continue;
		spa_open_ref(spa, FTAG);
		mutex_exit(&spa_namespace_lock);
		txg_wait_synced(spa_get_dsl(spa), 0);
		mutex_enter(&spa_namespace_lock);
		spa_close(spa, FTAG);
	}
	mutex_exit(&spa_namespace_lock);
}

/*
 * ==========================================================================
 * Miscellaneous routines
 * ==========================================================================
 */

/*
 * Remove all pools in the system.
 */
void
spa_evict_all(void)
{
	spa_t *spa;

	/*
	 * Remove all cached state.  All pools should be closed now,
	 * so every spa in the AVL tree should be unreferenced.
	 */
	mutex_enter(&spa_namespace_lock);
	while ((spa = spa_next(NULL)) != NULL) {
		/*
		 * Stop async tasks.  The async thread may need to detach
		 * a device that's been replaced, which requires grabbing
		 * spa_namespace_lock, so we must drop it here.
		 */
		spa_open_ref(spa, FTAG);
		mutex_exit(&spa_namespace_lock);
		spa_async_suspend(spa);
		mutex_enter(&spa_namespace_lock);
		spa_close(spa, FTAG);

		if (spa->spa_state != POOL_STATE_UNINITIALIZED) {
			spa_unload(spa);
			spa_deactivate(spa);
		}
		spa_remove(spa);
	}
	mutex_exit(&spa_namespace_lock);
}

vdev_t *
spa_lookup_by_guid(spa_t *spa, uint64_t guid, boolean_t l2cache)
{
	vdev_t *vd;
	int i;

	if ((vd = vdev_lookup_by_guid(spa->spa_root_vdev, guid)) != NULL)
		return (vd);

	if (l2cache) {
		for (i = 0; i < spa->spa_l2cache.sav_count; i++) {
			vd = spa->spa_l2cache.sav_vdevs[i];
			if (vd->vdev_guid == guid)
				return (vd);
		}
	}

	return (NULL);
}

void
spa_upgrade(spa_t *spa, uint64_t version)
{
	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);

	/*
	 * This should only be called for a non-faulted pool, and since a
	 * future version would result in an unopenable pool, this shouldn't be
	 * possible.
	 */
	ASSERT(spa->spa_uberblock.ub_version <= SPA_VERSION);
	ASSERT(version >= spa->spa_uberblock.ub_version);

	spa->spa_uberblock.ub_version = version;
	vdev_config_dirty(spa->spa_root_vdev);

	spa_config_exit(spa, SCL_ALL, FTAG);

	txg_wait_synced(spa_get_dsl(spa), 0);
}

boolean_t
spa_has_spare(spa_t *spa, uint64_t guid)
{
	int i;
	uint64_t spareguid;
	spa_aux_vdev_t *sav = &spa->spa_spares;

	for (i = 0; i < sav->sav_count; i++)
		if (sav->sav_vdevs[i]->vdev_guid == guid)
			return (B_TRUE);

	for (i = 0; i < sav->sav_npending; i++) {
		if (nvlist_lookup_uint64(sav->sav_pending[i], ZPOOL_CONFIG_GUID,
		    &spareguid) == 0 && spareguid == guid)
			return (B_TRUE);
	}

	return (B_FALSE);
}

/*
 * Check if a pool has an active shared spare device.
 * Note: reference count of an active spare is 2, as a spare and as a replace
 */
static boolean_t
spa_has_active_shared_spare(spa_t *spa)
{
	int i, refcnt;
	uint64_t pool;
	spa_aux_vdev_t *sav = &spa->spa_spares;

	for (i = 0; i < sav->sav_count; i++) {
		if (spa_spare_exists(sav->sav_vdevs[i]->vdev_guid, &pool,
		    &refcnt) && pool != 0ULL && pool == spa_guid(spa) &&
		    refcnt > 2)
			return (B_TRUE);
	}

	return (B_FALSE);
}

/*
 * Post a sysevent corresponding to the given event.  The 'name' must be one of
 * the event definitions in sys/sysevent/eventdefs.h.  The payload will be
 * filled in from the spa and (optionally) the vdev.  This doesn't do anything
 * in the userland libzpool, as we don't want consumers to misinterpret ztest
 * or zdb as real changes.
 */
void
spa_event_notify(spa_t *spa, vdev_t *vd, const char *name)
{
#ifdef _KERNEL
	sysevent_t		*ev;
	sysevent_attr_list_t	*attr = NULL;
	sysevent_value_t	value;
	sysevent_id_t		eid;

	ev = sysevent_alloc(EC_ZFS, (char *)name, SUNW_KERN_PUB "zfs",
	    SE_SLEEP);

	value.value_type = SE_DATA_TYPE_STRING;
	value.value.sv_string = spa_name(spa);
	if (sysevent_add_attr(&attr, ZFS_EV_POOL_NAME, &value, SE_SLEEP) != 0)
		goto done;

	value.value_type = SE_DATA_TYPE_UINT64;
	value.value.sv_uint64 = spa_guid(spa);
	if (sysevent_add_attr(&attr, ZFS_EV_POOL_GUID, &value, SE_SLEEP) != 0)
		goto done;

	if (vd) {
		value.value_type = SE_DATA_TYPE_UINT64;
		value.value.sv_uint64 = vd->vdev_guid;
		if (sysevent_add_attr(&attr, ZFS_EV_VDEV_GUID, &value,
		    SE_SLEEP) != 0)
			goto done;

		if (vd->vdev_path) {
			value.value_type = SE_DATA_TYPE_STRING;
			value.value.sv_string = vd->vdev_path;
			if (sysevent_add_attr(&attr, ZFS_EV_VDEV_PATH,
			    &value, SE_SLEEP) != 0)
				goto done;
		}
	}

	if (sysevent_attach_attributes(ev, attr) != 0)
		goto done;
	attr = NULL;

	(void) log_sysevent(ev, SE_SLEEP, &eid);

done:
	if (attr)
		sysevent_free_attr(attr);
	sysevent_free(ev);
#endif
}
