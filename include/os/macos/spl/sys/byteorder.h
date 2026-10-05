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
 * Copyright (C) 2013 Jorgen Lundman <lundman@lundman.net>
 *
 */

#ifndef _SPL_BYTEORDER_H
#define	_SPL_BYTEORDER_H

#include <TargetConditionals.h>
#include <AvailabilityMacros.h>

#include <libkern/OSByteOrder.h>
#include <machine/byte_order.h>
#include <sys/sysmacros.h>

#define	LE_16(x) OSSwapHostToLittleInt16(x)
#define	LE_32(x) OSSwapHostToLittleInt32(x)
#define	LE_64(x) OSSwapHostToLittleInt64(x)
#define	BE_16(x) OSSwapHostToBigInt16(x)
#define	BE_32(x) OSSwapHostToBigInt32(x)
#define	BE_64(x) OSSwapHostToBigInt64(x)

#define	BE_IN8(xa) \
	*((uint8_t *)(xa))

#define	BE_IN16(xa) \
	(((uint16_t)BE_IN8(xa) << 8) | BE_IN8((uint8_t *)(xa)+1))

#define	BE_IN32(xa) \
	(((uint32_t)BE_IN16(xa) << 16) | BE_IN16((uint8_t *)(xa)+2))


/* 10.8 is lacking in htonll */
#if !defined(htonll)
#define	htonll(x)	__DARWIN_OSSwapInt64(x)
#endif
#if !defined(ntohll)
#define	ntohll(x)	__DARWIN_OSSwapInt64(x)
#endif

#ifdef __LITTLE_ENDIAN__
#define	_ZFS_LITTLE_ENDIAN
#endif

#ifdef __BIG_ENDIAN__
#define	_ZFS_BIG_ENDIAN
#endif

#endif /* SPL_BYTEORDER_H */
