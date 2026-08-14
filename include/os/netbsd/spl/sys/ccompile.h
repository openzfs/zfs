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
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_CCOMPILE_H
#define	_SYS_CCOMPILE_H

/*
 * clang/zig define _LP64 / __LP64__ but not BSD __LP64.
 * NetBSD machine/types.h gates __HAVE_ATOMIC64_OPS on __LP64.
 */
#if (defined(_LP64) || defined(__LP64__)) && !defined(__LP64)
#define	__LP64	1
#endif

/*
 * amd64 types.h sets __HAVE_DIRECT_MAP only under _KERNEL_OPT.
 * The kmod is not _KERNEL_OPT, so pmap.h omitted PMAP_DIRECT_MAP
 * and zfs_map_page uvm_km_alloc'd at IPL_VM after fpu_kern_enter.
 * Direct I/O ABD iterate then SIMD-fletcher'd with CR0.TS set
 * and fpudna panicked.  Same predicate as machine/types.h.
 */
#if defined(_KERNEL) && defined(__x86_64__) && !defined(XENPV) && \
	!defined(KASAN) && !defined(KMSAN)
#ifndef __HAVE_DIRECT_MAP
#define	__HAVE_DIRECT_MAP	1
#endif
#endif

/*
 * zig riscv64-netbsd omits these; NetBSD sys/common_wchar_limits.h
 * #errors.  Must run in userland too — this file is -included first.
 */
#ifndef __WCHAR_MIN__
#if defined(__WCHAR_MAX__)
#if defined(__WCHAR_UNSIGNED__) || (__WCHAR_MAX__ == 0xffffffffU) || \
	(__WCHAR_MAX__ == 0xffffffff)
#define	__WCHAR_MIN__	0
#else
#define	__WCHAR_MIN__	(-__WCHAR_MAX__ - 1)
#endif
#endif
#endif
#ifndef __WINT_MIN__
#if defined(__WINT_MAX__)
#if defined(__WINT_UNSIGNED__) || (__WINT_MAX__ == 0xffffffffU) || \
	(__WINT_MAX__ == 0xffffffff)
#define	__WINT_MIN__	0
#else
#define	__WINT_MIN__	(-__WINT_MAX__ - 1)
#endif
#endif
#endif
#ifndef __SIG_ATOMIC_TYPE__
#define	__SIG_ATOMIC_TYPE__	int
#endif

/*
 * This file contains definitions designed to enable different compilers
 * to be used harmoniously on Solaris systems.
 */

#if defined(_KERNEL) && !defined(LOCORE)
/*
 * Kernel-only compat.  Do not include proplib here -- its
 * PROP_TYPE_* enumerators collide with include/zfs_prop.h.
 * Files that need proplib include <prop/proplib.h> themselves.
 */
#include <rpc/types.h>
#include <rpc/xdr.h>
struct kauth_cred;
typedef struct kauth_cred cred_t;
#define	UID_NOBODY	32767
#ifndef AT_NOSET
#define	AT_NOSET	0
#endif
#ifndef LK_TYPE_MASK
#define	LK_TYPE_MASK	0x0000000f
#endif
#ifdef __DECONST
#undef __DECONST
#endif
#define	__DECONST(type, var)	((type)(uintptr_t)(const void *)(var))

/*
 * Keep KDTRACE_HOOKS from bsd.kmodule.mk so NetBSD SDT probes fire.
 * SET_ERROR is defined in zfs_debug_os.h when hooks are on.
 */
#ifndef SET_ERROR
#define	SET_ERROR(err)	(err)
#endif
#ifdef _KERNEL
#define	qsort	spl_qsort
void	spl_qsort(void *, size_t, size_t, int (*)(const void *, const void *));
/*
 * libc sprintf/sscanf are not in GENERIC (see spl_string.c).
 * sprintf is live (spa.c); sscanf is the zcp.c "%lld%n" stub.
 */
int	sprintf(char *, const char *, ...) __printflike(2, 3);
int	sscanf(const char *, const char *, ...);
#endif


#ifndef WRITE_ONCE
#define	WRITE_ONCE(x, v) \
	(*(volatile __typeof__(x) *)&(x) = (v))
