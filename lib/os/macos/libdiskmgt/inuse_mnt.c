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
#include <sys/param.h>
#include <sys/ucred.h>
#include <sys/mount.h>
#include <libdiskmgt.h>
#include "disks_private.h"

int
inuse_mnt(char *slice, nvlist_t *attrs, int *errp)
{
	struct statfs *mounts;

	/* Read the current set of mounts */
	int num_mounts = getmntinfo(&mounts, MNT_WAIT);

	/* Check whether slice is presently in use */
	for (int i = 0; i < num_mounts; i++) {
		int slice_found = (strcmp(mounts[i].f_mntfromname, slice) == 0);

		if (slice_found) {
			libdiskmgt_add_str(attrs, DM_USED_BY, DM_USE_MOUNT,
			    errp);
			libdiskmgt_add_str(attrs, DM_USED_NAME,
			    mounts[i].f_mntonname, errp);
			return (1);
		}
	}
	return (0);
}
