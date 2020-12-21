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
#include <ctype.h> /* for isspace() */
#include <errno.h>
#include <unistd.h>
#include <sys/mnttab.h>
#include <sys/types.h>
#include <sys/vnode.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#endif

#define DIFF(xx) ((mrefp->xx != NULL) && \
		  (mgetp->xx == NULL || strcmp(mrefp->xx, mgetp->xx) != 0))

static struct statfs *gsfs = NULL;
static int allfs = 0;
/*
 * We will also query the extended filesystem capabilities API, to lookup
 * other mount options, for example, XATTR. We can not use the MNTNOUSERXATTR
 * option due to VFS rejecting with EACCESS.
 */

//#include <sys/attr.h>
//typedef struct attrlist attrlist_t;

//struct attrBufS {
//	u_int32_t       length;
//	vol_capabilities_set_t caps;
//} __attribute__((aligned(4), packed));



static int
chdir_block_begin(int newroot_fd)
{
	int cwdfd, error;

	cwdfd = open(".", O_RDONLY /*| O_DIRECTORY*/);
	if (cwdfd == -1)
		return (-1);

//	if (fchdir(newroot_fd) == -1) {
//		error = errno;
//		(void) close(cwdfd);
//		errno = error;
//		return (-1);
//	}
	return (cwdfd);
}

static void
chdir_block_end(int cwdfd)
{
	int error = errno;
//	(void) fchdir(cwdfd);
	(void) close(cwdfd);
	errno = error;
}

int
openat64(int dirfd, const char *path, int flags, ...)
{
	int cwdfd, filefd;

	if ((cwdfd = chdir_block_begin(dirfd)) == -1)
		return (-1);

	if ((flags & O_CREAT) != 0) {
		va_list ap;
		int mode;

		va_start(ap, flags);
		mode = va_arg(ap, int);
		va_end(ap);

		filefd = open(path, flags, mode);
	} else
		filefd = open(path, flags);

	chdir_block_end(cwdfd);
	return (filefd);
}

