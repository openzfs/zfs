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

#ifndef _SPL_KMEM_H
#define	_SPL_KMEM_H

#include <linux/slab.h>

extern int kmem_debugging(void);
extern char *kmem_vasprintf(const char *fmt, va_list ap);
extern char *kmem_asprintf(const char *fmt, ...);
extern char *strdup(const char *str);
extern void strfree(char *str);

/*
 * Memory allocation interfaces
 */
#define KM_SLEEP	GFP_KERNEL	/* Can sleep, never fails */
#define KM_NOSLEEP	GFP_ATOMIC	/* Can not sleep, may fail */
#define KM_PUSHPAGE	(GFP_NOIO | __GFP_HIGH)	/* Use reserved memory */
#define KM_NODEBUG	__GFP_NOWARN	/* Suppress warnings */
#define KM_FLAGS	__GFP_BITS_MASK
#define KM_VMFLAGS	GFP_LEVEL_MASK

/*
 * Used internally, the kernel does not need to support this flag
 */
#ifndef __GFP_ZERO
# define __GFP_ZERO                     0x8000
#endif

/*
 * __GFP_NOFAIL looks like it will be removed from the kernel perhaps as
 * early as 2.6.32.  To avoid this issue when it occurs in upstream kernels
 * we retry the allocation here as long as it is not __GFP_WAIT (GFP_ATOMIC).
 * I would prefer the caller handle the failure case cleanly but we are
 * trying to emulate Solaris and those are not the Solaris semantics.
 */
static inline void *
kmalloc_nofail(size_t size, gfp_t flags)
{
	void *ptr;

	do {
		ptr = kmalloc(size, flags);
	} while (ptr == NULL && (flags & __GFP_WAIT));

	return ptr;
}

static inline void *
kzalloc_nofail(size_t size, gfp_t flags)
{
	void *ptr;

	do {
		ptr = kzalloc(size, flags);
	} while (ptr == NULL && (flags & __GFP_WAIT));

	return ptr;
}

static inline void *
kmalloc_node_nofail(size_t size, gfp_t flags, int node)
{
	void *ptr;

	do {
		ptr = kmalloc_node(size, flags, node);
	} while (ptr == NULL && (flags & __GFP_WAIT));

	return ptr;
}

#ifdef DEBUG_KMEM

/*
 * Memory accounting functions to be used only when DEBUG_KMEM is set.
 */
# ifdef HAVE_ATOMIC64_T

# define kmem_alloc_used_add(size)      atomic64_add(size, &kmem_alloc_used)
# define kmem_alloc_used_sub(size)      atomic64_sub(size, &kmem_alloc_used)
# define kmem_alloc_used_read()         atomic64_read(&kmem_alloc_used)
# define kmem_alloc_used_set(size)      atomic64_set(&kmem_alloc_used, size)

extern atomic64_t kmem_alloc_used;
extern unsigned long long kmem_alloc_max;

# else  /* HAVE_ATOMIC64_T */

# define kmem_alloc_used_add(size)      atomic_add(size, &kmem_alloc_used)
# define kmem_alloc_used_sub(size)      atomic_sub(size, &kmem_alloc_used)
# define kmem_alloc_used_read()         atomic_read(&kmem_alloc_used)
# define kmem_alloc_used_set(size)      atomic_set(&kmem_alloc_used, size)

extern atomic_t kmem_alloc_used;
extern unsigned long long kmem_alloc_max;

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
#  define kmem_alloc(sz, fl)            kmem_alloc_track((sz), (fl),           \
                                             __FUNCTION__, __LINE__, 0, 0)
#  define kmem_zalloc(sz, fl)           kmem_alloc_track((sz), (fl)|__GFP_ZERO,\
                                             __FUNCTION__, __LINE__, 0, 0)
#  define kmem_alloc_node(sz, fl, nd)   kmem_alloc_track((sz), (fl),           \
                                             __FUNCTION__, __LINE__, 1, nd)
#  define kmem_free(ptr, sz)            kmem_free_track((ptr), (sz))

extern void *kmem_alloc_track(size_t, int, const char *, int, int, int);
extern void kmem_free_track(const void *, size_t);

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
#  define kmem_alloc(sz, fl)            kmem_alloc_debug((sz), (fl),           \
                                             __FUNCTION__, __LINE__, 0, 0)
#  define kmem_zalloc(sz, fl)           kmem_alloc_debug((sz), (fl)|__GFP_ZERO,\
                                             __FUNCTION__, __LINE__, 0, 0)
#  define kmem_alloc_node(sz, fl, nd)   kmem_alloc_debug((sz), (fl),           \
                                             __FUNCTION__, __LINE__, 1, nd)
#  define kmem_free(ptr, sz)            kmem_free_debug((ptr), (sz))

extern void *kmem_alloc_debug(size_t, int, const char *, int, int, int);
extern void kmem_free_debug(const void *, size_t);

# endif /* DEBUG_KMEM_TRACKING */
#else /* DEBUG_KMEM */
/*
 * !DEBUG_KMEM && !DEBUG_KMEM_TRACKING
 *
 * All debugging is disabled.  There will be no overhead even for
 * minimal memory accounting.  To enable basic accounting pass the
 * --enable-debug-kmem option to configure.
 */
# define kmem_alloc(sz, fl)             kmalloc_nofail((sz), (fl))
# define kmem_zalloc(sz, fl)            kzalloc_nofail((sz), (fl))
# define kmem_alloc_node(sz, fl, nd)    kmalloc_node_nofail((sz), (fl), (nd))
# define kmem_free(ptr, sz)             ((void)(sz), kfree(ptr))

#endif /* DEBUG_KMEM */

int spl_kmem_init(void);
void spl_kmem_fini(void);

#define kmem_virt(ptr)			(((ptr) >= (void *)VMALLOC_START) && \
					 ((ptr) <  (void *)VMALLOC_END))

#endif	/* _SPL_KMEM_H */
