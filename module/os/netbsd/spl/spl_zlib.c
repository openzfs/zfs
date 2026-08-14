// SPDX-License-Identifier: BSD-2-Clause

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cpu.h>
#include <sys/kmem.h>
#include <sys/zmod.h>
#include <net/zlib.h>

struct zc_hdr {
	size_t	size;
};

/*
 * Linux pre-allocates a per-call zlib workspace from a kmem_cache and
 * uses KM_SLEEP (module/os/linux/spl/spl-zlib.c).  That maps onto Linux
 * kernel zlib, which wants one contiguous workspace of known size.
 *
 * NetBSD (like FreeBSD) uses stock zlib with zalloc/zfree callbacks, so
 * a single-size workspace cache does not apply — inflate/deflate make
 * several variable-sized allocations.  A KM_NOSLEEP here turns a
 * transient allocation failure on decompress into Z_MEM_ERROR
 * → zio_decompress EIO on intact gzip data, and can mark the vdev
 * degraded.  gzip runs on the ZIO taskq, so KM_SLEEP waits instead of
 * failing the read.  Interrupt/softint still has to NOSLEEP.
 */
static void *
zcalloc(void *opaque, uint_t items, uint_t size)
{
	struct zc_hdr *h;
	size_t bytes, total;
	int kmflag;

	(void) opaque;
	if (size != 0 && items > (SIZE_MAX - sizeof (*h)) / size)
		return (NULL);
	bytes = (size_t)items * (size_t)size;
	total = sizeof (*h) + bytes;
	kmflag = (cpu_intr_p() || cpu_softintr_p()) ? KM_NOSLEEP : KM_SLEEP;
	h = kmem_alloc(total, kmflag);
	if (h == NULL)
		return (NULL);
	h->size = total;
	return (h + 1);
}

static void
zcfree(void *opaque, void *ptr)
{
	struct zc_hdr *h;

	(void) opaque;
	if (ptr == NULL)
		return;
	h = (struct zc_hdr *)ptr - 1;
	kmem_free(h, h->size);
}

static int
zlib_deflateInit(z_stream *stream, int level)
{
	stream->zalloc = zcalloc;
	stream->opaque = NULL;
	stream->zfree = zcfree;
	return (deflateInit(stream, level));
}

static int
zlib_deflate(z_stream *stream, int flush)
{
	return (deflate(stream, flush));
}

static int
zlib_deflateEnd(z_stream *stream)
{
	return (deflateEnd(stream));
}

static int
zlib_inflateInit(z_stream *stream)
{
	stream->zalloc = zcalloc;
	stream->opaque = NULL;
	stream->zfree = zcfree;
	return (inflateInit(stream));
}

static int
zlib_inflate(z_stream *stream, int flush)
{
	return (inflate(stream, flush));
}

static int
zlib_inflateEnd(z_stream *stream)
{
	return (inflateEnd(stream));
}

int
z_compress_level(void *dst, size_t *dstlen, const void *src, size_t srclen,
    int level)
{
	z_stream zs;
	int err;

	memset(&zs, 0, sizeof (zs));
	zs.next_in = (Bytef *)(uintptr_t)src;
	zs.avail_in = srclen;
	zs.next_out = dst;
	zs.avail_out = *dstlen;
	if ((err = zlib_deflateInit(&zs, level)) != Z_OK)
		return (err);
	if ((err = zlib_deflate(&zs, Z_FINISH)) != Z_STREAM_END) {
		(void) zlib_deflateEnd(&zs);
		return (err == Z_OK ? Z_BUF_ERROR : err);
	}
	*dstlen = zs.total_out;
	return (zlib_deflateEnd(&zs));
}

int
z_uncompress(void *dst, size_t *dstlen, const void *src, size_t srclen)
{
	z_stream zs;
	int err;

	memset(&zs, 0, sizeof (zs));
	zs.next_in = (Bytef *)(uintptr_t)src;
	zs.avail_in = srclen;
	zs.next_out = dst;
	zs.avail_out = *dstlen;
	if ((err = zlib_inflateInit(&zs)) != Z_OK)
		return (err);
	if ((err = zlib_inflate(&zs, Z_FINISH)) != Z_STREAM_END) {
		(void) zlib_inflateEnd(&zs);
		return (err == Z_OK ? Z_BUF_ERROR : err);
	}
	*dstlen = zs.total_out;
	return (zlib_inflateEnd(&zs));
}

int
z_compress(void *dst, size_t *dstlen, const void *src, size_t srclen)
{
	return (z_compress_level(dst, dstlen, src, srclen,
	    Z_DEFAULT_COMPRESSION));
}

const char *
z_strerror(int err)
{
	return (zError(err));
}
