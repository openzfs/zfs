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
 * Copyright (c) 2026, TrueNAS.
 */

#include <sys/zfs_context.h>
#include <sys/dmu.h>
#include <sys/dnode.h>
#include <sys/dsl_dataset.h>
#include <sys/zap.h>
#include <sys/zap_impl.h>

static kmem_cache_t *zap_name_cache;
static kmem_cache_t *zap_attr_cache;
static kmem_cache_t *zap_name_long_cache;
static kmem_cache_t *zap_attr_long_cache;

/* Setup/teardown caches. Part of the public interface in zap.h. */
void
zap_init(void)
{
	zap_name_cache = kmem_cache_create("zap_name",
	    sizeof (zap_name_t) + ZAP_MAXNAMELEN, 0, NULL, NULL,
	    NULL, NULL, NULL, 0);

	zap_attr_cache = kmem_cache_create("zap_attr_cache",
	    sizeof (zap_attribute_t) + ZAP_MAXNAMELEN,  0, NULL,
	    NULL, NULL, NULL, NULL, 0);

	zap_name_long_cache = kmem_cache_create("zap_name_long",
	    sizeof (zap_name_t) + ZAP_MAXNAMELEN_NEW, 0, NULL, NULL,
	    NULL, NULL, NULL, 0);

	zap_attr_long_cache = kmem_cache_create("zap_attr_long_cache",
	    sizeof (zap_attribute_t) + ZAP_MAXNAMELEN_NEW,  0, NULL,
	    NULL, NULL, NULL, NULL, 0);
}

void
zap_fini(void)
{
	kmem_cache_destroy(zap_name_cache);
	kmem_cache_destroy(zap_attr_cache);
	kmem_cache_destroy(zap_name_long_cache);
	kmem_cache_destroy(zap_attr_long_cache);
}

static int
zap_normalize(zap_t *zap, const char *name, char *namenorm, int normflags,
    size_t outlen)
{
	ASSERT(!(zap_getflags(zap) & ZAP_FLAG_UINT64_KEY));

	size_t inlen = strlen(name) + 1;

	int err = 0;
	(void) u8_textprep_str((char *)name, &inlen, namenorm, &outlen,
	    normflags | U8_TEXTPREP_IGNORE_NULL | U8_TEXTPREP_IGNORE_INVALID,
	    U8_UNICODE_LATEST, &err);

	return (err);
}

zap_name_t *
zap_name_alloc(zap_t *zap, boolean_t longname)
{
	kmem_cache_t *cache = longname ? zap_name_long_cache : zap_name_cache;
	zap_name_t *zn = kmem_cache_alloc(cache, KM_SLEEP);

	zn->zn_zap = zap;
	zn->zn_normbuf_len = longname ? ZAP_MAXNAMELEN_NEW : ZAP_MAXNAMELEN;
	return (zn);
}

zap_name_t *
zap_name_alloc_str(zap_t *zap, const char *key, matchtype_t mt)
{
	size_t key_len = strlen(key) + 1;
	zap_name_t *zn = zap_name_alloc(zap, (key_len > ZAP_MAXNAMELEN));
	if (zap_name_init_str(zn, key, mt) != 0) {
		zap_name_free(zn);
		return (NULL);
	}
	return (zn);
}

zap_name_t *
zap_name_alloc_uint64(zap_t *zap, const uint64_t *key, int numints)
{
	zap_name_t *zn = kmem_cache_alloc(zap_name_cache, KM_SLEEP);

	ASSERT0(zap->zap_normflags);
	zn->zn_zap = zap;
	zn->zn_key_intlen = sizeof (*key);
	zn->zn_key_orig = zn->zn_key_norm = key;
	zn->zn_key_orig_numints = zn->zn_key_norm_numints = numints;
	zn->zn_matchtype = 0;
	zn->zn_normbuf_len = ZAP_MAXNAMELEN;

	zn->zn_hash = zap_hash(zn);
	return (zn);
}

void
zap_name_free(zap_name_t *zn)
{
	if (zn->zn_normbuf_len == ZAP_MAXNAMELEN) {
		kmem_cache_free(zap_name_cache, zn);
	} else {
		ASSERT3U(zn->zn_normbuf_len, ==, ZAP_MAXNAMELEN_NEW);
		kmem_cache_free(zap_name_long_cache, zn);
	}
}

