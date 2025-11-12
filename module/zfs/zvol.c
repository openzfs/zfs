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
 * Copyright (C) 2008-2010 Lawrence Livermore National Security, LLC.
 * Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 * Rewritten for Linux by Brian Behlendorf <behlendorf1@llnl.gov>.
 * LLNL-CODE-403049.
 *
 * ZFS volume emulation driver.
 *
 * Makes a DMU object look like a volume of arbitrary size, up to 2^64 bytes.
 * Volumes are accessed through the symbolic links named:
 *
 * /dev/<pool_name>/<dataset_name>
 *
 * Volumes are persistent through reboot and module load.  No user command
 * needs to be run before opening and using a device.
 *
 * Copyright 2014 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2016 Actifio, Inc. All rights reserved.
 * Copyright (c) 2012, 2019 by Delphix. All rights reserved.
 * Copyright (c) 2024, 2025, Klara, Inc.
 */

/*
 * Note on locking of zvol state structures.
 *
 * zvol_state_t represents the connection between a single dataset
 * (DMU_OST_ZVOL) and the device "minor" (some OS-specific representation of a
 * "disk" or "device" or "volume", eg, a /dev/zdXX node, a GEOM object, etc).
 *
 * The global zvol_state_lock is used to protect access to zvol_state_list and
 * zvol_htable, which are the primary way to obtain a zvol_state_t from a name.
 * It should not be used for anything not name-relateds, and you should avoid
 * sleeping or waiting while its held. See zvol_find_by_name(), zvol_insert(),
 * zvol_remove().
 *
 * The zv_state_lock is used to protect the contents of the associated
 * zvol_state_t. Most of the zvol_state_t is dedicated to control and
 * configuration; almost none of it is needed for data operations (that is,
 * read, write, flush) so this lock is rarely taken during general IO. It
 * should be released quickly; you should avoid sleeping or waiting while its
 * held.
 *
 * zv_suspend_lock is used to suspend IO/data operations to a zvol. The read
 * half should held for the duration of an IO operation. The write half should
 * be taken when something to wait for IO to complete and the block further IO,
 * eg for the duration of receive and rollback operations. This lock can be
 * held for long periods of time.
 *
 * Thus, the following lock ordering appies.
 * - take zvol_state_lock if necessary, to protect zvol_state_list
 * - take zv_suspend_lock if necessary, by the code path in question
 * - take zv_state_lock to protect zvol_state_t
 *
 * The minor operations are issued to spa->spa_zvol_taskq queues, that are
 * single-threaded (to preserve order of minor operations), and are executed
 * through the zvol_task_cb that dispatches the specific operations. Therefore,
 * these operations are serialized per pool. Consequently, we can be certain
 * that for a given zvol, there is only one operation at a time in progress.
 * That is why one can be sure that first, zvol_state_t for a given zvol is
 * allocated and placed on zvol_state_list, and then other minor operations for
 * this zvol are going to proceed in the order of issue.
 */

#include <sys/dataset_kstats.h>
#include <sys/dbuf.h>
#include <sys/dmu_traverse.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_dir.h>
#include <sys/zap.h>
#include <sys/zfeature.h>
#include <sys/zil_impl.h>
#include <sys/dmu_tx.h>
#include <sys/zio.h>
#include <sys/zfs_rlock.h>
#include <sys/spa_impl.h>
#include <sys/zvol.h>
#include <sys/zvol_impl.h>

unsigned int zvol_inhibit_dev = 0;
unsigned int zvol_prefetch_bytes = (128 * 1024);
unsigned int zvol_volmode = ZFS_VOLMODE_GEOM;
unsigned int zvol_threads = 0;
unsigned int zvol_num_taskqs = 0;
unsigned int zvol_request_sync = 0;

struct hlist_head *zvol_htable;
static list_t zvol_state_list;
krwlock_t zvol_state_lock;
extern int zfs_bclone_wait_dirty;
zv_taskq_t zvol_taskqs;

typedef enum {
	ZVOL_ASYNC_CREATE_MINORS,
	ZVOL_ASYNC_REMOVE_MINORS,
	ZVOL_ASYNC_RENAME_MINORS,
	ZVOL_ASYNC_SET_SNAPDEV,
	ZVOL_ASYNC_SET_VOLMODE,
	ZVOL_ASYNC_MAX
} zvol_async_op_t;

typedef struct {
	zvol_async_op_t zt_op;
	char zt_name1[MAXNAMELEN];
	char zt_name2[MAXNAMELEN];
	uint64_t zt_value;
	uint32_t zt_total;
	uint32_t zt_done;
	int32_t zt_status;
	int zt_error;
} zvol_task_t;

zv_request_task_t *
zv_request_task_create(zv_request_t zvr)
{
	zv_request_task_t *task;
	task = kmem_alloc(sizeof (zv_request_task_t), KM_SLEEP);
	taskq_init_ent(&task->ent);
	task->zvr = zvr;
	return (task);
}

void
zv_request_task_free(zv_request_task_t *task)
{
	kmem_free(task, sizeof (*task));
}

uint64_t
zvol_name_hash(const char *name)
{
	uint64_t crc = -1ULL;
	ASSERT(zfs_crc64_table[128] == ZFS_CRC64_POLY);
	for (const uint8_t *p = (const uint8_t *)name; *p != 0; p++)
		crc = (crc >> 8) ^ zfs_crc64_table[(crc ^ (*p)) & 0xFF];
	return (crc);
}

/*
 * Find a zvol_state_t given the name and hash generated by zvol_name_hash.
 * If found, return with zv_suspend_lock and zv_state_lock taken, otherwise,
 * return (NULL) without the taking locks. The zv_suspend_lock is always taken
 * before zv_state_lock. The mode argument indicates the mode (including none)
 * for zv_suspend_lock to be taken.
 */
zvol_state_t *
zvol_find_by_name_hash(const char *name, uint64_t hash, int mode)
{
	zvol_state_t *zv;
	struct hlist_node *p = NULL;

	rw_enter(&zvol_state_lock, RW_READER);
	hlist_for_each(p, ZVOL_HT_HEAD(hash)) {
		zv = hlist_entry(p, zvol_state_t, zv_hlink);
		mutex_enter(&zv->zv_state_lock);
		if (zv->zv_hash == hash && strcmp(zv->zv_name, name) == 0) {
			/*
			 * this is the right zvol, take the locks in the
			 * right order
			 */
			if (mode != RW_NONE &&
			    !rw_tryenter(&zv->zv_suspend_lock, mode)) {
				mutex_exit(&zv->zv_state_lock);
				rw_enter(&zv->zv_suspend_lock, mode);
				mutex_enter(&zv->zv_state_lock);
				/*
				 * zvol cannot be renamed as we continue
				 * to hold zvol_state_lock
				 */
				ASSERT(zv->zv_hash == hash &&
				    strcmp(zv->zv_name, name) == 0);
			}
			rw_exit(&zvol_state_lock);
			return (zv);
		}
		mutex_exit(&zv->zv_state_lock);
	}
	rw_exit(&zvol_state_lock);

	return (NULL);
}

/*
 * Find a zvol_state_t given the name.
 * If found, return with zv_suspend_lock and zv_state_lock taken, otherwise,
 * return (NULL) without the taking locks. The zv_suspend_lock is always taken
 * before zv_state_lock. The mode argument indicates the mode (including none)
 * for zv_suspend_lock to be taken.
 */
static zvol_state_t *
zvol_find_by_name(const char *name, int mode)
{
	return (zvol_find_by_name_hash(name, zvol_name_hash(name), mode));
}

/*
 * ZFS_IOC_CREATE callback handles dmu zvol and zap object creation.
 */
void
zvol_create_cb(objset_t *os, void *arg, cred_t *cr, dmu_tx_t *tx)
{
	zfs_creat_t *zct = arg;
	nvlist_t *nvprops = zct->zct_props;
	int error;
	uint64_t volblocksize, volsize;

	VERIFY0(nvlist_lookup_uint64(nvprops,
	    zfs_prop_to_name(ZFS_PROP_VOLSIZE), &volsize));
	if (nvlist_lookup_uint64(nvprops,
	    zfs_prop_to_name(ZFS_PROP_VOLBLOCKSIZE), &volblocksize) != 0)
		volblocksize = zfs_prop_default_numeric(ZFS_PROP_VOLBLOCKSIZE);

	/*
	 * These properties must be removed from the list so the generic
	 * property setting step won't apply to them.
	 */
	VERIFY0(nvlist_remove_all(nvprops, zfs_prop_to_name(ZFS_PROP_VOLSIZE)));
	(void) nvlist_remove_all(nvprops,
	    zfs_prop_to_name(ZFS_PROP_VOLBLOCKSIZE));

	error = dmu_object_claim(os, ZVOL_OBJ, DMU_OT_ZVOL, volblocksize,
	    DMU_OT_NONE, 0, tx);
	ASSERT0(error);

	error = zap_create_claim(os, ZVOL_ZAP_OBJ, DMU_OT_ZVOL_PROP,
	    DMU_OT_NONE, 0, tx);
	ASSERT0(error);

	error = zap_update(os, ZVOL_ZAP_OBJ, "size", 8, 1, &volsize, tx);
	ASSERT0(error);
}

/*
 * ZFS_IOC_OBJSET_STATS entry point.
 */
