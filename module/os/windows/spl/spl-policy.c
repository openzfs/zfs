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
spl_priv_check_cred(const void *cred, int priv, /*__unused*/ int flags)
{
	int error = 0;
	(void)flags;
//	if (kauth_cred_getuid(cred) == 0) {
//		error = 0;
//		goto out;
//	}

	/*
	 * The default is deny, so if no policies have granted it, reject
	 * with a privilege error here.
	 */
	// Assuming everything is root for now, fix me. WIN32
	 //error = EPERM;
//out:
	return (error);
}

//secpolicy_fs_unmount
#ifdef illumos
/*
 * Does the policy computations for "ownership" of a mount;
 * here ownership is defined as the ability to "mount"
 * the filesystem originally.  The rootvfs doesn't cover any
 * vnodes; we attribute its ownership to the rootvp.
 */
static int
secpolicy_fs_owner(cred_t *cr, const struct vfs *vfsp)
{
	vnode_t *mvp;

	if (vfsp == NULL)
		mvp = NULL;
	else if (vfsp == rootvfs)
		mvp = rootvp;
	else
		mvp = vfsp->vfs_vnodecovered;

	return (secpolicy_fs_common(cr, mvp, vfsp, NULL));
}

int
secpolicy_fs_unmount(cred_t *cr, struct vfs *vfsp)
{
	return (secpolicy_fs_owner(cr, vfsp));
}
#elif defined(__FreeBSD__)
int
secpolicy_fs_unmount(cred_t *cr, struct mount *vfsp __unused)
{

	return (priv_check_cred(cr, PRIV_VFS_UNMOUNT, 0));
}
#elif defined(__APPLE__)
int
secpolicy_fs_unmount(cred_t *cr, struct mount *vfsp)
{
	return (spl_priv_check_cred((kauth_cred_t)cr, PRIV_VFS_UNMOUNT, 0));
}
#endif	/* illumos */

//secpolicy_nfs
#ifdef illumos
/*
 * Checks for operations that are either client-only or are used by
 * both clients and servers.
 */
int
secpolicy_nfs(const cred_t *cr)
{
	return (PRIV_POLICY(cr, PRIV_SYS_NFS, B_FALSE, EPERM, NULL));
}
#elif defined(__FreeBSD__)
int
secpolicy_nfs(cred_t *cr)
{

	return (priv_check_cred(cr, PRIV_NFS_DAEMON, 0));
}
#elif defined(__APPLE__)
int
secpolicy_nfs(const cred_t *cr)
{
	return (spl_priv_check_cred((kauth_cred_t)cr, PRIV_NFS_DAEMON, 0));
}
#endif	/* illumos */

//secpolicy_sys_config
#ifdef illumos
/*
 * Catch all system configuration.
 */
int
secpolicy_sys_config(const cred_t *cr, boolean_t checkonly)
{
	if (checkonly) {
		return (PRIV_POLICY_ONLY(cr, PRIV_SYS_CONFIG, B_FALSE) ? 0 :
		    EPERM);
	} else {
		return (PRIV_POLICY(cr, PRIV_SYS_CONFIG, B_FALSE, EPERM, NULL));
	}
}
#elif defined(__FreeBSD__)
int
secpolicy_sys_config(cred_t *cr, int checkonly __unused)
{

	return (priv_check_cred(cr, PRIV_ZFS_POOL_CONFIG, 0));
}
#elif defined(__APPLE__)
int
secpolicy_sys_config(const cred_t *cr, boolean_t checkonly)
{
        return (spl_priv_check_cred((kauth_cred_t)cr, PRIV_ZFS_POOL_CONFIG, 0));
}
#elif defined(_WIN32)
int
secpolicy_sys_config(const cred_t *cr, boolean_t checkonly)
{
	return (spl_priv_check_cred((void *)cr, PRIV_ZFS_POOL_CONFIG, 0));
}
#endif	/* illumos */

//secpolicy_zfs
#ifdef illumos
/*
 * secpolicy_zfs
 *
 * Determine if the subject has permission to manipulate ZFS datasets
 * (not pools).  Equivalent to the SYS_MOUNT privilege.
 */
