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
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/* #pragma ident	"@(#)vmem.c	1.10	05/06/08 SMI" */

/*
 * For a more complete description of the main ideas, see:
 *
 *	Jeff Bonwick and Jonathan Adams,
 *
 *	Magazines and vmem: Extending the Slab Allocator to Many CPUs and
 *	Arbitrary Resources.
 *
 *	Proceedings of the 2001 Usenix Conference.
 *	Available as /shared/sac/PSARC/2000/550/materials/vmem.pdf.
 *
 * For the "Big Theory Statement", see usr/src/common/os/vmem.c
 *
 * 1. Overview of changes
 * ------------------------------
 * There have been a few changes to vmem in order to support umem.  The
 * main areas are:
 *
 *	* VM_SLEEP unsupported
 *
 *	* Reaping changes
 *
 *	* initialization changes
 *
 *	* _vmem_extend_alloc
 *
 *
 * 2. VM_SLEEP Removed
 * -------------------
 * Since VM_SLEEP allocations can hold locks (in vmem_populate()) for
 * possibly infinite amounts of time, they are not supported in this
 * version of vmem.  Sleep-like behavior can be achieved through
 * UMEM_NOFAIL umem allocations.
 *
 *
 * 3. Reaping changes
 * ------------------
 * Unlike kmem_reap(), which just asynchronously schedules work, umem_reap()
 * can do allocations and frees synchronously.  This is a problem if it
 * occurs during a vmem_populate() allocation.
 *
 * Instead, we delay reaps while populates are active.
 *
 *
 * 4. Initialization changes
 * -------------------------
 * In the kernel, vmem_init() allows you to create a single, top-level arena,
 * which has vmem_internal_arena as a child.  For umem, we want to be able
 * to extend arenas dynamically.  It is much easier to support this if we
 * allow a two-level "heap" arena:
 *
 *	+----------+
 *	|  "fake"  |
 *	+----------+
 *	      |
 *	+----------+
 *	|  "heap"  |
 *	+----------+
 *	  |    \ \
 *	  |     +-+-- ... <other children>
 *	  |
 *	+---------------+
 *	| vmem_internal |
 *	+---------------+
 *	    | | | |
 *	   <children>
 *
 * The new vmem_init() allows you to specify a "parent" of the heap, along
 * with allocation functions.
 *
 *
 * 5. _vmem_extend_alloc
 * ---------------------
 * The other part of extending is _vmem_extend_alloc.  This function allows
 * you to extend (expand current spans, if possible) an arena and allocate
 * a chunk of the newly extened span atomically.  This is needed to support
 * extending the heap while vmem_populate()ing it.
 *
 * In order to increase the usefulness of extending, non-imported spans are
 * sorted in address order.
 */

#include "config.h"
/* #include "mtlib.h" */
#include <sys/vmem_impl_user.h>
#if HAVE_ALLOCA_H
#include <alloca.h>
#endif
#ifdef HAVE_SYS_SYSMACROS_H
#include <sys/sysmacros.h>
#endif
#include <stdio.h>
#if HAVE_STRINGS_H
#include <strings.h>
#endif
#if HAVE_ATOMIC_H
#include <atomic.h>
#endif

#include "vmem_base.h"
#include "umem_base.h"

#define	VMEM_INITIAL		6	/* early vmem arenas */
#define	VMEM_SEG_INITIAL	100	/* early segments */

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
 * vmem_alloc(vmem_seg_arena)		-> 2 segs (span create + exact alloc)
 *  vmem_alloc(vmem_internal_arena)	-> 2 segs (span create + exact alloc)
 *   heap_alloc(heap_arena)
 *    vmem_alloc(heap_arena)		-> 4 seg (span create + alloc)
 *     parent_alloc(parent_arena)
 *	_vmem_extend_alloc(parent_arena) -> 3 seg (span create + left alloc)
 *
 * Note:  The reservation for heap_arena must be 4, since vmem_xalloc()
 * is overly pessimistic on allocations where parent_arena has a stricter
 * alignment than heap_arena.
 *
 * The worst-case consumption for any arena is 4 segment structures.
 * For now, we only support VM_NOSLEEP allocations, so as long as we
 * serialize all vmem_populates, a 4-seg reserve is sufficient.
 */
#define	VMEM_POPULATE_SEGS_PER_ARENA	4
#define	VMEM_POPULATE_LOCKS		1

#define	VMEM_POPULATE_RESERVE		\
	(VMEM_POPULATE_SEGS_PER_ARENA * VMEM_POPULATE_LOCKS)

/*
 * vmem_populate() ensures that each arena has VMEM_MINFREE seg structures
 * so that it can satisfy the worst-case allocation *and* participate in
 * worst-case allocation from vmem_seg_arena.
 */
#define	VMEM_MINFREE	(VMEM_POPULATE_RESERVE + VMEM_SEGS_PER_ALLOC_MAX)

/* Don't assume new statics are zeroed - see vmem_startup() */
static vmem_t vmem0[VMEM_INITIAL];
static vmem_t *vmem_populator[VMEM_INITIAL];
static uint32_t vmem_id;
static uint32_t vmem_populators;
static vmem_seg_t vmem_seg0[VMEM_SEG_INITIAL];
static vmem_seg_t *vmem_segfree;
static mutex_t vmem_list_lock = DEFAULTMUTEX;
static mutex_t vmem_segfree_lock = DEFAULTMUTEX;
static vmem_populate_lock_t vmem_nosleep_lock = {
  DEFAULTMUTEX,
  0
};
#define	IN_POPULATE()	(vmem_nosleep_lock.vmpl_thr == thr_self())
static vmem_t *vmem_list;
static vmem_t *vmem_internal_arena;
static vmem_t *vmem_seg_arena;
static vmem_t *vmem_hash_arena;
static vmem_t *vmem_vmem_arena;

vmem_t *vmem_heap;
vmem_alloc_t *vmem_heap_alloc;
vmem_free_t *vmem_heap_free;

