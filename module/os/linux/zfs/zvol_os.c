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
 * Copyright (c) 2012, 2020 by Delphix. All rights reserved.
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

#include <linux/blkdev_compat.h>
#include <linux/task_io_accounting_ops.h>

unsigned int zvol_major = ZVOL_MAJOR;
unsigned int zvol_request_sync = 0;
unsigned int zvol_prefetch_bytes = (128 * 1024);
unsigned long zvol_max_discard_blocks = 16384;
unsigned int zvol_threads = 32;
unsigned int zvol_open_timeout_ms = 1000;

struct zvol_state_os {
	struct gendisk		*zvo_disk;	/* generic disk */
	struct request_queue	*zvo_queue;	/* request queue */
	dev_t			zvo_dev;	/* device id */
};

taskq_t *zvol_taskq;
static struct ida zvol_ida;

typedef struct zv_request_stack {
	zvol_state_t	*zv;
	struct bio	*bio;
} zv_request_t;

typedef struct zv_request_task {
	zv_request_t zvr;
	taskq_ent_t	ent;
} zv_request_task_t;

static zv_request_task_t *
zv_request_task_create(zv_request_t zvr)
{
	zv_request_task_t *task;
	task = kmem_alloc(sizeof (zv_request_task_t), KM_SLEEP);
	taskq_init_ent(&task->ent);
	task->zvr = zvr;
	return (task);
}

static void
zv_request_task_free(zv_request_task_t *task)
{
	kmem_free(task, sizeof (*task));
}

/*
 * Given a path, return TRUE if path is a ZVOL.
 */
static boolean_t
zvol_is_zvol_impl(const char *path)
{
	dev_t dev = 0;

	if (vdev_lookup_bdev(path, &dev) != 0)
		return (B_FALSE);

	if (MAJOR(dev) == zvol_major)
		return (B_TRUE);

	return (B_FALSE);
}

static void
zvol_write(zv_request_t *zvr)
{
	struct bio *bio = zvr->bio;
	int error = 0;
	zfs_uio_t uio;

	zfs_uio_bvec_init(&uio, bio);

	zvol_state_t *zv = zvr->zv;
	ASSERT3P(zv, !=, NULL);
	ASSERT3U(zv->zv_open_count, >, 0);
	ASSERT3P(zv->zv_zilog, !=, NULL);

	/* bio marked as FLUSH need to flush before write */
	if (bio_is_flush(bio))
		zil_commit(zv->zv_zilog, ZVOL_OBJ);

	/* Some requests are just for flush and nothing else. */
	if (uio.uio_resid == 0) {
		rw_exit(&zv->zv_suspend_lock);
		BIO_END_IO(bio, 0);
		return;
	}

	struct request_queue *q = zv->zv_zso->zvo_queue;
	struct gendisk *disk = zv->zv_zso->zvo_disk;
	ssize_t start_resid = uio.uio_resid;
	unsigned long start_time;

	boolean_t acct = blk_queue_io_stat(q);
	if (acct)
		start_time = blk_generic_start_io_acct(q, disk, WRITE, bio);

	boolean_t sync =
	    bio_is_fua(bio) || zv->zv_objset->os_sync == ZFS_SYNC_ALWAYS;

	zfs_locked_range_t *lr = zfs_rangelock_enter(&zv->zv_rangelock,
	    uio.uio_loffset, uio.uio_resid, RL_WRITER);

	uint64_t volsize = zv->zv_volsize;
	while (uio.uio_resid > 0 && uio.uio_loffset < volsize) {
		uint64_t bytes = MIN(uio.uio_resid, DMU_MAX_ACCESS >> 1);
		uint64_t off = uio.uio_loffset;
		dmu_tx_t *tx = dmu_tx_create(zv->zv_objset);

		if (bytes > volsize - off)	/* don't write past the end */
			bytes = volsize - off;

		dmu_tx_hold_write_by_dnode(tx, zv->zv_dn, off, bytes);

		/* This will only fail for ENOSPC */
		error = dmu_tx_assign(tx, TXG_WAIT);
		if (error) {
			dmu_tx_abort(tx);
			break;
		}
		error = dmu_write_uio_dnode(zv->zv_dn, &uio, bytes, tx);
		if (error == 0) {
			zvol_log_write(zv, tx, off, bytes, sync);
		}
		dmu_tx_commit(tx);

		if (error)
			break;
	}
	zfs_rangelock_exit(lr);

	int64_t nwritten = start_resid - uio.uio_resid;
	dataset_kstats_update_write_kstats(&zv->zv_kstat, nwritten);
	task_io_account_write(nwritten);

	if (sync)
		zil_commit(zv->zv_zilog, ZVOL_OBJ);

	rw_exit(&zv->zv_suspend_lock);

	if (acct)
		blk_generic_end_io_acct(q, disk, WRITE, bio, start_time);

	BIO_END_IO(bio, -error);
}

static void
zvol_write_task(void *arg)
{
	zv_request_task_t *task = arg;
	zvol_write(&task->zvr);
	zv_request_task_free(task);
}

