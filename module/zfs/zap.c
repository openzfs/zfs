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
#include <sys/btree.h>
#include <sys/zap.h>
#include <sys/zap_impl.h>
#include <sys/zap_leaf.h>

/* zap_create */

static uint64_t
zap_create_impl(objset_t *os, int normflags, zap_flags_t flags,
    dmu_object_type_t ot, int leaf_blockshift, int indirect_blockshift,
    dmu_object_type_t bonustype, int bonuslen, int dnodesize,
    dnode_t **allocated_dnode, const void *tag, dmu_tx_t *tx)
{
	uint64_t obj;

	ASSERT3U(DMU_OT_BYTESWAP(ot), ==, DMU_BSWAP_ZAP);

	if (allocated_dnode == NULL) {
		dnode_t *dn;
		obj = dmu_object_alloc_hold(os, ot, 1ULL << leaf_blockshift,
		    indirect_blockshift, bonustype, bonuslen, dnodesize,
		    &dn, FTAG, tx);
		mzap_create_impl(dn, normflags, flags, tx);
		dnode_rele(dn, FTAG);
	} else {
		obj = dmu_object_alloc_hold(os, ot, 1ULL << leaf_blockshift,
		    indirect_blockshift, bonustype, bonuslen, dnodesize,
		    allocated_dnode, tag, tx);
		mzap_create_impl(*allocated_dnode, normflags, flags, tx);
	}

	return (obj);
}

uint64_t
zap_create(objset_t *os, dmu_object_type_t ot,
    dmu_object_type_t bonustype, int bonuslen, dmu_tx_t *tx)
{
	return (zap_create_norm(os, 0, ot, bonustype, bonuslen, tx));
}

uint64_t
zap_create_dnsize(objset_t *os, dmu_object_type_t ot,
    dmu_object_type_t bonustype, int bonuslen, int dnodesize, dmu_tx_t *tx)
{
	return (zap_create_norm_dnsize(os, 0, ot, bonustype, bonuslen,
	    dnodesize, tx));
}

uint64_t
zap_create_norm(objset_t *os, int normflags, dmu_object_type_t ot,
    dmu_object_type_t bonustype, int bonuslen, dmu_tx_t *tx)
{
	return (zap_create_norm_dnsize(os, normflags, ot, bonustype, bonuslen,
	    0, tx));
}

uint64_t
zap_create_norm_dnsize(objset_t *os, int normflags, dmu_object_type_t ot,
    dmu_object_type_t bonustype, int bonuslen, int dnodesize, dmu_tx_t *tx)
{
	return (zap_create_impl(os, normflags, 0, ot, 0, 0,
	    bonustype, bonuslen, dnodesize, NULL, NULL, tx));
}

uint64_t
zap_create_flags(objset_t *os, int normflags, zap_flags_t flags,
    dmu_object_type_t ot, int leaf_blockshift, int indirect_blockshift,
    dmu_object_type_t bonustype, int bonuslen, dmu_tx_t *tx)
{
	return (zap_create_flags_dnsize(os, normflags, flags, ot,
	    leaf_blockshift, indirect_blockshift, bonustype, bonuslen, 0, tx));
}

uint64_t
zap_create_flags_dnsize(objset_t *os, int normflags, zap_flags_t flags,
    dmu_object_type_t ot, int leaf_blockshift, int indirect_blockshift,
    dmu_object_type_t bonustype, int bonuslen, int dnodesize, dmu_tx_t *tx)
{
	return (zap_create_impl(os, normflags, flags, ot, leaf_blockshift,
	    indirect_blockshift, bonustype, bonuslen, dnodesize, NULL, NULL,
	    tx));
}

/* zap_crate_hold */

uint64_t
zap_create_hold(objset_t *os, int normflags, zap_flags_t flags,
    dmu_object_type_t ot, int leaf_blockshift, int indirect_blockshift,
    dmu_object_type_t bonustype, int bonuslen, int dnodesize,
    dnode_t **allocated_dnode, const void *tag, dmu_tx_t *tx)
{
	return (zap_create_impl(os, normflags, flags, ot, leaf_blockshift,
	    indirect_blockshift, bonustype, bonuslen, dnodesize,
	    allocated_dnode, tag, tx));
}

/* zap_create_link */

uint64_t
zap_create_link(objset_t *os, dmu_object_type_t ot, uint64_t parent_obj,
    const char *name, dmu_tx_t *tx)
{
	return (zap_create_link_dnsize(os, ot, parent_obj, name, 0, tx));
}

uint64_t
zap_create_link_dnsize(objset_t *os, dmu_object_type_t ot, uint64_t parent_obj,
    const char *name, int dnodesize, dmu_tx_t *tx)
{
	uint64_t new_obj;

	new_obj = zap_create_dnsize(os, ot, DMU_OT_NONE, 0, dnodesize, tx);
	VERIFY(new_obj != 0);
	VERIFY0(zap_add(os, parent_obj, name, sizeof (uint64_t), 1, &new_obj,
	    tx));

	return (new_obj);
}

/* zap_create_claim */

int
zap_create_claim(objset_t *os, uint64_t obj, dmu_object_type_t ot,
    dmu_object_type_t bonustype, int bonuslen, dmu_tx_t *tx)
{
	return (zap_create_claim_dnsize(os, obj, ot, bonustype, bonuslen,
	    0, tx));
}

int
zap_create_claim_dnsize(objset_t *os, uint64_t obj, dmu_object_type_t ot,
    dmu_object_type_t bonustype, int bonuslen, int dnodesize, dmu_tx_t *tx)
{
	return (zap_create_claim_norm_dnsize(os, obj,
	    0, ot, bonustype, bonuslen, dnodesize, tx));
}

