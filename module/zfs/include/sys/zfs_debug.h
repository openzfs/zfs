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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SYS_ZFS_DEBUG_H
#define	_SYS_ZFS_DEBUG_H

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef TRUE
#define	TRUE 1
#endif

#ifndef FALSE
#define	FALSE 0
#endif

/*
 * ZFS debugging
 */

#if defined(DEBUG) || !defined(_KERNEL)
#define	ZFS_DEBUG
#endif

extern int zfs_flags;

#define	ZFS_DEBUG_DPRINTF	0x0001
#define	ZFS_DEBUG_DBUF_VERIFY	0x0002
#define	ZFS_DEBUG_DNODE_VERIFY	0x0004
#define	ZFS_DEBUG_SNAPNAMES	0x0008
#define	ZFS_DEBUG_MODIFY	0x0010

#ifdef ZFS_DEBUG
extern void __dprintf(const char *file, const char *func,
    int line, const char *fmt, ...);
#define	dprintf(...) \
	if (zfs_flags & ZFS_DEBUG_DPRINTF) \
		__dprintf(__FILE__, __func__, __LINE__, __VA_ARGS__)
#else
#define	dprintf(...) ((void)0)
#endif /* ZFS_DEBUG */

extern void zfs_panic_recover(const char *fmt, ...);

typedef struct zfs_dbgmsg {
	list_node_t zdm_node;
	time_t zdm_timestamp;
	char zdm_msg[1]; /* variable length allocation */
} zfs_dbgmsg_t;

extern void zfs_dbgmsg_init(void);
extern void zfs_dbgmsg_fini(void);
extern void zfs_dbgmsg(const char *fmt, ...);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ZFS_DEBUG_H */
