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
 * Copyright (c) 2012, 2014 by Delphix. All rights reserved.
 * Copyright (c) 2013 Steven Hartland. All rights reserved.
 * Copyright (c) 2015, 2016 ClusterHQ. All rights reserved.
 */

/*
 * LibZFS_Core (lzc) is intended to replace most functionality in libzfs.
 * It has the following characteristics:
 *
 *  - Thread Safe.  libzfs_core is accessible concurrently from multiple
 *  threads.  This is accomplished primarily by avoiding global data
 *  (e.g. caching).  Since it's thread-safe, there is no reason for a
 *  process to have multiple libzfs "instances".  Therefore, we store
 *  our few pieces of data (e.g. the file descriptor) in global
 *  variables.  The fd is reference-counted so that the libzfs_core
 *  library can be "initialized" multiple times (e.g. by different
 *  consumers within the same process).
 *
 *  - Committed Interface.  The libzfs_core interface will be committed,
 *  therefore consumers can compile against it and be confident that
 *  their code will continue to work on future releases of this code.
 *  Currently, the interface is Evolving (not Committed), but we intend
 *  to commit to it once it is more complete and we determine that it
 *  meets the needs of all consumers.
 *
 *  - Programmatic Error Handling.  libzfs_core communicates errors with
 *  defined error numbers, and doesn't print anything to stdout/stderr.
 *
 *  - Thin Layer.  libzfs_core is a thin layer, marshaling arguments
 *  to/from the kernel ioctls.  There is generally a 1:1 correspondence
 *  between libzfs_core functions and ioctls to /dev/zfs.
 *
 *  - Clear Atomicity.  Because libzfs_core functions are generally 1:1
 *  with kernel ioctls, and kernel ioctls are general atomic, each
 *  libzfs_core function is atomic.  For example, creating multiple
 *  snapshots with a single call to lzc_snapshot() is atomic -- it
 *  can't fail with only some of the requested snapshots created, even
 *  in the event of power loss or system crash.
 *
 *  - Continued libzfs Support.  Some higher-level operations (e.g.
 *  support for "zfs send -R") are too complicated to fit the scope of
 *  libzfs_core.  This functionality will continue to live in libzfs.
 *  Where appropriate, libzfs will use the underlying atomic operations
 *  of libzfs_core.  For example, libzfs may implement "zfs send -R |
 *  zfs receive" by using individual "send one snapshot", rename,
 *  destroy, and "receive one snapshot" operations in libzfs_core.
 *  /sbin/zfs and /zbin/zpool will link with both libzfs and
 *  libzfs_core.  Other consumers should aim to use only libzfs_core,
 *  since that will be the supported, stable interface going forwards.
 */

#include <libzfs_core.h>
#include <ctype.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/dmu_objset.h>
#include <sys/nvpair.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/zfs_ioctl.h>
#include <zfs_prop.h>
#include <zprop_conv.h>

static int g_fd;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_refcount;

int
libzfs_core_init(void)
{
	zpool_prop_init();
	zfs_prop_init();

	(void) pthread_mutex_lock(&g_lock);
	if (g_refcount == 0) {
		g_fd = open("/dev/zfs", O_RDWR);
		if (g_fd < 0) {
			(void) pthread_mutex_unlock(&g_lock);
			return (errno);
		}
	}
	g_refcount++;
	(void) pthread_mutex_unlock(&g_lock);
	return (0);
}

void
libzfs_core_fini(void)
{
	(void) pthread_mutex_lock(&g_lock);
	ASSERT3S(g_refcount, >, 0);
	g_refcount--;
	if (g_refcount == 0)
		(void) close(g_fd);
	(void) pthread_mutex_unlock(&g_lock);
}



static int
lzc_ioctl_impl(zfs_ioc_t ioc, char *name, const char *value,
    const char *log_str, const char **string, uint64_t *cookie,
    uint64_t *guid, uint32_t defer_destroy, dmu_objset_stats_t *objset_stats,
    nvlist_t *source, nvlist_t *config, nvlist_t **resultp)
{
	zfs_cmd_t zc = {"\0"};
	int error = 0;
	char *packed_source = NULL;
	char *packed_config = NULL;
	size_t source_size;
	size_t config_size;

	ASSERT3S(g_refcount, >, 0);

	if (cookie)
		zc.zc_cookie = *cookie;

	if (guid)
		zc.zc_guid = *guid;

	if (name)
		(void) strlcpy(zc.zc_name, name, sizeof (zc.zc_name));

	if (value)
		(void) strlcpy(zc.zc_value, value, sizeof (zc.zc_value));

	if (string && *string)
		(void) strlcpy(zc.zc_string, *string,
		    sizeof (zc.zc_string));

	if (config) {
		packed_config = fnvlist_pack(config, &config_size);
		zc.zc_nvlist_conf = (uint64_t)(uintptr_t)packed_config;
		zc.zc_nvlist_conf_size = config_size;
	}

	if (source) {
		packed_source = fnvlist_pack(source, &source_size);
		zc.zc_nvlist_src = (uint64_t)(uintptr_t)packed_source;
		zc.zc_nvlist_src_size = source_size;
	} else {
		source = fnvlist_alloc();
		packed_source = fnvlist_pack(source, &source_size);
		zc.zc_nvlist_src = (uint64_t)(uintptr_t)packed_source;
		zc.zc_nvlist_src_size = source_size;
		fnvlist_free(source);
	}

	zc.zc_history = (uint64_t)(uintptr_t)log_str;

	if (resultp != NULL) {
		*resultp = NULL;
		zc.zc_nvlist_dst_size = MAX(source_size * 2, 128 * 1024);
		zc.zc_nvlist_dst = (uint64_t)(uintptr_t)
		    malloc(zc.zc_nvlist_dst_size);
		if (zc.zc_nvlist_dst == (uint64_t)0) {
			error = ENOMEM;
			goto out;
		}
	}

	while (ioctl(g_fd, ioc, &zc) != 0) {
		if (errno == ENOMEM && resultp != NULL) {
			free((void *)(uintptr_t)zc.zc_nvlist_dst);
			zc.zc_nvlist_dst_size *= 2;
			zc.zc_nvlist_dst = (uint64_t)(uintptr_t)
			    malloc(zc.zc_nvlist_dst_size);
			if (zc.zc_nvlist_dst == (uint64_t)0) {
				error = ENOMEM;
				goto out;
			}
		} else {
			error = errno;
			break;
		}
	}
	if (zc.zc_nvlist_dst_filled) {
		*resultp = fnvlist_unpack((void *)(uintptr_t)zc.zc_nvlist_dst,
		    zc.zc_nvlist_dst_size);
	}

	errno = zc.zc_real_err;

	if (name)
		(void) strlcpy(name, zc.zc_name, sizeof (zc.zc_name));

	if (cookie)
		*cookie = zc.zc_cookie;

	if (guid)
		*guid = zc.zc_guid;

	if (objset_stats)
		*objset_stats = zc.zc_objset_stats;

out:
	if (packed_config)
		fnvlist_pack_free(packed_config, config_size);
	if (packed_source)
		fnvlist_pack_free(packed_source, source_size);
	free((void *)(uintptr_t)zc.zc_nvlist_dst);

	if (error == 0 && zc.zc_string[0])
		*string = strdup(zc.zc_string);

	return (error);
}

static int
lzc_ioctl_simple(zfs_ioc_t ioc, const char *name,
    nvlist_t *source, nvlist_t **resultp)
{
	char fsname[ZFS_MAX_DATASET_NAME_LEN];
	if (name) {
		strlcpy(fsname, name, sizeof (fsname));
	}
	return (lzc_ioctl_impl(ioc, (name) ? fsname : NULL, NULL, NULL, NULL,
	    NULL, NULL, 0, NULL, source, NULL, resultp));
}

/* Internal helper function */
static int
lzc_pool_log_history(const char *name, const char *message)
{
	nvlist_t *args;
	int err;

	args = fnvlist_alloc();
	fnvlist_add_string(args, "message", message);
	err = lzc_ioctl_simple(ZFS_IOC_LOG_HISTORY, name, args, NULL);
	nvlist_free(args);

	return (err);
}

int
lzc_pool_configs(nvlist_t *opts, nvlist_t **configs)
{
	if (configs == NULL)
		return (EINVAL);

	return (lzc_ioctl_simple(ZFS_IOC_POOL_CONFIGS, NULL, NULL, configs));
}

int
lzc_pool_getprops(const char *pool, nvlist_t *opts, nvlist_t **props)
{
	int err;
	if (props == NULL)
		return (EINVAL);

	err = lzc_ioctl_simple(ZFS_IOC_POOL_GET_PROPS, pool, NULL, props);

	if (props) {
		nvlist_t *nvl = zprop_conv_zpool_to_strings(*props);
		fnvlist_free(*props);
		*props = nvl;
	}

	return (err);
}

int
lzc_pool_export(const char *pool, nvlist_t *opts)
{
	uint64_t force = nvlist_exists(opts, "force");
	uint64_t hardforce = nvlist_exists(opts, "hardforce");
	const char *message;
	char poolname[ZFS_MAX_DATASET_NAME_LEN];

	strlcpy(poolname, pool, sizeof (poolname));

	if (nvlist_lookup_string(opts, "log_history", (char **)&message) != 0)
		message = NULL;

	return (lzc_ioctl_impl(ZFS_IOC_POOL_EXPORT, poolname, NULL, message,
	    NULL, &force, &hardforce, 0, NULL, NULL, NULL, NULL));
}

