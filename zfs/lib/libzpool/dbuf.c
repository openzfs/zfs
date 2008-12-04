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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/zfs_context.h>
#include <sys/dmu.h>
#include <sys/dmu_impl.h>
#include <sys/dbuf.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/dmu_tx.h>
#include <sys/spa.h>
#include <sys/zio.h>
#include <sys/dmu_zfetch.h>

static void dbuf_destroy(dmu_buf_impl_t *db);
static int dbuf_undirty(dmu_buf_impl_t *db, dmu_tx_t *tx);
static void dbuf_write(dbuf_dirty_record_t *dr, arc_buf_t *data, dmu_tx_t *tx);
static arc_done_func_t dbuf_write_ready;
static arc_done_func_t dbuf_write_done;
static zio_done_func_t dbuf_skip_write_ready;
static zio_done_func_t dbuf_skip_write_done;

/*
 * Global data structures and functions for the dbuf cache.
 */
static kmem_cache_t *dbuf_cache;

/* ARGSUSED */
static int
dbuf_cons(void *vdb, void *unused, int kmflag)
{
	dmu_buf_impl_t *db = vdb;
	bzero(db, sizeof (dmu_buf_impl_t));

	mutex_init(&db->db_mtx, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&db->db_changed, NULL, CV_DEFAULT, NULL);
	refcount_create(&db->db_holds);
	return (0);
}

/* ARGSUSED */
static void
dbuf_dest(void *vdb, void *unused)
{
	dmu_buf_impl_t *db = vdb;
	mutex_destroy(&db->db_mtx);
	cv_destroy(&db->db_changed);
	refcount_destroy(&db->db_holds);
}

/*
 * dbuf hash table routines
 */
static dbuf_hash_table_t dbuf_hash_table;

static uint64_t dbuf_hash_count;

static uint64_t
dbuf_hash(void *os, uint64_t obj, uint8_t lvl, uint64_t blkid)
{
	uintptr_t osv = (uintptr_t)os;
	uint64_t crc = -1ULL;

	ASSERT(zfs_crc64_table[128] == ZFS_CRC64_POLY);
	crc = (crc >> 8) ^ zfs_crc64_table[(crc ^ (lvl)) & 0xFF];
	crc = (crc >> 8) ^ zfs_crc64_table[(crc ^ (osv >> 6)) & 0xFF];
	crc = (crc >> 8) ^ zfs_crc64_table[(crc ^ (obj >> 0)) & 0xFF];
	crc = (crc >> 8) ^ zfs_crc64_table[(crc ^ (obj >> 8)) & 0xFF];
	crc = (crc >> 8) ^ zfs_crc64_table[(crc ^ (blkid >> 0)) & 0xFF];
	crc = (crc >> 8) ^ zfs_crc64_table[(crc ^ (blkid >> 8)) & 0xFF];

	crc ^= (osv>>14) ^ (obj>>16) ^ (blkid>>16);

	return (crc);
}

#define	DBUF_HASH(os, obj, level, blkid) dbuf_hash(os, obj, level, blkid);

#define	DBUF_EQUAL(dbuf, os, obj, level, blkid)		\
	((dbuf)->db.db_object == (obj) &&		\
	(dbuf)->db_objset == (os) &&			\
	(dbuf)->db_level == (level) &&			\
	(dbuf)->db_blkid == (blkid))

dmu_buf_impl_t *
dbuf_find(dnode_t *dn, uint8_t level, uint64_t blkid)
{
	dbuf_hash_table_t *h = &dbuf_hash_table;
	objset_impl_t *os = dn->dn_objset;
	uint64_t obj = dn->dn_object;
	uint64_t hv = DBUF_HASH(os, obj, level, blkid);
	uint64_t idx = hv & h->hash_table_mask;
	dmu_buf_impl_t *db;

	mutex_enter(DBUF_HASH_MUTEX(h, idx));
	for (db = h->hash_table[idx]; db != NULL; db = db->db_hash_next) {
		if (DBUF_EQUAL(db, os, obj, level, blkid)) {
			mutex_enter(&db->db_mtx);
			if (db->db_state != DB_EVICTING) {
				mutex_exit(DBUF_HASH_MUTEX(h, idx));
				return (db);
			}
			mutex_exit(&db->db_mtx);
		}
	}
	mutex_exit(DBUF_HASH_MUTEX(h, idx));
	return (NULL);
}

/*
 * Insert an entry into the hash table.  If there is already an element
 * equal to elem in the hash table, then the already existing element
 * will be returned and the new element will not be inserted.
 * Otherwise returns NULL.
 */
static dmu_buf_impl_t *
dbuf_hash_insert(dmu_buf_impl_t *db)
{
	dbuf_hash_table_t *h = &dbuf_hash_table;
	objset_impl_t *os = db->db_objset;
	uint64_t obj = db->db.db_object;
	int level = db->db_level;
	uint64_t blkid = db->db_blkid;
	uint64_t hv = DBUF_HASH(os, obj, level, blkid);
	uint64_t idx = hv & h->hash_table_mask;
	dmu_buf_impl_t *dbf;

	mutex_enter(DBUF_HASH_MUTEX(h, idx));
	for (dbf = h->hash_table[idx]; dbf != NULL; dbf = dbf->db_hash_next) {
		if (DBUF_EQUAL(dbf, os, obj, level, blkid)) {
			mutex_enter(&dbf->db_mtx);
			if (dbf->db_state != DB_EVICTING) {
				mutex_exit(DBUF_HASH_MUTEX(h, idx));
				return (dbf);
			}
			mutex_exit(&dbf->db_mtx);
		}
	}

	mutex_enter(&db->db_mtx);
	db->db_hash_next = h->hash_table[idx];
	h->hash_table[idx] = db;
	mutex_exit(DBUF_HASH_MUTEX(h, idx));
	atomic_add_64(&dbuf_hash_count, 1);

	return (NULL);
}

/*
 * Remove an entry from the hash table.  This operation will
 * fail if there are any existing holds on the db.
 */
static void
dbuf_hash_remove(dmu_buf_impl_t *db)
{
	dbuf_hash_table_t *h = &dbuf_hash_table;
	uint64_t hv = DBUF_HASH(db->db_objset, db->db.db_object,
	    db->db_level, db->db_blkid);
	uint64_t idx = hv & h->hash_table_mask;
	dmu_buf_impl_t *dbf, **dbp;

	/*
	 * We musn't hold db_mtx to maintin lock ordering:
	 * DBUF_HASH_MUTEX > db_mtx.
	 */
	ASSERT(refcount_is_zero(&db->db_holds));
	ASSERT(db->db_state == DB_EVICTING);
	ASSERT(!MUTEX_HELD(&db->db_mtx));

	mutex_enter(DBUF_HASH_MUTEX(h, idx));
	dbp = &h->hash_table[idx];
	while ((dbf = *dbp) != db) {
		dbp = &dbf->db_hash_next;
		ASSERT(dbf != NULL);
	}
	*dbp = db->db_hash_next;
	db->db_hash_next = NULL;
	mutex_exit(DBUF_HASH_MUTEX(h, idx));
	atomic_add_64(&dbuf_hash_count, -1);
}

static arc_evict_func_t dbuf_do_evict;

static void
dbuf_evict_user(dmu_buf_impl_t *db)
{
	ASSERT(MUTEX_HELD(&db->db_mtx));

	if (db->db_level != 0 || db->db_evict_func == NULL)
		return;

	if (db->db_user_data_ptr_ptr)
		*db->db_user_data_ptr_ptr = db->db.db_data;
	db->db_evict_func(&db->db, db->db_user_ptr);
	db->db_user_ptr = NULL;
	db->db_user_data_ptr_ptr = NULL;
	db->db_evict_func = NULL;
}

void
dbuf_evict(dmu_buf_impl_t *db)
{
	ASSERT(MUTEX_HELD(&db->db_mtx));
	ASSERT(db->db_buf == NULL);
	ASSERT(db->db_data_pending == NULL);

	dbuf_clear(db);
	dbuf_destroy(db);
}

void
dbuf_init(void)
{
	uint64_t hsize = 1ULL << 16;
	dbuf_hash_table_t *h = &dbuf_hash_table;
	int i;

	/*
	 * The hash table is big enough to fill all of physical memory
	 * with an average 4K block size.  The table will take up
	 * totalmem*sizeof(void*)/4K (i.e. 2MB/GB with 8-byte pointers).
	 */
	while (hsize * 4096 < physmem * PAGESIZE)
		hsize <<= 1;

retry:
	h->hash_table_mask = hsize - 1;
	h->hash_table = kmem_zalloc(hsize * sizeof (void *), KM_NOSLEEP);
	if (h->hash_table == NULL) {
		/* XXX - we should really return an error instead of assert */
		ASSERT(hsize > (1ULL << 10));
		hsize >>= 1;
		goto retry;
	}

	dbuf_cache = kmem_cache_create("dmu_buf_impl_t",
	    sizeof (dmu_buf_impl_t),
	    0, dbuf_cons, dbuf_dest, NULL, NULL, NULL, 0);

	for (i = 0; i < DBUF_MUTEXES; i++)
		mutex_init(&h->hash_mutexes[i], NULL, MUTEX_DEFAULT, NULL);
}

void
dbuf_fini(void)
{
	dbuf_hash_table_t *h = &dbuf_hash_table;
	int i;

	for (i = 0; i < DBUF_MUTEXES; i++)
		mutex_destroy(&h->hash_mutexes[i]);
	kmem_free(h->hash_table, (h->hash_table_mask + 1) * sizeof (void *));
	kmem_cache_destroy(dbuf_cache);
}

/*
 * Other stuff.
 */

