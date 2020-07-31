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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2011 Pawel Jakub Dawidek <pawel@dawidek.net>.
 * All rights reserved.
 * Copyright (C) 2011 Lawrence Livermore National Security, LLC.
 * Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 * LLNL-CODE-403049.
 * Rewritten for Linux by:
 *   Rohan Puri <rohan.puri15@gmail.com>
 *   Brian Behlendorf <behlendorf1@llnl.gov>
 * Copyright (c) 2012, 2015 by Delphix. All rights reserved.
 *
 * Rewritten for OSX by (based on FreeBSD)
 *   Jorgen Lundman <lundman@lundman.net>
 */

/*
 * ZFS control directory (a.k.a. ".zfs")
 *
 * This directory provides a common location for all ZFS meta-objects.
 * Currently, this is only the 'snapshot' directory, but this may expand in the
 * future.  The elements are built using the GFS primitives, as the hierarchy
 * does not actually exist on disk.
 *
 * For 'snapshot', we don't want to have all snapshots always mounted, because
 * this would take up a huge amount of space in /etc/mnttab.  We have three
 * types of objects:
 *
 * 	ctldir ------> snapshotdir -------> snapshot
 *                                             |
 *                                             |
 *                                             V
 *                                         mounted fs
 *
 * The 'snapshot' node contains just enough information to lookup '..' and act
 * as a mountpoint for the snapshot.  Whenever we lookup a specific snapshot, we
 * perform an automount of the underlying filesystem and return the
 * corresponding vnode.
 *
 * All mounts are handled automatically by an user mode helper which invokes
 * the mount procedure.  Unmounts are handled by allowing the mount
 * point to expire so the kernel may automatically unmount it.
 *
 * The '.zfs', '.zfs/snapshot', and all directories created under
 * '.zfs/snapshot' (ie: '.zfs/snapshot/<snapname>') all share the same
 * zfsvfs_t as the head filesystem (what '.zfs' lives under).
 *
 * File systems mounted ontop of the GFS nodes '.zfs/snapshot/<snapname>'
 * (ie: snapshots) are ZFS nodes and have their own unique vfs_t.
 * However, vnodes within these mounted on file systems have their v_vfsp
 * fields set to the head filesystem to make NFS happy (see
 * zfsctl_snapdir_lookup()). We VFS_HOLD the head filesystem's vfs_t
 * so that it cannot be freed until all snapshots have been unmounted.
 */

#include <sys/mount.h>
#include <sys/types.h>
#include <sys/pathname.h>
#include <sys/zfs_context.h>
#include <sys/zfs_ctldir.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_vfsops.h>
//#include <sys/namei.h>
#include <sys/gfs.h>
#include <sys/stat.h>
#include <sys/dmu.h>
#include <sys/dsl_destroy.h>

#include <sys/dsl_deleg.h>
#include <sys/mount.h>
#include <sys/sunddi.h>

#include "zfs_namecheck.h"

//#define dprintf printf

/*
 *            OSX     FreeBSD
 *           -------  ---------
 * Short:    iocount  usecount
 * Long:    usecount  holdcount
 * incr:   vnode_get  vget
 * decr:   vnode_put  vrele / vput (vput unlocks and vrele)
 *
 */


//typedef struct vnodeopv_entry_desc vop_vector;
#define vop_vector vnodeopv_entry_desc

typedef struct zfsctl_node {
	gfs_dir_t	zc_gfs_private;
	uint64_t	zc_id;
	timestruc_t	zc_cmtime;	/* ctime and mtime, always the same */
} zfsctl_node_t;

typedef struct zfsctl_snapdir {
	zfsctl_node_t	sd_node;
	kmutex_t	sd_lock;
	avl_tree_t	sd_snaps;
} zfsctl_snapdir_t;

typedef struct {
	char		*se_name;
	struct vnode		*se_root;
	avl_node_t	se_node;
} zfs_snapentry_t;

static int
snapentry_compare(const void *a, const void *b)
{
	const zfs_snapentry_t *sa = a;
	const zfs_snapentry_t *sb = b;
	int ret = strcmp(sa->se_name, sb->se_name);

	if (ret < 0)
		return (-1);
	else if (ret > 0)
		return (1);
	else
		return (0);
}

<<<<<<< HEAD:ZFSin/zfs/module/zfs/zfs_ctldir.c
#ifdef sun
vnodeops_t *zfsctl_ops_root;
vnodeops_t *zfsctl_ops_snapdir;
vnodeops_t *zfsctl_ops_snapshot;
vnodeops_t *zfsctl_ops_shares;
vnodeops_t *zfsctl_ops_shares_dir;

static const fs_operation_def_t zfsctl_tops_root[];
static const fs_operation_def_t zfsctl_tops_snapdir[];
static const fs_operation_def_t zfsctl_tops_snapshot[];
static const fs_operation_def_t zfsctl_tops_shares[];
#endif	/* !sun */
#ifdef __FreeBSD__
static struct vop_vector zfsctl_ops_root;
static struct vop_vector zfsctl_ops_snapdir;
static struct vop_vector zfsctl_ops_snapshot;
static struct vop_vector zfsctl_ops_shares;
static struct vop_vector zfsctl_ops_shares_dir;
#endif	/* !sun */
#ifdef _WIN32
=======
#ifdef __APPLE__
>>>>>>> b3b9184f... Snapshot mounts fail on second mount:module/zfs/zfs_ctldir.c
struct vnodeopv_desc zfsctl_ops_root;
struct vnodeopv_desc zfsctl_ops_snapdir;
struct vnodeopv_desc zfsctl_ops_snapshot;
#endif

static struct vnode *zfsctl_mknode_snapdir(struct vnode *);
//static struct vnode *zfsctl_mknode_shares(struct vnode *);
static struct vnode *zfsctl_snapshot_mknode(struct vnode *, uint64_t objset);
static int zfsctl_unmount_snap(zfs_snapentry_t *, int, cred_t *);


/*
 * Root directory elements.  We only have two entries
 * snapshot and shares.
 */
static gfs_dirent_t zfsctl_root_entries[] = {
	{ "snapshot", zfsctl_mknode_snapdir, GFS_CACHE_VNODE },
#ifndef _WIN32
	{ "shares", zfsctl_mknode_shares, GFS_CACHE_VNODE },
#endif
	{ NULL }
};

/* include . and .. in the calculation */
#define	NROOT_ENTRIES	((sizeof (zfsctl_root_entries) / \
    sizeof (gfs_dirent_t)) + 1)

int (**zfsctl_ops_root_dvnodeops) (void *);
int (**zfsctl_ops_snapdir_dvnodeops) (void *);
int (**zfsctl_ops_snapshot_dvnodeops) (void *);

#define LK_EXCLUSIVE 0

int
traverse(struct vnode **cvpp, int lktype)
{
    struct vnode *cvp;
    struct vnode *tvp;
    vfs_t *vfsp;
    int error;
    int loop = 0;

    dprintf("+traverse\n");

    cvp = *cvpp;
    tvp = NULL;

    /*
     * If this vnode is mounted on, then we transparently indirect
     * to the vnode which is the root of the mounted file system.
     * Before we do this we must check that an unmount is not in
     * progress on this vnode.
     */

    for (;;) {
        /*
         * Reached the end of the mount chain?
         */
        vfsp = vnode_mountedhere(cvp);
        if (vfsp == NULL)
            break;
        error = vfs_busy(vfsp, 0);
        /*
         * tvp is NULL for *cvpp vnode, which we can't unlock.
         */
        VN_RELE(cvp);

        if (error)
            return (error);

        /*
         * The read lock must be held across the call to VFS_ROOT() to
         * prevent a concurrent unmount from destroying the vfs.
         */
        error = VFS_ROOT(vfsp, lktype, &tvp);
        vfs_unbusy(vfsp);
        if (error != 0)
            return (error);

        cvp = tvp;

        if (loop++>5) {
            dprintf("loop detected, abort\n");
            break;
        }

    }

    dprintf("-traverse\n");
    *cvpp = cvp;
    return (0);
}




/*
 * Initialize the various GFS pieces we'll need to create and manipulate .zfs
 * directories.  This is called from the ZFS init routine, and initializes the
 * vnode ops vectors that we'll be using.
 */
void
zfsctl_init(void)
{
#ifdef sun
	VERIFY(gfs_make_opsvec(zfsctl_opsvec) == 0);
#endif
}

void
zfsctl_fini(void)
{
#ifdef sun
	/*
	 * Remove vfsctl vnode ops
	 */
	if (zfsctl_ops_root)
		vn_freevnodeops(zfsctl_ops_root);
	if (zfsctl_ops_snapdir)
		vn_freevnodeops(zfsctl_ops_snapdir);
	if (zfsctl_ops_snapshot)
		vn_freevnodeops(zfsctl_ops_snapshot);
	if (zfsctl_ops_shares)
		vn_freevnodeops(zfsctl_ops_shares);
	if (zfsctl_ops_shares_dir)
		vn_freevnodeops(zfsctl_ops_shares_dir);

	zfsctl_ops_root = NULL;
	zfsctl_ops_snapdir = NULL;
	zfsctl_ops_snapshot = NULL;
	zfsctl_ops_shares = NULL;
	zfsctl_ops_shares_dir = NULL;
#endif	/* sun */
}

boolean_t
zfsctl_is_node(struct vnode *vp)
{
    if (vnode_tag(vp) == VT_OTHER)
        return B_TRUE;
    return B_FALSE;
}