static void
zvol_discard(zv_request_t *zvr)
{
	struct bio *bio = zvr->bio;
	zvol_state_t *zv = zvr->zv;
	uint64_t start = BIO_BI_SECTOR(bio) << 9;
	uint64_t size = BIO_BI_SIZE(bio);
	uint64_t end = start + size;
	boolean_t sync;
	int error = 0;
	dmu_tx_t *tx;

	ASSERT3P(zv, !=, NULL);
	ASSERT3U(zv->zv_open_count, >, 0);
	ASSERT3P(zv->zv_zilog, !=, NULL);

	struct request_queue *q = zv->zv_zso->zvo_queue;
	struct gendisk *disk = zv->zv_zso->zvo_disk;
	unsigned long start_time;

	boolean_t acct = blk_queue_io_stat(q);
	if (acct)
		start_time = blk_generic_start_io_acct(q, disk, WRITE, bio);

	sync = bio_is_fua(bio) || zv->zv_objset->os_sync == ZFS_SYNC_ALWAYS;

	if (end > zv->zv_volsize) {
		error = SET_ERROR(EIO);
		goto unlock;
	}

	/*
	 * Align the request to volume block boundaries when a secure erase is
	 * not required.  This will prevent dnode_free_range() from zeroing out
	 * the unaligned parts which is slow (read-modify-write) and useless
	 * since we are not freeing any space by doing so.
	 */
	if (!bio_is_secure_erase(bio)) {
		start = P2ROUNDUP(start, zv->zv_volblocksize);
		end = P2ALIGN(end, zv->zv_volblocksize);
		size = end - start;
	}

	if (start >= end)
		goto unlock;

	zfs_locked_range_t *lr = zfs_rangelock_enter(&zv->zv_rangelock,
	    start, size, RL_WRITER);

	tx = dmu_tx_create(zv->zv_objset);
	dmu_tx_mark_netfree(tx);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error != 0) {
		dmu_tx_abort(tx);
	} else {
		zvol_log_truncate(zv, tx, start, size, B_TRUE);
		dmu_tx_commit(tx);
		error = dmu_free_long_range(zv->zv_objset,
		    ZVOL_OBJ, start, size);
	}
	zfs_rangelock_exit(lr);

	if (error == 0 && sync)
		zil_commit(zv->zv_zilog, ZVOL_OBJ);

unlock:
	rw_exit(&zv->zv_suspend_lock);

	if (acct)
		blk_generic_end_io_acct(q, disk, WRITE, bio, start_time);

	BIO_END_IO(bio, -error);
}

static void
zvol_discard_task(void *arg)
{
	zv_request_task_t *task = arg;
	zvol_discard(&task->zvr);
	zv_request_task_free(task);
}

static void
zvol_read(zv_request_t *zvr)
{
	struct bio *bio = zvr->bio;
	int error = 0;
	zfs_uio_t uio;

	zfs_uio_bvec_init(&uio, bio);

	zvol_state_t *zv = zvr->zv;
	ASSERT3P(zv, !=, NULL);
	ASSERT3U(zv->zv_open_count, >, 0);

	struct request_queue *q = zv->zv_zso->zvo_queue;
	struct gendisk *disk = zv->zv_zso->zvo_disk;
	ssize_t start_resid = uio.uio_resid;
	unsigned long start_time;

	boolean_t acct = blk_queue_io_stat(q);
	if (acct)
		start_time = blk_generic_start_io_acct(q, disk, READ, bio);

	zfs_locked_range_t *lr = zfs_rangelock_enter(&zv->zv_rangelock,
	    uio.uio_loffset, uio.uio_resid, RL_READER);

	uint64_t volsize = zv->zv_volsize;
	while (uio.uio_resid > 0 && uio.uio_loffset < volsize) {
		uint64_t bytes = MIN(uio.uio_resid, DMU_MAX_ACCESS >> 1);

		/* don't read past the end */
		if (bytes > volsize - uio.uio_loffset)
			bytes = volsize - uio.uio_loffset;

		error = dmu_read_uio_dnode(zv->zv_dn, &uio, bytes);
		if (error) {
			/* convert checksum errors into IO errors */
			if (error == ECKSUM)
				error = SET_ERROR(EIO);
			break;
		}
	}
	zfs_rangelock_exit(lr);

	int64_t nread = start_resid - uio.uio_resid;
	dataset_kstats_update_read_kstats(&zv->zv_kstat, nread);
	task_io_account_read(nread);

	rw_exit(&zv->zv_suspend_lock);

	if (acct)
		blk_generic_end_io_acct(q, disk, READ, bio, start_time);

	BIO_END_IO(bio, -error);
}

static void
zvol_read_task(void *arg)
{
	zv_request_task_t *task = arg;
	zvol_read(&task->zvr);
	zv_request_task_free(task);
}

