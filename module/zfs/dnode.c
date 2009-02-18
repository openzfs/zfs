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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/zfs_context.h>
#include <sys/dbuf.h>
#include <sys/dnode.h>
#include <sys/dmu.h>
#include <sys/dmu_impl.h>
#include <sys/dmu_tx.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_dataset.h>
#include <sys/spa.h>
#include <sys/zio.h>
#include <sys/dmu_zfetch.h>

static int free_range_compar(const void *node1, const void *node2);

static kmem_cache_t *dnode_cache;

static dnode_phys_t dnode_phys_zero;

int zfs_default_bs = SPA_MINBLOCKSHIFT;
int zfs_default_ibs = DN_MAX_INDBLKSHIFT;

/* ARGSUSED */
static int
dnode_cons(void *arg, void *unused, int kmflag)
{
	int i;
	dnode_t *dn = arg;
	bzero(dn, sizeof (dnode_t));

	rw_init(&dn->dn_struct_rwlock, NULL, RW_DEFAULT, NULL);
	mutex_init(&dn->dn_mtx, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&dn->dn_dbufs_mtx, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&dn->dn_notxholds, NULL, CV_DEFAULT, NULL);

	refcount_create(&dn->dn_holds);
	refcount_create(&dn->dn_tx_holds);

	for (i = 0; i < TXG_SIZE; i++) {
		avl_create(&dn->dn_ranges[i], free_range_compar,
		    sizeof (free_range_t),
		    offsetof(struct free_range, fr_node));
		list_create(&dn->dn_dirty_records[i],
		    sizeof (dbuf_dirty_record_t),
		    offsetof(dbuf_dirty_record_t, dr_dirty_node));
	}

	list_create(&dn->dn_dbufs, sizeof (dmu_buf_impl_t),
	    offsetof(dmu_buf_impl_t, db_link));

	return (0);
}

/* ARGSUSED */
static void
dnode_dest(void *arg, void *unused)
{
	int i;
	dnode_t *dn = arg;

	rw_destroy(&dn->dn_struct_rwlock);
	mutex_destroy(&dn->dn_mtx);
	mutex_destroy(&dn->dn_dbufs_mtx);
	cv_destroy(&dn->dn_notxholds);
	refcount_destroy(&dn->dn_holds);
	refcount_destroy(&dn->dn_tx_holds);

	for (i = 0; i < TXG_SIZE; i++) {
		avl_destroy(&dn->dn_ranges[i]);
		list_destroy(&dn->dn_dirty_records[i]);
	}

	list_destroy(&dn->dn_dbufs);
}

void
dnode_init(void)
{
	dnode_cache = kmem_cache_create("dnode_t",
	    sizeof (dnode_t),
	    0, dnode_cons, dnode_dest, NULL, NULL, NULL, 0);
}

void
dnode_fini(void)
{
	kmem_cache_destroy(dnode_cache);
}


#ifdef ZFS_DEBUG
void
dnode_verify(dnode_t *dn)
{
	int drop_struct_lock = FALSE;

	ASSERT(dn->dn_phys);
	ASSERT(dn->dn_objset);

	ASSERT(dn->dn_phys->dn_type < DMU_OT_NUMTYPES);

	if (!(zfs_flags & ZFS_DEBUG_DNODE_VERIFY))
		return;

	if (!RW_WRITE_HELD(&dn->dn_struct_rwlock)) {
		rw_enter(&dn->dn_struct_rwlock, RW_READER);
		drop_struct_lock = TRUE;
	}
	if (dn->dn_phys->dn_type != DMU_OT_NONE || dn->dn_allocated_txg != 0) {
		int i;
		ASSERT3U(dn->dn_indblkshift, >=, 0);
		ASSERT3U(dn->dn_indblkshift, <=, SPA_MAXBLOCKSHIFT);
		if (dn->dn_datablkshift) {
			ASSERT3U(dn->dn_datablkshift, >=, SPA_MINBLOCKSHIFT);
			ASSERT3U(dn->dn_datablkshift, <=, SPA_MAXBLOCKSHIFT);
			ASSERT3U(1<<dn->dn_datablkshift, ==, dn->dn_datablksz);
		}
		ASSERT3U(dn->dn_nlevels, <=, 30);
		ASSERT3U(dn->dn_type, <=, DMU_OT_NUMTYPES);
		ASSERT3U(dn->dn_nblkptr, >=, 1);
		ASSERT3U(dn->dn_nblkptr, <=, DN_MAX_NBLKPTR);
		ASSERT3U(dn->dn_bonuslen, <=, DN_MAX_BONUSLEN);
		ASSERT3U(dn->dn_datablksz, ==,
		    dn->dn_datablkszsec << SPA_MINBLOCKSHIFT);
		ASSERT3U(ISP2(dn->dn_datablksz), ==, dn->dn_datablkshift != 0);
		ASSERT3U((dn->dn_nblkptr - 1) * sizeof (blkptr_t) +
		    dn->dn_bonuslen, <=, DN_MAX_BONUSLEN);
		for (i = 0; i < TXG_SIZE; i++) {
			ASSERT3U(dn->dn_next_nlevels[i], <=, dn->dn_nlevels);
		}
	}
	if (dn->dn_phys->dn_type != DMU_OT_NONE)
		ASSERT3U(dn->dn_phys->dn_nlevels, <=, dn->dn_nlevels);
	ASSERT(dn->dn_object == DMU_META_DNODE_OBJECT || dn->dn_dbuf != NULL);
	if (dn->dn_dbuf != NULL) {
		ASSERT3P(dn->dn_phys, ==,
		    (dnode_phys_t *)dn->dn_dbuf->db.db_data +
		    (dn->dn_object % (dn->dn_dbuf->db.db_size >> DNODE_SHIFT)));
	}
	if (drop_struct_lock)
		rw_exit(&dn->dn_struct_rwlock);
}
#endif

void
dnode_byteswap(dnode_phys_t *dnp)
{
	uint64_t *buf64 = (void*)&dnp->dn_blkptr;
	int i;

	if (dnp->dn_type == DMU_OT_NONE) {
		bzero(dnp, sizeof (dnode_phys_t));
		return;
	}

	dnp->dn_datablkszsec = BSWAP_16(dnp->dn_datablkszsec);
	dnp->dn_bonuslen = BSWAP_16(dnp->dn_bonuslen);
	dnp->dn_maxblkid = BSWAP_64(dnp->dn_maxblkid);
	dnp->dn_used = BSWAP_64(dnp->dn_used);

	/*
	 * dn_nblkptr is only one byte, so it's OK to read it in either
	 * byte order.  We can't read dn_bouslen.
	 */
	ASSERT(dnp->dn_indblkshift <= SPA_MAXBLOCKSHIFT);
	ASSERT(dnp->dn_nblkptr <= DN_MAX_NBLKPTR);
	for (i = 0; i < dnp->dn_nblkptr * sizeof (blkptr_t)/8; i++)
		buf64[i] = BSWAP_64(buf64[i]);

	/*
	 * OK to check dn_bonuslen for zero, because it won't matter if
	 * we have the wrong byte order.  This is necessary because the
	 * dnode dnode is smaller than a regular dnode.
	 */
	if (dnp->dn_bonuslen != 0) {
		/*
		 * Note that the bonus length calculated here may be
		 * longer than the actual bonus buffer.  This is because
		 * we always put the bonus buffer after the last block
		 * pointer (instead of packing it against the end of the
		 * dnode buffer).
		 */
		int off = (dnp->dn_nblkptr-1) * sizeof (blkptr_t);
		size_t len = DN_MAX_BONUSLEN - off;
		ASSERT3U(dnp->dn_bonustype, <, DMU_OT_NUMTYPES);
		dmu_ot[dnp->dn_bonustype].ot_byteswap(dnp->dn_bonus + off, len);
	}
}