/*
 * Return the inode number associated with the 'snapshot' or
 * 'shares' directory.
 */
/* ARGSUSED */
static ino64_t
zfsctl_root_inode_cb(struct vnode *vp, int index)
{
	zfsvfs_t *zfsvfs = vfs_fsprivate(vnode_mount(vp));

	ASSERT(index <= 2);

	if (index == 0)
		return (ZFSCTL_INO_SNAPDIR);

	return (zfsvfs->z_shares_dir);
}

/*
 * Create the '.zfs' directory.  This directory is cached as part of the VFS
 * structure.  This results in a hold on the vfs_t.  The code in zfs_umount()
 * therefore checks against a vfs_count of 2 instead of 1.  This reference
 * is removed when the ctldir is destroyed in the unmount.
 */
void
zfsctl_create(zfsvfs_t *zfsvfs)
{
	struct vnode *vp = NULL; //, *rvp = NULL;
	zfsctl_node_t *zcp;
//	uint64_t crtime[2];


	ASSERT(zfsvfs->z_ctldir == NULL);

    dprintf("zfsctl_create\n");

    /*
     * This creates a vnode with VROOT set, this is so that unmount's
     * vflush() (called before our vfs_unmount) will pass (and not block
     * waiting for the usercount ref to be released). We then release the
     * VROOT vnode in zfsctl_destroy, and release the usercount ref.
     */

	vp = gfs_root_create(sizeof (zfsctl_node_t), zfsvfs->z_vfs,
                         zfsctl_ops_root_dvnodeops,
                         ZFSCTL_INO_ROOT, zfsctl_root_entries,
	    zfsctl_root_inode_cb, MAXNAMELEN, NULL, NULL);

    zcp = vnode_fsnode(vp);
    zcp->zc_id = ZFSCTL_INO_ROOT;

#ifndef __APPLE__
    VERIFY(VFS_ROOT(zfsvfs->z_vfs, 0, &rvp) == 0);
    VERIFY(0 == sa_lookup(VTOZ(rvp)->z_sa_hdl, SA_ZPL_CRTIME(zfsvfs),
                          &crtime, sizeof (crtime)));
    ZFS_TIME_DECODE(&zcp->zc_cmtime, crtime);
    VN_RELE(rvp);
#endif

#ifdef __LINUX__
    /*
     * We're only faking the fact that we have a root of a filesystem for
     * the sake of the GFS interfaces.  Undo the flag manipulation it did
     * for us.
     */
    vp->v_vflag &= ~VV_ROOT;
#endif
    /* In OSX we mark the node VSYSTEM instead */

    zfsvfs->z_ctldir = vp;

    /*
     * Explicitely hold a usecount (not iocount) reference here, so that
     * .zfs is hold until unmount is called
     */
    vnode_ref(zfsvfs->z_ctldir); // Hold an usecount ref

    VN_RELE(zfsvfs->z_ctldir); // release iocount ref(vnode_get/vnode_create)
}


/*
 * Destroy the '.zfs' directory.  Only called when the filesystem is unmounted.
 * There might still be more references if we were force unmounted, but only
 * new zfs_inactive() calls can occur and they don't reference .zfs
 */
void
zfsctl_destroy(zfsvfs_t *zfsvfs)
{
    struct vnode *vp;

    dprintf("zfsctl: releasing rootvp %p\n", zfsvfs->z_ctldir);
    vp = zfsvfs->z_ctldir;
	zfsvfs->z_ctldir = NULL;
    if (vp && !vnode_getwithref(vp)) {

        /*
         * Finally release the vnode_ref held in zfsctl_create()
         */
        vnode_rele(vp);
        vnode_recycle(vp);
        VN_RELE(vp);
    }

}

/*
 * Given a root znode, retrieve the associated .zfs directory.
 * Add a hold to the vnode and return it.
 */
struct vnode *
zfsctl_root(znode_t *zp)
{
	ASSERT(zfs_has_ctldir(zp));
    dprintf("zfsctl_root hold\n");
    VN_HOLD(zp->z_zfsvfs->z_ctldir);
	return (zp->z_zfsvfs->z_ctldir);
}

/*
 * Common open routine.  Disallow any write access.
 */
/* ARGSUSED */
static int
zfsctl_common_open(struct vnop_open_args *ap)
{
	int flags = ap->a_mode;

    dprintf("zfsctl_open: %p on %p\n",
           ap->a_vp, vnode_mountedhere(ap->a_vp));

	if (flags & FWRITE)
        return (EACCES);

	return (0);
}

/*
 * Common close routine.  Nothing to do here.
 */
/* ARGSUSED */
static int
zfsctl_common_close(struct vnop_close_args *ap)
{
	return (0);
}



/*
 * Common access routine.  Disallow writes.
 */
/* ARGSUSED */
static int
zfsctl_common_access(ap)
	struct vnop_access_args /* {
		struct vnode *a_vp;
		accmode_t a_accmode;
		struct ucred *a_cred;
		struct thread *a_td;
	} */ *ap;
{
	int accmode = ap->a_action;
    dprintf("zfsctl_access\n");

#ifdef TODO
	if (flags & V_ACE_MASK) {
		if (accmode & ACE_ALL_WRITE_PERMS)
			return (EACCES);
	} else {
#endif
		if (accmode & VWRITE)
			return (EACCES);
#ifdef TODO
	}
#endif

	return (0);
}

/*
 * Common getattr function.  Fill in basic information.
 */
static void
zfsctl_common_getattr(struct vnode *vp, vattr_t *vap)
{
	timestruc_t	now;

    dprintf("zfsctl: +getattr: %p\n",
		vp);

#ifdef _WIN32
    VATTR_SET_SUPPORTED(vap, va_mode);
    VATTR_SET_SUPPORTED(vap, va_type);
    VATTR_SET_SUPPORTED(vap, va_uid);
    VATTR_SET_SUPPORTED(vap, va_gid);
    VATTR_SET_SUPPORTED(vap, va_data_size);
    VATTR_SET_SUPPORTED(vap, va_total_size);
    VATTR_SET_SUPPORTED(vap, va_data_alloc);
    VATTR_SET_SUPPORTED(vap, va_total_alloc);
    VATTR_SET_SUPPORTED(vap, va_access_time);
    VATTR_SET_SUPPORTED(vap, va_dirlinkcount);
    VATTR_SET_SUPPORTED(vap, va_flags);
#endif

    vap->va_dirlinkcount = 1; //directory hard links.
    vap->va_nlink = 3;
	vap->va_uid = 0;
	vap->va_gid = 0;
	vap->va_rdev = 0;
	/*
	 * We are a purely virtual object, so we have no
	 * blocksize or allocated blocks.
	 */
    //	vap->va_blksize = 0;
    vap->va_data_alloc = 512;
    vap->va_total_alloc = 512;
    vap->va_data_size = 2; // . + ..
    vap->va_total_size = 2;
	vap->va_nblocks = 0;
	//vap->va_seq = 0;
	vap->va_gen = 0;

	vap->va_mode = S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP |
	    S_IROTH | S_IXOTH;
	vap->va_type = VDIR;

	if (VATTR_IS_ACTIVE(vap, va_nchildren) && vnode_isdir(vp)) {
		VATTR_RETURN(vap, va_nchildren, vap->va_nlink - 2);
    }
    vap->va_iosize = 512;

	/*
	 * We live in the now (for atime).
	 */
	gethrestime(&now);
	vap->va_atime = now;
	/* FreeBSD: Reset chflags(2) flags. */
	vap->va_flags = 0;

    dprintf("zfsctl: -getattr\n");
}

<<<<<<< HEAD:ZFSin/zfs/module/zfs/zfs_ctldir.c
#ifndef _WIN32
/*ARGSUSED*/
static int
zfsctl_common_fid(ap)
	struct vnop_fid_args /* {
		struct vnode *a_vp;
		struct fid *a_fid;
	} */ *ap;
{
	struct vnode		*vp = ap->a_vp;
	fid_t		*fidp = (void *)ap->a_fid;
	zfsvfs_t	*zfsvfs = vfs_fsprivate(vnode_mount(vp));
	zfsctl_node_t	*zcp = vnode_fsnode(vp);
	uint64_t	object = zcp->zc_id;
	zfid_short_t	*zfid;
	int		i;

	ZFS_ENTER(zfsvfs);

	fidp->fid_len = SHORT_FID_LEN;

	zfid = (zfid_short_t *)fidp;

	zfid->zf_len = SHORT_FID_LEN;

	for (i = 0; i < sizeof (zfid->zf_object); i++)
		zfid->zf_object[i] = (uint8_t)(object >> (8 * i));

	/* .zfs znodes always have a generation number of 0 */
	for (i = 0; i < sizeof (zfid->zf_gen); i++)
		zfid->zf_gen[i] = 0;

	ZFS_EXIT(zfsvfs);
	return (0);
}
#endif

/*ARGSUSED*/
#ifndef _WIN32
static int
zfsctl_shares_fid(ap)
	struct vop_fid_args /* {
		struct vnode *a_vp;
		struct fid *a_fid;
	} */ *ap;
{
	struct vnode		*vp = ap->a_vp;
	fid_t		*fidp = (void *)ap->a_fid;
	zfsvfs_t	*zfsvfs = vp->v_vfsp->vfs_data;
	znode_t		*dzp;
	int		error;

	ZFS_ENTER(zfsvfs);

	if (zfsvfs->z_shares_dir == 0) {
		ZFS_EXIT(zfsvfs);
		return (ENOTSUP);
	}

	if ((error = zfs_zget(zfsvfs, zfsvfs->z_shares_dir, &dzp)) == 0) {
		error = VOP_FID(ZTOV(dzp), fidp);
		VN_RELE(ZTOV(dzp));
	}

	ZFS_EXIT(zfsvfs);
	return (error);
}
#endif