#ifdef ZFS_DEBUG
static void
dbuf_verify(dmu_buf_impl_t *db)
{
	dnode_t *dn = db->db_dnode;

	ASSERT(MUTEX_HELD(&db->db_mtx));

	if (!(zfs_flags & ZFS_DEBUG_DBUF_VERIFY))
		return;

	ASSERT(db->db_objset != NULL);
	if (dn == NULL) {
		ASSERT(db->db_parent == NULL);
		ASSERT(db->db_blkptr == NULL);
	} else {
		ASSERT3U(db->db.db_object, ==, dn->dn_object);
		ASSERT3P(db->db_objset, ==, dn->dn_objset);
		ASSERT3U(db->db_level, <, dn->dn_nlevels);
		ASSERT(db->db_blkid == DB_BONUS_BLKID ||
		    list_head(&dn->dn_dbufs));
	}
	if (db->db_blkid == DB_BONUS_BLKID) {
		ASSERT(dn != NULL);
		ASSERT3U(db->db.db_size, >=, dn->dn_bonuslen);
		ASSERT3U(db->db.db_offset, ==, DB_BONUS_BLKID);
	} else {
		ASSERT3U(db->db.db_offset, ==, db->db_blkid * db->db.db_size);
	}

	/*
	 * We can't assert that db_size matches dn_datablksz because it
	 * can be momentarily different when another thread is doing
	 * dnode_set_blksz().
	 */
	if (db->db_level == 0 && db->db.db_object == DMU_META_DNODE_OBJECT) {
		dbuf_dirty_record_t *dr = db->db_data_pending;
		/*
		 * It should only be modified in syncing context, so
		 * make sure we only have one copy of the data.
		 */
		ASSERT(dr == NULL || dr->dt.dl.dr_data == db->db_buf);
	}

	/* verify db->db_blkptr */
	if (db->db_blkptr) {
		if (db->db_parent == dn->dn_dbuf) {
			/* db is pointed to by the dnode */
			/* ASSERT3U(db->db_blkid, <, dn->dn_nblkptr); */
			if (db->db.db_object == DMU_META_DNODE_OBJECT)
				ASSERT(db->db_parent == NULL);
			else
				ASSERT(db->db_parent != NULL);
			ASSERT3P(db->db_blkptr, ==,
			    &dn->dn_phys->dn_blkptr[db->db_blkid]);
		} else {
			/* db is pointed to by an indirect block */
			int epb = db->db_parent->db.db_size >> SPA_BLKPTRSHIFT;
			ASSERT3U(db->db_parent->db_level, ==, db->db_level+1);
			ASSERT3U(db->db_parent->db.db_object, ==,
			    db->db.db_object);
			/*
			 * dnode_grow_indblksz() can make this fail if we don't
			 * have the struct_rwlock.  XXX indblksz no longer
			 * grows.  safe to do this now?
			 */
			if (RW_WRITE_HELD(&db->db_dnode->dn_struct_rwlock)) {
				ASSERT3P(db->db_blkptr, ==,
				    ((blkptr_t *)db->db_parent->db.db_data +
				    db->db_blkid % epb));
			}
		}
	}
	if ((db->db_blkptr == NULL || BP_IS_HOLE(db->db_blkptr)) &&
	    db->db.db_data && db->db_blkid != DB_BONUS_BLKID &&
	    db->db_state != DB_FILL && !dn->dn_free_txg) {
		/*
		 * If the blkptr isn't set but they have nonzero data,
		 * it had better be dirty, otherwise we'll lose that
		 * data when we evict this buffer.
		 */
		if (db->db_dirtycnt == 0) {
			uint64_t *buf = db->db.db_data;
			int i;

			for (i = 0; i < db->db.db_size >> 3; i++) {
				ASSERT(buf[i] == 0);
			}
		}
	}
}
#endif

static void
dbuf_update_data(dmu_buf_impl_t *db)
{
	ASSERT(MUTEX_HELD(&db->db_mtx));
	if (db->db_level == 0 && db->db_user_data_ptr_ptr) {
		ASSERT(!refcount_is_zero(&db->db_holds));
		*db->db_user_data_ptr_ptr = db->db.db_data;
	}
}

static void
dbuf_set_data(dmu_buf_impl_t *db, arc_buf_t *buf)
{
	ASSERT(MUTEX_HELD(&db->db_mtx));
	ASSERT(db->db_buf == NULL || !arc_has_callback(db->db_buf));
	db->db_buf = buf;
	if (buf != NULL) {
		ASSERT(buf->b_data != NULL);
		db->db.db_data = buf->b_data;
		if (!arc_released(buf))
			arc_set_callback(buf, dbuf_do_evict, db);
		dbuf_update_data(db);
	} else {
		dbuf_evict_user(db);
		db->db.db_data = NULL;
		if (db->db_state != DB_NOFILL)
			db->db_state = DB_UNCACHED;
	}
}

uint64_t
dbuf_whichblock(dnode_t *dn, uint64_t offset)
{
	if (dn->dn_datablkshift) {
		return (offset >> dn->dn_datablkshift);
	} else {
		ASSERT3U(offset, <, dn->dn_datablksz);
		return (0);
	}
}

static void
dbuf_read_done(zio_t *zio, arc_buf_t *buf, void *vdb)
{
	dmu_buf_impl_t *db = vdb;

	mutex_enter(&db->db_mtx);
	ASSERT3U(db->db_state, ==, DB_READ);
	/*
	 * All reads are synchronous, so we must have a hold on the dbuf
	 */
	ASSERT(refcount_count(&db->db_holds) > 0);
	ASSERT(db->db_buf == NULL);
	ASSERT(db->db.db_data == NULL);
	if (db->db_level == 0 && db->db_freed_in_flight) {
		/* we were freed in flight; disregard any error */
		arc_release(buf, db);
		bzero(buf->b_data, db->db.db_size);
		arc_buf_freeze(buf);
		db->db_freed_in_flight = FALSE;
		dbuf_set_data(db, buf);
		db->db_state = DB_CACHED;
	} else if (zio == NULL || zio->io_error == 0) {
		dbuf_set_data(db, buf);
		db->db_state = DB_CACHED;
	} else {
		ASSERT(db->db_blkid != DB_BONUS_BLKID);
		ASSERT3P(db->db_buf, ==, NULL);
		VERIFY(arc_buf_remove_ref(buf, db) == 1);
		db->db_state = DB_UNCACHED;
	}
	cv_broadcast(&db->db_changed);
	mutex_exit(&db->db_mtx);
	dbuf_rele(db, NULL);
}

static void
dbuf_read_impl(dmu_buf_impl_t *db, zio_t *zio, uint32_t *flags)
{
	dnode_t *dn = db->db_dnode;
	zbookmark_t zb;
	uint32_t aflags = ARC_NOWAIT;
	arc_buf_t *pbuf;

	ASSERT(!refcount_is_zero(&db->db_holds));
	/* We need the struct_rwlock to prevent db_blkptr from changing. */
	ASSERT(RW_LOCK_HELD(&dn->dn_struct_rwlock));
	ASSERT(MUTEX_HELD(&db->db_mtx));
	ASSERT(db->db_state == DB_UNCACHED);
	ASSERT(db->db_buf == NULL);

	if (db->db_blkid == DB_BONUS_BLKID) {
		int bonuslen = dn->dn_bonuslen;

		ASSERT3U(bonuslen, <=, db->db.db_size);
		db->db.db_data = zio_buf_alloc(DN_MAX_BONUSLEN);
		arc_space_consume(DN_MAX_BONUSLEN);
		if (bonuslen < DN_MAX_BONUSLEN)
			bzero(db->db.db_data, DN_MAX_BONUSLEN);
		bcopy(DN_BONUS(dn->dn_phys), db->db.db_data,
		    bonuslen);
		dbuf_update_data(db);
		db->db_state = DB_CACHED;
		mutex_exit(&db->db_mtx);
		return;
	}

	/*
	 * Recheck BP_IS_HOLE() after dnode_block_freed() in case dnode_sync()
	 * processes the delete record and clears the bp while we are waiting
	 * for the dn_mtx (resulting in a "no" from block_freed).
	 */
	if (db->db_blkptr == NULL || BP_IS_HOLE(db->db_blkptr) ||
	    (db->db_level == 0 && (dnode_block_freed(dn, db->db_blkid) ||
	    BP_IS_HOLE(db->db_blkptr)))) {
		arc_buf_contents_t type = DBUF_GET_BUFC_TYPE(db);

		dbuf_set_data(db, arc_buf_alloc(dn->dn_objset->os_spa,
		    db->db.db_size, db, type));
		bzero(db->db.db_data, db->db.db_size);
		db->db_state = DB_CACHED;
		*flags |= DB_RF_CACHED;
		mutex_exit(&db->db_mtx);
		return;
	}

	db->db_state = DB_READ;
	mutex_exit(&db->db_mtx);

	if (DBUF_IS_L2CACHEABLE(db))
		aflags |= ARC_L2CACHE;

	zb.zb_objset = db->db_objset->os_dsl_dataset ?
	    db->db_objset->os_dsl_dataset->ds_object : 0;
	zb.zb_object = db->db.db_object;
	zb.zb_level = db->db_level;
	zb.zb_blkid = db->db_blkid;

	dbuf_add_ref(db, NULL);
	/* ZIO_FLAG_CANFAIL callers have to check the parent zio's error */

	if (db->db_parent)
		pbuf = db->db_parent->db_buf;
	else
		pbuf = db->db_objset->os_phys_buf;

	(void) arc_read(zio, dn->dn_objset->os_spa, db->db_blkptr, pbuf,
	    dbuf_read_done, db, ZIO_PRIORITY_SYNC_READ,
	    (*flags & DB_RF_CANFAIL) ? ZIO_FLAG_CANFAIL : ZIO_FLAG_MUSTSUCCEED,
	    &aflags, &zb);
	if (aflags & ARC_CACHED)
		*flags |= DB_RF_CACHED;
}

int
dbuf_read(dmu_buf_impl_t *db, zio_t *zio, uint32_t flags)
{
	int err = 0;
	int havepzio = (zio != NULL);
	int prefetch;

	/*
	 * We don't have to hold the mutex to check db_state because it
	 * can't be freed while we have a hold on the buffer.
	 */
	ASSERT(!refcount_is_zero(&db->db_holds));

	if (db->db_state == DB_NOFILL)
		return (EIO);

	if ((flags & DB_RF_HAVESTRUCT) == 0)
		rw_enter(&db->db_dnode->dn_struct_rwlock, RW_READER);

	prefetch = db->db_level == 0 && db->db_blkid != DB_BONUS_BLKID &&
	    (flags & DB_RF_NOPREFETCH) == 0 && db->db_dnode != NULL &&
	    DBUF_IS_CACHEABLE(db);

	mutex_enter(&db->db_mtx);
	if (db->db_state == DB_CACHED) {
		mutex_exit(&db->db_mtx);
		if (prefetch)
			dmu_zfetch(&db->db_dnode->dn_zfetch, db->db.db_offset,
			    db->db.db_size, TRUE);
		if ((flags & DB_RF_HAVESTRUCT) == 0)
			rw_exit(&db->db_dnode->dn_struct_rwlock);
	} else if (db->db_state == DB_UNCACHED) {
		if (zio == NULL) {
			zio = zio_root(db->db_dnode->dn_objset->os_spa,
			    NULL, NULL, ZIO_FLAG_CANFAIL);
		}
		dbuf_read_impl(db, zio, &flags);

		/* dbuf_read_impl has dropped db_mtx for us */

		if (prefetch)
			dmu_zfetch(&db->db_dnode->dn_zfetch, db->db.db_offset,
			    db->db.db_size, flags & DB_RF_CACHED);

		if ((flags & DB_RF_HAVESTRUCT) == 0)
			rw_exit(&db->db_dnode->dn_struct_rwlock);

		if (!havepzio)
			err = zio_wait(zio);
	} else {
		mutex_exit(&db->db_mtx);
		if (prefetch)
			dmu_zfetch(&db->db_dnode->dn_zfetch, db->db.db_offset,
			    db->db.db_size, TRUE);
		if ((flags & DB_RF_HAVESTRUCT) == 0)
			rw_exit(&db->db_dnode->dn_struct_rwlock);

		mutex_enter(&db->db_mtx);
		if ((flags & DB_RF_NEVERWAIT) == 0) {
			while (db->db_state == DB_READ ||
			    db->db_state == DB_FILL) {
				ASSERT(db->db_state == DB_READ ||
				    (flags & DB_RF_HAVESTRUCT) == 0);
				cv_wait(&db->db_changed, &db->db_mtx);
			}
			if (db->db_state == DB_UNCACHED)
				err = EIO;
		}
		mutex_exit(&db->db_mtx);
	}

	ASSERT(err || havepzio || db->db_state == DB_CACHED);
	return (err);
}

