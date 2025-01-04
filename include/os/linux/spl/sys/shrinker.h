// SPDX-License-Identifier: GPL-2.0-or-later
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
 * compatibility wrapper should be used.
 *
 *   shrinker = spl_register_shrinker(name, countfunc, scanfunc, seek_cost);
 *   spl_unregister_shrinker(shrinker);
 *
 * spl_register_shrinker is used to create and register a shrinker with the
 * given name.
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
 * static struct shrinker *my_shrinker;
 *
 * void my_init_func(void) {
 *	my_shrinker = spl_register_shrinker("my-shrinker",
 *	    my_count, my_scan, DEFAULT_SEEKS);
 * }
 *
 * void my_fini_func(void) {
 *	spl_unregister_shrinker(my_shrinker);
 * }
 */

typedef unsigned long (*spl_shrinker_cb)
	(struct shrinker *, struct shrink_control *);

struct shrinker *spl_register_shrinker(const char *name,
    spl_shrinker_cb countfunc, spl_shrinker_cb scanfunc, int seek_cost);
void spl_unregister_shrinker(struct shrinker *);

#ifndef SHRINK_STOP
/* 3.0-3.11 compatibility */
#define	SHRINK_STOP	(-1)
#endif

#endif /* SPL_SHRINKER_H */
