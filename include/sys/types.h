#ifndef _SPL_TYPES_H
#define	_SPL_TYPES_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <linux/types.h>
#include <sys/sysmacros.h>

typedef enum { B_FALSE=0, B_TRUE=1 }	boolean_t;
typedef unsigned long			uintptr_t;
typedef unsigned long			intptr_t;
typedef unsigned long			ulong_t;
typedef unsigned int			uint_t;
typedef unsigned char			uchar_t;
typedef unsigned long long		u_longlong_t;
typedef unsigned long long		u_offset_t;
typedef unsigned long long		rlim64_t;
typedef long long			longlong_t;
typedef long long			offset_t;
typedef struct task_struct		kthread_t;
typedef struct vmem { }			vmem_t;
typedef short				pri_t;
typedef struct timespec			timestruc_t; /* definition per SVr4 */
typedef longlong_t			hrtime_t;
typedef unsigned short			ushort_t;
typedef u_longlong_t			len_t;
typedef longlong_t			diskaddr_t;
typedef ushort_t			o_mode_t;
typedef uint_t				major_t;
typedef uint_t				minor_t;

#endif	/* _SPL_TYPES_H */
