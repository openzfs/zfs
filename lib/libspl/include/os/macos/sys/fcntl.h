/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
	(MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_10)

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
