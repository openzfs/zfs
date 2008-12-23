/*
 *  This file is part of the SPL: Solaris Porting Layer.
 *
 *  Copyright (c) 2008 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory
 *  Written by:
 *          Brian Behlendorf <behlendorf1@llnl.gov>,
 *          Herb Wartens <wartens2@llnl.gov>,
 *          Jim Garlick <garlick@llnl.gov>
 *  UCRL-CODE-235197
 *
 *  This is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#ifndef _SPL_VMSYSTM_H
#define _SPL_VMSYSTM_H

#include <linux/mm.h>
#include <linux/swap.h>
#include <sys/types.h>
#include <asm/uaccess.h>

extern vmem_t *zio_alloc_arena;		/* arena for zio caches */

#define physmem				num_physpages
#define freemem				nr_free_pages() // Expensive on linux,
							// cheap on solaris
#define minfree				0
#define needfree			0	/* # of needed pages */
#define ptob(pages)			(pages * PAGE_SIZE)
#define membar_producer()		smp_wmb()

#define xcopyin(from, to, size)		copy_from_user(to, from, size)
#define xcopyout(from, to, size)	copy_to_user(to, from, size)

static __inline__ int
copyin(const void *from, void *to, size_t len)
{
	/* On error copyin routine returns -1 */
	if (xcopyin(from, to, len))
		return -1;

	return 0;
}

static __inline__ int
copyout(const void *from, void *to, size_t len)
{
	/* On error copyout routine returns -1 */
	if (xcopyout(from, to, len))
		return -1;

	return 0;
}

static __inline__ int
copyinstr(const void *from, void *to, size_t len, size_t *done)
{
	size_t rc;

	if (len == 0)
		return -ENAMETOOLONG;

	/* XXX: Should return ENAMETOOLONG if 'strlen(from) > len' */

	memset(to, 0, len);
	rc = copyin(from, to, len - 1);
	if (done != NULL)
		*done = rc;

	return 0;
}

#if 0
/* The average number of free pages over the last 5 seconds */
#define avefree				0

/* The average number of free pages over the last 30 seconds */
#define avefree30			0

/* A guess as to how much memory has been promised to
 * processes but not yet allocated */
#define deficit				0

/* A bootlean the controls the setting of deficit */
#define desperate

/* When free memory is above this limit, no paging or swapping is done */
#define lotsfree			0

/* When free memory is above this limit, swapping is not performed */
#define desfree				0
#endif

#endif /* SPL_VMSYSTM_H */
