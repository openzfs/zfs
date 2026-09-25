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

#ifndef _LIBSPL_WINDOWS_SYS_FCNTL_H
#define	_LIBSPL_WINDOWS_SYS_FCNTL_H

#include_next <fcntl.h>

#define	O_LARGEFILE	0
#define	O_RSYNC		0
#define	O_DIRECT	0x100000 // Let's hope it is spare.
#define	O_EXLOCK	0x200000 // ditto
#define	O_SYNC		0
#define	O_DSYNC		0
#define	O_CLOEXEC	0
#define	O_NDELAY	0
#define	O_NONBLOCK	0
#define	O_NOCTTY	0

#define	F_SETFD		2
#define	FD_CLOEXEC	1

#define	O_DIRECTORY	0x1000000

/*
 * Special value used to indicate openat should use the current
 * working directory.
 */
#define	AT_FDCWD		-100

/* regular version, for both small and large file compilation environment */
typedef struct flock {
	short   l_type;
	short   l_whence;
	unsigned long long l_start;
	unsigned long long l_len; /* len == 0 means until end of file */
	int l_sysid;
	unsigned int l_pid;
	long    l_pad[4]; /* reserve area */
} flock_t;

/*
 * File segment locking types.
 */
#define	F_RDLCK		01 /* Read lock */
#define	F_WRLCK		02 /* Write lock */
#define	F_UNLCK		03 /* Remove lock(s) */
#define	F_UNLKSYS	04 /* remove remote locks for a given system */

#define	F_SETLK		6  /* Set file lock */
#define	F_SETLKW	7  /* Set file lock and wait */
#define	F_GETLK		14 /* Get file lock */

extern int fcntl(int fildes, int cmd, /* arg */ ...);

#endif /* _LIBSPL_SYS_FCNTL_H */
