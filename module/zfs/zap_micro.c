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

/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2011, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2014 Spectra Logic Corporation, All rights reserved.
 * Copyright 2017 Nexenta Systems, Inc.
 * Copyright (c) 2024, Klara, Inc.
 */

#include <sys/zio.h>
#include <sys/spa.h>
#include <sys/dmu.h>
#include <sys/zfs_context.h>
#include <sys/zap.h>
#include <sys/zap_impl.h>
#include <sys/zap_leaf.h>
#include <sys/btree.h>
#include <sys/arc.h>
#include <sys/dmu_objset.h>
#include <sys/spa_impl.h>

#ifdef _KERNEL
#include <sys/sunddi.h>
#endif

/*
 * The maximum size (in bytes) of a microzap before it is converted to a
 * fatzap. It will be rounded up to next multiple of 512 (SPA_MINBLOCKSIZE).
 *
 * By definition, a microzap must fit into a single block, so this has
 * traditionally been SPA_OLD_MAXBLOCKSIZE, and is set to that by default.
 * Setting this higher requires both the large_blocks feature (to even create
 * blocks that large) and the large_microzap feature (to enable the stream
 * machinery to understand not to try to split a microzap block).
 *
 * If large_microzap is enabled, this value will be clamped to
 * spa_maxblocksize(), up to 1M. If not, it will be clamped to
 * SPA_OLD_MAXBLOCKSIZE.
 */
static int zap_micro_max_size = SPA_OLD_MAXBLOCKSIZE;

/*
 * The 1M upper limit is necessary because the count of chunks in a microzap
 * block is stored as a uint16_t (mze_chunkid). Each chunk is 64 bytes, and the
 * first is used to store a header, so there are 32767 usable chunks, which is
 * just under 2M. 1M is the largest power-2-rounded block size under 2M, so we
 * must set the limit there.
 */
#define	MZAP_MAX_SIZE	(1048576)

uint64_t
zap_get_micro_max_size(spa_t *spa)
{
	uint64_t maxsz = MIN(MZAP_MAX_SIZE,
	    P2ROUNDUP(zap_micro_max_size, SPA_MINBLOCKSIZE));
	if (maxsz <= SPA_OLD_MAXBLOCKSIZE)
		return (maxsz);
	if (spa_feature_is_enabled(spa, SPA_FEATURE_LARGE_MICROZAP))
		return (MIN(maxsz, spa_maxblocksize(spa)));
	return (SPA_OLD_MAXBLOCKSIZE);
}

void
mzap_byteswap(mzap_phys_t *buf, size_t size)
{
	buf->mz_block_type = BSWAP_64(buf->mz_block_type);
	buf->mz_salt = BSWAP_64(buf->mz_salt);
	buf->mz_normflags = BSWAP_64(buf->mz_normflags);
	int max = (size / MZAP_ENT_LEN) - 1;
	for (int i = 0; i < max; i++) {
		buf->mz_chunk[i].mze_value =
		    BSWAP_64(buf->mz_chunk[i].mze_value);
		buf->mz_chunk[i].mze_cd =
		    BSWAP_32(buf->mz_chunk[i].mze_cd);
	}
}

__attribute__((always_inline)) inline
static int
mze_compare(const void *arg1, const void *arg2)
{
	const mzap_ent_t *mze1 = arg1;
	const mzap_ent_t *mze2 = arg2;

	return (TREE_CMP((uint64_t)(mze1->mze_hash) << 32 | mze1->mze_cd,
	    (uint64_t)(mze2->mze_hash) << 32 | mze2->mze_cd));
}

ZFS_BTREE_FIND_IN_BUF_FUNC(mze_find_in_buf, mzap_ent_t,
    mze_compare)

