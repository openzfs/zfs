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

#ifndef _LIBSPL_UNISTD_H
#define	_LIBSPL_UNISTD_H

/*
 * Guard before include_next.  NetBSD <unistd.h> includes <sys/types.h>,
 * which pulls libspl <sys/param.h>, which includes <unistd.h> again.
 * Defining issetugid() in that nested pass made the libc prototype
 * `int issetugid(void);` expand as a 0-arg macro (mkfile, mmap_*, …).
 */
#include_next <unistd.h>

#include <sys/ioctl.h>

/*
 * libc provides issetugid(3) on illumos, FreeBSD, and NetBSD.
 * The fallback is only for hosts configure did not detect.
 * Never wrap it on NetBSD: the libc symbol is always present
 * and a 0-arg macro breaks the system header when the include
 * cycle above races HAVE_ISSETUGID.
 */
#if !defined(HAVE_ISSETUGID) && !defined(__NetBSD__)
#include <sys/types.h>
#define	issetugid() (geteuid() == 0 || getegid() == 0)
#endif

#ifdef __NetBSD__
#ifndef lseek64
#define	lseek64		lseek
#endif
#ifndef ftruncate64
#define	ftruncate64	ftruncate
#endif
#endif

#endif /* _LIBSPL_UNISTD_H */