int
secpolicy_zfs(const cred_t *cr)
{
	return (PRIV_POLICY(cr, PRIV_SYS_MOUNT, B_FALSE, EPERM, NULL));
}
#elif defined(__FreeBSD__)
int
secpolicy_zfs(cred_t *cr)
{

	return (priv_check_cred(cr, PRIV_VFS_MOUNT, 0));
}
#elif defined(_WIN32)
int
secpolicy_zfs(const cred_t *cr)
{
	return (spl_priv_check_cred((kauth_cred_t *)cr, PRIV_VFS_MOUNT, 0));
}
#endif	/* illumos */

//secpolicy_zinject
#ifdef illumos
/*
 * secpolicy_zinject
 *
 * Determine if the subject can inject faults in the ZFS fault injection
 * framework.  Requires all privileges.
 */
int
secpolicy_zinject(const cred_t *cr)
{
	return (secpolicy_require_set(cr, PRIV_FULLSET, NULL, KLPDARG_NONE));
}
#elif defined(__FreeBSD__)
int
secpolicy_zinject(cred_t *cr)
{

	return (priv_check_cred(cr, PRIV_ZFS_INJECT, 0));
}
#elif defined(_WIN32)
int
secpolicy_zinject(const cred_t *cr)
{
	return (spl_priv_check_cred((kauth_cred_t *)cr, PRIV_ZFS_INJECT, 0));
}
#endif	/* illumos */

//secpolicy_vnode_any_access
#ifdef illumos
/*
 * This is a special routine for ZFS; it is used to determine whether
 * any of the privileges in effect allow any form of access to the
 * file.  There's no reason to audit this or any reason to record
 * this.  More work is needed to do the "KPLD" stuff.
 */
int
secpolicy_vnode_any_access(const cred_t *cr, vnode_t *vp, uid_t owner)
{
	static int privs[] = {
	    PRIV_FILE_OWNER,
	    PRIV_FILE_CHOWN,
	    PRIV_FILE_DAC_READ,
	    PRIV_FILE_DAC_WRITE,
	    PRIV_FILE_DAC_EXECUTE,
	    PRIV_FILE_DAC_SEARCH,
	};
	int i;

	/* Same as secpolicy_vnode_setdac */
	if (owner == cr->cr_uid)
		return (0);

	for (i = 0; i < sizeof (privs)/sizeof (int); i++) {
		boolean_t allzone = B_FALSE;
		int priv;

		switch (priv = privs[i]) {
		case PRIV_FILE_DAC_EXECUTE:
			if (vp->v_type == VDIR)
				continue;
			break;
		case PRIV_FILE_DAC_SEARCH:
			if (vp->v_type != VDIR)
				continue;
			break;
		case PRIV_FILE_DAC_WRITE:
		case PRIV_FILE_OWNER:
		case PRIV_FILE_CHOWN:
			/* We know here that if owner == 0, that cr_uid != 0 */
			allzone = owner == 0;
			break;
		}
		if (PRIV_POLICY_CHOICE(cr, priv, allzone))
			return (0);
	}
	return (EPERM);
}
#elif defined(__FreeBSD__)
int
secpolicy_vnode_any_access(cred_t *cr, vnode_t *vp, uid_t owner)
{
	static int privs[] = {
	    PRIV_VFS_ADMIN,
	    PRIV_VFS_READ,
	    PRIV_VFS_WRITE,
	    PRIV_VFS_EXEC,
	    PRIV_VFS_LOOKUP
	};
	int i;

	if (secpolicy_fs_owner(vp->v_mount, cr) == 0)
		return (0);

	/* Same as secpolicy_vnode_setdac */
	if (owner == cr->cr_uid)
		return (0);

	for (i = 0; i < sizeof (privs)/sizeof (int); i++) {
		boolean_t allzone = B_FALSE;
		int priv;

		switch (priv = privs[i]) {
		case PRIV_VFS_EXEC:
			if (vp->v_type == VDIR)
				continue;
			break;
		case PRIV_VFS_LOOKUP:
			if (vp->v_type != VDIR)
				continue;
			break;
		}
		if (priv_check_cred(cr, priv, 0) == 0)
			return (0);
	}
	return (EPERM);
}
#elif defined(_WIN32)
int
secpolicy_vnode_any_access(const cred_t *cr, vnode_t *vp, uid_t owner)
{
	// FIXME
	return (0);
}
#endif	/* illumos */

