#ifndef	_LIBIOACCT_H
#define	_LIBIOACCT_H

#ifdef _KERNEL
#include <net/sock.h>
#include <linux/netlink.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/sysctl.h>
#include "sys/zfs_znode.h"
#endif

#ifndef _KERNEL
#include "libzfs.h"
#include <string.h>
#endif

#define	ZFS_NL_IO_PROTO	NETLINK_USERSOCK
#define	ZFS_NL_IO_GRP	21

typedef char nl_msg;

typedef enum {
	ZFS_NL_READ,
	ZFS_NL_WRITE,
	ZFS_NL_READPAGE,
	ZFS_NL_WRITEPAGE
} zfs_io_type_t;

typedef struct zfs_io_info {
	pid_t pid;
	ssize_t nbytes;
	zfs_io_type_t op;
	char fsname[ZFS_MAXNAMELEN];
} zfs_io_info_t;

#define	NETLINK_MSGLEN sizeof (pid_t) + sizeof (ssize_t) \
	+ sizeof (zfs_io_type_t) + ZFS_MAXNAMELEN

void deserialize_io_info(zfs_io_info_t *zii, nl_msg *io_msg);

#endif // _LIBIOACCT_H
