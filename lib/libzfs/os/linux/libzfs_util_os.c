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


#include <errno.h>
#include <fcntl.h>
#include <libintl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/mnttab.h>
#include <sys/mntent.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <libzfs.h>
#include <libzfs_core.h>

#include "libzfs_impl.h"
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
		    "loaded.\nTry running '/sbin/modprobe zfs' as root "
		    "to load them."));
	case ENOENT:
		return (dgettext(TEXT_DOMAIN, "/dev/zfs and /proc/self/mounts "
		    "are required.\nTry running 'udevadm trigger' and 'mount "
		    "-t proc proc /proc' as root."));
	case ENOEXEC:
		return (dgettext(TEXT_DOMAIN, "The ZFS modules cannot be "
		    "auto-loaded.\nTry running '/sbin/modprobe zfs' as "
		    "root to manually load them."));
	case EACCES:
		return (dgettext(TEXT_DOMAIN, "Permission denied the "
		    "ZFS utilities must be run as root."));
	default:
		return (dgettext(TEXT_DOMAIN, "Failed to initialize the "
		    "libzfs library."));
	}
}

static int
libzfs_module_loaded(const char *module)
{
	const char path_prefix[] = "/sys/module/";
	char path[256];

	memcpy(path, path_prefix, sizeof (path_prefix) - 1);
	strcpy(path + sizeof (path_prefix) - 1, module);

	return (access(path, F_OK) == 0);
}

/*
 * Verify the required ZFS_DEV device is available and optionally attempt
 * to load the ZFS modules.  Under normal circumstances the modules
 * should already have been loaded by some external mechanism.
 *
 * Environment variables:
 * - ZFS_MODULE_LOADING="YES|yes|ON|on" - Attempt to load modules.
 * - ZFS_MODULE_TIMEOUT="<seconds>"     - Seconds to wait for ZFS_DEV
 */
static int
libzfs_load_module_impl(const char *module)
{
	char *argv[4] = {"/sbin/modprobe", "-q", (char *)module, (char *)0};
	char *load_str, *timeout_str;
	long timeout = 10; /* seconds */
	long busy_timeout = 10; /* milliseconds */
	int load = 0, fd;
	hrtime_t start;

	/* Optionally request module loading */
	if (!libzfs_module_loaded(module)) {
		load_str = getenv("ZFS_MODULE_LOADING");
		if (load_str) {
			if (!strncasecmp(load_str, "YES", strlen("YES")) ||
			    !strncasecmp(load_str, "ON", strlen("ON")))
				load = 1;
			else
				load = 0;
		}

		if (load) {
			if (libzfs_run_process("/sbin/modprobe", argv, 0))
				return (ENOEXEC);
		}

		if (!libzfs_module_loaded(module))
			return (ENXIO);
	}

	/*
	 * Device creation by udev is asynchronous and waiting may be
	 * required.  Busy wait for 10ms and then fall back to polling every
	 * 10ms for the allowed timeout (default 10s, max 10m).  This is
	 * done to optimize for the common case where the device is
	 * immediately available and to avoid penalizing the possible
	 * case where udev is slow or unable to create the device.
	 */
	timeout_str = getenv("ZFS_MODULE_TIMEOUT");
	if (timeout_str) {
		timeout = strtol(timeout_str, NULL, 0);
		timeout = MAX(MIN(timeout, (10 * 60)), 0); /* 0 <= N <= 600 */
	}

	start = gethrtime();
	do {
		fd = open(ZFS_DEV, O_RDWR);
		if (fd >= 0) {
			(void) close(fd);
			return (0);
		} else if (errno != ENOENT) {
			return (errno);
		} else if (NSEC2MSEC(gethrtime() - start) < busy_timeout) {
			sched_yield();
		} else {
			usleep(10 * MILLISEC);
		}
	} while (NSEC2MSEC(gethrtime() - start) < (timeout * MILLISEC));

	return (ENOENT);
}

int
libzfs_load_module(void)
{
	return (libzfs_load_module_impl(ZFS_DRIVER));
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

/*
 * Fill given version buffer with zfs kernel version read from ZFS_SYSFS_DIR
 * Returns 0 on success, and -1 on error (with errno set)
 */
int
zfs_version_kernel(char *version, int len)
{
	int _errno;
	int fd;
	int rlen;

	if ((fd = open(ZFS_SYSFS_DIR "/version", O_RDONLY)) == -1)
		return (-1);

	if ((rlen = read(fd, version, len)) == -1) {
		version[0] = '\0';
		_errno = errno;
		(void) close(fd);
		errno = _errno;
		return (-1);
	}

	version[rlen-1] = '\0';  /* discard '\n' */

	if (close(fd) == -1)
		return (-1);

	return (0);
}

static const struct system_property_map {
	zfs_prop_t prop;
	const char *system_name;
	const char *os_name;
} system_property_map[] = {
	{ ZFS_PROP_MOUNT_OPTIONS, "mount_options", "org.openzfs.linux:mount_options" },
	{ ZFS_PROP_TYPE, NULL, NULL },
};

boolean_t
zfs_prop_os_alias(zfs_prop_t prop)
{
	const struct system_property_map *ptr;

	for (ptr = system_property_map;
	    ptr->system_name != NULL;
	    ptr++)
		if (ptr->prop == prop)
			return (B_TRUE);
	return (B_FALSE);
}

const char *
zfs_prop_os_alias_name(zfs_prop_t prop)
{
	const struct system_property_map *ptr;

	for (ptr = system_property_map;
	    ptr->system_name != NULL;
	    ptr++)
		if (ptr->prop == prop)
			return (ptr->os_name);
	return (NULL);
}

/*
 * Set a system options, replacing it in the OS-supplied one.
 */
int
zfs_os_set_system_property(libzfs_handle_t *hdl, nvlist_t *dest, nvpair_t *elem)
{
	const struct system_property_map *ptr;
	char *elem_name;
	char *elem_value;

	assert(nvpair_type(elem) == DATA_TYPE_STRING);

	elem_name = nvpair_name(elem);
	(void) nvpair_value_string(elem, &elem_value);

	for (ptr = system_property_map;
	    ptr && ptr->system_name;
	    ptr++) {
		if (strcmp(ptr->system_name, elem_name) != 0)
			continue;

		(void) nvlist_remove(dest, ptr->system_name, DATA_TYPE_STRING);
		(void) nvlist_remove(dest, ptr->os_name, DATA_TYPE_STRING);
		return (nvlist_add_string(dest, ptr->os_name, elem_value));
	}
	return (ENOENT);
}

int
zfs_os_get_system_property(zfs_handle_t *zhp, zfs_prop_t prop,
    char *propbuf, size_t proplen)
{
	nvlist_t *user_props = zfs_get_user_props(zhp);
	nvlist_t *os_nvlist = NULL;
	const struct system_property_map *ptr;
	char *elem_value;

	if (user_props == NULL)
		return (ENOENT);

	for (ptr = system_property_map;
	    ptr && ptr->system_name;
	    ptr++) {
		if (ptr->prop != prop)
			continue;
		if (nvlist_exists(user_props, ptr->os_name) == B_FALSE)
			return (ENOENT);
		os_nvlist = fnvlist_lookup_nvlist(user_props, ptr->os_name);
		elem_value = fnvlist_lookup_string(os_nvlist, "value");
		if (elem_value && elem_value[0] != '\0' &&
		    strcmp(elem_value, "none") != 0)
			strlcpy(propbuf, elem_value, proplen);
		else
			*propbuf = '\0';
		return (0);
	}
	return (ENOENT);
}