int
zvol_get_stats(objset_t *os, nvlist_t *nv)
{
	int error;
	dmu_object_info_t *doi;
	uint64_t val;

	error = zap_lookup(os, ZVOL_ZAP_OBJ, "size", 8, 1, &val);
	if (error)
		return (error);

	dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_VOLSIZE, val);
	doi = kmem_alloc(sizeof (dmu_object_info_t), KM_SLEEP);
	error = dmu_object_info(os, ZVOL_OBJ, doi);

	if (error == 0) {
		dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_VOLBLOCKSIZE,
		    doi->doi_data_block_size);
	}

	kmem_free(doi, sizeof (dmu_object_info_t));

	return (error);
}

/*
 * Sanity check volume size.
 */
int
zvol_check_volsize(uint64_t volsize, uint64_t blocksize)
{
	if (volsize == 0)
		return (SET_ERROR(EINVAL));

	if (volsize % blocksize != 0)
		return (SET_ERROR(EINVAL));

#ifdef _ILP32
	if (volsize - 1 > SPEC_MAXOFFSET_T)
		return (SET_ERROR(EOVERFLOW));
#endif
	return (0);
}

/*
 * Ensure the zap is flushed then inform the VFS of the capacity change.
 */
static int
zvol_update_volsize(uint64_t volsize, objset_t *os)
{
	dmu_tx_t *tx;
	int error;
	uint64_t txg;

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, ZVOL_ZAP_OBJ, TRUE, NULL);
	dmu_tx_mark_netfree(tx);
	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		return (error);
	}
	txg = dmu_tx_get_txg(tx);

	error = zap_update(os, ZVOL_ZAP_OBJ, "size", 8, 1,
	    &volsize, tx);
	dmu_tx_commit(tx);

	txg_wait_synced(dmu_objset_pool(os), txg);

	if (error == 0)
		error = dmu_free_long_range(os,
		    ZVOL_OBJ, volsize, DMU_OBJECT_END);

	return (error);
}

/*
 * Set ZFS_PROP_VOLSIZE set entry point.  Note that modifying the volume
 * size will result in a udev "change" event being generated.
 */
int
zvol_set_volsize(const char *name, uint64_t volsize)
{
	objset_t *os = NULL;
	uint64_t readonly;
	int error;
	boolean_t owned = B_FALSE;

	error = dsl_prop_get_integer(name,
	    zfs_prop_to_name(ZFS_PROP_READONLY), &readonly, NULL);
	if (error != 0)
		return (error);
	if (readonly)
		return (SET_ERROR(EROFS));

	zvol_state_t *zv = zvol_find_by_name(name, RW_READER);

	ASSERT(zv == NULL || (MUTEX_HELD(&zv->zv_state_lock) &&
	    RW_READ_HELD(&zv->zv_suspend_lock)));

	if (zv == NULL || zv->zv_objset == NULL) {
		if (zv != NULL)
			rw_exit(&zv->zv_suspend_lock);
		if ((error = dmu_objset_own(name, DMU_OST_ZVOL, B_FALSE, B_TRUE,
		    FTAG, &os)) != 0) {
			if (zv != NULL)
				mutex_exit(&zv->zv_state_lock);
			return (error);
		}
		owned = B_TRUE;
		if (zv != NULL)
			zv->zv_objset = os;
	} else {
		os = zv->zv_objset;
	}

	dmu_object_info_t *doi = kmem_alloc(sizeof (*doi), KM_SLEEP);

	if ((error = dmu_object_info(os, ZVOL_OBJ, doi)) ||
	    (error = zvol_check_volsize(volsize, doi->doi_data_block_size)))
		goto out;

	error = zvol_update_volsize(volsize, os);
	if (error == 0 && zv != NULL) {
		zv->zv_volsize = volsize;
		zv->zv_changed = 1;
	}
out:
	kmem_free(doi, sizeof (dmu_object_info_t));

	if (owned) {
		dmu_objset_disown(os, B_TRUE, FTAG);
		if (zv != NULL)
			zv->zv_objset = NULL;
	} else {
		rw_exit(&zv->zv_suspend_lock);
	}

	if (zv != NULL)
		mutex_exit(&zv->zv_state_lock);

	if (error == 0 && zv != NULL)
		zvol_os_update_volsize(zv, volsize);

	return (error);
}

/*
 * Update volthreading.
 */
int
zvol_set_volthreading(const char *name, boolean_t value)
{
	zvol_state_t *zv = zvol_find_by_name(name, RW_NONE);
	if (zv == NULL)
		return (-1);
	zv->zv_threading = value;
	mutex_exit(&zv->zv_state_lock);
	return (0);
}

/*
 * Update zvol ro property.
 */
int
zvol_set_ro(const char *name, boolean_t value)
{
	zvol_state_t *zv = zvol_find_by_name(name, RW_NONE);
	if (zv == NULL)
		return (-1);
	if (value) {
		zvol_os_set_disk_ro(zv, 1);
		zv->zv_flags |= ZVOL_RDONLY;
	} else {
		zvol_os_set_disk_ro(zv, 0);
		zv->zv_flags &= ~ZVOL_RDONLY;
	}
	mutex_exit(&zv->zv_state_lock);
	return (0);
}

/*
 * Sanity check volume block size.
 */
int
zvol_check_volblocksize(const char *name, uint64_t volblocksize)
{
	/* Record sizes above 128k need the feature to be enabled */
	if (volblocksize > SPA_OLD_MAXBLOCKSIZE) {
		spa_t *spa;
		int error;

		if ((error = spa_open(name, &spa, FTAG)) != 0)
			return (error);

		if (!spa_feature_is_enabled(spa, SPA_FEATURE_LARGE_BLOCKS)) {
			spa_close(spa, FTAG);
			return (SET_ERROR(ENOTSUP));
		}

		/*
		 * We don't allow setting the property above 1MB,
		 * unless the tunable has been changed.
		 */
		if (volblocksize > zfs_max_recordsize) {
			spa_close(spa, FTAG);
			return (SET_ERROR(EDOM));
		}

		spa_close(spa, FTAG);
	}

	if (volblocksize < SPA_MINBLOCKSIZE ||
	    volblocksize > SPA_MAXBLOCKSIZE ||
	    !ISP2(volblocksize))
		return (SET_ERROR(EDOM));

	return (0);
}

/*
 * Replay a TX_TRUNCATE ZIL transaction if asked.  TX_TRUNCATE is how we
 * implement DKIOCFREE/free-long-range.
 */
static int
zvol_replay_truncate(void *arg1, void *arg2, boolean_t byteswap)
{
	zvol_state_t *zv = arg1;
	lr_truncate_t *lr = arg2;
	uint64_t offset, length;

	ASSERT3U(lr->lr_common.lrc_reclen, >=, sizeof (*lr));

	if (byteswap)
		byteswap_uint64_array(lr, sizeof (*lr));

	offset = lr->lr_offset;
	length = lr->lr_length;

	dmu_tx_t *tx = dmu_tx_create(zv->zv_objset);
	dmu_tx_mark_netfree(tx);
	int error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error != 0) {
		dmu_tx_abort(tx);
	} else {
		(void) zil_replaying(zv->zv_zilog, tx);
		dmu_tx_commit(tx);
		error = dmu_free_long_range(zv->zv_objset, ZVOL_OBJ, offset,
		    length);
	}

	return (error);
}

/*
 * Replay a TX_WRITE ZIL transaction that didn't get committed
 * after a system failure
 */
static int
zvol_replay_write(void *arg1, void *arg2, boolean_t byteswap)
{
	zvol_state_t *zv = arg1;
	lr_write_t *lr = arg2;
	objset_t *os = zv->zv_objset;
	char *data = (char *)(lr + 1);  /* data follows lr_write_t */
	uint64_t offset, length;
	dmu_tx_t *tx;
	int error;

	ASSERT3U(lr->lr_common.lrc_reclen, >=, sizeof (*lr));

	if (byteswap)
		byteswap_uint64_array(lr, sizeof (*lr));

	offset = lr->lr_offset;
	length = lr->lr_length;

	/* If it's a dmu_sync() block, write the whole block */
	if (lr->lr_common.lrc_reclen == sizeof (lr_write_t)) {
		uint64_t blocksize = BP_GET_LSIZE(&lr->lr_blkptr);
		if (length < blocksize) {
			offset -= offset % blocksize;
			length = blocksize;
		}
	}

	tx = dmu_tx_create(os);
	dmu_tx_hold_write(tx, ZVOL_OBJ, offset, length);
	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error) {
		dmu_tx_abort(tx);
	} else {
		dmu_write(os, ZVOL_OBJ, offset, length, data, tx,
		    DMU_READ_PREFETCH);
		(void) zil_replaying(zv->zv_zilog, tx);
		dmu_tx_commit(tx);
	}

	return (error);
}

/*
 * Replay a TX_CLONE_RANGE ZIL transaction that didn't get committed
 * after a system failure
 */
