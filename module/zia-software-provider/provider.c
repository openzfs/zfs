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

/*
 * This provider communicates with the "kernel offloader", which is
 * actually just software running on the local kernel.
 *
 * Providers and offloaders are usually separate entities. However, to
 * keep things simple, the kernel offloader is compiled into this
 * provider.
 *
 * Providers run at the same location as ZFS. They are intended to be
 * small shims that translate between the DPUSM provider API and an
 * offloader's API (probably a header file analogous to
 * kernel_offloader.h).
 *
 * The method used to communicate between the provider and offloader
 * is not prescribed by the DPUSM. This allows for vendors to place
 * their offloaders locally or remotely, and use whatever method they
 * wish to use to communicate with their offloaders e.g. NVMeOF. The
 * kernel offloader is local and the communication method to access
 * the kernel offloader is calling local functions.
 *
 * Offloaders are normally expected to be hardware with its own memory
 * space. In order to simulate copying data to an offloader's memory
 * space, the kernel offloader allocates new buffers and copies ZFS
 * data into them, rather than using ZFS data directly. In order to
 * simulate handles that the provider does not know how to manipulate
 * or have access to, pointers returned from the kernel offloader are
 * masked with a random value.
 *
 * Note that this provider has to be loaded after ZFS because it
 * depends on ZFS for its "offload" functionality.
 *
 * Usage:
 *     1. Reconfigure ZFS with --with-zia=<DPUSM root>
 *
 *     2. Create a zpool
 *
 *     3. Select this provider with
 *            zpool set zia_provider=zia-software-provider <zpool>
 *
 *     4. Enable "offloading" of operations with
 *            zpool set zia_compress=on   <zpool>
 *            zpool set zia_decompress=on <zpool>
 *            zpool set zia_checksum=on   <zpool>
 *            zpool set zia_raidz1_gen=on <zpool>
 *            zpool set zia_raidz2_gen=on <zpool>
 *            zpool set zia_raidz3_gen=on <zpool>
 *            zpool set zia_raidz1_rec=on <zpool>
 *            zpool set zia_raidz2_rec=on <zpool>
 *            zpool set zia_raidz3_rec=on <zpool>
 *            zpool set zia_file_write=on <zpool>
 *            zpool set zia_disk_write=on <zpool>
 *
 *     5. Use the zpool as you would normally
 *
 *     Notes:
 *         If a ZFS IO stage is not run, enabling a Z.I.A. offload
 *         will have no effect.
 *
 *         Resilvering requires both zia_checksum and zia_raidz*_rec
 *         to be enabled. Not enabling checksums would cause offloaded
 *         resilvering to fail, and perform the remaining operations
 *         in memory. To avoid the cost of offloading data only to
 *         fail, a check has been inserted to prevent offloading
 *         altogether if zia_checksum is not enabled.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include <dpusm/provider_api.h> /* the DPUSM provider API */
#include <kernel_offloader.h>   /* provides access to the offloader */

/* translate from offloader values to DPUSM values */
static int
translate_rc(const int offloader_rc)
{
	int dpusm_rc = DPUSM_NOT_IMPLEMENTED;
	switch (offloader_rc) {
		case KERNEL_OFFLOADER_OK:
			dpusm_rc = DPUSM_OK;
			break;
		case KERNEL_OFFLOADER_ERROR:
			dpusm_rc = DPUSM_ERROR;
			break;
		case KERNEL_OFFLOADER_UNAVAILABLE:
			dpusm_rc = DPUSM_NOT_IMPLEMENTED;
			break;
		case KERNEL_OFFLOADER_BAD_RESULT:
			dpusm_rc = DPUSM_BAD_RESULT;
			break;
		case KERNEL_OFFLOADER_DOWN:
			dpusm_rc = DPUSM_PROVIDER_INVALIDATED;
			break;
		default:
			/* only translate recognized values */
			dpusm_rc = offloader_rc;
			break;
	}
	return (dpusm_rc);
}

static int
sw_provider_algorithms(int *compress, int *decompress,
    int *checksum, int *checksum_byteorder, int *raid)
{
	*compress =
	    DPUSM_COMPRESS_GZIP_1 |
	    DPUSM_COMPRESS_GZIP_2 |
	    DPUSM_COMPRESS_GZIP_3 |
	    DPUSM_COMPRESS_GZIP_4 |
	    DPUSM_COMPRESS_GZIP_5 |
	    DPUSM_COMPRESS_GZIP_6 |
	    DPUSM_COMPRESS_GZIP_7 |
	    DPUSM_COMPRESS_GZIP_8 |
	    DPUSM_COMPRESS_GZIP_9 |
	    DPUSM_COMPRESS_LZ4;

	*decompress = *compress;

	*checksum = DPUSM_CHECKSUM_FLETCHER_2 | DPUSM_CHECKSUM_FLETCHER_4;

	*checksum_byteorder = DPUSM_BYTEORDER_NATIVE | DPUSM_BYTEORDER_BYTESWAP;

	*raid =
	    DPUSM_RAID_1_GEN |
	    DPUSM_RAID_2_GEN |
	    DPUSM_RAID_3_GEN |
	    DPUSM_RAID_1_REC |
	    DPUSM_RAID_2_REC |
	    DPUSM_RAID_3_REC;

	return (DPUSM_OK);
}

