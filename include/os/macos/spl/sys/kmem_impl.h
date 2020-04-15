/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
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
 * Copyright (c) 1994, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SYS_KMEM_IMPL_H
#define	_SYS_KMEM_IMPL_H

#include <sys/kmem.h>
#include <sys/vmem.h>
#include <sys/thread.h>
#include <sys/time.h>
#include <sys/kstat.h>
#include <sys/systm.h>
#include <sys/avl.h>
#include <sys/list.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * kernel memory allocator: implementation-private data structures
 *
 * Lock order:
 * 1. cache_lock
 * 2. cc_lock in order by CPU ID
 * 3. cache_depot_lock
 *
 * Do not call kmem_cache_alloc() or taskq_dispatch() while holding any of the
 * above locks.
 */

#define	KMF_AUDIT	0x00000001 /* transaction auditing */
#define	KMF_DEADBEEF	0x00000002 /* deadbeef checking */
#define	KMF_REDZONE	0x00000004 /* redzone checking */
#define	KMF_CONTENTS	0x00000008 /* freed-buffer content logging */
#define	KMF_STICKY	0x00000010 /* if set, override /etc/system */
#define	KMF_NOMAGAZINE	0x00000020 /* disable per-cpu magazines */
#define	KMF_FIREWALL	0x00000040 /* put all bufs before unmapped pages */
#define	KMF_LITE	0x00000100 /* lightweight debugging */

#define	KMF_HASH	0x00000200 /* cache has hash table */
#define	KMF_RANDOMIZE	0x00000400 /* randomize other kmem_flags */

#define	KMF_DUMPDIVERT	0x00001000 /* use alternate memory at dump time */
#define	KMF_DUMPUNSAFE	0x00002000 /* flag caches used at dump time */
#define	KMF_PREFILL	0x00004000 /* Prefill the slab when created. */

#define	KMF_BUFTAG	(KMF_DEADBEEF | KMF_REDZONE)
#define	KMF_TOUCH	(KMF_BUFTAG | KMF_LITE | KMF_CONTENTS)
#define	KMF_RANDOM	(KMF_TOUCH | KMF_AUDIT | KMF_NOMAGAZINE)
#define	KMF_DEBUG	(KMF_RANDOM | KMF_FIREWALL)

#define	KMEM_STACK_DEPTH	15

#define	KMEM_FREE_PATTERN		0xdeadbeefdeadbeefULL
#define	KMEM_UNINITIALIZED_PATTERN	0xbaddcafebaddcafeULL
#define	KMEM_REDZONE_PATTERN		0xfeedfacefeedfaceULL
#define	KMEM_REDZONE_BYTE		0xbb

/*
 * Upstream platforms handle size == 0 as valid alloc, we
 * can not return NULL, as that invalidates KM_SLEEP. So
 * we return a valid hardcoded address, instead of actually taking up
 * memory by fudging size to 1 byte. If read/writes are
 * attempted, we will get page fault (which is correct, they
 * asked for zero bytes after all)
 */
#define	KMEM_ZERO_SIZE_PTR ((void *)16)

/*
 * Redzone size encodings for kmem_alloc() / kmem_free().  We encode the
 * allocation size, rather than storing it directly, so that kmem_free()
 * can distinguish frees of the wrong size from redzone violations.
 *
 * A size of zero is never valid.
 */
#define	KMEM_SIZE_ENCODE(x)	(251 * (x) + 1)
#define	KMEM_SIZE_DECODE(x)	((x) / 251)
#define	KMEM_SIZE_VALID(x)	((x) % 251 == 1 && (x) != 1)


#define	KMEM_ALIGN		8	/* min guaranteed alignment */
#define	KMEM_ALIGN_SHIFT	3	/* log2(KMEM_ALIGN) */
#define	KMEM_VOID_FRACTION	8	/* never waste more than 1/8 of slab */

#define	KMEM_SLAB_IS_PARTIAL(sp)		\
	((sp)->slab_refcnt > 0 && (sp)->slab_refcnt < (sp)->slab_chunks)
#define	KMEM_SLAB_IS_ALL_USED(sp)		\
	((sp)->slab_refcnt == (sp)->slab_chunks)

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
typedef struct kmem_bufctl {
	struct kmem_bufctl	*bc_next;	/* next bufctl struct */
	void			*bc_addr;	/* address of buffer */
	struct kmem_slab	*bc_slab;	/* controlling slab */
} kmem_bufctl_t;

