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
 * Copyright (c) 2013, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2016, 2017 Intel Corporation.
 * Copyright 2016 Igor Kozhukhov <ikozhukhov@gmail.com>.
 */

/*
 * Functions to convert between a list of vdevs and an nvlist representing the
 * configuration.  Each entry in the list can be one of:
 *
 * 	Device vdevs
 * 		disk=(path=..., devid=...)
 * 		file=(path=...)
 *
 * 	Group vdevs
 * 		raidz[1|2]=(...)
 * 		mirror=(...)
 *
 * 	Hot spares
 *
 * While the underlying implementation supports it, group vdevs cannot contain
 * other group vdevs.  All userland verification of devices is contained within
 * this file.  If successful, the nvlist returned can be passed directly to the
 * kernel; we've done as much verification as possible in userland.
 *
 * Hot spares are a special case, and passed down as an array of disk vdevs, at
 * the same level as the root of the vdev tree.
 *
 * The only function exported by this file is 'make_root_vdev'.  The
 * function performs several passes:
 *
 * 	1. Construct the vdev specification.  Performs syntax validation and
 *         makes sure each device is valid.
 * 	2. Check for devices in use.  Using libblkid to make sure that no
 *         devices are also in use.  Some can be overridden using the 'force'
 *         flag, others cannot.
 * 	3. Check for replication errors if the 'force' flag is not specified.
 *         validates that the replication level is consistent across the
 *         entire pool.
 * 	4. Call libzfs to label any whole disks with an EFI label.
 */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <libintl.h>
#include <libnvpair.h>
#include <libzutil.h>
#include <limits.h>
#include <sys/spa.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "zpool_util.h"
#include <sys/zfs_context.h>

#include <scsi/scsi.h>
#include <scsi/sg.h>
#include <sys/efi_partition.h>
#include <sys/stat.h>
#include <sys/vtoc.h>
#include <sys/mntent.h>
#include <uuid/uuid.h>
#include <blkid/blkid.h>

typedef struct vdev_disk_db_entry
{
	char id[24];
	int sector_size;
} vdev_disk_db_entry_t;

/*
 * Database of block devices that lie about physical sector sizes.  The
 * identification string must be precisely 24 characters to avoid false
 * negatives
 */
