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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/efi_partition.h>

#ifdef HAVE_LIBUDEV
#include <libudev.h>
#endif

#include <libzutil.h>

/*
 * Append partition suffix to an otherwise fully qualified device path.
 * This is used to generate the name the full path as its stored in
 * ZPOOL_CONFIG_PATH for whole disk devices.  On success the new length
 * of 'path' will be returned on error a negative value is returned.
 */
int
zfs_append_partition(char *path, size_t max_len)
{
	int len = strlen(path);

	if ((strncmp(path, UDISK_ROOT, strlen(UDISK_ROOT)) == 0) ||
	    (strncmp(path, ZVOL_ROOT, strlen(ZVOL_ROOT)) == 0)) {
		if (len + 6 >= max_len)
			return (-1);

		(void) strcat(path, "-part1");
		len += 6;
	} else {
		if (len + 2 >= max_len)
			return (-1);

		if (isdigit(path[len-1])) {
			(void) strcat(path, "p1");
			len += 2;
		} else {
			(void) strcat(path, "1");
			len += 1;
		}
	}

	return (len);
}

/*
 * Remove partition suffix from a vdev path.  Partition suffixes may take three
 * forms: "-partX", "pX", or "X", where X is a string of digits.  The second
 * case only occurs when the suffix is preceded by a digit, i.e. "md0p0" The
 * third case only occurs when preceded by a string matching the regular
 * expression "^([hsv]|xv)d[a-z]+", i.e. a scsi, ide, virtio or xen disk.
 *
 * caller must free the returned string
 */
char *
zfs_strip_partition(char *path)
{
	char *tmp = strdup(path);
	char *part = NULL, *d = NULL;
	if (!tmp)
		return (NULL);

	if ((part = strstr(tmp, "-part")) && part != tmp) {
		d = part + 5;
	} else if ((part = strrchr(tmp, 'p')) &&
	    part > tmp + 1 && isdigit(*(part-1))) {
		d = part + 1;
	} else if ((tmp[0] == 'h' || tmp[0] == 's' || tmp[0] == 'v') &&
	    tmp[1] == 'd') {
		for (d = &tmp[2]; isalpha(*d); part = ++d) { }
	} else if (strncmp("xvd", tmp, 3) == 0) {
		for (d = &tmp[3]; isalpha(*d); part = ++d) { }
	}
	if (part && d && *d != '\0') {
		for (; isdigit(*d); d++) { }
		if (*d == '\0')
			*part = '\0';
	}

	return (tmp);
}

/*
 * Same as zfs_strip_partition, but allows "/dev/" to be in the pathname
 *
 * path:	/dev/sda1
 * returns:	/dev/sda
 *
 * Returned string must be freed.
 */
static char *
zfs_strip_partition_path(char *path)
{
	char *newpath = strdup(path);
	char *sd_offset;
	char *new_sd;

	if (!newpath)
		return (NULL);

	/* Point to "sda1" part of "/dev/sda1" */
	sd_offset = strrchr(newpath, '/') + 1;

	/* Get our new name "sda" */
	new_sd = zfs_strip_partition(sd_offset);
	if (!new_sd) {
		free(newpath);
		return (NULL);
	}

	/* Paste the "sda" where "sda1" was */
	strlcpy(sd_offset, new_sd, strlen(sd_offset) + 1);

	/* Free temporary "sda" */
	free(new_sd);

	return (newpath);
}

/*
 * Strip the unwanted portion of a device path.
 */
char *
zfs_strip_path(char *path)
{
	return (strrchr(path, '/') + 1);
}

/*
 * Allocate and return the underlying device name for a device mapper device.
 * If a device mapper device maps to multiple devices, return the first device.
 *
 * For example, dm_name = "/dev/dm-0" could return "/dev/sda". Symlinks to a
 * DM device (like /dev/disk/by-vdev/A0) are also allowed.
 *
 * Returns device name, or NULL on error or no match.  If dm_name is not a DM
 * device then return NULL.
 *
 * NOTE: The returned name string must be *freed*.
 */
