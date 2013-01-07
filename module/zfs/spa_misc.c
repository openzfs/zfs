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
 * Copyright (c) 2012 by Delphix. All rights reserved.
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/spa_impl.h>
#include <sys/zio.h>
#include <sys/zio_checksum.h>
#include <sys/zio_compress.h>
#include <sys/dmu.h>
#include <sys/dmu_tx.h>
#include <sys/zap.h>
#include <sys/zil.h>
#include <sys/vdev_impl.h>
#include <sys/metaslab.h>
#include <sys/uberblock_impl.h>
#include <sys/txg.h>
#include <sys/avl.h>
#include <sys/unique.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_prop.h>
#include <sys/fm/util.h>
#include <sys/dsl_scan.h>
#include <sys/fs/zfs.h>
#include <sys/metaslab_impl.h>
#include <sys/arc.h>
#include <sys/ddt.h>
#include "zfs_prop.h"
#include "zfeature_common.h"

/*
 * SPA locking
 *
 * There are four basic locks for managing spa_t structures:
 *
 * spa_namespace_lock (global mutex)
 *
 *	This lock must be acquired to do any of the following:
 *
 *		- Lookup a spa_t by name
 *		- Add or remove a spa_t from the namespace
 *		- Increase spa_refcount from non-zero
 *		- Check if spa_refcount is zero
 *		- Rename a spa_t
 *		- add/remove/attach/detach devices
 *		- Held for the duration of create/destroy/import/export
 *
 *	It does not need to handle recursion.  A create or destroy may
 *	reference objects (files or zvols) in other pools, but by
 *	definition they must have an existing reference, and will never need
 *	to lookup a spa_t by name.
 *
 * spa_refcount (per-spa refcount_t protected by mutex)
 *
 *	This reference count keep track of any active users of the spa_t.  The
 *	spa_t cannot be destroyed or freed while this is non-zero.  Internally,
 *	the refcount is never really 'zero' - opening a pool implicitly keeps
 *	some references in the DMU.  Internally we check against spa_minref, but
 *	present the image of a zero/non-zero value to consumers.
 *
 * spa_config_lock[] (per-spa array of rwlocks)
 *
 *	This protects the spa_t from config changes, and must be held in
 *	the following circumstances:
 *
 *		- RW_READER to perform I/O to the spa
 *		- RW_WRITER to change the vdev config
 *
 * The locking order is fairly straightforward:
 *
 *		spa_namespace_lock	->	spa_refcount
 *
 *	The namespace lock must be acquired to increase the refcount from 0
 *	or to check if it is zero.
 *
 *		spa_refcount		->	spa_config_lock[]
 *
 *	There must be at least one valid reference on the spa_t to acquire
 *	the config lock.
 *
 *		spa_namespace_lock	->	spa_config_lock[]
 *
 *	The namespace lock must always be taken before the config lock.
 *
 *
 * The spa_namespace_lock can be acquired directly and is globally visible.
 *
 * The namespace is manipulated using the following functions, all of which
 * require the spa_namespace_lock to be held.
 *
 *	spa_lookup()		Lookup a spa_t by name.
 *
 *	spa_add()		Create a new spa_t in the namespace.
 *
 *	spa_remove()		Remove a spa_t from the namespace.  This also
 *				frees up any memory associated with the spa_t.
 *
 *	spa_next()		Returns the next spa_t in the system, or the
 *				first if NULL is passed.
 *
 *	spa_evict_all()		Shutdown and remove all spa_t structures in
 *				the system.
 *
 *	spa_guid_exists()	Determine whether a pool/device guid exists.
 *
 * The spa_refcount is manipulated using the following functions:
 *
 *	spa_open_ref()		Adds a reference to the given spa_t.  Must be
 *				called with spa_namespace_lock held if the
 *				refcount is currently zero.
 *
 *	spa_close()		Remove a reference from the spa_t.  This will
 *				not free the spa_t or remove it from the
 *				namespace.  No locking is required.
 *
 *	spa_refcount_zero()	Returns true if the refcount is currently
 *				zero.  Must be called with spa_namespace_lock
 *				held.
 *
 * The spa_config_lock[] is an array of rwlocks, ordered as follows:
 * SCL_CONFIG > SCL_STATE > SCL_ALLOC > SCL_ZIO > SCL_FREE > SCL_VDEV.
 * spa_config_lock[] is manipulated with spa_config_{enter,exit,held}().
 *
 * To read the configuration, it suffices to hold one of these locks as reader.
 * To modify the configuration, you must hold all locks as writer.  To modify
 * vdev state without altering the vdev tree's topology (e.g. online/offline),
 * you must hold SCL_STATE and SCL_ZIO as writer.
 *
 * We use these distinct config locks to avoid recursive lock entry.
 * For example, spa_sync() (which holds SCL_CONFIG as reader) induces
 * block allocations (SCL_ALLOC), which may require reading space maps
 * from disk (dmu_read() -> zio_read() -> SCL_ZIO).
 *
 * The spa config locks cannot be normal rwlocks because we need the
 * ability to hand off ownership.  For example, SCL_ZIO is acquired
 * by the issuing thread and later released by an interrupt thread.
 * They do, however, obey the usual write-wanted semantics to prevent
 * writer (i.e. system administrator) starvation.
 *
 * The lock acquisition rules are as follows:
 *
 * SCL_CONFIG
 *	Protects changes to the vdev tree topology, such as vdev
 *	add/remove/attach/detach.  Protects the dirty config list
 *	(spa_config_dirty_list) and the set of spares and l2arc devices.
 *
 * SCL_STATE
 *	Protects changes to pool state and vdev state, such as vdev
 *	online/offline/fault/degrade/clear.  Protects the dirty state list
 *	(spa_state_dirty_list) and global pool state (spa_state).
 *
 * SCL_ALLOC
 *	Protects changes to metaslab groups and classes.
 *	Held as reader by metaslab_alloc() and metaslab_claim().
 *
 * SCL_ZIO
 *	Held by bp-level zios (those which have no io_vd upon entry)
 *	to prevent changes to the vdev tree.  The bp-level zio implicitly
 *	protects all of its vdev child zios, which do not hold SCL_ZIO.
 *
 * SCL_FREE
 *	Protects changes to metaslab groups and classes.
 *	Held as reader by metaslab_free().  SCL_FREE is distinct from
 *	SCL_ALLOC, and lower than SCL_ZIO, so that we can safely free
 *	blocks in zio_done() while another i/o that holds either
 *	SCL_ALLOC or SCL_ZIO is waiting for this i/o to complete.
 *
 * SCL_VDEV
 *	Held as reader to prevent changes to the vdev tree during trivial
 *	inquiries such as bp_get_dsize().  SCL_VDEV is distinct from the
 *	other locks, and lower than all of them, to ensure that it's safe
 *	to acquire regardless of caller context.
 *
 * In addition, the following rules apply:
 *
 * (a)	spa_props_lock protects pool properties, spa_config and spa_config_list.
 *	The lock ordering is SCL_CONFIG > spa_props_lock.
 *
 * (b)	I/O operations on leaf vdevs.  For any zio operation that takes
 *	an explicit vdev_t argument -- such as zio_ioctl(), zio_read_phys(),
 *	or zio_write_phys() -- the caller must ensure that the config cannot
 *	cannot change in the interim, and that the vdev cannot be reopened.
 *	SCL_STATE as reader suffices for both.
 *
 * The vdev configuration is protected by spa_vdev_enter() / spa_vdev_exit().
 *
 *	spa_vdev_enter()	Acquire the namespace lock and the config lock
 *				for writing.
 *
 *	spa_vdev_exit()		Release the config lock, wait for all I/O
 *				to complete, sync the updated configs to the
 *				cache, and release the namespace lock.
 *
 * vdev state is protected by spa_vdev_state_enter() / spa_vdev_state_exit().
 * Like spa_vdev_enter/exit, these are convenience wrappers -- the actual
 * locking is, always, based on spa_namespace_lock and spa_config_lock[].
 *
 * spa_rename() is also implemented within this file since it requires
 * manipulation of the namespace.
 */

