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
 * Copyright 2009 Sun Microsystems, Inc. All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SPL_CALLB_H
#define	_SPL_CALLB_H

#include <sys/mutex.h>

#define	CALLB_CPR_ASSERT(cp)	ASSERT(MUTEX_HELD((cp)->cc_lockp));

typedef struct callb_cpr {
	kmutex_t *cc_lockp;
} callb_cpr_t;

#define	CALLB_CPR_INIT(cp, lockp, func, name) { \
		(cp)->cc_lockp = lockp;	\
}

#define	CALLB_CPR_SAFE_BEGIN(cp) { \
		CALLB_CPR_ASSERT(cp); \
}

#define	CALLB_CPR_SAFE_END(cp, lockp) { \
		CALLB_CPR_ASSERT(cp); \
}

#define	CALLB_CPR_EXIT(cp) { \
		ASSERT(MUTEX_HELD((cp)->cc_lockp)); \
		mutex_exit((cp)->cc_lockp); \
}


#define	CALLOUT_FLAG_ROUNDUP	0x1
#define	CALLOUT_FLAG_ABSOLUTE	0x2
#define	CALLOUT_FLAG_HRESTIME	0x4
#define	CALLOUT_FLAG_32BIT	0x8

/* Move me to more correct "sys/callo.h" file when convenient. */
#define	CALLOUT_NORMAL 1
typedef uint64_t callout_id_t;
callout_id_t timeout_generic(int, void (*)(void *), void *, hrtime_t,
    hrtime_t, int);

#endif  /* _SPL_CALLB_H */
