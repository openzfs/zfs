/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2013 Jorgen Lundan <lundman@lundman.net>.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_ZFS_IOCTL_COMPAT_H
#define	_SYS_ZFS_IOCTL_COMPAT_H

#include <sys/cred.h>
#include <sys/dmu.h>
#include <sys/zio.h>
#include <sys/dsl_deleg.h>
#include <sys/zfs_ioctl.h>
#include <sys/ioccom.h>

#ifdef _KERNEL
#include <sys/nvpair.h>
#endif  /* _KERNEL */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Backwards ioctl compatibility
 */

/* ioctl versions for vfs.zfs.version.ioctl */
#define	ZFS_IOCVER_UNDEF	-1
#define	ZFS_IOCVER_NONE		0
#define	ZFS_IOCVER_1_9_4	1
#define	ZFS_IOCVER_ZOF		15

/* compatibility conversion flag */
#define	ZFS_CMD_COMPAT_NONE	0
#define	ZFS_CMD_COMPAT_V15	1
#define	ZFS_CMD_COMPAT_V28	2

#define	ZFS_IOC_COMPAT_PASS	254
#define	ZFS_IOC_COMPAT_FAIL	255

#define	ZFS_IOCREQ(ioreq)	((ioreq) & 0xff)

typedef struct zfs_iocparm {
	uint32_t	zfs_ioctl_version;
	uint64_t	zfs_cmd;
	uint64_t	zfs_cmd_size;

	/*
	 * ioctl() return codes can not be used to communicate -
	 * as XNU will skip copyout() if there is an error, so it
	 * is passed along in this wrapping structure.
	 */
	int			zfs_ioc_error;	/* ioctl error value */
} zfs_iocparm_t;

typedef struct zfs_cmd_1_9_4
{
	char		zc_name[MAXPATHLEN];	/* name of pool or dataset */
	uint64_t	zc_nvlist_src;		/* really (char *) */
	uint64_t	zc_nvlist_src_size;
	uint64_t	zc_nvlist_dst;		/* really (char *) */
	uint64_t	zc_nvlist_dst_size;
	boolean_t	zc_nvlist_dst_filled;	/* put an nvlist in dst? */
	int		zc_pad2;

	/*
	 * The following members are for legacy ioctls which haven't been
	 * converted to the new method.
	 */
	uint64_t	zc_history;		/* really (char *) */
	char		zc_value[MAXPATHLEN * 2];
	char		zc_string[MAXNAMELEN];
	uint64_t	zc_guid;
	uint64_t	zc_nvlist_conf;		/* really (char *) */
	uint64_t	zc_nvlist_conf_size;
	uint64_t	zc_cookie;
	uint64_t	zc_objset_type;
	uint64_t	zc_perm_action;
	uint64_t	zc_history_len;
	uint64_t	zc_history_offset;
	uint64_t	zc_obj;
	uint64_t	zc_iflags;		/* internal to zfs(7fs) */
	zfs_share_t	zc_share;
	dmu_objset_stats_t zc_objset_stats;
	struct drr_begin zc_begin_record;
	zinject_record_t zc_inject_record;
	uint32_t	zc_defer_destroy;
	uint32_t	zc_flags;
	uint64_t	zc_action_handle;
	int		zc_cleanup_fd;
	uint8_t		zc_simple;
	uint8_t		zc_pad3[3];
	boolean_t	zc_resumable;
	uint32_t	zc_pad4;
	uint64_t	zc_sendobj;
	uint64_t	zc_fromobj;
	uint64_t	zc_createtxg;
	zfs_stat_t	zc_stat;
    int		zc_ioc_error; /* ioctl error value */
	uint64_t	zc_dev;	/* OSX doesn't have ddi_driver_major */
} zfs_cmd_1_9_4_t;

