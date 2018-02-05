#include "sys/zfs_throttle.h"
#include "sys/zfs_ioctl.h"
#include "sys/zfs_vfsops.h"

// Current implementation does not take into account of the time of operation.
// Actual rate may be less.
void
zfs_do_throttle(zfs_throttle_t *zt, zfs_throttle_op_t op)
{
	struct semaphore *sem;
	int rate;

	switch (op) {
		case ZFS_THROTTLE_READ:
			sem = zt->z_sem_real_read;
			rate = atomic_read(&(zt->z_real_read));
			if (rate == ZFS_THROTTLE_NOLIMIT)
				break;
			down(sem);
			if (rate == ZFS_THROTTLE_NONE)
				break;
			usleep_range(
			    SEC_NANO/rate,
			    SEC_NANO/rate);
			up(sem);
			break;

		case ZFS_THROTTLE_WRITE:
			sem = zt->z_sem_real_write;
			rate = atomic_read(&(zt->z_real_write));
			if (rate == ZFS_THROTTLE_NOLIMIT)
				break;
			down(sem);
			if (rate == ZFS_THROTTLE_NONE)
				break;
			usleep_range(
			    SEC_NANO/rate,
			    SEC_NANO/rate);
			up(sem);
			break;

		default:
			break;
	}
}
