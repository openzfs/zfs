/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
\*****************************************************************************/

#ifndef _SPL_VMSYSTM_H
#define _SPL_VMSYSTM_H

#include <linux/mmzone.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/highmem.h>
#include <sys/types.h>
#include <asm/uaccess.h>

/* These values are loosely coupled with the VM page reclaim.
 * Linux uses its own heuristics to trigger page reclamation, and
 * because those interface are difficult to interface with.  These
 * values should only be considered as a rough guide to the system
 * memory state and not as direct evidence that page reclamation.
 * is or is not currently in progress.
 */
#define membar_producer()		smp_wmb()

#define physmem				totalram_pages
#define freemem				nr_free_pages()
#define availrmem			spl_kmem_availrmem()

extern pgcnt_t minfree;			/* Sum of zone->pages_min */
extern pgcnt_t desfree;			/* Sum of zone->pages_low */
extern pgcnt_t lotsfree;		/* Sum of zone->pages_high */
extern pgcnt_t needfree;		/* Always 0 unused in new Solaris */
extern pgcnt_t swapfs_minfree;		/* Solaris default value */
extern pgcnt_t swapfs_reserve;		/* Solaris default value */

extern vmem_t *heap_arena;		/* primary kernel heap arena */
extern vmem_t *zio_alloc_arena;		/* arena for zio caches */
extern vmem_t *zio_arena;		/* arena for allocating zio memory */

extern pgcnt_t spl_kmem_availrmem(void);
extern size_t vmem_size(vmem_t *vmp, int typemask);

/*
 * The following symbols are available for use within the kernel
 * itself, and they used to be available in older kernels.  But it
 * looks like they have been removed perhaps due to lack of use.
 * For our purposes we need them to access the global memory state
 * of the system, which is even available to user space process
 * in /proc/meminfo.  It's odd to me that there is no kernel API
 * to get the same information, minimally the proc handler for
 * the above mentioned /proc/meminfo file would make use of it.
 */

/* Source linux/fs/proc/mmu.c */
#ifndef HAVE_GET_VMALLOC_INFO
#ifdef CONFIG_MMU

#ifndef HAVE_VMALLOC_INFO
struct vmalloc_info {
	unsigned long used;
	unsigned long largest_chunk;
};
#endif

typedef void (*get_vmalloc_info_t)(struct vmalloc_info *);
extern get_vmalloc_info_t get_vmalloc_info_fn;

# define VMEM_ALLOC		0x01
# define VMEM_FREE		0x02
# define VMALLOC_TOTAL		(VMALLOC_END - VMALLOC_START)
# define get_vmalloc_info(vmi)	get_vmalloc_info_fn(vmi)
#else
# error "CONFIG_MMU must be defined"
#endif /* CONFIG_MMU */
#endif /* HAVE_GET_VMALLOC_INFO */

#ifdef HAVE_PGDAT_HELPERS
/* Source linux/mm/mmzone.c */
# ifndef HAVE_FIRST_ONLINE_PGDAT
typedef struct pglist_data *(*first_online_pgdat_t)(void);
extern first_online_pgdat_t first_online_pgdat_fn;
# define first_online_pgdat()	first_online_pgdat_fn()
# endif /* HAVE_FIRST_ONLINE_PGDAT */

# ifndef HAVE_NEXT_ONLINE_PGDAT
typedef struct pglist_data *(*next_online_pgdat_t)(struct pglist_data *);
extern next_online_pgdat_t next_online_pgdat_fn;
# define next_online_pgdat(pgd)	next_online_pgdat_fn(pgd)
# endif /* HAVE_NEXT_ONLINE_PGDAT */

# ifndef HAVE_NEXT_ZONE
typedef struct zone *(*next_zone_t)(struct zone *);
extern next_zone_t next_zone_fn;
# define next_zone(zone)	next_zone_fn(zone)
# endif /* HAVE_NEXT_ZONE */

#else /* HAVE_PGDAT_HELPERS */

# ifndef HAVE_PGDAT_LIST
extern struct pglist_data *pgdat_list_addr;
# define pgdat_list		pgdat_list_addr
# endif /* HAVE_PGDAT_LIST */

#endif /* HAVE_PGDAT_HELPERS */

/* Source linux/mm/vmstat.c */
#if defined(NEED_GET_ZONE_COUNTS) && !defined(HAVE_GET_ZONE_COUNTS)
typedef void (*get_zone_counts_t)(unsigned long *, unsigned long *,
				  unsigned long *);
extern get_zone_counts_t get_zone_counts_fn;
# define get_zone_counts(a,i,f)	get_zone_counts_fn(a,i,f)
#endif /* NEED_GET_ZONE_COUNTS && !HAVE_GET_ZONE_COUNTS */

typedef enum spl_zone_stat_item {
	SPL_NR_FREE_PAGES,
	SPL_NR_INACTIVE,
	SPL_NR_ACTIVE,
	SPL_NR_ZONE_STAT_ITEMS
} spl_zone_stat_item_t;

extern unsigned long spl_global_page_state(spl_zone_stat_item_t);

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

#endif /* SPL_VMSYSTM_H */
