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
#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/zfs_ioctl.h>
#include <os/freebsd/zfs/sys/zfs_ioctl_compat.h>
#include <err.h>
#include <libzfs_core.h>
#include "libzfs_core_impl.h"

int
lzc_ioctl_fd_os(int fd, unsigned long request, zfs_cmd_t *zc)
{
	/* Wrap zfs_cmd_t in zfs_iocparm_t, see zfs_ioctl_compat.h */
	zfs_iocparm_t zp = {
	    .zfs_cmd = (uint64_t)(uintptr_t)zc,
	    .zfs_cmd_size = sizeof (zfs_cmd_t),
	    .zfs_ioctl_version = ZFS_IOCVER_OZFS,
	};

	/*
	 * Solaris' ioctl() updates zc_nvlist_dst_size even if an error is
	 * returned, on FreeBSD if an error is returned zc_nvlist_dst_size
	 * won't be updated.
	 */
	size_t oldsize = zc->zc_nvlist_dst_size;

	int ret = ioctl(fd, _IOWR('Z', request, zfs_iocparm_t), &zp);

	if (ret == 0 && oldsize < zc->zc_nvlist_dst_size) {
		ret = -1;
		errno = ENOMEM;
	}

	return (ret);
}
