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

#include <sys/abd.h>
#include <sys/abd_impl.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_disk.h>
#include <sys/vdev_file.h>
#include <sys/vdev_raidz_impl.h>
#include <sys/zia.h>
#include <sys/zia_cddl.h>
#include <sys/zia_private.h>

#ifdef ZIA
#include <dpusm/user_api.h>
#else
typedef void * dpusm_uf_t;
#endif

/* ************************************************************* */
/* global offloader functions initialized with ZFS */
static const dpusm_uf_t *dpusm = NULL;
/* ************************************************************* */

zia_props_t *
zia_get_props(spa_t *spa)
{
	return (spa?&spa->spa_zia_props:NULL);
}

void
zia_prop_warn(boolean_t val, const char *name)
{
#ifdef _KERNEL
	if (val == B_TRUE) {
		printk("Z.I.A. %s enabled. Encryption and "
		    "Dedup for this spa will be disabled.\n",
		    name);
	}
#else
	(void) val; (void) name;
#endif
}

int
dpusm_to_ret(const int dpusm_ret)
{
#ifdef ZIA
	int zia_ret = ZIA_FALLBACK;
	switch (dpusm_ret) {
		case DPUSM_OK:
			zia_ret = ZIA_OK;
			break;
		case DPUSM_ERROR:
			zia_ret = ZIA_ERROR;
			break;
		case DPUSM_PROVIDER_MISMATCH:
			zia_ret = ZIA_PROVIDER_MISMATCH;
			break;
		case DPUSM_NOT_IMPLEMENTED:
		case DPUSM_NOT_SUPPORTED:
			zia_ret = ZIA_FALLBACK;
			break;
		case DPUSM_BAD_RESULT:
			zia_ret = ZIA_BAD_RESULT;
			break;
		case DPUSM_PROVIDER_NOT_EXISTS:
		case DPUSM_PROVIDER_INVALIDATED:
		case DPUSM_PROVIDER_UNREGISTERED:
		default:
			zia_ret = ZIA_ACCELERATOR_DOWN;
			break;
	}
	return (zia_ret);
#else
	(void) dpusm_ret;
	return (ZIA_DISABLED);
#endif
}

#ifdef ZIA
dpusm_compress_t
compress_to_dpusm(enum zio_compress c)
{
	dpusm_compress_t dpusm_c = 0;

	switch (c) {
		case ZIO_COMPRESS_GZIP_1:
			dpusm_c = DPUSM_COMPRESS_GZIP_1;
			break;
		case ZIO_COMPRESS_GZIP_2:
			dpusm_c = DPUSM_COMPRESS_GZIP_2;
			break;
		case ZIO_COMPRESS_GZIP_3:
			dpusm_c = DPUSM_COMPRESS_GZIP_3;
			break;
		case ZIO_COMPRESS_GZIP_4:
			dpusm_c = DPUSM_COMPRESS_GZIP_4;
			break;
		case ZIO_COMPRESS_GZIP_5:
			dpusm_c = DPUSM_COMPRESS_GZIP_5;
			break;
		case ZIO_COMPRESS_GZIP_6:
			dpusm_c = DPUSM_COMPRESS_GZIP_6;
			break;
		case ZIO_COMPRESS_GZIP_7:
			dpusm_c = DPUSM_COMPRESS_GZIP_7;
			break;
		case ZIO_COMPRESS_GZIP_8:
			dpusm_c = DPUSM_COMPRESS_GZIP_8;
			break;
		case ZIO_COMPRESS_GZIP_9:
			dpusm_c = DPUSM_COMPRESS_GZIP_9;
			break;
		case ZIO_COMPRESS_LZ4:
			dpusm_c = DPUSM_COMPRESS_LZ4;
			break;
		case ZIO_COMPRESS_INHERIT:
		case ZIO_COMPRESS_ON:
		case ZIO_COMPRESS_OFF:
		case ZIO_COMPRESS_LZJB:
		case ZIO_COMPRESS_EMPTY:
		case ZIO_COMPRESS_ZLE:
		case ZIO_COMPRESS_ZSTD:
		case ZIO_COMPRESS_FUNCTIONS:
		default:
			break;
	}

	return (dpusm_c);
}

static dpusm_checksum_t
checksum_to_dpusm(enum zio_checksum c)
{
	dpusm_checksum_t dpusm_c = 0;
	switch (c) {
		case ZIO_CHECKSUM_FLETCHER_2:
			dpusm_c = DPUSM_CHECKSUM_FLETCHER_2;
			break;
		case ZIO_CHECKSUM_FLETCHER_4:
			dpusm_c = DPUSM_CHECKSUM_FLETCHER_4;
			break;
		case ZIO_CHECKSUM_INHERIT:
		case ZIO_CHECKSUM_ON:
		case ZIO_CHECKSUM_OFF:
		case ZIO_CHECKSUM_LABEL:
		case ZIO_CHECKSUM_GANG_HEADER:
		case ZIO_CHECKSUM_ZILOG:
		case ZIO_CHECKSUM_SHA256:
		case ZIO_CHECKSUM_ZILOG2:
		case ZIO_CHECKSUM_NOPARITY:
		case ZIO_CHECKSUM_SHA512:
		case ZIO_CHECKSUM_SKEIN:
		default:
			break;
	}

	return (dpusm_c);
}

static dpusm_checksum_byteorder_t
byteorder_to_dpusm(zio_byteorder_t bo)
{
	dpusm_checksum_byteorder_t dpusm_bo = DPUSM_BYTEORDER_MAX;
	switch (bo) {
		case ZIO_CHECKSUM_NATIVE:
			dpusm_bo = DPUSM_BYTEORDER_NATIVE;
			break;
		case ZIO_CHECKSUM_BYTESWAP:
			dpusm_bo = DPUSM_BYTEORDER_BYTESWAP;
			break;
		default:
			break;
	}

	return (dpusm_bo);
}
#endif

