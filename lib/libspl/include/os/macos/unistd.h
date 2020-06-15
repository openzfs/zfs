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

#endif