#ifdef HAVE_SUBMIT_BIO_IN_BLOCK_DEVICE_OPERATIONS
#ifdef HAVE_BDEV_SUBMIT_BIO_RETURNS_VOID
static void
zvol_submit_bio(struct bio *bio)
#else
static blk_qc_t
zvol_submit_bio(struct bio *bio)
#endif
#else
static MAKE_REQUEST_FN_RET
zvol_request(struct request_queue *q, struct bio *bio)
#endif
{
#ifdef HAVE_SUBMIT_BIO_IN_BLOCK_DEVICE_OPERATIONS
#if defined(HAVE_BIO_BDEV_DISK)
	struct request_queue *q = bio->bi_bdev->bd_disk->queue;
#else
	struct request_queue *q = bio->bi_disk->queue;
#endif
#endif
	zvol_state_t *zv = q->queuedata;
	fstrans_cookie_t cookie = spl_fstrans_mark();
	uint64_t offset = BIO_BI_SECTOR(bio) << 9;
	uint64_t size = BIO_BI_SIZE(bio);
	int rw = bio_data_dir(bio);

	if (bio_has_data(bio) && offset + size > zv->zv_volsize) {
		printk(KERN_INFO
		    "%s: bad access: offset=%llu, size=%lu\n",
		    zv->zv_zso->zvo_disk->disk_name,
		    (long long unsigned)offset,
		    (long unsigned)size);

		BIO_END_IO(bio, -SET_ERROR(EIO));
		goto out;
	}

	zv_request_t zvr = {
		.zv = zv,
		.bio = bio,
	};
	zv_request_task_t *task;

	if (rw == WRITE) {
		if (unlikely(zv->zv_flags & ZVOL_RDONLY)) {
			BIO_END_IO(bio, -SET_ERROR(EROFS));
			goto out;
		}

		/*
		 * Prevents the zvol from being suspended, or the ZIL being
		 * concurrently opened.  Will be released after the i/o
		 * completes.
		 */
		rw_enter(&zv->zv_suspend_lock, RW_READER);

		/*
		 * Open a ZIL if this is the first time we have written to this
		 * zvol. We protect zv->zv_zilog with zv_suspend_lock rather
		 * than zv_state_lock so that we don't need to acquire an
		 * additional lock in this path.
		 */
		if (zv->zv_zilog == NULL) {
			rw_exit(&zv->zv_suspend_lock);
			rw_enter(&zv->zv_suspend_lock, RW_WRITER);
			if (zv->zv_zilog == NULL) {
				zv->zv_zilog = zil_open(zv->zv_objset,
				    zvol_get_data);
				zv->zv_flags |= ZVOL_WRITTEN_TO;
				/* replay / destroy done in zvol_create_minor */
				VERIFY0((zv->zv_zilog->zl_header->zh_flags &
				    ZIL_REPLAY_NEEDED));
			}
			rw_downgrade(&zv->zv_suspend_lock);
		}

		/*
		 * We don't want this thread to be blocked waiting for i/o to
		 * complete, so we instead wait from a taskq callback. The
		 * i/o may be a ZIL write (via zil_commit()), or a read of an
		 * indirect block, or a read of a data block (if this is a
		 * partial-block write).  We will indicate that the i/o is
		 * complete by calling BIO_END_IO() from the taskq callback.
		 *
		 * This design allows the calling thread to continue and
		 * initiate more concurrent operations by calling
		 * zvol_request() again. There are typically only a small
		 * number of threads available to call zvol_request() (e.g.
		 * one per iSCSI target), so keeping the latency of
		 * zvol_request() low is important for performance.
		 *
		 * The zvol_request_sync module parameter allows this
		 * behavior to be altered, for performance evaluation
		 * purposes.  If the callback blocks, setting
		 * zvol_request_sync=1 will result in much worse performance.
		 *
		 * We can have up to zvol_threads concurrent i/o's being
		 * processed for all zvols on the system.  This is typically
		 * a vast improvement over the zvol_request_sync=1 behavior
		 * of one i/o at a time per zvol.  However, an even better
		 * design would be for zvol_request() to initiate the zio
		 * directly, and then be notified by the zio_done callback,
		 * which would call BIO_END_IO().  Unfortunately, the DMU/ZIL
		 * interfaces lack this functionality (they block waiting for
		 * the i/o to complete).
		 */
		if (bio_is_discard(bio) || bio_is_secure_erase(bio)) {
			if (zvol_request_sync) {
				zvol_discard(&zvr);
			} else {
				task = zv_request_task_create(zvr);
				taskq_dispatch_ent(zvol_taskq,
				    zvol_discard_task, task, 0, &task->ent);
			}
		} else {
			if (zvol_request_sync) {
				zvol_write(&zvr);
			} else {
				task = zv_request_task_create(zvr);
				taskq_dispatch_ent(zvol_taskq,
				    zvol_write_task, task, 0, &task->ent);
			}
		}
	} else {
		/*
		 * The SCST driver, and possibly others, may issue READ I/Os
		 * with a length of zero bytes.  These empty I/Os contain no
		 * data and require no additional handling.
		 */
		if (size == 0) {
			BIO_END_IO(bio, 0);
			goto out;
		}

		rw_enter(&zv->zv_suspend_lock, RW_READER);

		/* See comment in WRITE case above. */
		if (zvol_request_sync) {
			zvol_read(&zvr);
		} else {
			task = zv_request_task_create(zvr);
			taskq_dispatch_ent(zvol_taskq,
			    zvol_read_task, task, 0, &task->ent);
		}
	}

out:
	spl_fstrans_unmark(cookie);
#if (defined(HAVE_MAKE_REQUEST_FN_RET_QC) || \
	defined(HAVE_SUBMIT_BIO_IN_BLOCK_DEVICE_OPERATIONS)) && \
	!defined(HAVE_BDEV_SUBMIT_BIO_RETURNS_VOID)
	return (BLK_QC_T_NONE);
#endif
}

