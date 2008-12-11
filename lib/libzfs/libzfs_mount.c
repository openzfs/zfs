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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Routines to manage ZFS mounts.  We separate all the nasty routines that have
 * to deal with the OS.  The following functions are the main entry points --
 * they are used by mount and unmount and when changing a filesystem's
 * mountpoint.
 *
 * 	zfs_is_mounted()
 * 	zfs_mount()
 * 	zfs_unmount()
 * 	zfs_unmountall()
 *
 * This file also contains the functions used to manage sharing filesystems via
 * NFS and iSCSI:
 *
 * 	zfs_is_shared()
 * 	zfs_share()
 * 	zfs_unshare()
 *
 * 	zfs_is_shared_nfs()
 * 	zfs_is_shared_smb()
 * 	zfs_is_shared_iscsi()
 * 	zfs_share_proto()
 * 	zfs_shareall();
 * 	zfs_share_iscsi()
 * 	zfs_unshare_nfs()
 * 	zfs_unshare_smb()
 * 	zfs_unshareall_nfs()
 *	zfs_unshareall_smb()
 *	zfs_unshareall()
 *	zfs_unshareall_bypath()
 * 	zfs_unshare_iscsi()
 *
 * The following functions are available for pool consumers, and will
 * mount/unmount and share/unshare all datasets within pool:
 *
 * 	zpool_enable_datasets()
 * 	zpool_disable_datasets()
 */

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <libgen.h>
#include <libintl.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <zone.h>
#include <sys/mntent.h>
#include <sys/mnttab.h>
#include <sys/mount.h>
#include <sys/stat.h>

#include <libzfs.h>

#include "libzfs_impl.h"

#include <libshare.h>
#include <sys/systeminfo.h>
#define	MAXISALEN	257	/* based on sysinfo(2) man page */

static int zfs_share_proto(zfs_handle_t *, zfs_share_proto_t *);
zfs_share_type_t zfs_is_shared_proto(zfs_handle_t *, char **,
    zfs_share_proto_t);

static int (*iscsitgt_zfs_share)(const char *);
static int (*iscsitgt_zfs_unshare)(const char *);
static int (*iscsitgt_zfs_is_shared)(const char *);
static int (*iscsitgt_svc_online)();

/*
 * The share protocols table must be in the same order as the zfs_share_prot_t
 * enum in libzfs_impl.h
 */
typedef struct {
	zfs_prop_t p_prop;
	char *p_name;
	int p_share_err;
	int p_unshare_err;
} proto_table_t;

proto_table_t proto_table[PROTO_END] = {
	{ZFS_PROP_SHARENFS, "nfs", EZFS_SHARENFSFAILED, EZFS_UNSHARENFSFAILED},
	{ZFS_PROP_SHARESMB, "smb", EZFS_SHARESMBFAILED, EZFS_UNSHARESMBFAILED},
};

zfs_share_proto_t nfs_only[] = {
	PROTO_NFS,
	PROTO_END
};

zfs_share_proto_t smb_only[] = {
	PROTO_SMB,
	PROTO_END
};
zfs_share_proto_t share_all_proto[] = {
	PROTO_NFS,
	PROTO_SMB,
	PROTO_END
};

#pragma init(zfs_iscsi_init)
static void
zfs_iscsi_init(void)
{
	void *libiscsitgt;

	if ((libiscsitgt = dlopen("/lib/libiscsitgt.so.1",
	    RTLD_LAZY | RTLD_GLOBAL)) == NULL ||
	    (iscsitgt_zfs_share = (int (*)(const char *))dlsym(libiscsitgt,
	    "iscsitgt_zfs_share")) == NULL ||
	    (iscsitgt_zfs_unshare = (int (*)(const char *))dlsym(libiscsitgt,
	    "iscsitgt_zfs_unshare")) == NULL ||
	    (iscsitgt_zfs_is_shared = (int (*)(const char *))dlsym(libiscsitgt,
	    "iscsitgt_zfs_is_shared")) == NULL ||
	    (iscsitgt_svc_online = (int (*)(const char *))dlsym(libiscsitgt,
	    "iscsitgt_svc_online")) == NULL) {
		iscsitgt_zfs_share = NULL;
		iscsitgt_zfs_unshare = NULL;
		iscsitgt_zfs_is_shared = NULL;
		iscsitgt_svc_online = NULL;
	}
}

/*
 * Search the sharetab for the given mountpoint and protocol, returning
 * a zfs_share_type_t value.
 */
