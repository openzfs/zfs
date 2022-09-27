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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2013, 2016 by Delphix. All rights reserved.
 * Copyright 2013 Saso Kiselkov. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
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
 *
 * SALTED CHECKSUMS
 *
 * To enable the use of less secure hash algorithms with dedup, we
 * introduce the notion of salted checksums (MACs, really).  A salted
 * checksum is fed both a random 256-bit value (the salt) and the data
 * to be checksummed.  This salt is kept secret (stored on the pool, but
 * never shown to the user).  Thus even if an attacker knew of collision
 * weaknesses in the hash algorithm, they won't be able to mount a known
 * plaintext attack on the DDT, since the actual hash value cannot be
 * known ahead of time.  How the salt is used is algorithm-specific
 * (some might simply prefix it to the data block, others might need to
 * utilize a full-blown HMAC).  On disk the salt is stored in a ZAP
 * object in the MOS (DMU_POOL_CHECKSUM_SALT).
 *
 * CONTEXT TEMPLATES
 *
 * Some hashing algorithms need to perform a substantial amount of
 * initialization work (e.g. salted checksums above may need to pre-hash
 * the salt) before being able to process data.  Performing this
 * redundant work for each block would be wasteful, so we instead allow
 * a checksum algorithm to do the work once (the first time it's used)
 * and then keep this pre-initialized context as a template inside the
 * spa_t (spa_cksum_tmpls).  If the zio_checksum_info_t contains
 * non-NULL ci_tmpl_init and ci_tmpl_free callbacks, they are used to
 * construct and destruct the pre-initialized checksum context.  The
 * pre-initialized context is then reused during each checksum
 * invocation and passed to the checksum function.
 */

static void
abd_checksum_off(abd_t *abd, uint64_t size,
    const void *ctx_template, zio_cksum_t *zcp)
{
	(void) abd, (void) size, (void) ctx_template;
	ZIO_SET_CHECKSUM(zcp, 0, 0, 0, 0);
}

static void
abd_fletcher_2_native(abd_t *abd, uint64_t size,
    const void *ctx_template, zio_cksum_t *zcp)
{
	(void) ctx_template;
	fletcher_init(zcp);
	(void) abd_iterate_func(abd, 0, size,
	    fletcher_2_incremental_native, zcp);
}

static void
abd_fletcher_2_byteswap(abd_t *abd, uint64_t size,
    const void *ctx_template, zio_cksum_t *zcp)
{
	(void) ctx_template;
	fletcher_init(zcp);
	(void) abd_iterate_func(abd, 0, size,
	    fletcher_2_incremental_byteswap, zcp);
}

static inline void
abd_fletcher_4_impl(abd_t *abd, uint64_t size, zio_abd_checksum_data_t *acdp)
{
	fletcher_4_abd_ops.acf_init(acdp);
	abd_iterate_func(abd, 0, size, fletcher_4_abd_ops.acf_iter, acdp);
	fletcher_4_abd_ops.acf_fini(acdp);
}

void
abd_fletcher_4_native(abd_t *abd, uint64_t size,
    const void *ctx_template, zio_cksum_t *zcp)
{
	(void) ctx_template;
	fletcher_4_ctx_t ctx;

	zio_abd_checksum_data_t acd = {
		.acd_byteorder	= ZIO_CHECKSUM_NATIVE,
		.acd_zcp 	= zcp,
		.acd_ctx	= &ctx
	};

	abd_fletcher_4_impl(abd, size, &acd);

}

void
abd_fletcher_4_byteswap(abd_t *abd, uint64_t size,
    const void *ctx_template, zio_cksum_t *zcp)
{
	(void) ctx_template;
	fletcher_4_ctx_t ctx;

	zio_abd_checksum_data_t acd = {
		.acd_byteorder	= ZIO_CHECKSUM_BYTESWAP,
		.acd_zcp 	= zcp,
		.acd_ctx	= &ctx
	};

	abd_fletcher_4_impl(abd, size, &acd);
}

zio_checksum_info_t zio_checksum_table[ZIO_CHECKSUM_FUNCTIONS] = {
	{{NULL, NULL}, NULL, NULL, 0, "inherit"},
	{{NULL, NULL}, NULL, NULL, 0, "on"},
	{{abd_checksum_off,		abd_checksum_off},
	    NULL, NULL, 0, "off"},
	{{abd_checksum_SHA256,		abd_checksum_SHA256},
	    NULL, NULL, ZCHECKSUM_FLAG_METADATA | ZCHECKSUM_FLAG_EMBEDDED,
	    "label"},
	{{abd_checksum_SHA256,		abd_checksum_SHA256},
	    NULL, NULL, ZCHECKSUM_FLAG_METADATA | ZCHECKSUM_FLAG_EMBEDDED,
	    "gang_header"},
	{{abd_fletcher_2_native,	abd_fletcher_2_byteswap},
	    NULL, NULL, ZCHECKSUM_FLAG_EMBEDDED, "zilog"},
	{{abd_fletcher_2_native,	abd_fletcher_2_byteswap},
	    NULL, NULL, 0, "fletcher2"},
	{{abd_fletcher_4_native,	abd_fletcher_4_byteswap},
	    NULL, NULL, ZCHECKSUM_FLAG_METADATA, "fletcher4"},
	{{abd_checksum_SHA256,		abd_checksum_SHA256},
	    NULL, NULL, ZCHECKSUM_FLAG_METADATA | ZCHECKSUM_FLAG_DEDUP |
	    ZCHECKSUM_FLAG_NOPWRITE, "sha256"},
	{{abd_fletcher_4_native,	abd_fletcher_4_byteswap},
	    NULL, NULL, ZCHECKSUM_FLAG_EMBEDDED, "zilog2"},
	{{abd_checksum_off,		abd_checksum_off},
	    NULL, NULL, 0, "noparity"},
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
	{{abd_checksum_blake3_native,	abd_checksum_blake3_byteswap},
	    abd_checksum_blake3_tmpl_init, abd_checksum_blake3_tmpl_free,
	    ZCHECKSUM_FLAG_METADATA | ZCHECKSUM_FLAG_DEDUP |
	    ZCHECKSUM_FLAG_SALTED | ZCHECKSUM_FLAG_NOPWRITE, "blake3"},
};

/*
 * The flag corresponding to the "verify" in dedup=[checksum,]verify
 * must be cleared first, so callers should use ZIO_CHECKSUM_MASK.
 */
spa_feature_t
zio_checksum_to_feature(enum zio_checksum cksum)
{
	VERIFY((cksum & ~ZIO_CHECKSUM_MASK) == 0);

	switch (cksum) {
	case ZIO_CHECKSUM_BLAKE3:
		return (SPA_FEATURE_BLAKE3);
	case ZIO_CHECKSUM_SHA512:
		return (SPA_FEATURE_SHA512);
	case ZIO_CHECKSUM_SKEIN:
		return (SPA_FEATURE_SKEIN);
	case ZIO_CHECKSUM_EDONR:
		return (SPA_FEATURE_EDONR);
	default:
		return (SPA_FEATURE_NONE);
	}
}

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

	ASSERT((zio_checksum_table[child & ZIO_CHECKSUM_MASK].ci_flags &
	    ZCHECKSUM_FLAG_DEDUP) ||
	    (child & ZIO_CHECKSUM_VERIFY) || child == ZIO_CHECKSUM_OFF);

	return (child);
}

