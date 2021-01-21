/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2017, Datto, Inc. All rights reserved.
 * Copyright (c) 2018 by Delphix. All rights reserved.
 */

#include <sys/dsl_crypt.h>
#include <sys/dsl_pool.h>
#include <sys/zap.h>
#include <sys/zil.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_prop.h>
#include <sys/spa_impl.h>
#include <sys/dmu_objset.h>
#include <sys/zvol.h>

/*
 * This file's primary purpose is for managing master encryption keys in
 * memory and on disk. For more info on how these keys are used, see the
 * block comment in zio_crypt.c.
 *
 * All master keys are stored encrypted on disk in the form of the DSL
 * Crypto Key ZAP object. The binary key data in this object is always
 * randomly generated and is encrypted with the user's wrapping key. This
 * layer of indirection allows the user to change their key without
 * needing to re-encrypt the entire dataset. The ZAP also holds on to the
 * (non-encrypted) encryption algorithm identifier, IV, and MAC needed to
 * safely decrypt the master key. For more info on the user's key see the
 * block comment in libzfs_crypto.c
 *
 * In-memory encryption keys are managed through the spa_keystore. The
 * keystore consists of 3 AVL trees, which are as follows:
 *
 * The Wrapping Key Tree:
 * The wrapping key (wkey) tree stores the user's keys that are fed into the
 * kernel through 'zfs load-key' and related commands. Datasets inherit their
 * parent's wkey by default, so these structures are refcounted. The wrapping
 * keys remain in memory until they are explicitly unloaded (with
 * "zfs unload-key"). Unloading is only possible when no datasets are using
 * them (refcount=0).
 *
 * The DSL Crypto Key Tree:
 * The DSL Crypto Keys (DCK) are the in-memory representation of decrypted
 * master keys. They are used by the functions in zio_crypt.c to perform
 * encryption, decryption, and authentication. Snapshots and clones of a given
 * dataset will share a DSL Crypto Key, so they are also refcounted. Once the
 * refcount on a key hits zero, it is immediately zeroed out and freed.
 *
 * The Crypto Key Mapping Tree:
 * The zio layer needs to lookup master keys by their dataset object id. Since
 * the DSL Crypto Keys can belong to multiple datasets, we maintain a tree of
 * dsl_key_mapping_t's which essentially just map the dataset object id to its
 * appropriate DSL Crypto Key. The management for creating and destroying these
 * mappings hooks into the code for owning and disowning datasets. Usually,
 * there will only be one active dataset owner, but there are times
 * (particularly during dataset creation and destruction) when this may not be
 * true or the dataset may not be initialized enough to own. As a result, this
 * object is also refcounted.
 */

/*
 * This tunable allows datasets to be raw received even if the stream does
 * not include IVset guids or if the guids don't match. This is used as part
 * of the resolution for ZPOOL_ERRATA_ZOL_8308_ENCRYPTION.
 */
int zfs_disable_ivset_guid_check = 0;

static void
dsl_wrapping_key_hold(dsl_wrapping_key_t *wkey, void *tag)
{
	(void) zfs_refcount_add(&wkey->wk_refcnt, tag);
}

static void
dsl_wrapping_key_rele(dsl_wrapping_key_t *wkey, void *tag)
{
	(void) zfs_refcount_remove(&wkey->wk_refcnt, tag);
}

static void
dsl_wrapping_key_free(dsl_wrapping_key_t *wkey)
{
	ASSERT0(zfs_refcount_count(&wkey->wk_refcnt));

	if (wkey->wk_key.ck_data) {
		bzero(wkey->wk_key.ck_data,
		    CRYPTO_BITS2BYTES(wkey->wk_key.ck_length));
		kmem_free(wkey->wk_key.ck_data,
		    CRYPTO_BITS2BYTES(wkey->wk_key.ck_length));
	}

	zfs_refcount_destroy(&wkey->wk_refcnt);
	kmem_free(wkey, sizeof (dsl_wrapping_key_t));
}

static void
dsl_wrapping_key_create(uint8_t *wkeydata, zfs_keyformat_t keyformat,
    uint64_t salt, uint64_t iters, dsl_wrapping_key_t **wkey_out)
{
	dsl_wrapping_key_t *wkey;

	/* allocate the wrapping key */
	wkey = kmem_alloc(sizeof (dsl_wrapping_key_t), KM_SLEEP);

	/* allocate and initialize the underlying crypto key */
	wkey->wk_key.ck_data = kmem_alloc(WRAPPING_KEY_LEN, KM_SLEEP);

	wkey->wk_key.ck_format = CRYPTO_KEY_RAW;
	wkey->wk_key.ck_length = CRYPTO_BYTES2BITS(WRAPPING_KEY_LEN);
	bcopy(wkeydata, wkey->wk_key.ck_data, WRAPPING_KEY_LEN);

	/* initialize the rest of the struct */
	zfs_refcount_create(&wkey->wk_refcnt);
	wkey->wk_keyformat = keyformat;
	wkey->wk_salt = salt;
	wkey->wk_iters = iters;

	*wkey_out = wkey;
}

int
dsl_crypto_params_create_nvlist(dcp_cmd_t cmd, nvlist_t *props,
    nvlist_t *crypto_args, dsl_crypto_params_t **dcp_out)
{
	int ret;
	uint64_t crypt = ZIO_CRYPT_INHERIT;
	uint64_t keyformat = ZFS_KEYFORMAT_NONE;
	uint64_t salt = 0, iters = 0;
	dsl_crypto_params_t *dcp = NULL;
	dsl_wrapping_key_t *wkey = NULL;
	uint8_t *wkeydata = NULL;
	uint_t wkeydata_len = 0;
	char *keylocation = NULL;

	dcp = kmem_zalloc(sizeof (dsl_crypto_params_t), KM_SLEEP);
	dcp->cp_cmd = cmd;

	/* get relevant arguments from the nvlists */
	if (props != NULL) {
		(void) nvlist_lookup_uint64(props,
		    zfs_prop_to_name(ZFS_PROP_ENCRYPTION), &crypt);
		(void) nvlist_lookup_uint64(props,
		    zfs_prop_to_name(ZFS_PROP_KEYFORMAT), &keyformat);
		(void) nvlist_lookup_string(props,
		    zfs_prop_to_name(ZFS_PROP_KEYLOCATION), &keylocation);
		(void) nvlist_lookup_uint64(props,
		    zfs_prop_to_name(ZFS_PROP_PBKDF2_SALT), &salt);
		(void) nvlist_lookup_uint64(props,
		    zfs_prop_to_name(ZFS_PROP_PBKDF2_ITERS), &iters);

		dcp->cp_crypt = crypt;
	}

	if (crypto_args != NULL) {
		(void) nvlist_lookup_uint8_array(crypto_args, "wkeydata",
		    &wkeydata, &wkeydata_len);
	}

	/* check for valid command */
	if (dcp->cp_cmd >= DCP_CMD_MAX) {
		ret = SET_ERROR(EINVAL);
		goto error;
	} else {
		dcp->cp_cmd = cmd;
	}

	/* check for valid crypt */
	if (dcp->cp_crypt >= ZIO_CRYPT_FUNCTIONS) {
		ret = SET_ERROR(EINVAL);
		goto error;
	} else {
		dcp->cp_crypt = crypt;
	}

	/* check for valid keyformat */
	if (keyformat >= ZFS_KEYFORMAT_FORMATS) {
		ret = SET_ERROR(EINVAL);
		goto error;
	}

	/* check for a valid keylocation (of any kind) and copy it in */
	if (keylocation != NULL) {
		if (!zfs_prop_valid_keylocation(keylocation, B_FALSE)) {
			ret = SET_ERROR(EINVAL);
			goto error;
		}

		dcp->cp_keylocation = spa_strdup(keylocation);
	}

	/* check wrapping key length, if given */
	if (wkeydata != NULL && wkeydata_len != WRAPPING_KEY_LEN) {
		ret = SET_ERROR(EINVAL);
		goto error;
	}

	/* if the user asked for the default crypt, determine that now */
	if (dcp->cp_crypt == ZIO_CRYPT_ON)
		dcp->cp_crypt = ZIO_CRYPT_ON_VALUE;

	/* create the wrapping key from the raw data */
	if (wkeydata != NULL) {
		/* create the wrapping key with the verified parameters */
		dsl_wrapping_key_create(wkeydata, keyformat, salt,
		    iters, &wkey);
		dcp->cp_wkey = wkey;
	}

	/*
	 * Remove the encryption properties from the nvlist since they are not
	 * maintained through the DSL.
	 */
	(void) nvlist_remove_all(props, zfs_prop_to_name(ZFS_PROP_ENCRYPTION));
	(void) nvlist_remove_all(props, zfs_prop_to_name(ZFS_PROP_KEYFORMAT));
	(void) nvlist_remove_all(props, zfs_prop_to_name(ZFS_PROP_PBKDF2_SALT));
	(void) nvlist_remove_all(props,
	    zfs_prop_to_name(ZFS_PROP_PBKDF2_ITERS));

	*dcp_out = dcp;

	return (0);

error:
	kmem_free(dcp, sizeof (dsl_crypto_params_t));
	*dcp_out = NULL;
	return (ret);
}

void
dsl_crypto_params_free(dsl_crypto_params_t *dcp, boolean_t unload)
{
	if (dcp == NULL)
		return;

	if (dcp->cp_keylocation != NULL)
		spa_strfree(dcp->cp_keylocation);
	if (unload && dcp->cp_wkey != NULL)
		dsl_wrapping_key_free(dcp->cp_wkey);

	kmem_free(dcp, sizeof (dsl_crypto_params_t));
}

static int
spa_crypto_key_compare(const void *a, const void *b)
{
	const dsl_crypto_key_t *dcka = a;
	const dsl_crypto_key_t *dckb = b;

	if (dcka->dck_obj < dckb->dck_obj)
		return (-1);
	if (dcka->dck_obj > dckb->dck_obj)
		return (1);
	return (0);
}

static int
spa_key_mapping_compare(const void *a, const void *b)
{
	const dsl_key_mapping_t *kma = a;
	const dsl_key_mapping_t *kmb = b;

	if (kma->km_dsobj < kmb->km_dsobj)
		return (-1);
	if (kma->km_dsobj > kmb->km_dsobj)
		return (1);
	return (0);
}

static int
spa_wkey_compare(const void *a, const void *b)
{
	const dsl_wrapping_key_t *wka = a;
	const dsl_wrapping_key_t *wkb = b;

	if (wka->wk_ddobj < wkb->wk_ddobj)
		return (-1);
	if (wka->wk_ddobj > wkb->wk_ddobj)
		return (1);
	return (0);
}

void
spa_keystore_init(spa_keystore_t *sk)
{
	rw_init(&sk->sk_dk_lock, NULL, RW_DEFAULT, NULL);
	rw_init(&sk->sk_km_lock, NULL, RW_DEFAULT, NULL);
	rw_init(&sk->sk_wkeys_lock, NULL, RW_DEFAULT, NULL);
	avl_create(&sk->sk_dsl_keys, spa_crypto_key_compare,
	    sizeof (dsl_crypto_key_t),
	    offsetof(dsl_crypto_key_t, dck_avl_link));
	avl_create(&sk->sk_key_mappings, spa_key_mapping_compare,
	    sizeof (dsl_key_mapping_t),
	    offsetof(dsl_key_mapping_t, km_avl_link));
	avl_create(&sk->sk_wkeys, spa_wkey_compare, sizeof (dsl_wrapping_key_t),
	    offsetof(dsl_wrapping_key_t, wk_avl_link));
}

void
spa_keystore_fini(spa_keystore_t *sk)
{
	dsl_wrapping_key_t *wkey;
	void *cookie = NULL;

	ASSERT(avl_is_empty(&sk->sk_dsl_keys));
	ASSERT(avl_is_empty(&sk->sk_key_mappings));

	while ((wkey = avl_destroy_nodes(&sk->sk_wkeys, &cookie)) != NULL)
		dsl_wrapping_key_free(wkey);

	avl_destroy(&sk->sk_wkeys);
	avl_destroy(&sk->sk_key_mappings);
	avl_destroy(&sk->sk_dsl_keys);
	rw_destroy(&sk->sk_wkeys_lock);
	rw_destroy(&sk->sk_km_lock);
	rw_destroy(&sk->sk_dk_lock);
}

static int
dsl_dir_get_encryption_root_ddobj(dsl_dir_t *dd, uint64_t *rddobj)
{
	if (dd->dd_crypto_obj == 0)
		return (SET_ERROR(ENOENT));

	return (zap_lookup(dd->dd_pool->dp_meta_objset, dd->dd_crypto_obj,
	    DSL_CRYPTO_KEY_ROOT_DDOBJ, 8, 1, rddobj));
}

static int
dsl_dir_get_encryption_version(dsl_dir_t *dd, uint64_t *version)
{
	*version = 0;

	if (dd->dd_crypto_obj == 0)
		return (SET_ERROR(ENOENT));

	/* version 0 is implied by ENOENT */
	(void) zap_lookup(dd->dd_pool->dp_meta_objset, dd->dd_crypto_obj,
	    DSL_CRYPTO_KEY_VERSION, 8, 1, version);

	return (0);
}

boolean_t
dsl_dir_incompatible_encryption_version(dsl_dir_t *dd)
{
	int ret;
	uint64_t version = 0;

	ret = dsl_dir_get_encryption_version(dd, &version);
	if (ret != 0)
		return (B_FALSE);

	return (version != ZIO_CRYPT_KEY_CURRENT_VERSION);
}

static int
spa_keystore_wkey_hold_ddobj_impl(spa_t *spa, uint64_t ddobj,
    void *tag, dsl_wrapping_key_t **wkey_out)
{
	int ret;
	dsl_wrapping_key_t search_wkey;
	dsl_wrapping_key_t *found_wkey;

	ASSERT(RW_LOCK_HELD(&spa->spa_keystore.sk_wkeys_lock));

	/* init the search wrapping key */
	search_wkey.wk_ddobj = ddobj;

	/* lookup the wrapping key */
	found_wkey = avl_find(&spa->spa_keystore.sk_wkeys, &search_wkey, NULL);
	if (!found_wkey) {
		ret = SET_ERROR(ENOENT);
		goto error;
	}

	/* increment the refcount */
	dsl_wrapping_key_hold(found_wkey, tag);

	*wkey_out = found_wkey;
	return (0);

error:
	*wkey_out = NULL;
	return (ret);
}

