// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/zfs_ioctl.h>
#include <os/freebsd/zfs/sys/zfs_ioctl_compat.h>
#include <err.h>
#include <libzfs_core.h>

int zfs_ioctl_version = ZFS_IOCVER_UNDEF;

/*
 * Get zfs_ioctl_version
 */
static int
get_zfs_ioctl_version(void)
{
	size_t ver_size;
	int ver = ZFS_IOCVER_NONE;

	ver_size = sizeof (ver);
	(void) sysctlbyname("vfs.zfs.version.ioctl", &ver, &ver_size, NULL, 0);

	return (ver);
}

static int
zcmd_ioctl_compat(int fd, int request, zfs_cmd_t *zc, const int cflag)
{
	int ret;
#ifdef ZFS_LEGACY_SUPPORT
	int newrequest;
	void *zc_c = NULL;
#endif
	unsigned long ncmd;
	zfs_iocparm_t zp;

	switch (cflag) {
	case ZFS_CMD_COMPAT_NONE:
		ncmd = _IOWR('Z', request, zfs_iocparm_t);
		zp.zfs_cmd = (uint64_t)(uintptr_t)zc;
		zp.zfs_cmd_size = sizeof (zfs_cmd_t);
		zp.zfs_ioctl_version = ZFS_IOCVER_OZFS;
		break;
#ifdef ZFS_LEGACY_SUPPORT
	case ZFS_CMD_COMPAT_LEGACY:
		newrequest = zfs_ioctl_ozfs_to_legacy(request);
		ncmd = _IOWR('Z', newrequest, zfs_iocparm_t);
		zc_c = malloc(sizeof (zfs_cmd_legacy_t));
		zfs_cmd_ozfs_to_legacy(zc, zc_c);
		zp.zfs_cmd = (uint64_t)(uintptr_t)zc_c;
		zp.zfs_cmd_size = sizeof (zfs_cmd_legacy_t);
		zp.zfs_ioctl_version = ZFS_IOCVER_LEGACY;
		break;
#endif
	default:
		abort();
		return (EINVAL);
	}

	ret = ioctl(fd, ncmd, &zp);
	if (ret) {
#ifdef ZFS_LEGACY_SUPPORT
		if (zc_c)
			free(zc_c);
#endif
		return (ret);
	}
#ifdef ZFS_LEGACY_SUPPORT
	if (zc_c) {
		zfs_cmd_legacy_to_ozfs(zc_c, zc);
		free(zc_c);
	}
#endif
	return (ret);
}

/*
 * This is FreeBSD version of ioctl, because Solaris' ioctl() updates
 * zc_nvlist_dst_size even if an error is returned, on FreeBSD if an
 * error is returned zc_nvlist_dst_size won't be updated.
 */
int
lzc_ioctl_fd(int fd, unsigned long request, zfs_cmd_t *zc)
{
	size_t oldsize;
	int ret, cflag = ZFS_CMD_COMPAT_NONE;

	if (zfs_ioctl_version == ZFS_IOCVER_UNDEF)
		zfs_ioctl_version = get_zfs_ioctl_version();

	switch (zfs_ioctl_version) {
#ifdef ZFS_LEGACY_SUPPORT
		case ZFS_IOCVER_LEGACY:
			cflag = ZFS_CMD_COMPAT_LEGACY;
			break;
#endif
		case ZFS_IOCVER_OZFS:
			cflag = ZFS_CMD_COMPAT_NONE;
			break;
		default:
			errx(1, "unrecognized zfs ioctl version %d",
			    zfs_ioctl_version);
	}

	oldsize = zc->zc_nvlist_dst_size;
	ret = zcmd_ioctl_compat(fd, request, zc, cflag);

	if (ret == 0 && oldsize < zc->zc_nvlist_dst_size) {
		ret = -1;
		errno = ENOMEM;
	}

	return (ret);
}
