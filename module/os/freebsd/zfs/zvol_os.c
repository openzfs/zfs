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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 *
 * Copyright (c) 2006-2010 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * All rights reserved.
 *
 * Portions Copyright 2010 Robert Milkowski
 *
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2012, 2017 by Delphix. All rights reserved.
 * Copyright (c) 2013, Joyent, Inc. All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 */

/* Portions Copyright 2011 Martin Matuska <mm@FreeBSD.org> */

/*
 * ZFS volume emulation driver.
 *
 * Makes a DMU object look like a volume of arbitrary size, up to 2^64 bytes.
 * Volumes are accessed through the symbolic links named:
 *
 * /dev/zvol/<pool_name>/<dataset_name>
 *
 * Volumes are persistent through reboot.  No user command needs to be
 * run before opening and using a device.
 *
 * On FreeBSD ZVOLs are simply GEOM providers like any other storage device
 * in the system. Except when they're simply character devices (volmode=dev).
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/errno.h>
#include <sys/uio.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/kmem.h>
#include <sys/conf.h>
#include <sys/cmn_err.h>
#include <sys/stat.h>
#include <sys/proc.h>
#include <sys/zap.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/zio.h>
#include <sys/disk.h>
#include <sys/dmu_traverse.h>
#include <sys/dnode.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_dir.h>
#include <sys/byteorder.h>
#include <sys/sunddi.h>
#include <sys/dirent.h>
#include <sys/policy.h>
#include <sys/queue.h>
#include <sys/fs/zfs.h>
#include <sys/zfs_ioctl.h>
#include <sys/zil.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_rlock.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_raidz.h>
#include <sys/zvol.h>
#include <sys/zil_impl.h>
#include <sys/dataset_kstats.h>
#include <sys/dbuf.h>
#include <sys/dmu_tx.h>
#include <sys/zfeature.h>
#include <sys/zio_checksum.h>
#include <sys/zil_impl.h>
#include <sys/filio.h>
#include <sys/freebsd_event.h>

#include <geom/geom.h>
#include <sys/zvol.h>
#include <sys/zvol_impl.h>

#include "zfs_namecheck.h"

#define	ZVOL_DUMPSIZE		"dumpsize"

#ifdef ZVOL_LOCK_DEBUG
#define	ZVOL_RW_READER		RW_WRITER
#define	ZVOL_RW_READ_HELD	RW_WRITE_HELD
#else
#define	ZVOL_RW_READER		RW_READER
#define	ZVOL_RW_READ_HELD	RW_READ_HELD
#endif

enum zvol_geom_state {
	ZVOL_GEOM_UNINIT,
	ZVOL_GEOM_STOPPED,
	ZVOL_GEOM_RUNNING,
};

struct zvol_state_os {
#define	zso_dev		_zso_state._zso_dev
#define	zso_geom	_zso_state._zso_geom
	union {
		/* volmode=dev */
		struct zvol_state_dev {
			struct cdev *zsd_cdev;
			uint64_t zsd_sync_cnt;
			struct selinfo zsd_selinfo;
		} _zso_dev;

		/* volmode=geom */
		struct zvol_state_geom {
			struct g_provider *zsg_provider;
			struct bio_queue_head zsg_queue;
			struct mtx zsg_queue_mtx;
			enum zvol_geom_state zsg_state;
		} _zso_geom;
	} _zso_state;
	int zso_dying;
};

static uint32_t zvol_minors;

SYSCTL_DECL(_vfs_zfs);
SYSCTL_NODE(_vfs_zfs, OID_AUTO, vol, CTLFLAG_RW, 0, "ZFS VOLUME");
SYSCTL_INT(_vfs_zfs_vol, OID_AUTO, mode, CTLFLAG_RWTUN, &zvol_volmode, 0,
	"Expose as GEOM providers (1), device files (2) or neither");
static boolean_t zpool_on_zvol = B_FALSE;
SYSCTL_INT(_vfs_zfs_vol, OID_AUTO, recursive, CTLFLAG_RWTUN, &zpool_on_zvol, 0,
	"Allow zpools to use zvols as vdevs (DANGEROUS)");

/*
 * Toggle unmap functionality.
 */
boolean_t zvol_unmap_enabled = B_TRUE;

SYSCTL_INT(_vfs_zfs_vol, OID_AUTO, unmap_enabled, CTLFLAG_RWTUN,
	&zvol_unmap_enabled, 0, "Enable UNMAP functionality");

/*
 * zvol maximum transfer in one DMU tx.
 */
int zvol_maxphys = DMU_MAX_ACCESS / 2;

static void zvol_ensure_zilog(zvol_state_t *zv);

static d_open_t		zvol_cdev_open;
static d_close_t	zvol_cdev_close;
static d_ioctl_t	zvol_cdev_ioctl;
static d_read_t		zvol_cdev_read;
static d_write_t	zvol_cdev_write;
static d_strategy_t	zvol_geom_bio_strategy;
static d_kqfilter_t	zvol_cdev_kqfilter;

static struct cdevsw zvol_cdevsw = {
	.d_name =	"zvol",
	.d_version =	D_VERSION,
	.d_flags =	D_DISK | D_TRACKCLOSE,
	.d_open =	zvol_cdev_open,
	.d_close =	zvol_cdev_close,
	.d_ioctl =	zvol_cdev_ioctl,
	.d_read =	zvol_cdev_read,
	.d_write =	zvol_cdev_write,
	.d_strategy =	zvol_geom_bio_strategy,
	.d_kqfilter =	zvol_cdev_kqfilter,
};

static void		zvol_filter_detach(struct knote *kn);
static int		zvol_filter_vnode(struct knote *kn, long hint);

static struct filterops zvol_filterops_vnode = {
	.f_isfd = 1,
	.f_detach = zvol_filter_detach,
	.f_event = zvol_filter_vnode,
};

extern uint_t zfs_geom_probe_vdev_key;

struct g_class zfs_zvol_class = {
	.name = "ZFS::ZVOL",
	.version = G_VERSION,
};

DECLARE_GEOM_CLASS(zfs_zvol_class, zfs_zvol);

static int zvol_geom_open(struct g_provider *pp, int flag, int count);
static int zvol_geom_close(struct g_provider *pp, int flag, int count);
static void zvol_geom_run(zvol_state_t *zv);
static void zvol_geom_destroy(zvol_state_t *zv);
static int zvol_geom_access(struct g_provider *pp, int acr, int acw, int ace);
static void zvol_geom_worker(void *arg);
static void zvol_geom_bio_start(struct bio *bp);
static int zvol_geom_bio_getattr(struct bio *bp);
/* static d_strategy_t	zvol_geom_bio_strategy; (declared elsewhere) */

/*
 * GEOM mode implementation
 */

