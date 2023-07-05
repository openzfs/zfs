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
 * Copyright 2015 Nexenta Systems, Inc. All rights reserved.
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright 2015 RackTop Systems.
 * Copyright (c) 2016, Intel Corporation.
 * Portions Copyright 2022 Andrew Innes <andrew.c12@gmail.com>
 */

/*
 * Pool import support functions.
 *
 * Used by zpool, ztest, zdb, and zhack to locate importable configs. Since
 * these commands are expected to run in the global zone, we can assume
 * that the devices are all readable when called.
 *
 * To import a pool, we rely on reading the configuration information from the
 * ZFS label of each device.  If we successfully read the label, then we
 * organize the configuration information in the following hierarchy:
 *
 *	pool guid -> toplevel vdev guid -> label txg
 *
 * Duplicate entries matching this same tuple will be discarded.  Once we have
 * examined every device, we pick the best label txg config for each toplevel
 * vdev.  We then arrange these toplevel vdevs into a complete pool config, and
 * update any paths that have changed.  Finally, we attempt to import the pool
 * using our derived config, and record the results.
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <libintl.h>
#include <libgen.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/dktp/fdisk.h>
#include <sys/vdev_impl.h>
#include <sys/fs/zfs.h>
#include <sys/vdev_impl.h>

#include <thread_pool.h>
#include <libzutil.h>
#include <libnvpair.h>

#include <sys/efi_partition.h>

#include "zutil_import.h"

#ifdef HAVE_LIBUDEV
#include <libudev.h>
#include <sched.h>
#endif

#define	_WIN32_MEAN_AND_LEAN
#include <Windows.h>
#include <Setupapi.h>
#include <Ntddstor.h>
#pragma comment(lib, "setupapi.lib")

/*
 * We allow /dev/ to be search in DEBUG build
 * DEFAULT_IMPORT_PATH_SIZE is decremented by one to remove /dev!
 * See below in zpool_find_import_blkid() to skip.
 */
#define	DEFAULT_IMPORT_PATH_SIZE	4

#define	DEV_BYID_PATH "/private/var/run/disk/by-id"

static char *
zpool_default_import_path[DEFAULT_IMPORT_PATH_SIZE] = {
	"/private/var/run/disk/by-id",
	"/private/var/run/disk/by-path",
	"/private/var/run/disk/by-serial",
	"/dev"	/* Only with DEBUG build */
};

extern uint64_t GetFileDriveSize(HANDLE);

static boolean_t
is_watchdog_dev(char *dev)
{
	/* For 'watchdog' dev */
	if (strcmp(dev, "watchdog") == 0)
		return (B_TRUE);

	/* For 'watchdog<digit><whatever> */
	if (strstr(dev, "watchdog") == dev && isdigit(dev[8]))
		return (B_TRUE);

	return (B_FALSE);
}

int
zfs_dev_flush(int fd)
{
//	return (ioctl(fd, BLKFLSBUF));
	return (0);
}

/*
 * Return the offset of the given label.
 */
static uint64_t
label_offset(uint64_t size, int l)
{
	ASSERT(P2PHASE_TYPED(size, sizeof (vdev_label_t), uint64_t) == 0);
	return (l * sizeof (vdev_label_t) + (l < VDEV_LABELS / 2 ?
	    0 : size - VDEV_LABELS * sizeof (vdev_label_t)));
}

static int
zpool_read_label_win(HANDLE h, off_t offset, uint64_t len,
    nvlist_t **config, int *num_labels)
{
	int l, count = 0;
	vdev_label_t *label;
	nvlist_t *expected_config = NULL;
	uint64_t expected_guid = 0, size;
	LARGE_INTEGER large;
	uint64_t drivesize;

	*config = NULL;

	drivesize = len;
	size = P2ALIGN_TYPED(drivesize, sizeof (vdev_label_t), uint64_t);

	if ((label = malloc(sizeof (vdev_label_t))) == NULL)
		return (-1);

	for (l = 0; l < VDEV_LABELS; l++) {
		uint64_t state, guid, txg;

		if (pread_win(h, label, sizeof (vdev_label_t),
		    label_offset(size, l) + offset) != sizeof (vdev_label_t))
			continue;

		if (nvlist_unpack(label->vl_vdev_phys.vp_nvlist,
		    sizeof (label->vl_vdev_phys.vp_nvlist), config, 0) != 0)
			continue;

		if (nvlist_lookup_uint64(*config, ZPOOL_CONFIG_GUID,
		    &guid) != 0 || guid == 0) {
			nvlist_free(*config);
			continue;
		}

		if (nvlist_lookup_uint64(*config, ZPOOL_CONFIG_POOL_STATE,
		    &state) != 0 || state > POOL_STATE_L2CACHE) {
			nvlist_free(*config);
			continue;
		}

		if (state != POOL_STATE_SPARE && state != POOL_STATE_L2CACHE &&
		    (nvlist_lookup_uint64(*config, ZPOOL_CONFIG_POOL_TXG,
		    &txg) != 0 || txg == 0)) {
			nvlist_free(*config);
			continue;
		}

		if (expected_guid) {
			if (expected_guid == guid)
				count++;

			nvlist_free(*config);
		} else {
			expected_config = *config;
			expected_guid = guid;
			count++;
		}
	}

	if (num_labels != NULL)
		*num_labels = count;

	free(label);
	*config = expected_config;

	return (0);
}

