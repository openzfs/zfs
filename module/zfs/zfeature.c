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
 * Copyright (c) 2013 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/zfeature.h>
#include <sys/dmu.h>
#include <sys/nvpair.h>
#include <sys/zap.h>
#include <sys/dmu_tx.h>
#include "zfeature_common.h"
#include <sys/spa_impl.h>

/*
 * ZFS Feature Flags
 * -----------------
 *
 * ZFS feature flags are used to provide fine-grained versioning to the ZFS
 * on-disk format. Once enabled on a pool feature flags replace the old
 * spa_version() number.
 *
 * Each new on-disk format change will be given a uniquely identifying string
 * guid rather than a version number. This avoids the problem of different
 * organizations creating new on-disk formats with the same version number. To
 * keep feature guids unique they should consist of the reverse dns name of the
 * organization which implemented the feature and a short name for the feature,
 * separated by a colon (e.g. com.delphix:async_destroy).
 *
 * Reference Counts
 * ----------------
 *
 * Within each pool features can be in one of three states: disabled, enabled,
 * or active. These states are differentiated by a reference count stored on
 * disk for each feature:
 *
 *   1) If there is no reference count stored on disk the feature is disabled.
 *   2) If the reference count is 0 a system administrator has enabled the
 *      feature, but the feature has not been used yet, so no on-disk
 *      format changes have been made.
 *   3) If the reference count is greater than 0 the feature is active.
 *      The format changes required by the feature are currently on disk.
 *      Note that if the feature's format changes are reversed the feature
 *      may choose to set its reference count back to 0.
 *
 * Feature flags makes no differentiation between non-zero reference counts
 * for an active feature (e.g. a reference count of 1 means the same thing as a
 * reference count of 27834721), but feature implementations may choose to use
 * the reference count to store meaningful information. For example, a new RAID
 * implementation might set the reference count to the number of vdevs using
 * it. If all those disks are removed from the pool the feature goes back to
 * having a reference count of 0.
 *
 * It is the responsibility of the individual features to maintain a non-zero
 * reference count as long as the feature's format changes are present on disk.
 *
 * Dependencies
 * ------------
 *
 * Each feature may depend on other features. The only effect of this
 * relationship is that when a feature is enabled all of its dependencies are
 * automatically enabled as well. Any future work to support disabling of
 * features would need to ensure that features cannot be disabled if other
 * enabled features depend on them.
 *
 * On-disk Format
 * --------------
 *
 * When feature flags are enabled spa_version() is set to SPA_VERSION_FEATURES
 * (5000). In order for this to work the pool is automatically upgraded to
 * SPA_VERSION_BEFORE_FEATURES (28) first, so all pre-feature flags on disk
 * format changes will be in use.
 *
 * Information about features is stored in 3 ZAP objects in the pool's MOS.
 * These objects are linked to by the following names in the pool directory
 * object:
 *
 * 1) features_for_read: feature guid -> reference count
 *    Features needed to open the pool for reading.
 * 2) features_for_write: feature guid -> reference count
 *    Features needed to open the pool for writing.
 * 3) feature_descriptions: feature guid -> descriptive string
 *    A human readable string.
 *
 * All enabled features appear in either features_for_read or
 * features_for_write, but not both.
 *
 * To open a pool in read-only mode only the features listed in
 * features_for_read need to be supported.
 *
 * To open the pool in read-write mode features in both features_for_read and
 * features_for_write need to be supported.
 *
 * Some features may be required to read the ZAP objects containing feature
 * information. To allow software to check for compatibility with these features
 * before the pool is opened their names must be stored in the label in a
 * new "features_for_read" entry (note that features that are only required
 * to write to a pool never need to be stored in the label since the
 * features_for_write ZAP object can be read before the pool is written to).
 * To save space in the label features must be explicitly marked as needing to
 * be written to the label. Also, reference counts are not stored in the label,
 * instead any feature whose reference count drops to 0 is removed from the
 * label.
 *
 * Adding New Features
 * -------------------
 *
 * Features must be registered in zpool_feature_init() function in
 * zfeature_common.c using the zfeature_register() function. This function
 * has arguments to specify if the feature should be stored in the
 * features_for_read or features_for_write ZAP object and if it needs to be
 * written to the label when active.
 *
 * Once a feature is registered it will appear as a "feature@<feature name>"
 * property which can be set by an administrator. Feature implementors should
 * use the spa_feature_is_enabled() and spa_feature_is_active() functions to
 * query the state of a feature and the spa_feature_incr() and
 * spa_feature_decr() functions to change an enabled feature's reference count.
 * Reference counts may only be updated in the syncing context.
 *
 * Features may not perform enable-time initialization. Instead, any such
 * initialization should occur when the feature is first used. This design
 * enforces that on-disk changes be made only when features are used. Code
 * should only check if a feature is enabled using spa_feature_is_enabled(),
 * not by relying on any feature specific metadata existing. If a feature is
 * enabled, but the feature's metadata is not on disk yet then it should be
 * created as needed.
 *
 * As an example, consider the com.delphix:async_destroy feature. This feature
 * relies on the existence of a bptree in the MOS that store blocks for
 * asynchronous freeing. This bptree is not created when async_destroy is
 * enabled. Instead, when a dataset is destroyed spa_feature_is_enabled() is
 * called to check if async_destroy is enabled. If it is and the bptree object
 * does not exist yet, the bptree object is created as part of the dataset
 * destroy and async_destroy's reference count is incremented to indicate it
 * has made an on-disk format change. Later, after the destroyed dataset's
 * blocks have all been asynchronously freed there is no longer any use for the
 * bptree object, so it is destroyed and async_destroy's reference count is
 * decremented back to 0 to indicate that it has undone its on-disk format
 * changes.
 */

