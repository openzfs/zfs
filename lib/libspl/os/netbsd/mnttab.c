// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2006 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * Copyright (c) 2009 The NetBSD Foundation, Inc.
 * Copyright (c) 2026 OpenZFS
 * All rights reserved.
 */

/*
 * Solaris-compatible hasmntopt() / getmntent() using NetBSD getvfsstat(2).
 *
 * NetBSD splits name lengths: _VFS_NAMELEN is 32 (fstype) and
 * _VFS_MNAMELEN is 1024 (mount paths). Using _VFS_NAMELEN for
 * f_mntonname truncated paths to 31 bytes, so zfs unmount/destroy
 * looked up the wrong mountpoint (ENOENT) and poisoned later cases.
 */

#include <sys/param.h>
#include <sys/statvfs.h>
#include <sys/mntent.h>
#include <sys/mnttab.h>

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _VFS_MNAMELEN
#ifdef VFS_MNAMELEN
#define	_VFS_MNAMELEN	VFS_MNAMELEN
#else
#define	_VFS_MNAMELEN	1024
#endif
#endif

#ifndef VFS_NAMELEN
#ifdef _VFS_NAMELEN
#define	VFS_NAMELEN	_VFS_NAMELEN
#else
#define	VFS_NAMELEN	32
#endif
#endif

static char *
mntopt(char **p)
{
	char *cp = *p;
	char *retstr;

	while (*cp && isspace((unsigned char)*cp))
		cp++;

	retstr = cp;
	while (*cp && *cp != ',')
		cp++;

	if (*cp) {
		*cp = '\0';
		cp++;
	}

	*p = cp;
	return (retstr);
}

char *
hasmntopt(struct mnttab *mnt, const char *opt)
{
	char tmpopts[MNT_LINE_MAX];
	char *f, *opts = tmpopts;

	if (mnt->mnt_mntopts == NULL)
		return (NULL);
	(void) strlcpy(opts, mnt->mnt_mntopts, sizeof (tmpopts));
	f = mntopt(&opts);
	for (; *f; f = mntopt(&opts)) {
		if (strncmp(opt, f, strlen(opt)) == 0)
			return (f - tmpopts + mnt->mnt_mntopts);
	}
	return (NULL);
}

static void
optadd(char *mntopts, size_t size, const char *opt)
{
	if (mntopts[0] != '\0')
		strlcat(mntopts, ",", size);
	strlcat(mntopts, opt, size);
}

static __thread char gfstypename[VFS_NAMELEN];
static __thread char gmntfromname[_VFS_MNAMELEN];
static __thread char gmntonname[_VFS_MNAMELEN];
static __thread char gmntopts[MNTMAXSTR];

void
statvfs2mnttab(struct statvfs *sfs, struct mnttab *mp)
{
	long flags;

	strlcpy(gfstypename, sfs->f_fstypename, sizeof (gfstypename));
	mp->mnt_fstype = gfstypename;

	strlcpy(gmntfromname, sfs->f_mntfromname, sizeof (gmntfromname));
	mp->mnt_special = gmntfromname;

	strlcpy(gmntonname, sfs->f_mntonname, sizeof (gmntonname));
	mp->mnt_mountp = gmntonname;

	flags = sfs->f_flag;
	gmntopts[0] = '\0';
#define	OPTADD(opt)	optadd(gmntopts, sizeof (gmntopts), (opt))
	/*
	 * Emit only flags that are actually set.  Synthesizing the
	 * opposite polarity (atime from !MNT_NOATIME, etc.) made
	 * zfs get treat a default-off property as a temporary
	 * override, which broke zfs_mount_007.
	 */
	if (flags & MNT_RDONLY)
		OPTADD(MNTOPT_RO);
	if (flags & MNT_NOSUID)
		OPTADD(MNTOPT_NOSETUID);
	if (flags & MNT_NOATIME)
		OPTADD(MNTOPT_NOATIME);
	if (flags & MNT_NOEXEC)
		OPTADD(MNTOPT_NOEXEC);
#ifdef MNT_NODEV
	if (flags & MNT_NODEV)
		OPTADD(MNTOPT_NODEVICES);
#endif
#undef	OPTADD
	mp->mnt_mntopts = gmntopts;
}

/* Keep the FreeBSD-named helper for getextmntent. */
void
statfs2mnttab(struct statvfs *sfs, struct mnttab *mp)
{
	statvfs2mnttab(sfs, mp);
}

static pthread_rwlock_t gsfs_lock = PTHREAD_RWLOCK_INITIALIZER;
static struct statvfs *gsfs = NULL;
static int allfs = 0;

static int
statvfs_init(void)
{
	struct statvfs *sfs;
	int error;

	(void) pthread_rwlock_wrlock(&gsfs_lock);

	if (gsfs != NULL) {
		free(gsfs);
		gsfs = NULL;
	}
	allfs = getvfsstat(NULL, 0, ST_NOWAIT);
	if (allfs == -1)
		goto fail;
	gsfs = malloc(sizeof (gsfs[0]) * allfs * 2);
	if (gsfs == NULL)
		goto fail;
	allfs = getvfsstat(gsfs, sizeof (gsfs[0]) * allfs * 2, ST_NOWAIT);
	if (allfs == -1)
		goto fail;
	sfs = realloc(gsfs, allfs * sizeof (gsfs[0]));
	if (sfs != NULL)
		gsfs = sfs;
	(void) pthread_rwlock_unlock(&gsfs_lock);
	return (0);
fail:
	error = errno;
	if (gsfs != NULL)
		free(gsfs);
	gsfs = NULL;
	allfs = 0;
	(void) pthread_rwlock_unlock(&gsfs_lock);
	return (error);
}

int
getmntent(FILE *fp, struct mnttab *mp)
{
	int error, nfs;

	nfs = (int)lseek(fileno(fp), 0, SEEK_CUR);
	if (nfs == -1)
		return (errno);
	(void) pthread_rwlock_rdlock(&gsfs_lock);
	if (nfs == 0 || gsfs == NULL) {
		(void) pthread_rwlock_unlock(&gsfs_lock);
		error = statvfs_init();
		if (error != 0)
			return (error);
		(void) pthread_rwlock_rdlock(&gsfs_lock);
	}
	if (nfs >= allfs) {
		(void) pthread_rwlock_unlock(&gsfs_lock);
		return (-1);
	}
	statvfs2mnttab(&gsfs[nfs], mp);
	(void) pthread_rwlock_unlock(&gsfs_lock);
	if (lseek(fileno(fp), 1, SEEK_CUR) == -1)
		return (errno);
	return (0);
}
