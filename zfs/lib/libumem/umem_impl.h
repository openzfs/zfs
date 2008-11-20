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
/*
 * Portions Copyright 2006 OmniTI, Inc.
 */

#ifndef _UMEM_IMPL_H
#define	_UMEM_IMPL_H

/* #pragma ident	"@(#)umem_impl.h	1.6	05/06/08 SMI" */

#include <umem.h>

#ifdef HAVE_SYS_SYSMACROS_H
#include <sys/sysmacros.h>
#endif

#if HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#include <sys/vmem.h>
#ifdef HAVE_THREAD_H
# include <thread.h>
#else
# include "sol_compat.h"
#endif

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * umem memory allocator: implementation-private data structures
 */

/*
 * Internal flags for umem_cache_create
 */
#define	UMC_QCACHE	0x00100000
#define	UMC_INTERNAL	0x80000000

/*
 * Cache flags
 */
#define	UMF_AUDIT	0x00000001	/* transaction auditing */
#define	UMF_DEADBEEF	0x00000002	/* deadbeef checking */
#define	UMF_REDZONE	0x00000004	/* redzone checking */
#define	UMF_CONTENTS	0x00000008	/* freed-buffer content logging */
#define	UMF_CHECKSIGNAL	0x00000010	/* abort when in signal context */
#define	UMF_NOMAGAZINE	0x00000020	/* disable per-cpu magazines */
#define	UMF_FIREWALL	0x00000040	/* put all bufs before unmapped pages */
#define	UMF_LITE	0x00000100	/* lightweight debugging */

#define	UMF_HASH	0x00000200	/* cache has hash table */
#define	UMF_RANDOMIZE	0x00000400	/* randomize other umem_flags */

#define	UMF_BUFTAG	(UMF_DEADBEEF | UMF_REDZONE)
#define	UMF_TOUCH	(UMF_BUFTAG | UMF_LITE | UMF_CONTENTS)
#define	UMF_RANDOM	(UMF_TOUCH | UMF_AUDIT | UMF_NOMAGAZINE)
#define	UMF_DEBUG	(UMF_RANDOM | UMF_FIREWALL)

#define	UMEM_STACK_DEPTH	umem_stack_depth

#define	UMEM_FREE_PATTERN		0xdeadbeefdeadbeefULL
#define	UMEM_UNINITIALIZED_PATTERN	0xbaddcafebaddcafeULL
#define	UMEM_REDZONE_PATTERN		0xfeedfacefeedfaceULL
#define	UMEM_REDZONE_BYTE		0xbb

#define	UMEM_FATAL_FLAGS	(UMEM_NOFAIL)
#define	UMEM_SLEEP_FLAGS	(0)

/*
 * Redzone size encodings for umem_alloc() / umem_free().  We encode the
 * allocation size, rather than storing it directly, so that umem_free()
 * can distinguish frees of the wrong size from redzone violations.
 */
#define	UMEM_SIZE_ENCODE(x)	(251 * (x) + 1)
#define	UMEM_SIZE_DECODE(x)	((x) / 251)
#define	UMEM_SIZE_VALID(x)	((x) % 251 == 1)

/*
 * The bufctl (buffer control) structure keeps some minimal information
 * about each buffer: its address, its slab, and its current linkage,
 * which is either on the slab's freelist (if the buffer is free), or
 * on the cache's buf-to-bufctl hash table (if the buffer is allocated).
 * In the case of non-hashed, or "raw", caches (the common case), only
 * the freelist linkage is necessary: the buffer address is at a fixed
 * offset from the bufctl address, and the slab is at the end of the page.
 *
 * NOTE: bc_next must be the first field; raw buffers have linkage only.
 */
typedef struct umem_bufctl {
	struct umem_bufctl	*bc_next;	/* next bufctl struct */
	void			*bc_addr;	/* address of buffer */
	struct umem_slab	*bc_slab;	/* controlling slab */
} umem_bufctl_t;

/*
 * The UMF_AUDIT version of the bufctl structure.  The beginning of this
 * structure must be identical to the normal bufctl structure so that
 * pointers are interchangeable.
 */

#define	UMEM_BUFCTL_AUDIT_SIZE_DEPTH(frames) \
	((size_t)(&((umem_bufctl_audit_t *)0)->bc_stack[frames]))

/*
 * umem_bufctl_audits must be allocated from a UMC_NOHASH cache, so we
 * require that 2 of them, plus 2 buftags, plus a umem_slab_t, all fit on
 * a single page.
 *
 * For ILP32, this is about 1000 frames.
 * For LP64, this is about 490 frames.
 */