void
dnode_buf_byteswap(void *vbuf, size_t size)
{
	dnode_phys_t *buf = vbuf;
	int i;

	ASSERT3U(sizeof (dnode_phys_t), ==, (1<<DNODE_SHIFT));
	ASSERT((size & (sizeof (dnode_phys_t)-1)) == 0);

	size >>= DNODE_SHIFT;
	for (i = 0; i < size; i++) {
		dnode_byteswap(buf);
		buf++;
	}
}

static int
free_range_compar(const void *node1, const void *node2)
{
	const free_range_t *rp1 = node1;
	const free_range_t *rp2 = node2;

	if (rp1->fr_blkid < rp2->fr_blkid)
		return (-1);
	else if (rp1->fr_blkid > rp2->fr_blkid)
		return (1);
	else return (0);
}

void
dnode_setbonuslen(dnode_t *dn, int newsize, dmu_tx_t *tx)
{
	ASSERT3U(refcount_count(&dn->dn_holds), >=, 1);

	dnode_setdirty(dn, tx);
	rw_enter(&dn->dn_struct_rwlock, RW_WRITER);
	ASSERT3U(newsize, <=, DN_MAX_BONUSLEN -
	    (dn->dn_nblkptr-1) * sizeof (blkptr_t));
	dn->dn_bonuslen = newsize;
	if (newsize == 0)
		dn->dn_next_bonuslen[tx->tx_txg & TXG_MASK] = DN_ZERO_BONUSLEN;
	else
		dn->dn_next_bonuslen[tx->tx_txg & TXG_MASK] = dn->dn_bonuslen;
	rw_exit(&dn->dn_struct_rwlock);
}

static void
dnode_setdblksz(dnode_t *dn, int size)
{
	ASSERT3U(P2PHASE(size, SPA_MINBLOCKSIZE), ==, 0);
	ASSERT3U(size, <=, SPA_MAXBLOCKSIZE);
	ASSERT3U(size, >=, SPA_MINBLOCKSIZE);
	ASSERT3U(size >> SPA_MINBLOCKSHIFT, <,
	    1<<(sizeof (dn->dn_phys->dn_datablkszsec) * 8));
	dn->dn_datablksz = size;
	dn->dn_datablkszsec = size >> SPA_MINBLOCKSHIFT;
	dn->dn_datablkshift = ISP2(size) ? highbit(size - 1) : 0;
}

static dnode_t *
dnode_create(objset_impl_t *os, dnode_phys_t *dnp, dmu_buf_impl_t *db,
    uint64_t object)
{
	dnode_t *dn = kmem_cache_alloc(dnode_cache, KM_SLEEP);
	(void) dnode_cons(dn, NULL, 0); /* XXX */

	dn->dn_objset = os;
	dn->dn_object = object;
	dn->dn_dbuf = db;
	dn->dn_phys = dnp;

	if (dnp->dn_datablkszsec)
		dnode_setdblksz(dn, dnp->dn_datablkszsec << SPA_MINBLOCKSHIFT);
	dn->dn_indblkshift = dnp->dn_indblkshift;
	dn->dn_nlevels = dnp->dn_nlevels;
	dn->dn_type = dnp->dn_type;
	dn->dn_nblkptr = dnp->dn_nblkptr;
	dn->dn_checksum = dnp->dn_checksum;
	dn->dn_compress = dnp->dn_compress;
	dn->dn_bonustype = dnp->dn_bonustype;
	dn->dn_bonuslen = dnp->dn_bonuslen;
	dn->dn_maxblkid = dnp->dn_maxblkid;

	dmu_zfetch_init(&dn->dn_zfetch, dn);

	ASSERT(dn->dn_phys->dn_type < DMU_OT_NUMTYPES);
	mutex_enter(&os->os_lock);
	list_insert_head(&os->os_dnodes, dn);
	mutex_exit(&os->os_lock);

	arc_space_consume(sizeof (dnode_t), ARC_SPACE_OTHER);
	return (dn);
}

static void
dnode_destroy(dnode_t *dn)
{
	objset_impl_t *os = dn->dn_objset;

#ifdef ZFS_DEBUG
	int i;

	for (i = 0; i < TXG_SIZE; i++) {
		ASSERT(!list_link_active(&dn->dn_dirty_link[i]));
		ASSERT(NULL == list_head(&dn->dn_dirty_records[i]));
		ASSERT(0 == avl_numnodes(&dn->dn_ranges[i]));
	}
	ASSERT(NULL == list_head(&dn->dn_dbufs));
#endif

	mutex_enter(&os->os_lock);
	list_remove(&os->os_dnodes, dn);
	mutex_exit(&os->os_lock);

	if (dn->dn_dirtyctx_firstset) {
		kmem_free(dn->dn_dirtyctx_firstset, 1);
		dn->dn_dirtyctx_firstset = NULL;
	}
	dmu_zfetch_rele(&dn->dn_zfetch);
	if (dn->dn_bonus) {
		mutex_enter(&dn->dn_bonus->db_mtx);
		dbuf_evict(dn->dn_bonus);
		dn->dn_bonus = NULL;
	}
	kmem_cache_free(dnode_cache, dn);
	arc_space_return(sizeof (dnode_t), ARC_SPACE_OTHER);
}

