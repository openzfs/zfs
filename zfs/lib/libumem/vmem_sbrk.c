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

/*
 * The structure of the sbrk backend:
 *
 * +-----------+
 * | sbrk_top  |
 * +-----------+
 *      | (vmem_sbrk_alloc(), vmem_free())
 *      |
 * +-----------+
 * | sbrk_heap |
 * +-----------+
 *   | | ... |  (vmem_alloc(), vmem_free())
 * <other arenas>
 *
 * The sbrk_top arena holds all controlled memory.  vmem_sbrk_alloc() handles
 * allocations from it, including growing the heap when we run low.
 *
 * Growing the heap is complicated by the fact that we have to extend the
 * sbrk_top arena (using _vmem_extend_alloc()), and that can fail.  Since
 * other threads may be actively allocating, we can't return the memory.
 *
 * Instead, we put it on a doubly-linked list, sbrk_fails, which we search
 * before calling sbrk().
 */

#include <errno.h>
#include <limits.h>
#include <sys/sysmacros.h>
#include <sys/mman.h>
#include <unistd.h>

#include "vmem_base.h"

#include "misc.h"

size_t vmem_sbrk_pagesize = 0; /* the preferred page size of the heap */

#define	VMEM_SBRK_MINALLOC	(64 * 1024)
size_t vmem_sbrk_minalloc = VMEM_SBRK_MINALLOC; /* minimum allocation */

static size_t real_pagesize;
static vmem_t *sbrk_heap;

typedef struct sbrk_fail {
	struct sbrk_fail *sf_next;
	struct sbrk_fail *sf_prev;
	void *sf_base;			/* == the sbrk_fail's address */
	size_t sf_size;			/* the size of this buffer */
} sbrk_fail_t;

static sbrk_fail_t sbrk_fails = {
	&sbrk_fails,
	&sbrk_fails,
	NULL,
	0
};

static mutex_t sbrk_faillock = DEFAULTMUTEX;

/*
 * Try to extend src with [pos, pos + size).
 *
 * If it fails, add the block to the sbrk_fails list.
 */
static void *
vmem_sbrk_extend_alloc(vmem_t *src, void *pos, size_t size, size_t alloc,
    int vmflags)
{
	sbrk_fail_t *fnext, *fprev, *fp;
	void *ret;

	ret = _vmem_extend_alloc(src, pos, size, alloc, vmflags);
	if (ret != NULL)
		return (ret);

	fp = (sbrk_fail_t *)pos;

	ASSERT(sizeof (sbrk_fail_t) <= size);

	fp->sf_base = pos;
	fp->sf_size = size;

	(void) mutex_lock(&sbrk_faillock);
	fp->sf_next = fnext = &sbrk_fails;
	fp->sf_prev = fprev = sbrk_fails.sf_prev;
	fnext->sf_prev = fp;
	fprev->sf_next = fp;
	(void) mutex_unlock(&sbrk_faillock);

	return (NULL);
}

/*
 * Try to add at least size bytes to src, using the sbrk_fails list
 */
static void *
vmem_sbrk_tryfail(vmem_t *src, size_t size, int vmflags)
{
	sbrk_fail_t *fp;

	(void) mutex_lock(&sbrk_faillock);
	for (fp = sbrk_fails.sf_next; fp != &sbrk_fails; fp = fp->sf_next) {
		if (fp->sf_size >= size) {
			fp->sf_next->sf_prev = fp->sf_prev;
			fp->sf_prev->sf_next = fp->sf_next;
			fp->sf_next = fp->sf_prev = NULL;
			break;
		}
	}
	(void) mutex_unlock(&sbrk_faillock);

	if (fp != &sbrk_fails) {
		ASSERT(fp->sf_base == (void *)fp);
		return (vmem_sbrk_extend_alloc(src, fp, fp->sf_size, size,
		    vmflags));
	}
	/*
	 * nothing of the right size on the freelist
	 */
	return (NULL);
}

static void *
vmem_sbrk_alloc(vmem_t *src, size_t size, int vmflags)
{
	extern void *_sbrk_grow_aligned(size_t min_size, size_t low_align,
	    size_t high_align, size_t *actual_size);

	void *ret;
	void *buf;
	size_t buf_size;

	int old_errno = errno;

	ret = vmem_alloc(src, size, VM_NOSLEEP);
	if (ret != NULL) {
		errno = old_errno;
		return (ret);
	}

	/*
	 * The allocation failed.  We need to grow the heap.
	 *
	 * First, try to use any buffers which failed earlier.
	 */
	if (sbrk_fails.sf_next != &sbrk_fails &&
	    (ret = vmem_sbrk_tryfail(src, size, vmflags)) != NULL)
		return (ret);

	buf_size = MAX(size, vmem_sbrk_minalloc);

	/*
	 * buf_size gets overwritten with the actual allocated size
	 */
	buf = _sbrk_grow_aligned(buf_size, real_pagesize, vmem_sbrk_pagesize,
	    &buf_size);

	if (buf != MAP_FAILED) {
		ret = vmem_sbrk_extend_alloc(src, buf, buf_size, size, vmflags);
		if (ret != NULL) {
			errno = old_errno;
			return (ret);
		}
	}

	/*
	 * Growing the heap failed. The vmem_alloc() above called umem_reap().
	 */
	ASSERT((vmflags & VM_NOSLEEP) == VM_NOSLEEP);

	errno = old_errno;
	return (NULL);
}

/*
 * fork1() support
 */
void
vmem_sbrk_lockup(void)
{
	(void) mutex_lock(&sbrk_faillock);
}

void
vmem_sbrk_release(void)
{
	(void) mutex_unlock(&sbrk_faillock);
}

vmem_t *
vmem_sbrk_arena(vmem_alloc_t **a_out, vmem_free_t **f_out)
{
	if (sbrk_heap == NULL) {
		size_t heap_size;

		real_pagesize = sysconf(_SC_PAGESIZE);

		heap_size = vmem_sbrk_pagesize;

		if (issetugid()) {
			heap_size = 0;
		} else if (heap_size != 0 && !ISP2(heap_size)) {
			heap_size = 0;
			log_message("ignoring bad pagesize: 0x%p\n", heap_size);
		}
		if (heap_size <= real_pagesize) {
			heap_size = real_pagesize;
		} else {
			struct memcntl_mha mha;
			mha.mha_cmd = MHA_MAPSIZE_BSSBRK;
			mha.mha_flags = 0;
			mha.mha_pagesize = heap_size;

			if (memcntl(NULL, 0, MC_HAT_ADVISE, (char *)&mha, 0, 0)
			    == -1) {
				log_message("unable to set MAPSIZE_BSSBRK to "
				    "0x%p\n", heap_size);
				heap_size = real_pagesize;
			}
		}
		vmem_sbrk_pagesize = heap_size;

		/* validate vmem_sbrk_minalloc */
		if (vmem_sbrk_minalloc < VMEM_SBRK_MINALLOC)
			vmem_sbrk_minalloc = VMEM_SBRK_MINALLOC;
		vmem_sbrk_minalloc = P2ROUNDUP(vmem_sbrk_minalloc, heap_size);

		sbrk_heap = vmem_init("sbrk_top", real_pagesize,
		    vmem_sbrk_alloc, vmem_free,
		    "sbrk_heap", NULL, 0, real_pagesize,
		    vmem_alloc, vmem_free);
	}

	if (a_out != NULL)
		*a_out = vmem_alloc;
	if (f_out != NULL)
		*f_out = vmem_free;

	return (sbrk_heap);
}