static void
mze_insert(zap_t *zap, uint16_t chunkid, uint64_t hash)
{
	mzap_ent_t mze;

	ASSERT(zap->zap_ismicro);
	ASSERT(RW_WRITE_HELD(&zap->zap_rwlock));

	mze.mze_chunkid = chunkid;
	ASSERT0(hash & 0xffffffff);
	mze.mze_hash = hash >> 32;
	ASSERT3U(MZE_PHYS(zap, &mze)->mze_cd, <=, 0xffff);
	mze.mze_cd = (uint16_t)MZE_PHYS(zap, &mze)->mze_cd;
	ASSERT(MZE_PHYS(zap, &mze)->mze_name[0] != 0);
	zfs_btree_add(&zap->zap_m.zap_tree, &mze);
}

mzap_ent_t *
mze_find(zap_name_t *zn, zfs_btree_index_t *idx)
{
	mzap_ent_t mze_tofind;
	mzap_ent_t *mze;
	zfs_btree_t *tree = &zn->zn_zap->zap_m.zap_tree;

	ASSERT(zn->zn_zap->zap_ismicro);
	ASSERT(RW_LOCK_HELD(&zn->zn_zap->zap_rwlock));

	ASSERT0(zn->zn_hash & 0xffffffff);
	mze_tofind.mze_hash = zn->zn_hash >> 32;
	mze_tofind.mze_cd = 0;

	mze = zfs_btree_find(tree, &mze_tofind, idx);
	if (mze == NULL)
		mze = zfs_btree_next(tree, idx, idx);
	for (; mze && mze->mze_hash == mze_tofind.mze_hash;
	    mze = zfs_btree_next(tree, idx, idx)) {
		ASSERT3U(mze->mze_cd, ==, MZE_PHYS(zn->zn_zap, mze)->mze_cd);
		if (zap_match(zn, MZE_PHYS(zn->zn_zap, mze)->mze_name))
			return (mze);
	}

	return (NULL);
}

static uint32_t
mze_find_unused_cd(zap_t *zap, uint64_t hash)
{
	mzap_ent_t mze_tofind;
	zfs_btree_index_t idx;
	zfs_btree_t *tree = &zap->zap_m.zap_tree;

	ASSERT(zap->zap_ismicro);
	ASSERT(RW_LOCK_HELD(&zap->zap_rwlock));

	ASSERT0(hash & 0xffffffff);
	hash >>= 32;
	mze_tofind.mze_hash = hash;
	mze_tofind.mze_cd = 0;

	uint32_t cd = 0;
	for (mzap_ent_t *mze = zfs_btree_find(tree, &mze_tofind, &idx);
	    mze && mze->mze_hash == hash;
	    mze = zfs_btree_next(tree, &idx, &idx)) {
		if (mze->mze_cd != cd)
			break;
		cd++;
	}

	return (cd);
}

/*
 * Each mzap entry requires at max : 4 chunks
 * 3 chunks for names + 1 chunk for value.
 */
#define	MZAP_ENT_CHUNKS	(1 + ZAP_LEAF_ARRAY_NCHUNKS(MZAP_NAME_LEN) + \
	ZAP_LEAF_ARRAY_NCHUNKS(sizeof (uint64_t)))

/*
 * Check if the current entry keeps the colliding entries under the fatzap leaf
 * size.
 */
boolean_t
mze_canfit_fzap_leaf(zap_name_t *zn, uint64_t hash)
{
	zap_t *zap = zn->zn_zap;
	mzap_ent_t mze_tofind;
	zfs_btree_index_t idx;
	zfs_btree_t *tree = &zap->zap_m.zap_tree;
	uint32_t mzap_ents = 0;

	ASSERT0(hash & 0xffffffff);
	hash >>= 32;
	mze_tofind.mze_hash = hash;
	mze_tofind.mze_cd = 0;

	for (mzap_ent_t *mze = zfs_btree_find(tree, &mze_tofind, &idx);
	    mze && mze->mze_hash == hash;
	    mze = zfs_btree_next(tree, &idx, &idx)) {
		mzap_ents++;
	}

	/* Include the new entry being added */
	mzap_ents++;

	return (ZAP_LEAF_NUMCHUNKS_DEF > (mzap_ents * MZAP_ENT_CHUNKS));
}

