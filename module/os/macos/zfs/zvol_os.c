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
 * Copyright (c) 2020 by Jorgen Lundman. All rights reserved.
 */

#include <sys/dataset_kstats.h>
#include <sys/disk.h>
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
#include <sys/zvol_os.h>
#include <sys/zvolIO.h>
#include <sys/fm/fs/zfs.h>
#include <libkern/OSDebug.h>

static uint32_t zvol_major = ZVOL_MAJOR;

unsigned int zvol_request_sync = 0;
unsigned int zvol_prefetch_bytes = (128 * 1024);
unsigned long zvol_max_discard_blocks = 16384;
unsigned int zvol_threads = 8;

taskq_t *zvol_taskq;

/* Until we can find a solution that works for us too */
extern list_t zvol_state_list;

extern unsigned int spl_split_stack_below;
_Atomic unsigned int spl_lowest_zvol_stack_remaining = UINT_MAX;

typedef struct zv_request {
	zvol_state_t	*zv_zv;

	union {
		void (*zv_func)(zvol_state_t *, void *);
		int (*zv_ifunc)(zvol_state_t *, void *);
	};
	void *zv_arg;
	int zv_rv;

	/* Used with zv_ifunc to wait for completion */
	kmutex_t zv_lock;
	kcondvar_t zv_cv;

	taskq_ent_t	zv_ent;
} zv_request_t;

#define	ZVOL_LOCK_HELD		(1<<0)
#define	ZVOL_LOCK_SPA		(1<<1)
#define	ZVOL_LOCK_SUSPEND	(1<<2)

static void
zvol_os_spawn_cb(void *param)
{
	zv_request_t *zvr = (zv_request_t *)param;
	zvr->zv_func(zvr->zv_zv, zvr->zv_arg);
	kmem_free(zvr, sizeof (zv_request_t));
}

static void
zvol_os_spawn(zvol_state_t *zv,
    void (*func)(zvol_state_t *, void *), void *arg)
{
	zv_request_t *zvr;
	zvr = kmem_alloc(sizeof (zv_request_t), KM_SLEEP);
	zvr->zv_zv = zv;
	zvr->zv_arg = arg;
	zvr->zv_func = func;

	taskq_init_ent(&zvr->zv_ent);

	taskq_dispatch_ent(zvol_taskq,
	    zvol_os_spawn_cb, zvr, 0, &zvr->zv_ent);
}

static void
zvol_os_spawn_wait_cb(void *param)
{
	zv_request_t *zvr = (zv_request_t *)param;

	zvr->zv_rv = zvr->zv_ifunc(zvr->zv_zv, zvr->zv_arg);

	zvr->zv_func = NULL;
	mutex_enter(&zvr->zv_lock);
	cv_broadcast(&zvr->zv_cv);
	mutex_exit(&zvr->zv_lock);
}

static int
zvol_os_spawn_wait(zvol_state_t *zv,
    int (*func)(zvol_state_t *, void *), void *arg)
{
	int rv;
	zv_request_t *zvr;
	zvr = kmem_alloc(sizeof (zv_request_t), KM_SLEEP);
	zvr->zv_zv = zv;
	zvr->zv_arg = arg;
	zvr->zv_ifunc = func;

	taskq_init_ent(&zvr->zv_ent);
	cv_init(&zvr->zv_cv, NULL, CV_DEFAULT, NULL);
	mutex_init(&zvr->zv_lock, NULL, MUTEX_DEFAULT, NULL);

	mutex_enter(&zvr->zv_lock);

	taskq_dispatch_ent(zvol_taskq,
	    zvol_os_spawn_wait_cb, zvr, 0, &zvr->zv_ent);

	/* Make sure it ran, by waiting */
	cv_wait(&zvr->zv_cv, &zvr->zv_lock);
	mutex_exit(&zvr->zv_lock);

	mutex_destroy(&zvr->zv_lock);
	cv_destroy(&zvr->zv_cv);

	VERIFY3P(zvr->zv_ifunc, ==, NULL);
	rv = zvr->zv_rv;
	kmem_free(zvr, sizeof (zv_request_t));
	return (rv);
}

/*
 * Given a path, return TRUE if path is a ZVOL.
 */
