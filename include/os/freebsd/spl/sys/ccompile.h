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

/*
 * analogous to lint's PRINTFLIKEn
 */
#define	__sun_attr___PRINTFLIKE__(__n)	\
		__attribute__((__format__(printf, __n, (__n)+1)))
#define	__sun_attr___VPRINTFLIKE__(__n)	\
		__attribute__((__format__(printf, __n, 0)))

/*
 * Handle the kernel printf routines that can take '%b' too
 */
#if __GNUC_VERSION < 30402
/*
 * XX64 at least this doesn't work correctly yet with 3.4.1 anyway!
 */
#define	__sun_attr___KPRINTFLIKE__	__sun_attr___PRINTFLIKE__
#define	__sun_attr___KVPRINTFLIKE__	__sun_attr___VPRINTFLIKE__
#else
#define	__sun_attr___KPRINTFLIKE__(__n)	\
		__attribute__((__format__(cmn_err, __n, (__n)+1)))
#define	__sun_attr___KVPRINTFLIKE__(__n) \
		__attribute__((__format__(cmn_err, __n, 0)))
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
#ifdef _KERNEL
#define	__NORETURN		__sun_attr__((__noreturn__))
#endif
#define	__CONST			__sun_attr__((__const__))
#define	__PURE			__sun_attr__((__pure__))

#if (defined(ZFS_DEBUG) || !defined(NDEBUG))&& !defined(DEBUG)
#define	DEBUG
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

#define	O_LARGEFILE 0
#define	O_RSYNC 0
#define	O_DSYNC 0

#define	KMALLOC_MAX_SIZE MAXPHYS

#ifdef _KERNEL
typedef unsigned long long	u_longlong_t;
typedef long long		longlong_t;

#include <linux/types.h>
typedef	void zfs_kernel_param_t;
#define	param_set_charp(a, b) (0)
#define	ATTR_UID AT_UID
#define	ATTR_GID AT_GID
#define	ATTR_MODE AT_MODE
#define	ATTR_XVATTR	AT_XVATTR
#define	ATTR_CTIME	AT_CTIME
#define	ATTR_MTIME	AT_MTIME
#define	ATTR_ATIME	AT_ATIME
#define	vmem_free zfs_kmem_free
#define	vmem_zalloc(size, flags) zfs_kmem_alloc(size, flags | M_ZERO)
#define	vmem_alloc zfs_kmem_alloc
#define	MUTEX_NOLOCKDEP 0
#define	RW_NOLOCKDEP 0


#if  __FreeBSD_version < 1300051
#define	vm_page_valid(m) (m)->valid = VM_PAGE_BITS_ALL
#define	vm_page_do_sunbusy(m)
#define	vm_page_none_valid(m) ((m)->valid == 0)
#else
#define	vm_page_do_sunbusy(m) vm_page_sunbusy(m)
#endif

#if  __FreeBSD_version < 1300074
#define	VOP_UNLOCK1(x)	VOP_UNLOCK(x, 0)
#else
#define	VOP_UNLOCK1(x)	VOP_UNLOCK(x)
#endif

#if  __FreeBSD_version < 1300064
#define	VN_IS_DOOMED(vp)	((vp)->v_iflag & VI_DOOMED)
#endif

#if  __FreeBSD_version < 1300068
#define	VFS_VOP_VECTOR_REGISTER(x)
#endif

#if  __FreeBSD_version >= 1300076
#define	getnewvnode_reserve_()	getnewvnode_reserve()
#else
#define	getnewvnode_reserve_()	getnewvnode_reserve(1)
#endif

struct hlist_node {
	struct hlist_node *next, **pprev;
};

struct hlist_head {
	struct hlist_node *first;
};

typedef struct {
	volatile int counter;
} atomic_t;

	/* BEGIN CSTYLED */
#define	hlist_for_each(p, head)                                      \
	for (p = (head)->first; p; p = (p)->next)

#define	hlist_entry(ptr, type, field)   container_of(ptr, type, field)

#define	container_of(ptr, type, member)                         \
({                                                              \
        const __typeof(((type *)0)->member) *__p = (ptr);       \
        (type *)((uintptr_t)__p - offsetof(type, member));      \
})
	/* END CSTYLED */

static inline void
hlist_add_head(struct hlist_node *n, struct hlist_head *h)
{
	n->next = h->first;
	if (h->first != NULL)
		h->first->pprev = &n->next;
	WRITE_ONCE(h->first, n);
	n->pprev = &h->first;
}

static inline void
hlist_del(struct hlist_node *n)
{
	WRITE_ONCE(*(n->pprev), n->next);
	if (n->next != NULL)
		n->next->pprev = n->pprev;
}
	/* BEGIN CSTYLED */
#define	READ_ONCE(x) ({			\
	__typeof(x) __var = ({		\
		barrier();		\
		ACCESS_ONCE(x);		\
	});				\
	barrier();			\
	__var;				\
})

#define	HLIST_HEAD_INIT { }
#define	HLIST_HEAD(name) struct hlist_head name = HLIST_HEAD_INIT
#define	INIT_HLIST_HEAD(head) (head)->first = NULL

#define	INIT_HLIST_NODE(node)					\
	do {																\
		(node)->next = NULL;											\
		(node)->pprev = NULL;											\
	} while (0)

/* END CSTYLED */
static inline int
atomic_read(const atomic_t *v)
{
	return (READ_ONCE(v->counter));
}

static inline int
atomic_inc(atomic_t *v)
{
	return (atomic_fetchadd_int(&v->counter, 1) + 1);
}

static inline int
atomic_dec(atomic_t *v)
{
	return (atomic_fetchadd_int(&v->counter, -1) - 1);
}

#else
typedef long loff_t;
typedef long rlim64_t;
typedef int bool_t;
typedef int enum_t;
#ifndef __cplusplus
#define	__init
#endif
#define	__exit
#define	FALSE 0
#define	TRUE 1
	/*
	 * XXX We really need to consolidate on standard
	 * error codes in the common code
	 */
#define	ENOSTR ENOTCONN
#define	ENODATA EINVAL


#define	__XSI_VISIBLE 1000
#define	__BSD_VISIBLE 1
#define	__POSIX_VISIBLE 201808
#define	ARRAY_SIZE(a) (sizeof (a) / sizeof (a[0]))
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
#define	ERESTART EAGAIN
#define	ABS(a)	((a) < 0 ? -(a) : (a))

#endif
#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CCOMPILE_H */