static int
zvol_geom_open(struct g_provider *pp, int flag, int count)
{
	zvol_state_t *zv;
	int err = 0;
	boolean_t drop_suspend = B_FALSE;

	if (!zpool_on_zvol && tsd_get(zfs_geom_probe_vdev_key) != NULL) {
		/*
		 * If zfs_geom_probe_vdev_key is set, that means that zfs is
		 * attempting to probe geom providers while looking for a
		 * replacement for a missing VDEV.  In this case, the
		 * spa_namespace_lock will not be held, but it is still illegal
		 * to use a zvol as a vdev.  Deadlocks can result if another
		 * thread has spa_namespace_lock.
		 */
		return (SET_ERROR(EOPNOTSUPP));
	}

retry:
	rw_enter(&zvol_state_lock, ZVOL_RW_READER);
	/*
	 * Obtain a copy of private under zvol_state_lock to make sure either
	 * the result of zvol free code setting private to NULL is observed,
	 * or the zv is protected from being freed because of the positive
	 * zv_open_count.
	 */
	zv = pp->private;
	if (zv == NULL) {
		rw_exit(&zvol_state_lock);
		err = SET_ERROR(ENXIO);
		goto out_locked;
	}

	mutex_enter(&zv->zv_state_lock);
	if (zv->zv_zso->zso_dying) {
		rw_exit(&zvol_state_lock);
		err = SET_ERROR(ENXIO);
		goto out_zv_locked;
	}
	ASSERT3S(zv->zv_volmode, ==, ZFS_VOLMODE_GEOM);

	/*
	 * Make sure zvol is not suspended during first open
	 * (hold zv_suspend_lock) and respect proper lock acquisition
	 * ordering - zv_suspend_lock before zv_state_lock.
	 */
	if (zv->zv_open_count == 0) {
		drop_suspend = B_TRUE;
		if (!rw_tryenter(&zv->zv_suspend_lock, ZVOL_RW_READER)) {
			mutex_exit(&zv->zv_state_lock);
			rw_enter(&zv->zv_suspend_lock, ZVOL_RW_READER);
			mutex_enter(&zv->zv_state_lock);
			/* Check to see if zv_suspend_lock is needed. */
			if (zv->zv_open_count != 0) {
				rw_exit(&zv->zv_suspend_lock);
				drop_suspend = B_FALSE;
			}
		}
	}
	rw_exit(&zvol_state_lock);

	ASSERT(MUTEX_HELD(&zv->zv_state_lock));

	if (zv->zv_open_count == 0) {
		boolean_t drop_namespace = B_FALSE;

		ASSERT(ZVOL_RW_READ_HELD(&zv->zv_suspend_lock));

		/*
		 * Take spa_namespace_lock to prevent lock inversion when
		 * zvols from one pool are opened as vdevs in another.
		 */
		if (!mutex_owned(&spa_namespace_lock)) {
			if (!mutex_tryenter(&spa_namespace_lock)) {
				mutex_exit(&zv->zv_state_lock);
				rw_exit(&zv->zv_suspend_lock);
				kern_yield(PRI_USER);
				goto retry;
			} else {
				drop_namespace = B_TRUE;
			}
		}
		err = zvol_first_open(zv, !(flag & FWRITE));
		if (drop_namespace)
			mutex_exit(&spa_namespace_lock);
		if (err)
			goto out_zv_locked;
		pp->mediasize = zv->zv_volsize;
		pp->stripeoffset = 0;
		pp->stripesize = zv->zv_volblocksize;
	}

	ASSERT(MUTEX_HELD(&zv->zv_state_lock));

	/*
	 * Check for a bad on-disk format version now since we
	 * lied about owning the dataset readonly before.
	 */
	if ((flag & FWRITE) && ((zv->zv_flags & ZVOL_RDONLY) ||
	    dmu_objset_incompatible_encryption_version(zv->zv_objset))) {
		err = SET_ERROR(EROFS);
		goto out_opened;
	}
	if (zv->zv_flags & ZVOL_EXCL) {
		err = SET_ERROR(EBUSY);
		goto out_opened;
	}
	if (flag & O_EXCL) {
		if (zv->zv_open_count != 0) {
			err = SET_ERROR(EBUSY);
			goto out_opened;
		}
		zv->zv_flags |= ZVOL_EXCL;
	}

	zv->zv_open_count += count;
out_opened:
	if (zv->zv_open_count == 0) {
		zvol_last_close(zv);
		wakeup(zv);
	}
out_zv_locked:
	mutex_exit(&zv->zv_state_lock);
out_locked:
	if (drop_suspend)
		rw_exit(&zv->zv_suspend_lock);
	return (err);
}

static int
zvol_geom_close(struct g_provider *pp, int flag, int count)
{
	(void) flag;
	zvol_state_t *zv;
	boolean_t drop_suspend = B_TRUE;
	int new_open_count;

	rw_enter(&zvol_state_lock, ZVOL_RW_READER);
	zv = pp->private;
	if (zv == NULL) {
		rw_exit(&zvol_state_lock);
		return (SET_ERROR(ENXIO));
	}

	mutex_enter(&zv->zv_state_lock);
	if (zv->zv_flags & ZVOL_EXCL) {
		ASSERT3U(zv->zv_open_count, ==, 1);
		zv->zv_flags &= ~ZVOL_EXCL;
	}

	ASSERT3S(zv->zv_volmode, ==, ZFS_VOLMODE_GEOM);

	/*
	 * If the open count is zero, this is a spurious close.
	 * That indicates a bug in the kernel / DDI framework.
	 */
	ASSERT3U(zv->zv_open_count, >, 0);

	/*
	 * Make sure zvol is not suspended during last close
	 * (hold zv_suspend_lock) and respect proper lock acquisition
	 * ordering - zv_suspend_lock before zv_state_lock.
	 */
	new_open_count = zv->zv_open_count - count;
	if (new_open_count == 0) {
		if (!rw_tryenter(&zv->zv_suspend_lock, ZVOL_RW_READER)) {
			mutex_exit(&zv->zv_state_lock);
			rw_enter(&zv->zv_suspend_lock, ZVOL_RW_READER);
			mutex_enter(&zv->zv_state_lock);
			/* Check to see if zv_suspend_lock is needed. */
			new_open_count = zv->zv_open_count - count;
			if (new_open_count != 0) {
				rw_exit(&zv->zv_suspend_lock);
				drop_suspend = B_FALSE;
			}
		}
	} else {
		drop_suspend = B_FALSE;
	}
	rw_exit(&zvol_state_lock);

	ASSERT(MUTEX_HELD(&zv->zv_state_lock));

	/*
	 * You may get multiple opens, but only one close.
	 */
	zv->zv_open_count = new_open_count;
	if (zv->zv_open_count == 0) {
		ASSERT(ZVOL_RW_READ_HELD(&zv->zv_suspend_lock));
		zvol_last_close(zv);
		wakeup(zv);
	}

	mutex_exit(&zv->zv_state_lock);

	if (drop_suspend)
		rw_exit(&zv->zv_suspend_lock);
	return (0);
}

static void
zvol_geom_run(zvol_state_t *zv)
{
	struct zvol_state_geom *zsg = &zv->zv_zso->zso_geom;
	struct g_provider *pp = zsg->zsg_provider;

	ASSERT3S(zv->zv_volmode, ==, ZFS_VOLMODE_GEOM);

	g_error_provider(pp, 0);

	kproc_kthread_add(zvol_geom_worker, zv, &system_proc, NULL, 0, 0,
	    "zfskern", "zvol %s", pp->name + sizeof (ZVOL_DRIVER));
}

