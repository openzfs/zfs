/*
 * BSD 3-Clause New License (https://spdx.org/licenses/BSD-3-Clause.html)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 2016-2018, Klara Inc.
 * Copyright (c) 2016-2018, Allan Jude
 * Copyright (c) 2018-2020, Sebastian Gottschall
 * Copyright (c) 2019-2020, Michael Niewöhner
 * Copyright (c) 2020, The FreeBSD Foundation [1]
 *
 * [1] Portions of this software were developed by Allan Jude
 *     under sponsorship from the FreeBSD Foundation.
 */

#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/zstd/zstd.h>
#include <sys/zstd/zstd_impl.h>

#define	ZSTD_STATIC_LINKING_ONLY
#include "lib/zstd.h"

/* Enums describing the allocator type specified by kmem_type in zstd_kmem */
enum zstd_kmem_type {
	ZSTD_KMEM_UNKNOWN = 0,
	/* Allocation type using kmem_vmalloc */
	ZSTD_KMEM_DEFAULT,
	/* Pool based allocation using mempool_alloc */
	ZSTD_KMEM_POOL,
	/* Reserved fallback memory for decompression only */
	ZSTD_KMEM_FALLBACK_DCTX,
	ZSTD_KMEM_COUNT,
};

/* Structure for pooled memory objects */
struct zstd_pool {
	void *mem;
	size_t size;
	kmutex_t barrier;
	hrtime_t timeout;
};

/* Global structure for handling memory allocations */
struct zstd_kmem {
	enum zstd_kmem_type kmem_type;
	size_t kmem_size;
	struct zstd_pool *pool;
};

/* Fallback memory structure used for decompression only if memory runs out */
struct zstd_fallback_mem {
	size_t mem_size;
	void *mem;
	kmutex_t barrier;
};

/*
 * This variable represents the maximum count of the pool based on the number
 * of CPUs plus some buffer. We default to cpu count * 4, see init_zstd.
 */
static int pool_count = 16;

#define	ZSTD_POOL_MAX		pool_count
#define	ZSTD_POOL_TIMEOUT	60 * 2

static struct zstd_fallback_mem zstd_dctx_fallback;
static struct zstd_pool *zstd_mempool_cctx;
static struct zstd_pool *zstd_mempool_dctx;

/*
 * Try to get a cached allocated buffer from memory pool or allocate a new one
 * if necessary. If a object is older than 2 minutes and does not fit the
 * requested size, it will be released and a new cached entry will be allocated.
 * If other pooled objects are detected without being used for 2 minutes, they
 * will be released, too.
 *
 * The concept is that high frequency memory allocations of bigger objects are
 * expensive. So if a lot of work is going on, allocations will be kept for a
 * while and can be reused in that time frame.
 *
 * The scheduled release will be updated every time a object is reused.
 */
