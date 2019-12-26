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
 * Copyright 2006 Ricardo Correia.  All rights reserved.
 * Use is subject to license terms.
 */

#include <zone.h>
#include <string.h>
#include <errno.h>

int aok = 0;

zoneid_t
getzoneid()
{
	return (GLOBAL_ZONEID);
}

zoneid_t
getzoneidbyname(const char *name)
{
	if (name == NULL)
		return (GLOBAL_ZONEID);

	if (strcmp(name, GLOBAL_ZONEID_NAME) == 0)
		return (GLOBAL_ZONEID);

	return (EINVAL);
}

ssize_t
getzonenamebyid(zoneid_t id, char *buf, size_t buflen)
{
	if (id != GLOBAL_ZONEID)
		return (EINVAL);

	ssize_t ret = strlen(GLOBAL_ZONEID_NAME) + 1;

	if (buf == NULL || buflen == 0)
		return (ret);

	strncpy(buf, GLOBAL_ZONEID_NAME, buflen);
	buf[buflen - 1] = '\0';

	return (ret);
}
