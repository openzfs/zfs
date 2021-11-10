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
 * This file implements Solaris compatible zmount() function.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/uio.h>
#include <sys/mntent.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mnttab.h>
#include <sys/errno.h>
#include <libzfs.h>

#include "../../libzfs_impl.h"

static void
build_iovec(struct iovec **iov, int *iovlen, const char *name, void *val,
    size_t len)
{
	int i;

	if (*iovlen < 0)
		return;
	i = *iovlen;
	*iov = realloc(*iov, sizeof (**iov) * (i + 2));
	if (*iov == NULL) {
		*iovlen = -1;
		return;
	}
	(*iov)[i].iov_base = strdup(name);
	(*iov)[i].iov_len = strlen(name) + 1;
	i++;
	(*iov)[i].iov_base = val;
	if (len == (size_t)-1) {
		if (val != NULL)
			len = strlen(val) + 1;
		else
			len = 0;
	}
	(*iov)[i].iov_len = (int)len;
	*iovlen = ++i;
}

static int
do_mount_(const char *spec, const char *dir, int mflag, char *fstype,
    char *dataptr, int datalen, char *optptr, int optlen)
{
	struct iovec *iov;
	char *optstr, *p, *tofree;
	int iovlen, rv;

	assert(spec != NULL);
	assert(dir != NULL);
	assert(fstype != NULL);
	assert(strcmp(fstype, MNTTYPE_ZFS) == 0);
	assert(dataptr == NULL);
	assert(datalen == 0);
	assert(optptr != NULL);
	assert(optlen > 0);

	tofree = optstr = strdup(optptr);
	assert(optstr != NULL);

	iov = NULL;
	iovlen = 0;
	if (strstr(optstr, MNTOPT_REMOUNT) != NULL)
		build_iovec(&iov, &iovlen, "update", NULL, 0);
	if (mflag & MS_RDONLY)
		build_iovec(&iov, &iovlen, "ro", NULL, 0);
	build_iovec(&iov, &iovlen, "fstype", fstype, (size_t)-1);
	build_iovec(&iov, &iovlen, "fspath", __DECONST(char *, dir),
	    (size_t)-1);
	build_iovec(&iov, &iovlen, "from", __DECONST(char *, spec), (size_t)-1);
	while ((p = strsep(&optstr, ",/")) != NULL)
		build_iovec(&iov, &iovlen, p, NULL, (size_t)-1);
	rv = nmount(iov, iovlen, 0);
	free(tofree);
	if (rv < 0)
		return (errno);
	return (rv);
}

int
do_mount(zfs_handle_t *zhp, const char *mntpt, char *opts, int flags)
{

	return (do_mount_(zfs_get_name(zhp), mntpt, flags, MNTTYPE_ZFS, NULL, 0,
	    opts, sizeof (mntpt)));
}

int
do_unmount(zfs_handle_t *zhp, const char *mntpt, int flags)
{
	if (unmount(mntpt, flags) < 0)
		return (errno);
	return (0);
}

int
zfs_mount_delegation_check(void)
{
	return (0);
}

/* Called from the tail end of zpool_disable_datasets() */
void
zpool_disable_datasets_os(zpool_handle_t *zhp, boolean_t force)
{
}

/* Called from the tail end of zfs_unmount() */
void
zpool_disable_volume_os(const char *name)
{
}
