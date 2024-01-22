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
 * Copyright (c) 2018 by Delphix. All rights reserved.
 */

#ifndef	_LIBZUTIL_H
#define	_LIBZUTIL_H extern __attribute__((visibility("default")))

#include <string.h>
#include <locale.h>
#include <sys/nvpair.h>
#include <sys/fs/zfs.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Default wait time in milliseconds for a device name to be created.
 */
#define	DISK_LABEL_WAIT		(30 * 1000)  /* 30 seconds */


/*
 * Pool Config Operations
 *
 * These are specific to the library libzfs or libzpool instance.
 */
typedef nvlist_t *refresh_config_func_t(void *, nvlist_t *);

typedef int pool_active_func_t(void *, const char *, uint64_t, boolean_t *);

typedef const struct pool_config_ops {
	refresh_config_func_t	*pco_refresh_config;
	pool_active_func_t	*pco_pool_active;
} pool_config_ops_t;

/*
 * An instance of pool_config_ops_t is expected in the caller's binary.
 */
_LIBZUTIL_H pool_config_ops_t libzfs_config_ops;
_LIBZUTIL_H pool_config_ops_t libzpool_config_ops;

typedef enum lpc_error {
	LPC_SUCCESS = 0,	/* no error -- success */
	LPC_BADCACHE = 2000,	/* out of memory */
	LPC_BADPATH,	/* must be an absolute path */
	LPC_NOMEM,	/* out of memory */
	LPC_EACCESS,	/* some devices require root privileges */
	LPC_UNKNOWN
} lpc_error_t;

typedef struct importargs {
	char **path;		/* a list of paths to search		*/
	int paths;		/* number of paths to search		*/
	const char *poolname;	/* name of a pool to find		*/
	uint64_t guid;		/* guid of a pool to find		*/
	const char *cachefile;	/* cachefile to use for import		*/
	boolean_t can_be_active; /* can the pool be active?		*/
	boolean_t scan;		/* prefer scanning to libblkid cache    */
	nvlist_t *policy;	/* load policy (max txg, rewind, etc.)	*/
} importargs_t;

typedef struct libpc_handle {
	int lpc_error;
	boolean_t lpc_printerr;
	boolean_t lpc_open_access_error;
	boolean_t lpc_desc_active;
	char lpc_desc[1024];
	pool_config_ops_t *lpc_ops;
	void *lpc_lib_handle;
} libpc_handle_t;

_LIBZUTIL_H const char *libpc_error_description(libpc_handle_t *);
_LIBZUTIL_H nvlist_t *zpool_search_import(libpc_handle_t *, importargs_t *);
_LIBZUTIL_H int zpool_find_config(libpc_handle_t *, const char *, nvlist_t **,
    importargs_t *);

_LIBZUTIL_H const char * const * zpool_default_search_paths(size_t *count);
_LIBZUTIL_H int zpool_read_label(int, nvlist_t **, int *);
_LIBZUTIL_H int zpool_label_disk_wait(const char *, int);
_LIBZUTIL_H int zpool_disk_wait(const char *);

struct udev_device;

_LIBZUTIL_H int zfs_device_get_devid(struct udev_device *, char *, size_t);
_LIBZUTIL_H int zfs_device_get_physical(struct udev_device *, char *, size_t);

_LIBZUTIL_H void update_vdev_config_dev_strs(nvlist_t *);

/*
 * Default device paths
 */
#define	DISK_ROOT	"/dev"
#define	UDISK_ROOT	"/dev/disk"
#define	ZVOL_ROOT	"/dev/zvol"

_LIBZUTIL_H int zfs_append_partition(char *path, size_t max_len);
_LIBZUTIL_H int zfs_resolve_shortname(const char *name, char *path,
    size_t pathlen);

_LIBZUTIL_H char *zfs_strip_partition(const char *);
_LIBZUTIL_H const char *zfs_strip_path(const char *);

