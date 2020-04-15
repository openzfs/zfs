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
 * Copyright (c) 2013 Will Andrews <will@firepipe.net>
 * Copyright (c) 2013, 2020 Jorgen Lundman <lundman@lundman.net>
 */

/*
 * XXX GENERAL COMPATIBILITY ISSUES
 *
 * 'name' is a common argument, but in OS X (and FreeBSD), we need to pass
 * the componentname pointer, so other things can use them.  We should
 * change the 'name' argument to be an opaque name pointer, and define
 * OS-dependent macros that yield the desired results when needed.
 *
 * On OS X, VFS performs access checks before calling anything, so
 * zfs_zaccess_* calls are not used.  Not true on FreeBSD, though.  Perhaps
 * those calls should be conditionally #if 0'd?
 *
 * On OS X, VFS & I/O objects are often opaque, e.g. uio_t and struct vnode
 * require using functions to access elements of an object.  Should convert
 * the Solaris code to use macros on other platforms.
 *
 * OS X and FreeBSD appear to use similar zfs-vfs interfaces; see Apple's
 * comment in zfs_remove() about the fact that VFS holds the last ref while
 * in Solaris it's the ZFS code that does.  On FreeBSD, the code Apple
 * refers to here results in a panic if the branch is actually taken.
 *
 */

#include <sys/cred.h>
#include <sys/vnode.h>
#include <miscfs/fifofs/fifo.h>
#include <miscfs/specfs/specdev.h>
#include <vfs/vfs_support.h>
#include <sys/ioccom.h>
#include <sys/zfs_dir.h>
#include <sys/zfs_ioctl.h>
#include <sys/fs/zfs.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/spa.h>
#include <sys/txg.h>
#include <sys/dbuf.h>
#include <sys/zap.h>
#include <sys/sa.h>
#include <sys/zfs_vnops.h>
#include <sys/vfs.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_rlock.h>
#include <sys/zfs_ctldir.h>

#include <sys/xattr.h>
#include <sys/utfconv.h>
#include <sys/ubc.h>
#include <sys/callb.h>
#include <sys/unistd.h>




#ifdef _KERNEL
#include <sys/sysctl.h>
#include <sys/hfs_internal.h>
unsigned int zfs_vnop_ignore_negatives = 0;
unsigned int zfs_vnop_ignore_positives = 0;
unsigned int zfs_vnop_create_negatives = 1;
unsigned int zfs_disable_spotlight = 0;
unsigned int zfs_disable_trashes = 0;
#endif

#define	DECLARE_CRED(ap) \
	cred_t *cr = (cred_t *)vfs_context_ucred((ap)->a_context)
#define	DECLARE_CONTEXT(ap) \
	caller_context_t *ct = (caller_context_t *)(ap)->a_context
#define	DECLARE_CRED_AND_CONTEXT(ap)	\
	DECLARE_CRED(ap);		\
	DECLARE_CONTEXT(ap)

/* Empty FinderInfo struct */
static u_int32_t emptyfinfo[8] = {0};

/* vnop_lookup needs path buffer each time */
static kmem_cache_t *vnop_lookup_cache = NULL;

/*
 * zfs vfs operations.
 */
static struct vfsops zfs_vfsops_template = {
	zfs_vfs_mount,
	zfs_vfs_start,
	zfs_vfs_unmount,
	zfs_vfs_root,
	zfs_vfs_quotactl,
	zfs_vfs_getattr,
	zfs_vfs_sync,
	zfs_vfs_vget,
	zfs_vfs_fhtovp,
	zfs_vfs_vptofh,
	zfs_vfs_init,
	zfs_vfs_sysctl,
	zfs_vfs_setattr,
#if defined(MAC_OS_X_VERSION_10_12) &&	\
	(MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_12)
	NULL, /* vfs_ioctl */
	NULL, /* vfs_vget_snapdir */
	NULL
#else
	{NULL}
#endif
};

#define	ZFS_VNOP_TBL_CNT	6

static struct vnodeopv_desc *zfs_vnodeop_opv_desc_list[ZFS_VNOP_TBL_CNT] =
{
	&zfs_dvnodeop_opv_desc,
	&zfs_fvnodeop_opv_desc,
	&zfs_symvnodeop_opv_desc,
	&zfs_xdvnodeop_opv_desc,
	&zfs_fifonodeop_opv_desc,
	&zfs_ctldir_opv_desc,
};

static vfstable_t zfs_vfsconf;

int
zfs_vnop_removexattr_int(zfsvfs_t *zfsvfs, znode_t *zp, const char *name,
    cred_t *cr);

int
zfs_vfs_init(__unused struct vfsconf *vfsp)
{
	return (0);
}

int
zfs_vfs_start(__unused struct mount *mp, __unused int flags,
    __unused vfs_context_t context)
{
	return (0);
}

int
zfs_vfs_quotactl(__unused struct mount *mp, __unused int cmds,
    __unused uid_t uid, __unused caddr_t datap, __unused vfs_context_t context)
{
	dprintf("%s ENOTSUP\n", __func__);
	return (ENOTSUP);
}

static kmutex_t		zfs_findernotify_lock;
static kcondvar_t	zfs_findernotify_thread_cv;
static boolean_t	zfs_findernotify_thread_exit;

#define	VNODE_EVENT_ATTRIB	0x00000008

static int
zfs_findernotify_callback(mount_t mp, __unused void *arg)
{
	vfs_context_t kernelctx = spl_vfs_context_kernel();
	struct vnode *rootvp, *vp;
	znode_t *zp = NULL;

	/*
	 * Since potentially other filesystems could be using "our"
	 * fssubtype, and we don't always announce as "zfs" due to
	 * hfs-mimic requirements, we have to make extra care here to
	 * make sure this "mp" really is ZFS.
	 */
	zfsvfs_t *zfsvfs;

	zfsvfs = vfs_fsprivate(mp);

	/* As set in vfs_fsadd() below */
	char tname[MFSNAMELEN] = { 0 };
	vfs_name(mp, tname);
	if (strncmp(tname, "zfs", MFSNAMELEN) != 0)
		return (VFS_RETURNED);

	/*
	 * The first entry in struct zfsvfs is the vfs ptr, so they
	 * should be equal if it is ZFS
	 */
	if (!zfsvfs ||
	    (mp != zfsvfs->z_vfs))
		return (VFS_RETURNED);

	// Filesystem ZFS? Confirm the location of root_id in zfsvfs
	if (zfsvfs->z_root != INO_ROOT)
		return (VFS_RETURNED);


	/* Guard against unmount */
	if (zfs_enter(zfsvfs, FTAG) != 0)
		return (VFS_RETURNED);

	/* Check if space usage has changed enough to bother updating */
	uint64_t refdbytes, availbytes, usedobjs, availobjs;
	uint64_t delta;
	dmu_objset_space(zfsvfs->z_os,
	    &refdbytes, &availbytes, &usedobjs, &availobjs);
	if (availbytes >= zfsvfs->z_findernotify_space) {
		delta = availbytes - zfsvfs->z_findernotify_space;
	} else {
		delta = zfsvfs->z_findernotify_space - availbytes;
	}

#define	ZFS_FINDERNOTIFY_THRESHOLD (1ULL<<20)

	/* Under the limit ? */
	if (delta <= ZFS_FINDERNOTIFY_THRESHOLD) goto out;

	/* Over threashold, so we will notify finder, remember value */
	zfsvfs->z_findernotify_space = availbytes;

	/* If old value is zero (first run), don't bother */
	if (availbytes == delta)
		goto out;

	dprintf("ZFS: findernotify %p space delta %llu\n", mp, delta);

	// Grab the root zp
	if (zfs_zget(zfsvfs, zfsvfs->z_root, &zp) == 0) {

		rootvp = ZTOV(zp);

		struct componentname cn;
		char *tmpname = ".fseventsd";

		memset(&cn, 0, sizeof (cn));
		cn.cn_nameiop = LOOKUP;
		cn.cn_flags = ISLASTCN;
		// cn.cn_context = kernelctx;
		cn.cn_pnbuf = tmpname;
		cn.cn_pnlen = sizeof (tmpname);
		cn.cn_nameptr = cn.cn_pnbuf;
		cn.cn_namelen = strlen(tmpname);

		// Attempt to lookup .Trashes
		if (!VOP_LOOKUP(rootvp, &vp, &cn, kernelctx)) {

			// Send the event to wake up Finder
			struct vnode_attr vattr;
			// Also calls VATTR_INIT
			spl_vfs_get_notify_attributes(&vattr);
			// Fill in vap
			vnode_getattr(vp, &vattr, kernelctx);
			// Send event
			spl_vnode_notify(vp, VNODE_EVENT_ATTRIB,
			    &vattr);

			// Cleanup vp
			vnode_put(vp);

		} // VNOP_LOOKUP

		// Cleanup rootvp
		vnode_put(rootvp);

	} // VFS_ROOT

out:
	zfs_exit(zfsvfs, FTAG);

	return (VFS_RETURNED);
}


static void
zfs_findernotify_thread(void *notused)
{
	callb_cpr_t		cpr;

	dprintf("ZFS: findernotify thread start\n");
	CALLB_CPR_INIT(&cpr, &zfs_findernotify_lock, callb_generic_cpr, FTAG);

	mutex_enter(&zfs_findernotify_lock);
	while (!zfs_findernotify_thread_exit) {

		/* Sleep 32 seconds */
		CALLB_CPR_SAFE_BEGIN(&cpr);
		(void) cv_timedwait(&zfs_findernotify_thread_cv,
		    &zfs_findernotify_lock, ddi_get_lbolt() + (hz<<5));
		CALLB_CPR_SAFE_END(&cpr, &zfs_findernotify_lock);

		if (!zfs_findernotify_thread_exit)
			vfs_iterate(LK_NOWAIT, zfs_findernotify_callback, NULL);

	}

	zfs_findernotify_thread_exit = FALSE;
	cv_broadcast(&zfs_findernotify_thread_cv);
	CALLB_CPR_EXIT(&cpr);		/* drops arc_reclaim_lock */
	dprintf("ZFS: findernotify thread exit\n");
	thread_exit();
}

void
zfs_start_notify_thread(void)
{
	mutex_init(&zfs_findernotify_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&zfs_findernotify_thread_cv, NULL, CV_DEFAULT, NULL);
	zfs_findernotify_thread_exit = FALSE;
	(void) thread_create(NULL, 0, zfs_findernotify_thread, NULL, 0, &p0,
	    TS_RUN, minclsyspri);
}


void
zfs_stop_notify_thread(void)
{
	mutex_enter(&zfs_findernotify_lock);
	zfs_findernotify_thread_exit = TRUE;
	/*
	 * The reclaim thread will set arc_reclaim_thread_exit back to
	 * FALSE when it is finished exiting; we're waiting for that.
	 */
	while (zfs_findernotify_thread_exit) {
		cv_signal(&zfs_findernotify_thread_cv);
		cv_wait(&zfs_findernotify_thread_cv, &zfs_findernotify_lock);
	}
	mutex_exit(&zfs_findernotify_lock);
	mutex_destroy(&zfs_findernotify_lock);
	cv_destroy(&zfs_findernotify_thread_cv);
}

int
zfs_vfs_sysctl(int *name, __unused uint_t namelen, user_addr_t oldp,
    size_t *oldlenp, user_addr_t newp, size_t newlen,
    __unused vfs_context_t context)
{
#if 0
	int error;
	switch (name[0]) {
	case ZFS_SYSCTL_FOOTPRINT: {
		zfs_footprint_stats_t *footprint;
		size_t copyinsize;
		size_t copyoutsize;
		int max_caches;
		int act_caches;

		if (newp) {
			return (EINVAL);
		}
		if (!oldp) {
			*oldlenp = sizeof (zfs_footprint_stats_t);
			return (0);
		}
		copyinsize = *oldlenp;
		if (copyinsize < sizeof (zfs_footprint_stats_t)) {
			*oldlenp = sizeof (zfs_footprint_stats_t);
			return (ENOMEM);
		}
		footprint = kmem_zalloc(copyinsize, KM_SLEEP);

		max_caches = copyinsize - sizeof (zfs_footprint_stats_t);
		max_caches += sizeof (kmem_cache_stats_t);
		max_caches /= sizeof (kmem_cache_stats_t);

		footprint->version = ZFS_FOOTPRINT_VERSION;

		footprint->memory_stats.current = zfs_footprint.current;
		footprint->memory_stats.target = zfs_footprint.target;
		footprint->memory_stats.highest = zfs_footprint.highest;
		footprint->memory_stats.maximum = zfs_footprint.maximum;

		arc_get_stats(&footprint->arc_stats);

		kmem_cache_stats(&footprint->cache_stats[0], max_caches,
		    &act_caches);
		footprint->caches_count = act_caches;
		footprint->thread_count = zfs_threads;

		copyoutsize = sizeof (zfs_footprint_stats_t) +
		    ((act_caches - 1) * sizeof (kmem_cache_stats_t));

		error = ddi_copyout(footprint, oldp, copyoutsize, 0);

		kmem_free(footprint, copyinsize);

		return (error);
	}

	case ZFS_SYSCTL_CONFIG_DEBUGMSG:
		error = sysctl_int(oldp, oldlenp, newp, newlen,
		    &zfs_msg_buf_enabled);
		return (error);

	case ZFS_SYSCTL_CONFIG_zdprintf:
#ifdef ZFS_DEBUG
		error = sysctl_int(oldp, oldlenp, newp, newlen,
		    &zfs_zdprintf_enabled);
#else
		error = ENOTSUP;
#endif
		return (error);
	}
#endif
	return (ENOTSUP);
}

/*
 * All these functions could be declared as 'static' but to assist with
 * dtrace debugging, we do not.
 */

int
zfs_vnop_open(struct vnop_open_args *ap)
#if 0
	struct vnop_open_args {
		struct vnode	*a_vp;
		int		a_mode;
		vfs_context_t	a_context;
	};
#endif
{
	DECLARE_CRED(ap);
	int err = 0;

	err = zfs_open(ap->a_vp, ap->a_mode, 0, cr);

	if (err) dprintf("zfs_open() failed %d\n", err);
	return (err);
}

int
zfs_vnop_close(struct vnop_close_args *ap)
#if 0
	struct vnop_close_args {
		struct vnode	*a_vp;
		int		a_fflag;
		vfs_context_t	a_context;
	};
#endif
{
//	int count = 1;
//	int offset = 0;
//	DECLARE_CRED_AND_CONTEXT(ap);
	DECLARE_CRED(ap);

	return (zfs_close(ap->a_vp, ap->a_fflag, cr));
}

int
zfs_vnop_ioctl(struct vnop_ioctl_args *ap)
#if 0
	struct vnop_ioctl_args {
		struct vnode	*a_vp;
		ulong_t		a_command;
		caddr_t		a_data;
		int		a_fflag;
		kauth_cred_t	a_cred;
		struct proc	*a_p;
	};
