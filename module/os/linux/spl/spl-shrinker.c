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
 *
 *  Solaris Porting Layer (SPL) Shrinker Implementation.
 */

#include <sys/kmem.h>
#include <sys/shrinker.h>

#ifdef HAVE_SINGLE_SHRINKER_CALLBACK
/* 3.0-3.11: single shrink() callback, which we wrap to carry both functions */
struct spl_shrinker_wrap {
	struct shrinker shrinker;
	spl_shrinker_cb countfunc;
	spl_shrinker_cb scanfunc;
};

static int
spl_shrinker_single_cb(struct shrinker *shrinker, struct shrink_control *sc)
{
	struct spl_shrinker_wrap *sw = (struct spl_shrinker_wrap *)shrinker;

	if (sc->nr_to_scan != 0)
		(void) sw->scanfunc(&sw->shrinker, sc);
	return (sw->countfunc(&sw->shrinker, sc));
}
#endif

struct shrinker *
spl_register_shrinker(const char *name, spl_shrinker_cb countfunc,
    spl_shrinker_cb scanfunc, int seek_cost)
{
	struct shrinker *shrinker;

	/* allocate shrinker */
#if defined(HAVE_SHRINKER_REGISTER)
	/* 6.7: kernel will allocate the shrinker for us */
	shrinker = shrinker_alloc(0, name);
#elif defined(HAVE_SPLIT_SHRINKER_CALLBACK)
	/* 3.12-6.6: we allocate the shrinker  */
	shrinker = kmem_zalloc(sizeof (struct shrinker), KM_SLEEP);
#elif defined(HAVE_SINGLE_SHRINKER_CALLBACK)
	/* 3.0-3.11: allocate a wrapper */
	struct spl_shrinker_wrap *sw =
	    kmem_zalloc(sizeof (struct spl_shrinker_wrap), KM_SLEEP);
	shrinker = &sw->shrinker;
#else
	/* 2.x-2.6.22, or a newer shrinker API has been introduced. */
#error "Unknown shrinker API"
#endif

	if (shrinker == NULL)
		return (NULL);

	/* set callbacks */
#ifdef HAVE_SINGLE_SHRINKER_CALLBACK
	sw->countfunc = countfunc;
	sw->scanfunc = scanfunc;
	shrinker->shrink = spl_shrinker_single_cb;
#else
	shrinker->count_objects = countfunc;
	shrinker->scan_objects = scanfunc;
#endif

	/* set params */
	shrinker->seeks = seek_cost;

	/* register with kernel */
#if defined(HAVE_SHRINKER_REGISTER)
	shrinker_register(shrinker);
#elif defined(HAVE_REGISTER_SHRINKER_VARARG)
	register_shrinker(shrinker, name);
#else
	register_shrinker(shrinker);
#endif

	return (shrinker);
}
EXPORT_SYMBOL(spl_register_shrinker);

void
spl_unregister_shrinker(struct shrinker *shrinker)
{
#if defined(HAVE_SHRINKER_REGISTER)
	shrinker_free(shrinker);
#elif defined(HAVE_SPLIT_SHRINKER_CALLBACK)
	unregister_shrinker(shrinker);
	kmem_free(shrinker, sizeof (struct shrinker));
#elif defined(HAVE_SINGLE_SHRINKER_CALLBACK)
	unregister_shrinker(shrinker);
	kmem_free(shrinker, sizeof (struct spl_shrinker_wrap));
#else
#error "Unknown shrinker API"
#endif
}
EXPORT_SYMBOL(spl_unregister_shrinker);