void
mze_destroy(zap_t *zap)
{
	zfs_btree_clear(&zap->zap_m.zap_tree);
	zfs_btree_destroy(&zap->zap_m.zap_tree);
}

zap_t *
mzap_open(dmu_buf_t *db)
{
	zap_t *winner;
	uint64_t *zap_hdr = (uint64_t *)db->db_data;
	uint64_t zap_block_type = zap_hdr[0];
	uint64_t zap_magic = zap_hdr[1];

	ASSERT3U(MZAP_ENT_LEN, ==, sizeof (mzap_ent_phys_t));

	zap_t *zap = kmem_zalloc(sizeof (zap_t), KM_SLEEP);
	rw_init(&zap->zap_rwlock, NULL, RW_DEFAULT, NULL);
	rw_enter(&zap->zap_rwlock, RW_WRITER);
	zap->zap_objset = dmu_buf_get_objset(db);
	zap->zap_object = db->db_object;
	zap->zap_dbuf = db;

	if (zap_block_type != ZBT_MICRO) {
		mutex_init(&zap->zap_f.zap_num_entries_mtx, 0, MUTEX_DEFAULT,
		    0);
		zap->zap_f.zap_block_shift = highbit64(db->db_size) - 1;
		if (zap_block_type != ZBT_HEADER || zap_magic != ZAP_MAGIC) {
			winner = NULL;	/* No actual winner here... */
			goto handle_winner;
		}
	} else {
		zap->zap_ismicro = TRUE;
	}

	/*
	 * Make sure that zap_ismicro is set before we let others see it,
	 * because zap_lock() checks zap_ismicro without the lock held.
	 */
	dmu_buf_init_user(&zap->zap_dbu, zap_evict_sync, NULL, &zap->zap_dbuf);
	winner = dmu_buf_set_user(db, &zap->zap_dbu);

	if (winner != NULL)
		goto handle_winner;

	if (zap->zap_ismicro) {
		zap->zap_salt = zap_m_phys(zap)->mz_salt;
		zap->zap_normflags = zap_m_phys(zap)->mz_normflags;
		zap->zap_m.zap_num_chunks = db->db_size / MZAP_ENT_LEN - 1;

		/*
		 * Reduce B-tree leaf from 4KB to 512 bytes to reduce memmove()
		 * overhead on massive inserts below.  It still allows to store
		 * 62 entries before we have to add 2KB B-tree core node.
		 */
		zfs_btree_create_custom(&zap->zap_m.zap_tree, mze_compare,
		    mze_find_in_buf, sizeof (mzap_ent_t), 512);

		zap_name_t *zn = zap_name_alloc(zap, B_FALSE);
		for (uint16_t i = 0; i < zap->zap_m.zap_num_chunks; i++) {
			mzap_ent_phys_t *mze =
			    &zap_m_phys(zap)->mz_chunk[i];
			if (mze->mze_name[0]) {
				zap->zap_m.zap_num_entries++;
				zap_name_init_str(zn, mze->mze_name, 0);
				mze_insert(zap, i, zn->zn_hash);
			}
		}
		zap_name_free(zn);
	} else {
		zap->zap_salt = zap_f_phys(zap)->zap_salt;
		zap->zap_normflags = zap_f_phys(zap)->zap_normflags;

		ASSERT3U(sizeof (struct zap_leaf_header), ==,
		    2*ZAP_LEAF_CHUNKSIZE);

		/*
		 * The embedded pointer table should not overlap the
		 * other members.
		 */
		ASSERT3P(&ZAP_EMBEDDED_PTRTBL_ENT(zap, 0), >,
		    &zap_f_phys(zap)->zap_salt);

		/*
		 * The embedded pointer table should end at the end of
		 * the block
		 */
		ASSERT3U((uintptr_t)&ZAP_EMBEDDED_PTRTBL_ENT(zap,
		    1<<ZAP_EMBEDDED_PTRTBL_SHIFT(zap)) -
		    (uintptr_t)zap_f_phys(zap), ==,
		    zap->zap_dbuf->db_size);
	}
	rw_exit(&zap->zap_rwlock);
	return (zap);

handle_winner:
	rw_exit(&zap->zap_rwlock);
	rw_destroy(&zap->zap_rwlock);
	if (!zap->zap_ismicro)
		mutex_destroy(&zap->zap_f.zap_num_entries_mtx);
	kmem_free(zap, sizeof (zap_t));
	return (winner);
}