int
zap_create_claim_norm(objset_t *os, uint64_t obj, int normflags,
    dmu_object_type_t ot,
    dmu_object_type_t bonustype, int bonuslen, dmu_tx_t *tx)
{
	return (zap_create_claim_norm_dnsize(os, obj, normflags, ot, bonustype,
	    bonuslen, 0, tx));
}

int
zap_create_claim_norm_dnsize(objset_t *os, uint64_t obj, int normflags,
    dmu_object_type_t ot, dmu_object_type_t bonustype, int bonuslen,
    int dnodesize, dmu_tx_t *tx)
{
	dnode_t *dn;
	int error;

	ASSERT3U(DMU_OT_BYTESWAP(ot), ==, DMU_BSWAP_ZAP);
	error = dmu_object_claim_dnsize(os, obj, ot, 0, bonustype, bonuslen,
	    dnodesize, tx);
	if (error != 0)
		return (error);

	error = dnode_hold(os, obj, FTAG, &dn);
	if (error != 0)
		return (error);

	mzap_create_impl(dn, normflags, 0, tx);

	dnode_rele(dn, FTAG);

	return (0);
}

/* zap_destroy */

int
zap_destroy(objset_t *os, uint64_t zapobj, dmu_tx_t *tx)
{
	/*
	 * dmu_object_free will free the object number and free the
	 * data.  Freeing the data will cause our pageout function to be
	 * called, which will destroy our data (zap_leaf_t's and zap_t).
	 */

	return (dmu_object_free(os, zapobj, tx));
}

/* zap_lookup */

int
zap_lookup_norm_by_dnode(dnode_t *dn, const char *name,
    uint64_t integer_size, uint64_t num_integers, void *buf,
    matchtype_t mt, char *realname, int rn_len,
    boolean_t *ncp)
{
	zap_t *zap;

	int err =
	    zap_lock_by_dnode(dn, NULL, RW_READER, TRUE, FALSE, FTAG, &zap);
	if (err != 0)
		return (err);

	zap_name_t *zn = zap_name_alloc_str(zap, name, mt);
	if (zn == NULL) {
		zap_unlock(zap, FTAG);
		return (SET_ERROR(ENOTSUP));
	}

	if (!zap->zap_ismicro) {
		err = fzap_lookup(zn, integer_size, num_integers, buf,
		    realname, rn_len, ncp, NULL);
	} else {
		zfs_btree_index_t idx;
		mzap_ent_t *mze = mze_find(zn, &idx);
		if (mze == NULL) {
			err = SET_ERROR(ENOENT);
		} else {
			if (num_integers < 1) {
				err = SET_ERROR(EOVERFLOW);
			} else if (integer_size != 8) {
				err = SET_ERROR(EINVAL);
			} else {
				*(uint64_t *)buf =
				    MZE_PHYS(zap, mze)->mze_value;
				if (realname != NULL)
					(void) strlcpy(realname,
					    MZE_PHYS(zap, mze)->mze_name,
					    rn_len);
				if (ncp) {
					*ncp = mzap_normalization_conflict(zap,
					    zn, mze, &idx);
				}
			}
		}
	}
	zap_name_free(zn);
	zap_unlock(zap, FTAG);
	return (err);
}

int
zap_lookup(objset_t *os, uint64_t zapobj, const char *name,
    uint64_t integer_size, uint64_t num_integers, void *buf)
{
	return (zap_lookup_norm(os, zapobj, name, integer_size,
	    num_integers, buf, 0, NULL, 0, NULL));
}

int
zap_lookup_by_dnode(dnode_t *dn, const char *name,
    uint64_t integer_size, uint64_t num_integers, void *buf)
{
	return (zap_lookup_norm_by_dnode(dn, name, integer_size,
	    num_integers, buf, 0, NULL, 0, NULL));
}

int
zap_lookup_norm(objset_t *os, uint64_t zapobj, const char *name,
    uint64_t integer_size, uint64_t num_integers, void *buf,
    matchtype_t mt, char *realname, int rn_len,
    boolean_t *ncp)
{
	dnode_t *dn;
	int err = dnode_hold(os, zapobj, FTAG, &dn);
	if (err != 0)
		return (err);
	err = zap_lookup_norm_by_dnode(dn, name, integer_size,
	    num_integers, buf, mt, realname, rn_len, ncp);
	dnode_rele(dn, FTAG);
	return (err);
}

/* zap_lookup_uint64 */

int
zap_lookup_length_uint64_by_dnode(dnode_t *dn, const uint64_t *key,
    int key_numints, uint64_t integer_size, uint64_t num_integers, void *buf,
    uint64_t *actual_num_integers)
{
	zap_t *zap;
	int err =
	    zap_lock_by_dnode(dn, NULL, RW_READER, TRUE, FALSE, FTAG, &zap);
	if (err != 0)
		return (err);

	zap_name_t *zn = zap_name_alloc_uint64(zap, key, key_numints);
	if (zn == NULL) {
		zap_unlock(zap, FTAG);
		return (SET_ERROR(ENOTSUP));
	}

	err = fzap_lookup(zn, integer_size, num_integers, buf,
	    NULL, 0, NULL, actual_num_integers);
	zap_name_free(zn);
	zap_unlock(zap, FTAG);
	return (err);
}