typedef enum {
	FEATURE_ACTION_ENABLE,
	FEATURE_ACTION_INCR,
	FEATURE_ACTION_DECR,
} feature_action_t;

/*
 * Checks that the features active in the specified object are supported by
 * this software.  Adds each unsupported feature (name -> description) to
 * the supplied nvlist.
 */
boolean_t
feature_is_supported(objset_t *os, uint64_t obj, uint64_t desc_obj,
    nvlist_t *unsup_feat, nvlist_t *enabled_feat)
{
	boolean_t supported;
	zap_cursor_t *zc;
	zap_attribute_t *za;
	char *buf;

	zc = kmem_alloc(sizeof (zap_cursor_t), KM_SLEEP);
	za = kmem_alloc(sizeof (zap_attribute_t), KM_SLEEP);
	buf = kmem_alloc(MAXPATHLEN, KM_SLEEP);

	supported = B_TRUE;
	for (zap_cursor_init(zc, os, obj);
	    zap_cursor_retrieve(zc, za) == 0;
	    zap_cursor_advance(zc)) {
		ASSERT(za->za_integer_length == sizeof (uint64_t) &&
		    za->za_num_integers == 1);

		if (NULL != enabled_feat) {
			fnvlist_add_uint64(enabled_feat, za->za_name,
			    za->za_first_integer);
		}

		if (za->za_first_integer != 0 &&
		    !zfeature_is_supported(za->za_name)) {
			supported = B_FALSE;

			if (NULL != unsup_feat) {
				char *desc = "";

				if (zap_lookup(os, desc_obj, za->za_name,
				    1, MAXPATHLEN, buf) == 0)
					desc = buf;

				VERIFY(nvlist_add_string(unsup_feat,
				    za->za_name, desc) == 0);
			}
		}
	}
	zap_cursor_fini(zc);

	kmem_free(buf, MAXPATHLEN);
	kmem_free(za, sizeof (zap_attribute_t));
	kmem_free(zc, sizeof (zap_cursor_t));

	return (supported);
}

static int
feature_get_refcount(objset_t *os, uint64_t read_obj, uint64_t write_obj,
    zfeature_info_t *feature, uint64_t *res)
{
	int err;
	uint64_t refcount;
	uint64_t zapobj = feature->fi_can_readonly ? write_obj : read_obj;

	/*
	 * If the pool is currently being created, the feature objects may not
	 * have been allocated yet.  Act as though all features are disabled.
	 */
	if (zapobj == 0)
		return (SET_ERROR(ENOTSUP));

	err = zap_lookup(os, zapobj, feature->fi_guid, sizeof (uint64_t), 1,
	    &refcount);
	if (err != 0) {
		if (err == ENOENT)
			return (SET_ERROR(ENOTSUP));
		else
			return (err);
	}
	*res = refcount;
	return (0);
}

static int
feature_do_action(objset_t *os, uint64_t read_obj, uint64_t write_obj,
    uint64_t desc_obj, zfeature_info_t *feature, feature_action_t action,
    dmu_tx_t *tx)
{
	int error;
	uint64_t refcount;
	uint64_t zapobj = feature->fi_can_readonly ? write_obj : read_obj;

	ASSERT(0 != zapobj);
	ASSERT(zfeature_is_valid_guid(feature->fi_guid));

	error = zap_lookup(os, zapobj, feature->fi_guid,
	    sizeof (uint64_t), 1, &refcount);

	/*
	 * If we can't ascertain the status of the specified feature, an I/O
	 * error occurred.
	 */
	if (error != 0 && error != ENOENT)
		return (error);

	switch (action) {
	case FEATURE_ACTION_ENABLE:
		/*
		 * If the feature is already enabled, ignore the request.
		 */
		if (error == 0)
			return (0);
		refcount = 0;
		break;
	case FEATURE_ACTION_INCR:
		if (error == ENOENT)
			return (SET_ERROR(ENOTSUP));
		if (refcount == UINT64_MAX)
			return (SET_ERROR(EOVERFLOW));
		refcount++;
		break;
	case FEATURE_ACTION_DECR:
		if (error == ENOENT)
			return (SET_ERROR(ENOTSUP));
		if (refcount == 0)
			return (SET_ERROR(EOVERFLOW));
		refcount--;
		break;
	default:
		ASSERT(0);
		break;
	}

	if (action == FEATURE_ACTION_ENABLE) {
		int i;

		for (i = 0; feature->fi_depends[i] != NULL; i++) {
			zfeature_info_t *dep = feature->fi_depends[i];

			error = feature_do_action(os, read_obj, write_obj,
			    desc_obj, dep, FEATURE_ACTION_ENABLE, tx);
			if (error != 0)
				return (error);
		}
	}

	error = zap_update(os, zapobj, feature->fi_guid,
	    sizeof (uint64_t), 1, &refcount, tx);
	if (error != 0)
		return (error);

	if (action == FEATURE_ACTION_ENABLE) {
		error = zap_update(os, desc_obj,
		    feature->fi_guid, 1, strlen(feature->fi_desc) + 1,
		    feature->fi_desc, tx);
		if (error != 0)
			return (error);
	}

	if (action == FEATURE_ACTION_INCR && refcount == 1 && feature->fi_mos) {
		spa_activate_mos_feature(dmu_objset_spa(os), feature->fi_guid);
	}

	if (action == FEATURE_ACTION_DECR && refcount == 0) {
		spa_deactivate_mos_feature(dmu_objset_spa(os),
		    feature->fi_guid);
	}

	return (0);
}

