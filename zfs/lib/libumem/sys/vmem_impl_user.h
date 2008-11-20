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
 * Copyright 1999-2002 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Portions Copyright 2006 OmniTI, Inc.
 */

#ifndef _SYS_VMEM_IMPL_USER_H
#define	_SYS_VMEM_IMPL_USER_H

/* #pragma ident	"@(#)vmem_impl_user.h	1.2	05/06/08 SMI" */

#if HAVE_SYS_KSTAT
#include <sys/kstat.h>
#endif
#ifndef _WIN32
#include <sys/time.h>
#endif
#include <sys/vmem.h>
#if HAVE_THREAD_H
#include <thread.h>
#else
# include "sol_compat.h"
#endif
#if HAVE_SYNC_H
#include <synch.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct vmem_seg vmem_seg_t;

#define	VMEM_STACK_DEPTH	20

struct vmem_seg {
	/*
	 * The first four fields must match vmem_freelist_t exactly.
	 */
	uintptr_t	vs_start;	/* start of segment (inclusive) */
	uintptr_t	vs_end;		/* end of segment (exclusive) */
	vmem_seg_t	*vs_knext;	/* next of kin (alloc, free, span) */
	vmem_seg_t	*vs_kprev;	/* prev of kin */

	vmem_seg_t	*vs_anext;	/* next in arena */
	vmem_seg_t	*vs_aprev;	/* prev in arena */
	uint8_t		vs_type;	/* alloc, free, span */
	uint8_t		vs_import;	/* non-zero if segment was imported */
	uint8_t		vs_depth;	/* stack depth if UMF_AUDIT active */
	/*
	 * The following fields are present only when UMF_AUDIT is set.
	 */
	thread_t	vs_thread;
	hrtime_t	vs_timestamp;
	uintptr_t	vs_stack[VMEM_STACK_DEPTH];
};

typedef struct vmem_freelist {
	uintptr_t	vs_start;	/* always zero */
	uintptr_t	vs_end;		/* segment size */
	vmem_seg_t	*vs_knext;	/* next of kin */
	vmem_seg_t	*vs_kprev;	/* prev of kin */
} vmem_freelist_t;

#define	VS_SIZE(vsp)	((vsp)->vs_end - (vsp)->vs_start)

/*
 * Segment hashing
 */
#define	VMEM_HASH_INDEX(a, s, q, m)					\
	((((a) + ((a) >> (s)) + ((a) >> ((s) << 1))) >> (q)) & (m))

#define	VMEM_HASH(vmp, addr)						\
	(&(vmp)->vm_hash_table[VMEM_HASH_INDEX(addr,			\
	(vmp)->vm_hash_shift, (vmp)->vm_qshift, (vmp)->vm_hash_mask)])

#define	VMEM_NAMELEN		30
#define	VMEM_HASH_INITIAL	16
#define	VMEM_NQCACHE_MAX	16
#define	VMEM_FREELISTS		(sizeof (void *) * 8)

typedef struct vmem_kstat {
	uint64_t	vk_mem_inuse;	/* memory in use */
	uint64_t	vk_mem_import;	/* memory imported */
	uint64_t	vk_mem_total;	/* total memory in arena */
	uint32_t	vk_source_id;	/* vmem id of vmem source */
	uint64_t	vk_alloc;	/* number of allocations */
	uint64_t	vk_free;	/* number of frees */
	uint64_t	vk_wait;	/* number of allocations that waited */
	uint64_t	vk_fail;	/* number of allocations that failed */
	uint64_t	vk_lookup;	/* hash lookup count */
	uint64_t	vk_search;	/* freelist search count */
	uint64_t	vk_populate_wait;	/* populates that waited */
	uint64_t	vk_populate_fail;	/* populates that failed */
	uint64_t	vk_contains;		/* vmem_contains() calls */
	uint64_t	vk_contains_search;	/* vmem_contains() search cnt */
} vmem_kstat_t;

struct vmem {
	char		vm_name[VMEM_NAMELEN];	/* arena name */
	cond_t		vm_cv;		/* cv for blocking allocations */
	mutex_t		vm_lock;	/* arena lock */
	uint32_t	vm_id;		/* vmem id */
	uint32_t	vm_mtbf;	/* induced alloc failure rate */
	int		vm_cflags;	/* arena creation flags */
	int		vm_qshift;	/* log2(vm_quantum) */
	size_t		vm_quantum;	/* vmem quantum */
	size_t		vm_qcache_max;	/* maximum size to front by umem */
	vmem_alloc_t	*vm_source_alloc;
	vmem_free_t	*vm_source_free;
	vmem_t		*vm_source;	/* vmem source for imported memory */
	vmem_t		*vm_next;	/* next in vmem_list */
	ssize_t		vm_nsegfree;	/* number of free vmem_seg_t's */
	vmem_seg_t	*vm_segfree;	/* free vmem_seg_t list */
	vmem_seg_t	**vm_hash_table; /* allocated-segment hash table */
	size_t		vm_hash_mask;	/* hash_size - 1 */
	size_t		vm_hash_shift;	/* log2(vm_hash_mask + 1) */
	ulong_t		vm_freemap;	/* bitmap of non-empty freelists */
	vmem_seg_t	vm_seg0;	/* anchor segment */
	vmem_seg_t	vm_rotor;	/* rotor for VM_NEXTFIT allocations */
	vmem_seg_t	*vm_hash0[VMEM_HASH_INITIAL];	/* initial hash table */
	void		*vm_qcache[VMEM_NQCACHE_MAX];	/* quantum caches */
	vmem_freelist_t	vm_freelist[VMEM_FREELISTS + 1]; /* power-of-2 flists */
	vmem_kstat_t	vm_kstat;	/* kstat data */
};

/*
 * We cannot use a mutex_t and MUTEX_HELD, since that will not work
 * when libthread is not linked.
 */
typedef struct vmem_populate_lock {
	mutex_t		vmpl_mutex;
	thread_t	vmpl_thr;
} vmem_populate_lock_t;

#define	VM_UMFLAGS	VM_KMFLAGS

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_VMEM_IMPL_USER_H */