static int
spa_keystore_wkey_hold_dd(spa_t *spa, dsl_dir_t *dd, void *tag,
    dsl_wrapping_key_t **wkey_out)
{
	int ret;
	dsl_wrapping_key_t *wkey;
	uint64_t rddobj;
	boolean_t locked = B_FALSE;

	if (!RW_WRITE_HELD(&spa->spa_keystore.sk_wkeys_lock)) {
		rw_enter(&spa->spa_keystore.sk_wkeys_lock, RW_READER);
		locked = B_TRUE;
	}

	/* get the ddobj that the keylocation property was inherited from */
	ret = dsl_dir_get_encryption_root_ddobj(dd, &rddobj);
	if (ret != 0)
		goto error;

	/* lookup the wkey in the avl tree */
	ret = spa_keystore_wkey_hold_ddobj_impl(spa, rddobj, tag, &wkey);
	if (ret != 0)
		goto error;

	/* unlock the wkey tree if we locked it */
	if (locked)
		rw_exit(&spa->spa_keystore.sk_wkeys_lock);

	*wkey_out = wkey;
	return (0);

error:
	if (locked)
		rw_exit(&spa->spa_keystore.sk_wkeys_lock);

	*wkey_out = NULL;
	return (ret);
}

int
dsl_crypto_can_set_keylocation(const char *dsname, const char *keylocation)
{
	int ret = 0;
	dsl_dir_t *dd = NULL;
	dsl_pool_t *dp = NULL;
	uint64_t rddobj;

	/* hold the dsl dir */
	ret = dsl_pool_hold(dsname, FTAG, &dp);
	if (ret != 0)
		goto out;

	ret = dsl_dir_hold(dp, dsname, FTAG, &dd, NULL);
	if (ret != 0) {
		dd = NULL;
		goto out;
	}

	/* if dd is not encrypted, the value may only be "none" */
	if (dd->dd_crypto_obj == 0) {
		if (strcmp(keylocation, "none") != 0) {
			ret = SET_ERROR(EACCES);
			goto out;
		}

		ret = 0;
		goto out;
	}

	/* check for a valid keylocation for encrypted datasets */
	if (!zfs_prop_valid_keylocation(keylocation, B_TRUE)) {
		ret = SET_ERROR(EINVAL);
		goto out;
	}

	/* check that this is an encryption root */
	ret = dsl_dir_get_encryption_root_ddobj(dd, &rddobj);
	if (ret != 0)
		goto out;

	if (rddobj != dd->dd_object) {
		ret = SET_ERROR(EACCES);
		goto out;
	}

	dsl_dir_rele(dd, FTAG);
	dsl_pool_rele(dp, FTAG);

	return (0);

out:
	if (dd != NULL)
		dsl_dir_rele(dd, FTAG);
	if (dp != NULL)
		dsl_pool_rele(dp, FTAG);

	return (ret);
}

static void
dsl_crypto_key_free(dsl_crypto_key_t *dck)
{
	ASSERT(zfs_refcount_count(&dck->dck_holds) == 0);

	/* destroy the zio_crypt_key_t */
	zio_crypt_key_destroy(&dck->dck_key);

	/* free the refcount, wrapping key, and lock */
	zfs_refcount_destroy(&dck->dck_holds);
	if (dck->dck_wkey)
		dsl_wrapping_key_rele(dck->dck_wkey, dck);

	/* free the key */
	kmem_free(dck, sizeof (dsl_crypto_key_t));
}

static void
dsl_crypto_key_rele(dsl_crypto_key_t *dck, void *tag)
{
	if (zfs_refcount_remove(&dck->dck_holds, tag) == 0)
		dsl_crypto_key_free(dck);
}

static int
dsl_crypto_key_open(objset_t *mos, dsl_wrapping_key_t *wkey,
    uint64_t dckobj, void *tag, dsl_crypto_key_t **dck_out)
{
	int ret;
	uint64_t crypt = 0, guid = 0, version = 0;
	uint8_t raw_keydata[MASTER_KEY_MAX_LEN];
	uint8_t raw_hmac_keydata[SHA512_HMAC_KEYLEN];
	uint8_t iv[WRAPPING_IV_LEN];
	uint8_t mac[WRAPPING_MAC_LEN];
	dsl_crypto_key_t *dck;

	/* allocate and initialize the key */
	dck = kmem_zalloc(sizeof (dsl_crypto_key_t), KM_SLEEP);

	/* fetch all of the values we need from the ZAP */
	ret = zap_lookup(mos, dckobj, DSL_CRYPTO_KEY_CRYPTO_SUITE, 8, 1,
	    &crypt);
	if (ret != 0)
		goto error;

	ret = zap_lookup(mos, dckobj, DSL_CRYPTO_KEY_GUID, 8, 1, &guid);
	if (ret != 0)
		goto error;

	ret = zap_lookup(mos, dckobj, DSL_CRYPTO_KEY_MASTER_KEY, 1,
	    MASTER_KEY_MAX_LEN, raw_keydata);
	if (ret != 0)
		goto error;

	ret = zap_lookup(mos, dckobj, DSL_CRYPTO_KEY_HMAC_KEY, 1,
	    SHA512_HMAC_KEYLEN, raw_hmac_keydata);
	if (ret != 0)
		goto error;

	ret = zap_lookup(mos, dckobj, DSL_CRYPTO_KEY_IV, 1, WRAPPING_IV_LEN,
	    iv);
	if (ret != 0)
		goto error;

	ret = zap_lookup(mos, dckobj, DSL_CRYPTO_KEY_MAC, 1, WRAPPING_MAC_LEN,
	    mac);
	if (ret != 0)
		goto error;

	/* the initial on-disk format for encryption did not have a version */
	(void) zap_lookup(mos, dckobj, DSL_CRYPTO_KEY_VERSION, 8, 1, &version);

	/*
	 * Unwrap the keys. If there is an error return EACCES to indicate
	 * an authentication failure.
	 */
	ret = zio_crypt_key_unwrap(&wkey->wk_key, crypt, version, guid,
	    raw_keydata, raw_hmac_keydata, iv, mac, &dck->dck_key);
	if (ret != 0) {
		ret = SET_ERROR(EACCES);
		goto error;
	}

	/* finish initializing the dsl_crypto_key_t */
	zfs_refcount_create(&dck->dck_holds);
	dsl_wrapping_key_hold(wkey, dck);
	dck->dck_wkey = wkey;
	dck->dck_obj = dckobj;
	zfs_refcount_add(&dck->dck_holds, tag);

	*dck_out = dck;
	return (0);

error:
	if (dck != NULL) {
		bzero(dck, sizeof (dsl_crypto_key_t));
		kmem_free(dck, sizeof (dsl_crypto_key_t));
	}

	*dck_out = NULL;
	return (ret);
}

static int
spa_keystore_dsl_key_hold_impl(spa_t *spa, uint64_t dckobj, void *tag,
    dsl_crypto_key_t **dck_out)
{
	int ret;
	dsl_crypto_key_t search_dck;
	dsl_crypto_key_t *found_dck;

	ASSERT(RW_LOCK_HELD(&spa->spa_keystore.sk_dk_lock));

	/* init the search key */
	search_dck.dck_obj = dckobj;

	/* find the matching key in the keystore */
	found_dck = avl_find(&spa->spa_keystore.sk_dsl_keys, &search_dck, NULL);
	if (!found_dck) {
		ret = SET_ERROR(ENOENT);
		goto error;
	}

	/* increment the refcount */
	zfs_refcount_add(&found_dck->dck_holds, tag);

	*dck_out = found_dck;
	return (0);

error:
	*dck_out = NULL;
	return (ret);
}

static int
spa_keystore_dsl_key_hold_dd(spa_t *spa, dsl_dir_t *dd, void *tag,
    dsl_crypto_key_t **dck_out)
{
	int ret;
	avl_index_t where;
	dsl_crypto_key_t *dck_io = NULL, *dck_ks = NULL;
	dsl_wrapping_key_t *wkey = NULL;
	uint64_t dckobj = dd->dd_crypto_obj;

	/* Lookup the key in the tree of currently loaded keys */
	rw_enter(&spa->spa_keystore.sk_dk_lock, RW_READER);
	ret = spa_keystore_dsl_key_hold_impl(spa, dckobj, tag, &dck_ks);
	rw_exit(&spa->spa_keystore.sk_dk_lock);
	if (ret == 0) {
		*dck_out = dck_ks;
		return (0);
	}

	/* Lookup the wrapping key from the keystore */
	ret = spa_keystore_wkey_hold_dd(spa, dd, FTAG, &wkey);
	if (ret != 0) {
		*dck_out = NULL;
		return (SET_ERROR(EACCES));
	}

	/* Read the key from disk */
	ret = dsl_crypto_key_open(spa->spa_meta_objset, wkey, dckobj,
	    tag, &dck_io);
	if (ret != 0) {
		dsl_wrapping_key_rele(wkey, FTAG);
		*dck_out = NULL;
		return (ret);
	}

	/*
	 * Add the key to the keystore.  It may already exist if it was
	 * added while performing the read from disk.  In this case discard
	 * it and return the key from the keystore.
	 */
	rw_enter(&spa->spa_keystore.sk_dk_lock, RW_WRITER);
	ret = spa_keystore_dsl_key_hold_impl(spa, dckobj, tag, &dck_ks);
	if (ret != 0) {
		avl_find(&spa->spa_keystore.sk_dsl_keys, dck_io, &where);
		avl_insert(&spa->spa_keystore.sk_dsl_keys, dck_io, where);
		*dck_out = dck_io;
	} else {
		dsl_crypto_key_free(dck_io);
		*dck_out = dck_ks;
	}

	/* Release the wrapping key (the dsl key now has a reference to it) */
	dsl_wrapping_key_rele(wkey, FTAG);
	rw_exit(&spa->spa_keystore.sk_dk_lock);

	return (0);
}

void
spa_keystore_dsl_key_rele(spa_t *spa, dsl_crypto_key_t *dck, void *tag)
{
	rw_enter(&spa->spa_keystore.sk_dk_lock, RW_WRITER);

	if (zfs_refcount_remove(&dck->dck_holds, tag) == 0) {
		avl_remove(&spa->spa_keystore.sk_dsl_keys, dck);
		dsl_crypto_key_free(dck);
	}

	rw_exit(&spa->spa_keystore.sk_dk_lock);
}

int
spa_keystore_load_wkey_impl(spa_t *spa, dsl_wrapping_key_t *wkey)
{
	int ret;
	avl_index_t where;
	dsl_wrapping_key_t *found_wkey;

	rw_enter(&spa->spa_keystore.sk_wkeys_lock, RW_WRITER);

	/* insert the wrapping key into the keystore */
	found_wkey = avl_find(&spa->spa_keystore.sk_wkeys, wkey, &where);
	if (found_wkey != NULL) {
		ret = SET_ERROR(EEXIST);
		goto error_unlock;
	}
	avl_insert(&spa->spa_keystore.sk_wkeys, wkey, where);

	rw_exit(&spa->spa_keystore.sk_wkeys_lock);

	return (0);

error_unlock:
	rw_exit(&spa->spa_keystore.sk_wkeys_lock);
	return (ret);
}

int
spa_keystore_load_wkey(const char *dsname, dsl_crypto_params_t *dcp,
    boolean_t noop)
{
	int ret;
	dsl_dir_t *dd = NULL;
	dsl_crypto_key_t *dck = NULL;
	dsl_wrapping_key_t *wkey = dcp->cp_wkey;
	dsl_pool_t *dp = NULL;
	uint64_t rddobj, keyformat, salt, iters;

	/*
	 * We don't validate the wrapping key's keyformat, salt, or iters
	 * since they will never be needed after the DCK has been wrapped.
	 */
	if (dcp->cp_wkey == NULL ||
	    dcp->cp_cmd != DCP_CMD_NONE ||
	    dcp->cp_crypt != ZIO_CRYPT_INHERIT ||
	    dcp->cp_keylocation != NULL)
		return (SET_ERROR(EINVAL));

	ret = dsl_pool_hold(dsname, FTAG, &dp);
	if (ret != 0)
		goto error;

	if (!spa_feature_is_enabled(dp->dp_spa, SPA_FEATURE_ENCRYPTION)) {
		ret = SET_ERROR(ENOTSUP);
		goto error;
	}

	/* hold the dsl dir */
	ret = dsl_dir_hold(dp, dsname, FTAG, &dd, NULL);
	if (ret != 0) {
		dd = NULL;
		goto error;
	}

	/* confirm that dd is the encryption root */
	ret = dsl_dir_get_encryption_root_ddobj(dd, &rddobj);
	if (ret != 0 || rddobj != dd->dd_object) {
		ret = SET_ERROR(EINVAL);
		goto error;
	}

	/* initialize the wkey's ddobj */
	wkey->wk_ddobj = dd->dd_object;

	/* verify that the wkey is correct by opening its dsl key */
	ret = dsl_crypto_key_open(dp->dp_meta_objset, wkey,
	    dd->dd_crypto_obj, FTAG, &dck);
	if (ret != 0)
		goto error;

	/* initialize the wkey encryption parameters from the DSL Crypto Key */
	ret = zap_lookup(dp->dp_meta_objset, dd->dd_crypto_obj,
	    zfs_prop_to_name(ZFS_PROP_KEYFORMAT), 8, 1, &keyformat);
	if (ret != 0)
		goto error;

	ret = zap_lookup(dp->dp_meta_objset, dd->dd_crypto_obj,
	    zfs_prop_to_name(ZFS_PROP_PBKDF2_SALT), 8, 1, &salt);
	if (ret != 0)
		goto error;

	ret = zap_lookup(dp->dp_meta_objset, dd->dd_crypto_obj,
	    zfs_prop_to_name(ZFS_PROP_PBKDF2_ITERS), 8, 1, &iters);
	if (ret != 0)
		goto error;

	ASSERT3U(keyformat, <, ZFS_KEYFORMAT_FORMATS);
	ASSERT3U(keyformat, !=, ZFS_KEYFORMAT_NONE);
	IMPLY(keyformat == ZFS_KEYFORMAT_PASSPHRASE, iters != 0);
	IMPLY(keyformat == ZFS_KEYFORMAT_PASSPHRASE, salt != 0);
	IMPLY(keyformat != ZFS_KEYFORMAT_PASSPHRASE, iters == 0);
	IMPLY(keyformat != ZFS_KEYFORMAT_PASSPHRASE, salt == 0);

	wkey->wk_keyformat = keyformat;
	wkey->wk_salt = salt;
	wkey->wk_iters = iters;

	/*
	 * At this point we have verified the wkey and confirmed that it can
	 * be used to decrypt a DSL Crypto Key. We can simply cleanup and
	 * return if this is all the user wanted to do.
	 */
	if (noop)
		goto error;

	/* insert the wrapping key into the keystore */
	ret = spa_keystore_load_wkey_impl(dp->dp_spa, wkey);
	if (ret != 0)
		goto error;

	dsl_crypto_key_rele(dck, FTAG);
	dsl_dir_rele(dd, FTAG);
	dsl_pool_rele(dp, FTAG);

	/* create any zvols under this ds */
	zvol_create_minors_recursive(dsname);

	return (0);

error:
	if (dck != NULL)
		dsl_crypto_key_rele(dck, FTAG);
	if (dd != NULL)
		dsl_dir_rele(dd, FTAG);
	if (dp != NULL)
		dsl_pool_rele(dp, FTAG);

	return (ret);
}

