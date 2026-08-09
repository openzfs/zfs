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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2011, 2019 by Delphix. All rights reserved.
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2014 Spectra Logic Corporation, All rights reserved.
 * Copyright 2013 Saso Kiselkov. All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 * Copyright 2017 Joyent, Inc.
 * Copyright (c) 2017 Datto Inc.
 */

#ifndef _SYS_BITOPS_H
#define	_SYS_BITOPS_H

#include <sys/zfs_context.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * General-purpose 32-bit and 64-bit bitfield encodings.
 */
#define	BF32_DECODE(x, low, len)	P2PHASE((x) >> (low), 1U << (len))
#define	BF64_DECODE(x, low, len)	P2PHASE((x) >> (low), 1ULL << (len))
#define	BF32_ENCODE(x, low, len)	(P2PHASE((x), 1U << (len)) << (low))
#define	BF64_ENCODE(x, low, len)	(P2PHASE((x), 1ULL << (len)) << (low))

#define	BF32_GET(x, low, len)		BF32_DECODE(x, low, len)
#define	BF64_GET(x, low, len)		BF64_DECODE(x, low, len)

#define	BF32_SET(x, low, len, val) do { \
	ASSERT3U(val, <, 1U << (len)); \
	ASSERT3U(low + len, <=, 32); \
	(x) ^= BF32_ENCODE((x >> low) ^ (val), low, len); \
} while (0)

#define	BF64_SET(x, low, len, val) do { \
	ASSERT3U(val, <, 1ULL << (len)); \
	ASSERT3U(low + len, <=, 64); \
	((x) ^= BF64_ENCODE((x >> low) ^ (val), low, len)); \
} while (0)

#define	BF32_GET_SB(x, low, len, shift, bias)	\
	((BF32_GET(x, low, len) + (bias)) << (shift))
#define	BF64_GET_SB(x, low, len, shift, bias)	\
	((BF64_GET(x, low, len) + (bias)) << (shift))

/*
 * We use ASSERT3U instead of ASSERT in these macros to prevent a lint error in
 * the case where val is a constant.  We can't fix ASSERT because it's used as
 * an expression in several places in the kernel.
 */
#define	BF32_SET_SB(x, low, len, shift, bias, val) do { \
	ASSERT3U(IS_P2ALIGNED(val, 1U << shift), !=, B_FALSE); \
	ASSERT3S((val) >> (shift), >=, bias); \
	BF32_SET(x, low, len, ((val) >> (shift)) - (bias)); \
} while (0)
#define	BF64_SET_SB(x, low, len, shift, bias, val) do { \
	ASSERT3U(IS_P2ALIGNED(val, 1ULL << shift), !=, B_FALSE); \
	ASSERT3S((val) >> (shift), >=, bias); \
	BF64_SET(x, low, len, ((val) >> (shift)) - (bias)); \
} while (0)

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_BITOPS_H */