/*
 * The KMF_AUDIT version of the bufctl structure.  The beginning of this
 * structure must be identical to the normal bufctl structure so that
 * pointers are interchangeable.
 */
typedef struct kmem_bufctl_audit {
	struct kmem_bufctl	*bc_next;	/* next bufctl struct */
	void			*bc_addr;	/* address of buffer */
	struct kmem_slab	*bc_slab;	/* controlling slab */
	kmem_cache_t		*bc_cache;	/* controlling cache */
	hrtime_t		bc_timestamp;	/* transaction time */
	kthread_t		*bc_thread;	/* thread doing transaction */
	struct kmem_bufctl	*bc_lastlog;	/* last log entry */
	void			*bc_contents;	/* contents at last free */
	int			bc_depth;	/* stack depth */
	pc_t			bc_stack[KMEM_STACK_DEPTH];	/* pc stack */
} kmem_bufctl_audit_t;

/*
 * A kmem_buftag structure is appended to each buffer whenever any of the
 * KMF_BUFTAG flags (KMF_DEADBEEF, KMF_REDZONE, KMF_VERIFY) are set.
 */
typedef struct kmem_buftag {
	uint64_t		bt_redzone;	/* 64-bit redzone pattern */
	kmem_bufctl_t		*bt_bufctl;	/* bufctl */
	intptr_t		bt_bxstat;	/* bufctl ^ (alloc/free) */
} kmem_buftag_t;

/*
 * A variant of the kmem_buftag structure used for KMF_LITE caches.
 * Previous callers are stored in reverse chronological order. (i.e. most
 * recent first)
 */
typedef struct kmem_buftag_lite {
	kmem_buftag_t		bt_buftag;	/* a normal buftag */
	pc_t			bt_history[1];	/* zero or more callers */
} kmem_buftag_lite_t;

#define	KMEM_BUFTAG_LITE_SIZE(f)	\
	(offsetof(kmem_buftag_lite_t, bt_history[f]))

#define	KMEM_BUFTAG(cp, buf)		\
	((kmem_buftag_t *)((char *)(buf) + (cp)->cache_buftag))

#define	KMEM_BUFCTL(cp, buf)		\
	((kmem_bufctl_t *)((char *)(buf) + (cp)->cache_bufctl))

#define	KMEM_BUF(cp, bcp)		\
	((void *)((char *)(bcp) - (cp)->cache_bufctl))

#define	KMEM_SLAB(cp, buf)		\
	((kmem_slab_t *)P2END((uintptr_t)(buf), (cp)->cache_slabsize) - 1)

/*
 * Test for using alternate memory at dump time.
 */
#define	KMEM_DUMP(cp)		((cp)->cache_flags & KMF_DUMPDIVERT)
#define	KMEM_DUMPCC(ccp)	((ccp)->cc_flags & KMF_DUMPDIVERT)

/*
 * The "CPU" macro loads a cpu_t that refers to the cpu that the current
 * thread is running on at the time the macro is executed.  A context switch
 * may occur immediately after loading this data structure, leaving this
 * thread pointing at the cpu_t for the previous cpu.  This is not a problem;
 * we'd just end up checking the previous cpu's per-cpu cache, and then check
 * the other layers of the kmem cache if need be.
 *
 * It's not even a problem if the old cpu gets DR'ed out during the context
 * switch.  The cpu-remove DR operation bzero()s the cpu_t, but doesn't free
 * it.  So the cpu_t's cpu_cache_offset would read as 0, causing us to use
 * cpu 0's per-cpu cache.
 *
 * So, there is no need to disable kernel preemption while using the CPU macro
 * below since if we have been context switched, there will not be any
 * correctness problem, just a momentary use of a different per-cpu cache.
 */

#define	KMEM_CPU_CACHE(cp)			\
	(&cp->cache_cpu[CPU_SEQID])

#define	KMOM_MAGAZINE_VALID(cp, mp)	\
	(((kmem_slab_t *)P2END((uintptr_t)(mp), PAGESIZE) - 1)->slab_cache == \
	(cp)->cache_magtype->mt_cache)

