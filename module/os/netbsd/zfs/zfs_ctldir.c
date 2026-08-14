// SPDX-License-Identifier: CDDL-1.0
/*
 * NetBSD .zfs control directory -- snapshot automount.
 *
 * From NetBSD 11.0 external/cddl/osnet zfs_ctldir.c (sfs_node +
 * vcache_get, sfs_lookup_snapshot cover-mount).  Lookup of
 * .zfs/snapshot/<name> cover-mounts the snapshot and returns its
 * root.  The mounter and waiters drop the snapdir lock so the
 * mounter can vn_lock it after sfs_snapshot_mount.  .zfs/shares is
 * the ZPL shares dir.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/namei.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/fstrans.h>
#include <sys/kmem.h>
#include <sys/condvar.h>
#include <sys/dirent.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <sys/malloc.h>
#include <sys/pathname.h>
#include <miscfs/genfs/genfs.h>
#include <uvm/uvm.h>

#include <sys/zfs_context.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_ctldir.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_ioctl_impl.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_deleg.h>
#include <sys/dsl_destroy.h>
#include <sys/zap.h>
#include <sys/sa.h>
#include <sys/spa.h>
#include <sys/fs/zfs.h>

#include "zfs_namecheck.h"

extern struct vfsops zfs_vfsops;
extern int zfs_domount(vfs_t *, char *);
extern int getzfsvfs(const char *, zfsvfs_t **);

#define	ZFS_SNAPDIR_NAME	"snapshot"
#define	ZFS_SHARESDIR_NAME	"shares"

struct zfsctl_root {
	timestruc_t	zc_cmtime;
};

struct sfs_node_key {
	uint64_t	parent_id;
	uint64_t	id;
};

struct sfs_node {
	struct sfs_node_key	sn_key;
#define	sn_parent_id	sn_key.parent_id
#define	sn_id		sn_key.id
	lwp_t		*sn_mounting;
	kcondvar_t	sn_cv;
};

#define	VTOSFS(vp)	((struct sfs_node *)((vp)->v_data))

#define	SFS_NODE_ASSERT(vp) \
	do { \
		ASSERT((vp)->v_op == zfs_sfsop_p); \
		ASSERT((vp)->v_type == VDIR); \
	} while (/*CONSTCOND*/ 0)

int (**zfs_sfsop_p)(void *);
int (**zfsctl_vnodeop_p)(void *);	/* alias for zfsctl_is_node callers */

static int
sfs_snapshot_mount(vnode_t *vp, const char *snapname)
{
	struct sfs_node *node = VTOSFS(vp);
	zfsvfs_t *zfsvfs = vp->v_mount->mnt_data;
	vfs_t *vfsp;
	char *path, *osname;
	int error;
	int domounted = 0;

	(void) node;
	path = kmem_alloc(MAXPATHLEN, KM_SLEEP);
	osname = kmem_alloc(MAXPATHLEN, KM_SLEEP);

	dmu_objset_name(zfsvfs->z_os, path);
	(void) snprintf(osname, MAXPATHLEN, "%s@%s", path, snapname);
	(void) snprintf(path, MAXPATHLEN,
	    "%s/" ZFS_CTLDIR_NAME "/" ZFS_SNAPDIR_NAME "/%s",
	    vp->v_mount->mnt_stat.f_mntonname, snapname);

	vfsp = vfs_mountalloc(vp->v_mount->mnt_op, vp);
	if (vfsp == NULL) {
		error = ENOMEM;
		goto out;
	}
	vfsp->mnt_op->vfs_refcount++;
	vfsp->mnt_stat.f_owner = 0;
	/*
	 * Inherit nodev/nosuid/noexec from the covered fs so a
	 * nodev mount does not grow live device nodes in snapshots.
	 */
	vfsp->mnt_flag = MNT_RDONLY | MNT_IGNORE |
	    (vp->v_mount->mnt_flag & (MNT_NOSUID | MNT_NODEV | MNT_NOEXEC));
	if ((vfsp->mnt_flag & MNT_NOSUID) == 0)
		vfsp->mnt_flag |= MNT_NOSUID;

	mutex_enter(vfsp->mnt_updating);

	error = zfs_domount(vfsp, osname);
	if (error)
		goto fail;
	domounted = 1;

	vfsp->mnt_stat.f_fsidx = vp->v_mount->mnt_stat.f_fsidx;
	strlcpy(vfsp->mnt_stat.f_mntfromname, osname,
	    sizeof (vfsp->mnt_stat.f_mntfromname));
	set_statvfs_info(path, UIO_SYSSPACE, vfsp->mnt_stat.f_mntfromname,
	    UIO_SYSSPACE, vfsp->mnt_op->vfs_name, vfsp, curlwp);

	error = vfs_set_lowermount(vfsp, vp->v_mount);
	if (error)
		goto fail;

	mountlist_append(vfsp);
	vref(vp);
	vp->v_mountedhere = vfsp;

	mutex_exit(vfsp->mnt_updating);
	(void) VFS_STATVFS(vfsp, &vfsp->mnt_stat);
	kmem_free(osname, MAXPATHLEN);
	kmem_free(path, MAXPATHLEN);
	return (0);

fail:
	/*
	 * vfs_rele without VFS_UNMOUNT leaves the zfsvfs, objset
	 * hold and znode cache behind (mount() err_mounted calls
	 * VFS_UNMOUNT first).  Do not panic: automount failure
	 * must not take the guest with it.
	 */
	if (domounted)
		(void) VFS_UNMOUNT(vfsp, MNT_FORCE);
	mutex_exit(vfsp->mnt_updating);
	if (domounted)
		vfs_set_lowermount(vfsp, NULL);
	vfs_rele(vfsp);
out:
	kmem_free(osname, MAXPATHLEN);
	kmem_free(path, MAXPATHLEN);
	return (error);
}