// Figure this out
unsigned static long zfs_ioctl_1_9_4[] =
{
	// ZFS_IOC_POOL_CREATE = _IOWR('Z', 0, struct zfs_cmd),

	0,  /*  0 ZFS_IOC_POOL_CREATE */
	1,  /*  1 ZFS_IOC_POOL_DESTROY */
	2,  /*  2 ZFS_IOC_POOL_IMPORT */
	3,  /*  3 ZFS_IOC_POOL_EXPORT */
	4,  /*  4 ZFS_IOC_POOL_CONFIGS */
	5,  /*  5 ZFS_IOC_POOL_STATS */
	6,  /*  6 ZFS_IOC_POOL_TRYIMPORT */
	7,  /*  7 ZFS_IOC_POOL_SCRUB */
	8,  /*  8 ZFS_IOC_POOL_FREEZE */
	9,  /*  9 ZFS_IOC_POOL_UPGRADE */
	10, /* 10 ZFS_IOC_POOL_GET_HISTORY */
	11, /* 11 ZFS_IOC_VDEV_ADD */
	12, /* 12 ZFS_IOC_VDEV_REMOVE */
	13, /* 13 ZFS_IOC_VDEV_SET_STATE */
	14, /* 14 ZFS_IOC_VDEV_ATTACH */
	15, /* 15 ZFS_IOC_VDEV_DETACH */
	16, /* 16 ZFS_IOC_VDEV_SETPATH */
	18, /* 17 ZFS_IOC_OBJSET_STATS */
	19, /* 18 ZFS_IOC_OBJSET_ZPLPROPS */
	20, /* 19 ZFS_IOC_DATASET_LIST_NEXT */
	21, /* 20 ZFS_IOC_SNAPSHOT_LIST_NEXT */
	22, /* 21 ZFS_IOC_SET_PROP */
	ZFS_IOC_COMPAT_PASS, /* 22 ZFS_IOC_CREATE_MINOR */
	ZFS_IOC_COMPAT_PASS, /* 23 ZFS_IOC_REMOVE_MINOR */
	23, /* 24 ZFS_IOC_CREATE */
	24, /* 25 ZFS_IOC_DESTROY */
	25, /* 26 ZFS_IOC_ROLLBACK */
	26, /* 27 ZFS_IOC_RENAME */
	27, /* 28 ZFS_IOC_RECV */
	28, /* 29 ZFS_IOC_SEND */
	29, /* 30 ZFS_IOC_INJECT_FAULT */
	30, /* 31 ZFS_IOC_CLEAR_FAULT */
	31, /* 32 ZFS_IOC_INJECT_LIST_NEXT */
	32, /* 33 ZFS_IOC_ERROR_LOG */
	33, /* 34 ZFS_IOC_CLEAR */
	34, /* 35 ZFS_IOC_PROMOTE */
	35, /* 36 ZFS_IOC_DESTROY_SNAPS */
	36, /* 37 ZFS_IOC_SNAPSHOT */
	37, /* 38 ZFS_IOC_DSOBJ_TO_DSNAME */
	38, /* 39 ZFS_IOC_OBJ_TO_PATH */
	39, /* 40 ZFS_IOC_POOL_SET_PROPS */
	40, /* 41 ZFS_IOC_POOL_GET_PROPS */
	41, /* 42 ZFS_IOC_SET_FSACL */
	42, /* 43 ZFS_IOC_GET_FSACL */
	ZFS_IOC_COMPAT_PASS, /* 44 ZFS_IOC_ISCSI_PERM_CHECK */
	43, /* 45 ZFS_IOC_SHARE */
	44, /* 46 ZFS_IOC_IHNERIT_PROP */
	58, /* 47 ZFS_IOC_JAIL */
	59, /* 48 ZFS_IOC_UNJAIL */
	45, /* 49 ZFS_IOC_SMB_ACL */
	46, /* 50 ZFS_IOC_USERSPACE_ONE */
	47, /* 51 ZFS_IOC_USERSPACE_MANY */
	48, /* 52 ZFS_IOC_USERSPACE_UPGRADE */
	17, /* 53 ZFS_IOC_SETFRU */
};

#ifdef _KERNEL
int zfs_ioctl_compat_pre(zfs_cmd_t *, int *, const int);
void zfs_ioctl_compat_post(zfs_cmd_t *, const int, const int);
nvlist_t *zfs_ioctl_compat_innvl(zfs_cmd_t *, nvlist_t *, const int,
    const int);
nvlist_t *zfs_ioctl_compat_outnvl(zfs_cmd_t *, nvlist_t *, const int,
    const int);
#endif	/* _KERNEL */
void zfs_cmd_compat_get(zfs_cmd_t *, caddr_t, const int);
void zfs_cmd_compat_put(zfs_cmd_t *, caddr_t, const int, const int);

int	zcommon_init(void);
int	icp_init(void);
int	zstd_init(void);
void zcommon_fini(void);
void icp_fini(void);
void zstd_fini(void);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ZFS_IOCTL_COMPAT_H */
