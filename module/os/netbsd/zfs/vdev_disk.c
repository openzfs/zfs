// SPDX-License-Identifier: CDDL-1.0
/*
 * Disk vdevs for NetBSD.  Ported from NetBSD 11.0 osnet
 * vdev_disk.c (vn_open + strategy + getdisksize) onto
 * the current OpenZFS vdev_ops / ABD / FLUSH / TRIM ABI.
 *
 * Open the character (raw) node whenever it exists.  I/O is the
 * driver's d_strategy with the character major in b_dev — the same
 * path physio uses.  Do not bdev_strategy() the block twin: that
 * shares the specfs buffer cache with /dev/ldN, so a raw dd is
 * invisible to scrub (0 CKSUM after L0 corruption).
 *
 * cdevsw has no d_strategy; the function pointer is looked up
 * through the block switch after chr2blk, then called directly.
 * VOP_STRATEGY on a VCHR vnode ENXIOs (spec_strategy feeds the
 * character major to bdev_strategy).
 *
 * If the raw node is missing, fall back to the block vnode and
 * vinvalbuf() on every FLUSH, like Linux invalidate_bdev().
 *
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates.
 * Copyright (c) 2012, 2015 by Delphix.
 * Copyright 2013 Nexenta Systems, Inc.
 * Copyright (c) 2013 Joyent, Inc.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_disk.h>
#include <sys/zio.h>
#include <sys/fs/zfs.h>
#include <sys/fm/fs/zfs.h>
#include <sys/abd.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/vnode.h>
#include <miscfs/specfs/specdev.h>
#include <sys/namei.h>
#include <sys/fcntl.h>
#include <sys/disk.h>
#include <sys/disklabel.h>
#include <sys/dkio.h>
#include <sys/buf.h>
#include <sys/workqueue.h>
#include <sys/kmem.h>
#include <sys/cpu.h>
#include <sys/conf.h>
#include <sys/cred.h>
#include <sys/proc.h>

#undef	vn_open
#undef	VOP_UNLOCK

typedef struct vdev_disk {
	vnode_t		*vd_vp;
	dev_t		vd_dev;		/* rdev passed to vd_strategy */
	void		(*vd_strategy)(struct buf *);
	int		vd_dflag;
	kmutex_t	vd_lock;
	kcondvar_t	vd_cv;
	uint_t		vd_flush_busy;
	uint_t		vd_io_busy;
	size_t		vd_maxphys;
	int		vd_fmode;
} vdev_disk_t;

static taskq_t *vdev_disk_taskq;
static kmutex_t vdev_disk_done_lock;
static kcondvar_t vdev_disk_done_cv;
static struct workqueue *vdev_disk_done_wq;
static uint_t vdev_disk_done_busy;

static boolean_t vdev_disk_dispatch(task_func_t, zio_t *);
static void vdev_disk_io_done_wk(struct work *, void *);
static void vdev_disk_io_rele(vdev_disk_t *);
static void vdev_disk_call_strategy(vdev_disk_t *, buf_t *);

void
vdev_disk_init(void)
{
	int error;

	mutex_init(&vdev_disk_done_lock, NULL, MUTEX_SPIN, NULL);
	cv_init(&vdev_disk_done_cv, NULL, CV_DEFAULT, NULL);
	/*
	 * Completions hop off biodone (kmutex is not IPL-safe) onto
	 * a WQ_PERCPU workqueue — one worker per CPU, using buf
	 * b_work.  A per-I/O threadpool_job that is destroyed in
	 * the callback is not legal: the pool worker still holds
	 * the job pointer until the callback returns.
	 */
	error = workqueue_create(&vdev_disk_done_wq, "z_vdev_done",
	    vdev_disk_io_done_wk, NULL, PRI_BIO, IPL_VM,
	    WQ_MPSAFE | WQ_PERCPU);
	VERIFY0(error);
	/*
	 * Issue, TRIM, and cache flush.  A flush is always queued
	 * here.  DIOCCACHESYNC blocks, and ld/wd/sd are MPSAFE, so
	 * running it from io_start would sit on txg_sync.  Dynamic:
	 * floor MAX(ncpu, 2), grows while its workers are blocked.
	 */
	vdev_disk_taskq = taskq_create("z_vdev_disk", MAX(boot_ncpus, 4),
	    minclsyspri, boot_ncpus, INT_MAX, TASKQ_DYNAMIC);
	VERIFY(vdev_disk_taskq);
}

void
vdev_disk_fini(void)
{
	struct workqueue *wq;

	/*
	 * Issue threads run flush.  Drain them first so the last
	 * zio_interrupt still has the completion queue.
	 */
	if (vdev_disk_taskq != NULL) {
		taskq_destroy(vdev_disk_taskq);
		vdev_disk_taskq = NULL;
	}
	mutex_enter(&vdev_disk_done_lock);
	wq = vdev_disk_done_wq;
	vdev_disk_done_wq = NULL;
	while (vdev_disk_done_busy != 0)
		cv_wait(&vdev_disk_done_cv, &vdev_disk_done_lock);
	mutex_exit(&vdev_disk_done_lock);
	if (wq != NULL)
		workqueue_destroy(wq);
	cv_destroy(&vdev_disk_done_cv);
	mutex_destroy(&vdev_disk_done_lock);
}

static void
vdev_disk_hold(vdev_t *vd)
{
	ASSERT3P(vd->vdev_path, !=, NULL);
}

static void
vdev_disk_rele(vdev_t *vd)
{
	ASSERT3P(vd->vdev_path, !=, NULL);
}

