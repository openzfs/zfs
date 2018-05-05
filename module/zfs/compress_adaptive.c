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

#include <sys/compress_adaptive.h>
#include <sys/zio_compress.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>

enum zio_compress ac_compress[COMPRESS_ADAPTIVE_LEVELS] = {
	ZIO_COMPRESS_LZ4,
	ZIO_COMPRESS_GZIP_1,
	ZIO_COMPRESS_GZIP_2,
	ZIO_COMPRESS_GZIP_3,
	ZIO_COMPRESS_GZIP_4,
	ZIO_COMPRESS_GZIP_5,
	ZIO_COMPRESS_GZIP_6,
	ZIO_COMPRESS_GZIP_7,
	ZIO_COMPRESS_GZIP_8,
	ZIO_COMPRESS_GZIP_9};


void
compress_set_algorithm(uint64_t level, enum zio_compress *c)
{
	*c = ac_compress[level];
}

void
compress_set_default_algorithm(enum zio_compress *c)
{
	compress_set_algorithm(0, c);
}

uint64_t
compress_calc_delay(uint64_t byte, uint64_t byte_per_second)
{
	return ((byte * 1000000000) / byte_per_second);
}

uint64_t
compress_calc_Bps(uint64_t byte, hrtime_t delay)
{
	return ((byte * 1000000000) / delay);
}

void
compress_calc_avg_without_zero(uint64_t act, uint64_t *res, int n)
{
	uint64_t prev = *res;
	if (act) {
		if (prev) {
			*res = (act + prev * (n - 1)) / n;
		} else {
			*res = act;
		}
	}
}

uint64_t
compress_vdev_queue_delay(uint64_t size, vdev_t *vd)
{
	uint64_t vd_writeBps = vd->vdev_stat_ex.vsx_diskBps[ZIO_TYPE_WRITE];

	if (vd_writeBps == 0) {
		return (0);
	}

	uint64_t vd_queued_size_write =
	    vd->vdev_queue.vq_class[ZIO_PRIORITY_ASYNC_WRITE].vqc_queued_size;

	uint32_t max_queue_depth = zfs_vdev_async_write_max_active *
	    zfs_vdev_queue_depth_pct / 50;
	/*
	 * keep at least 10 ZIOs in queue * compression factor about 2
	 * = average 25
	 */
	uint64_t queue_offset = size * (max_queue_depth / 4);
	if (vd_queued_size_write >= queue_offset) {
		vd_queued_size_write -= queue_offset;
	} else {
		vd_queued_size_write = 0;
	}

	return (compress_calc_delay(vd_queued_size_write, vd_writeBps));
}

uint64_t
compress_min_queue_delay(uint64_t size, vdev_t *vd)
{
	uint64_t min_delay = 0;

	if (!vd->vdev_children) { // is leaf
		min_delay = compress_vdev_queue_delay(size, vd);
	} else {
		int i;
		for (i = 0; i < vd->vdev_children; i++) {
			uint64_t vdev_delay = compress_min_queue_delay(size,
			    vd->vdev_child[i]);
			if (vdev_delay) {
				if (min_delay == 0) {
					min_delay = vdev_delay;
				} else if (vdev_delay < min_delay) {
					min_delay = vdev_delay;
				}
			}
		}
	}
	return (min_delay);
}

void
compress_update_pio(uint64_t compressBps, uint8_t compress_level,
    zio_t *pio)
{
	int n = 10;
	compress_calc_avg_without_zero(compressBps,
	    &pio->io_compress_adaptive_Bps[compress_level], n);

	if (pio->io_compress_adaptive_exploring) {
		pio->io_compress_adaptive_exploring = B_FALSE;
	} else {
		pio->io_compress_level = compress_level;
	}
}

uint64_t
compress_get_faster_level(uint64_t lsize, uint8_t level,
    uint64_t available_queue_delay, zio_t *pio)
{
	if (level < COMPRESS_ADAPTIVE_LEVELS - 1) {
		uint64_t fasterBps = pio->io_compress_adaptive_Bps[level + 1];

		if (fasterBps != 0) {

			uint64_t new_required_queue_delay =
			    compress_calc_delay(lsize, fasterBps);

			if (new_required_queue_delay < available_queue_delay) {
				level++;
			}

		} else if (pio->io_compress_adaptive_exploring == B_FALSE) {
			pio->io_compress_adaptive_exploring = B_TRUE;
			level++;
		}
	}
	return (level);
}

uint64_t
compress_get_slower_level(uint64_t lsize, uint8_t level,
    uint64_t required_queue_delay, uint64_t available_queue_delay, zio_t *pio)
{
	while (required_queue_delay > available_queue_delay) {
		if (level > 0) {
			level--;
			required_queue_delay = compress_calc_delay(lsize,
			    pio->io_compress_adaptive_Bps[level]);
		} else {
			break;
		}
	}
	return (level);
}

uint64_t
compress_get_optimal_level(uint64_t lsize, vdev_t *rvd, zio_t *pio)
{
	uint64_t available_queue_delay;
	uint64_t required_queue_delay;
	uint64_t level = pio->io_compress_level;

	if (pio->io_compress_adaptive_Bps[level] == 0) {
		return (level);
	}

	available_queue_delay = compress_min_queue_delay(lsize, rvd);
	required_queue_delay = compress_calc_delay(lsize,
	    pio->io_compress_adaptive_Bps[level]);


	if (required_queue_delay < available_queue_delay) {
		level = compress_get_faster_level(lsize, level,
		    available_queue_delay, pio);
	} else {
		level = compress_get_slower_level(lsize, level,
		    required_queue_delay, available_queue_delay, pio);
	}
	return (level);

}

size_t
compress_adaptive(zio_t *zio, abd_t *src, void *dst, size_t s_len,
    enum zio_compress *c)
{
	size_t psize;
	vdev_t *rvd = zio->io_spa->spa_root_vdev;
	zio_t *pio = zio_unique_parent(zio);

	compress_set_default_algorithm(c);

	if (pio == NULL) {
		psize = zio_compress_data(*c, src, dst, s_len);
	} else {
		hrtime_t compress_begin = gethrtime();

		uint64_t level = compress_get_optimal_level(zio->io_lsize,
		    rvd, pio);
		compress_set_algorithm(level, c);

		psize = zio_compress_data(*c, src, dst, s_len);

		hrtime_t compress_delay = gethrtime() - compress_begin;
		uint64_t compressBps = compress_calc_Bps(zio->io_lsize,
		    compress_delay);

		compress_update_pio(compressBps, level, pio);
	}

	return (psize);
}
