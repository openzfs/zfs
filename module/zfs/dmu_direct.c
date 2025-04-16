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


#include <sys/dmu.h>
#include <sys/dmu_impl.h>
#include <sys/dbuf.h>
#include <sys/dnode.h>
#include <sys/zfs_context.h>
#include <sys/zfs_racct.h>
#include <sys/dsl_dataset.h>
#include <sys/dmu_objset.h>

static abd_t *
make_abd_for_dbuf(dmu_buf_impl_t *db, abd_t *data, uint64_t offset,
    uint64_t size)
{
	size_t buf_size = db->db.db_size;
	abd_t *pre_buf = NULL, *post_buf = NULL, *mbuf = NULL;
	size_t buf_off = 0;

	ASSERT(MUTEX_HELD(&db->db_mtx));

	if (offset > db->db.db_offset) {
		size_t pre_size = offset - db->db.db_offset;
		pre_buf = abd_alloc_for_io(pre_size, B_TRUE);
		buf_size -= pre_size;
		buf_off = 0;
	} else {
		buf_off = db->db.db_offset - offset;
		size -= buf_off;
	}

	if (size < buf_size) {
		size_t post_size = buf_size - size;
		post_buf = abd_alloc_for_io(post_size, B_TRUE);
		buf_size -= post_size;
	}

	ASSERT3U(buf_size, >, 0);
	abd_t *buf = abd_get_offset_size(data, buf_off, buf_size);

	if (pre_buf || post_buf) {
		mbuf = abd_alloc_gang();
		if (pre_buf)
			abd_gang_add(mbuf, pre_buf, B_TRUE);
		abd_gang_add(mbuf, buf, B_TRUE);
		if (post_buf)
			abd_gang_add(mbuf, post_buf, B_TRUE);
	} else {
		mbuf = buf;
	}

	return (mbuf);
}

static void
dmu_read_abd_done(zio_t *zio)
{
	abd_free(zio->io_abd);
}

static void
dmu_write_direct_ready(zio_t *zio)
{
	dmu_sync_ready(zio, NULL, zio->io_private);
}

static void
dmu_write_direct_done(zio_t *zio)
{
	dmu_sync_arg_t *dsa = zio->io_private;
	dbuf_dirty_record_t *dr = dsa->dsa_dr;
	dmu_buf_impl_t *db = dr->dr_dbuf;

	abd_free(zio->io_abd);

	mutex_enter(&db->db_mtx);
	ASSERT3P(db->db_buf, ==, NULL);
	ASSERT3P(dr->dt.dl.dr_data, ==, NULL);
	ASSERT3P(db->db.db_data, ==, NULL);
	db->db_state = DB_UNCACHED;
	mutex_exit(&db->db_mtx);

	dmu_sync_done(zio, NULL, zio->io_private);

	if (zio->io_error != 0) {
		if (zio->io_flags & ZIO_FLAG_DIO_CHKSUM_ERR)
			ASSERT3U(zio->io_error, ==, EIO);

		/*
		 * In the event of an I/O error this block has been freed in
		 * zio_done() through zio_dva_unallocate(). Calling
		 * dmu_sync_done() above set dr_override_state to
		 * DR_NOT_OVERRIDDEN. In this case when dbuf_undirty() calls
		 * dbuf_unoverride(), it will skip doing zio_free() to free
		 * this block as that was already taken care of.
		 *
		 * Since we are undirtying the record in open-context, we must
		 * have a hold on the db, so it should never be evicted after
		 * calling dbuf_undirty().
		 */
		mutex_enter(&db->db_mtx);
		VERIFY3B(dbuf_undirty(db, dsa->dsa_tx), ==, B_FALSE);
		mutex_exit(&db->db_mtx);
	}

	kmem_free(zio->io_bp, sizeof (blkptr_t));
	zio->io_bp = NULL;
}

