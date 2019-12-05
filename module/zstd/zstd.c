/*
 * BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer
 * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 2016-2018 by Klara Systems Inc.
 * Copyright (c) 2016-2018 Allan Jude <allanjude@freebsd.org>
 * Copyright (c) 2018-2019 Sebastian Gottschall <s.gottschall@dd-wrt.com>
 */

#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/zfs_context.h>
#include <sys/zio_compress.h>
#include <sys/spa.h>

#define	ZSTD_STATIC_LINKING_ONLY
#include <sys/zstd/zstd.h>
#include <sys/zstd/zstd_errors.h>
#include <sys/zstd/error_private.h>

/* for BSD compat */
#define	__unused			__attribute__((unused))

/* for userspace compile, we disable error debugging */
#ifndef _KERNEL
#define	printk(fmt, ...)
#endif


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
/* these enums are index references to zstd_cache_config */

enum zstd_kmem_type {
	ZSTD_KMEM_UNKNOWN = 0,
	ZSTD_KMEM_DEFAULT,
	ZSTD_KMEM_POOL,
	ZSTD_KMEM_DCTX,
	ZSTD_KMEM_COUNT,
};
/*
 * this variable should represent the maximum count of the pool based on
 * the number of cpu's running in the system with some budged. by default
 * we use the cpu count * 4. see init_zstd
 */
static int pool_count = 16;

#define	ZSTD_POOL_MAX		pool_count
#define	ZSTD_POOL_TIMEOUT	60 * 2

struct zstd_kmem;

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

static struct zstd_fallback_mem zstd_dctx_fallback;
static struct zstd_pool *zstd_mempool_cctx;
static struct zstd_pool *zstd_mempool_dctx;

/* initializes memory pool barrier mutexes */
void
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

/* release object from pool and free memory */
static void
release_pool(struct zstd_pool *pool)
{
	mutex_enter(&pool->barrier);
	mutex_exit(&pool->barrier);
	mutex_destroy(&pool->barrier);
	kmem_free(pool->mem, pool->size);
	pool->mem = NULL;
	pool->size = 0;
}

/* releases memory pool objects */
void
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

/*
 * tries to get cached allocated buffer from memory pool and allocate new one
 * if neccessary. if a object is older than 2 minutes and does not fit to the
 * requested size, it will be released and a new cached entry will be allocated
 * if other pooled objects are detected without beeing used for 2 minutes,
 * they will be released too.
 * the concept is that high frequency memory allocations of bigger objects are
 * expensive.
 * so if alot of work is going on, allocations will be kept for a while and can
 * be reused in that timeframe. the scheduled release will be updated every time
 * a object is reused.
 */
void *
zstd_mempool_alloc(struct zstd_pool *zstd_mempool, size_t size)
{
	int i;
	struct zstd_pool *pool;
	struct zstd_kmem *mem = NULL;

	if (!zstd_mempool)
		return (NULL);
	/*
	 * seek for preallocated memory slot and free obsolete slots
	 */
	for (i = 0; i < ZSTD_POOL_MAX; i++) {
		pool = &zstd_mempool[i];
		if (mutex_tryenter(&pool->barrier)) {
			/*
			 * check if objects fits to size, if yes we take it
			 * and update timestamp
			 */
			if (!mem && pool->mem && size <= pool->size) {
				pool->timeout = gethrestime_sec() +
				    ZSTD_POOL_TIMEOUT;
				mem = pool->mem;
				continue;
			}
			/*
			 * free memory if object is older than 2 minutes
			 */
			if (pool->mem && gethrestime_sec() > pool->timeout) {
				kmem_free(pool->mem, pool->size);
				pool->mem = NULL;
				pool->size = 0;
				pool->timeout = 0;
			}
			mutex_exit(&pool->barrier);
		}
	}

	if (mem)
		return (mem);
	/*
	 * if no preallocated slot was found, try to fill in a new one
	 */
	for (i = 0; i < ZSTD_POOL_MAX; i++) {
		pool = &zstd_mempool[i];
		if (mutex_tryenter(&pool->barrier)) {
			/* object is free, try to allocate new one */
			if (!pool->mem) {
				mem = vmem_alloc(size, KM_SLEEP);
				pool->mem = mem;
				/* allocation successfull? */
				if (pool->mem) {
					/*
					 * keep track for later release
					 */
					mem->pool = pool;
					pool->size = size;
					mem->kmem_type = ZSTD_KMEM_POOL;
					mem->kmem_size = size;
				} else {
					mutex_exit(&pool->barrier);
				}
			}
			if (size <= pool->size) {
				/* update timestamp */
				pool->timeout = gethrestime_sec() +
				    ZSTD_POOL_TIMEOUT;
				return (pool->mem);
			}
			mutex_exit(&pool->barrier);
		}
	}

	/*
	 * maybe pool is full or allocation failed, lets do lazy allocation
	 * try in that case
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

/*
 * mark object as released by releasing the barrier
 * mutex and clear the buffer
 */
void
zstd_mempool_free(struct zstd_kmem *z)
{
	struct zstd_pool *pool = z->pool;
	mutex_exit(&pool->barrier);
}

struct levelmap {
	int32_t cookie;
	enum zio_zstd_levels level;
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

static enum zio_zstd_levels
zstd_cookie_to_enum(int32_t level)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(fastlevels); i++) {
		if (fastlevels[i].cookie == level)
			return (fastlevels[i].level);
	}

	/* This shouldn't happen. Cause a panic. */
	printk(KERN_ERR "%s:Invalid ZSTD level encountered: %d",
	    __func__, level);
	return (ZIO_ZSTD_LEVEL_DEFAULT);
}

