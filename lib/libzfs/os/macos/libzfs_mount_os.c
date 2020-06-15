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
#include <os/macos/zfs/sys/zfs_mount.h>
#include <libzfs.h>

#include "libzfs_impl.h"
#include <thread_pool.h>
#include <sys/sysctl.h>


/*
 * The default OpenZFS icon. Compare against known values to see if it needs
 * updating. Allowing users to set own.
 * No file: copy icon
 * correct size: do nothing
 * other size: user custom icon, do nothing
 */

/* icon name on root of a mount */
#define	MOUNT_POINT_CUSTOM_ICON ".VolumeIcon.icns"

/* source icon name from inside zfs.kext bundle */
#define	CUSTOM_ICON_PATH \
	KERNEL_MODPREFIX "/zfs.kext/Contents/Resources/VolumeIcon.icns"

#include <sys/xattr.h>


/*
 * On OSX we can set the icon to an Open ZFS specific one, just to be extra
 * shiny
 */
static void
zfs_mount_seticon(const char *mountpoint)
{
	/* For a root file system, add a volume icon. */
	ssize_t attrsize;
	uint16_t finderinfo[16];
	struct stat sbuf;
	char *path = NULL;
	FILE *dstfp = NULL, *srcfp = NULL;
	unsigned char buf[1024];
	unsigned int red;

	if (asprintf(&path, "%s/%s", mountpoint, MOUNT_POINT_CUSTOM_ICON) == -1)
		return;

	/* If we can stat it, and it has a size, leave it be. */
	if ((stat(path, &sbuf) == 0 && sbuf.st_size > 0))
		goto out;

	/* Looks like we should copy the icon over */

	/* check if we can read in the default ZFS icon */
	srcfp = fopen(CUSTOM_ICON_PATH, "r");

	/* No source icon */
	if (!srcfp)
		goto out;

	/* Open the output icon for writing */
	dstfp = fopen(path, "w");
	if (!dstfp)
		goto out;

	/* Copy icon */
	while ((red = fread(buf, 1, sizeof (buf), srcfp)) > 0)
		(void) fwrite(buf, 1, red, dstfp);

	/* We have copied it, set icon */
	attrsize = getxattr(mountpoint, XATTR_FINDERINFO_NAME, &finderinfo,
	    sizeof (finderinfo), 0);
	if (attrsize != sizeof (finderinfo))
		(void) memset(&finderinfo, 0, sizeof (finderinfo));
	if ((finderinfo[4] & BE_16(0x0400)) == 0) {
		finderinfo[4] |= BE_16(0x0400);
		(void) setxattr(mountpoint, XATTR_FINDERINFO_NAME, &finderinfo,
		    sizeof (finderinfo), 0);
	}

	/* Now tell Finder to update */
#if 0
	int fd = -1;
	strlcpy(template, mountpoint, sizeof (template));
	strlcat(template, "/tempXXXXXX", sizeof (template));
	if ((fd = mkstemp(template)) != -1) {
		unlink(template); // Just delete it right away
		close(fd);
	}
#endif

out:
	if (dstfp != NULL)
		fclose(dstfp);
	if (srcfp != NULL)
		fclose(srcfp);
	if (path != NULL)
		free(path);
}

/*
 * if (zmount(zhp, zfs_get_name(zhp), mountpoint, MS_OPTIONSTR | flags,
 * MNTTYPE_ZFS, NULL, 0, mntopts, sizeof (mntopts)) != 0) {
 */
