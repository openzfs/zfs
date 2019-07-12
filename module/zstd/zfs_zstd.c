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
 * Copyright (c) 2016-2019 by Klara Inc.
 * Copyright (c) 2016-2019 Allan Jude <allanjude@freebsd.org>.
 */

#include <sys/param.h>
#include <sys/zfs_context.h>
#include <sys/zio_compress.h>
#include <sys/spa.h>

#define	ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>
#include <zstd_errors.h>

#define	ZSTD_KMEM_MAGIC			0x20160831

#if defined(__linux__) && defined(_KERNEL)
#include <linux/sort.h>
#define	qsort(base, num, size, cmp)	sort(base, num, size, cmp, NULL)
#endif

#define	__unused			__attribute__((unused))

static size_t real_zstd_compress(const char *source, char *dest, int isize,
    int osize, int level);
static size_t real_zstd_decompress(const char *source, char *dest, int isize,
    int maxosize);

void *zstd_alloc(void *opaque, size_t size);
void zstd_free(void *opaque, void *ptr);

static const ZSTD_customMem zstd_malloc = {
	zstd_alloc,
	zstd_free,
	NULL,
};

enum zstd_kmem_type {
	ZSTD_KMEM_UNKNOWN = 0,
	ZSTD_KMEM_CCTX,
	ZSTD_KMEM_WRKSPC_4K_MIN,
	ZSTD_KMEM_WRKSPC_4K_DEF,
	ZSTD_KMEM_WRKSPC_4K_MAX,
	ZSTD_KMEM_WRKSPC_16K_MIN,
	ZSTD_KMEM_WRKSPC_16K_DEF,
	ZSTD_KMEM_WRKSPC_16K_MAX,
	ZSTD_KMEM_WRKSPC_128K_MIN,
	ZSTD_KMEM_WRKSPC_128K_DEF,
	ZSTD_KMEM_WRKSPC_128K_MAX,
	/* SPA_MAXBLOCKSIZE */
	ZSTD_KMEM_WRKSPC_16M_MIN,
	ZSTD_KMEM_WRKSPC_16M_DEF,
	ZSTD_KMEM_WRKSPC_16M_MAX,
	ZSTD_KMEM_DCTX,
	ZSTD_KMEM_COUNT,
};

struct zstd_kmem {
	uint_t			kmem_magic;
	enum zstd_kmem_type	kmem_type;
	size_t			kmem_size;
};

struct zstd_kmem_config {
	size_t			block_size;
	int			compress_level;
	char			*cache_name;
};

struct zstd_emerg_alloc {
	void			*ptr;
	kmutex_t 		mtx;
};

static kmem_cache_t *zstd_kmem_cache[ZSTD_KMEM_COUNT] = { NULL };
static struct zstd_kmem zstd_cache_size[ZSTD_KMEM_COUNT] = {
	{ ZSTD_KMEM_MAGIC, 0, 0 }
};
static struct zstd_kmem_config zstd_cache_config[ZSTD_KMEM_COUNT] = {
	{ 0, 0, "zstd_unknown" },
	{ 0, 0, "zstd_cctx" },
	{ 4096, ZIO_ZSTD_LEVEL_MIN, "zstd_wrkspc_4k_min" },
	{ 4096, ZIO_ZSTD_LEVEL_DEFAULT, "zstd_wrkspc_4k_def" },
	{ 4096, ZIO_ZSTD_LEVEL_MAX, "zstd_wrkspc_4k_max" },
	{ 16384, ZIO_ZSTD_LEVEL_MIN, "zstd_wrkspc_16k_min" },
	{ 16384, ZIO_ZSTD_LEVEL_DEFAULT, "zstd_wrkspc_16k_def" },
	{ 16384, ZIO_ZSTD_LEVEL_MAX, "zstd_wrkspc_16k_max" },
	{ SPA_OLD_MAXBLOCKSIZE, ZIO_ZSTD_LEVEL_MIN, "zstd_wrkspc_128k_min" },
	{ SPA_OLD_MAXBLOCKSIZE, ZIO_ZSTD_LEVEL_DEFAULT,
	    "zstd_wrkspc_128k_def" },
	{ SPA_OLD_MAXBLOCKSIZE, ZIO_ZSTD_LEVEL_MAX, "zstd_wrkspc_128k_max" },
	{ SPA_MAXBLOCKSIZE, ZIO_ZSTD_LEVEL_MIN, "zstd_wrkspc_mbs_min" },
	{ SPA_MAXBLOCKSIZE, ZIO_ZSTD_LEVEL_DEFAULT, "zstd_wrkspc_mbs_def" },
	{ SPA_MAXBLOCKSIZE, ZIO_ZSTD_LEVEL_MAX, "zstd_wrkspc_mbs_max" },
	{ 0, 0, "zstd_dctx" },
};
static struct zstd_emerg_alloc zstd_dctx_emerg = { NULL };

