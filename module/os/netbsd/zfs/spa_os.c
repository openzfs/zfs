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
	/*
	 * Root-on-ZFS label probe is not yet implemented.
	 * spa_import_rootpool() turns this NULL into EIO with a
	 * diagnostic.
	 */
	(void) name;
	return (NULL);
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

	spa_namespace_enter(FTAG);
	if (config != NULL) {
		pname = fnvlist_lookup_string(config, ZPOOL_CONFIG_POOL_NAME);
		VERIFY0(strcmp(name, pname));

		if ((spa = spa_lookup(pname)) != NULL) {
			/*
			 * The pool could already be imported,
			 * e.g., after reboot -r.
			 */
			if (spa->spa_state == POOL_STATE_ACTIVE) {
				spa_namespace_exit(FTAG);
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
		spa_namespace_exit(FTAG);
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
		spa_namespace_exit(FTAG);
		fnvlist_free(config);
		cmn_err(CE_NOTE, "Can not parse the config for pool '%s'",
		    name);
		return (error);
	}

	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
	vdev_free(rvd);
	spa_config_exit(spa, SCL_ALL, FTAG);
	spa_namespace_exit(FTAG);

	fnvlist_free(config);
	return (0);
}

const char *
spa_history_zone(void)
{
	return ("netbsd");
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
