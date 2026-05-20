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
 * Copyright (c) 2026, Hewlett Packard Enterprise Development LP.
 */

#include <sys/zio.h>
#include <sys/spa.h>
#include <sys/dmu.h>
#include <sys/zfs_context.h>
#include <sys/zap.h>
#include <sys/zap_impl.h>
#include <sys/zap_leaf.h>
#include <sys/btree.h>
#include <sys/zfeature.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_synctask.h>
#include <sys/dmu_tx.h>

void
tzap_feature_incr_sync(void *arg, dmu_tx_t *tx)
{
	tzap_feature_arg_t *tfa = arg;
	spa_feature_incr(tfa->tfa_spa, SPA_FEATURE_TINYZAP, tx);
	kmem_free(tfa, sizeof (*tfa));
}

void
tzap_feature_decr_sync(void *arg, dmu_tx_t *tx)
{
	tzap_feature_arg_t *tfa = arg;
	if (spa_feature_is_active(tfa->tfa_spa, SPA_FEATURE_TINYZAP))
		spa_feature_decr(tfa->tfa_spa, SPA_FEATURE_TINYZAP, tx);
	kmem_free(tfa, sizeof (*tfa));
}

/*
 * dmu_tx post-commit callback: incr|decr TinyZAP feature refcount.
 * Fires in syncing context after the upgrading tx commits.  Safe to
 * call spa_feature_{incr|decr} here without holding any tx.
 */

void
tzap_feature_incr_cb(void *arg, int error)
{
	tzap_feature_arg_t *tfa = arg;

	if (error == 0)
		/* tfa freed by tzap_feature_incr_sync */
		VERIFY0(dsl_sync_task(spa_name(tfa->tfa_spa), NULL,
		    tzap_feature_incr_sync, tfa, 0, ZFS_SPACE_CHECK_NONE));
	else
		/* tx aborted, upgrade did not happen, just free */
		kmem_free(tfa, sizeof (*tfa));
}

void
tzap_feature_decr_cb(void *arg, int error)
{
	tzap_feature_arg_t *tfa = arg;

	if (error == 0)
		/* tfa freed by tzap_feature_decr_sync */
		VERIFY0(dsl_sync_task(spa_name(tfa->tfa_spa), NULL,
		    tzap_feature_decr_sync, tfa, 0, ZFS_SPACE_CHECK_NONE));
	else
		/* tx aborted, upgrade did not happen, just free */
		kmem_free(tfa, sizeof (*tfa));
}

const uint16_t tzap_chunk_table[TZAP_CHUNK_SIZES] = { 64, 128, 256 };
/*
 * tzap_chunk_for_stride: select the smallest chunk (64/128/256) that
 * can accommodate (stride + cd(4) + keylen + NUL).
 *
 * Returns chunk_id (0=64B, 1=128B, or 2=256B) on success.
 * Returns -1 if no chunk fits, caller must fall back to FatZAP.
 */
static int
tzap_chunk_for_stride(uint16_t stride, size_t keylen)
{
	/* For stride=8 (1×uint64, long-name path), skip chunk=64 (cid=0) */
	int start_cid = (stride == TZAP_MIN_STRIDE) ? 1 : 0;

	for (int cid = start_cid; cid < TZAP_CHUNK_SIZES; cid++) {
		uint16_t chunk = tzap_chunk_table[cid];
		/*
		 * Slot must hold: value (stride) + cd (4) + at least
		 * TZAP_MIN_NAME_LEN bytes of name.
		 */
		if ((uint32_t)stride + sizeof (uint32_t) +
		    TZAP_MIN_NAME_LEN > chunk)
			continue;

		if (keylen < (size_t)TZAP_NAME_LEN(chunk, stride))
			return (cid);
	}
	return (-1);
}

/*
 * tzap_try_promote: attempt to promote this MicroZAP to TinyZAP.
 *
 * Called on the first zap_add() when ZAP_FLAG_TINYZAP was set at
 * create time, or when a plain MicroZAP entry doesn't fit.
 *
 * Selects the smallest fitting chunk via tzap_chunk_for_stride(), then:
 * writes three independent uint8_t fields on-disk:
 *    mz_flags       = MZAP_FLAG_TINY  (bit 0)
 *    mz_chunk_shift = log2(chunk)     (6, 7, or 8)
 *    mz_value_ints  = stride / 8
 * sets zap_stride, zap_chunk_size, zap_num_chunks in-memory.
 *
 * Returns B_TRUE  : if the ZAP was promoted (or was already promoted).
 * Returns B_FALSE : if the geometry doesn't qualify, caller falls back
 *                   to plain MicroZAP / FatZAP promotion.
 */
