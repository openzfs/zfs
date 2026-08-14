// SPDX-License-Identifier: CDDL-1.0
/*
 * NetBSD overlay for <sys/bitops.h>.
 *
 * include_next must land on NetBSD, not OpenZFS include/sys/bitops.h
 * (that header pulls zfs_context.h and hides __BITMAP_TYPE).
 * Makefile.inc.netbsd puts ${NETBSDSRCDIR}/sys before OpenZFS include/.
 */

#ifndef	_OPENSOLARIS_SYS_BITOPS_H_
#define	_OPENSOLARIS_SYS_BITOPS_H_

#include_next <sys/bitops.h>

#ifndef __BITMAP_TYPE
#error "NetBSD <sys/bitops.h> missing __BITMAP_TYPE; fix CPPFLAGS"
#endif

/* Hide OpenZFS include/sys/bitops.h if it is later on the path. */
#ifndef _SYS_BITOPS_H
#define	_SYS_BITOPS_H
#endif

#ifndef P2PHASE
#define	P2PHASE(x, align)	((x) & ((align) - 1))
#endif
#ifndef IS_P2ALIGNED
#define	IS_P2ALIGNED(v, a)	\
	((((uintptr_t)(v)) & ((uintptr_t)(a) - 1)) == 0)
#endif
#ifndef ASSERT3U
#define	ASSERT3U(l, op, r)	((void)sizeof (l), (void)sizeof (r))
#endif
#ifndef ASSERT3S
#define	ASSERT3S(l, op, r)	((void)sizeof (l), (void)sizeof (r))
#endif

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

#define	BF32_SET_SB(x, low, len, shift, bias, val) do { \
	ASSERT3U(IS_P2ALIGNED(val, 1U << shift), !=, 0); \
	ASSERT3S((val) >> (shift), >=, bias); \
	BF32_SET(x, low, len, ((val) >> (shift)) - (bias)); \
} while (0)
#define	BF64_SET_SB(x, low, len, shift, bias, val) do { \
	ASSERT3U(IS_P2ALIGNED(val, 1ULL << shift), !=, 0); \
	ASSERT3S((val) >> (shift), >=, bias); \
	BF64_SET(x, low, len, ((val) >> (shift)) - (bias)); \
} while (0)

#endif /* _OPENSOLARIS_SYS_BITOPS_H_ */
