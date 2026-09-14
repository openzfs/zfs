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