/*
 * Pin vp->v_mountedhere for VFS_ROOT.  A cover-vnode hold does not
 * stop dounmount: it NULLs v_mountedhere and drops the mount-time
 * ref.  vfs_busy is fstrans plus a ref, and fstrans is per LWP, so
 * the unbusy stays on this lookup LWP.
 */
static int
sfs_busy_mountedhere(vnode_t *vp, struct mount **mpp)
{
	struct mount *mp;
	int error;

	mutex_enter(vp->v_interlock);
	mp = vp->v_mountedhere;
	if (mp != NULL)
		vfs_ref(mp);
	mutex_exit(vp->v_interlock);
	if (mp == NULL)
		return (ENOENT);
	error = vfs_busy(mp);
	vfs_rele(mp);
	if (error != 0)
		return (error);
	if (vp->v_mountedhere != mp) {
		vfs_unbusy(mp);
		return (ENOENT);
	}
	*mpp = mp;
	return (0);
}

static int
sfs_lookup_snapshot(vnode_t *dvp, struct componentname *cnp, vnode_t **vpp)
{
	zfsvfs_t *zfsvfs = dvp->v_mount->mnt_data;
	vnode_t *vp;
	struct sfs_node *node;
	struct sfs_node_key key;
	char snapname[ZFS_MAX_DATASET_NAME_LEN];
	int error;

	if (cnp->cn_namelen >= sizeof (snapname))
		return (ENOENT);

	memcpy(snapname, cnp->cn_nameptr, cnp->cn_namelen);
	snapname[cnp->cn_namelen] = '\0';
	error = dsl_dataset_snap_lookup(dmu_objset_ds(zfsvfs->z_os),
	    snapname, &key.id);
	if (error)
		return (error);
	key.parent_id = ZFSCTL_INO_SNAPDIR;
	error = vcache_get(zfsvfs->z_vfs, &key, sizeof (key), vpp);
	if (error)
		return (error);

	vp = *vpp;
	mutex_enter(vp->v_interlock);
	for (;;) {
		node = VTOSFS(vp);
		if (node == NULL) {
			mutex_exit(vp->v_interlock);
			vrele(vp);
			*vpp = NULL;
			return (ENOENT);
		}
		/*
		 * Re-entered from our own sfs_snapshot_mount.
		 * The cover is not up yet.  Hand back the placeholder;
		 * do not VFS_ROOT(NULL).
		 */
		if (node->sn_mounting == curlwp) {
			mutex_exit(vp->v_interlock);
			return (0);
		}
		if (node->sn_mounting == NULL)
			break;
		/*
		 * Another LWP is cover-mounting.  Drop snapdir before
		 * sleeping: the mounter vn_lock(dvp) after
		 * sfs_snapshot_mount.
		 */
		mutex_exit(vp->v_interlock);
		VOP_UNLOCK(dvp);
		mutex_enter(vp->v_interlock);
		if ((node = VTOSFS(vp)) != NULL &&
		    node->sn_mounting != NULL &&
		    node->sn_mounting != curlwp)
			cv_wait(&node->sn_cv, vp->v_interlock);
		mutex_exit(vp->v_interlock);
		if (vn_lock(dvp, LK_EXCLUSIVE) != 0)
			(void) vn_lock(dvp, LK_EXCLUSIVE | LK_RETRY);
		mutex_enter(vp->v_interlock);
	}

	if (vp->v_mountedhere == NULL) {
		node->sn_mounting = curlwp;
		mutex_exit(vp->v_interlock);

		VOP_UNLOCK(dvp);
		error = sfs_snapshot_mount(vp, snapname);
		if (vn_lock(dvp, LK_EXCLUSIVE) != 0)
			(void) vn_lock(dvp, LK_EXCLUSIVE | LK_RETRY);

		mutex_enter(vp->v_interlock);
		if ((node = VTOSFS(vp)) != NULL) {
			node->sn_mounting = NULL;
			cv_broadcast(&node->sn_cv);
		}
		mutex_exit(vp->v_interlock);

		if (error) {
			vrele(vp);
			*vpp = NULL;
			return (error);
		}
	} else {
		mutex_exit(vp->v_interlock);
	}

	{
		struct mount *mp;

		error = sfs_busy_mountedhere(vp, &mp);
		if (error) {
			vrele(vp);
			*vpp = NULL;
			return (error);
		}
		error = VFS_ROOT(mp, LK_EXCLUSIVE, vpp);
		vfs_unbusy(mp);
	}
	vrele(vp);
	if (error)
		return (error);

	/*
	 * Keep VV_ROOT: unmount(2) of this path requires it.
	 * z_parent is the covered filesystem for ISDOTDOT.
	 */
	if (((*vpp)->v_vflag & VV_ROOT) != 0)
		VTOZ(*vpp)->z_zfsvfs->z_parent = zfsvfs;
	VOP_UNLOCK(*vpp);
	return (0);
}

static int
zfsctl_root_unlocked(zfsvfs_t *zfsvfs, vnode_t **vpp)
{
	struct sfs_node_key key = {
		.parent_id = 0,
		.id = ZFSCTL_INO_ROOT
	};

	return (vcache_get(zfsvfs->z_vfs, &key, sizeof (key), vpp));
}

int
zfsctl_snapshot(zfsvfs_t *zfsvfs, vnode_t **vpp)
{
	struct sfs_node_key key = {
		.parent_id = ZFSCTL_INO_ROOT,
		.id = ZFSCTL_INO_SNAPDIR
	};

	return (vcache_get(zfsvfs->z_vfs, &key, sizeof (key), vpp));
}

