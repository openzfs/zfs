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
#include <libzfs.h>

#include "libzfs_impl.h"
#include <thread_pool.h>

/*
 * zfs_init_libshare(zhandle, service)
 *
 * Initialize the libshare API if it hasn't already been initialized.
 * In all cases it returns 0 if it succeeded and an error if not. The
 * service value is which part(s) of the API to initialize and is a
 * direct map to the libshare sa_init(service) interface.
 */
int
zfs_init_libshare(libzfs_handle_t *zhandle, int service)
{
	int ret = SA_OK;

	if (ret == SA_OK && zhandle->libzfs_shareflags & ZFSSHARE_MISS) {
		/*
		 * We had a cache miss. Most likely it is a new ZFS
		 * dataset that was just created. We want to make sure
		 * so check timestamps to see if a different process
		 * has updated any of the configuration. If there was
		 * some non-ZFS change, we need to re-initialize the
		 * internal cache.
		 */
		zhandle->libzfs_shareflags &= ~ZFSSHARE_MISS;
		if (sa_needs_refresh(zhandle->libzfs_sharehdl)) {
			zfs_uninit_libshare(zhandle);
			zhandle->libzfs_sharehdl = sa_init(service);
		}
	}

	if (ret == SA_OK && zhandle && zhandle->libzfs_sharehdl == NULL)
		zhandle->libzfs_sharehdl = sa_init(service);

	if (ret == SA_OK && zhandle->libzfs_sharehdl == NULL)
		ret = SA_NO_MEMORY;
	return (ret);
}


/*
 * Share the given filesystem according to the options in the specified
 * protocol specific properties (sharenfs, sharesmb).  We rely
 * on "libshare" to do the dirty work for us.
 */
int
zfs_share_proto(zfs_handle_t *zhp, zfs_share_proto_t *proto)
{
	char mountpoint[ZFS_MAXPROPLEN];
	char shareopts[ZFS_MAXPROPLEN];
	char sourcestr[ZFS_MAXPROPLEN];
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	sa_share_t share;
	zfs_share_proto_t *curr_proto;
	zprop_source_t sourcetype;
	int err, ret;

	if (!zfs_is_mountable(zhp, mountpoint, sizeof (mountpoint), NULL, 0))
		return (0);

	for (curr_proto = proto; *curr_proto != PROTO_END; curr_proto++) {
		/*
		 * Return success if there are no share options.
		 */
		if (zfs_prop_get(zhp, proto_table[*curr_proto].p_prop,
		    shareopts, sizeof (shareopts), &sourcetype, sourcestr,
		    ZFS_MAXPROPLEN, B_FALSE) != 0 ||
		    strcmp(shareopts, "off") == 0)
			continue;

		ret = zfs_init_libshare(hdl, SA_INIT_SHARE_API);
		if (ret != SA_OK) {
			(void) zfs_error_fmt(hdl, EZFS_SHARENFSFAILED,
			    dgettext(TEXT_DOMAIN, "cannot share '%s': %s"),
			    zfs_get_name(zhp), sa_errorstr(ret));
			return (-1);
		}

		/*
		 * If the 'zoned' property is set, then zfs_is_mountable()
		 * will have already bailed out if we are in the global zone.
		 * But local zones cannot be NFS servers, so we ignore it for
		 * local zones as well.
		 */
		if (zfs_prop_get_int(zhp, ZFS_PROP_ZONED))
			continue;

		share = sa_find_share(hdl->libzfs_sharehdl, mountpoint);
		if (share == NULL) {
			/*
			 * This may be a new file system that was just
			 * created so isn't in the internal cache
			 * (second time through). Rather than
			 * reloading the entire configuration, we can
			 * assume ZFS has done the checking and it is
			 * safe to add this to the internal
			 * configuration.
			 */
			if (sa_zfs_process_share(hdl->libzfs_sharehdl,
			    NULL, NULL, mountpoint,
			    proto_table[*curr_proto].p_name, sourcetype,
			    shareopts, sourcestr, zhp->zfs_name) != SA_OK) {
				(void) zfs_error_fmt(hdl,
				    proto_table[*curr_proto].p_share_err,
				    dgettext(TEXT_DOMAIN, "cannot share '%s'"),
				    zfs_get_name(zhp));
				return (-1);
			}
			hdl->libzfs_shareflags |= ZFSSHARE_MISS;
			share = sa_find_share(hdl->libzfs_sharehdl,
			    mountpoint);
		}
		if (share != NULL) {
			err = sa_enable_share(share,
			    proto_table[*curr_proto].p_name);
			if (err != SA_OK) {
				(void) zfs_error_fmt(hdl,
				    proto_table[*curr_proto].p_share_err,
				    dgettext(TEXT_DOMAIN, "cannot share '%s'"),
				    zfs_get_name(zhp));
				return (-1);
			}
		} else {
			(void) zfs_error_fmt(hdl,
			    proto_table[*curr_proto].p_share_err,
			    dgettext(TEXT_DOMAIN, "cannot share '%s'"),
			    zfs_get_name(zhp));
			return (-1);
		}

	}
	return (0);
}

