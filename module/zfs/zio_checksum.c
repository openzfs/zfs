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
/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2013, 2016 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/zio.h>
#include <sys/zio_checksum.h>
#include <sys/zil.h>
#include <sys/abd.h>
#include <zfs_fletcher.h>

/*
 * Checksum vectors.
 *
 * In the SPA, everything is checksummed.  We support checksum vectors
 * for three distinct reasons:
 *
 *   1. Different kinds of data need different levels of protection.
 *	For SPA metadata, we always want a very strong checksum.
 *	For user data, we let users make the trade-off between speed
 *	and checksum strength.
 *
 *   2. Cryptographic hash and MAC algorithms are an area of active research.
 *	It is likely that in future hash functions will be at least as strong
 *	as current best-of-breed, and may be substantially faster as well.
 *	We want the ability to take advantage of these new hashes as soon as
 *	they become available.
 *
 *   3. If someone develops hardware that can compute a strong hash quickly,
 *	we want the ability to take advantage of that hardware.
 *
 * Of course, we don't want a checksum upgrade to invalidate existing
 * data, so we store the checksum *function* in eight bits of the bp.
 * This gives us room for up to 256 different checksum functions.
 *
 * When writing a block, we always checksum it with the latest-and-greatest
 * checksum function of the appropriate strength.  When reading a block,
 * we compare the expected checksum against the actual checksum, which we
 * compute via the checksum function specified by BP_GET_CHECKSUM(bp).
 */

/*ARGSUSED*/
static void
abd_checksum_off(abd_t *abd, uint64_t size, zio_cksum_t *zcp)
{
	ZIO_SET_CHECKSUM(zcp, 0, 0, 0, 0);
}

/*ARGSUSED*/
static int
fletcher_2_incremental_native_compat(void *buf, size_t size, void *zcp)
{
	fletcher_2_incremental_native(buf, size, (zio_cksum_t *)zcp);
	return (0);
}

/*ARGSUSED*/
void
abd_fletcher_2_native(abd_t *abd, uint64_t size, zio_cksum_t *zcp)
{
	fletcher_init(zcp);
	(void) abd_iterate_func(abd, 0, size,
	    fletcher_2_incremental_native_compat, zcp);
}

/*ARGSUSED*/
static int
fletcher_2_incremental_byteswap_compat(void *buf, size_t size, void *zcp)
{
	fletcher_2_incremental_byteswap(buf, size, (zio_cksum_t *) zcp);
	return (0);
}

/*ARGSUSED*/
void
abd_fletcher_2_byteswap(abd_t *abd, uint64_t size, zio_cksum_t *zcp)
{
	fletcher_init(zcp);
	(void) abd_iterate_func(abd, 0, size,
	    fletcher_2_incremental_byteswap_compat, zcp);
}

/*ARGSUSED*/
static int
fletcher_4_incremental_native_compat(void *buf, size_t size, void *zcp)
{
	fletcher_4_incremental_native(buf, size, (zio_cksum_t *)zcp);
	return (0);
}

/*ARGSUSED*/
void
abd_fletcher_4_native(abd_t *abd, uint64_t size, zio_cksum_t *zcp)
{
	fletcher_init(zcp);
	(void) abd_iterate_func(abd, 0, size,
	    fletcher_4_incremental_native_compat, zcp);
}

/*ARGSUSED*/
static int
fletcher_4_incremental_byteswap_compat(void *buf, size_t size, void *zcp)
{
	fletcher_4_incremental_byteswap(buf, size, (zio_cksum_t *)zcp);
	return (0);
}

/*ARGSUSED*/
void
abd_fletcher_4_byteswap(abd_t *abd, uint64_t size, zio_cksum_t *zcp)
{
	fletcher_init(zcp);
	(void) abd_iterate_func(abd, 0, size,
	    fletcher_4_incremental_byteswap_compat, zcp);
}