#endif
{
	/* OS X has no use for zfs_ioctl(). */
	znode_t *zp = VTOZ(ap->a_vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	int error = 0;
	DECLARE_CRED_AND_CONTEXT(ap);

	dprintf("vnop_ioctl %08lx: VTYPE %d\n", ap->a_command,
			vnode_vtype(ZTOV(zp)));

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	if (IFTOVT((mode_t)zp->z_mode) == VFIFO) {
		dprintf("ZFS: FIFO ioctl  %02lx ('%lu' + %lu)\n",
			ap->a_command, (ap->a_command&0xff00)>>8,
		    ap->a_command&0xff);
		error = fifo_ioctl(ap);
		error = 0;
		zfs_exit(zfsvfs, FTAG);
		goto out;
	}

	if ((IFTOVT((mode_t)zp->z_mode) == VBLK) ||
		(IFTOVT((mode_t)zp->z_mode) == VCHR)) {
		dprintf("ZFS: spec ioctl  %02lx ('%lu' + %lu)\n",
		    ap->a_command, (ap->a_command&0xff00)>>8,
		    ap->a_command&0xff);
		error = spec_ioctl(ap);
		zfs_exit(zfsvfs, FTAG);
		goto out;
	}
	zfs_exit(zfsvfs, FTAG);

	switch (ap->a_command) {

		/* ioctl supported by ZFS and POSIX */

		case F_FULLFSYNC:
			dprintf("%s F_FULLFSYNC\n", __func__);
#ifdef F_BARRIERFSYNC
		case F_BARRIERFSYNC:
			dprintf("%s F_BARRIERFSYNC\n", __func__);
#endif
			error = zfs_fsync(VTOZ(ap->a_vp), /* flag */0, cr);
			break;

		case F_CHKCLEAN:
			dprintf("%s F_CHKCLEAN\n", __func__);
			/*
			 * normally calls http://fxr.watson.org/fxr/source/bsd/
			 * vfs/vfs_cluster.c?v=xnu-2050.18.24#L5839
			 */
			/* XXX Why don't we? */
			off_t fsize = zp->z_size;
			error = is_file_clean(ap->a_vp, fsize);
			break;

		case F_RDADVISE:
			dprintf("%s F_RDADVISE\n", __func__);
			uint64_t file_size;
			struct radvisory *ra;
			int len;

			ra = (struct radvisory *)(ap->a_data);

			file_size = zp->z_size;
			len = ra->ra_count;

			/* XXX Check request size */
			if (ra->ra_offset > file_size) {
				dprintf("invalid request offset\n");
				error = EFBIG;
				break;
			}

			if ((ra->ra_offset + len) > file_size) {
				len = file_size - ra->ra_offset;
				dprintf("%s truncating F_RDADVISE from"
				    " %08x -> %08x\n", __func__,
				    ra->ra_count, len);
			}

			/*
			 * Rather than advisory_read (which calls
			 * cluster_io->VNOP_BLOCKMAP), prefetch
			 * the level 0 metadata and level 1 data
			 * at the requested offset + length.
			 */
			// error = advisory_read(ap->a_vp, file_size,
			//    ra->ra_offset, len);
			dmu_prefetch(zfsvfs->z_os, zp->z_id,
			    0, 0, 0, ZIO_PRIORITY_SYNC_READ);
			dmu_prefetch(zfsvfs->z_os, zp->z_id,
			    1, ra->ra_offset, len,
			    ZIO_PRIORITY_SYNC_READ);
#if 0
	{
		const char *name = vnode_getname(ap->a_vp);
		printf("%s F_RDADVISE: prefetch issued for "
		    "[%s](0x%016llx) (0x%016llx 0x%08x)\n", __func__,
		    (name ? name : ""), zp->z_id,
		    ra->ra_offset, len);
		if (name) vnode_putname(name);
	}
#endif

			break;

		case SPOTLIGHT_GET_MOUNT_TIME:
		case SPOTLIGHT_IOC_GET_MOUNT_TIME:
		case SPOTLIGHT_FSCTL_GET_MOUNT_TIME:
			dprintf("%s SPOTLIGHT_GET_MOUNT_TIME\n", __func__);
			*(uint32_t *)ap->a_data = zfsvfs->z_mount_time;
			break;
		case SPOTLIGHT_GET_UNMOUNT_TIME:
			dprintf("%s SPOTLIGHT_GET_UNMOUNT_TIME\n", __func__);
			*(uint32_t *)ap->a_data = zfsvfs->z_last_unmount_time;
			break;
		case SPOTLIGHT_FSCTL_GET_LAST_MTIME:
		case SPOTLIGHT_IOC_GET_LAST_MTIME:
			dprintf("%s SPOTLIGHT_FSCTL_GET_LAST_MTIME\n",
			    __func__);
			*(uint32_t *)ap->a_data = zfsvfs->z_last_unmount_time;
			break;

		case HFS_SET_ALWAYS_ZEROFILL:
			dprintf("%s HFS_SET_ALWAYS_ZEROFILL\n", __func__);
			/* Required by Spotlight search */
			break;
		case HFS_EXT_BULKACCESS_FSCTL:
			dprintf("%s HFS_EXT_BULKACCESS_FSCTL\n", __func__);
			/* Required by Spotlight search */
			break;

#ifdef FSIOC_FIOSEEKHOLE
		case FSIOC_FIOSEEKHOLE:
		case FSCTL_FIOSEEKHOLE:
		{
			loff_t off;
			off = *(loff_t *)ap->a_data;
			/* offset parameter is in/out */
			error = zfs_holey(zp, SEEK_HOLE, &off);
			if (error)
				break;
			*(loff_t *)ap->a_data = off;
			break;
		}
#endif

#ifdef FSIOC_FIOSEEKDATA
		case FSIOC_FIOSEEKDATA:
		case FSCTL_FIOSEEKDATA:
		{
			loff_t off;
			off = *(loff_t *)ap->a_data;
			/* offset parameter is in/out */
			error = zfs_holey(zp, SEEK_DATA, &off);
			if (error)
				break;
			*(loff_t *)ap->a_data = off;
			break;
		}
#endif


		/* ioctl required to simulate HFS mimic behavior */
		case 0x80005802:
			dprintf("%s 0x80005802 unknown\n", __func__);
			/* unknown - from subsystem read, 'X', 2 */
			break;

		case HFS_GETPATH:
		case HFSIOC_GETPATH:
			dprintf("%s HFS_GETPATH\n", __func__);
				{
				struct vfsstatfs *vfsp;
				struct vnode *file_vp;
				ino64_t cnid;
				int  outlen;
				char *bufptr;
				int flags = 0;

				/* Caller must be owner of file system. */
				vfsp = vfs_statfs(zfsvfs->z_vfs);
				if (proc_suser(current_proc()) &&
				    kauth_cred_getuid((kauth_cred_t)cr) !=
				    vfsp->f_owner) {
					error = EACCES;
					goto out;
				}
				/* Target vnode must be file system's root. */
				if (!vnode_isvroot(ap->a_vp)) {
					error = EINVAL;
					goto out;
				}

				/* We are passed a string containing inode # */
				bufptr = (char *)ap->a_data;
				cnid = strtoul(bufptr, NULL, 10);
				if (ap->a_fflag & HFS_GETPATH_VOLUME_RELATIVE) {
					flags |= BUILDPATH_VOLUME_RELATIVE;
				}

				if ((error = zfs_vfs_vget(zfsvfs->z_vfs, cnid,
				    &file_vp, (vfs_context_t)ct))) {
					goto out;
				}

				error = spl_build_path(file_vp, bufptr,
				    MAXPATHLEN, &outlen, flags,
				    (vfs_context_t)ct);

				vnode_put(file_vp);

				dprintf("ZFS: HFS_GETPATH done %d : '%s'\n",
				    error, error ? "" : bufptr);
			}
			break;

		case HFS_TRANSFER_DOCUMENT_ID:
		case HFSIOC_TRANSFER_DOCUMENT_ID:
			dprintf("%s HFS_TRANSFER_DOCUMENT_ID\n", __func__);
		    {
				u_int32_t to_fd = *(u_int32_t *)ap->a_data;
				file_t *to_fp;
				struct vnode *to_vp;
				znode_t *to_zp;

				to_fp = getf(to_fd);
				if (to_fp == NULL) {
					error = EBADF;
					goto out;
				}

				to_vp = getf_vnode(to_fp);

				if ((error = vnode_getwithref(to_vp))) {
					releasef(to_fd);
					goto out;
				}

				/* Confirm it is inside our mount */
				if (((zfsvfs_t *)vfs_fsprivate(
				    vnode_mount(to_vp))) != zfsvfs) {
					error = EXDEV;
					goto transfer_out;
				}

				to_zp = VTOZ(to_vp);

				/* Source should have UF_TRACKED */
				if (!(zp->z_pflags & ZFS_TRACKED)) {
					dprintf("ZFS: source is not TRACKED\n");
					error = EINVAL;
					/* dest should NOT have UF_TRACKED */
				} else if (to_zp->z_pflags & ZFS_TRACKED) {
					dprintf("ZFS: dest already TRACKED\n");
					error = EEXIST;
					/* should be valid types */
				} else if (
				    (IFTOVT((mode_t)zp->z_mode) == VDIR) ||
				    (IFTOVT((mode_t)zp->z_mode) == VREG) ||
				    (IFTOVT((mode_t)zp->z_mode) == VLNK)) {
					/*
					 * Make sure source has a document id
					 *  - although it can't
					 */
					if (!zp->z_document_id)
						zfs_setattr_generate_id(zp, 0,
							NULL);

					/* transfer over */
					to_zp->z_document_id =
					    zp->z_document_id;
					zp->z_document_id = 0;
					to_zp->z_pflags |= ZFS_TRACKED;
					zp->z_pflags &= ~ZFS_TRACKED;

					/* Commit to disk */
					zfs_setattr_set_documentid(to_zp,
					    B_TRUE);
					zfs_setattr_set_documentid(zp,
					    B_TRUE); /* also update flags */
					dprintf("ZFS: Moved docid %u from "
					    "id %llu to id %llu\n",
					    to_zp->z_document_id, zp->z_id,
					    to_zp->z_id);
				}
transfer_out:
				vnode_put(to_vp);
				releasef(to_fd);
			}
			break;


		case F_MAKECOMPRESSED:
			dprintf("%s F_MAKECOMPRESSED\n", __func__);
			/*
			 * Not entirely sure what this does, but HFS comments
			 * include: "Make the file compressed; truncate &
			 * toggle BSD bits"
			 * makes compressed copy of allocated blocks
			 * shortens file to new length
			 * sets BSD bits to indicate per-file compression
			 *
			 * On HFS, locks cnode and compresses its data. ZFS
			 * inband compression makes this obsolete.
			 */
			if (vfs_isrdonly(zfsvfs->z_vfs) ||
			    !spa_writeable(dmu_objset_spa(zfsvfs->z_os))) {
				error = EROFS;
				goto out;
			}

			/* Are there any other usecounts/FDs? */
			if (vnode_isinuse(ap->a_vp, 1)) {
				error = EBUSY;
				goto out;
			}

			if (zp->z_pflags & ZFS_IMMUTABLE) {
				error = EINVAL;
				goto out;
			}

			/* Return failure */
			error = EINVAL;
			break;

		case HFS_PREV_LINK:
		case HFS_NEXT_LINK:
		case HFSIOC_PREV_LINK:
		case HFSIOC_NEXT_LINK:
			dprintf("%s HFS_PREV/NEXT_LINK\n", __func__);
		{
			/*
			 * Find sibling linkids with hardlinks. a_data points
			 * to the "current" linkid, and look up either prev
			 * or next (a_command) linkid. Return in a_data.
			 */
			uint32_t linkfileid;
			struct vfsstatfs *vfsp;
			/* Caller must be owner of file system. */
			vfsp = vfs_statfs(zfsvfs->z_vfs);
			if ((kauth_cred_getuid(cr) == 0) &&
				kauth_cred_getuid(cr) != vfsp->f_owner) {
				error = EACCES;
				goto out;
			}
			/* Target vnode must be file system's root. */
			if (!vnode_isvroot(ap->a_vp)) {
				error = EINVAL;
				goto out;
			}
			linkfileid = *(uint32_t *)ap->a_data;
			if (linkfileid < 16) { /* kHFSFirstUserCatalogNodeID */
				error = EINVAL;
				goto out;
			}

			/*
			 * Attempt to find the linkid in the hardlink_link
			 * AVL tree. If found, call to get prev or next.
			 */
			hardlinks_t *searchnode, *findnode, *sibling;
			avl_index_t loc;

			searchnode = kmem_zalloc(sizeof (hardlinks_t),
			    KM_SLEEP);
			searchnode->hl_linkid = linkfileid;

			rw_enter(&zfsvfs->z_hardlinks_lock, RW_READER);
			findnode = avl_find(&zfsvfs->z_hardlinks_linkid,
			    searchnode, &loc);
			kmem_free(searchnode, sizeof (hardlinks_t));

			if (!findnode) {
				rw_exit(&zfsvfs->z_hardlinks_lock);
				*(uint32_t *)ap->a_data = 0;
				dprintf("ZFS: HFS_NEXT_LINK/HFS_PREV_LINK %u "
				    "not found\n", linkfileid);
				goto out;
			}

			if (ap->a_command != HFS_NEXT_LINK) {

				while ((sibling =
				    AVL_NEXT(&zfsvfs->z_hardlinks_linkid,
				    findnode)) != NULL) {
					if (findnode->hl_fileid ==
					    sibling->hl_fileid)
						break;
				}

			} else {

				while ((sibling =
				    AVL_PREV(&zfsvfs->z_hardlinks_linkid,
				    findnode)) != NULL) {
					if (findnode->hl_fileid ==
					    sibling->hl_fileid)
						break;
				}

			}
			rw_exit(&zfsvfs->z_hardlinks_lock);

			dprintf("ZFS: HFS_%s_LINK %u sibling %u\n",
			    (ap->a_command != HFS_NEXT_LINK) ? "NEXT" : "PREV",
			    linkfileid,
			    sibling ? sibling->hl_linkid : 0);

			// Did we get a new node?
			if (sibling == NULL) {
				*(uint32_t *)ap->a_data = 0;
				goto out;
			}

			*(uint32_t *)ap->a_data = sibling->hl_linkid;
			error = 0;
		}
			break;

		case HFS_RESIZE_PROGRESS:
		case HFSIOC_RESIZE_PROGRESS:
			dprintf("%s HFS_RESIZE_PROGRESS\n", __func__);
			/* fail as if requested of non-root fs */
			error = EINVAL;
			break;

		case HFS_RESIZE_VOLUME:
		case HFSIOC_RESIZE_VOLUME:
			dprintf("%s HFS_RESIZE_VOLUME\n", __func__);
			/* fail as if requested of non-root fs */
			error = EINVAL;
			break;

		case HFS_CHANGE_NEXT_ALLOCATION:
		case HFSIOC_CHANGE_NEXT_ALLOCATION:
			dprintf("%s HFS_CHANGE_NEXT_ALLOCATION\n", __func__);
			/* fail as if requested of non-root fs */
			error = EINVAL;
			break;

		case HFS_CHANGE_NEXTCNID:
		case HFSIOC_CHANGE_NEXTCNID:
			dprintf("%s HFS_CHANGE_NEXTCNID\n", __func__);
			/* FIXME : fail as though read only */
			error = EROFS;
			break;

		case F_FREEZE_FS:
			dprintf("%s F_FREEZE_FS\n", __func__);
			/* Dont support freeze */
			error = ENOTSUP;
			break;

		case F_THAW_FS:
			dprintf("%s F_THAW_FS\n", __func__);
			/* dont support fail as though insufficient privilege */
			error = EACCES;
			break;

		case HFS_BULKACCESS_FSCTL:
		case HFSIOC_BULKACCESS:
			dprintf("%s HFS_BULKACCESS_FSCTL\n", __func__);
			/* Respond as if HFS_STANDARD flag is set */
			error = EINVAL;
			break;

		case HFS_FSCTL_GET_VERY_LOW_DISK:
		case HFSIOC_GET_VERY_LOW_DISK:
			dprintf("%s HFS_FSCTL_GET_VERY_LOW_DISK\n", __func__);
			*(uint32_t *)ap->a_data =
			    zfsvfs->z_freespace_notify_dangerlimit;
			break;

		case HFS_FSCTL_SET_VERY_LOW_DISK:
		case HFSIOC_SET_VERY_LOW_DISK:
			dprintf("%s HFS_FSCTL_SET_VERY_LOW_DISK\n", __func__);
			if (*(uint32_t *)ap->a_data >=
			    zfsvfs->z_freespace_notify_warninglimit) {
				error = EINVAL;
			} else {
				zfsvfs->z_freespace_notify_dangerlimit =
				    *(uint32_t *)ap->a_data;
			}
			break;

		case HFS_FSCTL_GET_LOW_DISK:
		case HFSIOC_GET_LOW_DISK:
			dprintf("%s HFS_FSCTL_GET_LOW_DISK\n", __func__);
			*(uint32_t *)ap->a_data =
			    zfsvfs->z_freespace_notify_warninglimit;
			break;

		case HFS_FSCTL_SET_LOW_DISK:
		case HFSIOC_SET_LOW_DISK:
			dprintf("%s HFS_FSCTL_SET_LOW_DISK\n", __func__);
			if (*(uint32_t *)ap->a_data >=
			    zfsvfs->z_freespace_notify_desiredlevel ||
			    *(uint32_t *)ap->a_data <=
			    zfsvfs->z_freespace_notify_dangerlimit) {
				error = EINVAL;
			} else {
				zfsvfs->z_freespace_notify_warninglimit =
				    *(uint32_t *)ap->a_data;
			}
			break;

		case HFS_FSCTL_GET_DESIRED_DISK:
		case HFSIOC_GET_DESIRED_DISK:
			dprintf("%s HFS_FSCTL_GET_DESIRED_DISK\n", __func__);
			*(uint32_t *)ap->a_data =
			    zfsvfs->z_freespace_notify_desiredlevel;
			break;

		case HFS_FSCTL_SET_DESIRED_DISK:
		case HFSIOC_SET_DESIRED_DISK:
			dprintf("%s HFS_FSCTL_SET_DESIRED_DISK\n", __func__);
			if (*(uint32_t *)ap->a_data <=
			    zfsvfs->z_freespace_notify_warninglimit) {
				error = EINVAL;
			} else {
				zfsvfs->z_freespace_notify_desiredlevel =
				    *(uint32_t *)ap->a_data;
			}
			break;

		case HFS_VOLUME_STATUS:
		case HFSIOC_VOLUME_STATUS:
			dprintf("%s HFS_VOLUME_STATUS\n", __func__);
			/* For now we always reply "all ok" */
			*(uint32_t *)ap->a_data =
			    zfsvfs->z_notification_conditions;
			break;

		case HFS_SET_BOOT_INFO:
			dprintf("%s HFS_SET_BOOT_INFO\n", __func__);
			/*
			 * ZFS booting is not supported, mimic selection
			 * of a non-root HFS volume
			 */
			*(uint32_t *)ap->a_data = 0;
			error = EINVAL;
			break;
		case HFS_GET_BOOT_INFO:
			{
				u_int32_t vcbFndrInfo[8];
				dprintf("%s HFS_GET_BOOT_INFO\n", __func__);
				/*
				 * ZFS booting is not supported, mimic selection
				 * of a non-root HFS volume
				 */
				memset(vcbFndrInfo, 0, sizeof (vcbFndrInfo));
				struct vfsstatfs *vfsstatfs;
				vfsstatfs = vfs_statfs(zfsvfs->z_vfs);
				vcbFndrInfo[6] = vfsstatfs->f_fsid.val[0];
				vcbFndrInfo[7] = vfsstatfs->f_fsid.val[1];
				memcpy(ap->a_data, vcbFndrInfo,
				    sizeof (vcbFndrInfo));
			}
			break;
		case HFS_MARK_BOOT_CORRUPT:
			dprintf("%s HFS_MARK_BOOT_CORRUPT\n", __func__);
			/*
			 * ZFS booting is not supported, mimic selection
			 * of a non-root HFS volume
			 */
			*(uint32_t *)ap->a_data = 0;
			error = EINVAL;
			break;

		case HFS_FSCTL_GET_JOURNAL_INFO:
		case HFSIOC_GET_JOURNAL_INFO:
			dprintf("%s HFS_FSCTL_GET_JOURNAL_INFO\n", __func__);
			/*
			 * XXX We're setting the mount as 'Journaled'
			 * so this might conflict
			 * Respond as though journal is empty/disabled
			 */
		{
		    struct hfs_journal_info *jip;
		    jip = (struct hfs_journal_info *)ap->a_data;
		    jip->jstart = 0;
		    jip->jsize = 0;
		}
		break;

		case HFS_DISABLE_METAZONE:
			dprintf("%s HFS_DISABLE_METAZONE\n", __func__);
			/* fail as though insufficient privs */
			error = EACCES;
			break;

#ifdef HFS_GET_FSINFO
		case HFS_GET_FSINFO:
		case HFSIOC_GET_FSINFO:
			dprintf("%s HFS_GET_FSINFO\n", __func__);
			break;
#endif

#ifdef HFS_REPIN_HOTFILE_STATE
		case HFS_REPIN_HOTFILE_STATE:
		case HFSIOC_REPIN_HOTFILE_STATE:
			dprintf("%s HFS_REPIN_HOTFILE_STATE\n", __func__);
			break;
#endif

#ifdef HFS_SET_HOTFILE_STATE
		case HFS_SET_HOTFILE_STATE:
		case HFSIOC_SET_HOTFILE_STATE:
			dprintf("%s HFS_SET_HOTFILE_STATE\n", __func__);
			break;
#endif

#ifdef APFSIOC_GET_NEAR_LOW_DISK
		case APFSIOC_GET_NEAR_LOW_DISK:
			dprintf("%s APFSIOC_GET_NEAR_LOW_DISK\n", __func__);
			*(uint32_t *)ap->a_data =
			    zfsvfs->z_freespace_notify_warninglimit;
			break;
#endif

#ifdef APFSIOC_SET_NEAR_LOW_DISK
		case APFSIOC_SET_NEAR_LOW_DISK:
			dprintf("%s APFSIOC_SET_NEAR_LOW_DISK\n", __func__);
			if (*(uint32_t *)ap->a_data >=
			    zfsvfs->z_freespace_notify_desiredlevel ||
			    *(uint32_t *)ap->a_data <=
			    zfsvfs->z_freespace_notify_dangerlimit) {
				error = EINVAL;
			} else {
				zfsvfs->z_freespace_notify_warninglimit =
				    *(uint32_t *)ap->a_data;
			}
			break;
#endif

			/* End HFS mimic ioctl */

		default:
			dprintf("%s: Unknown ioctl %02lx ('%lu' + %lu)\n",
			    __func__, ap->a_command, (ap->a_command&0xff00)>>8,
			    ap->a_command&0xff);
			error = ENOTTY;
	}

out:
	if (error) {
		dprintf("%s: failing ioctl: %02lx ('%lu' + %lu) returned %d\n",
		    __func__, ap->a_command, (ap->a_command&0xff00)>>8,
		    ap->a_command&0xff, error);
	}

	return (error);
}


int
zfs_vnop_read(struct vnop_read_args *ap)
#if 0
	struct vnop_read_args {
		struct vnode	*a_vp;
		struct uio	*a_uio;
		int		a_ioflag;
		vfs_context_t	a_context;
	};
#endif
{
	int ioflag = zfs_ioflags(ap->a_ioflag);
	int error;
	/* uint64_t resid; */
//	DECLARE_CRED_AND_CONTEXT(ap);
	DECLARE_CRED(ap);
	ZFS_UIO_INIT_XNU(uio, ap->a_uio);

	/* resid = uio_resid(ap->a_uio); */
	error = zfs_read(VTOZ(ap->a_vp), uio, ioflag, cr);

	if (error) dprintf("vnop_read %d\n", error);
	return (error);
}

int
zfs_vnop_write(struct vnop_write_args *ap)
#if 0
	struct vnop_write_args {
		struct vnode	*a_vp;
		struct uio	*a_uio;
		int		a_ioflag;
		vfs_context_t	a_context;
	};
#endif
{
	int ioflag = zfs_ioflags(ap->a_ioflag);
	int error;
	DECLARE_CRED(ap);
	ZFS_UIO_INIT_XNU(uio, ap->a_uio);

	// dprintf("zfs_vnop_write(vp %p, offset 0x%llx size 0x%llx\n",
	//    ap->a_vp, uio_offset(ap->a_uio), uio_resid(ap->a_uio));

	error = zfs_write(VTOZ(ap->a_vp), uio, ioflag, cr);

	/*
	 * Mac OS X: pageout requires that the UBC file size be current.
	 * Possibly, we could update it only if size has changed.
	 */

	/* if (tx_bytes != 0) { */
	if (!error) {
		ubc_setsize(ap->a_vp, VTOZ(ap->a_vp)->z_size);
	} else {
		dprintf("%s error %d\n", __func__, error);
	}

	return (error);
}

int
zfs_vnop_access(struct vnop_access_args *ap)
#if 0
	struct vnop_access_args {
		struct vnodeop_desc *a_desc;
		struct vnode	a_vp;
		int		a_action;
		vfs_context_t	a_context;
	};
#endif
{
	int error = ENOTSUP;
	int action = ap->a_action;
	int mode = 0;
	DECLARE_CRED(ap);

	/*
	 * KAUTH_VNODE_READ_EXTATTRIBUTES, as well?
	 * KAUTH_VNODE_WRITE_EXTATTRIBUTES
	 */
	if (action & KAUTH_VNODE_READ_DATA)
		mode |= VREAD;
	if (action & KAUTH_VNODE_WRITE_DATA)
		mode |= VWRITE;
	if (action & KAUTH_VNODE_EXECUTE)
		mode |= VEXEC;

	dprintf("vnop_access: action %04x -> mode %04x\n", action, mode);
	error = zfs_access(VTOZ(ap->a_vp), mode, 0, cr);

	if (error) dprintf("%s: error %d\n", __func__, error);
	return (error);
}


/*
 * hard link references?
 * Read the comment in zfs_getattr_znode_unlocked for the reason
 * for this hackery. Since getattr(VA_NAME) is extremely common
 * call in OSX, we opt to always save the name. We need to be careful
 * as zfs_dirlook can return ctldir node as well (".zfs").
 * Hardlinks also need to be able to return the correct parentid.
 */
static void zfs_cache_name(struct vnode *vp, struct vnode *dvp, char *filename)
{
	znode_t *zp;

	if (vp == NULL)
		return;

	// Only cache files, or we might end up caching "."
	if (!vnode_isreg(vp))
		return;

	zp = VTOZ(vp);

	// If hardlink, remember the parentid.
	if (zp != NULL) {
		if (((zp->z_links > 1) || (zp->z_finder_hardlink)) &&
		    (IFTOVT((mode_t)zp->z_mode) == VREG) && dvp) {
			zp->z_finder_parentid = VTOZ(dvp)->z_id;
		}
	}

	if (!filename ||
	    !filename[0] ||
	    zfsctl_is_node(vp) ||
	    !VTOZ(vp))
		return;

	mutex_enter(&zp->z_lock);

	strlcpy(zp->z_name_cache, filename,
	    MAXPATHLEN);

	mutex_exit(&zp->z_lock);
}


int
zfs_vnop_lookup(struct vnop_lookup_args *ap)
#if 0
	struct vnop_lookup_args {
		struct vnode	*a_dvp;
		struct vnode	**a_vpp;
		struct componentname *a_cnp;
		vfs_context_t	a_context;
	};
#endif
{
	struct componentname *cnp = ap->a_cnp;
	DECLARE_CRED(ap);
	int error;
	int negative_cache = 0;
	znode_t *zp = NULL;
	int direntflags = 0;
	char *filename = NULL;
	int filename_num_bytes = 0;

	*ap->a_vpp = NULL;	/* In case we return an error */

	/*
	 * Darwin uses namelen as an optimisation, for example it can be
	 * set to 5 for the string "alpha/beta" to look up "alpha". In this
	 * case we need to copy it out to null-terminate.
	 * Since cn2 below needs it to be separate to given cnp, we always
	 * allocate it.
	 */
	// filename_num_bytes = cnp->cn_namelen + 1;
	// filename = (char *)kmem_alloc(filename_num_bytes, KM_SLEEP);
	filename_num_bytes = MAXPATHLEN;
	filename = kmem_cache_alloc(vnop_lookup_cache, KM_SLEEP);
	memcpy(filename, cnp->cn_nameptr, cnp->cn_namelen);
	filename[cnp->cn_namelen] = '\0';

#if 1
	/*
	 * cache_lookup() returns 0 for no-entry
	 * -1 for cache found (a_vpp set)
	 * ENOENT for negative cache
	 */
	error = cache_lookup(ap->a_dvp, ap->a_vpp, cnp);
	if (error) {
		/* We found a cache entry, positive or negative. */
		if (error == -1) {	/* Positive entry? */
			if (!zfs_vnop_ignore_positives) {
				error = 0;
				goto exit;	/* Positive cache, return it */
			}
			/* Release iocount held by cache_lookup */
			vnode_put(*ap->a_vpp);
		}
		/* Negatives are only followed if not CREATE, from HFS+. */

		if (cnp->cn_nameiop != CREATE) {
			if (!zfs_vnop_ignore_negatives) {
				goto exit; /* Negative cache hit */
			}
			negative_cache = 1;
		}
	}
#endif

	dprintf("+vnop_lookup '%s' %s\n", filename,
			negative_cache ? "negative_cache":"");

	/*
	 * 'cnp' passed to us is 'readonly' as XNU does not expect a return
	 * name, but most likely expects it correct in getattr.
	 */
	struct componentname cn2;
	cn2.cn_nameptr = filename;
	cn2.cn_namelen = cnp->cn_namelen;
	cn2.cn_pnlen = filename_num_bytes;
	cn2.cn_nameiop = cnp->cn_nameiop;
	cn2.cn_flags = cnp->cn_flags;

	error = zfs_lookup(VTOZ(ap->a_dvp), filename, &zp, /* flags */ 0, cr,
	    &direntflags, &cn2);

	/* flags can be LOOKUP_XATTR | FIGNORECASE */
	if (error == 0)
		*ap->a_vpp = ZTOV(zp);
	else if (error == ENOTSUP) // formD return for not enough space
		error = ENAMETOOLONG;

#if 1
	/*
	 * It appears that VFS layer adds negative cache entries for us, so
	 * we do not need to add them here, or they are duplicated.
	 */
	if (!negative_cache) {
		if ((error == ENOENT) && zfs_vnop_create_negatives) {
			if ((ap->a_cnp->cn_nameiop == CREATE ||
			    ap->a_cnp->cn_nameiop == RENAME) &&
			    (cnp->cn_flags & ISLASTCN)) {
				error = EJUSTRETURN;
				goto exit;
			}
			/* Insert name into cache (non-existent) */
			if ((cnp->cn_flags & MAKEENTRY) &&
			    ap->a_cnp->cn_nameiop != CREATE) {
				cache_enter(ap->a_dvp, NULL, ap->a_cnp);
				dprintf("Negative-cache made for '%s'\n",
				    filename);
			}
		} /* ENOENT */
	}
#endif

exit:

	// If cache_lookup() found it, set zp to it.
	if (*ap->a_vpp != NULL && zp == NULL)
		zp = VTOZ(*ap->a_vpp);

	if (error == 0 && (zp != NULL))
		zfs_cache_name(*ap->a_vpp, ap->a_dvp, filename);

	dprintf("-vnop_lookup %d : dvp %llu '%s'\n", error,
	    VTOZ(ap->a_dvp)->z_id, filename);

	// kmem_free(filename, filename_num_bytes);
	kmem_cache_free(vnop_lookup_cache, filename);

	return (error);
}

int
zfs_vnop_create(struct vnop_create_args *ap)
#if 0
	struct vnop_create_args {
		struct vnode	*a_dvp;
		struct vnode	**a_vpp;
		struct componentname *a_cnp;
		struct vnode_vattr *a_vap;
		vfs_context_t	a_context;
	};
#endif
{
	struct componentname *cnp = ap->a_cnp;
	vattr_t *vap = ap->a_vap;
	DECLARE_CRED(ap);
	vcexcl_t excl;
	int mode = 0;	/* FIXME */
	int error;
	znode_t *zp = NULL;

	dprintf("vnop_create: '%s'\n", cnp->cn_nameptr);

	/*
	 * extern int zfs_create(struct vnode *dvp, char *name, vattr_t *vap,
	 *     int excl, int mode, struct vnode **vpp, cred_t *cr);
	 */
	excl = (vap->va_vaflags & VA_EXCLUSIVE) ? EXCL : NONEXCL;

	/*
	 * This comment is from HFS: hfs_vnops.c
	 * Note that our [xnu] NFS server code does not set the
	 * VA_EXCLUSIVE flag so you cannot assume that callers don't want
	 * EEXIST errors if it's not set.  The common case, where users
	 * are calling open with the O_CREAT mode, is handled in VFS; when
	 * we return EEXIST, it will loop and do the look-up again.
	 */
	excl = EXCL;

	dprintf("*** %s: with %x: %s: mode supplied %o: UTIME_NULL is %s\n",
		__func__, excl,
		excl ? "EXCL" : "NONEXCL",
		vap->va_mode,
		vap->va_vaflags & VA_UTIMES_NULL ? "set" : "not set");

	if (VATTR_IS_ACTIVE(vap, va_access_time)) {
		ZFS_TIME_ENCODE(&vap->va_access_time, zp->z_atime);
	}


	error = zfs_create(VTOZ(ap->a_dvp), cnp->cn_nameptr, vap, excl, mode,
	    &zp, cr, 0, NULL, NULL);
	if (!error) {
		cache_purge_negatives(ap->a_dvp);
		*ap->a_vpp = ZTOV(zp);

		// Also tell XNU what VAPs we handled.
		if (VATTR_IS_ACTIVE(vap, va_mode))
			VATTR_SET_SUPPORTED(vap, va_mode);
		if (VATTR_IS_ACTIVE(vap, va_data_size))
			VATTR_SET_SUPPORTED(vap, va_data_size);
		if (VATTR_IS_ACTIVE(vap, va_type))
			VATTR_SET_SUPPORTED(vap, va_type);
		if (VATTR_IS_ACTIVE(vap, va_uid))
			VATTR_SET_SUPPORTED(vap, va_uid);
		if (VATTR_IS_ACTIVE(vap, va_gid))
			VATTR_SET_SUPPORTED(vap, va_gid);
		if (VATTR_IS_ACTIVE(vap, va_flags))
			VATTR_SET_SUPPORTED(vap, va_flags);
		if (VATTR_IS_ACTIVE(vap, va_create_time))
			VATTR_SET_SUPPORTED(vap, va_create_time);
		if (VATTR_IS_ACTIVE(vap, va_access_time))
			VATTR_SET_SUPPORTED(vap, va_access_time);
		if (VATTR_IS_ACTIVE(vap, va_modify_time))
			VATTR_SET_SUPPORTED(vap, va_modify_time);
		if (VATTR_IS_ACTIVE(vap, va_change_time))
			VATTR_SET_SUPPORTED(vap, va_change_time);
		if (VATTR_IS_ACTIVE(vap, va_backup_time))
			VATTR_SET_SUPPORTED(vap, va_backup_time);

		uint64_t missing = 0;
		missing =
		    (vap->va_active ^ (vap->va_active & vap->va_supported));
		if (missing != 0) {
			dprintf("%s: asked %08llx replied %08llx "
			    " missing %08llx\n", __func__,
			    vap->va_active, vap->va_supported,
			    missing);
		}

		/* We need to update name here for NFS; issue #104 */
		vnode_update_identity(*ap->a_vpp, NULL,
			(const char *)ap->a_cnp->cn_nameptr,
			ap->a_cnp->cn_namelen, 0,
			VNODE_UPDATE_NAME);

	}

	return (error);
}


static int zfs_remove_hardlink(struct vnode *vp, struct vnode *dvp, char *name)
{
	/*
	 * Because we store hash of hardlinks in an AVLtree, we need to remove
	 * any entries in it upon deletion. Since it is complicated to know
	 * if an entry was a hardlink, we simply check if the avltree has the
	 * name.
	 */
	hardlinks_t *searchnode, *findnode;
	avl_index_t loc;

	if (!vp || !VTOZ(vp))
		return (1);
	if (!dvp || !VTOZ(dvp))
		return (1);
	znode_t *zp = VTOZ(vp);
	znode_t *dzp = VTOZ(dvp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	int ishardlink = 0;

	ishardlink = ((zp->z_links > 1) &&
	    (IFTOVT((mode_t)zp->z_mode) == VREG)) ? 1 : 0;
	if (zp->z_finder_hardlink)
		ishardlink = 1;

	if (!ishardlink)
		return (0);

	dprintf("ZFS: removing hash (%llu,%llu,'%s')\n",
	    dzp->z_id, zp->z_id, name);

	// Attempt to remove from hardlink avl, if its there
	searchnode = kmem_zalloc(sizeof (hardlinks_t), KM_SLEEP);
	searchnode->hl_parent = dzp->z_id;
	searchnode->hl_fileid = zp->z_id;
	strlcpy(searchnode->hl_name, name, PATH_MAX);

	rw_enter(&zfsvfs->z_hardlinks_lock, RW_READER);
	findnode = avl_find(&zfsvfs->z_hardlinks, searchnode, &loc);
	rw_exit(&zfsvfs->z_hardlinks_lock);
	kmem_free(searchnode, sizeof (hardlinks_t));

	// Found it? remove it
	if (findnode) {
		rw_enter(&zfsvfs->z_hardlinks_lock, RW_WRITER);
		avl_remove(&zfsvfs->z_hardlinks, findnode);
		avl_remove(&zfsvfs->z_hardlinks_linkid, findnode);
		rw_exit(&zfsvfs->z_hardlinks_lock);
		kmem_free(findnode, sizeof (*findnode));
		dprintf("ZFS: removed hash '%s'\n", name);
		mutex_enter(&zp->z_lock);
		zp->z_name_cache[0] = 0;
		zp->z_finder_parentid = 0;
		mutex_exit(&zp->z_lock);
		return (1);
	}
	return (0);
}


static int zfs_rename_hardlink(struct vnode *vp, struct vnode *tvp,
    struct vnode *fdvp, struct vnode *tdvp,
    char *from, char *to)
{
	/*
	 * Because we store hash of hardlinks in an AVLtree, we need to update
	 * any entries in it upon rename. Since it is complicated to know
	 * if an entry was a hardlink, we simply check if the avltree has the
	 * name.
	 */
	hardlinks_t *searchnode, *findnode, *delnode;
	avl_index_t loc;
	uint64_t parent_fid, parent_tid;
	int ishardlink = 0;

	if (!vp || !VTOZ(vp))
		return (0);
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

	ishardlink = ((zp->z_links > 1) &&
	    (IFTOVT((mode_t)zp->z_mode) == VREG)) ? 1 : 0;
	if (zp->z_finder_hardlink)
		ishardlink = 1;

	if (!ishardlink)
		return (0);

	if (!fdvp || !VTOZ(fdvp))
		return (0);
	parent_fid = VTOZ(fdvp)->z_id;

	if (!tdvp || !VTOZ(tdvp)) {
		parent_tid = parent_fid;
	} else {
		parent_tid = VTOZ(tdvp)->z_id;
	}

	dprintf("ZFS: looking to rename hardlinks (%llu,%llu,%s)\n",
	    parent_fid, zp->z_id, from);


	// Attempt to remove from hardlink avl, if its there
	searchnode = kmem_zalloc(sizeof (hardlinks_t), KM_SLEEP);
	searchnode->hl_parent = parent_fid;
	searchnode->hl_fileid = zp->z_id;
	strlcpy(searchnode->hl_name, from, PATH_MAX);

	rw_enter(&zfsvfs->z_hardlinks_lock, RW_READER);
	findnode = avl_find(&zfsvfs->z_hardlinks, searchnode, &loc);
	rw_exit(&zfsvfs->z_hardlinks_lock);

	// Found it? update it
	if (findnode) {

		rw_enter(&zfsvfs->z_hardlinks_lock, RW_WRITER);

		// Technically, we do not need to re-do the _linkid AVL here.
		avl_remove(&zfsvfs->z_hardlinks, findnode);
		avl_remove(&zfsvfs->z_hardlinks_linkid, findnode);

		// If we already have a hashid for "to" and the rename
		// presumably unlinked it, we need to remove it first.
		searchnode->hl_parent = parent_tid;
		strlcpy(searchnode->hl_name, to, PATH_MAX);
		delnode = avl_find(&zfsvfs->z_hardlinks, searchnode, &loc);
		if (delnode) {
			dprintf("ZFS: apparently %llu:'%s' exists, deleting\n",
			    parent_tid, to);
			avl_remove(&zfsvfs->z_hardlinks, delnode);
			avl_remove(&zfsvfs->z_hardlinks_linkid, delnode);
			kmem_free(delnode, sizeof (*delnode));
		}

		dprintf("ZFS: renamed hash %llu (%llu:'%s' to %llu:'%s'): %s\n",
		    zp->z_id,
		    parent_fid, from,
		    parent_tid, to,
		    delnode ? "deleted":"");

		// Update source node to new hash, and name.
		findnode->hl_parent = parent_tid;
		strlcpy(findnode->hl_name, to, PATH_MAX);
		// zp->z_finder_parentid = parent_tid;

		avl_add(&zfsvfs->z_hardlinks, findnode);
		avl_add(&zfsvfs->z_hardlinks_linkid, findnode);

		rw_exit(&zfsvfs->z_hardlinks_lock);
		kmem_free(searchnode, sizeof (hardlinks_t));

		return (1);
	}

	kmem_free(searchnode, sizeof (hardlinks_t));
	return (0);
}


int
zfs_vnop_remove(struct vnop_remove_args *ap)
#if 0
	struct vnop_remove_args {
		struct vnode	*a_dvp;
		struct vnode	*a_vp;
		struct componentname *a_cnp;
		int		a_flags;
		vfs_context_t	a_context;
	};
#endif
{
//	DECLARE_CRED_AND_CONTEXT(ap);
	DECLARE_CRED(ap);
	int error;

	dprintf("vnop_remove: %p (%s)\n", ap->a_vp, ap->a_cnp->cn_nameptr);

	/*
	 * extern int zfs_remove ( struct vnode *dvp, char *name, cred_t *cr,
	 *     caller_context_t *ct, int flags);
	 */
	error = zfs_remove(VTOZ(ap->a_dvp), ap->a_cnp->cn_nameptr, cr,
	    /* flags */0);
	if (!error) {
		cache_purge(ap->a_vp);

		zfs_remove_hardlink(ap->a_vp,
		    ap->a_dvp,
		    ap->a_cnp->cn_nameptr);
	} else {
		dprintf("%s error %d\n", __func__, error);
	}

	return (error);
}

int
zfs_vnop_mkdir(struct vnop_mkdir_args *ap)
#if 0
	struct vnop_mkdir_args {
		struct vnode	*a_dvp;
		struct vnode	**a_vpp;
		struct componentname *a_cnp;
		struct vnode_vattr *a_vap;
		vfs_context_t	a_context;
	};
#endif
{
//	DECLARE_CRED_AND_CONTEXT(ap);
	DECLARE_CRED(ap);
	int error;

	dprintf("vnop_mkdir '%s'\n", ap->a_cnp->cn_nameptr);

	if (zfs_disable_spotlight) {
		/* Let's deny OS X fseventd */
	    if (ap->a_cnp->cn_nameptr &&
		    strcmp(ap->a_cnp->cn_nameptr, ".fseventsd") == 0)
			return (EINVAL);
		/* spotlight */
		if (ap->a_cnp->cn_nameptr &&
		    strcmp(ap->a_cnp->cn_nameptr, ".Spotlight-V100") == 0)
			return (EINVAL);
	}
	if (zfs_disable_trashes) {
		/* Let's deny OS X .trashes */
	    if (ap->a_cnp->cn_nameptr &&
		    strcmp(ap->a_cnp->cn_nameptr, ".Trashes") == 0)
			return (EINVAL);
	}
	/*
	 * extern int zfs_mkdir(struct vnode *dvp, char *dirname, vattr_t *vap,
	 *     struct vnode **vpp, cred_t *cr, caller_context_t *ct, int flags,
	 *     vsecattr_t *vsecp);
	 */
	znode_t *zp = NULL;
	ap->a_vap->va_mode |= S_IFDIR;
	error = zfs_mkdir(VTOZ(ap->a_dvp), ap->a_cnp->cn_nameptr, ap->a_vap,
	    &zp, cr, /* flags */0, /* vsecp */NULL, NULL);
	if (!error) {
		*ap->a_vpp = ZTOV(zp);
		cache_purge_negatives(ap->a_dvp);
		vnode_update_identity(*ap->a_vpp, ap->a_dvp,
		    (const char *)ap->a_cnp->cn_nameptr, ap->a_cnp->cn_namelen,
		    0, VNODE_UPDATE_NAME);

		VERIFY3P(zp->z_zfsvfs, ==,
		    vfs_fsprivate(vnode_mount(*ap->a_vpp)));


	} else {
		dprintf("%s error %d\n", __func__, error);
	}

	return (error);
}

int
zfs_vnop_rmdir(struct vnop_rmdir_args *ap)
#if 0
	struct vnop_rmdir_args {
		struct vnode	*a_dvp;
		struct vnode	*a_vp;
		struct componentname *a_cnp;
		vfs_context_t	a_context;
	};
#endif
{
//	DECLARE_CRED_AND_CONTEXT(ap);
	DECLARE_CRED(ap);
	int error;

	dprintf("vnop_rmdir\n");

	/*
	 * extern int zfs_rmdir(struct vnode *dvp, char *name,
	 *     struct vnode *cwd, cred_t *cr, caller_context_t *ct, int flags);
	 */
	error = zfs_rmdir(VTOZ(ap->a_dvp), ap->a_cnp->cn_nameptr,
		/* cwd */NULL, cr, /* flags */0);
	if (!error) {
		cache_purge(ap->a_vp);
	} else {
		dprintf("%s error %d\n", __func__, error);
	}

	return (error);
}

int
zfs_vnop_readdir(struct vnop_readdir_args *ap)
#if 0
	struct vnop_readdir_args {
		struct vnode	a_vp;
		struct uio	*a_uio;
		int		a_flags;
		int		*a_eofflag;
		int		*a_numdirent;
		vfs_context_t	a_context;
	};
#endif
{
	int error;
	DECLARE_CRED(ap);
	ZFS_UIO_INIT_XNU(uio, ap->a_uio);

	dprintf("+readdir: %p\n", ap->a_vp);

	/*
	 * XXX This interface needs vfs_has_feature.
	 * XXX zfs_readdir() also needs to grow support for passing back the
	 * number of entries (OS X/FreeBSD) and cookies (FreeBSD). However,
	 * it should be the responsibility of the OS caller to malloc/free
	 * space for that.
	 */

	/*
	 * extern int zfs_readdir(struct vnode *vp, uio_t *uio, cred_t *cr,
	 *     int *eofp, int flags, int *a_numdirent);
	 */
	*ap->a_numdirent = 0;

	error = zfs_readdir(ap->a_vp, uio, cr, ap->a_eofflag, ap->a_flags,
	    ap->a_numdirent);

	/* .zfs dirs can be completely empty */
	if (*ap->a_numdirent == 0)
		*ap->a_numdirent = 2; /* . and .. */

	if (error) {
		dprintf("-readdir %d (nument %d)\n", error, *ap->a_numdirent);
	}
	return (error);
}

int
zfs_vnop_fsync(struct vnop_fsync_args *ap)
#if 0
	struct vnop_fsync_args {
		struct vnode	*a_vp;
		int		a_waitfor;
		vfs_context_t	a_context;
	};
#endif
{
	znode_t *zp = VTOZ(ap->a_vp);
	zfsvfs_t *zfsvfs;
//	DECLARE_CRED_AND_CONTEXT(ap);
	DECLARE_CRED(ap);
	int err;

	/*
	 * Check if this znode has already been synced, freed, and recycled
	 * by znode_pageout_func.
	 *
	 * XXX What is this? Substitute for Illumos vn_has_cached_data()?
	 */
	if (zp == NULL)
		return (0);

	zfsvfs = zp->z_zfsvfs;

	if (!zfsvfs)
		return (0);

	/*
	 * If we come here via vnode_create()->vclean() we can not end up in
	 * zil_commit() or we will deadlock. But we know that vnop_reclaim will
	 * be called next, so we just return success.
	 */
	if (vnode_isrecycled(ap->a_vp))
		return (0);

	err = zfs_fsync(VTOZ(ap->a_vp), /* flag */0, cr);

	if (err) dprintf("%s err %d\n", __func__, err);

	return (err);
}

int
zfs_vnop_getattr(struct vnop_getattr_args *ap)
#if 0
	struct vnop_getattr_args {
		struct vnode	*a_vp;
		struct vnode_vattr *a_vap;
		vfs_context_t	a_context;
	};
#endif
{
	int error;
	DECLARE_CRED_AND_CONTEXT(ap);

	/* dprintf("+vnop_getattr zp %p vp %p\n", VTOZ(ap->a_vp), ap->a_vp); */

	/* If they want ADDEDTIME, make sure to ask for CRTIME */
	if (VATTR_IS_ACTIVE(ap->a_vap, va_addedtime))
		VATTR_WANTED(ap->a_vap, va_create_time);

	error = zfs_getattr(ap->a_vp, ap->a_vap, /* flags */0, cr, ct);

	if (error == 0) {
		error = zfs_getattr_znode_unlocked(ap->a_vp, ap->a_vap);
	}
	if (error)
		dprintf("-vnop_getattr '%p' %d\n", (ap->a_vp), error);

	return (error);
}

int
zfs_vnop_setattr(struct vnop_setattr_args *ap)
#if 0
	struct vnop_setattr_args {
		struct vnode	*a_vp;
		struct vnode_vattr *a_vap;
		vfs_context_t	a_context;
	};
#endif
{
	DECLARE_CRED(ap);
	vattr_t *vap = ap->a_vap;
	uint_t mask;
	int error = 0;
	znode_t *zp = VTOZ(ap->a_vp);

	/* Translate OS X requested mask to ZFS */
	mask = vap->va_mask;

	if (VATTR_IS_ACTIVE(vap, va_access_time)) {
		ZFS_TIME_ENCODE(&vap->va_access_time, zp->z_atime);
	}
	/*
	 * Both 'flags' and 'acl' can come to setattr, but without 'mode' set.
	 * However, ZFS assumes 'mode' is also set. We need to look up 'mode' in
	 * this case.
	 */
	if ((VATTR_IS_ACTIVE(vap, va_flags) || VATTR_IS_ACTIVE(vap, va_acl)) &&
	    !VATTR_IS_ACTIVE(vap, va_mode)) {
		uint64_t mode;

		mask |= ATTR_MODE;

		dprintf("fetching MODE for FLAGS or ACL\n");

		if ((error = zfs_enter_verify_zp(zp->z_zfsvfs, zp, FTAG)) != 0)
			return (error);
		(void) sa_lookup(zp->z_sa_hdl, SA_ZPL_MODE(zp->z_zfsvfs), &mode,
		    sizeof (mode));
		vap->va_mode = mode;
		zfs_exit(zp->z_zfsvfs, FTAG);
	}
	if (VATTR_IS_ACTIVE(vap, va_flags)) {

		/*
		 * If TRACKED is wanted, and not previously set,
		 * go set DocumentID
		 */
		if ((vap->va_flags & UF_TRACKED) &&
		    !(zp->z_pflags & ZFS_TRACKED)) {
			zfs_setattr_generate_id(zp, 0, NULL);
			/* flags updated in vnops */
			zfs_setattr_set_documentid(zp, B_FALSE);
		}

#ifndef DECMPFS_XATTR_NAME
#define	DECMPFS_XATTR_NAME "com.apple.decmpfs"
#endif

		/* If they are trying to turn on compression.. */
		if (vap->va_flags & UF_COMPRESSED) {
			zp->z_skip_truncate_undo_decmpfs = B_TRUE;
			dprintf("setattr trying to set COMPRESSED!\n");

			vap->va_flags &= ~UF_COMPRESSED;

			if (zp->z_size == 0) {
				dprintf("zero-length file, returning EINVAL\n");
				return (EINVAL);
			}

			/* delete the xattr, can be either of 2 names */
			error = zpl_xattr_set(ap->a_vp, DECMPFS_XATTR_NAME,
			    NULL, 0, cr);
			dprintf("del xattr '%s': %d\n", DECMPFS_XATTR_NAME,
			    error);
			error = zpl_xattr_set(ap->a_vp, XATTR_RESOURCEFORK_NAME,
			    NULL, 0, cr);
			dprintf("del xattr '%s': %d\n", XATTR_RESOURCEFORK_NAME,
			    error);

			if (error != 0)
				dprintf("setattr failed to delete xattr?!\n");

		}
		/* Map OS X file flags to zfs file flags */
		zfs_setbsdflags(zp, vap->va_flags);
		dprintf("OS X flags %08x changed to ZFS %04llx\n",
		    vap->va_flags, zp->z_pflags);
		vap->va_flags = zp->z_pflags;

	}

	vap->va_mask = mask;

	/*
	 * If z_skip_truncate_undo_decmpfs is set, and they are trying to
	 * va_size == 0 (truncate), we undo the decmpfs work here. This is
	 * because we can not stop (no error, or !feature works) macOS from
	 * using decmpfs.
	 */
	if ((VATTR_IS_ACTIVE(vap, va_total_size) ||
	    VATTR_IS_ACTIVE(vap, va_data_size)) &&
	    zp->z_skip_truncate_undo_decmpfs) {
		zp->z_skip_truncate_undo_decmpfs = B_FALSE;

		dprintf("setattr setsize with compress attempted\n");

		/* Successfully deleted the XATTR - skip truncate */
		VATTR_CLEAR_ACTIVE(vap, va_total_size);
		VATTR_CLEAR_ACTIVE(vap, va_data_size);

	}

	error = zfs_setattr(VTOZ(ap->a_vp), ap->a_vap, /* flag */0, cr,
	    NULL);

	dprintf("vnop_setattr: called on vp %p with mask %04x, err=%d\n",
	    ap->a_vp, mask, error);

	if (!error) {
		/* If successful, tell OS X which fields ZFS set. */
		if (VATTR_IS_ACTIVE(vap, va_data_size)) {
			dprintf("ZFS: setattr new size %llx %llx\n",
			    vap->va_size, ubc_getsize(ap->a_vp));
			ubc_setsize(ap->a_vp, vap->va_size);
			VATTR_SET_SUPPORTED(vap, va_data_size);
		}
		if (VATTR_IS_ACTIVE(vap, va_mode))
			VATTR_SET_SUPPORTED(vap, va_mode);
		if (VATTR_IS_ACTIVE(vap, va_acl))
			VATTR_SET_SUPPORTED(vap, va_acl);
		if (VATTR_IS_ACTIVE(vap, va_uid))
			VATTR_SET_SUPPORTED(vap, va_uid);
		if (VATTR_IS_ACTIVE(vap, va_gid))
			VATTR_SET_SUPPORTED(vap, va_gid);
		if (VATTR_IS_ACTIVE(vap, va_access_time))
			VATTR_SET_SUPPORTED(vap, va_access_time);
		if (VATTR_IS_ACTIVE(vap, va_modify_time))
			VATTR_SET_SUPPORTED(vap, va_modify_time);
		if (VATTR_IS_ACTIVE(vap, va_change_time))
			VATTR_SET_SUPPORTED(vap, va_change_time);
		if (VATTR_IS_ACTIVE(vap, va_create_time))
			VATTR_SET_SUPPORTED(vap, va_create_time);
		if (VATTR_IS_ACTIVE(vap, va_backup_time))
			VATTR_SET_SUPPORTED(vap, va_backup_time);
		if (VATTR_IS_ACTIVE(vap, va_flags)) {
			VATTR_SET_SUPPORTED(vap, va_flags);
		}

		/*
		 * If we are told to ignore owners, we scribble over the uid
		 * and gid here unless root.
		 */
		if (((unsigned int)vfs_flags(zp->z_zfsvfs->z_vfs)) &
		    MNT_IGNORE_OWNERSHIP) {
			if (kauth_cred_getuid(cr) != 0) {
				vap->va_uid = UNKNOWNUID;
				vap->va_gid = UNKNOWNGID;
			}
		}
	}

#if 1
	uint64_t missing = 0;
	missing = (vap->va_active ^ (vap->va_active & vap->va_supported));
	if (missing != 0) {
		dprintf("vnop_setattr:: asked %08llx replied %08llx "
		    "missing %08llx\n", vap->va_active,
		    vap->va_supported, missing);
	}
#endif

	if (error)
		dprintf("ZFS: vnop_setattr return failure %d\n", error);
	return (error);
}

int
zfs_vnop_rename(struct vnop_rename_args *ap)
#if 0
	struct vnop_rename_args {
		struct vnode	*a_fdvp;
		struct vnode	*a_fvp;
		struct componentname *a_fcnp;
		struct vnode	*a_tdvp;
		struct vnode	*a_tvp;
		struct componentname *a_tcnp;
		vfs_context_t	a_context;
	};
#endif
{
//	DECLARE_CRED_AND_CONTEXT(ap);
	DECLARE_CRED(ap);
	int error;

	dprintf("vnop_rename\n");

	/*
	 * extern int zfs_rename(struct vnode *sdvp, char *snm,
	 *     struct vnode *tdvp, char *tnm, cred_t *cr, caller_context_t *ct,
	 *     int flags);
	 */
	error = zfs_rename(VTOZ(ap->a_fdvp), ap->a_fcnp->cn_nameptr,
		VTOZ(ap->a_tdvp), ap->a_tcnp->cn_nameptr, cr, /* flags */0,
	    /* rflags */ 0, NULL, NULL);

	if (!error) {
		cache_purge_negatives(ap->a_fdvp);
		cache_purge_negatives(ap->a_tdvp);
		cache_purge(ap->a_fvp);

		zfs_rename_hardlink(ap->a_fvp, ap->a_tvp,
							ap->a_fdvp, ap->a_tdvp,
							ap->a_fcnp->cn_nameptr,
							ap->a_tcnp->cn_nameptr);
		if (ap->a_tvp) {
			cache_purge(ap->a_tvp);
		}

#ifdef __APPLE__
		/*
		 * After a rename, the VGET path /.vol/$fsid/$ino fails for
		 * a short period on hardlinks (until someone calls lookup).
		 * So until we can figure out exactly why this is, we drive
		 * a lookup here to ensure that vget will work
		 * (Finder/Spotlight).
		 */
		if (ap->a_fvp && VTOZ(ap->a_fvp) &&
		    VTOZ(ap->a_fvp)->z_finder_hardlink) {
			struct vnode *vp;
			if (VOP_LOOKUP(ap->a_tdvp, &vp, ap->a_tcnp,
			    spl_vfs_context_kernel()) == 0)
				vnode_put(vp);
		}
#endif

	}

	if (error) dprintf("%s: error %d\n", __func__, error);
	return (error);
}

#if defined(MAC_OS_X_VERSION_10_12) && \
	(MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_12)
int
zfs_vnop_renamex(struct vnop_renamex_args *ap)
#if 0
	struct vnop_renamex_args {
		struct vnode	*a_fdvp;
		struct vnode	*a_fvp;
		struct componentname *a_fcnp;
		struct vnode	*a_tdvp;
		struct vnode	*a_tvp;
		struct componentname *a_tcnp;
		struct vnode_attr *a_vap; // Reserved for future use
		vfs_rename_flags_t a_flags;
		vfs_context_t	a_context;
	};
#endif
{
	DECLARE_CRED(ap);
	int error;

	dprintf("vnop_renamex\n");

	/*
	 * extern int zfs_rename(struct vnode *sdvp, char *snm,
	 *     struct vnode *tdvp, char *tnm, cred_t *cr, caller_context_t *ct,
	 *     int flags);
	 *
	 * Currently, hfs only supports one flag, VFS_RENAME_EXCL, so
	 * we will do the same. Since zfs_rename() only has logic for
	 * FIGNORECASE, passing VFS_RENAME_EXCL should be ok, if a bit
	 * hacky.
	 */
	error = zfs_rename(VTOZ(ap->a_fdvp), ap->a_fcnp->cn_nameptr,
		VTOZ(ap->a_tdvp), ap->a_tcnp->cn_nameptr, cr,
		(ap->a_flags&VFS_RENAME_EXCL), 0, NULL, NULL);

	if (!error) {
		cache_purge_negatives(ap->a_fdvp);
		cache_purge_negatives(ap->a_tdvp);
		cache_purge(ap->a_fvp);

		zfs_rename_hardlink(ap->a_fvp, ap->a_tvp,
							ap->a_fdvp, ap->a_tdvp,
							ap->a_fcnp->cn_nameptr,
							ap->a_tcnp->cn_nameptr);
		if (ap->a_tvp) {
			cache_purge(ap->a_tvp);
		}

#ifdef __APPLE__
		/*
		 * After a rename, the VGET path /.vol/$fsid/$ino fails for
		 * a short period on hardlinks (until someone calls lookup).
		 * So until we can figure out exactly why this is, we drive
		 * a lookup here to ensure that vget will work
		 * (Finder/Spotlight).
		 */
		if (ap->a_fvp && VTOZ(ap->a_fvp) &&
		    VTOZ(ap->a_fvp)->z_finder_hardlink) {
			struct vnode *vp;
			if (VOP_LOOKUP(ap->a_tdvp, &vp, ap->a_tcnp,
			    spl_vfs_context_kernel()) == 0)
				vnode_put(vp);
		}
#endif

	}

	if (error) dprintf("%s: error %d\n", __func__, error);
	return (error);
}
#endif // vnop_renamex_args

int
zfs_vnop_symlink(struct vnop_symlink_args *ap)
#if 0
	struct vnop_symlink_args {
		struct vnode	*a_dvp;
		struct vnode	**a_vpp;
		struct componentname *a_cnp;
		struct vnode_vattr *a_vap;
		char		*a_target;
		vfs_context_t	a_context;
	};
#endif
{
	DECLARE_CRED(ap);
	int error;

	dprintf("vnop_symlink\n");

	/*
	 * extern int zfs_symlink(struct vnode *dvp, struct vnode **vpp,
	 *     char *name, vattr_t *vap, char *link, cred_t *cr);
	 */

	/* OS X doesn't need to set vap->va_mode? */
	znode_t *zp = NULL;
    ap->a_vap->va_mode |= S_IFLNK;
	error = zfs_symlink(VTOZ(ap->a_dvp), ap->a_cnp->cn_nameptr,
	    ap->a_vap, ap->a_target, &zp, cr, 0, NULL);
	if (!error) {
		*ap->a_vpp = ZTOV(zp);
		cache_purge_negatives(ap->a_dvp);
		vnode_update_identity(*ap->a_vpp, NULL,
			(const char *)ap->a_cnp->cn_nameptr,
			ap->a_cnp->cn_namelen, 0,
			VNODE_UPDATE_NAME);
	} else {
		dprintf("%s: error %d\n", __func__, error);
	}
	/* XXX zfs_attach_vnode()? */
	return (error);
}


int
zfs_vnop_readlink(struct vnop_readlink_args *ap)
#if 0
	struct vnop_readlink_args {
		struct vnode	*vp;
		struct uio	*uio;
		vfs_context_t	a_context;
	};
#endif
{
//	DECLARE_CRED_AND_CONTEXT(ap);
	DECLARE_CRED(ap);
	ZFS_UIO_INIT_XNU(uio, ap->a_uio);

	dprintf("vnop_readlink\n");

	/*
	 * extern int zfs_readlink(struct vnode *vp, uio_t *uio, cred_t *cr,
	 *     caller_context_t *ct);
	 */
	return (zfs_readlink(ap->a_vp, uio, cr));
}

int
zfs_vnop_link(struct vnop_link_args *ap)
#if 0
	struct vnop_link_args {
		struct vnode	*a_vp;
		struct vnode	*a_tdvp;
		struct componentname *a_cnp;
		vfs_context_t	a_context;
	};
#endif
{
//	DECLARE_CRED_AND_CONTEXT(ap);
	DECLARE_CRED(ap);
	int error;

	dprintf("vnop_link\n");

	/* XXX Translate this inside zfs_link() instead. */
	if (vnode_mount(ap->a_vp) != vnode_mount(ap->a_tdvp)) {
		dprintf("%s: vp and tdvp on different mounts\n", __func__);
		return (EXDEV);
	}

	/*
	 * XXX Understand why Apple made this comparison in so many places where
	 * others do not.
	 */
	if (ap->a_cnp->cn_namelen >= ZAP_MAXNAMELEN) {
		dprintf("%s: name too long %d\n", __func__,
		    ap->a_cnp->cn_namelen);
		return (ENAMETOOLONG);
	}

	/*
	 * extern int zfs_link(struct vnode *tdvp, struct vnode *svp,
	 *     char *name, cred_t *cr, caller_context_t *ct, int flags);
	 */
	error = zfs_link(VTOZ(ap->a_tdvp), VTOZ(ap->a_vp),
		ap->a_cnp->cn_nameptr, cr, 0);
	if (!error) {
		// Set source vnode to multipath too, zfs_get_vnode()
		// handles the target
		vnode_setmultipath(ap->a_vp);
		cache_purge(ap->a_vp);
		cache_purge_negatives(ap->a_tdvp);
		vnode_update_identity(ap->a_vp, NULL,
			(const char *)ap->a_cnp->cn_nameptr,
			ap->a_cnp->cn_namelen, 0,
			VNODE_UPDATE_NAME);
	} else {
		dprintf("%s error %d\n", __func__, error);
	}

	return (error);
}

int
zfs_vnop_pagein(struct vnop_pagein_args *ap)
#if 0
	struct vnop_pagein_args {
		struct vnode	*a_vp;
		upl_t		a_pl;
		vm_offset_t	a_pl_offset;
		off_t		a_foffset;
		size_t		a_size;
		int		a_flags;
		vfs_context_t	a_context;
	};
#endif
{
	/* XXX Crib this from the Apple zfs_vnops.c. */
	struct vnode *vp = ap->a_vp;
	offset_t off = ap->a_f_offset;
	size_t len = ap->a_size;
	upl_t upl = ap->a_pl;
	vm_offset_t upl_offset = ap->a_pl_offset;
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	caddr_t vaddr = NULL;
	/* vm_offset_t vaddr = NULL; */
	int flags = ap->a_flags;
	int need_unlock = 0;
	int error = 0;
	uint64_t file_sz;

	dprintf("+vnop_pagein: %p/%p off 0x%llx size 0x%lx filesz 0x%llx\n",
			zp, vp, off, len, zp->z_size);

	if (upl == (upl_t)NULL)
		panic("zfs_vnop_pagein: no upl!");

	if (len <= 0) {
		dprintf("zfs_vnop_pagein: invalid size %ld", len);
		if (!(flags & UPL_NOCOMMIT))
			(void) ubc_upl_abort(upl, 0);
		return (EINVAL);
	}

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	file_sz = zp->z_size;

	/* ASSERT(zp->z_dbuf_held && zp->z_phys); */
	/* can't fault passed EOF */
	if ((off < 0) || (off >= file_sz) ||
		(len & PAGE_MASK) || (upl_offset & PAGE_MASK)) {
		dprintf("passed EOF or size error\n");
		zfs_exit(zfsvfs, FTAG);
		if (!(flags & UPL_NOCOMMIT))
			ubc_upl_abort_range(upl, upl_offset, len,
			    (UPL_ABORT_ERROR | UPL_ABORT_FREE_ON_EMPTY));
		return (EFAULT);
	}

	/*
	 * If we already own the lock, then we must be page faulting in the
	 * middle of a write to this file (i.e., we are writing to this file
	 * using data from a mapped region of the file).
	 */
	if (!rw_write_held(&zp->z_map_lock)) {
		rw_enter(&zp->z_map_lock, RW_WRITER);
		need_unlock = TRUE;
	}


	if (ubc_upl_map(upl, (vm_offset_t *)&vaddr) != KERN_SUCCESS) {
		dprintf("zfs_vnop_pagein: failed to ubc_upl_map");
		if (!(flags & UPL_NOCOMMIT))
			(void) ubc_upl_abort(upl, 0);
		if (need_unlock)
			rw_exit(&zp->z_map_lock);
		zfs_exit(zfsvfs, FTAG);
		return (ENOMEM);
	}

	dprintf("vaddr %p with upl_off 0x%lx\n", vaddr, upl_offset);
	vaddr += upl_offset;

	/* Can't read beyond EOF - but we need to zero those extra bytes. */
	if (off + len > file_sz) {
		uint64_t newend = file_sz - off;

		dprintf("ZFS: pagein zeroing offset 0x%llx for 0x%llx bytes.\n",
				newend, len - newend);
		memset(&vaddr[newend], 0, len - newend);
		len = newend;
	}
	/*
	 * Fill pages with data from the file.
	 */
	while (len > 0) {
		uint64_t readlen;

		readlen = MIN(PAGESIZE, len);

		dprintf("pagein from off 0x%llx len 0x%llx into "
		    "address %p (len 0x%lx)\n",
		    off, readlen, vaddr, len);

		error = dmu_read(zp->z_zfsvfs->z_os, zp->z_id, off, readlen,
		    (void *)vaddr, DMU_READ_PREFETCH);
		if (error) {
			printf("zfs_vnop_pagein: dmu_read err %d\n", error);
			break;
		}
		off += readlen;
		vaddr += readlen;
		len -= readlen;
	}
	ubc_upl_unmap(upl);

	if (!(flags & UPL_NOCOMMIT)) {
		if (error)
			ubc_upl_abort_range(upl, upl_offset, ap->a_size,
			    (UPL_ABORT_ERROR | UPL_ABORT_FREE_ON_EMPTY));
		else
			ubc_upl_commit_range(upl, upl_offset, ap->a_size,
			    (UPL_COMMIT_CLEAR_DIRTY |
			    UPL_COMMIT_FREE_ON_EMPTY));
	}
	ZFS_ACCESSTIME_STAMP(zfsvfs, zp);

	/*
	 * We can't grab the range lock for the page as reader which would stop
	 * truncation as this leads to deadlock. So we need to recheck the file
	 * size.
	 */
	if (ap->a_f_offset >= file_sz)
		error = EFAULT;
	if (need_unlock)
		rw_exit(&zp->z_map_lock);

	zfs_exit(zfsvfs, FTAG);
	if (error) dprintf("%s error %d\n", __func__, error);
	return (error);
}




static int
zfs_pageout(zfsvfs_t *zfsvfs, znode_t *zp, upl_t upl, vm_offset_t upl_offset,
    offset_t off, size_t size, int flags)
{
	dmu_tx_t *tx;
	zfs_locked_range_t *lr;
	uint64_t filesz;
	int err = 0;
	size_t len = size;

	dprintf("+vnop_pageout: %p/%p off 0x%llx len 0x%lx upl_off 0x%lx: "
	    "blksz 0x%x, z_size 0x%llx upl %p flags 0x%x\n", zp, ZTOV(zp),
	    off, len, upl_offset, zp->z_blksz,
	    zp->z_size, upl, flags);

	if (upl == (upl_t)NULL) {
		dprintf("ZFS: vnop_pageout: failed on NULL upl\n");
		return (EINVAL);
	}

	if ((err = zfs_enter(zfsvfs, FTAG)) != 0) {
		if (!(flags & UPL_NOCOMMIT))
			(void) ubc_upl_abort(upl,
			    UPL_ABORT_DUMP_PAGES|UPL_ABORT_FREE_ON_EMPTY);
		dprintf("ZFS: vnop_pageout: abort on z_unmounted\n");
		zfs_exit(zfsvfs, FTAG);
		return (EIO);
	}


	/* ASSERT(zp->z_dbuf_held); */ /* field no longer present in znode. */

	if (len <= 0) {
		if (!(flags & UPL_NOCOMMIT))
			(void) ubc_upl_abort(upl,
			    UPL_ABORT_DUMP_PAGES|UPL_ABORT_FREE_ON_EMPTY);
		err = EINVAL;
		goto exit;
	}
	if (vnode_vfsisrdonly(ZTOV(zp))) {
		if (!(flags & UPL_NOCOMMIT))
			ubc_upl_abort_range(upl, upl_offset, len,
			    UPL_ABORT_FREE_ON_EMPTY);
		err = EROFS;
		goto exit;
	}

	filesz = zp->z_size; /* get consistent copy of zp_size */

	if (off < 0 || off >= filesz || (off & PAGE_MASK_64) ||
	    (len & PAGE_MASK)) {
		if (!(flags & UPL_NOCOMMIT))
			ubc_upl_abort_range(upl, upl_offset, len,
			    UPL_ABORT_FREE_ON_EMPTY);
		err = EINVAL;
		goto exit;
	}

	uint64_t pgsize = roundup(filesz, PAGESIZE);

	/* Any whole pages beyond the end of the file while we abort */
	if ((size + off) > pgsize) {
		printf("ZFS: pageout abort outside pages (rounded 0x%llx > "
		    "UPLlen 0x%llx\n", pgsize, size + off);
		ubc_upl_abort_range(upl, pgsize,
		    pgsize - (size + off),
		    UPL_ABORT_FREE_ON_EMPTY);
	}

	dprintf("ZFS: starting with size %lx\n", len);

top:
	lr = zfs_rangelock_enter(&zp->z_rangelock, off, len, RL_WRITER);
	/*
	 * can't push pages passed end-of-file
	 */
	filesz = zp->z_size;
	if (off >= filesz) {
		/* ignore all pages */
		err = 0;
		goto out;
	} else if (off + len > filesz) {
#if 0
		int npages = btopr(filesz - off);
		page_t *trunc;

		page_list_break(&pp, &trunc, npages);
		/* ignore pages past end of file */
		if (trunc)
			pvn_write_done(trunc, flags);
#endif
		len = filesz - off;
	}

	tx = dmu_tx_create(zfsvfs->z_os);
	if (!tx) {
		dprintf("ZFS: zfs_vnops_osx: NULL TX encountered!\n");
		if (!(flags & UPL_NOCOMMIT))
			ubc_upl_abort_range(upl, upl_offset, len,
			    UPL_ABORT_FREE_ON_EMPTY);
		err = EINVAL;
		goto exit;
	}
	dmu_tx_hold_write(tx, zp->z_id, off, len);

	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	zfs_sa_upgrade_txholds(tx, zp);
	err = dmu_tx_assign(tx, TXG_WAIT);
	if (err != 0) {
		if (err == ERESTART) {
			zfs_rangelock_exit(lr);
			dmu_tx_wait(tx);
			dmu_tx_abort(tx);
			goto top;
		}
		dmu_tx_abort(tx);
		goto out;
	}

	caddr_t va;

	if (ubc_upl_map(upl, (vm_offset_t *)&va) != KERN_SUCCESS) {
		err = EINVAL;
		goto out;
	}

	va += upl_offset;
	while (len >= PAGESIZE) {
		ssize_t sz = PAGESIZE;

		dprintf("pageout: dmu_write off 0x%llx size 0x%lx\n", off, sz);

		dmu_write(zfsvfs->z_os, zp->z_id, off, sz, va, tx);
		va += sz;
		off += sz;
		len -= sz;
	}

	/*
	 * The last, possibly partial block needs to have the data zeroed that
	 * would extend past the size of the file.
	 */
	if (len > 0) {
		ssize_t sz = len;

		dprintf("pageout: dmu_writeX off 0x%llx size 0x%lx\n", off, sz);
		dmu_write(zfsvfs->z_os, zp->z_id, off, sz, va, tx);

		va += sz;
		off += sz;
		len -= sz;

		/*
		 * Zero out the remainder of the PAGE that didn't fit within
		 * the file size.
		 */
		// memset(va, 0, PAGESIZE-sz);
		// dprintf("zero last 0x%lx bytes.\n", PAGESIZE-sz);

	}
	ubc_upl_unmap(upl);

	if (err == 0) {
		uint64_t mtime[2], ctime[2];
		sa_bulk_attr_t bulk[3];
		int count = 0;

		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs), NULL,
		    &mtime, 16);
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL,
		    &ctime, 16);
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs), NULL,
		    &zp->z_pflags, 8);
		zfs_tstamp_update_setup(zp, CONTENT_MODIFIED, mtime, ctime);
		err = sa_bulk_update(zp->z_sa_hdl, bulk, count, tx);
		ASSERT0(err);
		zfs_log_write(zfsvfs->z_log, tx, TX_WRITE, zp, off, len, 0,
		    NULL, NULL);
	}
	dmu_tx_commit(tx);