boolean_t
zvol_os_is_zvol(const char *device)
{
	if (device == NULL)
		return (B_FALSE);
	return (zvol_os_is_zvol_impl(device));
}

/*
 * Make sure zv is still in the list (not freed) and if it is
 * grab the locks in the correct order. We can not access "zv"
 * until we know it exists in the list. (may be freed memory)
 * Can we rely on list_link_active() instead of looping list?
 * Return value:
 *        0           : not found. No locks.
 *  ZVOL_LOCK_HELD    : found and zv->zv_state_lock held
 * |ZVOL_LOCK_SPA     : spa_namespace_lock held
 * |ZVOL_LOCK_SUSPEND : zv->zv_state_lock held
 * call zvol_os_verify_lock_exit() to release
 */
static int
zvol_os_verify_and_lock(zvol_state_t *node, boolean_t takesuspend)
{
	zvol_state_t *zv;
	int ret = ZVOL_LOCK_HELD;

retry:
	rw_enter(&zvol_state_lock, RW_READER);
	for (zv = list_head(&zvol_state_list); zv != NULL;
	    zv = list_next(&zvol_state_list, zv)) {

		/* Until we find the node ... */
		if (zv != node)
			continue;

		/* If this is to be first open, deal with spa_namespace */
		if (zv->zv_open_count == 0 &&
		    !mutex_owned(&spa_namespace_lock)) {
			/*
			 * We need to guarantee that the namespace lock is held
			 * to avoid spurious failures in zvol_first_open.
			 */
			ret |= ZVOL_LOCK_SPA;
			if (!mutex_tryenter(&spa_namespace_lock)) {
				rw_exit(&zvol_state_lock);
				mutex_enter(&spa_namespace_lock);
				/* Sadly, this will restart for loop */
				goto retry;
			}
		}

		mutex_enter(&zv->zv_state_lock);

		/*
		 * make sure zvol is not suspended during first open
		 * (hold zv_suspend_lock) and respect proper lock acquisition
		 * ordering - zv_suspend_lock before zv_state_lock
		 */
		if (zv->zv_open_count == 0 || takesuspend) {
			ret |= ZVOL_LOCK_SUSPEND;
			if (!rw_tryenter(&zv->zv_suspend_lock, RW_READER)) {
				mutex_exit(&zv->zv_state_lock);

				/* If we hold spa_namespace, we can deadlock */
				if (ret & ZVOL_LOCK_SPA) {
					rw_exit(&zvol_state_lock);
					mutex_exit(&spa_namespace_lock);
					ret &= ~ZVOL_LOCK_SPA;
					dprintf("%s: spa_namespace loop\n",
					    __func__);
					/* Let's not busy loop */
					delay(hz>>2);
					goto retry;
				}
				rw_enter(&zv->zv_suspend_lock, RW_READER);
				mutex_enter(&zv->zv_state_lock);
				/* check to see if zv_suspend_lock is needed */
				if (zv->zv_open_count != 0) {
					rw_exit(&zv->zv_suspend_lock);
					ret &= ~ZVOL_LOCK_SUSPEND;
				}
			}
		}
		rw_exit(&zvol_state_lock);

		/* Success */
		return (ret);

	} /* for */

	/* Not found */
	rw_exit(&zvol_state_lock);

	/* It's possible we grabbed spa, but then didn't re-find zv */
	if (ret & ZVOL_LOCK_SPA)
		mutex_exit(&spa_namespace_lock);
	return (0);
}

static void
zvol_os_verify_lock_exit(zvol_state_t *zv, int locks)
{
	if (locks & ZVOL_LOCK_SPA)
		mutex_exit(&spa_namespace_lock);
	mutex_exit(&zv->zv_state_lock);
	if (locks & ZVOL_LOCK_SUSPEND)
		rw_exit(&zv->zv_suspend_lock);
}

static void
zvol_os_register_device_cb(zvol_state_t *zv, void *param)
{
	int locks;

	if ((locks = zvol_os_verify_and_lock(zv, zv->zv_open_count == 0)) == 0)
		return;

	zvol_os_verify_lock_exit(zv, locks);

	/* This is a bit racy? */
	zvolRegisterDevice(zv);

}

