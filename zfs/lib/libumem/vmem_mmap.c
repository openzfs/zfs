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
 * Copyright 2002 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Portions Copyright 2006 OmniTI, Inc.
 */

/* #pragma ident	"@(#)vmem_mmap.c	1.2	05/06/08 SMI" */

#include "config.h"
#include <errno.h>

#if HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif

#ifdef HAVE_SYS_SYSMACROS_H
#include <sys/sysmacros.h>
#endif

#include <unistd.h>

#include "vmem_base.h"

#define	ALLOC_PROT	PROT_READ | PROT_WRITE | PROT_EXEC
#define	FREE_PROT	PROT_NONE

#define	ALLOC_FLAGS	MAP_PRIVATE | MAP_ANON
#define	FREE_FLAGS	MAP_PRIVATE | MAP_ANON | MAP_NORESERVE

#ifdef MAP_ALIGN
#define	CHUNKSIZE	(64*1024)	/* 64 kilobytes */
#else
static size_t CHUNKSIZE;
#endif

static vmem_t *mmap_heap;

static void *
vmem_mmap_alloc(vmem_t *src, size_t size, int vmflags)
{
	void *ret;
	int old_errno = errno;

	ret = vmem_alloc(src, size, vmflags);
#ifndef _WIN32
	if (ret != NULL
		&&
	    mmap(ret, size, ALLOC_PROT, ALLOC_FLAGS | MAP_FIXED, -1, 0) ==
	    MAP_FAILED
		) {
		vmem_free(src, ret, size);
		vmem_reap();

		ASSERT((vmflags & VM_NOSLEEP) == VM_NOSLEEP);
		errno = old_errno;
		return (NULL);
	}
#endif

	errno = old_errno;
	return (ret);
}

static void
vmem_mmap_free(vmem_t *src, void *addr, size_t size)
{
	int old_errno = errno;
#ifdef _WIN32
	VirtualFree(addr, size, MEM_RELEASE);
#else
	(void) mmap(addr, size, FREE_PROT, FREE_FLAGS | MAP_FIXED, -1, 0);
#endif
	vmem_free(src, addr, size);
	errno = old_errno;
}

static void *
vmem_mmap_top_alloc(vmem_t *src, size_t size, int vmflags)
{
	void *ret;
	void *buf;
	int old_errno = errno;

	ret = vmem_alloc(src, size, VM_NOSLEEP);

	if (ret) {
		errno = old_errno;
		return (ret);
	}
	/*
	 * Need to grow the heap
	 */
#ifdef _WIN32
	buf = VirtualAlloc(NULL, size, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
	if (buf == NULL) buf = MAP_FAILED;
#else
	buf = mmap(
#ifdef MAP_ALIGN
			(void *)CHUNKSIZE,
#else
			0,
#endif
			size, FREE_PROT, FREE_FLAGS
#ifdef MAP_ALIGN
			| MAP_ALIGN
#endif
			, -1, 0);
#endif

	if (buf != MAP_FAILED) {
		ret = _vmem_extend_alloc(src, buf, size, size, vmflags);
		if (ret != NULL)
			return (ret);
		else {
			(void) munmap(buf, size);
			errno = old_errno;
			return (NULL);
		}
	} else {
		/*
		 * Growing the heap failed.  The allocation above will
		 * already have called umem_reap().
		 */
		ASSERT((vmflags & VM_NOSLEEP) == VM_NOSLEEP);

		errno = old_errno;
		return (NULL);
	}
}

vmem_t *
vmem_mmap_arena(vmem_alloc_t **a_out, vmem_free_t **f_out)
{
#ifdef _WIN32
	SYSTEM_INFO info;
	size_t pagesize;
#else
	size_t pagesize = _sysconf(_SC_PAGESIZE);
#endif
	
#ifdef _WIN32
	GetSystemInfo(&info);
	pagesize = info.dwPageSize;
	CHUNKSIZE = info.dwAllocationGranularity;
#elif !defined(MAP_ALIGN)
	CHUNKSIZE = pagesize;
#endif
	
	if (mmap_heap == NULL) {
		mmap_heap = vmem_init("mmap_top", 
			CHUNKSIZE,
		    vmem_mmap_top_alloc, vmem_free,
		    "mmap_heap", NULL, 0, pagesize,
		    vmem_mmap_alloc, vmem_mmap_free);
	}

	if (a_out != NULL)
		*a_out = vmem_mmap_alloc;
	if (f_out != NULL)
		*f_out = vmem_mmap_free;

	return (mmap_heap);
}
