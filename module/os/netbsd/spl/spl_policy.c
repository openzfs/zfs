// SPDX-License-Identifier: BSD-2-Clause
/*
 * secpolicy_* mapped onto NetBSD kauth(9).
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/kauth.h>
#include <sys/cred.h>
#include <sys/policy.h>
#include <sys/priv.h>
#include <sys/vfs.h>
#include <sys/mntent.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_znode.h>
#include <sys/xvattr.h>
#include <sys/zone.h>

int
secpolicy_zfs(cred_t *cr)
{
	/*
	 * PRIV_VFS_MOUNT is the NetBSD stand-in for illumos PRIV_SYS_MOUNT.
	 * Callers must still do narrower checks (zone, dataset visibility)
	 * alongside this.
	 */
	return (priv_check_cred(cr, PRIV_VFS_MOUNT));
}

int
secpolicy_sys_config(cred_t *cr, int checkonly)
{
	(void) checkonly;
	/* pools can only be manipulated from the global zone */
	if (crgetzoneid(cr) != GLOBAL_ZONEID)
		return (EACCES);
	return (priv_check_cred(cr, PRIV_ZFS_POOL_CONFIG));
}

int
secpolicy_zinject(cred_t *cr)
{
	/* zinject is only in the global zone */
	if (crgetzoneid(cr) != GLOBAL_ZONEID)
		return (EACCES);
	return (priv_check_cred(cr, PRIV_ZFS_INJECT));
}

int
secpolicy_fs_unmount(cred_t *cr, struct mount *vfsp)
{
	(void) vfsp;
	return (priv_check_cred(cr, PRIV_VFS_UNMOUNT));
}

int
secpolicy_fs_owner(struct mount *mp, cred_t *cr)
{
	zfsvfs_t *zfsvfs;

	if (!zfs_super_owner || mp == NULL)
		return (EPERM);
	zfsvfs = mp->vfs_data;
	if (zfsvfs == NULL)
		return (EPERM);
	if (kauth_cred_geteuid(cr) == zfsvfs->z_mount_uid)
		return (0);
	return (EPERM);
}

int
secpolicy_basic_link(vnode_t *vp, cred_t *cr)
{
	(void) vp;
	return (priv_check_cred(cr, PRIV_VFS_LINK));
}

int
secpolicy_vnode_stky_modify(cred_t *cr)
{
	return (priv_check_cred(cr, PRIV_VFS_SYSFLAGS));
}

int
secpolicy_vnode_remove(vnode_t *vp, cred_t *cr)
{
	(void) vp;
	return (priv_check_cred(cr, PRIV_VFS_ADMIN));
}

int
secpolicy_vnode_owner(vnode_t *vp, cred_t *cr, uid_t owner)
{
	(void) vp;
	if (kauth_cred_geteuid(cr) == owner)
		return (0);
	return (priv_check_cred(cr, PRIV_VFS_ADMIN));
}

int
secpolicy_vnode_chown(vnode_t *vp, cred_t *cr, uid_t owner)
{
	(void) vp;
	if (kauth_cred_geteuid(cr) == owner)
		return (0);
	return (priv_check_cred(cr, PRIV_VFS_CHOWN));
}

int
secpolicy_vnode_access(cred_t *cr, vnode_t *vp, uid_t owner, accmode_t accmode)
{
	/*
	 * Owner bits already live in USER_OBJ / ACE_OWNER.  Granting
	 * the owner every leftover mode here made chmod 007 + owner
	 * create succeed on ZFS (NFSv4 ACE_EVERYONE) while tmpfs
	 * correctly denied it.
	 */
	(void) owner;
	if (vp != NULL && secpolicy_fs_owner(vp->v_mount, cr) == 0)
		return (0);
	if (priv_check_cred(cr, PRIV_VFS_ADMIN) == 0)
		return (0);
	/*
	 * Every PRIV_* here collapses to KAUTH_GENERIC_ISSUSER, which
	 * denies with EPERM.  A denied ordinary access must be EACCES;
	 * EPERM is only for VADMIN requests.
	 */
	return ((accmode & VADMIN) ? EPERM : EACCES);
}

int
secpolicy_vnode_access2(cred_t *cr, vnode_t *vp, uid_t owner,
    accmode_t curmode, accmode_t wantmode)
{
	accmode_t remain = wantmode & ~curmode;

	if (remain == 0)
		return (0);
	return (secpolicy_vnode_access(cr, vp, owner, remain));
}

int
secpolicy_vnode_any_access(cred_t *cr, vnode_t *vp, uid_t owner)
{
	if (kauth_cred_geteuid(cr) == owner)
		return (0);
	if (secpolicy_fs_owner(vp->v_mount, cr) == 0)
		return (0);
	return (priv_check_cred(cr, PRIV_VFS_ADMIN));
}

int
secpolicy_vnode_setdac(vnode_t *vp, cred_t *cr, uid_t owner)
{
	if (kauth_cred_geteuid(cr) == owner)
		return (0);
	if (secpolicy_fs_owner(vp->v_mount, cr) == 0)
		return (0);
	return (priv_check_cred(cr, PRIV_VFS_ADMIN));
}

