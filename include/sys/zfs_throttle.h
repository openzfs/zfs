#ifndef ZFS_THROTTLE_H_INCLUDED
#define ZFS_THROTTLE_H_INCLUDED

#include <linux/delay.h>
#include <linux/semaphore.h>
#include <linux/list.h>

#define SEC_NANO 1000000

struct zfs_sb;					/* defined in vfsops.h */

typedef struct zfs_throttle {
	struct zfs_sb           *zsb;
	struct semaphore        z_sem_read;
	struct semaphore        z_sem_write;
	struct semaphore	*z_sem_readp;
	struct semaphore	*z_sem_writep;
	uint64_t                z_prop_read;
	uint64_t                z_prop_write;;
	uint64_t		z_real_read;
	uint64_t		z_real_write;
	char                    fsname[255]; //TODO:
	struct list_head        list;
} zfs_throttle_t;

void zfs_throttle_set_zt(zfs_sb *zsb, const char *fsname);
void zfs_throttle_unset_zt(zfs_sb *zsb);

void zfs_do_throttle(struct semaphore *z_sem, uint64_t rate);

#endif