int
spa_keystore_unload_wkey_impl(spa_t *spa, uint64_t ddobj)
{
	int ret;
	dsl_wrapping_key_t search_wkey;
	dsl_wrapping_key_t *found_wkey;

	/* init the search wrapping key */
	search_wkey.wk_ddobj = ddobj;

	rw_enter(&spa->spa_keystore.sk_wkeys_lock, RW_WRITER);

	/* remove the wrapping key from the keystore */
	found_wkey = avl_find(&spa->spa_keystore.sk_wkeys,
	    &search_wkey, NULL);
	if (!found_wkey) {
		ret = SET_ERROR(EACCES);
		goto error_unlock;
	} else if (zfs_refcount_count(&found_wkey->wk_refcnt) != 0) {
		ret = SET_ERROR(EBUSY);
		goto error_unlock;
	}
	avl_remove(&spa->spa_keystore.sk_wkeys, found_wkey);

	rw_exit(&spa->spa_keystore.sk_wkeys_lock);

	/* free the wrapping key */
	dsl_wrapping_key_free(found_wkey);

	return (0);

error_unlock:
	rw_exit(&spa->spa_keystore.sk_wkeys_lock);
	return (ret);
}

int
spa_keystore_unload_wkey(const char *dsname)
{
	int ret = 0;
	dsl_dir_t *dd = NULL;
	dsl_pool_t *dp = NULL;
	spa_t *spa = NULL;

	ret = spa_open(dsname, &spa, FTAG);
	if (ret != 0)
		return (ret);

	/*
	 * Wait for any outstanding txg IO to complete, releasing any
	 * remaining references on the wkey.
	 */
	if (spa_mode(spa) != SPA_MODE_READ)
		txg_wait_synced(spa->spa_dsl_pool, 0);

	spa_close(spa, FTAG);

	/* hold the dsl dir */
	ret = dsl_pool_hold(dsname, FTAG, &dp);
	if (ret != 0)
		goto error;

	if (!spa_feature_is_enabled(dp->dp_spa, SPA_FEATURE_ENCRYPTION)) {
		ret = (SET_ERROR(ENOTSUP));
		goto error;
	}

	ret = dsl_dir_hold(dp, dsname, FTAG, &dd, NULL);
	if (ret != 0) {
		dd = NULL;
		goto error;
	}

	/* unload the wkey */
	ret = spa_keystore_unload_wkey_impl(dp->dp_spa, dd->dd_object);
	if (ret != 0)
		goto error;

	dsl_dir_rele(dd, FTAG);
	dsl_pool_rele(dp, FTAG);

	/* remove any zvols under this ds */
	zvol_remove_minors(dp->dp_spa, dsname, B_TRUE);

	return (0);

error:
	if (dd != NULL)
		dsl_dir_rele(dd, FTAG);
	if (dp != NULL)
		dsl_pool_rele(dp, FTAG);

	return (ret);
}

void
key_mapping_add_ref(dsl_key_mapping_t *km, void *tag)
{
	ASSERT3U(zfs_refcount_count(&km->km_refcnt), >=, 1);
	zfs_refcount_add(&km->km_refcnt, tag);
}

/*
 * The locking here is a little tricky to ensure we don't cause unnecessary
 * performance problems. We want to release a key mapping whenever someone
 * decrements the refcount to 0, but freeing the mapping requires removing
 * it from the spa_keystore, which requires holding sk_km_lock as a writer.
 * Most of the time we don't want to hold this lock as a writer, since the
 * same lock is held as a reader for each IO that needs to encrypt / decrypt
 * data for any dataset and in practice we will only actually free the
 * mapping after unmounting a dataset.
 */
void
key_mapping_rele(spa_t *spa, dsl_key_mapping_t *km, void *tag)
{
	ASSERT3U(zfs_refcount_count(&km->km_refcnt), >=, 1);

	if (zfs_refcount_remove(&km->km_refcnt, tag) != 0)
		return;

	/*
	 * We think we are going to need to free the mapping. Add a
	 * reference to prevent most other releasers from thinking
	 * this might be their responsibility. This is inherently
	 * racy, so we will confirm that we are legitimately the
	 * last holder once we have the sk_km_lock as a writer.
	 */
	zfs_refcount_add(&km->km_refcnt, FTAG);

	rw_enter(&spa->spa_keystore.sk_km_lock, RW_WRITER);
	if (zfs_refcount_remove(&km->km_refcnt, FTAG) != 0) {
		rw_exit(&spa->spa_keystore.sk_km_lock);
		return;
	}

	avl_remove(&spa->spa_keystore.sk_key_mappings, km);
	rw_exit(&spa->spa_keystore.sk_km_lock);

	spa_keystore_dsl_key_rele(spa, km->km_key, km);
	zfs_refcount_destroy(&km->km_refcnt);
	kmem_free(km, sizeof (dsl_key_mapping_t));
}

int
spa_keystore_create_mapping(spa_t *spa, dsl_dataset_t *ds, void *tag,
    dsl_key_mapping_t **km_out)
{
	int ret;
	avl_index_t where;
	dsl_key_mapping_t *km, *found_km;
	boolean_t should_free = B_FALSE;

	/* Allocate and initialize the mapping */
	km = kmem_zalloc(sizeof (dsl_key_mapping_t), KM_SLEEP);
	zfs_refcount_create(&km->km_refcnt);

	ret = spa_keystore_dsl_key_hold_dd(spa, ds->ds_dir, km, &km->km_key);
	if (ret != 0) {
		zfs_refcount_destroy(&km->km_refcnt);
		kmem_free(km, sizeof (dsl_key_mapping_t));

		if (km_out != NULL)
			*km_out = NULL;
		return (ret);
	}

	km->km_dsobj = ds->ds_object;

	rw_enter(&spa->spa_keystore.sk_km_lock, RW_WRITER);

	/*
	 * If a mapping already exists, simply increment its refcount and
	 * cleanup the one we made. We want to allocate / free outside of
	 * the lock because this lock is also used by the zio layer to lookup
	 * key mappings. Otherwise, use the one we created. Normally, there will
	 * only be one active reference at a time (the objset owner), but there
	 * are times when there could be multiple async users.
	 */
	found_km = avl_find(&spa->spa_keystore.sk_key_mappings, km, &where);
	if (found_km != NULL) {
		should_free = B_TRUE;
		zfs_refcount_add(&found_km->km_refcnt, tag);
		if (km_out != NULL)
			*km_out = found_km;
	} else {
		zfs_refcount_add(&km->km_refcnt, tag);
		avl_insert(&spa->spa_keystore.sk_key_mappings, km, where);
		if (km_out != NULL)
			*km_out = km;
	}

	rw_exit(&spa->spa_keystore.sk_km_lock);

	if (should_free) {
		spa_keystore_dsl_key_rele(spa, km->km_key, km);
		zfs_refcount_destroy(&km->km_refcnt);
		kmem_free(km, sizeof (dsl_key_mapping_t));
	}

	return (0);
}

int
spa_keystore_remove_mapping(spa_t *spa, uint64_t dsobj, void *tag)
{
	int ret;
	dsl_key_mapping_t search_km;
	dsl_key_mapping_t *found_km;

	/* init the search key mapping */
	search_km.km_dsobj = dsobj;

	rw_enter(&spa->spa_keystore.sk_km_lock, RW_READER);

	/* find the matching mapping */
	found_km = avl_find(&spa->spa_keystore.sk_key_mappings,
	    &search_km, NULL);
	if (found_km == NULL) {
		ret = SET_ERROR(ENOENT);
		goto error_unlock;
	}

	rw_exit(&spa->spa_keystore.sk_km_lock);

	key_mapping_rele(spa, found_km, tag);

	return (0);

error_unlock:
	rw_exit(&spa->spa_keystore.sk_km_lock);
	return (ret);
}

/*
 * This function is primarily used by the zio and arc layer to lookup
 * DSL Crypto Keys for encryption. Callers must release the key with
 * spa_keystore_dsl_key_rele(). The function may also be called with
 * dck_out == NULL and tag == NULL to simply check that a key exists
 * without getting a reference to it.
 */
int
spa_keystore_lookup_key(spa_t *spa, uint64_t dsobj, void *tag,
    dsl_crypto_key_t **dck_out)
{
	int ret;
	dsl_key_mapping_t search_km;
	dsl_key_mapping_t *found_km;

	ASSERT((tag != NULL && dck_out != NULL) ||
	    (tag == NULL && dck_out == NULL));

	/* init the search key mapping */
	search_km.km_dsobj = dsobj;

	rw_enter(&spa->spa_keystore.sk_km_lock, RW_READER);

	/* remove the mapping from the tree */
	found_km = avl_find(&spa->spa_keystore.sk_key_mappings, &search_km,
	    NULL);
	if (found_km == NULL) {
		ret = SET_ERROR(ENOENT);
		goto error_unlock;
	}

	if (found_km && tag)
		zfs_refcount_add(&found_km->km_key->dck_holds, tag);

	rw_exit(&spa->spa_keystore.sk_km_lock);

	if (dck_out != NULL)
		*dck_out = found_km->km_key;
	return (0);

error_unlock:
	rw_exit(&spa->spa_keystore.sk_km_lock);

	if (dck_out != NULL)
		*dck_out = NULL;
	return (ret);
}

static int
dmu_objset_check_wkey_loaded(dsl_dir_t *dd)
{
	int ret;
	dsl_wrapping_key_t *wkey = NULL;

	ret = spa_keystore_wkey_hold_dd(dd->dd_pool->dp_spa, dd, FTAG,
	    &wkey);
	if (ret != 0)
		return (SET_ERROR(EACCES));

	dsl_wrapping_key_rele(wkey, FTAG);

	return (0);
}

static zfs_keystatus_t
dsl_dataset_get_keystatus(dsl_dir_t *dd)
{
	/* check if this dd has a has a dsl key */
	if (dd->dd_crypto_obj == 0)
		return (ZFS_KEYSTATUS_NONE);

	return (dmu_objset_check_wkey_loaded(dd) == 0 ?
	    ZFS_KEYSTATUS_AVAILABLE : ZFS_KEYSTATUS_UNAVAILABLE);
}

static int
dsl_dir_get_crypt(dsl_dir_t *dd, uint64_t *crypt)
{
	if (dd->dd_crypto_obj == 0) {
		*crypt = ZIO_CRYPT_OFF;
		return (0);
	}

	return (zap_lookup(dd->dd_pool->dp_meta_objset, dd->dd_crypto_obj,
	    DSL_CRYPTO_KEY_CRYPTO_SUITE, 8, 1, crypt));
}

static void
dsl_crypto_key_sync_impl(objset_t *mos, uint64_t dckobj, uint64_t crypt,
    uint64_t root_ddobj, uint64_t guid, uint8_t *iv, uint8_t *mac,
    uint8_t *keydata, uint8_t *hmac_keydata, uint64_t keyformat,
    uint64_t salt, uint64_t iters, dmu_tx_t *tx)
{
	VERIFY0(zap_update(mos, dckobj, DSL_CRYPTO_KEY_CRYPTO_SUITE, 8, 1,
	    &crypt, tx));
	VERIFY0(zap_update(mos, dckobj, DSL_CRYPTO_KEY_ROOT_DDOBJ, 8, 1,
	    &root_ddobj, tx));
	VERIFY0(zap_update(mos, dckobj, DSL_CRYPTO_KEY_GUID, 8, 1,
	    &guid, tx));
	VERIFY0(zap_update(mos, dckobj, DSL_CRYPTO_KEY_IV, 1, WRAPPING_IV_LEN,
	    iv, tx));
	VERIFY0(zap_update(mos, dckobj, DSL_CRYPTO_KEY_MAC, 1, WRAPPING_MAC_LEN,
	    mac, tx));
	VERIFY0(zap_update(mos, dckobj, DSL_CRYPTO_KEY_MASTER_KEY, 1,
	    MASTER_KEY_MAX_LEN, keydata, tx));
	VERIFY0(zap_update(mos, dckobj, DSL_CRYPTO_KEY_HMAC_KEY, 1,
	    SHA512_HMAC_KEYLEN, hmac_keydata, tx));
	VERIFY0(zap_update(mos, dckobj, zfs_prop_to_name(ZFS_PROP_KEYFORMAT),
	    8, 1, &keyformat, tx));
	VERIFY0(zap_update(mos, dckobj, zfs_prop_to_name(ZFS_PROP_PBKDF2_SALT),
	    8, 1, &salt, tx));
	VERIFY0(zap_update(mos, dckobj, zfs_prop_to_name(ZFS_PROP_PBKDF2_ITERS),
	    8, 1, &iters, tx));
}