out:
	zfs_rangelock_exit(lr);
	if (flags & UPL_IOSYNC)
		zil_commit(zfsvfs->z_log, zp->z_id);

	if (!(flags & UPL_NOCOMMIT)) {
		if (err)
			ubc_upl_abort_range(upl, upl_offset, size,
			    (UPL_ABORT_ERROR | UPL_ABORT_FREE_ON_EMPTY));
		else
			ubc_upl_commit_range(upl, upl_offset, size,
			    (UPL_COMMIT_CLEAR_DIRTY |
			    UPL_COMMIT_FREE_ON_EMPTY));
	}
exit:
	zfs_exit(zfsvfs, FTAG);
	if (err) dprintf("%s err %d\n", __func__, err);
	return (err);
}



int
zfs_vnop_pageout(struct vnop_pageout_args *ap)
#if 0
	struct vnop_pageout_args {
		struct vnode	*a_vp;
		upl_t		a_pl;
		vm_offset_t	a_pl_offset;
		off_t		a_foffset;
		size_t		a_size;
		int		a_flags;
		vfs_context_t	a_context;
	};
#endif
{
	struct vnode *vp = ap->a_vp;
	int flags = ap->a_flags;
	upl_t upl = ap->a_pl;
	vm_offset_t upl_offset = ap->a_pl_offset;
	size_t len = ap->a_size;
	offset_t off = ap->a_f_offset;
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = NULL;
	int error;

	if (!zp || !zp->z_zfsvfs) {
		if (!(flags & UPL_NOCOMMIT))
			ubc_upl_abort(upl,
			    (UPL_ABORT_DUMP_PAGES|UPL_ABORT_FREE_ON_EMPTY));
		dprintf("ZFS: vnop_pageout: null zp or zfsvfs\n");
		return (ENXIO);
	}

	zfsvfs = zp->z_zfsvfs;

	dprintf("+vnop_pageout: off 0x%llx len 0x%lx upl_off 0x%lx: "
	    "blksz 0x%x, z_size 0x%llx\n", off, len, upl_offset, zp->z_blksz,
	    zp->z_size);

	/*
	 * XXX Crib this too, although Apple uses parts of zfs_putapage().
	 * Break up that function into smaller bits so it can be reused.
	 */
	error = zfs_pageout(zfsvfs, zp, upl, upl_offset, ap->a_f_offset,
	    len, flags);

	return (error);
}