static int
sfs_cn_copy(struct componentname *cnp, char *buf, size_t buflen)
{

	if (cnp->cn_namelen == 0 || cnp->cn_namelen >= buflen)
		return (SET_ERROR(ENAMETOOLONG));
	memcpy(buf, cnp->cn_nameptr, cnp->cn_namelen);
	buf[cnp->cn_namelen] = '\0';
	return (0);
}

static int
sfs_snapshot_fullname(zfsvfs_t *zfsvfs, const char *snapname,
    char *full, size_t len)
{

	if (zfs_component_namecheck(snapname, NULL, NULL) != 0)
		return (SET_ERROR(EILSEQ));
	dmu_objset_name(zfsvfs->z_os, full);
	if (strlen(full) + 1 + strlen(snapname) >= len)
		return (SET_ERROR(ENAMETOOLONG));
	(void) strcat(full, "@");
	(void) strcat(full, snapname);
	return (0);
}

static int
sfs_cover_get(zfsvfs_t *zfsvfs, const char *snapname, vnode_t **vpp)
{
	struct sfs_node_key key;
	int error;

	error = dsl_dataset_snap_lookup(dmu_objset_ds(zfsvfs->z_os),
	    snapname, &key.id);
	if (error != 0)
		return (error);
	key.parent_id = ZFSCTL_INO_SNAPDIR;
	return (vcache_get(zfsvfs->z_vfs, &key, sizeof (key), vpp));
}

static int
sfs_lookup_snapdir_create(vnode_t *dvp, struct componentname *cnp,
    vnode_t **vpp)
{
	zfsvfs_t *zfsvfs = dvp->v_mount->mnt_data;
	char snapname[ZFS_MAX_DATASET_NAME_LEN];
	uint64_t id;
	int error;

	*vpp = NULL;
	error = sfs_cn_copy(cnp, snapname, sizeof (snapname));
	if (error != 0)
		return (error);
	error = dsl_dataset_snap_lookup(dmu_objset_ds(zfsvfs->z_os),
	    snapname, &id);
	if (error == 0) {
		if (cnp->cn_nameiop == RENAME)
			return (sfs_cover_get(zfsvfs, snapname, vpp));
		return (SET_ERROR(EEXIST));
	}
	if (error != ENOENT)
		return (error);
	error = VOP_ACCESS(dvp, VWRITE, cnp->cn_cred);
	if (error != 0)
		return (error);
	return (EJUSTRETURN);
}

static int
sfs_lookup_snapdir_delete(vnode_t *dvp, struct componentname *cnp,
    vnode_t **vpp)
{
	zfsvfs_t *zfsvfs = dvp->v_mount->mnt_data;
	char snapname[ZFS_MAX_DATASET_NAME_LEN];
	int error;

	*vpp = NULL;
	error = sfs_cn_copy(cnp, snapname, sizeof (snapname));
	if (error != 0)
		return (error);
	error = VOP_ACCESS(dvp, VWRITE, cnp->cn_cred);
	if (error != 0)
		return (error);
	return (sfs_cover_get(zfsvfs, snapname, vpp));
}

