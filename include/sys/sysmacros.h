#ifndef _SPL_SYSMACROS_H
#define _SPL_SYSMACROS_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <linux/module.h>

#ifndef _KERNEL
#define _KERNEL                         __KERNEL__
#endif

/* Missing defines.
 */
#define INT32_MAX                       INT_MAX
#define UINT64_MAX                      (~0ULL)
#define NBBY                            8
#define ENOTSUP                         ENOTSUPP
#define MAXNAMELEN                      256
#define MAXPATHLEN                      PATH_MAX
#define __va_list                       va_list
#define max_ncpus                       64

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

#define kred                            NULL

#define FREAD                           1
#define FWRITE                          2
#define FCREAT  O_CREAT
#define FTRUNC  O_TRUNC
#define FOFFMAX O_LARGEFILE
#define FSYNC   O_SYNC
#define FDSYNC  O_DSYNC
#define FRSYNC  O_RSYNC
#define FEXCL   O_EXCL

#define FNODSYNC  0x10000 /* fsync pseudo flag */
#define FNOFOLLOW 0x20000 /* don't follow symlinks */

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
#define ASSERT(x)                       BUG_ON(!(x))
#define ASSERT3U(left,OP,right)         BUG_ON(!((left) OP (right)))

/* Missing globals
 */
extern int p0;

#define makedevice(maj,min) makedev(maj,min)

/* XXX - Borrowed from zfs project libsolcompat/include/sys/sysmacros.h */
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

#ifdef  __cplusplus
}
#endif

#endif  /* _SPL_SYSMACROS_H */
