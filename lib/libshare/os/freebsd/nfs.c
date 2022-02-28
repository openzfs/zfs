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

/*
 * This function translates options to a format acceptable by exports(5), eg.
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
 */
static int
translate_opts(const char *shareopts, FILE *out)
{
	static const char *const known_opts[] = { "ro", "maproot", "mapall",
	    "mask", "network", "sec", "alldirs", "public", "webnfs", "index",
	    "quiet" };
	char oldopts[OPTSSIZE], newopts[OPTSSIZE];
	char *o, *s = NULL;
	unsigned int i;
	size_t len;

	strlcpy(oldopts, shareopts, sizeof (oldopts));
	newopts[0] = '\0';
	s = oldopts;
	while ((o = strsep(&s, "-, ")) != NULL) {
		if (o[0] == '\0')
			continue;
		for (i = 0; i < ARRAY_SIZE(known_opts); ++i) {
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
	return (fputs(newopts, out));
}

static int
nfs_enable_share_impl(sa_share_impl_t impl_share, FILE *tmpfile)
{
	const char *shareopts = impl_share->sa_shareopts;
	if (strcmp(shareopts, "on") == 0)
		shareopts = "";

	boolean_t need_free;
	char *mp;
	int rc  = nfs_escape_mountpoint(impl_share->sa_mountpoint, &mp,
	    &need_free);
	if (rc != SA_OK)
		return (rc);

	if (fputs(mp, tmpfile) == EOF ||
	    fputc('\t', tmpfile) == EOF ||
	    translate_opts(shareopts, tmpfile) == EOF ||
	    fputc('\n', tmpfile) == EOF) {
		fprintf(stderr, "failed to write to temporary file\n");
		rc = SA_SYSTEM_ERR;
	}

	if (need_free)
		free(mp);
	return (rc);
}

static int
nfs_enable_share(sa_share_impl_t impl_share)
{
	return (nfs_toggle_share(
	    ZFS_EXPORTS_LOCK, ZFS_EXPORTS_FILE, NULL, impl_share,
	    nfs_enable_share_impl));
}

static int
nfs_disable_share_impl(sa_share_impl_t impl_share, FILE *tmpfile)
{
	(void) impl_share, (void) tmpfile;
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
	return (nfs_is_shared_impl(ZFS_EXPORTS_FILE, impl_share));
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
	struct pidfh *pfh;
	pid_t mountdpid;

start:
	pfh = pidfile_open(_PATH_MOUNTDPID, 0600, &mountdpid);
	if (pfh != NULL) {
		/* mountd(8) is not running. */
		pidfile_remove(pfh);
		return (SA_OK);
	}
	if (errno != EEXIST) {
		/* Cannot open pidfile for some reason. */
		return (SA_SYSTEM_ERR);
	}
	if (mountdpid == -1) {
		/* mountd(8) exists, but didn't write the PID yet */
		usleep(500);
		goto start;
	}
	/* We have mountd(8) PID in mountdpid variable. */
	kill(mountdpid, SIGHUP);
	return (SA_OK);
}

const sa_fstype_t libshare_nfs_type = {
	.enable_share = nfs_enable_share,
	.disable_share = nfs_disable_share,
	.is_shared = nfs_is_shared,

	.validate_shareopts = nfs_validate_shareopts,
	.commit_shares = nfs_commit_shares,
};