static void
dbuf_noread(dmu_buf_impl_t *db)
{
	ASSERT(!refcount_is_zero(&db->db_holds));
	ASSERT(db->db_blkid != DB_BONUS_BLKID);
	mutex_enter(&db->db_mtx);
	while (db->db_state == DB_READ || db->db_state == DB_FILL)
		cv_wait(&db->db_changed, &db->db_mtx);
	if (db->db_state == DB_UNCACHED) {
		arc_buf_contents_t type = DBUF_GET_BUFC_TYPE(db);

		ASSERT(db->db_buf == NULL);
		ASSERT(db->db.db_data == NULL);
		dbuf_set_data(db, arc_buf_alloc(db->db_dnode->dn_objset->os_spa,
		    db->db.db_size, db, type));
		db->db_state = DB_FILL;
	} else if (db->db_state == DB_NOFILL) {
		dbuf_set_data(db, NULL);
	} else {
		ASSERT3U(db->db_state, ==, DB_CACHED);
	}
	mutex_exit(&db->db_mtx);
}

/*
 * This is our just-in-time copy function.  It makes a copy of
 * buffers, that have been modified in a previous transaction
 * group, before we modify them in the current active group.
 *
 * This function is used in two places: when we are dirtying a
 * buffer for the first time in a txg, and when we are freeing
 * a range in a dnode that includes this buffer.
 *
 * Note that when we are called from dbuf_free_range() we do
 * not put a hold on the buffer, we just traverse the active
 * dbuf list for the dnode.
 */
static void
dbuf_fix_old_data(dmu_buf_impl_t *db, uint64_t txg)
{
	dbuf_dirty_record_t *dr = db->db_last_dirty;

	ASSERT(MUTEX_HELD(&db->db_mtx));
	ASSERT(db->db.db_data != NULL);
	ASSERT(db->db_level == 0);
	ASSERT(db->db.db_object != DMU_META_DNODE_OBJECT);

	if (dr == NULL ||
	    (dr->dt.dl.dr_data !=
	    ((db->db_blkid  == DB_BONUS_BLKID) ? db->db.db_data : db->db_buf)))
		return;

	/*
	 * If the last dirty record for this dbuf has not yet synced
	 * and its referencing the dbuf data, either:
	 * 	reset the reference to point to a new copy,
	 * or (if there a no active holders)
	 *	just null out the current db_data pointer.
	 */
	ASSERT(dr->dr_txg >= txg - 2);
	if (db->db_blkid == DB_BONUS_BLKID) {
		/* Note that the data bufs here are zio_bufs */
		dr->dt.dl.dr_data = zio_buf_alloc(DN_MAX_BONUSLEN);
		arc_space_consume(DN_MAX_BONUSLEN);
		bcopy(db->db.db_data, dr->dt.dl.dr_data, DN_MAX_BONUSLEN);
	} else if (refcount_count(&db->db_holds) > db->db_dirtycnt) {
		int size = db->db.db_size;
		arc_buf_contents_t type = DBUF_GET_BUFC_TYPE(db);
		dr->dt.dl.dr_data = arc_buf_alloc(
		    db->db_dnode->dn_objset->os_spa, size, db, type);
		bcopy(db->db.db_data, dr->dt.dl.dr_data->b_data, size);
	} else {
		dbuf_set_data(db, NULL);
	}
}

void
dbuf_unoverride(dbuf_dirty_record_t *dr)
{
	dmu_buf_impl_t *db = dr->dr_dbuf;
	uint64_t txg = dr->dr_txg;

	ASSERT(MUTEX_HELD(&db->db_mtx));
	ASSERT(dr->dt.dl.dr_override_state != DR_IN_DMU_SYNC);
	ASSERT(db->db_level == 0);

	if (db->db_blkid == DB_BONUS_BLKID ||
	    dr->dt.dl.dr_override_state == DR_NOT_OVERRIDDEN)
		return;

	/* free this block */
	if (!BP_IS_HOLE(&dr->dt.dl.dr_overridden_by)) {
		/* XXX can get silent EIO here */
		(void) dsl_free(NULL,
		    spa_get_dsl(db->db_dnode->dn_objset->os_spa),
		    txg, &dr->dt.dl.dr_overridden_by, NULL, NULL, ARC_WAIT);
	}
	dr->dt.dl.dr_override_state = DR_NOT_OVERRIDDEN;
	/*
	 * Release the already-written buffer, so we leave it in
	 * a consistent dirty state.  Note that all callers are
	 * modifying the buffer, so they will immediately do
	 * another (redundant) arc_release().  Therefore, leave
	 * the buf thawed to save the effort of freezing &
	 * immediately re-thawing it.
	 */
	arc_release(dr->dt.dl.dr_data, db);
}

/*
 * Evict (if its unreferenced) or clear (if its referenced) any level-0
 * data blocks in the free range, so that any future readers will find
 * empty blocks.  Also, if we happen accross any level-1 dbufs in the
 * range that have not already been marked dirty, mark them dirty so
 * they stay in memory.
 */
void
dbuf_free_range(dnode_t *dn, uint64_t start, uint64_t end, dmu_tx_t *tx)
{
	dmu_buf_impl_t *db, *db_next;
	uint64_t txg = tx->tx_txg;
	int epbs = dn->dn_indblkshift - SPA_BLKPTRSHIFT;
	uint64_t first_l1 = start >> epbs;
	uint64_t last_l1 = end >> epbs;

	if (end > dn->dn_maxblkid) {
		end = dn->dn_maxblkid;
		last_l1 = end >> epbs;
	}
	dprintf_dnode(dn, "start=%llu end=%llu\n", start, end);
	mutex_enter(&dn->dn_dbufs_mtx);
	for (db = list_head(&dn->dn_dbufs); db; db = db_next) {
		db_next = list_next(&dn->dn_dbufs, db);
		ASSERT(db->db_blkid != DB_BONUS_BLKID);

		if (db->db_level == 1 &&
		    db->db_blkid >= first_l1 && db->db_blkid <= last_l1) {
			mutex_enter(&db->db_mtx);
			if (db->db_last_dirty &&
			    db->db_last_dirty->dr_txg < txg) {
				dbuf_add_ref(db, FTAG);
				mutex_exit(&db->db_mtx);
				dbuf_will_dirty(db, tx);
				dbuf_rele(db, FTAG);
			} else {
				mutex_exit(&db->db_mtx);
			}
		}

		if (db->db_level != 0)
			continue;
		dprintf_dbuf(db, "found buf %s\n", "");
		if (db->db_blkid < start || db->db_blkid > end)
			continue;

		/* found a level 0 buffer in the range */
		if (dbuf_undirty(db, tx))
			continue;

		mutex_enter(&db->db_mtx);
		if (db->db_state == DB_UNCACHED ||
		    db->db_state == DB_NOFILL ||
		    db->db_state == DB_EVICTING) {
			ASSERT(db->db.db_data == NULL);
			mutex_exit(&db->db_mtx);
			continue;
		}
		if (db->db_state == DB_READ || db->db_state == DB_FILL) {
			/* will be handled in dbuf_read_done or dbuf_rele */
			db->db_freed_in_flight = TRUE;
			mutex_exit(&db->db_mtx);
			continue;
		}
		if (refcount_count(&db->db_holds) == 0) {
			ASSERT(db->db_buf);
			dbuf_clear(db);
			continue;
		}
		/* The dbuf is referenced */

		if (db->db_last_dirty != NULL) {
			dbuf_dirty_record_t *dr = db->db_last_dirty;

			if (dr->dr_txg == txg) {
				/*
				 * This buffer is "in-use", re-adjust the file
				 * size to reflect that this buffer may
				 * contain new data when we sync.
				 */
				if (db->db_blkid > dn->dn_maxblkid)
					dn->dn_maxblkid = db->db_blkid;
				dbuf_unoverride(dr);
			} else {
				/*
				 * This dbuf is not dirty in the open context.
				 * Either uncache it (if its not referenced in
				 * the open context) or reset its contents to
				 * empty.
				 */
				dbuf_fix_old_data(db, txg);
			}
		}
		/* clear the contents if its cached */
		if (db->db_state == DB_CACHED) {
			ASSERT(db->db.db_data != NULL);
			arc_release(db->db_buf, db);
			bzero(db->db.db_data, db->db.db_size);
			arc_buf_freeze(db->db_buf);
		}

		mutex_exit(&db->db_mtx);
	}
	mutex_exit(&dn->dn_dbufs_mtx);
}

static int
dbuf_block_freeable(dmu_buf_impl_t *db)
{
	dsl_dataset_t *ds = db->db_objset->os_dsl_dataset;
	uint64_t birth_txg = 0;

	/*
	 * We don't need any locking to protect db_blkptr:
	 * If it's syncing, then db_last_dirty will be set
	 * so we'll ignore db_blkptr.
	 */
	ASSERT(MUTEX_HELD(&db->db_mtx));
	if (db->db_last_dirty)
		birth_txg = db->db_last_dirty->dr_txg;
	else if (db->db_blkptr)
		birth_txg = db->db_blkptr->blk_birth;

	/* If we don't exist or are in a snapshot, we can't be freed */
	if (birth_txg)
		return (ds == NULL ||
		    dsl_dataset_block_freeable(ds, birth_txg));
	else
		return (FALSE);
}

void
dbuf_new_size(dmu_buf_impl_t *db, int size, dmu_tx_t *tx)
{
	arc_buf_t *buf, *obuf;
	int osize = db->db.db_size;
	arc_buf_contents_t type = DBUF_GET_BUFC_TYPE(db);

	ASSERT(db->db_blkid != DB_BONUS_BLKID);

	/* XXX does *this* func really need the lock? */
	ASSERT(RW_WRITE_HELD(&db->db_dnode->dn_struct_rwlock));

	/*
	 * This call to dbuf_will_dirty() with the dn_struct_rwlock held
	 * is OK, because there can be no other references to the db
	 * when we are changing its size, so no concurrent DB_FILL can
	 * be happening.
	 */
	/*
	 * XXX we should be doing a dbuf_read, checking the return
	 * value and returning that up to our callers
	 */
	dbuf_will_dirty(db, tx);

	/* create the data buffer for the new block */
	buf = arc_buf_alloc(db->db_dnode->dn_objset->os_spa, size, db, type);

	/* copy old block data to the new block */
	obuf = db->db_buf;
	bcopy(obuf->b_data, buf->b_data, MIN(osize, size));
	/* zero the remainder */
	if (size > osize)
		bzero((uint8_t *)buf->b_data + osize, size - osize);

	mutex_enter(&db->db_mtx);
	dbuf_set_data(db, buf);
	VERIFY(arc_buf_remove_ref(obuf, db) == 1);
	db->db.db_size = size;

	if (db->db_level == 0) {
		ASSERT3U(db->db_last_dirty->dr_txg, ==, tx->tx_txg);
		db->db_last_dirty->dt.dl.dr_data = buf;
	}
	mutex_exit(&db->db_mtx);

	dnode_willuse_space(db->db_dnode, size-osize, tx);
}

