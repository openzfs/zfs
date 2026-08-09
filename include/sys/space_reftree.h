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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright (c) 2013 by Delphix. All rights reserved.
 */

#ifndef _SYS_SPACE_REFTREE_H
#define	_SYS_SPACE_REFTREE_H

#include <sys/range_tree.h>
#include <sys/avl.h>
#ifdef	__cplusplus
extern "C" {
#endif

typedef struct space_ref {
	avl_node_t	sr_node;	/* AVL node */
	uint64_t	sr_offset;	/* range offset (start or end) */
	int64_t		sr_refcnt;	/* associated reference count */
} space_ref_t;

void space_reftree_create(avl_tree_t *t);
void space_reftree_destroy(avl_tree_t *t);
void space_reftree_add_seg(avl_tree_t *t, uint64_t start, uint64_t end,
    int64_t refcnt);
void space_reftree_add_map(avl_tree_t *t, zfs_range_tree_t *rt, int64_t refcnt);
void space_reftree_generate_map(avl_tree_t *t, zfs_range_tree_t *rt,
    int64_t minref);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SPACE_REFTREE_H */
