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
 * Copyright (c) 2018-2020, Sebastian Gottschall. All rights reserved.
 * Copyright (c) 2019-2020, Michael Niewöhner. All rights reserved.
 */

#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/zfs_context.h>
#include <sys/zio_compress.h>
#include <sys/spa.h>
#include <sys/zstd/zstd.h>

#define	ZSTD_STATIC_LINKING_ONLY
#include "zstdlib.h"

/* Enums describing the allocator type specified by kmem_type in zstd_kmem */
enum zstd_kmem_type {
	ZSTD_KMEM_UNKNOWN = 0,
	/* Allocation type using kmem_vmalloc */
	ZSTD_KMEM_DEFAULT,
	/* Pool based allocation using mempool_alloc */
	ZSTD_KMEM_POOL,
	/* Reserved fallback memory for decompression only */
	ZSTD_KMEM_DCTX,
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

struct levelmap {
	int16_t cookie;
	enum zio_zstd_levels level;
};

/*
 * ZSTD memory handlers
 *
 * For decompression we use a different handler which also provides fallback
 * memory allocation in case memory runs out.
 *
 * The ZSTD handlers were split up for the most simplified implementation.
 */
static void *zstd_alloc(void *opaque, size_t size);
static void *zstd_dctx_alloc(void *opaque, size_t size);
static void zstd_free(void *opaque, void *ptr);

/* Compression memory handler */
static const ZSTD_customMem zstd_malloc = {
	zstd_alloc,
	zstd_free,
	NULL,
};

/* Decompression memory handler */
static const ZSTD_customMem zstd_dctx_malloc = {
	zstd_dctx_alloc,
	zstd_free,
	NULL,
};

/* Level map for converting ZFS internal levels to ZSTD levels and vice versa */
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
			if (!mem && pool->mem && size <= pool->size) {
				pool->timeout = gethrestime_sec() +
				    ZSTD_POOL_TIMEOUT;
				mem = pool->mem;
				continue;
			}

