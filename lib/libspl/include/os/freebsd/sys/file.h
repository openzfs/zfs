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
/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _LIBSPL_SYS_FILE_H
#define	_LIBSPL_SYS_FILE_H

#include_next <sys/file.h>

#define	FCREAT	O_CREAT
#define	FTRUNC	O_TRUNC
#define	FSYNC	O_SYNC
#define	FDSYNC	O_DSYNC
#define	FEXCL	O_EXCL

#define	FNODSYNC	0x10000	/* fsync pseudo flag */
#define	FNOFOLLOW	0x20000	/* don't follow symlinks */
#define	FIGNORECASE	0x80000	/* request case-insensitive lookups */

#endif
