/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
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

#ifndef _SPL_FCNTL_H
#define	_SPL_FCNTL_H

#include <sys/types.h>

#define	_CRT_DECLARE_NONSTDC_NAMES 1
#include <fcntl.h>

#define	F_FREESP	11

#define	O_LARGEFILE	0
#define	O_RSYNC		0
#define	O_DIRECT	0
#define	O_SYNC		0
#define	O_DSYNC		0
#define	O_CLOEXEC	0
#define	O_NDELAY	0

#define	F_RDLCK		1	/* shared or read lock */
#define	F_UNLCK		2	/* unlock */
#define	F_WRLCK		3	/* exclusive or write lock */
#ifdef KERNEL
#define	F_WAIT		0x010	/* Wait until lock is granted */
#define	F_FLOCK		0x020	/* Use flock(2) semantics for lock */
#define	F_POSIX		0x040	/* Use POSIX semantics for lock */
#define	F_PROV		0x080	/* Non-coalesced provisional lock */
#define	F_WAKE1_SAFE	0x100	/* its safe to only wake one waiter */
#define	F_ABORT		0x200	/* lock attempt aborted (force umount) */
#define	F_OFD_LOCK	0x400	/* Use "OFD" semantics for lock */
#endif

struct flock {
	off_t   l_start;	/* starting offset */
	off_t   l_len;		/* len = 0 means until end of file */
	pid_t   l_pid;		/* lock owner */
	short   l_type;		/* lock type: read/write, etc. */
	short   l_whence;	/* type of l_start */
};

#endif /* _SPL_FCNTL_H */
