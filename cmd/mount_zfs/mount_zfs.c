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
#include <sys/mntent.h>
#include <sys/stat.h>
#include <libzfs.h>
#include <libzutil.h>
#include <locale.h>
#include <getopt.h>
#include <fcntl.h>
#include <errno.h>

#define	ZS_COMMENT	0x00000000	/* comment */
#define	ZS_ZFSUTIL	0x00000001	/* caller is zfs(8) */

libzfs_handle_t *g_zfs;

/*
 * Opportunistically convert a target string into a pool name. If the
 * string does not represent a block device with a valid zfs label
 * then it is passed through without modification.
 */
static void
parse_dataset(const char *target, char **dataset)
{
	/*
	 * Prior to util-linux 2.36.2, if a file or directory in the
	 * current working directory was named 'dataset' then mount(8)
	 * would prepend the current working directory to the dataset.
	 * Check for it and strip the prepended path when it is added.
	 */
	char cwd[PATH_MAX];
	if (getcwd(cwd, PATH_MAX) == NULL) {
		perror("getcwd");
		return;
	}
	int len = strlen(cwd);
	if (strncmp(cwd, target, len) == 0)
		target += len;

	/* Assume pool/dataset is more likely */
	strlcpy(*dataset, target, PATH_MAX);

	int fd = open(target, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return;

	nvlist_t *cfg = NULL;
	if (zpool_read_label(fd, &cfg, NULL) == 0) {
		char *nm = NULL;
		if (!nvlist_lookup_string(cfg, ZPOOL_CONFIG_POOL_NAME, &nm))
			strlcpy(*dataset, nm, PATH_MAX);
		nvlist_free(cfg);
	}

	if (close(fd))
		perror("close");
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

	error = lstat("/etc/mtab", &st);
	if (error || S_ISLNK(st.st_mode))
		return (0);

	fd = open("/etc/mtab", O_RDWR | O_CREAT, 0644);
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

	fp = setmntent("/etc/mtab", "a+");
	if (!fp) {
		(void) fprintf(stderr, gettext(
		    "filesystem '%s' was mounted, but /etc/mtab "
		    "could not be opened due to error: %s\n"),
		    dataset, strerror(errno));
		return (MOUNT_FILEIO);
	}

	error = addmntent(fp, &mnt);
	if (error) {
		(void) fprintf(stderr, gettext(
		    "filesystem '%s' was mounted, but /etc/mtab "
		    "could not be updated due to error: %s\n"),
		    dataset, strerror(errno));
		return (MOUNT_FILEIO);
	}

	(void) endmntent(fp);

	return (MOUNT_SUCCESS);
}

int
main(int argc, char **argv)
{
	zfs_handle_t *zhp;
	char prop[ZFS_MAXPROPLEN];
	uint64_t zfs_version = 0;
	char mntopts[MNT_LINE_MAX] = { '\0' };
	char badopt[MNT_LINE_MAX] = { '\0' };
	char mtabopt[MNT_LINE_MAX] = { '\0' };
	char mntpoint[PATH_MAX];
	char dataset[PATH_MAX], *pdataset = dataset;
	unsigned long mntflags = 0, zfsflags = 0, remount = 0;
	int sloppy = 0, fake = 0, verbose = 0, nomtab = 0, zfsutil = 0;
	int error, c;

	(void) setlocale(LC_ALL, "");
	(void) setlocale(LC_NUMERIC, "C");
	(void) textdomain(TEXT_DOMAIN);

	opterr = 0;

	/* check options */
	while ((c = getopt_long(argc, argv, "sfnvo:h?", 0, 0)) != -1) {
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

	parse_dataset(argv[0], &pdataset);

	/* canonicalize the mount point */
	if (realpath(argv[1], mntpoint) == NULL) {
		(void) fprintf(stderr, gettext("filesystem '%s' cannot be "
		    "mounted at '%s' due to canonicalization error: %s\n"),
		    dataset, argv[1], strerror(errno));
		return (MOUNT_SYSERR);
	}

	/* validate mount options and set mntflags */
	error = zfs_parse_mount_options(mntopts, &mntflags, &zfsflags, sloppy,
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

	if ((g_zfs = libzfs_init()) == NULL) {
		(void) fprintf(stderr, "%s\n", libzfs_error_init(errno));
		return (MOUNT_SYSERR);
	}

	/* try to open the dataset to access the mount point */
	if ((zhp = zfs_open(g_zfs, dataset,
	    ZFS_TYPE_FILESYSTEM | ZFS_TYPE_SNAPSHOT)) == NULL) {
		(void) fprintf(stderr, gettext("filesystem '%s' cannot be "
		    "mounted, unable to open the dataset\n"), dataset);
		libzfs_fini(g_zfs);
		return (MOUNT_USAGE);
	}

	zfs_adjust_mount_options(zhp, mntpoint, mntopts, mtabopt);

	/* treat all snapshots as legacy mount points */
	if (zfs_get_type(zhp) == ZFS_TYPE_SNAPSHOT)
		(void) strlcpy(prop, ZFS_MOUNTPOINT_LEGACY, ZFS_MAXPROPLEN);
	else
		(void) zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, prop,
		    sizeof (prop), NULL, NULL, 0, B_FALSE);

	/*
	 * Fetch the max supported zfs version in case we get ENOTSUP
	 * back from the mount command, since we need the zfs handle
	 * to do so.
	 */
	zfs_version = zfs_prop_get_int(zhp, ZFS_PROP_VERSION);
	if (zfs_version == 0) {
		fprintf(stderr, gettext("unable to fetch "
		    "ZFS version for filesystem '%s'\n"), dataset);
		return (MOUNT_SYSERR);
	}

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
	if (zfsutil && (strcmp(prop, ZFS_MOUNTPOINT_LEGACY) == 0)) {
		(void) fprintf(stderr, gettext(
		    "filesystem '%s' cannot be mounted using 'zfs mount'.\n"
		    "Use 'zfs set mountpoint=%s' or 'mount -t zfs %s %s'.\n"
		    "See zfs(8) for more information.\n"),
		    dataset, mntpoint, dataset, mntpoint);
		return (MOUNT_USAGE);
	}

	if (!zfsutil && !(remount || fake) &&
	    strcmp(prop, ZFS_MOUNTPOINT_LEGACY)) {
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
	}

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
		case ENOTSUP:
			if (zfs_version > ZPL_VERSION) {
				(void) fprintf(stderr,
				    gettext("filesystem '%s' (v%d) is not "
				    "supported by this implementation of "
				    "ZFS (max v%d).\n"), dataset,
				    (int)zfs_version, (int)ZPL_VERSION);
			} else {
				(void) fprintf(stderr,
				    gettext("filesystem '%s' mount "
				    "failed for unknown reason.\n"), dataset);
			}
			return (MOUNT_SYSERR);
#ifdef MS_MANDLOCK
		case EPERM:
			if (mntflags & MS_MANDLOCK) {
				(void) fprintf(stderr, gettext("filesystem "
				    "'%s' has the 'nbmand=on' property set, "
				    "this mount\noption may be disabled in "
				    "your kernel.  Use 'zfs set nbmand=off'\n"
				    "to disable this option and try to "
				    "mount the filesystem again.\n"), dataset);
				return (MOUNT_SYSERR);
			}
			/* fallthru */
#endif
		default:
			(void) fprintf(stderr, gettext("filesystem "
			    "'%s' can not be mounted: %s\n"), dataset,
			    strerror(errno));
			return (MOUNT_USAGE);
		}
	}

	if (!nomtab && mtab_is_writeable()) {
		error = mtab_update(dataset, mntpoint, MNTTYPE_ZFS, mtabopt);
		if (error)
			return (error);
	}

	return (MOUNT_SUCCESS);
}
