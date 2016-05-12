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

#ifndef	_SYS_DSL_CRYPT_H
#define	_SYS_DSL_CRYPT_H

#include <sys/dmu_tx.h>
#include <sys/dmu.h>
#include <sys/zio_crypt.h>
#include <sys/spa.h>
#include <sys/dsl_dataset.h>

/* forward declarations */
struct dsl_dataset;

typedef enum zfs_keystatus {
	ZFS_KEYSTATUS_NONE = 0,
	ZFS_KEYSTATUS_UNAVAILABLE,
	ZFS_KEYSTATUS_AVAILABLE,
} zfs_keystatus_t;

/* in memory representation of a wrapping key */
typedef struct dsl_wrapping_key {
	/* link into the keystore's tree of wrapping keys */
	avl_node_t wk_avl_link;

	/* actual wrapping key */
	crypto_key_t wk_key;

	/* refcount of number of dsl_crypto_key_t's holding this struct */
	refcount_t wk_refcnt;

	/* dsl directory object that owns this wrapping key */
	uint64_t wk_ddobj;
} dsl_wrapping_key_t;

/* structure for passing around encryption params from userspace */
typedef struct dsl_crypto_params {
	/* command to be executed */
	zfs_ioc_crypto_cmd_t cp_cmd;

	/* the encryption algorithm */
	uint64_t cp_crypt;

	/* the salt, if the keysource is of type passphrase */
	uint64_t cp_salt;

	/* the pbkdf2 iterations, if the keysource is of type passphrase */
	uint64_t cp_iters;

	/* keysource property string */
	const char *cp_keysource;

	/* the wrapping key */
	dsl_wrapping_key_t *cp_wkey;
} dsl_crypto_params_t;

/* in-memory representation of an encryption key for a dataset */
typedef struct dsl_crypto_key {
	/* avl node for linking into the keystore */
	avl_node_t dck_avl_link;

	/* refcount of dsl_key_mapping_t's holding this key */
	refcount_t dck_refcnt;

	/* master key used to derive encryption keys */
	zio_crypt_key_t dck_key;

	/* wrapping key for syncing this structure to disk */
	dsl_wrapping_key_t *dck_wkey;

	/* on-disk object id */
	uint64_t dck_obj;
} dsl_crypto_key_t;

/*
 * In memory mapping of a dataset to a DSL Crypto Key. This is used
 * to look up the corresponding dsl_crypto_key_t from the zio layer
 * for performing data encryption and decryption.
 */
typedef struct dsl_key_mapping {
	/* avl node for linking into the keystore */
	avl_node_t km_avl_link;

	/* refcount of how many users are depending on this mapping */
	refcount_t km_refcnt;

	/* dataset this crypto key belongs to (index) */
	uint64_t km_dsobj;

	/* crypto key (value) of this record */
	dsl_crypto_key_t *km_key;
} dsl_key_mapping_t;

/* in memory structure for holding all wrapping and dsl keys */
typedef struct spa_keystore {
	/* lock for protecting sk_dsl_keys */
	krwlock_t sk_dk_lock;

	/* tree of all dsl_crypto_key_t's */
	avl_tree_t sk_dsl_keys;

	/* lock for protecting sk_key_mappings */
	krwlock_t sk_km_lock;

	/* tree of all dsl_key_mapping_t's, indexed by dsobj */
	avl_tree_t sk_key_mappings;

	/* lock for protecting the wrapping keys tree */
	krwlock_t sk_wkeys_lock;

	/* tree of all wrapping keys, indexed by ddobj */
	avl_tree_t sk_wkeys;
} spa_keystore_t;

void dsl_wrapping_key_hold(dsl_wrapping_key_t *wkey, void *tag);
void dsl_wrapping_key_rele(dsl_wrapping_key_t *wkey, void *tag);
void dsl_wrapping_key_free(dsl_wrapping_key_t *wkey);
int dsl_wrapping_key_create(uint8_t *wkeydata, dsl_wrapping_key_t **wkey_out);

int dsl_crypto_params_create_nvlist(nvlist_t *props, nvlist_t *crypto_args,
    dsl_crypto_params_t **dcp_out);
void dsl_crypto_params_free(dsl_crypto_params_t *dcp, boolean_t unload);

void spa_keystore_init(spa_keystore_t *sk);
void spa_keystore_fini(spa_keystore_t *sk);
zfs_keystatus_t dsl_dataset_keystore_keystatus(struct dsl_dataset *ds);

int spa_keystore_wkey_hold_ddobj(spa_t *spa, uint64_t ddobj, void *tag,
    dsl_wrapping_key_t **wkey_out);
int spa_keystore_dsl_key_hold_dd(spa_t *spa, dsl_dir_t *dd, void *tag,
    dsl_crypto_key_t **dck_out);
void spa_keystore_dsl_key_rele(spa_t *spa, dsl_crypto_key_t *dck, void *tag);
int spa_keystore_load_wkey_impl(spa_t *spa, dsl_wrapping_key_t *wkey);
int spa_keystore_load_wkey(const char *dsname, dsl_crypto_params_t *dcp);
int spa_keystore_unload_wkey_impl(spa_t *spa, uint64_t ddobj);
int spa_keystore_unload_wkey(const char *dsname);
int spa_keystore_create_mapping(spa_t *spa, struct dsl_dataset *ds, void *tag);
int spa_keystore_remove_mapping(spa_t *spa, struct dsl_dataset *ds, void *tag);
int spa_keystore_lookup_key(spa_t *spa, uint64_t dsobj, void *tag,
    dsl_crypto_key_t **dck_out);

int spa_keystore_rewrap(const char *dsname, dsl_crypto_params_t *dcp);
int dmu_objset_create_encryption_check(dsl_dir_t *pdd,
    dsl_crypto_params_t *dcp);
int dmu_objset_clone_encryption_check(dsl_dir_t *pdd, dsl_dir_t *odd,
    dsl_crypto_params_t *dcp);
uint64_t dsl_crypto_key_create_sync(uint64_t crypt, dsl_wrapping_key_t *wkey,
    dmu_tx_t *tx);
uint64_t dsl_crypto_key_clone_sync(dsl_dir_t *orig_dd,
    dsl_wrapping_key_t *wkey, dmu_tx_t *tx);
void dsl_crypto_key_destroy_sync(uint64_t dckobj, dmu_tx_t *tx);

int spa_crypt_get_salt(spa_t *spa, uint64_t dsobj, uint8_t *salt);
int spa_do_crypt_abd(boolean_t encrypt, spa_t *spa, zbookmark_phys_t *zb,
    blkptr_t *bp, uint64_t txgid, uint_t datalen, abd_t *pabd, abd_t *cabd,
    uint8_t *iv, uint8_t *mac, uint8_t *salt);

#endif
