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
 * Copyright 2015 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2014, 2019 by Delphix. All rights reserved.
 * Copyright 2016 Igor Kozhukhov <ikozhukhov@gmail.com>
 * Copyright 2017 RackTop Systems.
 * Copyright (c) 2018 Datto Inc.
 * Copyright 2018 OmniOS Community Edition (OmniOSce) Association.
 */

#include <Shlobj.h>

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <libintl.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <zone.h>
#include <sys/mntent.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/dsl_crypt.h>
#include <sys/zfs_mount.h>
#include <libzfs.h>

#include "libzfs_impl.h"
#include <thread_pool.h>

#include <sys/zfs_ioctl.h>

/*
 * if (zmount(zhp, zfs_get_name(zhp), mountpoint, MS_OPTIONSTR | flags,
 * MNTTYPE_ZFS, NULL, 0, mntopts, sizeof (mntopts)) != 0) {
 */
int
do_mount(zfs_handle_t *zhp, const char *dir, char *optptr, int mflag)
{
	int ret = 0;
	int ispool = 0;
	char driveletter[100] = "off";
	int hasprop = 0;

	// mount 'spec' "tank/joe" on path 'dir' "/home/joe".
#ifdef DEBUG
	fprintf(stderr, "zmount running, emulating Unix mount: '%s'\r\n", dir); fflush(stderr);
#endif
	zfs_cmd_t zc = { "\0" };

	if (zhp) {
		if (zhp->zpool_hdl &&
			!strcmp(zpool_get_name(zhp->zpool_hdl),
				zfs_get_name(zhp)))
			ispool = 1;

		ret = zfs_prop_get(zhp, ZFS_PROP_DRIVELETTER, driveletter,
			sizeof(driveletter), NULL, NULL, 0, B_FALSE);

		hasprop = ret ? 0 : 1;
		if (!ret && !strncmp("-", driveletter, sizeof(driveletter)))
			hasprop = 0;
	}

	// if !hasprop and ispool -> hasprop=1 & driveletter=on
	// if hasprop = on -> driveletter = ?
	// if hasprop = off
	if (!hasprop && ispool) {
		strcpy(driveletter, "on");
		hasprop = 1;
	}
	if (!strcmp("off", driveletter))
		hasprop = 0;
	else if (!strcmp("on", driveletter))
		strcpy(driveletter, "?");

	(void)strlcpy(zc.zc_name, zhp->zfs_name, sizeof(zc.zc_name));
	(void)strlcpy(zc.zc_value, dir, sizeof(zc.zc_value));

	// If hasprop is set, use 'driveletter' and ignore mountpoint path
	// if !hasprop && rootds same
	if (hasprop) {
		// We just pass "\\??\\X:" to kernel.
		snprintf(zc.zc_value, sizeof(zc.zc_value), "\\??\\%c:", 
			tolower(driveletter[0]));
	} else {
		// We are to mount with path. Attempt to find parent
		// driveletter, if any. Otherwise assume c:/
		driveletter[0] = 'c';

		if (!ispool) {
			char parent[ZFS_MAX_DATASET_NAME_LEN] = "";
			char *slashp;
			struct mnttab entry = { 0 };

			zfs_parent_name(zhp, parent, sizeof(parent));

			while (strlen(parent) >= 1) {
				if ((libzfs_mnttab_find(zhp->zfs_hdl, parent, &entry) == 0) &&
					(entry.mnt_mountp[1] == ':')) {
					driveletter[0] = entry.mnt_mountp[0];
#ifdef DEBUG
					fprintf(stderr, "we think '%s' parent is '%s' and its mounts are: '%s'\r\n",
						zfs_get_name(zhp), parent, entry.mnt_mountp); fflush(stderr);
#endif
					break;
				}
				if ((slashp = strrchr(parent, '/')) == NULL)
					break;
				*slashp = '\0';
			}

			// We need to skip the parent name part, in mountpoint "dir" here,ie
			// if parent is "BOOM/lower" we need to skip to the 3nd slash
			// in "/BOOM/lower/newfs"
			// So, check if the mounted name is in the string
			snprintf(parent, sizeof(parent), "/%s/", entry.mnt_special); // "BOOM" -> "/BOOM/"
			char *part = strstr(dir, parent);
			if (part) dir = &part[strlen(parent) - 1];
		}
		
		snprintf(zc.zc_value, sizeof(zc.zc_value), "\\??\\%c:%s",
			tolower(driveletter[0]), dir);
	}

	// Convert Unix slash to Win32 backslash
	for (int i = 0; zc.zc_value[i]; i++)
		if (zc.zc_value[i] == '/')
			zc.zc_value[i] = '\\'; // "\\??\\c:\\BOOM\\lower"
#ifdef DEBUG
	fprintf(stderr, "zmount(%s,'%s') hasprop %d ispool %d\n",
		zhp->zfs_name, zc.zc_value, hasprop, ispool); fflush(stderr);
#endif
	ret = zfs_ioctl(zhp->zfs_hdl, ZFS_IOC_MOUNT, &zc);


	if (ret == 0) {
		// Tell Explorer we have a new drive
		// Whats the deal here with this header file - did not like to be included.
//#include <Shlobj.h>
		struct mnttab entry;

		// Locate this mount
		if (libzfs_mnttab_find(zhp->zfs_hdl, zhp->zfs_name, &entry) == 0) {

			// If we get a driveletter, we tell Explorer. Otherwise not required.
			if (entry.mnt_mountp[1] == ':') { // "E:\ " -> "E:"
				entry.mnt_mountp[2] = 0;
				SHChangeNotify(SHCNE_DRIVEADD, SHCNF_PATH, entry.mnt_mountp, NULL);
			}
		}
	}

#ifdef DEBUG
	fprintf(stderr, "zmount(%s,%s) returns %d\n",
		zhp->zfs_name, dir, ret);

	fprintf(stderr, "'%s' mounted on %s\r\n", zc.zc_name, zc.zc_value);
#endif

	// For BOOM, we get back 
	// "\\Device\\Volume{0b1bb601-af0b-32e8-a1d2-54c167af6277}\\"
	// which is the volume name, and the FS device attached to it is:
	// "\\\??\\\Volume{7cc383a0-beac-11e7-b56d-02150b22a130}"
	// and if change that to "\\\\?\\Volume{7cc383a0-beac-11e7-b56d-02150b22a130}\\";
	// we can use GetVolumePathNamesForVolumeName() to get back "\\DosDevices\\E".
#if 0
	char out[MAXPATHLEN];
	DWORD outlen;

	//if (QueryDosDevice(
	//	"G:",
	//	out, MAXPATHLEN) > 0)
	//	fprintf(stderr, "'%s' mounted on %s\r\n", zc.zc_name, zc.zc_value);
	//else
	//	fprintf(stderr, "QueryDos getlast 0x%x\n", GetLastError());

	outlen = 0;
	// these give error 0x57 (invalid parameter)
	//char *name = "\\\\?\\ZFS{0b1bb601-af0b-32e8-a1d2-54c167af6277}\\";
	//char *name = "\\\\?\\Device\\ZFS{0b1bb601-af0b-32e8-a1d2-54c167af6277}\\";
	//char *name = "\\\\?\\Device\\Volume{0b1bb601-af0b-32e8-a1d2-54c167af6277}\\";
	//char *name = "\\\\?\\DosDevices\\Global\\Volume{0b1bb601-af0b-32e8-a1d2-54c167af6277}\\";
	// this gives error 0x1 ERROR_INVALID_FUNCTION
	//char *name = "\\\\?\\Volume{0b1bb601-af0b-32e8-a1d2-54c167af6277}\\";

	//char *name = "\\\\?\\Volume{0b1bb601-af0b-32e8-a1d2-54c167af6277}\\";
	char *name = zc.zc_value;

	// Kernel returns "\\Device\\Volume{0b1bb601-af0b-32e8-a1d2-54c167af6277}\\"
	if (strncmp(name, "\\Device\\Volume{", 15) == 0) {
		strlcpy(&name[0], "\\\\?\\", sizeof(zc.zc_value));
		strlcpy(&name[4], &name[8], sizeof(zc.zc_value));
		strlcat(name, "\\", sizeof(zc.zc_value));
	}

	fprintf(stderr, "Looking up '%s'\r\n", name);
	ret = GetVolumePathNamesForVolumeName(name, out, MAXPATHLEN, &outlen);

	if (ret != 1)
		fprintf(stderr, "GetVolumePathNamesForVolumeName ret %d outlen %d GetLastError 0x%x\n", ret, outlen, GetLastError());
	if (outlen > 0 && ret > 0) {
		char *NameIdx;
		fprintf(stderr, "%s: ", zc.zc_name);
		for (NameIdx = out;
			NameIdx[0] != '\0';
			NameIdx += strlen(NameIdx) + 1)
		{
			fprintf(stderr, "  %s", NameIdx);
		}
		fprintf(stderr, "\r\n");
	}
#endif

#if 0
	fprintf(stderr, "Trying mountmgr\r\n");
#define MOUNTMGR_DOS_DEVICE_NAME L"\\\\.\\MountPointManager"
	typedef struct _MOUNTMGR_MOUNT_POINT {
		ULONG  SymbolicLinkNameOffset;
		USHORT SymbolicLinkNameLength;
		ULONG  UniqueIdOffset;
		USHORT UniqueIdLength;
		ULONG  DeviceNameOffset;
		USHORT DeviceNameLength;
	} MOUNTMGR_MOUNT_POINT, *PMOUNTMGR_MOUNT_POINT;
#define MOUNTMGRCONTROLTYPE ((ULONG) 'm')
#define IOCTL_MOUNTMGR_QUERY_POINTS CTL_CODE(MOUNTMGRCONTROLTYPE, 2, METHOD_BUFFERED, FILE_ANY_ACCESS)

	HANDLE mgr = CreateFileW(MOUNTMGR_DOS_DEVICE_NAME, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, INVALID_HANDLE_VALUE);
	if (mgr == INVALID_HANDLE_VALUE) return ret;

	MOUNTMGR_MOUNT_POINT input = { 0 };
	outlen = 0;
	fprintf(stderr, "sending\r\n");
	if (DeviceIoControl(mgr, IOCTL_MOUNTMGR_QUERY_POINTS, &input, sizeof(input),
		out, MAXPATHLEN, &outlen, NULL) != 0) {
		fprintf(stderr, "mountmgr returned success %d\r\n", outlen);
	}
	fprintf(stderr, "return outlen %d: GetLastError %d\r\n", outlen, GetLastError());
	CloseHandle(mgr);
#endif

#if 0
	typedef struct _REPARSE_DATA_BUFFER {
		/**
		* Reparse point tag. Must be a Microsoft reparse point tag.
		*/
		ULONG ReparseTag;
		/**
		* Size, in bytes, of the reparse data in the DataBuffer member.
		*/
		USHORT ReparseDataLength;
		/**
		* Length, in bytes, of the unparsed portion of the file name pointed
		* to by the FileName member of the associated file object.
		*/
		USHORT Reserved;
		union {
			struct {
				/** Offset, in bytes, of the substitute name string in the PathBuffer array. */
				USHORT SubstituteNameOffset;
				/** Length, in bytes, of the substitute name string. */
				USHORT SubstituteNameLength;
				/** Offset, in bytes, of the print name string in the PathBuffer array. */
				USHORT PrintNameOffset;
				/** Length, in bytes, of the print name string. */
				USHORT PrintNameLength;
				/** Used to indicate if the given symbolic link is an absolute or relative symbolic link. */
				ULONG Flags;
				/** First character of the path string. This is followed in memory by the remainder of the string. */
				WCHAR PathBuffer[1];
			} SymbolicLinkReparseBuffer;
			struct {
				/** Offset, in bytes, of the substitute name string in the PathBuffer array. */
				USHORT SubstituteNameOffset;
				/** Length, in bytes, of the substitute name string. */
				USHORT SubstituteNameLength;
				/** Offset, in bytes, of the print name string in the PathBuffer array. */
				USHORT PrintNameOffset;
				/** Length, in bytes, of the print name string. */
				USHORT PrintNameLength;
				/** First character of the path string. */
				WCHAR PathBuffer[1];
			} MountPointReparseBuffer;
			struct {
				/** Microsoft-defined data for the reparse point. */
				UCHAR DataBuffer[1];
			} GenericReparseBuffer;
		} DUMMYUNIONNAME;
	} REPARSE_DATA_BUFFER, *PREPARSE_DATA_BUFFER;
#pragma warning(pop)
#define REPARSE_DATA_BUFFER_HEADER_SIZE                                        \
FIELD_OFFSET(REPARSE_DATA_BUFFER, GenericReparseBuffer)

	PREPARSE_DATA_BUFFER reparseData;
	USHORT bufferLength;
	USHORT targetLength;
	BOOL result;
	ULONG resultLength;
	WCHAR targetDeviceName[MAX_PATH];

	fprintf(stderr, "making a reparse point\r\n");
	HANDLE handle;
	handle = CreateFile(L"\\DosDevices\\C:\\BOOM", GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
		FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
		NULL);
	if (handle == INVALID_HANDLE_VALUE) {
		fprintf(stderr, "failed %u 0x%x\r\n", GetLastError(), GetLastError());
	}
	else {
		fprintf(stderr, "reparse point ok\r\n");
	}

	ZeroMemory(targetDeviceName, sizeof(targetDeviceName));
	wcscat_s(targetDeviceName, MAX_PATH, L"\\??\\Volume{7cc383a0-beac-11e7-b56d-02150b22a130}");

	targetLength = (USHORT)wcslen(targetDeviceName) * sizeof(WCHAR);
	bufferLength =
		FIELD_OFFSET(REPARSE_DATA_BUFFER, MountPointReparseBuffer.PathBuffer) +
		targetLength + sizeof(WCHAR) + sizeof(WCHAR);

	reparseData = (PREPARSE_DATA_BUFFER)malloc(bufferLength);
	if (reparseData == NULL) {
		CloseHandle(handle);
		return FALSE;
	}

	ZeroMemory(reparseData, bufferLength);

	reparseData->ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
	reparseData->ReparseDataLength =
		bufferLength - REPARSE_DATA_BUFFER_HEADER_SIZE;

	reparseData->MountPointReparseBuffer.SubstituteNameOffset = 0;
	reparseData->MountPointReparseBuffer.SubstituteNameLength = targetLength;
	reparseData->MountPointReparseBuffer.PrintNameOffset =
		targetLength + sizeof(WCHAR);
	reparseData->MountPointReparseBuffer.PrintNameLength = 0;

	RtlCopyMemory(reparseData->MountPointReparseBuffer.PathBuffer,
		targetDeviceName, targetLength);

	result = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, reparseData,
		bufferLength, NULL, 0, &resultLength, NULL);

	CloseHandle(handle);
	free(reparseData);

	if (result) {
		fprintf(stderr, "CreateMountPoint  -> %S success\n", 
			targetDeviceName);
	}
	else {
		WCHAR errorMsg[256];
		FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(),
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), errorMsg, 256,
			NULL);
		fprintf(stderr, "CreateMountPoint -> %S failed: (%d) %S", 
			targetDeviceName, GetLastError(), errorMsg);
	}





	fprintf(stderr, "trying to set the mountpoint\r\n");

	ret = SetVolumeMountPoint(
		L"C:\\BOOM\\",
		L"\\\\?\\Volume{7cc383a0-beac-11e7-b56d-02150b22a130}\\"  //  This string must be of the form "\\?\Volume{GUID}\" 
	);

	fprintf(stderr, "trying to set the mountpoint: %d %d\r\n", ret, GetLastError());

