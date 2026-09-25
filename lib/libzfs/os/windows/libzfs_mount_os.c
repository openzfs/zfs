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

#include <sys/zfs_ioctl.h>

// #define	DEBUG

HANDLE
ZFSCreateEvent(char *name)
{
	char eventName[MAX_PATH];
	snprintf(eventName, MAX_PATH, "Global\\MountComplete_{%s}", name);

	HANDLE hEvent = OpenEventA(EVENT_ALL_ACCESS, FALSE, eventName);
	if (hEvent == NULL) {
		// If the event doesn't exist, create it
		hEvent = CreateEventA(NULL, FALSE, FALSE, eventName);
	}
	return (hEvent);
}

/*
 * if (zmount(zhp, zfs_get_name(zhp), mountpoint, MS_OPTIONSTR | flags,
 * MNTTYPE_ZFS, NULL, 0, mntopts, sizeof (mntopts)) != 0) {
 */
int
do_mount(zfs_handle_t *zhp, const char *dir, const char *optptr, int mflag)
{
	int ret = 0;
	int ispool = 0;
	char driveletter[100] = "off";
	int hasprop = 0;

	/* Linux remounts to set atime etc. */
	if (strstr(optptr, MNTOPT_REMOUNT) != NULL)
		return (0);

	/* mount 'spec' "tank/joe" on path 'dir' "/home/joe". */
#ifdef DEBUG
	fprintf(stderr,
	    "zmount running, emulating Unix mount: '%s'\r\n",
	    dir);
	fflush(stderr);
#endif
	zfs_cmd_t zc = { "\0" };

	if (zhp) {
		if (zhp->zpool_hdl &&
		    strcmp(zpool_get_name(zhp->zpool_hdl),
		    zfs_get_name(zhp)) == 0)
			ispool = 1;

		ret = zfs_prop_get(zhp, ZFS_PROP_DRIVELETTER, driveletter,
		    sizeof (driveletter), NULL, NULL, 0, B_FALSE);

		hasprop = ret ? 0 : 1;
		if (!ret &&
		    strncmp("-", driveletter, sizeof (driveletter)) == 0)
			hasprop = 0;
	}
	/*
	 * if !hasprop and ispool -> hasprop=1 & driveletter=on
	 * if hasprop = on -> driveletter = ?
	 * if hasprop = off
	 */
	if (!hasprop && ispool) {
		strcpy(driveletter, "on");
		hasprop = 1;
	}
	if (strcmp("off", driveletter) == 0)
		hasprop = 0;
	else if (strcmp("on", driveletter) == 0)
		strcpy(driveletter, "?");

	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));
	(void) strlcpy(zc.zc_value, dir, sizeof (zc.zc_value));

	// Make sure we get a fresh view
	libzfs_mnttab_cache(zhp->zfs_hdl, B_FALSE);

	if (zhp->zfs_type != ZFS_TYPE_SNAPSHOT) {
		/*
		 * If hasprop is set, use 'driveletter' and ignore mountpoint
		 * path. if !hasprop && rootds same
		 */
		if (hasprop) {
			/* We just pass "\\??\\X:" to kernel. */
			snprintf(zc.zc_value, sizeof (zc.zc_value), "\\??\\%c:",
			    tolower(driveletter[0]));
		} else {
			/*
			 * We are to mount with path. Attempt to find parent
			 * driveletter, if any. Otherwise assume c:/
			 *
			 * dir has the full mountpoint "/hello/world/visitor"
			 * walk parents to find driveletter
			 */
			driveletter[0] = 'c';

			boolean_t stop_loop = FALSE;

			// Get parent
			char parent[ZFS_MAX_DATASET_NAME_LEN] = "";
			zfs_parent_name(zhp, parent, sizeof (parent));
			char parent_mountpoint[ZFS_MAXPROPLEN];

			do {

				// Open parent
				char mtpt_prop[ZFS_MAXPROPLEN];
				zfs_handle_t *pzhp = NULL;

				pzhp = make_dataset_handle(zhp->zfs_hdl,
				    parent);
				if (!pzhp) {
					fprintf(stderr,
					    "Unable to open parent '%s'\r\n",
					    parent);
					break;
				}

				// Check if it has driveletter, we fetch it from
				// mounts, since we might not know which it got.
				// Or if we are the pool
				ret = zfs_prop_get(pzhp, ZFS_PROP_DRIVELETTER,
				    driveletter, sizeof (driveletter), NULL,
				    NULL, 0, B_FALSE);
				if (!ret &&
				    strncmp("-", driveletter,
				    sizeof (driveletter)) == 0) {

					if (strcmp(
					    zpool_get_name(pzhp->zpool_hdl),
					    zfs_get_name(pzhp)) != 0)
						ret = ENOENT;
				}

				if (ret == 0) {
					// Fetch driveletter
					int missing;
					struct mnttab entry = { 0 };

					memset(&entry, 0,
					    sizeof (entry));

					// Might take a bit to settle to a
					// driveletter
					int retry = 0;
				do {
					missing = libzfs_mnttab_find(
					    zhp->zfs_hdl,
					    parent,
					    &entry);

					if (!missing &&
					    (entry.mnt_mountp[1] == ':'))
						driveletter[0] =
						    entry.mnt_mountp[0];

					if (toupper(driveletter[0]) >= 'A' &&
					    toupper(driveletter[0]) <= 'Z')
						break;
					Sleep(250);
#ifdef DEBUG
					fprintf(stderr,
					    "waiting, looping\r\n");
#endif
				} while (retry++ < 10);

					zfs_prop_get(pzhp,
					    ZFS_PROP_MOUNTPOINT,
					    parent_mountpoint,
					    sizeof (parent_mountpoint),
					    NULL, NULL, 0,
					    B_FALSE);

					stop_loop = TRUE;
				}

				// Don't eat the parent name if we are stopping
				if (!stop_loop &&
				    zfs_parent_name(pzhp, parent,
				    sizeof (parent)))
					stop_loop = TRUE;
				zfs_close(pzhp);

			} while (!stop_loop);
#ifdef DEBUG
			fprintf(stderr,
			    "Ultimate parent '%s' with driveletter %c:, "
			    "subtract mountpoint '%s'\r\n",
			    parent, driveletter[0], parent_mountpoint);
#endif
			char *remaining_path;
			int skip;
			remaining_path = dir;
			skip = strlen(parent_mountpoint);
			if (skip < strlen(dir) &&
			    strncmp(dir, parent_mountpoint, skip) == 0)
				remaining_path = &dir[skip];
#ifdef DEBUG
			fprintf(stderr, "Skipping %d ('%s') of '%s' -> '%s'\n",
			    skip, parent_mountpoint, dir, remaining_path);
#endif
			snprintf(zc.zc_value, sizeof (zc.zc_value),
			    "\\??\\%c:%s",
			    driveletter[0], remaining_path);

		} // has driveletter prop

	} else {
		/* snapshot */
		snprintf(zc.zc_value, sizeof (zc.zc_value), "\\??\\%s",
		    dir);
		zc.zc_cleanup_fd = MNT_RDONLY;
	}

	fprintf(stderr,
	    "sending mountpoint: '%s'\r\n",
	    zc.zc_value);
	fflush(stderr);

	/* Convert Unix slash to Win32 backslash */
	for (int i = 0; zc.zc_value[i]; i++)
		if (zc.zc_value[i] == '/')
			zc.zc_value[i] = '\\'; /* "\\??\\c:\\BOOM\\lower" */