static vdev_disk_db_entry_t vdev_disk_database[] = {
	{"ATA     ADATA SSD S396 3", 8192},
	{"ATA     APPLE SSD SM128E", 8192},
	{"ATA     APPLE SSD SM256E", 8192},
	{"ATA     APPLE SSD SM512E", 8192},
	{"ATA     APPLE SSD SM768E", 8192},
	{"ATA     C400-MTFDDAC064M", 8192},
	{"ATA     C400-MTFDDAC128M", 8192},
	{"ATA     C400-MTFDDAC256M", 8192},
	{"ATA     C400-MTFDDAC512M", 8192},
	{"ATA     Corsair Force 3 ", 8192},
	{"ATA     Corsair Force GS", 8192},
	{"ATA     INTEL SSDSA2CT04", 8192},
	{"ATA     INTEL SSDSA2BZ10", 8192},
	{"ATA     INTEL SSDSA2BZ20", 8192},
	{"ATA     INTEL SSDSA2BZ30", 8192},
	{"ATA     INTEL SSDSA2CW04", 8192},
	{"ATA     INTEL SSDSA2CW08", 8192},
	{"ATA     INTEL SSDSA2CW12", 8192},
	{"ATA     INTEL SSDSA2CW16", 8192},
	{"ATA     INTEL SSDSA2CW30", 8192},
	{"ATA     INTEL SSDSA2CW60", 8192},
	{"ATA     INTEL SSDSC2CT06", 8192},
	{"ATA     INTEL SSDSC2CT12", 8192},
	{"ATA     INTEL SSDSC2CT18", 8192},
	{"ATA     INTEL SSDSC2CT24", 8192},
	{"ATA     INTEL SSDSC2CW06", 8192},
	{"ATA     INTEL SSDSC2CW12", 8192},
	{"ATA     INTEL SSDSC2CW18", 8192},
	{"ATA     INTEL SSDSC2CW24", 8192},
	{"ATA     INTEL SSDSC2CW48", 8192},
	{"ATA     KINGSTON SH100S3", 8192},
	{"ATA     KINGSTON SH103S3", 8192},
	{"ATA     M4-CT064M4SSD2  ", 8192},
	{"ATA     M4-CT128M4SSD2  ", 8192},
	{"ATA     M4-CT256M4SSD2  ", 8192},
	{"ATA     M4-CT512M4SSD2  ", 8192},
	{"ATA     OCZ-AGILITY2    ", 8192},
	{"ATA     OCZ-AGILITY3    ", 8192},
	{"ATA     OCZ-VERTEX2 3.5 ", 8192},
	{"ATA     OCZ-VERTEX3     ", 8192},
	{"ATA     OCZ-VERTEX3 LT  ", 8192},
	{"ATA     OCZ-VERTEX3 MI  ", 8192},
	{"ATA     OCZ-VERTEX4     ", 8192},
	{"ATA     SAMSUNG MZ7WD120", 8192},
	{"ATA     SAMSUNG MZ7WD240", 8192},
	{"ATA     SAMSUNG MZ7WD480", 8192},
	{"ATA     SAMSUNG MZ7WD960", 8192},
	{"ATA     SAMSUNG SSD 830 ", 8192},
	{"ATA     Samsung SSD 840 ", 8192},
	{"ATA     SanDisk SSD U100", 8192},
	{"ATA     TOSHIBA THNSNH06", 8192},
	{"ATA     TOSHIBA THNSNH12", 8192},
	{"ATA     TOSHIBA THNSNH25", 8192},
	{"ATA     TOSHIBA THNSNH51", 8192},
	{"ATA     APPLE SSD TS064C", 4096},
	{"ATA     APPLE SSD TS128C", 4096},
	{"ATA     APPLE SSD TS256C", 4096},
	{"ATA     APPLE SSD TS512C", 4096},
	{"ATA     INTEL SSDSA2M040", 4096},
	{"ATA     INTEL SSDSA2M080", 4096},
	{"ATA     INTEL SSDSA2M160", 4096},
	{"ATA     INTEL SSDSC2MH12", 4096},
	{"ATA     INTEL SSDSC2MH25", 4096},
	{"ATA     OCZ CORE_SSD    ", 4096},
	{"ATA     OCZ-VERTEX      ", 4096},
	{"ATA     SAMSUNG MCCOE32G", 4096},
	{"ATA     SAMSUNG MCCOE64G", 4096},
	{"ATA     SAMSUNG SSD PM80", 4096},
	/* Flash drives optimized for 4KB IOs on larger pages */
	{"ATA     INTEL SSDSC2BA10", 4096},
	{"ATA     INTEL SSDSC2BA20", 4096},
	{"ATA     INTEL SSDSC2BA40", 4096},
	{"ATA     INTEL SSDSC2BA80", 4096},
	{"ATA     INTEL SSDSC2BB08", 4096},
	{"ATA     INTEL SSDSC2BB12", 4096},
	{"ATA     INTEL SSDSC2BB16", 4096},
	{"ATA     INTEL SSDSC2BB24", 4096},
	{"ATA     INTEL SSDSC2BB30", 4096},
	{"ATA     INTEL SSDSC2BB40", 4096},
	{"ATA     INTEL SSDSC2BB48", 4096},
	{"ATA     INTEL SSDSC2BB60", 4096},
	{"ATA     INTEL SSDSC2BB80", 4096},
	{"ATA     INTEL SSDSC2BW24", 4096},
	{"ATA     INTEL SSDSC2BW48", 4096},
	{"ATA     INTEL SSDSC2BP24", 4096},
	{"ATA     INTEL SSDSC2BP48", 4096},
	{"NA      SmrtStorSDLKAE9W", 4096},
	{"NVMe    Amazon EC2 NVMe ", 4096},
	/* Imported from Open Solaris */
	{"ATA     MARVELL SD88SA02", 4096},
	/* Advanced format Hard drives */
	{"ATA     Hitachi HDS5C303", 4096},
	{"ATA     SAMSUNG HD204UI ", 4096},
	{"ATA     ST2000DL004 HD20", 4096},
	{"ATA     WDC WD10EARS-00M", 4096},
	{"ATA     WDC WD10EARS-00S", 4096},
	{"ATA     WDC WD10EARS-00Z", 4096},
	{"ATA     WDC WD15EARS-00M", 4096},
	{"ATA     WDC WD15EARS-00S", 4096},
	{"ATA     WDC WD15EARS-00Z", 4096},
	{"ATA     WDC WD20EARS-00M", 4096},
	{"ATA     WDC WD20EARS-00S", 4096},
	{"ATA     WDC WD20EARS-00Z", 4096},
	{"ATA     WDC WD1600BEVT-0", 4096},
	{"ATA     WDC WD2500BEVT-0", 4096},
	{"ATA     WDC WD3200BEVT-0", 4096},
	{"ATA     WDC WD5000BEVT-0", 4096},
};


#define	INQ_REPLY_LEN	96
#define	INQ_CMD_LEN	6

static const int vdev_disk_database_size =
	sizeof (vdev_disk_database) / sizeof (vdev_disk_database[0]);

boolean_t
check_sector_size_database(char *path, int *sector_size)
{
	unsigned char inq_buff[INQ_REPLY_LEN];
	unsigned char sense_buffer[32];
	unsigned char inq_cmd_blk[INQ_CMD_LEN] =
	    {INQUIRY, 0, 0, 0, INQ_REPLY_LEN, 0};
	sg_io_hdr_t io_hdr;
	int error;
	int fd;
	int i;

	/* Prepare INQUIRY command */
	memset(&io_hdr, 0, sizeof (sg_io_hdr_t));
	io_hdr.interface_id = 'S';
	io_hdr.cmd_len = sizeof (inq_cmd_blk);
	io_hdr.mx_sb_len = sizeof (sense_buffer);
	io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
	io_hdr.dxfer_len = INQ_REPLY_LEN;
	io_hdr.dxferp = inq_buff;
	io_hdr.cmdp = inq_cmd_blk;
	io_hdr.sbp = sense_buffer;
	io_hdr.timeout = 10;		/* 10 milliseconds is ample time */

	if ((fd = open(path, O_RDONLY|O_DIRECT)) < 0)
		return (B_FALSE);

	error = ioctl(fd, SG_IO, (unsigned long) &io_hdr);

	(void) close(fd);

	if (error < 0)
		return (B_FALSE);

	if ((io_hdr.info & SG_INFO_OK_MASK) != SG_INFO_OK)
		return (B_FALSE);

	for (i = 0; i < vdev_disk_database_size; i++) {
		if (memcmp(inq_buff + 8, vdev_disk_database[i].id, 24))
			continue;

		*sector_size = vdev_disk_database[i].sector_size;
		return (B_TRUE);
	}

	return (B_FALSE);
}

