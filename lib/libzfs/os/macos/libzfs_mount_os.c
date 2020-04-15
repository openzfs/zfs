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
#include "../../libzfs_impl.h"
#include <libzfs.h>

#include <thread_pool.h>
#include <sys/sysctl.h>
#include <libzutil.h>
#include <libdiskmgt.h>

#include <IOKit/IOBSD.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/storage/IOMedia.h>

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

	if (getenv("__ZFS_DISABLE_VOLUME_ICON") != NULL)
		return;

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

static void
check_special(zfs_handle_t *zhp)
{
	zpool_handle_t *zph = zhp->zpool_hdl;
	uint64_t feat_refcount;
	nvlist_t *features;

	/* check that features can be enabled */
	if (zpool_get_prop_int(zph, ZPOOL_PROP_VERSION, NULL)
	    < SPA_VERSION_FEATURES)
		return;

	/* SPA_FEATURE_PROJECT_QUOTA SPA_FEATURE_USEROBJ_ACCOUNTING */
	features = zpool_get_features(zph);
	if (!features)
		return;

	if (nvlist_lookup_uint64(features,
	    spa_feature_table[SPA_FEATURE_PROJECT_QUOTA].fi_guid,
	    &feat_refcount) != 0 &&
	    nvlist_lookup_uint64(features,
	    spa_feature_table[SPA_FEATURE_USEROBJ_ACCOUNTING].fi_guid,
	    &feat_refcount) != 0)
		return;

	printf(gettext("If importing from zfs-1.9.4 (or earlier), "
	    "then possibly enable features: \n"
	    "    project_quota & userobj_accounting\n"));

}

/* Not entirely sure what these do, but let's keep it close to upstream */

#define	ZS_COMMENT	0x00000000	/* comment */
#define	ZS_ZFSUTIL	0x00000001	/* caller is zfs(8) */

typedef struct option_map {
	const char *name;
	unsigned long mntmask;
	unsigned long zfsmask;
} option_map_t;