int
zap_lookup_uint64(objset_t *os, uint64_t zapobj, const uint64_t *key,
    int key_numints, uint64_t integer_size, uint64_t num_integers, void *buf)
{
	dnode_t *dn;
	int err = dnode_hold(os, zapobj, FTAG, &dn);
	if (err != 0)
		return (err);
	err = zap_lookup_length_uint64_by_dnode(dn, key, key_numints,
	    integer_size, num_integers, buf, NULL);
	dnode_rele(dn, FTAG);
	return (err);
}

int
zap_lookup_uint64_by_dnode(dnode_t *dn, const uint64_t *key,
    int key_numints, uint64_t integer_size, uint64_t num_integers, void *buf)
{
	return (zap_lookup_length_uint64_by_dnode(dn, key, key_numints,
	    integer_size, num_integers, buf, NULL));
}

/* zap_contains */

int
zap_contains_by_dnode(dnode_t *dn, const char *name)
{
	int err = zap_lookup_norm_by_dnode(dn, name, 0,
	    0, NULL, 0, NULL, 0, NULL);
	if (err == EOVERFLOW || err == EINVAL)
		err = 0; /* found, but skipped reading the value */
	return (err);
}

int
zap_contains(objset_t *os, uint64_t zapobj, const char *name)
{
	dnode_t *dn;
	int err = dnode_hold(os, zapobj, FTAG, &dn);
	if (err != 0)
		return (err);
	err = zap_contains_by_dnode(dn, name);
	dnode_rele(dn, FTAG);
	return (err);
}

/* zap_prefetch */

static int
zap_prefetch_by_dnode(dnode_t *dn, const char *name)
{
	zap_t *zap;
	int err =
	    zap_lock_by_dnode(dn, NULL, RW_READER, TRUE, FALSE, FTAG, &zap);
	if (err)
		return (err);

	zap_name_t *zn = zap_name_alloc_str(zap, name, 0);
	if (zn == NULL) {
		zap_unlock(zap, FTAG);
		return (SET_ERROR(ENOTSUP));
	}

	fzap_prefetch(zn);
	zap_name_free(zn);
	zap_unlock(zap, FTAG);
	return (err);
}

int
zap_prefetch(objset_t *os, uint64_t zapobj, const char *name)
{
	dnode_t *dn;
	int err = dnode_hold(os, zapobj, FTAG, &dn);
	if (err != 0)
		return (err);
	err = zap_prefetch_by_dnode(dn, name);
	dnode_rele(dn, FTAG);
	return (err);
}

/* zap_prefetch_uint64 */

int
zap_prefetch_uint64_by_dnode(dnode_t *dn, const uint64_t *key, int key_numints)
{
	zap_t *zap;
	int err =
	    zap_lock_by_dnode(dn, NULL, RW_READER, TRUE, FALSE, FTAG, &zap);
	if (err != 0)
		return (err);

	zap_name_t *zn = zap_name_alloc_uint64(zap, key, key_numints);
	if (zn == NULL) {
		zap_unlock(zap, FTAG);
		return (SET_ERROR(ENOTSUP));
	}

	fzap_prefetch(zn);
	zap_name_free(zn);
	zap_unlock(zap, FTAG);
	return (0);
}

int
zap_prefetch_uint64(objset_t *os, uint64_t zapobj, const uint64_t *key,
    int key_numints)
{
	dnode_t *dn;
	int err = dnode_hold(os, zapobj, FTAG, &dn);
	if (err != 0)
		return (err);
	err = zap_prefetch_uint64_by_dnode(dn, key, key_numints);
	dnode_rele(dn, FTAG);
	return (err);
}

/* zap_prefetch_object */

int
zap_prefetch_object(objset_t *os, uint64_t zapobj)
{
	int error;
	dmu_object_info_t doi;

	error = dmu_object_info(os, zapobj, &doi);
	if (error == 0 && DMU_OT_BYTESWAP(doi.doi_type) != DMU_BSWAP_ZAP)
		error = SET_ERROR(EINVAL);
	if (error == 0)
		dmu_prefetch_wait(os, zapobj, 0, doi.doi_max_offset);

	return (error);
}

/* zap_add */

int
zap_add_by_dnode(dnode_t *dn, const char *key,
    int integer_size, uint64_t num_integers,
    const void *val, dmu_tx_t *tx)
{
	zap_t *zap;
	int err =
	    zap_lock_by_dnode(dn, tx, RW_WRITER, TRUE, TRUE, FTAG, &zap);
	if (err != 0)
		return (err);

	const uint64_t *intval = val;
	zap_name_t *zn = zap_name_alloc_str(zap, key, 0);
	if (zn == NULL) {
		zap_unlock(zap, FTAG);
		return (SET_ERROR(ENOTSUP));
	}
	if (!zap->zap_ismicro) {
		err = fzap_add(zn, integer_size, num_integers, val, tx);
	} else if (integer_size != 8 || num_integers != 1 ||
	    strlen(key) >= MZAP_NAME_LEN ||
	    !mze_canfit_fzap_leaf(zn, zn->zn_hash)) {
		err = mzap_upgrade(&zn->zn_zap, tx, 0);
		if (err == 0) {
			err = fzap_add(zn, integer_size, num_integers, val, tx);
		}
	} else {
		zfs_btree_index_t idx;
		if (mze_find(zn, &idx) != NULL) {
			err = SET_ERROR(EEXIST);
		} else {
			mzap_addent(zn, *intval);
		}
	}
	ASSERT(zap == zn->zn_zap);
	zap_name_free(zn);
	zap_unlock(zap, FTAG);
	return (err);
}