#ifdef ZIA
int
zia_get_capabilities(void *provider, dpusm_pc_t **caps)
{
	/* dpusm is checked by the caller */
	/* provider and caps are checked by the dpusm */
	return (dpusm_to_ret(dpusm->capabilities(provider, caps)));
}
#endif

int
zia_init(void)
{
#ifdef ZIA
	if (dpusm) {
		return (ZIA_OK);
	}

	if (dpusm_initialize) {
		dpusm = dpusm_initialize();
	}

	if (!dpusm) {
#ifdef _KERNEL
		printk("Warning: Z.I.A. not initialized\n");
#endif
		return (ZIA_ERROR);
	}

#ifdef _KERNEL
	printk("Z.I.A. initialized (%p)\n", dpusm);
#endif
	return (ZIA_OK);
#else
	return (ZIA_DISABLED);
#endif
}

int
zia_fini(void)
{
	if (!dpusm) {
#ifdef _KERNEL
		printk("Warning: Z.I.A. not initialized. "
		    "Not uninitializing.\n");
#endif
		return (ZIA_ERROR);
	}

#ifdef ZIA
	if (dpusm_finalize) {
		dpusm_finalize();
#ifdef _KERNEL
		printk("Z.I.A. finalized\n");
#endif
	} else {
#ifdef _KERNEL
		if (dpusm) {
			printk("Z.I.A. incomplete finalize\n");
		}
#endif
	}
#endif

	dpusm = NULL;
	return (ZIA_OK);
}

#ifdef ZIA
/* recursively find all leaf vdevs and open them */
static void zia_open_vdevs(vdev_t *vd) {
	vdev_ops_t *ops = vd->vdev_ops;
	if (ops->vdev_op_leaf) {
		ASSERT(!vd->vdev_zia_handle);

		const size_t len = strlen(ops->vdev_op_type);
		if (len == 4) {
			if (memcmp(ops->vdev_op_type, "file", 4) == 0) {
				zia_file_open(vd, vd->vdev_path,
				    vdev_file_open_mode(spa_mode(vd->vdev_spa)),
				    0);
			}
#ifdef _KERNEL
			else if (memcmp(ops->vdev_op_type, "disk", 4) == 0) {
				/* first member is struct block_device * */
				void *disk = vd->vdev_tsd;
				zia_disk_open(vd, vd->vdev_path, disk);
			}
#endif
		}
	} else {
		for (uint64_t i = 0; i < vd->vdev_children; i++) {
			vdev_t *child = vd->vdev_child[i];
			zia_open_vdevs(child);
		}
	}
}
#endif

void *
zia_get_provider(const char *name, vdev_t *vdev)
{
#ifdef ZIA
	if (!dpusm) {
		return (NULL);
	}

	void *provider = NULL;
	provider = dpusm->get(name);
#ifdef _KERNEL
	printk("Z.I.A. obtained handle to provider \"%s\" (%p)",
	    name, provider);
#endif

	/* set up Z.I.A. for existing vdevs */
	if (vdev) {
		zia_open_vdevs(vdev);
	}
	return (provider);
#else
	(void) name; (void) vdev;
	return (NULL);
#endif
}

const char *
zia_get_provider_name(void *provider)
{
	if (!dpusm || !provider) {
		return (NULL);
	}

#ifdef ZIA
	return (dpusm->get_name(provider));
#else
	return (NULL);
#endif
}

#ifdef ZIA
/* recursively find all leaf vdevs and close them */
static void zia_close_vdevs(vdev_t *vd) {
	vdev_ops_t *ops = vd->vdev_ops;
	if (ops->vdev_op_leaf) {
		const size_t len = strlen(ops->vdev_op_type);
		if (len == 4) {
			if (memcmp(ops->vdev_op_type, "file", 4) == 0) {
				zia_file_close(vd);
			}
#ifdef _KERNEL
			else if (memcmp(ops->vdev_op_type, "disk", 4) == 0) {
				zia_disk_close(vd);
			}
#endif
		}
	} else {
		for (uint64_t i = 0; i < vd->vdev_children; i++) {
			vdev_t *child = vd->vdev_child[i];
			zia_close_vdevs(child);
		}
	}
}
#endif

int
zia_put_provider(void **provider, vdev_t *vdev)
{
#ifdef ZIA
	if (!dpusm || !provider || !*provider) {
		return (ZIA_FALLBACK);
	}

	/*
	 * if the zpool is not going down, but the provider is going away,
	 * make sure the vdevs don't keep pointing to the invalid provider
	 */
	if (vdev) {
		zia_close_vdevs(vdev);
	}

#ifdef _KERNEL
	const char *name = zia_get_provider_name(*provider);
#endif

	const int ret = dpusm->put(*provider);

#ifdef _KERNEL
	printk("Z.I.A. returned provider handle \"%s\" "
	    "(%p) and got return value %d",
	    name, *provider, ret);
#endif

	*provider = NULL;

	return (dpusm_to_ret(ret));
#else
	(void) provider; (void) vdev;
	return (ZIA_DISABLED);
#endif
}

int
zia_disable_offloading(zio_t *zio, boolean_t reexecute)
{
	if (!zio) {
		return (ZIA_ERROR);
	}

	/* stop all future zios from offloading */
	spa_t *spa = zio->io_spa;
	zia_props_t *zia_props = zia_get_props(spa);
	mutex_enter(&spa->spa_props_lock);
	zia_props->can_offload = B_FALSE;
	mutex_exit(&spa->spa_props_lock);

	/* stop this zio from offloading again */
	zio->io_can_offload = B_FALSE;

	if (reexecute == B_TRUE) {
		zio->io_flags |= ZIO_FLAG_ZIA_REEXECUTE;
	}

	return (ZIA_OK);
}

