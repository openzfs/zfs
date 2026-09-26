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

#define ZOS_CHUNK_SIZE (64*1024)
static const char ZOS_OBJSET_TAG[] = "zos_objset";

/*
 * ZOS uses a single hidden objset per pool (pool/__zos) to store buckets
 * as ZAP objects and objects as DMU dnodes.  Buckets are indexed in a
 * well-known bucket_dir ZAP at object ID 1 inside the ZOS objset.
 *
 * Layout:
 *   pool/__zos (DMU_OST_BUCKET)
 *     Obj 1: bucket_dir ZAP  (bucket_name → bucket_zap_obj_id)
 *     Obj N: bucket ZAP      (key → object_id)
 *     Obj M: object data     (DMU object)
 */

/*
 * Hold or lazily create the ZOS objset for the given pool.
 *
 * If the objset doesn't exist, creates it with DMU_OST_BUCKET type and
 * runs zos_init_cb to create the bucket_dir ZAP at ZOS_BUCKET_DIR_OBJ.
 *
 * Returns 0 on success with *osp held, or an error code.
 */
static void zos_init_cb(objset_t *os, void *arg, cred_t *cr, dmu_tx_t *tx) {
	int error;

	error = zap_create_claim(os, ZOS_BUCKET_DIR_OBJ, DMU_OT_ZAP_OTHER,
	    DMU_OT_NONE, 0, tx);
	if (error) {
		panic("ZOS: failed to claim bucket_dir ZAP at obj %llu: %d",
		    (u_longlong_t)ZOS_BUCKET_DIR_OBJ, error);
	}
}

int zos_get_objset(spa_t *spa, objset_t **osp) {
	char *name;
	objset_t *os;
	int error;

	name = kmem_alloc(MAXPATHLEN, KM_SLEEP);
	snprintf(name, MAXPATHLEN, "%s/%s", spa_name(spa), ZOS_OBJSET_SUFFIX);

	error = dmu_objset_hold(name, ZOS_OBJSET_TAG, &os);
	if (error == 0) {
		if (dmu_objset_type(os) != DMU_OST_BUCKET) {
			dmu_objset_rele(os, ZOS_OBJSET_TAG);
			kmem_free(name, MAXPATHLEN);
			return (EEXIST);
		}
		*osp = os;
		kmem_free(name, MAXPATHLEN);
		return (0);
	}

	if (error != ENOENT) {
		kmem_free(name, MAXPATHLEN);
		return (error);
	}

	/* ZOS objset doesn't exist yet — create it. */
	error = dmu_objset_create(name, DMU_OST_BUCKET, 0, NULL,
	    zos_init_cb, NULL);
	if (error) {
		kmem_free(name, MAXPATHLEN);
		return (error);
	}

	error = dmu_objset_hold(name, ZOS_OBJSET_TAG, &os);
	kmem_free(name, MAXPATHLEN);
	if (error)
		return (error);

	*osp = os;
	return (0);
}

void zos_release_objset(objset_t *os) {
	dmu_objset_rele(os, ZOS_OBJSET_TAG);
}

int get_bucket(spa_t *spa, const char *bucket, objset_t **os, uint64_t *bucket_zap){
	int error;

	error = zos_get_objset(spa, os);
	if (error) {
		*os = NULL;
		return (error);
	}

	error = zap_lookup(*os, ZOS_BUCKET_DIR_OBJ, bucket, 8, 1, bucket_zap);
	if (error) {
		return error;
	}

	return 0;
}

