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
	 * We use a union to be able to big endian encode a single 32 bit
	 * unsigned integer, but still access the individual bitmasked
	 * components easily.
	 */
	union {
		uint32_t raw_version_level;
		struct {
			uint32_t version : 24;
			uint8_t level;
		};
	};

	char data[];
} zfs_zstdhdr_t;

/*
 * kstat helper macros
 */
#define	ZSTDSTAT(stat)		(zstd_stats.stat.value.ui64)
#define	ZSTDSTAT_ADD(stat, val) \
	atomic_add_64(&zstd_stats.stat.value.ui64, (val))
#define	ZSTDSTAT_SUB(stat, val) \
	atomic_sub_64(&zstd_stats.stat.value.ui64, (val))
#define	ZSTDSTAT_BUMP(stat)	ZSTDSTAT_ADD(stat, 1)

/* (de)init for user space / kernel emulation */
int zstd_init(void);
void zstd_fini(void);

size_t zfs_zstd_compress(void *s_start, void *d_start, size_t s_len,
    size_t d_len, int level);
int zfs_zstd_get_level(void *s_start, size_t s_len, uint8_t *level);
int zfs_zstd_decompress_level(void *s_start, void *d_start, size_t s_len,
    size_t d_len, uint8_t *level);
int zfs_zstd_decompress(void *s_start, void *d_start, size_t s_len,
    size_t d_len, int n);
void zfs_zstd_cache_reap_now(void);

#ifdef	__cplusplus
}
#endif

#endif /* _ZFS_ZSTD_H */
