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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SOL_SYS_INTTYPES_H
#define	_SOL_SYS_INTTYPES_H

#ifdef __NetBSD__
/*
 * NetBSD <sys/param.h> includes <sys/inttypes.h>, which must provide
 * intmax_t via <sys/stdint.h>.  Our wrapper must not shadow that with
 * a recursive <inttypes.h> include.
 */
#include_next <sys/inttypes.h>
#include <inttypes.h>
#else
#include <inttypes.h>
#endif

#define	_INT64_TYPE

#endif
