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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*	Copyright (c) 1983, 1984, 1985, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * University Copyright- Copyright (c) 1982, 1986, 1988
 * The Regents of the University of California
 * All Rights Reserved
 *
 * University Acknowledgment- Portions of this document are derived from
 * software developed by the University of California, Berkeley, and its
 * contributors.
 */

#ifndef _SYS_BYTEORDER_H
#define	_SYS_BYTEORDER_H

#include <sys/cdefs.h>
#include <_types.h>

/*
 * Define the order of 32-bit words in 64-bit words.
 */
#define	_QUAD_HIGHWORD 1
#define	_QUAD_LOWWORD 0

/*
 * Definitions for byte order, according to byte significance from low
 * address to high.
 */
#undef _LITTLE_ENDIAN
/* LSB first: i386, vax */
#define	_LITTLE_ENDIAN	1234
/* LSB first in word, MSW first in long */
#define	_PDP_ENDIAN		3412

#define	_BYTE_ORDER		_LITTLE_ENDIAN

/*
 * Deprecated variants that don't have enough underscores to be useful in more
 * strict namespaces.
 */
#if __BSD_VISIBLE
#define	LITTLE_ENDIAN	_LITTLE_ENDIAN
#define	PDP_ENDIAN		_PDP_ENDIAN
#define	BYTE_ORDER		_BYTE_ORDER
#endif

#define	__bswap16_gen(x)	(__uint16_t)((x) << 8 | (x) >> 8)
#define	__bswap32_gen(x)                \
	(((__uint32_t)__bswap16((x) & 0xffff) << 16) | __bswap16((x) >> 16))
#define	__bswap64_gen(x)                \
	(((__uint64_t)__bswap32((x) & 0xffffffff) << 32) | __bswap32((x) >> 32))

#ifdef __GNUCLIKE_BUILTIN_CONSTANT_P
#define	__bswap16(x)                            \
	((__uint16_t)(__builtin_constant_p(x) ? \
	__bswap16_gen((__uint16_t)(x)) : __bswap16_var(x)))
#define	__bswap32(x)                    \
	(__builtin_constant_p(x) ?      \
	__bswap32_gen((__uint32_t)(x)) : __bswap32_var(x))
#define	__bswap64(x)                    \
	(__builtin_constant_p(x) ?      \
	__bswap64_gen((__uint64_t)(x)) : __bswap64_var(x))
#else
/* XXX these are broken for use in static initializers. */
#define	__bswap16(x)    __bswap16_var(x)
#define	__bswap32(x)    __bswap32_var(x)
#define	__bswap64(x)    __bswap64_var(x)
#endif

/* These are defined as functions to avoid multiple evaluation of x. */

static __inline __uint16_t
__bswap16_var(__uint16_t _x)
{

	return (__bswap16_gen(_x));
}

static __inline __uint32_t
__bswap32_var(__uint32_t _x)
{

#ifdef __GNUCLIKE_ASM
	__asm("bswap %0" : "+r" (_x));
	return (_x);
#else
	return (__bswap32_gen(_x));
#endif
}
#define	__htonl(x)	__bswap32(x)
#define	__htons(x)	__bswap16(x)
#define	__ntohl(x)	__bswap32(x)
#define	__ntohs(x)	__bswap16(x)

#include <sys/isa_defs.h>
#include <stdint.h>

#if defined(__GNUC__) && defined(_ASM_INLINES) && \
	(defined(__i386) || defined(__amd64))
#include <asm/byteorder.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * macros for conversion between host and (internet) network byte order
 */

#if defined(_BIG_ENDIAN) && !defined(ntohl) && !defined(__lint)
/* big-endian */
#if defined(_BIG_ENDIAN) && (defined(__amd64__) || defined(__amd64))
#error "incompatible ENDIAN / ARCH combination"
#endif
#define	ntohl(x)	(x)
#define	ntohs(x)	(x)
#define	htonl(x)	(x)
#define	htons(x)	(x)

#elif !defined(ntohl) /* little-endian */

#ifndef	_IN_PORT_T
#define	_IN_PORT_T
typedef uint16_t in_port_t;
#endif

#ifndef	_IN_ADDR_T
#define	_IN_ADDR_T
typedef uint32_t in_addr_t;
#endif

#if !defined(_XPG4_2) || defined(__EXTENSIONS__) || defined(_XPG5)
extern	uint32_t htonl(uint32_t);
extern	uint16_t htons(uint16_t);
extern 	uint32_t ntohl(uint32_t);
extern	uint16_t ntohs(uint16_t);
#else
extern	in_addr_t htonl(in_addr_t);
extern	in_port_t htons(in_port_t);
extern 	in_addr_t ntohl(in_addr_t);
extern	in_port_t ntohs(in_port_t);
#endif	/* !defined(_XPG4_2) || defined(__EXTENSIONS__) || defined(_XPG5) */
#endif