void
dnode_allocate(dnode_t *dn, dmu_object_type_t ot, int blocksize, int ibs,
    dmu_object_type_t bonustype, int bonuslen, dmu_tx_t *tx)
{
	int i;

	if (blocksize == 0)
		blocksize = 1 << zfs_default_bs;
	else if (blocksize > SPA_MAXBLOCKSIZE)
		blocksize = SPA_MAXBLOCKSIZE;
	else
		blocksize = P2ROUNDUP(blocksize, SPA_MINBLOCKSIZE);

	if (ibs == 0)
		ibs = zfs_default_ibs;

	ibs = MIN(MAX(ibs, DN_MIN_INDBLKSHIFT), DN_MAX_INDBLKSHIFT);

	dprintf("os=%p obj=%llu txg=%llu blocksize=%d ibs=%d\n", dn->dn_objset,
	    dn->dn_object, tx->tx_txg, blocksize, ibs);

	ASSERT(dn->dn_type == DMU_OT_NONE);
	ASSERT(bcmp(dn->dn_phys, &dnode_phys_zero, sizeof (dnode_phys_t)) == 0);
	ASSERT(dn->dn_phys->dn_type == DMU_OT_NONE);
	ASSERT(ot != DMU_OT_NONE);
	ASSERT3U(ot, <, DMU_OT_NUMTYPES);
	ASSERT((bonustype == DMU_OT_NONE && bonuslen == 0) ||
	    (bonustype != DMU_OT_NONE && bonuslen != 0));
	ASSERT3U(bonustype, <, DMU_OT_NUMTYPES);
	ASSERT3U(bonuslen, <=, DN_MAX_BONUSLEN);
	ASSERT(dn->dn_type == DMU_OT_NONE);
	ASSERT3U(dn->dn_maxblkid, ==, 0);
	ASSERT3U(dn->dn_allocated_txg, ==, 0);
	ASSERT3U(dn->dn_assigned_txg, ==, 0);
	ASSERT(refcount_is_zero(&dn->dn_tx_holds));
	ASSERT3U(refcount_count(&dn->dn_holds), <=, 1);
	ASSERT3P(list_head(&dn->dn_dbufs), ==, NULL);

	for (i = 0; i < TXG_SIZE; i++) {
		ASSERT3U(dn->dn_next_nlevels[i], ==, 0);
		ASSERT3U(dn->dn_next_indblkshift[i], ==, 0);
		ASSERT3U(dn->dn_next_bonuslen[i], ==, 0);
		ASSERT3U(dn->dn_next_blksz[i], ==, 0);
		ASSERT(!list_link_active(&dn->dn_dirty_link[i]));
		ASSERT3P(list_head(&dn->dn_dirty_records[i]), ==, NULL);
		ASSERT3U(avl_numnodes(&dn->dn_ranges[i]), ==, 0);
	}

	dn->dn_type = ot;
	dnode_setdblksz(dn, blocksize);
	dn->dn_indblkshift = ibs;
	dn->dn_nlevels = 1;
	dn->dn_nblkptr = 1 + ((DN_MAX_BONUSLEN - bonuslen) >> SPA_BLKPTRSHIFT);
	dn->dn_bonustype = bonustype;
	dn->dn_bonuslen = bonuslen;
	dn->dn_checksum = ZIO_CHECKSUM_INHERIT;
	dn->dn_compress = ZIO_COMPRESS_INHERIT;
	dn->dn_dirtyctx = 0;

	dn->dn_free_txg = 0;
	if (dn->dn_dirtyctx_firstset) {
		kmem_free(dn->dn_dirtyctx_firstset, 1);
		dn->dn_dirtyctx_firstset = NULL;
	}

	dn->dn_allocated_txg = tx->tx_txg;

	dnode_setdirty(dn, tx);
	dn->dn_next_indblkshift[tx->tx_txg & TXG_MASK] = ibs;
	dn->dn_next_bonuslen[tx->tx_txg & TXG_MASK] = dn->dn_bonuslen;
	dn->dn_next_blksz[tx->tx_txg & TXG_MASK] = dn->dn_datablksz;
}

void
dnode_reallocate(dnode_t *dn, dmu_object_type_t ot, int blocksize,
    dmu_object_type_t bonustype, int bonuslen, dmu_tx_t *tx)
{
	int i, nblkptr;
	dmu_buf_impl_t *db = NULL;

	ASSERT3U(blocksize, >=, SPA_MINBLOCKSIZE);
	ASSERT3U(blocksize, <=, SPA_MAXBLOCKSIZE);
	ASSERT3U(blocksize % SPA_MINBLOCKSIZE, ==, 0);
	ASSERT(dn->dn_object != DMU_META_DNODE_OBJECT || dmu_tx_private_ok(tx));
	ASSERT(tx->tx_txg != 0);
	ASSERT((bonustype == DMU_OT_NONE && bonuslen == 0) ||
	    (bonustype != DMU_OT_NONE && bonuslen != 0));
	ASSERT3U(bonustype, <, DMU_OT_NUMTYPES);
	ASSERT3U(bonuslen, <=, DN_MAX_BONUSLEN);

	for (i = 0; i < TXG_SIZE; i++)
		ASSERT(!list_link_active(&dn->dn_dirty_link[i]));

	/* clean up any unreferenced dbufs */
	dnode_evict_dbufs(dn);
	ASSERT3P(list_head(&dn->dn_dbufs), ==, NULL);

	/*
	 * XXX I should really have a generation number to tell if we
	 * need to do this...
	 */
	if (blocksize != dn->dn_datablksz ||
	    dn->dn_bonustype != bonustype || dn->dn_bonuslen != bonuslen) {
		/* free all old data */
		dnode_free_range(dn, 0, -1ULL, tx);
	}

	nblkptr = 1 + ((DN_MAX_BONUSLEN - bonuslen) >> SPA_BLKPTRSHIFT);

	/* change blocksize */
	rw_enter(&dn->dn_struct_rwlock, RW_WRITER);
	if (blocksize != dn->dn_datablksz &&
	    (!BP_IS_HOLE(&dn->dn_phys->dn_blkptr[0]) ||
	    list_head(&dn->dn_dbufs) != NULL)) {
		db = dbuf_hold(dn, 0, FTAG);
		dbuf_new_size(db, blocksize, tx);
	}
	dnode_setdblksz(dn, blocksize);
	dnode_setdirty(dn, tx);
	dn->dn_next_bonuslen[tx->tx_txg&TXG_MASK] = bonuslen;
	dn->dn_next_blksz[tx->tx_txg&TXG_MASK] = blocksize;
	if (dn->dn_nblkptr != nblkptr)
		dn->dn_next_nblkptr[tx->tx_txg&TXG_MASK] = nblkptr;
	rw_exit(&dn->dn_struct_rwlock);
	if (db)
		dbuf_rele(db, FTAG);

	/* change type */
	dn->dn_type = ot;

	/* change bonus size and type */
	mutex_enter(&dn->dn_mtx);
	dn->dn_bonustype = bonustype;
	dn->dn_bonuslen = bonuslen;
	dn->dn_nblkptr = nblkptr;
	dn->dn_checksum = ZIO_CHECKSUM_INHERIT;
	dn->dn_compress = ZIO_COMPRESS_INHERIT;
	ASSERT3U(dn->dn_nblkptr, <=, DN_MAX_NBLKPTR);

	/* fix up the bonus db_size */
	if (dn->dn_bonus) {
		dn->dn_bonus->db.db_size =
		    DN_MAX_BONUSLEN - (dn->dn_nblkptr-1) * sizeof (blkptr_t);
		ASSERT(dn->dn_bonuslen <= dn->dn_bonus->db.db_size);
	}

	dn->dn_allocated_txg = tx->tx_txg;
	mutex_exit(&dn->dn_mtx);
}

