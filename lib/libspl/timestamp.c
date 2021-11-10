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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <stdio.h>
#include <time.h>
#include <langinfo.h>
#include "statcommon.h"

#ifndef _DATE_FMT
#ifdef D_T_FMT
#define	_DATE_FMT D_T_FMT
#else /* D_T_FMT */
#define	_DATE_FMT "%+"
#endif /* !D_T_FMT */
#endif /* _DATE_FMT */

/*
 * Print timestamp as decimal reprentation of time_t value (-T u was specified)
 * or in date(1) format (-T d was specified).
 */
void
print_timestamp(uint_t timestamp_fmt)
{
	time_t t = time(NULL);
	static char *fmt = NULL;

	/* We only need to retrieve this once per invocation */
	if (fmt == NULL)
		fmt = nl_langinfo(_DATE_FMT);

	if (timestamp_fmt == UDATE) {
		(void) printf("%lld\n", (longlong_t)t);
	} else if (timestamp_fmt == DDATE) {
		char dstr[64];
		int len;

		len = strftime(dstr, sizeof (dstr), fmt, localtime(&t));
		if (len > 0)
			(void) printf("%s\n", dstr);
	}
}
