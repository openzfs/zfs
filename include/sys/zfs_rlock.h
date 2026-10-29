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

/*
 * RL_READER_FAST is RL_READER for a caller that holds the lock for
 * one call and takes no writer on the same lock while it holds it.
 * The request takes a slot in rl_slots if the array exists, a slot
 * is free and no writer is present.  Otherwise it is served as
 * RL_READER, and lr_type says which one the caller got.
 *
 * A slot excludes writers on the range rounded out to 4 KiB.  Page
 * writeback locks whole pages (Linux zfs_putpage()) or whole blocks
 * starting on a page boundary (FreeBSD zfs_putpages()), and a page is
 * a multiple of 4 KiB.  So inside the file a slot conflicts with the
 * same writeback as an RL_READER on the same range.  A page fault in
 * the copy that waits for writeback of this file is no more exposed
 * under a slot than under RL_READER.
 */
typedef enum {
	RL_READER,
	RL_WRITER,
	RL_APPEND,
	RL_READER_FAST
} zfs_rangelock_type_t;

#define	RL_SLOTS		32
#define	RL_SLOT_WANTED		(1ULL << 62)
#define	RL_SLOT_LENBITS		23
#define	RL_SLOT_SHIFT		12

struct zfs_locked_range;

typedef void (zfs_rangelock_cb_t)(struct zfs_locked_range *, void *);

typedef struct zfs_rangelock {
	avl_tree_t rl_tree; /* contains locked_range_t */
	kmutex_t rl_lock;
	zfs_rangelock_cb_t *rl_cb;
	void *rl_arg;
	uint64_t *rl_slots;	/* RL_SLOTS fast readers, or NULL */
	struct zfs_locked_range *rl_drainer; /* waits for a slot */
	uint32_t rl_writers;	/* writers waiting or holding */
	uint32_t rl_closed;	/* slots closed to new readers */
} zfs_rangelock_t;

typedef struct zfs_locked_range {
	zfs_rangelock_t *lr_rangelock; /* rangelock that this lock applies to */
	avl_node_t lr_node;	/* avl node link */
	uint64_t lr_offset;	/* file range offset */
	uint64_t lr_length;	/* file range length */
	uint_t lr_count;	/* range reference count in tree */
	zfs_rangelock_type_t lr_type; /* range type */
	kcondvar_t lr_write_cv;	/* cv for waiting writers */
	kcondvar_t lr_read_cv;	/* cv for waiting readers */
	uint8_t lr_proxy;	/* acting for original range */
	uint8_t lr_write_wanted; /* writer wants to lock this range */
	uint8_t lr_read_wanted;	/* reader wants to lock this range */
	uint8_t lr_slot;	/* index in rl_slots */
} zfs_locked_range_t;

void zfs_rangelock_init(zfs_rangelock_t *, zfs_rangelock_cb_t *, void *);
void zfs_rangelock_fini(zfs_rangelock_t *);

zfs_locked_range_t *zfs_rangelock_enter(zfs_rangelock_t *,
    uint64_t, uint64_t, zfs_rangelock_type_t);
zfs_locked_range_t *zfs_rangelock_tryenter(zfs_rangelock_t *,
    uint64_t, uint64_t, zfs_rangelock_type_t);
void zfs_rangelock_exit(zfs_locked_range_t *);
void zfs_rangelock_reduce(zfs_locked_range_t *, uint64_t, uint64_t);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FS_ZFS_RLOCK_H */