static int
zstd_compare(const void *a, const void *b)
{
	struct zstd_kmem *x, *y;

	x = (struct zstd_kmem *)a;
	y = (struct zstd_kmem *)b;

	ASSERT3U(x->kmem_magic, ==, ZSTD_KMEM_MAGIC);
	ASSERT3U(y->kmem_magic, ==, ZSTD_KMEM_MAGIC);

	return (TREE_CMP(x->kmem_size, y->kmem_size));
}

static enum zio_zstd_levels
zstd_cookie_to_enum(uint32_t level)
{
	enum zio_zstd_levels elevel = ZIO_ZSTDLVL_INHERIT;

	if (level > 0 && level <= ZIO_ZSTDLVL_MAX) {
		elevel = level;
		return (elevel);
	} else if (level < 0) {
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
				/* This shouldn't happen. Cause a panic. */
				panic("Invalid ZSTD level encountered: %d",
				    level);
		}
	}

	/* This shouldn't happen. Cause a panic. */
	panic("Invalid ZSTD level encountered: %d", level);

	return (ZIO_ZSTDLVL_INHERIT);
}

static uint32_t
zstd_enum_to_cookie(enum zio_zstd_levels elevel)
{
	int level = 0;

	if (elevel > ZIO_ZSTDLVL_INHERIT && elevel <= ZIO_ZSTDLVL_MAX) {
		level = elevel;
		return (level);
	} else if (elevel > ZIO_ZSTDLVL_FAST &&
	    elevel <= ZIO_ZSTDLVL_FAST_MAX) {
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
				/* This shouldn't happen. Cause a panic. */
				panic("Invalid ZSTD enum level encountered: %d",
				    elevel);
		}
	}

	/* This shouldn't happen. Cause a panic. */
	panic("Invalid ZSTD enum level encountered: %d", elevel);

	return (0);
}

