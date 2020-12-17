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

#define O_LARGEFILE             0
#define O_RSYNC                 0
#define O_DIRECT                0
#define O_SYNC                0
#define	O_DSYNC 0
#define	O_CLOEXEC 0
#define	O_NDELAY 0
#define	O_NOCTTY 0

#define	F_SETFD		2
#define	FD_CLOEXEC	1

#define AT_FDCWD		-100    /* Special value used to indicate
                                           openat should use the current
                                           working directory. */


#endif /* _LIBSPL_SYS_FCNTL_H */
