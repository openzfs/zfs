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
 * Copyright (c) 2014, 2021 by Delphix. All rights reserved.
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

#include "../../libzfs_impl.h"
#include <thread_pool.h>

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
	{ MNTOPT_NOSUID,	MS_NOSUID,	ZS_COMMENT	},
	{ MNTOPT_SUID,		MS_COMMENT,	ZS_COMMENT	},
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
zfs_parse_mount_options(char *mntopts, unsigned long *mntflags,
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

static void
zfs_selinux_setcontext(zfs_handle_t *zhp, zfs_prop_t zpt, const char *name,
    char *mntopts, char *mtabopt)
{
	char context[ZFS_MAXPROPLEN];

	if (zfs_prop_get(zhp, zpt, context, sizeof (context),
	    NULL, NULL, 0, B_FALSE) == 0) {
		if (strcmp(context, "none") != 0)
			append_mntopt(name, context, mntopts, mtabopt, B_TRUE);
	}
}

void
zfs_adjust_mount_options(zfs_handle_t *zhp, const char *mntpoint,
    char *mntopts, char *mtabopt)
{
	char prop[ZFS_MAXPROPLEN];

	/*
	 * Checks to see if the ZFS_PROP_SELINUX_CONTEXT exists
	 * if it does, create a tmp variable in case it's needed
	 * checks to see if the selinux context is set to the default
	 * if it is, allow the setting of the other context properties
	 * this is needed because the 'context' property overrides others
	 * if it is not the default, set the 'context' property
	 */
	if (zfs_prop_get(zhp, ZFS_PROP_SELINUX_CONTEXT, prop, sizeof (prop),
	    NULL, NULL, 0, B_FALSE) == 0) {
		if (strcmp(prop, "none") == 0) {
			zfs_selinux_setcontext(zhp, ZFS_PROP_SELINUX_FSCONTEXT,
			    MNTOPT_FSCONTEXT, mntopts, mtabopt);
			zfs_selinux_setcontext(zhp, ZFS_PROP_SELINUX_DEFCONTEXT,
			    MNTOPT_DEFCONTEXT, mntopts, mtabopt);
			zfs_selinux_setcontext(zhp,
			    ZFS_PROP_SELINUX_ROOTCONTEXT, MNTOPT_ROOTCONTEXT,
			    mntopts, mtabopt);
		} else {
			append_mntopt(MNTOPT_CONTEXT, prop,
			    mntopts, mtabopt, B_TRUE);
		}
	}

	/* A hint used to determine an auto-mounted snapshot mount point */
	append_mntopt(MNTOPT_MNTPOINT, mntpoint, mntopts, NULL, B_FALSE);
}

/*
 * By default the filesystem by preparing the mount options (i.e. parsing
 * some flags from the "opts" parameter into the "flags" parameter) and then
 * directly calling the system call mount(2). We don't need the mount utility
 * or update /etc/mtab, because this is a symlink on all modern systems.
 *
 * If the environment variable ZFS_MOUNT_HELPER is set, we fall back to the
 * previous behavior:
 * The filesystem is mounted by invoking the system mount utility rather
 * than by the system call mount(2).  This ensures that the /etc/mtab
 * file is correctly locked for the update.  Performing our own locking
 * and /etc/mtab update requires making an unsafe assumption about how
 * the mount utility performs its locking.  Unfortunately, this also means
 * in the case of a mount failure we do not have the exact errno.  We must
 * make due with return value from the mount process.
 */
int
do_mount(zfs_handle_t *zhp, const char *mntpt, char *opts, int flags)
{
	const char *src = zfs_get_name(zhp);
	int error = 0;

	if (!libzfs_envvar_is_set("ZFS_MOUNT_HELPER")) {
		char badopt[MNT_LINE_MAX] = {0};
		unsigned long mntflags = flags, zfsflags;
		char myopts[MNT_LINE_MAX] = {0};

		if (zfs_parse_mount_options(opts, &mntflags,
		    &zfsflags, 0, badopt, NULL)) {
			return (EINVAL);
		}
		strlcat(myopts, opts, MNT_LINE_MAX);
		zfs_adjust_mount_options(zhp, mntpt, myopts, NULL);
		if (mount(src, mntpt, MNTTYPE_ZFS, mntflags, myopts)) {
			return (errno);
		}
	} else {
		char *argv[9] = {
		    "/bin/mount",
		    "--no-canonicalize",
		    "-t", MNTTYPE_ZFS,
		    "-o", opts,
		    (char *)src,
		    (char *)mntpt,
		    (char *)NULL };

		/* Return only the most critical mount error */
		error = libzfs_run_process(argv[0], argv,
		    STDOUT_VERBOSE|STDERR_VERBOSE);
		if (error) {
			if (error & MOUNT_FILEIO) {
				error = EIO;
			} else if (error & MOUNT_USER) {
				error = EINTR;
			} else if (error & MOUNT_SOFTWARE) {
				error = EPIPE;
			} else if (error & MOUNT_BUSY) {
				error = EBUSY;
			} else if (error & MOUNT_SYSERR) {
				error = EAGAIN;
			} else if (error & MOUNT_USAGE) {
				error = EINVAL;
			} else
				error = ENXIO; /* Generic error */
		}
	}

	return (error);
}

int
do_unmount(zfs_handle_t *zhp, const char *mntpt, int flags)
{
	if (!libzfs_envvar_is_set("ZFS_MOUNT_HELPER")) {
		int rv = umount2(mntpt, flags);

		return (rv < 0 ? errno : 0);
	}

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

/* Called from the tail end of zpool_disable_datasets() */
void
zpool_disable_datasets_os(zpool_handle_t *zhp, boolean_t force)
{
}

/* Called from the tail end of zfs_unmount() */
void
zpool_disable_volume_os(const char *name)
{
}
