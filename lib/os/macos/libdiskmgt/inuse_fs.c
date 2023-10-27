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

#include <string.h>
#include <libnvpair.h>
#include <libdiskmgt.h>
#include "disks_private.h"


/*
 * Use the heuristics to check for a filesystem on the slice.
 */
int
inuse_fs(char *slice, nvlist_t *attrs, int *errp)
{
	int in_use = 0;
	DU_Info info;

	init_diskutil_info(&info);
	get_diskutil_info(slice, &info);

	if (diskutil_info_valid(info) && has_filesystem_type(info)) {
		CFStringRef filesystem_type = get_filesystem_type(info);
		char filesystem_type_str[128] = { 0 };
		Boolean success =
		    CFStringGetCString(filesystem_type,
		    filesystem_type_str, 128,
		    kCFStringEncodingUTF8);

		if (filesystem_type &&
		    (CFStringCompare(filesystem_type, CFSTR("zfs"),
		    kCFCompareCaseInsensitive) != kCFCompareEqualTo)) {

			if (CFStringCompare(filesystem_type, CFSTR("apfs"),
			    kCFCompareCaseInsensitive) == kCFCompareEqualTo) {
				libdiskmgt_add_str(attrs, DM_USED_BY,
				    DM_USE_FS_NO_FORCE, errp);
			} else {
				libdiskmgt_add_str(attrs, DM_USED_BY,
				    DM_USE_FS, errp);
			}

			if (success) {
				libdiskmgt_add_str(attrs, DM_USED_NAME,
				    filesystem_type_str, errp);
			} else {
				libdiskmgt_add_str(attrs, DM_USED_NAME,
				    "Unknown", errp);
			}
			in_use = 1;
		}
	}

	return (in_use);
}
