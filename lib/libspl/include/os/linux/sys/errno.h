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
