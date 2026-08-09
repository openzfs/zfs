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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 */

#include <sys/bplist.h>
#include <sys/zfs_context.h>


void
bplist_create(bplist_t *bpl)
{
	mutex_init(&bpl->bpl_lock, NULL, MUTEX_DEFAULT, NULL);
	list_create(&bpl->bpl_list, sizeof (bplist_entry_t),
	    offsetof(bplist_entry_t, bpe_node));
}

void
bplist_destroy(bplist_t *bpl)
{
	list_destroy(&bpl->bpl_list);
	mutex_destroy(&bpl->bpl_lock);
}

void
bplist_append(bplist_t *bpl, const blkptr_t *bp)
{
	bplist_entry_t *bpe = kmem_alloc(sizeof (*bpe), KM_SLEEP);

	mutex_enter(&bpl->bpl_lock);
	bpe->bpe_blk = *bp;
	list_insert_tail(&bpl->bpl_list, bpe);
	mutex_exit(&bpl->bpl_lock);
}

/*
 * To aid debugging, we keep the most recently removed entry.  This way if
 * we are in the callback, we can easily locate the entry.
 */
static bplist_entry_t *bplist_iterate_last_removed;

void
bplist_iterate(bplist_t *bpl, bplist_itor_t *func, void *arg, dmu_tx_t *tx)
{
	bplist_entry_t *bpe;

	mutex_enter(&bpl->bpl_lock);
	while ((bpe = list_remove_head(&bpl->bpl_list))) {
		bplist_iterate_last_removed = bpe;
		mutex_exit(&bpl->bpl_lock);
		func(arg, &bpe->bpe_blk, tx);
		kmem_free(bpe, sizeof (*bpe));
		mutex_enter(&bpl->bpl_lock);
	}
	mutex_exit(&bpl->bpl_lock);
}

void
bplist_clear(bplist_t *bpl)
{
	bplist_entry_t *bpe;

	mutex_enter(&bpl->bpl_lock);
	while ((bpe = list_remove_head(&bpl->bpl_list)))
		kmem_free(bpe, sizeof (*bpe));
	mutex_exit(&bpl->bpl_lock);
}
