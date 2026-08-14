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
 * Copyright (c) 2013 Martin Matuska <mm@FreeBSD.org>. All rights reserved.
 */
#include "../../libzfs_impl.h"
#include <libzfs.h>
#include <libzutil.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/module.h>
#include <sys/stat.h>
#include <libintl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define	ZFS_KMOD	"openzfs"

static __thread char errbuf[ERRBUFLEN];

const char *
libzfs_error_init(int error)
{
	(void) snprintf(errbuf, sizeof (errbuf), "%s", zfs_strerror(error));
	return (errbuf);
}

/*
 * Load openzfs.kmod via modctl(2).  Open /dev/zfs first so the
 * common case (module already loaded) does not talk to the loader.
 * system(3) is not used: PATH/IFS/LD_* would become root RCE in
 * any setuid or sudo consumer, including zed.
 */
int
libzfs_load_module(void)
{
	modctl_load_t ml;
	int fd, err;

	fd = open(ZFS_DEV, O_RDWR);
	if (fd >= 0) {
		(void) close(fd);
		return (0);
	}
	err = errno;

	memset(&ml, 0, sizeof (ml));
	ml.ml_filename = ZFS_KMOD;
	if (modctl(MODCTL_LOAD, &ml) != 0 && errno != EEXIST && errno != 0)
		err = errno;

	fd = open(ZFS_DEV, O_RDWR);
	if (fd >= 0) {
		(void) close(fd);
		return (0);
	}
	if (errno != 0)
		err = errno;
	return (err != 0 ? err : ENOENT);
}

int
zpool_relabel_disk(libzfs_handle_t *hdl, const char *path, const char *msg)
{
	(void) hdl, (void) path, (void) msg;
	return (0);
}

int
zpool_label_disk(libzfs_handle_t *hdl, zpool_handle_t *zhp, const char *name)
{
	(void) hdl, (void) zhp, (void) name;
	return (0);
}

int
find_shares_object(differ_info_t *di)
{
	(void) di;
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
	size_t l;
	if (sysctlbyname("vfs.zfs.version.module",
	    NULL, &l, NULL, 0) == -1)
		return (NULL);
	char *version = malloc(l);
	if (version == NULL)
		return (NULL);
	if (sysctlbyname("vfs.zfs.version.module",
	    version, &l, NULL, 0) == -1) {
		free(version);
		return (NULL);
	}
	return (version);
}