			/* Free memory if unused object older than 2 minutes */
			if (pool->mem && gethrestime_sec() > pool->timeout) {
				vmem_free(pool->mem, pool->size);
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
				pool->mem = mem;

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

/* Convert ZSTD level to ZFS internal, stored level */
static int
zstd_enum_to_cookie(enum zio_zstd_levels level, int16_t *cookie)
{
	if (level > 0 && level <= ZIO_ZSTD_LEVEL_19) {
		*cookie = zstd_levels[level - 1].cookie;
		return (0);
	}
	if (level >= ZIO_ZSTD_LEVEL_FAST_1 &&
	    level <= ZIO_ZSTD_LEVEL_FAST_1000) {
		*cookie = zstd_levels[level - ZIO_ZSTD_LEVEL_FAST_1
		    + ZIO_ZSTD_LEVEL_19].cookie;
		return (0);
	}

	/* Invalid/unknown ZSTD level - this should never happen. */
	return (1);
}
typedef struct {
	uint32_t version:24;
	uint8_t level;
} version_level_t;

/* NOTE: all fields in this header are in big endian order */
struct zstd_header {
	/* contains compressed size */
	uint32_t size;
	/*
	 * contains the version and level
	 * we have to choose a union here to handle
	 * endian conversation since the version and level
	 * is bitmask encoded.
	 */
	union {
		uint32_t version_data;
		version_level_t version_level;
	};
	char data[];
};

/* Compress block using zstd */
size_t
zstd_compress(void *s_start, void *d_start, size_t s_len, size_t d_len,
    int level)
{
	size_t c_len;
	int16_t levelcookie;
	ZSTD_CCtx *cctx;
	struct zstd_header *hdr = (struct zstd_header *)d_start;
	/* Skip compression if the specified level is invalid */

	if (zstd_enum_to_cookie(level, &levelcookie)) {
		return (s_len);
	}

	ASSERT3U(d_len, >=, sizeof (*hdr));
	ASSERT3U(d_len, <=, s_len);
	ASSERT3U(levelcookie, !=, 0);

	cctx = ZSTD_createCCtx_advanced(zstd_malloc);

	/*
	 * Out of kernel memory, gently fall through - this will disable
	 * compression in zio_compress_data
	 */
	if (!cctx) {
		return (s_len);
	}

	/* Set the compression level */
	ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, levelcookie);

	/* Use the "magicless" zstd header which saves us 4 header bytes */
	ZSTD_CCtx_setParameter(cctx, ZSTD_c_format, ZSTD_f_zstd1_magicless);

	/*
	 * Disable redundant checksum calculation and content size storage since
	 * this is already done by ZFS itself.
	 */
	ZSTD_CCtx_setParameter(cctx, ZSTD_c_checksumFlag, 0);
	ZSTD_CCtx_setParameter(cctx, ZSTD_c_contentSizeFlag, 0);

	c_len = ZSTD_compress2(cctx,
	    hdr->data,
	    d_len - sizeof (*hdr),
	    s_start, s_len);

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
	hdr->size = BE_32(c_len);

	/*
	 * Check version for overflow.
	 * The limit of 24 bits must not be exceeded. This allows a maximum
	 * version 1677.72.15 which we don't expect to be ever reached.
	 */
	ASSERT3U(ZSTD_VERSION_NUMBER, <=, 0xFFFFFF);

	/*
	 * Encode the compression level as well. We may need to know the
	 * original compression level if compressed_arc is disabled, to match
	 * the compression settings to write this block to the L2ARC.
	 *
	 * Encode the actual level, so if the enum changes in the future, we
	 * will be compatible.
	 *
	 * The upper 24 bits store the ZSTD version to be able to provide
	 * future compatibility, since new versions might enhance the
	 * compression algorithm in a way, where the compressed data will
	 * change.
	 *
	 * As soon as such incompatibility occurs, handling code needs to be
	 * added, differentiating between the versions.
	 */
	hdr->version_level.level = level;
	hdr->version_level.version = ZSTD_VERSION_NUMBER;
	hdr->version_data = BE_32(hdr->version_data);

	return (c_len + sizeof (*hdr));
}

/* Decompress block using zstd and return its stored level */
int
zstd_decompress_level(void *s_start, void *d_start, size_t s_len, size_t d_len,
    uint8_t *level)
{
	ZSTD_DCtx *dctx;
	size_t result;
	enum zio_zstd_levels zstdlevel;
	int16_t levelcookie;
	uint32_t version;
	uint32_t bufsize;
	const struct zstd_header *hdr = (const struct zstd_header *)s_start;
	struct zstd_header hdr_copy;
	hdr_copy.version_data = BE_32(hdr->version_data);
	/* Read buffer size */
	bufsize = BE_32(hdr->size);

	/* Read the level */
	zstdlevel = hdr_copy.version_level.level;

	/*
	 * We ignore the ZSTD version for now. As soon as incompatibilities
	 * occurr, it has to be read and handled accordingly.
	 */
	version = hdr_copy.version_level.version;

	/*
	 * Convert and check the level
	 * An invalid level is a strong indicator for data corruption! In such
	 * case return an error so the upper layers can try to fix it.
	 */
	if (version >= 10405 && zstd_enum_to_cookie(zstdlevel, &levelcookie)) {
		return (1);
	}
	if (version < 10405) {
		zstdlevel = version;
	}

	ASSERT3U(d_len, >=, s_len);
	ASSERT3U(zstdlevel, !=, ZIO_COMPLEVEL_INHERIT);

	/* Invalid compressed buffer size encoded at start */
	if (bufsize + sizeof (*hdr) > s_len) {
		return (1);
	}

	dctx = ZSTD_createDCtx_advanced(zstd_dctx_malloc);
	if (!dctx) {
		return (1);
	}

	/*
	 * special case for supporting older development versions
	 * which did contain the magic
	 */
	if (version >= 10405) {
		/* Set header type to "magicless" */
		ZSTD_DCtx_setParameter(dctx, ZSTD_d_format,
		    ZSTD_f_zstd1_magicless);
	}

	result = ZSTD_decompressDCtx(dctx, d_start, d_len,
	    hdr->data, bufsize);

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
		/*
		 * Barrier since we only can handle it in a single thread. All
		 * other following threads need to wait here until decompression
		 * is completed. zstd_free will release this barrier later.
		 */
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
		vmem_free(z, z->kmem_size);
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
	vmem_free(zstd_dctx_fallback.mem, zstd_dctx_fallback.mem_size);
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