boolean_t
zia_is_used(zio_t *zio)
{
	if (!zio) {
		return (B_FALSE);
	}

	zia_props_t *props = zia_get_props(zio->io_spa);

	/* provider + at least 1 operation */
	if (props->provider &&
	    (props->compress ||
	    props->decompress ||
	    props->checksum ||
	    props->raidz.gen[1] ||
	    props->raidz.gen[2] ||
	    props->raidz.gen[3] ||
	    props->raidz.rec[1] ||
	    props->raidz.rec[2] ||
	    props->raidz.rec[3] ||
	    props->file_write ||
	    props->disk_write)) {
		return (B_TRUE);
	}

	return (B_FALSE);
}

boolean_t
zia_is_offloaded(abd_t *abd)
{
	if (!abd) {
		return (B_FALSE);
	}

	return (ABD_HANDLE(abd)?B_TRUE:B_FALSE);
}

int
zia_worst_error(const int lhs, const int rhs)
{
	if (lhs == ZIA_ACCELERATOR_DOWN) {
		return (lhs);
	}

	if (rhs == ZIA_ACCELERATOR_DOWN) {
		return (rhs);
	}

	if (lhs == ZIA_OK) {
		return (rhs);
	}

	if (rhs == ZIA_OK) {
		return (lhs);
	}

	return (ZIA_ERROR);
}

/* create a provider handle/offloader buffer without copying data */
void *
zia_alloc(void *provider, size_t size, size_t min_offload_size)
{
#ifdef ZIA
	if (size < min_offload_size) {
		return (NULL);
	}

	return ((dpusm && provider)?dpusm->alloc(provider, size):NULL);
#else
	(void) provider; (void) size; (void) min_offload_size;
	return (NULL);
#endif
}

/* free the offloader handle without onloading the data */
int
zia_free(void **handle)
{
	ASSERT(handle);

#ifdef ZIA
	int ret = DPUSM_OK;
	if (dpusm) {
		ret = dpusm->free(*handle);
		*handle = NULL;
	}
	return (dpusm_to_ret(ret));
#else
	return (ZIA_DISABLED);
#endif
}

/* move data from the offloader into a linear abd and unregister the mapping */
int
zia_onload(void **handle, void *buf, size_t size)
{
#ifdef ZIA
	if (!dpusm) {
		return (ZIA_FALLBACK);
	}

	if (!handle || !*handle || !buf) {
		return (ZIA_ERROR);
	}

	void *provider = dpusm->extract(*handle);
	if (!provider) {
		return (ZIA_ERROR);
	}

	dpusm_pc_t *caps = NULL;
	if (zia_get_capabilities(provider, &caps) != ZIA_OK) {
		return (ZIA_ERROR);
	}

	dpusm_mv_t mv = { .handle = *handle, .offset = 0 };
	int ret = ZIA_ERROR;

	if (caps->optional & DPUSM_OPTIONAL_COPY_TO_PTR) {
		ret = dpusm_to_ret(dpusm->copy.to.ptr(&mv, buf, size));
	} else {
		ret = dpusm_to_ret(dpusm->copy.to.generic(&mv, buf, size));
	}

	/*
	 * if success, no more need for handle
	 * if failure, can't do anything with
	 * handle in any case, so destroy it
	 */
	zia_free(handle);

	return (dpusm_to_ret(ret));
#else
	(void) handle; (void) buf; (void) size;
	return (ZIA_DISABLED);
#endif
}

#ifdef ZIA
static int
zia_offload_generic_cb(void *buf, size_t len, void *priv)
{
	dpusm_mv_t *mv = (dpusm_mv_t *)priv;

	const int ret = dpusm->copy.from.generic(mv, buf, len);
	if (dpusm_to_ret(ret) != ZIA_OK) {
		return (ZIA_ERROR);
	}

	mv->offset += len;
	return (0);
}
#endif

/* offload abd + offset to handle + 0 */
static int
zia_offload_abd_offset(void *provider, abd_t *abd,
    size_t offset, size_t size,
    size_t min_offload_size, boolean_t *local_offload)
{
#ifdef ZIA
	/* already offloaded */
	if (ABD_HANDLE(abd)) {
		if (local_offload) {
			*local_offload = B_FALSE;
		}

		if (!provider) {
			return (ZIA_OK);
		}

		void *abd_provider = dpusm->extract(ABD_HANDLE(abd));
		return ((provider == abd_provider)?
		    ZIA_OK:ZIA_PROVIDER_MISMATCH);
	}

	dpusm_pc_t *caps = NULL;
	if (zia_get_capabilities(provider, &caps) != ZIA_OK) {
		return (ZIA_ERROR);
	}

	if (local_offload) {
		*local_offload = B_TRUE;
	}

	/* provider is checked by dpusm */
	void *handle = zia_alloc(provider, size, min_offload_size);
	if (!handle) {
		return (ZIA_ERROR);
	}

	dpusm_mv_t mv = {
		.handle = handle,
		.offset = 0,
	};

	int ret = ZIA_FALLBACK;
	if (abd_is_linear(abd) == B_TRUE) {
		ret = dpusm->copy.from.generic(&mv,
		    ABD_LINEAR_BUF(abd), size);
		ret = dpusm_to_ret(ret);
	} else {
		ret = abd_iterate_func(abd, offset, size,
		    zia_offload_generic_cb, &mv);

		if (ret == 0) {
			ret = ZIA_OK;
		}
	}

	if (ret == ZIA_OK) {
		ABD_HANDLE(abd) = handle;
	} else {
		zia_free(&handle);
	}

	return (ret);
#else
	(void) provider; (void) abd; (void) offset;
	(void) size; (void) min_offload_size;
	(void) local_offload;
	return (ZIA_DISABLED);
#endif
}

int
zia_offload_abd(void *provider, abd_t *abd,
    size_t size, size_t min_offload_size, boolean_t *local_offload)
{
	if (!dpusm) {
		return (ZIA_FALLBACK);
	}

	if (!abd) {
		return (ZIA_ERROR);
	}

	return (zia_offload_abd_offset(provider,
	    abd, 0, size, min_offload_size, local_offload));
}