_LIBZUTIL_H int zfs_strcmp_pathname(const char *, const char *, int);

_LIBZUTIL_H boolean_t zfs_dev_is_dm(const char *);
_LIBZUTIL_H boolean_t zfs_dev_is_whole_disk(const char *);
_LIBZUTIL_H int zfs_dev_flush(int);
_LIBZUTIL_H char *zfs_get_underlying_path(const char *);
_LIBZUTIL_H char *zfs_get_enclosure_sysfs_path(const char *);

_LIBZUTIL_H boolean_t is_mpath_whole_disk(const char *);

_LIBZUTIL_H boolean_t zfs_isnumber(const char *);

/*
 * Formats for iostat numbers.  Examples: "12K", "30ms", "4B", "2321234", "-".
 *
 * ZFS_NICENUM_1024:	Print kilo, mega, tera, peta, exa..
 * ZFS_NICENUM_BYTES:	Print single bytes ("13B"), kilo, mega, tera...
 * ZFS_NICENUM_TIME:	Print nanosecs, microsecs, millisecs, seconds...
 * ZFS_NICENUM_RAW:	Print the raw number without any formatting
 * ZFS_NICENUM_RAWTIME:	Same as RAW, but print dashes ('-') for zero.
 */
enum zfs_nicenum_format {
	ZFS_NICENUM_1024 = 0,
	ZFS_NICENUM_BYTES = 1,
	ZFS_NICENUM_TIME = 2,
	ZFS_NICENUM_RAW = 3,
	ZFS_NICENUM_RAWTIME = 4
};

/*
 * Convert a number to a human-readable form.
 */
_LIBZUTIL_H void zfs_nicebytes(uint64_t, char *, size_t);
_LIBZUTIL_H void zfs_nicenum(uint64_t, char *, size_t);
_LIBZUTIL_H void zfs_nicenum_format(uint64_t, char *, size_t,
    enum zfs_nicenum_format);
_LIBZUTIL_H void zfs_nicetime(uint64_t, char *, size_t);
_LIBZUTIL_H void zfs_niceraw(uint64_t, char *, size_t);

#define	nicenum(num, buf, size)	zfs_nicenum(num, buf, size)

_LIBZUTIL_H void zpool_dump_ddt(const ddt_stat_t *, const ddt_histogram_t *);
_LIBZUTIL_H int zpool_history_unpack(char *, uint64_t, uint64_t *, nvlist_t ***,
    uint_t *);
_LIBZUTIL_H void fsleep(float sec);
_LIBZUTIL_H int zpool_getenv_int(const char *env, int default_val);

struct zfs_cmd;

/*
 * List of colors to use
 */
#define	ANSI_BLACK	"\033[0;30m"
#define	ANSI_RED	"\033[0;31m"
#define	ANSI_GREEN	"\033[0;32m"
#define	ANSI_YELLOW	"\033[0;33m"
#define	ANSI_BLUE	"\033[0;34m"
#define	ANSI_BOLD_BLUE	"\033[1;34m" /* light blue */
#define	ANSI_MAGENTA	"\033[0;35m"
#define	ANSI_CYAN	"\033[0;36m"
#define	ANSI_GRAY	"\033[0;37m"

#define	ANSI_RESET	"\033[0m"
#define	ANSI_BOLD	"\033[1m"

_LIBZUTIL_H int use_color(void);
_LIBZUTIL_H void color_start(const char *color);
_LIBZUTIL_H void color_end(void);
_LIBZUTIL_H int printf_color(const char *color, const char *format, ...);

_LIBZUTIL_H const char *zfs_basename(const char *path);
_LIBZUTIL_H ssize_t zfs_dirnamelen(const char *path);
#ifdef __linux__
extern char **environ;
_LIBZUTIL_H void zfs_setproctitle_init(int argc, char *argv[], char *envp[]);
_LIBZUTIL_H void zfs_setproctitle(const char *fmt, ...);
#else
#define	zfs_setproctitle(fmt, ...)	setproctitle(fmt, ##__VA_ARGS__)
#define	zfs_setproctitle_init(x, y, z)	((void)0)
#endif