int
lzc_pool_import(const char *pool, nvlist_t *config, nvlist_t *opts,
    nvlist_t **newconfig)
{
	nvlist_t *props;
	char poolname[ZFS_MAX_DATASET_NAME_LEN];
	const char *message;
	uint64_t guid = 0;
	uint64_t flags = 0;
	int err;

	strlcpy(poolname, pool, sizeof (poolname));

	if (nvlist_lookup_nvlist(config, "props", &props) == 0) {
		if (nvlist_exists(props, "verbatim"))
			flags |= ZFS_IMPORT_VERBATIM;

		if (nvlist_exists(props, "any_host"))
			flags |= ZFS_IMPORT_ANY_HOST;

		if (nvlist_exists(props, "missing_log"))
			flags |= ZFS_IMPORT_MISSING_LOG;

		if (nvlist_exists(props, "only"))
			flags |= ZFS_IMPORT_ONLY;

		if (nvlist_exists(props, "temp_name"))
			flags |= ZFS_IMPORT_TEMP_NAME;
	}

	verify(nvlist_lookup_nvlist(config, "config", &config) == 0);

	verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID,
	    &guid) == 0);

	err = lzc_ioctl_impl(ZFS_IOC_POOL_IMPORT, poolname, NULL, NULL, NULL,
	    &flags, &guid, 0, NULL, NULL, config, newconfig);

	if (err == 0 &&
	    nvlist_lookup_string(opts, "log_history", (char **)&message) == 0) {
		(void) lzc_pool_log_history(pool, message);
	}

	return (err);
}

int
lzc_pool_tryimport(nvlist_t *config, nvlist_t *opts, nvlist_t **newconfig)
{
	return (lzc_ioctl_impl(ZFS_IOC_POOL_TRYIMPORT, NULL, NULL, NULL, NULL,
	    NULL, NULL, 0, NULL, NULL, config, newconfig));
}

int
lzc_pool_stats(const char *pool, nvlist_t *opts, nvlist_t **stats)
{
	return (errno = lzc_ioctl_simple(ZFS_IOC_POOL_STATS, pool, NULL,
	    stats));
}

int
lzc_create(const char *fsname, dmu_objset_type_t type, nvlist_t *props)
{
	return (lzc_create_ext(fsname, dmu_objset_type_name(type), props, NULL,
	    NULL));
}

int
lzc_create_ext(const char *fsname, const char *type, nvlist_t *props,
    nvlist_t *opts, nvlist_t **errlist)
{
	const char *message;
	dmu_objset_type_t itype;
	int error;

	if ((error = dmu_objset_get_type(type, &itype)))
		return (error);

	nvlist_t *args = fnvlist_alloc();
	fnvlist_add_int32(args, "type", itype);
	if (props != NULL) {
		props = zprop_conv_zfs_from_strings(props);
		fnvlist_add_nvlist(args, "props", props);
		fnvlist_free(props);
	}
	error = (lzc_ioctl_simple(ZFS_IOC_CREATE, fsname, args, errlist));
	nvlist_free(args);

	if (error == 0 &&
	    nvlist_lookup_string(opts, "log_history", (char **)&message) == 0) {
		int err = lzc_pool_log_history(fsname, message);
		if (errlist)
			nvlist_add_int32(*errlist, "log_history", err);
	}

	return (error);
}

int
lzc_clone(const char *fsname, const char *origin,
    nvlist_t *props)
{
	return (lzc_clone_ext(fsname, origin, props, NULL, NULL));
}

int
lzc_clone_ext(const char *fsname, const char *origin,
    nvlist_t *props, nvlist_t *opts, nvlist_t **errlist)
{
	const char *message;
	int error;
	nvlist_t *args = fnvlist_alloc();

	fnvlist_add_string(args, "origin", origin);
	if (props != NULL) {
		props = zprop_conv_zfs_from_strings(props);
		fnvlist_add_nvlist(args, "props", props);
		fnvlist_free(props);
	}
	error = lzc_ioctl_simple(ZFS_IOC_CLONE, fsname, args, errlist);
	nvlist_free(args);

	if (error == 0 &&
	    nvlist_lookup_string(opts, "log_history", (char **)&message) == 0) {
		int err = lzc_pool_log_history(fsname, message);
		if (errlist)
			nvlist_add_int32(*errlist, "log_history", err);
	}

	return (error);
}

int
lzc_promote(const char *fsname, nvlist_t *opts, nvlist_t **outnvl)
{
	const char *conflsnap = NULL;
	const char *message;
	dmu_objset_stats_t objset_stats;
	int error;
	char name[ZFS_MAX_DATASET_NAME_LEN];
	const char *origin = NULL;

	strlcpy(name, fsname, sizeof (name));
	if ((error = lzc_ioctl_impl(ZFS_IOC_OBJSET_STATS, name, NULL,
	    NULL, NULL, NULL, NULL, 0, &objset_stats, NULL, NULL, NULL)))
		return (error);

	if (objset_stats.dds_origin[0] != '\0')
		origin = objset_stats.dds_origin;

	strlcpy(name, fsname, sizeof (name));
	error = lzc_ioctl_impl(ZFS_IOC_PROMOTE, name, origin, NULL, &conflsnap,
	    NULL, NULL, 0, NULL, NULL, NULL, outnvl);

	if (error == 0 &&
	    nvlist_lookup_string(opts, "log_history", (char **)&message) == 0) {
		int err = lzc_pool_log_history(fsname, message);
		if (outnvl)
			nvlist_add_int32(*outnvl, "log_history", err);
	}

	return (error);
}

int
lzc_set_props(const char *fsname, nvlist_t *props, nvlist_t *opts,
    nvlist_t **errlist)
{
	const char *message;
	uint64_t received = nvlist_exists(opts, "received");
	int error;
	char name[ZFS_MAX_DATASET_NAME_LEN];

	strlcpy(name, fsname, sizeof (name));

	/*
	 * XXX: We cannot emulate default atomic behavior, so we do not check
	 * for noatomic.
	 */
	props = zprop_conv_zfs_from_strings(props);
	error = lzc_ioctl_impl(ZFS_IOC_SET_PROP, name, NULL, NULL, NULL,
	    &received, NULL, 0, NULL, props, NULL, errlist);
	nvlist_free(props);

	if (error == 0 &&
	    nvlist_lookup_string(opts, "log_history", (char **)&message) == 0) {
		int err = lzc_pool_log_history(fsname, message);
		if (errlist)
			nvlist_add_int32(*errlist, "log_history", err);
	}

	return (error);
}

/*
 * Creates snapshots.
 *
 * The keys in the snaps nvlist are the snapshots to be created.
 * They must all be in the same pool.
 *
 * The props nvlist is properties to set.  Currently only user properties
 * are supported.  { user:prop_name -> string value }
 *
 * The opts nvlist is intended to allow for extensions. Currently, only history
 * logging is supported. { log_history -> string value }
 *
 * The returned results nvlist will have an entry for each snapshot that failed.
 * The value will be the (int32) error code.
 *
 * The return value will be 0 if all snapshots were created, otherwise it will
 * be the errno of a (unspecified) snapshot that failed.
 */
int
lzc_snapshot_ext(nvlist_t *snaps, nvlist_t *props, nvlist_t *opts,
    nvlist_t **errlist)
{
	const char *message;
	nvpair_t *elem;
	nvlist_t *args;
	int error;
	char pool[ZFS_MAX_DATASET_NAME_LEN];

	*errlist = NULL;

	/* determine the pool name */
	elem = nvlist_next_nvpair(snaps, NULL);
	if (elem == NULL)
		return (0);
	(void) strlcpy(pool, nvpair_name(elem), sizeof (pool));
	pool[strcspn(pool, "/@")] = '\0';

	args = fnvlist_alloc();
	fnvlist_add_nvlist(args, "snaps", snaps);
	if (props != NULL) {
		props = zprop_conv_zfs_from_strings(props);
		fnvlist_add_nvlist(args, "props", props);
		fnvlist_free(props);
	}

	error = lzc_ioctl_simple(ZFS_IOC_SNAPSHOT, pool, args, errlist);

	nvlist_free(args);

	if (error == 0 &&
	    nvlist_lookup_string(opts, "log_history", (char **)&message) == 0) {
		int err = lzc_pool_log_history(pool, message);
		if (errlist)
			nvlist_add_int32(*errlist, "log_history", err);
	}

	return (error);
}

int
lzc_snapshot(nvlist_t *snaps, nvlist_t *props, nvlist_t **errlist)
{
	return (lzc_snapshot_ext(snaps, props, NULL, errlist));
}

/*
 * Destroys snapshots.
 *
 * The keys in the snaps nvlist are the snapshots to be destroyed.
 * They must all be in the same pool.
 *
 * Snapshots that do not exist will be silently ignored.
 *
 * If 'defer' is not set, and a snapshot has user holds or clones, the
 * destroy operation will fail and none of the snapshots will be
 * destroyed.
 *
 * If 'defer' is set, and a snapshot has user holds or clones, it will be
 * marked for deferred destruction, and will be destroyed when the last hold
 * or clone is removed/destroyed.
 *
 * The return value will be 0 if all snapshots were destroyed (or marked for
 * later destruction if 'defer' is set) or didn't exist to begin with.
 *
 * Otherwise the return value will be the errno of a (unspecified) snapshot
 * that failed, no snapshots will be destroyed, and the errlist will have an
 * entry for each snapshot that failed.  The value in the errlist will be
 * the (int32) error code.
 */
