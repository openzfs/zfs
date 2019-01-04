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
 * Copyright 2013 Xin Li <delphij@FreeBSD.org>. All rights reserved.
 * Copyright 2013 Martin Matuska <mm@FreeBSD.org>. All rights reserved.
 * Portions Copyright 2005, 2010, Oracle and/or its affiliates.
 * All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/cred.h>
#include <sys/dmu.h>
#include <sys/zio.h>
#include <sys/nvpair.h>
#include <sys/dsl_deleg.h>
#include <sys/zfs_ioctl.h>
#include "zfs_namecheck.h"
#include <os/freebsd/zfs/sys/zfs_ioctl_compat.h>

/*
 * FreeBSD zfs_cmd compatibility with older binaries
 * appropriately remap/extend the zfs_cmd_t structure
 */
void
zfs_cmd_compat_get(zfs_cmd_t *zc, caddr_t addr, const int cflag)
{

}
#if 0
static int
zfs_ioctl_compat_get_nvlist(uint64_t nvl, size_t size, int iflag,
    nvlist_t **nvp)
{
	char *packed;
	int error;
	nvlist_t *list = NULL;

	/*
	 * Read in and unpack the user-supplied nvlist.
	 */
	if (size == 0)
		return (EINVAL);

#ifdef _KERNEL
	packed = kmem_alloc(size, KM_SLEEP);
	if ((error = ddi_copyin((void *)(uintptr_t)nvl, packed, size,
	    iflag)) != 0) {
		kmem_free(packed, size);
		return (error);
	}
#else
	packed = (void *)(uintptr_t)nvl;
#endif

	error = nvlist_unpack(packed, size, &list, 0);

#ifdef _KERNEL
	kmem_free(packed, size);
#endif

	if (error != 0)
		return (error);

	*nvp = list;
	return (0);
}

static int
zfs_ioctl_compat_put_nvlist(zfs_cmd_t *zc, nvlist_t *nvl)
{
	char *packed = NULL;
	int error = 0;
	size_t size;

	VERIFY(nvlist_size(nvl, &size, NV_ENCODE_NATIVE) == 0);

#ifdef _KERNEL
	packed = kmem_alloc(size, KM_SLEEP);
	VERIFY(nvlist_pack(nvl, &packed, &size, NV_ENCODE_NATIVE,
	    KM_SLEEP) == 0);

	if (ddi_copyout(packed,
	    (void *)(uintptr_t)zc->zc_nvlist_dst, size, zc->zc_iflags) != 0)
		error = EFAULT;
	kmem_free(packed, size);
#else
	packed = (void *)(uintptr_t)zc->zc_nvlist_dst;
	VERIFY(nvlist_pack(nvl, &packed, &size, NV_ENCODE_NATIVE,
	    0) == 0);
#endif

	zc->zc_nvlist_dst_size = size;
	return (error);
}

static void
zfs_ioctl_compat_fix_stats_nvlist(nvlist_t *nvl)
{
	nvlist_t **child;
	nvlist_t *nvroot = NULL;
	vdev_stat_t *vs;
	uint_t c, children, nelem;

	if (nvlist_lookup_nvlist_array(nvl, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) == 0) {
		for (c = 0; c < children; c++) {
			zfs_ioctl_compat_fix_stats_nvlist(child[c]);
		}
	}

	if (nvlist_lookup_nvlist(nvl, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) == 0)
		zfs_ioctl_compat_fix_stats_nvlist(nvroot);
	if ((nvlist_lookup_uint64_array(nvl, "stats",
	    (uint64_t **)&vs, &nelem) == 0)) {
		nvlist_add_uint64_array(nvl,
		    ZPOOL_CONFIG_VDEV_STATS,
		    (uint64_t *)vs, nelem);
		nvlist_remove(nvl, "stats",
		    DATA_TYPE_UINT64_ARRAY);
	}
}


static int
zfs_ioctl_compat_fix_stats(zfs_cmd_t *zc, const int nc)
{
	nvlist_t *nv, *nvp = NULL;
	nvpair_t *elem;
	int error;

	if ((error = zfs_ioctl_compat_get_nvlist(zc->zc_nvlist_dst,
	    zc->zc_nvlist_dst_size, zc->zc_iflags, &nv)) != 0)
		return (error);

	if (nc == 5) { /* ZFS_IOC_POOL_STATS */
		elem = NULL;
		while ((elem = nvlist_next_nvpair(nv, elem)) != NULL) {
			if (nvpair_value_nvlist(elem, &nvp) == 0)
				zfs_ioctl_compat_fix_stats_nvlist(nvp);
		}
		elem = NULL;
	} else
		zfs_ioctl_compat_fix_stats_nvlist(nv);

	error = zfs_ioctl_compat_put_nvlist(zc, nv);

	nvlist_free(nv);

	return (error);
}

