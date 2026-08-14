// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2007 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _SPL_SYS_TYPES_H_
#define	_SPL_SYS_TYPES_H_

#pragma once
/*
 * This is a bag of dirty hacks to keep things compiling.
 *
 * Native NetBSD <sys/types.h> is `typedef int boolean_t` (kernel) and
 * `typedef int64_t longlong_t` / `typedef uint64_t u_longlong_t`.
 * Illumos and the Linux SPL use `typedef enum { B_FALSE, B_TRUE }
 * boolean_t` and `typedef long long longlong_t`.  GCC treats a 1-bit
 * field of that enum as unsigned, so B_TRUE stores as 1.  A signed
 * `int :1` stores B_TRUE as -1; BP_SET_DEDUP → BF64_SET then panics
 * ASSERT3U(UINT64_MAX, <, 2).  Native LP64 int64_t is `long`, so %llu
 * format strings need unsigned long long.  Rename the native typedefs
 * away before the include, then define the Solaris types.
 */
#define	boolean_t	__netbsd_boolean_t
#define	u_longlong_t	__netbsd_u_longlong_t
#define	longlong_t	__netbsd_longlong_t
#include_next <sys/types.h>
#undef	boolean_t
#undef	u_longlong_t
#undef	longlong_t
#include <sys/stdint.h>

#define	_CLOCK_T_DECLARED

#include <sys/types32.h>
#include <linux/types.h>

/*
 * Illumos/OpenZFS MAXNAMELEN is 256 (zfs_cmd_t, zinject).  NetBSD
 * sys/param.h uses 511.  Always the ZFS value, not whichever header
 * won the include order.
 */
#ifdef MAXNAMELEN
#undef MAXNAMELEN
#endif
#define	MAXNAMELEN	256



typedef	void zfs_kernel_param_t;

typedef	struct timespec	timestruc_t;
typedef	struct timespec	timespec_t;
typedef struct timespec inode_timespec_t;
/* BEGIN CSTYLED */
typedef u_int		uint_t;
typedef u_char		uchar_t;
typedef u_short		ushort_t;
typedef u_long		ulong_t;
/* END CSTYLED */
typedef	int		minor_t;
#ifndef	_OFF64_T_DECLARED
#define	_OFF64_T_DECLARED
typedef off_t		off64_t;
#endif
typedef id_t		taskid_t;
typedef id_t		projid_t;
typedef id_t		poolid_t;
typedef uint_t		zoneid_t;
typedef id_t		ctid_t;
typedef	mode_t		o_mode_t;
typedef	uint64_t	pgcnt_t;

typedef	short		index_t;
typedef	off_t		offset_t;
typedef	int64_t		rlim64_t;
typedef	int		major_t;

#ifndef	_BOOLEAN_T_DECLARED
#define	_BOOLEAN_T_DECLARED
typedef enum {
	B_FALSE = 0,
	B_TRUE = 1
} boolean_t;
#endif

#ifndef	_LONGLONG_T_DECLARED
#define	_LONGLONG_T_DECLARED
typedef unsigned long long	u_longlong_t;
typedef long long		longlong_t;
#endif

typedef	u_longlong_t	u_offset_t;
typedef	u_longlong_t	len_t;

typedef	longlong_t	diskaddr_t;

#ifndef	_CADDR_T_DECLARED
#define	_CADDR_T_DECLARED
typedef	char		*caddr_t;
#endif

#include <sys/debug.h>
#endif	/* !_OPENSOLARIS_SYS_TYPES_H_ */
