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

#include <linux/kernel.h>
#include <linux/random.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <sys/abd.h>
#include <sys/spa_checksum.h>
#include <sys/vdev_disk.h>
#include <sys/vdev_raidz.h>
#include <sys/vdev_raidz_impl.h>
#include <sys/zfs_file.h>
#include <sys/zio.h>
#include <sys/zmod.h>
#include <zfs_fletcher.h>

#include "kernel_offloader.h"

static const char NAME[] = "Kernel Offloader";
static const size_t NAME_LEN = sizeof (NAME);

typedef enum kernel_offloader_handle_type {
	KOH_REAL,	/* default type - convert all data into a single blob */
	KOH_REFERENCE,

	KOH_INVALID,
} koht_t;

/* offloaded data (not defined outside of "hardware") */
typedef struct kernel_offloader_handle {
	koht_t type;
	void *ptr;
	size_t size;
} koh_t;

/* **************************************** */
/* memory bookkeeping */
rwlock_t rwlock;			 /* atomic ints are not big enough */

/* never decreases */
static size_t total_count;   /* number of times alloc/alloc_ref was called */
static size_t total_size;    /* buffer size  */
static size_t total_actual;  /* buffer size + any extra memory */

/* currently active */
static size_t active_count;  /* number of times alloc/alloc_ref was called */
static size_t active_size;   /* buffer size */
static size_t active_actual; /* buffer size + any extra memory */
/* **************************************** */

/* **************************************** */
/* set kernel offloader to DOWN state */
typedef struct kernel_offloader_down {
    rwlock_t rwlock;
    int count;
    int max;
    int printed;
} kod_t;

#define	kod_init(name, max_val)                         \
	do {                                                \
		rwlock_init(&name.rwlock);                      \
		name.count = 0;                                 \
		name.max = max_val;                             \
		name.printed = 0;                               \
	} while (0)

#define	kod_inc(name)                                   \
	do {                                                \
		write_lock(&name.rwlock);                       \
		name.count++;                                   \
		write_unlock(&name.rwlock);                     \
	} while (0)