static int
lzc_destroy_snaps_impl(nvlist_t *snaps, boolean_t defer,
    const char *log_history, nvlist_t **errlist)
{
	nvpair_t *elem;
	nvlist_t *args;
	int error;
	char pool[ZFS_MAX_DATASET_NAME_LEN];

	/* determine the pool name */
	elem = nvlist_next_nvpair(snaps, NULL);
	if (elem == NULL)
		return (0);
	(void) strlcpy(pool, nvpair_name(elem), sizeof (pool));
	pool[strcspn(pool, "/@")] = '\0';

	args = fnvlist_alloc();
	fnvlist_add_nvlist(args, "snaps", snaps);
	if (defer)
		fnvlist_add_boolean(args, "defer");

	error = lzc_ioctl_simple(ZFS_IOC_DESTROY_SNAPS, pool, args, errlist);

	if (error == 0 && log_history != NULL) {
		int err = lzc_pool_log_history(pool, log_history);
		if (errlist)
			nvlist_add_int32(*errlist, "log_history", err);
	}

	nvlist_free(args);

	return (error);
}

int
lzc_destroy_snaps(nvlist_t *snaps, boolean_t defer, nvlist_t **errlist)
{
	return (lzc_destroy_snaps_impl(snaps, defer, NULL, errlist));
}

/*
 * Destroys snapshots.
 *
 * The keys in the snaps nvlist are the snapshots to be destroyed.
 * They must all be in pool specified by the pool string.
 *
 * The opts nvlist is intended to allow for extensions. Currently, only history
 * logging and the defer property are supported.
 *
 * { log_history -> string value }
 * { defer -> boolean }
 *
 * If the defer property is not set, and a snapshot has user holds or clones,
 * the destroy operation will fail and none of the snapshots will be destroyed.
 *
 * If the defer property is set, and a snapshot has user holds or clones, it
 * will be marked for deferred destruction, and will be destroyed when the last
 * hold or clone is removed/destroyed.
 *
 * The return value will be 0 if all snapshots were destroyed (or marked for
 * later destruction if 'defer' is set) or didn't exist to begin with.
 *
 * Otherwise the return value will be the errno of a (unspecified) snapshot
 * that failed, no snapshots will be destroyed, and the errlist will have an
 * entry for each snapshot that failed. The value in the errlist will be the
 * (int32) error code.
 */
int
lzc_destroy_snaps_ext(const char *pool, nvlist_t *snaps, nvlist_t *opts,
    nvlist_t **errlist)
{
	const char *message;
	boolean_t defer = nvlist_exists(opts, "defer");

	if (nvlist_lookup_string(opts, "log_history", (char **)&message) != 0)
		message = NULL;

	return (lzc_destroy_snaps_impl(snaps, defer, message, errlist));
}

int
lzc_snaprange_space(const char *firstsnap, const char *lastsnap,
    uint64_t *usedp)
{
	nvlist_t *args;
	nvlist_t *result;
	int err;
	char fs[ZFS_MAX_DATASET_NAME_LEN];
	char *atp;

	/* determine the fs name */
	(void) strlcpy(fs, firstsnap, sizeof (fs));
	atp = strchr(fs, '@');
	if (atp == NULL)
		return (EINVAL);
	*atp = '\0';

	args = fnvlist_alloc();
	fnvlist_add_string(args, "firstsnap", firstsnap);

	err = lzc_ioctl_simple(ZFS_IOC_SPACE_SNAPS, lastsnap, args, &result);

	nvlist_free(args);
	if (err == 0 && usedp != NULL)
		*usedp = fnvlist_lookup_uint64(result, "used");
	fnvlist_free(result);

	return (err);
}

boolean_t
lzc_exists(const char *dataset)
{
	return
	    (lzc_ioctl_simple(ZFS_IOC_OBJSET_STATS, dataset, NULL, NULL) == 0);
}

/*
 * Create "user holds" on snapshots.  If there is a hold on a snapshot,
 * the snapshot can not be destroyed.  (However, it can be marked for deletion
 * by lzc_destroy_snaps(defer=B_TRUE).)
 *
 * The keys in the nvlist are snapshot names.
 * The snapshots must all be in the same pool.
 * The value is the name of the hold (string type).
 *
 * If cleanup_fd is not -1, it must be the result of open("/dev/zfs", O_EXCL).
 * In this case, when the cleanup_fd is closed (including on process
 * termination), the holds will be released.  If the system is shut down
 * uncleanly, the holds will be released when the pool is next opened
 * or imported.
 *
 * Holds for snapshots which don't exist will be skipped and have an entry
 * added to errlist, but will not cause an overall failure.
 *
 * The return value will be 0 if all holds, for snapshots that existed,
 * were successfully created.
 *
 * Otherwise the return value will be the errno of a (unspecified) hold that
 * failed and no holds will be created.
 *
 * In all cases the errlist will have an entry for each hold that failed
 * (name = snapshot), with its value being the error code (int32).
 */
static int
lzc_hold_impl(nvlist_t *holds, int cleanup_fd, const char *log_history,
    nvlist_t **errlist)
{
	char pool[ZFS_MAX_DATASET_NAME_LEN];
	nvpair_t *elem;
	nvlist_t *args;
	int error;

	/* determine the pool name */
	elem = nvlist_next_nvpair(holds, NULL);
	if (elem == NULL)
		return (0);
	(void) strlcpy(pool, nvpair_name(elem), sizeof (pool));
	pool[strcspn(pool, "/@")] = '\0';

	args = fnvlist_alloc();
	fnvlist_add_nvlist(args, "holds", holds);

	if (cleanup_fd != -1)
		fnvlist_add_int32(args, "cleanup_fd", cleanup_fd);

	error = (lzc_ioctl_simple(ZFS_IOC_HOLD, pool, args, errlist));

	nvlist_free(args);

	if (error == 0 && log_history != NULL) {
		int err = lzc_pool_log_history(pool, log_history);
		if (errlist)
			nvlist_add_int32(*errlist, "log_history", err);
	}

	return (error);
}

int
lzc_hold_ext(nvlist_t *holds, nvlist_t *opts, nvlist_t **errlist)
{
	const char *message;
	int cleanup_fd;

	if (nvlist_lookup_int32(opts, "cleanup_fd", &cleanup_fd) != 0)
		cleanup_fd = -1;

	if (nvlist_lookup_string(opts, "log_history", (char **)&message) != 0)
		message = NULL;

	return (lzc_hold_impl(holds, cleanup_fd, message, errlist));
}

int
lzc_hold(nvlist_t *holds, int cleanup_fd, nvlist_t **errlist)
{
	return (lzc_hold_impl(holds, cleanup_fd, NULL, errlist));
}

/*
 * Release "user holds" on snapshots.  If the snapshot has been marked for
 * deferred destroy (by lzc_destroy_snaps(defer=B_TRUE)), it does not have
 * any clones, and all the user holds are removed, then the snapshot will be
 * destroyed.
 *
 * The keys in the nvlist are snapshot names.
 * The snapshots must all be in the same pool.
 * The value is a nvlist whose keys are the holds to remove.
 *
 * Holds which failed to release because they didn't exist will have an entry
 * added to errlist, but will not cause an overall failure.
 *
 * The return value will be 0 if the nvl holds was empty or all holds that
 * existed, were successfully removed.
 *
 * Otherwise the return value will be the errno of a (unspecified) hold that
 * failed to release and no holds will be released.
 *
 * In all cases the errlist will have an entry for each hold that failed to
 * to release.
 */
int
lzc_release_ext(nvlist_t *holds, nvlist_t *opts, nvlist_t **errlist)
{
	char pool[ZFS_MAX_DATASET_NAME_LEN];
	char const *message;
	nvpair_t *elem;
	int error;

	/* determine the pool name */
	elem = nvlist_next_nvpair(holds, NULL);
	if (elem == NULL)
		return (0);
	(void) strlcpy(pool, nvpair_name(elem), sizeof (pool));
	pool[strcspn(pool, "/@")] = '\0';

	error = lzc_ioctl_simple(ZFS_IOC_RELEASE, pool, holds, errlist);

	if (error == 0 &&
	    nvlist_lookup_string(opts, "log_history", (char **)&message) == 0) {
		int err = lzc_pool_log_history(pool, message);
		if (errlist)
			nvlist_add_int32(*errlist, "log_history", err);
	}

	return (error);
}

int
lzc_release(nvlist_t *holds, nvlist_t **errlist)
{
	return (lzc_release_ext(holds, NULL, errlist));
}

/*
 * Retrieve list of user holds on the specified snapshot.
 *
 * On success, *holdsp will be set to a nvlist which the caller must free.
 * The keys are the names of the holds, and the value is the creation time
 * of the hold (uint64) in seconds since the epoch.
 */