static int
zvol_open(struct block_device *bdev, fmode_t flag)
{
	zvol_state_t *zv;
	int error = 0;
	boolean_t drop_suspend = B_FALSE;
#ifndef HAVE_BLKDEV_GET_ERESTARTSYS
	hrtime_t timeout = MSEC2NSEC(zvol_open_timeout_ms);
	hrtime_t start = gethrtime();

retry:
#endif
	rw_enter(&zvol_state_lock, RW_READER);
	/*
	 * Obtain a copy of private_data under the zvol_state_lock to make
	 * sure that either the result of zvol free code path setting
	 * bdev->bd_disk->private_data to NULL is observed, or zvol_free()
	 * is not called on this zv because of the positive zv_open_count.
	 */
	zv = bdev->bd_disk->private_data;
	if (zv == NULL) {
		rw_exit(&zvol_state_lock);
		return (SET_ERROR(-ENXIO));
	}

	mutex_enter(&zv->zv_state_lock);
	/*
	 * Make sure zvol is not suspended during first open
	 * (hold zv_suspend_lock) and respect proper lock acquisition
	 * ordering - zv_suspend_lock before zv_state_lock
	 */
	if (zv->zv_open_count == 0) {
		if (!rw_tryenter(&zv->zv_suspend_lock, RW_READER)) {
			mutex_exit(&zv->zv_state_lock);
			rw_enter(&zv->zv_suspend_lock, RW_READER);
			mutex_enter(&zv->zv_state_lock);
			/* check to see if zv_suspend_lock is needed */
			if (zv->zv_open_count != 0) {
				rw_exit(&zv->zv_suspend_lock);
			} else {
				drop_suspend = B_TRUE;
			}
		} else {
			drop_suspend = B_TRUE;
		}
	}
	rw_exit(&zvol_state_lock);

	ASSERT(MUTEX_HELD(&zv->zv_state_lock));

	if (zv->zv_open_count == 0) {
		boolean_t drop_namespace = B_FALSE;

		ASSERT(RW_READ_HELD(&zv->zv_suspend_lock));

		/*
		 * In all other call paths the spa_namespace_lock is taken
		 * before the bdev->bd_mutex lock.  However, on open(2)
		 * the __blkdev_get() function calls fops->open() with the
		 * bdev->bd_mutex lock held.  This can result in a deadlock
		 * when zvols from one pool are used as vdevs in another.
		 *
		 * To prevent a lock inversion deadlock we preemptively
		 * take the spa_namespace_lock.  Normally the lock will not
		 * be contended and this is safe because spa_open_common()
		 * handles the case where the caller already holds the
		 * spa_namespace_lock.
		 *
		 * When the lock cannot be aquired after multiple retries
		 * this must be the vdev on zvol deadlock case and we have
		 * no choice but to return an error.  For 5.12 and older
		 * kernels returning -ERESTARTSYS will result in the
		 * bdev->bd_mutex being dropped, then reacquired, and
		 * fops->open() being called again.  This process can be
		 * repeated safely until both locks are acquired.  For 5.13
		 * and newer the -ERESTARTSYS retry logic was removed from
		 * the kernel so the only option is to return the error for
		 * the caller to handle it.
		 */
		if (!mutex_owned(&spa_namespace_lock)) {
			if (!mutex_tryenter(&spa_namespace_lock)) {
				mutex_exit(&zv->zv_state_lock);
				rw_exit(&zv->zv_suspend_lock);

#ifdef HAVE_BLKDEV_GET_ERESTARTSYS
				schedule();
				return (SET_ERROR(-ERESTARTSYS));
#else
				if ((gethrtime() - start) > timeout)
					return (SET_ERROR(-ERESTARTSYS));

				schedule_timeout(MSEC_TO_TICK(10));
				goto retry;
#endif
			} else {
				drop_namespace = B_TRUE;
			}
		}

		error = -zvol_first_open(zv, !(flag & FMODE_WRITE));

		if (drop_namespace)
			mutex_exit(&spa_namespace_lock);
	}

	if (error == 0) {
		if ((flag & FMODE_WRITE) && (zv->zv_flags & ZVOL_RDONLY)) {
			if (zv->zv_open_count == 0)
				zvol_last_close(zv);

			error = SET_ERROR(-EROFS);
		} else {
			zv->zv_open_count++;
		}
	}

	mutex_exit(&zv->zv_state_lock);
	if (drop_suspend)
		rw_exit(&zv->zv_suspend_lock);

	if (error == 0)
		zfs_check_media_change(bdev);

	return (error);
}

static void
zvol_release(struct gendisk *disk, fmode_t mode)
{
	zvol_state_t *zv;
	boolean_t drop_suspend = B_TRUE;

	rw_enter(&zvol_state_lock, RW_READER);
	zv = disk->private_data;

	mutex_enter(&zv->zv_state_lock);
	ASSERT3U(zv->zv_open_count, >, 0);
	/*
	 * make sure zvol is not suspended during last close
	 * (hold zv_suspend_lock) and respect proper lock acquisition
	 * ordering - zv_suspend_lock before zv_state_lock
	 */
	if (zv->zv_open_count == 1) {
		if (!rw_tryenter(&zv->zv_suspend_lock, RW_READER)) {
			mutex_exit(&zv->zv_state_lock);
			rw_enter(&zv->zv_suspend_lock, RW_READER);
			mutex_enter(&zv->zv_state_lock);
			/* check to see if zv_suspend_lock is needed */
			if (zv->zv_open_count != 1) {
				rw_exit(&zv->zv_suspend_lock);
				drop_suspend = B_FALSE;
			}
		}
	} else {
		drop_suspend = B_FALSE;
	}
	rw_exit(&zvol_state_lock);

	ASSERT(MUTEX_HELD(&zv->zv_state_lock));

	zv->zv_open_count--;
	if (zv->zv_open_count == 0) {
		ASSERT(RW_READ_HELD(&zv->zv_suspend_lock));
		zvol_last_close(zv);
	}

	mutex_exit(&zv->zv_state_lock);

	if (drop_suspend)
		rw_exit(&zv->zv_suspend_lock);
}