/*
 * Set the external verifier for a gang block based on <vdev, offset, txg>,
 * a tuple which is guaranteed to be unique for the life of the pool.
 */
static void
zio_checksum_gang_verifier(zio_cksum_t *zcp, const blkptr_t *bp)
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
 * Calls the template init function of a checksum which supports context
 * templates and installs the template into the spa_t.
 */
static void
zio_checksum_template_init(enum zio_checksum checksum, spa_t *spa)
{
	zio_checksum_info_t *ci = &zio_checksum_table[checksum];

	if (ci->ci_tmpl_init == NULL)
		return;
	if (spa->spa_cksum_tmpls[checksum] != NULL)
		return;

	VERIFY(ci->ci_tmpl_free != NULL);
	mutex_enter(&spa->spa_cksum_tmpls_lock);
	if (spa->spa_cksum_tmpls[checksum] == NULL) {
		spa->spa_cksum_tmpls[checksum] =
		    ci->ci_tmpl_init(&spa->spa_cksum_salt);
		VERIFY(spa->spa_cksum_tmpls[checksum] != NULL);
	}
	mutex_exit(&spa->spa_cksum_tmpls_lock);
}

/* convenience function to update a checksum to accommodate an encryption MAC */
static void
zio_checksum_handle_crypt(zio_cksum_t *cksum, zio_cksum_t *saved, boolean_t xor)
{
	/*
	 * Weak checksums do not have their entropy spread evenly
	 * across the bits of the checksum. Therefore, when truncating
	 * a weak checksum we XOR the first 2 words with the last 2 so
	 * that we don't "lose" any entropy unnecessarily.
	 */
	if (xor) {
		cksum->zc_word[0] ^= cksum->zc_word[2];
		cksum->zc_word[1] ^= cksum->zc_word[3];
	}

	cksum->zc_word[2] = saved->zc_word[2];
	cksum->zc_word[3] = saved->zc_word[3];
}

