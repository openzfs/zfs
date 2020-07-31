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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _VM_SEG_KMEM_H
#define	_VM_SEG_KMEM_H


#ifdef	__cplusplus
extern "C" {
#endif

//#include <sys/types.h>
//#include <sys/vnode.h>
#include <sys/vmem.h>
//#include <vm/as.h>
//#include <vm/seg.h>
//#include <vm/page.h>

	/*
	 * VM - Kernel Segment Driver
	 */

#if defined(_KERNEL)

	extern uint64_t segkmem_total_allocated;

//	extern char *kernelheap;	/* start of primary kernel heap */
//	extern char *ekernelheap;	/* end of primary kernel heap */
//	extern char *heap_lp_base;	/* start of kernel large page heap arena */
//	extern char *heap_lp_end;	/* end of kernel large page heap arena */
//	extern struct seg kvseg;	/* primary kernel heap segment */
//	extern struct seg kvseg_core;	/* "core" kernel heap segment */
//	extern struct seg kzioseg;	/* Segment for zio mappings */
//	extern vmem_t *heap_lp_arena;	/* kernel large page heap arena */
//	extern vmem_t *heap_arena;	/* primary kernel heap arena */
//	extern vmem_t *hat_memload_arena; /* HAT translation arena */
//	extern struct seg kvseg32;	/* 32-bit kernel heap segment */
//	extern vmem_t *heap32_arena;	/* 32-bit kernel heap arena */
//	extern vmem_t *heaptext_arena;	/* kernel text arena, from heap */
//	extern struct as kas;		/* kernel address space */
//	extern int segkmem_reloc;	/* enable/disable segkmem relocatable pages */
//	extern vmem_t *static_arena;	/* arena for caches to import static memory */
//	extern vmem_t *static_alloc_arena;	/* arena for allocating static memory */
	extern vmem_t *zio_arena_parent; /* qcaching for zio arenas and abd arena */
	extern vmem_t *zio_arena;	/* arena for zio caches for file blocks */
	extern vmem_t *zio_metadata_arena;	/* arena for zio caches for (zfs) metadata blocks */
//	extern struct vnode kvps[];
	/*
	 * segkmem page vnodes
	 */
#define	kvp		(kvps[KV_KVP])
#define	zvp		(kvps[KV_ZVP])
#if defined(__sparc)
#define	mpvp		(kvps[KV_MPVP])
#define	promvp		(kvps[KV_PROMVP])
#endif	/* __sparc */

//	extern int segkmem_create(struct seg *);
//	extern page_t *segkmem_page_create(void *, uint32_t, int, void *);
//	extern void *segkmem_xalloc(vmem_t *, void *, uint32_t, int, uint_t,
//								page_t *(*page_create_func)(void *, uint32_t, int, void *), void *);
	void *segkmem_alloc(vmem_t *, uint32_t, int);
//	extern void *segkmem_alloc_permanent(vmem_t *, uint32_t, int);
	extern void segkmem_free(vmem_t *, void *, uint32_t);
//	extern void segkmem_xfree(vmem_t *, void *, uint32_t, void (*)(page_t *));

//	extern void *boot_alloc(void *, uint32_t, uint_t);
//	extern void boot_mapin(caddr_t addr, uint32_t size);
	extern void kernelheap_init(void);
	extern void kernelheap_fini(void);
//	extern void segkmem_gc(void);

	extern void *segkmem_zio_alloc(vmem_t *, uint32_t, int);
//	extern int segkmem_zio_create(struct seg *);
	extern void segkmem_zio_free(vmem_t *, void *, uint32_t);
	extern void segkmem_zio_init(void);
	extern void segkmem_zio_fini(void);

	/*
	 * Flags for segkmem_xalloc().
	 *
	 * SEGKMEM_SHARELOCKED requests pages which are locked SE_SHARED to be
	 * returned rather than unlocked which is now the default.  Note that
	 * memory returned by SEGKMEM_SHARELOCKED cannot be freed by segkmem_free().
	 * This is a hack for seg_dev that should be cleaned up in the future.
	 */
#define	SEGKMEM_SHARELOCKED	0x20000

	/*
	 * Large page for kmem caches support
	 */
//	typedef struct segkmem_lpcb {
//		kmutex_t	lp_lock;
//		kcondvar_t	lp_cv;
//		uint_t		lp_wait;
//		uint_t		lp_uselp;
//		ulong_t		lp_throttle;

		/* stats */
//		uint64_t	sleep_allocs_failed;
//		uint64_t	nosleep_allocs_failed;
//		uint64_t	allocs_throttled;
//		uint64_t	allocs_limited;
//		uint64_t	alloc_bytes_failed;
//	} segkmem_lpcb_t;

//	extern void	*segkmem_alloc_lp(vmem_t *, uint32_t *, uint32_t, int);
//	extern void	segkmem_free_lp(vmem_t *, void *, uint32_t);
//	extern int	segkmem_lpsetup();
//	extern void	segkmem_heap_lp_init(void);

//	extern uint32_t	segkmem_lpsize;
//	extern int	segkmem_lpszc;
//	extern uint32_t	segkmem_heaplp_quantum;
//	extern uint32_t	segkmem_kmemlp_max;

#define	SEGKMEM_USE_LARGEPAGES (segkmem_lpsize > PAGESIZE)

#define	IS_KMEM_VA_LARGEPAGE(vaddr)				        \
(((vaddr) >= heap_lp_base) && ((vaddr) < heap_lp_end))

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _VM_SEG_KMEM_H */