=======
>>>>>>> b3b9184f... Snapshot mounts fail on second mount:module/zfs/zfs_ctldir.c
/*
 * Gets the full dataset name that corresponds to the given snapshot name
 * Example:
 * 	zfsctl_snapshot_zname("snap1") -> "mypool/myfs@snap1"
 */
static int
zfsctl_common_reclaim(ap)
	struct vnop_reclaim_args /* {
		struct vnode *a_vp;
		struct thread *a_td;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	gfs_file_t *fp = vnode_fsnode(vp);

    dprintf("zfsctl: +reclaim vp %p mountedon %p\n", vp,
            vnode_mountedhere(vp));

	/*
	 * Destroy the vm object and flush associated pages.
	 */
#ifdef _WIN32
    /*
     * It would appear that Darwin does not guarantee that vnop_inactive is
     * always called, but reclaim is used instead. All release happens in here
     * and inactive callbacks are mostly empty.
     */
    if (fp) {

        if (fp->gfs_type == GFS_DIR)
            gfs_dir_inactive(vp);
        else
            gfs_file_inactive(vp);

        kmem_free(fp, fp->gfs_size);

    }

    vnode_removefsref(vp); /* ADDREF from vnode_create */
    vnode_clearfsnode(vp); /* vp->v_data = NULL */

#endif

    dprintf("zfsctl: -reclaim vp %p\n", vp);
	return (0);
}

#define	ZFSCTL_INO_SNAP(id)	(id)

/*
 * Get root directory attributes.
 */
/* ARGSUSED */
static int
zfsctl_root_getattr(ap)
	struct vnop_getattr_args /* {
		struct vnode *a_vp;
		struct vattr *a_vap;
		struct ucred *a_cred;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	vattr_t *vap = ap->a_vap;
	zfsvfs_t *zfsvfs = vfs_fsprivate(vnode_mount(vp));
	zfsctl_node_t *zcp = vnode_fsnode(vp);

	ZFS_ENTER(zfsvfs);
#ifdef _WIN32
    VATTR_SET_SUPPORTED(vap, va_modify_time);
    VATTR_SET_SUPPORTED(vap, va_create_time);
    VATTR_SET_SUPPORTED(vap, va_fsid);
    VATTR_SET_SUPPORTED(vap, va_fileid); // SPL: va_nodeid
    VATTR_CLEAR_SUPPORTED(vap, va_acl);
#endif
    // CALL statvfs to get FSID here
	vap->va_fsid = vfs_statfs(vnode_mount(vp))->f_fsid.val[0];
	vap->va_nodeid = ZFSCTL_INO_ROOT;
	vap->va_nlink = vap->va_size = NROOT_ENTRIES;
	vap->va_mtime = vap->va_ctime = zcp->zc_cmtime;
	vap->va_ctime = vap->va_ctime;

	if (VATTR_IS_ACTIVE(vap, va_name) && vap->va_name) {
        (void)strlcpy(vap->va_name, ".zfs", MAXPATHLEN);
        VATTR_SET_SUPPORTED(vap, va_name);
    }

	zfsctl_common_getattr(vp, vap);

	ZFS_EXIT(zfsvfs);

	return (0);
}

/*
 * Special case the handling of "..".
 */
/* ARGSUSED */
int
zfsctl_root_lookup(struct vnode *dvp, char *nm, struct vnode **vpp, pathname_t *pnp,
    int flags, struct vnode *rdir, cred_t *cr, caller_context_t *ct,
    int *direntflags, pathname_t *realpnp)
{
	zfsvfs_t *zfsvfs = vfs_fsprivate(vnode_mount(dvp));
	int err;

    dprintf("zfsctl_root_lookup dvp %p\n", dvp);

    if (!zfsvfs) return EINVAL;

	/*
	 * No extended attributes allowed under .zfs
	 */
#ifndef _WIN32
	if (flags & LOOKUP_XATTR)
		return (EINVAL);
#endif

	ZFS_ENTER(zfsvfs);

	if (strcmp(nm, "..") == 0) {
        err = VFS_ROOT(vnode_mount(dvp), LK_EXCLUSIVE, vpp);
	} else {
		err = gfs_vop_lookup(dvp, nm, vpp, pnp, flags, rdir,
		    cr, ct, direntflags, realpnp);
    }

	ZFS_EXIT(zfsvfs);

	return (err);
}




/*
 * Special case the handling of "..".
 */
/* ARGSUSED */
int
zfsctl_freebsd_root_lookup(ap)
	struct vnop_lookup_args /* {
		struct vnode *a_dvp;
		struct vnode **a_vpp;
		struct componentname *a_cnp;
	} */ *ap;
{
	struct vnode *dvp = ap->a_dvp;
	struct vnode **vpp = ap->a_vpp;
	cred_t *cr = (cred_t *)vfs_context_ucred((ap)->a_context);
	int flags = ap->a_cnp->cn_flags;
	int nameiop = ap->a_cnp->cn_nameiop;
	char nm[NAME_MAX + 1];
	int err;

    dprintf("zfsctl: +freebsd_root_lookup: nameiop %d\n", nameiop);


	if ((flags & ISLASTCN) && (nameiop == RENAME || nameiop == CREATE)) {
        dprintf("failed\n");
		return (EOPNOTSUPP);
    }

	ASSERT(ap->a_cnp->cn_namelen < sizeof(nm));
	strlcpy(nm, ap->a_cnp->cn_nameptr, ap->a_cnp->cn_namelen + 1);

	err = zfsctl_root_lookup(dvp, nm, vpp, NULL, 0, NULL, cr, NULL, NULL, NULL);

#ifdef __FreeBSD__
	if (err == 0 && (nm[0] != '.' || nm[1] != '\0'))
        vn_lock(*vpp, LK_EXCLUSIVE | LK_RETRY);
#endif

	return (err);
}


#ifdef _WIN32
#define VOPFUNC int (*)(void *)
#include <vfs/vfs_support.h>
/* Directory vnode operations template */
//int (**zfsctl_ops_root_dvnodeops) (void *);
static struct vnodeopv_entry_desc zfsctl_ops_root_template[] = {
	{&vnop_default_desc, 	(VOPFUNC)vn_default_error },
	{&vnop_open_desc,	(VOPFUNC)zfsctl_common_open},
	{&vnop_close_desc,	(VOPFUNC)zfsctl_common_close},
	//{&vnop_ioctl_desc,	(VOPFUNC)zfs_vnop_ioctl},
	{&vnop_getattr_desc,	(VOPFUNC)zfsctl_root_getattr},
	{&vnop_access_desc,	(VOPFUNC)zfsctl_common_access},
	{&vnop_readdir_desc,	(VOPFUNC)gfs_vop_readdir},
	//{&vnop_readdirattr_desc, (VOPFUNC)zfs_vnop_readdirattr},
	//{&vnop_lookup_desc,	(VOPFUNC)zfsctl_root_lookup},
	{&vnop_lookup_desc,	(VOPFUNC)zfsctl_freebsd_root_lookup},
	{&vnop_inactive_desc,	(VOPFUNC)gfs_vop_inactive},
	{&vnop_reclaim_desc,	(VOPFUNC)zfsctl_common_reclaim},

    { &vnop_revoke_desc, (VOPFUNC)err_revoke },             /* revoke */
    { &vnop_fsync_desc, (VOPFUNC)nop_fsync },               /* fsync */

	{NULL, (VOPFUNC)NULL }
};
struct vnodeopv_desc zfsctl_ops_root =
{ &zfsctl_ops_root_dvnodeops, zfsctl_ops_root_template };

#endif



static int
zfsctl_snapshot_zname(struct vnode *vp, const char *name, int len, char *zname)
{
	objset_t *os = ((zfsvfs_t *)(vfs_fsprivate(vnode_mount(vp))))->z_os;

	if (zfs_component_namecheck(name, NULL, NULL) != 0)
		return (EILSEQ);
	dmu_objset_name(os, zname);
	if (strlen(zname) + 1 + strlen(name) >= len)
		return (ENAMETOOLONG);
	(void) strlcat(zname, "@", len);
	(void) strlcat(zname, name, len);
	return (0);
}

static int
zfsctl_unmount_snap(zfs_snapentry_t *sep, int fflags, cred_t *cr)
{
	struct vnode *svp = sep->se_root;
	int error;
	struct vnop_inactive_args iap;

	ASSERT(vn_ismntpt(svp));

	/* this will be dropped by dounmount() */
	if ((error = vn_vfswlock(svp)) != 0)
		return (error);

	/*
	 * We can't use VN_RELE(), as that will try to invoke
	 * zfsctl_snapdir_inactive(), which would cause us to destroy
	 * the sd_lock mutex held by our caller.
	 */
	//ASSERT(svp->v_count == 1);
    iap.a_vp = svp;
	gfs_vop_inactive(&iap);

    dprintf("zfsctldir: Releasing '%s'\n", sep->se_name);
	kmem_free(sep->se_name, strlen(sep->se_name) + 1);
    sep->se_name = NULL;
	kmem_free(sep, sizeof (zfs_snapentry_t));
    sep = NULL;

	return (0);
}


/*
 * This creates a snapshot under '.zfs/snapshot'.
 */
