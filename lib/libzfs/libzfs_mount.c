/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Copyright (c) 2014, 2022 by Delphix. All rights reserved.
 * Copyright 2016 Igor Kozhukhov <ikozhukhov@gmail.com>
 * Copyright 2017 RackTop Systems.
 * Copyright (c) 2018 Datto Inc.
 * Copyright 2018 OmniOS Community Edition (OmniOSce) Association.
 */

/*
 * Routines to manage ZFS mounts.  We separate all the nasty routines that have
 * to deal with the OS.  The following functions are the main entry points --
 * they are used by mount and unmount and when changing a filesystem's
 * mountpoint.
 *
 *	zfs_is_mounted()
 *	zfs_mount()
 *	zfs_mount_at()
 *	zfs_unmount()
 *	zfs_unmountall()
 *
 * This file also contains the functions used to manage sharing filesystems:
 *
 *	zfs_is_shared()
 *	zfs_share()
 *	zfs_unshare()
 *	zfs_unshareall()
 *	zfs_commit_shares()
 *
 * The following functions are available for pool consumers, and will
 * mount/unmount and share/unshare all datasets within pool:
 *
 *	zpool_enable_datasets()
 *	zpool_disable_datasets()
 */

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <libintl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zone.h>
#include <sys/mntent.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/dsl_crypt.h>

#include <libzfs.h>
#include <libzutil.h>

#include "libzfs_impl.h"
#include <thread_pool.h>

#include <libshare.h>
#include <sys/systeminfo.h>
#define	MAXISALEN	257	/* based on sysinfo(2) man page */

static int mount_tp_nthr = 512;	/* tpool threads for multi-threaded mounting */

static void zfs_mount_task(void *);

static const proto_table_t proto_table[SA_PROTOCOL_COUNT] = {
	[SA_PROTOCOL_NFS] =
	    {ZFS_PROP_SHARENFS, EZFS_SHARENFSFAILED, EZFS_UNSHARENFSFAILED},
	[SA_PROTOCOL_SMB] =
	    {ZFS_PROP_SHARESMB, EZFS_SHARESMBFAILED, EZFS_UNSHARESMBFAILED},
};

static const enum sa_protocol share_all_proto[SA_PROTOCOL_COUNT + 1] = {
	SA_PROTOCOL_NFS,
	SA_PROTOCOL_SMB,
	SA_NO_PROTOCOL
};



static boolean_t
dir_is_empty_stat(const char *dirname)
{
	struct stat st;

	/*
	 * We only want to return false if the given path is a non empty
	 * directory, all other errors are handled elsewhere.
	 */
	if (stat(dirname, &st) < 0 || !S_ISDIR(st.st_mode)) {
		return (B_TRUE);
	}

	/*
	 * An empty directory will still have two entries in it, one
	 * entry for each of "." and "..".
	 */
	if (st.st_size > 2) {
		return (B_FALSE);
	}

	return (B_TRUE);
}

static boolean_t
dir_is_empty_readdir(const char *dirname)
{
	DIR *dirp;
	struct dirent64 *dp;
	int dirfd;

	if ((dirfd = openat(AT_FDCWD, dirname,
	    O_RDONLY | O_NDELAY | O_LARGEFILE | O_CLOEXEC, 0)) < 0) {
		return (B_TRUE);
	}

	if ((dirp = fdopendir(dirfd)) == NULL) {
		(void) close(dirfd);
		return (B_TRUE);
	}

	while ((dp = readdir64(dirp)) != NULL) {

		if (strcmp(dp->d_name, ".") == 0 ||
		    strcmp(dp->d_name, "..") == 0)
			continue;

		(void) closedir(dirp);
		return (B_FALSE);
	}

	(void) closedir(dirp);
	return (B_TRUE);
}

/*
 * Returns true if the specified directory is empty.  If we can't open the
 * directory at all, return true so that the mount can fail with a more
 * informative error message.
 */
static boolean_t
dir_is_empty(const char *dirname)
{
	struct statfs64 st;

	/*
	 * If the statvfs call fails or the filesystem is not a ZFS
	 * filesystem, fall back to the slow path which uses readdir.
	 */
	if ((statfs64(dirname, &st) != 0) ||
	    (st.f_type != ZFS_SUPER_MAGIC)) {
		return (dir_is_empty_readdir(dirname));
	}

	/*
	 * At this point, we know the provided path is on a ZFS
	 * filesystem, so we can use stat instead of readdir to
	 * determine if the directory is empty or not. We try to avoid
	 * using readdir because that requires opening "dirname"; this
	 * open file descriptor can potentially end up in a child
	 * process if there's a concurrent fork, thus preventing the
	 * zfs_mount() from otherwise succeeding (the open file
	 * descriptor inherited by the child process will cause the
	 * parent's mount to fail with EBUSY). The performance
	 * implications of replacing the open, read, and close with a
	 * single stat is nice; but is not the main motivation for the
	 * added complexity.
	 */
	return (dir_is_empty_stat(dirname));
}

/*
 * Checks to see if the mount is active.  If the filesystem is mounted, we fill
 * in 'where' with the current mountpoint, and return 1.  Otherwise, we return
 * 0.
 */
boolean_t
is_mounted(libzfs_handle_t *zfs_hdl, const char *special, char **where)
{
	struct mnttab entry;

	if (libzfs_mnttab_find(zfs_hdl, special, &entry) != 0)
		return (B_FALSE);

	if (where != NULL)
		*where = zfs_strdup(zfs_hdl, entry.mnt_mountp);

	return (B_TRUE);
}

boolean_t
zfs_is_mounted(zfs_handle_t *zhp, char **where)
{
	return (is_mounted(zhp->zfs_hdl, zfs_get_name(zhp), where));
}

/*
 * Checks any higher order concerns about whether the given dataset is
 * mountable, false otherwise.  zfs_is_mountable_internal specifically assumes
 * that the caller has verified the sanity of mounting the dataset at
 * its mountpoint to the extent the caller wants.
 */