static zfs_share_type_t
is_shared(libzfs_handle_t *hdl, const char *mountpoint, zfs_share_proto_t proto)
{
	char buf[MAXPATHLEN], *tab;
	char *ptr;

	if (hdl->libzfs_sharetab == NULL)
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
 * Returns true if the specified directory is empty.  If we can't open the
 * directory at all, return true so that the mount can fail with a more
 * informative error message.
 */
static boolean_t
dir_is_empty(const char *dirname)
{
	DIR *dirp;
	struct dirent64 *dp;

	if ((dirp = opendir(dirname)) == NULL)
		return (B_TRUE);

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
 * Checks to see if the mount is active.  If the filesystem is mounted, we fill
 * in 'where' with the current mountpoint, and return 1.  Otherwise, we return
 * 0.
 */
boolean_t
is_mounted(libzfs_handle_t *zfs_hdl, const char *special, char **where)
{
	struct mnttab search = { 0 }, entry;

	/*
	 * Search for the entry in /etc/mnttab.  We don't bother getting the
	 * mountpoint, as we can just search for the special device.  This will
	 * also let us find mounts when the mountpoint is 'legacy'.
	 */
	search.mnt_special = (char *)special;
	search.mnt_fstype = MNTTYPE_ZFS;

	rewind(zfs_hdl->libzfs_mnttab);
	if (getmntany(zfs_hdl->libzfs_mnttab, &entry, &search) != 0)
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
 * Returns true if the given dataset is mountable, false otherwise.  Returns the
 * mountpoint in 'buf'.
 */
static boolean_t
zfs_is_mountable(zfs_handle_t *zhp, char *buf, size_t buflen,
    zprop_source_t *source)
{
	char sourceloc[ZFS_MAXNAMELEN];
	zprop_source_t sourcetype;

	if (!zfs_prop_valid_for_type(ZFS_PROP_MOUNTPOINT, zhp->zfs_type))
		return (B_FALSE);

	verify(zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, buf, buflen,
	    &sourcetype, sourceloc, sizeof (sourceloc), B_FALSE) == 0);

	if (strcmp(buf, ZFS_MOUNTPOINT_NONE) == 0 ||
	    strcmp(buf, ZFS_MOUNTPOINT_LEGACY) == 0)
		return (B_FALSE);

	if (zfs_prop_get_int(zhp, ZFS_PROP_CANMOUNT) == ZFS_CANMOUNT_OFF)
		return (B_FALSE);

	if (zfs_prop_get_int(zhp, ZFS_PROP_ZONED) &&
	    getzoneid() == GLOBAL_ZONEID)
		return (B_FALSE);

	if (source)
		*source = sourcetype;

	return (B_TRUE);
}

/*
 * Mount the given filesystem.
 */
int
zfs_mount(zfs_handle_t *zhp, const char *options, int flags)
{
	struct stat buf;
	char mountpoint[ZFS_MAXPROPLEN];
	char mntopts[MNT_LINE_MAX];
	libzfs_handle_t *hdl = zhp->zfs_hdl;

	if (options == NULL)
		mntopts[0] = '\0';
	else
		(void) strlcpy(mntopts, options, sizeof (mntopts));

	if (!zfs_is_mountable(zhp, mountpoint, sizeof (mountpoint), NULL))
		return (0);

	/* Create the directory if it doesn't already exist */
	if (lstat(mountpoint, &buf) != 0) {
		if (mkdirp(mountpoint, 0755) != 0) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "failed to create mountpoint"));
			return (zfs_error_fmt(hdl, EZFS_MOUNTFAILED,
			    dgettext(TEXT_DOMAIN, "cannot mount '%s'"),
			    mountpoint));
		}
	}

	/*
	 * Determine if the mountpoint is empty.  If so, refuse to perform the
	 * mount.  We don't perform this check if MS_OVERLAY is specified, which
	 * would defeat the point.  We also avoid this check if 'remount' is
	 * specified.
	 */
	if ((flags & MS_OVERLAY) == 0 &&
	    strstr(mntopts, MNTOPT_REMOUNT) == NULL &&
	    !dir_is_empty(mountpoint)) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "directory is not empty"));
		return (zfs_error_fmt(hdl, EZFS_MOUNTFAILED,
		    dgettext(TEXT_DOMAIN, "cannot mount '%s'"), mountpoint));
	}

	/* perform the mount */
	if (mount(zfs_get_name(zhp), mountpoint, MS_OPTIONSTR | flags,
	    MNTTYPE_ZFS, NULL, 0, mntopts, sizeof (mntopts)) != 0) {
		/*
		 * Generic errors are nasty, but there are just way too many
		 * from mount(), and they're well-understood.  We pick a few
		 * common ones to improve upon.
		 */
		if (errno == EBUSY) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "mountpoint or dataset is busy"));
		} else if (errno == EPERM) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "Insufficient privileges"));
		} else {
			zfs_error_aux(hdl, strerror(errno));
		}

		return (zfs_error_fmt(hdl, EZFS_MOUNTFAILED,
		    dgettext(TEXT_DOMAIN, "cannot mount '%s'"),
		    zhp->zfs_name));
	}

	return (0);
}

/*
 * Unmount a single filesystem.
 */
