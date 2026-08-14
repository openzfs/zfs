// SPDX-License-Identifier: BSD-2-Clause
/*
 * NetBSD zmount: mount(2) with zfs_args, not FreeBSD nmount(2).
 */
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/mntent.h>
#include <sys/statvfs.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <libzfs.h>
#include "../../libzfs_impl.h"

/*
 * Token match, not a substring.  "remount" must not fire on a value
 * that merely contains that text.
 */
static boolean_t
opt_has_token(const char *opts, const char *tok)
{
	const char *p;
	size_t n;

	if (opts == NULL || tok == NULL || tok[0] == '\0')
		return (B_FALSE);
	n = strlen(tok);
	for (p = opts; *p != '\0'; ) {
		const char *comma;
		size_t len;

		while (*p == ',')
			p++;
		if (*p == '\0')
			break;
		comma = strchr(p, ',');
		len = (comma != NULL) ? (size_t)(comma - p) : strlen(p);
		if (len == n && strncmp(p, tok, n) == 0)
			return (B_TRUE);
		if (len > n && p[n] == '=' && strncmp(p, tok, n) == 0)
			return (B_TRUE);
		p += len;
	}
	return (B_FALSE);
}

/*
 * Last explicit ro/rw token wins.  NetBSD mount(8) for
 * "mount -ur" passes "-o rw -o ro -o update", and "mount -uw"
 * passes "-o ro -o rw -o update".  Treating any "ro" as sticky
 * leaves a later "rw" unable to clear MNT_RDONLY.
 */
static boolean_t
opt_ro_effective(const char *opts, boolean_t def_ro)
{
	const char *p;
	boolean_t ro = def_ro;

	if (opts == NULL)
		return (ro);
	for (p = opts; *p != '\0'; ) {
		const char *comma;
		size_t len;

		while (*p == ',')
			p++;
		if (*p == '\0')
			break;
		comma = strchr(p, ',');
		len = (comma != NULL) ? (size_t)(comma - p) : strlen(p);
		if (len == strlen(MNTOPT_RO) &&
		    strncmp(p, MNTOPT_RO, len) == 0)
			ro = B_TRUE;
		else if (len == strlen(MNTOPT_RW) &&
		    strncmp(p, MNTOPT_RW, len) == 0)
			ro = B_FALSE;
		p += len;
	}
	return (ro);
}

int
do_mount(zfs_handle_t *zhp, const char *mntpt, const char *opts, int flags)
{
	zfs_args_t za;
	int mntflags = 0;
	const char *spec = zfs_get_name(zhp);

	assert(spec != NULL);
	assert(mntpt != NULL);

	memset(&za, 0, sizeof (za));
	strlcpy(za.fspec, spec, sizeof (za.fspec));
	strlcpy(za.mntonname, mntpt, sizeof (za.mntonname));
	if (opts != NULL) {
		strlcpy(za.optptr, opts, sizeof (za.optptr));
		za.optlen = (int)strlen(za.optptr);
		/*
		 * mount(2) copies mntflags onto mnt_flag.  Put the
		 * temporary -o bits there too so they survive if the
		 * VFS layer overwrites what zfs_apply_mntopts set.
		 * ro/rw is applied below so the last token wins.
		 */
		if (opt_has_token(opts, MNTOPT_NOATIME))
			mntflags |= MNT_NOATIME;
		if (opt_has_token(opts, MNTOPT_NOEXEC))
			mntflags |= MNT_NOEXEC;
		if (opt_has_token(opts, MNTOPT_NOSETUID) ||
		    opt_has_token(opts, "nosuid"))
			mntflags |= MNT_NOSUID;
		if (opt_has_token(opts, MNTOPT_NODEVICES) ||
		    opt_has_token(opts, "nodev") ||
		    opt_has_token(opts, "nodevices"))
			mntflags |= MNT_NODEV;
	}
	/*
	 * NetBSD mount(8) does not pass -u to mount_zfs.  It passes
	 * -o update.  Without MNT_UPDATE this is a second mount and
	 * mount(2) returns EBUSY (zfs_mount_remount).
	 */
	if ((flags & MS_REMOUNT) != 0 ||
	    (opts != NULL && (opt_has_token(opts, MNTOPT_REMOUNT) ||
	    opt_has_token(opts, "update"))))
		mntflags |= MNT_UPDATE;
	if (opt_ro_effective(opts, (flags & MS_RDONLY) != 0))
		mntflags |= MNT_RDONLY;
	za.mflag = mntflags;

	if (mount(MNTTYPE_ZFS, mntpt, mntflags, &za, sizeof (za)) < 0)
		return (errno);
	return (0);
}

/*
 * Snapshot mounts live under <mountpoint>/.zfs/snapshot/<name>.
 * zfs_umount cannot dounmount them: the parent already holds the
 * global vfs_suspend_lock ("locking against myself"), so the kernel
 * returns EBUSY instead.  Unmount them by path first, while that
 * path still namei's.  Deepest path first.  A failure (cwd inside
 * the snapshot, zfs_unmount_009_pos) leaves the parent mounted.
 * A plain umount(8) does not come through here and still gets EBUSY.
 * There is no idle-snapshot expiry task.
 */
