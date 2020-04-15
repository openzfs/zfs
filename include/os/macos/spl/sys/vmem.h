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
 * Copyright (c) 1999, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012 by Delphix. All rights reserved.
 */

#ifndef _SYS_VMEM_H
#define	_SYS_VMEM_H

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif


// Make sure IOMalloc uses kernel_map, and not kalloc.zones
// This is from XNU kalloc_max and kalloc_kernmap_size
// #define	KMEM_QUANTUM (PAGESIZE << 2) * 16 + PAGESIZE;
#define	KMEM_QUANTUM PAGESIZE

	/*
	 * Per-allocation flags
	 */
#define	VM_SLEEP	0x00000000 /* same as KM_SLEEP */
#define	VM_NOSLEEP	0x00000001 /* same as KM_NOSLEEP */
#define	VM_PANIC	0x00000002 /* same as KM_PANIC */
#define	VM_PUSHPAGE	0x00000004 /* same as KM_PUSHPAGE */
#define	VM_NORMALPRI	0x00000008 /* same as KM_NORMALPRI */
#define	VM_NODEBUG	0x00000010 /* matches KM_NODE~BUG, */
				    /* not implemented on OSX */
#define	VM_NO_VBA	0x00000020 /* OSX: do not descend to the bucket layer */
#define	VM_KMFLAGS	0x000000ff /* flags that must match KM_* flags */

#define	VM_BESTFIT	0x00000100
#define	VM_FIRSTFIT	0x00000200
#define	VM_NEXTFIT	0x00000400

/*
 * The following flags are restricted for use only within the kernel.
 * VM_MEMLOAD is for use by the HAT to avoid infinite recursion.
 * VM_NORELOC is used by the kernel when static VA->PA mappings are required.
 */
#define	VM_MEMLOAD	0x00000800
#define	VM_NORELOC	0x00001000

/*
 * VM_ABORT requests that vmem_alloc() *ignore* the VM_SLEEP/VM_NOSLEEP flags
 * and forgo reaping if the allocation or attempted import, fails.  This
 * flag is a segkmem-specific flag, and should not be used by anyone else.
 */
#define	VM_ABORT	0x00002000

/*
 * VM_ENDALLOC requests that large addresses be preferred in allocations.
 * Has no effect if VM_NEXTFIT is active.
 */
#define	VM_ENDALLOC	0x00004000

#define	VM_FLAGS	0x0000FFFF

/*
 * Arena creation flags
 */
#define	VMC_POPULATOR	0x00010000
#define	VMC_NO_QCACHE	0x00020000	/* cannot use quantum caches */
#define	VMC_IDENTIFIER	0x00040000	/* not backed by memory */
// VMC_XALLOC   0x00080000 below
// VMC_XALIGN   0x00100000 below
#define	VMC_DUMPSAFE	0x00200000	/* can use alternate dump memory */
// KMC_IDENTIFIER == 0x00400000
// KMC_PREFILL ==    0x00800000
#define	VMC_TIMEFREE	0x01000000	/* keep span creation time, */
					/* newest spans to front */
#define	VMC_OLDFIRST	0x02000000	/* must accompany VMC_TIMEFREE, */
					/* oldest spans to front */

/*
 * internal use only;	the import function uses the vmem_ximport_t interface
 *			and may increase the request size if it so desires.
 *			VMC_XALIGN, for use with vmem_xcreate, specifies that
 *			the address returned by the import function will be
 *			aligned according to the alignment argument.
 */
#define	VMC_XALLOC		0x00080000
#define	VMC_XALIGN		0x00100000
#define	VMC_FLAGS		0xFFFF0000

/*
 * Public segment types
 */
#define	VMEM_ALLOC		0x01
#define	VMEM_FREE		0x02

/*
 * Implementation-private segment types
 */
#define	VMEM_SPAN		0x10
#define	VMEM_ROTOR		0x20
#define	VMEM_WALKER		0x40

/*
 * VMEM_REENTRANT indicates to vmem_walk() that the callback routine may
 * call back into the arena being walked, so vmem_walk() must drop the
 * arena lock before each callback.  The caveat is that since the arena
 * isn't locked, its state can change.  Therefore it is up to the callback
 * routine to handle cases where the segment isn't of the expected type.
 * For example, we use this to walk heap_arena when generating a crash dump;
 * see segkmem_dump() for sample usage.
 */
#define	VMEM_REENTRANT	0x80000000

struct vmem;

typedef struct vmem vmem_t;
typedef void *(vmem_alloc_t)(vmem_t *, size_t, int);
typedef void (vmem_free_t)(vmem_t *, void *, size_t);

/*
 * Alternate import style; the requested size is passed in a pointer,
 * which can be increased by the import function if desired.
 */
typedef void *(vmem_ximport_t)(vmem_t *, size_t *, size_t, int);

#ifdef _KERNEL
extern vmem_t *vmem_init(const char *, void *, size_t, size_t,
    vmem_alloc_t *, vmem_free_t *);
extern void    vmem_fini(vmem_t *);
extern void vmem_update(void *);
extern int vmem_is_populator(void);
extern size_t vmem_seg_size;
#endif

extern vmem_t *vmem_create(const char *, void *, size_t, size_t,
    vmem_alloc_t *, vmem_free_t *, vmem_t *, size_t, int);
extern vmem_t *vmem_xcreate(const char *, void *, size_t, size_t,
    vmem_ximport_t *, vmem_free_t *, vmem_t *, size_t, int);
extern void vmem_destroy(vmem_t *);
extern void *vmem_alloc_impl(vmem_t *, size_t, int);
extern void *vmem_xalloc(vmem_t *, size_t, size_t, size_t, size_t,
    void *, void *, int);
extern void vmem_free_impl(vmem_t *, void *, size_t);
extern void vmem_xfree(vmem_t *, void *, size_t);
extern void *vmem_add(vmem_t *, void *, size_t, int);
extern int vmem_contains(vmem_t *, void *, size_t);
extern void vmem_walk(vmem_t *, int, void (*)(void *, void *, size_t), void *);
extern size_t vmem_size(vmem_t *, int);
extern size_t vmem_size_locked(vmem_t *, int);
extern size_t vmem_size_semi_atomic(vmem_t *, int);
extern void vmem_qcache_reap(vmem_t *vmp);
extern int64_t vmem_buckets_size(int);
extern int64_t abd_arena_empty_space(void);
extern int64_t abd_arena_total_size(void);

/*
 * Since Linux code uses vmem_alloc()/vmem_free and it is not
 * the illumos one, wrap kmem_alloc()/kmem_free.
 */
#define	vmem_free(A, B)		zfs_kmem_free((A), (B))
#define	vmem_alloc(A, B)	zfs_kmem_alloc((A), (B))
#define	vmem_zalloc(A, B)	zfs_kmem_zalloc((A), (B))

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_VMEM_H */