static int
check_slice(const char *path, blkid_cache cache, int force, boolean_t isspare)
{
	int err;
	char *value;

	/* No valid type detected device is safe to use */
	value = blkid_get_tag_value(cache, "TYPE", path);
	if (value == NULL)
		return (0);

	/*
	 * If libblkid detects a ZFS device, we check the device
	 * using check_file() to see if it's safe.  The one safe
	 * case is a spare device shared between multiple pools.
	 */
	if (strcmp(value, "zfs_member") == 0) {
		err = check_file(path, force, isspare);
	} else {
		if (force) {
			err = 0;
		} else {
			err = -1;
			vdev_error(gettext("%s contains a filesystem of "
			    "type '%s'\n"), path, value);
		}
	}

	free(value);

	return (err);
}

/*
 * Validate that a disk including all partitions are safe to use.
 *
 * For EFI labeled disks this can done relatively easily with the libefi
 * library.  The partition numbers are extracted from the label and used
 * to generate the expected /dev/ paths.  Each partition can then be
 * checked for conflicts.
 *
 * For non-EFI labeled disks (MBR/EBR/etc) the same process is possible
 * but due to the lack of a readily available libraries this scanning is
 * not implemented.  Instead only the device path as given is checked.
 */
static int
check_disk(const char *path, blkid_cache cache, int force,
    boolean_t isspare, boolean_t iswholedisk)
{
	struct dk_gpt *vtoc;
	char slice_path[MAXPATHLEN];
	int err = 0;
	int fd, i;
	int flags = O_RDONLY|O_DIRECT;

	if (!iswholedisk)
		return (check_slice(path, cache, force, isspare));

	/* only spares can be shared, other devices require exclusive access */
	if (!isspare)
		flags |= O_EXCL;

	if ((fd = open(path, flags)) < 0) {
		char *value = blkid_get_tag_value(cache, "TYPE", path);
		(void) fprintf(stderr, gettext("%s is in use and contains "
		    "a %s filesystem.\n"), path, value ? value : "unknown");
		free(value);
		return (-1);
	}

	/*
	 * Expected to fail for non-EFI labeled disks.  Just check the device
	 * as given and do not attempt to detect and scan partitions.
	 */
	err = efi_alloc_and_read(fd, &vtoc);
	if (err) {
		(void) close(fd);
		return (check_slice(path, cache, force, isspare));
	}

	/*
	 * The primary efi partition label is damaged however the secondary
	 * label at the end of the device is intact.  Rather than use this
	 * label we should play it safe and treat this as a non efi device.
	 */
	if (vtoc->efi_flags & EFI_GPT_PRIMARY_CORRUPT) {
		efi_free(vtoc);
		(void) close(fd);

		if (force) {
			/* Partitions will now be created using the backup */
			return (0);
		} else {
			vdev_error(gettext("%s contains a corrupt primary "
			    "EFI label.\n"), path);
			return (-1);
		}
	}

	for (i = 0; i < vtoc->efi_nparts; i++) {

		if (vtoc->efi_parts[i].p_tag == V_UNASSIGNED ||
		    uuid_is_null((uchar_t *)&vtoc->efi_parts[i].p_guid))
			continue;

		if (strncmp(path, UDISK_ROOT, strlen(UDISK_ROOT)) == 0)
			(void) snprintf(slice_path, sizeof (slice_path),
			    "%s%s%d", path, "-part", i+1);
		else
			(void) snprintf(slice_path, sizeof (slice_path),
			    "%s%s%d", path, isdigit(path[strlen(path)-1]) ?
			    "p" : "", i+1);

		err = check_slice(slice_path, cache, force, isspare);
		if (err)
			break;
	}

	efi_free(vtoc);
	(void) close(fd);

	return (err);
}

int
check_device(const char *path, boolean_t force,
    boolean_t isspare, boolean_t iswholedisk)
{
	blkid_cache cache;
	int error;

	error = blkid_get_cache(&cache, NULL);
	if (error != 0) {
		(void) fprintf(stderr, gettext("unable to access the blkid "
		    "cache.\n"));
		return (-1);
	}

	error = check_disk(path, cache, force, isspare, iswholedisk);
	blkid_put_cache(cache);

	return (error);
}

void
after_zpool_upgrade(zpool_handle_t *zhp)
{
}