static int
unmount_one(libzfs_handle_t *hdl, const char *mountpoint, int flags)
{
	if (umount2(mountpoint, flags) != 0) {
		zfs_error_aux(hdl, strerror(errno));
		return (zfs_error_fmt(hdl, EZFS_UMOUNTFAILED,
		    dgettext(TEXT_DOMAIN, "cannot unmount '%s'"),
		    mountpoint));
	}

	return (0);
}

/*
 * Unmount the given filesystem.
 */
int
zfs_unmount(zfs_handle_t *zhp, const char *mountpoint, int flags)
{
	struct mnttab search = { 0 }, entry;
	char *mntpt = NULL;

	/* check to see if need to unmount the filesystem */
	search.mnt_special = zhp->zfs_name;
	search.mnt_fstype = MNTTYPE_ZFS;
	rewind(zhp->zfs_hdl->libzfs_mnttab);
	if (mountpoint != NULL || ((zfs_get_type(zhp) == ZFS_TYPE_FILESYSTEM) &&
	    getmntany(zhp->zfs_hdl->libzfs_mnttab, &entry, &search) == 0)) {

		/*
		 * mountpoint may have come from a call to
		 * getmnt/getmntany if it isn't NULL. If it is NULL,
		 * we know it comes from getmntany which can then get
		 * overwritten later. We strdup it to play it safe.
		 */
		if (mountpoint == NULL)
			mntpt = zfs_strdup(zhp->zfs_hdl, entry.mnt_mountp);
		else
			mntpt = zfs_strdup(zhp->zfs_hdl, mountpoint);

		/*
		 * Unshare and unmount the filesystem
		 */
		if (zfs_unshare_proto(zhp, mntpt, share_all_proto) != 0)
			return (-1);

		if (unmount_one(zhp->zfs_hdl, mntpt, flags) != 0) {
			free(mntpt);
			(void) zfs_shareall(zhp);
			return (-1);
		}
		free(mntpt);
	}

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

	clp = changelist_gather(zhp, ZFS_PROP_MOUNTPOINT, 0, flags);
	if (clp == NULL)
		return (-1);

	ret = changelist_prefix(clp);
	changelist_free(clp);

	return (ret);
}

boolean_t
zfs_is_shared(zfs_handle_t *zhp)
{
	zfs_share_type_t rc = 0;
	zfs_share_proto_t *curr_proto;

	if (ZFS_IS_VOLUME(zhp))
		return (zfs_is_shared_iscsi(zhp));

	for (curr_proto = share_all_proto; *curr_proto != PROTO_END;
	    curr_proto++)
		rc |= zfs_is_shared_proto(zhp, NULL, *curr_proto);

	return (rc ? B_TRUE : B_FALSE);
}

int
zfs_share(zfs_handle_t *zhp)
{
	if (ZFS_IS_VOLUME(zhp))
		return (zfs_share_iscsi(zhp));

	return (zfs_share_proto(zhp, share_all_proto));
}

int
zfs_unshare(zfs_handle_t *zhp)
{
	if (ZFS_IS_VOLUME(zhp))
		return (zfs_unshare_iscsi(zhp));

	return (zfs_unshareall(zhp));
}

/*
 * Check to see if the filesystem is currently shared.
 */
zfs_share_type_t
zfs_is_shared_proto(zfs_handle_t *zhp, char **where, zfs_share_proto_t proto)
{
	char *mountpoint;
	zfs_share_type_t rc;

	if (!zfs_is_mounted(zhp, &mountpoint))
		return (SHARED_NOT_SHARED);

	if (rc = is_shared(zhp->zfs_hdl, mountpoint, proto)) {
		if (where != NULL)
			*where = mountpoint;
		else
			free(mountpoint);
		return (rc);
	} else {
		free(mountpoint);
		return (SHARED_NOT_SHARED);
	}
}

boolean_t
zfs_is_shared_nfs(zfs_handle_t *zhp, char **where)
{
	return (zfs_is_shared_proto(zhp, where,
	    PROTO_NFS) != SHARED_NOT_SHARED);
}

boolean_t
zfs_is_shared_smb(zfs_handle_t *zhp, char **where)
{
	return (zfs_is_shared_proto(zhp, where,
	    PROTO_SMB) != SHARED_NOT_SHARED);
}

/*
 * Make sure things will work if libshare isn't installed by using
 * wrapper functions that check to see that the pointers to functions
 * initialized in _zfs_init_libshare() are actually present.
 */

static sa_handle_t (*_sa_init)(int);
static void (*_sa_fini)(sa_handle_t);
static sa_share_t (*_sa_find_share)(sa_handle_t, char *);
static int (*_sa_enable_share)(sa_share_t, char *);
static int (*_sa_disable_share)(sa_share_t, char *);
static char *(*_sa_errorstr)(int);
static int (*_sa_parse_legacy_options)(sa_group_t, char *, char *);
static boolean_t (*_sa_needs_refresh)(sa_handle_t *);
static libzfs_handle_t *(*_sa_get_zfs_handle)(sa_handle_t);
static int (*_sa_zfs_process_share)(sa_handle_t, sa_group_t, sa_share_t,
    char *, char *, zprop_source_t, char *, char *, char *);
