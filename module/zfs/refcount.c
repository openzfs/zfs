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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2021 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/zfs_refcount.h>

#ifdef	ZFS_DEBUG
/*
 * Reference count tracking is disabled by default.  It's memory requirements
 * are reasonable, however as implemented it consumes a significant amount of
 * cpu time.  Until its performance is improved it should be manually enabled.
 */
int reference_tracking_enable = B_FALSE;
static int reference_history = 3; /* tunable */

static kmem_cache_t *reference_cache;
static kmem_cache_t *reference_history_cache;

void
zfs_refcount_init(void)
{
	reference_cache = kmem_cache_create("reference_cache",
	    sizeof (reference_t), 0, NULL, NULL, NULL, NULL, NULL, 0);

	reference_history_cache = kmem_cache_create("reference_history_cache",
	    sizeof (uint64_t), 0, NULL, NULL, NULL, NULL, NULL, 0);
}

void
zfs_refcount_fini(void)
{
	kmem_cache_destroy(reference_cache);
	kmem_cache_destroy(reference_history_cache);
}

void
zfs_refcount_create(zfs_refcount_t *rc)
{
	mutex_init(&rc->rc_mtx, NULL, MUTEX_DEFAULT, NULL);
	list_create(&rc->rc_list, sizeof (reference_t),
	    offsetof(reference_t, ref_link));
	list_create(&rc->rc_removed, sizeof (reference_t),
	    offsetof(reference_t, ref_link));
	rc->rc_count = 0;
	rc->rc_removed_count = 0;
	rc->rc_tracked = reference_tracking_enable;
}

void
zfs_refcount_create_tracked(zfs_refcount_t *rc)
{
	zfs_refcount_create(rc);
	rc->rc_tracked = B_TRUE;
}

void
zfs_refcount_create_untracked(zfs_refcount_t *rc)
{
	zfs_refcount_create(rc);
	rc->rc_tracked = B_FALSE;
}

void
zfs_refcount_destroy_many(zfs_refcount_t *rc, uint64_t number)
{
	reference_t *ref;

	ASSERT3U(rc->rc_count, ==, number);
	while ((ref = list_head(&rc->rc_list))) {
		list_remove(&rc->rc_list, ref);
		kmem_cache_free(reference_cache, ref);
	}
	list_destroy(&rc->rc_list);

	while ((ref = list_head(&rc->rc_removed))) {
		list_remove(&rc->rc_removed, ref);
		kmem_cache_free(reference_history_cache, ref->ref_removed);
		kmem_cache_free(reference_cache, ref);
	}
	list_destroy(&rc->rc_removed);
	mutex_destroy(&rc->rc_mtx);
}

void
zfs_refcount_destroy(zfs_refcount_t *rc)
{
	zfs_refcount_destroy_many(rc, 0);
}

int
zfs_refcount_is_zero(zfs_refcount_t *rc)
{
	return (zfs_refcount_count(rc) == 0);
}

int64_t
zfs_refcount_count(zfs_refcount_t *rc)
{
	return (atomic_load_64(&rc->rc_count));
}

int64_t
zfs_refcount_add_many(zfs_refcount_t *rc, uint64_t number, const void *holder)
{
	reference_t *ref = NULL;
	int64_t count;

	if (!rc->rc_tracked) {
		count = atomic_add_64_nv(&(rc)->rc_count, number);
		ASSERT3U(count, >=, number);
		return (count);
	}

	ref = kmem_cache_alloc(reference_cache, KM_SLEEP);
	ref->ref_holder = holder;
	ref->ref_number = number;
	mutex_enter(&rc->rc_mtx);
	ASSERT3U(rc->rc_count, >=, 0);
	list_insert_head(&rc->rc_list, ref);
	rc->rc_count += number;
	count = rc->rc_count;
	mutex_exit(&rc->rc_mtx);

	return (count);
}

int64_t
zfs_refcount_add(zfs_refcount_t *rc, const void *holder)
{
	return (zfs_refcount_add_many(rc, 1, holder));
}

int64_t
zfs_refcount_remove_many(zfs_refcount_t *rc, uint64_t number,
    const void *holder)
{
	reference_t *ref;
	int64_t count;

	if (!rc->rc_tracked) {
		count = atomic_add_64_nv(&(rc)->rc_count, -number);
		ASSERT3S(count, >=, 0);
		return (count);
	}

	mutex_enter(&rc->rc_mtx);
	ASSERT3U(rc->rc_count, >=, number);
	for (ref = list_head(&rc->rc_list); ref;
	    ref = list_next(&rc->rc_list, ref)) {
		if (ref->ref_holder == holder && ref->ref_number == number) {
			list_remove(&rc->rc_list, ref);
			if (reference_history > 0) {
				ref->ref_removed =
				    kmem_cache_alloc(reference_history_cache,
				    KM_SLEEP);
				list_insert_head(&rc->rc_removed, ref);
				rc->rc_removed_count++;
				if (rc->rc_removed_count > reference_history) {
					ref = list_tail(&rc->rc_removed);
					list_remove(&rc->rc_removed, ref);
					kmem_cache_free(reference_history_cache,
					    ref->ref_removed);
					kmem_cache_free(reference_cache, ref);
					rc->rc_removed_count--;
				}
			} else {
				kmem_cache_free(reference_cache, ref);
			}
			rc->rc_count -= number;
			count = rc->rc_count;
			mutex_exit(&rc->rc_mtx);
			return (count);
		}
	}
	panic("No such hold %p on refcount %llx", holder,
	    (u_longlong_t)(uintptr_t)rc);
	return (-1);
}