boolean_t
tzap_try_promote(zap_t *zap, int integer_size, uint64_t num_integers,
    const char *key, dmu_tx_t *tx)
{
	ASSERT(zap->zap_ismicro);
	ASSERT(RW_WRITE_HELD(&zap->zap_rwlock));

	/* Already promoted on a previous add */
	if (zap->zap_m.zap_stride != 0)
		return (B_TRUE);

	/* Require the pool feature to be enabled */
	spa_t *spa = dmu_objset_spa(zap->zap_objset);
	if (!spa_feature_is_enabled(spa, SPA_FEATURE_TINYZAP)) {
		dprintf("tzap_try_promote: SPA_FEATURE_TINYZAP is not enabled "
		    "obj=%llu, falling back to FatZAP\n",
		    (u_longlong_t)zap->zap_object);
		return (B_FALSE);
	}

	if (integer_size != 8)
		return (B_FALSE);

	uint16_t stride = (uint16_t)(num_integers * sizeof (uint64_t));

	if (stride < TZAP_MIN_STRIDE)
		return (B_FALSE);

	int cid = tzap_chunk_for_stride(stride, strlen(key));
	if (cid < 0 || cid >= TZAP_CHUNK_SIZES) {
		zap_m_phys(zap)->mz_flags = 0;
		dprintf("tzap_try_promote: disqualified obj=%llu "
		    "integer_size=%d num_integers=%llu key=%s\n",
		    (u_longlong_t)zap->zap_object, integer_size,
		    (u_longlong_t)num_integers, key);
		return (B_FALSE);
	}

	if (zap->zap_m.zap_num_entries > 0 && stride != 8)
		return (B_FALSE);

	ASSERT(cid >= 0 && cid < TZAP_CHUNK_SIZES);

	/*
	 * chunk_log2: 6=64B, 7=128B, 8=256B.
	 * tzap_chunk_table is {2^6, 2^7, 2^8}, so log2 = MIN_LOG2 + cid.
	 */
	uint16_t chunk = tzap_chunk_table[cid];
	uint8_t chunk_log2 = (uint8_t)(TZAP_MIN_CHUNK_LOG2 + cid);
	ASSERT3U(chunk_log2, >=, TZAP_MIN_CHUNK_LOG2);
	ASSERT3U(chunk_log2, <=, TZAP_MAX_CHUNK_LOG2);
	ASSERT3U(1U << chunk_log2, ==, chunk);

	if (zap->zap_m.zap_num_entries > 0) {
		/*
		 * Block may be too small to re-encode all existing MicroZAP
		 * entries at the new chunk pitch.  Grow it first if needed,
		 * otherwise return B_FALSE so caller upgrades to FatZAP.
		 */
		size_t need = (size_t)(zap->zap_m.zap_num_entries + 1) *
		    chunk + MZAP_ENT_LEN;
		if (need > (size_t)zap->zap_dbuf->db_size) {
			uint64_t newsz = P2ROUNDUP(need, SPA_MINBLOCKSIZE);
			uint64_t maxsz = zap_get_micro_max_size(
			    dmu_objset_spa(zap->zap_objset));
			if (newsz > maxsz)
				return (B_FALSE);
			VERIFY0(dmu_object_set_blocksize(zap->zap_objset,
			    zap->zap_object, newsz, 0, tx));
		}
		tzap_reencode_micro_to_tiny(zap, chunk, tx);
	}

	/* Stamp on-disk geometry into three independent uint8_t fields */
	zap_m_phys(zap)->mz_flags = MZAP_FLAG_TINY;
	zap_m_phys(zap)->mz_chunk_shift = chunk_log2;
	zap_m_phys(zap)->mz_value_ints = (uint8_t)(stride / sizeof (uint64_t));

	/*
	 * spa_feature_incr() requires syncing context.
	 */
	if (dmu_tx_is_syncing(tx)) {
		spa_feature_incr(spa, SPA_FEATURE_TINYZAP, tx);
	} else {
		tzap_feature_arg_t *tfa = kmem_alloc(sizeof (*tfa), KM_SLEEP);
		tfa->tfa_spa = spa;
		/* tfa freed by callback */
		dmu_tx_callback_register(tx, tzap_feature_incr_cb, tfa);
	}

	/* Only set in-memory state here if NOT re-encoded above */
	if (zap->zap_m.zap_num_entries == 0) {
		zap->zap_m.zap_stride = stride;
		zap->zap_m.zap_chunk_size = chunk;
		zap->zap_m.zap_num_chunks =
		    (int16_t)((zap->zap_dbuf->db_size - MZAP_ENT_LEN) / chunk);
	}
	dprintf("tzap_try_promote: Promoted obj=%llu stride=%u chunk=%uB "
	    "(log2=%u) nchunks=%d name_avail=%u key=%s\n",
	    (u_longlong_t)zap->zap_object, stride, chunk, chunk_log2,
	    zap->zap_m.zap_num_chunks, TZAP_NAME_LEN(chunk, stride) - 1, key);
	return (B_TRUE);
}

