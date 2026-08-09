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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include_next <unistd.h>

#ifndef _LIBSPL_UNISTD_H
#define	_LIBSPL_UNISTD_H

#include <sys/ioctl.h>

#if !defined(HAVE_ISSETUGID)
#include <sys/types.h>
#define	issetugid() (geteuid() == 0 || getegid() == 0)
#endif

#endif /* _LIBSPL_UNISTD_H */
