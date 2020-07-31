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
 * Portions Copyright 2022 Andrew Innes <andrew.c12@gmail.com>
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
#include <sys/zvol_os.h>

static uint32_t zvol_major = ZVOL_MAJOR;

unsigned int zvol_request_sync = 0;
unsigned int zvol_prefetch_bytes = (128 * 1024);
unsigned long zvol_max_discard_blocks = 16384;
unsigned int zvol_threads = 32;

taskq_t *zvol_taskq;

extern void wzvol_clear_targetid(uint8_t targetid, uint8_t lun,
    zvol_state_t *zv);
extern void wzvol_announce_buschange(void);
extern int wzvol_assign_targetid(zvol_state_t *zv);
extern list_t zvol_state_list;

_Atomic uint64_t spl_lowest_zvol_stack_remaining = 0;

typedef struct zv_request {
	zvol_state_t *zv;

	void (*zv_func)(void *);
	void *zv_arg;

	taskq_ent_t	ent;
} zv_request_t;

#define	ZVOL_LOCK_HELD		(1<<0)
#define	ZVOL_LOCK_SPA		(1<<1)
#define	ZVOL_LOCK_SUSPEND	(1<<2)

static void
zvol_os_spawn_cb(void *param)
{
	zv_request_t *zvr = (zv_request_t *)param;

	zvr->zv_func(zvr->zv_arg);

	kmem_free(zvr, sizeof (zv_request_t));
}

static void
zvol_os_spawn(void (*func)(void *), void *arg)
{
	zv_request_t *zvr;
	zvr = kmem_alloc(sizeof (zv_request_t), KM_SLEEP);
	zvr->zv_arg = arg;
	zvr->zv_func = func;

	taskq_init_ent(&zvr->ent);

	taskq_dispatch_ent(zvol_taskq,
	    zvol_os_spawn_cb, zvr, 0, &zvr->ent);
}

/*
 * Given a path, return TRUE if path is a ZVOL.
 * Implement me when it is time for zpools-is-zvols.
 * Windows.
 * Returning FALSE makes caller process everything async,
 * which will deadlock if zpool-in-zvol exists.
 * Returning TRUE goes into slow, but safe, path.
 */
static boolean_t
zvol_os_is_zvol(const char *device)
{
	return (B_FALSE);
}

/*
 * Make sure zv is still in the list (not freed) and if it is
 * grab the locks in the correct order.
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
zvol_os_register_device_cb(void *param)
{
	zvol_state_t *zv = (zvol_state_t *)param;
	int locks;

	if ((locks = zvol_os_verify_and_lock(zv, zv->zv_open_count == 0)) == 0)
		return;

	zvol_os_verify_lock_exit(zv, locks);
}

int
zvol_os_write(dev_t dev, zfs_uio_t *uio, int p)
{
	return (ENOTSUP);
}

int
zvol_os_read(dev_t dev, zfs_uio_t *uio, int p)
{
	return (ENOTSUP);
}

int
zvol_os_read_zv(zvol_state_t *zv, zfs_uio_t *uio, int flags)
{
	zfs_locked_range_t *lr;
	int error = 0;
	uint64_t offset = 0;

	const ULONG_PTR r = IoGetRemainingStackSize();

	if (spl_lowest_zvol_stack_remaining == 0) {
		spl_lowest_zvol_stack_remaining = r;
	} else if (spl_lowest_zvol_stack_remaining > r) {
		spl_lowest_zvol_stack_remaining = r;
	}

	if (zv == NULL || zv->zv_dn == NULL)
		return (ENXIO);

	uint64_t volsize = zv->zv_volsize;
	if (zfs_uio_offset(uio) >= volsize)
		return (EIO);

	rw_enter(&zv->zv_suspend_lock, RW_READER);

	lr = zfs_rangelock_enter(&zv->zv_rangelock,
	    zfs_uio_offset(uio), zfs_uio_resid(uio), RL_READER);

	while (zfs_uio_resid(uio) > 0 && zfs_uio_offset(uio) < volsize) {
		uint64_t bytes = MIN(zfs_uio_resid(uio), DMU_MAX_ACCESS >> 1);

		/* don't read past the end */
		if (bytes > volsize - zfs_uio_offset(uio))
			bytes = volsize - zfs_uio_offset(uio);

		TraceEvent(TRACE_VERBOSE, "%s:%d: %s %llu len %llu bytes"
		    " %llu\n", __func__, __LINE__, "zvol_read_iokit: position",
		    zfs_uio_offset(uio), zfs_uio_resid(uio), bytes);

		error = dmu_read_uio_dnode(zv->zv_dn, uio, bytes);

		if (error) {
			/* convert checksum errors into IO errors */
			if (error == ECKSUM)
				error = EIO;
			break;
		}
	}
	zfs_rangelock_exit(lr);

	dataset_kstats_update_read_kstats(&zv->zv_kstat, offset);

	rw_exit(&zv->zv_suspend_lock);
	return (error);
}


