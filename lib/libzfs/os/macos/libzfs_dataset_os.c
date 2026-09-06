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

#include <libintl.h>
#include "../../libzfs_impl.h"

int
zfs_destroy_snaps_nvl_os(libzfs_handle_t *hdl, nvlist_t *snaps)
{
	struct mnttab entry;
	int ret = 0;
	nvpair_t *pair;

	for (pair = nvlist_next_nvpair(snaps, NULL);
	    pair != NULL;
	    pair = nvlist_next_nvpair(snaps, pair)) {
		zfs_handle_t *zhp = zfs_open(hdl, nvpair_name(pair),
		    ZFS_TYPE_SNAPSHOT);
		if (zhp != NULL) {
			if (zfs_get_type(zhp) == ZFS_TYPE_SNAPSHOT &&
			    libzfs_mnttab_find(hdl, zhp->zfs_name, &entry)
			    == 0)
				ret |= zfs_snapshot_unmount(zhp, MS_FORCE);
			zfs_close(zhp);
		}
	}
	if (ret != 0)
		fprintf(stderr, gettext("could not unmount snapshot(s)\n"));
	return (ret);
}
