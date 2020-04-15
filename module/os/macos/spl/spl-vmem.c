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
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright (c) 2012 by Delphix. All rights reserved.
 * Copyright (c) 2012, Joyent, Inc. All rights reserved.
 * Copyright (c) 2017, 2021 by Sean Doran <smd@use.net>
 */

/*
 * Big Theory Statement for the virtual memory allocator.
 *
 * For a more complete description of the main ideas, see:
 *
 *	Jeff Bonwick and Jonathan Adams,
 *
 *	Magazines and vmem: Extending the Slab Allocator to Many CPUs and
 *	Arbitrary Resources.
 *
 *	Proceedings of the 2001 Usenix Conference.
 *	Available as http://www.usenix.org/event/usenix01/bonwick.html
 *
 *
 * 1. General Concepts
 * -------------------
 *
 * 1.1 Overview
 * ------------
 * We divide the kernel address space into a number of logically distinct
 * pieces, or *arenas*: text, data, heap, stack, and so on.  Within these
 * arenas we often subdivide further; for example, we use heap addresses
 * not only for the kernel heap (kmem_alloc() space), but also for DVMA,
 * bp_mapin(), /dev/kmem, and even some device mappings like the TOD chip.
 * The kernel address space, therefore, is most accurately described as
 * a tree of arenas in which each node of the tree *imports* some subset
 * of its parent.  The virtual memory allocator manages these arenas and
 * supports their natural hierarchical structure.
 *
 * 1.2 Arenas
 * ----------
 * An arena is nothing more than a set of integers.  These integers most
 * commonly represent virtual addresses, but in fact they can represent
 * anything at all.  For example, we could use an arena containing the
 * integers minpid through maxpid to allocate process IDs.  vmem_create()
 * and vmem_destroy() create and destroy vmem arenas.  In order to
 * differentiate between arenas used for adresses and arenas used for
 * identifiers, the VMC_IDENTIFIER flag is passed to vmem_create().  This
 * prevents identifier exhaustion from being diagnosed as general memory
 * failure.
 *
 * 1.3 Spans
 * ---------
 * We represent the integers in an arena as a collection of *spans*, or
 * contiguous ranges of integers.  For example, the kernel heap consists
 * of just one span: [kernelheap, ekernelheap).  Spans can be added to an
 * arena in two ways: explicitly, by vmem_add(), or implicitly, by
 * importing, as described in Section 1.5 below.
 *
 * 1.4 Segments
 * ------------
 * Spans are subdivided into *segments*, each of which is either allocated
 * or free.  A segment, like a span, is a contiguous range of integers.
 * Each allocated segment [addr, addr + size) represents exactly one
 * vmem_alloc_impl(size) that returned addr.  Free segments represent the space
 * between allocated segments.  If two free segments are adjacent, we
 * coalesce them into one larger segment; that is, if segments [a, b) and
 * [b, c) are both free, we merge them into a single segment [a, c).
 * The segments within a span are linked together in increasing-address order
 * so we can easily determine whether coalescing is possible.
 *
 * Segments never cross span boundaries.  When all segments within
 * an imported span become free, we return the span to its source.
 *
 * 1.5 Imported Memory
 * -------------------
 * As mentioned in the overview, some arenas are logical subsets of
 * other arenas.  For example, kmem_va_arena (a virtual address cache
 * that satisfies most kmem_slab_create() requests) is just a subset
 * of heap_arena (the kernel heap) that provides caching for the most
 * common slab sizes.  When kmem_va_arena runs out of virtual memory,
 * it *imports* more from the heap; we say that heap_arena is the
 * *vmem source* for kmem_va_arena.  vmem_create() allows you to
 * specify any existing vmem arena as the source for your new arena.
 * Topologically, since every arena is a child of at most one source,
 * the set of all arenas forms a collection of trees.
 *
 * 1.6 Constrained Allocations
 * ---------------------------
 * Some vmem clients are quite picky about the kind of address they want.
 * For example, the DVMA code may need an address that is at a particular
 * phase with respect to some alignment (to get good cache coloring), or
 * that lies within certain limits (the addressable range of a device),
 * or that doesn't cross some boundary (a DMA counter restriction) --
 * or all of the above.  vmem_xalloc() allows the client to specify any
 * or all of these constraints.
 *
 * 1.7 The Vmem Quantum
 * --------------------
 * Every arena has a notion of 'quantum', specified at vmem_create() time,
 * that defines the arena's minimum unit of currency.  Most commonly the
 * quantum is either 1 or PAGESIZE, but any power of 2 is legal.
 * All vmem allocations are guaranteed to be quantum-aligned.
 *
 * 1.8 Quantum Caching
 * -------------------
 * A vmem arena may be so hot (frequently used) that the scalability of vmem
 * allocation is a significant concern.  We address this by allowing the most
 * common allocation sizes to be serviced by the kernel memory allocator,
 * which provides low-latency per-cpu caching.  The qcache_max argument to
 * vmem_create() specifies the largest allocation size to cache.
 *
 * 1.9 Relationship to Kernel Memory Allocator
 * -------------------------------------------
 * Every kmem cache has a vmem arena as its slab supplier.  The kernel memory
 * allocator uses vmem_alloc_impl() and vmem_free_impl() to create and
 * destroy slabs.
 *
 *
 * 2. Implementation
 * -----------------
 *
 * 2.1 Segment lists and markers
 * -----------------------------
 * The segment structure (vmem_seg_t) contains two doubly-linked lists.
 *
 * The arena list (vs_anext/vs_aprev) links all segments in the arena.
 * In addition to the allocated and free segments, the arena contains
 * special marker segments at span boundaries.  Span markers simplify
 * coalescing and importing logic by making it easy to tell both when
 * we're at a span boundary (so we don't coalesce across it), and when
 * a span is completely free (its neighbors will both be span markers).
 *
 * Imported spans will have vs_import set.
 *
 * The next-of-kin list (vs_knext/vs_kprev) links segments of the same type:
 * (1) for allocated segments, vs_knext is the hash chain linkage;
 * (2) for free segments, vs_knext is the freelist linkage;
 * (3) for span marker segments, vs_knext is the next span marker.
 *
 * 2.2 Allocation hashing
 * ----------------------
 * We maintain a hash table of all allocated segments, hashed by address.
 * This allows vmem_free_impl() to discover the target segment in constant
 * time.
 * vmem_update() periodically resizes hash tables to keep hash chains short.
 *
 * 2.3 Freelist management
 * -----------------------
 * We maintain power-of-2 freelists for free segments, i.e. free segments
 * of size >= 2^n reside in vmp->vm_freelist[n].  To ensure constant-time
 * allocation, vmem_xalloc() looks not in the first freelist that *might*
 * satisfy the allocation, but in the first freelist that *definitely*
 * satisfies the allocation (unless VM_BESTFIT is specified, or all larger
 * freelists are empty).  For example, a 1000-byte allocation will be
 * satisfied not from the 512..1023-byte freelist, whose members *might*
 * contains a 1000-byte segment, but from a 1024-byte or larger freelist,
 * the first member of which will *definitely* satisfy the allocation.
 * This ensures that vmem_xalloc() works in constant time.
 *
 * We maintain a bit map to determine quickly which freelists are non-empty.
 * vmp->vm_freemap & (1 << n) is non-zero iff vmp->vm_freelist[n] is non-empty.
 *
 * The different freelists are linked together into one large freelist,
 * with the freelist heads serving as markers.  Freelist markers simplify
 * the maintenance of vm_freemap by making it easy to tell when we're taking
 * the last member of a freelist (both of its neighbors will be markers).
 *
 * 2.4 Vmem Locking
 * ----------------
 * For simplicity, all arena state is protected by a per-arena lock.
 * For very hot arenas, use quantum caching for scalability.
 *
 * 2.5 Vmem Population
 * -------------------
 * Any internal vmem routine that might need to allocate new segment
 * structures must prepare in advance by calling vmem_populate(), which
 * will preallocate enough vmem_seg_t's to get is through the entire
 * operation without dropping the arena lock.
 *
 * 2.6 Auditing
 * ------------
 * If KMF_AUDIT is set in kmem_flags, we audit vmem allocations as well.
 * Since virtual addresses cannot be scribbled on, there is no equivalent
 * in vmem to redzone checking, deadbeef, or other kmem debugging features.
 * Moreover, we do not audit frees because segment coalescing destroys the
 * association between an address and its segment structure.  Auditing is
 * thus intended primarily to keep track of who's consuming the arena.
 * Debugging support could certainly be extended in the future if it proves
 * necessary, but we do so much live checking via the allocation hash table
 * that even non-DEBUG systems get quite a bit of sanity checking already.
 */

#include <sys/vmem_impl.h>
#include <sys/kmem.h>
#include <sys/kstat.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/atomic.h>
#include <sys/sysmacros.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/types.h>
#include <stdbool.h>
#include <mach/machine/vm_types.h>
#include <libkern/OSDebug.h>
#include <kern/thread_call.h>

#define	VMEM_INITIAL		21	/* early vmem arenas */
#define	VMEM_SEG_INITIAL	800

/*
 * Adding a new span to an arena requires two segment structures: one to
 * represent the span, and one to represent the free segment it contains.
 */
#define	VMEM_SEGS_PER_SPAN_CREATE	2

/*
 * Allocating a piece of an existing segment requires 0-2 segment structures
 * depending on how much of the segment we're allocating.
 *
 * To allocate the entire segment, no new segment structures are needed; we
 * simply move the existing segment structure from the freelist to the
 * allocation hash table.
 *
 * To allocate a piece from the left or right end of the segment, we must
 * split the segment into two pieces (allocated part and remainder), so we
 * need one new segment structure to represent the remainder.
 *
 * To allocate from the middle of a segment, we need two new segment strucures
 * to represent the remainders on either side of the allocated part.
 */
#define	VMEM_SEGS_PER_EXACT_ALLOC	0
#define	VMEM_SEGS_PER_LEFT_ALLOC	1
#define	VMEM_SEGS_PER_RIGHT_ALLOC	1
#define	VMEM_SEGS_PER_MIDDLE_ALLOC	2

/*
 * vmem_populate() preallocates segment structures for vmem to do its work.
 * It must preallocate enough for the worst case, which is when we must import
 * a new span and then allocate from the middle of it.
 */
#define	VMEM_SEGS_PER_ALLOC_MAX		\
(VMEM_SEGS_PER_SPAN_CREATE + VMEM_SEGS_PER_MIDDLE_ALLOC)

/*
 * The segment structures themselves are allocated from vmem_seg_arena, so
 * we have a recursion problem when vmem_seg_arena needs to populate itself.
 * We address this by working out the maximum number of segment structures
 * this act will require, and multiplying by the maximum number of threads
 * that we'll allow to do it simultaneously.
 *
 * The worst-case segment consumption to populate vmem_seg_arena is as
 * follows (depicted as a stack trace to indicate why events are occurring):
 *
 * (In order to lower the fragmentation in the heap_arena, we specify a
 * minimum import size for the vmem_metadata_arena which is the same size
 * as the kmem_va quantum cache allocations.  This causes the worst-case
 * allocation from the vmem_metadata_arena to be 3 segments.)
 *
 * vmem_alloc_impl(vmem_seg_arena)	-> 2 segs (span create + exact alloc)
 *  segkmem_alloc(vmem_metadata_arena)
 *   vmem_alloc_impl(vmem_metadata_arena) -> 3 segs (span create + left alloc)
 *    vmem_alloc_impl(heap_arena)		-> 1 seg (left alloc)
 *   page_create()
 *   hat_memload()
 *    kmem_cache_alloc()
 *     kmem_slab_create()
 *	vmem_alloc_impl(hat_memload_arena) -> 2 segs (span create + exact alloc)
 *	 segkmem_alloc(heap_arena)
 *	  vmem_alloc_impl(heap_arena)	-> 1 seg (left alloc)
 *	  page_create()
 *	  hat_memload()		-> (hat layer won't recurse further)
 *
 * The worst-case consumption for each arena is 3 segment structures.
 * Of course, a 3-seg reserve could easily be blown by multiple threads.
 * Therefore, we serialize all allocations from vmem_seg_arena (which is OK
 * because they're rare).  We cannot allow a non-blocking allocation to get
 * tied up behind a blocking allocation, however, so we use separate locks
 * for VM_SLEEP and VM_NOSLEEP allocations.  Similarly, VM_PUSHPAGE allocations
 * must not block behind ordinary VM_SLEEPs.  In addition, if the system is
 * panicking then we must keep enough resources for panic_thread to do its
 * work.  Thus we have at most four threads trying to allocate from
 * vmem_seg_arena, and each thread consumes at most three segment structures,
 * so we must maintain a 12-seg reserve.
 */
#define	VMEM_POPULATE_RESERVE	12

/*
 * vmem_populate() ensures that each arena has VMEM_MINFREE seg structures
 * so that it can satisfy the worst-case allocation *and* participate in
 * worst-case allocation from vmem_seg_arena.
 */
#define	VMEM_MINFREE	(VMEM_POPULATE_RESERVE + VMEM_SEGS_PER_ALLOC_MAX)

static vmem_t vmem0[VMEM_INITIAL];
static vmem_t *vmem_populator[VMEM_INITIAL];
static uint32_t vmem_id;
static uint32_t vmem_populators;
static vmem_seg_t vmem_seg0[VMEM_SEG_INITIAL];
static vmem_seg_t *vmem_segfree;
static kmutex_t vmem_list_lock;
static kmutex_t vmem_segfree_lock;
static kmutex_t vmem_sleep_lock;
static kmutex_t vmem_nosleep_lock;
static kmutex_t vmem_pushpage_lock;
static kmutex_t vmem_panic_lock;
static kmutex_t vmem_xnu_alloc_lock;
static vmem_t *vmem_list;
static vmem_t *vmem_metadata_arena;
static vmem_t *vmem_seg_arena;
static vmem_t *vmem_hash_arena;
static vmem_t *vmem_vmem_arena;
vmem_t *spl_default_arena; // The bottom-most arena for SPL
static vmem_t *spl_default_arena_parent; // dummy arena as a placeholder
#define	VMEM_BUCKETS 13
#define	VMEM_BUCKET_LOWBIT 12
#define	VMEM_BUCKET_HIBIT 24
static vmem_t *vmem_bucket_arena[VMEM_BUCKETS];
vmem_t *spl_heap_arena;
static void *spl_heap_arena_initial_alloc;
static size_t spl_heap_arena_initial_alloc_size = 0;
#define	NUMBER_OF_ARENAS_IN_VMEM_INIT 21
/* vmem_update() every 15 seconds */
static struct timespec vmem_update_interval	= {15, 0};
uint32_t vmem_mtbf;	/* mean time between failures [default: off] */
size_t vmem_seg_size = sizeof (vmem_seg_t);

// must match with include/sys/vmem_impl.h
static vmem_kstat_t vmem_kstat_template = {
	{ "mem_inuse",		KSTAT_DATA_UINT64 },
	{ "mem_import",		KSTAT_DATA_UINT64 },
	{ "mem_total",		KSTAT_DATA_UINT64 },
	{ "vmem_source",	KSTAT_DATA_UINT32 },
	{ "alloc",		KSTAT_DATA_UINT64 },
	{ "free",		KSTAT_DATA_UINT64 },
	{ "wait",		KSTAT_DATA_UINT64 },
	{ "fail",		KSTAT_DATA_UINT64 },
	{ "lookup",		KSTAT_DATA_UINT64 },
	{ "search",		KSTAT_DATA_UINT64 },
	{ "populate_fail",	KSTAT_DATA_UINT64 },
	{ "contains",		KSTAT_DATA_UINT64 },
	{ "contains_search",	KSTAT_DATA_UINT64 },
	{ "parent_alloc",	KSTAT_DATA_UINT64 },
	{ "parent_free",	KSTAT_DATA_UINT64 },
	{ "threads_waiting",	KSTAT_DATA_UINT64 },
	{ "excess",		KSTAT_DATA_UINT64 },
	{ "lowest_stack",	KSTAT_DATA_UINT64 },
	{ "async_stack_calls",	KSTAT_DATA_UINT64 },
};


/*
 * Insert/delete from arena list (type 'a') or next-of-kin list (type 'k').
 */
#define	VMEM_INSERT(vprev, vsp, type)					\
{									\
vmem_seg_t *_vnext = (vprev)->vs_##type##next;			\
(vsp)->vs_##type##next = (_vnext);				\
(vsp)->vs_##type##prev = (vprev);				\
(vprev)->vs_##type##next = (vsp);				\
(_vnext)->vs_##type##prev = (vsp);				\
}

#define	VMEM_DELETE(vsp, type)						\
{									\
vmem_seg_t *_vprev = (vsp)->vs_##type##prev;			\
vmem_seg_t *_vnext = (vsp)->vs_##type##next;			\
(_vprev)->vs_##type##next = (_vnext);				\
(_vnext)->vs_##type##prev = (_vprev);				\
}

// vmem thread block count
uint64_t spl_vmem_threads_waiting = 0;

// number of allocations > minalloc
uint64_t spl_bucket_non_pow2_allocs = 0;

// allocator kstats
uint64_t spl_vmem_unconditional_allocs = 0;
uint64_t spl_vmem_unconditional_alloc_bytes = 0;
uint64_t spl_vmem_conditional_allocs = 0;
uint64_t spl_vmem_conditional_alloc_bytes = 0;
uint64_t spl_vmem_conditional_alloc_deny = 0;
uint64_t spl_vmem_conditional_alloc_deny_bytes = 0;

// bucket allocator kstat
uint64_t spl_xat_pressured = 0;
uint64_t spl_xat_lastalloc = 0;
uint64_t spl_xat_lastfree = 0;
uint64_t spl_xat_sleep = 0;

uint64_t spl_vba_fastpath = 0;
uint64_t spl_vba_fastexit = 0;
uint64_t spl_vba_slowpath = 0;
uint64_t spl_vba_parent_memory_appeared = 0;
uint64_t spl_vba_parent_memory_blocked = 0;
uint64_t spl_vba_hiprio_blocked = 0;
uint64_t spl_vba_cv_timeout = 0;
uint64_t spl_vba_loop_timeout = 0;
uint64_t spl_vba_cv_timeout_blocked = 0;
uint64_t spl_vba_loop_timeout_blocked = 0;
uint64_t spl_vba_sleep = 0;
uint64_t spl_vba_loop_entries = 0;

extern uint64_t stat_osif_malloc_fail;

// bucket minimum span size tunables
uint64_t spl_bucket_tunable_large_span = 0;
uint64_t spl_bucket_tunable_small_span = 0;

// for XAT & XATB visibility into VBA queue
static _Atomic uint32_t spl_vba_threads[VMEM_BUCKETS] = { 0 };
static uint32_t
    vmem_bucket_id_to_bucket_number[NUMBER_OF_ARENAS_IN_VMEM_INIT] = { 0 };
boolean_t spl_arc_no_grow(size_t, boolean_t, kmem_cache_t **);
_Atomic uint64_t spl_arc_no_grow_bits = 0;
uint64_t spl_arc_no_grow_count = 0;