void
dnode_special_close(dnode_t *dn)
{
	/*
	 * Wait for final references to the dnode to clear.  This can
	 * only happen if the arc is asyncronously evicting state that
	 * has a hold on this dnode while we are trying to evict this
	 * dnode.
	 */
	while (refcount_count(&dn->dn_holds) > 0)
		delay(1);
	dnode_destroy(dn);
}

dnode_t *
dnode_special_open(objset_impl_t *os, dnode_phys_t *dnp, uint64_t object)
{
	dnode_t *dn = dnode_create(os, dnp, NULL, object);
	DNODE_VERIFY(dn);
	return (dn);
}

static void
dnode_buf_pageout(dmu_buf_t *db, void *arg)
{
	dnode_t **children_dnodes = arg;
	int i;
	int epb = db->db_size >> DNODE_SHIFT;

	for (i = 0; i < epb; i++) {
		dnode_t *dn = children_dnodes[i];
		int n;

		if (dn == NULL)
			continue;
#ifdef ZFS_DEBUG
		/*
		 * If there are holds on this dnode, then there should
		 * be holds on the dnode's containing dbuf as well; thus
		 * it wouldn't be eligable for eviction and this function
		 * would not have been called.
		 */
		ASSERT(refcount_is_zero(&dn->dn_holds));
		ASSERT(list_head(&dn->dn_dbufs) == NULL);
		ASSERT(refcount_is_zero(&dn->dn_tx_holds));

		for (n = 0; n < TXG_SIZE; n++)
			ASSERT(!list_link_active(&dn->dn_dirty_link[n]));
#endif
		children_dnodes[i] = NULL;
		dnode_destroy(dn);
	}
	kmem_free(children_dnodes, epb * sizeof (dnode_t *));
}

/*
 * errors:
 * EINVAL - invalid object number.
 * EIO - i/o error.
 * succeeds even for free dnodes.
 */
int
dnode_hold_impl(objset_impl_t *os, uint64_t object, int flag,
    void *tag, dnode_t **dnp)
{
	int epb, idx, err;
	int drop_struct_lock = FALSE;
	int type;
	uint64_t blk;
	dnode_t *mdn, *dn;
	dmu_buf_impl_t *db;
	dnode_t **children_dnodes;

	/*
	 * If you are holding the spa config lock as writer, you shouldn't
	 * be asking the DMU to do *anything*.
	 */
	ASSERT(spa_config_held(os->os_spa, SCL_ALL, RW_WRITER) == 0);

	if (object == 0 || object >= DN_MAX_OBJECT)
		return (EINVAL);

	mdn = os->os_meta_dnode;

	DNODE_VERIFY(mdn);

	if (!RW_WRITE_HELD(&mdn->dn_struct_rwlock)) {
		rw_enter(&mdn->dn_struct_rwlock, RW_READER);
		drop_struct_lock = TRUE;
	}

	blk = dbuf_whichblock(mdn, object * sizeof (dnode_phys_t));

	db = dbuf_hold(mdn, blk, FTAG);
	if (drop_struct_lock)
		rw_exit(&mdn->dn_struct_rwlock);
	if (db == NULL)
		return (EIO);
	err = dbuf_read(db, NULL, DB_RF_CANFAIL);
	if (err) {
		dbuf_rele(db, FTAG);
		return (err);
	}

	ASSERT3U(db->db.db_size, >=, 1<<DNODE_SHIFT);
	epb = db->db.db_size >> DNODE_SHIFT;

	idx = object & (epb-1);

	children_dnodes = dmu_buf_get_user(&db->db);
	if (children_dnodes == NULL) {
		dnode_t **winner;
		children_dnodes = kmem_zalloc(epb * sizeof (dnode_t *),
		    KM_SLEEP);
		if (winner = dmu_buf_set_user(&db->db, children_dnodes, NULL,
		    dnode_buf_pageout)) {
			kmem_free(children_dnodes, epb * sizeof (dnode_t *));
			children_dnodes = winner;
		}
	}

	if ((dn = children_dnodes[idx]) == NULL) {
		dnode_phys_t *dnp = (dnode_phys_t *)db->db.db_data+idx;
		dnode_t *winner;

		dn = dnode_create(os, dnp, db, object);
		winner = atomic_cas_ptr(&children_dnodes[idx], NULL, dn);
		if (winner != NULL) {
			dnode_destroy(dn);
			dn = winner;
		}
	}

	mutex_enter(&dn->dn_mtx);
	type = dn->dn_type;
	if (dn->dn_free_txg ||
	    ((flag & DNODE_MUST_BE_ALLOCATED) && type == DMU_OT_NONE) ||
	    ((flag & DNODE_MUST_BE_FREE) && type != DMU_OT_NONE)) {
		mutex_exit(&dn->dn_mtx);
		dbuf_rele(db, FTAG);
		return (type == DMU_OT_NONE ? ENOENT : EEXIST);
	}
	mutex_exit(&dn->dn_mtx);

	if (refcount_add(&dn->dn_holds, tag) == 1)
		dbuf_add_ref(db, dn);

	DNODE_VERIFY(dn);
	ASSERT3P(dn->dn_dbuf, ==, db);
	ASSERT3U(dn->dn_object, ==, object);
	dbuf_rele(db, FTAG);

	*dnp = dn;
	return (0);
}

/*
 * Return held dnode if the object is allocated, NULL if not.
 */
int
dnode_hold(objset_impl_t *os, uint64_t object, void *tag, dnode_t **dnp)
{
	return (dnode_hold_impl(os, object, DNODE_MUST_BE_ALLOCATED, tag, dnp));
}

/*
 * Can only add a reference if there is already at least one
 * reference on the dnode.  Returns FALSE if unable to add a
 * new reference.
 */
boolean_t
dnode_add_ref(dnode_t *dn, void *tag)
{
	mutex_enter(&dn->dn_mtx);
	if (refcount_is_zero(&dn->dn_holds)) {
		mutex_exit(&dn->dn_mtx);
		return (FALSE);
	}
	VERIFY(1 < refcount_add(&dn->dn_holds, tag));
	mutex_exit(&dn->dn_mtx);
	return (TRUE);
}

void
dnode_rele(dnode_t *dn, void *tag)
{
	uint64_t refs;

	mutex_enter(&dn->dn_mtx);
	refs = refcount_remove(&dn->dn_holds, tag);
	mutex_exit(&dn->dn_mtx);
	/* NOTE: the DNODE_DNODE does not have a dn_dbuf */
	if (refs == 0 && dn->dn_dbuf)
		dbuf_rele(dn->dn_dbuf, dn);
}