zio_checksum_info_t zio_checksum_table[ZIO_CHECKSUM_FUNCTIONS] = {
	{{NULL,			NULL},			0, 0, 0, "inherit"},
	{{NULL,			NULL},			0, 0, 0, "on"},
	{{abd_checksum_off,	abd_checksum_off},	0, 0, 0, "off"},
	{{abd_checksum_SHA256,	abd_checksum_SHA256},	1, 1, 0, "label"},
	{{abd_checksum_SHA256,	abd_checksum_SHA256},	1, 1, 0, "gang_header"},
	{{abd_fletcher_2_native, abd_fletcher_2_byteswap}, 0, 1, 0, "zilog"},
	{{abd_fletcher_2_native, abd_fletcher_2_byteswap}, 0, 0, 0,
	"abd_fletcher2"},
	{{abd_fletcher_4_native, abd_fletcher_4_byteswap}, 1, 0, 0,
	"abd_fletcher4"},
	{{abd_checksum_SHA256,	abd_checksum_SHA256},	1, 0, 1, "sha256"},
	{{abd_fletcher_4_native, abd_fletcher_4_byteswap}, 0, 1, 0, "zilog2"},
	{{abd_checksum_off,	abd_checksum_off},	0, 0, 0, "noparity"},

#ifdef _HAS_ZOL_PR_4760_
	{{abd_checksum_SHA512_native,	abd_checksum_SHA512_byteswap},
	    NULL, NULL, ZCHECKSUM_FLAG_METADATA | ZCHECKSUM_FLAG_DEDUP |
	    ZCHECKSUM_FLAG_NOPWRITE, "sha512"},
	{{abd_checksum_skein_native,	abd_checksum_skein_byteswap},
	    abd_checksum_skein_tmpl_init, abd_checksum_skein_tmpl_free,
	    ZCHECKSUM_FLAG_METADATA | ZCHECKSUM_FLAG_DEDUP |
	    ZCHECKSUM_FLAG_SALTED | ZCHECKSUM_FLAG_NOPWRITE, "skein"},
	{{abd_checksum_edonr_native,	abd_checksum_edonr_byteswap},
	    abd_checksum_edonr_tmpl_init, abd_checksum_edonr_tmpl_free,
	    ZCHECKSUM_FLAG_METADATA | ZCHECKSUM_FLAG_SALTED |
	    ZCHECKSUM_FLAG_NOPWRITE, "edonr"},
#endif /* _HAS_ZOL_PR_4760_ */
};

enum zio_checksum
zio_checksum_select(enum zio_checksum child, enum zio_checksum parent)
{
	ASSERT(child < ZIO_CHECKSUM_FUNCTIONS);
	ASSERT(parent < ZIO_CHECKSUM_FUNCTIONS);
	ASSERT(parent != ZIO_CHECKSUM_INHERIT && parent != ZIO_CHECKSUM_ON);

	if (child == ZIO_CHECKSUM_INHERIT)
		return (parent);

	if (child == ZIO_CHECKSUM_ON)
		return (ZIO_CHECKSUM_ON_VALUE);

	return (child);
}

enum zio_checksum
zio_checksum_dedup_select(spa_t *spa, enum zio_checksum child,
    enum zio_checksum parent)
{
	ASSERT((child & ZIO_CHECKSUM_MASK) < ZIO_CHECKSUM_FUNCTIONS);
	ASSERT((parent & ZIO_CHECKSUM_MASK) < ZIO_CHECKSUM_FUNCTIONS);
	ASSERT(parent != ZIO_CHECKSUM_INHERIT && parent != ZIO_CHECKSUM_ON);

	if (child == ZIO_CHECKSUM_INHERIT)
		return (parent);

	if (child == ZIO_CHECKSUM_ON)
		return (spa_dedup_checksum(spa));

	if (child == (ZIO_CHECKSUM_ON | ZIO_CHECKSUM_VERIFY))
		return (spa_dedup_checksum(spa) | ZIO_CHECKSUM_VERIFY);

	ASSERT(zio_checksum_table[child & ZIO_CHECKSUM_MASK].ci_dedup ||
	    (child & ZIO_CHECKSUM_VERIFY) || child == ZIO_CHECKSUM_OFF);

	return (child);
}

/*
 * Set the external verifier for a gang block based on <vdev, offset, txg>,
 * a tuple which is guaranteed to be unique for the life of the pool.
 */
static void
zio_checksum_gang_verifier(zio_cksum_t *zcp, blkptr_t *bp)
{
	const dva_t *dva = BP_IDENTITY(bp);
	uint64_t txg = BP_PHYSICAL_BIRTH(bp);

	ASSERT(BP_IS_GANG(bp));

	ZIO_SET_CHECKSUM(zcp, DVA_GET_VDEV(dva), DVA_GET_OFFSET(dva), txg, 0);
}

/*
 * Set the external verifier for a label block based on its offset.
 * The vdev is implicit, and the txg is unknowable at pool open time --
 * hence the logic in vdev_uberblock_load() to find the most recent copy.
 */
static void
zio_checksum_label_verifier(zio_cksum_t *zcp, uint64_t offset)
{
	ZIO_SET_CHECKSUM(zcp, offset, 0, 0, 0);
}

/*
 * Generate the checksum.
 */
void
zio_checksum_compute(zio_t *zio, enum zio_checksum checksum,
    abd_t *abd, uint64_t size)
{
	blkptr_t *bp = zio->io_bp;
	uint64_t offset = zio->io_offset;
	zio_checksum_info_t *ci = &zio_checksum_table[checksum];
	zio_cksum_t cksum;

	ASSERT((uint_t)checksum < ZIO_CHECKSUM_FUNCTIONS);
	ASSERT(ci->ci_func[0] != NULL);

	if (ci->ci_eck) {
		zio_eck_t *eck;
		void *data = abd_to_buf(abd);

		if (checksum == ZIO_CHECKSUM_ZILOG2) {
			zil_chain_t *zilc = data;

			size = P2ROUNDUP_TYPED(zilc->zc_nused, ZIL_MIN_BLKSZ,
			    uint64_t);
			eck = &zilc->zc_eck;
		} else {
			eck = (zio_eck_t *)((char *)data + size) - 1;
		}
		if (checksum == ZIO_CHECKSUM_GANG_HEADER)
			zio_checksum_gang_verifier(&eck->zec_cksum, bp);
		else if (checksum == ZIO_CHECKSUM_LABEL)
			zio_checksum_label_verifier(&eck->zec_cksum, offset);
		else
			bp->blk_cksum = eck->zec_cksum;
		eck->zec_magic = ZEC_MAGIC;
		ci->ci_func[0](abd, size, &cksum);
		eck->zec_cksum = cksum;
	} else {
		ci->ci_func[0](abd, size, &bp->blk_cksum);
	}
}

