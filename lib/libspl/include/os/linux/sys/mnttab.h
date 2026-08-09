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
/* Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T */
/*  All Rights Reserved  */
/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/* Copyright 2006 Ricardo Correia */

#ifndef _SYS_MNTTAB_H
#define	_SYS_MNTTAB_H

#include <stdio.h>
#include <mntent.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef MNTTAB
#undef MNTTAB
#endif /* MNTTAB */

#define	MNTTAB		"/proc/self/mounts"
#define	MNT_LINE_MAX	4108

#define	MNT_TOOLONG	1	/* entry exceeds MNT_LINE_MAX */
#define	MNT_TOOMANY	2	/* too many fields in line */
#define	MNT_TOOFEW	3	/* too few fields in line */

struct mnttab {
	char *mnt_special;
	char *mnt_mountp;
	char *mnt_fstype;
	char *mnt_mntopts;
};

struct statfs;

extern int _sol_getmntent(FILE *fp, struct mnttab *mp);
extern int getextmntent(const char *path, struct mnttab *mp,
    struct stat64 *statbuf);
static inline char *_sol_hasmntopt(struct mnttab *mnt, const char *opt)
{
	struct mntent mnt_new;

	mnt_new.mnt_opts = mnt->mnt_mntopts;

	return (hasmntopt(&mnt_new, opt));
}

#define	hasmntopt	_sol_hasmntopt
#define	getmntent	_sol_getmntent

#endif