void
dnode_setdirty(dnode_t *dn, dmu_tx_t *tx)
{
	objset_impl_t *os = dn->dn_objset;
	uint64_t txg = tx->tx_txg;

	if (dn->dn_object == DMU_META_DNODE_OBJECT)
		return;

	DNODE_VERIFY(dn);

#ifdef ZFS_DEBUG
	mutex_enter(&dn->dn_mtx);
	ASSERT(dn->dn_phys->dn_type || dn->dn_allocated_txg);
	/* ASSERT(dn->dn_free_txg == 0 || dn->dn_free_txg >= txg); */
	mutex_exit(&dn->dn_mtx);
#endif

	mutex_enter(&os->os_lock);

	/*
	 * If we are already marked dirty, we're done.
	 */
	if (list_link_active(&dn->dn_dirty_link[txg & TXG_MASK])) {
		mutex_exit(&os->os_lock);
		return;
	}

	ASSERT(!refcount_is_zero(&dn->dn_holds) || list_head(&dn->dn_dbufs));
	ASSERT(dn->dn_datablksz != 0);
	ASSERT3U(dn->dn_next_bonuslen[txg&TXG_MASK], ==, 0);
	ASSERT3U(dn->dn_next_blksz[txg&TXG_MASK], ==, 0);

	dprintf_ds(os->os_dsl_dataset, "obj=%llu txg=%llu\n",
	    dn->dn_object, txg);

	if (dn->dn_free_txg > 0 && dn->dn_free_txg <= txg) {
		list_insert_tail(&os->os_free_dnodes[txg&TXG_MASK], dn);
	} else {
		list_insert_tail(&os->os_dirty_dnodes[txg&TXG_MASK], dn);
	}

	mutex_exit(&os->os_lock);

	/*
	 * The dnode maintains a hold on its containing dbuf as
	 * long as there are holds on it.  Each instantiated child
	 * dbuf maintaines a hold on the dnode.  When the last child
	 * drops its hold, the dnode will drop its hold on the
	 * containing dbuf. We add a "dirty hold" here so that the
	 * dnode will hang around after we finish processing its
	 * children.
	 */
	VERIFY(dnode_add_ref(dn, (void *)(uintptr_t)tx->tx_txg));

	(void) dbuf_dirty(dn->dn_dbuf, tx);

	dsl_dataset_dirty(os->os_dsl_dataset, tx);
}

void
dnode_free(dnode_t *dn, dmu_tx_t *tx)
{
	int txgoff = tx->tx_txg & TXG_MASK;

	dprintf("dn=%p txg=%llu\n", dn, tx->tx_txg);

	/* we should be the only holder... hopefully */
	/* ASSERT3U(refcount_count(&dn->dn_holds), ==, 1); */

	mutex_enter(&dn->dn_mtx);
	if (dn->dn_type == DMU_OT_NONE || dn->dn_free_txg) {
		mutex_exit(&dn->dn_mtx);
		return;
	}
	dn->dn_free_txg = tx->tx_txg;
	mutex_exit(&dn->dn_mtx);

	/*
	 * If the dnode is already dirty, it needs to be moved from
	 * the dirty list to the free list.
	 */
	mutex_enter(&dn->dn_objset->os_lock);
	if (list_link_active(&dn->dn_dirty_link[txgoff])) {
		list_remove(&dn->dn_objset->os_dirty_dnodes[txgoff], dn);
		list_insert_tail(&dn->dn_objset->os_free_dnodes[txgoff], dn);
		mutex_exit(&dn->dn_objset->os_lock);
	} else {
		mutex_exit(&dn->dn_objset->os_lock);
		dnode_setdirty(dn, tx);
	}
}

/*
 * Try to change the block size for the indicated dnode.  This can only
 * succeed if there are no blocks allocated or dirty beyond first block
 */
int
dnode_set_blksz(dnode_t *dn, uint64_t size, int ibs, dmu_tx_t *tx)
{
	dmu_buf_impl_t *db, *db_next;
	int err;

	if (size == 0)
		size = SPA_MINBLOCKSIZE;
	if (size > SPA_MAXBLOCKSIZE)
		size = SPA_MAXBLOCKSIZE;
	else
		size = P2ROUNDUP(size, SPA_MINBLOCKSIZE);

	if (ibs == dn->dn_indblkshift)
		ibs = 0;

	if (size >> SPA_MINBLOCKSHIFT == dn->dn_datablkszsec && ibs == 0)
		return (0);

	rw_enter(&dn->dn_struct_rwlock, RW_WRITER);

	/* Check for any allocated blocks beyond the first */
	if (dn->dn_phys->dn_maxblkid != 0)
		goto fail;

	mutex_enter(&dn->dn_dbufs_mtx);
	for (db = list_head(&dn->dn_dbufs); db; db = db_next) {
		db_next = list_next(&dn->dn_dbufs, db);

		if (db->db_blkid != 0 && db->db_blkid != DB_BONUS_BLKID) {
			mutex_exit(&dn->dn_dbufs_mtx);
			goto fail;
		}
	}
	mutex_exit(&dn->dn_dbufs_mtx);

	if (ibs && dn->dn_nlevels != 1)
		goto fail;

	/* resize the old block */
	err = dbuf_hold_impl(dn, 0, 0, TRUE, FTAG, &db);
	if (err == 0)
		dbuf_new_size(db, size, tx);
	else if (err != ENOENT)
		goto fail;

	dnode_setdblksz(dn, size);
	dnode_setdirty(dn, tx);
	dn->dn_next_blksz[tx->tx_txg&TXG_MASK] = size;
	if (ibs) {
		dn->dn_indblkshift = ibs;
		dn->dn_next_indblkshift[tx->tx_txg&TXG_MASK] = ibs;
	}
	/* rele after we have fixed the blocksize in the dnode */
	if (db)
		dbuf_rele(db, FTAG);

	rw_exit(&dn->dn_struct_rwlock);
	return (0);

fail:
	rw_exit(&dn->dn_struct_rwlock);
	return (ENOTSUP);
}

