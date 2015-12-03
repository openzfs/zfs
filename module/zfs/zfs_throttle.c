#include "sys/zfs_throttle.h"

void
z_do_throttle(struct semaphore *z_sem, uint64_t limit_rate)
{
	if (limit_rate==0)
		return;
	down(z_sem);

	// Current implementation does not take into account
	// the time of operation; so, actual rate may be less
	usleep_range(SEC_NANO/limit_rate, SEC_NANO/limit_rate);
	up(z_sem);
}