int
zap_add(objset_t *os, uint64_t zapobj, const char *key,
    int integer_size, uint64_t num_integers,
    const void *val, dmu_tx_t *tx)
{
	dnode_t *dn;
	int err = dnode_hold(os, zapobj, FTAG, &dn);
	if (err != 0)
		return (err);
	err = zap_add_by_dnode(dn, key, integer_size, num_integers, val, tx);
	dnode_rele(dn, FTAG);
	return (err);
}

/* zap_add_uint64 */

int
zap_add_uint64_by_dnode(dnode_t *dn, const uint64_t *key,
    int key_numints, int integer_size, uint64_t num_integers,
    const void *val, dmu_tx_t *tx)
{
	zap_t *zap;
	int err =
	    zap_lock_by_dnode(dn, tx, RW_WRITER, TRUE, TRUE, FTAG, &zap);
	if (err != 0)
		return (err);

	zap_name_t *zn = zap_name_alloc_uint64(zap, key, key_numints);
	if (zn == NULL) {
		zap_unlock(zap, FTAG);
		return (SET_ERROR(ENOTSUP));
	}
	err = fzap_add(zn, integer_size, num_integers, val, tx);
	zap = zn->zn_zap;	/* fzap_add() may change zap */
	zap_name_free(zn);
	if (zap != NULL)	/* may be NULL if fzap_add() failed */
		zap_unlock(zap, FTAG);
	return (err);
}

int
zap_add_uint64(objset_t *os, uint64_t zapobj, const uint64_t *key,
    int key_numints, int integer_size, uint64_t num_integers,
    const void *val, dmu_tx_t *tx)
{
	dnode_t *dn;
	int err = dnode_hold(os, zapobj, FTAG, &dn);
	if (err != 0)
		return (err);
	err = zap_add_uint64_by_dnode(dn, key, key_numints,
	    integer_size, num_integers, val, tx);
	dnode_rele(dn, FTAG);
	return (err);
}

/* zap_update */

int
zap_update_by_dnode(dnode_t *dn, const char *name, int integer_size,
    uint64_t num_integers, const void *val, dmu_tx_t *tx)
{
	zap_t *zap;
	int err =
	    zap_lock_by_dnode(dn, tx, RW_WRITER, TRUE, TRUE, FTAG, &zap);
	if (err != 0)
		return (err);

	const uint64_t *intval = val;
	zap_name_t *zn = zap_name_alloc_str(zap, name, 0);
	if (zn == NULL) {
		zap_unlock(zap, FTAG);
		return (SET_ERROR(ENOTSUP));
	}
	if (!zap->zap_ismicro) {
		err = fzap_update(zn, integer_size, num_integers, val, tx);
	} else if (integer_size != 8 || num_integers != 1 ||
	    strlen(name) >= MZAP_NAME_LEN) {
		dprintf("upgrading obj %llu: intsz=%u numint=%llu name=%s\n",
		    (u_longlong_t)dn->dn_object, integer_size,
		    (u_longlong_t)num_integers, name);
		err = mzap_upgrade(&zn->zn_zap, tx, 0);
		if (err == 0) {
			err = fzap_update(zn, integer_size, num_integers,
			    val, tx);
		}
	} else {
		zfs_btree_index_t idx;
		mzap_ent_t *mze = mze_find(zn, &idx);
		if (mze != NULL) {
			MZE_PHYS(zap, mze)->mze_value = *intval;
		} else {
			mzap_addent(zn, *intval);
		}
	}
	ASSERT(zap == zn->zn_zap);
	zap_name_free(zn);
	zap_unlock(zap, FTAG);
	return (err);
}

int
zap_update(objset_t *os, uint64_t zapobj, const char *name,
    int integer_size, uint64_t num_integers, const void *val, dmu_tx_t *tx)
{
	dnode_t *dn;
	int err = dnode_hold(os, zapobj, FTAG, &dn);
	if (err != 0)
		return (err);
	err = zap_update_by_dnode(dn, name,
	    integer_size, num_integers, val, tx);
	dnode_rele(dn, FTAG);
	return (err);
}

/* zap_update_uint64 */

int
zap_update_uint64_by_dnode(dnode_t *dn, const uint64_t *key, int key_numints,
    int integer_size, uint64_t num_integers, const void *val, dmu_tx_t *tx)
{
	zap_t *zap;
	int err =
	    zap_lock_by_dnode(dn, tx, RW_WRITER, TRUE, TRUE, FTAG, &zap);
	if (err != 0)
		return (err);

	zap_name_t *zn = zap_name_alloc_uint64(zap, key, key_numints);
	if (zn == NULL) {
		zap_unlock(zap, FTAG);
		return (SET_ERROR(ENOTSUP));
	}
	err = fzap_update(zn, integer_size, num_integers, val, tx);
	zap_name_free(zn);
	zap_unlock(zap, FTAG);
	return (err);
}

int
zap_update_uint64(objset_t *os, uint64_t zapobj, const uint64_t *key,
    int key_numints, int integer_size, uint64_t num_integers, const void *val,
    dmu_tx_t *tx)
{
	dnode_t *dn;
	int err = dnode_hold(os, zapobj, FTAG, &dn);
	if (err != 0)
		return (err);
	err = zap_update_uint64_by_dnode(dn, key, key_numints,
	    integer_size, num_integers, val, tx);
	dnode_rele(dn, FTAG);
	return (err);
}

/* zap_length */

