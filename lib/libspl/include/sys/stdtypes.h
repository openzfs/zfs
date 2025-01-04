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

#ifndef	__SYS_STDTYPES_H
#define	__SYS_STDTYPES_H

typedef enum {
	B_FALSE = 0,
	B_TRUE = 1
} boolean_t;

typedef unsigned char		uchar_t;
typedef unsigned short		ushort_t;
typedef unsigned int		uint_t;
typedef unsigned long		ulong_t;
typedef unsigned long long	u_longlong_t;
typedef long long		longlong_t;

typedef longlong_t		offset_t;
typedef u_longlong_t		u_offset_t;
typedef u_longlong_t		len_t;
typedef longlong_t		diskaddr_t;

typedef ulong_t			pgcnt_t;	/* number of pages */
typedef long			spgcnt_t;	/* signed number of pages */

typedef short			pri_t;
typedef ushort_t		o_mode_t;	/* old file attribute type */

typedef int			major_t;
typedef int			minor_t;

typedef short			index_t;

#endif	/* __SYS_STDTYPES_H */
