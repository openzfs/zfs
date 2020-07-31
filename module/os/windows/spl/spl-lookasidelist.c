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
 * Portions Copyright 2022 Andrew Innes <andrew.c12@gmail.com>
 */

#include <sys/zfs_context.h>
#include <sys/lookasidelist.h>


typedef struct lookasidelist_stats {
    kstat_named_t lookasidestat_active_alloc;
    kstat_named_t lookasidestat_total_alloc;
    kstat_named_t lookasidestat_total_free;
    kstat_named_t lookasidestat_chunk_size;
} lookasidelist_stats_t;

static lookasidelist_stats_t lookasidelist_stats = {
	/* Number of active allocations */
	{ "active_alloc",   KSTAT_DATA_UINT64 },
	/* The total number of allocations */
	{ "total_alloc",    KSTAT_DATA_UINT64 },
	/* The total number of frees */
	{ "total_free",	    KSTAT_DATA_UINT64 },
	/* The size of each object/chunk in lookaside list */
	{ "chunk_size",	    KSTAT_DATA_UINT64 },
};

static int
lookaside_kstat_update(kstat_t *ksp, int rw)
{
	lookasidelist_stats_t *ks = ksp->ks_data;
	lookasidelist_cache_t *cp = ksp->ks_private;

	if (rw == KSTAT_WRITE) {
		return (EACCES);
	}

	ks->lookasidestat_active_alloc.value.ui64 =
	    cp->cache_active_allocations;
	ks->lookasidestat_total_alloc.value.ui64 =
	    cp->total_alloc;
	ks->lookasidestat_total_free.value.ui64 =
	    cp->total_free;
	ks->lookasidestat_chunk_size.value.ui64 =
	    cp->cache_chunksize;

	return (0);
}

void *
allocate_func(
    __in POOL_TYPE  PoolType,
    __in SIZE_T  NumberOfBytes,
    __in ULONG  Tag,
    __inout PLOOKASIDE_LIST_EX  Lookaside)
{
	lookasidelist_cache_t *pLookasidelist_cache;
	pLookasidelist_cache = CONTAINING_RECORD(Lookaside,
	    lookasidelist_cache_t, lookasideField);
	void *buf = osif_malloc(NumberOfBytes);
	ASSERT(buf != NULL);

	if (buf != NULL) {
		atomic_inc_64(&pLookasidelist_cache->cache_active_allocations);
		atomic_inc_64(&pLookasidelist_cache->total_alloc);
	}

	return (buf);
}

void free_func(
    __in PVOID  Buffer,
    __inout PLOOKASIDE_LIST_EX  Lookaside
) {
	lookasidelist_cache_t *pLookasidelist_cache;
	pLookasidelist_cache = CONTAINING_RECORD(Lookaside,
	    lookasidelist_cache_t, lookasideField);
	osif_free(Buffer, pLookasidelist_cache->cache_chunksize);
	atomic_dec_64(&pLookasidelist_cache->cache_active_allocations);
	atomic_inc_64(&pLookasidelist_cache->total_free);
}

lookasidelist_cache_t *
lookasidelist_cache_create(char *name,	/* descriptive name for this cache */
    size_t size) /* size of the objects it manages */
{
	lookasidelist_cache_t *pLookasidelist_cache;
	pLookasidelist_cache = ExAllocatePoolWithTag(NonPagedPoolNx,
	    sizeof (lookasidelist_cache_t), ZFS_LookAsideList_DRV_TAG);

	if (pLookasidelist_cache != NULL) {
		memset(pLookasidelist_cache, 0, sizeof (lookasidelist_cache_t));
		pLookasidelist_cache->cache_chunksize = size;

		if (name != NULL) {
			strlcpy(pLookasidelist_cache->cache_name, name,
			    sizeof (pLookasidelist_cache->cache_name));
		}

		NTSTATUS retval = ExInitializeLookasideListEx(
		    &pLookasidelist_cache->lookasideField,
		    allocate_func,
		    free_func,
		    NonPagedPoolNx,
		    0,
		    size,
		    ZFS_LookAsideList_DRV_TAG,
		    0);

		ASSERT(retval == STATUS_SUCCESS);

		if (retval != STATUS_SUCCESS) {
			ExFreePoolWithTag(pLookasidelist_cache,
			    ZFS_LookAsideList_DRV_TAG);
			pLookasidelist_cache = NULL;
		}
	}

	if (pLookasidelist_cache != NULL) {
		kstat_t *ksp = kstat_create("spl", 0,
		    pLookasidelist_cache->cache_name,
		    "lookasidelist_cache", KSTAT_TYPE_NAMED,
		    sizeof (lookasidelist_stats) / sizeof (kstat_named_t),
		    KSTAT_FLAG_VIRTUAL);

		if (ksp != NULL) {
			ksp->ks_data = &lookasidelist_stats;
			ksp->ks_update = lookaside_kstat_update;
			ksp->ks_private = pLookasidelist_cache;
			pLookasidelist_cache->cache_kstat = ksp;
			kstat_install(ksp);
		}
	}

	return (pLookasidelist_cache);
}

void
lookasidelist_cache_destroy(lookasidelist_cache_t *pLookasidelist_cache)
{
	if (pLookasidelist_cache != NULL) {
		ExFlushLookasideListEx(&pLookasidelist_cache->lookasideField);
		ExDeleteLookasideListEx(&pLookasidelist_cache->lookasideField);

		if (pLookasidelist_cache->cache_kstat != NULL) {
			kstat_delete(pLookasidelist_cache->cache_kstat);
			pLookasidelist_cache->cache_kstat = NULL;
		}

		ExFreePoolWithTag(pLookasidelist_cache,
		    ZFS_LookAsideList_DRV_TAG);
	}
}

void *
lookasidelist_cache_alloc(lookasidelist_cache_t *pLookasidelist_cache)
{
	void *buf = ExAllocateFromLookasideListEx(
	    &pLookasidelist_cache->lookasideField);
	ASSERT(buf != NULL);
	return (buf);
}

void
lookasidelist_cache_free(lookasidelist_cache_t *pLookasidelist_cache, void *buf)
{
	ASSERT(buf != NULL);
	if (buf != NULL) {
		ExFreeToLookasideListEx(&pLookasidelist_cache->lookasideField,
		    buf);
	}
}