static avl_tree_t spa_namespace_avl;
kmutex_t spa_namespace_lock;
static kcondvar_t spa_namespace_cv;
static int spa_active_count;
int spa_max_replication_override = SPA_DVAS_PER_BP;

static kmutex_t spa_spare_lock;
static avl_tree_t spa_spare_avl;
static kmutex_t spa_l2cache_lock;
static avl_tree_t spa_l2cache_avl;

kmem_cache_t *spa_buffer_pool;
int spa_mode_global;

/*
 * ==========================================================================
 * SPA config locking
 * ==========================================================================
 */
static void
spa_config_lock_init(spa_t *spa)
{
	int i;

	for (i = 0; i < SCL_LOCKS; i++) {
		spa_config_lock_t *scl = &spa->spa_config_lock[i];
		mutex_init(&scl->scl_lock, NULL, MUTEX_DEFAULT, NULL);
		cv_init(&scl->scl_cv, NULL, CV_DEFAULT, NULL);
		refcount_create(&scl->scl_count);
		scl->scl_writer = NULL;
		scl->scl_write_wanted = 0;
	}
}

static void
spa_config_lock_destroy(spa_t *spa)
{
	int i;

	for (i = 0; i < SCL_LOCKS; i++) {
		spa_config_lock_t *scl = &spa->spa_config_lock[i];
		mutex_destroy(&scl->scl_lock);
		cv_destroy(&scl->scl_cv);
		refcount_destroy(&scl->scl_count);
		ASSERT(scl->scl_writer == NULL);
		ASSERT(scl->scl_write_wanted == 0);
	}
}

int
spa_config_tryenter(spa_t *spa, int locks, void *tag, krw_t rw)
{
	int i;

	for (i = 0; i < SCL_LOCKS; i++) {
		spa_config_lock_t *scl = &spa->spa_config_lock[i];
		if (!(locks & (1 << i)))
			continue;
		mutex_enter(&scl->scl_lock);
		if (rw == RW_READER) {
			if (scl->scl_writer || scl->scl_write_wanted) {
				mutex_exit(&scl->scl_lock);
				spa_config_exit(spa, locks ^ (1 << i), tag);
				return (0);
			}
		} else {
			ASSERT(scl->scl_writer != curthread);
			if (!refcount_is_zero(&scl->scl_count)) {
				mutex_exit(&scl->scl_lock);
				spa_config_exit(spa, locks ^ (1 << i), tag);
				return (0);
			}
			scl->scl_writer = curthread;
		}
		(void) refcount_add(&scl->scl_count, tag);
		mutex_exit(&scl->scl_lock);
	}
	return (1);
}

void
spa_config_enter(spa_t *spa, int locks, void *tag, krw_t rw)
{
	int wlocks_held = 0;
	int i;

	for (i = 0; i < SCL_LOCKS; i++) {
		spa_config_lock_t *scl = &spa->spa_config_lock[i];
		if (scl->scl_writer == curthread)
			wlocks_held |= (1 << i);
		if (!(locks & (1 << i)))
			continue;
		mutex_enter(&scl->scl_lock);
		if (rw == RW_READER) {
			while (scl->scl_writer || scl->scl_write_wanted) {
				cv_wait(&scl->scl_cv, &scl->scl_lock);
			}
		} else {
			ASSERT(scl->scl_writer != curthread);
			while (!refcount_is_zero(&scl->scl_count)) {
				scl->scl_write_wanted++;
				cv_wait(&scl->scl_cv, &scl->scl_lock);
				scl->scl_write_wanted--;
			}
			scl->scl_writer = curthread;
		}
		(void) refcount_add(&scl->scl_count, tag);
		mutex_exit(&scl->scl_lock);
	}
	ASSERT(wlocks_held <= locks);
}

void
spa_config_exit(spa_t *spa, int locks, void *tag)
{
	int i;

	for (i = SCL_LOCKS - 1; i >= 0; i--) {
		spa_config_lock_t *scl = &spa->spa_config_lock[i];
		if (!(locks & (1 << i)))
			continue;
		mutex_enter(&scl->scl_lock);
		ASSERT(!refcount_is_zero(&scl->scl_count));
		if (refcount_remove(&scl->scl_count, tag) == 0) {
			ASSERT(scl->scl_writer == NULL ||
			    scl->scl_writer == curthread);
			scl->scl_writer = NULL;	/* OK in either case */
			cv_broadcast(&scl->scl_cv);
		}
		mutex_exit(&scl->scl_lock);
	}
}

int
spa_config_held(spa_t *spa, int locks, krw_t rw)
{
	int i, locks_held = 0;

	for (i = 0; i < SCL_LOCKS; i++) {
		spa_config_lock_t *scl = &spa->spa_config_lock[i];
		if (!(locks & (1 << i)))
			continue;
		if ((rw == RW_READER && !refcount_is_zero(&scl->scl_count)) ||
		    (rw == RW_WRITER && scl->scl_writer == curthread))
			locks_held |= 1 << i;
	}

	return (locks_held);
}

/*
 * ==========================================================================
 * SPA namespace functions
 * ==========================================================================
 */

/*
 * Lookup the named spa_t in the AVL tree.  The spa_namespace_lock must be held.
 * Returns NULL if no matching spa_t is found.
 */
spa_t *
spa_lookup(const char *name)
{
	static spa_t search;	/* spa_t is large; don't allocate on stack */
	spa_t *spa;
	avl_index_t where;
	char c = 0;
	char *cp;

	ASSERT(MUTEX_HELD(&spa_namespace_lock));

	/*
	 * If it's a full dataset name, figure out the pool name and
	 * just use that.
	 */
	cp = strpbrk(name, "/@");
	if (cp) {
		c = *cp;
		*cp = '\0';
	}

	(void) strlcpy(search.spa_name, name, sizeof (search.spa_name));
	spa = avl_find(&spa_namespace_avl, &search, &where);

	if (cp)
		*cp = c;

	return (spa);
}

