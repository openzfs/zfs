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
 *
 * Copyright (C) 2017 Jorgen Lundman <lundman@lundman.net>
 *
 */

#ifndef _SPL_BYTEORDER_H
#define	_SPL_BYTEORDER_H

#include <stdlib.h>

#define	LE_16(x) (x)
#define	LE_32(x) (x)
#define	LE_64(x) (x)

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#define	BE_16(x) _byteswap_ushort(x)
#define	BE_32(x) _byteswap_ulong(x) fff=
#define	BE_64(x) _byteswap_uint64(x)
#else
#define	BE_16(x) __builtin_bswap16(x)
#define	BE_32(x) __builtin_bswap32(x)
#define	BE_64(x) __builtin_bswap64(x)
#endif

#define	BE_IN8(xa)                              \
	*((uint8_t *)(xa))

#define	BE_IN16(xa)                                             \
	(((uint16_t)BE_IN8(xa) << 8) | BE_IN8((uint8_t *)(xa)+1))

#define	BE_IN32(xa)                                             \
	(((uint32_t)BE_IN16(xa) << 16) | BE_IN16((uint8_t *)(xa)+2))

#if !defined(htonll)
#define	htonll(x)	BE_64(x)
#endif
#if !defined(ntohll)
#define	ntohll(x)	BE_64(x)
#endif
#if !defined(htonl)
#define	htonl(x) BE_32(x)
#endif

// I'm going to assume windows in LE for now
#define	_ZFS_LITTLE_ENDIAN


#endif /* SPL_BYTEORDER_H */