int
lzc_get_holds(const char *snapname, nvlist_t **holdsp)
{
	int error;
	nvlist_t *innvl = fnvlist_alloc();
	error = lzc_ioctl_simple(ZFS_IOC_GET_HOLDS, snapname, innvl, holdsp);
	fnvlist_free(innvl);
	return (error);
}

/*
 * Generate a zfs send stream for the specified snapshot and write it to
 * the specified file descriptor.
 *
 * "snapname" is the full name of the snapshot to send (e.g. "pool/fs@snap")
 *
 * If "from" is NULL, a full (non-incremental) stream will be sent.
 * If "from" is non-NULL, it must be the full name of a snapshot or
 * bookmark to send an incremental from (e.g. "pool/fs@earlier_snap" or
 * "pool/fs#earlier_bmark").  If non-NULL, the specified snapshot or
 * bookmark must represent an earlier point in the history of "snapname").
 * It can be an earlier snapshot in the same filesystem or zvol as "snapname",
 * or it can be the origin of "snapname"'s filesystem, or an earlier
 * snapshot in the origin, etc.
 *
 * "fd" is the file descriptor to write the send stream to.
 *
 * If "flags" contains LZC_SEND_FLAG_LARGE_BLOCK, the stream is permitted
 * to contain DRR_WRITE records with drr_length > 128K, and DRR_OBJECT
 * records with drr_blksz > 128K.
 *
 * If "flags" contains LZC_SEND_FLAG_EMBED_DATA, the stream is permitted
 * to contain DRR_WRITE_EMBEDDED records with drr_etype==BP_EMBEDDED_TYPE_DATA,
 * which the receiving system must support (as indicated by support
 * for the "embedded_data" feature).
 */
int
lzc_send(const char *snapname, const char *from, int fd,
    enum lzc_send_flags flags)
{
	return (lzc_send_resume(snapname, from, fd, flags, 0, 0));
}

int
lzc_send_resume(const char *snapname, const char *from, int fd,
    enum lzc_send_flags flags, uint64_t resumeobj, uint64_t resumeoff)
{
	nvlist_t *args;
	int err;

	args = fnvlist_alloc();
	fnvlist_add_int32(args, "fd", fd);
	if (from != NULL)
		fnvlist_add_string(args, "fromsnap", from);
	if (flags & LZC_SEND_FLAG_LARGE_BLOCK)
		fnvlist_add_boolean(args, "largeblockok");
	if (flags & LZC_SEND_FLAG_EMBED_DATA)
		fnvlist_add_boolean(args, "embedok");
	if (resumeobj != 0 || resumeoff != 0) {
		fnvlist_add_uint64(args, "resume_object", resumeobj);
		fnvlist_add_uint64(args, "resume_offset", resumeoff);
	}
	err = lzc_ioctl_simple(ZFS_IOC_SEND_NEW, snapname, args, NULL);
	nvlist_free(args);
	return (err);
}

/*
 * "from" can be NULL, a snapshot, or a bookmark.
 *
 * If from is NULL, a full (non-incremental) stream will be estimated.  This
 * is calculated very efficiently.
 *
 * If from is a snapshot, lzc_send_space uses the deadlists attached to
 * each snapshot to efficiently estimate the stream size.
 *
 * If from is a bookmark, the indirect blocks in the destination snapshot
 * are traversed, looking for blocks with a birth time since the creation TXG of
 * the snapshot this bookmark was created from.  This will result in
 * significantly more I/O and be less efficient than a send space estimation on
 * an equivalent snapshot.
 */
int
lzc_send_space(const char *snapname, const char *from, uint64_t *spacep)
{
	nvlist_t *args;
	nvlist_t *result;
	int err;

	args = fnvlist_alloc();
	if (from != NULL)
		fnvlist_add_string(args, "from", from);
	err = lzc_ioctl_simple(ZFS_IOC_SEND_SPACE, snapname, args, &result);
	nvlist_free(args);
	if (err == 0)
		*spacep = fnvlist_lookup_uint64(result, "space");
	nvlist_free(result);
	return (err);
}

/*
 * Query number of bytes written in a given send stream
 * for a given snapshot thus far.
 */
int
lzc_send_progress(const char *snapname, int fd, uint64_t *bytesp)
{
	nvlist_t *result;
	int err;
	uint64_t fildes = fd;
	char name[ZFS_MAX_DATASET_NAME_LEN];
	strlcpy(name, snapname, sizeof (name));

	err = lzc_ioctl_impl(ZFS_IOC_SEND_PROGRESS, name, NULL, NULL,
	    NULL, &fildes, NULL, 0, NULL, NULL, NULL, &result);
	if (err == 0)
		*bytesp = fnvlist_lookup_uint64(result, "offset");
	nvlist_free(result);
	return (err);
}

static int
recv_read(int fd, void *buf, int ilen)
{
	char *cp = buf;
	int rv;
	int len = ilen;

	do {
		rv = read(fd, cp, len);
		cp += rv;
		len -= rv;
	} while (rv > 0);

	if (rv < 0 || len != 0)
		return (EIO);

	return (0);
}

/*
 * Linux adds ZFS_IOC_RECV_NEW for resumable streams and preserves the legacy
 * ZFS_IOC_RECV user/kernel interface.  The new interface supports all stream
 * options but is currently only used for resumable streams.  This way updated
 * user space utilities will interoperate with older kernel modules.
 *
 * Non-Linux OpenZFS platforms have opted to modify the legacy interface.
 */
static int
recv_impl(const char *snapname, nvlist_t *props, const char *origin,
    boolean_t force, boolean_t resumable, int input_fd,
    const dmu_replay_record_t *begin_record, int cleanup_fd,
    uint64_t *read_bytes, uint64_t *errflags, uint64_t *action_handle,
    nvlist_t **errors)
{
	dmu_replay_record_t drr;
	char fsname[MAXPATHLEN];
	char *atp;
	int error;

	/* Set 'fsname' to the name of containing filesystem */
	(void) strlcpy(fsname, snapname, sizeof (fsname));
	atp = strchr(fsname, '@');
	if (atp != NULL)
		*atp = '\0';

	/* If the fs does not exist, try its parent. */
	if (!lzc_exists(fsname)) {
		char *slashp = strrchr(fsname, '/');
		if (slashp == NULL)
			return (ENOENT);
		*slashp = '\0';
	}

	/*
	 * The begin_record is normally a non-byteswapped BEGIN record.
	 * For resumable streams it may be set to any non-byteswapped
	 * dmu_replay_record_t.
	 */
	if (begin_record == NULL) {
		error = recv_read(input_fd, &drr, sizeof (drr));
		if (error != 0)
			return (error);
	} else {
		drr = *begin_record;
	}

	/* if snapshot name is not provided try to take it from the stream */
	if (atp == NULL) {
		atp = strchr(drr.drr_u.drr_begin.drr_toname, '@');
		if (atp == NULL)
			return (EINVAL);

		if (strlen(fsname) + strlen(atp) >= sizeof (fsname))
			return (ENAMETOOLONG);

		strcat(fsname, atp);
	}

	if (resumable) {
		nvlist_t *outnvl = NULL;
		nvlist_t *innvl = fnvlist_alloc();

		fnvlist_add_string(innvl, "snapname", snapname);

		if (props != NULL) {
			props = zprop_conv_zfs_from_strings(props);
			fnvlist_add_nvlist(innvl, "props", props);
			fnvlist_free(props);
		}

		if (origin != NULL && strlen(origin))
			fnvlist_add_string(innvl, "origin", origin);

		fnvlist_add_byte_array(innvl, "begin_record",
		    (uchar_t *) &drr, sizeof (drr));

		fnvlist_add_int32(innvl, "input_fd", input_fd);

		if (force)
			fnvlist_add_boolean(innvl, "force");

		if (resumable)
			fnvlist_add_boolean(innvl, "resumable");

		if (cleanup_fd >= 0)
			fnvlist_add_int32(innvl, "cleanup_fd", cleanup_fd);

		if (action_handle != NULL)
			fnvlist_add_uint64(innvl, "action_handle",
			    *action_handle);

		error = lzc_ioctl_simple(ZFS_IOC_RECV_NEW, fsname, innvl,
		    &outnvl);

		if (error == 0 && read_bytes != NULL)
			error = nvlist_lookup_uint64(outnvl, "read_bytes",
			    read_bytes);

		if (error == 0 && errflags != NULL)
			error = nvlist_lookup_uint64(outnvl, "error_flags",
			    errflags);

		if (error == 0 && action_handle != NULL)
			error = nvlist_lookup_uint64(outnvl, "action_handle",
			    action_handle);

		if (error == 0 && errors != NULL) {
			nvlist_t *nvl;
			error = nvlist_lookup_nvlist(outnvl, "errors", &nvl);
			if (error == 0)
				*errors = fnvlist_dup(nvl);
		}

		fnvlist_free(innvl);
		fnvlist_free(outnvl);
	} else {
		zfs_cmd_t zc = {"\0"};
		char *packed = NULL;
		size_t size;

		ASSERT3S(g_refcount, >, 0);

		(void) strlcpy(zc.zc_name, fsname, sizeof (zc.zc_value));
		(void) strlcpy(zc.zc_value, snapname, sizeof (zc.zc_value));

		if (props != NULL) {
			packed = fnvlist_pack(props, &size);
			zc.zc_nvlist_src = (uint64_t)(uintptr_t)packed;
			zc.zc_nvlist_src_size = size;
		}

		if (origin != NULL)
			(void) strlcpy(zc.zc_string, origin,
			    sizeof (zc.zc_string));

		ASSERT3S(drr.drr_type, ==, DRR_BEGIN);
		zc.zc_begin_record = drr.drr_u.drr_begin;
		zc.zc_guid = force;
		zc.zc_cookie = input_fd;
		zc.zc_cleanup_fd = -1;
		zc.zc_action_handle = 0;

		if (cleanup_fd >= 0)
			zc.zc_cleanup_fd = cleanup_fd;

		if (action_handle != NULL)
			zc.zc_action_handle = *action_handle;

		zc.zc_nvlist_dst_size = 128 * 1024;
		zc.zc_nvlist_dst = (uint64_t)(uintptr_t)
		    malloc(zc.zc_nvlist_dst_size);

		error = ioctl(g_fd, ZFS_IOC_RECV, &zc);
		if (error != 0) {
			error = errno;
		} else {
			if (read_bytes != NULL)
				*read_bytes = zc.zc_cookie;

			if (errflags != NULL)
				*errflags = zc.zc_obj;

			if (action_handle != NULL)
				*action_handle = zc.zc_action_handle;

			if (errors != NULL)
				VERIFY0(nvlist_unpack(
				    (void *)(uintptr_t)zc.zc_nvlist_dst,
				    zc.zc_nvlist_dst_size, errors, KM_SLEEP));
		}

		if (packed != NULL)
			fnvlist_pack_free(packed, size);
		free((void *)(uintptr_t)zc.zc_nvlist_dst);
	}

	return (error);
}