#ifdef ZIA
static int
zia_onload_generic_cb(void *buf, size_t len, void *priv)
{
	dpusm_mv_t *mv = (dpusm_mv_t *)priv;

	const int ret = dpusm->copy.to.generic(mv, buf, len);
	if (dpusm_to_ret(ret) != ZIA_OK) {
		return (ZIA_ERROR);
	}

	mv->offset += len;
	return (0);
}
#endif

/* onload handle + 0 into abd + offset */
static int
zia_onload_abd_offset(abd_t *abd, size_t offset,
    size_t size, boolean_t keep_handle)
{
#ifdef ZIA
	if (!dpusm) {
		return (ZIA_FALLBACK);
	}

	if (!abd) {
		return (ZIA_ERROR);
	}

	void *handle = ABD_HANDLE(abd);
	if (!handle) {
		return (ZIA_ERROR);
	}

	void *provider = dpusm->extract(handle);
	if (!provider) {
		mutex_exit(&abd->abd_mtx);
		return (ZIA_ERROR);
	}

	dpusm_pc_t *caps = NULL;
	if (zia_get_capabilities(provider, &caps) != ZIA_OK) {
		mutex_exit(&abd->abd_mtx);
		return (ZIA_ERROR);
	}

	dpusm_mv_t mv = {
		.handle = handle,
		.offset = 0,
	};

	int ret = ZIA_FALLBACK;
	if (abd_is_linear(abd) == B_TRUE) {
		ret = dpusm->copy.to.generic(&mv,
		    ABD_LINEAR_BUF(abd), size);
		ret = dpusm_to_ret(ret);
	} else {
		ret = abd_iterate_func(abd, offset, size,
		    zia_onload_generic_cb, &mv);

		if (ret == 0) {
			ret = ZIA_OK;
		}
	}

	if (keep_handle != B_TRUE) {
		zia_free_abd(abd, B_FALSE);
	}

	return (ret);
#else
	(void) abd; (void) offset; (void) size; (void) keep_handle;
	return (ZIA_DISABLED);
#endif
}

int
zia_onload_abd(abd_t *abd, size_t size, boolean_t keep_handle)
{
	if (abd_is_gang(abd)) {
		/*
		 * the only gangs that show up are from raidz
		 *
		 * get leading data size, stopping at first zero page
		 * which should always be the second child
		 */
		const size_t original_size = size;
		size = 0;
		for (abd_t *child = list_head(&ABD_GANG(abd).abd_gang_chain);
		    child != NULL;
		    child = list_next(&ABD_GANG(abd).abd_gang_chain, child)) {
			if (child->abd_flags & ABD_FLAG_ZEROS) {
				break;
			}

			size += child->abd_size;
		}

		ASSERT(size <= original_size);
	}

	return (zia_onload_abd_offset(abd, 0, size, keep_handle));
}

void
zia_move_into_abd(abd_t *dst, void **src_handle)
{
	ABD_HANDLE(dst) = *src_handle;
	*src_handle = NULL;
}

int
zia_free_abd(abd_t *abd, boolean_t lock)
{
	if (lock == B_TRUE) {
		mutex_enter(&abd->abd_mtx);
	}

	const int ret = zia_free(&ABD_HANDLE(abd));

	if (lock == B_TRUE) {
		mutex_exit(&abd->abd_mtx);
	}
	return (ret);
}

/*
 * if offloaded locally, just free the handle
 * if not, onload the data and free the handle
 */
int
zia_cleanup_abd(abd_t *abd, size_t size, boolean_t local_offload)
{
	if (!dpusm) {
		return (ZIA_FALLBACK);
	}

	if (!abd) {
		return (ZIA_ERROR);
	}

	int ret = ZIA_OK;
	if (local_offload == B_TRUE) {
		/* in-memory copy is still valid */
		/* lock just in case mirrors clean up at the same time */
		ret = zia_free_abd(abd, B_FALSE);
	} else {
		/* have to copy data into memory */
		ret = zia_onload_abd(abd, size, B_FALSE);
	}

	return (ret);
}

void
zia_restart_before_vdev(zio_t *zio)
{
	blkptr_t *bp = zio->io_bp;

	if (BP_IS_ENCRYPTED(bp) &&
	    (zio->io_stage != ZIO_STAGE_ENCRYPT)) {
		zio_pop_transform(zio);
	}

	if ((BP_GET_COMPRESS(bp) != ZIO_COMPRESS_OFF) &&
	    (zio->io_stage != ZIO_STAGE_WRITE_COMPRESS)) {
		zio_pop_transform(zio);
		BP_SET_PSIZE(bp, zio->io_size);
		BP_SET_LSIZE(bp, zio->io_size);
		BP_SET_COMPRESS(bp, ZIO_COMPRESS_OFF);
	}

	zia_disable_offloading(zio, B_TRUE);

	/* only keep trace up to issue async */
	zio->io_pipeline_trace &=
	    ZIO_STAGE_OPEN |
	    ZIO_STAGE_READ_BP_INIT |
	    ZIO_STAGE_WRITE_BP_INIT |
	    ZIO_STAGE_FREE_BP_INIT |
	    ZIO_STAGE_ISSUE_ASYNC;

	/* let zio_execute find the stage after issue async */
	zio->io_stage = ZIO_STAGE_ISSUE_ASYNC;
}

int
zia_zero_fill(abd_t *abd, size_t offset, size_t size)
{
#ifdef ZIA
	if (!dpusm) {
		return (ZIA_FALLBACK);
	}

	if (!abd || !ABD_HANDLE(abd)) {
		return (ZIA_ERROR);
	}

	return (dpusm_to_ret(dpusm->zero_fill(ABD_HANDLE(abd), offset, size)));
#else
	(void) abd; (void) offset; (void) size;
	return (ZIA_DISABLED);
#endif
}

