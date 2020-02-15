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
#include "zstdlib.h"

/* FreeBSD compatibility */
#if defined(__FreeBSD__) && defined(_KERNEL)
MALLOC_DEFINE(M_ZSTD, "zstd", "ZSTD Compressor");
#endif

/*
 * this specifies a incremental version of the zstd format
 * increase it on any new added zstd version.
 * this is made to ensure data integrity compatiblity since
 * new versions might enhance the compression algorithm
 * in a way the data itself will change.
 * the version will be stored as upper 16 bits, the level cookie
 * as lower 16 bits of the second 32 bit field of the data block
 */
#define	ZSTD_VERSION		1
#define	ZSTD_VERSION_SHIFT	16
#define	ZSTD_VERSION_MASK	0xffff0000
#define	ZSTD_LEVEL_SHIFT	0
#define	ZSTD_LEVEL_MASK		0x0000ffff

#define	SET_ZSTD_VERSION(bm, val) \
	bm = (bm & ~ZSTD_VERSION_MASK) | (val << ZSTD_VERSION_SHIFT)
#define	GET_ZSTD_VERSION(bm) (bm & ZSTD_VERSION_MASK) >> ZSTD_VERSION_SHIFT
#define	SET_ZSTD_LEVEL(bm, val) \
	bm = (bm & ~ZSTD_LEVEL_MASK) | (val << ZSTD_LEVEL_SHIFT)
#define	GET_ZSTD_LEVEL(bm) (bm & ZSTD_LEVEL_MASK) >> ZSTD_LEVEL_SHIFT

static void *zstd_alloc(void *opaque, size_t size);
static void *zstd_dctx_alloc(void *opaque, size_t size);
static void zstd_free(void *opaque, void *ptr);

/*
 * zstd memory handlers
 * for decompression we use a different handler which has the possiblity
 * of a fallback memory allocation in case memory was running out
 * We split up the zstd handlers for most simplified implementation
 */

/* compression memory handler */
static const ZSTD_customMem zstd_malloc = {
	zstd_alloc,
	zstd_free,
	NULL,
};

/* decompression memory handler */
static const ZSTD_customMem zstd_dctx_malloc = {
	zstd_dctx_alloc,
	zstd_free,
	NULL,
};

/*
 * these enums describe the allocator type specified by
 * kmem_type in struct zstd_kmem
 */
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
/*
 * this variable should represent the maximum count of the pool based on
 * the number of cpu's running in the system with some budged. by default
 * we use the cpu count * 4. see init_zstd
 */
static int pool_count = 16;

#define	ZSTD_POOL_MAX		pool_count
#define	ZSTD_POOL_TIMEOUT	60 * 2

struct zstd_kmem;

/* special structure for pooled memory objects */
struct zstd_pool {
	void *mem;
	size_t size;
	kmutex_t 		barrier;
	hrtime_t timeout;
};

/* global structure for handling memory allocations */
struct zstd_kmem {
	enum zstd_kmem_type	kmem_type;
	size_t			kmem_size;
	struct zstd_pool	*pool;
};

/*
 * special fallback memory structure used for decompression only
 * if memory is running out
 */
struct zstd_fallback_mem {
	size_t			mem_size;
	void			*mem;
	kmutex_t 		barrier;
};

static struct zstd_fallback_mem zstd_dctx_fallback;
static struct zstd_pool *zstd_mempool_cctx;
static struct zstd_pool *zstd_mempool_dctx;

/* initializes memory pool barrier mutexes */
static void
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
	mutex_destroy(&pool->barrier);
	kmem_free(pool->mem, pool->size);
	pool->mem = NULL;
	pool->size = 0;
}

/* releases memory pool objects */
static void
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
 * if necessary. if a object is older than 2 minutes and does not fit to the
 * requested size, it will be released and a new cached entry will be allocated
 * if other pooled objects are detected without being used for 2 minutes,
 * they will be released too.
 * the concept is that high frequency memory allocations of bigger objects are
 * expensive.
 * so if alot of work is going on, allocations will be kept for a while and can
 * be reused in that timeframe. the scheduled release will be updated every time
 * a object is reused.
 */
static void *
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
		/*
		 * this lock is simply a marker for a pool object beeing in use.
		 * if its already hold it will be skipped.
		 * we need to create it before checking to avoid race conditions
		 * since this is running in a threaded context.
		 * the lock is later released by zstd_mempool_free.
		 */
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
			 * and not in use.
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
	 * you may have noticed that we run a similar algorithm twice here.
	 * this is to avoid pool framentation. the first one may generate
	 * holes in the list if objects are released.
	 * we always make sure that these holes are filled instead of adding
	 * new allocations constantly at the end.
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
 * mark object as released by releasing the barrier mutex
 */
static void
zstd_mempool_free(struct zstd_kmem *z)
{
	mutex_exit(&z->pool->barrier);
}

struct levelmap {
	int16_t cookie;
	enum zio_zstd_levels level;
};

