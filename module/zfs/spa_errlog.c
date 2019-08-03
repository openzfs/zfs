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
 * Copyright (c) 2006, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2013, 2014 by Delphix. All rights reserved.
 */

/*
 * Routines to manage the on-disk persistent error log.
 *
 * Each pool stores a log of all logical data errors seen during normal
 * operation.  This is actually the union of two distinct logs: the last log,
 * and the current log.  All errors seen are logged to the current log.  When a
 * scrub completes, the current log becomes the last log, the last log is thrown
 * out, and the current log is reinitialized.  This way, if an error is somehow
 * corrected, a new scrub will show that that it no longer exists, and will be
 * deleted from the log when the scrub completes.
 *
 * The log is stored using a ZAP object whose key is a string form of the
 * zbookmark_phys tuple (objset, object, level, blkid), and whose contents is an
 * optional 'objset:object' human-readable string describing the data.  When an
 * error is first logged, this string will be empty, indicating that no name is
 * known.  This prevents us from having to issue a potentially large amount of
 * I/O to discover the object name during an error path.  Instead, we do the
 * calculation when the data is requested, storing the result so future queries
 * will be faster.
 *
 * This log is then shipped into an nvlist where the key is the dataset name and
 * the value is the object name.  Userland is then responsible for uniquifying
 * this list and displaying it to the user.
 */

#include <sys/dmu_tx.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/zap.h>
#include <sys/zio.h>
#include <sys/dsl_dir.h>
#include <sys/dmu_objset.h>
#include <sys/dbuf.h>

#ifdef _KERNEL
static int check_filesystem(spa_t *spa, uint64_t head_ds,
    zbookmark_err_phys_t *zep, uint64_t *count, void *addr,
    boolean_t only_count);
static uint64_t get_errlog_size(spa_t *spa, uint64_t spa_err_obj);
static uint64_t get_errlist_size(spa_t *spa, avl_tree_t *tree);
#endif

/*
 * Convert a bookmark to a string.
 */
static void
bookmark_to_name(zbookmark_phys_t *zb, char *buf, size_t len)
{
	(void) snprintf(buf, len, "%llx:%llx:%llx:%llx",
	    (u_longlong_t)zb->zb_objset, (u_longlong_t)zb->zb_object,
	    (u_longlong_t)zb->zb_level, (u_longlong_t)zb->zb_blkid);
}

/*
 * Convert a err_phys to a string.
 */
static void
errphys_to_name(zbookmark_err_phys_t *zep, char *buf, size_t len)
{
	(void) snprintf(buf, len, "%llx:%llx:%llx:%llx",
	    (u_longlong_t)zep->zb_object, (u_longlong_t)zep->zb_level,
	    (u_longlong_t)zep->zb_blkid, (u_longlong_t)zep->zb_birth);
}

#ifdef _KERNEL
/*
 * Convert a string to a bookmark.
 */
static void
name_to_bookmark(char *buf, zbookmark_phys_t *zb)
{
	zb->zb_objset = zfs_strtonum(buf, &buf);
	ASSERT(*buf == ':');
	zb->zb_object = zfs_strtonum(buf + 1, &buf);
	ASSERT(*buf == ':');
	zb->zb_level = (int)zfs_strtonum(buf + 1, &buf);
	ASSERT(*buf == ':');
	zb->zb_blkid = zfs_strtonum(buf + 1, &buf);
	ASSERT(*buf == '\0');
}

/*
 * Convert a string to a err_phys.
 */
static void
name_to_errphys(char *buf, zbookmark_err_phys_t *zep)
{
	zep->zb_object = zfs_strtonum(buf, &buf);
	ASSERT(*buf == ':');
	zep->zb_level = (int)zfs_strtonum(buf + 1, &buf);
	ASSERT(*buf == ':');
	zep->zb_blkid = zfs_strtonum(buf + 1, &buf);
	ASSERT(*buf == ':');
	zep->zb_birth = zfs_strtonum(buf + 1, &buf);
	ASSERT(*buf == '\0');
}

static void
zeb_to_zb(uint64_t dataset, zbookmark_err_phys_t *zep, zbookmark_phys_t *zb)
{
	zb->zb_objset = dataset;
	zb->zb_object = zep->zb_object;
	zb->zb_level = zep->zb_level;
	zb->zb_blkid = zep->zb_blkid;
}
#endif

static void
name_to_object(char *buf, uint64_t *obj)
{
	*obj = zfs_strtonum(buf, &buf);
	ASSERT(*buf == '\0');
}