int
zia_compress(zia_props_t *props, enum zio_compress c,
    abd_t *src, size_t s_len,
    void **cbuf_handle, uint64_t *c_len,
    uint8_t level, boolean_t *local_offload)
{
#ifdef ZIA
	if (!dpusm) {
		return (ZIA_FALLBACK);
	}

	return (zia_compress_impl(dpusm, props, c, src, s_len,
	    cbuf_handle, c_len, level, local_offload));
#else
	(void) props; (void) c; (void) src; (void) s_len;
	(void) cbuf_handle; (void) c_len; (void) level;
	(void) local_offload;
	return (ZIA_DISABLED);
#endif
}

int
zia_decompress(zia_props_t *props, enum zio_compress c,
    abd_t *src, size_t s_len, abd_t *dst, size_t d_len,
    uint8_t *level)
{
#ifdef ZIA
	if (!props) {
		return (ZIA_ERROR);
	}

	if (!dpusm || !props->provider) {
		return (ZIA_FALLBACK);
	}

	/* check that decompression can be done before offloading src */
	dpusm_pc_t *caps = NULL;
	if ((zia_get_capabilities(props->provider, &caps) != ZIA_OK) ||
	    !(caps->decompress & compress_to_dpusm(c))) {
		return (ZIA_FALLBACK);
	}

	int ret = zia_offload_abd(props->provider, src,
	    s_len, props->min_offload_size, NULL);
	if (ret != ZIA_OK) {
		return (ret);
	}

	/*
	 * allocate space for decompressed data
	 *
	 * a lot of these will fail because d_len tends to be small
	 */
	ABD_HANDLE(dst) = zia_alloc(props->provider, d_len,
	    props->min_offload_size);
	if (!ABD_HANDLE(dst)) {
		/* let abd_free clean up zio->io_abd */
		return (ZIA_ERROR);
	}

	/*
	 * d_len pulled from accelerator is not used, so
	 * passing in address of local variable is fine
	 */
	int cmp_level = *level;
	ret = dpusm->decompress(compress_to_dpusm(c), &cmp_level,
	    ABD_HANDLE(src), s_len, ABD_HANDLE(dst), &d_len);
	*level = cmp_level;

	if (ret != DPUSM_OK) {
		zia_free_abd(dst, B_FALSE);
		/* let abd_free clean up zio->io_abd */
	}
	return (dpusm_to_ret(ret));
#else
	(void) props; (void) c; (void) src; (void) s_len;
	(void) dst; (void) d_len; (void) level;
	return (ZIA_FALLBACK);
#endif
}

int
zia_checksum_compute(void *provider, zio_cksum_t *dst, enum zio_checksum alg,
    zio_t *zio, uint64_t size, boolean_t *local_offload)
{
#ifdef ZIA
	if (!dpusm || !provider) {
		return (ZIA_FALLBACK);
	}

	const dpusm_checksum_byteorder_t byteorder =
	    byteorder_to_dpusm(BP_SHOULD_BYTESWAP(zio->io_bp));

	if (!ABD_HANDLE(zio->io_abd)) {
		dpusm_pc_t *caps = NULL;
		if ((zia_get_capabilities(provider, &caps) != ZIA_OK) ||
		    !(caps->checksum & checksum_to_dpusm(alg)) ||
		    !(caps->checksum_byteorder & byteorder)) {
			return (ZIA_FALLBACK);
		}

		if (zia_offload_abd(provider, zio->io_abd, size,
		    zia_get_props(zio->io_spa)->min_offload_size,
		    local_offload) != ZIA_OK) {
			return (ZIA_ERROR);
		}
	} else {
		void *old_provider = dpusm->extract(ABD_HANDLE(zio->io_abd));
		if (old_provider != provider) {
			return (ZIA_PROVIDER_MISMATCH);
		}

		/* skip checks because dpusm will do them */
	}

	return (dpusm_to_ret(dpusm->checksum(checksum_to_dpusm(alg),
	    byteorder, ABD_HANDLE(zio->io_abd), size, dst->zc_word,
	    sizeof (dst->zc_word))));
#else
	(void) provider; (void) dst; (void) alg;
	(void) zio; (void) size; (void) local_offload;
	return (ZIA_FALLBACK);
#endif
}

int
zia_checksum_error(enum zio_checksum alg, abd_t *abd,
    uint64_t size, int byteswap, zio_cksum_t *actual_cksum)
{
#ifdef ZIA
	if (!dpusm) {
		return (ZIA_FALLBACK);
	}

	if (!abd || !actual_cksum) {
		return (ZIA_ERROR);
	}

	if (!ABD_HANDLE(abd)) {
		return (ZIA_FALLBACK);
	}

	const dpusm_checksum_byteorder_t byteorder =
	    byteorder_to_dpusm(byteswap);

	return (dpusm_to_ret(dpusm->checksum(checksum_to_dpusm(alg),
	    byteorder, ABD_HANDLE(abd), size, actual_cksum->zc_word,
	    sizeof (actual_cksum->zc_word))));
#else
	(void) alg; (void) abd; (void) size;
	(void) byteswap; (void) actual_cksum;
	return (ZIA_FALLBACK);
#endif
}

#ifdef ZIA
static boolean_t
zia_can_raidz(raidz_row_t *rr, zia_props_t *props, uint64_t raidn,
    boolean_t rec, uint_t cksum, size_t *col_sizes)
{
	/*
	 * generation is needed for both
	 * generation and reconstruction
	 */
	int good = (
	    /* raidz generation is turned on */
	    (props->raidz.gen[raidn] == 1) &&

		/*
		 * the provider knows whether or not
		 * raidz functions are available
		 */
	    (dpusm->raid.can_compute(props->provider, raidn,
	    rr->rr_cols - rr->rr_firstdatacol,
	    col_sizes, rec == B_TRUE) == DPUSM_OK));

	if (good && (rec == B_TRUE)) {
		dpusm_pc_t *caps = NULL;
		if (zia_get_capabilities(props->provider, &caps) != ZIA_OK) {
			return (B_FALSE);
		}

		good &= (
		    /* raidz reconstruction is turned on */
		    (props->raidz.rec[raidn] == 1) &&

		    /* need checksum */
		    (props->checksum == 1) &&

		    /* raidz reconstruction support was checked earlier */

		    /* make sure the checksum is supported by the provider */
		    (caps->checksum & checksum_to_dpusm(cksum)));
	}
	return (good?B_TRUE:B_FALSE);
}
#endif