/*
 * tzap_addent: insert an entry into a TinyZAP.
 *
 * zap_stride and zap_chunk_size must already be stamped by
 * tzap_try_promote().
 *
 * Slot layout: [value(stride bytes) | cd(4B) | name(NUL-terminated)]
 */
void
tzap_addent(zap_name_t *zn, const void *val)
{
	zap_t *zap = zn->zn_zap;
	uint16_t stride = zap->zap_m.zap_stride;
	uint16_t chunk = zap->zap_m.zap_chunk_size;

	ASSERT(zap->zap_ismicro);
	ASSERT(stride != 0);
	ASSERT(chunk != 0);
	ASSERT(RW_WRITE_HELD(&zap->zap_rwlock));
	ASSERT3U(stride, >=, TZAP_MIN_STRIDE);
	ASSERT3U(stride, <=, tzap_max_stride(chunk));
	ASSERT3U(strlen(zn->zn_key_orig), <, TZAP_NAME_LEN(chunk, stride));
	ASSERT3U(zap->zap_m.zap_num_entries, <, zap->zap_m.zap_num_chunks);

#ifdef ZFS_DEBUG
	for (int i = 0; i < zap->zap_m.zap_num_chunks; i++) {
		tzap_ent_phys_t *tze = (tzap_ent_phys_t *)
		    ((uint8_t *)zap_m_phys(zap)->mz_chunk +
		    ((size_t)i * chunk));
		if (tze_name_ptr(tze, stride)[0] == '\0')
			continue;
		ASSERT(strcmp(tze_name_ptr(tze, stride),
		    zn->zn_key_orig) != 0);
	}
#endif

	uint16_t start = zap->zap_m.zap_alloc_next;
	uint32_t cd = mze_find_unused_cd(zap, zn->zn_hash);
	/* TinyZAP capacity is small; this must not happen */
	ASSERT(cd < zap_maxcd(zap));

again:
	for (uint16_t i = start; i < zap->zap_m.zap_num_chunks; i++) {
		tzap_ent_phys_t *tze = (tzap_ent_phys_t *)((uint8_t *)
		    zap_m_phys(zap)->mz_chunk + (size_t)i * chunk);

		if (tze_name_ptr(tze, stride)[0] != '\0')
			continue;

		memcpy(tze_value(tze), val, stride);
		*tze_cd_ptr(tze, stride) = cd;
		(void) strlcpy(tze_name_ptr(tze, stride),
		    zn->zn_key_orig, TZAP_NAME_LEN(chunk, stride));
		zap->zap_m.zap_num_entries++;
		zap->zap_m.zap_alloc_next = i + 1;
		if (zap->zap_m.zap_alloc_next == zap->zap_m.zap_num_chunks)
			zap->zap_m.zap_alloc_next = 0;
		mze_insert(zap, i, zn->zn_hash);
		return;
	}
	if (start != 0) {
		start = 0;
		goto again;
	}
	cmn_err(CE_PANIC,
	    "tzap_addent: out of TinyZAP entries obj=%llu\n",
	    (u_longlong_t)zap->zap_object);
}

/*
 * tzap_lookup: read value and optional realname from a TinyZAP entry.
 *
 * Returns 0        on success.
 * Returns EINVAL   if integer_size != 8.
 * Returns EOVERFLOW if num_integers < stride/8.
 */
