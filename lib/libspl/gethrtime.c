/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <stdio.h>

hrtime_t
gethrtime(void)
{
	struct timespec ts;
	int rc;

	rc = clock_gettime(CLOCK_MONOTONIC, &ts);
	if (rc) {
		fprintf(stderr, "Error: clock_gettime() = %d\n", rc);
		abort();
	}

	return ((((u_int64_t)ts.tv_sec) * NANOSEC) + ts.tv_nsec);
}
