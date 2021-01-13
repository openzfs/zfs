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
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_CCOMPILE_H
#define	_SYS_CCOMPILE_H

/*
 * This file contains definitions designed to enable different compilers
 * to be used harmoniously on Solaris systems.
 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Allow for version tests for compiler bugs and features.
 */
#if defined(__GNUC__)
#define	__GNUC_VERSION	\
	(__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#else
#define	__GNUC_VERSION	0
#endif

#if defined(__ATTRIBUTE_IMPLEMENTED) || defined(__GNUC__)

#if 0
/*
 * analogous to lint's PRINTFLIKEn
 */
#define	__sun_attr___PRINTFLIKE__(__n)	\
		__attribute__((__format__(printf, __n, (__n)+1)))
#define	__sun_attr___VPRINTFLIKE__(__n)	\
		__attribute__((__format__(printf, __n, 0)))

#define	__sun_attr___KPRINTFLIKE__	__sun_attr___PRINTFLIKE__
#define	__sun_attr___KVPRINTFLIKE__	__sun_attr___VPRINTFLIKE__
#else
/*
 * Currently the openzfs codebase has a lot of formatting errors
 * which are not picked up in the linux build because they're not
 * doing formatting checks. LLVM's kprintf implementation doesn't
 * actually do format checks!
 *
 * For FreeBSD these break under gcc! LLVM shim'ed cmn_err as a
 * format attribute but also didn't check anything.  If one
 * replaces it with the above, all of the format issues
 * in the codebase show up.
 *
 * Once those format string issues are addressed, the above
 * should be flipped on once again.
 */
#define	__sun_attr___PRINTFLIKE__(__n)
#define	__sun_attr___VPRINTFLIKE__(__n)
#define	__sun_attr___KPRINTFLIKE__(__n)
#define	__sun_attr___KVPRINTFLIKE__(__n)

#endif

/*
 * This one's pretty obvious -- the function never returns
 */
#define	__sun_attr___noreturn__ __attribute__((__noreturn__))

/*
 * This is an appropriate label for functions that do not
 * modify their arguments, e.g. strlen()
 */
#define	__sun_attr___pure__	__attribute__((__pure__))

/*
 * This is a stronger form of __pure__. Can be used for functions
 * that do not modify their arguments and don't depend on global
 * memory.
 */
#define	__sun_attr___const__	__attribute__((__const__))

/*
 * structure packing like #pragma pack(1)
 */
#define	__sun_attr___packed__	__attribute__((__packed__))

#define	___sun_attr_inner(__a)	__sun_attr_##__a
#define	__sun_attr__(__a)	___sun_attr_inner __a

#else	/* __ATTRIBUTE_IMPLEMENTED || __GNUC__ */

#define	__sun_attr__(__a)

#endif	/* __ATTRIBUTE_IMPLEMENTED || __GNUC__ */

/*
 * Shorthand versions for readability
 */

#define	__PRINTFLIKE(__n)	__sun_attr__((__PRINTFLIKE__(__n)))
#define	__VPRINTFLIKE(__n)	__sun_attr__((__VPRINTFLIKE__(__n)))
#define	__KPRINTFLIKE(__n)	__sun_attr__((__KPRINTFLIKE__(__n)))
#define	__KVPRINTFLIKE(__n)	__sun_attr__((__KVPRINTFLIKE__(__n)))
#if	defined(_KERNEL) || defined(_STANDALONE)
#define	__NORETURN		__sun_attr__((__noreturn__))
#endif /* _KERNEL || _STANDALONE */
#define	__CONST			__sun_attr__((__const__))
#define	__PURE			__sun_attr__((__pure__))

#if defined(INVARIANTS) && !defined(ZFS_DEBUG)
#define	ZFS_DEBUG
#undef 	NDEBUG
#endif

#define	EXPORT_SYMBOL(x)
#define	MODULE_AUTHOR(s)
#define	MODULE_DESCRIPTION(s)
#define	MODULE_LICENSE(s)
#define	module_param(a, b, c)
#define	module_param_call(a, b, c, d, e)
#define	module_param_named(a, b, c, d)
#define	MODULE_PARM_DESC(a, b)
#define	asm __asm
#ifdef ZFS_DEBUG
#undef NDEBUG
#endif
#if !defined(ZFS_DEBUG) && !defined(NDEBUG)
#define	NDEBUG
#endif

#ifndef EINTEGRITY
#define	EINTEGRITY 97 /* EINTEGRITY is new in 13 */
#endif

/*
 * These are bespoke errnos used in ZFS. We map them to their closest FreeBSD
 * equivalents. This gives us more useful error messages from strerror(3).
 */
#define	ECKSUM	EINTEGRITY
#define	EFRAGS	ENOSPC

/* Similar for ENOACTIVE */
#define	ENOTACTIVE	ECANCELED

#define	EREMOTEIO EREMOTE
#define	ECHRNG ENXIO
#define	ETIME ETIMEDOUT

#ifndef LOCORE
#ifndef HAVE_RPC_TYPES
typedef int bool_t;
typedef int enum_t;
#endif
#endif

#ifndef __cplusplus
#define	__init
#define	__exit
#endif

#if defined(_KERNEL) || defined(_STANDALONE)
#define	param_set_charp(a, b) (0)
#define	ATTR_UID AT_UID
#define	ATTR_GID AT_GID
#define	ATTR_MODE AT_MODE
#define	ATTR_XVATTR	AT_XVATTR
#define	ATTR_CTIME	AT_CTIME
#define	ATTR_MTIME	AT_MTIME
#define	ATTR_ATIME	AT_ATIME
#if defined(_STANDALONE)
#define	vmem_free kmem_free
#define	vmem_zalloc kmem_zalloc
#define	vmem_alloc kmem_zalloc
#else
#define	vmem_free zfs_kmem_free
#define	vmem_zalloc(size, flags) zfs_kmem_alloc(size, flags | M_ZERO)
#define	vmem_alloc zfs_kmem_alloc
#endif
#define	MUTEX_NOLOCKDEP 0
#define	RW_NOLOCKDEP 0

#else
#define	FALSE 0
#define	TRUE 1
	/*
	 * XXX We really need to consolidate on standard
	 * error codes in the common code
	 */
#define	ENOSTR ENOTCONN
#define	ENODATA EINVAL


#define	__BSD_VISIBLE 1
#ifndef	IN_BASE
#define	__POSIX_VISIBLE 201808
#define	__XSI_VISIBLE 1000
#endif
#define	ARRAY_SIZE(a) (sizeof (a) / sizeof (a[0]))
#define	mmap64 mmap
/* Note: this file can be used on linux/macOS when bootstrapping tools. */
#if defined(__FreeBSD__)
#define	open64 open
#define	pwrite64 pwrite
#define	ftruncate64 ftruncate
#define	lseek64 lseek
#define	pread64 pread
#define	stat64 stat
#define	lstat64 lstat
#define	statfs64 statfs
#define	readdir64 readdir
#define	dirent64 dirent
#endif
#define	P2ALIGN(x, align)		((x) & -(align))
#define	P2CROSS(x, y, align)	(((x) ^ (y)) > (align) - 1)
#define	P2ROUNDUP(x, align)		((((x) - 1) | ((align) - 1)) + 1)
#define	P2PHASE(x, align)		((x) & ((align) - 1))
#define	P2NPHASE(x, align)		(-(x) & ((align) - 1))
#define	ISP2(x)			(((x) & ((x) - 1)) == 0)
#define	IS_P2ALIGNED(v, a)	((((uintptr_t)(v)) & ((uintptr_t)(a) - 1)) == 0)
#define	P2BOUNDARY(off, len, align) \
	(((off) ^ ((off) + (len) - 1)) > (align) - 1)

/*
 * Typed version of the P2* macros.  These macros should be used to ensure
 * that the result is correctly calculated based on the data type of (x),
 * which is passed in as the last argument, regardless of the data
 * type of the alignment.  For example, if (x) is of type uint64_t,
 * and we want to round it up to a page boundary using "PAGESIZE" as
 * the alignment, we can do either
 *
 * P2ROUNDUP(x, (uint64_t)PAGESIZE)
 * or
 * P2ROUNDUP_TYPED(x, PAGESIZE, uint64_t)
 */
#define	P2ALIGN_TYPED(x, align, type)   \
	((type)(x) & -(type)(align))
#define	P2PHASE_TYPED(x, align, type)   \
	((type)(x) & ((type)(align) - 1))
#define	P2NPHASE_TYPED(x, align, type)  \
	(-(type)(x) & ((type)(align) - 1))
#define	P2ROUNDUP_TYPED(x, align, type) \
	((((type)(x) - 1) | ((type)(align) - 1)) + 1)
#define	P2END_TYPED(x, align, type)     \
	(-(~(type)(x) & -(type)(align)))
#define	P2PHASEUP_TYPED(x, align, phase, type)  \
	((type)(phase) - (((type)(phase) - (type)(x)) & -(type)(align)))
#define	P2CROSS_TYPED(x, y, align, type)        \
	(((type)(x) ^ (type)(y)) > (type)(align) - 1)
#define	P2SAMEHIGHBIT_TYPED(x, y, type) \
	(((type)(x) ^ (type)(y)) < ((type)(x) & (type)(y)))

#define	DIV_ROUND_UP(n, d)	(((n) + (d) - 1) / (d))
#define	RLIM64_INFINITY RLIM_INFINITY
#ifndef HAVE_ERESTART
#define	ERESTART EAGAIN
#endif
#define	ABS(a)	((a) < 0 ? -(a) : (a))

#endif
#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CCOMPILE_H */