int
tzap_lookup(zap_t *zap, mzap_ent_t *mze, uint64_t integer_size,
    uint64_t num_integers, void *buf, char *realname, int rn_len,
    boolean_t *ncp)
{
	uint16_t stride = zap->zap_m.zap_stride;

	ASSERT(zap->zap_ismicro);
	ASSERT(stride != 0);
	ASSERT(mze != NULL);

	if (integer_size != 8)
		return (SET_ERROR(EINVAL));

	uint64_t stored_ints = stride / sizeof (uint64_t);
	if (num_integers < stored_ints)
		return (SET_ERROR(EOVERFLOW));

	tzap_ent_phys_t *tze = TZE_PHYS(zap, mze);
	memcpy(buf, tze_value(tze), stride);

	if (realname != NULL)
		(void) strlcpy(realname, tze_name_ptr(tze, stride), rn_len);

	/*
	 * Normalization conflicts are now checked via
	 * mzap_normalization_conflict.
	 */
	if (ncp != NULL) {
		zfs_btree_index_t idx;
		zap_name_t *zn_tmp = zap_name_alloc_str(zap,
		    tze_name_ptr(tze, stride), MT_NORMALIZE);
		if (zn_tmp != NULL) {
			mzap_ent_t *m = mze_find(zn_tmp, &idx);
			*ncp = (m != NULL) ?
			    mzap_normalization_conflict(zap, zn_tmp, m, &idx) :
			    B_FALSE;
			zap_name_free(zn_tmp);
		} else {
			*ncp = B_FALSE;
		}
	}

	return (0);
}

/*
 * tzap_reencode_micro_to_tiny: re-encode all plain MicroZAP entries
 * (stride=8, fixed 64-byte slots) into TinyZAP format (stride=8,
 * chunk=128 or 256).
 *
 * Called from tzap_try_promote() when zap_num_entries > 0 and
 * stride == 8 (long-name trigger).  The caller has already selected
 * chunk and verified all existing names fit TZAP_NAME_LEN(chunk, 8).
 *
 * Uses a scratch copy to avoid aliasing: source slots are 64 bytes,
 * destination slots are 128 or 256 bytes, so they overlap if done in-place.
 *
 * Caller must hold RW_WRITER and have called dmu_buf_will_dirty().
 */
void
tzap_reencode_micro_to_tiny(zap_t *zap, uint16_t chunk, dmu_tx_t *tx)
{
	(void) tx;
	ASSERT(RW_WRITE_HELD(&zap->zap_rwlock));
	ASSERT3U(chunk, >, MZAP_ENT_LEN); /* chunk=64 skipped for stride=8 */

	int db_size = zap->zap_dbuf->db_size;
	/*
	 * Use db_size to derive old_nchunks: zap_num_chunks may reflect a
	 * freshly grown block (dmu_object_set_blocksize just called), but
	 * the on-disk content is still in plain MicroZAP format with
	 * MZAP_ENT_LEN-sized slots.
	 */
	int old_nchunks = (db_size - MZAP_ENT_LEN) / MZAP_ENT_LEN;
	uint16_t stride = 8;

	/* copy of the current MicroZAP block */
	mzap_phys_t *src = vmem_alloc(db_size, KM_SLEEP);
	memcpy(src, zap_m_phys(zap), db_size);

	/* zero the destination chunk area (header is preserved in place) */
	memset(zap_m_phys(zap)->mz_chunk, 0, db_size - MZAP_ENT_LEN);

	int new_nchunks = (db_size - MZAP_ENT_LEN) / chunk;
	int dst_slot = 0;

	for (int i = 0; i < old_nchunks; i++) {
		mzap_ent_phys_t *me = &src->mz_chunk[i];
		if (me->mze_name[0] == '\0')
			continue;

		ASSERT3U(dst_slot, <, new_nchunks);

		tzap_ent_phys_t *tze = (tzap_ent_phys_t *)
		    ((uint8_t *)zap_m_phys(zap)->mz_chunk +
		    (size_t)dst_slot * chunk);
		/* value: 1×uint64 */
		memcpy(tze_value(tze), &me->mze_value, stride);
		/* cd */
		*tze_cd_ptr(tze, stride) = me->mze_cd;
		/* name */
		(void) strlcpy(tze_name_ptr(tze, stride),
		    me->mze_name, TZAP_NAME_LEN(chunk, stride));

		dst_slot++;
	}

	vmem_free(src, db_size);

	/*
	 * Rebuild the in-memory B-tree: mze_chunkid values must map to
	 * the new contiguous dst_slot indices, not the original sparse
	 * plain MicroZAP slot indices.
	 * mze_insert() reads from the live on-disk block, which is now
	 * in TinyZAP format, so we must set zap_stride and zap_chunk_size
	 * BEFORE rebuilding the tree.
	 */
	mze_destroy(zap);
	zap->zap_m.zap_stride = stride;
	zap->zap_m.zap_chunk_size = chunk;
	zap->zap_m.zap_num_chunks = (int16_t)new_nchunks;
	/* zap_num_entries is unchanged */

	zap_name_t *zn = zap_name_alloc(zap, B_FALSE);
	for (int i = 0; i < new_nchunks; i++) {
		tzap_ent_phys_t *tze = (tzap_ent_phys_t *)
		    ((uint8_t *)zap_m_phys(zap)->mz_chunk +
		    (size_t)i * chunk);
		const char *name = tze_name_ptr(tze, stride);
		if (name[0] == '\0')
			continue;
		zap_name_init_str(zn, name, 0);
		mze_insert(zap, i, zn->zn_hash);
	}
	zap_name_free(zn);
	zap->zap_m.zap_alloc_next = zap->zap_m.zap_num_entries;
}

