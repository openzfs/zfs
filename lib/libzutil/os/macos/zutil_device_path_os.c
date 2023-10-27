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

#include <ctype.h>
#include <fcntl.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/efi_partition.h>

#include <libzutil.h>

/*
 * Note: The caller must free the returned string.
 */
char *
zfs_strip_partition(const char *dev)
{
	unsigned int disk, slice;
	char *partless;
	char whole_disk[MAXPATHLEN];

	partless = strdup(dev);

	/* Ends with "diskNsP" - where 'N' and 'P' are integers - strip sP */
	if (sscanf(partless, "disk%us%u", &disk, &slice) == 2) {
		char *r;
		r = strrchr(partless, 's');
		if (r != NULL)
			*r = 0;
	} else if ((sscanf(partless, "%[^:]:%u", whole_disk, &slice)) == 2) {
		char *r;
		r = strrchr(partless, ':');
		if (r != NULL) {
			if (strchr(partless, '@')) { // by-path
				if (slice == 1)
					r[1] = '0';
			} else // by-serial
				*r = 0;
		}
	}

	return (partless);
}

int
zfs_append_partition(char *path, size_t max_len)
{
	int len = strlen(path);
	char dpath[max_len];
	if (strncmp(path, "/var/", 5) == 0) {
		(void) strlcpy(dpath, "/private", max_len);
		(void) strlcat(dpath, path, max_len);
	} else
		strlcpy(dpath, path, max_len);


	if (strncmp(dpath, "/private/var/run/disk/by-id", 27) == 0) {
		return (len);
	} else if (strncmp(dpath, "/private/var/run/disk/by-path", 29) == 0) {
		if (path[len - 1] == '0' &&
		    path[len - 2] == ':')
			path[len - 1] = '1';

	} else if (strncmp(dpath, "/private/var/run/disk/by-serial", 31) == 0) {
		if (len + 2 >= max_len)
			return (-1);

		if (strchr(path, ':') == NULL) {
			(void) strcat(path, ":1");
			len += 2;
		}
	} else {

		if (len + 2 >= max_len)
			return (-1);

		if (isdigit(path[len-1])) {
			(void) strcat(path, "s1");
			len += 2;
		} else {
			(void) strcat(path, "1");
			len += 1;
		}
	}

	return (len);
}

/*
 * Strip the path from a device name.
 * On FreeBSD we only want to remove "/dev/" from the beginning of
 * paths if present.
 */
const char *
zfs_strip_path(const char *path)
{
	char *r;
	r = strrchr(path, '/');
	if (r == NULL)
		return (r);
	return (&r[1]);
}

char *
zfs_get_underlying_path(const char *dev_name)
{

	if (dev_name == NULL)
		return (NULL);

	return (realpath(dev_name, NULL));
}

boolean_t
zfs_dev_is_whole_disk(const char *dev_name)
{
	struct dk_gpt *label;
	int fd;

	if ((fd = open(dev_name, O_RDONLY | O_DIRECT)) < 0)
		return (B_FALSE);

	if (efi_alloc_and_init(fd, EFI_NUMPAR, &label) != 0) {
		(void) close(fd);
		return (B_FALSE);
	}

	efi_free(label);
	(void) close(fd);

	return (B_TRUE);
}

/*
 * Wait up to timeout_ms for udev to set up the device node.  The device is
 * considered ready when libudev determines it has been initialized, all of
 * the device links have been verified to exist, and it has been allowed to
 * settle.  At this point the device the device can be accessed reliably.
 * Depending on the complexity of the udev rules this process could take
 * several seconds.
 */
int
zpool_label_disk_wait(const char *path, int timeout_ms)
{
	int settle_ms = 50;
	long sleep_ms = 10;
	hrtime_t start, settle;
	struct stat64 statbuf;

	start = gethrtime();
	settle = 0;

	do {
		errno = 0;
		if ((stat64(path, &statbuf) == 0) && (errno == 0)) {
			if (settle == 0)
				settle = gethrtime();
			else if (NSEC2MSEC(gethrtime() - settle) >= settle_ms)
				return (0);
		} else if (errno != ENOENT) {
			return (errno);
		}

		usleep(sleep_ms * MILLISEC);
	} while (NSEC2MSEC(gethrtime() - start) < timeout_ms);

	return (ENODEV);
}

boolean_t
is_mpath_whole_disk(const char *path)
{
	(void) path;
	return (B_FALSE);
}

/*
 * Return B_TRUE if device is a device mapper or multipath device.
 * Return B_FALSE if not.
 */
boolean_t
zfs_dev_is_dm(const char *dev_name)
{
	(void) dev_name;
	return (B_FALSE);
}