int create_bucket(const char *pool, const char *bucket) {
	spa_t *spa;
	objset_t *os;
	uint64_t bucket_zap;
	dmu_tx_t *tx;
	int error;

	error = spa_open(pool, &spa, ZOS_OBJSET_TAG);
	if (error) {
		return (error);
	}

	error = get_bucket(spa, bucket, &os, &bucket_zap);
	if (error == 0) {
		if (os != NULL) {
			zos_release_objset(os);
		}
		spa_close(spa, ZOS_OBJSET_TAG);
		return (EEXIST);
	}
	if (error != ENOENT) {
		if (os != NULL) {
			zos_release_objset(os);
		}
		spa_close(spa, ZOS_OBJSET_TAG);
		return (error);
	}

	/* Create the bucket ZAP and add it to the bucket_dir in one tx. */
	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, ZOS_BUCKET_DIR_OBJ, B_TRUE, bucket);
	dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0, 0);
	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		zos_release_objset(os);
		spa_close(spa, ZOS_OBJSET_TAG);
		return (error);
	}

	bucket_zap = zap_create(os, DMU_OT_ZAP_OTHER, DMU_OT_NONE, 0, tx);
	if (bucket_zap == 0) {
		dmu_tx_abort(tx);
		zos_release_objset(os);
		spa_close(spa, ZOS_OBJSET_TAG);
		return (EIO);
	}

	error = zap_add(os, ZOS_BUCKET_DIR_OBJ, bucket, 8, 1, &bucket_zap, tx);
	if (error == 0)
		dmu_tx_commit(tx);
	else
		dmu_tx_abort(tx);

	zos_release_objset(os);
	spa_close(spa, ZOS_OBJSET_TAG);
	return (error);
}

int delete_bucket(const char *pool, const char *bucket) {
	spa_t *spa;
	objset_t *os;
	uint64_t bucket_zap;
	dmu_tx_t *tx;
	int error;

	error = spa_open(pool, &spa, ZOS_OBJSET_TAG);
	if (error) {
		return (error);
	}

	error = get_bucket(spa, bucket, &os, &bucket_zap);
	if (error) {
		if (os != NULL) {
			zos_release_objset(os);
		}
		spa_close(spa, ZOS_OBJSET_TAG);
		return (error);
	}

	/* Remove from bucket_dir and free the bucket ZAP. */
	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, ZOS_BUCKET_DIR_OBJ, B_FALSE, bucket);
	dmu_tx_hold_free(tx, bucket_zap, 0, DMU_OBJECT_END);
	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		zos_release_objset(os);
		spa_close(spa, ZOS_OBJSET_TAG);
		return (error);
	}

	error = zap_remove(os, ZOS_BUCKET_DIR_OBJ, bucket, tx);
	if (error == 0) {
		error = dmu_object_free(os, bucket_zap, tx);
	}

	if (error == 0) {
		dmu_tx_commit(tx);
	} else {
		dmu_tx_abort(tx);
	}

	zos_release_objset(os);
	spa_close(spa, ZOS_OBJSET_TAG);
	return (error);
}