static boolean_t
zfs_is_mountable_internal(zfs_handle_t *zhp)
{
	if (zfs_prop_get_int(zhp, ZFS_PROP_ZONED) &&
	    getzoneid() == GLOBAL_ZONEID)
		return (B_FALSE);

	return (B_TRUE);
}

/*
 * Returns true if the given dataset is mountable, false otherwise.  Returns the
 * mountpoint in 'buf'.
 */
static boolean_t
zfs_is_mountable(zfs_handle_t *zhp, char *buf, size_t buflen,
    zprop_source_t *source, int flags)
{
	char sourceloc[MAXNAMELEN];
	zprop_source_t sourcetype;

	if (!zfs_prop_valid_for_type(ZFS_PROP_MOUNTPOINT, zhp->zfs_type,
	    B_FALSE))
		return (B_FALSE);

	verify(zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, buf, buflen,
	    &sourcetype, sourceloc, sizeof (sourceloc), B_FALSE) == 0);

	if (strcmp(buf, ZFS_MOUNTPOINT_NONE) == 0 ||
	    strcmp(buf, ZFS_MOUNTPOINT_LEGACY) == 0)
		return (B_FALSE);

	if (zfs_prop_get_int(zhp, ZFS_PROP_CANMOUNT) == ZFS_CANMOUNT_OFF)
		return (B_FALSE);

	if (!zfs_is_mountable_internal(zhp))
		return (B_FALSE);

	if (zfs_prop_get_int(zhp, ZFS_PROP_REDACTED) && !(flags & MS_FORCE))
		return (B_FALSE);

	if (source)
		*source = sourcetype;

	return (B_TRUE);
}

/*
 * The filesystem is mounted by invoking the system mount utility rather
 * than by the system call mount(2).  This ensures that the /etc/mtab
 * file is correctly locked for the update.  Performing our own locking
 * and /etc/mtab update requires making an unsafe assumption about how
 * the mount utility performs its locking.  Unfortunately, this also means
 * in the case of a mount failure we do not have the exact errno.  We must
 * make due with return value from the mount process.
 *
 * In the long term a shared library called libmount is under development
 * which provides a common API to address the locking and errno issues.
 * Once the standard mount utility has been updated to use this library
 * we can add an autoconf check to conditionally use it.
 *
 * http://www.kernel.org/pub/linux/utils/util-linux/libmount-docs/index.html
 */

static int
zfs_add_option(zfs_handle_t *zhp, char *options, int len,
    zfs_prop_t prop, const char *on, const char *off)
{
	const char *source;
	uint64_t value;

	/* Skip adding duplicate default options */
	if ((strstr(options, on) != NULL) || (strstr(options, off) != NULL))
		return (0);

	/*
	 * zfs_prop_get_int() is not used to ensure our mount options
	 * are not influenced by the current /proc/self/mounts contents.
	 */
	value = getprop_uint64(zhp, prop, &source);

	(void) strlcat(options, ",", len);
	(void) strlcat(options, value ? on : off, len);

	return (0);
}

static int
zfs_add_options(zfs_handle_t *zhp, char *options, int len)
{
	int error = 0;

	error = zfs_add_option(zhp, options, len,
	    ZFS_PROP_ATIME, MNTOPT_ATIME, MNTOPT_NOATIME);
	/*
	 * don't add relatime/strictatime when atime=off, otherwise strictatime
	 * will force atime=on
	 */
	if (strstr(options, MNTOPT_NOATIME) == NULL) {
		error = zfs_add_option(zhp, options, len,
		    ZFS_PROP_RELATIME, MNTOPT_RELATIME, MNTOPT_STRICTATIME);
	}
	error = error ? error : zfs_add_option(zhp, options, len,
	    ZFS_PROP_DEVICES, MNTOPT_DEVICES, MNTOPT_NODEVICES);
	error = error ? error : zfs_add_option(zhp, options, len,
	    ZFS_PROP_EXEC, MNTOPT_EXEC, MNTOPT_NOEXEC);
	error = error ? error : zfs_add_option(zhp, options, len,
	    ZFS_PROP_READONLY, MNTOPT_RO, MNTOPT_RW);
	error = error ? error : zfs_add_option(zhp, options, len,
	    ZFS_PROP_SETUID, MNTOPT_SETUID, MNTOPT_NOSETUID);
	error = error ? error : zfs_add_option(zhp, options, len,
	    ZFS_PROP_NBMAND, MNTOPT_NBMAND, MNTOPT_NONBMAND);

	return (error);
}

int
zfs_mount(zfs_handle_t *zhp, const char *options, int flags)
{
	char mountpoint[ZFS_MAXPROPLEN];

	if (!zfs_is_mountable(zhp, mountpoint, sizeof (mountpoint), NULL,
	    flags))
		return (0);

	return (zfs_mount_at(zhp, options, flags, mountpoint));
}

/*
 * Mount the given filesystem.
 */
