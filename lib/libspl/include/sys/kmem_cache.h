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
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2012, Joyent, Inc. All rights reserved.
 * Copyright (c) 2026, TrueNAS.
 */

#ifndef _SYS_KMEM_CACHE_H
#define	_SYS_KMEM_CACHE_H

#include <sys/types.h>

#ifdef  __cplusplus
extern "C" {
#endif

#define	KMC_NODEBUG		0x00020000
#define	KMC_KVMEM		0x0
#define	KMC_RECLAIMABLE		0x0

typedef int kmem_constructor_t(void *, void *, int);
typedef void kmem_destructor_t(void *, void *);
typedef void kmem_reclaim_t(void *);

typedef struct kmem_cache kmem_cache_t;

extern kmem_cache_t *kmem_cache_create(const char *name,
    size_t bufsize, size_t align,
    kmem_constructor_t *constructor, kmem_destructor_t *destructor,
    kmem_reclaim_t *reclaim, void *priv, void *vmp, int cflags);

extern void kmem_cache_destroy(kmem_cache_t *cp);

__attribute__((malloc))
extern void *kmem_cache_alloc(kmem_cache_t *cp, int flags);

extern void kmem_cache_free(kmem_cache_t *cp, void *ptr);

extern void kmem_cache_reap_now(kmem_cache_t *cp);
extern int kmem_cache_reap_active(void);

typedef enum kmem_cbrc {
	KMEM_CBRC_YES,
	KMEM_CBRC_NO,
	KMEM_CBRC_LATER,
	KMEM_CBRC_DONT_NEED,
	KMEM_CBRC_DONT_KNOW
} kmem_cbrc_t;

/*
 * A discard macro instead of a stub for now, as a true stub causes compile
 * errors elsewhere; these need to be dealt with first.
 */
#define	kmem_cache_set_move(_c, _cb)

#ifdef  __cplusplus
}
#endif

#endif
