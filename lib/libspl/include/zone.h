/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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

#ifdef __FreeBSD__
#define	GLOBAL_ZONEID	0
#else
/*
 * Hardcoded in the kernel's root user namespace.  A "better" way to get
 * this would be by using ioctl_ns(2), but this would need to be performed
 * recursively on NS_GET_PARENT and then NS_GET_USERNS.  Also, that's only
 * supported since Linux 4.9.
 */
#define	GLOBAL_ZONEID	4026531837U
#endif

extern zoneid_t		getzoneid(void);

#ifdef	__cplusplus
}
#endif

#endif /* _LIBSPL_ZONE_H */