static int
zvol_ioctl(struct block_device *bdev, fmode_t mode,
    unsigned int cmd, unsigned long arg)
{
	zvol_state_t *zv = bdev->bd_disk->private_data;
	int error = 0;

	ASSERT3U(zv->zv_open_count, >, 0);

	switch (cmd) {
	case BLKFLSBUF:
		fsync_bdev(bdev);
		invalidate_bdev(bdev);
		rw_enter(&zv->zv_suspend_lock, RW_READER);

		if (!(zv->zv_flags & ZVOL_RDONLY))
			txg_wait_synced(dmu_objset_pool(zv->zv_objset), 0);

		rw_exit(&zv->zv_suspend_lock);
		break;

	case BLKZNAME:
		mutex_enter(&zv->zv_state_lock);
		error = copy_to_user((void *)arg, zv->zv_name, MAXNAMELEN);
		mutex_exit(&zv->zv_state_lock);
		break;

	default:
		error = -ENOTTY;
		break;
	}

	return (SET_ERROR(error));
}

#ifdef CONFIG_COMPAT
static int
zvol_compat_ioctl(struct block_device *bdev, fmode_t mode,
    unsigned cmd, unsigned long arg)
{
	return (zvol_ioctl(bdev, mode, cmd, arg));
}
#else
#define	zvol_compat_ioctl	NULL
#endif

static unsigned int
zvol_check_events(struct gendisk *disk, unsigned int clearing)
{
	unsigned int mask = 0;

	rw_enter(&zvol_state_lock, RW_READER);

	zvol_state_t *zv = disk->private_data;
	if (zv != NULL) {
		mutex_enter(&zv->zv_state_lock);
		mask = zv->zv_changed ? DISK_EVENT_MEDIA_CHANGE : 0;
		zv->zv_changed = 0;
		mutex_exit(&zv->zv_state_lock);
	}

	rw_exit(&zvol_state_lock);

	return (mask);
}

static int
zvol_revalidate_disk(struct gendisk *disk)
{
	rw_enter(&zvol_state_lock, RW_READER);

	zvol_state_t *zv = disk->private_data;
	if (zv != NULL) {
		mutex_enter(&zv->zv_state_lock);
		set_capacity(zv->zv_zso->zvo_disk,
		    zv->zv_volsize >> SECTOR_BITS);
		mutex_exit(&zv->zv_state_lock);
	}

	rw_exit(&zvol_state_lock);

	return (0);
}

static int
zvol_update_volsize(zvol_state_t *zv, uint64_t volsize)
{
	struct gendisk *disk = zv->zv_zso->zvo_disk;

#if defined(HAVE_REVALIDATE_DISK_SIZE)
	revalidate_disk_size(disk, zvol_revalidate_disk(disk) == 0);
#elif defined(HAVE_REVALIDATE_DISK)
	revalidate_disk(disk);
#else
	zvol_revalidate_disk(disk);
#endif
	return (0);
}

static void
zvol_clear_private(zvol_state_t *zv)
{
	/*
	 * Cleared while holding zvol_state_lock as a writer
	 * which will prevent zvol_open() from opening it.
	 */
	zv->zv_zso->zvo_disk->private_data = NULL;
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
	sector_t sectors;

	ASSERT3U(zv->zv_open_count, >, 0);

	sectors = get_capacity(zv->zv_zso->zvo_disk);

	if (sectors > 2048) {
		geo->heads = 16;
		geo->sectors = 63;
	} else {
		geo->heads = 2;
		geo->sectors = 4;
	}

	geo->start = 0;
	geo->cylinders = sectors / (geo->heads * geo->sectors);

	return (0);
}

static struct block_device_operations zvol_ops = {
	.open			= zvol_open,
	.release		= zvol_release,
	.ioctl			= zvol_ioctl,
	.compat_ioctl		= zvol_compat_ioctl,
	.check_events		= zvol_check_events,
#ifdef HAVE_BLOCK_DEVICE_OPERATIONS_REVALIDATE_DISK
	.revalidate_disk	= zvol_revalidate_disk,
#endif
	.getgeo			= zvol_getgeo,
	.owner			= THIS_MODULE,
#ifdef HAVE_SUBMIT_BIO_IN_BLOCK_DEVICE_OPERATIONS
	.submit_bio		= zvol_submit_bio,
#endif
};

/*
 * Allocate memory for a new zvol_state_t and setup the required
 * request queue and generic disk structures for the block device.
 */
