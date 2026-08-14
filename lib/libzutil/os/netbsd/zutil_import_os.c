// SPDX-License-Identifier: CDDL-1.0
#include <sys/types.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/vdev_impl.h>
#include <libzutil.h>
#include "zutil_import.h"

void
update_vdev_config_dev_strs(nvlist_t *nv)
{
	(void) nvlist_remove_all(nv, ZPOOL_CONFIG_DEVID);
	(void) nvlist_remove_all(nv, ZPOOL_CONFIG_PHYS_PATH);
}

/*
 * /dev nodes that are not attached disks must not be opened while
 * scanning for labels.  File vdevs live outside /dev.
 */

static const char *
dev_basename(const char *name)
{
	const char *slash = strrchr(name, '/');

	return (slash != NULL ? slash + 1 : name);
}

static boolean_t
hw_disknames_has(const char *unit)
{
	char *names = NULL, *p, *sp;
	size_t need, len = 0;
	int tries;

	/*
	 * sysctl(3): a too-small buffer copies a prefix and returns
	 * ENOMEM without updating *oldlenp.  Size with oldp == NULL,
	 * then retry if the value grew.
	 */
	for (tries = 0; tries < 3; tries++) {
		need = 0;
		if (sysctlbyname("hw.disknames", NULL, &need, NULL, 0) != 0 ||
		    need == 0) {
			free(names);
			return (B_FALSE);
		}
		if (names == NULL || need + 1 > len) {
			char *nbuf = realloc(names, need + 1);

			if (nbuf == NULL) {
				free(names);
				return (B_FALSE);
			}
			names = nbuf;
		}
		len = need;
		if (sysctlbyname("hw.disknames", names, &len, NULL, 0) == 0) {
			names[len] = '\0';
			p = names;
			while ((sp = strsep(&p, " \t")) != NULL) {
				if (sp[0] != '\0' && strcmp(sp, unit) == 0) {
					free(names);
					return (B_TRUE);
				}
			}
			free(names);
			return (B_FALSE);
		}
		if (errno != ENOMEM) {
			free(names);
			return (B_FALSE);
		}
		len = need + 1;
	}
	free(names);
	return (B_FALSE);
}

/*
 * MAKEDEV creates rraid0..7, rvnd0.., rccd0.. even when the
 * driver has no unit.  Opening those hangs zdb/zpool import in
 * lio_listio.  hw.disknames is the attached set (ld2, dk0, vnd0).
 * Map rwd1a / rraid0 / rvnd0 onto wd1 / raid0 / vnd0.
 */
static boolean_t
disk_unit_configured(const char *base)
{
	const char *p = base;
	char unit[32];

	if (p[0] == 'r' && zfs_dev_is_disk_class(p + 1))
		p++;
	if (!zfs_dev_is_disk_class(p))
		return (B_TRUE);
	if (!zfs_dev_disk_unit(base, unit, sizeof (unit)))
		return (B_FALSE);
	return (hw_disknames_has(unit));
}

static boolean_t
excluded_dev(const char *name)
{
	const char *base;

	/*
	 * These names are /dev nodes (the zfs control device, ttys,
	 * kmem, ...).  File vdevs live elsewhere -- zfs-pool-v1.dat
	 * must not match the "zfs" prefix.  FreeBSD uses the same
	 * /dev/ gate.
	 */
	if (strncmp(name, _PATH_DEV, sizeof (_PATH_DEV) - 1) != 0)
		return (B_FALSE);

	base = dev_basename(name);
	if (zfs_dev_is_media_wait(base))
		return (B_TRUE);
	/*
	 * Only attached disk-class nodes (ld, wd, sd, dk, vnd, ...).
	 * Opening pad/lua/autofs autoloads kmods; lua.kmod collides
	 * with OpenZFS channel-program Lua.
	 */
	{
		const char *p = base;

		if (p[0] == 'r' && zfs_dev_is_disk_class(p + 1))
			p++;
		if (!zfs_dev_is_disk_class(p))
			return (B_TRUE);
	}
	if (!disk_unit_configured(base))
		return (B_TRUE);
	return (B_FALSE);
}