static void (*_sa_update_sharetab_ts)(sa_handle_t);

/*
 * _zfs_init_libshare()
 *
 * Find the libshare.so.1 entry points that we use here and save the
 * values to be used later. This is triggered by the runtime loader.
 * Make sure the correct ISA version is loaded.
 */

#pragma init(_zfs_init_libshare)
static void
_zfs_init_libshare(void)
{
	void *libshare;
	char path[MAXPATHLEN];
	char isa[MAXISALEN];

#if defined(_LP64)
	if (sysinfo(SI_ARCHITECTURE_64, isa, MAXISALEN) == -1)
		isa[0] = '\0';
#else
	isa[0] = '\0';
#endif
	(void) snprintf(path, MAXPATHLEN,
	    "/usr/lib/%s/libshare.so.1", isa);

	if ((libshare = dlopen(path, RTLD_LAZY | RTLD_GLOBAL)) != NULL) {
		_sa_init = (sa_handle_t (*)(int))dlsym(libshare, "sa_init");
		_sa_fini = (void (*)(sa_handle_t))dlsym(libshare, "sa_fini");
		_sa_find_share = (sa_share_t (*)(sa_handle_t, char *))
		    dlsym(libshare, "sa_find_share");
		_sa_enable_share = (int (*)(sa_share_t, char *))dlsym(libshare,
		    "sa_enable_share");
		_sa_disable_share = (int (*)(sa_share_t, char *))dlsym(libshare,
		    "sa_disable_share");
		_sa_errorstr = (char *(*)(int))dlsym(libshare, "sa_errorstr");
		_sa_parse_legacy_options = (int (*)(sa_group_t, char *, char *))
		    dlsym(libshare, "sa_parse_legacy_options");
		_sa_needs_refresh = (boolean_t (*)(sa_handle_t *))
		    dlsym(libshare, "sa_needs_refresh");
		_sa_get_zfs_handle = (libzfs_handle_t *(*)(sa_handle_t))
		    dlsym(libshare, "sa_get_zfs_handle");
		_sa_zfs_process_share = (int (*)(sa_handle_t, sa_group_t,
		    sa_share_t, char *, char *, zprop_source_t, char *,
		    char *, char *))dlsym(libshare, "sa_zfs_process_share");
		_sa_update_sharetab_ts = (void (*)(sa_handle_t))
		    dlsym(libshare, "sa_update_sharetab_ts");
		if (_sa_init == NULL || _sa_fini == NULL ||
		    _sa_find_share == NULL || _sa_enable_share == NULL ||
		    _sa_disable_share == NULL || _sa_errorstr == NULL ||
		    _sa_parse_legacy_options == NULL ||
		    _sa_needs_refresh == NULL || _sa_get_zfs_handle == NULL ||
		    _sa_zfs_process_share == NULL ||
		    _sa_update_sharetab_ts == NULL) {
			_sa_init = NULL;
			_sa_fini = NULL;
			_sa_disable_share = NULL;
			_sa_enable_share = NULL;
			_sa_errorstr = NULL;
			_sa_parse_legacy_options = NULL;
			(void) dlclose(libshare);
			_sa_needs_refresh = NULL;
			_sa_get_zfs_handle = NULL;
			_sa_zfs_process_share = NULL;
			_sa_update_sharetab_ts = NULL;
		}
	}
}

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

	if (_sa_init == NULL)
		ret = SA_CONFIG_ERR;

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
		if (_sa_needs_refresh != NULL &&
		    _sa_needs_refresh(zhandle->libzfs_sharehdl)) {
			zfs_uninit_libshare(zhandle);
			zhandle->libzfs_sharehdl = _sa_init(service);
		}
	}

	if (ret == SA_OK && zhandle && zhandle->libzfs_sharehdl == NULL)
		zhandle->libzfs_sharehdl = _sa_init(service);

	if (ret == SA_OK && zhandle->libzfs_sharehdl == NULL)
		ret = SA_NO_MEMORY;

	return (ret);
}

/*
 * zfs_uninit_libshare(zhandle)
 *
 * Uninitialize the libshare API if it hasn't already been
 * uninitialized. It is OK to call multiple times.
 */
void
zfs_uninit_libshare(libzfs_handle_t *zhandle)
{
	if (zhandle != NULL && zhandle->libzfs_sharehdl != NULL) {
		if (_sa_fini != NULL)
			_sa_fini(zhandle->libzfs_sharehdl);
		zhandle->libzfs_sharehdl = NULL;
	}
}

/*
 * zfs_parse_options(options, proto)
 *
 * Call the legacy parse interface to get the protocol specific
 * options using the NULL arg to indicate that this is a "parse" only.
 */