static int
zfs_ioctl_compat_pool_get_props(zfs_cmd_t *zc)
{
	nvlist_t *nv, *nva = NULL;
	int error;

	if ((error = zfs_ioctl_compat_get_nvlist(zc->zc_nvlist_dst,
	    zc->zc_nvlist_dst_size, zc->zc_iflags, &nv)) != 0)
		return (error);

	if (nvlist_lookup_nvlist(nv, "used", &nva) == 0) {
		nvlist_add_nvlist(nv, "allocated", nva);
		nvlist_remove(nv, "used", DATA_TYPE_NVLIST);
	}

	if (nvlist_lookup_nvlist(nv, "available", &nva) == 0) {
		nvlist_add_nvlist(nv, "free", nva);
		nvlist_remove(nv, "available", DATA_TYPE_NVLIST);
	}

	error = zfs_ioctl_compat_put_nvlist(zc, nv);

	nvlist_free(nv);

	return (error);
}
#endif

#ifdef _KERNEL
int
zfs_ioctl_compat_pre(zfs_cmd_t *zc, int *vec, const int cflag)
{
	int error = 0;

	/* are we creating a clone? */
	if (*vec == ZFS_IOC_CREATE && zc->zc_value[0] != '\0')
		*vec = ZFS_IOC_CLONE;

	if (cflag == ZFS_CMD_COMPAT_V15) {
		switch (*vec) {

		case 7: /* ZFS_IOC_POOL_SCRUB (v15) */
			zc->zc_cookie = POOL_SCAN_SCRUB;
			break;
		}
	}

	return (error);
}

void
zfs_ioctl_compat_post(zfs_cmd_t *zc, int vec, const int cflag)
{
	if (cflag == ZFS_CMD_COMPAT_V15) {
		switch (vec) {
		case ZFS_IOC_POOL_CONFIGS:
		case ZFS_IOC_POOL_STATS:
		case ZFS_IOC_POOL_TRYIMPORT:
			zfs_ioctl_compat_fix_stats(zc, vec);
			break;
		case 41: /* ZFS_IOC_POOL_GET_PROPS (v15) */
			zfs_ioctl_compat_pool_get_props(zc);
			break;
		}
	}
}