/*
 * Create an uninitialized spa_t with the given name.  Requires
 * spa_namespace_lock.  The caller must ensure that the spa_t doesn't already
 * exist by calling spa_lookup() first.
 */
spa_t *
spa_add(const char *name, nvlist_t *config, const char *altroot)
{
	spa_t *spa;
	spa_config_dirent_t *dp;
	int t;

	ASSERT(MUTEX_HELD(&spa_namespace_lock));

	spa = kmem_zalloc(sizeof (spa_t), KM_PUSHPAGE | KM_NODEBUG);

	mutex_init(&spa->spa_async_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&spa->spa_errlist_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&spa->spa_errlog_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&spa->spa_history_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&spa->spa_proc_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&spa->spa_props_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&spa->spa_scrub_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&spa->spa_suspend_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&spa->spa_vdev_top_lock, NULL, MUTEX_DEFAULT, NULL);

	cv_init(&spa->spa_async_cv, NULL, CV_DEFAULT, NULL);
	cv_init(&spa->spa_proc_cv, NULL, CV_DEFAULT, NULL);
	cv_init(&spa->spa_scrub_io_cv, NULL, CV_DEFAULT, NULL);
	cv_init(&spa->spa_suspend_cv, NULL, CV_DEFAULT, NULL);

	for (t = 0; t < TXG_SIZE; t++)
		bplist_create(&spa->spa_free_bplist[t]);

	(void) strlcpy(spa->spa_name, name, sizeof (spa->spa_name));
	spa->spa_state = POOL_STATE_UNINITIALIZED;
	spa->spa_freeze_txg = UINT64_MAX;
	spa->spa_final_txg = UINT64_MAX;
	spa->spa_load_max_txg = UINT64_MAX;
	spa->spa_proc = &p0;
	spa->spa_proc_state = SPA_PROC_NONE;

	refcount_create(&spa->spa_refcount);
	spa_config_lock_init(spa);

	avl_add(&spa_namespace_avl, spa);

	/*
	 * Set the alternate root, if there is one.
	 */
	if (altroot) {
		spa->spa_root = spa_strdup(altroot);
		spa_active_count++;
	}

	/*
	 * Every pool starts with the default cachefile
	 */
	list_create(&spa->spa_config_list, sizeof (spa_config_dirent_t),
	    offsetof(spa_config_dirent_t, scd_link));

	dp = kmem_zalloc(sizeof (spa_config_dirent_t), KM_PUSHPAGE);
	dp->scd_path = altroot ? NULL : spa_strdup(spa_config_path);
	list_insert_head(&spa->spa_config_list, dp);

	VERIFY(nvlist_alloc(&spa->spa_load_info, NV_UNIQUE_NAME,
	    KM_PUSHPAGE) == 0);

	if (config != NULL) {
		nvlist_t *features;

		if (nvlist_lookup_nvlist(config, ZPOOL_CONFIG_FEATURES_FOR_READ,
		    &features) == 0) {
			VERIFY(nvlist_dup(features, &spa->spa_label_features,
			    0) == 0);
		}

		VERIFY(nvlist_dup(config, &spa->spa_config, 0) == 0);
	}

	if (spa->spa_label_features == NULL) {
		VERIFY(nvlist_alloc(&spa->spa_label_features, NV_UNIQUE_NAME,
		    KM_SLEEP) == 0);
	}

	return (spa);
}

/*
 * Removes a spa_t from the namespace, freeing up any memory used.  Requires
 * spa_namespace_lock.  This is called only after the spa_t has been closed and
 * deactivated.
 */
void
spa_remove(spa_t *spa)
{
	spa_config_dirent_t *dp;
	int t;

	ASSERT(MUTEX_HELD(&spa_namespace_lock));
	ASSERT(spa->spa_state == POOL_STATE_UNINITIALIZED);

	nvlist_free(spa->spa_config_splitting);

	avl_remove(&spa_namespace_avl, spa);
	cv_broadcast(&spa_namespace_cv);

	if (spa->spa_root) {
		spa_strfree(spa->spa_root);
		spa_active_count--;
	}

	while ((dp = list_head(&spa->spa_config_list)) != NULL) {
		list_remove(&spa->spa_config_list, dp);
		if (dp->scd_path != NULL)
			spa_strfree(dp->scd_path);
		kmem_free(dp, sizeof (spa_config_dirent_t));
	}

	list_destroy(&spa->spa_config_list);

	nvlist_free(spa->spa_label_features);
	nvlist_free(spa->spa_load_info);
	spa_config_set(spa, NULL);

	refcount_destroy(&spa->spa_refcount);

	spa_config_lock_destroy(spa);

	for (t = 0; t < TXG_SIZE; t++)
		bplist_destroy(&spa->spa_free_bplist[t]);

	cv_destroy(&spa->spa_async_cv);
	cv_destroy(&spa->spa_proc_cv);
	cv_destroy(&spa->spa_scrub_io_cv);
	cv_destroy(&spa->spa_suspend_cv);

	mutex_destroy(&spa->spa_async_lock);
	mutex_destroy(&spa->spa_errlist_lock);
	mutex_destroy(&spa->spa_errlog_lock);
	mutex_destroy(&spa->spa_history_lock);
	mutex_destroy(&spa->spa_proc_lock);
	mutex_destroy(&spa->spa_props_lock);
	mutex_destroy(&spa->spa_scrub_lock);
	mutex_destroy(&spa->spa_suspend_lock);
	mutex_destroy(&spa->spa_vdev_top_lock);

	kmem_free(spa, sizeof (spa_t));
}

/*
 * Given a pool, return the next pool in the namespace, or NULL if there is
 * none.  If 'prev' is NULL, return the first pool.
 */
spa_t *
spa_next(spa_t *prev)
{
	ASSERT(MUTEX_HELD(&spa_namespace_lock));

	if (prev)
		return (AVL_NEXT(&spa_namespace_avl, prev));
	else
		return (avl_first(&spa_namespace_avl));
}

/*
 * ==========================================================================
 * SPA refcount functions
 * ==========================================================================
 */

/*
 * Add a reference to the given spa_t.  Must have at least one reference, or
 * have the namespace lock held.
 */
void
spa_open_ref(spa_t *spa, void *tag)
{
	ASSERT(refcount_count(&spa->spa_refcount) >= spa->spa_minref ||
	    MUTEX_HELD(&spa_namespace_lock));
	(void) refcount_add(&spa->spa_refcount, tag);
}

/*
 * Remove a reference to the given spa_t.  Must have at least one reference, or
 * have the namespace lock held.
 */
void
spa_close(spa_t *spa, void *tag)
{
	ASSERT(refcount_count(&spa->spa_refcount) > spa->spa_minref ||
	    MUTEX_HELD(&spa_namespace_lock));
	(void) refcount_remove(&spa->spa_refcount, tag);
}

