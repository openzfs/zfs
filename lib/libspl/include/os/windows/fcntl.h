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
