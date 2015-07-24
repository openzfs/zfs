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

#ifndef _SYS_FILE_H
#define	_SYS_FILE_H

#include <sys/fcntl.h>

/*
 * Prior to linux-2.6.33 only O_DSYNC semantics were implemented and
 * they used the O_SYNC flag.  As of linux-2.6.33 the this behavior
 * was properly split in to O_SYNC and O_DSYNC respectively.
 */
#ifndef O_DSYNC
#define	O_DSYNC		O_SYNC
#endif

#define	FREAD		0x01
#define	FWRITE		0x02
#define	FCREAT		O_CREAT
#define	FTRUNC		O_TRUNC
#define	FOFFMAX		O_LARGEFILE
#define	FSYNC		O_SYNC
#define	FDSYNC		O_DSYNC
#define	FRSYNC		O_SYNC
#define	FEXCL		O_EXCL
#define	FDIRECT		O_DIRECT
#define	FAPPEND		O_APPEND
#define	FNODSYNC	0x10000		/* fsync pseudo flag */
#define	FNOFOLLOW	0x20000		/* don't follow symlinks */
#define	FIGNORECASE	0x80000		/* request case-insensitive lookups */
#define	FKIOCTL		0x80000000

#endif	/* _SYS_FILE_H */