int64_t
zfs_refcount_remove(zfs_refcount_t *rc, const void *holder)
{
	return (zfs_refcount_remove_many(rc, 1, holder));
}

void
zfs_refcount_transfer(zfs_refcount_t *dst, zfs_refcount_t *src)
{
	int64_t count, removed_count;
	list_t list, removed;

	list_create(&list, sizeof (reference_t),
	    offsetof(reference_t, ref_link));
	list_create(&removed, sizeof (reference_t),
	    offsetof(reference_t, ref_link));

	mutex_enter(&src->rc_mtx);
	count = src->rc_count;
	removed_count = src->rc_removed_count;
	src->rc_count = 0;
	src->rc_removed_count = 0;
	list_move_tail(&list, &src->rc_list);
	list_move_tail(&removed, &src->rc_removed);
	mutex_exit(&src->rc_mtx);

	mutex_enter(&dst->rc_mtx);
	dst->rc_count += count;
	dst->rc_removed_count += removed_count;
	list_move_tail(&dst->rc_list, &list);
	list_move_tail(&dst->rc_removed, &removed);
	mutex_exit(&dst->rc_mtx);

	list_destroy(&list);
	list_destroy(&removed);
}

void
zfs_refcount_transfer_ownership_many(zfs_refcount_t *rc, uint64_t number,
    const void *current_holder, const void *new_holder)
{
	reference_t *ref;
	boolean_t found = B_FALSE;

	if (!rc->rc_tracked)
		return;

	mutex_enter(&rc->rc_mtx);
	for (ref = list_head(&rc->rc_list); ref;
	    ref = list_next(&rc->rc_list, ref)) {
		if (ref->ref_holder == current_holder &&
		    ref->ref_number == number) {
			ref->ref_holder = new_holder;
			found = B_TRUE;
			break;
		}
	}
	ASSERT(found);
	mutex_exit(&rc->rc_mtx);
}

void
zfs_refcount_transfer_ownership(zfs_refcount_t *rc, const void *current_holder,
    const void *new_holder)
{
	return (zfs_refcount_transfer_ownership_many(rc, 1, current_holder,
	    new_holder));
}

/*
 * If tracking is enabled, return true if a reference exists that matches
 * the "holder" tag. If tracking is disabled, then return true if a reference
 * might be held.
 */
boolean_t
zfs_refcount_held(zfs_refcount_t *rc, const void *holder)
{
	reference_t *ref;

	if (!rc->rc_tracked)
		return (zfs_refcount_count(rc) > 0);

	mutex_enter(&rc->rc_mtx);
	for (ref = list_head(&rc->rc_list); ref;
	    ref = list_next(&rc->rc_list, ref)) {
		if (ref->ref_holder == holder) {
			mutex_exit(&rc->rc_mtx);
			return (B_TRUE);
		}
	}
	mutex_exit(&rc->rc_mtx);
	return (B_FALSE);
}

/*
 * If tracking is enabled, return true if a reference does not exist that
 * matches the "holder" tag. If tracking is disabled, always return true
 * since the reference might not be held.
 */
boolean_t
zfs_refcount_not_held(zfs_refcount_t *rc, const void *holder)
{
	reference_t *ref;

	if (!rc->rc_tracked)
		return (B_TRUE);

	mutex_enter(&rc->rc_mtx);
	for (ref = list_head(&rc->rc_list); ref;
	    ref = list_next(&rc->rc_list, ref)) {
		if (ref->ref_holder == holder) {
			mutex_exit(&rc->rc_mtx);
			return (B_FALSE);
		}
	}
	mutex_exit(&rc->rc_mtx);
	return (B_TRUE);
}

EXPORT_SYMBOL(zfs_refcount_create);
EXPORT_SYMBOL(zfs_refcount_destroy);
EXPORT_SYMBOL(zfs_refcount_is_zero);
EXPORT_SYMBOL(zfs_refcount_count);
EXPORT_SYMBOL(zfs_refcount_add);
EXPORT_SYMBOL(zfs_refcount_remove);
EXPORT_SYMBOL(zfs_refcount_held);

/* BEGIN CSTYLED */
ZFS_MODULE_PARAM(zfs, , reference_tracking_enable, INT, ZMOD_RW,
	"Track reference holders to refcount_t objects");

ZFS_MODULE_PARAM(zfs, , reference_history, INT, ZMOD_RW,
	"Maximum reference holders being tracked");
/* END CSTYLED */
#endif	/* ZFS_DEBUG */