int
zap_name_init_str(zap_name_t *zn, const char *key, matchtype_t mt)
{
	zap_t *zap = zn->zn_zap;
	size_t key_len = strlen(key) + 1;

	/* Make sure zn is allocated for longname if key is long */
	IMPLY(key_len > ZAP_MAXNAMELEN,
	    zn->zn_normbuf_len == ZAP_MAXNAMELEN_NEW);

	zn->zn_key_intlen = sizeof (*key);
	zn->zn_key_orig = key;
	zn->zn_key_orig_numints = key_len;
	zn->zn_matchtype = mt;
	zn->zn_normflags = zap->zap_normflags;

	/*
	 * If we're dealing with a case sensitive lookup on a mixed or
	 * insensitive fs, remove U8_TEXTPREP_TOUPPER or the lookup
	 * will fold case to all caps overriding the lookup request.
	 */
	if (mt & MT_MATCH_CASE)
		zn->zn_normflags &= ~U8_TEXTPREP_TOUPPER;

	if (zap->zap_normflags) {
		/*
		 * We *must* use zap_normflags because this normalization is
		 * what the hash is computed from.
		 */
		if (zap_normalize(zap, key, zn->zn_normbuf,
		    zap->zap_normflags, zn->zn_normbuf_len) != 0)
			return (SET_ERROR(ENOTSUP));
		zn->zn_key_norm = zn->zn_normbuf;
		zn->zn_key_norm_numints = strlen(zn->zn_key_norm) + 1;
	} else {
		if (mt != 0)
			return (SET_ERROR(ENOTSUP));
		zn->zn_key_norm = zn->zn_key_orig;
		zn->zn_key_norm_numints = zn->zn_key_orig_numints;
	}

	zn->zn_hash = zap_hash(zn);

	if (zap->zap_normflags != zn->zn_normflags) {
		/*
		 * We *must* use zn_normflags because this normalization is
		 * what the matching is based on.  (Not the hash!)
		 */
		if (zap_normalize(zap, key, zn->zn_normbuf,
		    zn->zn_normflags, zn->zn_normbuf_len) != 0)
			return (SET_ERROR(ENOTSUP));
		zn->zn_key_norm_numints = strlen(zn->zn_key_norm) + 1;
	}

	return (0);
}

boolean_t
zap_match(zap_name_t *zn, const char *matchname)
{
	boolean_t res = B_FALSE;
	ASSERT(!(zap_getflags(zn->zn_zap) & ZAP_FLAG_UINT64_KEY));

	if (zn->zn_matchtype & MT_NORMALIZE) {
		size_t namelen = zn->zn_normbuf_len;
		char normbuf[ZAP_MAXNAMELEN];
		char *norm = normbuf;

		/*
		 * Cannot allocate this on-stack as it exceed the stack-limit of
		 * 1024.
		 */
		if (namelen > ZAP_MAXNAMELEN)
			norm = kmem_alloc(namelen, KM_SLEEP);

		if (zap_normalize(zn->zn_zap, matchname, norm,
		    zn->zn_normflags, namelen) != 0) {
			res = B_FALSE;
		} else {
			res = (strcmp(zn->zn_key_norm, norm) == 0);
		}
		if (norm != normbuf)
			kmem_free(norm, namelen);
	} else {
		res = (strcmp(zn->zn_key_orig, matchname) == 0);
	}
	return (res);
}

uint64_t
zap_hash(zap_name_t *zn)
{
	zap_t *zap = zn->zn_zap;
	uint64_t h = 0;

	if (zap_getflags(zap) & ZAP_FLAG_PRE_HASHED_KEY) {
		ASSERT(zap_getflags(zap) & ZAP_FLAG_UINT64_KEY);
		h = *(uint64_t *)zn->zn_key_orig;
	} else {
		h = zap->zap_salt;
		ASSERT(h != 0);
		ASSERT(zfs_crc64_table[128] == ZFS_CRC64_POLY);

		if (zap_getflags(zap) & ZAP_FLAG_UINT64_KEY) {
			const uint64_t *wp = zn->zn_key_norm;

			ASSERT(zn->zn_key_intlen == 8);
			for (int i = 0; i < zn->zn_key_norm_numints;
			    wp++, i++) {
				uint64_t word = *wp;

				for (int j = 0; j < 8; j++) {
					h = (h >> 8) ^
					    zfs_crc64_table[(h ^ word) & 0xFF];
					word >>= NBBY;
				}
			}
		} else {
			const uint8_t *cp = zn->zn_key_norm;

			/*
			 * We previously stored the terminating null on
			 * disk, but didn't hash it, so we need to
			 * continue to not hash it.  (The
			 * zn_key_*_numints includes the terminating
			 * null for non-binary keys.)
			 */
			int len = zn->zn_key_norm_numints - 1;

			ASSERT(zn->zn_key_intlen == 1);
			for (int i = 0; i < len; cp++, i++) {
				h = (h >> 8) ^
				    zfs_crc64_table[(h ^ *cp) & 0xFF];
			}
		}
	}
	/*
	 * Don't use all 64 bits, since we need some in the cookie for
	 * the collision differentiator.  We MUST use the high bits,
	 * since those are the ones that we first pay attention to when
	 * choosing the bucket.
	 */
	h &= ~((1ULL << (64 - zap_hashbits(zap))) - 1);

	return (h);
}

