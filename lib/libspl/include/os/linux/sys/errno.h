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
 * Copyright 2017 Zettabyte Software, LLC.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Compiling against musl correctly points out that including sys/errno.h is
 * disallowed by the Single UNIX Specification when building in userspace, so
 * we implement a dummy header to redirect the include to the proper header.
 */
#ifndef _LIBSPL_SYS_ERRNO_H
#define	_LIBSPL_SYS_ERRNO_H

#include <errno.h>
/*
 * We'll take the unused errnos, 'EBADE' and 'EBADR' (from the Convergent
 * graveyard) to indicate checksum errors and fragmentation.
 */
#define	ECKSUM	EBADE
#define	EFRAGS	EBADR

/* Similar for ENOACTIVE */
#define	ENOTACTIVE	ENOANO

#endif /* _LIBSPL_SYS_ERRNO_H */