#define	UMEM_BUFCTL_AUDIT_ALIGN	32

#define	UMEM_BUFCTL_AUDIT_MAX_SIZE					\
	(P2ALIGN((PAGESIZE - sizeof (umem_slab_t))/2 -			\
	    sizeof (umem_buftag_t), UMEM_BUFCTL_AUDIT_ALIGN))

#define	UMEM_MAX_STACK_DEPTH						\
	((UMEM_BUFCTL_AUDIT_MAX_SIZE -					\
	    UMEM_BUFCTL_AUDIT_SIZE_DEPTH(0)) / sizeof (uintptr_t))

typedef struct umem_bufctl_audit {
	struct umem_bufctl	*bc_next;	/* next bufctl struct */
	void			*bc_addr;	/* address of buffer */
	struct umem_slab	*bc_slab;	/* controlling slab */
	umem_cache_t		*bc_cache;	/* controlling cache */
	hrtime_t		bc_timestamp;	/* transaction time */
	thread_t		bc_thread;	/* thread doing transaction */
	struct umem_bufctl	*bc_lastlog;	/* last log entry */
	void			*bc_contents;	/* contents at last free */
	int			bc_depth;	/* stack depth */
	uintptr_t		bc_stack[1];	/* pc stack */
} umem_bufctl_audit_t;

#define	UMEM_LOCAL_BUFCTL_AUDIT(bcpp)					\
		*(bcpp) = (umem_bufctl_audit_t *)			\
		    alloca(UMEM_BUFCTL_AUDIT_SIZE)

#define	UMEM_BUFCTL_AUDIT_SIZE						\
	UMEM_BUFCTL_AUDIT_SIZE_DEPTH(UMEM_STACK_DEPTH)

/*
 * A umem_buftag structure is appended to each buffer whenever any of the
 * UMF_BUFTAG flags (UMF_DEADBEEF, UMF_REDZONE, UMF_VERIFY) are set.
 */
typedef struct umem_buftag {
	uint64_t		bt_redzone;	/* 64-bit redzone pattern */
	umem_bufctl_t		*bt_bufctl;	/* bufctl */
	intptr_t		bt_bxstat;	/* bufctl ^ (alloc/free) */
} umem_buftag_t;

#define	UMEM_BUFTAG(cp, buf)		\
	((umem_buftag_t *)((char *)(buf) + (cp)->cache_buftag))

#define	UMEM_BUFCTL(cp, buf)		\
	((umem_bufctl_t *)((char *)(buf) + (cp)->cache_bufctl))

#define	UMEM_BUF(cp, bcp)		\
	((void *)((char *)(bcp) - (cp)->cache_bufctl))

#define	UMEM_SLAB(cp, buf)		\
	((umem_slab_t *)P2END((uintptr_t)(buf), (cp)->cache_slabsize) - 1)

#define	UMEM_CPU_CACHE(cp, cpu)		\
	(umem_cpu_cache_t *)((char *)cp + cpu->cpu_cache_offset)

#define	UMEM_MAGAZINE_VALID(cp, mp)	\
	(((umem_slab_t *)P2END((uintptr_t)(mp), PAGESIZE) - 1)->slab_cache == \
	    (cp)->cache_magtype->mt_cache)

#define	UMEM_SLAB_MEMBER(sp, buf)	\
	((size_t)(buf) - (size_t)(sp)->slab_base < \
	    (sp)->slab_cache->cache_slabsize)

#define	UMEM_BUFTAG_ALLOC	0xa110c8edUL
#define	UMEM_BUFTAG_FREE	0xf4eef4eeUL

typedef struct umem_slab {
	struct umem_cache	*slab_cache;	/* controlling cache */
	void			*slab_base;	/* base of allocated memory */
	struct umem_slab	*slab_next;	/* next slab on freelist */
	struct umem_slab	*slab_prev;	/* prev slab on freelist */
	struct umem_bufctl	*slab_head;	/* first free buffer */
	long			slab_refcnt;	/* outstanding allocations */
	long			slab_chunks;	/* chunks (bufs) in this slab */
} umem_slab_t;

#define	UMEM_HASH_INITIAL	64

#define	UMEM_HASH(cp, buf)	\
	((cp)->cache_hash_table +	\
	(((uintptr_t)(buf) >> (cp)->cache_hash_shift) & (cp)->cache_hash_mask))

typedef struct umem_magazine {
	void	*mag_next;
	void	*mag_round[1];		/* one or more rounds */
} umem_magazine_t;

