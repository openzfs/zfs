/*
 * Copyright (c) 2007 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
 *
 * Copyright (c) 2020 by Delphix. All rights reserved.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/vfs.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <libutil.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <libintl.h>

#include <libshare.h>
#include "libshare_impl.h"
#include "nfs.h"

#define	_PATH_MOUNTDPID	"/var/run/mountd.pid"
#define	OPTSSIZE	1024
#define	MAXLINESIZE	(PATH_MAX + OPTSSIZE)
#define	ZFS_EXPORTS_FILE	"/etc/zfs/exports"
#define	ZFS_EXPORTS_LOCK	ZFS_EXPORTS_FILE".lock"

static sa_fstype_t *nfs_fstype;

/*
 * Read one line from a file. Skip comments, empty lines and a line with a
 * mountpoint specified in the 'skip' argument.
 *
 * NOTE: This function returns a static buffer and thus is not thread-safe.
 */
static char *
zgetline(FILE *fd, const char *skip)
{
	static char line[MAXLINESIZE];
	size_t len, skiplen = 0;
	char *s, last;

	if (skip != NULL)
		skiplen = strlen(skip);
	for (;;) {
		s = fgets(line, sizeof (line), fd);
		if (s == NULL)
			return (NULL);
		/* Skip empty lines and comments. */
		if (line[0] == '\n' || line[0] == '#')
			continue;
		len = strlen(line);
		if (line[len - 1] == '\n')
			line[len - 1] = '\0';
		last = line[skiplen];
		/* Skip the given mountpoint. */
		if (skip != NULL && strncmp(skip, line, skiplen) == 0 &&
		    (last == '\t' || last == ' ' || last == '\0')) {
			continue;
		}
		break;
	}
	return (line);
}

/*
 * This function translate options to a format acceptable by exports(5), eg.
 *
 *	-ro -network=192.168.0.0 -mask=255.255.255.0 -maproot=0 \
 *	zfs.freebsd.org 69.147.83.54
 *
 * Accepted input formats:
 *
 *	ro,network=192.168.0.0,mask=255.255.255.0,maproot=0,zfs.freebsd.org
 *	ro network=192.168.0.0 mask=255.255.255.0 maproot=0 zfs.freebsd.org
 *	-ro,-network=192.168.0.0,-mask=255.255.255.0,-maproot=0,zfs.freebsd.org
 *	-ro -network=192.168.0.0 -mask=255.255.255.0 -maproot=0 \
 *	zfs.freebsd.org
 *
 * Recognized keywords:
 *
 *	ro, maproot, mapall, mask, network, sec, alldirs, public, webnfs,
 *	index, quiet
 *
 * NOTE: This function returns a static buffer and thus is not thread-safe.
 */
static char *
translate_opts(const char *shareopts)
{
	static const char *known_opts[] = { "ro", "maproot", "mapall", "mask",
	    "network", "sec", "alldirs", "public", "webnfs", "index", "quiet",
	    NULL };
	static char newopts[OPTSSIZE];
	char oldopts[OPTSSIZE];
	char *o, *s = NULL;
	unsigned int i;
	size_t len;

	strlcpy(oldopts, shareopts, sizeof (oldopts));
	newopts[0] = '\0';
	s = oldopts;
	while ((o = strsep(&s, "-, ")) != NULL) {
		if (o[0] == '\0')
			continue;
		for (i = 0; known_opts[i] != NULL; i++) {
			len = strlen(known_opts[i]);
			if (strncmp(known_opts[i], o, len) == 0 &&
			    (o[len] == '\0' || o[len] == '=')) {
				strlcat(newopts, "-", sizeof (newopts));
				break;
			}
		}
		strlcat(newopts, o, sizeof (newopts));
		strlcat(newopts, " ", sizeof (newopts));
	}
	return (newopts);
}

/*
 * This function copies all entries from the exports file to "filename",
 * omitting any entries for the specified mountpoint.
 */
int
nfs_copy_entries(char *filename, const char *mountpoint)
{
	int error = SA_OK;
	char *line;

	FILE *oldfp = fopen(ZFS_EXPORTS_FILE, "re");
	FILE *newfp = fopen(filename, "w+e");
	if (newfp == NULL) {
		fprintf(stderr, "failed to open %s file: %s", filename,
		    strerror(errno));
		fclose(oldfp);
		return (SA_SYSTEM_ERR);
	}
	fputs(FILE_HEADER, newfp);

	/*
	 * The ZFS_EXPORTS_FILE may not exist yet. If that's the
	 * case then just write out the new file.
	 */
	if (oldfp != NULL) {
		while ((line = zgetline(oldfp, mountpoint)) != NULL)
			fprintf(newfp, "%s\n", line);
		if (ferror(oldfp) != 0) {
			error = ferror(oldfp);
		}
		if (fclose(oldfp) != 0) {
			fprintf(stderr, "Unable to close file %s: %s\n",
			    filename, strerror(errno));
			error = error != 0 ? error : SA_SYSTEM_ERR;
		}
	}

	if (error == 0 && ferror(newfp) != 0) {
		error = ferror(newfp);
	}

	if (fclose(newfp) != 0) {
		fprintf(stderr, "Unable to close file %s: %s\n",
		    filename, strerror(errno));
		error = error != 0 ? error : SA_SYSTEM_ERR;
	}
	return (error);
}

