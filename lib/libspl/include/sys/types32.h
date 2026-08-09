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