static int
get_head_and_birth_txg(spa_t *spa, zbookmark_err_phys_t *zep, uint64_t ds_obj,
    uint64_t *head_dataset_id)
{
	dsl_pool_t *dp = spa->spa_dsl_pool;
	dsl_dataset_t *ds;
	objset_t *os;

	dsl_pool_config_enter(dp, FTAG);
	int err = dsl_dataset_hold_obj(dp, ds_obj, FTAG, &ds);
	if (err != 0) {
		dsl_pool_config_exit(dp, FTAG);
		return (EFAULT);
	}
	*head_dataset_id = dsl_dir_phys(ds->ds_dir)->dd_head_dataset_obj;

	if (dmu_objset_from_ds(ds, &os) != 0) {
		dsl_dataset_rele(ds, FTAG);
		dsl_pool_config_exit(dp, FTAG);
		return (EFAULT);
	}

	dnode_t *dn;
	blkptr_t bp;

	if (dnode_hold(os, zep->zb_object, FTAG, &dn) != 0) {
		dsl_dataset_rele(ds, FTAG);
		dsl_pool_config_exit(dp, FTAG);
		return (EFAULT);
	}

	rw_enter(&dn->dn_struct_rwlock, RW_READER);
	err = dbuf_dnode_findbp(dn, zep->zb_level, zep->zb_blkid, &bp, NULL,
	    NULL);

	if (err != 0 || BP_IS_HOLE(&bp)) {
		rw_exit(&dn->dn_struct_rwlock);
		dnode_rele(dn, FTAG);
		dsl_dataset_rele(ds, FTAG);
		dsl_pool_config_exit(dp, FTAG);
		return (EFAULT);
	}

	zep->zb_birth = bp.blk_birth;
	rw_exit(&dn->dn_struct_rwlock);
	dnode_rele(dn, FTAG);
	dsl_dataset_rele(ds, FTAG);
	dsl_pool_config_exit(dp, FTAG);
	return (0);
}

/*
 * Log an uncorrectable error to the persistent error log.  We add it to the
 * spa's list of pending errors.  The changes are actually synced out to disk
 * during spa_errlog_sync().
 */
void
spa_log_error(spa_t *spa, const zbookmark_phys_t *zb)
{
	spa_error_entry_t search;
	spa_error_entry_t *new;
	avl_tree_t *tree;
	avl_index_t where;

	/*
	 * If we are trying to import a pool, ignore any errors, as we won't be
	 * writing to the pool any time soon.
	 */
	if (spa_load_state(spa) == SPA_LOAD_TRYIMPORT)
		return;

	mutex_enter(&spa->spa_errlist_lock);

	/*
	 * If we have had a request to rotate the log, log it to the next list
	 * instead of the current one.
	 */
	if (spa->spa_scrub_active || spa->spa_scrub_finished)
		tree = &spa->spa_errlist_scrub;
	else
		tree = &spa->spa_errlist_last;

	search.se_bookmark = *zb;
	if (avl_find(tree, &search, &where) != NULL) {
		mutex_exit(&spa->spa_errlist_lock);
		return;
	}

	new = kmem_zalloc(sizeof (spa_error_entry_t), KM_SLEEP);
	new->se_bookmark = *zb;
	avl_insert(tree, new, where);

	mutex_exit(&spa->spa_errlist_lock);
}

/*
 * Return the number of errors currently in the error log.  This is actually the
 * sum of both the last log and the current log, since we don't know the union
 * of these logs until we reach userland.
 */
uint64_t
spa_get_errlog_size(spa_t *spa)
{
	uint64_t total = 0;
	if (!spa_feature_is_enabled(spa, SPA_FEATURE_HEAD_ERRLOG)) {
		uint64_t count;
		mutex_enter(&spa->spa_errlog_lock);
		if (spa->spa_errlog_scrub != 0 &&
		    zap_count(spa->spa_meta_objset, spa->spa_errlog_scrub,
		    &count) == 0)
			total += count;

		if (spa->spa_errlog_last != 0 && !spa->spa_scrub_finished &&
		    zap_count(spa->spa_meta_objset, spa->spa_errlog_last,
		    &count) == 0)
			total += count;
		mutex_exit(&spa->spa_errlog_lock);

		mutex_enter(&spa->spa_errlist_lock);
		total += avl_numnodes(&spa->spa_errlist_last);
		total += avl_numnodes(&spa->spa_errlist_scrub);
		mutex_exit(&spa->spa_errlist_lock);

	} else {
#ifdef _KERNEL
		mutex_enter(&spa->spa_errlog_lock);
		total += get_errlog_size(spa, spa->spa_errlog_scrub);
		total += get_errlog_size(spa, spa->spa_errlog_last);
		mutex_exit(&spa->spa_errlog_lock);

		mutex_enter(&spa->spa_errlist_lock);
		total += get_errlist_size(spa, &spa->spa_errlist_last);
		total += get_errlist_size(spa, &spa->spa_errlist_scrub);
		mutex_exit(&spa->spa_errlist_lock);
#endif
	}
	return (total);
}