#define	KMEM_MAGAZINE_VALID(cp, mp)	\
	(((kmem_slab_t *)P2END((uintptr_t)(mp), PAGESIZE) - 1)->slab_cache == \
	(cp)->cache_magtype->mt_cache)

#define	KMEM_SLAB_OFFSET(sp, buf)	\
	((size_t)((uintptr_t)(buf) - (uintptr_t)((sp)->slab_base)))

#define	KMEM_SLAB_MEMBER(sp, buf)	\
	(KMEM_SLAB_OFFSET(sp, buf) < (sp)->slab_cache->cache_slabsize)

#define	KMEM_BUFTAG_ALLOC	0xa110c8edUL
#define	KMEM_BUFTAG_FREE	0xf4eef4eeUL

/* slab_later_count thresholds */
#define	KMEM_DISBELIEF		3

/* slab_flags */
#define	KMEM_SLAB_NOMOVE		0x1
#define	KMEM_SLAB_MOVE_PENDING	0x2

typedef struct kmem_slab {
	struct kmem_cache *slab_cache;	/* controlling cache */
	void		*slab_base;	/* base of allocated memory */
	avl_node_t	slab_link;	/* slab linkage */
	struct kmem_bufctl *slab_head;	/* first free buffer */
	long		slab_refcnt;	/* outstanding allocations */
	long		slab_chunks;	/* chunks (bufs) in this slab */
	uint32_t	slab_stuck_offset; /* unmoved buffer offset */
	uint16_t	slab_later_count; /* cf KMEM_CBRC_LATER */
	uint16_t	slab_flags;	/* bits to mark the slab */
	hrtime_t	slab_create_time; /* when was slab created? */
} kmem_slab_t;

#define	KMEM_HASH_INITIAL	64

#define	KMEM_HASH(cp, buf)	\
	((cp)->cache_hash_table +	\
	(((uintptr_t)(buf) >> (cp)->cache_hash_shift) & (cp)->cache_hash_mask))

#define	KMEM_CACHE_NAMELEN 31

typedef struct kmem_magazine {
    void	*mag_next;
    void	*mag_round[1];		/* one or more rounds */
} kmem_magazine_t;

/*
 * The magazine types for fast per-cpu allocation
 */
typedef struct kmem_magtype {
	short	mt_magsize;		/* magazine size (number of rounds) */
	int	mt_align;		/* magazine alignment */
	size_t	mt_minbuf;		/* all smaller buffers qualify */
	size_t	mt_maxbuf;		/* no larger buffers qualify */
	kmem_cache_t *mt_cache;		/* magazine cache */
} kmem_magtype_t;

#define	KMEM_CPU_CACHE_SIZE	128	/* must be power of 2 */
#define	KMEM_CPU_PAD	(KMEM_CPU_CACHE_SIZE - sizeof (kmutex_t) -	\
	2 * sizeof (uint64_t) - 2 * sizeof (void *) - sizeof (int) - \
	5 * sizeof (short))
#define	KMEM_CACHE_SIZE(ncpus)	\
	__builtin_offsetof(kmem_cache_t, cache_cpu[ncpus])

	/* Offset from kmem_cache->cache_cpu for per cpu caches */
#define	KMEM_CPU_CACHE_OFFSET(cpuid) \
	__builtin_offsetof(kmem_cache_t, cache_cpu[cpuid]) - \
	__builtin_offsetof(kmem_cache_t, cache_cpu)

/*
 * Per CPU cache data
 */
typedef struct kmem_cpu_cache {
	kmutex_t	cc_lock;	/* protects this cpu's local cache */
	uint64_t	cc_alloc;	/* allocations from this cpu */
	uint64_t	cc_free;	/* frees to this cpu */
	kmem_magazine_t	*cc_loaded;	/* the currently loaded magazine */
	kmem_magazine_t	*cc_ploaded;	/* the previously loaded magazine */
	int		cc_flags;	/* CPU-local copy of cache_flags */
	short		cc_rounds;	/* number of objects in loaded mag */
	short		cc_prounds;	/* number of objects in previous mag */
	short		cc_magsize;	/* number of rounds in a full mag */
	short		cc_dump_rounds;	/* dump time copy of cc_rounds */
	short		cc_dump_prounds; /* dump time copy of cc_prounds */
	char		cc_pad[KMEM_CPU_PAD]; /* for nice alignment */
} kmem_cpu_cache_t;

