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
 * Copyright (c) 2016, Brendon Humphrey (brendon.humphrey@mac.com).
 */

#ifndef _LIBDISKMGT_H
#define	_LIBDISKMGT_H

#include <libnvpair.h>

#ifdef __cplusplus
extern "C" {
#endif

/* attribute definitions */

#define	DM_USED_BY		"used_by"
#define	DM_USED_NAME		"used_name"
#define	DM_USE_MOUNT		"mount"
#define	DM_USE_FS		"fs"
#define	DM_USE_FS_NO_FORCE	"fs_nf"
#define	DM_USE_EXPORTED_ZPOOL	"exported_zpool"
#define	DM_USE_ACTIVE_ZPOOL	"active_zpool"
#define	DM_USE_SPARE_ZPOOL	"spare_zpool"
#define	DM_USE_L2CACHE_ZPOOL	"l2cache_zpool"
#define	DM_USE_CORESTORAGE_PV   "corestorage_pv"
#define	DM_USE_CORESTORAGE_LOCKED_LV    "corestorage_locked_lv"
#define	DM_USE_CORESTORAGE_CONVERTING_LV "corestorage_converting_lv"
#define	DM_USE_CORESTORAGE_OFFLINE_LV    "corestorage_offline_lv"
#define	DM_USE_OS_PARTITION    "reserved_os_partititon"
#define	DM_USE_OS_PARTITION_NO_FORCE   "reserved_os_partititon_nf"

#define	NOINUSE_SET	getenv("NOINUSE_CHECK") != NULL

typedef enum {
    DM_WHO_ZPOOL = 0,
    DM_WHO_ZPOOL_FORCE,
    DM_WHO_ZPOOL_SPARE
} dm_who_type_t;

/* slice stat name */
typedef enum {
    DM_SLICE_STAT_USE = 0
} dm_slice_stat_t;

/*
 * Unlike the Solaris implementation, libdiskmgt must be initialised,
 * and torn down when no longer used.
 */
void libdiskmgt_init(void);
void libdiskmgt_fini(void);

/*
 * This is a partial implementation of (or similar to) libdiskmgt,
 * adapted for OSX use.
 */
int dm_in_swap_dir(const char *dev_name);
int dm_inuse(char *dev_name, char **msg, dm_who_type_t who, int *errp);

#ifdef __cplusplus
}
#endif

#endif
