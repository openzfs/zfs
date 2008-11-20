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

#pragma ident	"@(#)dmu_traverse.c	1.7	08/04/01 SMI"

#include <sys/zfs_context.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_traverse.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_pool.h>
#include <sys/dnode.h>
#include <sys/spa.h>
#include <sys/zio.h>
#include <sys/dmu_impl.h>
#include <sys/zvol.h>

#define	BP_SPAN_SHIFT(level, width)	((level) * (width))

#define	BP_EQUAL(b1, b2)				\
	(DVA_EQUAL(BP_IDENTITY(b1), BP_IDENTITY(b2)) &&	\
	(b1)->blk_birth == (b2)->blk_birth)

/*
 * Compare two bookmarks.
 *
 * For ADVANCE_PRE, the visitation order is:
 *
 *	objset 0, 1, 2, ..., ZB_MAXOBJSET.
 *	object 0, 1, 2, ..., ZB_MAXOBJECT.
 *	blkoff 0, 1, 2, ...
 *	level ZB_MAXLEVEL, ..., 2, 1, 0.
 *
 * where blkoff = blkid << BP_SPAN_SHIFT(level, width), and thus a valid
 * ordering vector is:
 *
 *	< objset, object, blkoff, -level >
 *
 * For ADVANCE_POST, the starting offsets aren't sequential but ending
 * offsets [blkoff = (blkid + 1) << BP_SPAN_SHIFT(level, width)] are.
 * The visitation order is:
 *
 *	objset 1, 2, ..., ZB_MAXOBJSET, 0.
 *	object 1, 2, ..., ZB_MAXOBJECT, 0.
 *	blkoff 1, 2, ...
 *	level 0, 1, 2, ..., ZB_MAXLEVEL.
 *
 * and thus a valid ordering vector is:
 *
 *	< objset - 1, object - 1, blkoff, level >
 *
 * Both orderings can be expressed as:
 *
 *	< objset + bias, object + bias, blkoff, level ^ bias >
 *
 * where 'bias' is either 0 or -1 (for ADVANCE_PRE or ADVANCE_POST)
 * and 'blkoff' is (blkid - bias) << BP_SPAN_SHIFT(level, wshift).
 *
 * Special case: an objset's osphys is represented as level -1 of object 0.
 * It is always either the very first or very last block we visit in an objset.
 * Therefore, if either bookmark's level is -1, level alone determines order.
 */
static int
compare_bookmark(zbookmark_t *szb, zbookmark_t *ezb, dnode_phys_t *dnp,
    int advance)
{
	int bias = (advance & ADVANCE_PRE) ? 0 : -1;
	uint64_t sblkoff, eblkoff;
	int slevel, elevel, wshift;

	if (szb->zb_objset + bias < ezb->zb_objset + bias)
		return (-1);

	if (szb->zb_objset + bias > ezb->zb_objset + bias)
		return (1);

	slevel = szb->zb_level;
	elevel = ezb->zb_level;

	if ((slevel | elevel) < 0)
		return ((slevel ^ bias) - (elevel ^ bias));

	if (szb->zb_object + bias < ezb->zb_object + bias)
		return (-1);

	if (szb->zb_object + bias > ezb->zb_object + bias)
		return (1);

	if (dnp == NULL)
		return (0);

	wshift = dnp->dn_indblkshift - SPA_BLKPTRSHIFT;

	sblkoff = (szb->zb_blkid - bias) << BP_SPAN_SHIFT(slevel, wshift);
	eblkoff = (ezb->zb_blkid - bias) << BP_SPAN_SHIFT(elevel, wshift);

	if (sblkoff < eblkoff)
		return (-1);

	if (sblkoff > eblkoff)
		return (1);

	return ((elevel ^ bias) - (slevel ^ bias));
}

#define	SET_BOOKMARK(zb, objset, object, level, blkid)	\
{							\
	(zb)->zb_objset = objset;			\
	(zb)->zb_object = object;			\
	(zb)->zb_level = level;				\
	(zb)->zb_blkid = blkid;				\
}