#endif
#ifndef READ_ONCE
#define	READ_ONCE(x) \
	(*(volatile __typeof__(x) *)&(x))
#endif
#ifndef atomic_fetchadd_int
#define	atomic_fetchadd_int(p, v) \
	__atomic_fetch_add((p), (v), __ATOMIC_SEQ_CST)
#endif
/*
 * Do not decide atomic_load_64 here: __HAVE_ATOMIC64_OPS is not
 * visible yet (this file is -included first).  See sys/atomic.h.
 */
#ifndef atomic_load_ptr
#define	atomic_load_ptr(p) \
	__atomic_load_n((void **)(p), __ATOMIC_SEQ_CST)
#define	atomic_store_ptr(p, v) \
	__atomic_store_n((void **)(p), (v), __ATOMIC_SEQ_CST)
#define	atomic_store_rel_ptr(p, v) \
	__atomic_store_n((void **)(p), (void *)(uintptr_t)(v), \
	    __ATOMIC_RELEASE)
#endif

#ifndef ASSERT_VOP_LOCKED
#define	ASSERT_VOP_LOCKED(vp, w)	((void)0)
#define	ASSERT_VOP_ELOCKED(vp, w)	((void)0)
#define	VNASSERT(exp, vp, msg)		((void)0)
#endif
#endif /* _KERNEL */

#if defined(INVARIANTS) && !defined(ZFS_DEBUG)
#define	ZFS_DEBUG
#undef 	NDEBUG
#endif

#define	EXPORT_SYMBOL(x)

#define	asm __asm
#ifdef ZFS_DEBUG
#undef NDEBUG
#endif
#if !defined(ZFS_DEBUG) && !defined(NDEBUG)
#define	NDEBUG
#endif

#ifndef EBADE
#define	EBADE	ENOSR
#endif
#define	ECKSUM	EBADE
#define	EFRAGS	ENOSPC

#ifndef noinline
#define	noinline	__attribute__((noinline))
#endif
#ifndef zfs_fallthrough
#define	zfs_fallthrough	__attribute__((__fallthrough__))
#endif

/* Similar for ENOACTIVE */
#ifndef ENOTACTIVE
#define	ENOTACTIVE	ECANCELED
#endif
#ifndef EREMOTEIO
#define	EREMOTEIO	EREMOTE
#endif
#ifndef ECHRNG
#define	ECHRNG		ENXIO
#endif
#ifndef LOCORE
#ifndef HAVE_RPC_TYPES
#ifndef _KERNEL
typedef int bool_t;
typedef int enum_t;
#endif
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
#define	vmem_free(p, s)		kmem_free((p), (s))
#define	vmem_zalloc(size, flags)	kmem_zalloc((size), (flags))
#define	vmem_alloc(size, flags)	kmem_alloc((size), (flags))
#else
/*
 * Parse native vmem(9) declarations first so the 2-arg Solaris wrappers
 * below only rewrite OpenZFS call sites, not the kernel header.
 */
#include <sys/vmem.h>
#define	vmem_free(p, s)		zfs_kmem_free((p), (s))
#define	vmem_zalloc(size, flags)	zfs_kmem_zalloc((size), (flags))
#define	vmem_alloc(size, flags)	zfs_kmem_alloc((size), (flags))
#endif
#define	MUTEX_NOLOCKDEP	MUTEX_DEFAULT
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
#define	open64 open
#define	pwrite64 pwrite
#define	ftruncate64 ftruncate
#define	lseek64 lseek
#define	pread64 pread
#define	stat64 stat
#define	lstat64 lstat
#define	statfs64 statvfs
#define	fstatfs64 fstatvfs
#define	readdir64 readdir
#define	dirent64 dirent
#ifndef __DECONST
#define	__DECONST(type, var)	((type)(uintptr_t)(const void *)(var))
#endif
// Deprecated. Use P2ALIGN_TYPED instead.
// #define	P2ALIGN(x, align)		((x) & -(align))
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
#ifndef RLIM64_INFINITY
#define	RLIM64_INFINITY RLIM_INFINITY
#endif
#ifndef HAVE_ERESTART
#define	ERESTART EAGAIN
#endif
#define	ABS(a)	((a) < 0 ? -(a) : (a))

#endif
#endif	/* _SYS_CCOMPILE_H */