static zvol_state_t *
zvol_alloc(dev_t dev, const char *name)
{
	zvol_state_t *zv;
	struct zvol_state_os *zso;
	uint64_t volmode;

	if (dsl_prop_get_integer(name, "volmode", &volmode, NULL) != 0)
		return (NULL);

	if (volmode == ZFS_VOLMODE_DEFAULT)
		volmode = zvol_volmode;

	if (volmode == ZFS_VOLMODE_NONE)
		return (NULL);

	zv = kmem_zalloc(sizeof (zvol_state_t), KM_SLEEP);
	zso = kmem_zalloc(sizeof (struct zvol_state_os), KM_SLEEP);
	zv->zv_zso = zso;
	zv->zv_volmode = volmode;

	list_link_init(&zv->zv_next);
	mutex_init(&zv->zv_state_lock, NULL, MUTEX_DEFAULT, NULL);

#ifdef HAVE_SUBMIT_BIO_IN_BLOCK_DEVICE_OPERATIONS
#ifdef HAVE_BLK_ALLOC_DISK
	zso->zvo_disk = blk_alloc_disk(NUMA_NO_NODE);
	if (zso->zvo_disk == NULL)
		goto out_kmem;

	zso->zvo_disk->minors = ZVOL_MINORS;
	zso->zvo_queue = zso->zvo_disk->queue;
#else
	zso->zvo_queue = blk_alloc_queue(NUMA_NO_NODE);
	if (zso->zvo_queue == NULL)
		goto out_kmem;

	zso->zvo_disk = alloc_disk(ZVOL_MINORS);
	if (zso->zvo_disk == NULL) {
		blk_cleanup_queue(zso->zvo_queue);
		goto out_kmem;
	}

	zso->zvo_disk->queue = zso->zvo_queue;
#endif /* HAVE_BLK_ALLOC_DISK */
#else
	zso->zvo_queue = blk_generic_alloc_queue(zvol_request, NUMA_NO_NODE);
	if (zso->zvo_queue == NULL)
		goto out_kmem;

	zso->zvo_disk = alloc_disk(ZVOL_MINORS);
	if (zso->zvo_disk == NULL) {
		blk_cleanup_queue(zso->zvo_queue);
		goto out_kmem;
	}

	zso->zvo_disk->queue = zso->zvo_queue;
#endif /* HAVE_SUBMIT_BIO_IN_BLOCK_DEVICE_OPERATIONS */

	blk_queue_set_write_cache(zso->zvo_queue, B_TRUE, B_TRUE);

	/* Limit read-ahead to a single page to prevent over-prefetching. */
	blk_queue_set_read_ahead(zso->zvo_queue, 1);

	/* Disable write merging in favor of the ZIO pipeline. */
	blk_queue_flag_set(QUEUE_FLAG_NOMERGES, zso->zvo_queue);

	/* Enable /proc/diskstats */
	blk_queue_flag_set(QUEUE_FLAG_IO_STAT, zso->zvo_queue);

	zso->zvo_queue->queuedata = zv;
	zso->zvo_dev = dev;
	zv->zv_open_count = 0;
	strlcpy(zv->zv_name, name, MAXNAMELEN);

	zfs_rangelock_init(&zv->zv_rangelock, NULL, NULL);
	rw_init(&zv->zv_suspend_lock, NULL, RW_DEFAULT, NULL);

	zso->zvo_disk->major = zvol_major;
	zso->zvo_disk->events = DISK_EVENT_MEDIA_CHANGE;

	/*
	 * Setting ZFS_VOLMODE_DEV disables partitioning on ZVOL devices.
	 * This is accomplished by limiting the number of minors for the
	 * device to one and explicitly disabling partition scanning.
	 */
	if (volmode == ZFS_VOLMODE_DEV) {
		zso->zvo_disk->minors = 1;
		zso->zvo_disk->flags &= ~ZFS_GENHD_FL_EXT_DEVT;
		zso->zvo_disk->flags |= ZFS_GENHD_FL_NO_PART;
	}

	zso->zvo_disk->first_minor = (dev & MINORMASK);
	zso->zvo_disk->fops = &zvol_ops;
	zso->zvo_disk->private_data = zv;
	snprintf(zso->zvo_disk->disk_name, DISK_NAME_LEN, "%s%d",
	    ZVOL_DEV_NAME, (dev & MINORMASK));

	return (zv);

out_kmem:
	kmem_free(zso, sizeof (struct zvol_state_os));
	kmem_free(zv, sizeof (zvol_state_t));
	return (NULL);
}

/*
 * Cleanup then free a zvol_state_t which was created by zvol_alloc().
 * At this time, the structure is not opened by anyone, is taken off
 * the zvol_state_list, and has its private data set to NULL.
 * The zvol_state_lock is dropped.
 *
 * This function may take many milliseconds to complete (e.g. we've seen
 * it take over 256ms), due to the calls to "blk_cleanup_queue" and
 * "del_gendisk". Thus, consumers need to be careful to account for this
 * latency when calling this function.
 */
static void
zvol_free(zvol_state_t *zv)
{

	ASSERT(!RW_LOCK_HELD(&zv->zv_suspend_lock));
	ASSERT(!MUTEX_HELD(&zv->zv_state_lock));
	ASSERT0(zv->zv_open_count);
	ASSERT3P(zv->zv_zso->zvo_disk->private_data, ==, NULL);

	rw_destroy(&zv->zv_suspend_lock);
	zfs_rangelock_fini(&zv->zv_rangelock);

	del_gendisk(zv->zv_zso->zvo_disk);
#if defined(HAVE_SUBMIT_BIO_IN_BLOCK_DEVICE_OPERATIONS) && \
	defined(HAVE_BLK_ALLOC_DISK)
	blk_cleanup_disk(zv->zv_zso->zvo_disk);
#else
	blk_cleanup_queue(zv->zv_zso->zvo_queue);
	put_disk(zv->zv_zso->zvo_disk);
#endif

	ida_simple_remove(&zvol_ida,
	    MINOR(zv->zv_zso->zvo_dev) >> ZVOL_MINOR_BITS);

	mutex_destroy(&zv->zv_state_lock);
	dataset_kstats_destroy(&zv->zv_kstat);

	kmem_free(zv->zv_zso, sizeof (struct zvol_state_os));
	kmem_free(zv, sizeof (zvol_state_t));
}

