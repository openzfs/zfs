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
inuse_corestorage(char *slice, nvlist_t *attrs, int *errp)
{
	DU_Info info;
	int in_use = 0;

	init_diskutil_info(&info);

	get_diskutil_cs_info(slice, &info);

	if (diskutil_info_valid(info)) {
		if (is_cs_physical_volume(info)) {
			libdiskmgt_add_str(attrs, DM_USED_BY,
			    DM_USE_CORESTORAGE_PV, errp);
			libdiskmgt_add_str(attrs, DM_USED_NAME,
			    slice, errp);
			in_use = 1;
		} else if (is_cs_logical_volume(info)) {

			if (is_cs_locked(info)) {
				libdiskmgt_add_str(attrs, DM_USED_BY,
				    DM_USE_CORESTORAGE_LOCKED_LV, errp);
				libdiskmgt_add_str(attrs, DM_USED_NAME,
				    slice, errp);
				in_use = 1;
			} else if (!is_cs_converted(info)) {
				CFStringRef lv_status = get_cs_LV_status(info);
				char lv_status_str[128] = { 0 };
				Boolean success =
				    CFStringGetCString(lv_status,
				    lv_status_str, 128,
				    kCFStringEncodingMacRoman);

				libdiskmgt_add_str(attrs, DM_USED_BY,
				    DM_USE_CORESTORAGE_CONVERTING_LV, errp);

				if (success) {
					libdiskmgt_add_str(attrs, DM_USED_NAME,
					    lv_status_str, errp);
				} else {
					libdiskmgt_add_str(attrs, DM_USED_NAME,
					    "Unknown", errp);
				}
				in_use = 1;
			} else if (!is_cs_online(info)) {
				CFStringRef lv_status = get_cs_LV_status(info);
				char lv_status_str[128] = { 0 };
				Boolean success =
				    CFStringGetCString(lv_status,
				    lv_status_str, 128,
				    kCFStringEncodingMacRoman);

				libdiskmgt_add_str(attrs, DM_USED_BY,
				    DM_USE_CORESTORAGE_OFFLINE_LV, errp);

				if (success) {
					libdiskmgt_add_str(attrs, DM_USED_NAME,
					    lv_status_str, errp);
				} else {
					libdiskmgt_add_str(attrs,
					    DM_USED_NAME,
					    "Unknown", errp);
				}
				in_use = 1;
			}
		}
	}

	return (in_use);
}