int
zvol_os_write(dev_t dev, struct uio *uio, int p)
{
	return (ENOTSUP);
}

int
zvol_os_read(dev_t dev, struct uio *uio, int p)
{
	return (ENOTSUP);
}

static int
zvol_os_write_zv_impl(zvol_state_t *zv, void *param)
{
	zfs_uio_t *uio = (zfs_uio_t *)param;
	int error = 0;

	if (zv == NULL)
		return (ENXIO);

	rw_enter(&zv->zv_suspend_lock, RW_READER);

	/* Some requests are just for flush and nothing else. */
	if (zfs_uio_resid(uio) == 0) {
		rw_exit(&zv->zv_suspend_lock);
		return (0);
	}

	ssize_t start_resid = zfs_uio_resid(uio);
	boolean_t sync = (zv->zv_objset->os_sync == ZFS_SYNC_ALWAYS);

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
			    zvol_get_data, NULL);
			zv->zv_flags |= ZVOL_WRITTEN_TO;
		}
		rw_downgrade(&zv->zv_suspend_lock);
	}

	zfs_locked_range_t *lr = zfs_rangelock_enter(&zv->zv_rangelock,
	    zfs_uio_offset(uio), zfs_uio_resid(uio), RL_WRITER);

	uint64_t volsize = zv->zv_volsize;
	while (zfs_uio_resid(uio) > 0 && zfs_uio_offset(uio) < volsize) {
		uint64_t bytes = MIN(zfs_uio_resid(uio), DMU_MAX_ACCESS >> 1);
		uint64_t off = zfs_uio_offset(uio);
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
		error = dmu_write_uio_dnode(zv->zv_dn, uio, bytes, tx);
		if (error == 0) {
			zvol_log_write(zv, tx, off, bytes, sync);
		}
		dmu_tx_commit(tx);

		if (error)
			break;
	}
	zfs_rangelock_exit(lr);

	int64_t nwritten = start_resid - zfs_uio_resid(uio);
	dataset_kstats_update_write_kstats(&zv->zv_kstat, nwritten);

	if (sync)
		zil_commit(zv->zv_zilog, ZVOL_OBJ);

	rw_exit(&zv->zv_suspend_lock);

	return (error);
}

int
zvol_os_write_zv(zvol_state_t *zv, zfs_uio_t *uio)
{
	/* Start IO - possibly as taskq */
	const vm_offset_t r = OSKernelStackRemaining();

	if (r < spl_lowest_zvol_stack_remaining)
		spl_lowest_zvol_stack_remaining = r;

	if (zfs_uio_resid(uio) != 0 &&
	    r < spl_split_stack_below)
		return (zvol_os_spawn_wait(zv, zvol_os_write_zv_impl, uio));

	return (zvol_os_write_zv_impl(zv, uio));
}

int
zvol_os_read_zv_impl(zvol_state_t *zv, void *param)
{
	zfs_uio_t *uio = (zfs_uio_t *)param;
	int error = 0;

	ASSERT3P(zv, !=, NULL);
	ASSERT3U(zv->zv_open_count, >, 0);

	ssize_t start_resid = zfs_uio_resid(uio);

	rw_enter(&zv->zv_suspend_lock, RW_READER);

	zfs_locked_range_t *lr = zfs_rangelock_enter(&zv->zv_rangelock,
	    zfs_uio_offset(uio), zfs_uio_resid(uio), RL_READER);

	uint64_t volsize = zv->zv_volsize;
	while (zfs_uio_resid(uio) > 0 && zfs_uio_offset(uio) < volsize) {
		uint64_t bytes = MIN(zfs_uio_resid(uio), DMU_MAX_ACCESS >> 1);

		/* don't read past the end */
		if (bytes > volsize - zfs_uio_offset(uio))
			bytes = volsize - zfs_uio_offset(uio);

		error = dmu_read_uio_dnode(zv->zv_dn, uio, bytes);
		if (error) {
			/* convert checksum errors into IO errors */
			if (error == ECKSUM)
				error = SET_ERROR(EIO);
			break;
		}
	}
	zfs_rangelock_exit(lr);

	int64_t nread = start_resid - zfs_uio_resid(uio);
	dataset_kstats_update_read_kstats(&zv->zv_kstat, nread);
	rw_exit(&zv->zv_suspend_lock);


	return (error);
}

