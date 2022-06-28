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
 * Copyright (c) 2021 Klara, Inc.
 */

#include <alloca.h>
#include <errno.h>
#include <fcntl.h>
#include <libintl.h>
#include <math.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/inotify.h>
#include <sys/mntent.h>
#include <sys/mnttab.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <libzfs.h>
#include <libzfs_core.h>

#include "../../libzfs_impl.h"
#include "zfs_prop.h"
#include <libzutil.h>
#include <sys/zfs_sysfs.h>

#define	ZDIFF_SHARESDIR		"/.zfs/shares/"

int
zfs_ioctl(libzfs_handle_t *hdl, int request, zfs_cmd_t *zc)
{
	return (ioctl(hdl->libzfs_fd, request, zc));
}

const char *
libzfs_error_init(int error)
{
	switch (error) {
	case ENXIO:
		return (dgettext(TEXT_DOMAIN, "The ZFS modules are not "
		    "loaded.\nTry running 'modprobe zfs' as root "
		    "to load them."));
	case ENOENT:
		return (dgettext(TEXT_DOMAIN, "/dev/zfs and /proc/self/mounts "
		    "are required.\nTry running 'udevadm trigger' and 'mount "
		    "-t proc proc /proc' as root."));
	case ENOEXEC:
		return (dgettext(TEXT_DOMAIN, "The ZFS modules cannot be "
		    "auto-loaded.\nTry running 'modprobe zfs' as "
		    "root to manually load them."));
	case EACCES:
		return (dgettext(TEXT_DOMAIN, "Permission denied the "
		    "ZFS utilities must be run as root."));
	default:
		return (dgettext(TEXT_DOMAIN, "Failed to initialize the "
		    "libzfs library."));
	}
}

/*
 * zfs(4) is loaded by udev if there's a fstype=zfs device present,
 * but if there isn't, load them automatically;
 * always wait for ZFS_DEV to appear via udev.
 *
 * Environment variables:
 * - ZFS_MODULE_TIMEOUT="<seconds>" - Seconds to wait for ZFS_DEV,
 *                                    defaults to 10, max. 10 min.
 */
int
libzfs_load_module(void)
{
	if (access(ZFS_DEV, F_OK) == 0)
		return (0);

	if (access(ZFS_SYSFS_DIR, F_OK) != 0) {
		char *argv[] = {(char *)"modprobe", (char *)"zfs", NULL};
		if (libzfs_run_process("modprobe", argv, 0))
			return (ENOEXEC);

		if (access(ZFS_SYSFS_DIR, F_OK) != 0)
			return (ENXIO);
	}

	const char *timeout_str = getenv("ZFS_MODULE_TIMEOUT");
	int seconds = 10;
	if (timeout_str)
		seconds = MIN(strtol(timeout_str, NULL, 0), 600);
	struct itimerspec timeout = {.it_value.tv_sec = MAX(seconds, 0)};

	int ino = inotify_init1(IN_CLOEXEC);
	if (ino == -1)
		return (ENOENT);
	inotify_add_watch(ino, ZFS_DEVDIR, IN_CREATE);

	if (access(ZFS_DEV, F_OK) == 0) {
		close(ino);
		return (0);
	} else if (seconds == 0) {
		close(ino);
		return (ENOENT);
	}

	size_t evsz = sizeof (struct inotify_event) + NAME_MAX + 1;
	struct inotify_event *ev = alloca(evsz);

	int tout = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
	if (tout == -1) {
		close(ino);
		return (ENOENT);
	}
	timerfd_settime(tout, 0, &timeout, NULL);

	int ret = ENOENT;
	struct pollfd pfds[] = {
		{.fd = ino, .events = POLLIN},
		{.fd = tout, .events = POLLIN},
	};
	while (poll(pfds, ARRAY_SIZE(pfds), -1) != -1) {
		if (pfds[0].revents & POLLIN) {
			verify(read(ino, ev, evsz) >
			    sizeof (struct inotify_event));
			if (strcmp(ev->name, &ZFS_DEV[sizeof (ZFS_DEVDIR)])
			    == 0) {
				ret = 0;
				break;
			}
		}
		if (pfds[1].revents & POLLIN)
			break;
	}
	close(tout);
	close(ino);
	return (ret);
}