static void
vdev_disk_alloc(vdev_t *vd)
{
	vdev_disk_t *dvd;

	dvd = kmem_zalloc(sizeof (vdev_disk_t), KM_SLEEP);
	dvd->vd_dev = NODEV;
	dvd->vd_strategy = NULL;
	dvd->vd_dflag = D_MPSAFE;
	mutex_init(&dvd->vd_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&dvd->vd_cv, NULL, CV_DEFAULT, NULL);
	vd->vdev_tsd = dvd;
}

static void
vdev_disk_free(vdev_t *vd)
{
	vdev_disk_t *dvd = vd->vdev_tsd;

	if (dvd == NULL)
		return;
	cv_destroy(&dvd->vd_cv);
	mutex_destroy(&dvd->vd_lock);
	kmem_free(dvd, sizeof (vdev_disk_t));
	vd->vdev_tsd = NULL;
}

/*
 * disk_find by a few name candidates.  Strip a partition letter
 * first.  Only then drop a leading raw 'r', and only when the
 * remainder is a known disk class -- never raid0 → aid0.
 */
static boolean_t
vdev_disk_class(const char *leaf)
{
	static const char *const cls[] = {
		"wd", "sd", "ld", "dk", "ccd", "cgd", "xbd", "md",
		"raid", "vnd", "fd", "cd", NULL
	};
	size_t i, n;

	for (i = 0; cls[i] != NULL; i++) {
		n = strlen(cls[i]);
		if (strncmp(leaf, cls[i], n) == 0 &&
		    (leaf[n] == '\0' ||
		    (leaf[n] >= '0' && leaf[n] <= '9')))
			return (B_TRUE);
	}
	return (B_FALSE);
}

static void
vdev_disk_call_strategy(vdev_disk_t *dvd, buf_t *bp)
{
	int locked = (dvd->vd_dflag & D_MPSAFE) == 0;

	if (locked)
		KERNEL_LOCK(1, NULL);
	(*dvd->vd_strategy)(bp);
	if (locked)
		KERNEL_UNLOCK_ONE(NULL);
}

static struct disk *
vdev_disk_lookup(const char *path)
{
	const char *base;
	char n[32];
	struct disk *dk;
	size_t len;

	if (path == NULL)
		return (NULL);
	base = strrchr(path, '/');
	base = (base != NULL) ? base + 1 : path;
	strlcpy(n, base, sizeof (n));
	if ((dk = disk_find(n)) != NULL)
		return (dk);
	len = strlen(n);
	if (len > 1 && n[len - 1] >= 'a' &&
	    n[len - 1] < 'a' + MAXMAXPARTITIONS) {
		n[len - 1] = '\0';
		if ((dk = disk_find(n)) != NULL)
			return (dk);
	}
	if (base[0] == 'r' && vdev_disk_class(base + 1)) {
		strlcpy(n, base + 1, sizeof (n));
		if ((dk = disk_find(n)) != NULL)
			return (dk);
		len = strlen(n);
		if (len > 1 && n[len - 1] >= 'a' &&
		    n[len - 1] < 'a' + MAXMAXPARTITIONS) {
			n[len - 1] = '\0';
			if ((dk = disk_find(n)) != NULL)
				return (dk);
		}
	}
	return (NULL);
}

static int
vdev_disk_zvol_raw(const char *path, char *out, size_t n)
{
	const char *rest = NULL;

	if (strncmp(path, "/dev/rzvol/", 11) == 0) {
		if (strlcpy(out, path, n) >= n)
			return (ENAMETOOLONG);
		return (0);
	}
	if (strncmp(path, "/dev/zvol/rdsk/", 15) == 0)
		rest = path + 15;
	else if (strncmp(path, "/dev/zvol/dsk/", 14) == 0)
		rest = path + 14;
	else if (strncmp(path, "/dev/zvol/", 10) == 0) {
		rest = path + 10;
		if (strncmp(rest, "dsk/", 4) == 0 ||
		    strncmp(rest, "rdsk/", 5) == 0)
			return (ENODEV);
	} else {
		return (ENODEV);
	}
	if (rest[0] == '\0')
		return (ENODEV);
	if (snprintf(out, n, "/dev/rzvol/%s", rest) >= (int)n)
		return (ENAMETOOLONG);
	return (0);
}

/*
 * Character (raw) twin of a disk path for vn_open.
 * /dev/ld2 → /dev/rld2, /dev/wd0a → /dev/rwd0a, /dev/rld2 stays.
 * The directory is kept.  Zvols map onto /dev/rzvol, not a
 * leading 'r' on the leaf.  Non-disk paths return ENODEV.
 *
 * Raw is the preferred open: cdev_open, no specfs buffer cache.
 * VOP_STRATEGY cannot be used on that vnode — spec_strategy copies
 * the character major into bp->b_dev and calls bdev_strategy(),
 * which ENXIOs.  After open, vdev_disk_setup_strategy() looks up
 * the driver's d_strategy and we call it with the character rdev.
 */
static int
vdev_disk_rawpath(const char *path, char *out, size_t n)
{
	const char *slash, *base;

	if (path == NULL || path[0] == '\0')
		return (ENODEV);
	if (strncmp(path, "/dev/zvol/", 10) == 0 ||
	    strncmp(path, "/dev/rzvol/", 11) == 0)
		return (vdev_disk_zvol_raw(path, out, n));
	slash = strrchr(path, '/');
	base = (slash != NULL) ? slash + 1 : path;
	if (base[0] == 'r' && vdev_disk_class(base + 1)) {
		if (strlcpy(out, path, n) >= n)
			return (ENAMETOOLONG);
		return (0);
	}
	if (!vdev_disk_class(base))
		return (ENODEV);
	if (slash != NULL) {
		if (snprintf(out, n, "%.*sr%s",
		    (int)(base - path), path, base) >= (int)n)
			return (ENAMETOOLONG);
	} else if (snprintf(out, n, "r%s", base) >= (int)n) {
		return (ENAMETOOLONG);
	}
	return (0);
}

