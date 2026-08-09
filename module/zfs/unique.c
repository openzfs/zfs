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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */



#include <sys/zfs_context.h>
#include <sys/avl.h>
#include <sys/unique.h>

static avl_tree_t unique_avl;
static kmutex_t unique_mtx;

typedef struct unique {
	avl_node_t un_link;
	uint64_t un_value;
} unique_t;

#define	UNIQUE_MASK ((1ULL << UNIQUE_BITS) - 1)

static int
unique_compare(const void *a, const void *b)
{
	const unique_t *una = (const unique_t *)a;
	const unique_t *unb = (const unique_t *)b;

	return (TREE_CMP(una->un_value, unb->un_value));
}

void
unique_init(void)
{
	avl_create(&unique_avl, unique_compare,
	    sizeof (unique_t), offsetof(unique_t, un_link));
	mutex_init(&unique_mtx, NULL, MUTEX_DEFAULT, NULL);
}

void
unique_fini(void)
{
	avl_destroy(&unique_avl);
	mutex_destroy(&unique_mtx);
}

uint64_t
unique_create(void)
{
	uint64_t value = unique_insert(0);
	unique_remove(value);
	return (value);
}

uint64_t
unique_insert(uint64_t value)
{
	avl_index_t idx;
	unique_t *un = kmem_alloc(sizeof (unique_t), KM_SLEEP);

	un->un_value = value;

	mutex_enter(&unique_mtx);
	while (un->un_value == 0 || un->un_value & ~UNIQUE_MASK ||
	    avl_find(&unique_avl, un, &idx)) {
		mutex_exit(&unique_mtx);
		(void) random_get_pseudo_bytes((void*)&un->un_value,
		    sizeof (un->un_value));
		un->un_value &= UNIQUE_MASK;
		mutex_enter(&unique_mtx);
	}

	avl_insert(&unique_avl, un, idx);
	mutex_exit(&unique_mtx);

	return (un->un_value);
}

void
unique_remove(uint64_t value)
{
	unique_t un_tofind;
	unique_t *un;

	un_tofind.un_value = value;
	mutex_enter(&unique_mtx);
	un = avl_find(&unique_avl, &un_tofind, NULL);
	if (un != NULL) {
		avl_remove(&unique_avl, un);
		kmem_free(un, sizeof (unique_t));
	}
	mutex_exit(&unique_mtx);
}