void
spa_feature_create_zap_objects(spa_t *spa, dmu_tx_t *tx)
{
	/*
	 * We create feature flags ZAP objects in two instances: during pool
	 * creation and during pool upgrade.
	 */
	ASSERT(dsl_pool_sync_context(spa_get_dsl(spa)) || (!spa->spa_sync_on &&
	    tx->tx_txg == TXG_INITIAL));

	spa->spa_feat_for_read_obj = zap_create_link(spa->spa_meta_objset,
	    DMU_OTN_ZAP_METADATA, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_FEATURES_FOR_READ, tx);
	spa->spa_feat_for_write_obj = zap_create_link(spa->spa_meta_objset,
	    DMU_OTN_ZAP_METADATA, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_FEATURES_FOR_WRITE, tx);
	spa->spa_feat_desc_obj = zap_create_link(spa->spa_meta_objset,
	    DMU_OTN_ZAP_METADATA, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_FEATURE_DESCRIPTIONS, tx);
}

/*
 * Enable any required dependencies, then enable the requested feature.
 */
void
spa_feature_enable(spa_t *spa, zfeature_info_t *feature, dmu_tx_t *tx)
{
	ASSERT3U(spa_version(spa), >=, SPA_VERSION_FEATURES);
	VERIFY3U(0, ==, feature_do_action(spa->spa_meta_objset,
	    spa->spa_feat_for_read_obj, spa->spa_feat_for_write_obj,
	    spa->spa_feat_desc_obj, feature, FEATURE_ACTION_ENABLE, tx));
}

/*
 * If the specified feature has not yet been enabled, this function returns
 * ENOTSUP; otherwise, this function increments the feature's refcount (or
 * returns EOVERFLOW if the refcount cannot be incremented). This function must
 * be called from syncing context.
 */
void
spa_feature_incr(spa_t *spa, zfeature_info_t *feature, dmu_tx_t *tx)
{
	ASSERT3U(spa_version(spa), >=, SPA_VERSION_FEATURES);
	VERIFY3U(0, ==, feature_do_action(spa->spa_meta_objset,
	    spa->spa_feat_for_read_obj, spa->spa_feat_for_write_obj,
	    spa->spa_feat_desc_obj, feature, FEATURE_ACTION_INCR, tx));
}

/*
 * If the specified feature has not yet been enabled, this function returns
 * ENOTSUP; otherwise, this function decrements the feature's refcount (or
 * returns EOVERFLOW if the refcount is already 0). This function must
 * be called from syncing context.
 */
void
spa_feature_decr(spa_t *spa, zfeature_info_t *feature, dmu_tx_t *tx)
{
	ASSERT3U(spa_version(spa), >=, SPA_VERSION_FEATURES);
	VERIFY3U(0, ==, feature_do_action(spa->spa_meta_objset,
	    spa->spa_feat_for_read_obj, spa->spa_feat_for_write_obj,
	    spa->spa_feat_desc_obj, feature, FEATURE_ACTION_DECR, tx));
}

boolean_t
spa_feature_is_enabled(spa_t *spa, zfeature_info_t *feature)
{
	int err;
	uint64_t refcount = 0;

	if (spa_version(spa) < SPA_VERSION_FEATURES)
		return (B_FALSE);

	err = feature_get_refcount(spa->spa_meta_objset,
	    spa->spa_feat_for_read_obj, spa->spa_feat_for_write_obj,
	    feature, &refcount);
	ASSERT(err == 0 || err == ENOTSUP);
	return (err == 0);
}

boolean_t
spa_feature_is_active(spa_t *spa, zfeature_info_t *feature)
{
	int err;
	uint64_t refcount = 0;

	if (spa_version(spa) < SPA_VERSION_FEATURES)
		return (B_FALSE);

	err = feature_get_refcount(spa->spa_meta_objset,
	    spa->spa_feat_for_read_obj, spa->spa_feat_for_write_obj,
	    feature, &refcount);
	ASSERT(err == 0 || err == ENOTSUP);
	return (err == 0 && refcount > 0);
}
