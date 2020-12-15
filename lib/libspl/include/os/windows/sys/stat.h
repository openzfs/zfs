


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

#ifndef _LIBSPL_WINDOWS_SYS_STAT_H
#define	_LIBSPL_WINDOWS_SYS_STAT_H

#define	NOWOSIXYET
#include_next <sys/stat.h>
#undef NOWOSIXYET
#include <sys/types.h>

/* File type and permission flags for stat() */
#if defined(_MSC_VER)  &&  !defined(S_IREAD)
# define S_IFMT   _S_IFMT                      /* file type mask */
# define S_IFDIR  _S_IFDIR                     /* directory */
# define S_IFCHR  _S_IFCHR                     /* character device */
# define S_IFFIFO _S_IFFIFO                    /* pipe */
# define S_IFREG  _S_IFREG                     /* regular file */
# define S_IREAD  _S_IREAD                     /* read permission */
# define S_IWRITE _S_IWRITE                    /* write permission */
# define S_IEXEC  _S_IEXEC                     /* execute permission */
#endif

/* So ZFS uses both S_IFIFO and S_IFFIFO */
#if defined(_S_IFIFO) && !defined(S_IFFIFO)
# define S_IFFIFO _S_IFIFO                    /* pipe */
#endif
#if defined(_S_IFIFO) && !defined(S_IFIFO)
# define S_IFIFO _S_IFIFO                    /* pipe */
#endif
# define S_IFSOCK 0

/*
* File type macros.  Note that block devices, sockets and links cannot be
* distinguished on Windows and the macros S_ISBLK, S_ISSOCK and S_ISLNK are
* only defined for compatibility.  These macros should always return false
* on Windows.
*/
#define	S_ISFIFO(mode) (((mode) & S_IFMT) == S_IFFIFO)
#define	S_ISDIR(mode)  (((mode) & S_IFMT) == S_IFDIR)
#define	S_ISREG(mode)  (((mode) & S_IFMT) == S_IFREG)
#define	S_ISLNK(mode)  (((mode) & S_IFMT) == S_IFLNK)
#define	S_ISSOCK(mode) (((mode) & S_IFMT) == S_IFSOCK)
#define	S_ISCHR(mode)  (((mode) & S_IFMT) == S_IFCHR)
#define	S_ISBLK(mode)  (((mode) & S_IFMT) == S_IFBLK)


#endif /* _LIBSPL_SYS_STAT_H */
