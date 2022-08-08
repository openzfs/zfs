/*
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
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
 */

#ifndef _SPL_SHRINKER_H
#define	_SPL_SHRINKER_H

#include <linux/mm.h>
#include <linux/fs.h>

/*
 * Due to frequent changes in the shrinker API the following
 * compatibility wrappers should be used.  They are as follows:
 *
 *   SPL_SHRINKER_DECLARE(varname, countfunc, scanfunc, seek_cost);
 *
 * SPL_SHRINKER_DECLARE is used to declare a shrinker with the name varname,
 * which is passed to spl_register_shrinker()/spl_unregister_shrinker().
 * The countfunc returns the number of free-able objects.
 * The scanfunc returns the number of objects that were freed.
 * The callbacks can return SHRINK_STOP if further calls can't make any more
 * progress.  Note that a return value of SHRINK_EMPTY is currently not
 * supported.
 *
 * Example:
 *
 * static unsigned long
 * my_count(struct shrinker *shrink, struct shrink_control *sc)
 * {
 *	...calculate number of objects in the cache...
 *
 *	return (number of objects in the cache);
 * }
 *
 * static unsigned long
 * my_scan(struct shrinker *shrink, struct shrink_control *sc)
 * {
 *	...scan objects in the cache and reclaim them...
 * }
 *
 * SPL_SHRINKER_DECLARE(my_shrinker, my_count, my_scan, DEFAULT_SEEKS);
 *
 * void my_init_func(void) {
 *	spl_register_shrinker(&my_shrinker);
 * }
 */

#ifdef HAVE_REGISTER_SHRINKER_VARARG
#define	spl_register_shrinker(x)	register_shrinker(x, "zfs-arc-shrinker")
#else
#define	spl_register_shrinker(x)	register_shrinker(x)
#endif
#define	spl_unregister_shrinker(x)	unregister_shrinker(x)

/*
 * Linux 3.0 to 3.11 Shrinker API Compatibility.
 */
#if defined(HAVE_SINGLE_SHRINKER_CALLBACK)
#define	SPL_SHRINKER_DECLARE(varname, countfunc, scanfunc, seek_cost)	\
static int								\
__ ## varname ## _wrapper(struct shrinker *shrink, struct shrink_control *sc)\
{									\
	if (sc->nr_to_scan != 0) {					\
		(void) scanfunc(shrink, sc);				\
	}								\
	return (countfunc(shrink, sc));					\
}									\
									\
static struct shrinker varname = {					\
	.shrink = __ ## varname ## _wrapper,				\
	.seeks = seek_cost,						\
}

#define	SHRINK_STOP	(-1)

/*
 * Linux 3.12 and later Shrinker API Compatibility.
 */
#elif defined(HAVE_SPLIT_SHRINKER_CALLBACK)
#define	SPL_SHRINKER_DECLARE(varname, countfunc, scanfunc, seek_cost)	\
static struct shrinker varname = {					\
	.count_objects = countfunc,					\
	.scan_objects = scanfunc,					\
	.seeks = seek_cost,						\
}

#else
/*
 * Linux 2.x to 2.6.22, or a newer shrinker API has been introduced.
 */
#error "Unknown shrinker callback"
#endif

#endif /* SPL_SHRINKER_H */