dbuf_dirty_record_t *
dbuf_dirty(dmu_buf_impl_t *db, dmu_tx_t *tx)
{
	dnode_t *dn = db->db_dnode;
	objset_impl_t *os = dn->dn_objset;
	dbuf_dirty_record_t **drp, *dr;
	int drop_struct_lock = FALSE;
	boolean_t do_free_accounting = B_FALSE;
	int txgoff = tx->tx_txg & TXG_MASK;

	ASSERT(tx->tx_txg != 0);
	ASSERT(!refcount_is_zero(&db->db_holds));
	DMU_TX_DIRTY_BUF(tx, db);

	/*
	 * Shouldn't dirty a regular buffer in syncing context.  Private
	 * objects may be dirtied in syncing context, but only if they
	 * were already pre-dirtied in open context.
	 * XXX We may want to prohibit dirtying in syncing context even
	 * if they did pre-dirty.
	 */
	ASSERT(!dmu_tx_is_syncing(tx) ||
	    BP_IS_HOLE(dn->dn_objset->os_rootbp) ||
	    dn->dn_object == DMU_META_DNODE_OBJECT ||
	    dn->dn_objset->os_dsl_dataset == NULL ||
	    dsl_dir_is_private(dn->dn_objset->os_dsl_dataset->ds_dir));

	/*
	 * We make this assert for private objects as well, but after we
	 * check if we're already dirty.  They are allowed to re-dirty
	 * in syncing context.
	 */
	ASSERT(dn->dn_object == DMU_META_DNODE_OBJECT ||
	    dn->dn_dirtyctx == DN_UNDIRTIED || dn->dn_dirtyctx ==
	    (dmu_tx_is_syncing(tx) ? DN_DIRTY_SYNC : DN_DIRTY_OPEN));

	mutex_enter(&db->db_mtx);
	/*
	 * XXX make this true for indirects too?  The problem is that
	 * transactions created with dmu_tx_create_assigned() from
	 * syncing context don't bother holding ahead.
	 */
	ASSERT(db->db_level != 0 ||
	    db->db_state == DB_CACHED || db->db_state == DB_FILL ||
	    db->db_state == DB_NOFILL);

	mutex_enter(&dn->dn_mtx);
	/*
	 * Don't set dirtyctx to SYNC if we're just modifying this as we
	 * initialize the objset.
	 */
	if (dn->dn_dirtyctx == DN_UNDIRTIED &&
	    !BP_IS_HOLE(dn->dn_objset->os_rootbp)) {
		dn->dn_dirtyctx =
		    (dmu_tx_is_syncing(tx) ? DN_DIRTY_SYNC : DN_DIRTY_OPEN);
		ASSERT(dn->dn_dirtyctx_firstset == NULL);
		dn->dn_dirtyctx_firstset = kmem_alloc(1, KM_SLEEP);
	}
	mutex_exit(&dn->dn_mtx);

	/*
	 * If this buffer is already dirty, we're done.
	 */
	drp = &db->db_last_dirty;
	ASSERT(*drp == NULL || (*drp)->dr_txg <= tx->tx_txg ||
	    db->db.db_object == DMU_META_DNODE_OBJECT);
	while ((dr = *drp) != NULL && dr->dr_txg > tx->tx_txg)
		drp = &dr->dr_next;
	if (dr && dr->dr_txg == tx->tx_txg) {
		if (db->db_level == 0 && db->db_blkid != DB_BONUS_BLKID) {
			/*
			 * If this buffer has already been written out,
			 * we now need to reset its state.
			 */
			dbuf_unoverride(dr);
			if (db->db.db_object != DMU_META_DNODE_OBJECT)
				arc_buf_thaw(db->db_buf);
		}
		mutex_exit(&db->db_mtx);
		return (dr);
	}

	/*
	 * Only valid if not already dirty.
	 */
	ASSERT(dn->dn_dirtyctx == DN_UNDIRTIED || dn->dn_dirtyctx ==
	    (dmu_tx_is_syncing(tx) ? DN_DIRTY_SYNC : DN_DIRTY_OPEN));

	ASSERT3U(dn->dn_nlevels, >, db->db_level);
	ASSERT((dn->dn_phys->dn_nlevels == 0 && db->db_level == 0) ||
	    dn->dn_phys->dn_nlevels > db->db_level ||
	    dn->dn_next_nlevels[txgoff] > db->db_level ||
	    dn->dn_next_nlevels[(tx->tx_txg-1) & TXG_MASK] > db->db_level ||
	    dn->dn_next_nlevels[(tx->tx_txg-2) & TXG_MASK] > db->db_level);

	/*
	 * We should only be dirtying in syncing context if it's the
	 * mos, a spa os, or we're initializing the os.  However, we are
	 * allowed to dirty in syncing context provided we already
	 * dirtied it in open context.  Hence we must make this
	 * assertion only if we're not already dirty.
	 */
	ASSERT(!dmu_tx_is_syncing(tx) ||
	    os->os_dsl_dataset == NULL ||
	    !dsl_dir_is_private(os->os_dsl_dataset->ds_dir) ||
	    !BP_IS_HOLE(os->os_rootbp));
	ASSERT(db->db.db_size != 0);

	dprintf_dbuf(db, "size=%llx\n", (u_longlong_t)db->db.db_size);

	if (db->db_blkid != DB_BONUS_BLKID) {
		/*
		 * Update the accounting.
		 * Note: we delay "free accounting" until after we drop
		 * the db_mtx.  This keeps us from grabbing other locks
		 * (and possibly deadlocking) in bp_get_dasize() while
		 * also holding the db_mtx.
		 */
		dnode_willuse_space(dn, db->db.db_size, tx);
		do_free_accounting = dbuf_block_freeable(db);
	}

	/*
	 * If this buffer is dirty in an old transaction group we need
	 * to make a copy of it so that the changes we make in this
	 * transaction group won't leak out when we sync the older txg.
	 */
	dr = kmem_zalloc(sizeof (dbuf_dirty_record_t), KM_SLEEP);
	if (db->db_level == 0) {
		void *data_old = db->db_buf;

		if (db->db_state != DB_NOFILL) {
			if (db->db_blkid == DB_BONUS_BLKID) {
				dbuf_fix_old_data(db, tx->tx_txg);
				data_old = db->db.db_data;
			} else if (db->db.db_object != DMU_META_DNODE_OBJECT) {
				/*
				 * Release the data buffer from the cache so
				 * that we can modify it without impacting
				 * possible other users of this cached data
				 * block.  Note that indirect blocks and
				 * private objects are not released until the
				 * syncing state (since they are only modified
				 * then).
				 */
				arc_release(db->db_buf, db);
				dbuf_fix_old_data(db, tx->tx_txg);
				data_old = db->db_buf;
			}
			ASSERT(data_old != NULL);
		}
		dr->dt.dl.dr_data = data_old;
	} else {
		mutex_init(&dr->dt.di.dr_mtx, NULL, MUTEX_DEFAULT, NULL);
		list_create(&dr->dt.di.dr_children,
		    sizeof (dbuf_dirty_record_t),
		    offsetof(dbuf_dirty_record_t, dr_dirty_node));
	}
	dr->dr_dbuf = db;
	dr->dr_txg = tx->tx_txg;
	dr->dr_next = *drp;
	*drp = dr;

	/*
	 * We could have been freed_in_flight between the dbuf_noread
	 * and dbuf_dirty.  We win, as though the dbuf_noread() had
	 * happened after the free.
	 */
	if (db->db_level == 0 && db->db_blkid != DB_BONUS_BLKID) {
		mutex_enter(&dn->dn_mtx);
		dnode_clear_range(dn, db->db_blkid, 1, tx);
		mutex_exit(&dn->dn_mtx);
		db->db_freed_in_flight = FALSE;
	}

	/*
	 * This buffer is now part of this txg
	 */
	dbuf_add_ref(db, (void *)(uintptr_t)tx->tx_txg);
	db->db_dirtycnt += 1;
	ASSERT3U(db->db_dirtycnt, <=, 3);

	mutex_exit(&db->db_mtx);

	if (db->db_blkid == DB_BONUS_BLKID) {
		mutex_enter(&dn->dn_mtx);
		ASSERT(!list_link_active(&dr->dr_dirty_node));
		list_insert_tail(&dn->dn_dirty_records[txgoff], dr);
		mutex_exit(&dn->dn_mtx);
		dnode_setdirty(dn, tx);
		return (dr);
	} else if (do_free_accounting) {
		blkptr_t *bp = db->db_blkptr;
		int64_t willfree = (bp && !BP_IS_HOLE(bp)) ?
		    bp_get_dasize(os->os_spa, bp) : db->db.db_size;
		/*
		 * This is only a guess -- if the dbuf is dirty
		 * in a previous txg, we don't know how much
		 * space it will use on disk yet.  We should
		 * really have the struct_rwlock to access
		 * db_blkptr, but since this is just a guess,
		 * it's OK if we get an odd answer.
		 */
		dnode_willuse_space(dn, -willfree, tx);
	}

	if (!RW_WRITE_HELD(&dn->dn_struct_rwlock)) {
		rw_enter(&dn->dn_struct_rwlock, RW_READER);
		drop_struct_lock = TRUE;
	}

	if (db->db_level == 0) {
		dnode_new_blkid(dn, db->db_blkid, tx, drop_struct_lock);
		ASSERT(dn->dn_maxblkid >= db->db_blkid);
	}

	if (db->db_level+1 < dn->dn_nlevels) {
		dmu_buf_impl_t *parent = db->db_parent;
		dbuf_dirty_record_t *di;
		int parent_held = FALSE;

		if (db->db_parent == NULL || db->db_parent == dn->dn_dbuf) {
			int epbs = dn->dn_indblkshift - SPA_BLKPTRSHIFT;

			parent = dbuf_hold_level(dn, db->db_level+1,
			    db->db_blkid >> epbs, FTAG);
			parent_held = TRUE;
		}
		if (drop_struct_lock)
			rw_exit(&dn->dn_struct_rwlock);
		ASSERT3U(db->db_level+1, ==, parent->db_level);
		di = dbuf_dirty(parent, tx);
		if (parent_held)
			dbuf_rele(parent, FTAG);

		mutex_enter(&db->db_mtx);
		/*  possible race with dbuf_undirty() */
		if (db->db_last_dirty == dr ||
		    dn->dn_object == DMU_META_DNODE_OBJECT) {
			mutex_enter(&di->dt.di.dr_mtx);
			ASSERT3U(di->dr_txg, ==, tx->tx_txg);
			ASSERT(!list_link_active(&dr->dr_dirty_node));
			list_insert_tail(&di->dt.di.dr_children, dr);
			mutex_exit(&di->dt.di.dr_mtx);
			dr->dr_parent = di;
		}
		mutex_exit(&db->db_mtx);
	} else {
		ASSERT(db->db_level+1 == dn->dn_nlevels);
		ASSERT(db->db_blkid < dn->dn_nblkptr);
		ASSERT(db->db_parent == NULL ||
		    db->db_parent == db->db_dnode->dn_dbuf);
		mutex_enter(&dn->dn_mtx);
		ASSERT(!list_link_active(&dr->dr_dirty_node));
		list_insert_tail(&dn->dn_dirty_records[txgoff], dr);
		mutex_exit(&dn->dn_mtx);
		if (drop_struct_lock)
			rw_exit(&dn->dn_struct_rwlock);
	}

	dnode_setdirty(dn, tx);
	return (dr);
}