static void
dsl_crypto_key_sync(dsl_crypto_key_t *dck, dmu_tx_t *tx)
{
	zio_crypt_key_t *key = &dck->dck_key;
	dsl_wrapping_key_t *wkey = dck->dck_wkey;
	uint8_t keydata[MASTER_KEY_MAX_LEN];
	uint8_t hmac_keydata[SHA512_HMAC_KEYLEN];
	uint8_t iv[WRAPPING_IV_LEN];
	uint8_t mac[WRAPPING_MAC_LEN];

	ASSERT(dmu_tx_is_syncing(tx));
	ASSERT3U(key->zk_crypt, <, ZIO_CRYPT_FUNCTIONS);

	/* encrypt and store the keys along with the IV and MAC */
	VERIFY0(zio_crypt_key_wrap(&dck->dck_wkey->wk_key, key, iv, mac,
	    keydata, hmac_keydata));

	/* update the ZAP with the obtained values */
	dsl_crypto_key_sync_impl(tx->tx_pool->dp_meta_objset, dck->dck_obj,
	    key->zk_crypt, wkey->wk_ddobj, key->zk_guid, iv, mac, keydata,
	    hmac_keydata, wkey->wk_keyformat, wkey->wk_salt, wkey->wk_iters,
	    tx);
}

typedef struct spa_keystore_change_key_args {
	const char *skcka_dsname;
	dsl_crypto_params_t *skcka_cp;
} spa_keystore_change_key_args_t;

static int
spa_keystore_change_key_check(void *arg, dmu_tx_t *tx)
{
	int ret;
	dsl_dir_t *dd = NULL;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	spa_keystore_change_key_args_t *skcka = arg;
	dsl_crypto_params_t *dcp = skcka->skcka_cp;
	uint64_t rddobj;

	/* check for the encryption feature */
	if (!spa_feature_is_enabled(dp->dp_spa, SPA_FEATURE_ENCRYPTION)) {
		ret = SET_ERROR(ENOTSUP);
		goto error;
	}

	/* check for valid key change command */
	if (dcp->cp_cmd != DCP_CMD_NEW_KEY &&
	    dcp->cp_cmd != DCP_CMD_INHERIT &&
	    dcp->cp_cmd != DCP_CMD_FORCE_NEW_KEY &&
	    dcp->cp_cmd != DCP_CMD_FORCE_INHERIT) {
		ret = SET_ERROR(EINVAL);
		goto error;
	}

	/* hold the dd */
	ret = dsl_dir_hold(dp, skcka->skcka_dsname, FTAG, &dd, NULL);
	if (ret != 0) {
		dd = NULL;
		goto error;
	}

	/* verify that the dataset is encrypted */
	if (dd->dd_crypto_obj == 0) {
		ret = SET_ERROR(EINVAL);
		goto error;
	}

	/* clones must always use their origin's key */
	if (dsl_dir_is_clone(dd)) {
		ret = SET_ERROR(EINVAL);
		goto error;
	}

	/* lookup the ddobj we are inheriting the keylocation from */
	ret = dsl_dir_get_encryption_root_ddobj(dd, &rddobj);
	if (ret != 0)
		goto error;

	/* Handle inheritance */
	if (dcp->cp_cmd == DCP_CMD_INHERIT ||
	    dcp->cp_cmd == DCP_CMD_FORCE_INHERIT) {
		/* no other encryption params should be given */
		if (dcp->cp_crypt != ZIO_CRYPT_INHERIT ||
		    dcp->cp_keylocation != NULL ||
		    dcp->cp_wkey != NULL) {
			ret = SET_ERROR(EINVAL);
			goto error;
		}

		/* check that this is an encryption root */
		if (dd->dd_object != rddobj) {
			ret = SET_ERROR(EINVAL);
			goto error;
		}

		/* check that the parent is encrypted */
		if (dd->dd_parent->dd_crypto_obj == 0) {
			ret = SET_ERROR(EINVAL);
			goto error;
		}

		/* if we are rewrapping check that both keys are loaded */
		if (dcp->cp_cmd == DCP_CMD_INHERIT) {
			ret = dmu_objset_check_wkey_loaded(dd);
			if (ret != 0)
				goto error;

			ret = dmu_objset_check_wkey_loaded(dd->dd_parent);
			if (ret != 0)
				goto error;
		}

		dsl_dir_rele(dd, FTAG);
		return (0);
	}

	/* handle forcing an encryption root without rewrapping */
	if (dcp->cp_cmd == DCP_CMD_FORCE_NEW_KEY) {
		/* no other encryption params should be given */
		if (dcp->cp_crypt != ZIO_CRYPT_INHERIT ||
		    dcp->cp_keylocation != NULL ||
		    dcp->cp_wkey != NULL) {
			ret = SET_ERROR(EINVAL);
			goto error;
		}

		/* check that this is not an encryption root */
		if (dd->dd_object == rddobj) {
			ret = SET_ERROR(EINVAL);
			goto error;
		}

		dsl_dir_rele(dd, FTAG);
		return (0);
	}

	/* crypt cannot be changed after creation */
	if (dcp->cp_crypt != ZIO_CRYPT_INHERIT) {
		ret = SET_ERROR(EINVAL);
		goto error;
	}

	/* we are not inheritting our parent's wkey so we need one ourselves */
	if (dcp->cp_wkey == NULL) {
		ret = SET_ERROR(EINVAL);
		goto error;
	}

	/* check for a valid keyformat for the new wrapping key */
	if (dcp->cp_wkey->wk_keyformat >= ZFS_KEYFORMAT_FORMATS ||
	    dcp->cp_wkey->wk_keyformat == ZFS_KEYFORMAT_NONE) {
		ret = SET_ERROR(EINVAL);
		goto error;
	}

	/*
	 * If this dataset is not currently an encryption root we need a new
	 * keylocation for this dataset's new wrapping key. Otherwise we can
	 * just keep the one we already had.
	 */
	if (dd->dd_object != rddobj && dcp->cp_keylocation == NULL) {
		ret = SET_ERROR(EINVAL);
		goto error;
	}

	/* check that the keylocation is valid if it is not NULL */
	if (dcp->cp_keylocation != NULL &&
	    !zfs_prop_valid_keylocation(dcp->cp_keylocation, B_TRUE)) {
		ret = SET_ERROR(EINVAL);
		goto error;
	}

	/* passphrases require pbkdf2 salt and iters */
	if (dcp->cp_wkey->wk_keyformat == ZFS_KEYFORMAT_PASSPHRASE) {
		if (dcp->cp_wkey->wk_salt == 0 ||
		    dcp->cp_wkey->wk_iters < MIN_PBKDF2_ITERATIONS) {
			ret = SET_ERROR(EINVAL);
			goto error;
		}
	} else {
		if (dcp->cp_wkey->wk_salt != 0 || dcp->cp_wkey->wk_iters != 0) {
			ret = SET_ERROR(EINVAL);
			goto error;
		}
	}

	/* make sure the dd's wkey is loaded */
	ret = dmu_objset_check_wkey_loaded(dd);
	if (ret != 0)
		goto error;

	dsl_dir_rele(dd, FTAG);

	return (0);

error:
	if (dd != NULL)
		dsl_dir_rele(dd, FTAG);

	return (ret);
}

/*
 * This function deals with the intricacies of updating wrapping
 * key references and encryption roots recursively in the event
 * of a call to 'zfs change-key' or 'zfs promote'. The 'skip'
 * parameter should always be set to B_FALSE when called
 * externally.
 */
static void
spa_keystore_change_key_sync_impl(uint64_t rddobj, uint64_t ddobj,
    uint64_t new_rddobj, dsl_wrapping_key_t *wkey, boolean_t skip,
    dmu_tx_t *tx)
{
	int ret;
	zap_cursor_t *zc;
	zap_attribute_t *za;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	dsl_dir_t *dd = NULL;
	dsl_crypto_key_t *dck = NULL;
	uint64_t curr_rddobj;

	ASSERT(RW_WRITE_HELD(&dp->dp_spa->spa_keystore.sk_wkeys_lock));

	/* hold the dd */
	VERIFY0(dsl_dir_hold_obj(dp, ddobj, NULL, FTAG, &dd));

	/* ignore special dsl dirs */
	if (dd->dd_myname[0] == '$' || dd->dd_myname[0] == '%') {
		dsl_dir_rele(dd, FTAG);
		return;
	}

	ret = dsl_dir_get_encryption_root_ddobj(dd, &curr_rddobj);
	VERIFY(ret == 0 || ret == ENOENT);

	/*
	 * Stop recursing if this dsl dir didn't inherit from the root
	 * or if this dd is a clone.
	 */
	if (ret == ENOENT ||
	    (!skip && (curr_rddobj != rddobj || dsl_dir_is_clone(dd)))) {
		dsl_dir_rele(dd, FTAG);
		return;
	}

	/*
	 * If we don't have a wrapping key just update the dck to reflect the
	 * new encryption root. Otherwise rewrap the entire dck and re-sync it
	 * to disk. If skip is set, we don't do any of this work.
	 */
	if (!skip) {
		if (wkey == NULL) {
			VERIFY0(zap_update(dp->dp_meta_objset,
			    dd->dd_crypto_obj,
			    DSL_CRYPTO_KEY_ROOT_DDOBJ, 8, 1,
			    &new_rddobj, tx));
		} else {
			VERIFY0(spa_keystore_dsl_key_hold_dd(dp->dp_spa, dd,
			    FTAG, &dck));
			dsl_wrapping_key_hold(wkey, dck);
			dsl_wrapping_key_rele(dck->dck_wkey, dck);
			dck->dck_wkey = wkey;
			dsl_crypto_key_sync(dck, tx);
			spa_keystore_dsl_key_rele(dp->dp_spa, dck, FTAG);
		}
	}

	zc = kmem_alloc(sizeof (zap_cursor_t), KM_SLEEP);
	za = kmem_alloc(sizeof (zap_attribute_t), KM_SLEEP);

	/* Recurse into all child dsl dirs. */
	for (zap_cursor_init(zc, dp->dp_meta_objset,
	    dsl_dir_phys(dd)->dd_child_dir_zapobj);
	    zap_cursor_retrieve(zc, za) == 0;
	    zap_cursor_advance(zc)) {
		spa_keystore_change_key_sync_impl(rddobj,
		    za->za_first_integer, new_rddobj, wkey, B_FALSE, tx);
	}
	zap_cursor_fini(zc);

	/*
	 * Recurse into all dsl dirs of clones. We utilize the skip parameter
	 * here so that we don't attempt to process the clones directly. This
	 * is because the clone and its origin share the same dck, which has
	 * already been updated.
	 */
	for (zap_cursor_init(zc, dp->dp_meta_objset,
	    dsl_dir_phys(dd)->dd_clones);
	    zap_cursor_retrieve(zc, za) == 0;
	    zap_cursor_advance(zc)) {
		dsl_dataset_t *clone;

		VERIFY0(dsl_dataset_hold_obj(dp, za->za_first_integer,
		    FTAG, &clone));
		spa_keystore_change_key_sync_impl(rddobj,
		    clone->ds_dir->dd_object, new_rddobj, wkey, B_TRUE, tx);
		dsl_dataset_rele(clone, FTAG);
	}
	zap_cursor_fini(zc);

	kmem_free(za, sizeof (zap_attribute_t));
	kmem_free(zc, sizeof (zap_cursor_t));

	dsl_dir_rele(dd, FTAG);
}

static void
spa_keystore_change_key_sync(void *arg, dmu_tx_t *tx)
{
	dsl_dataset_t *ds;
	avl_index_t where;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	spa_t *spa = dp->dp_spa;
	spa_keystore_change_key_args_t *skcka = arg;
	dsl_crypto_params_t *dcp = skcka->skcka_cp;
	dsl_wrapping_key_t *wkey = NULL, *found_wkey;
	dsl_wrapping_key_t wkey_search;
	char *keylocation = dcp->cp_keylocation;
	uint64_t rddobj, new_rddobj;

	/* create and initialize the wrapping key */
	VERIFY0(dsl_dataset_hold(dp, skcka->skcka_dsname, FTAG, &ds));
	ASSERT(!ds->ds_is_snapshot);

	if (dcp->cp_cmd == DCP_CMD_NEW_KEY ||
	    dcp->cp_cmd == DCP_CMD_FORCE_NEW_KEY) {
		/*
		 * We are changing to a new wkey. Set additional properties
		 * which can be sent along with this ioctl. Note that this
		 * command can set keylocation even if it can't normally be
		 * set via 'zfs set' due to a non-local keylocation.
		 */
		if (dcp->cp_cmd == DCP_CMD_NEW_KEY) {
			wkey = dcp->cp_wkey;
			wkey->wk_ddobj = ds->ds_dir->dd_object;
		} else {
			keylocation = "prompt";
		}

		if (keylocation != NULL) {
			dsl_prop_set_sync_impl(ds,
			    zfs_prop_to_name(ZFS_PROP_KEYLOCATION),
			    ZPROP_SRC_LOCAL, 1, strlen(keylocation) + 1,
			    keylocation, tx);
		}

		VERIFY0(dsl_dir_get_encryption_root_ddobj(ds->ds_dir, &rddobj));
		new_rddobj = ds->ds_dir->dd_object;
	} else {
		/*
		 * We are inheritting the parent's wkey. Unset any local
		 * keylocation and grab a reference to the wkey.
		 */
		if (dcp->cp_cmd == DCP_CMD_INHERIT) {
			VERIFY0(spa_keystore_wkey_hold_dd(spa,
			    ds->ds_dir->dd_parent, FTAG, &wkey));
		}

		dsl_prop_set_sync_impl(ds,
		    zfs_prop_to_name(ZFS_PROP_KEYLOCATION), ZPROP_SRC_NONE,
		    0, 0, NULL, tx);

		rddobj = ds->ds_dir->dd_object;
		VERIFY0(dsl_dir_get_encryption_root_ddobj(ds->ds_dir->dd_parent,
		    &new_rddobj));
	}

	if (wkey == NULL) {
		ASSERT(dcp->cp_cmd == DCP_CMD_FORCE_INHERIT ||
		    dcp->cp_cmd == DCP_CMD_FORCE_NEW_KEY);
	}

	rw_enter(&spa->spa_keystore.sk_wkeys_lock, RW_WRITER);

	/* recurse through all children and rewrap their keys */
	spa_keystore_change_key_sync_impl(rddobj, ds->ds_dir->dd_object,
	    new_rddobj, wkey, B_FALSE, tx);

	/*
	 * All references to the old wkey should be released now (if it
	 * existed). Replace the wrapping key.
	 */
	wkey_search.wk_ddobj = ds->ds_dir->dd_object;
	found_wkey = avl_find(&spa->spa_keystore.sk_wkeys, &wkey_search, NULL);
	if (found_wkey != NULL) {
		ASSERT0(zfs_refcount_count(&found_wkey->wk_refcnt));
		avl_remove(&spa->spa_keystore.sk_wkeys, found_wkey);
		dsl_wrapping_key_free(found_wkey);
	}

	if (dcp->cp_cmd == DCP_CMD_NEW_KEY) {
		avl_find(&spa->spa_keystore.sk_wkeys, wkey, &where);
		avl_insert(&spa->spa_keystore.sk_wkeys, wkey, where);
	} else if (wkey != NULL) {
		dsl_wrapping_key_rele(wkey, FTAG);
	}

	rw_exit(&spa->spa_keystore.sk_wkeys_lock);

	dsl_dataset_rele(ds, FTAG);
}