/* 10.8 is lacking in htonll */
#if !defined(htonll)
#define	htonll(x) __DARWIN_OSSwapInt64(x)
#endif

#if !defined(ntohll)
#define	ntohll(x) __DARWIN_OSSwapInt64(x)
#endif

#if !defined(_XPG4_2) || defined(__EXTENSIONS__)

/*
 * Macros to reverse byte order
 */
#define	BSWAP_8(x)	((x) & 0xff)
#define	BSWAP_16(x)	((BSWAP_8(x) << 8) | BSWAP_8((x) >> 8))
#define	BSWAP_32(x)	((BSWAP_16(x) << 16) | BSWAP_16((x) >> 16))
#define	BSWAP_64(x)	((BSWAP_32(x) << 32) | BSWAP_32((x) >> 32))

#define	BMASK_8(x)	((x) & 0xff)
#define	BMASK_16(x)	((x) & 0xffff)
#define	BMASK_32(x)	((x) & 0xffffffff)
#define	BMASK_64(x)	(x)

/*
 * Macros to convert from a specific byte order to/from native byte order
 */
#ifdef _BIG_ENDIAN
#define	BE_8(x)		BMASK_8(x)
#define	BE_16(x)	BMASK_16(x)
#define	BE_32(x)	BMASK_32(x)
#define	BE_64(x)	BMASK_64(x)
#define	LE_8(x)		BSWAP_8(x)
#define	LE_16(x)	BSWAP_16(x)
#define	LE_32(x)	BSWAP_32(x)
#define	LE_64(x)	BSWAP_64(x)
#else
#define	LE_8(x)		BMASK_8(x)
#define	LE_16(x)	BMASK_16(x)
#define	LE_32(x)	BMASK_32(x)
#define	LE_64(x)	BMASK_64(x)
#define	BE_8(x)		BSWAP_8(x)
#define	BE_16(x)	BSWAP_16(x)
#define	BE_32(x)	BSWAP_32(x)
#define	BE_64(x)	BSWAP_64(x)
#endif

/*
 * Macros to read unaligned values from a specific byte order to
 * native byte order
 */

#define	BE_IN8(xa) \
	*((uint8_t *)(xa))

#define	BE_IN16(xa) \
	(((uint16_t)BE_IN8(xa) << 8) | BE_IN8((uint8_t *)(xa)+1))

#define	BE_IN32(xa) \
	(((uint32_t)BE_IN16(xa) << 16) | BE_IN16((uint8_t *)(xa)+2))

#define	BE_IN64(xa) \
	(((uint64_t)BE_IN32(xa) << 32) | BE_IN32((uint8_t *)(xa)+4))

#define	LE_IN8(xa) \
	*((uint8_t *)(xa))

#define	LE_IN16(xa) \
	(((uint16_t)LE_IN8((uint8_t *)(xa) + 1) << 8) | LE_IN8(xa))

#define	LE_IN32(xa) \
	(((uint32_t)LE_IN16((uint8_t *)(xa) + 2) << 16) | LE_IN16(xa))

#define	LE_IN64(xa) \
	(((uint64_t)LE_IN32((uint8_t *)(xa) + 4) << 32) | LE_IN32(xa))

/*
 * Macros to write unaligned values from native byte order to a specific byte
 * order.
 */

#define	BE_OUT8(xa, yv) *((uint8_t *)(xa)) = (uint8_t)(yv);

#define	BE_OUT16(xa, yv) \
	BE_OUT8((uint8_t *)(xa) + 1, yv); \
	BE_OUT8((uint8_t *)(xa), (yv) >> 8);

#define	BE_OUT32(xa, yv) \
	BE_OUT16((uint8_t *)(xa) + 2, yv); \
	BE_OUT16((uint8_t *)(xa), (yv) >> 16);

#define	BE_OUT64(xa, yv) \
	BE_OUT32((uint8_t *)(xa) + 4, yv); \
	BE_OUT32((uint8_t *)(xa), (yv) >> 32);

#define	LE_OUT8(xa, yv) *((uint8_t *)(xa)) = (uint8_t)(yv);

#define	LE_OUT16(xa, yv) \
	LE_OUT8((uint8_t *)(xa), yv); \
	LE_OUT8((uint8_t *)(xa) + 1, (yv) >> 8);

#define	LE_OUT32(xa, yv) \
	LE_OUT16((uint8_t *)(xa), yv); \
	LE_OUT16((uint8_t *)(xa) + 2, (yv) >> 16);

#define	LE_OUT64(xa, yv) \
	LE_OUT32((uint8_t *)(xa), yv); \
	LE_OUT32((uint8_t *)(xa) + 4, (yv) >> 32);

#endif	/* !defined(_XPG4_2) || defined(__EXTENSIONS__) */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_BYTEORDER_H */