#define	SET_BOOKMARK_LB(zb, level, blkid)		\
{							\
	(zb)->zb_level = level;				\
	(zb)->zb_blkid = blkid;				\
}

static int
advance_objset(zseg_t *zseg, uint64_t objset, int advance)
{
	zbookmark_t *zb = &zseg->seg_start;

	if (advance & ADVANCE_PRE) {
		if (objset >= ZB_MAXOBJSET)
			return (ERANGE);
		SET_BOOKMARK(zb, objset, 0, -1, 0);
	} else {
		if (objset >= ZB_MAXOBJSET)
			objset = 0;
		SET_BOOKMARK(zb, objset, 1, 0, 0);
	}

	if (compare_bookmark(zb, &zseg->seg_end, NULL, advance) > 0)
		return (ERANGE);

	return (EAGAIN);
}

static int
advance_object(zseg_t *zseg, uint64_t object, int advance)
{
	zbookmark_t *zb = &zseg->seg_start;

	if (advance & ADVANCE_PRE) {
		if (object >= ZB_MAXOBJECT) {
			SET_BOOKMARK(zb, zb->zb_objset + 1, 0, -1, 0);
		} else {
			SET_BOOKMARK(zb, zb->zb_objset, object, ZB_MAXLEVEL, 0);
		}
	} else {
		if (zb->zb_object == 0) {
			SET_BOOKMARK(zb, zb->zb_objset, 0, -1, 0);
		} else {
			if (object >= ZB_MAXOBJECT)
				object = 0;
			SET_BOOKMARK(zb, zb->zb_objset, object, 0, 0);
		}
	}

	if (compare_bookmark(zb, &zseg->seg_end, NULL, advance) > 0)
		return (ERANGE);

	return (EAGAIN);
}

static int
advance_from_osphys(zseg_t *zseg, int advance)
{
	zbookmark_t *zb = &zseg->seg_start;

	ASSERT(zb->zb_object == 0);
	ASSERT(zb->zb_level == -1);
	ASSERT(zb->zb_blkid == 0);

	if (advance & ADVANCE_PRE) {
		SET_BOOKMARK_LB(zb, ZB_MAXLEVEL, 0);
	} else {
		if (zb->zb_objset == 0)
			return (ERANGE);
		SET_BOOKMARK(zb, zb->zb_objset + 1, 1, 0, 0);
	}

	if (compare_bookmark(zb, &zseg->seg_end, NULL, advance) > 0)
		return (ERANGE);

	return (EAGAIN);
}

static int
advance_block(zseg_t *zseg, dnode_phys_t *dnp, int rc, int advance)
{
	zbookmark_t *zb = &zseg->seg_start;
	int wshift = dnp->dn_indblkshift - SPA_BLKPTRSHIFT;
	int maxlevel = dnp->dn_nlevels - 1;
	int level = zb->zb_level;
	uint64_t blkid = zb->zb_blkid;

	if (advance & ADVANCE_PRE) {
		if (level > 0 && rc == 0) {
			level--;
			blkid <<= wshift;
		} else {
			blkid++;

			if ((blkid << BP_SPAN_SHIFT(level, wshift)) >
			    dnp->dn_maxblkid)
				return (ERANGE);

			while (level < maxlevel) {
				if (P2PHASE(blkid, 1ULL << wshift))
					break;
				blkid >>= wshift;
				level++;
			}
		}
	} else {
		if (level >= maxlevel || P2PHASE(blkid + 1, 1ULL << wshift)) {
			blkid = (blkid + 1) << BP_SPAN_SHIFT(level, wshift);
			level = 0;
		} else {
			blkid >>= wshift;
			level++;
		}

		while ((blkid << BP_SPAN_SHIFT(level, wshift)) >
		    dnp->dn_maxblkid) {
			if (level == maxlevel)
				return (ERANGE);
			blkid >>= wshift;
			level++;
		}
	}
	SET_BOOKMARK_LB(zb, level, blkid);

	if (compare_bookmark(zb, &zseg->seg_end, dnp, advance) > 0)
		return (ERANGE);

	return (EAGAIN);
}