int
zap_length_by_dnode(dnode_t *dn, const char *name, uint64_t *integer_size,
    uint64_t *num_integers)
{
	zap_t *zap;
	int err =
	    zap_lock_by_dnode(dn, NULL, RW_READER, TRUE, FALSE, FTAG, &zap);
	if (err != 0)
		return (err);

	zap_name_t *zn = zap_name_alloc_str(zap, name, 0);
	if (zn == NULL) {
		zap_unlock(zap, FTAG);
		return (SET_ERROR(ENOTSUP));
	}
	if (!zap->zap_ismicro) {
		err = fzap_length(zn, integer_size, num_integers);
	} else {
		zfs_btree_index_t idx;
		mzap_ent_t *mze = mze_find(zn, &idx);
		if (mze == NULL) {
			err = SET_ERROR(ENOENT);
		} else {
			if (integer_size)
				*integer_size = 8;
			if (num_integers)
				*num_integers = 1;
		}
	}
	zap_name_free(zn);
	zap_unlock(zap, FTAG);
	return (err);
}

int
zap_length(objset_t *os, uint64_t zapobj, const char *name,
    uint64_t *integer_size, uint64_t *num_integers)
{
	dnode_t *dn;
	int err = dnode_hold(os, zapobj, FTAG, &dn);
	if (err != 0)
		return (err);
	err = zap_length_by_dnode(dn, name, integer_size, num_integers);
	dnode_rele(dn, FTAG);
	return (err);
}

/* zap_length_uint64 */

int
zap_length_uint64_by_dnode(dnode_t *dn, const uint64_t *key,
    int key_numints, uint64_t *integer_size, uint64_t *num_integers)
{
	zap_t *zap;
	int err =
	    zap_lock_by_dnode(dn, NULL, RW_READER, TRUE, FALSE, FTAG, &zap);
	if (err != 0)
		return (err);
	zap_name_t *zn = zap_name_alloc_uint64(zap, key, key_numints);
	if (zn == NULL) {
		zap_unlock(zap, FTAG);
		return (SET_ERROR(ENOTSUP));
	}
	err = fzap_length(zn, integer_size, num_integers);
	zap_name_free(zn);
	zap_unlock(zap, FTAG);
	return (err);
}

int
zap_length_uint64(objset_t *os, uint64_t zapobj, const uint64_t *key,
    int key_numints, uint64_t *integer_size, uint64_t *num_integers)
{
	dnode_t *dn;
	int err = dnode_hold(os, zapobj, FTAG, &dn);
	if (err != 0)
		return (err);
	err = zap_length_uint64_by_dnode(dn, key, key_numints,
	    integer_size, num_integers);
	dnode_rele(dn, FTAG);
	return (err);
}

/* zap_remove */

int
zap_remove_norm_by_dnode(dnode_t *dn, const char *name, matchtype_t mt,
    dmu_tx_t *tx)
{
	zap_t *zap;
	int err =
	    zap_lock_by_dnode(dn, tx, RW_WRITER, TRUE, FALSE, FTAG, &zap);
	if (err)
		return (err);

	zap_name_t *zn = zap_name_alloc_str(zap, name, mt);
	if (zn == NULL) {
		zap_unlock(zap, FTAG);
		return (SET_ERROR(ENOTSUP));
	}
	if (!zap->zap_ismicro) {
		err = fzap_remove(zn, tx);
	} else {
		zfs_btree_index_t idx;
		mzap_ent_t *mze = mze_find(zn, &idx);
		if (mze == NULL) {
			err = SET_ERROR(ENOENT);
		} else {
			zap->zap_m.zap_num_entries--;
			memset(MZE_PHYS(zap, mze), 0, sizeof (mzap_ent_phys_t));
			zfs_btree_remove_idx(&zap->zap_m.zap_tree, &idx);
		}
	}
	zap_name_free(zn);
	zap_unlock(zap, FTAG);
	return (err);
}

int
zap_remove(objset_t *os, uint64_t zapobj, const char *name, dmu_tx_t *tx)
{
	return (zap_remove_norm(os, zapobj, name, 0, tx));
}

int
zap_remove_by_dnode(dnode_t *dn, const char *name, dmu_tx_t *tx)
{
	return (zap_remove_norm_by_dnode(dn, name, 0, tx));
}

int
zap_remove_norm(objset_t *os, uint64_t zapobj, const char *name,
    matchtype_t mt, dmu_tx_t *tx)
{
	dnode_t *dn;
	int err = dnode_hold(os, zapobj, FTAG, &dn);
	if (err != 0)
		return (err);
	err = zap_remove_norm_by_dnode(dn, name, mt, tx);
	dnode_rele(dn, FTAG);
	return (err);
}

/* zap_remove_uint64 */

int
zap_remove_uint64_by_dnode(dnode_t *dn, const uint64_t *key, int key_numints,
    dmu_tx_t *tx)
{
	zap_t *zap;
	int err =
	    zap_lock_by_dnode(dn, tx, RW_WRITER, TRUE, FALSE, FTAG, &zap);
	if (err != 0)
		return (err);

	zap_name_t *zn = zap_name_alloc_uint64(zap, key, key_numints);
	if (zn == NULL) {
		zap_unlock(zap, FTAG);
		return (SET_ERROR(ENOTSUP));
	}
	err = fzap_remove(zn, tx);
	zap_name_free(zn);
	zap_unlock(zap, FTAG);
	return (err);
}

int
zap_remove_uint64(objset_t *os, uint64_t zapobj, const uint64_t *key,
    int key_numints, dmu_tx_t *tx)
{
	dnode_t *dn;
	int err = dnode_hold(os, zapobj, FTAG, &dn);
	if (err != 0)
		return (err);
	err = zap_remove_uint64_by_dnode(dn, key, key_numints, tx);
	dnode_rele(dn, FTAG);
	return (err);
}

/* zap_count */