int
zfs_parse_options(char *options, zfs_share_proto_t proto)
{
	if (_sa_parse_legacy_options != NULL) {
		return (_sa_parse_legacy_options(NULL, options,
		    proto_table[proto].p_name));
	}
	return (SA_CONFIG_ERR);
}

/*
 * zfs_sa_find_share(handle, path)
 *
 * wrapper around sa_find_share to find a share path in the
 * configuration.
 */
static sa_share_t
zfs_sa_find_share(sa_handle_t handle, char *path)
{
	if (_sa_find_share != NULL)
		return (_sa_find_share(handle, path));
	return (NULL);
}

/*
 * zfs_sa_enable_share(share, proto)
 *
 * Wrapper for sa_enable_share which enables a share for a specified
 * protocol.
 */
static int
zfs_sa_enable_share(sa_share_t share, char *proto)
{
	if (_sa_enable_share != NULL)
		return (_sa_enable_share(share, proto));
	return (SA_CONFIG_ERR);
}

/*
 * zfs_sa_disable_share(share, proto)
 *
 * Wrapper for sa_enable_share which disables a share for a specified
 * protocol.
 */
static int
zfs_sa_disable_share(sa_share_t share, char *proto)
{
	if (_sa_disable_share != NULL)
		return (_sa_disable_share(share, proto));
	return (SA_CONFIG_ERR);
}

/*
 * Share the given filesystem according to the options in the specified
 * protocol specific properties (sharenfs, sharesmb).  We rely
 * on "libshare" to the dirty work for us.
 */
