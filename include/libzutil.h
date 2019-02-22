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
 * Copyright (c) 2018 by Delphix. All rights reserved.
 */

#ifndef	_LIBZUTIL_H
#define	_LIBZUTIL_H

#include <sys/nvpair.h>
#include <sys/fs/zfs.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Default wait time for a device name to be created.
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
extern const pool_config_ops_t libzfs_config_ops;
extern const pool_config_ops_t libzpool_config_ops;

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

extern nvlist_t *zpool_search_import(void *, importargs_t *,
    const pool_config_ops_t *);
extern int zpool_find_config(void *, const char *, nvlist_t **, importargs_t *,
    const pool_config_ops_t *);

extern const char * const * zpool_default_search_paths(size_t *count);
extern int zpool_read_label(int, nvlist_t **, int *);
extern int zpool_label_disk_wait(const char *, int);

#ifdef HAVE_LIBUDEV
struct udev_device;

extern int zfs_device_get_devid(struct udev_device *, char *, size_t);
extern int zfs_device_get_physical(struct udev_device *, char *, size_t);
#else
#define	zfs_device_get_devid(dev, bufptr, buflen)	(ENODATA)
#define	zfs_device_get_physical(dev, bufptr, buflen)	(ENODATA)
#endif

extern void update_vdev_config_dev_strs(nvlist_t *);

/*
 * Default device paths
 */
#define	DISK_ROOT	"/dev"
#define	UDISK_ROOT	"/dev/disk"
#define	ZVOL_ROOT	"/dev/zvol"

extern int zfs_append_partition(char *path, size_t max_len);
extern int zfs_resolve_shortname(const char *name, char *path, size_t pathlen);

extern char *zfs_strip_partition(char *);
extern char *zfs_strip_partition_path(char *);

extern int zfs_strcmp_pathname(const char *, const char *, int);

extern int zfs_dev_is_dm(const char *);
extern int zfs_dev_is_whole_disk(const char *);
extern char *zfs_get_underlying_path(const char *);
extern char *zfs_get_enclosure_sysfs_path(const char *);

#ifdef HAVE_LIBUDEV
extern boolean_t is_mpath_whole_disk(const char *);
#else
#define	is_mpath_whole_disk(path) (B_FALSE)
#endif

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
extern void zfs_nicebytes(uint64_t, char *, size_t);
extern void zfs_nicenum(uint64_t, char *, size_t);
extern void zfs_nicenum_format(uint64_t, char *, size_t,
    enum zfs_nicenum_format);
extern void zfs_nicetime(uint64_t, char *, size_t);

#define	nicenum(num, buf, size)	zfs_nicenum(num, buf, size)

extern void zpool_dump_ddt(const ddt_stat_t *, const ddt_histogram_t *);
extern int zpool_history_unpack(char *, uint64_t, uint64_t *, nvlist_t ***,
    uint_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* _LIBZUTIL_H */