int
zio_checksum_error_impl(spa_t *spa, blkptr_t *bp, enum zio_checksum checksum,
    abd_t *abd, uint64_t size, uint64_t offset, zio_bad_cksum_t *info)
{
	zio_checksum_info_t *ci = &zio_checksum_table[checksum];
	zio_cksum_t actual_cksum, expected_cksum;
	int byteswap;

	if (checksum >= ZIO_CHECKSUM_FUNCTIONS || ci->ci_func[0] == NULL)
		return (SET_ERROR(EINVAL));

	if (ci->ci_eck) {
		zio_eck_t *eck;
		zio_cksum_t verifier;
		size_t eck_offset;
		uint64_t data_size = size;
		void *data = abd_borrow_buf_copy(abd, data_size);

		if (checksum == ZIO_CHECKSUM_ZILOG2) {
			zil_chain_t *zilc = data;
			uint64_t nused;

			eck = &zilc->zc_eck;
			if (eck->zec_magic == ZEC_MAGIC) {
				nused = zilc->zc_nused;
			} else if (eck->zec_magic == BSWAP_64(ZEC_MAGIC)) {
				nused = BSWAP_64(zilc->zc_nused);
			} else {
				abd_return_buf(abd, data, data_size);
				return (SET_ERROR(ECKSUM));
			}

			if (nused > data_size) {
				abd_return_buf(abd, data, data_size);
				return (SET_ERROR(ECKSUM));
			}

			size = P2ROUNDUP_TYPED(nused, ZIL_MIN_BLKSZ, uint64_t);
		} else {
			eck = (zio_eck_t *)((char *)data + data_size) - 1;
		}

		if (checksum == ZIO_CHECKSUM_GANG_HEADER)
			zio_checksum_gang_verifier(&verifier, bp);
		else if (checksum == ZIO_CHECKSUM_LABEL)
			zio_checksum_label_verifier(&verifier, offset);
		else
			verifier = bp->blk_cksum;

		byteswap = (eck->zec_magic == BSWAP_64(ZEC_MAGIC));

		if (byteswap)
			byteswap_uint64_array(&verifier, sizeof (zio_cksum_t));

		eck_offset = (size_t)(&eck->zec_cksum) - (size_t)data;
		expected_cksum = eck->zec_cksum;
		eck->zec_cksum = verifier;
		abd_return_buf_copy(abd, data, data_size);

		ci->ci_func[byteswap](abd, size, &actual_cksum);
		abd_copy_from_buf_off(abd, &expected_cksum,
		    eck_offset, sizeof (zio_cksum_t));

		if (byteswap) {
			byteswap_uint64_array(&expected_cksum,
			    sizeof (zio_cksum_t));
		}
	} else {
		byteswap = BP_SHOULD_BYTESWAP(bp);
		expected_cksum = bp->blk_cksum;
		ci->ci_func[byteswap](abd, size, &actual_cksum);
	}

	if (info != NULL) {
		info->zbc_expected = expected_cksum;
		info->zbc_actual = actual_cksum;
		info->zbc_checksum_name = ci->ci_name;
		info->zbc_byteswapped = byteswap;
		info->zbc_injected = 0;
		info->zbc_has_cksum = 1;
	}

	if (!ZIO_CHECKSUM_EQUAL(actual_cksum, expected_cksum))
		return (SET_ERROR(ECKSUM));

	return (0);
}

int
zio_checksum_error(zio_t *zio, zio_bad_cksum_t *info)
{
	blkptr_t *bp = zio->io_bp;
	uint_t checksum = (bp == NULL ? zio->io_prop.zp_checksum :
	    (BP_IS_GANG(bp) ? ZIO_CHECKSUM_GANG_HEADER : BP_GET_CHECKSUM(bp)));
	int error;
	uint64_t size = (bp == NULL ? zio->io_size :
	    (BP_IS_GANG(bp) ? SPA_GANGBLOCKSIZE : BP_GET_PSIZE(bp)));
	uint64_t offset = zio->io_offset;
	abd_t *data = zio->io_abd;
	spa_t *spa = zio->io_spa;

	error = zio_checksum_error_impl(spa, bp, checksum, data, size,
	    offset, info);
	if (error != 0 && zio_injection_enabled && !zio->io_error &&
	    (error = zio_handle_fault_injection(zio, ECKSUM)) != 0) {

		info->zbc_injected = 1;
		return (error);
	}
	return (error);
}
