/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
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
 */

#include <sys/dmu_traverse.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_prop.h>
#include <sys/zap.h>
#include <sys/zil_impl.h>
#include <sys/zio.h>
#include <sys/zfs_rlock.h>
#include <sys/zfs_znode.h>
#include <sys/zvol.h>
#include <linux/blkdev_compat.h>

unsigned int zvol_inhibit_dev = 0;
unsigned int zvol_major = ZVOL_MAJOR;
unsigned int zvol_threads = 32;
unsigned long zvol_max_discard_blocks = 16384;

static taskq_t *zvol_taskq;
static kmutex_t zvol_state_lock;
static list_t zvol_state_list;
static char *zvol_tag = "zvol_tag";

/*
 * The in-core state of each volume.
 */
typedef struct zvol_state {
	char			zv_name[MAXNAMELEN];	/* name */
	uint64_t		zv_volsize;	/* advertised space */
	uint64_t		zv_volblocksize;/* volume block size */
	objset_t		*zv_objset;	/* objset handle */
	uint32_t		zv_flags;	/* ZVOL_* flags */
	uint32_t		zv_open_count;	/* open counts */
	uint32_t		zv_changed;	/* disk changed */
	zilog_t			*zv_zilog;	/* ZIL handle */
	znode_t			zv_znode;	/* for range locking */
	dmu_buf_t		*zv_dbuf;	/* bonus handle */
	dev_t			zv_dev;		/* device id */
	struct gendisk		*zv_disk;	/* generic disk */
	struct request_queue	*zv_queue;	/* request queue */
	spinlock_t		zv_lock;	/* request queue lock */
	list_node_t		zv_next;	/* next zvol_state_t linkage */
} zvol_state_t;

#define	ZVOL_RDONLY	0x1

/*
 * Find the next available range of ZVOL_MINORS minor numbers.  The
 * zvol_state_list is kept in ascending minor order so we simply need
 * to scan the list for the first gap in the sequence.  This allows us
 * to recycle minor number as devices are created and removed.
 */
static int
zvol_find_minor(unsigned *minor)
{
	zvol_state_t *zv;

	*minor = 0;
	ASSERT(MUTEX_HELD(&zvol_state_lock));
	for (zv = list_head(&zvol_state_list); zv != NULL;
	     zv = list_next(&zvol_state_list, zv), *minor += ZVOL_MINORS) {
		if (MINOR(zv->zv_dev) != MINOR(*minor))
			break;
	}

	/* All minors are in use */
	if (*minor >= (1 << MINORBITS))
		return ENXIO;

	return 0;
}

/*
 * Find a zvol_state_t given the full major+minor dev_t.
 */
static zvol_state_t *
zvol_find_by_dev(dev_t dev)
{
	zvol_state_t *zv;

	ASSERT(MUTEX_HELD(&zvol_state_lock));
	for (zv = list_head(&zvol_state_list); zv != NULL;
	     zv = list_next(&zvol_state_list, zv)) {
		if (zv->zv_dev == dev)
			return zv;
	}

	return NULL;
}

/*
 * Find a zvol_state_t given the name provided at zvol_alloc() time.
 */
static zvol_state_t *
zvol_find_by_name(const char *name)
{
	zvol_state_t *zv;

	ASSERT(MUTEX_HELD(&zvol_state_lock));
	for (zv = list_head(&zvol_state_list); zv != NULL;
	     zv = list_next(&zvol_state_list, zv)) {
		if (!strncmp(zv->zv_name, name, MAXNAMELEN))
			return zv;
	}

	return NULL;
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

	VERIFY(nvlist_lookup_uint64(nvprops,
	    zfs_prop_to_name(ZFS_PROP_VOLSIZE), &volsize) == 0);
	if (nvlist_lookup_uint64(nvprops,
	    zfs_prop_to_name(ZFS_PROP_VOLBLOCKSIZE), &volblocksize) != 0)
		volblocksize = zfs_prop_default_numeric(ZFS_PROP_VOLBLOCKSIZE);

	/*
	 * These properties must be removed from the list so the generic
	 * property setting step won't apply to them.
	 */
	VERIFY(nvlist_remove_all(nvprops,
	    zfs_prop_to_name(ZFS_PROP_VOLSIZE)) == 0);
	(void) nvlist_remove_all(nvprops,
	    zfs_prop_to_name(ZFS_PROP_VOLBLOCKSIZE));

	error = dmu_object_claim(os, ZVOL_OBJ, DMU_OT_ZVOL, volblocksize,
	    DMU_OT_NONE, 0, tx);
	ASSERT(error == 0);

	error = zap_create_claim(os, ZVOL_ZAP_OBJ, DMU_OT_ZVOL_PROP,
	    DMU_OT_NONE, 0, tx);
	ASSERT(error == 0);

	error = zap_update(os, ZVOL_ZAP_OBJ, "size", 8, 1, &volsize, tx);
	ASSERT(error == 0);
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
	doi = kmem_alloc(sizeof(dmu_object_info_t), KM_SLEEP);
	error = dmu_object_info(os, ZVOL_OBJ, doi);

	if (error == 0) {
		dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_VOLBLOCKSIZE,
		    doi->doi_data_block_size);
	}

	kmem_free(doi, sizeof(dmu_object_info_t));

	return (error);
}

/*
 * Sanity check volume size.
 */
int
zvol_check_volsize(uint64_t volsize, uint64_t blocksize)
{
	if (volsize == 0)
		return (EINVAL);

	if (volsize % blocksize != 0)
		return (EINVAL);

#ifdef _ILP32
	if (volsize - 1 > MAXOFFSET_T)
		return (EOVERFLOW);
#endif
	return (0);
}

