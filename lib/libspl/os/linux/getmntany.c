/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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

#define	BUFSIZE	(MNT_LINE_MAX + 2)

__thread char buf[BUFSIZE];

#define	DIFF(xx)	( \
	    (mrefp->xx != NULL) && \
	    (mgetp->xx == NULL || strcmp(mrefp->xx, mgetp->xx) != 0))

int
getmntany(FILE *fp, struct mnttab *mgetp, struct mnttab *mrefp)
{
	int ret;

	while (
	    ((ret = _sol_getmntent(fp, mgetp)) == 0) && (
	    DIFF(mnt_special) || DIFF(mnt_mountp) ||
	    DIFF(mnt_fstype) || DIFF(mnt_mntopts))) { }

	return (ret);
}

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
getextmntent_impl(FILE *fp, struct extmnttab *mp, int len)
{
	int ret;
	struct stat64 st;

	ret = _sol_getmntent(fp, (struct mnttab *)mp);
	if (ret == 0) {
		if (stat64(mp->mnt_mountp, &st) != 0) {
			mp->mnt_major = 0;
			mp->mnt_minor = 0;
			return (ret);
		}
		mp->mnt_major = major(st.st_dev);
		mp->mnt_minor = minor(st.st_dev);
	}

	return (ret);
}

int
getextmntent(const char *path, struct extmnttab *entry, struct stat64 *statbuf)
{
	struct stat64 st;
	FILE *fp;
	int match;

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
		    path, strerror(errno));
		return (-1);
	}


#ifdef HAVE_SETMNTENT
	if ((fp = setmntent(MNTTAB, "r")) == NULL) {
#else
	if ((fp = fopen(MNTTAB, "r")) == NULL) {
#endif
		(void) fprintf(stderr, "cannot open %s\n", MNTTAB);
		return (-1);
	}

	/*
	 * Search for the given (major,minor) pair in the mount table.
	 */

	match = 0;
	while (getextmntent_impl(fp, entry, sizeof (*entry)) == 0) {
		if (makedev(entry->mnt_major, entry->mnt_minor) ==
		    statbuf->st_dev) {
			match = 1;
			break;
		}
	}

	if (!match) {
		(void) fprintf(stderr, "cannot find mountpoint for '%s'\n",
		    path);
		return (-1);
	}

	if (stat64(entry->mnt_mountp, &st) != 0) {
		entry->mnt_major = 0;
		entry->mnt_minor = 0;
		return (-1);
	}

	return (0);
}
