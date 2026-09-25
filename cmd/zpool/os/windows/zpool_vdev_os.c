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

#include <sys/efi_partition.h>
#include <sys/stat.h>
#include <sys/mntent.h>
#include <uuid/uuid.h>

boolean_t
check_sector_size_database(char *path, int *sector_size)
{
	return (B_FALSE);
}

static void
check_error(int err)
{
	/*
	 * ENXIO/ENODEV is a valid error message if the device doesn't live in
	 * /dev/dsk.  Don't bother printing an error message in this case.
	 */
	if (err == ENXIO || err == ENODEV)
		return;

	(void) fprintf(stderr, gettext("warning: device in use checking "
	    "failed: %s\n"), strerror(err));
}

static int
check_slice(const char *path, int force, boolean_t isspare)
{
	int flags = O_RDONLY | O_DIRECT;
	int fd;
	int err = 0;
	DWORD bytesReturned;
	BOOL result;

	// If it starts with # it is a EFI partition we handle differently.
	// Since we can't lock that, we call check_file() to see if it
	// is a ZFS partition.
	if (path[0] == '#')
		return (check_file(path, force, isspare));

	if ((fd = open(path, flags)) < 0) {
		fprintf(stderr, "%s: failed to open %s\n", __func__, path);
		return (0);
	}

	result = DeviceIoControl(
	    fd,
	    FSCTL_LOCK_VOLUME,
	    NULL, 0,
	    NULL, 0,
	    &bytesReturned, NULL);

	if (!result) {
		DWORD dwError = GetLastError();
		if (dwError == ERROR_ACCESS_DENIED) {
			printf("Volume is already in use "
			    "(locked by another process or OS).\n");
		} else {
			printf("Failed to lock volume: %lu\n", dwError);
		}
		(void) fprintf(stderr,
		    gettext("Slice %s appears in use.\n"), path);

		err = force ? 0 : EBUSY;
	} else {
		printf("Volume locked successfully. (So available for use)\n");
		result = DeviceIoControl(
		    fd,
		    FSCTL_UNLOCK_VOLUME,
		    NULL, 0,
		    NULL, 0,
		    &bytesReturned, NULL);
		err = 0;
	}

	(void) close(fd);
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
check_disk(const char *path, int force,
    boolean_t isspare, boolean_t iswholedisk)
{
	struct dk_gpt *vtoc;
	char slice_path[MAXPATHLEN];
	int err = 0;
	int fd, i;
	int flags = O_RDONLY|O_DIRECT;
	STORAGE_DEVICE_NUMBER deviceNumber;
	DWORD bytesReturned;
	DRIVE_LAYOUT_INFORMATION_EX *driveLayout;
	BYTE buffer[1024];

	if (!iswholedisk)
		return (check_slice(path, force, isspare));

	/* only spares can be shared, other devices require exclusive access */
	if (!isspare)
		flags |= O_EXCL;

	if ((fd = open(path, flags)) < 0) {
		fprintf(stderr, "%s: failed to open %s\n", __func__, path);
		return (-1);
	}

	// Query the device number using IOCTL_STORAGE_GET_DEVICE_NUMBER
	if (!DeviceIoControl(fd,
	    IOCTL_STORAGE_GET_DEVICE_NUMBER,
	    NULL, 0,
	    &deviceNumber, sizeof (deviceNumber),
	    &bytesReturned, NULL)) {
		return (force ? 0 : -1);
	}

	driveLayout = (DRIVE_LAYOUT_INFORMATION_EX *)buffer;

	// Retrieve the drive layout information
	BOOL result = DeviceIoControl(
	    fd,
	    IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
	    NULL, 0,
	    driveLayout, sizeof (buffer),
	    &bytesReturned,
	    NULL);

	if (result) {

		// Print out the partition information
		err = 0;
		for (DWORD i = 0; i < driveLayout->PartitionCount; i++) {
			PARTITION_INFORMATION_EX partition =
			    driveLayout->PartitionEntry[i];
			snprintf(slice_path, sizeof (slice_path),
			    "\\\\?\\Harddisk%luPartition%lu",
			    deviceNumber.DeviceNumber,
			    partition.PartitionNumber);
			err += check_slice(slice_path, force, isspare);
		}
		if (err > 0) {

			(void) fprintf(stderr,
			    gettext("Disk appears in use.\n"));

			return (force ? 0 : EBUSY);
		}
	}

	/*
	 * Expected to fail for non-EFI labeled disks.  Just check the device
	 * as given and do not attempt to detect and scan partitions.
	 */
	err = efi_alloc_and_read(fd, &vtoc);
	if (err) {
		(void) close(fd);
		return (check_slice(path, force, isspare));
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

		size_t length =
		    vtoc->efi_parts[i].p_size *
		    vtoc->efi_lbasize;
		off_t  offset =
		    vtoc->efi_parts[i].p_start *
		    vtoc->efi_lbasize;
		snprintf(slice_path, sizeof (slice_path),
		    "#%llu#%llu#%s",
		    offset, length, path);

		err = check_slice(slice_path, force, isspare);
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
	int error;

	error = check_disk(path, force, isspare, iswholedisk);
	if (error != 0)
		return (error);

	return (check_file(path, force, isspare));
}

int
check_file(const char *file, boolean_t force, boolean_t isspare)
{

	return (check_file_generic(file, force, isspare));
}

void
after_zpool_upgrade(zpool_handle_t *zhp)
{
}


int
zpool_power_current_state(zpool_handle_t *zhp, char *vdev)
{
	(void) zhp;
	(void) vdev;
	/* Enclosure slot power not supported on macOS yet */
	return (-1);
}

int
zpool_power(zpool_handle_t *zhp, char *vdev, boolean_t turn_on)
{
	(void) zhp;
	(void) vdev;
	(void) turn_on;
	/* Enclosure slot power not supported on macOS yet */
	return (ENOTSUP);
}
