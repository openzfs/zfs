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
/*
 * Define DNODE_STATS to turn on statistic gathering. By default, it is only
 * turned on when DEBUG is also defined.
 */
#ifdef	DEBUG
#define	DNODE_STATS
#endif	/* DEBUG */

#ifdef	DNODE_STATS
#define	DNODE_STAT_ADD(stat)			((stat)++)
#else
#define	DNODE_STAT_ADD(stat)			/* nothing */
#endif	/* DNODE_STATS */

ASSERTV(static dnode_phys_t dnode_phys_zero);

int zfs_default_bs = SPA_MINBLOCKSHIFT;
int zfs_default_ibs = DN_MAX_INDBLKSHIFT;

#ifdef	_KERNEL
static kmem_cbrc_t dnode_move(void *, void *, size_t, void *);
#endif /* _KERNEL */

/* ARGSUSED */
static int
dnode_cons(void *arg, void *unused, int kmflag)
{
	dnode_t *dn = arg;
	int i;

	rw_init(&dn->dn_struct_rwlock, NULL, RW_DEFAULT, NULL);
	mutex_init(&dn->dn_mtx, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&dn->dn_dbufs_mtx, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&dn->dn_notxholds, NULL, CV_DEFAULT, NULL);

	refcount_create(&dn->dn_holds);
	refcount_create(&dn->dn_tx_holds);
	list_link_init(&dn->dn_link);

	bzero(&dn->dn_next_nblkptr[0], sizeof (dn->dn_next_nblkptr));
	bzero(&dn->dn_next_nlevels[0], sizeof (dn->dn_next_nlevels));
	bzero(&dn->dn_next_indblkshift[0], sizeof (dn->dn_next_indblkshift));
	bzero(&dn->dn_next_bonustype[0], sizeof (dn->dn_next_bonustype));
	bzero(&dn->dn_rm_spillblk[0], sizeof (dn->dn_rm_spillblk));
	bzero(&dn->dn_next_bonuslen[0], sizeof (dn->dn_next_bonuslen));
	bzero(&dn->dn_next_blksz[0], sizeof (dn->dn_next_blksz));

	for (i = 0; i < TXG_SIZE; i++) {
		list_link_init(&dn->dn_dirty_link[i]);
		avl_create(&dn->dn_ranges[i], free_range_compar,
		    sizeof (free_range_t),
		    offsetof(struct free_range, fr_node));
		list_create(&dn->dn_dirty_records[i],
		    sizeof (dbuf_dirty_record_t),
		    offsetof(dbuf_dirty_record_t, dr_dirty_node));
	}

	dn->dn_allocated_txg = 0;
	dn->dn_free_txg = 0;
	dn->dn_assigned_txg = 0;
	dn->dn_dirtyctx = 0;
	dn->dn_dirtyctx_firstset = NULL;
	dn->dn_bonus = NULL;
	dn->dn_have_spill = B_FALSE;
	dn->dn_zio = NULL;
	dn->dn_oldused = 0;
	dn->dn_oldflags = 0;
	dn->dn_olduid = 0;
	dn->dn_oldgid = 0;
	dn->dn_newuid = 0;
	dn->dn_newgid = 0;
	dn->dn_id_flags = 0;

	dn->dn_dbufs_count = 0;
	list_create(&dn->dn_dbufs, sizeof (dmu_buf_impl_t),
	    offsetof(dmu_buf_impl_t, db_link));

	dn->dn_moved = 0;
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
	ASSERT(!list_link_active(&dn->dn_link));

	for (i = 0; i < TXG_SIZE; i++) {
		ASSERT(!list_link_active(&dn->dn_dirty_link[i]));
		avl_destroy(&dn->dn_ranges[i]);
		list_destroy(&dn->dn_dirty_records[i]);
		ASSERT3U(dn->dn_next_nblkptr[i], ==, 0);
		ASSERT3U(dn->dn_next_nlevels[i], ==, 0);
		ASSERT3U(dn->dn_next_indblkshift[i], ==, 0);
		ASSERT3U(dn->dn_next_bonustype[i], ==, 0);
		ASSERT3U(dn->dn_rm_spillblk[i], ==, 0);
		ASSERT3U(dn->dn_next_bonuslen[i], ==, 0);
		ASSERT3U(dn->dn_next_blksz[i], ==, 0);
	}

	ASSERT3U(dn->dn_allocated_txg, ==, 0);
	ASSERT3U(dn->dn_free_txg, ==, 0);
	ASSERT3U(dn->dn_assigned_txg, ==, 0);
	ASSERT3U(dn->dn_dirtyctx, ==, 0);
	ASSERT3P(dn->dn_dirtyctx_firstset, ==, NULL);
	ASSERT3P(dn->dn_bonus, ==, NULL);
	ASSERT(!dn->dn_have_spill);
	ASSERT3P(dn->dn_zio, ==, NULL);
	ASSERT3U(dn->dn_oldused, ==, 0);
	ASSERT3U(dn->dn_oldflags, ==, 0);
	ASSERT3U(dn->dn_olduid, ==, 0);
	ASSERT3U(dn->dn_oldgid, ==, 0);
	ASSERT3U(dn->dn_newuid, ==, 0);
	ASSERT3U(dn->dn_newgid, ==, 0);
	ASSERT3U(dn->dn_id_flags, ==, 0);

	ASSERT3U(dn->dn_dbufs_count, ==, 0);
	list_destroy(&dn->dn_dbufs);
}

