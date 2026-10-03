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

#ifndef _LIBSPL_SYS_FCNTL_H
#define	_LIBSPL_SYS_FCNTL_H

#include_next <sys/fcntl.h>

#define	O_LARGEFILE	0
#define	O_RSYNC	0

#ifndef O_DIRECT
#define	O_DIRECT 0
#endif

#include <TargetConditionals.h>
#include <AvailabilityMacros.h>

#if !defined(MAC_OS_X_VERSION_10_10) || \
	(MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_X_VERSION_10_10)
#define	AT_FDCWD -2
#include <stdio.h>
#include <stdarg.h>
#include <sys/syslimits.h>
static inline int
openat(int fd, const char *path, int oflag, ...)
{
	va_list arg;
	mode_t mode = 0;
	char dir[PATH_MAX], fullpath[PATH_MAX];
	if (oflag & O_CREAT) {
		va_start(arg, oflag);
		mode = va_arg(arg, mode_t);
		va_end(arg);
	}
	if (fd == AT_FDCWD || path[0] == '/')
		return (open(path, oflag, mode));
	if (fcntl(fd, F_GETPATH, dir) == -1)
		return (-1);
	snprintf(fullpath, sizeof (fullpath), "%s/%s", dir, path);
	return (open(fullpath, oflag, mode));
}

#include <dirent.h>
static DIR *
fdopendir(int fd)
{
	char dir[PATH_MAX];
	if (fcntl(fd, F_GETPATH, dir) == -1)
		return (NULL);
	return (opendir(dir));
}
#endif

#endif