// compare span ages this many steps from the head of the freelist
uint64_t spl_frag_max_walk = 1000;
uint64_t spl_frag_walked_out = 0;
uint64_t spl_frag_walk_cnt = 0;

extern void spl_free_set_emergency_pressure(int64_t p);
extern uint64_t segkmem_total_mem_allocated;
extern uint64_t total_memory;

extern uint64_t spl_enforce_memory_caps;
extern _Atomic uint64_t spl_dynamic_memory_cap;
extern hrtime_t spl_dynamic_memory_cap_last_downward_adjust;
extern kmutex_t spl_dynamic_memory_cap_lock;
extern uint64_t spl_dynamic_memory_cap_reductions;
extern uint64_t spl_dynamic_memory_cap_hit_floor;

extern void IOSleep(unsigned milliseconds);

#define	INITIAL_BLOCK_SIZE	16ULL*1024ULL*1024ULL
static char *initial_default_block = NULL;
void *IOMallocAligned(vm_size_t size, vm_offset_t alignment);
void IOFreeAligned(void * address, vm_size_t size);

/*
 * Get a vmem_seg_t from the global segfree list.
 */
static inline vmem_seg_t *
vmem_getseg_global(void)
{
	vmem_seg_t *vsp;

	mutex_enter(&vmem_segfree_lock);
	if ((vsp = vmem_segfree) != NULL)
		vmem_segfree = vsp->vs_knext;
	mutex_exit(&vmem_segfree_lock);

	if (vsp != NULL)
		vsp->vs_span_createtime = 0;

	return (vsp);
}

/*
 * Put a vmem_seg_t on the global segfree list.
 */
static inline void
vmem_putseg_global(vmem_seg_t *vsp)
{
	mutex_enter(&vmem_segfree_lock);
	vsp->vs_knext = vmem_segfree;
	vmem_segfree = vsp;
	mutex_exit(&vmem_segfree_lock);
}

/*
 * Get a vmem_seg_t from vmp's segfree list.
 */
static inline vmem_seg_t *
vmem_getseg(vmem_t *vmp)
{
	vmem_seg_t *vsp;

	ASSERT(vmp->vm_nsegfree > 0);

	vsp = vmp->vm_segfree;
	vmp->vm_segfree = vsp->vs_knext;
	vmp->vm_nsegfree--;

	return (vsp);
}

/*
 * Put a vmem_seg_t on vmp's segfree list.
 */
static inline void
vmem_putseg(vmem_t *vmp, vmem_seg_t *vsp)
{
	vsp->vs_knext = vmp->vm_segfree;
	vmp->vm_segfree = vsp;
	vmp->vm_nsegfree++;
}


/*
 * Add vsp to the appropriate freelist, at the appropriate location,
 * keeping the freelist sorted by age.
 */

/*
 * return true when we continue the for loop in
 * vmem_freelist_insert_sort_by_time
 */
static inline bool
flist_sort_compare(bool newfirst,
    const vmem_seg_t *vhead,
    const vmem_seg_t *nextlist,
    vmem_seg_t *p, vmem_seg_t *to_insert)
{
	/*
	 * vsp is the segment we are inserting into the freelist
	 * p is a freelist poniter or an element inside a  non-empty freelist
	 * if we return false, then vsp is inserted immedaitely after p,
	 */

	// always enter the for loop if we're at the front of a flist
	if (p == vhead)
		return (true);

	const vmem_seg_t *n = p->vs_knext;

	if (n == nextlist || n == NULL) {
		// if we are at the tail of the flist, then
		// insert vsp between p and n
		return (false);
	}

	if (n->vs_import == true && to_insert->vs_import == false) {
		/*
		 * put non-imported segments before imported segments
		 * no matter what their respective create times are,
		 * thereby making imported segments more likely "age out"
		 */
		return (false);  // inserts to_insert between p and n
	}

	if (newfirst == true) {
		if (n->vs_span_createtime < to_insert->vs_span_createtime) {
			// n is older than me, so insert me between p and n
			return (false);
		}
	} else {
		if (n->vs_span_createtime > to_insert->vs_span_createtime) {
			// n is newer than me, so insert me between p and n
			return (false);
		}
	}
	// continue iterating
	return (true);
}

static void
vmem_freelist_insert_sort_by_time(vmem_t *vmp, vmem_seg_t *vsp)
{
	ASSERT(vmp->vm_cflags & VMC_TIMEFREE);
	ASSERT(vsp->vs_span_createtime > 0);

	const bool newfirst = 0 == (vmp->vm_cflags & VMC_OLDFIRST);

	const uint64_t abs_max_walk_steps = 1ULL << 30ULL;
	uint32_t max_walk_steps = (uint32_t)MIN(spl_frag_max_walk,
	    abs_max_walk_steps);

	vmem_seg_t *vprev;

	ASSERT(*VMEM_HASH(vmp, vsp->vs_start) != vsp);

	/*
	 * in vmem_create_common() the freelists are arranged:
	 * freelist[0].vs_kprev = NULL, freelist[VMEM_FREELISTS].vs_knext = NULL
	 * freelist[1].vs_kprev = freelist[0], freelist[1].vs_knext =
	 *		freelist[2] ...
	 * from vmem_freelist_insert():
	 * VS_SIZE is the segment size (->vs_end - ->vs_start), so say 8k-512
	 * highbit is the higest bit set PLUS 1, so in this case would be the
	 * 16k list. so below, vprev is therefore pointing to the 8k list
	 * in vmem_alloc_impl, the unconstrained allocation takes, for a 8k-512
	 * block: vsp = flist[8k].vs_knext
	 * and calls vmem_seg_create() which sends any leftovers from vsp
	 * to vmem_freelist_insert
	 *
	 * vmem_freelist_insert would take the seg (as above, 8k-512 size),
	 * vprev points to the 16k list, and VMEM_INSERT(vprev, vsp, k)
	 * inserts the segment immediately after
	 *
	 * so vmem_seg_create(...8k-512...) pushes to the head of the 8k list,
	 * and vmem_alloc_impl(...8-512k...) will pull from the head of
	 * the 8k list
	 *
	 * below we may want to push to the TAIL of the 8k list, which is
	 * just before flist[16k].
	 */

	vprev = (vmem_seg_t *)&vmp->vm_freelist[highbit(VS_SIZE(vsp)) - 1];

	int my_listnum = highbit(VS_SIZE(vsp)) - 1;

	ASSERT(my_listnum >= 1);
	ASSERT(my_listnum < VMEM_FREELISTS);

	int next_listnum = my_listnum + 1;

	const vmem_seg_t *nextlist =
	    (vmem_seg_t *)&vmp->vm_freelist[next_listnum];

	ASSERT(vsp->vs_span_createtime != 0);
	if (vsp->vs_span_createtime == 0) {
		printf("SPL: %s: WARNING: vsp->vs_span_createtime == 0 (%s)!\n",
		    __func__, vmp->vm_name);
	}

	// continuing our example, starts with p at flist[8k]
	// and n at the following freelist entry

	const vmem_seg_t *vhead = vprev;
	vmem_seg_t *p = vprev;
	vmem_seg_t *n = p->vs_knext;

	// walk from the freelist head looking for
	// a segment whose creation time is earlier than
	// the segment to be inserted's creation time,
	// then insert before that segment.

	for (uint32_t step = 0;
	    flist_sort_compare(newfirst, vhead, nextlist, p, vsp) == true;
	    step++) {
		// iterating while predecessor pointer p was created
		// at a later tick than funcarg vsp.
		//
		// below we set p to n and update n.
		ASSERT(n != NULL);
		if (n == nextlist) {
			dprintf("SPL: %s: at marker (%s)(steps: %u) "
			    "p->vs_start, end == %lu, %lu\n",
			    __func__, vmp->vm_name, step,
			    (uintptr_t)p->vs_start, (uintptr_t)p->vs_end);
			// IOSleep(1);
			// the next entry is the next marker (e.g. 16k marker)
			break;
		}
		if (n->vs_start == 0) {
			// from vmem_freelist_delete, this is a head
			dprintf("SPL: %s: n->vs_start == 0 (%s)(steps: %u) "
			    "p->vs_start, end == %lu, %lu\n",
			    __func__, vmp->vm_name, step,
			    (uintptr_t)p->vs_start, (uintptr_t)p->vs_end);
			// IOSleep(1);
			break;
		}
		if (step >= max_walk_steps) {
			ASSERT(nextlist->vs_kprev != NULL);
			// we have walked far enough.
			// put this segment at the tail of the freelist.
			if (nextlist->vs_kprev != NULL) {
				n = (vmem_seg_t *)nextlist;
				p = nextlist->vs_kprev;
			}
			dprintf("SPL: %s: walked out (%s)\n", __func__,
			    vmp->vm_name);
			// IOSleep(1);
			atomic_inc_64(&spl_frag_walked_out);
			break;
		}
		if (n->vs_knext == NULL) {
			dprintf("SPL: %s: n->vs_knext == NULL (my_listnum "
			    "== %d)\n", __func__, my_listnum);
			// IOSleep(1);
			break;
		}
		p = n;
		n = n->vs_knext;
		atomic_inc_64(&spl_frag_walk_cnt);
	}

	ASSERT(p != NULL);

	// insert segment between p and n

	vsp->vs_type = VMEM_FREE;
	vmp->vm_freemap |= VS_SIZE(vprev);
	VMEM_INSERT(p, vsp, k);

	cv_broadcast(&vmp->vm_cv);
}

/*
 * Add vsp to the appropriate freelist.
 */
static void
vmem_freelist_insert(vmem_t *vmp, vmem_seg_t *vsp)
{

	if (vmp->vm_cflags & VMC_TIMEFREE) {
		vmem_freelist_insert_sort_by_time(vmp, vsp);
		return;
	}

	vmem_seg_t *vprev;

	ASSERT(*VMEM_HASH(vmp, vsp->vs_start) != vsp);

	vprev = (vmem_seg_t *)&vmp->vm_freelist[highbit(VS_SIZE(vsp)) - 1];
	vsp->vs_type = VMEM_FREE;
	vmp->vm_freemap |= VS_SIZE(vprev);
	VMEM_INSERT(vprev, vsp, k);

	cv_broadcast(&vmp->vm_cv);
}

/*
 * Take vsp from the freelist.
 */
static void
vmem_freelist_delete(vmem_t *vmp, vmem_seg_t *vsp)
{
	ASSERT(*VMEM_HASH(vmp, vsp->vs_start) != vsp);
	ASSERT(vsp->vs_type == VMEM_FREE);

	if (vsp->vs_knext->vs_start == 0 && vsp->vs_kprev->vs_start == 0) {
		/*
		 * The segments on both sides of 'vsp' are freelist heads,
		 * so taking vsp leaves the freelist at vsp->vs_kprev empty.
		 */
		ASSERT(vmp->vm_freemap & VS_SIZE(vsp->vs_kprev));
		vmp->vm_freemap ^= VS_SIZE(vsp->vs_kprev);
	}
	VMEM_DELETE(vsp, k);
}

/*
 * Add vsp to the allocated-segment hash table and update kstats.
 */
static void
vmem_hash_insert(vmem_t *vmp, vmem_seg_t *vsp)
{
	vmem_seg_t **bucket;

	vsp->vs_type = VMEM_ALLOC;
	bucket = VMEM_HASH(vmp, vsp->vs_start);
	vsp->vs_knext = *bucket;
	*bucket = vsp;

	if (vmem_seg_size == sizeof (vmem_seg_t)) {
		// vsp->vs_depth = (uint8_t)getpcstack(vsp->vs_stack,
		//		VMEM_STACK_DEPTH);
		// vsp->vs_thread = curthread;
		vsp->vs_depth = 0;
		vsp->vs_thread = 0;
		vsp->vs_timestamp = gethrtime();
	} else {
		vsp->vs_depth = 0;
	}

	vmp->vm_kstat.vk_alloc.value.ui64++;
	vmp->vm_kstat.vk_mem_inuse.value.ui64 += VS_SIZE(vsp);
}

/*
 * Remove vsp from the allocated-segment hash table and update kstats.
 */
static vmem_seg_t *
vmem_hash_delete(vmem_t *vmp, uintptr_t addr, size_t size)
{
	vmem_seg_t *vsp, **prev_vspp;

	prev_vspp = VMEM_HASH(vmp, addr);
	while ((vsp = *prev_vspp) != NULL) {
		if (vsp->vs_start == addr) {
			*prev_vspp = vsp->vs_knext;
			break;
		}
		vmp->vm_kstat.vk_lookup.value.ui64++;
		prev_vspp = &vsp->vs_knext;
	}

	if (vsp == NULL)
		panic("vmem_hash_delete(%p, %lx, %lu): bad free "
		    "(name: %s, addr, size)",
		    (void *)vmp, addr, size, vmp->vm_name);
	if (VS_SIZE(vsp) != size)
		panic("vmem_hash_delete(%p, %lx, %lu): (%s) wrong size"
		    "(expect %lu)",
		    (void *)vmp, addr, size, vmp->vm_name, VS_SIZE(vsp));

	vmp->vm_kstat.vk_free.value.ui64++;
	vmp->vm_kstat.vk_mem_inuse.value.ui64 -= size;

	return (vsp);
}

/*
 * Create a segment spanning the range [start, end) and add it to the arena.
 */
static vmem_seg_t *
vmem_seg_create(vmem_t *vmp, vmem_seg_t *vprev, uintptr_t start, uintptr_t end)
{
	vmem_seg_t *newseg = vmem_getseg(vmp);

	newseg->vs_start = start;
	newseg->vs_end = end;
	newseg->vs_type = 0;
	newseg->vs_import = 0;
	newseg->vs_span_createtime = 0;

	VMEM_INSERT(vprev, newseg, a);

	return (newseg);
}

/*
 * Remove segment vsp from the arena.
 */
static inline void
vmem_seg_destroy(vmem_t *vmp, vmem_seg_t *vsp)
{
	ASSERT(vsp->vs_type != VMEM_ROTOR);
	VMEM_DELETE(vsp, a);

	vmem_putseg(vmp, vsp);
}

/*
 * Add the span [vaddr, vaddr + size) to vmp and update kstats.
 */
static vmem_seg_t *
vmem_span_create(vmem_t *vmp, void *vaddr, size_t size, uint8_t import)
{
	vmem_seg_t *newseg, *span;
	uintptr_t start = (uintptr_t)vaddr;
	uintptr_t end = start + size;

	ASSERT(MUTEX_HELD(&vmp->vm_lock));
	if ((start | end) & (vmp->vm_quantum - 1))
		panic("vmem_span_create(%p, %p, %lu): misaligned (%s)",
		    (void *)vmp, vaddr, size, vmp->vm_name);

	span = vmem_seg_create(vmp, vmp->vm_seg0.vs_aprev, start, end);
	span->vs_type = VMEM_SPAN;
	span->vs_import = import;

	hrtime_t t = 0;
	if (vmp->vm_cflags & VMC_TIMEFREE) {
		t = gethrtime();
	}
	span->vs_span_createtime = t;

	VMEM_INSERT(vmp->vm_seg0.vs_kprev, span, k);

	newseg = vmem_seg_create(vmp, span, start, end);
	newseg->vs_span_createtime = t;

	vmem_freelist_insert(vmp, newseg);

	if (import)
		vmp->vm_kstat.vk_mem_import.value.ui64 += size;
	vmp->vm_kstat.vk_mem_total.value.ui64 += size;

	return (newseg);
}

/*
 * Remove span vsp from vmp and update kstats.
 */
static void
vmem_span_destroy(vmem_t *vmp, vmem_seg_t *vsp)
{
	vmem_seg_t *span = vsp->vs_aprev;
	size_t size = VS_SIZE(vsp);

	ASSERT(MUTEX_HELD(&vmp->vm_lock));
	ASSERT(span->vs_type == VMEM_SPAN);

	if (span->vs_import)
		vmp->vm_kstat.vk_mem_import.value.ui64 -= size;
	vmp->vm_kstat.vk_mem_total.value.ui64 -= size;

	VMEM_DELETE(span, k);

	vmem_seg_destroy(vmp, vsp);
	vmem_seg_destroy(vmp, span);
}

/*
 * Allocate the subrange [addr, addr + size) from segment vsp.
 * If there are leftovers on either side, place them on the freelist.
 * Returns a pointer to the segment representing [addr, addr + size).
 */
static vmem_seg_t *
vmem_seg_alloc(vmem_t *vmp, vmem_seg_t *vsp, uintptr_t addr, size_t size)
{
	uintptr_t vs_start = vsp->vs_start;
	uintptr_t vs_end = vsp->vs_end;
	size_t vs_size = vs_end - vs_start;
	size_t realsize = P2ROUNDUP(size, vmp->vm_quantum);
	uintptr_t addr_end = addr + realsize;

	ASSERT(P2PHASE(vs_start, vmp->vm_quantum) == 0);
	ASSERT(P2PHASE(addr, vmp->vm_quantum) == 0);
	ASSERT(vsp->vs_type == VMEM_FREE);
	ASSERT(addr >= vs_start && addr_end - 1 <= vs_end - 1);
	ASSERT(addr - 1 <= addr_end - 1);

	hrtime_t parent_seg_span_createtime = vsp->vs_span_createtime;

	/*
	 * If we're allocating from the start of the segment, and the
	 * remainder will be on the same freelist, we can save quite
	 * a bit of work.
	 */
	if (P2SAMEHIGHBIT(vs_size, vs_size - realsize) && addr == vs_start) {
		ASSERT(highbit(vs_size) == highbit(vs_size - realsize));
		vsp->vs_start = addr_end;
		vsp = vmem_seg_create(vmp, vsp->vs_aprev, addr, addr + size);
		vsp->vs_span_createtime = parent_seg_span_createtime;
		vmem_hash_insert(vmp, vsp);
		return (vsp);
	}

	vmem_freelist_delete(vmp, vsp);

	if (vs_end != addr_end) {
		vmem_seg_t *v = vmem_seg_create(vmp, vsp, addr_end, vs_end);
		v->vs_span_createtime = parent_seg_span_createtime;
		vmem_freelist_insert(vmp, v);
	}

	if (vs_start != addr) {
		vmem_seg_t *v =
		    vmem_seg_create(vmp, vsp->vs_aprev, vs_start, addr);
		v->vs_span_createtime = parent_seg_span_createtime;
		vmem_freelist_insert(vmp, v);
	}

	vsp->vs_start = addr;
	vsp->vs_end = addr + size;

	vsp->vs_span_createtime = parent_seg_span_createtime;

	vmem_hash_insert(vmp, vsp);
	return (vsp);
}

/*
 * Returns 1 if we are populating, 0 otherwise.
 * Call it if we want to prevent recursion from HAT.
 */
inline int
vmem_is_populator()
{
	return (mutex_owner(&vmem_sleep_lock) == curthread ||
	    mutex_owner(&vmem_nosleep_lock) == curthread ||
	    mutex_owner(&vmem_pushpage_lock) == curthread ||
	    mutex_owner(&vmem_panic_lock) == curthread);
}