/*
 * Check to see if the spa refcount is zero.  Must be called with
 * spa_namespace_lock held.  We really compare against spa_minref, which is the
 * number of references acquired when opening a pool
 */
boolean_t
spa_refcount_zero(spa_t *spa)
{
	ASSERT(MUTEX_HELD(&spa_namespace_lock));

	return (refcount_count(&spa->spa_refcount) == spa->spa_minref);
}

/*
 * ==========================================================================
 * SPA spare and l2cache tracking
 * ==========================================================================
 */

/*
 * Hot spares and cache devices are tracked using the same code below,
 * for 'auxiliary' devices.
 */

typedef struct spa_aux {
	uint64_t	aux_guid;
	uint64_t	aux_pool;
	avl_node_t	aux_avl;
	int		aux_count;
} spa_aux_t;

static int
spa_aux_compare(const void *a, const void *b)
{
	const spa_aux_t *sa = a;
	const spa_aux_t *sb = b;

	if (sa->aux_guid < sb->aux_guid)
		return (-1);
	else if (sa->aux_guid > sb->aux_guid)
		return (1);
	else
		return (0);
}

void
spa_aux_add(vdev_t *vd, avl_tree_t *avl)
{
	avl_index_t where;
	spa_aux_t search;
	spa_aux_t *aux;

	search.aux_guid = vd->vdev_guid;
	if ((aux = avl_find(avl, &search, &where)) != NULL) {
		aux->aux_count++;
	} else {
		aux = kmem_zalloc(sizeof (spa_aux_t), KM_PUSHPAGE);
		aux->aux_guid = vd->vdev_guid;
		aux->aux_count = 1;
		avl_insert(avl, aux, where);
	}
}

void
spa_aux_remove(vdev_t *vd, avl_tree_t *avl)
{
	spa_aux_t search;
	spa_aux_t *aux;
	avl_index_t where;

	search.aux_guid = vd->vdev_guid;
	aux = avl_find(avl, &search, &where);

	ASSERT(aux != NULL);

	if (--aux->aux_count == 0) {
		avl_remove(avl, aux);
		kmem_free(aux, sizeof (spa_aux_t));
	} else if (aux->aux_pool == spa_guid(vd->vdev_spa)) {
		aux->aux_pool = 0ULL;
	}
}

boolean_t
spa_aux_exists(uint64_t guid, uint64_t *pool, int *refcnt, avl_tree_t *avl)
{
	spa_aux_t search, *found;

	search.aux_guid = guid;
	found = avl_find(avl, &search, NULL);

	if (pool) {
		if (found)
			*pool = found->aux_pool;
		else
			*pool = 0ULL;
	}

	if (refcnt) {
		if (found)
			*refcnt = found->aux_count;
		else
			*refcnt = 0;
	}

	return (found != NULL);
}

void
spa_aux_activate(vdev_t *vd, avl_tree_t *avl)
{
	spa_aux_t search, *found;
	avl_index_t where;

	search.aux_guid = vd->vdev_guid;
	found = avl_find(avl, &search, &where);
	ASSERT(found != NULL);
	ASSERT(found->aux_pool == 0ULL);

	found->aux_pool = spa_guid(vd->vdev_spa);
}

/*
 * Spares are tracked globally due to the following constraints:
 *
 * 	- A spare may be part of multiple pools.
 * 	- A spare may be added to a pool even if it's actively in use within
 *	  another pool.
 * 	- A spare in use in any pool can only be the source of a replacement if
 *	  the target is a spare in the same pool.
 *
 * We keep track of all spares on the system through the use of a reference
 * counted AVL tree.  When a vdev is added as a spare, or used as a replacement
 * spare, then we bump the reference count in the AVL tree.  In addition, we set
 * the 'vdev_isspare' member to indicate that the device is a spare (active or
 * inactive).  When a spare is made active (used to replace a device in the
 * pool), we also keep track of which pool its been made a part of.
 *
 * The 'spa_spare_lock' protects the AVL tree.  These functions are normally
 * called under the spa_namespace lock as part of vdev reconfiguration.  The
 * separate spare lock exists for the status query path, which does not need to
 * be completely consistent with respect to other vdev configuration changes.
 */

static int
spa_spare_compare(const void *a, const void *b)
{
	return (spa_aux_compare(a, b));
}

void
spa_spare_add(vdev_t *vd)
{
	mutex_enter(&spa_spare_lock);
	ASSERT(!vd->vdev_isspare);
	spa_aux_add(vd, &spa_spare_avl);
	vd->vdev_isspare = B_TRUE;
	mutex_exit(&spa_spare_lock);
}

void
spa_spare_remove(vdev_t *vd)
{
	mutex_enter(&spa_spare_lock);
	ASSERT(vd->vdev_isspare);
	spa_aux_remove(vd, &spa_spare_avl);
	vd->vdev_isspare = B_FALSE;
	mutex_exit(&spa_spare_lock);
}

boolean_t
spa_spare_exists(uint64_t guid, uint64_t *pool, int *refcnt)
{
	boolean_t found;

	mutex_enter(&spa_spare_lock);
	found = spa_aux_exists(guid, pool, refcnt, &spa_spare_avl);
	mutex_exit(&spa_spare_lock);

	return (found);
}

void
spa_spare_activate(vdev_t *vd)
{
	mutex_enter(&spa_spare_lock);
	ASSERT(vd->vdev_isspare);
	spa_aux_activate(vd, &spa_spare_avl);
	mutex_exit(&spa_spare_lock);
}

/*
 * Level 2 ARC devices are tracked globally for the same reasons as spares.
 * Cache devices currently only support one pool per cache device, and so
 * for these devices the aux reference count is currently unused beyond 1.
 */

static int
spa_l2cache_compare(const void *a, const void *b)
{
	return (spa_aux_compare(a, b));
}

void
spa_l2cache_add(vdev_t *vd)
{
	mutex_enter(&spa_l2cache_lock);
	ASSERT(!vd->vdev_isl2cache);
	spa_aux_add(vd, &spa_l2cache_avl);
	vd->vdev_isl2cache = B_TRUE;
	mutex_exit(&spa_l2cache_lock);
}

void
spa_l2cache_remove(vdev_t *vd)
{
	mutex_enter(&spa_l2cache_lock);
	ASSERT(vd->vdev_isl2cache);
	spa_aux_remove(vd, &spa_l2cache_avl);
	vd->vdev_isl2cache = B_FALSE;
	mutex_exit(&spa_l2cache_lock);
}

boolean_t
spa_l2cache_exists(uint64_t guid, uint64_t *pool)
{
	boolean_t found;

	mutex_enter(&spa_l2cache_lock);
	found = spa_aux_exists(guid, pool, NULL, &spa_l2cache_avl);
	mutex_exit(&spa_l2cache_lock);

	return (found);
}

void
spa_l2cache_activate(vdev_t *vd)
{
	mutex_enter(&spa_l2cache_lock);
	ASSERT(vd->vdev_isl2cache);
	spa_aux_activate(vd, &spa_l2cache_avl);
	mutex_exit(&spa_l2cache_lock);
}

