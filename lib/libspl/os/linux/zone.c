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