static void
zvol_geom_destroy(zvol_state_t *zv)
{
	struct zvol_state_geom *zsg = &zv->zv_zso->zso_geom;
	struct g_provider *pp = zsg->zsg_provider;

	ASSERT3S(zv->zv_volmode, ==, ZFS_VOLMODE_GEOM);

	g_topology_assert();

	mutex_enter(&zv->zv_state_lock);
	VERIFY3S(zsg->zsg_state, ==, ZVOL_GEOM_RUNNING);
	mutex_exit(&zv->zv_state_lock);
	zsg->zsg_provider = NULL;
	g_wither_geom(pp->geom, ENXIO);
}

void
zvol_wait_close(zvol_state_t *zv)
{

	if (zv->zv_volmode != ZFS_VOLMODE_GEOM)
		return;
	mutex_enter(&zv->zv_state_lock);
	zv->zv_zso->zso_dying = B_TRUE;

	if (zv->zv_open_count)
		msleep(zv, &zv->zv_state_lock,
		    PRIBIO, "zvol:dying", 10*hz);
	mutex_exit(&zv->zv_state_lock);
}


static int
zvol_geom_access(struct g_provider *pp, int acr, int acw, int ace)
{
	int count, error, flags;

	g_topology_assert();

	/*
	 * To make it easier we expect either open or close, but not both
	 * at the same time.
	 */
	KASSERT((acr >= 0 && acw >= 0 && ace >= 0) ||
	    (acr <= 0 && acw <= 0 && ace <= 0),
	    ("Unsupported access request to %s (acr=%d, acw=%d, ace=%d).",
	    pp->name, acr, acw, ace));

	if (pp->private == NULL) {
		if (acr <= 0 && acw <= 0 && ace <= 0)
			return (0);
		return (pp->error);
	}

	/*
	 * We don't pass FEXCL flag to zvol_geom_open()/zvol_geom_close() if
	 * ace != 0, because GEOM already handles that and handles it a bit
	 * differently. GEOM allows for multiple read/exclusive consumers and
	 * ZFS allows only one exclusive consumer, no matter if it is reader or
	 * writer. I like better the way GEOM works so I'll leave it for GEOM
	 * to decide what to do.
	 */

	count = acr + acw + ace;
	if (count == 0)
		return (0);

	flags = 0;
	if (acr != 0 || ace != 0)
		flags |= FREAD;
	if (acw != 0)
		flags |= FWRITE;

	g_topology_unlock();
	if (count > 0)
		error = zvol_geom_open(pp, flags, count);
	else
		error = zvol_geom_close(pp, flags, -count);
	g_topology_lock();
	return (error);
}

static void
zvol_geom_worker(void *arg)
{
	zvol_state_t *zv = arg;
	struct zvol_state_geom *zsg = &zv->zv_zso->zso_geom;
	struct bio *bp;

	ASSERT3S(zv->zv_volmode, ==, ZFS_VOLMODE_GEOM);

	thread_lock(curthread);
	sched_prio(curthread, PRIBIO);
	thread_unlock(curthread);

	for (;;) {
		mtx_lock(&zsg->zsg_queue_mtx);
		bp = bioq_takefirst(&zsg->zsg_queue);
		if (bp == NULL) {
			if (zsg->zsg_state == ZVOL_GEOM_STOPPED) {
				zsg->zsg_state = ZVOL_GEOM_RUNNING;
				wakeup(&zsg->zsg_state);
				mtx_unlock(&zsg->zsg_queue_mtx);
				kthread_exit();
			}
			msleep(&zsg->zsg_queue, &zsg->zsg_queue_mtx,
			    PRIBIO | PDROP, "zvol:io", 0);
			continue;
		}
		mtx_unlock(&zsg->zsg_queue_mtx);
		zvol_geom_bio_strategy(bp);
	}
}

static void
zvol_geom_bio_start(struct bio *bp)
{
	zvol_state_t *zv = bp->bio_to->private;
	struct zvol_state_geom *zsg;
	boolean_t first;

	if (zv == NULL) {
		g_io_deliver(bp, ENXIO);
		return;
	}
	if (bp->bio_cmd == BIO_GETATTR) {
		if (zvol_geom_bio_getattr(bp))
			g_io_deliver(bp, EOPNOTSUPP);
		return;
	}

	if (!THREAD_CAN_SLEEP()) {
		zsg = &zv->zv_zso->zso_geom;
		mtx_lock(&zsg->zsg_queue_mtx);
		first = (bioq_first(&zsg->zsg_queue) == NULL);
		bioq_insert_tail(&zsg->zsg_queue, bp);
		mtx_unlock(&zsg->zsg_queue_mtx);
		if (first)
			wakeup_one(&zsg->zsg_queue);
		return;
	}

	zvol_geom_bio_strategy(bp);
}

static int
zvol_geom_bio_getattr(struct bio *bp)
{
	zvol_state_t *zv;

	zv = bp->bio_to->private;
	ASSERT3P(zv, !=, NULL);

	spa_t *spa = dmu_objset_spa(zv->zv_objset);
	uint64_t refd, avail, usedobjs, availobjs;

	if (g_handleattr_int(bp, "GEOM::candelete", 1))
		return (0);
	if (strcmp(bp->bio_attribute, "blocksavail") == 0) {
		dmu_objset_space(zv->zv_objset, &refd, &avail,
		    &usedobjs, &availobjs);
		if (g_handleattr_off_t(bp, "blocksavail", avail / DEV_BSIZE))
			return (0);
	} else if (strcmp(bp->bio_attribute, "blocksused") == 0) {
		dmu_objset_space(zv->zv_objset, &refd, &avail,
		    &usedobjs, &availobjs);
		if (g_handleattr_off_t(bp, "blocksused", refd / DEV_BSIZE))
			return (0);
	} else if (strcmp(bp->bio_attribute, "poolblocksavail") == 0) {
		avail = metaslab_class_get_space(spa_normal_class(spa));
		avail -= metaslab_class_get_alloc(spa_normal_class(spa));
		if (g_handleattr_off_t(bp, "poolblocksavail",
		    avail / DEV_BSIZE))
			return (0);
	} else if (strcmp(bp->bio_attribute, "poolblocksused") == 0) {
		refd = metaslab_class_get_alloc(spa_normal_class(spa));
		if (g_handleattr_off_t(bp, "poolblocksused", refd / DEV_BSIZE))
			return (0);
	}
	return (1);
}

static void
zvol_filter_detach(struct knote *kn)
{
	zvol_state_t *zv;
	struct zvol_state_dev *zsd;

	zv = kn->kn_hook;
	zsd = &zv->zv_zso->zso_dev;

	knlist_remove(&zsd->zsd_selinfo.si_note, kn, 0);
}

static int
zvol_filter_vnode(struct knote *kn, long hint)
{
	kn->kn_fflags |= kn->kn_sfflags & hint;

	return (kn->kn_fflags != 0);
}