/* ARGSUSED */
static int
zfsctl_snapdir_mkdir(struct vnode *dvp, char *dirname, vattr_t *vap, struct vnode  **vpp,
    cred_t *cr, caller_context_t *cc, int flags, vsecattr_t *vsecp)
{
    return ENOTSUP;
#if 0
	zfsvfs_t *zfsvfs = vfs_fsprivate(vnode_mount(dvp));
	char name[MAXNAMELEN];
	int err, error;
	//static enum symfollow follow = NO_FOLLOW;
	static enum uio_seg seg = UIO_SYSSPACE;

	if (snapshot_namecheck(dirname, NULL, NULL) != 0)

	zfs_sb_t *zsb = ITOZSB(dip);
	char *dsname;
	int error;

	dsname = kmem_alloc(MAXNAMELEN, KM_SLEEP);

	if (zfs_component_namecheck(dirname, NULL, NULL) != 0) {
		error = SET_ERROR(EILSEQ);
		goto out;
	}

	dmu_objset_name(zfsvfs->z_os, name);

	*vpp = NULL;

	err = zfs_secpolicy_snapshot_perms(name, cr);
	if (err)
		return (err);

	if (err == 0) {
        //		err = dmu_objset_snapshot(name, dirname, NULL, NULL,
        //  B_FALSE, B_FALSE, -1);
		if (err)
			return (err);
        err = zfsctl_snapdir_lookup(dvp, dirname, vpp,
                                    0, cr, NULL, NULL);
	}
out:
	return (err);
#endif
}

static int
zfsctl_freebsd_snapdir_mkdir(ap)
        struct vnop_mkdir_args /* {
                struct vnode *a_dvp;
                struct vnode **a_vpp;
                struct componentname *a_cnp;
                struct vattr *a_vap;
        } */ *ap;
{

//	ASSERT(ap->a_cnp->cn_flags & SAVENAME);
	cred_t *cr = (cred_t *)vfs_context_ucred((ap)->a_context);

	return (zfsctl_snapdir_mkdir(ap->a_dvp, ap->a_cnp->cn_nameptr, NULL,
	    ap->a_vpp, cr, NULL, 0, NULL));
}

static int
zfsctl_snapdir_readdir_cb(struct vnode *vp, void *dp, int *eofp,
     offset_t *offp, offset_t *nextp, void *data, int flags);

/*
 * Lookup entry point for the 'snapshot' directory.  Try to open the
 * snapshot if it exist, creating the pseudo filesystem vnode as necessary.
 * Perform a mount of the associated dataset on top of the vnode.
 */
/* ARGSUSED */
#if !defined (__OPTIMIZE__)
#pragma GCC diagnostic ignored "-Wframe-larger-than="
#endif
int
zfsctl_snapdir_lookup(ap)
	struct vnop_lookup_args /* {
		struct vnode *a_dvp;
		struct vnode **a_vpp;
		struct componentname *a_cnp;
	} */ *ap;
{
	struct vnode *dvp = ap->a_dvp;
	struct vnode **vpp = ap->a_vpp;
	struct componentname *cnp = ap->a_cnp;
	char nm[NAME_MAX + 1];
	zfsctl_snapdir_t *sdp = vnode_fsnode(dvp);
	objset_t *snap;
	char snapname[ZFS_MAX_DATASET_NAME_LEN];
	char real[ZFS_MAX_DATASET_NAME_LEN];
	char *mountpoint;
	zfs_snapentry_t *sep, search;
	size_t mountpoint_len;
	avl_index_t where;
	zfsvfs_t *zfsvfs = vfs_fsprivate(vnode_mount(dvp));
	int err;
	int flags = 0;

	/*
	 * No extended attributes allowed under .zfs
	 */
#ifndef _WIN32
	if (flags & LOOKUP_XATTR)
		return (EINVAL);
#endif

    if (!sdp) return ENOENT;

	ASSERT(ap->a_cnp->cn_namelen < sizeof(nm));
	strlcpy(nm, ap->a_cnp->cn_nameptr, ap->a_cnp->cn_namelen + 1);

    dprintf("zfsctl_snapdir_lookup '%s'\n", nm);

	ASSERT(vnode_isdir(dvp));

    if (!strcmp(nm, ".autodiskmounted")) return EINVAL;



	*vpp = NULL;

	/*
	 * If we get a recursive call, that means we got called
	 * from the domount() code while it was trying to look up the
	 * spec (which looks like a local path for zfs).  We need to
	 * add some flag to domount() to tell it not to do this lookup.
	 */
	if (MUTEX_HELD(&sdp->sd_lock))
		return (ENOENT);

	ZFS_ENTER(zfsvfs);

    // Returns if LOCK is held, otherwise we do not hold vpp
	if (gfs_lookup_dot(vpp, dvp, zfsvfs->z_ctldir, nm) == 0) {
		ZFS_EXIT(zfsvfs);
		return (0);
	}

	if (flags & FIGNORECASE) {
		boolean_t conflict = B_FALSE;

		err = dmu_snapshot_realname(zfsvfs->z_os, nm, real,
		    MAXNAMELEN, &conflict);
		if (err == 0) {
			strlcpy(nm, real, sizeof(nm));
		} else if (err != ENOTSUP) {
			ZFS_EXIT(zfsvfs);
			return (err);
		}
#if 0
		if (realpnp)
			(void) strlcpy(realpnp->pn_buf, nm,
			    realpnp->pn_bufsize);
		if (conflict && direntflags)
			*direntflags = ED_CASE_CONFLICT;
#endif
	}

	dprintf("looking for name '%s'\n", nm);

	mutex_enter(&sdp->sd_lock);
	search.se_name = (char *)nm;
	if ((sep = avl_find(&sdp->sd_snaps, &search, &where)) != NULL) {
		*vpp = sep->se_root;
		VN_HOLD(*vpp);
		err = traverse(vpp, LK_EXCLUSIVE | LK_RETRY);

		if (err) {
			VN_RELE(*vpp);
			*vpp = NULL;
            dprintf("vnrele\n");
		} else if (*vpp == sep->se_root) {
			/*
			 * The snapshot was unmounted behind our backs,
			 * try to remount it.
			 */
			VERIFY(zfsctl_snapshot_zname(dvp, nm, ZFS_MAX_DATASET_NAME_LEN, snapname) == 0);
            dprintf("goto domount\n");
			goto domount;
		} else {
			/*
			 * VROOT was set during the traverse call.  We need
			 * to clear it since we're pretending to be part
			 * of our parent's vfs.
			 */
			//(*vpp)->v_flag &= ~VROOT;
		}
		mutex_exit(&sdp->sd_lock);
		ZFS_EXIT(zfsvfs);
		return (err);
	}

	/*
	 * The requested snapshot is not currently mounted, look it up.
	 */
	err = zfsctl_snapshot_zname(dvp, nm, MAXNAMELEN, snapname);
	if (err) {
		mutex_exit(&sdp->sd_lock);
		ZFS_EXIT(zfsvfs);
		/*
		 * handle "ls *" or "?" in a graceful manner,
		 * forcing EILSEQ to ENOENT.
		 * Since shell ultimately passes "*" or "?" as name to lookup
		 */
		return (err == EILSEQ ? ENOENT : err);
	}
	if (dmu_objset_hold(snapname, FTAG, &snap) != 0) {
		mutex_exit(&sdp->sd_lock);
		/* Translate errors and add SAVENAME when needed. */
		if ((cnp->cn_flags & ISLASTCN) && cnp->cn_nameiop == CREATE) {
			err = EJUSTRETURN;
			//cnp->cn_flags |= SAVENAME;
		} else {
			err = ENOENT;
		}
		ZFS_EXIT(zfsvfs);
		return (err);
	}

	sep = kmem_alloc(sizeof (zfs_snapentry_t), KM_SLEEP);
	sep->se_name = kmem_alloc(strlen(nm) + 1, KM_SLEEP);
	(void) strlcpy(sep->se_name, nm, strlen(nm) + 1);
    dprintf("must not exist, Calling snapshot_mknode for '%s'\n", snapname);
    VN_RELE(*vpp);
	*vpp = sep->se_root = zfsctl_snapshot_mknode(dvp, dmu_objset_id(snap));
	avl_insert(&sdp->sd_snaps, sep, where);

	dmu_objset_rele(snap, FTAG);
domount:

    // vfs_statfs(vfsp)->f_mntfromname
	mountpoint_len = strlen(vfs_statfs(vnode_mount(dvp))->f_mntonname) +
	    strlen("/" ZFS_CTLDIR_NAME "/snapshot/") + strlen(nm) + 1;
	mountpoint = kmem_alloc(mountpoint_len, KM_SLEEP);
	(void) snprintf(mountpoint, mountpoint_len,
	    "%s/" ZFS_CTLDIR_NAME "/snapshot/%s",
                    vfs_statfs(vnode_mount(dvp))->f_mntonname, nm);

#ifdef __FreeBSD__
	err = mount_snapshot(curthread, vpp, "zfs", mountpoint, snapname, 0);
#endif

#ifdef _WIN32

    dprintf("Would call mount here on '%s' for '%s': mountedhere %p\n",
            mountpoint, snapname, vnode_mountedhere(*vpp));

#ifdef _KERNEL

    /* In upstream ZFS, mount_snapshot takes the current vp in vpp,
     * allocates a new mount, and creates a new mvp for it. Then
     * calls vput(vp) to release the current vnode. This lookup
     * functions then returns the lock held for mvp (in vpp).
     *
     * In OSX, we do not get a new vnode, since we are not calling mount
     * so we need to return 'vp' with a reference. We release one of the
     * references here, and return the other.
     */

    // VN_RELE(*vpp);

    /*
     * The world isn't ready for this yet
     zfs_ereport_snapshot_post(FM_EREPORT_ZFS_SNAPSHOT_MOUNT,
     dmu_objset_spa(zfsvfs->z_os), snapname);
    */

#endif // KERNEL
#endif // APPLE

	kmem_free(mountpoint, mountpoint_len);
	if (err == 0) {
		/*
		 * Fix up the root vnode mounted on .zfs/snapshot/<snapname>.
		 *
		 * This is where we lie about our v_vfsp in order to
		 * make .zfs/snapshot/<snapname> accessible over NFS
		 * without requiring manual mounts of <snapname>.
		 */
#ifndef _WIN32
		ASSERT(VTOZ(*vpp)->z_zfsvfs != zfsvfs);
		VTOZ(*vpp)->z_zfsvfs->z_parent = zfsvfs;
#endif

	}
	mutex_exit(&sdp->sd_lock);
	ZFS_EXIT(zfsvfs);
	if (err != 0) {
        dprintf("The illumos rele here %p\n", *vpp);
        VN_RELE(*vpp);
		*vpp = NULL;
    }

    /*
     * Gross hack for now, fix meeeee
     */
    dprintf("Lookup complete: %d %p\n", err, err==0?*vpp:NULL);
	return (err);
}