int
spa_keystore_change_key(const char *dsname, dsl_crypto_params_t *dcp)
{
	spa_keystore_change_key_args_t skcka;

	/* initialize the args struct */
	skcka.skcka_dsname = dsname;
	skcka.skcka_cp = dcp;

	/*
	 * Perform the actual work in syncing context. The blocks modified
	 * here could be calculated but it would require holding the pool
	 * lock and traversing all of the datasets that will have their keys
	 * changed.
	 */
	return (dsl_sync_task(dsname, spa_keystore_change_key_check,
	    spa_keystore_change_key_sync, &skcka, 15,
	    ZFS_SPACE_CHECK_RESERVED));
}

int
dsl_dir_rename_crypt_check(dsl_dir_t *dd, dsl_dir_t *newparent)
{
	int ret;
	uint64_t curr_rddobj, parent_rddobj;

	if (dd->dd_crypto_obj == 0)
		return (0);

	ret = dsl_dir_get_encryption_root_ddobj(dd, &curr_rddobj);
	if (ret != 0)
		goto error;

	/*
	 * if this is not an encryption root, we must make sure we are not
	 * moving dd to a new encryption root
	 */
	if (dd->dd_object != curr_rddobj) {
		ret = dsl_dir_get_encryption_root_ddobj(newparent,
		    &parent_rddobj);
		if (ret != 0)
			goto error;

		if (parent_rddobj != curr_rddobj) {
			ret = SET_ERROR(EACCES);
			goto error;
		}
	}

	return (0);

error:
	return (ret);
}

/*
 * Check to make sure that a promote from targetdd to origindd will not require
 * any key rewraps.
 */
int
dsl_dataset_promote_crypt_check(dsl_dir_t *target, dsl_dir_t *origin)
{
	int ret;
	uint64_t rddobj, op_rddobj, tp_rddobj;

	/* If the dataset is not encrypted we don't need to check anything */
	if (origin->dd_crypto_obj == 0)
		return (0);

	/*
	 * If we are not changing the first origin snapshot in a chain
	 * the encryption root won't change either.
	 */
	if (dsl_dir_is_clone(origin))
		return (0);

	/*
	 * If the origin is the encryption root we will update
	 * the DSL Crypto Key to point to the target instead.
	 */
	ret = dsl_dir_get_encryption_root_ddobj(origin, &rddobj);
	if (ret != 0)
		return (ret);

	if (rddobj == origin->dd_object)
		return (0);

	/*
	 * The origin is inheriting its encryption root from its parent.
	 * Check that the parent of the target has the same encryption root.
	 */
	ret = dsl_dir_get_encryption_root_ddobj(origin->dd_parent, &op_rddobj);
	if (ret == ENOENT)
		return (SET_ERROR(EACCES));
	else if (ret != 0)
		return (ret);

	ret = dsl_dir_get_encryption_root_ddobj(target->dd_parent, &tp_rddobj);
	if (ret == ENOENT)
		return (SET_ERROR(EACCES));
	else if (ret != 0)
		return (ret);

	if (op_rddobj != tp_rddobj)
		return (SET_ERROR(EACCES));

	return (0);
}

void
dsl_dataset_promote_crypt_sync(dsl_dir_t *target, dsl_dir_t *origin,
    dmu_tx_t *tx)
{
	uint64_t rddobj;
	dsl_pool_t *dp = target->dd_pool;
	dsl_dataset_t *targetds;
	dsl_dataset_t *originds;
	char *keylocation;

	if (origin->dd_crypto_obj == 0)
		return;
	if (dsl_dir_is_clone(origin))
		return;

	VERIFY0(dsl_dir_get_encryption_root_ddobj(origin, &rddobj));

	if (rddobj != origin->dd_object)
		return;

	/*
	 * If the target is being promoted to the encryption root update the
	 * DSL Crypto Key and keylocation to reflect that. We also need to
	 * update the DSL Crypto Keys of all children inheritting their
	 * encryption root to point to the new target. Otherwise, the check
	 * function ensured that the encryption root will not change.
	 */
	keylocation = kmem_alloc(ZAP_MAXVALUELEN, KM_SLEEP);

	VERIFY0(dsl_dataset_hold_obj(dp,
	    dsl_dir_phys(target)->dd_head_dataset_obj, FTAG, &targetds));
	VERIFY0(dsl_dataset_hold_obj(dp,
	    dsl_dir_phys(origin)->dd_head_dataset_obj, FTAG, &originds));

	VERIFY0(dsl_prop_get_dd(origin, zfs_prop_to_name(ZFS_PROP_KEYLOCATION),
	    1, ZAP_MAXVALUELEN, keylocation, NULL, B_FALSE));
	dsl_prop_set_sync_impl(targetds, zfs_prop_to_name(ZFS_PROP_KEYLOCATION),
	    ZPROP_SRC_LOCAL, 1, strlen(keylocation) + 1, keylocation, tx);
	dsl_prop_set_sync_impl(originds, zfs_prop_to_name(ZFS_PROP_KEYLOCATION),
	    ZPROP_SRC_NONE, 0, 0, NULL, tx);

	rw_enter(&dp->dp_spa->spa_keystore.sk_wkeys_lock, RW_WRITER);
	spa_keystore_change_key_sync_impl(rddobj, origin->dd_object,
	    target->dd_object, NULL, B_FALSE, tx);
	rw_exit(&dp->dp_spa->spa_keystore.sk_wkeys_lock);

	dsl_dataset_rele(targetds, FTAG);
	dsl_dataset_rele(originds, FTAG);
	kmem_free(keylocation, ZAP_MAXVALUELEN);
}

int
dmu_objset_create_crypt_check(dsl_dir_t *parentdd, dsl_crypto_params_t *dcp,
    boolean_t *will_encrypt)
{
	int ret;
	uint64_t pcrypt, crypt;
	dsl_crypto_params_t dummy_dcp = { 0 };

	if (will_encrypt != NULL)
		*will_encrypt = B_FALSE;

	if (dcp == NULL)
		dcp = &dummy_dcp;

	if (dcp->cp_cmd != DCP_CMD_NONE)
		return (SET_ERROR(EINVAL));

	if (parentdd != NULL) {
		ret = dsl_dir_get_crypt(parentdd, &pcrypt);
		if (ret != 0)
			return (ret);
	} else {
		pcrypt = ZIO_CRYPT_OFF;
	}

	crypt = (dcp->cp_crypt == ZIO_CRYPT_INHERIT) ? pcrypt : dcp->cp_crypt;

	ASSERT3U(pcrypt, !=, ZIO_CRYPT_INHERIT);
	ASSERT3U(crypt, !=, ZIO_CRYPT_INHERIT);

	/* check for valid dcp with no encryption (inherited or local) */
	if (crypt == ZIO_CRYPT_OFF) {
		/* Must not specify encryption params */
		if (dcp->cp_wkey != NULL ||
		    (dcp->cp_keylocation != NULL &&
		    strcmp(dcp->cp_keylocation, "none") != 0))
			return (SET_ERROR(EINVAL));

		return (0);
	}

	if (will_encrypt != NULL)
		*will_encrypt = B_TRUE;

	/*
	 * We will now definitely be encrypting. Check the feature flag. When
	 * creating the pool the caller will check this for us since we won't
	 * technically have the feature activated yet.
	 */
	if (parentdd != NULL &&
	    !spa_feature_is_enabled(parentdd->dd_pool->dp_spa,
	    SPA_FEATURE_ENCRYPTION)) {
		return (SET_ERROR(EOPNOTSUPP));
	}

	/* Check for errata #4 (encryption enabled, bookmark_v2 disabled) */
	if (parentdd != NULL &&
	    !spa_feature_is_enabled(parentdd->dd_pool->dp_spa,
	    SPA_FEATURE_BOOKMARK_V2)) {
		return (SET_ERROR(EOPNOTSUPP));
	}

	/* handle inheritance */
	if (dcp->cp_wkey == NULL) {
		ASSERT3P(parentdd, !=, NULL);

		/* key must be fully unspecified */
		if (dcp->cp_keylocation != NULL)
			return (SET_ERROR(EINVAL));

		/* parent must have a key to inherit */
		if (pcrypt == ZIO_CRYPT_OFF)
			return (SET_ERROR(EINVAL));

		/* check for parent key */
		ret = dmu_objset_check_wkey_loaded(parentdd);
		if (ret != 0)
			return (ret);

		return (0);
	}

	/* At this point we should have a fully specified key. Check location */
	if (dcp->cp_keylocation == NULL ||
	    !zfs_prop_valid_keylocation(dcp->cp_keylocation, B_TRUE))
		return (SET_ERROR(EINVAL));

	/* Must have fully specified keyformat */
	switch (dcp->cp_wkey->wk_keyformat) {
	case ZFS_KEYFORMAT_HEX:
	case ZFS_KEYFORMAT_RAW:
		/* requires no pbkdf2 iters and salt */
		if (dcp->cp_wkey->wk_salt != 0 || dcp->cp_wkey->wk_iters != 0)
			return (SET_ERROR(EINVAL));
		break;
	case ZFS_KEYFORMAT_PASSPHRASE:
		/* requires pbkdf2 iters and salt */
		if (dcp->cp_wkey->wk_salt == 0 ||
		    dcp->cp_wkey->wk_iters < MIN_PBKDF2_ITERATIONS)
			return (SET_ERROR(EINVAL));
		break;
	case ZFS_KEYFORMAT_NONE:
	default:
		/* keyformat must be specified and valid */
		return (SET_ERROR(EINVAL));
	}

	return (0);
}

void
dsl_dataset_create_crypt_sync(uint64_t dsobj, dsl_dir_t *dd,
    dsl_dataset_t *origin, dsl_crypto_params_t *dcp, dmu_tx_t *tx)
{
	dsl_pool_t *dp = dd->dd_pool;
	uint64_t crypt;
	dsl_wrapping_key_t *wkey;

	/* clones always use their origin's wrapping key */
	if (dsl_dir_is_clone(dd)) {
		ASSERT3P(dcp, ==, NULL);

		/*
		 * If this is an encrypted clone we just need to clone the
		 * dck into dd. Zapify the dd so we can do that.
		 */
		if (origin->ds_dir->dd_crypto_obj != 0) {
			dmu_buf_will_dirty(dd->dd_dbuf, tx);
			dsl_dir_zapify(dd, tx);

			dd->dd_crypto_obj =
			    dsl_crypto_key_clone_sync(origin->ds_dir, tx);
			VERIFY0(zap_add(dp->dp_meta_objset, dd->dd_object,
			    DD_FIELD_CRYPTO_KEY_OBJ, sizeof (uint64_t), 1,
			    &dd->dd_crypto_obj, tx));
		}

		return;
	}

	/*
	 * A NULL dcp at this point indicates this is the origin dataset
	 * which does not have an objset to encrypt. Raw receives will handle
	 * encryption separately later. In both cases we can simply return.
	 */
	if (dcp == NULL || dcp->cp_cmd == DCP_CMD_RAW_RECV)
		return;

	crypt = dcp->cp_crypt;
	wkey = dcp->cp_wkey;

	/* figure out the effective crypt */
	if (crypt == ZIO_CRYPT_INHERIT && dd->dd_parent != NULL)
		VERIFY0(dsl_dir_get_crypt(dd->dd_parent, &crypt));

	/* if we aren't doing encryption just return */
	if (crypt == ZIO_CRYPT_OFF || crypt == ZIO_CRYPT_INHERIT)
		return;

	/* zapify the dd so that we can add the crypto key obj to it */
	dmu_buf_will_dirty(dd->dd_dbuf, tx);
	dsl_dir_zapify(dd, tx);

	/* use the new key if given or inherit from the parent */
	if (wkey == NULL) {
		VERIFY0(spa_keystore_wkey_hold_dd(dp->dp_spa,
		    dd->dd_parent, FTAG, &wkey));
	} else {
		wkey->wk_ddobj = dd->dd_object;
	}

	ASSERT3P(wkey, !=, NULL);

	/* Create or clone the DSL crypto key and activate the feature */
	dd->dd_crypto_obj = dsl_crypto_key_create_sync(crypt, wkey, tx);
	VERIFY0(zap_add(dp->dp_meta_objset, dd->dd_object,
	    DD_FIELD_CRYPTO_KEY_OBJ, sizeof (uint64_t), 1, &dd->dd_crypto_obj,
	    tx));
	dsl_dataset_activate_feature(dsobj, SPA_FEATURE_ENCRYPTION,
	    (void *)B_TRUE, tx);

	/*
	 * If we inherited the wrapping key we release our reference now.
	 * Otherwise, this is a new key and we need to load it into the
	 * keystore.
	 */
	if (dcp->cp_wkey == NULL) {
		dsl_wrapping_key_rele(wkey, FTAG);
	} else {
		VERIFY0(spa_keystore_load_wkey_impl(dp->dp_spa, wkey));
	}
}