static int
zvol_replay_clone_range(void *arg1, void *arg2, boolean_t byteswap)
{
	zvol_state_t *zv = arg1;
	lr_clone_range_t *lr = arg2;
	objset_t *os = zv->zv_objset;
	dmu_tx_t *tx;
	int error;
	uint64_t blksz;
	uint64_t off;
	uint64_t len;

	ASSERT3U(lr->lr_common.lrc_reclen, >=, sizeof (*lr));
	ASSERT3U(lr->lr_common.lrc_reclen, >=, offsetof(lr_clone_range_t,
	    lr_bps[lr->lr_nbps]));

	if (byteswap)
		byteswap_uint64_array(lr, sizeof (*lr));

	ASSERT(spa_feature_is_enabled(dmu_objset_spa(os),
	    SPA_FEATURE_BLOCK_CLONING));

	off = lr->lr_offset;
	len = lr->lr_length;
	blksz = lr->lr_blksz;

	if ((off % blksz) != 0) {
		return (SET_ERROR(EINVAL));
	}

	error = dnode_hold(os, ZVOL_OBJ, zv, &zv->zv_dn);
	if (error != 0 || !zv->zv_dn)
		return (error);
	tx = dmu_tx_create(os);
	dmu_tx_hold_clone_by_dnode(tx, zv->zv_dn, off, len, blksz);
	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error != 0) {
		dmu_tx_abort(tx);
		goto out;
	}
	error = dmu_brt_clone(zv->zv_objset, ZVOL_OBJ, off, len,
	    tx, lr->lr_bps, lr->lr_nbps);
	if (error != 0) {
		dmu_tx_commit(tx);
		goto out;
	}

	/*
	 * zil_replaying() not only check if we are replaying ZIL, but also
	 * updates the ZIL header to record replay progress.
	 */
	VERIFY(zil_replaying(zv->zv_zilog, tx));
	dmu_tx_commit(tx);

out:
	dnode_rele(zv->zv_dn, zv);
	zv->zv_dn = NULL;
	return (error);
}

int
zvol_clone_range(zvol_state_t *zv_src, uint64_t inoff, zvol_state_t *zv_dst,
    uint64_t outoff, uint64_t len)
{
	zilog_t	*zilog_dst;
	zfs_locked_range_t *inlr, *outlr;
	objset_t *inos, *outos;
	dmu_tx_t *tx;
	blkptr_t *bps;
	size_t maxblocks;
	int error = 0;

	rw_enter(&zv_dst->zv_suspend_lock, RW_READER);
	if (zv_dst->zv_zilog == NULL) {
		rw_exit(&zv_dst->zv_suspend_lock);
		rw_enter(&zv_dst->zv_suspend_lock, RW_WRITER);
		if (zv_dst->zv_zilog == NULL) {
			zv_dst->zv_zilog = zil_open(zv_dst->zv_objset,
			    zvol_get_data, &zv_dst->zv_kstat.dk_zil_sums);
			zv_dst->zv_flags |= ZVOL_WRITTEN_TO;
			VERIFY0((zv_dst->zv_zilog->zl_header->zh_flags &
			    ZIL_REPLAY_NEEDED));
		}
		rw_downgrade(&zv_dst->zv_suspend_lock);
	}
	if (zv_src != zv_dst)
		rw_enter(&zv_src->zv_suspend_lock, RW_READER);

	inos = zv_src->zv_objset;
	outos = zv_dst->zv_objset;

	/*
	 * Sanity checks
	 */
	if (!spa_feature_is_enabled(dmu_objset_spa(outos),
	    SPA_FEATURE_BLOCK_CLONING)) {
		error = SET_ERROR(EOPNOTSUPP);
		goto out;
	}
	if (dmu_objset_spa(inos) != dmu_objset_spa(outos)) {
		error = SET_ERROR(EXDEV);
		goto out;
	}
	if (inos->os_encrypted != outos->os_encrypted) {
		error = SET_ERROR(EXDEV);
		goto out;
	}
	if (zv_src->zv_volblocksize != zv_dst->zv_volblocksize) {
		error = SET_ERROR(EINVAL);
		goto out;
	}
	if (inoff >= zv_src->zv_volsize || outoff >= zv_dst->zv_volsize) {
		goto out;
	}

	/*
	 * Do not read beyond boundary
	 */
	if (len > zv_src->zv_volsize - inoff)
		len = zv_src->zv_volsize - inoff;
	if (len > zv_dst->zv_volsize - outoff)
		len = zv_dst->zv_volsize - outoff;
	if (len == 0)
		goto out;

	/*
	 * No overlapping if we are cloning within the same file
	 */
	if (zv_src == zv_dst) {
		if (inoff < outoff + len && outoff < inoff + len) {
			error = SET_ERROR(EINVAL);
			goto out;
		}
	}

	/*
	 * Offsets and length must be at block boundaries
	 */
	if ((inoff % zv_src->zv_volblocksize) != 0 ||
	    (outoff % zv_dst->zv_volblocksize) != 0) {
		error = SET_ERROR(EINVAL);
		goto out;
	}

	/*
	 * Length must be multiple of block size
	 */
	if ((len % zv_src->zv_volblocksize) != 0) {
		error = SET_ERROR(EINVAL);
		goto out;
	}

	zilog_dst = zv_dst->zv_zilog;
	maxblocks = zil_max_log_data(zilog_dst, sizeof (lr_clone_range_t)) /
	    sizeof (bps[0]);
	bps = vmem_alloc(sizeof (bps[0]) * maxblocks, KM_SLEEP);
	/*
	 * Maintain predictable lock order.
	 */
	if (zv_src < zv_dst || (zv_src == zv_dst && inoff < outoff)) {
		inlr = zfs_rangelock_enter(&zv_src->zv_rangelock, inoff, len,
		    RL_READER);
		outlr = zfs_rangelock_enter(&zv_dst->zv_rangelock, outoff, len,
		    RL_WRITER);
	} else {
		outlr = zfs_rangelock_enter(&zv_dst->zv_rangelock, outoff, len,
		    RL_WRITER);
		inlr = zfs_rangelock_enter(&zv_src->zv_rangelock, inoff, len,
		    RL_READER);
	}

	while (len > 0) {
		uint64_t size, last_synced_txg;
		size_t nbps = maxblocks;
		size = MIN(zv_src->zv_volblocksize * maxblocks, len);
		last_synced_txg = spa_last_synced_txg(
		    dmu_objset_spa(zv_src->zv_objset));
		error = dmu_read_l0_bps(zv_src->zv_objset, ZVOL_OBJ, inoff,
		    size, bps, &nbps);
		if (error != 0) {
			/*
			 * If we are trying to clone a block that was created
			 * in the current transaction group, the error will be
			 * EAGAIN here.  Based on zfs_bclone_wait_dirty either
			 * return a shortened range to the caller so it can
			 * fallback, or wait for the next TXG and check again.
			 */
			if (error == EAGAIN && zfs_bclone_wait_dirty) {
				txg_wait_synced(dmu_objset_pool
				    (zv_src->zv_objset), last_synced_txg + 1);
					continue;
			}
			break;
		}

		tx = dmu_tx_create(zv_dst->zv_objset);
		dmu_tx_hold_clone_by_dnode(tx, zv_dst->zv_dn, outoff, size,
		    zv_src->zv_volblocksize);
		error = dmu_tx_assign(tx, DMU_TX_WAIT);
		if (error != 0) {
			dmu_tx_abort(tx);
			break;
		}
		error = dmu_brt_clone(zv_dst->zv_objset, ZVOL_OBJ, outoff, size,
		    tx, bps, nbps);
		if (error != 0) {
			dmu_tx_commit(tx);
			break;
		}
		zvol_log_clone_range(zilog_dst, tx, TX_CLONE_RANGE, outoff,
		    size, zv_src->zv_volblocksize, bps, nbps);
		dmu_tx_commit(tx);
		inoff += size;
		outoff += size;
		len -= size;
	}
	vmem_free(bps, sizeof (bps[0]) * maxblocks);
	zfs_rangelock_exit(outlr);
	zfs_rangelock_exit(inlr);
	if (error == 0 && zv_dst->zv_objset->os_sync == ZFS_SYNC_ALWAYS) {
		error = zil_commit(zilog_dst, ZVOL_OBJ);
	}
out:
	if (zv_src != zv_dst)
		rw_exit(&zv_src->zv_suspend_lock);
	rw_exit(&zv_dst->zv_suspend_lock);
	return (error);
}

/*
 * Handles TX_CLONE_RANGE transactions.
 */
void
zvol_log_clone_range(zilog_t *zilog, dmu_tx_t *tx, int txtype, uint64_t off,
    uint64_t len, uint64_t blksz, const blkptr_t *bps, size_t nbps)
{
	itx_t *itx;
	lr_clone_range_t *lr;
	uint64_t partlen, max_log_data;
	size_t partnbps;

	if (zil_replaying(zilog, tx))
		return;

	max_log_data = zil_max_log_data(zilog, sizeof (lr_clone_range_t));

	while (nbps > 0) {
		partnbps = MIN(nbps, max_log_data / sizeof (bps[0]));
		partlen = partnbps * blksz;
		ASSERT3U(partlen, <, len + blksz);
		partlen = MIN(partlen, len);

		itx = zil_itx_create(txtype,
		    sizeof (*lr) + sizeof (bps[0]) * partnbps);
		lr = (lr_clone_range_t *)&itx->itx_lr;
		lr->lr_foid = ZVOL_OBJ;
		lr->lr_offset = off;
		lr->lr_length = partlen;
		lr->lr_blksz = blksz;
		lr->lr_nbps = partnbps;
		memcpy(lr->lr_bps, bps, sizeof (bps[0]) * partnbps);

		zil_itx_assign(zilog, itx, tx);

		bps += partnbps;
		ASSERT3U(nbps, >=, partnbps);
		nbps -= partnbps;
		off += partlen;
		ASSERT3U(len, >=, partlen);
		len -= partlen;
	}
}