static int32_t
zstd_enum_to_cookie(enum zio_zstd_levels elevel)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(fastlevels); i++) {
		if (fastlevels[i].level == elevel)
			return (fastlevels[i].cookie);
	}

	/* This shouldn't happen. Cause a panic. */
	printk(KERN_ERR
	    "%s:Invalid ZSTD enum level encountered: %d",
	    __func__, elevel);

	return (3);
}

size_t
zstd_compress(void *s_start, void *d_start, size_t s_len, size_t d_len, int n)
{
	size_t c_len;
	uint32_t bufsiz;
	int32_t levelcookie;
	char *dest = d_start;
	ZSTD_CCtx *cctx;

	levelcookie = zstd_enum_to_cookie(n);
	ASSERT3U(d_len, >=, sizeof (bufsiz));
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
	 * out of kernel memory, gently fall through - this will disable
	 * compression in zio_compress_data
	 */
	if (cctx == NULL) {
		return (s_len);
	}

	c_len = ZSTD_compressCCtx(cctx,
	    &dest[sizeof (bufsiz) + sizeof (levelcookie)],
	    d_len - sizeof (bufsiz) - sizeof (levelcookie),
	    s_start, s_len, levelcookie);

	ZSTD_freeCCtx(cctx);

	/* Error in the compression routine. disable compression. */
	if (ZSTD_isError(c_len)) {
		return (s_len);
	}

	/*
	 * Encode the compressed buffer size at the start. We'll need this in
	 * decompression to counter the effects of padding which might be
	 * added to the compressed buffer and which, if unhandled, would
	 * confuse the hell out of our decompression function.
	 */
	bufsiz = c_len;
	*(uint32_t *)dest = BE_32(bufsiz);
	/*
	 * Encode the compression level as well. We may need to know the
	 * original compression level if compressed_arc is disabled, to match
	 * the compression settings to write this block to the L2ARC.
	 * Encode the actual level, so if the enum changes in the future,
	 * we will be compatible.
	 */
	*(uint32_t *)(&dest[sizeof (bufsiz)]) = BE_32(levelcookie);

	return (c_len + sizeof (bufsiz) + sizeof (levelcookie));
}

int
zstd_get_level(void *s_start, size_t s_len, uint8_t *level)
{
	const char *src = s_start;
	uint32_t levelcookie = BE_IN32(&src[sizeof (levelcookie)]);
	uint8_t zstdlevel = zstd_cookie_to_enum(levelcookie);

	ASSERT3U(zstdlevel, !=, ZIO_ZSTDLVL_INHERIT);

	if (level != NULL) {
		*level = zstdlevel;
	}

	return (0);
}

