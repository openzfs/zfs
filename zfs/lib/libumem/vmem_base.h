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

#ifndef	_VMEM_BASE_H
#define	_VMEM_BASE_H

/* #pragma ident	"@(#)vmem_base.h	1.3	05/06/08 SMI" */

#include <sys/vmem.h>
#include <umem.h>

#ifdef	__cplusplus
extern "C" {
#endif

#include "misc.h"

extern void vmem_startup(void);
extern vmem_t *vmem_init(const char *parent_name, size_t parent_quantum,
	vmem_alloc_t *parent_alloc, vmem_free_t *parent_free,
	const char *heap_name,
	void *heap_start, size_t heap_size, size_t heap_quantum,
	vmem_alloc_t *heap_alloc, vmem_free_t *heap_free);

extern void *_vmem_extend_alloc(vmem_t *vmp, void *vaddr, size_t size,
	size_t alloc, int vmflag);

extern vmem_t *vmem_heap_arena(vmem_alloc_t **, vmem_free_t **);
extern void vmem_heap_init(void);

extern vmem_t *vmem_sbrk_arena(vmem_alloc_t **, vmem_free_t **);
extern vmem_t *vmem_mmap_arena(vmem_alloc_t **, vmem_free_t **);
extern vmem_t *vmem_stand_arena(vmem_alloc_t **, vmem_free_t **);

extern void vmem_update(void *);
extern void vmem_reap(void);		/* vmem_populate()-safe reap */

extern size_t pagesize;
extern size_t vmem_sbrk_pagesize;

extern uint_t vmem_backend;
#define	VMEM_BACKEND_SBRK	0x0000001
#define	VMEM_BACKEND_MMAP	0x0000002
#define	VMEM_BACKEND_STAND	0x0000003

extern vmem_t *vmem_heap;
extern vmem_alloc_t *vmem_heap_alloc;
extern vmem_free_t *vmem_heap_free;

extern void vmem_lockup(void);
extern void vmem_release(void);

extern void vmem_sbrk_lockup(void);
extern void vmem_sbrk_release(void);

extern void vmem_no_debug(void);

#ifdef	__cplusplus
}
#endif

#endif	/* _VMEM_BASE_H */
