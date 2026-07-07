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
 * Asynchronous variant of dmu_read_abd().  Uses the shared
 * dmu_read_abd_dispatch() helper (common code) for the per-dbuf ZIO
 * submission loop; only the async plumbing (root zio callback, state
 * allocation, zio_nowait) lives here in the Linux-specific layer.
 */
int
dmu_read_abd_async(dnode_t *dn, uint64_t offset, uint64_t size,
    abd_t *data, dmu_flags_t flags,
    dmu_abd_done_func_t *done, void *done_arg)
{
	spa_t *spa = dn->dn_objset->os_spa;
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

	err = dmu_read_abd_dispatch(rio, dn, offset, size, data, flags,
	    dbp, numbufs);

	/*
	 * Dispatch the root zio.  On error, dmu_read_abd_dispatch() has
	 * already set rio->io_error; the async done callback will release
	 * dbp and signal the caller.
	 */
	zio_nowait(rio);
	return (0);
}



/*
 * Asynchronous variant of dmu_write_abd().  Uses the shared
 * dmu_write_abd_dispatch() helper (common code) for the per-dbuf
 * ZIO submission loop; only the async plumbing lives here.
 */
int
dmu_write_abd_async(dnode_t *dn, uint64_t offset, uint64_t size,
    abd_t *data, dmu_flags_t flags, dmu_tx_t *tx,
    dmu_abd_done_func_t *done, void *done_arg)
{
	spa_t *spa = dn->dn_objset->os_spa;
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

	zio_t *pio = zio_root(spa, dmu_abd_async_done, ds,
	    ZIO_FLAG_CANFAIL);

	(void) dmu_write_abd_dispatch(pio, dn, offset, size, data, flags, tx,
	    dbp, numbufs);

	zio_nowait(pio);
	return (0);
}
