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
 *
 * Copyright (C) 2017 Jorgen Lundman <lundman@lundman.net>
 *
 */

#ifndef _SPL_BYTEORDER_H
#define _SPL_BYTEORDER_H

//#include <libkern/OSByteOrder.h>
//#include <machine/byte_order.h>
#include <stdlib.h>

#define LE_16(x) (x)
#define LE_32(x) (x)
#define LE_64(x) (x)

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#define BE_16(x) _byteswap_ushort(x)
#define BE_32(x) _byteswap_ulong(x) fff=
#define BE_64(x) _byteswap_uint64(x)
#else
#define BE_16(x) __builtin_bswap16(x)
#define BE_32(x) __builtin_bswap32(x)
#define BE_64(x) __builtin_bswap64(x)
#endif

#define BE_IN8(xa)                              \
    *((uint8_t *)(xa))

#define BE_IN16(xa)                                             \
    (((uint16_t)BE_IN8(xa) << 8) | BE_IN8((uint8_t *)(xa)+1))

#define BE_IN32(xa)                                             \
    (((uint32_t)BE_IN16(xa) << 16) | BE_IN16((uint8_t *)(xa)+2))


#if !defined(htonll)
#define htonll(x)       BE_64(x)
#endif
#if !defined(ntohll)
#define ntohll(x)       BE_64(x)
#endif
#if !defined(htonl)
#define htonl(x) BE_32(x)
#endif

// I'm going to assume windows in LE for now
#define _ZFS_LITTLE_ENDIAN


#endif /* SPL_BYTEORDER_H */
