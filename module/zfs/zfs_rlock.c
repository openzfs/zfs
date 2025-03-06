// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 */

/*
 * This file contains the code to implement file range locking in
 * ZFS, although there isn't much specific to ZFS (all that comes to mind is
 * support for growing the blocksize).
 *
 * Interface
 * ---------
 * Defined in zfs_rlock.h but essentially:
 *	lr = rangelock_enter(zp, off, len, lock_type);
 *	rangelock_reduce(lr, off, len); // optional
 *	rangelock_exit(lr);
 *
 * Range locking rules
 * --------------------
 * 1. When truncating a file (zfs_create, zfs_setattr, zfs_space) the whole
 *    file range needs to be locked as RL_WRITER. Only then can the pages be
 *    freed etc and zp_size reset. zp_size must be set within range lock.
 * 2. For writes and punching holes (zfs_write & zfs_space) just the range
 *    being written or freed needs to be locked as RL_WRITER.
 *    Multiple writes at the end of the file must coordinate zp_size updates
 *    to ensure data isn't lost. A compare and swap loop is currently used
 *    to ensure the file size is at least the offset last written.
 * 3. For reads (zfs_read, zfs_get_data & zfs_putapage) just the range being
 *    read needs to be locked as RL_READER. A check against zp_size can then
 *    be made for reading beyond end of file.
 *
 * AVL tree
 * --------
 * An AVL tree is used to maintain the state of the existing ranges
 * that are locked for exclusive (writer) or shared (reader) use.
 * The starting range offset is used for searching and sorting the tree.
 *
 * Common case
 * -----------
 * The (hopefully) usual case is of no overlaps or contention for locks. On
 * entry to rangelock_enter(), a locked_range_t is allocated; the tree
 * searched that finds no overlap, and *this* locked_range_t is placed in the
 * tree.
 *
 * Overlaps/Reference counting/Proxy locks
 * ---------------------------------------
 * The avl code only allows one node at a particular offset. Also it's very
 * inefficient to search through all previous entries looking for overlaps
 * (because the very 1st in the ordered list might be at offset 0 but
 * cover the whole file).
 * So this implementation uses reference counts and proxy range locks.
 * Firstly, only reader locks use reference counts and proxy locks,
 * because writer locks are exclusive.
 * When a reader lock overlaps with another then a proxy lock is created
 * for that range and replaces the original lock. If the overlap
 * is exact then the reference count of the proxy is simply incremented.
 * Otherwise, the proxy lock is split into smaller lock ranges and
 * new proxy locks created for non overlapping ranges.
 * The reference counts are adjusted accordingly.
 * Meanwhile, the original lock is kept around (this is the callers handle)
 * and its offset and length are used when releasing the lock.
 *
 * Thread coordination
 * -------------------
 * In order to make wakeups efficient and to ensure multiple continuous
 * readers on a range don't starve a writer for the same range lock,
 * two condition variables are allocated in each rl_t.
 * If a writer (or reader) can't get a range it initialises the writer
 * (or reader) cv; sets a flag saying there's a writer (or reader) waiting;
 * and waits on that cv. When a thread unlocks that range it wakes up all
 * writers then all readers before destroying the lock.
 *
 * Append mode writes
 * ------------------
 * Append mode writes need to lock a range at the end of a file.
 * The offset of the end of the file is determined under the
 * range locking mutex, and the lock type converted from RL_APPEND to
 * RL_WRITER and the range locked.
 *
 * Grow block handling
 * -------------------
 * ZFS supports multiple block sizes, up to 16MB. The smallest
 * block size is used for the file which is grown as needed. During this
 * growth all other writers and readers must be excluded.
 * So if the block size needs to be grown then the whole file is
 * exclusively locked, then later the caller will reduce the lock
 * range to just the range to be written using rangelock_reduce().
 */

#include <sys/zfs_context.h>
#include <sys/zfs_rlock.h>


/*
 * AVL comparison function used to order range locks
 * Locks are ordered on the start offset of the range.
 */