/*
 * Driver strategy entry and the dev_t it expects.
 *
 * Raw (VCHR) is the preferred path: character major in b_dev, no
 * specfs buffer cache.  cdevsw has no d_strategy, so look the
 * function up through the block switch after chr2blk, then call
 * it with the character rdev (what physio does).
 *
 * VBLK is the fallback when the raw node is missing.  I/O then
 * shares the block-device buffer cache; FLUSH must vinvalbuf.
 */
static int
vdev_disk_setup_strategy(vnode_t *vp, vdev_disk_t *dvd)
{
	dev_t d;
	const struct bdevsw *bsw;

	if (vp == NULL || vp->v_rdev == NODEV)
		return (ENXIO);
	switch (vp->v_type) {
	case VCHR:
		d = devsw_chr2blk(vp->v_rdev);
		if (d == NODEV)
			return (ENXIO);
		bsw = bdevsw_lookup(d);
		if (bsw == NULL || bsw->d_strategy == NULL)
			return (ENXIO);
		dvd->vd_strategy = bsw->d_strategy;
		dvd->vd_dev = vp->v_rdev;
		dvd->vd_dflag = bsw->d_flag;
		return (0);
	case VBLK:
		bsw = bdevsw_lookup(vp->v_rdev);
		if (bsw == NULL || bsw->d_strategy == NULL)
			return (ENXIO);
		dvd->vd_strategy = bsw->d_strategy;
		dvd->vd_dev = vp->v_rdev;
		dvd->vd_dflag = bsw->d_flag;
		return (0);
	default:
		return (ENODEV);
	}
}

/*
 * Linux queries bdev_discard_supported() (queue limits).  FreeBSD
 * queries GEOM::candelete.  NetBSD 11 retired DIOCGDISCARDPARAMS
 * and DIOCDISCARD (dkio.h, 6.99).  spec_fdiscard is the remaining
 * interface: it calls cdev_discard / bdev_discard, which call
 * d_discard.  nodiscard is devenodev.
 *
 * Inspect the switch.  Do not VOP_FDISCARD to probe — that is a
 * live UNMAP.  The hardware range is the logical sector
 * (DIOCGSECTORSIZE / dk_geom.dg_secsize), not 2^ashift.
 *
 * This answers "the driver installed a discard method", not "the
 * device advertised UNMAP".  ld(4) and wd(4) always install one
 * and return ENODEV or a silent 0 if the hardware cannot TRIM.
 */
static boolean_t
vdev_disk_discard_supported(vnode_t *vp)
{
	dev_t dev;

	if (vp == NULL)
		return (B_FALSE);
	dev = vp->v_rdev;
	switch (vp->v_type) {
	case VCHR: {
		const struct cdevsw *csw = cdevsw_lookup(dev);

		return (csw != NULL && csw->d_discard != NULL &&
		    csw->d_discard != nodiscard);
	}
	case VBLK: {
		const struct bdevsw *bsw = bdevsw_lookup(dev);

		return (bsw != NULL && bsw->d_discard != NULL &&
		    bsw->d_discard != nodiscard);
	}
	default:
		return (B_FALSE);
	}
}

static void	vdev_disk_close(vdev_t *);

static void
vdev_disk_flush(zio_t *zio, vdev_disk_t *dvd, vnode_t *vp)
{
	int cmd = 1;
	int error;

	/*
	 * Already queued on vdev_disk_taskq by io_start.
	 * DIOCCACHESYNC blocks; txg_sync and the ZIL writer
	 * must not wait it out.  The taskq spawns another
	 * worker when its threads are blocked, so other
	 * disks flush at the same time.
	 */
	if (vp == NULL) {
		error = ENXIO;
	} else {
		error = VOP_IOCTL(vp, DIOCCACHESYNC, &cmd,
		    FREAD | FWRITE, kcred);
		if (error == ENOTTY || error == ENODEV || error == ENOTSUP) {
			zio->io_vd->vdev_nowritecache = B_TRUE;
			error = 0;
		}
	}
	/*
	 * Block vnode: drop the specfs cache so a subsequent read
	 * sees what is on disk (Linux invalidate_bdev).  Do not
	 * V_SAVE — that would write stale buffers back over a raw
	 * dd.  Raw (VCHR) has no buf cache; skip it.
	 */
	if (vp != NULL && vp->v_type == VBLK &&
	    vn_lock(vp, LK_EXCLUSIVE | LK_RETRY) == 0) {
		(void) vinvalbuf(vp, 0, FSCRED, curlwp, false, 0);
		VOP_UNLOCK(vp);
	}
	zio->io_error = error ? SET_ERROR(error) : 0;
	mutex_enter(&dvd->vd_lock);
	ASSERT(dvd->vd_flush_busy > 0);
	dvd->vd_flush_busy--;
	cv_broadcast(&dvd->vd_cv);
	mutex_exit(&dvd->vd_lock);
	zio_interrupt(zio);
}

static int
#ifdef ZFS_VDEV_OPEN_HAS_CRED
vdev_disk_open(vdev_t *vd, uint64_t *psize, uint64_t *max_psize,
    uint64_t *logical_ashift, uint64_t *physical_ashift, cred_t *cr)
#else
vdev_disk_open(vdev_t *vd, uint64_t *psize, uint64_t *max_psize,
    uint64_t *logical_ashift, uint64_t *physical_ashift)