/* read-holding callers must not rely on the lock being continuously held */
void
dnode_new_blkid(dnode_t *dn, uint64_t blkid, dmu_tx_t *tx, boolean_t have_read)
{
	uint64_t txgoff = tx->tx_txg & TXG_MASK;
	int epbs, new_nlevels;
	uint64_t sz;

	ASSERT(blkid != DB_BONUS_BLKID);

	ASSERT(have_read ?
	    RW_READ_HELD(&dn->dn_struct_rwlock) :
	    RW_WRITE_HELD(&dn->dn_struct_rwlock));

	/*
	 * if we have a read-lock, check to see if we need to do any work
	 * before upgrading to a write-lock.
	 */
	if (have_read) {
		if (blkid <= dn->dn_maxblkid)
			return;

		if (!rw_tryupgrade(&dn->dn_struct_rwlock)) {
			rw_exit(&dn->dn_struct_rwlock);
			rw_enter(&dn->dn_struct_rwlock, RW_WRITER);
		}
	}

	if (blkid <= dn->dn_maxblkid)
		goto out;

	dn->dn_maxblkid = blkid;

	/*
	 * Compute the number of levels necessary to support the new maxblkid.
	 */
	new_nlevels = 1;
	epbs = dn->dn_indblkshift - SPA_BLKPTRSHIFT;
	for (sz = dn->dn_nblkptr;
	    sz <= blkid && sz >= dn->dn_nblkptr; sz <<= epbs)
		new_nlevels++;

	if (new_nlevels > dn->dn_nlevels) {
		int old_nlevels = dn->dn_nlevels;
		dmu_buf_impl_t *db;
		list_t *list;
		dbuf_dirty_record_t *new, *dr, *dr_next;

		dn->dn_nlevels = new_nlevels;

		ASSERT3U(new_nlevels, >, dn->dn_next_nlevels[txgoff]);
		dn->dn_next_nlevels[txgoff] = new_nlevels;

		/* dirty the left indirects */
		db = dbuf_hold_level(dn, old_nlevels, 0, FTAG);
		new = dbuf_dirty(db, tx);
		dbuf_rele(db, FTAG);

		/* transfer the dirty records to the new indirect */
		mutex_enter(&dn->dn_mtx);
		mutex_enter(&new->dt.di.dr_mtx);
		list = &dn->dn_dirty_records[txgoff];
		for (dr = list_head(list); dr; dr = dr_next) {
			dr_next = list_next(&dn->dn_dirty_records[txgoff], dr);
			if (dr->dr_dbuf->db_level != new_nlevels-1 &&
			    dr->dr_dbuf->db_blkid != DB_BONUS_BLKID) {
				ASSERT(dr->dr_dbuf->db_level == old_nlevels-1);
				list_remove(&dn->dn_dirty_records[txgoff], dr);
				list_insert_tail(&new->dt.di.dr_children, dr);
				dr->dr_parent = new;
			}
		}
		mutex_exit(&new->dt.di.dr_mtx);
		mutex_exit(&dn->dn_mtx);
	}

out:
	if (have_read)
		rw_downgrade(&dn->dn_struct_rwlock);
}

void
dnode_clear_range(dnode_t *dn, uint64_t blkid, uint64_t nblks, dmu_tx_t *tx)
{
	avl_tree_t *tree = &dn->dn_ranges[tx->tx_txg&TXG_MASK];
	avl_index_t where;
	free_range_t *rp;
	free_range_t rp_tofind;
	uint64_t endblk = blkid + nblks;

	ASSERT(MUTEX_HELD(&dn->dn_mtx));
	ASSERT(nblks <= UINT64_MAX - blkid); /* no overflow */

	dprintf_dnode(dn, "blkid=%llu nblks=%llu txg=%llu\n",
	    blkid, nblks, tx->tx_txg);
	rp_tofind.fr_blkid = blkid;
	rp = avl_find(tree, &rp_tofind, &where);
	if (rp == NULL)
		rp = avl_nearest(tree, where, AVL_BEFORE);
	if (rp == NULL)
		rp = avl_nearest(tree, where, AVL_AFTER);

	while (rp && (rp->fr_blkid <= blkid + nblks)) {
		uint64_t fr_endblk = rp->fr_blkid + rp->fr_nblks;
		free_range_t *nrp = AVL_NEXT(tree, rp);

		if (blkid <= rp->fr_blkid && endblk >= fr_endblk) {
			/* clear this entire range */
			avl_remove(tree, rp);
			kmem_free(rp, sizeof (free_range_t));
		} else if (blkid <= rp->fr_blkid &&
		    endblk > rp->fr_blkid && endblk < fr_endblk) {
			/* clear the beginning of this range */
			rp->fr_blkid = endblk;
			rp->fr_nblks = fr_endblk - endblk;
		} else if (blkid > rp->fr_blkid && blkid < fr_endblk &&
		    endblk >= fr_endblk) {
			/* clear the end of this range */
			rp->fr_nblks = blkid - rp->fr_blkid;
		} else if (blkid > rp->fr_blkid && endblk < fr_endblk) {
			/* clear a chunk out of this range */
			free_range_t *new_rp =
			    kmem_alloc(sizeof (free_range_t), KM_SLEEP);

			new_rp->fr_blkid = endblk;
			new_rp->fr_nblks = fr_endblk - endblk;
			avl_insert_here(tree, new_rp, rp, AVL_AFTER);
			rp->fr_nblks = blkid - rp->fr_blkid;
		}
		/* there may be no overlap */
		rp = nrp;
	}
}

