#ifndef _SPL_TIME_H
#define _SPL_TIME_H

/*
 * Structure returned by gettimeofday(2) system call,
 * and used in other calls.
 */

#ifdef  __cplusplus
extern "C" {
#endif

#include <linux/module.h>
#include <linux/time.h>
#include <sys/types.h>

#define TIME32_MAX			INT32_MAX
#define TIME32_MIN			INT32_MIN

#define SEC				1
#define MILLISEC			1000
#define MICROSEC			1000000
#define NANOSEC				1000000000

/* Already defined in include/linux/time.h */
#undef CLOCK_THREAD_CPUTIME_ID
#undef CLOCK_REALTIME
#undef CLOCK_MONOTONIC
#undef CLOCK_PROCESS_CPUTIME_ID

typedef enum clock_type {
	__CLOCK_REALTIME0 =		0,	/* obsolete; same as CLOCK_REALTIME */
	CLOCK_VIRTUAL =			1,	/* thread's user-level CPU clock */
	CLOCK_THREAD_CPUTIME_ID	=	2,	/* thread's user+system CPU clock */
	CLOCK_REALTIME =		3,	/* wall clock */
	CLOCK_MONOTONIC =		4,	/* high resolution monotonic clock */
	CLOCK_PROCESS_CPUTIME_ID =	5,	/* process's user+system CPU clock */
	CLOCK_HIGHRES =			CLOCK_MONOTONIC,	/* alternate name */
	CLOCK_PROF =			CLOCK_THREAD_CPUTIME_ID,/* alternate name */
} clock_type_t;

#define hz					\
({						\
        BUG_ON(HZ < 100 || HZ > MICROSEC);	\
        HZ;					\
})

extern void __gethrestime(timestruc_t *);
extern int __clock_gettime(clock_type_t, timespec_t *);
extern hrtime_t __gethrtime(void);

#define gethrestime(ts)			__gethrestime(ts)
#define clock_gettime(fl, tp)		__clock_gettime(fl, tp)
#define gethrtime()			__gethrtime()

static __inline__ time_t
gethrestime_sec(void)
{
        timestruc_t now;

        __gethrestime(&now);
        return now.tv_sec;
}

#ifdef  __cplusplus
}
#endif

#endif  /* _SPL_TIME_H */