/* ARGSUSED */
<<<<<<< HEAD:ZFSin/zfs/module/zfs/zfs_ctldir.c
#ifndef _WIN32
int
zfsctl_shares_lookup(ap)
	struct vnop_lookup_args /* {
		struct vnode *a_dvp;
		struct vnode **a_vpp;
		struct componentname *a_cnp;
	} */ *ap;
{
	struct vnode *dvp = ap->a_dvp;
	struct vnode **vpp = ap->a_vpp;
	struct componentname *cnp = ap->a_cnp;
	zfsvfs_t *zfsvfs = vfs_fsprivate(vnode_mount(dvp));
	char nm[NAME_MAX + 1];
	znode_t *dzp;
	int error;

	ZFS_ENTER(zfsvfs);

	ASSERT(cnp->cn_namelen < sizeof(nm));
	strlcpy(nm, cnp->cn_nameptr, cnp->cn_namelen + 1);

	if (gfs_lookup_dot(vpp, dvp, zfsvfs->z_ctldir, nm) == 0) {
		ZFS_EXIT(zfsvfs);
		return (0);
	}

	if (zfsvfs->z_shares_dir == 0) {
		ZFS_EXIT(zfsvfs);
		return (ENOTSUP);
	}
	if ((error = zfs_zget(zfsvfs, zfsvfs->z_shares_dir, &dzp)) == 0)
		error = VOP_LOOKUP(ZTOV(dzp), vpp, cnp);

	VN_RELE(ZTOV(dzp));
	ZFS_EXIT(zfsvfs);

	return (error);
}
#endif

/* ARGSUSED */
=======
>>>>>>> b3b9184f... Snapshot mounts fail on second mount:module/zfs/zfs_ctldir.c
static int
zfsctl_snapdir_readdir_cb(struct vnode *vp, void *dp, int *eofp,
    offset_t *offp, offset_t *nextp, void *data, int flags)
{
	zfsvfs_t *zfsvfs = vfs_fsprivate(vnode_mount(vp));
	char snapname[MAXNAMELEN];
	uint64_t id, cookie;
	boolean_t case_conflict;
	int error;
    dirent64_t *odp;

	ZFS_ENTER(zfsvfs);

	cookie = *offp;
    dsl_pool_config_enter(dmu_objset_pool(zfsvfs->z_os), FTAG);
	error = dmu_snapshot_list_next(zfsvfs->z_os,
	    sizeof (snapname), snapname, &id, &cookie, &case_conflict);
    dsl_pool_config_exit(dmu_objset_pool(zfsvfs->z_os), FTAG);
	if (error) {
		ZFS_EXIT(zfsvfs);
		if (error == ENOENT) {
			*eofp = 1;
			return (0);
		}
		return (error);
	}

    odp=dp;
    (void) strlcpy(odp->d_name, snapname, ZFS_MAX_DATASET_NAME_LEN);
    odp->d_ino = ZFSCTL_INO_SNAP(id);

	*nextp = cookie;

	ZFS_EXIT(zfsvfs);

	return (0);
}

<<<<<<< HEAD:ZFSin/zfs/module/zfs/zfs_ctldir.c
#ifndef _WIN32
/* ARGSUSED */
static int
zfsctl_shares_readdir(ap)
	struct vnop_readdir_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		struct ucred *a_cred;
		int *a_eofflag;
		int *a_ncookies;
		u_long **a_cookies;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	uio_t *uiop = ap->a_uio;
	cred_t *cr = (cred_t *)vfs_context_ucred((ap)->a_context);
	int *eofp = ap->a_eofflag;
	zfsvfs_t *zfsvfs = vfs_fsprivate(vnode_mount(vp));
	znode_t *dzp;
	int error = 0;
    ulong *cookies;

	ZFS_ENTER(zfsvfs);

	if (zfsvfs->z_shares_dir == 0) {
		ZFS_EXIT(zfsvfs);
		return (ENOTSUP);
	}
#if 0
	if ((error = zfs_zget(zfsvfs, zfsvfs->z_shares_dir, &dzp)) == 0) {
		vn_lock(ZTOV(dzp), LK_SHARED | LK_RETRY);
		error = VOP_READDIR(ZTOV(dzp), uiop, cr, eofp, ap->a_numdirent, &cookies);
		VN_RELE(ZTOV(dzp));
	} else {
		*eofp = 1;
		error = ENOENT;
	}
#endif

	ZFS_EXIT(zfsvfs);
	return (error);
}
#endif
=======
>>>>>>> b3b9184f... Snapshot mounts fail on second mount:module/zfs/zfs_ctldir.c

/*
 * pvp is the '.zfs' directory (zfsctl_node_t).
 * Creates vp, which is '.zfs/snapshot' (zfsctl_snapdir_t).
 *
 * This function is the callback to create a GFS vnode for '.zfs/snapshot'
 * when a lookup is performed on .zfs for "snapshot".
 */
struct vnode *
zfsctl_mknode_snapdir(struct vnode *pvp)
{
	struct vnode *vp;
	zfsctl_snapdir_t *sdp;

    dprintf("+mknode_snapdir\n");

	vp = gfs_dir_create(sizeof (zfsctl_snapdir_t), pvp, vnode_mount(pvp),
        zfsctl_ops_snapdir_dvnodeops, NULL, NULL, MAXNAMELEN,
                        zfsctl_snapdir_readdir_cb, NULL, 0);
	sdp = vnode_fsnode(vp);
	sdp->sd_node.zc_id = ZFSCTL_INO_SNAPDIR;
	sdp->sd_node.zc_cmtime = ((zfsctl_node_t *)vnode_fsnode(pvp))->zc_cmtime;
	mutex_init(&sdp->sd_lock, NULL, MUTEX_DEFAULT, NULL);
	avl_create(&sdp->sd_snaps, snapentry_compare,
	    sizeof (zfs_snapentry_t), offsetof(zfs_snapentry_t, se_node));

#ifndef _WIN32
    VOP_UNLOCK(vp, 0);
#endif

    dprintf("-mknode_snapdir: %p\n", vp);
	return (vp);
}

<<<<<<< HEAD:ZFSin/zfs/module/zfs/zfs_ctldir.c
#ifndef _WIN32
struct vnode *
zfsctl_mknode_shares(struct vnode *pvp)
{
	struct vnode *vp;
	zfsctl_node_t *sdp;

	vp = gfs_dir_create(sizeof (zfsctl_node_t), pvp, pvp->v_vfsp,
	    &zfsctl_ops_shares, NULL, NULL, MAXNAMELEN,
	    NULL, NULL);
	sdp = vnode_fsnode(vp);
	sdp->zc_cmtime = ((zfsctl_node_t *)vnode_fsnode(pvp))->zc_cmtime;
	VOP_UNLOCK(vp, 0);
	return (vp);

}
#endif