static int
zvol_cdev_kqfilter(struct cdev *dev, struct knote *kn)
{
	zvol_state_t *zv;
	struct zvol_state_dev *zsd;

	zv = dev->si_drv2;
	zsd = &zv->zv_zso->zso_dev;

	if (kn->kn_filter != EVFILT_VNODE)
		return (EINVAL);

	/* XXX: extend support for other NOTE_* events */
	if (kn->kn_sfflags != NOTE_ATTRIB)
		return (EINVAL);

	kn->kn_fop = &zvol_filterops_vnode;
	kn->kn_hook = zv;
	knlist_add(&zsd->zsd_selinfo.si_note, kn, 0);

	return (0);
}

static void
zvol_geom_bio_strategy(struct bio *bp)
{
	zvol_state_t *zv;
	uint64_t off, volsize;
	size_t resid;
	char *addr;
	objset_t *os;
	zfs_locked_range_t *lr;
	int error = 0;
	boolean_t doread = B_FALSE;
	boolean_t is_dumpified;
	boolean_t sync;

	if (bp->bio_to)
		zv = bp->bio_to->private;
	else
		zv = bp->bio_dev->si_drv2;

	if (zv == NULL) {
		error = SET_ERROR(ENXIO);
		goto out;
	}

	rw_enter(&zv->zv_suspend_lock, ZVOL_RW_READER);

	switch (bp->bio_cmd) {
	case BIO_READ:
		doread = B_TRUE;
		break;
	case BIO_WRITE:
	case BIO_FLUSH:
	case BIO_DELETE:
		if (zv->zv_flags & ZVOL_RDONLY) {
			error = SET_ERROR(EROFS);
			goto resume;
		}
		zvol_ensure_zilog(zv);
		if (bp->bio_cmd == BIO_FLUSH)
			goto sync;
		break;
	default:
		error = SET_ERROR(EOPNOTSUPP);
		goto resume;
	}

	off = bp->bio_offset;
	volsize = zv->zv_volsize;

	os = zv->zv_objset;
	ASSERT3P(os, !=, NULL);

	addr = bp->bio_data;
	resid = bp->bio_length;

	if (resid > 0 && off >= volsize) {
		error = SET_ERROR(EIO);
		goto resume;
	}

	is_dumpified = B_FALSE;
	sync = !doread && !is_dumpified &&
	    zv->zv_objset->os_sync == ZFS_SYNC_ALWAYS;

	/*
	 * There must be no buffer changes when doing a dmu_sync() because
	 * we can't change the data whilst calculating the checksum.
	 */
	lr = zfs_rangelock_enter(&zv->zv_rangelock, off, resid,
	    doread ? RL_READER : RL_WRITER);

	if (bp->bio_cmd == BIO_DELETE) {
		dmu_tx_t *tx = dmu_tx_create(zv->zv_objset);
		error = dmu_tx_assign(tx, TXG_WAIT);
		if (error != 0) {
			dmu_tx_abort(tx);
		} else {
			zvol_log_truncate(zv, tx, off, resid, sync);
			dmu_tx_commit(tx);
			error = dmu_free_long_range(zv->zv_objset, ZVOL_OBJ,
			    off, resid);
			resid = 0;
		}
		goto unlock;
	}
	while (resid != 0 && off < volsize) {
		size_t size = MIN(resid, zvol_maxphys);
		if (doread) {
			error = dmu_read(os, ZVOL_OBJ, off, size, addr,
			    DMU_READ_PREFETCH);
		} else {
			dmu_tx_t *tx = dmu_tx_create(os);
			dmu_tx_hold_write_by_dnode(tx, zv->zv_dn, off, size);
			error = dmu_tx_assign(tx, TXG_WAIT);
			if (error) {
				dmu_tx_abort(tx);
			} else {
				dmu_write(os, ZVOL_OBJ, off, size, addr, tx);
				zvol_log_write(zv, tx, off, size, sync);
				dmu_tx_commit(tx);
			}
		}
		if (error) {
			/* Convert checksum errors into IO errors. */
			if (error == ECKSUM)
				error = SET_ERROR(EIO);
			break;
		}
		off += size;
		addr += size;
		resid -= size;
	}
unlock:
	zfs_rangelock_exit(lr);

	bp->bio_completed = bp->bio_length - resid;
	if (bp->bio_completed < bp->bio_length && off > volsize)
		error = SET_ERROR(EINVAL);

	switch (bp->bio_cmd) {
	case BIO_FLUSH:
		break;
	case BIO_READ:
		dataset_kstats_update_read_kstats(&zv->zv_kstat,
		    bp->bio_completed);
		break;
	case BIO_WRITE:
		dataset_kstats_update_write_kstats(&zv->zv_kstat,
		    bp->bio_completed);
		break;
	case BIO_DELETE:
		break;
	default:
		break;
	}

	if (sync) {
sync:
		zil_commit(zv->zv_zilog, ZVOL_OBJ);
	}
resume:
	rw_exit(&zv->zv_suspend_lock);
out:
	if (bp->bio_to)
		g_io_deliver(bp, error);
	else
		biofinish(bp, NULL, error);
}

/*
 * Character device mode implementation
 */

static int
zvol_cdev_read(struct cdev *dev, struct uio *uio_s, int ioflag)
{
	zvol_state_t *zv;
	uint64_t volsize;
	zfs_locked_range_t *lr;
	int error = 0;
	zfs_uio_t uio;

	zfs_uio_init(&uio, uio_s);

	zv = dev->si_drv2;

	volsize = zv->zv_volsize;
	/*
	 * uio_loffset == volsize isn't an error as
	 * it's required for EOF processing.
	 */
	if (zfs_uio_resid(&uio) > 0 &&
	    (zfs_uio_offset(&uio) < 0 || zfs_uio_offset(&uio) > volsize))
		return (SET_ERROR(EIO));

	rw_enter(&zv->zv_suspend_lock, ZVOL_RW_READER);
	ssize_t start_resid = zfs_uio_resid(&uio);
	lr = zfs_rangelock_enter(&zv->zv_rangelock, zfs_uio_offset(&uio),
	    zfs_uio_resid(&uio), RL_READER);
	while (zfs_uio_resid(&uio) > 0 && zfs_uio_offset(&uio) < volsize) {
		uint64_t bytes = MIN(zfs_uio_resid(&uio), DMU_MAX_ACCESS >> 1);

		/* Don't read past the end. */
		if (bytes > volsize - zfs_uio_offset(&uio))
			bytes = volsize - zfs_uio_offset(&uio);

		error =  dmu_read_uio_dnode(zv->zv_dn, &uio, bytes);
		if (error) {
			/* Convert checksum errors into IO errors. */
			if (error == ECKSUM)
				error = SET_ERROR(EIO);
			break;
		}
	}
	zfs_rangelock_exit(lr);
	int64_t nread = start_resid - zfs_uio_resid(&uio);
	dataset_kstats_update_read_kstats(&zv->zv_kstat, nread);
	rw_exit(&zv->zv_suspend_lock);

	return (error);
}