/*
 * The traverse_callback function will call the function specified in th_func.
 * In the event of an error the callee, specified by th_func, must return
 * one of the following errors:
 *
 *	EINTR		- Indicates that the callee wants the traversal to
 *			  abort immediately.
 * 	ERESTART	- The callee has acknowledged the error and would
 *			  like to continue.
 */
static int
traverse_callback(traverse_handle_t *th, zseg_t *zseg, traverse_blk_cache_t *bc)
{
	/*
	 * Before we issue the callback, prune against maxtxg.
	 *
	 * We prune against mintxg before we get here because it's a big win.
	 * If a given block was born in txg 37, then we know that the entire
	 * subtree below that block must have been born in txg 37 or earlier.
	 * We can therefore lop off huge branches of the tree as we go.
	 *
	 * There's no corresponding optimization for maxtxg because knowing
	 * that bp->blk_birth >= maxtxg doesn't imply anything about the bp's
	 * children.  In fact, the copy-on-write design of ZFS ensures that
	 * top-level blocks will pretty much always be new.
	 *
	 * Therefore, in the name of simplicity we don't prune against
	 * maxtxg until the last possible moment -- that being right now.
	 */
	if (bc->bc_errno == 0 && bc->bc_blkptr.blk_birth >= zseg->seg_maxtxg)
		return (0);

	/*
	 * Debugging: verify that the order we visit things agrees with the
	 * order defined by compare_bookmark().  We don't check this for
	 * log blocks because there's no defined ordering for them; they're
	 * always visited (or not) as part of visiting the objset_phys_t.
	 */
	if (bc->bc_errno == 0 && bc != &th->th_zil_cache) {
                zbookmark_t *zb = &bc->bc_bookmark;
                zbookmark_t *lzb = &th->th_lastcb;

                ASSERT(compare_bookmark(zb, &zseg->seg_end, bc->bc_dnode,
                                        th->th_advance) <= 0);
                ASSERT(compare_bookmark(zb, &zseg->seg_start, bc->bc_dnode,
                                        th->th_advance) == 0);
                ASSERT(compare_bookmark(lzb, zb, bc->bc_dnode,
                                        th->th_advance) < 0 ||
                                        lzb->zb_level == ZB_NO_LEVEL);
		*lzb = *zb;
	}

	th->th_callbacks++;
	return (th->th_func(bc, th->th_spa, th->th_arg));
}

static int
traverse_read(traverse_handle_t *th, traverse_blk_cache_t *bc, blkptr_t *bp,
	dnode_phys_t *dnp)
{
	zbookmark_t *zb = &bc->bc_bookmark;
	int error;

	th->th_hits++;

	bc->bc_dnode = dnp;
	bc->bc_errno = 0;

	if (BP_EQUAL(&bc->bc_blkptr, bp))
		return (0);

	bc->bc_blkptr = *bp;

	if (bc->bc_data == NULL)
		return (0);

	if (BP_IS_HOLE(bp)) {
		ASSERT(th->th_advance & ADVANCE_HOLES);
		return (0);
	}

	if (compare_bookmark(zb, &th->th_noread, dnp, 0) == 0) {
		error = EIO;
	} else if (arc_tryread(th->th_spa, bp, bc->bc_data) == 0) {
		error = 0;
		th->th_arc_hits++;
	} else {
		error = zio_wait(zio_read(NULL, th->th_spa, bp, bc->bc_data,
		    BP_GET_LSIZE(bp), NULL, NULL, ZIO_PRIORITY_SYNC_READ,
		    th->th_zio_flags | ZIO_FLAG_DONT_CACHE, zb));

		if (BP_SHOULD_BYTESWAP(bp) && error == 0)
			(zb->zb_level > 0 ? byteswap_uint64_array :
			    dmu_ot[BP_GET_TYPE(bp)].ot_byteswap)(bc->bc_data,
			    BP_GET_LSIZE(bp));
		th->th_reads++;
	}

	if (error) {
		bc->bc_errno = error;
		error = traverse_callback(th, NULL, bc);
		ASSERT(error == EAGAIN || error == EINTR || error == ERESTART);
		bc->bc_blkptr.blk_birth = -1ULL;
	}

	dprintf("cache %02x error %d <%llu, %llu, %d, %llx>\n",
	    bc - &th->th_cache[0][0], error,
	    zb->zb_objset, zb->zb_object, zb->zb_level, zb->zb_blkid);

	return (error);
}

