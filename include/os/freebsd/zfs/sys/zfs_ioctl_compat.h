// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */
/*
 * Copyright 2014 Xin Li <delphij@FreeBSD.org>.  All rights reserved.
 * Copyright 2013 Martin Matuska <mm@FreeBSD.org>.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_ZFS_IOCTL_COMPAT_H
#define	_SYS_ZFS_IOCTL_COMPAT_H

#include <sys/cred.h>
#include <sys/dmu.h>
#include <sys/zio.h>
#include <sys/dsl_deleg.h>
#include <sys/zfs_ioctl.h>

#ifdef _KERNEL
#include <sys/nvpair.h>
#endif  /* _KERNEL */

/*
 * Legacy ioctl support allows compatibility with pre-OpenZFS tools on
 * FreeBSD.  The need for it will eventually pass (perhaps after FreeBSD
 * 12 is well out of support), at which point this code can be removed.
 * For now, downstream consumers may choose to disable this code by
 * removing the following define.
 */
#define	ZFS_LEGACY_SUPPORT

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Backwards ioctl compatibility
 */

/* ioctl versions for vfs.zfs.version.ioctl */
#define	ZFS_IOCVER_UNDEF	-1
#define	ZFS_IOCVER_NONE		0
#define	ZFS_IOCVER_DEADMAN	1
#define	ZFS_IOCVER_LZC		2
#define	ZFS_IOCVER_ZCMD		3
#define	ZFS_IOCVER_EDBP		4
#define	ZFS_IOCVER_RESUME	5
#define	ZFS_IOCVER_INLANES	6
#define	ZFS_IOCVER_PAD		7
#define	ZFS_IOCVER_LEGACY	ZFS_IOCVER_PAD
#define	ZFS_IOCVER_OZFS		15

/* compatibility conversion flag */
#define	ZFS_CMD_COMPAT_NONE	0
#define	ZFS_CMD_COMPAT_V15	1
#define	ZFS_CMD_COMPAT_V28	2
#define	ZFS_CMD_COMPAT_DEADMAN	3
#define	ZFS_CMD_COMPAT_LZC	4
#define	ZFS_CMD_COMPAT_ZCMD	5
#define	ZFS_CMD_COMPAT_EDBP	6
#define	ZFS_CMD_COMPAT_RESUME	7
#define	ZFS_CMD_COMPAT_INLANES	8
#define	ZFS_CMD_COMPAT_LEGACY	9

#define	ZFS_IOC_COMPAT_PASS	254
#define	ZFS_IOC_COMPAT_FAIL	255

#define	ZFS_IOCREQ(ioreq)	((ioreq) & 0xff)

typedef struct zfs_iocparm {
	uint32_t	zfs_ioctl_version;
	uint64_t	zfs_cmd;
	uint64_t	zfs_cmd_size;
} zfs_iocparm_t;


#ifdef ZFS_LEGACY_SUPPORT
#define	LEGACY_MAXPATHLEN 1024
#define	LEGACY_MAXNAMELEN 256

/*
 * Note: this struct must have the same layout in 32-bit and 64-bit, so
 * that 32-bit processes (like /sbin/zfs) can pass it to the 64-bit
 * kernel.  Therefore, we add padding to it so that no "hidden" padding
 * is automatically added on 64-bit (but not on 32-bit).
 */
typedef struct zfs_cmd_legacy {
	char		zc_name[LEGACY_MAXPATHLEN];	/* pool|dataset name */
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
	char		zc_value[LEGACY_MAXPATHLEN * 2];
	char		zc_string[LEGACY_MAXNAMELEN];
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
	uint64_t	zc_jailid;
	dmu_objset_stats_t zc_objset_stats;
	dmu_replay_record_t zc_begin_record;
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
} zfs_cmd_legacy_t;
#endif


#ifdef _KERNEL
int zfs_ioctl_compat_pre(zfs_cmd_t *, int *, const int);
void zfs_ioctl_compat_post(zfs_cmd_t *, const int, const int);
nvlist_t *zfs_ioctl_compat_innvl(zfs_cmd_t *, nvlist_t *, const int,
    const int);
nvlist_t *zfs_ioctl_compat_outnvl(zfs_cmd_t *, nvlist_t *, const int,
    const int);
#endif	/* _KERNEL */
#ifdef ZFS_LEGACY_SUPPORT
int zfs_ioctl_legacy_to_ozfs(int request);
int zfs_ioctl_ozfs_to_legacy(int request);
void zfs_cmd_legacy_to_ozfs(zfs_cmd_legacy_t *src, zfs_cmd_t *dst);
void zfs_cmd_ozfs_to_legacy(zfs_cmd_t *src, zfs_cmd_legacy_t *dst);
#endif

void zfs_cmd_compat_put(zfs_cmd_t *, caddr_t, const int, const int);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ZFS_IOCTL_COMPAT_H */