int
zap_count_by_dnode(dnode_t *dn, uint64_t *count)
{
	zap_t *zap;
	int err =
	    zap_lock_by_dnode(dn, NULL, RW_READER, TRUE, FALSE, FTAG, &zap);
	if (err != 0)
		return (err);
	if (!zap->zap_ismicro) {
		err = fzap_count(zap, count);
	} else {
		*count = zap->zap_m.zap_num_entries;
	}
	zap_unlock(zap, FTAG);
	return (err);
}

int
zap_count(objset_t *os, uint64_t zapobj, uint64_t *count)
{
	dnode_t *dn;
	int err = dnode_hold(os, zapobj, FTAG, &dn);
	if (err != 0)
		return (err);
	err = zap_count_by_dnode(dn, count);
	dnode_rele(dn, FTAG);
	return (err);
}

/* zap_increment */

int
zap_increment_by_dnode(dnode_t *dn, const char *name, int64_t delta,
    dmu_tx_t *tx)
{
	uint64_t value = 0;

	if (delta == 0)
		return (0);

	int err = zap_lookup_by_dnode(dn, name, 8, 1, &value);
	if (err != 0 && err != ENOENT)
		return (err);
	value += delta;
	if (value == 0)
		err = zap_remove_by_dnode(dn, name, tx);
	else
		err = zap_update_by_dnode(dn, name, 8, 1, &value, tx);
	return (err);
}

int
zap_increment(objset_t *os, uint64_t zapobj, const char *name, int64_t delta,
    dmu_tx_t *tx)
{
	dnode_t *dn;
	int err = dnode_hold(os, zapobj, FTAG, &dn);
	if (err != 0)
		return (err);
	err = zap_increment_by_dnode(dn, name, delta, tx);
	dnode_rele(dn, FTAG);
	return (err);
}

/* zap_value_search */

static int
zap_value_search_impl(zap_cursor_t *zc, uint64_t value, uint64_t mask,
    char *name, uint64_t namelen)
{
	int err;

	if (mask == 0)
		mask = -1ULL;

	zap_attribute_t *za = zap_attribute_long_alloc();
	for (; (err = zap_cursor_retrieve(zc, za)) == 0;
	    zap_cursor_advance(zc)) {
		if ((za->za_first_integer & mask) == (value & mask)) {
			if (strlcpy(name, za->za_name, namelen) >= namelen)
				err = SET_ERROR(ENAMETOOLONG);
			break;
		}
	}
	zap_cursor_fini(zc);
	zap_attribute_free(za);
	return (err);
}

int
zap_value_search(objset_t *os, uint64_t zapobj, uint64_t value, uint64_t mask,
    char *name, uint64_t namelen)
{
	zap_cursor_t zc;
	zap_cursor_init(&zc, os, zapobj);
	return (zap_value_search_impl(&zc, value, mask, name, namelen));
}

int
zap_value_search_by_dnode(dnode_t *dn, uint64_t value, uint64_t mask,
    char *name, uint64_t namelen)
{
	zap_cursor_t zc;
	zap_cursor_init_by_dnode(&zc, dn);
	return (zap_value_search_impl(&zc, value, mask, name, namelen));
}

/* zap_*_int */

#define	FORMAT_INT_KEY(name, value)	\
	char name[20];			\
	(void) snprintf(name, sizeof (name), "%llx", (longlong_t)value);

int
zap_add_int(objset_t *os, uint64_t obj, uint64_t value, dmu_tx_t *tx)
{
	FORMAT_INT_KEY(name, value);
	return (zap_add(os, obj, name, 8, 1, &value, tx));
}
int
zap_add_int_by_dnode(dnode_t *dn, uint64_t value, dmu_tx_t *tx)
{
	FORMAT_INT_KEY(name, value);
	return (zap_add_by_dnode(dn, name, 8, 1, &value, tx));
}

int
zap_remove_int(objset_t *os, uint64_t obj, uint64_t value, dmu_tx_t *tx)
{
	FORMAT_INT_KEY(name, value);
	return (zap_remove(os, obj, name, tx));
}
int
zap_remove_int_by_dnode(dnode_t *dn, uint64_t value, dmu_tx_t *tx)
{
	FORMAT_INT_KEY(name, value);
	return (zap_remove_by_dnode(dn, name, tx));
}

int
zap_lookup_int(objset_t *os, uint64_t obj, uint64_t value)
{
	FORMAT_INT_KEY(name, value);
	return (zap_lookup(os, obj, name, 8, 1, &value));
}

int
zap_lookup_int_by_dnode(dnode_t *dn, uint64_t value)
{
	FORMAT_INT_KEY(name, value);
	return (zap_lookup_by_dnode(dn, name, 8, 1, &value));
}

/* zap_*_int_key */

int
zap_add_int_key(objset_t *os, uint64_t obj,
    uint64_t key, uint64_t value, dmu_tx_t *tx)
{
	FORMAT_INT_KEY(name, key);
	return (zap_add(os, obj, name, 8, 1, &value, tx));
}
int
zap_add_int_key_by_dnode(dnode_t *dn,
    uint64_t key, uint64_t value, dmu_tx_t *tx)
{
	FORMAT_INT_KEY(name, key);
	return (zap_add_by_dnode(dn, name, 8, 1, &value, tx));
}

int
zap_update_int_key(objset_t *os, uint64_t obj,
    uint64_t key, uint64_t value, dmu_tx_t *tx)
{
	FORMAT_INT_KEY(name, key);
	return (zap_update(os, obj, name, 8, 1, &value, tx));
}
int
zap_update_int_key_by_dnode(dnode_t *dn,
    uint64_t key, uint64_t value, dmu_tx_t *tx)
{
	FORMAT_INT_KEY(name, key);
	return (zap_update_by_dnode(dn, name, 8, 1, &value, tx));
}