static int
zvol_cdev_write(struct cdev *dev, struct uio *uio_s, int ioflag)
{
	zvol_state_t *zv;
	uint64_t volsize;
	zfs_locked_range_t *lr;
	int error = 0;
	boolean_t sync;
	zfs_uio_t uio;

	zv = dev->si_drv2;

	volsize = zv->zv_volsize;

	zfs_uio_init(&uio, uio_s);

	if (zfs_uio_resid(&uio) > 0 &&
	    (zfs_uio_offset(&uio) < 0 || zfs_uio_offset(&uio) > volsize))
		return (SET_ERROR(EIO));

	ssize_t start_resid = zfs_uio_resid(&uio);
	sync = (ioflag & IO_SYNC) ||
	    (zv->zv_objset->os_sync == ZFS_SYNC_ALWAYS);

	rw_enter(&zv->zv_suspend_lock, ZVOL_RW_READER);
	zvol_ensure_zilog(zv);

	lr = zfs_rangelock_enter(&zv->zv_rangelock, zfs_uio_offset(&uio),
	    zfs_uio_resid(&uio), RL_WRITER);
	while (zfs_uio_resid(&uio) > 0 && zfs_uio_offset(&uio) < volsize) {
		uint64_t bytes = MIN(zfs_uio_resid(&uio), DMU_MAX_ACCESS >> 1);
		uint64_t off = zfs_uio_offset(&uio);
		dmu_tx_t *tx = dmu_tx_create(zv->zv_objset);

		if (bytes > volsize - off)	/* Don't write past the end. */
			bytes = volsize - off;

		dmu_tx_hold_write_by_dnode(tx, zv->zv_dn, off, bytes);
		error = dmu_tx_assign(tx, TXG_WAIT);
		if (error) {
			dmu_tx_abort(tx);
			break;
		}
		error = dmu_write_uio_dnode(zv->zv_dn, &uio, bytes, tx);
		if (error == 0)
			zvol_log_write(zv, tx, off, bytes, sync);
		dmu_tx_commit(tx);

		if (error)
			break;
	}
	zfs_rangelock_exit(lr);
	int64_t nwritten = start_resid - zfs_uio_resid(&uio);
	dataset_kstats_update_write_kstats(&zv->zv_kstat, nwritten);
	if (sync)
		zil_commit(zv->zv_zilog, ZVOL_OBJ);
	rw_exit(&zv->zv_suspend_lock);
	return (error);
}

static int
zvol_cdev_open(struct cdev *dev, int flags, int fmt, struct thread *td)
{
	zvol_state_t *zv;
	struct zvol_state_dev *zsd;
	int err = 0;
	boolean_t drop_suspend = B_FALSE;

retry:
	rw_enter(&zvol_state_lock, ZVOL_RW_READER);
	/*
	 * Obtain a copy of si_drv2 under zvol_state_lock to make sure either
	 * the result of zvol free code setting si_drv2 to NULL is observed,
	 * or the zv is protected from being freed because of the positive
	 * zv_open_count.
	 */
	zv = dev->si_drv2;
	if (zv == NULL) {
		rw_exit(&zvol_state_lock);
		err = SET_ERROR(ENXIO);
		goto out_locked;
	}

	mutex_enter(&zv->zv_state_lock);
	if (zv->zv_zso->zso_dying) {
		rw_exit(&zvol_state_lock);
		err = SET_ERROR(ENXIO);
		goto out_zv_locked;
	}
	ASSERT3S(zv->zv_volmode, ==, ZFS_VOLMODE_DEV);

	/*
	 * Make sure zvol is not suspended during first open
	 * (hold zv_suspend_lock) and respect proper lock acquisition
	 * ordering - zv_suspend_lock before zv_state_lock.
	 */
	if (zv->zv_open_count == 0) {
		drop_suspend = B_TRUE;
		if (!rw_tryenter(&zv->zv_suspend_lock, ZVOL_RW_READER)) {
			mutex_exit(&zv->zv_state_lock);
			rw_enter(&zv->zv_suspend_lock, ZVOL_RW_READER);
			mutex_enter(&zv->zv_state_lock);
			/* Check to see if zv_suspend_lock is needed. */
			if (zv->zv_open_count != 0) {
				rw_exit(&zv->zv_suspend_lock);
				drop_suspend = B_FALSE;
			}
		}
	}
	rw_exit(&zvol_state_lock);

	ASSERT(MUTEX_HELD(&zv->zv_state_lock));

	if (zv->zv_open_count == 0) {
		boolean_t drop_namespace = B_FALSE;

		ASSERT(ZVOL_RW_READ_HELD(&zv->zv_suspend_lock));

		/*
		 * Take spa_namespace_lock to prevent lock inversion when
		 * zvols from one pool are opened as vdevs in another.
		 */
		if (!mutex_owned(&spa_namespace_lock)) {
			if (!mutex_tryenter(&spa_namespace_lock)) {
				mutex_exit(&zv->zv_state_lock);
				rw_exit(&zv->zv_suspend_lock);
				kern_yield(PRI_USER);
				goto retry;
			} else {
				drop_namespace = B_TRUE;
			}
		}
		err = zvol_first_open(zv, !(flags & FWRITE));
		if (drop_namespace)
			mutex_exit(&spa_namespace_lock);
		if (err)
			goto out_zv_locked;
	}

	ASSERT(MUTEX_HELD(&zv->zv_state_lock));

	if ((flags & FWRITE) && (zv->zv_flags & ZVOL_RDONLY)) {
		err = SET_ERROR(EROFS);
		goto out_opened;
	}
	if (zv->zv_flags & ZVOL_EXCL) {
		err = SET_ERROR(EBUSY);
		goto out_opened;
	}
	if (flags & O_EXCL) {
		if (zv->zv_open_count != 0) {
			err = SET_ERROR(EBUSY);
			goto out_opened;
		}
		zv->zv_flags |= ZVOL_EXCL;
	}

	zv->zv_open_count++;
	if (flags & O_SYNC) {
		zsd = &zv->zv_zso->zso_dev;
		zsd->zsd_sync_cnt++;
		if (zsd->zsd_sync_cnt == 1 &&
		    (zv->zv_flags & ZVOL_WRITTEN_TO) != 0)
			zil_async_to_sync(zv->zv_zilog, ZVOL_OBJ);
	}
out_opened:
	if (zv->zv_open_count == 0) {
		zvol_last_close(zv);
		wakeup(zv);
	}
out_zv_locked:
	mutex_exit(&zv->zv_state_lock);
out_locked:
	if (drop_suspend)
		rw_exit(&zv->zv_suspend_lock);
	return (err);
}

