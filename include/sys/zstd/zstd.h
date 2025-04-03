// SPDX-License-Identifier: BSD-3-Clause
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

#ifndef	_ZFS_ZSTD_H
#define	_ZFS_ZSTD_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * ZSTD block header
 * NOTE: all fields in this header are in big endian order.
 */
typedef struct zfs_zstd_header {
	/* Compressed size of data */
	uint32_t c_len;

	/*
	 * Version and compression level
	 * We used to use a union to reference compression level
	 * and version easily, but as it turns out, relying on the
	 * ordering of bitfields is not remotely portable.
	 * So now we have get/set functions in zfs_zstd.c for
	 * manipulating this in just the right way forever.
	 */
	uint32_t raw_version_level;
	char data[];
} zfs_zstdhdr_t;

/*
 * Simple struct to pass the data from raw_version_level around.
 */
typedef struct zfs_zstd_meta {
	uint8_t level;
	uint32_t version;
} zfs_zstdmeta_t;

/*
 * kstat helper macros
 */
#define	ZSTDSTAT(stat)		(zstd_stats.stat.value.ui64)
#define	ZSTDSTAT_ZERO(stat)	\
	atomic_store_64(&zstd_stats.stat.value.ui64, 0)
#define	ZSTDSTAT_ADD(stat, val) \
	atomic_add_64(&zstd_stats.stat.value.ui64, (val))
#define	ZSTDSTAT_SUB(stat, val) \
	atomic_sub_64(&zstd_stats.stat.value.ui64, (val))
#define	ZSTDSTAT_BUMP(stat)	ZSTDSTAT_ADD(stat, 1)

/* (de)init for user space / kernel emulation */
int zstd_init(void);
void zstd_fini(void);

size_t zfs_zstd_compress(abd_t *src, abd_t *dst, size_t s_len,
    size_t d_len, int level);
int zfs_zstd_get_level(void *s_start, size_t s_len, uint8_t *level);
int zfs_zstd_decompress_level(abd_t *src, abd_t *dst, size_t s_len,
    size_t d_len, uint8_t *level);
int zfs_zstd_decompress(abd_t *src, abd_t *dst, size_t s_len,
    size_t d_len, int n);
void zfs_zstd_cache_reap_now(void);

/*
 * So, the reason we have all these complicated set/get functions is that
 * originally, in the zstd "header" we wrote out to disk, we used a 32-bit
 * bitfield to store the "level" (8 bits) and "version" (24 bits).
 *
 * Unfortunately, bitfields make few promises about how they're arranged in
 * memory...
 *
 * By way of example, if we were using version 1.4.5 and level 3, it'd be
 * level = 0x03, version = 10405/0x0028A5, which gets broken into Vhigh = 0x00,
 * Vmid = 0x28, Vlow = 0xA5. We include these positions below to help follow
 * which data winds up where.
 *
 * As a consequence, we wound up with little endian platforms with a layout
 * like this in memory:
 *
 *      0       8      16      24      32
 *      +-------+-------+-------+-------+
 *      | Vlow  | Vmid  | Vhigh | level |
 *      +-------+-------+-------+-------+
 *        =A5     =28     =00     =03
 *
 * ...and then, after being run through BE_32(), serializing this out to
 * disk:
 *
 *      0       8      16      24      32
 *      +-------+-------+-------+-------+
 *      | level | Vhigh | Vmid  | Vlow  |
 *      +-------+-------+-------+-------+
 *        =03     =00     =28     =A5
 *
 * while on big-endian systems, since BE_32() is a noop there, both in
 * memory and on disk, we wind up with:
 *
 *      0       8      16      24      32
 *      +-------+-------+-------+-------+
 *      | Vhigh | Vmid  | Vlow  | level |
 *      +-------+-------+-------+-------+
 *        =00     =28     =A5     =03
 *
 * (Vhigh is always 0 until version exceeds 6.55.35. Vmid and Vlow are the
 * other two bytes of the "version" data.)
 *
 * So now we use the BF32_SET macros to get consistent behavior (the
 * ondisk LE encoding, since x86 currently rules the world) across
 * platforms, but the "get" behavior requires that we check each of the
 * bytes in the aforementioned former-bitfield for 0x00, and from there,
 * we can know which possible layout we're dealing with. (Only the two
 * that have been observed in the wild are illustrated above, but handlers
 * for all 4 positions of 0x00 are implemented.
 */

static inline void
zfs_get_hdrmeta(const zfs_zstdhdr_t *blob, zfs_zstdmeta_t *res)
{
	uint32_t raw = blob->raw_version_level;
	uint8_t findme = 0xff;
	int shift;
	for (shift = 0; shift < 4; shift++) {
		findme = BF32_GET(raw, 8*shift, 8);
		if (findme == 0)
			break;
	}
	switch (shift) {
	case 0:
		res->level = BF32_GET(raw, 24, 8);
		res->version = BSWAP_32(raw);
		res->version = BF32_GET(res->version, 8, 24);
		break;
	case 1:
		res->level = BF32_GET(raw, 0, 8);
		res->version = BSWAP_32(raw);
		res->version = BF32_GET(res->version, 0, 24);
		break;
	case 2:
		res->level = BF32_GET(raw, 24, 8);
		res->version = BF32_GET(raw, 0, 24);
		break;
	case 3:
		res->level = BF32_GET(raw, 0, 8);
		res->version = BF32_GET(raw, 8, 24);
		break;
	default:
		res->level = 0;
		res->version = 0;
		break;
	}
}

static inline uint8_t
zfs_get_hdrlevel(const zfs_zstdhdr_t *blob)
{
	uint8_t level = 0;
	zfs_zstdmeta_t res;
	zfs_get_hdrmeta(blob, &res);
	level = res.level;
	return (level);
}

static inline uint32_t
zfs_get_hdrversion(const zfs_zstdhdr_t *blob)
{
	uint32_t version = 0;
	zfs_zstdmeta_t res;
	zfs_get_hdrmeta(blob, &res);
	version = res.version;
	return (version);

}

static inline void
zfs_set_hdrversion(zfs_zstdhdr_t *blob, uint32_t version)
{
	/* cppcheck-suppress syntaxError */
	BF32_SET(blob->raw_version_level, 0, 24, version);
}

static inline void
zfs_set_hdrlevel(zfs_zstdhdr_t *blob, uint8_t level)
{
	/* cppcheck-suppress syntaxError */
	BF32_SET(blob->raw_version_level, 24, 8, level);
}


#ifdef	__cplusplus
}
#endif

#endif /* _ZFS_ZSTD_H */