/*
 * Unshare a filesystem by mountpoint.
 */
int
unshare_one(libzfs_handle_t *hdl, const char *name, const char *mountpoint,
    zfs_share_proto_t proto)
{
	sa_share_t share;
	int err;
	char *mntpt;
	/*
	 * Mountpoint could get trashed if libshare calls getmntany
	 * which it does during API initialization, so strdup the
	 * value.
	 */
	mntpt = zfs_strdup(hdl, mountpoint);

	/* make sure libshare initialized */
	if ((err = zfs_init_libshare(hdl, SA_INIT_SHARE_API)) != SA_OK) {
		free(mntpt);	/* don't need the copy anymore */
		return (zfs_error_fmt(hdl, proto_table[proto].p_unshare_err,
		    dgettext(TEXT_DOMAIN, "cannot unshare '%s': %s"),
		    name, sa_errorstr(err)));
	}

	share = sa_find_share(hdl->libzfs_sharehdl, mntpt);
	free(mntpt);	/* don't need the copy anymore */

	if (share != NULL) {
		err = sa_disable_share(share, proto_table[proto].p_name);
		if (err != SA_OK) {
			return (zfs_error_fmt(hdl,
			    proto_table[proto].p_unshare_err,
			    dgettext(TEXT_DOMAIN, "cannot unshare '%s': %s"),
			    name, sa_errorstr(err)));
		}
	} else {
		return (zfs_error_fmt(hdl, proto_table[proto].p_unshare_err,
		    dgettext(TEXT_DOMAIN, "cannot unshare '%s': not found"),
		    name));
	}
	return (0);
}

/*
 * Search the sharetab for the given mountpoint and protocol, returning
 * a zfs_share_type_t value.
 */
zfs_share_type_t
is_shared_impl(libzfs_handle_t *hdl, const char *mountpoint,
    zfs_share_proto_t proto)
{
	char buf[MAXPATHLEN], *tab;
	char *ptr;

	if (hdl->libzfs_sharetab == NULL)
		return (SHARED_NOT_SHARED);

	/* Reopen ZFS_SHARETAB to prevent reading stale data from open file */
	if (freopen(ZFS_SHARETAB, "r", hdl->libzfs_sharetab) == NULL)
		return (SHARED_NOT_SHARED);

	(void) fseek(hdl->libzfs_sharetab, 0, SEEK_SET);

	while (fgets(buf, sizeof (buf), hdl->libzfs_sharetab) != NULL) {

		/* the mountpoint is the first entry on each line */
		if ((tab = strchr(buf, '\t')) == NULL)
			continue;

		*tab = '\0';
		if (strcmp(buf, mountpoint) == 0) {
			/*
			 * the protocol field is the third field
			 * skip over second field
			 */
			ptr = ++tab;
			if ((tab = strchr(ptr, '\t')) == NULL)
				continue;
			ptr = ++tab;
			if ((tab = strchr(ptr, '\t')) == NULL)
				continue;
			*tab = '\0';
			if (strcmp(ptr,
			    proto_table[proto].p_name) == 0) {
				switch (proto) {
				case PROTO_NFS:
					return (SHARED_NFS);
				case PROTO_SMB:
					return (SHARED_SMB);
				default:
					return (0);
				}
			}
		}
	}

	return (SHARED_NOT_SHARED);
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
int
do_mount(const char *src, const char *mntpt, char *opts, int flags)
{
	char *argv[9] = {
	    "/bin/mount",
	    "--no-canonicalize",
	    "-t", MNTTYPE_ZFS,
	    "-o", opts,
	    (char *)src,
	    (char *)mntpt,
	    (char *)NULL };
	int rc;

	/* Return only the most critical mount error */
	rc = libzfs_run_process(argv[0], argv, STDOUT_VERBOSE|STDERR_VERBOSE);
	if (rc) {
		if (rc & MOUNT_FILEIO)
			return (EIO);
		if (rc & MOUNT_USER)
			return (EINTR);
		if (rc & MOUNT_SOFTWARE)
			return (EPIPE);
		if (rc & MOUNT_BUSY)
			return (EBUSY);
		if (rc & MOUNT_SYSERR)
			return (EAGAIN);
		if (rc & MOUNT_USAGE)
			return (EINVAL);

		return (ENXIO); /* Generic error */
	}

	return (0);
}

int
do_unmount(const char *mntpt, int flags)
{
	char force_opt[] = "-f";
	char lazy_opt[] = "-l";
	char *argv[7] = {
	    "/bin/umount",
	    "-t", MNTTYPE_ZFS,
	    NULL, NULL, NULL, NULL };
	int rc, count = 3;

	if (flags & MS_FORCE) {
		argv[count] = force_opt;
		count++;
	}

	if (flags & MS_DETACH) {
		argv[count] = lazy_opt;
		count++;
	}

	argv[count] = (char *)mntpt;
	rc = libzfs_run_process(argv[0], argv, STDOUT_VERBOSE|STDERR_VERBOSE);

	return (rc ? EINVAL : 0);
}

int
zfs_mount_delegation_check(void)
{
	return ((geteuid() != 0) ? EACCES : 0);
}