/*
 * The magazine types for fast per-cpu allocation
 */
typedef struct umem_magtype {
	int		mt_magsize;	/* magazine size (number of rounds) */
	int		mt_align;	/* magazine alignment */
	size_t		mt_minbuf;	/* all smaller buffers qualify */
	size_t		mt_maxbuf;	/* no larger buffers qualify */
	umem_cache_t	*mt_cache;	/* magazine cache */
} umem_magtype_t;

#if (defined(__PTHREAD_MUTEX_SIZE__) && __PTHREAD_MUTEX_SIZE__ >= 24) || defined(UMEM_PTHREAD_MUTEX_TOO_BIG)
#define	UMEM_CPU_CACHE_SIZE	128	/* must be power of 2 */
#else
#define	UMEM_CPU_CACHE_SIZE	64	/* must be power of 2 */
#endif
#define	UMEM_CPU_PAD		(UMEM_CPU_CACHE_SIZE - sizeof (mutex_t) - \
	2 * sizeof (uint_t) - 2 * sizeof (void *) - 4 * sizeof (int))
#define	UMEM_CACHE_SIZE(ncpus)	\
	((size_t)(&((umem_cache_t *)0)->cache_cpu[ncpus]))

typedef struct umem_cpu_cache {
	mutex_t		cc_lock;	/* protects this cpu's local cache */
	uint_t		cc_alloc;	/* allocations from this cpu */
	uint_t		cc_free;	/* frees to this cpu */
	umem_magazine_t	*cc_loaded;	/* the currently loaded magazine */
	umem_magazine_t	*cc_ploaded;	/* the previously loaded magazine */
	int		cc_rounds;	/* number of objects in loaded mag */
	int		cc_prounds;	/* number of objects in previous mag */
	int		cc_magsize;	/* number of rounds in a full mag */
	int		cc_flags;	/* CPU-local copy of cache_flags */
#if (!defined(_LP64) || defined(UMEM_PTHREAD_MUTEX_TOO_BIG)) && !defined(_WIN32)
	/* on win32, UMEM_CPU_PAD evaluates to zero, and the MS compiler
	 * won't allow static initialization of arrays containing structures
	 * that contain zero size arrays */
	char		cc_pad[UMEM_CPU_PAD]; /* for nice alignment (32-bit) */
#endif
} umem_cpu_cache_t;

/*
 * The magazine lists used in the depot.
 */
typedef struct umem_maglist {
	umem_magazine_t	*ml_list;	/* magazine list */
	long		ml_total;	/* number of magazines */
	long		ml_min;		/* min since last update */
	long		ml_reaplimit;	/* max reapable magazines */
	uint64_t	ml_alloc;	/* allocations from this list */
} umem_maglist_t;

#define	UMEM_CACHE_NAMELEN	31

struct umem_cache {
	/*
	 * Statistics
	 */
	uint64_t	cache_slab_create;	/* slab creates */
	uint64_t	cache_slab_destroy;	/* slab destroys */
	uint64_t	cache_slab_alloc;	/* slab layer allocations */
	uint64_t	cache_slab_free;	/* slab layer frees */
	uint64_t	cache_alloc_fail;	/* total failed allocations */
	uint64_t	cache_buftotal;		/* total buffers */
	uint64_t	cache_bufmax;		/* max buffers ever */
	uint64_t	cache_rescale;		/* # of hash table rescales */
	uint64_t	cache_lookup_depth;	/* hash lookup depth */
	uint64_t	cache_depot_contention;	/* mutex contention count */
	uint64_t	cache_depot_contention_prev; /* previous snapshot */

	/*
	 * Cache properties
	 */
	char		cache_name[UMEM_CACHE_NAMELEN + 1];
	size_t		cache_bufsize;		/* object size */
	size_t		cache_align;		/* object alignment */
	umem_constructor_t *cache_constructor;
	umem_destructor_t *cache_destructor;
	umem_reclaim_t	*cache_reclaim;
	void		*cache_private;		/* opaque arg to callbacks */
	vmem_t		*cache_arena;		/* vmem source for slabs */
	int		cache_cflags;		/* cache creation flags */
	int		cache_flags;		/* various cache state info */
	int		cache_uflags;		/* UMU_* flags */
	uint32_t	cache_mtbf;		/* induced alloc failure rate */
	umem_cache_t	*cache_next;		/* forward cache linkage */
	umem_cache_t	*cache_prev;		/* backward cache linkage */
	umem_cache_t	*cache_unext;		/* next in update list */
	umem_cache_t	*cache_uprev;		/* prev in update list */
	uint32_t	cache_cpu_mask;		/* mask for cpu offset */

