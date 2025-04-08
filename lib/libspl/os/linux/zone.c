// SPDX-License-Identifier: CDDL-1.0
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
 * Copyright 2006 Ricardo Correia.  All rights reserved.
 * Use is subject to license terms.
 */

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>

#include <zone.h>

zoneid_t
getzoneid(void)
{
	char path[PATH_MAX];
	char buf[128] = { '\0' };
	char *cp;

	int c = snprintf(path, sizeof (path), "/proc/self/ns/user");
	/* This API doesn't have any error checking... */
	if (c < 0 || c >= sizeof (path))
		return (GLOBAL_ZONEID);

	ssize_t r = readlink(path, buf, sizeof (buf) - 1);
	if (r < 0)
		return (GLOBAL_ZONEID);

	cp = strchr(buf, '[');
	if (cp == NULL)
		return (GLOBAL_ZONEID);
	cp++;

	unsigned long n = strtoul(cp, NULL, 10);
	if (n == ULONG_MAX && errno == ERANGE)
		return (GLOBAL_ZONEID);
	zoneid_t z = (zoneid_t)n;

	return (z);
}
