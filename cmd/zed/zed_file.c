// SPDX-License-Identifier: CDDL-1.0
/*
 * This file is part of the ZFS Event Daemon (ZED).
 *
 * Developed at Lawrence Livermore National Laboratory (LLNL-CODE-403049).
 * Copyright (C) 2013-2014 Lawrence Livermore National Security, LLC.
 * Refer to the OpenZFS git commit log for authoritative copyright attribution.
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License Version 1.0 (CDDL-1.0).
 * You can obtain a copy of the license from the top-level file
 * "OPENSOLARIS.LICENSE" or at <http://opensource.org/licenses/CDDL-1.0>.
 * You may not use this file except in compliance with the license.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "zed_file.h"
#include "zed_log.h"

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


#if __APPLE__
#define	PROC_SELF_FD "/dev/fd"
#else /* Linux-compatible layout */
#define	PROC_SELF_FD "/proc/self/fd"
#endif

/*
 * Close all open file descriptors greater than or equal to [lowfd].
 * Any errors encountered while closing file descriptors are ignored.
 */
void
zed_file_close_from(int lowfd)
{
	int errno_bak = errno;
	int maxfd = 0;
	int fd;
	DIR *fddir;
	struct dirent *fdent;

	if ((fddir = opendir(PROC_SELF_FD)) != NULL) {
		while ((fdent = readdir(fddir)) != NULL) {
			fd = atoi(fdent->d_name);
			if (fd > maxfd && fd != dirfd(fddir))
				maxfd = fd;
		}
		(void) closedir(fddir);
	} else {
		maxfd = sysconf(_SC_OPEN_MAX);
	}
	for (fd = lowfd; fd < maxfd; fd++)
		(void) close(fd);

	errno = errno_bak;
}
