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

#ifndef _LIBSPL_SYS_TIME_H
#define	_LIBSPL_SYS_TIME_H

#include_next <sys/time.h>
#include <sys/types.h>

#ifndef SEC
#define	SEC		1
#endif

#ifndef MILLISEC
#define	MILLISEC	1000
#endif

#ifndef MICROSEC
#define	MICROSEC	1000000
#endif

#ifndef NANOSEC
#define	NANOSEC		1000000000
#endif

#ifndef NSEC_PER_USEC
#define	NSEC_PER_USEC	1000L
#endif

#ifndef MSEC2NSEC
#define	MSEC2NSEC(m)    ((hrtime_t)(m) * (NANOSEC / MILLISEC))
#endif

#ifndef NSEC2MSEC
#define	NSEC2MSEC(n)    ((n) / (NANOSEC / MILLISEC))
#endif

extern hrtime_t gethrtime(void);
extern void gethrestime(timestruc_t *);

#endif /* _LIBSPL_SYS_TIME_H */