static char *
dm_get_underlying_path(const char *dm_name)
{
	DIR *dp = NULL;
	struct dirent *ep;
	char *realp;
	char *tmp = NULL;
	char *path = NULL;
	char *dev_str;
	int size;

	if (dm_name == NULL)
		return (NULL);

	/* dm name may be a symlink (like /dev/disk/by-vdev/A0) */
	realp = realpath(dm_name, NULL);
	if (realp == NULL)
		return (NULL);

	/*
	 * If they preface 'dev' with a path (like "/dev") then strip it off.
	 * We just want the 'dm-N' part.
	 */
	tmp = strrchr(realp, '/');
	if (tmp != NULL)
		dev_str = tmp + 1;    /* +1 since we want the chr after '/' */
	else
		dev_str = tmp;

	size = asprintf(&tmp, "/sys/block/%s/slaves/", dev_str);
	if (size == -1 || !tmp)
		goto end;

	dp = opendir(tmp);
	if (dp == NULL)
		goto end;

	/*
	 * Return first entry (that isn't itself a directory) in the
	 * directory containing device-mapper dependent (underlying)
	 * devices.
	 */
	while ((ep = readdir(dp))) {
		if (ep->d_type != DT_DIR) {	/* skip "." and ".." dirs */
			size = asprintf(&path, "/dev/%s", ep->d_name);
			break;
		}
	}

end:
	if (dp != NULL)
		closedir(dp);
	free(tmp);
	free(realp);
	return (path);
}

/*
 * Return B_TRUE if device is a device mapper or multipath device.
 * Return B_FALSE if not.
 */
boolean_t
zfs_dev_is_dm(const char *dev_name)
{

	char *tmp;
	tmp = dm_get_underlying_path(dev_name);
	if (tmp == NULL)
		return (B_FALSE);

	free(tmp);
	return (B_TRUE);
}

/*
 * By "whole disk" we mean an entire physical disk (something we can
 * label, toggle the write cache on, etc.) as opposed to the full
 * capacity of a pseudo-device such as lofi or did.  We act as if we
 * are labeling the disk, which should be a pretty good test of whether
 * it's a viable device or not.  Returns B_TRUE if it is and B_FALSE if
 * it isn't.
 */
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
 * Lookup the underlying device for a device name
 *
 * Often you'll have a symlink to a device, a partition device,
 * or a multipath device, and want to look up the underlying device.
 * This function returns the underlying device name.  If the device
 * name is already the underlying device, then just return the same
 * name.  If the device is a DM device with multiple underlying devices
 * then return the first one.
 *
 * For example:
 *
 * 1. /dev/disk/by-id/ata-QEMU_HARDDISK_QM00001 -> ../../sda
 * dev_name:	/dev/disk/by-id/ata-QEMU_HARDDISK_QM00001
 * returns:	/dev/sda
 *
 * 2. /dev/mapper/mpatha (made up of /dev/sda and /dev/sdb)
 * dev_name:	/dev/mapper/mpatha
 * returns:	/dev/sda (first device)
 *
 * 3. /dev/sda (already the underlying device)
 * dev_name:	/dev/sda
 * returns:	/dev/sda
 *
 * 4. /dev/dm-3 (mapped to /dev/sda)
 * dev_name:	/dev/dm-3
 * returns:	/dev/sda
 *
 * 5. /dev/disk/by-id/scsi-0QEMU_drive-scsi0-0-0-0-part9 -> ../../sdb9
 * dev_name:	/dev/disk/by-id/scsi-0QEMU_drive-scsi0-0-0-0-part9
 * returns:	/dev/sdb
 *
 * 6. /dev/disk/by-uuid/5df030cf-3cd9-46e4-8e99-3ccb462a4e9a -> ../dev/sda2
 * dev_name:	/dev/disk/by-uuid/5df030cf-3cd9-46e4-8e99-3ccb462a4e9a
 * returns:	/dev/sda
 *
 * Returns underlying device name, or NULL on error or no match.
 *
 * NOTE: The returned name string must be *freed*.
 */
char *
zfs_get_underlying_path(const char *dev_name)
{
	char *name = NULL;
	char *tmp;

	if (dev_name == NULL)
		return (NULL);

	tmp = dm_get_underlying_path(dev_name);

	/* dev_name not a DM device, so just un-symlinkize it */
	if (tmp == NULL)
		tmp = realpath(dev_name, NULL);

	if (tmp != NULL) {
		name = zfs_strip_partition_path(tmp);
		free(tmp);
	}

	return (name);
}

/*
 * Given a dev name like "sda", return the full enclosure sysfs path to
 * the disk.  You can also pass in the name with "/dev" prepended
 * to it (like /dev/sda).
 *
 * For example, disk "sda" in enclosure slot 1:
 *     dev:            "sda"
 *     returns:        "/sys/class/enclosure/1:0:3:0/Slot 1"
 *
 * 'dev' must be a non-devicemapper device.
 *
 * Returned string must be freed.
 */