static const option_map_t option_map[] = {
	/* Canonicalized filesystem independent options from mount(8) */
	{ MNTOPT_NOAUTO,	MS_COMMENT,	ZS_COMMENT	},
	{ MNTOPT_DEFAULTS,	MS_COMMENT,	ZS_COMMENT	},
	{ MNTOPT_NODEVICES,	MS_NODEV,	ZS_COMMENT	},
	{ MNTOPT_DEVICES,	MS_COMMENT,	ZS_COMMENT	},
	{ MNTOPT_DIRSYNC,	MS_DIRSYNC,	ZS_COMMENT	},
	{ MNTOPT_NOEXEC,	MS_NOEXEC,	ZS_COMMENT	},
	{ MNTOPT_EXEC,		MS_COMMENT,	ZS_COMMENT	},
	{ MNTOPT_GROUP,		MS_GROUP,	ZS_COMMENT	},
	{ MNTOPT_NETDEV,	MS_COMMENT,	ZS_COMMENT	},
	{ MNTOPT_NOFAIL,	MS_COMMENT,	ZS_COMMENT	},
	{ MNTOPT_NOSETUID,	MS_NOSUID,	ZS_COMMENT	},
	{ MNTOPT_SETUID,	MS_COMMENT,	ZS_COMMENT	},
	{ MNTOPT_OWNER,		MS_OWNER,	ZS_COMMENT	},
	{ MNTOPT_REMOUNT,	MS_REMOUNT,	ZS_COMMENT	},
	{ MNTOPT_RO,		MS_RDONLY,	ZS_COMMENT	},
	{ MNTOPT_RW,		MS_COMMENT,	ZS_COMMENT	},
	{ MNTOPT_SYNC,		MS_SYNCHRONOUS,	ZS_COMMENT	},
	{ MNTOPT_USER,		MS_USERS,	ZS_COMMENT	},
	{ MNTOPT_USERS,		MS_USERS,	ZS_COMMENT	},
	/* acl flags passed with util-linux-2.24 mount command */
	{ MNTOPT_ACL,		MS_POSIXACL,	ZS_COMMENT	},
	{ MNTOPT_NOACL,		MS_COMMENT,	ZS_COMMENT	},
	{ MNTOPT_POSIXACL,	MS_POSIXACL,	ZS_COMMENT	},
#ifdef MS_NOATIME
	{ MNTOPT_NOATIME,	MS_NOATIME,	ZS_COMMENT	},
	{ MNTOPT_ATIME,		MS_COMMENT,	ZS_COMMENT	},
#endif
#ifdef MS_NODIRATIME
	{ MNTOPT_NODIRATIME,	MS_NODIRATIME,	ZS_COMMENT	},
	{ MNTOPT_DIRATIME,	MS_COMMENT,	ZS_COMMENT	},
#endif
#ifdef MS_RELATIME
	{ MNTOPT_RELATIME,	MS_RELATIME,	ZS_COMMENT	},
	{ MNTOPT_NORELATIME,	MS_COMMENT,	ZS_COMMENT	},
#endif
#ifdef MS_STRICTATIME
	{ MNTOPT_STRICTATIME,	MS_STRICTATIME,	ZS_COMMENT	},
	{ MNTOPT_NOSTRICTATIME,	MS_COMMENT,	ZS_COMMENT	},
#endif
#ifdef MS_LAZYTIME
	{ MNTOPT_LAZYTIME,	MS_LAZYTIME,	ZS_COMMENT	},
#endif
	{ MNTOPT_CONTEXT,	MS_COMMENT,	ZS_COMMENT	},
	{ MNTOPT_FSCONTEXT,	MS_COMMENT,	ZS_COMMENT	},
	{ MNTOPT_DEFCONTEXT,	MS_COMMENT,	ZS_COMMENT	},
	{ MNTOPT_ROOTCONTEXT,	MS_COMMENT,	ZS_COMMENT	},
#ifdef MS_I_VERSION
	{ MNTOPT_IVERSION,	MS_I_VERSION,	ZS_COMMENT	},
#endif
#ifdef MS_MANDLOCK
	{ MNTOPT_NBMAND,	MS_MANDLOCK,	ZS_COMMENT	},
	{ MNTOPT_NONBMAND,	MS_COMMENT,	ZS_COMMENT	},
#endif
	/* Valid options not found in mount(8) */
	{ MNTOPT_BIND,		MS_BIND,	ZS_COMMENT	},
#ifdef MS_REC
	{ MNTOPT_RBIND,		MS_BIND|MS_REC,	ZS_COMMENT	},
#endif
	{ MNTOPT_COMMENT,	MS_COMMENT,	ZS_COMMENT	},
#ifdef MS_NOSUB
	{ MNTOPT_NOSUB,		MS_NOSUB,	ZS_COMMENT	},
#endif
#ifdef MS_SILENT
	{ MNTOPT_QUIET,		MS_SILENT,	ZS_COMMENT	},
#endif
	/* Custom zfs options */
	{ MNTOPT_XATTR,		MS_COMMENT,	ZS_COMMENT	},
	{ MNTOPT_NOXATTR,	MS_COMMENT,	ZS_COMMENT	},
	{ MNTOPT_ZFSUTIL,	MS_COMMENT,	ZS_ZFSUTIL	},
	{ NULL,			0,		0		} };

/*
 * Break the mount option in to a name/value pair.  The name is
 * validated against the option map and mount flags set accordingly.
 */
