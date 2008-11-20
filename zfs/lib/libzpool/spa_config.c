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

#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/nvpair.h>
#include <sys/uio.h>
#include <sys/fs/zfs.h>
#include <sys/vdev_impl.h>
#include <sys/zfs_ioctl.h>
#include <sys/utsname.h>
#include <sys/systeminfo.h>
#include <sys/sunddi.h>
#ifdef _KERNEL
#include <sys/kobj.h>
#endif

/*
 * Pool configuration repository.
 *
 * Pool configuration is stored as a packed nvlist on the filesystem.  By
 * default, all pools are stored in /etc/zfs/zpool.cache and loaded on boot
 * (when the ZFS module is loaded).  Pools can also have the 'cachefile'
 * property set that allows them to be stored in an alternate location until
 * the control of external software.
 *
 * For each cache file, we have a single nvlist which holds all the
 * configuration information.  When the module loads, we read this information
 * from /etc/zfs/zpool.cache and populate the SPA namespace.  This namespace is
 * maintained independently in spa.c.  Whenever the namespace is modified, or
 * the configuration of a pool is changed, we call spa_config_sync(), which
 * walks through all the active pools and writes the configuration to disk.
 */

static uint64_t spa_config_generation = 1;

/*
 * This can be overridden in userland to preserve an alternate namespace for
 * userland pools when doing testing.
 */
const char *spa_config_dir = ZPOOL_CACHE_DIR;

/*
 * Called when the module is first loaded, this routine loads the configuration
 * file into the SPA namespace.  It does not actually open or load the pools; it
 * only populates the namespace.
 */
void
spa_config_load(void)
{
	void *buf = NULL;
	nvlist_t *nvlist, *child;
	nvpair_t *nvpair;
	spa_t *spa;
	char pathname[128];
	struct _buf *file;
	uint64_t fsize;

	/*
	 * Open the configuration file.
	 */
	(void) snprintf(pathname, sizeof (pathname), "%s%s/%s",
	    (rootdir != NULL) ? "./" : "", spa_config_dir, ZPOOL_CACHE_FILE);

	file = kobj_open_file(pathname);
	if (file == (struct _buf *)-1)
		return;

	if (kobj_get_filesize(file, &fsize) != 0)
		goto out;

	buf = kmem_alloc(fsize, KM_SLEEP);

	/*
	 * Read the nvlist from the file.
	 */
	if (kobj_read_file(file, buf, fsize, 0) < 0)
		goto out;

	/*
	 * Unpack the nvlist.
	 */
	if (nvlist_unpack(buf, fsize, &nvlist, KM_SLEEP) != 0)
		goto out;

	/*
	 * Iterate over all elements in the nvlist, creating a new spa_t for
	 * each one with the specified configuration.
	 */
	mutex_enter(&spa_namespace_lock);
	nvpair = NULL;
	while ((nvpair = nvlist_next_nvpair(nvlist, nvpair)) != NULL) {

		if (nvpair_type(nvpair) != DATA_TYPE_NVLIST)
			continue;

		VERIFY(nvpair_value_nvlist(nvpair, &child) == 0);

		if (spa_lookup(nvpair_name(nvpair)) != NULL)
			continue;
		spa = spa_add(nvpair_name(nvpair), NULL);

		/*
		 * We blindly duplicate the configuration here.  If it's
		 * invalid, we will catch it when the pool is first opened.
		 */
		VERIFY(nvlist_dup(child, &spa->spa_config, 0) == 0);
	}
	mutex_exit(&spa_namespace_lock);

	nvlist_free(nvlist);

out:
	if (buf != NULL)
		kmem_free(buf, fsize);

	kobj_close_file(file);
}

/*
 * This function is called when destroying or exporting a pool.  It walks the
 * list of active pools, and searches for any that match the given cache file.
 * If there is only one cachefile, then the file is removed immediately,
 * because we won't see the pool when iterating in spa_config_sync().
 */
