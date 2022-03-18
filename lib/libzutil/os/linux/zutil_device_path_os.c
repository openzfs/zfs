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
 * Read the contents of a sysfs file into an allocated buffer and remove the
 * last newline.
 *
 * This is useful for reading sysfs files that return a single string.  Return
 * an allocated string pointer on success, NULL otherwise.  Returned buffer
 * must be freed by the user.
 */
static char *
zfs_read_sysfs_file(char *filepath)
{
	char buf[4096];	/* all sysfs files report 4k size */
	char *str = NULL;

	FILE *fp = fopen(filepath, "r");
	if (fp == NULL) {
		return (NULL);
	}
	if (fgets(buf, sizeof (buf), fp) == buf) {
		/* success */

		/* Remove the last newline (if any) */
		size_t len = strlen(buf);
		if (buf[len - 1] == '\n') {
			buf[len - 1] = '\0';
		}
		str = strdup(buf);
	}

	fclose(fp);

	return (str);
}

/*
 * Given a dev name like "nvme0n1", return the full PCI slot sysfs path to
 * the drive (in /sys/bus/pci/slots).
 *
 * For example:
 *     dev:            "nvme0n1"
 *     returns:        "/sys/bus/pci/slots/0"
 *
 * 'dev' must be an NVMe device.
 *
 * Returned string must be freed.  Returns NULL on error or no sysfs path.
 */
static char *
zfs_get_pci_slots_sys_path(const char *dev_name)
{
	DIR *dp = NULL;
	struct dirent *ep;
	char *address1 = NULL;
	char *address2 = NULL;
	char *path = NULL;
	char buf[MAXPATHLEN];
	char *tmp;

	/* If they preface 'dev' with a path (like "/dev") then strip it off */
	tmp = strrchr(dev_name, '/');
	if (tmp != NULL)
		dev_name = tmp + 1;    /* +1 since we want the chr after '/' */

	if (strncmp("nvme", dev_name, 4) != 0)
		return (NULL);

	(void) snprintf(buf, sizeof (buf), "/sys/block/%s/device/address",
	    dev_name);

	address1 = zfs_read_sysfs_file(buf);
	if (!address1)
		return (NULL);

	/*
	 * /sys/block/nvme0n1/device/address format will
	 * be "0000:01:00.0" while /sys/bus/pci/slots/0/address will be
	 * "0000:01:00".  Just NULL terminate at the '.' so they match.
	 */
	tmp = strrchr(address1, '.');
	if (tmp != NULL)
		*tmp = '\0';

	dp = opendir("/sys/bus/pci/slots/");
	if (dp == NULL) {
		free(address1);
		return (NULL);
	}

	/*
	 * Look through all the /sys/bus/pci/slots/ subdirs
	 */
	while ((ep = readdir(dp))) {
		/*
		 * We only care about directory names that are a single number.
		 * Sometimes there's other directories like
		 * "/sys/bus/pci/slots/0-3/" in there - skip those.
		 */
		if (!zfs_isnumber(ep->d_name))
			continue;

		(void) snprintf(buf, sizeof (buf),
		    "/sys/bus/pci/slots/%s/address", ep->d_name);

		address2 = zfs_read_sysfs_file(buf);
		if (!address2)
			continue;

		if (strcmp(address1, address2) == 0) {
			/* Addresses match, we're all done */
			free(address2);
			if (asprintf(&path, "/sys/bus/pci/slots/%s",
			    ep->d_name) == -1) {
				free(tmp);
				continue;
			}
			break;
		}
		free(address2);
	}

	closedir(dp);
	free(address1);

	return (path);
}

/*
 * Given a dev name like "sda", return the full enclosure sysfs path to
 * the disk.  You can also pass in the name with "/dev" prepended
 * to it (like /dev/sda).  This works for both JBODs and NVMe PCI devices.
 *
 * For example, disk "sda" in enclosure slot 1:
 *     dev_name:       "sda"
 *     returns:        "/sys/class/enclosure/1:0:3:0/Slot 1"
 *
 * Or:
 *
 *      dev_name:   "nvme0n1"
 *      returns:    "/sys/bus/pci/slots/0"
 *
 * 'dev' must be a non-devicemapper device.
 *
 * Returned string must be freed.  Returns NULL on error.
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
	if (dp == NULL)
		goto end;

	/*
	 * Look though all sysfs entries in /sys/block/<dev>/device for
	 * the enclosure symlink.
	 */
	while ((ep = readdir(dp))) {
		/* Ignore everything that's not our enclosure_device link */
		if (strstr(ep->d_name, "enclosure_device") == NULL)
			continue;

		if (asprintf(&tmp2, "%s/%s", tmp1, ep->d_name) == -1) {
			tmp2 = NULL;
			break;
		}

		size = readlink(tmp2, buf, sizeof (buf));

		/* Did readlink fail or crop the link name? */
		if (size == -1 || size >= sizeof (buf))
			break;

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

	if (!path) {
		/*
		 * This particular disk isn't in a JBOD.  It could be an NVMe
		 * drive. If so, look up the NVMe device's path in
		 * /sys/bus/pci/slots/. Within that directory is a 'attention'
		 * file which controls the NVMe fault LED.
		 */
		path = zfs_get_pci_slots_sys_path(dev_name);
	}

	return (path);
}