static int
sw_provider_get_size(void *handle, size_t *size, size_t *actual)
{
	return (translate_rc(kernel_offloader_get_size(handle,
	    size, actual)));
}

static int
sw_provider_copy_from_generic(dpusm_mv_t *mv, const void *buf, size_t size)
{
	return (translate_rc(kernel_offloader_copy_from_generic(mv->handle,
	    mv->offset, buf, size)));
}

static int
sw_provider_copy_to_generic(dpusm_mv_t *mv, void *buf, size_t size)
{
	return (translate_rc(kernel_offloader_copy_to_generic(mv->handle,
	    mv->offset, buf, size)));
}

static int
sw_provider_mem_stats(size_t *t_count, size_t *t_size, size_t *t_actual,
    size_t *a_count, size_t *a_size, size_t *a_actual)
{
	void *t_count_handle = NULL;
	void *t_size_handle = NULL;
	void *t_actual_handle = NULL;
	void *a_size_handle  = NULL;
	void *a_count_handle = NULL;
	void *a_actual_handle  = NULL;

	if (t_count) {
		t_count_handle = kernel_offloader_alloc(sizeof (size_t));
	}

	if (t_size) {
		t_size_handle = kernel_offloader_alloc(sizeof (size_t));
	}

	if (t_actual) {
		t_actual_handle = kernel_offloader_alloc(sizeof (size_t));
	}

	if (a_count) {
		a_count_handle = kernel_offloader_alloc(sizeof (size_t));
	}

	if (a_size) {
		a_size_handle = kernel_offloader_alloc(sizeof (size_t));
	}

	if (a_actual) {
		a_actual_handle = kernel_offloader_alloc(sizeof (size_t));
	}

	const int rc = kernel_offloader_mem_stats(t_count, t_size, t_actual,
	    a_count, a_size, a_actual);
	if (rc == KERNEL_OFFLOADER_OK) {
		/* should probably check for errors */
		kernel_offloader_copy_to_generic(t_count_handle, 0,
		    t_count, sizeof (*t_count));
		kernel_offloader_copy_to_generic(t_size_handle, 0,
		    t_size, sizeof (*t_size));
		kernel_offloader_copy_to_generic(t_actual_handle, 0,
		    t_actual, sizeof (*t_actual));
		kernel_offloader_copy_to_generic(a_count_handle, 0,
		    a_count, sizeof (*a_count));
		kernel_offloader_copy_to_generic(a_size_handle, 0,
		    a_size, sizeof (*a_size));
		kernel_offloader_copy_to_generic(a_actual_handle, 0,
		    a_actual, sizeof (*a_actual));
	}

	kernel_offloader_free(t_size_handle);
	kernel_offloader_free(t_count_handle);
	kernel_offloader_free(t_actual_handle);
	kernel_offloader_free(a_size_handle);
	kernel_offloader_free(a_count_handle);
	kernel_offloader_free(a_actual_handle);

	return (translate_rc(rc));
}

static int
sw_provider_zero_fill(void *handle, size_t offset, size_t size)
{
	return (translate_rc(kernel_offloader_zero_fill(handle, offset, size)));
}

static int
sw_provider_all_zeros(void *handle, size_t offset, size_t size)
{
	return (translate_rc(kernel_offloader_all_zeros(handle, offset, size)));
}

static int
sw_provider_compress(dpusm_compress_t alg, int level,
    void *src, size_t s_len, void *dst, size_t *d_len)
{
	/* buffer that offloader fills out */
	void *d_len_handle = kernel_offloader_alloc(sizeof (size_t));

	/* send original d_len to offloader */
	kernel_offloader_copy_from_generic(d_len_handle, 0,
	    d_len, sizeof (*d_len));

	const int kz_rc = kernel_offloader_compress(alg, level,
	    src, s_len, dst, d_len_handle);
	if (kz_rc == KERNEL_OFFLOADER_OK) {
		/* get updated d_len back from offloader */
		kernel_offloader_copy_to_generic(d_len_handle, 0,
		    d_len, sizeof (*d_len));
	}

	kernel_offloader_free(d_len_handle);

	return (translate_rc(kz_rc));
}