void
spa_config_check(const char *dir, const char *file)
{
	size_t count = 0;
	char pathname[128];
	spa_t *spa;

	if (dir != NULL && strcmp(dir, "none") == 0)
		return;

	ASSERT(MUTEX_HELD(&spa_namespace_lock));
	spa = NULL;
	while ((spa = spa_next(spa)) != NULL) {
		if (dir == NULL) {
			if (spa->spa_config_dir == NULL)
				count++;
		} else {
			if (spa->spa_config_dir &&
			    strcmp(spa->spa_config_dir, dir) == 0 &&
			    strcmp(spa->spa_config_file, file) == 0)
				count++;
		}
	}

	if (count == 1) {
		if (dir == NULL) {
			dir = spa_config_dir;
			file = ZPOOL_CACHE_FILE;
		}

		(void) snprintf(pathname, sizeof (pathname),
		    "%s/%s", dir, file);
		(void) vn_remove(pathname, UIO_SYSSPACE, RMFILE);
	}
}

typedef struct spa_config_entry {
	list_t		sc_link;
	const char	*sc_dir;
	const char	*sc_file;
	nvlist_t	*sc_nvl;
} spa_config_entry_t;

static void
spa_config_entry_add(list_t *listp, spa_t *spa)
{
	spa_config_entry_t *entry;
	const char *dir, *file;

	mutex_enter(&spa->spa_config_cache_lock);
	if (!spa->spa_config || !spa->spa_name) {
		mutex_exit(&spa->spa_config_cache_lock);
		return;
	}

	if (spa->spa_config_dir) {
		dir = spa->spa_config_dir;
		file = spa->spa_config_file;
	} else {
		dir = spa_config_dir;
		file = ZPOOL_CACHE_FILE;
	}

	if (strcmp(dir, "none") == 0) {
		mutex_exit(&spa->spa_config_cache_lock);
		return;
	}

	for (entry = list_head(listp); entry != NULL;
	    entry = list_next(listp, entry)) {
		if (strcmp(entry->sc_dir, dir) == 0 &&
		    strcmp(entry->sc_file, file) == 0)
			break;
	}

	if (entry == NULL) {
		entry = kmem_alloc(sizeof (spa_config_entry_t), KM_SLEEP);
		entry->sc_dir = dir;
		entry->sc_file = file;
		VERIFY(nvlist_alloc(&entry->sc_nvl, NV_UNIQUE_NAME,
		    KM_SLEEP) == 0);
		list_insert_tail(listp, entry);
	}

	VERIFY(nvlist_add_nvlist(entry->sc_nvl, spa->spa_name,
	    spa->spa_config) == 0);
	mutex_exit(&spa->spa_config_cache_lock);
}

static void
spa_config_entry_write(spa_config_entry_t *entry)
{
	nvlist_t *config = entry->sc_nvl;
	size_t buflen;
	char *buf;
	vnode_t *vp;
	int oflags = FWRITE | FTRUNC | FCREAT | FOFFMAX;
	char pathname[128];
	char pathname2[128];

	/*
	 * Pack the configuration into a buffer.
	 */
	VERIFY(nvlist_size(config, &buflen, NV_ENCODE_XDR) == 0);

	buf = kmem_alloc(buflen, KM_SLEEP);

	VERIFY(nvlist_pack(config, &buf, &buflen, NV_ENCODE_XDR,
	    KM_SLEEP) == 0);

	/*
	 * Write the configuration to disk.  We need to do the traditional
	 * 'write to temporary file, sync, move over original' to make sure we
	 * always have a consistent view of the data.
	 */
	(void) snprintf(pathname, sizeof (pathname), "%s/.%s", entry->sc_dir,
	    entry->sc_file);

	if (vn_open(pathname, UIO_SYSSPACE, oflags, 0644, &vp, CRCREAT, 0) != 0)
		goto out;

	if (vn_rdwr(UIO_WRITE, vp, buf, buflen, 0, UIO_SYSSPACE,
	    0, RLIM64_INFINITY, kcred, NULL) == 0 &&
	    VOP_FSYNC(vp, FSYNC, kcred, NULL) == 0) {
		(void) snprintf(pathname2, sizeof (pathname2), "%s/%s",
		    entry->sc_dir, entry->sc_file);
		(void) vn_rename(pathname, pathname2, UIO_SYSSPACE);
	}

	(void) VOP_CLOSE(vp, oflags, 1, 0, kcred, NULL);
	VN_RELE(vp);

out:
	(void) vn_remove(pathname, UIO_SYSSPACE, RMFILE);
	kmem_free(buf, buflen);
}