/*
 * Allocate and return the underlying device name for a device mapper device.
 *
 * For example, dm_name = "/dev/dm-0" could return "/dev/sda". Symlinks to a
 * DM device (like /dev/disk/by-vdev/A0) are also allowed.
 *
 * If the DM device has multiple underlying devices (like with multipath
 * DM devices), then favor underlying devices that have a symlink back to their
 * back to their enclosure device in sysfs.  This will be useful for the
 * zedlet scripts that toggle the fault LED.
 *
 * Returns an underlying device name, or NULL on error or no match.  If dm_name
 * is not a DM device then return NULL.
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
	char *first_path = NULL;
	char *enclosure_path;

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

	if ((size = asprintf(&tmp, "/sys/block/%s/slaves/", dev_str)) == -1) {
		tmp = NULL;
		goto end;
	}

	dp = opendir(tmp);
	if (dp == NULL)
		goto end;

	/*
	 * A device-mapper device can have multiple paths to it (multipath).
	 * Favor paths that have a symlink back to their enclosure device.
	 * We have to do this since some enclosures may only provide a symlink
	 * back for one underlying path to a disk and not the other.
	 *
	 * If no paths have links back to their enclosure, then just return the
	 * first path.
	 */
	while ((ep = readdir(dp))) {
		if (ep->d_type != DT_DIR) {	/* skip "." and ".." dirs */
			if (!first_path)
				first_path = strdup(ep->d_name);

			enclosure_path =
			    zfs_get_enclosure_sysfs_path(ep->d_name);

			if (!enclosure_path)
				continue;

			if ((size = asprintf(
			    &path, "/dev/%s", ep->d_name)) == -1)
				path = NULL;
			free(enclosure_path);
			break;
		}
	}

end:
	if (dp != NULL)
		closedir(dp);
	free(tmp);
	free(realp);

	if (!path && first_path) {
		/*
		 * None of the underlying paths had a link back to their
		 * enclosure devices.  Throw up out hands and return the first
		 * underlying path.
		 */
		if ((size = asprintf(&path, "/dev/%s", first_path)) == -1)
			path = NULL;
	}

	free(first_path);
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
	struct dk_gpt *label = NULL;
	int fd;

	if ((fd = open(dev_name, O_RDONLY | O_DIRECT | O_CLOEXEC)) < 0)
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


#ifdef HAVE_LIBUDEV

/*
 * A disk is considered a multipath whole disk when:
 *	DEVNAME key value has "dm-"
 *	DM_UUID key exists and starts with 'mpath-'
 *	ID_PART_TABLE_TYPE key does not exist or is not gpt
 *	ID_FS_LABEL key does not exist (disk isn't labeled)
 */
static boolean_t
is_mpath_udev_sane(struct udev_device *dev)
{
	const char *devname, *type, *uuid, *label;

	devname = udev_device_get_property_value(dev, "DEVNAME");
	type = udev_device_get_property_value(dev, "ID_PART_TABLE_TYPE");
	uuid = udev_device_get_property_value(dev, "DM_UUID");
	label = udev_device_get_property_value(dev, "ID_FS_LABEL");

	if ((devname != NULL && strncmp(devname, "/dev/dm-", 8) == 0) &&
	    ((type == NULL) || (strcmp(type, "gpt") != 0)) &&
	    ((uuid != NULL) && (strncmp(uuid, "mpath-", 6) == 0)) &&
	    (label == NULL)) {
		return (B_TRUE);
	}

	return (B_FALSE);
}

/*
 * Check if a disk is a multipath "blank" disk:
 *
 * 1. The disk has udev values that suggest it's a multipath disk
 * 2. The disk is not currently labeled with a filesystem of any type
 * 3. There are no partitions on the disk
 */
boolean_t
is_mpath_whole_disk(const char *path)
{
	struct udev *udev;
	struct udev_device *dev = NULL;
	char nodepath[MAXPATHLEN];
	char *sysname;

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

	/* Sanity check some udev values */
	boolean_t is_sane = is_mpath_udev_sane(dev);
	udev_device_unref(dev);

	return (is_sane);
}

#else /* HAVE_LIBUDEV */

/* ARGSUSED */
boolean_t
is_mpath_whole_disk(const char *path)
{
	return (B_FALSE);
}

#endif /* HAVE_LIBUDEV */