int
zfs_mount_at(zfs_handle_t *zhp, const char *options, int flags,
    const char *mountpoint)
{
	struct stat buf;
	char mntopts[MNT_LINE_MAX];
	char overlay[ZFS_MAXPROPLEN];
	char prop_encroot[MAXNAMELEN];
	boolean_t is_encroot;
	zfs_handle_t *encroot_hp = zhp;
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	uint64_t keystatus;
	int remount = 0, rc;

	if (options == NULL) {
		(void) strlcpy(mntopts, MNTOPT_DEFAULTS, sizeof (mntopts));
	} else {
		(void) strlcpy(mntopts, options, sizeof (mntopts));
	}

	if (strstr(mntopts, MNTOPT_REMOUNT) != NULL)
		remount = 1;

	/* Potentially duplicates some checks if invoked by zfs_mount(). */
	if (!zfs_is_mountable_internal(zhp))
		return (0);

	/*
	 * If the pool is imported read-only then all mounts must be read-only
	 */
	if (zpool_get_prop_int(zhp->zpool_hdl, ZPOOL_PROP_READONLY, NULL))
		(void) strlcat(mntopts, "," MNTOPT_RO, sizeof (mntopts));

	/*
	 * Append default mount options which apply to the mount point.
	 * This is done because under Linux (unlike Solaris) multiple mount
	 * points may reference a single super block.  This means that just
	 * given a super block there is no back reference to update the per
	 * mount point options.
	 */
	rc = zfs_add_options(zhp, mntopts, sizeof (mntopts));
	if (rc) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "default options unavailable"));
		return (zfs_error_fmt(hdl, EZFS_MOUNTFAILED,
		    dgettext(TEXT_DOMAIN, "cannot mount '%s'"),
		    mountpoint));
	}

	/*
	 * If the filesystem is encrypted the key must be loaded  in order to
	 * mount. If the key isn't loaded, the MS_CRYPT flag decides whether
	 * or not we attempt to load the keys. Note: we must call
	 * zfs_refresh_properties() here since some callers of this function
	 * (most notably zpool_enable_datasets()) may implicitly load our key
	 * by loading the parent's key first.
	 */
	if (zfs_prop_get_int(zhp, ZFS_PROP_ENCRYPTION) != ZIO_CRYPT_OFF) {
		zfs_refresh_properties(zhp);
		keystatus = zfs_prop_get_int(zhp, ZFS_PROP_KEYSTATUS);

		/*
		 * If the key is unavailable and MS_CRYPT is set give the
		 * user a chance to enter the key. Otherwise just fail
		 * immediately.
		 */
		if (keystatus == ZFS_KEYSTATUS_UNAVAILABLE) {
			if (flags & MS_CRYPT) {
				rc = zfs_crypto_get_encryption_root(zhp,
				    &is_encroot, prop_encroot);
				if (rc) {
					zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
					    "Failed to get encryption root for "
					    "'%s'."), zfs_get_name(zhp));
					return (rc);
				}

				if (!is_encroot) {
					encroot_hp = zfs_open(hdl, prop_encroot,
					    ZFS_TYPE_DATASET);
					if (encroot_hp == NULL)
						return (hdl->libzfs_error);
				}

				rc = zfs_crypto_load_key(encroot_hp,
				    B_FALSE, NULL);

				if (!is_encroot)
					zfs_close(encroot_hp);
				if (rc)
					return (rc);
			} else {
				zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
				    "encryption key not loaded"));
				return (zfs_error_fmt(hdl, EZFS_MOUNTFAILED,
				    dgettext(TEXT_DOMAIN, "cannot mount '%s'"),
				    mountpoint));
			}
		}

	}

	/*
	 * Append zfsutil option so the mount helper allow the mount
	 */
	strlcat(mntopts, "," MNTOPT_ZFSUTIL, sizeof (mntopts));

	/* Create the directory if it doesn't already exist */
	if (lstat(mountpoint, &buf) != 0) {
		if (mkdirp(mountpoint, 0755) != 0) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "failed to create mountpoint: %s"),
			    zfs_strerror(errno));
			return (zfs_error_fmt(hdl, EZFS_MOUNTFAILED,
			    dgettext(TEXT_DOMAIN, "cannot mount '%s'"),
			    mountpoint));
		}
	}

	/*
	 * Overlay mounts are enabled by default but may be disabled
	 * via the 'overlay' property. The -O flag remains for compatibility.
	 */
	if (!(flags & MS_OVERLAY)) {
		if (zfs_prop_get(zhp, ZFS_PROP_OVERLAY, overlay,
		    sizeof (overlay), NULL, NULL, 0, B_FALSE) == 0) {
			if (strcmp(overlay, "on") == 0) {
				flags |= MS_OVERLAY;
			}
		}
	}

	/*
	 * Determine if the mountpoint is empty.  If so, refuse to perform the
	 * mount.  We don't perform this check if 'remount' is
	 * specified or if overlay option (-O) is given
	 */
	if ((flags & MS_OVERLAY) == 0 && !remount &&
	    !dir_is_empty(mountpoint)) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "directory is not empty"));
		return (zfs_error_fmt(hdl, EZFS_MOUNTFAILED,
		    dgettext(TEXT_DOMAIN, "cannot mount '%s'"), mountpoint));
	}

	/* perform the mount */
	rc = do_mount(zhp, mountpoint, mntopts, flags);
	if (rc) {
		/*
		 * Generic errors are nasty, but there are just way too many
		 * from mount(), and they're well-understood.  We pick a few
		 * common ones to improve upon.
		 */
		if (rc == EBUSY) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "mountpoint or dataset is busy"));
		} else if (rc == EPERM) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "Insufficient privileges"));
		} else if (rc == ENOTSUP) {
			int spa_version;

			VERIFY(zfs_spa_version(zhp, &spa_version) == 0);
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "Can't mount a version %llu "
			    "file system on a version %d pool. Pool must be"
			    " upgraded to mount this file system."),
			    (u_longlong_t)zfs_prop_get_int(zhp,
			    ZFS_PROP_VERSION), spa_version);
		} else {
			zfs_error_aux(hdl, "%s", zfs_strerror(rc));
		}
		return (zfs_error_fmt(hdl, EZFS_MOUNTFAILED,
		    dgettext(TEXT_DOMAIN, "cannot mount '%s'"),
		    zhp->zfs_name));
	}

	/* remove the mounted entry before re-adding on remount */
	if (remount)
		libzfs_mnttab_remove(hdl, zhp->zfs_name);

	/* add the mounted entry into our cache */
	libzfs_mnttab_add(hdl, zfs_get_name(zhp), mountpoint, mntopts);
	return (0);
}

/*
 * Unmount a single filesystem.
 */
