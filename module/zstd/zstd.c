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
 * Copyright (c) 2016-2018, Klara Systems Inc. All rights reserved.
 * Copyright (c) 2016-2018, Allan Jude. All rights reserved.
 * Copyright (c) 2018-2019, Sebastian Gottschall. All rights reserved.
 * Copyright (c) 2019, Michael Niewöhner. All rights reserved.
 */

#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/zfs_context.h>
#include <sys/zio_compress.h>
#include <sys/spa.h>
#include <sys/zstd/zstd.h>

#define	ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>
#include <zstd_errors.h>
#include <error_private.h>

/* for userspace compile, we disable error debugging */
#ifndef _KERNEL
#define	printk(fmt, ...)
#endif

/* User space tests compatibility */
#ifndef _KERNEL
#define	__init
#define	__exit
#endif

/* These enums are index references to zstd_cache_config */
enum zstd_kmem_type {
	ZSTD_KMEM_UNKNOWN = 0,
	/* allocation type using kmem_vmalloc */
	ZSTD_KMEM_DEFAULT,
	/* pool based allocation using mempool_alloc */
	ZSTD_KMEM_POOL,
	/* reserved fallback memory for decompression only */
	ZSTD_KMEM_DCTX,
	ZSTD_KMEM_COUNT,
};

struct zstd_pool {
	void *mem;
	size_t size;
	kmutex_t 		barrier;
	time_t timeout;
};

struct zstd_kmem {
	enum zstd_kmem_type	kmem_type;
	size_t			kmem_size;
	struct zstd_pool	*pool;
};

struct zstd_fallback_mem {
	size_t			mem_size;
	void			*mem;
	kmutex_t 		barrier;
};

struct levelmap {
	int32_t cookie;
	enum zio_zstd_levels level;
};

static void *zstd_alloc(void *opaque, size_t size);
static void *zstd_dctx_alloc(void *opaque, size_t size);
static void zstd_free(void *opaque, void *ptr);

static const ZSTD_customMem zstd_malloc = {
	zstd_alloc,
	zstd_free,
	NULL,
};

static const ZSTD_customMem zstd_dctx_malloc = {
	zstd_dctx_alloc,
	zstd_free,
	NULL,
};

