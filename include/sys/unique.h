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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_UNIQUE_H
#define	_SYS_UNIQUE_H

#include <sys/zfs_context.h>

#ifdef	__cplusplus
extern "C" {
#endif

/* The number of significant bits in each unique value. */
#define	UNIQUE_BITS	56

void unique_init(void);
void unique_fini(void);

/*
 * Return a new unique value (which will not be uniquified against until
 * it is unique_insert()-ed).
 */
uint64_t unique_create(void);

/* Return a unique value, which equals the one passed in if possible. */
uint64_t unique_insert(uint64_t value);

/* Indicate that this value no longer needs to be uniquified against. */
void unique_remove(uint64_t value);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_UNIQUE_H */