static int
zfs_rangelock_compare(const void *arg1, const void *arg2)
{
	const zfs_locked_range_t *rl1 = (const zfs_locked_range_t *)arg1;
	const zfs_locked_range_t *rl2 = (const zfs_locked_range_t *)arg2;

	return (TREE_CMP(rl1->lr_offset, rl2->lr_offset));
}

/*
 * The callback is invoked when acquiring a RL_WRITER or RL_APPEND lock.
 * It must convert RL_APPEND to RL_WRITER (starting at the end of the file),
 * and may increase the range that's locked for RL_WRITER.
 */
void
zfs_rangelock_init(zfs_rangelock_t *rl, zfs_rangelock_cb_t *cb, void *arg)
{
	mutex_init(&rl->rl_lock, NULL, MUTEX_DEFAULT, NULL);
	avl_create(&rl->rl_tree, zfs_rangelock_compare,
	    sizeof (zfs_locked_range_t), offsetof(zfs_locked_range_t, lr_node));
	rl->rl_cb = cb;
	rl->rl_arg = arg;
}

void
zfs_rangelock_fini(zfs_rangelock_t *rl)
{
	mutex_destroy(&rl->rl_lock);
	avl_destroy(&rl->rl_tree);
}

/*
 * Check if a write lock can be grabbed.  If not, fail immediately or sleep and
 * recheck until available, depending on the value of the "nonblock" parameter.
 */
static boolean_t
zfs_rangelock_enter_writer(zfs_rangelock_t *rl, zfs_locked_range_t *new,
    boolean_t nonblock)
{
	avl_tree_t *tree = &rl->rl_tree;
	zfs_locked_range_t *lr;
	avl_index_t where;
	uint64_t orig_off = new->lr_offset;
	uint64_t orig_len = new->lr_length;
	zfs_rangelock_type_t orig_type = new->lr_type;

	for (;;) {
		/*
		 * Call callback which can modify new->r_off,len,type.
		 * Note, the callback is used by the ZPL to handle appending
		 * and changing blocksizes.  It isn't needed for zvols.
		 */
		if (rl->rl_cb != NULL) {
			rl->rl_cb(new, rl->rl_arg);
		}

		/*
		 * If the type was APPEND, the callback must convert it to
		 * WRITER.
		 */
		ASSERT3U(new->lr_type, ==, RL_WRITER);

		/*
		 * First check for the usual case of no locks
		 */
		if (avl_numnodes(tree) == 0) {
			avl_add(tree, new);
			return (B_TRUE);
		}

		/*
		 * Look for any locks in the range.
		 */
		lr = avl_find(tree, new, &where);
		if (lr != NULL)
			goto wait; /* already locked at same offset */

		lr = avl_nearest(tree, where, AVL_AFTER);
		if (lr != NULL &&
		    lr->lr_offset < new->lr_offset + new->lr_length)
			goto wait;

		lr = avl_nearest(tree, where, AVL_BEFORE);
		if (lr != NULL &&
		    lr->lr_offset + lr->lr_length > new->lr_offset)
			goto wait;

		avl_insert(tree, new, where);
		return (B_TRUE);
wait:
		if (nonblock)
			return (B_FALSE);
		if (!lr->lr_write_wanted) {
			cv_init(&lr->lr_write_cv, NULL, CV_DEFAULT, NULL);
			lr->lr_write_wanted = B_TRUE;
		}
		cv_wait(&lr->lr_write_cv, &rl->rl_lock);

		/* reset to original */
		new->lr_offset = orig_off;
		new->lr_length = orig_len;
		new->lr_type = orig_type;
	}
}

/*
 * If this is an original (non-proxy) lock then replace it by
 * a proxy and return the proxy.
 */
static zfs_locked_range_t *
zfs_rangelock_proxify(avl_tree_t *tree, zfs_locked_range_t *lr)
{
	zfs_locked_range_t *proxy;

	if (lr->lr_proxy)
		return (lr); /* already a proxy */

	ASSERT3U(lr->lr_count, ==, 1);
	ASSERT(lr->lr_write_wanted == B_FALSE);
	ASSERT(lr->lr_read_wanted == B_FALSE);
	avl_remove(tree, lr);
	lr->lr_count = 0;

	/* create a proxy range lock */
	proxy = kmem_alloc(sizeof (zfs_locked_range_t), KM_SLEEP);
	proxy->lr_offset = lr->lr_offset;
	proxy->lr_length = lr->lr_length;
	proxy->lr_count = 1;
	proxy->lr_type = RL_READER;
	proxy->lr_proxy = B_TRUE;
	proxy->lr_write_wanted = B_FALSE;
	proxy->lr_read_wanted = B_FALSE;
	avl_add(tree, proxy);

	return (proxy);
}