/*
 * Generate the checksum.
 */
void
zio_checksum_compute(zio_t *zio, enum zio_checksum checksum,
    abd_t *abd, uint64_t size)
{
	static const uint64_t zec_magic = ZEC_MAGIC;
	blkptr_t *bp = zio->io_bp;
	uint64_t offset = zio->io_offset;
	zio_checksum_info_t *ci = &zio_checksum_table[checksum];
	zio_cksum_t cksum, saved;
	spa_t *spa = zio->io_spa;
	boolean_t insecure = (ci->ci_flags & ZCHECKSUM_FLAG_DEDUP) == 0;

	ASSERT((uint_t)checksum < ZIO_CHECKSUM_FUNCTIONS);
	ASSERT(ci->ci_func[0] != NULL);

	zio_checksum_template_init(checksum, spa);

	if (ci->ci_flags & ZCHECKSUM_FLAG_EMBEDDED) {
		zio_eck_t eck;
		size_t eck_offset;

		memset(&saved, 0, sizeof (zio_cksum_t));

		if (checksum == ZIO_CHECKSUM_ZILOG2) {
			zil_chain_t zilc;
			abd_copy_to_buf(&zilc, abd, sizeof (zil_chain_t));

			size = P2ROUNDUP_TYPED(zilc.zc_nused, ZIL_MIN_BLKSZ,
			    uint64_t);
			eck = zilc.zc_eck;
			eck_offset = offsetof(zil_chain_t, zc_eck);
		} else {
			eck_offset = size - sizeof (zio_eck_t);
			abd_copy_to_buf_off(&eck, abd, eck_offset,
			    sizeof (zio_eck_t));
		}

		if (checksum == ZIO_CHECKSUM_GANG_HEADER) {
			zio_checksum_gang_verifier(&eck.zec_cksum, bp);
		} else if (checksum == ZIO_CHECKSUM_LABEL) {
			zio_checksum_label_verifier(&eck.zec_cksum, offset);
		} else {
			saved = eck.zec_cksum;
			eck.zec_cksum = bp->blk_cksum;
		}

		abd_copy_from_buf_off(abd, &zec_magic,
		    eck_offset + offsetof(zio_eck_t, zec_magic),
		    sizeof (zec_magic));
		abd_copy_from_buf_off(abd, &eck.zec_cksum,
		    eck_offset + offsetof(zio_eck_t, zec_cksum),
		    sizeof (zio_cksum_t));

		ci->ci_func[0](abd, size, spa->spa_cksum_tmpls[checksum],
		    &cksum);
		if (bp != NULL && BP_USES_CRYPT(bp) &&
		    BP_GET_TYPE(bp) != DMU_OT_OBJSET)
			zio_checksum_handle_crypt(&cksum, &saved, insecure);

		abd_copy_from_buf_off(abd, &cksum,
		    eck_offset + offsetof(zio_eck_t, zec_cksum),
		    sizeof (zio_cksum_t));
	} else {
		saved = bp->blk_cksum;
		ci->ci_func[0](abd, size, spa->spa_cksum_tmpls[checksum],
		    &cksum);
		if (BP_USES_CRYPT(bp) && BP_GET_TYPE(bp) != DMU_OT_OBJSET)
			zio_checksum_handle_crypt(&cksum, &saved, insecure);
		bp->blk_cksum = cksum;
	}
}

