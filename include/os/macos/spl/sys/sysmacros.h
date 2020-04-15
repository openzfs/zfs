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
 * Copyright (C) 2013 Jorgen Lundman <lundman@lundman.net>
 *
 */

#ifndef _SPL_SYSMACROS_H
#define	_SPL_SYSMACROS_H

#include <TargetConditionals.h>
#include <AvailabilityMacros.h>
#include <sys/types.h>
#include <string.h>
#include <sys/varargs.h>
#include <sys/zone.h>
#include <sys/signal.h>
#include <sys/param.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _KERNEL
#define	_KERNEL				__KERNEL__
#endif

#define	FALSE				0
#define	TRUE				1

#if 0
#define	INT8_MAX			(127)
#define	INT8_MIN			(-128)
#define	UINT8_MAX			(255)
#define	UINT8_MIN			(0)

#define	INT16_MAX			(32767)
#define	INT16_MIN			(-32768)
#define	UINT16_MAX			(65535)
#define	UINT16_MIN			(0)

#define	INT32_MAX			INT_MAX
#define	INT32_MIN			INT_MIN
#define	UINT32_MAX			UINT_MAX
#define	UINT32_MIN			UINT_MIN

#define	INT64_MAX			LLONG_MAX
#define	INT64_MIN			LLONG_MIN
#define	UINT64_MAX			ULLONG_MAX
#define	UINT64_MIN			ULLONG_MIN

#define	NBBY				8
#define	MAXBSIZE			8192
#endif

#define	MAXMSGLEN			256
#define	MAXNAMELEN			256
#define	MAXPATHLEN			PATH_MAX
#define	MAXOFFSET_T			LLONG_MAX
#define	DEV_BSIZE			512
#define	DEV_BSHIFT			9 /* log2(DEV_BSIZE) */

#define	proc_pageout			NULL
#define	curproc				(struct proc *)current_proc()

#ifndef __arm64__
extern int cpu_number(void);
#define	CPU_SEQID			(cpu_number())
#define	CPU_SEQID_UNSTABLE	(cpu_number())
#else
#define	CPU_SEQID			(0) // Find solution
#define	CPU_SEQID_UNSTABLE	(0)
#endif
#define	is_system_labeled()		0

extern unsigned int max_ncpus;
#define	boot_ncpus max_ncpus

#ifndef RLIM64_INFINITY
#define	RLIM64_INFINITY			(~0ULL)
#endif

/*
 * 0..MAX_PRIO-1:		Process priority
 * 0..MAX_RT_PRIO-1:		RT priority tasks
 * MAX_RT_PRIO..MAX_PRIO-1:	SCHED_NORMAL tasks
 *
 * Treat shim tasks as SCHED_NORMAL tasks
 */

/*
 * In OSX, the kernel thread priorities start at 81 and goes to
 * 95 MAXPRI_KERNEL. BASEPRI_REALTIME starts from 96. Since
 * swap priority is at 92.  ZFS priorities should have a base below
 * 81 in general.  Xnu will dynamically adjust priorities of
 * some taskq threads around maxclsyspri.
 */
#define	minclsyspri  70 /* well below the render server and other graphics */
#define	defclsyspri  75 /* five below the xnu kernel services */
#define	maxclsyspri  80 /* 1 less than base, 2 less than networking */

/*
 * Missing macros
 */
#define	PAGESIZE			PAGE_SIZE

/* from Solaris sys/byteorder.h */
#define	BSWAP_8(x)	((x) & 0xff)
#define	BSWAP_16(x)	((BSWAP_8(x) << 8) | BSWAP_8((x) >> 8))
#define	BSWAP_32(x)	((BSWAP_16(x) << 16) | BSWAP_16((x) >> 16))
#define	BSWAP_64(x)	((BSWAP_32(x) << 32) | BSWAP_32((x) >> 32))


/* Dtrace probes do not exist in the linux kernel */
#ifdef DTRACE_PROBE
#undef  DTRACE_PROBE
#endif  /* DTRACE_PROBE */
#define	DTRACE_PROBE(a)					((void)0)

#ifdef DTRACE_PROBE1
#undef  DTRACE_PROBE1
#endif  /* DTRACE_PROBE1 */
#define	DTRACE_PROBE1(a, b, c)				((void)0)

#ifdef DTRACE_PROBE2
#undef  DTRACE_PROBE2
#endif  /* DTRACE_PROBE2 */
#define	DTRACE_PROBE2(a, b, c, d, e)			((void)0)

#ifdef DTRACE_PROBE3
#undef  DTRACE_PROBE3
#endif  /* DTRACE_PROBE3 */
#define	DTRACE_PROBE3(a, b, c, d, e, f, g)		((void)0)

