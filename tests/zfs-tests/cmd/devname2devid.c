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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2016, Intel Corporation.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <libudev.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

/*
 * Linux persistent device strings for vdev labels
 *
 * based on udev_device_get_devid() at zfs/lib/libzfs/libzfs_import.c
 */

#define	DEV_BYID_PATH	"/dev/disk/by-id/"

static int
udev_device_get_devid(struct udev_device *dev, char *bufptr, size_t buflen)
{
	struct udev_list_entry *entry;
	const char *bus;
	char devbyid[MAXPATHLEN];

	/* The bus based by-id path is preferred */
	bus = udev_device_get_property_value(dev, "ID_BUS");

	if (bus == NULL) {
		const char *dm_uuid;

		/*
		 * For multipath nodes use the persistent uuid based identifier
		 *
		 * Example: 'dm-uuid-mpath-35000c5006304de3f'
		 */
		dm_uuid = udev_device_get_property_value(dev, "DM_UUID");
		if (dm_uuid != NULL) {
			(void) snprintf(bufptr, buflen, "dm-uuid-%s", dm_uuid);
			return (0);
		}
		return (ENODATA);
	}

	/*
	 * locate the bus specific by-id link
	 *
	 * Example: 'scsi-MG03SCA300_350000494a8cb3d67-part1'
	 */
	(void) snprintf(devbyid, sizeof (devbyid), "%s%s-", DEV_BYID_PATH, bus);
	entry = udev_device_get_devlinks_list_entry(dev);
	while (entry != NULL) {
		const char *name;

		name = udev_list_entry_get_name(entry);
		if (strncmp(name, devbyid, strlen(devbyid)) == 0) {
			name += strlen(DEV_BYID_PATH);
			(void) stpncpy(bufptr, name, buflen - 1);
			bufptr[buflen - 1] = '\0';
			return (0);
		}
		entry = udev_list_entry_get_next(entry);
	}

	return (ENODATA);
}

/*
 * Usage: devname2devid <devicepath>
 *
 * Examples:
 * # ./devname2devid /dev/sda1
 * devid scsi-350000394a8caede4-part1
 *
 * # ./devname2devid /dev/dm-1
 * devid: 'dm-uuid-mpath-35000c5006304de3f'
 *
 * This program accepts a disk or disk slice path and prints a
 * device id.
 *
 * Exit values:
 *	0 - means success
 *	1 - means failure
 *
 */
int
main(int argc, char *argv[])
{
	struct udev *udev;
	struct udev_device *dev = NULL;
	char devid[128], nodepath[MAXPATHLEN];
	char *device, *sysname;
	int ret;

	if (argc == 1) {
		(void) printf("%s <devicepath> [search path]\n", argv[0]);
		exit(1);
	}
	device = argv[1];

	if ((udev = udev_new()) == NULL) {
		perror("udev_new");
		exit(1);
	}

	/* resolve path to a runtime device node instance */
	if (realpath(device, nodepath) == NULL) {
		perror("realpath");
		exit(1);
	}
	sysname = strrchr(nodepath, '/') + 1;

	if ((dev = udev_device_new_from_subsystem_sysname(udev, "block",
	    sysname)) == NULL) {
		perror(sysname);
		exit(1);
	}

	if ((ret = udev_device_get_devid(dev, devid, sizeof (devid))) != 0) {
		udev_device_unref(dev);
		errno = ret;
		perror(sysname);
		exit(1);
	}

	(void) printf("devid %s\n", devid);

	udev_device_unref(dev);
	udev_unref(udev);

	return (0);
}
