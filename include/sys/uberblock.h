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
 */
/*
 * Copyright (c) 2014 by Delphix. All rights reserved.
 */

#ifndef _SYS_UBERBLOCK_H
#define	_SYS_UBERBLOCK_H

#include <sys/spa.h>
#include <sys/vdev.h>
#include <sys/zio.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct uberblock uberblock_t;

extern int uberblock_verify(uberblock_t *);
extern boolean_t uberblock_update(uberblock_t *ub, vdev_t *rvd, uint64_t txg,
    uint64_t mmp_delay);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_UBERBLOCK_H */
