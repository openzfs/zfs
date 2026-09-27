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
 * Copyright (c) 2020, DataCore Software Corp.
 */

#ifndef _SPL_LOOKASIDELIST_H
#define	_SPL_LOOKASIDELIST_H

extern void *osif_malloc(uint64_t);
extern void osif_free(void *, uint64_t);

#define	ZFS_LookAsideList_DRV_TAG '!SFZ'

#define	LOOKASIDELIST_CACHE_NAMELEN 31

typedef struct lookasidelist_cache {
    uint64_t cache_active_allocations;
    uint64_t total_alloc;
    uint64_t total_free;
    size_t cache_chunksize; // cache object size
    kstat_t *cache_kstat;
    char    cache_name[LOOKASIDELIST_CACHE_NAMELEN + 1];
    LOOKASIDE_LIST_EX lookasideField;
} lookasidelist_cache_t;

lookasidelist_cache_t *lookasidelist_cache_create(char *name, size_t size);
void lookasidelist_cache_destroy(lookasidelist_cache_t *pLookasidelist_cache);
void* lookasidelist_cache_alloc(lookasidelist_cache_t *pLookasidelist_cache);
void lookasidelist_cache_free(lookasidelist_cache_t *pLookasidelist_cache,
    void *buf);

#endif