static int
sfs_lookup(void *v)
{
	struct vop_lookup_v2_args *ap = v;
	vnode_t *dvp = ap->a_dvp;
	vnode_t **vpp = ap->a_vpp;
	struct componentname *cnp = ap->a_cnp;
	zfsvfs_t *zfsvfs = dvp->v_mount->mnt_data;
	struct sfs_node *dnode;
	int error;

	*vpp = NULL;
	if (dvp->v_data == NULL)
		return (EINVAL);
	dnode = VTOSFS(dvp);
	SFS_NODE_ASSERT(dvp);
	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	if ((cnp->cn_flags & ISLASTCN) && cnp->cn_nameiop != LOOKUP) {
		if (dnode->sn_id != ZFSCTL_INO_SNAPDIR ||
		    (cnp->cn_nameiop != CREATE &&
		    cnp->cn_nameiop != DELETE &&
		    cnp->cn_nameiop != RENAME)) {
			zfs_exit(zfsvfs, FTAG);
			return (ENOTSUP);
		}
		if (cnp->cn_nameiop == DELETE)
			error = sfs_lookup_snapdir_delete(dvp, cnp, vpp);
		else
			error = sfs_lookup_snapdir_create(dvp, cnp, vpp);
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	if (cnp->cn_namelen == 1 && cnp->cn_nameptr[0] == '.') {
		vref(dvp);
		*vpp = dvp;
		zfs_exit(zfsvfs, FTAG);
		return (0);
	}
	if ((cnp->cn_flags & ISDOTDOT) != 0) {
		if (dnode->sn_parent_id == 0) {
			error = vcache_get(zfsvfs->z_vfs,
			    &zfsvfs->z_root, sizeof (zfsvfs->z_root), vpp);
		} else if (dnode->sn_parent_id == ZFSCTL_INO_ROOT) {
			error = zfsctl_root_unlocked(zfsvfs, vpp);
		} else if (dnode->sn_parent_id == ZFSCTL_INO_SNAPDIR) {
			error = zfsctl_snapshot(zfsvfs, vpp);
		} else {
			error = ENOENT;
		}
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	if (dnode->sn_id == ZFSCTL_INO_ROOT) {
		if (cnp->cn_namelen == strlen(ZFS_SNAPDIR_NAME) &&
		    strncmp(cnp->cn_nameptr, ZFS_SNAPDIR_NAME,
		    cnp->cn_namelen) == 0)
			error = zfsctl_snapshot(zfsvfs, vpp);
		else if (cnp->cn_namelen == strlen(ZFS_SHARESDIR_NAME) &&
		    strncmp(cnp->cn_nameptr, ZFS_SHARESDIR_NAME,
		    cnp->cn_namelen) == 0 &&
		    zfsvfs->z_shares_dir != 0) {
			znode_t *szp;

			error = zfs_zget(zfsvfs, zfsvfs->z_shares_dir, &szp);
			if (error == 0)
				*vpp = ZTOV(szp);
		} else
			error = ENOENT;
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	if (dnode->sn_id == ZFSCTL_INO_SNAPDIR) {
		/*
		 * Snapshot mount takes vnode locks.  Do not hold
		 * teardown across that: umount wants the writer
		 * and other VOPs wait behind it with dvp locked.
		 */
		zfs_exit(zfsvfs, FTAG);
		error = sfs_lookup_snapshot(dvp, cnp, vpp);
		return (error);
	}

	zfs_exit(zfsvfs, FTAG);
	return (ENOENT);
}

static int
sfs_open(void *v)
{
	struct vop_open_args *ap = v;
	vnode_t *vp = ap->a_vp;

	if ((ap->a_mode & FWRITE) != 0) {
		if (vp->v_data == NULL ||
		    VTOSFS(vp)->sn_id != ZFSCTL_INO_SNAPDIR)
			return (EACCES);
	}
	return (0);
}

static int
sfs_close(void *v)
{

	(void) v;
	return (0);
}

static int
sfs_access(void *v)
{
	struct vop_access_args *ap = v;
	vnode_t *vp = ap->a_vp;

	if ((ap->a_accmode & VWRITE) != 0) {
		if (vp->v_data == NULL ||
		    VTOSFS(vp)->sn_id != ZFSCTL_INO_SNAPDIR)
			return (EACCES);
	}
	return (0);
}

static int
sfs_getattr(void *v)
{
	struct vop_getattr_args *ap = v;
	vnode_t *vp = ap->a_vp;
	struct sfs_node *node = VTOSFS(vp);
	struct vattr *vap = ap->a_vap;
	zfsvfs_t *zfsvfs = vp->v_mount->mnt_data;
	dsl_dataset_t *ds;
	timestruc_t now;
	uint64_t snap_count;
	int error;

	SFS_NODE_ASSERT(vp);
	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	vap->va_type = VDIR;
	vap->va_mode = S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP |
	    S_IROTH | S_IXOTH;
	if (node->sn_id == ZFSCTL_INO_SNAPDIR)
		vap->va_mode |= S_IWUSR | S_IWGRP | S_IWOTH;
	vap->va_nlink = 2;
	vap->va_uid = 0;
	vap->va_gid = 0;
	vap->va_fsid = vp->v_mount->mnt_stat.f_fsidx.__fsid_val[0];
	vap->va_fileid = node->sn_id;
	vap->va_size = 0;
	vap->va_blocksize = DEV_BSIZE;
	gethrestime(&now);
	vap->va_atime = now;
	if (zfsvfs->z_ctldir != NULL)
		vap->va_ctime = zfsvfs->z_ctldir->zc_cmtime;
	else
		vap->va_ctime = now;
	vap->va_mtime = vap->va_ctime;
	vap->va_birthtime = vap->va_ctime;
	vap->va_gen = 0;
	vap->va_flags = 0;
	vap->va_rdev = 0;
	vap->va_bytes = 0;

	switch (node->sn_id) {
	case ZFSCTL_INO_ROOT:
		vap->va_nlink += 1;
		vap->va_size = vap->va_nlink;
		break;
	case ZFSCTL_INO_SNAPDIR:
		/*
		 * .zfs stays at the dataset creation time.  The snapdir
		 * clock is dd_snap_cmtime, same as FreeBSD
		 * zfsctl_snapdir_getattr and Linux zpl_snapdir_getattr.
		 * No snapshots yet: that clock is 0.
		 */
		vap->va_ctime = dmu_objset_snap_cmtime(zfsvfs->z_os);
		vap->va_mtime = vap->va_ctime;
		vap->va_birthtime = vap->va_ctime;
		ds = dmu_objset_ds(zfsvfs->z_os);
		if (dsl_dataset_phys(ds)->ds_snapnames_zapobj != 0) {
			error = zap_count(
			    dmu_objset_pool(ds->ds_objset)->dp_meta_objset,
			    dsl_dataset_phys(ds)->ds_snapnames_zapobj,
			    &snap_count);
			if (error == 0)
				vap->va_nlink += snap_count;
		}
		vap->va_size = vap->va_nlink;
		break;
	}

	zfs_exit(zfsvfs, FTAG);
	return (0);
}

static int
sfs_mkdir(void *v)
{
	struct vop_mkdir_v3_args *ap = v;
	vnode_t *dvp = ap->a_dvp;
	vnode_t **vpp = ap->a_vpp;
	struct componentname *cnp = ap->a_cnp;
	zfsvfs_t *zfsvfs = dvp->v_mount->mnt_data;
	char *snapname, *dsname;
	int error;

	*vpp = NULL;
	if (dvp->v_data == NULL || VTOSFS(dvp)->sn_id != ZFSCTL_INO_SNAPDIR)
		return (SET_ERROR(ENOTSUP));
	snapname = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);
	error = sfs_cn_copy(cnp, snapname, ZFS_MAX_DATASET_NAME_LEN);
	if (error != 0) {
		kmem_free(snapname, ZFS_MAX_DATASET_NAME_LEN);
		return (error);
	}
	if (zfs_component_namecheck(snapname, NULL, NULL) != 0) {
		kmem_free(snapname, ZFS_MAX_DATASET_NAME_LEN);
		return (SET_ERROR(EILSEQ));
	}
	if ((error = zfs_enter(zfsvfs, FTAG)) != 0) {
		kmem_free(snapname, ZFS_MAX_DATASET_NAME_LEN);
		return (error);
	}
	dsname = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);
	dmu_objset_name(zfsvfs->z_os, dsname);
	error = zfs_secpolicy_snapshot_perms(dsname, cnp->cn_cred);
	if (error == 0)
		error = dmu_objset_snapshot_one(dsname, snapname);
	if (error == 0)
		error = sfs_cover_get(zfsvfs, snapname, vpp);
	if (error != 0)
		*vpp = NULL;
	kmem_free(dsname, ZFS_MAX_DATASET_NAME_LEN);
	kmem_free(snapname, ZFS_MAX_DATASET_NAME_LEN);
	zfs_exit(zfsvfs, FTAG);
	return (error);
}

static int
sfs_rmdir(void *v)
{
	struct vop_rmdir_v2_args *ap = v;
	vnode_t *dvp = ap->a_dvp;
	vnode_t *vp = ap->a_vp;
	struct componentname *cnp = ap->a_cnp;
	zfsvfs_t *zfsvfs = dvp->v_mount->mnt_data;
	char *snapname, *full;
	int error;

	if (dvp->v_data == NULL || VTOSFS(dvp)->sn_id != ZFSCTL_INO_SNAPDIR) {
		vput(vp);
		return (SET_ERROR(ENOTSUP));
	}
	snapname = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);
	error = sfs_cn_copy(cnp, snapname, ZFS_MAX_DATASET_NAME_LEN);
	if (error != 0) {
		kmem_free(snapname, ZFS_MAX_DATASET_NAME_LEN);
		vput(vp);
		return (error);
	}
	full = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);
	error = sfs_snapshot_fullname(zfsvfs, snapname, full,
	    ZFS_MAX_DATASET_NAME_LEN);
	if (error == 0)
		error = zfs_secpolicy_destroy_perms(full, cnp->cn_cred);
	if (error != 0) {
		kmem_free(full, ZFS_MAX_DATASET_NAME_LEN);
		kmem_free(snapname, ZFS_MAX_DATASET_NAME_LEN);
		vput(vp);
		return (error);
	}
	(void) zfsctl_snapshot_unmount(full);
	error = dsl_destroy_snapshot(full, B_FALSE);
	kmem_free(full, ZFS_MAX_DATASET_NAME_LEN);
	kmem_free(snapname, ZFS_MAX_DATASET_NAME_LEN);
	vput(vp);
	return (error);
}

