#ifndef	_SYS_ZFS_NL_IOACCT_H
#define	_SYS_ZFS_NL_IOACCT_H

#include <linux/netlink.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/sysctl.h>
#include "sys/zfs_znode.h"
#include "libioacct.h"

int zfs_nl_ioacct_init(void);
void zfs_nl_ioacct_fini(void);
void zfs_nl_ioacct_send(zfs_io_info_t *zii);
void serialize_io_info(const zfs_io_info_t *zii, nl_msg *io_msg);

#endif // _SYS_ZFS_NL_IOACCT_H
