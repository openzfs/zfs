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
 * Copyright (c) 2025 Stephen Reaves. All rights reserved.
 */

// TODO: Fix cred.h import

#include <sys/cred.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_tx.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_destroy.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_pool.h>
#include <sys/fs/zfs.h>
#include <sys/spa.h>
#include <sys/types.h>
#include <sys/zap.h>
#include <sys/zfs_context.h>
#include <sys/zfs_refcount.h>
#include <sys/zos.h>

/*
 * Bucket      -> DMU ObjSet
 * Object key  -> ZAP entry in a root ZAP
 * Object data -> DMU Object
 * Metadata    -> ZAP bonus buffer / separate ZAP
 * list object -> iterate ZAP
 * Delete      -> dmu_object_free()
 * Versions?   -> ZFS clones/snapshots?
 */

/*
 * tank/                  ← ZFS pool
 *   s3bucket1/           ← Dataset = Bucket
 *     root_index (ZAP)   ← key → object ID
 *     obj_1              ← DMU object for S3 key `photos/cat.jpg`
 *     obj_2              ← DMU object for S3 key `data/report.csv`
 */

const char *ROOT_ZAP = "zos:_meta";

// This gets called after creating the objset and creates the ZAP to store the
// object lookup table
static void create_bucket_cb(objset_t *os, void *arg, cred_t *cr,
                             dmu_tx_t *tx) {
  int error = 0;
  uint64_t zap_id = 0;

  // TODO: what do with this?
  zap_id = zap_create(os, DMU_OT_OBJECT_DIRECTORY, DMU_OT_NONE, 0, tx);
  if (zap_id == 0) {
    // TODO: handle error
    // SET_ERROR?
    // This is a callback, so we need to look at how other callbacks handle
    // errors
  }

  dsl_dataset_t *ds = os->os_dsl_dataset;

  // TODO: check size
  zap_add(os, ds->ds_object, ROOT_ZAP, 8, 1, &zap_id, tx);

  // dmu_tx_commit(tx);
}

int create_bucket(const char *bucket) {

  // Create dataset = prefix + bucket_name
  // If default prefix is taken, use UUID?
  // bucket->bucket_prefix = DEFAULT_ZOS_PREFIX;

  // create a root ZAP object

  // Store key -> obj mapping
  // zap_update(os, root_zap, key_str, sizeof(uint64_t), 1, &obj_id);

  int error = 0;

  // TODO: Is this ok to pass as NULL?
  error = dmu_objset_create(bucket, DMU_OST_BUCKET, 0, NULL, create_bucket_cb,
                            NULL);
  if (error) {
    return error;
  }

  // error = zap_create()

  return 0;
}

// int zfs_ioc_bucket_create_impl(zfs_ioc_bucket_create_args_t *arg) {
//   return create_bucket(arg->name);
// }

int delete_bucket(const char *bucket) { return dsl_destroy_head(bucket); }

int upsert_object(const struct zos_object *object, void *object_data,
                  size_t object_data_size) {
  objset_t *os = NULL;
  dmu_tx_t *tx = NULL;
  uint64_t obj = 0;
  int error = 0;

  // Open the dataset as an object
  error = dmu_objset_hold(object->bucket_name, FTAG, &os);
  if (error) {
    return error;
  }

  // Start a transaction
  tx = dmu_tx_create(os);
  dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0, object_data_size);
  error = dmu_tx_assign(tx, DMU_TX_NOWAIT);
  if (error) {
    dmu_tx_abort(tx);
    dmu_objset_rele(os, FTAG);
    return error;
  }

  // Allocate a new DMU object
  obj = dmu_object_alloc(os, DMU_OT_UINT64_OTHER, SPA_MINBLOCKSIZE, DMU_OT_NONE,
                         0, tx);
  if (obj == 0) {
    dmu_tx_abort(tx);
    dmu_objset_rele(os, FTAG);
    return EIO;
  }

  // Write data to object
  /*
  void dmu_write(objset_t * os, uint64_t object, uint64_t offset, uint64_t size,
                 const void *buf, dmu_tx_t *tx, dmu_flags_t flags);
  */
  // TODO: Remove CANFAIL once ready to merge
  dmu_flags_t write_flags = DB_RF_CANFAIL | DMU_DIRECTIO;
  dmu_write(os, obj, 0, object_data_size, object_data, tx, write_flags);
  /*
error = dmu_write(os, obj, 0, object_data_size, object_data, tx);
if (error) {
dmu_tx_abort(tx);
dmu_objset_rele(os, FTAG);
return error;
}
  */

  // os->os_dsl_dataset->ds_dir
  // error = zap_add(os, uint64_t zapobj, const char *key, int integer_size,
  // uint64_t num_integers, const void *val, tx) error = zap_lookup(os, uint64_t
  // zapobj, const char *name, uint64_t integer_size, uint64_t num_integers,
  // void *buf)
  uint64_t root_zap_id = 0;
  error = zap_lookup(os, os->os_dsl_dataset->ds_object, ROOT_ZAP, 8, 1,
                     &root_zap_id);
  if (error) {
    dmu_tx_abort(tx);
    dmu_objset_rele(os, FTAG);
    return error;
  }

  error =
      zap_update(os, root_zap_id, object->object_name, 8, 1, object->data, tx);
  if (error) {
    dmu_tx_abort(tx);
    dmu_objset_rele(os, FTAG);
    return error;
  }
  // metadata
  // zap_add(os, meta_zap_id, "Content-Type", 1, strlen("image/png") + 1,
  // "image/png")

  // Commit
  dmu_tx_commit(tx);

  // Cleanup
  dmu_objset_rele(os, FTAG);

  // object->data = (void *)obj;

  return 0;
}