/* level map for converting zfs internal levels to zstd levels and vice versa */
static struct levelmap zstd_levels[] = {
	{ZIO_ZSTD_LEVEL_1, ZIO_ZSTD_LEVEL_1},
	{ZIO_ZSTD_LEVEL_2, ZIO_ZSTD_LEVEL_2},
	{ZIO_ZSTD_LEVEL_3, ZIO_ZSTD_LEVEL_3},
	{ZIO_ZSTD_LEVEL_4, ZIO_ZSTD_LEVEL_4},
	{ZIO_ZSTD_LEVEL_5, ZIO_ZSTD_LEVEL_5},
	{ZIO_ZSTD_LEVEL_6, ZIO_ZSTD_LEVEL_6},
	{ZIO_ZSTD_LEVEL_7, ZIO_ZSTD_LEVEL_7},
	{ZIO_ZSTD_LEVEL_8, ZIO_ZSTD_LEVEL_8},
	{ZIO_ZSTD_LEVEL_9, ZIO_ZSTD_LEVEL_9},
	{ZIO_ZSTD_LEVEL_10, ZIO_ZSTD_LEVEL_10},
	{ZIO_ZSTD_LEVEL_11, ZIO_ZSTD_LEVEL_11},
	{ZIO_ZSTD_LEVEL_12, ZIO_ZSTD_LEVEL_12},
	{ZIO_ZSTD_LEVEL_13, ZIO_ZSTD_LEVEL_13},
	{ZIO_ZSTD_LEVEL_14, ZIO_ZSTD_LEVEL_14},
	{ZIO_ZSTD_LEVEL_15, ZIO_ZSTD_LEVEL_15},
	{ZIO_ZSTD_LEVEL_16, ZIO_ZSTD_LEVEL_16},
	{ZIO_ZSTD_LEVEL_17, ZIO_ZSTD_LEVEL_17},
	{ZIO_ZSTD_LEVEL_18, ZIO_ZSTD_LEVEL_18},
	{ZIO_ZSTD_LEVEL_19, ZIO_ZSTD_LEVEL_19},
	{-1, ZIO_ZSTD_LEVEL_FAST_1},
	{-2, ZIO_ZSTD_LEVEL_FAST_2},
	{-3, ZIO_ZSTD_LEVEL_FAST_3},
	{-4, ZIO_ZSTD_LEVEL_FAST_4},
	{-5, ZIO_ZSTD_LEVEL_FAST_5},
	{-6, ZIO_ZSTD_LEVEL_FAST_6},
	{-7, ZIO_ZSTD_LEVEL_FAST_7},
	{-8, ZIO_ZSTD_LEVEL_FAST_8},
	{-9, ZIO_ZSTD_LEVEL_FAST_9},
	{-10, ZIO_ZSTD_LEVEL_FAST_10},
	{-20, ZIO_ZSTD_LEVEL_FAST_20},
	{-30, ZIO_ZSTD_LEVEL_FAST_30},
	{-40, ZIO_ZSTD_LEVEL_FAST_40},
	{-50, ZIO_ZSTD_LEVEL_FAST_50},
	{-60, ZIO_ZSTD_LEVEL_FAST_60},
	{-70, ZIO_ZSTD_LEVEL_FAST_70},
	{-80, ZIO_ZSTD_LEVEL_FAST_80},
	{-90, ZIO_ZSTD_LEVEL_FAST_90},
	{-100, ZIO_ZSTD_LEVEL_FAST_100},
	{-500, ZIO_ZSTD_LEVEL_FAST_500},
	{-1000, ZIO_ZSTD_LEVEL_FAST_1000},
};

/*
 * convert internal stored zfs level enum to zstd level
 */
static int
zstd_cookie_to_enum(int16_t cookie, uint8_t *level)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(zstd_levels); i++) {
		if (zstd_levels[i].cookie == cookie) {
			*level = zstd_levels[i].level;
			return (0);
		}
	}
	/* This shouldn't happen. */
	return (1);
}

/*
 * convert zstd_level to internal stored zfs level
 */
static int
zstd_enum_to_cookie(enum zio_zstd_levels elevel, int16_t *cookie)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(zstd_levels); i++) {
		if (zstd_levels[i].level == elevel) {
			*cookie = zstd_levels[i].cookie;
			return (0);
		}
	}
	/* This shouldn't happen, data is broken */
	return (1);
}

/*
 * compress block using zstd
 */
