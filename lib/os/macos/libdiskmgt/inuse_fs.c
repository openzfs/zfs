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