static int
parse_option(char *mntopt, unsigned long *mntflags,
    unsigned long *zfsflags, int sloppy)
{
	const option_map_t *opt;
	char *ptr, *name, *value = NULL;
	int error = 0;

	name = strdup(mntopt);
	if (name == NULL)
		return (ENOMEM);

	for (ptr = name; ptr && *ptr; ptr++) {
		if (*ptr == '=') {
			*ptr = '\0';
			value = ptr+1;
			VERIFY3P(value, !=, NULL);
			break;
		}
	}

	for (opt = option_map; opt->name != NULL; opt++) {
		if (strncmp(name, opt->name, strlen(name)) == 0) {
			*mntflags |= opt->mntmask;
			*zfsflags |= opt->zfsmask;
			error = 0;
			goto out;
		}
	}

	if (!sloppy)
		error = ENOENT;
out:
	/* If required further process on the value may be done here */
	free(name);
	return (error);
}

/*
 * Translate the mount option string in to MS_* mount flags for the
 * kernel vfs.  When sloppy is non-zero unknown options will be ignored
 * otherwise they are considered fatal are copied in to badopt.
 */
int
zfs_parse_mount_options(const char *mntopts, unsigned long *mntflags,
    unsigned long *zfsflags, int sloppy, char *badopt, char *mtabopt)
{
	int error = 0, quote = 0, flag = 0, count = 0;
	char *ptr, *opt, *opts;

	opts = strdup(mntopts);
	if (opts == NULL)
		return (ENOMEM);

	*mntflags = 0;
	opt = NULL;

	/*
	 * Scan through all mount options which must be comma delimited.
	 * We must be careful to notice regions which are double quoted
	 * and skip commas in these regions.  Each option is then checked
	 * to determine if it is a known option.
	 */
	for (ptr = opts; ptr && !flag; ptr++) {
		if (opt == NULL)
			opt = ptr;

		if (*ptr == '"')
			quote = !quote;

		if (quote)
			continue;

		if (*ptr == '\0')
			flag = 1;

		if ((*ptr == ',') || (*ptr == '\0')) {
			*ptr = '\0';

			error = parse_option(opt, mntflags, zfsflags, sloppy);
			if (error) {
				strcpy(badopt, opt);
				goto out;

			}

			if (!(*mntflags & MS_REMOUNT) &&
			    !(*zfsflags & ZS_ZFSUTIL) &&
			    mtabopt != NULL) {
				if (count > 0)
					strlcat(mtabopt, ",", MNT_LINE_MAX);

				strlcat(mtabopt, opt, MNT_LINE_MAX);
				count++;
			}

			opt = NULL;
		}
	}

out:
	free(opts);
	return (error);
}

static void
append_mntopt(const char *name, const char *val, char *mntopts,
    char *mtabopt, boolean_t quote)
{
	char tmp[MNT_LINE_MAX];

	snprintf(tmp, MNT_LINE_MAX, quote ? ",%s=\"%s\"" : ",%s=%s", name, val);

	if (mntopts)
		strlcat(mntopts, tmp, MNT_LINE_MAX);

	if (mtabopt)
		strlcat(mtabopt, tmp, MNT_LINE_MAX);
}

void
zfs_adjust_mount_options(zfs_handle_t *zhp, const char *mntpoint,
    char *mntopts, char *mtabopt)
{
	(void) zhp;
	(void) mntopts;
	(void) mtabopt;

	/* A hint used to determine an auto-mounted snapshot mount point */
	append_mntopt(MNTOPT_MNTPOINT, mntpoint, mntopts, NULL, B_FALSE);
}

/*
 * if (zmount(zhp, zfs_get_name(zhp), mountpoint, MS_OPTIONSTR | flags,
 * MNTTYPE_ZFS, NULL, 0, mntopts, sizeof (mntopts)) != 0) {
 */