size_t
zstd_compress(void *s_start, void *d_start, size_t s_len, size_t d_len, int lvl)
{
	size_t c_len;
	uint32_t bufsiz;
	char *dest = d_start;
	ZSTD_CCtx *cctx;
	uint32_t levelbm = 0;
	int16_t levelcookie;
	if (zstd_enum_to_cookie(lvl, &levelcookie)) {
		/* level is invalid, ignore compression */
		return (s_len);
	}
	ASSERT3U(d_len, >=, sizeof (bufsiz));
	ASSERT3U(d_len, <=, s_len);
	ASSERT3U(levelcookie, !=, 0);


	cctx = ZSTD_createCCtx_advanced(zstd_malloc);
	/*
	 * out of kernel memory, gently fall through - this will disable
	 * compression in zio_compress_data
	 */
	if (cctx == NULL) {
		return (s_len);
	}

	/* set compression level */

	ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, levelcookie);

	/* Use the "magicless" zstd header which saves us 4 header bytes */
	ZSTD_CCtx_setParameter(cctx, ZSTD_c_format, ZSTD_f_zstd1_magicless);

	/* disable checksum calculation */
	ZSTD_CCtx_setParameter(cctx, ZSTD_c_checksumFlag, 0);

	/* disable store of content size since its redundant */
	ZSTD_CCtx_setParameter(cctx, ZSTD_c_contentSizeFlag, 0);

	c_len = ZSTD_compress2(cctx,
	    &dest[sizeof (bufsiz) + sizeof (levelbm)],
	    d_len - sizeof (bufsiz) - sizeof (levelbm),
	    s_start, s_len);

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
	 * the upper 16 bits will be the zstd format version to ensure
	 * compatibility with future zstd enhancemnts which may change
	 * the compressed data.
	 */
	SET_ZSTD_LEVEL(levelbm, levelcookie);
	SET_ZSTD_VERSION(levelbm, ZSTD_VERSION);
	*(uint32_t *)(&dest[sizeof (bufsiz)]) = BE_32(levelbm);

	return (c_len + sizeof (bufsiz) + sizeof (levelbm));
}

/*
 * decompress block using zstd and return its stored level
 */
int
zstd_decompress_level(void *s_start, void *d_start, size_t s_len, size_t d_len,
    uint8_t *level)
{
	const char *src = s_start;
	ZSTD_DCtx *dctx;
	size_t result;
	uint32_t bufsiz = BE_IN32(src);
	uint32_t levelbm = BE_IN32(&src[sizeof (bufsiz)]);
	uint8_t zstdlevel;
	if (zstd_cookie_to_enum(GET_ZSTD_LEVEL(levelbm), &zstdlevel)) {
		/* data is broken. return error */
		return (1);
	}

	ASSERT3U(d_len, >=, s_len);
	ASSERT3U(zstdlevel, !=, ZIO_ZSTD_LEVEL_INHERIT);

	/* invalid compressed buffer size encoded at start */
	if (bufsiz + sizeof (bufsiz) > s_len) {
		return (1);
	}

	dctx = ZSTD_createDCtx_advanced(zstd_dctx_malloc);
	if (dctx == NULL)
		return (1);

	if (GET_ZSTD_VERSION(levelbm)) {
		/* Set header type to "magicless" */
		ZSTD_DCtx_setParameter(dctx, ZSTD_d_format,
		    ZSTD_f_zstd1_magicless);
	}

	result = ZSTD_decompressDCtx(dctx, d_start, d_len,
	    &src[sizeof (bufsiz) + sizeof (levelbm)], bufsiz);

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

/*
 * decompress datablock using zstd
 */
int
zstd_decompress(void *s_start, void *d_start, size_t s_len, size_t d_len, int n)
{

	return (zstd_decompress_level(s_start, d_start, s_len, d_len, NULL));
}


/*
 * allocator for zstd compression context using mempool_allocator
 */
extern void *
zstd_alloc(void *opaque __maybe_unused, size_t size)
{
	size_t nbytes = sizeof (struct zstd_kmem) + size;
	struct zstd_kmem *z = NULL;

	z = (struct zstd_kmem *)zstd_mempool_alloc(zstd_mempool_cctx, nbytes);

	if (z == NULL) {
		return (NULL);
	}

	return ((void*)z + (sizeof (struct zstd_kmem)));
}

/*
 * allocator for zstd decompression context, uses mempool_allocator but
 * fallback to reserved memory if allocation fails
 */
extern void *
zstd_dctx_alloc(void *opaque __maybe_unused, size_t size)
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
		/*
		 * barrier since we only can handle it in a single thread
		 * all other following threads need to wait here until
		 * decompression is complete.
		 * zstd_free will release this barrier later.
		 */
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

/*
 * free allocated memory by its specific type
 */
extern void
zstd_free(void *opaque __maybe_unused, void *ptr)
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

/*
 * allocate fallback memory to ensure safe decompression
 */
static void create_fallback_mem(struct zstd_fallback_mem *mem, size_t size)
{
	mem->mem_size = size;
	mem->mem = \
	    vmem_zalloc(mem->mem_size, \
	    KM_SLEEP);
	mutex_init(&mem->barrier, \
	    NULL, MUTEX_DEFAULT, NULL);
}

/*
 * initializes zstd related memory handling
 */
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

ZFS_MODULE_DESCRIPTION("ZSTD Compression for ZFS");
ZFS_MODULE_LICENSE("Dual BSD/GPL");
ZFS_MODULE_VERSION("1.4.4");
#endif