void
dnode_init(void)
{
	ASSERT(dnode_cache == NULL);
	dnode_cache = kmem_cache_create("dnode_t", sizeof (dnode_t),
	    0, dnode_cons, dnode_dest, NULL, NULL, NULL, KMC_KMEM);
	kmem_cache_set_move(dnode_cache, dnode_move);
}

void
dnode_fini(void)
{
	kmem_cache_destroy(dnode_cache);
	dnode_cache = NULL;
}


#ifdef ZFS_DEBUG
void
dnode_verify(dnode_t *dn)
{
	int drop_struct_lock = FALSE;

	ASSERT(dn->dn_phys);
	ASSERT(dn->dn_objset);
	ASSERT(dn->dn_handle->dnh_dnode == dn);

	ASSERT(dn->dn_phys->dn_type < DMU_OT_NUMTYPES);

	if (!(zfs_flags & ZFS_DEBUG_DNODE_VERIFY))
		return;

	if (!RW_WRITE_HELD(&dn->dn_struct_rwlock)) {
		rw_enter(&dn->dn_struct_rwlock, RW_READER);
		drop_struct_lock = TRUE;
	}
	if (dn->dn_phys->dn_type != DMU_OT_NONE || dn->dn_allocated_txg != 0) {
		int i;
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
	ASSERT(DMU_OBJECT_IS_SPECIAL(dn->dn_object) || dn->dn_dbuf != NULL);
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

	/* Swap SPILL block if we have one */
	if (dnp->dn_flags & DNODE_FLAG_SPILL_BLKPTR)
		byteswap_uint64_array(&dnp->dn_spill, sizeof (blkptr_t));

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

void
dnode_setbonus_type(dnode_t *dn, dmu_object_type_t newtype, dmu_tx_t *tx)
{
	ASSERT3U(refcount_count(&dn->dn_holds), >=, 1);
	dnode_setdirty(dn, tx);
	rw_enter(&dn->dn_struct_rwlock, RW_WRITER);
	dn->dn_bonustype = newtype;
	dn->dn_next_bonustype[tx->tx_txg & TXG_MASK] = dn->dn_bonustype;
	rw_exit(&dn->dn_struct_rwlock);
}

void
dnode_rm_spill(dnode_t *dn, dmu_tx_t *tx)
{
	ASSERT3U(refcount_count(&dn->dn_holds), >=, 1);
	ASSERT(RW_WRITE_HELD(&dn->dn_struct_rwlock));
	dnode_setdirty(dn, tx);
	dn->dn_rm_spillblk[tx->tx_txg&TXG_MASK] = DN_KILL_SPILLBLK;
	dn->dn_have_spill = B_FALSE;
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
dnode_create(objset_t *os, dnode_phys_t *dnp, dmu_buf_impl_t *db,
    uint64_t object, dnode_handle_t *dnh)
{
	dnode_t *dn = kmem_cache_alloc(dnode_cache, KM_PUSHPAGE);

	ASSERT(!POINTER_IS_VALID(dn->dn_objset));
	dn->dn_moved = 0;

	/*
	 * Defer setting dn_objset until the dnode is ready to be a candidate
	 * for the dnode_move() callback.
	 */
	dn->dn_object = object;
	dn->dn_dbuf = db;
	dn->dn_handle = dnh;
	dn->dn_phys = dnp;

	if (dnp->dn_datablkszsec) {
		dnode_setdblksz(dn, dnp->dn_datablkszsec << SPA_MINBLOCKSHIFT);
	} else {
		dn->dn_datablksz = 0;
		dn->dn_datablkszsec = 0;
		dn->dn_datablkshift = 0;
	}
	dn->dn_indblkshift = dnp->dn_indblkshift;
	dn->dn_nlevels = dnp->dn_nlevels;
	dn->dn_type = dnp->dn_type;
	dn->dn_nblkptr = dnp->dn_nblkptr;
	dn->dn_checksum = dnp->dn_checksum;
	dn->dn_compress = dnp->dn_compress;
	dn->dn_bonustype = dnp->dn_bonustype;
	dn->dn_bonuslen = dnp->dn_bonuslen;
	dn->dn_maxblkid = dnp->dn_maxblkid;
	dn->dn_have_spill = ((dnp->dn_flags & DNODE_FLAG_SPILL_BLKPTR) != 0);
	dn->dn_id_flags = 0;

	dmu_zfetch_init(&dn->dn_zfetch, dn);

	ASSERT(dn->dn_phys->dn_type < DMU_OT_NUMTYPES);

	mutex_enter(&os->os_lock);
	list_insert_head(&os->os_dnodes, dn);
	membar_producer();
	/*
	 * Everything else must be valid before assigning dn_objset makes the
	 * dnode eligible for dnode_move().
	 */
	dn->dn_objset = os;
	mutex_exit(&os->os_lock);

	arc_space_consume(sizeof (dnode_t), ARC_SPACE_OTHER);
	return (dn);
}

/*
 * Caller must be holding the dnode handle, which is released upon return.
 */
static void
dnode_destroy(dnode_t *dn)
{
	objset_t *os = dn->dn_objset;

	ASSERT((dn->dn_id_flags & DN_ID_NEW_EXIST) == 0);

	mutex_enter(&os->os_lock);
	POINTER_INVALIDATE(&dn->dn_objset);
	list_remove(&os->os_dnodes, dn);
	mutex_exit(&os->os_lock);

	/* the dnode can no longer move, so we can release the handle */
	zrl_remove(&dn->dn_handle->dnh_zrlock);

	dn->dn_allocated_txg = 0;
	dn->dn_free_txg = 0;
	dn->dn_assigned_txg = 0;

	dn->dn_dirtyctx = 0;
	if (dn->dn_dirtyctx_firstset != NULL) {
		kmem_free(dn->dn_dirtyctx_firstset, 1);
		dn->dn_dirtyctx_firstset = NULL;
	}
	if (dn->dn_bonus != NULL) {
		mutex_enter(&dn->dn_bonus->db_mtx);
		dbuf_evict(dn->dn_bonus);
		dn->dn_bonus = NULL;
	}
	dn->dn_zio = NULL;

	dn->dn_have_spill = B_FALSE;
	dn->dn_oldused = 0;
	dn->dn_oldflags = 0;
	dn->dn_olduid = 0;
	dn->dn_oldgid = 0;
	dn->dn_newuid = 0;
	dn->dn_newgid = 0;
	dn->dn_id_flags = 0;

	dmu_zfetch_rele(&dn->dn_zfetch);
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
	    (bonustype == DMU_OT_SA && bonuslen == 0) ||
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
		ASSERT3U(dn->dn_next_nblkptr[i], ==, 0);
		ASSERT3U(dn->dn_next_nlevels[i], ==, 0);
		ASSERT3U(dn->dn_next_indblkshift[i], ==, 0);
		ASSERT3U(dn->dn_next_bonuslen[i], ==, 0);
		ASSERT3U(dn->dn_next_bonustype[i], ==, 0);
		ASSERT3U(dn->dn_rm_spillblk[i], ==, 0);
		ASSERT3U(dn->dn_next_blksz[i], ==, 0);
		ASSERT(!list_link_active(&dn->dn_dirty_link[i]));
		ASSERT3P(list_head(&dn->dn_dirty_records[i]), ==, NULL);
		ASSERT3U(avl_numnodes(&dn->dn_ranges[i]), ==, 0);
	}

	dn->dn_type = ot;
	dnode_setdblksz(dn, blocksize);
	dn->dn_indblkshift = ibs;
	dn->dn_nlevels = 1;
	if (bonustype == DMU_OT_SA) /* Maximize bonus space for SA */
		dn->dn_nblkptr = 1;
	else
		dn->dn_nblkptr = 1 +
		    ((DN_MAX_BONUSLEN - bonuslen) >> SPA_BLKPTRSHIFT);
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
	dn->dn_id_flags = 0;

	dnode_setdirty(dn, tx);
	dn->dn_next_indblkshift[tx->tx_txg & TXG_MASK] = ibs;
	dn->dn_next_bonuslen[tx->tx_txg & TXG_MASK] = dn->dn_bonuslen;
	dn->dn_next_bonustype[tx->tx_txg & TXG_MASK] = dn->dn_bonustype;
	dn->dn_next_blksz[tx->tx_txg & TXG_MASK] = dn->dn_datablksz;
}

void
dnode_reallocate(dnode_t *dn, dmu_object_type_t ot, int blocksize,
    dmu_object_type_t bonustype, int bonuslen, dmu_tx_t *tx)
{
	int nblkptr;

	ASSERT3U(blocksize, >=, SPA_MINBLOCKSIZE);
	ASSERT3U(blocksize, <=, SPA_MAXBLOCKSIZE);
	ASSERT3U(blocksize % SPA_MINBLOCKSIZE, ==, 0);
	ASSERT(dn->dn_object != DMU_META_DNODE_OBJECT || dmu_tx_private_ok(tx));
	ASSERT(tx->tx_txg != 0);
	ASSERT((bonustype == DMU_OT_NONE && bonuslen == 0) ||
	    (bonustype != DMU_OT_NONE && bonuslen != 0) ||
	    (bonustype == DMU_OT_SA && bonuslen == 0));
	ASSERT3U(bonustype, <, DMU_OT_NUMTYPES);
	ASSERT3U(bonuslen, <=, DN_MAX_BONUSLEN);

	/* clean up any unreferenced dbufs */
	dnode_evict_dbufs(dn);

	dn->dn_id_flags = 0;

	rw_enter(&dn->dn_struct_rwlock, RW_WRITER);
	dnode_setdirty(dn, tx);
	if (dn->dn_datablksz != blocksize) {
		/* change blocksize */
		ASSERT(dn->dn_maxblkid == 0 &&
		    (BP_IS_HOLE(&dn->dn_phys->dn_blkptr[0]) ||
		    dnode_block_freed(dn, 0)));
		dnode_setdblksz(dn, blocksize);
		dn->dn_next_blksz[tx->tx_txg&TXG_MASK] = blocksize;
	}
	if (dn->dn_bonuslen != bonuslen)
		dn->dn_next_bonuslen[tx->tx_txg&TXG_MASK] = bonuslen;

	if (bonustype == DMU_OT_SA) /* Maximize bonus space for SA */
		nblkptr = 1;
	else
		nblkptr = 1 + ((DN_MAX_BONUSLEN - bonuslen) >> SPA_BLKPTRSHIFT);
	if (dn->dn_bonustype != bonustype)
		dn->dn_next_bonustype[tx->tx_txg&TXG_MASK] = bonustype;
	if (dn->dn_nblkptr != nblkptr)
		dn->dn_next_nblkptr[tx->tx_txg&TXG_MASK] = nblkptr;
	if (dn->dn_phys->dn_flags & DNODE_FLAG_SPILL_BLKPTR) {
		dbuf_rm_spill(dn, tx);
		dnode_rm_spill(dn, tx);
	}
	rw_exit(&dn->dn_struct_rwlock);

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

#ifdef	_KERNEL
#ifdef	DNODE_STATS
static struct {
	uint64_t dms_dnode_invalid;
	uint64_t dms_dnode_recheck1;
	uint64_t dms_dnode_recheck2;
	uint64_t dms_dnode_special;
	uint64_t dms_dnode_handle;
	uint64_t dms_dnode_rwlock;
	uint64_t dms_dnode_active;
} dnode_move_stats;
#endif	/* DNODE_STATS */

static void
dnode_move_impl(dnode_t *odn, dnode_t *ndn)
{
	int i;

	ASSERT(!RW_LOCK_HELD(&odn->dn_struct_rwlock));
	ASSERT(MUTEX_NOT_HELD(&odn->dn_mtx));
	ASSERT(MUTEX_NOT_HELD(&odn->dn_dbufs_mtx));
	ASSERT(!RW_LOCK_HELD(&odn->dn_zfetch.zf_rwlock));

	/* Copy fields. */
	ndn->dn_objset = odn->dn_objset;
	ndn->dn_object = odn->dn_object;
	ndn->dn_dbuf = odn->dn_dbuf;
	ndn->dn_handle = odn->dn_handle;
	ndn->dn_phys = odn->dn_phys;
	ndn->dn_type = odn->dn_type;
	ndn->dn_bonuslen = odn->dn_bonuslen;
	ndn->dn_bonustype = odn->dn_bonustype;
	ndn->dn_nblkptr = odn->dn_nblkptr;
	ndn->dn_checksum = odn->dn_checksum;
	ndn->dn_compress = odn->dn_compress;
	ndn->dn_nlevels = odn->dn_nlevels;
	ndn->dn_indblkshift = odn->dn_indblkshift;
	ndn->dn_datablkshift = odn->dn_datablkshift;
	ndn->dn_datablkszsec = odn->dn_datablkszsec;
	ndn->dn_datablksz = odn->dn_datablksz;
	ndn->dn_maxblkid = odn->dn_maxblkid;
	bcopy(&odn->dn_next_nblkptr[0], &ndn->dn_next_nblkptr[0],
	    sizeof (odn->dn_next_nblkptr));
	bcopy(&odn->dn_next_nlevels[0], &ndn->dn_next_nlevels[0],
	    sizeof (odn->dn_next_nlevels));
	bcopy(&odn->dn_next_indblkshift[0], &ndn->dn_next_indblkshift[0],
	    sizeof (odn->dn_next_indblkshift));
	bcopy(&odn->dn_next_bonustype[0], &ndn->dn_next_bonustype[0],
	    sizeof (odn->dn_next_bonustype));
	bcopy(&odn->dn_rm_spillblk[0], &ndn->dn_rm_spillblk[0],
	    sizeof (odn->dn_rm_spillblk));
	bcopy(&odn->dn_next_bonuslen[0], &ndn->dn_next_bonuslen[0],
	    sizeof (odn->dn_next_bonuslen));
	bcopy(&odn->dn_next_blksz[0], &ndn->dn_next_blksz[0],
	    sizeof (odn->dn_next_blksz));
	for (i = 0; i < TXG_SIZE; i++) {
		list_move_tail(&ndn->dn_dirty_records[i],
		    &odn->dn_dirty_records[i]);
	}
	bcopy(&odn->dn_ranges[0], &ndn->dn_ranges[0], sizeof (odn->dn_ranges));
	ndn->dn_allocated_txg = odn->dn_allocated_txg;
	ndn->dn_free_txg = odn->dn_free_txg;
	ndn->dn_assigned_txg = odn->dn_assigned_txg;
	ndn->dn_dirtyctx = odn->dn_dirtyctx;
	ndn->dn_dirtyctx_firstset = odn->dn_dirtyctx_firstset;
	ASSERT(refcount_count(&odn->dn_tx_holds) == 0);
	refcount_transfer(&ndn->dn_holds, &odn->dn_holds);
	ASSERT(list_is_empty(&ndn->dn_dbufs));
	list_move_tail(&ndn->dn_dbufs, &odn->dn_dbufs);
	ndn->dn_dbufs_count = odn->dn_dbufs_count;
	ndn->dn_bonus = odn->dn_bonus;
	ndn->dn_have_spill = odn->dn_have_spill;
	ndn->dn_zio = odn->dn_zio;
	ndn->dn_oldused = odn->dn_oldused;
	ndn->dn_oldflags = odn->dn_oldflags;
	ndn->dn_olduid = odn->dn_olduid;
	ndn->dn_oldgid = odn->dn_oldgid;
	ndn->dn_newuid = odn->dn_newuid;
	ndn->dn_newgid = odn->dn_newgid;
	ndn->dn_id_flags = odn->dn_id_flags;
	dmu_zfetch_init(&ndn->dn_zfetch, NULL);
	list_move_tail(&ndn->dn_zfetch.zf_stream, &odn->dn_zfetch.zf_stream);
	ndn->dn_zfetch.zf_dnode = odn->dn_zfetch.zf_dnode;
	ndn->dn_zfetch.zf_stream_cnt = odn->dn_zfetch.zf_stream_cnt;
	ndn->dn_zfetch.zf_alloc_fail = odn->dn_zfetch.zf_alloc_fail;

	/*
	 * Update back pointers. Updating the handle fixes the back pointer of
	 * every descendant dbuf as well as the bonus dbuf.
	 */
	ASSERT(ndn->dn_handle->dnh_dnode == odn);
	ndn->dn_handle->dnh_dnode = ndn;
	if (ndn->dn_zfetch.zf_dnode == odn) {
		ndn->dn_zfetch.zf_dnode = ndn;
	}

	/*
	 * Invalidate the original dnode by clearing all of its back pointers.
	 */
	odn->dn_dbuf = NULL;
	odn->dn_handle = NULL;
	list_create(&odn->dn_dbufs, sizeof (dmu_buf_impl_t),
	    offsetof(dmu_buf_impl_t, db_link));
	odn->dn_dbufs_count = 0;
	odn->dn_bonus = NULL;
	odn->dn_zfetch.zf_dnode = NULL;

	/*
	 * Set the low bit of the objset pointer to ensure that dnode_move()
	 * recognizes the dnode as invalid in any subsequent callback.
	 */
	POINTER_INVALIDATE(&odn->dn_objset);

	/*
	 * Satisfy the destructor.
	 */
	for (i = 0; i < TXG_SIZE; i++) {
		list_create(&odn->dn_dirty_records[i],
		    sizeof (dbuf_dirty_record_t),
		    offsetof(dbuf_dirty_record_t, dr_dirty_node));
		odn->dn_ranges[i].avl_root = NULL;
		odn->dn_ranges[i].avl_numnodes = 0;
		odn->dn_next_nlevels[i] = 0;
		odn->dn_next_indblkshift[i] = 0;
		odn->dn_next_bonustype[i] = 0;
		odn->dn_rm_spillblk[i] = 0;
		odn->dn_next_bonuslen[i] = 0;
		odn->dn_next_blksz[i] = 0;
	}
	odn->dn_allocated_txg = 0;
	odn->dn_free_txg = 0;
	odn->dn_assigned_txg = 0;
	odn->dn_dirtyctx = 0;
	odn->dn_dirtyctx_firstset = NULL;
	odn->dn_have_spill = B_FALSE;
	odn->dn_zio = NULL;
	odn->dn_oldused = 0;
	odn->dn_oldflags = 0;
	odn->dn_olduid = 0;
	odn->dn_oldgid = 0;
	odn->dn_newuid = 0;
	odn->dn_newgid = 0;
	odn->dn_id_flags = 0;

	/*
	 * Mark the dnode.
	 */
	ndn->dn_moved = 1;
	odn->dn_moved = (uint8_t)-1;
}

/*ARGSUSED*/
static kmem_cbrc_t
dnode_move(void *buf, void *newbuf, size_t size, void *arg)
{
	dnode_t *odn = buf, *ndn = newbuf;
	objset_t *os;
	int64_t refcount;
	uint32_t dbufs;

	/*
	 * The dnode is on the objset's list of known dnodes if the objset
	 * pointer is valid. We set the low bit of the objset pointer when
	 * freeing the dnode to invalidate it, and the memory patterns written
	 * by kmem (baddcafe and deadbeef) set at least one of the two low bits.
	 * A newly created dnode sets the objset pointer last of all to indicate
	 * that the dnode is known and in a valid state to be moved by this
	 * function.
	 */
	os = odn->dn_objset;
	if (!POINTER_IS_VALID(os)) {
		DNODE_STAT_ADD(dnode_move_stats.dms_dnode_invalid);
		return (KMEM_CBRC_DONT_KNOW);
	}

	/*
	 * Ensure that the objset does not go away during the move.
	 */
	rw_enter(&os_lock, RW_WRITER);
	if (os != odn->dn_objset) {
		rw_exit(&os_lock);
		DNODE_STAT_ADD(dnode_move_stats.dms_dnode_recheck1);
		return (KMEM_CBRC_DONT_KNOW);
	}

	/*
	 * If the dnode is still valid, then so is the objset. We know that no
	 * valid objset can be freed while we hold os_lock, so we can safely
	 * ensure that the objset remains in use.
	 */
	mutex_enter(&os->os_lock);

	/*
	 * Recheck the objset pointer in case the dnode was removed just before
	 * acquiring the lock.
	 */
	if (os != odn->dn_objset) {
		mutex_exit(&os->os_lock);
		rw_exit(&os_lock);
		DNODE_STAT_ADD(dnode_move_stats.dms_dnode_recheck2);
		return (KMEM_CBRC_DONT_KNOW);
	}

	/*
	 * At this point we know that as long as we hold os->os_lock, the dnode
	 * cannot be freed and fields within the dnode can be safely accessed.
	 * The objset listing this dnode cannot go away as long as this dnode is
	 * on its list.
	 */
	rw_exit(&os_lock);
	if (DMU_OBJECT_IS_SPECIAL(odn->dn_object)) {
		mutex_exit(&os->os_lock);
		DNODE_STAT_ADD(dnode_move_stats.dms_dnode_special);
		return (KMEM_CBRC_NO);
	}
	ASSERT(odn->dn_dbuf != NULL); /* only "special" dnodes have no parent */

	/*
	 * Lock the dnode handle to prevent the dnode from obtaining any new
	 * holds. This also prevents the descendant dbufs and the bonus dbuf
	 * from accessing the dnode, so that we can discount their holds. The
	 * handle is safe to access because we know that while the dnode cannot
	 * go away, neither can its handle. Once we hold dnh_zrlock, we can
	 * safely move any dnode referenced only by dbufs.
	 */
	if (!zrl_tryenter(&odn->dn_handle->dnh_zrlock)) {
		mutex_exit(&os->os_lock);
		DNODE_STAT_ADD(dnode_move_stats.dms_dnode_handle);
		return (KMEM_CBRC_LATER);
	}

	/*
	 * Ensure a consistent view of the dnode's holds and the dnode's dbufs.
	 * We need to guarantee that there is a hold for every dbuf in order to
	 * determine whether the dnode is actively referenced. Falsely matching
	 * a dbuf to an active hold would lead to an unsafe move. It's possible
	 * that a thread already having an active dnode hold is about to add a
	 * dbuf, and we can't compare hold and dbuf counts while the add is in
	 * progress.
	 */
	if (!rw_tryenter(&odn->dn_struct_rwlock, RW_WRITER)) {
		zrl_exit(&odn->dn_handle->dnh_zrlock);
		mutex_exit(&os->os_lock);
		DNODE_STAT_ADD(dnode_move_stats.dms_dnode_rwlock);
		return (KMEM_CBRC_LATER);
	}

	/*
	 * A dbuf may be removed (evicted) without an active dnode hold. In that
	 * case, the dbuf count is decremented under the handle lock before the
	 * dbuf's hold is released. This order ensures that if we count the hold
	 * after the dbuf is removed but before its hold is released, we will
	 * treat the unmatched hold as active and exit safely. If we count the
	 * hold before the dbuf is removed, the hold is discounted, and the
	 * removal is blocked until the move completes.
	 */
	refcount = refcount_count(&odn->dn_holds);
	ASSERT(refcount >= 0);
	dbufs = odn->dn_dbufs_count;

	/* We can't have more dbufs than dnode holds. */
	ASSERT3U(dbufs, <=, refcount);
	DTRACE_PROBE3(dnode__move, dnode_t *, odn, int64_t, refcount,
	    uint32_t, dbufs);

	if (refcount > dbufs) {
		rw_exit(&odn->dn_struct_rwlock);
		zrl_exit(&odn->dn_handle->dnh_zrlock);
		mutex_exit(&os->os_lock);
		DNODE_STAT_ADD(dnode_move_stats.dms_dnode_active);
		return (KMEM_CBRC_LATER);
	}

	rw_exit(&odn->dn_struct_rwlock);

	/*
	 * At this point we know that anyone with a hold on the dnode is not
	 * actively referencing it. The dnode is known and in a valid state to
	 * move. We're holding the locks needed to execute the critical section.
	 */
	dnode_move_impl(odn, ndn);

	list_link_replace(&odn->dn_link, &ndn->dn_link);
	/* If the dnode was safe to move, the refcount cannot have changed. */
	ASSERT(refcount == refcount_count(&ndn->dn_holds));
	ASSERT(dbufs == ndn->dn_dbufs_count);
	zrl_exit(&ndn->dn_handle->dnh_zrlock); /* handle has moved */
	mutex_exit(&os->os_lock);

	return (KMEM_CBRC_YES);
}
#endif	/* _KERNEL */

void
dnode_special_close(dnode_handle_t *dnh)
{
	dnode_t *dn = dnh->dnh_dnode;

	/*
	 * Wait for final references to the dnode to clear.  This can
	 * only happen if the arc is asyncronously evicting state that
	 * has a hold on this dnode while we are trying to evict this
	 * dnode.
	 */
	while (refcount_count(&dn->dn_holds) > 0)
		delay(1);
	zrl_add(&dnh->dnh_zrlock);
	dnode_destroy(dn); /* implicit zrl_remove() */
	zrl_destroy(&dnh->dnh_zrlock);
	dnh->dnh_dnode = NULL;
}

dnode_t *
dnode_special_open(objset_t *os, dnode_phys_t *dnp, uint64_t object,
    dnode_handle_t *dnh)
{
	dnode_t *dn = dnode_create(os, dnp, NULL, object, dnh);
	dnh->dnh_dnode = dn;
	zrl_init(&dnh->dnh_zrlock);
	DNODE_VERIFY(dn);
	return (dn);
}

static void
dnode_buf_pageout(dmu_buf_t *db, void *arg)
{
	dnode_children_t *children_dnodes = arg;
	int i;
	int epb = db->db_size >> DNODE_SHIFT;

	ASSERT(epb == children_dnodes->dnc_count);

	for (i = 0; i < epb; i++) {
		dnode_handle_t *dnh = &children_dnodes->dnc_children[i];
		dnode_t *dn;

		/*
		 * The dnode handle lock guards against the dnode moving to
		 * another valid address, so there is no need here to guard
		 * against changes to or from NULL.
		 */
		if (dnh->dnh_dnode == NULL) {
			zrl_destroy(&dnh->dnh_zrlock);
			continue;
		}

		zrl_add(&dnh->dnh_zrlock);
		dn = dnh->dnh_dnode;
		/*
		 * If there are holds on this dnode, then there should
		 * be holds on the dnode's containing dbuf as well; thus
		 * it wouldn't be eligible for eviction and this function
		 * would not have been called.
		 */
		ASSERT(refcount_is_zero(&dn->dn_holds));
		ASSERT(refcount_is_zero(&dn->dn_tx_holds));

		dnode_destroy(dn); /* implicit zrl_remove() */
		zrl_destroy(&dnh->dnh_zrlock);
		dnh->dnh_dnode = NULL;
	}
	kmem_free(children_dnodes, sizeof (dnode_children_t) +
	    (epb - 1) * sizeof (dnode_handle_t));
}

/*
 * errors:
 * EINVAL - invalid object number.
 * EIO - i/o error.
 * succeeds even for free dnodes.
 */
int
dnode_hold_impl(objset_t *os, uint64_t object, int flag,
    void *tag, dnode_t **dnp)
{
	int epb, idx, err;
	int drop_struct_lock = FALSE;
	int type;
	uint64_t blk;
	dnode_t *mdn, *dn;
	dmu_buf_impl_t *db;
	dnode_children_t *children_dnodes;
	dnode_handle_t *dnh;

	/*
	 * If you are holding the spa config lock as writer, you shouldn't
	 * be asking the DMU to do *anything* unless it's the root pool
	 * which may require us to read from the root filesystem while
	 * holding some (not all) of the locks as writer.
	 */
	ASSERT(spa_config_held(os->os_spa, SCL_ALL, RW_WRITER) == 0 ||
	    (spa_is_root(os->os_spa) &&
	    spa_config_held(os->os_spa, SCL_STATE, RW_WRITER)));

	if (object == DMU_USERUSED_OBJECT || object == DMU_GROUPUSED_OBJECT) {
		dn = (object == DMU_USERUSED_OBJECT) ?
		    DMU_USERUSED_DNODE(os) : DMU_GROUPUSED_DNODE(os);
		if (dn == NULL)
			return (ENOENT);
		type = dn->dn_type;
		if ((flag & DNODE_MUST_BE_ALLOCATED) && type == DMU_OT_NONE)
			return (ENOENT);
		if ((flag & DNODE_MUST_BE_FREE) && type != DMU_OT_NONE)
			return (EEXIST);
		DNODE_VERIFY(dn);
		(void) refcount_add(&dn->dn_holds, tag);
		*dnp = dn;
		return (0);
	}

	if (object == 0 || object >= DN_MAX_OBJECT)
		return (EINVAL);

	mdn = DMU_META_DNODE(os);
	ASSERT(mdn->dn_object == DMU_META_DNODE_OBJECT);

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

	ASSERT(DB_DNODE(db)->dn_type == DMU_OT_DNODE);
	children_dnodes = dmu_buf_get_user(&db->db);
	if (children_dnodes == NULL) {
		int i;
		dnode_children_t *winner;
		children_dnodes = kmem_alloc(sizeof (dnode_children_t) +
		    (epb - 1) * sizeof (dnode_handle_t),
		    KM_PUSHPAGE | KM_NODEBUG);
		children_dnodes->dnc_count = epb;
		dnh = &children_dnodes->dnc_children[0];
		for (i = 0; i < epb; i++) {
			zrl_init(&dnh[i].dnh_zrlock);
			dnh[i].dnh_dnode = NULL;
		}
		if ((winner = dmu_buf_set_user(&db->db, children_dnodes, NULL,
		    dnode_buf_pageout))) {
			kmem_free(children_dnodes, sizeof (dnode_children_t) +
			    (epb - 1) * sizeof (dnode_handle_t));
			children_dnodes = winner;
		}
	}
	ASSERT(children_dnodes->dnc_count == epb);

	dnh = &children_dnodes->dnc_children[idx];
	zrl_add(&dnh->dnh_zrlock);
	if ((dn = dnh->dnh_dnode) == NULL) {
		dnode_phys_t *phys = (dnode_phys_t *)db->db.db_data+idx;
		dnode_t *winner;

		dn = dnode_create(os, phys, db, object, dnh);
		winner = atomic_cas_ptr(&dnh->dnh_dnode, NULL, dn);
		if (winner != NULL) {
			zrl_add(&dnh->dnh_zrlock);
			dnode_destroy(dn); /* implicit zrl_remove() */
			dn = winner;
		}
	}

	mutex_enter(&dn->dn_mtx);
	type = dn->dn_type;
	if (dn->dn_free_txg ||
	    ((flag & DNODE_MUST_BE_ALLOCATED) && type == DMU_OT_NONE) ||
	    ((flag & DNODE_MUST_BE_FREE) &&
	    (type != DMU_OT_NONE || !refcount_is_zero(&dn->dn_holds)))) {
		mutex_exit(&dn->dn_mtx);
		zrl_remove(&dnh->dnh_zrlock);
		dbuf_rele(db, FTAG);
		return (type == DMU_OT_NONE ? ENOENT : EEXIST);
	}
	mutex_exit(&dn->dn_mtx);

	if (refcount_add(&dn->dn_holds, tag) == 1)
		dbuf_add_ref(db, dnh);
	/* Now we can rely on the hold to prevent the dnode from moving. */
	zrl_remove(&dnh->dnh_zrlock);

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
dnode_hold(objset_t *os, uint64_t object, void *tag, dnode_t **dnp)
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
	/* Get while the hold prevents the dnode from moving. */
	dmu_buf_impl_t *db = dn->dn_dbuf;
	dnode_handle_t *dnh = dn->dn_handle;

	mutex_enter(&dn->dn_mtx);
	refs = refcount_remove(&dn->dn_holds, tag);
	mutex_exit(&dn->dn_mtx);

	/*
	 * It's unsafe to release the last hold on a dnode by dnode_rele() or
	 * indirectly by dbuf_rele() while relying on the dnode handle to
	 * prevent the dnode from moving, since releasing the last hold could
	 * result in the dnode's parent dbuf evicting its dnode handles. For
	 * that reason anyone calling dnode_rele() or dbuf_rele() without some
	 * other direct or indirect hold on the dnode must first drop the dnode
	 * handle.
	 */
	ASSERT(refs > 0 || dnh->dnh_zrlock.zr_owner != curthread);

	/* NOTE: the DNODE_DNODE does not have a dn_dbuf */
	if (refs == 0 && db != NULL) {
		/*
		 * Another thread could add a hold to the dnode handle in
		 * dnode_hold_impl() while holding the parent dbuf. Since the
		 * hold on the parent dbuf prevents the handle from being
		 * destroyed, the hold on the handle is OK. We can't yet assert
		 * that the handle has zero references, but that will be
		 * asserted anyway when the handle gets destroyed.
		 */
		dbuf_rele(db, dnh);
	}
}

void
dnode_setdirty(dnode_t *dn, dmu_tx_t *tx)
{
	objset_t *os = dn->dn_objset;
	uint64_t txg = tx->tx_txg;

	if (DMU_OBJECT_IS_SPECIAL(dn->dn_object)) {
		dsl_dataset_dirty(os->os_dsl_dataset, tx);
		return;
	}

	DNODE_VERIFY(dn);

#ifdef ZFS_DEBUG
	mutex_enter(&dn->dn_mtx);
	ASSERT(dn->dn_phys->dn_type || dn->dn_allocated_txg);
	ASSERT(dn->dn_free_txg == 0 || dn->dn_free_txg >= txg);
	mutex_exit(&dn->dn_mtx);
#endif

	/*
	 * Determine old uid/gid when necessary
	 */
	dmu_objset_userquota_get_ids(dn, B_TRUE, tx);

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
	ASSERT3U(dn->dn_next_bonustype[txg&TXG_MASK], ==, 0);

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
	 * dbuf maintains a hold on the dnode.  When the last child
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

		if (db->db_blkid != 0 && db->db_blkid != DMU_BONUS_BLKID &&
		    db->db_blkid != DMU_SPILL_BLKID) {
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

	ASSERT(blkid != DMU_BONUS_BLKID);

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
		ASSERT(db != NULL);
		new = dbuf_dirty(db, tx);
		dbuf_rele(db, FTAG);

		/* transfer the dirty records to the new indirect */
		mutex_enter(&dn->dn_mtx);
		mutex_enter(&new->dt.di.dr_mtx);
		list = &dn->dn_dirty_records[txgoff];
		for (dr = list_head(list); dr; dr = dr_next) {
			dr_next = list_next(&dn->dn_dirty_records[txgoff], dr);
			if (dr->dr_dbuf->db_level != new_nlevels-1 &&
			    dr->dr_dbuf->db_blkid != DMU_BONUS_BLKID &&
			    dr->dr_dbuf->db_blkid != DMU_SPILL_BLKID) {
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
			    kmem_alloc(sizeof (free_range_t), KM_PUSHPAGE);

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
		if ((db = dbuf_hold_level(dn, 1, first, FTAG))) {
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
		rp = kmem_alloc(sizeof (free_range_t), KM_PUSHPAGE);
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

static boolean_t
dnode_spill_freed(dnode_t *dn)
{
	int i;

	mutex_enter(&dn->dn_mtx);
	for (i = 0; i < TXG_SIZE; i++) {
		if (dn->dn_rm_spillblk[i] == DN_KILL_SPILLBLK)
			break;
	}
	mutex_exit(&dn->dn_mtx);
	return (i < TXG_SIZE);
}

/* return TRUE if this blkid was freed in a recent txg, or FALSE if it wasn't */
uint64_t
dnode_block_freed(dnode_t *dn, uint64_t blkid)
{
	free_range_t range_tofind;
	void *dp = spa_get_dsl(dn->dn_objset->os_spa);
	int i;

	if (blkid == DMU_BONUS_BLKID)
		return (FALSE);

	/*
	 * If we're in the process of opening the pool, dp will not be
	 * set yet, but there shouldn't be anything dirty.
	 */
	if (dp == NULL)
		return (FALSE);

	if (dn->dn_free_txg)
		return (TRUE);

	if (blkid == DMU_SPILL_BLKID)
		return (dnode_spill_freed(dn));

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
	objset_t *os = dn->dn_objset;
	dsl_dataset_t *ds = os->os_dsl_dataset;

	if (space > 0)
		space = spa_get_asize(os->os_spa, space);

	if (ds)
		dsl_dir_willuse_space(ds->ds_dir, space, tx);

	dmu_tx_willuse_space(tx, space);
}

/*
 * This function scans a block at the indicated "level" looking for
 * a hole or data (depending on 'flags').  If level > 0, then we are
 * scanning an indirect block looking at its pointers.  If level == 0,
 * then we are looking at a block of dnodes.  If we don't find what we
 * are looking for in the block, we return ESRCH.  Otherwise, return
 * with *offset pointing to the beginning (if searching forwards) or
 * end (if searching backwards) of the range covered by the block
 * pointer we matched on (or dnode).
 *
 * The basic search algorithm used below by dnode_next_offset() is to
 * use this function to search up the block tree (widen the search) until
 * we find something (i.e., we don't return ESRCH) and then search back
 * down the tree (narrow the search) until we reach our original search
 * level.
 */
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

	hole = ((flags & DNODE_FIND_HOLE) != 0);
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
			if ((dnp[i].dn_type == DMU_OT_NONE) == hole)
				break;
			*offset += (1ULL << span) * inc;
		}
		if (i < 0 || i == blkfill)
			error = ESRCH;
	} else {
		blkptr_t *bp = data;
		uint64_t start = *offset;
		span = (lvl - 1) * epbs + dn->dn_datablkshift;
		minfill = 0;
		maxfill = blkfill << ((lvl - 1) * epbs);

		if (hole)
			maxfill--;
		else
			minfill++;

		*offset = *offset >> span;
		for (i = BF64_GET(*offset, 0, epbs);
		    i >= 0 && i < epb; i += inc) {
			if (bp[i].blk_fill >= minfill &&
			    bp[i].blk_fill <= maxfill &&
			    (hole || bp[i].blk_birth > txg))
				break;
			if (inc > 0 || *offset > 0)
				*offset += inc;
		}
		*offset = *offset << span;
		if (inc < 0) {
			/* traversing backwards; position offset at the end */
			ASSERT3U(*offset, <=, start);
			*offset = MIN(*offset + (1ULL << span) - 1, start);
		} else if (*offset < start) {
			*offset = start;
		}
		if (i < 0 || i >= epb)
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