static boolean_t
dev_stat_probe_ok(const struct stat64 *statbuf)
{
	if (S_ISREG(statbuf->st_mode))
		return (statbuf->st_size >= SPA_MINDEVSIZE);
	return (S_ISCHR(statbuf->st_mode) || S_ISBLK(statbuf->st_mode));
}

boolean_t
zpool_dev_probe_ok(const char *path)
{
	struct stat64 statbuf;
	if (excluded_dev(path))
		return (B_FALSE);
	if (stat64(path, &statbuf) != 0)
		return (B_FALSE);
	return (dev_stat_probe_ok(&statbuf));
}

boolean_t
zpool_dev_probe_ok_fd(int fd)
{
	struct stat64 statbuf;
	if (fstat64(fd, &statbuf) != 0)
		return (B_FALSE);
	return (dev_stat_probe_ok(&statbuf));
}

/*
 * The directory walk only opens the block whole-unit node.  If the
 * label was written with the raw twin (/dev/rld3), keep that path:
 * same as Linux rn_labelpaths / IMPORT_ORDER_PREFERRED_1.  Do not
 * open every raw partition.
 */
static void
zpool_prefer_label_raw(rdsk_node_t *rn, nvlist_t *config)
{
	const char *lpath = NULL;
	const char *devid = NULL;
	char raw[MAXPATHLEN];
	rdsk_node_t *slice;
	avl_index_t where;
	uint64_t vdev_guid = 0;
	libpc_handle_t *hdl;

	if (rn->rn_labelpaths == B_FALSE || rn->rn_hdl == NULL ||
	    rn->rn_avl == NULL || rn->rn_lock == NULL)
		return;
	/*
	 * The path is on the leaf inside vdev_tree, not the label
	 * root.  label_paths() matches this label's vdev guid.
	 * The pointers stay valid while config does.
	 */
	if (label_paths(rn->rn_hdl, config, &lpath, &devid) != 0 ||
	    lpath == NULL)
		return;
	(void) devid;
	if (zfs_dev_to_raw_path(rn->rn_name, raw, sizeof (raw)) != 0)
		return;
	if (strcmp(lpath, raw) != 0 || strcmp(lpath, rn->rn_name) == 0)
		return;
	(void) nvlist_lookup_uint64(config, ZPOOL_CONFIG_GUID, &vdev_guid);
	hdl = rn->rn_hdl;
	slice = zutil_alloc(hdl, sizeof (rdsk_node_t));
	slice->rn_name = zutil_strdup(hdl, lpath);
	slice->rn_vdev_guid = vdev_guid;
	slice->rn_lock = rn->rn_lock;
	slice->rn_avl = rn->rn_avl;
	slice->rn_hdl = hdl;
	slice->rn_order = IMPORT_ORDER_PREFERRED_1;
	slice->rn_labelpaths = B_FALSE;
	pthread_mutex_lock(rn->rn_lock);
	if (avl_find(rn->rn_avl, slice, &where)) {
		pthread_mutex_unlock(rn->rn_lock);
		free(slice->rn_name);
		free(slice);
		return;
	}
	avl_insert(rn->rn_avl, slice, where);
	pthread_mutex_unlock(rn->rn_lock);
	zpool_open_func(slice);
}