static int bluster_pageout(zfsvfs_t *zfsvfs, znode_t *zp, upl_t upl,
    upl_offset_t upl_offset, off_t f_offset, int size,
    uint64_t filesize, int flags, caddr_t vaddr,
    dmu_tx_t *tx)
{
	int		io_size;
	int		rounded_size;
	off_t	max_size;
	int		is_clcommit = 0;

	if ((flags & UPL_NOCOMMIT) == 0)
		is_clcommit = 1;

	/*
	 * If they didn't specify any I/O, then we are done...
	 * we can't issue an abort because we don't know how
	 * big the upl really is
	 */
	if (size <= 0) {
		dprintf("%s invalid size %d\n", __func__, size);
		return (EINVAL);
	}

	if (vnode_vfsisrdonly(ZTOV(zp))) {
		if (is_clcommit)
			ubc_upl_abort_range(upl, upl_offset, size,
			    UPL_ABORT_FREE_ON_EMPTY);
		dprintf("%s: readonly fs\n", __func__);
		return (EROFS);
	}

	/*
	 * can't page-in from a negative offset
	 * or if we're starting beyond the EOF
	 * or if the file offset isn't page aligned
	 * or the size requested isn't a multiple of PAGE_SIZE
	 */
	if (f_offset < 0 || f_offset >= filesize ||
	    (f_offset & PAGE_MASK_64) || (size & PAGE_MASK)) {
		if (is_clcommit)
			ubc_upl_abort_range(upl, upl_offset, size,
			    UPL_ABORT_FREE_ON_EMPTY);
		dprintf("%s: invalid offset or size\n", __func__);
		return (EINVAL);
	}
	max_size = filesize - f_offset;

	if (size < max_size)
		io_size = size;
	else
		io_size = max_size;

	rounded_size = (io_size + (PAGE_SIZE - 1)) & ~PAGE_MASK;

	if (size > rounded_size) {
		if (is_clcommit)
			ubc_upl_abort_range(upl, upl_offset + rounded_size,
			    size - rounded_size, UPL_ABORT_FREE_ON_EMPTY);
	}

#if 1
	if (f_offset + size > filesize) {
		dprintf("ZFS: lowering size %u to %llu\n",
		    size, f_offset > filesize ? 0 : filesize - f_offset);
		if (f_offset > filesize)
			size = 0;
		else
			size = filesize - f_offset;
	}
#endif

	dmu_write(zfsvfs->z_os, zp->z_id, f_offset, size,
	    &vaddr[upl_offset], tx);

	return (0);
}




