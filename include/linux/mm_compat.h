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

#if !defined(HAVE_SHRINK_CONTROL_STRUCT)
struct shrink_control {
	gfp_t gfp_mask;
	unsigned long nr_to_scan;
};
#endif /* HAVE_SHRINK_CONTROL_STRUCT */

/*
 * 2.6.xx API compat,
 * There currently exists no exposed API to partially shrink the dcache.
 * The expected mechanism to shrink the cache is a registered shrinker
 * which is called during memory pressure.
 */
#ifndef HAVE_SHRINK_DCACHE_MEMORY
# if defined(HAVE_SHRINK_CONTROL_STRUCT)
typedef int (*shrink_dcache_memory_t)(struct shrinker *,
    struct shrink_control *);
extern shrink_dcache_memory_t shrink_dcache_memory_fn;
#  define shrink_dcache_memory(nr, gfp)                                      \
({                                                                           \
	struct shrink_control sc = { .nr_to_scan = nr, .gfp_mask = gfp };    \
	int __ret__ = 0;                                                     \
                                                                             \
	if (shrink_dcache_memory_fn)                                         \
		__ret__ = shrink_dcache_memory_fn(NULL, &sc);                \
                                                                             \
	__ret__;                                                             \
})
# elif defined(HAVE_3ARGS_SHRINKER_CALLBACK)
typedef int (*shrink_dcache_memory_t)(struct shrinker *, int, gfp_t);
extern shrink_dcache_memory_t shrink_dcache_memory_fn;
#  define shrink_dcache_memory(nr, gfp)                                      \
({                                                                           \
	int __ret__ = 0;                                                     \
                                                                             \
	if (shrink_dcache_memory_fn)                                         \
		__ret__ = shrink_dcache_memory_fn(NULL, nr, gfp);            \
                                                                             \
	__ret__;                                                             \
})
# else
typedef int (*shrink_dcache_memory_t)(int, gfp_t);
extern shrink_dcache_memory_t shrink_dcache_memory_fn;
#  define shrink_dcache_memory(nr, gfp)                                      \
({                                                                           \
	int __ret__ = 0;                                                     \
                                                                             \
	if (shrink_dcache_memory_fn)                                         \
		__ret__ = shrink_dcache_memory_fn(nr, gfp);                  \
                                                                             \
	__ret__;                                                             \
})
# endif /* HAVE_3ARGS_SHRINKER_CALLBACK */
#endif /* HAVE_SHRINK_DCACHE_MEMORY */

/*
 * 2.6.xx API compat,
 * There currently exists no exposed API to partially shrink the icache.
 * The expected mechanism to shrink the cache is a registered shrinker
 * which is called during memory pressure.
 */
#ifndef HAVE_SHRINK_ICACHE_MEMORY
# if defined(HAVE_SHRINK_CONTROL_STRUCT)
typedef int (*shrink_icache_memory_t)(struct shrinker *,
    struct shrink_control *);
extern shrink_icache_memory_t shrink_icache_memory_fn;
#  define shrink_icache_memory(nr, gfp)                                      \
({                                                                           \
	struct shrink_control sc = { .nr_to_scan = nr, .gfp_mask = gfp };    \
	int __ret__ = 0;                                                     \
                                                                             \
	if (shrink_icache_memory_fn)                                         \
		__ret__ = shrink_icache_memory_fn(NULL, &sc);                \
                                                                             \
	__ret__;                                                             \
})
# elif defined(HAVE_3ARGS_SHRINKER_CALLBACK)
typedef int (*shrink_icache_memory_t)(struct shrinker *, int, gfp_t);
extern shrink_icache_memory_t shrink_icache_memory_fn;
#  define shrink_icache_memory(nr, gfp)                                      \
({                                                                           \
	int __ret__ = 0;                                                     \
                                                                             \
	if (shrink_icache_memory_fn)                                         \
		__ret__ = shrink_icache_memory_fn(NULL, nr, gfp);            \
                                                                             \
	__ret__;                                                             \
})
# else
typedef int (*shrink_icache_memory_t)(int, gfp_t);
extern shrink_icache_memory_t shrink_icache_memory_fn;
#  define shrink_icache_memory(nr, gfp)                                      \
({                                                                           \
	int __ret__ = 0;                                                     \
                                                                             \
	if (shrink_icache_memory_fn)                                         \
		__ret__ = shrink_icache_memory_fn(nr, gfp);                  \
                                                                             \
	__ret__;                                                             \
})
# endif /* HAVE_3ARGS_SHRINKER_CALLBACK */
#endif /* HAVE_SHRINK_ICACHE_MEMORY */