static struct levelmap fastlevels[] = {
	{ZIO_ZSTDLVL_1, ZIO_ZSTDLVL_1},
	{ZIO_ZSTDLVL_2, ZIO_ZSTDLVL_2},
	{ZIO_ZSTDLVL_3, ZIO_ZSTDLVL_3},
	{ZIO_ZSTDLVL_4, ZIO_ZSTDLVL_4},
	{ZIO_ZSTDLVL_5, ZIO_ZSTDLVL_5},
	{ZIO_ZSTDLVL_6, ZIO_ZSTDLVL_6},
	{ZIO_ZSTDLVL_7, ZIO_ZSTDLVL_7},
	{ZIO_ZSTDLVL_8, ZIO_ZSTDLVL_8},
	{ZIO_ZSTDLVL_9, ZIO_ZSTDLVL_9},
	{ZIO_ZSTDLVL_10, ZIO_ZSTDLVL_10},
	{ZIO_ZSTDLVL_11, ZIO_ZSTDLVL_11},
	{ZIO_ZSTDLVL_12, ZIO_ZSTDLVL_12},
	{ZIO_ZSTDLVL_13, ZIO_ZSTDLVL_13},
	{ZIO_ZSTDLVL_14, ZIO_ZSTDLVL_14},
	{ZIO_ZSTDLVL_15, ZIO_ZSTDLVL_15},
	{ZIO_ZSTDLVL_16, ZIO_ZSTDLVL_16},
	{ZIO_ZSTDLVL_17, ZIO_ZSTDLVL_17},
	{ZIO_ZSTDLVL_18, ZIO_ZSTDLVL_18},
	{ZIO_ZSTDLVL_19, ZIO_ZSTDLVL_19},
	{-1, ZIO_ZSTDLVL_FAST_1},
	{-2, ZIO_ZSTDLVL_FAST_2},
	{-3, ZIO_ZSTDLVL_FAST_3},
	{-4, ZIO_ZSTDLVL_FAST_4},
	{-5, ZIO_ZSTDLVL_FAST_5},
	{-6, ZIO_ZSTDLVL_FAST_6},
	{-7, ZIO_ZSTDLVL_FAST_7},
	{-8, ZIO_ZSTDLVL_FAST_8},
	{-9, ZIO_ZSTDLVL_FAST_9},
	{-10, ZIO_ZSTDLVL_FAST_10},
	{-20, ZIO_ZSTDLVL_FAST_20},
	{-30, ZIO_ZSTDLVL_FAST_30},
	{-40, ZIO_ZSTDLVL_FAST_40},
	{-50, ZIO_ZSTDLVL_FAST_50},
	{-60, ZIO_ZSTDLVL_FAST_60},
	{-70, ZIO_ZSTDLVL_FAST_70},
	{-80, ZIO_ZSTDLVL_FAST_80},
	{-90, ZIO_ZSTDLVL_FAST_90},
	{-100, ZIO_ZSTDLVL_FAST_100},
	{-500, ZIO_ZSTDLVL_FAST_500},
	{-1000, ZIO_ZSTDLVL_FAST_1000},
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
 * if neccessary. If a object is older than 2 minutes and does not fit the
 * requested size, it will be released and a new cached entry will be allocated.
 * If other pooled objects are detected without beeing used for 2 minutes, they
 * will be released, too.
 *
 * The concept is that high frequency memory allocations of bigger objects are
 * expensive. So if alot of work is going on, allocations will be kept for a
 * while and can be reused in that timeframe.
 *
 * The scheduled release will be updated every time a object is reused.
 */
static void *
zstd_mempool_alloc(struct zstd_pool *zstd_mempool, size_t size)
{
	int i;
	struct zstd_pool *pool;
	struct zstd_kmem *mem = NULL;

	if (!zstd_mempool) {
		return (NULL);
	}

	/* Seek for preallocated memory slot and free obsolete slots */
	for (i = 0; i < ZSTD_POOL_MAX; i++) {
		pool = &zstd_mempool[i];
		if (mutex_tryenter(&pool->barrier)) {
			/*
			 * Check if objects fits the size, if so we take it and
			 * update the timestamp.
			 */
			if (!mem && pool->mem && size <= pool->size) {
				pool->timeout = gethrestime_sec() +
				    ZSTD_POOL_TIMEOUT;
				mem = pool->mem;
				continue;
			}

			/* Free memory if object is older than 2 minutes */
			if (pool->mem && gethrestime_sec() > pool->timeout) {
				kmem_free(pool->mem, pool->size);
				pool->mem = NULL;
				pool->size = 0;
				pool->timeout = 0;
			}

			mutex_exit(&pool->barrier);
		}
	}

	if (mem) {
		return (mem);
	}

	/* If no preallocated slot was found, try to fill in a new one */
	for (i = 0; i < ZSTD_POOL_MAX; i++) {
		pool = &zstd_mempool[i];
		if (mutex_tryenter(&pool->barrier)) {
			/* Object is free, try to allocate new one */
			if (!pool->mem) {
				mem = vmem_alloc(size, KM_SLEEP);
				pool->mem = mem;

				/* Allocation successfull? */
				if (pool->mem) {
					/* Keep track for later release */
					mem->pool = pool;
					pool->size = size;
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

/* Convert internal stored zfs level enum to zstd level */
static enum zio_zstd_levels
zstd_cookie_to_enum(int32_t level)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(fastlevels); i++) {
		if (fastlevels[i].cookie == level) {
			return (fastlevels[i].level);
		}
	}

	/* This shouldn't happen. Fall back to the default level. */
	printk(KERN_ERR "%s:Invalid ZSTD level encountered: %d",
	    __func__, level);
	return (ZIO_ZSTD_LEVEL_DEFAULT);
}

/* Convert zstd_level to internal stored zfs level */
static int32_t
zstd_enum_to_cookie(enum zio_zstd_levels elevel)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(fastlevels); i++) {
		if (fastlevels[i].level == elevel)
			return (fastlevels[i].cookie);
	}

	/* This shouldn't happen. Fall back to the default level. */
	printk(KERN_ERR "%s:Invalid ZSTD enum level encountered: %d",
	    __func__, elevel);
	return (ZIO_ZSTD_LEVEL_DEFAULT);
}

/* Compress block using zstd */
size_t
zstd_compress(void *s_start, void *d_start, size_t s_len, size_t d_len,
    int level)
{
	size_t c_len;
	uint32_t bufsize;
	int32_t levelcookie;
	char *dest = d_start;
	ZSTD_CCtx *cctx;

	levelcookie = zstd_enum_to_cookie(level);
	ASSERT3U(d_len, >=, sizeof (bufsize));
	ASSERT3U(d_len, <=, s_len);
	ASSERT3U(levelcookie, !=, 0);

	if (levelcookie == ZIO_COMPLEVEL_DEFAULT) {
		levelcookie = ZIO_ZSTD_LEVEL_DEFAULT;
	}
	if (levelcookie == ZIO_ZSTDLVL_DEFAULT) {
		levelcookie = ZIO_ZSTD_LEVEL_DEFAULT;
	}

	cctx = ZSTD_createCCtx_advanced(zstd_malloc);

	/*
	 * Out of kernel memory, gently fall through - this will disable
	 * compression in zio_compress_data
	 */
	if (!cctx) {
		return (s_len);
	}

	c_len = ZSTD_compressCCtx(cctx,
	    &dest[sizeof (bufsize) + sizeof (levelcookie)],
	    d_len - sizeof (bufsize) - sizeof (levelcookie),
	    s_start, s_len, levelcookie);

	ZSTD_freeCCtx(cctx);

	/* Error in the compression routine, disable compression. */
	if (ZSTD_isError(c_len)) {
		return (s_len);
	}

	/*
	 * Encode the compressed buffer size at the start. We'll need this in
	 * decompression to counter the effects of padding which might be added
	 * to the compressed buffer and which, if unhandled, would confuse the
	 * hell out of our decompression function.
	 */
	bufsize = c_len;
	*(uint32_t *)dest = BE_32(bufsize);

	/*
	 * Encode the compression level as well. We may need to know the
	 * original compression level if compressed_arc is disabled, to match
	 * the compression settings to write this block to the L2ARC. Encode
	 * the actual level, so if the enum changes in the future, we will be
	 * compatible.
	 */
	*(uint32_t *)(&dest[sizeof (bufsize)]) = BE_32(levelcookie);

	return (c_len + sizeof (bufsize) + sizeof (levelcookie));
}

/* Decompress block using zstd and return its stored level */
int
zstd_decompress_level(void *s_start, void *d_start, size_t s_len, size_t d_len,
    uint8_t *level)
{
	const char *src = s_start;
	ZSTD_DCtx *dctx;
	size_t result;
	uint32_t bufsize = BE_IN32(src);
	int32_t levelcookie = (int32_t)BE_IN32(&src[sizeof (bufsize)]);
	uint8_t zstdlevel = zstd_cookie_to_enum(levelcookie);

	ASSERT3U(d_len, >=, s_len);
	ASSERT3U(zstdlevel, !=, ZIO_ZSTDLVL_INHERIT);

	/* Invalid compressed buffer size encoded at start */
	if (bufsize + sizeof (bufsize) > s_len) {
		return (1);
	}

	dctx = ZSTD_createDCtx_advanced(zstd_dctx_malloc);
	if (!dctx) {
		return (1);
	}

	result = ZSTD_decompressDCtx(dctx, d_start, d_len,
	    &src[sizeof (bufsize) + sizeof (levelcookie)], bufsize);

	ZSTD_freeDCtx(dctx);

	/*
	 * Returns 0 on success (decompression function returned non-negative)
	 * and non-zero on failure (decompression function returned negative.
	 */
	if (ZSTD_isError(result)) {
		return (1);
	}

	if (level) {
		*level = zstdlevel;
	}

	return (0);
}

/* Decompress datablock using zstd */
int
zstd_decompress(void *s_start, void *d_start, size_t s_len, size_t d_len,
    int level __maybe_unused)
{

	return (zstd_decompress_level(s_start, d_start, s_len, d_len, NULL));
}

/* Allocator for zstd compression context using mempool_allocator */
static void *
zstd_alloc(void *opaque __maybe_unused, size_t size)
{
	size_t nbytes = sizeof (struct zstd_kmem) + size;
	struct zstd_kmem *z = NULL;

	z = (struct zstd_kmem *)zstd_mempool_alloc(zstd_mempool_cctx, nbytes);

	if (!z) {
		return (NULL);
	}

	return ((void*)z + (sizeof (struct zstd_kmem)));
}

/*
 * Allocator for zstd decompression context using mempool_allocator with
 * fallback to reserved memory if allocation fails
 */
static void *
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
	} else {
		return ((void*)z + (sizeof (struct zstd_kmem)));
	}

