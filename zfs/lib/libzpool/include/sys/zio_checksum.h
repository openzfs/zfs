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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SYS_ZIO_CHECKSUM_H
#define	_SYS_ZIO_CHECKSUM_H

#include <sys/zio.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Signature for checksum functions.
 */
typedef void zio_checksum_t(const void *data, uint64_t size, zio_cksum_t *zcp);

/*
 * Information about each checksum function.
 */
typedef struct zio_checksum_info {
	zio_checksum_t	*ci_func[2]; /* checksum function for each byteorder */
	int		ci_correctable;	/* number of correctable bits	*/
	int		ci_zbt;		/* uses zio block tail?	*/
	char		*ci_name;	/* descriptive name */
} zio_checksum_info_t;

extern zio_checksum_info_t zio_checksum_table[ZIO_CHECKSUM_FUNCTIONS];

/*
 * Checksum routines.
 */
extern zio_checksum_t fletcher_2_native;
extern zio_checksum_t fletcher_4_native;
extern zio_checksum_t fletcher_4_incremental_native;

extern zio_checksum_t fletcher_2_byteswap;
extern zio_checksum_t fletcher_4_byteswap;
extern zio_checksum_t fletcher_4_incremental_byteswap;

extern zio_checksum_t zio_checksum_SHA256;

extern void zio_checksum_compute(zio_t *zio, enum zio_checksum checksum,
    void *data, uint64_t size);
extern int zio_checksum_error(zio_t *zio);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ZIO_CHECKSUM_H */