#ifdef _KERNEL
static int
find_block_txg(dsl_dataset_t *ds, zbookmark_err_phys_t *zep,
    uint64_t *birth_txg)
{
	objset_t *os;
	if (dmu_objset_from_ds(ds, &os) != 0) {
		return (ENOENT);
	}

	dnode_t *dn;
	blkptr_t bp;

	if (dnode_hold(os, zep->zb_object, FTAG, &dn) != 0) {
		return (ENOENT);
	}

	rw_enter(&dn->dn_struct_rwlock, RW_READER);
	int err = dbuf_dnode_findbp(dn, zep->zb_level, zep->zb_blkid, &bp, NULL,
	    NULL);

	if (err != 0 || BP_IS_HOLE(&bp)) {
		rw_exit(&dn->dn_struct_rwlock);
		dnode_rele(dn, FTAG);
		return (ENOENT);
	}

	*birth_txg = bp.blk_birth;
	rw_exit(&dn->dn_struct_rwlock);
	dnode_rele(dn, FTAG);
	return (0);
}

static int
check_clones(spa_t *spa, uint64_t *snapshot, uint64_t snapshot_count,
    uint64_t zap_clone, zbookmark_err_phys_t *zep, uint64_t *count,
    void *addr, boolean_t only_count)
{
	zap_cursor_t zc;
	zap_attribute_t za;

	for (zap_cursor_init(&zc, spa->spa_meta_objset, zap_clone);
	    zap_cursor_retrieve(&zc, &za) == 0;
	    zap_cursor_advance(&zc)) {

		dsl_pool_t *dp = spa->spa_dsl_pool;
		dsl_dataset_t *clone;
		int err = dsl_dataset_hold_obj(dp, za.za_first_integer, FTAG,
		    &clone);

		if (err != 0) {
			zap_cursor_fini(&zc);
			return (EFAULT);
		}

		boolean_t found = B_FALSE;
		for (int i = 0; i < snapshot_count; i++) {
			if (dsl_dir_phys(clone->ds_dir)->dd_origin_obj
			    == snapshot[i]) {
				found = B_TRUE;
			}
		}
		dsl_dataset_rele(clone, FTAG);

		if (!found) {
			continue;
		}

		check_filesystem(spa, za.za_first_integer, zep, count, addr,
		    only_count);
	}
	zap_cursor_fini(&zc);
	return (0);
}

static int
check_filesystem(spa_t *spa, uint64_t fs, zbookmark_err_phys_t *zep,
    uint64_t *count, void *addr, boolean_t only_count)
{
	dsl_dataset_t *ds;
	dsl_pool_t *dp = spa->spa_dsl_pool;

	if (dsl_dataset_hold_obj(dp, fs, FTAG, &ds) != 0)
		return (EFAULT);

	uint64_t latest_txg;
	uint64_t txg_to_consider = spa->spa_syncing_txg;
	boolean_t check_snapshot = B_TRUE;
	if (find_block_txg(ds, zep, &latest_txg) == 0) {
		if (zep->zb_birth < latest_txg) {
			txg_to_consider = latest_txg;
		} else {
			/* Block neither free nor re written. */
			if (!only_count) {
				zbookmark_phys_t zb;
				zeb_to_zb(fs, zep, &zb);
				if (copyout(&zb, (char *)addr + (*count - 1)
				    * sizeof (zbookmark_phys_t),
				    sizeof (zbookmark_phys_t)) != 0) {
					dsl_dataset_rele(ds, FTAG);
					return (SET_ERROR(EFAULT));
				}
				(*count)--;
			} else {
				(*count)++;
			}
			check_snapshot = B_FALSE;
		}
	}

	uint64_t snap_count;
	if (zap_count(spa->spa_meta_objset,
	    dsl_dataset_phys(ds)->ds_snapnames_zapobj, &snap_count) != 0) {
		dsl_dataset_rele(ds, FTAG);
		return (EFAULT);
	}

	if (snap_count == 0) {
		/* File system has no snapshot. */
		dsl_dataset_rele(ds, FTAG);
		return (0);
	}

	uint64_t *snap_obj_array = kmem_alloc(snap_count * sizeof (uint64_t),
	    KM_SLEEP);

	int aff_snap_count = 0;
	uint64_t snap_obj = dsl_dataset_phys(ds)->ds_prev_snap_obj;
	uint64_t snap_obj_txg = dsl_dataset_phys(ds)->ds_prev_snap_txg;
	uint64_t zap_clone = dsl_dir_phys(ds->ds_dir)->dd_clones;

	/* Check only snapshots created from this file system. */
	while (snap_obj != 0 && zep->zb_birth < snap_obj_txg &&
	    snap_obj_txg <= txg_to_consider) {

		dsl_dataset_rele(ds, FTAG);
		if (dsl_dataset_hold_obj(dp, snap_obj, FTAG, &ds) != 0)
			return (SET_ERROR(EFAULT));

		if (dsl_dir_phys(ds->ds_dir)->dd_head_dataset_obj != fs) {
			break;
		}

		boolean_t affected = B_TRUE;
		if (check_snapshot) {
			affected = B_FALSE;
			uint64_t blk_txg;
			if (find_block_txg(ds, zep, &blk_txg) == 0 &&
			    zep->zb_birth == blk_txg) {
				affected = B_TRUE;
			}
		}

		if (affected) {
			snap_obj_array[aff_snap_count] = snap_obj;
			aff_snap_count++;

			if (!only_count) {
				zbookmark_phys_t zb;
				zeb_to_zb(snap_obj, zep, &zb);
				if (copyout(&zb, (char *)addr + (*count - 1)
				    * sizeof (zbookmark_phys_t),
				    sizeof (zbookmark_phys_t)) != 0) {
					goto out;
				}
				(*count)--;
			} else {
				(*count)++;
			}
		}
		snap_obj_txg = dsl_dataset_phys(ds)->ds_prev_snap_txg;
		snap_obj = dsl_dataset_phys(ds)->ds_prev_snap_obj;
	}
	dsl_dataset_rele(ds, FTAG);
	if (zap_clone != 0 && aff_snap_count > 0) {
		check_clones(spa, snap_obj_array, aff_snap_count, zap_clone,
		    zep, count, addr, only_count);
	}
	kmem_free(snap_obj_array, sizeof (*snap_obj_array));
	return (0);
out:
	dsl_dataset_rele(ds, FTAG);
	kmem_free(snap_obj_array, sizeof (*snap_obj_array));
	return (SET_ERROR(EFAULT));
}