#ifndef _WIN32
/* ARGSUSED */
static int
zfsctl_shares_getattr(ap)
	struct vnop_getattr_args /* {
		struct vnode *a_vp;
		struct vattr *a_vap;
		struct ucred *a_cred;
		struct thread *a_td;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	vattr_t *vap = ap->a_vap;
	cred_t *cr = (cred_t *)vfs_context_ucred((ap)->a_context);
	zfsvfs_t *zfsvfs = vfs_fsprivate(vnode_mount(vp));
	znode_t *dzp;
	int error;

	ZFS_ENTER(zfsvfs);
	if (zfsvfs->z_shares_dir == 0) {
		ZFS_EXIT(zfsvfs);
		return (ENOTSUP);
	}
	if ((error = zfs_zget(zfsvfs, zfsvfs->z_shares_dir, &dzp)) == 0) {
		vn_lock(ZTOV(dzp), LK_SHARED | LK_RETRY);
		error = VOP_GETATTR(ZTOV(dzp), vap, cr);
		VN_RELE(ZTOV(dzp));
	}
	ZFS_EXIT(zfsvfs);
	return (error);


}
#endif

=======
>>>>>>> b3b9184f... Snapshot mounts fail on second mount:module/zfs/zfs_ctldir.c
/* ARGSUSED */
static int
zfsctl_snapdir_getattr(ap)
	struct vnop_getattr_args /* {
		struct vnode *a_vp;
		struct vattr *a_vap;
		struct ucred *a_cred;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	vattr_t *vap = ap->a_vap;
	zfsvfs_t *zfsvfs = vfs_fsprivate(vnode_mount(vp));
	zfsctl_snapdir_t *sdp = vnode_fsnode(vp);

    dprintf("zfsctl: +snapdir_getattr: %p: (v_data %p)\n", vp, sdp);

    if (!sdp) return ENOENT;

	ZFS_ENTER(zfsvfs);
	zfsctl_common_getattr(vp, vap);
	vap->va_nodeid = gfs_file_inode(vp);
	vap->va_nlink = vap->va_size = avl_numnodes(&sdp->sd_snaps) + 2;
	vap->va_ctime = vap->va_mtime = vap->va_mtime = dmu_objset_snap_cmtime(zfsvfs->z_os);
#ifdef _WIN32
    VATTR_SET_SUPPORTED(vap, va_modify_time);
    VATTR_SET_SUPPORTED(vap, va_create_time);
    VATTR_SET_SUPPORTED(vap, va_nlink);
    VATTR_SET_SUPPORTED(vap, va_fileid);
    VATTR_CLEAR_SUPPORTED(vap, va_acl);
#endif
	ZFS_EXIT(zfsvfs);
    dprintf("zfsctl: -snapdir_getattr\n");

	return (0);
}

/* ARGSUSED */
static int
zfsctl_snapdir_reclaim(ap)
	struct vnop_inactive_args /* {
		struct vnode *a_vp;
		struct thread *a_td;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	zfsctl_snapdir_t *sdp = vnode_fsnode(vp);
	zfs_snapentry_t *sep;

    vnode_removefsref(vp);

    if (!sdp) return 0;

	/*
	 * On forced unmount we have to free snapshots from here.
	 */
	mutex_enter(&sdp->sd_lock);
	while ((sep = avl_first(&sdp->sd_snaps)) != NULL) {
		dprintf("Removing (reclaim) snap '%s'\n", sep->se_name);
		avl_remove(&sdp->sd_snaps, sep);
		kmem_free(sep->se_name, strlen(sep->se_name) + 1);
		kmem_free(sep, sizeof (zfs_snapentry_t));
	}
	mutex_exit(&sdp->sd_lock);
	gfs_dir_inactive(vp);
	ASSERT(avl_numnodes(&sdp->sd_snaps) == 0);
	mutex_destroy(&sdp->sd_lock);
	avl_destroy(&sdp->sd_snaps);
	kmem_free(sdp, sizeof (zfsctl_snapdir_t));

    vnode_clearfsnode(vp);

	return (0);
}

<<<<<<< HEAD:ZFSin/zfs/module/zfs/zfs_ctldir.c
#ifdef sun
static const fs_operation_def_t zfsctl_tops_snapdir[] = {
	{ VOPNAME_OPEN,		{ .vop_open = zfsctl_common_open }	},
	{ VOPNAME_CLOSE,	{ .vop_close = zfsctl_common_close }	},
	{ VOPNAME_IOCTL,	{ .error = fs_inval }			},
	{ VOPNAME_GETATTR,	{ .vop_getattr = zfsctl_snapdir_getattr } },
	{ VOPNAME_ACCESS,	{ .vop_access = zfsctl_common_access }	},
	{ VOPNAME_RENAME,	{ .vop_rename = zfsctl_snapdir_rename }	},
	{ VOPNAME_RMDIR,	{ .vop_rmdir = zfsctl_snapdir_remove }	},
	{ VOPNAME_MKDIR,	{ .vop_mkdir = zfsctl_snapdir_mkdir }	},
	{ VOPNAME_READDIR,	{ .vop_readdir = gfs_vop_readdir }	},
	{ VOPNAME_LOOKUP,	{ .vop_lookup = zfsctl_snapdir_lookup }	},
	{ VOPNAME_SEEK,		{ .vop_seek = fs_seek }			},
	{ VOPNAME_INACTIVE,	{ .vop_inactive = zfsctl_snapdir_inactive } },
	{ VOPNAME_FID,		{ .vop_fid = zfsctl_common_fid }	},
	{ NULL }
};

static const fs_operation_def_t zfsctl_tops_shares[] = {
	{ VOPNAME_OPEN,		{ .vop_open = zfsctl_common_open }	},
	{ VOPNAME_CLOSE,	{ .vop_close = zfsctl_common_close }	},
	{ VOPNAME_IOCTL,	{ .error = fs_inval }			},
	{ VOPNAME_GETATTR,	{ .vop_getattr = zfsctl_shares_getattr } },
	{ VOPNAME_ACCESS,	{ .vop_access = zfsctl_common_access }	},
	{ VOPNAME_READDIR,	{ .vop_readdir = zfsctl_shares_readdir } },
	{ VOPNAME_LOOKUP,	{ .vop_lookup = zfsctl_shares_lookup }	},
	{ VOPNAME_SEEK,		{ .vop_seek = fs_seek }			},
	{ VOPNAME_INACTIVE,	{ .vop_inactive = gfs_vop_inactive } },
	{ VOPNAME_FID,		{ .vop_fid = zfsctl_shares_fid } },
	{ NULL }
};
#endif	/* !sun */

#ifdef __FreeBSD__
static struct vop_vector zfsctl_ops_snapdir = {
	.vop_default =	&default_vnodeops,
	.vop_open =	zfsctl_common_open,
	.vop_close =	zfsctl_common_close,
	.vop_ioctl =	VOP_EINVAL,
	.vop_getattr =	zfsctl_snapdir_getattr,
	.vop_access =	zfsctl_common_access,
	.vop_mkdir =	zfsctl_freebsd_snapdir_mkdir,
	.vop_readdir =	gfs_vop_readdir,
	.vop_lookup =	zfsctl_snapdir_lookup,
	.vop_inactive =	zfsctl_snapdir_inactive,
	.vop_reclaim =	zfsctl_common_reclaim,
	.vop_fid =	zfsctl_common_fid,
};

static struct vop_vector zfsctl_ops_shares = {
	.vop_default =	&default_vnodeops,
	.vop_open =	zfsctl_common_open,
	.vop_close =	zfsctl_common_close,
	.vop_ioctl =	VOP_EINVAL,
	.vop_getattr =	zfsctl_shares_getattr,
	.vop_access =	zfsctl_common_access,
	.vop_readdir =	zfsctl_shares_readdir,
	.vop_lookup =	zfsctl_shares_lookup,
	.vop_inactive =	gfs_vop_inactive,
	.vop_reclaim =	zfsctl_common_reclaim,
	.vop_fid =	zfsctl_shares_fid,
};
#endif	/* FreeBSD */

#ifdef _WIN32
=======
#ifdef __APPLE__
>>>>>>> b3b9184f... Snapshot mounts fail on second mount:module/zfs/zfs_ctldir.c

static struct vnodeopv_entry_desc zfsctl_ops_snapdir_template[] = {
	{&vnop_default_desc, 	(VOPFUNC)vn_default_error },
	{&vnop_open_desc,	(VOPFUNC)zfsctl_common_open},
	{&vnop_close_desc,	(VOPFUNC)zfsctl_common_close},
	//{&vnop_ioctl_desc,	(VOPFUNC)zfs_vnop_ioctl},
	{&vnop_getattr_desc,	(VOPFUNC)zfsctl_snapdir_getattr},
	{&vnop_access_desc,	(VOPFUNC)zfsctl_common_access},
	{&vnop_mkdir_desc,	(VOPFUNC)zfsctl_freebsd_snapdir_mkdir},
	{&vnop_readdir_desc,	(VOPFUNC)gfs_vop_readdir},
	//{&vnop_readdirattr_desc, (VOPFUNC)zfs_vnop_readdirattr},
	{&vnop_lookup_desc,	(VOPFUNC)zfsctl_snapdir_lookup},
	{&vnop_reclaim_desc,	(VOPFUNC)zfsctl_snapdir_reclaim},
    //	{&vnop_reclaim_desc,	(VOPFUNC)zfsctl_common_reclaim},
	{NULL, (VOPFUNC)NULL }
};
struct vnodeopv_desc zfsctl_ops_snapdir =
{ &zfsctl_ops_snapdir_dvnodeops, zfsctl_ops_snapdir_template };

<<<<<<< HEAD:ZFSin/zfs/module/zfs/zfs_ctldir.c
#ifndef _WIN32
int (**zfsctl_ops_shares_dvnodeops) (void *);
static struct vnodeopv_entry_desc zfsctl_ops_shares_template[] = {
	{&vnop_default_desc, 	(VOPFUNC)vn_default_error },
	{&vnop_open_desc,	(VOPFUNC)zfsctl_common_open},
	{&vnop_close_desc,	(VOPFUNC)zfsctl_common_close},
	//{&vnop_ioctl_desc,	(VOPFUNC)zfs_vnop_ioctl},
	{&vnop_getattr_desc,	(VOPFUNC)zfsctl_shares_getattr},
	{&vnop_access_desc,	(VOPFUNC)zfsctl_common_access},
	{&vnop_readdir_desc,	(VOPFUNC)zfsctl_shares_readdir},
	//{&vnop_readdirattr_desc, (VOPFUNC)zfs_vnop_readdirattr},
	{&vnop_lookup_desc,	(VOPFUNC)zfsctl_shares_lookup},
	{&vnop_inactive_desc,	(VOPFUNC)gfs_vop_inactive},
	{&vnop_reclaim_desc,	(VOPFUNC)zfsctl_common_reclaim},
	{NULL, (VOPFUNC)NULL }
};
struct vnodeopv_desc zfsctl_ops_shares =
{ &zfsctl_ops_shares_dvnodeops, zfsctl_ops_shares_template };
#endif

