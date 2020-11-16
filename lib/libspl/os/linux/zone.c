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

#include <stdio.h>
#include <stdlib.h>
#include <zone.h>

zoneid_t
getzoneid()
{
	FILE *fptr;
	int len;
	zoneid_t zone_id;
	if ((fptr = fopen("/proc/spl/curzone", "r")) == NULL) {
		return GLOBAL_ZONEID;
	}

	len = fscanf(fptr, "%d\n", &zone_id);
	fclose(fptr);

	if (!len || (len < 0))
		return GLOBAL_ZONEID;
	return zone_id;
}