/*
 * ==========================================================================
 * SPA vdev locking
 * ==========================================================================
 */

/*
 * Lock the given spa_t for the purpose of adding or removing a vdev.
 * Grabs the global spa_namespace_lock plus the spa config lock for writing.
 * It returns the next transaction group for the spa_t.
 */
uint64_t
spa_vdev_enter(spa_t *spa)
{
	mutex_enter(&spa->spa_vdev_top_lock);
	mutex_enter(&spa_namespace_lock);
	return (spa_vdev_config_enter(spa));
}

/*
 * Internal implementation for spa_vdev_enter().  Used when a vdev
 * operation requires multiple syncs (i.e. removing a device) while
 * keeping the spa_namespace_lock held.
 */
uint64_t
spa_vdev_config_enter(spa_t *spa)
{
	ASSERT(MUTEX_HELD(&spa_namespace_lock));

	spa_config_enter(spa, SCL_ALL, spa, RW_WRITER);

	return (spa_last_synced_txg(spa) + 1);
}

/*
 * Used in combination with spa_vdev_config_enter() to allow the syncing
 * of multiple transactions without releasing the spa_namespace_lock.
 */
void
spa_vdev_config_exit(spa_t *spa, vdev_t *vd, uint64_t txg, int error, char *tag)
{
	int config_changed = B_FALSE;

	ASSERT(MUTEX_HELD(&spa_namespace_lock));
	ASSERT(txg > spa_last_synced_txg(spa));

	spa->spa_pending_vdev = NULL;

	/*
	 * Reassess the DTLs.
	 */
	vdev_dtl_reassess(spa->spa_root_vdev, 0, 0, B_FALSE);

	if (error == 0 && !list_is_empty(&spa->spa_config_dirty_list)) {
		config_changed = B_TRUE;
		spa->spa_config_generation++;
	}

	/*
	 * Verify the metaslab classes.
	 */
	ASSERT(metaslab_class_validate(spa_normal_class(spa)) == 0);
	ASSERT(metaslab_class_validate(spa_log_class(spa)) == 0);

	spa_config_exit(spa, SCL_ALL, spa);

	/*
	 * Panic the system if the specified tag requires it.  This
	 * is useful for ensuring that configurations are updated
	 * transactionally.
	 */
	if (zio_injection_enabled)
		zio_handle_panic_injection(spa, tag, 0);

	/*
	 * Note: this txg_wait_synced() is important because it ensures
	 * that there won't be more than one config change per txg.
	 * This allows us to use the txg as the generation number.
	 */
	if (error == 0)
		txg_wait_synced(spa->spa_dsl_pool, txg);

	if (vd != NULL) {
		ASSERT(!vd->vdev_detached || vd->vdev_dtl_smo.smo_object == 0);
		spa_config_enter(spa, SCL_ALL, spa, RW_WRITER);
		vdev_free(vd);
		spa_config_exit(spa, SCL_ALL, spa);
	}

	/*
	 * If the config changed, update the config cache.
	 */
	if (config_changed)
		spa_config_sync(spa, B_FALSE, B_TRUE);
}

/*
 * Unlock the spa_t after adding or removing a vdev.  Besides undoing the
 * locking of spa_vdev_enter(), we also want make sure the transactions have
 * synced to disk, and then update the global configuration cache with the new
 * information.
 */
int
spa_vdev_exit(spa_t *spa, vdev_t *vd, uint64_t txg, int error)
{
	spa_vdev_config_exit(spa, vd, txg, error, FTAG);
	mutex_exit(&spa_namespace_lock);
	mutex_exit(&spa->spa_vdev_top_lock);

	return (error);
}

/*
 * Lock the given spa_t for the purpose of changing vdev state.
 */
void
spa_vdev_state_enter(spa_t *spa, int oplocks)
{
	int locks = SCL_STATE_ALL | oplocks;

	/*
	 * Root pools may need to read of the underlying devfs filesystem
	 * when opening up a vdev.  Unfortunately if we're holding the
	 * SCL_ZIO lock it will result in a deadlock when we try to issue
	 * the read from the root filesystem.  Instead we "prefetch"
	 * the associated vnodes that we need prior to opening the
	 * underlying devices and cache them so that we can prevent
	 * any I/O when we are doing the actual open.
	 */
	if (spa_is_root(spa)) {
		int low = locks & ~(SCL_ZIO - 1);
		int high = locks & ~low;

		spa_config_enter(spa, high, spa, RW_WRITER);
		vdev_hold(spa->spa_root_vdev);
		spa_config_enter(spa, low, spa, RW_WRITER);
	} else {
		spa_config_enter(spa, locks, spa, RW_WRITER);
	}
	spa->spa_vdev_locks = locks;
}

int
spa_vdev_state_exit(spa_t *spa, vdev_t *vd, int error)
{
	boolean_t config_changed = B_FALSE;

	if (vd != NULL || error == 0)
		vdev_dtl_reassess(vd ? vd->vdev_top : spa->spa_root_vdev,
		    0, 0, B_FALSE);

	if (vd != NULL) {
		vdev_state_dirty(vd->vdev_top);
		config_changed = B_TRUE;
		spa->spa_config_generation++;
	}

	if (spa_is_root(spa))
		vdev_rele(spa->spa_root_vdev);

	ASSERT3U(spa->spa_vdev_locks, >=, SCL_STATE_ALL);
	spa_config_exit(spa, spa->spa_vdev_locks, spa);

	/*
	 * If anything changed, wait for it to sync.  This ensures that,
	 * from the system administrator's perspective, zpool(1M) commands
	 * are synchronous.  This is important for things like zpool offline:
	 * when the command completes, you expect no further I/O from ZFS.
	 */
	if (vd != NULL)
		txg_wait_synced(spa->spa_dsl_pool, 0);

	/*
	 * If the config changed, update the config cache.
	 */
	if (config_changed) {
		mutex_enter(&spa_namespace_lock);
		spa_config_sync(spa, B_FALSE, B_TRUE);
		mutex_exit(&spa_namespace_lock);
	}

	return (error);
}

/*
 * ==========================================================================
 * Miscellaneous functions
 * ==========================================================================
 */

void
spa_activate_mos_feature(spa_t *spa, const char *feature)
{
	(void) nvlist_add_boolean(spa->spa_label_features, feature);
	vdev_config_dirty(spa->spa_root_vdev);
}

void
spa_deactivate_mos_feature(spa_t *spa, const char *feature)
{
	(void) nvlist_remove_all(spa->spa_label_features, feature);
	vdev_config_dirty(spa->spa_root_vdev);
}

/*
 * Rename a spa_t.
 */
