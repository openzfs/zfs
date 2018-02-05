#ifndef	_SYS_ZFS_THROTTLE_H
#define	_SYS_ZFS_THROTTLE_H

#if defined(_KERNEL)

#include <linux/delay.h>
#include <linux/semaphore.h>
#include <sys/systm.h>

#endif /* _KERNEL */

#define	SEC_NANO	1000000

struct zfs_throttle;				/* defined in zfs_ioctl.h */

typedef enum zfs_throttle_mode {
	ZFS_THROTTLE_NONE    = UINT64_MAX-2,	/* disable io */
	ZFS_THROTTLE_SHARED  = UINT64_MAX-1,	/* shared */
	ZFS_THROTTLE_NOLIMIT = UINT64_MAX	/* nolimit */
} zfs_throttle_mode_t;

typedef enum zfs_throttle_op {
	ZFS_THROTTLE_READ,
	ZFS_THROTTLE_WRITE
} zfs_throttle_op_t;

void zfs_do_throttle(struct zfs_throttle *zt, zfs_throttle_op_t op);

#endif /* _SYS_ZFS_THROTTLE_H */