static int
zvol_replay_err(void *arg1, void *arg2, boolean_t byteswap)
{
	(void) arg1, (void) arg2, (void) byteswap;
	return (SET_ERROR(ENOTSUP));
}

/*
 * Callback vectors for replaying records.
 * Only TX_WRITE and TX_TRUNCATE are needed for zvol.
 */
zil_replay_func_t *const zvol_replay_vector[TX_MAX_TYPE] = {
	zvol_replay_err,	/* no such transaction type */
	zvol_replay_err,	/* TX_CREATE */
	zvol_replay_err,	/* TX_MKDIR */
	zvol_replay_err,	/* TX_MKXATTR */
	zvol_replay_err,	/* TX_SYMLINK */
	zvol_replay_err,	/* TX_REMOVE */
	zvol_replay_err,	/* TX_RMDIR */
	zvol_replay_err,	/* TX_LINK */
	zvol_replay_err,	/* TX_RENAME */
	zvol_replay_write,	/* TX_WRITE */
	zvol_replay_truncate,	/* TX_TRUNCATE */
	zvol_replay_err,	/* TX_SETATTR */
	zvol_replay_err,	/* TX_ACL_V0 */
	zvol_replay_err,	/* TX_ACL */
	zvol_replay_err,	/* TX_CREATE_ACL */
	zvol_replay_err,	/* TX_CREATE_ATTR */
	zvol_replay_err,	/* TX_CREATE_ACL_ATTR */
	zvol_replay_err,	/* TX_MKDIR_ACL */
	zvol_replay_err,	/* TX_MKDIR_ATTR */
	zvol_replay_err,	/* TX_MKDIR_ACL_ATTR */
	zvol_replay_err,	/* TX_WRITE2 */
	zvol_replay_err,	/* TX_SETSAXATTR */
	zvol_replay_err,	/* TX_RENAME_EXCHANGE */
	zvol_replay_err,	/* TX_RENAME_WHITEOUT */
	zvol_replay_clone_range,	/* TX_CLONE_RANGE */
};

/*
 * zvol_log_write() handles TX_WRITE transactions.
 */
void
zvol_log_write(zvol_state_t *zv, dmu_tx_t *tx, uint64_t offset,
    uint64_t size, boolean_t commit)
{
	uint32_t blocksize = zv->zv_volblocksize;
	zilog_t *zilog = zv->zv_zilog;
	itx_wr_state_t write_state;
	uint64_t log_size = 0;

	if (zil_replaying(zilog, tx))
		return;

	write_state = zil_write_state(zilog, size, blocksize, B_FALSE, commit);

	while (size) {
		itx_t *itx;
		lr_write_t *lr;
		itx_wr_state_t wr_state = write_state;
		ssize_t len = size;

		if (wr_state == WR_COPIED && size > zil_max_copied_data(zilog))
			wr_state = WR_NEED_COPY;
		else if (wr_state == WR_INDIRECT)
			len = MIN(blocksize - P2PHASE(offset, blocksize), size);

		itx = zil_itx_create(TX_WRITE, sizeof (*lr) +
		    (wr_state == WR_COPIED ? len : 0));
		lr = (lr_write_t *)&itx->itx_lr;
		if (wr_state == WR_COPIED &&
		    dmu_read_by_dnode(zv->zv_dn, offset, len, lr + 1,
		    DMU_READ_NO_PREFETCH | DMU_KEEP_CACHING) != 0) {
			zil_itx_destroy(itx, 0);
			itx = zil_itx_create(TX_WRITE, sizeof (*lr));
			lr = (lr_write_t *)&itx->itx_lr;
			wr_state = WR_NEED_COPY;
		}

		log_size += itx->itx_size;
		if (wr_state == WR_NEED_COPY)
			log_size += len;

		itx->itx_wr_state = wr_state;
		lr->lr_foid = ZVOL_OBJ;
		lr->lr_offset = offset;
		lr->lr_length = len;
		lr->lr_blkoff = 0;
		BP_ZERO(&lr->lr_blkptr);

		itx->itx_private = zv;

		zil_itx_assign(zilog, itx, tx);

		offset += len;
		size -= len;
	}

	dsl_pool_wrlog_count(zilog->zl_dmu_pool, log_size, tx->tx_txg);
}

/*
 * Log a DKIOCFREE/free-long-range to the ZIL with TX_TRUNCATE.
 */
void
zvol_log_truncate(zvol_state_t *zv, dmu_tx_t *tx, uint64_t off, uint64_t len)
{
	itx_t *itx;
	lr_truncate_t *lr;
	zilog_t *zilog = zv->zv_zilog;

	if (zil_replaying(zilog, tx))
		return;

	itx = zil_itx_create(TX_TRUNCATE, sizeof (*lr));
	lr = (lr_truncate_t *)&itx->itx_lr;
	lr->lr_foid = ZVOL_OBJ;
	lr->lr_offset = off;
	lr->lr_length = len;

	zil_itx_assign(zilog, itx, tx);
}


static void
zvol_get_done(zgd_t *zgd, int error)
{
	(void) error;
	if (zgd->zgd_db)
		dmu_buf_rele(zgd->zgd_db, zgd);

	zfs_rangelock_exit(zgd->zgd_lr);

	kmem_free(zgd, sizeof (zgd_t));
}

/*
 * Get data to generate a TX_WRITE intent log record.
 */
int
zvol_get_data(void *arg, uint64_t arg2, lr_write_t *lr, char *buf,
    struct lwb *lwb, zio_t *zio)
{
	zvol_state_t *zv = arg;
	uint64_t offset = lr->lr_offset;
	uint64_t size = lr->lr_length;
	dmu_buf_t *db;
	zgd_t *zgd;
	int error;

	ASSERT3P(lwb, !=, NULL);
	ASSERT3U(size, !=, 0);

	zgd = kmem_zalloc(sizeof (zgd_t), KM_SLEEP);
	zgd->zgd_lwb = lwb;

	/*
	 * Write records come in two flavors: immediate and indirect.
	 * For small writes it's cheaper to store the data with the
	 * log record (immediate); for large writes it's cheaper to
	 * sync the data and get a pointer to it (indirect) so that
	 * we don't have to write the data twice.
	 */
	if (buf != NULL) { /* immediate write */
		zgd->zgd_lr = zfs_rangelock_enter(&zv->zv_rangelock, offset,
		    size, RL_READER);
		error = dmu_read_by_dnode(zv->zv_dn, offset, size, buf,
		    DMU_READ_NO_PREFETCH | DMU_KEEP_CACHING);
	} else { /* indirect write */
		ASSERT3P(zio, !=, NULL);
		/*
		 * Have to lock the whole block to ensure when it's written out
		 * and its checksum is being calculated that no one can change
		 * the data. Contrarily to zfs_get_data we need not re-check
		 * blocksize after we get the lock because it cannot be changed.
		 */
		size = zv->zv_volblocksize;
		offset = P2ALIGN_TYPED(offset, size, uint64_t);
		zgd->zgd_lr = zfs_rangelock_enter(&zv->zv_rangelock, offset,
		    size, RL_READER);
		error = dmu_buf_hold_noread_by_dnode(zv->zv_dn, offset, zgd,
		    &db);
		if (error == 0) {
			blkptr_t *bp = &lr->lr_blkptr;

			zgd->zgd_db = db;
			zgd->zgd_bp = bp;

			ASSERT(db != NULL);
			ASSERT(db->db_offset == offset);
			ASSERT(db->db_size == size);

			error = dmu_sync(zio, lr->lr_common.lrc_txg,
			    zvol_get_done, zgd);

			if (error == 0)
				return (0);
		}
	}

	zvol_get_done(zgd, error);

	return (error);
}

/*
 * The zvol_state_t's are inserted into zvol_state_list and zvol_htable.
 */

void
zvol_insert(zvol_state_t *zv)
{
	ASSERT(RW_WRITE_HELD(&zvol_state_lock));
	list_insert_head(&zvol_state_list, zv);
	hlist_add_head(&zv->zv_hlink, ZVOL_HT_HEAD(zv->zv_hash));
}

/*
 * Simply remove the zvol from to list of zvols.
 */
static void
zvol_remove(zvol_state_t *zv)
{
	ASSERT(RW_WRITE_HELD(&zvol_state_lock));
	list_remove(&zvol_state_list, zv);
	hlist_del(&zv->zv_hlink);
}

/*
 * Setup zv after we just own the zv->objset
 */
static int
zvol_setup_zv(zvol_state_t *zv)
{
	uint64_t volsize;
	int error;
	uint64_t ro;
	objset_t *os = zv->zv_objset;

	ASSERT(MUTEX_HELD(&zv->zv_state_lock));
	ASSERT(RW_LOCK_HELD(&zv->zv_suspend_lock));

	zv->zv_zilog = NULL;
	zv->zv_flags &= ~ZVOL_WRITTEN_TO;

	error = dsl_prop_get_integer(zv->zv_name, "readonly", &ro, NULL);
	if (error)
		return (error);

	error = zap_lookup(os, ZVOL_ZAP_OBJ, "size", 8, 1, &volsize);
	if (error)
		return (error);

	error = dnode_hold(os, ZVOL_OBJ, zv, &zv->zv_dn);
	if (error)
		return (error);

	zvol_os_set_capacity(zv, volsize >> 9);
	zv->zv_volsize = volsize;

	if (ro || dmu_objset_is_snapshot(os) ||
	    !spa_writeable(dmu_objset_spa(os))) {
		zvol_os_set_disk_ro(zv, 1);
		zv->zv_flags |= ZVOL_RDONLY;
	} else {
		zvol_os_set_disk_ro(zv, 0);
		zv->zv_flags &= ~ZVOL_RDONLY;
	}
	return (0);
}

