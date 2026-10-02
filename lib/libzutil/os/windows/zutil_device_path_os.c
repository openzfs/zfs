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
 * Portions Copyright 2022 Andrew Innes <andrew.c12@gmail.com>
 */

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/efi_partition.h>

#include <libzutil.h>

#define	_WIN32_MEAN_AND_LEAN
#include <Windows.h>
#include <winioctl.h>

#include <wosix.h>

/*
 * We don't strip/append partitions on FreeBSD.
 */

/*
 * Note: The caller must free the returned string.
 */
char *
zfs_strip_partition(const char *dev)
{
	unsigned int disk, slice;
	char *partless;

	partless = strdup(dev);

	/* Ends with "diskNsP" - where 'N' and 'P' are integers - strip sP */
	if (sscanf(partless, "disk%us%u", &disk, &slice) == 2) {
		char *r;
		r = strrchr(partless, 's');
		if (r != NULL)
			*r = 0;
	}

	return (partless);
}

/*
 * When given PHYSICALDRIVE1 and we partition it for ZFS
 * change the name to the partition, ie
 * #offset#size#PHYSICALDRIVE1
 */
int
zfs_append_partition(char *path, size_t max_len)
{
	int len = strlen(path);
	int fd;
	struct dk_gpt *vtoc;

	// Already expanded, nothing to do
	if (path[0] == '#')
		return (len);

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return (len);

	if ((efi_alloc_and_read(fd, &vtoc)) == 0) {
		for (int i = 0; i < vtoc->efi_nparts; i++) {

			if (vtoc->efi_parts[i].p_start == 0 &&
			    vtoc->efi_parts[i].p_size == 0)
				continue;

			if (tolower(vtoc->efi_parts[i].p_name[0]) == 'z' &&
			    tolower(vtoc->efi_parts[i].p_name[1]) == 'f' &&
			    tolower(vtoc->efi_parts[i].p_name[2]) == 's') {
				size_t length =
				    vtoc->efi_parts[i].p_size *
				    vtoc->efi_lbasize;
				off_t  offset =
				    vtoc->efi_parts[i].p_start *
				    vtoc->efi_lbasize;
				char *copypath = strdup(path);
				snprintf(path, max_len, "#%llu#%llu#%s",
				    offset, length, copypath);
				free(copypath);
				len = strlen(path);
				break;
			}

		}
	} else {
		// If we can't read the partition, we are most likely
		// creating a pool, and it will be slice 1, alas, we
		// do not know the size/offset here, yet.
		// which is why we call this function again after
		// zpool_label_disk.
	}
	close(fd);
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
	if (r != NULL)
		return (&r[1]);
	r = strrchr(path, '\\');
	if (r != NULL)
		return (&r[1]);
	return (path);
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
	struct dk_gpt *label = NULL;
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
 * Wait up to timeout_ms for the disk to become accessible after OnlineDisk().
 *
 * On Windows the semantic is different from Linux: we are not waiting for a
 * udev-created partition device node.  Instead we wait for the physical disk
 * handle to be openable, which means OnlineDisk() has completed.  Windows
 * GPT partition enumeration (IOCTL_DISK_GET_DRIVE_LAYOUT_EX) is NOT required
 * — ZFS accesses partitions via the "#offset#size#device" encoding, which
 * works directly against the physical disk handle regardless of whether
 * Windows has created separate partition device objects.
 *
 * For "#offset#size#device" paths the parent disk is always accessible, so
 * we return success immediately.
 */
int
zpool_label_disk_wait(const char *path, int timeout_ms)
{
	const char *devpath = path;
	hrtime_t start;

	/*
	 * An encoded "#offset#size#device" path accesses the partition via
	 * the parent disk handle; no separate Windows partition object needed.
	 */
	if (devpath[0] == '#')
		return (0);

	start = gethrtime();

	do {
		HANDLE h = CreateFile(devpath, GENERIC_READ,
		    FILE_SHARE_READ | FILE_SHARE_WRITE,
		    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (h != INVALID_HANDLE_VALUE) {
			CloseHandle(h);
			return (0);
		}
		usleep(50 * MILLISEC);
	} while (NSEC2MSEC(gethrtime() - start) < timeout_ms);

	return (ENODEV);
}


boolean_t
is_mpath_whole_disk(const char *path)
{
	// Return TRUE here to have make_disks() call
	// update_vdev_config_dev_strs()
	return (B_TRUE);
}

/*
 * Return B_TRUE if device is a device mapper or multipath device.
 * Return B_FALSE if not.
 */
boolean_t
zfs_dev_is_dm(const char *dev_name)
{
	return (B_FALSE);
}
