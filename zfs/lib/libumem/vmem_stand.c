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

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Standalone-specific vmem routines
 *
 * The standalone allocator operates on a pre-existing blob of memory, the
 * location and dimensions of which are set using vmem_stand_setsize().  We
 * then hand out CHUNKSIZE-sized pieces of this blob, until we run out.
 */

#define	DEF_CHUNKSIZE	(64 * 1024)	/* 64K */

#define	DEF_NREGIONS	2

#include <errno.h>
#include <limits.h>
#include <sys/sysmacros.h>
#include <sys/mman.h>
#include <unistd.h>
#include <strings.h>

#include "vmem_base.h"
#include "misc.h"

static vmem_t *stand_heap;

static size_t stand_chunksize;

typedef struct stand_region {
	caddr_t sr_base;
	caddr_t sr_curtop;
	size_t sr_left;
} stand_region_t;

static stand_region_t stand_regions[DEF_NREGIONS];
static int stand_nregions;

extern void membar_producer(void);

void
vmem_stand_init(void)
{
	stand_chunksize = MAX(DEF_CHUNKSIZE, pagesize);

	stand_nregions = 0;
}

int
vmem_stand_add(caddr_t base, size_t len)
{
	stand_region_t *sr = &stand_regions[stand_nregions];

	ASSERT(pagesize != 0);

	if (stand_nregions == DEF_NREGIONS) {
		errno = ENOSPC;
		return (-1); /* we don't have room -- throw it back */
	}

	/*
	 * We guarantee that only one call to `vmem_stand_add' will be
	 * active at a time, but we can't ensure that the allocator won't be
	 * in use while this function is being called.  As such, we have to
	 * ensure that sr is populated and visible to other processors before
	 * allowing the allocator to access the new region.
	 */
	sr->sr_base = base;
	sr->sr_curtop = (caddr_t)P2ROUNDUP((ulong_t)base, stand_chunksize);
	sr->sr_left = P2ALIGN(len - (size_t)(sr->sr_curtop - sr->sr_base),
	    stand_chunksize);
	membar_producer();

	stand_nregions++;

	return (0);
}

static void *
stand_parent_alloc(vmem_t *src, size_t size, int vmflags)
{
	int old_errno = errno;
	stand_region_t *sr;
	size_t chksize;
	void *ret;
	int i;

	if ((ret = vmem_alloc(src, size, VM_NOSLEEP)) != NULL) {
		errno = old_errno;
		return (ret);
	}

	/* We need to allocate another chunk */
	chksize = roundup(size, stand_chunksize);

	for (sr = stand_regions, i = 0; i < stand_nregions; i++, sr++) {
		if (sr->sr_left >= chksize)
			break;
	}

	if (i == stand_nregions) {
		/*
		 * We don't have enough in any of our regions to satisfy the
		 * request.
		 */
		errno = old_errno;
		return (NULL);
	}

	if ((ret = _vmem_extend_alloc(src, sr->sr_curtop, chksize, size,
	    vmflags)) == NULL) {
		errno = old_errno;
		return (NULL);
	}

	bzero(sr->sr_curtop, chksize);

	sr->sr_curtop += chksize;
	sr->sr_left -= chksize;

	return (ret);
}

vmem_t *
vmem_stand_arena(vmem_alloc_t **a_out, vmem_free_t **f_out)
{
	ASSERT(stand_nregions == 1);

	stand_heap = vmem_init("stand_parent", stand_chunksize,
	    stand_parent_alloc, vmem_free,
	    "stand_heap", NULL, 0, pagesize, vmem_alloc, vmem_free);

	if (a_out != NULL)
		*a_out = vmem_alloc;
	if (f_out != NULL)
		*f_out = vmem_free;

	return (stand_heap);
}
