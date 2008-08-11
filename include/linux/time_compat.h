#ifndef _SPL_TIME_COMPAT_H
#define _SPL_TIME_COMPAT_H

#include <linux/time.h>

/* timespec_sub() API changes
 * 2.6.18  - 2.6.x: Inline function provided by linux/time.h
 */
#ifndef HAVE_TIMESPEC_SUB
static inline struct timespec
timespec_sub(struct timespec lhs, struct timespec rhs)
{
        struct timespec ts_delta;
        set_normalized_timespec(&ts_delta, lhs.tv_sec - rhs.tv_sec,
                                lhs.tv_nsec - rhs.tv_nsec);
        return ts_delta;
}
#endif /* HAVE_TIMESPEC_SUB */

#endif /* _SPL_TIME_COMPAT_H */

