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
