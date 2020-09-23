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
#include <sys/zfs_context.h>
#include <sys/zio.h>
#include <sys/zio_compress.h>
#include <sys/spa.h>
#include <sys/zstd/zstd.h>
#include <sys/zstd/zstd_impl.h>

#define	ZSTD_STATIC_LINKING_ONLY
#include "lib/zstd.h"

extern kmem_cache_t	*zio_data_buf_cache[];

/* Enums describing which kmem_cache memory was allocated from */
enum zstd_kmem_type {
	ZSTD_KMEM_UNKNOWN = 0,
	ZSTD_KMEM_CCTX_32,
	ZSTD_KMEM_CCTX_64,
	ZSTD_KMEM_CCTX_MAX,
	ZSTD_KMEM_DCTX,
	ZSTD_KMEM_FALLBACK_DCTX,
	ZSTD_KMEM_ZIO,
	ZSTD_KMEM_COUNT,
};

/* Structure for tracking memory allocation type */
struct zstd_kmem {
	enum zstd_kmem_type kmem_type;
	size_t kmem_size;
};

/* Fallback memory structure used for decompression only if memory runs out */
struct zstd_fallback_mem {
	size_t mem_size;
	void *mem;
	kmutex_t barrier;
};

static struct zstd_fallback_mem	zstd_dctx_fallback;
static kmem_cache_t		*zstd_cctx_cache_32 = NULL;
static kmem_cache_t		*zstd_cctx_cache_64 = NULL;
static kmem_cache_t		*zstd_cctx_cache_max = NULL;
static kmem_cache_t		*zstd_dctx_cache = NULL;
size_t				zstd_cctx_size_32 = 32*1024*1024;
size_t				zstd_cctx_size_64 = 64*1024*1024;
size_t				zstd_cctx_size_max = 0;
size_t				zstd_dctx_size = 0;