static int
find_block(traverse_handle_t *th, zseg_t *zseg, dnode_phys_t *dnp, int depth)
{
	zbookmark_t *zb = &zseg->seg_start;
	traverse_blk_cache_t *bc;
	blkptr_t *bp = dnp->dn_blkptr;
	int i, first, level;
	int nbp = dnp->dn_nblkptr;
	int minlevel = zb->zb_level;
	int maxlevel = dnp->dn_nlevels - 1;
	int wshift = dnp->dn_indblkshift - SPA_BLKPTRSHIFT;
	int bp_shift = BP_SPAN_SHIFT(maxlevel - minlevel, wshift);
	uint64_t blkid = zb->zb_blkid >> bp_shift;
	int do_holes = (th->th_advance & ADVANCE_HOLES) && depth == ZB_DN_CACHE;
	int rc;

	if (minlevel > maxlevel || blkid >= nbp)
		return (ERANGE);

	for (level = maxlevel; level >= minlevel; level--) {
		first = P2PHASE(blkid, 1ULL << wshift);

		for (i = first; i < nbp; i++)
			if (bp[i].blk_birth > zseg->seg_mintxg ||
			    (BP_IS_HOLE(&bp[i]) && do_holes))
				break;

		if (i != first) {
			i--;
			SET_BOOKMARK_LB(zb, level, blkid + (i - first));
			return (ENOTBLK);
		}

		bc = &th->th_cache[depth][level];

		SET_BOOKMARK(&bc->bc_bookmark, zb->zb_objset, zb->zb_object,
		    level, blkid);

		if ((rc = traverse_read(th, bc, bp + i, dnp))) {
			if (rc != EAGAIN) {
				SET_BOOKMARK_LB(zb, level, blkid);
			}
			return (rc);
		}

		if (BP_IS_HOLE(&bp[i])) {
			SET_BOOKMARK_LB(zb, level, blkid);
			th->th_lastcb.zb_level = ZB_NO_LEVEL;
			return (0);
		}

		nbp = 1 << wshift;
		bp = bc->bc_data;
		bp_shift -= wshift;
		blkid = zb->zb_blkid >> bp_shift;
	}

	return (0);
}

static int
get_dnode(traverse_handle_t *th, uint64_t objset, dnode_phys_t *mdn,
    uint64_t *objectp, dnode_phys_t **dnpp, uint64_t txg, int type, int depth)
{
	zseg_t zseg;
	zbookmark_t *zb = &zseg.seg_start;
	uint64_t object = *objectp;
	int i, rc;

	SET_BOOKMARK(zb, objset, 0, 0, object / DNODES_PER_BLOCK);
	SET_BOOKMARK(&zseg.seg_end, objset, 0, 0, ZB_MAXBLKID);

	zseg.seg_mintxg = txg;
	zseg.seg_maxtxg = -1ULL;

	for (;;) {
		rc = find_block(th, &zseg, mdn, depth);

		if (rc == EAGAIN || rc == EINTR || rc == ERANGE)
			break;

		if (rc == 0 && zb->zb_level == 0) {
			dnode_phys_t *dnp = th->th_cache[depth][0].bc_data;
			for (i = 0; i < DNODES_PER_BLOCK; i++) {
				object = (zb->zb_blkid * DNODES_PER_BLOCK) + i;
				if (object >= *objectp &&
				    dnp[i].dn_type != DMU_OT_NONE &&
				    (type == -1 || dnp[i].dn_type == type)) {
					*objectp = object;
					*dnpp = &dnp[i];
					return (0);
				}
			}
		}

		rc = advance_block(&zseg, mdn, rc, ADVANCE_PRE);

		if (rc == ERANGE)
			break;
	}

	if (rc == ERANGE)
		*objectp = ZB_MAXOBJECT;

	return (rc);
}

