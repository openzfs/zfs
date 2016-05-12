/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright (c) 2016, Datto, Inc. All rights reserved.
 */

#include <sys/dsl_crypt.h>
#include <sys/dsl_pool.h>
#include <sys/zap.h>
#include <sys/zil.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_prop.h>
#include <sys/spa_impl.h>
#include <sys/zvol.h>

/*
 * This file's primary purpose is for managing master encryption keys in
 * memory and on disk. For more info on how these keys are used, see the
 * block comment in zio_crypt.c.
 *
 * All master keys are stored encrypted on disk in the form of the DSL
 * Crypto Key ZAP object. The binary key data in this object is always
 * randomly generated and is encrypted with the user's secret key. This
 * layer of indirection allows the user to change their key without
 * needing to re-encrypt the entire dataset. The ZAP also holds on to the
 * (non-encrypted) encryption algorithm identifier, IV, and MAC needed to
 * safely decrypt the master key. For more info on the user's key see the
 * block comment in libzfs_crypto.c
 *
 * In memory encryption keys are managed through the spa_keystore. The
 * keystore consists of 3 AVL trees, which are as follows:
 *
 * The Wrapping Key Tree:
 * The wrapping key (wkey) tree stores the user's keys that are fed into the
 * kernel through 'zfs key -K' and related commands. Datasets inherit their
 * parent's wkey, so they are refcounted. The wrapping keys remain in memory
 * until they are explicitly unloaded (with "zfs key -u"). Unloading is only
 * possible when no datasets are using them (refcount=0).
 *
 * The DSL Crypto Key Tree:
 * The DSL Crypto Keys are the in-memory representation of decrypted master
 * keys. They are used by the functions in zio_crypt.c to perform encryption
 * and decryption. The decrypted master key bit patterns are shared between
 * all datasets within a "clone family", but each clone may use a different
 * wrapping key. As a result, we maintain one of these structs for each clone
 * to allow us to manage the loading and unloading of each separately.
 * Snapshots of a given dataset, however, will share a DSL Crypto Key, so they
 * are also refcounted. Once the refcount on a key hits zero, it is immediately
 * zeroed out and freed.
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

void
dsl_wrapping_key_hold(dsl_wrapping_key_t *wkey, void *tag)
{
	(void) refcount_add(&wkey->wk_refcnt, tag);
}

void
dsl_wrapping_key_rele(dsl_wrapping_key_t *wkey, void *tag)
{
	(void) refcount_remove(&wkey->wk_refcnt, tag);
}

void
dsl_wrapping_key_free(dsl_wrapping_key_t *wkey)
{
	ASSERT0(refcount_count(&wkey->wk_refcnt));

	if (wkey->wk_key.ck_data) {
		bzero(wkey->wk_key.ck_data,
		    BITS_TO_BYTES(wkey->wk_key.ck_length));
		kmem_free(wkey->wk_key.ck_data,
		    BITS_TO_BYTES(wkey->wk_key.ck_length));
	}

	refcount_destroy(&wkey->wk_refcnt);
	kmem_free(wkey, sizeof (dsl_wrapping_key_t));
}

int
dsl_wrapping_key_create(uint8_t *wkeydata, dsl_wrapping_key_t **wkey_out)
{
	int ret;
	dsl_wrapping_key_t *wkey;

	/* allocate the wrapping key */
	wkey = kmem_alloc(sizeof (dsl_wrapping_key_t), KM_SLEEP);
	if (!wkey)
		return (SET_ERROR(ENOMEM));

	/* allocate and initialize the underlying crypto key */
	wkey->wk_key.ck_data = kmem_alloc(WRAPPING_KEY_LEN, KM_SLEEP);
	if (!wkey->wk_key.ck_data) {
		ret = ENOMEM;
		goto error;
	}

	wkey->wk_key.ck_format = CRYPTO_KEY_RAW;
	wkey->wk_key.ck_length = BYTES_TO_BITS(WRAPPING_KEY_LEN);

	/* copy the data */
	bcopy(wkeydata, wkey->wk_key.ck_data, WRAPPING_KEY_LEN);

	/* initialize the refcount */
	refcount_create(&wkey->wk_refcnt);

	*wkey_out = wkey;
	return (0);

error:
	dsl_wrapping_key_free(wkey);

	*wkey_out = NULL;
	return (ret);
}

