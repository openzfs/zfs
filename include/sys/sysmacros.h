/*
 *  This file is part of the SPL: Solaris Porting Layer.
 *
 *  Copyright (c) 2008 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory
 *  Written by:
 *          Brian Behlendorf <behlendorf1@llnl.gov>,
 *          Herb Wartens <wartens2@llnl.gov>,
 *          Jim Garlick <garlick@llnl.gov>
 *  UCRL-CODE-235197
 *
 *  This is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#ifndef _SPL_SYSMACROS_H
#define _SPL_SYSMACROS_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <linux/module.h>
#include <sys/debug.h>
#include <sys/varargs.h>
#include <sys/zone.h>
#include <sys/signal.h>

#ifndef _KERNEL
#define _KERNEL                         __KERNEL__
#endif

/* Missing defines.
 */
#define FALSE				0
#define TRUE				1

#define INT32_MAX                       INT_MAX
#define INT32_MIN                       INT_MIN
#define UINT32_MAX			UINT_MAX
#define UINT32_MIN			UINT_MIN
#define INT64_MAX			LLONG_MAX
#define INT64_MIN			LLONG_MIN
#define UINT64_MAX                      ULLONG_MAX
#define UINT64_MIN			ULLONG_MIN

#define NBBY                            8
#define ENOTSUP                         ENOTSUPP

#define MAXMSGLEN			256
#define MAXNAMELEN                      256
#define MAXPATHLEN                      PATH_MAX
#define MAXOFFSET_T			0x7fffffffffffffffl

#define MAXBSIZE			8192
#define DEV_BSIZE			512
#define DEV_BSHIFT			9 /* log2(DEV_BSIZE) */

#define max_ncpus                       64
#define CPU_SEQID			smp_processor_id() /* I think... */
#define _NOTE(x)

#define RLIM64_INFINITY			RLIM_INFINITY

/* 0..MAX_PRIO-1:		Process priority
 * 0..MAX_RT_PRIO-1:		RT priority tasks
 * MAX_RT_PRIO..MAX_PRIO-1:	SCHED_NORMAL tasks
 *
 * Treat shim tasks as SCHED_NORMAL tasks
 */
#define minclsyspri                     (MAX_RT_PRIO)
#define maxclsyspri                     (MAX_PRIO-1)

#define NICE_TO_PRIO(nice)		(MAX_RT_PRIO + (nice) + 20)
#define PRIO_TO_NICE(prio)		((prio) - MAX_RT_PRIO - 20)

/* Missing macros
 */
#define PAGESIZE                        PAGE_SIZE

/* from Solaris sys/byteorder.h */
#define BSWAP_8(x)      ((x) & 0xff)
#define BSWAP_16(x)     ((BSWAP_8(x) << 8) | BSWAP_8((x) >> 8))
#define BSWAP_32(x)     ((BSWAP_16(x) << 16) | BSWAP_16((x) >> 16))
#define BSWAP_64(x)     ((BSWAP_32(x) << 32) | BSWAP_32((x) >> 32))

/* Map some simple functions.
 */
#define bzero(ptr,size)                 memset(ptr,0,size)
#define bcopy(src,dest,size)            memcpy(dest,src,size)
#define bcmp(src,dest,size)		memcmp((src), (dest), (size_t)(size))

/* Dtrace probes do not exist in the linux kernel */
#ifdef DTRACE_PROBE
#undef  DTRACE_PROBE
#endif  /* DTRACE_PROBE */
#define DTRACE_PROBE(a)					((void)0)

#ifdef DTRACE_PROBE1
#undef  DTRACE_PROBE1
#endif  /* DTRACE_PROBE1 */
#define DTRACE_PROBE1(a, b, c)				((void)0)

#ifdef DTRACE_PROBE2
#undef  DTRACE_PROBE2
#endif  /* DTRACE_PROBE2 */
#define DTRACE_PROBE2(a, b, c, d, e)			((void)0)

#ifdef DTRACE_PROBE3
#undef  DTRACE_PROBE3
#endif  /* DTRACE_PROBE3 */
#define DTRACE_PROBE3(a, b, c, d, e, f, g)		((void)0)