static int
sw_provider_decompress(dpusm_compress_t alg, int *level,
    void *src, size_t s_len, void *dst, size_t *d_len)
{
	/* buffers that offloader fills out */
	void *level_handle = kernel_offloader_alloc(sizeof (*level));
	void *d_len_handle = kernel_offloader_alloc(sizeof (*d_len));

	/* send original d_len to offloader */
	kernel_offloader_copy_from_generic(d_len_handle, 0,
	    d_len, sizeof (*d_len));

	const int kz_rc = kernel_offloader_decompress(alg, level_handle,
	    src, s_len, dst, d_len_handle);
	if (kz_rc == KERNEL_OFFLOADER_OK) {
		/* get updated d_len back from offloader */
		kernel_offloader_copy_to_generic(d_len_handle, 0,
		    d_len, sizeof (*d_len));
		kernel_offloader_copy_to_generic(level_handle, 0,
		    level, sizeof (*level));
	}

	kernel_offloader_free(d_len_handle);
	kernel_offloader_free(level_handle);

	return (translate_rc(kz_rc));
}

static int
sw_provider_checksum(dpusm_checksum_t alg,
    dpusm_checksum_byteorder_t order, void *data, size_t size,
    void *cksum, size_t cksum_size)
{
	/* maybe translate alg and order */

	/* trigger offloader to do actual calculation */
	return (translate_rc(kernel_offloader_checksum(alg,
	    order, data, size, cksum, cksum_size)));
}

static int
sw_provider_raid_can_compute(size_t nparity, size_t ndata,
    size_t *col_sizes, int rec)
{
	if ((nparity < 1) || (nparity > 3)) {
		return (DPUSM_NOT_SUPPORTED);
	}

	return (DPUSM_OK);
}

static int
sw_provider_raid_gen(void *raid)
{
	return (translate_rc(kernel_offloader_raidz_gen(raid)));
}

static int
sw_provider_raid_cmp(void *lhs_handle, void *rhs_handle, int *diff)
{
	return (translate_rc(kernel_offloader_cmp(lhs_handle,
	    rhs_handle, diff)));
}

static int
sw_provider_raid_rec(void *raid, int *tgts, int ntgts)
{
	return (translate_rc(kernel_offloader_raidz_rec(raid,
	    tgts, ntgts)));
}

static int
sw_provider_file_write(void *fp_handle, void *handle, size_t count,
    size_t trailing_zeros, loff_t offset, ssize_t *resid, int *err)
{
	return (translate_rc(kernel_offloader_file_write(fp_handle,
	    handle, count, trailing_zeros, offset, resid, err)));
}

/* BEGIN CSTYLED */
static const char name[] = "zia-software-provider";
static const dpusm_pf_t sw_provider_functions = {
	.algorithms           = sw_provider_algorithms,
	.alloc                = kernel_offloader_alloc,
	.alloc_ref            = kernel_offloader_alloc_ref,
	.get_size             = sw_provider_get_size,
	.free                 = kernel_offloader_free,
	.copy                 = {
	                            .from = {
	                                        .generic      = sw_provider_copy_from_generic,
	                                        .ptr          = NULL,
	                                        .scatterlist  = NULL,
	                                    },
	                            .to   = {
	                                        .generic      = sw_provider_copy_to_generic,
	                                        .ptr          = NULL,
	                                        .scatterlist  = NULL,
	                                    },
	                        },
	.mem_stats            = sw_provider_mem_stats,
	.zero_fill            = sw_provider_zero_fill,
	.all_zeros            = sw_provider_all_zeros,
	.compress             = sw_provider_compress,
	.decompress           = sw_provider_decompress,
	.checksum             = sw_provider_checksum,
	.raid                 = {
	                            .can_compute = sw_provider_raid_can_compute,
	                            .alloc       = kernel_offloader_raidz_alloc,
	                            .set_column  = kernel_offloader_raidz_set_column,
	                            .free        = kernel_offloader_raidz_free,
	                            .gen         = sw_provider_raid_gen,
	                            .cmp         = sw_provider_raid_cmp,
	                            .rec         = sw_provider_raid_rec,
	                        },
	.file                 = {
	                            .open        = kernel_offloader_file_open,
	                            .write       = sw_provider_file_write,
	                            .close       = kernel_offloader_file_close,
	                        },
	.disk                 = {
	                            .open        = kernel_offloader_disk_open,
	                            .invalidate  = kernel_offloader_disk_invalidate,
	                            .write       = kernel_offloader_disk_write,
	                            .flush       = kernel_offloader_disk_flush,
	                            .close       = kernel_offloader_disk_close,
	                        },
};
/* END CSTYLED */

static int __init
sw_provider_init(void)
{
	/*
	 * this should be a separate kernel module,
	 * but is here for simplicity
	 */
	kernel_offloader_init();

	return (dpusm_register_bsd(name, &sw_provider_functions));
}

static void __exit
sw_provider_exit(void)
{
	dpusm_unregister_bsd(name);

	kernel_offloader_fini();
}

module_init(sw_provider_init);
module_exit(sw_provider_exit);

MODULE_LICENSE("CDDL");