/*
 * Split the range lock at the supplied offset
 * returning the *front* proxy.
 */
static zfs_locked_range_t *
zfs_rangelock_split(avl_tree_t *tree, zfs_locked_range_t *lr, uint64_t off)
{
	zfs_locked_range_t *rear;

	ASSERT3U(lr->lr_length, >, 1);
	ASSERT3U(off, >, lr->lr_offset);
	ASSERT3U(off, <, lr->lr_offset + lr->lr_length);
	ASSERT(lr->lr_write_wanted == B_FALSE);
	ASSERT(lr->lr_read_wanted == B_FALSE);

	/* create the rear proxy range lock */
	rear = kmem_alloc(sizeof (zfs_locked_range_t), KM_SLEEP);
	rear->lr_offset = off;
	rear->lr_length = lr->lr_offset + lr->lr_length - off;
	rear->lr_count = lr->lr_count;
	rear->lr_type = RL_READER;
	rear->lr_proxy = B_TRUE;
	rear->lr_write_wanted = B_FALSE;
	rear->lr_read_wanted = B_FALSE;

	zfs_locked_range_t *front = zfs_rangelock_proxify(tree, lr);
	front->lr_length = off - lr->lr_offset;

	avl_insert_here(tree, rear, front, AVL_AFTER);
	return (front);
}

/*
 * Create and add a new proxy range lock for the supplied range.
 */
static void
zfs_rangelock_new_proxy(avl_tree_t *tree, uint64_t off, uint64_t len)
{
	zfs_locked_range_t *lr;

	ASSERT(len != 0);
	lr = kmem_alloc(sizeof (zfs_locked_range_t), KM_SLEEP);
	lr->lr_offset = off;
	lr->lr_length = len;
	lr->lr_count = 1;
	lr->lr_type = RL_READER;
	lr->lr_proxy = B_TRUE;
	lr->lr_write_wanted = B_FALSE;
	lr->lr_read_wanted = B_FALSE;
	avl_add(tree, lr);
}

static void
zfs_rangelock_add_reader(avl_tree_t *tree, zfs_locked_range_t *new,
    zfs_locked_range_t *prev, avl_index_t where)
{
	zfs_locked_range_t *next;
	uint64_t off = new->lr_offset;
	uint64_t len = new->lr_length;

	/*
	 * prev arrives either:
	 * - pointing to an entry at the same offset
	 * - pointing to the entry with the closest previous offset whose
	 *   range may overlap with the new range
	 * - null, if there were no ranges starting before the new one
	 */
	if (prev != NULL) {
		if (prev->lr_offset + prev->lr_length <= off) {
			prev = NULL;
		} else if (prev->lr_offset != off) {
			/*
			 * convert to proxy if needed then
			 * split this entry and bump ref count
			 */
			prev = zfs_rangelock_split(tree, prev, off);
			prev = AVL_NEXT(tree, prev); /* move to rear range */
		}
	}
	ASSERT((prev == NULL) || (prev->lr_offset == off));

	if (prev != NULL)
		next = prev;
	else
		next = avl_nearest(tree, where, AVL_AFTER);

	if (next == NULL || off + len <= next->lr_offset) {
		/* no overlaps, use the original new rl_t in the tree */
		avl_insert(tree, new, where);
		return;
	}

	if (off < next->lr_offset) {
		/* Add a proxy for initial range before the overlap */
		zfs_rangelock_new_proxy(tree, off, next->lr_offset - off);
	}

	new->lr_count = 0; /* will use proxies in tree */
	/*
	 * We now search forward through the ranges, until we go past the end
	 * of the new range. For each entry we make it a proxy if it
	 * isn't already, then bump its reference count. If there's any
	 * gaps between the ranges then we create a new proxy range.
	 */
	for (prev = NULL; next; prev = next, next = AVL_NEXT(tree, next)) {
		if (off + len <= next->lr_offset)
			break;
		if (prev != NULL && prev->lr_offset + prev->lr_length <
		    next->lr_offset) {
			/* there's a gap */
			ASSERT3U(next->lr_offset, >,
			    prev->lr_offset + prev->lr_length);
			zfs_rangelock_new_proxy(tree,
			    prev->lr_offset + prev->lr_length,
			    next->lr_offset -
			    (prev->lr_offset + prev->lr_length));
		}
		if (off + len == next->lr_offset + next->lr_length) {
			/* exact overlap with end */
			next = zfs_rangelock_proxify(tree, next);
			next->lr_count++;
			return;
		}
		if (off + len < next->lr_offset + next->lr_length) {
			/* new range ends in the middle of this block */
			next = zfs_rangelock_split(tree, next, off + len);
			next->lr_count++;
			return;
		}
		ASSERT3U(off + len, >, next->lr_offset + next->lr_length);
		next = zfs_rangelock_proxify(tree, next);
		next->lr_count++;
	}

	/* Add the remaining end range. */
	zfs_rangelock_new_proxy(tree, prev->lr_offset + prev->lr_length,
	    (off + len) - (prev->lr_offset + prev->lr_length));
}