/*
 * The magazine lists used in the depot.
 */
typedef struct kmem_maglist {
	kmem_magazine_t	*ml_list;	/* magazine list */
	long		ml_total;	/* number of magazines */
	long		ml_min;		/* min since last update */
	long		ml_reaplimit;	/* max reapable magazines */
	uint64_t	ml_alloc;	/* allocations from this list */
} kmem_maglist_t;

typedef struct kmem_defrag {
	/*
	 * Statistics
	 */
	uint64_t	kmd_callbacks;	/* move callbacks */
	uint64_t	kmd_yes;	/* KMEM_CBRC_YES responses */
	uint64_t	kmd_no;		/* NO responses */
	uint64_t	kmd_later;	/* LATER responses */
	uint64_t	kmd_dont_need;	/* DONT_NEED responses */
	uint64_t	kmd_dont_know;	/* DONT_KNOW responses */
	uint64_t	kmd_slabs_freed; /* slabs freed by moves */
	uint64_t	kmd_defrags;	/* kmem_cache_defrag() */
	uint64_t	kmd_scans;	/* kmem_cache_scan() */

	/*
	 * Consolidator fields
	 */
	avl_tree_t	kmd_moves_pending;	/* buffer moves pending */
	list_t		kmd_deadlist;		/* deferred slab frees */
	size_t		kmd_deadcount;		/* # of slabs in kmd_deadlist */
	uint8_t		kmd_reclaim_numer;	/* slab usage threshold */
	uint8_t		kmd_pad1;		/* compiler padding */
	uint16_t	kmd_consolidate;	/* triggers consolidator */
	uint32_t	kmd_pad2;		/* compiler padding */
	size_t		kmd_slabs_sought;	/* reclaimable slabs sought */
	size_t		kmd_slabs_found;	/* reclaimable slabs found */
	size_t		kmd_tries;		/* nth scan interval counter */
	/*
	 * Fields used to ASSERT that the client does not kmem_cache_free()
	 * objects passed to the move callback.
	 */
	void		*kmd_from_buf;		/* object to move */
	void		*kmd_to_buf;		/* move destination */
	kthread_t	*kmd_thread;		/* thread calling move */
} kmem_defrag_t;

/*
 * Cache callback function types
 */
typedef int (*constructor_fn_t)(void*, void*, int);
typedef void (*destructor_fn_t)(void*, void*);
typedef void (*reclaim_fn_t)(void*);

/*
 * Cache
 */
struct kmem_cache {

/*
 * Statistics
 */
	uint64_t cache_slab_create;	/* slab creates */
	uint64_t cache_slab_destroy;	/* slab destroys */
	uint64_t cache_slab_alloc;	/* slab layer allocations */
	uint64_t cache_slab_free;	/* slab layer frees */
	uint64_t cache_alloc_fail;	/* total failed allocations */
	uint64_t cache_buftotal;	/* total buffers */
	uint64_t cache_bufmax;		/* max buffers ever */
	uint64_t cache_bufslab;		/* buffers free in slab layer */
	uint64_t cache_reap;		/* cache reaps */
	uint64_t cache_rescale;		/* hash table rescales */
	uint64_t cache_lookup_depth;	/* hash lookup depth */
	uint64_t cache_depot_contention; /* mutex contention count */
	uint64_t cache_depot_contention_prev; /* previous snapshot */
	uint64_t cache_alloc_count;	/* Number of allocations in cache */
	/* successful calls with KM_NO_VBA flag set */
	uint64_t no_vba_success;
	uint64_t no_vba_fail;
	/* number of times we set arc growth suppression time */
	uint64_t arc_no_grow_set;
	/* number of times spl_zio_is_suppressed returned true for this cache */
	uint64_t arc_no_grow;