/* Allocator for zstd compression context using kmem caches */
void *
zstd_alloc(void *opaque __maybe_unused, size_t size)
{
	size_t nbytes = sizeof (struct zstd_kmem) + size;
	struct zstd_kmem *z = NULL;
	enum zstd_kmem_type type = ZSTD_KMEM_UNKNOWN;

	if (nbytes <= SPA_MAXBLOCKSIZE) {
		/* zio_data_buf_alloc() may sleep, we don't want that */
		size_t c = (nbytes - 1) >> SPA_MINBLOCKSHIFT;

		VERIFY3U(c, <, SPA_MAXBLOCKSIZE >> SPA_MINBLOCKSHIFT);
		z = kmem_cache_alloc(zio_data_buf_cache[c], KM_NOSLEEP);
		type = ZSTD_KMEM_ZIO;
	} else if (nbytes <= zstd_cctx_size_32) {
		z = kmem_cache_alloc(zstd_cctx_cache_32, KM_NOSLEEP);
		type = ZSTD_KMEM_CCTX_32;
	} else if (nbytes <= zstd_cctx_size_64) {
		z = kmem_cache_alloc(zstd_cctx_cache_64, KM_NOSLEEP);
		type = ZSTD_KMEM_CCTX_64;
	} else if (nbytes <= zstd_cctx_size_max) {
		z = kmem_cache_alloc(zstd_cctx_cache_max, KM_NOSLEEP);
		type = ZSTD_KMEM_CCTX_MAX;
	} else {
		/* Too large for zio_data_buf_cache */
		z = kmem_alloc(nbytes, KM_NOSLEEP);
		if (z) {
			ZSTDSTAT_ADD(zstd_stat_buffers, 1);
			ZSTDSTAT_ADD(zstd_stat_size, nbytes);
		}
	}
	/* If kmem_cache fails, try kmem_alloc */
	if (z == NULL) {
		ZSTDSTAT_BUMP(zstd_stat_alloc_fail);
		z = kmem_alloc(nbytes, KM_NOSLEEP);
		if (z) {
			ZSTDSTAT_ADD(zstd_stat_buffers, 1);
			ZSTDSTAT_ADD(zstd_stat_size, nbytes);
		}
	}
	if (z == NULL) {
		return (NULL);
	}
	z->kmem_type = type;
	z->kmem_size = nbytes;

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
	enum zstd_kmem_type type = ZSTD_KMEM_UNKNOWN;

	if (P2ROUNDUP(nbytes, PAGESIZE) == zstd_dctx_size) {
		type = ZSTD_KMEM_DCTX;
		z = kmem_cache_alloc(zstd_dctx_cache, KM_NOSLEEP);
	} else {
		z = zstd_alloc(NULL, size);
		if (z != NULL) {
			z = (void*)z - (sizeof (struct zstd_kmem));
			type = z->kmem_type;
		}
	}
	if (z == NULL) {
		/* Try harder, decompression shall not fail */
		z = kmem_alloc(nbytes, KM_NOSLEEP);
		ZSTDSTAT_BUMP(zstd_stat_alloc_fail);
	}

	/* Fallback if everything fails */
	if (z == NULL) {
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
	if (z == NULL) {
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

	ASSERT3U(z->kmem_type, <, ZSTD_KMEM_COUNT);
	ASSERT3U(z->kmem_type, >=, ZSTD_KMEM_UNKNOWN);

	switch (z->kmem_type) {
	case ZSTD_KMEM_UNKNOWN:
		ZSTDSTAT_SUB(zstd_stat_buffers, 1);
		ZSTDSTAT_SUB(zstd_stat_size, z->kmem_size);
		kmem_free(z, z->kmem_size);
		break;
	case ZSTD_KMEM_CCTX_32:
		kmem_cache_free(zstd_cctx_cache_32, z);
		break;
	case ZSTD_KMEM_CCTX_64:
		kmem_cache_free(zstd_cctx_cache_64, z);
		break;
	case ZSTD_KMEM_CCTX_MAX:
		kmem_cache_free(zstd_cctx_cache_max, z);
		break;
	case ZSTD_KMEM_DCTX:
		kmem_cache_free(zstd_dctx_cache, z);
		break;
	case ZSTD_KMEM_FALLBACK_DCTX:
		VERIFY3P(z, ==, zstd_dctx_fallback.mem);
		mutex_exit(&zstd_dctx_fallback.barrier);
		break;
	case ZSTD_KMEM_ZIO:
		zio_data_buf_free(z, z->kmem_size);
		break;
	default:
		panic("Attempting to free invalid zstd memory cache");
		break;
	}
}

/* Create zstd-related kmem caches */
static int
zstd_meminit(void)
{
	/*
	 * Create kmem_caches for large compression context workspaces.
	 * The zio_data_buf_cache only goes up to 16 MB, so we create a 32 and
	 * 64 MB kmem_cache, and one for the largest possible compression
	 * context. These will only be used when the ZSTD workspace is larger
	 * then the largest zio_data_buf_cache.
	 */
	zstd_cctx_cache_32 = kmem_cache_create("zfs_zstd_cctx_32",
	    zstd_cctx_size_32, 0, NULL, NULL, NULL, NULL, NULL, 0);
	zstd_cctx_cache_64 = kmem_cache_create("zfs_zstd_cctx_64",
	    zstd_cctx_size_64, 0, NULL, NULL, NULL, NULL, NULL, 0);
	/*
	 * Calculate the maximum memory required for the largest block size to
	 * be compressed at the highest compression level.
	 */
	zstd_cctx_size_max = P2ROUNDUP(ZSTD_estimateCCtxSize_usingCParams(
	    ZSTD_getCParams(ZIO_ZSTD_LEVEL_MAX, SPA_MAXBLOCKSIZE, 0)) +
	    sizeof (struct zstd_kmem), PAGESIZE);
	zstd_cctx_cache_max = kmem_cache_create("zfs_zstd_cctx_max",
	    zstd_cctx_size_max, 0, NULL, NULL, NULL, NULL, NULL, 0);

	/*
	 * Estimate the size of the decompression context, and create a
	 * matching kmem_cache.
	 */
	zstd_dctx_size = P2ROUNDUP(ZSTD_estimateDCtxSize()
	    + sizeof (struct zstd_kmem), PAGESIZE);
	zstd_dctx_cache = kmem_cache_create("zfs_zstd_dctx", zstd_dctx_size,
	    0, NULL, NULL, NULL, NULL, NULL, 0);

	/* Create the fallback decompression context. */
	zstd_dctx_fallback.mem_size = zstd_dctx_size;
	zstd_dctx_fallback.mem = kmem_alloc(zstd_dctx_size, KM_SLEEP);
	mutex_init(&zstd_dctx_fallback.barrier, NULL, MUTEX_DEFAULT, NULL);

	return (0);
}

/* Destroy zstd-related kmem caches */
static void
zstd_memfini(void)
{
	/* Destroy the zstd context kmem_caches */
	kmem_cache_destroy(zstd_cctx_cache_32);
	kmem_cache_destroy(zstd_cctx_cache_64);
	kmem_cache_destroy(zstd_cctx_cache_max);
	kmem_cache_destroy(zstd_dctx_cache);

	/* Release fallback memory */
	kmem_free(zstd_dctx_fallback.mem, zstd_dctx_fallback.mem_size);
	mutex_destroy(&zstd_dctx_fallback.barrier);
}

/* release unused memory from pool */
void
zfs_zstd_cache_reap_now(void)
{
}

int
zstd_init_os(void)
{
	int ret = 0;

	ret = zstd_meminit();

	return (ret);
}

void
zstd_fini_os(void)
{
	zstd_memfini();
}