#endif
{
	spa_t *spa = vd->vdev_spa;
	vdev_disk_t *dvd;
	vnode_t *vp;
	struct pathbuf *pb;
	struct disk *pdk;
	struct disk_sectoralign dsa;
	uint64_t numsecs;
	unsigned secsize;
	int error, fmode;
	off_t ms;

#ifdef ZFS_VDEV_OPEN_HAS_CRED
	(void) cr;
#endif
	vd->vdev_nonrot = B_FALSE;
	vd->vdev_is_blkdev = B_TRUE;
	vd->vdev_has_trim = B_FALSE;
	vd->vdev_has_securetrim = B_FALSE;

	if (vd->vdev_path == NULL || vd->vdev_path[0] != '/') {
		vd->vdev_stat.vs_aux = VDEV_AUX_BAD_LABEL;
		return (SET_ERROR(EINVAL));
	}

	if (vd->vdev_tsd != NULL) {
		ASSERT(vd->vdev_reopening);
		dvd = vd->vdev_tsd;
		vp = dvd->vd_vp;
		goto skip_open;
	}

	vdev_disk_alloc(vd);
	dvd = vd->vdev_tsd;

	fmode = FREAD;
	if (spa_mode(spa) & SPA_MODE_WRITE)
		fmode |= FWRITE;

	{
		char *rawpath;
		const char *openpath = vd->vdev_path;
		kauth_cred_t old;
#ifdef ZFS_VDEV_OPEN_HAS_CRED
		cred_t *use = (cr != NULL) ? cr : CRED();
#else
		cred_t *use = CRED();
#endif

		/*
		 * Heap, not a 64-byte stack slot: vdev_path can be a
		 * long /dev/rzvol/... name, and this function already
		 * sits near the 512-byte frame limit.  Prefer the raw
		 * node.  If it is missing, vn_open the path userland
		 * gave (block fallback).
		 */
		rawpath = kmem_alloc(MAXPATHLEN, KM_SLEEP);
		if (vdev_disk_rawpath(vd->vdev_path, rawpath,
		    MAXPATHLEN) == 0)
			openpath = rawpath;

		pb = pathbuf_create(openpath);
		if (pb == NULL) {
			kmem_free(rawpath, MAXPATHLEN);
			vdev_disk_free(vd);
			vd->vdev_stat.vs_aux = VDEV_AUX_OPEN_FAILED;
			return (SET_ERROR(ENOMEM));
		}
		old = zfs_lwp_cred_swap(use);
		error = vn_open(NULL, pb, 0, fmode, 0, &vp, NULL, NULL);
		zfs_lwp_cred_restore(old, use);
		pathbuf_destroy(pb);
		if (error != 0 && openpath != vd->vdev_path) {
			/* Raw node missing; last-ditch the userland path. */
			pb = pathbuf_create(vd->vdev_path);
			if (pb == NULL) {
				kmem_free(rawpath, MAXPATHLEN);
				vdev_disk_free(vd);
				vd->vdev_stat.vs_aux = VDEV_AUX_OPEN_FAILED;
				return (SET_ERROR(ENOMEM));
			}
			old = zfs_lwp_cred_swap(use);
			error = vn_open(NULL, pb, 0, fmode, 0, &vp, NULL, NULL);
			zfs_lwp_cred_restore(old, use);
			pathbuf_destroy(pb);
		}
		kmem_free(rawpath, MAXPATHLEN);
	}
	if (error != 0) {
		zfs_dbgmsg("vdev_disk_open vn_open %s: %d",
		    vd->vdev_path, error);
		vdev_disk_free(vd);
		vd->vdev_stat.vs_aux = VDEV_AUX_OPEN_FAILED;
		return (SET_ERROR(error));
	}

	if (vp->v_type != VCHR && vp->v_type != VBLK) {
		zfs_dbgmsg("vdev_disk_open %s: need VCHR/VBLK, v_type=%d",
		    vd->vdev_path, vp->v_type);
		VOP_UNLOCK(vp);
		(void) vn_close(vp, fmode, FSCRED);
		vdev_disk_free(vd);
		vd->vdev_stat.vs_aux = VDEV_AUX_OPEN_FAILED;
		return (SET_ERROR(ENODEV));
	}

	error = vdev_disk_setup_strategy(vp, dvd);
	if (error != 0) {
		zfs_dbgmsg("vdev_disk_open %s: no strategy v_type=%d",
		    vd->vdev_path, vp->v_type);
		VOP_UNLOCK(vp);
		(void) vn_close(vp, fmode, FSCRED);
		vdev_disk_free(vd);
		vd->vdev_stat.vs_aux = VDEV_AUX_OPEN_FAILED;
		return (SET_ERROR(error));
	}

	pdk = vdev_disk_lookup(vd->vdev_path);
	{
		struct buf buf = {
			.b_dev = dvd->vd_dev,
			.b_bcount = MAXPHYS,
		};
		if (pdk != NULL && pdk->dk_driver != NULL &&
		    pdk->dk_driver->d_minphys != NULL)
			(*pdk->dk_driver->d_minphys)(&buf);
		dvd->vd_maxphys = buf.b_bcount;
		if (dvd->vd_maxphys == 0)
			dvd->vd_maxphys = MAXPHYS;
	}

	/*
	 * Size ioctls while the vn_open lock is still held.
	 * getdisksize wants DIOCGWEDGEINFO / DIOCGPARTINFO; unlabeled
	 * ld(4) has neither.  DIOCGMEDIASIZE and dk_geom still work.
	 */
	error = getdisksize(vp, &numsecs, &secsize);
	if (error != 0 || numsecs == 0 || secsize == 0) {
		ms = 0;
		secsize = DEV_BSIZE;
		error = VOP_IOCTL(vp, DIOCGMEDIASIZE, &ms, FREAD, FSCRED);
		if (error == 0 && ms > 0) {
			unsigned ss = 0;

			(void) VOP_IOCTL(vp, DIOCGSECTORSIZE, &ss,
			    FREAD, FSCRED);
			if (ss != 0)
				secsize = ss;
			numsecs = (uint64_t)ms / secsize;
		} else if (pdk != NULL && pdk->dk_geom.dg_secperunit != 0) {
			numsecs = (uint64_t)pdk->dk_geom.dg_secperunit;
			if (pdk->dk_geom.dg_secsize != 0)
				secsize = pdk->dk_geom.dg_secsize;
			error = 0;
		} else {
			zfs_dbgmsg("vdev_disk_open %s: no size getdisksize=%d",
			    vd->vdev_path, error);
			VOP_UNLOCK(vp);
			(void) vn_close(vp, fmode, FSCRED);
			vdev_disk_free(vd);
			vd->vdev_stat.vs_aux = VDEV_AUX_OPEN_FAILED;
			return (SET_ERROR(error ? error : ENODEV));
		}
	}

	VOP_UNLOCK(vp);

	dvd->vd_vp = vp;
	dvd->vd_fmode = fmode;
	zfs_dbgmsg("vdev_disk_open %s: v_type=%d%s dev=%llu",
	    vd->vdev_path, vp->v_type,
	    vp->v_type == VCHR ? " (raw)" : " (blk)",
	    (u_longlong_t)dvd->vd_dev);

skip_open:
	if (vd->vdev_reopening) {
		error = vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
		if (error != 0) {
			vd->vdev_stat.vs_aux = VDEV_AUX_OPEN_FAILED;
			return (SET_ERROR(error));
		}
		error = getdisksize(vp, &numsecs, &secsize);
		if (error != 0 || numsecs == 0 || secsize == 0) {
			ms = 0;
			secsize = DEV_BSIZE;
			if (VOP_IOCTL(vp, DIOCGMEDIASIZE, &ms, FREAD,
			    FSCRED) == 0 && ms > 0) {
				unsigned ss = 0;

				(void) VOP_IOCTL(vp, DIOCGSECTORSIZE, &ss,
				    FREAD, FSCRED);
				if (ss != 0)
					secsize = ss;
				numsecs = (uint64_t)ms / secsize;
				error = 0;
			} else {
				VOP_UNLOCK(vp);
				vd->vdev_stat.vs_aux = VDEV_AUX_OPEN_FAILED;
				return (SET_ERROR(error ? error : ENODEV));
			}
		}
		VOP_UNLOCK(vp);
	}

	*psize = numsecs * (uint64_t)secsize;
	*max_psize = *psize;
	*logical_ashift = highbit64(MAX(secsize, SPA_MINBLOCKSIZE)) - 1;

	if (VOP_IOCTL(vp, DIOCGSECTORALIGN, &dsa, FREAD, NOCRED) == 0 &&
	    dsa.dsa_alignment != 0) {
		*physical_ashift = highbit64(
		    (uint64_t)dsa.dsa_alignment * secsize) - 1;
	} else {
		*physical_ashift = *logical_ashift;
	}

	vd->vdev_has_trim = vdev_disk_discard_supported(vp);
	vd->vdev_has_securetrim = B_FALSE;

	/*
	 * wholedisk: userland may already have set this.  Otherwise a
	 * trailing disklabel letter is a slice — except RAW_PART, which
	 * is the whole-disk designator ('d' on amd64/i386, 'c' on most
	 * other ports).  wd0d is the canonical whole disk, not a slice.
	 * dk(4) wedges are one volume per minor: never whole-disk, even
	 * when the wedge covers offset 0.  Match zfs_dev_is_whole_disk().
	 */
	if (vd->vdev_wholedisk == -1ULL) {
		const char *leaf = strrchr(vd->vdev_path, '/');
		size_t n;
		char part;

		leaf = (leaf != NULL) ? leaf + 1 : vd->vdev_path;
		if (leaf[0] == 'r' && leaf[1] != '\0')
			leaf++;
		n = strlen(leaf);
		part = (n >= 2) ? leaf[n - 1] : '\0';
		if (n >= 3 && leaf[0] == 'd' && leaf[1] == 'k' &&
		    leaf[2] >= '0' && leaf[2] <= '9')
			vd->vdev_wholedisk = 0;
		else if (n >= 2 && part >= 'a' &&
		    part < 'a' + MAXMAXPARTITIONS &&
		    leaf[n - 2] >= '0' && leaf[n - 2] <= '9' &&
		    part != 'a' + RAW_PART)
			vd->vdev_wholedisk = 0;
		else
			vd->vdev_wholedisk = 1;
	}
	{
		struct dkwedge_info dkw;

		memset(&dkw, 0, sizeof (dkw));
		if (VOP_IOCTL(vp, DIOCGWEDGEINFO, &dkw, FREAD, NOCRED) == 0) {
			if (dkw.dkw_offset != 0)
				vd->vdev_wholedisk = 0;
			if (vd->vdev_physpath == NULL &&
			    dkw.dkw_wname[0] != '\0')
				vd->vdev_physpath = spa_strdup(dkw.dkw_wname);
		}
	}

	vd->vdev_nowritecache = B_FALSE;
	return (0);
}