int
do_mount(zfs_handle_t *zhp, const char *dir, const char *optptr, int mflag)
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
	char *value = NULL;
	nvlist_t *args = NULL;

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

			strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));

			zcmd_alloc_dst_nvlist(zhp->zfs_hdl, &zc, 0);

			args = fnvlist_alloc();
			fnvlist_add_string(args, ZPOOL_CONFIG_POOL_NAME,
			    zhp->zfs_name);
			zcmd_write_src_nvlist(zhp->zfs_hdl, &zc, args);

			rv = zfs_ioctl(zhp->zfs_hdl,
			    ZFS_IOC_PROXY_DATASET, &zc);

			/* Free innvl */
			nvlist_free(args);
			args = NULL;

			/* args = outnvl */

			if (rv == 0 &&
			    zcmd_read_dst_nvlist(zhp->zfs_hdl, &zc, &args) == 0)
				if (nvlist_exists(args, ZPOOL_CONFIG_PATH))
					value = fnvlist_lookup_string(args,
					    ZPOOL_CONFIG_PATH);

			zcmd_free_nvlists(&zc);

#ifdef DEBUG
			if (rv)
				fprintf(stderr,
				    "proxy dataset returns %d '%s'\n",
				    rv, value ? value : "");
#endif

			// Mount using /dev/diskX, use temporary buffer to
			// give it full name
			if (rv == 0 && value != NULL) {
				snprintf(zc.zc_name, sizeof (zc.zc_name),
				    "/dev/%s", value);
				mnt_args.fspec = zc.zc_name;
			}

			/* free outnvl */
			if (args != NULL)
				nvlist_free(args);
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

	/* Check if we need to create/update icon */
	if (rv == 0)
		zfs_mount_seticon(dir);
	else
		rv = errno;

	/* 1.9.4 did not have projectquotas, check if user should upgrade */
	if (rv == EIO)
		check_special(zhp);


	if (rpath) free(rpath);

	return (rv);
}

static int
do_unmount_impl(const char *mntpt, int flags)
{
	const char force_opt[] = "force";
	const char *argv[7] = {
		"/usr/sbin/diskutil",
		"unmount",
		NULL, NULL, NULL, NULL };
	int rc, count = 2;

	if (flags & MS_FORCE) {
		argv[count] = force_opt;
		count++;
	}

	argv[count] = (const char *)mntpt;
	rc = libzfs_run_process(argv[0], (char **)argv,
	    STDOUT_VERBOSE|STDERR_VERBOSE);

	/*
	 * There is a bug, where we can not unmount, with the error
	 * already unmounted, even though it wasn't. But it is easy
	 * to work around by calling 'umount'. Until a real fix is done...
	 * re-test this: 202004/lundman
	 */
	if (rc != 0) {
		const char *argv[7] = {
		    "/sbin/umount",
		    NULL, NULL, NULL, NULL };
		int count = 1;

		if (flags & MS_FORCE) {
			argv[count] = "-f";
			count++;
		}
		argv[count] = (const char *)mntpt;
		rc = libzfs_run_process(argv[0], (char **)argv,
		    STDOUT_VERBOSE|STDERR_VERBOSE);
	}

	return (rc ? EINVAL : 0);
}


void unmount_snapshots(zfs_handle_t *zhp, const char *mntpt, int flags);

int
do_unmount(zfs_handle_t *zhp, const char *mntpt, int flags)
{
	int rv = 0;
	/*
	 * On OSX, the kernel can not unmount all snapshots for us, as XNU
	 * rejects the unmount before it reaches ZFS. But we can easily handle
	 * unmounting snapshots from userland.
	 */
	unmount_snapshots(zhp, mntpt, flags);

	rv = do_unmount_impl(mntpt, flags);

	/* We might need to remove the proxy as well */
	if (rv == 0 && zhp != NULL) {
		zfs_cmd_t zc = { "\0" };
		nvlist_t *args = NULL;

		strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));

		zcmd_alloc_dst_nvlist(zhp->zfs_hdl, &zc, 0);

		args = fnvlist_alloc();
		fnvlist_add_string(args, ZPOOL_CONFIG_POOL_NAME,
		    zhp->zfs_name);
		zcmd_write_src_nvlist(zhp->zfs_hdl, &zc, args);

		rv = zfs_ioctl(zhp->zfs_hdl, ZFS_IOC_PROXY_REMOVE, &zc);

		/* Free innvl */
		nvlist_free(args);
		args = NULL;

		/* args = outnvl */

		zcmd_free_nvlists(&zc);

		// We don't care about proxy failing
		rv = 0;
	}

	return (rv);
}

