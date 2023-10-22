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

#ifndef DISKS_PRIVATE_H
#define	DISKS_PRIVATE_H

#include <libnvpair.h>
#include <CoreFoundation/CoreFoundation.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	NVATTRS	NV_UNIQUE_NAME | NV_UNIQUE_NAME_TYPE
#define	NVATTRS_STAT	0x0

	typedef void* DU_Info;

	void diskutil_init(void);
	void diskutil_fini(void);

	void init_diskutil_info(DU_Info *info);
	int diskutil_info_valid(DU_Info info);
	void get_diskutil_cs_info(char *slice, DU_Info *info);
	void get_diskutil_info(char *slice, DU_Info *info);
	int is_cs_disk(DU_Info *info);
	int is_cs_converted(DU_Info *info);
	int is_cs_locked(DU_Info *info);
	int is_cs_logical_volume(DU_Info *info);
	int is_cs_physical_volume(DU_Info *info);
	int is_cs_online(DU_Info *info);
	CFStringRef get_cs_LV_status(DU_Info *info);

	int is_whole_disk(DU_Info info);
	int is_efi_partition(DU_Info info);
	int is_recovery_partition(DU_Info info);
	int is_APFS_partition(DU_Info info);
	int is_HFS_partition(DU_Info info);
	int is_MSDOS_partition(DU_Info info);
	int has_filesystem_type(DU_Info info);
	CFStringRef get_filesystem_type(DU_Info info);

	int inuse_corestorage(char *slice, nvlist_t *attrs, int *errp);
	int inuse_fs(char *slice, nvlist_t *attrs, int *errp);
	int inuse_macswap(const char *dev_name);
	int inuse_mnt(char *slice, nvlist_t *attrs, int *errp);
	int inuse_partition(char *slice, nvlist_t *attrs, int *errp);
	int inuse_active_zpool(char *slice, nvlist_t *attrs, int *errp);
	int inuse_exported_zpool(char *slice, nvlist_t *attrs, int *errp);

	void libdiskmgt_add_str(nvlist_t *attrs, const char *name,
	    const char *val, int *errp);

	nvlist_t *slice_get_stats(char *slice, int stat_type, int *errp);

#ifdef __cplusplus
}
#endif

#endif