/*
 * Check if a reader lock can be grabbed.  If not, fail immediately or sleep and
 * recheck until available, depending on the value of the "nonblock" parameter.
 */
static boolean_t
zfs_rangelock_enter_reader(zfs_rangelock_t *rl, zfs_locked_range_t *new,
    boolean_t nonblock)
{
	avl_tree_t *tree = &rl->rl_tree;
	zfs_locked_range_t *prev, *next;
	avl_index_t where;
	uint64_t off = new->lr_offset;
	uint64_t len = new->lr_length;

	/*
	 * Look for any writer locks in the range.
	 */
retry:
	prev = avl_find(tree, new, &where);
	if (prev == NULL)
		prev = avl_nearest(tree, where, AVL_BEFORE);

	/*
	 * Check the previous range for a writer lock overlap.
	 */
	if (prev && (off < prev->lr_offset + prev->lr_length)) {
		if ((prev->lr_type == RL_WRITER) || (prev->lr_write_wanted)) {
			if (nonblock)
				return (B_FALSE);
			if (!prev->lr_read_wanted) {
				cv_init(&prev->lr_read_cv,
				    NULL, CV_DEFAULT, NULL);
				prev->lr_read_wanted = B_TRUE;
			}
			cv_wait(&prev->lr_read_cv, &rl->rl_lock);
			goto retry;
		}
		if (off + len < prev->lr_offset + prev->lr_length)
			goto got_lock;
	}

	/*
	 * Search through the following ranges to see if there's
	 * write lock any overlap.
	 */
	if (prev != NULL)
		next = AVL_NEXT(tree, prev);
	else
		next = avl_nearest(tree, where, AVL_AFTER);
	for (; next != NULL; next = AVL_NEXT(tree, next)) {
		if (off + len <= next->lr_offset)
			goto got_lock;
		if ((next->lr_type == RL_WRITER) || (next->lr_write_wanted)) {
			if (nonblock)
				return (B_FALSE);
			if (!next->lr_read_wanted) {
				cv_init(&next->lr_read_cv,
				    NULL, CV_DEFAULT, NULL);
				next->lr_read_wanted = B_TRUE;
			}
			cv_wait(&next->lr_read_cv, &rl->rl_lock);
			goto retry;
		}
		if (off + len <= next->lr_offset + next->lr_length)
			goto got_lock;
	}

got_lock:
	/*
	 * Add the read lock, which may involve splitting existing
	 * locks and bumping ref counts (r_count).
	 */
	zfs_rangelock_add_reader(tree, new, prev, where);
	return (B_TRUE);
}