/*
 * Ensure the zap is flushed then inform the VFS of the capacity change.
 */
static int
zvol_update_volsize(zvol_state_t *zv, uint64_t volsize, objset_t *os)
{
	struct block_device *bdev;
	dmu_tx_t *tx;
	int error;

	ASSERT(MUTEX_HELD(&zvol_state_lock));

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, ZVOL_ZAP_OBJ, TRUE, NULL);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		return (error);
	}

	error = zap_update(os, ZVOL_ZAP_OBJ, "size", 8, 1,
	    &volsize, tx);
	dmu_tx_commit(tx);

	if (error)
		return (error);

	error = dmu_free_long_range(os,
	    ZVOL_OBJ, volsize, DMU_OBJECT_END);
	if (error)
		return (error);

	bdev = bdget_disk(zv->zv_disk, 0);
	if (!bdev)
		return (EIO);
/*
 * 2.6.28 API change
 * Added check_disk_size_change() helper function.
 */
#ifdef HAVE_CHECK_DISK_SIZE_CHANGE
	set_capacity(zv->zv_disk, volsize >> 9);
	zv->zv_volsize = volsize;
	check_disk_size_change(zv->zv_disk, bdev);
#else
	zv->zv_volsize = volsize;
	zv->zv_changed = 1;
	(void) check_disk_change(bdev);
#endif /* HAVE_CHECK_DISK_SIZE_CHANGE */

	bdput(bdev);

	return (0);
}

/*
 * Set ZFS_PROP_VOLSIZE set entry point.
 */
int
zvol_set_volsize(const char *name, uint64_t volsize)
{
	zvol_state_t *zv;
	dmu_object_info_t *doi;
	objset_t *os = NULL;
	uint64_t readonly;
	int error;

	mutex_enter(&zvol_state_lock);

	zv = zvol_find_by_name(name);
	if (zv == NULL) {
		error = ENXIO;
		goto out;
	}

	doi = kmem_alloc(sizeof(dmu_object_info_t), KM_SLEEP);

	error = dmu_objset_hold(name, FTAG, &os);
	if (error)
		goto out_doi;

	if ((error = dmu_object_info(os, ZVOL_OBJ, doi)) != 0 ||
	    (error = zvol_check_volsize(volsize,doi->doi_data_block_size)) != 0)
		goto out_doi;

	VERIFY(dsl_prop_get_integer(name, "readonly", &readonly, NULL) == 0);
	if (readonly) {
		error = EROFS;
		goto out_doi;
	}

	if (get_disk_ro(zv->zv_disk) || (zv->zv_flags & ZVOL_RDONLY)) {
		error = EROFS;
		goto out_doi;
	}

	error = zvol_update_volsize(zv, volsize, os);
out_doi:
	kmem_free(doi, sizeof(dmu_object_info_t));
out:
	if (os)
		dmu_objset_rele(os, FTAG);

	mutex_exit(&zvol_state_lock);

	return (error);
}

/*
 * Sanity check volume block size.
 */
int
zvol_check_volblocksize(uint64_t volblocksize)
{
	if (volblocksize < SPA_MINBLOCKSIZE ||
	    volblocksize > SPA_MAXBLOCKSIZE ||
	    !ISP2(volblocksize))
		return (EDOM);

	return (0);
}

/*
 * Set ZFS_PROP_VOLBLOCKSIZE set entry point.
 */
int
zvol_set_volblocksize(const char *name, uint64_t volblocksize)
{
	zvol_state_t *zv;
	dmu_tx_t *tx;
	int error;

	mutex_enter(&zvol_state_lock);

	zv = zvol_find_by_name(name);
	if (zv == NULL) {
		error = ENXIO;
		goto out;
	}

	if (get_disk_ro(zv->zv_disk) || (zv->zv_flags & ZVOL_RDONLY)) {
		error = EROFS;
		goto out;
	}

	tx = dmu_tx_create(zv->zv_objset);
	dmu_tx_hold_bonus(tx, ZVOL_OBJ);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
	} else {
		error = dmu_object_set_blocksize(zv->zv_objset, ZVOL_OBJ,
		    volblocksize, 0, tx);
		if (error == ENOTSUP)
			error = EBUSY;
		dmu_tx_commit(tx);
		if (error == 0)
			zv->zv_volblocksize = volblocksize;
	}
out:
	mutex_exit(&zvol_state_lock);

	return (error);
}

/*
 * Replay a TX_WRITE ZIL transaction that didn't get committed
 * after a system failure
 */
static int
zvol_replay_write(zvol_state_t *zv, lr_write_t *lr, boolean_t byteswap)
{
	objset_t *os = zv->zv_objset;
	char *data = (char *)(lr + 1);	/* data follows lr_write_t */
	uint64_t off = lr->lr_offset;
	uint64_t len = lr->lr_length;
	dmu_tx_t *tx;
	int error;

	if (byteswap)
		byteswap_uint64_array(lr, sizeof (*lr));

	tx = dmu_tx_create(os);
	dmu_tx_hold_write(tx, ZVOL_OBJ, off, len);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
	} else {
		dmu_write(os, ZVOL_OBJ, off, len, data, tx);
		dmu_tx_commit(tx);
	}

	return (error);
}

static int
zvol_replay_err(zvol_state_t *zv, lr_t *lr, boolean_t byteswap)
{
	return (ENOTSUP);
}

/*
 * Callback vectors for replaying records.
 * Only TX_WRITE is needed for zvol.
 */
