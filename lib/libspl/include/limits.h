/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include_next <limits.h>
#include <float.h>

#ifndef _LIBSPL_LIMITS_H
#define	_LIBSPL_LIMITS_H

#ifndef DBL_DIG
#define	DBL_DIG		15
#define	DBL_MAX		1.7976931348623157081452E+308
#define	DBL_MIN		2.2250738585072013830903E-308
#endif

#ifndef FLT_DIG
#define	FLT_DIG		6
#define	FLT_MAX		3.4028234663852885981170E+38F
#define	FLT_MIN		1.1754943508222875079688E-38F
#endif

#endif /* _LIBSPL_LIMITS_H */
