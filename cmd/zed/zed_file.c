/*
 * This file is part of the ZFS Event Daemon (ZED).
 *
 * Developed at Lawrence Livermore National Laboratory (LLNL-CODE-403049).
 * Copyright (C) 2013-2014 Lawrence Livermore National Security, LLC.
 * Refer to the ZoL git commit log for authoritative copyright attribution.
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License Version 1.0 (CDDL-1.0).
 * You can obtain a copy of the license from the top-level file
 * "OPENSOLARIS.LICENSE" or at <http://opensource.org/licenses/CDDL-1.0>.
 * You may not use this file except in compliance with the license.
 */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "zed_file.h"
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
 * or -1 on error (with errno set).
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
 * file descriptor [fd].
 * Return 0 if the file is not locked, >0 for the PID of another process
 * holding a conflicting lock, or -1 on error (with errno set).
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
 * closed upon successful execution of one of the exec functions.
 * Return 0 on success, or -1 on error.
 *
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