static void
vdev_disk_close(vdev_t *vd)
{
	vdev_disk_t *dvd = vd->vdev_tsd;

	if (vd->vdev_reopening || dvd == NULL)
		return;

	if (dvd->vd_vp != NULL) {
		vnode_t *vp;

		/*
		 * Drop the vnode under vd_lock so issue cannot
		 * start another flush.  Drain FLUSH and any
		 * strategy already in flight before vn_close.
		 */
		mutex_enter(&dvd->vd_lock);
		vp = dvd->vd_vp;
		dvd->vd_vp = NULL;
		while (dvd->vd_flush_busy != 0 || dvd->vd_io_busy != 0)
			cv_wait(&dvd->vd_cv, &dvd->vd_lock);
		mutex_exit(&dvd->vd_lock);
		if (vp != NULL) {
			/*
			 * vn_close takes LK_EXCLUSIVE.  Do not lock
			 * first (see zfs_file_close).
			 */
			(void) vn_close(vp, dvd->vd_fmode, FSCRED);
		}
	}
	vd->vdev_delayed_close = B_FALSE;
	vdev_disk_free(vd);
}

static void
vdev_disk_buf_return(zio_t *zio, buf_t *bp)
{
	void *buf = bp->b_data;

	putiobuf(bp);
	if (buf != NULL && zio->io_abd != NULL) {
		if (zio->io_type == ZIO_TYPE_READ)
			abd_return_buf_copy(zio->io_abd, buf, zio->io_size);
		else if (zio->io_type == ZIO_TYPE_WRITE)
			abd_return_buf(zio->io_abd, buf, zio->io_size);
	}
}