int put_object(const char *pool, const char *bucket, const char *key, int fd, uint64_t size) {
	spa_t *spa;
	objset_t *os;
	uint64_t bucket_zap;
	uint64_t old_obj_id = 0;
	uint64_t new_obj_id = 0;
	dmu_tx_t *tx;
	int error;
	struct file *file;
	loff_t pos = 0;
	char *buf;
	dmu_buf_t *dbuf;
	zos_object_meta_t *zom;

	error = spa_open(pool, &spa, ZOS_OBJSET_TAG);
	if (error) {
		return error;
	}

	error = get_bucket(spa, bucket, &os, &bucket_zap);
	if (error) {
		if (os != NULL) {
			zos_release_objset(os);
		}
		spa_close(spa, ZOS_OBJSET_TAG);
		return error;
	}

	/* Check if key already exists — if so, free old object */
	error = zap_lookup(os, bucket_zap, key, 8, 1, &old_obj_id);
	if (error != 0 && error != ENOENT) {
		zos_release_objset(os);
		spa_close(spa, ZOS_OBJSET_TAG);
		return error;
	}

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, bucket_zap, B_TRUE, key);
	dmu_tx_hold_bonus(tx, DMU_NEW_OBJECT);
	if (old_obj_id != 0) {
		dmu_tx_hold_free(tx, old_obj_id, 0, DMU_OBJECT_END);
	}
	if (size > 0) {
		dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0, size);
	} else {
		dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0, ZOS_CHUNK_SIZE);
	}
	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		zos_release_objset(os);
		spa_close(spa, ZOS_OBJSET_TAG);
		return error;
	}

	new_obj_id = dmu_object_alloc(os, DMU_OT_UINT64_OTHER, SPA_MINBLOCKSIZE, DMU_OT_ZOS_OBJECT_META, sizeof (zos_object_meta_t), tx);

	if (size > 0) {
		// Known size, single write
		file = fget(fd);
		if (file == NULL) {
			dmu_tx_abort(tx);
			zos_release_objset(os);
			spa_close(spa, ZOS_OBJSET_TAG);
			return EBADFD;
		}

		buf = kmem_alloc(size, KM_SLEEP);

		ssize_t n = kernel_read(file, buf, size, &pos);
		fput(file);
		if (n < 0 || n != size) {
			kmem_free(buf, size);
			dmu_tx_abort(tx);
			zos_release_objset(os);
			spa_close(spa, ZOS_OBJSET_TAG);
			return EIO;
		}

		dmu_write(os, new_obj_id, 0, size, buf, tx, DMU_READ_PREFETCH);
		kmem_free(buf, size);
	} else {
		file = fget(fd);
		if (file == NULL) {
			dmu_tx_abort(tx);
			zos_release_objset(os);
			spa_close(spa, ZOS_OBJSET_TAG);
			return EBADFD;
		}

		// Unknown size, 64KB chunked writes
		buf = kmem_alloc(ZOS_CHUNK_SIZE, KM_SLEEP);

		uint64_t offset = 0;
		for (;;) {
			ssize_t n = kernel_read(file, buf, ZOS_CHUNK_SIZE, &pos);
			if (n == 0) {
				break;
			}
			if (n < 0) {
				error = EIO;
				break;
			}

			dmu_write(os, new_obj_id, offset, n, buf, tx, DMU_READ_PREFETCH);
			offset += n;
		}
		size = offset;
		kmem_free(buf, ZOS_CHUNK_SIZE);
		fput(file);
		if (error) {
			dmu_tx_abort(tx);
			zos_release_objset(os);
			spa_close(spa, ZOS_OBJSET_TAG);
			return error;
		}
	}

	error = dmu_bonus_hold(os, new_obj_id, ZOS_OBJSET_TAG, &dbuf);
	if (error) {
		dmu_tx_abort(tx);
		zos_release_objset(os);
		spa_close(spa, ZOS_OBJSET_TAG);
		return error;
	}

	dmu_buf_will_dirty(dbuf, tx);
	zom = dbuf->db_data;
	memset(zom, 0, sizeof(*zom));
	zom->size = size;
	dmu_buf_rele(dbuf, ZOS_OBJSET_TAG);

	error = zap_update(os, bucket_zap, key, 8, 1, &new_obj_id, tx);
	if (error) {
		dmu_tx_abort(tx);
		zos_release_objset(os);
		spa_close(spa, ZOS_OBJSET_TAG);
		return error;
	}
	if (old_obj_id) {
		error = dmu_object_free(os, old_obj_id, tx);
		if (error) {
			dmu_tx_abort(tx);
			zos_release_objset(os);
			spa_close(spa, ZOS_OBJSET_TAG);
			return error;
		}
	}
	dmu_tx_commit(tx);
	zos_release_objset(os);
	spa_close(spa, ZOS_OBJSET_TAG);

	return 0;
}