int
zap_lookup_int_key(objset_t *os, uint64_t obj, uint64_t key, uint64_t *valuep)
{
	FORMAT_INT_KEY(name, key);
	return (zap_lookup(os, obj, name, 8, 1, valuep));
}
int
zap_lookup_int_key_by_dnode(dnode_t *dn, uint64_t key, uint64_t *valuep)
{
	FORMAT_INT_KEY(name, key);
	return (zap_lookup_by_dnode(dn, name, 8, 1, valuep));
}

/* zap_cursor */

static int
zap_cursor_init_by_dnode_impl(zap_cursor_t *zc, dnode_t *dn,
    uint64_t serialized, boolean_t prefetch)
{
	zc->zc_zap = NULL;
	zc->zc_leaf = NULL;

	int err = zap_lock_by_dnode(dn, NULL, RW_READER, TRUE, FALSE,
	    zc, &zc->zc_zap);
	if (err != 0)
		return (err);

	zc->zc_prefetch = prefetch;
	zc->zc_objset = dn->dn_objset;
	zc->zc_zapobj = dn->dn_object;

	int hb = zap_hashbits(zc->zc_zap);
	zc->zc_hash = serialized << (64 - hb);
	zc->zc_cd = serialized >> hb;
	if (zc->zc_cd >= zap_maxcd(zc->zc_zap)) /* corrupt serialized */
		zc->zc_cd = 0;

	/*
	 * Drop ZAP read lock, but keep the hold, so the holds on the
	 * underlying dnode and header dbuf are maintained.
	 */
	rw_exit(&zc->zc_zap->zap_rwlock);

	return (0);
}

static int
zap_cursor_init_impl(zap_cursor_t *zc, objset_t *os, uint64_t zapobj,
    uint64_t serialized, uint32_t prefetch)
{
	dnode_t *dn = NULL;
	int err = dnode_hold(os, zapobj, FTAG, &dn);
	if (err != 0) {
		zc->zc_zap = NULL;
		zc->zc_leaf = NULL;
		return (err);
	}

	err = zap_cursor_init_by_dnode_impl(zc, dn, serialized, prefetch);

	dnode_rele(dn, FTAG);

	return (err);
}

int
zap_cursor_init(zap_cursor_t *zc, objset_t *os, uint64_t zapobj)
{
	return (zap_cursor_init_impl(zc, os, zapobj, 0, B_TRUE));
}

int
zap_cursor_init_by_dnode(zap_cursor_t *zc, dnode_t *dn)
{
	return (zap_cursor_init_by_dnode_impl(zc, dn, 0, B_TRUE));
}

int
zap_cursor_init_noprefetch(zap_cursor_t *zc, objset_t *os, uint64_t zapobj)
{
	return (zap_cursor_init_impl(zc, os, zapobj, 0, B_FALSE));
}

int
zap_cursor_init_serialized(zap_cursor_t *zc, objset_t *os, uint64_t zapobj,
    uint64_t serialized)
{
	return (zap_cursor_init_impl(zc, os, zapobj, serialized, B_TRUE));
}

int
zap_cursor_init_serialized_by_dnode(zap_cursor_t *zc, dnode_t *dn,
    uint64_t serialized)
{
	return (zap_cursor_init_by_dnode_impl(zc, dn, serialized, B_TRUE));
}

void
zap_cursor_fini(zap_cursor_t *zc)
{
	if (zc->zc_leaf) {
		rw_enter(&zc->zc_leaf->l_rwlock, RW_READER);
		zap_put_leaf(zc->zc_leaf);
	}
	if (zc->zc_zap) {
		rw_enter(&zc->zc_zap->zap_rwlock, RW_READER);
		zap_unlock(zc->zc_zap, zc);
	}
	memset(zc, 0, sizeof (zap_cursor_t));
}

int
zap_cursor_retrieve(zap_cursor_t *zc, zap_attribute_t *za)
{
	int err;

	if (zc->zc_zap == NULL)
		/* zap_cursor_init failed, cursor is invalid */
		return (SET_ERROR(EIO));

	if (zc->zc_hash == -1ULL)
		return (SET_ERROR(ENOENT));

	rw_enter(&zc->zc_zap->zap_rwlock, RW_READER);

	if (!zc->zc_zap->zap_ismicro) {
		err = fzap_cursor_retrieve(zc->zc_zap, zc, za);
	} else {
		zfs_btree_index_t idx;
		mzap_ent_t mze_tofind;

		mze_tofind.mze_hash = zc->zc_hash >> 32;
		mze_tofind.mze_cd = zc->zc_cd;

		mzap_ent_t *mze = zfs_btree_find(&zc->zc_zap->zap_m.zap_tree,
		    &mze_tofind, &idx);
		if (mze == NULL) {
			mze = zfs_btree_next(&zc->zc_zap->zap_m.zap_tree,
			    &idx, &idx);
		}
		if (mze) {
			mzap_ent_phys_t *mzep = MZE_PHYS(zc->zc_zap, mze);
			ASSERT3U(mze->mze_cd, ==, mzep->mze_cd);
			za->za_normalization_conflict =
			    mzap_normalization_conflict(zc->zc_zap, NULL,
			    mze, &idx);
			za->za_integer_length = 8;
			za->za_num_integers = 1;
			za->za_first_integer = mzep->mze_value;
			(void) strlcpy(za->za_name, mzep->mze_name,
			    za->za_name_len);
			zc->zc_hash = (uint64_t)mze->mze_hash << 32;
			zc->zc_cd = mze->mze_cd;
			err = 0;
		} else {
			zc->zc_hash = -1ULL;
			err = SET_ERROR(ENOENT);
		}
	}

	rw_exit(&zc->zc_zap->zap_rwlock);
	return (err);
}