//secpolicy_vnode_access2
#ifdef illumos
/*
 * Like secpolicy_vnode_access() but we get the actual wanted mode and the
 * current mode of the file, not the missing bits.
 */
int
secpolicy_vnode_access2(const cred_t *cr, vnode_t *vp, uid_t owner,
    mode_t curmode, mode_t wantmode)
{
	mode_t mode;

	/* Inline the basic privileges tests. */
	if ((wantmode & VREAD) &&
	    !PRIV_ISASSERT(&CR_OEPRIV(cr), PRIV_FILE_READ) &&
	    priv_policy_va(cr, PRIV_FILE_READ, B_FALSE, EACCES, NULL,
	    KLPDARG_VNODE, vp, (char *)NULL, KLPDARG_NOMORE) != 0) {
		return (EACCES);
	}

	if ((wantmode & VWRITE) &&
	    !PRIV_ISASSERT(&CR_OEPRIV(cr), PRIV_FILE_WRITE) &&
	    priv_policy_va(cr, PRIV_FILE_WRITE, B_FALSE, EACCES, NULL,
	    KLPDARG_VNODE, vp, (char *)NULL, KLPDARG_NOMORE) != 0) {
		return (EACCES);
	}

	mode = ~curmode & wantmode;

	if (mode == 0)
		return (0);

	if ((mode & VREAD) && priv_policy_va(cr, PRIV_FILE_DAC_READ, B_FALSE,
	    EACCES, NULL, KLPDARG_VNODE, vp, (char *)NULL,
	    KLPDARG_NOMORE) != 0) {
		return (EACCES);
	}

	if (mode & VWRITE) {
		boolean_t allzone;

		if (owner == 0 && cr->cr_uid != 0)
			allzone = B_TRUE;
		else
			allzone = B_FALSE;
		if (priv_policy_va(cr, PRIV_FILE_DAC_WRITE, allzone, EACCES,
		    NULL, KLPDARG_VNODE, vp, (char *)NULL,
		    KLPDARG_NOMORE) != 0) {
			return (EACCES);
		}
	}

	if (mode & VEXEC) {
		/*
		 * Directories use file_dac_search to override the execute bit.
		 */
		int p = vp->v_type == VDIR ? PRIV_FILE_DAC_SEARCH :
		    PRIV_FILE_DAC_EXECUTE;

		return (priv_policy_va(cr, p, B_FALSE, EACCES, NULL,
		    KLPDARG_VNODE, vp, (char *)NULL, KLPDARG_NOMORE));
	}
	return (0);
}
#elif defined(__FreeBSD__)
int
secpolicy_vnode_access(cred_t *cr, vnode_t *vp, uid_t owner, accmode_t accmode)
{

	if (secpolicy_fs_owner(vp->v_mount, cr) == 0)
		return (0);

	if ((accmode & VREAD) && priv_check_cred(cr, PRIV_VFS_READ, 0) != 0)
		return (EACCES);
	if ((accmode & VWRITE) &&
	    priv_check_cred(cr, PRIV_VFS_WRITE, 0) != 0) {
		return (EACCES);
	}
	if (accmode & VEXEC) {
		if (vp->v_type == VDIR) {
			if (priv_check_cred(cr, PRIV_VFS_LOOKUP, 0) != 0)
				return (EACCES);
		} else {
			if (priv_check_cred(cr, PRIV_VFS_EXEC, 0) != 0)
				return (EACCES);
		}
	}
	return (0);
}


/*
 * Like secpolicy_vnode_access() but we get the actual wanted mode and the
 * current mode of the file, not the missing bits.
 */
int
secpolicy_vnode_access2(cred_t *cr, vnode_t *vp, uid_t owner,
    accmode_t curmode, accmode_t wantmode)
{
	accmode_t mode;

	mode = ~curmode & wantmode;

	if (mode == 0)
		return (0);

	return (secpolicy_vnode_access(cr, vp, owner, mode));
}
#elif defined(_WIN32)
int
secpolicy_vnode_access2(const cred_t *cr, vnode_t *vp, uid_t owner,
    mode_t curmode, mode_t wantmode)
{
	// FIXME
	return (0);
}
#endif	/* illumos */

