// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */
/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Copyright 2006 Ricardo Correia.  All rights reserved.
 * Use is subject to license terms.
 */

/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved	*/

#include <stdio.h>
#include <string.h>
#include <mntent.h>
#include <sys/errno.h>
#include <sys/mnttab.h>

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libzutil.h>

#define	BUFSIZE	(MNT_LINE_MAX + 2)

static __thread char buf[BUFSIZE];

int
_sol_getmntent(FILE *fp, struct mnttab *mgetp)
{
	struct mntent mntbuf;
	struct mntent *ret;

	ret = getmntent_r(fp, &mntbuf, buf, BUFSIZE);

	if (ret != NULL) {
		mgetp->mnt_special = mntbuf.mnt_fsname;
		mgetp->mnt_mountp = mntbuf.mnt_dir;
		mgetp->mnt_fstype = mntbuf.mnt_type;
		mgetp->mnt_mntopts = mntbuf.mnt_opts;
		return (0);
	}

	if (feof(fp))
		return (-1);

	return (MNT_TOOLONG);
}

static int
getextmntent_impl(FILE *fp, struct mnttab *mp, uint64_t *mnt_id, dev_t *dev)
{
	int ret;
	struct stat64 st;

	*mnt_id = 0;
	ret = _sol_getmntent(fp, (struct mnttab *)mp);
	if (ret == 0) {
#ifdef HAVE_STATX_MNT_ID
		struct statx stx;
		if (statx(AT_FDCWD, mp->mnt_mountp,
		    AT_STATX_SYNC_AS_STAT | AT_SYMLINK_NOFOLLOW,
		    STATX_MNT_ID, &stx) == 0 && (stx.stx_mask & STATX_MNT_ID))
			*mnt_id = stx.stx_mnt_id;
#endif
		if (stat64(mp->mnt_mountp, &st) != 0) {
			*dev = 0;
			return (ret);
		}
		*dev = st.st_dev;
	}

	return (ret);
}

int
getextmntent(const char *path, struct mnttab *entry, struct stat64 *statbuf)
{
	struct stat64 st;
	FILE *fp;
	int match;
	boolean_t have_mnt_id = B_FALSE;
	uint64_t target_mnt_id = 0;
	uint64_t entry_mnt_id;
	dev_t dev;
#ifdef HAVE_STATX_MNT_ID
	struct statx stx;
#endif

	if (strlen(path) >= MAXPATHLEN) {
		(void) fprintf(stderr, "invalid object; pathname too long\n");
		return (-1);
	}

	/*
	 * Search for the path in /proc/self/mounts. Rather than looking for the
	 * specific path, which can be fooled by non-standard paths (i.e. ".."
	 * or "//"), we stat() the path and search for the corresponding
	 * (major,minor) device pair.
	 */
	if (stat64(path, statbuf) != 0) {
		(void) fprintf(stderr, "cannot open '%s': %s\n",
		    path, zfs_strerror(errno));
		return (-1);
	}

#ifdef HAVE_STATX_MNT_ID
	/*
	 * Use AT_STATX_SYNC_AS_STAT without AT_SYMLINK_NOFOLLOW so that
	 * symlinks are followed, matching the behavior of stat64() above.
	 * Without this, if path is a symlink crossing a mount boundary,
	 * statx() returns the mnt_id of the symlink's location rather
	 * than the symlink target's mount.
	 */
	if (statx(AT_FDCWD, path, AT_STATX_SYNC_AS_STAT,
	    STATX_MNT_ID, &stx) == 0 && (stx.stx_mask & STATX_MNT_ID)) {
		have_mnt_id = B_TRUE;
		target_mnt_id = stx.stx_mnt_id;
	}
#endif

	if ((fp = fopen(MNTTAB, "re")) == NULL) {
		(void) fprintf(stderr, "cannot open %s\n", MNTTAB);
		return (-1);
	}

	/*
	 * Search for the given (major,minor) pair in the mount table.
	 */

	match = 0;
	while (getextmntent_impl(fp, entry, &entry_mnt_id, &dev) == 0) {
		if (have_mnt_id) {
			match = (entry_mnt_id == target_mnt_id);
		} else {
			match = (dev == statbuf->st_dev);
		}
		if (match)
			break;
	}
	(void) fclose(fp);

	if (!match) {
		(void) fprintf(stderr, "cannot find mountpoint for '%s'\n",
		    path);
		return (-1);
	}

	if (stat64(entry->mnt_mountp, &st) != 0)
		return (-1);

	return (0);
}
