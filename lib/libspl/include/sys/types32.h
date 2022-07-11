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

#ifndef _SYS_TYPES32_H
#define	_SYS_TYPES32_H

#include <sys/inttypes.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Interoperability types for programs. Used for:
 *
 * Crossing between 32-bit and 64-bit domains.
 *
 * On disk data formats such as filesystem meta data
 * and disk label.
 *
 * Note: Applications should never include this
 *       header file.
 */
typedef	uint32_t	caddr32_t;
typedef	int32_t		daddr32_t;
typedef	int32_t		off32_t;
typedef	uint32_t	ino32_t;
typedef	int32_t		blkcnt32_t;
typedef uint32_t	fsblkcnt32_t;
typedef	uint32_t	fsfilcnt32_t;
typedef	int32_t		id32_t;
typedef	uint32_t	major32_t;
typedef	uint32_t	minor32_t;
typedef	int32_t		key32_t;
typedef	uint32_t	mode32_t;
typedef	uint32_t	uid32_t;
typedef	uint32_t	gid32_t;
typedef	uint32_t	nlink32_t;
typedef	uint32_t	dev32_t;
typedef	int32_t		pid32_t;
typedef	uint32_t	size32_t;
typedef	int32_t		ssize32_t;
typedef	int32_t		time32_t;
typedef	int32_t		clock32_t;

struct timeval32 {
	time32_t	tv_sec;		/* seconds */
	int32_t		tv_usec;	/* and microseconds */
};

typedef struct timespec32 {
	time32_t	tv_sec;		/* seconds */
	int32_t		tv_nsec;	/* and nanoseconds */
} timespec32_t;

typedef struct timespec32 timestruc32_t;

typedef	struct itimerspec32 {
	struct timespec32 it_interval;
	struct timespec32 it_value;
} itimerspec32_t;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_TYPES32_H */
