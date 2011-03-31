/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://github.com/behlendorf/spl/>.
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

#ifndef _SPL_MM_COMPAT_H
#define _SPL_MM_COMPAT_H

#include <linux/mm.h>
#include <linux/fs.h>

/*
 * Linux 2.6.31 API Change.
 * Individual pages_{min,low,high} moved in to watermark array.
 */
#ifndef min_wmark_pages
#define min_wmark_pages(z)	(z->pages_min)
#endif

#ifndef low_wmark_pages
#define low_wmark_pages(z)	(z->pages_low)
#endif

#ifndef high_wmark_pages
#define high_wmark_pages(z)	(z->pages_high)
#endif

/*
 * 2.6.37 API compat,
 * The function invalidate_inodes() is no longer exported by the kernel.
 * The prototype however is still available which means it is safe
 * to acquire the symbol's address using spl_kallsyms_lookup_name().
 */
#ifndef HAVE_INVALIDATE_INODES
typedef int (*invalidate_inodes_t)(struct super_block *sb);
extern invalidate_inodes_t invalidate_inodes_fn;
#define invalidate_inodes(sb)	invalidate_inodes_fn(sb)
#endif /* HAVE_INVALIDATE_INODES */

/*
 * 2.6.xx API compat,
 * There currently exists no exposed API to partially shrink the dcache.
 * The expected mechanism to shrink the cache is a registered shrinker
 * which is called during memory pressure.
 */
#ifndef HAVE_SHRINK_DCACHE_MEMORY
# ifdef HAVE_3ARGS_SHRINKER_CALLBACK
typedef int (*shrink_dcache_memory_t)(struct shrinker *, int, gfp_t);
extern shrink_dcache_memory_t shrink_dcache_memory_fn;
#  define shrink_dcache_memory(nr, gfp)	shrink_dcache_memory_fn(NULL, nr, gfp)
# else
typedef int (*shrink_dcache_memory_t)(int, gfp_t);
extern shrink_dcache_memory_t shrink_dcache_memory_fn;
#  define shrink_dcache_memory(nr, gfp)	shrink_dcache_memory_fn(nr, gfp)
# endif /* HAVE_3ARGS_SHRINKER_CALLBACK */
#endif /* HAVE_SHRINK_DCACHE_MEMORY */

/*
 * 2.6.xx API compat,
 * There currently exists no exposed API to partially shrink the icache.
 * The expected mechanism to shrink the cache is a registered shrinker
 * which is called during memory pressure.
 */
#ifndef HAVE_SHRINK_ICACHE_MEMORY
# ifdef HAVE_3ARGS_SHRINKER_CALLBACK
typedef int (*shrink_icache_memory_t)(struct shrinker *, int, gfp_t);
extern shrink_icache_memory_t shrink_icache_memory_fn;
#  define shrink_icache_memory(nr, gfp)	shrink_icache_memory_fn(NULL, nr, gfp)
# else
typedef int (*shrink_icache_memory_t)(int, gfp_t);
extern shrink_icache_memory_t shrink_icache_memory_fn;
#  define shrink_icache_memory(nr, gfp)	shrink_icache_memory_fn(nr, gfp)
# endif /* HAVE_3ARGS_SHRINKER_CALLBACK */
#endif /* HAVE_SHRINK_ICACHE_MEMORY */

#ifdef HAVE_SET_SHRINKER
typedef struct spl_shrinker {
	struct shrinker *shrinker;
	shrinker_t fn;
	int seeks;
} spl_shrinker_t;

static inline void
spl_register_shrinker(spl_shrinker_t *ss)
{
	ss->shrinker = set_shrinker(ss->seeks, ss->fn);
}

static inline void
spl_unregister_shrinker(spl_shrinker_t *ss)
{
	remove_shrinker(ss->shrinker);
}

# define SPL_SHRINKER_DECLARE(s, x, y) \
	static spl_shrinker_t s = { .shrinker = NULL, .fn = x, .seeks = y }
# define SPL_SHRINKER_CALLBACK_PROTO(fn, x, y, z) \
	static int fn(int y, unsigned int z)
# define spl_exec_shrinker(ss, nr, gfp) \
	((spl_shrinker_t *)ss)->fn(nr, gfp)

#else /* HAVE_SET_SHRINKER */

# define spl_register_shrinker(x)	register_shrinker(x)
# define spl_unregister_shrinker(x)	unregister_shrinker(x)
# define SPL_SHRINKER_DECLARE(s, x, y) \
	static struct shrinker s = { .shrink = x, .seeks = y }

# ifdef HAVE_3ARGS_SHRINKER_CALLBACK
#  define SPL_SHRINKER_CALLBACK_PROTO(fn, x, y, z) \
	static int fn(struct shrinker *x, int y, unsigned int z)
#  define spl_exec_shrinker(ss, nr, gfp) \
	((struct shrinker *)ss)->shrink(NULL, nr, gfp)
# else /* HAVE_3ARGS_SHRINKER_CALLBACK */
#  define SPL_SHRINKER_CALLBACK_PROTO(fn, x, y, z) \
	static int fn(int y, unsigned int z)
#  define spl_exec_shrinker(ss, nr, gfp) \
	((struct shrinker *)ss)->shrink(nr, gfp)
# endif /* HAVE_3ARGS_SHRINKER_CALLBACK */
#endif /* HAVE_SET_SHRINKER */

#endif /* SPL_MM_COMPAT_H */
