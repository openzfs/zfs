/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://github.com/behlendorf/spl/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************
 *  z_compress_level/z_uncompress are nearly identical copies of the
 *  compress2/uncompress functions provided by the official zlib package
 *  available at http://zlib.net/.  The only changes made we to slightly
 *  adapt the functions called to match the linux kernel implementation
 *  of zlib.  The full zlib license follows:
 *
 *  zlib.h -- interface of the 'zlib' general purpose compression library
 *  version 1.2.5, April 19th, 2010
 *
 *  Copyright (C) 1995-2010 Jean-loup Gailly and Mark Adler
 *
 *  This software is provided 'as-is', without any express or implied
 *  warranty.  In no event will the authors be held liable for any damages
 *  arising from the use of this software.
 *
 *  Permission is granted to anyone to use this software for any purpose,
 *  including commercial applications, and to alter it and redistribute it
 *  freely, subject to the following restrictions:
 *
 *  1. The origin of this software must not be misrepresented; you must not
 *     claim that you wrote the original software. If you use this software
 *     in a product, an acknowledgment in the product documentation would be
 *     appreciated but is not required.
 *  2. Altered source versions must be plainly marked as such, and must not be
 *     misrepresented as being the original software.
 *  3. This notice may not be removed or altered from any source distribution.
 *
 *  Jean-loup Gailly
 *  Mark Adler
\*****************************************************************************/

#ifndef _SPL_ZMOD_H
#define _SPL_ZMOD_H

#include <linux/zlib.h>

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
static __inline__ int
z_compress_level(void *dest, size_t *destLen, const void *source,
                 size_t sourceLen, int level)
{
	z_stream stream;
	int err;

	stream.next_in = (Byte *)source;
	stream.avail_in = (uInt)sourceLen;
#ifdef MAXSEG_64K
	/* Check for source > 64K on 16-bit machine: */
	if ((size_t)stream.avail_in != sourceLen)
		return Z_BUF_ERROR;
#endif
	stream.next_out = dest;
	stream.avail_out = (uInt)*destLen;

	if ((size_t)stream.avail_out != *destLen)
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
static __inline__ int
z_uncompress(void *dest, size_t *destLen, const void *source, size_t sourceLen)
{
	z_stream stream;
	int err;

	stream.next_in = (Byte *)source;
	stream.avail_in = (uInt)sourceLen;
	/* Check for source > 64K on 16-bit machine: */
	if ((size_t)stream.avail_in != sourceLen)
		return Z_BUF_ERROR;

	stream.next_out = dest;
	stream.avail_out = (uInt)*destLen;

	if ((size_t)stream.avail_out != *destLen)
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