/*
 * Shutdown every zv_objset related stuff except zv_objset itself.
 * The is the reverse of zvol_setup_zv.
 */
static void
zvol_shutdown_zv(zvol_state_t *zv)
{
	ASSERT(MUTEX_HELD(&zv->zv_state_lock) &&
	    RW_LOCK_HELD(&zv->zv_suspend_lock));

	if (zv->zv_flags & ZVOL_WRITTEN_TO) {
		ASSERT(zv->zv_zilog != NULL);
		zil_close(zv->zv_zilog);
	}

	zv->zv_zilog = NULL;

	dnode_rele(zv->zv_dn, zv);
	zv->zv_dn = NULL;

	/*
	 * Evict cached data. We must write out any dirty data before
	 * disowning the dataset.
	 */
	if (zv->zv_flags & ZVOL_WRITTEN_TO)
		txg_wait_synced(dmu_objset_pool(zv->zv_objset), 0);
	dmu_objset_evict_dbufs(zv->zv_objset);
}

/*
 * return the proper tag for rollback and recv
 */
void *
zvol_tag(zvol_state_t *zv)
{
	ASSERT(RW_WRITE_HELD(&zv->zv_suspend_lock));
	return (zv->zv_open_count > 0 ? zv : NULL);
}

/*
 * Suspend the zvol for recv and rollback.
 */
int
zvol_suspend(const char *name, zvol_state_t **zvp)
{
	zvol_state_t *zv;

	zv = zvol_find_by_name(name, RW_WRITER);

	if (zv == NULL)
		return (SET_ERROR(ENOENT));

	/* block all I/O, release in zvol_resume. */
	ASSERT(MUTEX_HELD(&zv->zv_state_lock));
	ASSERT(RW_WRITE_HELD(&zv->zv_suspend_lock));

	/*
	 * If it's being removed, unlock and return error. It doesn't make any
	 * sense to try to suspend a zvol being removed, but being here also
	 * means that zvol_remove_minors_impl() is about to call zvol_remove()
	 * and then destroy the zvol_state_t, so returning a pointer to it for
	 * the caller to mess with would be a disaster anyway.
	 */
	if (zv->zv_flags & ZVOL_REMOVING) {
		mutex_exit(&zv->zv_state_lock);
		rw_exit(&zv->zv_suspend_lock);
		/* NB: Returning EIO here to match zfsvfs_teardown() */
		return (SET_ERROR(EIO));
	}

	atomic_inc(&zv->zv_suspend_ref);

	if (zv->zv_open_count > 0)
		zvol_shutdown_zv(zv);

	/*
	 * do not hold zv_state_lock across suspend/resume to
	 * avoid locking up zvol lookups
	 */
	mutex_exit(&zv->zv_state_lock);

	/* zv_suspend_lock is released in zvol_resume() */
	*zvp = zv;
	return (0);
}

int
zvol_resume(zvol_state_t *zv)
{
	int error = 0;

	ASSERT(RW_WRITE_HELD(&zv->zv_suspend_lock));

	mutex_enter(&zv->zv_state_lock);

	if (zv->zv_open_count > 0) {
		VERIFY0(dmu_objset_hold(zv->zv_name, zv, &zv->zv_objset));
		VERIFY3P(zv->zv_objset->os_dsl_dataset->ds_owner, ==, zv);
		VERIFY(dsl_dataset_long_held(zv->zv_objset->os_dsl_dataset));
		dmu_objset_rele(zv->zv_objset, zv);

		error = zvol_setup_zv(zv);
	}

	mutex_exit(&zv->zv_state_lock);

	rw_exit(&zv->zv_suspend_lock);
	/*
	 * We need this because we don't hold zvol_state_lock while releasing
	 * zv_suspend_lock. zvol_remove_minors_impl thus cannot check
	 * zv_suspend_lock to determine it is safe to free because rwlock is
	 * not inherent atomic.
	 */
	atomic_dec(&zv->zv_suspend_ref);

	if (zv->zv_flags & ZVOL_REMOVING)
		cv_broadcast(&zv->zv_removing_cv);

	return (error);
}

int
zvol_first_open(zvol_state_t *zv, boolean_t readonly)
{
	objset_t *os;
	int error;

	ASSERT(RW_READ_HELD(&zv->zv_suspend_lock));
	ASSERT(MUTEX_HELD(&zv->zv_state_lock));
	ASSERT(spa_namespace_held());

	boolean_t ro = (readonly || (strchr(zv->zv_name, '@') != NULL));
	error = dmu_objset_own(zv->zv_name, DMU_OST_ZVOL, ro, B_TRUE, zv, &os);
	if (error)
		return (error);

	zv->zv_objset = os;

	error = zvol_setup_zv(zv);
	if (error) {
		dmu_objset_disown(os, 1, zv);
		zv->zv_objset = NULL;
	}

	return (error);
}

void
zvol_last_close(zvol_state_t *zv)
{
	ASSERT(RW_READ_HELD(&zv->zv_suspend_lock));
	ASSERT(MUTEX_HELD(&zv->zv_state_lock));

	if (zv->zv_flags & ZVOL_REMOVING)
		cv_broadcast(&zv->zv_removing_cv);

	zvol_shutdown_zv(zv);

	dmu_objset_disown(zv->zv_objset, 1, zv);
	zv->zv_objset = NULL;
}

typedef struct minors_job {
	list_t *list;
	list_node_t link;
	/* input */
	char *name;
	/* output */
	int error;
} minors_job_t;

/*
 * Prefetch zvol dnodes for the minors_job
 */
static void
zvol_prefetch_minors_impl(void *arg)
{
	minors_job_t *job = arg;
	char *dsname = job->name;
	objset_t *os = NULL;

	job->error = dmu_objset_own(dsname, DMU_OST_ZVOL, B_TRUE, B_TRUE,
	    FTAG, &os);
	if (job->error == 0) {
		dmu_prefetch_dnode(os, ZVOL_OBJ, ZIO_PRIORITY_SYNC_READ);
		dmu_objset_disown(os, B_TRUE, FTAG);
	}
}

/*
 * Mask errors to continue dmu_objset_find() traversal
 */
static int
zvol_create_snap_minor_cb(const char *dsname, void *arg)
{
	minors_job_t *j = arg;
	list_t *minors_list = j->list;
	const char *name = j->name;

	ASSERT0(spa_namespace_held());

	/* skip the designated dataset */
	if (name && strcmp(dsname, name) == 0)
		return (0);

	/* at this point, the dsname should name a snapshot */
	if (strchr(dsname, '@') == 0) {
		dprintf("zvol_create_snap_minor_cb(): "
		    "%s is not a snapshot name\n", dsname);
	} else {
		minors_job_t *job;
		char *n = kmem_strdup(dsname);
		if (n == NULL)
			return (0);

		job = kmem_alloc(sizeof (minors_job_t), KM_SLEEP);
		job->name = n;
		job->list = minors_list;
		job->error = 0;
		list_insert_tail(minors_list, job);
		/* don't care if dispatch fails, because job->error is 0 */
		taskq_dispatch(system_taskq, zvol_prefetch_minors_impl, job,
		    TQ_SLEEP);
	}

	return (0);
}

/*
 * If spa_keystore_load_wkey() is called for an encrypted zvol,
 * we need to look for any clones also using the key. This function
 * is "best effort" - so we just skip over it if there are failures.
 */
static void
zvol_add_clones(const char *dsname, list_t *minors_list)
{
	/* Also check if it has clones */
	dsl_dir_t *dd = NULL;
	dsl_pool_t *dp = NULL;

	if (dsl_pool_hold(dsname, FTAG, &dp) != 0)
		return;

	if (!spa_feature_is_enabled(dp->dp_spa,
	    SPA_FEATURE_ENCRYPTION))
		goto out;

	if (dsl_dir_hold(dp, dsname, FTAG, &dd, NULL) != 0)
		goto out;

	if (dsl_dir_phys(dd)->dd_clones == 0)
		goto out;

	zap_cursor_t *zc = kmem_alloc(sizeof (zap_cursor_t), KM_SLEEP);
	zap_attribute_t *za = zap_attribute_alloc();
	objset_t *mos = dd->dd_pool->dp_meta_objset;

	for (zap_cursor_init(zc, mos, dsl_dir_phys(dd)->dd_clones);
	    zap_cursor_retrieve(zc, za) == 0;
	    zap_cursor_advance(zc)) {
		dsl_dataset_t *clone;
		minors_job_t *job;

		if (dsl_dataset_hold_obj(dd->dd_pool,
		    za->za_first_integer, FTAG, &clone) == 0) {

			char name[ZFS_MAX_DATASET_NAME_LEN];
			dsl_dataset_name(clone, name);

			char *n = kmem_strdup(name);
			job = kmem_alloc(sizeof (minors_job_t), KM_SLEEP);
			job->name = n;
			job->list = minors_list;
			job->error = 0;
			list_insert_tail(minors_list, job);

			dsl_dataset_rele(clone, FTAG);
		}
	}
	zap_cursor_fini(zc);
	zap_attribute_free(za);
	kmem_free(zc, sizeof (zap_cursor_t));

out:
	if (dd != NULL)
		dsl_dir_rele(dd, FTAG);
	dsl_pool_rele(dp, FTAG);
}