/* ARGSUSED */
static void
traverse_zil_block(zilog_t *zilog, blkptr_t *bp, void *arg, uint64_t claim_txg)
{
	traverse_handle_t *th = arg;
	traverse_blk_cache_t *bc = &th->th_zil_cache;
	zbookmark_t *zb = &bc->bc_bookmark;
	zseg_t *zseg = list_head(&th->th_seglist);

	if (bp->blk_birth <= zseg->seg_mintxg)
		return;

	if (claim_txg != 0 || bp->blk_birth < spa_first_txg(th->th_spa)) {
		zb->zb_object = 0;
		zb->zb_blkid = bp->blk_cksum.zc_word[ZIL_ZC_SEQ];
		bc->bc_blkptr = *bp;
		(void) traverse_callback(th, zseg, bc);
	}
}

/* ARGSUSED */
static void
traverse_zil_record(zilog_t *zilog, lr_t *lrc, void *arg, uint64_t claim_txg)
{
	traverse_handle_t *th = arg;
	traverse_blk_cache_t *bc = &th->th_zil_cache;
	zbookmark_t *zb = &bc->bc_bookmark;
	zseg_t *zseg = list_head(&th->th_seglist);

	if (lrc->lrc_txtype == TX_WRITE) {
		lr_write_t *lr = (lr_write_t *)lrc;
		blkptr_t *bp = &lr->lr_blkptr;

		if (bp->blk_birth <= zseg->seg_mintxg)
			return;

		if (claim_txg != 0 && bp->blk_birth >= claim_txg) {
			zb->zb_object = lr->lr_foid;
			zb->zb_blkid = lr->lr_offset / BP_GET_LSIZE(bp);
			bc->bc_blkptr = *bp;
			(void) traverse_callback(th, zseg, bc);
		}
	}
}

static void
traverse_zil(traverse_handle_t *th, traverse_blk_cache_t *bc)
{
	spa_t *spa = th->th_spa;
	dsl_pool_t *dp = spa_get_dsl(spa);
	objset_phys_t *osphys = bc->bc_data;
	zil_header_t *zh = &osphys->os_zil_header;
	uint64_t claim_txg = zh->zh_claim_txg;
	zilog_t *zilog;

	ASSERT(bc == &th->th_cache[ZB_MDN_CACHE][ZB_MAXLEVEL - 1]);
	ASSERT(bc->bc_bookmark.zb_level == -1);

	/*
	 * We only want to visit blocks that have been claimed but not yet
	 * replayed (or, in read-only mode, blocks that *would* be claimed).
	 */
	if (claim_txg == 0 && (spa_mode & FWRITE))
		return;

	th->th_zil_cache.bc_bookmark = bc->bc_bookmark;

	zilog = zil_alloc(dp->dp_meta_objset, zh);

	(void) zil_parse(zilog, traverse_zil_block, traverse_zil_record, th,
	    claim_txg);

	zil_free(zilog);
}