static uint64_t
find_top_affected_fs(spa_t *spa, uint64_t head_ds, zbookmark_err_phys_t *zep)
{
	dsl_dataset_t *ds;
	dsl_pool_t *dp = spa->spa_dsl_pool;

	if (dsl_dataset_hold_obj(dp, head_ds, FTAG, &ds) != 0)
		return (EFAULT);

	uint64_t snap_obj = dsl_dataset_phys(ds)->ds_prev_snap_obj;
	uint64_t snap_obj_txg = dsl_dataset_phys(ds)->ds_prev_snap_txg;
	uint64_t top_affected_fs = head_ds;

	while (snap_obj != 0 && zep->zb_birth < snap_obj_txg) {
		dsl_dataset_rele(ds, FTAG);
		if (dsl_dataset_hold_obj(dp, snap_obj, FTAG, &ds) != 0)
			break;
		snap_obj_txg = dsl_dataset_phys(ds)->ds_prev_snap_txg;
		snap_obj = dsl_dataset_phys(ds)->ds_prev_snap_obj;
		top_affected_fs = dsl_dir_phys(ds->ds_dir)->dd_head_dataset_obj;
	}
	dsl_dataset_rele(ds, FTAG);

	return (top_affected_fs);
}

static int
process_error_block(spa_t *spa, uint64_t head_ds, zbookmark_err_phys_t *zep,
    uint64_t *count, void *addr, boolean_t only_count)
{
	dsl_pool_t *dp = spa->spa_dsl_pool;
	dsl_pool_config_enter(dp, FTAG);
	uint64_t top_affected_fs = find_top_affected_fs(spa, head_ds, zep);
	int error = check_filesystem(spa, top_affected_fs, zep, count, addr,
	    only_count);
	dsl_pool_config_exit(dp, FTAG);
	return (error);
}

static uint64_t
get_errlog_size(spa_t *spa, uint64_t spa_err_obj)
{
	uint64_t total = 0;
	if (spa_err_obj == 0)
		return (total);

	zap_cursor_t zc;
	zap_attribute_t za;
	for (zap_cursor_init(&zc, spa->spa_meta_objset, spa_err_obj);
	    zap_cursor_retrieve(&zc, &za) == 0; zap_cursor_advance(&zc)) {

		zap_cursor_t head_ds_cursor;
		zap_attribute_t head_ds_attr;
		zbookmark_err_phys_t head_ds_block;

		uint64_t head_ds;
		name_to_object(za.za_name, &head_ds);

		for (zap_cursor_init(&head_ds_cursor, spa->spa_meta_objset,
		    za.za_first_integer); zap_cursor_retrieve(&head_ds_cursor,
		    &head_ds_attr) == 0; zap_cursor_advance(&head_ds_cursor)) {

			name_to_errphys(head_ds_attr.za_name, &head_ds_block);
			if (process_error_block(spa, head_ds, &head_ds_block,
			    &total, NULL, B_TRUE) != 0) {
				zap_cursor_fini(&head_ds_cursor);
				zap_cursor_fini(&zc);
				return (total);
			}
		}
		zap_cursor_fini(&head_ds_cursor);
	}
	zap_cursor_fini(&zc);
	return (total);
}