static void
sfs_rename_consume(vnode_t *fdvp, vnode_t *fvp, vnode_t *tdvp, vnode_t *tvp)
{

	vrele(fdvp);
	vrele(fvp);
	if (tvp != NULL)
		vput(tvp);
	vput(tdvp);
}

static int
sfs_rename(void *v)
{
	struct vop_rename_args *ap = v;
	vnode_t *fdvp = ap->a_fdvp;
	vnode_t *fvp = ap->a_fvp;
	vnode_t *tdvp = ap->a_tdvp;
	vnode_t *tvp = ap->a_tvp;
	zfsvfs_t *zfsvfs;
	char *fromsnap, *tosnap, *from, *to, *dsname;
	int error;

	if (fdvp->v_data == NULL || tdvp->v_data == NULL ||
	    VTOSFS(fdvp)->sn_id != ZFSCTL_INO_SNAPDIR ||
	    VTOSFS(tdvp)->sn_id != ZFSCTL_INO_SNAPDIR) {
		sfs_rename_consume(fdvp, fvp, tdvp, tvp);
		return (SET_ERROR(EINVAL));
	}
	if (tvp != NULL) {
		sfs_rename_consume(fdvp, fvp, tdvp, tvp);
		return (SET_ERROR(EEXIST));
	}
	fromsnap = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);
	tosnap = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);
	error = sfs_cn_copy(ap->a_fcnp, fromsnap, ZFS_MAX_DATASET_NAME_LEN);
	if (error == 0)
		error = sfs_cn_copy(ap->a_tcnp, tosnap,
		    ZFS_MAX_DATASET_NAME_LEN);
	if (error != 0) {
		kmem_free(fromsnap, ZFS_MAX_DATASET_NAME_LEN);
		kmem_free(tosnap, ZFS_MAX_DATASET_NAME_LEN);
		sfs_rename_consume(fdvp, fvp, tdvp, tvp);
		return (error);
	}
	if (strcmp(fromsnap, tosnap) == 0) {
		kmem_free(fromsnap, ZFS_MAX_DATASET_NAME_LEN);
		kmem_free(tosnap, ZFS_MAX_DATASET_NAME_LEN);
		sfs_rename_consume(fdvp, fvp, tdvp, tvp);
		return (0);
	}
	zfsvfs = fdvp->v_mount->mnt_data;
	from = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);
	to = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);
	dsname = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);
	error = sfs_snapshot_fullname(zfsvfs, fromsnap, from,
	    ZFS_MAX_DATASET_NAME_LEN);
	if (error == 0)
		error = sfs_snapshot_fullname(zfsvfs, tosnap, to,
		    ZFS_MAX_DATASET_NAME_LEN);
	if (error == 0)
		error = zfs_secpolicy_rename_perms(from, to,
		    ap->a_fcnp->cn_cred);
	if (error == 0) {
		(void) zfsctl_snapshot_unmount(from);
		dmu_objset_name(zfsvfs->z_os, dsname);
		error = dsl_dataset_rename_snapshot(dsname, fromsnap, tosnap,
		    B_FALSE);
	}
	kmem_free(from, ZFS_MAX_DATASET_NAME_LEN);
	kmem_free(to, ZFS_MAX_DATASET_NAME_LEN);
	kmem_free(dsname, ZFS_MAX_DATASET_NAME_LEN);
	kmem_free(fromsnap, ZFS_MAX_DATASET_NAME_LEN);
	kmem_free(tosnap, ZFS_MAX_DATASET_NAME_LEN);
	sfs_rename_consume(fdvp, fvp, tdvp, tvp);
	return (error);
}