void
dnode_free_range(dnode_t *dn, uint64_t off, uint64_t len, dmu_tx_t *tx)
{
	dmu_buf_impl_t *db;
	uint64_t blkoff, blkid, nblks;
	int blksz, blkshift, head, tail;
	int trunc = FALSE;
	int epbs;

	rw_enter(&dn->dn_struct_rwlock, RW_WRITER);
	blksz = dn->dn_datablksz;
	blkshift = dn->dn_datablkshift;
	epbs = dn->dn_indblkshift - SPA_BLKPTRSHIFT;

	if (len == -1ULL) {
		len = UINT64_MAX - off;
		trunc = TRUE;
	}

	/*
	 * First, block align the region to free:
	 */
	if (ISP2(blksz)) {
		head = P2NPHASE(off, blksz);
		blkoff = P2PHASE(off, blksz);
		if ((off >> blkshift) > dn->dn_maxblkid)
			goto out;
	} else {
		ASSERT(dn->dn_maxblkid == 0);
		if (off == 0 && len >= blksz) {
			/* Freeing the whole block; fast-track this request */
			blkid = 0;
			nblks = 1;
			goto done;
		} else if (off >= blksz) {
			/* Freeing past end-of-data */
			goto out;
		} else {
			/* Freeing part of the block. */
			head = blksz - off;
			ASSERT3U(head, >, 0);
		}
		blkoff = off;
	}
	/* zero out any partial block data at the start of the range */
	if (head) {
		ASSERT3U(blkoff + head, ==, blksz);
		if (len < head)
			head = len;
		if (dbuf_hold_impl(dn, 0, dbuf_whichblock(dn, off), TRUE,
		    FTAG, &db) == 0) {
			caddr_t data;

			/* don't dirty if it isn't on disk and isn't dirty */
			if (db->db_last_dirty ||
			    (db->db_blkptr && !BP_IS_HOLE(db->db_blkptr))) {
				rw_exit(&dn->dn_struct_rwlock);
				dbuf_will_dirty(db, tx);
				rw_enter(&dn->dn_struct_rwlock, RW_WRITER);
				data = db->db.db_data;
				bzero(data + blkoff, head);
			}
			dbuf_rele(db, FTAG);
		}
		off += head;
		len -= head;
	}

	/* If the range was less than one block, we're done */
	if (len == 0)
		goto out;

	/* If the remaining range is past end of file, we're done */
	if ((off >> blkshift) > dn->dn_maxblkid)
		goto out;

	ASSERT(ISP2(blksz));
	if (trunc)
		tail = 0;
	else
		tail = P2PHASE(len, blksz);

	ASSERT3U(P2PHASE(off, blksz), ==, 0);
	/* zero out any partial block data at the end of the range */
	if (tail) {
		if (len < tail)
			tail = len;
		if (dbuf_hold_impl(dn, 0, dbuf_whichblock(dn, off+len),
		    TRUE, FTAG, &db) == 0) {
			/* don't dirty if not on disk and not dirty */
			if (db->db_last_dirty ||
			    (db->db_blkptr && !BP_IS_HOLE(db->db_blkptr))) {
				rw_exit(&dn->dn_struct_rwlock);
				dbuf_will_dirty(db, tx);
				rw_enter(&dn->dn_struct_rwlock, RW_WRITER);
				bzero(db->db.db_data, tail);
			}
			dbuf_rele(db, FTAG);
		}
		len -= tail;
	}

	/* If the range did not include a full block, we are done */
	if (len == 0)
		goto out;

	ASSERT(IS_P2ALIGNED(off, blksz));
	ASSERT(trunc || IS_P2ALIGNED(len, blksz));
	blkid = off >> blkshift;
	nblks = len >> blkshift;
	if (trunc)
		nblks += 1;

	/*
	 * Read in and mark all the level-1 indirects dirty,
	 * so that they will stay in memory until syncing phase.
	 * Always dirty the first and last indirect to make sure
	 * we dirty all the partial indirects.
	 */
	if (dn->dn_nlevels > 1) {
		uint64_t i, first, last;
		int shift = epbs + dn->dn_datablkshift;

		first = blkid >> epbs;
		if (db = dbuf_hold_level(dn, 1, first, FTAG)) {
			dbuf_will_dirty(db, tx);
			dbuf_rele(db, FTAG);
		}
		if (trunc)
			last = dn->dn_maxblkid >> epbs;
		else
			last = (blkid + nblks - 1) >> epbs;
		if (last > first && (db = dbuf_hold_level(dn, 1, last, FTAG))) {
			dbuf_will_dirty(db, tx);
			dbuf_rele(db, FTAG);
		}
		for (i = first + 1; i < last; i++) {
			uint64_t ibyte = i << shift;
			int err;

			err = dnode_next_offset(dn,
			    DNODE_FIND_HAVELOCK, &ibyte, 1, 1, 0);
			i = ibyte >> shift;
			if (err == ESRCH || i >= last)
				break;
			ASSERT(err == 0);
			db = dbuf_hold_level(dn, 1, i, FTAG);
			if (db) {
				dbuf_will_dirty(db, tx);
				dbuf_rele(db, FTAG);
			}
		}
	}
done:
	/*
	 * Add this range to the dnode range list.
	 * We will finish up this free operation in the syncing phase.
	 */
	mutex_enter(&dn->dn_mtx);
	dnode_clear_range(dn, blkid, nblks, tx);
	{
		free_range_t *rp, *found;
		avl_index_t where;
		avl_tree_t *tree = &dn->dn_ranges[tx->tx_txg&TXG_MASK];

		/* Add new range to dn_ranges */
		rp = kmem_alloc(sizeof (free_range_t), KM_SLEEP);
		rp->fr_blkid = blkid;
		rp->fr_nblks = nblks;
		found = avl_find(tree, rp, &where);
		ASSERT(found == NULL);
		avl_insert(tree, rp, where);
		dprintf_dnode(dn, "blkid=%llu nblks=%llu txg=%llu\n",
		    blkid, nblks, tx->tx_txg);
	}
	mutex_exit(&dn->dn_mtx);

	dbuf_free_range(dn, blkid, blkid + nblks - 1, tx);
	dnode_setdirty(dn, tx);
out:
	if (trunc && dn->dn_maxblkid >= (off >> blkshift))
		dn->dn_maxblkid = (off >> blkshift ? (off >> blkshift) - 1 : 0);

	rw_exit(&dn->dn_struct_rwlock);
}

/* return TRUE if this blkid was freed in a recent txg, or FALSE if it wasn't */
uint64_t
dnode_block_freed(dnode_t *dn, uint64_t blkid)
{
	free_range_t range_tofind;
	void *dp = spa_get_dsl(dn->dn_objset->os_spa);
	int i;

	if (blkid == DB_BONUS_BLKID)
		return (FALSE);

	/*
	 * If we're in the process of opening the pool, dp will not be
	 * set yet, but there shouldn't be anything dirty.
	 */
	if (dp == NULL)
		return (FALSE);

	if (dn->dn_free_txg)
		return (TRUE);

	/*
	 * If dn_datablkshift is not set, then there's only a single
	 * block, in which case there will never be a free range so it
	 * won't matter.
	 */
	range_tofind.fr_blkid = blkid;
	mutex_enter(&dn->dn_mtx);
	for (i = 0; i < TXG_SIZE; i++) {
		free_range_t *range_found;
		avl_index_t idx;

		range_found = avl_find(&dn->dn_ranges[i], &range_tofind, &idx);
		if (range_found) {
			ASSERT(range_found->fr_nblks > 0);
			break;
		}
		range_found = avl_nearest(&dn->dn_ranges[i], idx, AVL_BEFORE);
		if (range_found &&
		    range_found->fr_blkid + range_found->fr_nblks > blkid)
			break;
	}
	mutex_exit(&dn->dn_mtx);
	return (i < TXG_SIZE);
}

/* call from syncing context when we actually write/free space for this dnode */
void
dnode_diduse_space(dnode_t *dn, int64_t delta)
{
	uint64_t space;
	dprintf_dnode(dn, "dn=%p dnp=%p used=%llu delta=%lld\n",
	    dn, dn->dn_phys,
	    (u_longlong_t)dn->dn_phys->dn_used,
	    (longlong_t)delta);

	mutex_enter(&dn->dn_mtx);
	space = DN_USED_BYTES(dn->dn_phys);
	if (delta > 0) {
		ASSERT3U(space + delta, >=, space); /* no overflow */
	} else {
		ASSERT3U(space, >=, -delta); /* no underflow */
	}
	space += delta;
	if (spa_version(dn->dn_objset->os_spa) < SPA_VERSION_DNODE_BYTES) {
		ASSERT((dn->dn_phys->dn_flags & DNODE_FLAG_USED_BYTES) == 0);
		ASSERT3U(P2PHASE(space, 1<<DEV_BSHIFT), ==, 0);
		dn->dn_phys->dn_used = space >> DEV_BSHIFT;
	} else {
		dn->dn_phys->dn_used = space;
		dn->dn_phys->dn_flags |= DNODE_FLAG_USED_BYTES;
	}
	mutex_exit(&dn->dn_mtx);
}

/*
 * Call when we think we're going to write/free space in open context.
 * Be conservative (ie. OK to write less than this or free more than
 * this, but don't write more or free less).
 */