int
zvol_os_read_zv(zvol_state_t *zv, zfs_uio_t *uio)
{
	/* Start IO - possibly as taskq */
	const vm_offset_t r = OSKernelStackRemaining();

	if (r < spl_lowest_zvol_stack_remaining)
		spl_lowest_zvol_stack_remaining = r;

	if (zfs_uio_resid(uio) != 0 &&
	    r < spl_split_stack_below)
		return (zvol_os_spawn_wait(zv, zvol_os_read_zv_impl, uio));

	return (zvol_os_read_zv_impl(zv, uio));
}

int
zvol_os_unmap(zvol_state_t *zv, uint64_t off, uint64_t bytes)
{
	zfs_locked_range_t *lr = NULL;
	dmu_tx_t *tx = NULL;
	int error = 0;
	uint64_t end = off + bytes;

	if (zv == NULL)
		return (ENXIO);

	/*
	 * XNU's wipefs_wipe() will issue one giant unmap for the entire
	 * device;
	 * zfs create -V 8g BOOM/vol
	 * zvolIO doDiscard calling zvol_unmap with offset, bytes (0, 858992)
	 * Which will both take too long, and is uneccessary. We will ignore
	 * any unmaps deemed "too large".
	 */
	if ((off == 0ULL) &&
	    (zv->zv_volsize > (1ULL << 24)) && /* 16Mb slop */
	    (bytes >= (zv->zv_volsize - (1ULL << 24))))
		return (0);

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
			    zvol_get_data, NULL);
			zv->zv_flags |= ZVOL_WRITTEN_TO;
		}
		rw_downgrade(&zv->zv_suspend_lock);
	}

	off = P2ROUNDUP(off, zv->zv_volblocksize);
	end = P2ALIGN(end, zv->zv_volblocksize);

	if (end > zv->zv_volsize) /* don't write past the end */
		end = zv->zv_volsize;

	if (off >= end) {
		/* Return success- caller does not need to know */
		goto out;
	}

	bytes = end - off;
	lr = zfs_rangelock_enter(&zv->zv_rangelock, off, bytes, RL_WRITER);

	tx = dmu_tx_create(zv->zv_objset);

	dmu_tx_mark_netfree(tx);

	error = dmu_tx_assign(tx, TXG_WAIT);

	if (error) {
		dmu_tx_abort(tx);
	} else {

		zvol_log_truncate(zv, tx, off, bytes, B_TRUE);

		dmu_tx_commit(tx);

		error = dmu_free_long_range(zv->zv_objset,
		    ZVOL_OBJ, off, bytes);
	}

	zfs_rangelock_exit(lr);

	if (error == 0) {
		/*
		 * If the 'sync' property is set to 'always' then
		 * treat this as a synchronous operation
		 * (i.e. commit to zil).
		 */
		if (zv->zv_objset->os_sync == ZFS_SYNC_ALWAYS) {
			zil_commit(zv->zv_zilog, ZVOL_OBJ);
		}
	}

out:
	rw_exit(&zv->zv_suspend_lock);
	return (error);
}

int
zvol_os_update_volsize(zvol_state_t *zv, uint64_t volsize)
{
	zv->zv_volsize = volsize;
	return (0);
}

static void
zvol_os_clear_private_cb(zvol_state_t *zv, void *param)
{
	zvolRemoveDeviceTerminate(param);
}

void
zvol_os_clear_private(zvol_state_t *zv)
{
	void *term;

	dprintf("%s\n", __func__);

	/* We can do all removal work, except call terminate. */
	term = zvolRemoveDevice(zv);
	if (term == NULL)
		return;

	zvol_remove_symlink(zv);

	zv->zv_zso->zvo_iokitdev = NULL;

	/* Call terminate in the background */
	zvol_os_spawn(zv, zvol_os_clear_private_cb, term);

}

/*
 * Find a zvol_state_t given the full major+minor dev_t. If found,
 * return with zv_state_lock taken, otherwise, return (NULL) without
 * taking zv_state_lock.
 */
