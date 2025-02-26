// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _LIBSPL_SYS_TYPES_H
#define	_LIBSPL_SYS_TYPES_H

#if defined(HAVE_MAKEDEV_IN_SYSMACROS)
#include <sys/sysmacros.h>
#elif defined(HAVE_MAKEDEV_IN_MKDEV)
#include <sys/mkdev.h>
#endif

#include <sys/isa_defs.h>
#include <sys/feature_tests.h>
#include_next <sys/types.h>
#include <sys/types32.h>
#include <stdarg.h>
#include <sys/stdtypes.h>

#ifndef HAVE_INTTYPES
#include <inttypes.h>
#endif /* HAVE_INTTYPES */

typedef uint_t		zoneid_t;
typedef int		projid_t;

#include <sys/param.h> /* for NBBY */

#endif