static int
unmount_one(zfs_handle_t *zhp, const char *mountpoint, int flags)
{
	int error;

	error = do_unmount(zhp, mountpoint, flags);
	if (error != 0) {
		int libzfs_err;

		switch (error) {
		case EBUSY:
			libzfs_err = EZFS_BUSY;
			break;
		case EIO:
			libzfs_err = EZFS_IO;
			break;
		case ENOENT:
			libzfs_err = EZFS_NOENT;
			break;
		case ENOMEM:
			libzfs_err = EZFS_NOMEM;
			break;
		case EPERM:
			libzfs_err = EZFS_PERM;
			break;
		default:
			libzfs_err = EZFS_UMOUNTFAILED;
		}
		if (zhp) {
			return (zfs_error_fmt(zhp->zfs_hdl, libzfs_err,
			    dgettext(TEXT_DOMAIN, "cannot unmount '%s'"),
			    mountpoint));
		} else {
			return (-1);
		}
	}

	return (0);
}

/*
 * Unmount the given filesystem.
 */
int
zfs_unmount(zfs_handle_t *zhp, const char *mountpoint, int flags)
{
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	struct mnttab entry;
	char *mntpt = NULL;
	boolean_t encroot, unmounted = B_FALSE;

	/* check to see if we need to unmount the filesystem */
	if (mountpoint != NULL || ((zfs_get_type(zhp) == ZFS_TYPE_FILESYSTEM) &&
	    libzfs_mnttab_find(hdl, zhp->zfs_name, &entry) == 0)) {
		/*
		 * mountpoint may have come from a call to
		 * getmnt/getmntany if it isn't NULL. If it is NULL,
		 * we know it comes from libzfs_mnttab_find which can
		 * then get freed later. We strdup it to play it safe.
		 */
		if (mountpoint == NULL)
			mntpt = zfs_strdup(hdl, entry.mnt_mountp);
		else
			mntpt = zfs_strdup(hdl, mountpoint);

		/*
		 * Unshare and unmount the filesystem
		 */
		if (zfs_unshare(zhp, mntpt, share_all_proto) != 0) {
			free(mntpt);
			return (-1);
		}
		zfs_commit_shares(NULL);

		if (unmount_one(zhp, mntpt, flags) != 0) {
			free(mntpt);
			(void) zfs_share(zhp, NULL);
			zfs_commit_shares(NULL);
			return (-1);
		}

		libzfs_mnttab_remove(hdl, zhp->zfs_name);
		free(mntpt);
		unmounted = B_TRUE;
	}

	/*
	 * If the MS_CRYPT flag is provided we must ensure we attempt to
	 * unload the dataset's key regardless of whether we did any work
	 * to unmount it. We only do this for encryption roots.
	 */
	if ((flags & MS_CRYPT) != 0 &&
	    zfs_prop_get_int(zhp, ZFS_PROP_ENCRYPTION) != ZIO_CRYPT_OFF) {
		zfs_refresh_properties(zhp);

		if (zfs_crypto_get_encryption_root(zhp, &encroot, NULL) != 0 &&
		    unmounted) {
			(void) zfs_mount(zhp, NULL, 0);
			return (-1);
		}

		if (encroot && zfs_prop_get_int(zhp, ZFS_PROP_KEYSTATUS) ==
		    ZFS_KEYSTATUS_AVAILABLE &&
		    zfs_crypto_unload_key(zhp) != 0) {
			(void) zfs_mount(zhp, NULL, 0);
			return (-1);
		}
	}

	zpool_disable_volume_os(zhp->zfs_name);

	return (0);
}

/*
 * Unmount this filesystem and any children inheriting the mountpoint property.
 * To do this, just act like we're changing the mountpoint property, but don't
 * remount the filesystems afterwards.
 */
int
zfs_unmountall(zfs_handle_t *zhp, int flags)
{
	prop_changelist_t *clp;
	int ret;

	clp = changelist_gather(zhp, ZFS_PROP_MOUNTPOINT,
	    CL_GATHER_ITER_MOUNTED, flags);
	if (clp == NULL)
		return (-1);

	ret = changelist_prefix(clp);
	changelist_free(clp);

	return (ret);
}

/*
 * Unshare a filesystem by mountpoint.
 */
static int
unshare_one(libzfs_handle_t *hdl, const char *name, const char *mountpoint,
    enum sa_protocol proto)
{
	int err = sa_disable_share(mountpoint, proto);
	if (err != SA_OK)
		return (zfs_error_fmt(hdl, proto_table[proto].p_unshare_err,
		    dgettext(TEXT_DOMAIN, "cannot unshare '%s': %s"),
		    name, sa_errorstr(err)));

	return (0);
}

/*
 * Share the given filesystem according to the options in the specified
 * protocol specific properties (sharenfs, sharesmb).  We rely
 * on "libshare" to do the dirty work for us.
 */
int
zfs_share(zfs_handle_t *zhp, const enum sa_protocol *proto)
{
	char mountpoint[ZFS_MAXPROPLEN];
	char shareopts[ZFS_MAXPROPLEN];
	char sourcestr[ZFS_MAXPROPLEN];
	const enum sa_protocol *curr_proto;
	zprop_source_t sourcetype;
	int err = 0;

	if (proto == NULL)
		proto = share_all_proto;

	if (!zfs_is_mountable(zhp, mountpoint, sizeof (mountpoint), NULL, 0))
		return (0);

	for (curr_proto = proto; *curr_proto != SA_NO_PROTOCOL; curr_proto++) {
		/*
		 * Return success if there are no share options.
		 */
		if (zfs_prop_get(zhp, proto_table[*curr_proto].p_prop,
		    shareopts, sizeof (shareopts), &sourcetype, sourcestr,
		    ZFS_MAXPROPLEN, B_FALSE) != 0 ||
		    strcmp(shareopts, "off") == 0)
			continue;

		/*
		 * If the 'zoned' property is set, then zfs_is_mountable()
		 * will have already bailed out if we are in the global zone.
		 * But local zones cannot be NFS servers, so we ignore it for
		 * local zones as well.
		 */
		if (zfs_prop_get_int(zhp, ZFS_PROP_ZONED))
			continue;

		err = sa_enable_share(zfs_get_name(zhp), mountpoint, shareopts,
		    *curr_proto);
		if (err != SA_OK) {
			return (zfs_error_fmt(zhp->zfs_hdl,
			    proto_table[*curr_proto].p_share_err,
			    dgettext(TEXT_DOMAIN, "cannot share '%s: %s'"),
			    zfs_get_name(zhp), sa_errorstr(err)));
		}

	}
	return (0);
}