static int
sfs_readdir_one(struct vop_readdir_args *ap, struct dirent *dp,
    const char *name, ino_t ino, off_t *offp)
{
	int error;

	dp->d_fileno = ino;
	dp->d_type = DT_DIR;
	strlcpy(dp->d_name, name, sizeof (dp->d_name));
	dp->d_namlen = strlen(dp->d_name);
	dp->d_reclen = _DIRENT_SIZE(dp);
	/*
	 * The same dirent is reused.  strlcpy stops at the NUL, so
	 * a shorter name would copy out the previous one's tail.
	 */
	{
		size_t used = offsetof(struct dirent, d_name) +
		    (size_t)dp->d_namlen + 1;

		if ((size_t)dp->d_reclen > used)
			memset((char *)dp + used, 0,
			    (size_t)dp->d_reclen - used);
	}

	if (ap->a_uio->uio_resid < dp->d_reclen)
		return (ENAMETOOLONG);
	if (ap->a_uio->uio_offset > *offp) {
		*offp += dp->d_reclen;
		return (0);
	}

	error = uiomove(dp, dp->d_reclen, ap->a_uio);
	if (error)
		return (error);
	*offp += dp->d_reclen;
	return (0);
}

static int
sfs_readdir(void *v)
{
	struct vop_readdir_args *ap = v;
	vnode_t *vp = ap->a_vp;
	struct sfs_node *node = VTOSFS(vp);
	zfsvfs_t *zfsvfs = vp->v_mount->mnt_data;
	struct dirent *dp;
	uint64_t parent;
	off_t offset;
	int error;

	SFS_NODE_ASSERT(vp);
	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	bool ateof = B_FALSE;

	parent = node->sn_parent_id == 0 ? zfsvfs->z_root : node->sn_parent_id;
	dp = kmem_zalloc(sizeof (*dp), KM_SLEEP);

	offset = 0;
	error = sfs_readdir_one(ap, dp, ".", node->sn_id, &offset);
	if (error == 0)
		error = sfs_readdir_one(ap, dp, "..", parent, &offset);
	if (error == 0 && node->sn_id == ZFSCTL_INO_ROOT) {
		error = sfs_readdir_one(ap, dp, ZFS_SNAPDIR_NAME,
		    ZFSCTL_INO_SNAPDIR, &offset);
		if (error == 0 && zfsvfs->z_shares_dir != 0)
			error = sfs_readdir_one(ap, dp, ZFS_SHARESDIR_NAME,
			    zfsvfs->z_shares_dir, &offset);
		if (error == 0)
			ateof = B_TRUE;
	} else if (error == 0 && node->sn_id == ZFSCTL_INO_SNAPDIR) {
		char snapname[ZFS_MAX_DATASET_NAME_LEN];
		uint64_t cookie, id;

		cookie = 0;
		for (;;) {
			dsl_pool_config_enter(dmu_objset_pool(zfsvfs->z_os),
			    FTAG);
			error = dmu_snapshot_list_next(zfsvfs->z_os,
			    sizeof (snapname), snapname, &id, &cookie, NULL);
			dsl_pool_config_exit(dmu_objset_pool(zfsvfs->z_os),
			    FTAG);
			if (error) {
				if (error == ENOENT) {
					error = 0;
					ateof = B_TRUE;
				}
				break;
			}
			error = sfs_readdir_one(ap, dp, snapname, id, &offset);
			if (error)
				break;
		}
	} else if (error == 0) {
		ateof = B_TRUE;
	}

	if (ap->a_eofflag != NULL && error == 0 && ateof)
		*ap->a_eofflag = 1;
	if (error == ENAMETOOLONG)
		error = 0;

	kmem_free(dp, sizeof (*dp));
	zfs_exit(zfsvfs, FTAG);
	return (error);
}

static int
sfs_inactive(void *v)
{
	struct vop_inactive_v2_args *ap = v;
	struct sfs_node *node = VTOSFS(ap->a_vp);

	*ap->a_recycle = (node->sn_parent_id == ZFSCTL_INO_SNAPDIR);
	return (0);
}

static int
sfs_reclaim(void *v)
{
	struct vop_reclaim_v2_args *ap = v;
	vnode_t *vp = ap->a_vp;
	struct sfs_node *node = VTOSFS(vp);

	/*
	 * VOP_RECLAIM v2 is entered locked and must unlock.
	 * vcache_reclaim will not unlock for us.  Same leak as
	 * the pre-fix zfs_netbsd_reclaim: fstrans hold from VOP_LOCK
	 * stays SHARED, process exit leaves trans_cnt>0, the next
	 * dounmount waits forever on fstcnt.  Snapshot placeholders
	 * recycle on every inactive, so this is the ordinary path.
	 */
	VOP_UNLOCK(vp);
	vp->v_data = NULL;
	cv_destroy(&node->sn_cv);
	kmem_free(node, sizeof (*node));
	return (0);
}

static int
sfs_print(void *v)
{
	struct vop_print_args *ap = v;
	struct sfs_node *node = VTOSFS(ap->a_vp);

	printf("\tid %llu, parent %llu\n",
	    (unsigned long long)node->sn_id,
	    (unsigned long long)node->sn_parent_id);
	return (0);
}

static int
sfs_getpages(void *v)
{
	struct vop_getpages_args *ap = v;

	if ((ap->a_flags & PGO_LOCKED) == 0)
		rw_exit(ap->a_vp->v_uobj.vmobjlock);
	return (EOPNOTSUPP);
}