/*
 * Synchronize all pools to disk.  This must be called with the namespace lock
 * held.
 */
void
spa_config_sync(void)
{
	spa_t *spa = NULL;
	list_t files = { 0 };
	spa_config_entry_t *entry;

	ASSERT(MUTEX_HELD(&spa_namespace_lock));

	list_create(&files, sizeof (spa_config_entry_t),
	    offsetof(spa_config_entry_t, sc_link));

	/*
	 * Add all known pools to the configuration list, ignoring those with
	 * alternate root paths.
	 */
	spa = NULL;
	while ((spa = spa_next(spa)) != NULL)
		spa_config_entry_add(&files, spa);

	while ((entry = list_head(&files)) != NULL) {
		spa_config_entry_write(entry);
		list_remove(&files, entry);
		nvlist_free(entry->sc_nvl);
		kmem_free(entry, sizeof (spa_config_entry_t));
	}

	spa_config_generation++;
}

/*
 * Sigh.  Inside a local zone, we don't have access to /etc/zfs/zpool.cache,
 * and we don't want to allow the local zone to see all the pools anyway.
 * So we have to invent the ZFS_IOC_CONFIG ioctl to grab the configuration
 * information for all pool visible within the zone.
 */
nvlist_t *
spa_all_configs(uint64_t *generation)
{
	nvlist_t *pools;
	spa_t *spa;

	if (*generation == spa_config_generation)
		return (NULL);

	VERIFY(nvlist_alloc(&pools, NV_UNIQUE_NAME, KM_SLEEP) == 0);

	spa = NULL;
	mutex_enter(&spa_namespace_lock);
	while ((spa = spa_next(spa)) != NULL) {
		if (INGLOBALZONE(curproc) ||
		    zone_dataset_visible(spa_name(spa), NULL)) {
			mutex_enter(&spa->spa_config_cache_lock);
			VERIFY(nvlist_add_nvlist(pools, spa_name(spa),
			    spa->spa_config) == 0);
			mutex_exit(&spa->spa_config_cache_lock);
		}
	}
	mutex_exit(&spa_namespace_lock);

	*generation = spa_config_generation;

	return (pools);
}

void
spa_config_set(spa_t *spa, nvlist_t *config)
{
	mutex_enter(&spa->spa_config_cache_lock);
	if (spa->spa_config != NULL)
		nvlist_free(spa->spa_config);
	spa->spa_config = config;
	mutex_exit(&spa->spa_config_cache_lock);
}

/*
 * Generate the pool's configuration based on the current in-core state.
 * We infer whether to generate a complete config or just one top-level config
 * based on whether vd is the root vdev.
 */
