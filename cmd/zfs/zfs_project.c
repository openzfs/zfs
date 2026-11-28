// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */

/*
 * Copyright (c) 2017, Intle Corporation. All rights reserved.
 */

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/list.h>
#include <sys/zfs_project.h>

#include "zfs_util.h"
#include "zfs_projectutil.h"

/*
 * A directory queued for -r: its path for messages, and the directory seen
 * when it was queued.  It is reopened from the top one component at a time
 * without following links, and only read if it is still that directory.
 */
typedef struct zfs_project_item {
	list_node_t	zpi_list;
	uint64_t	zpi_id;
	uint64_t	zpi_parent;	/* zpi_id of the directory it is in */
	dev_t		zpi_dev;
	ino_t		zpi_ino;
	char		zpi_name[0];
} zfs_project_item_t;

/*
 * The walk of one -r tree.  A few recently read directories stay open, so
 * that their subdirectories, which follow them in the queue, are opened
 * from them rather than from the top.
 */
#define	ZFS_PROJECT_DIRCACHE	16

typedef struct zfs_project_walk {
	int		zpw_topfd;
	size_t		zpw_toplen;
	uint64_t	zpw_next_id;
	uint_t		zpw_next_slot;
	struct {
		uint64_t	id;
		int		fd;
	} zpw_dirs[ZFS_PROJECT_DIRCACHE];
} zfs_project_walk_t;

static void
zfs_project_item_alloc(list_t *head, zfs_project_walk_t *zpw,
    uint64_t parent, const char *name, const struct stat *st)
{
	zfs_project_item_t *zpi;

	zpi = safe_malloc(sizeof (zfs_project_item_t) + strlen(name) + 1);
	zpi->zpi_id = ++zpw->zpw_next_id;
	zpi->zpi_parent = parent;
	zpi->zpi_dev = st->st_dev;
	zpi->zpi_ino = st->st_ino;
	strcpy(zpi->zpi_name, name);
	list_insert_tail(head, zpi);
}

static int
zfs_project_sanity_check(const char *name, zfs_project_control_t *zpc,
    struct stat *st)
{
	int ret;

	ret = stat(name, st);
	if (ret) {
		(void) fprintf(stderr, "failed to stat %s: %s\n",
		    name, strerror(errno));
		return (ret);
	}

	if (!S_ISREG(st->st_mode) && !S_ISDIR(st->st_mode)) {
		(void) fprintf(stderr, zpc->zpc_extsize ?
		    "only regular files and directories have a block size "
		    "setting\n" :
		    "only support project quota on regular file or "
		    "directory\n");
		return (-1);
	}

	if (!S_ISDIR(st->st_mode)) {
		if (zpc->zpc_dironly) {
			(void) fprintf(stderr,
			    "'-d' option on non-dir target %s\n", name);
			return (-1);
		}

		if (zpc->zpc_recursive) {
			(void) fprintf(stderr,
			    "'-r' option on non-dir target %s\n", name);
			return (-1);
		}
	}

	return (0);
}

/*
 * Open the path given on the command line, which may be a symbolic link,
 * and check that it is still what zfs_project_sanity_check() saw, so that
 * a FIFO or device swapped in is neither waited on nor sent ioctls.
 */