#endif

	return ret;
}


static int
do_unmount_impl(zfs_handle_t *zhp, const char *mntpt, int flags)
{
	int ret = 0;

	// mount 'spec' "tank/joe" on path 'dir' "/home/joe".
	fprintf(stderr, "zunmount(%s,%s) running\r\n",
		zhp->zfs_name, mntpt); fflush(stderr);
	zfs_cmd_t zc = { "\0" };

	(void)strlcpy(zc.zc_name, zhp->zfs_name, sizeof(zc.zc_name));
	(void)strlcpy(zc.zc_value, mntpt, sizeof(zc.zc_value));

	ret = zfs_ioctl(zhp->zfs_hdl, ZFS_IOC_UNMOUNT, &zc);

	if (!ret) {
		// if mountpoint is a folder, we need to turn it back from JUNCTION
		// to a real folder
		char mtpt_prop[ZFS_MAXPROPLEN];
		char driveletter[MAX_PATH];
		verify(zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, mtpt_prop,
			sizeof(mtpt_prop), NULL, NULL, 0, B_FALSE) == 0);
		verify(zfs_prop_get(zhp, ZFS_PROP_DRIVELETTER, driveletter,
			sizeof(driveletter), NULL, NULL, 0, B_FALSE) == 0);
		// if mountpoint starts with '/' we assume that it is a path to a directory 
		// make sure we didn't mount as driveletter
		if (mtpt_prop && mtpt_prop[0] == '/' && 
			(strstr(driveletter, "-") != 0 || strstr(driveletter, "off") != 0) &&
			(mntpt && strstr(mntpt, ":\\") == 0)) {
			fprintf(stderr, "recreate mountpoint %s\n", mtpt_prop); fflush(stderr);
			BOOL val = RemoveDirectoryA(mtpt_prop);
			if (!val) {
				if (GetLastError() != ERROR_FILE_NOT_FOUND)
					fprintf(stderr, "RemoveDirectoryA returns false, last error %lu\n", GetLastError()); fflush(stderr);
			} else {
				val = CreateDirectoryA(mtpt_prop, NULL);
				if (!val) 
					fprintf(stderr, "CreateDirectoryA returns false, last error %lu\n", GetLastError()); fflush(stderr);
			}
			
		}
			
	}

	fprintf(stderr, "zunmount(%s,%s) returns %d\n",
		zhp->zfs_name, mntpt, ret);

	return ret;
}