int
do_mount(zfs_handle_t *zhp, const char *dir, char *optptr, int mflag)
{
	int rv;
	const char *spec = zfs_get_name(zhp);
	const char *fstype = MNTTYPE_ZFS;
	struct zfs_mount_args mnt_args;
	char *rpath = NULL;
	zfs_cmd_t zc = { "\0" };
	int devdisk = ZFS_DEVDISK_POOLONLY;
	int ispool = 0;  // the pool dataset, that is
	int optlen = 0;

	assert(spec != NULL);
	assert(dir != NULL);
	assert(fstype != NULL);
	assert(mflag >= 0);

	if (optptr != NULL)
		optlen = strlen(optptr);

	/*
	 * Figure out if we want this mount as a /dev/diskX mount, if so
	 * ask kernel to create one for us, then use it to mount.
	 */

	// Use dataset name by default
	mnt_args.fspec = spec;

	/*
	 * Lookup the dataset property devdisk, and depending on its
	 * setting, we need to create a /dev/diskX for the mount
	 */
	if (zhp) {

		/* If we are in zfs-tests, no devdisks */
		if (getenv("__ZFS_MAIN_MOUNTPOINT_DIR") != NULL)
			devdisk = ZFS_DEVDISK_OFF;
		else
			devdisk = zfs_prop_get_int(zhp, ZFS_PROP_DEVDISK);

		if (zhp && zhp->zpool_hdl &&
		    strcmp(zpool_get_name(zhp->zpool_hdl),
		    zfs_get_name(zhp)) == 0)
			ispool = 1;

		if ((devdisk == ZFS_DEVDISK_ON) ||
		    ((devdisk == ZFS_DEVDISK_POOLONLY) &&
		    ispool)) {
			(void) strlcpy(zc.zc_name, zhp->zfs_name,
			    sizeof (zc.zc_name));
			zc.zc_value[0] = 0;

			rv = zfs_ioctl(zhp->zfs_hdl, ZFS_IOC_PROXY_DATASET,
			    &zc);

#ifdef DEBUG
			if (rv)
				fprintf(stderr,
				    "proxy dataset returns %d '%s'\n",
				    rv, zc.zc_value);
#endif

			// Mount using /dev/diskX, use temporary buffer to
			// give it full name
			if (rv == 0) {
				snprintf(zc.zc_name, sizeof (zc.zc_name),
				    "/dev/%s", zc.zc_value);
				mnt_args.fspec = zc.zc_name;
			}
		}
	}

	// We don't pass flags to XNU, we use optstr
	mflag = 0;

	// Some arguments need to be told to XNU
	if (strstr(optptr, "remount") != NULL)
		mflag |= MNT_UPDATE;

	mnt_args.mflag = mflag;
	mnt_args.optptr = optptr;
	mnt_args.optlen = optlen;
	mnt_args.struct_size = sizeof (mnt_args);

	/*
	 * There is a bug in XNU where /var/tmp is resolved as
	 * "private/var/tmp" without the leading "/", and both mount(2) and
	 * diskutil mount avoid this by calling realpath() first. So we will
	 * do the same.
	 */
	rpath = realpath(dir, NULL);

#ifdef ZFS_DEBUG
	printf("%s calling mount with fstype %s, %s %s, fspec %s, mflag %d,"
	    " optptr %s, optlen %d, devdisk %d, ispool %d\n",
	    __func__, fstype, (rpath ? "rpath" : "dir"),
	    (rpath ? rpath : dir), mnt_args.fspec, mflag, optptr, optlen,
	    devdisk, ispool);
#endif
	rv = mount(fstype, rpath ? rpath : dir, mflag, &mnt_args);

	if (rpath) free(rpath);

	/* Check if we need to create/update icon */
	if (rv == 0)
		zfs_mount_seticon(dir);

	return (rv);
}

static int
do_unmount_impl(const char *mntpt, int flags)
{
	char force_opt[] = "force";
	char *argv[7] = {
		"/usr/sbin/diskutil",
		"unmount",
		NULL, NULL, NULL, NULL };
	int rc, count = 2;

	if (flags & MS_FORCE) {
		argv[count] = force_opt;
		count++;
	}

	argv[count] = (char *)mntpt;
	rc = libzfs_run_process(argv[0], argv, STDOUT_VERBOSE|STDERR_VERBOSE);

	/*
	 * There is a bug, where we can not unmount, with the error
	 * already unmounted, even though it wasn't. But it is easy
	 * to work around by calling 'umount'. Until a real fix is done...
	 * re-test this: 202004/lundman
	 */
	if (rc != 0) {
		char *argv[7] = {
		    "/sbin/umount",
		    NULL, NULL, NULL, NULL };
		int rc, count = 1;

		fprintf(stderr, "Fallback umount called\r\n");
		if (flags & MS_FORCE) {
			argv[count] = "-f";
			count++;
		}
		argv[count] = (char *)mntpt;
		rc = libzfs_run_process(argv[0], argv,
		    STDOUT_VERBOSE|STDERR_VERBOSE);
	}

	return (rc ? EINVAL : 0);
}


void unmount_snapshots(libzfs_handle_t *hdl, const char *mntpt, int flags);

int
do_unmount(libzfs_handle_t *hdl, const char *mntpt, int flags)
{
	/*
	 * On OSX, the kernel can not unmount all snapshots for us, as XNU
	 * rejects the unmount before it reaches ZFS. But we can easily handle
	 * unmounting snapshots from userland.
	 */
	unmount_snapshots(hdl, mntpt, flags);

	return (do_unmount_impl(mntpt, flags));
}

/*
 * Given "/Volumes/BOOM" look for any lower mounts with ".zfs/snapshot/"
 * in them - issue unmount.
 */
void
unmount_snapshots(libzfs_handle_t *hdl, const char *mntpt, int flags)
{
	struct mnttab entry;
	int len = strlen(mntpt);

	while (getmntent(hdl->libzfs_mnttab, &entry) == 0) {
		/* Starts with our mountpoint ? */
		if (strncmp(mntpt, entry.mnt_mountp, len) == 0) {
			/* The next part is "/.zfs/snapshot/" ? */
			if (strncmp("/.zfs/snapshot/", &entry.mnt_mountp[len],
			    15) == 0) {
				/* Unmount it */
				do_unmount_impl(entry.mnt_mountp, MS_FORCE);
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

	/* Remember what the user has it set to */
	sysctlbyname("kstat.zfs.darwin.tunable.zfs_auto_snapshot",
	    &saved_automount, &slen, NULL, 0);

	/* Disable automounting */
	sysctlbyname("kstat.zfs.darwin.tunable.zfs_auto_snapshot",
	    NULL, NULL, &automount, len);

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

	/* Restore automount setting */
	sysctlbyname("kstat.zfs.darwin.tunable.zfs_auto_snapshot",
	    NULL, NULL, &saved_automount, len);

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
