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
#include <sys/param.h>
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

static int zos_get_root_zap(spa_t *spa, dmu_tx_t *tx, uint64_t *root_zap_obj) {
  objset_t *mos = spa_meta_objset(spa);
  int error;

  // Check if root ZAP exists in pool directory
  error = zap_lookup(mos, DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_ZOS_ROOT, 8, 1,
                     root_zap_obj);
  if (error == ENOENT) {
    // Create root ZAP
    *root_zap_obj = zap_create(mos, DMU_OT_ZAP_OTHER, DMU_OT_NONE, 0, tx);
    if (*root_zap_obj == 0) {
      return EIO;
    }

    // Add to pool directory
    error = zap_add(mos, DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_ZOS_ROOT, 8, 1,
                    root_zap_obj, tx);
  }

  return error;
}

// This gets called after creating the objset and creates the ZAP to store the
// object lookup table
// TODO: handle error
static void create_bucket_cb(objset_t *os, void *arg, cred_t *cr,
                             dmu_tx_t *tx) {
  int error = 0;

  // Create bucket metadata ZAP at well-known obj id
  error = zap_create_claim(os, ZOS_BUCKET_META_OBJ, DMU_OT_ZOS_BUCKET_META,
                           DMU_OT_NONE, 0, tx);
  if (error) {
    panic("Failed to create ZOS bucket metadata ZAP: %d", error);
  }

  error = zap_create_claim(os, ZOS_BUCKET_DATA_OBJ, DMU_OT_ZOS_BUCKET_DATA,
                           DMU_OT_NONE, 0, tx);
  if (error) {
    panic("Failed to create ZOS bucket data ZAP: %d", error);
  }

  // TODO:
  // uint64_t created_time = dmu_tx_get_txg(tx);
  uint64_t created_time = 0;

  error =
      zap_update(os, ZOS_BUCKET_META_OBJ, "created", 8, 1, &created_time, tx);
  if (error) {
    panic("Failed to initialize ZOS bucket metadata: %d", error);
  }
}

int create_bucket(const char *pool, const char *bucket) {
  int error = 0;

  spa_t *spa;

  // Open the pool
  error = spa_open(pool, &spa, FTAG);
  if (error) {
    return error;
  }

  // Construct full path: pool/bucket
  char *path = kmem_alloc(MAXPATHLEN, KM_SLEEP);
  snprintf(path, sizeof(path), "%s/%s", pool, bucket);

  // Create a transaction for the root ZAP operations
  dmu_tx_t *tx = dmu_tx_create(spa_meta_objset(spa));
  dmu_tx_hold_zap(tx, DMU_POOL_DIRECTORY_OBJECT, B_TRUE, DMU_POOL_ZOS_ROOT);
  error = dmu_tx_assign(tx, DMU_TX_WAIT);
  if (error) {
    kmem_free(path, MAXPATHLEN);
    dmu_tx_abort(tx);
    spa_close(spa, FTAG);
    return error;
  }

  // Ensure root ZAP exists
  uint64_t root_zap_obj;
  error = zos_get_root_zap(spa, tx, &root_zap_obj);
  if (error) {
    kmem_free(path, MAXPATHLEN);
    dmu_tx_abort(tx);
    spa_close(spa, FTAG);
    return error;
  }

  // Check if bucket already exists
  error = zap_lookup(spa_meta_objset(spa), root_zap_obj, bucket, 1, 0, NULL);
  if (error == 0) {
    // Bucket already exists
    kmem_free(path, MAXPATHLEN);
    dmu_tx_abort(tx);
    spa_close(spa, FTAG);
    return EEXIST;
  }

  // Create the bucket objset
  error =
      dmu_objset_create(path, DMU_OST_BUCKET, 0, NULL, create_bucket_cb, NULL);
  if (error) {
    kmem_free(path, MAXPATHLEN);
    dmu_tx_abort(tx);
    spa_close(spa, FTAG);
    return error;
  }

  // Add entry to root ZAP: name -> path
  error = zap_add(spa_meta_objset(spa), root_zap_obj, bucket, 1,
                  strlen(path) + 1, path, tx);
  if (error) {
    kmem_free(path, MAXPATHLEN);
    dmu_tx_abort(tx);
    spa_close(spa, FTAG);
    return error;
  }

  kmem_free(path, MAXPATHLEN);
  dmu_tx_commit(tx);
  spa_close(spa, FTAG);
  return 0;
}

// int zfs_ioc_bucket_create_impl(zfs_ioc_bucket_create_args_t *arg) {
//   return create_bucket(arg->name);
// }

int delete_bucket(const char *pool, const char *bucket) {
  int error = 0;

  spa_t *spa;

  // Open the pool
  error = spa_open(pool, &spa, FTAG);
  if (error) {
    return error;
  }

  // Create transaction
  dmu_tx_t *tx = dmu_tx_create(spa_meta_objset(spa));
  dmu_tx_hold_zap(tx, DMU_POOL_DIRECTORY_OBJECT, B_FALSE, DMU_POOL_ZOS_ROOT);
  error = dmu_tx_assign(tx, DMU_TX_WAIT);
  if (error) {
    dmu_tx_abort(tx);
    spa_close(spa, FTAG);
    return error;
  }

  // Get root ZAP
  uint64_t root_zap_obj;
  error = zos_get_root_zap(spa, tx, &root_zap_obj);
  if (error) {
    dmu_tx_abort(tx);
    spa_close(spa, FTAG);
    return error;
  }

  // Lookup path
  char *path = kmem_alloc(MAXPATHLEN, KM_SLEEP);
  error = zap_lookup(spa_meta_objset(spa), root_zap_obj, bucket, 1,
                     sizeof(path), path);
  if (error) {
    kmem_free(path, MAXPATHLEN);
    dmu_tx_abort(tx);
    spa_close(spa, FTAG);
    return error;
  }

  // Remove from root ZAP
  error = zap_remove(spa_meta_objset(spa), root_zap_obj, bucket, tx);
  if (error) {
    kmem_free(path, MAXPATHLEN);
    dmu_tx_abort(tx);
    spa_close(spa, FTAG);
    return error;
  }

  kmem_free(path, MAXPATHLEN);
  dmu_tx_commit(tx);
  spa_close(spa, FTAG);

  // TODO: Verify this destroys children
  return dsl_destroy_head(path);
}

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
