/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <libshare.h>
#include <unistd.h>
#include "nfs.h"


/*
 * nfs_exports_[lock|unlock] are used to guard against conconcurrent
 * updates to the exports file. Each protocol is responsible for
 * providing the necessary locking to ensure consistency.
 */
static int
nfs_exports_lock(const char *name, int *nfs_lock_fd)
{
	int err;

	*nfs_lock_fd = open(name, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
	if (*nfs_lock_fd == -1) {
		err = errno;
		fprintf(stderr, "failed to lock %s: %s\n", name, strerror(err));
		return (err);
	}

	while ((err = flock(*nfs_lock_fd, LOCK_EX)) != 0 && errno == EINTR)
		;
	if (err != 0) {
		err = errno;
		fprintf(stderr, "failed to lock %s: %s\n", name, strerror(err));
		(void) close(*nfs_lock_fd);
		*nfs_lock_fd = -1;
		return (err);
	}

	return (0);
}

static void
nfs_exports_unlock(const char *name, int *nfs_lock_fd)
{
	verify(*nfs_lock_fd > 0);

	if (flock(*nfs_lock_fd, LOCK_UN) != 0)
		fprintf(stderr, "failed to unlock %s: %s\n",
		    name, strerror(errno));

	(void) close(*nfs_lock_fd);
	*nfs_lock_fd = -1;
}

struct tmpfile {
	/*
	 * This only needs to be as wide as ZFS_EXPORTS_FILE and mktemp suffix,
	 * 64 is more than enough.
	 */
	char name[64];
	FILE *fp;
};

static boolean_t
nfs_init_tmpfile(const char *prefix, const char *mdir, struct tmpfile *tmpf)
{
	if (mdir != NULL &&
	    mkdir(mdir, 0755) < 0 &&
	    errno != EEXIST) {
		fprintf(stderr, "failed to create %s: %s\n",
		    mdir, strerror(errno));
		return (B_FALSE);
	}

	strlcpy(tmpf->name, prefix, sizeof (tmpf->name));
	strlcat(tmpf->name, ".XXXXXXXX", sizeof (tmpf->name) - strlen(prefix));

	int fd = mkostemp(tmpf->name, O_CLOEXEC);
	if (fd == -1) {
		fprintf(stderr, "Unable to create temporary file: %s",
		    strerror(errno));
		return (B_FALSE);
	}

	tmpf->fp = fdopen(fd, "w+");
	if (tmpf->fp == NULL) {
		fprintf(stderr, "Unable to reopen temporary file: %s",
		    strerror(errno));
		close(fd);
		return (B_FALSE);
	}

	return (B_TRUE);
}

static void
nfs_abort_tmpfile(struct tmpfile *tmpf)
{
	unlink(tmpf->name);
	fclose(tmpf->fp);
}

static int
nfs_fini_tmpfile(const char *exports, struct tmpfile *tmpf)
{
	if (fflush(tmpf->fp) != 0) {
		fprintf(stderr, "Failed to write to temporary file: %s\n",
		    strerror(errno));
		nfs_abort_tmpfile(tmpf);
		return (SA_SYSTEM_ERR);
	}

	if (rename(tmpf->name, exports) == -1) {
		fprintf(stderr, "Unable to rename %s -> %s: %s\n",
		    tmpf->name, exports, strerror(errno));
		nfs_abort_tmpfile(tmpf);
		return (SA_SYSTEM_ERR);
	}

	(void) fchmod(fileno(tmpf->fp), 0644);
	fclose(tmpf->fp);
	return (SA_OK);
}

int
nfs_escape_mountpoint(const char *mp, char **out, boolean_t *need_free)
{
	if (strpbrk(mp, "\t\n\v\f\r \\") == NULL) {
		*out = (char *)mp;
		*need_free = B_FALSE;
		return (SA_OK);
	} else {
		size_t len = strlen(mp);
		*out = malloc(len * 4 + 1);
		if (!*out)
			return (SA_NO_MEMORY);
		*need_free = B_TRUE;

		char *oc = *out;
		for (const char *c = mp; c < mp + len; ++c)
			if (memchr("\t\n\v\f\r \\", *c,
			    strlen("\t\n\v\f\r \\"))) {
				sprintf(oc, "\\%03hho", *c);
				oc += 4;
			} else
				*oc++ = *c;
		*oc = '\0';
	}

	return (SA_OK);
}

static int
nfs_process_exports(const char *exports, const char *mountpoint,
    boolean_t (*cbk)(void *userdata, char *line, boolean_t found_mountpoint),
    void *userdata)
{
	int error = SA_OK;
	boolean_t cont = B_TRUE;

	FILE *oldfp = fopen(exports, "re");
	if (oldfp != NULL) {
		boolean_t need_mp_free;
		char *mp;
		if ((error = nfs_escape_mountpoint(mountpoint,
		    &mp, &need_mp_free)) != SA_OK) {
			(void) fclose(oldfp);
			return (error);
		}

		char *buf = NULL, *sep;
		size_t buflen = 0, mplen = strlen(mp);

		while (cont && getline(&buf, &buflen, oldfp) != -1) {
			if (buf[0] == '\n' || buf[0] == '#')
				continue;

			cont = cbk(userdata, buf,
			    (sep = strpbrk(buf, "\t \n")) != NULL &&
			    sep - buf == mplen &&
			    strncmp(buf, mp, mplen) == 0);
		}
		free(buf);
		if (need_mp_free)
			free(mp);

		if (ferror(oldfp) != 0)
			error = ferror(oldfp);

		if (fclose(oldfp) != 0) {
			fprintf(stderr, "Unable to close file %s: %s\n",
			    exports, strerror(errno));
			error = error != SA_OK ? error : SA_SYSTEM_ERR;
		}
	}

	return (error);
}

static boolean_t
nfs_copy_entries_cb(void *userdata, char *line, boolean_t found_mountpoint)
{
	FILE *newfp = userdata;
	if (!found_mountpoint)
		fputs(line, newfp);
	return (B_TRUE);
}

/*
 * Copy all entries from the exports file (if it exists) to newfp,
 * omitting any entries for the specified mountpoint.
 */
static int
nfs_copy_entries(FILE *newfp, const char *exports, const char *mountpoint)
{
	fputs(FILE_HEADER, newfp);

	int error = nfs_process_exports(
	    exports, mountpoint, nfs_copy_entries_cb, newfp);

	if (error == SA_OK && ferror(newfp) != 0)
		error = ferror(newfp);

	return (error);
}

int
nfs_toggle_share(const char *lockfile, const char *exports,
    const char *expdir, sa_share_impl_t impl_share,
    int(*cbk)(sa_share_impl_t impl_share, FILE *tmpfile))
{
	int error, nfs_lock_fd = -1;
	struct tmpfile tmpf;

	if (!nfs_init_tmpfile(exports, expdir, &tmpf))
		return (SA_SYSTEM_ERR);

	error = nfs_exports_lock(lockfile, &nfs_lock_fd);
	if (error != 0) {
		nfs_abort_tmpfile(&tmpf);
		return (error);
	}

	error = nfs_copy_entries(tmpf.fp, exports, impl_share->sa_mountpoint);
	if (error != SA_OK)
		goto fullerr;

	error = cbk(impl_share, tmpf.fp);
	if (error != SA_OK)
		goto fullerr;

	error = nfs_fini_tmpfile(exports, &tmpf);
	nfs_exports_unlock(lockfile, &nfs_lock_fd);
	return (error);

fullerr:
	nfs_abort_tmpfile(&tmpf);
	nfs_exports_unlock(lockfile, &nfs_lock_fd);
	return (error);
}

void
nfs_reset_shares(const char *lockfile, const char *exports)
{
	int nfs_lock_fd = -1;

	if (nfs_exports_lock(lockfile, &nfs_lock_fd) == 0) {
		(void) ! truncate(exports, 0);
		nfs_exports_unlock(lockfile, &nfs_lock_fd);
	}
}

static boolean_t
nfs_is_shared_cb(void *userdata, char *line, boolean_t found_mountpoint)
{
	(void) line;

	boolean_t *found = userdata;
	*found = found_mountpoint;
	return (!found_mountpoint);
}

boolean_t
nfs_is_shared_impl(const char *exports, sa_share_impl_t impl_share)
{
	boolean_t found = B_FALSE;
	nfs_process_exports(exports, impl_share->sa_mountpoint,
	    nfs_is_shared_cb, &found);
	return (found);
}