int
mzap_upgrade(zap_t **zapp, const void *tag, dmu_tx_t *tx, zap_flags_t flags)
{
	int err = 0;
	zap_t *zap = *zapp;

	ASSERT(RW_WRITE_HELD(&zap->zap_rwlock));

	int sz = zap->zap_dbuf->db_size;
	mzap_phys_t *mzp = vmem_alloc(sz, KM_SLEEP);
	memcpy(mzp, zap->zap_dbuf->db_data, sz);
	int nchunks = zap->zap_m.zap_num_chunks;

	if (!flags) {
		err = dmu_object_set_blocksize(zap->zap_objset, zap->zap_object,
		    1ULL << fzap_default_block_shift, 0, tx);
		if (err != 0) {
			vmem_free(mzp, sz);
			return (err);
		}
	}

	dprintf("upgrading obj=%llu with %u chunks\n",
	    (u_longlong_t)zap->zap_object, nchunks);
	/* XXX destroy the tree later, so we can use the stored hash value */
	mze_destroy(zap);

	fzap_upgrade(zap, tx, flags);

	zap_name_t *zn = zap_name_alloc(zap, B_FALSE);
	for (int i = 0; i < nchunks; i++) {
		mzap_ent_phys_t *mze = &mzp->mz_chunk[i];
		if (mze->mze_name[0] == 0)
			continue;
		dprintf("adding %s=%llu\n",
		    mze->mze_name, (u_longlong_t)mze->mze_value);
		zap_name_init_str(zn, mze->mze_name, 0);
		/* If we fail here, we would end up losing entries */
		VERIFY0(fzap_add_cd(zn, 8, 1, &mze->mze_value, mze->mze_cd,
		    tag, tx));
	}
	zap_name_free(zn);
	vmem_free(mzp, sz);
	*zapp = zap;
	return (0);
}

/*
 * The "normflags" determine the behavior of the matchtype_t which is
 * passed to zap_lookup_norm().  Names which have the same normalized
 * version will be stored with the same hash value, and therefore we can
 * perform normalization-insensitive lookups.  We can be Unicode form-
 * insensitive and/or case-insensitive.  The following flags are valid for
 * "normflags":
 *
 * U8_TEXTPREP_NFC
 * U8_TEXTPREP_NFD
 * U8_TEXTPREP_NFKC
 * U8_TEXTPREP_NFKD
 * U8_TEXTPREP_TOUPPER
 *
 * The *_NF* (Normalization Form) flags are mutually exclusive; at most one
 * of them may be supplied.
 */
void
mzap_create_impl(dnode_t *dn, int normflags, zap_flags_t flags, dmu_tx_t *tx)
{
	dmu_buf_t *db;

	VERIFY0(dmu_buf_hold_by_dnode(dn, 0, FTAG, &db, DMU_READ_NO_PREFETCH));

	dmu_buf_will_dirty(db, tx);
	mzap_phys_t *zp = db->db_data;
	zp->mz_block_type = ZBT_MICRO;
	zp->mz_salt =
	    ((uintptr_t)db ^ (uintptr_t)tx ^ (dn->dn_object << 1)) | 1ULL;
	zp->mz_normflags = normflags;

	if (flags != 0) {
		zap_t *zap;
		/* Only fat zap supports flags; upgrade immediately. */
		VERIFY0(zap_lock_by_dnode(dn, tx,
		    RW_WRITER, B_FALSE, B_FALSE, FTAG, &zap));
		VERIFY0(mzap_upgrade(&zap, FTAG, tx, flags));
		zap_unlock(zap, FTAG);
	}

	dmu_buf_rele(db, FTAG);
}

