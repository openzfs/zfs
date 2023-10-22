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
