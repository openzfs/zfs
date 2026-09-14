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
#include <sys/types.h>
#include <dirent.h>

#include <sys/stat.h>

#ifdef MNTTAB
#undef MNTTAB
#endif /* MNTTAB */

#include <paths.h>
#include <sys/mount.h>
#define	MNTTAB		_PATH_DEVNULL
#define	MS_NOMNTTAB		0x0
#define	MS_RDONLY		0x1
#define	umount2(p, f)	unmount(p, f)
#define	MNT_LINE_MAX	4096

#define	MNT_TOOLONG	1	/* entry exceeds MNT_LINE_MAX */
#define	MNT_TOOMANY	2	/* too many fields in line */
#define	MNT_TOOFEW	3	/* too few fields in line */

struct mnttab {
	char *mnt_special;
	char *mnt_mountp;
	char *mnt_fstype;
	char *mnt_mntopts;
	uint_t mnt_major;
	uint_t mnt_minor;
	uint32_t mnt_fssubtype;
};

#define	extmnttab mnttab

struct stat64;
struct statfs;

extern DIR *fdopendir(int fd);
extern int openat64(int, const char *, int, ...);

extern int getmntany(FILE *fd, struct mnttab *mgetp, struct mnttab *mrefp);
extern int getmntent(FILE *fp, struct mnttab *mp);
extern char *hasmntopt(struct mnttab *mnt, const char *opt);
extern int getextmntent(const char *path, struct extmnttab *entry,
	struct stat64 *statbuf);

extern void statfs2mnttab(struct statfs *sfs, struct mnttab *mp);

#ifndef AT_SYMLINK_NOFOLLOW
#define	AT_SYMLINK_NOFOLLOW 0x100
#endif

extern int fstatat64(int, const char *, struct stat *, int);

#endif