int
spa_rename(const char *name, const char *newname)
{
	spa_t *spa;
	int err;

	/*
	 * Lookup the spa_t and grab the config lock for writing.  We need to
	 * actually open the pool so that we can sync out the necessary labels.
	 * It's OK to call spa_open() with the namespace lock held because we
	 * allow recursive calls for other reasons.
	 */
	mutex_enter(&spa_namespace_lock);
	if ((err = spa_open(name, &spa, FTAG)) != 0) {
		mutex_exit(&spa_namespace_lock);
		return (err);
	}

	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);

	avl_remove(&spa_namespace_avl, spa);
	(void) strlcpy(spa->spa_name, newname, sizeof (spa->spa_name));
	avl_add(&spa_namespace_avl, spa);

	/*
	 * Sync all labels to disk with the new names by marking the root vdev
	 * dirty and waiting for it to sync.  It will pick up the new pool name
	 * during the sync.
	 */
	vdev_config_dirty(spa->spa_root_vdev);

	spa_config_exit(spa, SCL_ALL, FTAG);

	txg_wait_synced(spa->spa_dsl_pool, 0);

	/*
	 * Sync the updated config cache.
	 */
	spa_config_sync(spa, B_FALSE, B_TRUE);

	spa_close(spa, FTAG);

	mutex_exit(&spa_namespace_lock);

	return (0);
}

/*
 * Return the spa_t associated with given pool_guid, if it exists.  If
 * device_guid is non-zero, determine whether the pool exists *and* contains
 * a device with the specified device_guid.
 */
spa_t *
spa_by_guid(uint64_t pool_guid, uint64_t device_guid)
{
	spa_t *spa;
	avl_tree_t *t = &spa_namespace_avl;

	ASSERT(MUTEX_HELD(&spa_namespace_lock));

	for (spa = avl_first(t); spa != NULL; spa = AVL_NEXT(t, spa)) {
		if (spa->spa_state == POOL_STATE_UNINITIALIZED)
			continue;
		if (spa->spa_root_vdev == NULL)
			continue;
		if (spa_guid(spa) == pool_guid) {
			if (device_guid == 0)
				break;

			if (vdev_lookup_by_guid(spa->spa_root_vdev,
			    device_guid) != NULL)
				break;

			/*
			 * Check any devices we may be in the process of adding.
			 */
			if (spa->spa_pending_vdev) {
				if (vdev_lookup_by_guid(spa->spa_pending_vdev,
				    device_guid) != NULL)
					break;
			}
		}
	}

	return (spa);
}

/*
 * Determine whether a pool with the given pool_guid exists.
 */
boolean_t
spa_guid_exists(uint64_t pool_guid, uint64_t device_guid)
{
	return (spa_by_guid(pool_guid, device_guid) != NULL);
}

char *
spa_strdup(const char *s)
{
	size_t len;
	char *new;

	len = strlen(s);
	new = kmem_alloc(len + 1, KM_PUSHPAGE);
	bcopy(s, new, len);
	new[len] = '\0';

	return (new);
}

void
spa_strfree(char *s)
{
	kmem_free(s, strlen(s) + 1);
}

uint64_t
spa_get_random(uint64_t range)
{
	uint64_t r;

	ASSERT(range != 0);

	(void) random_get_pseudo_bytes((void *)&r, sizeof (uint64_t));

	return (r % range);
}

uint64_t
spa_generate_guid(spa_t *spa)
{
	uint64_t guid = spa_get_random(-1ULL);

	if (spa != NULL) {
		while (guid == 0 || spa_guid_exists(spa_guid(spa), guid))
			guid = spa_get_random(-1ULL);
	} else {
		while (guid == 0 || spa_guid_exists(guid, 0))
			guid = spa_get_random(-1ULL);
	}

	return (guid);
}

void
sprintf_blkptr(char *buf, const blkptr_t *bp)
{
	char type[256];
	char *checksum = NULL;
	char *compress = NULL;

	if (bp != NULL) {
		if (BP_GET_TYPE(bp) & DMU_OT_NEWTYPE) {
			dmu_object_byteswap_t bswap =
			    DMU_OT_BYTESWAP(BP_GET_TYPE(bp));
			(void) snprintf(type, sizeof (type), "bswap %s %s",
			    DMU_OT_IS_METADATA(BP_GET_TYPE(bp)) ?
			    "metadata" : "data",
			    dmu_ot_byteswap[bswap].ob_name);
		} else {
			(void) strlcpy(type, dmu_ot[BP_GET_TYPE(bp)].ot_name,
			    sizeof (type));
		}
		checksum = zio_checksum_table[BP_GET_CHECKSUM(bp)].ci_name;
		compress = zio_compress_table[BP_GET_COMPRESS(bp)].ci_name;
	}

	SPRINTF_BLKPTR(snprintf, ' ', buf, bp, type, checksum, compress);
}

void
spa_freeze(spa_t *spa)
{
	uint64_t freeze_txg = 0;

	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
	if (spa->spa_freeze_txg == UINT64_MAX) {
		freeze_txg = spa_last_synced_txg(spa) + TXG_SIZE;
		spa->spa_freeze_txg = freeze_txg;
	}
	spa_config_exit(spa, SCL_ALL, FTAG);
	if (freeze_txg != 0)
		txg_wait_synced(spa_get_dsl(spa), freeze_txg);
}

/*
 * This is a stripped-down version of strtoull, suitable only for converting
 * lowercase hexidecimal numbers that don't overflow.
 */
uint64_t
strtonum(const char *str, char **nptr)
{
	uint64_t val = 0;
	char c;
	int digit;

	while ((c = *str) != '\0') {
		if (c >= '0' && c <= '9')
			digit = c - '0';
		else if (c >= 'a' && c <= 'f')
			digit = 10 + c - 'a';
		else
			break;

		val *= 16;
		val += digit;

		str++;
	}

	if (nptr)
		*nptr = (char *)str;

	return (val);
}

/*
 * ==========================================================================
 * Accessor functions
 * ==========================================================================
 */

boolean_t
spa_shutting_down(spa_t *spa)
{
	return (spa->spa_async_suspended);
}

dsl_pool_t *
spa_get_dsl(spa_t *spa)
{
	return (spa->spa_dsl_pool);
}

boolean_t
spa_is_initializing(spa_t *spa)
{
	return (spa->spa_is_initializing);
}

blkptr_t *
spa_get_rootblkptr(spa_t *spa)
{
	return (&spa->spa_ubsync.ub_rootbp);
}

void
spa_set_rootblkptr(spa_t *spa, const blkptr_t *bp)
{
	spa->spa_uberblock.ub_rootbp = *bp;
}

void
spa_altroot(spa_t *spa, char *buf, size_t buflen)
{
	if (spa->spa_root == NULL)
		buf[0] = '\0';
	else
		(void) strncpy(buf, spa->spa_root, buflen);
}

int
spa_sync_pass(spa_t *spa)
{
	return (spa->spa_sync_pass);
}

char *
spa_name(spa_t *spa)
{
	return (spa->spa_name);
}