int
secpolicy_vnode_setattr(cred_t *cr, vnode_t *vp, struct vattr *vap,
    const struct vattr *ovap, int flags,
    int unlocked_access(void *, int, cred_t *), void *node)
{
	int mask = vap->va_mask;
	int error;

	(void) flags;
	if (mask & AT_SIZE) {
		if (vp->v_type == VDIR)
			return (EISDIR);
		if (unlocked_access != NULL) {
			error = unlocked_access(node, VWRITE, cr);
			if (error)
				return (error);
		}
	}
	if (mask & AT_MODE) {
		error = secpolicy_vnode_setdac(vp, cr, ovap->va_uid);
		if (error)
			return (error);
		error = secpolicy_setid_setsticky_clear(vp, vap, ovap, cr);
		if (error)
			return (error);
	} else {
		vap->va_mode = ovap->va_mode;
	}
	if (mask & (AT_UID | AT_GID)) {
		error = secpolicy_vnode_setdac(vp, cr, ovap->va_uid);
		if (error)
			return (error);
		/*
		 * Giving the file away, or chgrp to a group we are
		 * not in, is privileged.  NetBSD chown(2) is the
		 * same: owner cannot donate the file.
		 */
		if (((mask & AT_UID) && vap->va_uid != ovap->va_uid) ||
		    ((mask & AT_GID) && vap->va_gid != ovap->va_gid &&
		    !zfs_cr_groupmember(vap->va_gid, cr))) {
			if (secpolicy_fs_owner(vp->v_mount, cr) != 0) {
				error = priv_check_cred(cr, PRIV_VFS_CHOWN);
				if (error)
					return (error);
			}
		}
		if (((mask & AT_UID) && vap->va_uid != ovap->va_uid) ||
		    ((mask & AT_GID) && vap->va_gid != ovap->va_gid)) {
			secpolicy_setid_clear(vap, vp, cr);
		}
	}
	if (mask & (AT_ATIME | AT_MTIME)) {
		/*
		 * utimes(2): NULL times → owner or write; explicit
		 * times → owner or privilege.
		 */
		error = secpolicy_vnode_setdac(vp, cr, ovap->va_uid);
		if (error && (vap->va_vaflags & VA_UTIMES_NULL)) {
			if (unlocked_access != NULL)
				error = unlocked_access(node, VWRITE, cr);
		}
		if (error)
			return (error);
	}
	return (0);
}

int
secpolicy_vnode_create_gid(cred_t *cr)
{
	return (priv_check_cred(cr, PRIV_VFS_SETGID));
}

int
secpolicy_vnode_setids_setgids(vnode_t *vp, cred_t *cr, gid_t gid)
{
	(void) vp;
	if (zfs_cr_groupmember(gid, cr))
		return (0);
	return (priv_check_cred(cr, PRIV_VFS_SETGID));
}

int
secpolicy_vnode_setid_retain(struct znode *zp, cred_t *cr,
    boolean_t issuidroot)
{
	(void) zp;
	(void) issuidroot;
	return (priv_check_cred(cr, PRIV_VFS_SYSFLAGS));
}

void
secpolicy_setid_clear(struct vattr *vap, vnode_t *vp, cred_t *cr)
{
	if (secpolicy_fs_owner(vp->v_mount, cr) == 0)
		return;
	if ((vap->va_mode & (S_ISUID | S_ISGID)) == 0)
		return;
	/*
	 * Must set AT_MODE.  zfs_setattr only writes the mode
	 * when that bit is set; clearing va_mode alone leaves
	 * setid bits on a chown-to-root.
	 */
	if (priv_check_cred(cr, PRIV_VFS_RETAINSUGID) != 0) {
		vap->va_mask |= AT_MODE;
		vap->va_mode &= ~(S_ISUID | S_ISGID);
	}
}

int
secpolicy_setid_setsticky_clear(vnode_t *vp, struct vattr *vap,
    const struct vattr *ovap, cred_t *cr)
{
	(void) ovap;
	if ((vap->va_mode & S_ISVTX) && vp->v_type != VDIR)
		return (secpolicy_vnode_stky_modify(cr));
	return (0);
}

int
secpolicy_fs_mount(cred_t *cr, vnode_t *mvp, struct mount *vfsp)
{
	(void) mvp;
	(void) vfsp;
	return (priv_check_cred(cr, PRIV_VFS_MOUNT));
}

void
secpolicy_fs_mount_clearopts(cred_t *cr, struct mount *vfsp)
{
	/*
	 * Called after a delegated mount (ZFS_DELEG_PERM_MOUNT)
	 * succeeded where PRIV_VFS_MOUNT did not.  Non-root
	 * mounts must not honor setuid or device nodes.
	 * vfs_setmntopt maps those names onto MNT_NOSUID / MNT_NODEV
	 * so mount(8) agrees with the property callbacks.
	 */
	if (priv_check_cred(cr, PRIV_VFS_MOUNT) == 0)
		return;
	vfsp->vfs_flag |= VFS_NOSETUID | VFS_NODEVICES;
	vfs_clearmntopt(vfsp, MNTOPT_SETUID);
	vfs_setmntopt(vfsp, MNTOPT_NOSETUID, NULL, 0);
	vfs_clearmntopt(vfsp, MNTOPT_DEVICES);
	vfs_setmntopt(vfsp, MNTOPT_NODEVICES, NULL, 0);
}

int
secpolicy_xvattr(vnode_t *vp, xvattr_t *xvap, uid_t owner, cred_t *cr,
    vtype_t vtype)
{
	(void) xvap;
	(void) vtype;
	return (secpolicy_vnode_setdac(vp, cr, owner));
}

int
secpolicy_smb(cred_t *cr)
{
	return (priv_check_cred(cr, PRIV_VFS_ADMIN));
}