int
zvol_os_write_zv(zvol_state_t *zv, zfs_uio_t *uio, int flags)
{
	uint64_t volsize;
	zfs_locked_range_t *lr;
	int error = 0;
	boolean_t sync;
	uint64_t offset = 0;
	uint64_t bytes = 0;
	uint64_t off;

	const ULONG_PTR r = IoGetRemainingStackSize();

	if (spl_lowest_zvol_stack_remaining == 0) {
		spl_lowest_zvol_stack_remaining = r;
	} else if (spl_lowest_zvol_stack_remaining > r) {
		spl_lowest_zvol_stack_remaining = r;
	}


	if (zv == NULL)
		return (ENXIO);

	/* Some requests are just for flush and nothing else. */
	if (zfs_uio_resid(uio) == 0)
		return (0);

	volsize = zv->zv_volsize;
	if (zfs_uio_offset(uio) >= volsize)
		return (EIO);

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

	TraceEvent(TRACE_VERBOSE, "%s:%d: zvol_write_iokit(offset "
	    "0x%llx bytes 0x%llx)\n", __func__, __LINE__,
	    zfs_uio_offset(uio), zfs_uio_resid(uio), bytes);

	sync = (zv->zv_objset->os_sync == ZFS_SYNC_ALWAYS);

	/* Lock the entire range */
	lr = zfs_rangelock_enter(&zv->zv_rangelock, zfs_uio_offset(uio),
	    zfs_uio_resid(uio), RL_WRITER);

	/* Iterate over (DMU_MAX_ACCESS/2) segments */
	while (zfs_uio_resid(uio) > 0 && zfs_uio_offset(uio) < volsize) {
		uint64_t bytes = MIN(zfs_uio_resid(uio), DMU_MAX_ACCESS >> 1);
		uint64_t off = zfs_uio_offset(uio);
		dmu_tx_t *tx = dmu_tx_create(zv->zv_objset);

		/* don't write past the end */
		if (bytes > volsize - off)
			bytes = volsize - off;

		dmu_tx_hold_write_by_dnode(tx, zv->zv_dn, off, bytes);
		error = dmu_tx_assign(tx, TXG_WAIT);
		if (error) {
			dmu_tx_abort(tx);
			break;
		}

		error = dmu_write_uio_dnode(zv->zv_dn, uio,
		    bytes, tx);

		if (error == 0) {
			zvol_log_write(zv, tx, offset, bytes, sync);
		}
		dmu_tx_commit(tx);

		if (error)
			break;
	}
	zfs_rangelock_exit(lr);

	dataset_kstats_update_write_kstats(&zv->zv_kstat, offset);

	if (sync)
		zil_commit(zv->zv_zilog, ZVOL_OBJ);

	rw_exit(&zv->zv_suspend_lock);

	return (error);
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
	 * This is APPLE, check if Windows does the same.
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
zvol_os_clear_private(zvol_state_t *zv)
{
#if 0
	// Close the Storport half open
	if (zv->zv_open_count == 0) {
		wzvol_clear_targetid(zv->zv_zso->zso_target_id,
		    zv->zv_zso->zso_lun_id, zv);
		wzvol_announce_buschange();
	}
#endif
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

	dprintf("%s\n", __func__);

	rw_enter(&zvol_state_lock, RW_READER);
	for (zv = list_head(&zvol_state_list); zv != NULL;
	    zv = list_next(&zvol_state_list, zv)) {
		mutex_enter(&zv->zv_state_lock);
		if (zv->zv_zso->zso_dev == dev) {
			rw_exit(&zvol_state_lock);
			return (zv);
		}
		mutex_exit(&zv->zv_state_lock);
	}
	rw_exit(&zvol_state_lock);

	return (NULL);
}

zvol_state_t *
zvol_os_targetlun_lookup(uint8_t target, uint8_t lun)
{
	zvol_state_t *zv;

	dprintf("%s\n", __func__);

	rw_enter(&zvol_state_lock, RW_READER);
	for (zv = list_head(&zvol_state_list); zv != NULL;
	    zv = list_next(&zvol_state_list, zv)) {
		mutex_enter(&zv->zv_state_lock);
		if (zv->zv_zso->zso_target_id == target &&
		    zv->zv_zso->zso_lun_id == lun) {
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
	ASSERT3U(minor(zv->zv_zso->zso_dev) & ZVOL_MINOR_MASK, ==, 0);
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
static void
zvol_os_free(zvol_state_t *zv)
{
	dprintf("%s\n", __func__);
#if 0
	rw_enter(&zv->zv_suspend_lock, RW_READER);
	mutex_enter(&zv->zv_state_lock);
	zv->zv_zso->zso_open_count--;
	zv->zv_open_count++;
	zvol_last_close(zv);
	zv->zv_open_count--;
	mutex_exit(&zv->zv_state_lock);
	rw_exit(&zv->zv_suspend_lock);
#endif

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
zvol_os_attach(char *name)
{
	zvol_state_t *zv;
	uint64_t hash = zvol_name_hash(name);
	int error = 0;

	dprintf("%s\n", __func__);

	zv = zvol_find_by_name_hash(name, hash, RW_NONE);
	if (zv != NULL) {
		mutex_exit(&zv->zv_state_lock);
		error = zvol_os_open_zv(zv, zv->zv_flags & ZVOL_RDONLY ?
		    FREAD : FWRITE, 0, NULL); // readonly?
		// Assign new TargetId and Lun
		if (error == 0) {
			wzvol_assign_targetid(zv);
			wzvol_announce_buschange();
		}
	}
}

void
zvol_os_detach_zv(zvol_state_t *zv)
{
	if (zv != NULL) {
		wzvol_clear_targetid(zv->zv_zso->zso_target_id,
		    zv->zv_zso->zso_lun_id, zv);
		/* Last close needs suspect lock, give it a try */
		if (zv->zv_open_count == 1) {
			if (rw_tryenter(&zv->zv_suspend_lock, RW_READER)) {
				zvol_last_close(zv);
				zv->zv_open_count--;
				rw_exit(&zv->zv_suspend_lock);
			}
		}
		wzvol_announce_buschange();
	}
}

void
zvol_os_detach(char *name)
{
	zvol_state_t *zv;
	uint64_t hash = zvol_name_hash(name);

	dprintf("%s\n", __func__);

	zv = zvol_find_by_name_hash(name, hash, RW_NONE);
	if (zv != NULL) {
		zvol_os_detach_zv(zv);
		mutex_exit(&zv->zv_state_lock);
		wzvol_announce_buschange();
	}
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
	unsigned minor = 0;
	int error = 0;
	uint64_t hash = zvol_name_hash(name);
	boolean_t replayed_zil = B_FALSE;

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
			replayed_zil = zil_destroy(zv->zv_zilog, B_FALSE);
		else
			replayed_zil = zil_replay(os, zv, zvol_replay_vector);
	}
	if (replayed_zil)
		zil_close(zv->zv_zilog);
	zv->zv_zilog = NULL;

	dataset_kstats_create(&zv->zv_kstat, zv->zv_objset);

	rw_enter(&zvol_state_lock, RW_WRITER);
	zvol_insert(zv);
	rw_exit(&zvol_state_lock);

	/*
	 * Here is where we differ to upstream. They will call open and
	 * close, as userland opens the /dev/disk node. Once opened, it
	 * has the open_count, and long_holds held, which is used in
	 * read/write. Once closed, everything is released. So when it
	 * comes to export/unmount/destroy of the ZVOL, it checks for
	 * opencount==0 and longholds==0. They should allow for == 1
	 * for Windows.
	 * However, in Windows there is no open/close devnode, but rather,
	 * we assign the zvol to the storport API, to expose the device.
	 * Really, the zvol needs to be "open" the entire time that storport
	 * has it. If we leave zvol "open" it will fail the checks for "==0".
	 * So we steal an opencount, and remember it privately. We could also
	 * change zvol.c to special-case it for Windows.
	 */
#if 0
	if (error == 0) {
		error = dnode_hold(os, ZVOL_OBJ, FTAG, &zv->zv_zso->zso_dn);
		if (error == 0) {
			zfs_rangelock_init(&zv->zv_zso->zso_rangelock, NULL,
			    NULL);
			wzvol_announce_buschange();
		}
	//	zvol_os_close_zv(zv, FWRITE, 0, NULL);
	}
//	error = dmu_objset_own(zv->zv_name, DMU_OST_ZVOL, ro, B_TRUE, zv, &os);

	if (error == 0) {
		// We keep zv_objset / dmu_objset_own() open for storport
		goto out_doi;
	}
#endif

	// About to disown
	zv->zv_objset = NULL;
out_dmu_objset_disown:
	dmu_objset_disown(os, B_TRUE, FTAG);
out_doi:
	kmem_free(doi, sizeof (dmu_object_info_t));

	if (error == 0) {
		zvol_os_attach(name);
	}
	dprintf("%s complete\n", __func__);
	return (error);
}


static void zvol_os_rename_device_cb(void *param)
{
	zvol_state_t *zv = (zvol_state_t *)param;
	int locks;
	if ((locks = zvol_os_verify_and_lock(zv, zv->zv_open_count == 0)) == 0)
		return;

	zvol_os_verify_lock_exit(zv, locks);
//	zvolRenameDevice(zv);
}

static void
zvol_os_rename_minor(zvol_state_t *zv, const char *newname)
{
	// int readonly = get_disk_ro(zv->zv_zso->zvo_disk);

	ASSERT(RW_LOCK_HELD(&zvol_state_lock));
	ASSERT(MUTEX_HELD(&zv->zv_state_lock));

	strlcpy(zv->zv_name, newname, sizeof (zv->zv_name));

	/* move to new hashtable entry  */
	zv->zv_hash = zvol_name_hash(zv->zv_name);
	hlist_del(&zv->zv_hlink);
	hlist_add_head(&zv->zv_hlink, ZVOL_HT_HEAD(zv->zv_hash));

	zvol_os_spawn(zvol_os_rename_device_cb, zv);

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

static void
zvol_os_set_disk_ro(zvol_state_t *zv, int flags)
{
	// set_disk_ro(zv->zv_zso->zvo_disk, flags);
}

static void
zvol_os_set_capacity(zvol_state_t *zv, uint64_t capacity)
{
	// set_capacity(zv->zv_zso->zvo_disk, capacity);
}

int
zvol_os_open_zv(zvol_state_t *zv, int flag, int otyp, struct proc *p)
{
	int error = 0;
	int locks;

	dprintf("%s\n", __func__);

	/*
	 * make sure zvol is not suspended during first open
	 * (hold zv_suspend_lock) and respect proper lock acquisition
	 * ordering - zv_suspend_lock before zv_state_lock
	 */
	if ((locks = zvol_os_verify_and_lock(zv, zv->zv_open_count == 0))
	    == 0)
		return (SET_ERROR(ENOENT));

	ASSERT(MUTEX_HELD(&zv->zv_state_lock));
	ASSERT(zv->zv_open_count != 0 || RW_READ_HELD(&zv->zv_suspend_lock));

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

	dprintf("%s\n", __func__);

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
	dprintf("%s\n", __func__);

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

	dprintf("%s\n", __func__);

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
	zvol_state_t *zv = NULL;

	dprintf("%s\n", __func__);

	if (!getminor(dev))
		return (ENXIO);

	zv = zvol_os_find_by_dev(dev);

	if (zv == NULL) {
		dprintf("zv is NULL\n");
		return (ENXIO);
	}

	//

	mutex_exit(&zv->zv_state_lock);

	return (SET_ERROR(error));
}

int
zvol_init(void)
{
	int threads = MIN(MAX(zvol_threads, 1), 1024);

	zvol_taskq = taskq_create(ZVOL_DRIVER, threads, maxclsyspri,
	    threads * 2, INT_MAX, TASKQ_PREPOPULATE | TASKQ_DYNAMIC);
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

/* ZFS ZVOLDI */

_Function_class_(PINTERFACE_REFERENCE) void
IncZvolRef(PVOID Context)
{
	zvol_state_t *zv = (zvol_state_t *)Context;
	atomic_inc_32(&zv->zv_open_count);
}

_Function_class_(PINTERFACE_REFERENCE) void
DecZvolRef(PVOID Context)
{
	zvol_state_t *zv = (zvol_state_t *)Context;
	atomic_dec_32(&zv->zv_open_count);
}

zvol_state_t *
zvol_name2zvolState(const char *name, uint32_t *openCount)
{
	zvol_state_t *zv;

	zv = zvol_find_by_name(name, RW_NONE);
	if (zv == NULL)
		return (zv);

	if (openCount)
		*openCount = zv->zv_open_count;

	mutex_exit(&zv->zv_state_lock);
	return (zv);
}