/*
 * Mask errors to continue dmu_objset_find() traversal
 */
static int
zvol_create_minors_cb(const char *dsname, void *arg)
{
	uint64_t snapdev;
	int error;
	list_t *minors_list = arg;

	ASSERT0(spa_namespace_held());

	error = dsl_prop_get_integer(dsname, "snapdev", &snapdev, NULL);
	if (error)
		return (0);

	/*
	 * Given the name and the 'snapdev' property, create device minor nodes
	 * with the linkages to zvols/snapshots as needed.
	 * If the name represents a zvol, create a minor node for the zvol, then
	 * check if its snapshots are 'visible', and if so, iterate over the
	 * snapshots and create device minor nodes for those.
	 */
	if (strchr(dsname, '@') == 0) {
		minors_job_t *job;
		char *n = kmem_strdup(dsname);
		if (n == NULL)
			return (0);

		job = kmem_alloc(sizeof (minors_job_t), KM_SLEEP);
		job->name = n;
		job->list = minors_list;
		job->error = 0;
		list_insert_tail(minors_list, job);
		/* don't care if dispatch fails, because job->error is 0 */
		taskq_dispatch(system_taskq, zvol_prefetch_minors_impl, job,
		    TQ_SLEEP);

		zvol_add_clones(dsname, minors_list);

		if (snapdev == ZFS_SNAPDEV_VISIBLE) {
			/*
			 * traverse snapshots only, do not traverse children,
			 * and skip the 'dsname'
			 */
			(void) dmu_objset_find(dsname,
			    zvol_create_snap_minor_cb, (void *)job,
			    DS_FIND_SNAPSHOTS);
		}
	} else {
		dprintf("zvol_create_minors_cb(): %s is not a zvol name\n",
		    dsname);
	}

	return (0);
}

static void
zvol_task_update_status(zvol_task_t *task, uint64_t total, uint64_t done,
    int error)
{

	task->zt_total += total;
	task->zt_done += done;
	if (task->zt_total != task->zt_done) {
		task->zt_status = -1;
		if (error)
			task->zt_error = error;
	}
}

static void
zvol_task_report_status(zvol_task_t *task)
{
#ifdef ZFS_DEBUG
	static const char *const msg[] = {
		"create",
		"remove",
		"rename",
		"set snapdev",
		"set volmode",
		"unknown",
	};

	if (task->zt_status == 0)
		return;

	zvol_async_op_t op = MIN(task->zt_op, ZVOL_ASYNC_MAX);
	if (task->zt_error) {
		dprintf("The %s minors zvol task was not ok, last error %d\n",
		    msg[op], task->zt_error);
	} else {
		dprintf("The %s minors zvol task was not ok\n", msg[op]);
	}
#else
	(void) task;
#endif
}

/*
 * Create minors for the specified dataset, including children and snapshots.
 * Pay attention to the 'snapdev' property and iterate over the snapshots
 * only if they are 'visible'. This approach allows one to assure that the
 * snapshot metadata is read from disk only if it is needed.
 *
 * The name can represent a dataset to be recursively scanned for zvols and
 * their snapshots, or a single zvol snapshot. If the name represents a
 * dataset, the scan is performed in two nested stages:
 * - scan the dataset for zvols, and
 * - for each zvol, create a minor node, then check if the zvol's snapshots
 *   are 'visible', and only then iterate over the snapshots if needed
 *
 * If the name represents a snapshot, a check is performed if the snapshot is
 * 'visible' (which also verifies that the parent is a zvol), and if so,
 * a minor node for that snapshot is created.
 */
static void
zvol_create_minors_impl(zvol_task_t *task)
{
	const char *name = task->zt_name1;
	list_t minors_list;
	minors_job_t *job;
	uint64_t snapdev;
	int total = 0, done = 0, last_error, error;

	/*
	 * Note: the dsl_pool_config_lock must not be held.
	 * Minor node creation needs to obtain the zvol_state_lock.
	 * zvol_open() obtains the zvol_state_lock and then the dsl pool
	 * config lock.  Therefore, we can't have the config lock now if
	 * we are going to wait for the zvol_state_lock, because it
	 * would be a lock order inversion which could lead to deadlock.
	 */

	if (zvol_inhibit_dev) {
		return;
	}

	/*
	 * This is the list for prefetch jobs. Whenever we found a match
	 * during dmu_objset_find, we insert a minors_job to the list and do
	 * taskq_dispatch to parallel prefetch zvol dnodes. Note we don't need
	 * any lock because all list operation is done on the current thread.
	 *
	 * We will use this list to do zvol_os_create_minor after prefetch
	 * so we don't have to traverse using dmu_objset_find again.
	 */
	list_create(&minors_list, sizeof (minors_job_t),
	    offsetof(minors_job_t, link));


	if (strchr(name, '@') != NULL) {
		error = dsl_prop_get_integer(name, "snapdev", &snapdev, NULL);
		if (error == 0 && snapdev == ZFS_SNAPDEV_VISIBLE) {
			error = zvol_os_create_minor(name);
			if (error == 0) {
				done++;
			} else {
				last_error = error;
			}
			total++;
		}
	} else {
		fstrans_cookie_t cookie = spl_fstrans_mark();
		(void) dmu_objset_find(name, zvol_create_minors_cb,
		    &minors_list, DS_FIND_CHILDREN);
		spl_fstrans_unmark(cookie);
	}

	taskq_wait_outstanding(system_taskq, 0);

	/*
	 * Prefetch is completed, we can do zvol_os_create_minor
	 * sequentially.
	 */
	while ((job = list_remove_head(&minors_list)) != NULL) {
		if (!job->error) {
			error = zvol_os_create_minor(job->name);
			if (error == 0) {
				done++;
			} else {
				last_error = error;
			}
		} else if (job->error == EINVAL) {
			/*
			 * The objset, with the name requested by current job
			 * exist, but have the type different from zvol.
			 * Just ignore this sort of errors.
			 */
			done++;
		} else {
			last_error = job->error;
		}
		total++;
		kmem_strfree(job->name);
		kmem_free(job, sizeof (minors_job_t));
	}

	list_destroy(&minors_list);
	zvol_task_update_status(task, total, done, last_error);
}

/*
 * Remove minors for specified dataset and, optionally, its children and
 * snapshots.
 */
static void
zvol_remove_minors_impl(zvol_task_t *task)
{
	zvol_state_t *zv, *zv_next;
	const char *name = task ? task->zt_name1 : NULL;
	int namelen = ((name) ? strlen(name) : 0);
	boolean_t children = task ? !!task->zt_value : B_TRUE;

	if (zvol_inhibit_dev)
		return;

	/*
	 * We collect up zvols that we want to remove on a separate list, so
	 * that we don't have to hold zvol_state_lock for the whole time.
	 *
	 * We can't remove them from the global lists until we're completely
	 * done with them, because that would make them appear to ZFS-side ops
	 * that they don't exist, and the name might be reused, which can't be
	 * good.
	 */
	list_t remove_list;
	list_create(&remove_list, sizeof (zvol_state_t),
	    offsetof(zvol_state_t, zv_remove_node));

	rw_enter(&zvol_state_lock, RW_READER);

	for (zv = list_head(&zvol_state_list); zv != NULL; zv = zv_next) {
		zv_next = list_next(&zvol_state_list, zv);

		mutex_enter(&zv->zv_state_lock);
		if (zv->zv_flags & ZVOL_REMOVING) {
			/* Another thread is handling shutdown, skip it. */
			mutex_exit(&zv->zv_state_lock);
			continue;
		}

		/*
		 * This zvol should be removed if:
		 * - no name was offered (ie removing all at shutdown); or
		 * - name matches exactly; or
		 * - we were asked to remove children, and
		 *   - the start of the name matches, and
		 *   - there is a '/' immediately after the matched name; or
		 *   - there is a '@' immediately after the matched name
		 */
		if (name == NULL || strcmp(zv->zv_name, name) == 0 ||
		    (children && strncmp(zv->zv_name, name, namelen) == 0 &&
		    (zv->zv_name[namelen] == '/' ||
		    zv->zv_name[namelen] == '@'))) {

			/*
			 * Matched, so mark it removal. We want to take the
			 * write half of the suspend lock to make sure that
			 * the zvol is not suspended, and give any data ops
			 * chance to finish.
			 */
			mutex_exit(&zv->zv_state_lock);
			rw_enter(&zv->zv_suspend_lock, RW_WRITER);
			mutex_enter(&zv->zv_state_lock);

			if (zv->zv_flags & ZVOL_REMOVING) {
				/* Another thread has taken it, let them. */
				mutex_exit(&zv->zv_state_lock);
				rw_exit(&zv->zv_suspend_lock);
				continue;
			}

			/*
			 * Mark it and unlock. New entries will see the flag
			 * and return ENXIO.
			 */
			zv->zv_flags |= ZVOL_REMOVING;
			mutex_exit(&zv->zv_state_lock);
			rw_exit(&zv->zv_suspend_lock);

			/* Put it on the list for the next stage. */
			list_insert_head(&remove_list, zv);
		} else
			mutex_exit(&zv->zv_state_lock);
	}

	rw_exit(&zvol_state_lock);

	/* Didn't match any, nothing to do! */
	if (list_is_empty(&remove_list)) {
		if (task)
			task->zt_error = SET_ERROR(ENOENT);
		return;
	}

	/* Actually shut them all down. */
	for (zv = list_head(&remove_list); zv != NULL; zv = zv_next) {
		zv_next = list_next(&remove_list, zv);

		mutex_enter(&zv->zv_state_lock);

		/*
		 * Still open or suspended, just wait. This can happen if, for
		 * example, we managed to acquire zv_state_lock in the moments
		 * where zvol_open() or zvol_release() are trading locks to
		 * call zvol_first_open() or zvol_last_close().
		 */
		while (zv->zv_open_count > 0 ||
		    atomic_read(&zv->zv_suspend_ref))
			cv_wait(&zv->zv_removing_cv, &zv->zv_state_lock);

		/*
		 * No users, shut down the OS side. This may not remove the
		 * minor from view immediately, depending on the kernel
		 * specifics, but it will ensure that it is unusable and that
		 * this zvol_state_t can never again be reached from an OS-side
		 * operation.
		 */
		zvol_os_remove_minor(zv);
		mutex_exit(&zv->zv_state_lock);

		/* Remove it from the name lookup lists */
		rw_enter(&zvol_state_lock, RW_WRITER);
		zvol_remove(zv);
		rw_exit(&zvol_state_lock);
	}

	/*
	 * Our own references on remove_list is the last one, free them and
	 * we're done.
	 */
	while ((zv = list_remove_head(&remove_list)) != NULL)
		zvol_os_free(zv);

	list_destroy(&remove_list);
}