/*
 * Check to see if the filesystem is currently shared.
 */
boolean_t
zfs_is_shared(zfs_handle_t *zhp, char **where,
    const enum sa_protocol *proto)
{
	char *mountpoint;
	if (proto == NULL)
		proto = share_all_proto;

	if (ZFS_IS_VOLUME(zhp))
		return (B_FALSE);

	if (!zfs_is_mounted(zhp, &mountpoint))
		return (B_FALSE);

	for (const enum sa_protocol *p = proto; *p != SA_NO_PROTOCOL; ++p)
		if (sa_is_shared(mountpoint, *p)) {
			if (where != NULL)
				*where = mountpoint;
			else
				free(mountpoint);
			return (B_TRUE);
		}

	free(mountpoint);
	return (B_FALSE);
}

void
zfs_commit_shares(const enum sa_protocol *proto)
{
	if (proto == NULL)
		proto = share_all_proto;

	for (const enum sa_protocol *p = proto; *p != SA_NO_PROTOCOL; ++p)
		sa_commit_shares(*p);
}

void
zfs_truncate_shares(const enum sa_protocol *proto)
{
	if (proto == NULL)
		proto = share_all_proto;

	for (const enum sa_protocol *p = proto; *p != SA_NO_PROTOCOL; ++p)
		sa_truncate_shares(*p);
}

/*
 * Unshare the given filesystem.
 */
int
zfs_unshare(zfs_handle_t *zhp, const char *mountpoint,
    const enum sa_protocol *proto)
{
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	struct mnttab entry;

	if (proto == NULL)
		proto = share_all_proto;

	if (mountpoint != NULL || ((zfs_get_type(zhp) == ZFS_TYPE_FILESYSTEM) &&
	    libzfs_mnttab_find(hdl, zfs_get_name(zhp), &entry) == 0)) {

		/* check to see if need to unmount the filesystem */
		const char *mntpt = mountpoint ?: entry.mnt_mountp;

		for (const enum sa_protocol *curr_proto = proto;
		    *curr_proto != SA_NO_PROTOCOL; curr_proto++)
			if (sa_is_shared(mntpt, *curr_proto) &&
			    unshare_one(hdl, zhp->zfs_name,
			    mntpt, *curr_proto) != 0)
					return (-1);
	}

	return (0);
}

/*
 * Same as zfs_unmountall(), but for NFS and SMB unshares.
 */
int
zfs_unshareall(zfs_handle_t *zhp, const enum sa_protocol *proto)
{
	prop_changelist_t *clp;
	int ret;

	if (proto == NULL)
		proto = share_all_proto;

	clp = changelist_gather(zhp, ZFS_PROP_SHARENFS, 0, 0);
	if (clp == NULL)
		return (-1);

	ret = changelist_unshare(clp, proto);
	changelist_free(clp);

	return (ret);
}

/*
 * Remove the mountpoint associated with the current dataset, if necessary.
 * We only remove the underlying directory if:
 *
 *	- The mountpoint is not 'none' or 'legacy'
 *	- The mountpoint is non-empty
 *	- The mountpoint is the default or inherited
 *	- The 'zoned' property is set, or we're in a local zone
 *
 * Any other directories we leave alone.
 */
void
remove_mountpoint(zfs_handle_t *zhp)
{
	char mountpoint[ZFS_MAXPROPLEN];
	zprop_source_t source;

	if (!zfs_is_mountable(zhp, mountpoint, sizeof (mountpoint),
	    &source, 0))
		return;

	if (source == ZPROP_SRC_DEFAULT ||
	    source == ZPROP_SRC_INHERITED) {
		/*
		 * Try to remove the directory, silently ignoring any errors.
		 * The filesystem may have since been removed or moved around,
		 * and this error isn't really useful to the administrator in
		 * any way.
		 */
		(void) rmdir(mountpoint);
	}
}

/*
 * Add the given zfs handle to the cb_handles array, dynamically reallocating
 * the array if it is out of space.
 */
void
libzfs_add_handle(get_all_cb_t *cbp, zfs_handle_t *zhp)
{
	if (cbp->cb_alloc == cbp->cb_used) {
		size_t newsz;
		zfs_handle_t **newhandles;

		newsz = cbp->cb_alloc != 0 ? cbp->cb_alloc * 2 : 64;
		newhandles = zfs_realloc(zhp->zfs_hdl,
		    cbp->cb_handles, cbp->cb_alloc * sizeof (zfs_handle_t *),
		    newsz * sizeof (zfs_handle_t *));
		cbp->cb_handles = newhandles;
		cbp->cb_alloc = newsz;
	}
	cbp->cb_handles[cbp->cb_used++] = zhp;
}

/*
 * Recursive helper function used during file system enumeration
 */
static int
zfs_iter_cb(zfs_handle_t *zhp, void *data)
{
	get_all_cb_t *cbp = data;

	if (!(zfs_get_type(zhp) & ZFS_TYPE_FILESYSTEM)) {
		zfs_close(zhp);
		return (0);
	}

	if (zfs_prop_get_int(zhp, ZFS_PROP_CANMOUNT) == ZFS_CANMOUNT_NOAUTO) {
		zfs_close(zhp);
		return (0);
	}

	if (zfs_prop_get_int(zhp, ZFS_PROP_KEYSTATUS) ==
	    ZFS_KEYSTATUS_UNAVAILABLE) {
		zfs_close(zhp);
		return (0);
	}

	/*
	 * If this filesystem is inconsistent and has a receive resume
	 * token, we can not mount it.
	 */
	if (zfs_prop_get_int(zhp, ZFS_PROP_INCONSISTENT) &&
	    zfs_prop_get(zhp, ZFS_PROP_RECEIVE_RESUME_TOKEN,
	    NULL, 0, NULL, NULL, 0, B_TRUE) == 0) {
		zfs_close(zhp);
		return (0);
	}

	libzfs_add_handle(cbp, zhp);
	if (zfs_iter_filesystems_v2(zhp, 0, zfs_iter_cb, cbp) != 0) {
		zfs_close(zhp);
		return (-1);
	}
	return (0);
}

