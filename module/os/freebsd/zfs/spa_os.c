// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Copyright (c) 2011 by Delphix. All rights reserved.
 * Copyright (c) 2013 Martin Matuska <mm@FreeBSD.org>. All rights reserved.
 */


#include <sys/zfs_context.h>
#include <sys/fm/fs/zfs.h>
#include <sys/spa_impl.h>
#include <sys/zio.h>
#include <sys/zio_checksum.h>
#include <sys/dmu.h>
#include <sys/dmu_tx.h>
#include <sys/zap.h>
#include <sys/zil.h>
#include <sys/ddt.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_os.h>
#include <sys/vdev_removal.h>
#include <sys/vdev_indirect_mapping.h>
#include <sys/vdev_indirect_births.h>
#include <sys/metaslab.h>
#include <sys/metaslab_impl.h>
#include <sys/uberblock_impl.h>
#include <sys/txg.h>
#include <sys/avl.h>
#include <sys/bpobj.h>
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
#include <sys/zfs_ioctl.h>
#include <sys/dsl_scan.h>
#include <sys/dmu_send.h>
#include <sys/dsl_destroy.h>
#include <sys/dsl_userhold.h>
#include <sys/zfeature.h>
#include <sys/zvol.h>
#include <sys/abd.h>
#include <sys/callb.h>
#include <sys/zone.h>

#include "zfs_prop.h"
#include "zfs_comutil.h"

static nvlist_t *
spa_generate_rootconf(const char *name)
{
	nvlist_t **configs, **tops;
	nvlist_t *config;
	nvlist_t *best_cfg, *nvtop, *nvroot;
	uint64_t *holes;
	uint64_t best_txg;
	uint64_t nchildren;
	uint64_t pgid;
	uint64_t count;
	uint64_t i;
	uint_t   nholes;

	if (vdev_geom_read_pool_label(name, &configs, &count) != 0)
		return (NULL);

	ASSERT3U(count, !=, 0);
	best_txg = 0;
	for (i = 0; i < count; i++) {
		uint64_t txg;

		if (configs[i] == NULL)
			continue;
		txg = fnvlist_lookup_uint64(configs[i], ZPOOL_CONFIG_POOL_TXG);
		if (txg > best_txg) {
			best_txg = txg;
			best_cfg = configs[i];
		}
	}

	nchildren = 1;
	nvlist_lookup_uint64(best_cfg, ZPOOL_CONFIG_VDEV_CHILDREN, &nchildren);
	holes = NULL;
	nvlist_lookup_uint64_array(best_cfg, ZPOOL_CONFIG_HOLE_ARRAY,
	    &holes, &nholes);

	tops = kmem_zalloc(nchildren * sizeof (void *), KM_SLEEP);
	for (i = 0; i < nchildren; i++) {
		if (i >= count)
			break;
		if (configs[i] == NULL)
			continue;
		nvtop = fnvlist_lookup_nvlist(configs[i],
		    ZPOOL_CONFIG_VDEV_TREE);
		tops[i] = fnvlist_dup(nvtop);
	}
	for (i = 0; holes != NULL && i < nholes; i++) {
		if (i >= nchildren)
			continue;
		if (tops[holes[i]] != NULL)
			continue;
		tops[holes[i]] = fnvlist_alloc();
		fnvlist_add_string(tops[holes[i]], ZPOOL_CONFIG_TYPE,
		    VDEV_TYPE_HOLE);
		fnvlist_add_uint64(tops[holes[i]], ZPOOL_CONFIG_ID, holes[i]);
		fnvlist_add_uint64(tops[holes[i]], ZPOOL_CONFIG_GUID, 0);
	}
	for (i = 0; i < nchildren; i++) {
		if (tops[i] != NULL)
			continue;
		tops[i] = fnvlist_alloc();
		fnvlist_add_string(tops[i], ZPOOL_CONFIG_TYPE,
		    VDEV_TYPE_MISSING);
		fnvlist_add_uint64(tops[i], ZPOOL_CONFIG_ID, i);
		fnvlist_add_uint64(tops[i], ZPOOL_CONFIG_GUID, 0);
	}

	/*
	 * Create pool config based on the best vdev config.
	 */
	config = fnvlist_dup(best_cfg);

	/*
	 * Put this pool's top-level vdevs into a root vdev.
	 */
	pgid = fnvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID);
	nvroot = fnvlist_alloc();
	fnvlist_add_string(nvroot, ZPOOL_CONFIG_TYPE, VDEV_TYPE_ROOT);
	fnvlist_add_uint64(nvroot, ZPOOL_CONFIG_ID, 0ULL);
	fnvlist_add_uint64(nvroot, ZPOOL_CONFIG_GUID, pgid);
	fnvlist_add_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN,
	    (const nvlist_t * const *)tops, nchildren);

	/*
	 * Replace the existing vdev_tree with the new root vdev in
	 * this pool's configuration (remove the old, add the new).
	 */
	fnvlist_add_nvlist(config, ZPOOL_CONFIG_VDEV_TREE, nvroot);

	/*
	 * Drop vdev config elements that should not be present at pool level.
	 */
	fnvlist_remove(config, ZPOOL_CONFIG_GUID);
	fnvlist_remove(config, ZPOOL_CONFIG_TOP_GUID);

	for (i = 0; i < count; i++)
		fnvlist_free(configs[i]);
	kmem_free(configs, count * sizeof (void *));
	for (i = 0; i < nchildren; i++)
		fnvlist_free(tops[i]);
	kmem_free(tops, nchildren * sizeof (void *));
	fnvlist_free(nvroot);
	return (config);
}