zil_replay_func_t *zvol_replay_vector[TX_MAX_TYPE] = {
	(zil_replay_func_t *)zvol_replay_err,	/* no such transaction type */
	(zil_replay_func_t *)zvol_replay_err,	/* TX_CREATE */
	(zil_replay_func_t *)zvol_replay_err,	/* TX_MKDIR */
	(zil_replay_func_t *)zvol_replay_err,	/* TX_MKXATTR */
	(zil_replay_func_t *)zvol_replay_err,	/* TX_SYMLINK */
	(zil_replay_func_t *)zvol_replay_err,	/* TX_REMOVE */
	(zil_replay_func_t *)zvol_replay_err,	/* TX_RMDIR */
	(zil_replay_func_t *)zvol_replay_err,	/* TX_LINK */
	(zil_replay_func_t *)zvol_replay_err,	/* TX_RENAME */
	(zil_replay_func_t *)zvol_replay_write,	/* TX_WRITE */
	(zil_replay_func_t *)zvol_replay_err,	/* TX_TRUNCATE */
	(zil_replay_func_t *)zvol_replay_err,	/* TX_SETATTR */
	(zil_replay_func_t *)zvol_replay_err,	/* TX_ACL */
};

/*
 * zvol_log_write() handles synchronous writes using TX_WRITE ZIL transactions.
 *
 * We store data in the log buffers if it's small enough.
 * Otherwise we will later flush the data out via dmu_sync().
 */
ssize_t zvol_immediate_write_sz = 32768;

static void
zvol_log_write(zvol_state_t *zv, dmu_tx_t *tx,
	       uint64_t offset, uint64_t size, int sync)
{
	uint32_t blocksize = zv->zv_volblocksize;
	zilog_t *zilog = zv->zv_zilog;
	boolean_t slogging;
	ssize_t immediate_write_sz;

	if (zil_replaying(zilog, tx))
		return;

	immediate_write_sz = (zilog->zl_logbias == ZFS_LOGBIAS_THROUGHPUT)
		? 0 : zvol_immediate_write_sz;
	slogging = spa_has_slogs(zilog->zl_spa) &&
		(zilog->zl_logbias == ZFS_LOGBIAS_LATENCY);

	while (size) {
		itx_t *itx;
		lr_write_t *lr;
		ssize_t len;
		itx_wr_state_t write_state;

		/*
		 * Unlike zfs_log_write() we can be called with
		 * up to DMU_MAX_ACCESS/2 (5MB) writes.
		 */
		if (blocksize > immediate_write_sz && !slogging &&
		    size >= blocksize && offset % blocksize == 0) {
			write_state = WR_INDIRECT; /* uses dmu_sync */
			len = blocksize;
		} else if (sync) {
			write_state = WR_COPIED;
			len = MIN(ZIL_MAX_LOG_DATA, size);
		} else {
			write_state = WR_NEED_COPY;
			len = MIN(ZIL_MAX_LOG_DATA, size);
		}

		itx = zil_itx_create(TX_WRITE, sizeof (*lr) +
		    (write_state == WR_COPIED ? len : 0));
		lr = (lr_write_t *)&itx->itx_lr;
		if (write_state == WR_COPIED && dmu_read(zv->zv_objset,
		    ZVOL_OBJ, offset, len, lr+1, DMU_READ_NO_PREFETCH) != 0) {
			zil_itx_destroy(itx);
			itx = zil_itx_create(TX_WRITE, sizeof (*lr));
			lr = (lr_write_t *)&itx->itx_lr;
			write_state = WR_NEED_COPY;
		}

		itx->itx_wr_state = write_state;
		if (write_state == WR_NEED_COPY)
			itx->itx_sod += len;
		lr->lr_foid = ZVOL_OBJ;
		lr->lr_offset = offset;
		lr->lr_length = len;
		lr->lr_blkoff = 0;
		BP_ZERO(&lr->lr_blkptr);

		itx->itx_private = zv;
		itx->itx_sync = sync;

		(void) zil_itx_assign(zilog, itx, tx);

		offset += len;
		size -= len;
	}
}

/*
 * Common write path running under the zvol taskq context.  This function
 * is responsible for copying the request structure data in to the DMU and
 * signaling the request queue with the result of the copy.
 */
static void
zvol_write(void *arg)
{
	struct request *req = (struct request *)arg;
	struct request_queue *q = req->q;
	zvol_state_t *zv = q->queuedata;
	uint64_t offset = blk_rq_pos(req) << 9;
	uint64_t size = blk_rq_bytes(req);
	int error = 0;
	dmu_tx_t *tx;
	rl_t *rl;

	/*
	 * Annotate this call path with a flag that indicates that it is
	 * unsafe to use KM_SLEEP during memory allocations due to the
	 * potential for a deadlock.  KM_PUSHPAGE should be used instead.
	 */
	ASSERT(!(current->flags & PF_NOFS));
	current->flags |= PF_NOFS;

	if (req->cmd_flags & VDEV_REQ_FLUSH)
		zil_commit(zv->zv_zilog, ZVOL_OBJ);

	/*
	 * Some requests are just for flush and nothing else.
	 */
	if (size == 0) {
		blk_end_request(req, 0, size);
		goto out;
	}

	rl = zfs_range_lock(&zv->zv_znode, offset, size, RL_WRITER);

	tx = dmu_tx_create(zv->zv_objset);
	dmu_tx_hold_write(tx, ZVOL_OBJ, offset, size);

	/* This will only fail for ENOSPC */
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		zfs_range_unlock(rl);
		blk_end_request(req, -error, size);
		goto out;
	}

	error = dmu_write_req(zv->zv_objset, ZVOL_OBJ, req, tx);
	if (error == 0)
		zvol_log_write(zv, tx, offset, size,
		    req->cmd_flags & VDEV_REQ_FUA);

	dmu_tx_commit(tx);
	zfs_range_unlock(rl);

	if ((req->cmd_flags & VDEV_REQ_FUA) ||
	    zv->zv_objset->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zv->zv_zilog, ZVOL_OBJ);

	blk_end_request(req, -error, size);
