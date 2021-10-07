/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
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
 * Copyright (c) 2013 Martin Matuska <mm@FreeBSD.org>. All rights reserved.
 */
#include "../../libzfs_impl.h"
#include <libzfs.h>
#include <libzutil.h>
#include <sys/sysctl.h>
#include <libintl.h>
#include <sys/linker.h>
#include <sys/module.h>
#include <sys/stat.h>
#include <sys/param.h>

#ifdef IN_BASE
#define	ZFS_KMOD	"zfs"
#else
#define	ZFS_KMOD	"openzfs"
#endif

void
libzfs_set_pipe_max(int infd)
{
	/* FreeBSD automatically resizes */
}

static int
execvPe(const char *name, const char *path, char * const *argv,
    char * const *envp)
{
	const char **memp;
	size_t cnt, lp, ln;
	int eacces, save_errno;
	char buf[MAXPATHLEN];
	const char *bp, *np, *op, *p;
	struct stat sb;

	eacces = 0;

	/* If it's an absolute or relative path name, it's easy. */
	if (strchr(name, '/')) {
		bp = name;
		op = NULL;
		goto retry;
	}
	bp = buf;

	/* If it's an empty path name, fail in the usual POSIX way. */
	if (*name == '\0') {
		errno = ENOENT;
		return (-1);
	}

	op = path;
	ln = strlen(name);
	while (op != NULL) {
		np = strchrnul(op, ':');

		/*
		 * It's a SHELL path -- double, leading and trailing colons
		 * mean the current directory.
		 */
		if (np == op) {
			/* Empty component. */
			p = ".";
			lp = 1;
		} else {
			/* Non-empty component. */
			p = op;
			lp = np - op;
		}

		/* Advance to the next component or terminate after this. */
		if (*np == '\0')
			op = NULL;
		else
			op = np + 1;

		/*
		 * If the path is too long complain.  This is a possible
		 * security issue; given a way to make the path too long
		 * the user may execute the wrong program.
		 */
		if (lp + ln + 2 > sizeof (buf)) {
			(void) write(STDERR_FILENO, "execvP: ", 8);
			(void) write(STDERR_FILENO, p, lp);
			(void) write(STDERR_FILENO, ": path too long\n",
			    16);
			continue;
		}
		bcopy(p, buf, lp);
		buf[lp] = '/';
		bcopy(name, buf + lp + 1, ln);
		buf[lp + ln + 1] = '\0';

retry:		(void) execve(bp, argv, envp);
		switch (errno) {
		case E2BIG:
			goto done;
		case ELOOP:
		case ENAMETOOLONG:
		case ENOENT:
			break;
		case ENOEXEC:
			for (cnt = 0; argv[cnt]; ++cnt)
				;

			/*
			 * cnt may be 0 above; always allocate at least
			 * 3 entries so that we can at least fit "sh", bp, and
			 * the NULL terminator.  We can rely on cnt to take into
			 * account the NULL terminator in all other scenarios,
			 * as we drop argv[0].
			 */
			memp = alloca(MAX(3, cnt + 2) * sizeof (char *));
			if (memp == NULL) {
				/* errno = ENOMEM; XXX override ENOEXEC? */
				goto done;
			}
			if (cnt > 0) {
				memp[0] = argv[0];
				memp[1] = bp;
				bcopy(argv + 1, memp + 2,
				    cnt * sizeof (char *));
			} else {
				memp[0] = "sh";
				memp[1] = bp;
				memp[2] = NULL;
			}
			(void) execve(_PATH_BSHELL,
			    __DECONST(char **, memp), envp);
			goto done;
		case ENOMEM:
			goto done;
		case ENOTDIR:
			break;
		case ETXTBSY:
			/*
			 * We used to retry here, but sh(1) doesn't.
			 */
			goto done;
		default:
			/*
			 * EACCES may be for an inaccessible directory or
			 * a non-executable file.  Call stat() to decide
			 * which.  This also handles ambiguities for EFAULT
			 * and EIO, and undocumented errors like ESTALE.
			 * We hope that the race for a stat() is unimportant.
			 */
			save_errno = errno;
			if (stat(bp, &sb) != 0)
				break;
			if (save_errno == EACCES) {
				eacces = 1;
				continue;
			}
			errno = save_errno;
			goto done;
		}
	}
	if (eacces)
		errno = EACCES;
	else
		errno = ENOENT;
done:
	return (-1);
}

int
execvpe(const char *name, char * const argv[], char * const envp[])
{
	const char *path;

	/* Get the path we're searching. */
	if ((path = getenv("PATH")) == NULL)
		path = _PATH_DEFPATH;

	return (execvPe(name, path, argv, envp));
}

#define	ERRBUFLEN 256

static __thread char errbuf[ERRBUFLEN];