/*
 * The simplest receive case: receive from the specified fd, creating the
 * specified snapshot.  Apply the specified properties as "received" properties
 * (which can be overridden by locally-set properties).  If the stream is a
 * clone, its origin snapshot must be specified by 'origin'.  The 'force'
 * flag will cause the target filesystem to be rolled back or destroyed if
 * necessary to receive.
 *
 * Return 0 on success or an errno on failure.
 *
 * Note: this interface does not work on dedup'd streams
 * (those with DMU_BACKUP_FEATURE_DEDUP).
 */
int
lzc_receive(const char *snapname, nvlist_t *props, const char *origin,
    boolean_t force, int fd)
{
	return (recv_impl(snapname, props, origin, force, B_FALSE, fd,
	    NULL, -1, NULL, NULL, NULL, NULL));
}

/*
 * Like lzc_receive, but if the receive fails due to premature stream
 * termination, the intermediate state will be preserved on disk.  In this
 * case, ECKSUM will be returned.  The receive may subsequently be resumed
 * with a resuming send stream generated by lzc_send_resume().
 */
int
lzc_receive_resumable(const char *snapname, nvlist_t *props, const char *origin,
    boolean_t force, int fd)
{
	return (recv_impl(snapname, props, origin, force, B_TRUE, fd,
	    NULL, -1, NULL, NULL, NULL, NULL));
}

/*
 * Like lzc_receive, but allows the caller to read the begin record and then to
 * pass it in.  That could be useful if the caller wants to derive, for example,
 * the snapname or the origin parameters based on the information contained in
 * the begin record.
 * The begin record must be in its original form as read from the stream,
 * in other words, it should not be byteswapped.
 *
 * The 'resumable' parameter allows to obtain the same behavior as with
 * lzc_receive_resumable.
 */
int
lzc_receive_with_header(const char *snapname, nvlist_t *props,
    const char *origin, boolean_t force, boolean_t resumable, int fd,
    const dmu_replay_record_t *begin_record)
{
	if (begin_record == NULL)
		return (EINVAL);
	return (recv_impl(snapname, props, origin, force, resumable, fd,
	    begin_record, -1, NULL, NULL, NULL, NULL));
}

/*
 * Like lzc_receive, but allows the caller to pass all supported arguments
 * and retrieve all values returned.  The only additional input parameter
 * is 'cleanup_fd' which is used to set a cleanup-on-exit file descriptor.
 *
 * The following parameters all provide return values.  Several may be set
 * in the failure case and will contain additional information.
 *
 * The 'read_bytes' value will be set to the total number of bytes read.
 *
 * The 'errflags' value will contain zprop_errflags_t flags which are
 * used to describe any failures.
 *
 * The 'action_handle' is used to pass the handle for this guid/ds mapping.
 * It should be set to zero on first call and will contain an updated handle
 * on success, it should be passed in subsequent calls.
 *
 * The 'errors' nvlist contains an entry for each unapplied received
 * property.  Callers are responsible for freeing this nvlist.
 */
int lzc_receive_one(const char *snapname, nvlist_t *props,
    const char *origin, boolean_t force, boolean_t resumable, int input_fd,
    const dmu_replay_record_t *begin_record, int cleanup_fd,
    uint64_t *read_bytes, uint64_t *errflags, uint64_t *action_handle,
    nvlist_t **errors)
{
	return (recv_impl(snapname, props, origin, force, resumable,
	    input_fd, begin_record, cleanup_fd, read_bytes, errflags,
	    action_handle, errors));
}

/*
 * Roll back this filesystem or volume to its most recent snapshot.
 * If snapnamebuf is not NULL, it will be filled in with the name
 * of the most recent snapshot.
 *
 * Return 0 on success or an errno on failure.
 */
int
lzc_rollback_ext(const char *fsname, char *snapnamebuf, int snapnamelen,
    nvlist_t *opts)
{
	const char *message;
	nvlist_t *args;
	nvlist_t *result;
	int err;

	args = fnvlist_alloc();
	err = lzc_ioctl_simple(ZFS_IOC_ROLLBACK, fsname, args, &result);
	nvlist_free(args);
	if (err == 0 && snapnamebuf != NULL) {
		const char *snapname = fnvlist_lookup_string(result, "target");
		(void) strlcpy(snapnamebuf, snapname, snapnamelen);
	}

	if (err == 0 &&
	    nvlist_lookup_string(opts, "log_history", (char **)&message) == 0) {
		(void) lzc_pool_log_history(fsname, message);
	}

	return (err);
}

int
lzc_rollback(const char *fsname, char *snapnamebuf, int snapnamelen)
{
	return (lzc_rollback_ext(fsname, snapnamebuf, snapnamelen, NULL));
}

/*
 * Creates bookmarks.
 *
 * The bookmarks nvlist maps from name of the bookmark (e.g. "pool/fs#bmark") to
 * the name of the snapshot (e.g. "pool/fs@snap").  All the bookmarks and
 * snapshots must be in the same pool.
 *
 * The returned results nvlist will have an entry for each bookmark that failed.
 * The value will be the (int32) error code.
 *
 * The return value will be 0 if all bookmarks were created, otherwise it will
 * be the errno of a (undetermined) bookmarks that failed.
 */
int
lzc_bookmark_ext(nvlist_t *bookmarks, nvlist_t *opts, nvlist_t **errlist)
{
	const char *message;
	nvpair_t *elem;
	int error;
	char pool[ZFS_MAX_DATASET_NAME_LEN];

	/* determine the pool name */
	elem = nvlist_next_nvpair(bookmarks, NULL);
	if (elem == NULL)
		return (0);
	(void) strlcpy(pool, nvpair_name(elem), sizeof (pool));
	pool[strcspn(pool, "/#")] = '\0';

	error = lzc_ioctl_simple(ZFS_IOC_BOOKMARK, pool, bookmarks, errlist);

	if (error == 0 &&
	    nvlist_lookup_string(opts, "log_history", (char **)&message) == 0) {
		int err = lzc_pool_log_history(pool, message);
		if (errlist)
			nvlist_add_int32(*errlist, "log_history", err);
	}

	return (error);
}

int
lzc_bookmark(nvlist_t *bookmarks, nvlist_t **errlist)
{
	return (lzc_bookmark_ext(bookmarks, NULL, errlist));
}

/*
 * Retrieve bookmarks.
 *
 * Retrieve the list of bookmarks for the given file system. The props
 * parameter is an nvlist of property names (with no values) that will be
 * returned for each bookmark.
 *
 * The following are valid properties on bookmarks, all of which are numbers
 * (represented as uint64 in the nvlist)
 *
 * "guid" - globally unique identifier of the snapshot it refers to
 * "createtxg" - txg when the snapshot it refers to was created
 * "creation" - timestamp when the snapshot it refers to was created
 *
 * The format of the returned nvlist as follows:
 * <short name of bookmark> -> {
 *     <name of property> -> {
 *         "value" -> uint64
 *     }
 *  }
 */
int
lzc_get_bookmarks(const char *fsname, nvlist_t *props, nvlist_t **bmarks)
{
	return (lzc_ioctl_simple(ZFS_IOC_GET_BOOKMARKS, fsname, props,
	    bmarks));
}

/*
 * Destroys bookmarks.
 *
 * The keys in the bmarks nvlist are the bookmarks to be destroyed.
 * They must all be in the same pool.  Bookmarks are specified as
 * <fs>#<bmark>.
 *
 * Bookmarks that do not exist will be silently ignored.
 *
 * The return value will be 0 if all bookmarks that existed were destroyed.
 *
 * Otherwise the return value will be the errno of a (undetermined) bookmark
 * that failed, no bookmarks will be destroyed, and the errlist will have an
 * entry for each bookmarks that failed.  The value in the errlist will be
 * the (int32) error code.
 */
