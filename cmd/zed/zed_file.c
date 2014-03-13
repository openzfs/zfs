/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license from the top-level
 * OPENSOLARIS.LICENSE or <http://opensource.org/licenses/CDDL-1.0>.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each file
 * and include the License file from the top-level OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Developed at Lawrence Livermore National Laboratory (LLNL-CODE-403049).
 * Copyright (C) 2013-2014 Lawrence Livermore National Security, LLC.
 */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "zed_log.h"

/*
 * Read up to [n] bytes from [fd] into [buf].
 * Return the number of bytes read, 0 on EOF, or -1 on error.
 */
ssize_t
zed_file_read_n(int fd, void *buf, size_t n)
{
	unsigned char *p;
	size_t n_left;
	ssize_t n_read;

	p = buf;
	n_left = n;
	while (n_left > 0) {
		if ((n_read = read(fd, p, n_left)) < 0) {
			if (errno == EINTR)
				continue;
			else
				return (-1);

		} else if (n_read == 0) {
			break;
		}
		n_left -= n_read;
		p += n_read;
	}
	return (n - n_left);
}

/*
 * Write [n] bytes from [buf] out to [fd].
 * Return the number of bytes written, or -1 on error.
 */
ssize_t
zed_file_write_n(int fd, void *buf, size_t n)
{
	const unsigned char *p;
	size_t n_left;
	ssize_t n_written;

	p = buf;
	n_left = n;
	while (n_left > 0) {
		if ((n_written = write(fd, p, n_left)) < 0) {
			if (errno == EINTR)
				continue;
			else
				return (-1);

		}
		n_left -= n_written;
		p += n_written;
	}
	return (n);
}

/*
 * Set an exclusive advisory lock on the open file descriptor [fd].
 * Return 0 on success, 1 if a conflicting lock is held by another process,
 *   or -1 on error (with errno set).
 */
int
zed_file_lock(int fd)
{
	struct flock lock;

	if (fd < 0) {
		errno = EBADF;
		return (-1);
	}
	lock.l_type = F_WRLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 0;

	if (fcntl(fd, F_SETLK, &lock) < 0) {
		if ((errno == EACCES) || (errno == EAGAIN))
			return (1);

		return (-1);
	}
	return (0);
}

/*
 * Release an advisory lock held on the open file descriptor [fd].
 * Return 0 on success, or -1 on error (with errno set).
 */
int
zed_file_unlock(int fd)
{
	struct flock lock;

	if (fd < 0) {
		errno = EBADF;
		return (-1);
	}
	lock.l_type = F_UNLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 0;

	if (fcntl(fd, F_SETLK, &lock) < 0)
		return (-1);

	return (0);
}

/*
 * Test whether an exclusive advisory lock could be obtained for the open
 *   file descriptor [fd].
 * Return 0 if the file is not locked, >0 for the pid of another process
 *   holding a conflicting lock, or -1 on error (with errno set).
 */
pid_t
zed_file_is_locked(int fd)
{
	struct flock lock;

	if (fd < 0) {
		errno = EBADF;
		return (-1);
	}
	lock.l_type = F_WRLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 0;

	if (fcntl(fd, F_GETLK, &lock) < 0)
		return (-1);

	if (lock.l_type == F_UNLCK)
		return (0);

	return (lock.l_pid);
}

/*
 * Close all open file descriptors greater than or equal to [lowfd].
 * Any errors encountered while closing file descriptors are ignored.
 */
void
zed_file_close_from(int lowfd)
{
	const int maxfd_def = 256;
	int errno_bak;
	struct rlimit rl;
	int maxfd;
	int fd;

	errno_bak = errno;

	if (getrlimit(RLIMIT_NOFILE, &rl) < 0) {
		maxfd = maxfd_def;
	} else if (rl.rlim_max == RLIM_INFINITY) {
		maxfd = maxfd_def;
	} else {
		maxfd = rl.rlim_max;
	}
	for (fd = lowfd; fd < maxfd; fd++)
		(void) close(fd);

	errno = errno_bak;
}

/*
 * Set the CLOEXEC flag on file descriptor [fd] so it will be automatically
 *   closed upon successful execution of one of the exec functions.
 * Return 0 on success, or -1 on error.
 * FIXME: No longer needed?
 */
int
zed_file_close_on_exec(int fd)
{
	int flags;

	if (fd < 0) {
		errno = EBADF;
		return (-1);
	}
	flags = fcntl(fd, F_GETFD);
	if (flags == -1)
		return (-1);

	flags |= FD_CLOEXEC;

	if (fcntl(fd, F_SETFD, flags) == -1)
		return (-1);

	return (0);
}

/*
 * Create the directory [dir_name] and any missing parent directories.
 *   Directories will be created with permissions 0755 modified by the umask.
 * Return 0 on success, or -1 on error.
 * FIXME: Deprecate in favor of mkdirp(). (lib/libspl/mkdirp.c)
 */
int
zed_file_create_dirs(const char *dir_name)
{
	struct stat st;
	char dir_buf[PATH_MAX];
	mode_t dir_mode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
	char *p;

	if ((dir_name == NULL) || (dir_name[0] == '\0')) {
		zed_log_msg(LOG_WARNING,
		    "Failed to create directory: no directory specified");
		errno = EINVAL;
		return (-1);
	}
	if (dir_name[0] != '/') {
		zed_log_msg(LOG_WARNING,
		    "Failed to create directory \"%s\": not absolute path",
		    dir_name);
		errno = EINVAL;
		return (-1);
	}
	/* Check if directory already exists. */
	if (stat(dir_name, &st) == 0) {
		if (S_ISDIR(st.st_mode))
			return (0);

		errno = EEXIST;
		zed_log_msg(LOG_WARNING,
		    "Failed to create directory \"%s\": %s",
		    dir_name, strerror(errno));
		return (-1);
	}
	/* Create copy for modification. */
	if (strlen(dir_name) >= sizeof (dir_buf)) {
		errno = ENAMETOOLONG;
		zed_log_msg(LOG_WARNING,
		    "Failed to create directory \"%s\": %s",
		    dir_name, strerror(errno));
		return (-1);
	}
	strncpy(dir_buf, dir_name, sizeof (dir_buf));

	/* Remove trailing slashes. */
	p = dir_buf + strlen(dir_buf) - 1;
	while ((p > dir_buf) && (*p == '/'))
		*p-- = '\0';

	/* Process directory components starting from the root dir. */
	p = dir_buf;

	while (1) {

		/* Skip over adjacent slashes. */
		while (*p == '/')
			p++;

		/* Advance to the next path component. */
		p = strchr(p, '/');
		if (p != NULL)
			*p = '\0';

		/* Create directory. */
		if (mkdir(dir_buf, dir_mode) < 0) {

			int mkdir_errno = errno;

			if ((mkdir_errno == EEXIST) ||
			    (stat(dir_buf, &st) < 0) ||
			    (!S_ISDIR(st.st_mode))) {
				zed_log_msg(LOG_WARNING,
				    "Failed to create directory \"%s\": %s",
				    dir_buf, strerror(mkdir_errno));
				return (-1);
			}
		}
		if (p == NULL)
			break;

		*p++ = '/';
	}
	return (0);
}