void
zvol_wait_close(zvol_state_t *zv)
{
}

/*
 * Create a block device minor node and setup the linkage between it
 * and the specified volume.  Once this function returns the block
 * device is live and ready for use.
 */
static int
zvol_os_create_minor(const char *name)
{
	zvol_state_t *zv;
	objset_t *os;
	dmu_object_info_t *doi;
	uint64_t volsize;
	uint64_t len;
	unsigned minor = 0;
	int error = 0;
	int idx;
	uint64_t hash = zvol_name_hash(name);

	if (zvol_inhibit_dev)
		return (0);

	idx = ida_simple_get(&zvol_ida, 0, 0, kmem_flags_convert(KM_SLEEP));
	if (idx < 0)
		return (SET_ERROR(-idx));
	minor = idx << ZVOL_MINOR_BITS;

	zv = zvol_find_by_name_hash(name, hash, RW_NONE);
	if (zv) {
		ASSERT(MUTEX_HELD(&zv->zv_state_lock));
		mutex_exit(&zv->zv_state_lock);
		ida_simple_remove(&zvol_ida, idx);
		return (SET_ERROR(EEXIST));
	}

	doi = kmem_alloc(sizeof (dmu_object_info_t), KM_SLEEP);

	error = dmu_objset_own(name, DMU_OST_ZVOL, B_TRUE, B_TRUE, FTAG, &os);
	if (error)
		goto out_doi;

	error = dmu_object_info(os, ZVOL_OBJ, doi);
	if (error)
		goto out_dmu_objset_disown;

	error = zap_lookup(os, ZVOL_ZAP_OBJ, "size", 8, 1, &volsize);
	if (error)
		goto out_dmu_objset_disown;

	zv = zvol_alloc(MKDEV(zvol_major, minor), name);
	if (zv == NULL) {
		error = SET_ERROR(EAGAIN);
		goto out_dmu_objset_disown;
	}
	zv->zv_hash = hash;

	if (dmu_objset_is_snapshot(os))
		zv->zv_flags |= ZVOL_RDONLY;

	zv->zv_volblocksize = doi->doi_data_block_size;
	zv->zv_volsize = volsize;
	zv->zv_objset = os;

	set_capacity(zv->zv_zso->zvo_disk, zv->zv_volsize >> 9);

	blk_queue_max_hw_sectors(zv->zv_zso->zvo_queue,
	    (DMU_MAX_ACCESS / 4) >> 9);
	blk_queue_max_segments(zv->zv_zso->zvo_queue, UINT16_MAX);
	blk_queue_max_segment_size(zv->zv_zso->zvo_queue, UINT_MAX);
	blk_queue_physical_block_size(zv->zv_zso->zvo_queue,
	    zv->zv_volblocksize);
	blk_queue_io_opt(zv->zv_zso->zvo_queue, zv->zv_volblocksize);
	blk_queue_max_discard_sectors(zv->zv_zso->zvo_queue,
	    (zvol_max_discard_blocks * zv->zv_volblocksize) >> 9);
	blk_queue_discard_granularity(zv->zv_zso->zvo_queue,
	    zv->zv_volblocksize);
#ifdef QUEUE_FLAG_DISCARD
	blk_queue_flag_set(QUEUE_FLAG_DISCARD, zv->zv_zso->zvo_queue);
#endif
#ifdef QUEUE_FLAG_NONROT
	blk_queue_flag_set(QUEUE_FLAG_NONROT, zv->zv_zso->zvo_queue);
#endif
#ifdef QUEUE_FLAG_ADD_RANDOM
	blk_queue_flag_clear(QUEUE_FLAG_ADD_RANDOM, zv->zv_zso->zvo_queue);
#endif
	/* This flag was introduced in kernel version 4.12. */
#ifdef QUEUE_FLAG_SCSI_PASSTHROUGH
	blk_queue_flag_set(QUEUE_FLAG_SCSI_PASSTHROUGH, zv->zv_zso->zvo_queue);
#endif

	ASSERT3P(zv->zv_zilog, ==, NULL);
	zv->zv_zilog = zil_open(os, zvol_get_data);
	if (spa_writeable(dmu_objset_spa(os))) {
		if (zil_replay_disable)
			zil_destroy(zv->zv_zilog, B_FALSE);
		else
			zil_replay(os, zv, zvol_replay_vector);
	}
	zil_close(zv->zv_zilog);
	zv->zv_zilog = NULL;
	ASSERT3P(zv->zv_kstat.dk_kstats, ==, NULL);
	dataset_kstats_create(&zv->zv_kstat, zv->zv_objset);

	/*
	 * When udev detects the addition of the device it will immediately
	 * invoke blkid(8) to determine the type of content on the device.
	 * Prefetching the blocks commonly scanned by blkid(8) will speed
	 * up this process.
	 */
	len = MIN(MAX(zvol_prefetch_bytes, 0), SPA_MAXBLOCKSIZE);
	if (len > 0) {
		dmu_prefetch(os, ZVOL_OBJ, 0, 0, len, ZIO_PRIORITY_SYNC_READ);
		dmu_prefetch(os, ZVOL_OBJ, 0, volsize - len, len,
		    ZIO_PRIORITY_SYNC_READ);
	}

	zv->zv_objset = NULL;
out_dmu_objset_disown:
	dmu_objset_disown(os, B_TRUE, FTAG);
out_doi:
	kmem_free(doi, sizeof (dmu_object_info_t));

	/*
	 * Keep in mind that once add_disk() is called, the zvol is
	 * announced to the world, and zvol_open()/zvol_release() can
	 * be called at any time. Incidentally, add_disk() itself calls
	 * zvol_open()->zvol_first_open() and zvol_release()->zvol_last_close()
	 * directly as well.
	 */
	if (error == 0) {
		rw_enter(&zvol_state_lock, RW_WRITER);
		zvol_insert(zv);
		rw_exit(&zvol_state_lock);
#ifdef HAVE_ADD_DISK_RET
		error = add_disk(zv->zv_zso->zvo_disk);
#else
		add_disk(zv->zv_zso->zvo_disk);
#endif
	} else {
		ida_simple_remove(&zvol_ida, idx);
	}

	return (error);
}

