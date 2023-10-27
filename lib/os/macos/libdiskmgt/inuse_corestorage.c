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
