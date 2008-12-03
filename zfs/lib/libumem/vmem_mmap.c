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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/sysmacros.h>
#include "vmem_base.h"

#define	ALLOC_PROT	PROT_READ | PROT_WRITE | PROT_EXEC
#define	FREE_PROT	PROT_NONE

#define	ALLOC_FLAGS	MAP_PRIVATE | MAP_ANON
#define	FREE_FLAGS	MAP_PRIVATE | MAP_ANON | MAP_NORESERVE

#define	CHUNKSIZE	(64*1024)	/* 64 kilobytes */

static vmem_t *mmap_heap;

static void *
vmem_mmap_alloc(vmem_t *src, size_t size, int vmflags)
{
	void *ret;
	int old_errno = errno;

	ret = vmem_alloc(src, size, vmflags);
	if (ret != NULL &&
	    mmap(ret, size, ALLOC_PROT, ALLOC_FLAGS | MAP_FIXED, -1, 0) ==
	    MAP_FAILED) {
		vmem_free(src, ret, size);
		vmem_reap();

		ASSERT((vmflags & VM_NOSLEEP) == VM_NOSLEEP);
		errno = old_errno;
		return (NULL);
	}

	errno = old_errno;
	return (ret);
}

static void
vmem_mmap_free(vmem_t *src, void *addr, size_t size)
{
	int old_errno = errno;
	(void) mmap(addr, size, FREE_PROT, FREE_FLAGS | MAP_FIXED, -1, 0);
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
	buf = mmap((void *)CHUNKSIZE, size, FREE_PROT, FREE_FLAGS | MAP_ALIGN,
	    -1, 0);

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
	size_t pagesize = sysconf(_SC_PAGESIZE);

	if (mmap_heap == NULL) {
		mmap_heap = vmem_init("mmap_top", CHUNKSIZE,
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