	/*
	 * Slab layer
	 */
	mutex_t		cache_lock;		/* protects slab layer */
	size_t		cache_chunksize;	/* buf + alignment [+ debug] */
	size_t		cache_slabsize;		/* size of a slab */
	size_t		cache_bufctl;		/* buf-to-bufctl distance */
	size_t		cache_buftag;		/* buf-to-buftag distance */
	size_t		cache_verify;		/* bytes to verify */
	size_t		cache_contents;		/* bytes of saved content */
	size_t		cache_color;		/* next slab color */
	size_t		cache_mincolor;		/* maximum slab color */
	size_t		cache_maxcolor;		/* maximum slab color */
	size_t		cache_hash_shift;	/* get to interesting bits */
	size_t		cache_hash_mask;	/* hash table mask */
	umem_slab_t	*cache_freelist;	/* slab free list */
	umem_slab_t	cache_nullslab;		/* end of freelist marker */
	umem_cache_t	*cache_bufctl_cache;	/* source of bufctls */
	umem_bufctl_t	**cache_hash_table;	/* hash table base */
	/*
	 * Depot layer
	 */
	mutex_t		cache_depot_lock;	/* protects depot */
	umem_magtype_t	*cache_magtype;		/* magazine type */
	umem_maglist_t	cache_full;		/* full magazines */
	umem_maglist_t	cache_empty;		/* empty magazines */

	/*
	 * Per-CPU layer
	 */
	umem_cpu_cache_t cache_cpu[1];		/* cache_cpu_mask + 1 entries */
};

typedef struct umem_cpu_log_header {
	mutex_t		clh_lock;
	char		*clh_current;
	size_t		clh_avail;
	int		clh_chunk;
	int		clh_hits;
	char		clh_pad[UMEM_CPU_CACHE_SIZE -
				sizeof (mutex_t) - sizeof (char *) -
				sizeof (size_t) - 2 * sizeof (int)];
} umem_cpu_log_header_t;

typedef struct umem_log_header {
	mutex_t		lh_lock;
	char		*lh_base;
	int		*lh_free;
	size_t		lh_chunksize;
	int		lh_nchunks;
	int		lh_head;
	int		lh_tail;
	int		lh_hits;
	umem_cpu_log_header_t lh_cpu[1];	/* actually umem_max_ncpus */
} umem_log_header_t;

typedef struct umem_cpu {
	uint32_t cpu_cache_offset;
	uint32_t cpu_number;
} umem_cpu_t;

#define	UMEM_MAXBUF	16384

#define	UMEM_ALIGN		8	/* min guaranteed alignment */
#define	UMEM_ALIGN_SHIFT	3	/* log2(UMEM_ALIGN) */
#define	UMEM_VOID_FRACTION	8	/* never waste more than 1/8 of slab */

/*
 * For 64 bits, buffers >= 16 bytes must be 16-byte aligned
 */
#ifdef _LP64
#define	UMEM_SECOND_ALIGN 16
#else
#define	UMEM_SECOND_ALIGN UMEM_ALIGN
#endif

#define	MALLOC_MAGIC			0x3a10c000 /* 8-byte tag */
#define	MEMALIGN_MAGIC			0x3e3a1000

#ifdef _LP64
#define	MALLOC_SECOND_MAGIC		0x16ba7000 /* 8-byte tag, 16-aligned */
#define	MALLOC_OVERSIZE_MAGIC		0x06e47000 /* 16-byte tag, _LP64 */
#endif

#define	UMEM_MALLOC_ENCODE(type, sz)	(uint32_t)((type) - (sz))
#define	UMEM_MALLOC_DECODE(stat, sz)	(uint32_t)((stat) + (sz))
#define	UMEM_FREE_PATTERN_32		(uint32_t)(UMEM_FREE_PATTERN)

#define	UMU_MAGAZINE_RESIZE	0x00000001
#define	UMU_HASH_RESCALE	0x00000002
#define	UMU_REAP		0x00000004
#define	UMU_NOTIFY		0x08000000
#define	UMU_ACTIVE		0x80000000

#define	UMEM_READY_INIT_FAILED		-1
#define	UMEM_READY_STARTUP		1
#define	UMEM_READY_INITING		2
#define	UMEM_READY			3

#ifdef UMEM_STANDALONE
extern void umem_startup(caddr_t, size_t, size_t, caddr_t, caddr_t);
extern int umem_add(caddr_t, size_t);
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _UMEM_IMPL_H */