=======
>>>>>>> b3b9184f... Snapshot mounts fail on second mount:module/zfs/zfs_ctldir.c
#endif

/*
 * pvp is the GFS vnode '.zfs/snapshot'.
 *
 * This creates a GFS node under '.zfs/snapshot' representing each
 * snapshot.  This newly created GFS node is what we mount snapshot
 * vfs_t's ontop of.
 */
static struct vnode *
zfsctl_snapshot_mknode(struct vnode *pvp, uint64_t objset)
{
	struct vnode *vp = NULL;
	zfsctl_node_t *zcp;
#if 1
    dprintf("+snapshot_mknode\n");
	vp = gfs_dir_create(sizeof (zfsctl_node_t), pvp, vnode_mount(pvp),
         zfsctl_ops_snapshot_dvnodeops, NULL, NULL, MAXNAMELEN, NULL, NULL, 0);
	zcp = vnode_fsnode(vp);
	zcp->zc_id = objset;
    dprintf("-snapshot_mknode\n");
#endif
	return (vp);
}

static int
zfsctl_snapshot_inactive(ap)
	struct vnop_inactive_args /* {
		struct vnode *a_vp;
		struct thread *a_td;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	cred_t *cr = (cred_t *)vfs_context_ucred((ap)->a_context);
	struct vnop_inactive_args iap;
	zfsctl_snapdir_t *sdp;
	zfs_snapentry_t *sep, *next;
	int locked;
	struct vnode *dvp;

    dprintf("zfsctl_snapshot_inactive: %p mountedhere %p\n",vp,
            vnode_mountedhere(vp));

	if (vnode_isinuse(vp,1))
		goto end;

	VERIFY(gfs_dir_lookup(vp, "..", &dvp, cr, 0, NULL, NULL) == 0);
	sdp = vnode_fsnode(dvp);

	if (!(locked = MUTEX_HELD(&sdp->sd_lock)))
		mutex_enter(&sdp->sd_lock);

	ASSERT(!vn_ismntpt(vp));

	sep = avl_first(&sdp->sd_snaps);
	while (sep != NULL) {
		next = AVL_NEXT(&sdp->sd_snaps, sep);

		if (sep->se_root == vp) {
			dprintf("Removing (inactive) snap '%s'\n", sep->se_name);
			avl_remove(&sdp->sd_snaps, sep);
			kmem_free(sep->se_name, strlen(sep->se_name) + 1);
			kmem_free(sep, sizeof (zfs_snapentry_t));
			/*
			 * After releasing the snapshot/$name entry, we need to
			 * recycle the vnode, as we will always create a new one
			 * in zfsctl_snapdir_lookup() - we do not keep a reference
			 * to it once the AVL node is removed.
			 */
			vnode_recycle(vp);
			break;
		}
		sep = next;
	}
	ASSERT(sep != NULL);

	if (!locked)
		mutex_exit(&sdp->sd_lock);
	VN_RELE(dvp);

end:
	/*
	 * Dispose of the vnode for the snapshot mount point.
	 * This is safe to do because once this entry has been removed
	 * from the AVL tree, it can't be found again, so cannot become
	 * "active".  If we lookup the same name again we will end up
	 * creating a new vnode.
	 */
	iap.a_vp = vp;
	return (gfs_vop_inactive(&iap));
}

<<<<<<< HEAD:ZFSin/zfs/module/zfs/zfs_ctldir.c
#if 0 // unused function
static int
zfsctl_traverse_begin(struct vnode **vpp, int lktype)
{
	VN_HOLD(*vpp);
	/* Snapshot should be already mounted, but just in case. */
    if (vnode_mount(*vpp) == NULL)
        return (ENOENT);
	return (traverse(vpp, lktype));
}

static void
zfsctl_traverse_end(struct vnode *vp, int err)
{
    VN_RELE(vp);
}

static int
zfsctl_snapshot_getattr(ap)
	struct vnop_getattr_args /* {
		struct vnode *a_vp;
		struct vattr *a_vap;
		struct ucred *a_cred;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	int err;

    dprintf("zfsctl: XXX +snapshot_getattr\n");
	err = zfsctl_traverse_begin(&vp, LK_SHARED | LK_RETRY);
	if (err == 0)
		err = VOP_GETATTR(vp, ap->a_vap, 0, NULL, NULL);
	zfsctl_traverse_end(vp, err);
    dprintf("zfsctl: XXX -snapshot_getattr\n");
	return (err);
}
#endif

#ifdef _WIN32
=======
#ifdef __APPLE__
>>>>>>> b3b9184f... Snapshot mounts fail on second mount:module/zfs/zfs_ctldir.c
/*
 * This call is pretty much identical to snapdir_getattr, but we have
 * separated them to avoid any vnop_snapshot calling vnop_snapdir and the
 * confusion that can come from that.
 */

/* ARGSUSED */
static int
zfsctl_snapshot_getattr(ap)
	struct vnop_getattr_args /* {
		struct vnode *a_vp;
		struct vattr *a_vap;
		struct ucred *a_cred;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	vattr_t *vap = ap->a_vap;
	zfsvfs_t *zfsvfs = vfs_fsprivate(vnode_mount(vp));
	zfsctl_snapdir_t *sdp = vnode_fsnode(vp);

    dprintf("zfsctl: +snapshot_getattr: %p: (v_data %p)\n", vp, sdp);

    if (!sdp) return ENOENT;

	ZFS_ENTER(zfsvfs);
	zfsctl_common_getattr(vp, vap);
	vap->va_nodeid = gfs_file_inode(vp);
	vap->va_nlink = vap->va_size = 2;
	vap->va_ctime = vap->va_mtime = vap->va_mtime = dmu_objset_snap_cmtime(zfsvfs->z_os);
#ifdef _WIN32
    VATTR_SET_SUPPORTED(vap, va_modify_time);
    VATTR_SET_SUPPORTED(vap, va_create_time);
    VATTR_SET_SUPPORTED(vap, va_nlink);
    VATTR_SET_SUPPORTED(vap, va_fileid);
    VATTR_CLEAR_SUPPORTED(vap, va_acl);
#endif
	ZFS_EXIT(zfsvfs);
    dprintf("zfsctl: -snapshot_getattr\n");

	return (0);
}

#endif

<<<<<<< HEAD:ZFSin/zfs/module/zfs/zfs_ctldir.c


#ifndef _WIN32
static int
zfsctl_snapshot_fid(ap)
	struct vnop_fid_args /* {
		struct vnode *a_vp;
		struct fid *a_fid;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	int err;

	err = zfsctl_traverse_begin(&vp, LK_SHARED | LK_RETRY);
	if (err == 0)
		err = VOP_VPTOFH(vp, (void *)ap->a_fid);
	zfsctl_traverse_end(vp, err);
	return (err);
}
#endif

=======
>>>>>>> b3b9184f... Snapshot mounts fail on second mount:module/zfs/zfs_ctldir.c
#if 1 // unused function
static int
zfsctl_snapshot_lookup(ap)
	struct vnop_lookup_args /* {
		struct vnode *a_dvp;
		struct vnode **a_vpp;
		struct componentname *a_cnp;
	} */ *ap;
{
	struct vnode *dvp = ap->a_dvp;
	struct vnode **vpp = ap->a_vpp;
	struct componentname *cnp = ap->a_cnp;
	cred_t *cr = (cred_t *)vfs_context_ucred((ap)->a_context);

	zfsvfs_t *zfsvfs = vfs_fsprivate(vnode_mount(dvp));
	int error;

	if (cnp->cn_namelen != 2 || cnp->cn_nameptr[0] != '.' ||
	    cnp->cn_nameptr[1] != '.') {
		return (ENOENT);
	}

	ASSERT(vnode_isdir(dvp));
	ASSERT(zfsvfs->z_ctldir != NULL);

    dprintf("zfsctl_snapshot_lookup 'snapshot' name '%s'\n",
		cnp->cn_nameptr);

	error = zfsctl_root_lookup(zfsvfs->z_ctldir, "snapshot", vpp,
	    NULL, 0, NULL, cr, NULL, NULL, NULL);

	return (error);
}
#endif

<<<<<<< HEAD:ZFSin/zfs/module/zfs/zfs_ctldir.c
#ifndef _WIN32
static int
zfsctl_snapshot_vptocnp(struct vnop_vptocnp_args *ap)
{
	zfsvfs_t *zfsvfs = vfs_fsprivate(vnode_mount(ap->a_vp));
	struct vnode *dvp, *vp;
	zfsctl_snapdir_t *sdp;
	zfs_snapentry_t *sep;
	int error;

	ASSERT(zfsvfs->z_ctldir != NULL);
	error = zfsctl_root_lookup(zfsvfs->z_ctldir, "snapshot", &dvp,
	    NULL, 0, NULL, kcred, NULL, NULL, NULL);
	if (error != 0)
		return (error);
	sdp = vnode_fsnode(dvp);

	mutex_enter(&sdp->sd_lock);
	sep = avl_first(&sdp->sd_snaps);
	while (sep != NULL) {
		vp = sep->se_root;
		if (vp == ap->a_vp)
			break;
		sep = AVL_NEXT(&sdp->sd_snaps, sep);
	}
	if (sep == NULL) {
		mutex_exit(&sdp->sd_lock);
		error = ENOENT;
	} else {
		size_t len;

		len = strlen(sep->se_name);
		*ap->a_buflen -= len;
		bcopy(sep->se_name, ap->a_buf + *ap->a_buflen, len);
		mutex_exit(&sdp->sd_lock);
		vref(dvp);
		*ap->a_vpp = dvp;
	}
	VN_RELE(dvp);

	return (error);
}
#endif

