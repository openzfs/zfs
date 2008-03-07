#ifndef _SPL_ZMOD_H
#define _SPL_ZMOD_H

#include <linux/zlib.h>

/* NOTE: z_compress_level/z_uncompress are nearly identical copies of
 * the compress2/uncompress functions provided by the official zlib
 * package available at http://zlib.net/.  The only changes made we to
 * slightly adapt the functioned called to match the linux kernel
 * implementation of zlib.
 */

/* ===========================================================================
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
static __inline__ int
z_compress_level(Byte *dest, uLong *destLen, const Byte *source,
                 uLong sourceLen, int level)
{
	z_stream stream;
	int err;

	stream.next_in = (Byte *)source;
	stream.avail_in = (uInt)sourceLen;
#ifdef MAXSEG_64K
	/* Check for source > 64K on 16-bit machine: */
	if ((uLong)stream.avail_in != sourceLen)
		return Z_BUF_ERROR;
#endif
	stream.next_out = dest;
	stream.avail_out = (uInt)*destLen;

	if ((uLong)stream.avail_out != *destLen)
		return Z_BUF_ERROR;

	err = zlib_deflateInit(&stream, level);
	if (err != Z_OK)
		return err;

	err = zlib_deflate(&stream, Z_FINISH);
	if (err != Z_STREAM_END) {
		zlib_deflateEnd(&stream);
		return err == Z_OK ? Z_BUF_ERROR : err;
	}
	*destLen = stream.total_out;

	err = zlib_deflateEnd(&stream);
	return err;
} /* z_compress_level() */

/* ===========================================================================
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
static __inline__ int
z_uncompress(Byte *dest, uLong *destLen, const Byte *source, uLong sourceLen)
{
	z_stream stream;
	int err;

	stream.next_in = (Byte *)source;
	stream.avail_in = (uInt)sourceLen;
	/* Check for source > 64K on 16-bit machine: */
	if ((uLong)stream.avail_in != sourceLen)
		return Z_BUF_ERROR;

	stream.next_out = dest;
	stream.avail_out = (uInt)*destLen;

	if ((uLong)stream.avail_out != *destLen)
		return Z_BUF_ERROR;

	err = zlib_inflateInit(&stream);
	if (err != Z_OK)
		return err;

	err = zlib_inflate(&stream, Z_FINISH);
	if (err != Z_STREAM_END) {
		zlib_inflateEnd(&stream);

		if (err == Z_NEED_DICT ||
		   (err == Z_BUF_ERROR && stream.avail_in == 0))
			return Z_DATA_ERROR;

		return err;
	}
	*destLen = stream.total_out;

	err = zlib_inflateEnd(&stream);
	return err;
} /* z_uncompress() */

#endif /* SPL_ZMOD_H */