static uint64_t
get_errlist_size(spa_t *spa, avl_tree_t *tree)
{
	uint64_t total = 0;

	if (avl_numnodes(tree) == 0)
		return (total);

	spa_error_entry_t *se;
	for (se = avl_first(tree); se != NULL; se = AVL_NEXT(tree, se)) {
		zbookmark_err_phys_t zep;
		zep.zb_object = se->se_bookmark.zb_object;
		zep.zb_level = se->se_bookmark.zb_level;
		zep.zb_blkid = se->se_bookmark.zb_blkid;
		uint64_t head_ds_obj;
		get_head_and_birth_txg(spa, &zep, se->se_bookmark.zb_objset,
		    &head_ds_obj);

		if (process_error_block(spa, head_ds_obj, &zep, &total, NULL,
		    B_TRUE) != 0) {
			return (SET_ERROR(EFAULT));
		}
	}
	return (total);
}
#endif

/*
 * If an error block is shared by two dataset it would be counted twice. For
 * detailed message see spa_get_errlog_size() above.
 */

#ifdef _KERNEL
static int
process_error_log(spa_t *spa, uint64_t obj, void *addr, uint64_t *count)
{
	zap_cursor_t zc;
	zap_attribute_t za;

	if (obj == 0)
		return (0);

	if (!spa_feature_is_enabled(spa, SPA_FEATURE_HEAD_ERRLOG)) {
		for (zap_cursor_init(&zc, spa->spa_meta_objset, obj);
		    zap_cursor_retrieve(&zc, &za) == 0;
		    zap_cursor_advance(&zc)) {
			if (*count == 0) {
					zap_cursor_fini(&zc);
					return (SET_ERROR(ENOMEM));
				}

				zbookmark_phys_t zb;
				name_to_bookmark(za.za_name, &zb);

				if (copyout(&zb, (char *)addr +
				    (*count - 1) * sizeof (zbookmark_phys_t),
				    sizeof (zbookmark_phys_t)) != 0) {
					zap_cursor_fini(&zc);
					return (SET_ERROR(EFAULT));
				}
				*count -= 1;

		}
		zap_cursor_fini(&zc);
		return (0);
	}

	for (zap_cursor_init(&zc, spa->spa_meta_objset, obj);
	    zap_cursor_retrieve(&zc, &za) == 0;
	    zap_cursor_advance(&zc)) {

		zap_cursor_t head_ds_cursor;
		zap_attribute_t head_ds_attr;
		zbookmark_err_phys_t head_ds_block;

		uint64_t head_ds_err_obj = za.za_first_integer;
		uint64_t head_ds;
		name_to_object(za.za_name, &head_ds);
		int err;
		for (zap_cursor_init(&head_ds_cursor, spa->spa_meta_objset,
		    head_ds_err_obj); zap_cursor_retrieve(&head_ds_cursor,
		    &head_ds_attr) == 0; zap_cursor_advance(&head_ds_cursor)) {

			name_to_errphys(head_ds_attr.za_name, &head_ds_block);
			err = process_error_block(spa, head_ds, &head_ds_block,
			    count, addr, B_FALSE);

			if (err != 0) {
				zap_cursor_fini(&head_ds_cursor);
				zap_cursor_fini(&zc);
				return (SET_ERROR(EFAULT));
			}
		}
		zap_cursor_fini(&head_ds_cursor);
	}
	zap_cursor_fini(&zc);
	return (0);
}

static int
process_error_list(spa_t *spa, avl_tree_t *list, void *addr, uint64_t *count)
{
	spa_error_entry_t *se;

	if (!spa_feature_is_enabled(spa, SPA_FEATURE_HEAD_ERRLOG)) {
		for (se = avl_first(list); se != NULL;
		    se = AVL_NEXT(list, se)) {

			if (*count == 0)
				return (SET_ERROR(ENOMEM));

			if (copyout(&se->se_bookmark, (char *)addr +
			    (*count - 1) * sizeof (zbookmark_phys_t),
			    sizeof (zbookmark_phys_t)) != 0)
				return (SET_ERROR(EFAULT));

			*count -= 1;
		}
		return (0);
	}

	for (se = avl_first(list); se != NULL; se = AVL_NEXT(list, se)) {
		zbookmark_err_phys_t zep;
		zep.zb_object = se->se_bookmark.zb_object;
		zep.zb_level = se->se_bookmark.zb_level;
		zep.zb_blkid = se->se_bookmark.zb_blkid;
		uint64_t head_ds_obj;
		get_head_and_birth_txg(spa, &zep,
		    se->se_bookmark.zb_objset, &head_ds_obj);

		if (process_error_block(spa, head_ds_obj, &zep,
		    count, addr, B_FALSE) != 0) {
			return (SET_ERROR(EFAULT));
		}

	}
	return (0);
}
#endif