int
spa_import_rootpool(const char *name, bool checkpointrewind)
{
	spa_t *spa;
	vdev_t *rvd;
	nvlist_t *config, *nvtop;
	const char *pname;
	int error;

	/*
	 * Read the label from the boot device and generate a configuration.
	 */
	config = spa_generate_rootconf(name);

	mutex_enter(&spa_namespace_lock);
	if (config != NULL) {
		pname = fnvlist_lookup_string(config, ZPOOL_CONFIG_POOL_NAME);
		VERIFY0(strcmp(name, pname));

		if ((spa = spa_lookup(pname)) != NULL) {
			/*
			 * The pool could already be imported,
			 * e.g., after reboot -r.
			 */
			if (spa->spa_state == POOL_STATE_ACTIVE) {
				mutex_exit(&spa_namespace_lock);
				fnvlist_free(config);
				return (0);
			}

			/*
			 * Remove the existing root pool from the namespace so
			 * that we can replace it with the correct config
			 * we just read in.
			 */
			spa_remove(spa);
		}
		spa = spa_add(pname, config, NULL);

		/*
		 * Set spa_ubsync.ub_version as it can be used in vdev_alloc()
		 * via spa_version().
		 */
		if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_VERSION,
		    &spa->spa_ubsync.ub_version) != 0)
			spa->spa_ubsync.ub_version = SPA_VERSION_INITIAL;
	} else if ((spa = spa_lookup(name)) == NULL) {
		mutex_exit(&spa_namespace_lock);
		fnvlist_free(config);
		cmn_err(CE_NOTE, "Cannot find the pool label for '%s'",
		    name);
		return (EIO);
	} else {
		config = fnvlist_dup(spa->spa_config);
	}
	spa->spa_is_root = B_TRUE;
	spa->spa_import_flags = ZFS_IMPORT_VERBATIM;
	if (checkpointrewind) {
		spa->spa_import_flags |= ZFS_IMPORT_CHECKPOINT;
	}

	/*
	 * Build up a vdev tree based on the boot device's label config.
	 */
	nvtop = fnvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE);
	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
	error = spa_config_parse(spa, &rvd, nvtop, NULL, 0,
	    VDEV_ALLOC_ROOTPOOL);
	spa_config_exit(spa, SCL_ALL, FTAG);
	if (error) {
		mutex_exit(&spa_namespace_lock);
		fnvlist_free(config);
		cmn_err(CE_NOTE, "Can not parse the config for pool '%s'",
		    name);
		return (error);
	}

	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
	vdev_free(rvd);
	spa_config_exit(spa, SCL_ALL, FTAG);
	mutex_exit(&spa_namespace_lock);

	fnvlist_free(config);
	return (0);
}

const char *
spa_history_zone(void)
{
	return ("freebsd");
}

void
spa_import_os(spa_t *spa)
{
	(void) spa;
}

void
spa_export_os(spa_t *spa)
{
	(void) spa;
}

void
spa_activate_os(spa_t *spa)
{
	(void) spa;
}

void
spa_deactivate_os(spa_t *spa)
{
	(void) spa;
}