int
dsl_crypto_params_create_nvlist(nvlist_t *props, nvlist_t *crypto_args,
    dsl_crypto_params_t **dcp_out)
{
	int ret;
	dsl_crypto_params_t *dcp = NULL;
	dsl_wrapping_key_t *wkey = NULL;
	boolean_t crypt_exists = B_TRUE, wkeydata_exists = B_TRUE;
	boolean_t keysource_exists = B_TRUE, salt_exists = B_TRUE;
	boolean_t iters_exists = B_TRUE, cmd_exists = B_TRUE;
	char *keysource = NULL;
	uint64_t iters = 0, salt = 0, crypt = 0, cmd = ZFS_IOC_KEY_CMD_NONE;
	uint8_t *wkeydata;
	uint_t wkeydata_len;

	/* get relevant properties from the nvlist */
	if (props) {
		ret = nvlist_lookup_uint64(props,
		    zfs_prop_to_name(ZFS_PROP_ENCRYPTION), &crypt);
		if (ret)
			crypt_exists = B_FALSE;

		ret = nvlist_lookup_string(props,
		    zfs_prop_to_name(ZFS_PROP_KEYSOURCE), &keysource);
		if (ret)
			keysource_exists = B_FALSE;

		ret = nvlist_lookup_uint64(props,
		    zfs_prop_to_name(ZFS_PROP_SALT), &salt);
		if (ret)
			salt_exists = B_FALSE;

		ret = nvlist_lookup_uint64(props,
		    zfs_prop_to_name(ZFS_PROP_PBKDF2_ITERS), &iters);
		if (ret)
			iters_exists = B_FALSE;

		ret = nvlist_lookup_uint64(props, "crypto_cmd", &cmd);
		if (ret)
			cmd_exists = B_FALSE;
	} else {
		crypt_exists = B_FALSE;
		keysource_exists = B_FALSE;
		salt_exists = B_FALSE;
		iters_exists = B_FALSE;
		cmd_exists = B_FALSE;
	}

	if (crypto_args) {
		ret = nvlist_lookup_uint8_array(crypto_args, "wkeydata",
		    &wkeydata, &wkeydata_len);
		if (ret)
			wkeydata_exists = B_FALSE;
	} else {
		wkeydata_exists = B_FALSE;
	}

	/* no parameters are valid; results in inherited crypto settings */
	if (!crypt_exists && !keysource_exists && !wkeydata_exists &&
	    !salt_exists && !cmd_exists && !iters_exists) {
		*dcp_out = NULL;
		return (0);
	}

	dcp = kmem_alloc(sizeof (dsl_crypto_params_t), KM_SLEEP);
	if (!dcp) {
		ret = SET_ERROR(ENOMEM);
		goto error;
	}

	/* check wrapping key length */
	if (wkeydata_exists && wkeydata_len != WRAPPING_KEY_LEN) {
		ret = SET_ERROR(EINVAL);
		goto error;
	}

	/* specifying a keysource requires keydata */
	if (keysource_exists && !wkeydata_exists) {
		ret = SET_ERROR(EINVAL);
		goto error;
	}

	/* remove crypto_cmd from props since it should not be used again */
	if (cmd_exists)
		(void) nvlist_remove_all(props, "crypto_cmd");

	/* if the user asked for the deault crypt, decide that now */
	if (crypt == ZIO_CRYPT_ON) {
		crypt = ZIO_CRYPT_ON_VALUE;

		ret = nvlist_remove_all(props,
		    zfs_prop_to_name(ZFS_PROP_ENCRYPTION));
		if (ret)
			goto error;
		ret = nvlist_add_uint64(props,
		    zfs_prop_to_name(ZFS_PROP_ENCRYPTION), crypt);
		if (ret)
			goto error;
	}

	/* create the wrapping key from the raw data */
	if (wkeydata_exists) {
		/* create the wrapping key with the verified parameters */
		ret = dsl_wrapping_key_create(wkeydata, &wkey);
		if (ret)
			goto error;
	}

	dcp->cp_cmd = cmd;
	dcp->cp_crypt = crypt;
	dcp->cp_salt = salt;
	dcp->cp_iters = iters;
	dcp->cp_keysource = keysource;
	dcp->cp_wkey = wkey;
	*dcp_out = dcp;

	return (0);

error:
	if (wkey)
		dsl_wrapping_key_free(wkey);
	if (dcp)
		kmem_free(dcp, sizeof (dsl_crypto_params_t));

	*dcp_out = NULL;
	return (ret);
}