static void
vdev_disk_io_rele(vdev_disk_t *dvd)
{
	mutex_enter(&dvd->vd_lock);
	ASSERT(dvd->vd_io_busy > 0);
	dvd->vd_io_busy--;
	cv_broadcast(&dvd->vd_cv);
	mutex_exit(&dvd->vd_lock);
}

static void
vdev_disk_complete(zio_t *zio, buf_t *bp)
{
	vdev_disk_t *dvd;

	dvd = (zio->io_vd != NULL) ? zio->io_vd->vdev_tsd : NULL;
	vdev_disk_buf_return(zio, bp);
	if (dvd != NULL)
		vdev_disk_io_rele(dvd);
	zio_delay_interrupt(zio);
}

static void
vdev_disk_io_done_wk(struct work *work, void *cookie)
{
	buf_t *bp = (buf_t *)work;
	zio_t *zio = bp->b_private;

	(void) cookie;
	/*
	 * Log here, not in biodone.  zfs_dbgmsg allocates KM_SLEEP
	 * and takes a sleeping mutex; this workqueue is process context.
	 */
	if (zio->io_error != 0) {
		zfs_dbgmsg("vdev_disk %s %s off=%llu n=%llu err=%d",
		    zio->io_vd != NULL && zio->io_vd->vdev_path != NULL ?
		    zio->io_vd->vdev_path : "?",
		    zio->io_type == ZIO_TYPE_READ ? "read" : "write",
		    (u_longlong_t)zio->io_offset,
		    (u_longlong_t)zio->io_size, zio->io_error);
	}
	vdev_disk_complete(zio, bp);
	mutex_enter(&vdev_disk_done_lock);
	ASSERT(vdev_disk_done_busy > 0);
	vdev_disk_done_busy--;
	cv_broadcast(&vdev_disk_done_cv);
	mutex_exit(&vdev_disk_done_lock);
}

static void
vdev_disk_io_intr(buf_t *bp)
{
	zio_t *zio = bp->b_private;
	struct workqueue *wq;

	/*
	 * biodone2() already ran vwakeup() for writes (it does that
	 * before this iodone).  Do not vwakeup() here — that would
	 * underflow v_numoutput and panic.
	 */
	zio->io_error = (bp->b_error != 0 ? SET_ERROR(bp->b_error) : 0);
	if (zio->io_error == 0 && bp->b_resid != 0)
		zio->io_error = SET_ERROR(EIO);

	/*
	 * Per-buf b_work, not zio->io_tqent.  Issue may still be on
	 * the stack (md/vnd cache-hit biodone from d_strategy)
	 * and the ZIO pipeline owns io_tqent.  Do not stash state
	 * in b_saveaddr — that is physio's saved b_data.
	 */
	mutex_enter(&vdev_disk_done_lock);
	wq = vdev_disk_done_wq;
	if (wq == NULL) {
		mutex_exit(&vdev_disk_done_lock);
		panic("ZFS: vdev_disk_io_intr after vdev_disk_fini");
	}
	vdev_disk_done_busy++;
	workqueue_enqueue(wq, &bp->b_work, NULL);
	mutex_exit(&vdev_disk_done_lock);
}

static void
vdev_disk_strategy(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	vdev_disk_t *dvd = vd->vdev_tsd;
	vnode_t *vp;
	dev_t dev;
	size_t maxphys;
	buf_t *bp, *nbp;
	void *buf;
	size_t resid, off, size;

	if (dvd == NULL) {
		zio->io_error = SET_ERROR(ENXIO);
		zio_delay_interrupt(zio);
		return;
	}
	mutex_enter(&dvd->vd_lock);
	vp = dvd->vd_vp;
	dev = dvd->vd_dev;
	maxphys = dvd->vd_maxphys;
	if (vp == NULL || dvd->vd_strategy == NULL || dev == NODEV) {
		mutex_exit(&dvd->vd_lock);
		zio->io_error = SET_ERROR(ENXIO);
		zio_delay_interrupt(zio);
		return;
	}
	dvd->vd_io_busy++;
	mutex_exit(&dvd->vd_lock);

	if (zio->io_type == ZIO_TYPE_READ)
		buf = abd_borrow_buf(zio->io_abd, zio->io_size);
	else
		buf = abd_borrow_buf_copy(zio->io_abd, zio->io_size);

	bp = getiobuf(vp, true);
	bp->b_flags = B_ASYNC | B_RAW |
	    (zio->io_type == ZIO_TYPE_READ ? B_READ : 0);
	bp->b_cflags = BC_BUSY | BC_NOCACHE;
	bp->b_dev = dev;
	bp->b_data = buf;
	bp->b_blkno = btodb(zio->io_offset);
	bp->b_rawblkno = bp->b_blkno;
	bp->b_bcount = zio->io_size;
	bp->b_bufsize = zio->io_size;
	bp->b_resid = zio->io_size;
	bp->b_iodone = vdev_disk_io_intr;
	bp->b_private = zio;

	/*
	 * Match bwrite()/nestiobuf_setup(): biodone2() vwakeup()s
	 * every write before calling b_iodone, and panics if
	 * v_numoutput goes negative.  We never VOP_STRATEGY, so
	 * nobody else charges this buf.  Split children get their
	 * own increment inside nestiobuf_setup(); the parent
	 * increment here is released when the last child biodone()s
	 * the parent.
	 */
	if (!(bp->b_flags & B_READ)) {
		mutex_enter(vp->v_interlock);
		vp->v_numoutput++;
		mutex_exit(vp->v_interlock);
	}

	if ((size_t)bp->b_bcount <= maxphys) {
		vdev_disk_call_strategy(dvd, bp);
		return;
	}

	resid = zio->io_size;
	off = 0;
	while (resid != 0) {
		size = MIN(resid, maxphys);
		nbp = getiobuf(NULL, true);
		nbp->b_blkno = btodb(zio->io_offset + off);
		nestiobuf_setup(bp, nbp, off, size);
		nbp->b_rawblkno = nbp->b_blkno;
		vdev_disk_call_strategy(dvd, nbp);
		resid -= size;
		off += size;
	}
	/*
	 * Children cover the full parent.  nestiobuf_done() is only
	 * for unissued leftover bytes and takes (mbp, donebytes,
	 * error).  Calling it here (especially with two args) can
	 * complete the parent while children are still in flight or
	 * pass stack garbage as the error.
	 */
}