void unmount_snapshots(zfs_handle_t *zhp, const char *mntpt, int flags);

int
do_unmount(zfs_handle_t *zhp, const char *mntpt, int flags)
{

	unmount_snapshots(zhp, mntpt, flags);

	return (do_unmount_impl(zhp, mntpt, flags));
}

/*
 * Given "/Volumes/BOOM" look for any lower mounts with ".zfs/snapshot/"
 * in them - issue unmount.
 */
void
unmount_snapshots(zfs_handle_t *zhp, const char *mntpt, int flags)
{
	struct mnttab entry;
	int len = strlen(mntpt);

	while (getmntent(zhp->zfs_hdl->libzfs_mnttab, &entry) == 0) {
		/* Starts with our mountpoint ? */
		if (strncmp(mntpt, entry.mnt_mountp, len) == 0) {
			/* The next part is "/.zfs/snapshot/" ? */
			if (strncmp("/.zfs/snapshot/", &entry.mnt_mountp[len],
			    15) == 0) {
				/* Unmount it */
				do_unmount_impl(zhp, entry.mnt_mountp, MS_FORCE);
			}
		}
	}
}

int
zfs_mount_delegation_check(void)
{
	return ((geteuid() != 0) ? EACCES : 0);
}

static char *
zfs_snapshot_mountpoint(zfs_handle_t *zhp)
{
	char *dataset_name, *snapshot_mountpoint, *parent_mountpoint;
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	zfs_handle_t *parent;
	char *r;

	dataset_name = zfs_strdup(hdl, zhp->zfs_name);
	if (dataset_name == NULL) {
		(void) fprintf(stderr, gettext("not enough memory"));
		return (NULL);
	}

	r = strrchr(dataset_name, '@');

	if (r == NULL) {
		(void) fprintf(stderr, gettext("snapshot '%s' "
		    "has no '@'\n"), zhp->zfs_name);
		free(dataset_name);
		return (NULL);
	}

	r[0] = 0;

	/* Open the dataset */
	if ((parent = zfs_open(hdl, dataset_name,
	    ZFS_TYPE_FILESYSTEM)) == NULL) {
		(void) fprintf(stderr,
		    gettext("unable to open parent dataset '%s'\n"),
		    dataset_name);
		free(dataset_name);
		return (NULL);
	}

	if (!zfs_is_mounted(parent, &parent_mountpoint)) {
		(void) fprintf(stderr,
		    gettext("parent dataset '%s' must be mounted\n"),
		    dataset_name);
		free(dataset_name);
		zfs_close(parent);
		return (NULL);
	}

	zfs_close(parent);

	snapshot_mountpoint =
	    zfs_asprintf(hdl, "%s/.zfs/snapshot/%s/",
	    parent_mountpoint, &r[1]);

	free(dataset_name);
	free(parent_mountpoint);

	return (snapshot_mountpoint);
}

