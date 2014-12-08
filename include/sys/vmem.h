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

#ifndef _SPL_VMEM_H
#define	_SPL_VMEM_H

#include <sys/kmem.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>

typedef struct vmem { } vmem_t;

extern vmem_t *heap_arena;
extern vmem_t *zio_alloc_arena;
extern vmem_t *zio_arena;

extern size_t vmem_size(vmem_t *vmp, int typemask);

/*
 * Memory allocation interfaces
 */
#define VMEM_ALLOC      0x01
#define VMEM_FREE       0x02

#ifndef VMALLOC_TOTAL
#define VMALLOC_TOTAL   (VMALLOC_END - VMALLOC_START)
#endif

static inline void *
vmalloc_nofail(size_t size, gfp_t flags)
{
	void *ptr;

	/*
	 * Retry failed __vmalloc() allocations once every second.  The
	 * rational for the delay is that the likely failure modes are:
	 *
	 * 1) The system has completely exhausted memory, in which case
	 *    delaying 1 second for the memory reclaim to run is reasonable
	 *    to avoid thrashing the system.
	 * 2) The system has memory but has exhausted the small virtual
	 *    address space available on 32-bit systems.  Retrying the
	 *    allocation immediately will only result in spinning on the
	 *    virtual address space lock.  It is better delay a second and
	 *    hope that another process will free some of the address space.
	 *    But the bottom line is there is not much we can actually do
	 *    since we can never safely return a failure and honor the
	 *    Solaris semantics.
	 */
	while (1) {
		ptr = __vmalloc(size, flags | __GFP_HIGHMEM, PAGE_KERNEL);
		if (unlikely((ptr == NULL) && (flags & __GFP_WAIT))) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(HZ);
		} else {
			break;
		}
	}

	return ptr;
}

static inline void *
vzalloc_nofail(size_t size, gfp_t flags)
{
	void *ptr;

	ptr = vmalloc_nofail(size, flags);
	if (ptr)
		memset(ptr, 0, (size));

	return ptr;
}

#ifdef DEBUG_KMEM

/*
 * Memory accounting functions to be used only when DEBUG_KMEM is set.
 */
# ifdef HAVE_ATOMIC64_T

# define vmem_alloc_used_add(size)      atomic64_add(size, &vmem_alloc_used)
# define vmem_alloc_used_sub(size)      atomic64_sub(size, &vmem_alloc_used)
# define vmem_alloc_used_read()         atomic64_read(&vmem_alloc_used)
# define vmem_alloc_used_set(size)      atomic64_set(&vmem_alloc_used, size)

extern atomic64_t vmem_alloc_used;
extern unsigned long long vmem_alloc_max;

# else  /* HAVE_ATOMIC64_T */

# define vmem_alloc_used_add(size)      atomic_add(size, &vmem_alloc_used)
# define vmem_alloc_used_sub(size)      atomic_sub(size, &vmem_alloc_used)
# define vmem_alloc_used_read()         atomic_read(&vmem_alloc_used)
# define vmem_alloc_used_set(size)      atomic_set(&vmem_alloc_used, size)

extern atomic_t vmem_alloc_used;
extern unsigned long long vmem_alloc_max;

# endif /* HAVE_ATOMIC64_T */

# ifdef DEBUG_KMEM_TRACKING
/*
 * DEBUG_KMEM && DEBUG_KMEM_TRACKING
 *
 * The maximum level of memory debugging.  All memory will be accounted
 * for and each allocation will be explicitly tracked.  Any allocation
 * which is leaked will be reported on module unload and the exact location
 * where that memory was allocation will be reported.  This level of memory
 * tracking will have a significant impact on performance and should only
 * be enabled for debugging.  This feature may be enabled by passing
 * --enable-debug-kmem-tracking to configure.
 */
#  define vmem_alloc(sz, fl)            vmem_alloc_track((sz), (fl),           \
                                             __FUNCTION__, __LINE__)
#  define vmem_zalloc(sz, fl)           vmem_alloc_track((sz), (fl)|__GFP_ZERO,\
                                             __FUNCTION__, __LINE__)
#  define vmem_free(ptr, sz)            vmem_free_track((ptr), (sz))

extern void *kmem_alloc_track(size_t, int, const char *, int, int, int);
extern void kmem_free_track(const void *, size_t);
extern void *vmem_alloc_track(size_t, int, const char *, int);
extern void vmem_free_track(const void *, size_t);

# else /* DEBUG_KMEM_TRACKING */
/*
 * DEBUG_KMEM && !DEBUG_KMEM_TRACKING
 *
 * The default build will set DEBUG_KEM.  This provides basic memory
 * accounting with little to no impact on performance.  When the module
 * is unloaded in any memory was leaked the total number of leaked bytes
 * will be reported on the console.  To disable this basic accounting
 * pass the --disable-debug-kmem option to configure.
 */
#  define vmem_alloc(sz, fl)            vmem_alloc_debug((sz), (fl),           \
                                             __FUNCTION__, __LINE__)
#  define vmem_zalloc(sz, fl)           vmem_alloc_debug((sz), (fl)|__GFP_ZERO,\
                                             __FUNCTION__, __LINE__)
#  define vmem_free(ptr, sz)            vmem_free_debug((ptr), (sz))

extern void *vmem_alloc_debug(size_t, int, const char *, int);
extern void vmem_free_debug(const void *, size_t);

# endif /* DEBUG_KMEM_TRACKING */
#else /* DEBUG_KMEM */
/*
 * !DEBUG_KMEM && !DEBUG_KMEM_TRACKING
 *
 * All debugging is disabled.  There will be no overhead even for
 * minimal memory accounting.  To enable basic accounting pass the
 * --enable-debug-kmem option to configure.
 */
# define vmem_alloc(sz, fl)             vmalloc_nofail((sz), (fl))
# define vmem_zalloc(sz, fl)            vzalloc_nofail((sz), (fl))
# define vmem_free(ptr, sz)             ((void)(sz), vfree(ptr))

#endif /* DEBUG_KMEM */

int spl_vmem_init(void);
void spl_vmem_fini(void);

#endif	/* _SPL_VMEM_H */