const struct vnodeopv_entry_desc zfs_sfsop_entries[] = {
	{ &vop_default_desc, vn_default_error },
	{ &vop_parsepath_desc, genfs_parsepath },
	{ &vop_lookup_desc, sfs_lookup },
	{ &vop_open_desc, sfs_open },
	{ &vop_close_desc, sfs_close },
	{ &vop_access_desc, sfs_access },
	{ &vop_getattr_desc, sfs_getattr },
	{ &vop_mkdir_desc, sfs_mkdir },
	{ &vop_rmdir_desc, sfs_rmdir },
	{ &vop_rename_desc, sfs_rename },
	{ &vop_lock_desc, genfs_lock },
	{ &vop_unlock_desc, genfs_unlock },
	{ &vop_readdir_desc, sfs_readdir },
	{ &vop_inactive_desc, sfs_inactive },
	{ &vop_reclaim_desc, sfs_reclaim },
	{ &vop_seek_desc, genfs_seek },
	{ &vop_getpages_desc, sfs_getpages },
	{ &vop_putpages_desc, genfs_null_putpages },
	{ &vop_islocked_desc, genfs_islocked },
	{ &vop_print_desc, sfs_print },
	{ &vop_pathconf_desc, genfs_pathconf },
	{ NULL, NULL }
};

const struct vnodeopv_desc zfs_sfsop_opv_desc =
	{ &zfs_sfsop_p, zfs_sfsop_entries };

/* Same vector under the name vfsops already references. */
const struct vnodeopv_desc zfsctl_vnodeop_opv_desc =
	{ &zfsctl_vnodeop_p, zfs_sfsop_entries };

void
zfsctl_init(void)
{
}

void
zfsctl_fini(void)
{
}

int
zfsctl_loadvnode(vfs_t *vfsp, vnode_t *vp,
    const void *key, size_t key_len, const void **new_key)
{
	struct sfs_node_key node_key;
	struct sfs_node *node;

	if (key_len != sizeof (node_key))
		return (EINVAL);
	if ((vfsp->mnt_iflag & IMNT_UNMOUNT) != 0)
		return (ENOENT);

	memcpy(&node_key, key, key_len);
	node = kmem_zalloc(sizeof (*node), KM_SLEEP);
	node->sn_mounting = NULL;
	node->sn_key = node_key;
	cv_init(&node->sn_cv, "zfsctlsnap", CV_DEFAULT, NULL);

	vp->v_data = node;
	vp->v_op = zfs_sfsop_p;
	vp->v_tag = VT_ZFS;
	vp->v_type = VDIR;
	vp->v_vflag |= VV_MPSAFE;
	/* No pager -- synthetic directory. */
	vp->v_size = 0;
	vp->v_writesize = 0;

	*new_key = &node->sn_key;
	return (0);
}

int
zfsctl_root(zfsvfs_t *zfsvfs, int flags, vnode_t **vpp)
{
	int error;

	error = zfsctl_root_unlocked(zfsvfs, vpp);
	if (error == 0 && (flags & LK_TYPE_MASK) != 0) {
		error = vn_lock(*vpp, flags);
		if (error != 0) {
			vrele(*vpp);
			*vpp = NULL;
		}
	}
	return (error);
}

void
zfsctl_create(zfsvfs_t *zfsvfs)
{
	vnode_t *vp;
	struct zfsctl_root *zc;
	uint64_t crtime[2];

	zc = kmem_zalloc(sizeof (*zc), KM_SLEEP);
	memset(crtime, 0, sizeof (crtime));
	if (VFS_ROOT(zfsvfs->z_vfs, LK_EXCLUSIVE, &vp) == 0) {
		if (VTOZ(vp)->z_sa_hdl != NULL)
			(void) sa_lookup(VTOZ(vp)->z_sa_hdl,
			    SA_ZPL_CRTIME(zfsvfs), &crtime, sizeof (crtime));
		vput(vp);
	}
	ZFS_TIME_DECODE(&zc->zc_cmtime, crtime);
	zfsvfs->z_ctldir = zc;
}

void
zfsctl_destroy(zfsvfs_t *zfsvfs)
{
	struct zfsctl_root *zc = zfsvfs->z_ctldir;

	zfsvfs->z_ctldir = NULL;
	if (zc != NULL)
		kmem_free(zc, sizeof (*zc));
}

int
zfsctl_lookup_objset(vfs_t *vfsp, uint64_t objsetid, zfsvfs_t **zfsvfsp)
{
	struct sfs_node_key key = {
		.parent_id = ZFSCTL_INO_SNAPDIR,
		.id = objsetid
	};
	vnode_t *vp;
	struct mount *mp;
	zfsvfs_t *z;
	int error;

	*zfsvfsp = NULL;
	error = vcache_get(vfsp, &key, sizeof (key), &vp);
	if (error != 0)
		return (SET_ERROR(EINVAL));

	mutex_enter(vp->v_interlock);
	mp = vp->v_mountedhere;
	if (mp != NULL)
		vfs_ref(mp);
	mutex_exit(vp->v_interlock);
	vrele(vp);

	/*
	 * vfs_trybusy is fstrans_start_nowait plus a ref.  Do not
	 * call it under v_interlock (ASSERT_SLEEPABLE).  It does
	 * not keep the zfsvfs: suspendctl does not drain fstrans.
	 * Re-check the objset user pointer and pin z_nb_holds
	 * before dropping that lock.  EBUSY means the snapshot
	 * mount is already suspending.
	 */
	if (mp == NULL)
		return (SET_ERROR(EINVAL));
	error = vfs_trybusy(mp);
	vfs_rele(mp);
	if (error != 0)
		return (SET_ERROR(EINVAL));
	/*
	 * dounmount holds mnt_updating across VFS_UNMOUNT, which
	 * may free the zfsvfs.  Take it before reading mnt_data.
	 * Same order as unmount: mnt_updating, then the user
	 * pointer lock.  Pin before dropping either.
	 */
	mutex_enter(mp->mnt_updating);
	z = mp->mnt_data;
	if (z == NULL || z->z_vfs != mp || z->z_os == NULL) {
		mutex_exit(mp->mnt_updating);
		vfs_unbusy(mp);
		return (SET_ERROR(EINVAL));
	}
	mutex_enter(&z->z_os->os_user_ptr_lock);
	if (dmu_objset_get_user(z->z_os) != z || z->z_vfs != mp) {
		mutex_exit(&z->z_os->os_user_ptr_lock);
		mutex_exit(mp->mnt_updating);
		vfs_unbusy(mp);
		return (SET_ERROR(EINVAL));
	}
	atomic_inc_32(&z->z_nb_holds);
	mutex_exit(&z->z_os->os_user_ptr_lock);
	mutex_exit(mp->mnt_updating);
	*zfsvfsp = z;
	return (0);
}

