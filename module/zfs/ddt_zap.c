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

/*
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2018 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/zio.h>
#include <sys/ddt.h>
#include <sys/ddt_impl.h>
#include <sys/zap.h>
#include <sys/dmu_tx.h>
#include <sys/zio_compress.h>

static unsigned int ddt_zap_default_bs = 15;
static unsigned int ddt_zap_default_ibs = 15;

#define	DDT_ZAP_COMPRESS_BYTEORDER_MASK	0x80
#define	DDT_ZAP_COMPRESS_FUNCTION_MASK	0x7f

#define	DDT_KEY_WORDS	(sizeof (ddt_key_t) / sizeof (uint64_t))

static size_t
ddt_zap_compress(const void *src, uchar_t *dst, size_t s_len, size_t d_len)
{
	uchar_t *version = dst++;
	int cpfunc = ZIO_COMPRESS_ZLE;
	zio_compress_info_t *ci = &zio_compress_table[cpfunc];
	size_t c_len;

	ASSERT3U(d_len, >=, s_len + 1);	/* no compression plus version byte */

	c_len = ci->ci_compress((void *)src, dst, s_len, d_len - 1,
	    ci->ci_level);

	if (c_len == s_len) {
		cpfunc = ZIO_COMPRESS_OFF;
		memcpy(dst, src, s_len);
	}

	*version = cpfunc;
	if (ZFS_HOST_BYTEORDER)
		*version |= DDT_ZAP_COMPRESS_BYTEORDER_MASK;

	return (c_len + 1);
}

static void
ddt_zap_decompress(uchar_t *src, void *dst, size_t s_len, size_t d_len)
{
	uchar_t version = *src++;
	int cpfunc = version & DDT_ZAP_COMPRESS_FUNCTION_MASK;
	zio_compress_info_t *ci = &zio_compress_table[cpfunc];

	if (ci->ci_decompress != NULL)
		(void) ci->ci_decompress(src, dst, s_len, d_len, ci->ci_level);
	else
		memcpy(dst, src, d_len);

	if (((version & DDT_ZAP_COMPRESS_BYTEORDER_MASK) != 0) !=
	    (ZFS_HOST_BYTEORDER != 0))
		byteswap_uint64_array(dst, d_len);
}

static int
ddt_zap_create(objset_t *os, uint64_t *objectp, dmu_tx_t *tx, boolean_t prehash)
{
	zap_flags_t flags = ZAP_FLAG_HASH64 | ZAP_FLAG_UINT64_KEY;

	if (prehash)
		flags |= ZAP_FLAG_PRE_HASHED_KEY;

	*objectp = zap_create_flags(os, 0, flags, DMU_OT_DDT_ZAP,
	    ddt_zap_default_bs, ddt_zap_default_ibs,
	    DMU_OT_NONE, 0, tx);
	if (*objectp == 0)
		return (SET_ERROR(ENOTSUP));

	return (0);
}

static int
ddt_zap_destroy(objset_t *os, uint64_t object, dmu_tx_t *tx)
{
	return (zap_destroy(os, object, tx));
}

static int
ddt_zap_lookup(objset_t *os, uint64_t object,
    const ddt_key_t *ddk, ddt_phys_t *phys, size_t psize)
{
	uchar_t *cbuf;
	uint64_t one, csize;
	int error;

	error = zap_length_uint64(os, object, (uint64_t *)ddk,
	    DDT_KEY_WORDS, &one, &csize);
	if (error)
		return (error);

	ASSERT3U(one, ==, 1);
	ASSERT3U(csize, <=, psize + 1);

	cbuf = kmem_alloc(csize, KM_SLEEP);

	error = zap_lookup_uint64(os, object, (uint64_t *)ddk,
	    DDT_KEY_WORDS, 1, csize, cbuf);
	if (error == 0)
		ddt_zap_decompress(cbuf, phys, csize, psize);

	kmem_free(cbuf, csize);

	return (error);
}