static int
zfs_unmount_dot_snapshots(const char *fsname, const char *mntpt, int uflags)
{
	struct statvfs *sfs;
	char prefix[MAXPATHLEN];
	char **paths;
	int n, i, j, npath, error;
	size_t nlen, plen;

	if (fsname == NULL || mntpt == NULL || mntpt[0] == '\0')
		return (0);
	nlen = strlen(fsname);
	if (strcmp(mntpt, "/") == 0)
		(void) strlcpy(prefix, "/.zfs/snapshot/", sizeof (prefix));
	else if (snprintf(prefix, sizeof (prefix), "%s/.zfs/snapshot/",
	    mntpt) >= (int)sizeof (prefix))
		return (0);
	plen = strlen(prefix);

	n = getvfsstat(NULL, 0, ST_NOWAIT);
	if (n <= 0)
		return (0);
	sfs = calloc((size_t)n, sizeof (sfs[0]));
	if (sfs == NULL)
		return (ENOMEM);
	n = getvfsstat(sfs, n * sizeof (sfs[0]), ST_NOWAIT);
	if (n <= 0) {
		free(sfs);
		return (0);
	}
	paths = calloc((size_t)n, sizeof (*paths));
	if (paths == NULL) {
		free(sfs);
		return (ENOMEM);
	}
	npath = 0;
	for (i = 0; i < n; i++) {
		if (strcmp(sfs[i].f_fstypename, MNTTYPE_ZFS) != 0)
			continue;
		if (strncmp(sfs[i].f_mntfromname, fsname, nlen) != 0 ||
		    sfs[i].f_mntfromname[nlen] != '@')
			continue;
		if (strncmp(sfs[i].f_mntonname, prefix, plen) != 0)
			continue;
		paths[npath++] = sfs[i].f_mntonname;
	}
	for (i = 0; i < npath; i++) {
		for (j = i + 1; j < npath; j++) {
			if (strlen(paths[j]) > strlen(paths[i])) {
				char *tmp = paths[i];

				paths[i] = paths[j];
				paths[j] = tmp;
			}
		}
	}
	error = 0;
	for (i = 0; i < npath; i++) {
		if (unmount(paths[i], uflags) != 0) {
			error = errno;
			break;
		}
	}
	free(paths);
	free(sfs);
	return (error);
}

int
do_unmount(zfs_handle_t *zhp, const char *mntpt, int flags)
{
	int uflags = 0;
	int error;

	/*
	 * Same as Linux do_unmount(): the unmount system call only.
	 * No private ioctl, and ZFS_IOC_DESTROY does not dounmount.
	 *
	 * Two NetBSD kernel gaps remain, and both need kernel changes
	 * rather than another ioctl:
	 *
	 * kauth rejects unmount(2) for a non-root caller before
	 * VFS_UNMOUNT, so a delegated "mount" permission never reaches
	 * zfs_umount().  FreeBSD lets that permission through.
	 *
	 * unmount(2) namei's the path.  After a rename the mount can
	 * still be on the previous directory; that path is taken from
	 * getvfsstat, which is the mount table Linux reads via
	 * /proc/self/mounts.  A child whose covered directory
	 * rollback or receive removed is unmounted in the kernel
	 * (zfs_resume_fs).  This fallback is the rename case: the
	 * directory is still there, under the old path.  A delegated
	 * non-root user who hits a mount the kernel could not drop
	 * gets EBUSY from destroy: kauth will not let unmount(2)
	 * through, and destroy does not dounmount for them.
	 */
	if ((flags & MS_FORCE) != 0)
		uflags |= MS_FORCE;
	if ((flags & MS_DETACH) != 0)
		uflags |= MS_DETACH;
	if (zhp != NULL) {
		error = zfs_unmount_dot_snapshots(zfs_get_name(zhp), mntpt,
		    uflags);
		if (error != 0)
			return (error);
	}
	if (unmount(mntpt, uflags) == 0)
		return (0);
	error = errno;
	if (error == ENOENT && zhp != NULL) {
		struct statvfs *sfs = NULL;
		int n, i;
		const char *name = zfs_get_name(zhp);

		n = getvfsstat(NULL, 0, ST_NOWAIT);
		if (n > 0) {
			sfs = calloc((size_t)n, sizeof (sfs[0]));
			if (sfs != NULL) {
				n = getvfsstat(sfs, n * sizeof (sfs[0]),
				    ST_NOWAIT);
			} else {
				n = -1;
			}
		}
		for (i = 0; n > 0 && i < n; i++) {
			if (strcmp(sfs[i].f_fstypename, MNTTYPE_ZFS) != 0)
				continue;
			if (strcmp(sfs[i].f_mntfromname, name) != 0)
				continue;
			if (strcmp(sfs[i].f_mntonname, mntpt) == 0)
				continue;
			if (unmount(sfs[i].f_mntonname, uflags) == 0) {
				free(sfs);
				return (0);
			}
			error = errno;
		}
		free(sfs);
	}
	return (error);
}

int
zfs_mount_setattr(zfs_handle_t *zhp, uint32_t nspflags)
{
	(void) nspflags;
	return (zfs_mount(zhp, MNTOPT_REMOUNT, 0));
}

int
zfs_mount_delegation_check(void)
{
	return (0);
}

void
zpool_disable_datasets_os(zpool_handle_t *zhp, boolean_t force)
{
	(void) zhp;
	(void) force;
}

void
zpool_disable_volume_os(const char *name)
{
	(void) name;
}
