// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _SPL_BYTEORDER_H
#define	_SPL_BYTEORDER_H

#include <asm/byteorder.h>

#if defined(__BIG_ENDIAN) && !defined(_ZFS_BIG_ENDIAN)
#define	_ZFS_BIG_ENDIAN
#endif

#if defined(__LITTLE_ENDIAN) && !defined(_ZFS_LITTLE_ENDIAN)
#define	_ZFS_LITTLE_ENDIAN
#endif

#include <sys/isa_defs.h>

#ifdef __COVERITY__
/*
 * Coverity's taint warnings from byteswapping are false positives for us.
 * Suppress them by hiding byteswapping from Coverity.
 */

#define	BSWAP_8(x)	((x) & 0xff)
#define	BSWAP_16(x)	((x) & 0xffff)
#define	BSWAP_32(x)	((x) & 0xffffffff)
#define	BSWAP_64(x)	(x)

#else /* __COVERITY__ */

#define	BSWAP_8(x)	((x) & 0xff)
#define	BSWAP_16(x)	((BSWAP_8(x) << 8) | BSWAP_8((x) >> 8))
#define	BSWAP_32(x)	((BSWAP_16(x) << 16) | BSWAP_16((x) >> 16))
#define	BSWAP_64(x)	((BSWAP_32(x) << 32) | BSWAP_32((x) >> 32))

#endif /* __COVERITY__ */

#define	LE_16(x)	cpu_to_le16(x)
#define	LE_32(x)	cpu_to_le32(x)
#define	LE_64(x)	cpu_to_le64(x)
#define	BE_16(x)	cpu_to_be16(x)
#define	BE_32(x)	cpu_to_be32(x)
#define	BE_64(x)	cpu_to_be64(x)

#define	BE_IN8(xa) \
	*((uint8_t *)(xa))

#define	BE_IN16(xa) \
	(((uint16_t)BE_IN8(xa) << 8) | BE_IN8((uint8_t *)(xa)+1))

#define	BE_IN32(xa) \
	(((uint32_t)BE_IN16(xa) << 16) | BE_IN16((uint8_t *)(xa)+2))

#ifdef _ZFS_BIG_ENDIAN
static __inline__ uint64_t
htonll(uint64_t n)
{
	return (n);
}

static __inline__ uint64_t
ntohll(uint64_t n)
{
	return (n);
}
#else
static __inline__ uint64_t
htonll(uint64_t n)
{
	return ((((uint64_t)htonl(n)) << 32) + htonl(n >> 32));
}

static __inline__ uint64_t
ntohll(uint64_t n)
{
	return ((((uint64_t)ntohl(n)) << 32) + ntohl(n >> 32));
}
#endif

#endif /* SPL_BYTEORDER_H */