#define	kod_ret(name)                                   \
	do {                                                \
		if (name.max) {                                 \
			write_lock(&name.rwlock);                   \
			if (name.count > name.max) {                \
				if (!name.printed) {                    \
					printk("%s\n", #name);              \
					name.printed = 1;                   \
				}                                       \
				write_unlock(&name.rwlock);             \
				return (DPUSM_PROVIDER_INVALIDATED);    \
			}                                           \
			write_unlock(&name.rwlock);                 \
		}                                               \
	} while (0)

#define	kod_run(name)                                   \
	do {                                                \
		kod_inc(name);                                  \
		kod_ret(name);                                  \
	} while (0)

/* can probably do with macros */
static kod_t copy_from_generic_down; static int copy_from_generic_down_max = 0;
module_param(copy_from_generic_down_max, int, 0660);

static kod_t copy_to_generic_down; static int copy_to_generic_down_max = 0;
module_param(copy_to_generic_down_max, int, 0660);

static kod_t cmp_down; static int cmp_down_max = 0;
module_param(cmp_down_max, int, 0660);

static kod_t compress_down; static int compress_down_max = 0;
module_param(compress_down_max, int, 0660);

static kod_t checksum_down; static int checksum_down_max = 0;
module_param(checksum_down_max, int, 0660);

static kod_t raidz_gen_down; static int raidz_gen_down_max = 0;
module_param(raidz_gen_down_max, int, 0660);

static kod_t raidz_rec_down; static int raidz_rec_down_max = 0;
module_param(raidz_rec_down_max, int, 0660);

static kod_t disk_write_down; static int disk_write_down_max = 0;
module_param(disk_write_down_max, int, 0660);
/* **************************************** */

/*
 * value used to swizzle the pointer so that
 * dereferencing the handle will fail
 */
static void *mask = NULL;
void
kernel_offloader_init(void)
{
	get_random_bytes(&mask, sizeof (mask));
	rwlock_init(&rwlock);
	total_count = 0;
	total_size = 0;
	total_actual = 0;
	active_count = 0;
	active_size = 0;
	active_actual = 0;

	kod_init(copy_from_generic_down, copy_from_generic_down_max);
	kod_init(copy_to_generic_down, copy_to_generic_down_max);
	kod_init(cmp_down, cmp_down_max);
	kod_init(compress_down, compress_down_max);
	kod_init(checksum_down, checksum_down_max);
	kod_init(raidz_gen_down, raidz_gen_down_max);
	kod_init(raidz_rec_down, raidz_rec_down_max);
	kod_init(disk_write_down, disk_write_down_max);

	printk("kernel offloader init: %p\n", mask);
}

void
kernel_offloader_fini(void)
{
	mask = NULL;

	printk("kernel offloader fini with "
	    "%zu/%zu (actual %zu/%zu) bytes "
	    "in %zu/%zu allocations remaining\n",
	    active_size, total_size,
	    active_actual, total_actual,
	    active_count, total_count);
}

/* get a starting address of a linear koh_t */
static void *
ptr_start(koh_t *koh, size_t offset)
{
	return (void *)(((uintptr_t)koh->ptr) + offset);
}

/*
 * convert the actual pointer to a handle (pretend
 * the data is not accessible from the Z.I.A. base)
 */
static void *
swizzle(void *ptr)
{
	return (ptr?((void *)(((uintptr_t)ptr) ^ ((uintptr_t)mask))):NULL);
}

/* convert the handle to a usable pointer */
static void *
unswizzle(void *handle)
{
	return (swizzle(handle));
}

static koh_t *
koh_alloc(size_t size)
{
	koh_t *koh = kmalloc(sizeof (koh_t), GFP_KERNEL);
	if (koh) {
		koh->type = KOH_REAL;
		koh->ptr = kmalloc(size, GFP_KERNEL);
		koh->size = size;

		write_lock(&rwlock);
		total_count++;
		active_count++;

		/* the allocation itself */
		total_size += size;
		active_size += size;
		total_actual += size;
		active_actual += size;

		/* the wrapper struct */
		total_actual += sizeof (koh_t);
		active_actual += sizeof (koh_t);

		write_unlock(&rwlock);
	}

	return (koh);
}

static koh_t *
koh_alloc_ref(koh_t *src, size_t offset, size_t size)
{
	koh_t *ref = NULL;
	if (src) {
		koh_t *src_koh = (koh_t *)src;

		if ((offset + size) > src_koh->size) {
			printk("Error: Cannot reference handle of size %zu "
			    "starting at offset %zu with size %zu\n",
			    src_koh->size, offset, size);
			return (NULL);
		}

		ref = kmalloc(sizeof (koh_t), GFP_KERNEL);
		if (ref) {
			ref->type = KOH_REFERENCE;
			ref->ptr = ptr_start(src, offset);
			ref->size = size;

			write_lock(&rwlock);
			total_count++;
			active_count++;

			/* no new requested space */

			/* the wrapper struct */
			total_actual += sizeof (koh_t);
			active_actual += sizeof (koh_t);
			write_unlock(&rwlock);
		}
	}

	return (ref);
}

int
kernel_offloader_get_size(void *handle, size_t *size, size_t *actual)
{
	koh_t *koh = (koh_t *)unswizzle(handle);

	if (size) {
		*size = koh->size;
	}

	if (actual) {
		*actual = koh->size;
	}

	return (KERNEL_OFFLOADER_OK);
}

static int
koh_free(koh_t *koh)
{
	if (koh) {
		write_lock(&rwlock);
		switch (koh->type) {
			case KOH_REAL:
				/* the allocation itself */
				active_size -= koh->size;
				active_actual -= koh->size;
				kfree(koh->ptr);
				break;
			case KOH_REFERENCE:
			case KOH_INVALID:
			default:
				break;
		}

		/* the wrapper struct */
		active_actual -= sizeof (koh_t);

		active_count--;
		write_unlock(&rwlock);

		kfree(koh);
	}

	return (KERNEL_OFFLOADER_OK);
}

void *
kernel_offloader_alloc(size_t size)
{
	return (swizzle(koh_alloc(size)));
}

void *
kernel_offloader_alloc_ref(void *src_handle, size_t offset, size_t size)
{
	return swizzle(koh_alloc_ref(unswizzle(src_handle),
	    offset, size));
}

int
kernel_offloader_free(void *handle)
{
	koh_free(unswizzle(handle));
	return (DPUSM_OK);
}

int
kernel_offloader_copy_from_generic(void *handle, size_t offset,
    const void *src, size_t size)
{
	koh_t *koh = (koh_t *)unswizzle(handle);
	if (!koh) {
		return (KERNEL_OFFLOADER_ERROR);
	}

	if ((offset + size) > koh->size) {
		return (KERNEL_OFFLOADER_ERROR);
	}

	kod_run(copy_from_generic_down);

	void *dst = ptr_start(koh, offset);
	if (memcpy(dst, src, size) != dst) {
		return (KERNEL_OFFLOADER_ERROR);
	}
	return (KERNEL_OFFLOADER_OK);
}

int
kernel_offloader_copy_to_generic(void *handle, size_t offset,
    void *dst, size_t size)
{
	koh_t *koh = (koh_t *)unswizzle(handle);
	if (!koh) {
		return (KERNEL_OFFLOADER_ERROR);
	}

	if ((offset + size) > koh->size) {
		return (KERNEL_OFFLOADER_ERROR);
	}

	kod_run(copy_to_generic_down);

	if (memcpy(dst, ptr_start(koh, offset), size) != dst) {
		return (KERNEL_OFFLOADER_ERROR);
	}

	return (KERNEL_OFFLOADER_OK);
}

int
kernel_offloader_cmp(void *lhs_handle, void *rhs_handle, int *diff)
{
	koh_t *lhs = (koh_t *)unswizzle(lhs_handle);
	koh_t *rhs = (koh_t *)unswizzle(rhs_handle);

	if (!lhs || !rhs || !diff) {
		return (KERNEL_OFFLOADER_ERROR);
	}

	kod_run(cmp_down);

	size_t len = rhs->size;
	if (lhs->size != rhs->size) {
		len =
		    (lhs->size < rhs->size)?lhs->size:rhs->size;
	}

	*diff = memcmp(ptr_start(lhs, 0),
	    ptr_start(rhs, 0), len);

	return (KERNEL_OFFLOADER_OK);
}

int
kernel_offloader_zero_fill(void *handle, size_t offset, size_t size)
{
	koh_t *koh = (koh_t *)unswizzle(handle);
	memset(ptr_start(koh, offset), 0, size);
	return (KERNEL_OFFLOADER_OK);
}

int
kernel_offloader_all_zeros(void *handle, size_t offset, size_t size)
{
	koh_t *koh = (koh_t *)unswizzle(handle);
	if (koh->size - offset < size) {
		return (KERNEL_OFFLOADER_ERROR);
	}

	uint64_t *array = ptr_start(koh, offset);
	size_t i;
	for (i = 0; i < size / sizeof (uint64_t); i++) {
		if (array[i]) {
			return (KERNEL_OFFLOADER_BAD_RESULT);
		}
	}

	char *remaining = ptr_start(koh, offset);
	for (i *= sizeof (uint64_t); i < size; i++) {
		if (remaining[i]) {
			return (KERNEL_OFFLOADER_BAD_RESULT);
		}
	}

	return (KERNEL_OFFLOADER_OK);
}

int
kernel_offloader_mem_stats(
    void *t_count_handle, void *t_size_handle, void *t_actual_handle,
    void *a_count_handle, void *a_size_handle, void *a_actual_handle)
{
	read_lock(&rwlock);

	if (t_count_handle) {
		*(size_t *)ptr_start(t_count_handle, 0) =
		    total_count;
	}

	if (t_size_handle) {
		*(size_t *)ptr_start(t_size_handle, 0) =
		    total_size;
	}

	if (t_actual_handle) {
		*(size_t *)ptr_start(t_actual_handle, 0) =
		    total_actual;
	}

	if (a_count_handle) {
		*(size_t *)ptr_start(a_count_handle, 0) =
		    active_count;
	}

	if (a_size_handle) {
		*(size_t *)ptr_start(a_size_handle, 0) =
		    active_size;
	}

	if (a_actual_handle) {
		*(size_t *)ptr_start(a_actual_handle, 0) =
		    active_actual;
	}

	read_unlock(&rwlock);

	return (KERNEL_OFFLOADER_OK);
}

/* specific implementation */
static int
kernel_offloader_gzip_compress(koh_t *src, size_t s_len,
    koh_t *dst, size_t *d_len, int level)
{
	if (z_compress_level(ptr_start(dst, 0), d_len,
	    ptr_start(src, 0), s_len, level) != Z_OK) {
		if (*d_len != src->size) {
			return (KERNEL_OFFLOADER_ERROR);
		}
		return (KERNEL_OFFLOADER_OK);
	}

	return (KERNEL_OFFLOADER_OK);
}

static int
kernel_offloader_lz4_compress(koh_t *src, koh_t *dst,
    size_t s_len, int level, size_t *c_len)
{
	*c_len = dst->size;

	if (zfs_lz4_compress_buf(ptr_start(src, 0), ptr_start(dst, 0),
	    s_len, *c_len, level) == s_len) {
		return (KERNEL_OFFLOADER_ERROR);
	}

	return (KERNEL_OFFLOADER_OK);
}

int
kernel_offloader_compress(dpusm_compress_t alg, int level,
    void *src, size_t s_len, void *dst, void *d_len)
{
	int status = KERNEL_OFFLOADER_UNAVAILABLE;
	koh_t *src_koh   = NULL;
	koh_t *dst_koh   = NULL;
	koh_t *d_len_koh = NULL;
	if (!src || !dst || !d_len) {
		return (KERNEL_OFFLOADER_ERROR);
	}

	kod_run(compress_down);

	src_koh   = (koh_t *)unswizzle(src);
	dst_koh   = (koh_t *)unswizzle(dst);
	d_len_koh = (koh_t *)unswizzle(d_len);

	if ((DPUSM_COMPRESS_GZIP_1 <= alg) &&
	    (alg <= DPUSM_COMPRESS_GZIP_9)) {
		status = kernel_offloader_gzip_compress(src_koh, s_len,
		    dst_koh, (size_t *)ptr_start(d_len_koh, 0), level);
	} else if (alg == DPUSM_COMPRESS_LZ4) {
		status = kernel_offloader_lz4_compress(src_koh, dst_koh, s_len,
		    level, (size_t *)ptr_start(d_len_koh, 0));
	}

	return (status);
}

/* specific implementation */
static int
kernel_offloader_gzip_decompress(koh_t *src, size_t s_len,
    koh_t *dst, size_t *d_len, int level)
{
	if (z_uncompress(ptr_start(dst, 0), d_len,
	    ptr_start(src, 0), s_len) != Z_OK) {
		return (KERNEL_OFFLOADER_ERROR);
	}

	return (KERNEL_OFFLOADER_OK);
}

static int
kernel_offloader_lz4_decompress(koh_t *src, size_t s_len,
    koh_t *dst, size_t *d_len, int level)
{
	if (zfs_lz4_decompress_buf(ptr_start(src, 0), ptr_start(dst, 0),
	    s_len, *d_len, level) != 0) {
		return (KERNEL_OFFLOADER_ERROR);
	}

	return (KERNEL_OFFLOADER_OK);
}

int
kernel_offloader_decompress(dpusm_decompress_t alg, void *level,
    void *src, size_t s_len, void *dst, void *d_len)
{
	int status = KERNEL_OFFLOADER_UNAVAILABLE;
	koh_t *level_koh = NULL;
	koh_t *src_koh   = NULL;
	koh_t *dst_koh   = NULL;
	koh_t *d_len_koh = NULL;
	if (!level || !src || !dst || !d_len) {
		return (KERNEL_OFFLOADER_ERROR);
	}

	level_koh = (koh_t *)unswizzle(level);
	src_koh   = (koh_t *)unswizzle(src);
	dst_koh   = (koh_t *)unswizzle(dst);
	d_len_koh = (koh_t *)unswizzle(d_len);

	if ((DPUSM_COMPRESS_GZIP_1 <= alg) &&
	    (alg <= DPUSM_COMPRESS_GZIP_9)) {
		status = kernel_offloader_gzip_decompress(src_koh, s_len,
		    dst_koh, (size_t *)ptr_start(d_len_koh, 0),
		    *(int *)ptr_start(level_koh, 0));
	} else if (alg == DPUSM_COMPRESS_LZ4) {
		status = kernel_offloader_lz4_decompress(src_koh, s_len,
		    dst_koh, (size_t *)ptr_start(d_len_koh, 0),
		    *(int *)ptr_start(level_koh, 0));
	}

	return (status);
}

int
kernel_offloader_checksum(dpusm_checksum_t alg,
    dpusm_checksum_byteorder_t order, void *data, size_t size,
    void *cksum, size_t cksum_size)
{
	koh_t *data_koh = (koh_t *)unswizzle(data);
	if (!data_koh) {
		return (KERNEL_OFFLOADER_ERROR);
	}

	zio_cksum_t zcp;
	if (cksum_size < sizeof (zcp.zc_word)) {
		return (KERNEL_OFFLOADER_ERROR);
	}

	kod_run(checksum_down);

	/* compute checksum */

	void *buf = ptr_start(data_koh, 0);

	if (alg == DPUSM_CHECKSUM_FLETCHER_2) {
		fletcher_init(&zcp);
		if (order == DPUSM_BYTEORDER_NATIVE) {
			fletcher_2_native(buf, size, NULL, &zcp);
		} else {
			fletcher_2_byteswap(buf, size, NULL, &zcp);
		}
	} else if (alg == DPUSM_CHECKSUM_FLETCHER_4) {
		fletcher_init(&zcp);
		if (order == DPUSM_BYTEORDER_NATIVE) {
			fletcher_4_native(buf, size, NULL, &zcp);
		} else {
			fletcher_4_byteswap(buf, size, NULL, &zcp);
		}
	} else {
		return (DPUSM_NOT_SUPPORTED);
	}

	memcpy(cksum, zcp.zc_word, sizeof (zcp.zc_word));

	return (DPUSM_OK);
}

void *
kernel_offloader_raidz_alloc(size_t nparity, size_t ndata)
{
	const size_t ncols = nparity + ndata;

	const size_t rr_size = offsetof(raidz_row_t, rr_col[ncols]);
	raidz_row_t *rr = kzalloc(rr_size, GFP_KERNEL);
	rr->rr_cols = ncols;
	rr->rr_firstdatacol = nparity;

	write_lock(&rwlock);
	total_count++;
	active_count++;

	/* the op struct does not contribute to buffer allocations */
	total_actual += rr_size;
	active_actual += rr_size;

	write_unlock(&rwlock);

	return (swizzle(rr));
}

/* attaches a column to the raidz struct */
int
kernel_offloader_raidz_set_column(void *raidz, uint64_t c,
    void *col, size_t size)
{
	raidz_row_t *rr = (raidz_row_t *)unswizzle(raidz);
	koh_t *koh = (koh_t *)unswizzle(col);

	if (!rr || !koh) {
		return (DPUSM_ERROR);
	}

	/* c is too big */
	if (c >= rr->rr_cols) {
		return (DPUSM_ERROR);
	}

	/* "active" size is larger than allocated size */
	if (size > koh->size) {
		return (DPUSM_ERROR);
	}

	raidz_col_t *rc = &rr->rr_col[c];

	/* clean up old column */
	abd_free(rc->rc_abd);

	/*
	 * rc->rc_abd does not take ownership of koh->ptr,
	 * so don't need to release ownership
	 */
	rc->rc_abd = abd_get_from_buf(koh->ptr, size);
	rc->rc_size = size;

	return (DPUSM_OK);
}

int
kernel_offloader_raidz_free(void *raidz)
{
	raidz_row_t *rr = (raidz_row_t *)unswizzle(raidz);
	if (!rr) {
		return (DPUSM_ERROR);
	}

	for (int c = 0; c < rr->rr_cols; c++) {
		raidz_col_t *rc = &rr->rr_col[c];
		abd_free(rc->rc_abd);
	}
	kfree(rr);

	const size_t rr_size = offsetof(raidz_row_t, rr_col[rr->rr_cols]);

	write_lock(&rwlock);
	active_count--;
	active_actual -= rr_size;
	write_unlock(&rwlock);

	return (DPUSM_OK);
}

int
kernel_offloader_raidz_gen(void *raidz)
{
	raidz_row_t *rr = (raidz_row_t *)unswizzle(raidz);
	if (!rr) {
		return (KERNEL_OFFLOADER_ERROR);
	}

	kod_run(raidz_gen_down);

	switch (rr->rr_firstdatacol) {
		case 1:
			vdev_raidz_generate_parity_p(rr);
			break;
		case 2:
			vdev_raidz_generate_parity_pq(rr);
			break;
		case 3:
			vdev_raidz_generate_parity_pqr(rr);
			break;
	}

	return (KERNEL_OFFLOADER_OK);
}

int
kernel_offloader_raidz_rec(void *raidz, int *tgts, int ntgts)
{
	raidz_row_t *rr = (raidz_row_t *)unswizzle(raidz);
	if (!rr) {
		return (KERNEL_OFFLOADER_ERROR);
	}

	kod_run(raidz_rec_down);

	vdev_raidz_reconstruct_general(rr, tgts, ntgts);

	return (KERNEL_OFFLOADER_OK);
}

void *
kernel_offloader_file_open(const char *path, int flags, int mode)
{
	zfs_file_t *fp = NULL;
	/* on error, fp should still be NULL */
	zfs_file_open(path, flags, mode, &fp);
	return (swizzle(fp));
}

int
kernel_offloader_file_write(void *fp_handle, void *handle, size_t count,
    size_t trailing_zeros, loff_t offset, ssize_t *resid, int *err)
{
	zfs_file_t *fp = (zfs_file_t *)unswizzle(fp_handle);
	if (!fp) {
		return (ENODEV);
	}

	koh_t *koh = (koh_t *)unswizzle(handle);
	if (!koh) {
		return (EIO);
	}

	if (!err) {
		return (EIO);
	}

	*err = zfs_file_pwrite(fp, ptr_start(koh, 0),
	    count, offset, resid);

	if (*err == 0) {
		void *zeros = kzalloc(trailing_zeros, GFP_KERNEL);
		*err = zfs_file_pwrite(fp, zeros,
		    trailing_zeros, offset + count, resid);
		kfree(zeros);
	}

	return (*err);
}

void
kernel_offloader_file_close(void *fp_handle)
{
	zfs_file_close(unswizzle(fp_handle));
}

void *
kernel_offloader_disk_open(dpusm_dd_t *disk_data)
{
	return (swizzle(disk_data->bdev));
}

int
kernel_offloader_disk_invalidate(void *disk_handle)
{
	struct block_device *bdev =
	    (struct block_device *)unswizzle(disk_handle);
	invalidate_bdev(bdev);
	return (DPUSM_OK);
}

int
kernel_offloader_disk_write(void *disk_handle, void *handle, size_t data_size,
    size_t trailing_zeros, uint64_t io_offset, int flags,
    dpusm_disk_write_completion_t write_completion, void *wc_args)
{
	struct block_device *bdev =
	    (struct block_device *)unswizzle(disk_handle);
	koh_t *koh = (koh_t *)unswizzle(handle);

	const size_t io_size = data_size + trailing_zeros;

	kod_run(disk_write_down);

	if (trailing_zeros) {
		/* create a copy of the data with the trailing zeros attached */
		void *copy = kzalloc(io_size, GFP_KERNEL);
		memcpy(copy, ptr_start(koh, 0), data_size);

		write_lock(&rwlock);
		/* need to keep copy alive, so replace koh->ptr */
		if (koh->type == KOH_REAL) {
			/* subtract size of original koh->ptr */
			active_size -= koh->size;
			active_actual -= koh->size;

			kfree(koh->ptr);
		}

		koh->type = KOH_REAL;
		koh->ptr = copy;
		koh->size = io_size;

		total_size += io_size;
		active_size += io_size;
		total_actual += io_size;
		active_actual += io_size;

		/* wrapper struct size was not modified */
		write_unlock(&rwlock);
	}

	abd_t *abd = abd_get_from_buf(koh->ptr, io_size);
	zio_push_transform(wc_args, abd, io_size, io_size, NULL);

	/* __vdev_disk_physio already adds write_completion */
	(void) write_completion;

	return (__vdev_classic_physio(bdev, wc_args,
	    io_size, io_offset, WRITE, flags));
}

int
kernel_offloader_disk_flush(void *disk_handle,
    dpusm_disk_flush_completion_t flush_completion, void *fc_args)
{
	struct block_device *bdev =
	    (struct block_device *)unswizzle(disk_handle);

	/* vdev_disk_io_flush already adds flush completion */
	(void) flush_completion;

	return (vdev_disk_io_flush(bdev, fc_args));
}

void
kernel_offloader_disk_close(void *disk_handle)
{}