#ifdef DTRACE_PROBE4
#undef  DTRACE_PROBE4
#endif  /* DTRACE_PROBE4 */
#define	DTRACE_PROBE4(a, b, c, d, e, f, g, h, i)	((void)0)

/* Missing globals */
extern char spl_version[32];
extern unsigned long spl_hostid;
extern char hw_serial[11];

/* Missing misc functions */
extern uint32_t zone_get_hostid(void *zone);
extern void spl_setup(void);
extern void spl_cleanup(void);

#define	makedevice(maj, min) makedev(maj, min)

/* common macros */
#ifndef MIN
#define	MIN(a, b)		((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define	MAX(a, b)		((a) < (b) ? (b) : (a))
#endif
#ifndef ABS
#define	ABS(a)			((a) < 0 ? -(a) : (a))
#endif
#ifndef DIV_ROUND_UP
#define	DIV_ROUND_UP(n, d)	(((n) + (d) - 1) / (d))
#endif

#ifndef ARRAY_SIZE
#define	ARRAY_SIZE(a) (sizeof (a) / sizeof (a[0]))
#endif

/*
 * Compatibility macros/typedefs needed for Solaris -> Linux port
 */
#define	P2ALIGN(x, align)	((x) & -(align))
#define	P2CROSS(x, y, align)	(((x) ^ (y)) > (align) - 1)
#define	P2ROUNDUP(x, align)	(-(-(x) & -(align)))
#define	P2PHASE(x, align)	((x) & ((align) - 1))
#define	P2NPHASE(x, align)	(-(x) & ((align) - 1))
#define	ISP2(x)			(((x) & ((x) - 1)) == 0)
#define	IS_P2ALIGNED(v, a)	((((uintptr_t)(v)) & ((uintptr_t)(a) - 1)) == 0)
#define	P2BOUNDARY(off, len, align) \
				(((off) ^ ((off) + (len) - 1)) > (align) - 1)

/*
 * Typed version of the P2* macros.  These macros should be used to ensure
 * that the result is correctly calculated based on the data type of (x),
 * which is passed in as the last argument, regardless of the data
 * type of the alignment.  For example, if (x) is of type uint64_t,
 * and we want to round it up to a page boundary using "PAGESIZE" as
 * the alignment, we can do either
 *
 * P2ROUNDUP(x, (uint64_t)PAGESIZE)
 * or
 * P2ROUNDUP_TYPED(x, PAGESIZE, uint64_t)
 */
#define	P2ALIGN_TYPED(x, align, type)   \
	((type)(x) & -(type)(align))
#define	P2PHASE_TYPED(x, align, type)   \
	((type)(x) & ((type)(align) - 1))
#define	P2NPHASE_TYPED(x, align, type)  \
	(-(type)(x) & ((type)(align) - 1))
#define	P2ROUNDUP_TYPED(x, align, type) \
	(-(-(type)(x) & -(type)(align)))
#define	P2END_TYPED(x, align, type)     \
	(-(~(type)(x) & -(type)(align)))
#define	P2PHASEUP_TYPED(x, align, phase, type)  \
	((type)(phase) - (((type)(phase) - (type)(x)) & -(type)(align)))
#define	P2CROSS_TYPED(x, y, align, type)        \
	(((type)(x) ^ (type)(y)) > (type)(align) - 1)
#define	P2SAMEHIGHBIT_TYPED(x, y, type) \
	(((type)(x) ^ (type)(y)) < ((type)(x) & (type)(y)))

/*
 * P2* Macros from Illumos
 */

/*
 * return x rounded up to the next phase (offset) within align.
 * phase should be < align.
 * eg, P2PHASEUP(0x1234, 0x100, 0x10) == 0x1310 (0x13*align + phase)
 * eg, P2PHASEUP(0x5600, 0x100, 0x10) == 0x5610 (0x56*align + phase)
 */
#define	P2PHASEUP(x, align, phase)	((phase) - (((phase) - (x)) & -(align)))

/*
 * Return TRUE if they have the same highest bit set.
 * eg, P2SAMEHIGHBIT(0x1234, 0x1001) == TRUE (the high bit is 0x1000)
 * eg, P2SAMEHIGHBIT(0x1234, 0x3010) == FALSE (high bit of 0x3010 is 0x2000)
 */
#define	P2SAMEHIGHBIT(x, y)		(((x) ^ (y)) < ((x) & (y)))

/*
 * End Illumos copy-fest
 */

/* avoid any possibility of clashing with <stddef.h> version */
#if defined(_KERNEL) && !defined(_KMEMUSER) && !defined(offsetof)
/*
 * Use the correct builtin mechanism. The Traditional macro is
 * not safe on this platform.
 */
#define	offsetof(s, m)  __builtin_offsetof(s, m)
#endif

#define	SET_ERROR(X) (X)

#ifdef __cplusplus
}
#endif

#endif  /* _SPL_SYSMACROS_H */