/*
 * zn may be NULL; if not specified, it will be computed if needed.
 * See also the comment above zap_entry_normalization_conflict().
 */
boolean_t
mzap_normalization_conflict(zap_t *zap, zap_name_t *zn, mzap_ent_t *mze,
    zfs_btree_index_t *idx)
{
	boolean_t allocdzn = B_FALSE;
	mzap_ent_t *other;
	zfs_btree_index_t oidx;

	if (zap->zap_normflags == 0)
		return (B_FALSE);

	for (other = zfs_btree_prev(&zap->zap_m.zap_tree, idx, &oidx);
	    other && other->mze_hash == mze->mze_hash;
	    other = zfs_btree_prev(&zap->zap_m.zap_tree, &oidx, &oidx)) {

		if (zn == NULL) {
			zn = zap_name_alloc_str(zap,
			    MZE_PHYS(zap, mze)->mze_name, MT_NORMALIZE);
			allocdzn = B_TRUE;
		}
		if (zap_match(zn, MZE_PHYS(zap, other)->mze_name)) {
			if (allocdzn)
				zap_name_free(zn);
			return (B_TRUE);
		}
	}

	for (other = zfs_btree_next(&zap->zap_m.zap_tree, idx, &oidx);
	    other && other->mze_hash == mze->mze_hash;
	    other = zfs_btree_next(&zap->zap_m.zap_tree, &oidx, &oidx)) {

		if (zn == NULL) {
			zn = zap_name_alloc_str(zap,
			    MZE_PHYS(zap, mze)->mze_name, MT_NORMALIZE);
			allocdzn = B_TRUE;
		}
		if (zap_match(zn, MZE_PHYS(zap, other)->mze_name)) {
			if (allocdzn)
				zap_name_free(zn);
			return (B_TRUE);
		}
	}

	if (allocdzn)
		zap_name_free(zn);
	return (B_FALSE);
}

void
mzap_addent(zap_name_t *zn, uint64_t value)
{
	zap_t *zap = zn->zn_zap;
	uint16_t start = zap->zap_m.zap_alloc_next;

	ASSERT(RW_WRITE_HELD(&zap->zap_rwlock));

#ifdef ZFS_DEBUG
	for (int i = 0; i < zap->zap_m.zap_num_chunks; i++) {
		mzap_ent_phys_t *mze = &zap_m_phys(zap)->mz_chunk[i];
		ASSERT(strcmp(zn->zn_key_orig, mze->mze_name) != 0);
	}
#endif

	uint32_t cd = mze_find_unused_cd(zap, zn->zn_hash);
	/* given the limited size of the microzap, this can't happen */
	ASSERT(cd < zap_maxcd(zap));

again:
	for (uint16_t i = start; i < zap->zap_m.zap_num_chunks; i++) {
		mzap_ent_phys_t *mze = &zap_m_phys(zap)->mz_chunk[i];
		if (mze->mze_name[0] == 0) {
			mze->mze_value = value;
			mze->mze_cd = cd;
			(void) strlcpy(mze->mze_name, zn->zn_key_orig,
			    sizeof (mze->mze_name));
			zap->zap_m.zap_num_entries++;
			zap->zap_m.zap_alloc_next = i+1;
			if (zap->zap_m.zap_alloc_next ==
			    zap->zap_m.zap_num_chunks)
				zap->zap_m.zap_alloc_next = 0;
			mze_insert(zap, i, zn->zn_hash);
			return;
		}
	}
	if (start != 0) {
		start = 0;
		goto again;
	}
	cmn_err(CE_PANIC, "out of entries!");
}

ZFS_MODULE_PARAM(zfs, , zap_micro_max_size, INT, ZMOD_RW,
	"Maximum micro ZAP size before converting to a fat ZAP, "
	    "in bytes (max 1M)");