int
zio_checksum_error_impl(spa_t *spa, const blkptr_t *bp,
    enum zio_checksum checksum, abd_t *abd, uint64_t size, uint64_t offset,
    zio_bad_cksum_t *info)
{
	zio_checksum_info_t *ci = &zio_checksum_table[checksum];
	zio_cksum_t actual_cksum, expected_cksum;
	zio_eck_t eck;
	int byteswap;

	if (checksum >= ZIO_CHECKSUM_FUNCTIONS || ci->ci_func[0] == NULL)
		return (SET_ERROR(EINVAL));

	zio_checksum_template_init(checksum, spa);

	if (ci->ci_flags & ZCHECKSUM_FLAG_EMBEDDED) {
		zio_cksum_t verifier;
		size_t eck_offset;

		if (checksum == ZIO_CHECKSUM_ZILOG2) {
			zil_chain_t zilc;
			uint64_t nused;

			abd_copy_to_buf(&zilc, abd, sizeof (zil_chain_t));

			eck = zilc.zc_eck;
			eck_offset = offsetof(zil_chain_t, zc_eck) +
			    offsetof(zio_eck_t, zec_cksum);

			if (eck.zec_magic == ZEC_MAGIC) {
				nused = zilc.zc_nused;
			} else if (eck.zec_magic == BSWAP_64(ZEC_MAGIC)) {
				nused = BSWAP_64(zilc.zc_nused);
			} else {
				return (SET_ERROR(ECKSUM));
			}

			if (nused > size) {
				return (SET_ERROR(ECKSUM));
			}

			size = P2ROUNDUP_TYPED(nused, ZIL_MIN_BLKSZ, uint64_t);
		} else {
			eck_offset = size - sizeof (zio_eck_t);
			abd_copy_to_buf_off(&eck, abd, eck_offset,
			    sizeof (zio_eck_t));
			eck_offset += offsetof(zio_eck_t, zec_cksum);
		}

		if (checksum == ZIO_CHECKSUM_GANG_HEADER)
			zio_checksum_gang_verifier(&verifier, bp);
		else if (checksum == ZIO_CHECKSUM_LABEL)
			zio_checksum_label_verifier(&verifier, offset);
		else
			verifier = bp->blk_cksum;

		byteswap = (eck.zec_magic == BSWAP_64(ZEC_MAGIC));

		if (byteswap)
			byteswap_uint64_array(&verifier, sizeof (zio_cksum_t));

		expected_cksum = eck.zec_cksum;

		abd_copy_from_buf_off(abd, &verifier, eck_offset,
		    sizeof (zio_cksum_t));

		ci->ci_func[byteswap](abd, size,
		    spa->spa_cksum_tmpls[checksum], &actual_cksum);

		abd_copy_from_buf_off(abd, &expected_cksum, eck_offset,
		    sizeof (zio_cksum_t));

		if (byteswap) {
			byteswap_uint64_array(&expected_cksum,
			    sizeof (zio_cksum_t));
		}
	} else {
		byteswap = BP_SHOULD_BYTESWAP(bp);
		expected_cksum = bp->blk_cksum;
		ci->ci_func[byteswap](abd, size,
		    spa->spa_cksum_tmpls[checksum], &actual_cksum);
	}

	/*
	 * MAC checksums are a special case since half of this checksum will
	 * actually be the encryption MAC. This will be verified by the
	 * decryption process, so we just check the truncated checksum now.
	 * Objset blocks use embedded MACs so we don't truncate the checksum
	 * for them.
	 */
	if (bp != NULL && BP_USES_CRYPT(bp) &&
	    BP_GET_TYPE(bp) != DMU_OT_OBJSET) {
		if (!(ci->ci_flags & ZCHECKSUM_FLAG_DEDUP)) {
			actual_cksum.zc_word[0] ^= actual_cksum.zc_word[2];
			actual_cksum.zc_word[1] ^= actual_cksum.zc_word[3];
		}

		actual_cksum.zc_word[2] = 0;
		actual_cksum.zc_word[3] = 0;
		expected_cksum.zc_word[2] = 0;
		expected_cksum.zc_word[3] = 0;
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

	if (zio_injection_enabled && error == 0 && zio->io_error == 0) {
		error = zio_handle_fault_injection(zio, ECKSUM);
		if (error != 0)
			info->zbc_injected = 1;
	}

	return (error);
}

/*
 * Called by a spa_t that's about to be deallocated. This steps through
 * all of the checksum context templates and deallocates any that were
 * initialized using the algorithm-specific template init function.
 */
void
zio_checksum_templates_free(spa_t *spa)
{
	for (enum zio_checksum checksum = 0;
	    checksum < ZIO_CHECKSUM_FUNCTIONS; checksum++) {
		if (spa->spa_cksum_tmpls[checksum] != NULL) {
			zio_checksum_info_t *ci = &zio_checksum_table[checksum];

			VERIFY(ci->ci_tmpl_free != NULL);
			ci->ci_tmpl_free(spa->spa_cksum_tmpls[checksum]);
			spa->spa_cksum_tmpls[checksum] = NULL;
		}
	}
}