/*
 * This routine "consumes" the caller's hold on the dbuf, which must
 * have the specified tag.
 */
int
zap_lock_impl(dnode_t *dn, dmu_buf_t *db, const void *tag, dmu_tx_t *tx,
    krw_t lti, boolean_t fatreader, boolean_t adding, zap_t **zapp)
{
	ASSERT0(db->db_offset);
	objset_t *os = dmu_buf_get_objset(db);
	uint64_t obj = db->db_object;

	*zapp = NULL;

	if (DMU_OT_BYTESWAP(dn->dn_type) != DMU_BSWAP_ZAP)
		return (SET_ERROR(EINVAL));

	zap_t *zap = dmu_buf_get_user(db);
	if (zap == NULL) {
		zap = mzap_open(db);
		if (zap == NULL) {
			/*
			 * mzap_open() didn't like what it saw on-disk.
			 * Check for corruption!
			 */
			return (SET_ERROR(EIO));
		}
	}

	/*
	 * We're checking zap_ismicro without the lock held, in order to
	 * tell what type of lock we want.  Once we have some sort of
	 * lock, see if it really is the right type.  In practice this
	 * can only be different if it was upgraded from micro to fat,
	 * and micro wanted WRITER but fat only needs READER.
	 */
	krw_t lt = (!zap->zap_ismicro && fatreader) ? RW_READER : lti;
	rw_enter(&zap->zap_rwlock, lt);
	if (lt != ((!zap->zap_ismicro && fatreader) ? RW_READER : lti)) {
		/* it was upgraded, now we only need reader */
		ASSERT(lt == RW_WRITER);
		ASSERT(RW_READER ==
		    ((!zap->zap_ismicro && fatreader) ? RW_READER : lti));
		rw_downgrade(&zap->zap_rwlock);
		lt = RW_READER;
	}

	zap->zap_objset = os;
	zap->zap_dnode = dn;

	if (lt == RW_WRITER)
		dmu_buf_will_dirty(db, tx);

	ASSERT3P(zap->zap_dbuf, ==, db);

	ASSERT(!zap->zap_ismicro ||
	    zap->zap_m.zap_num_entries <= zap->zap_m.zap_num_chunks);
	if (zap->zap_ismicro && tx && adding &&
	    zap->zap_m.zap_num_entries == zap->zap_m.zap_num_chunks) {
		uint64_t newsz = db->db_size + SPA_MINBLOCKSIZE;
		if (newsz > zap_get_micro_max_size(dmu_objset_spa(os))) {
			dprintf("upgrading obj %llu: num_entries=%u\n",
			    (u_longlong_t)obj, zap->zap_m.zap_num_entries);
			*zapp = zap;
			int err = mzap_upgrade(zapp, tag, tx, 0);
			if (err != 0)
				rw_exit(&zap->zap_rwlock);
			return (err);
		}
		VERIFY0(dmu_object_set_blocksize(os, obj, newsz, 0, tx));
		zap->zap_m.zap_num_chunks =
		    db->db_size / MZAP_ENT_LEN - 1;

		if (newsz > SPA_OLD_MAXBLOCKSIZE) {
			dsl_dataset_t *ds = dmu_objset_ds(os);
			if (!dsl_dataset_feature_is_active(ds,
			    SPA_FEATURE_LARGE_MICROZAP)) {
				/*
				 * A microzap just grew beyond the old limit
				 * for the first time, so we have to ensure the
				 * feature flag is activated.
				 * zap_get_micro_max_size() won't let us get
				 * here if the feature is not enabled, so we
				 * don't need any other checks beforehand.
				 *
				 * Since we're in open context, we can't
				 * activate the feature directly, so we instead
				 * flag it on the dataset for next sync.
				 */
				dsl_dataset_dirty(ds, tx);
				mutex_enter(&ds->ds_lock);
				ds->ds_feature_activation
				    [SPA_FEATURE_LARGE_MICROZAP] =
				    (void *)B_TRUE;
				mutex_exit(&ds->ds_lock);
			}
		}
	}

	*zapp = zap;
	return (0);
}

