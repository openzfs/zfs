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

#ifndef _SYS_MNTTAB_H
#define	_SYS_MNTTAB_H

#include <stdio.h>
#ifndef MNTTAB_NO_DIRENT_H
#include <dirent.h>
#else
typedef void *DIR;
#endif
#include <mntent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <fcntl.h>

#ifdef MNTTAB
#undef MNTTAB
#endif /* MNTTAB */

/*
 * mnttab file is updated by kernel to show current mounts on
 * other platforms, there is no such file in Windows. We call
 * xxx() instead, but build a "mnttab" list to be
 * compatible. But since the existance of the MNTTAB is required
 * (and fails silently) the "fd" work has been removed.
 */

#ifdef MNTTAB
#undef MNTTAB
#endif /* MNTTAB */
#define	MNTTAB		"nul"

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

extern DIR *fdopendir(int fd);
extern int openat64(int, const char *, int, ...);

// From FreeBSD
extern int getmntany(FILE *fd, struct mnttab *mgetp, struct mnttab *mrefp);
extern int getmntent(FILE *fp, struct mnttab *mp);
extern char *hasmntopt(struct mnttab *mnt, char *opt);
FILE *setmntent(const char *filename, const char *type);

extern void statfs2mnttab(struct statfs *sfs, struct mnttab *mp);

#ifndef AT_SYMLINK_NOFOLLOW
#define	AT_SYMLINK_NOFOLLOW 0x100
#endif

extern int fstatat64(int, const char *, struct _stat64 *, int);
extern int getextmntent(const char *, struct extmnttab *, struct stat64 *);

#endif