static void
zvol_rename_minor(zvol_state_t *zv, const char *newname)
{
	int readonly = get_disk_ro(zv->zv_zso->zvo_disk);

	ASSERT(RW_LOCK_HELD(&zvol_state_lock));
	ASSERT(MUTEX_HELD(&zv->zv_state_lock));

	strlcpy(zv->zv_name, newname, sizeof (zv->zv_name));

	/* move to new hashtable entry  */
	zv->zv_hash = zvol_name_hash(zv->zv_name);
	hlist_del(&zv->zv_hlink);
	hlist_add_head(&zv->zv_hlink, ZVOL_HT_HEAD(zv->zv_hash));

	/*
	 * The block device's read-only state is briefly changed causing
	 * a KOBJ_CHANGE uevent to be issued.  This ensures udev detects
	 * the name change and fixes the symlinks.  This does not change
	 * ZVOL_RDONLY in zv->zv_flags so the actual read-only state never
	 * changes.  This would normally be done using kobject_uevent() but
	 * that is a GPL-only symbol which is why we need this workaround.
	 */
	set_disk_ro(zv->zv_zso->zvo_disk, !readonly);
	set_disk_ro(zv->zv_zso->zvo_disk, readonly);
}

static void
zvol_set_disk_ro_impl(zvol_state_t *zv, int flags)
{

	set_disk_ro(zv->zv_zso->zvo_disk, flags);
}

static void
zvol_set_capacity_impl(zvol_state_t *zv, uint64_t capacity)
{

	set_capacity(zv->zv_zso->zvo_disk, capacity);
}

const static zvol_platform_ops_t zvol_linux_ops = {
	.zv_free = zvol_free,
	.zv_rename_minor = zvol_rename_minor,
	.zv_create_minor = zvol_os_create_minor,
	.zv_update_volsize = zvol_update_volsize,
	.zv_clear_private = zvol_clear_private,
	.zv_is_zvol = zvol_is_zvol_impl,
	.zv_set_disk_ro = zvol_set_disk_ro_impl,
	.zv_set_capacity = zvol_set_capacity_impl,
};

int
zvol_init(void)
{
	int error;
	int threads = MIN(MAX(zvol_threads, 1), 1024);

	error = register_blkdev(zvol_major, ZVOL_DRIVER);
	if (error) {
		printk(KERN_INFO "ZFS: register_blkdev() failed %d\n", error);
		return (error);
	}
	zvol_taskq = taskq_create(ZVOL_DRIVER, threads, maxclsyspri,
	    threads * 2, INT_MAX, TASKQ_PREPOPULATE | TASKQ_DYNAMIC);
	if (zvol_taskq == NULL) {
		unregister_blkdev(zvol_major, ZVOL_DRIVER);
		return (-ENOMEM);
	}
	zvol_init_impl();
	ida_init(&zvol_ida);
	zvol_register_ops(&zvol_linux_ops);
	return (0);
}

void
zvol_fini(void)
{
	zvol_fini_impl();
	unregister_blkdev(zvol_major, ZVOL_DRIVER);
	taskq_destroy(zvol_taskq);
	ida_destroy(&zvol_ida);
}

/* BEGIN CSTYLED */
module_param(zvol_inhibit_dev, uint, 0644);
MODULE_PARM_DESC(zvol_inhibit_dev, "Do not create zvol device nodes");

module_param(zvol_major, uint, 0444);
MODULE_PARM_DESC(zvol_major, "Major number for zvol device");

module_param(zvol_threads, uint, 0444);
MODULE_PARM_DESC(zvol_threads, "Max number of threads to handle I/O requests");

module_param(zvol_request_sync, uint, 0644);
MODULE_PARM_DESC(zvol_request_sync, "Synchronously handle bio requests");

module_param(zvol_max_discard_blocks, ulong, 0444);
MODULE_PARM_DESC(zvol_max_discard_blocks, "Max number of blocks to discard");

module_param(zvol_prefetch_bytes, uint, 0644);
MODULE_PARM_DESC(zvol_prefetch_bytes, "Prefetch N bytes at zvol start+end");

module_param(zvol_volmode, uint, 0644);
MODULE_PARM_DESC(zvol_volmode, "Default volmode property value");
/* END CSTYLED */