/*
 * In V2 of vnop_pageout, we are given a NULL upl, so that we can
 * grab the file locks first, then request the upl to lock down pages.
 */
int
zfs_vnop_pageoutv2(struct vnop_pageout_args *ap)
#if 0
	struct vnop_pageout_args {
		struct vnode	*a_vp;
		upl_t		a_pl;
		vm_offset_t	a_pl_offset;
		off_t		a_foffset;
		size_t		a_size;
		int		a_flags;
		vfs_context_t	a_context;
	};
#endif
{
	struct vnode *vp = ap->a_vp;
	int a_flags = ap->a_flags;
	vm_offset_t	a_pl_offset = ap->a_pl_offset;
	size_t a_size = ap->a_size;
	upl_t upl = ap->a_pl;
	upl_page_info_t *pl;
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = NULL;
	int error = 0;
	uint64_t filesize;
	zfs_locked_range_t *lr;
	dmu_tx_t *tx;
	caddr_t vaddr = NULL;
	int merror = 0;

	/*
	 * We can still get into this function as non-v2 style, by the default
	 * pager (ie, swap - when we eventually support it)
	 */
	if (upl) {
		dprintf("ZFS: Relaying vnop_pageoutv2 to vnop_pageout\n");
		return (zfs_vnop_pageout(ap));
	}

	if (!zp || !zp->z_zfsvfs) {
		dprintf("ZFS: vnop_pageout: null zp or zfsvfs\n");
		return (ENXIO);
	}

	if (ZTOV(zp) == NULL) {
		dprintf("ZFS: vnop_pageout: null vp\n");
		return (ENXIO);
	}

	// XNU can call us with iocount == 0 && usecount == 0. Grab
	// a ref now so the vp doesn't reclaim while we are in here.
	if (vnode_get(ZTOV(zp)) != 0) {
		dprintf("ZFS: vnop_pageout: vnode_ref failed.\n");
		return (ENXIO);
	}

	mutex_enter(&zp->z_lock);

	sa_handle_t *z_sa_hdl;
	z_sa_hdl = zp->z_sa_hdl;
	if (!z_sa_hdl) {
		mutex_exit(&zp->z_lock);
		vnode_put(ZTOV(zp));
		dprintf("ZFS: vnop_pageout: null sa_hdl\n");
		return (ENXIO);
	}

	zfsvfs = zp->z_zfsvfs;

	mutex_exit(&zp->z_lock);

	if (error) {
		dprintf("ZFS: %s: can't hold_sa: %d\n", __func__, error);
		vnode_put(ZTOV(zp));
		return (ENXIO);
	}

	dprintf("+vnop_pageout2: off 0x%llx len 0x%lx upl_off 0x%lx: "
	    "blksz 0x%x, z_size 0x%llx\n", ap->a_f_offset, a_size,
	    a_pl_offset, zp->z_blksz,
	    zp->z_size);


	/* Start the pageout request */
	/*
	 * We can't leave this function without either calling upl_commit or
	 * upl_abort. So use the non-error version.
	 */
	if ((error = zfs_enter(zfsvfs, FTAG)) != 0) {
		dprintf("ZFS: vnop_pageoutv2: abort on z_unmounted\n");
		zfsvfs = NULL;
		error = EIO;
		goto exit_abort;
	}
	if (vfs_flags(zfsvfs->z_vfs) & MNT_RDONLY) {
		dprintf("ZFS: vnop_pageoutv2: readonly\n");
		error = EROFS;
		goto exit_abort;
	}

	lr = zfs_rangelock_enter(&zp->z_rangelock, ap->a_f_offset, a_size,
	    RL_WRITER);

	/* Grab UPL now */
	int request_flags;

	/*
	 * we're in control of any UPL we commit
	 * make sure someone hasn't accidentally passed in UPL_NOCOMMIT
	 */
	a_flags &= ~UPL_NOCOMMIT;
	a_pl_offset = 0;

	if (a_flags & UPL_MSYNC) {
		request_flags = UPL_UBC_MSYNC | UPL_RET_ONLY_DIRTY;
	} else {
		request_flags = UPL_UBC_PAGEOUT | UPL_RET_ONLY_DIRTY;
	}

	error = ubc_create_upl(vp, ap->a_f_offset, ap->a_size, &upl, &pl,
	    request_flags);
	if (error || (upl == NULL)) {
		dprintf("ZFS: Failed to create UPL! %d\n", error);
		goto pageout_done;
	}

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_write(tx, zp->z_id, ap->a_f_offset, ap->a_size);

	// NULL z_sa_hdl
	if (z_sa_hdl != NULL)
		dmu_tx_hold_sa(tx, z_sa_hdl, B_FALSE);

	zfs_sa_upgrade_txholds(tx, zp);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error != 0) {
		dmu_tx_abort(tx);
		ubc_upl_abort(upl, (UPL_ABORT_ERROR|UPL_ABORT_FREE_ON_EMPTY));
		goto pageout_done;
	}

	off_t f_offset;
	int64_t offset;
	int64_t isize;
	int64_t pg_index;

	filesize = zp->z_size; /* get consistent copy of zp_size */

	isize = ap->a_size;
	f_offset = ap->a_f_offset;

	/*
	 * Scan from the back to find the last page in the UPL, so that we
	 * aren't looking at a UPL that may have already been freed by the
	 * preceding aborts/completions.
	 */
	for (pg_index = ((isize) / PAGE_SIZE); pg_index > 0; ) {
		if (upl_page_present(pl, --pg_index))
			break;
		if (pg_index == 0) {
			dprintf("ZFS: failed on pg_index\n");
			dmu_tx_commit(tx);
			ubc_upl_abort_range(upl, 0, isize,
			    UPL_ABORT_FREE_ON_EMPTY);
			goto pageout_done;
		}
	}

	dprintf("ZFS: isize %llu pg_index %llu\n", isize, pg_index);
	/*
	 * initialize the offset variables before we touch the UPL.
	 * a_f_offset is the position into the file, in bytes
	 * offset is the position into the UPL, in bytes
	 * pg_index is the pg# of the UPL we're operating on.
	 * isize is the offset into the UPL of the last non-clean page.
	 */
	isize = ((pg_index + 1) * PAGE_SIZE);

	offset = 0;
	pg_index = 0;
	while (isize > 0) {
		int64_t  xsize;
		int64_t  num_of_pages;

		// printf("isize %d for page %d\n", isize, pg_index);

		if (!upl_page_present(pl, pg_index)) {
			/*
			 * we asked for RET_ONLY_DIRTY, so it's possible
			 * to get back empty slots in the UPL.
			 * just skip over them
			 */
			f_offset += PAGE_SIZE;
			offset   += PAGE_SIZE;
			isize    -= PAGE_SIZE;
			pg_index++;

			continue;
		}
		if (!upl_dirty_page(pl, pg_index)) {
			/*
			 * hfs has a call to panic here, but we trigger this
			 * *a lot* so unsure what is going on
			 */
			dprintf("zfs_vnop_pageoutv2: unforeseen clean page "
			    "@ index %lld for UPL %p\n", pg_index, upl);
			f_offset += PAGE_SIZE;
			offset   += PAGE_SIZE;
			isize    -= PAGE_SIZE;
			pg_index++;
			continue;
		}

		/*
		 * We know that we have at least one dirty page.
		 * Now checking to see how many in a row we have
		 */
		num_of_pages = 1;
		xsize = isize - PAGE_SIZE;

		while (xsize > 0) {
			if (!upl_dirty_page(pl, pg_index + num_of_pages))
				break;
			num_of_pages++;
			xsize -= PAGE_SIZE;
		}
		xsize = num_of_pages * PAGE_SIZE;

		if (!vnode_isswap(vp)) {
			off_t end_of_range;

			end_of_range = f_offset + xsize - 1;
			if (end_of_range >= filesize) {
				end_of_range = (off_t)(filesize - 1);
			}
		}

		// Map it if needed
		if (!vaddr) {
			if ((ubc_upl_map(upl, (vm_offset_t *)&vaddr) !=
				    KERN_SUCCESS) || vaddr == NULL) {
				error = EINVAL;
				vaddr = NULL;
				dprintf("ZFS: unable to map\n");
				goto out;
			}
			dprintf("ZFS: Mapped %p\n", vaddr);
		}


		dprintf("ZFS: bluster offset %lld fileoff %lld size %lld "
			"filesize %lld\n", offset, f_offset, xsize, filesize);
		merror = bluster_pageout(zfsvfs, zp, upl, offset, f_offset,
		    xsize, filesize, a_flags, vaddr, tx);
		/* remember the first error */
		if ((error == 0) && (merror))
			error = merror;

		f_offset += xsize;
		offset   += xsize;
		isize    -= xsize;
		pg_index += num_of_pages;
	} // while isize

	/* finish off transaction */
	if (error == 0) {
		uint64_t mtime[2], ctime[2];
		sa_bulk_attr_t bulk[3];
		int count = 0;

		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs), NULL,
		    &mtime, 16);
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL,
		    &ctime, 16);
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs), NULL,
		    &zp->z_pflags, 8);
		zfs_tstamp_update_setup(zp, CONTENT_MODIFIED, mtime, ctime);
		zfs_log_write(zfsvfs->z_log, tx, TX_WRITE, zp, ap->a_f_offset,
		    a_size, 0, NULL, NULL);
	}
	dmu_tx_commit(tx);

	// unmap
	if (vaddr) {
		ubc_upl_unmap(upl);
		vaddr = NULL;
	}
out:
	zfs_rangelock_exit(lr);
	if (a_flags & UPL_IOSYNC)
		zil_commit(zfsvfs->z_log, zp->z_id);

	if (error)
		ubc_upl_abort(upl, (UPL_ABORT_ERROR|UPL_ABORT_FREE_ON_EMPTY));
	else
		ubc_upl_commit_range(upl, 0, a_size, UPL_COMMIT_FREE_ON_EMPTY);

	upl = NULL;

	vnode_put(ZTOV(zp));

	zfs_exit(zfsvfs, FTAG);
	if (error)
		dprintf("ZFS: pageoutv2 failed %d\n", error);
	return (error);

pageout_done:
	zfs_rangelock_exit(lr);

exit_abort:
	dprintf("ZFS: pageoutv2 aborted %d\n", error);
	// VERIFY(ubc_create_upl(vp, off, len, &upl, &pl, flags) == 0);
	// ubc_upl_abort(upl, UPL_ABORT_FREE_ON_EMPTY);

	vnode_put(ZTOV(zp));

	if (zfsvfs)
		zfs_exit(zfsvfs, FTAG);
	return (error);
}






int
zfs_vnop_mmap(struct vnop_mmap_args *ap)
#if 0
	struct vnop_mmap_args {
		struct vnode	*a_vp;
		int		a_fflags;
		kauth_cred_t	a_cred;
		struct proc	*a_p;
	};
#endif
{
	struct vnode *vp = ap->a_vp;
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs;
	int error = 0;

	if (!zp)
		return (ENODEV);

	zfsvfs = zp->z_zfsvfs;

	dprintf("+vnop_mmap: %p\n", ap->a_vp);

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		goto out;

	if (!vnode_isreg(vp)) {
		error = ENODEV;
		goto out;
	}

	mutex_enter(&zp->z_lock);
	zp->z_is_mapped = 1;
	mutex_exit(&zp->z_lock);

out:
	zfs_exit(zfsvfs, FTAG);
	dprintf("-vnop_mmap\n");
	return (error);
}

int
zfs_vnop_mnomap(struct vnop_mnomap_args *ap)
#if 0
	struct vnop_mnomap_args {
		struct vnode	*a_vp;
		int		a_fflags;
		kauth_cred_t	a_cred;
		struct proc	*a_p;
	};
#endif
{
	struct vnode *vp = ap->a_vp;
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	int error = 0;

	dprintf("+vnop_mnomap: %p\n", ap->a_vp);

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		goto out;

	if (!vnode_isreg(vp)) {
		error = ENODEV;
		goto out;
	}
	mutex_enter(&zp->z_lock);
	/*
	 * If a file as been mmaped even once, it needs to keep "z_is_mapped"
	 * high because it will potentially keep pages in the UPL cache we need
	 * to update on writes. We can either drop the UPL pages here, or simply
	 * keep updating both places on zfs_write().
	 */
	/* zp->z_is_mapped = 0; */
	mutex_exit(&zp->z_lock);

out:
	zfs_exit(zfsvfs, FTAG);
	dprintf("-vnop_mnomap\n");
	return (error);
}

int
zfs_vnop_inactive(struct vnop_inactive_args *ap)
#if 0
	struct vnop_inactive_args {
		struct vnode	*a_vp;
		vfs_context_t	a_context;
	};
#endif
{
	struct vnode *vp = ap->a_vp;
	zfs_inactive(vp);
	return (0);
}



#ifdef _KERNEL
uint64_t vnop_num_reclaims = 0;
uint64_t vnop_num_vnodes = 0;
#endif