/*
 * Copy all known errors to userland as an array of bookmarks.  This is
 * actually a union of the on-disk last log and current log, as well as any
 * pending error requests.
 *
 * Because the act of reading the on-disk log could cause errors to be
 * generated, we have two separate locks: one for the error log and one for the
 * in-core error lists.  We only need the error list lock to log and error, so
 * we grab the error log lock while we read the on-disk logs, and only pick up
 * the error list lock when we are finished.
 */
int
spa_get_errlog(spa_t *spa, void *uaddr, uint64_t *count)
{
	int ret = 0;

#ifdef _KERNEL
	mutex_enter(&spa->spa_errlog_lock);

	ret = process_error_log(spa, spa->spa_errlog_scrub, uaddr, count);

	if (!ret && !spa->spa_scrub_finished)
		ret = process_error_log(spa, spa->spa_errlog_last, uaddr,
		    count);

	mutex_enter(&spa->spa_errlist_lock);
	if (!ret)
		ret = process_error_list(spa, &spa->spa_errlist_scrub, uaddr,
		    count);
	if (!ret)
		ret = process_error_list(spa, &spa->spa_errlist_last, uaddr,
		    count);
	mutex_exit(&spa->spa_errlist_lock);

	mutex_exit(&spa->spa_errlog_lock);
#endif

	return (ret);
}

/*
 * Called when a scrub completes.  This simply set a bit which tells which AVL
 * tree to add new errors.  spa_errlog_sync() is responsible for actually
 * syncing the changes to the underlying objects.
 */
void
spa_errlog_rotate(spa_t *spa)
{
	mutex_enter(&spa->spa_errlist_lock);
	spa->spa_scrub_finished = B_TRUE;
	mutex_exit(&spa->spa_errlist_lock);
}

/*
 * Discard any pending errors from the spa_t.  Called when unloading a faulted
 * pool, as the errors encountered during the open cannot be synced to disk.
 */
void
spa_errlog_drain(spa_t *spa)
{
	spa_error_entry_t *se;
	void *cookie;

	mutex_enter(&spa->spa_errlist_lock);

	cookie = NULL;
	while ((se = avl_destroy_nodes(&spa->spa_errlist_last,
	    &cookie)) != NULL)
		kmem_free(se, sizeof (spa_error_entry_t));
	cookie = NULL;
	while ((se = avl_destroy_nodes(&spa->spa_errlist_scrub,
	    &cookie)) != NULL)
		kmem_free(se, sizeof (spa_error_entry_t));

	mutex_exit(&spa->spa_errlist_lock);
}

/*
 * Process a list of errors into the current on-disk log.
 */
static void
sync_error_list(spa_t *spa, avl_tree_t *t, uint64_t *obj, dmu_tx_t *tx)
{
	spa_error_entry_t *se;
	char buf[64];
	void *cookie;

	if (avl_numnodes(t) == 0) {
		return;
	}

	/* create log if necessary */
	if (*obj == 0)
		*obj = zap_create(spa->spa_meta_objset, DMU_OT_ERROR_LOG,
		    DMU_OT_NONE, 0, tx);

	/* add errors to the current log */
	if (!spa_feature_is_enabled(spa, SPA_FEATURE_HEAD_ERRLOG)) {
		for (se = avl_first(t); se != NULL; se = AVL_NEXT(t, se)) {
			char *name = se->se_name ? se->se_name : "";

			bookmark_to_name(&se->se_bookmark, buf, sizeof (buf));

			(void) zap_update(spa->spa_meta_objset, *obj, buf, 1,
			    strlen(name) + 1, name, tx);
		}
	} else {
		for (se = avl_first(t); se != NULL;
		    se = AVL_NEXT(t, se)) {
			char *name = se->se_name ? se->se_name : "";

			zbookmark_err_phys_t zep;
			zep.zb_object = se->se_bookmark.zb_object;
			zep.zb_level = se->se_bookmark.zb_level;
			zep.zb_blkid = se->se_bookmark.zb_blkid;

			uint64_t head_dataset_obj;
			get_head_and_birth_txg(spa, &zep,
			    se->se_bookmark.zb_objset, &head_dataset_obj);
			uint64_t err_obj;
			int error = zap_lookup_int_key(spa->spa_meta_objset,
			    *obj, head_dataset_obj, &err_obj);

			if (error != 0) {
				err_obj = zap_create(spa->spa_meta_objset,
				    DMU_OT_ERROR_LOG, DMU_OT_NONE, 0, tx);

				(void) zap_update_int_key(spa->spa_meta_objset,
				    *obj, head_dataset_obj, err_obj, tx);
			}
			errphys_to_name(&zep, buf, sizeof (buf));

			(void) zap_update(spa->spa_meta_objset,
			    err_obj, buf, 1, strlen(name) + 1, name, tx);
		}
	}
	/* purge the error list */
	cookie = NULL;
	while ((se = avl_destroy_nodes(t, &cookie)) != NULL)
		kmem_free(se, sizeof (spa_error_entry_t));
}