typedef struct dsl_crypto_recv_key_arg {
	uint64_t dcrka_dsobj;
	uint64_t dcrka_fromobj;
	dmu_objset_type_t dcrka_ostype;
	nvlist_t *dcrka_nvl;
	boolean_t dcrka_do_key;
} dsl_crypto_recv_key_arg_t;

static int
dsl_crypto_recv_raw_objset_check(dsl_dataset_t *ds, dsl_dataset_t *fromds,
    dmu_objset_type_t ostype, nvlist_t *nvl, dmu_tx_t *tx)
{
	int ret;
	objset_t *os;
	dnode_t *mdn;
	uint8_t *buf = NULL;
	uint_t len;
	uint64_t intval, nlevels, blksz, ibs;
	uint64_t nblkptr, maxblkid;

	if (ostype != DMU_OST_ZFS && ostype != DMU_OST_ZVOL)
		return (SET_ERROR(EINVAL));

	/* raw receives also need info about the structure of the metadnode */
	ret = nvlist_lookup_uint64(nvl, "mdn_compress", &intval);
	if (ret != 0 || intval >= ZIO_COMPRESS_LEGACY_FUNCTIONS)
		return (SET_ERROR(EINVAL));

	ret = nvlist_lookup_uint64(nvl, "mdn_checksum", &intval);
	if (ret != 0 || intval >= ZIO_CHECKSUM_LEGACY_FUNCTIONS)
		return (SET_ERROR(EINVAL));

	ret = nvlist_lookup_uint64(nvl, "mdn_nlevels", &nlevels);
	if (ret != 0 || nlevels > DN_MAX_LEVELS)
		return (SET_ERROR(EINVAL));

	ret = nvlist_lookup_uint64(nvl, "mdn_blksz", &blksz);
	if (ret != 0 || blksz < SPA_MINBLOCKSIZE)
		return (SET_ERROR(EINVAL));
	else if (blksz > spa_maxblocksize(tx->tx_pool->dp_spa))
		return (SET_ERROR(ENOTSUP));

	ret = nvlist_lookup_uint64(nvl, "mdn_indblkshift", &ibs);
	if (ret != 0 || ibs < DN_MIN_INDBLKSHIFT || ibs > DN_MAX_INDBLKSHIFT)
		return (SET_ERROR(ENOTSUP));

	ret = nvlist_lookup_uint64(nvl, "mdn_nblkptr", &nblkptr);
	if (ret != 0 || nblkptr != DN_MAX_NBLKPTR)
		return (SET_ERROR(ENOTSUP));

	ret = nvlist_lookup_uint64(nvl, "mdn_maxblkid", &maxblkid);
	if (ret != 0)
		return (SET_ERROR(EINVAL));

	ret = nvlist_lookup_uint8_array(nvl, "portable_mac", &buf, &len);
	if (ret != 0 || len != ZIO_OBJSET_MAC_LEN)
		return (SET_ERROR(EINVAL));

	ret = dmu_objset_from_ds(ds, &os);
	if (ret != 0)
		return (ret);

	mdn = DMU_META_DNODE(os);

	/*
	 * If we already created the objset, make sure its unchangeable
	 * properties match the ones received in the nvlist.
	 */
	rrw_enter(&ds->ds_bp_rwlock, RW_READER, FTAG);
	if (!BP_IS_HOLE(dsl_dataset_get_blkptr(ds)) &&
	    (mdn->dn_nlevels != nlevels || mdn->dn_datablksz != blksz ||
	    mdn->dn_indblkshift != ibs || mdn->dn_nblkptr != nblkptr)) {
		rrw_exit(&ds->ds_bp_rwlock, FTAG);
		return (SET_ERROR(EINVAL));
	}
	rrw_exit(&ds->ds_bp_rwlock, FTAG);

	/*
	 * Check that the ivset guid of the fromds matches the one from the
	 * send stream. Older versions of the encryption code did not have
	 * an ivset guid on the from dataset and did not send one in the
	 * stream. For these streams we provide the
	 * zfs_disable_ivset_guid_check tunable to allow these datasets to
	 * be received with a generated ivset guid.
	 */
	if (fromds != NULL && !zfs_disable_ivset_guid_check) {
		uint64_t from_ivset_guid = 0;
		intval = 0;

		(void) nvlist_lookup_uint64(nvl, "from_ivset_guid", &intval);
		(void) zap_lookup(tx->tx_pool->dp_meta_objset,
		    fromds->ds_object, DS_FIELD_IVSET_GUID,
		    sizeof (from_ivset_guid), 1, &from_ivset_guid);

		if (intval == 0 || from_ivset_guid == 0)
			return (SET_ERROR(ZFS_ERR_FROM_IVSET_GUID_MISSING));

		if (intval != from_ivset_guid)
			return (SET_ERROR(ZFS_ERR_FROM_IVSET_GUID_MISMATCH));
	}

	return (0);
}

static void
dsl_crypto_recv_raw_objset_sync(dsl_dataset_t *ds, dmu_objset_type_t ostype,
    nvlist_t *nvl, dmu_tx_t *tx)
{
	dsl_pool_t *dp = tx->tx_pool;
	objset_t *os;
	dnode_t *mdn;
	zio_t *zio;
	uint8_t *portable_mac;
	uint_t len;
	uint64_t compress, checksum, nlevels, blksz, ibs, maxblkid;
	boolean_t newds = B_FALSE;

	VERIFY0(dmu_objset_from_ds(ds, &os));
	mdn = DMU_META_DNODE(os);

	/*
	 * Fetch the values we need from the nvlist. "to_ivset_guid" must
	 * be set on the snapshot, which doesn't exist yet. The receive
	 * code will take care of this for us later.
	 */
	compress = fnvlist_lookup_uint64(nvl, "mdn_compress");
	checksum = fnvlist_lookup_uint64(nvl, "mdn_checksum");
	nlevels = fnvlist_lookup_uint64(nvl, "mdn_nlevels");
	blksz = fnvlist_lookup_uint64(nvl, "mdn_blksz");
	ibs = fnvlist_lookup_uint64(nvl, "mdn_indblkshift");
	maxblkid = fnvlist_lookup_uint64(nvl, "mdn_maxblkid");
	VERIFY0(nvlist_lookup_uint8_array(nvl, "portable_mac", &portable_mac,
	    &len));

	/* if we haven't created an objset for the ds yet, do that now */
	rrw_enter(&ds->ds_bp_rwlock, RW_READER, FTAG);
	if (BP_IS_HOLE(dsl_dataset_get_blkptr(ds))) {
		(void) dmu_objset_create_impl_dnstats(dp->dp_spa, ds,
		    dsl_dataset_get_blkptr(ds), ostype, nlevels, blksz,
		    ibs, tx);
		newds = B_TRUE;
	}
	rrw_exit(&ds->ds_bp_rwlock, FTAG);

	/*
	 * Set the portable MAC. The local MAC will always be zero since the
	 * incoming data will all be portable and user accounting will be
	 * deferred until the next mount. Afterwards, flag the os to be
	 * written out raw next time.
	 */
	arc_release(os->os_phys_buf, &os->os_phys_buf);
	bcopy(portable_mac, os->os_phys->os_portable_mac, ZIO_OBJSET_MAC_LEN);
	os->os_phys->os_flags &= ~OBJSET_FLAG_USERACCOUNTING_COMPLETE;
	os->os_phys->os_flags &= ~OBJSET_FLAG_USEROBJACCOUNTING_COMPLETE;
	os->os_flags = os->os_phys->os_flags;
	bzero(os->os_phys->os_local_mac, ZIO_OBJSET_MAC_LEN);
	os->os_next_write_raw[tx->tx_txg & TXG_MASK] = B_TRUE;

	/* set metadnode compression and checksum */
	mdn->dn_compress = compress;
	mdn->dn_checksum = checksum;

	rw_enter(&mdn->dn_struct_rwlock, RW_WRITER);
	dnode_new_blkid(mdn, maxblkid, tx, B_FALSE, B_TRUE);
	rw_exit(&mdn->dn_struct_rwlock);

	/*
	 * We can't normally dirty the dataset in syncing context unless
	 * we are creating a new dataset. In this case, we perform a
	 * pseudo txg sync here instead.
	 */
	if (newds) {
		dsl_dataset_dirty(ds, tx);
	} else {
		zio = zio_root(dp->dp_spa, NULL, NULL, ZIO_FLAG_MUSTSUCCEED);
		dsl_dataset_sync(ds, zio, tx);
		VERIFY0(zio_wait(zio));

		/* dsl_dataset_sync_done will drop this reference. */
		dmu_buf_add_ref(ds->ds_dbuf, ds);
		dsl_dataset_sync_done(ds, tx);
	}
}

int
dsl_crypto_recv_raw_key_check(dsl_dataset_t *ds, nvlist_t *nvl, dmu_tx_t *tx)
{
	int ret;
	objset_t *mos = tx->tx_pool->dp_meta_objset;
	uint8_t *buf = NULL;
	uint_t len;
	uint64_t intval, key_guid, version;
	boolean_t is_passphrase = B_FALSE;

	ASSERT(dsl_dataset_phys(ds)->ds_flags & DS_FLAG_INCONSISTENT);

	/*
	 * Read and check all the encryption values from the nvlist. We need
	 * all of the fields of a DSL Crypto Key, as well as a fully specified
	 * wrapping key.
	 */
	ret = nvlist_lookup_uint64(nvl, DSL_CRYPTO_KEY_CRYPTO_SUITE, &intval);
	if (ret != 0 || intval >= ZIO_CRYPT_FUNCTIONS ||
	    intval <= ZIO_CRYPT_OFF)
		return (SET_ERROR(EINVAL));

	ret = nvlist_lookup_uint64(nvl, DSL_CRYPTO_KEY_GUID, &intval);
	if (ret != 0)
		return (SET_ERROR(EINVAL));

	/*
	 * If this is an incremental receive make sure the given key guid
	 * matches the one we already have.
	 */
	if (ds->ds_dir->dd_crypto_obj != 0) {
		ret = zap_lookup(mos, ds->ds_dir->dd_crypto_obj,
		    DSL_CRYPTO_KEY_GUID, 8, 1, &key_guid);
		if (ret != 0)
			return (ret);
		if (intval != key_guid)
			return (SET_ERROR(EACCES));
	}

	ret = nvlist_lookup_uint8_array(nvl, DSL_CRYPTO_KEY_MASTER_KEY,
	    &buf, &len);
	if (ret != 0 || len != MASTER_KEY_MAX_LEN)
		return (SET_ERROR(EINVAL));

	ret = nvlist_lookup_uint8_array(nvl, DSL_CRYPTO_KEY_HMAC_KEY,
	    &buf, &len);
	if (ret != 0 || len != SHA512_HMAC_KEYLEN)
		return (SET_ERROR(EINVAL));

	ret = nvlist_lookup_uint8_array(nvl, DSL_CRYPTO_KEY_IV, &buf, &len);
	if (ret != 0 || len != WRAPPING_IV_LEN)
		return (SET_ERROR(EINVAL));

	ret = nvlist_lookup_uint8_array(nvl, DSL_CRYPTO_KEY_MAC, &buf, &len);
	if (ret != 0 || len != WRAPPING_MAC_LEN)
		return (SET_ERROR(EINVAL));

	/*
	 * We don't support receiving old on-disk formats. The version 0
	 * implementation protected several fields in an objset that were
	 * not always portable during a raw receive. As a result, we call
	 * the old version an on-disk errata #3.
	 */
	ret = nvlist_lookup_uint64(nvl, DSL_CRYPTO_KEY_VERSION, &version);
	if (ret != 0 || version != ZIO_CRYPT_KEY_CURRENT_VERSION)
		return (SET_ERROR(ENOTSUP));

	ret = nvlist_lookup_uint64(nvl, zfs_prop_to_name(ZFS_PROP_KEYFORMAT),
	    &intval);
	if (ret != 0 || intval >= ZFS_KEYFORMAT_FORMATS ||
	    intval == ZFS_KEYFORMAT_NONE)
		return (SET_ERROR(EINVAL));

	is_passphrase = (intval == ZFS_KEYFORMAT_PASSPHRASE);

	/*
	 * for raw receives we allow any number of pbkdf2iters since there
	 * won't be a chance for the user to change it.
	 */
	ret = nvlist_lookup_uint64(nvl, zfs_prop_to_name(ZFS_PROP_PBKDF2_ITERS),
	    &intval);
	if (ret != 0 || (is_passphrase == (intval == 0)))
		return (SET_ERROR(EINVAL));

	ret = nvlist_lookup_uint64(nvl, zfs_prop_to_name(ZFS_PROP_PBKDF2_SALT),
	    &intval);
	if (ret != 0 || (is_passphrase == (intval == 0)))
		return (SET_ERROR(EINVAL));

	return (0);
}