int
dmu_write_direct(zio_t *pio, dmu_buf_impl_t *db, abd_t *data, dmu_tx_t *tx)
{
	objset_t *os = db->db_objset;
	dsl_dataset_t *ds = dmu_objset_ds(os);
	zbookmark_phys_t zb;
	dbuf_dirty_record_t *dr_head;

	SET_BOOKMARK(&zb, ds->ds_object,
	    db->db.db_object, db->db_level, db->db_blkid);

	DB_DNODE_ENTER(db);
	zio_prop_t zp;
	dmu_write_policy(os, DB_DNODE(db), db->db_level,
	    WP_DMU_SYNC | WP_DIRECT_WR, &zp);
	DB_DNODE_EXIT(db);

	/*
	 * Dirty this dbuf with DB_NOFILL since we will not have any data
	 * associated with the dbuf.
	 */
	dmu_buf_will_clone_or_dio(&db->db, tx);

	mutex_enter(&db->db_mtx);

	uint64_t txg = dmu_tx_get_txg(tx);
	ASSERT3U(txg, >, spa_last_synced_txg(os->os_spa));
	ASSERT3U(txg, >, spa_syncing_txg(os->os_spa));

	dr_head = list_head(&db->db_dirty_records);
	ASSERT3U(dr_head->dr_txg, ==, txg);
	dr_head->dt.dl.dr_diowrite = B_TRUE;
	dr_head->dr_accounted = db->db.db_size;

	blkptr_t *bp = kmem_alloc(sizeof (blkptr_t), KM_SLEEP);
	if (db->db_blkptr != NULL) {
		/*
		 * Fill in bp with the current block pointer so that
		 * the nopwrite code can check if we're writing the same
		 * data that's already on disk.
		 */
		*bp = *db->db_blkptr;
	} else {
		memset(bp, 0, sizeof (blkptr_t));
	}

	/*
	 * Disable nopwrite if the current block pointer could change
	 * before this TXG syncs.
	 */
	if (list_next(&db->db_dirty_records, dr_head) != NULL)
		zp.zp_nopwrite = B_FALSE;

	ASSERT0(dr_head->dt.dl.dr_has_raw_params);
	ASSERT3S(dr_head->dt.dl.dr_override_state, ==, DR_NOT_OVERRIDDEN);
	dr_head->dt.dl.dr_override_state = DR_IN_DMU_SYNC;

	mutex_exit(&db->db_mtx);

	dmu_objset_willuse_space(os, dr_head->dr_accounted, tx);

	dmu_sync_arg_t *dsa = kmem_zalloc(sizeof (dmu_sync_arg_t), KM_SLEEP);
	dsa->dsa_dr = dr_head;
	dsa->dsa_tx = tx;

	zio_t *zio = zio_write(pio, os->os_spa, txg, bp, data,
	    db->db.db_size, db->db.db_size, &zp,
	    dmu_write_direct_ready, NULL, dmu_write_direct_done, dsa,
	    ZIO_PRIORITY_SYNC_WRITE, ZIO_FLAG_CANFAIL, &zb);

	if (pio == NULL)
		return (zio_wait(zio));

	zio_nowait(zio);

	return (0);
}

int
dmu_write_abd(dnode_t *dn, uint64_t offset, uint64_t size,
    abd_t *data, uint32_t flags, dmu_tx_t *tx)
{
	dmu_buf_t **dbp;
	spa_t *spa = dn->dn_objset->os_spa;
	int numbufs, err;

	ASSERT(flags & DMU_DIRECTIO);

	err = dmu_buf_hold_array_by_dnode(dn, offset,
	    size, B_FALSE, FTAG, &numbufs, &dbp, flags);
	if (err)
		return (err);

	zio_t *pio = zio_root(spa, NULL, NULL, ZIO_FLAG_CANFAIL);

	for (int i = 0; i < numbufs && err == 0; i++) {
		dmu_buf_impl_t *db = (dmu_buf_impl_t *)dbp[i];

		abd_t *abd = abd_get_offset_size(data,
		    db->db.db_offset - offset, dn->dn_datablksz);

		zfs_racct_write(spa, db->db.db_size, 1, flags);
		err = dmu_write_direct(pio, db, abd, tx);
		ASSERT0(err);
	}

	err = zio_wait(pio);

	/*
	 * The dbuf must be held until the Direct I/O write has completed in
	 * the event there was any errors and dbuf_undirty() was called.
	 */
	dmu_buf_rele_array(dbp, numbufs, FTAG);

	return (err);
}

