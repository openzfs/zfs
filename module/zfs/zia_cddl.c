/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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

#ifdef ZIA

#include <sys/vdev.h>
#include <sys/vdev_disk.h>
#include <sys/vdev_raidz_impl.h>
#include <sys/zia.h>
#include <sys/zia_cddl.h>
#include <sys/zia_private.h>
#include <sys/zio_compress.h>

/* basically a duplicate of zio_compress_data */
int
zia_compress_impl(const dpusm_uf_t *dpusm, zia_props_t *props,
    enum zio_compress c, abd_t *src, size_t s_len,
    void **cbuf_handle, uint64_t *c_len,
    uint8_t level, boolean_t *local_offload)
{
	size_t d_len;
	uint8_t complevel;
	zio_compress_info_t *ci = &zio_compress_table[c];
	int ret = ZIA_OK;

	ASSERT((uint_t)c < ZIO_COMPRESS_FUNCTIONS);
	ASSERT((uint_t)c == ZIO_COMPRESS_EMPTY || ci->ci_compress != NULL);

	/*
	 * If the data is all zeros, we don't even need to allocate
	 * a block for it.  We indicate this by returning zero size.
	 */
	if (!ABD_HANDLE(src)) {
		/* check in-memory buffer for zeros */
		if (abd_iterate_func(src, 0, s_len,
		    zio_compress_zeroed_cb, NULL) == 0) {
			*c_len = 0;
			return (ZIA_OK);
		}

		if (c == ZIO_COMPRESS_EMPTY) {
			*c_len = s_len;
			return (ZIA_OK);
		}

		/* check that compression can be done before offloading */
		dpusm_pc_t *caps = NULL;
		if ((zia_get_capabilities(props->provider, &caps) != ZIA_OK) ||
		    !(caps->compress & compress_to_dpusm(c))) {
			return (ZIA_FALLBACK);
		}

		ret = zia_offload_abd(props->provider, src, s_len,
		    props->min_offload_size, local_offload);
		if (ret != ZIA_OK) {
			return (ret);
		}
	} else {
		/* came in offloaded */
		void *old_provider = dpusm->extract(ABD_HANDLE(src));
		if (old_provider != props->provider) {
			return (ZIA_PROVIDER_MISMATCH);
		}

		/* use provider to check for zero buffer */
		ret = dpusm->all_zeros(ABD_HANDLE(src), 0, s_len);
		if (ret == DPUSM_OK) {
			*c_len = 0;
			return (ZIA_OK);
		} else if (ret != DPUSM_BAD_RESULT) {
			return (dpusm_to_ret(ret));
		}

		if (c == ZIO_COMPRESS_EMPTY) {
			*c_len = s_len;
			return (ZIA_OK);
		}

		dpusm_pc_t *caps = NULL;
		ret = zia_get_capabilities(props->provider, &caps);
		if (ret != ZIA_OK) {
			return (ret);
		}

		if (!(caps->compress & compress_to_dpusm(c))) {
			return (ZIA_FALLBACK);
		}
	}

	/* Compress at least 12.5% */
	d_len = s_len - (s_len >> 3);

	complevel = ci->ci_level;

	if (c == ZIO_COMPRESS_ZSTD) {
		/* If we don't know the level, we can't compress it */
		if (level == ZIO_COMPLEVEL_INHERIT) {
			*c_len = s_len;
			return (ZIA_OK);
		}

		if (level == ZIO_COMPLEVEL_DEFAULT)
			complevel = ZIO_ZSTD_LEVEL_DEFAULT;
		else
			complevel = level;

		ASSERT3U(complevel, !=, ZIO_COMPLEVEL_INHERIT);
	}

	/* nothing to offload, so just allocate space */
	*cbuf_handle = zia_alloc(props->provider,
	    s_len, props->min_offload_size);
	if (!*cbuf_handle) {
		return (ZIA_ERROR);
	}

	/* DPUSM interface takes in a size_t, not a uint64_t */
	size_t zia_c_len = (size_t)s_len;
	ret = dpusm->compress(compress_to_dpusm(c), (int8_t)level,
	    ABD_HANDLE(src), s_len, *cbuf_handle, &zia_c_len);
	if (ret != DPUSM_OK) {
		zia_free(cbuf_handle);
		return (dpusm_to_ret(ret));
	}

	*c_len = zia_c_len;

	/*
	 * Return ZIA_OK because this is not an error - it just didn't
	 * compress well. The data will be dropped later on (instead of
	 * onloaded) because c_len is too big.
	 */
	if (*c_len > d_len) {
		*c_len = s_len;
	}

	return (ZIA_OK);
}

int
zia_raidz_rec_impl(const dpusm_uf_t *dpusm,
    raidz_row_t *rr, int *t, int nt)
{
	int tgts[VDEV_RAIDZ_MAXPARITY];
	int ntgts = 0;
	for (int i = 0, c = 0; c < rr->rr_cols; c++) {
		if (i < nt && c == t[i]) {
			tgts[ntgts++] = c;
			i++;
		} else if (rr->rr_col[c].rc_error != 0) {
			tgts[ntgts++] = c;
		}
	}

	ASSERT(ntgts >= nt);

	return (dpusm->raid.rec(rr->rr_zia_handle,
	    tgts, ntgts));
}

#ifdef _KERNEL
/* called by provider */
void
zia_disk_write_completion(void *zio_ptr, int error)
{
	zio_t *zio = (zio_t *)zio_ptr;
	zio->io_error = error;
	ASSERT3S(zio->io_error, >=, 0);
	if (zio->io_error)
		vdev_disk_error(zio);

	zio_delay_interrupt(zio);
}

/* called by provider */
void
zia_disk_flush_completion(void *zio_ptr, int error)
{
	zio_t *zio = (zio_t *)zio_ptr;

	if (zio->io_error && (zio->io_error == EOPNOTSUPP))
		zio->io_vd->vdev_nowritecache = B_TRUE;

	ASSERT3S(zio->io_error, >=, 0);
	if (zio->io_error)
		vdev_disk_error(zio);
	zio_interrupt(zio);
}
#endif /* _KERNEL */

#endif /* ZIA */
