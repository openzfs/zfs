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
/* We can not use Windows' stat.h versions, they are too small */
#undef S_IFMT
#undef S_IFCHR
#undef S_IFDIR
#undef S_IFREG

#define	S_IFMT		0170000 /* [XSI] type of file mask */
#define	S_IFIFO		0010000 /* [XSI] named pipe (fifo) */
#define	S_IFCHR		0020000 /* [XSI] character special */
#define	S_IFDIR		0040000 /* [XSI] directory */
#define	S_IFBLK		0060000 /* [XSI] block special */
#define	S_IFREG		0100000 /* [XSI] regular */
#define	S_IFLNK		0120000 /* [XSI] symbolic link */
#define	S_IFSOCK	0140000 /* [XSI] socket */
#if !defined(_POSIX_C_SOURCE)
#define	S_IFWHT		0160000 /* OBSOLETE: whiteout */
#endif
/* File mode */
/* Read, write, execute/search by owner */
#define	S_IRWXU		0000700 /* [XSI] RWX mask for owner */
#define	S_IRUSR		0000400 /* [XSI] R for owner */
#define	S_IWUSR		0000200 /* [XSI] W for owner */
#define	S_IXUSR		0000100 /* [XSI] X for owner */
/* Read, write, execute/search by group */
#define	S_IRWXG		0000070 /* [XSI] RWX mask for group */
#define	S_IRGRP		0000040 /* [XSI] R for group */
#define	S_IWGRP		0000020 /* [XSI] W for group */
#define	S_IXGRP		0000010 /* [XSI] X for group */
/* Read, write, execute/search by others */
#define	S_IRWXO		0000007 /* [XSI] RWX mask for other */
#define	S_IROTH		0000004 /* [XSI] R for other */
#define	S_IWOTH		0000002 /* [XSI] W for other */
#define	S_IXOTH		0000001 /* [XSI] X for other */

#define	S_ISUID		0004000 /* [XSI] set user id on execution */
#define	S_ISGID		0002000 /* [XSI] set group id on execution */
#define	S_ISVTX		0001000 /* [XSI] directory restrcted delete */

#if !defined(_POSIX_C_SOURCE)
#ifndef S_ISTXT
#define	S_ISTXT		S_ISVTX /* sticky bit: not supported */
#endif
#ifndef S_IREAD
#define	S_IREAD		S_IRUSR /* backward compatability */
#endif
#ifndef S_IWRITE
#define	S_IWRITE	S_IWUSR /* backward compatability */
#endif
#ifndef S_IEXEC
#define	S_IEXEC		S_IXUSR /* backward compatability */
#endif
#endif

/*
 * File type macros.  Note that block devices, sockets and links cannot be
 * distinguished on Windows and the macros S_ISBLK, S_ISSOCK and S_ISLNK are
 * only defined for compatibility.  These macros should always return false
 * on Windows.
 */
#define	S_ISFIFO(mode) (((mode) & S_IFMT) == S_IFIFO)
#define	S_ISDIR(mode)  (((mode) & S_IFMT) == S_IFDIR)
#define	S_ISREG(mode)  (((mode) & S_IFMT) == S_IFREG)
#define	S_ISLNK(mode)  (((mode) & S_IFMT) == S_IFLNK)
#define	S_ISSOCK(mode) (((mode) & S_IFMT) == S_IFSOCK)
#define	S_ISCHR(mode)  (((mode) & S_IFMT) == S_IFCHR)
#define	S_ISBLK(mode)  (((mode) & S_IFMT) == S_IFBLK)

#endif /* _LIBSPL_SYS_STAT_H */
