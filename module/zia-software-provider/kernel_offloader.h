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

#ifndef _KERNEL_OFFLOADER_H
#define	_KERNEL_OFFLOADER_H

#include <linux/blk_types.h>
#include <linux/scatterlist.h>

#include <dpusm/provider_api.h>

/*
 * This file represents the API provided by a vendor to access their
 * offloader. The API can be anything the implementor chooses to
 * expose. There are no limitations on the function signature or
 * name. They just have to be called correctly in the Z.I.A. provider.
 * ZFS and Z.I.A. will not need direct access to any data located on
 * the offloader. Some raw pointers from Z.I.A. will be used directly,
 * but those will always contain information located in memory.
 *
 * -------------------------------------------------------------------
 *
 * The kernel offloader fakes offloads by copying data into memory
 * regions distinct from the calling process's memory space. The
 * corresponding C file conflates the driver and the "physical" device
 * since both memory spaces are in kernel space and run on the
 * CPU. This offloader provides opaque pointers to the provider to
 * simulate handles to inaccessible memory locations. In order to
 * prevent the handle from being dereferenced and used successfully by
 * ZFS or Z.I.A., the handle pointer is masked with a random value
 * generated at load-time. Other offloaders may choose to present
 * non-void handles.
 */

/* return values */
#define	KERNEL_OFFLOADER_OK 0

/* function is implemented, but the chosen operation is not implemented */
#define	KERNEL_OFFLOADER_UNAVAILABLE 1

/* ran, but could not complete */
#define	KERNEL_OFFLOADER_ERROR 2

/* ran, but failed a check on a result */
#define	KERNEL_OFFLOADER_BAD_RESULT 3

/* "hardware" went down for some reason (overheated, unplugged, etc.) */
#define	KERNEL_OFFLOADER_DOWN 4

/*
 * init function - this should be the kernel module init, but
 * kernel offloader is not compiled as a separate kernel module
 */
void kernel_offloader_init(void);
void kernel_offloader_fini(void);

/* offloader handle access */
void *kernel_offloader_alloc(size_t size);
void *kernel_offloader_alloc_ref(void *src, size_t offset, size_t size);
int kernel_offloader_get_size(void *handle, size_t *size, size_t *actual);
int kernel_offloader_free(void *handle);
int kernel_offloader_copy_from_generic(void *handle, size_t offset,
    const void *src, size_t size);
int kernel_offloader_copy_to_generic(void *handle, size_t offset,
    void *dst, size_t size);
/* status check */
int kernel_offloader_mem_stats(
    void *t_count_handle, void *t_size_handle, void *t_actual_handle,
    void *a_count_handle, void *a_size_handle, void *a_actual_handle);
int kernel_offloader_cmp(void *lhs_handle, void *rhs_handle, int *diff);
int kernel_offloader_zero_fill(void *handle, size_t offset, size_t size);
int kernel_offloader_all_zeros(void *handle, size_t offset, size_t size);

/* ZIO Pipeline Stages */

int kernel_offloader_compress(dpusm_compress_t alg, int level,
    void *src, size_t s_len, void *dst, void *d_len);

int kernel_offloader_decompress(dpusm_compress_t alg, void *level,
    void *src, size_t s_len, void *dst, void *d_len);

int kernel_offloader_checksum(dpusm_checksum_t alg,
    dpusm_checksum_byteorder_t order, void *data, size_t size,
    void *cksum, size_t cksum_size);

void *kernel_offloader_raidz_alloc(size_t nparity, size_t ndata);
int kernel_offloader_raidz_set_column(void *raidz, uint64_t c,
    void *col, size_t size);
int kernel_offloader_raidz_free(void *raidz);
int kernel_offloader_raidz_gen(void *raidz);
int kernel_offloader_raidz_rec(void *raidz, int *tgts, int ntgts);

/* io */
void *kernel_offloader_file_open(const char *path, int flags, int mode);
int kernel_offloader_file_write(void *fp_handle, void *handle, size_t count,
    size_t trailing_zeros, loff_t offset, ssize_t *resid, int *err);
void kernel_offloader_file_close(void *fp_handle);

void *kernel_offloader_disk_open(dpusm_dd_t *disk_data);
int kernel_offloader_disk_reread_part(void *disk_handle);
int kernel_offloader_disk_invalidate(void *disk_handle);
int kernel_offloader_disk_write(void *disk_handle, void *handle,
    size_t data_size, size_t trailing_zeros, uint64_t io_offset, int flags,
    dpusm_disk_write_completion_t write_completion, void *wc_args);
int kernel_offloader_disk_flush(void *disk_handle,
    dpusm_disk_flush_completion_t flush_completion, void *fc_args);
void kernel_offloader_disk_close(void *disk_handle);

#endif
