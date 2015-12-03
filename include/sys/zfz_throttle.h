#ifndef Z_THROTTLE_H_INCLUDED
#define Z_THROTTLE_H_INCLUDED

#include <linux/delay.h>
#include <linux/semaphore.h>

#define SEC_NANO 1000000

void z_do_throttle(struct semaphore *z_sem, uint64_t limit_rate);

#endif