/*
 * Attempt to grow chunk size to fit a longer key.
 * Returns B_TRUE if upgrade succeeded and the entry can be added,
 * B_FALSE if no larger chunk fits and caller must go to FatZAP.
 */
boolean_t
tzap_try_chunk_upgrade(zap_t *zap, uint16_t stride, size_t keylen,
    dmu_tx_t *tx)
{
	uint16_t chunk = zap->zap_m.zap_chunk_size;

	int new_cid = tzap_chunk_for_stride(stride, keylen);
	if (new_cid < 0)
		return (B_FALSE);

	uint16_t new_chunk = tzap_chunk_table[new_cid];
	if (new_chunk <= chunk)
		return (B_FALSE);

	size_t need = (size_t)(zap->zap_m.zap_num_entries + 1) *
	    new_chunk + MZAP_ENT_LEN;
	if (need > (size_t)zap->zap_dbuf->db_size) {
		uint64_t newsz = P2ROUNDUP(need, SPA_MINBLOCKSIZE);
		uint64_t maxsz =
		    zap_get_micro_max_size(dmu_objset_spa(zap->zap_objset));
		if (newsz > maxsz)
			return (B_FALSE);
		VERIFY0(dmu_object_set_blocksize(zap->zap_objset,
		    zap->zap_object, newsz, 0, tx));
	}

	return (tzap_upgrade_chunk(zap, new_chunk, tx) == 0);
}

/*
 * Add all TinyZAP entries from old_chunk to
 * new_chunk, keeping stride unchanged.
 *
 * Triggered when an entry's key fits the stamped stride but is too long
 * for the current TZAP_NAME_LEN(chunk, stride).
 *
 * Caller must have grown the block if necessary and called
 * dmu_buf_will_dirty(). Holds RW_WRITER.
 *
 * Returns 0 on success, ENOSPC if entries won't fit at new pitch.
 */