static int
dbuf_undirty(dmu_buf_impl_t *db, dmu_tx_t *tx)
{
	dnode_t *dn = db->db_dnode;
	uint64_t txg = tx->tx_txg;
	dbuf_dirty_record_t *dr, **drp;

	ASSERT(txg != 0);
	ASSERT(db->db_blkid != DB_BONUS_BLKID);

	mutex_enter(&db->db_mtx);

	/*
	 * If this buffer is not dirty, we're done.
	 */
	for (drp = &db->db_last_dirty; (dr = *drp) != NULL; drp = &dr->dr_next)
		if (dr->dr_txg <= txg)
			break;
	if (dr == NULL || dr->dr_txg < txg) {
		mutex_exit(&db->db_mtx);
		return (0);
	}
	ASSERT(dr->dr_txg == txg);

	/*
	 * If this buffer is currently held, we cannot undirty
	 * it, since one of the current holders may be in the
	 * middle of an update.  Note that users of dbuf_undirty()
	 * should not place a hold on the dbuf before the call.
	 */
	if (refcount_count(&db->db_holds) > db->db_dirtycnt) {
		mutex_exit(&db->db_mtx);
		/* Make sure we don't toss this buffer at sync phase */
		mutex_enter(&dn->dn_mtx);
		dnode_clear_range(dn, db->db_blkid, 1, tx);
		mutex_exit(&dn->dn_mtx);
		return (0);
	}

	dprintf_dbuf(db, "size=%llx\n", (u_longlong_t)db->db.db_size);

	ASSERT(db->db.db_size != 0);

	/* XXX would be nice to fix up dn_towrite_space[] */

	*drp = dr->dr_next;

	if (dr->dr_parent) {
		mutex_enter(&dr->dr_parent->dt.di.dr_mtx);
		list_remove(&dr->dr_parent->dt.di.dr_children, dr);
		mutex_exit(&dr->dr_parent->dt.di.dr_mtx);
	} else if (db->db_level+1 == dn->dn_nlevels) {
		ASSERT(db->db_blkptr == NULL || db->db_parent == dn->dn_dbuf);
		mutex_enter(&dn->dn_mtx);
		list_remove(&dn->dn_dirty_records[txg & TXG_MASK], dr);
		mutex_exit(&dn->dn_mtx);
	}

	if (db->db_level == 0) {
		if (db->db_state != DB_NOFILL) {
			dbuf_unoverride(dr);

			ASSERT(db->db_buf != NULL);
			ASSERT(dr->dt.dl.dr_data != NULL);
			if (dr->dt.dl.dr_data != db->db_buf)
				VERIFY(arc_buf_remove_ref(dr->dt.dl.dr_data,
				    db) == 1);
		}
	} else {
		ASSERT(db->db_buf != NULL);
		ASSERT(list_head(&dr->dt.di.dr_children) == NULL);
		mutex_destroy(&dr->dt.di.dr_mtx);
		list_destroy(&dr->dt.di.dr_children);
	}
	kmem_free(dr, sizeof (dbuf_dirty_record_t));

	ASSERT(db->db_dirtycnt > 0);
	db->db_dirtycnt -= 1;

	if (refcount_remove(&db->db_holds, (void *)(uintptr_t)txg) == 0) {
		arc_buf_t *buf = db->db_buf;

		ASSERT(arc_released(buf));
		dbuf_set_data(db, NULL);
		VERIFY(arc_buf_remove_ref(buf, db) == 1);
		dbuf_evict(db);
		return (1);
	}

	mutex_exit(&db->db_mtx);
	return (0);
}

#pragma weak dmu_buf_will_dirty = dbuf_will_dirty
void
dbuf_will_dirty(dmu_buf_impl_t *db, dmu_tx_t *tx)
{
	int rf = DB_RF_MUST_SUCCEED | DB_RF_NOPREFETCH;

	ASSERT(tx->tx_txg != 0);
	ASSERT(!refcount_is_zero(&db->db_holds));

	if (RW_WRITE_HELD(&db->db_dnode->dn_struct_rwlock))
		rf |= DB_RF_HAVESTRUCT;
	(void) dbuf_read(db, NULL, rf);
	(void) dbuf_dirty(db, tx);
}

void
dmu_buf_will_not_fill(dmu_buf_t *db_fake, dmu_tx_t *tx)
{
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)db_fake;

	db->db_state = DB_NOFILL;

	dmu_buf_will_fill(db_fake, tx);
}

void
dmu_buf_will_fill(dmu_buf_t *db_fake, dmu_tx_t *tx)
{
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)db_fake;

	ASSERT(db->db_blkid != DB_BONUS_BLKID);
	ASSERT(tx->tx_txg != 0);
	ASSERT(db->db_level == 0);
	ASSERT(!refcount_is_zero(&db->db_holds));

	ASSERT(db->db.db_object != DMU_META_DNODE_OBJECT ||
	    dmu_tx_private_ok(tx));

	dbuf_noread(db);
	(void) dbuf_dirty(db, tx);
}

#pragma weak dmu_buf_fill_done = dbuf_fill_done
/* ARGSUSED */
void
dbuf_fill_done(dmu_buf_impl_t *db, dmu_tx_t *tx)
{
	mutex_enter(&db->db_mtx);
	DBUF_VERIFY(db);

	if (db->db_state == DB_FILL) {
		if (db->db_level == 0 && db->db_freed_in_flight) {
			ASSERT(db->db_blkid != DB_BONUS_BLKID);
			/* we were freed while filling */
			/* XXX dbuf_undirty? */
			bzero(db->db.db_data, db->db.db_size);
			db->db_freed_in_flight = FALSE;
		}
		db->db_state = DB_CACHED;
		cv_broadcast(&db->db_changed);
	}
	mutex_exit(&db->db_mtx);
}

/*
 * "Clear" the contents of this dbuf.  This will mark the dbuf
 * EVICTING and clear *most* of its references.  Unfortunetely,
 * when we are not holding the dn_dbufs_mtx, we can't clear the
 * entry in the dn_dbufs list.  We have to wait until dbuf_destroy()
 * in this case.  For callers from the DMU we will usually see:
 *	dbuf_clear()->arc_buf_evict()->dbuf_do_evict()->dbuf_destroy()
 * For the arc callback, we will usually see:
 * 	dbuf_do_evict()->dbuf_clear();dbuf_destroy()
 * Sometimes, though, we will get a mix of these two:
 *	DMU: dbuf_clear()->arc_buf_evict()
 *	ARC: dbuf_do_evict()->dbuf_destroy()
 */
void
dbuf_clear(dmu_buf_impl_t *db)
{
	dnode_t *dn = db->db_dnode;
	dmu_buf_impl_t *parent = db->db_parent;
	dmu_buf_impl_t *dndb = dn->dn_dbuf;
	int dbuf_gone = FALSE;

	ASSERT(MUTEX_HELD(&db->db_mtx));
	ASSERT(refcount_is_zero(&db->db_holds));

	dbuf_evict_user(db);

	if (db->db_state == DB_CACHED) {
		ASSERT(db->db.db_data != NULL);
		if (db->db_blkid == DB_BONUS_BLKID) {
			zio_buf_free(db->db.db_data, DN_MAX_BONUSLEN);
			arc_space_return(DN_MAX_BONUSLEN);
		}
		db->db.db_data = NULL;
		db->db_state = DB_UNCACHED;
	}

	ASSERT(db->db_state == DB_UNCACHED || db->db_state == DB_NOFILL);
	ASSERT(db->db_data_pending == NULL);

	db->db_state = DB_EVICTING;
	db->db_blkptr = NULL;

	if (db->db_blkid != DB_BONUS_BLKID && MUTEX_HELD(&dn->dn_dbufs_mtx)) {
		list_remove(&dn->dn_dbufs, db);
		dnode_rele(dn, db);
		db->db_dnode = NULL;
	}

	if (db->db_buf)
		dbuf_gone = arc_buf_evict(db->db_buf);

	if (!dbuf_gone)
		mutex_exit(&db->db_mtx);

	/*
	 * If this dbuf is referened from an indirect dbuf,
	 * decrement the ref count on the indirect dbuf.
	 */
	if (parent && parent != dndb)
		dbuf_rele(parent, db);
}

static int
dbuf_findbp(dnode_t *dn, int level, uint64_t blkid, int fail_sparse,
    dmu_buf_impl_t **parentp, blkptr_t **bpp)
{
	int nlevels, epbs;

	*parentp = NULL;
	*bpp = NULL;

	ASSERT(blkid != DB_BONUS_BLKID);

	if (dn->dn_phys->dn_nlevels == 0)
		nlevels = 1;
	else
		nlevels = dn->dn_phys->dn_nlevels;

	epbs = dn->dn_indblkshift - SPA_BLKPTRSHIFT;

	ASSERT3U(level * epbs, <, 64);
	ASSERT(RW_LOCK_HELD(&dn->dn_struct_rwlock));
	if (level >= nlevels ||
	    (blkid > (dn->dn_phys->dn_maxblkid >> (level * epbs)))) {
		/* the buffer has no parent yet */
		return (ENOENT);
	} else if (level < nlevels-1) {
		/* this block is referenced from an indirect block */
		int err = dbuf_hold_impl(dn, level+1,
		    blkid >> epbs, fail_sparse, NULL, parentp);
		if (err)
			return (err);
		err = dbuf_read(*parentp, NULL,
		    (DB_RF_HAVESTRUCT | DB_RF_NOPREFETCH | DB_RF_CANFAIL));
		if (err) {
			dbuf_rele(*parentp, NULL);
			*parentp = NULL;
			return (err);
		}
		*bpp = ((blkptr_t *)(*parentp)->db.db_data) +
		    (blkid & ((1ULL << epbs) - 1));
		return (0);
	} else {
		/* the block is referenced from the dnode */
		ASSERT3U(level, ==, nlevels-1);
		ASSERT(dn->dn_phys->dn_nblkptr == 0 ||
		    blkid < dn->dn_phys->dn_nblkptr);
		if (dn->dn_dbuf) {
			dbuf_add_ref(dn->dn_dbuf, NULL);
			*parentp = dn->dn_dbuf;
		}
		*bpp = &dn->dn_phys->dn_blkptr[blkid];
		return (0);
	}
}