static int
traverse_segment(traverse_handle_t *th, zseg_t *zseg, blkptr_t *mosbp)
{
	zbookmark_t *zb = &zseg->seg_start;
	traverse_blk_cache_t *bc;
	dnode_phys_t *dn, *dn_tmp;
	int worklimit = 100;
	int rc;

	dprintf("<%llu, %llu, %d, %llx>\n",
	    zb->zb_objset, zb->zb_object, zb->zb_level, zb->zb_blkid);

	bc = &th->th_cache[ZB_MOS_CACHE][ZB_MAXLEVEL - 1];
	dn = &((objset_phys_t *)bc->bc_data)->os_meta_dnode;

	SET_BOOKMARK(&bc->bc_bookmark, 0, 0, -1, 0);

	rc = traverse_read(th, bc, mosbp, dn);

	if (rc)		/* If we get ERESTART, we've got nowhere left to go */
		return (rc == ERESTART ? EINTR : rc);

	ASSERT(dn->dn_nlevels < ZB_MAXLEVEL);

	if (zb->zb_objset != 0) {
		uint64_t objset = zb->zb_objset;
		dsl_dataset_phys_t *dsp;

		rc = get_dnode(th, 0, dn, &objset, &dn_tmp, 0,
		    DMU_OT_DSL_DATASET, ZB_MOS_CACHE);

		if (objset != zb->zb_objset)
			rc = advance_objset(zseg, objset, th->th_advance);

		if (rc != 0)
			return (rc);

		dsp = DN_BONUS(dn_tmp);

		bc = &th->th_cache[ZB_MDN_CACHE][ZB_MAXLEVEL - 1];
		dn = &((objset_phys_t *)bc->bc_data)->os_meta_dnode;

		SET_BOOKMARK(&bc->bc_bookmark, objset, 0, -1, 0);

		/*
		 * If we're traversing an open snapshot, we know that it
		 * can't be deleted (because it's open) and it can't change
		 * (because it's a snapshot).  Therefore, once we've gotten
		 * from the uberblock down to the snapshot's objset_phys_t,
		 * we no longer need to synchronize with spa_sync(); we're
		 * traversing a completely static block tree from here on.
		 */
		if (th->th_advance & ADVANCE_NOLOCK) {
			ASSERT(th->th_locked);
			rw_exit(spa_traverse_rwlock(th->th_spa));
			th->th_locked = 0;
		}

		rc = traverse_read(th, bc, &dsp->ds_bp, dn);

		if (rc != 0) {
			if (rc == ERESTART)
				rc = advance_objset(zseg, zb->zb_objset + 1,
				    th->th_advance);
			return (rc);
		}

		if (th->th_advance & ADVANCE_PRUNE)
			zseg->seg_mintxg =
			    MAX(zseg->seg_mintxg, dsp->ds_prev_snap_txg);
	}

	if (zb->zb_level == -1) {
		ASSERT(zb->zb_object == 0);
		ASSERT(zb->zb_blkid == 0);
		ASSERT(BP_GET_TYPE(&bc->bc_blkptr) == DMU_OT_OBJSET);

		if (bc->bc_blkptr.blk_birth > zseg->seg_mintxg) {
			rc = traverse_callback(th, zseg, bc);
			if (rc) {
				ASSERT(rc == EINTR);
				return (rc);
			}
			if ((th->th_advance & ADVANCE_ZIL) &&
			    zb->zb_objset != 0)
				traverse_zil(th, bc);
		}

		return (advance_from_osphys(zseg, th->th_advance));
	}

	if (zb->zb_object != 0) {
		uint64_t object = zb->zb_object;

		rc = get_dnode(th, zb->zb_objset, dn, &object, &dn_tmp,
		    zseg->seg_mintxg, -1, ZB_MDN_CACHE);

		if (object != zb->zb_object)
			rc = advance_object(zseg, object, th->th_advance);

		if (rc != 0)
			return (rc);

		dn = dn_tmp;
	}

	if (zb->zb_level == ZB_MAXLEVEL)
		zb->zb_level = dn->dn_nlevels - 1;

	for (;;) {
		rc = find_block(th, zseg, dn, ZB_DN_CACHE);

		if (rc == EAGAIN || rc == EINTR || rc == ERANGE)
			break;

		if (rc == 0) {
			bc = &th->th_cache[ZB_DN_CACHE][zb->zb_level];
			ASSERT(bc->bc_dnode == dn);
			ASSERT(bc->bc_blkptr.blk_birth <= mosbp->blk_birth);
			rc = traverse_callback(th, zseg, bc);
			if (rc) {
				ASSERT(rc == EINTR);
				return (rc);
			}
			if (BP_IS_HOLE(&bc->bc_blkptr)) {
				ASSERT(th->th_advance & ADVANCE_HOLES);
				rc = ENOTBLK;
			}
		}

		rc = advance_block(zseg, dn, rc, th->th_advance);

		if (rc == ERANGE)
			break;

		/*
		 * Give spa_sync() a chance to run.
		 */
		if (th->th_locked && spa_traverse_wanted(th->th_spa)) {
			th->th_syncs++;
			return (EAGAIN);
		}

		if (--worklimit == 0)
			return (EAGAIN);
	}

	if (rc == ERANGE)
		rc = advance_object(zseg, zb->zb_object + 1, th->th_advance);

	return (rc);
}

