/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
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