	/*
	 * Cache properties
	 */
	char		cache_name[KMEM_CACHE_NAMELEN + 1];
	size_t		cache_bufsize;	/* object size */
	size_t		cache_align;	/* object alignment */
	int		(*cache_constructor)(void *, void *, int);
	void		(*cache_destructor)(void *, void *);
	void		(*cache_reclaim)(void *);
	kmem_cbrc_t	(*cache_move)(void *, void *, size_t, void *);
	void		*cache_private;	/* opaque arg to callbacks */
	vmem_t		*cache_arena;	/* vmem source for slabs */
	int		cache_cflags;	/* cache creation flags */
	int		cache_flags;	/* various cache state info */
	uint32_t	cache_mtbf;	/* induced alloc failure rate */
	uint32_t	cache_pad1;	/* compiler padding */
	kstat_t		*cache_kstat;	/* exported statistics */
	list_node_t	cache_link;	/* cache linkage */

	/*
	 * Slab layer
	 */
	kmutex_t	cache_lock;		/* protects slab layer */

	size_t		cache_chunksize;	/* buf + alignment [+ debug] */
	size_t		cache_slabsize;		/* size of a slab */
	size_t		cache_maxchunks;	/* max buffers per slab */
	size_t		cache_bufctl;		/* buf-to-bufctl distance */
	size_t		cache_buftag;		/* buf-to-buftag distance */
	size_t		cache_verify;		/* bytes to verify */
	size_t		cache_contents;		/* bytes of saved content */
	size_t		cache_color;		/* next slab color */
	size_t		cache_mincolor;		/* maximum slab color */
	size_t		cache_maxcolor;		/* maximum slab color */
	size_t		cache_hash_shift;	/* get to interesting bits */
	size_t		cache_hash_mask;	/* hash table mask */
	list_t		cache_complete_slabs;	/* completely allocated slabs */
	size_t		cache_complete_slab_count;
	avl_tree_t	cache_partial_slabs;	/* partial slab freelist */
	size_t		cache_partial_binshift;	/* for AVL sort bins */
	kmem_cache_t	*cache_bufctl_cache;	/* source of bufctls */
	kmem_bufctl_t	**cache_hash_table;	/* hash table base */
	kmem_defrag_t	*cache_defrag;		/* slab consolidator fields */

	/*
	 * Depot layer
	 */
	kmutex_t	cache_depot_lock;	/* protects depot */
	kmem_magtype_t	*cache_magtype;		/* magazine type */
	kmem_maglist_t	cache_full;		/* full magazines */
	kmem_maglist_t	cache_empty;		/* empty magazines */
	void		*cache_dumpfreelist;	/* heap during crash dump */
	void		*cache_dumplog;		/* log entry during dump */

	/*
	 * Per CPU structures
	 */
	// XNU adjust to suit __builtin_offsetof
	kmem_cpu_cache_t cache_cpu[1];		/* per-cpu data */

};

typedef struct kmem_cpu_log_header {
	kmutex_t	clh_lock;
	char		*clh_current;
	size_t		clh_avail;
	int		clh_chunk;
	int		clh_hits;
#if	defined(SPL_DEBUG_MUTEX)
	char		clh_pad[128 - sizeof (kmutex_t) - sizeof (char *) -
	    sizeof (size_t) - 2 * sizeof (int)];
#else
	char		clh_pad[64 - sizeof (kmutex_t) - sizeof (char *) -
	    sizeof (size_t) - 2 * sizeof (int)];
#endif
} kmem_cpu_log_header_t;

typedef struct kmem_log_header {
	kmutex_t	lh_lock;
	char		*lh_base;
	int		*lh_free;
	size_t		lh_chunksize;
	int		lh_nchunks;
	int		lh_head;
	int		lh_tail;
	int		lh_hits;
	kmem_cpu_log_header_t lh_cpu[1];	/* ncpus actually allocated */
} kmem_log_header_t;

/* kmem_move kmm_flags */
#define	KMM_DESPERATE	0x1
#define	KMM_NOTIFY		0x2
#define	KMM_DEBUG		0x4

typedef struct kmem_move {
	kmem_slab_t	*kmm_from_slab;
	void		*kmm_from_buf;
	void		*kmm_to_buf;
	avl_node_t	kmm_entry;
	int			kmm_flags;
} kmem_move_t;

/*
 * In order to consolidate partial slabs, it must be possible for the cache to
 * have partial slabs.
 */
#define	KMEM_IS_MOVABLE(cp) \
	(((cp)->cache_chunksize * 2) <= (cp)->cache_slabsize)

#endif