void
dsl_crypto_params_free(dsl_crypto_params_t *dcp, boolean_t unload)
{
	if (!dcp)
		return;

	if (unload)
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
dsl_dir_hold_keysource_source_dd(dsl_dir_t *dd, void *tag,
    dsl_dir_t **inherit_dd_out)
{
	int ret;
	dsl_dir_t *inherit_dd = NULL;
	char keysource[MAXNAMELEN];
	char setpoint[MAXNAMELEN];

	/*
	 * lookup dd's keysource property and find
	 * out where it was inherited from
	 */
	ret = dsl_prop_get_dd(dd, zfs_prop_to_name(ZFS_PROP_KEYSOURCE),
	    1, sizeof (keysource), keysource, setpoint, B_FALSE);
	if (ret)
		goto error;

	/* hold the dsl dir that we inherited the property from */
	ret = dsl_dir_hold(dd->dd_pool, setpoint, tag, &inherit_dd, NULL);
	if (ret)
		goto error;

	*inherit_dd_out = inherit_dd;
	return (0);

error:
	*inherit_dd_out = NULL;
	return (ret);
}

zfs_keystatus_t
dsl_dataset_keystore_keystatus(dsl_dataset_t *ds)
{
	int ret;
	dsl_wrapping_key_t *wkey;

	/* check if this dataset has a owns a dsl key */
	if (ds->ds_dir->dd_crypto_obj == 0)
		return (ZFS_KEYSTATUS_NONE);

	/* lookup the wkey. if it doesn't exist the key is unavailable */
	ret = spa_keystore_wkey_hold_ddobj(ds->ds_dir->dd_pool->dp_spa,
	    ds->ds_dir->dd_object, FTAG, &wkey);
	if (ret)
		return (ZFS_KEYSTATUS_UNAVAILABLE);

	dsl_wrapping_key_rele(wkey, FTAG);

	return (ZFS_KEYSTATUS_AVAILABLE);
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

int
spa_keystore_wkey_hold_ddobj(spa_t *spa, uint64_t ddobj, void *tag,
    dsl_wrapping_key_t **wkey_out)
{
	int ret;
	dsl_pool_t *dp = spa_get_dsl(spa);
	dsl_dir_t *dd = NULL, *inherit_dd = NULL;
	dsl_wrapping_key_t *wkey;
	boolean_t locked = B_FALSE;

	if (!RW_WRITE_HELD(&dp->dp_spa->spa_keystore.sk_wkeys_lock)) {
		rw_enter(&spa->spa_keystore.sk_wkeys_lock, RW_READER);
		locked = B_TRUE;
	}

	/*
	 * There is a special case in zfs_create_fs() where the wrapping key
	 * is needed before the filesystem's properties are set. This is
	 * problematic because dsl_dir_hold_keysource_source_dd() uses the
	 * properties to determine where the wrapping key is inherited from.
	 * As a result, here we try to find a wrapping key for this dd before
	 * checking for wrapping key inheritance.
	 */
	ret = spa_keystore_wkey_hold_ddobj_impl(spa, ddobj, tag, &wkey);
	if (ret == 0) {
		if (locked)
			rw_exit(&spa->spa_keystore.sk_wkeys_lock);

		*wkey_out = wkey;
		return (0);
	}

	/* hold the dsl dir */
	ret = dsl_dir_hold_obj(dp, ddobj, NULL, FTAG, &dd);
	if (ret)
		goto error;

	/* get the dd that the keysource property was inherited from */
	ret = dsl_dir_hold_keysource_source_dd(dd, FTAG, &inherit_dd);
	if (ret)
		goto error;

	/* lookup the wkey in the avl tree */
	ret = spa_keystore_wkey_hold_ddobj_impl(spa, inherit_dd->dd_object,
	    tag, &wkey);
	if (ret)
		goto error;

	/* unlock the wkey tree if we locked it */
	if (locked)
		rw_exit(&spa->spa_keystore.sk_wkeys_lock);

	dsl_dir_rele(inherit_dd, FTAG);
	dsl_dir_rele(dd, FTAG);

	*wkey_out = wkey;
	return (0);

error:
	if (locked)
		rw_exit(&spa->spa_keystore.sk_wkeys_lock);
	if (inherit_dd)
		dsl_dir_rele(inherit_dd, FTAG);
	if (dd)
		dsl_dir_rele(dd, FTAG);

	*wkey_out = NULL;
	return (ret);
}

static void
dsl_crypto_key_free(dsl_crypto_key_t *dck)
{
	ASSERT(refcount_count(&dck->dck_refcnt) == 0);

	/* destroy the zio_crypt_key_t */
	zio_crypt_key_destroy(&dck->dck_key);

	/* free the refcount, wrapping key, and lock */
	refcount_destroy(&dck->dck_refcnt);
	if (dck->dck_wkey)
		dsl_wrapping_key_rele(dck->dck_wkey, dck);

	/* free the key */
	kmem_free(dck, sizeof (dsl_crypto_key_t));
}

static void
dsl_crypto_key_rele(dsl_crypto_key_t *dck, void *tag)
{
	if (refcount_remove(&dck->dck_refcnt, tag) == 0)
		dsl_crypto_key_free(dck);
}

static int
dsl_crypto_key_open(objset_t *mos, dsl_wrapping_key_t *wkey,
    uint64_t dckobj, void *tag, dsl_crypto_key_t **dck_out)
{
	int ret;
	uint64_t crypt = 0;
	uint8_t raw_keydata[MAX_MASTER_KEY_LEN];
	uint8_t raw_hmac_keydata[HMAC_SHA256_KEYLEN];
	uint8_t iv[WRAPPING_IV_LEN];
	uint8_t mac[WRAPPING_MAC_LEN];
	dsl_crypto_key_t *dck;

	/* allocate and initialize the key */
	dck = kmem_zalloc(sizeof (dsl_crypto_key_t), KM_SLEEP);
	if (!dck)
		return (SET_ERROR(ENOMEM));

	/* fetch all of the values we need from the ZAP */
	ret = zap_lookup(mos, dckobj, DSL_CRYPTO_KEY_CRYPT, 8, 1, &crypt);
	if (ret)
		goto error;

	ret = zap_lookup(mos, dckobj, DSL_CRYPTO_KEY_MASTER_BUF, 1,
	    MAX_MASTER_KEY_LEN, raw_keydata);
	if (ret)
		goto error;

	ret = zap_lookup(mos, dckobj, DSL_CRYPTO_KEY_HMAC_KEY_BUF, 1,
	    HMAC_SHA256_KEYLEN, raw_hmac_keydata);
	if (ret)
		goto error;

	ret = zap_lookup(mos, dckobj, DSL_CRYPTO_KEY_IV, 1, WRAPPING_IV_LEN,
	    iv);
	if (ret)
		goto error;

	ret = zap_lookup(mos, dckobj, DSL_CRYPTO_KEY_MAC, 1, WRAPPING_MAC_LEN,
	    mac);
	if (ret)
		goto error;

	/*
	 * Unwrap the keys. If there is an error return EACCES to indicate
	 * an authentication failure.
	 */
	ret = zio_crypt_key_unwrap(&wkey->wk_key, crypt, raw_keydata,
	    raw_hmac_keydata, iv, mac, &dck->dck_key);
	if (ret) {
		ret = SET_ERROR(EACCES);
		goto error;
	}

	/* finish initializing the dsl_crypto_key_t */
	refcount_create(&dck->dck_refcnt);
	dsl_wrapping_key_hold(wkey, dck);
	dck->dck_wkey = wkey;
	dck->dck_obj = dckobj;
	refcount_add(&dck->dck_refcnt, tag);

	*dck_out = dck;
	return (0);

error:
	if (dck) {
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
	refcount_add(&found_dck->dck_refcnt, tag);

	*dck_out = found_dck;
	return (0);

error:
	*dck_out = NULL;
	return (ret);
}

int
spa_keystore_dsl_key_hold_dd(spa_t *spa, dsl_dir_t *dd, void *tag,
    dsl_crypto_key_t **dck_out)
{
	int ret;
	avl_index_t where;
	dsl_crypto_key_t *dck = NULL;
	dsl_wrapping_key_t *wkey = NULL;
	uint64_t dckobj = dd->dd_crypto_obj;

	/*
	 * we need a write lock here because we might load a dsl key
	 * from disk if we don't have it in the keystore already.
	 * This could be a problem because this lock also allows the zio
	 * layer to access the keys, but this function should only be
	 * called during key loading, encrypted dataset mounting, encrypted
	 * dataset creation, etc. so this is probably ok. If it becomes a
	 * problem an RCU-like implementation could make sense here.
	 */
	rw_enter(&spa->spa_keystore.sk_dk_lock, RW_WRITER);

	/* lookup the key in the tree of currently loaded keys */
	ret = spa_keystore_dsl_key_hold_impl(spa, dckobj, tag, &dck);
	if (!ret) {
		rw_exit(&spa->spa_keystore.sk_dk_lock);
		*dck_out = dck;
		return (0);
	}

	/* lookup the wrapping key from the keystore */
	ret = spa_keystore_wkey_hold_ddobj(spa, dd->dd_object, FTAG, &wkey);
	if (ret) {
		ret = SET_ERROR(EACCES);
		goto error_unlock;
	}

	/* read the key from disk */
	ret = dsl_crypto_key_open(spa_get_dsl(spa)->dp_meta_objset, wkey,
	    dckobj, tag, &dck);
	if (ret)
		goto error_unlock;

	/*
	 * add the key to the keystore (this should always succeed
	 * since we made sure it didn't exist before)
	 */
	avl_find(&spa->spa_keystore.sk_dsl_keys, dck, &where);
	avl_insert(&spa->spa_keystore.sk_dsl_keys, dck, where);

	/* release the wrapping key (the dsl key now has a reference to it) */
	dsl_wrapping_key_rele(wkey, FTAG);

	rw_exit(&spa->spa_keystore.sk_dk_lock);

	*dck_out = dck;
	return (0);

error_unlock:
	rw_exit(&spa->spa_keystore.sk_dk_lock);
	if (wkey)
		dsl_wrapping_key_rele(wkey, FTAG);

	*dck_out = NULL;
	return (ret);
}

void
spa_keystore_dsl_key_rele(spa_t *spa, dsl_crypto_key_t *dck, void *tag)
{
	rw_enter(&spa->spa_keystore.sk_dk_lock, RW_WRITER);

	if (refcount_remove(&dck->dck_refcnt, tag) == 0) {
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
	if (found_wkey) {
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
spa_keystore_load_wkey(const char *dsname, dsl_crypto_params_t *dcp)
{
	int ret;
	dsl_dir_t *dd = NULL;
	dsl_crypto_key_t *dck = NULL;
	dsl_wrapping_key_t *wkey = dcp->cp_wkey;
	dsl_pool_t *dp = NULL;

	if (!dcp || !dcp->cp_wkey)
		return (SET_ERROR(EINVAL));
	if (dcp->cp_crypt || dcp->cp_keysource || dcp->cp_salt ||
	    dcp->cp_cmd || dcp->cp_iters)
		return (SET_ERROR(EINVAL));

	ret = dsl_pool_hold(dsname, FTAG, &dp);
	if (ret)
		goto error;

	/* hold the dsl dir */
	ret = dsl_dir_hold(dp, dsname, FTAG, &dd, NULL);
	if (ret)
		goto error;

	/* initialize the wkey's ddobj */
	wkey->wk_ddobj = dd->dd_object;

	/* verify that the wkey is correct by opening its dsl key */
	ret = dsl_crypto_key_open(dp->dp_meta_objset, wkey,
	    dd->dd_crypto_obj, FTAG, &dck);
	if (ret)
		goto error;

	/* insert the wrapping key into the keystore */
	ret = spa_keystore_load_wkey_impl(dp->dp_spa, wkey);
	if (ret)
		goto error;

	dsl_crypto_key_rele(dck, FTAG);
	dsl_dir_rele(dd, FTAG);
	dsl_pool_rele(dp, FTAG);

	/* create any zvols under this ds */
	zvol_create_minors(dp->dp_spa, dsname, B_TRUE);

	return (0);

error:
	if (dck)
		dsl_crypto_key_rele(dck, FTAG);
	if (dd)
		dsl_dir_rele(dd, FTAG);
	if (dp)
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
		ret = SET_ERROR(ENOENT);
		goto error_unlock;
	} else if (refcount_count(&found_wkey->wk_refcnt) != 0) {
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

	/* hold the dsl dir */
	ret = dsl_pool_hold(dsname, FTAG, &dp);
	if (ret)
		goto error;

	ret = dsl_dir_hold(dp, dsname, FTAG, &dd, NULL);
	if (ret)
		goto error;

	/* unload the wkey */
	ret = spa_keystore_unload_wkey_impl(dp->dp_spa, dd->dd_object);
	if (ret)
		goto error;

	dsl_dir_rele(dd, FTAG);
	dsl_pool_rele(dp, FTAG);

	/* remove any zvols under this ds */
	zvol_remove_minors(dp->dp_spa, dsname, B_TRUE);

	return (0);

error:
	if (dd)
		dsl_dir_rele(dd, FTAG);
	if (dp)
		dsl_pool_rele(dp, FTAG);

	return (ret);
}

int
spa_keystore_create_mapping(spa_t *spa, dsl_dataset_t *ds, void *tag)
{
	int ret;
	avl_index_t where;
	dsl_key_mapping_t *km = NULL, *found_km;
	boolean_t should_free = B_FALSE;

	/* allocate the mapping */
	km = kmem_alloc(sizeof (dsl_key_mapping_t), KM_SLEEP);
	if (!km)
		return (SET_ERROR(ENOMEM));

	/* initialize the mapping */
	refcount_create(&km->km_refcnt);

	ret = spa_keystore_dsl_key_hold_dd(spa, ds->ds_dir, km,
	    &km->km_key);
	if (ret)
		goto error;

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
	if (found_km) {
		should_free = B_TRUE;
		refcount_add(&found_km->km_refcnt, tag);
	} else {
		refcount_add(&km->km_refcnt, tag);
		avl_insert(&spa->spa_keystore.sk_key_mappings, km, where);
	}

	rw_exit(&spa->spa_keystore.sk_km_lock);

	if (should_free) {
		spa_keystore_dsl_key_rele(spa, km->km_key, km);
		refcount_destroy(&km->km_refcnt);
		kmem_free(km, sizeof (dsl_key_mapping_t));
	}

	return (0);

error:
	if (km->km_key)
		spa_keystore_dsl_key_rele(spa, km->km_key, km);

	refcount_destroy(&km->km_refcnt);
	kmem_free(km, sizeof (dsl_key_mapping_t));

	return (ret);
}

int
spa_keystore_remove_mapping(spa_t *spa, dsl_dataset_t *ds, void *tag)
{
	int ret;
	dsl_key_mapping_t search_km;
	dsl_key_mapping_t *found_km;
	boolean_t should_free = B_FALSE;

	/* init the search key mapping */
	search_km.km_dsobj = ds->ds_object;

	rw_enter(&spa->spa_keystore.sk_km_lock, RW_WRITER);

	/* find the matching mapping */
	found_km = avl_find(&spa->spa_keystore.sk_key_mappings,
	    &search_km, NULL);
	if (found_km == NULL) {
		ret = SET_ERROR(ENOENT);
		goto error_unlock;
	}

	/*
	 * Decrement the refcount on the mapping and remove it from the tree if
	 * it is zero. Try to minimize time spent in this lock by deferring
	 * cleanup work.
	 */
	if (refcount_remove(&found_km->km_refcnt, tag) == 0) {
		should_free = B_TRUE;
		avl_remove(&spa->spa_keystore.sk_key_mappings, found_km);
	}

	rw_exit(&spa->spa_keystore.sk_km_lock);

	/* destroy the key mapping */
	if (should_free) {
		spa_keystore_dsl_key_rele(spa, found_km->km_key, found_km);
		kmem_free(found_km, sizeof (dsl_key_mapping_t));
	}

	return (0);

error_unlock:
	rw_exit(&spa->spa_keystore.sk_km_lock);
	return (ret);
}

/*
 * This function is primarily used by the zio layer to lookup DSL
 * Crypto Keys for encryption. For this use case the key cannot be
 * unloaded because it is held open by the owning dataset. There are some
 * callers, however, that are not protected by the owning dataset (currently
 * just the L2ARC). These callers may pass a tag which will prevent the key
 * from vanishing while it is being used. Those callers must release the
 * key with spa_keystore_dsl_key_rele().
 */
int
spa_keystore_lookup_key(spa_t *spa, uint64_t dsobj, void *tag,
    dsl_crypto_key_t **dck_out)
{
	int ret;
	dsl_key_mapping_t search_km;
	dsl_key_mapping_t *found_km;

	IMPLY(tag != NULL, dck_out != NULL);

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
		refcount_add(&found_km->km_key->dck_refcnt, tag);

	rw_exit(&spa->spa_keystore.sk_km_lock);

	if (dck_out)
		*dck_out = found_km->km_key;
	return (0);

error_unlock:
	rw_exit(&spa->spa_keystore.sk_km_lock);

	if (dck_out)
		*dck_out = NULL;
	return (ret);
}

static void
dsl_crypto_key_sync(dsl_crypto_key_t *dck, dmu_tx_t *tx)
{
	uint64_t dckobj = dck->dck_obj;
	zio_crypt_key_t *key = &dck->dck_key;
	objset_t *mos = tx->tx_pool->dp_meta_objset;
	uint8_t keydata[MAX_MASTER_KEY_LEN];
	uint8_t hmac_keydata[HMAC_SHA256_KEYLEN];
	uint8_t iv[WRAPPING_IV_LEN];
	uint8_t mac[WRAPPING_MAC_LEN];

	ASSERT(dmu_tx_is_syncing(tx));
	ASSERT3U(key->zk_crypt, <, ZIO_CRYPT_FUNCTIONS);

	/* encrypt and store the keys along with the IV and MAC */
	VERIFY0(zio_crypt_key_wrap(&dck->dck_wkey->wk_key, key, iv, mac,
	    keydata, hmac_keydata));

	/* update the ZAP with the obtained values */
	VERIFY0(zap_update(mos, dckobj, DSL_CRYPTO_KEY_CRYPT, 8, 1,
	    &key->zk_crypt, tx));

	VERIFY0(zap_update(mos, dckobj, DSL_CRYPTO_KEY_IV, 1, WRAPPING_IV_LEN,
	    iv, tx));

	VERIFY0(zap_update(mos, dckobj, DSL_CRYPTO_KEY_MAC, 1, WRAPPING_MAC_LEN,
	    mac, tx));

	VERIFY0(zap_update(mos, dckobj, DSL_CRYPTO_KEY_MASTER_BUF, 1,
	    MAX_MASTER_KEY_LEN, keydata, tx));

	VERIFY0(zap_update(mos, dckobj, DSL_CRYPTO_KEY_HMAC_KEY_BUF, 1,
	    HMAC_SHA256_KEYLEN, hmac_keydata, tx));
}

typedef struct spa_keystore_rewrap_args {
	const char *skra_dsname;
	dsl_crypto_params_t *skra_cp;
} spa_keystore_rewrap_args_t;

static int
spa_keystore_rewrap_check(void *arg, dmu_tx_t *tx)
{
	int ret;
	dsl_dir_t *dd;
	dsl_crypto_key_t *dck = NULL;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	spa_keystore_rewrap_args_t *skra = arg;
	dsl_crypto_params_t *dcp = skra->skra_cp;

	if (!dcp || !dcp->cp_wkey)
		return (SET_ERROR(EINVAL));
	if (dcp->cp_crypt != ZIO_CRYPT_INHERIT || dcp->cp_cmd)
		return (SET_ERROR(EINVAL));
	if (dcp->cp_keysource &&
	    strncmp(dcp->cp_keysource, "passphrase", 10) == 0 &&
	    (!skra->skra_cp->cp_salt || !skra->skra_cp->cp_iters))
		return (SET_ERROR(EINVAL));

	/* hold the dd */
	ret = dsl_dir_hold(dp, skra->skra_dsname, FTAG, &dd, NULL);
	if (ret)
		return (ret);

	/* check that this dd has a dsl key */
	if (dd->dd_crypto_obj == 0) {
		ret = SET_ERROR(EINVAL);
		goto error;
	}

	/* make sure the dsl key is loaded / loadable */
	ret = spa_keystore_dsl_key_hold_dd(dp->dp_spa, dd, FTAG, &dck);
	if (ret)
		goto error;

	ASSERT(dck->dck_wkey != NULL);

	spa_keystore_dsl_key_rele(dp->dp_spa, dck, FTAG);
	dsl_dir_rele(dd, FTAG);

	return (0);

error:
	if (dck)
		spa_keystore_dsl_key_rele(dp->dp_spa, dck, FTAG);
	dsl_dir_rele(dd, FTAG);

	return (ret);
}


static void
spa_keystore_rewrap_sync_impl(uint64_t root_ddobj, uint64_t ddobj,
    dsl_wrapping_key_t *wkey, dmu_tx_t *tx)
{
	zap_cursor_t zc;
	zap_attribute_t za;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	dsl_dir_t *dd = NULL, *inherit_dd = NULL;
	dsl_crypto_key_t *dck = NULL;

	ASSERT(RW_WRITE_HELD(&dp->dp_spa->spa_keystore.sk_wkeys_lock));

	/* hold the dd */
	VERIFY0(dsl_dir_hold_obj(dp, ddobj, NULL, FTAG, &dd));

	/* ignore hidden dsl dirs */
	if (dd->dd_myname[0] == '$' || dd->dd_myname[0] == '%') {
		dsl_dir_rele(dd, FTAG);
		return;
	}

	/* hold the dd we inherited the keysource from */
	VERIFY0(dsl_dir_hold_keysource_source_dd(dd, FTAG, &inherit_dd));

	/* stop recursing if this dsl dir didn't inherit from the root */
	if (inherit_dd->dd_object != root_ddobj) {
		dsl_dir_rele(inherit_dd, FTAG);
		dsl_dir_rele(dd, FTAG);
		return;
	}

	/* get the dsl_crypt_key_t for the current dsl dir */
	VERIFY0(spa_keystore_dsl_key_hold_dd(dp->dp_spa, dd, FTAG, &dck));

	/* replace the wrapping key */
	dsl_wrapping_key_hold(wkey, dck);
	dsl_wrapping_key_rele(dck->dck_wkey, dck);
	dck->dck_wkey = wkey;

	/* sync the dsl key wrapped with the new wrapping key */
	dsl_crypto_key_sync(dck, tx);

	/* recurse into all children dsl dirs */
	for (zap_cursor_init(&zc, dp->dp_meta_objset,
	    dsl_dir_phys(dd)->dd_child_dir_zapobj);
	    zap_cursor_retrieve(&zc, &za) == 0;
	    zap_cursor_advance(&zc)) {
		spa_keystore_rewrap_sync_impl(root_ddobj, za.za_first_integer,
		    wkey, tx);
	}
	zap_cursor_fini(&zc);

	spa_keystore_dsl_key_rele(dp->dp_spa, dck, FTAG);
	dsl_dir_rele(inherit_dd, FTAG);
	dsl_dir_rele(dd, FTAG);
}

static void
spa_keystore_rewrap_sync(void *arg, dmu_tx_t *tx)
{
	dsl_dataset_t *ds;
	avl_index_t where;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	spa_t *spa = dp->dp_spa;
	spa_keystore_rewrap_args_t *skra = arg;
	dsl_wrapping_key_t *wkey = skra->skra_cp->cp_wkey;
	dsl_wrapping_key_t *found_wkey;
	uint64_t crypt;
	const char *keysource = skra->skra_cp->cp_keysource;

	/* create and initialize the wrapping key */
	VERIFY0(dsl_dataset_hold(dp, skra->skra_dsname, FTAG, &ds));
	ASSERT(!ds->ds_is_snapshot);

	/* set additional properties which can be sent along with this ioctl */
	if (keysource)
		dsl_prop_set_sync_impl(ds,
		    zfs_prop_to_name(ZFS_PROP_KEYSOURCE), ZPROP_SRC_LOCAL,
		    1, strlen(keysource) + 1, keysource, tx);

	if (skra->skra_cp->cp_iters)
		dsl_prop_set_sync_impl(ds,
		    zfs_prop_to_name(ZFS_PROP_PBKDF2_ITERS),
		    ZPROP_SRC_LOCAL, 8, 1, &skra->skra_cp->cp_iters, tx);

	dsl_prop_set_sync_impl(ds, zfs_prop_to_name(ZFS_PROP_SALT),
	    ZPROP_SRC_LOCAL, 8, 1, &skra->skra_cp->cp_salt, tx);

	/*
	 * Rewrapping the dataset implies we are making this an encryption
	 * root. Set the encryption property locally now.
	 */
	VERIFY0(dsl_prop_get_ds(ds, zfs_prop_to_name(ZFS_PROP_ENCRYPTION),
	    8, 1, &crypt, NULL));
	dsl_prop_set_sync_impl(ds, zfs_prop_to_name(ZFS_PROP_ENCRYPTION),
	    ZPROP_SRC_LOCAL, 8, 1, &crypt, tx);

	wkey->wk_ddobj = ds->ds_dir->dd_object;

	rw_enter(&spa->spa_keystore.sk_wkeys_lock, RW_WRITER);

	/* recurse through all children and rewrap their keys */
	spa_keystore_rewrap_sync_impl(ds->ds_dir->dd_object,
	    ds->ds_dir->dd_object, wkey, tx);

	/*
	 * All references to the old wkey should be released now (if it
	 * existed). Replace the wrapping key.
	 */
	found_wkey = avl_find(&spa->spa_keystore.sk_wkeys, wkey, NULL);
	if (found_wkey) {
		ASSERT0(refcount_count(&found_wkey->wk_refcnt));
		avl_remove(&spa->spa_keystore.sk_wkeys, found_wkey);
		dsl_wrapping_key_free(found_wkey);
	}

	avl_find(&spa->spa_keystore.sk_wkeys, wkey, &where);
	avl_insert(&spa->spa_keystore.sk_wkeys, wkey, where);

	rw_exit(&spa->spa_keystore.sk_wkeys_lock);

	dsl_dataset_rele(ds, FTAG);
}

int
spa_keystore_rewrap(const char *dsname, dsl_crypto_params_t *dcp)
{
	spa_keystore_rewrap_args_t skra;

	/* initialize the args struct */
	skra.skra_dsname = dsname;
	skra.skra_cp = dcp;

	/* perform the actual work in syncing context */
	return (dsl_sync_task(dsname, spa_keystore_rewrap_check,
	    spa_keystore_rewrap_sync, &skra, 0, ZFS_SPACE_CHECK_NORMAL));
}

int
dmu_objset_create_encryption_check(dsl_dir_t *pdd, dsl_crypto_params_t *dcp)
{
	int ret;
	dsl_wrapping_key_t *wkey = NULL;
	uint64_t cmd = 0, salt = 0, iters = 0;
	uint64_t pcrypt, crypt = ZIO_CRYPT_INHERIT;
	const char *keysource = NULL;

	if (!spa_feature_is_enabled(pdd->dd_pool->dp_spa,
	    SPA_FEATURE_ENCRYPTION) && dcp)
		return (SET_ERROR(EINVAL));

	ret = dsl_prop_get_dd(pdd, zfs_prop_to_name(ZFS_PROP_ENCRYPTION),
	    8, 1, &pcrypt, NULL, B_FALSE);
	if (ret)
		return (ret);

	if (dcp) {
		crypt = dcp->cp_crypt;
		wkey = dcp->cp_wkey;
		salt = dcp->cp_salt;
		iters = dcp->cp_iters;
		keysource = dcp->cp_keysource;
		cmd = dcp->cp_cmd;
	}

	if (crypt >= ZIO_CRYPT_FUNCTIONS)
		return (SET_ERROR(EINVAL));
	if (crypt == ZIO_CRYPT_OFF && pcrypt != ZIO_CRYPT_OFF)
		return (SET_ERROR(EINVAL));
	if (crypt == ZIO_CRYPT_INHERIT && pcrypt == ZIO_CRYPT_OFF &&
	    (salt || keysource || wkey || iters))
		return (SET_ERROR(EINVAL));
	if (crypt == ZIO_CRYPT_OFF && (salt || keysource || wkey || iters))
		return (SET_ERROR(EINVAL));
	if (crypt != ZIO_CRYPT_INHERIT && crypt != ZIO_CRYPT_OFF &&
	    pcrypt == ZIO_CRYPT_OFF && (!keysource || !wkey))
		return (SET_ERROR(EINVAL));
	if (keysource && strncmp(keysource, "passphrase", 10) == 0 &&
	    (!salt || !iters))
	if (cmd)
		return (SET_ERROR(EINVAL));

	if (!wkey && pcrypt != ZIO_CRYPT_OFF) {
		ret = spa_keystore_wkey_hold_ddobj(pdd->dd_pool->dp_spa,
		    pdd->dd_object, FTAG, &wkey);
		if (ret)
			return (SET_ERROR(EACCES));

		dsl_wrapping_key_rele(wkey, FTAG);
	}

	return (0);
}

int
dmu_objset_clone_encryption_check(dsl_dir_t *pdd, dsl_dir_t *odd,
    dsl_crypto_params_t *dcp)
{
	int ret;
	dsl_wrapping_key_t *wkey = NULL;
	uint64_t cmd = 0, salt = 0, iters = 0;
	uint64_t pcrypt, ocrypt, crypt = ZIO_CRYPT_INHERIT;
	const char *keysource = NULL;

	if (!spa_feature_is_enabled(pdd->dd_pool->dp_spa,
	    SPA_FEATURE_ENCRYPTION) && dcp)
		return (SET_ERROR(EINVAL));

	ret = dsl_prop_get_dd(pdd, zfs_prop_to_name(ZFS_PROP_ENCRYPTION), 8, 1,
	    &pcrypt, NULL, B_FALSE);
	if (ret)
		return (ret);

	ret = dsl_prop_get_dd(odd, zfs_prop_to_name(ZFS_PROP_ENCRYPTION), 8, 1,
	    &ocrypt, NULL, B_FALSE);
	if (ret)
		return (ret);

	if (dcp) {
		crypt = dcp->cp_crypt;
		wkey = dcp->cp_wkey;
		salt = dcp->cp_salt;
		iters = dcp->cp_iters;
		keysource = dcp->cp_keysource;
		cmd = dcp->cp_cmd;
	}

	if (crypt != ZIO_CRYPT_INHERIT)
		return (SET_ERROR(EINVAL));
	if (pcrypt != ZIO_CRYPT_OFF && ocrypt == ZIO_CRYPT_OFF)
		return (SET_ERROR(EINVAL));
	if (pcrypt == ZIO_CRYPT_OFF && ocrypt != ZIO_CRYPT_OFF &&
	    (!wkey || !keysource))
		return (SET_ERROR(EINVAL));
	if (keysource && strncmp(keysource, "passphrase", 10) == 0 &&
	    (!salt || !iters))
		return (SET_ERROR(EINVAL));

	/* origin wrapping key must be present, if it is encrypted */
	if (ocrypt != ZIO_CRYPT_OFF) {
		ret = spa_keystore_wkey_hold_ddobj(pdd->dd_pool->dp_spa,
		    odd->dd_object, FTAG, &wkey);
		if (ret)
			return (SET_ERROR(EACCES));

		dsl_wrapping_key_rele(wkey, FTAG);
	}

	/* parent's wrapping key must be present if a new one isn't specified */
	if (!wkey && pcrypt != ZIO_CRYPT_OFF) {
		ret = spa_keystore_wkey_hold_ddobj(pdd->dd_pool->dp_spa,
		    pdd->dd_object, FTAG, &wkey);
		if (ret)
			return (SET_ERROR(EACCES));

		dsl_wrapping_key_rele(wkey, FTAG);
	}

	return (0);
}

uint64_t
dsl_crypto_key_create_sync(uint64_t crypt, dsl_wrapping_key_t *wkey,
    dmu_tx_t *tx)
{
	dsl_crypto_key_t dck;

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

	zio_crypt_key_destroy(&dck.dck_key);
	bzero(&dck.dck_key, sizeof (zio_crypt_key_t));

	/* increment the encryption feature count */
	spa_feature_incr(tx->tx_pool->dp_spa, SPA_FEATURE_ENCRYPTION, tx);

	return (dck.dck_obj);
}

uint64_t
dsl_crypto_key_clone_sync(dsl_dir_t *orig_dd, dsl_wrapping_key_t *wkey,
    dmu_tx_t *tx)
{
	dsl_pool_t *dp = tx->tx_pool;
	dsl_crypto_key_t *orig_dck;
	dsl_crypto_key_t dck;

	ASSERT(dmu_tx_is_syncing(tx));

	/* get the key from the original dataset */
	VERIFY0(spa_keystore_dsl_key_hold_dd(dp->dp_spa, orig_dd, FTAG,
	    &orig_dck));

	/* create the DSL Crypto Key ZAP object */
	dck.dck_obj = zap_create(dp->dp_meta_objset, DMU_OTN_ZAP_METADATA,
	    DMU_OT_NONE, 0, tx);

	/* assign the wrapping key temporarily */
	dck.dck_wkey = wkey;

	/*
	 * Fill in the temporary key with the original key's data. We only need
	 * to actually copy the fields that will be synced to disk, namely the
	 * master key, hmac key and crypt.
	 */
	bcopy(&orig_dck->dck_key.zk_master_keydata,
	    &dck.dck_key.zk_master_keydata, MAX_MASTER_KEY_LEN);
	bcopy(&orig_dck->dck_key.zk_hmac_keydata,
	    &dck.dck_key.zk_hmac_keydata, HMAC_SHA256_KEYLEN);

	dck.dck_key.zk_crypt = orig_dck->dck_key.zk_crypt;

	/* sync the new key, wrapped with the new wrapping key */
	dsl_crypto_key_sync(&dck, tx);
	bzero(&dck.dck_key, sizeof (zio_crypt_key_t));

	/* increment the encryption feature count */
	spa_feature_incr(dp->dp_spa, SPA_FEATURE_ENCRYPTION, tx);

	spa_keystore_dsl_key_rele(dp->dp_spa, orig_dck, FTAG);

	return (dck.dck_obj);
}

void
dsl_crypto_key_destroy_sync(uint64_t dckobj, dmu_tx_t *tx)
{
	/* destroy the DSL Crypto Key object */
	VERIFY0(zap_destroy(tx->tx_pool->dp_meta_objset, dckobj, tx));

	/* decrement the feature count */
	spa_feature_decr(tx->tx_pool->dp_spa, SPA_FEATURE_ENCRYPTION, tx);
}

int
spa_crypt_get_salt(spa_t *spa, uint64_t dsobj, uint8_t *salt)
{
	int ret;
	dsl_crypto_key_t *dck;

	/* look up the key from the spa's keystore */
	ret = spa_keystore_lookup_key(spa, dsobj, NULL, &dck);
	if (ret) {
		ret = SET_ERROR(EACCES);
		goto error;
	}

	ret = zio_crypt_key_get_salt(&dck->dck_key, salt);
	if (ret)
		goto error;

	return (0);

error:
	return (ret);
}

/*
 * This function serve as a multiplexer for encryption and decryption of
 * all blocks (except the L2ARC). For encryption, it will populate the IV,
 * salt, MAC, and cabd (the ciphertext). On decryption it will simply use
 * these fields to populate pabd (the plaintext).
 */
int
spa_do_crypt_abd(boolean_t encrypt, spa_t *spa, zbookmark_phys_t *zb,
    blkptr_t *bp, uint64_t txgid, uint_t datalen, abd_t *pabd, abd_t *cabd,
    uint8_t *iv, uint8_t *mac, uint8_t *salt)
{
	int ret;
	dmu_object_type_t ot = BP_GET_TYPE(bp);
	dsl_crypto_key_t *dck;
	uint8_t tmpiv[DATA_IV_LEN];
	uint8_t *plainbuf = NULL, *cipherbuf = NULL;

	ASSERT(spa_feature_is_active(spa, SPA_FEATURE_ENCRYPTION));
	ASSERT(!BP_IS_EMBEDDED(bp));
	ASSERT(BP_IS_ENCRYPTED(bp));

	/* look up the key from the spa's keystore */
	ret = spa_keystore_lookup_key(spa, zb->zb_objset, NULL, &dck);
	if (ret) {
		ret = SET_ERROR(EACCES);
		goto error;
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
	if (encrypt && ot != DMU_OT_INTENT_LOG && !BP_GET_DEDUP(bp)) {
		ret = zio_crypt_key_get_salt(&dck->dck_key, salt);
		if (ret)
			goto error;

		ret = zio_crypt_generate_iv(iv);
		if (ret)
			goto error;
	} else if (encrypt && BP_GET_DEDUP(bp)) {
		ret = zio_crypt_generate_iv_salt_dedup(&dck->dck_key,
		    plainbuf, datalen, iv, salt);
		if (ret)
			goto error;
	} else if (ot == DMU_OT_INTENT_LOG) {
		uint8_t *src = (encrypt) ? plainbuf : cipherbuf;

		zio_crypt_derive_zil_iv(src, iv, tmpiv);
		iv = tmpiv;
	}

	/* call lower level function to perform encryption / decryption */
	ret = zio_do_crypt_data(encrypt, &dck->dck_key, salt, ot, iv, mac,
	    datalen, plainbuf, cipherbuf);
	if (ret)
		goto error;

	if (encrypt) {
		abd_return_buf(pabd, plainbuf, datalen);
		abd_return_buf_copy(cabd, cipherbuf, datalen);
	} else {
		abd_return_buf_copy(pabd, plainbuf, datalen);
		abd_return_buf(cabd, cipherbuf, datalen);
	}

	return (0);

error:
	/* zero out any state we might have changed while encrypting */
	if (encrypt) {
		bzero(salt, DATA_SALT_LEN);
		bzero(iv, DATA_IV_LEN);
		bzero(mac, (ot == DMU_OT_INTENT_LOG) ?
		    ZIL_MAC_LEN : DATA_MAC_LEN);
	}

	if (plainbuf)
		abd_return_buf(pabd, plainbuf, datalen);
	if (cipherbuf)
		abd_return_buf(cabd, cipherbuf, datalen);

	return (ret);
}