/*
 * Somethings do not like mixing slashes with backslashes. So
 * let's try using slashes with user facing output, zpool status etc.
 * but internally we use backslashes in vdev_physpath
 */
static void
zfs_backslashes(char *s)
{
	char *r;
	while ((r = strchr(s, '/')) != NULL)
		*r = '\\';
}

static void
zfs_slashes(char *s)
{
	char *r;
	while ((r = strchr(s, '\\')) != NULL)
		*r = '/';
}

void
zpool_open_func(void *arg)
{
	rdsk_node_t *rn = arg;
	libpc_handle_t *hdl = rn->rn_hdl;
	struct stat64 statbuf;
	nvlist_t *config;
	char *bname, *dupname;
	uint64_t vdev_guid = 0;
	int error;
	int num_labels = 0;
	HANDLE h;
	uint64_t offset = 0;
	uint64_t len = 0;
	uint64_t drive_len;

	// Check if this filename is encoded with "#start#len#name"
	if (rn->rn_name[0] == '#') {
		char *end = NULL;

		offset = strtoull(&rn->rn_name[1], &end, 10);
		while (end && *end == '#') end++;
		len = strtoull(end, &end, 10);
		while (end && *end == '#') end++;
		h = CreateFile(end,
		    GENERIC_READ,
		    FILE_SHARE_READ /* | FILE_SHARE_WRITE */,
		    NULL,
		    OPEN_EXISTING,
		    FILE_ATTRIBUTE_NORMAL /* | FILE_FLAG_OVERLAPPED */,
		    NULL);
		if (h == INVALID_HANDLE_VALUE) {
			int error = GetLastError();
			return;
		}
		LARGE_INTEGER place;
		place.QuadPart = offset;
		// If it fails, we cant read label
		SetFilePointerEx(h, place, NULL, FILE_BEGIN);
		drive_len = len;

	} else {
		// We have no openat() - so stitch paths togther.
		// char fullpath[MAX_PATH];
		// snprintf(fullpath, sizeof (fullpath), "%s%s",
		// 	"", rn->rn_name);
		zfs_backslashes(rn->rn_name);
		h = CreateFile(rn->rn_name,
		    GENERIC_READ,
		    FILE_SHARE_READ /* | FILE_SHARE_WRITE */,
		    NULL,
		    OPEN_EXISTING,
		    FILE_ATTRIBUTE_NORMAL /* | FILE_FLAG_OVERLAPPED */,
		    NULL);
		if (h == INVALID_HANDLE_VALUE) {
			int error = GetLastError();
			return;
		}

		drive_len = GetFileDriveSize(h);
	}

	DWORD type = GetFileType(h);

	/* this file is too small to hold a zpool */
	if (type == FILE_TYPE_DISK &&
	    drive_len < SPA_MINDEVSIZE) {
		CloseHandle(h);
		return;
	}
// else if (type != FILE_TYPE_DISK) {
		/*
		 * Try to read the disk label first so we don't have to
		 * open a bunch of minor nodes that can't have a zpool.
		 */
//		check_slices(rn->rn_avl, HTOI(h), rn->rn_name);
//	}

	if ((zpool_read_label_win(h, offset, drive_len, &config,
	    &num_labels)) != 0) {
		CloseHandle(h);
		(void) no_memory(rn->rn_hdl);
		return;
	}

	if (num_labels == 0) {
		CloseHandle(h);
		nvlist_free(config);
		return;
	}


	/*
	 * Check that the vdev is for the expected guid.  Additional entries
	 * are speculatively added based on the paths stored in the labels.
	 * Entries with valid paths but incorrect guids must be removed.
	 */
	error = nvlist_lookup_uint64(config, ZPOOL_CONFIG_GUID, &vdev_guid);
	if (error || (rn->rn_vdev_guid && rn->rn_vdev_guid != vdev_guid)) {
		CloseHandle(h);
		nvlist_free(config);
		return;
	}

	CloseHandle(h);

	rn->rn_config = config;
	rn->rn_num_labels = num_labels;

	/*
	 * Add additional entries for paths described by this label.
	 */
	if (rn->rn_labelpaths) {
		char *path = NULL;
		char *devid = NULL;
		char *env = NULL;
		rdsk_node_t *slice;
		avl_index_t where;
		int timeout;
		int error;

		if (label_paths(rn->rn_hdl, rn->rn_config, &path, &devid))
			return;

		env = getenv("ZPOOL_IMPORT_UDEV_TIMEOUT_MS");
		if ((env == NULL) || sscanf(env, "%d", &timeout) != 1 ||
		    timeout < 0) {
			timeout = DISK_LABEL_WAIT;
		}

		/*
		 * Allow devlinks to stabilize so all paths are available.
		 */
		zpool_label_disk_wait(rn->rn_name, timeout);

		if (path != NULL) {
			slice = zutil_alloc(hdl, sizeof (rdsk_node_t));
			// slice->rn_name = zutil_strdup(hdl, path);
			slice->rn_name = zutil_strdup(hdl, rn->rn_name);
			slice->rn_vdev_guid = vdev_guid;
			slice->rn_avl = rn->rn_avl;
			slice->rn_hdl = hdl;
			slice->rn_order = IMPORT_ORDER_PREFERRED_1;
			slice->rn_labelpaths = B_FALSE;
			pthread_mutex_lock(rn->rn_lock);
			if (avl_find(rn->rn_avl, slice, &where)) {
			pthread_mutex_unlock(rn->rn_lock);
				free(slice->rn_name);
				free(slice);
			} else {
				avl_insert(rn->rn_avl, slice, where);
				pthread_mutex_unlock(rn->rn_lock);
				zpool_open_func(slice);
			}
		}

		if (devid != NULL) {
			slice = zutil_alloc(hdl, sizeof (rdsk_node_t));
			error = asprintf(&slice->rn_name, "%s%s",
			    DEV_BYID_PATH, devid);
			if (error == -1) {
				free(slice);
				return;
			}

			slice->rn_vdev_guid = vdev_guid;
			slice->rn_avl = rn->rn_avl;
			slice->rn_hdl = hdl;
			slice->rn_order = IMPORT_ORDER_PREFERRED_2;
			slice->rn_labelpaths = B_FALSE;
			pthread_mutex_lock(rn->rn_lock);
			if (avl_find(rn->rn_avl, slice, &where)) {
				pthread_mutex_unlock(rn->rn_lock);
				free(slice->rn_name);
				free(slice);
			} else {
				avl_insert(rn->rn_avl, slice, where);
				pthread_mutex_unlock(rn->rn_lock);
				zpool_open_func(slice);
			}
		}
	}

}