#ifdef DTRACE_PROBE4
#undef  DTRACE_PROBE4
#endif  /* DTRACE_PROBE4 */
#define DTRACE_PROBE4(a, b, c, d, e, f, g, h, i)	((void)0)

/* Missing globals */
extern char spl_version[16];
extern long spl_hostid;
extern char hw_serial[11];
extern int p0;

/* Missing misc functions */
extern int highbit(unsigned long i);
extern int ddi_strtoul(const char *str, char **nptr,
		       int base, unsigned long *result);

#define makedevice(maj,min) makedev(maj,min)

/* common macros */
#ifndef MIN
#define MIN(a, b)       ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b)       ((a) < (b) ? (b) : (a))
#endif
#ifndef ABS
#define ABS(a)          ((a) < 0 ? -(a) : (a))
#endif

/*
 * Compatibility macros/typedefs needed for Solaris -> Linux port
 */
#define P2ALIGN(x, align)    ((x) & -(align))
#define P2CROSS(x, y, align) (((x) ^ (y)) > (align) - 1)
#define P2ROUNDUP(x, align)  (-(-(x) & -(align)))
#define P2ROUNDUP_TYPED(x, align, type) \
                             (-(-(type)(x) & -(type)(align)))
#define P2PHASE(x, align)    ((x) & ((align) - 1))
#define P2NPHASE(x, align)   (-(x) & ((align) - 1))
#define P2NPHASE_TYPED(x, align, type) \
                             (-(type)(x) & ((type)(align) - 1))
#define ISP2(x)              (((x) & ((x) - 1)) == 0)
#define IS_P2ALIGNED(v, a)   ((((uintptr_t)(v)) & ((uintptr_t)(a) - 1)) == 0)

/*
 * Typed version of the P2* macros.  These macros should be used to ensure
 * that the result is correctly calculated based on the data type of (x),
 * which is passed in as the last argument, regardless of the data
 * type of the alignment.  For example, if (x) is of type uint64_t,
 * and we want to round it up to a page boundary using "PAGESIZE" as
 * the alignment, we can do either
 * P2ROUNDUP(x, (uint64_t)PAGESIZE)
 * or
 * P2ROUNDUP_TYPED(x, PAGESIZE, uint64_t)
 */
#define P2ALIGN_TYPED(x, align, type)   \
        ((type)(x) & -(type)(align))
#define P2PHASE_TYPED(x, align, type)   \
        ((type)(x) & ((type)(align) - 1))
#define P2NPHASE_TYPED(x, align, type)  \
        (-(type)(x) & ((type)(align) - 1))
#define P2ROUNDUP_TYPED(x, align, type) \
        (-(-(type)(x) & -(type)(align)))
#define P2END_TYPED(x, align, type)     \
        (-(~(type)(x) & -(type)(align)))
#define P2PHASEUP_TYPED(x, align, phase, type)  \
        ((type)(phase) - (((type)(phase) - (type)(x)) & -(type)(align)))
#define P2CROSS_TYPED(x, y, align, type)        \
        (((type)(x) ^ (type)(y)) > (type)(align) - 1)
#define P2SAMEHIGHBIT_TYPED(x, y, type) \
        (((type)(x) ^ (type)(y)) < ((type)(x) & (type)(y)))

#if defined(_KERNEL) && !defined(_KMEMUSER) && !defined(offsetof)

/* avoid any possibility of clashing with <stddef.h> version */

#define offsetof(s, m)  ((size_t)(&(((s *)0)->m)))
#endif

#ifdef HAVE_3ARGS_INIT_WORK

#define spl_init_work(wq,cb,d)	INIT_WORK((wq), (void *)(cb), (void *)(d))
#define spl_get_work_data(type,field,data)	(data)

#else

#define spl_init_work(wq,cb,d)	INIT_WORK((wq), (void *)(cb));
#define spl_get_work_data(type,field,data)	container_of(data,type,field)

#endif

#ifdef  __cplusplus
}
#endif

#endif  /* _SPL_SYSMACROS_H */
