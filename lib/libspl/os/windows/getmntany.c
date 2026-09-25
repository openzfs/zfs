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
 * Copyright(c) 2017 Jorgen Lundman <lundman@lundman.net>
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

#include <sys/fs/zfs.h>

#include <windows.h>
#include <winternl.h>
#include <ntstatus.h>

typedef struct
{
    int len;
    uint32_t flags; /* live MNT_RDONLY/MNT_NOATIME/MNT_NOEXEC/MNT_NODEV */
    WCHAR buffer[1]; // make this dynamic?
} fsctl_zfs_volume_mountpoint_t;


#define	DIFF(xx) ((mrefp->xx != NULL) && \
	    (mgetp->xx == NULL || strcmp(mrefp->xx, mgetp->xx) != 0))

typedef struct _MOUNTDEV_NAME
{
    USHORT NameLength;
    WCHAR  Name[1];
} MOUNTDEV_NAME, * PMOUNTDEV_NAME;
#define	IOCTL_MOUNTDEV_QUERY_DEVICE_NAME 0x004d0008
typedef struct _MOUNTDEV_UNIQUE_ID
{
    USHORT  UniqueIdLength;
    UCHAR   UniqueId[1];
} MOUNTDEV_UNIQUE_ID;
typedef MOUNTDEV_UNIQUE_ID* PMOUNTDEV_UNIQUE_ID;
#define	IOCTL_MOUNTDEV_QUERY_UNIQUE_ID 0x4d0000

// To handle state between calls to getmntent(),
// we store it in the fake FILE* cookie.
struct mntent_internal {
	int index;
	int allfs;
	struct statfs *gsfs;
	char *fstypename;
	char *mntfromname;
	char *mntonname;
	char *mntopts;
};
typedef struct mntent_internal mntent_internal_t;

/*
 * We will also query the extended filesystem capabilities API, to lookup
 * other mount options, for example, XATTR. We can not use the MNTNOUSERXATTR
 * option due to VFS rejecting with EACCESS.
 */