/*
 * These functions are used by the ZFS libraries and cmd/zpool code, but are
 * not exported in the ABI.
 */
typedef int (*pool_vdev_iter_f)(void *, nvlist_t *, void *);
int for_each_vdev_cb(void *zhp, nvlist_t *nv, pool_vdev_iter_f func,
    void *data);
int for_each_vdev_macro_helper_func(void *zhp_data, nvlist_t *nv, void *data);
int for_each_real_leaf_vdev_macro_helper_func(void *zhp_data, nvlist_t *nv,
    void *data);
/*
 * Often you'll want to iterate over all the vdevs in the pool, but don't want
 * to use for_each_vdev() since it requires a callback function.
 *
 * Instead you can use FOR_EACH_VDEV():
 *
 *     zpool_handle_t *zhp      // Assume this is initialized
 *     nvlist_t *nv
 *     ...
 *     FOR_EACH_VDEV(zhp, nv) {
 *	 const char *path = NULL;
 *	 nvlist_lookup_string(nv, ZPOOL_CONFIG_PATH, &path);
 *	 printf("Looking at vdev %s\n", path);
 *     }
 *
 * Note: FOR_EACH_VDEV runs in O(n^2) time where n = number of vdevs.  However,
 * there's an upper limit of 256 vdevs per dRAID top-level vdevs (TLDs), 255 for
 * raidz2 TLDs, a real world limit of ~500 vdevs for mirrors, so this shouldn't
 * really be an issue.
 *
 * Here are some micro-benchmarks of a complete FOR_EACH_VDEV loop on a RAID0
 * pool:
 *
 * 100  vdevs = 0.7ms
 * 500  vdevs = 17ms
 * 750  vdevs = 40ms
 * 1000 vdevs = 82ms
 *
 * The '__nv += 0' at the end of the for() loop gets around a "comma or
 * semicolon followed by non-blank" checkstyle error.  Note on most compliers
 * the '__nv += 0' can just be replaced with 'NULL', but gcc on Centos 7
 * will give a 'warning: statement with no effect' error if you do that.
 */
#define	__FOR_EACH_VDEV(__zhp, __nv, __func) { \
	__nv = zpool_get_config(__zhp, NULL); \
	VERIFY0(nvlist_lookup_nvlist(__nv, ZPOOL_CONFIG_VDEV_TREE, &__nv)); \
	} \
	for (nvlist_t *__root_nv = __nv, *__state = (nvlist_t *)0; \
	    for_each_vdev_cb(&__state, __root_nv, __func, &__nv) == 1; \
	    __nv += 0)

#define	FOR_EACH_VDEV(__zhp, __nv) \
	__FOR_EACH_VDEV(__zhp, __nv, for_each_vdev_macro_helper_func)

/*
 * "real leaf" vdevs are leaf vdevs that are real devices (disks or files).
 * This excludes leaf vdevs like like draid spares.
 */
#define	FOR_EACH_REAL_LEAF_VDEV(__zhp, __nv) \
	__FOR_EACH_VDEV(__zhp, __nv, for_each_real_leaf_vdev_macro_helper_func)

int for_each_vdev_in_nvlist(nvlist_t *nvroot, pool_vdev_iter_f func,
    void *data);
void update_vdevs_config_dev_sysfs_path(nvlist_t *config);
_LIBZUTIL_H void update_vdev_config_dev_sysfs_path(nvlist_t *nv,
    const char *path, const char *key);

/*
 * Thread-safe strerror() for use in ZFS libraries
 */
static inline char *zfs_strerror(int errnum) {
	return (strerror_l(errnum, uselocale(0)));
}

#ifdef	__cplusplus
}
#endif

#endif	/* _LIBZUTIL_H */
