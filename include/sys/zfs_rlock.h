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
	zfs_rangelock_cb_t *rl_cb;
	void *rl_arg;
	list_t rl_free;
	uint8_t rl_processing;
#ifdef ZFS_DEBUG
	char *rl_name;
	list_node_t rl_node;
	list_t rl_ranges;
#endif
} zfs_rangelock_t;

typedef struct zfs_locked_range {
	zfs_rangelock_t *lr_rangelock; /* rangelock that this lock applies to */
	kthread_t *lr_owner; /* thread holding the locked range */
	void *lr_context; /* context referencing locked range */
	avl_node_t lr_node;	/* avl node link */
	uint64_t lr_offset;	/* file range offset */
	uint64_t lr_length;	/* file range length */
	uint64_t lr_orig_offset;	/* file range offset */
	uint64_t lr_orig_length;	/* file range length */
	uint_t lr_count;	/* range reference count in tree */
	zfs_rangelock_type_t lr_type; /* range type */
	zfs_rangelock_type_t lr_orig_type; /* range type */
	list_t lr_cb; /* list of waiters */
	uint8_t lr_proxy;	/* acting for original range */
	uint8_t lr_write_wanted; /* writer wants to lock this range */
	list_node_t lr_ranges_node;
} zfs_locked_range_t;

void zfs_rangelock_init_named(zfs_rangelock_t *, zfs_rangelock_cb_t *, void *,
    const char *);
void zfs_rangelock_init(zfs_rangelock_t *, zfs_rangelock_cb_t *, void *);
void zfs_rangelock_fini(zfs_rangelock_t *);
void zfs_rangelock_debug_init(void);
void zfs_rangelock_debug_fini(void);

zfs_locked_range_t *zfs_rangelock_enter(zfs_rangelock_t *,
    uint64_t, uint64_t, zfs_rangelock_type_t);
zfs_locked_range_t *zfs_rangelock_tryenter(zfs_rangelock_t *,
    uint64_t, uint64_t, zfs_rangelock_type_t);
int zfs_rangelock_tryenter_async(zfs_rangelock_t *rl, uint64_t off,
    uint64_t len, zfs_rangelock_type_t type, zfs_locked_range_t **lr,
    callback_fn cb, void *arg);
void zfs_rangelock_exit(zfs_locked_range_t *);
void zfs_rangelock_reduce(zfs_locked_range_t *, uint64_t, uint64_t);

extern list_t zfs_rangelocks_list;
extern kmutex_t zfs_rangelocks_lock;
#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FS_ZFS_RLOCK_H */
