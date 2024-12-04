/*
 * Copyright (c) 2020 iXsystems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/types.h>
#include <sys/kmem.h>
#include <sys/kmem_cache.h>
#include <sys/zmod.h>
#include <contrib/zlib/zlib.h>
#include <sys/kobj.h>


static void *
zcalloc(void *opaque, uint_t items, uint_t size)
{
	(void) opaque;
	return (malloc((size_t)items*size, M_SOLARIS, M_NOWAIT));
}

static void
zcfree(void *opaque, void *ptr)
{
	(void) opaque;
	free(ptr, M_SOLARIS);
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
zlib_inflate(z_stream *stream, int finish)
{
	return (inflate(stream, finish));
}


static int
zlib_inflateEnd(z_stream *stream)
{
	return (inflateEnd(stream));
}

/*
 * A kmem_cache is used for the zlib workspaces to avoid having to vmalloc
 * and vfree for every call.  Using a kmem_cache also has the advantage
 * that improves the odds that the memory used will be local to this cpu.
 * To further improve things it might be wise to create a dedicated per-cpu
 * workspace for use.  This would take some additional care because we then
 * must disable preemption around the critical section, and verify that
 * zlib_deflate* and zlib_inflate* never internally call schedule().
 */
static void *
zlib_workspace_alloc(int flags)
{
	// return (kmem_cache_alloc(zlib_workspace_cache, flags));
	return (NULL);
}

static void
zlib_workspace_free(void *workspace)
{
	// kmem_cache_free(zlib_workspace_cache, workspace);
}

/*
 * Compresses the source buffer into the destination buffer. The level
 * parameter has the same meaning as in deflateInit.  sourceLen is the byte
 * length of the source buffer. Upon entry, destLen is the total size of the
 * destination buffer, which must be at least 0.1% larger than sourceLen plus
 * 12 bytes. Upon exit, destLen is the actual size of the compressed buffer.
 *
 * compress2 returns Z_OK if success, Z_MEM_ERROR if there was not enough
 * memory, Z_BUF_ERROR if there was not enough room in the output buffer,
 * Z_STREAM_ERROR if the level parameter is invalid.
 */
int
z_compress_level(void *dest, size_t *destLen, const void *source,
    size_t sourceLen, int level)
{
	z_stream stream = {0};
	int err;

	stream.next_in = (Byte *)source;
	stream.avail_in = (uInt)sourceLen;
	stream.next_out = dest;
	stream.avail_out = (uInt)*destLen;
	stream.opaque = NULL;

	if ((size_t)stream.avail_out != *destLen)
		return (Z_BUF_ERROR);

	stream.opaque = zlib_workspace_alloc(KM_SLEEP);
#if 0
	if (!stream.opaque)
		return (Z_MEM_ERROR);
#endif
	err = zlib_deflateInit(&stream, level);
	if (err != Z_OK) {
		zlib_workspace_free(stream.opaque);
		return (err);
	}

	err = zlib_deflate(&stream, Z_FINISH);
	if (err != Z_STREAM_END) {
		zlib_deflateEnd(&stream);
		zlib_workspace_free(stream.opaque);
		return (err == Z_OK ? Z_BUF_ERROR : err);
	}
	*destLen = stream.total_out;

	err = zlib_deflateEnd(&stream);
	zlib_workspace_free(stream.opaque);
	return (err);
}

/*
 * Decompresses the source buffer into the destination buffer.  sourceLen is
 * the byte length of the source buffer. Upon entry, destLen is the total
 * size of the destination buffer, which must be large enough to hold the
 * entire uncompressed data. (The size of the uncompressed data must have
 * been saved previously by the compressor and transmitted to the decompressor
 * by some mechanism outside the scope of this compression library.)
 * Upon exit, destLen is the actual size of the compressed buffer.
 * This function can be used to decompress a whole file at once if the
 * input file is mmap'ed.
 *
 * uncompress returns Z_OK if success, Z_MEM_ERROR if there was not
 * enough memory, Z_BUF_ERROR if there was not enough room in the output
 * buffer, or Z_DATA_ERROR if the input data was corrupted.
 */
int
z_uncompress(void *dest, size_t *destLen, const void *source, size_t sourceLen)
{
	z_stream stream = {0};
	int err;

	stream.next_in = (Byte *)source;
	stream.avail_in = (uInt)sourceLen;
	stream.next_out = dest;
	stream.avail_out = (uInt)*destLen;

	if ((size_t)stream.avail_out != *destLen)
		return (Z_BUF_ERROR);

	stream.opaque = zlib_workspace_alloc(KM_SLEEP);
#if 0
	if (!stream.opaque)
		return (Z_MEM_ERROR);
#endif
	err = zlib_inflateInit(&stream);
	if (err != Z_OK) {
		zlib_workspace_free(stream.opaque);
		return (err);
	}

	err = zlib_inflate(&stream, Z_FINISH);
	if (err != Z_STREAM_END) {
		zlib_inflateEnd(&stream);
		zlib_workspace_free(stream.opaque);

		if (err == Z_NEED_DICT ||
		    (err == Z_BUF_ERROR && stream.avail_in == 0))
			return (Z_DATA_ERROR);

		return (err);
	}
	*destLen = stream.total_out;

	err = zlib_inflateEnd(&stream);
	zlib_workspace_free(stream.opaque);

	return (err);
}
