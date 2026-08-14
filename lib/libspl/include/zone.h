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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _LIBSPL_ZONE_H
#define	_LIBSPL_ZONE_H

#include <sys/types.h>
#include <sys/zone.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Linux user namespaces: hardcoded root userns inode.  NetBSD and
 * FreeBSD have no user namespaces; getzoneid() is always 0.
 */
#if defined(__FreeBSD__) || defined(__NetBSD__)
#define	GLOBAL_ZONEID	0
#else
#define	GLOBAL_ZONEID	4026531837U
#endif

extern zoneid_t		getzoneid(void);

#ifdef	__cplusplus
}
#endif

#endif /* _LIBSPL_ZONE_H */
