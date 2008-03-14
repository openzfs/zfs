#include <sys/sysmacros.h>
#include <sys/time.h>
#include "config.h"

void
__gethrestime(timestruc_t *ts)
{
	getnstimeofday((struct timespec *)ts);
}
EXPORT_SYMBOL(__gethrestime);

int
__clock_gettime(clock_type_t type, timespec_t *tp)
{
	/* Only support CLOCK_REALTIME+__CLOCK_REALTIME0 for now */
        BUG_ON(!((type == CLOCK_REALTIME) || (type == __CLOCK_REALTIME0)));

        getnstimeofday(tp);
        return 0;
}
EXPORT_SYMBOL(__clock_gettime);

/* This function may not be as fast as using monotonic_clock() but it
 * should be much more portable, if performance becomes as issue we can
 * look at using monotonic_clock() for x86_64 and x86 arches.
 */
hrtime_t
__gethrtime(void) {
        timespec_t tv;
        hrtime_t rc;

        do_posix_clock_monotonic_gettime(&tv);
        rc = (NSEC_PER_SEC * (hrtime_t)tv.tv_sec) + (hrtime_t)tv.tv_nsec;

        return rc;
}
EXPORT_SYMBOL(__gethrtime);