void
zpool_open_func(void *arg)
{
	rdsk_node_t *rn = arg;
	struct stat64 statbuf;
	nvlist_t *config;
	int num_labels;
	int fd;

	if (excluded_dev(rn->rn_name))
		return;

	/*
	 * Directory scan of /dev itself: whole-unit block only
	 * (ld3, not rld3a / pad0).  Nested dirs (/dev/zvol) are
	 * unchanged.  A preferred label path is the raw twin and
	 * is not a scan leaf; it is inserted below, not by readdir.
	 */
	if (rn->rn_order != IMPORT_ORDER_PREFERRED_1 &&
	    strncmp(rn->rn_name, "/dev/", 5) == 0 &&
	    strchr(rn->rn_name + 5, '/') == NULL &&
	    !zfs_dev_is_import_scan_leaf(rn->rn_name + 5))
		return;

	/*
	 * Probe/zdb uses the path as listed (block whole-unit:
	 * /dev/ld3).  Do not rewrite to the raw twin here — that
	 * was duplicating rld3a + rld3d + rld3 for every disk and
	 * opening rcd0.  Kernel vdev_disk_open() still prefers
	 * the character node so strategy I/O skips specfs.
	 *
	 * O_NONBLOCK: a leftover cd/fd node must not sleep for
	 * media even if the allowlist regresses.
	 */
	fd = open(rn->rn_name, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0)
		return;
	if (fstat64(fd, &statbuf) != 0)
		goto out;
	if (S_ISREG(statbuf.st_mode)) {
		if (statbuf.st_size < SPA_MINDEVSIZE)
			goto out;
	} else if (!(S_ISCHR(statbuf.st_mode) || S_ISBLK(statbuf.st_mode))) {
		goto out;
	} else if (statbuf.st_size > 0 && statbuf.st_size < SPA_MINDEVSIZE) {
		goto out;
	}
	if (zpool_read_label(fd, &config, &num_labels) != 0)
		goto out;
	if (num_labels == 0) {
		nvlist_free(config);
		goto out;
	}
	if (rn->rn_vdev_guid != 0) {
		uint64_t vdev_guid = 0;

		if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_GUID,
		    &vdev_guid) != 0 || vdev_guid != rn->rn_vdev_guid) {
			nvlist_free(config);
			goto out;
		}
	}
	rn->rn_config = config;
	rn->rn_num_labels = num_labels;
	zpool_prefer_label_raw(rn, config);
out:
	(void) close(fd);
}

static const char * const
zpool_default_import_path[] = {
	"/dev"
};

const char * const *
zpool_default_search_paths(size_t *count)
{
	*count = nitems(zpool_default_import_path);
	return (zpool_default_import_path);
}

int
zpool_find_import_blkid(libpc_handle_t *hdl, pthread_mutex_t *lock,
    avl_tree_t **slice_cache)
{
	DIR *dirp;
	struct dirent *dp;
	rdsk_node_t *slice;
	avl_index_t where;
	char path[MAXPATHLEN];

	*slice_cache = zutil_alloc(hdl, sizeof (avl_tree_t));
	avl_create(*slice_cache, slice_cache_compare, sizeof (rdsk_node_t),
	    offsetof(rdsk_node_t, rn_node));

	dirp = opendir("/dev");
	if (dirp == NULL)
		return (errno);

	while ((dp = readdir(dirp)) != NULL) {
		const char *n = dp->d_name;
		/*
		 * Block whole-unit nodes only: ld3, dk0, wd0d, vnd0.
		 * Not rld3 / rld3a (kernel uses those for I/O) and
		 * not cd/fd/st (open waits for media).
		 */
		if (!zfs_dev_is_import_scan_leaf(n))
			continue;
		(void) snprintf(path, sizeof (path), "/dev/%s", n);
		if (excluded_dev(path))
			continue;
		slice = zutil_alloc(hdl, sizeof (rdsk_node_t));
		slice->rn_name = zutil_strdup(hdl, path);
		slice->rn_vdev_guid = 0;
		slice->rn_lock = lock;
		slice->rn_avl = *slice_cache;
		slice->rn_hdl = hdl;
		slice->rn_labelpaths = B_TRUE;
		slice->rn_order = IMPORT_ORDER_DEFAULT;
		pthread_mutex_lock(lock);
		if (avl_find(*slice_cache, slice, &where)) {
			free(slice->rn_name);
			free(slice);
		} else {
			avl_insert(*slice_cache, slice, where);
		}
		pthread_mutex_unlock(lock);
	}
	(void) closedir(dirp);
	return (0);
}

int
zfs_dev_flush(int fd)
{
	(void) fd;
	return (0);
}

void
update_vdev_config_dev_sysfs_path(nvlist_t *nv, const char *path,
    const char *key)
{
	(void) nv;
	(void) path;
	(void) key;
}

void
update_vdevs_config_dev_sysfs_path(nvlist_t *config)
{
	(void) config;
}

int
zpool_disk_wait(const char *path)
{
	(void) path;
	return (ENOTSUP);
}