int
tzap_upgrade_chunk(zap_t *zap, uint16_t new_chunk, dmu_tx_t *tx)
{
	(void) tx;
	ASSERT(RW_WRITE_HELD(&zap->zap_rwlock));

	uint16_t stride = zap->zap_m.zap_stride;
	uint16_t old_chunk = zap->zap_m.zap_chunk_size;
	int db_size   = zap->zap_dbuf->db_size;

	ASSERT3U(new_chunk, >, old_chunk);
	ASSERT3U(stride, !=, 0);

	int new_nchunks = (db_size - MZAP_ENT_LEN) / new_chunk;
	if (zap->zap_m.zap_num_entries > new_nchunks)
		return (SET_ERROR(ENOSPC));

	mzap_phys_t *src = vmem_alloc(db_size, KM_SLEEP);
	memcpy(src, zap_m_phys(zap), db_size);
	memset(zap_m_phys(zap)->mz_chunk, 0, db_size - MZAP_ENT_LEN);

	int old_nchunks = zap->zap_m.zap_num_chunks;
	int dst_slot = 0;

	for (int i = 0; i < old_nchunks; i++) {
		tzap_ent_phys_t *s = (tzap_ent_phys_t *)
		    ((uint8_t *)src->mz_chunk + (size_t)i * old_chunk);
		if (tze_name_ptr(s, stride)[0] == '\0')
			continue;
		ASSERT3U(dst_slot, <, new_nchunks);
		tzap_ent_phys_t *d = (tzap_ent_phys_t *)
		    ((uint8_t *)zap_m_phys(zap)->mz_chunk +
		    (size_t)dst_slot * new_chunk);
		memcpy(tze_value(d), tze_value(s), stride);
		*tze_cd_ptr(d, stride) = *tze_cd_ptr(s, stride);
		(void) strlcpy(tze_name_ptr(d, stride),
		    tze_name_ptr(s, stride),
		    TZAP_NAME_LEN(new_chunk, stride));
		dst_slot++;
	}
	vmem_free(src, db_size);
	uint8_t new_log2 = 0;
	for (uint16_t v = new_chunk; v > 1; v >>= 1)
		new_log2++;

	/* Only the chunk size changes; TINY bit and stride stay. */
	zap_m_phys(zap)->mz_chunk_shift = new_log2;

	mze_destroy(zap);
	zap->zap_m.zap_chunk_size = new_chunk;
	zap->zap_m.zap_num_chunks = (int16_t)new_nchunks;
	zap->zap_m.zap_alloc_next = zap->zap_m.zap_num_entries;

	zap_name_t *zn = zap_name_alloc(zap, B_FALSE);
	for (int i = 0; i < new_nchunks; i++) {
		tzap_ent_phys_t *tze = (tzap_ent_phys_t *)
		    ((uint8_t *)zap_m_phys(zap)->mz_chunk +
		    (size_t)i * new_chunk);
		const char *name = tze_name_ptr(tze, stride);
		if (name[0] == '\0')
			continue;
		zap_name_init_str(zn, name, 0);
		mze_insert(zap, i, zn->zn_hash);
	}
	zap_name_free(zn);
	return (0);
}

/*
 * tzap_cursor_fill: populate a zap_attribute_t from a TinyZAP entry.
 *
 * za_integer_length  = 8 (always uint64_t)
 * za_num_integers    = stride / 8
 * za_first_integer   = first uint64 of value blob only; callers
 *                      needing the full blob must call zap_lookup().
 */
void
tzap_cursor_fill(zap_cursor_t *zc, mzap_ent_t *mze, zap_attribute_t *za)
{
	zap_t *zap = zc->zc_zap;
	uint16_t stride = zap->zap_m.zap_stride;

	ASSERT(stride != 0);
	ASSERT(mze != NULL);

	tzap_ent_phys_t *tze = TZE_PHYS(zap, mze);

	za->za_integer_length = sizeof (uint64_t);
	za->za_num_integers = stride / sizeof (uint64_t);
	za->za_first_integer = *(const uint64_t *)tze_value(tze);

	(void) strlcpy(za->za_name, tze_name_ptr(tze, stride),
	    za->za_name_len);
}

/*
 * tzap_upgrade_entries: re-encode TinyZAP entries into a FatZAP.
 * nchunks: total chunk slots in mzp (computed by caller from block size).
 */
int
tzap_upgrade_entries(mzap_phys_t *mzp, size_t db_size,
    zap_name_t *zn, dmu_tx_t *tx)
{
	uint16_t stride = MZAP_STRIDE(mzp);

	/*
	 * Read geometry from independent uint8_t fields.
	 * mz_value_ints == 0 means no entries were ever added.
	 */
	if (stride == 0)
		return (0);

	ASSERT(MZAP_IS_TINYZAP(mzp));

	uint8_t log2 = mzp->mz_chunk_shift;
	ASSERT3U(log2, >=, TZAP_MIN_CHUNK_LOG2);
	ASSERT3U(log2, <=, TZAP_MAX_CHUNK_LOG2);

	/* Derive actual chunk byte-size */
	uint16_t chunk = MZAP_CHUNK_SIZE(mzp);
	ASSERT3U(chunk, >=, TZAP_MIN_CHUNK);
	ASSERT3U(chunk, <=, TZAP_MAX_CHUNK);

	/* Derive nchunks from TinyZAP chunk size */
	int nchunks = (int)((db_size - MZAP_ENT_LEN) / chunk);
	ASSERT3S(nchunks, >, 0);
	for (int i = 0; i < nchunks; i++) {
		tzap_ent_phys_t *tze =
		    (tzap_ent_phys_t *)((uint8_t *)mzp->mz_chunk +
		    (size_t)i * chunk);
		const char *name = tze_name_ptr(tze, stride);

		if (name[0] == '\0')
			continue;

		(void) zap_name_init_str(zn, name, 0);

		int err = fzap_add_cd(zn,
		    sizeof (uint64_t),
		    stride / sizeof (uint64_t),
		    tze_value(tze),
		    *tze_cd_ptr(tze, stride), tx);
		if (err != 0) {
			dprintf("tzap_upgrade_entries: obj=%llu "
			    "key=%s fzap_add_cd failed: error=%d\n",
			    (u_longlong_t)zn->zn_zap->zap_object, name, err);
			return (err);
		}
	}
	return (0);
}