nvlist_t *
spa_config_generate(spa_t *spa, vdev_t *vd, uint64_t txg, int getstats)
{
	nvlist_t *config, *nvroot;
	vdev_t *rvd = spa->spa_root_vdev;
	unsigned long hostid = 0;

	ASSERT(spa_config_held(spa, RW_READER) ||
	    spa_config_held(spa, RW_WRITER));

	if (vd == NULL)
		vd = rvd;

	/*
	 * If txg is -1, report the current value of spa->spa_config_txg.
	 */
	if (txg == -1ULL)
		txg = spa->spa_config_txg;

	VERIFY(nvlist_alloc(&config, NV_UNIQUE_NAME, KM_SLEEP) == 0);

	VERIFY(nvlist_add_uint64(config, ZPOOL_CONFIG_VERSION,
	    spa_version(spa)) == 0);
	VERIFY(nvlist_add_string(config, ZPOOL_CONFIG_POOL_NAME,
	    spa_name(spa)) == 0);
	VERIFY(nvlist_add_uint64(config, ZPOOL_CONFIG_POOL_STATE,
	    spa_state(spa)) == 0);
	VERIFY(nvlist_add_uint64(config, ZPOOL_CONFIG_POOL_TXG,
	    txg) == 0);
	VERIFY(nvlist_add_uint64(config, ZPOOL_CONFIG_POOL_GUID,
	    spa_guid(spa)) == 0);
	(void) ddi_strtoul(hw_serial, NULL, 10, &hostid);
	if (hostid != 0) {
		VERIFY(nvlist_add_uint64(config, ZPOOL_CONFIG_HOSTID,
		    hostid) == 0);
	}
	VERIFY(nvlist_add_string(config, ZPOOL_CONFIG_HOSTNAME,
	    utsname.nodename) == 0);

	if (vd != rvd) {
		VERIFY(nvlist_add_uint64(config, ZPOOL_CONFIG_TOP_GUID,
		    vd->vdev_top->vdev_guid) == 0);
		VERIFY(nvlist_add_uint64(config, ZPOOL_CONFIG_GUID,
		    vd->vdev_guid) == 0);
		if (vd->vdev_isspare)
			VERIFY(nvlist_add_uint64(config, ZPOOL_CONFIG_IS_SPARE,
			    1ULL) == 0);
		if (vd->vdev_islog)
			VERIFY(nvlist_add_uint64(config, ZPOOL_CONFIG_IS_LOG,
			    1ULL) == 0);
		vd = vd->vdev_top;		/* label contains top config */
	}

	nvroot = vdev_config_generate(spa, vd, getstats, B_FALSE, B_FALSE);
	VERIFY(nvlist_add_nvlist(config, ZPOOL_CONFIG_VDEV_TREE, nvroot) == 0);
	nvlist_free(nvroot);

	return (config);
}

/*
 * For a pool that's not currently a booting rootpool, update all disk labels,
 * generate a fresh config based on the current in-core state, and sync the
 * global config cache.
 */
void
spa_config_update(spa_t *spa, int what)
{
	spa_config_update_common(spa, what, FALSE);
}

/*
 * Update all disk labels, generate a fresh config based on the current
 * in-core state, and sync the global config cache (do not sync the config
 * cache if this is a booting rootpool).
 */
void
spa_config_update_common(spa_t *spa, int what, boolean_t isroot)
{
	vdev_t *rvd = spa->spa_root_vdev;
	uint64_t txg;
	int c;

	ASSERT(MUTEX_HELD(&spa_namespace_lock));

	spa_config_enter(spa, RW_WRITER, FTAG);
	txg = spa_last_synced_txg(spa) + 1;
	if (what == SPA_CONFIG_UPDATE_POOL) {
		vdev_config_dirty(rvd);
	} else {
		/*
		 * If we have top-level vdevs that were added but have
		 * not yet been prepared for allocation, do that now.
		 * (It's safe now because the config cache is up to date,
		 * so it will be able to translate the new DVAs.)
		 * See comments in spa_vdev_add() for full details.
		 */
		for (c = 0; c < rvd->vdev_children; c++) {
			vdev_t *tvd = rvd->vdev_child[c];
			if (tvd->vdev_ms_array == 0) {
				vdev_init(tvd, txg);
				vdev_config_dirty(tvd);
			}
		}
	}
	spa_config_exit(spa, FTAG);

	/*
	 * Wait for the mosconfig to be regenerated and synced.
	 */
	txg_wait_synced(spa->spa_dsl_pool, txg);

	/*
	 * Update the global config cache to reflect the new mosconfig.
	 */
	if (!isroot)
		spa_config_sync();

	if (what == SPA_CONFIG_UPDATE_POOL)
		spa_config_update_common(spa, SPA_CONFIG_UPDATE_VDEVS, isroot);
}