static void
delete_errlog(spa_t *spa, uint64_t spa_err_obj, dmu_tx_t *tx)
{
	if (spa_feature_is_enabled(spa, SPA_FEATURE_HEAD_ERRLOG)) {
		zap_cursor_t zc;
		zap_attribute_t za;
		for (zap_cursor_init(&zc, spa->spa_meta_objset, spa_err_obj);
		    zap_cursor_retrieve(&zc, &za) == 0;
		    zap_cursor_advance(&zc)) {
			VERIFY(dmu_object_free(spa->spa_meta_objset,
			    za.za_first_integer, tx) == 0);
		}
		zap_cursor_fini(&zc);
	}
	VERIFY(dmu_object_free(spa->spa_meta_objset, spa_err_obj, tx) == 0);
}

/*
 * Sync the error log out to disk.  This is a little tricky because the act of
 * writing the error log requires the spa_errlist_lock.  So, we need to lock the
 * error lists, take a copy of the lists, and then reinitialize them.  Then, we
 * drop the error list lock and take the error log lock, at which point we
 * do the errlog processing.  Then, if we encounter an I/O error during this
 * process, we can successfully add the error to the list.  Note that this will
 * result in the perpetual recycling of errors, but it is an unlikely situation
 * and not a performance critical operation.
 */
void
spa_errlog_sync(spa_t *spa, uint64_t txg)
{
	dmu_tx_t *tx;
	avl_tree_t scrub, last;
	int scrub_finished;

	mutex_enter(&spa->spa_errlist_lock);

	/*
	 * Bail out early under normal circumstances.
	 */
	if (avl_numnodes(&spa->spa_errlist_scrub) == 0 &&
	    avl_numnodes(&spa->spa_errlist_last) == 0 &&
	    !spa->spa_scrub_finished) {
		mutex_exit(&spa->spa_errlist_lock);
		return;
	}

	spa_get_errlists(spa, &last, &scrub);
	scrub_finished = spa->spa_scrub_finished;
	spa->spa_scrub_finished = B_FALSE;

	mutex_exit(&spa->spa_errlist_lock);
	mutex_enter(&spa->spa_errlog_lock);

	tx = dmu_tx_create_assigned(spa->spa_dsl_pool, txg);

	/*
	 * Sync out the current list of errors.
	 */
	sync_error_list(spa, &last, &spa->spa_errlog_last, tx);

	/*
	 * Rotate the log if necessary.
	 */
	if (scrub_finished) {
		if (spa->spa_errlog_last != 0)
			delete_errlog(spa, spa->spa_errlog_last, tx);
		spa->spa_errlog_last = spa->spa_errlog_scrub;
		spa->spa_errlog_scrub = 0;

		sync_error_list(spa, &scrub, &spa->spa_errlog_last, tx);
	}

	/*
	 * Sync out any pending scrub errors.
	 */
	sync_error_list(spa, &scrub, &spa->spa_errlog_scrub, tx);

	/*
	 * Update the MOS to reflect the new values.
	 */
	(void) zap_update(spa->spa_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_ERRLOG_LAST, sizeof (uint64_t), 1,
	    &spa->spa_errlog_last, tx);
	(void) zap_update(spa->spa_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_ERRLOG_SCRUB, sizeof (uint64_t), 1,
	    &spa->spa_errlog_scrub, tx);

	dmu_tx_commit(tx);

	mutex_exit(&spa->spa_errlog_lock);
}

static void
delete_dataset_errlog(spa_t *spa, uint64_t spa_err_obj, uint64_t ds,
    dmu_tx_t *tx)
{
	if (spa_err_obj == 0)
		return;

	zap_cursor_t zc;
	zap_attribute_t za;
	for (zap_cursor_init(&zc, spa->spa_meta_objset, spa_err_obj);
	    zap_cursor_retrieve(&zc, &za) == 0; zap_cursor_advance(&zc)) {
		uint64_t head_ds;
		name_to_object(za.za_name, &head_ds);
		if (head_ds == ds) {
			(void) zap_remove(spa->spa_meta_objset, spa_err_obj,
			    za.za_name, tx);
			VERIFY(dmu_object_free(spa->spa_meta_objset,
			    za.za_first_integer, tx) == 0);
			break;
		}
	}
	zap_cursor_fini(&zc);
}