void
dnode_willuse_space(dnode_t *dn, int64_t space, dmu_tx_t *tx)
{
	objset_impl_t *os = dn->dn_objset;
	dsl_dataset_t *ds = os->os_dsl_dataset;

	if (space > 0)
		space = spa_get_asize(os->os_spa, space);

	if (ds)
		dsl_dir_willuse_space(ds->ds_dir, space, tx);

	dmu_tx_willuse_space(tx, space);
}

static int
dnode_next_offset_level(dnode_t *dn, int flags, uint64_t *offset,
	int lvl, uint64_t blkfill, uint64_t txg)
{
	dmu_buf_impl_t *db = NULL;
	void *data = NULL;
	uint64_t epbs = dn->dn_phys->dn_indblkshift - SPA_BLKPTRSHIFT;
	uint64_t epb = 1ULL << epbs;
	uint64_t minfill, maxfill;
	boolean_t hole;
	int i, inc, error, span;

	dprintf("probing object %llu offset %llx level %d of %u\n",
	    dn->dn_object, *offset, lvl, dn->dn_phys->dn_nlevels);

	hole = flags & DNODE_FIND_HOLE;
	inc = (flags & DNODE_FIND_BACKWARDS) ? -1 : 1;
	ASSERT(txg == 0 || !hole);

	if (lvl == dn->dn_phys->dn_nlevels) {
		error = 0;
		epb = dn->dn_phys->dn_nblkptr;
		data = dn->dn_phys->dn_blkptr;
	} else {
		uint64_t blkid = dbuf_whichblock(dn, *offset) >> (epbs * lvl);
		error = dbuf_hold_impl(dn, lvl, blkid, TRUE, FTAG, &db);
		if (error) {
			if (error != ENOENT)
				return (error);
			if (hole)
				return (0);
			/*
			 * This can only happen when we are searching up
			 * the block tree for data.  We don't really need to
			 * adjust the offset, as we will just end up looking
			 * at the pointer to this block in its parent, and its
			 * going to be unallocated, so we will skip over it.
			 */
			return (ESRCH);
		}
		error = dbuf_read(db, NULL, DB_RF_CANFAIL | DB_RF_HAVESTRUCT);
		if (error) {
			dbuf_rele(db, FTAG);
			return (error);
		}
		data = db->db.db_data;
	}

	if (db && txg &&
	    (db->db_blkptr == NULL || db->db_blkptr->blk_birth <= txg)) {
		/*
		 * This can only happen when we are searching up the tree
		 * and these conditions mean that we need to keep climbing.
		 */
		error = ESRCH;
	} else if (lvl == 0) {
		dnode_phys_t *dnp = data;
		span = DNODE_SHIFT;
		ASSERT(dn->dn_type == DMU_OT_DNODE);

		for (i = (*offset >> span) & (blkfill - 1);
		    i >= 0 && i < blkfill; i += inc) {
			boolean_t newcontents = B_TRUE;
			if (txg) {
				int j;
				newcontents = B_FALSE;
				for (j = 0; j < dnp[i].dn_nblkptr; j++) {
					if (dnp[i].dn_blkptr[j].blk_birth > txg)
						newcontents = B_TRUE;
				}
			}
			if (!dnp[i].dn_type == hole && newcontents)
				break;
			*offset += (1ULL << span) * inc;
		}
		if (i < 0 || i == blkfill)
			error = ESRCH;
	} else {
		blkptr_t *bp = data;
		span = (lvl - 1) * epbs + dn->dn_datablkshift;
		minfill = 0;
		maxfill = blkfill << ((lvl - 1) * epbs);

		if (hole)
			maxfill--;
		else
			minfill++;

		for (i = (*offset >> span) & ((1ULL << epbs) - 1);
		    i >= 0 && i < epb; i += inc) {
			if (bp[i].blk_fill >= minfill &&
			    bp[i].blk_fill <= maxfill &&
			    (hole || bp[i].blk_birth > txg))
				break;
			if (inc < 0 && *offset < (1ULL << span))
				*offset = 0;
			else
				*offset += (1ULL << span) * inc;
		}
		if (i < 0 || i == epb)
			error = ESRCH;
	}

	if (db)
		dbuf_rele(db, FTAG);

	return (error);
}

/*
 * Find the next hole, data, or sparse region at or after *offset.
 * The value 'blkfill' tells us how many items we expect to find
 * in an L0 data block; this value is 1 for normal objects,
 * DNODES_PER_BLOCK for the meta dnode, and some fraction of
 * DNODES_PER_BLOCK when searching for sparse regions thereof.
 *
 * Examples:
 *
 * dnode_next_offset(dn, flags, offset, 1, 1, 0);
 *	Finds the next/previous hole/data in a file.
 *	Used in dmu_offset_next().
 *
 * dnode_next_offset(mdn, flags, offset, 0, DNODES_PER_BLOCK, txg);
 *	Finds the next free/allocated dnode an objset's meta-dnode.
 *	Only finds objects that have new contents since txg (ie.
 *	bonus buffer changes and content removal are ignored).
 *	Used in dmu_object_next().
 *
 * dnode_next_offset(mdn, DNODE_FIND_HOLE, offset, 2, DNODES_PER_BLOCK >> 2, 0);
 *	Finds the next L2 meta-dnode bp that's at most 1/4 full.
 *	Used in dmu_object_alloc().
 */
int
dnode_next_offset(dnode_t *dn, int flags, uint64_t *offset,
    int minlvl, uint64_t blkfill, uint64_t txg)
{
	uint64_t initial_offset = *offset;
	int lvl, maxlvl;
	int error = 0;

	if (!(flags & DNODE_FIND_HAVELOCK))
		rw_enter(&dn->dn_struct_rwlock, RW_READER);

	if (dn->dn_phys->dn_nlevels == 0) {
		error = ESRCH;
		goto out;
	}

	if (dn->dn_datablkshift == 0) {
		if (*offset < dn->dn_datablksz) {
			if (flags & DNODE_FIND_HOLE)
				*offset = dn->dn_datablksz;
		} else {
			error = ESRCH;
		}
		goto out;
	}

	maxlvl = dn->dn_phys->dn_nlevels;

	for (lvl = minlvl; lvl <= maxlvl; lvl++) {
		error = dnode_next_offset_level(dn,
		    flags, offset, lvl, blkfill, txg);
		if (error != ESRCH)
			break;
	}

	while (error == 0 && --lvl >= minlvl) {
		error = dnode_next_offset_level(dn,
		    flags, offset, lvl, blkfill, txg);
	}

	if (error == 0 && (flags & DNODE_FIND_BACKWARDS ?
	    initial_offset < *offset : initial_offset > *offset))
		error = ESRCH;
out:
	if (!(flags & DNODE_FIND_HAVELOCK))
		rw_exit(&dn->dn_struct_rwlock);

	return (error);
}