/*
 * Lock a range (offset, length) as either shared (RL_READER) or exclusive
 * (RL_WRITER or RL_APPEND).  If RL_APPEND is specified, rl_cb() will convert
 * it to a RL_WRITER lock (with the offset at the end of the file).  Returns
 * the range lock structure for later unlocking (or reduce range if the
 * entire file is locked as RL_WRITER), or NULL if nonblock is true and the
 * lock could not be acquired immediately.
 */
static zfs_locked_range_t *
zfs_rangelock_enter_impl(zfs_rangelock_t *rl, uint64_t off, uint64_t len,
    zfs_rangelock_type_t type, boolean_t nonblock)
{
	zfs_locked_range_t *new;

	ASSERT(type == RL_READER || type == RL_WRITER || type == RL_APPEND);

	new = kmem_alloc(sizeof (zfs_locked_range_t), KM_SLEEP);
	new->lr_rangelock = rl;
	new->lr_offset = off;
	if (len + off < off)	/* overflow */
		len = UINT64_MAX - off;
	new->lr_length = len;
	new->lr_count = 1; /* assume it's going to be in the tree */
	new->lr_type = type;
	new->lr_proxy = B_FALSE;
	new->lr_write_wanted = B_FALSE;
	new->lr_read_wanted = B_FALSE;

	mutex_enter(&rl->rl_lock);
	if (type == RL_READER) {
		/*
		 * First check for the usual case of no locks
		 */
		if (avl_numnodes(&rl->rl_tree) == 0) {
			avl_add(&rl->rl_tree, new);
		} else if (!zfs_rangelock_enter_reader(rl, new, nonblock)) {
			kmem_free(new, sizeof (*new));
			new = NULL;
		}
	} else if (!zfs_rangelock_enter_writer(rl, new, nonblock)) {
		kmem_free(new, sizeof (*new));
		new = NULL;
	}
	mutex_exit(&rl->rl_lock);
	return (new);
}

zfs_locked_range_t *
zfs_rangelock_enter(zfs_rangelock_t *rl, uint64_t off, uint64_t len,
    zfs_rangelock_type_t type)
{
	return (zfs_rangelock_enter_impl(rl, off, len, type, B_FALSE));
}

zfs_locked_range_t *
zfs_rangelock_tryenter(zfs_rangelock_t *rl, uint64_t off, uint64_t len,
    zfs_rangelock_type_t type)
{
	return (zfs_rangelock_enter_impl(rl, off, len, type, B_TRUE));
}

/*
 * Safely free the zfs_locked_range_t.
 */
static void
zfs_rangelock_free(zfs_locked_range_t *lr)
{
	if (lr->lr_write_wanted)
		cv_destroy(&lr->lr_write_cv);

	if (lr->lr_read_wanted)
		cv_destroy(&lr->lr_read_cv);

	kmem_free(lr, sizeof (zfs_locked_range_t));
}

/*
 * Unlock a reader lock
 */
static void
zfs_rangelock_exit_reader(zfs_rangelock_t *rl, zfs_locked_range_t *remove,
    list_t *free_list)
{
	avl_tree_t *tree = &rl->rl_tree;
	uint64_t len;

	/*
	 * The common case is when the remove entry is in the tree
	 * (cnt == 1) meaning there's been no other reader locks overlapping
	 * with this one. Otherwise the remove entry will have been
	 * removed from the tree and replaced by proxies (one or
	 * more ranges mapping to the entire range).
	 */
	if (remove->lr_count == 1) {
		avl_remove(tree, remove);
		if (remove->lr_write_wanted)
			cv_broadcast(&remove->lr_write_cv);
		if (remove->lr_read_wanted)
			cv_broadcast(&remove->lr_read_cv);
		list_insert_tail(free_list, remove);
	} else {
		ASSERT0(remove->lr_count);
		ASSERT0(remove->lr_write_wanted);
		ASSERT0(remove->lr_read_wanted);
		/*
		 * Find start proxy representing this reader lock,
		 * then decrement ref count on all proxies
		 * that make up this range, freeing them as needed.
		 */
		zfs_locked_range_t *lr = avl_find(tree, remove, NULL);
		ASSERT3P(lr, !=, NULL);
		ASSERT3U(lr->lr_count, !=, 0);
		ASSERT3U(lr->lr_type, ==, RL_READER);
		zfs_locked_range_t *next = NULL;
		for (len = remove->lr_length; len != 0; lr = next) {
			len -= lr->lr_length;
			if (len != 0) {
				next = AVL_NEXT(tree, lr);
				ASSERT3P(next, !=, NULL);
				ASSERT3U(lr->lr_offset + lr->lr_length, ==,
				    next->lr_offset);
				ASSERT3U(next->lr_count, !=, 0);
				ASSERT3U(next->lr_type, ==, RL_READER);
			}
			lr->lr_count--;
			if (lr->lr_count == 0) {
				avl_remove(tree, lr);
				if (lr->lr_write_wanted)
					cv_broadcast(&lr->lr_write_cv);
				if (lr->lr_read_wanted)
					cv_broadcast(&lr->lr_read_cv);
				list_insert_tail(free_list, lr);
			}
		}
		kmem_free(remove, sizeof (zfs_locked_range_t));
	}
}

