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

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/


#ifndef _SYS_BITMAP_H
#define	_SYS_BITMAP_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Operations on bitmaps of arbitrary size
 * A bitmap is a vector of 1 or more ulong_t's.
 * The user of the package is responsible for range checks and keeping
 * track of sizes.
 */

#ifdef _LP64
#define	BT_ULSHIFT	6 /* log base 2 of BT_NBIPUL, to extract word index */
#define	BT_ULSHIFT32	5 /* log base 2 of BT_NBIPUL, to extract word index */
#else
#define	BT_ULSHIFT	5 /* log base 2 of BT_NBIPUL, to extract word index */
#endif

#define	BT_NBIPUL	(1 << BT_ULSHIFT)	/* n bits per ulong_t */
#define	BT_ULMASK	(BT_NBIPUL - 1)		/* to extract bit index */

/*
 * bitmap is a ulong_t *, bitindex an index_t
 *
 * The macros BT_WIM and BT_BIW internal; there is no need
 * for users of this package to use them.
 */

/*
 * word in map
 */
#define	BT_WIM(bitmap, bitindex) \
	((bitmap)[(bitindex) >> BT_ULSHIFT])
/*
 * bit in word
 */
#define	BT_BIW(bitindex) \
	(1UL << ((bitindex) & BT_ULMASK))

/*
 * These are public macros
 *
 * BT_BITOUL == n bits to n ulong_t's
 */
#define	BT_BITOUL(nbits) \
	(((nbits) + BT_NBIPUL - 1l) / BT_NBIPUL)
#define	BT_SIZEOFMAP(nbits) \
	(BT_BITOUL(nbits) * sizeof (ulong_t))
#define	BT_TEST(bitmap, bitindex) \
	((BT_WIM((bitmap), (bitindex)) & BT_BIW(bitindex)) ? 1 : 0)
#define	BT_SET(bitmap, bitindex) \
	{ BT_WIM((bitmap), (bitindex)) |= BT_BIW(bitindex); }
#define	BT_CLEAR(bitmap, bitindex) \
	{ BT_WIM((bitmap), (bitindex)) &= ~BT_BIW(bitindex); }

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_BITMAP_H */
