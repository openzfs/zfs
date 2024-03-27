/*
 * © 2021. Triad National Security, LLC. All rights reserved.
 *
 * This program was produced under U.S. Government contract
 * 89233218CNA000001 for Los Alamos National Laboratory (LANL), which
 * is operated by Triad National Security, LLC for the U.S.
 * Department of Energy/National Nuclear Security Administration. All
 * rights in the program are reserved by Triad National Security, LLC,
 * and the U.S. Department of Energy/National Nuclear Security
 * Administration. The Government is granted for itself and others
 * acting on its behalf a nonexclusive, paid-up, irrevocable worldwide
 * license in this material to reproduce, prepare derivative works,
 * distribute copies to the public, perform publicly and display
 * publicly, and to permit others to do so.
 *
 * ----
 *
 * This program is open source under the BSD-3 License.
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
 *
 */

#ifndef _ZIA_H
#define	_ZIA_H

#include <sys/abd.h>
#include <sys/fs/zfs.h> /* VDEV_RAIDZ_MAXPARITY */
#include <sys/vdev.h>
#include <sys/zio.h>
#include <sys/zio_compress.h>
#include <sys/zio_checksum.h>

typedef struct raidz_row raidz_row_t;
typedef struct raidz_map raidz_map_t;

/* ******************************************************** */
/* return values */
#define	ZIA_OK 1000

/* something bad happened not related to missing functionality */
#define	ZIA_ERROR 1001

/* error, fallback to zfs implementation */
#define	ZIA_FALLBACK 1002

/* ran, but result is bad */
#define	ZIA_BAD_RESULT 1003

/* expected provider and actual provider do not match */
#define	ZIA_PROVIDER_MISMATCH 1004

/*
 * error, returned when the provider can no longer
 * communicate with the accelerator (providers are
 * software, and are not expected to randomly go
 * down)
 */
#define	ZIA_ACCELERATOR_DOWN 1005
/* ******************************************************** */

/* DPUSM was not found by configure */
#define	ZIA_DISABLED 1006

/*
 * This struct is normally set with
 * zpool set zia_<property>=on/off/<value>
 * and passed around in spa_t.
 */
typedef struct zia_props {
	/* global state */
	boolean_t can_offload;
	void *provider;

	/* minimum size allowed to offload - set by ashift */
	size_t min_offload_size;

	int compress;
	int decompress;

	int checksum;

	struct {
		int gen[VDEV_RAIDZ_MAXPARITY + 1];
		int rec[VDEV_RAIDZ_MAXPARITY + 1];
	} raidz;

	int file_write;
	int disk_write;
} zia_props_t;

zia_props_t *zia_get_props(spa_t *spa);
void zia_prop_warn(boolean_t val, const char *name);

int zia_init(void);
int zia_fini(void);

void *zia_get_provider(const char *name, vdev_t *vdev);
const char *zia_get_provider_name(void *provider);
int zia_put_provider(void **provider, vdev_t *vdev);

/*
 * turn off offloading for this zio as well as
 * all new zios created with the same spa
 */
int zia_disable_offloading(zio_t *zio, boolean_t reexecute);

/* check if offloading can occur */
boolean_t zia_is_used(zio_t *zio);

/*
 * check if a handle is associated with this pointer
 *
 * not exposing functions for different handles because
 * only abd handles are checked outside of zia.c
 */
boolean_t zia_is_offloaded(abd_t *abd);

int zia_worst_error(const int lhs, const int rhs);

/* create a new offloader handle without copying data */
void *zia_alloc(void *provider, size_t size, size_t min_offload_size);

/* deallocate handle without onloading */
int zia_free(void **handle);

/* move linear data between from the offloader to memory */
int zia_onload(void **handle, void *buf, size_t size);

/* calls abd_iterate_func on the abd to copy abd data back and forth */
int zia_offload_abd(void *provider, abd_t *abd,
    size_t size, size_t min_offload_size, boolean_t *local_offload);
int zia_onload_abd(abd_t *abd, size_t size, boolean_t keep_handle);
/* move a handle into an abd */
void zia_move_into_abd(abd_t *dst, void **src);
int zia_free_abd(abd_t *abd, boolean_t lock);

/*
 * if offloaded locally, just free the handle
 * if not, onload the data and free the handle
 */
int zia_cleanup_abd(abd_t *abd, size_t size, boolean_t local_offload);

/* if the accelerator failed, restart the zio */
void zia_restart_before_vdev(zio_t *zio);

/* fill a buffer with zeros */
int zia_zero_fill(abd_t *abd, size_t offset, size_t size);

int
zia_compress(zia_props_t *props, enum zio_compress c,
    abd_t *src, size_t s_len,
    void **cbuf_handle, uint64_t *c_len,
    uint8_t level, boolean_t *local_offload);

int
zia_decompress(zia_props_t *props, enum zio_compress c,
    abd_t *src, size_t s_len, abd_t *dst, size_t d_len,
    uint8_t *level);

int zia_checksum_compute(void *provider, zio_cksum_t *dst,
    enum zio_checksum alg, zio_t *zio, uint64_t size,
    boolean_t *local_offload);
int zia_checksum_error(enum zio_checksum alg, abd_t *abd,
    uint64_t size, int byteswap, zio_cksum_t *actual_cksum);

/* raidz */
int zia_raidz_alloc(zio_t *zio, raidz_row_t *rr, boolean_t rec,
    uint_t cksum, boolean_t *local_offload);
int zia_raidz_free(raidz_row_t *rr, boolean_t onload_parity);
int zia_raidz_gen(raidz_row_t *rr);
int zia_raidz_gen_cleanup(zio_t *zio, raidz_row_t *rr,
    boolean_t local_offload);
int zia_raidz_new_parity(zio_t *zio, raidz_row_t *rr, uint64_t c);
/* compare the contents of offloaded abds (only used in resilver) */
int zia_raidz_cmp(abd_t *lhs, abd_t *rhs, int *diff);
int zia_raidz_rec(raidz_row_t *rr, int *t, int nt);
int zia_raidz_rec_cleanup(zio_t *zio, raidz_row_t *rr,
    boolean_t local_offload, boolean_t onload_parity);

/* file I/O */
int zia_file_open(vdev_t *vdev, const char *path,
    int flags, int mode);
int zia_file_write(vdev_t *vdev, abd_t *abd, ssize_t size,
    loff_t offset, ssize_t *resid, int *err);
int zia_file_close(vdev_t *vdev);

#ifdef _KERNEL
#include <linux/blkdev.h>

/* disk I/O */
int zia_disk_open(vdev_t *vdev, const char *path,
    struct block_device *bdev);
int zia_disk_invalidate(vdev_t *vdev);
int zia_disk_write(vdev_t *vdev, zio_t *zio,
    size_t io_size, uint64_t io_offset, int flags);
int zia_disk_flush(vdev_t *vdev, zio_t *zio);
int zia_disk_close(vdev_t *vdev);
#endif

#endif