/*
 * Given "/Volumes/BOOM" look for any lower mounts with ".zfs/snapshot/"
 * in them - issue unmount.
 */
void
unmount_snapshots(zfs_handle_t *zhp, const char *mntpt, int flags)
{
	struct mnttab entry;
	int mntlen = strlen(mntpt);

	(void) flags;

	if (zhp == NULL)
		return;
	/*
	 * The automounting will kick in, and zed mounts it - so
	 * we temporarily disable it
	 */
	uint64_t automount = getpid();
	uint64_t saved_automount = 0;
	size_t len = sizeof (automount);
	size_t slen = sizeof (saved_automount);

	/* Remember what the user has it set to */
	sysctlbyname("kstat.zfs.darwin.tunable.zfs_auto_snapshot",
	    &saved_automount, &slen, NULL, 0);

	/* Disable automounting */
	sysctlbyname("kstat.zfs.darwin.tunable.zfs_auto_snapshot",
	    NULL, NULL, &automount, len);

	while (getmntent(NULL, &entry) == 0) {
		/* Starts with our mountpoint ? */
		if (strncmp(mntpt, entry.mnt_mountp, mntlen) == 0) {
			/* The next part is "/.zfs/snapshot/" ? */
			if (strncmp("/.zfs/snapshot/",
			    &entry.mnt_mountp[mntlen], 15) == 0) {
				/* Unmount it */
				do_unmount_impl(entry.mnt_mountp, MS_FORCE);
			}
		}
	}

	/* Restore automount setting */
	sysctlbyname("kstat.zfs.darwin.tunable.zfs_auto_snapshot",
	    NULL, NULL, &saved_automount, len);
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
	uint64_t automount = getpid();
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
		ret = EBUSY;
		goto out;
	}

	mountpoint = zfs_snapshot_mountpoint(zhp);
	if (mountpoint == NULL) {
		ret = EINVAL;
		goto out;
	}

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

out:
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

	/*
	 * The automounting will kick in, and zed mounts it - so
	 * we temporarily disable it
	 */
	uint64_t automount = getpid();
	uint64_t saved_automount = 0;
	size_t len = sizeof (automount);
	size_t slen = sizeof (saved_automount);

	/* Remember what the user has it set to */
	sysctlbyname("kstat.zfs.darwin.tunable.zfs_auto_snapshot",
	    &saved_automount, &slen, NULL, 0);

	/* Disable automounting */
	sysctlbyname("kstat.zfs.darwin.tunable.zfs_auto_snapshot",
	    NULL, NULL, &automount, len);

	if (!zfs_is_mounted(zhp, NULL)) {
		ret = ENOENT;
		goto out;
	}

	mountpoint = zfs_snapshot_mountpoint(zhp);
	if (mountpoint == NULL) {
		ret = EINVAL;
		goto out;
	}

	ret = zfs_unmount(zhp, mountpoint, flags);

	free(mountpoint);

out:
	sysctlbyname("kstat.zfs.darwin.tunable.zfs_auto_snapshot",
	    NULL, NULL, &saved_automount, len);

	return (ret);
}

static int
do_unmount_volume(const char *mntpt, int flags)
{
	const char force_opt[] = "force";
	const char *argv[7] = {
	    "/usr/sbin/diskutil",
	    NULL, NULL, NULL, NULL };
	int rc, count = 1;

	// Check if ends with "s1" partition
	int idx = strlen(mntpt);
	while (idx > 0 &&
	    isdigit(mntpt[idx]))
		idx--;
	if (mntpt[idx] == 's')
		argv[count++] = "unmount";
	else
		argv[count++] = "unmountDisk";

	if (flags & MS_FORCE) {
		argv[count] = force_opt;
		count++;
	}

	argv[count] = (const char *)mntpt;
	rc = libzfs_run_process(argv[0], (char **)argv,
	    STDOUT_VERBOSE|STDERR_VERBOSE);

	return (rc ? EINVAL : 0);
}

