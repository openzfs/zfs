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
