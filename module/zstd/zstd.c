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


/* For BSD compatibility */
#define	__unused			__attribute__((unused))

/* For userspace we disable error debugging */
#if !defined(_KERNEL)
#define	printk(fmt, ...)
#endif

size_t real_zstd_compress(const char *source, char *dest, int isize,
    int osize, int level);
size_t real_zstd_decompress(const char *source, char *dest, int isize,
    int maxosize);

void *zstd_alloc(void *opaque, size_t size);
void *zstd_dctx_alloc(void *opaque, size_t size);
void zstd_free(void *opaque, void *ptr);

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
/* These enums are index references to zstd_cache_config */
enum zstd_kmem_type {
	ZSTD_KMEM_UNKNOWN = 0,
	ZSTD_KMEM_DCTX,
	ZSTD_KMEM_COUNT,
};

/*
 * This variable represents the maximum count of the pool based on the number
 * of CPUs plus some buffer. We default to cpu count * 4, see init_zstd.
 */
static int pool_count = 16;

#define	ZSTD_POOL_MAX		pool_count
#define	ZSTD_POOL_TIMEOUT	60 * 2

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

/* Initialize memory pool barrier mutexes */
void
zstd_mempool_init(void)
{
	int i;

	zstd_mempool_cctx = (struct zstd_pool *)
	    kmem_zalloc(ZSTD_POOL_MAX * sizeof (struct zstd_pool), KM_SLEEP);
	zstd_mempool_dctx = (struct zstd_pool *)
	    kmem_zalloc(ZSTD_POOL_MAX * sizeof (struct zstd_pool), KM_SLEEP);

	for (i = 0; i < ZSTD_POOL_MAX; i++) {
		mutex_init(&zstd_mempool_cctx[i].barrier,
		    NULL, MUTEX_DEFAULT, NULL);
		mutex_init(&zstd_mempool_dctx[i].barrier,
		    NULL, MUTEX_DEFAULT, NULL);
	}
}