static int
chdir_block_begin(int newroot_fd)
{
	int cwdfd, error;

	cwdfd = open(".", O_RDONLY /* | O_DIRECTORY */);
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

	// if (flag == AT_SYMLINK_NOFOLLOW)
	//	error = lstat(path, statbuf);
	// else
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
#define	OPTADD(opt)	optadd(mntopts, sizeof (mntopts), (opt))
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
			attrlist_t attrList;

			memset(&attrList, 0, sizeof (attrList));
			attrList.bitmapcount = ATTR_BIT_MAP_COUNT;
			attrList.volattr = ATTR_VOL_INFO|ATTR_VOL_CAPABILITIES;

			if (getattrlist(sfs->f_mntonname, &attrList, &attrBuf,
			    sizeof (attrBuf), 0) == 0)  {

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

void
DisplayVolumePaths(char *VolumeName, char *out, int len)
{
	DWORD CharCount = MAX_PATH + 1;
	char *Names = NULL, *NameIdx = NULL;
	BOOL Success = FALSE;

	for (;;) {
		Names = (char *)malloc(CharCount);
		if (!Names)
			return;
		Success = GetVolumePathNamesForVolumeName(VolumeName, Names,
		    CharCount, &CharCount);
		if (Success || GetLastError() != ERROR_MORE_DATA)
			break;
		free(Names);
		Names = NULL;
	}

	if (Success) {
		// `out` is "" on entry, but be safe
		size_t used = strnlen(out, len);
		for (NameIdx = Names; NameIdx[0] != '\0';
		    NameIdx += strlen(NameIdx) + 1) {
			if (used + 1 < (size_t)len) { // leave space for NUL
				size_t avail = (size_t)len - 1 - used;
				size_t copy = strnlen(NameIdx, avail);
				memcpy(out + used, NameIdx, copy);
				used += copy;
				out[used] = '\0';
			} else {
				break; // truncated, but safe
			}
		}
	}

	if (Names) {
		free(Names);
		Names = NULL;
	}
}

static void
backslash2slash(char *s)
{
	while (*s != 0) {
		if (*s == '\\')
			*s = '/';
		s++;
	}
}

int
getfsstat(struct statfs *buf, int bufsize, int flags)
{
	char name[256];
	char saved;
	HANDLE vh = INVALID_HANDLE_VALUE;
	int count = 0;
	MOUNTDEV_UNIQUE_ID *UID = NULL;
	fsctl_zfs_volume_mountpoint_t *fzvm = NULL;
	char *dataset;
	DWORD Size;
	BOOL gotname = FALSE;
	HANDLE h;
	DWORD cap;
	DWORD outSize = 0;

	// If buf is NULL, return number of entries
	vh = FindFirstVolume(name, sizeof (name));
	if (vh == INVALID_HANDLE_VALUE)
		return (-1);

	do {
		char *s = name;

		// Still room in out buffer?
		if (buf && (bufsize < sizeof (struct statfs)))
			break;

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
		CharCount = QueryDosDevice(s, DeviceName, sizeof (DeviceName));

		// Restore trailing "\\"
		if (name[trail] == 0)
			name[trail] = '\\';

		DisplayVolumePaths(name, driveletter, sizeof (driveletter));

		saved = name[2];
		name[2] = '.'; // "\\?\" -> "\\.\"

		// We open the devices returned; like
		// "'\\.\Volume{0b1bb601-af0b-32e8-a1d2-54c167af6277}'" and
		// query its Unique ID, where we return the dataset name. "BOOM"
		// fprintf(stderr, "Opening %s to see if it is ZFS:\r\n", name);
		if (name[trail] == '\\')
			name[trail] = 0;
		h = CreateFile(name, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
		    NULL, OPEN_EXISTING, 0, NULL);

		name[2] = saved;

		if (name[trail] == 0)
			name[trail] = '\\';

		if (h != INVALID_HANDLE_VALUE) {

			cap = 1024;
			UID = malloc(cap);
			if (UID) {

				outSize = 0;

				gotname = DeviceIoControl(h,
				    IOCTL_MOUNTDEV_QUERY_UNIQUE_ID,
				    NULL, 0, UID, cap, &outSize, NULL);

				if (gotname) {
					size_t base = offsetof(
					    MOUNTDEV_UNIQUE_ID, UniqueId);

					if (outSize >= base &&
					    UID->UniqueIdLength < cap - base &&
					    base + UID->UniqueIdLength + 1 <=
					    cap) {
						UID->UniqueId[
						    UID->UniqueIdLength] = '\0';
					} else {
						free(UID);
						UID = NULL;
					}
				} else {
					free(UID);
					UID = NULL;
				}
			} // if UID


			IO_STATUS_BLOCK iosb;
			long Status;

			cap = 1024;
			outSize = 0;
			fzvm = malloc(cap);
			if (fzvm) {

				if (DeviceIoControl(h, ZFS_IOC_GET_MOUNT, NULL,
				    0, fzvm, cap, &outSize, NULL)) {
					size_t off = offsetof(
					    fsctl_zfs_volume_mountpoint_t,
					    buffer);
					if (outSize >= off &&
					    fzvm->len <= cap - off &&
					    (fzvm->len % sizeof (WCHAR) == 0)) {
						fzvm->buffer[
						    fzvm->len / sizeof (WCHAR)
						    ] = L'\0';
					} else {
						free(fzvm);
						fzvm = NULL;
					}

				} else {
					free(fzvm);
					fzvm = NULL;
				}
			}

			CloseHandle(h);
		}

		// We found a mount here, add it.
		if (buf) {
			memset(buf, 0, sizeof (*buf));
			if (UID && fzvm) {
				buf->f_flags = fzvm->flags;
				// Look up mountpoint
				strlcpy(buf->f_mntfromname, UID->UniqueId,
				    sizeof (buf->f_mntfromname));
				strlcpy(buf->f_fstypename, MNTTYPE_ZFS,
				    sizeof (buf->f_fstypename));
				// If we have a driveletter, use it
				// otherwise, lookup mountpoint, and if
				// no mountpoint, the device exists, but
				// is not mounted.
				if (strlen(driveletter) > 2) {
					strlcpy(buf->f_mntonname, driveletter,
					    sizeof (buf->f_mntonname));
				} else {
					if (fzvm) {
						snprintf(buf->f_mntonname,
						    sizeof (buf->f_mntonname),
						    "%ws",
						    &fzvm->buffer[4]);
					} else {
						strlcpy(buf->f_mntonname,
						    UID->UniqueId,
						    sizeof (buf->f_mntonname));
					}
				}

			} else {
				// Not ZFS - do we care?
				strlcpy(buf->f_mntfromname, DeviceName,
				    sizeof (buf->f_mntfromname));
				strlcpy(buf->f_fstypename, "UKN",
				    sizeof (buf->f_fstypename));
				strlcpy(buf->f_mntonname, name,
				    sizeof (buf->f_mntonname));
			}

			backslash2slash(buf->f_mntfromname);
			backslash2slash(buf->f_mntonname);

			// If it is mounted, add node.
			if (buf->f_mntonname[0] != 0) {
				buf++; // Go to next struct.
				bufsize -= sizeof (*buf);
			}
		}

		count++;

		if (fzvm)
			free(fzvm);
		fzvm = NULL;

		if (UID)
			free(UID);
		UID = NULL;

	} while (FindNextVolume(vh, name, sizeof (name)) != 0);

	FindVolumeClose(vh);
	vh = INVALID_HANDLE_VALUE;
	return (count);
}



static int
statfs_init(struct statfs **gsfs,
    int *allfs)
{
	if (gsfs == NULL || allfs == NULL)
		return (EINVAL);

	struct statfs *sfs;
	int error;

	if (*gsfs != NULL) {
		free(*gsfs);
		*gsfs = NULL;
	}

	*allfs = 0;

	*allfs = getfsstat(NULL, 0, MNT_NOWAIT);
	if (*allfs == -1)
		goto fail;

	int sz = sizeof (struct statfs) * *allfs * 2;

	*gsfs = malloc(sz);
	if (*gsfs == NULL)
		goto fail;
	*allfs = getfsstat(*gsfs, (long)(sizeof (struct statfs) * *allfs * 2),
	    MNT_NOWAIT);
	if (*allfs == -1)
		goto fail;

	sz = *allfs * sizeof (struct statfs);
	sfs = realloc(*gsfs, *allfs * sizeof (struct statfs));

	if (sfs != NULL)
		*gsfs = sfs;
	return (0);
fail:

	error = errno;
	if (*gsfs != NULL)
		free(*gsfs);
	*gsfs = NULL;
	*allfs = 0;
	return (error);
}

int
getmntany(FILE *fd, struct mnttab *mgetp, struct mnttab *mrefp)
{
	int i, error;
	struct statfs *gsfs = NULL;
	int allfs = 0;
	fakeFILE *fFILE = (fakeFILE *)fd;
	mntent_internal_t *internal = NULL;

	if (wosix_is_fake_file(fd))
		internal = fFILE->cookie;

	if (internal) {
		if (internal->fstypename)
			free(internal->fstypename);
		internal->fstypename = NULL;
		if (internal->mntfromname)
			free(internal->mntfromname);
		internal->mntfromname = NULL;
		if (internal->mntonname)
			free(internal->mntonname);
		internal->mntonname = NULL;
		if (internal->mntopts)
			free(internal->mntopts);
		internal->mntopts = NULL;
	}

	error = statfs_init(&gsfs, &allfs);
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

		if (internal) {
			internal->fstypename = strdup(mgetp->mnt_fstype);
			internal->mntfromname = strdup(mgetp->mnt_special);
			internal->mntonname = strdup(mgetp->mnt_mountp);
			internal->mntopts = strdup(mgetp->mnt_mntopts);
			mgetp->mnt_fstype = internal->fstypename;
			mgetp->mnt_special = internal->mntfromname;
			mgetp->mnt_mountp = internal->mntonname;
			mgetp->mnt_mntopts = internal->mntopts;
		}
		free(gsfs);
		return (0);
	}

	if (gsfs)
		free(gsfs);
	return (-1);
}

int
getmntent(FILE *fp, struct mnttab *mp)
{
	int error = 0;
	fakeFILE *fFILE = (fakeFILE *)fp;
	mntent_internal_t *internal;

	if (!wosix_is_fake_file(fp))
		return (EINVAL);

	internal = fFILE->cookie;

	if (internal->index < 0) {
		error = statfs_init(&internal->gsfs, &internal->allfs);
	}

	if (error != 0)
		return (error);

	internal->index++;

	// If we have finished "reading" the mnttab, reset it to
	// start from the beginning, and return EOF.
	if (internal->index >= internal->allfs) {
		internal->index = -1;
		if (internal->gsfs)
			free(internal->gsfs);
		internal->gsfs = NULL;
		internal->allfs = 0;
		return (-1);
	}

	statfs2mnttab(&internal->gsfs[internal->index], mp);
	return (0);
}

// Buffers to hold the strings that getextmntent() will return.
// The OpenZFS version of getextmntent() is a bit odd.
static __thread char gfstypename[MFSTYPENAMELEN] = { 0 };
static __thread char gmntfromname[MNAMELEN] = { 0 };
static __thread char gmntonname[MNAMELEN] = { 0 };
static __thread char gmntopts[MNTMAXSTR] = { 0 };

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

	strlcpy(gfstypename, sfs.f_fstypename, sizeof (gfstypename));
	entry->mnt_fstype = gfstypename;
	strlcpy(gmntfromname, sfs.f_mntfromname, sizeof (gmntfromname));
	entry->mnt_special = gmntfromname;
	strlcpy(gmntonname, sfs.f_mntonname, sizeof (gmntonname));
	entry->mnt_special = gmntonname;
	strlcpy(gmntopts, "", sizeof (gmntopts));
	entry->mnt_mntopts = gmntopts;

	return (0);
}

// Solaris API says to call endmntent() to close the FILE*,
// but Linux tends to fclose() it directly.
// Either way, fclose() is called on the fake FILE*, so
// the funopen() closefn() is called below.
void
endmntent(FILE *fd)
{
	fclose(fd);
}

// The funopen() closefn() callback.
static int
endmntent_impl(void *cookie)
{
	mntent_internal_t *internal = (mntent_internal_t *)cookie;

	if (internal)
		free(internal);
}

// If they use Solaris API, they call this to open the mnttab, but
// Linux tends to fopen() directly. Either way, we end up here from
// posix.c's wosix_fopen(). So the FILE* returned is fake style.
FILE *
setmntent(const char *filename, const char *type)
{
	FILE *ret;
	mntent_internal_t *internal;

	internal = malloc(sizeof (mntent_internal_t));
	if (!internal)
		return (NULL);

	internal->index = -1;
	internal->allfs = 0;
	internal->gsfs = NULL;
	internal->fstypename = NULL;
	internal->mntfromname = NULL;
	internal->mntonname = NULL;
	internal->mntopts = NULL;

	ret = wosix_funopen(internal, NULL, NULL, NULL,
	    endmntent_impl);

	return (ret);
}