void
dsl_crypto_recv_raw_key_sync(dsl_dataset_t *ds, nvlist_t *nvl, dmu_tx_t *tx)
{
	dsl_pool_t *dp = tx->tx_pool;
	objset_t *mos = dp->dp_meta_objset;
	dsl_dir_t *dd = ds->ds_dir;
	uint_t len;
	uint64_t rddobj, one = 1;
	uint8_t *keydata, *hmac_keydata, *iv, *mac;
	uint64_t crypt, key_guid, keyformat, iters, salt;
	uint64_t version = ZIO_CRYPT_KEY_CURRENT_VERSION;
	char *keylocation = "prompt";

	/* lookup the values we need to create the DSL Crypto Key */
	crypt = fnvlist_lookup_uint64(nvl, DSL_CRYPTO_KEY_CRYPTO_SUITE);
	key_guid = fnvlist_lookup_uint64(nvl, DSL_CRYPTO_KEY_GUID);
	keyformat = fnvlist_lookup_uint64(nvl,
	    zfs_prop_to_name(ZFS_PROP_KEYFORMAT));
	iters = fnvlist_lookup_uint64(nvl,
	    zfs_prop_to_name(ZFS_PROP_PBKDF2_ITERS));
	salt = fnvlist_lookup_uint64(nvl,
	    zfs_prop_to_name(ZFS_PROP_PBKDF2_SALT));
	VERIFY0(nvlist_lookup_uint8_array(nvl, DSL_CRYPTO_KEY_MASTER_KEY,
	    &keydata, &len));
	VERIFY0(nvlist_lookup_uint8_array(nvl, DSL_CRYPTO_KEY_HMAC_KEY,
	    &hmac_keydata, &len));
	VERIFY0(nvlist_lookup_uint8_array(nvl, DSL_CRYPTO_KEY_IV, &iv, &len));
	VERIFY0(nvlist_lookup_uint8_array(nvl, DSL_CRYPTO_KEY_MAC, &mac, &len));

	/* if this is a new dataset setup the DSL Crypto Key. */
	if (dd->dd_crypto_obj == 0) {
		/* zapify the dsl dir so we can add the key object to it */
		dmu_buf_will_dirty(dd->dd_dbuf, tx);
		dsl_dir_zapify(dd, tx);

		/* create the DSL Crypto Key on disk and activate the feature */
		dd->dd_crypto_obj = zap_create(mos,
		    DMU_OTN_ZAP_METADATA, DMU_OT_NONE, 0, tx);
		VERIFY0(zap_update(tx->tx_pool->dp_meta_objset,
		    dd->dd_crypto_obj, DSL_CRYPTO_KEY_REFCOUNT,
		    sizeof (uint64_t), 1, &one, tx));
		VERIFY0(zap_update(tx->tx_pool->dp_meta_objset,
		    dd->dd_crypto_obj, DSL_CRYPTO_KEY_VERSION,
		    sizeof (uint64_t), 1, &version, tx));

		dsl_dataset_activate_feature(ds->ds_object,
		    SPA_FEATURE_ENCRYPTION, (void *)B_TRUE, tx);
		ds->ds_feature[SPA_FEATURE_ENCRYPTION] = (void *)B_TRUE;

		/* save the dd_crypto_obj on disk */
		VERIFY0(zap_add(mos, dd->dd_object, DD_FIELD_CRYPTO_KEY_OBJ,
		    sizeof (uint64_t), 1, &dd->dd_crypto_obj, tx));

		/*
		 * Set the keylocation to prompt by default. If keylocation
		 * has been provided via the properties, this will be overridden
		 * later.
		 */
		dsl_prop_set_sync_impl(ds,
		    zfs_prop_to_name(ZFS_PROP_KEYLOCATION),
		    ZPROP_SRC_LOCAL, 1, strlen(keylocation) + 1,
		    keylocation, tx);

		rddobj = dd->dd_object;
	} else {
		VERIFY0(dsl_dir_get_encryption_root_ddobj(dd, &rddobj));
	}

	/* sync the key data to the ZAP object on disk */
	dsl_crypto_key_sync_impl(mos, dd->dd_crypto_obj, crypt,
	    rddobj, key_guid, iv, mac, keydata, hmac_keydata, keyformat, salt,
	    iters, tx);
}

static int
dsl_crypto_recv_key_check(void *arg, dmu_tx_t *tx)
{
	int ret;
	dsl_crypto_recv_key_arg_t *dcrka = arg;
	dsl_dataset_t *ds = NULL, *fromds = NULL;

	ret = dsl_dataset_hold_obj(tx->tx_pool, dcrka->dcrka_dsobj,
	    FTAG, &ds);
	if (ret != 0)
		goto out;

	if (dcrka->dcrka_fromobj != 0) {
		ret = dsl_dataset_hold_obj(tx->tx_pool, dcrka->dcrka_fromobj,
		    FTAG, &fromds);
		if (ret != 0)
			goto out;
	}

	ret = dsl_crypto_recv_raw_objset_check(ds, fromds,
	    dcrka->dcrka_ostype, dcrka->dcrka_nvl, tx);
	if (ret != 0)
		goto out;

	/*
	 * We run this check even if we won't be doing this part of
	 * the receive now so that we don't make the user wait until
	 * the receive finishes to fail.
	 */
	ret = dsl_crypto_recv_raw_key_check(ds, dcrka->dcrka_nvl, tx);
	if (ret != 0)
		goto out;

out:
	if (ds != NULL)
		dsl_dataset_rele(ds, FTAG);
	if (fromds != NULL)
		dsl_dataset_rele(fromds, FTAG);
	return (ret);
}

static void
dsl_crypto_recv_key_sync(void *arg, dmu_tx_t *tx)
{
	dsl_crypto_recv_key_arg_t *dcrka = arg;
	dsl_dataset_t *ds;

	VERIFY0(dsl_dataset_hold_obj(tx->tx_pool, dcrka->dcrka_dsobj,
	    FTAG, &ds));
	dsl_crypto_recv_raw_objset_sync(ds, dcrka->dcrka_ostype,
	    dcrka->dcrka_nvl, tx);
	if (dcrka->dcrka_do_key)
		dsl_crypto_recv_raw_key_sync(ds, dcrka->dcrka_nvl, tx);
	dsl_dataset_rele(ds, FTAG);
}

/*
 * This function is used to sync an nvlist representing a DSL Crypto Key and
 * the associated encryption parameters. The key will be written exactly as is
 * without wrapping it.
 */
int
dsl_crypto_recv_raw(const char *poolname, uint64_t dsobj, uint64_t fromobj,
    dmu_objset_type_t ostype, nvlist_t *nvl, boolean_t do_key)
{
	dsl_crypto_recv_key_arg_t dcrka;

	dcrka.dcrka_dsobj = dsobj;
	dcrka.dcrka_fromobj = fromobj;
	dcrka.dcrka_ostype = ostype;
	dcrka.dcrka_nvl = nvl;
	dcrka.dcrka_do_key = do_key;

	return (dsl_sync_task(poolname, dsl_crypto_recv_key_check,
	    dsl_crypto_recv_key_sync, &dcrka, 1, ZFS_SPACE_CHECK_NORMAL));
}

int
dsl_crypto_populate_key_nvlist(objset_t *os, uint64_t from_ivset_guid,
    nvlist_t **nvl_out)
{
	int ret;
	dsl_dataset_t *ds = os->os_dsl_dataset;
	dnode_t *mdn;
	uint64_t rddobj;
	nvlist_t *nvl = NULL;
	uint64_t dckobj = ds->ds_dir->dd_crypto_obj;
	dsl_dir_t *rdd = NULL;
	dsl_pool_t *dp = ds->ds_dir->dd_pool;
	objset_t *mos = dp->dp_meta_objset;
	uint64_t crypt = 0, key_guid = 0, format = 0;
	uint64_t iters = 0, salt = 0, version = 0;
	uint64_t to_ivset_guid = 0;
	uint8_t raw_keydata[MASTER_KEY_MAX_LEN];
	uint8_t raw_hmac_keydata[SHA512_HMAC_KEYLEN];
	uint8_t iv[WRAPPING_IV_LEN];
	uint8_t mac[WRAPPING_MAC_LEN];

	ASSERT(dckobj != 0);

	mdn = DMU_META_DNODE(os);

	nvl = fnvlist_alloc();

	/* lookup values from the DSL Crypto Key */
	ret = zap_lookup(mos, dckobj, DSL_CRYPTO_KEY_CRYPTO_SUITE, 8, 1,
	    &crypt);
	if (ret != 0)
		goto error;

	ret = zap_lookup(mos, dckobj, DSL_CRYPTO_KEY_GUID, 8, 1, &key_guid);
	if (ret != 0)
		goto error;

	ret = zap_lookup(mos, dckobj, DSL_CRYPTO_KEY_MASTER_KEY, 1,
	    MASTER_KEY_MAX_LEN, raw_keydata);
	if (ret != 0)
		goto error;

	ret = zap_lookup(mos, dckobj, DSL_CRYPTO_KEY_HMAC_KEY, 1,
	    SHA512_HMAC_KEYLEN, raw_hmac_keydata);
	if (ret != 0)
		goto error;

	ret = zap_lookup(mos, dckobj, DSL_CRYPTO_KEY_IV, 1, WRAPPING_IV_LEN,
	    iv);
	if (ret != 0)
		goto error;

	ret = zap_lookup(mos, dckobj, DSL_CRYPTO_KEY_MAC, 1, WRAPPING_MAC_LEN,
	    mac);
	if (ret != 0)
		goto error;

	/* see zfs_disable_ivset_guid_check tunable for errata info */
	ret = zap_lookup(mos, ds->ds_object, DS_FIELD_IVSET_GUID, 8, 1,
	    &to_ivset_guid);
	if (ret != 0)
		ASSERT3U(dp->dp_spa->spa_errata, !=, 0);

	/*
	 * We don't support raw sends of legacy on-disk formats. See the
	 * comment in dsl_crypto_recv_key_check() for details.
	 */
	ret = zap_lookup(mos, dckobj, DSL_CRYPTO_KEY_VERSION, 8, 1, &version);
	if (ret != 0 || version != ZIO_CRYPT_KEY_CURRENT_VERSION) {
		dp->dp_spa->spa_errata = ZPOOL_ERRATA_ZOL_6845_ENCRYPTION;
		ret = SET_ERROR(ENOTSUP);
		goto error;
	}

	/*
	 * Lookup wrapping key properties. An early version of the code did
	 * not correctly add these values to the wrapping key or the DSL
	 * Crypto Key on disk for non encryption roots, so to be safe we
	 * always take the slightly circuitous route of looking it up from
	 * the encryption root's key.
	 */
	ret = dsl_dir_get_encryption_root_ddobj(ds->ds_dir, &rddobj);
	if (ret != 0)
		goto error;

	dsl_pool_config_enter(dp, FTAG);

	ret = dsl_dir_hold_obj(dp, rddobj, NULL, FTAG, &rdd);
	if (ret != 0)
		goto error_unlock;

	ret = zap_lookup(dp->dp_meta_objset, rdd->dd_crypto_obj,
	    zfs_prop_to_name(ZFS_PROP_KEYFORMAT), 8, 1, &format);
	if (ret != 0)
		goto error_unlock;

	if (format == ZFS_KEYFORMAT_PASSPHRASE) {
		ret = zap_lookup(dp->dp_meta_objset, rdd->dd_crypto_obj,
		    zfs_prop_to_name(ZFS_PROP_PBKDF2_ITERS), 8, 1, &iters);
		if (ret != 0)
			goto error_unlock;

		ret = zap_lookup(dp->dp_meta_objset, rdd->dd_crypto_obj,
		    zfs_prop_to_name(ZFS_PROP_PBKDF2_SALT), 8, 1, &salt);
		if (ret != 0)
			goto error_unlock;
	}

	dsl_dir_rele(rdd, FTAG);
	dsl_pool_config_exit(dp, FTAG);

	fnvlist_add_uint64(nvl, DSL_CRYPTO_KEY_CRYPTO_SUITE, crypt);
	fnvlist_add_uint64(nvl, DSL_CRYPTO_KEY_GUID, key_guid);
	fnvlist_add_uint64(nvl, DSL_CRYPTO_KEY_VERSION, version);
	VERIFY0(nvlist_add_uint8_array(nvl, DSL_CRYPTO_KEY_MASTER_KEY,
	    raw_keydata, MASTER_KEY_MAX_LEN));
	VERIFY0(nvlist_add_uint8_array(nvl, DSL_CRYPTO_KEY_HMAC_KEY,
	    raw_hmac_keydata, SHA512_HMAC_KEYLEN));
	VERIFY0(nvlist_add_uint8_array(nvl, DSL_CRYPTO_KEY_IV, iv,
	    WRAPPING_IV_LEN));
	VERIFY0(nvlist_add_uint8_array(nvl, DSL_CRYPTO_KEY_MAC, mac,
	    WRAPPING_MAC_LEN));
	VERIFY0(nvlist_add_uint8_array(nvl, "portable_mac",
	    os->os_phys->os_portable_mac, ZIO_OBJSET_MAC_LEN));
	fnvlist_add_uint64(nvl, zfs_prop_to_name(ZFS_PROP_KEYFORMAT), format);
	fnvlist_add_uint64(nvl, zfs_prop_to_name(ZFS_PROP_PBKDF2_ITERS), iters);
	fnvlist_add_uint64(nvl, zfs_prop_to_name(ZFS_PROP_PBKDF2_SALT), salt);
	fnvlist_add_uint64(nvl, "mdn_checksum", mdn->dn_checksum);
	fnvlist_add_uint64(nvl, "mdn_compress", mdn->dn_compress);
	fnvlist_add_uint64(nvl, "mdn_nlevels", mdn->dn_nlevels);
	fnvlist_add_uint64(nvl, "mdn_blksz", mdn->dn_datablksz);
	fnvlist_add_uint64(nvl, "mdn_indblkshift", mdn->dn_indblkshift);
	fnvlist_add_uint64(nvl, "mdn_nblkptr", mdn->dn_nblkptr);
	fnvlist_add_uint64(nvl, "mdn_maxblkid", mdn->dn_maxblkid);
	fnvlist_add_uint64(nvl, "to_ivset_guid", to_ivset_guid);
	fnvlist_add_uint64(nvl, "from_ivset_guid", from_ivset_guid);

	*nvl_out = nvl;
	return (0);

error_unlock:
	dsl_pool_config_exit(dp, FTAG);
error:
	if (rdd != NULL)
		dsl_dir_rele(rdd, FTAG);
	nvlist_free(nvl);

	*nvl_out = NULL;
	return (ret);
}