static int
ddt_zap_contains(objset_t *os, uint64_t object, const ddt_key_t *ddk)
{
	return (zap_length_uint64(os, object, (uint64_t *)ddk, DDT_KEY_WORDS,
	    NULL, NULL));
}

static void
ddt_zap_prefetch(objset_t *os, uint64_t object, const ddt_key_t *ddk)
{
	(void) zap_prefetch_uint64(os, object, (uint64_t *)ddk, DDT_KEY_WORDS);
}

static int
ddt_zap_update(objset_t *os, uint64_t object, const ddt_key_t *ddk,
    const ddt_phys_t *phys, size_t psize, dmu_tx_t *tx)
{
	const size_t cbuf_size = psize + 1;

	uchar_t *cbuf = kmem_alloc(cbuf_size, KM_SLEEP);

	uint64_t csize = ddt_zap_compress(phys, cbuf, psize, cbuf_size);

	int error = zap_update_uint64(os, object, (uint64_t *)ddk,
	    DDT_KEY_WORDS, 1, csize, cbuf, tx);

	kmem_free(cbuf, cbuf_size);

	return (error);
}

static int
ddt_zap_remove(objset_t *os, uint64_t object, const ddt_key_t *ddk,
    dmu_tx_t *tx)
{
	return (zap_remove_uint64(os, object, (uint64_t *)ddk,
	    DDT_KEY_WORDS, tx));
}

static int
ddt_zap_walk(objset_t *os, uint64_t object, uint64_t *walk, ddt_key_t *ddk,
    ddt_phys_t *phys, size_t psize)
{
	zap_cursor_t zc;
	zap_attribute_t za;
	int error;

	if (*walk == 0) {
		/*
		 * We don't want to prefetch the entire ZAP object, because
		 * it can be enormous.  Also the primary use of DDT iteration
		 * is for scrubbing, in which case we will be issuing many
		 * scrub I/Os for each ZAP block that we read in, so
		 * reading the ZAP is unlikely to be the bottleneck.
		 */
		zap_cursor_init_noprefetch(&zc, os, object);
	} else {
		zap_cursor_init_serialized(&zc, os, object, *walk);
	}
	if ((error = zap_cursor_retrieve(&zc, &za)) == 0) {
		uint64_t csize = za.za_num_integers;

		ASSERT3U(za.za_integer_length, ==, 1);
		ASSERT3U(csize, <=, psize + 1);

		uchar_t *cbuf = kmem_alloc(csize, KM_SLEEP);

		error = zap_lookup_uint64(os, object, (uint64_t *)za.za_name,
		    DDT_KEY_WORDS, 1, csize, cbuf);
		ASSERT0(error);
		if (error == 0) {
			ddt_zap_decompress(cbuf, phys, csize, psize);
			*ddk = *(ddt_key_t *)za.za_name;
		}

		kmem_free(cbuf, csize);

		zap_cursor_advance(&zc);
		*walk = zap_cursor_serialize(&zc);
	}
	zap_cursor_fini(&zc);
	return (error);
}

static int
ddt_zap_count(objset_t *os, uint64_t object, uint64_t *count)
{
	return (zap_count(os, object, count));
}

const ddt_ops_t ddt_zap_ops = {
	"zap",
	ddt_zap_create,
	ddt_zap_destroy,
	ddt_zap_lookup,
	ddt_zap_contains,
	ddt_zap_prefetch,
	ddt_zap_update,
	ddt_zap_remove,
	ddt_zap_walk,
	ddt_zap_count,
};

/* BEGIN CSTYLED */
ZFS_MODULE_PARAM(zfs_dedup, , ddt_zap_default_bs, UINT, ZMOD_RW,
	"DDT ZAP leaf blockshift");
ZFS_MODULE_PARAM(zfs_dedup, , ddt_zap_default_ibs, UINT, ZMOD_RW,
	"DDT ZAP indirect blockshift");
/* END CSTYLED */
