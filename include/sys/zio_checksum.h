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
 * Copyright (c) 2014, 2016 by Delphix. All rights reserved.
 */

#ifndef _SYS_ZIO_CHECKSUM_H
#define	_SYS_ZIO_CHECKSUM_H

#include <sys/zio.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct abd;

/*
 * Signature for checksum functions.
 */
typedef void zio_checksum_t(struct abd *, uint64_t, zio_cksum_t *);

typedef enum {
	ZIO_CHECKSUM_NATIVE,
	ZIO_CHECKSUM_BYTESWAP
} zio_byteorder_t;

typedef struct zio_abd_checksum_data {
	zio_byteorder_t		acd_byteorder;
	zio_cksum_t 		*acd_zcp;
	void 			*acd_private;
} zio_abd_checksum_data_t;

typedef void zio_abd_checksum_init_t(zio_abd_checksum_data_t *);
typedef void zio_abd_checksum_fini_t(zio_abd_checksum_data_t *);
typedef int zio_abd_checksum_iter_t(void *, size_t, void *);

typedef const struct zio_abd_checksum_func {
	zio_abd_checksum_init_t *acf_init;
	zio_abd_checksum_fini_t *acf_fini;
	zio_abd_checksum_iter_t *acf_iter;
} zio_abd_checksum_func_t;


/*
 * Information about each checksum function.
 */
typedef const struct zio_checksum_info {
	zio_checksum_t *ci_func[2];	/* checksum function per byteorder */
	int		ci_correctable;	/* number of correctable bits	*/
	int		ci_eck;		/* uses zio embedded checksum? */
	boolean_t	ci_dedup;	/* strong enough for dedup? */
	char		*ci_name;	/* descriptive name */
} zio_checksum_info_t;

typedef struct zio_bad_cksum {
	zio_cksum_t		zbc_expected;
	zio_cksum_t		zbc_actual;
	const char		*zbc_checksum_name;
	uint8_t			zbc_byteswapped;
	uint8_t			zbc_injected;
	uint8_t			zbc_has_cksum;	/* expected/actual valid */
} zio_bad_cksum_t;

extern zio_checksum_info_t zio_checksum_table[ZIO_CHECKSUM_FUNCTIONS];

/*
 * Checksum routines.
 */
extern void zio_checksum_SHA256(const void *, uint64_t, zio_cksum_t *);

extern zio_checksum_t abd_checksum_SHA256;
extern zio_checksum_t abd_checksum_SHA512_native;
extern zio_checksum_t abd_checksum_SHA512_byteswap;

extern zio_abd_checksum_func_t fletcher_4_abd_ops;
extern zio_checksum_t abd_fletcher_4_native;
extern zio_checksum_t abd_fletcher_4_byteswap;

extern int zio_checksum_equal(spa_t *, blkptr_t *, enum zio_checksum,
    void *, uint64_t, uint64_t, zio_bad_cksum_t *);
extern void zio_checksum_compute(zio_t *, enum zio_checksum,
    struct abd *, uint64_t);
extern int zio_checksum_error_impl(spa_t *, const blkptr_t *, enum zio_checksum,
    struct abd *, uint64_t, uint64_t, zio_bad_cksum_t *);
extern int zio_checksum_error(zio_t *zio, zio_bad_cksum_t *out);
extern enum zio_checksum spa_dedup_checksum(spa_t *spa);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ZIO_CHECKSUM_H */