/*
 * Populate vmp's segfree list with VMEM_MINFREE vmem_seg_t structures.
 */
static int
vmem_populate(vmem_t *vmp, int vmflag)
{
	char *p;
	vmem_seg_t *vsp;
	ssize_t nseg;
	size_t size;
	kmutex_t *lp;
	int i;

	while (vmp->vm_nsegfree < VMEM_MINFREE &&
	    (vsp = vmem_getseg_global()) != NULL)
		vmem_putseg(vmp, vsp);

	if (vmp->vm_nsegfree >= VMEM_MINFREE)
		return (1);

	/*
	 * If we're already populating, tap the reserve.
	 */
	if (vmem_is_populator()) {
		ASSERT(vmp->vm_cflags & VMC_POPULATOR);
		return (1);
	}

	mutex_exit(&vmp->vm_lock);

	//	if (panic_thread == curthread)
	//		lp = &vmem_panic_lock;
	//	else

	if (vmflag & VM_NOSLEEP)
		lp = &vmem_nosleep_lock;
	else if (vmflag & VM_PUSHPAGE)
		lp = &vmem_pushpage_lock;
	else
		lp = &vmem_sleep_lock;

	mutex_enter(lp);

	nseg = VMEM_MINFREE + vmem_populators * VMEM_POPULATE_RESERVE;
	size = P2ROUNDUP(nseg * vmem_seg_size, vmem_seg_arena->vm_quantum);
	nseg = size / vmem_seg_size;

	/*
	 * The following vmem_alloc_impl() may need to populate vmem_seg_arena
	 * and all the things it imports from.  When doing so, it will tap
	 * each arena's reserve to prevent recursion (see the block comment
	 * above the definition of VMEM_POPULATE_RESERVE).
	 */
	p = vmem_alloc_impl(vmem_seg_arena, size, vmflag & VM_KMFLAGS);
	if (p == NULL) {
		mutex_exit(lp);
		mutex_enter(&vmp->vm_lock);
		vmp->vm_kstat.vk_populate_fail.value.ui64++;
		return (0);
	}

	/*
	 * Restock the arenas that may have been depleted during population.
	 */
	for (i = 0; i < vmem_populators; i++) {
		mutex_enter(&vmem_populator[i]->vm_lock);
		while (vmem_populator[i]->vm_nsegfree < VMEM_POPULATE_RESERVE)
			vmem_putseg(vmem_populator[i],
			    (vmem_seg_t *)(p + --nseg * vmem_seg_size));
		mutex_exit(&vmem_populator[i]->vm_lock);
	}

	mutex_exit(lp);
	mutex_enter(&vmp->vm_lock);

	/*
	 * Now take our own segments.
	 */
	ASSERT(nseg >= VMEM_MINFREE);
	while (vmp->vm_nsegfree < VMEM_MINFREE)
		vmem_putseg(vmp, (vmem_seg_t *)(p + --nseg * vmem_seg_size));

	/*
	 * Give the remainder to charity.
	 */
	while (nseg > 0)
		vmem_putseg_global((vmem_seg_t *)(p + --nseg * vmem_seg_size));

	return (1);
}

/*
 * Advance a walker from its previous position to 'afterme'.
 * Note: may drop and reacquire vmp->vm_lock.
 */
static void
vmem_advance(vmem_t *vmp, vmem_seg_t *walker, vmem_seg_t *afterme)
{
	vmem_seg_t *vprev = walker->vs_aprev;
	vmem_seg_t *vnext = walker->vs_anext;
	vmem_seg_t *vsp = NULL;

	VMEM_DELETE(walker, a);

	if (afterme != NULL)
		VMEM_INSERT(afterme, walker, a);

	/*
	 * The walker segment's presence may have prevented its neighbors
	 * from coalescing.  If so, coalesce them now.
	 */
	if (vprev->vs_type == VMEM_FREE) {
		if (vnext->vs_type == VMEM_FREE) {
			ASSERT(vprev->vs_end == vnext->vs_start);
			ASSERT(vprev->vs_span_createtime ==
			    vnext->vs_span_createtime);
			vmem_freelist_delete(vmp, vnext);
			vmem_freelist_delete(vmp, vprev);
			vprev->vs_end = vnext->vs_end;
			vmem_freelist_insert(vmp, vprev);
			vmem_seg_destroy(vmp, vnext);
		}
		vsp = vprev;
	} else if (vnext->vs_type == VMEM_FREE) {
		vsp = vnext;
	}

	/*
	 * vsp could represent a complete imported span,
	 * in which case we must return it to the source.
	 */
	if (vsp != NULL && vsp->vs_aprev->vs_import &&
	    vmp->vm_source_free != NULL &&
	    vsp->vs_aprev->vs_type == VMEM_SPAN &&
	    vsp->vs_anext->vs_type == VMEM_SPAN) {
		void *vaddr = (void *)vsp->vs_start;
		size_t size = VS_SIZE(vsp);
		ASSERT(size == VS_SIZE(vsp->vs_aprev));
		vmem_freelist_delete(vmp, vsp);
		vmem_span_destroy(vmp, vsp);
		vmp->vm_kstat.vk_parent_free.value.ui64++;
		mutex_exit(&vmp->vm_lock);
		vmp->vm_source_free(vmp->vm_source, vaddr, size);
		mutex_enter(&vmp->vm_lock);
	}
}

/*
 * VM_NEXTFIT allocations deliberately cycle through all virtual addresses
 * in an arena, so that we avoid reusing addresses for as long as possible.
 * This helps to catch used-after-freed bugs.  It's also the perfect policy
 * for allocating things like process IDs, where we want to cycle through
 * all values in order.
 */
static void *
vmem_nextfit_alloc(vmem_t *vmp, size_t size, int vmflag)
{
	vmem_seg_t *vsp, *rotor;
	uintptr_t addr;
	size_t realsize = P2ROUNDUP(size, vmp->vm_quantum);
	size_t vs_size;

	mutex_enter(&vmp->vm_lock);

	if (vmp->vm_nsegfree < VMEM_MINFREE && !vmem_populate(vmp, vmflag)) {
		mutex_exit(&vmp->vm_lock);
		return (NULL);
	}

	/*
	 * The common case is that the segment right after the rotor is free,
	 * and large enough that extracting 'size' bytes won't change which
	 * freelist it's on.  In this case we can avoid a *lot* of work.
	 * Instead of the normal vmem_seg_alloc(), we just advance the start
	 * address of the victim segment.  Instead of moving the rotor, we
	 * create the new segment structure *behind the rotor*, which has
	 * the same effect.  And finally, we know we don't have to coalesce
	 * the rotor's neighbors because the new segment lies between them.
	 */
	rotor = &vmp->vm_rotor;
	vsp = rotor->vs_anext;
	if (vsp->vs_type == VMEM_FREE && (vs_size = VS_SIZE(vsp)) > realsize &&
	    P2SAMEHIGHBIT(vs_size, vs_size - realsize)) {
		ASSERT(highbit(vs_size) == highbit(vs_size - realsize));
		addr = vsp->vs_start;
		vsp->vs_start = addr + realsize;
		hrtime_t t = vsp->vs_span_createtime;
		vmem_hash_insert(vmp,
		    vmem_seg_create(vmp, rotor->vs_aprev, addr, addr + size));
		vsp->vs_span_createtime = t;
		mutex_exit(&vmp->vm_lock);
		return ((void *)addr);
	}

	/*
	 * Starting at the rotor, look for a segment large enough to
	 * satisfy the allocation.
	 */
	for (;;) {
		atomic_inc_64(&vmp->vm_kstat.vk_search.value.ui64);
		if (vsp->vs_type == VMEM_FREE && VS_SIZE(vsp) >= size)
			break;
		vsp = vsp->vs_anext;
		if (vsp == rotor) {
			/*
			 * We've come full circle.  One possibility is that the
			 * there's actually enough space, but the rotor itself
			 * is preventing the allocation from succeeding because
			 * it's sitting between two free segments.  Therefore,
			 * we advance the rotor and see if that liberates a
			 * suitable segment.
			 */
			vmem_advance(vmp, rotor, rotor->vs_anext);
			vsp = rotor->vs_aprev;
			if (vsp->vs_type == VMEM_FREE && VS_SIZE(vsp) >= size)
				break;
			/*
			 * If there's a lower arena we can import from, or it's
			 * a VM_NOSLEEP allocation, let vmem_xalloc() handle it.
			 * Otherwise, wait until another thread frees something.
			 */
			if (vmp->vm_source_alloc != NULL ||
			    (vmflag & VM_NOSLEEP)) {
				mutex_exit(&vmp->vm_lock);
				return (vmem_xalloc(vmp, size, vmp->vm_quantum,
				    0, 0, NULL, NULL,
				    vmflag & (VM_KMFLAGS | VM_NEXTFIT)));
			}
			atomic_inc_64(&vmp->vm_kstat.vk_wait.value.ui64);
			atomic_inc_64(
			    &vmp->vm_kstat.vk_threads_waiting.value.ui64);
			atomic_inc_64(&spl_vmem_threads_waiting);
			if (spl_vmem_threads_waiting > 1)
				dprintf("SPL: %s: waiting for %lu sized alloc "
				    "after full circle of  %s, waiting "
				    "threads %llu, total threads waiting "
				    "= %llu.\n",
				    __func__, size, vmp->vm_name,
				    vmp->vm_kstat.vk_threads_waiting.value.ui64,
				    spl_vmem_threads_waiting);
			cv_wait(&vmp->vm_cv, &vmp->vm_lock);
			atomic_dec_64(&spl_vmem_threads_waiting);
			atomic_dec_64(
			    &vmp->vm_kstat.vk_threads_waiting.value.ui64);
			vsp = rotor->vs_anext;
		}
	}

	/*
	 * We found a segment.  Extract enough space to satisfy the allocation.
	 */
	addr = vsp->vs_start;
	vsp = vmem_seg_alloc(vmp, vsp, addr, size);
	ASSERT(vsp->vs_type == VMEM_ALLOC &&
	    vsp->vs_start == addr && vsp->vs_end == addr + size);

	/*
	 * Advance the rotor to right after the newly-allocated segment.
	 * That's where the next VM_NEXTFIT allocation will begin searching.
	 */
	vmem_advance(vmp, rotor, vsp);
	mutex_exit(&vmp->vm_lock);
	return ((void *)addr);
}

/*
 * Checks if vmp is guaranteed to have a size-byte buffer somewhere on its
 * freelist.  If size is not a power-of-2, it can return a false-negative.
 *
 * Used to decide if a newly imported span is superfluous after re-acquiring
 * the arena lock.
 */
static inline int
vmem_canalloc(vmem_t *vmp, size_t size)
{
	int hb;
	int flist = 0;
	ASSERT(MUTEX_HELD(&vmp->vm_lock));

	if ((size & (size - 1)) == 0)
		flist = lowbit(P2ALIGN(vmp->vm_freemap, size));
	else if ((hb = highbit(size)) < VMEM_FREELISTS)
		flist = lowbit(P2ALIGN(vmp->vm_freemap, 1ULL << hb));

	return (flist);
}

// Convenience functions for use when gauging
// allocation ability when not holding the lock.
// These are unreliable because vmp->vm_freemap is
// liable to change immediately after being examined.
inline int
vmem_canalloc_lock(vmem_t *vmp, size_t size)
{
	mutex_enter(&vmp->vm_lock);
	int i = vmem_canalloc(vmp, size);
	mutex_exit(&vmp->vm_lock);
	return (i);
}

int
vmem_canalloc_atomic(vmem_t *vmp, size_t size)
{
	int hb;
	int flist = 0;

	ulong_t freemap =
	    __c11_atomic_load((_Atomic ulong_t *)&vmp->vm_freemap,
	    __ATOMIC_SEQ_CST);

	if (ISP2(size))
		flist = lowbit(P2ALIGN(freemap, size));
	else if ((hb = highbit(size)) < VMEM_FREELISTS)
		flist = lowbit(P2ALIGN(freemap, 1ULL << hb));

	return (flist);
}

uint64_t
spl_vmem_xnu_useful_bytes_free(void)
{
	extern _Atomic uint32_t spl_vm_pages_reclaimed;
	extern _Atomic uint32_t spl_vm_pages_wanted;
	extern _Atomic uint32_t spl_vm_pressure_level;

	/* carve out a small reserve for unconditional allocs */
	const uint64_t reserve = total_memory >> 9ULL;
	const uint64_t total_minus_reserve = total_memory - reserve;

	/*
	 * pages are wanted *and* we are in our reserve area,
	 * so we report only one page of "usable" memory.
	 *
	 * if we are below the reserve, return the amount left
	 */

	if (spl_vm_pages_wanted > 0) {
		if (segkmem_total_mem_allocated >= total_minus_reserve)
			return (PAGE_SIZE * MAX(spl_vm_pages_reclaimed, 1));
		else
			return (total_minus_reserve -
			    (segkmem_total_mem_allocated +
			    PAGE_SIZE * spl_vm_pages_reclaimed));
	}

	/*
	 * If there is pressure, and we are in the reserve area,
	 * then there is no "usable" memory, unless we have reclaimed
	 * some pages.
	 *
	 * beware of large magic guard values,
	 * the pressure enum only goes to 4.
	 */

	if (spl_vm_pressure_level > 0 &&
	    spl_vm_pressure_level < 100) {
		if (spl_vm_pages_reclaimed > 0)
			return (PAGE_SIZE * spl_vm_pages_reclaimed);
		else if (segkmem_total_mem_allocated < total_minus_reserve)
			return (PAGE_SIZE);
		else
			return (0);
	}

	/*
	 * No pressure: return non-reserved bytes not allocated.
	 * The reserve may be needed for VM_NOWAIT and VM_PANIC flags.
	 */

	return (total_minus_reserve - segkmem_total_mem_allocated);
}

uint64_t
vmem_xnu_useful_bytes_free(void)
{
	return (spl_vmem_xnu_useful_bytes_free());
}


static inline void *
spl_vmem_malloc_unconditionally_unlocked(size_t size)
{
	extern void *osif_malloc(uint64_t);
	atomic_inc_64(&spl_vmem_unconditional_allocs);
	atomic_add_64(&spl_vmem_unconditional_alloc_bytes, size);
	return (osif_malloc(size));
}

/*
 * Allocate size bytes at offset phase from an align boundary such that the
 * resulting segment [addr, addr + size) is a subset of [minaddr, maxaddr)
 * that does not straddle a nocross-aligned boundary.
 */
