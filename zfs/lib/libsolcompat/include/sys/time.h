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

#ifndef _SOL_SYS_TIME_H
#define _SOL_SYS_TIME_H

#include_next <sys/time.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>

typedef longlong_t hrtime_t;
typedef struct timespec timestruc_t;

static inline hrtime_t gethrtime(void) {
	struct timespec ts;

	if(clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		fprintf(stderr, "Error: clock_gettime(CLOCK_MONOTONIC) failed\n");
		fprintf(stderr, "Make sure you are are running kernel 2.6.x and have glibc 2.3.3 or newer installed\n");
		fprintf(stderr, "Aborting...\n");
		abort();
	}

	return (((u_int64_t)ts.tv_sec) * NANOSEC) + ts.tv_nsec;
}

#endif