/*
 * Mount a snapshot; called from "zfs mount dataset@snapshot".
 * Given "dataset@snapshot" construct mountpoint path of the
 * style "/mountpoint/dataset/.zfs/snapshot/$name/". Ensure
 * parent "dataset" is mounted, then issue mount for snapshot.
 */
int
zfs_snapshot_mount(zfs_handle_t *zhp, const char *options,
    int flags)
{
	int ret = 0;
	char *mountpoint;

	/*
	 * The automounting will kick in, and zed mounts it - so
	 * we temporarily disable it
	 */
	uint64_t automount = 0;
	uint64_t saved_automount = 0;
	size_t len = sizeof (automount);
	size_t slen = sizeof (saved_automount);

	if (zfs_is_mounted(zhp, NULL)) {
		return (EBUSY);
	}

	mountpoint = zfs_snapshot_mountpoint(zhp);
	if (mountpoint == NULL)
		return (EINVAL);

	ret = zfs_mount_at(zhp, options, MS_RDONLY | flags,
	    mountpoint);

	/* If zed is running, it can mount it before us */
	if (ret == -1 && errno == EINVAL)
		ret = 0;

	if (ret == 0) {
		(void) fprintf(stderr,
		    gettext("ZFS: snapshot mountpoint '%s'\n"),
		    mountpoint);
	}

	free(mountpoint);

	return (ret);
}

int
zfs_snapshot_unmount(zfs_handle_t *zhp, int flags)
{
	int ret = 0;
	char *mountpoint;

	if (!zfs_is_mounted(zhp, NULL)) {
		return (ENOENT);
	}

	mountpoint = zfs_snapshot_mountpoint(zhp);
	if (mountpoint == NULL)
		return (EINVAL);

	ret = zfs_unmount(zhp, mountpoint, flags);

	free(mountpoint);

	return (ret);
}