inline void *
vmem_xalloc(vmem_t *vmp, size_t size, size_t align_arg, size_t phase,
    size_t nocross, void *minaddr, void *maxaddr, int vmflag)
{
	vmem_seg_t *vsp;
	vmem_seg_t *vbest = NULL;
	uintptr_t addr = 0, taddr, start, end;
	uintptr_t align = (align_arg != 0) ? align_arg : vmp->vm_quantum;
	void *vaddr, *xvaddr = NULL;
	size_t xsize = 0;
	int hb, flist, resv;
	uint32_t mtbf;

	if ((align | phase | nocross) & (vmp->vm_quantum - 1))
		panic("vmem_xalloc(%p, %lu, %lu, %lu, %lu, %p, %p, %x): "
		    "parameters not vm_quantum aligned",
		    (void *)vmp, size, align_arg, phase, nocross,
		    minaddr, maxaddr, vmflag);

	if (nocross != 0 &&
	    (align > nocross || P2ROUNDUP(phase + size, align) > nocross))
		panic("vmem_xalloc(%p, %lu, %lu, %lu, %lu, %p, %p, %x): "
		    "overconstrained allocation",
		    (void *)vmp, size, align_arg, phase, nocross,
		    minaddr, maxaddr, vmflag);

	if (phase >= align || (align & (align - 1)) != 0 ||
	    (nocross & (nocross - 1)) != 0)
		panic("vmem_xalloc(%p, %lu, %lu, %lu, %lu, %p, %p, %x): "
		    "parameters inconsistent or invalid",
		    (void *)vmp, size, align_arg, phase, nocross,
		    minaddr, maxaddr, vmflag);

	if ((mtbf = vmem_mtbf | vmp->vm_mtbf) != 0 && gethrtime() % mtbf == 0 &&
	    (vmflag & (VM_NOSLEEP | VM_PANIC)) == VM_NOSLEEP)
		return (NULL);

	mutex_enter(&vmp->vm_lock);
	for (;;) {
		if (vmp->vm_nsegfree < VMEM_MINFREE &&
		    !vmem_populate(vmp, vmflag))
			break;
do_alloc:
		/*
		 * highbit() returns the highest bit + 1, which is exactly
		 * what we want: we want to search the first freelist whose
		 * members are *definitely* large enough to satisfy our
		 * allocation.  However, there are certain cases in which we
		 * want to look at the next-smallest freelist (which *might*
		 * be able to satisfy the allocation):
		 *
		 * (1)	The size is exactly a power of 2, in which case
		 *	the smaller freelist is always big enough;
		 *
		 * (2)	All other freelists are empty;
		 *
		 * (3)	We're in the highest possible freelist, which is
		 *	always empty (e.g. the 4GB freelist on 32-bit systems);
		 *
		 * (4)	We're doing a best-fit or first-fit allocation.
		 */
		if ((size & (size - 1)) == 0) {
			flist = lowbit(P2ALIGN(vmp->vm_freemap, size));
		} else {
			hb = highbit(size);
			if ((vmp->vm_freemap >> hb) == 0 ||
			    hb == VMEM_FREELISTS ||
			    (vmflag & (VM_BESTFIT | VM_FIRSTFIT)))
				hb--;
			flist = lowbit(P2ALIGN(vmp->vm_freemap, 1UL << hb));
		}

		for (vbest = NULL, vsp = (flist == 0) ? NULL :
		    vmp->vm_freelist[flist - 1].vs_knext;
		    vsp != NULL; vsp = vsp->vs_knext) {
			atomic_inc_64(&vmp->vm_kstat.vk_search.value.ui64);
			if (vsp->vs_start == 0) {
				/*
				 * We're moving up to a larger freelist,
				 * so if we've already found a candidate,
				 * the fit can't possibly get any better.
				 */
				if (vbest != NULL)
					break;
				/*
				 * Find the next non-empty freelist.
				 */
				flist = lowbit(P2ALIGN(vmp->vm_freemap,
				    VS_SIZE(vsp)));
				if (flist-- == 0)
					break;
				vsp = (vmem_seg_t *)&vmp->vm_freelist[flist];
				ASSERT(vsp->vs_knext->vs_type == VMEM_FREE);
				continue;
			}
			if (vsp->vs_end - 1 < (uintptr_t)minaddr)
				continue;
			if (vsp->vs_start > (uintptr_t)maxaddr - 1)
				continue;
			start = MAX(vsp->vs_start, (uintptr_t)minaddr);
			end = MIN(vsp->vs_end - 1, (uintptr_t)maxaddr - 1) + 1;
			taddr = P2PHASEUP(start, align, phase);
			if (P2BOUNDARY(taddr, size, nocross))
				taddr +=
				    P2ROUNDUP(P2NPHASE(taddr, nocross), align);
			if ((taddr - start) + size > end - start ||
			    (vbest != NULL && VS_SIZE(vsp) >= VS_SIZE(vbest)))
				continue;
			vbest = vsp;
			addr = taddr;
			if (!(vmflag & VM_BESTFIT) || VS_SIZE(vbest) == size)
				break;
		}
		if (vbest != NULL)
			break;
		ASSERT(xvaddr == NULL);
		if (size == 0)
			panic("vmem_xalloc(): size == 0");
		if (vmp->vm_source_alloc != NULL && nocross == 0 &&
		    minaddr == NULL && maxaddr == NULL) {
			size_t aneeded, asize;
			size_t aquantum = MAX(vmp->vm_quantum,
			    vmp->vm_source->vm_quantum);
			size_t aphase = phase;
			if ((align > aquantum) &&
			    !(vmp->vm_cflags & VMC_XALIGN)) {
				aphase = (P2PHASE(phase, aquantum) != 0) ?
				    align - vmp->vm_quantum : align - aquantum;
				ASSERT(aphase >= phase);
			}
			aneeded = MAX(size + aphase, vmp->vm_min_import);
			asize = P2ROUNDUP(aneeded, aquantum);

			if (asize < size) {
				/*
				 * The rounding induced overflow; return NULL
				 * if we are permitted to fail the allocation
				 * (and explicitly panic if we aren't).
				 */
				if ((vmflag & VM_NOSLEEP) &&
				    !(vmflag & VM_PANIC)) {
					mutex_exit(&vmp->vm_lock);
					return (NULL);
				}

				panic("vmem_xalloc(): size overflow");
			}

			/*
			 * Determine how many segment structures we'll consume.
			 * The calculation must be precise because if we're
			 * here on behalf of vmem_populate(), we are taking
			 * segments from a very limited reserve.
			 */
			if (size == asize && !(vmp->vm_cflags & VMC_XALLOC))
				resv = VMEM_SEGS_PER_SPAN_CREATE +
				    VMEM_SEGS_PER_EXACT_ALLOC;
			else if (phase == 0 &&
			    align <= vmp->vm_source->vm_quantum)
				resv = VMEM_SEGS_PER_SPAN_CREATE +
				    VMEM_SEGS_PER_LEFT_ALLOC;
			else
				resv = VMEM_SEGS_PER_ALLOC_MAX;

			ASSERT(vmp->vm_nsegfree >= resv);
			vmp->vm_nsegfree -= resv;	/* reserve our segs */
			mutex_exit(&vmp->vm_lock);
			if (vmp->vm_cflags & VMC_XALLOC) {
				ASSERTV(size_t oasize = asize);
				vaddr = ((vmem_ximport_t *)
				    vmp->vm_source_alloc)(vmp->vm_source,
				    &asize, align, vmflag & VM_KMFLAGS);
				ASSERT(asize >= oasize);
				ASSERT(P2PHASE(asize,
				    vmp->vm_source->vm_quantum) == 0);
				ASSERT(!(vmp->vm_cflags & VMC_XALIGN) ||
				    IS_P2ALIGNED(vaddr, align));
			} else {
				atomic_inc_64(
				    &vmp->vm_kstat.vk_parent_alloc.value.ui64);
				vaddr = vmp->vm_source_alloc(vmp->vm_source,
				    asize, vmflag & (VM_KMFLAGS | VM_NEXTFIT));
			}
			mutex_enter(&vmp->vm_lock);
			vmp->vm_nsegfree += resv;	/* claim reservation */
			aneeded = size + align - vmp->vm_quantum;
			aneeded = P2ROUNDUP(aneeded, vmp->vm_quantum);
			if (vaddr != NULL) {
				/*
				 * Since we dropped the vmem lock while
				 * calling the import function, other
				 * threads could have imported space
				 * and made our import unnecessary.  In
				 * order to save space, we return
				 * excess imports immediately.
				 */
				// but if there are threads waiting below,
				// do not return the excess import, rather
				// wake those threads up so they can use it.
				if (asize > aneeded &&
				    vmp->vm_source_free != NULL &&
				    vmp->vm_kstat.vk_threads_waiting.value.ui64
				    == 0 && vmem_canalloc(vmp, aneeded)) {
					ASSERT(resv >=
					    VMEM_SEGS_PER_MIDDLE_ALLOC);
					xvaddr = vaddr;
					xsize = asize;
					goto do_alloc;
				} else if (
				    vmp->vm_kstat.vk_threads_waiting.value.ui64
				    > 0) {
					vmp->vm_kstat.vk_excess.value.ui64++;
					cv_broadcast(&vmp->vm_cv);
				}
				vbest = vmem_span_create(vmp, vaddr, asize, 1);
				addr = P2PHASEUP(vbest->vs_start, align, phase);
				break;
			} else if (vmem_canalloc(vmp, aneeded)) {
				/*
				 * Our import failed, but another thread
				 * added sufficient free memory to the arena
				 * to satisfy our request.  Go back and
				 * grab it.
				 */
				ASSERT(resv >= VMEM_SEGS_PER_MIDDLE_ALLOC);
				goto do_alloc;
			}
		}

		/*
		 * If the requestor chooses to fail the allocation attempt
		 * rather than reap wait and retry - get out of the loop.
		 */
		if (vmflag & VM_ABORT)
			break;
		mutex_exit(&vmp->vm_lock);

#if 0
		if (vmp->vm_cflags & VMC_IDENTIFIER)
			kmem_reap_idspace();
		else
			kmem_reap();
#endif

		mutex_enter(&vmp->vm_lock);
		if (vmflag & VM_NOSLEEP)
			break;
		atomic_inc_64(&vmp->vm_kstat.vk_wait.value.ui64);
		atomic_inc_64(&vmp->vm_kstat.vk_threads_waiting.value.ui64);
		atomic_inc_64(&spl_vmem_threads_waiting);
		if (spl_vmem_threads_waiting > 0) {
			dprintf("SPL: %s: vmem waiting for %lu sized alloc "
			    "for %s, waiting threads %llu, total threads "
			    "waiting = %llu\n",
			    __func__, size, vmp->vm_name,
			    vmp->vm_kstat.vk_threads_waiting.value.ui64,
			    spl_vmem_threads_waiting);
			extern int64_t spl_free_set_and_wait_pressure(int64_t,
			    boolean_t, clock_t);
			extern int64_t spl_free_manual_pressure_wrapper(void);
			mutex_exit(&vmp->vm_lock);
			// release other waiting threads
			spl_free_set_pressure(0);
			int64_t target_pressure = size *
			    spl_vmem_threads_waiting;
			int64_t delivered_pressure =
			    spl_free_set_and_wait_pressure(target_pressure,
			    TRUE, USEC2NSEC(500));
			dprintf("SPL: %s: pressure %lld targeted, %lld "
			    "delivered\n", __func__, target_pressure,
			    delivered_pressure);
			mutex_enter(&vmp->vm_lock);
		}
		cv_wait(&vmp->vm_cv, &vmp->vm_lock);
		atomic_dec_64(&spl_vmem_threads_waiting);
		atomic_dec_64(&vmp->vm_kstat.vk_threads_waiting.value.ui64);
	}
	if (vbest != NULL) {
		ASSERT(vbest->vs_type == VMEM_FREE);
		ASSERT(vbest->vs_knext != vbest);
		/* re-position to end of buffer */
		if (vmflag & VM_ENDALLOC) {
			addr += ((vbest->vs_end - (addr + size)) / align) *
			    align;
		}
		(void) vmem_seg_alloc(vmp, vbest, addr, size);
		mutex_exit(&vmp->vm_lock);
		if (xvaddr) {
			atomic_inc_64(&vmp->vm_kstat.vk_parent_free.value.ui64);
			vmp->vm_source_free(vmp->vm_source, xvaddr, xsize);
		}
		ASSERT(P2PHASE(addr, align) == phase);
		ASSERT(!P2BOUNDARY(addr, size, nocross));
		ASSERT(addr >= (uintptr_t)minaddr);
		ASSERT(addr + size - 1 <= (uintptr_t)maxaddr - 1);
		return ((void *)addr);
	}
	if (0 == (vmflag & VM_NO_VBA)) {
		vmp->vm_kstat.vk_fail.value.ui64++;
	}
	mutex_exit(&vmp->vm_lock);
	if (vmflag & VM_PANIC)
		panic("vmem_xalloc(%p, %lu, %lu, %lu, %lu, %p, %p, %x): "
		    "cannot satisfy mandatory allocation",
		    (void *)vmp, size, align_arg, phase, nocross,
		    minaddr, maxaddr, vmflag);
	ASSERT(xvaddr == NULL);
	return (NULL);
}

/*
 * Free the segment [vaddr, vaddr + size), where vaddr was a constrained
 * allocation.  vmem_xalloc() and vmem_xfree() must always be paired because
 * both routines bypass the quantum caches.
 */
void
vmem_xfree(vmem_t *vmp, void *vaddr, size_t size)
{
	vmem_seg_t *vsp, *vnext, *vprev;

	mutex_enter(&vmp->vm_lock);

	vsp = vmem_hash_delete(vmp, (uintptr_t)vaddr, size);
	vsp->vs_end = P2ROUNDUP(vsp->vs_end, vmp->vm_quantum);

	/*
	 * Attempt to coalesce with the next segment.
	 */
	vnext = vsp->vs_anext;
	if (vnext->vs_type == VMEM_FREE) {
		ASSERT(vsp->vs_end == vnext->vs_start);
		vmem_freelist_delete(vmp, vnext);
		vsp->vs_end = vnext->vs_end;
		vmem_seg_destroy(vmp, vnext);
	}

	/*
	 * Attempt to coalesce with the previous segment.
	 */
	vprev = vsp->vs_aprev;
	if (vprev->vs_type == VMEM_FREE) {
		ASSERT(vprev->vs_end == vsp->vs_start);
		vmem_freelist_delete(vmp, vprev);
		vprev->vs_end = vsp->vs_end;
		vmem_seg_destroy(vmp, vsp);
		vsp = vprev;
	}

	/*
	 * If the entire span is free, return it to the source.
	 */
	if (vsp->vs_aprev->vs_import && vmp->vm_source_free != NULL &&
	    vsp->vs_aprev->vs_type == VMEM_SPAN &&
	    vsp->vs_anext->vs_type == VMEM_SPAN) {
		vaddr = (void *)vsp->vs_start;
		size = VS_SIZE(vsp);
		ASSERT(size == VS_SIZE(vsp->vs_aprev));
		vmem_span_destroy(vmp, vsp);
		vmp->vm_kstat.vk_parent_free.value.ui64++;
		mutex_exit(&vmp->vm_lock);
		vmp->vm_source_free(vmp->vm_source, vaddr, size);
	} else {
		vmem_freelist_insert(vmp, vsp);
		mutex_exit(&vmp->vm_lock);
	}
}

/*
 * vmem_alloc_impl() and auxiliary functions :
 *
 * Allocate size bytes from arena vmp.  Returns the allocated address
 * on success, NULL on failure.  vmflag specifies VM_SLEEP or VM_NOSLEEP,
 * and may also specify best-fit, first-fit, or next-fit allocation policy
 * instead of the default instant-fit policy.  VM_SLEEP allocations are
 * guaranteed to succeed.
 */

/*
 * If there is less space on the kernel stack than
 * (dynamically tunable) spl_split_stack_below
 * then perform the vmem_alloc in the thread_call
 * function. Don't set it to 16384, because then it
 * continuously triggers, and we hang.
 */
unsigned long spl_split_stack_below = 8192;

/* kstat tracking the global minimum free stack space */
_Atomic unsigned int spl_lowest_alloc_stack_remaining = UINT_MAX;

/* forward decls */
static inline void *wrapped_vmem_alloc_impl(vmem_t *, size_t, int);
static void *vmem_alloc_in_worker_thread(vmem_t *, size_t, int);

/*
 * unwrapped vmem_alloc_impl() :
 * Examine stack remaining; if it is less than our split stack below
 * threshold, or (for code coverage early near kext load time) is less than
 * the lowest we have seen call out to a worker thread that will
 * perform the wrapped_vmem_alloc_impl() and update stat counters.
 */
void *
vmem_alloc_impl(vmem_t *vmp, size_t size, int vmflag)
{
	const vm_offset_t r = OSKernelStackRemaining();

	if (vmp->vm_kstat.vk_lowest_stack.value.ui64 == 0) {
		vmp->vm_kstat.vk_lowest_stack.value.ui64 = r;
	} else if (vmp->vm_kstat.vk_lowest_stack.value.ui64 > r) {
		vmp->vm_kstat.vk_lowest_stack.value.ui64 = r;
	}

	if (vmem_is_populator()) {
		/*
		 * Current thread holds one of the vmem locks and the worker
		 * thread invoked in vmem_alloc_in_worker_thread() would
		 * therefore deadlock. vmem_populate on a vmem cache is an
		 * early (and rare) operation and typically does descend below
		 * the vmem source.
		 */
		return (wrapped_vmem_alloc_impl(vmp, size, vmflag));
	}

	if (r < spl_split_stack_below) {
		return (vmem_alloc_in_worker_thread(vmp, size, vmflag));
	}

	return (wrapped_vmem_alloc_impl(vmp, size, vmflag));
}

/*
 * Executes a wrapped_vmem_alloc_impl() in a kernel worker thread, which
 * will start with an essentially empty stack.  The stack above the
 * immediate client of the vmem_alloc_impl() that
 * has thread_enter1()-ed this function is already over a depth threshold.
 */
void
vmem_alloc_update_lowest_cb(thread_call_param_t param0,
    thread_call_param_t param1)
{

	/* param 0 is a vmp, set in vmem_create() */

	vmem_t *vmp = (vmem_t *)param0;
	cb_params_t *cbp = &vmp->vm_cb;

	VERIFY3U(cbp->in_child, ==, B_FALSE);

	/* tell the caller we are live */
	cbp->in_child = B_TRUE;
	__atomic_store_n(&cbp->in_child, B_TRUE, __ATOMIC_SEQ_CST);

	/* are we ever here after pending? */
	ASSERT0(cbp->already_pending);

	atomic_inc_64(&vmp->vm_kstat.vk_async_stack_calls.value.ui64);

	cbp->r_alloc = wrapped_vmem_alloc_impl(vmp,
	    cbp->size, cbp->vmflag);

	ASSERT3P(cbp->r_alloc, !=, NULL);

	/* indicate that we are done and wait for our caller */
	__atomic_store_n(&cbp->c_done, B_TRUE, __ATOMIC_SEQ_CST);
	/* from this point we cannot use param1, vmp, or cbp */

	mutex_enter(&vmp->vm_stack_lock);
	cv_signal(&vmp->vm_stack_cv);
	mutex_exit(&vmp->vm_stack_lock);
}

/*
 * Set up parameters and thread_enter1() to send them to a worker thread
 * executing vmem_alloc_update_lowest_cb().   Wait for the worker thread
 * to set c_done to nonzero.
 */