/*
 * Linux 2.6. - 2.6. Shrinker API Compatibility.
 */
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

# define SPL_SHRINKER_DECLARE(s, x, y)                                 \
	static spl_shrinker_t s = {                                    \
		.shrinker = NULL,                                      \
		.fn = x,                                               \
		.seeks = y                                             \
	}

# define SPL_SHRINKER_CALLBACK_FWD_DECLARE(fn)                         \
	static int fn(int, unsigned int)
# define SPL_SHRINKER_CALLBACK_WRAPPER(fn)                             \
static int                                                             \
fn(int nr_to_scan, unsigned int gfp_mask)                              \
{                                                                      \
	struct shrink_control sc;                                      \
                                                                       \
        sc.nr_to_scan = nr_to_scan;                                    \
        sc.gfp_mask = gfp_mask;                                        \
                                                                       \
	return __ ## fn(NULL, &sc);                                    \
}

#else

# define spl_register_shrinker(x)	register_shrinker(x)
# define spl_unregister_shrinker(x)	unregister_shrinker(x)
# define SPL_SHRINKER_DECLARE(s, x, y)                                 \
	static struct shrinker s = {                                   \
		.shrink = x,                                           \
		.seeks = y                                             \
	}

/*
 * Linux 2.6. - 2.6. Shrinker API Compatibility.
 */
# if defined(HAVE_SHRINK_CONTROL_STRUCT)
#  define SPL_SHRINKER_CALLBACK_FWD_DECLARE(fn)                        \
	static int fn(struct shrinker *, struct shrink_control *)
#  define SPL_SHRINKER_CALLBACK_WRAPPER(fn)                            \
static int                                                             \
fn(struct shrinker *shrink, struct shrink_control *sc) {               \
	return __ ## fn(shrink, sc);                                   \
}

/*
 * Linux 2.6. - 2.6. Shrinker API Compatibility.
 */
# elif defined(HAVE_3ARGS_SHRINKER_CALLBACK)
#  define SPL_SHRINKER_CALLBACK_FWD_DECLARE(fn)                       \
	static int fn(struct shrinker *, int, unsigned int)
#  define SPL_SHRINKER_CALLBACK_WRAPPER(fn)                           \
static int                                                            \
fn(struct shrinker *shrink, int nr_to_scan, unsigned int gfp_mask)    \
{                                                                     \
	struct shrink_control sc;                                     \
                                                                      \
        sc.nr_to_scan = nr_to_scan;                                   \
        sc.gfp_mask = gfp_mask;                                       \
                                                                      \
	return __ ## fn(shrink, &sc);                                 \
}

/*
 * Linux 2.6. - 2.6. Shrinker API Compatibility.
 */
# else
#  define SPL_SHRINKER_CALLBACK_FWD_DECLARE(fn)                       \
	static int fn(int, unsigned int)
#  define SPL_SHRINKER_CALLBACK_WRAPPER(fn)                           \
static int                                                            \
fn(int nr_to_scan, unsigned int gfp_mask)                             \
{                                                                     \
	struct shrink_control sc;                                     \
                                                                      \
        sc.nr_to_scan = nr_to_scan;                                   \
        sc.gfp_mask = gfp_mask;                                       \
                                                                      \
	return __ ## fn(NULL, &sc);                                   \
}

# endif
#endif /* HAVE_SET_SHRINKER */

#endif /* SPL_MM_COMPAT_H */
