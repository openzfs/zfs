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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _LIBSPL_SYS_TYPES_H
#define	_LIBSPL_SYS_TYPES_H

#include <sys/isa_defs.h>
#include <sys/feature_tests.h>
#include_next <sys/types.h>
#include <sys/param.h> /* for NBBY */
#include <sys/types32.h>
#include <sys/va_list.h>

#ifndef HAVE_INTTYPES
#include <inttypes.h>

typedef enum boolean { B_FALSE, B_TRUE } boolean_t;

typedef unsigned char	uchar_t;
typedef unsigned short	ushort_t;
typedef unsigned int	uint_t;
typedef unsigned long	ulong_t;

typedef long long	longlong_t;
typedef unsigned long long u_longlong_t;
#endif /* HAVE_INTTYPES */

typedef longlong_t	offset_t;
typedef u_longlong_t	u_offset_t;
typedef u_longlong_t	len_t;
typedef longlong_t	diskaddr_t;

typedef ulong_t		pfn_t;		/* page frame number */
typedef ulong_t		pgcnt_t;	/* number of pages */
typedef long		spgcnt_t;	/* signed number of pages */

typedef longlong_t	hrtime_t;
typedef struct timespec	timestruc_t;
typedef struct timespec timespec_t;

typedef short		pri_t;

typedef int		zoneid_t;
typedef int		projid_t;

typedef int		major_t;
typedef int		minor_t;

typedef ushort_t o_mode_t; /* old file attribute type */

/*
 * Definitions remaining from previous partial support for 64-bit file
 * offsets.  This partial support for devices greater than 2gb requires
 * compiler support for long long.
 */
#ifdef _LONG_LONG_LTOH
typedef union {
	offset_t _f;    /* Full 64 bit offset value */
	struct {
		int32_t _l; /* lower 32 bits of offset value */
		int32_t _u; /* upper 32 bits of offset value */
	} _p;
} lloff_t;
#endif

#ifdef _LONG_LONG_HTOL
typedef union {
	offset_t _f;    /* Full 64 bit offset value */
	struct {
		int32_t _u; /* upper 32 bits of offset value */
		int32_t _l; /* lower 32 bits of offset value */
	} _p;
} lloff_t;
#endif

#endif