static int
zfs_project_open_top(const char *name, const struct stat *st)
{
	struct stat fst;
	int fd;

	fd = open(name, O_RDONLY | O_NOCTTY | O_NONBLOCK);
	/* Named on the command line, a file under a lease is waited for. */
	if (fd < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
		fd = open(name, O_RDONLY | O_NOCTTY);
	if (fd < 0) {
		(void) fprintf(stderr, "failed to open %s: %s\n",
		    name, strerror(errno));
		return (-1);
	}
	if (fstat(fd, &fst) != 0 || fst.st_dev != st->st_dev ||
	    fst.st_ino != st->st_ino) {
		(void) fprintf(stderr, "%s changed while it was opened\n",
		    name);
		close(fd);
		return (-1);
	}
	return (fd);
}

static int
zfs_project_load_projid(int fd, const char *name, zfs_project_control_t *zpc)
{
	zfsxattr_t fsx;
	int ret;

	ret = ioctl(fd, ZFS_IOC_FSGETXATTR, &fsx);
	if (ret)
		(void) fprintf(stderr,
		    "failed to get xattr for %s: %s\n",
		    name, strerror(errno));
	else if (zpc->zpc_extsize)
		zpc->zpc_expected_extsize = (fsx.fsx_xflags &
		    (FS_XFLAG_EXTSIZE | FS_XFLAG_EXTSZINHERIT)) ?
		    fsx.fsx_extsize : 0;
	else
		zpc->zpc_expected_projid = fsx.fsx_projid;

	return (ret);
}

/*
 * zfs fileblocksize on one file or directory.  Returns 1 when fsx should be
 * set, 0 when there is nothing to set, and -1 on error.
 */
static int
zfs_extsize_handle_one(int fd, const char *name, zfsxattr_t *fsx,
    zfs_project_control_t *zpc)
{
	struct stat st;
	uint32_t flag, size;

	if (fstat(fd, &st) != 0) {
		(void) fprintf(stderr, "failed to stat %s: %s\n",
		    name, strerror(errno));
		return (-1);
	}
	flag = S_ISDIR(st.st_mode) ? FS_XFLAG_EXTSZINHERIT : FS_XFLAG_EXTSIZE;
	size = (fsx->fsx_xflags & flag) ? fsx->fsx_extsize : 0;

	switch (zpc->zpc_op) {
	case ZFS_PROJECT_OP_LIST:
		(void) printf("%7u %c %s\n", size,
		    (fsx->fsx_xflags & FS_XFLAG_EXTSZINHERIT) ? 'I' : '-',
		    name);
		return (0);
	case ZFS_PROJECT_OP_CHECK:
		if (size == zpc->zpc_expected_extsize)
			return (0);
		if (!zpc->zpc_newline)
			(void) printf("%s%c", name, '\0');
		else
			(void) printf("%s - block size is not set properly "
			    "(%u/%u)\n", name, size,
			    zpc->zpc_expected_extsize);
		return (0);
	case ZFS_PROJECT_OP_CLEAR:
		if (size == 0)
			return (0);
		fsx->fsx_xflags &= ~(FS_XFLAG_EXTSIZE | FS_XFLAG_EXTSZINHERIT);
		fsx->fsx_extsize = 0;
		return (1);
	case ZFS_PROJECT_OP_SET:
		if (size == zpc->zpc_expected_extsize)
			return (0);
		/*
		 * A file with data can't change.  Below the top
		 * (zpc_ignore_noent), skip it; -c lists it.
		 */
		if (S_ISREG(st.st_mode) && st.st_size != 0) {
			if (zpc->zpc_ignore_noent)
				return (0);
			(void) fprintf(stderr, "cannot set the block size of "
			    "%s: the file already has data\n", name);
			return (-1);
		}
		fsx->fsx_xflags &= ~(FS_XFLAG_EXTSIZE | FS_XFLAG_EXTSZINHERIT);
		fsx->fsx_xflags |= flag;
		fsx->fsx_extsize = zpc->zpc_expected_extsize;
		return (1);
	default:
		ASSERT(0);
		return (-1);
	}
}

static int
zfs_project_handle_one(int fd, const char *name, zfs_project_control_t *zpc)
{
	zfsxattr_t fsx;
	struct stat st;
	int ret, err;

	ret = ioctl(fd, ZFS_IOC_FSGETXATTR, &fsx);
	if (ret) {
		(void) fprintf(stderr,
		    "failed to get xattr for %s: %s\n",
		    name, strerror(errno));
		return (ret);
	}

	if (zpc->zpc_extsize) {
		ret = zfs_extsize_handle_one(fd, name, &fsx, zpc);
		if (ret <= 0)
			return (ret);
		goto set;
	}

	switch (zpc->zpc_op) {
	case ZFS_PROJECT_OP_LIST:
		(void) printf("%5u %c %s\n", fsx.fsx_projid,
		    (fsx.fsx_xflags & FS_XFLAG_PROJINHERIT) ? 'P' : '-', name);
		return (0);
	case ZFS_PROJECT_OP_CHECK:
		if (fsx.fsx_projid == zpc->zpc_expected_projid &&
		    fsx.fsx_xflags & FS_XFLAG_PROJINHERIT)
			return (0);

		if (!zpc->zpc_newline) {
			char c = '\0';

			(void) printf("%s%c", name, c);
			return (0);
		}

		if (fsx.fsx_projid != zpc->zpc_expected_projid)
			(void) printf("%s - project ID is not set properly "
			    "(%u/%u)\n", name, fsx.fsx_projid,
			    (uint32_t)zpc->zpc_expected_projid);

		if (!(fsx.fsx_xflags & FS_XFLAG_PROJINHERIT))
			(void) printf("%s - project inherit flag is not set\n",
			    name);

		return (0);
	case ZFS_PROJECT_OP_CLEAR:
		if (!(fsx.fsx_xflags & FS_XFLAG_PROJINHERIT) &&
		    (zpc->zpc_keep_projid ||
		    fsx.fsx_projid == ZFS_DEFAULT_PROJID))
			return (0);

		fsx.fsx_xflags &= ~FS_XFLAG_PROJINHERIT;
		if (!zpc->zpc_keep_projid)
			fsx.fsx_projid = ZFS_DEFAULT_PROJID;
		break;
	case ZFS_PROJECT_OP_SET:
		if (fsx.fsx_projid == zpc->zpc_expected_projid &&
		    (!zpc->zpc_set_flag ||
		    fsx.fsx_xflags & FS_XFLAG_PROJINHERIT))
			return (0);

		fsx.fsx_projid = zpc->zpc_expected_projid;
		if (zpc->zpc_set_flag)
			fsx.fsx_xflags |= FS_XFLAG_PROJINHERIT;
		break;
	default:
		ASSERT(0);
		break;
	}

set:
	ret = ioctl(fd, ZFS_IOC_FSSETXATTR, &fsx);
	if (ret) {
		err = errno;
		/*
		 * Below the top, a file written to since it was checked can't
		 * take a block size any more; skip it, as if seen with data.
		 */
		if (zpc->zpc_extsize && zpc->zpc_ignore_noent &&
		    err == EINVAL && zpc->zpc_op == ZFS_PROJECT_OP_SET &&
		    fstat(fd, &st) == 0 && S_ISREG(st.st_mode) &&
		    st.st_size != 0)
			return (0);
		(void) fprintf(stderr,
		    "failed to set xattr for %s: %s\n",
		    name, strerror(err));

		if (err == ENOTSUP && zpc->zpc_extsize)
			(void) fprintf(stderr, "The dataset's fileblocksize "
			    "property may be off, the pool may lack the "
			    "project_quota feature, or the file may predate "
			    "system attributes.\n");
		if (err == ENOTSUP) {
			char *kver = zfs_version_kernel();
			/*
			 * Special case: a module/userspace version mismatch can
			 * return ENOTSUP due to us fixing the XFLAGs bits in
			 * #17884.  In that case give a hint to the user that
			 * they should take action to make the versions match.
			 */
			if (strcmp(kver, ZFS_META_ALIAS) != 0) {
				fprintf(stderr,
				    "Warning: The zfs module version "
				    "(%s) and userspace\nversion (%s) do not "
				    "match up.  This may be the\ncause of the "
				    "\"Operation not supported\" error.\n",
				    kver, ZFS_META_ALIAS);
			}
		}
	}

	return (ret);
}

/* Errors that mean an entry changed after readdir(), or is no file. */
static boolean_t
zfs_project_skip_errno(int err)
{
	/* FreeBSD gives EMLINK for O_NOFOLLOW on a link, EOPNOTSUPP sockets */
	return (err == ENOENT || err == ENOTDIR || err == ELOOP ||
	    err == EMLINK || err == ENXIO || err == EOPNOTSUPP);
}

/*
 * Open a queued directory below the top without following a link: from its
 * parent if that is still open, otherwise from the top one component at a
 * time.  Then check that it is the directory that was queued.  Returns -1
 * with errno ENOENT if it is gone, ESTALE if something else is there now.
 */
static int
zfs_project_open_queued(zfs_project_walk_t *zpw, zfs_project_item_t *zpi)
{
	const int flags = O_RDONLY | O_NOCTTY | O_DIRECTORY | O_NOFOLLOW |
	    O_NONBLOCK;
	const char *rel = zpi->zpi_name + zpw->zpw_toplen;
	const char *base;
	struct stat st;
	char *path, *comp, *last;
	int fd = -1, nfd, i;

	while (*rel == '/')
		rel++;
	base = strrchr(rel, '/');
	base = base == NULL ? rel : base + 1;
	for (i = 0; i < ZFS_PROJECT_DIRCACHE && *rel != '\0'; i++) {
		if (zpw->zpw_dirs[i].fd >= 0 &&
		    zpw->zpw_dirs[i].id == zpi->zpi_parent) {
			fd = openat(zpw->zpw_dirs[i].fd, base, flags);
			goto check;
		}
	}

	fd = openat(zpw->zpw_topfd, ".", O_RDONLY | O_NOCTTY | O_DIRECTORY);
	if (fd < 0)
		return (-1);
	path = safe_malloc(strlen(rel) + 1);
	strcpy(path, rel);
	for (comp = strtok_r(path, "/", &last); comp != NULL;
	    comp = strtok_r(NULL, "/", &last)) {
		nfd = openat(fd, comp, flags);
		(void) close(fd);
		fd = nfd;
		if (fd < 0)
			break;
	}
	free(path);

check:
	if (fd < 0) {
		if (errno != ENOENT && zfs_project_skip_errno(errno))
			errno = ESTALE;
		return (-1);
	}
	if (fstat(fd, &st) != 0 || st.st_dev != zpi->zpi_dev ||
	    st.st_ino != zpi->zpi_ino) {
		(void) close(fd);
		errno = ESTALE;
		return (-1);
	}
	return (fd);
}

/* Keep a read directory open for its subdirectories. */
static void
zfs_project_cache_dir(zfs_project_walk_t *zpw, zfs_project_item_t *zpi, int fd)
{
	uint_t slot = zpw->zpw_next_slot++ % ZFS_PROJECT_DIRCACHE;
	int cfd = dup(fd);

	if (zpw->zpw_dirs[slot].fd >= 0)
		(void) close(zpw->zpw_dirs[slot].fd);
	zpw->zpw_dirs[slot].fd = cfd;
	zpw->zpw_dirs[slot].id = zpi->zpi_id;
}

/*
 * Read one queued directory.  A file or directory that can't be handled
 * because of something done to it during the walk is skipped with a
 * message and noted in *skipped, so that the walk fails at its end.
 */
static int
zfs_project_handle_dir(zfs_project_walk_t *zpw, zfs_project_item_t *zpi,
    zfs_project_control_t *zpc, list_t *head, int *skipped)
{
	const char *name = zpi->zpi_name;
	struct dirent *ent;
	struct stat st;
	DIR *dir = NULL;
	int ret = 0;
	int fd;

	fd = zfs_project_open_queued(zpw, zpi);
	if (fd >= 0 && (dir = fdopendir(fd)) == NULL)
		(void) close(fd);
	if (dir == NULL) {
		if (zpc->zpc_ignore_noent && errno == ENOENT)
			return (0);
		if (zpc->zpc_ignore_noent && errno == ESTALE) {
			(void) fprintf(stderr, "skipped %s: it was replaced "
			    "during the walk\n", name);
			*skipped = -ESTALE;
			return (0);
		}

		ret = -errno;
		(void) fprintf(stderr, "failed to opendir %s: %s\n",
		    name, strerror(errno));
		return (ret);
	}
	if (zpc->zpc_recursive)
		zfs_project_cache_dir(zpw, zpi, dirfd(dir));

	/* Non-top item, ignore the case of being removed or renamed by race. */
	zpc->zpc_ignore_noent = B_TRUE;
	/* Skipped entries may leave errno set; only readdir()'s counts. */
	while (!ret && (errno = 0, ent = readdir(dir)) != NULL) {
		char *fullname;
		int cfd;

		/* skip "." and ".." */
		if (strcmp(ent->d_name, ".") == 0 ||
		    strcmp(ent->d_name, "..") == 0)
			continue;

		/* Only regular files and directories have project IDs. */
		if (ent->d_type != DT_REG && ent->d_type != DT_DIR &&
		    (ent->d_type != DT_UNKNOWN ||
		    fstatat(dirfd(dir), ent->d_name, &st,
		    AT_SYMLINK_NOFOLLOW) != 0 ||
		    !(S_ISREG(st.st_mode) || S_ISDIR(st.st_mode))))
			continue;

		if (strlen(ent->d_name) + strlen(name) + 1 >= PATH_MAX) {
			errno = ENAMETOOLONG;
			break;
		}

		if (asprintf(&fullname, "%s/%s", name, ent->d_name) == -1) {
			errno = ENOMEM;
			break;
		}

		/*
		 * Open it here, not by its path, without following a link or
		 * waiting on a FIFO swapped in since readdir().  A file with a
		 * lease (EAGAIN) is skipped with a message, not waited for.
		 */
		cfd = openat(dirfd(dir), ent->d_name,
		    O_RDONLY | O_NOCTTY | O_NOFOLLOW | O_NONBLOCK);
		if (cfd < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				(void) fprintf(stderr, "skipped %s: %s\n",
				    fullname, strerror(errno));
				*skipped = -errno;
			} else if (!zfs_project_skip_errno(errno)) {
				ret = -errno;
				(void) fprintf(stderr, "failed to open %s: "
				    "%s\n", fullname, strerror(errno));
			}
			free(fullname);
			continue;
		}
		if (fstat(cfd, &st) != 0 ||
		    !(S_ISREG(st.st_mode) || S_ISDIR(st.st_mode))) {
			(void) close(cfd);
			free(fullname);
			continue;
		}

		ret = zfs_project_handle_one(cfd, fullname, zpc);
		(void) close(cfd);
		if (!ret && zpc->zpc_recursive && S_ISDIR(st.st_mode))
			zfs_project_item_alloc(head, zpw, zpi->zpi_id,
			    fullname, &st);

		free(fullname);
	}

	if (errno && !ret) {
		ret = -errno;
		(void) fprintf(stderr, "failed to readdir %s: %s\n",
		    name, strerror(errno));
	}

	closedir(dir);
	return (ret);
}

