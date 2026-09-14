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

#include <libnvpair.h>
#include <libdiskmgt.h>
#include "disks_private.h"

int
inuse_partition(char *slice, nvlist_t *attrs, int *errp)
{
	int in_use = 0;
	DU_Info info;

	init_diskutil_info(&info);
	get_diskutil_info(slice, &info);

	if (diskutil_info_valid(info)) {
		if (is_efi_partition(info)) {
			libdiskmgt_add_str(attrs, DM_USED_BY,
			    DM_USE_OS_PARTITION, errp);
			libdiskmgt_add_str(attrs, DM_USED_NAME,
			    "EFI", errp);
			in_use = 1;
		} else if (is_recovery_partition(info)) {
			libdiskmgt_add_str(attrs, DM_USED_BY,
			    DM_USE_OS_PARTITION_NO_FORCE, errp);
			libdiskmgt_add_str(attrs, DM_USED_NAME,
			    "Recovery", errp);
			in_use = 1;
		} else if (is_APFS_partition(info)) {
			libdiskmgt_add_str(attrs, DM_USED_BY,
			    DM_USE_OS_PARTITION_NO_FORCE, errp);
			libdiskmgt_add_str(attrs, DM_USED_NAME,
			    "APFS", errp);
			in_use = 1;
		} else if (is_HFS_partition(info)) {
			libdiskmgt_add_str(attrs, DM_USED_BY,
			    DM_USE_OS_PARTITION, errp);
			libdiskmgt_add_str(attrs, DM_USED_NAME,
			    "HFS", errp);
			in_use = 1;
		} else if (is_MSDOS_partition(info)) {
			libdiskmgt_add_str(attrs, DM_USED_BY,
			    DM_USE_OS_PARTITION, errp);
			libdiskmgt_add_str(attrs, DM_USED_NAME,
			    "MSDOS", errp);
			in_use = 1;
		}
	}

	return (in_use);
}
