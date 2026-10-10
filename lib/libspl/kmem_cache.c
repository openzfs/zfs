// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */
/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2016 Actifio, Inc. All rights reserved.
 * Copyright (c) 2025, Klara, Inc.
 * Copyright (c) 2026, TrueNAS.
 */

#include <sys/kmem_cache.h>
#include <sys/kmem.h>
#include <sys/string.h>

#define	KMEM_CACHE_NAMELEN	31

struct kmem_cache {
	char			cache_name[KMEM_CACHE_NAMELEN + 1];
	size_t			cache_bufsize;
	size_t			cache_align;
	kmem_constructor_t	*cache_constructor;
	kmem_destructor_t	*cache_destructor;
	kmem_reclaim_t		*cache_reclaim;
	void			*cache_private;
	void			*cache_arena;
	int			cache_cflags;
};

kmem_cache_t *
kmem_cache_create(const char *name, size_t bufsize, size_t align,
    kmem_constructor_t *constructor, kmem_destructor_t *destructor,
    kmem_reclaim_t *reclaim, void *priv, void *vmp, int cflags)
{
	kmem_cache_t *cp;

	cp = (kmem_cache_t *)kmem_alloc(sizeof (kmem_cache_t), KM_NOSLEEP);
	if (cp) {
		strlcpy(cp->cache_name, name, KMEM_CACHE_NAMELEN);
		cp->cache_bufsize = bufsize;
		cp->cache_align = align;
		cp->cache_constructor = constructor;
		cp->cache_destructor = destructor;
		cp->cache_reclaim = reclaim;
		cp->cache_private = priv;
		cp->cache_arena = vmp;
		cp->cache_cflags = cflags;
	}

	return (cp);
}

void
kmem_cache_destroy(kmem_cache_t *cp)
{
	kmem_free(cp, sizeof (kmem_cache_t));
}

void *
kmem_cache_alloc(kmem_cache_t *cp, int flags)
{
	void *ptr = NULL;

	if (cp->cache_align != 0)
		ptr = kmem_alloc_aligned(
		    cp->cache_bufsize, cp->cache_align, flags);
	else
		ptr = kmem_alloc(cp->cache_bufsize, flags);

	if (ptr && cp->cache_constructor)
		cp->cache_constructor(ptr, cp->cache_private, KM_NOSLEEP);

	return (ptr);
}

void
kmem_cache_free(kmem_cache_t *cp, void *ptr)
{
	if (cp->cache_destructor)
		cp->cache_destructor(ptr, cp->cache_private);

	if (cp->cache_align != 0)
		kmem_free_aligned(ptr, cp->cache_bufsize);
	else
		kmem_free(ptr, cp->cache_bufsize);
}

void
kmem_cache_reap_now(kmem_cache_t *cp)
{
	(void) cp;
}

int
kmem_cache_reap_active(void)
{
	return (0);
}