void
zap_cursor_advance(zap_cursor_t *zc)
{
	if (zc->zc_hash == -1ULL)
		return;
	zc->zc_cd++;
}

uint64_t
zap_cursor_serialize(zap_cursor_t *zc)
{
	if (zc->zc_zap == NULL || zc->zc_hash == -1ULL)
		return (-1ULL);

	ASSERT0((zc->zc_hash & zap_maxcd(zc->zc_zap)));
	ASSERT(zc->zc_cd < zap_maxcd(zc->zc_zap));

	/*
	 * We want to keep the high 32 bits of the cursor zero if we can, so
	 * that 32-bit programs can access this.  So usually use a small
	 * (28-bit) hash value so we can fit 4 bits of cd into the low 32-bits
	 * of the cursor.
	 *
	 * [ collision differentiator | zap_hashbits()-bit hash value ]
	 */
	return ((zc->zc_hash >> (64 - zap_hashbits(zc->zc_zap))) |
	    ((uint64_t)zc->zc_cd << zap_hashbits(zc->zc_zap)));
}

/* zap_get_stats */

int
zap_get_stats_by_dnode(dnode_t *dn, zap_stats_t *zs)
{
	zap_t *zap;
	int err =
	    zap_lock_by_dnode(dn, NULL, RW_READER, TRUE, FALSE, FTAG, &zap);
	if (err != 0)
		return (err);

	memset(zs, 0, sizeof (zap_stats_t));

	if (zap->zap_ismicro) {
		zs->zs_blocksize = zap->zap_dbuf->db_size;
		zs->zs_num_entries = zap->zap_m.zap_num_entries;
		zs->zs_num_blocks = 1;
	} else {
		fzap_get_stats(zap, zs);
	}
	zap_unlock(zap, FTAG);
	return (0);
}

int
zap_get_stats(objset_t *os, uint64_t zapobj, zap_stats_t *zs)
{
	dnode_t *dn;
	int err = dnode_hold(os, zapobj, FTAG, &dn);
	if (err != 0)
		return (err);
	err = zap_get_stats_by_dnode(dn, zs);
	dnode_rele(dn, FTAG);
	return (err);
}

EXPORT_SYMBOL(zap_create);
EXPORT_SYMBOL(zap_create_dnsize);
EXPORT_SYMBOL(zap_create_norm);
EXPORT_SYMBOL(zap_create_norm_dnsize);
EXPORT_SYMBOL(zap_create_flags);
EXPORT_SYMBOL(zap_create_flags_dnsize);
EXPORT_SYMBOL(zap_create_claim);
EXPORT_SYMBOL(zap_create_claim_norm);
EXPORT_SYMBOL(zap_create_claim_norm_dnsize);
EXPORT_SYMBOL(zap_create_hold);
EXPORT_SYMBOL(zap_destroy);
EXPORT_SYMBOL(zap_lookup);
EXPORT_SYMBOL(zap_lookup_by_dnode);
EXPORT_SYMBOL(zap_lookup_norm);
EXPORT_SYMBOL(zap_lookup_uint64);
EXPORT_SYMBOL(zap_lookup_length_uint64_by_dnode);
EXPORT_SYMBOL(zap_contains);
EXPORT_SYMBOL(zap_prefetch);
EXPORT_SYMBOL(zap_prefetch_uint64);
EXPORT_SYMBOL(zap_prefetch_object);
EXPORT_SYMBOL(zap_add);
EXPORT_SYMBOL(zap_add_by_dnode);
EXPORT_SYMBOL(zap_add_uint64);
EXPORT_SYMBOL(zap_add_uint64_by_dnode);
EXPORT_SYMBOL(zap_update);
EXPORT_SYMBOL(zap_update_uint64);
EXPORT_SYMBOL(zap_update_uint64_by_dnode);
EXPORT_SYMBOL(zap_length);
EXPORT_SYMBOL(zap_length_uint64);
EXPORT_SYMBOL(zap_length_uint64_by_dnode);
EXPORT_SYMBOL(zap_remove);
EXPORT_SYMBOL(zap_remove_by_dnode);
EXPORT_SYMBOL(zap_remove_norm);
EXPORT_SYMBOL(zap_remove_uint64);
EXPORT_SYMBOL(zap_remove_uint64_by_dnode);
EXPORT_SYMBOL(zap_count);
EXPORT_SYMBOL(zap_count_by_dnode);
EXPORT_SYMBOL(zap_value_search);
EXPORT_SYMBOL(zap_add_int);
EXPORT_SYMBOL(zap_remove_int);
EXPORT_SYMBOL(zap_lookup_int);
EXPORT_SYMBOL(zap_add_int_key);
EXPORT_SYMBOL(zap_lookup_int_key);
EXPORT_SYMBOL(zap_increment);
EXPORT_SYMBOL(zap_cursor_init);
EXPORT_SYMBOL(zap_cursor_fini);
EXPORT_SYMBOL(zap_cursor_retrieve);
EXPORT_SYMBOL(zap_cursor_advance);
EXPORT_SYMBOL(zap_cursor_serialize);
EXPORT_SYMBOL(zap_cursor_init_serialized);
EXPORT_SYMBOL(zap_get_stats);