size_t
zstd_compress(void *s_start, void *d_start, size_t s_len, size_t d_len, int n)
{
	size_t c_len;
	uint32_t bufsiz;
	uint32_t levelcookie;
	char *dest = d_start;

	ASSERT3U(d_len, >=, sizeof (bufsiz));
	ASSERT3U(d_len, <=, s_len);

	levelcookie = zstd_enum_to_cookie(n);

	/* XXX: this could overflow, but we never have blocks that big */
	c_len = real_zstd_compress(s_start,
	    &dest[sizeof (bufsiz) + sizeof (levelcookie)], s_len,
	    d_len - sizeof (bufsiz) - sizeof (levelcookie), levelcookie);

	/* Signal an error if the compression routine returned an error. */
	if (ZSTD_isError(c_len)) {
		return (s_len);
	}

	/*
	 * Encode the compresed buffer size at the start. We'll need this in
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
	uint32_t bufsiz = BE_IN32(src);
	uint32_t levelcookie = BE_IN32(&src[sizeof (bufsiz)]);
	uint8_t zstdlevel = zstd_cookie_to_enum(levelcookie);

	ASSERT3U(d_len, >=, s_len);
	ASSERT3U(zstdlevel, !=, ZIO_ZSTDLVL_INHERIT);

	/* invalid compressed buffer size encoded at start */
	if (bufsiz + sizeof (bufsiz) > s_len) {
		return (1);
	}

	/*
	 * Returns 0 on success (decompression function returned non-negative)
	 * and non-zero on failure (decompression function returned negative.
	 */
	if (ZSTD_isError(real_zstd_decompress(
	    &src[sizeof (bufsiz) + sizeof (levelcookie)], d_start, bufsiz,
	    d_len))) {
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

static size_t
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
	 * out of kernel memory, gently fall through - this will disable
	 * compression in zio_compress_data
	 */
	if (cctx == NULL) {
		return (0);
	}

	result = ZSTD_compressCCtx(cctx, dest, osize, source, isize, level);

	ZSTD_freeCCtx(cctx);
	return (result);
}


static size_t
real_zstd_decompress(const char *source, char *dest, int isize, int maxosize)
{
	size_t result;
	boolean_t emerg = B_FALSE;
	ZSTD_DCtx *dctx;

	dctx = ZSTD_createDCtx_advanced(zstd_malloc);
	if (dctx == NULL) {
		if (zstd_dctx_emerg.ptr == NULL) {
			return (ZSTD_error_memory_allocation);
		}
		emerg = B_TRUE;
		mutex_enter(&zstd_dctx_emerg.mtx);
		ZSTD_DCtx_reset(zstd_dctx_emerg.ptr,
		    ZSTD_reset_session_and_parameters);
		dctx = (ZSTD_DCtx *)&zstd_dctx_emerg.ptr;
	}

	result = ZSTD_decompressDCtx(dctx, dest, maxosize, source, isize);

	if (emerg == B_TRUE) {
		mutex_exit(&zstd_dctx_emerg.mtx);
	} else {
		ZSTD_freeDCtx(dctx);
	}

	return (result);
}

void *
zstd_alloc(void *opaque __unused, size_t size)
{
	size_t nbytes = sizeof (struct zstd_kmem) + size;
	struct zstd_kmem *z = NULL;
	enum zstd_kmem_type type;
	int i;

	type = ZSTD_KMEM_UNKNOWN;
	for (i = 0; i < ZSTD_KMEM_COUNT; i++) {
		if (nbytes <= zstd_cache_size[i].kmem_size) {
			type = zstd_cache_size[i].kmem_type;
			z = kmem_cache_alloc(zstd_kmem_cache[type],
			    KM_NOSLEEP);
			break;
		}
	}
	/* No matching cache */
	if (type == ZSTD_KMEM_UNKNOWN) {
		/* XXX: This is likely to fail on Linux if nbytes > 64KB */
		z = kmem_alloc(nbytes, KM_NOSLEEP);
	}
	if (z == NULL) {
		return (NULL);
	}

	z->kmem_magic = ZSTD_KMEM_MAGIC;
	z->kmem_type = type;
	z->kmem_size = nbytes;

	return (z + 1);
}

void
zstd_free(void *opaque __unused, void *ptr)
{
	struct zstd_kmem *z = (struct zstd_kmem *)ptr - 1;

	ASSERT3U(z->kmem_magic, ==, ZSTD_KMEM_MAGIC);
	ASSERT3U(z->kmem_type, <, ZSTD_KMEM_COUNT);
	ASSERT3U(z->kmem_type, >=, ZSTD_KMEM_UNKNOWN);

	if (z->kmem_type == ZSTD_KMEM_UNKNOWN) {
		kmem_free(z, z->kmem_size);
	} else {
		kmem_cache_free(zstd_kmem_cache[z->kmem_type], z);
	}
}

void
zstd_init(void)
{
	int i;

	/* There is no estimate function for the CCtx itself */
	zstd_cache_size[1].kmem_magic = ZSTD_KMEM_MAGIC;
	zstd_cache_size[1].kmem_type = 1;
	zstd_cache_size[1].kmem_size = P2ROUNDUP(zstd_cache_config[1].block_size
	    + sizeof (struct zstd_kmem), PAGESIZE);
	zstd_kmem_cache[1] = kmem_cache_create(
	    zstd_cache_config[1].cache_name, zstd_cache_size[1].kmem_size,
	    0, NULL, NULL, NULL, NULL, NULL, 0);

	/*
	 * Estimate the size of the ZSTD CCtx workspace required for each record
	 * size at each compression level.
	 */
	for (i = 2; i < ZSTD_KMEM_DCTX; i++) {
		ASSERT3P(zstd_cache_config[i].cache_name, !=, NULL);
		zstd_cache_size[i].kmem_magic = ZSTD_KMEM_MAGIC;
		zstd_cache_size[i].kmem_type = i;
		zstd_cache_size[i].kmem_size = P2ROUNDUP(
		    ZSTD_estimateCCtxSize_usingCParams(
		    ZSTD_getCParams(zstd_cache_config[i].compress_level,
		    zstd_cache_config[i].block_size, 0)) +
		    sizeof (struct zstd_kmem), PAGESIZE);
		zstd_kmem_cache[i] = kmem_cache_create(
		    zstd_cache_config[i].cache_name,
		    zstd_cache_size[i].kmem_size,
		    0, NULL, NULL, NULL, NULL, NULL, 0);
	}
	/* Estimate the size of the decompression context */
	zstd_cache_size[i].kmem_magic = ZSTD_KMEM_MAGIC;
	zstd_cache_size[i].kmem_type = i;
	zstd_cache_size[i].kmem_size = P2ROUNDUP(ZSTD_estimateDCtxSize() +
	    sizeof (struct zstd_kmem), PAGESIZE);
	zstd_kmem_cache[i] = kmem_cache_create(zstd_cache_config[i].cache_name,
	    zstd_cache_size[i].kmem_size, 0, NULL, NULL, NULL, NULL, NULL, 0);

	/* Sort the kmem caches for later searching */
	qsort(zstd_cache_size, ZSTD_KMEM_COUNT, sizeof (struct zstd_kmem),
	    zstd_compare);

	/* Allocate a last-ditch DCTX to use on allocation failure */
	zstd_dctx_emerg.ptr = kmem_cache_alloc(zstd_kmem_cache[ZSTD_KMEM_DCTX],
	    KM_SLEEP);
	if (zstd_dctx_emerg.ptr == NULL) {
		panic("Failed to allocate memory in zstd_init()");
	}
	mutex_init(&zstd_dctx_emerg.mtx, NULL, MUTEX_DEFAULT, NULL);
}

void
zstd_fini(void)
{
	int i, type;

	kmem_cache_free(zstd_kmem_cache[ZSTD_KMEM_DCTX], zstd_dctx_emerg.ptr);
	mutex_destroy(&zstd_dctx_emerg.mtx);

	for (i = 0; i < ZSTD_KMEM_COUNT; i++) {
		type = zstd_cache_size[i].kmem_type;
		if (zstd_kmem_cache[type] != NULL) {
			kmem_cache_destroy(zstd_kmem_cache[type]);
		}
	}
}

EXPORT_SYMBOL(zstd_compress);
EXPORT_SYMBOL(zstd_decompress_level);
EXPORT_SYMBOL(zstd_decompress);
EXPORT_SYMBOL(zstd_get_level);