/* onload abd and delete raidz_row_t stuff */
static int
zia_raidz_cleanup(zio_t *zio, raidz_row_t *rr,
    boolean_t local_offload, boolean_t onload_parity)
{
	/*
	 * bring data back to zio->io_abd, which should
	 * place data into parent automatically
	 */

	mutex_enter(&zio->io_abd->abd_mtx);
	const int ret = zia_worst_error(
	    zia_raidz_free(rr, onload_parity),
	    zia_cleanup_abd(zio->io_abd, zio->io_size, local_offload));
	mutex_exit(&zio->io_abd->abd_mtx);

	return (ret);
}

int
zia_raidz_alloc(zio_t *zio, raidz_row_t *rr, boolean_t rec,
    uint_t cksum, boolean_t *local_offload)
{
#ifdef ZIA
	if (!dpusm) {
		return (ZIA_FALLBACK);
	}

	if (!zio || !rr) {
		return (ZIA_ERROR);
	}

	/* do not offload in the middle of resilvering */
	if (zio->io_flags & ZIO_FLAG_RESILVER) {
		if (!ABD_HANDLE(zio->io_abd)) {
			return (ZIA_FALLBACK);
		}
	}

	/*
	 * existence of row handle implies existence
	 * of data and column handles
	 */
	if (rr->rr_zia_handle) {
		return (ZIA_OK);
	}

	if (zio->io_can_offload != B_TRUE) {
		return (ZIA_ACCELERATOR_DOWN);
	}

	const uint64_t raidn = rr->rr_firstdatacol;
	if ((1 > raidn) || (raidn > 3)) {
		return (ZIA_ERROR);
	}

	/* need at least raidn + 2 columns */
	if (raidn + 2 > rr->rr_cols) {
		return (ZIA_ERROR);
	}

	zia_props_t *props = zia_get_props(zio->io_spa);

	/* get column sizes */
	const size_t column_sizes_size = sizeof (size_t) * rr->rr_cols;
	size_t *column_sizes = kmem_alloc(column_sizes_size, KM_SLEEP);
	for (uint64_t c = 0; c < rr->rr_cols; c++) {
		raidz_col_t *rc = &rr->rr_col[c];

		/* this is tied to the ashift, not to the accelerator */
		if (rc->rc_abd->abd_size < props->min_offload_size) {
			kmem_free(column_sizes, column_sizes_size);
			return (ZIA_FALLBACK);
		}

		column_sizes[c] = rc->rc_size;
	}

	if (zia_can_raidz(rr, props, raidn, rec,
	    cksum, column_sizes) != B_TRUE) {
		kmem_free(column_sizes, column_sizes_size);
		return (ZIA_FALLBACK);
	}
	kmem_free(column_sizes, column_sizes_size);

	void *provider = props->provider;
	if (!provider) {
		return (ZIA_FALLBACK);
	}

	/*
	 * offload the source data if it hasn't already been offloaded
	 *
	 * need to lock here since offloading normally doesn't lock, but
	 * abds hitting raidz might have been mirrored
	 */
	mutex_enter(&zio->io_abd->abd_mtx);
	const int ret = zia_offload_abd(provider, zio->io_abd,
	    zio->io_size, props->min_offload_size, local_offload);
	mutex_exit(&zio->io_abd->abd_mtx);
	if (ret != ZIA_OK) {
		return (ret);
	}

	/* mirrored abds generate their own references to the columns */

	/* set up raid context */
	rr->rr_zia_handle = dpusm->raid.alloc(provider,
	    raidn, rr->rr_cols - raidn);

	if (!rr->rr_zia_handle) {
		return (ZIA_ERROR);
	}

	/* fill in raid context */

	/* create parity column handles */
	for (uint64_t c = 0; c < raidn; c++) {
		raidz_col_t *rc = &rr->rr_col[c];

		ASSERT(!ABD_HANDLE(rc->rc_abd));

		void *handle = NULL;

		/* allocate rc->rc_abd->abd_size, mark as rc->rc_size */
		if (rec == B_TRUE) {
			/*
			 * reconstructing - parity columns are not
			 * in zio->io_abd - offload rc->rc_abd
			 */
			zia_offload_abd(provider, rc->rc_abd,
			    rc->rc_abd->abd_size, props->min_offload_size,
			    NULL);
			handle = ABD_HANDLE(rc->rc_abd);
		} else {
			/* generating - create new columns */
			handle =
			    dpusm->alloc(provider, rc->rc_abd->abd_size);
		}

		if (!handle) {
			goto error;
		}

		if (dpusm->raid.set_column(rr->rr_zia_handle,
		    c, handle, rc->rc_size) != DPUSM_OK) {
			goto error;
		}

		ABD_HANDLE(rc->rc_abd) = handle;
	}

	/*
	 * recalculate data column offsets and
	 * create references for each column
	 */
	uint64_t offset = 0;
	for (uint64_t c = raidn; c < rr->rr_cols; c++) {
		raidz_col_t *rc = &rr->rr_col[c];

		/*
		 * if the column is a gang abd, the handle
		 * will point to the first child
		 */
		void *handle = dpusm->alloc_ref(ABD_HANDLE(zio->io_abd),
		    offset, rc->rc_size);

		if (!handle) {
			goto error;
		}

		if (dpusm->raid.set_column(rr->rr_zia_handle,
		    c, handle, rc->rc_size) != DPUSM_OK) {
			goto error;
		}

		ABD_HANDLE(rc->rc_abd) = handle;
		offset += rc->rc_size;
	}

	for (uint64_t c = 0; c < rr->rr_cols; c++) {
		raidz_col_t *rc = &rr->rr_col[c];
		ASSERT(zia_is_offloaded(rc->rc_abd) == B_TRUE);
	}
	ASSERT(rr->rr_zia_handle);
	ASSERT(zia_is_offloaded(zio->io_abd) == B_TRUE);

	return (ZIA_OK);

error:
	zia_raidz_cleanup(zio, rr, local_offload?*local_offload:B_FALSE,
	    B_FALSE);

	for (uint64_t c = 0; c < rr->rr_cols; c++) {
		raidz_col_t *rc = &rr->rr_col[c];
		ASSERT(zia_is_offloaded(rc->rc_abd) == B_FALSE);
	}
	ASSERT(rr->rr_zia_handle == NULL);
	ASSERT(zia_is_offloaded(zio->io_abd) == B_FALSE);

	return (ZIA_ERROR);
#else
	(void) zio; (void) rr; (void) rec;
	(void) cksum; (void) local_offload;
	return (ZIA_FALLBACK);
#endif

}