void
zpool_disable_volume_os(const char *name)
{
	CFMutableDictionaryRef matching = 0;
	char *fullname = NULL;
	int result;
	io_service_t service = 0;
	io_service_t child;
	char bsdstr[MAXPATHLEN];
	char bsdstr2[MAXPATHLEN];
	CFStringRef bsdname;
	CFStringRef bsdname2;
	io_iterator_t iter;

	if (asprintf(&fullname, "ZVOL %s Media", name) < 0)
		return;

	matching = IOServiceNameMatching(fullname);
	if (matching == 0)
		goto out;
	service = IOServiceGetMatchingService(kIOMasterPortDefault, matching);
	if (service == 0)
		goto out;
	// printf("GetMatching said %p\n", service);

	// Get BSDName?
	bsdname = IORegistryEntryCreateCFProperty(service,
	    CFSTR(kIOBSDNameKey), kCFAllocatorDefault, 0);
	if (bsdname &&
	    CFStringGetCString(bsdname, bsdstr, sizeof (bsdstr),
	    kCFStringEncodingUTF8)) {
		// printf("BSDName '%s'\n", bsdstr);

		// Now loop through and check if apfs has any synthesized
		// garbage attached, as they didnt make "diskutil unmountdisk"
		// handle it, we have to do it manually. (minus 1 apple!)

		result = IORegistryEntryCreateIterator(service,
		    kIOServicePlane,
		    kIORegistryIterateRecursively,
		    &iter);

		// printf("iterating ret %d \n", result);
		if (result == 0) {
			while ((child = IOIteratorNext(iter)) != 0) {

				bsdname2 = IORegistryEntryCreateCFProperty(
				    child, CFSTR(kIOBSDNameKey),
				    kCFAllocatorDefault, 0);

				if (bsdname2 &&
				    CFStringGetCString(bsdname2, bsdstr2,
				    sizeof (bsdstr2), kCFStringEncodingUTF8)) {
					CFRelease(bsdname2);
					printf(
					    "... asking apfs to eject '%s'\n",
					    bsdstr2);
					do_unmount_volume(bsdstr2, 0);

				} // Has BSDName?

				IOObjectRelease(child);
			}
			IOObjectRelease(iter);
		} // iterate

		CFRelease(bsdname);
		printf("... asking ZVOL to export '%s'\n", bsdstr);
		do_unmount_volume(bsdstr, 0);
	}

out:
	if (service != 0)
		IOObjectRelease(service);
	if (fullname != NULL)
		free(fullname);

}

static int
zpool_disable_volumes(zfs_handle_t *nzhp, void *data)
{
	// Same pool?
	if (nzhp && nzhp->zpool_hdl && zpool_get_name(nzhp->zpool_hdl) &&
	    data &&
	    strcmp(zpool_get_name(nzhp->zpool_hdl), (char *)data) == 0) {
		if (zfs_get_type(nzhp) == ZFS_TYPE_VOLUME) {
			/*
			 *      /var/run/zfs/zvol/dsk/$POOL/$volume
			 */

			zpool_disable_volume_os(zfs_get_name(nzhp));

		}
	}
	(void) zfs_iter_children(nzhp, 0, zpool_disable_volumes, data);
	zfs_close(nzhp);
	return (0);
}

/*
 * Since volumes can be mounted, we need to ask diskutil to unmountdisk
 * to make sure Spotlight and all that, let go of the mount.
 */
void
zpool_disable_datasets_os(zpool_handle_t *zhp, boolean_t force)
{
	libzfs_handle_t *hdl = zhp->zpool_hdl;

	(void) force;
	zfs_iter_root(hdl, zpool_disable_volumes, (void *)zpool_get_name(zhp));
}