const char *
libzfs_error_init(int error)
{
	char *msg = errbuf;
	size_t len, msglen = ERRBUFLEN;

	if (modfind("zfs") < 0) {
		len = snprintf(msg, msglen, dgettext(TEXT_DOMAIN,
		    "Failed to load %s module: "), ZFS_KMOD);
		msg += len;
		msglen -= len;
	}

	(void) snprintf(msg, msglen, "%s", strerror(error));

	return (errbuf);
}

int
zfs_ioctl(libzfs_handle_t *hdl, int request, zfs_cmd_t *zc)
{
	return (lzc_ioctl_fd(hdl->libzfs_fd, request, zc));
}

/*
 * Verify the required ZFS_DEV device is available and optionally attempt
 * to load the ZFS modules.  Under normal circumstances the modules
 * should already have been loaded by some external mechanism.
 */
int
libzfs_load_module(void)
{
	/*
	 * XXX: kldfind(ZFS_KMOD) would be nice here, but we retain
	 * modfind("zfs") so out-of-base openzfs userland works with the
	 * in-base module.
	 */
	if (modfind("zfs") < 0) {
		/* Not present in kernel, try loading it. */
		if (kldload(ZFS_KMOD) < 0 && errno != EEXIST) {
			return (errno);
		}
	}
	return (0);
}

int
zpool_relabel_disk(libzfs_handle_t *hdl, const char *path, const char *msg)
{
	return (0);
}

int
zpool_label_disk(libzfs_handle_t *hdl, zpool_handle_t *zhp, const char *name)
{
	return (0);
}

int
find_shares_object(differ_info_t *di)
{
	return (0);
}

int
zfs_destroy_snaps_nvl_os(libzfs_handle_t *hdl, nvlist_t *snaps)
{
	return (0);
}

/*
 * Attach/detach the given filesystem to/from the given jail.
 */
int
zfs_jail(zfs_handle_t *zhp, int jailid, int attach)
{
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	zfs_cmd_t zc = {"\0"};
	char errbuf[1024];
	unsigned long cmd;
	int ret;

	if (attach) {
		(void) snprintf(errbuf, sizeof (errbuf),
		    dgettext(TEXT_DOMAIN, "cannot jail '%s'"), zhp->zfs_name);
	} else {
		(void) snprintf(errbuf, sizeof (errbuf),
		    dgettext(TEXT_DOMAIN, "cannot unjail '%s'"), zhp->zfs_name);
	}

	switch (zhp->zfs_type) {
	case ZFS_TYPE_VOLUME:
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "volumes can not be jailed"));
		return (zfs_error(hdl, EZFS_BADTYPE, errbuf));
	case ZFS_TYPE_SNAPSHOT:
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "snapshots can not be jailed"));
		return (zfs_error(hdl, EZFS_BADTYPE, errbuf));
	case ZFS_TYPE_BOOKMARK:
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "bookmarks can not be jailed"));
		return (zfs_error(hdl, EZFS_BADTYPE, errbuf));
	case ZFS_TYPE_POOL:
	case ZFS_TYPE_FILESYSTEM:
		/* OK */
		;
	}
	assert(zhp->zfs_type == ZFS_TYPE_FILESYSTEM);

	(void) strlcpy(zc.zc_name, zhp->zfs_name, sizeof (zc.zc_name));
	zc.zc_objset_type = DMU_OST_ZFS;
	zc.zc_zoneid = jailid;

	cmd = attach ? ZFS_IOC_JAIL : ZFS_IOC_UNJAIL;
	if ((ret = zfs_ioctl(hdl, cmd, &zc)) != 0)
		zfs_standard_error(hdl, errno, errbuf);

	return (ret);
}

/*
 * Set loader options for next boot.
 */
int
zpool_nextboot(libzfs_handle_t *hdl, uint64_t pool_guid, uint64_t dev_guid,
    const char *command)
{
	zfs_cmd_t zc = {"\0"};
	nvlist_t *args;
	int error;

	args = fnvlist_alloc();
	fnvlist_add_uint64(args, ZPOOL_CONFIG_POOL_GUID, pool_guid);
	fnvlist_add_uint64(args, ZPOOL_CONFIG_GUID, dev_guid);
	fnvlist_add_string(args, "command", command);
	error = zcmd_write_src_nvlist(hdl, &zc, args);
	if (error == 0)
		error = zfs_ioctl(hdl, ZFS_IOC_NEXTBOOT, &zc);
	zcmd_free_nvlists(&zc);
	nvlist_free(args);
	return (error);
}

/*
 * Fill given version buffer with zfs kernel version.
 * Returns 0 on success, and -1 on error (with errno set)
 */
int
zfs_version_kernel(char *version, int len)
{
	size_t l = len;

	return (sysctlbyname("vfs.zfs.version.module",
	    version, &l, NULL, 0));
}