/*
 * Sort comparator that compares two mountpoint paths. We sort these paths so
 * that subdirectories immediately follow their parents. This means that we
 * effectively treat the '/' character as the lowest value non-nul char.
 * Since filesystems from non-global zones can have the same mountpoint
 * as other filesystems, the comparator sorts global zone filesystems to
 * the top of the list. This means that the global zone will traverse the
 * filesystem list in the correct order and can stop when it sees the
 * first zoned filesystem. In a non-global zone, only the delegated
 * filesystems are seen.
 *
 * An example sorted list using this comparator would look like:
 *
 * /foo
 * /foo/bar
 * /foo/bar/baz
 * /foo/baz
 * /foo.bar
 * /foo (NGZ1)
 * /foo (NGZ2)
 *
 * The mounting code depends on this ordering to deterministically iterate
 * over filesystems in order to spawn parallel mount tasks.
 */
static int
mountpoint_cmp(const void *arga, const void *argb)
{
	zfs_handle_t *const *zap = arga;
	zfs_handle_t *za = *zap;
	zfs_handle_t *const *zbp = argb;
	zfs_handle_t *zb = *zbp;
	char mounta[MAXPATHLEN];
	char mountb[MAXPATHLEN];
	const char *a = mounta;
	const char *b = mountb;
	boolean_t gota, gotb;
	uint64_t zoneda, zonedb;

	zoneda = zfs_prop_get_int(za, ZFS_PROP_ZONED);
	zonedb = zfs_prop_get_int(zb, ZFS_PROP_ZONED);
	if (zoneda && !zonedb)
		return (1);
	if (!zoneda && zonedb)
		return (-1);

	gota = (zfs_get_type(za) == ZFS_TYPE_FILESYSTEM);
	if (gota) {
		verify(zfs_prop_get(za, ZFS_PROP_MOUNTPOINT, mounta,
		    sizeof (mounta), NULL, NULL, 0, B_FALSE) == 0);
	}
	gotb = (zfs_get_type(zb) == ZFS_TYPE_FILESYSTEM);
	if (gotb) {
		verify(zfs_prop_get(zb, ZFS_PROP_MOUNTPOINT, mountb,
		    sizeof (mountb), NULL, NULL, 0, B_FALSE) == 0);
	}

	if (gota && gotb) {
		while (*a != '\0' && (*a == *b)) {
			a++;
			b++;
		}
		if (*a == *b)
			return (0);
		if (*a == '\0')
			return (-1);
		if (*b == '\0')
			return (1);
		if (*a == '/')
			return (-1);
		if (*b == '/')
			return (1);
		return (*a < *b ? -1 : *a > *b);
	}

	if (gota)
		return (-1);
	if (gotb)
		return (1);

	/*
	 * If neither filesystem has a mountpoint, revert to sorting by
	 * dataset name.
	 */
	return (strcmp(zfs_get_name(za), zfs_get_name(zb)));
}

/*
 * Return true if path2 is a child of path1 or path2 equals path1 or
 * path1 is "/" (path2 is always a child of "/").
 */
static boolean_t
libzfs_path_contains(const char *path1, const char *path2)
{
	return (strcmp(path1, path2) == 0 || strcmp(path1, "/") == 0 ||
	    (strstr(path2, path1) == path2 && path2[strlen(path1)] == '/'));
}

/*
 * Given a mountpoint specified by idx in the handles array, find the first
 * non-descendent of that mountpoint and return its index. Descendant paths
 * start with the parent's path. This function relies on the ordering
 * enforced by mountpoint_cmp().
 */
static int
non_descendant_idx(zfs_handle_t **handles, size_t num_handles, int idx)
{
	char parent[ZFS_MAXPROPLEN];
	char child[ZFS_MAXPROPLEN];
	int i;

	verify(zfs_prop_get(handles[idx], ZFS_PROP_MOUNTPOINT, parent,
	    sizeof (parent), NULL, NULL, 0, B_FALSE) == 0);

	for (i = idx + 1; i < num_handles; i++) {
		verify(zfs_prop_get(handles[i], ZFS_PROP_MOUNTPOINT, child,
		    sizeof (child), NULL, NULL, 0, B_FALSE) == 0);
		if (!libzfs_path_contains(parent, child))
			break;
	}
	return (i);
}

typedef struct mnt_param {
	libzfs_handle_t	*mnt_hdl;
	tpool_t		*mnt_tp;
	zfs_handle_t	**mnt_zhps; /* filesystems to mount */
	size_t		mnt_num_handles;
	int		mnt_idx;	/* Index of selected entry to mount */
	zfs_iter_f	mnt_func;
	void		*mnt_data;
} mnt_param_t;

/*
 * Allocate and populate the parameter struct for mount function, and
 * schedule mounting of the entry selected by idx.
 */
static void
zfs_dispatch_mount(libzfs_handle_t *hdl, zfs_handle_t **handles,
    size_t num_handles, int idx, zfs_iter_f func, void *data, tpool_t *tp)
{
	mnt_param_t *mnt_param = zfs_alloc(hdl, sizeof (mnt_param_t));

	mnt_param->mnt_hdl = hdl;
	mnt_param->mnt_tp = tp;
	mnt_param->mnt_zhps = handles;
	mnt_param->mnt_num_handles = num_handles;
	mnt_param->mnt_idx = idx;
	mnt_param->mnt_func = func;
	mnt_param->mnt_data = data;

	(void) tpool_dispatch(tp, zfs_mount_task, (void*)mnt_param);
}

/*
 * This is the structure used to keep state of mounting or sharing operations
 * during a call to zpool_enable_datasets().
 */
