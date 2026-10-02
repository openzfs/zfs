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
 *
 * Copyright (C) 2008 MacZFS
 * Copyright (C) 2013 Jorgen Lundman <lundman@lundman.net>
 *
 */

#ifndef _SPL_TYPES_H
#define	_SPL_TYPES_H

#define	likely(x)		__builtin_expect(!!(x), 1)
#define	unlikely(x)		__builtin_expect(!!(x), 0)

typedef short			pri_t;
typedef unsigned long		ulong_t;
typedef unsigned long long	u_longlong_t;
typedef unsigned long long	rlim64_t;
typedef unsigned long long	loff_t;
typedef long long		longlong_t;
typedef unsigned char		uchar_t;
typedef unsigned int		uint_t;
typedef unsigned short		ushort_t;
typedef void 			*spinlock_t;
typedef long long		offset_t;
typedef struct timespec		timestruc_t; /* definition per SVr4 */
typedef struct timespec		timespec_t;
typedef ulong_t			pgcnt_t;
typedef unsigned int 		umode_t;
#define	NODEV32			(dev32_t)(-1)
typedef	unsigned int		dev32_t;
typedef unsigned int	minor_t;
typedef	short			index_t;

#include <TargetConditionals.h>
#include <AvailabilityMacros.h>
#include_next <sys/types.h>
#include <string.h>

#include <stddef.h>

#if !defined(MAC_OS_X_VERSION_10_12) ||	\
	(MAC_OS_X_VERSION_MIN_REQUIRED <= MAC_OS_X_VERSION_10_12)
#include <i386/types.h>
#include <i386/limits.h>
#include <sys/_types/_ptrdiff_t.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Avoid kcdata.h header error */
extern unsigned long strnlen(const char *, unsigned long);

#ifdef  __cplusplus
}
#endif

#include <libkern/libkern.h>

#include <sys/stropts.h>

#ifndef ULLONG_MAX
#define	ULLONG_MAX			(~0ULL)
#endif

#ifndef LLONG_MAX
#define	LLONG_MAX			((long long)(~0ULL>>1))
#endif

#ifdef NEED_SOLARIS_BOOLEAN

#if defined(__XOPEN_OR_POSIX)
typedef enum { _B_FALSE, _B_TRUE }	boolean_t;
#else
typedef enum { B_FALSE, B_TRUE }	boolean_t;
#endif /* defined(__XOPEN_OR_POSIX) */
#else

#define	B_FALSE 0
#define	B_TRUE  1

#endif /* NEED_SOLARIS_BOOLEAN */

#include  <sys/fcntl.h>
#define	FCREAT		O_CREAT
#define	FTRUNC		O_TRUNC
#define	FEXCL		O_EXCL
#define	FNOCTTY		O_NOCTTY
#define	FNOFOLLOW	O_NOFOLLOW

#ifdef __APPLE__
#define	FSYNC		O_SYNC  /* file (data+inode) integrity while writing */
#define	FDSYNC		O_DSYNC /* file data only integrity while writing */
#define	FOFFMAX		0x0000  /* not used */
#define	FRSYNC		0x0000  /* not used */
#else
#define	FRSYNC		0x8000  /* sync read operations at same level of */
				/* integrity as specified for writes by */
				/* FSYNC and FDSYNC flags */
#define	FOFFMAX		0x2000  /* large file */
#endif

#define	EXPORT_SYMBOL(X)
#define	module_param(X, Y, Z)
#define	MODULE_PARM_DESC(X, Y)

#ifdef __GNUC__
#define	member_type(type, member) __typeof__(((type *)0)->member)
#else
#define	member_type(type, member) void
#endif

#define	container_of(ptr, type, member) ((type *) \
	((char *)(member_type(type, member) *) \
	{ ptr } - offsetof(type, member)))

typedef struct timespec inode_timespec_t;
typedef void zidmap_t;

#endif	/* _SPL_TYPES_H */