int
find_shares_object(differ_info_t *di)
{
	char fullpath[MAXPATHLEN];
	struct stat64 sb = { 0 };

	(void) strlcpy(fullpath, di->dsmnt, MAXPATHLEN);
	(void) strlcat(fullpath, ZDIFF_SHARESDIR, MAXPATHLEN);

	if (stat64(fullpath, &sb) != 0) {
		(void) snprintf(di->errbuf, sizeof (di->errbuf),
		    dgettext(TEXT_DOMAIN, "Cannot stat %s"), fullpath);
		return (zfs_error(di->zhp->zfs_hdl, EZFS_DIFF, di->errbuf));
	}

	di->shares = (uint64_t)sb.st_ino;
	return (0);
}

int
zfs_destroy_snaps_nvl_os(libzfs_handle_t *hdl, nvlist_t *snaps)
{
	(void) hdl, (void) snaps;
	return (0);
}

/*
 * Return allocated loaded module version, or NULL on error (with errno set)
 */
char *
zfs_version_kernel(void)
{
	FILE *f = fopen(ZFS_SYSFS_DIR "/version", "re");
	if (f == NULL)
		return (NULL);

	char *ret = NULL;
	size_t l;
	ssize_t read;
	if ((read = getline(&ret, &l, f)) == -1) {
		int err = errno;
		fclose(f);
		errno = err;
		return (NULL);
	}

	fclose(f);
	if (ret[read - 1] == '\n')
		ret[read - 1] = '\0';
	return (ret);
}

/*
 * Add or delete the given filesystem to/from the given user namespace.
 */
int
zfs_userns(zfs_handle_t *zhp, const char *nspath, int attach)
{
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	zfs_cmd_t zc = {"\0"};
	char errbuf[1024];
	unsigned long cmd;
	int ret;

	if (attach) {
		(void) snprintf(errbuf, sizeof (errbuf),
		    dgettext(TEXT_DOMAIN, "cannot add '%s' to namespace"),
		    zhp->zfs_name);
	} else {
		(void) snprintf(errbuf, sizeof (errbuf),
		    dgettext(TEXT_DOMAIN, "cannot remove '%s' from namespace"),
		    zhp->zfs_name);
	}

	switch (zhp->zfs_type) {
	case ZFS_TYPE_VOLUME:
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "volumes can not be namespaced"));
		return (zfs_error(hdl, EZFS_BADTYPE, errbuf));
	case ZFS_TYPE_SNAPSHOT:
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "snapshots can not be namespaced"));
		return (zfs_error(hdl, EZFS_BADTYPE, errbuf));
	case ZFS_TYPE_BOOKMARK:
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "bookmarks can not be namespaced"));
		return (zfs_error(hdl, EZFS_BADTYPE, errbuf));
	case ZFS_TYPE_VDEV:
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "vdevs can not be namespaced"));
		return (zfs_error(hdl, EZFS_BADTYPE, errbuf));
	case ZFS_TYPE_INVALID:
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "invalid zfs_type_t: ZFS_TYPE_INVALID"));
		return (zfs_error(hdl, EZFS_BADTYPE, errbuf));
	case ZFS_TYPE_POOL:
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "pools can not be namespaced"));
		return (zfs_error(hdl, EZFS_BADTYPE, errbuf));
	case ZFS_TYPE_FILESYSTEM:
		break;
	}
	assert(zhp->zfs_type == ZFS_TYPE_FILESYSTEM);

	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));
	zc.zc_objset_type = DMU_OST_ZFS;
	zc.zc_cleanup_fd = open(nspath, O_RDONLY);
	if (zc.zc_cleanup_fd < 0) {
		return (zfs_error(hdl, EZFS_NOT_USER_NAMESPACE, errbuf));
	}

	cmd = attach ? ZFS_IOC_USERNS_ATTACH : ZFS_IOC_USERNS_DETACH;
	if ((ret = zfs_ioctl(hdl, cmd, &zc)) != 0)
		zfs_standard_error(hdl, errno, errbuf);

	(void) close(zc.zc_cleanup_fd);

	return (ret);
}