int
zfs_project_handle(const char *name, zfs_project_control_t *zpc)
{
	zfs_project_walk_t zpw;
	zfs_project_item_t *zpi;
	struct stat st;
	list_t head;
	int ret, fd, i, skipped = 0;

	ret = zfs_project_sanity_check(name, zpc, &st);
	if (ret)
		return (ret);

	fd = zfs_project_open_top(name, &st);
	if (fd < 0)
		return (fd);

	if (zpc->zpc_extsize ? (zpc->zpc_op == ZFS_PROJECT_OP_CHECK &&
	    zpc->zpc_expected_extsize == ZFS_EXTSIZE_FROM_TARGET) :
	    ((zpc->zpc_op == ZFS_PROJECT_OP_SET ||
	    zpc->zpc_op == ZFS_PROJECT_OP_CHECK) &&
	    zpc->zpc_expected_projid == ZFS_INVALID_PROJID)) {
		ret = zfs_project_load_projid(fd, name, zpc);
		if (ret)
			goto out;
	}

	zpc->zpc_ignore_noent = B_FALSE;
	ret = zfs_project_handle_one(fd, name, zpc);
	if (ret || !S_ISDIR(st.st_mode) || zpc->zpc_dironly ||
	    (!zpc->zpc_recursive &&
	    zpc->zpc_op != ZFS_PROJECT_OP_LIST &&
	    zpc->zpc_op != ZFS_PROJECT_OP_CHECK))
		goto out;

	zpw.zpw_topfd = fd;
	zpw.zpw_toplen = strlen(name);
	zpw.zpw_next_id = 0;
	zpw.zpw_next_slot = 0;
	for (i = 0; i < ZFS_PROJECT_DIRCACHE; i++)
		zpw.zpw_dirs[i].fd = -1;
	list_create(&head, sizeof (zfs_project_item_t),
	    offsetof(zfs_project_item_t, zpi_list));
	zfs_project_item_alloc(&head, &zpw, 0, name, &st);
	while ((zpi = list_remove_head(&head)) != NULL) {
		if (!ret)
			ret = zfs_project_handle_dir(&zpw, zpi, zpc, &head,
			    &skipped);
		free(zpi);
	}
	for (i = 0; i < ZFS_PROJECT_DIRCACHE; i++)
		if (zpw.zpw_dirs[i].fd >= 0)
			(void) close(zpw.zpw_dirs[i].fd);
	/* A skipped file or directory fails the walk, but at its end. */
	if (ret == 0)
		ret = skipped;

out:
	close(fd);
	return (ret);
}