static dmu_buf_impl_t *
dbuf_create(dnode_t *dn, uint8_t level, uint64_t blkid,
    dmu_buf_impl_t *parent, blkptr_t *blkptr)
{
	objset_impl_t *os = dn->dn_objset;
	dmu_buf_impl_t *db, *odb;

	ASSERT(RW_LOCK_HELD(&dn->dn_struct_rwlock));
	ASSERT(dn->dn_type != DMU_OT_NONE);

	db = kmem_cache_alloc(dbuf_cache, KM_SLEEP);

	db->db_objset = os;
	db->db.db_object = dn->dn_object;
	db->db_level = level;
	db->db_blkid = blkid;
	db->db_last_dirty = NULL;
	db->db_dirtycnt = 0;
	db->db_dnode = dn;
	db->db_parent = parent;
	db->db_blkptr = blkptr;

	db->db_user_ptr = NULL;
	db->db_user_data_ptr_ptr = NULL;
	db->db_evict_func = NULL;
	db->db_immediate_evict = 0;
	db->db_freed_in_flight = 0;

	if (blkid == DB_BONUS_BLKID) {
		ASSERT3P(parent, ==, dn->dn_dbuf);
		db->db.db_size = DN_MAX_BONUSLEN -
		    (dn->dn_nblkptr-1) * sizeof (blkptr_t);
		ASSERT3U(db->db.db_size, >=, dn->dn_bonuslen);
		db->db.db_offset = DB_BONUS_BLKID;
		db->db_state = DB_UNCACHED;
		/* the bonus dbuf is not placed in the hash table */
		arc_space_consume(sizeof (dmu_buf_impl_t));
		return (db);
	} else {
		int blocksize =
		    db->db_level ? 1<<dn->dn_indblkshift :  dn->dn_datablksz;
		db->db.db_size = blocksize;
		db->db.db_offset = db->db_blkid * blocksize;
	}

	/*
	 * Hold the dn_dbufs_mtx while we get the new dbuf
	 * in the hash table *and* added to the dbufs list.
	 * This prevents a possible deadlock with someone
	 * trying to look up this dbuf before its added to the
	 * dn_dbufs list.
	 */
	mutex_enter(&dn->dn_dbufs_mtx);
	db->db_state = DB_EVICTING;
	if ((odb = dbuf_hash_insert(db)) != NULL) {
		/* someone else inserted it first */
		kmem_cache_free(dbuf_cache, db);
		mutex_exit(&dn->dn_dbufs_mtx);
		return (odb);
	}
	list_insert_head(&dn->dn_dbufs, db);
	db->db_state = DB_UNCACHED;
	mutex_exit(&dn->dn_dbufs_mtx);
	arc_space_consume(sizeof (dmu_buf_impl_t));

	if (parent && parent != dn->dn_dbuf)
		dbuf_add_ref(parent, db);

	ASSERT(dn->dn_object == DMU_META_DNODE_OBJECT ||
	    refcount_count(&dn->dn_holds) > 0);
	(void) refcount_add(&dn->dn_holds, db);

	dprintf_dbuf(db, "db=%p\n", db);

	return (db);
}

static int
dbuf_do_evict(void *private)
{
	arc_buf_t *buf = private;
	dmu_buf_impl_t *db = buf->b_private;

	if (!MUTEX_HELD(&db->db_mtx))
		mutex_enter(&db->db_mtx);

	ASSERT(refcount_is_zero(&db->db_holds));

	if (db->db_state != DB_EVICTING) {
		ASSERT(db->db_state == DB_CACHED);
		DBUF_VERIFY(db);
		db->db_buf = NULL;
		dbuf_evict(db);
	} else {
		mutex_exit(&db->db_mtx);
		dbuf_destroy(db);
	}
	return (0);
}

static void
dbuf_destroy(dmu_buf_impl_t *db)
{
	ASSERT(refcount_is_zero(&db->db_holds));

	if (db->db_blkid != DB_BONUS_BLKID) {
		/*
		 * If this dbuf is still on the dn_dbufs list,
		 * remove it from that list.
		 */
		if (db->db_dnode) {
			dnode_t *dn = db->db_dnode;

			mutex_enter(&dn->dn_dbufs_mtx);
			list_remove(&dn->dn_dbufs, db);
			mutex_exit(&dn->dn_dbufs_mtx);

			dnode_rele(dn, db);
			db->db_dnode = NULL;
		}
		dbuf_hash_remove(db);
	}
	db->db_parent = NULL;
	db->db_buf = NULL;

	ASSERT(!list_link_active(&db->db_link));
	ASSERT(db->db.db_data == NULL);
	ASSERT(db->db_hash_next == NULL);
	ASSERT(db->db_blkptr == NULL);
	ASSERT(db->db_data_pending == NULL);

	kmem_cache_free(dbuf_cache, db);
	arc_space_return(sizeof (dmu_buf_impl_t));
}

void
dbuf_prefetch(dnode_t *dn, uint64_t blkid)
{
	dmu_buf_impl_t *db = NULL;
	blkptr_t *bp = NULL;

	ASSERT(blkid != DB_BONUS_BLKID);
	ASSERT(RW_LOCK_HELD(&dn->dn_struct_rwlock));

	if (dnode_block_freed(dn, blkid))
		return;

	/* dbuf_find() returns with db_mtx held */
	if (db = dbuf_find(dn, 0, blkid)) {
		if (refcount_count(&db->db_holds) > 0) {
			/*
			 * This dbuf is active.  We assume that it is
			 * already CACHED, or else about to be either
			 * read or filled.
			 */
			mutex_exit(&db->db_mtx);
			return;
		}
		mutex_exit(&db->db_mtx);
		db = NULL;
	}

	if (dbuf_findbp(dn, 0, blkid, TRUE, &db, &bp) == 0) {
		if (bp && !BP_IS_HOLE(bp)) {
			arc_buf_t *pbuf;
			uint32_t aflags = ARC_NOWAIT | ARC_PREFETCH;
			zbookmark_t zb;
			zb.zb_objset = dn->dn_objset->os_dsl_dataset ?
			    dn->dn_objset->os_dsl_dataset->ds_object : 0;
			zb.zb_object = dn->dn_object;
			zb.zb_level = 0;
			zb.zb_blkid = blkid;

			if (db)
				pbuf = db->db_buf;
			else
				pbuf = dn->dn_objset->os_phys_buf;

			(void) arc_read(NULL, dn->dn_objset->os_spa,
			    bp, pbuf, NULL, NULL, ZIO_PRIORITY_ASYNC_READ,
			    ZIO_FLAG_CANFAIL | ZIO_FLAG_SPECULATIVE,
			    &aflags, &zb);
		}
		if (db)
			dbuf_rele(db, NULL);
	}
}

/*
 * Returns with db_holds incremented, and db_mtx not held.
 * Note: dn_struct_rwlock must be held.
 */
int
dbuf_hold_impl(dnode_t *dn, uint8_t level, uint64_t blkid, int fail_sparse,
    void *tag, dmu_buf_impl_t **dbp)
{
	dmu_buf_impl_t *db, *parent = NULL;

	ASSERT(blkid != DB_BONUS_BLKID);
	ASSERT(RW_LOCK_HELD(&dn->dn_struct_rwlock));
	ASSERT3U(dn->dn_nlevels, >, level);

	*dbp = NULL;
top:
	/* dbuf_find() returns with db_mtx held */
	db = dbuf_find(dn, level, blkid);

	if (db == NULL) {
		blkptr_t *bp = NULL;
		int err;

		ASSERT3P(parent, ==, NULL);
		err = dbuf_findbp(dn, level, blkid, fail_sparse, &parent, &bp);
		if (fail_sparse) {
			if (err == 0 && bp && BP_IS_HOLE(bp))
				err = ENOENT;
			if (err) {
				if (parent)
					dbuf_rele(parent, NULL);
				return (err);
			}
		}
		if (err && err != ENOENT)
			return (err);
		db = dbuf_create(dn, level, blkid, parent, bp);
	}

	if (db->db_buf && refcount_is_zero(&db->db_holds)) {
		arc_buf_add_ref(db->db_buf, db);
		if (db->db_buf->b_data == NULL) {
			dbuf_clear(db);
			if (parent) {
				dbuf_rele(parent, NULL);
				parent = NULL;
			}
			goto top;
		}
		ASSERT3P(db->db.db_data, ==, db->db_buf->b_data);
	}

	ASSERT(db->db_buf == NULL || arc_referenced(db->db_buf));

	/*
	 * If this buffer is currently syncing out, and we are are
	 * still referencing it from db_data, we need to make a copy
	 * of it in case we decide we want to dirty it again in this txg.
	 */
	if (db->db_level == 0 && db->db_blkid != DB_BONUS_BLKID &&
	    dn->dn_object != DMU_META_DNODE_OBJECT &&
	    db->db_state == DB_CACHED && db->db_data_pending) {
		dbuf_dirty_record_t *dr = db->db_data_pending;

		if (dr->dt.dl.dr_data == db->db_buf) {
			arc_buf_contents_t type = DBUF_GET_BUFC_TYPE(db);

			dbuf_set_data(db,
			    arc_buf_alloc(db->db_dnode->dn_objset->os_spa,
			    db->db.db_size, db, type));
			bcopy(dr->dt.dl.dr_data->b_data, db->db.db_data,
			    db->db.db_size);
		}
	}

	(void) refcount_add(&db->db_holds, tag);
	dbuf_update_data(db);
	DBUF_VERIFY(db);
	mutex_exit(&db->db_mtx);

	/* NOTE: we can't rele the parent until after we drop the db_mtx */
	if (parent)
		dbuf_rele(parent, NULL);

	ASSERT3P(db->db_dnode, ==, dn);
	ASSERT3U(db->db_blkid, ==, blkid);
	ASSERT3U(db->db_level, ==, level);
	*dbp = db;

	return (0);
}

dmu_buf_impl_t *
dbuf_hold(dnode_t *dn, uint64_t blkid, void *tag)
{
	dmu_buf_impl_t *db;
	int err = dbuf_hold_impl(dn, 0, blkid, FALSE, tag, &db);
	return (err ? NULL : db);
}

dmu_buf_impl_t *
dbuf_hold_level(dnode_t *dn, int level, uint64_t blkid, void *tag)
{
	dmu_buf_impl_t *db;
	int err = dbuf_hold_impl(dn, level, blkid, FALSE, tag, &db);
	return (err ? NULL : db);
}

void
dbuf_create_bonus(dnode_t *dn)
{
	ASSERT(RW_WRITE_HELD(&dn->dn_struct_rwlock));

	ASSERT(dn->dn_bonus == NULL);
	dn->dn_bonus = dbuf_create(dn, 0, DB_BONUS_BLKID, dn->dn_dbuf, NULL);
}

#pragma weak dmu_buf_add_ref = dbuf_add_ref
void
dbuf_add_ref(dmu_buf_impl_t *db, void *tag)
{
	int64_t holds = refcount_add(&db->db_holds, tag);
	ASSERT(holds > 1);
}

#pragma weak dmu_buf_rele = dbuf_rele
void
dbuf_rele(dmu_buf_impl_t *db, void *tag)
{
	int64_t holds;

	mutex_enter(&db->db_mtx);
	DBUF_VERIFY(db);

	holds = refcount_remove(&db->db_holds, tag);
	ASSERT(holds >= 0);

	/*
	 * We can't freeze indirects if there is a possibility that they
	 * may be modified in the current syncing context.
	 */
	if (db->db_buf && holds == (db->db_level == 0 ? db->db_dirtycnt : 0))
		arc_buf_freeze(db->db_buf);

	if (holds == db->db_dirtycnt &&
	    db->db_level == 0 && db->db_immediate_evict)
		dbuf_evict_user(db);

	if (holds == 0) {
		if (db->db_blkid == DB_BONUS_BLKID) {
			mutex_exit(&db->db_mtx);
			dnode_rele(db->db_dnode, db);
		} else if (db->db_buf == NULL) {
			/*
			 * This is a special case: we never associated this
			 * dbuf with any data allocated from the ARC.
			 */
			ASSERT(db->db_state == DB_UNCACHED ||
			    db->db_state == DB_NOFILL);
			dbuf_evict(db);
		} else if (arc_released(db->db_buf)) {
			arc_buf_t *buf = db->db_buf;
			/*
			 * This dbuf has anonymous data associated with it.
			 */
			dbuf_set_data(db, NULL);
			VERIFY(arc_buf_remove_ref(buf, db) == 1);
			dbuf_evict(db);
		} else {
			VERIFY(arc_buf_remove_ref(db->db_buf, db) == 0);
			if (!DBUF_IS_CACHEABLE(db))
				dbuf_clear(db);
			else
				mutex_exit(&db->db_mtx);
		}
	} else {
		mutex_exit(&db->db_mtx);
	}
}