typedef struct mount_state {
	/*
	 * ms_mntstatus is set to -1 if any mount fails. While multiple threads
	 * could update this variable concurrently, no synchronization is
	 * needed as it's only ever set to -1.
	 */
	int		ms_mntstatus;
	int		ms_mntflags;
	const char	*ms_mntopts;
} mount_state_t;

static int
zfs_mount_one(zfs_handle_t *zhp, void *arg)
{
	mount_state_t *ms = arg;
	int ret = 0;

	/*
	 * don't attempt to mount encrypted datasets with
	 * unloaded keys
	 */
	if (zfs_prop_get_int(zhp, ZFS_PROP_KEYSTATUS) ==
	    ZFS_KEYSTATUS_UNAVAILABLE)
		return (0);

	if (zfs_mount(zhp, ms->ms_mntopts, ms->ms_mntflags) != 0)
		ret = ms->ms_mntstatus = -1;
	return (ret);
}

static int
zfs_share_one(zfs_handle_t *zhp, void *arg)
{
	mount_state_t *ms = arg;
	int ret = 0;

	if (zfs_share(zhp, NULL) != 0)
		ret = ms->ms_mntstatus = -1;
	return (ret);
}

/*
 * Thread pool function to mount one file system. On completion, it finds and
 * schedules its children to be mounted. This depends on the sorting done in
 * zfs_foreach_mountpoint(). Note that the degenerate case (chain of entries
 * each descending from the previous) will have no parallelism since we always
 * have to wait for the parent to finish mounting before we can schedule
 * its children.
 */
static void
zfs_mount_task(void *arg)
{
	mnt_param_t *mp = arg;
	int idx = mp->mnt_idx;
	zfs_handle_t **handles = mp->mnt_zhps;
	size_t num_handles = mp->mnt_num_handles;
	char mountpoint[ZFS_MAXPROPLEN];

	verify(zfs_prop_get(handles[idx], ZFS_PROP_MOUNTPOINT, mountpoint,
	    sizeof (mountpoint), NULL, NULL, 0, B_FALSE) == 0);

	if (mp->mnt_func(handles[idx], mp->mnt_data) != 0)
		goto out;

	/*
	 * We dispatch tasks to mount filesystems with mountpoints underneath
	 * this one. We do this by dispatching the next filesystem with a
	 * descendant mountpoint of the one we just mounted, then skip all of
	 * its descendants, dispatch the next descendant mountpoint, and so on.
	 * The non_descendant_idx() function skips over filesystems that are
	 * descendants of the filesystem we just dispatched.
	 */
	for (int i = idx + 1; i < num_handles;
	    i = non_descendant_idx(handles, num_handles, i)) {
		char child[ZFS_MAXPROPLEN];
		verify(zfs_prop_get(handles[i], ZFS_PROP_MOUNTPOINT,
		    child, sizeof (child), NULL, NULL, 0, B_FALSE) == 0);

		if (!libzfs_path_contains(mountpoint, child))
			break; /* not a descendant, return */
		zfs_dispatch_mount(mp->mnt_hdl, handles, num_handles, i,
		    mp->mnt_func, mp->mnt_data, mp->mnt_tp);
	}

out:
	free(mp);
}

/*
 * Issue the func callback for each ZFS handle contained in the handles
 * array. This function is used to mount all datasets, and so this function
 * guarantees that filesystems for parent mountpoints are called before their
 * children. As such, before issuing any callbacks, we first sort the array
 * of handles by mountpoint.
 *
 * Callbacks are issued in one of two ways:
 *
 * 1. Sequentially: If the parallel argument is B_FALSE or the ZFS_SERIAL_MOUNT
 *    environment variable is set, then we issue callbacks sequentially.
 *
 * 2. In parallel: If the parallel argument is B_TRUE and the ZFS_SERIAL_MOUNT
 *    environment variable is not set, then we use a tpool to dispatch threads
 *    to mount filesystems in parallel. This function dispatches tasks to mount
 *    the filesystems at the top-level mountpoints, and these tasks in turn
 *    are responsible for recursively mounting filesystems in their children
 *    mountpoints.
 */
void
zfs_foreach_mountpoint(libzfs_handle_t *hdl, zfs_handle_t **handles,
    size_t num_handles, zfs_iter_f func, void *data, boolean_t parallel)
{
	zoneid_t zoneid = getzoneid();

	/*
	 * The ZFS_SERIAL_MOUNT environment variable is an undocumented
	 * variable that can be used as a convenience to do a/b comparison
	 * of serial vs. parallel mounting.
	 */
	boolean_t serial_mount = !parallel ||
	    (getenv("ZFS_SERIAL_MOUNT") != NULL);

	/*
	 * Sort the datasets by mountpoint. See mountpoint_cmp for details
	 * of how these are sorted.
	 */
	qsort(handles, num_handles, sizeof (zfs_handle_t *), mountpoint_cmp);

	if (serial_mount) {
		for (int i = 0; i < num_handles; i++) {
			func(handles[i], data);
		}
		return;
	}

	/*
	 * Issue the callback function for each dataset using a parallel
	 * algorithm that uses a thread pool to manage threads.
	 */
	tpool_t *tp = tpool_create(1, mount_tp_nthr, 0, NULL);

	/*
	 * There may be multiple "top level" mountpoints outside of the pool's
	 * root mountpoint, e.g.: /foo /bar. Dispatch a mount task for each of
	 * these.
	 */
	for (int i = 0; i < num_handles;
	    i = non_descendant_idx(handles, num_handles, i)) {
		/*
		 * Since the mountpoints have been sorted so that the zoned
		 * filesystems are at the end, a zoned filesystem seen from
		 * the global zone means that we're done.
		 */
		if (zoneid == GLOBAL_ZONEID &&
		    zfs_prop_get_int(handles[i], ZFS_PROP_ZONED))
			break;
		zfs_dispatch_mount(hdl, handles, num_handles, i, func, data,
		    tp);
	}

	tpool_wait(tp);	/* wait for all scheduled mounts to complete */
	tpool_destroy(tp);
}

/*
 * Mount and share all datasets within the given pool.  This assumes that no
 * datasets within the pool are currently mounted.
 */
