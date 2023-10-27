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
spl_priv_check_cred(kauth_cred_t cred, int priv, __unused int flags)
{
	int error;

	if (kauth_cred_getuid(cred) == 0) {
		error = 0;
		goto out;
	}

	/*
	 * The default is deny, so if no policies have granted it, reject
	 * with a privilege error here.
	 */
	error = EPERM;
out:
	return (error);
}

int
secpolicy_fs_unmount(cred_t *cr, struct mount *vfsp)
{
	return (spl_priv_check_cred((kauth_cred_t)cr, PRIV_VFS_UNMOUNT, 0));
}

int
secpolicy_nfs(const cred_t *cr)
{
	return (spl_priv_check_cred((kauth_cred_t)cr, PRIV_NFS_DAEMON, 0));
}

int
secpolicy_sys_config(const cred_t *cr, boolean_t checkonly)
{
	return (spl_priv_check_cred((kauth_cred_t)cr, PRIV_ZFS_POOL_CONFIG, 0));
}

int
secpolicy_zfs(const cred_t *cr)
{
	return (spl_priv_check_cred((kauth_cred_t)cr, PRIV_VFS_MOUNT, 0));
}

int
secpolicy_zinject(const cred_t *cr)
{
	return (spl_priv_check_cred((kauth_cred_t)cr, PRIV_ZFS_INJECT, 0));
}

int
secpolicy_vnode_any_access(const cred_t *cr, struct vnode *vp, uid_t owner)
{
	// FIXME
	return (0);
}

int
secpolicy_vnode_access2(const cred_t *cr, struct vnode *vp, uid_t owner,
    mode_t curmode, mode_t wantmode)
{
	// FIXME
	return (0);
}

int
secpolicy_vnode_setattr(cred_t *cr, struct vnode *vp, vattr_t *vap,
    const vattr_t *ovap, int flags,
    int unlocked_access(void *, int, cred_t *),
    void *node)
{
	// FIXME
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
	// FIXME
	return (0);
}

int
secpolicy_vnode_remove(struct vnode *vp, const cred_t *cr)
{
	return (0);
}

int
secpolicy_vnode_create_gid(const cred_t *cred)
{
	return (0);
}

int
secpolicy_vnode_setids_setgids(struct vnode *vp, const cred_t *cr,
    gid_t gid)
{
	return (0);
}

int
secpolicy_vnode_setdac(struct vnode *vp, const cred_t *cr, uid_t u)
{
	return (0);
}

int
secpolicy_vnode_chown(struct vnode *vp, const cred_t *cr, uid_t u)
{
	return (0);
}

int
secpolicy_vnode_setid_retain(struct znode *zp, const cred_t *cr,
    boolean_t issuidroot)
{
	return (0);
}

int
secpolicy_xvattr(vattr_t *vap, uid_t uid, const cred_t *cr, mode_t mod)
{
	return (0);
}

int
secpolicy_setid_clear(vattr_t *vap, const cred_t *cr)
{
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
	return (spl_priv_check_cred((kauth_cred_t)cr, PRIV_VFS_MOUNT, 0));
}

int
secpolicy_zfs_proc(const cred_t *cr, proc_t *proc)
{
	return (spl_priv_check_cred((kauth_cred_t)cr, PRIV_VFS_MOUNT, 0));
}