/* Remove minor for this specific volume only */
static int
zvol_remove_minor_impl(const char *name)
{
	if (zvol_inhibit_dev)
		return (0);

	zvol_task_t task;
	memset(&task, 0, sizeof (zvol_task_t));
	strlcpy(task.zt_name1, name, sizeof (task.zt_name1));
	task.zt_value = B_FALSE;

	zvol_remove_minors_impl(&task);

	return (task.zt_error);
}

/*
 * Rename minors for specified dataset including children and snapshots.
 */
static void
zvol_rename_minors_impl(zvol_task_t *task)
{
	zvol_state_t *zv, *zv_next;
	const char *oldname = task->zt_name1;
	const char *newname = task->zt_name2;
	int total = 0, done = 0, last_error, error, oldnamelen;

	if (zvol_inhibit_dev)
		return;

	oldnamelen = strlen(oldname);

	rw_enter(&zvol_state_lock, RW_READER);

	for (zv = list_head(&zvol_state_list); zv != NULL; zv = zv_next) {
		zv_next = list_next(&zvol_state_list, zv);

		mutex_enter(&zv->zv_state_lock);

		if (strcmp(zv->zv_name, oldname) == 0) {
			error = zvol_os_rename_minor(zv, newname);
		} else if (strncmp(zv->zv_name, oldname, oldnamelen) == 0 &&
		    (zv->zv_name[oldnamelen] == '/' ||
		    zv->zv_name[oldnamelen] == '@')) {
			char *name = kmem_asprintf("%s%c%s", newname,
			    zv->zv_name[oldnamelen],
			    zv->zv_name + oldnamelen + 1);
			error = zvol_os_rename_minor(zv, name);
			kmem_strfree(name);
		}
		if (error) {
			last_error = error;
		} else {
			done++;
		}
		total++;
		mutex_exit(&zv->zv_state_lock);
	}

	rw_exit(&zvol_state_lock);
	zvol_task_update_status(task, total, done, last_error);
}

typedef struct zvol_snapdev_cb_arg {
	zvol_task_t *task;
	uint64_t snapdev;
} zvol_snapdev_cb_arg_t;

static int
zvol_set_snapdev_cb(const char *dsname, void *param)
{
	zvol_snapdev_cb_arg_t *arg = param;
	int error = 0;

	if (strchr(dsname, '@') == NULL)
		return (0);

	switch (arg->snapdev) {
		case ZFS_SNAPDEV_VISIBLE:
			error = zvol_os_create_minor(dsname);
			break;
		case ZFS_SNAPDEV_HIDDEN:
			error = zvol_remove_minor_impl(dsname);
			break;
	}

	zvol_task_update_status(arg->task, 1, error == 0, error);
	return (0);
}

static void
zvol_set_snapdev_impl(zvol_task_t *task)
{
	const char *name = task->zt_name1;
	uint64_t snapdev = task->zt_value;

	zvol_snapdev_cb_arg_t arg = {task, snapdev};
	fstrans_cookie_t cookie = spl_fstrans_mark();
	/*
	 * The zvol_set_snapdev_sync() sets snapdev appropriately
	 * in the dataset hierarchy. Here, we only scan snapshots.
	 */
	dmu_objset_find(name, zvol_set_snapdev_cb, &arg, DS_FIND_SNAPSHOTS);
	spl_fstrans_unmark(cookie);
}

static void
zvol_set_volmode_impl(zvol_task_t *task)
{
	const char *name = task->zt_name1;
	uint64_t volmode = task->zt_value;
	fstrans_cookie_t cookie;
	uint64_t old_volmode;
	zvol_state_t *zv;
	int error;

	if (strchr(name, '@') != NULL)
		return;

	/*
	 * It's unfortunate we need to remove minors before we create new ones:
	 * this is necessary because our backing gendisk (zvol_state->zv_disk)
	 * could be different when we set, for instance, volmode from "geom"
	 * to "dev" (or vice versa).
	 */
	zv = zvol_find_by_name(name, RW_NONE);
	if (zv == NULL && volmode == ZFS_VOLMODE_NONE)
		return;
	if (zv != NULL) {
		old_volmode = zv->zv_volmode;
		mutex_exit(&zv->zv_state_lock);
		if (old_volmode == volmode)
			return;
		zvol_wait_close(zv);
	}
	cookie = spl_fstrans_mark();
	switch (volmode) {
		case ZFS_VOLMODE_NONE:
			error = zvol_remove_minor_impl(name);
			break;
		case ZFS_VOLMODE_GEOM:
		case ZFS_VOLMODE_DEV:
			error = zvol_remove_minor_impl(name);
			/*
			 * The remove minor function call above, might be not
			 * needed, if volmode was switched from 'none' value.
			 * Ignore error in this case.
			 */
			if (error == ENOENT)
				error = 0;
			else if (error)
				break;
			error = zvol_os_create_minor(name);
			break;
		case ZFS_VOLMODE_DEFAULT:
			error = zvol_remove_minor_impl(name);
			if (zvol_volmode == ZFS_VOLMODE_NONE)
				break;
			else /* if zvol_volmode is invalid defaults to "geom" */
				error = zvol_os_create_minor(name);
			break;
	}
	zvol_task_update_status(task, 1, error == 0, error);
	spl_fstrans_unmark(cookie);
}

/*
 * The worker thread function performed asynchronously.
 */
static void
zvol_task_cb(void *arg)
{
	zvol_task_t *task = arg;

	switch (task->zt_op) {
	case ZVOL_ASYNC_CREATE_MINORS:
		zvol_create_minors_impl(task);
		break;
	case ZVOL_ASYNC_REMOVE_MINORS:
		zvol_remove_minors_impl(task);
		break;
	case ZVOL_ASYNC_RENAME_MINORS:
		zvol_rename_minors_impl(task);
		break;
	case ZVOL_ASYNC_SET_SNAPDEV:
		zvol_set_snapdev_impl(task);
		break;
	case ZVOL_ASYNC_SET_VOLMODE:
		zvol_set_volmode_impl(task);
		break;
	default:
		VERIFY(0);
		break;
	}

	zvol_task_report_status(task);
	kmem_free(task, sizeof (zvol_task_t));
}

typedef struct zvol_set_prop_int_arg {
	const char *zsda_name;
	uint64_t zsda_value;
	zprop_source_t zsda_source;
	zfs_prop_t zsda_prop;
} zvol_set_prop_int_arg_t;

/*
 * Sanity check the dataset for safe use by the sync task.  No additional
 * conditions are imposed.
 */
static int
zvol_set_common_check(void *arg, dmu_tx_t *tx)
{
	zvol_set_prop_int_arg_t *zsda = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	dsl_dir_t *dd;
	int error;

	error = dsl_dir_hold(dp, zsda->zsda_name, FTAG, &dd, NULL);
	if (error != 0)
		return (error);

	dsl_dir_rele(dd, FTAG);

	return (error);
}

static int
zvol_set_common_sync_cb(dsl_pool_t *dp, dsl_dataset_t *ds, void *arg)
{
	zvol_set_prop_int_arg_t *zsda = arg;
	char dsname[ZFS_MAX_DATASET_NAME_LEN];
	zvol_task_t *task;
	uint64_t prop;

	const char *prop_name = zfs_prop_to_name(zsda->zsda_prop);
	dsl_dataset_name(ds, dsname);

	if (dsl_prop_get_int_ds(ds, prop_name, &prop) != 0)
		return (0);

	task = kmem_zalloc(sizeof (zvol_task_t), KM_SLEEP);
	if (zsda->zsda_prop == ZFS_PROP_VOLMODE) {
		task->zt_op = ZVOL_ASYNC_SET_VOLMODE;
	} else if (zsda->zsda_prop == ZFS_PROP_SNAPDEV) {
		task->zt_op = ZVOL_ASYNC_SET_SNAPDEV;
	} else {
		kmem_free(task, sizeof (zvol_task_t));
		return (0);
	}
	task->zt_value = prop;
	strlcpy(task->zt_name1, dsname, sizeof (task->zt_name1));
	(void) taskq_dispatch(dp->dp_spa->spa_zvol_taskq, zvol_task_cb,
	    task, TQ_SLEEP);
	return (0);
}