char *
zfs_get_enclosure_sysfs_path(const char *dev_name)
{
	DIR *dp = NULL;
	struct dirent *ep;
	char buf[MAXPATHLEN];
	char *tmp1 = NULL;
	char *tmp2 = NULL;
	char *tmp3 = NULL;
	char *path = NULL;
	size_t size;
	int tmpsize;

	if (dev_name == NULL)
		return (NULL);

	/* If they preface 'dev' with a path (like "/dev") then strip it off */
	tmp1 = strrchr(dev_name, '/');
	if (tmp1 != NULL)
		dev_name = tmp1 + 1;    /* +1 since we want the chr after '/' */

	tmpsize = asprintf(&tmp1, "/sys/block/%s/device", dev_name);
	if (tmpsize == -1 || tmp1 == NULL) {
		tmp1 = NULL;
		goto end;
	}

	dp = opendir(tmp1);
	if (dp == NULL) {
		tmp1 = NULL;	/* To make free() at the end a NOP */
		goto end;
	}

	/*
	 * Look though all sysfs entries in /sys/block/<dev>/device for
	 * the enclosure symlink.
	 */
	while ((ep = readdir(dp))) {
		/* Ignore everything that's not our enclosure_device link */
		if (strstr(ep->d_name, "enclosure_device") == NULL)
			continue;

		if (asprintf(&tmp2, "%s/%s", tmp1, ep->d_name) == -1 ||
		    tmp2 == NULL)
			break;

		size = readlink(tmp2, buf, sizeof (buf));

		/* Did readlink fail or crop the link name? */
		if (size == -1 || size >= sizeof (buf)) {
			free(tmp2);
			tmp2 = NULL;	/* To make free() at the end a NOP */
			break;
		}

		/*
		 * We got a valid link.  readlink() doesn't terminate strings
		 * so we have to do it.
		 */
		buf[size] = '\0';

		/*
		 * Our link will look like:
		 *
		 * "../../../../port-11:1:2/..STUFF../enclosure/1:0:3:0/SLOT 1"
		 *
		 * We want to grab the "enclosure/1:0:3:0/SLOT 1" part
		 */
		tmp3 = strstr(buf, "enclosure");
		if (tmp3 == NULL)
			break;

		if (asprintf(&path, "/sys/class/%s", tmp3) == -1) {
			/* If asprintf() fails, 'path' is undefined */
			path = NULL;
			break;
		}

		if (path == NULL)
			break;
	}

end:
	free(tmp2);
	free(tmp1);

	if (dp != NULL)
		closedir(dp);

	return (path);
}

#ifdef HAVE_LIBUDEV

/*
 * A disk is considered a multipath whole disk when:
 *	DEVNAME key value has "dm-"
 *	DM_NAME key value has "mpath" prefix
 *	DM_UUID key exists
 *	ID_PART_TABLE_TYPE key does not exist or is not gpt
 */
static boolean_t
udev_mpath_whole_disk(struct udev_device *dev)
{
	const char *devname, *type, *uuid;

	devname = udev_device_get_property_value(dev, "DEVNAME");
	type = udev_device_get_property_value(dev, "ID_PART_TABLE_TYPE");
	uuid = udev_device_get_property_value(dev, "DM_UUID");

	if ((devname != NULL && strncmp(devname, "/dev/dm-", 8) == 0) &&
	    ((type == NULL) || (strcmp(type, "gpt") != 0)) &&
	    (uuid != NULL)) {
		return (B_TRUE);
	}

	return (B_FALSE);
}

/*
 * Check if a disk is effectively a multipath whole disk
 */
boolean_t
is_mpath_whole_disk(const char *path)
{
	struct udev *udev;
	struct udev_device *dev = NULL;
	char nodepath[MAXPATHLEN];
	char *sysname;
	boolean_t wholedisk = B_FALSE;

	if (realpath(path, nodepath) == NULL)
		return (B_FALSE);
	sysname = strrchr(nodepath, '/') + 1;
	if (strncmp(sysname, "dm-", 3) != 0)
		return (B_FALSE);
	if ((udev = udev_new()) == NULL)
		return (B_FALSE);
	if ((dev = udev_device_new_from_subsystem_sysname(udev, "block",
	    sysname)) == NULL) {
		udev_device_unref(dev);
		return (B_FALSE);
	}

	wholedisk = udev_mpath_whole_disk(dev);

	udev_device_unref(dev);
	return (wholedisk);
}

#else /* HAVE_LIBUDEV */

/* ARGSUSED */
boolean_t
is_mpath_whole_disk(const char *path)
{
	return (B_FALSE);
}

#endif /* HAVE_LIBUDEV */