static void
vdev_disk_trim(void *arg)
{
	zio_t *zio = arg;
	vdev_disk_t *dvd = zio->io_vd->vdev_tsd;
	vnode_t *vp;
	int error;

	if (dvd == NULL) {
		zio->io_error = SET_ERROR(ENXIO);
		zio_interrupt(zio);
		return;
	}
	mutex_enter(&dvd->vd_lock);
	vp = dvd->vd_vp;
	if (vp == NULL) {
		mutex_exit(&dvd->vd_lock);
		zio->io_error = SET_ERROR(ENXIO);
		zio_interrupt(zio);
		return;
	}
	dvd->vd_io_busy++;
	mutex_exit(&dvd->vd_lock);
	/*
	 * Byte range, already ashift-aligned by the TRIM pipeline.
	 * ashift was taken from the hardware logical sector
	 * (getdisksize / DIOCGSECTORSIZE), which is what
	 * dk_discard() requires.  Not 2^ashift independently of
	 * the device.
	 */
	error = vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	if (error != 0) {
		zio->io_error = SET_ERROR(error);
		vdev_disk_io_rele(dvd);
		zio_interrupt(zio);
		return;
	}
	error = VOP_FDISCARD(vp, zio->io_offset, zio->io_size);
	VOP_UNLOCK(vp);
	/*
	 * ld(4)/wd(4) always install d_discard.  Hardware without
	 * UNMAP returns ENODEV (or ENOTTY/ENOTSUP).  That is "no
	 * TRIM", not "disk yanked" — unlike a read/write ENXIO.
	 * Flush already maps those to vdev_nowritecache.  Do the
	 * same here so vdev_disk_io_done does not SPA_ASYNC_REMOVE
	 * a healthy disk on the first zpool trim / autotrim.
	 */
	if (error == ENOTTY || error == ENODEV || error == ENOTSUP ||
	    error == EOPNOTSUPP) {
		zio->io_vd->vdev_has_trim = B_FALSE;
		zio->io_vd->vdev_has_securetrim = B_FALSE;
		error = 0;
	}
	zio->io_error = error ? SET_ERROR(error) : 0;
	vdev_disk_io_rele(dvd);
	zio_interrupt(zio);
}

static void
vdev_disk_io_issue(void *arg)
{
	zio_t *zio = arg;
	vdev_t *vd = zio->io_vd;
	vdev_disk_t *dvd = vd->vdev_tsd;

	if (dvd == NULL) {
		zio->io_error = SET_ERROR(ENXIO);
		zio_interrupt(zio);
		return;
	}

	if (zio->io_type == ZIO_TYPE_FLUSH) {
		vnode_t *vp;

		/*
		 * io_start already counted vd_flush_busy, before
		 * this was queued.  Drop it on every return.
		 */
		if (!vdev_readable(vd)) {
			zio->io_error = SET_ERROR(ENXIO);
		} else if (!zfs_nocacheflush && !vd->vdev_nowritecache) {
			mutex_enter(&dvd->vd_lock);
			vp = dvd->vd_vp;
			mutex_exit(&dvd->vd_lock);
			vdev_disk_flush(zio, dvd, vp);
			return;
		}
		mutex_enter(&dvd->vd_lock);
		ASSERT(dvd->vd_flush_busy > 0);
		dvd->vd_flush_busy--;
		cv_broadcast(&dvd->vd_cv);
		mutex_exit(&dvd->vd_lock);
		zio_interrupt(zio);
		return;
	}

	ASSERT(zio->io_type == ZIO_TYPE_READ || zio->io_type == ZIO_TYPE_WRITE);
	vdev_disk_strategy(zio);
}

static boolean_t
vdev_disk_dispatch(task_func_t func, zio_t *zio)
{
	/*
	 * Issue, TRIM, and flush.  Completion uses bp->b_work so
	 * it cannot collide with io_tqent while issue is still
	 * running (inline biodone from md/vnd) or with the ZIO
	 * pipeline.  The entry is preallocated: no allocation,
	 * so this is safe from any context.  Never
	 * taskq_dispatch(TQ_SLEEP) -- that is kmem_zalloc
	 * (KM_SLEEP) and biodone is at IPL.
	 */
	if (vdev_disk_taskq == NULL) {
		zio->io_error = SET_ERROR(ENXIO);
		zio_interrupt(zio);
		return (B_FALSE);
	}
	VERIFY(taskq_empty_ent(&zio->io_tqent));
	taskq_dispatch_ent(vdev_disk_taskq, func, zio, 0, &zio->io_tqent);
	return (B_TRUE);
}