static void *
zstd_mempool_alloc(struct zstd_pool *zstd_mempool, size_t size)
{
	struct zstd_pool *pool;
	struct zstd_kmem *mem = NULL;

	if (!zstd_mempool) {
		return (NULL);
	}

	/* Seek for preallocated memory slot and free obsolete slots */
	for (int i = 0; i < ZSTD_POOL_MAX; i++) {
		pool = &zstd_mempool[i];
		/*
		 * This lock is simply a marker for a pool object beeing in use.
		 * If it's already hold, it will be skipped.
		 *
		 * We need to create it before checking it to avoid race
		 * conditions caused by running in a threaded context.
		 *
		 * The lock is later released by zstd_mempool_free.
		 */
		if (mutex_tryenter(&pool->barrier)) {
			/*
			 * Check if objects fits the size, if so we take it and
			 * update the timestamp.
			 */
			if (size && !mem && pool->mem && size <= pool->size) {
				pool->timeout = gethrestime_sec() +
				    ZSTD_POOL_TIMEOUT;
				mem = pool->mem;
				continue;
			}

			/* Free memory if unused object older than 2 minutes */
			if (pool->mem && gethrestime_sec() > pool->timeout) {
				vmem_free(pool->mem, pool->size);
				ZSTDSTAT_SUB(zstd_stat_buffers, 1);
				ZSTDSTAT_SUB(zstd_stat_size, pool->size);
				pool->mem = NULL;
				pool->size = 0;
				pool->timeout = 0;
			}

			mutex_exit(&pool->barrier);
		}
	}

	if (!size || mem) {
		return (mem);
	}

	/*
	 * If no preallocated slot was found, try to fill in a new one.
	 *
	 * We run a similar algorithm twice here to avoid pool fragmentation.
	 * The first one may generate holes in the list if objects get released.
	 * We always make sure that these holes get filled instead of adding new
	 * allocations constantly at the end.
	 */
	for (int i = 0; i < ZSTD_POOL_MAX; i++) {
		pool = &zstd_mempool[i];
		if (mutex_tryenter(&pool->barrier)) {
			/* Object is free, try to allocate new one */
			if (!pool->mem) {
				mem = vmem_alloc(size, KM_SLEEP);
				if (mem) {
					ZSTDSTAT_ADD(zstd_stat_buffers, 1);
					ZSTDSTAT_ADD(zstd_stat_size, size);
					pool->mem = mem;
					pool->size = size;
					/* Keep track for later release */
					mem->pool = pool;
					mem->kmem_type = ZSTD_KMEM_POOL;
					mem->kmem_size = size;
				}
			}

			if (size <= pool->size) {
				/* Update timestamp */
				pool->timeout = gethrestime_sec() +
				    ZSTD_POOL_TIMEOUT;

				return (pool->mem);
			}

			mutex_exit(&pool->barrier);
		}
	}

	/*
	 * If the pool is full or the allocation failed, try lazy allocation
	 * instead.
	 */
	if (!mem) {
		mem = vmem_alloc(size, KM_NOSLEEP);
		if (mem) {
			mem->pool = NULL;
			mem->kmem_type = ZSTD_KMEM_DEFAULT;
			mem->kmem_size = size;
		}
	}

	return (mem);
}

/* Mark object as released by releasing the barrier mutex */
static void
zstd_mempool_free(struct zstd_kmem *z)
{
	mutex_exit(&z->pool->barrier);
}

/* Allocator for zstd compression context using mempool_allocator */
void *
zstd_alloc(void *opaque __maybe_unused, size_t size)
{
	size_t nbytes = sizeof (struct zstd_kmem) + size;
	struct zstd_kmem *z = NULL;

	z = (struct zstd_kmem *)zstd_mempool_alloc(zstd_mempool_cctx, nbytes);

	if (!z) {
		ZSTDSTAT_BUMP(zstd_stat_alloc_fail);
		return (NULL);
	}

	return ((void*)z + (sizeof (struct zstd_kmem)));
}

/*
 * Allocator for zstd decompression context using mempool_allocator with
 * fallback to reserved memory if allocation fails
 */
void *
zstd_dctx_alloc(void *opaque __maybe_unused, size_t size)
{
	size_t nbytes = sizeof (struct zstd_kmem) + size;
	struct zstd_kmem *z = NULL;
	enum zstd_kmem_type type = ZSTD_KMEM_DEFAULT;

	z = (struct zstd_kmem *)zstd_mempool_alloc(zstd_mempool_dctx, nbytes);
	if (!z) {
		/* Try harder, decompression shall not fail */
		z = vmem_alloc(nbytes, KM_SLEEP);
		if (z) {
			z->pool = NULL;
		}
		ZSTDSTAT_BUMP(zstd_stat_alloc_fail);
	} else {
		return ((void*)z + (sizeof (struct zstd_kmem)));
	}

	/* Fallback if everything fails */
	if (!z) {
		/*
		 * Barrier since we only can handle it in a single thread. All
		 * other following threads need to wait here until decompression
		 * is completed. zstd_free will release this barrier later.
		 */
		mutex_enter(&zstd_dctx_fallback.barrier);

		z = zstd_dctx_fallback.mem;
		type = ZSTD_KMEM_FALLBACK_DCTX;
		ZSTDSTAT_BUMP(zstd_stat_alloc_fallback);
	}

	/* Allocation should always be successful */
	if (!z) {
		return (NULL);
	}

	z->kmem_type = type;
	z->kmem_size = nbytes;

	return ((void*)z + (sizeof (struct zstd_kmem)));
}