//secpolicy_vnode_setattr
#ifdef illumos
/*
 * This function checks the policy decisions surrounding the
 * vop setattr call.
 *
 * It should be called after sufficient locks have been established
 * on the underlying data structures.  No concurrent modifications
 * should be allowed.
 *
 * The caller must pass in unlocked version of its vaccess function
 * this is required because vop_access function should lock the
 * node for reading.  A three argument function should be defined
 * which accepts the following argument:
 * 	A pointer to the internal "node" type (inode *)
 *	vnode access bits (VREAD|VWRITE|VEXEC)
 *	a pointer to the credential
 *
 * This function makes the following policy decisions:
 *
 *		- change permissions
 *			- permission to change file mode if not owner
 *			- permission to add sticky bit to non-directory
 *			- permission to add set-gid bit
 *
 * The ovap argument should include AT_MODE|AT_UID|AT_GID.
 *
 * If the vap argument does not include AT_MODE, the mode will be copied from
 * ovap.  In certain situations set-uid/set-gid bits need to be removed;
 * this is done by marking vap->va_mask to include AT_MODE and va_mode
 * is updated to the newly computed mode.
 */

int
secpolicy_vnode_setattr(cred_t *cr, struct vnode *vp, struct vattr *vap,
	const struct vattr *ovap, int flags,
	int unlocked_access(void *, int, cred_t *),
	void *node)
{
	int mask = vap->va_mask;
	int error = 0;
	boolean_t skipaclchk = (flags & ATTR_NOACLCHECK) ? B_TRUE : B_FALSE;

	if (mask & AT_SIZE) {
		if (vp->v_type == VDIR) {
			error = EISDIR;
			goto out;
		}

		/*
		 * If ATTR_NOACLCHECK is set in the flags, then we don't
		 * perform the secondary unlocked_access() call since the
		 * ACL (if any) is being checked there.
		 */
		if (skipaclchk == B_FALSE) {
			error = unlocked_access(node, VWRITE, cr);
			if (error)
				goto out;
		}
	}
	if (mask & AT_MODE) {
		/*
		 * If not the owner of the file then check privilege
		 * for two things: the privilege to set the mode at all
		 * and, if we're setting setuid, we also need permissions
		 * to add the set-uid bit, if we're not the owner.
		 * In the specific case of creating a set-uid root
		 * file, we need even more permissions.
		 */
		if ((error = secpolicy_vnode_setdac(cr, ovap->va_uid)) != 0)
			goto out;

		if ((error = secpolicy_setid_setsticky_clear(vp, vap,
		    ovap, cr)) != 0)
			goto out;
	} else
		vap->va_mode = ovap->va_mode;

	if (mask & (AT_UID|AT_GID)) {
		boolean_t checkpriv = B_FALSE;

		/*
		 * Chowning files.
		 *
		 * If you are the file owner:
		 *	chown to other uid		FILE_CHOWN_SELF
		 *	chown to gid (non-member) 	FILE_CHOWN_SELF
		 *	chown to gid (member) 		<none>
		 *
		 * Instead of PRIV_FILE_CHOWN_SELF, FILE_CHOWN is also
		 * acceptable but the first one is reported when debugging.
		 *
		 * If you are not the file owner:
		 *	chown from root			PRIV_FILE_CHOWN + zone
		 *	chown from other to any		PRIV_FILE_CHOWN
		 *
		 */
		if (cr->cr_uid != ovap->va_uid) {
			checkpriv = B_TRUE;
		} else {
			if (((mask & AT_UID) && vap->va_uid != ovap->va_uid) ||
			    ((mask & AT_GID) && vap->va_gid != ovap->va_gid &&
			    !groupmember(vap->va_gid, cr))) {
				checkpriv = B_TRUE;
			}
		}
		/*
		 * If necessary, check privilege to see if update can be done.
		 */
		if (checkpriv &&
		    (error = secpolicy_vnode_chown(cr, ovap->va_uid)) != 0) {
			goto out;
		}

		/*
		 * If the file has either the set UID or set GID bits
		 * set and the caller can set the bits, then leave them.
		 */
		secpolicy_setid_clear(vap, cr);
	}
	if (mask & (AT_ATIME|AT_MTIME)) {
		/*
		 * If not the file owner and not otherwise privileged,
		 * always return an error when setting the
		 * time other than the current (ATTR_UTIME flag set).
		 * If setting the current time (ATTR_UTIME not set) then
		 * unlocked_access will check permissions according to policy.
		 */
		if (cr->cr_uid != ovap->va_uid) {
			if (flags & ATTR_UTIME)
				error = secpolicy_vnode_utime_modify(cr);
			else if (skipaclchk == B_FALSE) {
				error = unlocked_access(node, VWRITE, cr);
				if (error == EACCES &&
				    secpolicy_vnode_utime_modify(cr) == 0)
					error = 0;
			}
			if (error)
				goto out;
		}
	}

	/*
	 * Check for optional attributes here by checking the following:
	 */
	if (mask & AT_XVATTR)
		error = secpolicy_xvattr((xvattr_t *)vap, ovap->va_uid, cr,
		    vp->v_type);
out:
	return (error);
}
#elif defined(__FreeBSD__)
int
secpolicy_vnode_setattr(cred_t *cr, vnode_t *vp, struct vattr *vap,
    const struct vattr *ovap, int flags,
    int unlocked_access(void *, int, cred_t *), void *node)
{
	int mask = vap->va_mask;
	int error;

	if (mask & AT_SIZE) {
		if (vp->v_type == VDIR)
			return (EISDIR);
		error = unlocked_access(node, VWRITE, cr);
		if (error)
			return (error);
	}
	if (mask & AT_MODE) {
		/*
		 * If not the owner of the file then check privilege
		 * for two things: the privilege to set the mode at all
		 * and, if we're setting setuid, we also need permissions
		 * to add the set-uid bit, if we're not the owner.
		 * In the specific case of creating a set-uid root
		 * file, we need even more permissions.
		 */
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
		 * To change the owner of a file, or change the group of a file to a
		 * group of which we are not a member, the caller must have
		 * privilege.
		 */
		if (((mask & AT_UID) && vap->va_uid != ovap->va_uid) ||
		    ((mask & AT_GID) && vap->va_gid != ovap->va_gid &&
		     !groupmember(vap->va_gid, cr))) {
			if (secpolicy_fs_owner(vp->v_mount, cr) != 0) {
				error = priv_check_cred(cr, PRIV_VFS_CHOWN, 0);
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
		 * From utimes(2):
		 * If times is NULL, ... The caller must be the owner of
		 * the file, have permission to write the file, or be the
		 * super-user.
		 * If times is non-NULL, ... The caller must be the owner of
		 * the file or be the super-user.
		 */
		error = secpolicy_vnode_setdac(vp, cr, ovap->va_uid);
		if (error && (vap->va_vaflags & VA_UTIMES_NULL))
			error = unlocked_access(node, VWRITE, cr);
		if (error)
			return (error);
	}
	return (0);
}
#elif defined(__APPLE__)
int
secpolicy_vnode_setattr(cred_t *cr, struct vnode *vp, vattr_t *vap,
        const vattr_t *ovap, int flags,
        int unlocked_access(void *, int, cred_t *),
        void *node)
{
	// FIXME
	return (0);
}
#endif	/* illumos */

//secpolicy_vnode_stky_modify
#ifdef illumos
/*
 * Name:	secpolicy_vnode_stky_modify()
 *
 * Normal:	verify that subject can make a file a "sticky".
 *
 * Output:	EPERM - if access denied.
 */

int
secpolicy_vnode_stky_modify(const cred_t *cred)
{
	return (PRIV_POLICY(cred, PRIV_SYS_CONFIG, B_FALSE, EPERM,
	    "set file sticky"));
}
#elif defined(__FreeBSD__)
int
secpolicy_vnode_stky_modify(cred_t *cr)
{

	return (EPERM);
}
#elif defined(_WIN32)
int
secpolicy_vnode_stky_modify(const cred_t *cred)
{
	return (EPERM);
}
#endif	/* illumos */

//secpolicy_setid_setsticky_clear
#ifdef illumos
/*
 * Name:	secpolicy_vnode_setids_setgids()
 *
 * Normal:	verify that subject can set the file setgid flag.
 *
 * Output:	EPERM - if not privileged
 */

int
secpolicy_vnode_setids_setgids(const cred_t *cred, gid_t gid)
{
	if (!groupmember(gid, cred))
		return (PRIV_POLICY(cred, PRIV_FILE_SETID, B_FALSE, EPERM,
		    NULL));
	return (0);
}

int
secpolicy_setid_setsticky_clear(vnode_t *vp, vattr_t *vap, const vattr_t *ovap,
    cred_t *cr)
{
	int error;

	if ((vap->va_mode & S_ISUID) != 0 &&
	    (error = secpolicy_vnode_setid_modify(cr,
	    ovap->va_uid)) != 0) {
		return (error);
	}

	/*
	 * Check privilege if attempting to set the
	 * sticky bit on a non-directory.
	 */
	if (vp->v_type != VDIR && (vap->va_mode & S_ISVTX) != 0 &&
	    secpolicy_vnode_stky_modify(cr) != 0) {
		vap->va_mode &= ~S_ISVTX;
	}

	/*
	 * Check for privilege if attempting to set the
	 * group-id bit.
	 */
	if ((vap->va_mode & S_ISGID) != 0 &&
	    secpolicy_vnode_setids_setgids(cr, ovap->va_gid) != 0) {
		vap->va_mode &= ~S_ISGID;
	}

	return (0);
}
#elif defined(__FreeBSD__)
int
secpolicy_vnode_setids_setgids(vnode_t *vp, cred_t *cr, gid_t gid)
{

	if (groupmember(gid, cr))
		return (0);
	if (secpolicy_fs_owner(vp->v_mount, cr) == 0)
		return (0);
	return (priv_check_cred(cr, PRIV_VFS_SETGID, 0));
}

int
secpolicy_setid_setsticky_clear(vnode_t *vp, struct vattr *vap,
    const struct vattr *ovap, cred_t *cr)
{
        int error;

	if (secpolicy_fs_owner(vp->v_mount, cr) == 0)
		return (0);

	/*
	 * Privileged processes may set the sticky bit on non-directories,
	 * as well as set the setgid bit on a file with a group that the process
	 * is not a member of. Both of these are allowed in jail(8).
	 */
	if (vp->v_type != VDIR && (vap->va_mode & S_ISTXT)) {
		if (priv_check_cred(cr, PRIV_VFS_STICKYFILE, 0))
			return (EFTYPE);
	}
	/*
	 * Check for privilege if attempting to set the
	 * group-id bit.
	 */
	if ((vap->va_mode & S_ISGID) != 0) {
		error = secpolicy_vnode_setids_setgids(vp, cr, ovap->va_gid);
		if (error)
			return (error);
	}
	/*
	 * Deny setting setuid if we are not the file owner.
	 */
	if ((vap->va_mode & S_ISUID) && ovap->va_uid != cr->cr_uid) {
		error = priv_check_cred(cr, PRIV_VFS_ADMIN, 0);
		if (error)
			return (error);
	}
	return (0);
}
#elif defined(__APPLE__)
int
secpolicy_setid_setsticky_clear(vnode_t *vp, vattr_t *vap, const vattr_t *ovap,
    cred_t *cr)
{
	// FIXME
	return (0);
}
#endif	/* illumos */

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

int secpolicy_vnode_setids_setgids(struct vnode *vp, const cred_t *cr,
                                          gid_t gid)
{
	return (0);
}

int secpolicy_vnode_setdac(struct vnode *vp, const cred_t *cr, uid_t u)
{
	return (0);
}

int secpolicy_vnode_chown( struct vnode *vp, const cred_t *cr, uid_t u)
{
	return (0);
}

int secpolicy_vnode_setid_retain( struct vnode *vp, const cred_t *cr,
                                  int fal)
{
	return (0);
}

int secpolicy_xvattr(struct vnode *dvp, vattr_t *vap, uid_t uid,
                     const cred_t *cr, enum vtype ty)
{
	return (0);
}

int secpolicy_setid_clear(vattr_t *vap, struct vnode *vp,
                          const cred_t *cr)
{
	return (0);
}

int secpolicy_basic_link(struct vnode *svp, const cred_t *cr)
{
	return (0);
}

int secpolicy_fs_mount_clearopts(const cred_t *cr, struct mount *mp)
{
	return (0);
}

int secpolicy_fs_mount(const cred_t *cr, struct vnode *vp, struct mount *mp)
{
	return (spl_priv_check_cred(cr, PRIV_VFS_MOUNT, 0));
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
secpolicy_setid_setsticky_clear(vnode_t *vp, vattr_t *vap, const vattr_t *ovap,
	cred_t *cr)
{
	// FIXME
	return (0);
}