static int
zfs_share_proto(zfs_handle_t *zhp, zfs_share_proto_t *proto)
{
	char mountpoint[ZFS_MAXPROPLEN];
	char shareopts[ZFS_MAXPROPLEN];
	char sourcestr[ZFS_MAXPROPLEN];
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	sa_share_t share;
	zfs_share_proto_t *curr_proto;
	zprop_source_t sourcetype;
	int ret;

	if (!zfs_is_mountable(zhp, mountpoint, sizeof (mountpoint), NULL))
		return (0);

	if ((ret = zfs_init_libshare(hdl, SA_INIT_SHARE_API)) != SA_OK) {
		(void) zfs_error_fmt(hdl, EZFS_SHARENFSFAILED,
		    dgettext(TEXT_DOMAIN, "cannot share '%s': %s"),
		    zfs_get_name(zhp), _sa_errorstr != NULL ?
		    _sa_errorstr(ret) : "");
		return (-1);
	}

	for (curr_proto = proto; *curr_proto != PROTO_END; curr_proto++) {
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

		share = zfs_sa_find_share(hdl->libzfs_sharehdl, mountpoint);
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
			if (_sa_zfs_process_share(hdl->libzfs_sharehdl,
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
			share = zfs_sa_find_share(hdl->libzfs_sharehdl,
			    mountpoint);
		}
		if (share != NULL) {
			int err;
			err = zfs_sa_enable_share(share,
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


int
zfs_share_nfs(zfs_handle_t *zhp)
{
	return (zfs_share_proto(zhp, nfs_only));
}

int
zfs_share_smb(zfs_handle_t *zhp)
{
	return (zfs_share_proto(zhp, smb_only));
}

int
zfs_shareall(zfs_handle_t *zhp)
{
	return (zfs_share_proto(zhp, share_all_proto));
}

/*
 * Unshare a filesystem by mountpoint.
 */
static int
unshare_one(libzfs_handle_t *hdl, const char *name, const char *mountpoint,
    zfs_share_proto_t proto)
{
	sa_share_t share;
	int err;
	char *mntpt;
	/*
	 * Mountpoint could get trashed if libshare calls getmntany
	 * which id does during API initialization, so strdup the
	 * value.
	 */
	mntpt = zfs_strdup(hdl, mountpoint);

	/* make sure libshare initialized */
	if ((err = zfs_init_libshare(hdl, SA_INIT_SHARE_API)) != SA_OK) {
		free(mntpt);	/* don't need the copy anymore */
		return (zfs_error_fmt(hdl, EZFS_SHARENFSFAILED,
		    dgettext(TEXT_DOMAIN, "cannot unshare '%s': %s"),
		    name, _sa_errorstr(err)));
	}

	share = zfs_sa_find_share(hdl->libzfs_sharehdl, mntpt);
	free(mntpt);	/* don't need the copy anymore */

	if (share != NULL) {
		err = zfs_sa_disable_share(share, proto_table[proto].p_name);
		if (err != SA_OK) {
			return (zfs_error_fmt(hdl, EZFS_UNSHARENFSFAILED,
			    dgettext(TEXT_DOMAIN, "cannot unshare '%s': %s"),
			    name, _sa_errorstr(err)));
		}
	} else {
		return (zfs_error_fmt(hdl, EZFS_UNSHARENFSFAILED,
		    dgettext(TEXT_DOMAIN, "cannot unshare '%s': not found"),
		    name));
	}
	return (0);
}

/*
 * Unshare the given filesystem.
 */
int
zfs_unshare_proto(zfs_handle_t *zhp, const char *mountpoint,
    zfs_share_proto_t *proto)
{
	struct mnttab search = { 0 }, entry;
	char *mntpt = NULL;

	/* check to see if need to unmount the filesystem */
	search.mnt_special = (char *)zfs_get_name(zhp);
	search.mnt_fstype = MNTTYPE_ZFS;
	rewind(zhp->zfs_hdl->libzfs_mnttab);
	if (mountpoint != NULL)
		mntpt = zfs_strdup(zhp->zfs_hdl, mountpoint);

	if (mountpoint != NULL || ((zfs_get_type(zhp) == ZFS_TYPE_FILESYSTEM) &&
	    getmntany(zhp->zfs_hdl->libzfs_mnttab, &entry, &search) == 0)) {
		zfs_share_proto_t *curr_proto;

		if (mountpoint == NULL)
			mntpt = zfs_strdup(zhp->zfs_hdl, entry.mnt_mountp);

		for (curr_proto = proto; *curr_proto != PROTO_END;
		    curr_proto++) {

			if (is_shared(zhp->zfs_hdl, mntpt, *curr_proto) &&
			    unshare_one(zhp->zfs_hdl, zhp->zfs_name,
			    mntpt, *curr_proto) != 0) {
				if (mntpt != NULL)
					free(mntpt);
				return (-1);
			}
		}
	}
	if (mntpt != NULL)
		free(mntpt);

	return (0);
}

int
zfs_unshare_nfs(zfs_handle_t *zhp, const char *mountpoint)
{
	return (zfs_unshare_proto(zhp, mountpoint, nfs_only));
}

int
zfs_unshare_smb(zfs_handle_t *zhp, const char *mountpoint)
{
	return (zfs_unshare_proto(zhp, mountpoint, smb_only));
}

/*
 * Same as zfs_unmountall(), but for NFS and SMB unshares.
 */
int
zfs_unshareall_proto(zfs_handle_t *zhp, zfs_share_proto_t *proto)
{
	prop_changelist_t *clp;
	int ret;

	clp = changelist_gather(zhp, ZFS_PROP_SHARENFS, 0, 0);
	if (clp == NULL)
		return (-1);

	ret = changelist_unshare(clp, proto);
	changelist_free(clp);

	return (ret);
}

int
zfs_unshareall_nfs(zfs_handle_t *zhp)
{
	return (zfs_unshareall_proto(zhp, nfs_only));
}

int
zfs_unshareall_smb(zfs_handle_t *zhp)
{
	return (zfs_unshareall_proto(zhp, smb_only));
}

int
zfs_unshareall(zfs_handle_t *zhp)
{
	return (zfs_unshareall_proto(zhp, share_all_proto));
}

int
zfs_unshareall_bypath(zfs_handle_t *zhp, const char *mountpoint)
{
	return (zfs_unshare_proto(zhp, mountpoint, share_all_proto));
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
	    &source))
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

boolean_t
zfs_is_shared_iscsi(zfs_handle_t *zhp)
{

	/*
	 * If iscsi deamon isn't running then we aren't shared
	 */
	if (iscsitgt_svc_online && iscsitgt_svc_online() == 1)
		return (B_FALSE);
	else
		return (iscsitgt_zfs_is_shared != NULL &&
		    iscsitgt_zfs_is_shared(zhp->zfs_name) != 0);
}

int
zfs_share_iscsi(zfs_handle_t *zhp)
{
	char shareopts[ZFS_MAXPROPLEN];
	const char *dataset = zhp->zfs_name;
	libzfs_handle_t *hdl = zhp->zfs_hdl;

	/*
	 * Return success if there are no share options.
	 */
	if (zfs_prop_get(zhp, ZFS_PROP_SHAREISCSI, shareopts,
	    sizeof (shareopts), NULL, NULL, 0, B_FALSE) != 0 ||
	    strcmp(shareopts, "off") == 0)
		return (0);

	if (iscsitgt_zfs_share == NULL || iscsitgt_zfs_share(dataset) != 0) {
		int error = EZFS_SHAREISCSIFAILED;

		/*
		 * If service isn't availabele and EPERM was
		 * returned then use special error.
		 */
		if (iscsitgt_svc_online && errno == EPERM &&
		    (iscsitgt_svc_online() != 0))
			error = EZFS_ISCSISVCUNAVAIL;

		return (zfs_error_fmt(hdl, error,
		    dgettext(TEXT_DOMAIN, "cannot share '%s'"), dataset));
	}

	return (0);
}

int
zfs_unshare_iscsi(zfs_handle_t *zhp)
{
	const char *dataset = zfs_get_name(zhp);
	libzfs_handle_t *hdl = zhp->zfs_hdl;

	/*
	 * Return if the volume is not shared
	 */
	if (zfs_is_shared_iscsi(zhp) != SHARED_ISCSI)
		return (0);

	/*
	 * If this fails with ENODEV it indicates that zvol wasn't shared so
	 * we should return success in that case.
	 */
	if (iscsitgt_zfs_unshare == NULL ||
	    (iscsitgt_zfs_unshare(dataset) != 0 && errno != ENODEV)) {
		if (errno == EPERM)
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "Insufficient privileges to unshare iscsi"));
		return (zfs_error_fmt(hdl, EZFS_UNSHAREISCSIFAILED,
		    dgettext(TEXT_DOMAIN, "cannot unshare '%s'"), dataset));
	}

	return (0);
}

typedef struct mount_cbdata {
	zfs_handle_t	**cb_datasets;
	int 		cb_used;
	int		cb_alloc;
} mount_cbdata_t;

static int
mount_cb(zfs_handle_t *zhp, void *data)
{
	mount_cbdata_t *cbp = data;

	if (!(zfs_get_type(zhp) & (ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME))) {
		zfs_close(zhp);
		return (0);
	}

	if (zfs_prop_get_int(zhp, ZFS_PROP_CANMOUNT) == ZFS_CANMOUNT_NOAUTO) {
		zfs_close(zhp);
		return (0);
	}

	if (cbp->cb_alloc == cbp->cb_used) {
		void *ptr;

		if ((ptr = zfs_realloc(zhp->zfs_hdl,
		    cbp->cb_datasets, cbp->cb_alloc * sizeof (void *),
		    cbp->cb_alloc * 2 * sizeof (void *))) == NULL)
			return (-1);
		cbp->cb_datasets = ptr;

		cbp->cb_alloc *= 2;
	}

	cbp->cb_datasets[cbp->cb_used++] = zhp;

	return (zfs_iter_filesystems(zhp, mount_cb, cbp));
}

static int
dataset_cmp(const void *a, const void *b)
{
	zfs_handle_t **za = (zfs_handle_t **)a;
	zfs_handle_t **zb = (zfs_handle_t **)b;
	char mounta[MAXPATHLEN];
	char mountb[MAXPATHLEN];
	boolean_t gota, gotb;

	if ((gota = (zfs_get_type(*za) == ZFS_TYPE_FILESYSTEM)) != 0)
		verify(zfs_prop_get(*za, ZFS_PROP_MOUNTPOINT, mounta,
		    sizeof (mounta), NULL, NULL, 0, B_FALSE) == 0);
	if ((gotb = (zfs_get_type(*zb) == ZFS_TYPE_FILESYSTEM)) != 0)
		verify(zfs_prop_get(*zb, ZFS_PROP_MOUNTPOINT, mountb,
		    sizeof (mountb), NULL, NULL, 0, B_FALSE) == 0);

	if (gota && gotb)
		return (strcmp(mounta, mountb));

	if (gota)
		return (-1);
	if (gotb)
		return (1);

	return (strcmp(zfs_get_name(a), zfs_get_name(b)));
}

/*
 * Mount and share all datasets within the given pool.  This assumes that no
 * datasets within the pool are currently mounted.  Because users can create
 * complicated nested hierarchies of mountpoints, we first gather all the
 * datasets and mountpoints within the pool, and sort them by mountpoint.  Once
 * we have the list of all filesystems, we iterate over them in order and mount
 * and/or share each one.
 */
#pragma weak zpool_mount_datasets = zpool_enable_datasets
int
zpool_enable_datasets(zpool_handle_t *zhp, const char *mntopts, int flags)
{
	mount_cbdata_t cb = { 0 };
	libzfs_handle_t *hdl = zhp->zpool_hdl;
	zfs_handle_t *zfsp;
	int i, ret = -1;
	int *good;

	/*
	 * Gather all non-snap datasets within the pool.
	 */
	if ((cb.cb_datasets = zfs_alloc(hdl, 4 * sizeof (void *))) == NULL)
		return (-1);
	cb.cb_alloc = 4;

	if ((zfsp = zfs_open(hdl, zhp->zpool_name, ZFS_TYPE_DATASET)) == NULL)
		goto out;

	cb.cb_datasets[0] = zfsp;
	cb.cb_used = 1;

	if (zfs_iter_filesystems(zfsp, mount_cb, &cb) != 0)
		goto out;

	/*
	 * Sort the datasets by mountpoint.
	 */
	qsort(cb.cb_datasets, cb.cb_used, sizeof (void *), dataset_cmp);

	/*
	 * And mount all the datasets, keeping track of which ones
	 * succeeded or failed. By using zfs_alloc(), the good pointer
	 * will always be non-NULL.
	 */
	good = zfs_alloc(zhp->zpool_hdl, cb.cb_used * sizeof (int));
	ret = 0;
	for (i = 0; i < cb.cb_used; i++) {
		if (zfs_mount(cb.cb_datasets[i], mntopts, flags) != 0)
			ret = -1;
		else
			good[i] = 1;
	}

	/*
	 * Then share all the ones that need to be shared. This needs
	 * to be a separate pass in order to avoid excessive reloading
	 * of the configuration. Good should never be NULL since
	 * zfs_alloc is supposed to exit if memory isn't available.
	 */
	for (i = 0; i < cb.cb_used; i++) {
		if (good[i] && zfs_share(cb.cb_datasets[i]) != 0)
			ret = -1;
	}

	free(good);

out:
	for (i = 0; i < cb.cb_used; i++)
		zfs_close(cb.cb_datasets[i]);
	free(cb.cb_datasets);

	return (ret);
}


static int
zvol_cb(const char *dataset, void *data)
{
	libzfs_handle_t *hdl = data;
	zfs_handle_t *zhp;

	/*
	 * Ignore snapshots and ignore failures from non-existant datasets.
	 */
	if (strchr(dataset, '@') != NULL ||
	    (zhp = zfs_open(hdl, dataset, ZFS_TYPE_VOLUME)) == NULL)
		return (0);

	if (zfs_unshare_iscsi(zhp) != 0)
		return (-1);

	zfs_close(zhp);

	return (0);
}

static int
mountpoint_compare(const void *a, const void *b)
{
	const char *mounta = *((char **)a);
	const char *mountb = *((char **)b);

	return (strcmp(mountb, mounta));
}

/*
 * Unshare and unmount all datasets within the given pool.  We don't want to
 * rely on traversing the DSL to discover the filesystems within the pool,
 * because this may be expensive (if not all of them are mounted), and can fail
 * arbitrarily (on I/O error, for example).  Instead, we walk /etc/mnttab and
 * gather all the filesystems that are currently mounted.
 */
#pragma weak zpool_unmount_datasets = zpool_disable_datasets
int
zpool_disable_datasets(zpool_handle_t *zhp, boolean_t force)
{
	int used, alloc;
	struct mnttab entry;
	size_t namelen;
	char **mountpoints = NULL;
	zfs_handle_t **datasets = NULL;
	libzfs_handle_t *hdl = zhp->zpool_hdl;
	int i;
	int ret = -1;
	int flags = (force ? MS_FORCE : 0);

	/*
	 * First unshare all zvols.
	 */
	if (zpool_iter_zvol(zhp, zvol_cb, hdl) != 0)
		return (-1);

	namelen = strlen(zhp->zpool_name);

	rewind(hdl->libzfs_mnttab);
	used = alloc = 0;
	while (getmntent(hdl->libzfs_mnttab, &entry) == 0) {
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
				if ((mountpoints = zfs_alloc(hdl,
				    8 * sizeof (void *))) == NULL)
					goto out;

				if ((datasets = zfs_alloc(hdl,
				    8 * sizeof (void *))) == NULL)
					goto out;

				alloc = 8;
			} else {
				void *ptr;

				if ((ptr = zfs_realloc(hdl, mountpoints,
				    alloc * sizeof (void *),
				    alloc * 2 * sizeof (void *))) == NULL)
					goto out;
				mountpoints = ptr;

				if ((ptr = zfs_realloc(hdl, datasets,
				    alloc * sizeof (void *),
				    alloc * 2 * sizeof (void *))) == NULL)
					goto out;
				datasets = ptr;

				alloc *= 2;
			}
		}

		if ((mountpoints[used] = zfs_strdup(hdl,
		    entry.mnt_mountp)) == NULL)
			goto out;

		/*
		 * This is allowed to fail, in case there is some I/O error.  It
		 * is only used to determine if we need to remove the underlying
		 * mountpoint, so failure is not fatal.
		 */
		datasets[used] = make_dataset_handle(hdl, entry.mnt_special);

		used++;
	}

	/*
	 * At this point, we have the entire list of filesystems, so sort it by
	 * mountpoint.
	 */
	qsort(mountpoints, used, sizeof (char *), mountpoint_compare);

	/*
	 * Walk through and first unshare everything.
	 */
	for (i = 0; i < used; i++) {
		zfs_share_proto_t *curr_proto;
		for (curr_proto = share_all_proto; *curr_proto != PROTO_END;
		    curr_proto++) {
			if (is_shared(hdl, mountpoints[i], *curr_proto) &&
			    unshare_one(hdl, mountpoints[i],
			    mountpoints[i], *curr_proto) != 0)
				goto out;
		}
	}

	/*
	 * Now unmount everything, removing the underlying directories as
	 * appropriate.
	 */
	for (i = 0; i < used; i++) {
		if (unmount_one(hdl, mountpoints[i], flags) != 0)
			goto out;
	}

	for (i = 0; i < used; i++) {
		if (datasets[i])
			remove_mountpoint(datasets[i]);
	}

	ret = 0;
out:
	for (i = 0; i < used; i++) {
		if (datasets[i])
			zfs_close(datasets[i]);
		free(mountpoints[i]);
	}
	free(datasets);
	free(mountpoints);

	return (ret);
}