#pragma weak dmu_buf_refcount = dbuf_refcount
uint64_t
dbuf_refcount(dmu_buf_impl_t *db)
{
	return (refcount_count(&db->db_holds));
}

void *
dmu_buf_set_user(dmu_buf_t *db_fake, void *user_ptr, void *user_data_ptr_ptr,
    dmu_buf_evict_func_t *evict_func)
{
	return (dmu_buf_update_user(db_fake, NULL, user_ptr,
	    user_data_ptr_ptr, evict_func));
}

void *
dmu_buf_set_user_ie(dmu_buf_t *db_fake, void *user_ptr, void *user_data_ptr_ptr,
    dmu_buf_evict_func_t *evict_func)
{
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)db_fake;

	db->db_immediate_evict = TRUE;
	return (dmu_buf_update_user(db_fake, NULL, user_ptr,
	    user_data_ptr_ptr, evict_func));
}

void *
dmu_buf_update_user(dmu_buf_t *db_fake, void *old_user_ptr, void *user_ptr,
    void *user_data_ptr_ptr, dmu_buf_evict_func_t *evict_func)
{
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)db_fake;
	ASSERT(db->db_level == 0);

	ASSERT((user_ptr == NULL) == (evict_func == NULL));

	mutex_enter(&db->db_mtx);

	if (db->db_user_ptr == old_user_ptr) {
		db->db_user_ptr = user_ptr;
		db->db_user_data_ptr_ptr = user_data_ptr_ptr;
		db->db_evict_func = evict_func;

		dbuf_update_data(db);
	} else {
		old_user_ptr = db->db_user_ptr;
	}

	mutex_exit(&db->db_mtx);
	return (old_user_ptr);
}

void *
dmu_buf_get_user(dmu_buf_t *db_fake)
{
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)db_fake;
	ASSERT(!refcount_is_zero(&db->db_holds));

	return (db->db_user_ptr);
}

static void
dbuf_check_blkptr(dnode_t *dn, dmu_buf_impl_t *db)
{
	/* ASSERT(dmu_tx_is_syncing(tx) */
	ASSERT(MUTEX_HELD(&db->db_mtx));

	if (db->db_blkptr != NULL)
		return;

	if (db->db_level == dn->dn_phys->dn_nlevels-1) {
		/*
		 * This buffer was allocated at a time when there was
		 * no available blkptrs from the dnode, or it was
		 * inappropriate to hook it in (i.e., nlevels mis-match).
		 */
		ASSERT(db->db_blkid < dn->dn_phys->dn_nblkptr);
		ASSERT(db->db_parent == NULL);
		db->db_parent = dn->dn_dbuf;
		db->db_blkptr = &dn->dn_phys->dn_blkptr[db->db_blkid];
		DBUF_VERIFY(db);
	} else {
		dmu_buf_impl_t *parent = db->db_parent;
		int epbs = dn->dn_phys->dn_indblkshift - SPA_BLKPTRSHIFT;

		ASSERT(dn->dn_phys->dn_nlevels > 1);
		if (parent == NULL) {
			mutex_exit(&db->db_mtx);
			rw_enter(&dn->dn_struct_rwlock, RW_READER);
			(void) dbuf_hold_impl(dn, db->db_level+1,
			    db->db_blkid >> epbs, FALSE, db, &parent);
			rw_exit(&dn->dn_struct_rwlock);
			mutex_enter(&db->db_mtx);
			db->db_parent = parent;
		}
		db->db_blkptr = (blkptr_t *)parent->db.db_data +
		    (db->db_blkid & ((1ULL << epbs) - 1));
		DBUF_VERIFY(db);
	}
}

static void
dbuf_sync_indirect(dbuf_dirty_record_t *dr, dmu_tx_t *tx)
{
	dmu_buf_impl_t *db = dr->dr_dbuf;
	dnode_t *dn = db->db_dnode;
	zio_t *zio;

	ASSERT(dmu_tx_is_syncing(tx));

	dprintf_dbuf_bp(db, db->db_blkptr, "blkptr=%p", db->db_blkptr);

	mutex_enter(&db->db_mtx);

	ASSERT(db->db_level > 0);
	DBUF_VERIFY(db);

	if (db->db_buf == NULL) {
		mutex_exit(&db->db_mtx);
		(void) dbuf_read(db, NULL, DB_RF_MUST_SUCCEED);
		mutex_enter(&db->db_mtx);
	}
	ASSERT3U(db->db_state, ==, DB_CACHED);
	ASSERT3U(db->db.db_size, ==, 1<<dn->dn_phys->dn_indblkshift);
	ASSERT(db->db_buf != NULL);

	dbuf_check_blkptr(dn, db);

	db->db_data_pending = dr;

	mutex_exit(&db->db_mtx);
	dbuf_write(dr, db->db_buf, tx);

	zio = dr->dr_zio;
	mutex_enter(&dr->dt.di.dr_mtx);
	dbuf_sync_list(&dr->dt.di.dr_children, tx);
	ASSERT(list_head(&dr->dt.di.dr_children) == NULL);
	mutex_exit(&dr->dt.di.dr_mtx);
	zio_nowait(zio);
}

static void
dbuf_sync_leaf(dbuf_dirty_record_t *dr, dmu_tx_t *tx)
{
	arc_buf_t **datap = &dr->dt.dl.dr_data;
	dmu_buf_impl_t *db = dr->dr_dbuf;
	dnode_t *dn = db->db_dnode;
	objset_impl_t *os = dn->dn_objset;
	uint64_t txg = tx->tx_txg;
	int blksz;

	ASSERT(dmu_tx_is_syncing(tx));

	dprintf_dbuf_bp(db, db->db_blkptr, "blkptr=%p", db->db_blkptr);

	mutex_enter(&db->db_mtx);
	/*
	 * To be synced, we must be dirtied.  But we
	 * might have been freed after the dirty.
	 */
	if (db->db_state == DB_UNCACHED) {
		/* This buffer has been freed since it was dirtied */
		ASSERT(db->db.db_data == NULL);
	} else if (db->db_state == DB_FILL) {
		/* This buffer was freed and is now being re-filled */
		ASSERT(db->db.db_data != dr->dt.dl.dr_data);
	} else {
		ASSERT(db->db_state == DB_CACHED || db->db_state == DB_NOFILL);
	}
	DBUF_VERIFY(db);

	/*
	 * If this is a bonus buffer, simply copy the bonus data into the
	 * dnode.  It will be written out when the dnode is synced (and it
	 * will be synced, since it must have been dirty for dbuf_sync to
	 * be called).
	 */
	if (db->db_blkid == DB_BONUS_BLKID) {
		dbuf_dirty_record_t **drp;

		ASSERT(*datap != NULL);
		ASSERT3U(db->db_level, ==, 0);
		ASSERT3U(dn->dn_phys->dn_bonuslen, <=, DN_MAX_BONUSLEN);
		bcopy(*datap, DN_BONUS(dn->dn_phys), dn->dn_phys->dn_bonuslen);
		if (*datap != db->db.db_data) {
			zio_buf_free(*datap, DN_MAX_BONUSLEN);
			arc_space_return(DN_MAX_BONUSLEN);
		}
		db->db_data_pending = NULL;
		drp = &db->db_last_dirty;
		while (*drp != dr)
			drp = &(*drp)->dr_next;
		ASSERT(dr->dr_next == NULL);
		*drp = dr->dr_next;
		kmem_free(dr, sizeof (dbuf_dirty_record_t));
		ASSERT(db->db_dirtycnt > 0);
		db->db_dirtycnt -= 1;
		mutex_exit(&db->db_mtx);
		dbuf_rele(db, (void *)(uintptr_t)txg);
		return;
	}

	/*
	 * This function may have dropped the db_mtx lock allowing a dmu_sync
	 * operation to sneak in. As a result, we need to ensure that we
	 * don't check the dr_override_state until we have returned from
	 * dbuf_check_blkptr.
	 */
	dbuf_check_blkptr(dn, db);

	/*
	 * If this buffer is in the middle of an immdiate write,
	 * wait for the synchronous IO to complete.
	 */
	while (dr->dt.dl.dr_override_state == DR_IN_DMU_SYNC) {
		ASSERT(dn->dn_object != DMU_META_DNODE_OBJECT);
		cv_wait(&db->db_changed, &db->db_mtx);
		ASSERT(dr->dt.dl.dr_override_state != DR_NOT_OVERRIDDEN);
	}

	/*
	 * If this dbuf has already been written out via an immediate write,
	 * just complete the write by copying over the new block pointer and
	 * updating the accounting via the write-completion functions.
	 */
	if (dr->dt.dl.dr_override_state == DR_OVERRIDDEN) {
		zio_t zio_fake;

		zio_fake.io_private = &db;
		zio_fake.io_error = 0;
		zio_fake.io_bp = db->db_blkptr;
		zio_fake.io_bp_orig = *db->db_blkptr;
		zio_fake.io_txg = txg;
		zio_fake.io_flags = 0;

		*db->db_blkptr = dr->dt.dl.dr_overridden_by;
		dr->dt.dl.dr_override_state = DR_NOT_OVERRIDDEN;
		db->db_data_pending = dr;
		dr->dr_zio = &zio_fake;
		mutex_exit(&db->db_mtx);

		ASSERT(!DVA_EQUAL(BP_IDENTITY(zio_fake.io_bp),
		    BP_IDENTITY(&zio_fake.io_bp_orig)) ||
		    BP_IS_HOLE(zio_fake.io_bp));

		if (BP_IS_OLDER(&zio_fake.io_bp_orig, txg))
			(void) dsl_dataset_block_kill(os->os_dsl_dataset,
			    &zio_fake.io_bp_orig, dn->dn_zio, tx);

		dbuf_write_ready(&zio_fake, db->db_buf, db);
		dbuf_write_done(&zio_fake, db->db_buf, db);

		return;
	}

	if (db->db_state != DB_NOFILL) {
		blksz = arc_buf_size(*datap);

		if (dn->dn_object != DMU_META_DNODE_OBJECT) {
			/*
			 * If this buffer is currently "in use" (i.e., there
			 * are active holds and db_data still references it),
			 * then make a copy before we start the write so that
			 * any modifications from the open txg will not leak
			 * into this write.
			 *
			 * NOTE: this copy does not need to be made for
			 * objects only modified in the syncing context (e.g.
			 * DNONE_DNODE blocks).
			 */
			if (refcount_count(&db->db_holds) > 1 &&
			    *datap == db->db_buf) {
				arc_buf_contents_t type =
				    DBUF_GET_BUFC_TYPE(db);
				*datap =
				    arc_buf_alloc(os->os_spa, blksz, db, type);
				bcopy(db->db.db_data, (*datap)->b_data, blksz);
			}
		}

		ASSERT(*datap != NULL);
	}
	db->db_data_pending = dr;

	mutex_exit(&db->db_mtx);

	dbuf_write(dr, *datap, tx);

	ASSERT(!list_link_active(&dr->dr_dirty_node));
	if (dn->dn_object == DMU_META_DNODE_OBJECT)
		list_insert_tail(&dn->dn_dirty_records[txg&TXG_MASK], dr);
	else
		zio_nowait(dr->dr_zio);
}