int
zfs_vnop_reclaim(struct vnop_reclaim_args *ap)
#if 0
	struct vnop_reclaim_args {
		struct vnode	*a_vp;
		vfs_context_t	a_context;
	};
#endif
{
	/*
	 * Care needs to be taken here, we may already have called reclaim
	 * from vnop_inactive, if so, very little needs to be done.
	 */

	struct vnode	*vp = ap->a_vp;
	znode_t	*zp = NULL;
	zfsvfs_t *zfsvfs = NULL;

	/* Destroy the vm object and flush associated pages. */
#ifndef __APPLE__
	vnode_destroy_vobject(vp);
#endif

	/* Already been released? */
	zp = VTOZ(vp);
	ASSERT(zp != NULL);
	dprintf("+vnop_reclaim zp %p/%p type %d\n", zp, vp, vnode_vtype(vp));
	if (!zp) goto out;

	zfsvfs = zp->z_zfsvfs;

	if (!zfsvfs) {
		dprintf("ZFS: vnop_reclaim with zfsvfs == NULL\n");
		return (0);
	}

	if (zfsctl_is_node(vp)) {
		dprintf("ZFS: vnop_reclaim with ctldir node\n");
		return (0);
	}

	ZTOV(zp) = NULL;

	/*
	 * Purge old data structures associated with the denode.
	 */
	vnode_clearfsnode(vp); /* vp->v_data = NULL */
	vnode_removefsref(vp); /* ADDREF from vnode_create */
	atomic_dec_64(&vnop_num_vnodes);

	dprintf("+vnop_reclaim zp %p/%p unlinked %d unmount "
		"%d sa_hdl %p\n", zp, vp, zp->z_unlinked,
	    zfsvfs->z_unmounted, zp->z_sa_hdl);

	rw_enter(&zfsvfs->z_teardown_inactive_lock, RW_READER);
	if (zp->z_sa_hdl == NULL) {
		zfs_znode_free(zp);
	} else {
		zfs_zinactive(zp);
		zfs_znode_free(zp);
	}
	rw_exit(&zfsvfs->z_teardown_inactive_lock);

#ifdef _KERNEL
	atomic_inc_64(&vnop_num_reclaims);
#endif

out:
	return (0);
}





int
zfs_vnop_mknod(struct vnop_mknod_args *ap)
#if 0
	struct vnop_mknod_args {
		struct vnode	*a_dvp;
		struct vnode	**a_vpp;
		struct componentname *a_cnp;
		struct vnode_vattr *vap;
		vfs_context_t	a_context;
	};
#endif
{
	struct vnop_create_args create_ap;
	int error;

	dprintf("%s\n", __func__);

	memset(&create_ap, 0, sizeof (struct vnop_create_args));

	create_ap.a_dvp = ap->a_dvp;
	create_ap.a_vpp = ap->a_vpp;
	create_ap.a_cnp = ap->a_cnp;
	create_ap.a_vap = ap->a_vap;
	create_ap.a_context = ap->a_context;

	error = zfs_vnop_create(&create_ap);
	if (error) dprintf("%s error %d\n", __func__, error);
	return (error);
}

int
zfs_vnop_allocate(struct vnop_allocate_args *ap)
#if 0
	struct vnop_allocate_args {
		struct vnode	*a_vp;
		off_t		a_length;
		u_int32_t	a_flags;
		off_t		*a_bytesallocated;
		off_t		a_offset;
		vfs_context_t	a_context;
	};
#endif
{
	struct vnode *vp = ap->a_vp;
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs;
	uint64_t wantedsize = 0, filesize = 0;
	int error = 0;

	dprintf("%s %llu %d %llu %llu: '%s'\n", __func__, ap->a_length,
	    ap->a_flags, (ap->a_bytesallocated ? *ap->a_bytesallocated : 0),
	    ap->a_offset, zp->z_name_cache);

	/*
	 * This code has been reverted:
	 * https://github.com/openzfsonosx/zfs/issues/631
	 * Most likely not correctly aligned, and too-large offsets.
	 */
	return (0);

	if (!zp || !zp->z_sa_hdl)
		return (ENODEV);

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);


//	*ap->a_bytesallocated = 0;

	if (!vnode_isreg(vp)) {
		zfs_exit(zfsvfs, FTAG);
		return (ENODEV);
	}

	filesize = zp->z_size;
	wantedsize = ap->a_length;

	if (ap->a_flags & ALLOCATEFROMPEOF)
		wantedsize += filesize;
	else if (ap->a_flags & ALLOCATEFROMVOL)
		/* blockhint = ap->a_offset / blocksize */  // yeah, no idea
		dprintf("%s: help, allocatefromvolume set?\n", __func__);

	dprintf("%s: filesize %llu wantedsize %llu\n", __func__,
		filesize, wantedsize);

	// If we are extending
	if (wantedsize > filesize) {

		error = zfs_freesp(zp, wantedsize, 0, FWRITE, B_TRUE);

		// If we are truncating, Apple claims this code is never called.
	} else if (wantedsize < filesize) {

		dprintf("%s: file shrinking branch taken?\n", __func__);

	}

	if (!error) {
		*(ap->a_bytesallocated) = wantedsize - filesize;
	}

	zfs_exit(zfsvfs, FTAG);
	dprintf("-%s: %d\n", __func__, error);
	return (error);
}

int
zfs_vnop_whiteout(struct vnop_whiteout_args *ap)
#if 0
	struct vnop_whiteout_args {
		struct vnode	*a_dvp;
		struct componentname *a_cnp;
		int		a_flags;
		vfs_context_t	a_context;
	};
#endif
{
	dprintf("vnop_whiteout: ENOTSUP\n");

	return (ENOTSUP);
}

int
zfs_vnop_pathconf(struct vnop_pathconf_args *ap)
#if 0
	struct vnop_pathconf_args {
		struct vnode	*a_vp;
		int		a_name;
		register_t	*a_retval;
		vfs_context_t	a_context;
	};
#endif
{
	int32_t  *valp = ap->a_retval;
	int error = 0;

	dprintf("+vnop_pathconf a_name %d\n", ap->a_name);

	switch (ap->a_name) {
	case _PC_LINK_MAX:
		*valp = INT_MAX;
		break;
	case _PC_PIPE_BUF:
		*valp = PIPE_BUF;
		break;
	case _PC_CHOWN_RESTRICTED:
		*valp = 200112;  /* POSIX */
		break;
	case _PC_NO_TRUNC:
		*valp = 200112;  /* POSIX */
		break;
	case _PC_NAME_MAX:
	case _PC_NAME_CHARS_MAX:
		*valp = ZAP_MAXNAMELEN - 1;  /* 255 */
		break;
	case _PC_PATH_MAX:
	case _PC_SYMLINK_MAX:
		*valp = PATH_MAX;  /* 1024 */
		break;
	case _PC_CASE_SENSITIVE:
	{
		znode_t *zp = VTOZ(ap->a_vp);
		*valp = 1;
		if (zp && zp->z_zfsvfs) {
			zfsvfs_t *zfsvfs = zp->z_zfsvfs;
			*valp = (zfsvfs->z_case == ZFS_CASE_SENSITIVE) ? 1 : 0;
		}
	}
		break;
	case _PC_CASE_PRESERVING:
		*valp = 1;
		break;
/*
 * OS X 10.6 does not define this.
 */
#ifndef	_PC_XATTR_SIZE_BITS
#define	_PC_XATTR_SIZE_BITS   26
#endif
/*
 * Even though ZFS has 64 bit limit on XATTR size, there would appear to be a
 * limit in SMB2 that the bit size returned has to be 18, or we will get an
 * error from most XATTR calls (STATUS_ALLOTTED_SPACE_EXCEEDED).
 */
#ifndef	AD_XATTR_SIZE_BITS
#define	AD_XATTR_SIZE_BITS 18
#endif
	case _PC_XATTR_SIZE_BITS:
		*valp = AD_XATTR_SIZE_BITS;
		break;
	case _PC_FILESIZEBITS:
		*valp = 64;
		break;
	default:
		printf("ZFS: unknown pathconf %d called.\n", ap->a_name);
		error = EINVAL;
	}

	if (error) dprintf("%s vp %p : %d\n", __func__, ap->a_vp, error);
	return (error);
}

int
zfs_vnop_getxattr(struct vnop_getxattr_args *ap)
#if 0
	struct vnop_getxattr_args {
		struct vnodeop_desc *a_desc;
		struct vnode	*a_vp;
		char		*a_name;
		struct uio	*a_uio;
		size_t		*a_size;
		int		a_options;
		vfs_context_t	a_context;
	};
#endif
{
	DECLARE_CRED(ap);
	struct vnode *vp = ap->a_vp;
	znode_t  *zp = VTOZ(vp);
	zfsvfs_t  *zfsvfs = zp->z_zfsvfs;
	ZFS_UIO_INIT_XNU(uio, ap->a_uio);
	zfs_uio_t local_uio = { 0 };
	struct iovec iov;
	u_int32_t local_finderinfo[8] = {0};
	boolean_t is_finderinfo = B_FALSE;
	int  error = 0;
	uint64_t resid = ap->a_uio ? zfs_uio_resid(uio) : 0;
	ssize_t retsize;

	dprintf("%s: vp %p: '%s'\n", __func__, ap->a_vp, ap->a_name);

	/* xattrs disabled? */
	if (zfsvfs->z_xattr == B_FALSE)
		return (ENOTSUP);

	/*
	 * We need to do some special work on the finderinfo xattr in XNU.
	 * So it is better to read it into local memory, modify and copyout
	 * at the end. "resid" is set if we are going to read the value in,
	 * ie, not the a_uio == NULL case to read the size required.
	 */
	if (resid &&
		memcmp(XATTR_FINDERINFO_NAME, ap->a_name,
			sizeof (XATTR_FINDERINFO_NAME)) == 0) {

		/* Must be 32 bytes */
		if (resid != sizeof (emptyfinfo)) {
			return (ERANGE);
		}

		iov.iov_base = (void *)local_finderinfo;
		iov.iov_len = resid;

		zfs_uio_iovec_init(&local_uio, &iov, 1, 0, UIO_SYSSPACE,
		    resid, 0);

		is_finderinfo = B_TRUE;
	}


	error = zpl_xattr_get(vp, ap->a_name,
	    is_finderinfo ? &local_uio : uio, &retsize, cr);

	if (error != 0)
		return (error);

	if (ap->a_size)
		*ap->a_size = retsize;

	if (is_finderinfo) {

		/* According to HFS zero out some fields */
		finderinfo_update((uint8_t *)local_finderinfo, zp);

		/* If FinderInfo is empty > it doesn't exist */
		if (memcmp(emptyfinfo, local_finderinfo,
		    sizeof (emptyfinfo)) == 0) {
			error = ENOATTR;
		} else {
			error = zfs_uiomove(local_finderinfo, resid,
			    UIO_READ, uio);
		}
	}


	dprintf("%s: return 0 size %ld\n", __func__, retsize);

	return (0);
}

int
zfs_vnop_setxattr(struct vnop_setxattr_args *ap)
#if 0
	struct vnop_setxattr_args {
		struct vnodeop_desc *a_desc;
		struct vnode	*a_vp;
		char		*a_name;
		struct uio	*a_uio;
		int		a_options;
		vfs_context_t	a_context;
	};
#endif
{
	DECLARE_CRED(ap);
	ZFS_UIO_INIT_XNU(uio, ap->a_uio);
	struct vnode *vp = ap->a_vp;
	znode_t  *zp = VTOZ(vp);
	zfsvfs_t  *zfsvfs = zp->z_zfsvfs;
	int  error = 0;
	zfs_uio_t local_uio = { 0 };
	struct iovec iov;
	u_int32_t local_finderinfo[8] = {0};
	boolean_t is_finderinfo = B_FALSE;

	dprintf("+setxattr vp %p '%s' (enabled: %d) resid %lu\n", ap->a_vp,
		ap->a_name, zfsvfs->z_xattr, zfs_uio_resid(uio));

	/* xattrs disabled? */
	if (zfsvfs->z_xattr == B_FALSE)
		return (ENOTSUP);

	if (ap->a_name == NULL || ap->a_name[0] == '\0')
		return (EINVAL);  /* invalid name */

	if (strlen(ap->a_name) >= ZAP_MAXNAMELEN)
		return (ENAMETOOLONG);

	/*
	 * We need to do special work on the finderinfo when writing, so
	 * copyin to local buffer, and modify before passing to lower
	 */
	if (memcmp(XATTR_FINDERINFO_NAME, ap->a_name,
			sizeof (XATTR_FINDERINFO_NAME)) == 0) {

		/* Must be 32 bytes */
		if (zfs_uio_resid(uio) != sizeof (emptyfinfo)) {
			return (ERANGE);
		}

		/* copyin finderinfo from userland */
		error = zfs_uiomove(local_finderinfo, sizeof (local_finderinfo),
		    UIO_WRITE, uio);

		/* According to HFS zero out some fields */
		finderinfo_update((uint8_t *)local_finderinfo, zp);

		/* If FinderInfo is empty > it doesn't exist - don't write */
		if (memcmp(emptyfinfo, local_finderinfo,
		    sizeof (emptyfinfo)) == 0) {

			/* But if there was one, delete it */
			(void) zpl_xattr_set(vp, ap->a_name, NULL, 0, cr);

			/* Pretend we wrote it fine. */
			return (0);

		}

		/* We read finderinfo, and possibly modified it, change uio */
		iov.iov_base = (void *)local_finderinfo;
		iov.iov_len = sizeof (local_finderinfo);

		zfs_uio_iovec_init(&local_uio, &iov, 1, 0, UIO_SYSSPACE,
			sizeof (local_finderinfo), 0);

		is_finderinfo = B_TRUE;
	}


	error = zpl_xattr_set(vp, ap->a_name,
	    is_finderinfo ? &local_uio : uio,
	    ap->a_options, cr);

	dprintf("zpl_xattr_set(%s) returned %d\n", ap->a_name, error);

	if (error < 0)
		return (-error);

	return (error);
}

int
zfs_vnop_removexattr(struct vnop_removexattr_args *ap)
#if 0
	struct vnop_removexattr_args {
		struct vnodeop_desc *a_desc;
		struct vnode	*a_vp;
		char		*a_name;
		int		a_options;
		vfs_context_t	a_context;
	};
#endif
{
	DECLARE_CRED(ap);
	struct vnode *vp = ap->a_vp;
	znode_t  *zp = VTOZ(vp);
	zfsvfs_t  *zfsvfs = zp->z_zfsvfs;
	int error = 0;

	dprintf("+removexattr vp %p '%s'\n", ap->a_vp, ap->a_name);

	/* xattrs disabled? */
	if (zfsvfs->z_xattr == B_FALSE)
		return (ENOTSUP);

	error = zpl_xattr_set(vp, ap->a_name, NULL, 0, cr);

	if (error < 0)
		return (-error);

	return (error);

}


int
zfs_vnop_listxattr(struct vnop_listxattr_args *ap)
#if 0
	struct vnop_listxattr_args {
		struct vnodeop_desc *a_desc;
		vnode_t a_vp;
		uio_t a_uio;
		size_t *a_size;
		int a_options;
		vfs_context_t a_context;
	};
#endif
{
	DECLARE_CRED(ap);
	ZFS_UIO_INIT_XNU(uio, ap->a_uio);
	struct vnode *vp = ap->a_vp;
	znode_t  *zp = VTOZ(vp);
	zfsvfs_t  *zfsvfs = zp->z_zfsvfs;
	int  error = 0;
	ssize_t retsize;

	dprintf("+listxattr vp %p: resid %lu\n", ap->a_vp, zfs_uio_resid(uio));

	/* xattrs disabled? */
	if (zfsvfs->z_xattr == B_FALSE) {
		return (EINVAL);
	}

	/*
	 * Call the Linux helper functions to deal with the xattr.
	 * Note they return negative errors; ie -ERANGE
	 * Positive for size of xattr.
	 */
	error = zpl_xattr_list(vp, uio, &retsize, cr);

	if (error != 0)
		return (error);

	if (ap->a_size != NULL)
		*ap->a_size = retsize;

	dprintf("%s: size %lu\n", __func__, retsize);
	return (0);
}

#ifdef HAVE_NAMED_STREAMS
int
zfs_vnop_getnamedstream(struct vnop_getnamedstream_args *ap)
#if 0
	struct vnop_getnamedstream_args {
		struct vnode	*a_vp;
		struct vnode	**a_svpp;
		char		*a_name;
	};
#endif
{
	DECLARE_CRED(ap);
	struct vnode *vp = ap->a_vp;
	struct vnode **svpp = ap->a_svpp;
	znode_t  *zp = VTOZ(vp);
	zfsvfs_t  *zfsvfs = zp->z_zfsvfs;
	struct componentname cn = { 0 };
	int  error = 0;
	znode_t *xdzp = NULL;
	znode_t *xzp = NULL;

	dprintf("+getnamedstream vp %p '%s': op %u\n", ap->a_vp, ap->a_name,
		ap->a_operation);

	*svpp = NULLVP;

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	/*
	 * Mac OS X only supports the "com.apple.ResourceFork" stream.
	 */
	if (memcmp(XATTR_RESOURCEFORK_NAME, ap->a_name,
	    sizeof (XATTR_RESOURCEFORK_NAME)) != 0) {
		error = ENOATTR;
		goto out;
	}

	/* Only regular files */
	if (!vnode_isreg(vp)) {
		error = EPERM;
		goto out;
	}

	/* Grab the hidden attribute directory vnode. */
	if ((error = zfs_get_xattrdir(zp, &xdzp, cr, 0)) != 0)
		goto out;

	cn.cn_namelen = strlen(ap->a_name) + 1;
	cn.cn_nameptr = (char *)kmem_zalloc(cn.cn_namelen, KM_SLEEP);

	/* Lookup the attribute name. */
	if ((error = zfs_dirlook(xdzp, (char *)ap->a_name, &xzp, 0, NULL,
	    &cn))) {
		if (error == ENOENT)
			error = ENOATTR;
	} else {
		*svpp = ZTOV(xzp);
	}

	kmem_free(cn.cn_nameptr, cn.cn_namelen);

out:
	if (xdzp)
		zrele(xdzp);

#if 0 // Disabled, not sure its required and empty vnodes are odd.
	/*
	 * If the lookup is NS_OPEN, they are accessing "..namedfork/rsrc"
	 * to which we should return 0 with empty vp to empty file.
	 * See hfs_vnop_getnamedstream()
	 */
	if ((error == ENOATTR) &&
		ap->a_operation == NS_OPEN) {

		if ((error = zfs_get_xattrdir(zp, &xdvp, cr,
		    CREATE_XATTR_DIR)) == 0) {
			/* Lookup or create the named attribute. */
			error = zpl_obtain_xattr(VTOZ(xdvp), ap->a_name,
			    VTOZ(vp)->z_mode, cr, ap->a_svpp,
			    ZNEW);
			vnode_put(xdvp);
		}
	}
#endif

	zfs_exit(zfsvfs, FTAG);
	if (error) dprintf("%s vp %p: error %d\n", __func__, ap->a_vp, error);
	return (error);
}

int
zfs_vnop_makenamedstream(struct vnop_makenamedstream_args *ap)
#if 0
	struct vnop_makenamedstream_args {
		struct vnode	*a_vp;
		struct vnode	**a_svpp;
		char		*a_name;
	};
#endif
{
	DECLARE_CRED(ap);
	struct vnode *vp = ap->a_vp;
	znode_t  *zp = VTOZ(vp);
	zfsvfs_t  *zfsvfs = zp->z_zfsvfs;
	struct componentname  cn;
	struct vnode_attr  vattr;
	int  error = 0;
	znode_t *xdzp = NULL;
	znode_t *xzp = NULL;

	dprintf("+makenamedstream vp %p: '%s'\n", ap->a_vp, ap->a_name);

	*ap->a_svpp = NULLVP;

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	/* Only regular files can have a resource fork stream. */
	if (!vnode_isreg(vp)) {
		error = EPERM;
		goto out;
	}

	/*
	 * Mac OS X only supports the "com.apple.ResourceFork" stream.
	 */
	if (memcmp(XATTR_RESOURCEFORK_NAME, ap->a_name,
	    sizeof (XATTR_RESOURCEFORK_NAME)) != 0) {
		error = ENOATTR;
		goto out;
	}

	/* Grab the hidden attribute directory vnode. */
	if ((error = zfs_get_xattrdir(zp, &xdzp, cr, CREATE_XATTR_DIR)))
		goto out;

	memset(&cn, 0, sizeof (cn));
	cn.cn_nameiop = CREATE;
	cn.cn_flags = ISLASTCN;
	cn.cn_nameptr = (char *)ap->a_name;
	cn.cn_namelen = strlen(cn.cn_nameptr);

	VATTR_INIT(&vattr);
	VATTR_SET(&vattr, va_type, VREG);
	VATTR_SET(&vattr, va_mode, VTOZ(vp)->z_mode & ~S_IFMT);

	error = zfs_create(xdzp, (char *)ap->a_name, &vattr, NONEXCL,
	    VTOZ(vp)->z_mode, &xzp, cr, 0, NULL, NULL);

	if (error == 0)
		*ap->a_svpp = ZTOV(xzp);

out:
	if (xdzp)
		zrele(xdzp);

	zfs_exit(zfsvfs, FTAG);
	if (error) dprintf("%s vp %p: error %d\n", __func__, ap->a_vp, error);
	return (error);
}

int
zfs_vnop_removenamedstream(struct vnop_removenamedstream_args *ap)
#if 0
	struct vnop_removenamedstream_args {
		struct vnode	*a_vp;
		struct vnode	**a_svpp;
		char		*a_name;
	};