static zvol_state_t *
zvol_os_find_by_dev(dev_t dev)
{
	zvol_state_t *zv;

	rw_enter(&zvol_state_lock, RW_READER);
	for (zv = list_head(&zvol_state_list); zv != NULL;
	    zv = list_next(&zvol_state_list, zv)) {
		mutex_enter(&zv->zv_state_lock);
		if (zv->zv_zso->zvo_dev == dev) {
			rw_exit(&zvol_state_lock);
			return (zv);
		}
		mutex_exit(&zv->zv_state_lock);
	}
	rw_exit(&zvol_state_lock);

	return (NULL);
}

void
zvol_os_validate_dev(zvol_state_t *zv)
{
}

/*
 * Allocate memory for a new zvol_state_t and setup the required
 * request queue and generic disk structures for the block device.
 */
static zvol_state_t *
zvol_os_alloc(dev_t dev, const char *name)
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

	list_link_init(&zv->zv_next);
	mutex_init(&zv->zv_state_lock, NULL, MUTEX_DEFAULT, NULL);

	zv->zv_open_count = 0;
	strlcpy(zv->zv_name, name, MAXNAMELEN);

	zfs_rangelock_init(&zv->zv_rangelock, NULL, NULL);
	rw_init(&zv->zv_suspend_lock, NULL, RW_DEFAULT, NULL);

	return (zv);
#if 0
out_kmem:
	kmem_free(zso, sizeof (struct zvol_state_os));
	kmem_free(zv, sizeof (zvol_state_t));
	return (NULL);
#endif
}

/*
 * Cleanup then free a zvol_state_t which was created by zvol_alloc().
 * At this time, the structure is not opened by anyone, is taken off
 * the zvol_state_list, and has its private data set to NULL.
 * The zvol_state_lock is dropped.
 *
 */