/*
 * Parent VFS_UNMOUNT already holds vfs_suspend_lock.  That lock is
 * global, not per-mount, so a nested dounmount of a snapshot blocks
 * on the lock this thread owns ("locking against myself").
 * fstrans_setstate to pretend the snapshot is already suspended was
 * the workaround.  It is gone.  A mounted snapshot makes the parent
 * unmount EBUSY.  The kernel needs a per-mount suspend lock before
 * this can unmount the snapshot from inside the parent unmount.
 */
int
zfsctl_umount_snapshots(vfs_t *vfsp, int fflags, cred_t *cr)
{
	char snapname[ZFS_MAX_DATASET_NAME_LEN];
	zfsvfs_t *zfsvfs = vfsp->vfs_data;
	struct mount *mp;
	vnode_t *vp;
	struct sfs_node_key key;
	uint64_t cookie;
	int error;

	(void) cr;
	(void) fflags;
	if (zfsvfs->z_ctldir == NULL)
		return (0);

	cookie = 0;
	key.parent_id = ZFSCTL_INO_SNAPDIR;
	for (;;) {
		dsl_pool_config_enter(dmu_objset_pool(zfsvfs->z_os), FTAG);
		error = dmu_snapshot_list_next(zfsvfs->z_os, sizeof (snapname),
		    snapname, &key.id, &cookie, NULL);
		dsl_pool_config_exit(dmu_objset_pool(zfsvfs->z_os), FTAG);
		if (error) {
			if (error == ENOENT)
				error = 0;
			break;
		}

		error = vcache_get(zfsvfs->z_vfs, &key, sizeof (key), &vp);
		if (error == ENOENT)
			continue;
		else if (error)
			break;

		mutex_enter(vp->v_interlock);
		mp = vp->v_mountedhere;
		mutex_exit(vp->v_interlock);
		vrele(vp);
		if (mp != NULL) {
			error = SET_ERROR(EBUSY);
			break;
		}
	}
	return (error);
}

boolean_t
zfsctl_is_node(vnode_t *vp)
{

	return (vp != NULL &&
	    (vp->v_op == zfs_sfsop_p || vp->v_op == zfsctl_vnodeop_p));
}

int
zfsctl_vptofh(vnode_t *vp, fid_t *fidp, size_t *fh_size)
{
	struct sfs_node *node;
	zfid_short_t *zfid;
	uint64_t object;
	int i;

	if (vp == NULL || vp->v_data == NULL)
		return (SET_ERROR(ESTALE));
	if (*fh_size < SHORT_FID_LEN) {
		*fh_size = SHORT_FID_LEN;
		return (SET_ERROR(E2BIG));
	}
	*fh_size = SHORT_FID_LEN;
	node = VTOSFS(vp);
	object = node->sn_id;
	zfid = (zfid_short_t *)fidp;
	zfid->zf_len = SHORT_FID_LEN;
	for (i = 0; i < (int)sizeof (zfid->zf_object); i++)
		zfid->zf_object[i] = (uint8_t)(object >> (8 * i));
	/*
	 * Generation 0 is the .zfs / .zfs/snapshot sentinel
	 * (same contract as zfs_fid / native osnet).
	 */
	for (i = 0; i < (int)sizeof (zfid->zf_gen); i++)
		zfid->zf_gen[i] = 0;
	return (0);
}

int
zfsctl_snapshot_unmount(const char *snapname)
{
	zfsvfs_t *zfsvfs = NULL;
	vfs_t *vfsp;
	int error;

	if (snapname == NULL || strchr(snapname, '@') == NULL)
		return (0);
	error = getzfsvfs(snapname, &zfsvfs);
	if (error != 0 || zfsvfs == NULL)
		return (0);
	vfsp = zfsvfs->z_vfs;
	if (vfsp == NULL) {
		zfs_vfs_rele(zfsvfs);
		return (0);
	}
	/*
	 * getzfsvfs pinned z_nb_holds and took vfs_busy.  Drop
	 * both before dounmount.  This thread does not rele
	 * again, so a leftover pin would leak the zfsvfs.
	 * A plain vfs_ref keeps the mount across dounmount.
	 * vfs_busy does not delay it: suspendctl does not drain.
	 */
	vfs_ref(vfsp);
	zfs_vfs_rele(zfsvfs);
	error = dounmount(vfsp, MNT_FORCE, curlwp);
	vfs_rele(vfsp);
	return (error);
}

int
zfsctl_rename_snapshot(const char *from, const char *to)
{

	(void) to;
	return (zfsctl_snapshot_unmount(from));
}

int
zfsctl_destroy_snapshot(const char *snapname, int force)
{

	(void) force;
	return (zfsctl_snapshot_unmount(snapname));
}
