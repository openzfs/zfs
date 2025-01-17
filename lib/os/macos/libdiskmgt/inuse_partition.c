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