/*
 * Unlock range and destroy range lock structure.
 */
void
zfs_rangelock_exit(zfs_locked_range_t *lr)
{
	zfs_rangelock_t *rl = lr->lr_rangelock;
	list_t free_list;
	zfs_locked_range_t *free_lr;

	ASSERT(lr->lr_type == RL_WRITER || lr->lr_type == RL_READER);
	ASSERT(lr->lr_count == 1 || lr->lr_count == 0);
	ASSERT(!lr->lr_proxy);

	/*
	 * The free list is used to defer the cv_destroy() and
	 * subsequent kmem_free until after the mutex is dropped.
	 */
	list_create(&free_list, sizeof (zfs_locked_range_t),
	    offsetof(zfs_locked_range_t, lr_node));

	mutex_enter(&rl->rl_lock);
	if (lr->lr_type == RL_WRITER) {
		/* writer locks can't be shared or split */
		avl_remove(&rl->rl_tree, lr);
		if (lr->lr_write_wanted)
			cv_broadcast(&lr->lr_write_cv);
		if (lr->lr_read_wanted)
			cv_broadcast(&lr->lr_read_cv);
		list_insert_tail(&free_list, lr);
	} else {
		/*
		 * lock may be shared, let rangelock_exit_reader()
		 * release the lock and free the zfs_locked_range_t.
		 */
		zfs_rangelock_exit_reader(rl, lr, &free_list);
	}
	mutex_exit(&rl->rl_lock);

	while ((free_lr = list_remove_head(&free_list)) != NULL)
		zfs_rangelock_free(free_lr);

	list_destroy(&free_list);
}

/*
 * Reduce range locked as RL_WRITER from whole file to specified range.
 * Asserts the whole file is exclusively locked and so there's only one
 * entry in the tree.
 */
void
zfs_rangelock_reduce(zfs_locked_range_t *lr, uint64_t off, uint64_t len)
{
	zfs_rangelock_t *rl = lr->lr_rangelock;

	/* Ensure there are no other locks */
	ASSERT3U(avl_numnodes(&rl->rl_tree), ==, 1);
	ASSERT3U(lr->lr_offset, ==, 0);
	ASSERT3U(lr->lr_type, ==, RL_WRITER);
	ASSERT(!lr->lr_proxy);
	ASSERT3U(lr->lr_length, ==, UINT64_MAX);
	ASSERT3U(lr->lr_count, ==, 1);

	mutex_enter(&rl->rl_lock);
	lr->lr_offset = off;
	lr->lr_length = len;
	mutex_exit(&rl->rl_lock);
	if (lr->lr_write_wanted)
		cv_broadcast(&lr->lr_write_cv);
	if (lr->lr_read_wanted)
		cv_broadcast(&lr->lr_read_cv);
}

#if defined(_KERNEL)
EXPORT_SYMBOL(zfs_rangelock_init);
EXPORT_SYMBOL(zfs_rangelock_fini);
EXPORT_SYMBOL(zfs_rangelock_enter);
EXPORT_SYMBOL(zfs_rangelock_tryenter);
EXPORT_SYMBOL(zfs_rangelock_exit);
EXPORT_SYMBOL(zfs_rangelock_reduce);
#endif
