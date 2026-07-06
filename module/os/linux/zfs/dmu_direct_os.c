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
 *
 * Copyright 2026, tiehexue <tiehexue@hotmail.com>. All rights reserved.
 *
 */

/*
 * Linux async Direct I/O variants for the DMU layer.
 *
 * dmu_read_abd_async() / dmu_write_abd_async() submit I/O via the ZIO
 * pipeline and return immediately.  Completions fire from ZIO taskq
 * context via caller-provided callbacks (dmu_abd_done_func_t).
 *
 * These are Linux-only because only the Linux VFS layer has async
 * kiocb / -EIOCBQUEUED infrastructure that benefits from non-blocking
 * DMU entry points.
 */

#include <sys/dmu.h>
#include <sys/dmu_impl.h>
#include <sys/dmu_direct_os.h>
#include <sys/dbuf.h>
#include <sys/dnode.h>
#include <sys/zfs_context.h>
#include <sys/zfs_racct.h>
#include <sys/dsl_dataset.h>
#include <sys/dmu_objset.h>

/*
 * Shared async state for dmu_read_abd_async() and dmu_write_abd_async().
 */
struct dmu_abd_async_state {
	dmu_buf_t	**ds_dbp;
	int		ds_numbufs;
	dmu_abd_done_func_t *ds_done;
	void		*ds_done_arg;
};

static void
dmu_abd_async_done(zio_t *zio)
{
	struct dmu_abd_async_state *ds = zio->io_private;
	int error = zio->io_error;

	dmu_buf_rele_array(ds->ds_dbp, ds->ds_numbufs, FTAG);
	ds->ds_done(ds->ds_done_arg, error);
	kmem_free(ds, sizeof (*ds));
}

/*
 * Asynchronous variant of dmu_read_abd().  Same logic but returns
 * immediately after dispatching child zios.  The caller's callback
 * fires from ZIO taskq context when all children complete.
 */
int
dmu_read_abd_async(dnode_t *dn, uint64_t offset, uint64_t size,
    abd_t *data, dmu_flags_t flags,
    dmu_abd_done_func_t *done, void *done_arg)
{
	objset_t *os = dn->dn_objset;
	spa_t *spa = os->os_spa;
	dmu_buf_t **dbp;
	int numbufs, err;

	ASSERT(flags & DMU_DIRECTIO);
	ASSERT3P(done, !=, NULL);

	err = dmu_buf_hold_array_by_dnode(dn, offset,
	    size, B_FALSE, FTAG, &numbufs, &dbp, flags);
	if (err)
		return (err);

	struct dmu_abd_async_state *ds =
	    kmem_alloc(sizeof (*ds), KM_SLEEP);
	ds->ds_dbp = dbp;
	ds->ds_numbufs = numbufs;
	ds->ds_done = done;
	ds->ds_done_arg = done_arg;

	zio_t *rio = zio_root(spa, dmu_abd_async_done, ds,
	    ZIO_FLAG_CANFAIL);

	for (int i = 0; i < numbufs; i++) {
		dmu_buf_impl_t *db = (dmu_buf_impl_t *)dbp[i];
		abd_t *mbuf;
		zbookmark_phys_t zb;
		blkptr_t *bp;

		mutex_enter(&db->db_mtx);
		SET_BOOKMARK(&zb, dmu_objset_ds(os)->ds_object,
		    db->db.db_object, db->db_level, db->db_blkid);

		while (db->db_state == DB_READ)
			cv_wait(&db->db_changed, &db->db_mtx);

		err = dmu_buf_get_bp_from_dbuf(db, &bp);
		if (err) {
			mutex_exit(&db->db_mtx);
			goto async_error;
		}

		if (bp == NULL || BP_IS_HOLE(bp) || db->db_state == DB_CACHED) {
			size_t aoff = offset < db->db.db_offset ?
			    db->db.db_offset - offset : 0;
			size_t boff = offset > db->db.db_offset ?
			    offset - db->db.db_offset : 0;
			size_t len = MIN(size - aoff, db->db.db_size - boff);

			if (db->db_state == DB_CACHED) {
				err = dmu_buf_untransform_direct(db, spa);
				if (err) {
					mutex_exit(&db->db_mtx);
					goto async_error;
				}
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

		zio_t *cio = zio_read(rio, spa, bp, mbuf, db->db.db_size,
		    dmu_read_abd_done, NULL, ZIO_PRIORITY_SYNC_READ,
		    ZIO_FLAG_CANFAIL | ZIO_FLAG_DIO_READ, &zb);
		mutex_exit(&db->db_mtx);

		zfs_racct_read(spa, db->db.db_size, 1, flags);
		zio_nowait(cio);
	}

	/*
	 * Dispatch the root zio to kick off its ZIO_ROOT_PIPELINE.
	 * Without this, the done callback never fires (hang).
	 */
	zio_nowait(rio);
	return (0);

async_error:
	/*
	 * Set the error on the root zio and dispatch it.
	 * The done callback delivers the error and frees
	 * the state, so return 0 to tell the caller the callback owns
	 * cleanup.
	 */
	rio->io_error = err;
	zio_nowait(rio);
	return (0);
}



/*
 * Asynchronous variant of dmu_write_abd().  Dispatches child zios and
 * returns immediately.  The done callback fires from ZIO taskq context
 * when all children complete.  The caller's transaction (tx) is carried
 * through and must be committed by the caller's callback.
 */
int
dmu_write_abd_async(dnode_t *dn, uint64_t offset, uint64_t size,
    abd_t *data, dmu_flags_t flags, dmu_tx_t *tx,
    dmu_abd_done_func_t *done, void *done_arg)
{
	dmu_buf_t **dbp;
	spa_t *spa = dn->dn_objset->os_spa;
	int numbufs, err;

	ASSERT(flags & DMU_DIRECTIO);
	ASSERT3P(done, !=, NULL);

	err = dmu_buf_hold_array_by_dnode(dn, offset,
	    size, B_FALSE, FTAG, &numbufs, &dbp, flags);
	if (err)
		return (err);

	struct dmu_abd_async_state *ds =
	    kmem_alloc(sizeof (*ds), KM_SLEEP);
	ds->ds_dbp = dbp;
	ds->ds_numbufs = numbufs;
	ds->ds_done = done;
	ds->ds_done_arg = done_arg;

	zio_t *pio = zio_root(spa, dmu_abd_async_done, ds,
	    ZIO_FLAG_CANFAIL);

	for (int i = 0; i < numbufs && err == 0; i++) {
		dmu_buf_impl_t *db = (dmu_buf_impl_t *)dbp[i];

		abd_t *abd = abd_get_offset_size(data,
		    db->db.db_offset - offset, dn->dn_datablksz);

		zfs_racct_write(spa, db->db.db_size, 1, flags);
		err = dmu_write_direct(pio, db, abd, tx);
		ASSERT0(err);
	}

	zio_nowait(pio);
	return (0);
}
