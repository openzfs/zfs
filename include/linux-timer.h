#ifndef _SOLARIS_TIMER_H
#define _SOLARIS_TIMER_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/timer.h>

#define lbolt				((clock_t)jiffies)
#define lbolt64				((int64_t)get_jiffies_64())

#define delay(ticks)			schedule_timeout((long timeout)(ticks))

#ifdef  __cplusplus
}
#endif

#endif  /* _SOLARIS_TIMER_H */