/*
 * only frees the raidz data
 * onload the data separately if it is needed
 */
int
zia_raidz_free(raidz_row_t *rr, boolean_t onload_parity)
{
#ifdef ZIA
	if (!dpusm) {
		return (ZIA_FALLBACK);
	}

	if (!rr) {
		return (ZIA_ERROR);
	}

	if (!rr->rr_zia_handle) {
		return (ZIA_FALLBACK);
	}

	int ret = ZIA_OK;
	uint64_t c = 0;

	if (onload_parity == B_TRUE) {
		for (; c < rr->rr_firstdatacol; c++) {
			raidz_col_t *rc = &rr->rr_col[c];
			ret = zia_worst_error(ret,
			    zia_onload_abd(rc->rc_abd,
			    rc->rc_size, B_FALSE));
		}
	}

	for (; c < rr->rr_cols; c++) {
		raidz_col_t *rc = &rr->rr_col[c];
		ret = zia_worst_error(ret,
		    zia_free_abd(rc->rc_abd, B_FALSE));
	}

	ret = zia_worst_error(ret,
	    dpusm_to_ret(dpusm->raid.free(
	    rr->rr_zia_handle)));
	rr->rr_zia_handle = NULL;

	return (ret);
#else
	(void) rr; (void) onload_parity;
	return (ZIA_FALLBACK);
#endif
}

int
zia_raidz_gen(raidz_row_t *rr)
{
#ifdef ZIA
	if (!dpusm) {
		return (ZIA_FALLBACK);
	}

	/* can only pass if raidz_alloc succeeded */
	if (!rr->rr_zia_handle) {
		return (ZIA_ERROR);
	}

	return (dpusm_to_ret(dpusm->raid.gen(rr->rr_zia_handle)));
#else
	(void) rr;
	return (ZIA_FALLBACK);
#endif
}

int
zia_raidz_gen_cleanup(zio_t *zio, raidz_row_t *rr,
    boolean_t local_offload)
{
	/*
	 * RAIDZ generation only calls cleanup
	 * on failure, so parity does not need
	 * to be brought back.
	 */
	return (zia_raidz_cleanup(zio, rr,
	    local_offload, B_FALSE));
}

/*
 * allocate new parity columns for this row
 * and assign them to the raidz struct
 *
 * orig takes ownership of the original handles
 */
int
zia_raidz_new_parity(zio_t *zio, raidz_row_t *rr, uint64_t c)
{
#ifdef ZIA
	if (!zio || !rr || (c >= rr->rr_firstdatacol)) {
		return (ZIA_ERROR);
	}

	if (!ABD_HANDLE(zio->io_abd) || !rr->rr_zia_handle) {
		return (ZIA_FALLBACK);
	}

	zia_props_t *props = zia_get_props(zio->io_spa);
	void *provider = props->provider;
	if (!provider) {
		return (ZIA_FALLBACK);
	}

	raidz_col_t *rc = &rr->rr_col[c];
	if (ABD_HANDLE(rc->rc_abd)) {
		return (ZIA_ERROR);
	}

	void *new_parity_handle = zia_alloc(provider,
	    rc->rc_abd->abd_size, props->min_offload_size);
	if (!new_parity_handle) {
		return (ZIA_ERROR);
	}

	const int ret = dpusm->raid.set_column(rr->rr_zia_handle,
	    c, new_parity_handle, rc->rc_size);
	if (ret == DPUSM_OK) {
		ABD_HANDLE(rc->rc_abd) = new_parity_handle;
	} else {
		zia_free(&new_parity_handle);
	}

	return (dpusm_to_ret(ret));
#else
	(void) zio; (void) rr; (void) c;
	return (ZIA_FALLBACK);
#endif
}

int
zia_raidz_cmp(abd_t *lhs, abd_t *rhs, int *diff)
{
#ifdef ZIA
	if (!lhs || !rhs || !diff) {
		return (ZIA_ERROR);
	}

	if (lhs == rhs) {
		*diff = 0;
		return (ZIA_OK);
	}

	void *lhs_handle = ABD_HANDLE(lhs);
	void *rhs_handle = ABD_HANDLE(rhs);
	if (!lhs_handle || !rhs_handle) {
		return (ZIA_ERROR);
	}

	return (dpusm_to_ret(dpusm->raid.cmp(lhs_handle, rhs_handle, diff)));
#else
	(void) lhs; (void) rhs; (void) diff;
	return (ZIA_FALLBACK);
#endif
}

int
zia_raidz_rec(raidz_row_t *rr, int *t, int nt)
{
#ifdef ZIA
	if (!dpusm) {
		return (ZIA_FALLBACK);
	}

	/* can only pass if raidz_alloc succeeded */
	if (!rr->rr_zia_handle) {
		return (ZIA_FALLBACK);
	}

	return (dpusm_to_ret(zia_raidz_rec_impl(dpusm, rr, t, nt)));
#else
	(void) rr; (void) t; (void) nt;
	return (ZIA_FALLBACK);
#endif
}