uint64_t
dsl_crypto_key_create_sync(uint64_t crypt, dsl_wrapping_key_t *wkey,
    dmu_tx_t *tx)
{
	dsl_crypto_key_t dck;
	uint64_t version = ZIO_CRYPT_KEY_CURRENT_VERSION;
	uint64_t one = 1ULL;

	ASSERT(dmu_tx_is_syncing(tx));
	ASSERT3U(crypt, <, ZIO_CRYPT_FUNCTIONS);
	ASSERT3U(crypt, >, ZIO_CRYPT_OFF);

	/* create the DSL Crypto Key ZAP object */
	dck.dck_obj = zap_create(tx->tx_pool->dp_meta_objset,
	    DMU_OTN_ZAP_METADATA, DMU_OT_NONE, 0, tx);

	/* fill in the key (on the stack) and sync it to disk */
	dck.dck_wkey = wkey;
	VERIFY0(zio_crypt_key_init(crypt, &dck.dck_key));

	dsl_crypto_key_sync(&dck, tx);
	VERIFY0(zap_update(tx->tx_pool->dp_meta_objset, dck.dck_obj,
	    DSL_CRYPTO_KEY_REFCOUNT, sizeof (uint64_t), 1, &one, tx));
	VERIFY0(zap_update(tx->tx_pool->dp_meta_objset, dck.dck_obj,
	    DSL_CRYPTO_KEY_VERSION, sizeof (uint64_t), 1, &version, tx));

	zio_crypt_key_destroy(&dck.dck_key);
	bzero(&dck.dck_key, sizeof (zio_crypt_key_t));

	return (dck.dck_obj);
}

uint64_t
dsl_crypto_key_clone_sync(dsl_dir_t *origindd, dmu_tx_t *tx)
{
	objset_t *mos = tx->tx_pool->dp_meta_objset;

	ASSERT(dmu_tx_is_syncing(tx));

	VERIFY0(zap_increment(mos, origindd->dd_crypto_obj,
	    DSL_CRYPTO_KEY_REFCOUNT, 1, tx));

	return (origindd->dd_crypto_obj);
}

void
dsl_crypto_key_destroy_sync(uint64_t dckobj, dmu_tx_t *tx)
{
	objset_t *mos = tx->tx_pool->dp_meta_objset;
	uint64_t refcnt;

	/* Decrement the refcount, destroy if this is the last reference */
	VERIFY0(zap_lookup(mos, dckobj, DSL_CRYPTO_KEY_REFCOUNT,
	    sizeof (uint64_t), 1, &refcnt));

	if (refcnt != 1) {
		VERIFY0(zap_increment(mos, dckobj, DSL_CRYPTO_KEY_REFCOUNT,
		    -1, tx));
	} else {
		VERIFY0(zap_destroy(mos, dckobj, tx));
	}
}

void
dsl_dataset_crypt_stats(dsl_dataset_t *ds, nvlist_t *nv)
{
	uint64_t intval;
	dsl_dir_t *dd = ds->ds_dir;
	dsl_dir_t *enc_root;
	char buf[ZFS_MAX_DATASET_NAME_LEN];

	if (dd->dd_crypto_obj == 0)
		return;

	intval = dsl_dataset_get_keystatus(dd);
	dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_KEYSTATUS, intval);

	if (dsl_dir_get_crypt(dd, &intval) == 0)
		dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_ENCRYPTION, intval);
	if (zap_lookup(dd->dd_pool->dp_meta_objset, dd->dd_crypto_obj,
	    DSL_CRYPTO_KEY_GUID, 8, 1, &intval) == 0) {
		dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_KEY_GUID, intval);
	}
	if (zap_lookup(dd->dd_pool->dp_meta_objset, dd->dd_crypto_obj,
	    zfs_prop_to_name(ZFS_PROP_KEYFORMAT), 8, 1, &intval) == 0) {
		dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_KEYFORMAT, intval);
	}
	if (zap_lookup(dd->dd_pool->dp_meta_objset, dd->dd_crypto_obj,
	    zfs_prop_to_name(ZFS_PROP_PBKDF2_SALT), 8, 1, &intval) == 0) {
		dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_PBKDF2_SALT, intval);
	}
	if (zap_lookup(dd->dd_pool->dp_meta_objset, dd->dd_crypto_obj,
	    zfs_prop_to_name(ZFS_PROP_PBKDF2_ITERS), 8, 1, &intval) == 0) {
		dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_PBKDF2_ITERS, intval);
	}
	if (zap_lookup(dd->dd_pool->dp_meta_objset, ds->ds_object,
	    DS_FIELD_IVSET_GUID, 8, 1, &intval) == 0) {
		dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_IVSET_GUID, intval);
	}

	if (dsl_dir_get_encryption_root_ddobj(dd, &intval) == 0) {
		if (dsl_dir_hold_obj(dd->dd_pool, intval, NULL, FTAG,
		    &enc_root) == 0) {
			dsl_dir_name(enc_root, buf);
			dsl_dir_rele(enc_root, FTAG);
			dsl_prop_nvlist_add_string(nv,
			    ZFS_PROP_ENCRYPTION_ROOT, buf);
		}
	}
}

int
spa_crypt_get_salt(spa_t *spa, uint64_t dsobj, uint8_t *salt)
{
	int ret;
	dsl_crypto_key_t *dck = NULL;

	/* look up the key from the spa's keystore */
	ret = spa_keystore_lookup_key(spa, dsobj, FTAG, &dck);
	if (ret != 0)
		goto error;

	ret = zio_crypt_key_get_salt(&dck->dck_key, salt);
	if (ret != 0)
		goto error;

	spa_keystore_dsl_key_rele(spa, dck, FTAG);
	return (0);

error:
	if (dck != NULL)
		spa_keystore_dsl_key_rele(spa, dck, FTAG);
	return (ret);
}

/*
 * Objset blocks are a special case for MAC generation. These blocks have 2
 * 256-bit MACs which are embedded within the block itself, rather than a
 * single 128 bit MAC. As a result, this function handles encoding and decoding
 * the MACs on its own, unlike other functions in this file.
 */
int
spa_do_crypt_objset_mac_abd(boolean_t generate, spa_t *spa, uint64_t dsobj,
    abd_t *abd, uint_t datalen, boolean_t byteswap)
{
	int ret;
	dsl_crypto_key_t *dck = NULL;
	void *buf = abd_borrow_buf_copy(abd, datalen);
	objset_phys_t *osp = buf;
	uint8_t portable_mac[ZIO_OBJSET_MAC_LEN];
	uint8_t local_mac[ZIO_OBJSET_MAC_LEN];

	/* look up the key from the spa's keystore */
	ret = spa_keystore_lookup_key(spa, dsobj, FTAG, &dck);
	if (ret != 0)
		goto error;

	/* calculate both HMACs */
	ret = zio_crypt_do_objset_hmacs(&dck->dck_key, buf, datalen,
	    byteswap, portable_mac, local_mac);
	if (ret != 0)
		goto error;

	spa_keystore_dsl_key_rele(spa, dck, FTAG);

	/* if we are generating encode the HMACs in the objset_phys_t */
	if (generate) {
		bcopy(portable_mac, osp->os_portable_mac, ZIO_OBJSET_MAC_LEN);
		bcopy(local_mac, osp->os_local_mac, ZIO_OBJSET_MAC_LEN);
		abd_return_buf_copy(abd, buf, datalen);
		return (0);
	}

	if (bcmp(portable_mac, osp->os_portable_mac, ZIO_OBJSET_MAC_LEN) != 0 ||
	    bcmp(local_mac, osp->os_local_mac, ZIO_OBJSET_MAC_LEN) != 0) {
		abd_return_buf(abd, buf, datalen);
		return (SET_ERROR(ECKSUM));
	}

	abd_return_buf(abd, buf, datalen);

	return (0);

error:
	if (dck != NULL)
		spa_keystore_dsl_key_rele(spa, dck, FTAG);
	abd_return_buf(abd, buf, datalen);
	return (ret);
}

int
spa_do_crypt_mac_abd(boolean_t generate, spa_t *spa, uint64_t dsobj, abd_t *abd,
    uint_t datalen, uint8_t *mac)
{
	int ret;
	dsl_crypto_key_t *dck = NULL;
	uint8_t *buf = abd_borrow_buf_copy(abd, datalen);
	uint8_t digestbuf[ZIO_DATA_MAC_LEN];

	/* look up the key from the spa's keystore */
	ret = spa_keystore_lookup_key(spa, dsobj, FTAG, &dck);
	if (ret != 0)
		goto error;

	/* perform the hmac */
	ret = zio_crypt_do_hmac(&dck->dck_key, buf, datalen,
	    digestbuf, ZIO_DATA_MAC_LEN);
	if (ret != 0)
		goto error;

	abd_return_buf(abd, buf, datalen);
	spa_keystore_dsl_key_rele(spa, dck, FTAG);

	/*
	 * Truncate and fill in mac buffer if we were asked to generate a MAC.
	 * Otherwise verify that the MAC matched what we expected.
	 */
	if (generate) {
		bcopy(digestbuf, mac, ZIO_DATA_MAC_LEN);
		return (0);
	}

	if (bcmp(digestbuf, mac, ZIO_DATA_MAC_LEN) != 0)
		return (SET_ERROR(ECKSUM));

	return (0);

error:
	if (dck != NULL)
		spa_keystore_dsl_key_rele(spa, dck, FTAG);
	abd_return_buf(abd, buf, datalen);
	return (ret);
}

/*
 * This function serves as a multiplexer for encryption and decryption of
 * all blocks (except the L2ARC). For encryption, it will populate the IV,
 * salt, MAC, and cabd (the ciphertext). On decryption it will simply use
 * these fields to populate pabd (the plaintext).
 */
int
spa_do_crypt_abd(boolean_t encrypt, spa_t *spa, const zbookmark_phys_t *zb,
    dmu_object_type_t ot, boolean_t dedup, boolean_t bswap, uint8_t *salt,
    uint8_t *iv, uint8_t *mac, uint_t datalen, abd_t *pabd, abd_t *cabd,
    boolean_t *no_crypt)
{
	int ret;
	dsl_crypto_key_t *dck = NULL;
	uint8_t *plainbuf = NULL, *cipherbuf = NULL;

	ASSERT(spa_feature_is_active(spa, SPA_FEATURE_ENCRYPTION));

	/* look up the key from the spa's keystore */
	ret = spa_keystore_lookup_key(spa, zb->zb_objset, FTAG, &dck);
	if (ret != 0) {
		ret = SET_ERROR(EACCES);
		return (ret);
	}

	if (encrypt) {
		plainbuf = abd_borrow_buf_copy(pabd, datalen);
		cipherbuf = abd_borrow_buf(cabd, datalen);
	} else {
		plainbuf = abd_borrow_buf(pabd, datalen);
		cipherbuf = abd_borrow_buf_copy(cabd, datalen);
	}

	/*
	 * Both encryption and decryption functions need a salt for key
	 * generation and an IV. When encrypting a non-dedup block, we
	 * generate the salt and IV randomly to be stored by the caller. Dedup
	 * blocks perform a (more expensive) HMAC of the plaintext to obtain
	 * the salt and the IV. ZIL blocks have their salt and IV generated
	 * at allocation time in zio_alloc_zil(). On decryption, we simply use
	 * the provided values.
	 */
	if (encrypt && ot != DMU_OT_INTENT_LOG && !dedup) {
		ret = zio_crypt_key_get_salt(&dck->dck_key, salt);
		if (ret != 0)
			goto error;

		ret = zio_crypt_generate_iv(iv);
		if (ret != 0)
			goto error;
	} else if (encrypt && dedup) {
		ret = zio_crypt_generate_iv_salt_dedup(&dck->dck_key,
		    plainbuf, datalen, iv, salt);
		if (ret != 0)
			goto error;
	}

	/* call lower level function to perform encryption / decryption */
	ret = zio_do_crypt_data(encrypt, &dck->dck_key, ot, bswap, salt, iv,
	    mac, datalen, plainbuf, cipherbuf, no_crypt);

	/*
	 * Handle injected decryption faults. Unfortunately, we cannot inject
	 * faults for dnode blocks because we might trigger the panic in
	 * dbuf_prepare_encrypted_dnode_leaf(), which exists because syncing
	 * context is not prepared to handle malicious decryption failures.
	 */
	if (zio_injection_enabled && !encrypt && ot != DMU_OT_DNODE && ret == 0)
		ret = zio_handle_decrypt_injection(spa, zb, ot, ECKSUM);
	if (ret != 0)
		goto error;

	if (encrypt) {
		abd_return_buf(pabd, plainbuf, datalen);
		abd_return_buf_copy(cabd, cipherbuf, datalen);
	} else {
		abd_return_buf_copy(pabd, plainbuf, datalen);
		abd_return_buf(cabd, cipherbuf, datalen);
	}

	spa_keystore_dsl_key_rele(spa, dck, FTAG);

	return (0);

error:
	if (encrypt) {
		/* zero out any state we might have changed while encrypting */
		bzero(salt, ZIO_DATA_SALT_LEN);
		bzero(iv, ZIO_DATA_IV_LEN);
		bzero(mac, ZIO_DATA_MAC_LEN);
		abd_return_buf(pabd, plainbuf, datalen);
		abd_return_buf_copy(cabd, cipherbuf, datalen);
	} else {
		abd_return_buf_copy(pabd, plainbuf, datalen);
		abd_return_buf(cabd, cipherbuf, datalen);
	}

	spa_keystore_dsl_key_rele(spa, dck, FTAG);

	return (ret);
}

ZFS_MODULE_PARAM(zfs, zfs_, disable_ivset_guid_check, INT, ZMOD_RW,
	"Set to allow raw receives without IVset guids");