/* Free allocated memory by its specific type */
void
zstd_free(void *opaque __maybe_unused, void *ptr)
{
	struct zstd_kmem *z = (ptr - sizeof (struct zstd_kmem));
	enum zstd_kmem_type type;

	ASSERT3U(z->kmem_type, <, ZSTD_KMEM_COUNT);
	ASSERT3U(z->kmem_type, >, ZSTD_KMEM_UNKNOWN);

	type = z->kmem_type;
	switch (type) {
	case ZSTD_KMEM_DEFAULT:
		vmem_free(z, z->kmem_size);
		break;
	case ZSTD_KMEM_POOL:
		zstd_mempool_free(z);
		break;
	case ZSTD_KMEM_FALLBACK_DCTX:
		mutex_exit(&zstd_dctx_fallback.barrier);
		break;
	default:
		break;
	}
}

/* Allocate fallback memory to ensure safe decompression */
static void __init
create_fallback_mem(struct zstd_fallback_mem *mem, size_t size)
{
	mem->mem_size = size;
	mem->mem = vmem_zalloc(mem->mem_size, KM_SLEEP);
	mutex_init(&mem->barrier, NULL, MUTEX_DEFAULT, NULL);
}

/* Initialize memory pool barrier mutexes */
static void __init
zstd_mempool_init(void)
{
	zstd_mempool_cctx = (struct zstd_pool *)
	    kmem_zalloc(ZSTD_POOL_MAX * sizeof (struct zstd_pool), KM_SLEEP);
	zstd_mempool_dctx = (struct zstd_pool *)
	    kmem_zalloc(ZSTD_POOL_MAX * sizeof (struct zstd_pool), KM_SLEEP);

	for (int i = 0; i < ZSTD_POOL_MAX; i++) {
		mutex_init(&zstd_mempool_cctx[i].barrier, NULL,
		    MUTEX_DEFAULT, NULL);
		mutex_init(&zstd_mempool_dctx[i].barrier, NULL,
		    MUTEX_DEFAULT, NULL);
	}
}

/* Initialize zstd-related memory handling */
static int __init
zstd_meminit(void)
{
	zstd_mempool_init();

	/*
	 * Estimate the size of the fallback decompression context.
	 * The expected size on x64 with current ZSTD should be about 160 KB.
	 */
	create_fallback_mem(&zstd_dctx_fallback,
	    P2ROUNDUP(ZSTD_estimateDCtxSize() + sizeof (struct zstd_kmem),
	    PAGESIZE));

	return (0);
}

/* Release object from pool and free memory */
static void __exit
release_pool(struct zstd_pool *pool)
{
	mutex_destroy(&pool->barrier);
	vmem_free(pool->mem, pool->size);
	pool->mem = NULL;
	pool->size = 0;
}

/* Release memory pool objects */
static void __exit
zstd_mempool_deinit(void)
{
	for (int i = 0; i < ZSTD_POOL_MAX; i++) {
		release_pool(&zstd_mempool_cctx[i]);
		release_pool(&zstd_mempool_dctx[i]);
	}

	kmem_free(zstd_mempool_dctx, ZSTD_POOL_MAX * sizeof (struct zstd_pool));
	kmem_free(zstd_mempool_cctx, ZSTD_POOL_MAX * sizeof (struct zstd_pool));
	zstd_mempool_dctx = NULL;
	zstd_mempool_cctx = NULL;
}

/* release unused memory from pool */
void
zfs_zstd_cache_reap_now(void)
{
	/*
	 * calling alloc with zero size seeks
	 * and releases old unused objects
	 */
	zstd_mempool_alloc(zstd_mempool_cctx, 0);
	zstd_mempool_alloc(zstd_mempool_dctx, 0);
}

int __init
zstd_init_os(void)
{
	/* Set pool size by using maximum sane thread count * 4 */
	pool_count = (boot_ncpus * 4);
	zstd_meminit();

	return (0);
}

void __exit
zstd_fini_os(void)
{
	/* Release fallback memory */
	vmem_free(zstd_dctx_fallback.mem, zstd_dctx_fallback.mem_size);
	mutex_destroy(&zstd_dctx_fallback.barrier);

	/* Deinit memory pool */
	zstd_mempool_deinit();
}