out:
	current->flags &= ~PF_NOFS;
}

#ifdef HAVE_BLK_QUEUE_DISCARD
static void
zvol_discard(void *arg)
{
	struct request *req = (struct request *)arg;
	struct request_queue *q = req->q;
	zvol_state_t *zv = q->queuedata;
	uint64_t offset = blk_rq_pos(req) << 9;
	uint64_t size = blk_rq_bytes(req);
	int error;
	rl_t *rl;

	/*
	 * Annotate this call path with a flag that indicates that it is
	 * unsafe to use KM_SLEEP during memory allocations due to the
	 * potential for a deadlock.  KM_PUSHPAGE should be used instead.
	 */
	ASSERT(!(current->flags & PF_NOFS));
	current->flags |= PF_NOFS;

	if (offset + size > zv->zv_volsize) {
		blk_end_request(req, -EIO, size);
		goto out;
	}

	if (size == 0) {
		blk_end_request(req, 0, size);
		goto out;
	}

	rl = zfs_range_lock(&zv->zv_znode, offset, size, RL_WRITER);

	error = dmu_free_long_range(zv->zv_objset, ZVOL_OBJ, offset, size);

	/*
	 * TODO: maybe we should add the operation to the log.
	 */

	zfs_range_unlock(rl);

	blk_end_request(req, -error, size);
out:
	current->flags &= ~PF_NOFS;
}
#endif /* HAVE_BLK_QUEUE_DISCARD */

/*
 * Common read path running under the zvol taskq context.  This function
 * is responsible for copying the requested data out of the DMU and in to
 * a linux request structure.  It then must signal the request queue with
 * an error code describing the result of the copy.
 */
static void
zvol_read(void *arg)
{
	struct request *req = (struct request *)arg;
	struct request_queue *q = req->q;
	zvol_state_t *zv = q->queuedata;
	uint64_t offset = blk_rq_pos(req) << 9;
	uint64_t size = blk_rq_bytes(req);
	int error;
	rl_t *rl;

	if (size == 0) {
		blk_end_request(req, 0, size);
		return;
	}

	rl = zfs_range_lock(&zv->zv_znode, offset, size, RL_READER);

	error = dmu_read_req(zv->zv_objset, ZVOL_OBJ, req);

	zfs_range_unlock(rl);

	/* convert checksum errors into IO errors */
	if (error == ECKSUM)
		error = EIO;

	blk_end_request(req, -error, size);
}

/*
 * Request will be added back to the request queue and retried if
 * it cannot be immediately dispatched to the taskq for handling
 */
static inline void
zvol_dispatch(task_func_t func, struct request *req)
{
	if (!taskq_dispatch(zvol_taskq, func, (void *)req, TQ_NOSLEEP))
		blk_requeue_request(req->q, req);
}

/*
 * Common request path.  Rather than registering a custom make_request()
 * function we use the generic Linux version.  This is done because it allows
 * us to easily merge read requests which would otherwise we performed
 * synchronously by the DMU.  This is less critical in write case where the
 * DMU will perform the correct merging within a transaction group.  Using
 * the generic make_request() also let's use leverage the fact that the
 * elevator with ensure correct ordering in regards to barrior IOs.  On
 * the downside it means that in the write case we end up doing request
 * merging twice once in the elevator and once in the DMU.
 *
 * The request handler is called under a spin lock so all the real work
 * is handed off to be done in the context of the zvol taskq.  This function
 * simply performs basic request sanity checking and hands off the request.
 */
static void
zvol_request(struct request_queue *q)
{
	zvol_state_t *zv = q->queuedata;
	struct request *req;
	unsigned int size;

	while ((req = blk_fetch_request(q)) != NULL) {
		size = blk_rq_bytes(req);

		if (size != 0 && blk_rq_pos(req) + blk_rq_sectors(req) >
		    get_capacity(zv->zv_disk)) {
			printk(KERN_INFO
			       "%s: bad access: block=%llu, count=%lu\n",
			       req->rq_disk->disk_name,
			       (long long unsigned)blk_rq_pos(req),
			       (long unsigned)blk_rq_sectors(req));
			__blk_end_request(req, -EIO, size);
			continue;
		}

		if (!blk_fs_request(req)) {
			printk(KERN_INFO "%s: non-fs cmd\n",
			       req->rq_disk->disk_name);
			__blk_end_request(req, -EIO, size);
			continue;
		}

		switch (rq_data_dir(req)) {
		case READ:
			zvol_dispatch(zvol_read, req);
			break;
		case WRITE:
			if (unlikely(get_disk_ro(zv->zv_disk)) ||
			    unlikely(zv->zv_flags & ZVOL_RDONLY)) {
				__blk_end_request(req, -EROFS, size);
				break;
			}

#ifdef HAVE_BLK_QUEUE_DISCARD
			if (req->cmd_flags & VDEV_REQ_DISCARD) {
				zvol_dispatch(zvol_discard, req);
				break;
			}
#endif /* HAVE_BLK_QUEUE_DISCARD */

			zvol_dispatch(zvol_write, req);
			break;
		default:
			printk(KERN_INFO "%s: unknown cmd: %d\n",
			       req->rq_disk->disk_name, (int)rq_data_dir(req));
			__blk_end_request(req, -EIO, size);
			break;
		}
	}
}

