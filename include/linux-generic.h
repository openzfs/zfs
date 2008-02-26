#ifndef _SYS_LINUX_GENERIC_H
#define _SYS_LINUX_GENERIC_H

#ifdef  __cplusplus
extern "C" {
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
#define _KERNEL                         1
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
static int p0 = 0;

#ifdef  __cplusplus
}
#endif

#endif  /* _SYS_LINUX_GENERIC_H */