int
fstatat64(int dirfd, const char *path, struct _stat64 *statbuf, int flag)
{
	int cwdfd, error;

	if ((cwdfd = chdir_block_begin(dirfd)) == -1)
		return (-1);

	//if (flag == AT_SYMLINK_NOFOLLOW)
	//	error = lstat(path, statbuf);
	//else
		error = _stat64(path, statbuf);

	chdir_block_end(cwdfd);
	return (error);
}


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
hasmntopt(struct mnttab *mnt, char *opt)
{
	char tmpopts[256];
	char *f, *opts = tmpopts;

	if (mnt->mnt_mntopts == NULL)
		return (NULL);
	(void) strlcpy(opts, mnt->mnt_mntopts, 256);
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

void
statfs2mnttab(struct statfs *sfs, struct mnttab *mp)
{
	static char mntopts[MNTMAXSTR];
	long flags;

	mntopts[0] = '\0';

	flags = sfs->f_flags;
#define	OPTADD(opt)	optadd(mntopts, sizeof(mntopts), (opt))
	if (flags & MNT_RDONLY)
		OPTADD(MNTOPT_RO);
	else
		OPTADD(MNTOPT_RW);

	if (flags & MNT_UPDATE)
		OPTADD(MNTOPT_REMOUNT);
	if (flags & MNT_NOATIME)
		OPTADD(MNTOPT_NOATIME);
	else
		OPTADD(MNTOPT_ATIME);
#if 0
	{
			struct attrBufS attrBuf;
			attrlist_t      attrList;

			memset(&attrList, 0, sizeof(attrList));
			attrList.bitmapcount = ATTR_BIT_MAP_COUNT;
			attrList.volattr = ATTR_VOL_INFO|ATTR_VOL_CAPABILITIES;

			if (getattrlist(sfs->f_mntonname, &attrList, &attrBuf,
							sizeof(attrBuf), 0) == 0)  {

				if (attrBuf.caps[VOL_CAPABILITIES_INTERFACES] &
					VOL_CAP_INT_EXTENDED_ATTR) {
					OPTADD(MNTOPT_XATTR);
				} else {
					OPTADD(MNTOPT_NOXATTR);
				} // If EXTENDED
			} // if getattrlist
		}
#endif
	if (flags & MNT_NOEXEC)
		OPTADD(MNTOPT_NOEXEC);
	else
		OPTADD(MNTOPT_EXEC);
	if (flags & MNT_NODEV)
		OPTADD(MNTOPT_NODEVICES);
	else
		OPTADD(MNTOPT_DEVICES);
//	if (flags & MNT_DONTBROWSE)
//		OPTADD(MNTOPT_NOBROWSE);
//	else
//		OPTADD(MNTOPT_BROWSE);
//	if (flags & MNT_IGNORE_OWNERSHIP)
//		OPTADD(MNTOPT_NOOWNERS);
//	else
//		OPTADD(MNTOPT_OWNERS);

#undef	OPTADD

	mp->mnt_special = sfs->f_mntfromname;
	mp->mnt_mountp = sfs->f_mntonname;
	mp->mnt_fstype = sfs->f_fstypename;
	mp->mnt_mntopts = mntopts;
	mp->mnt_fssubtype = sfs->f_fssubtype;
}

void DisplayVolumePaths(char *VolumeName, char *out, int len)
{
	DWORD  CharCount = MAX_PATH + 1;
	char *Names = NULL;
	char *NameIdx = NULL;
	BOOL   Success = FALSE;

	for (;;) {
		//
		//  Allocate a buffer to hold the paths.
		Names = (char *) malloc(CharCount);

		if (!Names)	return;

		//
		//  Obtain all of the paths
		//  for this volume.
		Success = GetVolumePathNamesForVolumeName(
			VolumeName, Names, CharCount, &CharCount
		);

		if (Success) break;

		if (GetLastError() != ERROR_MORE_DATA) break;

		//
		//  Try again with the
		//  new suggested size.
		free(Names);
		Names = NULL;
	}

	if (Success) {
		//
		//  Display the various paths.
		for (NameIdx = Names;
			NameIdx[0] != '\0';
			NameIdx += strlen(NameIdx) + 1) {
			//printf("  %s", NameIdx);
			snprintf(out, len, "%s%s ", out, NameIdx);
		}
		//printf("\n");
	}

	if (Names != NULL) {
		free(Names);
		Names = NULL;
	}

	return;
}

typedef struct _MOUNTDEV_NAME {
	USHORT NameLength;
	WCHAR  Name[1];
} MOUNTDEV_NAME, *PMOUNTDEV_NAME;
#define IOCTL_MOUNTDEV_QUERY_DEVICE_NAME 0x004d0008
typedef struct _MOUNTDEV_UNIQUE_ID {
	USHORT  UniqueIdLength;
	UCHAR   UniqueId[1];
} MOUNTDEV_UNIQUE_ID;
typedef MOUNTDEV_UNIQUE_ID *PMOUNTDEV_UNIQUE_ID;
#define IOCTL_MOUNTDEV_QUERY_UNIQUE_ID 0x4d0000

int getfsstat(struct statfs *buf, int bufsize, int flags)
{
	char name[256];
	HANDLE vh;
	int count = 0;
	MOUNTDEV_UNIQUE_ID *UID = NULL;

	// If buf is NULL, return number of entries
	vh = FindFirstVolume(name, sizeof(name));
	if (vh == INVALID_HANDLE_VALUE) return -1;

	do {
		char *s = name;

		// Still room in out buffer?
		if (buf && (bufsize < sizeof(struct statfs))) break;


		// We must skip the "\\?\" start
		if (s[0] == '\\' &&
			s[1] == '\\' &&
			s[2] == '?' &&
			s[3] == '\\')
			s = &s[4];
		// We must eat the final "\\"
		int trail = strlen(name) - 1;
		if (name[trail] == '\\')
			name[trail] = 0;

		// Query DOS
		char DeviceName[256], driveletter[256] = "";
		int CharCount;
		CharCount = QueryDosDevice(s, DeviceName, sizeof(DeviceName));

		// Restore trailing "\\"
		if (name[trail] == 0)
			name[trail] = '\\';

		//printf("%s: volume '%s' device '%s'\n", __func__, name, DeviceName);
		DisplayVolumePaths(name, driveletter, sizeof(driveletter));

		// Open DeviceName, and query it for dataset name
		HANDLE h;

		name[2] = '.'; // "\\?\" -> "\\.\" 

		// We open the devices returned; like "'\\.\Volume{0b1bb601-af0b-32e8-a1d2-54c167af6277}'" and
		// query its Unique ID, where we return the dataset name. "BOOM"

		h = CreateFile(name, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
//		printf("Open '%s': %d : getlast %d\n", name, h, GetLastError());
	//	fflush(stdout);
		if (h != INVALID_HANDLE_VALUE) {
			char *dataset;
			char cheat[1024];
			UID = cheat;
			DWORD Size;
			BOOL gotname = FALSE;

			gotname = DeviceIoControl(h, IOCTL_MOUNTDEV_QUERY_UNIQUE_ID, NULL, 0, UID, sizeof(cheat) - 1, &Size, NULL);
			//printf("deviocon %d: namelen %d\n", status, UID->UniqueIdLength);
			if (gotname) {
				UID->UniqueId[UID->UniqueIdLength] = 0; // Kernel doesn't null terminate
				//printf("'%s' ==> '%s'\n", name, UID->UniqueId);
			} else
				UID = NULL;
			CloseHandle(h);
		}

		// We found a mount here, add it.
		if (buf) {
			memset(buf, 0, sizeof(*buf));
			if (UID) {
				// Look up mountpoint
				strlcpy(buf->f_mntfromname, UID->UniqueId, sizeof(buf->f_mntfromname));
				strlcpy(buf->f_fstypename, MNTTYPE_ZFS, sizeof(buf->f_fstypename));
				if (strlen(driveletter) > 2)
					strlcpy(buf->f_mntonname, driveletter, sizeof(buf->f_mntonname)); // FIXME, should be mountpoint!
				else
					strlcpy(buf->f_mntonname, UID->UniqueId, sizeof(buf->f_mntonname)); // FIXME, should be mountpoint!
			} else {
				strlcpy(buf->f_mntfromname, DeviceName, sizeof(buf->f_mntfromname));
				strlcpy(buf->f_fstypename, "UKN", sizeof(buf->f_fstypename));
				strlcpy(buf->f_mntonname, name, sizeof(buf->f_mntonname));
			}
			UID = NULL;

			/*
			printf("  entry '%s' '%s' '%s'\n",
				buf->f_mntfromname,
				buf->f_mntonname,
				name);
			*/
			buf++; // Go to next struct.
			bufsize -= sizeof(*buf);
		}

		count++;

	} while (FindNextVolume(vh, name, sizeof(name)) != 0);
	FindVolumeClose(vh);
	//printf("%s: returning %d structures\n", __func__, count);
	return count;
}



static int
statfs_init(void)
{
	struct statfs *sfs;
	int error;

	if (gsfs != NULL) {
		free(gsfs);
		gsfs = NULL;
	}
	allfs = getfsstat(NULL, 0, MNT_NOWAIT);
	if (allfs == -1)
		goto fail;
	gsfs = malloc(sizeof(gsfs[0]) * allfs * 2);
	if (gsfs == NULL)
		goto fail;
	allfs = getfsstat(gsfs, (long)(sizeof(gsfs[0]) * allfs * 2),
		MNT_NOWAIT);
	if (allfs == -1)
		goto fail;
	sfs = realloc(gsfs, allfs * sizeof(gsfs[0]));
	if (sfs != NULL)
		gsfs = sfs;
	return (0);
fail:
	error = errno;
	if (gsfs != NULL)
		free(gsfs);
	gsfs = NULL;
	allfs = 0;
	return (error);
}

int
getmntany(FILE *fd, struct mnttab *mgetp, struct mnttab *mrefp)
{
	int i, error;

	error = statfs_init();
	if (error != 0)
		return (error);

	for (i = 0; i < allfs; i++) {
		statfs2mnttab(&gsfs[i], mgetp);
		if (mrefp->mnt_special != NULL && mgetp->mnt_special != NULL &&
		    strcmp(mrefp->mnt_special, mgetp->mnt_special) != 0) {
			continue;
		}
		if (mrefp->mnt_mountp != NULL && mgetp->mnt_mountp != NULL &&
		    strcmp(mrefp->mnt_mountp, mgetp->mnt_mountp) != 0) {
			continue;
		}
		if (mrefp->mnt_fstype != NULL && mgetp->mnt_fstype != NULL &&
		    strcmp(mrefp->mnt_fstype, mgetp->mnt_fstype) != 0) {
			continue;
		}
		return (0);
	}
	return (-1);
}

int
getmntent(FILE *fp, struct mnttab *mp)
{
	static int index = -1;
	int error = 0;

	if (index < 0) {
		error = statfs_init();
	}

	if (error != 0)
		return (error);

	index++;

	// If we have finished "reading" the mnttab, reset it to
	// start from the beginning, and return EOF.
	if (index >= allfs) {
		index = -1;
		return -1;
	}

	statfs2mnttab(&gsfs[index], mp);
	return (0);
}

int
getextmntent(const char *path, struct extmnttab *entry, struct stat64 *statbuf)
{
	struct statfs sfs;

	if (strlen(path) >= MAXPATHLEN) {
		(void) fprintf(stderr, "invalid object; pathname too long\n");
		return (-1);
	}

	if (stat64(path, statbuf) != 0) {
		(void) fprintf(stderr, "cannot open '%s': %s\n",
		    path, strerror(errno));
		return (-1);
	}

	if (statfs(path, &sfs) != 0) {
	(void) fprintf(stderr, "%s: %s\n", path,
		    strerror(errno));
		return (-1);
	}
	statfs2mnttab(&sfs, (struct mnttab *)entry);
	return (0);
}

FILE *
setmntent(const char *filename, const char *type)
{
	FILE *ret;

	if (tmpfile_s(&ret) == 0)
		return (ret);
	return (NULL);
}

void
endmntent(FILE *fd)
{
    fclose(fd);
}