int
zstd_decompress_level(void *s_start, void *d_start, size_t s_len, size_t d_len,
    uint8_t *level)
{
	const char *src = s_start;
	ZSTD_DCtx *dctx;
	size_t result;
	uint32_t bufsiz = BE_IN32(src);
	int32_t levelcookie = (int32_t)BE_IN32(&src[sizeof (bufsiz)]);
	uint8_t zstdlevel = zstd_cookie_to_enum(levelcookie);

	ASSERT3U(d_len, >=, s_len);
	ASSERT3U(zstdlevel, !=, ZIO_ZSTDLVL_INHERIT);

	/* invalid compressed buffer size encoded at start */
	if (bufsiz + sizeof (bufsiz) > s_len) {
		return (1);
	}

	dctx = ZSTD_createDCtx_advanced(zstd_dctx_malloc);
	if (dctx == NULL)
		return (1);

	result = ZSTD_decompressDCtx(dctx, d_start, d_len,
	    &src[sizeof (bufsiz) + sizeof (levelcookie)], bufsiz);

	ZSTD_freeDCtx(dctx);

	/*
	 * Returns 0 on success (decompression function returned non-negative)
	 * and non-zero on failure (decompression function returned negative.
	 */
	if (ZSTD_isError(result)) {
		return (1);
	}

	if (level != NULL) {
		*level = zstdlevel;
	}

	return (0);
}

int
zstd_decompress(void *s_start, void *d_start, size_t s_len, size_t d_len, int n)
{

	return (zstd_decompress_level(s_start, d_start, s_len, d_len, NULL));
}

static int zstd_meminit(void);

extern void *
zstd_alloc(void *opaque __unused, size_t size)
{
	size_t nbytes = sizeof (struct zstd_kmem) + size;
	struct zstd_kmem *z = NULL;

	z = (struct zstd_kmem *)zstd_mempool_alloc(zstd_mempool_cctx, nbytes);

	if (z == NULL) {
		return (NULL);
	}

	return ((void*)z + (sizeof (struct zstd_kmem)));
}

extern void *
zstd_dctx_alloc(void *opaque __unused, size_t size)
{
	size_t nbytes = sizeof (struct zstd_kmem) + size;
	struct zstd_kmem *z = NULL;
	enum zstd_kmem_type type = ZSTD_KMEM_DEFAULT;

	z = (struct zstd_kmem *)zstd_mempool_alloc(zstd_mempool_dctx, nbytes);
	if (!z) {
		/* try harder, decompression shall not fail */
		z = vmem_alloc(nbytes, KM_SLEEP);
		if (z)
			z->pool = NULL;
	} else {
		return ((void*)z + (sizeof (struct zstd_kmem)));
	}

	/* fallback if everything fails */
	if (z == NULL) {
		/* barrier since we only can handle it in a single thread */
		mutex_enter(&zstd_dctx_fallback.barrier);
		mutex_exit(&zstd_dctx_fallback.barrier);
		mutex_enter(&zstd_dctx_fallback.barrier);
		z = zstd_dctx_fallback.mem;
		type = ZSTD_KMEM_DCTX;
	}

	/* allocation should always be successful */
	if (z == NULL) {
		return (NULL);
	}

	z->kmem_type = type;
	z->kmem_size = nbytes;

	return ((void*)z + (sizeof (struct zstd_kmem)));
}


extern void
zstd_free(void *opaque __unused, void *ptr)
{
	struct zstd_kmem *z = ptr - sizeof (struct zstd_kmem);
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
#ifndef _KERNEL
#define	__init
#define	__exit
#endif

static void create_fallback_mem(struct zstd_fallback_mem *mem, size_t size)
{
	mem->mem_size = size;
	mem->mem = \
	    vmem_zalloc(mem->mem_size, \
	    KM_SLEEP);
	mutex_init(&mem->barrier, \
	    NULL, MUTEX_DEFAULT, NULL);
}
static int zstd_meminit(void)
{
	zstd_mempool_init();

	/* Estimate the size of the fallback decompression context */
	create_fallback_mem(&zstd_dctx_fallback,
	    P2ROUNDUP(ZSTD_estimateDCtxSize() +
	    sizeof (struct zstd_kmem), PAGESIZE));

	return (0);
}

extern int __init
zstd_init(void)
{
	/*
	 * set pool size by using maximum sane thread count * 4
	 */
	pool_count = boot_ncpus * 4;
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
EXPORT_SYMBOL(zstd_compress);
EXPORT_SYMBOL(zstd_decompress_level);
EXPORT_SYMBOL(zstd_decompress);
EXPORT_SYMBOL(zstd_get_level);

MODULE_DESCRIPTION("ZSTD Compression for ZFS");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION("1.4.4");
#endif