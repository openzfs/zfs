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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright (c) 2018 by Delphix. All rights reserved.
 */

#ifndef	_SYS_FS_ZFS_RLOCK_H
#define	_SYS_FS_ZFS_RLOCK_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/avl.h>
#ifdef __NetBSD__
#include <sys/list.h>
/*
 * NetBSD faults and zfs_get_done() do not share a thread, so a
 * per-thread lock stack cannot see a range entered on one thread
 * and exited on another.  The owner lives on the lock.  Other
 * platforms do not query it; their objects and enter/exit path
 * stay as upstream left them.
 */
#define	ZFS_RANGELOCK_TRACK_OWNERS
#endif

typedef enum {
	RL_READER,
	RL_WRITER,
	RL_APPEND
} zfs_rangelock_type_t;

struct zfs_locked_range;

typedef void (zfs_rangelock_cb_t)(struct zfs_locked_range *, void *);

typedef struct zfs_rangelock {
	avl_tree_t rl_tree; /* contains locked_range_t */
	kmutex_t rl_lock;
#ifdef	ZFS_RANGELOCK_TRACK_OWNERS
	/*
	 * Entered ranges, keyed by the thread that called enter.
	 * Exit may run on a different thread (zfs_get_done from a
	 * zio thread after zfs_get_data on the committer).
	 */
	list_t rl_owners;
#endif
	zfs_rangelock_cb_t *rl_cb;
	void *rl_arg;
} zfs_rangelock_t;

typedef struct zfs_locked_range {
	zfs_rangelock_t *lr_rangelock; /* rangelock that this lock applies to */
	avl_node_t lr_node;	/* avl node link */
#ifdef	ZFS_RANGELOCK_TRACK_OWNERS
	list_node_t lr_owner_node; /* rl_owners link */
	void *lr_owner;		/* thread that called enter */
#endif
	uint64_t lr_offset;	/* file range offset */
	uint64_t lr_length;	/* file range length */
	uint_t lr_count;	/* range reference count in tree */
	zfs_rangelock_type_t lr_type; /* range type */
	kcondvar_t lr_write_cv;	/* cv for waiting writers */
	kcondvar_t lr_read_cv;	/* cv for waiting readers */
	uint8_t lr_proxy;	/* acting for original range */
	uint8_t lr_write_wanted; /* writer wants to lock this range */
	uint8_t lr_read_wanted;	/* reader wants to lock this range */
} zfs_locked_range_t;

void zfs_rangelock_init(zfs_rangelock_t *, zfs_rangelock_cb_t *, void *);
void zfs_rangelock_fini(zfs_rangelock_t *);

zfs_locked_range_t *zfs_rangelock_enter(zfs_rangelock_t *,
    uint64_t, uint64_t, zfs_rangelock_type_t);
zfs_locked_range_t *zfs_rangelock_tryenter(zfs_rangelock_t *,
    uint64_t, uint64_t, zfs_rangelock_type_t);
void zfs_rangelock_exit(zfs_locked_range_t *);
void zfs_rangelock_reduce(zfs_locked_range_t *, uint64_t, uint64_t);

#ifdef	ZFS_RANGELOCK_TRACK_OWNERS
/*
 * True when the current thread holds an entered range on rl.
 * writer: only RL_WRITER counts.  off/len is the range that must
 * be inside the held lock.  owner_held is any lock on rl.
 */
boolean_t zfs_rangelock_owner_covers(zfs_rangelock_t *, uint64_t, uint64_t,
    boolean_t);
boolean_t zfs_rangelock_owner_held(zfs_rangelock_t *);
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FS_ZFS_RLOCK_H */