static int get_dmu_data(objset_t *os, struct zos_object *object,
                        uint64_t *dmu_obj_id, dmu_object_info_t *doi) {
  int error = 0;
  uint64_t root_zap_id = 0;

  error = zap_lookup(os, os->os_dsl_dataset->ds_object, ROOT_ZAP, 8, 1,
                     &root_zap_id);
  if (error) {
    return error;
  }

  error = zap_lookup(os, root_zap_id, object->object_name, 8, 1, dmu_obj_id);
  if (error) {
    return error;
  }

  error = dmu_object_info(os, *dmu_obj_id, doi);
  if (error) {
    return error;
  }

  return 0;
}

int read_object(struct zos_object *object) {
  objset_t *os = NULL;
  uint64_t obj = 0;
  int error = 0;

  error = dmu_objset_hold(object->bucket_name, FTAG, &os);
  if (error) {
    return error;
  }

  // uint64_t root_zap_id = 0;
  // error = zap_lookup(os, os->os_dsl_dataset->ds_object, ROOT_ZAP, 8, 1,
  //                    &root_zap_id);
  // if (error) {
  //   dmu_tx_abort(tx);
  //   dmu_objset_rele(os, FTAG);
  //   return error;
  // }
  //
  uint64_t dmu_obj_id = 0;
  // error = zap_lookup(os, root_zap_id, object->object_name, 8, 1,
  // &dmu_obj_id); if (error) {
  //   dmu_tx_abort(tx);
  //   dmu_objset_rele(os, FTAG);
  //   return error;
  // }
  //
  dmu_object_info_t doi = {0};
  // error = dmu_object_info(os, dmu_obj_id, &doi);
  // if (error) {
  //   dmu_tx_abort(tx);
  //   dmu_objset_rele(os, FTAG);
  //   return error;
  // }
  error = get_dmu_data(os, object, &dmu_obj_id, &doi);
  if (error) {
    dmu_objset_rele(os, FTAG);
    return error;
  }

  error = dmu_read(os, dmu_obj_id, 0, doi.doi_max_offset, object->data, 0);
  if (error) {
    dmu_objset_rele(os, FTAG);
    return error;
  }

  // Cleanup
  dmu_objset_rele(os, FTAG);

  return 0;
}

int delete_object(struct zos_object *object) {
  objset_t *os = NULL;
  dmu_tx_t *tx = NULL;
  uint64_t obj = 0;
  int error = 0;

  // Open the dataset as an object
  error = dmu_objset_hold(object->bucket_name, FTAG, &os);
  if (error) {
    return error;
  }

  // Start a transaction
  tx = dmu_tx_create(os);

  uint64_t dmu_obj_id = 0;
  dmu_object_info_t doi = {0};
  error = get_dmu_data(os, object, &dmu_obj_id, &doi);
  if (error) {
    dmu_tx_abort(tx);
    dmu_objset_rele(os, FTAG);
    return error;
  }
  // dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0, object_data_size);
  dmu_tx_hold_free(tx, dmu_obj_id, 0, doi.doi_max_offset);
  error = dmu_tx_assign(tx, DMU_TX_NOWAIT);
  if (error) {
    dmu_tx_abort(tx);
    dmu_objset_rele(os, FTAG);
    return error;
  }

  error = dmu_object_free(os, dmu_obj_id, tx);
  if (error) {
    dmu_tx_abort(tx);
    dmu_objset_rele(os, FTAG);
    return error;
  }

  // Commit
  dmu_tx_commit(tx);

  // Cleanup
  dmu_objset_rele(os, FTAG);

  return 0;
}

static int zos_get_root_zap(spa_t *spa, dmu_tx_t *tx, uint64_t *root_zap_obj) {
  objset_t *mos = spa->spa_meta_objset;
  int error;

  // Check if root ZAP exists
  error = dmu_object_info(mos, DMU_POOL_ZOS_ROOT, NULL);
  if (error == ENOENT) {
    // Create root ZAP
    *root_zap_obj = zap_create(mos, DMU_OT_ZAP_OTHER, DMU_OT_NONE, 0, tx);
    // Add to pool directory
    error = zap_add(mos, DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_ZOS_ROOT, 8, 1,
                    root_zap_obj, tx);
  } else if (error == 0) {
    // Read existing root ZAP
    error = zap_lookup(mos, DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_ZOS_ROOT, 8, 1,
                       root_zap_obj);
  }
  return error;
}