uint64_t
spa_guid(spa_t *spa)
{
	dsl_pool_t *dp = spa_get_dsl(spa);
	uint64_t guid;

	/*
	 * If we fail to parse the config during spa_load(), we can go through
	 * the error path (which posts an ereport) and end up here with no root
	 * vdev.  We stash the original pool guid in 'spa_config_guid' to handle
	 * this case.
	 */
	if (spa->spa_root_vdev == NULL)
		return (spa->spa_config_guid);

	guid = spa->spa_last_synced_guid != 0 ?
	    spa->spa_last_synced_guid : spa->spa_root_vdev->vdev_guid;

	/*
	 * Return the most recently synced out guid unless we're
	 * in syncing context.
	 */
	if (dp && dsl_pool_sync_context(dp))
		return (spa->spa_root_vdev->vdev_guid);
	else
		return (guid);
}

uint64_t
spa_load_guid(spa_t *spa)
{
	/*
	 * This is a GUID that exists solely as a reference for the
	 * purposes of the arc.  It is generated at load time, and
	 * is never written to persistent storage.
	 */
	return (spa->spa_load_guid);
}

uint64_t
spa_last_synced_txg(spa_t *spa)
{
	return (spa->spa_ubsync.ub_txg);
}

uint64_t
spa_first_txg(spa_t *spa)
{
	return (spa->spa_first_txg);
}

uint64_t
spa_syncing_txg(spa_t *spa)
{
	return (spa->spa_syncing_txg);
}

pool_state_t
spa_state(spa_t *spa)
{
	return (spa->spa_state);
}

spa_load_state_t
spa_load_state(spa_t *spa)
{
	return (spa->spa_load_state);
}

uint64_t
spa_freeze_txg(spa_t *spa)
{
	return (spa->spa_freeze_txg);
}

/* ARGSUSED */
uint64_t
spa_get_asize(spa_t *spa, uint64_t lsize)
{
	/*
	 * The worst case is single-sector max-parity RAID-Z blocks, in which
	 * case the space requirement is exactly (VDEV_RAIDZ_MAXPARITY + 1)
	 * times the size; so just assume that.  Add to this the fact that
	 * we can have up to 3 DVAs per bp, and one more factor of 2 because
	 * the block may be dittoed with up to 3 DVAs by ddt_sync().
	 */
	return (lsize * (VDEV_RAIDZ_MAXPARITY + 1) * SPA_DVAS_PER_BP * 2);
}

uint64_t
spa_get_dspace(spa_t *spa)
{
	return (spa->spa_dspace);
}

void
spa_update_dspace(spa_t *spa)
{
	spa->spa_dspace = metaslab_class_get_dspace(spa_normal_class(spa)) +
	    ddt_get_dedup_dspace(spa);
}

/*
 * Return the failure mode that has been set to this pool. The default
 * behavior will be to block all I/Os when a complete failure occurs.
 */
uint8_t
spa_get_failmode(spa_t *spa)
{
	return (spa->spa_failmode);
}

boolean_t
spa_suspended(spa_t *spa)
{
	return (spa->spa_suspended);
}

uint64_t
spa_version(spa_t *spa)
{
	return (spa->spa_ubsync.ub_version);
}

boolean_t
spa_deflate(spa_t *spa)
{
	return (spa->spa_deflate);
}

metaslab_class_t *
spa_normal_class(spa_t *spa)
{
	return (spa->spa_normal_class);
}

metaslab_class_t *
spa_log_class(spa_t *spa)
{
	return (spa->spa_log_class);
}

int
spa_max_replication(spa_t *spa)
{
	/*
	 * As of SPA_VERSION == SPA_VERSION_DITTO_BLOCKS, we are able to
	 * handle BPs with more than one DVA allocated.  Set our max
	 * replication level accordingly.
	 */
	if (spa_version(spa) < SPA_VERSION_DITTO_BLOCKS)
		return (1);
	return (MIN(SPA_DVAS_PER_BP, spa_max_replication_override));
}

int
spa_prev_software_version(spa_t *spa)
{
	return (spa->spa_prev_software_version);
}

uint64_t
dva_get_dsize_sync(spa_t *spa, const dva_t *dva)
{
	uint64_t asize = DVA_GET_ASIZE(dva);
	uint64_t dsize = asize;

	ASSERT(spa_config_held(spa, SCL_ALL, RW_READER) != 0);

	if (asize != 0 && spa->spa_deflate) {
		vdev_t *vd = vdev_lookup_top(spa, DVA_GET_VDEV(dva));
		dsize = (asize >> SPA_MINBLOCKSHIFT) * vd->vdev_deflate_ratio;
	}

	return (dsize);
}

uint64_t
bp_get_dsize_sync(spa_t *spa, const blkptr_t *bp)
{
	uint64_t dsize = 0;
	int d;

	for (d = 0; d < SPA_DVAS_PER_BP; d++)
		dsize += dva_get_dsize_sync(spa, &bp->blk_dva[d]);

	return (dsize);
}

uint64_t
bp_get_dsize(spa_t *spa, const blkptr_t *bp)
{
	uint64_t dsize = 0;
	int d;

	spa_config_enter(spa, SCL_VDEV, FTAG, RW_READER);

	for (d = 0; d < SPA_DVAS_PER_BP; d++)
		dsize += dva_get_dsize_sync(spa, &bp->blk_dva[d]);

	spa_config_exit(spa, SCL_VDEV, FTAG);

	return (dsize);
}

/*
 * ==========================================================================
 * Initialization and Termination
 * ==========================================================================
 */

static int
spa_name_compare(const void *a1, const void *a2)
{
	const spa_t *s1 = a1;
	const spa_t *s2 = a2;
	int s;

	s = strcmp(s1->spa_name, s2->spa_name);
	if (s > 0)
		return (1);
	if (s < 0)
		return (-1);
	return (0);
}

void
spa_boot_init(void)
{
	spa_config_load();
}

void
spa_init(int mode)
{
	mutex_init(&spa_namespace_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&spa_spare_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&spa_l2cache_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&spa_namespace_cv, NULL, CV_DEFAULT, NULL);

	avl_create(&spa_namespace_avl, spa_name_compare, sizeof (spa_t),
	    offsetof(spa_t, spa_avl));

	avl_create(&spa_spare_avl, spa_spare_compare, sizeof (spa_aux_t),
	    offsetof(spa_aux_t, aux_avl));

	avl_create(&spa_l2cache_avl, spa_l2cache_compare, sizeof (spa_aux_t),
	    offsetof(spa_aux_t, aux_avl));

	spa_mode_global = mode;

	fm_init();
	refcount_init();
	unique_init();
	zio_init();
	dmu_init();
	zil_init();
	vdev_cache_stat_init();
	zfs_prop_init();
	zpool_prop_init();
	zpool_feature_init();
	spa_config_load();
	l2arc_start();
}

void
spa_fini(void)
{
	l2arc_stop();

	spa_evict_all();

	vdev_cache_stat_fini();
	zil_fini();
	dmu_fini();
	zio_fini();
	unique_fini();
	refcount_fini();
	fm_fini();

	avl_destroy(&spa_namespace_avl);
	avl_destroy(&spa_spare_avl);
	avl_destroy(&spa_l2cache_avl);

	cv_destroy(&spa_namespace_cv);
	mutex_destroy(&spa_namespace_lock);
	mutex_destroy(&spa_spare_lock);
	mutex_destroy(&spa_l2cache_lock);
}

