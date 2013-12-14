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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2011 Lawrence Livermore National Security, LLC.
 */

#include <libintl.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <libzfs.h>
#include <locale.h>
#ifdef HAVE_LIBSELINUX
#include <selinux/selinux.h>
#endif /* HAVE_LIBSELINUX */

libzfs_handle_t *g_zfs;

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
	{ MNTOPT_DIRSYNC,	MS_DIRSYNC,	ZS_COMMENT	},
	{ MNTOPT_NOEXEC,	MS_NOEXEC,	ZS_COMMENT	},
	{ MNTOPT_GROUP,		MS_GROUP,	ZS_COMMENT	},
	{ MNTOPT_NETDEV,	MS_COMMENT,	ZS_COMMENT	},
	{ MNTOPT_NOFAIL,	MS_COMMENT,	ZS_COMMENT	},
	{ MNTOPT_NOSUID,	MS_NOSUID,	ZS_COMMENT	},
	{ MNTOPT_OWNER,		MS_OWNER,	ZS_COMMENT	},
	{ MNTOPT_REMOUNT,	MS_REMOUNT,	ZS_COMMENT	},
	{ MNTOPT_RO,		MS_RDONLY,	ZS_COMMENT	},
	{ MNTOPT_RW,		MS_COMMENT,	ZS_COMMENT	},
	{ MNTOPT_SYNC,		MS_SYNCHRONOUS,	ZS_COMMENT	},
	{ MNTOPT_USER,		MS_USERS,	ZS_COMMENT	},
	{ MNTOPT_USERS,		MS_USERS,	ZS_COMMENT	},
#ifdef MS_NOATIME
	{ MNTOPT_NOATIME,	MS_NOATIME,	ZS_COMMENT	},
#endif
#ifdef MS_NODIRATIME
	{ MNTOPT_NODIRATIME,	MS_NODIRATIME,	ZS_COMMENT	},
#endif
#ifdef MS_RELATIME
	{ MNTOPT_RELATIME,	MS_RELATIME,	ZS_COMMENT	},
#endif
#ifdef MS_STRICTATIME
	{ MNTOPT_DFRATIME,	MS_STRICTATIME,	ZS_COMMENT	},
#endif
	{ MNTOPT_CONTEXT,	MS_COMMENT,	ZS_NOCONTEXT	},
	{ MNTOPT_NOCONTEXT,	MS_COMMENT,	ZS_NOCONTEXT	},
	{ MNTOPT_FSCONTEXT,	MS_COMMENT,	ZS_NOCONTEXT	},
	{ MNTOPT_DEFCONTEXT,	MS_COMMENT,	ZS_NOCONTEXT	},
	{ MNTOPT_ROOTCONTEXT,	MS_COMMENT,	ZS_NOCONTEXT	},
#ifdef MS_I_VERSION
	{ MNTOPT_IVERSION,	MS_I_VERSION,	ZS_COMMENT	},
