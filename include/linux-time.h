#ifndef _SYS_TIME_H
#define _SYS_TIME_H

#pragma ident   "%Z%%M% %I%     %E% SMI"        /* SVr4.0 1.16  */

/*
 * Structure returned by gettimeofday(2) system call,
 * and used in other calls.
 */

#ifdef  __cplusplus
extern "C" {
#endif

#include <linux/time.h>
#include <sys/linux-types.h>

extern unsigned long long monotonic_clock(void);
typedef struct timespec timestruc_t;    /* definition per SVr4 */
typedef longlong_t      hrtime_t;

#define TIME32_MAX      INT32_MAX
#define TIME32_MIN      INT32_MIN

#define SEC             1
#define MILLISEC        1000
#define MICROSEC        1000000
#define NANOSEC         1000000000

#define hz					\
({						\
        BUG_ON(HZ < 100 || HZ > MICROSEC);	\
        HZ;					\
})

#define gethrestime(ts) getnstimeofday((ts))

static __inline__ hrtime_t
gethrtime(void) {
        /* BUG_ON(cur_timer == timer_none); */

        /* Solaris expects a long long here but monotonic_clock() returns an
         * unsigned long long.  Note that monotonic_clock() returns the number
	 * of nanoseconds passed since kernel initialization.  Even for a signed
         * long long this will not "go negative" for ~292 years.
         */
        return monotonic_clock();
}

static __inline__ time_t
gethrestime_sec(void)
{
        timestruc_t now;

        gethrestime(&now);
        return (now.tv_sec);
}


#ifdef  __cplusplus
}
#endif

#endif  /* _SYS_TIME_H */
