/*
 * Copyright (c) 2006 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * This file implements Solaris compatible getmntany() and hasmntopt()
 * functions.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/mntent.h>
#include <sys/mnttab.h>

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *
mntopt(char **p)
{
	char *cp = *p;
	char *retstr;

	while (*cp && isspace(*cp))
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
	(void) strcpy(opts, mnt->mnt_mntopts);
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

static __thread char gfstypename[MFSNAMELEN];
static __thread char gmntfromname[MNAMELEN];
static __thread char gmntonname[MNAMELEN];
static __thread char gmntopts[MNTMAXSTR];

void
statfs2mnttab(struct statfs *sfs, struct mnttab *mp)
{
	long flags;

	strlcpy(gfstypename, sfs->f_fstypename, sizeof (gfstypename));
	mp->mnt_fstype = gfstypename;

	strlcpy(gmntfromname, sfs->f_mntfromname, sizeof (gmntfromname));
	mp->mnt_special = gmntfromname;

	strlcpy(gmntonname, sfs->f_mntonname, sizeof (gmntonname));
	mp->mnt_mountp = gmntonname;

	flags = sfs->f_flags;
	gmntopts[0] = '\0';
#define	OPTADD(opt)	optadd(gmntopts, sizeof (gmntopts), (opt))
	if (flags & MNT_RDONLY)
		OPTADD(MNTOPT_RO);
	else
		OPTADD(MNTOPT_RW);
	if (flags & MNT_NOSUID)
		OPTADD(MNTOPT_NOSETUID);
	else
		OPTADD(MNTOPT_SETUID);
	if (flags & MNT_UPDATE)
		OPTADD(MNTOPT_REMOUNT);
	if (flags & MNT_NOATIME)
		OPTADD(MNTOPT_NOATIME);
	else
		OPTADD(MNTOPT_ATIME);
	OPTADD(MNTOPT_NOXATTR);
	if (flags & MNT_NOEXEC)
		OPTADD(MNTOPT_NOEXEC);
	else
		OPTADD(MNTOPT_EXEC);
#undef	OPTADD
	mp->mnt_mntopts = gmntopts;
}

static pthread_rwlock_t gsfs_lock = PTHREAD_RWLOCK_INITIALIZER;
static struct statfs *gsfs = NULL;
static int allfs = 0;

static int
statfs_init(void)
{
	struct statfs *sfs;
	int error;

	(void) pthread_rwlock_wrlock(&gsfs_lock);

	if (gsfs != NULL) {
		free(gsfs);
		gsfs = NULL;
	}
	allfs = getfsstat(NULL, 0, MNT_NOWAIT);
	if (allfs == -1)
		goto fail;
	gsfs = malloc(sizeof (gsfs[0]) * allfs * 2);
	if (gsfs == NULL)
		goto fail;
	allfs = getfsstat(gsfs, (long)(sizeof (gsfs[0]) * allfs * 2),
	    MNT_NOWAIT);
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
getmntany(FILE *fd __unused, struct mnttab *mgetp, struct mnttab *mrefp)
{
	int i, error;

	error = statfs_init();
	if (error != 0)
		return (error);

	(void) pthread_rwlock_rdlock(&gsfs_lock);

	for (i = 0; i < allfs; i++) {
		if (mrefp->mnt_special != NULL &&
		    strcmp(mrefp->mnt_special, gsfs[i].f_mntfromname) != 0) {
			continue;
		}
		if (mrefp->mnt_mountp != NULL &&
		    strcmp(mrefp->mnt_mountp, gsfs[i].f_mntonname) != 0) {
			continue;
		}
		if (mrefp->mnt_fstype != NULL &&
		    strcmp(mrefp->mnt_fstype, gsfs[i].f_fstypename) != 0) {
			continue;
		}
		statfs2mnttab(&gsfs[i], mgetp);
		(void) pthread_rwlock_unlock(&gsfs_lock);
		return (0);
	}
	(void) pthread_rwlock_unlock(&gsfs_lock);
	return (-1);
}

int
getmntent(FILE *fp, struct mnttab *mp)
{
	int error, nfs;

	nfs = (int)lseek(fileno(fp), 0, SEEK_CUR);
	if (nfs == -1)
		return (errno);
	/* If nfs is 0, we want to refresh out cache. */
	if (nfs == 0 || gsfs == NULL) {
		error = statfs_init();
		if (error != 0)
			return (error);
	}
	(void) pthread_rwlock_rdlock(&gsfs_lock);
	if (nfs >= allfs) {
		(void) pthread_rwlock_unlock(&gsfs_lock);
		return (-1);
	}
	statfs2mnttab(&gsfs[nfs], mp);
	(void) pthread_rwlock_unlock(&gsfs_lock);
	if (lseek(fileno(fp), 1, SEEK_CUR) == -1)
		return (errno);
	return (0);
}
