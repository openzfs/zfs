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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 * Common routines for acquiring snapshots of kstats for
 * iostat, mpstat, and vmstat.
 */

#ifndef	_STATCOMMON_H
#define	_STATCOMMON_H

#include <sys/types.h>

#define	NODATE	0	/* Default:  No time stamp */
#define	DDATE	1	/* Standard date format */
#define	UDATE	2	/* Internal representation of Unix time */

/* Print a timestamp in either Unix or standard format. */
void print_timestamp(uint_t);
/* Return timestamp in either Unix or standard format in provided buffer */
void get_timestamp(uint_t, char *, int);
/* convert time_t to standard format */
void format_timestamp(time_t, char *, int);

#endif /* _STATCOMMON_H */
