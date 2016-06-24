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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_ZFS_FLETCHER_H
#define	_ZFS_FLETCHER_H

#include <sys/types.h>
#include <sys/spa_checksum.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * fletcher checksum functions
 */

void fletcher_2_native(const void *, uint64_t, zio_cksum_t *);
void fletcher_2_byteswap(const void *, uint64_t, zio_cksum_t *);
void fletcher_4_native(const void *, uint64_t, zio_cksum_t *);
void fletcher_4_byteswap(const void *, uint64_t, zio_cksum_t *);
void fletcher_4_incremental_native(const void *, uint64_t,
    zio_cksum_t *);
void fletcher_4_incremental_byteswap(const void *, uint64_t,
    zio_cksum_t *);
int fletcher_4_impl_set(const char *selector);
void fletcher_4_init(void);
void fletcher_4_fini(void);

/*
 * fletcher checksum struct
 */
typedef struct fletcher_4_func {
	void (*init)(zio_cksum_t *);
	void (*fini)(zio_cksum_t *);
	void (*compute)(const void *, uint64_t, zio_cksum_t *);
	void (*compute_byteswap)(const void *, uint64_t, zio_cksum_t *);
	boolean_t (*valid)(void);
	const char *name;
} fletcher_4_ops_t;

#if defined(HAVE_SSE2)
extern const fletcher_4_ops_t fletcher_4_sse2_ops;
#endif

#if defined(HAVE_SSE2) && defined(HAVE_SSSE3)
extern const fletcher_4_ops_t fletcher_4_ssse3_ops;
#endif

#if defined(HAVE_AVX) && defined(HAVE_AVX2)
extern const fletcher_4_ops_t fletcher_4_avx2_ops;
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _ZFS_FLETCHER_H */