int
zia_raidz_rec_cleanup(zio_t *zio, raidz_row_t *rr,
    boolean_t local_offload, boolean_t onload_parity)
{
	return (zia_raidz_cleanup(zio, rr,
	    local_offload, onload_parity));
}

int
zia_file_open(vdev_t *vdev, const char *path,
    int flags, int mode)
{
	if (!vdev || !vdev->vdev_spa) {
		return (ZIA_ERROR);
	}

#ifdef ZIA
	void *provider = zia_get_props(vdev->vdev_spa)->provider;
	if (!dpusm || !provider) {
		return (ZIA_FALLBACK);
	}

	if (!VDEV_HANDLE(vdev)) {
		VDEV_HANDLE(vdev) = dpusm->file.open(provider,
		    path, flags, mode);
	}

	return (VDEV_HANDLE(vdev)?ZIA_OK:ZIA_ERROR);
#else
	(void) path; (void) flags; (void) mode;
	return (ZIA_FALLBACK);
#endif
}

int
zia_file_write(vdev_t *vdev, abd_t *abd, ssize_t size,
    loff_t offset, ssize_t *resid, int *err)
{
#ifdef ZIA
	if (!vdev || !abd) {
		return (ZIA_ERROR);
	}

	if (!dpusm || !VDEV_HANDLE(vdev) || !ABD_HANDLE(abd)) {
		return (ZIA_FALLBACK);
	}

	if (!abd_is_linear(abd)) {
		return (EIO);
	}

	/*
	 * this was intended to handle gang abds, but breaking
	 * at first zero child abd was not correct
	 */
	size_t data_size = size;
	size_t trailing_zeros = 0;

	return (dpusm->file.write(VDEV_HANDLE(vdev),
	    ABD_HANDLE(abd), data_size, trailing_zeros, offset, resid, err));
#else
	(void) vdev; (void) abd; (void) size;
	(void) offset; (void) resid; (void) err;
	return (ZIA_FALLBACK);
#endif
}

int
zia_file_close(vdev_t *vdev)
{
#ifdef ZIA
	if (!vdev) {
		return (ZIA_ERROR);
	}

	if (!dpusm || !VDEV_HANDLE(vdev)) {
		return (ZIA_FALLBACK);
	}

	dpusm->file.close(VDEV_HANDLE(vdev));
	VDEV_HANDLE(vdev) = NULL;
	zia_get_props(vdev->vdev_spa)->min_offload_size = 0;

	return (ZIA_OK);
#else
	(void) vdev;
	return (ZIA_FALLBACK);
#endif
}

#ifdef _KERNEL
int
zia_disk_open(vdev_t *vdev, const char *path,
    struct block_device *bdev)
{
#ifdef ZIA
	if (!vdev || !vdev->vdev_spa) {
		return (ZIA_ERROR);
	}

	void *provider = zia_get_props(vdev->vdev_spa)->provider;
	if (!dpusm || !provider) {
		return (ZIA_FALLBACK);
	}

	if (!VDEV_HANDLE(vdev)) {
		VDEV_HANDLE(vdev) = dpusm->disk.open(provider,
		    path, bdev);
	}

	return (VDEV_HANDLE(vdev)?ZIA_OK:ZIA_ERROR);
#else
	(void) vdev; (void) path; (void) bdev;
	return (ZIA_FALLBACK);
#endif
}

int
zia_disk_invalidate(vdev_t *vdev)
{
#ifdef ZIA
	if (!vdev) {
		return (ZIA_ERROR);
	}

	if (!dpusm || !VDEV_HANDLE(vdev)) {
		return (ZIA_FALLBACK);
	}

	return (dpusm_to_ret(dpusm->disk.invalidate(VDEV_HANDLE(vdev))));
#else
	(void) vdev;
	return (ZIA_FALLBACK);
#endif
}

int
zia_disk_write(vdev_t *vdev, zio_t *zio, size_t io_size,
    uint64_t io_offset, int flags)
{
#ifdef ZIA
	if (!vdev || !zio || !zio->io_abd) {
		return (EIO);
	}

	if (!dpusm || !VDEV_HANDLE(vdev) || !ABD_HANDLE(zio->io_abd)) {
		return (EIO);
	}

	if (!abd_is_linear(zio->io_abd)) {
		return (EIO);
	}

	/*
	 * this was intended to handle gang abds, but breaking
	 * at first zero child abd was not correct
	 */
	size_t data_size = io_size;
	size_t trailing_zeros = 0;

	/* returns E errors */
	return (dpusm->disk.write(VDEV_HANDLE(vdev), ABD_HANDLE(zio->io_abd),
	    data_size, trailing_zeros, io_offset, flags,
	    zia_disk_write_completion, zio));
#else
	(void) vdev; (void) zio; (void) io_size;
	(void) io_offset; (void) flags;
	return (ZIA_FALLBACK);
#endif
}

int
zia_disk_flush(vdev_t *vdev, zio_t *zio)
{
#ifdef ZIA
	if (!vdev || !zio) {
		return (EIO);
	}

	if (!dpusm || !VDEV_HANDLE(vdev)) {
		return (EIO);
	}

	return (dpusm->disk.flush(VDEV_HANDLE(vdev),
	    zia_disk_flush_completion, zio));
#else
	(void) vdev; (void) zio;
	return (EIO);
#endif
}

int
zia_disk_close(vdev_t *vdev)
{
#ifdef ZIA
	if (!vdev) {
		return (ZIA_ERROR);
	}

	void *handle = VDEV_HANDLE(vdev);
	VDEV_HANDLE(vdev) = NULL;

	zia_get_props(vdev->vdev_spa)->min_offload_size = 0;

	if (!dpusm || !handle) {
		return (ZIA_FALLBACK);
	}

	/* trust that ZFS handles closing disks once */
	dpusm->disk.close(handle);

	return (ZIA_OK);
#else
	(void) vdev;
	return (ZIA_FALLBACK);
#endif
}
#endif
