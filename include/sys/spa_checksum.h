// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */
/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SPA_CHECKSUM_H
#define	_SPA_CHECKSUM_H

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Each block has a 256-bit checksum -- strong enough for cryptographic hashes.
 */
typedef struct zio_cksum {
	uint64_t	zc_word[4];
} zio_cksum_t;

#define	ZIO_SET_CHECKSUM(zcp, w0, w1, w2, w3)	\
{						\
	(zcp)->zc_word[0] = w0;			\
	(zcp)->zc_word[1] = w1;			\
	(zcp)->zc_word[2] = w2;			\
	(zcp)->zc_word[3] = w3;			\
}

#define	ZIO_CHECKSUM_EQUAL(zc1, zc2) \
	(0 == (((zc1).zc_word[0] - (zc2).zc_word[0]) | \
	((zc1).zc_word[1] - (zc2).zc_word[1]) | \
	((zc1).zc_word[2] - (zc2).zc_word[2]) | \
	((zc1).zc_word[3] - (zc2).zc_word[3])))

#define	ZIO_CHECKSUM_IS_ZERO(zc) \
	(0 == ((zc)->zc_word[0] | (zc)->zc_word[1] | \
	(zc)->zc_word[2] | (zc)->zc_word[3]))

#define	ZIO_CHECKSUM_BSWAP(zcp)					\
{								\
	(zcp)->zc_word[0] = BSWAP_64((zcp)->zc_word[0]);	\
	(zcp)->zc_word[1] = BSWAP_64((zcp)->zc_word[1]);	\
	(zcp)->zc_word[2] = BSWAP_64((zcp)->zc_word[2]);	\
	(zcp)->zc_word[3] = BSWAP_64((zcp)->zc_word[3]);	\
}

#ifdef	__cplusplus
}
#endif

#endif