static int
zvol_cdev_close(struct cdev *dev, int flags, int fmt, struct thread *td)
{
	zvol_state_t *zv;
	struct zvol_state_dev *zsd;
	boolean_t drop_suspend = B_TRUE;

	rw_enter(&zvol_state_lock, ZVOL_RW_READER);
	zv = dev->si_drv2;
	if (zv == NULL) {
		rw_exit(&zvol_state_lock);
		return (SET_ERROR(ENXIO));
	}

	mutex_enter(&zv->zv_state_lock);
	if (zv->zv_flags & ZVOL_EXCL) {
		ASSERT3U(zv->zv_open_count, ==, 1);
		zv->zv_flags &= ~ZVOL_EXCL;
	}

	ASSERT3S(zv->zv_volmode, ==, ZFS_VOLMODE_DEV);

	/*
	 * If the open count is zero, this is a spurious close.
	 * That indicates a bug in the kernel / DDI framework.
	 */
	ASSERT3U(zv->zv_open_count, >, 0);
	/*
	 * Make sure zvol is not suspended during last close
	 * (hold zv_suspend_lock) and respect proper lock acquisition
	 * ordering - zv_suspend_lock before zv_state_lock.
	 */
	if (zv->zv_open_count == 1) {
		if (!rw_tryenter(&zv->zv_suspend_lock, ZVOL_RW_READER)) {
			mutex_exit(&zv->zv_state_lock);
			rw_enter(&zv->zv_suspend_lock, ZVOL_RW_READER);
			mutex_enter(&zv->zv_state_lock);
			/* Check to see if zv_suspend_lock is needed. */
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

	/*
	 * You may get multiple opens, but only one close.
	 */
	zv->zv_open_count--;
	if (flags & O_SYNC) {
		zsd = &zv->zv_zso->zso_dev;
		zsd->zsd_sync_cnt--;
	}

	if (zv->zv_open_count == 0) {
		ASSERT(ZVOL_RW_READ_HELD(&zv->zv_suspend_lock));
		zvol_last_close(zv);
		wakeup(zv);
	}

	mutex_exit(&zv->zv_state_lock);

	if (drop_suspend)
		rw_exit(&zv->zv_suspend_lock);
	return (0);
}

static int
zvol_cdev_ioctl(struct cdev *dev, ulong_t cmd, caddr_t data,
    int fflag, struct thread *td)
{
	zvol_state_t *zv;
	zfs_locked_range_t *lr;
	off_t offset, length;
	int error;
	boolean_t sync;

	zv = dev->si_drv2;

	error = 0;
	KASSERT(zv->zv_open_count > 0,
	    ("Device with zero access count in %s", __func__));

	switch (cmd) {
	case DIOCGSECTORSIZE:
		*(uint32_t *)data = DEV_BSIZE;
		break;
	case DIOCGMEDIASIZE:
		*(off_t *)data = zv->zv_volsize;
		break;
	case DIOCGFLUSH:
		rw_enter(&zv->zv_suspend_lock, ZVOL_RW_READER);
		if (zv->zv_zilog != NULL)
			zil_commit(zv->zv_zilog, ZVOL_OBJ);
		rw_exit(&zv->zv_suspend_lock);
		break;
	case DIOCGDELETE:
		if (!zvol_unmap_enabled)
			break;

		offset = ((off_t *)data)[0];
		length = ((off_t *)data)[1];
		if ((offset % DEV_BSIZE) != 0 || (length % DEV_BSIZE) != 0 ||
		    offset < 0 || offset >= zv->zv_volsize ||
		    length <= 0) {
			printf("%s: offset=%jd length=%jd\n", __func__, offset,
			    length);
			error = SET_ERROR(EINVAL);
			break;
		}
		rw_enter(&zv->zv_suspend_lock, ZVOL_RW_READER);
		zvol_ensure_zilog(zv);
		lr = zfs_rangelock_enter(&zv->zv_rangelock, offset, length,
		    RL_WRITER);
		dmu_tx_t *tx = dmu_tx_create(zv->zv_objset);
		error = dmu_tx_assign(tx, TXG_WAIT);
		if (error != 0) {
			sync = FALSE;
			dmu_tx_abort(tx);
		} else {
			sync = (zv->zv_objset->os_sync == ZFS_SYNC_ALWAYS);
			zvol_log_truncate(zv, tx, offset, length, sync);
			dmu_tx_commit(tx);
			error = dmu_free_long_range(zv->zv_objset, ZVOL_OBJ,
			    offset, length);
		}
		zfs_rangelock_exit(lr);
		if (sync)
			zil_commit(zv->zv_zilog, ZVOL_OBJ);
		rw_exit(&zv->zv_suspend_lock);
		break;
	case DIOCGSTRIPESIZE:
		*(off_t *)data = zv->zv_volblocksize;
		break;
	case DIOCGSTRIPEOFFSET:
		*(off_t *)data = 0;
		break;
	case DIOCGATTR: {
		spa_t *spa = dmu_objset_spa(zv->zv_objset);
		struct diocgattr_arg *arg = (struct diocgattr_arg *)data;
		uint64_t refd, avail, usedobjs, availobjs;

		if (strcmp(arg->name, "GEOM::candelete") == 0)
			arg->value.i = 1;
		else if (strcmp(arg->name, "blocksavail") == 0) {
			dmu_objset_space(zv->zv_objset, &refd, &avail,
			    &usedobjs, &availobjs);
			arg->value.off = avail / DEV_BSIZE;
		} else if (strcmp(arg->name, "blocksused") == 0) {
			dmu_objset_space(zv->zv_objset, &refd, &avail,
			    &usedobjs, &availobjs);
			arg->value.off = refd / DEV_BSIZE;
		} else if (strcmp(arg->name, "poolblocksavail") == 0) {
			avail = metaslab_class_get_space(spa_normal_class(spa));
			avail -= metaslab_class_get_alloc(
			    spa_normal_class(spa));
			arg->value.off = avail / DEV_BSIZE;
		} else if (strcmp(arg->name, "poolblocksused") == 0) {
			refd = metaslab_class_get_alloc(spa_normal_class(spa));
			arg->value.off = refd / DEV_BSIZE;
		} else
			error = SET_ERROR(ENOIOCTL);
		break;
	}
	case FIOSEEKHOLE:
	case FIOSEEKDATA: {
		off_t *off = (off_t *)data;
		uint64_t noff;
		boolean_t hole;

		hole = (cmd == FIOSEEKHOLE);
		noff = *off;
		lr = zfs_rangelock_enter(&zv->zv_rangelock, 0, UINT64_MAX,
		    RL_READER);
		error = dmu_offset_next(zv->zv_objset, ZVOL_OBJ, hole, &noff);
		zfs_rangelock_exit(lr);
		*off = noff;
		break;
	}
	default:
		error = SET_ERROR(ENOIOCTL);
	}

	return (error);
}

/*
 * Misc. helpers
 */

static void
zvol_ensure_zilog(zvol_state_t *zv)
{
	ASSERT(ZVOL_RW_READ_HELD(&zv->zv_suspend_lock));

	/*
	 * Open a ZIL if this is the first time we have written to this
	 * zvol. We protect zv->zv_zilog with zv_suspend_lock rather
	 * than zv_state_lock so that we don't need to acquire an
	 * additional lock in this path.
	 */
	if (zv->zv_zilog == NULL) {
		if (!rw_tryupgrade(&zv->zv_suspend_lock)) {
			rw_exit(&zv->zv_suspend_lock);
			rw_enter(&zv->zv_suspend_lock, RW_WRITER);
		}
		if (zv->zv_zilog == NULL) {
			zv->zv_zilog = zil_open(zv->zv_objset,
			    zvol_get_data, &zv->zv_kstat.dk_zil_sums);
			zv->zv_flags |= ZVOL_WRITTEN_TO;
			/* replay / destroy done in zvol_os_create_minor() */
			VERIFY0(zv->zv_zilog->zl_header->zh_flags &
			    ZIL_REPLAY_NEEDED);
		}
		rw_downgrade(&zv->zv_suspend_lock);
	}
}

boolean_t
zvol_os_is_zvol(const char *device)
{
	return (device && strncmp(device, ZVOL_DIR, strlen(ZVOL_DIR)) == 0);
}

void
zvol_os_rename_minor(zvol_state_t *zv, const char *newname)
{
	ASSERT(RW_LOCK_HELD(&zvol_state_lock));
	ASSERT(MUTEX_HELD(&zv->zv_state_lock));

	/* Move to a new hashtable entry.  */
	zv->zv_hash = zvol_name_hash(zv->zv_name);
	hlist_del(&zv->zv_hlink);
	hlist_add_head(&zv->zv_hlink, ZVOL_HT_HEAD(zv->zv_hash));

	if (zv->zv_volmode == ZFS_VOLMODE_GEOM) {
		struct zvol_state_geom *zsg = &zv->zv_zso->zso_geom;
		struct g_provider *pp = zsg->zsg_provider;
		struct g_geom *gp;

		g_topology_lock();
		gp = pp->geom;
		ASSERT3P(gp, !=, NULL);

		zsg->zsg_provider = NULL;
		g_wither_provider(pp, ENXIO);

		pp = g_new_providerf(gp, "%s/%s", ZVOL_DRIVER, newname);
		pp->flags |= G_PF_DIRECT_RECEIVE | G_PF_DIRECT_SEND;
		pp->sectorsize = DEV_BSIZE;
		pp->mediasize = zv->zv_volsize;
		pp->private = zv;
		zsg->zsg_provider = pp;
		g_error_provider(pp, 0);
		g_topology_unlock();
	} else if (zv->zv_volmode == ZFS_VOLMODE_DEV) {
		struct zvol_state_dev *zsd = &zv->zv_zso->zso_dev;
		struct cdev *dev;
		struct make_dev_args args;

		dev = zsd->zsd_cdev;
		if (dev != NULL) {
			destroy_dev(dev);
			dev = zsd->zsd_cdev = NULL;
			if (zv->zv_open_count > 0) {
				zv->zv_flags &= ~ZVOL_EXCL;
				zv->zv_open_count = 0;
				/* XXX  need suspend lock but lock order */
				zvol_last_close(zv);
			}
		}

		make_dev_args_init(&args);
		args.mda_flags = MAKEDEV_CHECKNAME | MAKEDEV_WAITOK;
		args.mda_devsw = &zvol_cdevsw;
		args.mda_cr = NULL;
		args.mda_uid = UID_ROOT;
		args.mda_gid = GID_OPERATOR;
		args.mda_mode = 0640;
		args.mda_si_drv2 = zv;
		if (make_dev_s(&args, &dev, "%s/%s", ZVOL_DRIVER, newname)
		    == 0) {
#if __FreeBSD_version > 1300130
			dev->si_iosize_max = maxphys;
#else
			dev->si_iosize_max = MAXPHYS;
#endif
			zsd->zsd_cdev = dev;
		}
	}
	strlcpy(zv->zv_name, newname, sizeof (zv->zv_name));
	dataset_kstats_rename(&zv->zv_kstat, newname);
}

/*
 * Remove minor node for the specified volume.
 */
void
zvol_os_free(zvol_state_t *zv)
{
	ASSERT(!RW_LOCK_HELD(&zv->zv_suspend_lock));
	ASSERT(!MUTEX_HELD(&zv->zv_state_lock));
	ASSERT0(zv->zv_open_count);

	ZFS_LOG(1, "ZVOL %s destroyed.", zv->zv_name);

	rw_destroy(&zv->zv_suspend_lock);
	zfs_rangelock_fini(&zv->zv_rangelock);

	if (zv->zv_volmode == ZFS_VOLMODE_GEOM) {
		struct zvol_state_geom *zsg = &zv->zv_zso->zso_geom;
		struct g_provider *pp __maybe_unused = zsg->zsg_provider;

		ASSERT3P(pp->private, ==, NULL);

		g_topology_lock();
		zvol_geom_destroy(zv);
		g_topology_unlock();
		mtx_destroy(&zsg->zsg_queue_mtx);
	} else if (zv->zv_volmode == ZFS_VOLMODE_DEV) {
		struct zvol_state_dev *zsd = &zv->zv_zso->zso_dev;
		struct cdev *dev = zsd->zsd_cdev;

		if (dev != NULL) {
			ASSERT3P(dev->si_drv2, ==, NULL);
			destroy_dev(dev);
			knlist_clear(&zsd->zsd_selinfo.si_note, 0);
			knlist_destroy(&zsd->zsd_selinfo.si_note);
		}
	}

	mutex_destroy(&zv->zv_state_lock);
	dataset_kstats_destroy(&zv->zv_kstat);
	kmem_free(zv->zv_zso, sizeof (struct zvol_state_os));
	kmem_free(zv, sizeof (zvol_state_t));
	zvol_minors--;
}

/*
 * Create a minor node (plus a whole lot more) for the specified volume.
 */
int
zvol_os_create_minor(const char *name)
{
	zvol_state_t *zv;
	objset_t *os;
	dmu_object_info_t *doi;
	uint64_t volsize;
	uint64_t volmode, hash;
	int error;
	bool replayed_zil = B_FALSE;

	ZFS_LOG(1, "Creating ZVOL %s...", name);
	hash = zvol_name_hash(name);
	if ((zv = zvol_find_by_name_hash(name, hash, RW_NONE)) != NULL) {
		ASSERT(MUTEX_HELD(&zv->zv_state_lock));
		mutex_exit(&zv->zv_state_lock);
		return (SET_ERROR(EEXIST));
	}

	DROP_GIANT();

	doi = kmem_alloc(sizeof (dmu_object_info_t), KM_SLEEP);

	/* Lie and say we're read-only. */
	error = dmu_objset_own(name, DMU_OST_ZVOL, B_TRUE, B_TRUE, FTAG, &os);
	if (error)
		goto out_doi;

	error = dmu_object_info(os, ZVOL_OBJ, doi);
	if (error)
		goto out_dmu_objset_disown;

	error = zap_lookup(os, ZVOL_ZAP_OBJ, "size", 8, 1, &volsize);
	if (error)
		goto out_dmu_objset_disown;

	error = dsl_prop_get_integer(name,
	    zfs_prop_to_name(ZFS_PROP_VOLMODE), &volmode, NULL);
	if (error || volmode == ZFS_VOLMODE_DEFAULT)
		volmode = zvol_volmode;
	error = 0;

	/*
	 * zvol_alloc equivalent ...
	 */
	zv = kmem_zalloc(sizeof (*zv), KM_SLEEP);
	zv->zv_hash = hash;
	mutex_init(&zv->zv_state_lock, NULL, MUTEX_DEFAULT, NULL);
	zv->zv_zso = kmem_zalloc(sizeof (struct zvol_state_os), KM_SLEEP);
	zv->zv_volmode = volmode;
	if (zv->zv_volmode == ZFS_VOLMODE_GEOM) {
		struct zvol_state_geom *zsg = &zv->zv_zso->zso_geom;
		struct g_provider *pp;
		struct g_geom *gp;

		zsg->zsg_state = ZVOL_GEOM_UNINIT;
		mtx_init(&zsg->zsg_queue_mtx, "zvol", NULL, MTX_DEF);

		g_topology_lock();
		gp = g_new_geomf(&zfs_zvol_class, "zfs::zvol::%s", name);
		gp->start = zvol_geom_bio_start;
		gp->access = zvol_geom_access;
		pp = g_new_providerf(gp, "%s/%s", ZVOL_DRIVER, name);
		pp->flags |= G_PF_DIRECT_RECEIVE | G_PF_DIRECT_SEND;
		pp->sectorsize = DEV_BSIZE;
		pp->mediasize = 0;
		pp->private = zv;

		zsg->zsg_provider = pp;
		bioq_init(&zsg->zsg_queue);
	} else if (zv->zv_volmode == ZFS_VOLMODE_DEV) {
		struct zvol_state_dev *zsd = &zv->zv_zso->zso_dev;
		struct cdev *dev;
		struct make_dev_args args;

		make_dev_args_init(&args);
		args.mda_flags = MAKEDEV_CHECKNAME | MAKEDEV_WAITOK;
		args.mda_devsw = &zvol_cdevsw;
		args.mda_cr = NULL;
		args.mda_uid = UID_ROOT;
		args.mda_gid = GID_OPERATOR;
		args.mda_mode = 0640;
		args.mda_si_drv2 = zv;
		if (make_dev_s(&args, &dev, "%s/%s", ZVOL_DRIVER, name)
		    == 0) {
#if __FreeBSD_version > 1300130
			dev->si_iosize_max = maxphys;
#else
			dev->si_iosize_max = MAXPHYS;
#endif
			zsd->zsd_cdev = dev;
			knlist_init_sx(&zsd->zsd_selinfo.si_note,
			    &zv->zv_state_lock);
		}
	}
	(void) strlcpy(zv->zv_name, name, MAXPATHLEN);
	rw_init(&zv->zv_suspend_lock, NULL, RW_DEFAULT, NULL);
	zfs_rangelock_init(&zv->zv_rangelock, NULL, NULL);

	if (dmu_objset_is_snapshot(os) || !spa_writeable(dmu_objset_spa(os)))
		zv->zv_flags |= ZVOL_RDONLY;

	zv->zv_volblocksize = doi->doi_data_block_size;
	zv->zv_volsize = volsize;
	zv->zv_objset = os;

	ASSERT3P(zv->zv_kstat.dk_kstats, ==, NULL);
	error = dataset_kstats_create(&zv->zv_kstat, zv->zv_objset);
	if (error)
		goto out_dmu_objset_disown;
	ASSERT3P(zv->zv_zilog, ==, NULL);
	zv->zv_zilog = zil_open(os, zvol_get_data, &zv->zv_kstat.dk_zil_sums);
	if (spa_writeable(dmu_objset_spa(os))) {
		if (zil_replay_disable)
			replayed_zil = zil_destroy(zv->zv_zilog, B_FALSE);
		else
			replayed_zil = zil_replay(os, zv, zvol_replay_vector);
	}
	if (replayed_zil)
		zil_close(zv->zv_zilog);
	zv->zv_zilog = NULL;

	/* TODO: prefetch for geom tasting */

	zv->zv_objset = NULL;
out_dmu_objset_disown:
	dmu_objset_disown(os, B_TRUE, FTAG);

	if (error == 0 && volmode == ZFS_VOLMODE_GEOM) {
		zvol_geom_run(zv);
		g_topology_unlock();
	}
out_doi:
	kmem_free(doi, sizeof (dmu_object_info_t));
	if (error == 0) {
		rw_enter(&zvol_state_lock, RW_WRITER);
		zvol_insert(zv);
		zvol_minors++;
		rw_exit(&zvol_state_lock);
		ZFS_LOG(1, "ZVOL %s created.", name);
	}
	PICKUP_GIANT();
	return (error);
}

void
zvol_os_clear_private(zvol_state_t *zv)
{
	ASSERT(RW_LOCK_HELD(&zvol_state_lock));
	if (zv->zv_volmode == ZFS_VOLMODE_GEOM) {
		struct zvol_state_geom *zsg = &zv->zv_zso->zso_geom;
		struct g_provider *pp = zsg->zsg_provider;

		if (pp->private == NULL) /* already cleared */
			return;

		mtx_lock(&zsg->zsg_queue_mtx);
		zsg->zsg_state = ZVOL_GEOM_STOPPED;
		pp->private = NULL;
		wakeup_one(&zsg->zsg_queue);
		while (zsg->zsg_state != ZVOL_GEOM_RUNNING)
			msleep(&zsg->zsg_state, &zsg->zsg_queue_mtx,
			    0, "zvol:w", 0);
		mtx_unlock(&zsg->zsg_queue_mtx);
		ASSERT(!RW_LOCK_HELD(&zv->zv_suspend_lock));
	} else if (zv->zv_volmode == ZFS_VOLMODE_DEV) {
		struct zvol_state_dev *zsd = &zv->zv_zso->zso_dev;
		struct cdev *dev = zsd->zsd_cdev;

		if (dev != NULL)
			dev->si_drv2 = NULL;
	}
}

int
zvol_os_update_volsize(zvol_state_t *zv, uint64_t volsize)
{
	zv->zv_volsize = volsize;
	if (zv->zv_volmode == ZFS_VOLMODE_GEOM) {
		struct zvol_state_geom *zsg = &zv->zv_zso->zso_geom;
		struct g_provider *pp = zsg->zsg_provider;

		g_topology_lock();

		if (pp->private == NULL) {
			g_topology_unlock();
			return (SET_ERROR(ENXIO));
		}

		/*
		 * Do not invoke resize event when initial size was zero.
		 * ZVOL initializes the size on first open, this is not
		 * real resizing.
		 */
		if (pp->mediasize == 0)
			pp->mediasize = zv->zv_volsize;
		else
			g_resize_provider(pp, zv->zv_volsize);

		g_topology_unlock();
	} else if (zv->zv_volmode == ZFS_VOLMODE_DEV) {
		struct zvol_state_dev *zsd = &zv->zv_zso->zso_dev;

		KNOTE_UNLOCKED(&zsd->zsd_selinfo.si_note, NOTE_ATTRIB);
	}
	return (0);
}

void
zvol_os_set_disk_ro(zvol_state_t *zv, int flags)
{
	// XXX? set_disk_ro(zv->zv_zso->zvo_disk, flags);
}

void
zvol_os_set_capacity(zvol_state_t *zv, uint64_t capacity)
{
	// XXX? set_capacity(zv->zv_zso->zvo_disk, capacity);
}

/*
 * Public interfaces
 */

int
zvol_busy(void)
{
	return (zvol_minors != 0);
}

int
zvol_init(void)
{
	zvol_init_impl();
	return (0);
}

void
zvol_fini(void)
{
	zvol_fini_impl();
}
