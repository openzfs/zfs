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

#include <sys/param.h>
#include <sys/vfs.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <libintl.h>

#include <libshare.h>
#include "libshare_impl.h"
#include "nfs.h"

#define	OPTSSIZE	1024
#define	MAXLINESIZE	(PATH_MAX + OPTSSIZE)
#define	ZFS_EXPORTS_FILE	"/etc/exports"
#define	ZFS_EXPORTS_LOCK	ZFS_EXPORTS_FILE".lock"

static int nfs_lock_fd = -1;

/*
 * The nfs_exports_[lock|unlock] is used to guard against conconcurrent
 * updates to the exports file. Each protocol is responsible for
 * providing the necessary locking to ensure consistency.
 */
static int
nfs_exports_lock(void)
{
	nfs_lock_fd = open(ZFS_EXPORTS_LOCK,
	    O_RDWR | O_CREAT, 0600);
	if (nfs_lock_fd == -1) {
		fprintf(stderr, "failed to lock %s: %s\n",
		    ZFS_EXPORTS_LOCK, strerror(errno));
		return (errno);
	}
	if (flock(nfs_lock_fd, LOCK_EX) != 0) {
		fprintf(stderr, "failed to lock %s: %s\n",
		    ZFS_EXPORTS_LOCK, strerror(errno));
		return (errno);
	}
	return (0);
}

static void
nfs_exports_unlock(void)
{
	verify(nfs_lock_fd > 0);

	if (flock(nfs_lock_fd, LOCK_UN) != 0) {
		fprintf(stderr, "failed to unlock %s: %s\n",
		    ZFS_EXPORTS_LOCK, strerror(errno));
	}
	close(nfs_lock_fd);
	nfs_lock_fd = -1;
}

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

static char *
nfs_init_tmpfile(void)
{
	char *tmpfile = NULL;

	if (asprintf(&tmpfile, "%s%s", ZFS_EXPORTS_FILE, ".XXXXXXXX") == -1) {
		fprintf(stderr, "Unable to allocate buffer for temporary "
		    "file name\n");
		return (NULL);
	}

	int fd = mkstemp(tmpfile);
	if (fd == -1) {
		fprintf(stderr, "Unable to create temporary file: %s",
		    strerror(errno));
		free(tmpfile);
		return (NULL);
	}
	close(fd);
	return (tmpfile);
}

static int
nfs_fini_tmpfile(char *tmpfile)
{
	if (rename(tmpfile, ZFS_EXPORTS_FILE) == -1) {
		fprintf(stderr, "Unable to rename %s: %s\n", tmpfile,
		    strerror(errno));
		unlink(tmpfile);
		free(tmpfile);
		return (SA_SYSTEM_ERR);
	}
	free(tmpfile);
	return (SA_OK);
}

/*
 * This function copies all entries from the exports file to "filename",
 * omitting any entries for the specified mountpoint.
 */
static int
nfs_copy_entries(char *filename, const char *mountpoint)
{
	int error = SA_OK;
	char *line;

	/*
	 * If the file doesn't exist then there is nothing more
	 * we need to do.
	 */
	FILE *oldfp = fopen(ZFS_EXPORTS_FILE, "r");
	if (oldfp == NULL)
		return (SA_OK);

	FILE *newfp = fopen(filename, "w+");
	fputs(FILE_HEADER, newfp);
	while ((line = zgetline(oldfp, mountpoint)) != NULL)
		fprintf(newfp, "%s\n", line);
	if (ferror(oldfp) != 0) {
		error = ferror(oldfp);
	}
	if (error == 0 && ferror(newfp) != 0) {
		error = ferror(newfp);
	}

	if (fclose(newfp) != 0) {
		fprintf(stderr, "Unable to close file %s: %s\n",
		    filename, strerror(errno));
		error = error != 0 ? error : SA_SYSTEM_ERR;
	}
	fclose(oldfp);

	return (error);
}

static int
nfs_enable_share(sa_share_impl_t impl_share)
{
	char *filename = NULL;
	int error;

	if ((filename = nfs_init_tmpfile()) == NULL)
		return (SA_SYSTEM_ERR);

	error = nfs_exports_lock();
	if (error != 0) {
		unlink(filename);
		free(filename);
		return (error);
	}

	error = nfs_copy_entries(filename, impl_share->sa_mountpoint);
	if (error != SA_OK) {
		unlink(filename);
		free(filename);
		nfs_exports_unlock();
		return (error);
	}

	FILE *fp = fopen(filename, "a+");
	if (fp == NULL) {
		fprintf(stderr, "failed to open %s file: %s", filename,
		    strerror(errno));
		unlink(filename);
		free(filename);
		nfs_exports_unlock();
		return (SA_SYSTEM_ERR);
	}
	const char *shareopts = impl_share->sa_shareopts;
	if (strcmp(shareopts, "on") == 0)
		shareopts = "";

	if (fprintf(fp, "%s\t%s\n", impl_share->sa_mountpoint,
	    translate_opts(shareopts)) < 0) {
		fprintf(stderr, "failed to write to %s\n", filename);
		fclose(fp);
		unlink(filename);
		free(filename);
		nfs_exports_unlock();
		return (SA_SYSTEM_ERR);
	}

	if (fclose(fp) != 0) {
		fprintf(stderr, "Unable to close file %s: %s\n",
		    filename, strerror(errno));
		unlink(filename);
		free(filename);
		nfs_exports_unlock();
		return (SA_SYSTEM_ERR);
	}
	error = nfs_fini_tmpfile(filename);
	nfs_exports_unlock();
	return (error);
}

static int
nfs_disable_share(sa_share_impl_t impl_share)
{
	int error;
	char *filename = NULL;

	if ((filename = nfs_init_tmpfile()) == NULL)
		return (SA_SYSTEM_ERR);

	error = nfs_exports_lock();
	if (error != 0) {
		unlink(filename);
		free(filename);
		return (error);
	}

	error = nfs_copy_entries(filename, impl_share->sa_mountpoint);
	if (error != SA_OK) {
		unlink(filename);
		free(filename);
		nfs_exports_unlock();
		return (error);
	}

	error = nfs_fini_tmpfile(filename);
	nfs_exports_unlock();
	return (error);
}

/*
 * NOTE: This function returns a static buffer and thus is not thread-safe.
 */
static boolean_t
nfs_is_shared(sa_share_impl_t impl_share)
{
	static char line[MAXLINESIZE];
	char *s, last;
	size_t len;
	const char *mntpoint = impl_share->sa_mountpoint;
	size_t mntlen = strlen(mntpoint);

	FILE *fp = fopen(ZFS_EXPORTS_FILE, "r");
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
	(void) shareopts;
	return (SA_OK);
}

/*
 * Commit the shares by restarting mountd.
 */
static int
nfs_commit_shares(void)
{
	return (SA_OK);
}

const sa_fstype_t libshare_nfs_type = {
	.enable_share = nfs_enable_share,
	.disable_share = nfs_disable_share,
	.is_shared = nfs_is_shared,

	.validate_shareopts = nfs_validate_shareopts,
	.commit_shares = nfs_commit_shares,
};