uint32_t vmem_mtbf;		/* mean time between failures [default: off] */
size_t vmem_seg_size = sizeof (vmem_seg_t);

/*
 * we use the _ version, since we don't want to be cancelled.
 * Actually, this is automatically taken care of by including "mtlib.h".
 */
extern int _cond_wait(cond_t *cv, mutex_t *mutex);

/*
 * Insert/delete from arena list (type 'a') or next-of-kin list (type 'k').
 */
#define	VMEM_INSERT(vprev, vsp, type)					\
{									\
	vmem_seg_t *vnext = (vprev)->vs_##type##next;			\
	(vsp)->vs_##type##next = (vnext);				\
	(vsp)->vs_##type##prev = (vprev);				\
	(vprev)->vs_##type##next = (vsp);				\
	(vnext)->vs_##type##prev = (vsp);				\
}

#define	VMEM_DELETE(vsp, type)						\
{									\
	vmem_seg_t *vprev = (vsp)->vs_##type##prev;			\
	vmem_seg_t *vnext = (vsp)->vs_##type##next;			\
	(vprev)->vs_##type##next = (vnext);				\
	(vnext)->vs_##type##prev = (vprev);				\
}

/*
 * Get a vmem_seg_t from the global segfree list.
 */
static vmem_seg_t *
vmem_getseg_global(void)
{
	vmem_seg_t *vsp;

	(void) mutex_lock(&vmem_segfree_lock);
	if ((vsp = vmem_segfree) != NULL)
		vmem_segfree = vsp->vs_knext;
	(void) mutex_unlock(&vmem_segfree_lock);

	return (vsp);
}

/*
 * Put a vmem_seg_t on the global segfree list.
 */
static void
vmem_putseg_global(vmem_seg_t *vsp)
{
	(void) mutex_lock(&vmem_segfree_lock);
	vsp->vs_knext = vmem_segfree;
	vmem_segfree = vsp;
	(void) mutex_unlock(&vmem_segfree_lock);
}

/*
 * Get a vmem_seg_t from vmp's segfree list.
 */
static vmem_seg_t *
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
static void
vmem_putseg(vmem_t *vmp, vmem_seg_t *vsp)
{
	vsp->vs_knext = vmp->vm_segfree;
	vmp->vm_segfree = vsp;
	vmp->vm_nsegfree++;
}

/*
 * Add vsp to the appropriate freelist.
 */
static void
vmem_freelist_insert(vmem_t *vmp, vmem_seg_t *vsp)
{
	vmem_seg_t *vprev;

	ASSERT(*VMEM_HASH(vmp, vsp->vs_start) != vsp);

	vprev = (vmem_seg_t *)&vmp->vm_freelist[highbit(VS_SIZE(vsp)) - 1];
	vsp->vs_type = VMEM_FREE;
	vmp->vm_freemap |= VS_SIZE(vprev);
	VMEM_INSERT(vprev, vsp, k);

	(void) cond_broadcast(&vmp->vm_cv);
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
		vsp->vs_depth = (uint8_t)getpcstack(vsp->vs_stack,
		    VMEM_STACK_DEPTH, 0);
		vsp->vs_thread = thr_self();
		vsp->vs_timestamp = gethrtime();
	} else {
		vsp->vs_depth = 0;
	}

	vmp->vm_kstat.vk_alloc++;
	vmp->vm_kstat.vk_mem_inuse += VS_SIZE(vsp);
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
		vmp->vm_kstat.vk_lookup++;
		prev_vspp = &vsp->vs_knext;
	}

	if (vsp == NULL) {
		umem_panic("vmem_hash_delete(%p, %lx, %lu): bad free",
		    vmp, addr, size);
	}
	if (VS_SIZE(vsp) != size) {
		umem_panic("vmem_hash_delete(%p, %lx, %lu): wrong size "
		    "(expect %lu)", vmp, addr, size, VS_SIZE(vsp));
	}

	vmp->vm_kstat.vk_free++;
	vmp->vm_kstat.vk_mem_inuse -= size;

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

	VMEM_INSERT(vprev, newseg, a);

	return (newseg);
}

/*
 * Remove segment vsp from the arena.
 */
static void
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
	vmem_seg_t *knext;
	vmem_seg_t *newseg, *span;
	uintptr_t start = (uintptr_t)vaddr;
	uintptr_t end = start + size;

	knext = &vmp->vm_seg0;
	if (!import && vmp->vm_source_alloc == NULL) {
		vmem_seg_t *kend, *kprev;
		/*
		 * non-imported spans are sorted in address order.  This
		 * makes vmem_extend_unlocked() much more effective.
		 *
		 * We search in reverse order, since new spans are
		 * generally at higher addresses.
		 */
		kend = &vmp->vm_seg0;
		for (kprev = kend->vs_kprev; kprev != kend;
		    kprev = kprev->vs_kprev) {
			if (!kprev->vs_import && (kprev->vs_end - 1) < start)
				break;
		}
		knext = kprev->vs_knext;
	}

	ASSERT(MUTEX_HELD(&vmp->vm_lock));

	if ((start | end) & (vmp->vm_quantum - 1)) {
		umem_panic("vmem_span_create(%p, %p, %lu): misaligned",
		    vmp, vaddr, size);
	}

	span = vmem_seg_create(vmp, knext->vs_aprev, start, end);
	span->vs_type = VMEM_SPAN;
	VMEM_INSERT(knext->vs_kprev, span, k);

	newseg = vmem_seg_create(vmp, span, start, end);
	vmem_freelist_insert(vmp, newseg);

	newseg->vs_import = import;
	if (import)
		vmp->vm_kstat.vk_mem_import += size;
	vmp->vm_kstat.vk_mem_total += size;

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

	if (vsp->vs_import)
		vmp->vm_kstat.vk_mem_import -= size;
	vmp->vm_kstat.vk_mem_total -= size;

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

	/*
	 * If we're allocating from the start of the segment, and the
	 * remainder will be on the same freelist, we can save quite
	 * a bit of work.
	 */
	if (P2SAMEHIGHBIT(vs_size, vs_size - realsize) && addr == vs_start) {
		ASSERT(highbit(vs_size) == highbit(vs_size - realsize));
		vsp->vs_start = addr_end;
		vsp = vmem_seg_create(vmp, vsp->vs_aprev, addr, addr + size);
		vmem_hash_insert(vmp, vsp);
		return (vsp);
	}

	vmem_freelist_delete(vmp, vsp);

	if (vs_end != addr_end)
		vmem_freelist_insert(vmp,
		    vmem_seg_create(vmp, vsp, addr_end, vs_end));

	if (vs_start != addr)
		vmem_freelist_insert(vmp,
		    vmem_seg_create(vmp, vsp->vs_aprev, vs_start, addr));

	vsp->vs_start = addr;
	vsp->vs_end = addr + size;

	vmem_hash_insert(vmp, vsp);
	return (vsp);
}