static void
zvol_get_done(zgd_t *zgd, int error)
{
	if (zgd->zgd_db)
		dmu_buf_rele(zgd->zgd_db, zgd);

	zfs_range_unlock(zgd->zgd_rl);

	if (error == 0 && zgd->zgd_bp)
		zil_add_block(zgd->zgd_zilog, zgd->zgd_bp);

	kmem_free(zgd, sizeof (zgd_t));
}

/*
 * Get data to generate a TX_WRITE intent log record.
 */
static int
zvol_get_data(void *arg, lr_write_t *lr, char *buf, zio_t *zio)
{
	zvol_state_t *zv = arg;
	objset_t *os = zv->zv_objset;
	uint64_t offset = lr->lr_offset;
	uint64_t size = lr->lr_length;
	dmu_buf_t *db;
	zgd_t *zgd;
	int error;

	ASSERT(zio != NULL);
	ASSERT(size != 0);

	zgd = (zgd_t *)kmem_zalloc(sizeof (zgd_t), KM_PUSHPAGE);
	zgd->zgd_zilog = zv->zv_zilog;
	zgd->zgd_rl = zfs_range_lock(&zv->zv_znode, offset, size, RL_READER);

	/*
	 * Write records come in two flavors: immediate and indirect.
	 * For small writes it's cheaper to store the data with the
	 * log record (immediate); for large writes it's cheaper to
	 * sync the data and get a pointer to it (indirect) so that
	 * we don't have to write the data twice.
	 */
	if (buf != NULL) { /* immediate write */
		error = dmu_read(os, ZVOL_OBJ, offset, size, buf,
		    DMU_READ_NO_PREFETCH);
	} else {
		size = zv->zv_volblocksize;
		offset = P2ALIGN_TYPED(offset, size, uint64_t);
		error = dmu_buf_hold(os, ZVOL_OBJ, offset, zgd, &db,
		    DMU_READ_NO_PREFETCH);
		if (error == 0) {
			zgd->zgd_db = db;
			zgd->zgd_bp = &lr->lr_blkptr;

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
 * The zvol_state_t's are inserted in increasing MINOR(dev_t) order.
 */
static void
zvol_insert(zvol_state_t *zv_insert)
{
	zvol_state_t *zv = NULL;

	ASSERT(MUTEX_HELD(&zvol_state_lock));
	ASSERT3U(MINOR(zv_insert->zv_dev) & ZVOL_MINOR_MASK, ==, 0);
	for (zv = list_head(&zvol_state_list); zv != NULL;
	     zv = list_next(&zvol_state_list, zv)) {
		if (MINOR(zv->zv_dev) > MINOR(zv_insert->zv_dev))
			break;
	}

	list_insert_before(&zvol_state_list, zv, zv_insert);
}

/*
 * Simply remove the zvol from to list of zvols.
 */
static void
zvol_remove(zvol_state_t *zv_remove)
{
	ASSERT(MUTEX_HELD(&zvol_state_lock));
	list_remove(&zvol_state_list, zv_remove);
}

static int
zvol_first_open(zvol_state_t *zv)
{
	objset_t *os;
	uint64_t volsize;
	int error;
	uint64_t ro;

	/* lie and say we're read-only */
	error = dmu_objset_own(zv->zv_name, DMU_OST_ZVOL, 1, zvol_tag, &os);
	if (error)
		return (-error);

	error = zap_lookup(os, ZVOL_ZAP_OBJ, "size", 8, 1, &volsize);
	if (error) {
	        dmu_objset_disown(os, zvol_tag);
	        return (-error);
	}

	zv->zv_objset = os;
	error = dmu_bonus_hold(os, ZVOL_OBJ, zvol_tag, &zv->zv_dbuf);
	if (error) {
	        dmu_objset_disown(os, zvol_tag);
	        return (-error);
	}

	set_capacity(zv->zv_disk, volsize >> 9);
	zv->zv_volsize = volsize;
	zv->zv_zilog = zil_open(os, zvol_get_data);

	VERIFY(dsl_prop_get_integer(zv->zv_name, "readonly", &ro, NULL) == 0);
	if (ro || dmu_objset_is_snapshot(os)) {
                set_disk_ro(zv->zv_disk, 1);
	        zv->zv_flags |= ZVOL_RDONLY;
	} else {
                set_disk_ro(zv->zv_disk, 0);
	        zv->zv_flags &= ~ZVOL_RDONLY;
	}

	return (-error);
}

static void
zvol_last_close(zvol_state_t *zv)
{
	zil_close(zv->zv_zilog);
	zv->zv_zilog = NULL;
	dmu_buf_rele(zv->zv_dbuf, zvol_tag);
	zv->zv_dbuf = NULL;
	dmu_objset_disown(zv->zv_objset, zvol_tag);
	zv->zv_objset = NULL;
}

static int
zvol_open(struct block_device *bdev, fmode_t flag)
{
	zvol_state_t *zv = bdev->bd_disk->private_data;
	int error = 0, drop_mutex = 0;

	/*
	 * If the caller is already holding the mutex do not take it
	 * again, this will happen as part of zvol_create_minor().
	 * Once add_disk() is called the device is live and the kernel
	 * will attempt to open it to read the partition information.
	 */
	if (!mutex_owned(&zvol_state_lock)) {
		mutex_enter(&zvol_state_lock);
		drop_mutex = 1;
	}

	ASSERT3P(zv, !=, NULL);

	if (zv->zv_open_count == 0) {
		error = zvol_first_open(zv);
		if (error)
			goto out_mutex;
	}

	if ((flag & FMODE_WRITE) &&
	    (get_disk_ro(zv->zv_disk) || (zv->zv_flags & ZVOL_RDONLY))) {
		error = -EROFS;
		goto out_open_count;
	}

	zv->zv_open_count++;

out_open_count:
	if (zv->zv_open_count == 0)
		zvol_last_close(zv);

out_mutex:
	if (drop_mutex)
		mutex_exit(&zvol_state_lock);

	check_disk_change(bdev);

	return (error);
}

static int
zvol_release(struct gendisk *disk, fmode_t mode)
{
	zvol_state_t *zv = disk->private_data;
	int drop_mutex = 0;

	if (!mutex_owned(&zvol_state_lock)) {
		mutex_enter(&zvol_state_lock);
		drop_mutex = 1;
	}

	ASSERT3P(zv, !=, NULL);
	ASSERT3U(zv->zv_open_count, >, 0);
	zv->zv_open_count--;
	if (zv->zv_open_count == 0)
		zvol_last_close(zv);

	if (drop_mutex)
		mutex_exit(&zvol_state_lock);

	return (0);
}

static int
zvol_ioctl(struct block_device *bdev, fmode_t mode,
           unsigned int cmd, unsigned long arg)
{
	zvol_state_t *zv = bdev->bd_disk->private_data;
	int error = 0;

	if (zv == NULL)
		return (-ENXIO);

	switch (cmd) {
	case BLKFLSBUF:
		zil_commit(zv->zv_zilog, ZVOL_OBJ);
		break;
	case BLKZNAME:
		error = copy_to_user((void *)arg, zv->zv_name, MAXNAMELEN);
		break;

	default:
		error = -ENOTTY;
		break;

	}

	return (error);
}

#ifdef CONFIG_COMPAT
static int
zvol_compat_ioctl(struct block_device *bdev, fmode_t mode,
                  unsigned cmd, unsigned long arg)
{
	return zvol_ioctl(bdev, mode, cmd, arg);
}
#else
#define zvol_compat_ioctl   NULL
#endif

static int zvol_media_changed(struct gendisk *disk)
{
	zvol_state_t *zv = disk->private_data;

	return zv->zv_changed;
}

static int zvol_revalidate_disk(struct gendisk *disk)
{
	zvol_state_t *zv = disk->private_data;

	zv->zv_changed = 0;
	set_capacity(zv->zv_disk, zv->zv_volsize >> 9);

	return 0;
}

/*
 * Provide a simple virtual geometry for legacy compatibility.  For devices
 * smaller than 1 MiB a small head and sector count is used to allow very
 * tiny devices.  For devices over 1 Mib a standard head and sector count
 * is used to keep the cylinders count reasonable.
 */
static int
zvol_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	zvol_state_t *zv = bdev->bd_disk->private_data;
	sector_t sectors = get_capacity(zv->zv_disk);

	if (sectors > 2048) {
		geo->heads = 16;
		geo->sectors = 63;
	} else {
		geo->heads = 2;
		geo->sectors = 4;
	}

	geo->start = 0;
	geo->cylinders = sectors / (geo->heads * geo->sectors);

	return 0;
}

static struct kobject *
zvol_probe(dev_t dev, int *part, void *arg)
{
	zvol_state_t *zv;
	struct kobject *kobj;

	mutex_enter(&zvol_state_lock);
	zv = zvol_find_by_dev(dev);
	kobj = zv ? get_disk(zv->zv_disk) : ERR_PTR(-ENOENT);
	mutex_exit(&zvol_state_lock);

	return kobj;
}

#ifdef HAVE_BDEV_BLOCK_DEVICE_OPERATIONS
static struct block_device_operations zvol_ops = {
	.open            = zvol_open,
	.release         = zvol_release,
	.ioctl           = zvol_ioctl,
	.compat_ioctl    = zvol_compat_ioctl,
	.media_changed   = zvol_media_changed,
	.revalidate_disk = zvol_revalidate_disk,
	.getgeo          = zvol_getgeo,
        .owner           = THIS_MODULE,
};

#else /* HAVE_BDEV_BLOCK_DEVICE_OPERATIONS */

static int
zvol_open_by_inode(struct inode *inode, struct file *file)
{
	return zvol_open(inode->i_bdev, file->f_mode);
}

static int
zvol_release_by_inode(struct inode *inode, struct file *file)
{
	return zvol_release(inode->i_bdev->bd_disk, file->f_mode);
}

static int
zvol_ioctl_by_inode(struct inode *inode, struct file *file,
                    unsigned int cmd, unsigned long arg)
{
	if (file == NULL || inode == NULL)
		return -EINVAL;
	return zvol_ioctl(inode->i_bdev, file->f_mode, cmd, arg);
}

# ifdef CONFIG_COMPAT
static long
zvol_compat_ioctl_by_inode(struct file *file,
                           unsigned int cmd, unsigned long arg)
{
	if (file == NULL)
		return -EINVAL;
	return zvol_compat_ioctl(file->f_dentry->d_inode->i_bdev,
	                         file->f_mode, cmd, arg);
}
# else
# define zvol_compat_ioctl_by_inode   NULL
# endif

static struct block_device_operations zvol_ops = {
	.open            = zvol_open_by_inode,
	.release         = zvol_release_by_inode,
	.ioctl           = zvol_ioctl_by_inode,
	.compat_ioctl    = zvol_compat_ioctl_by_inode,
	.media_changed   = zvol_media_changed,
	.revalidate_disk = zvol_revalidate_disk,
	.getgeo          = zvol_getgeo,
        .owner           = THIS_MODULE,
};
#endif /* HAVE_BDEV_BLOCK_DEVICE_OPERATIONS */

/*
 * Allocate memory for a new zvol_state_t and setup the required
 * request queue and generic disk structures for the block device.
 */
static zvol_state_t *
zvol_alloc(dev_t dev, const char *name)
{
	zvol_state_t *zv;

	zv = kmem_zalloc(sizeof (zvol_state_t), KM_SLEEP);
	if (zv == NULL)
		goto out;

	zv->zv_queue = blk_init_queue(zvol_request, &zv->zv_lock);
	if (zv->zv_queue == NULL)
		goto out_kmem;

#ifdef HAVE_BLK_QUEUE_FLUSH
	blk_queue_flush(zv->zv_queue, VDEV_REQ_FLUSH | VDEV_REQ_FUA);
#else
	blk_queue_ordered(zv->zv_queue, QUEUE_ORDERED_DRAIN, NULL);
#endif /* HAVE_BLK_QUEUE_FLUSH */

	zv->zv_disk = alloc_disk(ZVOL_MINORS);
	if (zv->zv_disk == NULL)
		goto out_queue;

	zv->zv_queue->queuedata = zv;
	zv->zv_dev = dev;
	zv->zv_open_count = 0;
	strlcpy(zv->zv_name, name, MAXNAMELEN);

	mutex_init(&zv->zv_znode.z_range_lock, NULL, MUTEX_DEFAULT, NULL);
	avl_create(&zv->zv_znode.z_range_avl, zfs_range_compare,
	    sizeof (rl_t), offsetof(rl_t, r_node));
	zv->zv_znode.z_is_zvol = TRUE;

	spin_lock_init(&zv->zv_lock);
	list_link_init(&zv->zv_next);

	zv->zv_disk->major = zvol_major;
	zv->zv_disk->first_minor = (dev & MINORMASK);
	zv->zv_disk->fops = &zvol_ops;
	zv->zv_disk->private_data = zv;
	zv->zv_disk->queue = zv->zv_queue;
	snprintf(zv->zv_disk->disk_name, DISK_NAME_LEN, "%s%d",
	    ZVOL_DEV_NAME, (dev & MINORMASK));

	return zv;

out_queue:
	blk_cleanup_queue(zv->zv_queue);
out_kmem:
	kmem_free(zv, sizeof (zvol_state_t));
out:
	return NULL;
}

/*
 * Cleanup then free a zvol_state_t which was created by zvol_alloc().
 */
static void
zvol_free(zvol_state_t *zv)
{
	avl_destroy(&zv->zv_znode.z_range_avl);
	mutex_destroy(&zv->zv_znode.z_range_lock);

	del_gendisk(zv->zv_disk);
	blk_cleanup_queue(zv->zv_queue);
	put_disk(zv->zv_disk);

	kmem_free(zv, sizeof (zvol_state_t));
}

static int
__zvol_create_minor(const char *name)
{
	zvol_state_t *zv;
	objset_t *os;
	dmu_object_info_t *doi;
	uint64_t volsize;
	unsigned minor = 0;
	int error = 0;

	ASSERT(MUTEX_HELD(&zvol_state_lock));

	zv = zvol_find_by_name(name);
	if (zv) {
		error = EEXIST;
		goto out;
	}

	doi = kmem_alloc(sizeof(dmu_object_info_t), KM_SLEEP);

	error = dmu_objset_own(name, DMU_OST_ZVOL, B_TRUE, zvol_tag, &os);
	if (error)
		goto out_doi;

	error = dmu_object_info(os, ZVOL_OBJ, doi);
	if (error)
		goto out_dmu_objset_disown;

	error = zap_lookup(os, ZVOL_ZAP_OBJ, "size", 8, 1, &volsize);
	if (error)
		goto out_dmu_objset_disown;

	error = zvol_find_minor(&minor);
	if (error)
		goto out_dmu_objset_disown;

	zv = zvol_alloc(MKDEV(zvol_major, minor), name);
	if (zv == NULL) {
		error = EAGAIN;
		goto out_dmu_objset_disown;
	}

	if (dmu_objset_is_snapshot(os))
		zv->zv_flags |= ZVOL_RDONLY;

	zv->zv_volblocksize = doi->doi_data_block_size;
	zv->zv_volsize = volsize;
	zv->zv_objset = os;

	set_capacity(zv->zv_disk, zv->zv_volsize >> 9);

	blk_queue_max_hw_sectors(zv->zv_queue, UINT_MAX);
	blk_queue_max_segments(zv->zv_queue, UINT16_MAX);
	blk_queue_max_segment_size(zv->zv_queue, UINT_MAX);
	blk_queue_physical_block_size(zv->zv_queue, zv->zv_volblocksize);
	blk_queue_io_opt(zv->zv_queue, zv->zv_volblocksize);
#ifdef HAVE_BLK_QUEUE_DISCARD
	blk_queue_max_discard_sectors(zv->zv_queue,
	    (zvol_max_discard_blocks * zv->zv_volblocksize) >> 9);
	blk_queue_discard_granularity(zv->zv_queue, zv->zv_volblocksize);
	queue_flag_set_unlocked(QUEUE_FLAG_DISCARD, zv->zv_queue);
#endif
#ifdef HAVE_BLK_QUEUE_NONROT
	queue_flag_set_unlocked(QUEUE_FLAG_NONROT, zv->zv_queue);
#endif

	if (zil_replay_disable)
		zil_destroy(dmu_objset_zil(os), B_FALSE);
	else
		zil_replay(os, zv, zvol_replay_vector);

out_dmu_objset_disown:
	dmu_objset_disown(os, zvol_tag);
	zv->zv_objset = NULL;
out_doi:
	kmem_free(doi, sizeof(dmu_object_info_t));
out:

	if (error == 0) {
		zvol_insert(zv);
		add_disk(zv->zv_disk);
	}

	return (error);
}

/*
 * Create a block device minor node and setup the linkage between it
 * and the specified volume.  Once this function returns the block
 * device is live and ready for use.
 */
int
zvol_create_minor(const char *name)
{
	int error;

	mutex_enter(&zvol_state_lock);
	error = __zvol_create_minor(name);
	mutex_exit(&zvol_state_lock);

	return (error);
}

static int
__zvol_remove_minor(const char *name)
{
	zvol_state_t *zv;

	ASSERT(MUTEX_HELD(&zvol_state_lock));

	zv = zvol_find_by_name(name);
	if (zv == NULL)
		return (ENXIO);

	if (zv->zv_open_count > 0)
		return (EBUSY);

	zvol_remove(zv);
	zvol_free(zv);

	return (0);
}

/*
 * Remove a block device minor node for the specified volume.
 */
int
zvol_remove_minor(const char *name)
{
	int error;

	mutex_enter(&zvol_state_lock);
	error = __zvol_remove_minor(name);
	mutex_exit(&zvol_state_lock);

	return (error);
}

static int
zvol_create_minors_cb(spa_t *spa, uint64_t dsobj,
		      const char *dsname, void *arg)
{
	if (strchr(dsname, '/') == NULL)
		return 0;

	(void) __zvol_create_minor(dsname);
	return (0);
}

/*
 * Create minors for specified pool, if pool is NULL create minors
 * for all available pools.
 */
int
zvol_create_minors(const char *pool)
{
	spa_t *spa = NULL;
	int error = 0;

	if (zvol_inhibit_dev)
		return (0);

	mutex_enter(&zvol_state_lock);
	if (pool) {
		error = dmu_objset_find_spa(NULL, pool, zvol_create_minors_cb,
		    NULL, DS_FIND_CHILDREN | DS_FIND_SNAPSHOTS);
	} else {
		mutex_enter(&spa_namespace_lock);
		while ((spa = spa_next(spa)) != NULL) {
			error = dmu_objset_find_spa(NULL,
			    spa_name(spa), zvol_create_minors_cb, NULL,
			    DS_FIND_CHILDREN | DS_FIND_SNAPSHOTS);
			if (error)
				break;
		}
		mutex_exit(&spa_namespace_lock);
	}
	mutex_exit(&zvol_state_lock);

	return error;
}

/*
 * Remove minors for specified pool, if pool is NULL remove all minors.
 */
void
zvol_remove_minors(const char *pool)
{
	zvol_state_t *zv, *zv_next;
	char *str;

	if (zvol_inhibit_dev)
		return;

	str = kmem_zalloc(MAXNAMELEN, KM_SLEEP);
	if (pool) {
		(void) strncpy(str, pool, strlen(pool));
		(void) strcat(str, "/");
	}

	mutex_enter(&zvol_state_lock);
	for (zv = list_head(&zvol_state_list); zv != NULL; zv = zv_next) {
		zv_next = list_next(&zvol_state_list, zv);

		if (pool == NULL || !strncmp(str, zv->zv_name, strlen(str))) {
			zvol_remove(zv);
			zvol_free(zv);
		}
	}
	mutex_exit(&zvol_state_lock);
	kmem_free(str, MAXNAMELEN);
}

int
zvol_init(void)
{
	int error;

	zvol_taskq = taskq_create(ZVOL_DRIVER, zvol_threads, maxclsyspri,
		                  zvol_threads, INT_MAX, TASKQ_PREPOPULATE);
	if (zvol_taskq == NULL) {
		printk(KERN_INFO "ZFS: taskq_create() failed\n");
		return (-ENOMEM);
	}

	error = register_blkdev(zvol_major, ZVOL_DRIVER);
	if (error) {
		printk(KERN_INFO "ZFS: register_blkdev() failed %d\n", error);
		taskq_destroy(zvol_taskq);
		return (error);
	}

	blk_register_region(MKDEV(zvol_major, 0), 1UL << MINORBITS,
	                    THIS_MODULE, zvol_probe, NULL, NULL);

	mutex_init(&zvol_state_lock, NULL, MUTEX_DEFAULT, NULL);
	list_create(&zvol_state_list, sizeof (zvol_state_t),
	            offsetof(zvol_state_t, zv_next));

	(void) zvol_create_minors(NULL);

	return (0);
}

void
zvol_fini(void)
{
	zvol_remove_minors(NULL);
	blk_unregister_region(MKDEV(zvol_major, 0), 1UL << MINORBITS);
	unregister_blkdev(zvol_major, ZVOL_DRIVER);
	taskq_destroy(zvol_taskq);
	mutex_destroy(&zvol_state_lock);
	list_destroy(&zvol_state_list);
}

module_param(zvol_inhibit_dev, uint, 0644);
MODULE_PARM_DESC(zvol_inhibit_dev, "Do not create zvol device nodes");

module_param(zvol_major, uint, 0444);
MODULE_PARM_DESC(zvol_major, "Major number for zvol device");

module_param(zvol_threads, uint, 0444);
MODULE_PARM_DESC(zvol_threads, "Number of threads for zvol device");

module_param(zvol_max_discard_blocks, ulong, 0444);
MODULE_PARM_DESC(zvol_max_discard_blocks, "Max number of blocks to discard at once");