/*
 * It is the caller's responsibility to ensure that the dsl_dataset_t
 * doesn't go away during traversal.
 */
int
traverse_dsl_dataset(dsl_dataset_t *ds, uint64_t txg_start, int advance,
    blkptr_cb_t func, void *arg)
{
	spa_t *spa = ds->ds_dir->dd_pool->dp_spa;
	traverse_handle_t *th;
	int err;

	th = traverse_init(spa, func, arg, advance, ZIO_FLAG_MUSTSUCCEED);

	traverse_add_objset(th, txg_start, -1ULL, ds->ds_object);

	while ((err = traverse_more(th)) == EAGAIN)
		continue;

	traverse_fini(th);
	return (err);
}

int
traverse_zvol(objset_t *os, int advance,  blkptr_cb_t func, void *arg)
{
	spa_t *spa = dmu_objset_spa(os);
	traverse_handle_t *th;
	int err;

	th = traverse_init(spa, func, arg, advance, ZIO_FLAG_CANFAIL);

	traverse_add_dnode(th, 0, -1ULL, dmu_objset_id(os), ZVOL_OBJ);

	while ((err = traverse_more(th)) == EAGAIN)
		continue;

	traverse_fini(th);
	return (err);
}

int
traverse_more(traverse_handle_t *th)
{
	zseg_t *zseg = list_head(&th->th_seglist);
	uint64_t save_txg;	/* XXX won't be necessary with real itinerary */
	krwlock_t *rw = spa_traverse_rwlock(th->th_spa);
	blkptr_t *mosbp = spa_get_rootblkptr(th->th_spa);
	int rc;

	if (zseg == NULL)
		return (0);

	th->th_restarts++;

	save_txg = zseg->seg_mintxg;

	rw_enter(rw, RW_READER);
	th->th_locked = 1;

	rc = traverse_segment(th, zseg, mosbp);
	ASSERT(rc == ERANGE || rc == EAGAIN || rc == EINTR);

	if (th->th_locked)
		rw_exit(rw);
	th->th_locked = 0;

	zseg->seg_mintxg = save_txg;

	if (rc == ERANGE) {
		list_remove(&th->th_seglist, zseg);
		kmem_free(zseg, sizeof (*zseg));
		return (EAGAIN);
	}

	return (rc);
}

/*
 * Note: (mintxg, maxtxg) is an open interval; mintxg and maxtxg themselves
 * are not included.  The blocks covered by this segment will all have
 * mintxg < birth < maxtxg.
 */
static void
traverse_add_segment(traverse_handle_t *th, uint64_t mintxg, uint64_t maxtxg,
    uint64_t sobjset, uint64_t sobject, int slevel, uint64_t sblkid,
    uint64_t eobjset, uint64_t eobject, int elevel, uint64_t eblkid)
{
	zseg_t *zseg;

	zseg = kmem_alloc(sizeof (zseg_t), KM_SLEEP);

	zseg->seg_mintxg = mintxg;
	zseg->seg_maxtxg = maxtxg;

	zseg->seg_start.zb_objset = sobjset;
	zseg->seg_start.zb_object = sobject;
	zseg->seg_start.zb_level = slevel;
	zseg->seg_start.zb_blkid = sblkid;

	zseg->seg_end.zb_objset = eobjset;
	zseg->seg_end.zb_object = eobject;
	zseg->seg_end.zb_level = elevel;
	zseg->seg_end.zb_blkid = eblkid;

	list_insert_tail(&th->th_seglist, zseg);
}