void
spa_delete_dataset_errlog(spa_t *spa, uint64_t ds, dmu_tx_t *tx)
{
	mutex_enter(&spa->spa_errlog_lock);
	delete_dataset_errlog(spa, spa->spa_errlog_scrub, ds, tx);
	delete_dataset_errlog(spa, spa->spa_errlog_last, ds, tx);
	mutex_exit(&spa->spa_errlog_lock);
}

#if defined(_KERNEL)
static uint64_t
find_txg_ancestor_snapshot(spa_t *spa, uint64_t new_head, uint64_t old_head)
{
	dsl_dataset_t *ds;
	dsl_pool_t *dp = spa->spa_dsl_pool;

	if (dsl_dataset_hold_obj(dp, old_head, FTAG, &ds) != 0)
		return (EFAULT);

	uint64_t snap_obj = dsl_dataset_phys(ds)->ds_prev_snap_obj;
	uint64_t snap_obj_txg = dsl_dataset_phys(ds)->ds_prev_snap_txg;

	while (snap_obj != 0) {
		dsl_dataset_rele(ds, FTAG);
		if (dsl_dataset_hold_obj(dp, snap_obj, FTAG, &ds) != 0)
			continue;
		if (dsl_dir_phys(ds->ds_dir)->dd_head_dataset_obj
		    == new_head) {
			break;
		}

		snap_obj_txg = dsl_dataset_phys(ds)->ds_prev_snap_txg;
		snap_obj = dsl_dataset_phys(ds)->ds_prev_snap_obj;
	}
	dsl_dataset_rele(ds, FTAG);
	ASSERT(snap_obj != 0);
	return (snap_obj_txg);
}

static void
swap_errlog(spa_t *spa, uint64_t spa_err_obj, uint64_t new_head, uint64_t
    old_head, dmu_tx_t *tx)
{
	if (spa_err_obj == 0)
		return;

	uint64_t old_head_errlog;
	int error = zap_lookup_int_key(spa->spa_meta_objset, spa_err_obj,
	    old_head, &old_head_errlog);

	/* Check if file system being depromoted has errlog. */
	if (error != 0)
		return;

	uint64_t txg = find_txg_ancestor_snapshot(spa, new_head, old_head);

	/*
	 * Check if file system being promoted already has errlog otherwise
	 * create zap object.
	 */
	uint64_t new_head_errlog;
	error = zap_lookup_int_key(spa->spa_meta_objset, spa_err_obj, new_head,
	    &new_head_errlog);

	if (error != 0) {
		new_head_errlog = zap_create(spa->spa_meta_objset,
		    DMU_OT_ERROR_LOG, DMU_OT_NONE, 0, tx);

		(void) zap_update_int_key(spa->spa_meta_objset, spa_err_obj,
		    new_head, new_head_errlog, tx);
	}

	zap_cursor_t zc;
	zap_attribute_t za;
	zbookmark_err_phys_t err_block;
	for (zap_cursor_init(&zc, spa->spa_meta_objset, old_head_errlog);
	    zap_cursor_retrieve(&zc, &za) == 0; zap_cursor_advance(&zc)) {

		char *name = "";
		name_to_errphys(za.za_name, &err_block);
		if (err_block.zb_birth < txg) {
			(void) zap_update(spa->spa_meta_objset, new_head_errlog,
			    za.za_name, 1, strlen(name) + 1, name, tx);

			(void) zap_remove(spa->spa_meta_objset, old_head_errlog,
			    za.za_name, tx);
		}
	}
	zap_cursor_fini(&zc);
}
#endif

void
spa_swap_errlog(spa_t *spa, uint64_t new_head_ds, uint64_t old_head_ds,
    dmu_tx_t *tx)
{
	mutex_enter(&spa->spa_errlog_lock);
#if defined(_KERNEL)
	swap_errlog(spa, spa->spa_errlog_scrub, new_head_ds, old_head_ds, tx);
	swap_errlog(spa, spa->spa_errlog_last, new_head_ds, old_head_ds, tx);
#endif
	mutex_exit(&spa->spa_errlog_lock);
}

#if defined(_KERNEL)
/* error handling */
EXPORT_SYMBOL(spa_log_error);
EXPORT_SYMBOL(spa_get_errlog_size);
EXPORT_SYMBOL(spa_get_errlog);
EXPORT_SYMBOL(spa_errlog_rotate);
EXPORT_SYMBOL(spa_errlog_drain);
EXPORT_SYMBOL(spa_errlog_sync);
EXPORT_SYMBOL(spa_get_errlists);
EXPORT_SYMBOL(spa_delete_dataset_errlog);
EXPORT_SYMBOL(spa_swap_errlog);
#endif
