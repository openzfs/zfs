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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"@(#)zio_checksum.c	1.6	06/11/10 SMI"

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/zio.h>
#include <sys/zio_checksum.h>

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
 * data, so we store the checksum *function* in five bits of the DVA.
 * This gives us room for up to 32 different checksum functions.
 *
 * When writing a block, we always checksum it with the latest-and-greatest
 * checksum function of the appropriate strength.  When reading a block,
 * we compare the expected checksum against the actual checksum, which we
 * compute via the checksum function specified in the DVA encoding.
 */

/*ARGSUSED*/
static void
zio_checksum_off(const void *buf, uint64_t size, zio_cksum_t *zcp)
{
	ZIO_SET_CHECKSUM(zcp, 0, 0, 0, 0);
}

zio_checksum_info_t zio_checksum_table[ZIO_CHECKSUM_FUNCTIONS] = {
	{{NULL,			NULL},			0, 0,	"inherit"},
	{{NULL,			NULL},			0, 0,	"on"},
	{{zio_checksum_off,	zio_checksum_off},	0, 0,	"off"},
	{{zio_checksum_SHA256,	zio_checksum_SHA256},	1, 1,	"label"},
	{{zio_checksum_SHA256,	zio_checksum_SHA256},	1, 1,	"gang_header"},
	{{fletcher_2_native,	fletcher_2_byteswap},	0, 1,	"zilog"},
	{{fletcher_2_native,	fletcher_2_byteswap},	0, 0,	"fletcher2"},
	{{fletcher_4_native,	fletcher_4_byteswap},	1, 0,	"fletcher4"},
	{{zio_checksum_SHA256,	zio_checksum_SHA256},	1, 0,	"SHA256"},
};

uint8_t
zio_checksum_select(uint8_t child, uint8_t parent)
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

/*
 * Generate the checksum.
 */
void
zio_checksum(uint_t checksum, zio_cksum_t *zcp, void *data, uint64_t size)
{
	zio_block_tail_t *zbt = (zio_block_tail_t *)((char *)data + size) - 1;
	zio_checksum_info_t *ci = &zio_checksum_table[checksum];
	zio_cksum_t zbt_cksum;

	ASSERT(checksum < ZIO_CHECKSUM_FUNCTIONS);
	ASSERT(ci->ci_func[0] != NULL);

	if (ci->ci_zbt) {
		*zcp = zbt->zbt_cksum;
		zbt->zbt_magic = ZBT_MAGIC;
		ci->ci_func[0](data, size, &zbt_cksum);
		zbt->zbt_cksum = zbt_cksum;
	} else {
		ci->ci_func[0](data, size, zcp);
	}
}

int
zio_checksum_error(zio_t *zio)
{
	blkptr_t *bp = zio->io_bp;
	zio_cksum_t zc = bp->blk_cksum;
	uint_t checksum = BP_IS_GANG(bp) ? ZIO_CHECKSUM_GANG_HEADER :
	    BP_GET_CHECKSUM(bp);
	int byteswap = BP_SHOULD_BYTESWAP(bp);
	void *data = zio->io_data;
	uint64_t size = ZIO_GET_IOSIZE(zio);
	zio_block_tail_t *zbt = (zio_block_tail_t *)((char *)data + size) - 1;
	zio_checksum_info_t *ci = &zio_checksum_table[checksum];
	zio_cksum_t actual_cksum, expected_cksum;

	if (checksum >= ZIO_CHECKSUM_FUNCTIONS || ci->ci_func[0] == NULL)
		return (EINVAL);

	if (ci->ci_zbt) {
		if (checksum == ZIO_CHECKSUM_GANG_HEADER)
			zio_set_gang_verifier(zio, &zc);

		if (zbt->zbt_magic == BSWAP_64(ZBT_MAGIC)) {
			expected_cksum = zbt->zbt_cksum;
			byteswap_uint64_array(&expected_cksum,
			    sizeof (zio_cksum_t));
			zbt->zbt_cksum = zc;
			byteswap_uint64_array(&zbt->zbt_cksum,
			    sizeof (zio_cksum_t));
			ci->ci_func[1](data, size, &actual_cksum);
			zbt->zbt_cksum = expected_cksum;
			byteswap_uint64_array(&zbt->zbt_cksum,
			    sizeof (zio_cksum_t));
		} else {
			expected_cksum = zbt->zbt_cksum;
			zbt->zbt_cksum = zc;
			ci->ci_func[0](data, size, &actual_cksum);
			zbt->zbt_cksum = expected_cksum;
		}
		zc = expected_cksum;
	} else {
		ASSERT(!BP_IS_GANG(bp));
		ci->ci_func[byteswap](data, size, &actual_cksum);
	}

	if (!ZIO_CHECKSUM_EQUAL(actual_cksum, zc))
		return (ECKSUM);

	if (zio_injection_enabled && !zio->io_error)
		return (zio_handle_fault_injection(zio, ECKSUM));

	return (0);
}