void
traverse_add_dnode(traverse_handle_t *th, uint64_t mintxg, uint64_t maxtxg,
    uint64_t objset, uint64_t object)
{
	if (th->th_advance & ADVANCE_PRE)
		traverse_add_segment(th, mintxg, maxtxg,
		    objset, object, ZB_MAXLEVEL, 0,
		    objset, object, 0, ZB_MAXBLKID);
	else
		traverse_add_segment(th, mintxg, maxtxg,
		    objset, object, 0, 0,
		    objset, object, 0, ZB_MAXBLKID);
}

void
traverse_add_objset(traverse_handle_t *th, uint64_t mintxg, uint64_t maxtxg,
    uint64_t objset)
{
	if (th->th_advance & ADVANCE_PRE)
		traverse_add_segment(th, mintxg, maxtxg,
		    objset, 0, -1, 0,
		    objset, ZB_MAXOBJECT, 0, ZB_MAXBLKID);
	else
		traverse_add_segment(th, mintxg, maxtxg,
		    objset, 1, 0, 0,
		    objset, 0, -1, 0);
}

void
traverse_add_pool(traverse_handle_t *th, uint64_t mintxg, uint64_t maxtxg)
{
	if (th->th_advance & ADVANCE_PRE)
		traverse_add_segment(th, mintxg, maxtxg,
		    0, 0, -1, 0,
		    ZB_MAXOBJSET, ZB_MAXOBJECT, 0, ZB_MAXBLKID);
	else
		traverse_add_segment(th, mintxg, maxtxg,
		    1, 1, 0, 0,
		    0, 0, -1, 0);
}

traverse_handle_t *
traverse_init(spa_t *spa, blkptr_cb_t func, void *arg, int advance,
    int zio_flags)
{
	traverse_handle_t *th;
	int d, l;

	th = kmem_zalloc(sizeof (*th), KM_SLEEP);

	th->th_spa = spa;
	th->th_func = func;
	th->th_arg = arg;
	th->th_advance = advance;
	th->th_lastcb.zb_level = ZB_NO_LEVEL;
	th->th_noread.zb_level = ZB_NO_LEVEL;
	th->th_zio_flags = zio_flags;

	list_create(&th->th_seglist, sizeof (zseg_t),
	    offsetof(zseg_t, seg_node));

	for (d = 0; d < ZB_DEPTH; d++) {
		for (l = 0; l < ZB_MAXLEVEL; l++) {
			if ((advance & ADVANCE_DATA) ||
			    l != 0 || d != ZB_DN_CACHE)
				th->th_cache[d][l].bc_data =
				    zio_buf_alloc(SPA_MAXBLOCKSIZE);
		}
	}

	return (th);
}

void
traverse_fini(traverse_handle_t *th)
{
	int d, l;
	zseg_t *zseg;

	for (d = 0; d < ZB_DEPTH; d++)
		for (l = 0; l < ZB_MAXLEVEL; l++)
			if (th->th_cache[d][l].bc_data != NULL)
				zio_buf_free(th->th_cache[d][l].bc_data,
				    SPA_MAXBLOCKSIZE);

	while ((zseg = list_head(&th->th_seglist)) != NULL) {
		list_remove(&th->th_seglist, zseg);
		kmem_free(zseg, sizeof (*zseg));
	}

	list_destroy(&th->th_seglist);

	dprintf("%llu hit, %llu ARC, %llu IO, %llu cb, %llu sync, %llu again\n",
	    th->th_hits, th->th_arc_hits, th->th_reads, th->th_callbacks,
	    th->th_syncs, th->th_restarts);

	kmem_free(th, sizeof (*th));
}