int
lzc_destroy_bookmarks_ext(nvlist_t *bmarks, nvlist_t *opts, nvlist_t **errlist)
{
	const char *message;
	nvpair_t *elem;
	int error;
	char pool[ZFS_MAX_DATASET_NAME_LEN];

	/* determine the pool name */
	elem = nvlist_next_nvpair(bmarks, NULL);
	if (elem == NULL)
		return (0);
	(void) strlcpy(pool, nvpair_name(elem), sizeof (pool));
	pool[strcspn(pool, "/#")] = '\0';

	error = lzc_ioctl_simple(ZFS_IOC_DESTROY_BOOKMARKS, pool, bmarks,
	    errlist);

	if (error == 0 &&
	    nvlist_lookup_string(opts, "log_history", (char **)&message) == 0) {
		int err = lzc_pool_log_history(pool, message);
		if (errlist)
			nvlist_add_int32(*errlist, "log_history", err);
	}

	return (error);
}

int
lzc_destroy_bookmarks(nvlist_t *bmarks, nvlist_t **errlist)
{
	return (lzc_destroy_bookmarks_ext(bmarks, NULL, errlist));
}

/*
 * Resets a property on a  DSL directory (i.e. filesystems, volumes, snapshots)
 * to its original value.
 *
 * The following are the valid properties in opts, all of which are booleans:
 *
 * "received" - resets property value to from `zfs recv` if it set a value
 */
int
lzc_inherit(const char *fsname, const char *propname, nvlist_t *opts)
{
	const char *message;
	uint64_t received;
	int error;
	char name[ZFS_MAX_DATASET_NAME_LEN];

	strlcpy(name, fsname, sizeof (name));

	if (fsname == NULL || (propname == NULL ||
	    strlen(fsname) == 0 || strlen(propname) == 0))
		return (EINVAL);

	received = nvlist_exists(opts, "received");

	error = lzc_ioctl_impl(ZFS_IOC_INHERIT_PROP, name, propname, NULL,
	    NULL, &received, NULL, 0, NULL, NULL, NULL, NULL);

	if (error == 0 &&
	    nvlist_lookup_string(opts, "log_history", (char **)&message) == 0) {
		(void) lzc_pool_log_history(fsname, message);
	}

	return (error);
}

/*
 * Destroys a DSL directory that is either a filesystems or a volume.
 * Destroying snapshots and bookmarks is not currently supported. Call
 * lzc_destroy_snaps and lzc_destroy_bookmarks for those respectively.
 *
 * The only currently valid property is the boolean "defer". It makes
 * destruction asynchronous such that the only error code back is if we try to
 * destroy something that does not exist. The caller must unmount the dataset
 * before calling this. Otherwise, it will fail.
 */

int
lzc_destroy_one(const char *fsname, nvlist_t *opts)
{
	const char *message;
	nvlist_t *args;
	int error;
	char name[ZFS_MAX_DATASET_NAME_LEN];

	if (fsname == NULL ||
	    strlen(fsname) == 0)
		return (EINVAL);

	strlcpy(name, fsname, sizeof (name));

	args = fnvlist_alloc();
	error = lzc_ioctl_impl(ZFS_IOC_DESTROY, name, NULL, NULL, NULL, NULL,
	    NULL, nvlist_exists(opts, "defer"), NULL, args, NULL, NULL);
	fnvlist_free(args);

	if (error == 0 &&
	    nvlist_lookup_string(opts, "log_history", (char **)&message) == 0) {
		(void) lzc_pool_log_history(fsname, message);
	}

	return (error);
}

/*
 * Rename DSL directory (i.e. filesystems, volumes, snapshots)
 *
 * The opts flag accepts a boolean named "recursive" to signal that the
 * mountpoint property on children should be updated.
 *
 * The following are the valid properties in opts, all of which are booleans:
 *
 * "recursive" - Rename mountpoints on child DSL directories
 *
 * If a recursive rename is done, an error occurs and errname is initialized, a
 * string will be allocated with strdup() and returned via it. The caller must
 * free it with strfree().
 */
int
lzc_rename(const char *oldname, const char *newname, nvlist_t *opts,
    char **errname)
{
	const char *message;
	nvlist_t *args, *errlist;
	uint64_t recursive;
	int error;
	char name[ZFS_MAX_DATASET_NAME_LEN];

	if (newname == NULL || (oldname == NULL ||
	    strlen(oldname) == 0 || strlen(newname) == 0))
		return (EINVAL);

	strlcpy(name, oldname, sizeof (name));

	recursive = nvlist_exists(opts, "recursive");

	args = fnvlist_alloc();
	errlist = (errname != NULL) ? fnvlist_alloc() : NULL;
	fnvlist_add_string(args, "newname", newname);
	error = lzc_ioctl_impl(ZFS_IOC_RENAME, name, newname, NULL,
	    NULL, &recursive, NULL, 0, NULL, NULL, NULL, &errlist);

	if (error && errname != NULL) {
		const char *name = fnvlist_lookup_string(errlist, "name");
		*errname = strdup(name);
	}

	fnvlist_free(args);
	if (errlist)
		fnvlist_free(errlist);

	if (error == 0 &&
	    nvlist_lookup_string(opts, "log_history", (char **)&message) == 0) {
		(void) lzc_pool_log_history(newname, message);
	}

	return (error);
}

#define	DLS_TRAVERSE_ALL	(DLS_TRAVERSE_FILESYSTEM | \
				DLS_TRAVERSE_SNAPSHOT | \
				DLS_TRAVERSE_VOLUME | \
				DLS_TRAVERSE_BOOKMARK)

typedef enum dls_flag {
	DLS_RECURSE		= 1 << 0,
	DLS_TRAVERSE_FILESYSTEM	= 1 << 1,
	DLS_TRAVERSE_SNAPSHOT	= 1 << 2,
	DLS_TRAVERSE_VOLUME	= 1 << 3,
	DLS_TRAVERSE_BOOKMARK	= 1 << 4,
	DLS_IGNORE_LISTSNAPS	= 1 << 5,
} dls_flag_t;


typedef struct lzc_list_ctx {
	int lctx_fd;
	const char *lctx_name;
	nvlist_t *lctx_opts;
	lzc_iter_f lctx_func;
	void *lctx_data;
} lzc_list_ctx_t;


/*
 * If props is a handle to a non-empty nvlist, we assume objset_stats points to
 * corresponding stats
 */
static int
lzc_objset_propstat(const char *name, dmu_objset_stats_t *objset_stats,
    nvlist_t **props) {
	dmu_objset_stats_t stats;
	nvlist_t *nvl;
	int err;

	if ((props == NULL || nvlist_empty(*props)) || objset_stats == NULL) {
		char fsname[ZFS_MAX_DATASET_NAME_LEN];
		strlcpy(fsname, name, sizeof (fsname));
		if ((err = lzc_ioctl_impl(ZFS_IOC_OBJSET_STATS, fsname, NULL,
		    NULL, NULL, NULL, NULL, 0, &stats, NULL, NULL, props))
		    != 0) {
			return (err);
		}
	} else {
		stats = *objset_stats;
	}

	/* Add version, case, normalization, etcetera on filesystems */
	if (props != NULL && stats.dds_type == DMU_OST_ZFS) {
		nvpair_t *pair = NULL;
		if ((err = lzc_ioctl_simple(ZFS_IOC_OBJSET_ZPLPROPS, name, NULL,
		    &nvl)) != 0) {
			fnvlist_free(*props);
			*props = NULL;
			return (err);
		}
		while ((pair = nvlist_next_nvpair(nvl, pair)) != NULL) {
			nvlist_t *propval;
			uint64_t intval;
			const char *propname = nvpair_name(pair);
			zfs_prop_t prop = zfs_name_to_prop(propname);

			switch (prop) {
			case ZFS_PROP_VERSION:
			case ZFS_PROP_NORMALIZE:
			case ZFS_PROP_UTF8ONLY:
			case ZFS_PROP_CASE:
				intval = fnvpair_value_uint64(pair);
				propval = fnvlist_alloc();

				fnvlist_add_uint64(propval, ZPROP_VALUE,
				    intval);
				if (zfs_prop_default_numeric(prop) == intval)
					fnvlist_add_string(propval,
					    ZPROP_SOURCE, "");
				fnvlist_add_nvlist(*props, propname, propval);

				fnvlist_free(propval);
			default:
				break;
			}
		}

		fnvlist_free(nvl);
	}

	if (objset_stats)
		*objset_stats = stats;

	return (0);
}

static int
lzc_list_invoke_cb(const char *fsname, nvlist_t *props,
    const dmu_objset_stats_t *stats, lzc_iter_f cb, void *data)
{
	nvlist_t *outnvl = fnvlist_alloc();
	nvlist_t *nvl;
	int err;

	fnvlist_add_string(outnvl, "name", fsname);
	props = zprop_conv_zfs_to_strings(props);
	fnvlist_add_nvlist(outnvl, "properties", props);
	fnvlist_free(props);

	nvl = dmu_objset_stats_nvlist(stats);
	fnvlist_add_nvlist(outnvl, "dmu_objset_stats", nvl);

	err = (*cb)(outnvl, data);

	fnvlist_free(nvl);
	fnvlist_free(outnvl);

	return (err);
}