void
dbuf_sync_list(list_t *list, dmu_tx_t *tx)
{
	dbuf_dirty_record_t *dr;

	while (dr = list_head(list)) {
		if (dr->dr_zio != NULL) {
			/*
			 * If we find an already initialized zio then we
			 * are processing the meta-dnode, and we have finished.
			 * The dbufs for all dnodes are put back on the list
			 * during processing, so that we can zio_wait()
			 * these IOs after initiating all child IOs.
			 */
			ASSERT3U(dr->dr_dbuf->db.db_object, ==,
			    DMU_META_DNODE_OBJECT);
			break;
		}
		list_remove(list, dr);
		if (dr->dr_dbuf->db_level > 0)
			dbuf_sync_indirect(dr, tx);
		else
			dbuf_sync_leaf(dr, tx);
	}
}

static void
dbuf_write(dbuf_dirty_record_t *dr, arc_buf_t *data, dmu_tx_t *tx)
{
	dmu_buf_impl_t *db = dr->dr_dbuf;
	dnode_t *dn = db->db_dnode;
	objset_impl_t *os = dn->dn_objset;
	dmu_buf_impl_t *parent = db->db_parent;
	uint64_t txg = tx->tx_txg;
	zbookmark_t zb;
	writeprops_t wp = { 0 };
	zio_t *zio;

	if (!BP_IS_HOLE(db->db_blkptr) &&
	    (db->db_level > 0 || dn->dn_type == DMU_OT_DNODE)) {
		/*
		 * Private object buffers are released here rather
		 * than in dbuf_dirty() since they are only modified
		 * in the syncing context and we don't want the
		 * overhead of making multiple copies of the data.
		 */
		arc_release(data, db);
	} else if (db->db_state != DB_NOFILL) {
		ASSERT(arc_released(data));
		/* XXX why do we need to thaw here? */
		arc_buf_thaw(data);
	}

	if (parent != dn->dn_dbuf) {
		ASSERT(parent && parent->db_data_pending);
		ASSERT(db->db_level == parent->db_level-1);
		ASSERT(arc_released(parent->db_buf));
		zio = parent->db_data_pending->dr_zio;
	} else {
		ASSERT(db->db_level == dn->dn_phys->dn_nlevels-1);
		ASSERT3P(db->db_blkptr, ==,
		    &dn->dn_phys->dn_blkptr[db->db_blkid]);
		zio = dn->dn_zio;
	}

	ASSERT(db->db_level == 0 || data == db->db_buf);
	ASSERT3U(db->db_blkptr->blk_birth, <=, txg);
	ASSERT(zio);

	zb.zb_objset = os->os_dsl_dataset ? os->os_dsl_dataset->ds_object : 0;
	zb.zb_object = db->db.db_object;
	zb.zb_level = db->db_level;
	zb.zb_blkid = db->db_blkid;

	wp.wp_type = dn->dn_type;
	wp.wp_level = db->db_level;
	wp.wp_copies = os->os_copies;
	wp.wp_dncompress = dn->dn_compress;
	wp.wp_oscompress = os->os_compress;
	wp.wp_dnchecksum = dn->dn_checksum;
	wp.wp_oschecksum = os->os_checksum;

	if (BP_IS_OLDER(db->db_blkptr, txg))
		(void) dsl_dataset_block_kill(
		    os->os_dsl_dataset, db->db_blkptr, zio, tx);

	if (db->db_state == DB_NOFILL) {
		zio_prop_t zp = { 0 };

		write_policy(os->os_spa, &wp, &zp);
		dr->dr_zio = zio_write(zio, os->os_spa,
		    txg, db->db_blkptr, NULL,
		    db->db.db_size, &zp, dbuf_skip_write_ready,
		    dbuf_skip_write_done, db, ZIO_PRIORITY_ASYNC_WRITE,
		    ZIO_FLAG_MUSTSUCCEED, &zb);
	} else {
		dr->dr_zio = arc_write(zio, os->os_spa, &wp,
		    DBUF_IS_L2CACHEABLE(db), txg, db->db_blkptr,
		    data, dbuf_write_ready, dbuf_write_done, db,
		    ZIO_PRIORITY_ASYNC_WRITE, ZIO_FLAG_MUSTSUCCEED, &zb);
	}
}

/* wrapper function for dbuf_write_ready bypassing ARC */
static void
dbuf_skip_write_ready(zio_t *zio)
{
	blkptr_t *bp = zio->io_bp;

	if (!BP_IS_GANG(bp))
		zio_skip_write(zio);

	dbuf_write_ready(zio, NULL, zio->io_private);
}

/* wrapper function for dbuf_write_done bypassing ARC */
static void
dbuf_skip_write_done(zio_t *zio)
{
	dbuf_write_done(zio, NULL, zio->io_private);
}

/* ARGSUSED */
static void
dbuf_write_ready(zio_t *zio, arc_buf_t *buf, void *vdb)
{
	dmu_buf_impl_t *db = vdb;
	dnode_t *dn = db->db_dnode;
	objset_impl_t *os = dn->dn_objset;
	blkptr_t *bp = zio->io_bp;
	blkptr_t *bp_orig = &zio->io_bp_orig;
	uint64_t fill = 0;
	int old_size, new_size, i;

	ASSERT(db->db_blkptr == bp);

	dprintf_dbuf_bp(db, bp_orig, "bp_orig: %s", "");

	old_size = bp_get_dasize(os->os_spa, bp_orig);
	new_size = bp_get_dasize(os->os_spa, bp);

	dnode_diduse_space(dn, new_size - old_size);

	if (BP_IS_HOLE(bp)) {
		dsl_dataset_t *ds = os->os_dsl_dataset;
		dmu_tx_t *tx = os->os_synctx;

		if (bp_orig->blk_birth == tx->tx_txg)
			(void) dsl_dataset_block_kill(ds, bp_orig, zio, tx);
		ASSERT3U(bp->blk_fill, ==, 0);
		return;
	}

	ASSERT(BP_GET_TYPE(bp) == dn->dn_type);
	ASSERT(BP_GET_LEVEL(bp) == db->db_level);

	mutex_enter(&db->db_mtx);

	if (db->db_level == 0) {
		mutex_enter(&dn->dn_mtx);
		if (db->db_blkid > dn->dn_phys->dn_maxblkid)
			dn->dn_phys->dn_maxblkid = db->db_blkid;
		mutex_exit(&dn->dn_mtx);

		if (dn->dn_type == DMU_OT_DNODE) {
			dnode_phys_t *dnp = db->db.db_data;
			for (i = db->db.db_size >> DNODE_SHIFT; i > 0;
			    i--, dnp++) {
				if (dnp->dn_type != DMU_OT_NONE)
					fill++;
			}
		} else {
			fill = 1;
		}
	} else {
		blkptr_t *ibp = db->db.db_data;
		ASSERT3U(db->db.db_size, ==, 1<<dn->dn_phys->dn_indblkshift);
		for (i = db->db.db_size >> SPA_BLKPTRSHIFT; i > 0; i--, ibp++) {
			if (BP_IS_HOLE(ibp))
				continue;
			ASSERT3U(BP_GET_LSIZE(ibp), ==,
			    db->db_level == 1 ? dn->dn_datablksz :
			    (1<<dn->dn_phys->dn_indblkshift));
			fill += ibp->blk_fill;
		}
	}

	bp->blk_fill = fill;

	mutex_exit(&db->db_mtx);

	if (zio->io_flags & ZIO_FLAG_IO_REWRITE) {
		ASSERT(DVA_EQUAL(BP_IDENTITY(bp), BP_IDENTITY(bp_orig)));
	} else {
		dsl_dataset_t *ds = os->os_dsl_dataset;
		dmu_tx_t *tx = os->os_synctx;

		if (bp_orig->blk_birth == tx->tx_txg)
			(void) dsl_dataset_block_kill(ds, bp_orig, zio, tx);
		dsl_dataset_block_born(ds, bp, tx);
	}
}

/* ARGSUSED */
static void
dbuf_write_done(zio_t *zio, arc_buf_t *buf, void *vdb)
{
	dmu_buf_impl_t *db = vdb;
	uint64_t txg = zio->io_txg;
	dbuf_dirty_record_t **drp, *dr;

	ASSERT3U(zio->io_error, ==, 0);

	mutex_enter(&db->db_mtx);

	drp = &db->db_last_dirty;
	while ((dr = *drp) != db->db_data_pending)
		drp = &dr->dr_next;
	ASSERT(!list_link_active(&dr->dr_dirty_node));
	ASSERT(dr->dr_txg == txg);
	ASSERT(dr->dr_next == NULL);
	*drp = dr->dr_next;

	if (db->db_level == 0) {
		ASSERT(db->db_blkid != DB_BONUS_BLKID);
		ASSERT(dr->dt.dl.dr_override_state == DR_NOT_OVERRIDDEN);

		if (db->db_state != DB_NOFILL) {
			if (dr->dt.dl.dr_data != db->db_buf)
				VERIFY(arc_buf_remove_ref(dr->dt.dl.dr_data,
				    db) == 1);
			else if (!BP_IS_HOLE(db->db_blkptr))
				arc_set_callback(db->db_buf, dbuf_do_evict, db);
			else
				ASSERT(arc_released(db->db_buf));
		}
	} else {
		dnode_t *dn = db->db_dnode;

		ASSERT(list_head(&dr->dt.di.dr_children) == NULL);
		ASSERT3U(db->db.db_size, ==, 1<<dn->dn_phys->dn_indblkshift);
		if (!BP_IS_HOLE(db->db_blkptr)) {
			int epbs =
			    dn->dn_phys->dn_indblkshift - SPA_BLKPTRSHIFT;
			ASSERT3U(BP_GET_LSIZE(db->db_blkptr), ==,
			    db->db.db_size);
			ASSERT3U(dn->dn_phys->dn_maxblkid
			    >> (db->db_level * epbs), >=, db->db_blkid);
			arc_set_callback(db->db_buf, dbuf_do_evict, db);
		}
		mutex_destroy(&dr->dt.di.dr_mtx);
		list_destroy(&dr->dt.di.dr_children);
	}
	kmem_free(dr, sizeof (dbuf_dirty_record_t));

	cv_broadcast(&db->db_changed);
	ASSERT(db->db_dirtycnt > 0);
	db->db_dirtycnt -= 1;
	db->db_data_pending = NULL;
	mutex_exit(&db->db_mtx);

	dprintf_dbuf_bp(db, zio->io_bp, "bp: %s", "");

	dbuf_rele(db, (void *)(uintptr_t)txg);
}
