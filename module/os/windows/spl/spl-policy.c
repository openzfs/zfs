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
#include <sys/cred.h>
#include <sys/policy.h>
#include <sys/priv.h>

int
spl_priv_check_cred(const cred_t *cred, int priv, int flags)
{
	(void) cred; (void) priv; (void) flags;
	return (spl_get_caller_uid() == 0 ? 0 : EPERM);
}

int
secpolicy_fs_unmount(cred_t *cr, struct mount *vfsp)
{
	return (spl_priv_check_cred(cr, PRIV_VFS_UNMOUNT, 0));
}

int
secpolicy_nfs(const cred_t *cr)
{
	return (spl_priv_check_cred(cr, PRIV_NFS_DAEMON, 0));
}

int
secpolicy_sys_config(const cred_t *cr, boolean_t checkonly)
{
	return (spl_priv_check_cred(cr, PRIV_ZFS_POOL_CONFIG, 0));
}

int
secpolicy_zfs(const cred_t *cr)
{
	return (spl_priv_check_cred(cr, PRIV_VFS_MOUNT, 0));
}

int
secpolicy_zinject(const cred_t *cr)
{
	return (spl_priv_check_cred(cr, PRIV_ZFS_INJECT, 0));
}

int
secpolicy_vnode_any_access(const cred_t *cr, struct vnode *vp, uid_t owner)
{
	uid_t uid = crgetuid(cr);
	if (uid == owner || uid == 0)
		return (0);
	return (EPERM);
}

/*
 * Like secpolicy_vnode_access() but takes the currently-granted mode bits
 * (curmode) and the desired mode bits (wantmode).  Returns 0 if all wanted
 * bits are already granted, or if the caller has admin privilege to override
 * the missing bits.
 */
int
secpolicy_vnode_access2(const cred_t *cr, struct vnode *vp, uid_t owner,
    mode_t curmode, mode_t wantmode)
{
	mode_t mode = ~curmode & wantmode;

	if (mode == 0)
		return (0);

	return (crgetuid(cr) == 0 ? 0 : EACCES);
}

int
secpolicy_vnode_setattr(cred_t *cr, struct vnode *vp, vattr_t *vap,
    const vattr_t *ovap, int flags,
    int unlocked_access(void *, int, cred_t *),
    void *node)
{
	int mask = vap->va_mask;
	int error;

	if (mask & ATTR_SIZE) {
		if (vp->v_type == VDIR)
			return (EISDIR);
		error = unlocked_access(node, VWRITE, cr);
		if (error)
			return (error);
	}
	if (mask & ATTR_MODE) {
		error = secpolicy_vnode_setdac(vp, cr, ovap->va_uid);
		if (error)
			return (error);
		error = secpolicy_setid_setsticky_clear(vp, vap, ovap, cr);
		if (error)
			return (error);
	} else {
		vap->va_mode = ovap->va_mode;
	}
	if (mask & (ATTR_UID | ATTR_GID)) {
		error = secpolicy_vnode_setdac(vp, cr, ovap->va_uid);
		if (error)
			return (error);
		/*
		 * Changing uid/gid to something other than the caller's own
		 * requires admin privilege.
		 */
		if (((mask & ATTR_UID) && vap->va_uid != ovap->va_uid) ||
		    ((mask & ATTR_GID) && vap->va_gid != ovap->va_gid &&
		    !groupmember(vap->va_gid, cr))) {
			if (crgetuid(cr) != 0)
				return (EPERM);
		}
		if (((mask & ATTR_UID) && vap->va_uid != ovap->va_uid) ||
		    ((mask & ATTR_GID) && vap->va_gid != ovap->va_gid)) {
			secpolicy_setid_clear(vap, cr);
		}
	}
	if (mask & (ATTR_ATIME | ATTR_MTIME)) {
		error = secpolicy_vnode_setdac(vp, cr, ovap->va_uid);
		if (error)
			return (error);
	}
	return (0);
}

int
secpolicy_vnode_stky_modify(const cred_t *cred)
{
	return (EPERM);
}

int
secpolicy_setid_setsticky_clear(vnode_t *vp, vattr_t *vap, const vattr_t *ovap,
    cred_t *cr)
{
	uid_t uid = crgetuid(cr);

	/*
	 * setuid: only the file owner or admin may set it.
	 */
	if ((vap->va_mode & S_ISUID) != 0 &&
	    uid != ovap->va_uid && uid != 0)
		vap->va_mode &= ~S_ISUID;

	/*
	 * sticky bit on a non-directory: only admin.
	 */
	if (vp->v_type != VDIR && (vap->va_mode & S_ISVTX) != 0 && uid != 0)
		vap->va_mode &= ~S_ISVTX;

	/*
	 * setgid: only if the caller is a member of the target group or admin.
	 */
	if ((vap->va_mode & S_ISGID) != 0 && uid != 0 &&
	    !groupmember(vap->va_gid, cr))
		vap->va_mode &= ~S_ISGID;

	return (0);
}

int
secpolicy_vnode_remove(struct vnode *vp, const cred_t *cr)
{
	return (crgetuid(cr) == 0 ? 0 : EPERM);
}

int
secpolicy_vnode_create_gid(const cred_t *cred)
{
	return (crgetuid(cred) == 0 ? 0 : EPERM);
}

int
secpolicy_vnode_setids_setgids(struct vnode *vp, const cred_t *cr,
    gid_t gid)
{
	if (groupmember(gid, cr))
		return (0);
	return (crgetuid(cr) == 0 ? 0 : EPERM);
}

/*
 * Check whether the caller may change the DAC (mode bits / ownership) of
 * a vnode.  The file owner and admin (uid 0) are always allowed.
 */
int
secpolicy_vnode_setdac(struct vnode *vp, const cred_t *cr, uid_t owner)
{
	uid_t uid = crgetuid(cr);
	if (uid == owner || uid == 0)
		return (0);
	return (EPERM);
}

int
secpolicy_vnode_chown(struct vnode *vp, const cred_t *cr, uid_t owner)
{
	uid_t uid = crgetuid(cr);
	if (uid == owner || uid == 0)
		return (0);
	return (EPERM);
}

int
secpolicy_vnode_setid_retain(struct znode *zp, const cred_t *cr, int fal)
{
	return (0);
}

int
secpolicy_xvattr(vattr_t *vap, uid_t uid, const cred_t *cr, mode_t mod)
{
	return (0);
}

/*
 * POSIX requires clearing setuid/setgid when ownership changes and the
 * caller is not privileged.
 */
int
secpolicy_setid_clear(vattr_t *vap, const cred_t *cr)
{
	if (crgetuid(cr) != 0)
		vap->va_mode &= ~(S_ISUID | S_ISGID);
	return (0);
}

int
secpolicy_basic_link(const cred_t *cr)
{
	return (0);
}

int
secpolicy_fs_mount_clearopts(const cred_t *cr, struct mount *mp)
{
	return (0);
}

int
secpolicy_fs_mount(const cred_t *cr, struct vnode *vp, struct mount *mp)
{
	return (spl_priv_check_cred(cr, PRIV_VFS_MOUNT, 0));
}

int
secpolicy_zfs_proc(const cred_t *cr, proc_t *proc)
{
	return (spl_priv_check_cred(cr, PRIV_VFS_MOUNT, 0));
}