/*
 * These VP's should never see the light of day.  They should always
 * be covered.
 */
#ifndef _WIN32
static struct vop_vector zfsctl_ops_snapshot = {
	.vop_default =	&default_vnodeops,
	.vop_inactive =	zfsctl_snapshot_inactive,
	.vop_lookup =	zfsctl_snapshot_lookup,
	.vop_reclaim =	zfsctl_common_reclaim,
	.vop_getattr =	zfsctl_snapshot_getattr,
	.vop_fid =	zfsctl_snapshot_fid,
	.vop_vptocnp =	zfsctl_snapshot_vptocnp,
};
#endif

#ifdef _WIN32
static struct vnodeopv_entry_desc zfsctl_ops_snapshot_template[] = {
	{&vnop_default_desc, 	(VOPFUNC)vn_default_error },
	{&vnop_inactive_desc,	(VOPFUNC)zfsctl_snapshot_inactive},
	{&vnop_reclaim_desc,	(VOPFUNC)zfsctl_common_reclaim},

    /*
     * In normal ZFS, the ".zfs/snashot/snap", the "snap" is immediately
     * mounted over, so these vnodeops are not used. But in OSX, since we
     * are unable to mount from the kernel, we need to define enough vnodeops
     * such that userland mount call will succeed.
     */
	{&vnop_getattr_desc,	(VOPFUNC)zfsctl_snapshot_getattr},
    {&vnop_revoke_desc,     (VOPFUNC)err_revoke },
    {&vnop_fsync_desc,      (VOPFUNC)nop_fsync },

	{&vnop_lookup_desc,	    (VOPFUNC)zfsctl_snapshot_lookup},

	//{&vnop_readdir_desc,	(VOPFUNC)gfs_vop_readdir},
	{&vnop_readdir_desc,	(VOPFUNC)nop_readdir},

    //	{&vnop_open_desc,	(VOPFUNC)zfsctl_common_openX},
	//{&vnop_close_desc,	(VOPFUNC)zfsctl_common_closeX},
    //{&vnop_open_desc,	(VOPFUNC)nop_open},
	//{&vnop_close_desc,	(VOPFUNC)nop_close},

	{NULL, (VOPFUNC)NULL }
};
struct vnodeopv_desc zfsctl_ops_snapshot =
{ &zfsctl_ops_snapshot_dvnodeops, zfsctl_ops_snapshot_template };
#endif



=======
>>>>>>> b3b9184f... Snapshot mounts fail on second mount:module/zfs/zfs_ctldir.c
int
zfsctl_lookup_objset(vfs_t *vfsp, uint64_t objsetid, zfsvfs_t **zfsvfsp)
{
	zfsvfs_t *zfsvfs = vfs_fsprivate(vfsp);
	struct vnode *dvp, *vp;
	zfsctl_snapdir_t *sdp;
	zfsctl_node_t *zcp;
	zfs_snapentry_t *sep;
	int error;
    dprintf("zfsctl_lookup_objset\n");

	ASSERT(zfsvfs->z_ctldir != NULL);
	error = zfsctl_root_lookup(zfsvfs->z_ctldir, "snapshot", &dvp,
	    NULL, 0, NULL, kcred, NULL, NULL, NULL);
	if (error != 0)
		return (error);
	sdp = vnode_fsnode(dvp);

	mutex_enter(&sdp->sd_lock);
	sep = avl_first(&sdp->sd_snaps);
	while (sep != NULL) {
		vp = sep->se_root;
		zcp = vnode_fsnode(vp);
		if (zcp->zc_id == objsetid)
			break;

		sep = AVL_NEXT(&sdp->sd_snaps, sep);
	}

	if (sep != NULL) {
		VN_HOLD(vp);
		/*
		 * Return the mounted root rather than the covered mount point.
		 * Takes the GFS vnode at .zfs/snapshot/<snapshot objsetid>
		 * and returns the ZFS vnode mounted on top of the GFS node.
		 * This ZFS vnode is the root of the vfs for objset 'objsetid'.
		 */
		error = traverse(&vp, LK_SHARED | LK_RETRY);
		if (error == 0) {
			if (vp == sep->se_root)
				error = EINVAL;
			else
				*zfsvfsp = VTOZ(vp)->z_zfsvfs;
		}
		mutex_exit(&sdp->sd_lock);
        VN_RELE(vp);
	} else {
		error = EINVAL;
		mutex_exit(&sdp->sd_lock);
	}

	VN_RELE(dvp);

	return (error);
}

/*
 * Unmount any snapshots for the given filesystem.  This is called from
 * zfs_umount() - if we have a ctldir, then go through and unmount all the
 * snapshots.
 */
int
zfsctl_umount_snapshots(vfs_t *vfsp, int fflags, cred_t *cr)
{
	zfsvfs_t *zfsvfs = vfs_fsprivate(vfsp);
	struct vnode *dvp;
	zfsctl_snapdir_t *sdp;
	zfs_snapentry_t *sep, *next;
	int error;

    dprintf("unmount_snapshots\n");

	ASSERT(zfsvfs->z_ctldir != NULL);
	error = zfsctl_root_lookup(zfsvfs->z_ctldir, "snapshot", &dvp,
	    NULL, 0, NULL, cr, NULL, NULL, NULL);
	if (error != 0)
		return (error);

	sdp = vnode_fsnode(dvp);
    if (!sdp) return 0;

	mutex_enter(&sdp->sd_lock);

	sep = avl_first(&sdp->sd_snaps);
	while (sep != NULL) {
		next = AVL_NEXT(&sdp->sd_snaps, sep);

		/*
		 * If this snapshot is not mounted, then it must
		 * have just been unmounted by somebody else, and
		 * will be cleaned up by zfsctl_snapdir_inactive().
		 */
		if (vn_ismntpt(sep->se_root)) {
			error = zfsctl_unmount_snap(sep, fflags, cr);
			if (error) {
				avl_index_t where;

				/*
				 * Before reinserting snapshot to the tree,
				 * check if it was actually removed. For example
				 * when snapshot mount point is busy, we will
				 * have an error here, but there will be no need
				 * to reinsert snapshot.
				 */
				if (avl_find(&sdp->sd_snaps, sep, &where) == NULL)
					avl_insert(&sdp->sd_snaps, sep, where);
				break;
			}
		}
		sep = next;
	}

	mutex_exit(&sdp->sd_lock);

	VN_RELE(dvp);

    dprintf("umount_snapshot err %d\n", error);
	return (error);
}



/*
 * Covered VNOPs, from before the snapshot is mounted
 */
/* ARGSUSED */
static int
zfsctl_covered_open(struct vnop_open_args *ap)
{
	int flags = ap->a_mode;

    dprintf("%s: %p on %p\n", __func__,
           ap->a_vp, vnode_mountedhere(ap->a_vp));

	if (flags & FWRITE)
        return (EACCES);

	return (0);
}

/* ARGSUSED */
static int
zfsctl_covered_close(struct vnop_close_args *ap)
{
	return (0);
}

static int
zfsctl_covered_readdir(struct vnop_readdir_args *ap)
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

	dprintf("%s: %p\n", __func__, ap->a_vp);

	if (*ap->a_numdirent == 0)
		*ap->a_numdirent = 2; /* . and .. */

	return 0;
}


#ifdef __APPLE__
static struct vnodeopv_entry_desc zfsctl_ops_snapshot_template[] = {
	{&vnop_default_desc, 	(VOPFUNC)vn_default_error },
	{&vnop_inactive_desc,	(VOPFUNC)zfsctl_snapshot_inactive},
	{&vnop_reclaim_desc,	(VOPFUNC)zfsctl_common_reclaim},

    /*
     * In normal ZFS, the ".zfs/snashot/snap", the "snap" is immediately
     * mounted over, so these vnodeops are not used. But in OSX, since we
     * are unable to mount from the kernel, we need to define enough vnodeops
     * such that userland mount call will succeed.
     */
	{&vnop_getattr_desc,	(VOPFUNC)zfsctl_snapshot_getattr},
    {&vnop_revoke_desc,     (VOPFUNC)err_revoke },
    {&vnop_fsync_desc,      (VOPFUNC)nop_fsync },

	{&vnop_lookup_desc,	    (VOPFUNC)zfsctl_snapshot_lookup},

	//{&vnop_readdir_desc,	(VOPFUNC)gfs_vop_readdir},
	{&vnop_readdir_desc,	(VOPFUNC)zfsctl_covered_readdir},

	{&vnop_open_desc,	(VOPFUNC)zfsctl_covered_open},
	{&vnop_close_desc,	(VOPFUNC)zfsctl_covered_close},
    //{&vnop_open_desc,	(VOPFUNC)nop_open},
	//{&vnop_close_desc,	(VOPFUNC)nop_close},

	{NULL, (VOPFUNC)NULL }
};
struct vnodeopv_desc zfsctl_ops_snapshot =
{ &zfsctl_ops_snapshot_dvnodeops, zfsctl_ops_snapshot_template };
#endif