void
zvol_os_free(zvol_state_t *zv)
{
	ASSERT(!RW_LOCK_HELD(&zv->zv_suspend_lock));
	ASSERT(!MUTEX_HELD(&zv->zv_state_lock));
	ASSERT(zv->zv_open_count == 0);

	rw_destroy(&zv->zv_suspend_lock);
	zfs_rangelock_fini(&zv->zv_rangelock);

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
int
zvol_os_create_minor(const char *name)
{
	zvol_state_t *zv;
	objset_t *os;
	dmu_object_info_t *doi;
	uint64_t volsize;
	unsigned minor = 0;
	int error = 0;
	uint64_t hash = zvol_name_hash(name);

	dprintf("%s\n", __func__);

	if (zvol_inhibit_dev)
		return (0);

	// minor?
	zv = zvol_find_by_name_hash(name, hash, RW_NONE);
	if (zv) {
		ASSERT(MUTEX_HELD(&zv->zv_state_lock));
		mutex_exit(&zv->zv_state_lock);
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

	zv = zvol_os_alloc(makedevice(zvol_major, minor), name);
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

	// set_capacity(zv->zv_zso->zvo_disk, zv->zv_volsize >> 9);
	ASSERT3P(zv->zv_zilog, ==, NULL);
	zv->zv_zilog = zil_open(os, zvol_get_data, NULL);
	if (spa_writeable(dmu_objset_spa(os))) {
		if (zil_replay_disable)
			zil_destroy(zv->zv_zilog, B_FALSE);
		else
			zil_replay(os, zv, zvol_replay_vector);
	}
	zil_close(zv->zv_zilog);
	zv->zv_zilog = NULL;

	dataset_kstats_create(&zv->zv_kstat, zv->zv_objset);

	/* Create the IOKit zvol while owned */
	if ((error = zvolCreateNewDevice(zv)) != 0) {
		dprintf("%s zvolCreateNewDevice error %d\n",
		    __func__, error);
	}

	zv->zv_objset = NULL;
out_dmu_objset_disown:
	dmu_objset_disown(os, B_TRUE, FTAG);
out_doi:
	kmem_free(doi, sizeof (dmu_object_info_t));

	if (error == 0) {
		rw_enter(&zvol_state_lock, RW_WRITER);
		zvol_insert(zv);
		rw_exit(&zvol_state_lock);

		/* Register (async) IOKit zvol after disown and unlock */
		/* The callback with release the mutex */
		zvol_os_spawn(zv, zvol_os_register_device_cb, NULL);

	} else {

	}

	dprintf("%s complete\n", __func__);
	return (error);
}


static void zvol_os_rename_device_cb(zvol_state_t *zv, void *param)
{
	int locks;

	if ((locks = zvol_os_verify_and_lock(zv, zv->zv_open_count == 0)) == 0)
		return;

	zvol_add_symlink(zv, zv->zv_zso->zvo_bsdname + 1,
	    zv->zv_zso->zvo_bsdname);

	zvol_os_verify_lock_exit(zv, locks);
	zvolRenameDevice(zv);
}

void
zvol_os_rename_minor(zvol_state_t *zv, const char *newname)
{
	// int readonly = get_disk_ro(zv->zv_zso->zvo_disk);

	ASSERT(RW_LOCK_HELD(&zvol_state_lock));
	ASSERT(MUTEX_HELD(&zv->zv_state_lock));

	zvol_remove_symlink(zv);

	strlcpy(zv->zv_name, newname, sizeof (zv->zv_name));

	/* move to new hashtable entry  */
	zv->zv_hash = zvol_name_hash(zv->zv_name);
	hlist_del(&zv->zv_hlink);
	hlist_add_head(&zv->zv_hlink, ZVOL_HT_HEAD(zv->zv_hash));

	zvol_os_spawn(zv, zvol_os_rename_device_cb, NULL);

	/*
	 * The block device's read-only state is briefly changed causing
	 * a KOBJ_CHANGE uevent to be issued.  This ensures udev detects
	 * the name change and fixes the symlinks.  This does not change
	 * ZVOL_RDONLY in zv->zv_flags so the actual read-only state never
	 * changes.  This would normally be done using kobject_uevent() but
	 * that is a GPL-only symbol which is why we need this workaround.
	 */
	// set_disk_ro(zv->zv_zso->zvo_disk, !readonly);
	// set_disk_ro(zv->zv_zso->zvo_disk, readonly);
}

void
zvol_os_set_disk_ro(zvol_state_t *zv, int flags)
{
	// set_disk_ro(zv->zv_zso->zvo_disk, flags);
}

void
zvol_os_set_capacity(zvol_state_t *zv, uint64_t capacity)
{
	// set_capacity(zv->zv_zso->zvo_disk, capacity);
}

int
zvol_os_open_zv(zvol_state_t *zv, int flag, int otyp, struct proc *p)
{
	int error = 0;
	int locks;

	/*
	 * make sure zvol is not suspended during first open
	 * (hold zv_suspend_lock) and respect proper lock acquisition
	 * ordering - zv_suspend_lock before zv_state_lock
	 */
	if ((locks = zvol_os_verify_and_lock(zv, zv->zv_open_count == 0))
	    == 0) {
		return (SET_ERROR(ENOENT));
	}

	ASSERT(MUTEX_HELD(&zv->zv_state_lock));
	ASSERT(zv->zv_open_count != 0 || RW_READ_HELD(&zv->zv_suspend_lock));

	/*
	 * We often race opens due to DiskArb. So if spa_namespace_lock is
	 * already held, potentially a zvol_first_open() is already in progress
	 */
	if (zv->zv_open_count == 0) {
		error = zvol_first_open(zv, !(flag & FWRITE));
		if (error)
			goto out_mutex;
	}

	if ((flag & FWRITE) && (zv->zv_flags & ZVOL_RDONLY)) {
		error = EROFS;
		goto out_open_count;
	}

	zv->zv_open_count++;

	zvol_os_verify_lock_exit(zv, locks);
	return (0);

out_open_count:
	if (zv->zv_open_count == 0)
		zvol_last_close(zv);

out_mutex:
	zvol_os_verify_lock_exit(zv, locks);

	if (error == EINTR) {
		error = ERESTART;
		schedule();
	}
	return (SET_ERROR(error));
}

int
zvol_os_open(dev_t devp, int flag, int otyp, struct proc *p)
{
	zvol_state_t *zv;
	int error = 0;

	if (!getminor(devp))
		return (0);

	zv = zvol_os_find_by_dev(devp);
	if (zv == NULL) {
		return (SET_ERROR(ENXIO));
	}

	error = zvol_os_open_zv(zv, flag, otyp, p);

	mutex_exit(&zv->zv_state_lock);
	return (SET_ERROR(error));
}

int
zvol_os_close_zv(zvol_state_t *zv, int flag, int otyp, struct proc *p)
{
	int locks;

	if ((locks = zvol_os_verify_and_lock(zv, TRUE)) == 0)
		return (SET_ERROR(ENOENT));

	ASSERT(MUTEX_HELD(&zv->zv_state_lock));
	ASSERT(zv->zv_open_count != 1 || RW_READ_HELD(&zv->zv_suspend_lock));

	zv->zv_open_count--;

	if (zv->zv_open_count == 0)
		zvol_last_close(zv);

	zvol_os_verify_lock_exit(zv, locks);

	return (0);
}

int
zvol_os_close(dev_t dev, int flag, int otyp, struct proc *p)
{
	zvol_state_t *zv;
	int error = 0;

	if (!getminor(dev))
		return (0);

	zv = zvol_os_find_by_dev(dev);
	if (zv == NULL) {
		return (SET_ERROR(-ENXIO));
	}

	error = zvol_os_close_zv(zv, flag, otyp, p);

	mutex_exit(&zv->zv_state_lock);
	return (0);
}

void
zvol_os_strategy(struct buf *bp)
{

}

int
zvol_os_get_volume_blocksize(dev_t dev)
{
	/* XNU can only handle two sizes. */
	return (DEV_BSIZE);
}

int
zvol_os_ioctl(dev_t dev, unsigned long cmd, caddr_t data, int isblk,
    cred_t *cr, int *rvalp)
{
	int error = 0;
	u_int32_t *f;
	u_int64_t *o;
	zvol_state_t *zv = NULL;

	dprintf("%s\n", __func__);

	if (!getminor(dev))
		return (ENXIO);

	zv = zvol_os_find_by_dev(dev);

	if (zv == NULL) {
		dprintf("zv is NULL\n");
		return (ENXIO);
	}

	f = (u_int32_t *)data;
	o = (u_int64_t *)data;

	switch (cmd) {

		case DKIOCGETMAXBLOCKCOUNTREAD:
			dprintf("DKIOCGETMAXBLOCKCOUNTREAD\n");
			*o = 32;
			break;

		case DKIOCGETMAXBLOCKCOUNTWRITE:
			dprintf("DKIOCGETMAXBLOCKCOUNTWRITE\n");
			*o = 32;
			break;
		case DKIOCGETMAXSEGMENTCOUNTREAD:
			dprintf("DKIOCGETMAXSEGMENTCOUNTREAD\n");
			*o = 32;
			break;

		case DKIOCGETMAXSEGMENTCOUNTWRITE:
			dprintf("DKIOCGETMAXSEGMENTCOUNTWRITE\n");
			*o = 32;
			break;

		case DKIOCGETBLOCKSIZE:
			dprintf("DKIOCGETBLOCKSIZE: %llu\n",
			    zv->zv_volblocksize);
			*f = zv->zv_volblocksize;
			break;

		case DKIOCSETBLOCKSIZE:
			dprintf("DKIOCSETBLOCKSIZE %u\n", (uint32_t)*f);

			if (!isblk) {
				/* We can only do this for a block device */
				error = ENODEV;
				break;
			}

			if (zvol_check_volblocksize(zv->zv_name,
			    (uint64_t)*f)) {
				error = EINVAL;
				break;
			}

			/* set the new block size */
			zv->zv_volblocksize = (uint64_t)*f;
			dprintf("setblocksize changed: %llu\n",
			    zv->zv_volblocksize);
			break;

		case DKIOCISWRITABLE:
			dprintf("DKIOCISWRITABLE\n");
			if (zv && (zv->zv_flags & ZVOL_RDONLY))
				*f = 0;
			else
				*f = 1;
			break;
#ifdef DKIOCGETBLOCKCOUNT32
		case DKIOCGETBLOCKCOUNT32:
			dprintf("DKIOCGETBLOCKCOUNT32: %lu\n",
			    (uint32_t)zv->zv_volsize / zv->zv_volblocksize);
			*f = (uint32_t)zv->zv_volsize / zv->zv_volblocksize;
			break;
#endif

		case DKIOCGETBLOCKCOUNT:
			dprintf("DKIOCGETBLOCKCOUNT: %llu\n",
			    zv->zv_volsize / zv->zv_volblocksize);
			*o = (uint64_t)zv->zv_volsize / zv->zv_volblocksize;
			break;

		case DKIOCGETBASE:
			dprintf("DKIOCGETBASE\n");
			/*
			 * What offset should we say?
			 * 0 is ok for FAT but to HFS
			 */
			*o = zv->zv_volblocksize * 0;
			break;

		case DKIOCGETPHYSICALBLOCKSIZE:
			dprintf("DKIOCGETPHYSICALBLOCKSIZE\n");
			*f = zv->zv_volblocksize;
			break;

#ifdef DKIOCGETTHROTTLEMASK
		case DKIOCGETTHROTTLEMASK:
			dprintf("DKIOCGETTHROTTLEMASK\n");
			*o = 0;
			break;
#endif

		case DKIOCGETMAXBYTECOUNTREAD:
			*o = SPA_MAXBLOCKSIZE;
			break;

		case DKIOCGETMAXBYTECOUNTWRITE:
			*o = SPA_MAXBLOCKSIZE;
			break;
#ifdef DKIOCUNMAP
		case DKIOCUNMAP:
			dprintf("DKIOCUNMAP\n");
			*f = 1;
			break;
#endif

		case DKIOCGETFEATURES:
			*f = 0;
			break;

#ifdef DKIOCISSOLIDSTATE
		case DKIOCISSOLIDSTATE:
			dprintf("DKIOCISSOLIDSTATE\n");
			*f = 0;
			break;
#endif

		case DKIOCISVIRTUAL:
			*f = 1;
			break;

		case DKIOCGETMAXSEGMENTBYTECOUNTREAD:
			*o = 32 * zv->zv_volblocksize;
			break;

		case DKIOCGETMAXSEGMENTBYTECOUNTWRITE:
			*o = 32 * zv->zv_volblocksize;
			break;

		case DKIOCSYNCHRONIZECACHE:
			dprintf("DKIOCSYNCHRONIZECACHE\n");
			break;

		default:
			dprintf("unknown ioctl: ENOTTY\n");
			error = ENOTTY;
			break;
	}

	mutex_exit(&zv->zv_state_lock);

	return (SET_ERROR(error));
}

int
zvol_init(void)
{
	int threads = MIN(MAX(zvol_threads, 1), 1024);

	zvol_taskq = taskq_create(ZVOL_DRIVER, threads, maxclsyspri-4,
	    threads * 2, INT_MAX, TASKQ_PREPOPULATE);
	if (zvol_taskq == NULL) {
		return (-ENOMEM);
	}

	zvol_init_impl();
	return (0);
}

void
zvol_fini(void)
{
	zvol_fini_impl();
	taskq_destroy(zvol_taskq);
}



/*
 * Due to OS X limitations in /dev, we create a symlink for "/dev/zvol" to
 * "/var/run/zfs" (if we can) and for each pool, create the traditional
 * ZFS Volume symlinks.
 *
 * Ie, for ZVOL $POOL/$VOLUME
 * BSDName /dev/disk2 /dev/rdisk2
 * /dev/zvol -> /var/run/zfs
 * /var/run/zfs/zvol/dsk/$POOL/$VOLUME -> /dev/disk2
 * /var/run/zfs/zvol/rdsk/$POOL/$VOLUME -> /dev/rdisk2
 *
 * Note, we do not create symlinks for the partitioned slices.
 *
 */

void
zvol_add_symlink(zvol_state_t *zv, const char *bsd_disk, const char *bsd_rdisk)
{
	zfs_ereport_zvol_post(FM_RESOURCE_ZVOL_CREATE_SYMLINK,
	    zv->zv_name, bsd_disk, bsd_rdisk);
}


void
zvol_remove_symlink(zvol_state_t *zv)
{
	if (!zv || !zv->zv_name[0])
		return;

	zfs_ereport_zvol_post(FM_RESOURCE_ZVOL_REMOVE_SYMLINK,
	    zv->zv_name, &zv->zv_zso->zvo_bsdname[1],
	    zv->zv_zso->zvo_bsdname);
}