static int
lzc_list_listprops_check(const char *name, int *flags)
{
	int err = 0;

	if ((*flags & DLS_TRAVERSE_SNAPSHOT) == 0 &&
	    (*flags & DLS_RECURSE) != 0 &&
	    (*flags & DLS_IGNORE_LISTSNAPS) == 0) {
		nvlist_t *props, *nvl;
		uint64_t listsnap;
		char pool[ZFS_MAX_DATASET_NAME_LEN];

		(void) strlcpy(pool, name, sizeof (pool));
		pool[strcspn(pool, "/@#")] = '\0';

		if ((err = lzc_pool_getprops(pool, NULL, &props)) != 0)
			return (err);

		if (nvlist_lookup_nvlist(props,
		    zpool_prop_to_name(ZPOOL_PROP_LISTSNAPS), &nvl) == 0 &&
		    nvlist_lookup_uint64(nvl, ZPROP_VALUE, &listsnap) == 0) {

			if (listsnap)
				*flags |= DLS_TRAVERSE_SNAPSHOT;
		}

		fnvlist_free(props);
	}

	return (err);
}

static int
lzc_list_find_children_impl(const char *name, lzc_iter_f cb,
    const dmu_objset_stats_t *top_stats, nvlist_t *top_props,
    void *data, uint64_t mindepth, uint64_t maxdepth, int flags)
{
	uint64_t cookie;
	int err = 0;
	nvlist_t *props = NULL;
	dmu_objset_stats_t objset_stats;
	char fsname[ZFS_MAX_DATASET_NAME_LEN];

	strlcpy(fsname, name, sizeof (fsname));

	if (maxdepth) {
		cookie = 0;
		while ((err = lzc_ioctl_impl(ZFS_IOC_DATASET_LIST_NEXT, fsname,
		    NULL, NULL, NULL, &cookie, NULL, 0, &objset_stats, NULL,
		    NULL, &props)) == 0) {
			uint64_t min = (mindepth != 0) ? mindepth - 1 : 0;
			uint64_t max =  (maxdepth != DS_FIND_MAX_DEPTH) ?
			    maxdepth - 1 : maxdepth;

			if (flags & DLS_RECURSE) {
				/* Add version, case, normalization, etcetera */
				if ((err = lzc_objset_propstat(fsname,
				    &objset_stats, &props)) != 0) {
					fnvlist_free(props);
					props = NULL;
					break;
				}

				err = lzc_list_find_children_impl(fsname, cb,
				    &objset_stats, props, data,
				    min, max, flags);
			}

			strlcpy(fsname, name, sizeof (fsname));

			fnvlist_free(props);
			props = NULL;

			if (err != 0)
				break;

		}

		if (err == ESRCH)
			err = 0;

	}

	if (err == 0 && mindepth < 2 && maxdepth > 0 &&
	    flags & DLS_TRAVERSE_SNAPSHOT) {
		nvlist_t *props = NULL;
		cookie = 0;
		strlcpy(fsname, name, sizeof (fsname));
		while ((err = lzc_ioctl_impl(ZFS_IOC_SNAPSHOT_LIST_NEXT, fsname,
		    NULL, NULL, NULL, &cookie, NULL, 0, &objset_stats, NULL,
		    NULL, &props)) == 0) {
			err = lzc_list_invoke_cb(fsname, props, &objset_stats,
			    cb, data);

			fnvlist_free(props);
			props = NULL;

			if (err != 0)
				break;

			strlcpy(fsname, name, sizeof (fsname));
		}

		if (err == ESRCH)
			err = 0;
	}

	if (err == 0 && mindepth < 2 && maxdepth > 0 &&
	    flags & DLS_TRAVERSE_BOOKMARK) {
		nvpair_t *pair;
		nvlist_t *bmarks;
		nvlist_t *props = fnvlist_alloc();

		fnvlist_add_boolean(props, zfs_prop_to_name(ZFS_PROP_GUID));
		fnvlist_add_boolean(props,
		    zfs_prop_to_name(ZFS_PROP_CREATETXG));
		fnvlist_add_boolean(props,
		    zfs_prop_to_name(ZFS_PROP_CREATION));

		err = lzc_get_bookmarks(name, props, &bmarks);

		fnvlist_free(props);

		if (err != 0)
			return (err);

		for (pair = nvlist_next_nvpair(bmarks, NULL);
		    pair != NULL; pair = nvlist_next_nvpair(bmarks, pair)) {
			char *bmark_name;
			nvlist_t *bmark_props;

			bmark_name = nvpair_name(pair);
			bmark_props = fnvpair_value_nvlist(pair);

			(void) snprintf(fsname, sizeof (fsname), "%s#%s", name,
			    bmark_name);

			err = lzc_list_invoke_cb(fsname, bmark_props, top_stats,
			    cb, data);

			if (err != 0)
				break;
		}

		fnvlist_free(bmarks);

	}

	if (err == 0 && mindepth == 0 &&
	    (((top_stats->dds_type == DMU_OST_ZFS) &&
	    (flags & DLS_TRAVERSE_FILESYSTEM) != 0) ||
	    ((top_stats->dds_type == DMU_OST_ZVOL) && (flags &
	    DLS_TRAVERSE_VOLUME) != 0))) {
		err = lzc_list_invoke_cb(name, top_props, top_stats,
		    cb, data);
	}

	return (err);
}

static int
lzc_list_find_children(const char *name, lzc_iter_f cb,
    void *data, uint64_t mindepth, uint64_t maxdepth, int flags)
{
	nvlist_t *props = NULL;
	dmu_objset_stats_t stats;
	int err;

	if ((err = lzc_objset_propstat(name, &stats, &props)) != 0)
		return (err);

	if ((err = lzc_list_listprops_check(name, &flags)) != 0)
		return (err);

	err = lzc_list_find_children_impl(name, cb, &stats, props,
	    data, mindepth, maxdepth, flags);

	fnvlist_free(props);

	return (err);

}


static void *
lzc_list_worker(void *args)
{
	lzc_list_ctx_t *lctxp = args;
	int fd = lctxp->lctx_fd;
	const char *name = lctxp->lctx_name;
	nvlist_t *opts = lctxp->lctx_opts;
	nvlist_t *type = NULL;
	uint64_t mindepth = 0;
	uint64_t maxdepth = DS_FIND_MAX_DEPTH;
	int flags = 0;
	zfs_pipe_record_t zpr;

	if (nvlist_lookup_nvlist(opts, "type", &type) == 0 &&
	    !nvlist_empty(type)) {
		flags |= DLS_IGNORE_LISTSNAPS;
		if (nvlist_exists(type, "all"))
			flags |= DLS_TRAVERSE_ALL;
		else {
			if (nvlist_exists(type, "bookmark"))
				flags |= DLS_TRAVERSE_BOOKMARK;
			if (nvlist_exists(type, "filesystem"))
				flags |= DLS_TRAVERSE_FILESYSTEM;
			if (nvlist_exists(type, "snap"))
				flags |= DLS_TRAVERSE_SNAPSHOT;
			if (nvlist_exists(type, "snapshot"))
				flags |= DLS_TRAVERSE_SNAPSHOT;
			if (nvlist_exists(type, "volume"))
				flags |= DLS_TRAVERSE_VOLUME;
		}
	} else if (name == NULL) {
		flags |= DLS_TRAVERSE_FILESYSTEM;
		flags |= DLS_TRAVERSE_VOLUME;
	}

	if (nvlist_exists(opts, "recurse")) {
		flags |= DLS_RECURSE;
		(void) nvlist_lookup_uint64(opts, "recurse", &maxdepth);
	} else if (name) {
		maxdepth = 0;
	}

	if (nvlist_exists(opts, "maxrecurse") ||
	    nvlist_exists(opts, "minrecurse")) {
		flags |= DLS_RECURSE;
		(void) nvlist_lookup_uint64(opts, "minrecurse", &mindepth);
		(void) nvlist_lookup_uint64(opts, "maxrecurse", &maxdepth);
		if (mindepth > maxdepth) {
			zpr.zpr_err = SET_ERROR(EINVAL);
			goto out;
		}
	}

	if (name) {
		nvlist_t *props = NULL;
		dmu_objset_stats_t stats;

		if ((zpr.zpr_err = lzc_objset_propstat(name, &stats, &props))
		    != 0) {
			goto out;
		}

		if ((strchr(name, '#') != NULL)) {
			flags |= DLS_TRAVERSE_BOOKMARK;
			flags &= ~DLS_RECURSE;
		} else if ((strchr(name, '@') != NULL)) {
			flags |= DLS_TRAVERSE_SNAPSHOT;
			flags &= ~DLS_RECURSE;
		} else if (nvlist_empty(type)) {
			/* Adopt sane defaults based on the DSL directory */
			switch (stats.dds_type) {
			case DMU_OST_ZVOL:
				flags |= DLS_TRAVERSE_VOLUME;
				break;
			case DMU_OST_ZFS:
				flags |= DLS_TRAVERSE_FILESYSTEM;
				break;
			default:
				VERIFY(0);
			}
		}

		if ((zpr.zpr_err = lzc_list_listprops_check(name, &flags)) == 0)
			zpr.zpr_err = (uint8_t)lzc_list_find_children_impl(name,
			    lctxp->lctx_func, &stats, props, lctxp->lctx_data,
			    mindepth, maxdepth, flags);

		fnvlist_free(props);
	} else {
		nvlist_t *config;
		nvpair_t *elem = NULL;

		zpr.zpr_err = (uint8_t) lzc_pool_configs(NULL, &config);

		if (zpr.zpr_err != 0)
			goto out;

		while ((elem = nvlist_next_nvpair(config, elem)) != NULL) {
			name = nvpair_name(elem);

			zpr.zpr_err = (uint8_t)lzc_list_find_children(name,
			    lctxp->lctx_func, lctxp->lctx_data, mindepth,
			    maxdepth, flags);
		}
		fnvlist_free(config);
	}

out:
	if (fd != -1) {
		ssize_t r;
		free(lctxp);
		bzero(&zpr, sizeof (zfs_pipe_record_t));
		r = write(fd, &zpr, sizeof (zpr));
		return (NULL);
	}

	return ((void *)(uintptr_t)zpr.zpr_err);

}