int
zap_lock_by_dnode(dnode_t *dn, dmu_tx_t *tx,
    krw_t lti, boolean_t fatreader, boolean_t adding, const void *tag,
    zap_t **zapp)
{
	dmu_buf_t *db;
	int err;

	err = dmu_buf_hold_by_dnode(dn, 0, tag, &db, DMU_READ_NO_PREFETCH);
	if (err != 0)
		return (err);
	err = zap_lock_impl(dn, db, tag, tx, lti, fatreader, adding, zapp);
	if (err != 0)
		dmu_buf_rele(db, tag);
	else
		VERIFY(dnode_add_ref(dn, tag));
	return (err);
}

int
zap_lock(objset_t *os, uint64_t obj, dmu_tx_t *tx,
    krw_t lti, boolean_t fatreader, boolean_t adding, const void *tag,
    zap_t **zapp)
{
	dnode_t *dn;
	dmu_buf_t *db;
	int err;

	err = dnode_hold(os, obj, tag, &dn);
	if (err != 0)
		return (err);
	err = dmu_buf_hold_by_dnode(dn, 0, tag, &db, DMU_READ_NO_PREFETCH);
	if (err != 0) {
		dnode_rele(dn, tag);
		return (err);
	}
	err = zap_lock_impl(dn, db, tag, tx, lti, fatreader, adding, zapp);
	if (err != 0) {
		dmu_buf_rele(db, tag);
		dnode_rele(dn, tag);
	}
	return (err);
}

void
zap_unlock(zap_t *zap, const void *tag)
{
	rw_exit(&zap->zap_rwlock);
	dnode_rele(zap->zap_dnode, tag);
	dmu_buf_rele(zap->zap_dbuf, tag);
}

void
zap_evict_sync(void *dbu)
{
	zap_t *zap = dbu;

	rw_destroy(&zap->zap_rwlock);

	if (zap->zap_ismicro)
		mze_destroy(zap);
	else
		mutex_destroy(&zap->zap_f.zap_num_entries_mtx);

	kmem_free(zap, sizeof (zap_t));
}

uint64_t
zap_getflags(zap_t *zap)
{
	if (zap->zap_ismicro)
		return (0);
	return (zap_f_phys(zap)->zap_flags);
}

int
zap_hashbits(zap_t *zap)
{
	if (zap_getflags(zap) & ZAP_FLAG_HASH64)
		return (48);
	else
		return (28);
}

uint32_t
zap_maxcd(zap_t *zap)
{
	if (zap_getflags(zap) & ZAP_FLAG_HASH64)
		return ((1<<16)-1);
	else
		return (-1U);
}

/* DNU byteswap callback for DMU_BSWAP_ZAP, see dmu_ot_byteswap. */
void
zap_byteswap(void *buf, size_t size)
{
	uint64_t block_type = *(uint64_t *)buf;

	if (block_type == ZBT_MICRO || block_type == BSWAP_64(ZBT_MICRO)) {
		/* ASSERT(magic == ZAP_LEAF_MAGIC); */
		mzap_byteswap(buf, size);
	} else {
		fzap_byteswap(buf, size);
	}
}

/*
 * Cursor attribute allocator/free. Part of the public interface in zap.h,
 * in this file to get access to the kmem caches.
 */
static zap_attribute_t *
zap_attribute_alloc_impl(boolean_t longname)
{
	zap_attribute_t *za;

	za = kmem_cache_alloc((longname)? zap_attr_long_cache : zap_attr_cache,
	    KM_SLEEP);
	za->za_name_len = (longname)? ZAP_MAXNAMELEN_NEW : ZAP_MAXNAMELEN;
	return (za);
}

zap_attribute_t *
zap_attribute_alloc(void)
{
	return (zap_attribute_alloc_impl(B_FALSE));
}

zap_attribute_t *
zap_attribute_long_alloc(void)
{
	return (zap_attribute_alloc_impl(B_TRUE));
}

void
zap_attribute_free(zap_attribute_t *za)
{
	if (za->za_name_len == ZAP_MAXNAMELEN) {
		kmem_cache_free(zap_attr_cache, za);
	} else {
		ASSERT3U(za->za_name_len, ==, ZAP_MAXNAMELEN_NEW);
		kmem_cache_free(zap_attr_long_cache, za);
	}
}