int
dmu_read_abd(dnode_t *dn, uint64_t offset, uint64_t size,
    abd_t *data, uint32_t flags)
{
	objset_t *os = dn->dn_objset;
	spa_t *spa = os->os_spa;
	dmu_buf_t **dbp;
	int numbufs, err;

	ASSERT(flags & DMU_DIRECTIO);

	err = dmu_buf_hold_array_by_dnode(dn, offset,
	    size, B_FALSE, FTAG, &numbufs, &dbp, flags);
	if (err)
		return (err);

	zio_t *rio = zio_root(spa, NULL, NULL, ZIO_FLAG_CANFAIL);

	for (int i = 0; i < numbufs; i++) {
		dmu_buf_impl_t *db = (dmu_buf_impl_t *)dbp[i];
		abd_t *mbuf;
		zbookmark_phys_t zb;
		blkptr_t *bp;

		mutex_enter(&db->db_mtx);

		SET_BOOKMARK(&zb, dmu_objset_ds(os)->ds_object,
		    db->db.db_object, db->db_level, db->db_blkid);

		/*
		 * If there is another read for this dbuf, we will wait for
		 * that to complete first before checking the db_state below.
		 */
		while (db->db_state == DB_READ)
			cv_wait(&db->db_changed, &db->db_mtx);

		err = dmu_buf_get_bp_from_dbuf(db, &bp);
		if (err) {
			mutex_exit(&db->db_mtx);
			goto error;
		}

		/*
		 * There is no need to read if this is a hole or the data is
		 * cached. This will not be considered a direct read for IO
		 * accounting in the same way that an ARC hit is not counted.
		 */
		if (bp == NULL || BP_IS_HOLE(bp) || db->db_state == DB_CACHED) {
			size_t aoff = offset < db->db.db_offset ?
			    db->db.db_offset - offset : 0;
			size_t boff = offset > db->db.db_offset ?
			    offset - db->db.db_offset : 0;
			size_t len = MIN(size - aoff, db->db.db_size - boff);

			if (db->db_state == DB_CACHED) {
				/*
				 * We need to untransformed the ARC buf data
				 * before we copy it over.
				 */
				err = dmu_buf_untransform_direct(db, spa);
				ASSERT0(err);
				abd_copy_from_buf_off(data,
				    (char *)db->db.db_data + boff, aoff, len);
			} else {
				abd_zero_off(data, aoff, len);
			}

			mutex_exit(&db->db_mtx);
			continue;
		}

		mbuf = make_abd_for_dbuf(db, data, offset, size);
		ASSERT3P(mbuf, !=, NULL);

		/*
		 * The dbuf mutex (db_mtx) must be held when creating the ZIO
		 * for the read. The BP returned from
		 * dmu_buf_get_bp_from_dbuf() could be from a pending block
		 * clone or a yet to be synced Direct I/O write that is in the
		 * dbuf's dirty record. When zio_read() is called, zio_create()
		 * will make a copy of the BP. However, if zio_read() is called
		 * without the mutex being held then the dirty record from the
		 * dbuf could be freed in dbuf_write_done() resulting in garbage
		 * being set for the zio BP.
		 */
		zio_t *cio = zio_read(rio, spa, bp, mbuf, db->db.db_size,
		    dmu_read_abd_done, NULL, ZIO_PRIORITY_SYNC_READ,
		    ZIO_FLAG_CANFAIL | ZIO_FLAG_DIO_READ, &zb);
		mutex_exit(&db->db_mtx);

		zfs_racct_read(spa, db->db.db_size, 1, flags);
		zio_nowait(cio);
	}

	dmu_buf_rele_array(dbp, numbufs, FTAG);

	return (zio_wait(rio));

error:
	dmu_buf_rele_array(dbp, numbufs, FTAG);
	(void) zio_wait(rio);
	return (err);
}

#ifdef _KERNEL
int
dmu_read_uio_direct(dnode_t *dn, zfs_uio_t *uio, uint64_t size)
{
	offset_t offset = zfs_uio_offset(uio);
	offset_t page_index = (offset - zfs_uio_soffset(uio)) >> PAGESHIFT;
	int err;

	ASSERT(uio->uio_extflg & UIO_DIRECT);
	ASSERT3U(page_index, <, uio->uio_dio.npages);

	abd_t *data = abd_alloc_from_pages(&uio->uio_dio.pages[page_index],
	    offset & (PAGESIZE - 1), size);
	err = dmu_read_abd(dn, offset, size, data, DMU_DIRECTIO);
	abd_free(data);

	if (err == 0)
		zfs_uioskip(uio, size);

	return (err);
}

int
dmu_write_uio_direct(dnode_t *dn, zfs_uio_t *uio, uint64_t size, dmu_tx_t *tx)
{
	offset_t offset = zfs_uio_offset(uio);
	offset_t page_index = (offset - zfs_uio_soffset(uio)) >> PAGESHIFT;
	int err;

	ASSERT(uio->uio_extflg & UIO_DIRECT);
	ASSERT3U(page_index, <, uio->uio_dio.npages);

	abd_t *data = abd_alloc_from_pages(&uio->uio_dio.pages[page_index],
	    offset & (PAGESIZE - 1), size);
	err = dmu_write_abd(dn, offset, size, data, DMU_DIRECTIO, tx);
	abd_free(data);

	if (err == 0)
		zfs_uioskip(uio, size);

	return (err);
}
#endif /* _KERNEL */

EXPORT_SYMBOL(dmu_read_uio_direct);
EXPORT_SYMBOL(dmu_write_uio_direct);