static int
nfs_enable_share_impl(sa_share_impl_t impl_share, char *filename)
{
	FILE *fp = fopen(filename, "a+e");
	if (fp == NULL) {
		fprintf(stderr, "failed to open %s file: %s", filename,
		    strerror(errno));
		return (SA_SYSTEM_ERR);
	}

	char *shareopts = FSINFO(impl_share, nfs_fstype)->shareopts;
	if (strcmp(shareopts, "on") == 0)
		shareopts = "";

	if (fprintf(fp, "%s\t%s\n", impl_share->sa_mountpoint,
	    translate_opts(shareopts)) < 0) {
		fprintf(stderr, "failed to write to %s\n", filename);
		fclose(fp);
		return (SA_SYSTEM_ERR);
	}

	if (fclose(fp) != 0) {
		fprintf(stderr, "Unable to close file %s: %s\n",
		    filename, strerror(errno));
		return (SA_SYSTEM_ERR);
	}

	return (SA_OK);
}

static int
nfs_enable_share(sa_share_impl_t impl_share)
{
	return (nfs_toggle_share(
	    ZFS_EXPORTS_LOCK, ZFS_EXPORTS_FILE, NULL, impl_share,
	    nfs_enable_share_impl));
}

static int
nfs_disable_share_impl(sa_share_impl_t impl_share, char *filename)
{
	return (SA_OK);
}

static int
nfs_disable_share(sa_share_impl_t impl_share)
{
	return (nfs_toggle_share(
	    ZFS_EXPORTS_LOCK, ZFS_EXPORTS_FILE, NULL, impl_share,
	    nfs_disable_share_impl));
}

static boolean_t
nfs_is_shared(sa_share_impl_t impl_share)
{
	char *s, last, line[MAXLINESIZE];
	size_t len;
	char *mntpoint = impl_share->sa_mountpoint;
	size_t mntlen = strlen(mntpoint);

	FILE *fp = fopen(ZFS_EXPORTS_FILE, "re");
	if (fp == NULL)
		return (B_FALSE);

	for (;;) {
		s = fgets(line, sizeof (line), fp);
		if (s == NULL)
			return (B_FALSE);
		/* Skip empty lines and comments. */
		if (line[0] == '\n' || line[0] == '#')
			continue;
		len = strlen(line);
		if (line[len - 1] == '\n')
			line[len - 1] = '\0';
		last = line[mntlen];
		/* Skip the given mountpoint. */
		if (strncmp(mntpoint, line, mntlen) == 0 &&
		    (last == '\t' || last == ' ' || last == '\0')) {
			fclose(fp);
			return (B_TRUE);
		}
	}
	fclose(fp);
	return (B_FALSE);
}

static int
nfs_validate_shareopts(const char *shareopts)
{
	return (SA_OK);
}

static int
nfs_update_shareopts(sa_share_impl_t impl_share, const char *shareopts)
{
	FSINFO(impl_share, nfs_fstype)->shareopts = (char *)shareopts;
	return (SA_OK);
}

static void
nfs_clear_shareopts(sa_share_impl_t impl_share)
{
	FSINFO(impl_share, nfs_fstype)->shareopts = NULL;
}

/*
 * Commit the shares by restarting mountd.
 */
static int
nfs_commit_shares(void)
{
	struct pidfh *pfh;
	pid_t mountdpid;

	pfh = pidfile_open(_PATH_MOUNTDPID, 0600, &mountdpid);
	if (pfh != NULL) {
		/* Mountd is not running. */
		pidfile_remove(pfh);
		return (SA_OK);
	}
	if (errno != EEXIST) {
		/* Cannot open pidfile for some reason. */
		return (SA_SYSTEM_ERR);
	}
	/* We have mountd(8) PID in mountdpid variable. */
	kill(mountdpid, SIGHUP);
	return (SA_OK);
}

static const sa_share_ops_t nfs_shareops = {
	.enable_share = nfs_enable_share,
	.disable_share = nfs_disable_share,
	.is_shared = nfs_is_shared,

	.validate_shareopts = nfs_validate_shareopts,
	.update_shareopts = nfs_update_shareopts,
	.clear_shareopts = nfs_clear_shareopts,
	.commit_shares = nfs_commit_shares,
};

/*
 * Initializes the NFS functionality of libshare.
 */
void
libshare_nfs_init(void)
{
	nfs_fstype = register_fstype("nfs", &nfs_shareops);
}