void *
vmem_alloc_in_worker_thread(vmem_t *vmp, size_t size, int vmflag)
{
	const vm_offset_t sr = OSKernelStackRemaining();

	if (sr < spl_lowest_alloc_stack_remaining)
		spl_lowest_alloc_stack_remaining = sr;

	/*
	 * Loop until we can grab cb_busy flag for ourselves:
	 * allow only one thread at a time to thread_call_enter
	 * on this vmem arena, because there is a race wherein
	 * a later racer can cancel a "medallist" who got to
	 * the callback registered earlier before the medallist
	 * has begun running in the callback function.
	 */
	for (unsigned int i = 1; ; i++) {
		/*
		 * if busy == f then busy = true and
		 * return result is true; otherwise result is
		 * false and f = true
		 */
		bool f = false;
		if (!__c11_atomic_compare_exchange_strong(
		    &vmp->vm_cb_busy, &f, true,
		    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
			/* delay and loop */
			extern void IODelay(unsigned microseconds);
			if ((i % 1000) == 0)
				IOSleep(1); // ms
			else
				IODelay(1); // us
			continue;
		} else {
			VERIFY0(!vmp->vm_cb_busy);
			break;
		}
	}

	mutex_enter(&vmp->vm_stack_lock);
	vmp->vm_cb.size = size;
	vmp->vm_cb.vmflag = vmflag;

	vmp->vm_cb.c_done = B_FALSE;
	vmp->vm_cb.r_alloc = NULL;
	vmp->vm_cb.in_child = B_FALSE;
	vmp->vm_cb.already_pending = B_FALSE;

	/*
	 * send a pointer to our parameter struct to the worker thread's
	 * vmem_alloc_update_lowest_cb()'s param1.
	 */
	boolean_t tc_already_pending __maybe_unused =
	    thread_call_enter1(vmp->vm_stack_call_thread, NULL);

	/* in DEBUG, bleat if worker thread was already working */
	ASSERT0(tc_already_pending);

	vmp->vm_cb.already_pending = tc_already_pending;

	/*
	 * Wait for a cv_signal from our worker thread.
	 * "Impossible" things, left over from before the
	 * cb_busy flag, which limits concurrency:
	 * If the worker has died we will time out and panic.
	 * If we get a spurious signal, it may have been
	 * for someone else.
	 * Less impossibly: if we lost the signal from
	 * the worker, log that and carry one.
	 */
	for (unsigned int i = 0; vmp->vm_cb.c_done != B_TRUE; i++) {
		int retval = cv_timedwait(&vmp->vm_stack_cv,
		    &vmp->vm_stack_lock,
		    ddi_get_lbolt() + SEC_TO_TICK(10));
		if (retval == -1) {
			if (vmp->vm_cb.c_done != B_TRUE) {
				printf("timed out waiting for"
				    " child callback, inchild: %d: '%s'",
				    vmp->vm_cb.in_child, vmp->vm_name);
			} else {
				printf("SPL: %s:%d timedout, lost cv_signal!\n",
				    __func__, __LINE__);
				cv_signal(&vmp->vm_stack_cv);
			}
		} else if (retval == 1 && vmp->vm_cb.c_done != B_TRUE) {
			ASSERT(vmp->vm_cb.in_child);
			/* this was not for us, wake up someone else */
			printf("SPL: this was not for us, wake up '%s'\n",
			    vmp->vm_name);
			cv_signal(&vmp->vm_stack_cv);
		}
		VERIFY(mutex_owned(&vmp->vm_stack_lock));
	}

	mutex_exit(&vmp->vm_stack_lock);

	/* give up busy flag */
	VERIFY0(!vmp->vm_cb_busy);
	vmp->vm_cb_busy = false;

	ASSERT3P(vmp->vm_cb.r_alloc, !=, NULL);

	return (vmp->vm_cb.r_alloc);
}

/*
 * The guts of vmem_alloc_impl()
 */
static inline void *
wrapped_vmem_alloc_impl(vmem_t *vmp, size_t size, int vmflag)
{
	vmem_seg_t *vsp;
	uintptr_t addr;
	int hb;
	int flist = 0;
	uint32_t mtbf;

	if (size - 1 < vmp->vm_qcache_max)
		return (kmem_cache_alloc(vmp->vm_qcache[(size - 1) >>
		    vmp->vm_qshift], vmflag & VM_KMFLAGS));

	if ((mtbf = vmem_mtbf | vmp->vm_mtbf) != 0 && gethrtime() % mtbf == 0 &&
	    (vmflag & (VM_NOSLEEP | VM_PANIC)) == VM_NOSLEEP)
		return (NULL);

	if (vmflag & VM_NEXTFIT)
		return (vmem_nextfit_alloc(vmp, size, vmflag));

	if (vmflag & (VM_BESTFIT | VM_FIRSTFIT))
		return (vmem_xalloc(vmp, size, vmp->vm_quantum, 0, 0,
		    NULL, NULL, vmflag));
	if (vmp->vm_cflags & VM_NEXTFIT)
		return (vmem_nextfit_alloc(vmp, size, vmflag));

	/*
	 * Unconstrained instant-fit allocation from the segment list.
	 */
	mutex_enter(&vmp->vm_lock);

	if (vmp->vm_nsegfree >= VMEM_MINFREE || vmem_populate(vmp, vmflag)) {
		if ((size & (size - 1)) == 0)
			flist = lowbit(P2ALIGN(vmp->vm_freemap, size));
		else if ((hb = highbit(size)) < VMEM_FREELISTS)
			flist = lowbit(P2ALIGN(vmp->vm_freemap, 1UL << hb));
	}

	if (flist-- == 0) {
		mutex_exit(&vmp->vm_lock);
		return (vmem_xalloc(vmp, size, vmp->vm_quantum,
		    0, 0, NULL, NULL, vmflag));
	}

	ASSERT(size <= (1UL << flist));
	vsp = vmp->vm_freelist[flist].vs_knext;
	addr = vsp->vs_start;
	if (vmflag & VM_ENDALLOC) {
		addr += vsp->vs_end - (addr + size);
	}
	(void) vmem_seg_alloc(vmp, vsp, addr, size);
	mutex_exit(&vmp->vm_lock);
	return ((void *)addr);
}

/*
 * Free the segment [vaddr, vaddr + size).
 */
void
vmem_free_impl(vmem_t *vmp, void *vaddr, size_t size)
{
	if (size - 1 < vmp->vm_qcache_max)
		kmem_cache_free(vmp->vm_qcache[(size - 1) >> vmp->vm_qshift],
		    vaddr);
	else
		vmem_xfree(vmp, vaddr, size);
}

/*
 * Determine whether arena vmp contains the segment [vaddr, vaddr + size).
 */
int
vmem_contains(vmem_t *vmp, void *vaddr, size_t size)
{
	uintptr_t start = (uintptr_t)vaddr;
	uintptr_t end = start + size;
	vmem_seg_t *vsp;
	vmem_seg_t *seg0 = &vmp->vm_seg0;

	mutex_enter(&vmp->vm_lock);
	vmp->vm_kstat.vk_contains.value.ui64++;
	for (vsp = seg0->vs_knext; vsp != seg0; vsp = vsp->vs_knext) {
		vmp->vm_kstat.vk_contains_search.value.ui64++;
		ASSERT(vsp->vs_type == VMEM_SPAN);
		if (start >= vsp->vs_start && end - 1 <= vsp->vs_end - 1)
			break;
	}
	mutex_exit(&vmp->vm_lock);
	return (vsp != seg0);
}

/*
 * Add the span [vaddr, vaddr + size) to arena vmp.
 */
void *
vmem_add(vmem_t *vmp, void *vaddr, size_t size, int vmflag)
{
	if (vaddr == NULL || size == 0)
		panic("vmem_add(%p, %p, %lu): bad arguments",
		    (void *)vmp, vaddr, size);

	ASSERT(!vmem_contains(vmp, vaddr, size));

	mutex_enter(&vmp->vm_lock);
	if (vmem_populate(vmp, vmflag))
		(void) vmem_span_create(vmp, vaddr, size, 0);
	else
		vaddr = NULL;
	mutex_exit(&vmp->vm_lock);
	return (vaddr);
}

/*
 * Walk the vmp arena, applying func to each segment matching typemask.
 * If VMEM_REENTRANT is specified, the arena lock is dropped across each
 * call to func(); otherwise, it is held for the duration of vmem_walk()
 * to ensure a consistent snapshot.  Note that VMEM_REENTRANT callbacks
 * are *not* necessarily consistent, so they may only be used when a hint
 * is adequate.
 */
void
vmem_walk(vmem_t *vmp, int typemask,
    void (*func)(void *, void *, size_t), void *arg)
{
	vmem_seg_t *vsp;
	vmem_seg_t *seg0 = &vmp->vm_seg0;
	vmem_seg_t walker;

	if (typemask & VMEM_WALKER)
		return;

	memset(&walker, 0, sizeof (walker));
	walker.vs_type = VMEM_WALKER;

	mutex_enter(&vmp->vm_lock);
	VMEM_INSERT(seg0, &walker, a);
	for (vsp = seg0->vs_anext; vsp != seg0; vsp = vsp->vs_anext) {
		if (vsp->vs_type & typemask) {
			void *start = (void *)vsp->vs_start;
			size_t size = VS_SIZE(vsp);
			if (typemask & VMEM_REENTRANT) {
				vmem_advance(vmp, &walker, vsp);
				mutex_exit(&vmp->vm_lock);
				func(arg, start, size);
				mutex_enter(&vmp->vm_lock);
				vsp = &walker;
			} else {
				func(arg, start, size);
			}
		}
	}
	vmem_advance(vmp, &walker, NULL);
	mutex_exit(&vmp->vm_lock);
}

/*
 * Return the total amount of memory whose type matches typemask.  Thus:
 *
 *	typemask VMEM_ALLOC yields total memory allocated (in use).
 *	typemask VMEM_FREE yields total memory free (available).
 *	typemask (VMEM_ALLOC | VMEM_FREE) yields total arena size.
 */
size_t
vmem_size(vmem_t *vmp, int typemask)
{
	int64_t size = 0;

	if (typemask & VMEM_ALLOC)
		size += (int64_t)vmp->vm_kstat.vk_mem_inuse.value.ui64;
	if (typemask & VMEM_FREE)
		size += (int64_t)vmp->vm_kstat.vk_mem_total.value.ui64 -
		    (int64_t)vmp->vm_kstat.vk_mem_inuse.value.ui64;
	if (size < 0)
		size = 0;

	return ((size_t)size);
}

size_t
vmem_size_locked(vmem_t *vmp, int typemask)
{
	boolean_t m = (mutex_owner(&vmp->vm_lock) == curthread);

	if (!m)
		mutex_enter(&vmp->vm_lock);
	size_t s = vmem_size(vmp, typemask);
	if (!m)
		mutex_exit(&vmp->vm_lock);
	return (s);
}

size_t
vmem_size_semi_atomic(vmem_t *vmp, int typemask)
{
	int64_t size = 0;
	uint64_t inuse = 0;
	uint64_t total = 0;

	__sync_swap(&total, vmp->vm_kstat.vk_mem_total.value.ui64);
	__sync_swap(&inuse, vmp->vm_kstat.vk_mem_inuse.value.ui64);

	int64_t inuse_signed = (int64_t)inuse;
	int64_t total_signed = (int64_t)total;

	if (typemask & VMEM_ALLOC)
		size += inuse_signed;
	if (typemask & VMEM_FREE)
		size += total_signed - inuse_signed;

	if (size < 0)
		size = 0;

	return ((size_t)size);
}

size_t
spl_vmem_size(vmem_t *vmp, int typemask)
{
	return (vmem_size_locked(vmp, typemask));
}

/*
 * Create an arena called name whose initial span is [base, base + size).
 * The arena's natural unit of currency is quantum, so vmem_alloc_impl()
 * guarantees quantum-aligned results.  The arena may import new spans
 * by invoking afunc() on source, and may return those spans by invoking
 * ffunc() on source.  To make small allocations fast and scalable,
 * the arena offers high-performance caching for each integer multiple
 * of quantum up to qcache_max.
 */
static vmem_t *
vmem_create_common(const char *name, void *base, size_t size, size_t quantum,
    void *(*afunc)(vmem_t *, size_t, int),
    void (*ffunc)(vmem_t *, void *, size_t),
    vmem_t *source, size_t qcache_max, int vmflag)
{
	int i;
	size_t nqcache;
	vmem_t *vmp, *cur, **vmpp;
	vmem_seg_t *vsp;
	vmem_freelist_t *vfp;
	uint32_t id = atomic_inc_32_nv(&vmem_id);

	if (vmem_vmem_arena != NULL) {
		vmp = vmem_alloc_impl(vmem_vmem_arena, sizeof (vmem_t),
		    vmflag & VM_KMFLAGS);
	} else {
		ASSERT(id <= VMEM_INITIAL);
		vmp = &vmem0[id - 1];
	}

	/* An identifier arena must inherit from another identifier arena */
	ASSERT(source == NULL || ((source->vm_cflags & VMC_IDENTIFIER) ==
	    (vmflag & VMC_IDENTIFIER)));

	if (vmp == NULL)
		return (NULL);
	memset(vmp, 0, sizeof (vmem_t));

	(void) snprintf(vmp->vm_name, VMEM_NAMELEN, "%s", name);
	mutex_init(&vmp->vm_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&vmp->vm_cv, NULL, CV_DEFAULT, NULL);
	vmp->vm_cflags = vmflag;
	vmflag &= VM_KMFLAGS;

	hrtime_t hrnow = gethrtime();

	vmp->vm_createtime = hrnow;

	vmp->vm_quantum = quantum;
	vmp->vm_qshift = highbit(quantum) - 1;
	nqcache = MIN(qcache_max >> vmp->vm_qshift, VMEM_NQCACHE_MAX);

	for (i = 0; i <= VMEM_FREELISTS; i++) {
		vfp = &vmp->vm_freelist[i];
		vfp->vs_end = 1UL << i;
		vfp->vs_knext = (vmem_seg_t *)(vfp + 1);
		vfp->vs_kprev = (vmem_seg_t *)(vfp - 1);
	}

	vmp->vm_freelist[0].vs_kprev = NULL;
	vmp->vm_freelist[VMEM_FREELISTS].vs_knext = NULL;
	vmp->vm_freelist[VMEM_FREELISTS].vs_end = 0;
	vmp->vm_hash_table = vmp->vm_hash0;
	vmp->vm_hash_mask = VMEM_HASH_INITIAL - 1;
	vmp->vm_hash_shift = highbit(vmp->vm_hash_mask);

	vsp = &vmp->vm_seg0;
	vsp->vs_anext = vsp;
	vsp->vs_aprev = vsp;
	vsp->vs_knext = vsp;
	vsp->vs_kprev = vsp;
	vsp->vs_type = VMEM_SPAN;
	vsp->vs_span_createtime = hrnow;

	vsp = &vmp->vm_rotor;
	vsp->vs_type = VMEM_ROTOR;
	VMEM_INSERT(&vmp->vm_seg0, vsp, a);

	memcpy(&vmp->vm_kstat, &vmem_kstat_template, sizeof (vmem_kstat_t));

	vmp->vm_id = id;
	if (source != NULL)
		vmp->vm_kstat.vk_source_id.value.ui32 = source->vm_id;
	vmp->vm_source = source;
	vmp->vm_source_alloc = afunc;
	vmp->vm_source_free = ffunc;

	/*
	 * Some arenas (like vmem_metadata and kmem_metadata) cannot
	 * use quantum caching to lower fragmentation.  Instead, we
	 * increase their imports, giving a similar effect.
	 */
	if (vmp->vm_cflags & VMC_NO_QCACHE) {
		if (qcache_max > VMEM_NQCACHE_MAX && ISP2(qcache_max)) {
			vmp->vm_min_import = qcache_max;
		} else {
			vmp->vm_min_import =
			    VMEM_QCACHE_SLABSIZE(nqcache << vmp->vm_qshift);
		}
		nqcache = 0;
	}

	if (nqcache != 0) {
		ASSERT(!(vmflag & VM_NOSLEEP));
		vmp->vm_qcache_max = nqcache << vmp->vm_qshift;
		for (i = 0; i < nqcache; i++) {
			char buf[VMEM_NAMELEN + 21];
			(void) snprintf(buf, VMEM_NAMELEN + 20, "%s_%lu",
			    vmp->vm_name, (i + 1) * quantum);
			vmp->vm_qcache[i] = kmem_cache_create(buf,
			    (i + 1) * quantum, quantum, NULL, NULL, NULL,
			    NULL, vmp, KMC_QCACHE | KMC_NOTOUCH);
		}
	}

	if ((vmp->vm_ksp = kstat_create("vmem", vmp->vm_id, vmp->vm_name,
	    "vmem", KSTAT_TYPE_NAMED, sizeof (vmem_kstat_t) /
	    sizeof (kstat_named_t), KSTAT_FLAG_VIRTUAL)) != NULL) {
		vmp->vm_ksp->ks_data = &vmp->vm_kstat;
		kstat_install(vmp->vm_ksp);
	}

	mutex_enter(&vmem_list_lock);
	vmpp = &vmem_list;
	while ((cur = *vmpp) != NULL)
		vmpp = &cur->vm_next;
	*vmpp = vmp;
	mutex_exit(&vmem_list_lock);

	if (vmp->vm_cflags & VMC_POPULATOR) {
		ASSERT(vmem_populators < VMEM_INITIAL);
		vmem_populator[atomic_inc_32_nv(&vmem_populators) - 1] = vmp;
		mutex_enter(&vmp->vm_lock);
		(void) vmem_populate(vmp, vmflag | VM_PANIC);
		mutex_exit(&vmp->vm_lock);
	}

	if ((base || size) && vmem_add(vmp, base, size, vmflag) == NULL) {
		vmem_destroy(vmp);
		return (NULL);
	}

	/* set up thread call */
	vmp->vm_cb_busy = false;
	mutex_init(&vmp->vm_stack_lock, "lock for thread call",
	    MUTEX_DEFAULT, NULL);
	cv_init(&vmp->vm_stack_cv, NULL, CV_DEFAULT, NULL);

#if defined(MAC_OS_X_VERSION_10_13) && \
	(MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_13)
	vmp->vm_stack_call_thread = thread_call_allocate_with_options(
	    (thread_call_func_t)vmem_alloc_update_lowest_cb,
	    (thread_call_param_t)vmp,
	    THREAD_CALL_PRIORITY_KERNEL,
	    0);
#else
	vmp->vm_stack_call_thread = thread_call_allocate(
	    vmem_alloc_update_lowest_cb, vmp);
#endif

	dprintf("SPL: %s:%d: setup of %s done\n",
	    __func__, __LINE__, vmp->vm_name);

	return (vmp);
}

vmem_t *
vmem_xcreate(const char *name, void *base, size_t size, size_t quantum,
    vmem_ximport_t *afunc, vmem_free_t *ffunc, vmem_t *source,
    size_t qcache_max, int vmflag)
{
	ASSERT(!(vmflag & (VMC_POPULATOR | VMC_XALLOC)));
	vmflag &= ~(VMC_POPULATOR | VMC_XALLOC);

	return (vmem_create_common(name, base, size, quantum,
	    (vmem_alloc_t *)afunc, ffunc, source, qcache_max,
	    vmflag | VMC_XALLOC));
}

vmem_t *
vmem_create(const char *name, void *base, size_t size, size_t quantum,
    vmem_alloc_t *afunc, vmem_free_t *ffunc, vmem_t *source,
    size_t qcache_max, int vmflag)
{
	ASSERT(!(vmflag & (VMC_XALLOC | VMC_XALIGN)));
	vmflag &= ~(VMC_XALLOC | VMC_XALIGN);

	return (vmem_create_common(name, base, size, quantum,
	    afunc, ffunc, source, qcache_max, vmflag));
}

/*
 * Destroy arena vmp.
 */
void
vmem_destroy(vmem_t *vmp)
{
	vmem_t *cur, **vmpp;
	vmem_seg_t *seg0 = &vmp->vm_seg0;
	vmem_seg_t *vsp, *anext;
	size_t leaked;

	/* check for possible async stack calls */

	const boolean_t ret_thread_call_cancel __maybe_unused =
	    thread_call_cancel(vmp->vm_stack_call_thread);
	ASSERT0(ret_thread_call_cancel);

	/* tear down async stack call mechanisms */

	const boolean_t ret_thread_call_free __maybe_unused =
	    thread_call_free(vmp->vm_stack_call_thread);
	ASSERT0(!ret_thread_call_free);

	mutex_destroy(&vmp->vm_stack_lock);
	cv_destroy(&vmp->vm_stack_cv);

	/*
	 * set vm_nsegfree to zero because vmem_free_span_list
	 * would have already freed vm_segfree.
	 */
	vmp->vm_nsegfree = 0;
	mutex_enter(&vmem_list_lock);
	vmpp = &vmem_list;
	while ((cur = *vmpp) != vmp)
		vmpp = &cur->vm_next;
	*vmpp = vmp->vm_next;
	mutex_exit(&vmem_list_lock);

	leaked = vmem_size(vmp, VMEM_ALLOC);
	if (leaked != 0)
		printf("SPL: vmem_destroy('%s'): leaked %lu %s\n",
		    vmp->vm_name, leaked, (vmp->vm_cflags & VMC_IDENTIFIER) ?
		    "identifiers" : "bytes");

	if (vmp->vm_hash_table != vmp->vm_hash0)
		if (vmem_hash_arena != NULL)
			vmem_free_impl(vmem_hash_arena, vmp->vm_hash_table,
			    (vmp->vm_hash_mask + 1) * sizeof (void *));

	/*
	 * Give back the segment structures for anything that's left in the
	 * arena, e.g. the primary spans and their free segments.
	 */
	VMEM_DELETE(&vmp->vm_rotor, a);
	for (vsp = seg0->vs_anext; vsp != seg0; vsp = anext) {
		anext = vsp->vs_anext;
		vmem_putseg_global(vsp);
	}

	while (vmp->vm_nsegfree > 0)
		vmem_putseg_global(vmem_getseg(vmp));

	kstat_delete(vmp->vm_ksp);

	mutex_destroy(&vmp->vm_lock);
	cv_destroy(&vmp->vm_cv);
	vmem_free_impl(vmem_vmem_arena, vmp, sizeof (vmem_t));
}


/*
 * Destroy arena vmp.
 */
void
vmem_destroy_internal(vmem_t *vmp)
{
	vmem_t *cur, **vmpp;
	vmem_seg_t *seg0 = &vmp->vm_seg0;
	vmem_seg_t *vsp, *anext;
	size_t leaked;

	mutex_enter(&vmem_list_lock);
	vmpp = &vmem_list;
	while ((cur = *vmpp) != vmp)
		vmpp = &cur->vm_next;
	*vmpp = vmp->vm_next;
	mutex_exit(&vmem_list_lock);

	leaked = vmem_size(vmp, VMEM_ALLOC);
	if (leaked != 0)
		printf("SPL: vmem_destroy('%s'): leaked %lu %s\n",
		    vmp->vm_name, leaked, (vmp->vm_cflags & VMC_IDENTIFIER) ?
		    "identifiers" : "bytes");

	if (vmp->vm_hash_table != vmp->vm_hash0)
		if (vmem_hash_arena != NULL)
			vmem_free_impl(vmem_hash_arena, vmp->vm_hash_table,
			    (vmp->vm_hash_mask + 1) * sizeof (void *));

	/*
	 * Give back the segment structures for anything that's left in the
	 * arena, e.g. the primary spans and their free segments.
	 */
	VMEM_DELETE(&vmp->vm_rotor, a);
	for (vsp = seg0->vs_anext; vsp != seg0; vsp = anext) {
		anext = vsp->vs_anext;
		vmem_putseg_global(vsp);
	}

	while (vmp->vm_nsegfree > 0)
		vmem_putseg_global(vmem_getseg(vmp));

	if (!(vmp->vm_cflags & VMC_IDENTIFIER) &&
	    vmem_size(vmp, VMEM_ALLOC) != 0)
		printf("SPL: vmem_destroy('%s'): STILL %lu bytes at "
		    "kstat_delete() time\n",
		    vmp->vm_name, vmem_size(vmp, VMEM_ALLOC));

	kstat_delete(vmp->vm_ksp);

	mutex_destroy(&vmp->vm_lock);
	cv_destroy(&vmp->vm_cv);

	// Alas, to free, requires access to "vmem_vmem_arena" the very thing
	// we release first.
	// vmem_free_impl(vmem_vmem_arena, vmp, sizeof (vmem_t));
}

/*
 * Only shrink vmem hashtable if it is 1<<vmem_rescale_minshift times (8x)
 * larger than necessary.
 */
int vmem_rescale_minshift = 3;

/*
 * Resize vmp's hash table to keep the average lookup depth near 1.0.
 */
static void
vmem_hash_rescale(vmem_t *vmp)
{
	vmem_seg_t **old_table, **new_table, *vsp;
	size_t old_size, new_size, h, nseg;

	nseg = (size_t)(vmp->vm_kstat.vk_alloc.value.ui64 -
	    vmp->vm_kstat.vk_free.value.ui64);

	new_size = MAX(VMEM_HASH_INITIAL, 1 << (highbit(3 * nseg + 4) - 2));
	old_size = vmp->vm_hash_mask + 1;

	if ((old_size >> vmem_rescale_minshift) <= new_size &&
	    new_size <= (old_size << 1))
		return;

	new_table = vmem_alloc_impl(vmem_hash_arena, new_size * sizeof (void *),
	    VM_NOSLEEP);
	if (new_table == NULL)
		return;
	memset(new_table, 0, new_size * sizeof (void *));

	mutex_enter(&vmp->vm_lock);

	old_size = vmp->vm_hash_mask + 1;
	old_table = vmp->vm_hash_table;

	vmp->vm_hash_mask = new_size - 1;
	vmp->vm_hash_table = new_table;
	vmp->vm_hash_shift = highbit(vmp->vm_hash_mask);

	for (h = 0; h < old_size; h++) {
		vsp = old_table[h];
		while (vsp != NULL) {
			uintptr_t addr = vsp->vs_start;
			vmem_seg_t *next_vsp = vsp->vs_knext;
			vmem_seg_t **hash_bucket = VMEM_HASH(vmp, addr);
			vsp->vs_knext = *hash_bucket;
			*hash_bucket = vsp;
			vsp = next_vsp;
		}
	}

	mutex_exit(&vmp->vm_lock);

	if (old_table != vmp->vm_hash0)
		vmem_free_impl(vmem_hash_arena, old_table,
		    old_size * sizeof (void *));
}

/*
 * Perform periodic maintenance on all vmem arenas.
 */

void
vmem_update(void *dummy)
{
	vmem_t *vmp;

	mutex_enter(&vmem_list_lock);
	for (vmp = vmem_list; vmp != NULL; vmp = vmp->vm_next) {
		/*
		 * If threads are waiting for resources, wake them up
		 * periodically so they can issue another kmem_reap()
		 * to reclaim resources cached by the slab allocator.
		 */
		cv_broadcast(&vmp->vm_cv);

		/*
		 * Rescale the hash table to keep the hash chains short.
		 */
		vmem_hash_rescale(vmp);
	}
	mutex_exit(&vmem_list_lock);

	(void) bsd_timeout(vmem_update, dummy, &vmem_update_interval);
}

void
vmem_qcache_reap(vmem_t *vmp)
{
	int i;

	/*
	 * Reap any quantum caches that may be part of this vmem.
	 */
	for (i = 0; i < VMEM_NQCACHE_MAX; i++)
		if (vmp->vm_qcache[i])
			kmem_cache_reap_now(vmp->vm_qcache[i]);
}

/* given a size, return the appropriate vmem_bucket_arena[] entry */

static inline uint16_t
vmem_bucket_number(size_t size)
{
	// For VMEM_BUCKET_HIBIT == 12,
	// vmem_bucket_arena[n] holds allocations from 2^[n+11]+1 to  2^[n+12],
	// so for [n] = 0, 2049-4096, for [n]=5 65537-131072,
	// for [n]=7 (256k+1)-512k
	// set hb: 512k == 19, 256k+1 == 19, 256k == 18, ...
	const int hb = highbit(size-1);

	int bucket = hb - VMEM_BUCKET_LOWBIT;

	// very large allocations go into the 16 MiB bucket
	if (hb > VMEM_BUCKET_HIBIT)
		bucket = VMEM_BUCKET_HIBIT - VMEM_BUCKET_LOWBIT;

	// very small allocations go into the 4 kiB bucket
	if (bucket < 0)
		bucket = 0;

	return ((int16_t)bucket);
}

static inline vmem_t *
vmem_bucket_arena_by_size(size_t size)
{
	uint16_t bucket = vmem_bucket_number(size);

	return (vmem_bucket_arena[bucket]);
}

inline vmem_t *
spl_vmem_bucket_arena_by_size(size_t size)
{
	return (vmem_bucket_arena_by_size(size));
}

static inline void
vmem_bucket_wake_all_waiters(void)
{
	for (int i = VMEM_BUCKET_LOWBIT; i < VMEM_BUCKET_HIBIT; i++) {
		const int bucket = i - VMEM_BUCKET_LOWBIT;
		vmem_t *bvmp = vmem_bucket_arena[bucket];
		cv_broadcast(&bvmp->vm_cv);
	}
	cv_broadcast(&spl_heap_arena->vm_cv);
}



static void *
xnu_alloc_throttled(vmem_t *bvmp, size_t size, int vmflag)
{
	static volatile _Atomic uint64_t fail_at = 0;
	static volatile _Atomic int16_t success_ct = 0;

	void *p =  spl_vmem_malloc_unconditionally_unlocked(size);

	if (p != NULL) {
		/* grow fail_at periodically */
		if (success_ct++ >= 128) {
			fail_at += size;
			success_ct = 0;
		}
		spl_xat_lastalloc = gethrtime();
		cv_broadcast(&bvmp->vm_cv);
		return (p);
	}

	success_ct = 0;
	fail_at = segkmem_total_mem_allocated - size;

	/*
	 * adjust dynamic memory cap downwards by 1/32 (~ 3%) of total_memory
	 * but do not drop below 1/8 of total_memory..
	 *
	 * see also spl-kmem.c:spl_reduce_dynamic_cap(), which is
	 * triggered by ARC or other clients inquiring about spl_free()
	 */
	if (spl_enforce_memory_caps != 0 &&
	    (fail_at < spl_dynamic_memory_cap ||
	    spl_dynamic_memory_cap == 0)) {
		mutex_enter(&spl_dynamic_memory_cap_lock);

		spl_dynamic_memory_cap_last_downward_adjust = gethrtime();
		const int64_t thresh = total_memory >> 3;
		const int64_t below_fail_at = fail_at - (total_memory >> 5);
		const int64_t reduced = MAX(below_fail_at, thresh);

		if (spl_dynamic_memory_cap == 0 ||
		    spl_dynamic_memory_cap >= total_memory) {
			spl_dynamic_memory_cap = reduced;
			atomic_inc_64(&spl_dynamic_memory_cap_reductions);
		} else if (thresh > spl_dynamic_memory_cap) {
			spl_dynamic_memory_cap = thresh;
			atomic_inc_64(&spl_dynamic_memory_cap_hit_floor);
		} else {
			spl_dynamic_memory_cap = reduced;
			atomic_inc_64(&spl_dynamic_memory_cap_reductions);
		}

		mutex_exit(&spl_dynamic_memory_cap_lock);
	}

	/* wait until used memory falls below failure_at */

	extern void spl_set_arc_no_grow(int);
	spl_set_arc_no_grow(B_TRUE);
	spl_free_set_emergency_pressure(total_memory >> 7LL);
	atomic_inc_64(&spl_xat_pressured);
	if ((vmflag & (VM_NOSLEEP | VM_PANIC | VM_ABORT)) > 0)
		return (NULL);

	for (uint64_t loop_for_mem = 1; ; loop_for_mem++) {
		// ASSERT3U((loop_for_mem % 10), ==, 0); // 1 second bleat beat
		IOSleep(100); /* sleep 100 milliseconds, hope to free memory */
		/* only try to allocate if there is memory */
		if (fail_at > segkmem_total_mem_allocated) {
			p = spl_vmem_malloc_unconditionally_unlocked(size);
			if (p != NULL)
				return (p);
		} else {
			/* abuse existing kstat */
			atomic_inc_64(&spl_xat_sleep);
		}
		success_ct = 0;
		const uint64_t x = segkmem_total_mem_allocated - size;
		if (fail_at > x)
			fail_at = x;
		spl_set_arc_no_grow(B_TRUE);
		spl_free_set_emergency_pressure(total_memory >> 7LL);
		atomic_inc_64(&spl_xat_pressured);
		/* after ten seconds, just return NULL */
		if (loop_for_mem > 100)
			return (NULL);
	}
}

static void
xnu_free_throttled(vmem_t *vmp, void *vaddr, size_t size)
{
	extern void osif_free(void *, uint64_t);

	osif_free(vaddr, size);
	spl_xat_lastfree = gethrtime();
	vmem_bucket_wake_all_waiters();
}

// return 0 if the bit was unset before the atomic OR.
static inline bool
vba_atomic_lock_bucket(volatile _Atomic uint16_t *bbap, uint16_t bucket_bit)
{

	// We use a test-and-set of the appropriate bit
	// in buckets_busy_allocating; if it was not set,
	// then break out of the loop.
	//
	// This compiles into an orl, cmpxchgw instruction pair.
	// the return from __c11_atomic_fetch_or() is the
	// previous value of buckets_busy_allocating.

	uint16_t prev =
	    __c11_atomic_fetch_or(bbap, bucket_bit, __ATOMIC_SEQ_CST);
	if (prev & bucket_bit)
		return (false); // we did not acquire the bit lock here
	else
		return (true); // we turned the bit from 0 to 1
}

static void *
vmem_bucket_alloc(vmem_t *null_vmp, size_t size, const int vmflags)
{

	if (vmflags & VM_NO_VBA)
		return (NULL);

	// caller is spl_heap_arena looking for memory.
	// null_vmp will be spl_default_arena_parent, and so
	// is just a placeholder.

	vmem_t *calling_arena = spl_heap_arena;

	static volatile _Atomic uint32_t hipriority_allocators = 0;
	boolean_t local_hipriority_allocator = false;

	if (0 != (vmflags & (VM_PUSHPAGE | VM_NOSLEEP | VM_PANIC | VM_ABORT))) {
		local_hipriority_allocator = true;
		hipriority_allocators++;
	}

	if (!ISP2(size))
		atomic_inc_64(&spl_bucket_non_pow2_allocs);

	vmem_t *bvmp = vmem_bucket_arena_by_size(size);

	void *fastm = vmem_alloc_impl(bvmp, size,
	    local_hipriority_allocator ? vmflags : vmflags | VM_BESTFIT);

	if (fastm != NULL) {
		atomic_inc_64(&spl_vba_fastpath);
		cv_broadcast(&calling_arena->vm_cv);
		return (fastm);
	} else if ((vmflags & (VM_NOSLEEP | VM_PANIC | VM_ABORT)) > 0) {
		atomic_inc_64(&spl_vba_fastexit);
		return (NULL);
	}

	atomic_inc_64(&spl_vba_slowpath);

	/* work harder to avoid an allocation */
	const int slow_vmflags = vmflags | VM_BESTFIT;

	// there are 13 buckets, so use a 16-bit scalar to hold
	// a set of bits, where each bit corresponds to an in-progress
	// vmem_alloc_impl(bucket, ...) below.

	static volatile _Atomic uint16_t buckets_busy_allocating = 0;
	const uint16_t bucket_number = vmem_bucket_number(size);
	const uint16_t bucket_bit = (uint16_t)(1 << bucket_number);

	spl_vba_threads[bucket_number]++;

	static volatile _Atomic uint32_t waiters = 0;

	// First, if we are VM_SLEEP, check for memory, try some pressure,
	// and if that doesn't work, force entry into the loop below.

	bool loop_once = false;

	if ((slow_vmflags & (VM_NOSLEEP | VM_PANIC | VM_ABORT)) == 0 &&
	    ! vmem_canalloc_atomic(bvmp, size)) {
		if (spl_vmem_xnu_useful_bytes_free() < (MAX(size,
		    16ULL*1024ULL*1024ULL))) {
			spl_free_set_emergency_pressure(
			    total_memory >> 7LL);
			IOSleep(1);
			if (!vmem_canalloc_atomic(bvmp, size) &&
			    (spl_vmem_xnu_useful_bytes_free() < (MAX(size,
			    16ULL*1024ULL*1024ULL)))) {
				loop_once = true;
			}
		}
	}

	// spin-sleep: if we would need to go to the xnu allocator.
	//
	// We want to avoid a burst of allocs from bucket_heap's children
	// successively hitting a low-memory condition, or alternatively
	// each successfully importing memory from xnu when they can share
	// a single import.
	//
	// We also want to take advantage of any memory that becomes available
	// in bucket_heap.
	//
	// If there is more than one thread in this function (~ few percent)
	// then the subsequent threads are put into the loop below.   They
	// can escape the loop if they are [1]non-waiting allocations, or
	// [2]if they become the only waiting thread, or
	// [3]if the cv_timedwait_hires returns -1 (which represents EWOULDBLOCK
	// from msleep() which gets it from _sleep()'s THREAD_TIMED_OUT)
	// allocating in the bucket, or [4]if this thread has (rare condition)
	// spent a quarter of a second in the loop.

	if (waiters++ > 1 || loop_once) {
		atomic_inc_64(&spl_vba_loop_entries);
	}

	static _Atomic uint32_t max_waiters_seen = 0;

	if (waiters > max_waiters_seen) {
		max_waiters_seen = waiters;
		dprintf("SPL: %s: max_waiters_seen increased to %u\n", __func__,
		    max_waiters_seen);
	}

	// local counters, to be added atomically to global kstat variables
	uint64_t local_memory_blocked = 0, local_cv_timeout = 0;
	uint64_t local_loop_timeout = 0;
	uint64_t local_cv_timeout_blocked = 0, local_loop_timeout_blocked = 0;
	uint64_t local_sleep = 0, local_hipriority_blocked = 0;

	const uint64_t loop_ticks = 25; // a tick is 10 msec, so 250 msec
	const uint64_t hiprio_loop_ticks = 4; // 40 msec

	for (uint64_t entry_time = zfs_lbolt(),
	    loop_timeout = entry_time + loop_ticks,
	    hiprio_timeout = entry_time + hiprio_loop_ticks, timedout = 0;
	    waiters > 1UL || loop_once; /* empty */) {
		loop_once = false;
		// non-waiting allocations should proceeed to vmem_alloc_impl()
		// immediately
		if (slow_vmflags & (VM_NOSLEEP | VM_PANIC | VM_ABORT)) {
			break;
		}
		if (vmem_canalloc_atomic(bvmp, size)) {
			// We can probably
			// vmem_alloc_impl(bvmp, size, slow_vmflags).
			// At worst case it will give us a NULL and we will
			// end up on the vmp's cv_wait.
			//
			// We can have threads with different bvmp
			// taking this exit, and will proceed concurrently.
			//
			// However, we should protect against a burst of
			// callers hitting the same bvmp before the allocation
			// results are reflected in
			// vmem_canalloc_atomic(bvmp, ...)
			if (local_hipriority_allocator == false &&
			    hipriority_allocators > 0) {
				// more high priority allocations are wanted,
				// so this thread stays here
				local_hipriority_blocked++;
			} else if (vba_atomic_lock_bucket(
			    &buckets_busy_allocating, bucket_bit)) {
				// we are not being blocked by another allocator
				// to the same bucket, or any higher priority
				// allocator
				atomic_inc_64(&spl_vba_parent_memory_appeared);
				break;
				// The vmem_alloc_impl() should return extremely
				// quickly from an INSTANTFIT allocation that
				// canalloc predicts will succeed.
			} else {
				// another thread is trying to use the free
				// memory in the bucket_## arena; there might
				// still be free memory there after its
				// allocation is completed, and there might be
				// excess in the bucket_heap arena, so stick
				// around in this loop.
				local_memory_blocked++;
				cv_broadcast(&bvmp->vm_cv);
			}
		}
		if (timedout > 0) {
			if (local_hipriority_allocator == false &&
			    hipriority_allocators > 0) {
				local_hipriority_blocked++;
			} else if (vba_atomic_lock_bucket(
			    &buckets_busy_allocating, bucket_bit)) {
				if (timedout & 1)
					local_cv_timeout++;
				if (timedout & 6 || zfs_lbolt() >= loop_timeout)
					local_loop_timeout++;
				break;
			} else {
				if (timedout & 1) {
					local_cv_timeout_blocked++;
				}
				if (timedout & 6) {
					local_loop_timeout_blocked++;
				} else if (zfs_lbolt() > loop_timeout) {
					timedout |= 2;
				}
				// flush the current thread in xat() out of
				// xat()'s for() loop and into xat_bail()
				cv_broadcast(&bvmp->vm_cv);
			}
		}
		// The bucket is already allocating, or the bucket needs
		// more memory to satisfy vmem_allocat(bvmp, size, VM_NOSLEEP),
		// or we want to give the bucket some time to acquire more
		// memory.
		// substitute for the vmp arena's cv_wait in vmem_xalloc()
		// (vmp is the bucket_heap AKA spl_heap_arena)
		mutex_enter(&calling_arena->vm_lock);
		local_sleep++;
		if (local_sleep >= 1000ULL) {
			atomic_add_64(&spl_vba_sleep, local_sleep - 1ULL);
			local_sleep = 1ULL;
			atomic_add_64(&spl_vba_cv_timeout_blocked,
			    local_cv_timeout_blocked);
			local_cv_timeout_blocked = 0;
			atomic_add_64(&spl_vba_loop_timeout_blocked,
			    local_loop_timeout_blocked);
			local_loop_timeout_blocked = 0;
			atomic_add_64(&spl_vba_hiprio_blocked,
			    local_hipriority_blocked);
			local_hipriority_blocked = 0;
			if (local_memory_blocked > 1ULL) {
				atomic_add_64(&spl_vba_parent_memory_blocked,
				    local_memory_blocked - 1ULL);
				local_memory_blocked = 1ULL;
			}
		}
		clock_t wait_time = MSEC2NSEC(30);
		if (timedout > 0 || local_memory_blocked > 0) {
			wait_time = MSEC2NSEC(1);
		}
		int ret = cv_timedwait_hires(&calling_arena->vm_cv,
		    &calling_arena->vm_lock,
		    wait_time, 0, 0);
		// We almost certainly have exited because of a
		// signal/broadcast, but maybe just timed out.
		// Either way, recheck memory.
		mutex_exit(&calling_arena->vm_lock);
		if (ret == -1) {
			// cv_timedwait_hires timer expired
			timedout |= 1;
			cv_broadcast(&bvmp->vm_cv);
		} else if ((timedout & 2) == 0) {
			// we were awakened; check to see if we have been
			// in the for loop for a long time
			uint64_t n = zfs_lbolt();
			if (n > loop_timeout) {
				timedout |= 2;
				extern uint64_t real_total_memory;
				spl_free_set_emergency_pressure(
				    total_memory >> 7LL);
				// flush the current thread in xat() out of
				// xat()'s for() loop and into xat_bail()
				cv_broadcast(&bvmp->vm_cv);
			} else if (local_hipriority_allocator &&
			    n > hiprio_timeout && waiters > 1UL) {
				timedout |= 4;
			}
		}
	}

	/*
	 * Turn on the exclusion bit in buckets_busy_allocating, to
	 * prevent multiple threads from calling vmem_alloc_impl() on the
	 * same bucket arena concurrently rather than serially.
	 *
	 * This principally reduces the liklihood of asking xnu for
	 * more memory when other memory is or becomes available.
	 *
	 * This exclusion only applies to VM_SLEEP allocations;
	 * others (VM_PANIC, VM_NOSLEEP, VM_ABORT) will go to
	 * vmem_alloc_impl() concurrently with any other threads.
	 *
	 * Since we aren't doing a test-and-set operation like above,
	 * we can just use |= and &= below and get correct atomic
	 * results, instead of using:
	 *
	 * __c11_atomic_fetch_or(&buckets_busy_allocating,
	 * bucket_bit, __ATOMIC_SEQ_CST);
	 * with the &= down below being written as
	 * __c11_atomic_fetch_and(&buckets_busy_allocating,
	 * ~bucket_bit, __ATOMIC_SEQ_CST);
	 *
	 * and this makes a difference with no optimization either
	 * compiling the whole file or with __attribute((optnone))
	 * in front of the function decl.   In particular, the non-
	 * optimized version that uses the builtin __c11_atomic_fetch_{and,or}
	 * preserves the C program order in the machine language output,
	 * inersting cmpxchgws, while all optimized versions, and the
	 * non-optimized version using the plainly-written version, reorder
	 * the "orw regr, memory" and "andw register, memory" (these are atomic
	 * RMW operations in x86-64 when the memory is naturally aligned) so
	 * that the strong memory model x86-64 promise that later loads see the
	 * results of earlier stores.
	 *
	 * clang+llvm simply are good at optimizing _Atomics and
	 * the optimized code differs only in line numbers and
	 * among all three approaches (as plainly written, using
	 * the __c11_atomic_fetch_{or,and} with sequential consistency,
	 * or when compiling with at least -O optimization so an
	 * atomic_or_16(&buckets_busy_allocating) built with GCC intrinsics
	 * is actually inlined rather than a function call).
	 *
	 */

	// in case we left the loop by being the only waiter, stop the
	// next thread arriving from leaving the for loop because
	// vmem_canalloc(bvmp, that_thread's_size) is true.

	buckets_busy_allocating |= bucket_bit;

	// update counters
	if (local_sleep > 0)
		atomic_add_64(&spl_vba_sleep, local_sleep);
	if (local_memory_blocked > 0)
		atomic_add_64(&spl_vba_parent_memory_blocked,
		    local_memory_blocked);
	if (local_cv_timeout > 0)
		atomic_add_64(&spl_vba_cv_timeout, local_cv_timeout);
	if (local_cv_timeout_blocked > 0)
		atomic_add_64(&spl_vba_cv_timeout_blocked,
		    local_cv_timeout_blocked);
	if (local_loop_timeout > 0)
		atomic_add_64(&spl_vba_loop_timeout, local_loop_timeout);
	if (local_loop_timeout_blocked > 0)
		atomic_add_64(&spl_vba_loop_timeout_blocked,
		    local_loop_timeout_blocked);
	if (local_hipriority_blocked > 0)
		atomic_add_64(&spl_vba_hiprio_blocked,
		    local_hipriority_blocked);

	// There is memory in this bucket, or there are no other waiters,
	// or we aren't a VM_SLEEP allocation,  or we iterated out of the
	// for loop.
	// vmem_alloc_impl() and vmem_xalloc() do their own mutex serializing
	// on bvmp->vm_lock, so we don't have to here.
	//
	// vmem_alloc may take some time to return (especially for VM_SLEEP
	// allocations where we did not take the vm_canalloc(bvmp...) break out
	// of the for loop).  Therefore, if we didn't enter the for loop at all
	// because waiters was 0 when we entered this function,
	// subsequent callers will enter the for loop.

	void *m = vmem_alloc_impl(bvmp, size, slow_vmflags);

	// allow another vmem_canalloc() through for this bucket
	// by atomically turning off the appropriate bit

	/*
	 * Except clang+llvm DTRT because of _Atomic, could be written as:
	 *	__c11_atomic_fetch_and(&buckets_busy_allocating,
	 *	~bucket_bit, __ATOMIC_SEQ_CST);
	 *
	 * On processors with more relaxed memory models, it might be
	 * more efficient to do so with release semantics here, and
	 * in the atomic |= above, with acquire semantics in the bit tests,
	 * but on the other hand it may be hard to do better than clang+llvm.
	 */

	buckets_busy_allocating &= ~bucket_bit;

	if (local_hipriority_allocator)
		hipriority_allocators--;

	// if we got an allocation, wake up the arena cv waiters
	// to let them try to exit the for(;;) loop above and
	// exit the cv_wait() in vmem_xalloc(vmp, ...)

	if (m != NULL) {
		cv_broadcast(&calling_arena->vm_cv);
	}

	waiters--;
	spl_vba_threads[bucket_number]--;
	return (m);
}

static void
vmem_bucket_free(vmem_t *null_vmp, void *vaddr, size_t size)
{
	vmem_t *calling_arena = spl_heap_arena;

	vmem_free_impl(vmem_bucket_arena_by_size(size), vaddr, size);

	// wake up arena waiters to let them try an alloc
	cv_broadcast(&calling_arena->vm_cv);
}

static inline int64_t
vmem_bucket_arena_free(uint16_t bucket)
{
	VERIFY(bucket < VMEM_BUCKETS);
	return ((int64_t)vmem_size_semi_atomic(vmem_bucket_arena[bucket],
	    VMEM_FREE));
}

static inline int64_t
vmem_bucket_arena_used(int bucket)
{
	VERIFY(bucket < VMEM_BUCKETS);
	return ((int64_t)vmem_size_semi_atomic(vmem_bucket_arena[bucket],
	    VMEM_ALLOC));
}


inline int64_t
vmem_buckets_size(int typemask)
{
	int64_t total_size = 0;

	for (uint16_t i = 0; i < VMEM_BUCKETS; i++) {
		int64_t u = vmem_bucket_arena_used(i);
		int64_t f = vmem_bucket_arena_free(i);
		if (typemask & VMEM_ALLOC)
			total_size += u;
		if (typemask & VMEM_FREE)
			total_size += f;
	}
	if (total_size < 0)
		total_size = 0;

	return ((size_t)total_size);
}

static inline uint64_t
spl_validate_bucket_span_size(uint64_t val)
{
	if (!ISP2(val)) {
		printf("SPL: %s: WARNING %llu is not a power of two, "
		    "not changing.\n", __func__, val);
		return (0);
	}
	if (val < 128ULL*1024ULL || val > 16ULL*1024ULL*1024ULL) {
		printf("SPL: %s: WARNING %llu is out of range [128k - 16M], "
		    "not changing.\n", __func__, val);
		return (0);
	}
	return (val);
}

static inline void
spl_modify_bucket_span_size(int bucket, uint64_t size)
{
	vmem_t *bvmp = vmem_bucket_arena[bucket];

	mutex_enter(&bvmp->vm_lock);
	bvmp->vm_min_import = size;
	mutex_exit(&bvmp->vm_lock);
}

static inline void
spl_modify_bucket_array()
{
	for (int i = VMEM_BUCKET_LOWBIT; i < VMEM_BUCKET_HIBIT; i++) {
		// i = 12, bucket = 0, contains allocs from 8192 to 16383 bytes,
		// and should never ask xnu for < 16384 bytes, so as to avoid
		// asking xnu for a non-power-of-two size.
		const int bucket = i - VMEM_BUCKET_LOWBIT;
		const uint32_t bucket_alloc_minimum_size = 1UL << (uint32_t)i;
		const uint32_t bucket_parent_alloc_minimum_size =
		    bucket_alloc_minimum_size * 2UL;

		switch (i) {
			// see vmem_init() below for details
		case 16:
		case 17:
			spl_modify_bucket_span_size(bucket,
			    MAX(spl_bucket_tunable_small_span,
			    bucket_parent_alloc_minimum_size));
			break;
		default:
			spl_modify_bucket_span_size(bucket,
			    MAX(spl_bucket_tunable_large_span,
			    bucket_parent_alloc_minimum_size));
			break;
		}
	}
}

static inline void
spl_printf_bucket_span_sizes(void)
{
	// this doesn't have to be super-exact
	dprintf("SPL: %s: ", __func__);
	for (int i = VMEM_BUCKET_LOWBIT; i < VMEM_BUCKET_HIBIT; i++) {
		int bnum = i - VMEM_BUCKET_LOWBIT;
		vmem_t *bvmp = vmem_bucket_arena[bnum];
		dprintf("%llu ", (uint64_t)bvmp->vm_min_import);
	}
	dprintf("\n");
}

static inline void
spl_set_bucket_spans(uint64_t l, uint64_t s)
{
	if (spl_validate_bucket_span_size(l) &&
	    spl_validate_bucket_span_size(s)) {
		atomic_swap_64(&spl_bucket_tunable_large_span, l);
		atomic_swap_64(&spl_bucket_tunable_small_span, s);
		spl_modify_bucket_array();
	}
}

void
spl_set_bucket_tunable_large_span(uint64_t size)
{
	uint64_t s = 0;

	mutex_enter(&vmem_xnu_alloc_lock);
	atomic_swap_64(&s, spl_bucket_tunable_small_span);
	spl_set_bucket_spans(size, s);
	mutex_exit(&vmem_xnu_alloc_lock);

	spl_printf_bucket_span_sizes();
}

void
spl_set_bucket_tunable_small_span(uint64_t size)
{
	uint64_t l = 0;

	mutex_enter(&vmem_xnu_alloc_lock);
	atomic_swap_64(&l, spl_bucket_tunable_large_span);
	spl_set_bucket_spans(l, size);
	mutex_exit(&vmem_xnu_alloc_lock);

	spl_printf_bucket_span_sizes();
}

static inline void *
spl_vmem_default_alloc(vmem_t *vmp, size_t size, int vmflags)
{
	extern void *osif_malloc(uint64_t);
	return (osif_malloc(size));
}

static inline void
spl_vmem_default_free(vmem_t *vmp, void *vaddr, size_t size)
{
	extern void osif_free(void *, uint64_t);
	osif_free(vaddr, size);
}

vmem_t *
vmem_init(const char *heap_name,
    void *heap_start, size_t heap_size, size_t heap_quantum,
    void *(*heap_alloc)(vmem_t *, size_t, int),
    void (*heap_free)(vmem_t *, void *, size_t))
{
	uint32_t id;
	int nseg = VMEM_SEG_INITIAL;
	vmem_t *heap;

	// XNU mutexes need initialisation
	mutex_init(&vmem_list_lock, "vmem_list_lock", MUTEX_DEFAULT,
	    NULL);
	mutex_init(&vmem_segfree_lock, "vmem_segfree_lock", MUTEX_DEFAULT,
	    NULL);
	mutex_init(&vmem_sleep_lock, "vmem_sleep_lock", MUTEX_DEFAULT,
	    NULL);
	mutex_init(&vmem_nosleep_lock, "vmem_nosleep_lock", MUTEX_DEFAULT,
	    NULL);
	mutex_init(&vmem_pushpage_lock, "vmem_pushpage_lock", MUTEX_DEFAULT,
	    NULL);
	mutex_init(&vmem_panic_lock, "vmem_panic_lock", MUTEX_DEFAULT,
	    NULL);
	mutex_init(&vmem_xnu_alloc_lock, "vmem_xnu_alloc_lock", MUTEX_DEFAULT,
	    NULL);

	while (--nseg >= 0)
		vmem_putseg_global(&vmem_seg0[nseg]);

	/*
	 * On OSX we ultimately have to use the OS allocator
	 * as the ource and sink of memory as it is allocated
	 * and freed.
	 *
	 * The spl_root_arena_parent is needed in order to provide a
	 * base arena with an always-NULL afunc and ffunc in order to
	 * end the searches done by vmem_[x]alloc and vm_xfree; it
	 * serves no other purpose; its stats will always be zero.
	 *
	 */

	// id 0
	spl_default_arena_parent = vmem_create("spl_default_arena_parent",
	    NULL, 0, heap_quantum, NULL, NULL, NULL, 0, VM_SLEEP);

	// illumos/openzfs has a gigantic pile of memory that it can use
	// for its first arena;
	// o3x is not so lucky, so we start with this
	// Intel can go with 4096 alignment, but arm64 needs 16384. So
	// we just use the larger.
	initial_default_block =
	    IOMallocAligned(INITIAL_BLOCK_SIZE, 16384);

	VERIFY3P(initial_default_block, !=, NULL);

	memset(initial_default_block, 0, INITIAL_BLOCK_SIZE);

	// The default arena is very low-bandwidth; it supplies the initial
	// large allocation for the heap arena below, and it serves as the
	// parent of the vmem_metadata arena.   It will typically do only 2
	// or 3 parent_alloc calls (to spl_vmem_default_alloc) in total.

	spl_default_arena = vmem_create("spl_default_arena", // id 1
	    initial_default_block, INITIAL_BLOCK_SIZE,
	    heap_quantum, spl_vmem_default_alloc, spl_vmem_default_free,
	    spl_default_arena_parent,
	    32, /* minimum import */
	    VM_SLEEP | VMC_POPULATOR | VMC_NO_QCACHE);

	VERIFY(spl_default_arena != NULL);

	// The bucket arenas satisfy allocations & frees from the bucket heap
	// that are dispatched to the bucket whose power-of-two label is the
	// smallest allocation that vmem_bucket_allocate will ask for.
	//
	// The bucket arenas in turn exchange memory with XNU's allocator/freer
	// in large spans (~ 1 MiB is stable on all systems but creates bucket
	// fragmentation)
	//
	// Segregating by size constrains internal fragmentation within the
	// bucket and provides kstat.vmem visiblity and span-size policy to
	// be applied to particular buckets (notably the sources of most
	// allocations, see the comments below)
	//
	// For VMEM_BUCKET_HIBIT == 12,
	// vmem_bucket_arena[n] holds allocations from 2^[n+11]+1 to  2^[n+12],
	// so for [n] = 0, 2049-4096, for [n]=5 65537-131072,
	// for [n]=7 (256k+1)-512k
	//
	// so "kstat.vmvm.vmem.bucket_1048576" should be read as the bucket
	// arena containing allocations 1 MiB and smaller, but larger
	// than 512 kiB.

	// create arenas for the VMEM_BUCKETS, id 2 - id 14

	extern uint64_t real_total_memory;
	VERIFY3U(real_total_memory, >=, 1024ULL*1024ULL*1024ULL);

	/*
	 * Minimum bucket span size, which is what we ask IOMallocAligned for.
	 * See comments in the switch statement below.
	 *
	 * By default ask the kernel for at least 128kiB allocations.
	 */
	spl_bucket_tunable_large_span = spl_bucket_tunable_small_span =
	    128ULL * 1024UL;

	dprintf("SPL: %s: real_total_memory %llu, large spans %llu, small "
	    "spans %llu\n", __func__, real_total_memory,
	    spl_bucket_tunable_large_span, spl_bucket_tunable_small_span);

	char *buf;
	buf = vmem_alloc_impl(spl_default_arena, VMEM_NAMELEN + 21, VM_SLEEP);

	for (int32_t i = VMEM_BUCKET_LOWBIT; i <= VMEM_BUCKET_HIBIT; i++) {
		const uint64_t bucket_largest_size = (1ULL << (uint64_t)i);

		(void) snprintf(buf, VMEM_NAMELEN + 20, "%s_%llu",
		    "bucket", bucket_largest_size);

		dprintf("SPL: %s creating arena %s (i == %d)\n", __func__, buf,
		    i);

		const int bucket_number = i - VMEM_BUCKET_LOWBIT;
		/*
		 * To reduce the number of IOMalloc/IOFree transactions with
		 * the kernel, we create vmem bucket arenas with a PAGESIZE or
		 * bigger quantum, and a minimum import that is several pages
		 * for small bucket sizes, and twice the bucket size.
		 * These will serve power-of-two sized blocks to the
		 * bucket_heap arena.
		 */
		vmem_t *b = vmem_create(buf, NULL, 0,
		    heap_quantum, /* minimum export */
		    xnu_alloc_throttled, xnu_free_throttled,
		    spl_default_arena_parent,
		    32, /* minimum import */
		    VM_SLEEP | VMC_POPULATOR | VMC_NO_QCACHE | VMC_TIMEFREE);

		VERIFY(b != NULL);

		b->vm_source = b;
		vmem_bucket_arena[bucket_number] = b;
		vmem_bucket_id_to_bucket_number[b->vm_id] = bucket_number;
	}

	vmem_free_impl(spl_default_arena, buf, VMEM_NAMELEN + 21);
	// spl_heap_arena, the bucket heap, is the primary interface
	// to the vmem system

	// all arenas not rooted to vmem_metadata will be rooted to
	// spl_heap arena.

	spl_heap_arena = vmem_create("bucket_heap", // id 15
	    NULL, 0, heap_quantum,
	    vmem_bucket_alloc, vmem_bucket_free, spl_default_arena_parent, 0,
	    VM_SLEEP | VMC_TIMEFREE | VMC_OLDFIRST);

	VERIFY(spl_heap_arena != NULL);

	// add a fixed-sized allocation to spl_heap_arena; this reduces the
	// need to talk to the bucket arenas by a substantial margin
	// (kstat.vmem.vmem.bucket_heap.{alloc+free} is much greater than
	// kstat.vmem.vmem.bucket_heap.parent_{alloc+free}, and improves with
	// increasing initial fixed allocation size.

	/*
	 * Add an initial segment to spl_heap_arena for convenience.
	 */

	const size_t mib = 1024ULL * 1024ULL;
	const size_t resv_size = 128ULL * mib;

	dprintf("SPL: %s adding fixed allocation of %llu to the bucket_heap\n",
	    __func__, (uint64_t)resv_size);

	spl_heap_arena_initial_alloc = vmem_add(spl_heap_arena,
	    vmem_xalloc(spl_default_arena, resv_size, resv_size,
	    0, 0, NULL, NULL, VM_SLEEP),
	    resv_size, VM_SLEEP);

	VERIFY(spl_heap_arena_initial_alloc != NULL);

	/* remember size we allocated */
	spl_heap_arena_initial_alloc_size = resv_size;

	// kstat.vmem.vmem.heap : kmem_cache_alloc() and similar calls
	// to handle in-memory datastructures other than abd

	heap = vmem_create(heap_name,  // id 16
	    NULL, 0, heap_quantum,
	    vmem_alloc_impl, vmem_free_impl, spl_heap_arena, 0,
	    VM_SLEEP);

	VERIFY(heap != NULL);

	// Root all the low bandwidth metadata arenas to the default arena.
	// The vmem_metadata allocations will all be 32 kiB or larger,
	// and the total allocation will generally cap off around 24 MiB.

	vmem_metadata_arena = vmem_create("vmem_metadata", // id 17
	    NULL, 0, heap_quantum, vmem_alloc_impl, vmem_free_impl,
	    spl_default_arena,
#ifdef __arm64__
	    2 * PAGESIZE,
#else
	    8 * PAGESIZE,
#endif
	    VM_SLEEP | VMC_POPULATOR | VMC_NO_QCACHE);

	VERIFY(vmem_metadata_arena != NULL);

	vmem_seg_arena = vmem_create("vmem_seg", // id 18
	    NULL, 0, heap_quantum,
	    vmem_alloc_impl, vmem_free_impl, vmem_metadata_arena, 0,
	    VM_SLEEP | VMC_POPULATOR);

	VERIFY(vmem_seg_arena != NULL);

	vmem_hash_arena = vmem_create("vmem_hash", // id 19
	    NULL, 0, 8,
	    vmem_alloc_impl, vmem_free_impl, vmem_metadata_arena, 0,
	    VM_SLEEP);

	VERIFY(vmem_hash_arena != NULL);

	vmem_vmem_arena = vmem_create("vmem_vmem", // id 20
	    vmem0, sizeof (vmem0), 1,
	    vmem_alloc_impl, vmem_free_impl, vmem_metadata_arena, 0,
	    VM_SLEEP);

	VERIFY(vmem_vmem_arena != NULL);

	// 21 (0-based) vmem_create before this line. - macroized
	// NUMBER_OF_ARENAS_IN_VMEM_INIT
	for (id = 0; id < vmem_id; id++) {
		(void) vmem_xalloc(vmem_vmem_arena, sizeof (vmem_t),
		    1, 0, 0, &vmem0[id], &vmem0[id + 1],
		    VM_NOSLEEP | VM_BESTFIT | VM_PANIC);
	}

	dprintf("SPL: starting vmem_update() thread\n");
	vmem_update(NULL);

	return (heap);
}

struct free_slab {
	vmem_t *vmp;
	size_t slabsize;
	void *slab;
	list_node_t next;
};
static list_t freelist;

static void vmem_fini_freelist(void *vmp, void *start, size_t size)
{
	struct free_slab *fs;

	MALLOC(fs, struct free_slab *, sizeof (struct free_slab), M_TEMP,
	    M_WAITOK);
	fs->vmp = vmp;
	fs->slabsize = size;
	fs->slab = start;
	list_link_init(&fs->next);
	list_insert_tail(&freelist, fs);
}

void
vmem_free_span_list(void)
{
	int total __maybe_unused = 0;
	int total_count = 0;
	struct free_slab *fs;
//	int release = 1;

	while ((fs = list_head(&freelist))) {
		total_count++;
		total += fs->slabsize;
		list_remove(&freelist, fs);
		/*
		 * Commenting out due to BSOD during uninstallation,
		 * will revisit later.
		 *
		 * for (int id = 0; id < VMEM_INITIAL; id++) {
		 * if (&vmem0[id] == fs->slab) {
		 * release = 0;
		 * break;
		 *	}
		 * }
		 *
		 * if (release)
		 *	fs->vmp->vm_source_free(fs->vmp, fs->slab,
		 *	    fs->slabsize);
		 * release = 1;
		 *
		 */
		FREE(fs, M_TEMP);
	}
}

static void
vmem_fini_void(void *vmp, void *start, size_t size)
{
}

void
vmem_fini(vmem_t *heap)
{
	struct free_slab *fs;
	uint64_t total;

	bsd_untimeout(vmem_update, NULL);

	dprintf("SPL: %s: stopped vmem_update.  Creating list and walking "
	    "arenas.\n", __func__);

	/* Create a list of slabs to free by walking the list of allocs */
	list_create(&freelist, sizeof (struct free_slab),
	    offsetof(struct free_slab, next));

	/* Walk to list of allocations */

	/*
	 * walking with VMEM_REENTRANT causes segment consolidation and
	 * freeing of spans the freelist contains a list of segments that
	 * are still allocated at the time of the walk; unfortunately the
	 * lists cannot be exact without complex multiple passes, locking,
	 * and a more complex vmem_fini_freelist().
	 *
	 * Walking without VMEM_REENTRANT can produce a nearly-exact list
	 * of unfreed spans, which Illumos would then free directly after
	 * the list is complete.
	 *
	 * Unfortunately in O3X, that lack of exactness can lead to a panic
	 * caused by attempting to free to xnu memory that we already freed
	 * to xnu. Fortunately, we can get a sense of what would have been
	 * destroyed after the (non-reentrant) walking, and we printf that
	 * at the end of this function.
	 */

	// Walk all still-alive arenas from leaves to the root

	vmem_walk(heap, VMEM_ALLOC | VMEM_REENTRANT, vmem_fini_void, heap);

	vmem_walk(heap, VMEM_ALLOC, vmem_fini_freelist, heap);

	vmem_free_span_list();
	dprintf("\nSPL: %s destroying heap\n", __func__);
	vmem_destroy(heap); // PARENT: spl_heap_arena

	dprintf("SPL: %s: walking spl_heap_arena, aka bucket_heap (pass 1)\n",
	    __func__);

	vmem_walk(spl_heap_arena, VMEM_ALLOC | VMEM_REENTRANT, vmem_fini_void,
	    spl_heap_arena);

	dprintf("SPL: %s: calling vmem_xfree(spl_default_arena, ptr, %llu);\n",
	    __func__, (uint64_t)spl_heap_arena_initial_alloc_size);

	// forcibly remove the initial alloc from spl_heap_arena arena, whether
	// or not it is empty.  below this point, any activity on
	// spl_default_arena other than a non-reentrant(!) walk and a destroy
	// is unsafe (UAF or MAF).
	// However, all the children of spl_heap_arena should now be destroyed.

	vmem_xfree(spl_default_arena, spl_heap_arena_initial_alloc,
	    spl_heap_arena_initial_alloc_size);

	printf("SPL: %s: walking spl_heap_arena, aka bucket_heap (pass 2)\n",
	    __func__);

	vmem_walk(spl_heap_arena, VMEM_ALLOC, vmem_fini_freelist,
	    spl_heap_arena);
	vmem_free_span_list();

	printf("SPL: %s: walking bucket arenas...\n", __func__);

	for (int i = VMEM_BUCKET_LOWBIT; i <= VMEM_BUCKET_HIBIT; i++) {
		const int bucket = i - VMEM_BUCKET_LOWBIT;
		vmem_walk(vmem_bucket_arena[bucket],
		    VMEM_ALLOC | VMEM_REENTRANT, vmem_fini_void,
		    vmem_bucket_arena[bucket]);

		vmem_walk(vmem_bucket_arena[bucket], VMEM_ALLOC,
		    vmem_fini_freelist, vmem_bucket_arena[bucket]);
	}
	vmem_free_span_list();

	dprintf("SPL: %s destroying spl_bucket_arenas...", __func__);
	for (int32_t i = VMEM_BUCKET_LOWBIT; i <= VMEM_BUCKET_HIBIT; i++) {
		vmem_t *vmpt = vmem_bucket_arena[i - VMEM_BUCKET_LOWBIT];
		dprintf(" %llu", (1ULL << i));
		vmem_destroy(vmpt); // parent: spl_default_arena_parent
	}
	dprintf("\n");

	printf("SPL: %s: walking vmem metadata-related arenas...\n", __func__);

	vmem_walk(vmem_vmem_arena, VMEM_ALLOC | VMEM_REENTRANT,
	    vmem_fini_void, vmem_vmem_arena);

	vmem_walk(vmem_vmem_arena, VMEM_ALLOC,
	    vmem_fini_freelist, vmem_vmem_arena);

	vmem_free_span_list();

	// We should not do VMEM_REENTRANT on vmem_seg_arena or
	// vmem_hash_arena or below to avoid causing work in
	// vmem_seg_arena and vmem_hash_arena.

	vmem_walk(vmem_seg_arena, VMEM_ALLOC,
	    vmem_fini_freelist, vmem_seg_arena);

	vmem_free_span_list();

	vmem_walk(vmem_hash_arena, VMEM_ALLOC,
	    vmem_fini_freelist, vmem_hash_arena);
	vmem_free_span_list();

	vmem_walk(vmem_metadata_arena, VMEM_ALLOC,
	    vmem_fini_freelist, vmem_metadata_arena);

	vmem_free_span_list();
	dprintf("SPL: %s walking the root arena (spl_default_arena)...\n",
	    __func__);

	vmem_walk(spl_default_arena, VMEM_ALLOC,
	    vmem_fini_freelist, spl_default_arena);

	vmem_free_span_list();

	dprintf("SPL: %s destroying bucket heap\n", __func__);
	// PARENT: spl_default_arena_parent (but depends on buckets)
	vmem_destroy(spl_heap_arena);

	// destroying the vmem_vmem arena and any arena afterwards
	// requires the use of vmem_destroy_internal(), which does
	// not talk to vmem_vmem_arena like vmem_destroy() does.
	// dprintf("SPL: %s destroying vmem_vmem_arena\n", __func__);
	// vmem_destroy_internal(vmem_vmem_arena);
	// parent: vmem_metadata_arena

	// destroying the seg arena means we must no longer
	// talk to vmem_populate()
	dprintf("SPL: %s destroying vmem_seg_arena\n", __func__);
	vmem_destroy(vmem_seg_arena);

	// vmem_hash_arena may be freed-to in vmem_destroy_internal()
	// so it should be just before the vmem_metadata_arena.
	dprintf("SPL: %s destroying vmem_hash_arena\n", __func__);
	vmem_destroy(vmem_hash_arena); // parent: vmem_metadata_arena
	vmem_hash_arena = NULL;

	// XXX: if we panic on unload below here due to destroyed mutex,
	// vmem_init() will need some reworking (e.g. have
	// vmem_metadata_arena talk directly to xnu), or alternatively a
	// vmem_destroy_internal_internal() function that does not touch
	// vmem_hash_arena will need writing.

	dprintf("SPL: %s destroying vmem_metadata_arena\n", __func__);
	vmem_destroy(vmem_metadata_arena); // parent: spl_default_arena

	dprintf("\nSPL: %s destroying spl_default_arena\n", __func__);
	vmem_destroy(spl_default_arena); // parent: spl_default_arena_parent
	dprintf("\nSPL: %s destroying spl_default_arena_parent\n", __func__);
	vmem_destroy(spl_default_arena_parent);

	dprintf("SPL: %s destroying vmem_vmem_arena\n", __func__);
	vmem_destroy_internal(vmem_vmem_arena);

	printf("SPL: %s: freeing initial_default_block\n", __func__);
	IOFreeAligned(initial_default_block, INITIAL_BLOCK_SIZE);

	printf("SPL: arenas removed, now try destroying mutexes... ");

	printf("vmem_xnu_alloc_lock ");
	mutex_destroy(&vmem_xnu_alloc_lock);
	printf("vmem_panic_lock ");
	mutex_destroy(&vmem_panic_lock);
	printf("vmem_pushpage_lock ");
	mutex_destroy(&vmem_pushpage_lock);
	printf("vmem_nosleep_lock ");
	mutex_destroy(&vmem_nosleep_lock);
	printf("vmem_sleep_lock ");
	mutex_destroy(&vmem_sleep_lock);
	printf("vmem_segfree_lock ");
	mutex_destroy(&vmem_segfree_lock);
	printf("vmem_list_lock ");
	mutex_destroy(&vmem_list_lock);

	printf("\nSPL: %s: walking list of live slabs at time of call to %s\n",
	    __func__, __func__);

	// annoyingly, some of these should be returned to xnu, but
	// we have no idea which have already been freed to xnu, and
	// freeing a second time results in a panic.

	/* Now release the list of allocs to built above */
	total = 0;
	uint64_t total_count = 0;
	while ((fs = list_head(&freelist))) {
		total_count++;
		total += fs->slabsize;
		list_remove(&freelist, fs);
		// extern void segkmem_free(vmem_t *, void *, size_t);
		// segkmem_free(fs->vmp, fs->slab, fs->slabsize);
		FREE(fs, M_TEMP);
	}
	printf("SPL: WOULD HAVE released %llu bytes (%llu spans) from arenas\n",
	    total, total_count);
	list_destroy(&freelist);
	printf("SPL: %s: Brief delay for readability...\n", __func__);
	delay(hz);
	printf("SPL: %s: done!\n", __func__);
}

/*
 * return true if inuse is much smaller than imported
 */
static inline bool
bucket_fragmented(const uint16_t bn, const uint64_t now)
{

	// early during uptime, just let buckets grow.

	if (now < 600 * hz)
		return (false);

	// if there has been no pressure in the past five minutes,
	// then we will just let the bucket grow.

	const uint64_t timeout = 5ULL * 60ULL * hz;

	if (spl_free_last_pressure_wrapper() + timeout <  now)
		return (false);

	const vmem_t *vmp = vmem_bucket_arena[bn];

	const int64_t imported =
	    (int64_t)vmp->vm_kstat.vk_mem_import.value.ui64;
	const int64_t inuse =
	    (int64_t)vmp->vm_kstat.vk_mem_inuse.value.ui64;
	const int64_t tiny = 64LL*1024LL*1024LL;
	const int64_t small = tiny * 2LL;		// 128 M
	const int64_t medium = small * 2LL;		// 256
	const int64_t large = medium * 2LL;		// 512
	const int64_t huge = large * 2LL;		// 1 G
	const int64_t super_huge = huge * 2LL;	// 2

	const int64_t amount_free = imported - inuse;

	if (amount_free <= tiny || imported <= small)
		return (false);

	const int64_t percent_free = (amount_free * 100LL) / imported;

	if (percent_free > 75LL) {
		return (true);
	} else if (imported <= medium) {
		return (percent_free >= 50);
	} else if (imported <= large) {
		return (percent_free >= 33);
	} else if (imported <= huge) {
		return (percent_free >= 25);
	} else if (imported <= super_huge) {
		return (percent_free >= 15);
	} else {
		return (percent_free >= 10);
	}
}

/*
 * Return an adjusted number of bytes free in the
 * abd_cache_arena (if it exists), for arc_no_grow
 * policy: if there's lots of space, don't allow
 * arc growth for a while to see if the gap
 * between imported and inuse drops.
 */
int64_t
abd_arena_empty_space(void)
{
	extern vmem_t *abd_arena;

	if (abd_arena == NULL)
		return (0);

	const int64_t imported =
	    (int64_t)abd_arena->vm_kstat.vk_mem_import.value.ui64;
	const int64_t inuse =
	    (int64_t)abd_arena->vm_kstat.vk_mem_inuse.value.ui64;

	/* Hide 10% or 1GiB fragmentation from arc_no_grow */
	int64_t headroom =
	    (imported * 90LL / 100LL) - inuse;

	if (headroom < 1024LL*1024LL*1024LL)
		headroom = 0;

	return (headroom);
}

int64_t
abd_arena_total_size(void)
{
	extern vmem_t *abd_arena;

	if (abd_arena != NULL)
		return (abd_arena->vm_kstat.vk_mem_total.value.ui64);
	return (0LL);
}


/*
 * return true if the bucket for size is fragmented
 */
static inline bool
spl_arc_no_grow_impl(const uint16_t b, const size_t size,
    const boolean_t buf_is_metadata, kmem_cache_t **kc)
{
	static _Atomic uint8_t frag_suppression_counter[VMEM_BUCKETS] = { 0 };

	const uint64_t now = zfs_lbolt();

	const bool fragmented = bucket_fragmented(b, now);

	if (fragmented) {
		if (size < 32768) {
			// Don't suppress small qcached blocks when the
			// qcache size (bucket_262144) is fragmented,
			// since they will push everything else towards
			// the tails of ARC lists without eating up a large
			// amount of space themselves.
			return (false);
		}
		const uint32_t b_bit = (uint32_t)1 << (uint32_t)b;
		spl_arc_no_grow_bits |= b_bit;
		const uint32_t sup_at_least_every = MIN(b_bit, 255);
		const uint32_t sup_at_most_every = MAX(b_bit, 16);
		const uint32_t sup_every = MIN(sup_at_least_every,
		    sup_at_most_every);
		if (frag_suppression_counter[b] >= sup_every) {
			frag_suppression_counter[b] = 0;
			return (true);
		} else {
			frag_suppression_counter[b]++;
			return (false);
		}
	} else {
		const uint32_t b_bit = (uint32_t)1 << (uint32_t)b;
		spl_arc_no_grow_bits &= ~b_bit;
	}

	return (false);
}

static inline uint16_t
vmem_bucket_number_arc_no_grow(const size_t size)
{
	// qcaching on arc
	if (size < 128*1024)
		return (vmem_bucket_number(262144));
	else
		return (vmem_bucket_number(size));
}

boolean_t
spl_arc_no_grow(size_t size, boolean_t buf_is_metadata, kmem_cache_t **zp)
{
	const uint16_t b = vmem_bucket_number_arc_no_grow(size);

	const bool rv = spl_arc_no_grow_impl(b, size, buf_is_metadata, zp);

	if (rv) {
		atomic_inc_64(&spl_arc_no_grow_count);
	}

	return ((boolean_t)rv);
}