/*
 * tzap_get_stats: fill the TinyZAP-specific fields of a zap_stats_t.
 *
 * Called from zap_get_stats() when the ZAP is a MicroZAP with
 * ZAP_FLAG_TINYZAP set.
 */
void
tzap_get_stats(zap_t *zap, zap_stats_t *zs)
{
	ASSERT(zap->zap_ismicro);

	mzap_phys_t *mzp = zap_m_phys(zap);

	zs->zs_is_tinyzap = MZAP_IS_TINYZAP(mzp);
	zs->zs_tinyzap_stride = MZAP_STRIDE(mzp);
	zs->zs_tinyzap_chunk = (zs->zs_is_tinyzap) ?
	    MZAP_CHUNK_SIZE(mzp) : 0;
	zs->zs_tinyzap_num_chunks = zap->zap_m.zap_num_chunks;
	zs->zs_tinyzap_flags = mzp->mz_flags;
}

/*
 * tzap_verify_phys: validate on-disk mz_flags before any dereference.
 */
boolean_t
tzap_verify_phys(const char *caller, zap_t *zap)
{
	mzap_phys_t *mzp = zap_m_phys(zap);

	if (!MZAP_IS_TINYZAP(mzp)) {
		cmn_err(CE_WARN, "%s: obj=%llu mz_flags=0x%x: "
		    "TINY bit missing", caller,
		    (u_longlong_t)zap->zap_object,
		    (unsigned)mzp->mz_flags);
		return (B_FALSE);
	}

	uint16_t stride = MZAP_STRIDE(mzp);
	uint8_t log2 = mzp->mz_chunk_shift;
	uint16_t chunk = MZAP_CHUNK_SIZE(mzp);

	if (stride != 0 && (stride % sizeof (uint64_t)) != 0) {
		cmn_err(CE_WARN, "%s: obj=%llu bad stride=%u",
		    caller, (u_longlong_t)zap->zap_object, stride);
		return (B_FALSE);
	}

	if (log2 < TZAP_MIN_CHUNK_LOG2 || log2 > TZAP_MAX_CHUNK_LOG2) {
		cmn_err(CE_WARN, "%s: obj=%llu bad chunk_log2=%u",
		    caller, (u_longlong_t)zap->zap_object, log2);
		return (B_FALSE);
	}

	if (chunk != (1U << log2)) {
		cmn_err(CE_WARN, "%s: obj=%llu chunk=%u != 1<<%u=%u",
		    caller, (u_longlong_t)zap->zap_object,
		    chunk, log2, 1U << log2);
		return (B_FALSE);
	}

	if (stride + sizeof (uint32_t) + TZAP_MIN_NAME_LEN > chunk) {
		cmn_err(CE_WARN, "%s: obj=%llu stride=%u won't fit in chunk=%u",
		    caller, (u_longlong_t)zap->zap_object, stride, chunk);
		return (B_FALSE);
	}

	/* Sanity: computed nchunks must be > 0 */
	int nchunks = (int)((zap->zap_dbuf->db_size - MZAP_ENT_LEN) / chunk);
	if (nchunks <= 0) {
		cmn_err(CE_WARN, "%s: obj=%llu nchunks=%d <= 0 "
		    "(dbuf_size=%lu chunk=%u)",
		    caller, (u_longlong_t)zap->zap_object, nchunks,
		    (ulong_t)zap->zap_dbuf->db_size, chunk);
		return (B_FALSE);
	}

	dprintf("%s: obj=%llu TinyZAP layout OK nchunks=%d\n",
	    caller, (u_longlong_t)zap->zap_object, nchunks);
	return (B_TRUE);
}