#ifdef DEBUG
	fprintf(stderr, "zmount(%s,'%s') hasprop %d ispool %d\n",
	    zhp->zfs_name, zc.zc_value, hasprop, ispool);
	fflush(stderr);
#endif

	HANDLE h = ZFSCreateEvent(zc.zc_name);

	ret = zfs_ioctl(zhp->zfs_hdl, ZFS_IOC_MOUNT, &zc);
	/* zfs_ioctl returns -1 on failure; translate to the actual errno */
	if (ret != 0)
		ret = errno ? errno : EIO;

	if (ret == 0) {

#ifdef DEBUG
		fprintf(stderr, "waiting ... \n");
		fflush(stderr);
#endif
		// Wait for kernel to signal mount is completed.
		DWORD waitResult = 0;
		if (h) {
			waitResult = WaitForSingleObject(h, 10 * 1000);
			CloseHandle(h);
		}

#ifdef DEBUG
		fprintf(stderr, "kernel said wait is over %d\n", waitResult);
		fflush(stderr);
#endif

		/*
		 * Tell Explorer we have a new drive
		 * Whats the deal here with this header file -
		 * did not like to be included.
		 * #include <Shlobj.h>
		 */
		struct mnttab entry;

		/* Locate this mount */
		if (libzfs_mnttab_find(zhp->zfs_hdl, zhp->zfs_name,
		    &entry) == 0) {
			/*
			 * If we get a driveletter, we tell Explorer.
			 * Otherwise not required.
			 */
			if (entry.mnt_mountp[1] == ':') { // "E:\ " -> "E:"
				entry.mnt_mountp[2] = 0;
				SHChangeNotify(SHCNE_DRIVEADD, SHCNF_PATH,
				    entry.mnt_mountp, NULL);
			}
		}
	}

#ifdef DEBUG
	fprintf(stderr, "zmount(%s,%s) returns %d\n",
	    zhp->zfs_name, dir, ret);

	fprintf(stderr, "'%s' mounted on %s\r\n", zc.zc_name, zc.zc_value);