#endif
{
	struct vnode *svp = ap->a_svp;
	znode_t *zp = VTOZ(svp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	int error = 0;

	dprintf("zfs_vnop_removenamedstream: %p '%s'\n",
		svp, ap->a_name);

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	/*
	 * Mac OS X only supports the "com.apple.ResourceFork" stream.
	 */
	if (memcmp(XATTR_RESOURCEFORK_NAME, ap->a_name,
	    sizeof (XATTR_RESOURCEFORK_NAME)) != 0) {
		error = ENOATTR;
		goto out;
	}

	/* ### MISING CODE ### */
	/*
	 * It turns out that even though APPLE uses makenamedstream() to
	 * create a stream, for example compression, they use vnop_removexattr
	 * to delete it, so this appears not in use.
	 */
	dprintf("zfs_vnop_removenamedstream\n");
	error = EPERM;
out:
	zfs_exit(zfsvfs, FTAG);
	return (error);
}
#endif /* HAVE_NAMED_STREAMS */

/*
 * The Darwin kernel's HFS+ appears to implement this by two methods,
 *
 * if (ap->a_options & FSOPT_EXCHANGE_DATA_ONLY) is set
 *	** Copy the data of the files over (including rsrc)
 *
 * if not set
 *	** exchange FileID between the two nodes, copy over vnode information
 *	   like that of *time records, uid/gid, flags, mode, linkcount,
 *	   finderinfo, c_desc, c_attr, c_flag, and cache_purge().
 *
 * This call is deprecated in 10.8
 */
int
zfs_vnop_exchange(struct vnop_exchange_args *ap)
#if 0
	struct vnop_exchange_args {
		struct vnode	*a_fvp;
		struct vnode	*a_tvp;
		int		a_options;
		vfs_context_t	a_context;
	};
#endif
{
	vnode_t *fvp = ap->a_fvp;
	vnode_t *tvp = ap->a_tvp;
	znode_t  *fzp;
	zfsvfs_t  *zfsvfs;
	int error = 0;

	/* The files must be on the same volume. */
	if (vnode_mount(fvp) != vnode_mount(tvp)) {
		dprintf("%s fvp and tvp not in same mountpoint\n",
		    __func__);
		return (EXDEV);
	}

	if (fvp == tvp) {
		dprintf("%s fvp == tvp\n", __func__);
		return (EINVAL);
	}

	/* Only normal files can be exchanged. */
	if (!vnode_isreg(fvp) || !vnode_isreg(tvp)) {
		dprintf("%s fvp or tvp is not a regular file\n",
		    __func__);
		return (EINVAL);
	}

	fzp = VTOZ(fvp);
	zfsvfs = fzp->z_zfsvfs;

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	/* ADD MISSING CODE HERE */

	zfs_exit(zfsvfs, FTAG);
	printf("vnop_exchange: ENOTSUP\n");
	return (ENOTSUP);
}

int
zfs_vnop_revoke(struct vnop_revoke_args *ap)
#if 0
	struct vnop_revoke_args {
		struct vnode	*a_vp;
		int		a_flags;
		vfs_context_t	a_context;
	};
#endif
{
	return (vn_revoke(ap->a_vp, ap->a_flags, ap->a_context));
}

int
zfs_vnop_blktooff(struct vnop_blktooff_args *ap)
#if 0
	struct vnop_blktooff_args {
		struct vnode	*a_vp;
		daddr64_t	a_lblkno;
		off_t		*a_offset;
	};
#endif
{
	dprintf("vnop_blktooff: 0\n");
	return (ENOTSUP);
}

int
zfs_vnop_offtoblk(struct vnop_offtoblk_args *ap)
#if 0
	struct vnop_offtoblk_args {
		struct vnode	*a_vp;
		off_t		a_offset;
		daddr64_t	*a_lblkno;
	};
#endif
{
	dprintf("+vnop_offtoblk\n");
	return (ENOTSUP);
}

int
zfs_vnop_blockmap(struct vnop_blockmap_args *ap)
#if 0
	struct vnop_blockmap_args {
		struct vnode	*a_vp;
		off_t		a_foffset;
		size_t		a_size;
		daddr64_t	*a_bpn;
		size_t		*a_run;
		void		*a_poff;
		int		a_flags;
};
#endif
{
	dprintf("+vnop_blockmap\n");
	return (ENOTSUP);

#if 0
	znode_t *zp;
	zfsvfs_t *zfsvfs;

	ASSERT(ap);
	ASSERT(ap->a_vp);
	ASSERT(ap->a_size);

	if (!ap->a_bpn) {
		return (0);
	}

	if (vnode_isdir(ap->a_vp)) {
		return (ENOTSUP);
	}

	zp = VTOZ(ap->a_vp);
	if (!zp)
		return (ENODEV);

	zfsvfs = zp->z_zfsvfs;
	if (!zfsvfs)
		return (ENODEV);

	/* Return full request size as contiguous */
	if (ap->a_run) {
		// *ap->a_run = ap->a_size;
		*ap->a_run = 0;
	}
	if (ap->a_poff) {
		*((int *)(ap->a_poff)) = 0;
		/*
		 * returning offset of -1 asks the
		 * caller to zero the ranges
		 */
		// *((int *)(ap->a_poff)) = -1;
	}
	*ap->a_bpn = 0;
//	*ap->a_bpn = (daddr64_t)(ap->a_foffset / zfsvfs->z_max_blksz);

	dprintf("%s ret %lu %d %llu\n", __func__,
	    ap->a_size, *((int *)(ap->a_poff)), *((uint64_t *)(ap->a_bpn)));

	return (0);
#endif
}

int
zfs_vnop_strategy(struct vnop_strategy_args *ap)
#if 0
	struct vnop_strategy_args {
		struct buf	*a_bp;
	};
#endif
{
	dprintf("vnop_strategy: 0\n");
	return (ENOTSUP);
}

int
zfs_vnop_select(struct vnop_select_args *ap)
#if 0
	struct vnop_select_args {
		struct vnode	*a_vp;
		int		a_which;
		int		a_fflags;
		kauth_cred_t	a_cred;
		void		*a_wql;
		struct proc	*a_p;
	};
#endif
{
	dprintf("vnop_select: 1\n");
	return (1);
}

#ifdef WITH_READDIRATTR
int
zfs_vnop_readdirattr(struct vnop_readdirattr_args *ap)
#if 0
	struct vnop_readdirattr_args {
		struct vnodeop_desc *a_desc;
		struct vnode	*a_vp;
		struct attrlist	*a_alist;
		struct uio	*a_uio;
		ulong_t		a_maxcount;
		ulong_t		a_options;
		ulong_t		*a_newstate;
		int		*a_eofflag;
		ulong_t		*a_actualcount;
		vfs_context_t	a_context;
	};
#endif
{
	struct vnode *vp = ap->a_vp;
	struct attrlist *alp = ap->a_alist;
	struct uio *uio = ap->a_uio;
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	zap_cursor_t zc;
	zap_attribute_t zap;
	attrinfo_t attrinfo;
	int maxcount = ap->a_maxcount;
	uint64_t offset = (uint64_t)uio_offset(uio);
	u_int32_t fixedsize;
	u_int32_t maxsize;
	u_int32_t attrbufsize;
	void *attrbufptr = NULL;
	void *attrptr;
	void *varptr;  /* variable-length storage area */
	boolean_t user64 = vfs_context_is64bit(ap->a_context);
	int prefetch = 0;
	int error = 0;

#if 0
	dprintf("+vnop_readdirattr\n");
#endif

	*(ap->a_actualcount) = 0;
	*(ap->a_eofflag) = 0;

	/*
	 * Check for invalid options or invalid uio.
	 */
	if (((ap->a_options & ~(FSOPT_NOINMEMUPDATE | FSOPT_NOFOLLOW)) != 0) ||
		(uio_resid(uio) <= 0) || (maxcount <= 0)) {
		dprintf("%s invalid argument\n");
		return (EINVAL);
	}
	/*
	 * Reject requests for unsupported attributes.
	 */
	if ((alp->bitmapcount != ZFS_ATTR_BIT_MAP_COUNT) ||
	    (alp->commonattr & ~ZFS_ATTR_CMN_VALID) ||
	    (alp->dirattr & ~ZFS_ATTR_DIR_VALID) ||
	    (alp->fileattr & ~ZFS_ATTR_FILE_VALID) ||
	    (alp->volattr != 0 || alp->forkattr != 0)) {
		dprintf("%s unsupported attr\n");
		return (EINVAL);
	}
	/*
	 * Check if we should prefetch znodes
	 */
	if ((alp->commonattr & ~ZFS_DIR_ENT_ATTRS) ||
		(alp->dirattr != 0) || (alp->fileattr != 0)) {
		prefetch = TRUE;
	}

	/*
	 * Setup a buffer to hold the packed attributes.
	 */
	fixedsize = sizeof (u_int32_t) + getpackedsize(alp, user64);
	maxsize = fixedsize;
	if (alp->commonattr & ATTR_CMN_NAME)
		maxsize += ZAP_MAXNAMELEN + 1;
	attrbufptr = (void*)kmem_zalloc(maxsize, KM_SLEEP);
	if (attrbufptr == NULL) {
		dprintf("%s kmem_zalloc failed\n");
		return (ENOMEM);
	}
	attrptr = attrbufptr;
	varptr = (char *)attrbufptr + fixedsize;

	attrinfo.ai_attrlist = alp;
	attrinfo.ai_varbufend = (char *)attrbufptr + maxsize;
	attrinfo.ai_context = ap->a_context;

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	/*
	 * Initialize the zap iterator cursor.
	 */

	if (offset <= 3) {
		/*
		 * Start iteration from the beginning of the directory.
		 */
		zap_cursor_init(&zc, zfsvfs->z_os, zp->z_id);
	} else {
		/*
		 * The offset is a serialized cursor.
		 */
		zap_cursor_init_serialized(&zc, zfsvfs->z_os, zp->z_id, offset);
	}

	while (1) {
		ino64_t objnum;
		enum vtype vtype = VNON;
		znode_t *tmp_zp = NULL;

		/*
		 * Note that the low 4 bits of the cookie returned by zap is
		 * always zero. This allows us to use the low nibble for
		 * "special" entries:
		 * We use 0 for '.', and 1 for '..' (ignored here).
		 * If this is the root of the filesystem, we use the offset 2
		 * for the *'.zfs' directory.
		 */
		if (offset <= 1) {
			offset = 2;
			continue;
		} else if (offset == 2 && zfs_show_ctldir(zp)) {
			(void) strlcpy(zap.za_name, ZFS_CTLDIR_NAME,
			    MAXNAMELEN);
			objnum = ZFSCTL_INO_ROOT;
			vtype = VDIR;
		} else {
			/*
			 * Grab next entry.
			 */
			if ((error = zap_cursor_retrieve(&zc, &zap))) {
				*(ap->a_eofflag) = (error == ENOENT);
				goto update;
			}

			if (zap.za_integer_length != 8 ||
				zap.za_num_integers != 1) {
				error = ENXIO;
				goto update;
			}

			objnum = ZFS_DIRENT_OBJ(zap.za_first_integer);
			vtype = DTTOVT(ZFS_DIRENT_TYPE(zap.za_first_integer));
			/* Check if vtype is MIA */
			if ((vtype == 0) && !prefetch && (alp->dirattr ||
			    alp->fileattr ||
			    (alp->commonattr & ATTR_CMN_OBJTYPE))) {
				prefetch = 1;
			}
		}

		/* Grab znode if required */
		if (prefetch) {
			dmu_prefetch(zfsvfs->z_os, objnum, 0, 0);
			if ((error = zfs_zget(zfsvfs, objnum, &tmp_zp)) == 0) {
				if (vtype == VNON) {
					/* SA_LOOKUP? */
					vtype = IFTOVT(tmp_zp->z_mode);
				}
			} else {
				tmp_zp = NULL;
				error = ENXIO;
				goto skip_entry;
				/*
				 * Currently ".zfs" entry is skipped, as we have
				 * no methods to pack that into the attrs (all
				 * helper functions take znode_t *, and .zfs is
				 * not a znode_t *). Add dummy .zfs code here if
				 * it is desirable to show .zfs in Finder.
				 */
			}
		}

		/*
		 * Setup for the next item's attribute list
		 */
		*((u_int32_t *)attrptr) = 0; /* byte count slot */
		attrptr = ((u_int32_t *)attrptr) + 1; /* fixed attr start */
		attrinfo.ai_attrbufpp = &attrptr;
		attrinfo.ai_varbufpp = &varptr;

		/*
		 * Pack entries into attribute buffer.
		 */
		if (alp->commonattr) {
			commonattrpack(&attrinfo, zfsvfs, tmp_zp, zap.za_name,
			    objnum, vtype, user64);
		}
		if (alp->dirattr && vtype == VDIR) {
			dirattrpack(&attrinfo, tmp_zp);
		}
		if (alp->fileattr && vtype != VDIR) {
			fileattrpack(&attrinfo, zfsvfs, tmp_zp);
		}
		/* All done with tmp znode. */
		if (prefetch && tmp_zp) {
			vnode_put(ZTOV(tmp_zp));
			tmp_zp = NULL;
		}
		attrbufsize = ((char *)varptr - (char *)attrbufptr);

		/*
		 * Make sure there's enough buffer space remaining.
		 */
		if (uio_resid(uio) < 0 ||
			attrbufsize > (u_int32_t)uio_resid(uio)) {
			break;
		} else {
			*((u_int32_t *)attrbufptr) = attrbufsize;
			error = uiomove((caddr_t)attrbufptr, attrbufsize,
			    UIO_READ, uio);
			if (error != 0)
				break;
			attrptr = attrbufptr;
			/* Point to variable-length storage */
			varptr = (char *)attrbufptr + fixedsize;
			*(ap->a_actualcount) += 1;

			/*
			 * Move to the next entry, fill in the previous offset.
			 */
		skip_entry:
			if ((offset > 2) || ((offset == 2) &&
			    !zfs_show_ctldir(zp))) {
				zap_cursor_advance(&zc);
				offset = zap_cursor_serialize(&zc);
			} else {
				offset += 1;
			}

			/* Termination checks */
			if (--maxcount <= 0 || uio_resid(uio) < 0 ||
			    (u_int32_t)uio_resid(uio) < (fixedsize +
			    ZAP_AVENAMELEN)) {
				break;
			}
		}
	}
update:
	zap_cursor_fini(&zc);

	if (attrbufptr) {
		kmem_free(attrbufptr, maxsize);
	}
	if (error == ENOENT) {
		error = 0;
	}
	ZFS_ACCESSTIME_STAMP(zfsvfs, zp);

	/* XXX newstate TBD */
	*ap->a_newstate = zp->z_atime[0] + zp->z_atime[1];
	uio_setoffset(uio, offset);

	zfs_exit(zfsvfs, FTAG);
	dprintf("-readdirattr: error %d\n", error);
	return (error);
}
#endif


#ifdef WITH_SEARCHFS
int
zfs_vnop_searchfs(struct vnop_searchfs_args *ap)
#if 0
	struct vnop_searchfs_args {
		struct vnodeop_desc *a_desc;
		struct vnode	*a_vp;
		void		*a_searchparams1;
		void		*a_searchparams2;
		struct attrlist	*a_searchattrs;
		ulong_t		a_maxmatches;
		struct timeval	*a_timelimit;
		struct attrlist	*a_returnattrs;
		ulong_t		*a_nummatches;
		ulong_t		a_scriptcode;
		ulong_t		a_options;
		struct uio	*a_uio;
		struct searchstate *a_searchstate;
		vfs_context_t	a_context;
	};
#endif
{
	printf("vnop_searchfs called, type %d\n", vnode_vtype(ap->a_vp));

	*(ap->a_nummatches) = 0;

	return (ENOTSUP);
}
#endif



/*
 * Predeclare these here so that the compiler assumes that this is an "old
 * style" function declaration that does not include arguments so that we won't
 * get type mismatch errors in the initializations that follow.
 */
static int zfs_inval(void);
static int zfs_isdir(void);

static int
zfs_inval()
{
	dprintf("ZFS: Bad vnop: returning EINVAL\n");
	return (EINVAL);
}

static int
zfs_isdir()
{
	dprintf("ZFS: Bad vnop: returning EISDIR\n");
	return (EISDIR);
}


#define	VOPFUNC int (*)(void *)

/* Directory vnode operations template */
int (**zfs_dvnodeops) (void *);
struct vnodeopv_entry_desc zfs_dvnodeops_template[] = {
	{&vnop_default_desc, 	(VOPFUNC)vn_default_error },
	{&vnop_lookup_desc,	(VOPFUNC)zfs_vnop_lookup},
	{&vnop_create_desc,	(VOPFUNC)zfs_vnop_create},
	{&vnop_whiteout_desc,	(VOPFUNC)zfs_vnop_whiteout},
	{&vnop_mknod_desc,	(VOPFUNC)zfs_vnop_mknod},
	{&vnop_open_desc,	(VOPFUNC)zfs_vnop_open},
	{&vnop_close_desc,	(VOPFUNC)zfs_vnop_close},
	{&vnop_access_desc,	(VOPFUNC)zfs_vnop_access},
	{&vnop_getattr_desc,	(VOPFUNC)zfs_vnop_getattr},
	{&vnop_setattr_desc,	(VOPFUNC)zfs_vnop_setattr},
	{&vnop_read_desc,	(VOPFUNC)zfs_isdir},
	{&vnop_write_desc,	(VOPFUNC)zfs_isdir},
	{&vnop_ioctl_desc,	(VOPFUNC)zfs_vnop_ioctl},
	{&vnop_select_desc,	(VOPFUNC)zfs_isdir},
	{&vnop_bwrite_desc, (VOPFUNC)zfs_isdir},
	{&vnop_fsync_desc,	(VOPFUNC)zfs_vnop_fsync},
	{&vnop_remove_desc,	(VOPFUNC)zfs_vnop_remove},
	{&vnop_link_desc,	(VOPFUNC)zfs_vnop_link},
	{&vnop_rename_desc,	(VOPFUNC)zfs_vnop_rename},
#if defined(MAC_OS_X_VERSION_10_12) &&        \
	(MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_12)
	{&vnop_renamex_desc,	(VOPFUNC)zfs_vnop_renamex},
#endif
	{&vnop_mkdir_desc,	(VOPFUNC)zfs_vnop_mkdir},
	{&vnop_rmdir_desc,	(VOPFUNC)zfs_vnop_rmdir},
	{&vnop_symlink_desc,	(VOPFUNC)zfs_vnop_symlink},
	{&vnop_readdir_desc,	(VOPFUNC)zfs_vnop_readdir},
	{&vnop_inactive_desc,	(VOPFUNC)zfs_vnop_inactive},
	{&vnop_reclaim_desc,	(VOPFUNC)zfs_vnop_reclaim},
	{&vnop_pathconf_desc,	(VOPFUNC)zfs_vnop_pathconf},
	{&vnop_revoke_desc,	(VOPFUNC)zfs_vnop_revoke},
	{&vnop_getxattr_desc,	(VOPFUNC)zfs_vnop_getxattr},
	{&vnop_setxattr_desc,	(VOPFUNC)zfs_vnop_setxattr},
	{&vnop_removexattr_desc, (VOPFUNC)zfs_vnop_removexattr},
	{&vnop_listxattr_desc,	(VOPFUNC)zfs_vnop_listxattr},
#ifdef WITH_READDIRATTR
	{&vnop_readdirattr_desc, (VOPFUNC)zfs_vnop_readdirattr},
#endif
#ifdef WITH_SEARCHFS
	{&vnop_searchfs_desc,	(VOPFUNC)zfs_vnop_searchfs},
#endif
	{NULL, (VOPFUNC)NULL }
};
struct vnodeopv_desc zfs_dvnodeop_opv_desc =
{ &zfs_dvnodeops, zfs_dvnodeops_template };

/* Regular file vnode operations template */
int (**zfs_fvnodeops) (void *);
struct vnodeopv_entry_desc zfs_fvnodeops_template[] = {
	{&vnop_default_desc, 	(VOPFUNC)vn_default_error },
	{&vnop_whiteout_desc,	(VOPFUNC)zfs_vnop_whiteout},
	{&vnop_open_desc,	(VOPFUNC)zfs_vnop_open},
	{&vnop_close_desc,	(VOPFUNC)zfs_vnop_close},
	{&vnop_access_desc,	(VOPFUNC)zfs_vnop_access},
	{&vnop_getattr_desc,	(VOPFUNC)zfs_vnop_getattr},
	{&vnop_setattr_desc,	(VOPFUNC)zfs_vnop_setattr},
	{&vnop_read_desc,	(VOPFUNC)zfs_vnop_read},
	{&vnop_write_desc,	(VOPFUNC)zfs_vnop_write},
	{&vnop_ioctl_desc,	(VOPFUNC)zfs_vnop_ioctl},
	{&vnop_select_desc,	(VOPFUNC)zfs_vnop_select},
	{&vnop_fsync_desc,	(VOPFUNC)zfs_vnop_fsync},
	{&vnop_inactive_desc,	(VOPFUNC)zfs_vnop_inactive},
	{&vnop_reclaim_desc,	(VOPFUNC)zfs_vnop_reclaim},
	{&vnop_pathconf_desc,	(VOPFUNC)zfs_vnop_pathconf},
	{&vnop_bwrite_desc, (VOPFUNC)zfs_inval},
	{&vnop_pagein_desc,	(VOPFUNC)zfs_vnop_pagein},
#if	HAVE_PAGEOUT_V2
	{&vnop_pageout_desc,	(VOPFUNC)zfs_vnop_pageoutv2},
#else
	{&vnop_pageout_desc,	(VOPFUNC)zfs_vnop_pageout},
#endif
	{&vnop_mmap_desc,	(VOPFUNC)zfs_vnop_mmap},
	{&vnop_mnomap_desc,	(VOPFUNC)zfs_vnop_mnomap},
	{&vnop_blktooff_desc,	(VOPFUNC)zfs_vnop_blktooff},
	{&vnop_offtoblk_desc,	(VOPFUNC)zfs_vnop_offtoblk},
	{&vnop_blockmap_desc,	(VOPFUNC)zfs_vnop_blockmap},
	{&vnop_strategy_desc,	(VOPFUNC)zfs_vnop_strategy},
	{&vnop_allocate_desc,   (VOPFUNC)zfs_vnop_allocate},
	{&vnop_revoke_desc,	(VOPFUNC)zfs_vnop_revoke},
	{&vnop_exchange_desc,	(VOPFUNC)zfs_vnop_exchange},
	{&vnop_getxattr_desc,	(VOPFUNC)zfs_vnop_getxattr},
	{&vnop_setxattr_desc,	(VOPFUNC)zfs_vnop_setxattr},
	{&vnop_removexattr_desc, (VOPFUNC)zfs_vnop_removexattr},
	{&vnop_listxattr_desc,	(VOPFUNC)zfs_vnop_listxattr},
#ifdef HAVE_NAMED_STREAMS
	{&vnop_getnamedstream_desc,	(VOPFUNC)zfs_vnop_getnamedstream},
	{&vnop_makenamedstream_desc,	(VOPFUNC)zfs_vnop_makenamedstream},
	{&vnop_removenamedstream_desc,	(VOPFUNC)zfs_vnop_removenamedstream},
#endif
#ifdef WITH_SEARCHFS
	{&vnop_searchfs_desc,	(VOPFUNC)zfs_vnop_searchfs},
#endif
	{NULL, (VOPFUNC)NULL }
};
struct vnodeopv_desc zfs_fvnodeop_opv_desc =
{ &zfs_fvnodeops, zfs_fvnodeops_template };

/* Symbolic link vnode operations template */
int (**zfs_symvnodeops) (void *);
struct vnodeopv_entry_desc zfs_symvnodeops_template[] = {
	{&vnop_default_desc, 	(VOPFUNC)vn_default_error },
	{&vnop_open_desc,	(VOPFUNC)zfs_vnop_open},
	{&vnop_close_desc,	(VOPFUNC)zfs_vnop_close},
	{&vnop_access_desc,	(VOPFUNC)zfs_vnop_access},
	{&vnop_getattr_desc,	(VOPFUNC)zfs_vnop_getattr},
	{&vnop_setattr_desc,	(VOPFUNC)zfs_vnop_setattr},
	{&vnop_ioctl_desc,	(VOPFUNC)zfs_vnop_ioctl},
	{&vnop_readlink_desc,	(VOPFUNC)zfs_vnop_readlink},
	{&vnop_inactive_desc,	(VOPFUNC)zfs_vnop_inactive},
	{&vnop_reclaim_desc,	(VOPFUNC)zfs_vnop_reclaim},
	{&vnop_pathconf_desc,	(VOPFUNC)zfs_vnop_pathconf},
	{&vnop_revoke_desc,	(VOPFUNC)zfs_vnop_revoke},
	{&vnop_getxattr_desc,	(VOPFUNC)zfs_vnop_getxattr},
	{&vnop_setxattr_desc,	(VOPFUNC)zfs_vnop_setxattr},
	{&vnop_removexattr_desc, (VOPFUNC)zfs_vnop_removexattr},
	{&vnop_listxattr_desc,	(VOPFUNC)zfs_vnop_listxattr},
	{NULL, (VOPFUNC)NULL }
};
struct vnodeopv_desc zfs_symvnodeop_opv_desc =
{ &zfs_symvnodeops, zfs_symvnodeops_template };

/* Extended attribtue directory vnode operations template */
int (**zfs_xdvnodeops) (void *);
struct vnodeopv_entry_desc zfs_xdvnodeops_template[] = {
	{&vnop_default_desc, 	(VOPFUNC)vn_default_error },
	{&vnop_lookup_desc,	(VOPFUNC)zfs_vnop_lookup},
	{&vnop_create_desc,	(VOPFUNC)zfs_vnop_create},
	{&vnop_whiteout_desc,	(VOPFUNC)zfs_vnop_whiteout},
	{&vnop_mknod_desc,	(VOPFUNC)zfs_inval},
	{&vnop_open_desc,	(VOPFUNC)zfs_vnop_open},
	{&vnop_close_desc,	(VOPFUNC)zfs_vnop_close},
	{&vnop_access_desc,	(VOPFUNC)zfs_vnop_access},
	{&vnop_getattr_desc,	(VOPFUNC)zfs_vnop_getattr},
	{&vnop_setattr_desc,	(VOPFUNC)zfs_vnop_setattr},
	{&vnop_read_desc,	(VOPFUNC)zfs_vnop_read},
	{&vnop_write_desc,	(VOPFUNC)zfs_vnop_write},
	{&vnop_ioctl_desc,	(VOPFUNC)zfs_vnop_ioctl},
	{&vnop_select_desc,	(VOPFUNC)zfs_vnop_select},
	{&vnop_fsync_desc,	(VOPFUNC)zfs_vnop_fsync},
	{&vnop_remove_desc,	(VOPFUNC)zfs_vnop_remove},
	{&vnop_link_desc,	(VOPFUNC)zfs_vnop_link},
	{&vnop_rename_desc,	(VOPFUNC)zfs_vnop_rename},
	{&vnop_mkdir_desc,	(VOPFUNC)zfs_inval},
	{&vnop_rmdir_desc,	(VOPFUNC)zfs_vnop_rmdir},
	{&vnop_symlink_desc,	(VOPFUNC)zfs_inval},
	{&vnop_readdir_desc,	(VOPFUNC)zfs_vnop_readdir},
	{&vnop_inactive_desc,	(VOPFUNC)zfs_vnop_inactive},
	{&vnop_reclaim_desc,	(VOPFUNC)zfs_vnop_reclaim},
	{&vnop_pathconf_desc,	(VOPFUNC)zfs_vnop_pathconf},
	{NULL, (VOPFUNC)NULL }
};
struct vnodeopv_desc zfs_xdvnodeop_opv_desc =
{ &zfs_xdvnodeops, zfs_xdvnodeops_template };

/* Error vnode operations template */
int (**zfs_evnodeops) (void *);
struct vnodeopv_entry_desc zfs_evnodeops_template[] = {
	{&vnop_default_desc, 	(VOPFUNC)vn_default_error },
	{&vnop_inactive_desc,	(VOPFUNC)zfs_vnop_inactive},
	{&vnop_reclaim_desc,	(VOPFUNC)zfs_vnop_reclaim},
	{&vnop_pathconf_desc,	(VOPFUNC)zfs_vnop_pathconf},
	{NULL, (VOPFUNC)NULL }
};
struct vnodeopv_desc zfs_evnodeop_opv_desc =
{ &zfs_evnodeops, zfs_evnodeops_template };

int (**zfs_fifonodeops)(void *);
struct vnodeopv_entry_desc zfs_fifonodeops_template[] = {
	{ &vnop_default_desc, (VOPFUNC)vn_default_error },
	{ &vnop_lookup_desc, (VOPFUNC)fifo_lookup },
	{ &vnop_create_desc, (VOPFUNC)fifo_create },
	{ &vnop_mknod_desc, (VOPFUNC)fifo_mknod },
	{ &vnop_open_desc, (VOPFUNC)fifo_open },
	{ &vnop_close_desc, (VOPFUNC)fifo_close },
	{ &vnop_getattr_desc, (VOPFUNC)zfs_vnop_getattr },
	{ &vnop_setattr_desc, (VOPFUNC)zfs_vnop_setattr },
	{ &vnop_read_desc, (VOPFUNC)fifo_read },
	{ &vnop_write_desc, (VOPFUNC)fifo_write },
	{ &vnop_ioctl_desc, (VOPFUNC)fifo_ioctl },
	{ &vnop_select_desc, (VOPFUNC)fifo_select },
	{ &vnop_revoke_desc, (VOPFUNC)fifo_revoke },
	{ &vnop_mmap_desc, (VOPFUNC)fifo_mmap },
	{ &vnop_fsync_desc, (VOPFUNC)zfs_vnop_fsync },
	{ &vnop_remove_desc, (VOPFUNC)fifo_remove },
	{ &vnop_link_desc, (VOPFUNC)fifo_link },
	{ &vnop_rename_desc, (VOPFUNC)fifo_rename },
	{ &vnop_mkdir_desc, (VOPFUNC)fifo_mkdir },
	{ &vnop_rmdir_desc, (VOPFUNC)fifo_rmdir },
	{ &vnop_symlink_desc, (VOPFUNC)fifo_symlink },
	{ &vnop_readdir_desc, (VOPFUNC)fifo_readdir },
	{ &vnop_readlink_desc, (VOPFUNC)fifo_readlink },
	{ &vnop_inactive_desc, (VOPFUNC)zfs_vnop_inactive },
	{ &vnop_reclaim_desc, (VOPFUNC)zfs_vnop_reclaim },
	{ &vnop_strategy_desc, (VOPFUNC)fifo_strategy },
	{ &vnop_pathconf_desc, (VOPFUNC)fifo_pathconf },
	{ &vnop_advlock_desc, (VOPFUNC)err_advlock },
	{ &vnop_bwrite_desc, (VOPFUNC)zfs_inval },
	{ &vnop_pagein_desc, (VOPFUNC)zfs_vnop_pagein },
#if	HAVE_PAGEOUT_V2
	{ &vnop_pageout_desc, (VOPFUNC)zfs_vnop_pageoutv2 },
#else
	{ &vnop_pageout_desc, (VOPFUNC)zfs_vnop_pageout },
#endif
	{ &vnop_copyfile_desc, (VOPFUNC)err_copyfile },
	{ &vnop_blktooff_desc, (VOPFUNC)zfs_vnop_blktooff },
	{ &vnop_offtoblk_desc, (VOPFUNC)zfs_vnop_offtoblk },
	{ &vnop_blockmap_desc, (VOPFUNC)zfs_vnop_blockmap },
	{ &vnop_getxattr_desc, (VOPFUNC)zfs_vnop_getxattr},
	{ &vnop_setxattr_desc, (VOPFUNC)zfs_vnop_setxattr},
	{ &vnop_removexattr_desc, (VOPFUNC)zfs_vnop_removexattr},
	{ &vnop_listxattr_desc, (VOPFUNC)zfs_vnop_listxattr},
	{ (struct vnodeop_desc *)NULL, (VOPFUNC)NULL }
};
struct vnodeopv_desc zfs_fifonodeop_opv_desc =
	{ &zfs_fifonodeops, zfs_fifonodeops_template };


/*
 * .zfs/snapdir vnops
 */
int (**zfs_ctldirops) (void *);
struct vnodeopv_entry_desc zfs_ctldir_template[] = {
	{&vnop_default_desc, 	(VOPFUNC)vn_default_error },
	{&vnop_lookup_desc,		(VOPFUNC)zfsctl_vnop_lookup},
	{&vnop_getattr_desc,	(VOPFUNC)zfsctl_vnop_getattr},
	{&vnop_readdir_desc,	(VOPFUNC)zfsctl_vnop_readdir},
	{&vnop_mkdir_desc,		(VOPFUNC)zfsctl_vnop_mkdir},
	{&vnop_rmdir_desc,		(VOPFUNC)zfsctl_vnop_rmdir},
	/* We also need to define these for the top ones to work */
	{&vnop_open_desc,		(VOPFUNC)zfsctl_vnop_open},
	{&vnop_close_desc,		(VOPFUNC)zfsctl_vnop_close},
	{&vnop_access_desc,		(VOPFUNC)zfsctl_vnop_access},
	{&vnop_inactive_desc,	(VOPFUNC)zfsctl_vnop_inactive},
	{&vnop_reclaim_desc,	(VOPFUNC)zfsctl_vnop_reclaim},
	{&vnop_revoke_desc,		(VOPFUNC)err_revoke},
	{&vnop_fsync_desc,		(VOPFUNC)nop_fsync},
	{NULL, (VOPFUNC)NULL }
};
struct vnodeopv_desc zfs_ctldir_opv_desc =
{ &zfs_ctldirops, zfs_ctldir_template };

/*
 * Get new vnode for znode.
 *
 * This function uses zp->z_zfsvfs, zp->z_mode, zp->z_flags, zp->z_id and sets
 * zp->z_vnode and zp->z_vid.
 */
int
zfs_znode_getvnode(znode_t *zp, zfsvfs_t *zfsvfs)
{
	struct vnode_fsparam vfsp;
	struct vnode *vp = NULL;

	dprintf("getvnode zp %p with vp %p zfsvfs %p vfs %p\n", zp, vp,
	    zfsvfs, zfsvfs->z_vfs);

	if (zp->z_vnode)
		panic("zp %p vnode already set\n", zp->z_vnode);

	memset(&vfsp, 0, sizeof (vfsp));
	vfsp.vnfs_str = "zfs";
	vfsp.vnfs_mp = zfsvfs->z_vfs;
	vfsp.vnfs_vtype = IFTOVT((mode_t)zp->z_mode);
	vfsp.vnfs_fsnode = zp;
	vfsp.vnfs_flags = VNFS_ADDFSREF;

	/* Tag root directory */
	if (zp->z_id == zfsvfs->z_root) {
		vfsp.vnfs_markroot = 1;
	}

	switch (vfsp.vnfs_vtype) {
	case VDIR:
		if (zp->z_pflags & ZFS_XATTR) {
			vfsp.vnfs_vops = zfs_xdvnodeops;
		} else {
			vfsp.vnfs_vops = zfs_dvnodeops;
		}
		zp->z_zn_prefetch = B_TRUE; /* z_prefetch default is enabled */
		break;
	case VBLK:
	case VCHR:
		{
			uint64_t rdev;
			VERIFY(sa_lookup(zp->z_sa_hdl, SA_ZPL_RDEV(zfsvfs),
			    &rdev, sizeof (rdev)) == 0);

			vfsp.vnfs_rdev = zfs_cmpldev(rdev);
		}
		/* FALLTHROUGH */
	case VSOCK:
		vfsp.vnfs_vops = zfs_fvnodeops;
		break;
	case VFIFO:
		vfsp.vnfs_vops = zfs_fifonodeops;
		break;
	case VREG:
		vfsp.vnfs_vops = zfs_fvnodeops;
		vfsp.vnfs_filesize = zp->z_size;
		break;
	case VLNK:
		vfsp.vnfs_vops = zfs_symvnodeops;
#if 0
		vfsp.vnfs_filesize = ???;
#endif
		break;
	default:
		vfsp.vnfs_vops = zfs_fvnodeops;
		printf("ZFS: Warning, error-vnops selected: vtype %d\n",
		    vfsp.vnfs_vtype);
		break;
	}

	/*
	 * vnode_create() has a habit of calling both vnop_reclaim() and
	 * vnop_fsync(), which can create havok as we are already holding locks.
	 */

	while (vnode_create(VNCREATE_FLAVOR, VCREATESIZE, &vfsp, &vp) != 0) {
		kpreempt(KPREEMPT_SYNC);
	}
	atomic_inc_64(&vnop_num_vnodes);

	dprintf("Assigned zp %p with vp %p zfsvfs %p\n", zp, vp, zp->z_zfsvfs);

	/*
	 * Unfortunately, when it comes to IOCTL_GET_BOOT_INFO and getting
	 * the volume finderinfo, XNU checks the tags, and only acts on
	 * HFS. So we have to set it to HFS on the root. It is pretty gross
	 * but until XNU adds supporting code..
	 * We no longer use tags in ZFS.
	 */
	if (zp->z_id == zfsvfs->z_root)
		vnode_settag(vp, VT_HFS);
	else
		vnode_settag(vp, VT_ZFS);

	zp->z_vid = vnode_vid(vp);
	zp->z_vnode = vp;

	/*
	 * OS X Finder is hardlink agnostic, so we need to mark vp's that
	 * are hardlinks, so that it forces a lookup each time, ignoring
	 * the name cache.
	 */
	if ((zp->z_links > 1) && (IFTOVT((mode_t)zp->z_mode) == VREG))
		vnode_setmultipath(vp);

	return (0);
}


/*
 * Called by taskq, to call zfs_znode_getvnode( vnode_create( - and
 * attach vnode to znode.
 */
void
zfs_znode_asyncgetvnode_impl(void *arg)
{
	znode_t *zp = (znode_t *)arg;
	VERIFY3P(zp, !=, NULL);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	VERIFY3P(zfsvfs, !=, NULL);

	// Attach vnode, done as different thread
	zfs_znode_getvnode(zp, zfsvfs);

	// Wake up anyone blocked on us
	mutex_enter(&zp->z_attach_lock);
	taskq_init_ent(&zp->z_attach_taskq);
	cv_broadcast(&zp->z_attach_cv);
	mutex_exit(&zp->z_attach_lock);

}


/*
 * If the znode's vnode is not yet attached (zp->z_vnode == NULL)
 * we call taskq_wait to wait for it to complete.
 * We guarantee znode has a vnode at the return of function only
 * when return is "0". On failure to wait, it returns -1, and caller
 * may consider waiting by other means.
 */
int
zfs_znode_asyncwait(zfsvfs_t *zfsvfs, znode_t *zp)
{
	int ret = -1;
	int error = 0;

	if (zp == NULL)
		return (ret);

	if (zfsvfs == NULL)
		return (ret);

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (ret);

	if (zfsvfs->z_os == NULL)
		goto out;

	// Work out if we need to block, that is, we have
	// no vnode AND a taskq was launched. Unsure if we should
	// look inside taskqent node like this.
	mutex_enter(&zp->z_attach_lock);
	if (zp->z_vnode == NULL &&
	    zp->z_attach_taskq.tqent_func != NULL) {
		// We need to block and wait for taskq to finish.
		cv_wait(&zp->z_attach_cv, &zp->z_attach_lock);
		ret = 0;
	}
	mutex_exit(&zp->z_attach_lock);

out:
	zfs_exit(zfsvfs, FTAG);
	return (ret);
}

/*
 * Called in place of VN_RELE() for the places that uses ZGET_FLAG_ASYNC.
 */
void
zfs_znode_asyncput_impl(znode_t *zp)
{
	// Make sure the other thread finished zfs_znode_getvnode();
	// This may block, if waiting is required.
	zfs_znode_asyncwait(zp->z_zfsvfs, zp);

	// Safe to release now that it is attached.
	VN_RELE(ZTOV(zp));

}

/*
 * Called in place of VN_RELE() for the places that uses ZGET_FLAG_ASYNC,
 * where we also taskq it - as we come from reclaim.
 */
void
zfs_znode_asyncput(znode_t *zp)
{
	dsl_pool_t *dp = dmu_objset_pool(zp->z_zfsvfs->z_os);
	taskq_t *tq = dsl_pool_zrele_taskq(dp);
	vnode_t *vp = ZTOV(zp);

	VERIFY3P(tq, !=, NULL);

	/* If iocount > 1, AND, vp is set (not async_get) */
	if (vp != NULL && vnode_iocount(vp) > 1) {
		VN_RELE(vp);
		return;
	}

	VERIFY(taskq_dispatch(
	    (taskq_t *)tq,
	    (task_func_t *)zfs_znode_asyncput_impl, zp, TQ_SLEEP) != 0);
}

/*
 * Attach a new vnode to the znode asynchronically. We do this using
 * a taskq to call it, and then wait to release the iocount.
 * Called of zget_ext(..., ZGET_FLAG_ASYNC); will use
 * zfs_znode_asyncput(zp) instead of VN_RELE(vp).
 */
int
zfs_znode_asyncgetvnode(znode_t *zp, zfsvfs_t *zfsvfs)
{
	VERIFY(zp != NULL);
	VERIFY(zfsvfs != NULL);

	// We should not have a vnode here.
	VERIFY3P(ZTOV(zp), ==, NULL);

	dsl_pool_t *dp = dmu_objset_pool(zfsvfs->z_os);
	taskq_t *tq = dsl_pool_zrele_taskq(dp);
	VERIFY3P(tq, !=, NULL);

	mutex_enter(&zp->z_attach_lock);
	taskq_dispatch_ent(tq,
	    (task_func_t *)zfs_znode_asyncgetvnode_impl,
	    zp,
	    TQ_SLEEP,
	    &zp->z_attach_taskq);
	mutex_exit(&zp->z_attach_lock);
	return (0);
}



/*
 * Maybe these should live in vfsops
 */
int
zfs_vfsops_init(void)
{
	struct vfs_fsentry vfe;

	vnop_lookup_cache = kmem_cache_create("zfs_vnop_lookup",
	    MAXPATHLEN, 0, NULL, NULL, NULL, NULL, NULL, 0);

	/* Start thread to notify Finder of changes */
	zfs_start_notify_thread();

	vfe.vfe_vfsops = &zfs_vfsops_template;
	vfe.vfe_vopcnt = ZFS_VNOP_TBL_CNT;
	vfe.vfe_opvdescs = zfs_vnodeop_opv_desc_list;

	strlcpy(vfe.vfe_fsname, "zfs", MFSNAMELEN);

	/*
	 * Note: must set VFS_TBLGENERICMNTARGS with VFS_TBLLOCALVOL
	 * to suppress local mount argument handling.
	 */
	vfe.vfe_flags = VFS_TBLTHREADSAFE | VFS_TBLNOTYPENUM | VFS_TBLLOCALVOL |
	    VFS_TBL64BITREADY | VFS_TBLNATIVEXATTR | VFS_TBLGENERICMNTARGS |
	    VFS_TBLREADDIR_EXTENDED;

#if	HAVE_PAGEOUT_V2
	vfe.vfe_flags |= VFS_TBLVNOP_PAGEOUTV2;
#endif

#ifdef VFS_TBLCANMOUNTROOT  // From 10.12
	vfe.vfe_flags |= VFS_TBLCANMOUNTROOT;
#endif

	vfe.vfe_reserv[0] = 0;
	vfe.vfe_reserv[1] = 0;

	if (vfs_fsadd(&vfe, &zfs_vfsconf) != 0)
		return (KERN_FAILURE);
	else
		return (KERN_SUCCESS);
}

int
zfs_vfsops_fini(void)
{

	zfs_stop_notify_thread();

	if (vnop_lookup_cache)
		kmem_cache_destroy(vnop_lookup_cache);
	vnop_lookup_cache = NULL;

	return (vfs_fsremove(zfs_vfsconf));
}