int
zpool_enable_datasets(zpool_handle_t *zhp, const char *mntopts, int flags)
{
	get_all_cb_t cb = { 0 };
	mount_state_t ms = { 0 };
	zfs_handle_t *zfsp;
	int ret = 0;

	if ((zfsp = zfs_open(zhp->zpool_hdl, zhp->zpool_name,
	    ZFS_TYPE_DATASET)) == NULL)
		goto out;

	/*
	 * Gather all non-snapshot datasets within the pool. Start by adding
	 * the root filesystem for this pool to the list, and then iterate
	 * over all child filesystems.
	 */
	libzfs_add_handle(&cb, zfsp);
	if (zfs_iter_filesystems_v2(zfsp, 0, zfs_iter_cb, &cb) != 0)
		goto out;

	/*
	 * Mount all filesystems
	 */
	ms.ms_mntopts = mntopts;
	ms.ms_mntflags = flags;
	zfs_foreach_mountpoint(zhp->zpool_hdl, cb.cb_handles, cb.cb_used,
	    zfs_mount_one, &ms, B_TRUE);
	if (ms.ms_mntstatus != 0)
		ret = EZFS_MOUNTFAILED;

	/*
	 * Share all filesystems that need to be shared. This needs to be
	 * a separate pass because libshare is not mt-safe, and so we need
	 * to share serially.
	 */
	ms.ms_mntstatus = 0;
	zfs_foreach_mountpoint(zhp->zpool_hdl, cb.cb_handles, cb.cb_used,
	    zfs_share_one, &ms, B_FALSE);
	if (ms.ms_mntstatus != 0)
		ret = EZFS_SHAREFAILED;
	else
		zfs_commit_shares(NULL);

out:
	for (int i = 0; i < cb.cb_used; i++)
		zfs_close(cb.cb_handles[i]);
	free(cb.cb_handles);

	return (ret);
}

struct sets_s {
	char *mountpoint;
	zfs_handle_t *dataset;
};

static int
mountpoint_compare(const void *a, const void *b)
{
	const struct sets_s *mounta = (struct sets_s *)a;
	const struct sets_s *mountb = (struct sets_s *)b;

	return (strcmp(mountb->mountpoint, mounta->mountpoint));
}

/*
 * Unshare and unmount all datasets within the given pool.  We don't want to
 * rely on traversing the DSL to discover the filesystems within the pool,
 * because this may be expensive (if not all of them are mounted), and can fail
 * arbitrarily (on I/O error, for example).  Instead, we walk /proc/self/mounts
 * and gather all the filesystems that are currently mounted.
 */
int
zpool_disable_datasets(zpool_handle_t *zhp, boolean_t force)
{
	int used, alloc;
	FILE *mnttab;
	struct mnttab entry;
	size_t namelen;
	struct sets_s *sets = NULL;
	libzfs_handle_t *hdl = zhp->zpool_hdl;
	int i;
	int ret = -1;
	int flags = (force ? MS_FORCE : 0);

	namelen = strlen(zhp->zpool_name);

	if ((mnttab = fopen(MNTTAB, "re")) == NULL)
		return (ENOENT);

	used = alloc = 0;
	while (getmntent(mnttab, &entry) == 0) {
		/*
		 * Ignore non-ZFS entries.
		 */
		if (entry.mnt_fstype == NULL ||
		    strcmp(entry.mnt_fstype, MNTTYPE_ZFS) != 0)
			continue;

		/*
		 * Ignore filesystems not within this pool.
		 */
		if (entry.mnt_mountp == NULL ||
		    strncmp(entry.mnt_special, zhp->zpool_name, namelen) != 0 ||
		    (entry.mnt_special[namelen] != '/' &&
		    entry.mnt_special[namelen] != '\0'))
			continue;

		/*
		 * At this point we've found a filesystem within our pool.  Add
		 * it to our growing list.
		 */
		if (used == alloc) {
			if (alloc == 0) {
				sets = zfs_alloc(hdl,
				    8 * sizeof (struct sets_s));
				alloc = 8;
			} else {
				sets = zfs_realloc(hdl, sets,
				    alloc * sizeof (struct sets_s),
				    alloc * 2 * sizeof (struct sets_s));

				alloc *= 2;
			}
		}

		sets[used].mountpoint = zfs_strdup(hdl, entry.mnt_mountp);

		/*
		 * This is allowed to fail, in case there is some I/O error.  It
		 * is only used to determine if we need to remove the underlying
		 * mountpoint, so failure is not fatal.
		 */
		sets[used].dataset = make_dataset_handle(hdl,
		    entry.mnt_special);

		used++;
	}

	/*
	 * At this point, we have the entire list of filesystems, so sort it by
	 * mountpoint.
	 */
	if (used != 0)
		qsort(sets, used, sizeof (struct sets_s), mountpoint_compare);

	/*
	 * Walk through and first unshare everything.
	 */
	for (i = 0; i < used; i++) {
		for (enum sa_protocol p = 0; p < SA_PROTOCOL_COUNT; ++p) {
			if (sa_is_shared(sets[i].mountpoint, p) &&
			    unshare_one(hdl, sets[i].mountpoint,
			    sets[i].mountpoint, p) != 0)
				goto out;
		}
	}
	zfs_commit_shares(NULL);

	/*
	 * Now unmount everything, removing the underlying directories as
	 * appropriate.
	 */
	for (i = 0; i < used; i++) {
		if (unmount_one(sets[i].dataset, sets[i].mountpoint,
		    flags) != 0)
			goto out;
	}

	for (i = 0; i < used; i++) {
		if (sets[i].dataset)
			remove_mountpoint(sets[i].dataset);
	}

	zpool_disable_datasets_os(zhp, force);

	ret = 0;
out:
	(void) fclose(mnttab);
	for (i = 0; i < used; i++) {
		if (sets[i].dataset)
			zfs_close(sets[i].dataset);
		free(sets[i].mountpoint);
	}
	free(sets);

	return (ret);
}