nvlist_t *
zfs_ioctl_compat_innvl(zfs_cmd_t *zc, nvlist_t *innvl, const int vec,
    const int cflag)
{
	nvlist_t *nvl, *tmpnvl, *hnvl;
	nvpair_t *elem;
	char *poolname, *snapname;
	int err;

	if (cflag == ZFS_CMD_COMPAT_NONE || cflag == ZFS_CMD_COMPAT_LZC ||
	    cflag == ZFS_CMD_COMPAT_ZCMD || cflag == ZFS_CMD_COMPAT_EDBP ||
	    cflag == ZFS_CMD_COMPAT_RESUME || cflag == ZFS_CMD_COMPAT_INLANES)
		goto out;

	switch (vec) {
	case ZFS_IOC_CREATE:
		nvl = fnvlist_alloc();
		fnvlist_add_int32(nvl, "type", zc->zc_objset_type);
		if (innvl != NULL) {
			fnvlist_add_nvlist(nvl, "props", innvl);
			nvlist_free(innvl);
		}
		return (nvl);
	break;
	case ZFS_IOC_CLONE:
		nvl = fnvlist_alloc();
		fnvlist_add_string(nvl, "origin", zc->zc_value);
		if (innvl != NULL) {
			fnvlist_add_nvlist(nvl, "props", innvl);
			nvlist_free(innvl);
		}
		return (nvl);
	break;
	case ZFS_IOC_SNAPSHOT:
		if (innvl == NULL)
			goto out;
		nvl = fnvlist_alloc();
		fnvlist_add_nvlist(nvl, "props", innvl);
		tmpnvl = fnvlist_alloc();
		snapname = kmem_asprintf("%s@%s", zc->zc_name, zc->zc_value);
		fnvlist_add_boolean(tmpnvl, snapname);
		kmem_free(snapname, strlen(snapname + 1));
		/* check if we are doing a recursive snapshot */
		if (zc->zc_cookie)
			dmu_get_recursive_snaps_nvl(zc->zc_name, zc->zc_value,
			    tmpnvl);
		fnvlist_add_nvlist(nvl, "snaps", tmpnvl);
		fnvlist_free(tmpnvl);
		nvlist_free(innvl);
		/* strip dataset part from zc->zc_name */
		zc->zc_name[strcspn(zc->zc_name, "/@")] = '\0';
		return (nvl);
	break;
	case ZFS_IOC_SPACE_SNAPS:
		nvl = fnvlist_alloc();
		fnvlist_add_string(nvl, "firstsnap", zc->zc_value);
		if (innvl != NULL)
			nvlist_free(innvl);
		return (nvl);
	break;
	case ZFS_IOC_DESTROY_SNAPS:
		if (innvl == NULL && cflag == ZFS_CMD_COMPAT_DEADMAN)
			goto out;
		nvl = fnvlist_alloc();
		if (innvl != NULL) {
			fnvlist_add_nvlist(nvl, "snaps", innvl);
		} else {
			/*
			 * We are probably called by even older binaries,
			 * allocate and populate nvlist with recursive
			 * snapshots
			 */
			if (zfs_component_namecheck(zc->zc_value, NULL,
			    NULL) == 0) {
				tmpnvl = fnvlist_alloc();
				if (dmu_get_recursive_snaps_nvl(zc->zc_name,
				    zc->zc_value, tmpnvl) == 0)
					fnvlist_add_nvlist(nvl, "snaps",
					    tmpnvl);
				nvlist_free(tmpnvl);
			}
		}
		if (innvl != NULL)
			nvlist_free(innvl);
		/* strip dataset part from zc->zc_name */
		zc->zc_name[strcspn(zc->zc_name, "/@")] = '\0';
		return (nvl);
	break;
	case ZFS_IOC_HOLD:
		nvl = fnvlist_alloc();
		tmpnvl = fnvlist_alloc();
		if (zc->zc_cleanup_fd != -1)
			fnvlist_add_int32(nvl, "cleanup_fd",
			    (int32_t)zc->zc_cleanup_fd);
		if (zc->zc_cookie) {
			hnvl = fnvlist_alloc();
			if (dmu_get_recursive_snaps_nvl(zc->zc_name,
			    zc->zc_value, hnvl) == 0) {
				elem = NULL;
				while ((elem = nvlist_next_nvpair(hnvl,
				    elem)) != NULL) {
					nvlist_add_string(tmpnvl,
					    nvpair_name(elem), zc->zc_string);
				}
			}
			nvlist_free(hnvl);
		} else {
			snapname = kmem_asprintf("%s@%s", zc->zc_name,
			    zc->zc_value);
			nvlist_add_string(tmpnvl, snapname, zc->zc_string);
			kmem_free(snapname, strlen(snapname + 1));
		}
		fnvlist_add_nvlist(nvl, "holds", tmpnvl);
		nvlist_free(tmpnvl);
		if (innvl != NULL)
			nvlist_free(innvl);
		/* strip dataset part from zc->zc_name */
		zc->zc_name[strcspn(zc->zc_name, "/@")] = '\0';
		return (nvl);
	break;
	case ZFS_IOC_RELEASE:
		nvl = fnvlist_alloc();
		tmpnvl = fnvlist_alloc();
		if (zc->zc_cookie) {
			hnvl = fnvlist_alloc();
			if (dmu_get_recursive_snaps_nvl(zc->zc_name,
			    zc->zc_value, hnvl) == 0) {
				elem = NULL;
				while ((elem = nvlist_next_nvpair(hnvl,
				    elem)) != NULL) {
					fnvlist_add_boolean(tmpnvl,
					    zc->zc_string);
					fnvlist_add_nvlist(nvl,
					    nvpair_name(elem), tmpnvl);
				}
			}
			nvlist_free(hnvl);
		} else {
			snapname = kmem_asprintf("%s@%s", zc->zc_name,
			    zc->zc_value);
			fnvlist_add_boolean(tmpnvl, zc->zc_string);
			fnvlist_add_nvlist(nvl, snapname, tmpnvl);
			kmem_free(snapname, strlen(snapname + 1));
		}
		nvlist_free(tmpnvl);
		if (innvl != NULL)
			nvlist_free(innvl);
		/* strip dataset part from zc->zc_name */
		zc->zc_name[strcspn(zc->zc_name, "/@")] = '\0';
		return (nvl);
	break;
	}
out:
	return (innvl);
}

nvlist_t *
zfs_ioctl_compat_outnvl(zfs_cmd_t *zc, nvlist_t *outnvl, const int vec,
    const int cflag)
{
	nvlist_t *tmpnvl;

	if (cflag == ZFS_CMD_COMPAT_NONE || cflag == ZFS_CMD_COMPAT_LZC ||
	    cflag == ZFS_CMD_COMPAT_ZCMD || cflag == ZFS_CMD_COMPAT_EDBP ||
	    cflag == ZFS_CMD_COMPAT_RESUME || cflag == ZFS_CMD_COMPAT_INLANES)
		return (outnvl);

	switch (vec) {
	case ZFS_IOC_SPACE_SNAPS:
		(void) nvlist_lookup_uint64(outnvl, "used", &zc->zc_cookie);
		(void) nvlist_lookup_uint64(outnvl, "compressed",
		    &zc->zc_objset_type);
		(void) nvlist_lookup_uint64(outnvl, "uncompressed",
		    &zc->zc_perm_action);
		nvlist_free(outnvl);
		/* return empty outnvl */
		tmpnvl = fnvlist_alloc();
		return (tmpnvl);
	break;
	case ZFS_IOC_CREATE:
	case ZFS_IOC_CLONE:
	case ZFS_IOC_HOLD:
	case ZFS_IOC_RELEASE:
		nvlist_free(outnvl);
		/* return empty outnvl */
		tmpnvl = fnvlist_alloc();
		return (tmpnvl);
	break;
	}

	return (outnvl);
}
#endif /* KERNEL */