#endif
	/*
	 * For BOOM, we get back
	 * "\\Device\\Volume{0b1bb601-af0b-32e8-a1d2-54c167af6277}\\"
	 * which is the volume name, and the FS device attached to it is:
	 * "\\\??\\\Volume{7cc383a0-beac-11e7-b56d-02150b22a130}"
	 * and if change that to
	 * "\\\\?\\Volume{7cc383a0-beac-11e7-b56d-02150b22a130}\\";
	 *  we can use GetVolumePathNamesForVolumeName()
	 * to get back "\\DosDevices\\E".
	 */

	return (ret);
}


static int
do_unmount_impl(zfs_handle_t *zhp, const char *mntpt, int flags)
{
	int ret = 0;

	/* mount 'spec' "tank/joe" on path 'dir' "/home/joe". */
	fprintf(stderr, "zunmount(%s,%s) running\r\n",
	    zhp->zfs_name, mntpt);
	fflush(stderr);
	zfs_cmd_t zc = { "\0" };

	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));
	(void) strlcpy(zc.zc_value, mntpt, sizeof (zc.zc_value));

	ret = zfs_ioctl(zhp->zfs_hdl, ZFS_IOC_UNMOUNT, &zc);
	/* zfs_ioctl returns -1 on failure; translate to the actual errno */
	if (ret != 0)
		ret = errno ? errno : EIO;

	if (!ret) {
		/*
		 * if mountpoint is a folder, we need to turn it back
		 * from JUNCTION to a real folder
		 */
		char mtpt_prop[ZFS_MAXPROPLEN];
		char driveletter[MAX_PATH];
		/* snapshots dont have mountpoint property */
		if (zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, mtpt_prop,
		    sizeof (mtpt_prop), NULL, NULL, 0, B_FALSE) == 0) {
			verify(zfs_prop_get(zhp, ZFS_PROP_DRIVELETTER,
			    driveletter, sizeof (driveletter), NULL,
			    NULL, 0, B_FALSE) == 0);
			/*
			 * if mountpoint starts with '/' we assume that it is a
			 * path to a directory make sure we didn't mount as
			 * driveletter
			 */
			if (mtpt_prop && mtpt_prop[0] == '/' &&
			    (strstr(driveletter, "-") != 0 ||
			    strstr(driveletter, "off") != 0) &&
			    (mntpt && strstr(mntpt, ":\\") == 0)) {
				BOOL val = RemoveDirectoryA(mtpt_prop);
				if (!val) {
				} else {
					val = CreateDirectoryA(mtpt_prop, NULL);
				}

			}
		}
	}

	fprintf(stderr, "zunmount(%s,%s) returns %d\n",
	    zhp->zfs_name, mntpt, ret);

	return (ret);
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
	FILE *mnttab;

	if ((mnttab = fopen(MNTTAB, "re")) == NULL)
		return;

	while (getmntent(mnttab, &entry) == 0) {
		/* Starts with our mountpoint ? */
		if (strncmp(mntpt, entry.mnt_mountp, len) == 0) {
			/* The next part is "/.zfs/snapshot/" ? */
			if (strncmp("/.zfs/snapshot/", &entry.mnt_mountp[len],
			    15) == 0) {
				/* Unmount it */
				zfs_handle_t *szhp;
				szhp =	make_dataset_handle(zhp->zfs_hdl,
				    entry.mnt_special);
				if (szhp == NULL) {
					fprintf(stderr,
					    "Unable to unmount '%s'\r\n",
					    entry.mnt_special);
					continue;
				}

				do_unmount_impl(szhp, entry.mnt_mountp,
				    MS_FORCE);
				zfs_close(szhp);
			}
		}
	}
	fclose(mnttab);
}

int
zfs_mount_delegation_check(void)
{
	return ((geteuid() != 0) ? EACCES : 0);
}

/*
 * Windows has no equivalent to Linux's mount_setattr(2). As with FreeBSD
 * and macOS, fall back to a full remount: zfs_mount()'s option-building
 * logic reads the dataset's current properties fresh every time, so a
 * remount picks up whatever namespace property (atime, exec, setuid,
 * etc.) just changed without needing to translate nspflags into
 * individual mount flags.
 */
int
zfs_mount_setattr(zfs_handle_t *zhp, uint32_t nspflags)
{
	(void) nspflags;
	return (zfs_mount(zhp, MNTOPT_REMOUNT, 0));
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

	/* Strip trailing slashes so path becomes "E:/.zfs/snapshot/snap" */
	{
		size_t mplen = strlen(parent_mountpoint);
		while (mplen > 1 && (parent_mountpoint[mplen - 1] == '/' ||
		    parent_mountpoint[mplen - 1] == '\\'))
			parent_mountpoint[--mplen] = '\0';
	}

	snapshot_mountpoint =
	    zfs_asprintf(hdl, "%s/.zfs/snapshot/%s",
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

/* Called from the tail end of zpool_disable_datasets() */
void
zpool_disable_datasets_os(zpool_handle_t *zhp, boolean_t force)
{
	(void) zhp, (void) force;
}

/* Called from the tail end of zfs_unmount() */
void
zpool_disable_volume_os(const char *name)
{
	(void) name;
}