	/* Fallback if everything fails */
	if (!z) {
		/* Barrier since we only can handle it in a single thread */
		mutex_enter(&zstd_dctx_fallback.barrier);
		mutex_exit(&zstd_dctx_fallback.barrier);
		mutex_enter(&zstd_dctx_fallback.barrier);
		z = zstd_dctx_fallback.mem;
		type = ZSTD_KMEM_DCTX;
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
static void
zstd_free(void *opaque __maybe_unused, void *ptr)
{
	struct zstd_kmem *z = (ptr - sizeof (struct zstd_kmem));
	enum zstd_kmem_type type;

	ASSERT3U(z->kmem_type, <, ZSTD_KMEM_COUNT);
	ASSERT3U(z->kmem_type, >, ZSTD_KMEM_UNKNOWN);

	type = z->kmem_type;
	switch (type) {
	case ZSTD_KMEM_DEFAULT:
		kmem_free(z, z->kmem_size);
		break;
	case ZSTD_KMEM_POOL:
		zstd_mempool_free(z);
		break;
	case ZSTD_KMEM_DCTX:
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
	int i;

	zstd_mempool_cctx = (struct zstd_pool *)
	    kmem_zalloc(ZSTD_POOL_MAX * sizeof (struct zstd_pool), KM_SLEEP);
	zstd_mempool_dctx = (struct zstd_pool *)
	    kmem_zalloc(ZSTD_POOL_MAX * sizeof (struct zstd_pool), KM_SLEEP);

	for (i = 0; i < ZSTD_POOL_MAX; i++) {
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

	/* Estimate the size of the fallback decompression context */
	create_fallback_mem(&zstd_dctx_fallback,
	    P2ROUNDUP(ZSTD_estimateDCtxSize() + sizeof (struct zstd_kmem),
	    PAGESIZE));

	return (0);
}

/* Release object from pool and free memory */
static void __exit
release_pool(struct zstd_pool *pool)
{
	mutex_enter(&pool->barrier);
	mutex_exit(&pool->barrier);
	mutex_destroy(&pool->barrier);
	kmem_free(pool->mem, pool->size);
	pool->mem = NULL;
	pool->size = 0;
}

/* Release memory pool objects */
static void __exit
zstd_mempool_deinit(void)
{
	int i;

	for (i = 0; i < ZSTD_POOL_MAX; i++) {
		release_pool(&zstd_mempool_cctx[i]);
		release_pool(&zstd_mempool_dctx[i]);
	}

	kmem_free(zstd_mempool_dctx, ZSTD_POOL_MAX * sizeof (struct zstd_pool));
	kmem_free(zstd_mempool_cctx, ZSTD_POOL_MAX * sizeof (struct zstd_pool));
	zstd_mempool_dctx = NULL;
	zstd_mempool_cctx = NULL;
}

extern int __init
zstd_init(void)
{
	/* Set pool size by using maximum sane thread count * 4 */
	pool_count = (boot_ncpus * 4);
	zstd_meminit();

	return (0);
}

extern void __exit
zstd_fini(void)
{
	kmem_free(zstd_dctx_fallback.mem, zstd_dctx_fallback.mem_size);
	mutex_destroy(&zstd_dctx_fallback.barrier);
	zstd_mempool_deinit();
}

#if defined(_KERNEL)
module_init(zstd_init);
module_exit(zstd_fini);

ZFS_MODULE_DESCRIPTION("ZSTD Compression for ZFS");
ZFS_MODULE_LICENSE("Dual BSD/GPL");
ZFS_MODULE_VERSION(ZSTD_VERSION_STRING);

EXPORT_SYMBOL(zstd_compress);
EXPORT_SYMBOL(zstd_decompress_level);
EXPORT_SYMBOL(zstd_decompress);
#endif
