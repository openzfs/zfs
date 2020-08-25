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
#include <sys/types.h>

#ifdef MNTTAB
#undef MNTTAB
#endif /* MNTTAB */

#include <paths.h>
#include <sys/mount.h>
#define	MNTTAB		_PATH_DEVZERO
#define	MS_NOMNTTAB		0x0
#define	MS_RDONLY		0x1
#define	umount2(p, f)	unmount(p, f)
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

/*
 * NOTE: fields in extmnttab should match struct mnttab till new fields
 * are encountered, this allows hasmntopt to work properly when its arg is
 * a pointer to an extmnttab struct cast to a mnttab struct pointer.
 */

struct extmnttab {
	char *mnt_special;
	char *mnt_mountp;
	char *mnt_fstype;
	char *mnt_mntopts;
	uint_t mnt_major;
	uint_t mnt_minor;
};

struct stat64;
struct statfs;

extern int getmntany(FILE *fp, struct mnttab *mp, struct mnttab *mpref);
extern int _sol_getmntent(FILE *fp, struct mnttab *mp);
extern int getextmntent(const char *path, struct extmnttab *entry,
    struct stat64 *statbuf);
extern void statfs2mnttab(struct statfs *sfs, struct mnttab *mp);
char *hasmntopt(struct mnttab *mnt, char *opt);
int getmntent(FILE *fp, struct mnttab *mp);

#endif
