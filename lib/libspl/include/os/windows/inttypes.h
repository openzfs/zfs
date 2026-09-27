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

#ifndef _LIBSPL_INTTYPES_H
#define	_LIBSPL_INTTYPES_H

#include_next <inttypes.h>

#define	SCNi8  "hhi"
#define	SCNi16 "hi"
#define	SCNi32 "i"
#define	SCNi64 "lli"
// #define	PRId32 "i"
#define	PRIu64 "llu"
#define	PRIx64 "llx"
#define	PRIi64 "lli"
// #define	PRId64 "lli"


#define	strtoimax strtoull
#define	strtoumax strtoull

#endif /* SPL_INTTYPES_H */
