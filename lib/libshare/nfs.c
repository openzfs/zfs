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


#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <libshare.h>
#include "nfs.h"


static int nfs_lock_fd = -1;


/*
 * nfs_exports_[lock|unlock] are used to guard against conconcurrent
 * updates to the exports file. Each protocol is responsible for
 * providing the necessary locking to ensure consistency.
 */
static int
nfs_exports_lock(const char *name)
{
	int err;

	nfs_lock_fd = open(name, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
	if (nfs_lock_fd == -1) {
		err = errno;
		fprintf(stderr, "failed to lock %s: %s\n", name, strerror(err));
		return (err);
	}

	if (flock(nfs_lock_fd, LOCK_EX) != 0) {
		err = errno;
		fprintf(stderr, "failed to lock %s: %s\n", name, strerror(err));
		(void) close(nfs_lock_fd);
		nfs_lock_fd = -1;
		return (err);
	}

	return (0);
}

static void
nfs_exports_unlock(const char *name)
{
	verify(nfs_lock_fd > 0);

	if (flock(nfs_lock_fd, LOCK_UN) != 0) {
		fprintf(stderr, "failed to unlock %s: %s\n",
		    name, strerror(errno));
	}

	(void) close(nfs_lock_fd);
	nfs_lock_fd = -1;
}

static char *
nfs_init_tmpfile(const char *prefix, const char *mdir)
{
	char *tmpfile = NULL;
	struct stat sb;

	if (mdir != NULL &&
	    stat(mdir, &sb) < 0 &&
	    mkdir(mdir, 0755) < 0) {
		fprintf(stderr, "failed to create %s: %s\n",
		    mdir, strerror(errno));
		return (NULL);
	}

	if (asprintf(&tmpfile, "%s.XXXXXXXX", prefix) == -1) {
		fprintf(stderr, "Unable to allocate temporary file\n");
		return (NULL);
	}

	int fd = mkostemp(tmpfile, O_CLOEXEC);
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
nfs_fini_tmpfile(const char *exports, char *tmpfile)
{
	if (rename(tmpfile, exports) == -1) {
		fprintf(stderr, "Unable to rename %s: %s\n", tmpfile,
		    strerror(errno));
		unlink(tmpfile);
		free(tmpfile);
		return (SA_SYSTEM_ERR);
	}
	free(tmpfile);
	return (SA_OK);
}

int
nfs_toggle_share(const char *lockfile, const char *exports,
    const char *expdir, sa_share_impl_t impl_share,
    int(*cbk)(sa_share_impl_t impl_share, char *filename))
{
	int error;
	char *filename;

	if ((filename = nfs_init_tmpfile(exports, expdir)) == NULL)
		return (SA_SYSTEM_ERR);

	error = nfs_exports_lock(lockfile);
	if (error != 0) {
		unlink(filename);
		free(filename);
		return (error);
	}

	error = nfs_copy_entries(filename, impl_share->sa_mountpoint);
	if (error != SA_OK)
		goto fullerr;

	error = cbk(impl_share, filename);
	if (error != SA_OK)
		goto fullerr;

	error = nfs_fini_tmpfile(exports, filename);
	nfs_exports_unlock(lockfile);
	return (error);

fullerr:
	unlink(filename);
	free(filename);
	nfs_exports_unlock(lockfile);
	return (error);
}