int
lzc_list_fd_output(nvlist_t *nvl, void *data)
{
	lzc_list_ctx_t *lctxp = data;
	int fd = lctxp->lctx_fd;
	size_t nvsize;
	char *packed;
	zfs_pipe_record_t *zpr;
	int err;

	ASSERT(sizeof (zfs_pipe_record_t) == sizeof (uint64_t));

	nvsize = fnvlist_size(nvl);
	if (nvsize > UINT32_MAX - 8)
		return (EOVERFLOW);

	/*
	 * Allocate memory ourselves so that we can include space for the
	 * header.
	 */
	zpr = malloc(nvsize + sizeof (zfs_pipe_record_t));

	/* Setup header */
	bzero(zpr, sizeof (zfs_pipe_record_t));
	zpr->zpr_data_size = (uint32_t) nvsize;
#ifdef  _LITTLE_ENDIAN
	zpr->zpr_endian = 1;
#endif

	packed = (char *)(zpr + 1);
	err = nvlist_pack(nvl, &packed, &nvsize, NV_ENCODE_XDR, 0);

	if (err == 0)
		err = write(fd, packed, nvsize + sizeof (zfs_pipe_record_t));

	free(zpr);

	return (err);

}

/*
 * List DSL directory/directories
 *
 * This is an asynchronous API call. The caller passes a file descriptor
 * through which output is received. The file descriptor should typically be
 * the send side of a pipe, but this is not required.
 *
 * Preliminary error checks are done prior to the start of output and if
 * successful, a return code of zero is provided. If unsuccessful, a non-zero
 * error code is passed.
 *
 * The opts field is an nvlist which supports the following properties:
 *
 * Name		Type		Description
 * "recurse"	boolean/uint64	List output for children.
 * "type"	nvlist		List only types specified.
 *
 * If the passed name is that of a bookmark or snapshot, the recurse field is
 * ignored. If all children are desired, recurse should be set to be a boolean
 * type. If a recursion limit is desired, recurses hould be a uint64_t. If no
 * type is specified, a default behavior consistent with the zfs list command
 * is provided. Valid children of the type nvlist are:
 *
 * Name		Type		Description
 * "all"	boolean		List output for all types
 * "bookmark"	boolean		List output for bookmarks
 * "filesystem"	boolean		List output for filesystems
 * "snap"	boolean		List output for snapshots
 * "snapshot"	boolean		List output for snapshots
 * "volume"	boolean		List output for volumes
 *
 * Whenever a boolean type is specified, any type may be passed and be
 * considered boolean. However, future extensions may accept alternate types
 * and consequently, backward compatibility is only guarenteed to callers
 * passing a boolean type that contains no value. A boolean that contains
 * B_TRUE or B_FALSE is considered a separate type from a boolean that contains
 * no value. Additionally, future enhancements to zfs may create a new type and
 * callers that only wish to handle existing types should specify them
 * explicitly rather than relying on the default behavior.
 *
 * The parent-child relationship is obeyed such all children of each
 * pool/directory are output alongside their parents. However, no guarantees
 * are made with regard to post-order/pre-order traversal or the order of
 * bookmarks/snapshots, such that the order is allowed to change. Userland
 * applications that are sensitive to a particular output order are expected to
 * sort.
 *
 * The output consists of a record header followed immediately by XDR-encoded
 * nvlist. The header format is as follows:
 *
 * Offset	Size		Description
 * 0 bytes	4 bytes		XDR-nvlist size (unsigned)
 * 4 bytes	1 byte		Header extension space (unsigned)
 * 5 bytes	1 byte		Return code (unsigned)
 * 6 bytes	1 byte		Endian bit (0 is BE, 1 is LE)
 * 7 bytes	1 byte		Reserved
 *
 * Errors obtaining information for any record will be contained in the return
 * code. The output for any record whose header return code contains an error
 * is a XDR encoded nvlist whose contents are undefined, unless the size
 * provided in the header is zero, in which case the output for that record is
 * empty. The receiver is expected to check the endian bit field before
 * processing the XDR-nvlist size and perform a byte-swap operation on the
 * value should the endian-ness differ.
 *
 * Non-zero values in the reserved field and upper bits of the endian field
 * imply a back-incompatible change. If the header extension field is non-zero
 * when neither the reserved field nor the upper bits of the endian field are
 * non-zero, the header should be assumed to have been extended in a
 * backward-compatible way and the XDR-nvlist of the specified size shall
 * follow the extended header. The lzc_list() library call will always request
 * API version 0 request as part of the ioctl to userland.  Consequently, the
 * kernel will return an API version 0 compatible-stream unless a change is
 * requested via a future extension to the opts nvlist.
 *
 * The nvlist will have the following members:
 *
 * Name			Type		Description
 * "name"		string		SPA/DSL name
 * "dmu_objset_stats"	nvlist		DMU Objset Stats
 * "properties"		nvlist		DSL properties
 *
 * Additional members may be added in future extensions.
 *
 * The "dmu_objset_stats" will have the following members:
 *
 * Name			Type		Description
 * "dds_num_clones"	uint64_t	Number of clones
 * "dds_creation_txg"	uint64_t	Creation transaction group
 * "dds_guid"		uint64_t	Globally unique identifier
 * "dds_type"		string		Type
 * "dds_is_snapshot"	boolean		Is a snapshot
 * "dds_inconsistent"	boolean		Is being received or destroyed
 * "dds_origin"		string		Name of parent (clone)
 *
 * Additional members may be added in future extensions.
 *
 * The "dds_" prefix stands for "DSL Dataset". "dds_type" is a string
 * representation of internal object types. Valid values at this time are:
 *
 * Name		Public	Description
 * "NONE"	No	Uninitialized value
 * "META"	No	Metadata
 * "ZPL"	Yes	Dataset
 * "ZVOL"	Yes	Volume
 * "OTHER"	No	Undefined
 * "ANY"	No	Open
 *
 * Only the public values will be returned for any output. The return of a
 * value not on this list implies a record for a new storage type. The output
 * should be consistent with existing types and the receiver can elect to
 * either handle it in a manner consistent with existing types or skip it.
 * Under no circumstance will an unlisted type be returned when types were
 * explicitly provided via the opts nvlist.
 *
 * On bookmarks, the "dmu_objset_stats" of the parent DSL Dataset shall be
 * returned. Consequently, "dds_is_snapshot" shall be false and identification
 * of bookmarks shall be done by checking for the '#' character in the "name"
 * member of the top level nvlist. This is done so that the type of the
 * bookmarked DSL dataset may be known.
 *
 * End of output shall be signified by NULL record header. Userland is expected
 * to close the file descriptor. Early termination can be signaled from
 * userland by closing the file descriptor.
 *
 * The design of the output is intended to enable userland to perform readahead
 * on the file descriptor. On certain platforms, libc may provide output
 * buffering. Userland libraries and applications electing to perform readahead
 * should take care not to block on a partially filled buffer when an end of
 * stream NULL record is returned.
 */

int
lzc_list(const char *name, nvlist_t *opts)
{
	lzc_list_ctx_t *lctxp;
	int fd;
	pthread_t threadid;

	if (nvlist_lookup_int32(opts, "fd", &fd) != 0)
		return (EINVAL);

	if (fd < 0)
		return (EINVAL);

	lctxp = malloc(sizeof (lzc_list_ctx_t));

	lctxp->lctx_fd = fd;
	lctxp->lctx_name = name;
	lctxp->lctx_opts = opts;
	lctxp->lctx_func = &lzc_list_fd_output;
	lctxp->lctx_data = lctxp;

	return (pthread_create(&threadid, NULL, &lzc_list_worker, lctxp));
}

/*
 * Helper function to iterate over all filesystems.
 * Excluding the "fd" option, the same options that are passed to lzc_list must
 * be passed to this.
 */
int
lzc_list_iter(const char *name, nvlist_t *opts, lzc_iter_f func, void *data)
{
	lzc_list_ctx_t lctx;

	lctx.lctx_fd = -1;
	lctx.lctx_name = name;
	lctx.lctx_opts = opts;
	lctx.lctx_func = func;
	lctx.lctx_data = data;

	return ((int)(uintptr_t)lzc_list_worker(&lctx));
}