static void
vdev_disk_io_start(zio_t *zio)
{
	vdev_disk_t *dvd = zio->io_vd->vdev_tsd;

	if (dvd == NULL) {
		zio->io_error = SET_ERROR(ENXIO);
		zio_interrupt(zio);
		return;
	}

	if (zio->io_type == ZIO_TYPE_TRIM) {
		vdev_disk_dispatch(vdev_disk_trim, zio);
		return;
	}

	ASSERT(zio->io_type == ZIO_TYPE_READ ||
	    zio->io_type == ZIO_TYPE_WRITE ||
	    zio->io_type == ZIO_TYPE_FLUSH);
	if (zio->io_type == ZIO_TYPE_FLUSH) {
		/*
		 * DIOCCACHESYNC blocks until the disk finishes.
		 * ld, wd, and sd are D_MPSAFE, so running it here
		 * puts that wait on txg_sync and the ZIL writer,
		 * one leaf after another.  Always queue it.  The
		 * taskq spawns while its workers block, so the
		 * flushes overlap.  Count the flush before the
		 * queue: close() clears vd_vp and waits on
		 * vd_flush_busy, and the task has not started.
		 */
		if (!vdev_readable(zio->io_vd)) {
			zio->io_error = SET_ERROR(ENXIO);
			zio_interrupt(zio);
			return;
		}
		if (zfs_nocacheflush || zio->io_vd->vdev_nowritecache) {
			zio_interrupt(zio);
			return;
		}
		mutex_enter(&dvd->vd_lock);
		if (dvd->vd_vp == NULL || vdev_disk_taskq == NULL) {
			mutex_exit(&dvd->vd_lock);
			zio->io_error = SET_ERROR(ENXIO);
			zio_interrupt(zio);
			return;
		}
		dvd->vd_flush_busy++;
		mutex_exit(&dvd->vd_lock);
		if (!vdev_disk_dispatch(vdev_disk_io_issue, zio)) {
			mutex_enter(&dvd->vd_lock);
			ASSERT(dvd->vd_flush_busy > 0);
			dvd->vd_flush_busy--;
			cv_broadcast(&dvd->vd_cv);
			mutex_exit(&dvd->vd_lock);
		}
		return;
	}
	zio->io_target_timestamp = zio_handle_io_delay(zio);
	/*
	 * getiobuf / abd_borrow can sleep.  A completion on one
	 * disk that issues the next ZIO must not do that at IPL.
	 * !D_MPSAFE strategy takes KERNEL_LOCK and may tsleep;
	 * do not run that on txg_sync.
	 */
	if ((dvd->vd_dflag & D_MPSAFE) == 0 ||
	    cpu_intr_p() || cpu_softintr_p())
		vdev_disk_dispatch(vdev_disk_io_issue, zio);
	else
		vdev_disk_io_issue(zio);
}

static void
vdev_disk_io_done(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	vdev_disk_t *dvd;

	/*
	 * Detach is ENXIO.  ENODEV on read/write is the same (ld
	 * gone).  TRIM/FLUSH already mapped "ioctl not supported"
	 * ENODEV to success above; do not treat a leftover ENODEV
	 * on those types as removal.
	 *
	 * Do not SPA_ASYNC_REMOVE when we closed the vnode ourselves.
	 * vdev_offline sets vdev_offline and vdev_close drops vd_vp;
	 * queued I/O then completes ENXIO via vdev_accessible() and
	 * would otherwise turn OFFLINE into REMOVED.  A real yank
	 * fails while the vnode is still open.
	 */
	if (zio->io_error != ENXIO &&
	    !(zio->io_error == ENODEV &&
	    zio->io_type != ZIO_TYPE_TRIM &&
	    zio->io_type != ZIO_TYPE_FLUSH))
		return;
	if (vd == NULL || vd->vdev_remove_wanted)
		return;
	if (vd->vdev_offline)
		return;
	dvd = vd->vdev_tsd;
	if (dvd == NULL || dvd->vd_vp == NULL)
		return;
	vd->vdev_remove_wanted = B_TRUE;
	spa_async_request(zio->io_spa, SPA_ASYNC_REMOVE);
}

vdev_ops_t vdev_disk_ops = {
	.vdev_op_init = NULL,
	.vdev_op_fini = NULL,
	.vdev_op_open = vdev_disk_open,
	.vdev_op_close = vdev_disk_close,
	.vdev_op_psize_to_asize = vdev_default_asize,
	.vdev_op_asize_to_psize = vdev_default_psize,
	.vdev_op_min_asize = vdev_default_min_asize,
	.vdev_op_min_alloc = NULL,
	.vdev_op_io_start = vdev_disk_io_start,
	.vdev_op_io_done = vdev_disk_io_done,
	.vdev_op_state_change = NULL,
	.vdev_op_need_resilver = NULL,
	.vdev_op_hold = vdev_disk_hold,
	.vdev_op_rele = vdev_disk_rele,
	.vdev_op_remap = NULL,
	.vdev_op_xlate = vdev_default_xlate,
	.vdev_op_rebuild_asize = NULL,
	.vdev_op_metaslab_init = NULL,
	.vdev_op_config_generate = NULL,
	.vdev_op_nparity = NULL,
	.vdev_op_ndisks = NULL,
	.vdev_op_type = VDEV_TYPE_DISK,
	.vdev_op_leaf = B_TRUE
};
