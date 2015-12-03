#ifndef ZFS_THROTTLE_H_INCLUDED
#define ZFS_THROTTLE_H_INCLUDED

#include <linux/delay.h>
#include <linux/semaphore.h>

#include <sys/zfs_znode.h>

#define SEC_NANO 1000000

struct zfs_sb;					/* defined in vfsops.h */
struct semaphore;				/* defined in linux/semaphore.h */

typedef struct z_throttle {
	struct zfs_sb           *zsb;
	struct semaphore        z_sem_read;
	struct semaphore        z_sem_write;
	struct semaphore	*z_sem_real_read;
	struct semaphore	*z_sem_real_write;
	uint64_t                z_prop_read;
	uint64_t                z_prop_write;;
	uint64_t		z_real_read;
	uint64_t		z_real_write;
	char                    fsname[ZFS_MAXNAMELEN];
	struct list_head        list;
} z_throttle_t;

void z_do_throttle(struct semaphore *z_sem, uint64_t limit_rate);

#endif