/*
 * Traverse all child datasets and apply the property appropriately.
 * We call dsl_prop_set_sync_impl() here to set the value only on the toplevel
 * dataset and read the effective "property" on every child in the callback
 * function: this is because the value is not guaranteed to be the same in the
 * whole dataset hierarchy.
 */
static void
zvol_set_common_sync(void *arg, dmu_tx_t *tx)
{
	zvol_set_prop_int_arg_t *zsda = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	dsl_dir_t *dd;
	dsl_dataset_t *ds;
	int error;

	VERIFY0(dsl_dir_hold(dp, zsda->zsda_name, FTAG, &dd, NULL));

	error = dsl_dataset_hold(dp, zsda->zsda_name, FTAG, &ds);
	if (error == 0) {
		dsl_prop_set_sync_impl(ds, zfs_prop_to_name(zsda->zsda_prop),
		    zsda->zsda_source, sizeof (zsda->zsda_value), 1,
		    &zsda->zsda_value, tx);
		dsl_dataset_rele(ds, FTAG);
	}

	dmu_objset_find_dp(dp, dd->dd_object, zvol_set_common_sync_cb,
	    zsda, DS_FIND_CHILDREN);

	dsl_dir_rele(dd, FTAG);
}

int
zvol_set_common(const char *ddname, zfs_prop_t prop, zprop_source_t source,
    uint64_t val)
{
	zvol_set_prop_int_arg_t zsda;

	zsda.zsda_name = ddname;
	zsda.zsda_source = source;
	zsda.zsda_value = val;
	zsda.zsda_prop = prop;

	return (dsl_sync_task(ddname, zvol_set_common_check,
	    zvol_set_common_sync, &zsda, 0, ZFS_SPACE_CHECK_NONE));
}

void
zvol_create_minors(const char *name)
{
	spa_t *spa;
	zvol_task_t *task;
	taskqid_t id;

	if (spa_open(name, &spa, FTAG) != 0)
		return;

	task = kmem_zalloc(sizeof (zvol_task_t), KM_SLEEP);
	task->zt_op = ZVOL_ASYNC_CREATE_MINORS;
	strlcpy(task->zt_name1, name, sizeof (task->zt_name1));
	id = taskq_dispatch(spa->spa_zvol_taskq, zvol_task_cb, task, TQ_SLEEP);
	if (id != TASKQID_INVALID)
		taskq_wait_id(spa->spa_zvol_taskq, id);

	spa_close(spa, FTAG);
}

void
zvol_remove_minors(spa_t *spa, const char *name, boolean_t async)
{
	zvol_task_t *task;
	taskqid_t id;

	task = kmem_zalloc(sizeof (zvol_task_t), KM_SLEEP);
	task->zt_op = ZVOL_ASYNC_REMOVE_MINORS;
	strlcpy(task->zt_name1, name, sizeof (task->zt_name1));
	task->zt_value = B_TRUE;
	id = taskq_dispatch(spa->spa_zvol_taskq, zvol_task_cb, task, TQ_SLEEP);
	if ((async == B_FALSE) && (id != TASKQID_INVALID))
		taskq_wait_id(spa->spa_zvol_taskq, id);
}

void
zvol_rename_minors(spa_t *spa, const char *name1, const char *name2,
    boolean_t async)
{
	zvol_task_t *task;
	taskqid_t id;

	task = kmem_zalloc(sizeof (zvol_task_t), KM_SLEEP);
	task->zt_op = ZVOL_ASYNC_RENAME_MINORS;
	strlcpy(task->zt_name1, name1, sizeof (task->zt_name1));
	strlcpy(task->zt_name2, name2, sizeof (task->zt_name2));
	id = taskq_dispatch(spa->spa_zvol_taskq, zvol_task_cb, task, TQ_SLEEP);
	if ((async == B_FALSE) && (id != TASKQID_INVALID))
		taskq_wait_id(spa->spa_zvol_taskq, id);
}

boolean_t
zvol_is_zvol(const char *name)
{

	return (zvol_os_is_zvol(name));
}

int
zvol_init_impl(void)
{
	int i;

	/*
	 * zvol_threads is the module param the user passes in.
	 *
	 * zvol_actual_threads is what we use internally, since the user can
	 * pass zvol_thread = 0 to mean "use all the CPUs" (the default).
	 */
	static unsigned int zvol_actual_threads;

	if (zvol_threads == 0) {
		/*
		 * See dde9380a1 for why 32 was chosen here.  This should
		 * probably be refined to be some multiple of the number
		 * of CPUs.
		 */
		zvol_actual_threads = MAX(max_ncpus, 32);
	} else {
		zvol_actual_threads = MIN(MAX(zvol_threads, 1), 1024);
	}

	/*
	 * Use at least 32 zvol_threads but for many core system,
	 * prefer 6 threads per taskq, but no more taskqs
	 * than threads in them on large systems.
	 *
	 *                 taskq   total
	 * cpus    taskqs  threads threads
	 * ------- ------- ------- -------
	 * 1       1       32       32
	 * 2       1       32       32
	 * 4       1       32       32
	 * 8       2       16       32
	 * 16      3       11       33
	 * 32      5       7        35
	 * 64      8       8        64
	 * 128     11      12       132
	 * 256     16      16       256
	 */
	zv_taskq_t *ztqs = &zvol_taskqs;
	int num_tqs = MIN(max_ncpus, zvol_num_taskqs);
	if (num_tqs == 0) {
		num_tqs = 1 + max_ncpus / 6;
		while (num_tqs * num_tqs > zvol_actual_threads)
			num_tqs--;
	}

	int per_tq_thread = zvol_actual_threads / num_tqs;
	if (per_tq_thread * num_tqs < zvol_actual_threads)
		per_tq_thread++;

	ztqs->tqs_cnt = num_tqs;
	ztqs->tqs_taskq = kmem_alloc(num_tqs * sizeof (taskq_t *), KM_SLEEP);

	for (uint_t i = 0; i < num_tqs; i++) {
		char name[32];
		(void) snprintf(name, sizeof (name), "%s_tq-%u",
		    ZVOL_DRIVER, i);
		ztqs->tqs_taskq[i] = taskq_create(name, per_tq_thread,
		    maxclsyspri, per_tq_thread, INT_MAX,
		    TASKQ_PREPOPULATE | TASKQ_DYNAMIC);
		if (ztqs->tqs_taskq[i] == NULL) {
			for (int j = i - 1; j >= 0; j--)
				taskq_destroy(ztqs->tqs_taskq[j]);
			kmem_free(ztqs->tqs_taskq, ztqs->tqs_cnt *
			    sizeof (taskq_t *));
			ztqs->tqs_taskq = NULL;
			return (SET_ERROR(ENOMEM));
		}
	}

	list_create(&zvol_state_list, sizeof (zvol_state_t),
	    offsetof(zvol_state_t, zv_next));
	rw_init(&zvol_state_lock, NULL, RW_DEFAULT, NULL);

	zvol_htable = kmem_alloc(ZVOL_HT_SIZE * sizeof (struct hlist_head),
	    KM_SLEEP);
	for (i = 0; i < ZVOL_HT_SIZE; i++)
		INIT_HLIST_HEAD(&zvol_htable[i]);

	return (0);
}

void
zvol_fini_impl(void)
{
	zv_taskq_t *ztqs = &zvol_taskqs;

	zvol_remove_minors_impl(NULL);

	kmem_free(zvol_htable, ZVOL_HT_SIZE * sizeof (struct hlist_head));
	list_destroy(&zvol_state_list);
	rw_destroy(&zvol_state_lock);

	if (ztqs->tqs_taskq == NULL) {
		ASSERT0(ztqs->tqs_cnt);
	} else {
		for (uint_t i = 0; i < ztqs->tqs_cnt; i++) {
			ASSERT3P(ztqs->tqs_taskq[i], !=, NULL);
			taskq_destroy(ztqs->tqs_taskq[i]);
		}
		kmem_free(ztqs->tqs_taskq, ztqs->tqs_cnt *
		    sizeof (taskq_t *));
		ztqs->tqs_taskq = NULL;
	}
}

ZFS_MODULE_PARAM(zfs_vol, zvol_, inhibit_dev, UINT, ZMOD_RW,
	"Do not create zvol device nodes");
ZFS_MODULE_PARAM(zfs_vol, zvol_, prefetch_bytes, UINT, ZMOD_RW,
	"Prefetch N bytes at zvol start+end");
ZFS_MODULE_PARAM(zfs_vol, zvol_vol, mode, UINT, ZMOD_RW,
	"Default volmode property value");
ZFS_MODULE_PARAM(zfs_vol, zvol_, threads, UINT, ZMOD_RW,
	"Number of threads for I/O requests. Set to 0 to use all active CPUs");
ZFS_MODULE_PARAM(zfs_vol, zvol_, num_taskqs, UINT, ZMOD_RW,
	"Number of zvol taskqs");
ZFS_MODULE_PARAM(zfs_vol, zvol_, request_sync, UINT, ZMOD_RW,
	"Synchronously handle bio requests");