const char * const *
zpool_default_search_paths(size_t *count)
{
	*count = DEFAULT_IMPORT_PATH_SIZE;
	return ((const char * const *)zpool_default_import_path);
}

/*
 * Call Windows API to get list of physical disks, and iterate through them
 * finding partitions.
 */
int
zpool_find_import_blkid(libpc_handle_t *hdl, pthread_mutex_t *lock,
    avl_tree_t **slice_cache)
{
	int i, dirs;
	struct dirent *dp;
	char path[MAXPATHLEN];
	char *end, **dir;
	size_t pathleft;
	avl_index_t where;
	rdsk_node_t *slice;
	int error = 0;
	void *cookie;
	char rdsk[MAXPATHLEN];

	HDEVINFO diskClassDevices;
	GUID diskClassDeviceInterfaceGuid = GUID_DEVINTERFACE_DISK;
	SP_DEVICE_INTERFACE_DATA deviceInterfaceData;
	PSP_DEVICE_INTERFACE_DETAIL_DATA deviceInterfaceDetailData;
	DWORD requiredSize;
	DWORD deviceIndex;

	HANDLE disk = INVALID_HANDLE_VALUE;
	STORAGE_DEVICE_NUMBER diskNumber;
	DWORD bytesReturned;

	/*
	 * Go through and read the label configuration information from every
	 * possible device, organizing the information according to pool GUID
	 * and toplevel GUID.
	 */
	*slice_cache = zutil_alloc(hdl, sizeof (avl_tree_t));
	avl_create(*slice_cache, slice_cache_compare,
	    sizeof (rdsk_node_t), offsetof(rdsk_node_t, rn_node));



	/* First, open all raw physical devices */

	diskClassDevices = SetupDiGetClassDevs(&diskClassDeviceInterfaceGuid,
	    NULL,
	    NULL,
	    DIGCF_PRESENT |
	    DIGCF_DEVICEINTERFACE);
	// CHK(INVALID_HANDLE_VALUE != diskClassDevices,
	//	"SetupDiGetClassDevs");

	ZeroMemory(&deviceInterfaceData, sizeof (SP_DEVICE_INTERFACE_DATA));
	deviceInterfaceData.cbSize = sizeof (SP_DEVICE_INTERFACE_DATA);
	deviceIndex = 0;

	while (SetupDiEnumDeviceInterfaces(diskClassDevices,
	    NULL,
	    &diskClassDeviceInterfaceGuid,
	    deviceIndex,
	    &deviceInterfaceData)) {

		++deviceIndex;

		SetupDiGetDeviceInterfaceDetail(diskClassDevices,
		    &deviceInterfaceData,
		    NULL,
		    0,
		    &requiredSize,
		    NULL);
		// CHK(ERROR_INSUFFICIENT_BUFFER == GetLastError(),
		//	"SetupDiGetDeviceInterfaceDetail - 1");

		deviceInterfaceDetailData =
		    (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(requiredSize);
		// CHK(NULL != deviceInterfaceDetailData,
		//	"malloc");

		ZeroMemory(deviceInterfaceDetailData, requiredSize);
		deviceInterfaceDetailData->cbSize =
		    sizeof (SP_DEVICE_INTERFACE_DETAIL_DATA);

		SetupDiGetDeviceInterfaceDetail(diskClassDevices,
		    &deviceInterfaceData,
		    deviceInterfaceDetailData,
		    requiredSize,
		    NULL,
		    NULL);

		// Here, the device path is something like
		// " \\?\ide#diskvmware_virtual_ide_hard_drive___________"
		// "00000001#5&1778b74b&0&0.0.0#{53f56307-b6bf-11d0-"
		// "94f2-00a0c91efb8b}"
		// and we create a path like
		// "\\?\PhysicalDrive0"
		// but perhaps it is better to use the full name of the device.
		disk = CreateFile(deviceInterfaceDetailData->DevicePath,
		    0 /* GENERIC_READ */,
		    FILE_SHARE_READ /* | FILE_SHARE_WRITE */,
		    NULL,
		    OPEN_EXISTING,
		    FILE_ATTRIBUTE_NORMAL /* | FILE_FLAG_OVERLAPPED */,
		    NULL);
		if (disk == INVALID_HANDLE_VALUE)
			continue;

		DeviceIoControl(disk,
		    IOCTL_STORAGE_GET_DEVICE_NUMBER,
		    NULL,
		    0,
		    &diskNumber,
		    sizeof (STORAGE_DEVICE_NUMBER),
		    &bytesReturned,
		    NULL);

		fprintf(stderr, "path '%s'\n and '\\\\?\\PhysicalDrive%d'\n",
		    deviceInterfaceDetailData->DevicePath,
		    diskNumber.DeviceNumber);
		fflush(stderr);
		snprintf(rdsk, MAXPATHLEN, "\\\\.\\PHYSICALDRIVE%d",
		    diskNumber.DeviceNumber);

		// CloseHandle(disk);

#if 0
		// This debug code was here to skip the boot disk,
		// but it assumes the first disk is boot, which is wrong.
		if (diskNumber.DeviceNumber == 0) {
			CloseHandle(disk);
			continue;
		}
#endif
		DWORD ior;
		PDRIVE_LAYOUT_INFORMATION_EX partitions;
		DWORD partitionsSize = sizeof (DRIVE_LAYOUT_INFORMATION_EX) +
		    127 * sizeof (PARTITION_INFORMATION_EX);
		partitions =
		    (PDRIVE_LAYOUT_INFORMATION_EX)malloc(partitionsSize);
		if (DeviceIoControl(disk, IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
		    NULL, 0, partitions, partitionsSize, &ior, NULL)) {
			fprintf(stderr, "read partitions ok %d\n",
			    partitions->PartitionCount);
			fflush(stderr);

			for (int i = 0; i < partitions->PartitionCount; i++) {
				int add = 0;
		switch (partitions->PartitionEntry[i].PartitionStyle) {
		case PARTITION_STYLE_MBR:
			fprintf(stderr,
			    "    mbr %d: type %x off 0x%llx len 0x%llx\n", i,
			    partitions->PartitionEntry[i].Mbr.PartitionType,
			    partitions->PartitionEntry[i].
			    StartingOffset.QuadPart,
			    partitions->PartitionEntry[i].
			    PartitionLength.QuadPart);
			fflush(stderr);
			add = 1;
			break;
		case PARTITION_STYLE_GPT:
			fprintf(stderr,
			    "    gpt %d: type %llx off 0x%llx len 0x%llx\n", i,
			    partitions->PartitionEntry[i].Gpt.PartitionType,
			    partitions->PartitionEntry[i].
			    StartingOffset.QuadPart,
			    partitions->PartitionEntry[i].PartitionLength.
			    QuadPart);
			fflush(stderr);
			add = 1;
			break;
		}

		if (add &&
		    partitions->PartitionEntry[i].PartitionLength.
		    QuadPart > SPA_MINDEVSIZE) {
			slice = zutil_alloc(hdl, sizeof (rdsk_node_t));

			error = asprintf(&slice->rn_name,
			    "\\\\?\\Harddisk%uPartition%u",
			    diskNumber.DeviceNumber, i);
			if (error == -1) {
				free(slice);
				continue;
			}

			slice->rn_vdev_guid = 0;
			slice->rn_lock = lock;
			slice->rn_avl = *slice_cache;
			slice->rn_hdl = hdl;
			slice->rn_labelpaths = B_TRUE;
			slice->rn_order = IMPORT_ORDER_PREFERRED_2;

			pthread_mutex_lock(lock);
			if (avl_find(*slice_cache, slice, &where)) {
				free(slice->rn_name);
				free(slice);
			} else {
				avl_insert(*slice_cache, slice, where);
			}
			pthread_mutex_unlock(lock);
		}
	}
			// in case we have a disk without partition,
			// it would be possible that the
			// disk itself contains a pool, so let's check that
			if (partitions->PartitionCount == 0) {

				slice = zutil_alloc(hdl, sizeof (rdsk_node_t));

				uint64_t size = GetFileDriveSize(disk);

				error = asprintf(&slice->rn_name,
				    "#%llu#%llu#%s",
				    0ULL, size,
				    deviceInterfaceDetailData->DevicePath);

				if (error == -1) {
					free(slice);
					continue;
				}

				slice->rn_vdev_guid = 0;
				slice->rn_lock = lock;
				slice->rn_avl = *slice_cache;
				slice->rn_hdl = hdl;
				slice->rn_labelpaths = B_TRUE;
				slice->rn_order =
				    IMPORT_ORDER_SCAN_OFFSET + deviceIndex;

				pthread_mutex_lock(lock);
				if (avl_find(*slice_cache, slice, &where)) {
					free(slice->rn_name);
					free(slice);
				} else {
					avl_insert(*slice_cache, slice, where);
				}
				pthread_mutex_unlock(lock);
			}

			free(partitions);
		} else {
			fprintf(stderr, "read partitions ng\n");
			fflush(stderr);
		}

		CloseHandle(disk);
#if 1 // efi
		// Add the whole physical device, but lets also try
		// to read EFI off it.
		disk = CreateFile(deviceInterfaceDetailData->DevicePath,
		    GENERIC_READ,
		    FILE_SHARE_READ /* | FILE_SHARE_WRITE */,
		    NULL,
		    OPEN_EXISTING,
		    FILE_ATTRIBUTE_NORMAL /* | FILE_FLAG_OVERLAPPED */,
		    NULL);

		// On standard OsX created zpool, we expect:
		// offset name
		// 0x200  MBR partition protective GPT
		// 0x400  EFI partition, s0 as ZFS
		// 0x8410 "version" "name" "testpool" ZFS label
		if (disk != INVALID_HANDLE_VALUE) {
			fprintf(stderr, "asking libefi to read label\n");
			fflush(stderr);
			int error;
			struct dk_gpt *vtoc;
			error = efi_alloc_and_read(HTOI(disk), &vtoc);
			if (error >= 0) {
				fprintf(stderr,
				    "EFI read OK, max partitions %d\n",
				    vtoc->efi_nparts);
				fflush(stderr);
				for (int i = 0; i < vtoc->efi_nparts; i++) {

					if (vtoc->efi_parts[i].p_start == 0 &&
					    vtoc->efi_parts[i].p_size == 0)
						continue;

			fprintf(stderr,
			    "    part %d:  offset %llx:    len %llx:    "
			    "tag: %x    name: '%s'\n",
			    i, vtoc->efi_parts[i].p_start,
			    vtoc->efi_parts[i].p_size,
			    vtoc->efi_parts[i].p_tag,
			    vtoc->efi_parts[i].p_name);
			fflush(stderr);
			if (vtoc->efi_parts[i].p_start != 0 &&
			    vtoc->efi_parts[i].p_size != 0) {
			// Lets invent a naming scheme with start,
			// and len in it.

			slice = zutil_alloc(hdl,
			    sizeof (rdsk_node_t));

			error = asprintf(&slice->rn_name, "#%llu#%llu#%s",
			    vtoc->efi_parts[i].p_start * vtoc->efi_lbasize,
			    vtoc->efi_parts[i].p_size * vtoc->efi_lbasize,
			    deviceInterfaceDetailData->DevicePath);
			if (error == -1) {
				free(slice);
				continue;
			}

			slice->rn_vdev_guid = 0;
			slice->rn_lock = lock;
			slice->rn_avl = *slice_cache;
			slice->rn_hdl = hdl;
			slice->rn_labelpaths = B_TRUE;
			slice->rn_order = IMPORT_ORDER_SCAN_OFFSET + i;

			pthread_mutex_lock(lock);
			if (avl_find(*slice_cache, slice, &where)) {
				free(slice->rn_name);
				free(slice);
			} else {
				avl_insert(*slice_cache, slice, where);
			}
			pthread_mutex_unlock(lock);
			}
			}
			}
			efi_free(vtoc);
			CloseHandle(disk);
		} else { // Unable to open handle
			fprintf(stderr,
			    "Unable to open disk, are we Administrator? "
			    "GetLastError() is 0x%x\n",
			    GetLastError());
			fflush(stderr);
		}

#endif
	} // while SetupDiEnumDeviceInterfaces

	return (0);
}


/*
 * Linux persistent device strings for vdev labels
 *
 * based on libudev for consistency with libudev disk add/remove events
 */

typedef struct vdev_dev_strs {
	char	vds_devid[128];
	char	vds_devphys[128];
} vdev_dev_strs_t;


int
zfs_device_get_devid(struct udev_device *dev, char *bufptr, size_t buflen)
{
	return (ENODATA);
}


int
zfs_device_get_physical(struct udev_device *dev, char *bufptr, size_t buflen)
{
	return (ENODATA);
}

/*
 * Encode the persistent devices strings
 * used for the vdev disk label
 */
static int
encode_device_strings(const char *path, vdev_dev_strs_t *ds,
    boolean_t wholedisk)
{
	return (ENOENT);
}

/* Given a "#1234#1234#/path/part" - find the path part only */
static void
remove_partition_offset_hack(char *hacked_path, char **out_dev_path)
{
	uint64_t offset;
	uint64_t len;
	char *end = NULL;

	if (hacked_path[0] != '#') {
		*out_dev_path = hacked_path;
		return;
	}

	end = hacked_path;
	for (int i = 0; i < 3; i++) {
		while (*end && *end != '#') {
			end++;
		}
		if (*end == 0)
			break;
		end++;
	}
	*out_dev_path = end;
}

static int
get_device_number(char *device_path, STORAGE_DEVICE_NUMBER *device_number)
{
	HANDLE hDevice = INVALID_HANDLE_VALUE;
	DWORD returned = 0;

	hDevice = CreateFile(device_path,
	    GENERIC_READ,
	    FILE_SHARE_READ /* | FILE_SHARE_WRITE */,
	    NULL,
	    OPEN_EXISTING,
	    FILE_ATTRIBUTE_NORMAL /* | FILE_FLAG_OVERLAPPED */,
	    NULL);
	if (hDevice == INVALID_HANDLE_VALUE) {
		// fprintf(stderr, "invalid handle value\n"); fflush(stderr);
		return (GetLastError());
	}

	BOOL ret = DeviceIoControl(hDevice,
	    IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0,
	    (LPVOID)device_number, (DWORD)sizeof (*device_number),
	    (LPDWORD)&returned, (LPOVERLAPPED)NULL);

	CloseHandle(hDevice);

	if (!ret) {
		// fprintf(stderr, "DeviceIoControl returned error\n");
		// fflush(stderr);
		return (ERROR_INVALID_FUNCTION);
	}

	return (ERROR_SUCCESS);
}


/*
 * Update a leaf vdev's persistent device strings
 *
 * - only applies for a dedicated leaf vdev (aka whole disk)
 * - updated during pool create|add|attach|import
 * - used for matching device matching during auto-{online,expand,replace}
 * - stored in a leaf disk config label (i.e. alongside 'path' NVP)
 * - these strings are currently not used in kernel (i.e. for vdev_disk_open)
 *
 * single device node example:
 * 	devid:		'scsi-MG03SCA300_350000494a8cb3d67-part1'
 * 	phys_path:	'pci-0000:04:00.0-sas-0x50000394a8cb3d67-lun-0'
 *
 * multipath device node example:
 * 	devid:		'dm-uuid-mpath-35000c5006304de3f'
 *
 * We also store the enclosure sysfs path for turning on enclosure LEDs
 * (if applicable):
 *	vdev_enc_sysfs_path: '/sys/class/enclosure/11:0:1:0/SLOT 4'
 */
void
update_vdev_config_dev_strsXXXX(nvlist_t *nv)
{
	vdev_dev_strs_t vds;
	char *env, *type, *path, *devid;
	uint64_t wholedisk = 0;
	int ret;
	// Build a pretty vdev_path here
	char *end = NULL;
	STORAGE_DEVICE_NUMBER deviceNumber;
	char udevpath[MAXPATHLEN];

	if (nvlist_lookup_string(nv, ZPOOL_CONFIG_PATH, &path) != 0)
		return;
	nvlist_lookup_uint64(nv, ZPOOL_CONFIG_WHOLE_DISK, &wholedisk);

	fprintf(stderr, "working on dev '%s'\n", path); fflush(stderr);

	devid = strdup(path);

	HANDLE h;
	h = CreateFile(path, GENERIC_READ,
	    FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (h != INVALID_HANDLE_VALUE) {
		struct dk_gpt *vtoc;
		if ((efi_alloc_and_read(HTOI(h), &vtoc)) == 0) {
		// Slice 1 should be ZFS
		fprintf(stderr,
		    "this code assumes ZFS is on partition 1\n");
		fflush(stderr);
		snprintf(udevpath, MAXPATHLEN, "#%llu#%llu#%s",
		    vtoc->efi_parts[0].p_start * (uint64_t)vtoc->efi_lbasize,
		    vtoc->efi_parts[0].p_size * (uint64_t)vtoc->efi_lbasize,
		    path);
		efi_free(vtoc);
		path = udevpath;
		}
		CloseHandle(h);
	}

	remove_partition_offset_hack(devid, &end);

	// If it is a device, clean that up - otherwise it is a filename pool
	ret = get_device_number(end, &deviceNumber);
	if (ret == 0) {
		char *vdev_path;

		if (wholedisk)
			asprintf(&vdev_path, "/dev/physicaldrive%lu",
			    deviceNumber.DeviceNumber);
		else
			asprintf(&vdev_path, "/dev/Harddisk%luPartition%lu",
			    deviceNumber.DeviceNumber,
			    deviceNumber.PartitionNumber);

		fprintf(stderr, "setting path here '%s'\r\n", vdev_path);
		fflush(stderr);
		fprintf(stderr, "setting physpath here '%s'\r\n", path);
		fflush(stderr);
		nvlist_remove_all(nv, ZPOOL_CONFIG_PHYS_PATH);
		if (nvlist_add_string(nv, ZPOOL_CONFIG_PHYS_PATH, path) != 0)
			return;
		if (nvlist_add_string(nv, ZPOOL_CONFIG_VDEV_ENC_SYSFS_PATH,
		    strdup(path)) != 0)
			return;
		// This call frees the original "path", can't access after now
		nvlist_remove_all(nv, ZPOOL_CONFIG_PATH);
		if (nvlist_add_string(nv, ZPOOL_CONFIG_PATH, vdev_path) != 0)
			return;

	} else {
		// Not a disk, filepool. Fix path.
		char *vdev_path;

		if (path[0] != '/') {
			asprintf(&vdev_path, "\\??\\%s", path);
			zfs_backslashes(vdev_path);
			if (nvlist_add_string(nv, ZPOOL_CONFIG_PHYS_PATH,
			    vdev_path) != 0)
				return;

			asprintf(&vdev_path, "//./%s", path);
			zfs_slashes(vdev_path);
			fprintf(stderr, "correcting path: '%s' \r\n",
			    vdev_path);
			fflush(stderr);
			if (nvlist_add_string(nv, ZPOOL_CONFIG_PATH,
			    vdev_path) != 0)
				return;

		}
	}

	free(devid);

}

/*
 * Update a leaf vdev's persistent device strings
 *
 * - only applies for a dedicated leaf vdev (aka whole disk)
 * - updated during pool create|add|attach|import
 * - used for matching device matching during auto-{online,expand,replace}
 * - stored in a leaf disk config label (i.e. alongside 'path' NVP)
 * - these strings are currently not used in kernel (i.e. for vdev_disk_open)
 *
 * single device node example:
 *      devid:          'scsi-MG03SCA300_350000494a8cb3d67-part1'
 *      phys_path:      'pci-0000:04:00.0-sas-0x50000394a8cb3d67-lun-0'
 *
 * multipath device node example:
 *      devid:          'dm-uuid-mpath-35000c5006304de3f'
 *
 * We also store the enclosure sysfs path for turning on enclosure LEDs
 * (if applicable):
 *      vdev_enc_sysfs_path: '/sys/class/enclosure/11:0:1:0/SLOT 4'
 */
void
update_vdev_config_dev_strs(nvlist_t *nv)
{
	/*
	 * First Windows work
	 */
	vdev_dev_strs_t vds;
	char *env, *type, *path, *devid;
	uint64_t wholedisk = 0;
	int ret;
	// Build a pretty vdev_path here
	char *end = NULL;
	STORAGE_DEVICE_NUMBER deviceNumber;
	char udevpath[MAXPATHLEN];

	if (nvlist_lookup_string(nv, ZPOOL_CONFIG_PATH, &path) != 0)
		return;
	nvlist_lookup_uint64(nv, ZPOOL_CONFIG_WHOLE_DISK, &wholedisk);

	fprintf(stderr, "working on dev '%s'\n", path); fflush(stderr);

	devid = strdup(path);

	HANDLE h;
	h = CreateFile(path, GENERIC_READ,
	    FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (h != INVALID_HANDLE_VALUE) {
		struct dk_gpt *vtoc;
		if ((efi_alloc_and_read(HTOI(h), &vtoc)) == 0) {
			// Slice 1 should be ZFS
			fprintf(stderr,
			"this code assumes ZFS is on partition 1\n");
			fflush(stderr);
			snprintf(udevpath, MAXPATHLEN, "#%llu#%llu#%s",
			    vtoc->efi_parts[0].p_start * (uint64_t)vtoc->
			    efi_lbasize,
			    vtoc->efi_parts[0].p_size * (uint64_t)vtoc->
			    efi_lbasize,
			    path);
			efi_free(vtoc);
			path = udevpath;
		}
		CloseHandle(h);
	}

	remove_partition_offset_hack(devid, &end);

	// If it is a device, clean that up - otherwise it is a filename pool
	ret = get_device_number(end, &deviceNumber);
	if (ret == 0) {
		char *vdev_path;

		if (wholedisk)
			asprintf(&vdev_path, "/dev/physicaldrive%lu",
			    deviceNumber.DeviceNumber);
		else
			asprintf(&vdev_path, "/dev/Harddisk%luPartition%lu",
			    deviceNumber.DeviceNumber,
			    deviceNumber.PartitionNumber);

		fprintf(stderr, "setting path here '%s'\r\n", vdev_path);
		fflush(stderr);
		fprintf(stderr, "setting physpath here '%s'\r\n", path);
		fflush(stderr);
		nvlist_remove_all(nv, ZPOOL_CONFIG_PHYS_PATH);
		if (nvlist_add_string(nv, ZPOOL_CONFIG_PHYS_PATH, path) != 0)
			return;
		// This call frees the original "path", can't access after now
		nvlist_remove_all(nv, ZPOOL_CONFIG_PATH);
		if (nvlist_add_string(nv, ZPOOL_CONFIG_PATH, vdev_path) != 0)
			return;

	} else {
		// Not a disk, filepool. Fix path.
		char *vdev_path;

		if (path[0] != '/') {
			asprintf(&vdev_path, "\\??\\%s", path);
			zfs_backslashes(vdev_path);
			if (nvlist_add_string(nv, ZPOOL_CONFIG_PHYS_PATH, \
			    vdev_path) != 0)
			return;

		asprintf(&vdev_path, "//./%s", path);
		zfs_slashes(vdev_path);
		fprintf(stderr, "correcting path: '%s' \r\n", vdev_path);
		fflush(stderr);
		if (nvlist_add_string(nv, ZPOOL_CONFIG_PATH, vdev_path) != 0)
			return;

		}
	}

	free(devid);



	/*
	 * For the benefit of legacy ZFS implementations, allow
	 * for opting out of devid strings in the vdev label.
	 *
	 * example use:
	 *      env ZFS_VDEV_DEVID_OPT_OUT=YES zpool import dozer
	 *
	 * explanation:
	 * Older ZFS on Linux implementations had issues when attempting to
	 * display pool config VDEV names if a "devid" NVP value is present
	 * in the pool's config.
	 *
	 *
	 * For example, a pool that originated on illumos platform would
	 * have a devid value in the config and "zpool status" would fail
	 * when listing the config.
	 *
	 * A pool can be stripped of any "devid" values on import or
	 * prevented from adding them on zpool create|add by setting
	 * ZFS_VDEV_DEVID_OPT_OUT.
	 */
	env = getenv("ZFS_VDEV_DEVID_OPT_OUT");
	if (env && (strtoul(env, NULL, 0) > 0 ||
	    !strncasecmp(env, "YES", 3) || !strncasecmp(env, "ON", 2))) {
		(void) nvlist_remove_all(nv, ZPOOL_CONFIG_DEVID);
		(void) nvlist_remove_all(nv, ZPOOL_CONFIG_PHYS_PATH);
		return;
	}

	if (nvlist_lookup_string(nv, ZPOOL_CONFIG_TYPE, &type) != 0 ||
	    strcmp(type, VDEV_TYPE_DISK) != 0) {
		return;
	}
	if (nvlist_lookup_string(nv, ZPOOL_CONFIG_PATH, &path) != 0)
		return;
	(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_WHOLE_DISK, &wholedisk);

	/*
	 * Update device string values in the config nvlist.
	 */
	if (encode_device_strings(path, &vds, (boolean_t)wholedisk) == 0) {
		(void) nvlist_add_string(nv, ZPOOL_CONFIG_DEVID, vds.vds_devid);
		if (vds.vds_devphys[0] != '\0') {
			(void) nvlist_add_string(nv, ZPOOL_CONFIG_PHYS_PATH,
			    vds.vds_devphys);
		}

	} else {
		/* Clear out any stale entries. */
		(void) nvlist_remove_all(nv, ZPOOL_CONFIG_DEVID);
		// (void)nvlist_remove_all(nv, ZPOOL_CONFIG_PHYS_PATH);
		(void) nvlist_remove_all(nv, ZPOOL_CONFIG_VDEV_ENC_SYSFS_PATH);
	}
}

/*
 * The shared resolve shortname requires the shortname to exist in a directory
 * which is not the case for us to handle "PHYSICALDRIVEx". The device object
 * store is not opendir()able.
 */
int
zfs_resolve_shortname_os(const char *name, char *path, size_t len)
{
	/* Ok lets let them say just "PHYSICALDRIVEx" */
	if (!strncasecmp("PHYSICALDRIVE", name, 13)) {
		// Convert to "\\?\PHYSICALDRIVEx"
		snprintf(path, len, "\\\\?\\%s", name);
		printf("Expanded path to '%s'\n", path);
		return (0);
	}
	if (!strncasecmp("Harddisk", name, 8)) {
	    // Convert to "\\?\HarddiskXPartitionY"
		snprintf(path, len, "\\\\?\\%s", name);
		printf("Expanded path to '%s'\n", path);
		return (0);
	}
	return (ENOENT);
}

void
update_vdevs_config_dev_sysfs_path(nvlist_t *config)
{
	(void) config;
}