int delete_object(const char *pool, const char *bucket, const char *key) {
	spa_t *spa;
	objset_t *os;
	uint64_t bucket_zap;
	uint64_t obj_id;
	dmu_tx_t *tx;
	int error;

	error = spa_open(pool, &spa, ZOS_OBJSET_TAG);
	if (error) {
		return error;
	}

	error = get_bucket(spa, bucket, &os, &bucket_zap);
	if (error) {
		if (os != NULL) {
			zos_release_objset(os);
		}
		spa_close(spa, ZOS_OBJSET_TAG);
		return error;
	}

	error = zap_lookup(os, bucket_zap, key, 8, 1, &obj_id);
	if (error) {
		zos_release_objset(os);
		spa_close(spa, ZOS_OBJSET_TAG);
		return error;
	}

	tx = dmu_tx_create(os);
	// For some reason, this isn't B_TRUE
	dmu_tx_hold_zap(tx, bucket_zap, B_FALSE, key);
	dmu_tx_hold_free(tx, obj_id, 0, DMU_OBJECT_END);
	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		zos_release_objset(os);
		spa_close(spa, ZOS_OBJSET_TAG);
		return error;
	}

	error = zap_remove(os, bucket_zap, key, tx);
	if (error) {
		dmu_tx_abort(tx);
		zos_release_objset(os);
		spa_close(spa, ZOS_OBJSET_TAG);
		return error;
	}

	error = dmu_object_free(os, obj_id, tx);
	if (error) {
		dmu_tx_abort(tx);
		zos_release_objset(os);
		spa_close(spa, ZOS_OBJSET_TAG);
		return error;
	}

	dmu_tx_commit(tx);
	zos_release_objset(os);
	spa_close(spa, ZOS_OBJSET_TAG);

	return 0;
}

int get_object(const char *pool, const char *bucket, const char *key, int fd, uint64_t *size) {
	spa_t *spa;
	objset_t *os;
	uint64_t bucket_zap;
	uint64_t obj_id;
	int error;
	struct file *file;
	loff_t pos = 0;
	char *buf;
	dmu_buf_t *dbuf;
	zos_object_meta_t *zom;
	uint64_t obj_size;

	error = spa_open(pool, &spa, ZOS_OBJSET_TAG);
	if (error) {
		return error;
	}

	error = get_bucket(spa, bucket, &os, &bucket_zap);
	if (error) {
		if (os != NULL) {
			zos_release_objset(os);
		}
		spa_close(spa, ZOS_OBJSET_TAG);
		return error;
	}

	error = zap_lookup(os, bucket_zap, key, 8, 1, &obj_id);
	if (error) {
		zos_release_objset(os);
		spa_close(spa, ZOS_OBJSET_TAG);
		return error;
	}

	error = dmu_bonus_hold(os, obj_id, ZOS_OBJSET_TAG, &dbuf);
	if (error) {
		zos_release_objset(os);
		spa_close(spa, ZOS_OBJSET_TAG);
		return error;
	}

	zom = dbuf->db_data;
	if (dbuf->db_size < sizeof(*zom)) {
		dmu_buf_rele(dbuf, ZOS_OBJSET_TAG);
		zos_release_objset(os);
		spa_close(spa, ZOS_OBJSET_TAG);
		return EIO;
	}
	obj_size = zom->size;
	dmu_buf_rele(dbuf, ZOS_OBJSET_TAG);

	file = fget(fd);
	if (file == NULL) {
		zos_release_objset(os);
		spa_close(spa, ZOS_OBJSET_TAG);
		return EBADFD;
	}


	buf = kmem_alloc(ZOS_CHUNK_SIZE, KM_SLEEP);
	uint64_t offset = 0;
	while (offset < obj_size) {
		uint64_t chunk = MIN(ZOS_CHUNK_SIZE, obj_size - offset);

		error = dmu_read(os, obj_id, offset, chunk, buf, DMU_READ_PREFETCH);
		if (error) {
			break;
		}

		ssize_t n = kernel_write(file, buf, chunk, &pos);
		if (n < 0 || n != chunk) {
			error = EIO;
			break;
		}
		offset += chunk;
	}

	fput(file);
	kmem_free(buf, ZOS_CHUNK_SIZE);
	zos_release_objset(os);
	spa_close(spa, ZOS_OBJSET_TAG);

	if (error) {
		return error;
	}

	*size = offset;

	return 0;
}