#endif
#ifdef MS_MANDLOCK
	{ MNTOPT_NBMAND,	MS_MANDLOCK,	ZS_COMMENT	},
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
static int
parse_options(char *mntopts, unsigned long *mntflags, unsigned long *zfsflags,
    int sloppy, char *badopt, char *mtabopt)
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
			    !(*zfsflags & ZS_ZFSUTIL)) {
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

/*
 * Return the pool/dataset to mount given the name passed to mount.  This
 * is expected to be of the form pool/dataset, however may also refer to
 * a block device if that device contains a valid zfs label.
 */
static char *
parse_dataset(char *dataset)
{
	char cwd[PATH_MAX];
	struct stat64 statbuf;
	int error;
	int len;

	/*
	 * We expect a pool/dataset to be provided, however if we're
	 * given a device which is a member of a zpool we attempt to
	 * extract the pool name stored in the label.  Given the pool
	 * name we can mount the root dataset.
	 */
	error = stat64(dataset, &statbuf);
	if (error == 0) {
		nvlist_t *config;
		char *name;
		int fd;

		fd = open(dataset, O_RDONLY);
		if (fd < 0)
			goto out;

		error = zpool_read_label(fd, &config);
		(void) close(fd);
		if (error)
			goto out;

		error = nvlist_lookup_string(config,
		    ZPOOL_CONFIG_POOL_NAME, &name);
		if (error) {
			nvlist_free(config);
		} else {
			dataset = strdup(name);
			nvlist_free(config);
			return (dataset);
		}
	}
out:
	/*
	 * If a file or directory in your current working directory is
	 * named 'dataset' then mount(8) will prepend your current working
	 * directory to the dataset.  There is no way to prevent this
	 * behavior so we simply check for it and strip the prepended
	 * patch when it is added.
	 */
	if (getcwd(cwd, PATH_MAX) == NULL)
		return (dataset);

	len = strlen(cwd);

	/* Do not add one when cwd already ends in a trailing '/' */
	if (!strncmp(cwd, dataset, len))
		return (dataset + len + (cwd[len-1] != '/'));

	return (dataset);
}

/*
 * Update the mtab_* code to use the libmount library when it is commonly
 * available otherwise fallback to legacy mode.  The mount(8) utility will
 * manage the lock file for us to prevent racing updates to /etc/mtab.
 */
static int
mtab_is_writeable(void)
{
	struct stat st;
	int error, fd;

	error = lstat(MNTTAB, &st);
	if (error || S_ISLNK(st.st_mode))
		return (0);

	fd = open(MNTTAB, O_RDWR | O_CREAT, 0644);
	if (fd < 0)
		return (0);

	close(fd);
	return (1);
}

static int
mtab_update(char *dataset, char *mntpoint, char *type, char *mntopts)
{
	struct mntent mnt;
	FILE *fp;
	int error;

	mnt.mnt_fsname = dataset;
	mnt.mnt_dir = mntpoint;
	mnt.mnt_type = type;
	mnt.mnt_opts = mntopts ? mntopts : "";
	mnt.mnt_freq = 0;
	mnt.mnt_passno = 0;

	fp = setmntent(MNTTAB, "a+");
	if (!fp) {
		(void) fprintf(stderr, gettext(
		    "filesystem '%s' was mounted, but %s "
		    "could not be opened due to error %d\n"),
		    dataset, MNTTAB, errno);
		return (MOUNT_FILEIO);
	}

	error = addmntent(fp, &mnt);
	if (error) {
		(void) fprintf(stderr, gettext(
		    "filesystem '%s' was mounted, but %s "
		    "could not be updated due to error %d\n"),
		    dataset, MNTTAB, errno);
		return (MOUNT_FILEIO);
	}

	(void) endmntent(fp);

	return (MOUNT_SUCCESS);
}

int
main(int argc, char **argv)
{
	zfs_handle_t *zhp;
	char legacy[ZFS_MAXPROPLEN];
	char mntopts[MNT_LINE_MAX] = { '\0' };
	char badopt[MNT_LINE_MAX] = { '\0' };
	char mtabopt[MNT_LINE_MAX] = { '\0' };
	char mntpoint[PATH_MAX];
	char *dataset;
	unsigned long mntflags = 0, zfsflags = 0, remount = 0;
	int sloppy = 0, fake = 0, verbose = 0, nomtab = 0, zfsutil = 0;
	int error, c;

	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

	opterr = 0;

	/* check options */
	while ((c = getopt(argc, argv, "sfnvo:h?")) != -1) {
		switch (c) {
		case 's':
			sloppy = 1;
			break;
		case 'f':
			fake = 1;
			break;
		case 'n':
			nomtab = 1;
			break;
		case 'v':
			verbose++;
			break;
		case 'o':
			(void) strlcpy(mntopts, optarg, sizeof (mntopts));
			break;
		case 'h':
		case '?':
			(void) fprintf(stderr, gettext("Invalid option '%c'\n"),
			    optopt);
			(void) fprintf(stderr, gettext("Usage: mount.zfs "
			    "[-sfnv] [-o options] <dataset> <mountpoint>\n"));
			return (MOUNT_USAGE);
		}
	}

	argc -= optind;
	argv += optind;

	/* check that we only have two arguments */
	if (argc != 2) {
		if (argc == 0)
			(void) fprintf(stderr, gettext("missing dataset "
			    "argument\n"));
		else if (argc == 1)
			(void) fprintf(stderr,
			    gettext("missing mountpoint argument\n"));
		else
			(void) fprintf(stderr, gettext("too many arguments\n"));
		(void) fprintf(stderr, "usage: mount <dataset> <mountpoint>\n");
		return (MOUNT_USAGE);
	}

	dataset = parse_dataset(argv[0]);

	/* canonicalize the mount point */
	if (realpath(argv[1], mntpoint) == NULL) {
		(void) fprintf(stderr, gettext("filesystem '%s' cannot be "
		    "mounted at '%s' due to canonicalization error %d.\n"),
		    dataset, argv[1], errno);
		return (MOUNT_SYSERR);
	}

	/* validate mount options and set mntflags */
	error = parse_options(mntopts, &mntflags, &zfsflags, sloppy,
	    badopt, mtabopt);
	if (error) {
		switch (error) {
		case ENOMEM:
			(void) fprintf(stderr, gettext("filesystem '%s' "
			    "cannot be mounted due to a memory allocation "
			    "failure.\n"), dataset);
			return (MOUNT_SYSERR);
		case ENOENT:
			(void) fprintf(stderr, gettext("filesystem '%s' "
			    "cannot be mounted due to invalid option "
			    "'%s'.\n"), dataset, badopt);
			(void) fprintf(stderr, gettext("Use the '-s' option "
			    "to ignore the bad mount option.\n"));
			return (MOUNT_USAGE);
		default:
			(void) fprintf(stderr, gettext("filesystem '%s' "
			    "cannot be mounted due to internal error %d.\n"),
			    dataset, error);
			return (MOUNT_SOFTWARE);
		}
	}

#ifdef HAVE_LIBSELINUX
	/*
	 * Automatically add the default zfs context when selinux is enabled
	 * and the caller has not specified their own context.  This must be
	 * done until zfs is added to the default selinux policy configuration
	 * as a known filesystem type which supports xattrs.
	 */
        if (is_selinux_enabled() && !(zfsflags & ZS_NOCONTEXT)) {
                (void) strlcat(mntopts, ",context=\"system_u:"
                    "object_r:file_t:s0\"", sizeof (mntopts));
                (void) strlcat(mtabopt, ",context=\"system_u:"
                    "object_r:file_t:s0\"", sizeof (mtabopt));
	}
#endif /* HAVE_LIBSELINUX */


	if (verbose)
		(void) fprintf(stdout, gettext("mount.zfs:\n"
		    "  dataset:    \"%s\"\n  mountpoint: \"%s\"\n"
		    "  mountflags: 0x%lx\n  zfsflags:   0x%lx\n"
		    "  mountopts:  \"%s\"\n  mtabopts:   \"%s\"\n"),
		    dataset, mntpoint, mntflags, zfsflags, mntopts, mtabopt);

	if (mntflags & MS_REMOUNT) {
		nomtab = 1;
		remount = 1;
	}

	if (zfsflags & ZS_ZFSUTIL)
		zfsutil = 1;

	if ((g_zfs = libzfs_init()) == NULL)
		return (MOUNT_SYSERR);

	/* try to open the dataset to access the mount point */
	if ((zhp = zfs_open(g_zfs, dataset,
	    ZFS_TYPE_FILESYSTEM | ZFS_TYPE_SNAPSHOT)) == NULL) {
		(void) fprintf(stderr, gettext("filesystem '%s' cannot be "
		    "mounted, unable to open the dataset\n"), dataset);
		libzfs_fini(g_zfs);
		return (MOUNT_USAGE);
	}

	/* treat all snapshots as legacy mount points */
	if (zfs_get_type(zhp) == ZFS_TYPE_SNAPSHOT)
		(void) strlcpy(legacy, ZFS_MOUNTPOINT_LEGACY, ZFS_MAXPROPLEN);
	else
		(void) zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, legacy,
		    sizeof (legacy), NULL, NULL, 0, B_FALSE);

	zfs_close(zhp);
	libzfs_fini(g_zfs);

	/*
	 * Legacy mount points may only be mounted using 'mount', never using
	 * 'zfs mount'.  However, since 'zfs mount' actually invokes 'mount'
	 * we differentiate the two cases using the 'zfsutil' mount option.
	 * This mount option should only be supplied by the 'zfs mount' util.
	 *
	 * The only exception to the above rule is '-o remount' which is
	 * always allowed for non-legacy datasets.  This is done because when
	 * using zfs as your root file system both rc.sysinit/umountroot and
	 * systemd depend on 'mount -o remount <mountpoint>' to work.
	 */
	if (zfsutil && !strcmp(legacy, ZFS_MOUNTPOINT_LEGACY)) {
		(void) fprintf(stderr, gettext(
		    "filesystem '%s' cannot be mounted using 'zfs mount'.\n"
		    "Use 'zfs set mountpoint=%s' or 'mount -t zfs %s %s'.\n"
		    "See zfs(8) for more information.\n"),
		   dataset, mntpoint, dataset, mntpoint);
		return (MOUNT_USAGE);
	}

	if (!zfsutil && !(remount || fake) &&
	    strcmp(legacy, ZFS_MOUNTPOINT_LEGACY)) {
		(void) fprintf(stderr, gettext(
		    "filesystem '%s' cannot be mounted using 'mount'.\n"
		    "Use 'zfs set mountpoint=%s' or 'zfs mount %s'.\n"
		    "See zfs(8) for more information.\n"),
		    dataset, "legacy", dataset);
		return (MOUNT_USAGE);
	}

	if (!fake) {
		error = mount(dataset, mntpoint, MNTTYPE_ZFS,
		    mntflags, mntopts);
		if (error) {
			switch (errno) {
			case ENOENT:
				(void) fprintf(stderr, gettext("mount point "
				    "'%s' does not exist\n"), mntpoint);
				return (MOUNT_SYSERR);
			case EBUSY:
				(void) fprintf(stderr, gettext("filesystem "
				    "'%s' is already mounted\n"), dataset);
				return (MOUNT_BUSY);
			default:
				(void) fprintf(stderr, gettext("filesystem "
				    "'%s' can not be mounted due to error "
				    "%d\n"), dataset, errno);
				return (MOUNT_USAGE);
			}
		}
	}

	if (!nomtab && mtab_is_writeable()) {
		error = mtab_update(dataset, mntpoint, MNTTYPE_ZFS, mtabopt);
		if (error)
			return (error);
	}

	return (MOUNT_SUCCESS);
}