/*
 * Return whether this pool has slogs. No locking needed.
 * It's not a problem if the wrong answer is returned as it's only for
 * performance and not correctness
 */
boolean_t
spa_has_slogs(spa_t *spa)
{
	return (spa->spa_log_class->mc_rotor != NULL);
}

spa_log_state_t
spa_get_log_state(spa_t *spa)
{
	return (spa->spa_log_state);
}

void
spa_set_log_state(spa_t *spa, spa_log_state_t state)
{
	spa->spa_log_state = state;
}

boolean_t
spa_is_root(spa_t *spa)
{
	return (spa->spa_is_root);
}

boolean_t
spa_writeable(spa_t *spa)
{
	return (!!(spa->spa_mode & FWRITE));
}

int
spa_mode(spa_t *spa)
{
	return (spa->spa_mode);
}

uint64_t
spa_bootfs(spa_t *spa)
{
	return (spa->spa_bootfs);
}

uint64_t
spa_delegation(spa_t *spa)
{
	return (spa->spa_delegation);
}

objset_t *
spa_meta_objset(spa_t *spa)
{
	return (spa->spa_meta_objset);
}

enum zio_checksum
spa_dedup_checksum(spa_t *spa)
{
	return (spa->spa_dedup_checksum);
}

/*
 * Reset pool scan stat per scan pass (or reboot).
 */
void
spa_scan_stat_init(spa_t *spa)
{
	/* data not stored on disk */
	spa->spa_scan_pass_start = gethrestime_sec();
	spa->spa_scan_pass_exam = 0;
	vdev_scan_stat_init(spa->spa_root_vdev);
}

/*
 * Get scan stats for zpool status reports
 */
int
spa_scan_get_stats(spa_t *spa, pool_scan_stat_t *ps)
{
	dsl_scan_t *scn = spa->spa_dsl_pool ? spa->spa_dsl_pool->dp_scan : NULL;

	if (scn == NULL || scn->scn_phys.scn_func == POOL_SCAN_NONE)
		return (ENOENT);
	bzero(ps, sizeof (pool_scan_stat_t));

	/* data stored on disk */
	ps->pss_func = scn->scn_phys.scn_func;
	ps->pss_start_time = scn->scn_phys.scn_start_time;
	ps->pss_end_time = scn->scn_phys.scn_end_time;
	ps->pss_to_examine = scn->scn_phys.scn_to_examine;
	ps->pss_examined = scn->scn_phys.scn_examined;
	ps->pss_to_process = scn->scn_phys.scn_to_process;
	ps->pss_processed = scn->scn_phys.scn_processed;
	ps->pss_errors = scn->scn_phys.scn_errors;
	ps->pss_state = scn->scn_phys.scn_state;

	/* data not stored on disk */
	ps->pss_pass_start = spa->spa_scan_pass_start;
	ps->pss_pass_exam = spa->spa_scan_pass_exam;

	return (0);
}

boolean_t
spa_debug_enabled(spa_t *spa)
{
	return (spa->spa_debug);
}

#if defined(_KERNEL) && defined(HAVE_SPL)
/* Namespace manipulation */
EXPORT_SYMBOL(spa_lookup);
EXPORT_SYMBOL(spa_add);
EXPORT_SYMBOL(spa_remove);
EXPORT_SYMBOL(spa_next);

/* Refcount functions */
EXPORT_SYMBOL(spa_open_ref);
EXPORT_SYMBOL(spa_close);
EXPORT_SYMBOL(spa_refcount_zero);

/* Pool configuration lock */
EXPORT_SYMBOL(spa_config_tryenter);
EXPORT_SYMBOL(spa_config_enter);
EXPORT_SYMBOL(spa_config_exit);
EXPORT_SYMBOL(spa_config_held);

/* Pool vdev add/remove lock */
EXPORT_SYMBOL(spa_vdev_enter);
EXPORT_SYMBOL(spa_vdev_exit);

/* Pool vdev state change lock */
EXPORT_SYMBOL(spa_vdev_state_enter);
EXPORT_SYMBOL(spa_vdev_state_exit);

/* Accessor functions */
EXPORT_SYMBOL(spa_shutting_down);
EXPORT_SYMBOL(spa_get_dsl);
EXPORT_SYMBOL(spa_get_rootblkptr);
EXPORT_SYMBOL(spa_set_rootblkptr);
EXPORT_SYMBOL(spa_altroot);
EXPORT_SYMBOL(spa_sync_pass);
EXPORT_SYMBOL(spa_name);
EXPORT_SYMBOL(spa_guid);
EXPORT_SYMBOL(spa_last_synced_txg);
EXPORT_SYMBOL(spa_first_txg);
EXPORT_SYMBOL(spa_syncing_txg);
EXPORT_SYMBOL(spa_version);
EXPORT_SYMBOL(spa_state);
EXPORT_SYMBOL(spa_load_state);
EXPORT_SYMBOL(spa_freeze_txg);
EXPORT_SYMBOL(spa_get_asize);
EXPORT_SYMBOL(spa_get_dspace);
EXPORT_SYMBOL(spa_update_dspace);
EXPORT_SYMBOL(spa_deflate);
EXPORT_SYMBOL(spa_normal_class);
EXPORT_SYMBOL(spa_log_class);
EXPORT_SYMBOL(spa_max_replication);
EXPORT_SYMBOL(spa_prev_software_version);
EXPORT_SYMBOL(spa_get_failmode);
EXPORT_SYMBOL(spa_suspended);
EXPORT_SYMBOL(spa_bootfs);
EXPORT_SYMBOL(spa_delegation);
EXPORT_SYMBOL(spa_meta_objset);

/* Miscellaneous support routines */
EXPORT_SYMBOL(spa_rename);
EXPORT_SYMBOL(spa_guid_exists);
EXPORT_SYMBOL(spa_strdup);
EXPORT_SYMBOL(spa_strfree);
EXPORT_SYMBOL(spa_get_random);
EXPORT_SYMBOL(spa_generate_guid);
EXPORT_SYMBOL(sprintf_blkptr);
EXPORT_SYMBOL(spa_freeze);
EXPORT_SYMBOL(spa_upgrade);
EXPORT_SYMBOL(spa_evict_all);
EXPORT_SYMBOL(spa_lookup_by_guid);
EXPORT_SYMBOL(spa_has_spare);
EXPORT_SYMBOL(dva_get_dsize_sync);
EXPORT_SYMBOL(bp_get_dsize_sync);
EXPORT_SYMBOL(bp_get_dsize);
EXPORT_SYMBOL(spa_has_slogs);
EXPORT_SYMBOL(spa_is_root);
EXPORT_SYMBOL(spa_writeable);
EXPORT_SYMBOL(spa_mode);

EXPORT_SYMBOL(spa_namespace_lock);
#endif
