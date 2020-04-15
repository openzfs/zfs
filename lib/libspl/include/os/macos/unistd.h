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
#ifndef _LIBSPL_UNISTD_H
#define	_LIBSPL_UNISTD_H

#include_next <unistd.h>
#include <fcntl.h>
#include <sys/param.h>

#include <TargetConditionals.h>
#include <AvailabilityMacros.h>

/* Handle Linux use of 64 names */

#define	open64		open
#define	pread64		pread
#define	pwrite64	pwrite
#define	ftruncate64	ftruncate
#define	lseek64		lseek


static inline int
fdatasync(int fd)
{
	if (fcntl(fd, F_FULLFSYNC) == -1)
		return (-1);
	return (0);
}

#ifndef _SC_PHYS_PAGES
#define	_SC_PHYS_PAGES 200
#endif

static inline int
pipe2(int fildes[2], int flags)
{
	int rv;
	int old;

	if ((rv = pipe(fildes)) != 0)
		return (rv);

	if (flags & O_NONBLOCK) {
		old = fcntl(fildes[0], F_GETFL);
		if (old >= 0)
			fcntl(fildes[0], F_SETFL, old | O_NONBLOCK);
		old = fcntl(fildes[1], F_GETFL);
		if (old >= 0)
			fcntl(fildes[1], F_SETFL, old | O_NONBLOCK);
	}
	if (flags & O_CLOEXEC) {
		old = fcntl(fildes[0], F_GETFD);
		if (old >= 0)
			fcntl(fildes[0], F_SETFD, old | FD_CLOEXEC);
		old = fcntl(fildes[1], F_GETFD);
		if (old >= 0)
			fcntl(fildes[1], F_SETFD, old | FD_CLOEXEC);
	}
	return (0);
}

#if !defined(MAC_OS_X_VERSION_10_12) || \
	(MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_12)
#define	mkostemp(template, oflag) mkstemp((template))
#define	mkostemps(template, slen, oflag) mkstemps((template), (slen))
#endif

#endif
