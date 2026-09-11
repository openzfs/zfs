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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _VM_SEG_KMEM_H
#define	_VM_SEG_KMEM_H


#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/vmem.h>

/*
 * VM - Kernel Segment Driver
 */

#if defined(_KERNEL)

extern uint64_t segkmem_total_allocated;

/* segregated vmem arenas for abd */
extern vmem_t *abd_arena;
extern vmem_t *abd_subpage_arena;

/*
 * segkmem page vnodes
 */
// #define	kvp		(kvps[KV_KVP])
// #define	zvp		(kvps[KV_ZVP])
#if defined(__sparc)
#define	mpvp		(kvps[KV_MPVP])
#define	promvp		(kvps[KV_PROMVP])
#endif	/* __sparc */

void *segkmem_alloc(vmem_t *, size_t, int);
extern void segkmem_free(vmem_t *, const void *, size_t);
extern void kernelheap_init(void);
extern void kernelheap_fini(void);

extern void segkmem_abd_init(void);
extern void segkmem_abd_fini(void);

/*
 * Flags for segkmem_xalloc().
 *
 * SEGKMEM_SHARELOCKED requests pages which are locked SE_SHARED to be
 * returned rather than unlocked which is now the default.  Note that
 * memory returned by SEGKMEM_SHARELOCKED cannot be freed by segkmem_free().
 * This is a hack for seg_dev that should be cleaned up in the future.
 */
#define	SEGKMEM_SHARELOCKED	0x20000

#define	SEGKMEM_USE_LARGEPAGES (segkmem_lpsize > PAGESIZE)

#define	IS_KMEM_VA_LARGEPAGE(vaddr)				        \
	(((vaddr) >= heap_lp_base) && ((vaddr) < heap_lp_end))

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _VM_SEG_KMEM_H */