/* Release object from pool and free memory */
void
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
void
zstd_mempool_deinit(void)
{
	int i;

	for (i = 0; i < ZSTD_POOL_MAX; i++) {
		release_pool(&zstd_mempool_cctx[i]);
		release_pool(&zstd_mempool_dctx[i]);
	}

	kmem_free(zstd_mempool_dctx,
	    ZSTD_POOL_MAX * sizeof (struct zstd_pool));
	kmem_free(zstd_mempool_cctx,
	    ZSTD_POOL_MAX * sizeof (struct zstd_pool));

	zstd_mempool_dctx = NULL;
	zstd_mempool_cctx = NULL;
}

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
void *
zstd_mempool_alloc(struct zstd_pool *zstd_mempool, size_t size)
{
	int i;
	struct zstd_pool *pool;
	void *mem = NULL;

	/* Seek for preallocated memory slot and free obsolete slots */
	for (i = 0; i < ZSTD_POOL_MAX; i++) {
		pool = &zstd_mempool[i];
		if (mutex_tryenter(&pool->barrier)) {
			/*
			 * Check if objects fits the size, if so we take  it and
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

	/* If no preallocated slot was found, try to fill in a new one */
	if (!mem) {
		for (i = 0; i < ZSTD_POOL_MAX; i++) {
			pool = &zstd_mempool[i];

			if (mutex_tryenter(&pool->barrier)) {
				/* Object is free, try to allocate new one */
				if (!pool->mem) {
					struct zstd_kmem *z = vmem_alloc(size, KM_SLEEP);
					pool->mem = z;

					/* allocation successfull? */
					if (pool->mem) {
						/*
						 * keep track for later release and
						 * update timestamp
						 */
						z->pool = pool;
						pool->size = size;
						pool->timeout = gethrestime_sec() +
						    ZSTD_POOL_TIMEOUT;
					} else {
						mutex_exit(&pool->barrier);
					}

					return (z);
				}

				mutex_exit(&pool->barrier);
			}
		}
	}

	/*
	 * If the pool is full or the allocation failed, try lazy
	 * allocation instead.
	 */
	return (mem ? mem : vmem_alloc(size, KM_NOSLEEP));
}

/*
 * mark object as released by releasing the barrier mutex and clear the buffer
 */
void
zstd_mempool_free(struct zstd_kmem *z)
{
	mutex_exit(&z->pool->barrier);
}

enum zio_zstd_levels
zstd_cookie_to_enum(int32_t level)
{
	if (level > 0 && level <= ZIO_ZSTDLVL_MAX) {
		return ((enum zio_zstd_levels)level);
	}

	if (level < 0) {
		switch (level) {
			case -1:
				return (ZIO_ZSTDLVL_FAST_1);
			case -2:
				return (ZIO_ZSTDLVL_FAST_2);
			case -3:
				return (ZIO_ZSTDLVL_FAST_3);
			case -4:
				return (ZIO_ZSTDLVL_FAST_4);
			case -5:
				return (ZIO_ZSTDLVL_FAST_5);
			case -6:
				return (ZIO_ZSTDLVL_FAST_6);
			case -7:
				return (ZIO_ZSTDLVL_FAST_7);
			case -8:
				return (ZIO_ZSTDLVL_FAST_8);
			case -9:
				return (ZIO_ZSTDLVL_FAST_9);
			case -10:
				return (ZIO_ZSTDLVL_FAST_10);
			case -20:
				return (ZIO_ZSTDLVL_FAST_20);
			case -30:
				return (ZIO_ZSTDLVL_FAST_30);
			case -40:
				return (ZIO_ZSTDLVL_FAST_40);
			case -50:
				return (ZIO_ZSTDLVL_FAST_50);
			case -60:
				return (ZIO_ZSTDLVL_FAST_60);
			case -70:
				return (ZIO_ZSTDLVL_FAST_70);
			case -80:
				return (ZIO_ZSTDLVL_FAST_80);
			case -90:
				return (ZIO_ZSTDLVL_FAST_90);
			case -100:
				return (ZIO_ZSTDLVL_FAST_100);
			case -500:
				return (ZIO_ZSTDLVL_FAST_500);
			case -1000:
				return (ZIO_ZSTDLVL_FAST_1000);
			default:
				break;
		}
	}

	/* This shouldn't happen. Cause a panic. */
	printk(KERN_ERR "%s:Invalid ZSTD level encountered: %d",
	    __func__, level);
	return (ZIO_ZSTD_LEVEL_DEFAULT);
}

int32_t
zstd_enum_to_cookie(enum zio_zstd_levels elevel)
{
	if (elevel > ZIO_ZSTDLVL_INHERIT && elevel <= ZIO_ZSTDLVL_MAX) {
		return ((int32_t)elevel);
	}

	if (elevel > ZIO_ZSTDLVL_FAST && elevel <= ZIO_ZSTDLVL_FAST_MAX) {
		switch (elevel) {
			case ZIO_ZSTDLVL_FAST_1:
				return (-1);
			case ZIO_ZSTDLVL_FAST_2:
				return (-2);
			case ZIO_ZSTDLVL_FAST_3:
				return (-3);
			case ZIO_ZSTDLVL_FAST_4:
				return (-4);
			case ZIO_ZSTDLVL_FAST_5:
				return (-5);
			case ZIO_ZSTDLVL_FAST_6:
				return (-6);
			case ZIO_ZSTDLVL_FAST_7:
				return (-7);
			case ZIO_ZSTDLVL_FAST_8:
				return (-8);
			case ZIO_ZSTDLVL_FAST_9:
				return (-9);
			case ZIO_ZSTDLVL_FAST_10:
				return (-10);
			case ZIO_ZSTDLVL_FAST_20:
				return (-20);
			case ZIO_ZSTDLVL_FAST_30:
				return (-30);
			case ZIO_ZSTDLVL_FAST_40:
				return (-40);
			case ZIO_ZSTDLVL_FAST_50:
				return (-50);
			case ZIO_ZSTDLVL_FAST_60:
				return (-60);
			case ZIO_ZSTDLVL_FAST_70:
				return (-70);
			case ZIO_ZSTDLVL_FAST_80:
				return (-80);
			case ZIO_ZSTDLVL_FAST_90:
				return (-90);
			case ZIO_ZSTDLVL_FAST_100:
				return (-100);
			case ZIO_ZSTDLVL_FAST_500:
				return (-500);
			case ZIO_ZSTDLVL_FAST_1000:
				return (-1000);
			default:
				break;
		}
	}

	/* This shouldn't happen. Cause a panic. */
	printk(KERN_ERR "%s:Invalid ZSTD enum level encountered: %d",
	    __func__, elevel);
	return (3);
}

size_t
zstd_compress(void *s_start, void *d_start, size_t s_len, size_t d_len, int n)
{
	size_t c_len;
	uint32_t bufsize;
	int32_t levelcookie;
	char *dest = d_start;

	ASSERT3U(d_len, >=, sizeof (bufsize));
	ASSERT3U(d_len, <=, s_len);

	levelcookie = zstd_enum_to_cookie(n);

	/* This could overflow, but we never have blocks that big */
	c_len = real_zstd_compress(s_start,
	    &dest[sizeof (bufsize) + sizeof (levelcookie)], s_len,
	    d_len - sizeof (bufsize) - sizeof (levelcookie),
	    levelcookie);

	/* Signal an error if the compression routine failed */
	if (ZSTD_isError(c_len)) {
		return (s_len);
	}

	/*
	 * Encode the compresed buffer size at the start. We'll need this in
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
	uint32_t bufsize = BE_IN32(src);
	int32_t levelcookie = (int32_t)BE_IN32(&src[sizeof (bufsize)]);
	uint8_t zstdlevel = zstd_cookie_to_enum(levelcookie);

	ASSERT3U(d_len, >=, s_len);
	ASSERT3U(zstdlevel, !=, ZIO_ZSTDLVL_INHERIT);

	/* Invalid compressed buffer size encoded at start */
	if (bufsize + sizeof (bufsize) > s_len) {
		return (1);
	}

	/*
	 * Returns 0 on success (decompression function returned non-negative)
	 * and non-zero on failure (decompression function returned negative.
	 */
	if (ZSTD_isError(real_zstd_decompress(
	    &src[sizeof (bufsize) + sizeof (levelcookie)],
	    d_start, bufsize, d_len))) {
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

size_t
real_zstd_compress(const char *source, char *dest, int isize, int osize,
    int level)
{
	size_t result;
	ZSTD_CCtx *cctx;

	ASSERT3U(level, !=, 0);
	if (level == ZIO_COMPLEVEL_DEFAULT) {
		level = ZIO_ZSTD_LEVEL_DEFAULT;
	}
	if (level == ZIO_ZSTDLVL_DEFAULT) {
		level = ZIO_ZSTD_LEVEL_DEFAULT;
	}

	cctx = ZSTD_createCCtx_advanced(zstd_malloc);

	/*
	 * Out of kernel memory; gently fall through - this will disable
	 * compression in zio_compress_data.
	 */
	if (cctx == NULL) {
		return (0);
	}

	result = ZSTD_compressCCtx(cctx, dest, osize, source, isize, level);

	ZSTD_freeCCtx(cctx);
	return (result);
}

size_t
real_zstd_decompress(const char *source, char *dest, int isize, int maxosize)
{
	size_t result;
	ZSTD_DCtx *dctx;

	dctx = ZSTD_createDCtx_advanced(zstd_dctx_malloc);
	if (dctx == NULL) {
		return (ZSTD_error_memory_allocation);
	}

	result = ZSTD_decompressDCtx(dctx, dest, maxosize, source, isize);

	ZSTD_freeDCtx(dctx);

	return (result);
}

int zstd_meminit(void);

extern void *
zstd_alloc(void *opaque __unused, size_t size)
{
	size_t nbytes = sizeof (struct zstd_kmem) + size;
	struct zstd_kmem *z = NULL;

	z = (struct zstd_kmem *)zstd_mempool_alloc(zstd_mempool_cctx, nbytes);

	if (z == NULL) {
		return (NULL);
	}

	z->kmem_type = ZSTD_KMEM_UNKNOWN;
	z->kmem_size = nbytes;

	return ((void*)z + (sizeof (struct zstd_kmem)));
}

extern void *
zstd_dctx_alloc(void *opaque __unused, size_t size)
{
	size_t nbytes = sizeof (struct zstd_kmem) + size;
	struct zstd_kmem *z = NULL;
	enum zstd_kmem_type type = ZSTD_KMEM_UNKNOWN;

	z = (struct zstd_kmem *)
	    zstd_mempool_alloc(zstd_mempool_dctx, nbytes);
	if (!z) {
		/* Try harder, decompression shall not fail */
		z = vmem_alloc(nbytes, KM_SLEEP);
		z->pool = NULL;
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

	/* allocation should always be successful */
	if (!z) {
		return (NULL);
	}

	z->kmem_type = type;
	z->kmem_size = nbytes;

	return ((void*)z + (sizeof (struct zstd_kmem)));
}

extern void
zstd_free(void *opaque __unused, void *ptr)
{
	struct zstd_kmem *z = (ptr - sizeof (struct zstd_kmem));

	ASSERT3U(z->kmem_type, <, ZSTD_KMEM_COUNT);
	ASSERT3U(z->kmem_type, >=, ZSTD_KMEM_UNKNOWN);

	if (((enum zstd_kmem_type)z->kmem_type) == ZSTD_KMEM_UNKNOWN) {
		if (z->pool) {
			zstd_mempool_free(z);
		} else {
			kmem_free(z, z->kmem_size);
		}
	} else {
		mutex_exit(&zstd_dctx_fallback.barrier);
	}
}

/* User space / kernel emulation compatibility */
#if !defined(_KERNEL)
#define	__init
#define	__exit
#endif

void __init
create_fallback_mem(struct zstd_fallback_mem *mem, size_t size)
{
	mem->mem_size = size;
	mem->mem = vmem_zalloc(mem->mem_size, KM_SLEEP);
	mutex_init(&mem->barrier, NULL, MUTEX_DEFAULT, NULL);
}

int __init
zstd_meminit(void)
{
	zstd_mempool_init();

	/* Estimate the size of the fallback decompression context */
	create_fallback_mem(&zstd_dctx_fallback,
	    P2ROUNDUP(ZSTD_estimateDCtxSize() + sizeof (struct zstd_kmem),
	    PAGESIZE));

	return (0);
}


extern int __init
zstd_init(void)
{
	/* Set pool size by using maximum sane thread count * 4 */
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

ZFS_MODULE_DESCRIPTION("ZSTD Compression for ZFS");
ZFS_MODULE_LICENSE("Dual BSD/GPL");
ZFS_MODULE_VERSION("1.4.4");

EXPORT_SYMBOL(zstd_compress);
EXPORT_SYMBOL(zstd_decompress_level);
EXPORT_SYMBOL(zstd_decompress);
EXPORT_SYMBOL(zstd_get_level);
#endif