/*
 * We cannot reap if we are in the middle of a vmem_populate().
 */
void
vmem_reap(void)
{
	if (!IN_POPULATE())
		umem_reap();
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
	vmem_populate_lock_t *lp;
	int i;

	while (vmp->vm_nsegfree < VMEM_MINFREE &&
	    (vsp = vmem_getseg_global()) != NULL)
		vmem_putseg(vmp, vsp);

	if (vmp->vm_nsegfree >= VMEM_MINFREE)
		return (1);

	/*
	 * If we're already populating, tap the reserve.
	 */
	if (vmem_nosleep_lock.vmpl_thr == thr_self()) {
		ASSERT(vmp->vm_cflags & VMC_POPULATOR);
		return (1);
	}

	(void) mutex_unlock(&vmp->vm_lock);

	ASSERT(vmflag & VM_NOSLEEP);	/* we do not allow sleep allocations */
	lp = &vmem_nosleep_lock;

	/*
	 * Cannot be just a mutex_lock(), since that has no effect if
	 * libthread is not linked.
	 */
	(void) mutex_lock(&lp->vmpl_mutex);
	ASSERT(lp->vmpl_thr == 0);
	lp->vmpl_thr = thr_self();

	nseg = VMEM_MINFREE + vmem_populators * VMEM_POPULATE_RESERVE;
	size = P2ROUNDUP(nseg * vmem_seg_size, vmem_seg_arena->vm_quantum);
	nseg = size / vmem_seg_size;

	/*
	 * The following vmem_alloc() may need to populate vmem_seg_arena
	 * and all the things it imports from.  When doing so, it will tap
	 * each arena's reserve to prevent recursion (see the block comment
	 * above the definition of VMEM_POPULATE_RESERVE).
	 *
	 * During this allocation, vmem_reap() is a no-op.  If the allocation
	 * fails, we call vmem_reap() after dropping the population lock.
	 */
	p = vmem_alloc(vmem_seg_arena, size, vmflag & VM_UMFLAGS);
	if (p == NULL) {
		lp->vmpl_thr = 0;
		(void) mutex_unlock(&lp->vmpl_mutex);
		vmem_reap();

		(void) mutex_lock(&vmp->vm_lock);
		vmp->vm_kstat.vk_populate_fail++;
		return (0);
	}
	/*
	 * Restock the arenas that may have been depleted during population.
	 */
	for (i = 0; i < vmem_populators; i++) {
		(void) mutex_lock(&vmem_populator[i]->vm_lock);
		while (vmem_populator[i]->vm_nsegfree < VMEM_POPULATE_RESERVE)
			vmem_putseg(vmem_populator[i],
			    (vmem_seg_t *)(p + --nseg * vmem_seg_size));
		(void) mutex_unlock(&vmem_populator[i]->vm_lock);
	}

	lp->vmpl_thr = 0;
	(void) mutex_unlock(&lp->vmpl_mutex);
	(void) mutex_lock(&vmp->vm_lock);

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
	if (vsp != NULL && vsp->vs_import && vmp->vm_source_free != NULL &&
	    vsp->vs_aprev->vs_type == VMEM_SPAN &&
	    vsp->vs_anext->vs_type == VMEM_SPAN) {
		void *vaddr = (void *)vsp->vs_start;
		size_t size = VS_SIZE(vsp);
		ASSERT(size == VS_SIZE(vsp->vs_aprev));
		vmem_freelist_delete(vmp, vsp);
		vmem_span_destroy(vmp, vsp);
		(void) mutex_unlock(&vmp->vm_lock);
		vmp->vm_source_free(vmp->vm_source, vaddr, size);
		(void) mutex_lock(&vmp->vm_lock);
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

	(void) mutex_lock(&vmp->vm_lock);

	if (vmp->vm_nsegfree < VMEM_MINFREE && !vmem_populate(vmp, vmflag)) {
		(void) mutex_unlock(&vmp->vm_lock);
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
		vmem_hash_insert(vmp,
		    vmem_seg_create(vmp, rotor->vs_aprev, addr, addr + size));
		(void) mutex_unlock(&vmp->vm_lock);
		return ((void *)addr);
	}

	/*
	 * Starting at the rotor, look for a segment large enough to
	 * satisfy the allocation.
	 */
	for (;;) {
		vmp->vm_kstat.vk_search++;
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
				(void) mutex_unlock(&vmp->vm_lock);
				return (vmem_xalloc(vmp, size, vmp->vm_quantum,
				    0, 0, NULL, NULL, vmflag & VM_UMFLAGS));
			}
			vmp->vm_kstat.vk_wait++;
			(void) _cond_wait(&vmp->vm_cv, &vmp->vm_lock);
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
	(void) mutex_unlock(&vmp->vm_lock);
	return ((void *)addr);
}

/*
 * Allocate size bytes at offset phase from an align boundary such that the
 * resulting segment [addr, addr + size) is a subset of [minaddr, maxaddr)
 * that does not straddle a nocross-aligned boundary.
 */
void *
vmem_xalloc(vmem_t *vmp, size_t size, size_t align, size_t phase,
	size_t nocross, void *minaddr, void *maxaddr, int vmflag)
{
	vmem_seg_t *vsp;
	vmem_seg_t *vbest = NULL;
	uintptr_t addr, taddr, start, end;
	void *vaddr;
	int hb, flist, resv;
	uint32_t mtbf;

	if (phase > 0 && phase >= align)
		umem_panic("vmem_xalloc(%p, %lu, %lu, %lu, %lu, %p, %p, %x): "
		    "invalid phase",
		    (void *)vmp, size, align, phase, nocross,
		    minaddr, maxaddr, vmflag);

	if (align == 0)
		align = vmp->vm_quantum;

	if ((align | phase | nocross) & (vmp->vm_quantum - 1)) {
		umem_panic("vmem_xalloc(%p, %lu, %lu, %lu, %lu, %p, %p, %x): "
		    "parameters not vm_quantum aligned",
		    (void *)vmp, size, align, phase, nocross,
		    minaddr, maxaddr, vmflag);
	}

	if (nocross != 0 &&
	    (align > nocross || P2ROUNDUP(phase + size, align) > nocross)) {
		umem_panic("vmem_xalloc(%p, %lu, %lu, %lu, %lu, %p, %p, %x): "
		    "overconstrained allocation",
		    (void *)vmp, size, align, phase, nocross,
		    minaddr, maxaddr, vmflag);
	}

	if ((mtbf = vmem_mtbf | vmp->vm_mtbf) != 0 && gethrtime() % mtbf == 0 &&
	    (vmflag & (VM_NOSLEEP | VM_PANIC)) == VM_NOSLEEP)
		return (NULL);

	(void) mutex_lock(&vmp->vm_lock);
	for (;;) {
		if (vmp->vm_nsegfree < VMEM_MINFREE &&
		    !vmem_populate(vmp, vmflag))
			break;

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
			vmp->vm_kstat.vk_search++;
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
			if (P2CROSS(taddr, taddr + size - 1, nocross))
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
		if (size == 0)
			umem_panic("vmem_xalloc(): size == 0");
		if (vmp->vm_source_alloc != NULL && nocross == 0 &&
		    minaddr == NULL && maxaddr == NULL) {
			size_t asize = P2ROUNDUP(size + phase,
			    MAX(align, vmp->vm_source->vm_quantum));
			if (asize < size) {		/* overflow */
				(void) mutex_unlock(&vmp->vm_lock);
				if (vmflag & VM_NOSLEEP)
					return (NULL);

				umem_panic("vmem_xalloc(): "
				    "overflow on VM_SLEEP allocation");
			}
			/*
			 * Determine how many segment structures we'll consume.
			 * The calculation must be presise because if we're
			 * here on behalf of vmem_populate(), we are taking
			 * segments from a very limited reserve.
			 */
			resv = (size == asize) ?
			    VMEM_SEGS_PER_SPAN_CREATE +
			    VMEM_SEGS_PER_EXACT_ALLOC :
			    VMEM_SEGS_PER_ALLOC_MAX;
			ASSERT(vmp->vm_nsegfree >= resv);
			vmp->vm_nsegfree -= resv;	/* reserve our segs */
			(void) mutex_unlock(&vmp->vm_lock);
			vaddr = vmp->vm_source_alloc(vmp->vm_source, asize,
			    vmflag & VM_UMFLAGS);
			(void) mutex_lock(&vmp->vm_lock);
			vmp->vm_nsegfree += resv;	/* claim reservation */
			if (vaddr != NULL) {
				vbest = vmem_span_create(vmp, vaddr, asize, 1);
				addr = P2PHASEUP(vbest->vs_start, align, phase);
				break;
			}
		}
		(void) mutex_unlock(&vmp->vm_lock);
		vmem_reap();
		(void) mutex_lock(&vmp->vm_lock);
		if (vmflag & VM_NOSLEEP)
			break;
		vmp->vm_kstat.vk_wait++;
		(void) _cond_wait(&vmp->vm_cv, &vmp->vm_lock);
	}
	if (vbest != NULL) {
		ASSERT(vbest->vs_type == VMEM_FREE);
		ASSERT(vbest->vs_knext != vbest);
		(void) vmem_seg_alloc(vmp, vbest, addr, size);
		(void) mutex_unlock(&vmp->vm_lock);
		ASSERT(P2PHASE(addr, align) == phase);
		ASSERT(!P2CROSS(addr, addr + size - 1, nocross));
		ASSERT(addr >= (uintptr_t)minaddr);
		ASSERT(addr + size - 1 <= (uintptr_t)maxaddr - 1);
		return ((void *)addr);
	}
	vmp->vm_kstat.vk_fail++;
	(void) mutex_unlock(&vmp->vm_lock);
	if (vmflag & VM_PANIC)
		umem_panic("vmem_xalloc(%p, %lu, %lu, %lu, %lu, %p, %p, %x): "
		    "cannot satisfy mandatory allocation",
		    (void *)vmp, size, align, phase, nocross,
		    minaddr, maxaddr, vmflag);
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

	(void) mutex_lock(&vmp->vm_lock);

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
	if (vsp->vs_import && vmp->vm_source_free != NULL &&
	    vsp->vs_aprev->vs_type == VMEM_SPAN &&
	    vsp->vs_anext->vs_type == VMEM_SPAN) {
		vaddr = (void *)vsp->vs_start;
		size = VS_SIZE(vsp);
		ASSERT(size == VS_SIZE(vsp->vs_aprev));
		vmem_span_destroy(vmp, vsp);
		(void) mutex_unlock(&vmp->vm_lock);
		vmp->vm_source_free(vmp->vm_source, vaddr, size);
	} else {
		vmem_freelist_insert(vmp, vsp);
		(void) mutex_unlock(&vmp->vm_lock);
	}
}

/*
 * Allocate size bytes from arena vmp.  Returns the allocated address
 * on success, NULL on failure.  vmflag specifies VM_SLEEP or VM_NOSLEEP,
 * and may also specify best-fit, first-fit, or next-fit allocation policy
 * instead of the default instant-fit policy.  VM_SLEEP allocations are
 * guaranteed to succeed.
 */
void *
vmem_alloc(vmem_t *vmp, size_t size, int vmflag)
{
	vmem_seg_t *vsp;
	uintptr_t addr;
	int hb;
	int flist = 0;
	uint32_t mtbf;

	if (size - 1 < vmp->vm_qcache_max) {
		ASSERT(vmflag & VM_NOSLEEP);
		return (_umem_cache_alloc(vmp->vm_qcache[(size - 1) >>
		    vmp->vm_qshift], UMEM_DEFAULT));
	}

	if ((mtbf = vmem_mtbf | vmp->vm_mtbf) != 0 && gethrtime() % mtbf == 0 &&
	    (vmflag & (VM_NOSLEEP | VM_PANIC)) == VM_NOSLEEP)
		return (NULL);

	if (vmflag & VM_NEXTFIT)
		return (vmem_nextfit_alloc(vmp, size, vmflag));

	if (vmflag & (VM_BESTFIT | VM_FIRSTFIT))
		return (vmem_xalloc(vmp, size, vmp->vm_quantum, 0, 0,
		    NULL, NULL, vmflag));

	/*
	 * Unconstrained instant-fit allocation from the segment list.
	 */
	(void) mutex_lock(&vmp->vm_lock);

	if (vmp->vm_nsegfree >= VMEM_MINFREE || vmem_populate(vmp, vmflag)) {
		if ((size & (size - 1)) == 0)
			flist = lowbit(P2ALIGN(vmp->vm_freemap, size));
		else if ((hb = highbit(size)) < VMEM_FREELISTS)
			flist = lowbit(P2ALIGN(vmp->vm_freemap, 1UL << hb));
	}

	if (flist-- == 0) {
		(void) mutex_unlock(&vmp->vm_lock);
		return (vmem_xalloc(vmp, size, vmp->vm_quantum,
		    0, 0, NULL, NULL, vmflag));
	}

	ASSERT(size <= (1UL << flist));
	vsp = vmp->vm_freelist[flist].vs_knext;
	addr = vsp->vs_start;
	(void) vmem_seg_alloc(vmp, vsp, addr, size);
	(void) mutex_unlock(&vmp->vm_lock);
	return ((void *)addr);
}

/*
 * Free the segment [vaddr, vaddr + size).
 */
void
vmem_free(vmem_t *vmp, void *vaddr, size_t size)
{
	if (size - 1 < vmp->vm_qcache_max)
		_umem_cache_free(vmp->vm_qcache[(size - 1) >> vmp->vm_qshift],
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

	(void) mutex_lock(&vmp->vm_lock);
	vmp->vm_kstat.vk_contains++;
	for (vsp = seg0->vs_knext; vsp != seg0; vsp = vsp->vs_knext) {
		vmp->vm_kstat.vk_contains_search++;
		ASSERT(vsp->vs_type == VMEM_SPAN);
		if (start >= vsp->vs_start && end - 1 <= vsp->vs_end - 1)
			break;
	}
	(void) mutex_unlock(&vmp->vm_lock);
	return (vsp != seg0);
}

/*
 * Add the span [vaddr, vaddr + size) to arena vmp.
 */
void *
vmem_add(vmem_t *vmp, void *vaddr, size_t size, int vmflag)
{
	if (vaddr == NULL || size == 0) {
		umem_panic("vmem_add(%p, %p, %lu): bad arguments",
		    vmp, vaddr, size);
	}

	ASSERT(!vmem_contains(vmp, vaddr, size));

	(void) mutex_lock(&vmp->vm_lock);
	if (vmem_populate(vmp, vmflag))
		(void) vmem_span_create(vmp, vaddr, size, 0);
	else
		vaddr = NULL;
	(void) cond_broadcast(&vmp->vm_cv);
	(void) mutex_unlock(&vmp->vm_lock);
	return (vaddr);
}

/*
 * Adds the address range [addr, endaddr) to arena vmp, by either:
 *   1. joining two existing spans, [x, addr), and [endaddr, y) (which
 *      are in that order) into a single [x, y) span,
 *   2. expanding an existing [x, addr) span to [x, endaddr),
 *   3. expanding an existing [endaddr, x) span to [addr, x), or
 *   4. creating a new [addr, endaddr) span.
 *
 * Called with vmp->vm_lock held, and a successful vmem_populate() completed.
 * Cannot fail.  Returns the new segment.
 *
 * NOTE:  this algorithm is linear-time in the number of spans, but is
 *      constant-time when you are extending the last (highest-addressed)
 *      span.
 */
static vmem_seg_t *
vmem_extend_unlocked(vmem_t *vmp, uintptr_t addr, uintptr_t endaddr)
{
	vmem_seg_t *span;
	vmem_seg_t *vsp;

	vmem_seg_t *end = &vmp->vm_seg0;

	ASSERT(MUTEX_HELD(&vmp->vm_lock));

	/*
	 * the second "if" clause below relies on the direction of this search
	 */
	for (span = end->vs_kprev; span != end; span = span->vs_kprev) {
		if (span->vs_end == addr || span->vs_start == endaddr)
			break;
	}

	if (span == end)
		return (vmem_span_create(vmp, (void *)addr, endaddr - addr, 0));
	if (span->vs_kprev->vs_end == addr && span->vs_start == endaddr) {
		vmem_seg_t *prevspan = span->vs_kprev;
		vmem_seg_t *nextseg = span->vs_anext;
		vmem_seg_t *prevseg = span->vs_aprev;

		/*
		 * prevspan becomes the span marker for the full range
		 */
		prevspan->vs_end = span->vs_end;

		/*
		 * Notionally, span becomes a free segment representing
		 * [addr, endaddr).
		 *
		 * However, if either of its neighbors are free, we coalesce
		 * by destroying span and changing the free segment.
		 */
		if (prevseg->vs_type == VMEM_FREE &&
		    nextseg->vs_type == VMEM_FREE) {
			/*
			 * coalesce both ways
			 */
			ASSERT(prevseg->vs_end == addr &&
			    nextseg->vs_start == endaddr);

			vmem_freelist_delete(vmp, prevseg);
			prevseg->vs_end = nextseg->vs_end;

			vmem_freelist_delete(vmp, nextseg);
			VMEM_DELETE(span, k);
			vmem_seg_destroy(vmp, nextseg);
			vmem_seg_destroy(vmp, span);

			vsp = prevseg;
		} else if (prevseg->vs_type == VMEM_FREE) {
			/*
			 * coalesce left
			 */
			ASSERT(prevseg->vs_end == addr);

			VMEM_DELETE(span, k);
			vmem_seg_destroy(vmp, span);

			vmem_freelist_delete(vmp, prevseg);
			prevseg->vs_end = endaddr;

			vsp = prevseg;
		} else if (nextseg->vs_type == VMEM_FREE) {
			/*
			 * coalesce right
			 */
			ASSERT(nextseg->vs_start == endaddr);

			VMEM_DELETE(span, k);
			vmem_seg_destroy(vmp, span);

			vmem_freelist_delete(vmp, nextseg);
			nextseg->vs_start = addr;

			vsp = nextseg;
		} else {
			/*
			 * cannnot coalesce
			 */
			VMEM_DELETE(span, k);
			span->vs_start = addr;
			span->vs_end = endaddr;

			vsp = span;
		}
	} else if (span->vs_end == addr) {
		vmem_seg_t *oldseg = span->vs_knext->vs_aprev;
		span->vs_end = endaddr;

		ASSERT(oldseg->vs_type != VMEM_SPAN);
		if (oldseg->vs_type == VMEM_FREE) {
			ASSERT(oldseg->vs_end == addr);
			vmem_freelist_delete(vmp, oldseg);
			oldseg->vs_end = endaddr;
			vsp = oldseg;
		} else
			vsp = vmem_seg_create(vmp, oldseg, addr, endaddr);
	} else {
		vmem_seg_t *oldseg = span->vs_anext;
		ASSERT(span->vs_start == endaddr);
		span->vs_start = addr;

		ASSERT(oldseg->vs_type != VMEM_SPAN);
		if (oldseg->vs_type == VMEM_FREE) {
			ASSERT(oldseg->vs_start == endaddr);
			vmem_freelist_delete(vmp, oldseg);
			oldseg->vs_start = addr;
			vsp = oldseg;
		} else
			vsp = vmem_seg_create(vmp, span, addr, endaddr);
	}
	vmem_freelist_insert(vmp, vsp);
	vmp->vm_kstat.vk_mem_total += (endaddr - addr);
	return (vsp);
}

/*
 * Does some error checking, calls vmem_extend_unlocked to add
 * [vaddr, vaddr+size) to vmp, then allocates alloc bytes from the
 * newly merged segment.
 */
void *
_vmem_extend_alloc(vmem_t *vmp, void *vaddr, size_t size, size_t alloc,
    int vmflag)
{
	uintptr_t addr = (uintptr_t)vaddr;
	uintptr_t endaddr = addr + size;
	vmem_seg_t *vsp;

	ASSERT(vaddr != NULL && size != 0 && endaddr > addr);
	ASSERT(alloc <= size && alloc != 0);
	ASSERT(((addr | size | alloc) & (vmp->vm_quantum - 1)) == 0);

	ASSERT(!vmem_contains(vmp, vaddr, size));

	(void) mutex_lock(&vmp->vm_lock);
	if (!vmem_populate(vmp, vmflag)) {
		(void) mutex_unlock(&vmp->vm_lock);
		return (NULL);
	}
	/*
	 * if there is a source, we can't mess with the spans
	 */
	if (vmp->vm_source_alloc != NULL)
		vsp = vmem_span_create(vmp, vaddr, size, 0);
	else
		vsp = vmem_extend_unlocked(vmp, addr, endaddr);

	ASSERT(VS_SIZE(vsp) >= alloc);

	addr = vsp->vs_start;
	(void) vmem_seg_alloc(vmp, vsp, addr, alloc);
	vaddr = (void *)addr;

	(void) cond_broadcast(&vmp->vm_cv);
	(void) mutex_unlock(&vmp->vm_lock);

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

	bzero(&walker, sizeof (walker));
	walker.vs_type = VMEM_WALKER;

	(void) mutex_lock(&vmp->vm_lock);
	VMEM_INSERT(seg0, &walker, a);
	for (vsp = seg0->vs_anext; vsp != seg0; vsp = vsp->vs_anext) {
		if (vsp->vs_type & typemask) {
			void *start = (void *)vsp->vs_start;
			size_t size = VS_SIZE(vsp);
			if (typemask & VMEM_REENTRANT) {
				vmem_advance(vmp, &walker, vsp);
				(void) mutex_unlock(&vmp->vm_lock);
				func(arg, start, size);
				(void) mutex_lock(&vmp->vm_lock);
				vsp = &walker;
			} else {
				func(arg, start, size);
			}
		}
	}
	vmem_advance(vmp, &walker, NULL);
	(void) mutex_unlock(&vmp->vm_lock);
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
	uint64_t size = 0;

	if (typemask & VMEM_ALLOC)
		size += vmp->vm_kstat.vk_mem_inuse;
	if (typemask & VMEM_FREE)
		size += vmp->vm_kstat.vk_mem_total -
		    vmp->vm_kstat.vk_mem_inuse;
	return ((size_t)size);
}

/*
 * Create an arena called name whose initial span is [base, base + size).
 * The arena's natural unit of currency is quantum, so vmem_alloc()
 * guarantees quantum-aligned results.  The arena may import new spans
 * by invoking afunc() on source, and may return those spans by invoking
 * ffunc() on source.  To make small allocations fast and scalable,
 * the arena offers high-performance caching for each integer multiple
 * of quantum up to qcache_max.
 */
vmem_t *
vmem_create(const char *name, void *base, size_t size, size_t quantum,
	vmem_alloc_t *afunc, vmem_free_t *ffunc, vmem_t *source,
	size_t qcache_max, int vmflag)
{
	int i;
	size_t nqcache;
	vmem_t *vmp, *cur, **vmpp;
	vmem_seg_t *vsp;
	vmem_freelist_t *vfp;
	uint32_t id = atomic_add_32_nv(&vmem_id, 1);

	if (vmem_vmem_arena != NULL) {
		vmp = vmem_alloc(vmem_vmem_arena, sizeof (vmem_t),
		    vmflag & VM_UMFLAGS);
	} else {
		ASSERT(id <= VMEM_INITIAL);
		vmp = &vmem0[id - 1];
	}

	if (vmp == NULL)
		return (NULL);
	bzero(vmp, sizeof (vmem_t));

	(void) snprintf(vmp->vm_name, VMEM_NAMELEN, "%s", name);
	(void) mutex_init(&vmp->vm_lock, USYNC_THREAD, NULL);
	(void) cond_init(&vmp->vm_cv, USYNC_THREAD, NULL);
	vmp->vm_cflags = vmflag;
	vmflag &= VM_UMFLAGS;

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

	vsp = &vmp->vm_rotor;
	vsp->vs_type = VMEM_ROTOR;
	VMEM_INSERT(&vmp->vm_seg0, vsp, a);

	vmp->vm_id = id;
	if (source != NULL)
		vmp->vm_kstat.vk_source_id = source->vm_id;
	vmp->vm_source = source;
	vmp->vm_source_alloc = afunc;
	vmp->vm_source_free = ffunc;

	if (nqcache != 0) {
		vmp->vm_qcache_max = nqcache << vmp->vm_qshift;
		for (i = 0; i < nqcache; i++) {
			char buf[VMEM_NAMELEN + 21];
			(void) snprintf(buf, sizeof (buf), "%s_%lu",
			    vmp->vm_name, (long)((i + 1) * quantum));
			vmp->vm_qcache[i] = umem_cache_create(buf,
			    (i + 1) * quantum, quantum, NULL, NULL, NULL,
			    NULL, vmp, UMC_QCACHE | UMC_NOTOUCH);
			if (vmp->vm_qcache[i] == NULL) {
				vmp->vm_qcache_max = i * quantum;
				break;
			}
		}
	}

	(void) mutex_lock(&vmem_list_lock);
	vmpp = &vmem_list;
	while ((cur = *vmpp) != NULL)
		vmpp = &cur->vm_next;
	*vmpp = vmp;
	(void) mutex_unlock(&vmem_list_lock);

	if (vmp->vm_cflags & VMC_POPULATOR) {
		uint_t pop_id = atomic_add_32_nv(&vmem_populators, 1);
		ASSERT(pop_id <= VMEM_INITIAL);
		vmem_populator[pop_id - 1] = vmp;
		(void) mutex_lock(&vmp->vm_lock);
		(void) vmem_populate(vmp, vmflag | VM_PANIC);
		(void) mutex_unlock(&vmp->vm_lock);
	}

	if ((base || size) && vmem_add(vmp, base, size, vmflag) == NULL) {
		vmem_destroy(vmp);
		return (NULL);
	}

	return (vmp);
}

/*
 * Destroy arena vmp.
 */
void
vmem_destroy(vmem_t *vmp)
{
	vmem_t *cur, **vmpp;
	vmem_seg_t *seg0 = &vmp->vm_seg0;
	vmem_seg_t *vsp;
	size_t leaked;
	int i;

	(void) mutex_lock(&vmem_list_lock);
	vmpp = &vmem_list;
	while ((cur = *vmpp) != vmp)
		vmpp = &cur->vm_next;
	*vmpp = vmp->vm_next;
	(void) mutex_unlock(&vmem_list_lock);

	for (i = 0; i < VMEM_NQCACHE_MAX; i++)
		if (vmp->vm_qcache[i])
			umem_cache_destroy(vmp->vm_qcache[i]);

	leaked = vmem_size(vmp, VMEM_ALLOC);
	if (leaked != 0)
		umem_printf("vmem_destroy('%s'): leaked %lu bytes",
		    vmp->vm_name, leaked);

	if (vmp->vm_hash_table != vmp->vm_hash0)
		vmem_free(vmem_hash_arena, vmp->vm_hash_table,
		    (vmp->vm_hash_mask + 1) * sizeof (void *));

	/*
	 * Give back the segment structures for anything that's left in the
	 * arena, e.g. the primary spans and their free segments.
	 */
	VMEM_DELETE(&vmp->vm_rotor, a);
	for (vsp = seg0->vs_anext; vsp != seg0; vsp = vsp->vs_anext)
		vmem_putseg_global(vsp);

	while (vmp->vm_nsegfree > 0)
		vmem_putseg_global(vmem_getseg(vmp));

	(void) mutex_destroy(&vmp->vm_lock);
	(void) cond_destroy(&vmp->vm_cv);
	vmem_free(vmem_vmem_arena, vmp, sizeof (vmem_t));
}

/*
 * Resize vmp's hash table to keep the average lookup depth near 1.0.
 */
static void
vmem_hash_rescale(vmem_t *vmp)
{
	vmem_seg_t **old_table, **new_table, *vsp;
	size_t old_size, new_size, h, nseg;

	nseg = (size_t)(vmp->vm_kstat.vk_alloc - vmp->vm_kstat.vk_free);

	new_size = MAX(VMEM_HASH_INITIAL, 1 << (highbit(3 * nseg + 4) - 2));
	old_size = vmp->vm_hash_mask + 1;

	if ((old_size >> 1) <= new_size && new_size <= (old_size << 1))
		return;

	new_table = vmem_alloc(vmem_hash_arena, new_size * sizeof (void *),
	    VM_NOSLEEP);
	if (new_table == NULL)
		return;
	bzero(new_table, new_size * sizeof (void *));

	(void) mutex_lock(&vmp->vm_lock);

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

	(void) mutex_unlock(&vmp->vm_lock);

	if (old_table != vmp->vm_hash0)
		vmem_free(vmem_hash_arena, old_table,
		    old_size * sizeof (void *));
}

/*
 * Perform periodic maintenance on all vmem arenas.
 */
/*ARGSUSED*/
void
vmem_update(void *dummy)
{
	vmem_t *vmp;

	(void) mutex_lock(&vmem_list_lock);
	for (vmp = vmem_list; vmp != NULL; vmp = vmp->vm_next) {
		/*
		 * If threads are waiting for resources, wake them up
		 * periodically so they can issue another vmem_reap()
		 * to reclaim resources cached by the slab allocator.
		 */
		(void) cond_broadcast(&vmp->vm_cv);

		/*
		 * Rescale the hash table to keep the hash chains short.
		 */
		vmem_hash_rescale(vmp);
	}
	(void) mutex_unlock(&vmem_list_lock);
}

/*
 * If vmem_init is called again, we need to be able to reset the world.
 * That includes resetting the statics back to their original values.
 */
void
vmem_startup(void)
{
#ifdef UMEM_STANDALONE
	vmem_id = 0;
	vmem_populators = 0;
	vmem_segfree = NULL;
	vmem_list = NULL;
	vmem_internal_arena = NULL;
	vmem_seg_arena = NULL;
	vmem_hash_arena = NULL;
	vmem_vmem_arena = NULL;
	vmem_heap = NULL;
	vmem_heap_alloc = NULL;
	vmem_heap_free = NULL;

	bzero(vmem0, sizeof (vmem0));
	bzero(vmem_populator, sizeof (vmem_populator));
	bzero(vmem_seg0, sizeof (vmem_seg0));
#endif
}

/*
 * Prepare vmem for use.
 */
vmem_t *
vmem_init(const char *parent_name, size_t parent_quantum,
    vmem_alloc_t *parent_alloc, vmem_free_t *parent_free,
    const char *heap_name, void *heap_start, size_t heap_size,
    size_t heap_quantum, vmem_alloc_t *heap_alloc, vmem_free_t *heap_free)
{
	uint32_t id;
	int nseg = VMEM_SEG_INITIAL;
	vmem_t *parent, *heap;

	ASSERT(vmem_internal_arena == NULL);

	while (--nseg >= 0)
		vmem_putseg_global(&vmem_seg0[nseg]);

	if (parent_name != NULL) {
		parent = vmem_create(parent_name,
		    heap_start, heap_size, parent_quantum,
		    NULL, NULL, NULL, 0,
		    VM_SLEEP | VMC_POPULATOR);
		heap_start = NULL;
		heap_size = 0;
	} else {
		ASSERT(parent_alloc == NULL && parent_free == NULL);
		parent = NULL;
	}

	heap = vmem_create(heap_name,
	    heap_start, heap_size, heap_quantum,
	    parent_alloc, parent_free, parent, 0,
	    VM_SLEEP | VMC_POPULATOR);

	vmem_heap = heap;
	vmem_heap_alloc = heap_alloc;
	vmem_heap_free = heap_free;

	vmem_internal_arena = vmem_create("vmem_internal",
	    NULL, 0, heap_quantum,
	    heap_alloc, heap_free, heap, 0,
	    VM_SLEEP | VMC_POPULATOR);

	vmem_seg_arena = vmem_create("vmem_seg",
	    NULL, 0, heap_quantum,
	    vmem_alloc, vmem_free, vmem_internal_arena, 0,
	    VM_SLEEP | VMC_POPULATOR);

	vmem_hash_arena = vmem_create("vmem_hash",
	    NULL, 0, 8,
	    vmem_alloc, vmem_free, vmem_internal_arena, 0,
	    VM_SLEEP);

	vmem_vmem_arena = vmem_create("vmem_vmem",
	    vmem0, sizeof (vmem0), 1,
	    vmem_alloc, vmem_free, vmem_internal_arena, 0,
	    VM_SLEEP);

	for (id = 0; id < vmem_id; id++)
		(void) vmem_xalloc(vmem_vmem_arena, sizeof (vmem_t),
		    1, 0, 0, &vmem0[id], &vmem0[id + 1],
		    VM_NOSLEEP | VM_BESTFIT | VM_PANIC);

	return (heap);
}

void
vmem_no_debug(void)
{
	/*
	 * This size must be a multiple of the minimum required alignment,
	 * since vmem_populate allocates them compactly.
	 */
	vmem_seg_size = P2ROUNDUP(offsetof(vmem_seg_t, vs_thread),
	    sizeof (hrtime_t));
}

/*
 * Lockup and release, for fork1(2) handling.
 */
void
vmem_lockup(void)
{
	vmem_t *cur;

	(void) mutex_lock(&vmem_list_lock);
	(void) mutex_lock(&vmem_nosleep_lock.vmpl_mutex);

	/*
	 * Lock up and broadcast all arenas.
	 */
	for (cur = vmem_list; cur != NULL; cur = cur->vm_next) {
		(void) mutex_lock(&cur->vm_lock);
		(void) cond_broadcast(&cur->vm_cv);
	}

	(void) mutex_lock(&vmem_segfree_lock);
}

void
vmem_release(void)
{
	vmem_t *cur;

	(void) mutex_unlock(&vmem_nosleep_lock.vmpl_mutex);

	for (cur = vmem_list; cur != NULL; cur = cur->vm_next)
		(void) mutex_unlock(&cur->vm_lock);

	(void) mutex_unlock(&vmem_segfree_lock);
	(void) mutex_unlock(&vmem_list_lock);
}
